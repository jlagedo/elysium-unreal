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

Four stages are differenced, plus the chain that runs them end to end:

    decoded locals  retail's `BASE` evaluation, against the shipped decoder
                    `mdl_skel` run at the owner, animation, frame and blend
                    cells the contributions below that `BASE` witnessed;
    composed locals retail's `FINL` locals, against retail's own `BASE` — which
                    is what separates a decode error from the transition, layer
                    and controller stage nothing offline models;
    bone-to-world   retail's `POSE` first half, against composing retail's own
                    `FINL` locals under the split-inheritance rule and placing
                    them by the captured root frame;
    skin palette    retail's `POSE` second half, against retail's own captured
                    bone-to-world times the stored `StudioBone.poseToBone`.

Each stage is fed retail's own input for that stage, so an earlier error never
cascades into a later stage's numbers, and the bones retail did not write are
seeded from retail's own output rather than recomputed. The chain then feeds
each stage *our* output for the one above it and reports which stage a record
first leaves the band at, which is the propagation the per-stage numbers cannot
show.

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
transcribed rule.

The decoded-locals stage is the one that does measure shipped code, and it is
the only stage whose subject is a decoder rather than a rule. What surrounds
`mdl_skel` there is not: the frame interpolation, the blend-cell mix, the
owner-to-entity bone correspondence and the include-model position transform are
runtime bindings, so the stage carries five candidates — the complete rule, the
same rule with the position transform the entity's own include group declares,
the same rule without the frame interpolation the exporter defers to its
runtime, the correspondence the export actually ships, and plain bone-name
matching.

The shipped correspondence is not plain name matching, and reading it as such
understates the export by the whole cinematic cast. `mdl_gltf.export_cinematic`
splits a bank holding several complete actors into one bank per `BipNN` root,
keeps only that root's chain and folds its prefix onto `Bip01`; the runtime
resolves `(anim set, the scene actor's bonerename root)` to that bank, so an
actor posed on `Bip02` already takes the `Bip02` chain. `cinematic_split` models
that, and `bone_name` stays as the counterfactual it avoids.

Which of the two remap candidates governs a record is not something the capture
witnesses. An include group's transform belongs to the sequence resolving
through that group, and a cinematic bank holding several complete actors is
reached above it; the capture records the owning header and the owner-local
index but not the route. So the two candidates are reported side by side and
split per owner, and the report says which population each one closes rather
than choosing between them.

A mismatch is not a defect. It is the product: CAP4.4 ranks these clusters, and
a run that found nothing would have proved only that the corpus was too narrow.
A defect is a population meaning a comparison read the wrong values.

There is no single denominator. Every draw reaches the palette stage; only a
draw whose consumed pose build produced a composed pose, for the same entity and
model, reaches the bone-to-world stage. `carry_generation` is the last pose build
seen on the drawing thread, so the entity and model relation is required rather
than assumed, and the draws it excludes are counted. The two clip stages run
over a third population — one row per base-pose evaluation bound to the
contribution nested below it — and the chain over a fourth, the part of that
population a draw consumed.

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
import struct
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
BASE_KIND = "BASE"
SEQUENCE_KIND = "SEQP"
CELL_KIND = "ANIM"

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

STAGE_DECODED_LOCALS = "decoded_locals"
STAGE_COMPOSED_LOCALS = "composed_locals"
STAGE_BONE_TO_WORLD = "bone_to_world"
STAGE_SKIN_PALETTE = "skin_palette"
# The order the ladder walks. A record is attributed to the earliest stage whose
# error leaves the excellent band, because a later stage fed a wrong input would
# be reporting the earlier stage's failure a second time.
STAGE_ORDER = (
    STAGE_DECODED_LOCALS,
    STAGE_COMPOSED_LOCALS,
    STAGE_BONE_TO_WORLD,
    STAGE_SKIN_PALETTE,
)
# The two stages the draw-paired ladder can attribute on its own: they are the
# ones its loop holds both payloads for.
DRAW_LADDER_STAGES = (STAGE_BONE_TO_WORLD, STAGE_SKIN_PALETTE)

# The chain is not a stage. It is the four stages run end to end off our own
# output, so it carries its own arm and its own name rather than sharing one.
STAGE_CHAIN = "chain"

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

# The decoded-locals rules, as (name, bone correspondence, frame interpolation,
# include-model position transform, inherits retail's selected mask).
#
# `complete` is A.4b's stated remap with the transform byte clear — the position
# copied and the rotation copied; `include_remap` adds the transform the entity's
# own group array declares for the bones whose byte is set; `frame_key` drops the
# frame interpolation, so its residual is what `mdl_gltf` leaves to its runtime
# rather than a decode error.
#
# `cinematic_split` is the shipped path. `export_cinematic` splits a bank holding
# several complete actors into one bank per `BipNN` root, folded onto `Bip01`,
# and the runtime picks the root from the choreo scene actor's
# `bonerename "BipNN" "Bip01"`; a single-skeleton bank ships whole and resolves by
# plain name. It takes the root from the owner's own witnessed mask rather than
# from the scene, which is a join this database cannot make — those two agree on
# 180,812 of 180,812 contributions the scene bindings do reach, and the ones they
# do not reach are counted under `cinematic_root_unresolved` rather than scored.
#
# `bone_name` is plain name matching over the whole owner skeleton. It is what a
# single-skeleton bank ships and what a multi-actor bank would cost without the
# split, so it is the counterfactual the split is measured against — not a path
# anything runs.
#
# The last element is whether the candidate inherits retail's selected mask. A
# candidate reproducing retail's own evaluation does, because a bone the cell
# decoder never ran for is not a comparison. The two export candidates do not,
# and must not: the shipped path has no mask — glTFRuntime plays a clip on every
# bone whose name matches — so gating them by a mask they never consult would
# credit them for exactly the slots their correspondence gets wrong.
CLIP_CANDIDATES = (
    ("complete", "family_name", "linear", False, True),
    ("include_remap", "family_name", "linear", True, True),
    ("frame_key", "family_name", "frame", False, True),
    ("cinematic_split", "cinematic_split", "linear", False, False),
    ("bone_name", "name", "linear", False, False),
)
CLIP_LADDER_CANDIDATE = "complete"

# The composed-locals rules. Nothing offline models the transition, layer and
# controller stage between the base pose and the final locals, so both
# candidates are retail's own evaluations: the first one the pose build ran and
# the last. They differ only on a build that evaluated more than one sequence.
COMPOSED_CANDIDATES = ("base_pose", "last_evaluation")
COMPOSED_LADDER_CANDIDATE = "last_evaluation"

MAX_REPORTED_CLUSTERS = 64
MAX_REPORTED_BONES = 40
MAX_REPORTED_EXEMPLARS = 3
MAX_REPORTED_MODELS = 60
MAX_REPORTED_OWNERS = 400

DEFAULT_BATCH = 4096
# The clip stages hold a whole batch's owner-space samples at once, and a
# cinematic bank is 288 bones wide, so their batch is smaller than the fetch.
CLIP_BATCH = 512


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
    "base_records_with_no_paired_contribution",
    "paired_records_whose_cycle_disagrees",
    "paired_records_whose_renderable_offset_disagrees",
    "animation_index_outside_the_owner_declaration",
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
    "stage1_records_in_the_investigate_band": (
        "A decoded local outside the excellent band is the report's product. "
        "This is the first measurement the program has of the shipped decoder, "
        "so a count here names work rather than a fault of the instrument."
    ),
    "stage1_records_in_the_definite_band": (
        "As above, past the investigate ceiling. The per-candidate bone list "
        "names which owner and bone carry it."
    ),
    "stage1_include_remap_over_the_band": (
        "What the include-model position transform costs where it is applied. "
        "The entity's own `StudioModelGroup`+0x10 array declares a matrix per "
        "bone; A.4b's rule applies it to the position where the record's "
        "transform byte is set. Whether a given evaluation resolved through "
        "that group or through the virtual-model path above it is not something "
        "the capture witnesses, so the transform is carried as its own "
        "candidate rather than folded into the complete rule, and the two "
        "counts are read against the models the cluster list names."
    ),
    "remap_bones_the_groups_disagree_on": (
        "A model declaring two include groups whose arrays name different "
        "transforms for one bone. Which group a sequence resolved through is a "
        "runtime range no image carries, so the bone keeps its untransformed "
        "position and is counted instead of being transformed by a guess."
    ),
    "stage1_frame_key_over_the_band": (
        "What the exporter's baked key costs before its runtime interpolates: "
        "`mdl_gltf` writes one glTF key per frame and glTFRuntime samples "
        "between them, so this candidate holds retail's own frame against our "
        "nearest key and its residual is the interpolation, not a decode error."
    ),
    "stage1_cinematic_split_over_the_band": (
        "What the shipped export costs. `export_cinematic` splits a bank "
        "holding several complete actors into one bank per `BipNN` root, folded "
        "onto `Bip01`, and the runtime picks the root from the choreo scene "
        "actor's `bonerename`; a one-skeleton bank ships whole and resolves by "
        "plain name. This is the residual to read as the export's, and it is "
        "the only stage-1 candidate that is a path anything runs."
    ),
    "stage1_bone_name_over_the_band": (
        "What plain bone-name matching would cost — the counterfactual the "
        "split avoids, not a path anything runs. It is exact against a "
        "one-skeleton bank and wrong against a cinematic bank holding several "
        "bipeds, where the name alone cannot say which chain an actor is, so "
        "the gap between it and `cinematic_split` is what the split buys."
    ),
    "stage2_records_in_the_investigate_band": (
        "A final local that differs from the base pose it was built from. "
        "Nothing offline models the transition, layer and controller stage "
        "between them, so this count is that stage's footprint rather than a "
        "disagreement with a rule."
    ),
    "stage2_records_in_the_definite_band": (
        "As above, past the investigate ceiling."
    ),
    "stage2_base_pose_over_the_band": (
        "The same measured against the *first* evaluation of a pose build "
        "rather than the last, which is what a build that evaluated several "
        "sequences separates."
    ),
    "cells_that_decoded_no_bone": (
        "A.4b's cell whose mask selects no bone: it reads sixteen header bytes "
        "and decodes nothing, so the evaluation wrote no local and there is "
        "nothing to difference."
    ),
    "records_sampled_at_the_last_frame": (
        "The witnessed frame is the clip's last, so its successor is clamped "
        "to itself. A.4 records that retail's own look-ahead leaves the track "
        "there; clamping is this side declining to invent the byte rather than "
        "a claim about what retail read."
    ),
    "witnessed_frame_disagrees_with_the_cycle_rule": (
        "`floor((numframes - 1) * cycle)` did not reproduce the frame the cell "
        "decoder was witnessed at. A.4b establishes the rule over two complete "
        "runs, so a count here is a corpus this pass has not seen rather than "
        "an instrument fault — and the witnessed frame is used either way."
    ),
    "owner_family_ambiguous": (
        "The owner bones a contribution's mask selects carry more than one "
        "biped family token, or none, so which chain of a multi-skeleton bank "
        "the actor is cannot be read off the mask. The record leaves the "
        "family-name candidate rather than being matched by guess."
    ),
    "cinematic_root_unresolved": (
        "A contribution from a bank holding several complete actors whose "
        "(entity model, bank) pair never had one biped family named by a mask, "
        "so which per-root bank the export ships for it cannot be read back out "
        "of this database. The shipped-path candidate leaves the record rather "
        "than scoring it against a chain chosen by guess; the complete rule, "
        "which reads the family per contribution, still scores it."
    ),
    "bones_selected_with_no_owner_source": (
        "An entity bone the pose build selected that the owner's own mask has "
        "no counterpart for. The bank drove the rest of the skeleton and left "
        "this slot to whatever the buffer held, so it is not a comparison."
    ),
    "owner_bones_decoded_with_no_entity_target": (
        "An owner bone the cell decoded that the entity skeleton has no bone "
        "for. A 288-bone cinematic bank holds several complete actors, so most "
        "of what it decodes belongs to somebody else."
    ),
    "generations_with_more_than_one_base_evaluation": (
        "A pose build that evaluated more than one sequence — the transition "
        "and autoplay layers A.4b names. The two composed-locals candidates "
        "differ only on these."
    ),
    "evaluations_that_are_neither_the_first_nor_the_last_of_their_build": (
        "The middle evaluation of a pose build that ran three sequences. "
        "Neither composed-locals candidate covers it, because neither the base "
        "pose nor the last evaluation is what it is."
    ),
    "owner_image_absent": (
        "A contribution names an owning studio header the run never captured "
        "an image of, so there is no clip to decode. It is coverage of the "
        "census rather than a wrong comparison."
    ),
    "chain_records_in_the_investigate_band": (
        "The chained run: our decoded locals carried through composition and "
        "the palette with no retail value fed in between. This is propagation "
        "rather than a stage's own error, which is why it is reported apart "
        "from the per-stage counts."
    ),
    "chain_records_in_the_definite_band": (
        "As above, past the investigate ceiling."
    ),
    "bones_seeded_from_retail_because_no_clip_drove_them": (
        "A selected bone the fired clip has no source for. The chain has "
        "nothing of its own to carry forward, so it takes retail's own local "
        "and counts the slot rather than composing a bind pose retail did not "
        "use."
    ),
    "pose_builds_with_no_draw": (
        "An evaluation whose pose build the renderer drew nothing under, so "
        "the chain stops at the composed locals for it. CAP1.3 measured that "
        "population; it is coverage, not a broken join."
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
        "carries_base_poses": BASE_KIND in kinds,
        "carries_contributions": {SEQUENCE_KIND, CELL_KIND} <= kinds
        and {"contribution", "owner_checksum", "channel_frame"} <= columns,
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


def _payload_join(
    flags: dict[str, Any], alias: str, key: str, *, left: bool = False
) -> tuple[str, str]:
    """How to reach a payload's bytes, with or without the content store.

    `left` is for a key that is allowed to be absent — a pose build the renderer
    drew nothing under has no draw payload, and dropping the group would turn a
    coverage bound into a missing comparison.
    """
    kind = "LEFT JOIN" if left else "JOIN"
    if flags["carries_payload_store"]:
        return (
            f"{kind} {PAYLOAD_TABLE} {alias} ON {alias}.id = g.{key}",
            f"{alias}.bytes",
        )
    return (
        f"{kind} {flags['event_table']} {alias} ON {alias}.id = g.{key}",
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
# Binding a base pose to the contributions nested below it
# --------------------------------------------------------------------------


def bind_contributions(
    connection: sqlite3.Connection, flags: dict[str, Any]
) -> dict[str, int]:
    """Bind each `BASE` evaluation to the sequence and cells that produced it.

    The binding is an identity, not a time. A pose build opens one generation;
    inside it every evaluated sequence emits its cells, then its own `SEQP`, then
    the `BASE` snapshot of the buffer it wrote — so the k-th contribution of a
    generation belongs to its k-th evaluation, and the dense global sequence
    counter every record carries orders them without a clock being read.

    Three witnesses check that rather than assume it, and each disagreement is a
    defect because a wrong pairing would difference one sequence's decode
    against another's output: the contribution must precede the evaluation it is
    bound to, the two must carry the same cycle, and the contribution's entity
    must be the evaluation's renderable subobject at the fixed offset CAP1.3
    measured.
    """
    table = flags["event_table"]
    # Without the content store a record's own id is its payload key, exactly as
    # `_payload_join` resolves it.
    key = "payload_id" if flags["carries_payload_store"] else "id"
    connection.executescript(
        f"""
        CREATE TEMP TABLE base_pose AS
        SELECT id, generation, sequence_number, client_entity, checksum,
               bone_count, sample_phase, {key} AS payload_id,
               row_number() OVER (
                   PARTITION BY generation ORDER BY sequence_number
               ) AS rank,
               count(*) OVER (PARTITION BY generation) AS evaluations
        FROM {table} WHERE kind = '{BASE_KIND}';

        CREATE TEMP TABLE sequence_contribution AS
        SELECT id, generation, sequence_number, generation_entity, owner_checksum,
               bone_count AS owner_bones, contribution, sequence_index, num_blends,
               cycle, blend_weight, pose_parameter_bytes, {key} AS payload_id,
               row_number() OVER (
                   PARTITION BY generation ORDER BY sequence_number
               ) AS rank
        FROM {table} WHERE kind = '{SEQUENCE_KIND}';

        CREATE TEMP TABLE cell_signature AS
        SELECT contribution,
               count(*) AS cells,
               max(CASE WHEN place = 1 THEN animation_index END) AS anim0,
               max(CASE WHEN place = 1 THEN channel_frame END) AS frame0,
               max(CASE WHEN place = 1 THEN channel_fraction END) AS fraction0,
               max(CASE WHEN place = 1 THEN channel_quaternion_calls END) AS decoded0,
               max(CASE WHEN place = 2 THEN animation_index END) AS anim1,
               max(CASE WHEN place = 2 THEN channel_frame END) AS frame1,
               max(CASE WHEN place = 2 THEN channel_fraction END) AS fraction1,
               max(CASE WHEN place = 2 THEN channel_quaternion_calls END) AS decoded1
        FROM (
            SELECT contribution, animation_index, channel_frame, channel_fraction,
                   channel_quaternion_calls,
                   row_number() OVER (
                       PARTITION BY contribution ORDER BY ordinal
                   ) AS place
            FROM {table} WHERE kind = '{CELL_KIND}'
        )
        GROUP BY contribution;
        CREATE INDEX cell_signature_contribution ON cell_signature(contribution);

        CREATE TEMP TABLE final_locals AS
        SELECT generation, id, client_entity, checksum, bone_count,
               {key} AS payload_id
        FROM {table} WHERE kind = '{COMPOSED_KIND}';
        CREATE INDEX final_locals_generation ON final_locals(generation);

        -- One draw per pose build: the first the renderer consumed under it,
        -- required to name the same actor and model by the fixed renderable
        -- offset rather than by having arrived next.
        CREATE TEMP TABLE chain_draw AS
        SELECT generation, payload_id AS draw_key FROM (
            SELECT f.generation AS generation, d.{key} AS payload_id,
                   row_number() OVER (
                       PARTITION BY f.generation ORDER BY d.id
                   ) AS place
            FROM {table} d
            JOIN final_locals f ON f.generation = d.carry_generation
            WHERE d.kind = '{DRAW_KIND}'
              AND d.client_entity = f.client_entity + {RENDERABLE_OFFSET}
              AND d.checksum = f.checksum
              AND d.bone_count = f.bone_count
        ) WHERE place = 1;
        CREATE INDEX chain_draw_generation ON chain_draw(generation);

        CREATE TEMP TABLE clip_group AS
        SELECT b.checksum AS checksum, b.bone_count AS bone_count,
               s.owner_checksum AS owner_checksum, s.owner_bones AS owner_bones,
               c.cells AS cells, c.anim0 AS anim0, c.frame0 AS frame0,
               c.fraction0 AS fraction0, c.decoded0 AS decoded0,
               c.anim1 AS anim1, c.frame1 AS frame1, c.fraction1 AS fraction1,
               c.decoded1 AS decoded1, s.blend_weight AS blend_weight,
               s.cycle AS cycle, s.pose_parameter_bytes AS pose_parameter_bytes,
               b.rank AS rank, b.evaluations AS evaluations,
               b.payload_id AS base_key, s.payload_id AS sequence_key,
               f.payload_id AS final_key, d.draw_key AS draw_key,
               count(*) AS records, min(b.id) AS exemplar
        FROM base_pose b
        JOIN sequence_contribution s USING (generation, rank)
        JOIN cell_signature c ON c.contribution = s.contribution
        LEFT JOIN final_locals f ON f.generation = b.generation
        LEFT JOIN chain_draw d ON d.generation = b.generation
        GROUP BY b.checksum, b.bone_count, s.owner_checksum, s.owner_bones,
                 c.cells, c.anim0, c.frame0, c.fraction0, c.decoded0,
                 c.anim1, c.frame1, c.fraction1, c.decoded1, s.blend_weight,
                 s.cycle, s.pose_parameter_bytes, b.rank, b.evaluations,
                 b.payload_id, s.payload_id, f.payload_id, d.draw_key;
        """
    )

    def scalar(sql: str) -> int:
        return int(connection.execute(sql).fetchone()[0] or 0)

    evaluations = scalar("SELECT count(*) FROM base_pose")
    paired = scalar(
        "SELECT count(*) FROM base_pose b "
        "JOIN sequence_contribution s USING (generation, rank)"
    )
    return {
        "base_evaluations": evaluations,
        "sequence_contributions": scalar("SELECT count(*) FROM sequence_contribution"),
        "cells": scalar("SELECT sum(cells) FROM cell_signature"),
        "paired": paired,
        "paired_with_cells": scalar(
            "SELECT count(*) FROM base_pose b "
            "JOIN sequence_contribution s USING (generation, rank) "
            "JOIN cell_signature c ON c.contribution = s.contribution"
        ),
        "base_records_with_no_paired_contribution": evaluations - paired,
        "paired_records_whose_cycle_disagrees": scalar(
            "SELECT count(*) FROM base_pose b "
            "JOIN sequence_contribution s USING (generation, rank) "
            "WHERE s.cycle <> b.sample_phase"
        ),
        "paired_records_whose_renderable_offset_disagrees": scalar(
            "SELECT count(*) FROM base_pose b "
            "JOIN sequence_contribution s USING (generation, rank) "
            f"WHERE s.generation_entity <> b.client_entity + {RENDERABLE_OFFSET}"
        ),
        "generations_with_more_than_one_base_evaluation": scalar(
            "SELECT count(*) FROM (SELECT generation FROM base_pose "
            "GROUP BY generation HAVING count(*) > 1)"
        ),
        "clip_groups": scalar("SELECT count(*) FROM clip_group"),
        "pose_builds_with_no_draw": scalar(
            "SELECT count(*) FROM final_locals f "
            "WHERE NOT EXISTS (SELECT 1 FROM chain_draw d "
            "WHERE d.generation = f.generation)"
        ),
    }


# --------------------------------------------------------------------------
# Stages 1 and 2 — the decoded and composed locals, and the chain
# --------------------------------------------------------------------------


class ClipCache:
    """A bounded cache of clips decoded by the shipped decoder.

    The corpus fires a few dozen distinct `(owner, animation)` identities and
    some of them are five-thousand-frame cinematic banks over 288 bones, so
    holding them all would cost more than the database read. The loop walks its
    groups in owner and animation order, which is what makes a cache this small
    enough: each identity is decoded once and evicted once the walk is past it.
    """

    def __init__(self, images: dict[int, bytes], capacity: int = 4) -> None:
        self.images = images
        self.capacity = capacity
        self.order: list[tuple[int, int]] = []
        self.store: dict[tuple[int, int], decoder_pose.Clip] = {}
        self.decoded = 0
        self.faults = 0

    def get(self, owner: int, animation: int) -> decoder_pose.Clip | None:
        key = (owner, animation)
        if key in self.store:
            return self.store[key]
        image = self.images.get(owner)
        if image is None:
            return None
        clip = decoder_pose.clip_from_image(image, animation)
        self.decoded += 1
        if clip.fault:
            self.faults += 1
        while len(self.order) >= self.capacity:
            self.store.pop(self.order.pop(0), None)
        self.order.append(key)
        self.store[key] = clip
        return clip


# The `clip_group` columns, by position. Named because the row is wide and a
# bare index into it is the kind of thing that reads one field as another.
(
    G_CHECKSUM, G_BONES, G_OWNER, G_OWNER_BONES, G_CELLS,
    G_ANIM0, G_FRAME0, G_FRACTION0, G_ANIM1, G_FRAME1, G_FRACTION1,
    G_WEIGHT, G_CYCLE, G_MASK_OFFSET, G_RANK, G_EVALUATIONS,
    G_DECODED0, G_DECODED1,
    G_RECORDS, G_EXEMPLAR, G_BASE, G_SEQUENCE, G_FINAL, G_DRAW,
) = range(24)


def bone_remaps(
    connection: sqlite3.Connection, models: dict[int, Skeleton]
) -> tuple[dict[int, decoder_pose.BoneRemap], int]:
    """One merged remap array per including model, plus the bones in dispute.

    A model that includes two banks carries two arrays. Which one a sequence
    resolved through is the group's virtual range, a runtime field no image
    holds, so a bone the arrays agree on is taken and a bone they disagree on
    keeps its untransformed position and is counted.
    """
    rows: dict[int, list[decoder_pose.BoneRemap]] = {}
    for checksum, count, records in connection.execute(
        "SELECT owner_checksum, record_count, records FROM bone_remap_group "
        "WHERE inside_image = 1 AND records IS NOT NULL ORDER BY group_index"
    ):
        checksum, count = int(checksum), int(count)
        skeleton = models.get(checksum)
        if skeleton is None or skeleton.bones != count:
            continue
        if len(records) < count * decoder_pose.REMAP_RECORD_BYTES:
            continue
        rows.setdefault(checksum, []).append(
            decoder_pose.bone_remap(bytes(records), count)
        )
    resolved: dict[int, decoder_pose.BoneRemap] = {}
    disputed = 0
    for checksum, group in rows.items():
        first = group[0]
        transform = first.transform.copy()
        matrix = first.matrix.copy()
        for other in group[1:]:
            differs = (other.transform != transform) | (
                np.abs(other.matrix - matrix).max(axis=(1, 2)) > 1.0e-6
            )
            differs &= transform | other.transform
            disputed += int(differs.sum())
            transform = transform & ~differs
        resolved[checksum] = decoder_pose.BoneRemap(
            source=first.source, transform=transform, matrix=matrix
        )
    return resolved, disputed


def split_roots(
    connection: sqlite3.Connection,
    flags: dict[str, Any],
    names_of: Any,
) -> tuple[dict[tuple[int, int], str], int]:
    """The `BipNN` root each (entity model, multi-actor bank) pair is posed on.

    The shipped path takes this root from the choreo scene actor's `bonerename`,
    which is a fact about the scene and not about the clip: the same sequence of
    the same bank is witnessed under four different roots, one per cast member,
    so nothing per-sequence can carry it. This database holds no join from a
    pose build to the scene actor that started it, so the root is read back off
    the owner's own selected mask instead — the two agree wherever the scene
    bindings do reach, and a pair whose masks never name one family is left
    unresolved rather than guessed.

    The pass is separate from the stage-1 loop because that loop flushes in
    fixed-size batches: resolving a pair inside a batch would make the answer
    depend on where the batch boundary fell.
    """
    join, column = _payload_join(flags, "sp", "sequence_key")
    roots: dict[tuple[int, int], set[str]] = {}
    for checksum, owner, owner_bones, offset, payload in connection.execute(
        "SELECT g.checksum, g.owner_checksum, g.owner_bones, "
        f"g.pose_parameter_bytes, {column} FROM clip_group g {join}"
    ):
        checksum, owner, owner_bones = int(checksum), int(owner), int(owner_bones)
        owner_names = names_of(owner)
        if owner_names is None or len(decoder_pose.biped_families(owner_names)) < 2:
            continue
        width = ((owner_bones + 31) // 32) * 4
        if payload is None or len(payload) < int(offset) + width:
            continue
        family = _one_family(
            owner_names,
            decoder_pose.payload_selected(payload, owner_bones, offset=int(offset)),
        )
        if family:
            roots.setdefault((checksum, owner), set()).add(family)
    resolved = {pair: next(iter(found)) for pair, found in roots.items() if len(found) == 1}
    return resolved, len(roots) - len(resolved)


def _one_family(names: tuple[str, ...], selected: np.ndarray) -> str | None:
    """The single biped family a mask selects.

    Three answers, and the middle one is the reason this is not a set lookup. A
    skeleton with one family names it; a skeleton with none — a prop, a
    doorknob, a manhole — has no family to divide out and matches on its plain
    names, which is the empty string; and a mask spanning several families
    cannot say which chain the actor is, which is `None` and is counted.
    """
    found = decoder_pose.biped_families(names, selected)
    if not found:
        return ""
    return found.pop() if len(found) == 1 else None


def _scatter(values: np.ndarray, columns: np.ndarray, width: int) -> np.ndarray:
    """Place a `(N, k)` per-bone error back into the skeleton's full width.

    A bone the correspondence never reached lands on zero, which is what every
    other stage does with a slot it did not compare. The accumulator's own bone
    count has to match the skeleton either way, so the two cannot be merged.
    """
    full = np.zeros((values.shape[0], width), dtype=np.float64)
    full[:, columns] = values
    return full


def _blend_weight(text: str | None) -> float:
    """The axis-0 weight a contribution recorded, or zero if it carried none."""
    if not text:
        return 0.0
    try:
        value = json.loads(text)
    except ValueError:
        return 0.0
    return float(value[0]) if isinstance(value, list) and value else 0.0


def _fold_locals(
    arm: StageArm,
    checksum: int,
    candidate: str,
    bones: int,
    translation: np.ndarray,
    rotation: np.ndarray,
    multiplier: np.ndarray,
    exemplars: list[int],
) -> np.ndarray:
    """Fold one candidate's local difference; return its per-record worst band."""
    arm.add(checksum, candidate, "translation", bones, translation, multiplier)
    arm.add(checksum, candidate, "rotation_degrees", bones, rotation, multiplier)
    bands = {
        "translation": decoder_pose.band_of(translation, BANDS["local_translation"]),
        "rotation_degrees": decoder_pose.band_of(rotation, BANDS["rotation_degrees"]),
    }
    band = np.maximum.reduce(list(bands.values()))
    for metric, value in bands.items():
        arm.add_records(candidate, metric, value.max(axis=1), multiplier)
    worst = band.max(axis=1)
    arm.add_records(candidate, ANY_METRIC, worst, multiplier)
    over = band > 0
    for index, exemplar in enumerate(exemplars):
        for bone in np.flatnonzero(over[index]):
            arm.witness(candidate, checksum, int(bone), exemplar)
    return worst


def clip_stages(
    connection: sqlite3.Connection,
    flags: dict[str, Any],
    models: dict[int, Skeleton],
    images: dict[int, bytes],
    remaps: dict[int, decoder_pose.BoneRemap],
    *,
    batch: int,
    sample: int | None,
) -> tuple[
    dict[str, Any], StageArm, dict[str, Any], StageArm, dict[str, Any], StageArm
]:
    """Difference the decoded locals, the composed locals, and the chain.

    One loop, because all three read the same group and the clip decode is the
    expensive part of it: a group's `BASE` payload is retail's decoded locals,
    the `FINL` payload of its generation is retail's composed locals and its root
    frame, and the draw the build fed carries retail's bone-to-world and palette.

    The two stage arms are fed retail's own input for their stage — our decode is
    held against `BASE`, and `BASE` is held against `FINL` — while the chain
    carries our own decode all the way to the palette with nothing of retail's
    fed in between except the slots no fired clip drove, which it counts.
    """
    decoded_arm = StageArm(
        STAGE_DECODED_LOCALS,
        {"translation": "local_translation", "rotation_degrees": "rotation_degrees"},
    )
    composed_arm = StageArm(
        STAGE_COMPOSED_LOCALS,
        {"translation": "local_translation", "rotation_degrees": "rotation_degrees"},
    )
    chain_arm = StageArm(
        STAGE_CHAIN,
        {
            "world_translation": "world_translation",
            "rotation_degrees": "rotation_degrees",
            "palette_translation": "world_translation",
        },
    )
    decoded_counts = {
        "entity_image_absent": 0,
        "owner_image_absent": 0,
        "payload_width_disagrees_with_bone_count": 0,
        "payload_identity_missing": 0,
        "animation_index_outside_the_owner_declaration": 0,
        "animation_unreadable": 0,
        "cells_that_decoded_no_bone": 0,
        "records_sampled_at_the_last_frame": 0,
        "witnessed_frame_disagrees_with_the_cycle_rule": 0,
        "owner_family_ambiguous": 0,
        "cinematic_root_unresolved": 0,
        "bones_outside_the_selected_mask": 0,
        "bones_selected_with_no_owner_source": 0,
        "owner_bones_decoded_with_no_entity_target": 0,
        "records_excluded_by_the_sample_bound": 0,
    }
    composed_counts = {
        "payload_width_disagrees_with_bone_count": 0,
        "bones_outside_the_selected_mask": 0,
        "evaluations_that_are_neither_the_first_nor_the_last_of_their_build": 0,
    }
    chain_counts = {
        "pose_builds_with_no_draw": 0,
        "payload_width_disagrees_with_bone_count": 0,
        "root_transform_not_rigid": 0,
        "bones_seeded_from_retail_because_no_clip_drove_them": 0,
        "bones_the_renderer_never_wrote": 0,
    }
    by_stage = {stage: 0 for stage in STAGE_ORDER}
    by_stage["none"] = 0
    chain_first_bone: dict[tuple[int, int, str], int] = {}
    # Which owner a record's clip came from is the identity the decoded stage
    # turns on and the only one that separates its two remap candidates: a
    # shared bank is reached through an include group and carries that group's
    # position transform, while a cinematic bank holding several actors is
    # reached above it and does not. Nothing in the capture says which route an
    # evaluation took, so the split is reported per owner rather than decided.
    by_owner: dict[tuple[int, int, str], np.ndarray] = {}

    base_join, base_column = _payload_join(flags, "bp", "base_key")
    sequence_join, sequence_column = _payload_join(flags, "sp", "sequence_key")
    final_join, final_column = _payload_join(flags, "fp", "final_key", left=True)
    draw_join, draw_column = _payload_join(flags, "dp", "draw_key", left=True)
    sql = (
        "SELECT g.checksum, g.bone_count, g.owner_checksum, g.owner_bones, "
        "g.cells, g.anim0, g.frame0, g.fraction0, g.anim1, g.frame1, "
        "g.fraction1, g.blend_weight, g.cycle, g.pose_parameter_bytes, "
        "g.rank, g.evaluations, g.decoded0, g.decoded1, g.records, g.exemplar, "
        f"{base_column}, {sequence_column}, {final_column}, {draw_column} "
        f"FROM clip_group g {base_join} {sequence_join} {final_join} {draw_join} "
        "ORDER BY g.owner_checksum, g.anim0, g.anim1, g.checksum, g.bone_count"
    )

    started = time.monotonic()
    clips = ClipCache(images)
    correspondences: dict[tuple, tuple[np.ndarray, np.ndarray]] = {}
    bone_names: dict[int, tuple[str, ...]] = {}
    descendants: dict[int, np.ndarray] = {}
    groups = 0
    records = 0
    composed_records = 0
    chain_records = 0
    pending: list[tuple] = []
    pending_key: tuple | None = None

    def names_of(checksum: int) -> tuple[str, ...]:
        if checksum not in bone_names:
            bone_names[checksum] = (
                models[checksum].names
                if checksum in models
                else tuple(decoder_pose.mdl_skel.bone_names(images[checksum]))
            )
        return bone_names[checksum]

    def known_names(checksum: int) -> tuple[str, ...] | None:
        if checksum not in models and checksum not in images:
            return None
        return names_of(checksum)

    # Which chain of a multi-actor bank each entity model is posed on, resolved
    # once so the answer does not depend on where a batch boundary fell.
    roots, unresolved_pairs = split_roots(connection, flags, known_names)

    def flush() -> None:
        nonlocal groups, records, composed_records, chain_records
        if not pending:
            return
        rows = list(pending)
        pending.clear()
        checksum = int(rows[0][G_CHECKSUM])
        bones = int(rows[0][G_BONES])
        owner = int(rows[0][G_OWNER])
        owner_bones = int(rows[0][G_OWNER_BONES])
        skeleton = models[checksum]
        entity_names = names_of(checksum)
        owner_names = names_of(owner)
        # Only a bank carrying more than one biped is split into per-root banks;
        # anything else ships whole, so the shipped path is plain name matching
        # and the two export candidates coincide.
        multi_actor_owner = len(decoder_pose.biped_families(owner_names)) > 1
        multiplier = np.array([int(row[G_RECORDS]) for row in rows], dtype=np.int64)
        exemplars = [int(row[G_EXEMPLAR]) for row in rows]
        groups += len(rows)
        records += int(multiplier.sum())

        locals_pairs = [
            decoder_pose.payload_local_pose(row[G_BASE], bones) for row in rows
        ]
        base_pos = np.stack([pair[0] for pair in locals_pairs])
        base_quat = np.stack([pair[1] for pair in locals_pairs])
        base_selected = np.stack(
            [decoder_pose.payload_selected(row[G_BASE], bones) for row in rows]
        )
        owner_selected = np.stack(
            [
                decoder_pose.payload_selected(
                    row[G_SEQUENCE], owner_bones, offset=int(row[G_MASK_OFFSET])
                )
                for row in rows
            ]
        )
        decoded_counts["bones_outside_the_selected_mask"] += int(
            (multiplier[:, None] * ~base_selected).sum()
        )

        frame0 = np.array([int(row[G_FRAME0]) for row in rows], dtype=np.int64)
        fraction0 = np.array([float(row[G_FRACTION0]) for row in rows])
        frame1 = np.array([int(row[G_FRAME1] or 0) for row in rows], dtype=np.int64)
        fraction1 = np.array([float(row[G_FRACTION1] or 0.0) for row in rows])
        weight = np.array([_blend_weight(row[G_WEIGHT]) for row in rows])
        cycle = np.array([float(row[G_CYCLE] or 0.0) for row in rows])
        two_cell = np.array([int(row[G_CELLS]) > 1 for row in rows])

        first = clips.get(owner, int(rows[0][G_ANIM0]))
        if first is None or first.fault or not first.frames:
            decoded_counts["animation_unreadable"] += int(multiplier.sum())
            return
        second = (
            clips.get(owner, int(rows[0][G_ANIM1]))
            if rows[0][G_ANIM1] is not None and bool(two_cell.any())
            else None
        )
        decoded_counts["records_sampled_at_the_last_frame"] += int(
            multiplier[frame0 >= first.frames - 1].sum()
        )
        # A.4b's frame rule, checked rather than used. The witnessed frame is
        # what the cell decoder was handed, so a disagreement is a corpus this
        # pass has not seen rather than a reason to recompute it.
        decoded_counts["witnessed_frame_disagrees_with_the_cycle_rule"] += int(
            multiplier[
                np.floor((first.frames - 1) * cycle).astype(np.int64) != frame0
            ].sum()
        )

        # --- stage 2: retail's own base pose against retail's final locals ---
        composed_band = np.zeros(len(rows), dtype=np.int8)
        composed_ready = np.zeros(len(rows), dtype=bool)
        final_width = bones * 7 * 4 + ((bones + 31) // 32) * 4
        usable = [
            index
            for index, row in enumerate(rows)
            if row[G_FINAL] is not None
            and len(row[G_FINAL]) == final_width + ROOT_TRANSFORM_BYTES
        ]
        composed_counts["payload_width_disagrees_with_bone_count"] += int(
            multiplier[
                [
                    index
                    for index, row in enumerate(rows)
                    if row[G_FINAL] is not None
                    and len(row[G_FINAL]) != final_width + ROOT_TRANSFORM_BYTES
                ]
            ].sum()
        )
        final_pos = final_quat = final_selected = final_root = None
        if usable:
            picked = np.array(usable, dtype=np.int64)
            pairs = [decoder_pose.payload_local_pose(rows[i][G_FINAL], bones) for i in usable]
            final_pos = np.stack([pair[0] for pair in pairs])
            final_quat = np.stack([pair[1] for pair in pairs])
            final_selected = np.stack(
                [decoder_pose.payload_selected(rows[i][G_FINAL], bones) for i in usable]
            )
            final_root = np.stack(
                [
                    decoder_pose.payload_matrices(
                        rows[i][G_FINAL], 1, offset=final_width
                    )[0]
                    for i in usable
                ]
            )
            composed_counts["bones_outside_the_selected_mask"] += int(
                (multiplier[picked, None] * ~final_selected).sum()
            )
            rank = np.array([int(rows[i][G_RANK]) for i in usable], dtype=np.int64)
            total = np.array(
                [int(rows[i][G_EVALUATIONS]) for i in usable], dtype=np.int64
            )
            composed_counts[
                "evaluations_that_are_neither_the_first_nor_the_last_of_their_build"
            ] += int(multiplier[picked][(rank != 1) & (rank != total)].sum())
            live = final_selected & base_selected[picked]
            translation = np.where(
                live,
                decoder_pose.position_error(base_pos[picked], final_pos),
                0.0,
            )
            rotation = np.where(
                live,
                decoder_pose.rotation_angle_degrees(base_quat[picked], final_quat),
                0.0,
            )
            for candidate, keep in (
                ("base_pose", rank == 1),
                ("last_evaluation", rank == total),
            ):
                if not keep.any():
                    continue
                band = _fold_locals(
                    composed_arm,
                    checksum,
                    candidate,
                    bones,
                    translation[keep],
                    rotation[keep],
                    multiplier[picked][keep],
                    [exemplars[i] for i, take in zip(usable, keep) if take],
                )
                if candidate == COMPOSED_LADDER_CANDIDATE:
                    composed_band[picked[keep]] = band
                    composed_ready[picked[keep]] = True
                    composed_records += int(multiplier[picked][keep].sum())

        # --- stage 1: the shipped decoder against retail's base pose ---
        family_rows: dict[tuple[str | None, str | None], list[int]] = {}
        for index in range(len(rows)):
            family_rows.setdefault(
                (
                    _one_family(owner_names, owner_selected[index]),
                    _one_family(entity_names, base_selected[index]),
                ),
                [],
            ).append(index)

        # The bank the export ships for this pair, and so the chain the shipped
        # path reaches. It is a property of the (entity, bank) pair rather than
        # of the contribution, because the scene names the actor's root once and
        # every contribution under it plays that one bank — including the ones
        # whose own mask selects no biped bone at all.
        split_root = roots.get((checksum, owner)) if multi_actor_owner else None

        for (owner_family, entity_family), members in family_rows.items():
            picked = np.array(members, dtype=np.int64)
            for candidate, matching, interpolation, transform, masked in CLIP_CANDIDATES:
                family = matching == "family_name"
                split = matching == "cinematic_split"
                if transform and checksum not in remaps:
                    continue
                if family and owner_family is None:
                    if candidate == CLIP_LADDER_CANDIDATE:
                        decoded_counts["owner_family_ambiguous"] += int(
                            multiplier[picked].sum()
                        )
                    continue
                if split and multi_actor_owner and split_root is None:
                    decoded_counts["cinematic_root_unresolved"] += int(
                        multiplier[picked].sum()
                    )
                    continue
                key = (
                    checksum,
                    owner,
                    matching,
                    (entity_family or None) if family else None,
                    split_root if split else ((owner_family or None) if family else None),
                )
                if key not in correspondences:
                    correspondences[key] = decoder_pose.correspondence(
                        entity_names,
                        owner_names,
                        matching=matching,
                        entity_family=key[3],
                        owner_family=key[4],
                    )
                target, source = correspondences[key]
                if target.size == 0:
                    continue
                grid = np.ix_(picked, target)
                live = base_selected[grid]
                if masked:
                    live = live & owner_selected[np.ix_(picked, source)]
                if candidate == CLIP_LADDER_CANDIDATE:
                    driven = np.zeros((len(picked), bones), dtype=bool)
                    driven[:, target] = live
                    decoded_counts["bones_selected_with_no_owner_source"] += int(
                        (multiplier[picked, None] * (base_selected[picked] & ~driven)).sum()
                    )
                    reached = np.zeros((len(picked), owner_bones), dtype=bool)
                    reached[:, source] = live
                    decoded_counts["owner_bones_decoded_with_no_entity_target"] += int(
                        (
                            multiplier[picked, None]
                            * (owner_selected[picked] & ~reached)
                        ).sum()
                    )
                ours = decoder_pose.sample_clip(
                    first, frame0[picked], fraction0[picked], interpolation, source
                )
                if second is not None and not second.fault and second.frames:
                    other = decoder_pose.sample_clip(
                        second, frame1[picked], fraction1[picked], interpolation, source
                    )
                    mixed = decoder_pose.blend_cells(ours, other, weight[picked])
                    take = two_cell[picked][:, None, None]
                    ours = (
                        np.where(take, mixed[0], ours[0]),
                        np.where(take, mixed[1], ours[1]),
                    )
                if transform:
                    ours = (
                        decoder_pose.apply_remap(ours[0], remaps[checksum], target),
                        ours[1],
                    )
                translation = _scatter(
                    np.where(
                        live, decoder_pose.position_error(ours[0], base_pos[grid]), 0.0
                    ),
                    target,
                    bones,
                )
                rotation = _scatter(
                    np.where(
                        live,
                        decoder_pose.rotation_angle_degrees(ours[1], base_quat[grid]),
                        0.0,
                    ),
                    target,
                    bones,
                )
                band = _fold_locals(
                    decoded_arm,
                    checksum,
                    candidate,
                    bones,
                    translation,
                    rotation,
                    multiplier[picked],
                    [exemplars[index] for index in members],
                )
                totals = by_owner.setdefault(
                    (checksum, owner, candidate),
                    np.zeros(len(BAND_NAMES), dtype=np.int64),
                )
                for index in range(len(BAND_NAMES)):
                    totals[index] += int(multiplier[picked][band == index].sum())
                if candidate != CLIP_LADDER_CANDIDATE or final_root is None:
                    continue

                # --- the chain: our decode carried forward, nothing fed back ---
                seat = {index: place for place, index in enumerate(usable)}
                chained = [
                    (place, seat[index])
                    for place, index in enumerate(members)
                    if index in seat and rows[index][G_DRAW] is not None
                ]
                chain_counts["pose_builds_with_no_draw"] += int(
                    multiplier[
                        [index for index in members if rows[index][G_DRAW] is None]
                    ].sum()
                )
                if not chained:
                    continue
                here = np.array([place for place, _ in chained], dtype=np.int64)
                there = np.array([seat for _, seat in chained], dtype=np.int64)
                rows_here = [members[place] for place in here]
                weights = multiplier[np.array(rows_here, dtype=np.int64)]
                draw_width = bones * MATRIX_BYTES * 2
                wrong = [
                    index
                    for index in rows_here
                    if len(rows[index][G_DRAW]) != draw_width
                ]
                if wrong:
                    chain_counts["payload_width_disagrees_with_bone_count"] += int(
                        multiplier[np.array(wrong, dtype=np.int64)].sum()
                    )
                    continue

                ours_pos = base_pos[np.array(rows_here)].copy()
                ours_quat = base_quat[np.array(rows_here)].copy()
                subset_pos = ours_pos[:, target]
                subset_quat = ours_quat[:, target]
                keep = live[here][..., None]
                ours_pos[:, target] = np.where(keep, ours[0][here], subset_pos)
                ours_quat[:, target] = np.where(keep, ours[1][here], subset_quat)
                driven = np.zeros((len(here), bones), dtype=bool)
                driven[:, target] = live[here]
                selected = final_selected[there]
                chain_counts[
                    "bones_seeded_from_retail_because_no_clip_drove_them"
                ] += int((weights[:, None] * (selected & ~driven)).sum())

                root = final_root[there]
                rigid = (
                    decoder_pose.row_length_error(root) <= BANDS["row_length"][1]
                ) & (np.abs(np.linalg.det(root[..., :3])) > 0.5)
                if not rigid.all():
                    chain_counts["root_transform_not_rigid"] += int(
                        weights[~rigid].sum()
                    )
                    if not rigid.any():
                        continue
                    here, there = here[rigid], there[rigid]
                    rows_here = [row for row, take in zip(rows_here, rigid) if take]
                    weights = weights[rigid]
                    ours_pos, ours_quat = ours_pos[rigid], ours_quat[rigid]
                    selected, root = selected[rigid], root[rigid]

                retail_world = np.stack(
                    [
                        decoder_pose.payload_matrices(rows[index][G_DRAW], bones)
                        for index in rows_here
                    ]
                )
                retail_skin = np.stack(
                    [
                        decoder_pose.payload_matrices(
                            rows[index][G_DRAW], bones, offset=bones * MATRIX_BYTES
                        )
                        for index in rows_here
                    ]
                )
                world_live = selected & decoder_pose.is_frame(retail_world)
                chain_counts["bones_the_renderer_never_wrote"] += int(
                    (weights[:, None] * (selected & ~world_live)).sum()
                )
                world = decoder_pose.compose(
                    decoder_pose.matrices_from_local(ours_pos, ours_quat),
                    skeleton,
                    root,
                    split=True,
                    seed=retail_world,
                    selected=selected,
                    procedural=True,
                )
                world_error = np.where(
                    world_live,
                    decoder_pose.translation_error(world, retail_world),
                    0.0,
                )
                world_rotation = np.where(
                    world_live,
                    decoder_pose.matrix_rotation_angle_degrees(world, retail_world),
                    0.0,
                )
                palette_error = np.where(
                    world_live,
                    decoder_pose.translation_error(
                        decoder_pose.palette(world, skeleton.pose_to_bone), retail_skin
                    ),
                    0.0,
                )
                chain_arm.add(
                    checksum, "chained", "world_translation", bones, world_error, weights
                )
                chain_arm.add(
                    checksum, "chained", "rotation_degrees", bones, world_rotation, weights
                )
                chain_arm.add(
                    checksum, "chained", "palette_translation", bones, palette_error,
                    weights,
                )
                chain_bands = {
                    "world_translation": decoder_pose.band_of(
                        world_error, BANDS["world_translation"]
                    ),
                    "rotation_degrees": decoder_pose.band_of(
                        world_rotation, BANDS["rotation_degrees"]
                    ),
                    "palette_translation": decoder_pose.band_of(
                        palette_error, BANDS["world_translation"]
                    ),
                }
                for metric, value in chain_bands.items():
                    chain_arm.add_records("chained", metric, value.max(axis=1), weights)
                chain_arm.add_records(
                    "chained",
                    ANY_METRIC,
                    np.maximum.reduce(
                        [value.max(axis=1) for value in chain_bands.values()]
                    ),
                    weights,
                )
                chain_records += int(weights.sum())
                if checksum not in descendants:
                    descendants[checksum] = np.stack(
                        [skeleton.descendants(index) for index in range(bones)]
                    )
                seats = np.array(rows_here, dtype=np.int64)
                _fold_chain(
                    skeleton,
                    checksum,
                    band[here],
                    composed_band[seats],
                    composed_ready[seats],
                    np.maximum(
                        chain_bands["world_translation"],
                        chain_bands["rotation_degrees"],
                    )
                    > 0,
                    chain_bands["palette_translation"] > 0,
                    weights,
                    by_stage,
                    chain_first_bone,
                )

    for batched in _stream(connection, sql, batch):
        for row in batched:
            count = int(row[G_RECORDS])
            if sample is not None and records >= sample:
                decoded_counts["records_excluded_by_the_sample_bound"] += count
                continue
            checksum = int(row[G_CHECKSUM])
            bones = int(row[G_BONES])
            owner = int(row[G_OWNER])
            owner_bones = int(row[G_OWNER_BONES])
            if row[G_BASE] is None or row[G_SEQUENCE] is None:
                decoded_counts["payload_identity_missing"] += count
                continue
            if checksum not in models:
                decoded_counts["entity_image_absent"] += count
                continue
            if owner not in images:
                decoded_counts["owner_image_absent"] += count
                continue
            if models[checksum].bones != bones:
                decoded_counts["payload_width_disagrees_with_bone_count"] += count
                continue
            if len(row[G_BASE]) != bones * 7 * 4 + ((bones + 31) // 32) * 4:
                decoded_counts["payload_width_disagrees_with_bone_count"] += count
                continue
            if len(row[G_SEQUENCE]) < int(row[G_MASK_OFFSET]) + (
                (owner_bones + 31) // 32
            ) * 4:
                decoded_counts["payload_width_disagrees_with_bone_count"] += count
                continue
            if row[G_ANIM0] is None or not (
                int(row[G_DECODED0] or 0) or int(row[G_DECODED1] or 0)
            ):
                decoded_counts["cells_that_decoded_no_bone"] += count
                continue
            try:
                decoder_pose.animation_descriptor(images[owner], int(row[G_ANIM0]))
            except (IndexError, struct.error):
                decoded_counts["animation_index_outside_the_owner_declaration"] += count
                continue
            key = (owner, int(row[G_ANIM0]), row[G_ANIM1], checksum, bones)
            if pending and key != pending_key:
                flush()
            pending_key = key
            pending.append(row)
            if len(pending) >= CLIP_BATCH:
                flush()
    flush()

    investigate, definite = decoded_arm.band_totals(CLIP_LADDER_CANDIDATE)
    decoded_counts["stage1_records_in_the_investigate_band"] = investigate
    decoded_counts["stage1_records_in_the_definite_band"] = definite
    decoded_counts["stage1_include_remap_over_the_band"] = decoded_arm.over_band(
        "include_remap"
    )
    decoded_counts["stage1_frame_key_over_the_band"] = decoded_arm.over_band("frame_key")
    decoded_counts["stage1_cinematic_split_over_the_band"] = decoded_arm.over_band(
        "cinematic_split"
    )
    decoded_counts["stage1_bone_name_over_the_band"] = decoded_arm.over_band("bone_name")
    investigate, definite = composed_arm.band_totals(COMPOSED_LADDER_CANDIDATE)
    composed_counts["stage2_records_in_the_investigate_band"] = investigate
    composed_counts["stage2_records_in_the_definite_band"] = definite
    composed_counts["stage2_base_pose_over_the_band"] = composed_arm.over_band("base_pose")
    investigate, definite = chain_arm.band_totals("chained")
    chain_counts["chain_records_in_the_investigate_band"] = investigate
    chain_counts["chain_records_in_the_definite_band"] = definite

    elapsed = round(time.monotonic() - started, 3)
    decoded = {
        "available": bool(records),
        "reason": "" if records else "no base pose bound to a readable contribution",
        "retail_side": "the BASE payload's position and quaternion arrays",
        "our_side": (
            "elysium_pipeline.formats.mdl_skel.read_anim, run unmodified at the "
            "witnessed owner, animation, frame and blend cells"
        ),
        "implementation": (
            "the shipped decoder; the frame interpolation, the cell blend and "
            "the owner-to-entity bone correspondence around it are transcribed"
        ),
        "records": records,
        "distinct_payload_keys": groups,
        "clips_decoded": clips.decoded,
        "clips_unreadable": clips.faults,
        "models_carrying_an_include_remap": len(remaps),
        "cinematic_root_pairs_resolved": len(roots),
        "cinematic_root_pairs_unresolved": unresolved_pairs,
        "elapsed_seconds": elapsed,
        "candidates": decoded_arm.summary(models),
        "by_owner": by_owner,
        "counts": decoded_counts,
    }
    composed = {
        "available": bool(composed_records),
        "reason": "" if composed_records else "no base pose reached a final pose",
        "retail_side": "the FINL payload's position and quaternion arrays",
        "our_side": "retail's own BASE evaluation for the same pose build",
        "implementation": (
            "no offline model of the transition, layer and controller stage "
            "between the two, so the difference is that stage's footprint"
        ),
        "records": composed_records,
        "candidates": composed_arm.summary(models),
        "counts": composed_counts,
    }
    chain = {
        "available": bool(chain_records),
        "reason": "" if chain_records else "no bound base pose reached a draw",
        "population": (
            "one draw per pose build, the first the renderer consumed under it; "
            "the chain measures propagation rather than draw coverage"
        ),
        "our_side": (
            "our decoded locals composed under the split and ProcType == 1 "
            "rules and skinned by the stored inverse bind, with no retail value "
            "fed in between except the slots no fired clip drove"
        ),
        "records": chain_records,
        "stages_available": list(STAGE_ORDER),
        "by_stage": by_stage,
        "candidates": chain_arm.summary(models),
        "first_bone_raw": chain_first_bone,
        "counts": chain_counts,
    }
    return decoded, decoded_arm, composed, composed_arm, chain, chain_arm


def finish_by_owner(
    stage: dict[str, Any], names: dict[int, str]
) -> dict[str, Any]:
    """Turn the decoded stage's per-owner totals into named, ranked rows."""
    raw = stage.pop("by_owner", None)
    if raw is None:
        return stage
    rows = []
    for (checksum, owner, candidate), totals in raw.items():
        rows.append(
            {
                "model": names.get(checksum, ""),
                "checksum": f"0x{checksum:08x}",
                "owner": names.get(owner, ""),
                "owner_checksum": f"0x{owner:08x}",
                "candidate": candidate,
                "records": int(totals.sum()),
                "records_by_band": {
                    name: int(totals[index])
                    for index, name in enumerate(BAND_NAMES)
                },
            }
        )
    rows.sort(key=lambda row: (row["candidate"], -row["records"]))
    stage["by_owner"] = rows[:MAX_REPORTED_OWNERS]
    stage["by_owner_truncated"] = max(len(rows) - MAX_REPORTED_OWNERS, 0)
    return stage


def _fold_chain(
    skeleton: Skeleton,
    checksum: int,
    decoded_band: np.ndarray,
    composed_band: np.ndarray,
    composed_ready: np.ndarray,
    world_over: np.ndarray,
    palette_over: np.ndarray,
    multiplier: np.ndarray,
    by_stage: dict[str, int],
    first_bone: dict[tuple[int, int, str], int],
) -> None:
    """Attribute each chained record to the earliest stage that left the band.

    The four stages are walked in the order the runtime runs them, so a record
    whose decode already diverged is not counted again at the composition it
    fed. "Earliest bone" is the shallowest one over the band, because the
    composition is a forward pass and a deeper bone's error may be its
    ancestor's arriving rather than its own.
    """
    order = np.argsort(skeleton.depth, kind="stable")
    for index in range(len(multiplier)):
        count = int(multiplier[index])
        if decoded_band[index] > 0:
            by_stage[STAGE_DECODED_LOCALS] += count
            continue
        if composed_ready[index] and composed_band[index] > 0:
            by_stage[STAGE_COMPOSED_LOCALS] += count
            continue
        row = world_over[index]
        if row.any():
            bone = int(next(b for b in order if row[b]))
            by_stage[STAGE_BONE_TO_WORLD] += count
            key = (checksum, bone, STAGE_BONE_TO_WORLD)
            first_bone[key] = first_bone.get(key, 0) + count
            continue
        row = palette_over[index]
        if row.any():
            bone = int(next(b for b in order if row[b]))
            by_stage[STAGE_SKIN_PALETTE] += count
            key = (checksum, bone, STAGE_SKIN_PALETTE)
            first_bone[key] = first_bone.get(key, 0) + count
            continue
        by_stage["none"] += count


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
    if "propagation_raw" not in ladder:
        return ladder

    spread = []
    for (checksum, bone), stats in ladder.pop("propagation_raw", {}).items():
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
    return rank_per_candidate(rows, MAX_REPORTED_CLUSTERS)


def rank_per_candidate(
    rows: list[dict[str, Any]], limit: int
) -> list[dict[str, Any]]:
    """Rank within each stage, candidate and metric rather than across them all.

    A single list ranked across candidates is dominated by whichever rule is
    deliberately wrong: the ordinary hierarchy and the regenerated inverse bind
    exist to be worse, so they crowd the complete rule out of its own list and
    leave its residual with no bone named. The metric is part of the key for the
    same reason one rung down — a stage whose rotations are wrong on many more
    bones than its translations would otherwise leave the translation residual
    unnamed, which is exactly the population CAP4.3 could not attribute.
    """
    grouped: dict[tuple[str, str, str], list[dict[str, Any]]] = {}
    for row in rows:
        grouped.setdefault(
            (row["stage"], row["candidate"], row["metric"]), []
        ).append(row)
    out: list[dict[str, Any]] = []
    for key in sorted(grouped):
        group = grouped[key]
        group.sort(
            key=lambda row: (
                -row["records_in_the_definite_band"],
                -row["records_over_the_band"],
                -row["max"],
                row.get("cluster_key") or row["bone_name"],
            )
        )
        for row in group[:limit]:
            out.append({**row, "candidate_rank_truncated": max(len(group) - limit, 0)})
    return out


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
            "Retail's decoded locals, composed locals, bone-to-world and skin "
            "palette were compared against the shipped decoder and the "
            "transcribed composition and palette rules, each fed retail's own "
            "input for its stage, and the four were then chained off our own "
            "output. Every non-zero population is declared. A mismatch count is "
            "the product of this pass, not a defect of it."
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
        binding: dict[str, Any] = {"available": False, "reason": "not reached"}
        one: dict[str, Any] = {"available": False, "reason": "not reached"}
        two: dict[str, Any] = {"available": False, "reason": "not reached"}
        three: dict[str, Any] = {"available": False, "reason": "not reached"}
        four: dict[str, Any] = {"available": False, "reason": "not reached"}
        chain: dict[str, Any] = {"available": False, "reason": "not reached"}
        bind: dict[str, Any] = {"available": False, "reason": "not reached"}
        ladder: dict[str, Any] = {}
        ranked: list[dict[str, Any]] = []
        worst: list[dict[str, Any]] = []
        arms: list[StageArm] = []
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
            arms = [three_arm, four_arm]
            if flags["carries_base_poses"] and flags["carries_contributions"]:
                bound = bind_contributions(connection, flags)
                binding = {
                    "available": True,
                    "reason": "",
                    "renderable_offset": RENDERABLE_OFFSET,
                    **bound,
                    "counts": {
                        name: bound[name]
                        for name in (
                            "base_records_with_no_paired_contribution",
                            "paired_records_whose_cycle_disagrees",
                            "paired_records_whose_renderable_offset_disagrees",
                            "generations_with_more_than_one_base_evaluation",
                        )
                    },
                }
                images = {
                    checksum: walker.image
                    for checksum, walker in load_images(connection).items()
                    if not walker.capped
                }
                remaps, disputed = bone_remaps(connection, models)
                binding["remap_bones_the_groups_disagree_on"] = disputed
                binding["counts"]["remap_bones_the_groups_disagree_on"] = disputed
                one, one_arm, two, two_arm, chain, chain_arm = clip_stages(
                    connection, flags, models, images, remaps,
                    batch=batch, sample=sample,
                )
                del images
                one = finish_by_owner(one, names)
                chain = finish_ladder(chain, names, models)
                arms = [one_arm, two_arm, three_arm, four_arm, chain_arm]
            else:
                binding = {
                    "available": False,
                    "reason": (
                        "this database carries no base-pose evaluations or no "
                        "sequence and cell contributions, so no clip identity "
                        "can be bound to an evaluation"
                    ),
                    "counts": {},
                }
                one = two = chain = dict(binding)
            ranked = clusters(arms, names, models)
            worst = rank_per_candidate(
                [row for arm in arms for row in arm.bones_over_band(names, models)],
                MAX_REPORTED_BONES,
            )
        verdict = decide(
            flags, [bones_report, pairing, binding, one, two, four, three, chain, bind]
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
        "binding": binding,
        "stages": {
            STAGE_DECODED_LOCALS: one,
            STAGE_COMPOSED_LOCALS: two,
            STAGE_BONE_TO_WORLD: three,
            STAGE_SKIN_PALETTE: four,
        },
        "chain": chain,
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
        **{
            f"{stage}_{candidate}": [
                bands(report, stage, candidate) for report in reports
            ]
            for stage, candidate in STAGE_HEADLINE.items()
        },
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


STAGE_HEADLINE = {
    STAGE_DECODED_LOCALS: CLIP_LADDER_CANDIDATE,
    STAGE_COMPOSED_LOCALS: COMPOSED_LADDER_CANDIDATE,
    STAGE_BONE_TO_WORLD: LADDER_CANDIDATE,
    STAGE_SKIN_PALETTE: "pose_to_bone",
}


def summarize(report: dict[str, Any]) -> str:
    lines = [f"{report['session']}: {report['verdict']['statement']}"]
    for stage in STAGE_ORDER:
        section = report["stages"].get(stage) or {}
        if not section.get("available"):
            lines.append(f"  {stage}: unavailable — {section.get('reason', '')}")
            continue
        candidate = STAGE_HEADLINE[stage]
        metrics = (section.get("candidates") or {}).get(candidate) or {}
        payloads = section.get("distinct_payload_keys")
        lines.append(
            f"  {stage} ({candidate}): {section['records']:,} records"
            + (f" over {payloads:,} distinct payloads" if payloads else "")
            + (
                f" ({section['elapsed_seconds']}s)"
                if section.get("elapsed_seconds")
                else ""
            )
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
    for label, section in (
        ("draw-paired ladder", report.get("ladder") or {}),
        ("chained", report.get("chain") or {}),
    ):
        by_stage = section.get("by_stage") or {}
        if not by_stage:
            continue
        first = ", ".join(f"{name} {count:,}" for name, count in by_stage.items())
        lines.append(f"  first mismatching stage ({label}): {first}")
        for row in section.get("by_bone", [])[:1]:
            lines.append(
                f"    first mismatching bone: {row['bone_name'] or row['bone']} on "
                f"{row['model']} ({row['records']:,} records)"
            )
    for row in report.get("clusters", [])[:6]:
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
