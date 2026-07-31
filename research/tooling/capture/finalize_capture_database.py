"""Finalize recoverable retail trace streams into one queryable SQLite file."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import struct
from typing import BinaryIO, Iterator


POSE_FILE_HEADER = struct.Struct("<8sIIQqIIIII65s11s")
POSE_RECORD_HEADER = struct.Struct("<4sIQqIIIIII5I64s")
ANIMATION_FILE_HEADER = struct.Struct("<8sIIQqIIIII65s11s")
ANIMATION_RECORD_HEADER = struct.Struct("<4sIQqIIIIIiffi2I")
MAXIMUM_BONES = 1024


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(8 * 1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def read_key_values(path: Path) -> dict[str, str]:
    if not path.is_file():
        return {}
    values: dict[str, str] = {}
    for line in path.read_text(encoding="ascii", errors="replace").splitlines():
        key, separator, value = line.partition("=")
        if separator:
            values[key] = value
    return values


def _read_record(
    stream: BinaryIO,
    header: struct.Struct,
) -> tuple[bytes, bytes] | bytes | None:
    raw_header = stream.read(header.size)
    if not raw_header:
        return None
    if len(raw_header) != header.size:
        return raw_header
    record_bytes = struct.unpack_from("<I", raw_header, 4)[0]
    if record_bytes < header.size:
        return raw_header + stream.read()
    payload = stream.read(record_bytes - header.size)
    if len(payload) != record_bytes - header.size:
        return raw_header + payload
    return raw_header, payload


def _pose_records(
    stream: BinaryIO,
) -> Iterator[tuple[dict[str, object], bytes, bytes] | bytes]:
    while True:
        record = _read_record(stream, POSE_RECORD_HEADER)
        if record is None:
            return
        if isinstance(record, bytes):
            yield record
            return
        raw_header, payload = record
        fields = POSE_RECORD_HEADER.unpack(raw_header)
        bone_count = int(fields[8])
        expected_bytes = POSE_RECORD_HEADER.size + bone_count * 12 * 4 * 2
        if (
            fields[0] != b"POSE"
            or bone_count < 1
            or bone_count > MAXIMUM_BONES
            or int(fields[1]) != expected_bytes
        ):
            yield raw_header + payload + stream.read()
            return
        yield (
            {
                "kind": "POSE",
                "sequence_number": int(fields[2]),
                "qpc": int(fields[3]),
                "thread_id": int(fields[4]),
                "studio_hdr": int(fields[5]),
                "client_entity": int(fields[6]),
                "checksum": int(fields[7]),
                "bone_count": bone_count,
                "model_info": int(fields[9]),
                "draw_arguments": json.dumps([int(value) for value in fields[10:15]]),
                "model_name": fields[15].split(b"\0", 1)[0].decode(
                    "ascii", "replace"
                ),
            },
            raw_header,
            payload,
        )


def _animation_records(
    stream: BinaryIO,
    *,
    selected_bones: bool,
) -> Iterator[tuple[dict[str, object], bytes, bytes] | bytes]:
    while True:
        record = _read_record(stream, ANIMATION_RECORD_HEADER)
        if record is None:
            return
        if isinstance(record, bytes):
            yield record
            return
        raw_header, payload = record
        fields = ANIMATION_RECORD_HEADER.unpack(raw_header)
        bone_count = int(fields[8])
        selected_bytes = ((bone_count + 31) // 32) * 4 if selected_bones else 0
        expected_bytes = (
            ANIMATION_RECORD_HEADER.size + bone_count * 7 * 4 + selected_bytes
        )
        if (
            fields[0] not in {b"BASE", b"FINL"}
            or bone_count < 1
            or bone_count > MAXIMUM_BONES
            or int(fields[1]) != expected_bytes
        ):
            yield raw_header + payload + stream.read()
            return
        yield (
            {
                "kind": fields[0].decode("ascii"),
                "sequence_number": int(fields[2]),
                "qpc": int(fields[3]),
                "thread_id": int(fields[4]),
                "client_entity": int(fields[5]),
                "studio_hdr": int(fields[6]),
                "checksum": int(fields[7]),
                "bone_count": bone_count,
                "studio_sequence": int(fields[9]),
                "sample_phase": float(fields[10]),
                "entity_cycle": float(fields[11]),
                "result": int(fields[12]),
                "positions": int(fields[13]),
                "quaternions": int(fields[14]),
            },
            raw_header,
            payload,
        )


SCHEMA = """
CREATE TABLE capture_metadata (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE launch_arguments (
    position INTEGER PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE modules (
    name TEXT PRIMARY KEY,
    path TEXT NOT NULL,
    file_size INTEGER NOT NULL,
    sha256 TEXT NOT NULL,
    binary_profile TEXT
);
CREATE TABLE streams (
    name TEXT PRIMARY KEY,
    source_name TEXT NOT NULL,
    format TEXT NOT NULL,
    file_size INTEGER NOT NULL,
    sha256 TEXT NOT NULL,
    record_count INTEGER NOT NULL,
    incomplete_tail_bytes INTEGER NOT NULL,
    file_header BLOB NOT NULL,
    incomplete_tail BLOB NOT NULL
);
CREATE TABLE records (
    id INTEGER PRIMARY KEY,
    stream_name TEXT NOT NULL REFERENCES streams(name),
    ordinal INTEGER NOT NULL,
    kind TEXT NOT NULL,
    sequence_number INTEGER,
    qpc INTEGER,
    thread_id INTEGER,
    client_entity INTEGER,
    studio_hdr INTEGER,
    checksum INTEGER,
    bone_count INTEGER,
    studio_sequence INTEGER,
    sample_phase REAL,
    entity_cycle REAL,
    result INTEGER,
    model_info INTEGER,
    model_name TEXT,
    draw_arguments TEXT,
    positions INTEGER,
    quaternions INTEGER,
    raw_header BLOB NOT NULL,
    raw_payload BLOB NOT NULL,
    UNIQUE(stream_name, ordinal)
);
CREATE TABLE failures (
    category TEXT PRIMARY KEY,
    count INTEGER NOT NULL,
    detail TEXT NOT NULL
);
CREATE TABLE artifacts (
    name TEXT PRIMARY KEY,
    contents BLOB NOT NULL
);
CREATE INDEX records_entity_time ON records(client_entity, qpc);
CREATE INDEX records_model_time ON records(checksum, qpc);
CREATE INDEX records_kind_time ON records(kind, qpc);
"""


INSERT_RECORD = """
INSERT INTO records (
    stream_name, ordinal, kind, sequence_number, qpc, thread_id,
    client_entity, studio_hdr, checksum, bone_count, studio_sequence,
    sample_phase, entity_cycle, result, model_info, model_name,
    draw_arguments, positions, quaternions, raw_header, raw_payload
) VALUES (
    :stream_name, :ordinal, :kind, :sequence_number, :qpc, :thread_id,
    :client_entity, :studio_hdr, :checksum, :bone_count, :studio_sequence,
    :sample_phase, :entity_cycle, :result, :model_info, :model_name,
    :draw_arguments, :positions, :quaternions, :raw_header, :raw_payload
)
"""


def _insert_stream(
    connection: sqlite3.Connection,
    path: Path,
    name: str,
) -> tuple[int, int]:
    file_header_struct = (
        POSE_FILE_HEADER if name == "pose" else ANIMATION_FILE_HEADER
    )
    with path.open("rb") as stream:
        file_header = stream.read(file_header_struct.size)
        if len(file_header) != file_header_struct.size:
            raise ValueError(f"{path} has no complete file header")
        fields = file_header_struct.unpack(file_header)
        magic = fields[0].rstrip(b"\0")
        if name == "pose":
            if magic != b"ELPOSE2" or int(fields[1]) != 2:
                raise ValueError(f"{path} is not an ELPOSE2 stream")
            records = _pose_records(stream)
        else:
            if (magic, int(fields[1])) not in {
                (b"ELANIM1", 1),
                (b"ELANIM2", 2),
            }:
                raise ValueError(f"{path} is not a supported ELANIM stream")
            records = _animation_records(
                stream,
                selected_bones=int(fields[1]) >= 2,
            )

        connection.execute(
            """
            INSERT INTO streams (
                name, source_name, format, file_size, sha256, record_count,
                incomplete_tail_bytes, file_header, incomplete_tail
            ) VALUES (?, ?, ?, ?, ?, 0, 0, ?, ?)
            """,
            (
                name,
                path.name,
                magic.decode("ascii"),
                path.stat().st_size,
                file_sha256(path),
                file_header,
                b"",
            ),
        )

        record_count = 0
        incomplete_tail = b""
        for ordinal, record in enumerate(records):
            if isinstance(record, bytes):
                incomplete_tail = record
                break
            values, raw_header, raw_payload = record
            connection.execute(
                INSERT_RECORD,
                {
                    "stream_name": name,
                    "ordinal": ordinal,
                    "studio_sequence": None,
                    "sample_phase": None,
                    "entity_cycle": None,
                    "result": None,
                    "model_info": None,
                    "model_name": None,
                    "draw_arguments": None,
                    "positions": None,
                    "quaternions": None,
                    "raw_header": raw_header,
                    "raw_payload": raw_payload,
                    **values,
                },
            )
            record_count += 1

    connection.execute(
        """
        UPDATE streams
        SET record_count = ?, incomplete_tail_bytes = ?, incomplete_tail = ?
        WHERE name = ?
        """,
        (
            record_count,
            len(incomplete_tail),
            incomplete_tail,
            name,
        ),
    )
    return record_count, len(incomplete_tail)


def _json_value(value: object) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"))


def finalize(
    session: Path,
    database: Path | None = None,
    *,
    retain_temporary_streams: bool = False,
) -> dict[str, object]:
    session = session.resolve()
    database = (database or session / "capture.sqlite").resolve()
    if database.exists():
        raise FileExistsError(database)
    launch_path = session / "launch.json"
    if not launch_path.is_file():
        raise FileNotFoundError(launch_path)
    launch = json.loads(launch_path.read_text(encoding="utf-8"))
    stream_paths = [
        ("pose", session / "scene.elpose"),
        ("animation", session / "animation.elanim"),
    ]
    stream_paths = [(name, path) for name, path in stream_paths if path.is_file()]
    if not stream_paths:
        raise FileNotFoundError(f"no raw capture streams below {session}")

    temporary = database.with_name(database.name + ".tmp")
    if temporary.exists():
        temporary.unlink()
    database.parent.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(temporary)
    counts: dict[str, int] = {}
    tails: dict[str, int] = {}
    try:
        connection.execute("PRAGMA journal_mode=DELETE")
        connection.execute("PRAGMA synchronous=FULL")
        connection.execute("PRAGMA foreign_keys=ON")
        connection.executescript(SCHEMA)
        connection.execute("BEGIN IMMEDIATE")
        for key, value in {
            "created_utc": launch.get("created_utc"),
            "tool_git": launch.get("tool_git"),
            "map": launch.get("map"),
            "capture_duration_seconds": launch.get("capture_duration_seconds"),
            "retail_exit_code": launch.get("retail_exit_code"),
        }.items():
            connection.execute(
                "INSERT INTO capture_metadata(key, value) VALUES (?, ?)",
                (key, _json_value(value)),
            )
        for position, argument in enumerate(launch.get("launch_arguments", [])):
            connection.execute(
                "INSERT INTO launch_arguments(position, value) VALUES (?, ?)",
                (position, str(argument)),
            )
        for module in launch.get("modules", []):
            connection.execute(
                """
                INSERT INTO modules(name, path, file_size, sha256, binary_profile)
                VALUES (?, ?, ?, ?, ?)
                """,
                (
                    module["name"],
                    module["path"],
                    int(module["file_size"]),
                    module["sha256"],
                    module.get("binary_profile"),
                ),
            )
        for artifact_name in (
            "launch.json",
            "recipe.cfg",
            "supervision.txt",
            "done.txt",
            "ready.txt",
            "live_pose_hook.ini",
        ):
            artifact = session / artifact_name
            if artifact.is_file():
                connection.execute(
                    "INSERT INTO artifacts(name, contents) VALUES (?, ?)",
                    (artifact_name, artifact.read_bytes()),
                )
        for stream_name, stream_path in stream_paths:
            count, tail = _insert_stream(connection, stream_path, stream_name)
            counts[stream_name] = count
            tails[stream_name] = tail

        done = read_key_values(session / "done.txt")
        supervision = read_key_values(session / "supervision.txt")
        failures = {
            "hook_dropped": (int(done.get("dropped", "0")), "hook writer drops"),
            "incomplete_streams": (
                sum(1 for value in tails.values() if value),
                _json_value(tails),
            ),
            "probe_profile_misses": (
                int(supervision.get("binary_profile_misses", "0")),
                "hash-gated module profile misses",
            ),
            "probe_diagnostics": (
                int(supervision.get("probe_diagnostic_writes", "0")),
                "hash or hook validation diagnostics",
            ),
        }
        for category, (count, detail) in failures.items():
            connection.execute(
                "INSERT INTO failures(category, count, detail) VALUES (?, ?, ?)",
                (category, count, detail),
            )
        connection.commit()
        integrity = connection.execute("PRAGMA integrity_check").fetchone()[0]
        stored_records = connection.execute("SELECT count(*) FROM records").fetchone()[0]
        if integrity != "ok" or stored_records != sum(counts.values()):
            raise RuntimeError(
                f"SQLite verification failed: integrity={integrity!r} "
                f"records={stored_records}/{sum(counts.values())}"
            )
    except BaseException:
        connection.close()
        if temporary.exists():
            temporary.unlink()
        raise
    connection.close()
    os.replace(temporary, database)

    if not retain_temporary_streams:
        for _, stream_path in stream_paths:
            stream_path.unlink()

    return {
        "database": str(database),
        "database_bytes": database.stat().st_size,
        "database_sha256": file_sha256(database),
        "records": counts,
        "incomplete_tail_bytes": tails,
        "temporary_streams_retained": retain_temporary_streams,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("session", type=Path)
    parser.add_argument("--database", type=Path)
    parser.add_argument("--retain-temporary-streams", action="store_true")
    args = parser.parse_args()
    report = finalize(
        args.session,
        args.database,
        retain_temporary_streams=args.retain_temporary_streams,
    )
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
