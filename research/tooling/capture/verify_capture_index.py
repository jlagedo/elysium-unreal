"""Judge the spine and the stored roll-up one indexed capture carries.

Usage:
    uv run elysium research verify_capture_index <session> [<session> ...]

`index_capture_database` materializes the joins six verifiers used to rebuild as
TEMP tables, and stores the roll-up `verify_capture_integrity` computes. This
tool reads the result back and asks the two questions that make it evidence
rather than convention: does every row of the spine reach the thing it names,
and do the stored payloads still hash to the digests they are keyed by.

An unjoined count is not automatically a defect. A pose build that produced no
evaluation has no actor to name, and a run that stops mid-scene leaves every
live actor's interval open; both are properties of the runtime that CAP2.1 and
CAP2.3 already measured. Those carry a stated reason and are reported apart from
the counts that would be defects, the same split the integrity roll-up makes.

Nothing here re-derives a join. The spine is checked against the tables it was
built from, so a disagreement names a defect in the pass rather than a second
opinion about what the capture meant.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sqlite3
from typing import Any

from research.tooling.capture.calibrate_theatre_capture import (
    DATABASE_NAME,
    compared_maps,
    open_database,
    resolve_session,
)

# The writer and the roll-up are imported inside the functions that need them.
# The roll-up imports this module to run it, and this module reads the writer's
# table names and the roll-up's shard declaration, so importing either at module
# scope closes a cycle through three files.


REPORT_NAME = "capture-index.json"

# A count whose non-zero value is a recorded property of the runtime rather than
# a defect, with the reason it holds. Mirrors the roll-up's own split: a number
# reported with its cause is accounted for, and folding it into a pass would
# hide the one case where it moves.
ACCOUNTED: dict[str, str] = {
    "pose_groups_without_an_actor": (
        "A bone cache answers a second SetupBones for an actor already posed "
        "this frame, so the call builds nothing and names no evaluation. "
        "CAP2.1 measured 181,751 of 419,700."
    ),
    "intervals_open_at_capture_stop": (
        "The run stops mid-scene, so every actor still alive has an interval "
        "the capture never saw closed. CAP2.3 measured 49."
    ),
    "lifetimes_open_at_capture_stop": (
        "Same stop, seen from the construction side: an actor alive at the "
        "stop has no destruction to pair with."
    ),
    "identities_observed_but_unused": (
        "An include-model bank is censused by the contribution path and "
        "animates no actor of its own, so it is observed without being used."
    ),
    "bindings_reaching_no_slot": (
        "A scene binds actors the client never posed, so the handle index it "
        "names reaches no observed entity. Measured apart rather than joined."
    ),
    "remap_arrays_outside_image": (
        "A captured image truncated at the census cap cannot hold the array "
        "its group points at; the group is recorded with the array absent."
    ),
}


def _scalar(connection: sqlite3.Connection, sql: str) -> int:
    row = connection.execute(sql).fetchone()
    return int(row[0] or 0) if row else 0


def support(connection: sqlite3.Connection) -> dict[str, Any]:
    from research.tooling.capture.index_capture_database import (
        INDEX_TABLES,
        ROLLUP_TABLES,
        SPINE_TABLES,
    )

    objects = {
        name: kind
        for kind, name in connection.execute(
            "SELECT type, name FROM sqlite_master WHERE type IN ('table', 'view')"
        )
    }
    return {
        "carries_spine": all(name in objects for name in SPINE_TABLES),
        "carries_rollup": all(name in objects for name in ROLLUP_TABLES),
        "carries_payload_store": "record_events" in objects
        and "payloads" in objects,
        "records_is_a_view": objects.get("records") == "view",
        "missing_tables": sorted(
            name for name in INDEX_TABLES if name not in objects
        ),
    }


def spine(connection: sqlite3.Connection) -> dict[str, Any]:
    """Every row of the spine against the table it was built from."""
    return {
        "generation_brackets": _scalar(
            connection, "SELECT count(*) FROM generation_bracket"
        ),
        "duplicate_generations": _scalar(
            connection,
            "SELECT count(*) FROM (SELECT generation FROM generation_bracket "
            "GROUP BY generation HAVING count(*) > 1)",
        ),
        "brackets_with_a_missing_parent": _scalar(
            connection,
            "SELECT count(*) FROM generation_bracket b LEFT JOIN "
            "generation_bracket p ON p.generation = b.parent "
            "WHERE b.depth > 0 AND p.generation IS NULL",
        ),
        "pose_groups": _scalar(connection, "SELECT count(*) FROM pose_group"),
        "pose_groups_without_an_actor": _scalar(
            connection,
            "SELECT count(*) FROM pose_group WHERE actor_entity IS NULL",
        ),
        "pose_groups_spanning_several_entities": _scalar(
            connection, "SELECT count(*) FROM pose_group WHERE entities > 1"
        ),
        "pose_groups_whose_bracket_is_missing": _scalar(
            connection,
            "SELECT count(*) FROM pose_group g LEFT JOIN generation_bracket b "
            "ON b.generation = g.generation WHERE b.generation IS NULL",
        ),
        # The bracket owner is the renderable and the evaluations name the
        # C_BaseAnimating. Both are stored as recorded, so this is a measurement
        # of the offset rather than an assertion of it.
        "pose_groups_whose_addresses_differ_by_four": _scalar(
            connection,
            "SELECT count(*) FROM pose_group WHERE actor_entity IS NOT NULL "
            "AND bracket_entity = actor_entity + 4",
        ),
        "pose_groups_whose_addresses_differ_otherwise": _scalar(
            connection,
            "SELECT count(*) FROM pose_group WHERE actor_entity IS NOT NULL "
            "AND bracket_entity != actor_entity + 4",
        ),
        "actor_intervals": _scalar(
            connection, "SELECT count(*) FROM actor_interval"
        ),
        "intervals_open_at_capture_stop": _scalar(
            connection,
            "SELECT count(*) FROM actor_interval WHERE closed_qpc IS NULL",
        ),
        "actor_lifetimes": _scalar(connection, "SELECT count(*) FROM actor_life"),
        "lifetimes_open_at_capture_stop": _scalar(
            connection,
            "SELECT count(*) FROM actor_life WHERE destructed_qpc IS NULL",
        ),
        "entity_slots": _scalar(connection, "SELECT count(*) FROM entity_slot"),
        "slots_serving_several_entities": _scalar(
            connection,
            "SELECT count(*) FROM (SELECT entity_index FROM entity_slot "
            "GROUP BY entity_index HAVING count(DISTINCT entity) > 1)",
        ),
        "model_identities": _scalar(
            connection, "SELECT count(*) FROM model_identity"
        ),
        # A run that carried no census observes nothing, so every identity it
        # used reads as unobserved. That is the absence of a stream, not a
        # broken join, and the verdict below separates the two.
        "census_observations": _scalar(
            connection, "SELECT count(*) FROM model_headers"
        ),
        "identities_used_without_observation": _scalar(
            connection,
            "SELECT count(*) FROM model_identity "
            "WHERE records > 0 AND first_observed_qpc IS NULL",
        ),
        "identities_observed_after_first_use": _scalar(
            connection,
            "SELECT count(*) FROM model_identity "
            "WHERE first_observed_qpc IS NOT NULL AND first_used_qpc IS NOT NULL "
            "AND first_observed_qpc > first_used_qpc",
        ),
        "identities_observed_but_unused": _scalar(
            connection, "SELECT count(*) FROM model_identity WHERE records = 0"
        ),
        "skeletons": _scalar(
            connection, "SELECT count(DISTINCT checksum) FROM skeleton_bone"
        ),
        "skeleton_bones": _scalar(
            connection, "SELECT count(*) FROM skeleton_bone"
        ),
        "images_without_a_skeleton": _scalar(
            connection,
            "SELECT count(*) FROM model_images i WHERE NOT EXISTS "
            "(SELECT 1 FROM skeleton_bone b WHERE b.checksum = i.checksum)",
        ),
        "remap_groups": _scalar(
            connection, "SELECT count(*) FROM bone_remap_group"
        ),
        "remap_arrays_located": _scalar(
            connection,
            "SELECT count(*) FROM bone_remap_group WHERE inside_image = 1",
        ),
        "remap_arrays_outside_image": _scalar(
            connection,
            "SELECT count(*) FROM bone_remap_group WHERE inside_image = 0",
        ),
        "scene_bindings": _scalar(
            connection, "SELECT count(*) FROM scene_binding"
        ),
        "bindings_reaching_no_slot": _scalar(
            connection,
            "SELECT count(*) FROM scene_binding s WHERE "
            "s.target_entity_index IS NOT NULL AND NOT EXISTS "
            "(SELECT 1 FROM entity_slot e "
            "WHERE e.entity_index = s.target_entity_index)",
        ),
    }


def payloads(connection: sqlite3.Connection, flags: dict[str, Any]) -> dict[str, Any]:
    """The content-addressed store against its own digests.

    Re-hashing every stored payload is what makes the store evidence rather than
    a claim: a row whose bytes no longer hash to the key they are filed under is
    either corruption or a collision, and both matter.
    """
    if not flags["carries_payload_store"]:
        return {
            "available": False,
            "reason": "this database stores its payloads inline",
        }
    rows = _scalar(connection, "SELECT count(*) FROM payloads")
    stored = _scalar(connection, "SELECT sum(length(bytes)) FROM payloads")
    events = _scalar(connection, "SELECT count(*) FROM record_events")
    dangling = _scalar(
        connection,
        "SELECT count(*) FROM record_events re LEFT JOIN payloads p "
        "ON p.id = re.payload_id WHERE p.id IS NULL",
    )
    mismatched = 0
    for identifier, digest, blob in connection.execute(
        "SELECT id, sha256, bytes FROM payloads"
    ):
        if hashlib.sha256(bytes(blob)).digest() != bytes(digest):
            mismatched += 1
    logical = _scalar(connection, "SELECT sum(length(raw_payload)) FROM records")
    declared = _scalar(
        connection, "SELECT sum(byte_length) FROM payloads"
    )
    return {
        "available": True,
        "payload_rows": rows,
        "stored_bytes": stored,
        "declared_bytes": declared,
        "event_rows": events,
        "logical_bytes_through_the_view": logical,
        "dangling_payload_ids": dangling,
        "payloads_not_matching_their_digest": mismatched,
        "deduplication": round(stored / logical, 6) if logical else None,
    }


def rollup(connection: sqlite3.Connection, flags: dict[str, Any]) -> dict[str, Any]:
    if not flags["carries_rollup"]:
        return {"available": False, "reason": "this database carries no roll-up"}
    from research.tooling.capture.verify_capture_integrity import SHARDS

    stored = {
        (tool, path)
        for tool, path in connection.execute(
            "SELECT tool, path FROM integrity_shard"
        )
    }
    declared = {
        (shard.tool, json.dumps(list(shard.path))) for shard in SHARDS
    }
    summary = {
        key: json.loads(value)
        for key, value in connection.execute(
            "SELECT key, value FROM integrity_summary"
        )
    }
    return {
        "available": True,
        "shards_stored": len(stored),
        "shards_declared": len(declared),
        "shards_declared_but_not_stored": sorted(
            f"{tool}.{path}" for tool, path in declared - stored
        ),
        "shards_stored_but_not_declared": sorted(
            f"{tool}.{path}" for tool, path in stored - declared
        ),
        "buckets": {
            bucket: {"shards": shards, "available": available, "nonzero": nonzero}
            for bucket, shards, available, nonzero in connection.execute(
                "SELECT bucket, shards, available, nonzero FROM integrity_bucket"
                " ORDER BY bucket"
            )
        },
        "summary_keys": sorted(summary),
        "verdict": summary.get("verdict"),
    }


def decide(
    flags: dict[str, Any],
    rows: dict[str, Any],
    store: dict[str, Any],
    roll: dict[str, Any],
) -> dict[str, Any]:
    if not flags["carries_spine"]:
        return {
            "judgeable": False,
            "spine_joined": False,
            "payloads_intact": False,
            "statement": (
                "This database has not been indexed; run "
                "index_capture_database over it first."
            ),
        }
    accounted_here = dict(ACCOUNTED)
    if not rows.get("census_observations"):
        accounted_here["identities_used_without_observation"] = (
            "This run carried no model census, so every identity it used is "
            "unobserved by construction."
        )
    unexpected = {
        name: value
        for name, value in rows.items()
        if value
        and name not in accounted_here
        and (
            name.startswith(("duplicate", "brackets_with", "slots_serving"))
            or "without" in name
            or "missing" in name
            or "no_slot" in name
            or "otherwise" in name
            or name == "identities_observed_after_first_use"
            or name == "pose_groups_spanning_several_entities"
        )
    }
    accounted = {
        name: value
        for name, value in rows.items()
        if value and name in accounted_here
    }
    joined = not unexpected
    intact = (not store.get("available")) or (
        store["dangling_payload_ids"] == 0
        and store["payloads_not_matching_their_digest"] == 0
    )
    if unexpected:
        statement = (
            f"{len(unexpected)} spine populations reach nothing they name: "
            + "; ".join(f"{name} = {value:,}" for name, value in unexpected.items())
        )
    elif not intact:
        statement = (
            f"{store['dangling_payload_ids']:,} events reference a payload that "
            f"is not stored and {store['payloads_not_matching_their_digest']:,} "
            "payloads no longer hash to their key"
        )
    else:
        parts = [
            f"{rows['pose_groups']:,} pose groups",
            f"{rows['actor_intervals']:,} actor intervals",
            f"{rows['model_identities']:,} model identities",
            f"{rows['skeleton_bones']:,} skeleton bones",
        ]
        if store.get("available"):
            parts.append(
                f"{store['payload_rows']:,} payloads at "
                f"{store['deduplication']:.4f} of their logical bytes"
            )
        statement = (
            "The spine joins: "
            + ", ".join(parts)
            + f", with {len(accounted)} accounted populations and no unjoined one."
        )
    return {
        "judgeable": True,
        "spine_joined": joined,
        "payloads_intact": intact,
        "unexpected": unexpected,
        "accounted": {name: accounted_here[name] for name in accounted},
        "rollup_stored": bool(roll.get("available")),
        "statement": statement,
    }


def verify(session: Path) -> dict[str, Any]:
    connection = open_database(session)
    try:
        flags = support(connection)
        identity = {
            key: json.loads(value)
            for key, value in connection.execute(
                "SELECT key, value FROM capture_metadata WHERE key IN "
                "('map', 'created_utc', 'index_version', 'index_tool_git', "
                "'index_source_database_sha256', 'index_built_utc')"
            )
        }
        rows = spine(connection) if flags["carries_spine"] else {}
        store = payloads(connection, flags)
        roll = rollup(connection, flags)
        verdict = decide(flags, rows, store, roll)
    finally:
        connection.close()
    return {
        "session": session.name,
        "session_path": str(session),
        "database": str(session / DATABASE_NAME),
        "identity": identity,
        "support": flags,
        "spine": rows,
        "payloads": store,
        "rollup": roll,
        "verdict": verdict,
    }


def compare(reports: list[dict[str, Any]]) -> dict[str, Any]:
    maps = compared_maps(reports)
    return {
        "sessions": [report["session"] for report in reports],
        "maps": maps,
        "pose_groups": [
            report["spine"].get("pose_groups") for report in reports
        ],
        "model_identities": [
            report["spine"].get("model_identities") for report in reports
        ],
        "statement": (
            "Counts are a property of each run's own cast and are reported "
            "side by side rather than compared."
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sessions", nargs="+")
    parser.add_argument("--report", type=Path)
    parser.add_argument(
        "--no-session-reports", dest="session_reports", action="store_false"
    )
    args = parser.parse_args()

    reports = []
    for value in args.sessions:
        session = resolve_session(value)
        report = verify(session)
        reports.append(report)
        if args.session_reports:
            (session / REPORT_NAME).write_text(
                json.dumps(report, indent=2) + "\n", encoding="utf-8"
            )
        print(f"{report['session']}: {report['verdict']['statement']}")
    if args.report:
        args.report.write_text(
            json.dumps(
                {
                    "sessions": [report["session"] for report in reports],
                    "reports": reports,
                    "comparison": compare(reports) if len(reports) > 1 else None,
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
    return (
        0
        if all(
            report["verdict"]["spine_joined"]
            and report["verdict"]["payloads_intact"]
            for report in reports
        )
        else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
