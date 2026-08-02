"""What moves the bones the composition rules do not cover.

Usage:
    uv run elysium research analyze_secondary_motion <session> [<session> ...]

Reads a finalized, indexed capture database read-only and writes one
`secondary-motion.json` beside it. Nothing is written into the database, for
`verify_transform_difference.py`'s reason: a capture is evidence and a report is
a claim about this checkout, so the two never share a file.

`verify_transform_difference` closes the bone-to-world stage down to a residual
it names but does not explain — bones carrying no `ProcType`, no `Flags & 0x1`
and no `Flags & 0x2`, whose drawn orientation retail's own hierarchy plus the
`ProcType == 1` correction still does not reproduce. The names are cloth, hair
and a prop. This pass asks the two questions that need no new capture and no
game run, and it answers them with correlations rather than with a verdict.

    step 1  Does the divergence track the actor's own motion? A secondary-motion
            solve goes quiet when its actor is still, so the divergence is
            correlated against the entity's translation and rotation between
            consecutive draws — and against the driving parent bone's own
            world-space motion, because an actor can stand rooted while its
            skeleton moves and only the second is an input a solve would see.

    step 2  Does the drawn orientation lag the previous draw's, or does it carry
            a velocity term? Three orientations bound the question per sample:
            the previous drawn one, the one composition targets now, and the one
            retail drew. Where retail's lands between the other two the stage
            behaves like a first-order lag; where it lands past the target the
            stage overshot and carries velocity; where it lands off the arc
            joining them it is neither.

Both steps run over one ordered series per actor. Draws are ordered by the
capture's dense global `sequence_number` and never by wall clock, and a draw is
tied to its pose build by identity — the generation spine, the fixed renderable
offset, and a matching model — exactly as `verify_transform_difference` pairs
them.

Each candidate is fed retail's own input for the bone under test: the target
orientation is retail's captured bone-to-world **for the parent** times the
captured local, not our own composed parent. A hair chain's bones are each
other's parents, so composing the chain would report the chain root's
divergence once per bone and hide where it starts.

Three bounds travel with the report.

The residual set is measured here rather than taken from a sibling report. The
first pass composes the whole corpus under the most complete offline rule —
A.4a's split inheritance plus the `ProcType == 1` correction — and keeps the
bones that leave the band. A report that named its own bones would be a claim
about which bones are interesting rather than a measurement of which are.

A bone's divergence is scored only where the pose build selected it. An
unselected slot's local is whatever the builder last left there, so scoring it
would difference against a value nothing wrote. The unselected samples still
enter the series, because they carry retail's drawn matrix and the actor's root
frame, and both are needed to say what happened between two scored samples.

`Sheriff Sword` is reported apart from the cloth and hair bones and never
merged into their conclusion. It is the sole authored bone of a separate prop
model drawn as its own entity, so whatever places it is entity-level parenting
and a different question; folding it in would let a prop's answer stand in for
cloth's.

Correlations are reported, not verdicts. A rank correlation says the two move
together, and this pass has no instrument that could say which drives which. A
weak correlation is reported weak.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import sqlite3
import time
from typing import Any, Iterator

import numpy as np

from research.tooling.capture import decoder_pose
from research.tooling.capture.calibrate_theatre_capture import (
    DATABASE_NAME,
    compared_maps,
    open_database,
    resolve_session,
    stream_headers,
)
from research.tooling.capture.decoder_pose import BANDS, Skeleton
from research.tooling.capture.resolve_consumed_spans import (
    file_sha256,
    tool_commit,
)
from research.tooling.capture.verify_transform_difference import (
    COMPOSED_KIND,
    DRAW_KIND,
    MATRIX_BYTES,
    RENDERABLE_OFFSET,
    ROOT_TRANSFORM_BYTES,
    corpus,
    modules,
    owner_names,
    skeletons,
    support,
)

REPORT_NAME = "secondary-motion.json"

DEFAULT_BATCH = 4096

# The residual is defined by the most complete rule available offline, which is
# `verify_transform_difference`'s `split_procedural` candidate: A.4a's split
# inheritance plus the `ProcType == 1` correction `docs/vtmb/procedural_bones.md`
# owns. A bone that only the weaker candidates miss is that candidate's cost and
# not this pass's subject.
RESIDUAL_SPLIT = True
RESIDUAL_PROCEDURAL = True

# Two populations, kept apart for the whole report. A prop drawn as its own
# entity is placed by whatever parents entities, which is not the question cloth
# and hair ask.
CLOTH_AND_HAIR = "cloth_and_hair"
PROP_BONE = "prop_bone"
# The one bone name the corpus carries that is a prop rather than secondary
# motion. Matched on the name because what makes it a prop is that its model is
# a separate drawn entity, which no bone field records.
PROP_BONE_NAMES = ("Sheriff Sword",)

# An actor is still when neither its translation nor its rotation moved between
# two consecutive draws by more than this. Both are far below one frame of any
# authored motion at the recipe's pinned 30 Hz, so the partition separates
# "rooted in place" from "moving at all" rather than "moving slowly".
STILL_TRANSLATION = 1.0e-3   # source units between draws
STILL_ROTATION = 1.0e-3      # degrees between draws

# How far from a great-circle arc the drawn orientation may sit and still be
# called on it. The excellent rotation band sets the floor; the relative term
# keeps a 20-degree triangle from being judged by a 0.05-degree ruler.
ARC_ABSOLUTE_DEGREES = BANDS["rotation_degrees"][0]
ARC_RELATIVE = 0.02

# Below this a triangle carries no signal — the previous draw and the current
# target already agree, so nothing distinguishes lag from overshoot.
ARC_MINIMUM_DEGREES = 0.05

# A component of the signed error vector below this is noise for the purpose of
# counting sign changes; counting them would report float32 jitter as
# oscillation.
SIGN_DEADBAND_DEGREES = 0.01

# How close to its own largest value a divergence sits to count as pinned there.
# Far above the float32 noise a captured `matrix3x4_t` carries and far below the
# excellent band, so the share it reports is neither jitter nor a wide band
# called narrow.
PLATEAU_DEGREES = 1.0e-3

DECILES = 10

# The residual is a small named set rather than a ranking, so the whole of it is
# reported. A truncated list would read as "these are the bones" when it is only
# the worst of them.
MAX_REPORTED_BONES = 256
MAX_REPORTED_SERIES = 24


# A population whose non-zero value means a comparison read the wrong values.
DEFECTS = (
    "entity_image_absent",
    "captured_image_capped",
    "spine_skeleton_disagrees_with_the_image",
    "skeleton_parent_out_of_range",
    "skeleton_parent_not_topological",
    "payload_width_disagrees_with_bone_count",
    "root_transform_bytes_not_48",
    "pose_to_bone_absent",
    "payload_identity_missing",
    "stream_qpc_frequencies_disagree",
)

# A population whose non-zero value is a recorded property of the runtime, of
# the corpus, or of this pass's scope, with the reason it is.
ACCOUNTED = {
    "procedural_rule_unreadable": (
        "A bone declares a procedural rule whose table does not fit its image, "
        "or names a control bone or axis out of range. The bone composes "
        "ordinarily instead, so the residual set overstates what the rule "
        "explains rather than reading the wrong bytes."
    ),
    "records_over_the_band_under_the_complete_rule": (
        "The residual this pass exists to explain: paired records the split "
        "hierarchy and the `ProcType == 1` correction together do not "
        "reproduce. It is the subject, not a fault of the instrument."
    ),
    "bones_outside_the_selected_mask": (
        "Slots the composed-pose stage never wrote, seeded from retail's own "
        "bone-to-world in the residual pass and never scored in the series."
    ),
    "bones_seeded_from_retail_because_their_parent_was_unselected": (
        "A selected bone whose parent retail did not write composes off "
        "retail's own value for that parent, which is what keeps the residual "
        "pass fed retail's input for its stage."
    ),
    "bones_the_renderer_never_wrote": (
        "A bone whose captured matrix is singular: the slot holds zeros, so "
        "both metrics read as numbers that are not comparisons and the bone "
        "leaves them."
    ),
    "root_transform_not_rigid": (
        "The composed-pose stage was handed a root argument that is not a "
        "rotation and a translation. There is nothing to place a pose into and "
        "no motion to difference, so the sample is excluded rather than "
        "inverted."
    ),
    "draws_whose_consumed_pose_build_produced_no_composed_pose": (
        "The bone cache: a second draw for an actor already posed this frame "
        "reaches the renderer without a pose build running, so there is no "
        "composed pose to pair it with."
    ),
    "draws_whose_carry_generation_names_another_actor": (
        "`carry_generation` is the last pose build seen on the drawing thread "
        "rather than a per-actor link, so a draw can name a build belonging to "
        "another entity or model. The fixed renderable offset and a matching "
        "model exclude them."
    ),
    "draws_collapsed_onto_one_pose_build": (
        "Several draws of one actor consuming one pose build carry the same "
        "matrices, so the series keeps the first and counts the rest. Keeping "
        "them would place repeated samples a zero interval apart and report "
        "that interval's rate as infinite."
    ),
    "series_openings_with_no_previous_draw": (
        "The first sample of each actor's series has no predecessor, so it "
        "carries no motion interval and no previous orientation. It is "
        "excluded from both steps and counted."
    ),
    "samples_where_the_pose_build_did_not_select_the_bone": (
        "The bone's local slot holds whatever the builder last left there, so "
        "there is no target to difference against. The sample still enters the "
        "series for its drawn matrix and its root frame."
    ),
    "residual_bones_with_no_parent": (
        "A residual bone at the root of its skeleton has no parent matrix to "
        "form a target from, so the whole comparison this pass makes is "
        "undefined for it."
    ),
    "samples_excluded_by_the_bone_bound": (
        "`--max-bones` was given, so the reported bone set is a bound the "
        "operator chose rather than the residual."
    ),
}


# --------------------------------------------------------------------------
# Support
# --------------------------------------------------------------------------


def qpc_frequency(connection: sqlite3.Connection) -> tuple[int, int]:
    """The capture's tick rate, and how many distinct rates the streams claim.

    Every stream is stamped by one process against one performance counter, so
    a second rate would mean the header is being read at the wrong offset and
    every interval in this report would be scaled by the wrong constant.
    """
    rates = {
        int(header["qpc_frequency"])
        for header in stream_headers(connection).values()
        if header.get("qpc_frequency")
    }
    return (max(rates) if rates else 0), max(len(rates) - 1, 0)


def _stream(
    connection: sqlite3.Connection, sql: str, batch: int
) -> Iterator[list[tuple]]:
    cursor = connection.execute(sql)
    while True:
        rows = cursor.fetchmany(batch)
        if not rows:
            return
        yield rows


def prepare(connection: sqlite3.Connection, flags: dict[str, Any]) -> dict[str, int]:
    """The draw-to-pose-build pairing, per record and grouped, in one pass.

    Both tables come from `verify_transform_difference`'s join and mean the same
    thing: a draw names a pose build only when the generation spine, the fixed
    renderable offset and the model all agree. `pair` keeps one row per draw
    because a series needs its order; `pair_group` deduplicates by payload
    identity because the residual scan is a function of the bytes alone.
    """
    table = flags["event_table"]
    key = "payload_id" if flags["carries_payload_store"] else "id"

    connection.execute(
        f"""
        CREATE TEMP TABLE pair AS
        SELECT d.id AS draw_id, d.sequence_number AS draw_sequence,
               d.qpc AS draw_qpc, f.checksum AS checksum,
               f.bone_count AS bone_count, f.client_entity AS actor,
               f.generation AS generation,
               f.{key} AS local_key, d.{key} AS draw_key
        FROM {table} d
        JOIN {table} f
          ON f.generation = d.carry_generation AND f.kind = '{COMPOSED_KIND}'
        WHERE d.kind = '{DRAW_KIND}'
          AND d.client_entity = f.client_entity + {RENDERABLE_OFFSET}
          AND d.checksum = f.checksum
          AND d.bone_count = f.bone_count
        """
    )
    connection.execute(
        """
        CREATE TEMP TABLE pair_group AS
        SELECT checksum, bone_count, local_key, draw_key,
               count(*) AS records, min(draw_id) AS exemplar
        FROM pair
        GROUP BY checksum, bone_count, local_key, draw_key
        """
    )
    connection.execute("CREATE INDEX temp.pair_model ON pair(checksum, actor)")

    def scalar(sql: str) -> int:
        return int(connection.execute(sql).fetchone()[0] or 0)

    draws = scalar(f"SELECT count(*) FROM {table} WHERE kind = '{DRAW_KIND}'")
    joined = scalar(
        f"""
        SELECT count(*) FROM {table} d
        JOIN {table} f
          ON f.generation = d.carry_generation AND f.kind = '{COMPOSED_KIND}'
        WHERE d.kind = '{DRAW_KIND}'
        """
    )
    paired = scalar("SELECT count(*) FROM pair")
    return {
        "draws": draws,
        "draws_paired": paired,
        "draws_whose_consumed_pose_build_produced_no_composed_pose": draws - joined,
        "draws_whose_carry_generation_names_another_actor": joined - paired,
    }


def _payload_join(flags: dict[str, Any], alias: str, key: str) -> tuple[str, str]:
    """How to reach a payload's bytes, with or without the content store."""
    if flags["carries_payload_store"]:
        return (
            f"JOIN payloads {alias} ON {alias}.id = g.{key}",
            f"{alias}.bytes",
        )
    return (
        f"JOIN {flags['event_table']} {alias} ON {alias}.id = g.{key}",
        f"{alias}.raw_payload",
    )


# --------------------------------------------------------------------------
# Rotation arithmetic beyond the shared library
# --------------------------------------------------------------------------


def _orthonormal(matrix: np.ndarray) -> np.ndarray:
    rows = matrix[..., :3]
    return rows / np.maximum(np.linalg.norm(rows, axis=-1, keepdims=True), 1.0e-30)


def relative_rotation_vector(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """The rotation taking `a`'s frame to `b`'s, as a vector in `a`'s frame.

    Degrees along a unit axis, so a magnitude is the same angle
    `decoder_pose.matrix_rotation_angle_degrees` reports and a direction is what
    tells a solve that always leans one way from one that oscillates. Rows are
    scaled to unit length first for that function's reason: `arccos` near one is
    violently sensitive to the float32 scale a captured `matrix3x4_t` carries.
    """
    relative = np.swapaxes(_orthonormal(a), -1, -2) @ _orthonormal(b)
    trace = np.trace(relative, axis1=-2, axis2=-1)
    angle = np.arccos(np.clip((trace - 1.0) * 0.5, -1.0, 1.0))
    skew = np.stack(
        [
            relative[..., 2, 1] - relative[..., 1, 2],
            relative[..., 0, 2] - relative[..., 2, 0],
            relative[..., 1, 0] - relative[..., 0, 1],
        ],
        axis=-1,
    )
    norm = np.linalg.norm(skew, axis=-1, keepdims=True)
    axis = skew / np.maximum(norm, 1.0e-30)
    return axis * np.degrees(angle)[..., None]


def average_ranks(values: np.ndarray) -> np.ndarray:
    """Ranks with ties averaged, which is what makes Spearman a rank statistic.

    A capture repeats values exactly — a rooted actor's motion is bit-identical
    zero on thousands of draws — so leaving ties to `argsort` order would invent
    an ordering the data does not carry and read as a correlation.
    """
    values = np.asarray(values, dtype=np.float64)
    order = np.argsort(values, kind="stable")
    ordered = values[order]
    ranks = np.empty(values.size, dtype=np.float64)
    start = 0
    for index in range(1, values.size + 1):
        if index == values.size or ordered[index] != ordered[start]:
            ranks[order[start:index]] = 0.5 * (start + index - 1)
            start = index
    return ranks


def pearson(x: np.ndarray, y: np.ndarray) -> float | None:
    """Linear correlation, or None where either side carries no variation."""
    x = np.asarray(x, dtype=np.float64)
    y = np.asarray(y, dtype=np.float64)
    if x.size < 3:
        return None
    dx = x - x.mean()
    dy = y - y.mean()
    denominator = math.sqrt(float(dx @ dx) * float(dy @ dy))
    if denominator <= 0.0:
        return None
    return round(float(dx @ dy) / denominator, 6)


def spearman(x: np.ndarray, y: np.ndarray) -> float | None:
    if np.asarray(x).size < 3:
        return None
    return pearson(average_ranks(x), average_ranks(y))


def plateau(values: np.ndarray) -> dict[str, Any]:
    """How much of a divergence sits pinned at one level, and how tightly.

    A solve lands where its inputs put it and spreads; a limit lands on its own
    ceiling and stays there. The share of samples within `PLATEAU_DEGREES` of the
    largest value separates the two, and the spread across those samples says
    how exact the ceiling is — a number reproduced to five decimal places on
    hundreds of samples is authored rather than computed.
    """
    values = np.asarray(values, dtype=np.float64)
    if values.size == 0:
        return {"level": None, "samples_at_the_level": 0, "share": None,
                "spread_at_the_level": None}
    level = float(values.max())
    at = values >= level - PLATEAU_DEGREES
    return {
        "level": level,
        "samples_at_the_level": int(at.sum()),
        "share": round(float(at.mean()), 6),
        "spread_at_the_level": float(level - values[at].min()),
    }


def distribution(values: np.ndarray) -> dict[str, Any]:
    values = np.asarray(values, dtype=np.float64)
    if values.size == 0:
        return {"samples": 0, "median": None, "p90": None, "max": None}
    return {
        "samples": int(values.size),
        "median": float(np.median(values)),
        "p90": float(np.quantile(values, 0.9)),
        "max": float(values.max()),
    }


def signed_distribution(values: np.ndarray) -> dict[str, Any]:
    """A distribution that keeps its sign, for a quantity whose sign is the point."""
    values = np.asarray(values, dtype=np.float64)
    if values.size == 0:
        return {"samples": 0, "median": None, "min": None, "max": None,
                "share_negative": None}
    return {
        "samples": int(values.size),
        "median": float(np.median(values)),
        "min": float(values.min()),
        "max": float(values.max()),
        "share_negative": round(float((values < 0.0).mean()), 6),
    }


# --------------------------------------------------------------------------
# Pass one — which bones the complete rule leaves over the band
# --------------------------------------------------------------------------


def residual_bones(
    connection: sqlite3.Connection,
    flags: dict[str, Any],
    models: dict[int, Skeleton],
    names: dict[int, str],
    *,
    batch: int,
) -> dict[str, Any]:
    """Compose the whole corpus under the complete rule and keep what misses.

    One candidate rather than three: this pass is not pricing the export, it is
    locating a stage. Everything else follows `verify_transform_difference`'s
    stage three — retail's own composed locals, the captured root frame,
    unselected slots seeded from retail's own bone-to-world.
    """
    counts = {
        "entity_image_absent": 0,
        "payload_width_disagrees_with_bone_count": 0,
        "payload_identity_missing": 0,
        "root_transform_bytes_not_48": 0,
        "root_transform_not_rigid": 0,
        "bones_outside_the_selected_mask": 0,
        "bones_the_renderer_never_wrote": 0,
        "bones_seeded_from_retail_because_their_parent_was_unselected": 0,
        "records_over_the_band_under_the_complete_rule": 0,
    }
    rotation_over: dict[tuple[int, int], int] = {}
    translation_over: dict[tuple[int, int], int] = {}
    observations: dict[tuple[int, int], int] = {}
    rotation_max: dict[tuple[int, int], float] = {}
    translation_max: dict[tuple[int, int], float] = {}

    local_join, local_column = _payload_join(flags, "lp", "local_key")
    draw_join, draw_column = _payload_join(flags, "dp", "draw_key")
    sql = (
        f"SELECT g.checksum, g.bone_count, g.records, {local_column}, {draw_column} "
        f"FROM pair_group g {local_join} {draw_join} "
        f"ORDER BY g.checksum, g.local_key, g.draw_key"
    )
    started = time.monotonic()
    records = 0
    groups = 0
    for rows in _stream(connection, sql, batch):
        by_model: dict[tuple[int, int], list[tuple]] = {}
        for checksum, bones, count, local, draw in rows:
            checksum, bones, count = int(checksum), int(bones), int(count)
            if local is None or draw is None:
                counts["payload_identity_missing"] += count
                continue
            if checksum not in models:
                counts["entity_image_absent"] += count
                continue
            if models[checksum].bones != bones:
                counts["payload_width_disagrees_with_bone_count"] += count
                continue
            if len(draw) != bones * MATRIX_BYTES * 2:
                counts["payload_width_disagrees_with_bone_count"] += count
                continue
            expected = bones * 7 * 4 + ((bones + 31) // 32) * 4
            if len(local) == expected:
                counts["root_transform_bytes_not_48"] += count
                continue
            if len(local) != expected + ROOT_TRANSFORM_BYTES:
                counts["payload_width_disagrees_with_bone_count"] += count
                continue
            by_model.setdefault((checksum, bones), []).append((count, local, draw))

        for (checksum, bones), entries in by_model.items():
            skeleton = models[checksum]
            decoded = [
                decoder_pose.payload_local_pose(blob, bones) for _, blob, _ in entries
            ]
            positions = np.stack([pair[0] for pair in decoded])
            quaternions = np.stack([pair[1] for pair in decoded])
            selected = np.stack(
                [decoder_pose.payload_selected(blob, bones) for _, blob, _ in entries]
            )
            root_offset = bones * 7 * 4 + ((bones + 31) // 32) * 4
            root = np.stack(
                [
                    decoder_pose.payload_matrices(blob, 1, offset=root_offset)[0]
                    for _, blob, _ in entries
                ]
            )
            retail = np.stack(
                [decoder_pose.payload_matrices(blob, bones) for _, _, blob in entries]
            )
            multiplier = np.array([count for count, _, _ in entries], dtype=np.int64)

            rigid = (
                decoder_pose.row_length_error(root) <= BANDS["row_length"][1]
            ) & (np.abs(np.linalg.det(root[..., :3])) > 0.5)
            if not rigid.all():
                counts["root_transform_not_rigid"] += int(multiplier[~rigid].sum())
                keep = np.flatnonzero(rigid)
                if keep.size == 0:
                    continue
                positions, quaternions = positions[keep], quaternions[keep]
                selected, root, retail = selected[keep], root[keep], retail[keep]
                multiplier = multiplier[keep]
                entries = [entries[index] for index in keep]

            counts["bones_outside_the_selected_mask"] += int(
                (multiplier[:, None] * (~selected)).sum()
            )
            live = selected & decoder_pose.is_frame(retail)
            counts["bones_the_renderer_never_wrote"] += int(
                (multiplier[:, None] * (selected & ~live)).sum()
            )
            parent = skeleton.parent
            has_parent = parent >= 0
            unselected_parent = np.zeros_like(selected)
            unselected_parent[:, has_parent] = (
                selected[:, has_parent] & ~selected[:, parent[has_parent]]
            )
            counts[
                "bones_seeded_from_retail_because_their_parent_was_unselected"
            ] += int((multiplier[:, None] * unselected_parent).sum())

            world = decoder_pose.compose(
                decoder_pose.matrices_from_local(positions, quaternions),
                skeleton,
                root,
                split=RESIDUAL_SPLIT,
                seed=retail,
                selected=selected,
                procedural=RESIDUAL_PROCEDURAL,
            )
            rotation = np.where(
                live, decoder_pose.matrix_rotation_angle_degrees(world, retail), 0.0
            )
            translation = np.where(
                live, decoder_pose.translation_error(world, retail), 0.0
            )
            over_rotation = rotation > BANDS["rotation_degrees"][0]
            over_translation = translation > BANDS["world_translation"][0]
            over = over_rotation | over_translation
            counts["records_over_the_band_under_the_complete_rule"] += int(
                multiplier[over.any(axis=1)].sum()
            )
            groups += len(entries)
            records += int(multiplier.sum())
            for bone in np.flatnonzero(over.any(axis=0)):
                bone = int(bone)
                key = (checksum, bone)
                observations[key] = observations.get(key, 0) + int(
                    (multiplier * live[:, bone]).sum()
                )
                rotation_over[key] = rotation_over.get(key, 0) + int(
                    (multiplier * over_rotation[:, bone]).sum()
                )
                translation_over[key] = translation_over.get(key, 0) + int(
                    (multiplier * over_translation[:, bone]).sum()
                )
                rotation_max[key] = max(
                    rotation_max.get(key, 0.0), float(rotation[:, bone].max())
                )
                translation_max[key] = max(
                    translation_max.get(key, 0.0), float(translation[:, bone].max())
                )

    rows = []
    for (checksum, bone), total in observations.items():
        skeleton = models[checksum]
        parent = int(skeleton.parent[bone])
        rows.append(
            {
                "population": (
                    PROP_BONE
                    if skeleton.names[bone] in PROP_BONE_NAMES
                    else CLOTH_AND_HAIR
                ),
                "checksum": f"0x{checksum:08x}",
                "model": names.get(checksum, ""),
                "bone": bone,
                "bone_name": skeleton.names[bone],
                "parent": parent,
                "parent_name": skeleton.names[parent] if parent >= 0 else "",
                "depth": int(skeleton.depth[bone]),
                "flags": f"0x{int(skeleton.flags[bone]):04x}",
                "proc_type": int(skeleton.proc_type[bone]),
                "bone_observations": total,
                "rotation_over_the_band": rotation_over.get((checksum, bone), 0),
                "translation_over_the_band": translation_over.get((checksum, bone), 0),
                "rotation_degrees_max": rotation_max.get((checksum, bone), 0.0),
                "translation_max": translation_max.get((checksum, bone), 0.0),
            }
        )
    rows.sort(
        key=lambda row: (
            -max(row["rotation_over_the_band"], row["translation_over_the_band"]),
            -row["rotation_degrees_max"],
        )
    )
    return {
        "available": bool(records),
        "reason": "" if records else "no draw paired with a composed pose",
        "rule": (
            "A.4a split inheritance plus the ProcType == 1 correction, "
            "retail's own composed locals, the captured root frame, unselected "
            "slots seeded from retail"
        ),
        "records": records,
        "distinct_payload_keys": groups,
        "elapsed_seconds": round(time.monotonic() - started, 3),
        "bones": rows,
        "counts": counts,
    }


# --------------------------------------------------------------------------
# Pass two — one ordered series per actor
# --------------------------------------------------------------------------


class Series:
    """One actor's paired draws in global sequence order, and what they carry.

    Everything below is a slice of retail's own capture. `target` is the only
    computed quantity, and it is computed from retail's captured parent matrix
    times retail's captured local — never from our own composed parent — so a
    bone in a chain reports its own divergence rather than its chain root's.
    """

    def __init__(self, checksum: int, actor: int, bones: tuple[int, ...]) -> None:
        self.checksum = checksum
        self.actor = actor
        self.bones = bones
        self.sequence: list[int] = []
        self.qpc: list[int] = []
        self.root: list[np.ndarray] = []
        self.drawn: list[np.ndarray] = []     # (b, 3, 4) retail, per residual bone
        self.parent: list[np.ndarray] = []    # (b, 3, 4) retail, the bone's parent
        self.local: list[np.ndarray] = []     # (b, 3, 4) retail's composed local
        self.selected: list[np.ndarray] = []  # (b,) bool
        self.live: list[np.ndarray] = []      # (b,) bool

    def add(
        self,
        sequence: int,
        qpc: int,
        root: np.ndarray,
        drawn: np.ndarray,
        parent: np.ndarray,
        local: np.ndarray,
        selected: np.ndarray,
        live: np.ndarray,
    ) -> None:
        self.sequence.append(sequence)
        self.qpc.append(qpc)
        self.root.append(root)
        self.drawn.append(drawn)
        self.parent.append(parent)
        self.local.append(local)
        self.selected.append(selected)
        self.live.append(live)

    def stacked(self) -> dict[str, np.ndarray]:
        parent = np.stack(self.parent)
        local = np.stack(self.local)
        return {
            "sequence": np.array(self.sequence, dtype=np.int64),
            "qpc": np.array(self.qpc, dtype=np.int64),
            "root": np.stack(self.root),
            "drawn": np.stack(self.drawn),
            "parent": parent,
            "local": local,
            "target": decoder_pose.multiply(parent, local),
            "selected": np.stack(self.selected),
            "live": np.stack(self.live),
        }


def build_series(
    connection: sqlite3.Connection,
    flags: dict[str, Any],
    models: dict[int, Skeleton],
    wanted: dict[int, tuple[int, ...]],
    *,
    batch: int,
) -> tuple[dict[tuple[int, int], Series], dict[str, int]]:
    """Read the paired draws for the residual models, in order, once each."""
    counts = {
        "draws_collapsed_onto_one_pose_build": 0,
        "root_transform_not_rigid": 0,
        "payload_identity_missing": 0,
        "payload_width_disagrees_with_bone_count": 0,
        "residual_bones_with_no_parent": 0,
    }
    series: dict[tuple[int, int], Series] = {}
    if not wanted:
        return series, counts

    checksums = ", ".join(str(checksum) for checksum in sorted(wanted))
    local_join, local_column = _payload_join(flags, "lp", "local_key")
    draw_join, draw_column = _payload_join(flags, "dp", "draw_key")
    sql = (
        f"SELECT g.checksum, g.bone_count, g.actor, g.draw_sequence, g.draw_qpc, "
        f"g.generation, {local_column}, {draw_column} "
        f"FROM pair g {local_join} {draw_join} "
        f"WHERE g.checksum IN ({checksums}) "
        f"ORDER BY g.checksum, g.actor, g.draw_sequence"
    )
    parent_index = {
        checksum: np.array(
            [int(models[checksum].parent[bone]) for bone in indices], dtype=np.int64
        )
        for checksum, indices in wanted.items()
    }
    bone_index = {
        checksum: np.array(indices, dtype=np.int64)
        for checksum, indices in wanted.items()
    }
    seen: dict[tuple[int, int], int] = {}
    for rows in _stream(connection, sql, batch):
        for checksum, bones, actor, sequence, qpc, generation, local, draw in rows:
            checksum, bones, actor = int(checksum), int(bones), int(actor)
            sequence, generation = int(sequence), int(generation)
            key = (checksum, actor)
            # A pose build drawn several times carries one set of matrices, so
            # the repeats are counted rather than resampled: they would sit a
            # zero interval apart and make every rate infinite.
            if seen.get(key) == generation:
                counts["draws_collapsed_onto_one_pose_build"] += 1
                continue
            seen[key] = generation
            if local is None or draw is None:
                counts["payload_identity_missing"] += 1
                continue
            expected = bones * 7 * 4 + ((bones + 31) // 32) * 4
            if (
                models[checksum].bones != bones
                or len(draw) != bones * MATRIX_BYTES * 2
                or len(local) != expected + ROOT_TRANSFORM_BYTES
            ):
                counts["payload_width_disagrees_with_bone_count"] += 1
                continue

            root = decoder_pose.payload_matrices(local, 1, offset=expected)[0]
            if (
                decoder_pose.row_length_error(root) > BANDS["row_length"][1]
                or abs(float(np.linalg.det(root[:3, :3]))) <= 0.5
            ):
                counts["root_transform_not_rigid"] += 1
                continue
            world = decoder_pose.payload_matrices(draw, bones)
            position, quaternion = decoder_pose.payload_local_pose(local, bones)
            mask = decoder_pose.payload_selected(local, bones)
            chosen = bone_index[checksum]
            parent_world = world[parent_index[checksum]]
            if key not in series:
                series[key] = Series(checksum, actor, wanted[checksum])
            series[key].add(
                sequence,
                int(qpc),
                root,
                world[chosen],
                parent_world,
                decoder_pose.matrices_from_local(
                    position[chosen], quaternion[chosen]
                ),
                mask[chosen],
                decoder_pose.is_frame(world[chosen])
                & decoder_pose.is_frame(parent_world),
            )
    return series, counts


# --------------------------------------------------------------------------
# Step one — the divergence against the actor's own motion
# --------------------------------------------------------------------------


PREDICTORS = (
    "root_translation_delta",
    "root_rotation_degrees_delta",
    "root_translation_rate",
    "root_rotation_degrees_rate",
    "parent_translation_delta",
    "parent_rotation_degrees_delta",
)


def _deciles(predictor: np.ndarray, response: np.ndarray) -> list[dict[str, Any]]:
    """The response's distribution inside each tenth of the predictor's rank.

    A single correlation reports a monotone relationship badly when the response
    is flat over most of the range and only lifts at the top, which is what a
    threshold looks like. The buckets show that shape without assuming it.
    """
    if predictor.size < DECILES:
        return []
    order = np.argsort(average_ranks(predictor), kind="stable")
    edges = np.linspace(0, predictor.size, DECILES + 1).astype(int)
    rows = []
    for index in range(DECILES):
        chunk = order[edges[index] : edges[index + 1]]
        if chunk.size == 0:
            continue
        rows.append(
            {
                "decile": index + 1,
                "predictor_upper": float(predictor[chunk].max()),
                **distribution(response[chunk]),
            }
        )
    return rows


def step_one(
    series: dict[tuple[int, int], Series],
    models: dict[int, Skeleton],
    names: dict[int, str],
    frequency: int,
) -> dict[str, Any]:
    counts = {
        "series_openings_with_no_previous_draw": 0,
        "samples_where_the_pose_build_did_not_select_the_bone": 0,
        "bones_the_renderer_never_wrote": 0,
    }
    rows = []
    for (checksum, actor), holder in sorted(series.items()):
        data = holder.stacked()
        # Every series opens on a sample with no predecessor, so it carries no
        # interval; a one-sample series is nothing but that opening.
        counts["series_openings_with_no_previous_draw"] += 1
        if data["sequence"].size < 2:
            continue
        skeleton = models[checksum]

        root = data["root"]
        previous = slice(0, -1)
        current = slice(1, None)
        root_translation = np.linalg.norm(
            root[current][..., 3] - root[previous][..., 3], axis=-1
        )
        root_rotation = decoder_pose.matrix_rotation_angle_degrees(
            root[previous], root[current]
        )
        # Ticks to seconds by the streams' own performance-counter rate. A
        # repeated draw would have made this zero, which is why the series
        # collapses them before it gets here; the clamp is the floor on a
        # counter that can still read one tick apart.
        seconds = (
            np.maximum(
                (data["qpc"][current] - data["qpc"][previous]).astype(np.float64), 1.0
            )
            / float(frequency or 1)
        )

        parent = data["parent"]
        parent_translation = np.linalg.norm(
            parent[current][..., 3] - parent[previous][..., 3], axis=-1
        )
        parent_rotation = decoder_pose.matrix_rotation_angle_degrees(
            parent[previous], parent[current]
        )
        divergence = decoder_pose.matrix_rotation_angle_degrees(
            data["target"], data["drawn"]
        )
        scored = data["selected"] & data["live"]
        counts["samples_where_the_pose_build_did_not_select_the_bone"] += int(
            (~data["selected"]).sum()
        )
        counts["bones_the_renderer_never_wrote"] += int(
            (data["selected"] & ~data["live"]).sum()
        )

        for column, bone in enumerate(holder.bones):
            keep = np.flatnonzero(scored[current, column])
            if keep.size == 0:
                continue
            response = divergence[current, column][keep]
            predictors = {
                "root_translation_delta": root_translation[keep],
                "root_rotation_degrees_delta": root_rotation[keep],
                "root_translation_rate": root_translation[keep] / seconds[keep],
                "root_rotation_degrees_rate": root_rotation[keep] / seconds[keep],
                "parent_translation_delta": parent_translation[keep, column],
                "parent_rotation_degrees_delta": parent_rotation[keep, column],
            }
            still = (
                (predictors["root_translation_delta"] <= STILL_TRANSLATION)
                & (predictors["root_rotation_degrees_delta"] <= STILL_ROTATION)
            )
            parent_still = (
                (predictors["parent_translation_delta"] <= STILL_TRANSLATION)
                & (predictors["parent_rotation_degrees_delta"] <= STILL_ROTATION)
            )
            band = BANDS["rotation_degrees"][0]
            rows.append(
                {
                    "population": (
                        PROP_BONE
                        if skeleton.names[bone] in PROP_BONE_NAMES
                        else CLOTH_AND_HAIR
                    ),
                    "model": names.get(checksum, ""),
                    "checksum": f"0x{checksum:08x}",
                    "actor": f"0x{actor:08x}",
                    "bone_name": skeleton.names[bone],
                    "scored_samples": int(keep.size),
                    "divergence": distribution(response),
                    "divergence_plateau": plateau(response),
                    # Which way the correction leans. These rigs run their
                    # chains along each bone's own +X, so the world Z component
                    # of that axis is how high the bone points; drawn minus
                    # target is therefore signed, and a consistently negative
                    # value means retail draws the bone lower than composition
                    # puts it. A direction the whole population shares is a
                    # different kind of fact from a magnitude it shares.
                    "world_up_component_drawn_minus_target": signed_distribution(
                        data["drawn"][current][keep, column, 2, 0]
                        - data["target"][current][keep, column, 2, 0]
                    ),
                    # Whether any clip touches the bone at all. A local equal to
                    # bind on every sample means the divergence cannot come from
                    # animation, because there is no animation on it.
                    "local_against_bind": {
                        "position": distribution(
                            np.linalg.norm(
                                data["local"][current][keep, column, :, 3]
                                - skeleton.bind_position[bone],
                                axis=-1,
                            )
                        ),
                        "rotation_degrees": distribution(
                            decoder_pose.matrix_rotation_angle_degrees(
                                data["local"][current][keep, column],
                                decoder_pose.matrices_from_local(
                                    skeleton.bind_position[bone],
                                    skeleton.bind_quaternion[bone],
                                ),
                            )
                        ),
                    },
                    "spearman": {
                        name: spearman(value, response)
                        for name, value in predictors.items()
                    },
                    "pearson": {
                        name: pearson(value, response)
                        for name, value in predictors.items()
                    },
                    "predictor_spread": {
                        name: distribution(value) for name, value in predictors.items()
                    },
                    "actor_still": {
                        **distribution(response[still]),
                        "over_the_band": int((response[still] > band).sum()),
                    },
                    "actor_moving": {
                        **distribution(response[~still]),
                        "over_the_band": int((response[~still] > band).sum()),
                    },
                    "parent_still": {
                        **distribution(response[parent_still]),
                        "over_the_band": int((response[parent_still] > band).sum()),
                    },
                    "parent_moving": {
                        **distribution(response[~parent_still]),
                        "over_the_band": int((response[~parent_still] > band).sum()),
                    },
                    "deciles": {
                        name: _deciles(value, response)
                        for name, value in predictors.items()
                    },
                }
            )
    rows.sort(key=lambda row: -row["scored_samples"])
    return {
        "available": bool(rows),
        "reason": "" if rows else "no residual bone reached a scored sample",
        "predictors": list(PREDICTORS),
        "still_thresholds": {
            "translation": STILL_TRANSLATION,
            "rotation_degrees": STILL_ROTATION,
        },
        "response": (
            "the rotation between retail's drawn bone-to-world and retail's own "
            "captured parent matrix times retail's captured local, in degrees"
        ),
        "by_bone": rows[:MAX_REPORTED_BONES],
        "by_bone_truncated": max(len(rows) - MAX_REPORTED_BONES, 0),
        "counts": counts,
    }


# --------------------------------------------------------------------------
# Step two — the divergence against the previous draw
# --------------------------------------------------------------------------


ALTERNATIVE_SOURCES = (
    "parent_now_times_local",
    "root_now_times_local",
    "parent_previous_times_local",
    "drawn_previously",
)


def step_two(
    series: dict[tuple[int, int], Series],
    models: dict[int, Skeleton],
    names: dict[int, str],
) -> dict[str, Any]:
    """Where retail's drawn orientation sits relative to the previous draw's.

    Three orientations bound each sample: `P`, the previous draw's; `T`, the one
    composition targets now; and `C`, the one retail drew. On a great-circle arc
    the three angles between them satisfy one of three additions, and which one
    holds is the question:

        |CP| + |CT| = |PT|   C between P and T   — a first-order lag
        |PT| + |CT| = |CP|   T between P and C   — the stage overshot the target
        |PT| + |CP| = |CT|   P between T and C   — the previous draw is the mix

    None holding means the three do not lie on one arc, so the divergence is not
    a scalar blend of the previous orientation and the current target at all and
    neither hypothesis applies.

    A first-order lag additionally predicts `|CT| = (1 - alpha) * |PT|` with one
    constant `alpha`, which is a line through the origin. An under-damped spring
    predicts a signed error that changes sign as it settles. Both are reported,
    because the arc test alone cannot tell a lag from a solve that happens to
    undershoot.

    The three classes overlap where a triangle degenerates — with `P` and `C`
    already equal, both the first and the third addition hold — so they are
    counted independently rather than partitioned, and `off_arc` counts only
    the samples none of them covers.
    """
    counts: dict[str, int] = {}
    rows = []
    sources = []
    for (checksum, actor), holder in sorted(series.items()):
        data = holder.stacked()
        if data["sequence"].size < 2:
            continue
        skeleton = models[checksum]
        previous = slice(0, -1)
        current = slice(1, None)

        drawn_now = data["drawn"][current]
        drawn_before = data["drawn"][previous]
        target_now = data["target"][current]
        scored = (data["selected"] & data["live"])[current] & data["live"][previous]

        a_prev = decoder_pose.matrix_rotation_angle_degrees(drawn_now, drawn_before)
        a_target = decoder_pose.matrix_rotation_angle_degrees(drawn_now, target_now)
        a_total = decoder_pose.matrix_rotation_angle_degrees(drawn_before, target_now)
        error = relative_rotation_vector(target_now, drawn_now)

        # Candidate orientations for the drawn bone, each fed retail's own
        # matrices. If one of them reproduces retail inside the excellent band
        # the stage is named and nothing further needs to be inferred.
        local_now = data["local"][current]
        candidates = {
            "parent_now_times_local": a_target,
            "root_now_times_local": decoder_pose.matrix_rotation_angle_degrees(
                decoder_pose.multiply(data["root"][current][:, None], local_now),
                drawn_now,
            ),
            "parent_previous_times_local": decoder_pose.matrix_rotation_angle_degrees(
                decoder_pose.multiply(data["parent"][previous], local_now),
                drawn_now,
            ),
            "drawn_previously": a_prev,
        }
        translation = decoder_pose.translation_error(target_now, drawn_now)

        for column, bone in enumerate(holder.bones):
            keep = np.flatnonzero(scored[:, column])
            if keep.size == 0:
                continue
            prev = a_prev[keep, column]
            target = a_target[keep, column]
            total = a_total[keep, column]
            tolerance = np.maximum(
                ARC_ABSOLUTE_DEGREES,
                ARC_RELATIVE * np.maximum.reduce([prev, target, total]),
            )
            signal = np.maximum.reduce([prev, target, total]) > ARC_MINIMUM_DEGREES
            lag = signal & (np.abs(prev + target - total) <= tolerance)
            overshoot = signal & (np.abs(total + target - prev) <= tolerance)
            behind = signal & (np.abs(total + prev - target) <= tolerance)
            off_arc = signal & ~(lag | overshoot | behind)

            fit = total > ARC_MINIMUM_DEGREES
            slope = None
            r_squared = None
            if int(fit.sum()) >= 3:
                x = total[fit]
                y = target[fit]
                denominator = float(x @ x)
                if denominator > 0.0:
                    slope = float(x @ y) / denominator
                    residual = y - slope * x
                    spread = float(y @ y)
                    r_squared = (
                        1.0 - float(residual @ residual) / spread
                        if spread > 0.0
                        else None
                    )

            # Adjacency matters: an autocorrelation across a gap in the scored
            # samples would compare orientations many draws apart and report a
            # settling solve as an oscillating one.
            adjacent = keep[1:][np.diff(keep) == 1]
            component = error[keep, column]
            autocorrelation = []
            sign_changes = []
            for axis in range(3):
                values = error[:, column, axis]
                if adjacent.size >= 3:
                    autocorrelation.append(
                        pearson(values[adjacent - 1], values[adjacent])
                    )
                    live_pair = (
                        np.abs(values[adjacent - 1]) > SIGN_DEADBAND_DEGREES
                    ) & (np.abs(values[adjacent]) > SIGN_DEADBAND_DEGREES)
                    flips = int(
                        (
                            np.sign(values[adjacent - 1][live_pair])
                            != np.sign(values[adjacent][live_pair])
                        ).sum()
                    )
                    sign_changes.append(
                        round(flips / live_pair.sum(), 6)
                        if int(live_pair.sum())
                        else None
                    )
                else:
                    autocorrelation.append(None)
                    sign_changes.append(None)
            magnitude = np.linalg.norm(component, axis=-1)
            axis_unit = component / np.maximum(
                magnitude[:, None], 1.0e-30
            )
            resultant = (
                float(np.linalg.norm(axis_unit[magnitude > SIGN_DEADBAND_DEGREES].mean(axis=0)))
                if int((magnitude > SIGN_DEADBAND_DEGREES).sum())
                else None
            )

            rows.append(
                {
                    "population": (
                        PROP_BONE
                        if skeleton.names[bone] in PROP_BONE_NAMES
                        else CLOTH_AND_HAIR
                    ),
                    "model": names.get(checksum, ""),
                    "checksum": f"0x{checksum:08x}",
                    "actor": f"0x{actor:08x}",
                    "bone_name": skeleton.names[bone],
                    "scored_samples": int(keep.size),
                    "adjacent_samples": int(adjacent.size),
                    "samples_carrying_signal": int(signal.sum()),
                    "drawn_against_target_degrees": distribution(target),
                    "drawn_against_previous_degrees": distribution(prev),
                    "previous_against_target_degrees": distribution(total),
                    "arc": {
                        "lag": int(lag.sum()),
                        "overshoot": int(overshoot.sum()),
                        "previous_between": int(behind.sum()),
                        "off_arc": int(off_arc.sum()),
                    },
                    "lag_fit": {
                        "samples": int(fit.sum()),
                        "slope": None if slope is None else round(slope, 6),
                        "alpha": None if slope is None else round(1.0 - slope, 6),
                        "r_squared": None if r_squared is None else round(r_squared, 6),
                    },
                    "signed_error": {
                        "lag1_autocorrelation": autocorrelation,
                        "sign_change_rate": sign_changes,
                        "axis_mean_resultant_length": (
                            None if resultant is None else round(resultant, 6)
                        ),
                        "magnitude": distribution(magnitude),
                    },
                    "held_exactly_from_the_previous_draw": int(
                        (prev <= BANDS["rotation_degrees"][0]).sum()
                    ),
                    "translation_against_the_parent_target": distribution(
                        translation[keep, column]
                    ),
                }
            )
            band = BANDS["rotation_degrees"][0]
            for name, values in candidates.items():
                selected_values = values[keep, column]
                sources.append(
                    {
                        "population": (
                            PROP_BONE
                            if skeleton.names[bone] in PROP_BONE_NAMES
                            else CLOTH_AND_HAIR
                        ),
                        "model": names.get(checksum, ""),
                        "bone_name": skeleton.names[bone],
                        "candidate": name,
                        **distribution(selected_values),
                        "within_the_excellent_band": int(
                            (selected_values <= band).sum()
                        ),
                    }
                )

    rows.sort(key=lambda row: -row["scored_samples"])
    sources.sort(key=lambda row: (row["model"], row["bone_name"], row["candidate"]))
    return {
        "available": bool(rows),
        "reason": "" if rows else "no residual bone reached a scored sample",
        "arc_tolerance": {
            "absolute_degrees": ARC_ABSOLUTE_DEGREES,
            "relative": ARC_RELATIVE,
            "minimum_signal_degrees": ARC_MINIMUM_DEGREES,
        },
        "alternative_rotation_sources": ALTERNATIVE_SOURCES,
        "by_bone": rows[:MAX_REPORTED_BONES],
        "by_bone_truncated": max(len(rows) - MAX_REPORTED_BONES, 0),
        "sources": sources[: MAX_REPORTED_BONES * len(ALTERNATIVE_SOURCES)],
        "sources_truncated": max(
            len(sources) - MAX_REPORTED_BONES * len(ALTERNATIVE_SOURCES), 0
        ),
        "counts": counts,
    }


# --------------------------------------------------------------------------
# Verdict
# --------------------------------------------------------------------------


def decide(flags: dict[str, Any], sections: list[dict[str, Any]]) -> dict[str, Any]:
    if not flags["carries_spine"]:
        return {
            "judgeable": False,
            "secondary_motion_measured": False,
            "statement": (
                "This database carries no join spine; run "
                "`index_capture_database` over it before analysing it."
            ),
        }
    if not (flags["carries_images"] and flags["carries_census"]):
        return {
            "judgeable": False,
            "secondary_motion_measured": False,
            "statement": (
                "This database carries no model images or no census, so no "
                "skeleton can be read for the residual bones."
            ),
        }
    if not (flags["carries_draws"] and flags["carries_generations"]):
        return {
            "judgeable": False,
            "secondary_motion_measured": False,
            "statement": (
                "This database carries no draw records or no generation "
                "columns, so no draw can be tied to the pose build it consumed."
            ),
        }
    defects: dict[str, int] = {}
    accounted: dict[str, int] = {}
    unclassified: dict[str, int] = {}
    for section in sections:
        for name, value in (section.get("counts") or {}).items():
            if not value:
                continue
            if name in DEFECTS:
                defects[name] = defects.get(name, 0) + value
            elif name in ACCOUNTED:
                accounted[name] = accounted.get(name, 0) + value
            else:
                unclassified[name] = unclassified.get(name, 0) + value
    measured = not defects and not unclassified and any(
        section.get("available") for section in sections
    )
    if measured:
        statement = (
            "The residual bones were located by composing the corpus under the "
            "complete offline rule, and their divergence was correlated "
            "against the actor's own motion and differenced against the "
            "previous draw. Every non-zero population is declared. The report "
            "carries correlations; naming the mechanism is not something this "
            "pass can do on its own."
        )
    elif defects:
        statement = (
            "One or more comparisons would have read the wrong values: "
            + ", ".join(sorted(defects))
            + "."
        )
    elif unclassified:
        statement = (
            "A population reached the report that no declaration covers: "
            + ", ".join(sorted(unclassified))
            + ". Declare it as a defect or account for it before reading the "
            "correlations."
        )
    else:
        statement = "No residual bone reached a comparable sample."
    return {
        "judgeable": True,
        "secondary_motion_measured": measured,
        "defects": defects,
        "unclassified": unclassified,
        "accounted": {name: ACCOUNTED[name] for name in sorted(accounted)},
        "accounted_counts": accounted,
        "statement": statement,
    }


# --------------------------------------------------------------------------
# Driver
# --------------------------------------------------------------------------


def analyze(
    session: Path,
    *,
    batch: int = DEFAULT_BATCH,
    max_bones: int | None = None,
) -> dict[str, Any]:
    from elysium_pipeline.formats import mdl_skel

    connection = open_database(session)
    try:
        flags = support(connection)
        metadata = {
            key: json.loads(value)
            for key, value in connection.execute(
                "SELECT key, value FROM capture_metadata WHERE key IN "
                "('map', 'created_utc', 'tool_git', 'index_version')"
            )
        }
        frequency, extra_rates = qpc_frequency(connection)
        module_hashes = modules(connection)
        run: dict[str, Any] = {}
        names: dict[int, str] = {}
        bones_report: dict[str, Any] = {"available": False, "reason": "not reached"}
        pairing: dict[str, Any] = {"available": False, "reason": "not reached"}
        residual: dict[str, Any] = {"available": False, "reason": "not reached"}
        one: dict[str, Any] = {"available": False, "reason": "not reached"}
        two: dict[str, Any] = {"available": False, "reason": "not reached"}
        judgeable = (
            flags["carries_spine"]
            and flags["carries_images"]
            and flags["carries_census"]
            and flags["carries_draws"]
            and flags["carries_generations"]
        )
        if judgeable:
            run = corpus(connection, flags)
            names = owner_names(connection)
            models, bones_report = skeletons(connection, flags)
            counts = prepare(connection, flags)
            pairing = {
                "available": True,
                "reason": "",
                "renderable_offset": RENDERABLE_OFFSET,
                **counts,
                "counts": {
                    name: counts[name]
                    for name in (
                        "draws_whose_consumed_pose_build_produced_no_composed_pose",
                        "draws_whose_carry_generation_names_another_actor",
                    )
                },
            }
            pairing["counts"]["stream_qpc_frequencies_disagree"] = extra_rates
            residual = residual_bones(
                connection, flags, models, names, batch=batch
            )
            wanted: dict[int, list[int]] = {}
            parentless = 0
            excluded = 0
            for index, row in enumerate(residual["bones"]):
                if row["parent"] < 0:
                    parentless += 1
                    continue
                if max_bones is not None and index >= max_bones:
                    excluded += 1
                    continue
                wanted.setdefault(int(row["checksum"], 16), []).append(row["bone"])
            residual["counts"]["residual_bones_with_no_parent"] = parentless
            residual["counts"]["samples_excluded_by_the_bone_bound"] = excluded
            residual["bones_reported"] = len(residual["bones"])
            residual["bones_truncated"] = max(
                len(residual["bones"]) - MAX_REPORTED_BONES, 0
            )
            residual["bones"] = residual["bones"][:MAX_REPORTED_BONES]
            frozen = {
                checksum: tuple(sorted(indices))
                for checksum, indices in wanted.items()
            }
            started = time.monotonic()
            series, series_counts = build_series(
                connection, flags, models, frozen, batch=batch
            )
            one = step_one(series, models, names, frequency)
            two = step_two(series, models, names)
            one["counts"].update(series_counts)
            one["series"] = {
                "models": len(frozen),
                "actors": len(series),
                "samples": sum(len(holder.sequence) for holder in series.values()),
                "qpc_frequency": frequency,
                "elapsed_seconds": round(time.monotonic() - started, 3),
                "longest": sorted(
                    (
                        {
                            "model": names.get(holder.checksum, ""),
                            "actor": f"0x{holder.actor:08x}",
                            "samples": len(holder.sequence),
                            "bones": len(holder.bones),
                        }
                        for holder in series.values()
                    ),
                    key=lambda row: -row["samples"],
                )[:MAX_REPORTED_SERIES],
            }
        verdict = decide(flags, [bones_report, pairing, residual, one, two])
    finally:
        connection.close()
    return {
        "session": session.name,
        "session_path": str(session),
        "database": str(session / DATABASE_NAME),
        "identity": {
            **metadata,
            "secondary_motion_tool_git": tool_commit(),
            "decoder_module": decoder_pose.SHIPPED,
            "decoder_sha256": file_sha256(Path(mdl_skel.__file__)),
            "evaluator_module": "research.tooling.capture.decoder_pose",
            "evaluator_sha256": file_sha256(Path(decoder_pose.__file__)),
            "bands": {
                "source": "docs/vtmb/vtmb-animation-reverse-engineering.md 11.3",
                **{name: list(edges) for name, edges in BANDS.items()},
            },
            "qpc_frequency": frequency,
            "modules": module_hashes,
        },
        "support": flags,
        "corpus": run,
        "skeletons": bones_report,
        "pairing": pairing,
        "residual": residual,
        "step1_actor_motion": one,
        "step2_previous_draw": two,
        "verdict": verdict,
    }


def summarize(report: dict[str, Any]) -> str:
    lines = [f"{report['session']}: {report['verdict']['statement']}"]
    residual = report.get("residual") or {}
    if residual.get("available"):
        lines.append(
            f"  residual: {residual.get('bones_reported', 0)} bones over the band on "
            f"{residual['counts']['records_over_the_band_under_the_complete_rule']:,}"
            f" of {residual['records']:,} paired records "
            f"({residual['elapsed_seconds']}s)"
        )
        for row in residual["bones"][:12]:
            lines.append(
                f"    {row['model'].split('/')[-1]} {row['bone_name']} "
                f"({row['population']}, parent {row['parent_name']}, "
                f"flags {row['flags']}): rotation over "
                f"{row['rotation_over_the_band']:,}, translation over "
                f"{row['translation_over_the_band']:,}, worst "
                f"{row['rotation_degrees_max']:.4g} deg / "
                f"{row['translation_max']:.4g} units"
            )
    one = report.get("step1_actor_motion") or {}
    if one.get("available"):
        holder = one.get("series") or {}
        lines.append(
            f"  series: {holder.get('actors', 0)} actors, "
            f"{holder.get('samples', 0):,} samples over "
            f"{holder.get('models', 0)} models"
        )
        for row in one["by_bone"][:12]:
            still = row["actor_still"]
            moving = row["actor_moving"]
            level = row["divergence_plateau"]
            lines.append(
                f"    {row['model'].split('/')[-1]} {row['bone_name']}: "
                f"{row['scored_samples']:,} scored, divergence median "
                f"{row['divergence']['median']:.4g} deg, pinned at "
                f"{level['level']:.8g} on {level['share']:.0%} "
                f"(spread {level['spread_at_the_level']:.2g}); "
                f"local vs bind "
                f"{row['local_against_bind']['rotation_degrees']['max']:.3g} deg"
            )
            lines.append(
                f"      rho(root move) "
                f"{row['spearman']['root_translation_delta']}, rho(parent turn) "
                f"{row['spearman']['parent_rotation_degrees_delta']}; "
                f"still {still['samples']:,} median "
                f"{still['median'] if still['median'] is None else round(still['median'], 5)}"
                f" / moving {moving['samples']:,} median "
                f"{moving['median'] if moving['median'] is None else round(moving['median'], 5)}"
            )
    two = report.get("step2_previous_draw") or {}
    if two.get("available"):
        for row in two["by_bone"][:12]:
            arc = row["arc"]
            lines.append(
                f"    {row['model'].split('/')[-1]} {row['bone_name']}: arc "
                f"lag {arc['lag']:,} / overshoot {arc['overshoot']:,} / "
                f"previous-between {arc['previous_between']:,} / off-arc "
                f"{arc['off_arc']:,} of {row['samples_carrying_signal']:,}; "
                f"alpha {row['lag_fit']['alpha']} r2 {row['lag_fit']['r_squared']}"
            )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sessions", nargs="+")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--batch", type=int, default=DEFAULT_BATCH)
    parser.add_argument(
        "--max-bones",
        type=int,
        help="Bound the residual bone set for a smoke run, worst first.",
    )
    parser.add_argument(
        "--no-session-reports", dest="session_reports", action="store_false"
    )
    args = parser.parse_args()

    reports = []
    for value in args.sessions:
        session = resolve_session(value)
        report = analyze(session, batch=args.batch, max_bones=args.max_bones)
        reports.append(report)
        if args.session_reports:
            (session / REPORT_NAME).write_text(
                json.dumps(report, indent=2) + "\n", encoding="utf-8"
            )
        print(summarize(report))
    if args.report:
        args.report.write_text(
            json.dumps(
                {
                    "sessions": [report["session"] for report in reports],
                    "reports": reports,
                    "maps": compared_maps(reports) if len(reports) > 1 else None,
                },
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
    return (
        0
        if all(report["verdict"]["secondary_motion_measured"] for report in reports)
        else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
