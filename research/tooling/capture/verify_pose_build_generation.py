"""Decide whether every captured record belongs to one pose-build generation.

Usage:
    uv run elysium research verify_pose_build_generation <session> [<session> ...]

A session is an absolute path or a directory name below
``$ELYSIUM_WORK_ROOT/research/retail-capture/theatre``.  Every database opens
read-only; nothing is written into an evidence file.  Each session receives a
``pose-build-generation.json`` beside its database, and a combined report
carries the cross-run comparison.

``C_BaseAnimating::InternalDrawModel`` and ``C_BaseAnimating::SetupBones`` are
bracketed on entry and exit, so a draw and the pose build feeding it are
siblings under one enclosing draw generation rather than one containing the
other.  This report answers CAP2.1: whether every record carries a generation,
whether the brackets themselves are well formed, and whether one entity's
composed pose and its contributing evaluations share one generation.

A draw record is attributed twice and independently.  Its enclosing draw
bracket is structural and needs no inference.  The pose build it should belong
to is carried in thread state and confirmed against the instance identity
CAP1.3 established.  The two are compared on every record, so an attribution
that disagrees is reported as a fault rather than resolved by preferring one of
them.

Nothing here pairs records by time.  A bracket is named by the generation the
record already carries; the timestamp window only checks that a record the
generation already claims also falls inside the bracket's span.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sqlite3
from typing import Any

from research.tooling.capture.calibrate_theatre_capture import (
    DATABASE_NAME,
    artifact_key_values,
    open_database,
    record_bytes_expression,
    resolve_session,
    stream_headers,
    table_columns,
)


POSE_KIND = "POSE"
POSE_BUILD_KIND = "PBLD"
DRAW_BUILD_KIND = "DBLD"
SHADOW_BUILD_KIND = "SHDW"
# Two engine frames submit studio draws — CModelRender::DrawModel through
# RenderModel, and CModelRender::DrawModelShadow directly — and they are
# siblings, so a draw is enclosed by one or the other and never by both.
DRAW_FRAME_KINDS = (DRAW_BUILD_KIND, SHADOW_BUILD_KIND)
BRACKET_KINDS = (POSE_BUILD_KIND, *DRAW_FRAME_KINDS)
EVALUATION_KINDS = ("BASE", "FINL")
BRACKET_PLACEHOLDERS = ", ".join("?" for _ in BRACKET_KINDS)
DRAW_FRAME_PLACEHOLDERS = ", ".join("?" for _ in DRAW_FRAME_KINDS)
# CAP1.3 measured this: the draw side stores an interface subobject four bytes
# into the C_BaseAnimating the skeletal side stores directly.
INSTANCE_DELTA = 4
# SetupBones is reached through IClientRenderable slot +0x3c, so its `this` is
# already that interface subobject and matches the draw field exactly; it
# adjusts down to the C_BaseAnimating itself (LEA ESI,[EDI-0x4] at 0x100919ea).
# The draw side is therefore compared against the bracket at delta zero, which
# is CAP1.3's +4 seen from the other end of the same relation.
SETUP_BONES_DELTA = 0
# The generation counter starts at 1 so that zero stays the unassigned
# sentinel a record can carry without ambiguity.
UNASSIGNED = 0
# The streams whose records carry a generation. Every section below reads only
# these, so only these decide whether the database can be judged.
EVENT_STREAMS = ("pose", "animation")
# CAP1.2's measured baseline for one complete run, which the overhead section
# reports against rather than re-deriving.
BASELINE_MEAN_MB_PER_SECOND = 8.1
BASELINE_PEAK_SECOND_MB = 21.4
BASELINE_QUEUE_HIGH_WATER = 82
MAX_REPORTED_FAULTS = 64
MAX_REPORTED_MODELS = 400
MINIMUM_GENERATION_VERSION = 3


def address(value: int | None) -> str | None:
    return None if value is None else f"0x{value:08x}"


def prepare(connection: sqlite3.Connection) -> None:
    """Materialize the narrow columns and the brackets into temporary tables.

    Two costs make the direct queries unusable on a real run. ``records``
    stores its raw header and payload blobs inline, so every aggregate over it
    drags the whole multi-gigabyte capture off disk; and relating a record to
    its bracket by joining ``records`` to itself makes SQLite plan a nested
    scan. Both are paid once here: one pass copies the columns these questions
    actually read, and the brackets are lifted out of that copy and indexed, so
    every relation below becomes an indexed lookup over a small table.

    The database stays read-only; temporary objects live in this connection
    alone and are discarded when it closes.
    """
    # The root-transform trailer is part of a record's size, so the byte closure
    # below needs it in the copy. A database finalized before it existed carries
    # no such column and is answered with a null one rather than a SQL error.
    root_bytes = (
        "root_transform_bytes"
        if "root_transform_bytes" in table_columns(connection, "records")
        else "NULL AS root_transform_bytes"
    )
    connection.execute(
        f"""
        CREATE TEMP TABLE event AS
        SELECT stream_name, kind, sequence_number, qpc, entry_qpc, thread_id,
               client_entity, bone_count, model_name, generation,
               generation_parent, generation_depth, generation_entity,
               carry_generation, {root_bytes}
        FROM records
        """
    )
    connection.execute("CREATE INDEX temp.event_generation ON event(generation)")
    connection.execute("CREATE INDEX temp.event_kind ON event(kind)")
    connection.execute(
        f"""
        CREATE TEMP TABLE bracket AS
        SELECT generation, kind, generation_parent AS parent,
               generation_depth AS depth, client_entity, entry_qpc,
               qpc AS exit_qpc, thread_id
        FROM event WHERE kind IN ({BRACKET_PLACEHOLDERS})
        """,
        BRACKET_KINDS,
    )
    # Not unique: a duplicated generation is one of the faults being looked
    # for, so the index must tolerate it rather than reject the database.
    connection.execute("CREATE INDEX temp.bracket_generation ON bracket(generation)")
    connection.execute("CREATE INDEX temp.bracket_parent ON bracket(parent)")
    # Which generations produced an evaluation is asked once per bracket. As a
    # subquery it is materialized without an index and rescanned every time,
    # so it is built and indexed here instead.
    connection.execute(
        """
        CREATE TEMP TABLE evaluated AS
        SELECT DISTINCT generation FROM event WHERE kind IN (?, ?)
        """,
        EVALUATION_KINDS,
    )
    connection.execute(
        "CREATE UNIQUE INDEX temp.evaluated_generation ON evaluated(generation)"
    )
    # Which draw frames actually contained a pose build. A frame that built no
    # pose leaves the carried fields naming an earlier one, so the two
    # attributions can only be required to agree where a pose build exists.
    connection.execute(
        """
        CREATE TEMP TABLE posed_frame AS
        SELECT DISTINCT parent FROM bracket WHERE kind = ? AND parent != ?
        """,
        (POSE_BUILD_KIND, UNASSIGNED),
    )
    connection.execute(
        "CREATE UNIQUE INDEX temp.posed_frame_parent ON posed_frame(parent)"
    )


def capability(headers: dict[str, dict[str, Any]]) -> dict[str, Any]:
    """Report whether this database was captured with generations at all.

    A CAP1 database is a valid capture that predates the bracket, so it is
    answered rather than rejected: every later section reports null and the
    verdict says the run cannot be judged.

    Only the streams this report reads are gated. A database may carry other
    streams on their own versions — the model census is one — and a stream no
    section below queries cannot decide whether the brackets are judgeable.
    """
    versions = {name: header.get("version") for name, header in headers.items()}
    gated = {
        name: version
        for name, version in versions.items()
        if name in EVENT_STREAMS
    }
    supported = all(
        (version or 0) >= MINIMUM_GENERATION_VERSION for version in gated.values()
    )
    return {
        "stream_versions": versions,
        "gated_streams": sorted(gated),
        "carries_generations": bool(supported and gated),
        "minimum_version": MINIMUM_GENERATION_VERSION,
    }


def coverage(connection: sqlite3.Connection) -> dict[str, Any]:
    """Count assigned and unassigned records per kind.

    This is CAP2.1's acceptance criterion; everything else explains it.
    """
    per_kind = []
    total = 0
    unassigned_total = 0
    for kind, count, unassigned in connection.execute(
        """
        SELECT kind, count(*),
               sum(CASE WHEN generation IS NULL OR generation = ? THEN 1 ELSE 0 END)
        FROM event GROUP BY kind ORDER BY kind
        """,
        (UNASSIGNED,),
    ):
        per_kind.append(
            {
                "kind": kind,
                "records": count,
                "assigned": count - unassigned,
                "unassigned": unassigned,
            }
        )
        total += count
        unassigned_total += unassigned

    models = [
        {"model_name": name, "kind": kind, "unassigned": count}
        for name, kind, count in connection.execute(
            """
            SELECT model_name, kind, count(*) FROM event
            WHERE generation IS NULL OR generation = ?
            GROUP BY model_name, kind ORDER BY count(*) DESC LIMIT ?
            """,
            (UNASSIGNED, MAX_REPORTED_MODELS),
        )
    ]
    return {
        "per_kind": per_kind,
        "records": total,
        "assigned": total - unassigned_total,
        "unassigned": unassigned_total,
        "unassigned_by_model": models,
        "complete": unassigned_total == 0 and total > 0,
    }


def brackets(connection: sqlite3.Connection) -> dict[str, Any]:
    """Describe the bracket records themselves."""
    per_kind = {
        kind: {"brackets": count, "distinct_generations": distinct, "threads": threads}
        for kind, count, distinct, threads in connection.execute(
            f"""
            SELECT kind, count(*), count(DISTINCT generation),
                   count(DISTINCT thread_id)
            FROM event WHERE kind IN ({BRACKET_PLACEHOLDERS}) GROUP BY kind
            """,
            BRACKET_KINDS,
        )
    }
    depths = [
        {"kind": kind, "depth": depth, "brackets": count}
        for kind, depth, count in connection.execute(
            f"""
            SELECT kind, generation_depth, count(*) FROM event
            WHERE kind IN ({BRACKET_PLACEHOLDERS}) GROUP BY kind, generation_depth
            ORDER BY kind, generation_depth
            """,
            BRACKET_KINDS,
        )
    ]
    duplicates = [
        {"generation": generation, "rows": count}
        for generation, count in connection.execute(
            f"""
            SELECT generation, count(*) FROM event
            WHERE kind IN ({BRACKET_PLACEHOLDERS})
            GROUP BY generation HAVING count(*) > 1 LIMIT ?
            """,
            (*BRACKET_KINDS, MAX_REPORTED_FAULTS),
        )
    ]
    # How often a pose build produced no evaluation at all is the measurement
    # that says whether a draw can inherit one, so it is reported rather than
    # filtered away at capture time.
    barren = connection.execute(
        """
        SELECT count(*) FROM bracket b
        LEFT JOIN evaluated e ON e.generation = b.generation
        WHERE b.kind = ? AND e.generation IS NULL
        """,
        (POSE_BUILD_KIND,),
    ).fetchone()[0]
    pose_builds = per_kind.get(POSE_BUILD_KIND, {}).get("brackets", 0)
    return {
        "per_kind": per_kind,
        "depths": depths,
        "duplicate_generations": duplicates,
        "pose_builds_without_evaluations": barren,
        "pose_builds": pose_builds,
    }


def integrity(connection: sqlite3.Connection) -> dict[str, Any]:
    """Check that every generation a record names is a real, well-formed bracket."""
    dangling = connection.execute(
        f"""
        SELECT count(*) FROM event r
        LEFT JOIN bracket b ON b.generation = r.generation
        WHERE r.kind NOT IN ({BRACKET_PLACEHOLDERS}) AND r.generation IS NOT NULL
          AND r.generation != ? AND b.generation IS NULL
        """,
        (*BRACKET_KINDS, UNASSIGNED),
    ).fetchone()[0]
    orphan_parents = connection.execute(
        """
        SELECT count(*) FROM bracket b
        LEFT JOIN bracket p
               ON p.generation = b.parent AND p.depth = b.depth - 1
        WHERE b.depth > 0 AND p.generation IS NULL
        """
    ).fetchone()[0]
    inverted = connection.execute(
        "SELECT count(*) FROM bracket WHERE entry_qpc > exit_qpc"
    ).fetchone()[0]
    # A consistency check, not a pairing: the generation names the bracket and
    # the window only asks whether the record it already claims fits inside it.
    outside_span = connection.execute(
        """
        SELECT count(*) FROM event r
        JOIN bracket b ON b.generation = r.generation
        WHERE r.kind IN (?, ?) AND (r.qpc < b.entry_qpc OR r.qpc > b.exit_qpc)
        """,
        EVALUATION_KINDS,
    ).fetchone()[0]
    crossed_threads = connection.execute(
        """
        SELECT count(*) FROM event r
        JOIN bracket b ON b.generation = r.generation
        WHERE r.kind IN (?, ?) AND r.thread_id != b.thread_id
        """,
        EVALUATION_KINDS,
    ).fetchone()[0]
    return {
        "records_naming_a_missing_bracket": dangling,
        "brackets_with_a_missing_parent": orphan_parents,
        "brackets_with_inverted_span": inverted,
        "evaluations_outside_their_bracket_span": outside_span,
        "evaluations_on_another_thread": crossed_threads,
        "sound": (
            dangling == 0
            and orphan_parents == 0
            and inverted == 0
            and outside_span == 0
            and crossed_threads == 0
        ),
    }


def pose_groups(connection: sqlite3.Connection) -> dict[str, Any]:
    """Answer CAP2.1's acceptance statement about one entity's pose build."""
    # The bracket owns the interface subobject while an evaluation names the
    # C_BaseAnimating four bytes below it, so the two agree at that offset
    # rather than exactly.
    groups, multi_entity, mismatched_owner = connection.execute(
        f"""
        SELECT count(*),
               sum(CASE WHEN entities > 1 THEN 1 ELSE 0 END),
               sum(CASE WHEN owner IS NOT NULL AND single IS NOT NULL
                             AND owner != single + {INSTANCE_DELTA}
                        THEN 1 ELSE 0 END)
        FROM (
            SELECT b.generation AS generation,
                   count(DISTINCT e.client_entity) AS entities,
                   b.client_entity AS owner,
                   min(e.client_entity) AS single
            FROM bracket b
            JOIN records e ON e.generation = b.generation
                          AND e.kind IN (?, ?)
            WHERE b.kind = ?
            GROUP BY b.generation
        )
        """,
        (*EVALUATION_KINDS, POSE_BUILD_KIND),
    ).fetchone()
    composed = connection.execute(
        """
        SELECT count(*) FROM event WHERE kind = 'FINL'
          AND generation IS NOT NULL AND generation != ?
        """,
        (UNASSIGNED,),
    ).fetchone()[0]
    split = connection.execute(
        """
        SELECT count(*) FROM (
            SELECT client_entity, generation FROM event
            WHERE kind = 'FINL' GROUP BY client_entity, generation
            HAVING count(*) > 1
        )
        """
    ).fetchone()[0]
    return {
        "pose_builds_with_evaluations": groups or 0,
        "pose_builds_spanning_several_entities": multi_entity or 0,
        "pose_builds_whose_evaluations_name_another_entity": mismatched_owner or 0,
        "composed_poses_assigned": composed,
        "generations_with_repeated_composed_pose": split,
        "one_generation_per_entity": (multi_entity or 0) == 0
        and (mismatched_owner or 0) == 0,
    }


def cross_check(connection: sqlite3.Connection) -> dict[str, Any]:
    """Compare the structural and carried attribution of every draw record.

    The two are derived independently — one from bracket nesting, one from
    thread state confirmed by CAP1.3's instance delta — so a disagreement is a
    detected fault. Neither is preferred over the other here.
    """
    row = connection.execute(
        f"""
        SELECT count(*),
               sum(CASE WHEN d.generation IS NOT NULL THEN 1 ELSE 0 END),
               sum(CASE WHEN b.generation IS NOT NULL THEN 1 ELSE 0 END),
               sum(CASE WHEN d.generation IS NOT NULL
                             AND b.parent = p.generation THEN 1 ELSE 0 END),
               sum(CASE WHEN d.generation IS NOT NULL
                             AND p.client_entity
                                 = p.generation_entity + {SETUP_BONES_DELTA}
                        THEN 1 ELSE 0 END),
               sum(CASE WHEN f.parent IS NOT NULL THEN 1 ELSE 0 END),
               sum(CASE WHEN f.parent IS NOT NULL
                         AND d.generation IS NOT NULL
                         AND b.generation IS NOT NULL
                         AND b.parent = p.generation
                         AND p.client_entity = p.generation_entity + {SETUP_BONES_DELTA}
                        THEN 1 ELSE 0 END)
        FROM event p
        LEFT JOIN bracket d
               ON d.generation = p.generation
              AND d.kind IN ({DRAW_FRAME_PLACEHOLDERS})
        LEFT JOIN bracket b
               ON b.generation = p.carry_generation AND b.kind = ?
        LEFT JOIN posed_frame f ON f.parent = p.generation
        WHERE p.kind = ?
        """,
        (*DRAW_FRAME_KINDS, POSE_BUILD_KIND, POSE_KIND),
    ).fetchone()
    draws, structural, carried, nested, delta, posed, agreeing = (
        value or 0 for value in row
    )
    faults = [
        {
            "model_name": name,
            "client_entity": address(entity),
            "generation": generation,
            "carry_generation": carry,
            "generation_entity": address(carry_entity),
            "structural_resolved": bool(structural_resolved),
            "carry_resolved": bool(carry_resolved),
            "carry_nested_in_structural": bool(is_nested),
            "delta_agrees": bool(delta_ok),
        }
        for (
            name,
            entity,
            generation,
            carry,
            carry_entity,
            structural_resolved,
            carry_resolved,
            is_nested,
            delta_ok,
        ) in connection.execute(
            f"""
            SELECT p.model_name, p.client_entity, p.generation, p.carry_generation,
                   p.generation_entity,
                   d.generation IS NOT NULL,
                   b.generation IS NOT NULL,
                   b.parent = p.generation,
                   p.client_entity = p.generation_entity + {SETUP_BONES_DELTA}
            FROM event p
            LEFT JOIN bracket d
                   ON d.generation = p.generation
                  AND d.kind IN ({DRAW_FRAME_PLACEHOLDERS})
            LEFT JOIN bracket b
                   ON b.generation = p.carry_generation AND b.kind = ?
            LEFT JOIN posed_frame f ON f.parent = p.generation
            WHERE p.kind = ? AND f.parent IS NOT NULL
              AND NOT (d.generation IS NOT NULL
                       AND b.generation IS NOT NULL
                       AND b.parent = p.generation
                       AND p.client_entity = p.generation_entity + {SETUP_BONES_DELTA})
            LIMIT ?
            """,
            (
                *DRAW_FRAME_KINDS,
                POSE_BUILD_KIND,
                POSE_KIND,
                MAX_REPORTED_FAULTS,
            ),
        )
    ]
    by_model = [
        {"model_name": name, "draws": count, "disagreeing": count}
        for name, count in connection.execute(
            f"""
            SELECT p.model_name, count(*)
            FROM event p
            LEFT JOIN bracket d
                   ON d.generation = p.generation
                  AND d.kind IN ({DRAW_FRAME_PLACEHOLDERS})
            LEFT JOIN bracket b
                   ON b.generation = p.carry_generation AND b.kind = ?
            LEFT JOIN posed_frame f ON f.parent = p.generation
            WHERE p.kind = ? AND f.parent IS NOT NULL
              AND NOT (d.generation IS NOT NULL
                       AND b.generation IS NOT NULL
                       AND b.parent = p.generation
                       AND p.client_entity = p.generation_entity + {SETUP_BONES_DELTA})
            GROUP BY p.model_name ORDER BY count(*) DESC LIMIT ?
            """,
            (
                *DRAW_FRAME_KINDS,
                POSE_BUILD_KIND,
                POSE_KIND,
                MAX_REPORTED_MODELS,
            ),
        )
    ]
    return {
        "draws": draws,
        "structural_resolved": structural,
        "carry_resolved": carried,
        # Both are counted only over draws that resolved a bracket: an
        # unassigned draw carries generation zero, and a pose build at
        # depth zero has parent zero, so the two would match vacuously.
        "carry_nested_in_structural": nested,
        "instance_delta_agrees": delta,
        "instance_delta": INSTANCE_DELTA,
        "setup_bones_delta": SETUP_BONES_DELTA,
        # A draw whose frame built no pose is not a disagreement: it is a draw
        # with no pose build, and saying otherwise would report retail
        # behaviour as an instrument fault.
        "draws_with_a_pose_build": posed,
        "draws_without_a_pose_build": draws - posed,
        "agreeing": agreeing,
        "disagreeing": posed - agreeing,
        "agreement_rate": round(agreeing / posed, 6) if posed else None,
        "faults": faults,
        "disagreement_by_model": by_model,
        "agrees": posed > 0 and posed == agreeing,
    }


def overhead(
    connection: sqlite3.Connection,
    headers: dict[str, dict[str, Any]],
    done: dict[str, str],
) -> dict[str, Any]:
    """Report what the brackets cost against CAP1.2's baseline."""
    record_bytes = record_bytes_expression(headers)
    frequency = next(iter(headers.values()))["qpc_frequency"]
    first, last, records, payload = connection.execute(
        f"SELECT min(qpc), max(qpc), count(*), sum({record_bytes}) FROM event"
    ).fetchone()
    span = (last - first) / frequency if first is not None and last > first else None
    bracket_records = connection.execute(
        f"SELECT count(*) FROM event WHERE kind IN ({BRACKET_PLACEHOLDERS})",
        BRACKET_KINDS,
    ).fetchone()[0]
    bracket_bytes = connection.execute(
        f"SELECT sum({record_bytes}) FROM event "
        f"WHERE kind IN ({BRACKET_PLACEHOLDERS})",
        BRACKET_KINDS,
    ).fetchone()[0] or 0
    peak = max(
        (
            payload_second
            for _, payload_second in connection.execute(
                f"""
                SELECT (qpc - ?) / ?, sum({record_bytes})
                FROM event GROUP BY 1
                """,
                (first, frequency),
            )
        ),
        default=0,
    )
    return {
        "span_seconds": round(span, 3) if span else None,
        "records": records,
        "bracket_records": bracket_records,
        "bracket_share": round(bracket_records / records, 6) if records else None,
        "bracket_megabytes": round(bracket_bytes / 1_000_000, 3),
        "mean_megabytes_per_second": (
            round(payload / span / 1_000_000, 3) if span and payload else None
        ),
        "peak_second_megabytes": round(peak / 1_000_000, 3),
        "queue_peak": int(done["queue_peak"]) if "queue_peak" in done else None,
        "generations": int(done["generations"]) if "generations" in done else None,
        "unbracketed": int(done["unbracketed"]) if "unbracketed" in done else None,
        "bracket_overflow": (
            int(done["bracket_overflow"]) if "bracket_overflow" in done else None
        ),
        "baseline": {
            "mean_megabytes_per_second": BASELINE_MEAN_MB_PER_SECOND,
            "peak_second_megabytes": BASELINE_PEAK_SECOND_MB,
            "queue_peak": BASELINE_QUEUE_HIGH_WATER,
        },
    }


def decide(
    support: dict[str, Any],
    counts: dict[str, Any],
    sound: dict[str, Any],
    groups: dict[str, Any],
    agreement: dict[str, Any],
) -> dict[str, Any]:
    verdict: dict[str, Any] = {
        "generations_complete": False,
        "attributions_agree": False,
        "join_owner": "CAP1.3-delta",
    }
    if not support["carries_generations"]:
        verdict["statement"] = (
            "This database predates the pose-build bracket "
            f"({support['stream_versions']}), so no record carries a "
            "generation and CAP2.1 cannot be judged from it."
        )
        return verdict
    if not counts["complete"]:
        verdict["statement"] = (
            f"{counts['unassigned']} of {counts['records']} records carry no "
            "generation, so the run does not meet CAP2.1's acceptance. The "
            "unassigned records are reported by kind and model rather than "
            "attributed by proximity."
        )
        return verdict
    if not sound["sound"]:
        verdict["statement"] = (
            "Every record carries a generation, but the brackets are not well "
            f"formed ({sound}). A malformed bracket is a lost join, not an "
            "explicit unknown."
        )
        return verdict
    verdict["generations_complete"] = True
    if not groups["one_generation_per_entity"]:
        verdict["statement"] = (
            f"{groups['pose_builds_spanning_several_entities']} pose builds "
            "cover more than one entity and "
            f"{groups['pose_builds_whose_evaluations_name_another_entity']} "
            "carry evaluations naming another entity, so a composed pose and "
            "its contributions do not share one generation."
        )
        return verdict
    if not agreement["agrees"]:
        verdict["statement"] = (
            f"{agreement['disagreeing']} of "
            f"{agreement['draws_with_a_pose_build']} draws whose frame built a "
            "pose attribute differently under bracket nesting and under the "
            "carried "
            f"+{SETUP_BONES_DELTA} instance delta. The two derivations are "
            "independent, so this is a detected fault in the attribution "
            "itself, not a reporting threshold."
        )
        return verdict
    verdict["attributions_agree"] = True
    verdict["join_owner"] = "CAP2.1-generation"
    verdict["statement"] = (
        f"All {counts['records']} records carry a generation, every bracket is "
        "well formed, each pose build covers exactly one entity, and all "
        f"{agreement['draws_with_a_pose_build']} draws whose frame built a "
        "pose attribute identically under bracket nesting and under the "
        f"carried +{SETUP_BONES_DELTA} instance delta. The pose-build generation scopes the join independently of "
        "pointer lifetime."
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
        failures = {
            category: count
            for category, count in connection.execute(
                "SELECT category, count FROM failures"
            )
        }
        headers = stream_headers(connection)
        support = capability(headers)
        done = artifact_key_values(connection, session, "done.txt")

        if not support["carries_generations"]:
            counts = sound = groups = agreement = cost = None
            verdict = decide(support, {}, {}, {}, {})
        else:
            prepare(connection)
            counts = coverage(connection)
            sound = integrity(connection)
            groups = pose_groups(connection)
            agreement = cross_check(connection)
            cost = overhead(connection, headers, done)
            verdict = decide(support, counts, sound, groups, agreement)

        return {
            "session": session.name,
            "session_path": str(session),
            "database": str(session / DATABASE_NAME),
            "identity": {
                "map": metadata.get("map"),
                "created_utc": metadata.get("created_utc"),
                "tool_git": metadata.get("tool_git"),
                "modules": {
                    name: sha
                    for name, sha in connection.execute(
                        "SELECT name, sha256 FROM modules"
                    )
                },
            },
            "support": support,
            "failures": failures,
            "coverage": counts,
            "brackets": brackets(connection) if counts else None,
            "integrity": sound,
            "pose_groups": groups,
            "cross_check": agreement,
            "overhead": cost,
            "verdict": verdict,
        }
    finally:
        connection.close()


def compare(reports: list[dict[str, Any]]) -> dict[str, Any]:
    rows = [
        {
            "session": report["session"],
            "records": (report["coverage"] or {}).get("records"),
            "unassigned": (report["coverage"] or {}).get("unassigned"),
            "draws": (report["cross_check"] or {}).get("draws"),
            "disagreeing": (report["cross_check"] or {}).get("disagreeing"),
            "pose_builds": (report["brackets"] or {}).get("pose_builds"),
            "generations_complete": report["verdict"]["generations_complete"],
            "attributions_agree": report["verdict"]["attributions_agree"],
        }
        for report in reports
    ]
    complete = [row for row in rows if row["generations_complete"]]
    if len(complete) != len(rows):
        statement = (
            f"{len(rows) - len(complete)} of {len(rows)} runs leave records "
            "unassigned, so the generation is not yet established across runs."
        )
    elif all(row["attributions_agree"] for row in rows):
        statement = (
            f"All {len(rows)} runs assign every record and agree on both "
            "attributions, so the pose-build generation reproduces rather "
            "than holding in a single capture."
        )
    else:
        statement = (
            "Every run assigns its records, but at least one run's two "
            "attributions disagree, so the generation is complete without "
            "being confirmed."
        )
    return {
        "sessions": [report["session"] for report in reports],
        "runs": rows,
        "statement": statement,
    }


def summarize(report: dict[str, Any]) -> str:
    lines = [f"{report['session']}"]
    counts = report["coverage"]
    if counts is None:
        lines.append(f"  support    {report['verdict']['statement']}")
        return "\n".join(lines)
    lines.append(
        f"  coverage   {counts['assigned']}/{counts['records']} records "
        f"assigned; {counts['unassigned']} unassigned"
    )
    frames = report["brackets"]
    lines.append(
        f"  brackets   {frames['pose_builds']} pose builds, "
        f"{frames['pose_builds_without_evaluations']} of them with no "
        "evaluation"
    )
    lines.append(
        "  frames     "
        + ", ".join(
            f"{kind} {frames['per_kind'].get(kind, {}).get('brackets', 0)}"
            for kind in DRAW_FRAME_KINDS
        )
    )
    sound = report["integrity"]
    lines.append(
        "  integrity  "
        + (
            "well formed"
            if sound["sound"]
            else ", ".join(
                f"{key}={value}"
                for key, value in sound.items()
                if key != "sound" and value
            )
        )
    )
    agreement = report["cross_check"]
    lines.append(
        f"  agreement  {agreement['agreeing']}/"
        f"{agreement['draws_with_a_pose_build']} posed draws agree on both "
        f"attributions ({agreement['disagreeing']} faults); "
        f"{agreement['draws_without_a_pose_build']} draws built no pose"
    )
    cost = report["overhead"]
    lines.append(
        f"  overhead   {cost['mean_megabytes_per_second']} MB/s mean "
        f"(baseline {cost['baseline']['mean_megabytes_per_second']}), "
        f"queue peak {cost['queue_peak']} "
        f"(baseline {cost['baseline']['queue_peak']}), "
        f"brackets {cost['bracket_share']} of records"
    )
    verdict = report["verdict"]
    lines.append(
        "  verdict    "
        + (
            "GENERATION ESTABLISHED"
            if verdict["attributions_agree"]
            else "INCOMPLETE"
        )
        + f" — join owner {verdict['join_owner']}"
    )
    lines.append(f"             {verdict['statement']}")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sessions", nargs="+")
    parser.add_argument("--report", type=Path)
    parser.add_argument(
        "--no-session-reports",
        action="store_true",
        help="Do not write pose-build-generation.json beside each database.",
    )
    args = parser.parse_args()

    reports = []
    for value in args.sessions:
        session = resolve_session(value)
        report = verify(session)
        reports.append(report)
        if not args.no_session_reports:
            (session / "pose-build-generation.json").write_text(
                json.dumps(report, indent=2) + "\n", encoding="utf-8"
            )
        print(summarize(report), flush=True)

    combined = {
        "sessions": [report["session"] for report in reports],
        "reports": reports,
        "comparison": compare(reports) if len(reports) > 1 else None,
    }
    if combined["comparison"]:
        print(combined["comparison"]["statement"], flush=True)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(
            json.dumps(combined, indent=2) + "\n", encoding="utf-8"
        )
        print(args.report.resolve(), flush=True)
    return 0 if all(
        report["verdict"]["attributions_agree"] for report in reports
    ) else 1


if __name__ == "__main__":
    raise SystemExit(main())
