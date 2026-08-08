"""Report the retail server decisions that choose gameplay animation clips.

Usage:
    uv run elysium research verify_gameplay_actions <session>

The report keeps the raw numeric action/activity/sequence values. Their names
are intentionally not guessed: this instrument discovers the table that the
remake animation layer needs before a later RE pass assigns semantics to each
observed value.

Call sites are reported the same way. Each recorded caller resolves through the
module base stored beside the stream, so the producers that reach these five
boundaries -- the player path, the NPC path, and the probes that reach neither --
are named by address rather than inferred from which entities they touched.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import sqlite3
from typing import Any, Iterable

from research.tooling.capture.calibrate_theatre_capture import (
    DATABASE_NAME,
    read_boundary,
    resolve_session,
    table_columns,
)


REPORT_NAME = "gameplay-actions-report.json"
PLAYER_KIND = "PCLS"
SELECTION_KINDS = ("SWGT", "SHVY")
REQUIRED_HUB_BEATS = (
    "ELYSIUM_RE37_BEAT_PLAYER_LOCOMOTION",
    "ELYSIUM_RE37_BEAT_NPC_LOCOMOTION",
    "ELYSIUM_RE37_BEAT_DIALOG",
    "ELYSIUM_RE37_BEAT_REACTION",
)
# Mirrors GameplayActionFault in live_pose_hook.cpp. A record carrying either of
# these never had a live networked entity to read, so the guarded reads that
# follow describe nothing.
FAULT_ENTITY = 1 << 0
FAULT_REF_HANDLE = 1 << 1
FAULT_WITHOUT_ENTITY = FAULT_ENTITY | FAULT_REF_HANDLE
# Where the module the RVAs belong to is preferred to load, so a resolved call
# site can be pasted into a decompiler listing unchanged.
PREFERRED_IMAGE_BASE = 0x10000000
# How many of a call site's requested activities the summary names. The count of
# distinct inputs is reported beside them so a truncated tail stays visible.
CALL_SITE_INPUTS = 8

# Return sites inside CBasePlayer's ordinary activity apply path in the pinned
# vampire.dll.  The five generic hooks see many callers; these four sites are
# what turn their rows into one ordered player decision without guessing from
# the entity model or from timing alone.
PLAYER_APPLY_IDEAL_CALLER_RVA = 0x00164577
PLAYER_APPLY_WEAPON_CALLER_RVA = 0x00164582
PLAYER_APPLY_SELECTOR_CALLER_RVAS = frozenset((0x001645DA, 0x001645E3))

# Return sites inside CAI_BaseNPC's ordinary requested-activity resolver.  The
# first weapon call follows the actor/class pre-translation, the loop call
# follows NPC_TranslateActivity, the heaviest selector validates the resolved
# activity, and the weighted selector chooses the concrete sequence.  Keeping
# the sites separate is important: their identical activity values in an idle
# capture do not make them one semantic stage.
NPC_FIRST_WEAPON_CALLER_RVA = 0x00272019
NPC_LOOP_WEAPON_CALLER_RVA = 0x0027204A
NPC_VALIDATE_HEAVIEST_CALLER_RVA = 0x00272070
NPC_CHOOSE_WEIGHTED_CALLER_RVA = 0x00272299


def _distribution(rows: Iterable[tuple[Any, int]]) -> list[dict[str, Any]]:
    return [
        {"value": value, "records": count}
        for value, count in rows
    ]


def _database(session_or_database: Path) -> tuple[Path, Path]:
    if session_or_database.is_file():
        return session_or_database.parent.resolve(), session_or_database.resolve()
    session = resolve_session(str(session_or_database))
    database = session / DATABASE_NAME
    if not database.is_file():
        raise FileNotFoundError(database)
    return session, database


def _actor_models(connection: sqlite3.Connection) -> dict[int, str]:
    """Latest non-empty client model observed for each shared entity index."""
    return {
        int(index): str(model)
        for index, model in connection.execute(
            """
            SELECT a.entity_index, a.model_name
            FROM actor_observations a
            JOIN (
                SELECT entity_index, max(qpc) AS qpc
                FROM actor_observations
                WHERE entity_index IS NOT NULL AND model_name != ''
                GROUP BY entity_index
            ) latest
              ON latest.entity_index = a.entity_index AND latest.qpc = a.qpc
            WHERE a.model_name != ''
            """
        )
    }


def _actor_handle_models(connection: sqlite3.Connection) -> dict[int, str]:
    """Latest non-empty client model observed for each entity lifetime."""
    return {
        int(handle): str(model)
        for handle, model in connection.execute(
            """
            SELECT a.ref_handle, a.model_name
            FROM actor_observations a
            JOIN (
                SELECT ref_handle, max(qpc) AS qpc
                FROM actor_observations
                WHERE ref_handle NOT IN (0, 4294967295) AND model_name != ''
                GROUP BY ref_handle
            ) latest
              ON latest.ref_handle = a.ref_handle AND latest.qpc = a.qpc
            WHERE a.model_name != ''
            """
        )
    }


def _character_role(model: str | None) -> str | None:
    key = (model or "").replace("\\", "/").casefold().lstrip("/")
    if key.startswith("models/"):
        key = key[len("models/") :]
    if key.startswith("character/pc/"):
        return "player"
    if key.startswith("character/"):
        return "npc"
    return None


def _image_address(caller_rva: int | None) -> str | None:
    """One call site as it reads in a listing of the preferred image.

    Rendered rather than numeric because nothing does arithmetic on it: the
    `caller_rva` beside it is the value that indexes the database column.
    """
    if caller_rva is None:
        return None
    return f"0x{PREFERRED_IMAGE_BASE + caller_rva:08x}"


def _fault_class(row: sqlite3.Row, entities_read_cleanly: set[int]) -> str:
    """Why one record's guarded reads failed.

    The guards exist because these five boundaries are also reached with a
    `this` that is not a live entity -- a model probed before it is networked,
    or an object still under construction. Those faults describe the call site,
    not a wrong offset. A fault on an entity the same session reads cleanly
    elsewhere is the opposite: the layout held a moment earlier, so something
    about this read is wrong and the run should not pass.
    """
    if int(row["faults"]) & FAULT_WITHOUT_ENTITY or row["entity_index"] is None:
        return "unresolved_object"
    if int(row["entity_index"]) in entities_read_cleanly:
        return "live_entity"
    return "transient_state"


def _call_sites(
    rows: Iterable[sqlite3.Row], player_handles: set[int]
) -> list[dict[str, Any]]:
    """Every distinct (boundary, caller) pair the run reached.

    `caller_rva` is what makes this answerable without knowing where the module
    happened to load; a database finalized before the binding was stored has
    none, and reports the raw address alone.
    """
    sites: dict[tuple[str, int | None], dict[str, Any]] = {}
    for row in rows:
        key = (str(row["kind"]), row["caller_rva"])
        site = sites.setdefault(
            key,
            {
                "kind": key[0],
                "caller_rva": key[1],
                "image_address": _image_address(key[1]),
                "records": 0,
                "player_records": 0,
                "faulted_records": 0,
                "entities": set(),
                "inputs": Counter(),
            },
        )
        site["records"] += 1
        if row["ref_handle"] in player_handles:
            site["player_records"] += 1
        if int(row["faults"]):
            site["faulted_records"] += 1
        if row["entity_index"] is not None:
            site["entities"].add(int(row["entity_index"]))
        site["inputs"][int(row["input_value"])] += 1
    return [
        {
            **{
                key: value
                for key, value in site.items()
                if key not in ("entities", "inputs")
            },
            "distinct_entities": len(site["entities"]),
            "distinct_inputs": len(site["inputs"]),
            "top_inputs": _distribution(
                site["inputs"].most_common(CALL_SITE_INPUTS)
            ),
        }
        for site in sorted(sites.values(), key=lambda site: -site["records"])
    ]


def _beat_intervals(
    boundary: dict[str, Any] | None,
    last_qpc: int | None,
) -> list[tuple[str, int, int]]:
    if not boundary or last_qpc is None:
        return []
    beats = sorted(
        (
            (str(beat.get("marker", "")), int(beat["qpc"]))
            for beat in boundary.get("beats", [])
            if beat.get("marker") and beat.get("qpc") is not None
        ),
        key=lambda item: item[1],
    )
    return [
        (marker, start, beats[index + 1][1] if index + 1 < len(beats) else last_qpc + 1)
        for index, (marker, start) in enumerate(beats)
    ]


def _player_action_paths(
    rows: Iterable[sqlite3.Row], player_handles: set[int]
) -> dict[str, Any]:
    """Observed compact-code -> activity -> sequence paths for the player.

    One classifier return owns the rows until the next classifier return for
    the same player and thread.  Caller RVAs then select only the ordinary
    apply path's SetIdealActivity, Weapon_TranslateActivity and final selector
    calls.  A selector is intentionally optional: retail does not draw a new
    weighted sequence when the translated activity is already current.

    Current activity/sequence snapshots remain beside those reused paths.  We
    do not silently promote them to a selector result, because the distinction
    is the evidence for a held state rather than a fresh animation choice.
    """
    grouped: dict[tuple[int, int], list[sqlite3.Row]] = defaultdict(list)
    for row in rows:
        handle = row["ref_handle"]
        if handle in player_handles:
            grouped[(int(handle), int(row["thread_id"]))].append(row)

    paths: Counter[tuple[Any, ...]] = Counter()
    classifications = 0
    for group in grouped.values():
        for index, classifier in enumerate(group):
            if classifier["kind"] != PLAYER_KIND:
                continue
            classifications += 1
            following = []
            for row in group[index + 1 :]:
                if row["kind"] == PLAYER_KIND:
                    break
                following.append(row)

            ideal = next(
                (
                    row
                    for row in following
                    if row["kind"] == "IDEA"
                    and row["caller_rva"] == PLAYER_APPLY_IDEAL_CALLER_RVA
                ),
                None,
            )
            apply_rows = following
            if ideal is not None:
                ideal_index = following.index(ideal)
                apply_rows = following[ideal_index + 1 :]
                next_ideal = next(
                    (
                        offset
                        for offset, row in enumerate(apply_rows)
                        if row["kind"] == "IDEA"
                        and row["caller_rva"] == PLAYER_APPLY_IDEAL_CALLER_RVA
                    ),
                    None,
                )
                if next_ideal is not None:
                    apply_rows = apply_rows[:next_ideal]

            weapon = next(
                (
                    row
                    for row in apply_rows
                    if row["kind"] == "WTRN"
                    and row["caller_rva"] == PLAYER_APPLY_WEAPON_CALLER_RVA
                ),
                None,
            )
            selector = next(
                (
                    row
                    for row in apply_rows
                    if row["kind"] in SELECTION_KINDS
                    and row["caller_rva"] in PLAYER_APPLY_SELECTOR_CALLER_RVAS
                ),
                None,
            )

            selection_mode = (
                str(selector["kind"])
                if selector is not None
                else "reused"
                if ideal is not None
                else "no_apply"
            )
            paths[
                (
                    int(classifier["output_value"]),
                    int(ideal["input_value"]) if ideal is not None else None,
                    int(weapon["input_value"]) if weapon is not None else None,
                    int(weapon["output_value"]) if weapon is not None else None,
                    int(selector["input_value"]) if selector is not None else None,
                    (
                        int(selector["selected_sequence"])
                        if selector is not None
                        else None
                    ),
                    (
                        int(ideal["current_activity"])
                        if ideal is not None and selector is None
                        else None
                    ),
                    (
                        int(ideal["current_sequence"])
                        if ideal is not None and selector is None
                        else None
                    ),
                    int(classifier["jump_landing_state"]),
                    int(classifier["water_level"]),
                    int(classifier["active_weapon_handle"]),
                    selection_mode,
                )
            ] += 1

    names = (
        "compact_code",
        "base_activity",
        "weapon_input_activity",
        "weapon_output_activity",
        "selector_activity",
        "selected_sequence",
        "current_activity_at_apply",
        "current_sequence_at_apply",
        "jump_landing_state",
        "water_level",
        "active_weapon_handle",
        "selection_mode",
    )
    ordered = sorted(
        paths.items(),
        key=lambda item: (
            item[0][0],
            -item[1],
            tuple(-1 if value is None else value for value in item[0][1:6]),
        ),
    )
    return {
        "classifications": classifications,
        "ordinary_apply_callers": {
            "ideal": _image_address(PLAYER_APPLY_IDEAL_CALLER_RVA),
            "weapon_translation": _image_address(PLAYER_APPLY_WEAPON_CALLER_RVA),
            "weighted_selector": _image_address(
                min(PLAYER_APPLY_SELECTOR_CALLER_RVAS)
            ),
            "heaviest_selector": _image_address(
                max(PLAYER_APPLY_SELECTOR_CALLER_RVAS)
            ),
        },
        "paths": [
            {**dict(zip(names, key)), "records": count}
            for key, count in ordered
        ],
    }


def _npc_resolution_paths(
    rows: Iterable[sqlite3.Row],
    player_handles: set[int],
    actor_handle_models: dict[int, str],
    clip_models: dict[str, dict[str, list[dict[str, Any]]]] | None = None,
) -> dict[str, Any]:
    """Observed NPC requested-activity -> resolved-activity -> sequence paths.

    The four caller RVAs delimit one synchronous invocation without relying on
    timing.  A path begins at the first weapon-translation return, may contain
    up to five loop weapon translations, is validated by the heaviest selector,
    and ends at the ordinary weighted selector.  Custom-move exact-label lookup
    and disposition delegation do not cross that final boundary, so the static
    resolver rules remain the authority for those exceptional paths.
    """
    central_callers = {
        NPC_FIRST_WEAPON_CALLER_RVA,
        NPC_LOOP_WEAPON_CALLER_RVA,
        NPC_VALIDATE_HEAVIEST_CALLER_RVA,
        NPC_CHOOSE_WEIGHTED_CALLER_RVA,
    }
    grouped: dict[tuple[int, int], list[sqlite3.Row]] = defaultdict(list)
    for row in rows:
        handle = row["ref_handle"]
        if (
            handle not in player_handles
            and handle not in (None, 0, 0xFFFFFFFF)
            and row["caller_rva"] in central_callers
        ):
            grouped[(int(handle), int(row["thread_id"]))].append(row)

    paths: Counter[tuple[Any, ...]] = Counter()
    invocations = 0
    weighted_choices = 0
    resolution_only = 0
    incomplete = Counter()

    def emit(
        stages: list[sqlite3.Row],
        validation: sqlite3.Row,
        selector: sqlite3.Row | None,
    ) -> None:
        nonlocal invocations, weighted_choices, resolution_only
        first = stages[0]
        loop = stages[1:]
        terminal = selector if selector is not None else validation
        entity_index = (
            int(terminal["entity_index"])
            if terminal["entity_index"] is not None
            else None
        )
        ideal_snapshot = int(first["ideal_activity"])
        if ideal_snapshot < 0:
            ideal_snapshot = None
        key = (
            int(first["ref_handle"]),
            entity_index,
            actor_handle_models.get(int(first["ref_handle"])),
            ideal_snapshot,
            int(first["input_value"]),
            int(first["output_value"]),
            tuple(
                (int(stage["input_value"]), int(stage["output_value"]))
                for stage in loop
            ),
            int(validation["input_value"]),
            int(validation["selected_sequence"]),
            "weighted" if selector is not None else "resolution_only",
            int(selector["input_value"]) if selector is not None else None,
            int(selector["selected_sequence"]) if selector is not None else None,
            int(terminal["current_activity"]),
            int(terminal["current_sequence"]),
        )
        paths[key] += 1
        invocations += 1
        if selector is None:
            resolution_only += 1
        else:
            weighted_choices += 1

    for group in grouped.values():
        current: list[sqlite3.Row] = []
        pending: tuple[list[sqlite3.Row], sqlite3.Row] | None = None
        for row in group:
            caller = row["caller_rva"]
            if caller == NPC_FIRST_WEAPON_CALLER_RVA and row["kind"] == "WTRN":
                if pending is not None:
                    emit(*pending, None)
                    pending = None
                if current:
                    incomplete["superseded_by_next_start"] += 1
                current = [row]
                continue
            if not current:
                if (
                    pending is not None
                    and caller == NPC_CHOOSE_WEIGHTED_CALLER_RVA
                    and row["kind"] == "SWGT"
                ):
                    emit(*pending, row)
                    pending = None
                continue
            if caller == NPC_LOOP_WEAPON_CALLER_RVA and row["kind"] == "WTRN":
                current.append(row)
                continue
            if (
                caller == NPC_VALIDATE_HEAVIEST_CALLER_RVA
                and row["kind"] == "SHVY"
            ):
                if pending is not None:
                    emit(*pending, None)
                pending = (current, row)
                current = []
                continue

        if current:
            incomplete["unterminated_at_end"] += 1
        if pending is not None:
            emit(*pending, None)

    names = (
        "ref_handle",
        "entity_index",
        "model",
        "ideal_activity_snapshot",
        "actor_translation_activity",
        "first_weapon_activity",
        "npc_weapon_iterations",
        "resolved_activity",
        "validation_sequence",
        "selection_mode",
        "selector_activity",
        "selected_sequence",
        "current_activity_at_resolution",
        "current_sequence_at_resolution",
    )
    ordered = sorted(
        paths.items(),
        key=lambda item: (
            "" if item[0][2] is None else item[0][2],
            item[0][0],
            -1 if item[0][1] is None else item[0][1],
            -1 if item[0][3] is None else item[0][3],
            -item[1],
        ),
    )
    rendered = []
    for key, count in ordered:
        path = dict(zip(names, key))
        path["npc_weapon_iterations"] = [
            {"npc_activity": source, "weapon_activity": translated}
            for source, translated in path["npc_weapon_iterations"]
        ]
        model = path["model"]
        if model is not None and clip_models:
            for field, activity_field, sequence_field in (
                (
                    "validation_clip",
                    "resolved_activity",
                    "validation_sequence",
                ),
                ("selected_clip", "selector_activity", "selected_sequence"),
            ):
                activity = path[activity_field]
                sequence = path[sequence_field]
                clips = (
                    clip_models.get(model, {}).get(str(activity), [])
                    if activity is not None and sequence is not None
                    else []
                )
                match = next(
                    (
                        clip
                        for clip in clips
                        if int(clip["sequence_index"]) == int(sequence)
                    ),
                    None,
                )
                if match is not None:
                    path[field] = match
        rendered.append({**path, "records": count})

    return {
        "invocations": invocations,
        "weighted_choices": weighted_choices,
        "resolution_only": resolution_only,
        "distinct_paths": len(paths),
        "callers": {
            "first_weapon_translation": _image_address(
                NPC_FIRST_WEAPON_CALLER_RVA
            ),
            "loop_weapon_translation": _image_address(
                NPC_LOOP_WEAPON_CALLER_RVA
            ),
            "activity_validation": _image_address(
                NPC_VALIDATE_HEAVIEST_CALLER_RVA
            ),
            "weighted_sequence_choice": _image_address(
                NPC_CHOOSE_WEIGHTED_CALLER_RVA
            ),
        },
        "incomplete": dict(sorted(incomplete.items())),
        "paths": rendered,
    }


def verify(session_or_database: Path) -> dict[str, Any]:
    session, database = _database(session_or_database)
    clip_table_path = session / "clip-table.json"
    clip_table = (
        json.loads(clip_table_path.read_text(encoding="utf-8"))
        if clip_table_path.is_file()
        else {}
    )
    uri = database.as_uri() + "?mode=ro"
    connection = sqlite3.connect(uri, uri=True)
    connection.row_factory = sqlite3.Row
    try:
        tables = {
            str(name)
            for (name,) in connection.execute(
                "SELECT name FROM sqlite_master WHERE type IN ('table', 'view')"
            )
        }
        if "gameplay_action_events" not in tables:
            report = {
                "session": str(session),
                "database": str(database),
                "available": False,
                "reason": "database predates the gameplay-action stream",
                "complete": False,
            }
            (session / REPORT_NAME).write_text(
                json.dumps(report, indent=2) + "\n", encoding="utf-8"
            )
            return report

        boundary, boundary_source = read_boundary(connection, session)
        actor_models = (
            _actor_models(connection) if "actor_observations" in tables else {}
        )
        actor_handle_models = (
            _actor_handle_models(connection)
            if "actor_observations" in tables
            else {}
        )
        binding = (
            connection.execute(
                "SELECT * FROM gameplay_action_binding"
            ).fetchone()
            if "gameplay_action_binding" in tables
            else None
        )
        # A database finalized before the module base was stored still answers
        # every other question; only its call sites stay unresolved.
        columns = table_columns(connection, "gameplay_action_events")
        rva_column = (
            "caller_rva" if "caller_rva" in columns else "NULL AS caller_rva"
        )
        rows = list(
            connection.execute(
                f"""
                SELECT kind, sequence_number, qpc, thread_id, ref_handle,
                       entity_index, entity_serial,
                       caller_address, {rva_column},
                       input_value, output_value, selection_argument,
                       selected_sequence, current_activity, ideal_activity,
                       current_sequence, active_weapon_handle, water_level,
                       jump_landing_state, faults
                FROM gameplay_action_events ORDER BY sequence_number
                """
            )
        )
        player_handles = {
            int(row["ref_handle"])
            for row in rows
            if row["kind"] == PLAYER_KIND and row["ref_handle"] not in (0, 0xFFFFFFFF)
        }
        player_indices = {
            int(row["entity_index"])
            for row in rows
            if row["ref_handle"] in player_handles and row["entity_index"] is not None
        }
        selection_rows = [row for row in rows if row["kind"] in SELECTION_KINDS]
        player_selections = [
            row for row in selection_rows if row["ref_handle"] in player_handles
        ]
        npc_selections = [
            row
            for row in selection_rows
            if row["ref_handle"] not in player_handles
            and row["ref_handle"] not in (0, 0xFFFFFFFF)
            and row["entity_index"] is not None
            and _character_role(
                actor_handle_models.get(int(row["ref_handle"]))
            ) == "npc"
        ]
        entity_indices = {
            int(row["entity_index"])
            for row in rows
            if row["entity_index"] is not None
        }
        joined_indices = entity_indices & actor_models.keys()
        npc_indices = {
            int(row["entity_index"])
            for row in npc_selections
            if row["entity_index"] is not None
        }
        required_identity_indices = player_indices | npc_indices
        by_kind = Counter(str(row["kind"]) for row in rows)
        entities_read_cleanly = {
            int(row["entity_index"])
            for row in rows
            if int(row["faults"]) == 0 and row["entity_index"] is not None
        }
        faulted_rows = [row for row in rows if int(row["faults"]) != 0]
        fault_classes = Counter(
            _fault_class(row, entities_read_cleanly) for row in faulted_rows
        )
        fault_sites = Counter(
            (
                _fault_class(row, entities_read_cleanly),
                str(row["kind"]),
                row["caller_rva"],
                int(row["faults"]),
            )
            for row in faulted_rows
        )
        last_qpc = max((int(row["qpc"]) for row in rows), default=None)

        beat_sections = []
        for marker, start, end in _beat_intervals(boundary, last_qpc):
            section_rows = [row for row in rows if start <= int(row["qpc"]) < end]
            section_player = sum(
                1 for row in section_rows if row["ref_handle"] in player_handles
            )
            beat_sections.append(
                {
                    "marker": marker,
                    "start_qpc": start,
                    "end_qpc_exclusive": end,
                    "records": len(section_rows),
                    "player_records": section_player,
                    "npc_or_other_records": len(section_rows) - section_player,
                    "by_kind": dict(Counter(str(row["kind"]) for row in section_rows)),
                }
            )

        observed_beats = {
            str(beat.get("marker")) for beat in (boundary or {}).get("beats", [])
        }
        required_beats = (
            REQUIRED_HUB_BEATS
            if (boundary or {}).get("recipe") == "sm_hub_1"
            else ()
        )
        checks = {
            "records_captured": bool(rows),
            "guarded_reads_explained": fault_classes["live_entity"] == 0,
            "player_classifier_observed": bool(player_handles),
            "player_sequence_selected": any(
                int(row["selected_sequence"]) >= 0 for row in player_selections
            ),
            "npc_sequence_selected": any(
                int(row["selected_sequence"]) >= 0 for row in npc_selections
            ),
            "selected_characters_join_actor_census": bool(required_identity_indices)
            and required_identity_indices <= actor_models.keys(),
            "required_operator_beats_marked": all(
                marker in observed_beats for marker in required_beats
            ),
        }
        report = {
            "session": str(session),
            "database": str(database),
            "available": True,
            "boundary_source": boundary_source,
            "records": len(rows),
            "by_kind": dict(sorted(by_kind.items())),
            "faulted_records": len(faulted_rows),
            "faults": {
                "by_class": dict(sorted(fault_classes.items())),
                "sites": [
                    {
                        "fault_class": fault_class,
                        "kind": kind,
                        "caller_rva": caller_rva,
                        "image_address": _image_address(caller_rva),
                        "fault_bits": faulted,
                        "records": count,
                    }
                    for (
                        fault_class,
                        kind,
                        caller_rva,
                        faulted,
                    ), count in fault_sites.most_common()
                ],
            },
            "binding": (
                dict(binding)
                if binding is not None
                else {"reason": "database predates the module binding"}
            ),
            "call_sites": _call_sites(rows, player_handles),
            "players": {
                "handles": sorted(player_handles),
                "entity_indices": sorted(player_indices),
                "models": {
                    str(index): actor_models[index]
                    for index in sorted(player_indices & actor_models.keys())
                },
                "classifications": _distribution(
                    sorted(
                        Counter(
                            int(row["output_value"])
                            for row in rows
                            if row["kind"] == PLAYER_KIND
                        ).items()
                    )
                ),
                "selected_sequences": _distribution(
                    sorted(
                        Counter(
                            int(row["selected_sequence"])
                            for row in player_selections
                        ).items()
                    )
                ),
            },
            "player_action_paths": _player_action_paths(rows, player_handles),
            "npc_resolution_paths": _npc_resolution_paths(
                rows,
                player_handles,
                actor_handle_models,
                clip_table.get("models", {}),
            ),
            "clip_table": {
                "available": bool(clip_table),
                "path": str(clip_table_path),
                "ordering": clip_table.get("ordering"),
            },
            "npcs": {
                "entity_indices": sorted(npc_indices),
                "selected_sequences": _distribution(
                    sorted(
                        Counter(
                            int(row["selected_sequence"])
                            for row in npc_selections
                        ).items()
                    )
                ),
            },
            "activities": {
                "ideal": _distribution(
                    sorted(Counter(int(row["ideal_activity"]) for row in rows).items())
                ),
                "current": _distribution(
                    sorted(
                        Counter(int(row["current_activity"]) for row in rows).items()
                    )
                ),
                "selector_inputs": _distribution(
                    sorted(
                        Counter(
                            int(row["input_value"]) for row in selection_rows
                        ).items()
                    )
                ),
            },
            "identity_join": {
                "action_entity_indices": sorted(entity_indices),
                "joined_entity_indices": sorted(joined_indices),
                "unjoined_entity_indices": sorted(entity_indices - joined_indices),
                "models": {
                    str(index): actor_models[index] for index in sorted(joined_indices)
                },
            },
            "beats": beat_sections,
            "checks": checks,
            "complete": all(checks.values()),
        }
    finally:
        connection.close()

    (session / REPORT_NAME).write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
    )
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    args = parser.parse_args()
    report = verify(args.session)
    print(json.dumps(report, indent=2))
    return 0 if report["complete"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
