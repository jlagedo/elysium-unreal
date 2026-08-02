"""Decide whether every runtime studio header the run used was censused once.

Usage:
    uv run elysium research verify_model_skeleton_census <session> [<session> ...]

A session is an absolute path or a directory name below
``$ELYSIUM_WORK_ROOT/research/retail-capture/theatre``.  Every database opens
read-only; nothing is written into an evidence file.  Each session receives a
``model-skeleton-census.json`` beside its database, and a combined report
carries the cross-run comparison.

The census keeps identity per sighting and bytes once per checksum.  An
observation names one studio header at one address; a model image is the bytes
that header sits at the front of, stored under its checksum however many
addresses served it.  This report answers CAP2.2: whether every header a draw
or an evaluation used was observed at or before its first use, whether the
image behind it decodes as a skeleton, and whether those bytes are the
installed model's bytes.

Nothing here decodes in the probe.  The bone array is read offline by the same
pipeline decoder the export uses, so a wrong field hypothesis costs a re-run of
this script rather than a re-run of the game.

Unload is not claimed.  No hooked target sees a model-cache free, so the sweep
at capture stop reports residency instead: a header that still reads as its own
studio header was never unloaded, and one that no longer does is counted and
named rather than presented as an unload event.
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
    artifact_key_values,
    census_bytes_expression,
    compared_maps,
    open_database,
    record_bytes_expression,
    resolve_session,
    stream_headers,
    table_columns,
)
from research.tooling.capture.finalize_capture_database import (
    CENSUS_FILE_HEADER,
)


CENSUS_STREAM = "census"
OBSERVATION_FIRST = 1
OBSERVATION_REPLACEMENT = 2
OBSERVATION_RESIDENT_AT_STOP = 3
OBSERVATION_NAMES = {
    OBSERVATION_FIRST: "first",
    OBSERVATION_REPLACEMENT: "replacement",
    OBSERVATION_RESIDENT_AT_STOP: "resident_at_stop",
}
# The draw record's model-name field is 64 bytes of the header's own 128, so a
# path at or beyond the limit reaches the database with no terminator and no
# flag. The census carries the full field, which is what makes the loss
# measurable rather than invisible.
POSE_NAME_BYTES = 64
BONE_STRIDE = 160
MODEL_GROUP_STRIDE = 116
BONE_FLAG_SPLIT_INHERITANCE = 0x2
# docs/vtmb/mdl_v2531.md records every shipped bind quaternion normalized within
# 9.24e-8 and every conventional inverse bind within 7.36e-6 of identity. The
# bands here are looser than both, so a captured skeleton that fails one is
# failing by orders of magnitude rather than by rounding.
QUATERNION_NORM_TOLERANCE = 1e-5
INVERSE_BIND_TOLERANCE = 1e-3
# CAP1.3 measured ten *client entity* addresses serving more than one model
# checksum in a complete run. That is a different population from the studio
# headers this census keys on — an entity is an actor, a studio header is a
# loaded model — so it is reported beside the header figure rather than as its
# baseline, and conflating the two would compare an actor count to a model one.
BASELINE_REUSED_ENTITY_ADDRESSES = 10
# CAP2.1's measured baseline for one complete bracketed run.
BASELINE_RECORDS = 1_693_202
BASELINE_MEAN_MB_PER_SECOND = 8.3
BASELINE_PEAK_SECOND_MB = 22.1
BASELINE_QUEUE_HIGH_WATER = 178
MAX_REPORTED_FAULTS = 64
MAX_REPORTED_HEADERS = 400
MAX_REPORTED_SPANS = 32


def install_key(model_name: str) -> str:
    """A captured studio header name as the install index is keyed.

    Some shipped names are absolute (`/items/rings/Ground/Ring01.mdl`) and none
    carries the `models/` prefix the index reads, so a name reaches the install
    only after both are reconciled.
    """
    key = model_name.replace("\\", "/").lower().lstrip("/")
    return key if key.startswith("models/") else "models/" + key


def address(value: int | None) -> str | None:
    return None if value is None else f"0x{value:08x}"


def prepare(connection: sqlite3.Connection) -> None:
    """Materialize what each studio header was actually used for.

    ``records`` stores its payload blobs inline, so grouping it directly drags
    the whole capture off disk. One pass reduces it to one row per header
    identity, which is the grain every question below asks at.
    """
    connection.execute(
        """
        CREATE TEMP TABLE usage AS
        SELECT studio_hdr, checksum, max(bone_count) AS bone_count,
               min(qpc) AS first_qpc, max(qpc) AS last_qpc,
               count(*) AS records
        FROM records WHERE studio_hdr IS NOT NULL
        GROUP BY studio_hdr, checksum
        """
    )
    connection.execute(
        "CREATE UNIQUE INDEX temp.usage_identity ON usage(studio_hdr, checksum)"
    )
    # The first observation of each identity, which is what "observed at or
    # before its first use" is measured against. The residency sweep runs at
    # capture stop and must not be allowed to satisfy that test.
    connection.execute(
        f"""
        CREATE TEMP TABLE observed AS
        SELECT studio_hdr, checksum, min(qpc) AS first_qpc, count(*) AS records
        FROM model_headers WHERE reason != {OBSERVATION_RESIDENT_AT_STOP}
        GROUP BY studio_hdr, checksum
        """
    )
    connection.execute(
        "CREATE UNIQUE INDEX temp.observed_identity "
        "ON observed(studio_hdr, checksum)"
    )


def capability(
    connection: sqlite3.Connection, headers: dict[str, dict[str, Any]]
) -> dict[str, Any]:
    """Report whether this database carries a census at all.

    A CAP1 or CAP2.1 database is a valid capture that predates the census, so
    it is answered rather than rejected: every later section reports null and
    the verdict says the run cannot be judged.
    """
    present = CENSUS_STREAM in headers
    cap = None
    if present:
        blob = connection.execute(
            "SELECT file_header FROM streams WHERE name = ?", (CENSUS_STREAM,)
        ).fetchone()
        if blob:
            cap = int(CENSUS_FILE_HEADER.unpack(blob[0])[8])
    return {
        "stream_versions": {
            name: header.get("version") for name, header in headers.items()
        },
        "carries_census": present,
        "image_cap_bytes": cap,
    }


def coverage(connection: sqlite3.Connection) -> dict[str, Any]:
    """Count header identities used against header identities observed.

    This is CAP2.2's acceptance criterion; everything else explains it.
    """
    used, observed = connection.execute(
        "SELECT (SELECT count(*) FROM usage), (SELECT count(*) FROM observed)"
    ).fetchone()
    unobserved = [
        {
            "studio_hdr": address(studio_hdr),
            "checksum": address(checksum),
            "records": records,
            "first_qpc": first_qpc,
        }
        for studio_hdr, checksum, records, first_qpc in connection.execute(
            """
            SELECT u.studio_hdr, u.checksum, u.records, u.first_qpc
            FROM usage u LEFT JOIN observed o
              ON o.studio_hdr = u.studio_hdr AND o.checksum = u.checksum
            WHERE o.studio_hdr IS NULL
            ORDER BY u.records DESC LIMIT ?
            """,
            (MAX_REPORTED_FAULTS,),
        )
    ]
    late = [
        {
            "studio_hdr": address(studio_hdr),
            "checksum": address(checksum),
            "observed_qpc": observed_qpc,
            "first_use_qpc": first_use,
        }
        for studio_hdr, checksum, observed_qpc, first_use in connection.execute(
            """
            SELECT u.studio_hdr, u.checksum, o.first_qpc, u.first_qpc
            FROM usage u JOIN observed o
              ON o.studio_hdr = u.studio_hdr AND o.checksum = u.checksum
            WHERE o.first_qpc > u.first_qpc
            ORDER BY o.first_qpc - u.first_qpc DESC LIMIT ?
            """,
            (MAX_REPORTED_FAULTS,),
        )
    ]
    unobserved_total = connection.execute(
        """
        SELECT count(*) FROM usage u LEFT JOIN observed o
          ON o.studio_hdr = u.studio_hdr AND o.checksum = u.checksum
        WHERE o.studio_hdr IS NULL
        """
    ).fetchone()[0]
    late_total = connection.execute(
        """
        SELECT count(*) FROM usage u JOIN observed o
          ON o.studio_hdr = u.studio_hdr AND o.checksum = u.checksum
        WHERE o.first_qpc > u.first_qpc
        """
    ).fetchone()[0]
    # An observation for an identity no event record ever names is not a fault:
    # the residency sweep and a header seen only through a filtered call both
    # produce one. It is reported so the two counts are never assumed equal.
    unused = connection.execute(
        """
        SELECT count(*) FROM observed o LEFT JOIN usage u
          ON u.studio_hdr = o.studio_hdr AND u.checksum = o.checksum
        WHERE u.studio_hdr IS NULL
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


def census(connection: sqlite3.Connection) -> dict[str, Any]:
    """The header dictionary itself: what was observed, and how it was keyed."""
    per_reason = {
        OBSERVATION_NAMES.get(reason, str(reason)): count
        for reason, count in connection.execute(
            "SELECT reason, count(*) FROM model_headers GROUP BY reason"
        )
    }
    distinct_headers, distinct_checksums, distinct_names = connection.execute(
        """
        SELECT count(DISTINCT studio_hdr), count(DISTINCT checksum),
               count(DISTINCT model_name)
        FROM model_headers
        """
    ).fetchone()
    images, image_checksums, image_bytes, capped = connection.execute(
        """
        SELECT count(*), count(DISTINCT checksum), sum(captured_bytes),
               sum(capped)
        FROM model_images
        """
    ).fetchone()
    bone_histogram = {
        str(bone_count): count
        for bone_count, count in connection.execute(
            """
            SELECT bone_count, count(DISTINCT checksum) FROM model_headers
            GROUP BY bone_count ORDER BY bone_count
            """
        )
    }
    entries = [
        {
            "studio_hdr": address(studio_hdr),
            "checksum": address(checksum),
            "model_name": name,
            "bone_count": bones,
            "model_length": length,
            "include_models": includes,
            "reason": OBSERVATION_NAMES.get(reason, str(reason)),
            "image_captured": bool(captured),
            "qpc": qpc,
        }
        for (
            studio_hdr,
            checksum,
            name,
            bones,
            length,
            includes,
            reason,
            captured,
            qpc,
        ) in connection.execute(
            """
            SELECT studio_hdr, checksum, model_name, bone_count, model_length,
                   include_model_count, reason, image_captured, qpc
            FROM model_headers ORDER BY qpc LIMIT ?
            """,
            (MAX_REPORTED_HEADERS,),
        )
    ]
    return {
        "observations": sum(per_reason.values()),
        "observations_by_reason": per_reason,
        "distinct_studio_headers": distinct_headers,
        "distinct_checksums": distinct_checksums,
        "distinct_model_names": distinct_names,
        "images": images,
        "image_checksums": image_checksums,
        "image_bytes": image_bytes or 0,
        "images_capped": capped or 0,
        "bone_count_by_checksum": bone_histogram,
        # The identity sets stay complete even when the per-entry table is
        # capped, because the cross-run comparison is made from them.
        "checksums": [
            address(checksum)
            for checksum, in connection.execute(
                "SELECT DISTINCT checksum FROM model_headers ORDER BY checksum"
            )
        ],
        "addresses": [
            address(studio_hdr)
            for studio_hdr, in connection.execute(
                "SELECT DISTINCT studio_hdr FROM model_headers "
                "ORDER BY studio_hdr"
            )
        ],
        "entries": entries,
        "entries_truncated": len(entries) >= MAX_REPORTED_HEADERS,
        # One image per checksum is what makes immutable source bytes a
        # dictionary rather than a stream; more than one is a dedup failure.
        "one_image_per_checksum": images == image_checksums,
        "every_checksum_has_an_image": (
            distinct_checksums == image_checksums and distinct_checksums > 0
        ),
    }


def reuse(connection: sqlite3.Connection) -> dict[str, Any]:
    """Addresses that served more than one model, from the join rather than a counter.

    CAP1.2's census already produces per-identity record counts, so reuse is
    read out of the same grouping instead of being counted a second time in the
    probe.
    """
    reused = [
        {
            "studio_hdr": address(studio_hdr),
            "checksums": checksums,
        }
        for studio_hdr, checksums in connection.execute(
            """
            SELECT studio_hdr, count(DISTINCT checksum) FROM usage
            GROUP BY studio_hdr HAVING count(DISTINCT checksum) > 1
            ORDER BY count(DISTINCT checksum) DESC LIMIT ?
            """,
            (MAX_REPORTED_FAULTS,),
        )
    ]
    reused_total = connection.execute(
        """
        SELECT count(*) FROM (
            SELECT studio_hdr FROM usage GROUP BY studio_hdr
            HAVING count(DISTINCT checksum) > 1
        )
        """
    ).fetchone()[0]
    replacements = connection.execute(
        "SELECT count(*) FROM model_headers WHERE reason = ?",
        (OBSERVATION_REPLACEMENT,),
    ).fetchone()[0]
    # A pointer that served three checksums produces two replacements, so the
    # comparable total is the extra identities beyond the first at each address.
    extra_identities = connection.execute(
        """
        SELECT coalesce(sum(extra), 0) FROM (
            SELECT count(DISTINCT checksum) - 1 AS extra FROM usage
            GROUP BY studio_hdr
        )
        """
    ).fetchone()[0]
    # The entity population CAP1.3 measured, so the two reuse figures can be
    # compared to their own baselines instead of to each other.
    reused_entities = connection.execute(
        """
        SELECT count(*) FROM (
            SELECT client_entity FROM records
            WHERE client_entity IS NOT NULL AND checksum IS NOT NULL
            GROUP BY client_entity HAVING count(DISTINCT checksum) > 1
        )
        """
    ).fetchone()[0]
    return {
        "reused_header_addresses": reused_total,
        "extra_identities_at_reused_addresses": extra_identities,
        "replacement_observations": replacements,
        "reused": reused,
        "reused_entity_addresses": reused_entities,
        "baseline_reused_entity_addresses": BASELINE_REUSED_ENTITY_ADDRESSES,
        "replacements_account_for_reuse": replacements == extra_identities,
    }


def residency(connection: sqlite3.Connection, done: dict[str, str]) -> dict[str, Any]:
    """What the sweep at capture stop could say in place of an unload event."""
    published = connection.execute(
        "SELECT count(DISTINCT studio_hdr) FROM model_headers"
    ).fetchone()[0]
    resident = connection.execute(
        "SELECT count(*) FROM model_headers WHERE reason = ?",
        (OBSERVATION_RESIDENT_AT_STOP,),
    ).fetchone()[0]
    vanished = int(done.get("census_vanished", "0")) if done else 0
    changed = connection.execute(
        """
        SELECT count(*) FROM model_headers
        WHERE reason = ? AND previous_checksum != 0
          AND previous_checksum != checksum
        """,
        (OBSERVATION_RESIDENT_AT_STOP,),
    ).fetchone()[0]
    return {
        "published_addresses": published,
        "resident_at_stop": resident,
        "vanished_at_stop": vanished,
        "changed_identity_by_stop": changed,
        "accounted": resident + vanished == published,
        "statement": (
            f"{resident} of {published} recorded addresses still read as their "
            f"own studio header at capture stop and {vanished} did not. No "
            "hooked target sees a model-cache free, so this is residency, not "
            "an unload event; the cache-unload target is undeclared in every "
            "case specification and a Ghidra pass is its prerequisite."
        ),
    }


def _quaternion_matrix(
    quat: tuple[float, ...], pos: tuple[float, ...]
) -> list[float]:
    x, y, z, w = quat
    return [
        1.0 - 2.0 * (y * y + z * z), 2.0 * (x * y - w * z),
        2.0 * (x * z + w * y), pos[0],
        2.0 * (x * y + w * z), 1.0 - 2.0 * (x * x + z * z),
        2.0 * (y * z - w * x), pos[1],
        2.0 * (x * z - w * y), 2.0 * (y * z + w * x),
        1.0 - 2.0 * (x * x + y * y), pos[2],
    ]


def _multiply(left: list[float], right: list[float]) -> list[float]:
    out: list[float] = []
    for row in range(3):
        for column in range(4):
            value = sum(
                left[row * 4 + k] * right[k * 4 + column] for k in range(3)
            )
            if column == 3:
                value += left[row * 4 + 3]
            out.append(value)
    return out


def _identity_error(matrix: list[float]) -> float:
    identity = [
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
    ]
    return max(abs(a - b) for a, b in zip(matrix, identity))


def _inspect_skeleton(bones: list[Any]) -> dict[str, Any]:
    """Self-consistency of one captured bone array.

    Composing each bone's bind transform down the hierarchy and applying the
    stored ``poseToBone`` must return the identity; that is what makes the
    captured bytes a skeleton rather than a plausible block of floats.
    """
    roots = 0
    ordered = True
    worst_norm = 0.0
    worst_bind = 0.0
    split = 0
    world: list[list[float]] = []
    for bone in bones:
        if bone.parent < 0:
            roots += 1
        elif bone.parent >= bone.index:
            ordered = False
        norm = sum(component * component for component in bone.quat) ** 0.5
        worst_norm = max(worst_norm, abs(norm - 1.0))
        if bone.flags & BONE_FLAG_SPLIT_INHERITANCE:
            split += 1
        local = _quaternion_matrix(bone.quat, bone.pos)
        if 0 <= bone.parent < len(world):
            world.append(_multiply(world[bone.parent], local))
        else:
            world.append(local)
        worst_bind = max(
            worst_bind,
            _identity_error(_multiply(world[-1], list(bone.pose_to_bone))),
        )
    return {
        "bones": len(bones),
        "roots": roots,
        "parents_precede_children": ordered,
        "worst_quaternion_norm_error": worst_norm,
        "worst_inverse_bind_error": worst_bind,
        "split_inheritance_bones": split,
        "sound": (
            ordered
            and roots >= 1
            and worst_norm <= QUATERNION_NORM_TOLERANCE
            and worst_bind <= INVERSE_BIND_TOLERANCE
        ),
    }


def skeletons(connection: sqlite3.Connection) -> dict[str, Any]:
    """Decode every captured image offline and report what the bones are."""
    from elysium_pipeline.formats import mdl_skel

    entries = []
    faults = []
    total_bones = 0
    split_total = 0
    worst_norm = 0.0
    worst_bind = 0.0
    for checksum, image, expected, name in connection.execute(
        """
        SELECT i.checksum, i.image, i.captured_bytes,
               (SELECT model_name FROM model_headers h
                WHERE h.checksum = i.checksum LIMIT 1)
        FROM model_images i ORDER BY i.checksum
        """
    ):
        if len(image) != expected:
            faults.append(
                {
                    "checksum": address(checksum),
                    "model_name": name,
                    "error": f"stored {len(image)} of {expected} bytes",
                }
            )
            continue
        try:
            bones = mdl_skel.read_bones(image)
            report = _inspect_skeleton(bones)
        except Exception as error:  # noqa: BLE001 - reported, not raised
            faults.append(
                {
                    "checksum": address(checksum),
                    "model_name": name,
                    "error": f"{type(error).__name__}: {error}",
                }
            )
            continue
        total_bones += report["bones"]
        split_total += report["split_inheritance_bones"]
        worst_norm = max(worst_norm, report["worst_quaternion_norm_error"])
        worst_bind = max(worst_bind, report["worst_inverse_bind_error"])
        if not report["sound"] and len(faults) < MAX_REPORTED_FAULTS:
            faults.append(
                {"checksum": address(checksum), "model_name": name, **report}
            )
        entries.append(
            {"checksum": address(checksum), "model_name": name, **report}
        )
    return {
        "skeletons": len(entries),
        "bones": total_bones,
        "split_inheritance_bones": split_total,
        "worst_quaternion_norm_error": worst_norm,
        "worst_inverse_bind_error": worst_bind,
        "unsound": faults,
        "sound": bool(entries) and not faults,
    }


def names(connection: sqlite3.Connection) -> dict[str, Any]:
    """How much of the run's model identity the draw record's field cannot hold.

    The census carries the header's whole 128-byte name field. Comparing it
    against the draw record's 64 turns a silent truncation into a number.
    """
    truncated = [
        {"model_name": name, "length": len(name)}
        for name, in connection.execute(
            "SELECT DISTINCT model_name FROM model_headers "
            "WHERE length(model_name) >= ? ORDER BY length(model_name) DESC "
            "LIMIT ?",
            (POSE_NAME_BYTES, MAX_REPORTED_FAULTS),
        )
    ]
    longest = connection.execute(
        "SELECT max(length(model_name)) FROM model_headers"
    ).fetchone()[0]
    colliding = connection.execute(
        """
        SELECT count(*) FROM (
            SELECT substr(model_name, 1, ?) AS prefix FROM (
                SELECT DISTINCT model_name FROM model_headers
            ) GROUP BY prefix HAVING count(*) > 1
        )
        """,
        (POSE_NAME_BYTES,),
    ).fetchone()[0]
    affected = connection.execute(
        "SELECT count(*) FROM records WHERE kind = 'POSE' AND checksum IN ("
        "SELECT checksum FROM model_headers WHERE length(model_name) >= ?)",
        (POSE_NAME_BYTES,),
    ).fetchone()[0]
    return {
        "draw_record_name_bytes": POSE_NAME_BYTES,
        "longest_name": longest,
        "names_at_or_over_the_draw_field": len(truncated),
        "prefixes_shared_by_several_models": colliding,
        "draw_records_carrying_a_truncated_name": affected,
        "truncated": truncated,
    }


def dictionary(connection: sqlite3.Connection) -> dict[str, Any]:
    """What the event stream still spends repeating what the census now holds.

    CAP2.2 does not drop the repeated field — CAP3.2's join schema owns that —
    so the deferral is reported as a measured cost rather than an intention.
    """
    draws, repeated = connection.execute(
        "SELECT count(*), count(*) * ? FROM records WHERE kind = 'POSE'",
        (POSE_NAME_BYTES,),
    ).fetchone()
    return {
        "draw_records": draws,
        "repeated_name_bytes": repeated or 0,
        "repeated_name_megabytes": round((repeated or 0) / 1_000_000, 3),
        "owner": "CAP3.1 content-addressed store, CAP3.2 join schema",
    }


_DIFF_BLOCK = 4096


def _differing_spans(left: bytes, right: bytes) -> tuple[int, list[list[int]]]:
    """Byte ranges where two model images disagree.

    Whole blocks are compared first and only a block that differs is walked
    byte by byte, so an identical multi-megabyte image costs one comparison
    per block rather than one per byte.
    """
    limit = min(len(left), len(right))
    spans: list[list[int]] = []
    differing = 0
    start = None
    for base in range(0, limit, _DIFF_BLOCK):
        end = min(base + _DIFF_BLOCK, limit)
        if left[base:end] == right[base:end]:
            if start is not None:
                spans.append([start, base])
                start = None
            continue
        for offset in range(base, end):
            if left[offset] != right[offset]:
                differing += 1
                if start is None:
                    start = offset
            elif start is not None:
                spans.append([start, offset])
                start = None
    if start is not None:
        spans.append([start, limit])
    return differing, spans


def source_join(connection: sqlite3.Connection) -> dict[str, Any]:
    """Compare each captured image against the patch-first installed bytes.

    This is the falsifiable check. Byte ranges that differ are the loader's
    in-place fixups, and naming them is what tells CAP2.5 which runtime
    pointers are image-relative.
    """
    try:
        from elysium_pipeline.formats import install
    except Exception as error:  # noqa: BLE001 - an absent install is an answer
        return {
            "available": False,
            "reason": f"{type(error).__name__}: {error}",
        }
    try:
        index = install.build_index(dirs=("models",), verbose=False)
    except Exception as error:  # noqa: BLE001
        return {
            "available": False,
            "reason": f"{type(error).__name__}: {error}",
        }

    entries = []
    unresolved = []
    identical = 0
    rows = connection.execute(
        """
        SELECT i.checksum, i.image, i.captured_bytes, i.model_length,
               min(h.model_name), min(h.bone_index), min(h.bone_count),
               min(h.include_model_index), min(h.include_model_count)
        FROM model_images i JOIN model_headers h ON h.checksum = i.checksum
        GROUP BY i.checksum ORDER BY i.checksum
        """
    ).fetchall()
    for row in rows:
        checksum, image, captured, length, name = row[0], row[1], row[2], row[3], row[4]
        bone_index, bone_count = row[5], row[6]
        include_index, include_count = row[7], row[8]
        key = install_key(name)
        installed = install.read(index, key)
        if installed is None:
            unresolved.append(
                {
                    "checksum": address(checksum),
                    "model_name": name,
                    "reason": "the patch-first install has no such path",
                }
            )
            continue
        if len(installed) < 12 or struct.unpack_from("<I", installed, 8)[0] != checksum:
            unresolved.append(
                {
                    "checksum": address(checksum),
                    "model_name": name,
                    "reason": (
                        "the installed file carries checksum "
                        + address(
                            struct.unpack_from("<I", installed, 8)[0]
                            if len(installed) >= 12
                            else 0
                        )
                    ),
                }
            )
            continue
        differing, spans = _differing_spans(image, installed)
        bone_end = bone_index + BONE_STRIDE * bone_count
        include_end = include_index + MODEL_GROUP_STRIDE * include_count
        entry = {
            "checksum": address(checksum),
            "model_name": name,
            "installed_bytes": len(installed),
            "captured_bytes": captured,
            "model_length": length,
            "length_matches": len(installed) == length,
            "differing_bytes": differing,
            "differing_spans": [
                [f"0x{low:x}", f"0x{high:x}"] for low, high in spans
            ][:MAX_REPORTED_SPANS],
            "differing_spans_truncated": len(spans) > MAX_REPORTED_SPANS,
            "bone_array": [f"0x{bone_index:x}", f"0x{bone_end:x}"],
            "bone_array_differs": any(
                low < bone_end and high > bone_index for low, high in spans
            ),
            "include_array": [
                f"0x{include_index:x}",
                f"0x{include_end:x}",
            ],
            "include_array_differs": any(
                low < include_end and high > include_index for low, high in spans
            ),
        }
        if differing == 0 and entry["length_matches"]:
            identical += 1
        entries.append(entry)

    return {
        "available": True,
        "joined": len(entries),
        "unresolved": len(unresolved),
        "identical": identical,
        "differing": len(entries) - identical,
        "bone_arrays_differing": sum(
            1 for entry in entries if entry["bone_array_differs"]
        ),
        "include_arrays_differing": sum(
            1 for entry in entries if entry["include_array_differs"]
        ),
        "entries": [entry for entry in entries if entry["differing_bytes"]][
            :MAX_REPORTED_FAULTS
        ],
        "unresolved_identities": unresolved[:MAX_REPORTED_FAULTS],
        "agrees": bool(entries) and identical == len(entries),
    }


def overhead(
    connection: sqlite3.Connection,
    headers: dict[str, dict[str, Any]],
    done: dict[str, str],
) -> dict[str, Any]:
    """Report what the census cost against CAP2.1's baseline."""
    record_bytes = record_bytes_expression(
        headers, table_columns(connection, "records")
    )
    frequency = next(iter(headers.values()))["qpc_frequency"]
    first, last, records, payload = connection.execute(
        f"SELECT min(qpc), max(qpc), count(*), sum({record_bytes}) FROM records"
    ).fetchone()
    span = (last - first) / frequency if first is not None and last > first else None
    census_records, census_payload = connection.execute(
        f"SELECT count(*), sum(bytes) FROM ({census_bytes_expression()})"
    ).fetchone()
    census_payload = census_payload or 0
    return {
        "span_seconds": round(span, 3) if span else None,
        "event_records": records,
        "census_records": census_records,
        "census_megabytes": round(census_payload / 1_000_000, 3),
        "census_byte_share": (
            round(census_payload / (payload + census_payload), 6)
            if payload
            else None
        ),
        "mean_megabytes_per_second": (
            round(payload / span / 1_000_000, 3) if span and payload else None
        ),
        "queue_peak": int(done["queue_peak"]) if "queue_peak" in done else None,
        "census_bytes_reported": (
            int(done["census_bytes"]) if "census_bytes" in done else None
        ),
        "census_overflow": (
            int(done["census_overflow"]) if "census_overflow" in done else None
        ),
        "census_faults": (
            int(done["census_faults"]) if "census_faults" in done else None
        ),
        "census_capped": (
            int(done["census_capped"]) if "census_capped" in done else None
        ),
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
    dictionary_state: dict[str, Any],
    reused: dict[str, Any],
    bones: dict[str, Any],
    joined: dict[str, Any],
    cost: dict[str, Any],
) -> dict[str, Any]:
    verdict: dict[str, Any] = {
        "census_complete": False,
        "skeletons_sound": False,
        "source_join": "unavailable",
    }
    if not support["carries_census"]:
        verdict["statement"] = (
            "This database predates the model and skeleton census "
            f"({support['stream_versions']}), so no studio header was recorded "
            "and CAP2.2 cannot be judged from it."
        )
        return verdict
    if not counts["complete"]:
        verdict["statement"] = (
            f"{counts['identities_unobserved']} of {counts['identities_used']} "
            "header identities were used without ever being observed and "
            f"{counts['identities_observed_late']} were observed after their "
            "first use, so the run does not meet CAP2.2's acceptance. The "
            "missing identities are named rather than inferred from a nearby "
            "header."
        )
        return verdict
    if cost and cost.get("census_overflow"):
        verdict["statement"] = (
            f"Every used identity was observed, but {cost['census_overflow']} "
            "headers were refused because a census table was full, so the "
            "census is complete only over what the tables could hold."
        )
        return verdict
    if not dictionary_state["one_image_per_checksum"]:
        verdict["statement"] = (
            f"{dictionary_state['images']} images cover "
            f"{dictionary_state['image_checksums']} checksums, so the same "
            "immutable model bytes were stored more than once and the "
            "dictionary is behaving as a stream."
        )
        return verdict
    if dictionary_state["images_capped"]:
        verdict["statement"] = (
            f"{dictionary_state['images_capped']} of "
            f"{dictionary_state['images']} model images were truncated at the "
            "capture cap, so an offset past the cap inside those models cannot "
            "be resolved and CAP2.5 would read them as unknown rather than "
            "unread."
        )
        return verdict
    if not dictionary_state["every_checksum_has_an_image"]:
        verdict["statement"] = (
            f"{dictionary_state['distinct_checksums']} checksums were observed "
            f"but only {dictionary_state['image_checksums']} carry an image, so "
            "some header's bytes were never retained and CAP2.5 cannot resolve "
            "an offset inside it."
        )
        return verdict
    verdict["census_complete"] = True
    if not reused["replacements_account_for_reuse"]:
        verdict["statement"] = (
            f"{reused['extra_identities_at_reused_addresses']} extra identities "
            f"appear at {reused['reused_header_addresses']} reused addresses but "
            f"{reused['replacement_observations']} replacement observations "
            "were recorded, so a pointer changed model without the census "
            "seeing it."
        )
        return verdict
    if not bones["sound"]:
        verdict["statement"] = (
            f"{len(bones['unsound'])} of {bones['skeletons']} captured images "
            "do not decode as a self-consistent skeleton, so the retained bytes "
            "are not the bone array they are claimed to be."
        )
        return verdict
    verdict["skeletons_sound"] = True
    if not joined.get("available"):
        verdict["source_join"] = "unavailable"
        verdict["statement"] = (
            f"All {counts['identities_used']} header identities were observed "
            f"at or before first use, {dictionary_state['image_checksums']} "
            f"images decode as sound skeletons over {bones['bones']} bones, and "
            f"{reused['replacement_observations']} replacements account for "
            "every reused address. The installed bytes were not reachable "
            f"({joined.get('reason')}), so the census is internally complete "
            "and not yet independently confirmed."
        )
        return verdict
    if joined["unresolved"]:
        verdict["source_join"] = "incomplete"
        verdict["statement"] = (
            f"{joined['unresolved']} of "
            f"{joined['joined'] + joined['unresolved']} checksums do not "
            "resolve to an installed model, so the byte comparison covers only "
            "part of the census. An unresolved identity is named, not counted "
            "as a mismatch."
        )
        return verdict
    if not joined["agrees"]:
        verdict["source_join"] = "differs"
        verdict["statement"] = (
            f"{joined['differing']} of {joined['joined']} captured images "
            "differ from the installed bytes for the same checksum "
            f"({joined['bone_arrays_differing']} inside the bone array, "
            f"{joined['include_arrays_differing']} inside the include-model "
            "array). Those spans are what the loader fixes up in place, and "
            "they are the ranges CAP2.5 must treat as not image-relative."
        )
        return verdict
    verdict["source_join"] = "identical"
    verdict["statement"] = (
        f"All {counts['identities_used']} header identities were observed at or "
        f"before first use, {dictionary_state['image_checksums']} images cover "
        f"{dictionary_state['distinct_checksums']} checksums exactly once, "
        f"{bones['bones']} bones decode as sound skeletons, "
        f"{reused['replacement_observations']} replacements account for every "
        f"reused address, and all {joined['joined']} images match the "
        "patch-first installed bytes for their checksum. The runtime studio "
        "header is the model image at offset zero, so a captured pointer minus "
        "the header base is a model-image offset."
    )
    return verdict


def verify(session: Path, *, join_source: bool = True) -> dict[str, Any]:
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

        if not support["carries_census"]:
            counts = entries = reused = resident = bones = None
            identity = repeated = joined = cost = None
            verdict = decide(support, {}, {}, {}, {}, {}, {})
        else:
            prepare(connection)
            counts = coverage(connection)
            entries = census(connection)
            reused = reuse(connection)
            resident = residency(connection, done)
            bones = skeletons(connection)
            identity = names(connection)
            repeated = dictionary(connection)
            joined = (
                source_join(connection)
                if join_source
                else {"available": False, "reason": "not requested"}
            )
            cost = overhead(connection, headers, done)
            verdict = decide(
                support, counts, entries, reused, bones, joined, cost
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
            "census": entries,
            "reuse": reused,
            "residency": resident,
            "skeletons": bones,
            "names": identity,
            "dictionary": repeated,
            "source_join": joined,
            "overhead": cost,
            "verdict": verdict,
        }
    finally:
        connection.close()


def compare(reports: list[dict[str, Any]]) -> dict[str, Any]:
    rows = []
    for report in reports:
        entries = report["census"] or {}
        rows.append(
            {
                "session": report["session"],
                "identities_used": (report["coverage"] or {}).get(
                    "identities_used"
                ),
                "unobserved": (report["coverage"] or {}).get(
                    "identities_unobserved"
                ),
                "distinct_studio_headers": entries.get("distinct_studio_headers"),
                "distinct_checksums": entries.get("distinct_checksums"),
                "census_complete": report["verdict"]["census_complete"],
                "source_join": report["verdict"]["source_join"],
                "checksums": sorted(entries.get("checksums", [])),
                "addresses": sorted(entries.get("addresses", [])),
            }
        )
    complete = [row for row in rows if row["census_complete"]]
    checksum_sets = {tuple(row["checksums"]) for row in rows}
    shared_addresses = (
        set.intersection(*(set(row["addresses"]) for row in rows))
        if len(rows) > 1
        else set()
    )
    same_map, maps = compared_maps(reports)
    if len(complete) != len(rows):
        statement = (
            f"{len(rows) - len(complete)} of {len(rows)} runs leave a used "
            "header identity unobserved, so the census is not yet established "
            "across runs."
        )
    elif not same_map:
        # Which models a run loads is a property of the scene's cast, so two
        # scenes differ on it by construction. Reporting that as a failure of
        # reproducibility would claim a comparison that was never entitled to
        # hold; the union is coverage.
        union = set().union(*(set(row["checksums"]) for row in rows))
        statement = (
            f"All {len(rows)} runs censused every identity they used. They "
            "captured different scenes (" + ", ".join(maps) + "), so their "
            f"model sets are coverage rather than agreement: {len(union)} "
            "distinct checksums across the runs, "
            f"{len(set.intersection(*(set(r['checksums']) for r in rows)))} "
            "of them loaded by every one."
        )
    elif len(checksum_sets) != 1:
        statement = (
            f"All {len(rows)} runs censused every identity they used, but the "
            "runs do not agree on which models the scene loads, so the "
            "census is complete without being reproducible."
        )
    else:
        statement = (
            f"All {len(rows)} runs censused every identity they used and agree "
            "on the same set of model checksums while sharing "
            f"{len(shared_addresses)} header addresses, so the census names "
            "the models rather than the heap they happened to land in."
        )
    return {
        "sessions": [report["session"] for report in reports],
        "maps": maps,
        "same_map": same_map,
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
        f"{counts['identities_used']} used header identities observed; "
        f"{counts['identities_unobserved']} unobserved, "
        f"{counts['identities_observed_late']} late"
    )
    entries = report["census"]
    lines.append(
        f"  census     {entries['distinct_studio_headers']} addresses, "
        f"{entries['distinct_checksums']} checksums, "
        f"{entries['images']} images ({entries['images_capped']} capped)"
    )
    reused = report["reuse"]
    lines.append(
        f"  reuse      {reused['reused_header_addresses']} header addresses "
        f"served several models, {reused['replacement_observations']} "
        f"replacements recorded; {reused['reused_entity_addresses']} entity "
        f"addresses did (CAP1.3 baseline "
        f"{reused['baseline_reused_entity_addresses']})"
    )
    lines.append(f"  residency  {report['residency']['resident_at_stop']} "
                 f"resident, {report['residency']['vanished_at_stop']} vanished")
    bones = report["skeletons"]
    lines.append(
        f"  skeletons  {bones['skeletons']} decoded over {bones['bones']} "
        f"bones; worst inverse bind {bones['worst_inverse_bind_error']:.3g}, "
        f"{bones['split_inheritance_bones']} split-inheritance bones"
    )
    identity = report["names"]
    lines.append(
        f"  names      longest {identity['longest_name']} chars; "
        f"{identity['names_at_or_over_the_draw_field']} at or over the "
        f"{identity['draw_record_name_bytes']}-byte draw field, "
        f"{identity['prefixes_shared_by_several_models']} colliding prefixes"
    )
    joined = report["source_join"]
    lines.append(
        "  join       "
        + (
            f"{joined['identical']}/{joined['joined']} images match the "
            f"install ({joined['unresolved']} unresolved)"
            if joined.get("available")
            else f"unavailable — {joined.get('reason')}"
        )
    )
    cost = report["overhead"]
    lines.append(
        f"  overhead   {cost['census_megabytes']} MB census "
        f"({cost['census_byte_share']} of bytes), "
        f"{cost['mean_megabytes_per_second']} MB/s mean "
        f"(baseline {cost['baseline']['mean_megabytes_per_second']}), "
        f"queue peak {cost['queue_peak']} "
        f"(baseline {cost['baseline']['queue_peak']})"
    )
    verdict = report["verdict"]
    lines.append(
        "  verdict    "
        + (
            "CENSUS ESTABLISHED"
            if verdict["census_complete"] and verdict["source_join"] == "identical"
            else "COMPLETE, UNCONFIRMED"
            if verdict["census_complete"]
            else "INCOMPLETE"
        )
        + f" — source join {verdict['source_join']}"
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
        help="Do not write model-skeleton-census.json beside each database.",
    )
    parser.add_argument(
        "--no-source-join",
        action="store_true",
        help="Skip the comparison against the patch-first installed models.",
    )
    args = parser.parse_args()

    reports = []
    for value in args.sessions:
        session = resolve_session(value)
        report = verify(session, join_source=not args.no_source_join)
        reports.append(report)
        if not args.no_session_reports:
            (session / "model-skeleton-census.json").write_text(
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
        report["verdict"]["census_complete"] for report in reports
    ) else 1


if __name__ == "__main__":
    raise SystemExit(main())
