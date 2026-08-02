"""Decide whether the spans retail dereferenced are witnessed rather than modelled.

Usage:
    uv run elysium research verify_consumed_spans <session> [<session> ...]

Reads a finalized capture database read-only, after `resolve_consumed_spans`
has written its span dictionary into it, and reports whether the pointers the
probe witnessed at the hook boundary land where an independent walker predicts
them from the owner image alone.

The question is the one CAP2.5 asks. A matching pose cannot distinguish a
correct decoder from a lucky one, so CAP4.2 needs the byte ranges retail read.
Those ranges are only evidence if the walk that produced them is checked
against something it did not produce, which is what the channel accumulator is
for: the probe latches the first animation record and bone each decoder was
handed and decodes nothing, and this report asks whether the walker predicts
the same two pointers, the same frame, and the same set of bones.

Four bounds travel with this report.

The pointers are checked, the spans below them are not. A witnessed record base
landing where the walker predicts confirms the displacement, both strides and
the animindex indirection together; it does not independently confirm the RLE
walk inside a track. That walk is transcribed from a confirmed finding and
bounded here only by staying inside the image.

The bone gating is compared, not assumed. Each cell's decoded-bone bitmap is
checked against the selected-bone mask of the sequence that enclosed it. Equal
means the mask rule survived the include-model dispatcher unchanged; a strict
subset is reported rather than accepted, and a superset is a hard failure.

The sequence descriptor is only partly claimed. The resolver emits the fields
the specification pins and leaves the rest of the 764 bytes unclaimed, so the
byte totals here are a floor for the sequence side and exact for the animation
side.

The loader-written intersection needs the install. It reuses the census
verifier's per-image difference, which reads the user's own game tree, and is
reported unavailable rather than failing when that tree is absent.
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
    open_database,
    record_bytes_expression,
    resolve_session,
    stream_headers,
    table_columns,
)
from research.tooling.capture.resolve_consumed_spans import (
    ANIM_RECORD_STRIDE,
    BONE_STRIDE,
    HEADER_BONE_INDEX,
    HEADER_LOCAL_ANIM_INDEX,
    HEADER_NUM_BONES,
    HEADER_NUM_LOCAL_ANIMS,
    ANIM_DESC_ANIMINDEX,
    ANIM_DESC_NUMFRAMES,
    ANIM_DESC_STRIDE,
    SPAN_TABLES,
)

CONTRIBUTION_STREAM = "contribution"
SEQUENCE_KIND = "SEQP"
ANIMATION_KIND = "ANIM"
CONTRIBUTION_KINDS = (SEQUENCE_KIND, ANIMATION_KIND)

# Mirrors the channel bits of ContributionFault in the probe.
CHANNEL_FAULT_NAMES = {
    1 << 8: "channels_unwitnessed",
    1 << 9: "channel_stride",
    1 << 10: "channel_nested",
    1 << 11: "channel_overflow",
}
CHANNEL_FAULT_MASK = sum(CHANNEL_FAULT_NAMES)

# CAP2.4's measured cost on the database this one widens.
BASELINE_MEAN_MB_PER_SECOND = 8.60
BASELINE_QUEUE_HIGH_WATER = 368

MAX_REPORTED_OFFENDERS = 64


def address(value: int | None) -> str | None:
    return None if value is None else f"0x{value:08x}"


def prepare(connection: sqlite3.Connection) -> None:
    """Reduce the blob-bearing record table to the cells being judged.

    Each cell is joined to the selected-bone mask of the sequence that shares
    its contribution scope, because the gating question is exactly whether the
    two agree.
    """
    connection.execute(
        f"""
        CREATE TEMP TABLE cell AS
        SELECT id, sequence_number, qpc, contribution, owner_checksum,
               owner_studio_hdr, bone_count, animation_index, channel_record_base,
               channel_bone_base, channel_quaternion_calls,
               channel_position_calls, channel_frame, channel_fraction,
               channel_bone_bytes, cycle, faults, raw_payload
        FROM records
        WHERE kind = '{ANIMATION_KIND}'
        """
    )
    connection.execute("CREATE INDEX temp.cell_scope ON cell(contribution)")
    connection.execute(
        f"""
        CREATE TEMP TABLE enclosing AS
        SELECT contribution AS scope, owner_checksum, bone_count,
               selected_bone_bytes, sequence_index, raw_payload
        FROM records
        WHERE kind = '{SEQUENCE_KIND}' AND contribution != 0
        """
    )
    connection.execute(
        "CREATE INDEX temp.enclosing_scope ON enclosing(scope)"
    )


def capability(
    connection: sqlite3.Connection, headers: dict[str, dict[str, Any]]
) -> dict[str, Any]:
    """What this database can be asked, so an older one is answered not rejected."""
    columns = table_columns(connection, "records")
    tables = {
        name
        for (name,) in connection.execute(
            "SELECT name FROM sqlite_master WHERE type = 'table'"
        )
    }
    version = int(headers.get(CONTRIBUTION_STREAM, {}).get("version", 0) or 0)
    return {
        "stream_versions": {
            name: header.get("version") for name, header in headers.items()
        },
        "carries_channels": version >= 2 and "channel_record_base" in columns,
        "carries_spans": set(SPAN_TABLES) <= tables,
        "carries_images": "model_images" in tables
        and bool(
            connection.execute("SELECT count(*) FROM model_images").fetchone()[0]
        ),
    }


def _images(connection: sqlite3.Connection) -> dict[int, bytes]:
    return {
        int(checksum): bytes(image)
        for checksum, image in connection.execute(
            "SELECT checksum, image FROM model_images"
        )
    }


def _bits(payload: bytes | None, words: int, second: bool) -> set[int]:
    if not payload:
        return set()
    half = words * 4
    chunk = payload[half : half * 2] if second else payload[:half]
    return {
        index
        for index in range(len(chunk) * 8)
        if chunk[index // 8] & (1 << (index % 8))
    }


def witness(connection: sqlite3.Connection) -> dict[str, Any]:
    """How many cells carry an accumulator, and whether its counts cohere.

    An absent accumulator is not automatically a loss. The cell frame bails
    before either decoder when its mask selects no bone, so a cell enclosed by
    an empty mask correctly witnesses nothing; only one enclosed by a mask that
    does select bones is evidence the accumulator went missing.
    """
    total, witnessed = connection.execute(
        "SELECT count(*), sum(channel_record_base IS NOT NULL "
        "AND channel_record_base != 0) FROM cell"
    ).fetchone()
    by_fault: dict[str, int] = {}
    for bit, name in CHANNEL_FAULT_NAMES.items():
        count = connection.execute(
            "SELECT count(*) FROM cell WHERE faults & ? != 0", (bit,)
        ).fetchone()[0]
        if count:
            by_fault[name] = int(count)
    miscounted = 0
    for calls_q, calls_p, words, payload in connection.execute(
        "SELECT channel_quaternion_calls, channel_position_calls, "
        "channel_bone_bytes, raw_payload FROM cell "
        "WHERE channel_record_base IS NOT NULL AND channel_record_base != 0"
    ):
        words = int(words or 0) // 8
        quaternion = _bits(payload, words, False)
        position = _bits(payload, words, True)
        if len(quaternion) != int(calls_q or 0) or len(position) != int(
            calls_p or 0
        ):
            miscounted += 1
    expected = lost = 0
    for bones, mask_bytes, mask_payload in connection.execute(
        """
        SELECT c.bone_count, e.selected_bone_bytes, e.raw_payload
        FROM cell c JOIN enclosing e ON e.scope = c.contribution
        WHERE c.channel_record_base IS NULL OR c.channel_record_base = 0
        """
    ):
        blob = (mask_payload or b"")[-int(mask_bytes or 0) :]
        selected = sum(
            1
            for index in range(int(bones or 0))
            if index // 8 < len(blob) and blob[index // 8] & (1 << (index % 8))
        )
        if selected:
            lost += 1
        else:
            expected += 1
    return {
        "cells": int(total or 0),
        "witnessed": int(witnessed or 0),
        "unwitnessed": int(total or 0) - int(witnessed or 0),
        "unwitnessed_with_an_empty_mask": expected,
        "unwitnessed_with_a_selecting_mask": lost,
        "faults": {
            name: count
            for name, count in by_fault.items()
            # The probe stamps this bit on every cell that witnessed nothing,
            # including the ones that correctly decoded nothing, so it is
            # counted above rather than read as a failure here.
            if name != "channels_unwitnessed"
        },
        "calls_disagreeing_with_bitmap": miscounted,
        "complete": lost == 0
        and not {
            name: count
            for name, count in by_fault.items()
            if name != "channels_unwitnessed"
        }
        and miscounted == 0,
    }


def roots(
    connection: sqlite3.Connection, support: dict[str, Any]
) -> dict[str, Any]:
    """The falsifiable check: do witnessed pointers land where the walker says?

    The probe latches both pointers from the first bone a decoder ran for and
    indexes its bitmaps off that pointer, so which bone that was is not recorded
    anywhere -- bit zero means "the first one", not "bone zero". That is what
    makes this check strong rather than weak: the bone is solved for
    independently from each pointer and the two answers must agree.

        (record base - studiohdr - animdesc - animindex) / 32   == bone
        (bone base   - studiohdr - BoneIndex)            / 160  == the same bone

    Both must divide exactly, both must land inside the owner's bone count, and
    both must name one bone. That verifies the animindex indirection, the
    32-byte record stride, the 160-byte bone stride and the two base fields
    together, from two values the probe recorded separately and neither of which
    the walker produced.
    """
    if not support["carries_images"]:
        return {
            "available": False,
            "reason": "the capture carries no model images",
        }
    images = _images(connection)
    checked = 0
    misplaced_records = 0
    misplaced_bones = 0
    offenders: list[dict[str, Any]] = []
    # Which bone each cell started at, solved from its own pointers, so the
    # gating comparison can shift a relative bitmap into the mask's index space
    # without the walker supplying the shift.
    first_bones: dict[int, int] = {}
    for (
        record_id,
        checksum,
        studio_hdr,
        animation_index,
        record_base,
        bone_base,
        words,
        payload,
    ) in connection.execute(
        "SELECT id, owner_checksum, owner_studio_hdr, animation_index, "
        "channel_record_base, channel_bone_base, channel_bone_bytes, "
        "raw_payload FROM cell "
        "WHERE channel_record_base IS NOT NULL AND channel_record_base != 0"
    ):
        image = images.get(int(checksum or 0))
        if image is None:
            continue
        decoded = _bits(payload, int(words or 0) // 8, False) | _bits(
            payload, int(words or 0) // 8, True
        )
        if not decoded:
            continue
        anims = struct.unpack_from("<i", image, HEADER_NUM_LOCAL_ANIMS)[0]
        if not 0 <= int(animation_index) < anims:
            continue
        anim_base = struct.unpack_from("<i", image, HEADER_LOCAL_ANIM_INDEX)[0]
        desc = anim_base + int(animation_index) * ANIM_DESC_STRIDE
        animindex = struct.unpack_from("<i", image, desc + ANIM_DESC_ANIMINDEX)[0]
        bone_index = struct.unpack_from("<i", image, HEADER_BONE_INDEX)[0]
        bones = struct.unpack_from("<i", image, HEADER_NUM_BONES)[0]
        checked += 1
        base = int(studio_hdr or 0)
        record_delta = int(record_base) - base - desc - animindex
        bone_delta = int(bone_base) - base - bone_index
        from_record = (
            record_delta // ANIM_RECORD_STRIDE
            if record_delta >= 0 and record_delta % ANIM_RECORD_STRIDE == 0
            else None
        )
        from_bone = (
            bone_delta // BONE_STRIDE
            if bone_delta >= 0 and bone_delta % BONE_STRIDE == 0
            else None
        )
        if from_record is None or not 0 <= from_record < bones:
            misplaced_records += 1
        if from_bone is None or not 0 <= from_bone < bones:
            misplaced_bones += 1
        elif from_record is not None and from_record != from_bone:
            # Both divide and both land in range but they name different bones,
            # which is the case a single-sided check would have accepted.
            misplaced_bones += 1
        if (
            from_record is None
            or from_bone is None
            or from_record != from_bone
            or not 0 <= from_record < bones
        ) and len(offenders) < MAX_REPORTED_OFFENDERS:
            offenders.append(
                {
                    "record_id": int(record_id),
                    "checksum": address(int(checksum)),
                    "studio_hdr": address(base),
                    "bone_from_record": from_record,
                    "bone_from_bone_array": from_bone,
                    "bone_count": bones,
                    "record_delta": record_delta,
                    "bone_delta": bone_delta,
                }
            )
        else:
            first_bones[int(record_id)] = from_record
    return {
        "available": True,
        "checked": checked,
        "misplaced_records": misplaced_records,
        "misplaced_bones": misplaced_bones,
        "offenders": offenders,
        "first_bones": first_bones,
        # Vacuous when nothing was checked, which the count beside it makes
        # visible; a run whose cells all decoded nothing has no pointer to
        # place, and calling that a disagreement would be a false alarm.
        "agree": misplaced_records == 0 and misplaced_bones == 0,
    }


def frames(
    connection: sqlite3.Connection, support: dict[str, Any]
) -> dict[str, Any]:
    """Whether the witnessed frame is floor((numframes - 1) * cycle).

    No capture before this one could refute the sampling law; the frame the cell
    frame computed is now recorded beside the cycle it was given.
    """
    if not support["carries_images"]:
        return {"available": False, "reason": "the capture carries no model images"}
    images = _images(connection)
    checked = 0
    disagreeing = 0
    out_of_range = 0
    offenders: list[dict[str, Any]] = []
    for record_id, checksum, animation_index, frame, fraction, cycle in (
        connection.execute(
            "SELECT id, owner_checksum, animation_index, channel_frame, "
            "channel_fraction, cycle FROM cell "
            "WHERE channel_record_base IS NOT NULL AND channel_record_base != 0"
        )
    ):
        image = images.get(int(checksum or 0))
        if image is None:
            continue
        anims = struct.unpack_from("<i", image, HEADER_NUM_LOCAL_ANIMS)[0]
        if not 0 <= int(animation_index) < anims:
            continue
        anim_base = struct.unpack_from("<i", image, HEADER_LOCAL_ANIM_INDEX)[0]
        desc = anim_base + int(animation_index) * ANIM_DESC_STRIDE
        numframes = struct.unpack_from("<i", image, desc + ANIM_DESC_NUMFRAMES)[0]
        expected = int((numframes - 1) * float(cycle))
        checked += 1
        if int(frame) != expected:
            disagreeing += 1
            if len(offenders) < MAX_REPORTED_OFFENDERS:
                offenders.append(
                    {
                        "record_id": int(record_id),
                        "numframes": numframes,
                        "cycle": float(cycle),
                        "witnessed_frame": int(frame),
                        "expected_frame": expected,
                    }
                )
        if not 0.0 <= float(fraction or 0.0) < 1.0:
            out_of_range += 1
    return {
        "available": True,
        "checked": checked,
        "disagreeing": disagreeing,
        "fractions_out_of_range": out_of_range,
        "offenders": offenders,
        "agree": disagreeing == 0 and out_of_range == 0,
    }


def gating(
    connection: sqlite3.Connection, first_bones: dict[int, int]
) -> dict[str, Any]:
    """Each cell's decoded bones against the mask of the sequence above it.

    The bitmaps are indexed from the first bone a decoder ran for, so they are
    shifted into the mask's index space by the bone the two witnessed pointers
    independently agreed on. A cell whose pointers did not agree is not compared
    here at all; `roots` has already failed it.

    The dispatcher that resolves an include-model sequence remaps bones across
    the group boundary, and whether the mask a cell gates on survives that is
    not pinned by any finding. Comparing the two answers it for the corpus.
    """
    compared = 0
    equal = 0
    subset = 0
    superset = 0
    unplaced = 0
    for (
        record_id,
        cell_words,
        cell_payload,
        mask_bytes,
        mask_payload,
        bone_count,
    ) in connection.execute(
        """
        SELECT c.id, c.channel_bone_bytes, c.raw_payload, e.selected_bone_bytes,
               e.raw_payload, c.bone_count
        FROM cell c JOIN enclosing e ON e.scope = c.contribution
        WHERE c.channel_record_base IS NOT NULL AND c.channel_record_base != 0
        """
    ):
        shift = first_bones.get(int(record_id))
        if shift is None:
            unplaced += 1
            continue
        words = int(cell_words or 0) // 8
        decoded = {
            index + shift
            for index in _bits(cell_payload, words, False)
            | _bits(cell_payload, words, True)
        }
        mask_blob = (mask_payload or b"")[-int(mask_bytes or 0) :]
        selected = {
            index
            for index in range(int(bone_count or 0))
            if index // 8 < len(mask_blob)
            and mask_blob[index // 8] & (1 << (index % 8))
        }
        compared += 1
        if decoded == selected:
            equal += 1
        elif decoded < selected:
            subset += 1
        else:
            superset += 1
    return {
        "compared": compared,
        "equal": equal,
        "decoded_subset_of_mask": subset,
        "decoded_beyond_mask": superset,
        "cells_whose_pointers_disagreed": unplaced,
        "consistent": superset == 0,
    }


def spans(connection: sqlite3.Connection) -> dict[str, Any]:
    """Whether every resolved span lies inside the owner image it names."""
    resolved, faulted = connection.execute(
        "SELECT sum(resolved), sum(NOT resolved) FROM record_span_sets"
    ).fetchone()
    sets = connection.execute("SELECT count(*) FROM span_sets").fetchone()[0]
    outside = connection.execute(
        """
        SELECT count(*) FROM span_sets s
        LEFT JOIN model_images m ON m.checksum = s.checksum
        WHERE m.image IS NULL OR s.first_offset < 0
           OR s.last_offset > length(m.image)
        """
    ).fetchone()[0]
    inverted = connection.execute(
        "SELECT count(*) FROM span_sets WHERE last_offset <= first_offset"
    ).fetchone()[0]
    by_role = {
        role: {"intervals": int(count), "bytes": int(total)}
        for role, count, total in connection.execute(
            "SELECT role, sum(intervals), sum(bytes) FROM span_roles "
            "GROUP BY role ORDER BY role"
        )
    }
    per_model = {
        f"0x{int(checksum):08x}": int(total)
        for checksum, total in connection.execute(
            'SELECT checksum, sum("end" - start) FROM model_coverage '
            "GROUP BY checksum ORDER BY checksum"
        )
    }
    intervals = connection.execute(
        "SELECT sum(interval_count) FROM span_sets"
    ).fetchone()[0]
    # One digest per shape, so a second run can be asked whether the same shape
    # produced the same bytes. Small enough to travel in the report: a cutscene
    # produces under two hundred.
    shapes = {
        f"{int(checksum):08x}:{shape}": digest
        for checksum, shape, digest in connection.execute(
            "SELECT checksum, shape, digest FROM span_sets ORDER BY id"
        )
    }
    faults = {
        reason: int(count)
        for reason, count in connection.execute(
            "SELECT fault, count(*) FROM record_span_sets "
            "WHERE fault IS NOT NULL GROUP BY fault ORDER BY count(*) DESC"
        )
    }
    return {
        "resolved": int(resolved or 0),
        "faulted": int(faulted or 0),
        "span_sets": int(sets or 0),
        "intervals": int(intervals or 0),
        "span_sets_outside_their_image": int(outside),
        "inverted_intervals": int(inverted),
        "by_role": by_role,
        "shapes": shapes,
        "bytes_per_model": per_model,
        "faults": faults,
        "closed": int(faulted or 0) == 0
        and int(outside) == 0
        and int(inverted) == 0,
    }


def dictionary(connection: sqlite3.Connection) -> dict[str, Any]:
    """What the deduplicated store cost against the per-contribution shape."""
    references, sets, intervals, blob_bytes = connection.execute(
        "SELECT (SELECT count(*) FROM record_span_sets), "
        "(SELECT count(*) FROM span_sets), "
        "(SELECT sum(interval_count) FROM span_sets), "
        "(SELECT sum(length(intervals)) FROM span_sets)"
    ).fetchone()
    naive = connection.execute(
        """
        SELECT sum(s.interval_count) FROM record_span_sets r
        JOIN span_sets s ON s.id = r.span_set_id
        """
    ).fetchone()[0]
    return {
        "references": int(references or 0),
        "span_sets": int(sets or 0),
        "intervals": int(intervals or 0),
        "intervals_without_dedup": int(naive or 0),
        "encoded_bytes": int(blob_bytes or 0),
        "bytes_per_interval": (
            round(int(blob_bytes or 0) / int(intervals), 2) if intervals else None
        ),
        "reuse_factor": (
            round(int(references or 0) / int(sets), 2) if sets else None
        ),
        "intervals_avoided": int(naive or 0) - int(intervals or 0),
    }


def loader_written(connection: sqlite3.Connection) -> dict[str, Any]:
    """How much of the consumed union falls in ranges the loader rewrites.

    Reuses the census verifier's per-image difference, which needs the user's
    own install; without it the section reports itself unavailable rather than
    failing, exactly as that verifier's own source join does.
    """
    try:
        from research.tooling.capture.verify_model_skeleton_census import (
            source_join,
        )
    except ImportError as error:  # pragma: no cover - import guard
        return {"available": False, "reason": str(error)}
    try:
        joined = source_join(connection)
    except Exception as error:  # noqa: BLE001
        return {"available": False, "reason": str(error)}
    if not joined.get("available", False):
        return {"available": False, "reason": joined.get("reason", "unavailable")}
    return {
        "available": True,
        "source_join": {
            key: joined[key]
            for key in sorted(joined)
            if key not in {"available", "reason"}
        },
        "note": (
            "Spans intersecting these ranges are loader-written rather than "
            "source bytes, and CAP4.2 must not attribute them to the file."
        ),
    }


def overhead(
    connection: sqlite3.Connection,
    headers: dict[str, dict[str, Any]],
    done: dict[str, str],
) -> dict[str, Any]:
    expression = record_bytes_expression(
        headers, table_columns(connection, "records")
    )
    stream = headers.get(CONTRIBUTION_STREAM, {})
    closed_form = connection.execute(
        f"SELECT sum({expression}) FROM records WHERE kind IN {CONTRIBUTION_KINDS}"
    ).fetchone()[0]
    payload = int(stream.get("file_size", 0)) - int(stream.get("header_bytes", 0))
    total_bytes = sum(
        int(header.get("file_size", 0)) for header in headers.values()
    )
    span = connection.execute(
        "SELECT (max(qpc) - min(qpc)) FROM records"
    ).fetchone()[0]
    frequency = int(stream.get("qpc_frequency", 0)) or 1
    seconds = (span or 0) / frequency
    calls = connection.execute(
        "SELECT sum(channel_quaternion_calls), sum(channel_position_calls) "
        "FROM cell"
    ).fetchone()
    return {
        "closed_form_bytes": int(closed_form or 0),
        "stream_payload_bytes": payload,
        "byte_closure": int(closed_form or 0) == payload,
        "megabytes": round(payload / (1024 * 1024), 3),
        "share_of_database": (
            round(payload / total_bytes, 5) if total_bytes else None
        ),
        "mean_megabytes_per_second": (
            round(total_bytes / (1024 * 1024) / seconds, 3) if seconds else None
        ),
        "baseline_mean_megabytes_per_second": BASELINE_MEAN_MB_PER_SECOND,
        "queue_high_water": int(done.get("queue_peak", "0") or 0),
        "baseline_queue_high_water": BASELINE_QUEUE_HIGH_WATER,
        "dropped": int(done.get("dropped", "0") or 0),
        "quaternion_decodes": int(calls[0] or 0),
        "position_decodes": int(calls[1] or 0),
        "channel_unwitnessed": int(done.get("channel_unwitnessed", "0") or 0),
        "channel_stride_faults": int(done.get("channel_stride_faults", "0") or 0),
        "channel_nested": int(done.get("channel_nested", "0") or 0),
    }


def decide(
    support: dict[str, Any],
    seen: dict[str, Any] | None,
    rooted: dict[str, Any] | None,
    framed: dict[str, Any] | None,
    gated: dict[str, Any] | None,
    walked: dict[str, Any] | None,
) -> dict[str, Any]:
    if not support["carries_channels"]:
        return {
            "spans_resolved": False,
            "roots_agree": False,
            "judgeable": False,
            "statement": (
                "This capture carries no witnessed channel accumulator, so "
                "consumed spans cannot be judged."
            ),
        }
    if not support["carries_spans"]:
        return {
            "spans_resolved": False,
            "roots_agree": False,
            "judgeable": False,
            "statement": (
                "This database has not been resolved; run "
                "resolve_consumed_spans over it first."
            ),
        }
    verdict = {
        "spans_resolved": bool(walked and walked["closed"]),
        "roots_agree": bool(
            rooted
            and rooted.get("agree")
            and framed
            and framed.get("agree")
            and gated
            and gated["consistent"]
            and seen
            and seen["complete"]
        ),
        "judgeable": True,
    }
    if seen and not seen["complete"]:
        verdict["statement"] = (
            f"{seen['unwitnessed_with_a_selecting_mask']} of {seen['cells']} "
            "cells witnessed no decode although their enclosing mask selects "
            f"bones, {seen['calls_disagreeing_with_bitmap']} disagree with "
            f"their own bitmaps and the probe reported {seen['faults']}."
        )
        return verdict
    if rooted and rooted.get("available") and not rooted["agree"]:
        verdict["statement"] = (
            f"{rooted['misplaced_records']} witnessed record pointers and "
            f"{rooted['misplaced_bones']} bone pointers of {rooted['checked']} "
            "do not land where an independent walker predicts them."
        )
        return verdict
    if framed and framed.get("available") and not framed["agree"]:
        verdict["statement"] = (
            f"{framed['disagreeing']} of {framed['checked']} witnessed frames "
            "disagree with floor((numframes - 1) * cycle) and "
            f"{framed['fractions_out_of_range']} fractions fall outside [0, 1)."
        )
        return verdict
    if gated and not gated["consistent"]:
        verdict["statement"] = (
            f"{gated['decoded_beyond_mask']} cells decoded bones their "
            "enclosing sequence's mask does not select."
        )
        return verdict
    if walked and not walked["closed"]:
        verdict["statement"] = (
            f"{walked['faulted']} contributions could not be resolved "
            f"({walked['faults']}), {walked['span_sets_outside_their_image']} "
            "span sets leave their owner image."
        )
        return verdict
    verdict["statement"] = (
        f"{seen['witnessed'] if seen else 0} of {seen['cells'] if seen else 0} "
        "cells carry a witnessed accumulator and the "
        f"{seen['unwitnessed_with_an_empty_mask'] if seen else 0} that do not "
        "are enclosed by a mask selecting no bone, so every one is accounted "
        "for; "
        f"{rooted['checked'] if rooted else 0} witnessed record and bone "
        "pointers land exactly where an independent walker predicts, so the "
        "displacement, both strides and the animindex indirection are verified "
        f"together; {framed['checked'] if framed else 0} witnessed frames equal "
        "floor((numframes - 1) * cycle); "
        f"{gated['equal'] if gated else 0} of {gated['compared'] if gated else 0} "
        "decoded-bone sets equal the selected-bone mask of their enclosing "
        f"sequence; and {walked['span_sets'] if walked else 0} distinct span "
        f"sets cover {sum((walked or {}).get('bytes_per_model', {}).values())} "
        f"bytes of {len((walked or {}).get('bytes_per_model', {}))} owner "
        "images, every interval inside the image it names."
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

        seen = rooted = framed = gated = walked = stored = written = cost = None
        if support["carries_channels"]:
            prepare(connection)
            seen = witness(connection)
            rooted = roots(connection, support)
            framed = frames(connection, support)
            gated = gating(connection, (rooted or {}).get("first_bones") or {})
            cost = overhead(connection, headers, done)
        if support["carries_spans"]:
            walked = spans(connection)
            stored = dictionary(connection)
            written = loader_written(connection)
        verdict = decide(support, seen, rooted, framed, gated, walked)

        return {
            "session": session.name,
            "session_path": str(session),
            "database": str(session / DATABASE_NAME),
            "identity": {
                "spans_source_database_sha256": metadata.get(
                    "spans_source_database_sha256"
                ),
                "spans_resolver_version": metadata.get("spans_resolver_version"),
                "spans_tool_git": metadata.get("spans_tool_git"),
                "modules": {
                    name: sha
                    for name, sha in connection.execute(
                        "SELECT name, sha256 FROM modules"
                    )
                },
            },
            "support": support,
            "witness": seen,
            "roots": (
                {k: v for k, v in rooted.items() if k != "first_bones"}
                if rooted
                else None
            ),
            "frames": framed,
            "gating": gated,
            "spans": walked,
            "dictionary": stored,
            "loader_written": written,
            "overhead": cost,
            "verdict": verdict,
        }
    finally:
        connection.close()


def compare(reports: list[dict[str, Any]]) -> dict[str, Any]:
    """Whether the same shape produced the same bytes in every run.

    The per-model consumed union is deliberately *not* the claim. Which frames
    a run samples depends on where the operator reached the arrival trigger, and
    a different frame walks past a different number of run headers, so two
    honest runs of one cutscene cover different bytes of the same clip. Reading
    that as a disagreement would fail a correct pair of runs every time.

    What must not vary is the walk itself: one shape -- one owner, one animation
    index, one pair of decoded-bone bitmaps -- has one answer, and the digest is
    over that answer. So the cross-run claim is that every shape appearing in
    more than one run carries the same digest in all of them, and the union
    difference is reported beside it as coverage rather than as a fault.
    """
    rows = []
    shapes: dict[str, dict[str, str]] = {}
    for report in reports:
        walked = report.get("spans") or {}
        rooted = report.get("roots") or {}
        session = report["session"]
        for shape, digest in (walked.get("shapes") or {}).items():
            shapes.setdefault(shape, {})[session] = digest
        rows.append(
            {
                "session": session,
                "cells": (report.get("witness") or {}).get("cells"),
                "roots_checked": rooted.get("checked"),
                "misplaced": (rooted.get("misplaced_records") or 0)
                + (rooted.get("misplaced_bones") or 0),
                "span_sets": walked.get("span_sets"),
                "consumed_bytes": sum(
                    (walked.get("bytes_per_model") or {}).values()
                ),
                "models": len(walked.get("bytes_per_model") or {}),
                "resolved": report["verdict"].get("spans_resolved")
                and report["verdict"].get("roots_agree"),
            }
        )
    failing = [row["session"] for row in rows if not row["resolved"]]
    common = {
        shape: seen for shape, seen in shapes.items() if len(seen) == len(rows)
    }
    divergent = sorted(
        shape for shape, seen in common.items() if len(set(seen.values())) > 1
    )
    if failing:
        statement = (
            "Runs disagree because "
            + ", ".join(failing)
            + " did not resolve every span."
        )
    elif divergent:
        statement = (
            f"{len(divergent)} of {len(common)} shapes present in every run "
            "produced different spans, so the walk is not a function of the "
            "shape alone: " + ", ".join(divergent[:6])
        )
    else:
        statement = (
            f"All {len(rows)} runs resolve every span and place every witnessed "
            f"pointer where the walker predicts, and all {len(common)} shapes "
            "they share produce byte-identical spans, so the walk is a function "
            "of the shape and nothing else. Their consumed unions differ by "
            "which frames each run happened to sample, which is coverage rather "
            "than disagreement."
        )
    return {
        "rows": rows,
        "shared_shapes": len(common),
        "divergent_shapes": divergent,
        "statement": statement,
    }


def summarize(report: dict[str, Any]) -> str:
    seen = report.get("witness") or {}
    rooted = report.get("roots") or {}
    framed = report.get("frames") or {}
    gated = report.get("gating") or {}
    walked = report.get("spans") or {}
    stored = report.get("dictionary") or {}
    cost = report.get("overhead") or {}
    lines = [
        f"session                       {report['session']}",
        f"cells witnessed               {seen.get('witnessed')} / {seen.get('cells')}",
        f"  unwitnessed, mask empty     {seen.get('unwitnessed_with_an_empty_mask')}",
        f"  unwitnessed, mask selects   {seen.get('unwitnessed_with_a_selecting_mask')}",
        f"record pointers checked       {rooted.get('checked')}",
        f"  misplaced records           {rooted.get('misplaced_records')}",
        f"  misplaced bones             {rooted.get('misplaced_bones')}",
        f"frames checked                {framed.get('checked')}",
        f"  disagreeing with the cycle  {framed.get('disagreeing')}",
        f"bitmaps equal to their mask   {gated.get('equal')} / {gated.get('compared')}",
        f"  decoded beyond the mask     {gated.get('decoded_beyond_mask')}",
        f"span sets                     {walked.get('span_sets')}",
        f"  intervals                   {walked.get('intervals')}",
        f"  outside their image         {walked.get('span_sets_outside_their_image')}",
        f"  unresolved contributions    {walked.get('faulted')}",
        f"dictionary reuse              {stored.get('reuse_factor')}x",
        f"  intervals avoided           {stored.get('intervals_avoided')}",
        f"  encoded bytes               {stored.get('encoded_bytes')} "
        f"({stored.get('bytes_per_interval')} per interval)",
        f"byte closure                  {cost.get('byte_closure')}",
        f"mean MB/s                     {cost.get('mean_megabytes_per_second')} "
        f"(baseline {cost.get('baseline_mean_megabytes_per_second')})",
        f"queue high water              {cost.get('queue_high_water')} "
        f"(baseline {cost.get('baseline_queue_high_water')})",
        f"channel decodes               {cost.get('quaternion_decodes')} quaternion, "
        f"{cost.get('position_decodes')} position",
        "",
        report["verdict"]["statement"],
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
            (session / "consumed-spans-verdict.json").write_text(
                json.dumps(report, indent=2) + "\n", encoding="utf-8"
            )
    across = compare(reports) if len(reports) > 1 else None
    if across:
        print(across["statement"])
    if args.report:
        args.report.write_text(
            json.dumps({"sessions": reports, "comparison": across}, indent=2)
            + "\n",
            encoding="utf-8",
        )
    return (
        0
        if all(
            report["verdict"]["spans_resolved"]
            and report["verdict"]["roots_agree"]
            for report in reports
        )
        else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
