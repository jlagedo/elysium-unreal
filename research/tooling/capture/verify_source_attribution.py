"""Decide whether every fired contribution names the source it was decoded from.

Usage:
    uv run elysium research verify_source_attribution <session> [<session> ...]

Reads a finalized capture database read-only and reports whether each fired
contribution resolves to an owner studio header the run also captured, to an
owner-local sequence and animation index inside that owner's declared counts,
and to blend cells and weights the runtime actually produced.

The question is the one CAP2.4 asks: a pose that matches proves nothing unless
the bytes it was decoded from are named. A target-model sequence index is not
that name, because a character resolves its clips through include models that
are nobody's entity model.

Three bounds travel with this report.

Owner attribution is witnessed, not inferred. The three hooked frames receive
the owning studio header as argument zero, so the owner on a record is what the
runtime passed, and the census observation for it is emitted from the same call.
A run whose owner never reaches the census is a fault, not a silent gap.

The caller is an address, not a name. Which builder asked for a contribution is
resolved offline against the case specification by nearest preceding seed
function, so an address inside an unlisted function is reported unresolved
rather than attributed to the seed below it.

Blend weights are witnessed only where the resolver ran. A sequence whose axes
resolve against a descriptor this record does not name carries the unwitnessed
fault and zeroed cells rather than a recomputed guess.
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
from research.tooling.capture.finalize_capture_database import (
    CONTRIBUTION_FILE_HEADER,
)

CONTRIBUTION_STREAM = "contribution"
ANIMATION_STREAM = "animation"
SEQUENCE_KIND = "SEQP"
ANIMATION_KIND = "ANIM"
CONTRIBUTION_KINDS = (SEQUENCE_KIND, ANIMATION_KIND)
BASE_KIND = "BASE"
POSE_BUILD_KIND = "PBLD"

# Mirrors ContributionFault in the probe.
FAULT_NAMES = {
    1 << 0: "owner_header",
    1 << 1: "sequence_descriptor",
    1 << 2: "animation_descriptor",
    1 << 3: "pose_parameters",
    1 << 4: "selected_bones",
    1 << 5: "blend_unwitnessed",
    1 << 6: "sequence_out_of_range",
    1 << 7: "no_contribution_scope",
}
FAULT_MASK = sum(FAULT_NAMES)

# MDLHeader displacements the range check reads out of the owner's own image.
HEADER_NUM_LOCAL_ANIMS = 264
HEADER_LOCAL_ANIM_INDEX = 268
HEADER_NUM_LOCAL_SEQ = 272
HEADER_LOCAL_SEQ_INDEX = 276
SEQUENCE_DESCRIPTOR_BYTES = 764
ANIMATION_DESCRIPTOR_BYTES = 72

# CAP2.3's measured cost on the database this one widens.
BASELINE_MEAN_MB_PER_SECOND = 8.42
BASELINE_QUEUE_HIGH_WATER = 368
# The whole installed character tree carries 294 multi-blend sequences; a run
# that exercises none of them has not tested the blend path at all.
CORPUS_MULTI_BLEND_SEQUENCES = 294

MAX_REPORTED_FAULTS = 64
MAX_REPORTED_OWNERS = 200
MAX_REPORTED_CALLERS = 64

SPEC_PATH = (
    Path(__file__).resolve().parents[3]
    / "research"
    / "cases"
    / "animation-pose"
    / "specs"
    / "animation_pose.json"
)


def address(value: int | None) -> str | None:
    return None if value is None else f"0x{value:08x}"


def prepare(connection: sqlite3.Connection) -> None:
    """Reduce the blob-bearing record table to one row per thing being judged.

    `records` stores its payloads inline, so grouping it directly drags the
    whole capture off disk. Every section below reads these instead.
    """
    connection.execute(
        f"""
        CREATE TEMP TABLE contribution_row AS
        SELECT id, kind, sequence_number, qpc, generation, generation_entity,
               contribution, caller_address, owner_studio_hdr, owner_checksum,
               bone_count, sequence_index, animation_index,
               sequence_descriptor, animation_descriptor, num_blends,
               group_size, param_index, blend_cell, blend_weight, faults,
               pose_parameter_bytes, selected_bone_bytes
        FROM records
        WHERE kind IN {CONTRIBUTION_KINDS}
        """
    )
    connection.execute(
        "CREATE INDEX temp.contribution_row_scope ON contribution_row(contribution)"
    )
    connection.execute(
        "CREATE INDEX temp.contribution_row_owner "
        "ON contribution_row(owner_checksum, sequence_index)"
    )
    # Coverage correlates an evaluation's generation against this table. Without
    # this index SQLite rescans every contribution once per evaluation, which on
    # a full cutscene is a quarter-million scans of half a million rows and
    # looks like a hang rather than a slow query.
    connection.execute(
        "CREATE INDEX temp.contribution_row_generation "
        "ON contribution_row(generation)"
    )
    connection.execute(
        f"""
        CREATE TEMP TABLE contribution_scope AS
        SELECT contribution AS scope,
               sum(kind = '{SEQUENCE_KIND}') AS sequences,
               sum(kind = '{ANIMATION_KIND}') AS cells,
               count(DISTINCT generation) AS generations,
               count(DISTINCT owner_checksum) AS owners,
               min(qpc) AS first_qpc, max(qpc) AS last_qpc
        FROM contribution_row
        WHERE contribution != 0
        GROUP BY contribution
        """
    )
    connection.execute(
        "CREATE UNIQUE INDEX temp.contribution_scope_id "
        "ON contribution_scope(scope)"
    )
    connection.execute(
        """
        CREATE TEMP TABLE contribution_owner AS
        SELECT owner_studio_hdr AS studio_hdr, owner_checksum AS checksum,
               max(bone_count) AS bone_count, min(qpc) AS first_qpc,
               max(qpc) AS last_qpc, count(*) AS records
        FROM contribution_row
        GROUP BY owner_studio_hdr, owner_checksum
        """
    )
    connection.execute(
        "CREATE UNIQUE INDEX temp.contribution_owner_identity "
        "ON contribution_owner(studio_hdr, checksum)"
    )
    # The three generation sets coverage compares. Reducing each to its distinct
    # values once turns the comparisons into joins over thousands of rows rather
    # than seeks into a multi-gigabyte blob table once per record.
    for name, source in (
        ("pose_build_generation", f"records WHERE kind = '{POSE_BUILD_KIND}'"),
        ("evaluation_generation", f"records WHERE kind = '{BASE_KIND}'"),
        ("contribution_generation", "contribution_row"),
    ):
        connection.execute(
            f"CREATE TEMP TABLE {name} AS "
            f"SELECT DISTINCT generation FROM {source}"
        )
        connection.execute(
            f"CREATE UNIQUE INDEX temp.{name}_value ON {name}(generation)"
        )


def capability(
    connection: sqlite3.Connection, headers: dict[str, dict[str, Any]]
) -> dict[str, Any]:
    """What this database can be asked, so an older one is answered not rejected."""
    present = CONTRIBUTION_STREAM in headers
    columns = table_columns(connection, "records")
    rvas: dict[str, str] | None = None
    if present:
        blob = connection.execute(
            "SELECT file_header FROM streams WHERE name = ?",
            (CONTRIBUTION_STREAM,),
        ).fetchone()
        if blob:
            fields = CONTRIBUTION_FILE_HEADER.unpack(blob[0])
            rvas = {
                "client_base": address(int(fields[6])),
                "evaluate_sequence_pose": address(int(fields[7])),
                "decode_selected_bones": address(int(fields[8])),
                "resolve_blend_axis_weight": address(int(fields[9])),
            }
    return {
        "stream_versions": {
            name: header.get("version") for name, header in headers.items()
        },
        "carries_contributions": present
        and "owner_checksum" in columns
        and "contribution" in columns,
        "carries_images": bool(
            connection.execute(
                "SELECT count(*) FROM sqlite_master "
                "WHERE type = 'table' AND name = 'model_images'"
            ).fetchone()[0]
        ),
        "targets": rvas,
    }


def coverage(connection: sqlite3.Connection) -> dict[str, Any]:
    """The acceptance criterion: every contribution is scoped and grouped."""
    sequences, cells = connection.execute(
        f"SELECT sum(kind = '{SEQUENCE_KIND}'), sum(kind = '{ANIMATION_KIND}') "
        "FROM contribution_row"
    ).fetchone()
    unbracketed = connection.execute(
        "SELECT count(*) FROM contribution_row WHERE generation = 0"
    ).fetchone()[0]
    unscoped = connection.execute(
        "SELECT count(*) FROM contribution_row WHERE contribution = 0"
    ).fetchone()[0]
    # A decoded cell whose scope carries no sequence would be a cell nobody
    # asked for, which the call graph says cannot happen.
    orphan_cells = connection.execute(
        "SELECT count(*) FROM contribution_scope WHERE cells > 0 AND sequences = 0"
    ).fetchone()[0]
    split_scopes = connection.execute(
        "SELECT count(*) FROM contribution_scope WHERE generations > 1"
    ).fetchone()[0]
    # Every contribution should sit inside a generation a pose build opened.
    outside_pose_build = connection.execute(
        """
        SELECT count(*) FROM contribution_row c
        WHERE c.generation != 0 AND c.generation NOT IN (
            SELECT generation FROM pose_build_generation
        )
        """
    ).fetchone()[0]
    # And every evaluation that produced a pose should have named a source.
    # Counted over distinct generations rather than records: several evaluations
    # in one pose build share its sources, so counting records would report the
    # same unattributed group once per evaluation.
    evaluations = connection.execute(
        "SELECT count(*) FROM evaluation_generation"
    ).fetchone()[0]
    unattributed = connection.execute(
        """
        SELECT count(*) FROM evaluation_generation e
        WHERE e.generation NOT IN (
            SELECT generation FROM contribution_generation
        )
        """
    ).fetchone()[0]
    complete = (
        unbracketed == 0
        and unscoped == 0
        and orphan_cells == 0
        and split_scopes == 0
        and outside_pose_build == 0
        and unattributed == 0
    )
    return {
        "sequences": int(sequences or 0),
        "cells": int(cells or 0),
        "scopes": connection.execute(
            "SELECT count(*) FROM contribution_scope"
        ).fetchone()[0],
        "unbracketed": unbracketed,
        "unscoped": unscoped,
        "orphan_cells": orphan_cells,
        "scopes_spanning_generations": split_scopes,
        "outside_a_pose_build": outside_pose_build,
        "evaluation_generations": evaluations,
        "generations_without_a_contribution": unattributed,
        "complete": complete,
    }


def owners(connection: sqlite3.Connection, support: dict[str, Any]) -> dict[str, Any]:
    """Owner census closure, extended from the entity models CAP2.2 covered."""
    rows = connection.execute(
        "SELECT studio_hdr, checksum, bone_count, first_qpc, records "
        "FROM contribution_owner ORDER BY records DESC"
    ).fetchall()
    if not support["carries_images"]:
        return {
            "available": False,
            "reason": "the database carries no model census",
            "identities": len(rows),
        }
    unobserved: list[dict[str, Any]] = []
    late: list[dict[str, Any]] = []
    without_image: list[dict[str, Any]] = []
    bank_only = 0
    for studio_hdr, checksum, bone_count, first_qpc, records in rows:
        observed = connection.execute(
            "SELECT min(qpc) FROM model_headers "
            "WHERE studio_hdr = ? AND checksum = ?",
            (studio_hdr, checksum),
        ).fetchone()[0]
        entry = {
            "studio_hdr": address(studio_hdr),
            "checksum": address(checksum),
            "bone_count": bone_count,
            "records": records,
        }
        if observed is None:
            unobserved.append(entry)
        elif observed > first_qpc:
            late.append({**entry, "observed_qpc": observed, "used_qpc": first_qpc})
        if not connection.execute(
            "SELECT count(*) FROM model_images WHERE checksum = ?", (checksum,)
        ).fetchone()[0]:
            without_image.append(entry)
        # An owner that no actor ever animated under is a bank model, which is
        # exactly the population no earlier census could see. Only existence
        # matters, so this stops at the first row rather than counting every
        # evaluation of a busy model.
        if not connection.execute(
            "SELECT EXISTS(SELECT 1 FROM records "
            "WHERE kind IN ('BASE', 'FINL') AND checksum = ?)",
            (checksum,),
        ).fetchone()[0]:
            bank_only += 1
    return {
        "available": True,
        "identities": len(rows),
        "distinct_checksums": len({row[1] for row in rows}),
        "bank_only_owners": bank_only,
        "unobserved": unobserved[:MAX_REPORTED_OWNERS],
        "unobserved_count": len(unobserved),
        "late": late[:MAX_REPORTED_OWNERS],
        "late_count": len(late),
        "without_image": without_image[:MAX_REPORTED_OWNERS],
        "without_image_count": len(without_image),
        "closed": not unobserved and not late and not without_image,
    }


def indices(connection: sqlite3.Connection, support: dict[str, Any]) -> dict[str, Any]:
    """Range-check every owner-local index against the owner's own image.

    The descriptor pointers are checked too. If a captured pointer minus the
    header base is not exactly the declared index times the declared stride,
    then either the displacement or the stride is wrong, and a matching pose
    downstream would be luck.
    """
    if not support["carries_images"]:
        return {"available": False, "reason": "the database carries no model census"}
    images = {
        checksum: blob
        for checksum, blob in connection.execute(
            "SELECT checksum, image FROM model_images"
        )
    }
    checked = 0
    out_of_range: list[dict[str, Any]] = []
    misplaced: list[dict[str, Any]] = []
    unresolved = 0
    for row in connection.execute(
        "SELECT kind, owner_studio_hdr, owner_checksum, sequence_index, "
        "animation_index, sequence_descriptor, animation_descriptor "
        "FROM contribution_row"
    ):
        (
            kind,
            studio_hdr,
            checksum,
            sequence_index,
            animation_index,
            sequence_descriptor,
            animation_descriptor,
        ) = row
        image = images.get(checksum)
        if image is None or len(image) < HEADER_LOCAL_SEQ_INDEX + 4:
            unresolved += 1
            continue
        checked += 1
        if kind == SEQUENCE_KIND:
            count, base = struct.unpack_from(
                "<ii", image, HEADER_NUM_LOCAL_SEQ
            )
            index, pointer, stride = (
                sequence_index,
                sequence_descriptor,
                SEQUENCE_DESCRIPTOR_BYTES,
            )
        else:
            count, base = struct.unpack_from(
                "<ii", image, HEADER_NUM_LOCAL_ANIMS
            )
            index, pointer, stride = (
                animation_index,
                animation_descriptor,
                ANIMATION_DESCRIPTOR_BYTES,
            )
        entry = {
            "kind": kind,
            "checksum": address(checksum),
            "index": index,
            "declared_count": count,
            "descriptor": address(pointer),
        }
        if index < 0 or index >= count:
            if len(out_of_range) < MAX_REPORTED_FAULTS:
                out_of_range.append(entry)
            continue
        if pointer and studio_hdr:
            if pointer - studio_hdr != base + index * stride:
                if len(misplaced) < MAX_REPORTED_FAULTS:
                    misplaced.append(
                        {
                            **entry,
                            "offset": pointer - studio_hdr,
                            "expected_offset": base + index * stride,
                        }
                    )
    return {
        "available": True,
        "checked": checked,
        "unresolved": unresolved,
        "out_of_range": out_of_range,
        "out_of_range_count": len(out_of_range),
        "misplaced_descriptors": misplaced,
        "misplaced_count": len(misplaced),
        "in_range": not out_of_range and not misplaced,
    }


def blends(connection: sqlite3.Connection) -> dict[str, Any]:
    """Blend closure: the grid the descriptor declares and the cells that fired."""
    total = 0
    multi = 0
    product_mismatch = 0
    cell_out_of_range = 0
    weight_out_of_range = 0
    unwitnessed = 0
    grids: dict[str, int] = {}
    for num_blends, group_size, blend_cell, blend_weight, faults in (
        connection.execute(
            "SELECT num_blends, group_size, blend_cell, blend_weight, faults "
            f"FROM contribution_row WHERE kind = '{SEQUENCE_KIND}'"
        )
    ):
        total += 1
        sizes = json.loads(group_size)
        cells = json.loads(blend_cell)
        weights = json.loads(blend_weight)
        if faults & (1 << 5):
            unwitnessed += 1
        if (num_blends or 0) > 1:
            multi += 1
            grids[f"{sizes[0]}x{sizes[1]}"] = grids.get(
                f"{sizes[0]}x{sizes[1]}", 0
            ) + 1
        if sizes[0] * sizes[1] != num_blends:
            product_mismatch += 1
        for axis, size in enumerate(sizes):
            if size > 1 and not 0 <= cells[axis] < size:
                cell_out_of_range += 1
        for weight in weights:
            if not -1e-6 <= weight <= 1.0 + 1e-6:
                weight_out_of_range += 1
    return {
        "sequences": total,
        "multi_blend_sequences": multi,
        "grids": dict(sorted(grids.items(), key=lambda item: -item[1])),
        "group_product_mismatch": product_mismatch,
        "cell_out_of_range": cell_out_of_range,
        "weight_out_of_range": weight_out_of_range,
        "unwitnessed": unwitnessed,
        "closed": product_mismatch == 0
        and cell_out_of_range == 0
        and weight_out_of_range == 0
        and unwitnessed == 0,
    }


def spec_functions(path: Path = SPEC_PATH) -> list[tuple[int, str]]:
    """The seed entry points a case specification declares, ascending.

    Shared with the scene verifier, which resolves callers in a second module
    against a second specification by the same rule.
    """
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except Exception:  # noqa: BLE001 - an absent specification is an answer
        return []
    entries = []
    for function in document.get("functions", []):
        try:
            entries.append(
                (int(function["address"], 16), str(function["label"]))
            )
        except (KeyError, ValueError):
            continue
    return sorted(entries)


def nearest_preceding_label(
    functions: list[tuple[int, str]], virtual: int
) -> str | None:
    """The seed an address falls in, or None before the first one.

    The specification carries entry points and no sizes, so an address is
    attributed to the seed below it only when one exists; anything before the
    first seed is unresolved rather than guessed at.
    """
    label = None
    for start, name in functions:
        if start <= virtual:
            label = name
        else:
            break
    return label


def _spec_functions() -> list[tuple[int, str]]:
    return spec_functions()


def callers(connection: sqlite3.Connection, support: dict[str, Any]) -> dict[str, Any]:
    """Which frame asked for each contribution, by nearest preceding seed.

    The specification carries function entry points and no sizes, so an address
    is attributed to the seed below it only when one exists, and anything before
    the first seed is reported unresolved rather than guessed.
    """
    targets = support.get("targets") or {}
    base_text = targets.get("client_base")
    if not base_text:
        return {"available": False, "reason": "the stream carries no client base"}
    client_base = int(base_text, 16)
    functions = _spec_functions()
    rows = connection.execute(
        "SELECT kind, caller_address, count(*) FROM contribution_row "
        "GROUP BY kind, caller_address ORDER BY count(*) DESC"
    ).fetchall()
    resolved: list[dict[str, Any]] = []
    unresolved = 0
    for kind, caller, count in rows:
        virtual = caller - client_base + 0x10000000
        label = nearest_preceding_label(functions, virtual)
        if label is None:
            unresolved += count
        if len(resolved) < MAX_REPORTED_CALLERS:
            resolved.append(
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
        "sites": resolved,
        "sites_truncated": len(rows) > MAX_REPORTED_CALLERS,
    }


def multiplicity(connection: sqlite3.Connection) -> dict[str, Any]:
    """Repeated calls stay separate events, so the shape of the repetition is data."""
    cells = connection.execute(
        "SELECT cells, count(*) FROM contribution_scope GROUP BY cells "
        "ORDER BY cells"
    ).fetchall()
    per_generation = connection.execute(
        f"""
        SELECT contributions, count(*) FROM (
            SELECT generation, sum(kind = '{SEQUENCE_KIND}') AS contributions
            FROM contribution_row WHERE generation != 0 GROUP BY generation
        ) GROUP BY contributions ORDER BY contributions
        """
    ).fetchall()
    return {
        "cells_per_sequence": {str(value): count for value, count in cells},
        "sequences_per_generation": {
            str(value): count for value, count in per_generation
        },
        "layered_generations": sum(
            count for value, count in per_generation if value > 1
        ),
    }


def faults(connection: sqlite3.Connection) -> dict[str, Any]:
    """The attribution faults, and only those.

    A later task may add bits to the same word -- CAP2.5's channel accumulator
    does -- and a record carrying one of those has not failed to name its
    source. Masking to the bits this report knows keeps a widened probe from
    reading as a regression here.
    """
    counts: dict[str, int] = {}
    total = 0
    for value, count in connection.execute(
        f"SELECT faults, count(*) FROM contribution_row "
        f"WHERE faults & {FAULT_MASK} != 0 GROUP BY faults"
    ):
        total += count
        for bit, name in FAULT_NAMES.items():
            if value & bit:
                counts[name] = counts.get(name, 0) + count
    return {
        "records_with_a_fault": total,
        "by_name": dict(sorted(counts.items(), key=lambda item: -item[1])),
        "clean": total == 0,
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
    queue_peak = int(done.get("queue_peak", "0") or 0)
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
        "queue_high_water": queue_peak,
        "baseline_queue_high_water": BASELINE_QUEUE_HIGH_WATER,
        "dropped": int(done.get("dropped", "0") or 0),
        "contribution_overflow": int(done.get("contribution_overflow", "0") or 0),
        "contribution_unscoped": int(done.get("contribution_unscoped", "0") or 0),
    }


def decide(
    support: dict[str, Any],
    counts: dict[str, Any] | None,
    owned: dict[str, Any] | None,
    ranged: dict[str, Any] | None,
    blended: dict[str, Any] | None,
    failed: dict[str, Any] | None,
    cost: dict[str, Any] | None,
) -> dict[str, Any]:
    if not support["carries_contributions"]:
        return {
            "attribution_complete": False,
            "sources_resolved": False,
            "judgeable": False,
            "statement": (
                "This capture carries no contribution stream, so source "
                "attribution cannot be judged."
            ),
        }
    verdict = {
        "attribution_complete": bool(counts and counts["complete"]),
        "sources_resolved": bool(
            owned
            and owned.get("closed")
            and ranged
            and ranged.get("in_range")
            and blended
            and blended["closed"]
            and failed
            and failed["clean"]
        ),
        "judgeable": True,
    }
    if counts and counts["unscoped"]:
        verdict["statement"] = (
            f"{counts['unscoped']} decoded cells were emitted outside every "
            "contribution scope, so they name no sequence."
        )
        return verdict
    if counts and counts["unbracketed"]:
        verdict["statement"] = (
            f"{counts['unbracketed']} contributions sit outside every pose "
            "build, so no actor owns them."
        )
        return verdict
    if counts and counts["scopes_spanning_generations"]:
        verdict["statement"] = (
            f"{counts['scopes_spanning_generations']} contribution scopes span "
            "more than one generation, so the scope is not a grouping."
        )
        return verdict
    if counts and counts["generations_without_a_contribution"]:
        verdict["statement"] = (
            f"{counts['generations_without_a_contribution']} of "
            f"{counts['evaluation_generations']} evaluated pose builds "
            "named no source."
        )
        return verdict
    if owned and owned.get("available") and not owned["closed"]:
        verdict["statement"] = (
            f"{owned['unobserved_count']} owner identities were used without a "
            f"census observation, {owned['late_count']} were observed late and "
            f"{owned['without_image_count']} carry no image."
        )
        return verdict
    if ranged and ranged.get("available") and not ranged["in_range"]:
        verdict["statement"] = (
            f"{ranged['out_of_range_count']} indices fall outside their owner's "
            f"declared counts and {ranged['misplaced_count']} descriptor "
            "pointers disagree with the declared stride."
        )
        return verdict
    if blended and not blended["closed"]:
        verdict["statement"] = (
            f"{blended['group_product_mismatch']} blend grids disagree with "
            f"their blend count, {blended['cell_out_of_range']} cells fall "
            f"outside their axis and {blended['unwitnessed']} sequences carry "
            "no witnessed weight."
        )
        return verdict
    if failed and not failed["clean"]:
        verdict["statement"] = (
            f"{failed['records_with_a_fault']} contributions carry a copy "
            f"fault: {failed['by_name']}."
        )
        return verdict
    verdict["statement"] = (
        f"{counts['sequences']} sequence contributions and {counts['cells']} "
        f"decoded cells across {counts['scopes']} scopes all name an owner; "
        f"{owned['identities'] if owned else 0} owner identities "
        f"({owned['bank_only_owners'] if owned else 0} of them bank models no "
        f"actor animates under) were observed at or before first use with an "
        f"image each; every index lies inside its owner's declared counts and "
        f"every descriptor pointer matches the declared stride; "
        f"{blended['multi_blend_sequences'] if blended else 0} multi-blend "
        "sequences close on their grids with witnessed weights; and no record "
        "carries a copy fault."
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

        counts = owned = ranged = blended = called = repeated = failed = cost = None
        if support["carries_contributions"]:
            prepare(connection)
            counts = coverage(connection)
            owned = owners(connection, support)
            ranged = indices(connection, support)
            blended = blends(connection)
            called = callers(connection, support)
            repeated = multiplicity(connection)
            failed = faults(connection)
            cost = overhead(connection, headers, done)
        verdict = decide(support, counts, owned, ranged, blended, failed, cost)

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
            "owners": owned,
            "indices": ranged,
            "blends": blended,
            "callers": called,
            "multiplicity": repeated,
            "faults": failed,
            "overhead": cost,
            "verdict": verdict,
        }
    finally:
        connection.close()


def compare(reports: list[dict[str, Any]]) -> dict[str, Any]:
    rows = []
    for report in reports:
        counts = report.get("coverage") or {}
        owned = report.get("owners") or {}
        rows.append(
            {
                "session": report["session"],
                "sequences": counts.get("sequences"),
                "cells": counts.get("cells"),
                "owners": owned.get("identities"),
                "bank_only": owned.get("bank_only_owners"),
                "resolved": report["verdict"].get("sources_resolved"),
            }
        )
    failing = [row["session"] for row in rows if not row["resolved"]]
    if failing:
        statement = (
            "Runs disagree because "
            + ", ".join(failing)
            + " did not resolve every source."
        )
    elif len({row["owners"] for row in rows}) > 1:
        statement = (
            "Every run resolves its sources, but they observe different owner "
            "counts: " + ", ".join(f"{r['session']}={r['owners']}" for r in rows)
        )
    else:
        statement = (
            f"All {len(rows)} runs resolve every source and agree on "
            f"{rows[0]['owners']} owner identities."
        )
    return {"rows": rows, "statement": statement}


def summarize(report: dict[str, Any]) -> str:
    lines = [f"session      {report['session']}"]
    support = report["support"]
    lines.append(
        "support      contributions="
        f"{support['carries_contributions']} images={support['carries_images']}"
    )
    counts = report.get("coverage")
    if counts:
        lines.append(
            f"coverage     sequences={counts['sequences']} "
            f"cells={counts['cells']} scopes={counts['scopes']} "
            f"unscoped={counts['unscoped']} unbracketed={counts['unbracketed']}"
        )
    owned = report.get("owners")
    if owned and owned.get("available"):
        lines.append(
            f"owners       identities={owned['identities']} "
            f"bank_only={owned['bank_only_owners']} "
            f"unobserved={owned['unobserved_count']} "
            f"no_image={owned['without_image_count']}"
        )
    ranged = report.get("indices")
    if ranged and ranged.get("available"):
        lines.append(
            f"indices      checked={ranged['checked']} "
            f"out_of_range={ranged['out_of_range_count']} "
            f"misplaced={ranged['misplaced_count']}"
        )
    blended = report.get("blends")
    if blended:
        lines.append(
            f"blends       multi={blended['multi_blend_sequences']} "
            f"grids={blended['grids']} "
            f"unwitnessed={blended['unwitnessed']}"
        )
    called = report.get("callers")
    if called and called.get("available"):
        lines.append(
            f"callers      distinct={called['distinct_callers']} "
            f"unresolved={called['unresolved_records']}"
        )
    failed = report.get("faults")
    if failed:
        lines.append(f"faults       {failed['records_with_a_fault']}")
    cost = report.get("overhead")
    if cost:
        lines.append(
            f"overhead     {cost['megabytes']} MB "
            f"closure={cost['byte_closure']} "
            f"queue={cost['queue_high_water']}"
        )
    lines.append(f"verdict      {report['verdict']['statement']}")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sessions", nargs="+")
    parser.add_argument("--report", type=Path)
    parser.add_argument(
        "--no-session-reports",
        action="store_true",
        help="Do not write source-attribution.json beside each database.",
    )
    args = parser.parse_args()

    reports = []
    for value in args.sessions:
        session = resolve_session(value)
        report = verify(session)
        reports.append(report)
        if not args.no_session_reports:
            (session / "source-attribution.json").write_text(
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
    return (
        0
        if all(
            report["verdict"]["attribution_complete"]
            and report["verdict"]["sources_resolved"]
            for report in reports
        )
        else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
