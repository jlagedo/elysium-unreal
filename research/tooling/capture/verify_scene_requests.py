"""Decide whether a finalized capture attributes each pose group to what asked for it.

Usage: uv run elysium research verify_scene_requests <session> [<session> ...]

This is CAP2.6's report. It reads a finalized database read-only and answers one
question: does the run name, for every animation the scene system requested, the
caller, the target entity and the time -- and does that request join to the
client pose group that carried it out.

The join is the result. A server request and a client sequence change name the
same entity only if the index each carries independently agrees, so the report
never pairs a request with a pose group by time or by model; it pairs them by
index, bounded by the serial and by the interval an address was one actor, and
reports every population that does not resolve rather than dropping it.

Four bounds travel with this report.

The client sequence change is an effect, not a request. `m_nSequence` is
networked, so a scene asks on the server and the client acts on it some frames
later. A `SEVT` record witnesses when a request was made and a `SEQC` record when
a client entity changed; the causal claim is only ever that a scene asked and an
entity changed inside its interval.

An activity change is observable in this stream only as a sequence change. The
server selector is located and indexed by the gameplay-actions specification,
but this scene report does not record its translation stages; it counts sequence
transitions and dispatched sequence labels and never infers an activity from one.

Map scenes and dialogue scenes keep different clocks. One hook covers both
because they share the dispatch body, and the record carries the vftable so the
two populations stay separable; the timing comparison across runs is made over
map scenes only.

A caller is an address, not a name. Which frame asked is a lookup against the
case specification by nearest preceding seed, and an address inside a function
the specification does not declare is reported unresolved rather than attributed
to the seed below it.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sqlite3
import sys
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from research.tooling.capture.calibrate_theatre_capture import (  # noqa: E402
    DATABASE_NAME,
    artifact_key_values,
    compared_maps,
    open_database,
    resolve_session,
    scene_bytes_expression,
    stream_headers,
    table_columns,
)
from research.tooling.capture.verify_source_attribution import (  # noqa: E402
    address,
    nearest_preceding_label,
    spec_functions,
)

SCENE_STREAM = "scene"
SCENE_REQUEST_BYTES = 600
SEQUENCE_CHANGE_BYTES = 68

# Mirrors SceneFault in the probe, masked to the bits this report owns so a
# widened probe never reads as a regression here.
FAULT_NAMES = {
    1 << 0: "scene-entity",
    1 << 1: "event",
    1 << 2: "scene-file-truncated",
    1 << 3: "param-truncated",
    1 << 4: "actor-name-truncated",
    1 << 5: "no-target-entity",
    1 << 6: "no-entity-index",
    1 << 7: "no-scene-scope",
    1 << 8: "no-generation",
    1 << 9: "transition-array",
    1 << 10: "rename-unavailable",
}
FAULT_MASK = sum(FAULT_NAMES)
# An actor the scene could not resolve and a binding outside an anim-set scope
# are outcomes the run reports rather than failures of the capture.
EXPECTED_FAULTS = (1 << 5) | (1 << 10)

# CChoreoEvent types that carry an animation sequence label.
GESTURE_EVENT = 6
SEQUENCE_EVENT = 7
ANIMATION_EVENTS = (GESTURE_EVENT, SEQUENCE_EVENT)

# Measured on CAP2.5's two complete cutscenes; the scene stream must not move
# either past these.
BASELINE_MEAN_MB_PER_SECOND = 8.68
BASELINE_QUEUE_HIGH_WATER = 368

MAX_REPORTED_OFFENDERS = 64
MAX_REPORTED_CALLERS = 64

VAMPIRE_SPEC = (
    Path(__file__).resolve().parents[3]
    / "research"
    / "cases"
    / "animation-pose"
    / "specs"
    / "scene_requests.json"
)
VAMPIRE_IMAGE_BASE = 0x10000000


def _model_key(path: str) -> str:
    """One comparable form for a model path from either side of the join.

    A scene's animation-set keyvalue is authored from the game root, so it reads
    `models/cinematic/.../Courtroom_bip1.mdl`. The runtime studio header's own
    name field is relative to that directory and reads
    `cinematic/.../Courtroom_bip1.mdl`. Comparing them without stripping the
    prefix can never match, which is not a disagreement about which model played
    but a difference in what each field is rooted at. Case and separator differ
    between the two as well -- one scene names SANTA_MONICA where the census
    names Santa_Monica.
    """
    key = path.replace("\\", "/").casefold().lstrip("/")
    prefix = "models/"
    if key.startswith(prefix):
        key = key[len(prefix) :]
    return key


def prepare(connection: sqlite3.Connection) -> None:
    """Project the rows each section reads into indexed temp tables.

    The scene tables store their raw header inline, so grouping them directly
    drags every blob off disk for questions that never look at one.
    """
    connection.execute(
        """
        CREATE TEMP TABLE scene_request AS
        SELECT id, kind, sequence_number, qpc, reason, scope, parent_scope,
               scene_entity, scene_entity_index, scene_vftable, target_entity,
               target_ref_handle, target_entity_index, target_entity_serial,
               caller_address,
               event_type, event_start, event_end, scene_time, playing_back,
               faults, scene_file, actor_name, text0, text1, text2
        FROM scene_events
        """
    )
    for statement in (
        "CREATE INDEX temp.scene_request_scope ON scene_request(scope)",
        "CREATE INDEX temp.scene_request_scene ON scene_request(scene_entity, qpc)",
        "CREATE INDEX temp.scene_request_target"
        " ON scene_request(target_entity_index, qpc)",
    ):
        connection.execute(statement)


def capability(
    connection: sqlite3.Connection, headers: dict[str, Any]
) -> dict[str, Any]:
    """What this database can answer, gated per section rather than per stream.

    A database that predates a column is answered as unjudgeable rather than
    raised on, which is why every gate below asks for the column it reads.
    """
    # Views count: an indexed database reaches its event rows through one, and
    # asking only for tables would leave `records` unseen, its columns unread,
    # and every contribution-gated section below silently unjudged.
    tables = {
        name
        for (name,) in connection.execute(
            "SELECT name FROM sqlite_master WHERE type IN ('table', 'view')"
        )
    }
    # The tables exist in every database this schema creates, so presence is no
    # evidence at all: what says a run carried the stream is the stream row and
    # the rows it produced.
    scene = SCENE_STREAM in headers and "scene_events" in tables
    changes = bool(
        "sequence_changes" in tables
        and connection.execute(
            "SELECT count(*) FROM sequence_changes"
        ).fetchone()[0]
    )
    actor_columns = (
        table_columns(connection, "actor_observations")
        if "actor_observations" in tables
        else set()
    )
    record_columns = (
        table_columns(connection, "records") if "records" in tables else set()
    )
    contributions = bool(
        "owner_checksum" in record_columns
        and connection.execute(
            "SELECT count(*) FROM records WHERE owner_checksum IS NOT NULL"
        ).fetchone()[0]
    )
    images = bool(
        "model_headers" in tables
        and connection.execute(
            "SELECT count(*) FROM model_headers"
        ).fetchone()[0]
    )
    indices = False
    if scene:
        indices = bool(
            connection.execute(
                "SELECT count(*) FROM scene_events"
                " WHERE target_entity_index IS NOT NULL"
            ).fetchone()[0]
        )
    return {
        "stream_versions": {
            name: header.get("version") for name, header in headers.items()
        },
        "carries_scene": scene,
        "carries_sequence_changes": changes,
        "carries_indices": indices,
        "carries_actor_indices": "entity_index" in actor_columns,
        "carries_contributions": contributions,
        "carries_images": images,
        "targets": {
            "vampire_base": headers.get(SCENE_STREAM, {}).get("vampire_base"),
            "client_base": headers.get(SCENE_STREAM, {}).get("client_base"),
        },
    }


def coverage(connection: sqlite3.Connection) -> dict[str, Any]:
    """Every request scoped, and every scope naming a scene that started."""
    counts = dict(
        connection.execute(
            "SELECT kind, count(*) FROM scene_request GROUP BY kind"
        ).fetchall()
    )
    unscoped = connection.execute(
        "SELECT count(*) FROM scene_request WHERE scope = 0"
    ).fetchone()[0]
    # A dispatched event, a binding or an anim-set application must sit under a
    # scope its own frame or an enclosing one opened.
    started = {
        row[0]
        for row in connection.execute(
            "SELECT scene_entity FROM scene_request WHERE reason = 1"
        )
    }
    orphan_events = connection.execute(
        "SELECT count(*) FROM scene_request"
        " WHERE reason = 4 AND scene_entity NOT IN"
        " (SELECT scene_entity FROM scene_request WHERE reason = 1)"
    ).fetchone()[0]
    scenes = connection.execute(
        "SELECT count(DISTINCT scene_entity) FROM scene_request"
    ).fetchone()[0]
    files = connection.execute(
        "SELECT count(DISTINCT scene_file) FROM scene_request"
        " WHERE scene_file != ''"
    ).fetchone()[0]
    classes = dict(
        connection.execute(
            "SELECT scene_vftable, count(DISTINCT scene_entity)"
            " FROM scene_request GROUP BY scene_vftable"
        ).fetchall()
    )
    return {
        "records": sum(counts.values()),
        "by_kind": counts,
        "unscoped": unscoped,
        "scenes_started": len(started),
        "distinct_scene_entities": scenes,
        "distinct_scene_files": files,
        "scene_classes": {address(k): v for k, v in classes.items()},
        "events_without_a_started_scene": orphan_events,
        "complete": unscoped == 0 and orphan_events == 0,
    }


def binding(connection: sqlite3.Connection) -> dict[str, Any]:
    """Every actor an animation-bearing event named, resolved to an entity."""
    placeholders = ",".join(str(value) for value in ANIMATION_EVENTS)
    selecting = connection.execute(
        f"SELECT count(*) FROM scene_request WHERE event_type IN ({placeholders})"
    ).fetchone()[0]
    labels = connection.execute(
        f"SELECT count(DISTINCT text0) FROM scene_request"
        f" WHERE event_type IN ({placeholders}) AND text0 != ''"
    ).fetchone()[0]
    bound = connection.execute(
        "SELECT count(*) FROM scene_request"
        " WHERE reason = 5 AND target_entity != 0"
    ).fetchone()[0]
    unbound_rows = connection.execute(
        "SELECT actor_name, count(*) FROM scene_request"
        " WHERE reason = 5 AND target_entity = 0"
        " GROUP BY actor_name ORDER BY count(*) DESC"
    ).fetchall()
    unbound = sum(count for _, count in unbound_rows)
    # The claim is that a binding which resolved to an entity carries an index,
    # so the count is taken over resolved bindings rather than over every row.
    # An unresolved binding has no handle to decode and must not be counted as
    # though it had one.
    indexed = connection.execute(
        "SELECT count(*) FROM scene_request"
        " WHERE reason = 5 AND target_entity != 0"
        " AND target_entity_index IS NOT NULL"
        " AND target_ref_handle NOT IN (0, 4294967295)"
    ).fetchone()[0]
    return {
        "animation_events": selecting,
        "distinct_sequence_labels": labels,
        "bindings_resolved": bound,
        "bindings_unresolved": unbound,
        "bindings_with_an_index": indexed,
        "unresolved_actors": [
            {"actor": name, "records": count}
            for name, count in unbound_rows[:MAX_REPORTED_OFFENDERS]
        ],
        # A binding that resolved must carry an index; one that resolved to
        # nothing is the engine's own documented outcome and is counted apart.
        "bound": bound == indexed,
    }


def join(
    connection: sqlite3.Connection, support: dict[str, Any]
) -> dict[str, Any]:
    """Whether an index names one entity on each side, and the anim set agrees.

    Four values recorded by four different hooks have to agree, and none of them
    is derived from another: the BaseAnim string and the index come from the
    server, the same index comes from the client, and the owner checksum comes
    from the client's own sequence evaluator.
    """
    if not support["carries_actor_indices"]:
        return {"available": False, "reason": "the actor census carries no index"}

    # 1. An index names one client entity and one server entity at a time. The
    #    client side is the actor census rather than the sequence changes: the
    #    census carries every skeletal entity the run saw, while a sequence
    #    change is only recorded for one that actually transitioned, so joining
    #    through it would reach a fraction of the cast and call the rest absent.
    client_conflicts = connection.execute(
        "SELECT entity_index, count(DISTINCT entity) AS n"
        " FROM actor_observations WHERE entity_index IS NOT NULL"
        " GROUP BY entity_index HAVING n > 1"
    ).fetchall()
    server_conflicts = connection.execute(
        "SELECT target_entity_index, count(DISTINCT target_entity) AS n"
        " FROM scene_request WHERE target_entity_index IS NOT NULL"
        " GROUP BY target_entity_index HAVING n > 1"
    ).fetchall()

    # 2. CAP1.3's fixed +4, re-derived from a population it never measured.
    renderable_rows = connection.execute(
        "SELECT count(*), sum(CASE WHEN renderable - client_entity = 4"
        " THEN 1 ELSE 0 END) FROM sequence_changes WHERE renderable != 0"
    ).fetchone()
    renderable_total = int(renderable_rows[0] or 0)
    renderable_agree = int(renderable_rows[1] or 0)

    # 3. The decisive check: every actor an anim-set-carrying scene bound must
    #    have animated under the BaseAnim model that scene named.
    animset = {"available": False, "reason": "no contributions or model census"}
    if support["carries_contributions"] and support["carries_images"]:
        rows = connection.execute(
            """
            SELECT s.scene_entity, s.text1, b.target_entity_index,
                   s.qpc AS applied_qpc
            FROM scene_request AS s
            JOIN scene_request AS b
              ON b.scope = s.scope AND b.reason = 5
            WHERE s.reason = 6 AND s.text1 != ''
              AND b.target_entity_index IS NOT NULL
            """
        ).fetchall()
        compared = 0
        agreeing = 0
        without_renderable: list[dict[str, Any]] = []
        disagreeing: list[dict[str, Any]] = []
        for scene_entity, base_anim, index, applied in rows:
            # An evaluation is scoped by the pose build it sits in, and that
            # bracket is entered on the renderable subobject rather than on the
            # entity -- CAP1.3's fixed +4. The census records both addresses, so
            # the join takes the renderable it stored rather than adding four to
            # the entity and calling that evidence.
            entities = [
                row[0]
                for row in connection.execute(
                    "SELECT DISTINCT renderable FROM actor_observations"
                    " WHERE entity_index = ? AND renderable != 0",
                    (index,),
                )
            ]
            if not entities:
                # The scene applied an animation set to an index the census
                # never observed a renderable for. Skipping it silently would
                # drop it from numerator and denominator alike, so the join
                # would read as closed over a population it never saw.
                if len(without_renderable) < MAX_REPORTED_OFFENDERS:
                    without_renderable.append(
                        {
                            "scene": address(scene_entity),
                            "base_anim": base_anim,
                            "entity_index": index,
                        }
                    )
                continue
            compared += 1
            wanted = _model_key(base_anim)
            owners = connection.execute(
                """
                SELECT DISTINCT m.model_name FROM records AS r
                JOIN model_headers AS m ON m.checksum = r.owner_checksum
                WHERE r.generation_entity IN ({slots}) AND r.qpc >= ?
                """.format(slots=",".join("?" * len(entities))),
                (*entities, applied),
            ).fetchall()
            matched = any(_model_key(name) == wanted for (name,) in owners)
            if matched:
                agreeing += 1
            elif len(disagreeing) < MAX_REPORTED_OFFENDERS:
                disagreeing.append(
                    {
                        "scene": address(scene_entity),
                        "base_anim": base_anim,
                        "entity_index": index,
                    }
                )
        animset = {
            "available": True,
            "bindings": len(rows),
            "compared": compared,
            "agreeing": agreeing,
            "disagreeing": compared - agreeing,
            "without_a_renderable": len(without_renderable),
            "without_a_renderable_detail": without_renderable,
            "offenders": disagreeing,
        }

    joins = (
        not client_conflicts
        and not server_conflicts
        and (renderable_total == 0 or renderable_agree == renderable_total)
        and (
            not animset.get("available")
            or animset["disagreeing"] == 0
        )
    )
    return {
        "available": True,
        "client_index_conflicts": len(client_conflicts),
        "server_index_conflicts": len(server_conflicts),
        "renderable_pairs": renderable_total,
        "renderable_agreeing": renderable_agree,
        "anim_set_owner": animset,
        "joins": joins,
    }


def requests(
    connection: sqlite3.Connection, support: dict[str, Any]
) -> dict[str, Any]:
    """How many pose groups belong to an entity a scene had asked for.

    The remainder is counted apart by reason rather than folded away: an idle
    NPC, the player, and a scripted sequence are all real populations this run
    does not attribute to a choreographed scene.
    """
    if not support["carries_sequence_changes"]:
        return {"available": False, "reason": "no sequence-change records"}
    total = connection.execute(
        "SELECT count(*) FROM sequence_changes"
    ).fetchone()[0]
    attributed = connection.execute(
        "SELECT count(*) FROM sequence_changes AS c"
        " WHERE c.entity_index IS NOT NULL AND EXISTS ("
        "   SELECT 1 FROM scene_request AS s"
        "   WHERE s.reason = 5 AND s.target_entity_index = c.entity_index"
        "     AND s.qpc <= c.qpc)"
    ).fetchone()[0]
    ungenerated = connection.execute(
        "SELECT count(*) FROM sequence_changes WHERE generation = 0"
    ).fetchone()[0]
    entities = connection.execute(
        "SELECT count(DISTINCT entity_index) FROM sequence_changes"
        " WHERE entity_index IS NOT NULL"
    ).fetchone()[0]
    return {
        "available": True,
        "sequence_changes": total,
        "attributed_to_a_scene": attributed,
        "unattributed": total - attributed,
        "outside_a_pose_build": ungenerated,
        "distinct_entities": entities,
        "attributed": total == 0 or attributed > 0,
    }


def activity(connection: sqlite3.Connection) -> dict[str, Any]:
    """What the run actually dispatched, by event type.

    No activity enum appears here and none can: the selection data is recovered
    but the selector is not located, so a sequence label is the finest thing the
    request side names.
    """
    histogram = dict(
        connection.execute(
            "SELECT event_type, count(*) FROM scene_request"
            " WHERE reason = 4 GROUP BY event_type ORDER BY event_type"
        ).fetchall()
    )
    return {
        "dispatched_by_type": histogram,
        "gesture_events": histogram.get(GESTURE_EVENT, 0),
        "sequence_events": histogram.get(SEQUENCE_EVENT, 0),
        "activity_enum_observed": False,
        "observed": bool(histogram),
    }


def shapes(connection: sqlite3.Connection) -> dict[str, str]:
    """Digest what each scene file dispatched, as the authored thing it is.

    Two honest runs reach different sets of scenes, because which per-line
    dialogue a run gets to depends on where the operator crossed the trigger.
    What cannot differ is what a scene dispatched once it started: scene time is
    elapsed time since that scene's own start and the simulation step is pinned,
    so the ordered events and their scene-relative times are a property of the
    file rather than of the run. Digesting that is what makes a cross-run
    comparison a claim about content instead of about timing.
    """
    digests: dict[str, str] = {}
    rows = connection.execute(
        "SELECT scene_file, event_type, text0, event_start, actor_name"
        " FROM scene_request WHERE reason = 4 AND scene_file != ''"
        " ORDER BY scene_file, event_start, event_type, actor_name, text0"
    ).fetchall()
    grouped: dict[str, list[str]] = {}
    for scene_file, event_type, param, start, actor in rows:
        grouped.setdefault(scene_file, []).append(
            # Rounded to a millisecond: the authored times are three-decimal
            # literals, so finer precision would compare float noise.
            f"{event_type}|{param}|{start:.3f}|{actor}"
        )
    for scene_file, entries in grouped.items():
        digest = hashlib.sha256(
            "\n".join(entries).encode("utf-8")
        ).hexdigest()
        digests[scene_file] = f"{digest[:16]}:{len(entries)}"
    return digests


def callers(
    connection: sqlite3.Connection, support: dict[str, Any]
) -> dict[str, Any]:
    """Which frame asked, by nearest preceding seed in the vampire spec."""
    base_text = (support.get("targets") or {}).get("vampire_base")
    if not base_text:
        return {"available": False, "reason": "the stream carries no server base"}
    base = int(base_text, 16)
    functions = spec_functions(VAMPIRE_SPEC)
    rows = connection.execute(
        "SELECT kind, caller_address, count(*) FROM scene_request"
        " GROUP BY kind, caller_address ORDER BY count(*) DESC"
    ).fetchall()
    sites: list[dict[str, Any]] = []
    unresolved = 0
    for kind, caller, count in rows:
        virtual = caller - base + VAMPIRE_IMAGE_BASE
        label = nearest_preceding_label(functions, virtual)
        if label is None:
            unresolved += count
        if len(sites) < MAX_REPORTED_CALLERS:
            sites.append(
                {
                    "kind": kind,
                    "caller": address(caller),
                    "spec_address": address(virtual),
                    "nearest_preceding_label": label,
                    "records": count,
                }
            )
    return {
        "available": True,
        "distinct_callers": len(rows),
        "unresolved_records": unresolved,
        "sites": sites,
        "sites_truncated": len(rows) > MAX_REPORTED_CALLERS,
    }


def truncation(connection: sqlite3.Connection) -> dict[str, Any]:
    """The longest string each bounded field actually saw.

    A width is a measurement rather than an assumption, on the same terms the
    model census measured its 64-byte name field.
    """
    longest = connection.execute(
        "SELECT max(length(scene_file)), max(length(actor_name)),"
        " max(length(text0)), max(length(text1)), max(length(text2))"
        " FROM scene_request"
    ).fetchone()
    truncated = connection.execute(
        "SELECT count(*) FROM scene_request WHERE faults & ? != 0",
        ((1 << 2) | (1 << 3) | (1 << 4),),
    ).fetchone()[0]
    return {
        "longest_scene_file": longest[0] or 0,
        "longest_actor_name": longest[1] or 0,
        "longest_text0": longest[2] or 0,
        "longest_text1": longest[3] or 0,
        "longest_text2": longest[4] or 0,
        "field_widths": {
            "scene_file": 128,
            "actor_name": 64,
            "text0": 128,
            "text1": 128,
            "text2": 64,
        },
        "truncated_records": truncated,
        "lossless": truncated == 0,
    }


def faults(connection: sqlite3.Connection) -> dict[str, Any]:
    """Fault bits, masked to the ones this report owns."""
    rows = connection.execute(
        f"SELECT faults, count(*) FROM scene_request"
        f" WHERE faults & {FAULT_MASK} != 0 GROUP BY faults"
    ).fetchall()
    change_rows = connection.execute(
        f"SELECT faults, count(*) FROM sequence_changes"
        f" WHERE faults & {FAULT_MASK} != 0 GROUP BY faults"
    ).fetchall()
    named: dict[str, int] = {}
    unexpected = 0
    for word, count in list(rows) + list(change_rows):
        for bit, name in FAULT_NAMES.items():
            if word & bit:
                named[name] = named.get(name, 0) + count
        if word & FAULT_MASK & ~EXPECTED_FAULTS:
            unexpected += count
    return {
        "records_with_faults": sum(count for _, count in rows)
        + sum(count for _, count in change_rows),
        "by_bit": named,
        "unexpected_records": unexpected,
        "clean": unexpected == 0,
    }


def overhead(
    connection: sqlite3.Connection,
    headers: dict[str, Any],
    done: dict[str, str],
) -> dict[str, Any]:
    """What the scene stream cost, and whether its bytes close exactly."""
    stream = headers.get(SCENE_STREAM)
    if stream is None:
        return {"available": False, "reason": "no scene stream"}
    counted = connection.execute(
        f"SELECT count(*), sum(bytes) FROM ({scene_bytes_expression()})"
    ).fetchone()
    payload = int(stream["file_size"]) - int(stream["header_bytes"])
    database_bytes = sum(
        int(header["file_size"]) for header in headers.values()
    )
    span = 0.0
    frequency = int(stream.get("qpc_frequency") or 0)
    if frequency:
        bounds = connection.execute(
            "SELECT min(qpc), max(qpc) FROM ("
            "SELECT qpc FROM records UNION ALL SELECT qpc FROM scene_request)"
        ).fetchone()
        if bounds[0] is not None:
            span = (int(bounds[1]) - int(bounds[0])) / frequency
    return {
        "available": True,
        "records": int(counted[0] or 0),
        "derived_payload_bytes": int(counted[1] or 0),
        "stream_payload_bytes": payload,
        "byte_closure": int(counted[1] or 0) == payload,
        "share_of_raw_streams": (
            (int(stream["file_size"]) / database_bytes)
            if database_bytes
            else 0.0
        ),
        "mean_mb_per_second": (
            round(database_bytes / span / (1024 * 1024), 3) if span else 0.0
        ),
        "baseline_mean_mb_per_second": BASELINE_MEAN_MB_PER_SECOND,
        "queue_high_water": int(done.get("queue_peak", "0")),
        "baseline_queue_high_water": BASELINE_QUEUE_HIGH_WATER,
        "scene_bytes_reported": int(done.get("scene_bytes", "0")),
    }


def decide(
    support: dict[str, Any],
    coverage_report: dict[str, Any],
    binding_report: dict[str, Any],
    join_report: dict[str, Any],
    requests_report: dict[str, Any],
    faults_report: dict[str, Any],
    overhead_report: dict[str, Any],
) -> dict[str, Any]:
    if not support["carries_scene"]:
        return {
            "requests_complete": False,
            "requests_joined": False,
            "judgeable": False,
            "statement": (
                "This capture carries no scene stream, so scene-driven "
                "animation requests cannot be judged."
            ),
        }
    if not support["carries_indices"]:
        return {
            "requests_complete": False,
            "requests_joined": False,
            "judgeable": False,
            "statement": (
                "This capture predates the entity-index fields, so a request "
                "cannot be joined to the pose group that carried it out."
            ),
        }
    verdict = {
        "requests_complete": bool(
            coverage_report["complete"]
            and binding_report["bound"]
            and faults_report["clean"]
        ),
        "requests_joined": bool(
            join_report.get("joins") and requests_report.get("attributed")
        ),
        "judgeable": True,
        "statement": "",
    }
    if coverage_report["unscoped"]:
        verdict["statement"] = (
            f"{coverage_report['unscoped']} scene records were emitted outside "
            "every scene scope, so a request cannot be tied to the scene that "
            "made it."
        )
        return verdict
    if coverage_report["events_without_a_started_scene"]:
        verdict["statement"] = (
            f"{coverage_report['events_without_a_started_scene']} events "
            "dispatched for a scene this run never saw start."
        )
        return verdict
    if not binding_report["bound"]:
        verdict["statement"] = (
            f"{binding_report['bindings_resolved']} bindings resolved to an "
            f"entity but only {binding_report['bindings_with_an_index']} "
            "carried an index."
        )
        return verdict
    if join_report.get("client_index_conflicts") or join_report.get(
        "server_index_conflicts"
    ):
        verdict["statement"] = (
            f"{join_report['client_index_conflicts']} client and "
            f"{join_report['server_index_conflicts']} server entity indices "
            "named more than one entity, so the join is not a bijection."
        )
        return verdict
    animset = join_report.get("anim_set_owner") or {}
    if animset.get("available") and animset.get("disagreeing"):
        verdict["statement"] = (
            f"{animset['disagreeing']} of {animset['compared']} scene actors "
            "animated under no model matching the animation set their scene "
            "applied."
        )
        return verdict
    if not faults_report["clean"]:
        verdict["statement"] = (
            f"{faults_report['unexpected_records']} scene records carry a "
            "fault beyond an unresolved actor or an unavailable bone rename."
        )
        return verdict
    if not overhead_report.get("byte_closure", True):
        verdict["statement"] = (
            "The scene stream's derived payload does not equal its file size, "
            "so its byte accounting does not close."
        )
        return verdict
    verdict["statement"] = (
        f"{coverage_report['records']} scene records over "
        f"{coverage_report['scenes_started']} started scenes name "
        f"{binding_report['distinct_sequence_labels']} distinct sequence "
        f"labels and resolve {binding_report['bindings_resolved']} actor "
        f"bindings, every one carrying an entity index; "
        f"{join_report.get('renderable_agreeing', 0)} of "
        f"{join_report.get('renderable_pairs', 0)} sequence changes reproduce "
        "CAP1.3's fixed renderable offset from a population it never measured, "
        f"and {animset.get('agreeing', 0)} of {animset.get('compared', 0)} "
        "scene actors animated under the animation set their scene applied -- "
        "so a pose group is attributable to what asked for it."
    )
    return verdict


def verify(session: Path) -> dict[str, Any]:
    connection = open_database(session)
    try:
        metadata = {
            key: json.loads(value)
            for key, value in connection.execute(
                "SELECT key, value FROM capture_metadata"
            )
        }
        headers = stream_headers(connection)
        support = capability(connection, headers)
        done = artifact_key_values(connection, session, "done.txt")
        if support["carries_scene"]:
            prepare(connection)
            coverage_report = coverage(connection)
            binding_report = binding(connection)
            join_report = join(connection, support)
            requests_report = requests(connection, support)
            activity_report = activity(connection)
            shape_digests = shapes(connection)
            callers_report = callers(connection, support)
            truncation_report = truncation(connection)
            faults_report = faults(connection)
            overhead_report = overhead(connection, headers, done)
        else:
            empty = {"available": False, "reason": "no scene stream"}
            coverage_report = {
                "records": 0,
                "unscoped": 0,
                "scenes_started": 0,
                "events_without_a_started_scene": 0,
                "complete": False,
            }
            binding_report = {
                "bound": False,
                "bindings_resolved": 0,
                "bindings_with_an_index": 0,
                "distinct_sequence_labels": 0,
            }
            join_report = dict(empty)
            requests_report = dict(empty)
            activity_report = {"observed": False}
            shape_digests = {}
            callers_report = dict(empty)
            truncation_report = {"lossless": False}
            faults_report = {"clean": False, "unexpected_records": 0}
            overhead_report = dict(empty)
        verdict = decide(
            support,
            coverage_report,
            binding_report,
            join_report,
            requests_report,
            faults_report,
            overhead_report,
        )
        modules = {
            name: digest
            for name, digest in connection.execute(
                "SELECT name, sha256 FROM modules"
            )
        }
        return {
            "session": session.name,
            "session_path": str(session),
            "database": str(session / DATABASE_NAME),
            "identity": {
                "map": metadata.get("map"),
                "created_utc": metadata.get("created_utc"),
                "tool_git": metadata.get("tool_git"),
                "modules": modules,
            },
            "support": support,
            "coverage": coverage_report,
            "binding": binding_report,
            "join": join_report,
            "requests": requests_report,
            "activity": activity_report,
            "shapes": shape_digests,
            "callers": callers_report,
            "truncation": truncation_report,
            "faults": faults_report,
            "overhead": overhead_report,
            "verdict": verdict,
        }
    finally:
        connection.close()


def compare(reports: list[dict[str, Any]]) -> dict[str, Any]:
    """Compare runs on the authored shape rather than on what each one reached.

    The operator's walk lands at a different moment every run, so which per-line
    dialogue scenes a run reaches differs honestly between captures. What cannot
    differ is what a scene dispatched once it started: scene time is elapsed time
    since its own start and the simulation step is pinned, so two runs play the
    same authored events at the same scene-relative times. The set of scene files
    reached at all is reported beside that as coverage, never as disagreement.
    """
    rows = []
    shapes: dict[str, dict[str, str]] = {}
    for report in reports:
        session = report["session"]
        rows.append(
            {
                "session": session,
                "records": report["coverage"].get("records"),
                "scenes_started": report["coverage"].get("scenes_started"),
                "scene_files": report["coverage"].get("distinct_scene_files"),
                "bindings": report["binding"].get("bindings_resolved"),
                "sequence_changes": report["requests"].get("sequence_changes"),
                "judgeable": report["verdict"]["judgeable"],
                "requests_joined": report["verdict"]["requests_joined"],
            }
        )
        for scene_file, digest in (report.get("shapes") or {}).items():
            shapes.setdefault(scene_file, {})[session] = digest
    common = {
        scene: seen for scene, seen in shapes.items() if len(seen) == len(rows)
    }
    divergent = sorted(
        scene for scene, seen in common.items() if len(set(seen.values())) > 1
    )
    coverage_only = sorted(set(shapes) - set(common))
    unjudgeable = [row["session"] for row in rows if not row["judgeable"]]
    if divergent:
        return {
            "rows": rows,
            "maps": compared_maps(reports)[1],
            "same_map": compared_maps(reports)[0],
            "shapes": shapes,
            "compared_scene_files": len(common),
            "shape_claim_tested": True,
            "divergent_scene_files": divergent,
            "reached_by_some_runs_only": coverage_only,
            "statement": (
                f"{len(divergent)} scene files dispatched a different sequence "
                "of events between runs, so the request side is not "
                "reproducible: " + ", ".join(divergent[:8])
            ),
        }
    same_map, maps = compared_maps(reports)
    joined = (
        f"All {len(rows)} runs join every scene request to the pose group "
        "that carried it out"
    )
    if unjudgeable:
        statement = (
            f"{len(unjudgeable)} of {len(rows)} runs cannot be judged: "
            + ", ".join(unjudgeable)
        )
    elif not all(row["requests_joined"] for row in rows):
        statement = "At least one run does not join its requests to pose groups."
    elif not common:
        # An empty intersection makes the authored-shape claim vacuous: "the 0
        # scene files present in every run agreed" is not evidence of anything.
        # Two scenes share no .vcd at all, and two runs of one scene can also
        # diverge completely, so this is reported as untested either way.
        statement = (
            joined
            + ", but no scene file is present in every run"
            + (
                " because they captured different scenes (" + ", ".join(maps) + ")"
                if not same_map
                else ""
            )
            + f", so the authored-shape claim is untested; {len(shapes)} scene "
            "files were reached by some run only."
        )
    else:
        statement = (
            joined
            + f", and the {len(common)} scene files present in every run "
            "dispatched an identical sequence of events at identical "
            "scene-relative times."
        )
    return {
        "rows": rows,
        "maps": maps,
        "same_map": same_map,
        "shapes": shapes,
        "compared_scene_files": len(common),
        "shape_claim_tested": bool(common),
        "divergent_scene_files": divergent,
        "reached_by_some_runs_only": coverage_only,
        "statement": statement,
    }


def summarize(report: dict[str, Any]) -> str:
    verdict = report["verdict"]
    lines = [
        f"session                        {report['session']}",
        f"scene records                  {report['coverage'].get('records')}",
        f"scenes started                 {report['coverage'].get('scenes_started')}",
        f"unscoped records               {report['coverage'].get('unscoped')}",
        f"animation events               {report['binding'].get('animation_events')}",
        f"distinct sequence labels       "
        f"{report['binding'].get('distinct_sequence_labels')}",
        f"bindings resolved              "
        f"{report['binding'].get('bindings_resolved')}",
        f"bindings unresolved            "
        f"{report['binding'].get('bindings_unresolved')}",
        f"sequence changes               "
        f"{report['requests'].get('sequence_changes')}",
        f"attributed to a scene          "
        f"{report['requests'].get('attributed_to_a_scene')}",
        f"renderable +4 agreeing         "
        f"{report['join'].get('renderable_agreeing')}"
        f"/{report['join'].get('renderable_pairs')}",
        f"anim-set owner agreeing        "
        f"{(report['join'].get('anim_set_owner') or {}).get('agreeing')}"
        f"/{(report['join'].get('anim_set_owner') or {}).get('compared')}",
        f"records with faults            "
        f"{report['faults'].get('records_with_faults')}",
        f"byte closure                   {report['overhead'].get('byte_closure')}",
        f"queue high-water               "
        f"{report['overhead'].get('queue_high_water')}",
        "",
        verdict["statement"],
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sessions", nargs="+")
    parser.add_argument("--report", type=Path, default=None)
    parser.add_argument("--no-session-reports", action="store_true")
    args = parser.parse_args()

    reports = []
    for value in args.sessions:
        session = resolve_session(value)
        report = verify(session)
        reports.append(report)
        print(summarize(report))
        print()
        if not args.no_session_reports:
            (session / "scene-requests-verdict.json").write_text(
                json.dumps(report, indent=2) + "\n", encoding="utf-8"
            )
    comparison = compare(reports)
    if len(reports) > 1:
        print(comparison["statement"])
    if args.report:
        args.report.write_text(
            json.dumps(
                {"sessions": reports, "comparison": comparison}, indent=2
            )
            + "\n",
            encoding="utf-8",
        )
    return (
        0
        if all(
            report["verdict"]["requests_complete"]
            and report["verdict"]["requests_joined"]
            for report in reports
        )
        else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
