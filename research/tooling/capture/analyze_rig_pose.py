"""Turn a `life_rig_pose` session into the pose fixture the runtime is held to.

The capture records what retail drew: `C_BaseAnimating::SetupBones` leaves a
frame's bone-to-world matrices in the entity's own array, and this reads them
back beside the state needed to rebuild that same frame offline -- which clip
the body was playing, at what cycle, with which contributions at which weights.

Three joins turn a stream of hooks into one row per drawn frame, and each is
structural rather than inferred:

* **A pose to its model.** `client.setup_bones` runs on the renderable
  subobject, so its `this` is the entity plus four; `client.get_studio_hdr`
  reports the entity itself. Every posed entity in a session joins at that
  delta, so the pose's model -- and therefore its bone names and its export
  stem -- is read rather than guessed.
* **A pose to its contributions.** `client.accumulate_sequence_pose` runs
  nested inside the `SetupBones` call that consumes it, at depth one against
  depth zero on the same thread -- but the pose target is sampled, so most of
  those enclosing calls are declined and the nesting alone would strand the
  contributions of every frame that was not recorded. They carry the same frame
  clock instead, so a frame's contributions are the accumulate calls sharing its
  time whether or not its own `SetupBones` was kept. The nesting is still read,
  as the cross-check that the clock join agrees with the call structure.
* **A pose to the player's own state.** `SetupBones` is passed the frame time
  as its fourth argument, and it is the same clock `vampire.player_item_post_frame`
  reads as `curtime`, matching to under a millisecond -- so the sequence, cycle
  and pose parameters recorded server-side attach to the client-side pose of the
  same frame.

The matrices are **bone-to-world**: they carry the entity's placement in the
map as well as its pose. A comparison against an evaluated pose has to divide
that out, which is why every row states its root bone's matrix beside the rest
rather than pre-multiplying it away here -- the reader chooses its own frame.

The fixture is written beside the session as `pose_oracle.json`, in the
export's own stem and bone-name namespace, so the test that reads it needs
neither the install nor the capture tooling.

Usage:
    uv run elysium research analyze_rig_pose
    uv run elysium research analyze_rig_pose --session <capture directory>
"""

from __future__ import annotations

import argparse
from collections import Counter
import bisect
import json
from pathlib import Path
import struct
import sys
from typing import Any, Iterable

from research.tooling.capture.rig_parity import (
    load_export_index,
    model_key,
    stems_by_model,
)

REPORT_NAME = "pose_oracle.json"
RECIPE = "life_rig_pose"

#: `client.setup_bones` runs on the renderable subobject: its `this` is the
#: entity plus four, and `client.get_studio_hdr` reports the entity itself.
#: Measured, not assumed -- every posed entity of a session joins at this delta,
#: and `entity_delta_agreement` reports the figure so a session where it does not
#: hold fails loudly instead of resolving every pose to no model.
ENTITY_DELTA = 4

#: One matrix3x4_t is twelve floats: three rows of a rotation-scale basis, each
#: followed by that row's translation component.
MATRIX_FLOATS = 12

#: The frame clock is shared but not identical -- the server writes `curtime`
#: once a frame and the client is passed it a moment later. A frame is 16 ms at
#: sixty; half of that separates "the same frame" from "the next one".
CLOCK_TOLERANCE_SECONDS = 0.008

POSE_TARGET = "client.setup_bones"
MODEL_TARGET = "client.get_studio_hdr"
CONTRIBUTION_TARGET = "client.accumulate_sequence_pose"
PLAYER_TARGET = "vampire.player_item_post_frame"


def _float_from_word(word: str | None) -> float | None:
    """A stack word carrying float bits, as the float it is."""
    if word is None:
        return None
    raw = int(str(word), 16) & 0xFFFFFFFF
    return struct.unpack("<f", struct.pack("<I", raw))[0]


def _offset_pointer(pointer: str, delta: int) -> str:
    return hex(int(str(pointer), 16) + delta)


# --- the session ------------------------------------------------------------------------------


def latest_session() -> Path:
    from elysium_pipeline.paths import research_root

    root = research_root() / "frida"
    candidates = sorted(
        (path for path in root.glob(f"*-{RECIPE}") if (path / "events.jsonl").is_file()),
        key=lambda path: path.name,
    )
    if not candidates:
        raise FileNotFoundError(
            f"no {RECIPE} capture exists below {root}; run "
            f"`uv run elysium research frida_probe attach --recipe {RECIPE}` first"
        )
    return candidates[-1]


def read_events(session: Path) -> list[dict[str, Any]]:
    """Every event in emission order, which is what the nesting scan rides on."""
    path = session / "events.jsonl"
    if not path.is_file():
        raise FileNotFoundError(path)
    events = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip():
            events.append(json.loads(line))
    return events


# --- the joins --------------------------------------------------------------------------------


def entity_models(events: Iterable[dict[str, Any]]) -> dict[str, list[dict[str, Any]]]:
    """entity pointer -> every model it answered with, in the order it did.

    Keyed by the pointer `client.get_studio_hdr` reports, which is the entity
    itself. **An entity outlives the model it draws**: the player keeps one
    pointer across a body swap, so a census that kept the first answer per
    pointer would name the whole session after whichever body loaded first. The
    list is what the census can state; which entry a given frame belongs to is
    decided per frame by the bone count the engine filled.
    """
    calls: dict[int, dict[str, Any]] = {}
    out: dict[str, list[dict[str, Any]]] = {}
    for event in events:
        if event.get("target") != MODEL_TARGET:
            continue
        if event.get("kind") == "call":
            calls[event["sequence"]] = event
            continue
        if event.get("kind") != "return":
            continue
        call = calls.get(event["sequence"])
        fields = event.get("fields") or {}
        model = fields.get("model")
        if call is None or not isinstance(model, str) or not model:
            continue
        seen = out.setdefault(call["ecx"], [])
        if not any(entry["model"] == model for entry in seen):
            seen.append({"model": model, "numbones": fields.get("numbones")})
    return out


def resolve_body(
    bone_count: int,
    stated: str | None,
    census: list[str],
    session_models: list[str],
    contribution_models: list[str],
    bone_count_of: dict[str, int],
) -> tuple[str | None, str]:
    """Which model a frame's matrices belong to, or None and why not.

    **The bone count the engine filled is the hard measurement**, and a model may
    only name a frame whose array is exactly as long as that model's own bone
    list. Naming an 88-bone array with a 79-name list does not fail -- it returns
    79 matrices under the wrong names and silently drops nine, which is worse
    than having no answer at all.

    A pose record that names its own model answers directly, and that is the
    whole answer -- the read is off the entity being posed, so no join can
    contradict it. A session captured before the pose target read one falls back
    to the census, which is evidence rather than an answer: an entity outlives
    the model it draws, so the census is accepted where it agrees with the count,
    and where it cannot the answer is whichever model the session named that both
    fits the count and, where more than one does, is named by this frame's own
    contributions. Anything still ambiguous is refused.
    """
    def fits(models: Iterable[str]) -> list[str]:
        return [
            model for model in dict.fromkeys(models)
            if bone_count_of.get(model) == bone_count
        ]

    if stated is not None:
        if bone_count_of.get(stated) == bone_count:
            return stated, "stated by the pose record"
        # The model the pose names cannot be the one whose array this is. That is a
        # decode fault in the read chain, not a body swap, and guessing past it would
        # bury it.
        return None, "the model the pose names does not carry this bone count"

    census_fits = fits(census)
    if len(census_fits) == 1:
        return census_fits[0], "census"
    if len(census_fits) > 1:
        corroborated = [model for model in census_fits if model in contribution_models]
        if len(corroborated) == 1:
            return corroborated[0], "census, narrowed by a contribution"
        return None, "several of the entity's own models carry this bone count"

    wider = fits(session_models)
    corroborated = [model for model in wider if model in contribution_models]
    if len(corroborated) == 1:
        return corroborated[0], "re-attributed, named by a contribution"
    if len(wider) == 1:
        return wider[0], "re-attributed by bone count"
    if not wider:
        return None, "no model the session named carries this bone count"
    return None, "several models carry this bone count"


def entity_delta_agreement(
    events: Iterable[dict[str, Any]], models: dict[str, dict[str, Any]]
) -> dict[str, int]:
    """How many posed entities resolve to a model at the measured delta.

    A session where this is not total has either lost the model target or met a
    layout this delta does not describe, and every pose would silently resolve to
    no model -- so it is counted rather than assumed.
    """
    posed = {
        event["ecx"]
        for event in events
        if event.get("target") == POSE_TARGET and event.get("kind") == "call"
    }
    matched = sum(
        1 for pointer in posed if _offset_pointer(pointer, -ENTITY_DELTA) in models
    )
    return {"posed_entities": len(posed), "resolved_to_a_model": matched}


def player_states(events: Iterable[dict[str, Any]]) -> list[dict[str, Any]]:
    """The player's own per-frame state, ordered by the clock it was read on."""
    states = []
    for event in events:
        if event.get("target") != PLAYER_TARGET or event.get("kind") != "call":
            continue
        fields = event.get("fields") or {}
        if not isinstance(fields.get("curtime"), (int, float)):
            continue
        states.append({"entity": event.get("ecx"), **fields})
    states.sort(key=lambda state: state["curtime"])
    return states


def nearest_state(
    states: list[dict[str, Any]],
    curtime: float,
    tolerance: float = CLOCK_TOLERANCE_SECONDS,
) -> dict[str, Any] | None:
    """The player state read closest to a frame time, or None outside tolerance."""
    if not states:
        return None
    times = [state["curtime"] for state in states]
    index = bisect.bisect_left(times, curtime)
    best: tuple[float, dict[str, Any]] | None = None
    for candidate in (index - 1, index):
        if 0 <= candidate < len(states):
            distance = abs(times[candidate] - curtime)
            if best is None or distance < best[0]:
                best = (distance, states[candidate])
    if best is None or best[0] > tolerance:
        return None
    return best[1]


def contributions(events: Iterable[dict[str, Any]]) -> tuple[list[dict[str, Any]], int]:
    """Every accumulated contribution, with the frame clock that places it.

    A contribution states the model it posed, the sequence index inside that
    model and the weight it was accumulated at. It carries the same `curtime`
    the pose target is passed, which is what attaches it to a frame whose own
    `SetupBones` the sampler may have declined. A record with no clock cannot be
    placed at all and is counted rather than attached to a neighbour.
    """
    rows: list[dict[str, Any]] = []
    unplaceable = 0
    for event in events:
        if event.get("target") != CONTRIBUTION_TARGET or event.get("kind") != "call":
            continue
        fields = event.get("fields") or {}
        words = event.get("stack_words") or []
        curtime = fields.get("curtime")
        if not isinstance(curtime, (int, float)):
            unplaceable += 1
            continue
        rows.append(
            {
                "curtime": float(curtime),
                "thread_id": event.get("thread_id"),
                "depth": event.get("depth"),
                "model": fields.get("model"),
                "sequence": _word(words, 4),
                "weight": _float_from_word(words[7] if len(words) > 7 else None),
            }
        )
    rows.sort(key=lambda row: row["curtime"])
    return rows, unplaceable


def contributions_at(
    rows: list[dict[str, Any]],
    curtime: float,
    tolerance: float = CLOCK_TOLERANCE_SECONDS,
) -> list[dict[str, Any]]:
    """Every contribution sharing a frame's clock, in accumulation order."""
    if not rows:
        return []
    times = [row["curtime"] for row in rows]
    low = bisect.bisect_left(times, curtime - tolerance)
    high = bisect.bisect_right(times, curtime + tolerance)
    return rows[low:high]


def group_frames(events: Iterable[dict[str, Any]]) -> tuple[list[dict[str, Any]], int]:
    """One record per captured pose, and how many contributions nested inside one.

    Walks the stream in emission order: a `setup_bones` call opens a frame and
    its return closes it carrying the matrices. The nesting count is the
    cross-check on the clock join -- an accumulate seen while a frame is open is
    one the call structure and the clock must both place in that frame.
    """
    frames: list[dict[str, Any]] = []
    open_frames: dict[int, dict[str, Any]] = {}
    nested = 0
    for event in events:
        target = event.get("target")
        kind = event.get("kind")
        if target == POSE_TARGET and kind == "call":
            open_frames[event["sequence"]] = {
                "entity": event["ecx"],
                "thread_id": event.get("thread_id"),
                "curtime": _float_from_word((event.get("stack_words") or [None] * 5)[4]),
            }
            continue
        if target == CONTRIBUTION_TARGET and kind == "call":
            if any(
                frame["thread_id"] == event.get("thread_id")
                for frame in open_frames.values()
            ):
                nested += 1
            continue
        if target == POSE_TARGET and kind == "return":
            frame = open_frames.pop(event["sequence"], None)
            if frame is None:
                continue
            fields = event.get("fields") or {}
            matrices = fields.get("bone_matrices")
            bone_count = fields.get("bone_count")
            if not isinstance(matrices, list) or not isinstance(bone_count, int):
                continue
            frame["bone_count"] = bone_count
            frame["matrices"] = matrices
            # A pose target that reads the model off the entity it is posing names its
            # own body; a session captured before it did says nothing here and is
            # resolved through the census instead.
            stated = fields.get("model")
            frame["stated_model"] = stated if isinstance(stated, str) and stated else None
            frames.append(frame)
    return frames, nested


def _word(words: list[Any], index: int) -> int | None:
    if index >= len(words) or words[index] is None:
        return None
    value = int(str(words[index]), 16) & 0xFFFFFFFF
    return value - 0x100000000 if value >= 0x80000000 else value


def shape_matrices(matrices: list[float], bone_count: int) -> list[list[float]]:
    """A flat float run as one matrix3x4_t a bone, trimmed to the stated count."""
    usable = min(bone_count, len(matrices) // MATRIX_FLOATS)
    return [
        matrices[index * MATRIX_FLOATS:(index + 1) * MATRIX_FLOATS]
        for index in range(usable)
    ]


def name_matrices(
    matrices: list[list[float]], bone_names: list[str]
) -> tuple[dict[str, list[float]], int]:
    """Matrices keyed by bone name, and how many the model's own list cannot name.

    The capture reads the entity's array, whose length is the count the engine
    filled; the install's bone list is the authority on what those slots are
    called. A surplus is reported rather than dropped silently, because the two
    disagreeing means the model resolved wrongly.
    """
    named = {
        bone_names[index]: matrix
        for index, matrix in enumerate(matrices)
        if index < len(bone_names)
    }
    return named, max(0, len(matrices) - len(bone_names))


# --- the fixture ------------------------------------------------------------------------------


def build(session: Path, export_root: Path) -> dict[str, Any]:
    # Imported here rather than at module scope: `install` resolves the VtMB root the
    # moment it loads, and every join above is pure and tested without one.
    from elysium_pipeline.formats import install, mdl_skel

    events = read_events(session)
    models = entity_models(events)
    delta = entity_delta_agreement(events, models)
    states = player_states(events)
    frames, nested = group_frames(events)
    accumulated, unplaceable = contributions(events)

    index = install.build_index(dirs=("models",), verbose=False)
    export_index = load_export_index(export_root)
    stem_of = stems_by_model(export_index)
    bones: dict[str, list[str]] = {}

    def bone_names(key: str) -> list[str]:
        if key not in bones:
            data = install.read(index, key)
            bones[key] = mdl_skel.bone_names(data) if data else []
        return bones[key]

    # Every model the session named, from either direction: the census answers for the
    # entity that drew, and a contribution answers for the model whose sequence posed it.
    # A body swapped into mid-session is named by the second even when the census, keyed
    # per entity, never got to state it.
    session_models: list[str] = []
    for observations in models.values():
        for entry in observations:
            session_models.append(model_key(entry["model"]))
    for contribution in accumulated:
        if contribution["model"]:
            session_models.append(model_key(contribution["model"]))
    for frame in frames:
        if frame.get("stated_model"):
            session_models.append(model_key(frame["stated_model"]))
    session_models = list(dict.fromkeys(session_models))
    bone_count_of = {key: len(bone_names(key)) for key in session_models}
    # The census carries the header's own `numbones` beside the model it named; the
    # install's bone list is what the matrices are keyed by. The two disagreeing means one
    # of them is not describing the model the other is, so it is counted rather than
    # reconciled.
    census_bone_count_disagreements = 0
    for observations in models.values():
        for entry in observations:
            stated = entry.get("numbones")
            listed = bone_count_of.get(model_key(entry["model"]))
            if isinstance(stated, int) and listed and stated != listed:
                census_bone_count_disagreements += 1

    rows: list[dict[str, Any]] = []
    unresolved_models = Counter()
    attribution = Counter()
    surplus_total = 0
    for frame in frames:
        census = [
            model_key(entry["model"])
            for entry in models.get(_offset_pointer(frame["entity"], -ENTITY_DELTA), [])
        ]
        frame_contributions = (
            contributions_at(accumulated, frame["curtime"]) if frame["curtime"] else []
        )
        contribution_models = [
            model_key(contribution["model"])
            for contribution in frame_contributions
            if contribution["model"]
        ]
        stated = frame.get("stated_model")
        key, reason = resolve_body(
            frame["bone_count"], model_key(stated) if stated else None,
            census, session_models, contribution_models, bone_count_of,
        )
        if key is None:
            unresolved_models[reason] += 1
            continue
        attribution[reason] += 1
        stem = stem_of.get(key)
        if stem is None:
            unresolved_models[key] += 1
            continue
        names = bone_names(key)
        if not names:
            unresolved_models[f"{key} (not in the install)"] += 1
            continue
        shaped = shape_matrices(frame["matrices"], frame["bone_count"])
        named, surplus = name_matrices(shaped, names)
        surplus_total += surplus
        # The player state is read server-side and names no client entity, so it is
        # attached only to a body drawn from the player namespace. Attaching it to
        # whatever else shared the frame would label an NPC pose with the player cycle.
        is_player = "/character/pc/" in f"/{key}"
        state = (
            nearest_state(states, frame["curtime"])
            if is_player and frame["curtime"] else None
        )
        rows.append(
            {
                "stem": stem,
                "model": key,
                "curtime": frame["curtime"],
                "bone_count": frame["bone_count"],
                # The matrices are bone-to-world, so a reader comparing a pose has to
                # divide this one out. It is stated because the map below is keyed by
                # name and written sorted, which loses the bone order the model states.
                "root_bone": names[0],
                "is_player": is_player,
                "player_state": {
                    "sequence": state.get("sequence"),
                    "cycle": state.get("cycle"),
                    "activity": state.get("activity"),
                    "ideal_activity": state.get("ideal_activity"),
                    "aim_yaw": state.get("aim_yaw"),
                    "velocity": state.get("velocity"),
                    "active_weapon": state.get("active_weapon"),
                } if state else None,
                "contributions": [
                    {
                        "model": model_key(contribution["model"])
                        if contribution["model"] else None,
                        "owner_stem": stem_of.get(model_key(contribution["model"]))
                        if contribution["model"] else None,
                        "sequence": contribution["sequence"],
                        "weight": contribution["weight"],
                    }
                    for contribution in frame_contributions
                ],
                "bones": named,
            }
        )

    by_stem = Counter(row["stem"] for row in rows)
    report = {
        "schema": "elysium.pose-oracle",
        "version": 1,
        "session": str(session),
        "export_root": str(export_root),
        "entity_delta": ENTITY_DELTA,
        "entity_delta_agreement": delta,
        "frames_captured": len(frames),
        "frames_resolved": len(rows),
        "frames_by_stem": dict(by_stem.most_common()),
        "frames_by_attribution": dict(attribution.most_common()),
        "census_bone_count_disagreements": census_bone_count_disagreements,
        "frames_with_player_state": sum(1 for row in rows if row["player_state"]),
        "frames_with_contributions": sum(1 for row in rows if row["contributions"]),
        "contributions_recorded": len(accumulated),
        "contributions_without_a_clock": unplaceable,
        "contributions_nested_in_a_recorded_call": nested,
        "bones_beyond_the_model_bone_list": surplus_total,
        "unresolved": dict(unresolved_models.most_common()),
        "frames": rows,
    }
    (session / REPORT_NAME).write_text(
        json.dumps(report, indent=1, sort_keys=True) + "\n", encoding="utf-8"
    )
    return report


def print_summary(report: dict[str, Any]) -> None:
    delta = report["entity_delta_agreement"]
    print(f"Pose oracle: {report['session']}")
    print(
        f"  entity join            {delta['resolved_to_a_model']}"
        f"/{delta['posed_entities']} posed entities named a model"
    )
    print(f"  frames captured        {report['frames_captured']}")
    print(f"  frames resolved        {report['frames_resolved']}")
    for stem, count in report["frames_by_stem"].items():
        print(f"    - {stem}: {count}")
    for reason, count in report["frames_by_attribution"].items():
        print(f"    named by {reason}: {count}")
    if report["census_bone_count_disagreements"]:
        print(
            "  WARNING - census rows whose stated bone count is not the install's: "
            f"{report['census_bone_count_disagreements']}"
        )
    print(f"  with player state      {report['frames_with_player_state']}")
    print(f"  with contributions     {report['frames_with_contributions']}")
    print(
        f"  contributions          {report['contributions_recorded']} recorded, "
        f"{report['contributions_nested_in_a_recorded_call']} nested in a kept call, "
        f"{report['contributions_without_a_clock']} unplaceable"
    )
    if report["bones_beyond_the_model_bone_list"]:
        print(
            "  WARNING - bones past the model's own list: "
            f"{report['bones_beyond_the_model_bone_list']}"
        )
    if report["unresolved"]:
        print(f"  unresolved             {report['unresolved']}")
    print(f"  report {Path(report['session']) / REPORT_NAME}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session", type=Path,
                        help="A capture directory; the newest life_rig_pose one when omitted.")
    parser.add_argument("--export", type=Path,
                        help="The export root; $ELYSIUM_EXPORT_ROOT when omitted.")
    arguments = parser.parse_args()
    session = arguments.session.resolve() if arguments.session else latest_session()
    if not session.is_dir():
        raise NotADirectoryError(session)
    if arguments.export:
        export_root = arguments.export.resolve()
    else:
        from elysium_pipeline.paths import export_root as default_export_root

        export_root = default_export_root()
    report = build(session, export_root)
    print_summary(report)
    return 0 if report["frames_resolved"] else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
    except Exception as exc:  # noqa: BLE001 - the CLI reports its own failure
        print(f"WARNING - pose analysis failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
