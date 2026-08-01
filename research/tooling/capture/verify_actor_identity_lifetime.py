"""Decide whether every skeletal actor the run posed was identified and bounded.

Usage:
    uv run elysium research verify_actor_identity_lifetime <session> [<session> ...]

A session is an absolute path or a directory name below
``$ELYSIUM_WORK_ROOT/research/retail-capture/theatre``.  Every database opens
read-only; nothing is written into an evidence file.  Each session receives an
``actor-identity-lifetime.json`` beside its database, and a combined report
carries the cross-run comparison.

The actor census keeps identity per sighting, the same split the model census
keeps: one observation names one skeletal client entity at one address, and a
second observation is emitted when that address starts evaluating a different
model.  This report answers CAP2.3: whether every entity an evaluation named was
observed at or before its first use, whether every reused address is separated by
an observed identity change, and whether any record is attributed to an address
outside the interval in which that address meant that actor.

Two bounds travel with this report and are stated rather than papered over.

Construction and destruction are **not witnessed**.  No hooked target sees a
client entity constructed or destructed, so the run reports the interval an
address demonstrably meant one actor, and counts residency at capture stop, in
place of a lifetime it cannot observe.  A real lifetime event needs a Ghidra
pass on the client entity list first.

There is **no targetname on the client**.  VtMB's targetname is server-side, so
the identity an actor record can carry is its address, the renderable subobject
the draw stream sees, and the model the evaluators resolved for it.  The verdict
says so rather than implying a name was captured.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sqlite3
import struct
from typing import Any

from research.tooling.capture.calibrate_theatre_capture import (
    DATABASE_NAME,
    actor_bytes_expression,
    artifact_key_values,
    open_database,
    record_bytes_expression,
    resolve_session,
    stream_headers,
    table_columns,
)
from research.tooling.capture.finalize_capture_database import (
    ACTOR_FILE_HEADER,
    ROOT_TRANSFORM_BYTES,
)


ACTOR_STREAM = "actor"
ANIMATION_STREAM = "animation"
OBSERVATION_FIRST = 1
OBSERVATION_IDENTITY_CHANGE = 2
OBSERVATION_RESIDENT_AT_STOP = 3
OBSERVATION_CONSTRUCT = 4
OBSERVATION_DESTRUCT = 5
OBSERVATION_NAMES = {
    OBSERVATION_FIRST: "first",
    OBSERVATION_IDENTITY_CHANGE: "identity_change",
    OBSERVATION_RESIDENT_AT_STOP: "resident_at_stop",
    OBSERVATION_CONSTRUCT: "construct",
    OBSERVATION_DESTRUCT: "destruct",
}
# The reasons that carry a model identity. A lifetime record names an address
# and a time, so it never joins on checksum.
IDENTITY_REASONS = (
    OBSERVATION_FIRST,
    OBSERVATION_IDENTITY_CHANGE,
    OBSERVATION_RESIDENT_AT_STOP,
)
# The stream version at which the construction and destruction targets were
# armed. Below it a run reports a bounded interval and no witnessed lifetime.
LIFETIME_STREAM_VERSION = 2
# The actor population is the entities the skeletal evaluators run on. A draw
# record names the renderable subobject instead, which is a different address in
# the same object, so mixing the two would compare an actor count to a draw one.
EVALUATION_KINDS = ("BASE", "FINL")
COMPOSED_POSE_KIND = "FINL"
DRAW_KIND = "POSE"
POSE_BUILD_KIND = "PBLD"
# CAP1.3's measured relation, restated here as a prediction rather than as an
# assumption: the probe records both addresses as observed, so this file reports
# the difference it finds instead of deriving one address from the other.
INSTANCE_DELTA = 4
# CAP1.2 and CAP1.3 measured 49 skeletal client entities in a complete run, of
# which 10 addresses served more than one model.
BASELINE_SKELETAL_ENTITIES = 49
BASELINE_REUSED_ENTITY_ADDRESSES = 10
# CAP2.2's measured baseline for one complete run carrying the model census.
BASELINE_RECORDS = 1_696_055
BASELINE_MEAN_MB_PER_SECOND = 8.35
BASELINE_PEAK_SECOND_MB = 22.0
BASELINE_QUEUE_HIGH_WATER = 141
# A root/entity transform is a rotation and a translation, so the 3x3 part has
# unit determinant. The band is far looser than float error, so a sample that
# fails it is failing because the bytes are not a transform at all.
DETERMINANT_TOLERANCE = 1e-3
MAX_REPORTED_FAULTS = 64
MAX_REPORTED_ACTORS = 200
MAX_SAMPLED_PLACEMENTS = 256


def address(value: int | None) -> str | None:
    return None if value is None else f"0x{value:08x}"


def prepare(connection: sqlite3.Connection) -> None:
    """Materialize what each skeletal address was used as, and when.

    ``records`` stores its payload blobs inline, so grouping it directly drags
    the whole capture off disk. One pass reduces it to one row per actor
    identity, which is the grain every question below asks at.
    """
    connection.execute(
        f"""
        CREATE TEMP TABLE actor_usage AS
        SELECT client_entity AS entity, checksum,
               max(bone_count) AS bone_count,
               min(qpc) AS first_qpc, max(qpc) AS last_qpc,
               min(generation) AS first_generation,
               max(generation) AS last_generation,
               count(*) AS records
        FROM records
        WHERE kind IN {EVALUATION_KINDS} AND client_entity IS NOT NULL
        GROUP BY client_entity, checksum
        """
    )
    connection.execute(
        "CREATE UNIQUE INDEX temp.actor_usage_identity "
        "ON actor_usage(entity, checksum)"
    )
    # The first observation of each identity, which is what "observed at or
    # before its first use" is measured against. The sweep at capture stop runs
    # after every evaluation and must not be allowed to satisfy that test.
    connection.execute(
        f"""
        CREATE TEMP TABLE actor_observed AS
        SELECT entity, checksum, min(qpc) AS first_qpc, count(*) AS records
        FROM actor_observations
        WHERE reason IN ({OBSERVATION_FIRST}, {OBSERVATION_IDENTITY_CHANGE})
        GROUP BY entity, checksum
        """
    )
    connection.execute(
        "CREATE UNIQUE INDEX temp.actor_observed_identity "
        "ON actor_observed(entity, checksum)"
    )
    # The window in which an address demonstrably meant one actor: from the
    # observation that opened it to the observation that gave the same address a
    # different model. An open interval is one that never changed identity.
    connection.execute(
        f"""
        CREATE TEMP TABLE actor_interval AS
        SELECT o.entity, o.checksum, o.first_qpc AS opened_qpc,
               (SELECT min(n.qpc) FROM actor_observations n
                 WHERE n.entity = o.entity
                   AND n.reason IN ({OBSERVATION_FIRST},
                                    {OBSERVATION_IDENTITY_CHANGE})
                   AND n.checksum != o.checksum
                   AND n.qpc > o.first_qpc) AS closed_qpc
        FROM actor_observed o
        """
    )
    connection.execute(
        "CREATE UNIQUE INDEX temp.actor_interval_identity "
        "ON actor_interval(entity, checksum)"
    )
    # The witnessed lifetime: from a construction to the next destruction at the
    # same address. An address constructed before the hooks armed has no row
    # here at all, which is why the checks below ask whether an entity has a
    # lifetime before asking whether a record falls inside one.
    #
    # Restricted to addresses the census observed, because destruction is
    # recorded only for those: a skeletal entity constructed and destroyed
    # without ever being posed would otherwise contribute a construction that
    # can never be paired and read as a lifetime still open at capture stop.
    connection.execute(
        f"""
        CREATE TEMP TABLE actor_life AS
        SELECT c.entity, c.qpc AS constructed_qpc,
               (SELECT min(d.qpc) FROM actor_observations d
                 WHERE d.entity = c.entity
                   AND d.reason = {OBSERVATION_DESTRUCT}
                   AND d.qpc > c.qpc) AS destructed_qpc
        FROM actor_observations c
        WHERE c.reason = {OBSERVATION_CONSTRUCT}
          AND c.entity IN (SELECT entity FROM actor_observed)
        """
    )
    connection.execute(
        "CREATE INDEX temp.actor_life_entity ON actor_life(entity)"
    )
    # The renderable subobject as observed, never as computed. A draw record
    # names that address, so this is the only bridge from an actor to its draws
    # that does not assume the offset the run is meant to be evidence for.
    connection.execute(
        """
        CREATE TEMP TABLE actor_renderable AS
        SELECT entity, min(renderable) AS renderable
        FROM actor_observations WHERE renderable != 0
        GROUP BY entity
        """
    )
    connection.execute(
        "CREATE UNIQUE INDEX temp.actor_renderable_entity "
        "ON actor_renderable(entity)"
    )


def capability(
    connection: sqlite3.Connection, headers: dict[str, dict[str, Any]]
) -> dict[str, Any]:
    """Report what this database carries at all.

    A CAP1, CAP2.1 or CAP2.2 database is a valid capture that predates the actor
    census, so it is answered rather than rejected: every later section reports
    null and the verdict says the run cannot be judged.
    """
    present = ACTOR_STREAM in headers
    slots = None
    if present:
        blob = connection.execute(
            "SELECT file_header FROM streams WHERE name = ?", (ACTOR_STREAM,)
        ).fetchone()
        if blob:
            slots = int(ACTOR_FILE_HEADER.unpack(blob[0])[7])
    animation_version = headers.get(ANIMATION_STREAM, {}).get("version", 0)
    actor_version = headers.get(ACTOR_STREAM, {}).get("version", 0)
    return {
        "stream_versions": {
            name: header.get("version") for name, header in headers.items()
        },
        "carries_actors": present,
        "carries_lifetime": actor_version >= LIFETIME_STREAM_VERSION,
        "carries_root_transform": animation_version >= 4
        and "root_transform_bytes" in table_columns(connection, "records"),
        "slot_count": slots,
    }


def coverage(connection: sqlite3.Connection) -> dict[str, Any]:
    """Count actor identities used against actor identities observed.

    This is CAP2.3's acceptance criterion; everything else explains it.
    """
    used, observed = connection.execute(
        "SELECT (SELECT count(*) FROM actor_usage), "
        "(SELECT count(*) FROM actor_observed)"
    ).fetchone()
    unobserved = [
        {
            "entity": address(entity),
            "checksum": address(checksum),
            "records": records,
            "first_qpc": first_qpc,
        }
        for entity, checksum, records, first_qpc in connection.execute(
            """
            SELECT u.entity, u.checksum, u.records, u.first_qpc
            FROM actor_usage u LEFT JOIN actor_observed o
              ON o.entity = u.entity AND o.checksum = u.checksum
            WHERE o.entity IS NULL
            ORDER BY u.records DESC LIMIT ?
            """,
            (MAX_REPORTED_FAULTS,),
        )
    ]
    late = [
        {
            "entity": address(entity),
            "checksum": address(checksum),
            "observed_qpc": observed_qpc,
            "first_use_qpc": first_use,
        }
        for entity, checksum, observed_qpc, first_use in connection.execute(
            """
            SELECT u.entity, u.checksum, o.first_qpc, u.first_qpc
            FROM actor_usage u JOIN actor_observed o
              ON o.entity = u.entity AND o.checksum = u.checksum
            WHERE o.first_qpc > u.first_qpc
            ORDER BY o.first_qpc - u.first_qpc DESC LIMIT ?
            """,
            (MAX_REPORTED_FAULTS,),
        )
    ]
    unobserved_total = connection.execute(
        """
        SELECT count(*) FROM actor_usage u LEFT JOIN actor_observed o
          ON o.entity = u.entity AND o.checksum = u.checksum
        WHERE o.entity IS NULL
        """
    ).fetchone()[0]
    late_total = connection.execute(
        """
        SELECT count(*) FROM actor_usage u JOIN actor_observed o
          ON o.entity = u.entity AND o.checksum = u.checksum
        WHERE o.first_qpc > u.first_qpc
        """
    ).fetchone()[0]
    # An observation for an identity no evaluation ever names is not a fault:
    # the sweep at capture stop produces one for every recorded actor. It is
    # reported so the two counts are never assumed equal.
    unused = connection.execute(
        """
        SELECT count(*) FROM actor_observed o LEFT JOIN actor_usage u
          ON u.entity = o.entity AND u.checksum = o.checksum
        WHERE u.entity IS NULL
        """
    ).fetchone()[0]
    return {
        "identities_used": used,
        "identities_observed": observed,
        "identities_unobserved": unobserved_total,
        "identities_observed_late": late_total,
        "identities_observed_but_unused": unused,
        "unobserved": unobserved,
        "observed_late": late,
        "complete": unobserved_total == 0 and late_total == 0 and used > 0,
    }


def identity(connection: sqlite3.Connection) -> dict[str, Any]:
    """The actor dictionary itself, and the address relation it observed."""
    observations = {
        OBSERVATION_NAMES.get(reason, str(reason)): count
        for reason, count in connection.execute(
            "SELECT reason, count(*) FROM actor_observations GROUP BY reason"
        )
    }
    # Restricted to the reasons that carry a model. Construction is recorded
    # from the shared base of the entity hierarchy, so counting every address in
    # this table would report the whole client entity population as actors.
    actors, checksums, models = connection.execute(
        f"""
        SELECT count(DISTINCT entity), count(DISTINCT checksum),
               count(DISTINCT model_name)
        FROM actor_observations WHERE reason IN {IDENTITY_REASONS}
        """
    ).fetchone()
    constructed_only = connection.execute(
        f"""
        SELECT count(*) FROM (
            SELECT DISTINCT entity FROM actor_observations
            WHERE reason NOT IN {IDENTITY_REASONS}
              AND entity NOT IN (
                  SELECT entity FROM actor_observations
                  WHERE reason IN {IDENTITY_REASONS})
        )
        """
    ).fetchone()[0]
    entries = [
        {
            "entity": address(entity),
            "renderable": address(renderable),
            "checksum": address(checksum),
            "bone_count": bone_count,
            "model_name": model_name,
            "observations": count,
        }
        for entity, renderable, checksum, bone_count, model_name, count in (
            connection.execute(
                f"""
                SELECT entity, min(renderable), checksum, max(bone_count),
                       model_name, count(*)
                FROM actor_observations WHERE reason IN {IDENTITY_REASONS}
                GROUP BY entity, checksum
                ORDER BY entity LIMIT ?
                """,
                (MAX_REPORTED_ACTORS,),
            )
        )
    ]
    # Both addresses are recorded as observed, so the difference between them is
    # a measurement. CAP1.3 derived the same number from model identity in the
    # other two streams; agreement across independent derivations is the point.
    deltas = {
        int(delta): count
        for delta, count in connection.execute(
            """
            SELECT renderable - entity, count(*)
            FROM actor_observations WHERE renderable != 0
            GROUP BY 1 ORDER BY 2 DESC
            """
        )
    }
    observed_deltas = sum(deltas.values())
    return {
        "distinct_actors": actors,
        "distinct_checksums": checksums,
        "distinct_model_names": models,
        # Addresses this run only ever saw constructed. They are client entities
        # that never posed, so they are not actors and are counted apart.
        "addresses_constructed_but_never_posed": constructed_only,
        "observations_by_reason": observations,
        "actors": entries,
        "actors_truncated": actors > MAX_REPORTED_ACTORS,
        "renderable_deltas": deltas,
        "observations_carrying_a_renderable": observed_deltas,
        "renderable_delta_is_constant": len(deltas) == 1 and observed_deltas > 0,
        "renderable_delta": next(iter(deltas)) if len(deltas) == 1 else None,
        "matches_cap1_3_delta": deltas.get(INSTANCE_DELTA, 0) == observed_deltas
        and observed_deltas > 0,
        "baseline_skeletal_entities": BASELINE_SKELETAL_ENTITIES,
        # Stated, not measured: the client has no targetname to capture.
        "targetname": (
            "not available — VtMB's targetname is server-side, so client "
            "identity is the address, the renderable subobject and the model"
        ),
    }


def lifetime(
    connection: sqlite3.Connection,
    support: dict[str, Any],
    done: dict[str, str],
) -> dict[str, Any]:
    """How long an address meant one actor, by two independent measures.

    The identity interval opens where an address claimed a model and closes
    where it claimed a different one; the witnessed lifetime opens at the
    constructor and closes at the destructor. An address alive before the hooks
    armed has an interval and no lifetime, which is reported rather than
    faulted — the probe arms before map load, but the run does not create the
    world.
    """
    intervals, closed = connection.execute(
        "SELECT count(*), count(closed_qpc) FROM actor_interval"
    ).fetchone()
    outside = connection.execute(
        f"""
        SELECT count(*) FROM records r JOIN actor_interval i
          ON i.entity = r.client_entity AND i.checksum = r.checksum
        WHERE r.kind IN {EVALUATION_KINDS}
          AND (r.qpc < i.opened_qpc
               OR (i.closed_qpc IS NOT NULL AND r.qpc >= i.closed_qpc))
        """
    ).fetchone()[0]
    outside_detail = [
        {
            "entity": address(entity),
            "checksum": address(checksum),
            "records": records,
            "opened_qpc": opened,
            "closed_qpc": closed_qpc,
        }
        for entity, checksum, records, opened, closed_qpc in connection.execute(
            f"""
            SELECT r.client_entity, r.checksum, count(*), i.opened_qpc,
                   i.closed_qpc
            FROM records r JOIN actor_interval i
              ON i.entity = r.client_entity AND i.checksum = r.checksum
            WHERE r.kind IN {EVALUATION_KINDS}
              AND (r.qpc < i.opened_qpc
                   OR (i.closed_qpc IS NOT NULL AND r.qpc >= i.closed_qpc))
            GROUP BY r.client_entity, r.checksum
            ORDER BY count(*) DESC LIMIT ?
            """,
            (MAX_REPORTED_FAULTS,),
        )
    ]
    report: dict[str, Any] = {
        "intervals": intervals,
        "intervals_closed_by_an_identity_change": closed,
        "intervals_open_at_capture_stop": intervals - closed,
        "records_outside_their_interval": outside,
        "outside": outside_detail,
        "resident_at_stop": int(done.get("actor_resident", "0")),
        "vanished_at_stop": int(done.get("actor_vanished", "0")),
        "bounded": outside == 0 and intervals > 0,
    }
    if not support["carries_lifetime"]:
        report.update(
            {
                "witnessed": False,
                "constructions": 0,
                "destructions": 0,
                "statement": (
                    "This run armed no construction or destruction target, so "
                    "the interval above is what an address demonstrably meant "
                    "and the residency at capture stop, not a lifetime."
                ),
            }
        )
        return report

    lifetimes, destructed = connection.execute(
        "SELECT count(*), count(destructed_qpc) FROM actor_life"
    ).fetchone()
    without = connection.execute(
        """
        SELECT count(*) FROM (
            SELECT DISTINCT entity FROM actor_usage
            WHERE entity NOT IN (SELECT entity FROM actor_life)
        )
        """
    ).fetchone()[0]
    outside_life = connection.execute(
        f"""
        SELECT count(*) FROM records r
        WHERE r.kind IN {EVALUATION_KINDS}
          AND EXISTS (SELECT 1 FROM actor_life l
                       WHERE l.entity = r.client_entity)
          AND NOT EXISTS (
              SELECT 1 FROM actor_life l
               WHERE l.entity = r.client_entity
                 AND r.qpc >= l.constructed_qpc
                 AND (l.destructed_qpc IS NULL
                      OR r.qpc < l.destructed_qpc))
        """
    ).fetchone()[0]
    # An address whose actor died and was rebuilt is the case a raw-address join
    # cannot see and an identity interval only catches when the model changed.
    recycled = connection.execute(
        """
        SELECT count(*) FROM (
            SELECT entity FROM actor_life GROUP BY entity HAVING count(*) > 1
        )
        """
    ).fetchone()[0]
    # Constructions for addresses the census never observed. These are skeletal
    # entities the run built but never posed, so no destruction was recorded for
    # them and they are counted here rather than pairing as open lifetimes.
    never_posed = connection.execute(
        f"""
        SELECT count(*) FROM actor_observations c
        WHERE c.reason = {OBSERVATION_CONSTRUCT}
          AND c.entity NOT IN (SELECT entity FROM actor_observed)
        """
    ).fetchone()[0]
    report.update(
        {
            "witnessed": True,
            "constructions_of_entities_never_posed": never_posed,
            "lifetimes": lifetimes,
            "lifetimes_closed_by_a_destruction": destructed,
            "lifetimes_open_at_capture_stop": lifetimes - destructed,
            "addresses_reused_across_a_destruction": recycled,
            "entities_without_a_construction": without,
            "records_outside_any_lifetime": outside_life,
            "constructions": int(done.get("actor_constructions", "0")),
            "destructions": int(done.get("actor_destructions", "0")),
            "statement": (
                "Construction is recorded from C_BaseAnimating's own "
                "constructor, so the population is the skeletal entities and "
                "not every client entity; destruction is recorded from "
                "C_BaseEntity's destructor and filtered to addresses the "
                "census already published. An entity alive before the hooks "
                "armed has no construction and is counted rather than faulted."
            ),
        }
    )
    report["bounded"] = report["bounded"] and outside_life == 0
    return report


def reuse(connection: sqlite3.Connection) -> dict[str, Any]:
    """Whether every address that served several models was seen changing.

    CAP1.3 left this open: ten entity addresses served more than one model in a
    complete run, so an address join was only valid inside a lifetime nothing
    recorded. An identity change that no observation witnessed is the fault this
    section looks for.
    """
    reused = connection.execute(
        """
        SELECT count(*) FROM (
            SELECT entity FROM actor_usage GROUP BY entity
            HAVING count(DISTINCT checksum) > 1
        )
        """
    ).fetchone()[0]
    extra = connection.execute(
        """
        SELECT COALESCE(sum(identities - 1), 0) FROM (
            SELECT count(DISTINCT checksum) AS identities
            FROM actor_usage GROUP BY entity HAVING count(DISTINCT checksum) > 1
        )
        """
    ).fetchone()[0]
    changes = connection.execute(
        "SELECT count(*) FROM actor_observations WHERE reason = ?",
        (OBSERVATION_IDENTITY_CHANGE,),
    ).fetchone()[0]
    unwitnessed = [
        {
            "entity": address(entity),
            "models": models,
            "identity_changes": changes_seen,
        }
        for entity, models, changes_seen in connection.execute(
            f"""
            SELECT u.entity, count(DISTINCT u.checksum),
                   (SELECT count(*) FROM actor_observations a
                     WHERE a.entity = u.entity
                       AND a.reason = {OBSERVATION_IDENTITY_CHANGE})
            FROM actor_usage u GROUP BY u.entity
            HAVING count(DISTINCT u.checksum) - 1 >
                   (SELECT count(*) FROM actor_observations a
                     WHERE a.entity = u.entity
                       AND a.reason = {OBSERVATION_IDENTITY_CHANGE})
            LIMIT ?
            """,
            (MAX_REPORTED_FAULTS,),
        )
    ]
    return {
        "reused_entity_addresses": reused,
        "extra_identities_at_reused_addresses": extra,
        "identity_change_observations": changes,
        "unwitnessed": unwitnessed,
        "changes_account_for_reuse": not unwitnessed,
        "baseline_reused_entity_addresses": BASELINE_REUSED_ENTITY_ADDRESSES,
    }


def _determinant(values: tuple[float, ...]) -> float:
    """The determinant of a row-major 3x4 transform's rotation part."""
    a, b, c = values[0], values[1], values[2]
    d, e, f = values[4], values[5], values[6]
    g, h, i = values[8], values[9], values[10]
    return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)


def placement(
    connection: sqlite3.Connection, support: dict[str, Any]
) -> dict[str, Any]:
    """The root/entity transform the composed-pose stage receives.

    The stage's third argument is the frame the pose is placed into, and nothing
    else on the skeletal path carries it. It is checked rather than trusted: the
    rotation part of a transform has unit determinant, so a sample whose
    determinant is anything else is not a transform.
    """
    if not support["carries_root_transform"]:
        return {
            "available": False,
            "reason": "this database predates the root-transform trailer",
        }
    composed, carrying = connection.execute(
        f"""
        SELECT count(*), COALESCE(sum(root_transform_bytes > 0), 0)
        FROM records WHERE kind = '{COMPOSED_POSE_KIND}'
        """
    ).fetchone()
    stray = connection.execute(
        f"""
        SELECT count(*) FROM records
        WHERE kind != '{COMPOSED_POSE_KIND}'
          AND COALESCE(root_transform_bytes, 0) != 0
        """
    ).fetchone()[0]
    layout = struct.Struct("<12f")
    sampled = 0
    worst = 0.0
    unsound: list[dict[str, Any]] = []
    translations: list[list[float]] = []
    for entity, payload, root_bytes in connection.execute(
        f"""
        SELECT client_entity, raw_payload, root_transform_bytes
        FROM records
        WHERE kind = '{COMPOSED_POSE_KIND}' AND root_transform_bytes > 0
        ORDER BY id LIMIT ?
        """,
        (MAX_SAMPLED_PLACEMENTS,),
    ):
        if root_bytes != ROOT_TRANSFORM_BYTES or len(payload) < root_bytes:
            unsound.append(
                {"entity": address(entity), "reason": "trailer is not 48 bytes"}
            )
            continue
        values = layout.unpack(payload[-ROOT_TRANSFORM_BYTES:])
        error = abs(_determinant(values) - 1.0)
        sampled += 1
        worst = max(worst, error)
        if error > DETERMINANT_TOLERANCE:
            unsound.append(
                {"entity": address(entity), "determinant_error": error}
            )
        translations.append([values[3], values[7], values[11]])
    extent = None
    if translations:
        extent = [
            [min(row[axis] for row in translations) for axis in range(3)],
            [max(row[axis] for row in translations) for axis in range(3)],
        ]
    return {
        "available": True,
        "composed_poses": composed,
        "composed_poses_carrying_a_transform": carrying,
        "every_composed_pose_carries_one": composed > 0 and composed == carrying,
        "other_kinds_carrying_a_transform": stray,
        "sampled": sampled,
        "worst_determinant_error": worst,
        "unsound": unsound[:MAX_REPORTED_FAULTS],
        "translation_extent": extent,
        # A run that composed no pose has nothing to check, which is not the
        # same as a trailer that failed the check.
        "checked": sampled > 0,
        "sound": not unsound,
        # The entity's own origin field is not captured, so this says the bytes
        # are a transform and where it puts the actor. Whether it equals the
        # entity origin needs a pinned displacement and is not claimed here.
        "statement": (
            "The trailer decodes as a rotation and a translation. Whether that "
            "translation is the entity's own origin field is not decided here; "
            "no displacement for it is pinned, so CAP4.3 normalizes against "
            "the captured frame rather than against a claimed origin."
        ),
    }


def draw_outcome(connection: sqlite3.Connection) -> dict[str, Any]:
    """What became of each actor's pose: was it built, and was it drawn.

    The bridge is the observed renderable, so a draw is attributed to an actor
    by an address the probe recorded rather than by an offset applied here.
    """
    rows = [
        {
            "entity": address(entity),
            "renderable": address(renderable),
            "evaluations": evaluations,
            "pose_builds": builds,
            "draws": draws,
        }
        for entity, renderable, evaluations, builds, draws in (
            connection.execute(
                f"""
                SELECT a.entity, a.renderable,
                       (SELECT count(*) FROM records r
                         WHERE r.client_entity = a.entity
                           AND r.kind IN {EVALUATION_KINDS}),
                       (SELECT count(*) FROM records r
                         WHERE r.client_entity = a.renderable
                           AND r.kind = '{POSE_BUILD_KIND}'),
                       (SELECT count(*) FROM records r
                         WHERE r.client_entity = a.renderable
                           AND r.kind = '{DRAW_KIND}')
                FROM actor_renderable a ORDER BY a.entity LIMIT ?
                """,
                (MAX_REPORTED_ACTORS,),
            )
        )
    ]
    drawn = sum(1 for row in rows if row["draws"])
    posed = sum(1 for row in rows if row["pose_builds"])
    return {
        "actors": rows,
        "actors_truncated": len(rows) >= MAX_REPORTED_ACTORS,
        "actors_with_a_renderable": len(rows),
        "actors_drawn": drawn,
        "actors_never_drawn": len(rows) - drawn,
        "actors_with_a_pose_build": posed,
        # A skeletal actor need not be drawn under the model it animates under,
        # and an actor may animate without ever reaching a draw. Both are engine
        # behaviour CAP1.3 measured, so neither is reported as a fault.
        "statement": (
            "An actor that evaluated without being drawn is reported, not "
            "faulted: CAP1.3 measured that a skeletal actor need not be drawn "
            "under the model it animates under."
        ),
    }


def overhead(
    connection: sqlite3.Connection,
    headers: dict[str, dict[str, Any]],
    done: dict[str, str],
) -> dict[str, Any]:
    """Report what the actor census costs against CAP2.2's baseline."""
    record_bytes = record_bytes_expression(headers)
    frequency = next(iter(headers.values()))["qpc_frequency"]
    first, last, records, payload = connection.execute(
        f"SELECT min(qpc), max(qpc), count(*), sum({record_bytes}) FROM records"
    ).fetchone()
    span = (
        (last - first) / frequency
        if first is not None and last > first
        else None
    )
    actor_records, actor_payload = connection.execute(
        f"SELECT count(*), COALESCE(sum(bytes), 0) "
        f"FROM ({actor_bytes_expression()})"
    ).fetchone()
    total_bytes = (payload or 0) + actor_payload
    peak = max(
        (
            second_bytes
            for _, second_bytes in connection.execute(
                f"""
                SELECT (qpc - ?) / ?, sum({record_bytes})
                FROM records GROUP BY 1
                """,
                (first, frequency),
            )
        ),
        default=0,
    )
    return {
        "span_seconds": round(span, 3) if span else None,
        "event_records": records,
        "actor_records": actor_records,
        "actor_megabytes": round(actor_payload / 1_000_000, 3),
        "actor_byte_share": (
            round(actor_payload / total_bytes, 6) if total_bytes else None
        ),
        "mean_megabytes_per_second": (
            round(total_bytes / span / 1_000_000, 2) if span else None
        ),
        "peak_second_megabytes": round(peak / 1_000_000, 2),
        "queue_peak": int(done.get("queue_peak", "0")),
        "actor_overflow": int(done.get("actor_overflow", "0")),
        "actor_faults": int(done.get("actor_faults", "0")),
        "actor_bytes": int(done.get("actor_bytes", "0")),
        "baseline": {
            "records": BASELINE_RECORDS,
            "mean_megabytes_per_second": BASELINE_MEAN_MB_PER_SECOND,
            "peak_second_megabytes": BASELINE_PEAK_SECOND_MB,
            "queue_peak": BASELINE_QUEUE_HIGH_WATER,
        },
    }


def decide(
    support: dict[str, Any],
    counts: dict[str, Any],
    named: dict[str, Any],
    lived: dict[str, Any],
    reused: dict[str, Any],
    placed: dict[str, Any],
    cost: dict[str, Any],
) -> dict[str, Any]:
    verdict: dict[str, Any] = {
        "identity_complete": False,
        "lifetime_bounded": False,
        "lifetime_witnessed": False,
        "placement": "unavailable",
    }
    if not support["carries_actors"]:
        verdict["statement"] = (
            "This database predates the actor census "
            f"({support['stream_versions']}), so no skeletal client entity was "
            "recorded and CAP2.3 cannot be judged from it."
        )
        return verdict
    if not counts["complete"]:
        verdict["statement"] = (
            f"{counts['identities_unobserved']} of {counts['identities_used']} "
            "actor identities were evaluated without ever being observed and "
            f"{counts['identities_observed_late']} were observed after their "
            "first use, so the run does not meet CAP2.3's acceptance. The "
            "missing identities are named rather than inferred from a nearby "
            "address."
        )
        return verdict
    if cost and cost.get("actor_overflow"):
        verdict["statement"] = (
            f"Every evaluated identity was observed, but {cost['actor_overflow']}"
            " actors were refused because the actor table was full, so the "
            "census is complete only over what the table could hold."
        )
        return verdict
    if not reused["changes_account_for_reuse"]:
        verdict["statement"] = (
            f"{reused['extra_identities_at_reused_addresses']} extra identities "
            f"appear at {reused['reused_entity_addresses']} reused addresses but "
            f"{reused['identity_change_observations']} identity changes were "
            "observed, so an address changed actor without the census seeing "
            "it and a join on that address is still unbounded."
        )
        return verdict
    verdict["identity_complete"] = True
    if not lived["bounded"]:
        outside_life = lived.get("records_outside_any_lifetime", 0)
        verdict["statement"] = (
            f"{lived['records_outside_their_interval']} evaluation records fall "
            "outside the interval in which their address meant the model they "
            f"name and {outside_life} fall outside any witnessed lifetime of "
            "that address, so the interval does not bound the join it is "
            "supposed to bound."
        )
        return verdict
    verdict["lifetime_bounded"] = True
    verdict["lifetime_witnessed"] = bool(lived.get("witnessed"))
    if placed.get("available"):
        verdict["placement"] = (
            "unsound"
            if not placed["sound"]
            else "sound"
            if placed["checked"]
            else "unexercised"
        )
    if placed.get("available") and not placed["sound"]:
        verdict["statement"] = (
            f"{len(placed['unsound'])} of {placed['sampled']} sampled root "
            "transforms do not decode as a rotation and a translation, so the "
            "retained trailer is not the frame it is claimed to be."
        )
        return verdict
    delta = (
        f"a constant +{named['renderable_delta']}"
        if named["renderable_delta_is_constant"]
        else f"{len(named['renderable_deltas'])} different values"
    )
    closing = (
        f"{lived['constructions']} constructions and {lived['destructions']} "
        "destructions were recorded, "
        f"{lived['addresses_reused_across_a_destruction']} addresses were "
        "rebuilt into a new actor, and "
        f"{lived['entities_without_a_construction']} entities were already "
        "alive when the hooks armed."
        if lived.get("witnessed")
        else (
            "Construction and destruction remain unwitnessed: this is a "
            "bounded interval, not a lifetime."
        )
    )
    verdict["statement"] = (
        f"All {counts['identities_used']} actor identities were observed at or "
        f"before first use across {named['distinct_actors']} addresses, "
        f"{reused['identity_change_observations']} identity changes account for "
        f"every one of {reused['reused_entity_addresses']} reused addresses, "
        f"and no record falls outside its interval, so an address join is "
        f"bounded. The observed renderable sits {delta} above the entity, which "
        f"is CAP1.3's relation measured a fourth way. {closing}"
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
        support = capability(connection, headers)
        done = artifact_key_values(connection, session, "done.txt")

        if not support["carries_actors"]:
            counts = named = lived = reused = placed = drawn = cost = None
            verdict = decide(support, {}, {}, {}, {}, {}, {})
        else:
            prepare(connection)
            counts = coverage(connection)
            named = identity(connection)
            lived = lifetime(connection, support, done)
            reused = reuse(connection)
            placed = placement(connection, support)
            drawn = draw_outcome(connection)
            cost = overhead(connection, headers, done)
            verdict = decide(
                support, counts, named, lived, reused, placed, cost
            )

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
            "actors": named,
            "lifetime": lived,
            "reuse": reused,
            "placement": placed,
            "draw_outcome": drawn,
            "overhead": cost,
            "verdict": verdict,
        }
    finally:
        connection.close()


def compare(reports: list[dict[str, Any]]) -> dict[str, Any]:
    rows = []
    for report in reports:
        named = report["actors"] or {}
        rows.append(
            {
                "session": report["session"],
                "identities_used": (report["coverage"] or {}).get(
                    "identities_used"
                ),
                "unobserved": (report["coverage"] or {}).get(
                    "identities_unobserved"
                ),
                "distinct_actors": named.get("distinct_actors"),
                "reused_addresses": (report["reuse"] or {}).get(
                    "reused_entity_addresses"
                ),
                "identity_complete": report["verdict"]["identity_complete"],
                "lifetime_bounded": report["verdict"]["lifetime_bounded"],
                "renderable_delta": named.get("renderable_delta"),
                "addresses": sorted(
                    entry["entity"] for entry in named.get("actors", [])
                ),
            }
        )
    bounded = [
        row for row in rows if row["identity_complete"] and row["lifetime_bounded"]
    ]
    deltas = {row["renderable_delta"] for row in rows}
    shared_addresses = (
        set.intersection(*(set(row["addresses"]) for row in rows))
        if len(rows) > 1
        else set()
    )
    if len(bounded) != len(rows):
        statement = (
            f"{len(rows) - len(bounded)} of {len(rows)} runs leave an actor "
            "identity unobserved or a record outside its interval, so actor "
            "identity is not yet established across runs."
        )
    elif len(deltas) != 1:
        statement = (
            f"All {len(rows)} runs bound every actor they posed, but the runs "
            f"disagree on the renderable offset ({sorted(deltas)}), so the "
            "relation is not a fixed offset inside the object."
        )
    else:
        statement = (
            f"All {len(rows)} runs observed every actor they posed and agree on "
            f"a renderable offset of +{next(iter(deltas))} while sharing "
            f"{len(shared_addresses)} actor addresses, so the census names the "
            "actors rather than the heap they happened to land in."
        )
    return {
        "sessions": [report["session"] for report in reports],
        "runs": rows,
        "shared_addresses": len(shared_addresses),
        "statement": statement,
    }


def summarize(report: dict[str, Any]) -> str:
    lines = [f"{report['session']}"]
    counts = report["coverage"]
    if counts is None:
        lines.append(f"  support    {report['verdict']['statement']}")
        return "\n".join(lines)
    lines.append(
        f"  coverage   {counts['identities_observed']}/"
        f"{counts['identities_used']} used actor identities observed; "
        f"{counts['identities_unobserved']} unobserved, "
        f"{counts['identities_observed_late']} late"
    )
    named = report["actors"]
    lines.append(
        f"  actors     {named['distinct_actors']} addresses "
        f"(+{named['addresses_constructed_but_never_posed']} constructed but "
        f"never posed), {named['distinct_checksums']} models, "
        f"{named['distinct_model_names']} names; renderable delta "
        + (
            f"+{named['renderable_delta']} constant"
            if named["renderable_delta_is_constant"]
            else f"{sorted(named['renderable_deltas'])}"
        )
    )
    lived = report["lifetime"]
    lines.append(
        f"  intervals  {lived['intervals']} "
        f"({lived['intervals_open_at_capture_stop']} open at stop), "
        f"{lived['records_outside_their_interval']} records outside theirs; "
        f"{lived['resident_at_stop']} resident, {lived['vanished_at_stop']} "
        "vanished"
    )
    lines.append(
        "  lifetime   "
        + (
            f"{lived['constructions']} constructions, "
            f"{lived['destructions']} destructions, "
            f"{lived['addresses_reused_across_a_destruction']} addresses "
            f"rebuilt, {lived['entities_without_a_construction']} pre-existing;"
            f" {lived['records_outside_any_lifetime']} records outside any"
            if lived.get("witnessed")
            else "not witnessed — no construction or destruction target armed"
        )
    )
    reused = report["reuse"]
    lines.append(
        f"  reuse      {reused['reused_entity_addresses']} addresses served "
        f"several models (CAP1.3 baseline "
        f"{reused['baseline_reused_entity_addresses']}), "
        f"{reused['identity_change_observations']} identity changes observed"
    )
    placed = report["placement"]
    lines.append(
        "  placement  "
        + (
            f"{placed['composed_poses_carrying_a_transform']}/"
            f"{placed['composed_poses']} composed poses carry a transform; "
            f"{placed['sampled']} sampled, worst determinant error "
            f"{placed['worst_determinant_error']:.3g}"
            if placed.get("available")
            else f"unavailable — {placed.get('reason')}"
        )
    )
    drawn = report["draw_outcome"]
    lines.append(
        f"  draws      {drawn['actors_drawn']}/"
        f"{drawn['actors_with_a_renderable']} actors reached a draw, "
        f"{drawn['actors_with_a_pose_build']} reached a pose build"
    )
    cost = report["overhead"]
    lines.append(
        f"  overhead   {cost['actor_megabytes']} MB actors "
        f"({cost['actor_byte_share']} of bytes), "
        f"{cost['mean_megabytes_per_second']} MB/s mean "
        f"(baseline {cost['baseline']['mean_megabytes_per_second']}), "
        f"queue peak {cost['queue_peak']} "
        f"(baseline {cost['baseline']['queue_peak']})"
    )
    verdict = report["verdict"]
    lines.append(
        "  verdict    "
        + (
            "ACTORS BOUNDED"
            if verdict["identity_complete"] and verdict["lifetime_bounded"]
            else "IDENTIFIED, UNBOUNDED"
            if verdict["identity_complete"]
            else "INCOMPLETE"
        )
        + f" — placement {verdict['placement']}, lifetime "
        + ("witnessed" if verdict["lifetime_witnessed"] else "not witnessed")
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
        help="Do not write actor-identity-lifetime.json beside each database.",
    )
    args = parser.parse_args()

    reports = []
    for value in args.sessions:
        session = resolve_session(value)
        report = verify(session)
        reports.append(report)
        if not args.no_session_reports:
            (session / "actor-identity-lifetime.json").write_text(
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
        report["verdict"]["identity_complete"]
        and report["verdict"]["lifetime_bounded"]
        for report in reports
    ) else 1


if __name__ == "__main__":
    raise SystemExit(main())
