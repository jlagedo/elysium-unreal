"""Rewrite one finalized capture's payloads into a content-addressed store.

Usage:
    uv run elysium research compact_capture_payloads <session> [<session> ...]

A cutscene draws the same actor several times a frame and decodes the same
channel bitmaps over and over, so 42% of one theatre database's payload bytes
are exact duplicates of bytes it already holds. This pass stores each distinct
payload once, keyed by its content hash, and leaves every event row where it
was. Nothing is compressed: measured against the same corpus, compression buys a
further 5% of the file and costs a decode on every read of the hottest path in
CAP4, which is a bad trade.

`records` becomes a view over `record_events` and `payloads`, column for column
and in the original order, so a reader that selects `raw_payload` still gets the
exact bytes the probe wrote. The view reaches the store through a correlated
scalar subquery rather than a join, because SQLite offers the omit-noop-join
optimization only on the non-aggregate path and every verifier here aggregates:
as a LEFT JOIN, `count(*)` alone measured three orders of magnitude slower.

The rewrite is transactional. It builds beside the original, proves every stored
payload against the bytes it replaced, and only then replaces the file.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sqlite3
from typing import Any

from research.tooling.capture.calibrate_theatre_capture import (
    DATABASE_NAME,
    resolve_session,
)
from research.tooling.capture.resolve_consumed_spans import file_sha256, tool_commit


COMPACT_VERSION = 1
EVENT_TABLE = "record_events"
PAYLOAD_TABLE = "payloads"
PAYLOAD_COLUMN = "raw_payload"
# Rows are inserted in batches rather than one statement per record: the whole
# point of the pass is that the payloads are read once, and a per-row round trip
# would cost more than the hashing does.
BATCH = 20_000

PAYLOAD_SCHEMA = """
CREATE TABLE payloads (
    id INTEGER PRIMARY KEY,
    sha256 BLOB NOT NULL UNIQUE,
    byte_length INTEGER NOT NULL,
    bytes BLOB NOT NULL
);
"""


def _quote(name: str) -> str:
    return '"' + name.replace('"', '""') + '"'


def event_table_ddl(source_sql: str) -> str:
    """Turn the `records` DDL into the base table, from the catalogue's own text.

    `PRAGMA table_info` reports name, type, notnull, default and pk and nothing
    else, so generating this from it would silently drop `UNIQUE(stream_name,
    ordinal)` and the foreign key into `streams` -- and with the unique
    constraint goes its autoindex, which is a live access path for two of the
    verifiers. The stored SQL is the only description that carries them.

    The payload column is replaced in place rather than removed and appended, so
    every other clause keeps its position and nothing but that one line moves.
    """
    lines = source_sql.splitlines()
    out: list[str] = []
    replaced = False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith(f"{PAYLOAD_COLUMN} ") or stripped.startswith(
            f"{_quote(PAYLOAD_COLUMN)} "
        ):
            indent = line[: len(line) - len(line.lstrip())]
            out.append(
                f"{indent}payload_id INTEGER NOT NULL "
                f"REFERENCES {PAYLOAD_TABLE}(id),"
            )
            replaced = True
            continue
        out.append(line)
    if not replaced:
        raise ValueError(
            f"the records table declares no {PAYLOAD_COLUMN} column; "
            "this database is already compacted or is not a capture"
        )
    ddl = "\n".join(out)
    head = f"CREATE TABLE records"
    if head not in ddl:
        raise ValueError(f"unexpected records DDL: {source_sql[:120]!r}")
    return ddl.replace(head, f"CREATE TABLE {EVENT_TABLE}", 1)


def records_view_ddl(columns: list[str]) -> str:
    """The view, column for column and in the original order.

    A reader that unpacks a row positionally has to see exactly what it saw
    before, so the projection is built from the source's own column order rather
    than from the base table's, where the payload column has moved.
    """
    projected = []
    for column in columns:
        if column == PAYLOAD_COLUMN:
            projected.append(
                f"       (SELECT p.bytes FROM {PAYLOAD_TABLE} p "
                f"WHERE p.id = re.payload_id) AS {_quote(PAYLOAD_COLUMN)}"
            )
        else:
            projected.append(f"       re.{_quote(column)}")
    body = ",\n".join(projected).lstrip()
    return f"CREATE VIEW records AS\nSELECT {body}\nFROM {EVENT_TABLE} re"


def _catalogue(connection: sqlite3.Connection) -> list[tuple[str, str, str, str]]:
    return [
        (kind, name, table, sql)
        for kind, name, table, sql in connection.execute(
            "SELECT type, name, tbl_name, sql FROM sqlite_master "
            "WHERE sql IS NOT NULL AND name NOT LIKE 'sqlite_%' "
            "ORDER BY CASE type WHEN 'table' THEN 0 ELSE 1 END, name"
        )
    ]


def compact(
    session: Path,
    *,
    rewrite: bool = False,
    stamped_utc: str | None = None,
) -> dict[str, Any]:
    database = session / DATABASE_NAME
    if not database.is_file():
        raise FileNotFoundError(f"no finalized database at {database}")
    source_digest = file_sha256(database)
    temporary = database.with_name(database.name + ".tmp")
    if temporary.exists():
        temporary.unlink()

    source = sqlite3.connect(f"file:{database.as_posix()}?mode=ro", uri=True)
    try:
        catalogue = _catalogue(source)
        names = {name for _, name, _, _ in catalogue}
        if EVENT_TABLE in names:
            # Re-compacting is not a rebuild: the payload column this pass reads
            # no longer exists, so there is nothing to hash a second time.
            # Recovering means restoring the database from the acquisition it
            # was copied from and running the pass again.
            raise ValueError(
                f"{database} already carries {EVENT_TABLE} and is compacted; "
                "restore it from its acquisition to run this pass again."
            )
        records_sql = next(
            (
                sql
                for kind, name, _, sql in catalogue
                if kind == "table" and name == "records"
            ),
            None,
        )
        if records_sql is None:
            raise ValueError(f"{database} has no records table")
        columns = [
            row[1] for row in source.execute("PRAGMA table_info(records)")
        ]
        logical_bytes = source.execute(
            f"SELECT count(*), sum(length({PAYLOAD_COLUMN})) FROM records"
        ).fetchone()
        source_rows, source_payload_bytes = int(logical_bytes[0]), int(
            logical_bytes[1] or 0
        )
    finally:
        source.close()

    mapping = temporary.with_name(temporary.name + ".map")
    if mapping.exists():
        mapping.unlink()

    # Opened as a URI so the read-only ATTACH below is honoured: SQLite only
    # parses a URI filename in ATTACH when the connection carries the flag.
    connection = sqlite3.connect(f"file:{temporary.as_posix()}", uri=True)
    counts: dict[str, Any] = {}
    try:
        # Foreign keys stay off for the copy so table order does not matter, and
        # the whole graph is checked once at the end instead.
        connection.execute("PRAGMA foreign_keys = OFF")
        connection.execute("PRAGMA journal_mode = OFF")
        connection.execute("PRAGMA synchronous = OFF")
        connection.execute(
            f"ATTACH DATABASE 'file:{database.as_posix()}?mode=ro' AS src"
        )
        connection.execute(f"ATTACH DATABASE '{mapping.as_posix()}' AS map")
        connection.execute(
            "CREATE TABLE map.record_payload ("
            "record_id INTEGER PRIMARY KEY, payload_id INTEGER NOT NULL, "
            "source_sha BLOB NOT NULL, source_length INTEGER NOT NULL)"
        )
        # The record that first produced each payload, remembered while hashing
        # so the byte comparison joins one row per distinct payload instead of
        # sorting every event to pick one.
        connection.execute(
            "CREATE TABLE map.payload_source ("
            "payload_id INTEGER PRIMARY KEY, record_id INTEGER NOT NULL)"
        )

        connection.executescript(PAYLOAD_SCHEMA)
        connection.execute(event_table_ddl(records_sql))
        for kind, name, table, sql in catalogue:
            if name in ("records", EVENT_TABLE) or table == "records":
                continue
            if name == "record_span_sets" or "REFERENCES records(" in sql:
                # A foreign key cannot reference a view, and `records` is about
                # to become one. The parent moves to the base table, which
                # carries the same ids.
                sql = sql.replace("REFERENCES records(", f"REFERENCES {EVENT_TABLE}(")
            connection.execute(sql)

        counts.update(_store_payloads(connection))
        _copy_events(connection, columns)
        for kind, name, table, sql in catalogue:
            if kind != "table" or name in ("records", EVENT_TABLE):
                continue
            connection.execute(
                f"INSERT INTO main.{_quote(name)} SELECT * FROM src.{_quote(name)}"
            )
        for kind, name, table, sql in catalogue:
            if kind == "index" and table == "records":
                connection.execute(
                    sql.replace("ON records(", f"ON {EVENT_TABLE}(").replace(
                        "ON records (", f"ON {EVENT_TABLE} ("
                    )
                )
        connection.execute(records_view_ddl(columns))

        connection.executemany(
            "INSERT OR REPLACE INTO capture_metadata (key, value) VALUES (?, ?)",
            [
                (key, json.dumps(value))
                for key, value in sorted(
                    {
                        "compact_source_database_sha256": source_digest,
                        "compact_version": COMPACT_VERSION,
                        "compact_tool_git": tool_commit(),
                        "compact_payload_logical_bytes": source_payload_bytes,
                        **(
                            {"compact_built_utc": stamped_utc}
                            if stamped_utc
                            else {}
                        ),
                    }.items()
                )
            ],
        )
        connection.commit()
        counts.update(
            _prove(connection, source_rows, source_payload_bytes)
        )
        connection.execute("DETACH DATABASE map")
        connection.execute("DETACH DATABASE src")
        connection.execute("PRAGMA foreign_keys = ON")
        problems = connection.execute("PRAGMA foreign_key_check").fetchall()
        if problems:
            raise RuntimeError(f"foreign key check failed: {problems[:4]}")
        integrity = connection.execute("PRAGMA integrity_check").fetchone()[0]
        if integrity != "ok":
            raise RuntimeError(f"SQLite verification failed: {integrity!r}")
        connection.execute("PRAGMA synchronous = FULL")
        connection.commit()
    except BaseException:
        connection.close()
        for path in (temporary, mapping):
            if path.exists():
                path.unlink()
        raise
    connection.close()
    if mapping.exists():
        mapping.unlink()
    os.replace(temporary, database)

    return {
        "session": session.name,
        "session_path": str(session),
        "database": str(database),
        "source_database_sha256": source_digest,
        "database_sha256": file_sha256(database),
        "compact_version": COMPACT_VERSION,
        "source_bytes": None,
        "database_bytes": database.stat().st_size,
        "counts": counts,
    }


def _store_payloads(connection: sqlite3.Connection) -> dict[str, Any]:
    """One pass over the payloads, assigning ids in first-use order.

    Order is not cosmetic. Without it the id a payload receives depends on
    whatever scan the planner picks, so two runs over the same input produce
    different files -- unacceptable for an artefact whose digest is recorded
    provenance. Reading in record order also leaves the store's ids climbing
    with the events that reference them, which keeps the one query that must
    fetch payloads reading mostly forward.
    """
    seen: dict[bytes, int] = {}
    payload_rows: list[tuple[int, bytes, int, bytes]] = []
    mapping_rows: list[tuple[int, int, bytes, int]] = []
    source_rows: list[tuple[int, int]] = []
    total = 0
    cursor = connection.execute(
        f"SELECT id, {PAYLOAD_COLUMN} FROM src.records ORDER BY id"
    )
    while batch := cursor.fetchmany(BATCH):
        for record_id, payload in batch:
            blob = bytes(payload)
            digest = hashlib.sha256(blob).digest()
            payload_id = seen.get(digest)
            if payload_id is None:
                payload_id = len(seen) + 1
                seen[digest] = payload_id
                payload_rows.append((payload_id, digest, len(blob), blob))
                source_rows.append((payload_id, record_id))
            mapping_rows.append((record_id, payload_id, digest, len(blob)))
            total += 1
        if payload_rows:
            connection.executemany(
                "INSERT INTO main.payloads (id, sha256, byte_length, bytes) "
                "VALUES (?, ?, ?, ?)",
                payload_rows,
            )
            connection.executemany(
                "INSERT INTO map.payload_source (payload_id, record_id) "
                "VALUES (?, ?)",
                source_rows,
            )
            payload_rows.clear()
            source_rows.clear()
        connection.executemany(
            "INSERT INTO map.record_payload "
            "(record_id, payload_id, source_sha, source_length) "
            "VALUES (?, ?, ?, ?)",
            mapping_rows,
        )
        mapping_rows.clear()
    return {"records_read": total, "distinct_payloads": len(seen)}


def _copy_events(connection: sqlite3.Connection, columns: list[str]) -> None:
    projected = ", ".join(
        _quote(column) for column in columns if column != PAYLOAD_COLUMN
    )
    connection.execute(
        f"""
        INSERT INTO main.{EVENT_TABLE} ({projected}, payload_id)
        SELECT {", ".join(f"r.{_quote(c)}" for c in columns if c != PAYLOAD_COLUMN)},
               m.payload_id
        FROM src.records r
        JOIN map.record_payload m ON m.record_id = r.id
        ORDER BY r.id
        """
    )


def _prove(
    connection: sqlite3.Connection, source_rows: int, source_payload_bytes: int
) -> dict[str, Any]:
    """Prove the store against the bytes it replaced, sequentially.

    Every check below is a scan of one or two tables in key order rather than a
    seek per record: the point is to read the payloads once more, not 2.18
    million times. Between them they close the loop -- each stored payload
    hashes to the key it is filed under, each event's digest taken from the
    *original* bytes matches the payload it now points at, and the view returns
    the same number of rows and the same byte total the flat table did.
    """
    rows = connection.execute(
        f"SELECT count(*) FROM main.{EVENT_TABLE}"
    ).fetchone()[0]
    if rows != source_rows:
        raise RuntimeError(f"event rows {rows} != source {source_rows}")

    mismatched = 0
    for identifier, digest, blob in connection.execute(
        "SELECT id, sha256, bytes FROM main.payloads ORDER BY id"
    ):
        if hashlib.sha256(bytes(blob)).digest() != bytes(digest):
            mismatched += 1
    if mismatched:
        raise RuntimeError(f"{mismatched} payloads do not hash to their key")

    disagreeing = connection.execute(
        """
        SELECT count(*) FROM map.record_payload m
        JOIN main.payloads p ON p.id = m.payload_id
        WHERE p.sha256 != m.source_sha OR p.byte_length != m.source_length
        """
    ).fetchone()[0]
    if disagreeing:
        raise RuntimeError(
            f"{disagreeing} events reference a payload that is not their bytes"
        )

    # One true byte comparison per distinct payload against a record that
    # produced it, so the proof rests on the bytes rather than on sha256 being
    # collision free.
    compared = differing = 0
    for stored, original in connection.execute(
        f"""
        SELECT p.bytes, r.{PAYLOAD_COLUMN}
        FROM main.payloads p
        JOIN map.payload_source s ON s.payload_id = p.id
        JOIN src.records r ON r.id = s.record_id
        ORDER BY p.id
        """
    ):
        compared += 1
        if bytes(stored) != bytes(original):
            differing += 1
    if differing:
        raise RuntimeError(f"{differing} payloads differ from their source bytes")

    dangling = connection.execute(
        f"SELECT count(*) FROM main.{EVENT_TABLE} re "
        "LEFT JOIN main.payloads p ON p.id = re.payload_id WHERE p.id IS NULL"
    ).fetchone()[0]
    if dangling:
        raise RuntimeError(f"{dangling} events reference no stored payload")

    logical = connection.execute(
        f"SELECT sum(length({PAYLOAD_COLUMN})) FROM main.records"
    ).fetchone()[0]
    if int(logical or 0) != source_payload_bytes:
        raise RuntimeError(
            f"view returns {logical} payload bytes, source held "
            f"{source_payload_bytes}"
        )
    stored_bytes = connection.execute(
        "SELECT sum(length(bytes)) FROM main.payloads"
    ).fetchone()[0]
    return {
        "payloads_compared_byte_for_byte": compared,
        "logical_payload_bytes": int(logical or 0),
        "stored_payload_bytes": int(stored_bytes or 0),
        "deduplication": (
            round(int(stored_bytes or 0) / source_payload_bytes, 6)
            if source_payload_bytes
            else None
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sessions", nargs="+")
    parser.add_argument("--rewrite", action="store_true")
    parser.add_argument("--stamped-utc", default=None)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()

    results = []
    for value in args.sessions:
        session = resolve_session(value)
        before = (session / DATABASE_NAME).stat().st_size
        result = compact(
            session, rewrite=args.rewrite, stamped_utc=args.stamped_utc
        )
        result["source_bytes"] = before
        results.append(result)
        (session / "capture-compaction.json").write_text(
            json.dumps(result, indent=2) + "\n", encoding="utf-8"
        )
        counts = result["counts"]
        print(f"{result['session']}:")
        print(f"  records                    {counts['records_read']:,}")
        print(f"  distinct payloads          {counts['distinct_payloads']:,}")
        print(f"  logical payload bytes      {counts['logical_payload_bytes']:,}")
        print(f"  stored payload bytes       {counts['stored_payload_bytes']:,}")
        print(f"  deduplication              {counts['deduplication']}")
        print(f"  database bytes             {before:,} -> "
              f"{result['database_bytes']:,}")
    if args.report:
        args.report.write_text(
            json.dumps({"sessions": results}, indent=2) + "\n", encoding="utf-8"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
