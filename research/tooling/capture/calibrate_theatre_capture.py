"""Measure finalized ``sp_theatre`` capture databases and compare runs.

Usage:
    uv run elysium research calibrate_theatre_capture <session> [<session> ...]

A session is an absolute path or a directory name below
``$ELYSIUM_WORK_ROOT/research/retail-capture/theatre``.  Every database opens
read-only; nothing is written into an evidence file.  Each session receives a
``calibration.json`` beside its database, and a combined report carries the
cross-run comparison.

The report answers CAP1.2: record and byte rates per stream, queue and disk
high-water marks, dropped/skipped/filtered/truncated counts, the distinct
runtime studio headers with model identity and bone counts, distinct client
entities, per-actor record rates, capture span against wall clock, and the
trigger instant derived from the stream rather than from the console log.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sqlite3
import struct
from typing import Any

from elysium_pipeline.paths import research_root
from research.tooling.capture.finalize_capture_database import (
    ACTOR_FILE_HEADER,
    ACTOR_OBSERVATION_HEADER,
    ACTOR_OBSERVATION_HEADER_V2,
    ANIMATION_FILE_HEADER,
    ANIMATION_RECORD_HEADER,
    ANIMATION_RECORD_HEADER_V2,
    ANIMATION_RECORD_HEADER_V3,
    BRACKET_RECORD_HEADER,
    CENSUS_FILE_HEADER,
    CONTRIBUTION_FILE_HEADER,
    CONTRIBUTION_RECORD_HEADER,
    CONTRIBUTION_RECORD_HEADER_V1,
    MODEL_IMAGE_HEADER,
    MODEL_OBSERVATION_HEADER,
    POSE_FILE_HEADER,
    POSE_RECORD_HEADER,
    POSE_RECORD_HEADER_V2,
    POSE_RECORD_HEADER_V3,
    SCENE_FILE_HEADER,
    SCENE_REQUEST_HEADER,
    SEQUENCE_CHANGE_HEADER,
    read_key_values,
)


DATABASE_NAME = "capture.sqlite"
# The recipe pins host_framerate to 1/30, so one simulation step is the
# tightest window in which two events can still be called simultaneous.
FRAME_SECONDS = 1.0 / 30.0
CHARACTER_PREFIX = "character/"
PLAYER_PREFIX = "character/pc/"
# chooseSire() and castUnderstudy() both run from the arrival trigger, so the
# batch that starts the cutscene always carries more than one player model.
TRIGGER_BATCH_PLAYER_MODELS = 2
# The console line is written when the trigger's script output fires and the
# cast is drawn a few frames later, so a valid stamp trails the derived zero by
# a fraction of a second. A stamp poisoned by a stale log is tens of seconds
# out, so the window is wide enough to never reject a good stamp.
CONSOLE_STAMP_TOLERANCE_SECONDS = 1.0
MAX_REPORTED_SEQUENCE_GAPS = 64
MAX_REPORTED_STUDIO_SEQUENCES = 200


def record_bytes_expression(
    headers: dict[str, dict[str, Any]],
    columns: set[str] | None = None,
) -> str:
    """Build the closed form for record size from each stream's version.

    Size stays a function of kind and bone count, so byte accounting needs no
    blob reads and still checks itself against the stream size the finalizer
    stored. Reading the header width from the stream rather than pinning it is
    what keeps a database captured before the generation existed measurable.

    Every kind is named. A kind with no arm evaluates to NULL, ``sum`` skips
    it, and byte closure fails loudly; falling through to another kind's
    formula would produce a plausible wrong number instead.
    """
    pose_version = headers.get("pose", {}).get("version", 3)
    if pose_version >= 4:
        pose = POSE_RECORD_HEADER
    elif pose_version >= 3:
        pose = POSE_RECORD_HEADER_V3
    else:
        pose = POSE_RECORD_HEADER_V2
    animation_version = headers.get("animation", {}).get("version", 3)
    if animation_version >= 4:
        evaluation = ANIMATION_RECORD_HEADER
    elif animation_version >= 3:
        evaluation = ANIMATION_RECORD_HEADER_V3
    else:
        evaluation = ANIMATION_RECORD_HEADER_V2
    # The trailer is stored per record rather than implied by kind, so the
    # closed form reads it back instead of asserting which stage receives one.
    root = (
        "COALESCE(root_transform_bytes, 0)" if animation_version >= 4 else "0"
    )
    contribution_version = headers.get("contribution", {}).get("version", 1)
    contribution = (
        CONTRIBUTION_RECORD_HEADER
        if contribution_version >= 2
        else CONTRIBUTION_RECORD_HEADER_V1
    )
    # A cell's two witnessed-bone bitmaps are a third span the record declares.
    # A database finalized before the channel decoders were hooked carries
    # neither the column nor the payload, so the term is dropped rather than
    # referencing a column SQLite would refuse to parse.
    channel = (
        "+ COALESCE(channel_bone_bytes, 0) "
        if columns is None or "channel_bone_bytes" in columns
        else ""
    )
    return (
        f"CASE WHEN kind = 'POSE' THEN {pose.size} + 96 * bone_count "
        f"WHEN kind IN ('PBLD', 'DBLD', 'SHDW') "
        f"THEN {BRACKET_RECORD_HEADER.size} "
        f"WHEN kind IN ('BASE', 'FINL') THEN {evaluation.size} "
        f"+ 28 * bone_count + 4 * ((bone_count + 31) / 32) + {root} "
        # A contribution stores both span widths, so its size reads them back
        # rather than re-deriving which kind carries which. A database finalized
        # before the stream existed carries neither column and no such row, so
        # the arm is dropped rather than referencing a column SQLite would
        # refuse to parse.
        + (
            f"WHEN kind IN ('SEQP', 'ANIM') THEN {contribution.size} "
            "+ COALESCE(pose_parameter_bytes, 0) "
            "+ COALESCE(selected_bone_bytes, 0) " + channel
            if columns is None
            or {"pose_parameter_bytes", "selected_bone_bytes"} <= columns
            else ""
        )
        + "ELSE NULL END"
    )


def census_bytes_expression() -> str:
    """The census stream's payload bytes, from its own two tables.

    Census rows are a dictionary rather than events, so they never reach
    ``records`` and their bytes cannot come from the record expression. An
    image's size is stored rather than derived, so this reads no blob either.
    """
    return (
        f"SELECT {MODEL_OBSERVATION_HEADER.size} AS bytes, 'MOBS' AS kind, "
        "qpc FROM model_headers UNION ALL "
        f"SELECT {MODEL_IMAGE_HEADER.size} + captured_bytes, 'MIMG', qpc "
        "FROM model_images"
    )


def actor_bytes_expression(version: int = 3) -> str:
    """The actor stream's bytes, from its own table.

    An actor observation is a fixed-size dictionary row with no payload, so its
    size is the header width and nothing needs deriving from bone count. The
    width itself moved when the entity handle was added, so it is taken from the
    stream version rather than assumed.
    """
    width = (
        ACTOR_OBSERVATION_HEADER.size
        if version >= 3
        else ACTOR_OBSERVATION_HEADER_V2.size
    )
    return f"SELECT {width} AS bytes, 'ACTR' AS kind, qpc FROM actor_observations"


def scene_bytes_expression() -> str:
    """The scene stream's bytes, from its own two tables.

    Both record kinds are a flat constant, so byte closure over this stream is a
    multiplication rather than a sum over shapes -- which is why
    ``record_bytes_expression`` needs no arm for it and never sees a scene row.
    """
    return (
        f"SELECT {SCENE_REQUEST_HEADER.size} AS bytes, kind, qpc "
        "FROM scene_events UNION ALL "
        f"SELECT {SEQUENCE_CHANGE_HEADER.size}, kind, qpc FROM sequence_changes"
    )


CAPTURE_ROOT = "retail-capture"


def resolve_session(value: str) -> Path:
    """Resolve a bare session name under any capture recipe's own root.

    Each recipe writes its sessions to its own subdirectory, so a bare name is
    searched across all of them rather than assumed to be a theatre run. Two
    roots holding the same name is named as ambiguous instead of resolved to
    whichever sorts first, because the wrong database reads as a clean run of
    the wrong scene.
    """
    candidate = Path(value)
    if candidate.is_absolute() or candidate.exists():
        return candidate.resolve()
    root = research_root() / CAPTURE_ROOT
    matches = sorted(path for path in root.glob(f"*/{value}") if path.is_dir())
    if len(matches) == 1:
        return matches[0].resolve()
    if not matches:
        known = sorted(path.name for path in root.glob("*") if path.is_dir())
        raise FileNotFoundError(
            f"no capture session named {value!r} under {root}; "
            f"known recipe roots: {known}"
        )
    raise ValueError(
        f"ambiguous session {value!r}: "
        + ", ".join(os.fspath(match) for match in matches)
    )


def open_database(session: Path) -> sqlite3.Connection:
    database = session / DATABASE_NAME
    if not database.is_file():
        raise FileNotFoundError(database)
    return sqlite3.connect(f"file:{database.as_posix()}?mode=ro", uri=True)


def artifact(connection: sqlite3.Connection, name: str) -> bytes | None:
    row = connection.execute(
        "SELECT contents FROM artifacts WHERE name = ?", (name,)
    ).fetchone()
    return row[0] if row else None


def artifact_key_values(
    connection: sqlite3.Connection, session: Path, name: str
) -> dict[str, str]:
    contents = artifact(connection, name)
    if contents is not None:
        return {
            key: value
            for key, _, value in (
                line.partition("=")
                for line in contents.decode("ascii", "replace").splitlines()
            )
            if key
        }
    return read_key_values(session / name)


def read_boundary(
    connection: sqlite3.Connection, session: Path
) -> tuple[dict[str, Any] | None, str]:
    contents = artifact(connection, "boundary.json")
    if contents is not None:
        return json.loads(contents.decode("utf-8")), "database-artifact"
    result = session / "result.json"
    if result.is_file():
        document = json.loads(result.read_text(encoding="utf-8"))
        boundary = document.get("boundary")
        if boundary is not None:
            return boundary, "session-result-json"
    return None, "absent"


def stream_headers(connection: sqlite3.Connection) -> dict[str, dict[str, Any]]:
    headers: dict[str, dict[str, Any]] = {}
    for name, source, fmt, size, sha, count, tail, blob in connection.execute(
        """
        SELECT name, source_name, format, file_size, sha256, record_count,
               incomplete_tail_bytes, file_header
        FROM streams
        """
    ):
        layout = (
            POSE_FILE_HEADER
            if name == "pose"
            else CENSUS_FILE_HEADER
            if name == "census"
            else ACTOR_FILE_HEADER
            if name == "actor"
            else CONTRIBUTION_FILE_HEADER
            if name == "contribution"
            else SCENE_FILE_HEADER
            if name == "scene"
            else ANIMATION_FILE_HEADER
        )
        fields = layout.unpack(blob)
        headers[name] = {
            "source_name": source,
            "format": fmt,
            "file_size": size,
            "sha256": sha,
            "record_count": count,
            "incomplete_tail_bytes": tail,
            "header_bytes": layout.size,
            "version": int(fields[1]),
            "qpc_frequency": int(fields[3]),
            "start_qpc": int(fields[4]),
            "process_id": int(fields[5]),
            # Field 6 is the module the stream's hooks live in: StudioRender.dll
            # for the draw stream, client.dll for the skeletal streams.
            **(
                {
                    "studio_render_base": f"0x{int(fields[6]):08x}",
                    "studio_object": f"0x{int(fields[7]):08x}",
                    "studio_vtable": f"0x{int(fields[8]):08x}",
                }
                if name == "pose"
                # The scene stream is the one whose hooks span two modules, so
                # field 6 is the server game DLL and the client base follows it.
                else {
                    "vampire_base": f"0x{int(fields[6]):08x}",
                    "client_base": f"0x{int(fields[7]):08x}",
                }
                if name == "scene"
                else {"client_base": f"0x{int(fields[6]):08x}"}
            ),
        }
    return headers


# The hook stamps one counter across every stream it writes, so density is only
# a proof that nothing was lost if every table it reaches is counted. Census and
# actor rows are a dictionary rather than events and live in their own tables,
# but they draw from the same counter and would otherwise read as holes.
SEQUENCE_TABLES = (
    "records",
    "model_headers",
    "model_images",
    "actor_observations",
    "scene_events",
    "sequence_changes",
)
def table_columns(connection: sqlite3.Connection, table: str) -> set[str]:
    """The column names a table actually carries.

    A database finalized before a column existed still has to be answerable, so
    every query that reads a late-added column asks first rather than failing
    with a SQL error that reads like a corrupt capture.
    """
    return {
        row[1] for row in connection.execute(f"PRAGMA table_info({table})")
    }


def sequence_union(connection: sqlite3.Connection) -> str:
    """Union every sequence-bearing table this database actually carries.

    A table missing because the run predates the stream that fills it is not a
    hole in the counter; reading it unconditionally would turn an older database
    into a query error rather than an answer.

    Views count. An indexed database reaches its event rows through one, and
    asking only for tables would drop every record from the union and report a
    dense counter as full of holes -- CAP1.2's proof that nothing was lost
    between emission and flush, turned into a false alarm by a catalogue query.
    """
    present = {
        name
        for (name,) in connection.execute(
            "SELECT name FROM sqlite_master WHERE type IN ('table', 'view')"
        )
    }
    return " UNION ALL ".join(
        f"SELECT sequence_number FROM {table}"
        for table in SEQUENCE_TABLES
        if table in present
    )


def sequence_continuity(connection: sqlite3.Connection) -> dict[str, Any]:
    union = sequence_union(connection)
    low, high, distinct, total = connection.execute(
        f"""
        SELECT min(sequence_number), max(sequence_number),
               count(DISTINCT sequence_number), count(*)
        FROM ({union})
        """
    ).fetchone()
    dense = (
        low == 1 and distinct == total and high is not None and high - low + 1 == total
    )
    report: dict[str, Any] = {
        "first": low,
        "last": high,
        "distinct": distinct,
        "records": total,
        "dense": bool(dense),
        "gaps": [],
        "duplicates": total - distinct,
    }
    if dense:
        return report
    seen = [
        value
        for (value,) in connection.execute(
            f"SELECT sequence_number FROM ({union}) ORDER BY sequence_number"
        )
    ]
    gaps: list[dict[str, int]] = []
    expected = low
    for value in seen:
        if value > expected:
            gaps.append({"after": expected - 1, "missing": value - expected})
            if len(gaps) >= MAX_REPORTED_SEQUENCE_GAPS:
                break
        expected = value + 1
    report["gaps"] = gaps
    report["gaps_truncated"] = len(gaps) >= MAX_REPORTED_SEQUENCE_GAPS
    return report


def volume(
    connection: sqlite3.Connection,
    headers: dict[str, dict[str, Any]],
    frequency: int,
    origin: int,
) -> dict[str, Any]:
    record_bytes = record_bytes_expression(headers)
    per_kind = []
    for row in connection.execute(
        f"""
        SELECT stream_name, kind, count(*), sum({record_bytes}),
               min(qpc), max(qpc), min(bone_count), max(bone_count)
        FROM records GROUP BY stream_name, kind ORDER BY stream_name, kind
        """
    ):
        stream, kind, count, payload, first, last, low_bones, high_bones = row
        span = (last - first) / frequency
        per_kind.append(
            {
                "stream": stream,
                "kind": kind,
                "records": count,
                "bytes": payload,
                "first_qpc": first,
                "last_qpc": last,
                "span_seconds": round(span, 3),
                "records_per_second": round(count / span, 1) if span else None,
                "bytes_per_second": round(payload / span, 1) if span else None,
                "bone_count_range": [low_bones, high_bones],
            }
        )

    stream_bytes: dict[str, int] = {}
    for entry in per_kind:
        stream_bytes[entry["stream"]] = (
            stream_bytes.get(entry["stream"], 0) + entry["bytes"]
        )

    # The dictionary streams are measured from their own tables and reported
    # beside the event streams rather than mixed into them, so the per-second
    # event rates CAP1.2 published stay the same numbers.
    census_kinds = []
    for stream, expression in (
        ("census", census_bytes_expression()),
        (
            "actor",
            actor_bytes_expression(
                int(headers.get("actor", {}).get("version", 3) or 3)
            ),
        ),
        ("scene", scene_bytes_expression()),
    ):
        if stream not in headers:
            continue
        for kind, count, payload, first, last in connection.execute(
            f"""
            SELECT kind, count(*), sum(bytes), min(qpc), max(qpc)
            FROM ({expression}) GROUP BY kind ORDER BY kind
            """
        ):
            census_kinds.append(
                {
                    "stream": stream,
                    "kind": kind,
                    "records": count,
                    "bytes": payload,
                    "first_qpc": first,
                    "last_qpc": last,
                }
            )
            stream_bytes[stream] = stream_bytes.get(stream, 0) + payload

    closure = {
        name: {
            "derived_payload_bytes": stream_bytes.get(name, 0),
            "file_size": header["file_size"],
            "header_bytes": header["header_bytes"],
            "closes": stream_bytes.get(name, 0) + header["header_bytes"]
            == header["file_size"],
        }
        for name, header in headers.items()
    }

    seconds: dict[int, dict[str, int]] = {}
    for second, count, payload in connection.execute(
        f"""
        SELECT (qpc - ?) / ?, count(*), sum({record_bytes})
        FROM records GROUP BY 1 ORDER BY 1
        """,
        (origin, frequency),
    ):
        seconds[int(second)] = {"records": count, "bytes": payload}
    peak_records = max(seconds.items(), key=lambda item: item[1]["records"])
    peak_bytes = max(seconds.items(), key=lambda item: item[1]["bytes"])
    return {
        "per_kind": per_kind,
        "census_per_kind": census_kinds,
        "byte_closure": closure,
        "peak_second": {
            "by_records": {"second": peak_records[0], **peak_records[1]},
            "by_bytes": {"second": peak_bytes[0], **peak_bytes[1]},
        },
        "occupied_seconds": len(seconds),
    }


def census(
    connection: sqlite3.Connection,
    record_bytes: str,
    frequency: int,
    zero: int | None,
) -> dict[str, Any]:
    def relative(qpc: int) -> float | None:
        return None if zero is None else round((qpc - zero) / frequency, 3)

    names = {
        checksum: name
        for checksum, name in connection.execute(
            "SELECT checksum, model_name FROM records WHERE kind = 'POSE' "
            "GROUP BY checksum"
        )
    }

    headers = []
    # Pose-build brackets name a generation and an entity, never a model, so
    # the runtime studio-header census is over the records that resolved one.
    for row in connection.execute(
        """
        SELECT stream_name, kind, studio_hdr, checksum, bone_count, count(*),
               min(qpc), max(qpc), count(DISTINCT client_entity)
        FROM records WHERE studio_hdr IS NOT NULL
        GROUP BY stream_name, kind, studio_hdr, checksum, bone_count
        ORDER BY min(qpc)
        """
    ):
        stream, kind, studio, checksum, bones, count, first, last, actors = row
        headers.append(
            {
                "stream": stream,
                "kind": kind,
                "studio_hdr": f"0x{studio:08x}",
                "checksum": f"0x{checksum:08x}",
                "bone_count": bones,
                "model_name": names.get(checksum),
                "records": count,
                "actors": actors,
                "first_seconds": relative(first),
                "last_seconds": relative(last),
            }
        )

    models = []
    for row in connection.execute(
        """
        SELECT model_name, checksum, max(bone_count), count(*),
               count(DISTINCT client_entity), min(qpc), max(qpc)
        FROM records WHERE kind = 'POSE'
        GROUP BY model_name, checksum ORDER BY min(qpc)
        """
    ):
        name, checksum, bones, count, actors, first, last = row
        models.append(
            {
                "model_name": name,
                "checksum": f"0x{checksum:08x}",
                "bone_count": bones,
                "records": count,
                "actors": actors,
                "first_seconds": relative(first),
                "last_seconds": relative(last),
            }
        )

    actors = []
    for row in connection.execute(
        f"""
        SELECT stream_name, client_entity, count(*), sum({record_bytes}),
               min(qpc), max(qpc), count(DISTINCT checksum)
        FROM records WHERE client_entity IS NOT NULL
        GROUP BY stream_name, client_entity ORDER BY min(qpc)
        """
    ):
        stream, entity, count, payload, first, last, checksums = row
        span = max((last - first) / frequency, FRAME_SECONDS)
        actors.append(
            {
                "stream": stream,
                "client_entity": f"0x{entity:08x}",
                "records": count,
                "bytes": payload,
                "live_seconds": round((last - first) / frequency, 3),
                "records_per_second": round(count / span, 1),
                "distinct_checksums": checksums,
                "first_seconds": relative(first),
            }
        )

    entities = {
        stream: {
            value
            for (value,) in connection.execute(
                "SELECT DISTINCT client_entity FROM records WHERE stream_name = ?",
                (stream,),
            )
        }
        for (stream,) in connection.execute("SELECT name FROM streams")
    }
    pose_entities = entities.get("pose", set())
    animation_entities = entities.get("animation", set())

    sequences = [
        {"studio_sequence": sequence, "records": count, "models": distinct}
        for sequence, count, distinct in connection.execute(
            """
            SELECT studio_sequence, count(*), count(DISTINCT checksum)
            FROM records WHERE kind IN ('BASE', 'FINL')
            GROUP BY studio_sequence ORDER BY count(*) DESC
            """
        )
    ]

    return {
        "studio_headers": headers,
        "distinct_studio_headers": len({entry["studio_hdr"] for entry in headers}),
        "models": models,
        "distinct_models": len(models),
        "actors": actors,
        "client_entities": {
            "pose": len(pose_entities),
            "animation": len(animation_entities),
            "union": len(pose_entities | animation_entities),
            "intersection": len(pose_entities & animation_entities),
        },
        "studio_sequences": sequences[:MAX_REPORTED_STUDIO_SEQUENCES],
        "distinct_studio_sequences": len(sequences),
        "studio_sequences_truncated": len(sequences) > MAX_REPORTED_STUDIO_SEQUENCES,
    }


def character_batches(
    connection: sqlite3.Connection, frequency: int
) -> list[dict[str, Any]]:
    """Group first appearances of character models into simulation frames."""
    appearances = connection.execute(
        """
        SELECT model_name, min(qpc) FROM records
        WHERE kind = 'POSE' AND model_name LIKE ?
        GROUP BY model_name ORDER BY 2
        """,
        (CHARACTER_PREFIX + "%",),
    ).fetchall()
    window = frequency * FRAME_SECONDS
    batches: list[dict[str, Any]] = []
    for name, qpc in appearances:
        if batches and qpc - batches[-1]["qpc"] <= window:
            batches[-1]["models"].append(name)
        else:
            batches.append({"qpc": qpc, "models": [name]})
    for batch in batches:
        batch["player_models"] = sum(
            1 for name in batch["models"] if name.startswith(PLAYER_PREFIX)
        )
    return batches


def _zero_player_cast_batch(
    batches: list[dict[str, Any]],
) -> tuple[dict[str, Any] | None, str | None]:
    """chooseSire() and castUnderstudy() both run from the arrival trigger."""
    trigger = next(
        (
            batch
            for batch in batches[1:]
            if batch["player_models"] >= TRIGGER_BATCH_PLAYER_MODELS
        ),
        None,
    )
    if trigger is None:
        return None, "no batch after the map load cast two player models"
    return trigger, None


def _zero_map_load_batch(
    batches: list[dict[str, Any]],
) -> tuple[dict[str, Any] | None, str | None]:
    """A scene entered inside itself starts where its cast is first drawn."""
    return batches[0], None


RUN_ZERO_RULES = {
    "player_cast_batch": (
        _zero_player_cast_batch,
        "earliest frame after the first character frame in which two or "
        "more previously unseen character/pc models are first drawn",
    ),
    "map_load_batch": (
        _zero_map_load_batch,
        "the first frame in which a character model is drawn, which for a "
        "scene entered from a save inside it is the load itself",
    ),
}


def resolve_run_zero_rule(metadata: dict[str, Any]) -> tuple[str, str]:
    """Name this database's run-zero rule and where the name came from.

    A capture written before recipes existed carries no rule, so the map name
    supplies it; that keeps every theatre database deriving exactly the zero it
    always did rather than silently changing what its timestamps are relative
    to.
    """
    declared = metadata.get("run_zero_rule")
    if isinstance(declared, str) and declared in RUN_ZERO_RULES:
        return declared, "capture-metadata"
    if metadata.get("map") == "sp_theatre":
        return "player_cast_batch", "map-default"
    return "map_load_batch", "map-default"


def first_multi_blend_contribution(
    connection: sqlite3.Connection, zero: int, frequency: int
) -> dict[str, Any] | None:
    """When this run first evaluated a grid with more than one cell.

    A capture finalized before the contribution stream existed has no such
    column, so the milestone is absent rather than zero.
    """
    if "num_blends" not in table_columns(connection, "records"):
        return None
    row = connection.execute(
        "SELECT qpc, num_blends, group_size, blend_cell, blend_weight "
        "FROM records WHERE kind = 'SEQP' AND num_blends > 1 "
        "ORDER BY qpc LIMIT 1"
    ).fetchone()
    if row is None:
        return None
    qpc, blends, sizes, cell, weight = row
    grid = json.loads(sizes) if sizes else [0, 0]
    return {
        "seconds": round((qpc - zero) / frequency, 3),
        "num_blends": blends,
        "grid": f"{grid[0]}x{grid[1]}",
        "cell": json.loads(cell) if cell else None,
        "weights": json.loads(weight) if weight else None,
    }


def run_zero(
    connection: sqlite3.Connection,
    frequency: int,
    boundary: dict[str, Any] | None,
    rule_name: str = "player_cast_batch",
    rule_source: str = "map-default",
) -> dict[str, Any]:
    select, description = RUN_ZERO_RULES[rule_name]
    batches = character_batches(connection, frequency)
    if not batches:
        return {
            "derived": None,
            "rule_name": rule_name,
            "rule_source": rule_source,
            "reason": "no character model was ever drawn",
        }
    trigger, reason = select(batches)
    if trigger is None:
        return {
            "derived": None,
            "rule_name": rule_name,
            "rule_source": rule_source,
            "reason": reason,
        }
    zero = trigger["qpc"]
    largest = max(
        (batch for batch in batches if batch["qpc"] > zero),
        key=lambda batch: len(batch["models"]),
        default=None,
    )

    def milestone(batch: dict[str, Any] | None) -> dict[str, Any] | None:
        if batch is None:
            return None
        return {
            "seconds": round((batch["qpc"] - zero) / frequency, 3),
            "models": sorted(batch["models"]),
            "player_models": batch["player_models"],
        }

    stamp = (boundary or {}).get("arm_qpc")
    delta = None if stamp is None else round((stamp - zero) / frequency, 3)
    # A recipe whose scene starts where it loads declares no authored arm
    # marker, so its stamp is the load echo rather than a trigger output. The
    # tolerance test is meaningless there and is reported as a measurement.
    authored_arm = (boundary or {}).get("arm_marker") is not None
    accepted: bool | None = (
        delta is not None and abs(delta) <= CONSOLE_STAMP_TOLERANCE_SECONDS
        if authored_arm
        else None
    )
    return {
        "derived_qpc": zero,
        "rule_name": rule_name,
        "rule_source": rule_source,
        "rule": description,
        "frame_seconds": FRAME_SECONDS,
        "map_load_batch": milestone(batches[0]),
        "trigger_batch": milestone(trigger),
        "largest_batch_after_trigger": milestone(largest),
        # The point of a capture that reaches the four-cell blend path.
        "first_multi_blend_contribution": first_multi_blend_contribution(
            connection, zero, frequency
        ),
        "beats": [
            {
                "marker": beat.get("marker"),
                "seconds": (
                    None
                    if beat.get("qpc") is None
                    else round((int(beat["qpc"]) - zero) / frequency, 3)
                ),
            }
            for beat in (boundary or {}).get("beats", [])
        ],
        "batches": [
            {
                "seconds": round((batch["qpc"] - zero) / frequency, 3),
                "models": len(batch["models"]),
                "player_models": batch["player_models"],
            }
            for batch in batches
        ],
        "console_stamp": {
            "arm_qpc": stamp,
            "arm_marker": (boundary or {}).get("arm_marker"),
            "delta_seconds": delta,
            "accepted": accepted,
            "reason": (
                None
                if authored_arm
                else "this recipe declares no authored arm marker, so the "
                "stamp is the load echo and its delta is a measurement"
            ),
        },
        # None means the run ended on its duration backstop rather than on the
        # observed map transition, so its tail is short of the cutscene.
        "stop_signal": (boundary or {}).get("signal"),
    }


def calibrate(session: Path) -> dict[str, Any]:
    connection = open_database(session)
    try:
        metadata = {
            key: json.loads(value)
            for key, value in connection.execute(
                "SELECT key, value FROM capture_metadata"
            )
        }
        modules = [
            {
                "name": name,
                "path": path,
                "file_size": size,
                "sha256": sha,
                "binary_profile": profile,
            }
            for name, path, size, sha, profile in connection.execute(
                "SELECT name, path, file_size, sha256, binary_profile FROM modules"
            )
        ]
        failures = {
            category: {"count": count, "detail": detail}
            for category, count, detail in connection.execute(
                "SELECT category, count, detail FROM failures"
            )
        }
        headers = stream_headers(connection)
        boundary, boundary_source = read_boundary(connection, session)
        done = artifact_key_values(connection, session, "done.txt")
        supervision = artifact_key_values(connection, session, "supervision.txt")

        frequency = next(iter(headers.values()))["qpc_frequency"]
        origin = connection.execute("SELECT min(qpc) FROM records").fetchone()[0]
        record_bytes = record_bytes_expression(headers)
        rule_name, rule_source = resolve_run_zero_rule(metadata)
        zero = run_zero(connection, frequency, boundary, rule_name, rule_source)
        counts = volume(connection, headers, frequency, origin)
        identity = census(
            connection, record_bytes, frequency, zero.get("derived_qpc")
        )

        database = session / DATABASE_NAME
        raw_bytes = sum(header["file_size"] for header in headers.values())
        queue_peak = done.get("queue_peak")
        limitations = [
            "The cutscene window is inferred from the drawn model population; "
            "CAP2.6 records scene events directly.",
            "client_entity is a raw pointer value, and the draw and skeletal "
            "streams express it differently: the draw stream's value is the "
            "skeletal instance pointer plus 4. Counts here are per stream; "
            "verify_entity_pointer_join reports the join.",
        ]
        if queue_peak is None:
            limitations.append(
                "This run predates the hook's queue depth counter, so the "
                "queue high-water mark is unmeasured rather than zero."
            )
        if rule_source == "map-default":
            limitations.append(
                "This database declares no run-zero rule, so the rule was "
                f"assumed from its map name: {rule_name}. Every relative "
                "timestamp below is measured against that assumption."
            )
        if (boundary or {}).get("signal") is None and not (boundary or {}).get(
            "probe"
        ):
            limitations.append(
                "This run stopped on its duration backstop rather than on the "
                "observed map transition, so the cutscene tail is missing and "
                "its span is not comparable to a run that transitioned."
            )
        if boundary_source != "database-artifact":
            limitations.append(
                f"The run zero was read from {boundary_source}; this database "
                "does not archive boundary.json."
            )

        return {
            "session": session.name,
            "session_path": str(session),
            "database": {
                "path": str(database),
                "bytes": database.stat().st_size,
                "raw_stream_bytes": raw_bytes,
                "inflation": round(database.stat().st_size / raw_bytes, 3),
            },
            "identity": {
                "map": metadata.get("map"),
                "created_utc": metadata.get("created_utc"),
                "tool_git": metadata.get("tool_git"),
                "capture_duration_seconds": metadata.get("capture_duration_seconds"),
                "retail_exit_code": metadata.get("retail_exit_code"),
                "modules": modules,
                "streams": headers,
            },
            "integrity": {
                "supervision_state": supervision.get("state"),
                "supervision_reason": supervision.get("reason"),
                "binary_profile_matches": supervision.get("binary_profile_matches"),
                "binary_profile_count": supervision.get("binary_profile_count"),
                # Misses count non-target module loads, not capture failures.
                "binary_profile_misses": supervision.get("binary_profile_misses"),
                "hook": done,
                "failures": failures,
                "sequence_numbers": sequence_continuity(connection),
                "threads": [
                    {"thread_id": thread, "records": count}
                    for thread, count in connection.execute(
                        "SELECT thread_id, count(*) FROM records GROUP BY thread_id"
                    )
                ],
            },
            "volume": {
                **counts,
                "queue_peak": None if queue_peak is None else int(queue_peak),
                "bytes_written": (
                    None
                    if done.get("bytes_written") is None
                    else int(done["bytes_written"])
                ),
                "disk_high_water_bytes": raw_bytes,
            },
            "census": identity,
            "zero": {**zero, "boundary_source": boundary_source},
            "limitations": limitations,
        }
    finally:
        connection.close()


def compared_maps(reports: list[dict[str, Any]]) -> tuple[bool, list[str]]:
    """Whether every run captured the same scene, and which scenes they are.

    A claim about a run's cast, its scene files or its record count is a claim
    about what the scene authored. Two runs of different scenes disagree on all
    of it by construction, so a comparator states that the claim does not apply
    rather than reporting a difference it was never entitled to measure.
    """
    names = sorted(
        {
            (report.get("identity") or {}).get("map")
            for report in reports
        },
        key=lambda name: (name is None, name),
    )
    present = [name for name in names if name is not None]
    return len(names) == 1, present


def compare(reports: list[dict[str, Any]]) -> dict[str, Any]:
    base = reports[0]
    base_models = {
        entry["model_name"]: entry for entry in base["census"]["models"]
    }
    same_map, maps = compared_maps(reports)
    same_rule = len({report["zero"].get("rule_name") for report in reports}) == 1

    milestones = []
    # A milestone is a time relative to each run's own zero, so comparing two
    # runs that derived their zero by different rules compares two origins.
    if same_rule:
        for key in ("map_load_batch", "largest_batch_after_trigger"):
            row: dict[str, Any] = {"milestone": key}
            for report in reports:
                entry = report["zero"].get(key)
                row[report["session"]] = (
                    None if entry is None else entry["seconds"]
                )
            milestones.append(row)

    # The table is keyed off the first run's cast, so across scenes it would
    # report the whole of that cast as missing from every other run.
    models = []
    if same_map:
        for name, entry in base_models.items():
            row = {"model_name": name, "bone_count": entry["bone_count"]}
            for report in reports:
                other = next(
                    (
                        candidate
                        for candidate in report["census"]["models"]
                        if candidate["model_name"] == name
                    ),
                    None,
                )
                row[report["session"]] = (
                    None
                    if other is None
                    else {
                        "records": other["records"],
                        "first_seconds": other["first_seconds"],
                    }
                )
            models.append(row)

    totals = [
        {
            "session": report["session"],
            "records": sum(
                entry["records"] for entry in report["volume"]["per_kind"]
            ),
            "bytes": sum(entry["bytes"] for entry in report["volume"]["per_kind"]),
        }
        for report in reports
    ]
    # A record-count delta measures reproducibility, which only two runs of the
    # same scene have. Across scenes it is a difference in what was played.
    if same_map:
        reference = totals[0]["records"]
        for entry in totals:
            entry["delta_percent"] = round(
                100.0 * (entry["records"] - reference) / reference, 4
            )

    return {
        "sessions": [report["session"] for report in reports],
        "maps": maps,
        "same_map": same_map,
        "same_run_zero_rule": same_rule,
        "totals": totals,
        "milestones_on_derived_zero": milestones,
        "models": models,
        "models_only_in_some_runs": [
            row["model_name"]
            for row in models
            if any(row[report["session"]] is None for report in reports)
        ],
        "statement": (
            "These runs captured different scenes ("
            + ", ".join(maps)
            + "), so cast, record count and milestone comparisons are "
            "coverage rather than agreement and are not reported."
            if not same_map
            else "These runs captured the same scene, so cast and record "
            "counts are compared as reproducibility."
        ),
    }


def summarize(report: dict[str, Any]) -> str:
    volumes = report["volume"]
    lines = [f"{report['session']}"]
    for entry in volumes["per_kind"]:
        rate = entry["records_per_second"] or 0.0
        throughput = (entry["bytes_per_second"] or 0.0) / 1e6
        lines.append(
            f"  {entry['stream']:<9} {entry['kind']:<4} "
            f"{entry['records']:>8} records  "
            f"{entry['bytes'] / 1e9:>6.3f} GB  "
            f"{rate:>8.1f} rec/s  {throughput:>7.2f} MB/s"
        )
    closes = all(entry["closes"] for entry in volumes["byte_closure"].values())
    sequences = report["integrity"]["sequence_numbers"]
    zero = report["zero"]
    lines.append(
        f"  bytes close={closes}  sequence dense={sequences['dense']}  "
        f"peak second={volumes['peak_second']['by_bytes']['bytes'] / 1e6:.1f} MB  "
        f"queue peak={volumes['queue_peak']}"
    )
    lines.append(
        f"  models={report['census']['distinct_models']}  "
        f"studio headers={report['census']['distinct_studio_headers']}  "
        f"entities pose/anim="
        f"{report['census']['client_entities']['pose']}/"
        f"{report['census']['client_entities']['animation']}"
    )
    if zero.get("derived_qpc") is not None:
        stamp = zero["console_stamp"]
        # A scene entered from a save inside it can draw its whole cast in one
        # batch, so there is not always a later one to name.
        later = zero.get("largest_batch_after_trigger")
        lines.append(
            f"  zero by {zero['rule_name']} ({zero['rule_source']}) from "
            f"{len(zero['trigger_batch']['models'])} cast models; "
            f"console stamp delta={stamp['delta_seconds']}s "
            f"accepted={stamp['accepted']}; "
            + (
                f"largest later batch at {later['seconds']}s; "
                if later is not None
                else "no later batch; "
            )
            + f"stop={zero['stop_signal'] or 'duration-backstop'}"
        )
        blend = zero.get("first_multi_blend_contribution")
        lines.append(
            "  first multi-blend contribution: "
            + (
                f"{blend['grid']} at {blend['seconds']}s cell={blend['cell']} "
                f"weights={blend['weights']}"
                if blend
                else "none -- this run exercised no blend grid"
            )
        )
    else:
        lines.append(
            f"  zero not derived by {zero.get('rule_name')}: "
            f"{zero.get('reason')}"
        )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sessions", nargs="+")
    parser.add_argument("--report", type=Path)
    parser.add_argument(
        "--no-session-reports",
        action="store_true",
        help="Do not write calibration.json beside each database.",
    )
    args = parser.parse_args()

    reports = []
    for value in args.sessions:
        session = resolve_session(value)
        report = calibrate(session)
        reports.append(report)
        if not args.no_session_reports:
            (session / "calibration.json").write_text(
                json.dumps(report, indent=2) + "\n", encoding="utf-8"
            )
        print(summarize(report), flush=True)

    combined = {
        "sessions": [report["session"] for report in reports],
        "reports": reports,
        "comparison": compare(reports) if len(reports) > 1 else None,
    }
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(
            json.dumps(combined, indent=2) + "\n", encoding="utf-8"
        )
        print(args.report.resolve(), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
