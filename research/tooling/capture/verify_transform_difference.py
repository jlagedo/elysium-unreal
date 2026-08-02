"""Difference the transforms retail produced against the ones our rules produce.

Usage:
    uv run elysium research verify_transform_difference <session> [<session> ...]

Reads a finalized, indexed capture database read-only and compares the retail
transform chain against ours per record and per bone. Nothing is written into
the database; our arm is a claim about this checkout's decoder and evaluator at
this commit, and an evidence file must not carry a claim about a moment outside
itself. The report names the decoder, the evaluator and the exporter it measured
by content hash, so a difference is readable against the build it is a
difference from.

CAP4.1 answered the question for identity and CAP4.2 for bytes. Neither says
anything about values: a matching byte set proves the decoder reads the right
bytes, not that the transforms coming out are retail's transforms.

This pass covers the two stages that need no contribution binding and no clip
decode, so they run over the largest population the capture holds:

    bone-to-world  retail's `POSE` first half, against composing retail's own
                   `FINL` locals under the split-inheritance rule and placing
                   them by the captured root frame;
    skin palette   retail's `POSE` second half, against retail's own captured
                   bone-to-world times the stored `StudioBone.poseToBone`.

Each stage is fed retail's own input for that stage, so an earlier error never
cascades into a later stage's numbers, and the bones retail did not write are
seeded from retail's own output rather than recomputed.

Six bounds travel with the report.

Not every slot is a comparison, and the excluded ones are counted rather than
scored. A bone outside the composed pose's selected mask was never written; a
bone whose captured matrix is singular holds zeros, and two zero matrices read
as a perfect translation match and a 120-degree rotation disagreement, neither
of which means anything; and a composed pose handed a root that is not a frame
has nothing to be placed by. Each leaves the metrics under its own name.

A mismatching bone is labelled by what it is. `StudioBone.ProcType` rides on
every cluster, because a bone a procedural rule drives after the hierarchy
composes and a bone the hierarchy composes wrongly produce the same numbers and
are different work.

Neither composition stage measures shipped code. Unreal composes glTF
conventionally through glTFRuntime, and `mdl_gltf` discards
`StudioBone.poseToBone` and regenerates inverse binds by ordinary hierarchy FK,
so there is no Elysium implementation of a `Flags & 0x2` hierarchy or of a
`poseToBone` palette. Each stage therefore carries a second candidate — the
ordinary hierarchy, and the regenerated inverse bind — and those candidates'
band counts are the cost of the current export rather than a defect of the
transcribed rule. The decoded-locals and composed-locals stages, which do
measure `mdl_skel`, are not in this pass and say so rather than being omitted.

A mismatch is not a defect. It is the product: CAP4.4 ranks these clusters, and
a run that found nothing would have proved only that the corpus was too narrow.
A defect is a population meaning a comparison read the wrong values.

There is no single denominator. Every draw reaches the palette stage; only a
draw whose consumed pose build produced a composed pose, for the same entity and
model, reaches the bone-to-world stage. `carry_generation` is the last pose build
seen on the drawing thread, so the entity and model relation is required rather
than assumed, and the draws it excludes are counted.

Quantiles are bounded, not exact. Maxima and per-band counts are exact; a
reported median or p99 is the upper edge of the log-spaced bucket the true value
falls in, accurate to the ratio the report states.

The bands are reporting bands. `docs/vtmb/vtmb-animation-reverse-engineering.md`
11.3 owns them, and a band is never loosened to make something pass.
"""

from __future__ import annotations

import argparse
import json
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
)
from research.tooling.capture.decoder_pose import (
    BANDS,
    BAND_NAMES,
    BoneAccumulator,
    QUANTILE_RESOLUTION,
    Skeleton,
)
from research.tooling.capture.resolve_consumed_spans import (
    file_sha256,
    load_images,
    tool_commit,
)

REPORT_NAME = "transform-difference.json"

DRAW_KIND = "POSE"
COMPOSED_KIND = "FINL"

EVENT_TABLE = "record_events"
DEFAULT_EVENT_TABLE = "records"
PAYLOAD_TABLE = "payloads"
SPINE_TABLES = ("pose_group", "skeleton_bone", "model_identity")

# The renderable subobject sits a fixed four bytes into the `C_BaseAnimating`,
# which CAP1.3 measured over three runs and CAP2.3 reproduced from two
# separately recorded addresses. A draw and a composed pose name the same actor
# only if they satisfy it.
RENDERABLE_OFFSET = 4

MATRIX_BYTES = 12 * 4
ROOT_TRANSFORM_BYTES = 48

STAGE_BONE_TO_WORLD = "bone_to_world"
STAGE_SKIN_PALETTE = "skin_palette"
# The order the ladder walks. A record is attributed to the earliest stage whose
# error leaves the excellent band, because a later stage fed a wrong input would
# be reporting the earlier stage's failure a second time.
STAGE_ORDER = (STAGE_BONE_TO_WORLD, STAGE_SKIN_PALETTE)

UNREACHED = "stages_not_reached_in_this_pass"

# The pseudo-metric under which a record is counted at its worst band over every
# metric, beside the per-metric counts.
ANY_METRIC = "any_metric"

# The bone-to-world rules run side by side, as (name, split inheritance,
# procedural correction). `split` is A.4a's hierarchy alone, which is what the
# declared stage-3 populations count; `conventional` is the ordinary hierarchy
# the shipped glTF path composes; `split_procedural` adds the `ProcType == 1`
# correction that `docs/vtmb/procedural_bones.md` owns, and is the most complete
# rule available offline.
CANDIDATES = (
    ("split", True, False),
    ("conventional", False, False),
    ("split_procedural", True, True),
)
# The ladder attributes a record to a stage using the most complete rule, so a
# difference an available rule already explains is not reported as one.
LADDER_CANDIDATE = "split_procedural"

MAX_REPORTED_CLUSTERS = 64
MAX_REPORTED_BONES = 40
MAX_REPORTED_EXEMPLARS = 3
MAX_REPORTED_MODELS = 60

DEFAULT_BATCH = 4096


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
)

# A population whose non-zero value is a recorded property of the runtime, of
# the corpus, or of this pass's scope, with the reason it is.
ACCOUNTED = {
    "stage3_records_in_the_investigate_band": (
        "A composed bone-to-world outside the excellent band is the report's "
        "product rather than a fault of the instrument. CAP4.4 ranks these; a "
        "run finding none would have proved only that the corpus was narrow."
    ),
    "stage3_records_in_the_definite_band": (
        "As above, past the investigate ceiling. The cluster list names which "
        "model and bone carry it."
    ),
    "stage4_records_in_the_investigate_band": (
        "A skin palette outside the excellent band is the report's product. It "
        "prices the stored inverse bind against the palette retail wrote."
    ),
    "stage4_records_in_the_definite_band": (
        "As above, past the investigate ceiling."
    ),
    "stage3_procedural_rule_over_the_band": (
        "What the most complete offline rule still cannot reproduce: A.4a's "
        "hierarchy plus the `ProcType == 1` correction that "
        "`docs/vtmb/procedural_bones.md` owns. This is the residual the "
        "declared stage-3 counts overstate, because those count the hierarchy "
        "alone, and it is the population CAP4.4 should rank."
    ),
    "procedural_rule_unreadable": (
        "A bone declares a procedural rule whose table does not fit its image, "
        "or names a control bone or axis out of range. The bone composes "
        "ordinarily instead, so the candidate understates what the rule "
        "explains rather than reading the wrong bytes."
    ),
    "stage3_conventional_hierarchy_over_the_band": (
        "What the shipped glTF path composes, because glTFRuntime evaluates an "
        "ordinary hierarchy and knows nothing of `Flags & 0x2`. The count is "
        "the cost of the current export, not a defect of the transcribed rule."
    ),
    "stage4_regenerated_inverse_bind_over_the_band": (
        "What the shipped glTF path skins with: `mdl_gltf` discards "
        "`StudioBone.poseToBone` and regenerates the inverse bind by ordinary "
        "hierarchy FK. The count is what that costs."
    ),
    "retail_bone_to_world_row_length_over_the_band": (
        "The captured matrix's own orthonormality. This measures retail's float "
        "pipeline rather than any difference from ours, and it bounds how tight "
        "a claim the other bands can support."
    ),
    "bones_the_renderer_never_wrote": (
        "A bone whose captured matrix is singular: the slot holds zeros because "
        "nothing wrote a frame into it. Both sides then agree exactly and "
        "vacuously on translation, while the rotation metric reads "
        "`acos(-0.5)` = 120 degrees for two identical zero matrices. Neither "
        "number is a comparison, so the bone is excluded from both."
    ),
    "bones_outside_the_selected_mask": (
        "Slots the composed-pose stage never wrote. They hold whatever the bone "
        "cache last left there, so they are seeded from retail's own "
        "bone-to-world and never enter a comparison."
    ),
    "bones_seeded_from_retail_because_their_parent_was_unselected": (
        "A selected bone whose parent retail did not write composes off "
        "retail's own value for that parent. That is what keeps a stage fed "
        "retail's input for that stage rather than cascading an earlier error."
    ),
    "root_transform_not_rigid": (
        "The composed-pose stage was handed a root argument that is not a "
        "rotation and a translation, so there is no frame to place the pose "
        "into and no inverse to divide out. Such a record is excluded rather "
        "than inverted, which is why this is a property of the run and not a "
        "wrong comparison. Read it beside `distinct_non_rigid_roots`: a handful "
        "of repeated singular matrices is scratch the caller never filled in, "
        "while a wide spread of distinct ones would mean the trailer is being "
        "read at the wrong offset and every root is being misjudged."
    ),
    "draws_whose_consumed_pose_build_produced_no_composed_pose": (
        "The bone cache CAP2.1 measured: a second draw for an actor already "
        "posed this frame reaches the renderer without a pose build running. "
        "The palette stage still runs on the draw; the bone-to-world stage has "
        "no composed pose to feed it."
    ),
    "draws_whose_carry_generation_names_another_actor": (
        "`carry_generation` is the last pose build seen on the drawing thread, "
        "not a per-actor link, so a draw can name a build belonging to a "
        "different entity or model. Requiring the fixed renderable offset and a "
        "matching model excludes them rather than comparing the wrong pose."
    ),
    "composed_poses_no_draw_consumed": (
        "A pose built for an actor the renderer did not draw under it. CAP1.3 "
        "measured that population; it is coverage, not a broken join."
    ),
    "records_excluded_by_the_sample_bound": (
        "`--sample` was given, so the reported population is a bound the "
        "operator chose rather than the corpus."
    ),
    "payload_store_absent": (
        "The database was not compacted, so identical payloads are compared "
        "once each instead of once in total. The sets are the same; the cost is "
        "not."
    ),
    UNREACHED: (
        "The decoded-locals and composed-locals stages need a `BASE` record "
        "bound to the contributions nested below it and a clip decoded at the "
        "witnessed frame. They are a later pass and report a reason rather than "
        "being omitted."
    ),
}


# --------------------------------------------------------------------------
# Support and corpus
# --------------------------------------------------------------------------


def event_table(connection: sqlite3.Connection) -> str:
    names = {
        name
        for (name,) in connection.execute(
            "SELECT name FROM sqlite_master WHERE type IN ('table', 'view')"
        )
    }
    return EVENT_TABLE if EVENT_TABLE in names else DEFAULT_EVENT_TABLE


def support(connection: sqlite3.Connection) -> dict[str, Any]:
    """What this database can answer, read from its catalogue.

    Never from a stream version: CAP2.2 records a verifier that gated on every
    stream's version rather than the ones it read and would have refused a
    database carrying anything further.
    """
    objects = {
        name
        for (name,) in connection.execute(
            "SELECT name FROM sqlite_master WHERE type IN ('table', 'view')"
        )
    }
    table = event_table(connection)
    columns = {row[1] for row in connection.execute(f"PRAGMA table_info({table})")}
    kinds = {
        kind
        for (kind,) in connection.execute(f"SELECT DISTINCT kind FROM {table}")
    }
    return {
        "event_table": table,
        "payload_table": PAYLOAD_TABLE if PAYLOAD_TABLE in objects else None,
        "carries_payload_store": PAYLOAD_TABLE in objects
        and "payload_id" in columns,
        "carries_draws": DRAW_KIND in kinds,
        "carries_evaluations": COMPOSED_KIND in kinds,
        "carries_generations": {"generation", "carry_generation"} <= columns,
        "carries_images": "model_images" in objects,
        "carries_census": "model_headers" in objects,
        "carries_spine": set(SPINE_TABLES) <= objects,
    }


def modules(connection: sqlite3.Connection) -> dict[str, str]:
    """The hash-gated modules the run captured.

    A difference against our rules is only meaningful beside the build it is a
    difference from, and that build is named by the bytes the capture recorded
    rather than by the recipe that launched it.
    """
    return {
        name: digest
        for name, digest in connection.execute(
            "SELECT name, sha256 FROM modules ORDER BY name"
        )
    }


def owner_names(connection: sqlite3.Connection) -> dict[int, str]:
    return {
        int(checksum): name
        for checksum, name in connection.execute(
            "SELECT checksum, min(model_name) FROM model_headers GROUP BY checksum"
        )
    }


def corpus(connection: sqlite3.Connection, flags: dict[str, Any]) -> dict[str, Any]:
    table = flags["event_table"]

    def scalar(sql: str) -> int:
        return int(connection.execute(sql).fetchone()[0] or 0)

    return {
        "pose_groups": scalar("SELECT count(*) FROM pose_group"),
        "draw_records": scalar(
            f"SELECT count(*) FROM {table} WHERE kind = '{DRAW_KIND}'"
        ),
        "composed_records": scalar(
            f"SELECT count(*) FROM {table} WHERE kind = '{COMPOSED_KIND}'"
        ),
        "drawn_checksums": scalar(
            f"SELECT count(DISTINCT checksum) FROM {table} "
            f"WHERE kind = '{DRAW_KIND}'"
        ),
        "images": scalar("SELECT count(*) FROM model_images"),
        "bones_min": scalar(
            f"SELECT min(bone_count) FROM {table} WHERE kind = '{DRAW_KIND}'"
        ),
        "bones_max": scalar(
            f"SELECT max(bone_count) FROM {table} WHERE kind = '{DRAW_KIND}'"
        ),
    }


# --------------------------------------------------------------------------
# Skeletons
# --------------------------------------------------------------------------


def skeletons(
    connection: sqlite3.Connection, flags: dict[str, Any]
) -> tuple[dict[int, Skeleton], dict[str, Any]]:
    """One skeleton per captured model image, decoded by the shipped decoder.

    The spine's `skeleton_bone` is a cache of the same `mdl_skel.read_bones`
    call rather than a second witness, so re-deriving here and comparing is a
    check that this report and this spine describe one database — not a second
    opinion on the bones.
    """
    counts = {
        "captured_image_capped": 0,
        "skeleton_parent_out_of_range": 0,
        "skeleton_parent_not_topological": 0,
        "procedural_rule_unreadable": 0,
        "pose_to_bone_absent": 0,
        "spine_skeleton_disagrees_with_the_image": 0,
    }
    resolved: dict[int, Skeleton] = {}
    for checksum, walker in load_images(connection).items():
        if walker.capped:
            counts["captured_image_capped"] += 1
            continue
        skeleton = decoder_pose.skeleton_from_image(walker.image, checksum)
        for fault in skeleton.faults:
            counts[fault] += 1
        # A rule that will not read costs its own bone the correction and
        # nothing else, so the model is still comparable; a parent array that
        # will not walk disqualifies every bone on it.
        if set(skeleton.faults) - {"procedural_rule_unreadable"}:
            continue
        if not np.isfinite(skeleton.pose_to_bone).all():
            counts["pose_to_bone_absent"] += 1
            continue
        resolved[checksum] = skeleton

    spine = {}
    if flags["carries_spine"]:
        for checksum, count in connection.execute(
            "SELECT checksum, count(*) FROM skeleton_bone GROUP BY checksum"
        ):
            spine[int(checksum)] = int(count)
        for checksum, skeleton in resolved.items():
            if checksum in spine and spine[checksum] != skeleton.bones:
                counts["spine_skeleton_disagrees_with_the_image"] += 1

    return resolved, {
        "available": bool(resolved),
        "reason": "" if resolved else "no readable model image carries a skeleton",
        "skeletons": len(resolved),
        "bones": sum(skeleton.bones for skeleton in resolved.values()),
        "split_inheritance_bones": sum(
            int(skeleton.split.sum()) for skeleton in resolved.values()
        ),
        "spine_skeletons": len(spine),
        "counts": counts,
    }


# --------------------------------------------------------------------------
# Per-bone accumulation
# --------------------------------------------------------------------------


class StageArm:
    """One stage's per-bone statistics, one accumulator per candidate rule.

    Values are folded in batches and never retained: a corpus is hundreds of
    thousands of records over up to 96 bones, and the exact numbers the report
    needs — maxima, per-band counts, and which bone carries them — are all
    foldable.
    """

    def __init__(self, stage: str, metrics: dict[str, str]) -> None:
        self.stage = stage
        self.metrics = metrics
        self.store: dict[tuple[int, str, str], BoneAccumulator] = {}
        self.exemplars: dict[tuple[str, int, int], list[int]] = {}
        self.records: dict[str, np.ndarray] = {}

    def add(
        self,
        checksum: int,
        candidate: str,
        metric: str,
        bones: int,
        values: np.ndarray,
        multiplier: np.ndarray | int = 1,
    ) -> None:
        key = (checksum, candidate, metric)
        accumulator = self.store.get(key)
        if accumulator is None:
            accumulator = BoneAccumulator(bones, BANDS[self.metrics[metric]])
            self.store[key] = accumulator
        accumulator.add(values, multiplier)

    def add_records(
        self,
        candidate: str,
        metric: str,
        band: np.ndarray,
        multiplier: np.ndarray,
    ) -> None:
        """Fold one band per record: its worst band over the bones it compared.

        A record's band is not the sum of its bones' bands. Counting bones would
        report a 96-bone actor ninety-six times for one wrong pose, so the two
        are kept apart and named apart.

        Kept per metric as well as combined, because a stage whose translations
        are excellent and whose rotations are not is the interesting case and a
        single worst-of number would hide it.
        """
        totals = self.records.setdefault(
            (candidate, metric), np.zeros(len(BAND_NAMES), dtype=np.int64)
        )
        for index in range(len(BAND_NAMES)):
            totals[index] += int(multiplier[band == index].sum())

    def record_bands(self, candidate: str, metric: str = ANY_METRIC) -> dict[str, int]:
        totals = self.records.get((candidate, metric))
        if totals is None:
            return {name: 0 for name in BAND_NAMES}
        return {name: int(totals[index]) for index, name in enumerate(BAND_NAMES)}

    def witness(self, candidate: str, checksum: int, bone: int, record: int) -> None:
        seen = self.exemplars.setdefault((candidate, checksum, bone), [])
        if len(seen) < MAX_REPORTED_EXEMPLARS:
            seen.append(int(record))

    def candidates(self) -> list[str]:
        return sorted({candidate for _, candidate, _ in self.store})

    def summary(self, models: dict[int, Skeleton] | None = None) -> dict[str, Any]:
        out: dict[str, Any] = {}
        # Bones are partitioned two ways, because a total that mixes them
        # answers neither question the two candidates exist to ask. Whether the
        # split rule holds is a question about the bones carrying `Flags & 0x2`;
        # whether an offline evaluator is missing a whole stage is a question
        # about the bones a procedural rule drives, which nothing here composes.
        partitions: dict[str, dict[tuple[str, str], dict[str, dict[str, int]]]] = {
            "inheritance": {},
            "procedural": {},
        }
        groups = {
            "inheritance": (
                ("split_inheritance", lambda s: s.split),
                ("ordinary", lambda s: ~s.split),
            ),
            "procedural": (
                ("procedural", lambda s: s.procedural),
                ("ordinary", lambda s: ~s.procedural),
            ),
        }
        for (checksum, candidate, metric), accumulator in self.store.items():
            skeleton = None if models is None else models.get(checksum)
            if skeleton is None or skeleton.bones != accumulator.bones:
                continue
            for partition, members in groups.items():
                bucket = partitions[partition].setdefault(
                    (candidate, metric),
                    {
                        group: {name: 0 for name in BAND_NAMES}
                        for group, _ in members
                    },
                )
                for group, select in members:
                    mask = select(skeleton)
                    for index, name in enumerate(BAND_NAMES):
                        bucket[group][name] += int(
                            accumulator.banded[mask, index].sum()
                        )
        inheritance = partitions["inheritance"]
        for (_, candidate, metric), accumulator in self.store.items():
            bucket = out.setdefault(candidate, {})
            merged = bucket.setdefault(
                metric,
                {"bone_observations": 0, "max": 0.0, "median": 0.0, "p99": 0.0,
                 "bone_observations_by_band": {name: 0 for name in BAND_NAMES}},
            )
            piece = accumulator.summary()
            merged["bone_observations"] += piece["values"]
            merged["max"] = max(merged["max"], piece["max"])
            merged["median"] = max(merged["median"], piece["median"])
            merged["p99"] = max(merged["p99"], piece["p99"])
            for name in BAND_NAMES:
                merged["bone_observations_by_band"][name] += piece["bands"][name]
        for candidate, metrics in out.items():
            for metric, block in metrics.items():
                block["records_by_band"] = self.record_bands(candidate, metric)
                block["bone_observations_by_inheritance"] = inheritance.get(
                    (candidate, metric)
                )
                block["bone_observations_by_procedural"] = partitions[
                    "procedural"
                ].get((candidate, metric))
            metrics["records_by_band"] = self.record_bands(candidate)
        return out

    def over_band(self, candidate: str) -> int:
        """Records past the excellent band on any metric of this candidate."""
        totals = self.records.get((candidate, ANY_METRIC))
        return 0 if totals is None else int(totals[1:].sum())

    def band_totals(self, candidate: str) -> tuple[int, int]:
        totals = self.records.get((candidate, ANY_METRIC))
        if totals is None:
            return 0, 0
        return int(totals[1]), int(totals[2])

    def bones_over_band(
        self, names: dict[int, str], models: dict[int, Skeleton]
    ) -> list[dict[str, Any]]:
        rows = []
        for (checksum, candidate, metric), accumulator in self.store.items():
            skeleton = models.get(checksum)
            for bone in range(accumulator.bones):
                over = int(accumulator.banded[bone, 1:].sum())
                if not over:
                    continue
                rows.append(
                    {
                        "stage": self.stage,
                        "candidate": candidate,
                        "metric": metric,
                        "checksum": f"0x{checksum:08x}",
                        "model": names.get(checksum, ""),
                        "bone": bone,
                        "bone_name": (
                            skeleton.names[bone]
                            if skeleton and bone < skeleton.bones
                            else ""
                        ),
                        "depth": (
                            int(skeleton.depth[bone])
                            if skeleton and bone < skeleton.bones
                            else -1
                        ),
                        # `StudioBone.ProcType`, carried so a cluster can be
                        # classified against the candidate-cause table without
                        # reopening the model image.
                        "proc_type": (
                            int(skeleton.proc_type[bone])
                            if skeleton and bone < skeleton.bones
                            else 0
                        ),
                        "records": int(accumulator.count[bone]),
                        "records_over_the_band": over,
                        "records_in_the_definite_band": int(
                            accumulator.banded[bone, 2]
                        ),
                        "max": float(accumulator.maximum[bone]),
                        "p99": float(accumulator.quantile(0.99)[bone]),
                        "exemplars": self.exemplars.get(
                            (candidate, checksum, bone), []
                        ),
                    }
                )
        rows.sort(
            key=lambda row: (
                -row["records_in_the_definite_band"],
                -row["records_over_the_band"],
                -row["max"],
            )
        )
        return rows


# --------------------------------------------------------------------------
# Grouping
# --------------------------------------------------------------------------


def _payload_join(flags: dict[str, Any], alias: str, key: str) -> tuple[str, str]:
    """How to reach a payload's bytes, with or without the content store."""
    if flags["carries_payload_store"]:
        return (
            f"JOIN {PAYLOAD_TABLE} {alias} ON {alias}.id = g.{key}",
            f"{alias}.bytes",
        )
    return (
        f"JOIN {flags['event_table']} {alias} ON {alias}.id = g.{key}",
        f"{alias}.raw_payload",
    )


def prepare(connection: sqlite3.Connection, flags: dict[str, Any]) -> dict[str, int]:
    """The two comparison grains, deduplicated by payload identity.

    A cutscene draws the same actor several times a frame from the same buffers,
    so the draw stream repeats payloads heavily. Comparing a distinct payload
    once and multiplying by its record count is exact, because the comparison is
    a function of the bytes and the skeleton and of nothing else.
    """
    table = flags["event_table"]
    key = "payload_id" if flags["carries_payload_store"] else "id"

    connection.execute(
        f"""
        CREATE TEMP TABLE draw_group AS
        SELECT checksum, bone_count, {key} AS payload_key,
               count(*) AS records, min(id) AS exemplar
        FROM {table}
        WHERE kind = '{DRAW_KIND}'
        GROUP BY checksum, bone_count, {key}
        """
    )

    # `carry_generation` names the last pose build the drawing thread saw, which
    # is the build whose output a cached draw consumes but is not by itself a
    # per-actor link. The fixed renderable offset and a matching model are what
    # make the pairing an identity rather than a coincidence of timing.
    connection.execute(
        f"""
        CREATE TEMP TABLE world_group AS
        SELECT f.checksum AS checksum, f.bone_count AS bone_count,
               f.{key} AS local_key, d.{key} AS draw_key,
               count(*) AS records, min(d.id) AS exemplar
        FROM {table} d
        JOIN {table} f
          ON f.generation = d.carry_generation AND f.kind = '{COMPOSED_KIND}'
        WHERE d.kind = '{DRAW_KIND}'
          AND d.client_entity = f.client_entity + {RENDERABLE_OFFSET}
          AND d.checksum = f.checksum
          AND d.bone_count = f.bone_count
        GROUP BY f.checksum, f.bone_count, f.{key}, d.{key}
        """
    )

    paired = int(
        connection.execute("SELECT sum(records) FROM world_group").fetchone()[0] or 0
    )
    joined = int(
        connection.execute(
            f"""
            SELECT count(*) FROM {table} d
            JOIN {table} f
              ON f.generation = d.carry_generation AND f.kind = '{COMPOSED_KIND}'
            WHERE d.kind = '{DRAW_KIND}'
            """
        ).fetchone()[0]
        or 0
    )
    draws = int(
        connection.execute(
            f"SELECT count(*) FROM {table} WHERE kind = '{DRAW_KIND}'"
        ).fetchone()[0]
        or 0
    )
    composed = int(
        connection.execute(
            f"SELECT count(*) FROM {table} WHERE kind = '{COMPOSED_KIND}'"
        ).fetchone()[0]
        or 0
    )
    consumed = int(
        connection.execute(
            "SELECT count(DISTINCT local_key) FROM world_group"
        ).fetchone()[0]
        or 0
    )
    return {
        "draws": draws,
        "composed": composed,
        "draws_joined_by_carry_generation": joined,
        "draws_paired": paired,
        "draws_whose_consumed_pose_build_produced_no_composed_pose": draws - joined,
        "draws_whose_carry_generation_names_another_actor": joined - paired,
        "composed_poses_no_draw_consumed": max(composed - consumed, 0),
    }


def _stream(
    connection: sqlite3.Connection, sql: str, batch: int
) -> Iterator[list[tuple]]:
    cursor = connection.execute(sql)
    while True:
        rows = cursor.fetchmany(batch)
        if not rows:
            return
        yield rows


# --------------------------------------------------------------------------
# Stage 4 — the skin palette
# --------------------------------------------------------------------------


def stage_four(
    connection: sqlite3.Connection,
    flags: dict[str, Any],
    models: dict[int, Skeleton],
    *,
    batch: int,
    sample: int | None,
) -> tuple[dict[str, Any], StageArm]:
    """Retail's skin palette against its own bone-to-world times the inverse bind.

    Transcribed from the StudioRender palette loop; there is no shipped Elysium
    implementation of it, because `mdl_gltf` regenerates inverse binds by
    ordinary hierarchy FK instead of reading `StudioBone.poseToBone`. That
    regenerated bind is the second candidate, so what the export costs is priced
    beside what the stored bind achieves.
    """
    arm = StageArm(
        STAGE_SKIN_PALETTE,
        {
            "translation": "world_translation",
            "rotation_degrees": "rotation_degrees",
            "row_length": "row_length",
        },
    )
    counts = {
        "entity_image_absent": 0,
        "payload_width_disagrees_with_bone_count": 0,
        "payload_identity_missing": 0,
        "bones_the_renderer_never_wrote": 0,
        "records_excluded_by_the_sample_bound": 0,
    }
    join, column = _payload_join(flags, "p", "payload_key")
    sql = (
        f"SELECT g.checksum, g.bone_count, g.records, g.exemplar, {column} "
        f"FROM draw_group g {join} ORDER BY g.checksum, g.payload_key"
    )
    started = time.monotonic()
    groups = 0
    records = 0
    regenerated: dict[int, np.ndarray] = {}
    for rows in _stream(connection, sql, batch):
        if sample is not None and records >= sample:
            counts["records_excluded_by_the_sample_bound"] += sum(
                int(row[2]) for row in rows
            )
            continue
        by_model: dict[tuple[int, int], list[tuple]] = {}
        for row in rows:
            checksum, bones, count, exemplar, payload = row
            checksum, bones, count = int(checksum), int(bones), int(count)
            if payload is None:
                counts["payload_identity_missing"] += count
                continue
            if checksum not in models:
                counts["entity_image_absent"] += count
                continue
            if len(payload) != bones * MATRIX_BYTES * 2:
                counts["payload_width_disagrees_with_bone_count"] += count
                continue
            if models[checksum].bones != bones:
                counts["payload_width_disagrees_with_bone_count"] += count
                continue
            by_model.setdefault((checksum, bones), []).append(
                (count, int(exemplar), payload)
            )
        for (checksum, bones), entries in by_model.items():
            skeleton = models[checksum]
            if checksum not in regenerated:
                regenerated[checksum] = decoder_pose.regenerated_inverse_bind(skeleton)
            world = np.stack(
                [decoder_pose.payload_matrices(blob, bones) for _, _, blob in entries]
            )
            retail = np.stack(
                [
                    decoder_pose.payload_matrices(
                        blob, bones, offset=bones * MATRIX_BYTES
                    )
                    for _, _, blob in entries
                ]
            )
            multiplier = np.array([count for count, _, _ in entries], dtype=np.int64)
            groups += len(entries)
            records += int(multiplier.sum())
            # This stage has no selected mask of its own — a draw carries none —
            # so which slots hold a frame is read from the matrices themselves.
            live = decoder_pose.is_frame(world) & decoder_pose.is_frame(retail)
            counts["bones_the_renderer_never_wrote"] += int(
                (multiplier[:, None] * ~live).sum()
            )
            for candidate, inverse in (
                ("pose_to_bone", skeleton.pose_to_bone),
                ("regenerated", regenerated[checksum]),
            ):
                ours = decoder_pose.palette(world, inverse)
                translation = np.where(
                    live, decoder_pose.translation_error(ours, retail), 0.0
                )
                rotation = np.where(
                    live,
                    decoder_pose.matrix_rotation_angle_degrees(ours, retail),
                    0.0,
                )
                arm.add(checksum, candidate, "translation", bones, translation, multiplier)
                arm.add(
                    checksum, candidate, "rotation_degrees", bones, rotation, multiplier
                )
                bands = {
                    "translation": decoder_pose.band_of(
                        translation, BANDS["world_translation"]
                    ),
                    "rotation_degrees": decoder_pose.band_of(
                        rotation, BANDS["rotation_degrees"]
                    ),
                }
                band = np.maximum.reduce(list(bands.values()))
                for metric, value in bands.items():
                    arm.add_records(
                        candidate, metric, value.max(axis=1), multiplier
                    )
                arm.add_records(candidate, ANY_METRIC, band.max(axis=1), multiplier)
                over = band > 0
                for index, (_, exemplar, _) in enumerate(entries):
                    for bone in np.flatnonzero(over[index]):
                        arm.witness(candidate, checksum, int(bone), exemplar)
            # Retail's own orthonormality, measured once rather than per
            # candidate: it is a property of the captured matrix.
            rows = np.where(live, decoder_pose.row_length_error(world), 0.0)
            arm.add(checksum, "retail", "row_length", bones, rows, multiplier)
            row_band = decoder_pose.band_of(rows, BANDS["row_length"]).max(axis=1)
            arm.add_records("retail", "row_length", row_band, multiplier)
            arm.add_records("retail", ANY_METRIC, row_band, multiplier)

    investigate, definite = arm.band_totals("pose_to_bone")
    counts["stage4_records_in_the_investigate_band"] = investigate
    counts["stage4_records_in_the_definite_band"] = definite
    counts["stage4_regenerated_inverse_bind_over_the_band"] = arm.over_band(
        "regenerated"
    )
    counts["retail_bone_to_world_row_length_over_the_band"] = arm.over_band("retail")
    candidates = arm.summary(models)
    # Retail's own orthonormality is a measurement of the captured matrix rather
    # than a candidate rule of ours, so it is reported apart from the two.
    retail_rows = candidates.pop("retail", {})
    return (
        {
            "available": bool(records),
            "reason": "" if records else "no draw record reached the palette stage",
            "retail_side": "the POSE payload's second half",
            "our_side": (
                "retail's own captured bone-to-world times "
                "StudioBone.poseToBone"
            ),
            "implementation": (
                "no shipped Elysium implementation; transcribed from the "
                "StudioRender palette loop"
            ),
            "records": records,
            "distinct_payload_keys": groups,
            "elapsed_seconds": round(time.monotonic() - started, 3),
            "candidates": candidates,
            "retail_row_length_error": retail_rows.get("row_length"),
            "counts": counts,
        },
        arm,
    )


# --------------------------------------------------------------------------
# Stage 3 — bone-to-world, and the ladder over the same population
# --------------------------------------------------------------------------


def stage_three(
    connection: sqlite3.Connection,
    flags: dict[str, Any],
    models: dict[int, Skeleton],
    *,
    batch: int,
    sample: int | None,
) -> tuple[dict[str, Any], StageArm, dict[str, Any]]:
    """Retail's bone-to-world against composing retail's own composed locals.

    Transcribed from `docs/vtmb/animation_and_movers.md` A.4a; not a shipped
    Elysium path, because glTFRuntime evaluates an ordinary hierarchy and knows
    nothing of `Flags & 0x2`. That ordinary hierarchy is the second candidate.

    Two frames are reported. World space is the comparison as retail wrote it;
    model space divides out the captured root, which isolates the composition
    rule from where the entity happens to stand. They are related exactly —
    `boneToWorld = root . modelSpace` holds through the split branch too — so
    reporting both costs one multiply and buys the separation.

    The ladder runs here rather than in its own pass, because attributing a
    record to the earliest stage that failed needs both stages for that record,
    and this loop is the one that holds both payloads.
    """
    arm = StageArm(
        STAGE_BONE_TO_WORLD,
        {
            "model_translation": "model_translation",
            "world_translation": "world_translation",
            "rotation_degrees": "rotation_degrees",
        },
    )
    counts = {
        "entity_image_absent": 0,
        "payload_width_disagrees_with_bone_count": 0,
        "payload_identity_missing": 0,
        "root_transform_bytes_not_48": 0,
        "root_transform_not_rigid": 0,
        "bones_outside_the_selected_mask": 0,
        "bones_the_renderer_never_wrote": 0,
        "bones_seeded_from_retail_because_their_parent_was_unselected": 0,
        "records_excluded_by_the_sample_bound": 0,
    }
    by_stage = {stage: 0 for stage in STAGE_ORDER}
    by_stage["none"] = 0
    first_bone: dict[tuple[int, int, str], int] = {}
    propagation: dict[tuple[int, int], dict[str, int]] = {}

    local_join, local_column = _payload_join(flags, "lp", "local_key")
    draw_join, draw_column = _payload_join(flags, "dp", "draw_key")
    sql = (
        f"SELECT g.checksum, g.bone_count, g.records, g.exemplar, "
        f"{local_column}, {draw_column} "
        f"FROM world_group g {local_join} {draw_join} "
        f"ORDER BY g.checksum, g.local_key, g.draw_key"
    )
    started = time.monotonic()
    groups = 0
    records = 0
    descendants: dict[int, np.ndarray] = {}
    # How many *distinct* root frames failed the rigidity test, which is what
    # separates a caller leaving scratch behind from the trailer being read at
    # the wrong offset.
    non_rigid: set[bytes] = set()
    worst_root = 0.0
    for rows in _stream(connection, sql, batch):
        if sample is not None and records >= sample:
            counts["records_excluded_by_the_sample_bound"] += sum(
                int(row[2]) for row in rows
            )
            continue
        by_model: dict[tuple[int, int], list[tuple]] = {}
        for checksum, bones, count, exemplar, local, draw in rows:
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
            mask_bytes = ((bones + 31) // 32) * 4
            expected = bones * 7 * 4 + mask_bytes
            if len(local) == expected:
                counts["root_transform_bytes_not_48"] += count
                continue
            if len(local) != expected + ROOT_TRANSFORM_BYTES:
                counts["payload_width_disagrees_with_bone_count"] += count
                continue
            by_model.setdefault((checksum, bones), []).append(
                (count, int(exemplar), local, draw)
            )

        for (checksum, bones), entries in by_model.items():
            skeleton = models[checksum]
            if checksum not in descendants:
                descendants[checksum] = np.stack(
                    [skeleton.descendants(index) for index in range(bones)]
                )
            decoded = [
                decoder_pose.payload_local_pose(blob, bones)
                for _, _, blob, _ in entries
            ]
            positions = np.stack([pair[0] for pair in decoded])
            quaternions = np.stack([pair[1] for pair in decoded])
            selected = np.stack(
                [decoder_pose.payload_selected(blob, bones) for _, _, blob, _ in entries]
            )
            root_offset = bones * 7 * 4 + ((bones + 31) // 32) * 4
            root = np.stack(
                [
                    decoder_pose.payload_matrices(blob, 1, offset=root_offset)[0]
                    for _, _, blob, _ in entries
                ]
            )
            retail = np.stack(
                [decoder_pose.payload_matrices(blob, bones) for _, _, _, blob in entries]
            )
            retail_skin = np.stack(
                [
                    decoder_pose.payload_matrices(
                        blob, bones, offset=bones * MATRIX_BYTES
                    )
                    for _, _, _, blob in entries
                ]
            )
            multiplier = np.array(
                [count for count, _, _, _ in entries], dtype=np.int64
            )

            rigid = (
                decoder_pose.row_length_error(root) <= BANDS["row_length"][1]
            ) & (np.abs(np.linalg.det(root[..., :3])) > 0.5)
            if not rigid.all():
                counts["root_transform_not_rigid"] += int(
                    multiplier[~rigid].sum()
                )
                for index in np.flatnonzero(~rigid):
                    non_rigid.add(root[index].astype(np.float32).tobytes())
                    worst_root = max(
                        worst_root,
                        float(decoder_pose.row_length_error(root[index])),
                    )
                keep = np.flatnonzero(rigid)
                if keep.size == 0:
                    continue
                positions, quaternions = positions[keep], quaternions[keep]
                selected, root, retail = selected[keep], root[keep], retail[keep]
                retail_skin = retail_skin[keep]
                multiplier = multiplier[keep]
                entries = [entries[index] for index in keep]

            counts["bones_outside_the_selected_mask"] += int(
                (multiplier[:, None] * (~selected)).sum()
            )
            # A slot the mask selects but the renderer left singular is not a
            # comparison either way, so it leaves the metrics with the rest.
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

            local_matrices = decoder_pose.matrices_from_local(positions, quaternions)
            inverse_root = decoder_pose.invert_rigid(root)
            retail_model = decoder_pose.multiply(inverse_root[:, None], retail)
            groups += len(entries)
            records += int(multiplier.sum())

            worst_over = None
            for candidate, split, procedural in CANDIDATES:
                world = decoder_pose.compose(
                    local_matrices,
                    skeleton,
                    root,
                    split=split,
                    seed=retail,
                    selected=selected,
                    procedural=procedural,
                )
                model = decoder_pose.compose(
                    local_matrices,
                    skeleton,
                    None,
                    split=split,
                    seed=retail_model,
                    selected=selected,
                    procedural=procedural,
                )
                world_error = np.where(
                    live, decoder_pose.translation_error(world, retail), 0.0
                )
                model_error = np.where(
                    live,
                    decoder_pose.translation_error(model, retail_model),
                    0.0,
                )
                rotation = np.where(
                    live,
                    decoder_pose.matrix_rotation_angle_degrees(world, retail),
                    0.0,
                )
                arm.add(checksum, candidate, "world_translation", bones, world_error, multiplier)
                arm.add(checksum, candidate, "model_translation", bones, model_error, multiplier)
                arm.add(checksum, candidate, "rotation_degrees", bones, rotation, multiplier)
                bands = {
                    "model_translation": decoder_pose.band_of(
                        model_error, BANDS["model_translation"]
                    ),
                    "world_translation": decoder_pose.band_of(
                        world_error, BANDS["world_translation"]
                    ),
                    "rotation_degrees": decoder_pose.band_of(
                        rotation, BANDS["rotation_degrees"]
                    ),
                }
                band = np.maximum.reduce(list(bands.values()))
                for metric, value in bands.items():
                    arm.add_records(
                        candidate, metric, value.max(axis=1), multiplier
                    )
                arm.add_records(candidate, ANY_METRIC, band.max(axis=1), multiplier)
                over = band > 0
                for index, (_, exemplar, _, _) in enumerate(entries):
                    for bone in np.flatnonzero(over[index]):
                        arm.witness(candidate, checksum, int(bone), exemplar)
                if candidate == LADDER_CANDIDATE:
                    worst_over = over

            # The ladder, over the split candidate and this loop's own draw
            # payload: which stage failed first for each of these records.
            palette_error = np.where(
                live,
                decoder_pose.translation_error(
                    decoder_pose.palette(retail, skeleton.pose_to_bone),
                    retail_skin,
                ),
                0.0,
            )
            palette_over = palette_error > BANDS["world_translation"][0]
            _fold_ladder(
                skeleton,
                checksum,
                worst_over,
                palette_over,
                multiplier,
                by_stage,
                first_bone,
                propagation,
                descendants[checksum],
            )

    investigate, definite = arm.band_totals("split")
    counts["stage3_records_in_the_investigate_band"] = investigate
    counts["stage3_records_in_the_definite_band"] = definite
    counts["stage3_conventional_hierarchy_over_the_band"] = arm.over_band(
        "conventional"
    )
    counts["stage3_procedural_rule_over_the_band"] = arm.over_band(
        LADDER_CANDIDATE
    )
    ladder = {
        "population": (
            "the bone-to-world comparable pairs; a record is attributed to the "
            f"earliest stage whose error leaves the excellent band, judged on "
            f"the {LADDER_CANDIDATE} candidate so a difference an available "
            "rule already explains is not counted as one"
        ),
        "stages_available": list(STAGE_ORDER),
        "by_stage": by_stage,
        "by_bone": [],
        "descendant_propagation": [],
        "first_bone_raw": first_bone,
        "propagation_raw": propagation,
    }
    return (
        {
            "available": bool(records),
            "reason": (
                ""
                if records
                else "no draw paired with a composed pose for the same actor"
            ),
            "retail_side": "the POSE payload's first half, over the FINL selected mask",
            "our_side": (
                "compose(FINL locals, split=True) placed by the captured "
                "48-byte root frame, unselected bones seeded from retail"
            ),
            "implementation": (
                "no shipped Elysium implementation; transcribed from "
                "animation_and_movers.md A.4a"
            ),
            "normalization": (
                "the captured 48-byte root frame from the paired FINL record; "
                "model space divides it out and world space does not"
            ),
            "records": records,
            "distinct_payload_keys": groups,
            "distinct_non_rigid_roots": len(non_rigid),
            "worst_non_rigid_root_row_length": worst_root,
            "elapsed_seconds": round(time.monotonic() - started, 3),
            "candidates": arm.summary(models),
            "counts": counts,
        },
        arm,
        ladder,
    )


def _fold_ladder(
    skeleton: Skeleton,
    checksum: int,
    world_over: np.ndarray,
    palette_over: np.ndarray,
    multiplier: np.ndarray,
    by_stage: dict[str, int],
    first_bone: dict[tuple[int, int, str], int],
    propagation: dict[tuple[int, int], dict[str, int]],
    descendants: np.ndarray,
) -> None:
    """Attribute each record to its earliest failing stage, bone and subtree.

    "Earliest bone" is the shallowest one over the band, because the composition
    is a forward pass: a deeper bone's error may be its ancestor's arriving
    rather than its own. What separates the two is whether the rest of the
    subtree moved with it, which is what the propagation rates report.
    """
    order = np.argsort(skeleton.depth, kind="stable")
    for index in range(world_over.shape[0]):
        count = int(multiplier[index])
        row = world_over[index]
        if row.any():
            bone = int(next(b for b in order if row[b]))
            by_stage[STAGE_BONE_TO_WORLD] += count
            key = (checksum, bone, STAGE_BONE_TO_WORLD)
            first_bone[key] = first_bone.get(key, 0) + count
            below = descendants[bone]
            stats = propagation.setdefault(
                (checksum, bone),
                {"records": 0, "descendants": 0, "descendants_over": 0,
                 "others": 0, "others_over": 0},
            )
            stats["records"] += count
            stats["descendants"] += count * int(below.sum())
            stats["descendants_over"] += count * int((row & below).sum())
            stats["others"] += count * int((~below).sum())
            stats["others_over"] += count * int((row & ~below).sum())
            continue
        if palette_over[index].any():
            bone = int(next(b for b in order if palette_over[index][b]))
            by_stage[STAGE_SKIN_PALETTE] += count
            key = (checksum, bone, STAGE_SKIN_PALETTE)
            first_bone[key] = first_bone.get(key, 0) + count
            continue
        by_stage["none"] += count


def finish_ladder(
    ladder: dict[str, Any], names: dict[int, str], models: dict[int, Skeleton]
) -> dict[str, Any]:
    rows = []
    for (checksum, bone, stage), count in ladder.pop("first_bone_raw").items():
        skeleton = models.get(checksum)
        rows.append(
            {
                "stage": stage,
                "checksum": f"0x{checksum:08x}",
                "model": names.get(checksum, ""),
                "bone": bone,
                "bone_name": (
                    skeleton.names[bone] if skeleton and bone < skeleton.bones else ""
                ),
                "depth": (
                    int(skeleton.depth[bone])
                    if skeleton and bone < skeleton.bones
                    else -1
                ),
                "records": count,
            }
        )
    rows.sort(key=lambda row: -row["records"])
    ladder["by_bone"] = rows[:MAX_REPORTED_BONES]
    ladder["by_bone_truncated"] = max(len(rows) - MAX_REPORTED_BONES, 0)

    spread = []
    for (checksum, bone), stats in ladder.pop("propagation_raw").items():
        skeleton = models.get(checksum)
        spread.append(
            {
                "checksum": f"0x{checksum:08x}",
                "model": names.get(checksum, ""),
                "first_bone": (
                    skeleton.names[bone] if skeleton and bone < skeleton.bones else ""
                ),
                "depth": (
                    int(skeleton.depth[bone])
                    if skeleton and bone < skeleton.bones
                    else -1
                ),
                "records": stats["records"],
                "descendants": {
                    "total": stats["descendants"],
                    "over_band": stats["descendants_over"],
                    "rate": round(
                        stats["descendants_over"] / stats["descendants"], 6
                    )
                    if stats["descendants"]
                    else 0.0,
                },
                "non_descendants": {
                    "total": stats["others"],
                    "over_band": stats["others_over"],
                    "rate": round(stats["others_over"] / stats["others"], 6)
                    if stats["others"]
                    else 0.0,
                },
            }
        )
    spread.sort(key=lambda row: -row["records"])
    ladder["descendant_propagation"] = spread[:MAX_REPORTED_BONES]
    ladder["descendant_propagation_truncated"] = max(
        len(spread) - MAX_REPORTED_BONES, 0
    )
    return ladder


# --------------------------------------------------------------------------
# The static bind arm
# --------------------------------------------------------------------------


def static_bind(
    models: dict[int, Skeleton], names: dict[int, str]
) -> dict[str, Any]:
    """What discarding `StudioBone.poseToBone` costs, with no per-record noise.

    One row per model rather than per draw: the stored inverse bind and the one
    `mdl_gltf` regenerates are both properties of the image, so a difference
    between them is a fact about the model and not about a pose.
    """
    rows = []
    over = 0
    for checksum, skeleton in models.items():
        regenerated = decoder_pose.regenerated_inverse_bind(skeleton)
        translation = decoder_pose.translation_error(
            regenerated, skeleton.pose_to_bone
        )
        rotation = decoder_pose.matrix_rotation_angle_degrees(
            regenerated, skeleton.pose_to_bone
        )
        bones_over = int(
            (
                (translation > BANDS["model_translation"][0])
                | (rotation > BANDS["rotation_degrees"][0])
            ).sum()
        )
        over += bones_over
        rows.append(
            {
                "checksum": f"0x{checksum:08x}",
                "model": names.get(checksum, ""),
                "bones": skeleton.bones,
                "split_inheritance_bones": int(skeleton.split.sum()),
                "translation_max": float(translation.max(initial=0.0)),
                "rotation_degrees_max": float(rotation.max(initial=0.0)),
                "bones_over_the_band": bones_over,
            }
        )
    rows.sort(key=lambda row: (-row["bones_over_the_band"], -row["translation_max"]))
    return {
        "available": bool(rows),
        "reason": "" if rows else "no readable model image carries a skeleton",
        "our_side": (
            "mdl_gltf's regenerated inverse bind, in Source space, against the "
            "stored StudioBone.poseToBone"
        ),
        "models": len(rows),
        "bones_over_the_band": over,
        "worst": rows[:MAX_REPORTED_MODELS],
        "truncated": max(len(rows) - MAX_REPORTED_MODELS, 0),
        "counts": {},
    }


# --------------------------------------------------------------------------
# Clusters
# --------------------------------------------------------------------------


def clusters(
    arms: list[StageArm], names: dict[int, str], models: dict[int, Skeleton]
) -> list[dict[str, Any]]:
    """The ranked list CAP4.4 reads, one row per stage, candidate, model and bone.

    The key is built from identities alone so two runs over different databases
    produce the same key for the same claim, which is what lets a repeat capture
    say a bone named mismatching in one is named mismatching in the other.
    """
    rows = []
    for arm in arms:
        for row in arm.bones_over_band(names, models):
            if row["candidate"] == "retail":
                continue
            rows.append(
                {
                    "cluster_key": "/".join(
                        (
                            row["stage"],
                            row["candidate"],
                            row["metric"],
                            row["checksum"],
                            row["bone_name"] or str(row["bone"]),
                        )
                    ),
                    "band": (
                        "definite"
                        if row["records_in_the_definite_band"]
                        else "investigate"
                    ),
                    # The row of the candidate-cause table this cluster falls
                    # under, from what the bone is rather than from its numbers.
                    "candidate_cause": (
                        "controllers and procedural order"
                        if row["proc_type"]
                        else "hierarchy composition"
                    ),
                    **row,
                }
            )
    rows.sort(
        key=lambda row: (
            -row["records_in_the_definite_band"],
            -row["records_over_the_band"],
            -row["max"],
            row["cluster_key"],
        )
    )
    return rows[:MAX_REPORTED_CLUSTERS]


# --------------------------------------------------------------------------
# Verdict
# --------------------------------------------------------------------------


def decide(flags: dict[str, Any], sections: list[dict[str, Any]]) -> dict[str, Any]:
    if not flags["carries_spine"]:
        return {
            "judgeable": False,
            "transform_difference_compared": False,
            "statement": (
                "This database carries no join spine; run "
                "`index_capture_database` over it before differencing it."
            ),
        }
    if not flags["carries_images"] or not flags["carries_census"]:
        return {
            "judgeable": False,
            "transform_difference_compared": False,
            "statement": (
                "This database carries no model images or no census, so no "
                "skeleton and no inverse bind can be read for either arm."
            ),
        }
    if not flags["carries_draws"] or not flags["carries_generations"]:
        return {
            "judgeable": False,
            "transform_difference_compared": False,
            "statement": (
                "This database carries no draw records or no generation "
                "columns, so no retail transform is available to difference."
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
                # A population no declaration covers fails the verdict rather
                # than passing unremarked: an undeclared count is a claim
                # nobody has read.
                unclassified[name] = unclassified.get(name, 0) + value
    compared = not defects and not unclassified and any(
        section.get("available") for section in sections
    )
    if compared:
        statement = (
            "Retail's bone-to-world and skin palette were compared against the "
            "transcribed composition and palette rules, each fed retail's own "
            "input for its stage. Every non-zero population is declared. A "
            "mismatch count is the product of this pass, not a defect of it; "
            "the decoded-locals and composed-locals stages are a later pass."
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
            "difference."
        )
    else:
        statement = "No stage reached a comparable record."
    return {
        "judgeable": True,
        "transform_difference_compared": compared,
        "defects": defects,
        "unclassified": unclassified,
        "accounted": {name: ACCOUNTED[name] for name in sorted(accounted)},
        "accounted_counts": accounted,
        "statement": statement,
    }


# --------------------------------------------------------------------------
# Driver
# --------------------------------------------------------------------------


def verify(
    session: Path,
    *,
    batch: int = DEFAULT_BATCH,
    sample: int | None = None,
) -> dict[str, Any]:
    from elysium_pipeline.formats import mdl_skel

    # The exporter is named by the hash of its source rather than imported: its
    # own imports reach `install`, which resolves the user's game root at import
    # time, and this pass reads the capture and nothing else. Its inverse-bind
    # rule is mirrored in `decoder_pose` in Source space rather than called.
    exporter = Path(mdl_skel.__file__).with_name("mdl_gltf.py")

    connection = open_database(session)
    try:
        flags = support(connection)
        metadata = {
            key: json.loads(value)
            for key, value in connection.execute(
                "SELECT key, value FROM capture_metadata WHERE key IN "
                "('map', 'created_utc', 'tool_git', 'index_version', "
                "'spans_tool_git')"
            )
        }
        module_hashes = modules(connection)
        run: dict[str, Any] = {}
        names: dict[int, str] = {}
        bones_report: dict[str, Any] = {"available": False, "reason": "not reached"}
        pairing: dict[str, Any] = {"available": False, "reason": "not reached"}
        three: dict[str, Any] = {"available": False, "reason": "not reached"}
        four: dict[str, Any] = {"available": False, "reason": "not reached"}
        bind: dict[str, Any] = {"available": False, "reason": "not reached"}
        ladder: dict[str, Any] = {}
        ranked: list[dict[str, Any]] = []
        worst: list[dict[str, Any]] = []
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
                        "composed_poses_no_draw_consumed",
                    )
                },
            }
            if not flags["carries_payload_store"]:
                pairing["counts"]["payload_store_absent"] = counts["draws"]
            four, four_arm = stage_four(
                connection, flags, models, batch=batch, sample=sample
            )
            three, three_arm, ladder = stage_three(
                connection, flags, models, batch=batch, sample=sample
            )
            bind = static_bind(models, names)
            ladder = finish_ladder(ladder, names, models)
            ranked = clusters([three_arm, four_arm], names, models)
            worst = (
                three_arm.bones_over_band(names, models)
                + four_arm.bones_over_band(names, models)
            )
            worst.sort(key=lambda row: -row["max"])
            worst = worst[:MAX_REPORTED_BONES]
        unreached = {
            "available": False,
            "reason": UNREACHED,
            "counts": {UNREACHED: 2 if judgeable else 0},
        }
        verdict = decide(
            flags, [bones_report, pairing, four, three, bind, unreached]
        )
    finally:
        connection.close()
    return {
        "session": session.name,
        "session_path": str(session),
        "database": str(session / DATABASE_NAME),
        "identity": {
            **metadata,
            "transform_tool_git": tool_commit(),
            "decoder_module": decoder_pose.SHIPPED,
            "decoder_sha256": file_sha256(Path(mdl_skel.__file__)),
            "evaluator_module": "research.tooling.capture.decoder_pose",
            "evaluator_sha256": file_sha256(Path(decoder_pose.__file__)),
            "exporter_module": "elysium_pipeline.formats.mdl_gltf",
            "exporter_sha256": (
                file_sha256(exporter) if exporter.is_file() else None
            ),
            "bands": {
                "source": (
                    "docs/vtmb/vtmb-animation-reverse-engineering.md 11.3"
                ),
                "quantile_resolution": round(QUANTILE_RESOLUTION, 6),
                **{name: list(edges) for name, edges in BANDS.items()},
            },
            "modules": module_hashes,
        },
        "support": flags,
        "corpus": run,
        "skeletons": bones_report,
        "pairing": pairing,
        "stages": {
            "decoded_locals": {"available": False, "reason": UNREACHED},
            "composed_locals": {"available": False, "reason": UNREACHED},
            STAGE_BONE_TO_WORLD: three,
            STAGE_SKIN_PALETTE: four,
        },
        "static_bind": bind,
        "ladder": ladder,
        "worst_bones": worst,
        "clusters": ranked,
        "verdict": verdict,
    }


def compare(reports: list[dict[str, Any]]) -> dict[str, Any]:
    def bands(report: dict[str, Any], stage: str, candidate: str) -> dict[str, int]:
        section = report["stages"].get(stage) or {}
        metrics = (section.get("candidates") or {}).get(candidate) or {}
        return metrics.get("records_by_band") or {name: 0 for name in BAND_NAMES}

    keys = [
        {row["cluster_key"] for row in report["clusters"]} for report in reports
    ]
    shared = set.intersection(*keys) if keys else set()
    return {
        "sessions": [report["session"] for report in reports],
        "maps": compared_maps(reports),
        "bone_to_world_split": [
            bands(report, STAGE_BONE_TO_WORLD, LADDER_CANDIDATE)
            for report in reports
        ],
        "skin_palette_pose_to_bone": [
            bands(report, STAGE_SKIN_PALETTE, "pose_to_bone") for report in reports
        ],
        "clusters_in_every_run": sorted(shared),
        "clusters_in_one_run_only": sorted(
            set.union(*keys) - shared if keys else set()
        ),
        "statement": (
            "Two runs are placed side by side. Which identities, frames and "
            "actors a run reached is coverage, so a repeat capture supports "
            "that a bone named mismatching in one is named mismatching in the "
            "other; it does not support the totals agreeing, and a cluster "
            "reached by one run only is coverage rather than a disagreement."
        ),
    }


def summarize(report: dict[str, Any]) -> str:
    lines = [f"{report['session']}: {report['verdict']['statement']}"]
    for stage in STAGE_ORDER:
        section = report["stages"].get(stage) or {}
        if not section.get("available"):
            lines.append(f"  {stage}: unavailable — {section.get('reason', '')}")
            continue
        candidate = (
            LADDER_CANDIDATE if stage == STAGE_BONE_TO_WORLD else "pose_to_bone"
        )
        metrics = (section.get("candidates") or {}).get(candidate) or {}
        lines.append(
            f"  {stage} ({candidate}): {section['records']:,} records over "
            f"{section['distinct_payload_keys']:,} distinct payloads "
            f"({section['elapsed_seconds']}s)"
        )
        # One line per metric rather than a worst-of: a stage whose translations
        # are excellent and whose rotations are not is the interesting case, and
        # collapsing them would report it as uniformly broken.
        for name, value in sorted(metrics.items()):
            if name == "records_by_band":
                continue
            band = value["records_by_band"]
            lines.append(
                f"    {name}: worst {value['max']:.6g}, records "
                f"{band['excellent']:,} excellent / "
                f"{band['investigate']:,} investigate / "
                f"{band['definite']:,} definite"
            )
            for field, group, label in (
                ("bone_observations_by_inheritance", "split_inheritance",
                 "Flags&0x2"),
                ("bone_observations_by_procedural", "procedural", "procedural"),
            ):
                block = (value.get(field) or {}).get(group)
                if not block or not sum(block.values()):
                    continue
                over = block["investigate"] + block["definite"]
                total = over + block["excellent"]
                lines.append(
                    f"      {label} bones: {over:,} of {total:,} bone "
                    f"observations over the band"
                )
    by_stage = (report.get("ladder") or {}).get("by_stage") or {}
    if by_stage:
        first = ", ".join(f"{name} {count:,}" for name, count in by_stage.items())
        lines.append(f"  first mismatching stage: {first}")
    for row in (report.get("ladder") or {}).get("by_bone", [])[:1]:
        lines.append(
            f"  first mismatching bone: {row['bone_name'] or row['bone']} on "
            f"{row['model']} ({row['records']:,} records)"
        )
    for row in report.get("clusters", [])[:3]:
        lines.append(
            f"  cluster {row['cluster_key']}: {row['records_over_the_band']:,} "
            f"over the band, worst {row['max']:.6g}"
        )
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sessions", nargs="+")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--batch", type=int, default=DEFAULT_BATCH)
    parser.add_argument(
        "--sample",
        type=int,
        help="Stop each arm after roughly this many records, for a smoke run.",
    )
    parser.add_argument(
        "--no-session-reports", dest="session_reports", action="store_false"
    )
    args = parser.parse_args()

    reports = []
    for value in args.sessions:
        session = resolve_session(value)
        report = verify(session, batch=args.batch, sample=args.sample)
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
            report["verdict"]["transform_difference_compared"] for report in reports
        )
        else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
