"""Index one finalized capture into a joinable spine and a stored roll-up.

Usage:
    uv run elysium research index_capture_database <session> [<session> ...]

CAP1 and CAP2 left every join the verifiers need computed as a TEMP table inside
whichever verifier asked the question. Six of them rebuild the same three grains
-- an address plus a checksum, its first observation, its usage extent -- and two
of those disagree about which observations count. CAP3.2 asks for one spine
instead: the derived tables materialized once, keyed on the generation each
record already carries rather than on nearest timestamp, so a question about a
time and an entity is a join rather than a rebuild.

CAP3.3 asks the database to report its own counts. `verify_capture_integrity`
already computes that roll-up and must not gain a writer, so this pass calls it
and stores what it returns. The verifier stays the offline recomputation the
stored copy is diffed against.

Nothing here pairs records by time. Every table below is keyed by an identity --
a generation, an address and a checksum, an entity index, a scope -- and a
timestamp appears only as the containment predicate on a pairing an identity
already established, or as a `min`/`max` tie-break inside one identity's own
group. That is the invariant the six verifiers hold and the reason their joins
are evidence.

The database is evidence, so the write is guarded the way `resolve_consumed_spans`
guards its own: it refuses a database that already carries the tables, records
the digest the file had before the pass, and commits once.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sqlite3
import struct
from typing import Any

from elysium_pipeline.formats import mdl_skel

from research.tooling.capture.calibrate_theatre_capture import (
    DATABASE_NAME,
    resolve_session,
    table_columns,
)
from research.tooling.capture.resolve_consumed_spans import file_sha256, tool_commit
from research.tooling.capture.verify_capture_integrity import _tools, aggregate
from research.tooling.capture.verify_capture_index import spine as spine_coverage


INDEX_VERSION = 1

# The kinds the spine reads, named here rather than inlined so the tables below
# and the verifier that checks them cannot drift apart.
EVALUATION_KINDS = ("BASE", "FINL")
POSE_BUILD_KIND = "PBLD"
DRAW_FRAME_KINDS = ("DBLD", "SHDW")
BRACKET_KINDS = (POSE_BUILD_KIND, *DRAW_FRAME_KINDS)
POSE_KIND = "POSE"
CONTRIBUTION_KINDS = ("SEQP", "ANIM")

# Observation reasons, mirroring verify_actor_identity_lifetime. Reason 3 is the
# residency sweep at capture stop: it runs after every evaluation, so it can
# satisfy "observed at or before first use" without witnessing anything, and the
# observation grains below exclude it.
OBSERVATION_FIRST = 1
OBSERVATION_IDENTITY_CHANGE = 2
OBSERVATION_RESIDENT_AT_STOP = 3
OBSERVATION_CONSTRUCT = 4
OBSERVATION_DESTRUCT = 5
OPENING_REASONS = (OBSERVATION_FIRST, OBSERVATION_IDENTITY_CHANGE)

# Model census reasons, mirroring verify_model_skeleton_census.
HEADER_RESIDENT_AT_STOP = 3

# Scene request reasons, mirroring verify_scene_requests.
SCENE_REASON_BINDING = 5
SCENE_REASON_ANIMATION_SET = 6

# MDLHeader displacements. Owned by docs/vtmb/mdl_v2531.md; repeated here only
# because this pass reads the captured image directly.
HEADER_NUM_BONES = 240
HEADER_NUM_INCLUDE_MODELS = 404
HEADER_INCLUDE_MODEL_INDEX = 408
# StudioModelGroup is 116 bytes. `+0x10` holds an authored offset, relative to
# the group entry, to that group's bone remap array -- an index no header field
# points at, which is why a walk from the header alone misses the region
# entirely. The array holds one 56-byte record per bone of the including model.
MODEL_GROUP_STRIDE = 116
MODEL_GROUP_REMAP_OFFSET = 0x10
REMAP_RECORD_BYTES = 56

SPINE_TABLES = (
    "generation_bracket",
    "pose_group",
    "actor_interval",
    "actor_life",
    "actor_renderable",
    "entity_slot",
    "model_identity",
    "skeleton_bone",
    "bone_remap_group",
    "scene_binding",
)
ROLLUP_TABLES = ("integrity_shard", "integrity_bucket", "integrity_summary")
INDEX_TABLES = (*SPINE_TABLES, *ROLLUP_TABLES)


SCHEMA = """
CREATE TABLE generation_bracket (
    generation INTEGER NOT NULL,
    kind TEXT NOT NULL,
    parent INTEGER NOT NULL,
    depth INTEGER NOT NULL,
    owner_entity INTEGER,
    entry_qpc INTEGER NOT NULL,
    exit_qpc INTEGER NOT NULL,
    thread_id INTEGER NOT NULL
);
CREATE TABLE pose_group (
    generation INTEGER NOT NULL,
    parent_generation INTEGER NOT NULL,
    bracket_entity INTEGER,
    actor_entity INTEGER,
    checksum INTEGER,
    thread_id INTEGER NOT NULL,
    entry_qpc INTEGER NOT NULL,
    exit_qpc INTEGER NOT NULL,
    entities INTEGER NOT NULL,
    evaluations INTEGER NOT NULL,
    contributions INTEGER NOT NULL,
    draws INTEGER NOT NULL
);
CREATE TABLE actor_interval (
    entity INTEGER NOT NULL,
    checksum INTEGER NOT NULL,
    opened_qpc INTEGER NOT NULL,
    closed_qpc INTEGER,
    observations INTEGER NOT NULL
);
CREATE TABLE actor_life (
    entity INTEGER NOT NULL,
    constructed_qpc INTEGER NOT NULL,
    destructed_qpc INTEGER
);
CREATE TABLE actor_renderable (
    entity INTEGER NOT NULL,
    renderable INTEGER NOT NULL
);
CREATE TABLE entity_slot (
    entity_index INTEGER NOT NULL,
    entity INTEGER NOT NULL,
    renderable INTEGER,
    first_qpc INTEGER NOT NULL,
    last_qpc INTEGER NOT NULL,
    observations INTEGER NOT NULL
);
CREATE TABLE model_identity (
    studio_hdr INTEGER NOT NULL,
    checksum INTEGER NOT NULL,
    first_observed_qpc INTEGER,
    first_used_qpc INTEGER,
    last_used_qpc INTEGER,
    bone_count INTEGER,
    model_name TEXT,
    model_length INTEGER,
    include_model_count INTEGER,
    observations INTEGER NOT NULL,
    records INTEGER NOT NULL
);
CREATE TABLE skeleton_bone (
    checksum INTEGER NOT NULL,
    bone_index INTEGER NOT NULL,
    name TEXT NOT NULL,
    parent INTEGER NOT NULL,
    flags INTEGER NOT NULL,
    pos_x REAL NOT NULL,
    pos_y REAL NOT NULL,
    pos_z REAL NOT NULL,
    quat_x REAL NOT NULL,
    quat_y REAL NOT NULL,
    quat_z REAL NOT NULL,
    quat_w REAL NOT NULL,
    pose_to_bone BLOB NOT NULL
);
CREATE TABLE bone_remap_group (
    owner_checksum INTEGER NOT NULL,
    group_index INTEGER NOT NULL,
    include_path TEXT NOT NULL,
    group_base INTEGER NOT NULL,
    array_offset INTEGER NOT NULL,
    record_count INTEGER NOT NULL,
    record_bytes INTEGER NOT NULL,
    inside_image INTEGER NOT NULL,
    records BLOB
);
CREATE TABLE scene_binding (
    scope INTEGER NOT NULL,
    scene_entity INTEGER,
    scene_file TEXT,
    actor_name TEXT,
    animation_set TEXT,
    target_entity_index INTEGER,
    target_entity INTEGER,
    target_entity_serial INTEGER,
    bound_qpc INTEGER,
    applied_qpc INTEGER
);
CREATE TABLE integrity_shard (
    tool TEXT NOT NULL,
    name TEXT NOT NULL,
    path TEXT NOT NULL,
    bucket TEXT NOT NULL,
    status TEXT NOT NULL,
    count INTEGER,
    of_path TEXT,
    denominator INTEGER,
    ratio REAL,
    expected_nonzero INTEGER NOT NULL,
    because TEXT NOT NULL
);
CREATE TABLE integrity_bucket (
    bucket TEXT PRIMARY KEY,
    shards INTEGER NOT NULL,
    available INTEGER NOT NULL,
    nonzero INTEGER NOT NULL
);
CREATE TABLE integrity_summary (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE UNIQUE INDEX generation_bracket_generation
    ON generation_bracket(generation);
CREATE INDEX generation_bracket_parent ON generation_bracket(parent);
CREATE UNIQUE INDEX pose_group_generation ON pose_group(generation);
CREATE INDEX pose_group_actor ON pose_group(actor_entity, entry_qpc);
CREATE UNIQUE INDEX actor_interval_identity
    ON actor_interval(entity, checksum);
CREATE INDEX actor_life_entity ON actor_life(entity);
CREATE UNIQUE INDEX actor_renderable_entity ON actor_renderable(entity);
CREATE INDEX actor_renderable_reverse ON actor_renderable(renderable);
CREATE UNIQUE INDEX entity_slot_identity ON entity_slot(entity_index, entity);
CREATE UNIQUE INDEX model_identity_identity
    ON model_identity(studio_hdr, checksum);
CREATE INDEX model_identity_checksum ON model_identity(checksum);
CREATE UNIQUE INDEX skeleton_bone_identity
    ON skeleton_bone(checksum, bone_index);
CREATE UNIQUE INDEX bone_remap_group_identity
    ON bone_remap_group(owner_checksum, group_index);
CREATE INDEX scene_binding_scope ON scene_binding(scope);
CREATE INDEX scene_binding_target ON scene_binding(target_entity_index);
CREATE UNIQUE INDEX integrity_shard_identity ON integrity_shard(tool, path);
"""


def _in(values: tuple[str, ...]) -> str:
    return ", ".join(f"'{value}'" for value in values)


def support(connection: sqlite3.Connection) -> dict[str, bool]:
    """What this database carries, so a pass over an older one still runs.

    A stream that was added after a capture was taken leaves neither its table
    nor its columns behind, so each section asks for what it needs rather than
    assuming the current shape and failing on a database CAP1 measured.
    """
    tables = {
        name
        for (name,) in connection.execute(
            "SELECT name FROM sqlite_master WHERE type IN ('table', 'view')"
        )
    }
    records = table_columns(connection, "records")
    actors = table_columns(connection, "actor_observations")
    return {
        "carries_generations": "generation" in records,
        "carries_contributions": "owner_checksum" in records,
        "carries_actors": "actor_observations" in tables,
        "carries_actor_indices": "entity_index" in actors,
        "carries_images": "model_images" in tables,
        "carries_scene": "scene_events" in tables,
    }


def _brackets(connection: sqlite3.Connection) -> int:
    """One row per opened generation.

    A bracket record is both the entry and the exit -- `entry_qpc` stamps the
    entry and `qpc` the exit on the same row -- so the span comes from one row
    rather than from pairing two.
    """
    connection.execute(
        f"""
        INSERT INTO generation_bracket (
            generation, kind, parent, depth, owner_entity, entry_qpc, exit_qpc,
            thread_id
        )
        SELECT generation, kind, generation_parent, generation_depth,
               client_entity, entry_qpc, qpc, thread_id
        FROM records
        WHERE kind IN ({_in(BRACKET_KINDS)})
        """
    )
    return connection.execute(
        "SELECT count(*) FROM generation_bracket"
    ).fetchone()[0]


def _pose_groups(connection: sqlite3.Connection, flags: dict[str, bool]) -> int:
    """One row per pose build, carrying both addresses it is known by.

    The bracket owner is the renderable subobject and the evaluations name the
    `C_BaseAnimating`; the two differ by a fixed four. Both are stored as
    recorded. Deriving one from the other would erase the relation four separate
    verifiers exist to establish.
    """
    connection.execute(
        f"""
        CREATE TEMP TABLE evaluated_group AS
        SELECT generation,
               count(*) AS evaluations,
               count(DISTINCT client_entity) AS entities,
               min(client_entity) AS actor_entity,
               min(checksum) AS checksum
        FROM records
        WHERE kind IN ({_in(EVALUATION_KINDS)}) AND generation IS NOT NULL
        GROUP BY generation
        """
    )
    connection.execute(
        "CREATE UNIQUE INDEX temp.evaluated_group_generation "
        "ON evaluated_group(generation)"
    )
    if flags["carries_contributions"]:
        connection.execute(
            f"""
            CREATE TEMP TABLE contributed_group AS
            SELECT generation, count(*) AS contributions
            FROM records
            WHERE kind IN ({_in(CONTRIBUTION_KINDS)}) AND generation IS NOT NULL
            GROUP BY generation
            """
        )
    else:
        connection.execute(
            "CREATE TEMP TABLE contributed_group "
            "(generation INTEGER, contributions INTEGER)"
        )
    connection.execute(
        "CREATE UNIQUE INDEX temp.contributed_group_generation "
        "ON contributed_group(generation)"
    )
    # A draw names the pose build it consumed in `carry_generation`, which is
    # what attributes it to a group; `generation` names the draw frame that
    # submitted it and is a different question.
    connection.execute(
        f"""
        CREATE TEMP TABLE drawn_group AS
        SELECT carry_generation AS generation, count(*) AS draws
        FROM records
        WHERE kind = '{POSE_KIND}' AND carry_generation IS NOT NULL
          AND carry_generation != 0
        GROUP BY carry_generation
        """
    )
    connection.execute(
        "CREATE UNIQUE INDEX temp.drawn_group_generation "
        "ON drawn_group(generation)"
    )
    connection.execute(
        f"""
        INSERT INTO pose_group (
            generation, parent_generation, bracket_entity, actor_entity,
            checksum, thread_id, entry_qpc, exit_qpc, entities, evaluations,
            contributions, draws
        )
        SELECT b.generation, b.parent, b.owner_entity, e.actor_entity,
               e.checksum, b.thread_id, b.entry_qpc, b.exit_qpc,
               COALESCE(e.entities, 0), COALESCE(e.evaluations, 0),
               COALESCE(c.contributions, 0), COALESCE(d.draws, 0)
        FROM generation_bracket b
        LEFT JOIN evaluated_group e ON e.generation = b.generation
        LEFT JOIN contributed_group c ON c.generation = b.generation
        LEFT JOIN drawn_group d ON d.generation = b.generation
        WHERE b.kind = '{POSE_BUILD_KIND}'
        """
    )
    for table in ("evaluated_group", "contributed_group", "drawn_group"):
        connection.execute(f"DROP TABLE temp.{table}")
    return connection.execute("SELECT count(*) FROM pose_group").fetchone()[0]


def _actors(connection: sqlite3.Connection, flags: dict[str, bool]) -> dict[str, int]:
    """The actor grains, lifted from verify_actor_identity_lifetime.

    The interval is the window in which an address demonstrably meant one actor:
    from the observation that opened it to the next observation giving the same
    address a different model. An open interval never changed identity. The
    lifetime is the separate witness -- construction to the next destruction at
    that address -- and it is what separates two actors an interval cannot,
    because an address rebuilt under an unchanged checksum looks like one actor
    to identity alone.
    """
    connection.execute(
        f"""
        INSERT INTO actor_interval (
            entity, checksum, opened_qpc, closed_qpc, observations
        )
        SELECT o.entity, o.checksum, o.first_qpc,
               (SELECT min(n.qpc) FROM actor_observations n
                 WHERE n.entity = o.entity
                   AND n.reason IN ({_in_ints(OPENING_REASONS)})
                   AND n.checksum != o.checksum
                   AND n.qpc > o.first_qpc),
               o.observations
        FROM (
            SELECT entity, checksum, min(qpc) AS first_qpc,
                   count(*) AS observations
            FROM actor_observations
            WHERE reason IN ({_in_ints(OPENING_REASONS)})
            GROUP BY entity, checksum
        ) o
        """
    )
    # Restricted to addresses an opening observation named, because destruction
    # is recorded only for those: a client entity constructed and destroyed
    # without ever being posed would otherwise contribute a construction that can
    # never be paired and read as a lifetime still open at capture stop.
    connection.execute(
        f"""
        INSERT INTO actor_life (entity, constructed_qpc, destructed_qpc)
        SELECT c.entity, c.qpc,
               (SELECT min(d.qpc) FROM actor_observations d
                 WHERE d.entity = c.entity
                   AND d.reason = {OBSERVATION_DESTRUCT}
                   AND d.qpc > c.qpc)
        FROM actor_observations c
        WHERE c.reason = {OBSERVATION_CONSTRUCT}
          AND c.entity IN (SELECT entity FROM actor_interval)
        """
    )
    # The renderable subobject as observed, never as computed. A draw record
    # names that address, so this is the only bridge from an actor to its draws
    # that does not assume the offset the run is meant to be evidence for.
    connection.execute(
        """
        INSERT INTO actor_renderable (entity, renderable)
        SELECT entity, min(renderable)
        FROM actor_observations WHERE renderable != 0
        GROUP BY entity
        """
    )
    if flags["carries_actor_indices"]:
        # The client side of the server/client handle join. It carries an index
        # and no serial, so a slot is bounded by the address's own intervals
        # rather than by a serial the schema never recorded.
        connection.execute(
            """
            INSERT INTO entity_slot (
                entity_index, entity, renderable, first_qpc, last_qpc,
                observations
            )
            SELECT o.entity_index, o.entity,
                   (SELECT renderable FROM actor_renderable r
                     WHERE r.entity = o.entity),
                   min(o.qpc), max(o.qpc), count(*)
            FROM actor_observations o
            WHERE o.entity_index IS NOT NULL
            GROUP BY o.entity_index, o.entity
            """
        )
    return {
        "actor_intervals": connection.execute(
            "SELECT count(*) FROM actor_interval"
        ).fetchone()[0],
        "actor_lifetimes": connection.execute(
            "SELECT count(*) FROM actor_life"
        ).fetchone()[0],
        "actor_renderables": connection.execute(
            "SELECT count(*) FROM actor_renderable"
        ).fetchone()[0],
        "entity_slots": connection.execute(
            "SELECT count(*) FROM entity_slot"
        ).fetchone()[0],
    }


def _in_ints(values: tuple[int, ...]) -> str:
    return ", ".join(str(value) for value in values)


def _models(connection: sqlite3.Connection) -> int:
    """One row per `(studio_hdr, checksum)`, observed and used in one grain.

    Three verifiers rebuild this and two of them disagree: the census excludes
    the capture-stop residency sweep from the observation grain and source
    attribution does not, so the same identity can be "observed before first
    use" in one report and late in the other. This table excludes it, which is
    the stricter of the two and the one whose exclusion is reasoned.

    The key set is the union of observed and used rather than either side, so an
    identity the run used without observing still gets a row with a null
    observation instead of vanishing from the spine.
    """
    connection.execute(
        f"""
        CREATE TEMP TABLE model_observed AS
        SELECT studio_hdr, checksum, min(qpc) AS first_qpc,
               count(*) AS observations, min(model_name) AS model_name,
               min(model_length) AS model_length,
               min(include_model_count) AS include_model_count,
               max(bone_count) AS bone_count
        FROM model_headers
        WHERE reason != {HEADER_RESIDENT_AT_STOP}
        GROUP BY studio_hdr, checksum
        """
    )
    connection.execute(
        "CREATE UNIQUE INDEX temp.model_observed_identity "
        "ON model_observed(studio_hdr, checksum)"
    )
    connection.execute(
        """
        CREATE TEMP TABLE model_used AS
        SELECT studio_hdr, checksum, min(qpc) AS first_qpc,
               max(qpc) AS last_qpc, max(bone_count) AS bone_count,
               count(*) AS records
        FROM records
        WHERE studio_hdr IS NOT NULL AND checksum IS NOT NULL
        GROUP BY studio_hdr, checksum
        """
    )
    connection.execute(
        "CREATE UNIQUE INDEX temp.model_used_identity "
        "ON model_used(studio_hdr, checksum)"
    )
    connection.execute(
        """
        INSERT INTO model_identity (
            studio_hdr, checksum, first_observed_qpc, first_used_qpc,
            last_used_qpc, bone_count, model_name, model_length,
            include_model_count, observations, records
        )
        SELECT k.studio_hdr, k.checksum, o.first_qpc, u.first_qpc, u.last_qpc,
               COALESCE(o.bone_count, u.bone_count), o.model_name,
               o.model_length, o.include_model_count,
               COALESCE(o.observations, 0), COALESCE(u.records, 0)
        FROM (
            SELECT studio_hdr, checksum FROM model_observed
            UNION
            SELECT studio_hdr, checksum FROM model_used
        ) k
        LEFT JOIN model_observed o
               ON o.studio_hdr = k.studio_hdr AND o.checksum = k.checksum
        LEFT JOIN model_used u
               ON u.studio_hdr = k.studio_hdr AND u.checksum = k.checksum
        """
    )
    connection.execute("DROP TABLE temp.model_observed")
    connection.execute("DROP TABLE temp.model_used")
    return connection.execute(
        "SELECT count(*) FROM model_identity"
    ).fetchone()[0]


def _cstr(image: bytes, offset: int) -> str:
    end = image.find(b"\0", offset)
    if end < 0:
        end = len(image)
    return image[offset:end].decode("ascii", "replace")


def _i32(image: bytes, offset: int) -> int:
    return struct.unpack_from("<i", image, offset)[0]


def _skeletons(connection: sqlite3.Connection) -> dict[str, int]:
    """Decode each captured image's bones through the pipeline's own decoder.

    The decoder is imported rather than reimplemented: a second bone reader here
    would be a second thing to keep correct, and the point of the spine is that
    a skeleton is queryable, not that this pass knows how to read one.
    """
    decoded = failed = bones = 0
    for checksum, image in connection.execute(
        "SELECT checksum, image FROM model_images ORDER BY checksum"
    ):
        blob = bytes(image)
        try:
            skeleton = mdl_skel.read_bones(blob)
        except Exception:
            failed += 1
            continue
        connection.executemany(
            """
            INSERT INTO skeleton_bone (
                checksum, bone_index, name, parent, flags, pos_x, pos_y, pos_z,
                quat_x, quat_y, quat_z, quat_w, pose_to_bone
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """,
            [
                (
                    int(checksum),
                    bone.index,
                    bone.name,
                    bone.parent,
                    bone.flags,
                    *(float(value) for value in bone.pos),
                    *(float(value) for value in bone.quat),
                    struct.pack("<12f", *bone.pose_to_bone),
                )
                for bone in skeleton
            ],
        )
        decoded += 1
        bones += len(skeleton)
    return {
        "skeletons_decoded": decoded,
        "skeletons_failed": failed,
        "skeleton_bones": bones,
    }


def _remaps(connection: sqlite3.Connection) -> dict[str, int]:
    """Locate each include group's bone remap array and retain it raw.

    The addressing is confirmed: the array sits at an authored offset held at
    `StudioModelGroup`+0x10 *relative to the group entry*, and holds one 56-byte
    record per bone of the including model. The record's fields are not. The
    nested virtual-model remap branch that consumes them is an open seam, so the
    bytes are stored as evidence and nothing here claims to know what they mean.
    Losing the join would be the failure; leaving the interpretation open is not.
    """
    groups = located = outside = 0
    for checksum, image in connection.execute(
        "SELECT checksum, image FROM model_images ORDER BY checksum"
    ):
        blob = bytes(image)
        if len(blob) < HEADER_INCLUDE_MODEL_INDEX + 4:
            continue
        count = _i32(blob, HEADER_NUM_INCLUDE_MODELS)
        base = _i32(blob, HEADER_INCLUDE_MODEL_INDEX)
        bones = _i32(blob, HEADER_NUM_BONES)
        if count <= 0 or base <= 0:
            continue
        for index in range(count):
            entry = base + index * MODEL_GROUP_STRIDE
            if entry < 0 or entry + MODEL_GROUP_STRIDE > len(blob):
                break
            path = _cstr(blob, entry + _i32(blob, entry)).replace("\\", "/")
            offset = _i32(blob, entry + MODEL_GROUP_REMAP_OFFSET)
            start = entry + offset
            length = bones * REMAP_RECORD_BYTES
            inside = (
                offset > 0 and start >= 0 and start + length <= len(blob)
            )
            connection.execute(
                """
                INSERT INTO bone_remap_group (
                    owner_checksum, group_index, include_path, group_base,
                    array_offset, record_count, record_bytes, inside_image,
                    records
                ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    int(checksum),
                    index,
                    path,
                    entry,
                    start,
                    bones,
                    REMAP_RECORD_BYTES,
                    1 if inside else 0,
                    blob[start : start + length] if inside else None,
                ),
            )
            groups += 1
            located += 1 if inside else 0
            outside += 0 if inside else 1
    return {
        "remap_groups": groups,
        "remap_arrays_located": located,
        "remap_arrays_outside_image": outside,
    }


def _scene_bindings(connection: sqlite3.Connection) -> int:
    """Join each applied animation set to the actor its own scope bound.

    Both sides come from the scope the server opened, not from a timestamp: the
    binding names the target and the application names the set, and the scope is
    what says they belong to the same request.
    """
    connection.execute(
        f"""
        INSERT INTO scene_binding (
            scope, scene_entity, scene_file, actor_name, animation_set,
            target_entity_index, target_entity, target_entity_serial,
            bound_qpc, applied_qpc
        )
        SELECT s.scope, s.scene_entity, s.scene_file, s.actor_name, s.text1,
               b.target_entity_index, b.target_entity, b.target_entity_serial,
               b.qpc, s.qpc
        FROM scene_events s
        JOIN scene_events b
          ON b.scope = s.scope AND b.reason = {SCENE_REASON_BINDING}
        WHERE s.reason = {SCENE_REASON_ANIMATION_SET}
          AND b.target_entity_index IS NOT NULL
        """
    )
    return connection.execute("SELECT count(*) FROM scene_binding").fetchone()[0]


def _dig(node: Any, path: tuple[str, ...]) -> Any:
    for key in path:
        if not isinstance(node, dict):
            return None
        node = node.get(key)
    return node


# What CAP3.3 asks a finalized database to report about itself. Each is a path
# into the report the verifier already produces, so the stored copy and the
# recomputation read the same value from the same place.
SUMMARY_PATHS: tuple[tuple[str, tuple[str, ...]], ...] = (
    ("map", ("identity", "map")),
    ("created_utc", ("identity", "created_utc")),
    ("capture_duration_seconds", ("identity", "capture_duration_seconds")),
    ("retail_exit_code", ("identity", "retail_exit_code")),
    ("hook", ("integrity", "hook")),
    ("failures", ("integrity", "failures")),
    ("sequence_numbers", ("integrity", "sequence_numbers")),
    ("supervision", ("integrity", "supervision")),
    ("volume", ("volume",)),
    ("zero", ("zero",)),
    ("database", ("database",)),
)


def _rollup(
    connection: sqlite3.Connection,
    report: dict[str, Any],
    reports: dict[str, Any] | None,
) -> dict[str, Any]:
    """Store the roll-up the integrity verifier computed.

    The verifier owns the shard table and must not gain a writer, so this pass
    calls it and persists what came back. Nothing is re-derived here: a second
    implementation of thirty predicates would fork from the verifiers that own
    them and the first correction to any one would silently stop applying.

    The report is computed before the write connection opens. Each verifier
    opens its own read-only connection, and running them underneath an open
    write transaction would have them read around it.
    """
    connection.executemany(
        """
        INSERT INTO integrity_shard (
            tool, name, path, bucket, status, count, of_path, denominator,
            ratio, expected_nonzero, because
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """,
        [
            (
                row.get("tool"),
                row.get("name"),
                json.dumps(row.get("path")),
                row.get("bucket"),
                row.get("status"),
                row.get("count"),
                json.dumps(row.get("of")) if row.get("of") else None,
                row.get("denominator"),
                row.get("ratio"),
                1 if row.get("expected_nonzero") else 0,
                row.get("because") or "",
            )
            for row in report.get("shards", [])
        ],
    )
    connection.executemany(
        """
        INSERT INTO integrity_bucket (bucket, shards, available, nonzero)
        VALUES (?, ?, ?, ?)
        """,
        [
            (
                bucket,
                int(values.get("shards", 0)),
                int(values.get("available", 0)),
                int(values.get("nonzero", 0)),
            )
            for bucket, values in (report.get("by_bucket") or {}).items()
        ],
    )
    calibration = (reports or {}).get("calibrate")
    rows: list[tuple[str, str]] = []
    if isinstance(calibration, dict):
        for key, path in SUMMARY_PATHS:
            rows.append((key, json.dumps(_dig(calibration, path))))
    for key in ("written", "verdict"):
        rows.append((key, json.dumps(report.get(key))))
    for key in (
        "unexpected_nonzero",
        "accounted_nonzero",
        "missing_paths",
        "unavailable",
    ):
        rows.append((f"{key}_count", json.dumps(len(report.get(key) or []))))
    connection.executemany(
        "INSERT OR REPLACE INTO integrity_summary (key, value) VALUES (?, ?)",
        rows,
    )
    return report


def index(
    session: Path,
    *,
    rewrite: bool = False,
    join_source: bool = True,
    rollup: bool = True,
    reports: dict[str, Any] | None = None,
    stamped_utc: str | None = None,
) -> dict[str, Any]:
    """Build the spine and the stored roll-up inside one finalized database."""
    database = session / DATABASE_NAME
    if not database.is_file():
        raise FileNotFoundError(f"no finalized database at {database}")
    source_digest = file_sha256(database)
    # Before the write connection opens: every verifier the roll-up runs takes
    # its own read-only connection, and underneath an open write transaction
    # they would read around it and describe a database that no longer exists.
    #
    # The verifiers run here rather than inside `aggregate` so their own reports
    # survive the call. CAP3.3's summary wants what only the calibration knows --
    # capture span, queue and disk high-water, boundary markers, cleanup -- and
    # `aggregate` returns the roll-up alone. The mapping comes from the roll-up
    # so the two cannot name different tools.
    report = None
    if rollup:
        if reports is None:
            reports = {
                name: run(session) for name, run in _tools(join_source).items()
            }
        report = aggregate(session, join_source=join_source, reports=reports)
    connection = sqlite3.connect(database)
    connection.execute("PRAGMA foreign_keys = ON")
    try:
        existing = {
            name
            for (name,) in connection.execute(
                "SELECT name FROM sqlite_master WHERE type IN ('table', 'view')"
            )
        }
        already = sorted(set(INDEX_TABLES) & existing)
        if already and not rewrite:
            raise ValueError(
                f"{database} already carries {', '.join(already)}; "
                "a finalized database is indexed once. Pass --rewrite to "
                "rebuild the spine after a change to this pass."
            )
        if already:
            # The spine is derived, so replacing it costs nothing that is not
            # regenerable; the guard makes a second pass deliberate rather than
            # impossible.
            connection.execute("PRAGMA foreign_keys = OFF")
            for table in reversed(INDEX_TABLES):
                connection.execute(f"DROP TABLE IF EXISTS {table}")
            connection.execute("PRAGMA foreign_keys = ON")
        connection.executescript(SCHEMA)

        flags = support(connection)
        counts: dict[str, Any] = {"brackets": _brackets(connection)}
        counts["pose_groups"] = _pose_groups(connection, flags)
        if flags["carries_actors"]:
            counts.update(_actors(connection, flags))
        counts["model_identities"] = _models(connection)
        if flags["carries_images"]:
            counts.update(_skeletons(connection))
            counts.update(_remaps(connection))
        if flags["carries_scene"]:
            counts["scene_bindings"] = _scene_bindings(connection)

        verdict: dict[str, Any] | None = None
        if report is not None:
            verdict = _rollup(connection, report, reports).get("verdict")

        # CAP3.3 asks the database for its unjoined counts, and the spine's own
        # are not among the roll-up's: that is computed before this pass writes,
        # so it can only describe a database with no spine in it. They are
        # measured here with the same query the verifier judges them by, so the
        # stored copy and the recomputation cannot drift.
        coverage = spine_coverage(connection)
        connection.execute(
            "INSERT OR REPLACE INTO integrity_summary (key, value) VALUES (?, ?)",
            ("spine", json.dumps(coverage)),
        )
        counts["unjoined"] = {
            name: value
            for name, value in coverage.items()
            if value
            and ("without" in name or "missing" in name or "no_slot" in name)
        }

        metadata = {
            "index_source_database_sha256": source_digest,
            "index_version": INDEX_VERSION,
            "index_tool_git": tool_commit(),
        }
        if stamped_utc:
            metadata["index_built_utc"] = stamped_utc
        # Every value in this table is JSON, and every reader loads it that way;
        # a bare string here parses as a syntax error in each of them.
        connection.executemany(
            "INSERT OR REPLACE INTO capture_metadata (key, value) VALUES (?, ?)",
            [(key, json.dumps(value)) for key, value in sorted(metadata.items())],
        )
        connection.commit()
        integrity = connection.execute("PRAGMA integrity_check").fetchone()[0]
        if integrity != "ok":
            raise RuntimeError(f"SQLite verification failed: {integrity!r}")
    finally:
        connection.close()

    return {
        "session": session.name,
        "session_path": str(session),
        "database": str(database),
        "source_database_sha256": source_digest,
        "index_version": INDEX_VERSION,
        "support": flags,
        "counts": counts,
        "rollup_verdict": verdict,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sessions", nargs="+")
    parser.add_argument("--rewrite", action="store_true")
    parser.add_argument("--stamped-utc", default=None)
    parser.add_argument("--report", type=Path)
    parser.add_argument(
        "--no-rollup",
        dest="rollup",
        action="store_false",
        help="build the spine without running the integrity verifiers",
    )
    parser.add_argument(
        "--no-source-join",
        dest="join_source",
        action="store_false",
        help="skip the census read of the installed game tree",
    )
    parser.add_argument(
        "--reports",
        type=Path,
        help=(
            "a JSON dump of already-computed verifier reports, keyed by tool, "
            "used instead of re-running them for the roll-up"
        ),
    )
    args = parser.parse_args()

    loaded: dict[str, Any] | None = None
    if args.reports:
        payload = json.loads(args.reports.read_text(encoding="utf-8"))
        loaded = payload.get("reports", payload)

    results = []
    for value in args.sessions:
        session = resolve_session(value)
        result = index(
            session,
            rewrite=args.rewrite,
            join_source=args.join_source,
            rollup=args.rollup,
            reports=loaded,
            stamped_utc=args.stamped_utc,
        )
        results.append(result)
        (session / "capture-index-build.json").write_text(
            json.dumps(result, indent=2) + "\n", encoding="utf-8"
        )
        for key, value_ in sorted(result["counts"].items()):
            print(f"  {key:28s} {value_}")
        print(f"  {'source sha256':28s} {result['source_database_sha256']}")
    if args.report:
        args.report.write_text(
            json.dumps({"sessions": results}, indent=2) + "\n", encoding="utf-8"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
