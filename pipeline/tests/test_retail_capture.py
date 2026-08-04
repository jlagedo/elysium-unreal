from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import shutil
import sqlite3
import struct
import tempfile
import unittest

import numpy as np

from research.tooling.capture.inventory_player_animations import (
    parse_owner,
    player_slots,
)
from research.tooling.capture.capture_player_sequence import (
    ARM_AFTER_TARGET_SECONDS,
    MAX_PRE_SEQUENCE_FRAMES,
    MOVEMENT_SETTLE_WAITS,
    POST_SEQUENCE_WAITS,
    PRE_THIRD_PERSON_WAITS,
    REPEAT_GAP_WAITS,
    SEQUENCE_REPETITIONS,
    TARGET_READY_WAITS,
    assess_seed,
    build_config,
    read_load_command,
)
from research.tooling.capture.capture_theatre import (
    PRE_MAP_WAITS,
    RECIPES,
    THEATRE,
    TUTORIAL,
    arm_signal,
    beat_marks,
    build_config as build_theatre_config,
    transition_signal,
)
from research.tooling.capture import calibrate_theatre_capture
from research.tooling.capture.calibrate_theatre_capture import (
    calibrate,
    resolve_session,
)
from research.tooling.capture.verify_entity_pointer_join import address, verify
from research.tooling.capture.finalize_capture_database import (
    ACTOR_FILE_HEADER,
    ACTOR_OBSERVATION_HEADER_V2,
    ACTOR_OBSERVATION_HEADER,
    ANIMATION_FILE_HEADER,
    ANIMATION_RECORD_HEADER,
    BRACKET_RECORD_HEADER,
    CENSUS_FILE_HEADER,
    CONTRIBUTION_FILE_HEADER,
    CONTRIBUTION_RECORD_HEADER,
    MODEL_IMAGE_HEADER,
    MODEL_OBSERVATION_HEADER,
    POSE_FILE_HEADER,
    POSE_PARAMETER_BYTES,
    POSE_RECORD_HEADER,
    RENDER_INFO_BYTES,
    ROOT_TRANSFORM_BYTES,
    SCENE_FILE_HEADER,
    SCENE_REQUEST_HEADER,
    SEQUENCE_CHANGE_HEADER,
    finalize,
)
from research.tooling.capture.verify_pose_build_generation import (
    verify as verify_generation,
)
from research.tooling.capture.verify_model_skeleton_census import (
    compare as compare_census,
    verify as verify_census,
)
from research.tooling.capture.verify_actor_identity_lifetime import (
    compare as compare_actors,
    verify as verify_actors,
)
from research.tooling.capture.verify_source_attribution import (
    compare as compare_attribution,
    missing_grids,
    verify as verify_attribution,
)
from research.tooling.capture.resolve_consumed_spans import (
    ROLE_ANIMATION_DESCRIPTOR,
    ROLE_ANIM_RECORD,
    ROLE_HEADER_FIELD,
    ROLE_TRACK_HEADER,
    ROLE_TRACK_KEY,
    Walker,
    decode_intervals,
    encode_intervals,
    merge,
    resolve as resolve_spans,
    resolve_cell,
)
from research.tooling.capture.verify_consumed_spans import (
    compare as compare_spans,
    verify as verify_spans,
)
from research.tooling.capture.verify_scene_requests import (
    compare as compare_scene,
    verify as verify_scene,
)
from research.tooling.capture.verify_capture_integrity import (
    SHARDS,
    SIDECARS,
    _tools as integrity_tools,
    aggregate as aggregate_integrity,
    resolve as resolve_shard,
)
from research.tooling.capture.compact_capture_payloads import (
    EVENT_TABLE,
    compact,
    event_table_ddl,
)
from research.tooling.capture.index_capture_database import (
    INDEX_TABLES,
    index as index_capture,
)
from research.tooling.capture.verify_capture_index import (
    verify as verify_index,
)
from research.tooling.capture import verify_source_join
from research.tooling.capture.verify_source_join import (
    install_key,
    verify as verify_join,
)
from research.tooling.capture.decoder_coverage import (
    UnmeasuredRead,
    instrumented,
)
from research.tooling.capture import decoder_pose
from research.tooling.capture.verify_transform_difference import (
    ACCOUNTED as TRANSFORM_ACCOUNTED,
    CLIP_CANDIDATES,
    CLIP_LADDER_CANDIDATE,
    DEFECTS as TRANSFORM_DEFECTS,
    MAX_REPORTED_BONES,
    MAX_REPORTED_CLUSTERS,
    MAX_REPORTED_EXEMPLARS,
    MAX_REPORTED_MODELS,
    REPORT_NAME,
    _fold_route_witness,
    compare as compare_transform,
    include_routes,
    summarize as summarize_transform,
    verify as verify_transform,
)
from research.tooling.capture.verify_byte_coverage import (
    ACCOUNTED,
    DEFECTS,
    HEADER_BYTES,
    Layout,
    _intervals,
    _sequence_attribution,
    compare as compare_coverage,
    verify as verify_coverage,
)

REPO_ROOT = Path(__file__).resolve().parents[2]
NATIVE_ROOT = REPO_ROOT / "research" / "tooling" / "capture" / "native"
QPC_FREQUENCY = 10_000_000
MODEL_RENDER = 0x20D63830
FRAME_TICKS = QPC_FREQUENCY // 30


def pose_record(
    sequence: int,
    qpc: int,
    model_name: str,
    *,
    bone_count: int = 1,
    client_entity: int = 0x2000,
    checksum: int = 0x3000,
    studio_hdr: int = 0x1000,
    generation: int = 0,
    generation_entity: int = 0,
    carry_generation: int = 0,
    bone_to_world: list[tuple[float, ...]] | None = None,
    skin_palette: list[tuple[float, ...]] | None = None,
) -> bytes:
    # The draw payload is the whole bone-to-world array followed by the whole
    # skin palette, each one 3x4 row-major per bone, exactly as the two pointers
    # at object+0x5C and +0x60 are copied. A caller that names neither gets the
    # zero fill every test predating the transform arms already produced.
    if bone_to_world is None and skin_palette is None:
        payload = bytes(bone_count * 12 * 4 * 2)
    else:
        first = bone_to_world or [IDENTITY_3X4] * bone_count
        second = skin_palette or [IDENTITY_3X4] * bone_count
        payload = struct.pack(
            f"<{bone_count * 24}f",
            *[value for matrix in first for value in matrix],
            *[value for matrix in second for value in matrix],
        )
    return (
        POSE_RECORD_HEADER.pack(
            b"POSE",
            POSE_RECORD_HEADER.size + len(payload),
            sequence,
            qpc,
            7,
            studio_hdr,
            client_entity,
            checksum,
            bone_count,
            0x4000,
            1,
            2,
            3,
            4,
            5,
            model_name.encode("ascii") + b"\0",
            generation,
            generation_entity,
            carry_generation,
            RENDER_INFO_BYTES,
            # The render info as the engine wrote it: the studio header at
            # +0x00 and the entity at +0x18 are the two decoded fields.
            struct.pack(
                "<8I",
                studio_hdr,
                0xC000,
                0xFFFF | (0xFFFF << 16),
                0xD000,
                0xE000,
                0,
                client_entity,
                0xF000,
            ),
        )
        + payload
    )


def animation_record(
    magic: bytes,
    sequence: int,
    qpc: int,
    *,
    bone_count: int = 1,
    client_entity: int = 0x2000,
    checksum: int = 0x3000,
    studio_hdr: int = 0x1000,
    generation: int = 0,
    generation_depth: int = 0,
    root_transform: bool | None = None,
    local_pose: list[tuple[tuple[float, ...], tuple[float, ...]]] | None = None,
    selected: set[int] | None = None,
    root: tuple[float, ...] | None = None,
    sample_phase: float = 0.25,
) -> bytes:
    # The composed-pose stage is the only one that receives a root transform, so
    # a FINL record carries the trailer and a BASE record carries none unless a
    # test is deliberately building a malformed one.
    carries_root = magic == b"FINL" if root_transform is None else root_transform
    # The evaluation payload is the whole position array followed by the whole
    # quaternion array, because the hook copies two separate buffers rather than
    # one interleaved one.
    mask_words = (bone_count + 31) // 32
    if local_pose is None:
        payload = bytearray(bone_count * 7 * 4)
    else:
        payload = bytearray(
            struct.pack(
                f"<{bone_count * 7}f",
                *[axis for position, _ in local_pose for axis in position],
                *[axis for _, quaternion in local_pose for axis in quaternion],
            )
        )
    words = [0] * mask_words
    for index in range(bone_count) if selected is None else selected:
        words[index // 32] |= 1 << (index % 32)
    # A caller naming no selection gets the zero mask every test predating the
    # transform arms already produced; naming one selects exactly those bones.
    payload += struct.pack(
        f"<{mask_words}I", *(words if selected is not None else [0] * mask_words)
    )
    root_bytes = ROOT_TRANSFORM_BYTES if carries_root else 0
    if carries_root:
        # A real root/entity transform: identity rotation and a translation, so
        # the verifier's unit-determinant check has something to measure rather
        # than a zero-filled block that would fail it for the wrong reason.
        payload += struct.pack(
            "<12f",
            *(root if root is not None else (
                1.0, 0.0, 0.0, 64.0,
                0.0, 1.0, 0.0, -32.0,
                0.0, 0.0, 1.0, 16.0,
            )),
        )
    return (
        ANIMATION_RECORD_HEADER.pack(
            magic,
            ANIMATION_RECORD_HEADER.size + len(payload),
            sequence,
            qpc,
            7,
            client_entity,
            studio_hdr,
            checksum,
            bone_count,
            4,
            sample_phase,
            0.5,
            1,
            0x9000,
            0xA000,
            generation,
            generation_depth,
            0xB000 if carries_root else 0,
            root_bytes,
        )
        + payload
    )


# The server game DLL's load base in a synthetic session. A caller address is
# resolved by subtracting it and adding the specification's image base, so the
# two have to be distinguishable for that arithmetic to be exercised at all.
VAMPIRE_BASE = 0x30000000
# Reasons, mirroring SceneReason in the probe.
SCENE_STARTED = 1
SCENE_FINISHED = 2
SCENE_CANCELLED = 3
SCENE_EVENT = 4
SCENE_BOUND = 5
SCENE_ANIMSET = 6


def scene_request_record(
    magic: bytes,
    sequence: int,
    qpc: int,
    reason: int,
    *,
    scope: int = 1,
    parent_scope: int = 0,
    scene_entity: int = 0x5000,
    scene_ref_handle: int = (7 << 13) | 40,
    scene_vftable: int = 0x1044F05C,
    target_entity: int = 0,
    target_ref_handle: int = 0xFFFFFFFF,
    caller_address: int = VAMPIRE_BASE + 0x82F31,
    event_type: int = -1,
    event_start: float = 0.0,
    event_end: float = -1.0,
    scene_time: float = 0.0,
    playing_back: int = 1,
    faults: int = 0,
    scene_file: str = "scenes/theatre/courtroom.vcd",
    actor_name: str = "",
    text0: str = "",
    text1: str = "",
    text2: str = "",
) -> bytes:
    return SCENE_REQUEST_HEADER.pack(
        magic,
        SCENE_REQUEST_HEADER.size,
        sequence,
        qpc,
        7,
        reason,
        scope,
        parent_scope,
        scene_entity,
        scene_ref_handle,
        scene_vftable,
        target_entity,
        target_ref_handle,
        caller_address,
        event_type,
        event_start,
        event_end,
        scene_time,
        playing_back,
        faults,
        scene_file.encode("ascii") + b"\0",
        actor_name.encode("ascii") + b"\0",
        text0.encode("ascii") + b"\0",
        text1.encode("ascii") + b"\0",
        text2.encode("ascii") + b"\0",
    )


def sequence_change_record(
    sequence: int,
    qpc: int,
    *,
    generation: int = 1,
    client_entity: int = 0x1FFC,
    ref_handle: int = (7 << 13) | 40,
    renderable: int = 0x2000,
    studio_hdr: int = 0x1000,
    checksum: int = 0x3000,
    transitions_before: int = 0,
    transitions_after: int = 1,
    caller_address: int = 0x10091650,
    faults: int = 0,
) -> bytes:
    return SEQUENCE_CHANGE_HEADER.pack(
        b"SEQC",
        SEQUENCE_CHANGE_HEADER.size,
        sequence,
        qpc,
        7,
        generation,
        client_entity,
        ref_handle,
        renderable,
        studio_hdr,
        checksum,
        transitions_before,
        transitions_after,
        caller_address,
        faults,
    )


def _actor_layout(version: int) -> struct.Struct:
    return (
        ACTOR_OBSERVATION_HEADER
        if version >= 3
        else ACTOR_OBSERVATION_HEADER_V2
    )


def actor_lifetime_record(
    sequence: int,
    qpc: int,
    reason: int,
    *,
    entity: int = 0x1FFC,
    ref_handle: int = (7 << 13) | 40,
    version: int = 3,
) -> bytes:
    """A construction or destruction: an address and a time, no identity."""
    layout = _actor_layout(version)
    tail = (ref_handle,) if version >= 3 else ()
    return layout.pack(
        b"ACTR",
        layout.size,
        sequence,
        qpc,
        7,
        entity,
        0,
        reason,
        0,
        0,
        0,
        0,
        0,
        *tail,
        b"\0",
    )


def actor_record(
    sequence: int,
    qpc: int,
    model_name: str,
    *,
    entity: int = 0x1FFC,
    renderable: int = 0x2000,
    reason: int = 1,
    generation: int = 0,
    studio_hdr: int = 0x1000,
    checksum: int = 0x3000,
    previous_checksum: int = 0,
    bone_count: int = 1,
    ref_handle: int = (7 << 13) | 40,
    version: int = 3,
) -> bytes:
    layout = _actor_layout(version)
    tail = (ref_handle,) if version >= 3 else ()
    return layout.pack(
        b"ACTR",
        layout.size,
        sequence,
        qpc,
        7,
        entity,
        renderable,
        reason,
        generation,
        studio_hdr,
        checksum,
        previous_checksum,
        bone_count,
        *tail,
        model_name.encode("ascii") + b"\0",
    )


def bracket_record(
    magic: bytes,
    sequence: int,
    entry_qpc: int,
    exit_qpc: int,
    *,
    generation: int,
    parent: int = 0,
    depth: int = 0,
    client_entity: int = 0x2000,
    thread_id: int = 7,
) -> bytes:
    return BRACKET_RECORD_HEADER.pack(
        magic,
        BRACKET_RECORD_HEADER.size,
        sequence,
        exit_qpc,
        entry_qpc,
        thread_id,
        generation,
        parent,
        depth,
        client_entity,
        1,
        2,
        3,
        4,
        5,
        6,
        7,
        8,
        9,
    )


CONTRIBUTION_OWNER_HDR = 0x5000
CONTRIBUTION_SEQ_INDEX_OFF = 0x1000
CONTRIBUTION_ANIM_INDEX_OFF = 0x800
CONTRIBUTION_NUM_SEQ = 4

BONE_ARRAY_OFFSET = 512
BONE_STRIDE = 160
ANIM_DESC_STRIDE = 72
SEQ_DESC_STRIDE = 764
ANIM_RECORD_STRIDE = 32
# `MAXSTUDIOBLENDS`: the inline `anim[16][16]` grid keeps this row stride
# whatever the authored extents are.
BLEND_ROW_STRIDE = 16
POSE_PARAM_STRIDE = 20
# posX, posY, posZ, rotX, rotY, rotZ, rotW, in the order the per-bone record
# declares them.
CHANNEL_COUNT = 7


# --- CAP4.3: transform arithmetic, written out by hand -----------------------
#
# The comparison tools build these matrices with numpy. These exist so a
# fixture's expected value never comes from the code under test: an agreement
# test that computed both sides with `decoder_pose` would assert nothing.
#
# A transform is a flat 12-tuple, row-major, the same shape the probe copies out
# of the process and the same shape `StudioBone.poseToBone` is stored in.


def _matrix_3x4(
    position: tuple[float, float, float],
    quaternion: tuple[float, float, float, float],
) -> tuple[float, ...]:
    x, y, z, w = quaternion
    return (
        1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), position[0],
        2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w), position[1],
        2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y), position[2],
    )


def _multiply_3x4(a: tuple[float, ...], b: tuple[float, ...]) -> tuple[float, ...]:
    out = []
    for row in range(3):
        for column in range(4):
            total = sum(a[row * 4 + k] * b[k * 4 + column] for k in range(3))
            if column == 3:
                total += a[row * 4 + 3]
            out.append(total)
    return tuple(out)


def _invert_rigid_3x4(matrix: tuple[float, ...]) -> tuple[float, ...]:
    out = [0.0] * 12
    for row in range(3):
        for column in range(3):
            out[row * 4 + column] = matrix[column * 4 + row]
    for row in range(3):
        out[row * 4 + 3] = -sum(
            matrix[k * 4 + row] * matrix[k * 4 + 3] for k in range(3)
        )
    return tuple(out)


IDENTITY_3X4 = (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0)


def _bone_spec(entry) -> tuple[str, int, tuple[float, ...], tuple[float, ...], int]:
    """Normalize a fixture bone entry.

    A three-element entry is the identity-rotation, unflagged bone every caller
    predating CAP4.3 writes; a five-element one names its bind rotation and its
    flags, which is what the split-inheritance and palette arms discriminate on.
    """
    name, parent, position = entry[0], entry[1], tuple(entry[2])
    quaternion = tuple(entry[3]) if len(entry) > 3 else (0.0, 0.0, 0.0, 1.0)
    flags = int(entry[4]) if len(entry) > 4 else 0
    return name, int(parent), position, quaternion, flags


def _bind_world(bones) -> list[tuple[float, ...]]:
    """Bind-pose bone-to-model by the ordinary hierarchy.

    The split-inheritance flag is not consulted: a flagged bone's inverse bind is
    the conventional one, which `research/cases/animation-pose/specs/
    animation_pose.json` records as confirmed.
    """
    world: list[tuple[float, ...]] = []
    for entry in bones:
        _, parent, position, quaternion, _ = _bone_spec(entry)
        local = _matrix_3x4(position, quaternion)
        # A parent the forward pass has not reached yet composes against the
        # slot as it stands, which is the identity. That is what a deliberately
        # non-topological fixture is for; the decoder names it as a fault rather
        # than the fixture refusing to build it.
        if parent < 0 or parent >= len(world):
            world.append(local)
        else:
            world.append(_multiply_3x4(world[parent], local))
    return world


def _expected_world(
    bones,
    locals_: list[tuple[tuple[float, ...], tuple[float, ...]]],
    root: tuple[float, ...] | None = None,
    *,
    split: bool,
    seed: list[tuple[float, ...]] | None = None,
    selected: set[int] | None = None,
    procedural: dict[int, tuple] | None = None,
) -> list[tuple[float, ...]]:
    """Bone-to-world for one pose, by hand.

    ``split`` takes a flagged bone's rotation from the root rather than from its
    parent, and its translation from its parent as usual. A bone outside
    ``selected`` is taken from ``seed`` rather than composed, which is how a
    stage is fed retail's own input for the slots retail wrote and nothing else.
    """
    frame = root if root is not None else IDENTITY_3X4
    world: list[tuple[float, ...]] = []
    for index, entry in enumerate(bones):
        _, parent, _, _, flags = _bone_spec(entry)
        if selected is not None and index not in selected:
            assert seed is not None, "an unselected bone needs a seed"
            world.append(seed[index])
            continue
        position, quaternion = locals_[index]
        local = _matrix_3x4(position, quaternion)
        rule = (procedural or {}).get(index)
        if rule is not None and parent >= 0:
            # A driven bone ignores its animated local and takes the table's.
            # The two flags are disjoint on real models, so it composes off its
            # parent rather than through the split branch.
            world.append(_multiply_3x4(world[parent], _axis_interp_local(rule, world, bones)))
        elif parent < 0 or parent >= len(world):
            world.append(_multiply_3x4(frame, local))
        elif split and flags & 0x2:
            rotation = _multiply_3x4(frame, _matrix_3x4((0.0, 0.0, 0.0), quaternion))
            translation = _multiply_3x4(world[parent], _matrix_3x4(position, (0.0, 0.0, 0.0, 1.0)))
            world.append(
                tuple(
                    translation[row * 4 + 3] if column == 3 else rotation[row * 4 + column]
                    for row in range(3)
                    for column in range(4)
                )
            )
        else:
            world.append(_multiply_3x4(world[parent], local))
    return world


def _slerp_one(a, b, alpha):
    """Shortest-arc quaternion interpolation of two 4-tuples, by hand."""
    a = list(a)
    b = list(b)
    dot = sum(x * y for x, y in zip(a, b))
    if dot < 0.0:
        b = [-y for y in b]
        dot = -dot
    dot = max(-1.0, min(1.0, dot))
    if dot > 0.9995:
        out = [(1.0 - alpha) * x + alpha * y for x, y in zip(a, b)]
    else:
        theta = math.acos(dot)
        sine = math.sin(theta)
        out = [
            math.sin((1.0 - alpha) * theta) / sine * x
            + math.sin(alpha * theta) / sine * y
            for x, y in zip(a, b)
        ]
    norm = math.sqrt(sum(v * v for v in out)) or 1.0
    return tuple(v / norm for v in out)


def _nlerp_one(a, b, alpha):
    """Retail's shortest-arc normalized component mix, written by hand."""
    a = list(a)
    b = list(b)
    if sum(x * y for x, y in zip(a, b)) < 0.0:
        b = [-value for value in b]
    out = [(1.0 - alpha) * x + alpha * y for x, y in zip(a, b)]
    norm = math.sqrt(sum(value * value for value in out)) or 1.0
    return tuple(value / norm for value in out)


def _axis_interp_local(rule, world, bones):
    """The local a `ProcType == 1` table produces, by hand.

    `docs/vtmb/procedural_bones.md` owns the rule; this transcribes it a second
    time so an agreement with `decoder_pose` is between two formulations.
    """
    control, axis, positions, quaternions = rule
    driver = [world[control][row * 4 + axis] for row in range(3)]
    parent = _bone_spec(bones[control])[1]
    if parent >= 0:
        frame = world[parent]
        driver = [
            sum(frame[row * 4 + col] * driver[row] for row in range(3))
            for col in range(3)
        ]
    picked = (
        0 if driver[0] >= 0 else 1,
        2 if driver[1] >= 0 else 3,
        4 if driver[2] >= 0 else 5,
    )
    a1, a2, a3 = (abs(v) for v in driver)
    if a1 + a2 > 0.0:
        scale = 1.0 / (a1 + a2 + a3)
        quaternion = _slerp_one(
            _slerp_one(quaternions[picked[1]], quaternions[picked[0]], a1 / (a1 + a2)),
            quaternions[picked[2]],
            a3 * scale,
        )
        position = tuple(
            a1 * scale * positions[picked[0]][axis_index]
            + a2 * scale * positions[picked[1]][axis_index]
            + a3 * scale * positions[picked[2]][axis_index]
            for axis_index in range(3)
        )
    else:
        quaternion = quaternions[picked[2]]
        position = positions[picked[2]]
    return _matrix_3x4(position, quaternion)


class Track:
    """One channel's RLE runs, as ``(total, keys)`` per run.

    ``valid`` is the key count rather than a separate number, so a run is
    well-formed by construction and a test that wants ``total < valid`` — the
    corruption the position decoder resets the frame on — says so by passing a
    total below the key count.
    """

    def __init__(self, *runs: tuple[int, tuple[int, ...]]) -> None:
        self.runs = runs

    def encode(self) -> bytes:
        blob = bytearray()
        for total, keys in self.runs:
            blob += struct.pack("<BB", len(keys), total)
            for key in keys:
                blob += struct.pack("<h", key)
        return bytes(blob)


class Grid:
    """One StudioSeqDesc's blend space, as the fixture writes it.

    ``cells`` maps ``(axis0, axis1)`` to a local animation index, and every
    position inside ``groupsize`` is expected to carry one — a grid whose product
    disagrees with ``numblends`` is written verbatim so the decoder's fallback can
    be exercised rather than assumed.
    """

    def __init__(
        self,
        groupsize: tuple[int, int],
        cells: dict[tuple[int, int], int],
        *,
        paramindex: tuple[int, int] = (0, -1),
        paramstart: tuple[float, float] = (-180.0, 0.0),
        paramend: tuple[float, float] = (180.0, 0.0),
        numblends: int | None = None,
    ) -> None:
        self.groupsize = groupsize
        self.cells = cells
        self.paramindex = paramindex
        self.paramstart = paramstart
        self.paramend = paramend
        self.numblends = (
            groupsize[0] * groupsize[1] if numblends is None else numblends
        )


class Clip:
    """One StudioAnimDesc and the animation block it points at."""

    def __init__(
        self,
        name: str,
        numframes: int,
        *,
        fps: float = 30.0,
        tracks: dict[tuple[int, int], Track] | None = None,
        weights: dict[int, float] | None = None,
    ) -> None:
        self.name = name
        self.numframes = numframes
        self.fps = fps
        self.tracks = tracks or {}
        self.weights = weights or {}


# The default corpus exercises every branch a consumed-span walk has to take:
# a single-run track, a multi-run track the walk has to skip through, a held run
# whose total exceeds its key count, an unanimated channel that falls back to a
# bind field, and a zero-weight record that ends the decode before anything else
# is read.
DEFAULT_CLIPS = (
    Clip(
        "@walk",
        3,
        tracks={
            (0, 3): Track((3, (10, 20, 30))),
            (1, 0): Track((3, (-5, 0, 5))),
        },
    ),
    Clip(
        "@run",
        5,
        tracks={
            (0, 0): Track((2, (1, 2)), (3, (7, 8, 9))),
            (0, 6): Track((5, (100,))),
        },
    ),
    Clip("@idle", 1),
    Clip("@dead", 2, weights={0: 0.0, 1: 0.0}),
    Clip("@aim", 4, tracks={(1, 4): Track((4, (0, 11, 22, 33)))}),
    Clip("@turn", 2, tracks={(0, 1): Track((2, (3, 4)))}),
    # Long enough that the sampled frame lands past the first two runs, so the
    # walk has to skip through them and reads only their two header bytes each.
    Clip(
        "@skip",
        9,
        tracks={(0, 3): Track((2, (1, 2)), (2, (3, 4)), (2, (5, 6)))},
    ),
)
CONTRIBUTION_NUM_ANIM = len(DEFAULT_CLIPS)


def _animation_section(bones: int, clips: tuple[Clip, ...]) -> bytearray:
    """The animdesc array followed by one animation block per clip.

    Every offset a decoder follows is relative to something — ``animindex`` to
    its own animdesc, a channel offset to its own 32-byte record — so the
    section is position independent and the caller decides where it lands.
    """
    section = bytearray(ANIM_DESC_STRIDE * len(clips))
    for index, clip in enumerate(clips):
        desc = ANIM_DESC_STRIDE * index
        block = len(section)
        struct.pack_into("<f", section, desc + 4, clip.fps)
        struct.pack_into("<i", section, desc + 12, clip.numframes)
        struct.pack_into("<i", section, desc + 48, block - desc)
        section += bytearray(bones * ANIM_RECORD_STRIDE)
        for bone in range(bones):
            record = block + bone * ANIM_RECORD_STRIDE
            struct.pack_into("<f", section, record, clip.weights.get(bone, 1.0))
            for channel in range(CHANNEL_COUNT):
                track = clip.tracks.get((bone, channel))
                if track is None:
                    continue
                struct.pack_into(
                    "<i", section, record + 4 + channel * 4,
                    len(section) - record,
                )
                section += track.encode()
        struct.pack_into("<i", section, desc, len(section) - desc)
        section += clip.name.encode("ascii") + b"\0"
    return section


def model_image(
    checksum: int,
    model_name: str,
    *,
    bones: tuple[tuple, ...] = (
        ("Bip01", -1, (0.0, 0.0, 0.0)),
        ("Bip01 Spine", 0, (1.0, 2.0, 3.0)),
    ),
    clips: tuple[Clip, ...] = DEFAULT_CLIPS,
    sequences: int = CONTRIBUTION_NUM_SEQ,
    labels: tuple[str, ...] = (),
    activities: tuple[str, ...] = (),
    base_cells: tuple[int, ...] = (),
    pose_to_bone: str | tuple[tuple[float, ...], ...] = "inverse",
    procedural: dict[int, tuple] | None = None,
    grids: dict[int, "Grid"] | None = None,
    pose_parameters: tuple[tuple, ...] = (),
) -> bytes:
    """A minimal v2531 model image the pipeline bone decoder can read.

    A bone is ``(name, parent, position)`` or
    ``(name, parent, position, quaternion, flags)``; the short form is the
    identity-rotation, unflagged bone and keeps every caller predating the
    rotation arms working unchanged.

    Each bone's ``poseToBone`` is by default the exact inverse of its composed
    bind, so a correctly decoded skeleton returns the identity, the census
    verifier's soundness check has something real to measure, and a palette
    built over the bind pose is the identity. ``pose_to_bone="identity"`` writes
    an inverse bind that is deliberately not that, and an explicit sequence
    writes one 12-tuple per bone.

    ``procedural`` maps a bone index to ``(control, axis, pos6, quat6)`` and
    writes a real ``ProcType == 1`` axis-interpolation table for it, setting
    ``Flags & 0x1`` and a ``ProcIndex`` reaching the 176-byte record. Mapping a
    bone to ``None`` declares the rule and leaves ``ProcIndex`` at zero, which is
    the unreadable case.

    ``grids`` maps a sequence index to a :class:`Grid`, which writes that
    descriptor's real blend space — ``numblends``, the two axis extents, the pose
    parameter each axis binds to, its range, and one cell per position inside the
    extents at the fixed 16-short row stride. A sequence with no entry keeps the
    single-cell grid every caller predating this argument already produced.
    ``pose_parameters`` writes ``(name, flags, start, end, loop)`` records at
    ``NumLocalPoseParameters``/``LocalPoseParamIndex``.

    The animation and sequence arrays sit at the two fixed offsets a
    contribution's default descriptor pointers are built from, so a span walk
    reads real descriptors, real per-bone records and real RLE runs rather than
    a count and an array base that point at nothing.
    """
    names_offset = BONE_ARRAY_OFFSET + BONE_STRIDE * len(bones)
    blob = bytearray(names_offset)
    struct.pack_into("<4sI", blob, 0, b"IDST", 2531)
    struct.pack_into("<I", blob, 8, checksum)
    blob[12 : 12 + len(model_name)] = model_name.encode("ascii")
    struct.pack_into("<i", blob, 240, len(bones))
    struct.pack_into("<i", blob, 244, BONE_ARRAY_OFFSET)
    struct.pack_into("<i", blob, 404, 0)
    struct.pack_into("<i", blob, 408, 0)
    # The sequence and animation arrays a contribution's owner-local index is
    # range-checked against, at the two offsets the default descriptor pointers
    # are built from.
    struct.pack_into(
        "<ii", blob, 264, len(clips), CONTRIBUTION_ANIM_INDEX_OFF
    )
    struct.pack_into(
        "<ii", blob, 272, sequences, CONTRIBUTION_SEQ_INDEX_OFF
    )

    composed = _bind_world(bones)
    for index, entry in enumerate(bones):
        name, parent, position, quaternion, flags = _bone_spec(entry)
        base = BONE_ARRAY_OFFSET + BONE_STRIDE * index
        if pose_to_bone == "inverse":
            inverse = _invert_rigid_3x4(composed[index])
        elif pose_to_bone == "identity":
            inverse = IDENTITY_3X4
        else:
            inverse = tuple(pose_to_bone[index])
        struct.pack_into("<i", blob, base, len(blob) - base)
        blob += name.encode("ascii") + b"\0"
        struct.pack_into("<i", blob, base + 4, parent)
        struct.pack_into("<3f", blob, base + 32, *position)
        struct.pack_into("<4f", blob, base + 44, *quaternion)
        struct.pack_into("<3f", blob, base + 60, 1.0, 1.0, 1.0)
        struct.pack_into("<4f", blob, base + 72, 1.0, 1.0, 1.0, 1.0)
        rule = (procedural or {}).get(index, False)
        if rule is not False:
            flags |= 0x1
            struct.pack_into("<i", blob, base + 140, 1)
        struct.pack_into("<12f", blob, base + 88, *inverse)
        struct.pack_into("<i", blob, base + 136, flags)

    # The axis-interpolation tables, after the bone array and the name strings.
    # `ProcIndex` is relative to the bone record, so it is back-patched once the
    # table's absolute position is known.
    for index, rule in sorted((procedural or {}).items()):
        if rule is None:
            continue
        control, axis, positions, quaternions = rule
        base = BONE_ARRAY_OFFSET + BONE_STRIDE * index
        struct.pack_into("<i", blob, base + 144, len(blob) - base)
        blob += struct.pack("<ii", control, axis)
        blob += struct.pack("<18f", *[v for entry in positions for v in entry])
        blob += struct.pack("<24f", *[v for entry in quaternions for v in entry])

    animation = _animation_section(len(bones), clips)
    if len(blob) > CONTRIBUTION_ANIM_INDEX_OFF or (
        CONTRIBUTION_ANIM_INDEX_OFF + len(animation)
        > CONTRIBUTION_SEQ_INDEX_OFF
    ):
        raise ValueError("fixture sections overlap; widen the fixed offsets")
    blob += bytearray(CONTRIBUTION_ANIM_INDEX_OFF - len(blob))
    blob += animation
    blob += bytearray(CONTRIBUTION_SEQ_INDEX_OFF - len(blob))
    # Sequence descriptors carry a single-cell grid: numblends 1 and both group
    # sizes 1, with cell zero naming animation zero. `grids` replaces that with a
    # real blend space on the descriptors it names.
    for index in range(sequences):
        desc = CONTRIBUTION_SEQ_INDEX_OFF + SEQ_DESC_STRIDE * index
        blob += bytearray(SEQ_DESC_STRIDE)
        grid = (grids or {}).get(index)
        if grid is None:
            struct.pack_into("<i", blob, desc + 52, 1)
            struct.pack_into("<ii", blob, desc + 572, 1, 1)
            struct.pack_into("<ii", blob, desc + 580, -1, -1)
        else:
            struct.pack_into("<i", blob, desc + 52, grid.numblends)
            struct.pack_into("<ii", blob, desc + 572, *grid.groupsize)
            struct.pack_into("<ii", blob, desc + 580, *grid.paramindex)
            struct.pack_into("<2f", blob, desc + 588, *grid.paramstart)
            struct.pack_into("<2f", blob, desc + 596, *grid.paramend)
            for (axis0, axis1), animation in grid.cells.items():
                struct.pack_into(
                    "<h", blob, desc + 56 + (axis0 * BLEND_ROW_STRIDE + axis1) * 2,
                    animation,
                )
        if index < len(base_cells):
            struct.pack_into("<h", blob, desc + 56, base_cells[index])
    # The pose parameters a grid axis binds to by index, after the descriptors.
    if pose_parameters:
        struct.pack_into("<ii", blob, 384, len(pose_parameters), len(blob))
        base = len(blob)
        blob += bytearray(POSE_PARAM_STRIDE * len(pose_parameters))
        for index, (name, flags, start, end, loop) in enumerate(pose_parameters):
            record = base + POSE_PARAM_STRIDE * index
            struct.pack_into("<i", blob, record, len(blob) - record)
            blob += name.encode("ascii") + b"\0"
            struct.pack_into("<i", blob, record + 4, flags)
            struct.pack_into("<3f", blob, record + 8, start, end, loop)
    # Labels and activity names sit after the descriptor array, reached through
    # the descriptor-relative index fields at +0 and +4. A sequence with no
    # entry keeps the zero index the engine reads as an empty string, which is
    # what every caller predating these arguments already produced.
    for index in range(sequences):
        desc = CONTRIBUTION_SEQ_INDEX_OFF + SEQ_DESC_STRIDE * index
        for field, table in ((0, labels), (4, activities)):
            if index >= len(table) or not table[index]:
                continue
            struct.pack_into("<i", blob, desc + field, len(blob) - desc)
            blob += table[index].encode("ascii") + b"\0"
    struct.pack_into("<i", blob, 140, len(blob))
    return bytes(blob)


def observation_record(
    sequence: int,
    qpc: int,
    model_name: str,
    *,
    studio_hdr: int = 0x1000,
    checksum: int = 0x3000,
    previous_checksum: int = 0,
    bone_count: int = 2,
    model_length: int = 1024,
    reason: int = 1,
    image_captured: int = 1,
    generation: int = 0,
) -> bytes:
    return MODEL_OBSERVATION_HEADER.pack(
        b"MOBS",
        MODEL_OBSERVATION_HEADER.size,
        sequence,
        qpc,
        7,
        studio_hdr,
        checksum,
        previous_checksum,
        bone_count,
        BONE_ARRAY_OFFSET,
        model_length,
        0,
        0,
        2531,
        reason,
        image_captured,
        generation,
        model_name.encode("ascii") + b"\0",
    )


def image_record(
    sequence: int,
    qpc: int,
    image: bytes,
    *,
    studio_hdr: int = 0x1000,
    checksum: int = 0x3000,
    capped: int = 0,
) -> bytes:
    return (
        MODEL_IMAGE_HEADER.pack(
            b"MIMG",
            MODEL_IMAGE_HEADER.size + len(image),
            sequence,
            qpc,
            7,
            studio_hdr,
            checksum,
            len(image),
            len(image),
            capped,
            0,
        )
        + image
    )


CONTRIBUTION_CYCLE = 0.25


def animation_block_offset(
    index: int, *, bones: int = 2, clips: tuple[Clip, ...] = DEFAULT_CLIPS
) -> int:
    """Where clip ``index``'s per-bone record array lands in the image.

    Read back out of the section the fixture builds rather than recomputed, so
    the expectation a test checks and the bytes it checks against cannot drift.
    """
    section = _animation_section(bones, clips)
    desc = ANIM_DESC_STRIDE * index
    animindex = struct.unpack_from("<i", section, desc + 48)[0]
    return CONTRIBUTION_ANIM_INDEX_OFF + desc + animindex


def sampled_frame(numframes: int, cycle: float = CONTRIBUTION_CYCLE) -> int:
    return int((numframes - 1) * cycle)


def contribution_record(
    magic: bytes,
    sequence: int,
    qpc: int,
    *,
    scope: int = 1,
    generation: int = 1,
    entity: int = 0x1FFC,
    bones: int = 3,
    checksum: int = 0xABCD,
    studio_hdr: int = CONTRIBUTION_OWNER_HDR,
    sequence_index: int = -1,
    animation_index: int = -1,
    num_blends: int = 1,
    group_size: tuple[int, int] = (1, 1),
    param_index: tuple[int, int] = (0, -1),
    blend_cell: tuple[int, int] = (0, 0),
    blend_weight: tuple[float, float] = (0.0, 0.0),
    faults: int = 0,
    caller: int = 0x10091700,
    sequence_descriptor: int | None = None,
    animation_descriptor: int | None = None,
    mask_bones: tuple[int, ...] | None = None,
    channel_bones: tuple[int, ...] | None = None,
    channel_record_base: int | None = None,
    channel_bone_base: int | None = None,
    channel_frame: int | None = None,
    channel_fraction: float | None = None,
    clips: tuple[Clip, ...] = DEFAULT_CLIPS,
    image_bones: int = 2,
    cycle: float = CONTRIBUTION_CYCLE,
) -> bytes:
    """One fired contribution, laid out as the probe writes it.

    The descriptor pointers and the witnessed channel bases default to the
    arithmetic the verifiers check, so a test that wants a misplaced pointer or
    a frame disagreeing with the cycle has to say so.
    """
    is_sequence = magic == b"SEQP"
    pose_bytes = POSE_PARAMETER_BYTES if is_sequence else 0
    mask_words = (bones + 31) // 32
    mask_bytes = mask_words * 4 if is_sequence else 0
    channel_bytes = 0 if is_sequence else mask_words * 8
    size = (
        CONTRIBUTION_RECORD_HEADER.size + pose_bytes + mask_bytes + channel_bytes
    )
    if sequence_descriptor is None:
        sequence_descriptor = (
            studio_hdr + CONTRIBUTION_SEQ_INDEX_OFF + sequence_index * 764
            if is_sequence
            else 0
        )
    if animation_descriptor is None:
        animation_descriptor = (
            0
            if is_sequence
            else studio_hdr + CONTRIBUTION_ANIM_INDEX_OFF + animation_index * 72
        )
    if channel_bones is None:
        channel_bones = () if is_sequence else tuple(range(image_bones))
    first_bone = min(channel_bones) if channel_bones else 0
    if channel_record_base is None:
        channel_record_base = (
            0
            if is_sequence or not channel_bones
            else studio_hdr
            + animation_block_offset(
                animation_index, bones=image_bones, clips=clips
            )
            + first_bone * ANIM_RECORD_STRIDE
        )
    if channel_bone_base is None:
        channel_bone_base = (
            0
            if is_sequence or not channel_bones
            else studio_hdr + BONE_ARRAY_OFFSET + first_bone * BONE_STRIDE
        )
    numframes = (
        clips[animation_index].numframes
        if not is_sequence and 0 <= animation_index < len(clips)
        else 1
    )
    if channel_frame is None:
        channel_frame = 0 if is_sequence else sampled_frame(numframes, cycle)
    if channel_fraction is None:
        channel_fraction = (
            0.0 if is_sequence else (numframes - 1) * cycle - channel_frame
        )
    bitmap = bytearray(mask_words * 4)
    for bone in channel_bones:
        bitmap[bone // 8] |= 1 << (bone % 8)
    # The selected-bone mask is a real mask rather than filler, because a cell's
    # decoded bones are checked against it: leaving it arbitrary would make
    # every clean run report bones the sequence did not select.
    if mask_bones is None:
        mask_bones = tuple(range(bones))
    mask = bytearray(mask_bytes)
    for bone in mask_bones:
        if bone // 8 < len(mask):
            mask[bone // 8] |= 1 << (bone % 8)
    return (
        CONTRIBUTION_RECORD_HEADER.pack(
            magic,
            size,
            sequence,
            qpc,
            7,
            generation,
            1,
            entity,
            scope,
            caller,
            studio_hdr,
            checksum,
            bones,
            sequence_index,
            animation_index,
            sequence_descriptor,
            animation_descriptor,
            0x30000000,
            cycle,
            num_blends,
            group_size[0],
            group_size[1],
            param_index[0],
            param_index[1],
            blend_cell[0],
            blend_cell[1],
            blend_weight[0],
            blend_weight[1],
            faults,
            pose_bytes,
            mask_bytes,
            channel_record_base,
            channel_bone_base,
            len(channel_bones),
            len(channel_bones),
            channel_frame,
            channel_fraction,
            channel_bytes,
        )
        + b"\x11" * pose_bytes
        + bytes(mask)
        + (bytes(bitmap) * 2 if channel_bytes else b"")
    )


def write_session(
    session: Path,
    *,
    pose_records: bytes,
    animation_records: bytes,
    census_records: bytes | None = None,
    actor_records: bytes | None = None,
    contribution_records: bytes | None = None,
    scene_records: bytes | None = None,
    actor_version: int = 3,
    done: str = "complete=1\nqueued=2\nwritten=2\ndropped=0\n",
    boundary: dict[str, object] | None = None,
    console: str | None = None,
    map_name: str = "sp_theatre",
    run_zero_rule: str | None = None,
) -> None:
    (session / "launch.json").write_text(
        json.dumps(
            {
                "created_utc": "test",
                "tool_git": {"commit": "abc", "dirty": False},
                "map": map_name,
                **(
                    {"run_zero_rule": run_zero_rule}
                    if run_zero_rule is not None
                    else {}
                ),
                "capture_duration_seconds": 30,
                "retail_exit_code": 28,
                "launch_arguments": ["-game", "Unofficial_Patch"],
                "modules": [
                    {
                        "name": "Vampire.exe",
                        "path": "Vampire.exe",
                        "file_size": 3,
                        "sha256": "0" * 64,
                        "binary_profile": "owner-test",
                    }
                ],
            }
        ),
        encoding="utf-8",
    )
    (session / "recipe.cfg").write_text(f"map {map_name}\n", encoding="ascii")
    (session / "done.txt").write_text(done, encoding="ascii")
    (session / "supervision.txt").write_text(
        "reason=timeout\nstate=complete\ncapture_done=1\n"
        "binary_profile_matches=4\nbinary_profile_misses=0\n"
        "probe_diagnostic_writes=0\n",
        encoding="ascii",
    )
    if boundary is not None:
        (session / "boundary.json").write_text(
            json.dumps(boundary), encoding="utf-8"
        )
    if console is not None:
        (session / "console.log").write_text(console, encoding="utf-8")
    (session / "scene.elpose").write_bytes(
        POSE_FILE_HEADER.pack(
            b"ELPOSE4",
            4,
            POSE_FILE_HEADER.size,
            QPC_FREQUENCY,
            90,
            12,
            0x5000,
            0x6000,
            0x7000,
            0x8000,
            b"1" * 64 + b"\0",
            b"",
        )
        + pose_records
    )
    (session / "animation.elanim").write_bytes(
        ANIMATION_FILE_HEADER.pack(
            b"ELANIM4",
            4,
            ANIMATION_FILE_HEADER.size,
            QPC_FREQUENCY,
            90,
            12,
            0xB000,
            0xC000,
            0xD000,
            0,
            b"2" * 64 + b"\0",
            0xE000,
            0xF000,
            b"",
        )
        + animation_records
    )
    if census_records is not None:
        (session / "model.elmdl").write_bytes(
            CENSUS_FILE_HEADER.pack(
                b"ELMDL1",
                1,
                CENSUS_FILE_HEADER.size,
                QPC_FREQUENCY,
                90,
                12,
                0xB000,
                0x5000,
                8 * 1024 * 1024,
                1024,
                b"",
            )
            + census_records
        )
    if actor_records is not None:
        (session / "actor.elact").write_bytes(
            ACTOR_FILE_HEADER.pack(
                f"ELACT{actor_version}".encode("ascii"),
                actor_version,
                ACTOR_FILE_HEADER.size,
                QPC_FREQUENCY,
                90,
                12,
                0xB000,
                512,
                0xE000,
                b"",
            )
            + actor_records
        )
    if contribution_records is not None:
        (session / "contribution.elcon").write_bytes(
            CONTRIBUTION_FILE_HEADER.pack(
                b"ELCON2",
                2,
                CONTRIBUTION_FILE_HEADER.size,
                QPC_FREQUENCY,
                90,
                12,
                0x10000000,
                0x89740,
                0x89B20,
                0x89500,
                0x889F0,
                0x88BA0,
                b"2" * 64 + b"\0",
                b"",
            )
            + contribution_records
        )
    if scene_records is not None:
        (session / "scene.elscn").write_bytes(
            SCENE_FILE_HEADER.pack(
                b"ELSCN1",
                1,
                SCENE_FILE_HEADER.size,
                QPC_FREQUENCY,
                90,
                12,
                VAMPIRE_BASE,
                0x10000000,
                0x82EE0,
                0x83CD0,
                0x843D0,
                0x91110,
                b"3" * 64 + b"\0",
                b"",
            )
            + scene_records
        )


def write_generation_session(
    session: Path,
    actors: list[dict[str, object]],
    done_extra: str = "unbracketed=0\nbracket_overflow=0\n",
    census_records: bytes | None = None,
    actor_records: bytes | None = None,
) -> None:
    """Finalize a session whose records are bracketed as retail brackets them.

    One actor produces the nesting the hooks create: an engine draw bracket
    that encloses a pose build, the evaluations inside that pose build, and the
    draw record the pose build feeds. ``frame`` selects which engine frame
    encloses the actor, since the ordinary draw and the shadow draw are
    siblings that both build a pose. ``carry_generation`` may be overridden per
    actor so a test can make the structural and carried attributions disagree.

    ``entity`` is the IClientRenderable subobject, which is what the bracket
    hooks and the draw field both see. The skeletal evaluators are called on
    the C_BaseAnimating four bytes below it, so the fixture reproduces that
    offset rather than using one address everywhere.
    """
    poses: list[bytes] = []
    animations: list[bytes] = []
    sequence = 0
    generation = 0
    qpc = 1_000_000
    for actor in actors:
        entity = int(actor["entity"])
        name = str(actor.get("model", "models/test.mdl"))
        generation += 1
        draw_generation = generation
        generation += 1
        pose_generation = generation
        entry = qpc
        qpc += 10
        sequence += 1
        animations.append(
            bracket_record(
                b"PBLD",
                sequence,
                entry + 1,
                qpc,
                generation=pose_generation,
                parent=draw_generation,
                depth=1,
                client_entity=entity,
            )
        )
        for magic in (b"BASE", b"FINL"):
            sequence += 1
            animations.append(
                animation_record(
                    magic,
                    sequence,
                    entry + 2,
                    client_entity=entity - 4,
                    generation=pose_generation,
                    generation_depth=1,
                )
            )
        sequence += 1
        poses.append(
            pose_record(
                sequence,
                qpc + 1,
                name,
                client_entity=entity,
                generation=int(actor.get("pose_generation", draw_generation)),
                generation_entity=int(actor.get("generation_entity", entity)),
                carry_generation=int(
                    actor.get("carry_generation", pose_generation)
                ),
            )
        )
        sequence += 1
        animations.append(
            bracket_record(
                bytes(actor.get("frame", b"DBLD")),
                sequence,
                entry,
                qpc + 2,
                generation=draw_generation,
                parent=0,
                depth=0,
                # The engine draw frame's `this` is the CModelRender
                # singleton, the same value on every draw.
                client_entity=MODEL_RENDER,
            )
        )
        qpc += 100
    write_session(
        session,
        pose_records=b"".join(poses),
        animation_records=b"".join(animations),
        census_records=census_records,
        actor_records=actor_records,
        done=(
            "complete=1\nqueued=2\nwritten=2\ndropped=0\nqueue_peak=9\n"
            + done_extra
        ),
    )
    finalize(session)


def write_join_session(
    session: Path, cast: dict[str, tuple[int, list[int], list[int]]]
) -> None:
    """Finalize a session whose two streams carry the given per-model entities.

    ``cast`` maps a model name to its checksum, its draw-stream entity values,
    and its skeletal-stream entity values.
    """
    poses: list[bytes] = []
    animations: list[bytes] = []
    sequence = 0
    for name, (checksum, pose_entities, animation_entities) in cast.items():
        for entity in pose_entities:
            sequence += 1
            poses.append(
                pose_record(
                    sequence,
                    1_000_000,
                    name,
                    checksum=checksum,
                    client_entity=entity,
                )
            )
        for entity in animation_entities:
            sequence += 1
            animations.append(
                animation_record(
                    b"BASE",
                    sequence,
                    1_000_000,
                    checksum=checksum,
                    client_entity=entity,
                )
            )
    write_session(
        session,
        pose_records=b"".join(poses),
        animation_records=b"".join(animations),
    )
    finalize(session)


class RetailCaptureTests(unittest.TestCase):
    def test_theatre_recipe_delays_map_until_capture_hook_can_arm(self) -> None:
        lines = build_theatre_config(THEATRE).splitlines()
        map_line = lines.index("map sp_theatre")
        self.assertEqual(lines[map_line - 1], "echo ELYSIUM_CAP11_MAP_SP_THEATRE")
        self.assertEqual(lines[:map_line].count("wait"), PRE_MAP_WAITS)
        self.assertLess(lines.index("host_framerate 0.033333333"), map_line)
        self.assertNotIn("player_sequence", "\n".join(lines))

    def test_theatre_recipe_leaves_input_live_and_retail_running(self) -> None:
        # The operator walks onto the arrival trigger, and the hook only
        # flushes while retail is alive, so neither may be taken away.
        lines = build_theatre_config(THEATRE).splitlines()
        for forbidden in ("cl_mouselook 0", "cl_mouseenable 0", "quit"):
            self.assertNotIn(forbidden, lines)

    def test_tutorial_recipe_enters_its_scene_through_the_named_save(self) -> None:
        lines = build_theatre_config(TUTORIAL, save="Vampire-007").splitlines()
        entry = lines.index("load Vampire-007")
        self.assertEqual(lines[entry - 1], f"echo {TUTORIAL.map_marker}")
        self.assertEqual(lines[:entry].count("wait"), PRE_MAP_WAITS)
        self.assertLess(lines.index("host_framerate 0.033333333"), entry)
        # Every beat is a keypress, because the operator marks one mid-fight.
        for key, marker in TUTORIAL.beat_binds:
            self.assertIn(f'bind "{key}" "echo {marker}"', lines)
        self.assertNotIn("sp_theatre", "\n".join(lines))
        for forbidden in ("cl_mouselook 0", "cl_mouseenable 0", "quit"):
            self.assertNotIn(forbidden, lines)

    def test_a_recipe_requiring_a_save_refuses_to_build_without_one(self) -> None:
        # A cfg that silently loads nothing captures an idle main menu and
        # reports itself clean, which is the one failure worth making loud.
        with self.assertRaises(ValueError):
            build_theatre_config(TUTORIAL)
        with self.assertRaises(ValueError):
            build_theatre_config(THEATRE, save="Vampire-007")
        with self.assertRaises(ValueError):
            build_theatre_config(TUTORIAL, save="a b; quit")

    def test_every_recipe_names_itself_uniquely(self) -> None:
        # Two recipes sharing a cfg name would clobber each other in the
        # install, and the signature guard would then accept the wrong file.
        for field in ("config_name", "config_signature", "map_marker"):
            values = [getattr(r, field) for r in RECIPES.values()]
            self.assertEqual(len(values), len(set(values)), field)

    def test_watcher_arms_on_the_map_marker_when_no_arm_marker_is_declared(
        self,
    ) -> None:
        self.assertFalse(arm_signal(TUTORIAL, "nothing yet"))
        self.assertTrue(arm_signal(TUTORIAL, f"x\n{TUTORIAL.map_marker}\ny"))
        # The theatre still requires its authored trigger line, and requires it
        # after its own map echo, so a surviving earlier log cannot arm a run.
        stale = f"{THEATRE.arm_marker}\n{THEATRE.map_marker}\n"
        self.assertFalse(arm_signal(THEATRE, stale))
        self.assertTrue(
            arm_signal(THEATRE, f"{THEATRE.map_marker}\n{THEATRE.arm_marker}\n")
        )

    def test_beat_marks_are_reported_once_each_in_console_order(self) -> None:
        text = "\n".join(
            [
                "ELYSIUM_CAP27_BEAT_FIGHT",  # before the marker: not this run
                TUTORIAL.map_marker,
                "ELYSIUM_CAP27_BEAT_PLAYER_AIM",
                "ELYSIUM_CAP27_BEAT_FIGHT",
                "ELYSIUM_CAP27_BEAT_PLAYER_AIM",
            ]
        )
        self.assertEqual(
            beat_marks(TUTORIAL, text),
            ["ELYSIUM_CAP27_BEAT_PLAYER_AIM", "ELYSIUM_CAP27_BEAT_FIGHT"],
        )
        self.assertEqual(beat_marks(THEATRE, text), [])

    def _game_root_with_console(self, root: Path, text: str) -> Path:
        log = root / "logs" / "console.log"
        log.parent.mkdir(parents=True, exist_ok=True)
        log.write_text(text, encoding="utf-8")
        return root

    def test_transition_signal_uses_the_recipe_stop_tokens(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            saves = root / "Unofficial_Patch" / "Save"
            saves.mkdir(parents=True)
            self._game_root_with_console(root, "still playing\n")
            self.assertIsNone(transition_signal(root, TUTORIAL, set()))
            # The scene's own name is not its ending.
            self._game_root_with_console(root, "sp_tutorial_1 loaded\n")
            self.assertIsNone(transition_signal(root, TUTORIAL, set()))
            self._game_root_with_console(root, "sm_pawnshop_1\n")
            self.assertEqual(
                transition_signal(root, TUTORIAL, set()), "console-log"
            )
            # The independent signal: transition state named after this scene.
            self._game_root_with_console(root, "still playing\n")
            (saves / "sp_tutorial_1.HL0").write_bytes(b"")
            self.assertEqual(
                transition_signal(root, TUTORIAL, set()), "map-transition-state"
            )
            # The theatre's globs do not match, so recipes cannot cross-signal.
            self.assertIsNone(transition_signal(root, THEATRE, set()))

    def test_an_operator_stop_is_named_apart_from_the_authored_ending(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self._game_root_with_console(root, "ELYSIUM_CAP27_STOP\n")
            # Not an ending unless the run opted in.
            self.assertIsNone(transition_signal(root, TUTORIAL, set()))
            self.assertEqual(
                transition_signal(root, TUTORIAL, set(), True),
                "operator-console-token",
            )
            # The authored ending still wins when both are present, so a run
            # that reached it is never demoted.
            self._game_root_with_console(
                root, "ELYSIUM_CAP27_STOP\nsm_pawnshop_1\n"
            )
            self.assertEqual(
                transition_signal(root, TUTORIAL, set(), True), "console-log"
            )

    def test_census_comparison_across_scenes_reports_models_as_coverage(
        self,
    ) -> None:
        # Two scenes load different models by construction, so reporting that
        # as "complete without being reproducible" claims a comparison the runs
        # were never entitled to make.
        theatre = {
            "session": "cap11_a",
            "identity": {"map": "sp_theatre"},
            "coverage": {"identities_used": 141, "identities_unobserved": 0},
            "census": {
                "distinct_studio_headers": 141,
                "distinct_checksums": 141,
                "checksums": [1, 2, 3],
                "addresses": [10, 11],
            },
            "verdict": {"census_complete": True, "source_join": True},
        }
        tutorial = {
            **theatre,
            "session": "cap27_b",
            "identity": {"map": "sp_tutorial_1"},
            "census": {
                **theatre["census"],
                "checksums": [3, 4, 5],
                "addresses": [12, 13],
            },
        }
        across = compare_census([theatre, tutorial])
        self.assertFalse(across["same_map"])
        self.assertNotIn("without being reproducible", across["statement"])
        self.assertIn("coverage rather than agreement", across["statement"])
        self.assertIn("5 distinct checksums", across["statement"])
        # Two runs of one scene still have to agree.
        within = compare_census([theatre, {**tutorial, "identity": {"map": "sp_theatre"}}])
        self.assertIn("without being reproducible", within["statement"])

    def test_actor_comparison_across_scenes_still_claims_the_renderable_offset(
        self,
    ) -> None:
        # The shared-address count belongs to two runs of one scene. The +4
        # renderable offset does not: reproducing it across a different cast is
        # a stronger result, so that is the claim the statement has to make.
        theatre = {
            "session": "cap11_a",
            "identity": {"map": "sp_theatre"},
            "coverage": {"identities_used": 54, "identities_unobserved": 0},
            "actors": {
                "distinct_actors": 54,
                "renderable_delta": 4,
                "actors": [{"entity": 0x1000}, {"entity": 0x1004}],
            },
            "reuse": {"reused_entity_addresses": 5},
            "verdict": {"identity_complete": True, "lifetime_bounded": True},
        }
        tutorial = {
            **theatre,
            "session": "cap27_b",
            "identity": {"map": "sp_tutorial_1"},
            "actors": {
                **theatre["actors"],
                "actors": [{"entity": 0x9000}, {"entity": 0x9004}],
            },
        }
        across = compare_actors([theatre, tutorial])
        self.assertFalse(across["same_map"])
        self.assertTrue(across["renderable_delta_agrees"])
        self.assertEqual(across["shared_addresses"], 0)
        self.assertIn("+4", across["statement"])
        self.assertIn("across a different cast", across["statement"])
        # A genuine disagreement on the offset is still a disagreement.
        disagreeing = compare_actors(
            [theatre, {**tutorial, "actors": {**tutorial["actors"], "renderable_delta": 8}}]
        )
        self.assertFalse(disagreeing["renderable_delta_agrees"])
        self.assertIn("not a fixed offset", disagreeing["statement"])

    def test_scene_shape_comparison_does_not_pass_on_an_empty_intersection(
        self,
    ) -> None:
        # "The 0 scene files present in every run dispatched an identical
        # sequence of events" is not evidence. This fires whenever two runs
        # share no scene file, whether or not they captured different scenes.
        base = {
            "session": "cap11_a",
            "identity": {"map": "sp_theatre"},
            "coverage": {"records": 10, "scenes_started": 2, "distinct_scene_files": 1},
            "binding": {"bindings_resolved": 4},
            "requests": {"sequence_changes": 9},
            "shapes": {"theatre_a.vcd": "digest-a"},
            "verdict": {"judgeable": True, "requests_joined": True},
        }
        other = {
            **base,
            "session": "cap27_b",
            "identity": {"map": "sp_tutorial_1"},
            "shapes": {"tutorial_b.vcd": "digest-b"},
        }
        across = compare_scene([base, other])
        self.assertFalse(across["shape_claim_tested"])
        self.assertEqual(across["compared_scene_files"], 0)
        self.assertIn("untested", across["statement"])
        self.assertIn("sp_tutorial_1", across["statement"])
        # Same scene, disjoint walks: still untested, and still not a pass.
        same_scene = compare_scene(
            [base, {**other, "identity": {"map": "sp_theatre"}}]
        )
        self.assertFalse(same_scene["shape_claim_tested"])
        self.assertIn("untested", same_scene["statement"])
        # A genuine shared shape is still compared and still claimed.
        shared = compare_scene([base, {**other, "shapes": base["shapes"]}])
        self.assertTrue(shared["shape_claim_tested"])
        self.assertIn("identical sequence of events", shared["statement"])

    def test_session_resolution_finds_a_session_under_any_recipe_root(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            captures = root / "retail-capture"
            (captures / "theatre" / "cap11_a").mkdir(parents=True)
            (captures / "tutorial" / "cap27_b").mkdir(parents=True)
            (captures / "theatre" / "shared").mkdir()
            (captures / "tutorial" / "shared").mkdir()
            original = calibrate_theatre_capture.research_root
            calibrate_theatre_capture.research_root = lambda: root
            try:
                self.assertEqual(
                    resolve_session("cap27_b"),
                    (captures / "tutorial" / "cap27_b").resolve(),
                )
                self.assertEqual(
                    resolve_session("cap11_a"),
                    (captures / "theatre" / "cap11_a").resolve(),
                )
                absolute = (captures / "theatre" / "cap11_a").resolve()
                self.assertEqual(resolve_session(str(absolute)), absolute)
                # The wrong database reads as a clean run of the wrong scene,
                # so a name in two roots is named rather than picked.
                with self.assertRaises(ValueError) as ambiguous:
                    resolve_session("shared")
                self.assertIn("theatre", str(ambiguous.exception))
                self.assertIn("tutorial", str(ambiguous.exception))
                with self.assertRaises(FileNotFoundError):
                    resolve_session("cap11_missing")
            finally:
                calibrate_theatre_capture.research_root = original

    def test_save_state_is_not_a_transition_before_the_scene_is_up(self) -> None:
        # A load recipe restores transition state as part of loading its save.
        # Against a baseline taken before launch those files read as an ending
        # and stop the capture at load, so the watcher holds the baseline at
        # None until its own map marker appears.
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            saves = root / "Unofficial_Patch" / "Save"
            saves.mkdir(parents=True)
            self._game_root_with_console(root, "loading\n")
            (saves / "sp_tutorial_1.HL0").write_bytes(b"")
            self.assertIsNone(transition_signal(root, TUTORIAL, None))
            self.assertEqual(
                transition_signal(root, TUTORIAL, set()), "map-transition-state"
            )

    def test_capture_finalizer_retains_exact_records_in_one_sqlite_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            pose_payload = bytes(12 * 4 * 2)
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=animation_record(b"BASE", 2, 101),
            )

            report = finalize(session)
            database = Path(report["database"])
            self.assertTrue(database.is_file())
            self.assertFalse((session / "scene.elpose").exists())
            self.assertFalse((session / "animation.elanim").exists())
            connection = sqlite3.connect(database)
            try:
                self.assertEqual(
                    connection.execute("SELECT count(*) FROM records").fetchone()[0],
                    2,
                )
                self.assertEqual(
                    connection.execute(
                        "SELECT raw_payload FROM records WHERE kind = 'POSE'"
                    ).fetchone()[0],
                    pose_payload,
                )
                self.assertEqual(
                    connection.execute(
                        "SELECT value FROM capture_metadata WHERE key = 'map'"
                    ).fetchone()[0],
                    '"sp_theatre"',
                )
                self.assertEqual(
                    connection.execute("PRAGMA user_version").fetchone()[0], 0
                )
            finally:
                connection.close()

    def test_capture_finalizer_archives_the_run_zero_beside_the_records(self) -> None:
        # A calibration reading only the database has to find the trigger
        # stamp, so the boundary block travels inside the evidence file.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=animation_record(b"BASE", 2, 101),
                boundary={"arm_qpc": 4242, "arm_marker": "marker"},
                console="ELYSIUM_CAP11_BOOT\n",
                done=(
                    "complete=1\nqueued=2\nwritten=2\ndropped=0\n"
                    "queue_peak=9\nskipped=3\nfiltered=1\nbytes_written=200\n"
                ),
            )
            report = finalize(session)
            connection = sqlite3.connect(Path(report["database"]))
            try:
                archived = {
                    name
                    for (name,) in connection.execute("SELECT name FROM artifacts")
                }
                self.assertIn("boundary.json", archived)
                self.assertIn("console.log", archived)
                failures = dict(
                    connection.execute("SELECT category, count FROM failures")
                )
                self.assertEqual(failures["hook_skipped"], 3)
                self.assertEqual(failures["hook_filtered"], 1)
            finally:
                connection.close()

    def test_finalizer_reads_brackets_and_evaluations_from_one_stream(self) -> None:
        # Bracket records are header-only and interleave with the evaluations
        # they enclose, so the reader has to pick a layout per record.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_generation_session(session, [{"entity": 0x2000}])
            connection = sqlite3.connect(session / "capture.sqlite")
            try:
                kinds = dict(
                    connection.execute(
                        "SELECT kind, count(*) FROM records GROUP BY kind"
                    )
                )
                self.assertEqual(
                    kinds, {"POSE": 1, "BASE": 1, "FINL": 1, "PBLD": 1, "DBLD": 1}
                )
                self.assertEqual(
                    connection.execute(
                        "SELECT count(*) FROM records WHERE generation IS NULL"
                    ).fetchone()[0],
                    0,
                )
                # A bracket record carries no bone payload, so a reader that
                # sized it like an evaluation would have desynchronised here.
                self.assertEqual(
                    connection.execute(
                        "SELECT length(raw_payload) FROM records WHERE kind = 'DBLD'"
                    ).fetchone()[0],
                    0,
                )
                self.assertEqual(
                    connection.execute(
                        "SELECT generation_parent FROM records WHERE kind = 'PBLD'"
                    ).fetchone()[0],
                    connection.execute(
                        "SELECT generation FROM records WHERE kind = 'DBLD'"
                    ).fetchone()[0],
                )
            finally:
                connection.close()

            report = calibrate(session)
            self.assertTrue(
                all(
                    entry["closes"]
                    for entry in report["volume"]["byte_closure"].values()
                )
            )

    def test_generation_verifier_accepts_a_fully_bracketed_run(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_generation_session(
                session, [{"entity": 0x2000}, {"entity": 0x3000}]
            )
            report = verify_generation(session)
            self.assertEqual(report["coverage"]["unassigned"], 0)
            self.assertTrue(report["integrity"]["sound"])
            self.assertTrue(report["pose_groups"]["one_generation_per_entity"])
            self.assertEqual(report["cross_check"]["disagreeing"], 0)
            self.assertTrue(report["verdict"]["attributions_agree"])
            self.assertEqual(report["verdict"]["join_owner"], "CAP2.1-generation")

    def test_generation_verifier_attributes_a_shadow_framed_draw(self) -> None:
        # The client shadow manager enters CModelRender::DrawModelShadow, which
        # reaches the StudioRender draw without passing through
        # CModelRender::DrawModel. A verifier that only knew the ordinary frame
        # would leave every shadow draw unassigned.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_generation_session(
                session,
                [
                    {"entity": 0x2000},
                    {"entity": 0x2000, "frame": b"SHDW"},
                ],
            )
            connection = sqlite3.connect(session / "capture.sqlite")
            try:
                kinds = dict(
                    connection.execute(
                        "SELECT kind, count(*) FROM records GROUP BY kind"
                    )
                )
                self.assertEqual(kinds["SHDW"], 1)
                self.assertEqual(kinds["DBLD"], 1)
            finally:
                connection.close()
            report = verify_generation(session)
            self.assertEqual(report["coverage"]["unassigned"], 0)
            self.assertTrue(report["integrity"]["sound"])
            self.assertEqual(report["cross_check"]["draws"], 2)
            self.assertEqual(report["cross_check"]["disagreeing"], 0)
            self.assertTrue(report["verdict"]["attributions_agree"])
            # Byte closure is what proves the new kind is sized correctly.
            calibration = calibrate(session)
            self.assertTrue(
                all(
                    entry["closes"]
                    for entry in calibration["volume"]["byte_closure"].values()
                )
            )

    def test_generation_verifier_reports_an_unassigned_record(self) -> None:
        # A draw produced outside every bracket carries generation 0. That is
        # an explicit unknown, and it must fail acceptance rather than be
        # attributed to whichever bracket happens to be nearest.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_generation_session(
                session,
                [{"entity": 0x2000}, {"entity": 0x3000, "pose_generation": 0}],
                done_extra="unbracketed=1\nbracket_overflow=0\n",
            )
            report = verify_generation(session)
            self.assertEqual(report["coverage"]["unassigned"], 1)
            self.assertFalse(report["coverage"]["complete"])
            self.assertFalse(report["verdict"]["generations_complete"])
            self.assertIn("carry no", report["verdict"]["statement"])
            # The cost measurement stands on its own: an incomplete run still
            # has to say what the brackets cost.
            self.assertEqual(report["overhead"]["unbracketed"], 1)

    def test_generation_verifier_reports_disagreeing_attributions(self) -> None:
        # The failure carry-over alone cannot see: the draw names a pose build
        # belonging to a different draw, while the entity still satisfies
        # CAP1.3's +4 delta. Structural nesting is what catches it, and the
        # verifier has to call that a fault rather than pick a winner.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_generation_session(
                session,
                [
                    {"entity": 0x2000},
                    # Actor two carries actor one's pose build (generation 2).
                    {"entity": 0x3000, "carry_generation": 2},
                ],
            )
            report = verify_generation(session)
            self.assertEqual(report["coverage"]["unassigned"], 0)
            self.assertTrue(report["integrity"]["sound"])
            cross = report["cross_check"]
            self.assertEqual(cross["disagreeing"], 1)
            self.assertEqual(cross["draws"], 2)
            # The carried generation resolves and the delta holds; only the
            # nesting relation exposes the wrong pose build.
            fault = cross["faults"][0]
            self.assertTrue(fault["carry_resolved"])
            self.assertTrue(fault["delta_agrees"])
            self.assertFalse(fault["carry_nested_in_structural"])
            self.assertTrue(report["verdict"]["generations_complete"])
            self.assertFalse(report["verdict"]["attributions_agree"])

    def test_generation_verifier_answers_a_capture_without_generations(self) -> None:
        # A CAP1 database is a valid capture that predates the bracket, so it
        # is answered rather than rejected.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=animation_record(b"BASE", 2, 101),
            )
            finalize(session)
            database = session / "capture.sqlite"
            connection = sqlite3.connect(database)
            try:
                header = connection.execute(
                    "SELECT file_header FROM streams WHERE name = 'pose'"
                ).fetchone()[0]
                connection.execute(
                    "UPDATE streams SET file_header = ? WHERE name = 'pose'",
                    (b"ELPOSE2\0" + (2).to_bytes(4, "little") + header[12:],),
                )
                connection.commit()
            finally:
                connection.close()
            report = verify_generation(session)
            self.assertFalse(report["support"]["carries_generations"])
            self.assertIsNone(report["coverage"])
            self.assertIn("predates", report["verdict"]["statement"])

    def test_census_record_layouts_match_the_probe_that_writes_them(
        self,
    ) -> None:
        # The C++ struct is the definition and the reader re-declares it, so
        # nothing but this test stops the two drifting apart.
        source = (
            REPO_ROOT
            / "research"
            / "tooling"
            / "capture"
            / "live_pose_hook.cpp"
        ).read_text(encoding="utf-8")
        for struct_name, layout in (
            ("CensusFileHeader", CENSUS_FILE_HEADER),
            ("ModelObservationHeader", MODEL_OBSERVATION_HEADER),
            ("ModelImageHeader", MODEL_IMAGE_HEADER),
        ):
            self.assertIn(
                f"sizeof({struct_name}) == {layout.size}",
                source,
                struct_name,
            )

    def test_census_finalizer_keeps_the_dictionary_out_of_the_event_table(
        self,
    ) -> None:
        # A census row is a dictionary entry, not an event, so it lands in its
        # own tables and `records` keeps exactly the events it had before.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            image = model_image(0x3000, "models/test.mdl")
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=animation_record(b"BASE", 2, 101),
                census_records=(
                    observation_record(
                        3, 99, "models/test.mdl", model_length=len(image)
                    )
                    + image_record(4, 99, image)
                ),
            )
            result = finalize(session)
            self.assertEqual(result["records"]["census"], 2)
            connection = sqlite3.connect(session / "capture.sqlite")
            try:
                self.assertEqual(
                    connection.execute(
                        "SELECT count(*) FROM records"
                    ).fetchone()[0],
                    2,
                )
                name, bones, length, reason = connection.execute(
                    "SELECT model_name, bone_count, model_length, reason "
                    "FROM model_headers"
                ).fetchone()
                self.assertEqual(name, "models/test.mdl")
                self.assertEqual(bones, 2)
                self.assertEqual(length, len(image))
                self.assertEqual(reason, 1)
                stored, captured = connection.execute(
                    "SELECT image, captured_bytes FROM model_images"
                ).fetchone()
                # The retained bytes are the model image verbatim; nothing in
                # the finalizer decodes or rewrites them.
                self.assertEqual(stored, image)
                self.assertEqual(captured, len(image))
            finally:
                connection.close()

    def test_census_verifier_accepts_a_fully_observed_run(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            image = model_image(0x3000, "models/test.mdl")
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=animation_record(b"BASE", 2, 101),
                census_records=(
                    observation_record(
                        3, 99, "models/test.mdl", model_length=len(image)
                    )
                    + image_record(4, 99, image)
                ),
            )
            finalize(session)
            report = verify_census(session, join_source=False)
            self.assertTrue(report["support"]["carries_census"])
            self.assertEqual(report["coverage"]["identities_used"], 1)
            self.assertEqual(report["coverage"]["identities_unobserved"], 0)
            self.assertEqual(report["coverage"]["identities_observed_late"], 0)
            self.assertTrue(report["coverage"]["complete"])
            self.assertTrue(report["census"]["one_image_per_checksum"])
            self.assertTrue(report["census"]["every_checksum_has_an_image"])
            # The captured bytes decode as a skeleton whose composed bind and
            # stored inverse bind return the identity.
            self.assertEqual(report["skeletons"]["skeletons"], 1)
            self.assertEqual(report["skeletons"]["bones"], 2)
            self.assertTrue(report["skeletons"]["sound"])
            self.assertLess(
                report["skeletons"]["worst_inverse_bind_error"], 1e-6
            )
            self.assertTrue(report["verdict"]["census_complete"])
            self.assertEqual(report["verdict"]["source_join"], "unavailable")

    def test_census_verifier_reports_a_header_used_without_an_observation(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            image = model_image(0x3000, "models/test.mdl")
            write_session(
                session,
                pose_records=(
                    pose_record(1, 100, "models/test.mdl")
                    + pose_record(
                        2, 101, "models/other.mdl", studio_hdr=0x5000,
                        checksum=0x7000,
                    )
                ),
                animation_records=animation_record(b"BASE", 3, 102),
                census_records=(
                    observation_record(
                        4, 99, "models/test.mdl", model_length=len(image)
                    )
                    + image_record(5, 99, image)
                ),
            )
            finalize(session)
            report = verify_census(session, join_source=False)
            self.assertEqual(report["coverage"]["identities_used"], 2)
            self.assertEqual(report["coverage"]["identities_unobserved"], 1)
            self.assertEqual(
                report["coverage"]["unobserved"][0]["checksum"], "0x00007000"
            )
            self.assertFalse(report["verdict"]["census_complete"])
            self.assertIn(
                "without ever being observed", report["verdict"]["statement"]
            )

    def test_census_verifier_accounts_for_a_reused_address(self) -> None:
        # One address serving two models is the only free this capture can
        # observe, so the replacement observation has to account for it.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            first = model_image(0x3000, "models/first.mdl")
            second = model_image(0x7000, "models/second.mdl")
            write_session(
                session,
                pose_records=(
                    pose_record(1, 100, "models/first.mdl")
                    + pose_record(2, 200, "models/second.mdl", checksum=0x7000)
                ),
                animation_records=animation_record(b"BASE", 3, 101),
                census_records=(
                    observation_record(
                        4, 99, "models/first.mdl", model_length=len(first)
                    )
                    + image_record(5, 99, first)
                    + observation_record(
                        6,
                        199,
                        "models/second.mdl",
                        checksum=0x7000,
                        previous_checksum=0x3000,
                        reason=2,
                        model_length=len(second),
                    )
                    + image_record(7, 199, second, checksum=0x7000)
                ),
            )
            finalize(session)
            report = verify_census(session, join_source=False)
            self.assertEqual(report["reuse"]["reused_header_addresses"], 1)
            self.assertEqual(
                report["reuse"]["extra_identities_at_reused_addresses"], 1
            )
            self.assertEqual(report["reuse"]["replacement_observations"], 1)
            self.assertTrue(report["reuse"]["replacements_account_for_reuse"])
            self.assertEqual(report["census"]["distinct_studio_headers"], 1)
            self.assertEqual(report["census"]["distinct_checksums"], 2)
            self.assertTrue(report["verdict"]["census_complete"])

    def test_census_verifier_refuses_a_truncated_model_image(self) -> None:
        # A capped image cannot answer an offset past the cap, so it is a
        # completeness failure rather than a warning.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            image = model_image(0x3000, "models/test.mdl")
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=animation_record(b"BASE", 2, 101),
                census_records=(
                    observation_record(
                        3, 99, "models/test.mdl", model_length=len(image) * 4
                    )
                    + image_record(4, 99, image, capped=1)
                ),
            )
            finalize(session)
            report = verify_census(session, join_source=False)
            self.assertTrue(report["coverage"]["complete"])
            self.assertEqual(report["census"]["images_capped"], 1)
            self.assertFalse(report["verdict"]["census_complete"])
            self.assertIn("truncated", report["verdict"]["statement"])

    def test_census_verifier_measures_the_truncated_draw_name(self) -> None:
        # The draw record keeps 64 of the header's 128 name bytes, so a long
        # path reaches the database cut with no flag. The census carries the
        # whole field, which turns that into a number.
        long_name = (
            "models/character/npc/unique/society_of_leopold/"
            "average_vampire_hunter/average_vampire_hunter.mdl"
        )
        self.assertGreater(len(long_name), 64)
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            image = model_image(0x3000, long_name)
            write_session(
                session,
                pose_records=pose_record(1, 100, long_name[:64]),
                animation_records=animation_record(b"BASE", 2, 101),
                census_records=(
                    observation_record(
                        3, 99, long_name, model_length=len(image)
                    )
                    + image_record(4, 99, image)
                ),
            )
            finalize(session)
            report = verify_census(session, join_source=False)
            self.assertEqual(report["names"]["longest_name"], len(long_name))
            self.assertEqual(
                report["names"]["names_at_or_over_the_draw_field"], 1
            )
            self.assertEqual(
                report["names"]["draw_records_carrying_a_truncated_name"], 1
            )

    def test_census_verifier_answers_a_capture_without_a_census(self) -> None:
        # A CAP2.1 database is a valid capture that predates the census, so it
        # is answered rather than rejected.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=animation_record(b"BASE", 2, 101),
            )
            finalize(session)
            report = verify_census(session, join_source=False)
            self.assertFalse(report["support"]["carries_census"])
            self.assertIsNone(report["coverage"])
            self.assertIsNone(report["skeletons"])
            self.assertIn("predates", report["verdict"]["statement"])

    def test_actor_record_layouts_match_the_probe_that_writes_them(
        self,
    ) -> None:
        # The C++ struct is the definition and the reader re-declares it, so
        # nothing but this test stops the two drifting apart.
        source = (
            REPO_ROOT
            / "research"
            / "tooling"
            / "capture"
            / "live_pose_hook.cpp"
        ).read_text(encoding="utf-8")
        for struct_name, layout in (
            ("ActorFileHeader", ACTOR_FILE_HEADER),
            ("ActorObservationHeader", ACTOR_OBSERVATION_HEADER),
            ("AnimationRecordHeader", ANIMATION_RECORD_HEADER),
        ):
            self.assertIn(
                f"sizeof({struct_name}) == {layout.size}",
                source,
                struct_name,
            )

    def test_actor_finalizer_keeps_the_dictionary_out_of_the_event_table(
        self,
    ) -> None:
        # An actor observation is a dictionary entry, not an event, so it lands
        # in its own table and `records` keeps exactly the events it had.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=animation_record(b"BASE", 2, 101),
                actor_records=actor_record(3, 99, "models/test.mdl"),
            )
            result = finalize(session)
            self.assertEqual(result["records"]["actor"], 1)
            connection = sqlite3.connect(session / "capture.sqlite")
            try:
                self.assertEqual(
                    connection.execute(
                        "SELECT count(*) FROM records"
                    ).fetchone()[0],
                    2,
                )
                entity, renderable, name, reason = connection.execute(
                    "SELECT entity, renderable, model_name, reason "
                    "FROM actor_observations"
                ).fetchone()
                # Both addresses are stored as observed; neither is derived.
                self.assertEqual(entity, 0x1FFC)
                self.assertEqual(renderable, 0x2000)
                self.assertEqual(name, "models/test.mdl")
                self.assertEqual(reason, 1)
            finally:
                connection.close()

    def test_actor_verifier_accepts_a_fully_observed_run(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                # The evaluators run on the C_BaseAnimating; the draw stream
                # names the renderable subobject four bytes above it.
                animation_records=(
                    animation_record(b"BASE", 2, 101, client_entity=0x1FFC)
                    + animation_record(b"FINL", 3, 102, client_entity=0x1FFC)
                ),
                actor_records=actor_record(4, 99, "models/test.mdl"),
                done=(
                    "complete=1\nqueued=4\nwritten=4\ndropped=0\n"
                    "actor_records=1\nactor_resident=0\nactor_vanished=0\n"
                    "actor_overflow=0\nactor_faults=0\n"
                ),
            )
            finalize(session)
            report = verify_actors(session)
            self.assertTrue(report["support"]["carries_actors"])
            self.assertTrue(report["support"]["carries_root_transform"])
            self.assertEqual(report["coverage"]["identities_used"], 1)
            self.assertEqual(report["coverage"]["identities_unobserved"], 0)
            self.assertTrue(report["coverage"]["complete"])
            # The renderable is four bytes above the entity, measured from the
            # two addresses the probe recorded rather than derived from one.
            self.assertTrue(report["actors"]["renderable_delta_is_constant"])
            self.assertEqual(report["actors"]["renderable_delta"], 4)
            self.assertTrue(report["actors"]["matches_cap1_3_delta"])
            self.assertEqual(
                report["lifetime"]["records_outside_their_interval"], 0
            )
            self.assertTrue(report["lifetime"]["bounded"])
            self.assertTrue(report["verdict"]["identity_complete"])
            self.assertTrue(report["verdict"]["lifetime_bounded"])
            # This run armed the lifetime targets but the actor predates them,
            # so it is counted as already alive rather than faulted.
            self.assertTrue(report["verdict"]["lifetime_witnessed"])
            self.assertEqual(
                report["lifetime"]["entities_without_a_construction"], 1
            )

    def test_actor_verifier_reports_an_entity_used_without_an_observation(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=(
                    animation_record(b"BASE", 2, 101, client_entity=0x1FFC)
                    + animation_record(
                        b"BASE", 3, 102, client_entity=0x4FFC, checksum=0x7000
                    )
                ),
                actor_records=actor_record(4, 99, "models/test.mdl"),
            )
            finalize(session)
            report = verify_actors(session)
            self.assertEqual(report["coverage"]["identities_used"], 2)
            self.assertEqual(report["coverage"]["identities_unobserved"], 1)
            self.assertFalse(report["coverage"]["complete"])
            self.assertFalse(report["verdict"]["identity_complete"])
            self.assertEqual(
                report["coverage"]["unobserved"][0]["entity"], "0x00004ffc"
            )

    def test_actor_verifier_bounds_a_reused_address_by_its_identity_change(
        self,
    ) -> None:
        # CAP1.3's open residual: an address that serves two models is only
        # joinable inside an interval. An observed identity change is what
        # closes the first interval and opens the second.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=(
                    animation_record(b"BASE", 2, 101, client_entity=0x1FFC)
                    + animation_record(
                        b"BASE", 5, 300, client_entity=0x1FFC, checksum=0x7000
                    )
                ),
                actor_records=(
                    actor_record(3, 99, "models/test.mdl")
                    + actor_record(
                        4,
                        299,
                        "models/other.mdl",
                        reason=2,
                        checksum=0x7000,
                        previous_checksum=0x3000,
                    )
                ),
            )
            finalize(session)
            report = verify_actors(session)
            self.assertEqual(report["reuse"]["reused_entity_addresses"], 1)
            self.assertEqual(
                report["reuse"]["extra_identities_at_reused_addresses"], 1
            )
            self.assertEqual(
                report["reuse"]["identity_change_observations"], 1
            )
            self.assertTrue(report["reuse"]["changes_account_for_reuse"])
            # The first identity's interval closes where the second opens, and
            # neither record falls outside the interval that names it.
            self.assertEqual(report["lifetime"]["intervals"], 2)
            self.assertEqual(
                report["lifetime"]["intervals_closed_by_an_identity_change"], 1
            )
            self.assertEqual(
                report["lifetime"]["records_outside_their_interval"], 0
            )
            self.assertTrue(report["verdict"]["lifetime_bounded"])

    def test_actor_verifier_reports_a_record_outside_its_actors_interval(
        self,
    ) -> None:
        # The same address serving two models, but an evaluation of the first
        # model arrives after the change that gave the address the second. A
        # join on the raw address would silently attribute it to the wrong
        # actor; the interval is what makes it visible.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=(
                    animation_record(b"BASE", 2, 101, client_entity=0x1FFC)
                    + animation_record(
                        b"BASE", 5, 300, client_entity=0x1FFC, checksum=0x7000
                    )
                    + animation_record(b"BASE", 6, 400, client_entity=0x1FFC)
                ),
                actor_records=(
                    actor_record(3, 99, "models/test.mdl")
                    + actor_record(
                        4,
                        299,
                        "models/other.mdl",
                        reason=2,
                        checksum=0x7000,
                        previous_checksum=0x3000,
                    )
                ),
            )
            finalize(session)
            report = verify_actors(session)
            self.assertTrue(report["verdict"]["identity_complete"])
            self.assertEqual(
                report["lifetime"]["records_outside_their_interval"], 1
            )
            self.assertFalse(report["lifetime"]["bounded"])
            self.assertFalse(report["verdict"]["lifetime_bounded"])
            self.assertEqual(
                report["lifetime"]["outside"][0]["entity"], "0x00001ffc"
            )

    def test_actor_verifier_witnesses_a_construction_and_a_destruction(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=animation_record(
                    b"BASE", 3, 120, client_entity=0x1FFC
                ),
                actor_records=(
                    actor_lifetime_record(2, 90, 4)
                    + actor_record(4, 119, "models/test.mdl")
                    + actor_lifetime_record(5, 200, 5)
                ),
                done=(
                    "complete=1\nqueued=4\nwritten=4\ndropped=0\n"
                    "actor_constructions=1\nactor_destructions=1\n"
                ),
            )
            finalize(session)
            report = verify_actors(session)
            self.assertTrue(report["support"]["carries_lifetime"])
            lived = report["lifetime"]
            self.assertTrue(lived["witnessed"])
            self.assertEqual(lived["lifetimes"], 1)
            self.assertEqual(lived["lifetimes_closed_by_a_destruction"], 1)
            self.assertEqual(lived["entities_without_a_construction"], 0)
            self.assertEqual(lived["records_outside_any_lifetime"], 0)
            self.assertEqual(lived["constructions_of_entities_never_posed"], 0)
            self.assertTrue(report["verdict"]["lifetime_bounded"])
            self.assertTrue(report["verdict"]["lifetime_witnessed"])

    def test_actor_verifier_counts_a_construction_that_was_never_posed(
        self,
    ) -> None:
        # Destruction is only recorded for addresses the census published, so a
        # skeletal entity built and dropped without ever being posed would pair
        # as a lifetime still open at capture stop if it were counted as one.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=animation_record(
                    b"BASE", 3, 120, client_entity=0x1FFC
                ),
                actor_records=(
                    actor_lifetime_record(2, 90, 4)
                    + actor_record(4, 119, "models/test.mdl")
                    + actor_lifetime_record(5, 95, 4, entity=0x9FFC)
                ),
            )
            finalize(session)
            lived = verify_actors(session)["lifetime"]
            self.assertEqual(lived["lifetimes"], 1)
            self.assertEqual(lived["lifetimes_open_at_capture_stop"], 1)
            self.assertEqual(lived["constructions_of_entities_never_posed"], 1)

    def test_actor_verifier_reports_a_record_outside_any_witnessed_lifetime(
        self,
    ) -> None:
        # An address rebuilt into a new actor under the same model is exactly
        # what an identity interval cannot see: the checksum never changes, so
        # only the destruction separates the two actors.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=(
                    animation_record(b"BASE", 3, 120, client_entity=0x1FFC)
                    # Fires after the destruction and before any reconstruction.
                    + animation_record(b"BASE", 6, 250, client_entity=0x1FFC)
                ),
                actor_records=(
                    actor_lifetime_record(2, 90, 4)
                    + actor_record(4, 119, "models/test.mdl")
                    + actor_lifetime_record(5, 200, 5)
                ),
            )
            finalize(session)
            report = verify_actors(session)
            self.assertTrue(report["verdict"]["identity_complete"])
            # The identity interval alone sees nothing wrong; the lifetime does.
            self.assertEqual(
                report["lifetime"]["records_outside_their_interval"], 0
            )
            self.assertEqual(
                report["lifetime"]["records_outside_any_lifetime"], 1
            )
            self.assertFalse(report["lifetime"]["bounded"])
            self.assertFalse(report["verdict"]["lifetime_bounded"])

    def test_actor_verifier_counts_an_entity_alive_before_the_hooks_armed(
        self,
    ) -> None:
        # The probe arms before map load but does not create the world, so an
        # entity with no construction is counted, not faulted.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=animation_record(
                    b"BASE", 2, 101, client_entity=0x1FFC
                ),
                actor_records=actor_record(3, 99, "models/test.mdl"),
            )
            finalize(session)
            report = verify_actors(session)
            self.assertTrue(report["lifetime"]["witnessed"])
            self.assertEqual(report["lifetime"]["lifetimes"], 0)
            self.assertEqual(
                report["lifetime"]["entities_without_a_construction"], 1
            )
            self.assertEqual(
                report["lifetime"]["records_outside_any_lifetime"], 0
            )
            self.assertTrue(report["verdict"]["lifetime_bounded"])

    def test_actor_verifier_answers_a_capture_without_lifetime_targets(
        self,
    ) -> None:
        # An ELACT1 run carried the census without the construction and
        # destruction targets, so it is answered with a bounded interval and an
        # explicitly unwitnessed lifetime.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=animation_record(
                    b"BASE", 2, 101, client_entity=0x1FFC
                ),
                actor_records=actor_record(
                    3, 99, "models/test.mdl", version=1
                ),
                actor_version=1,
            )
            finalize(session)
            report = verify_actors(session)
            self.assertTrue(report["support"]["carries_actors"])
            self.assertFalse(report["support"]["carries_lifetime"])
            self.assertFalse(report["lifetime"]["witnessed"])
            self.assertTrue(report["verdict"]["lifetime_bounded"])
            self.assertFalse(report["verdict"]["lifetime_witnessed"])
            self.assertIn("unwitnessed", report["verdict"]["statement"])

    def test_actor_verifier_answers_a_capture_without_an_actor_stream(
        self,
    ) -> None:
        # A CAP2.2 database is a valid capture that predates the actor census,
        # so it is answered rather than rejected.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=animation_record(b"BASE", 2, 101),
            )
            finalize(session)
            report = verify_actors(session)
            self.assertFalse(report["support"]["carries_actors"])
            self.assertIsNone(report["coverage"])
            self.assertIsNone(report["lifetime"])
            self.assertIn("predates", report["verdict"]["statement"])

    def test_census_and_generation_verdicts_survive_an_actor_stream(
        self,
    ) -> None:
        # The actor stream draws from the same global sequence counter, so a
        # verifier that counted only the tables it reads would report the other
        # tables' rows as holes and turn CAP1.2's loss proof into a false alarm.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            image = model_image(0x3000, "models/test.mdl")
            write_generation_session(
                session,
                [{"entity": 0x2000, "checksum": 0x3000}],
                # One bracketed actor writes sequences 1..5, so the dictionary
                # rows continue the same counter rather than leaving a hole in
                # it that would read as loss.
                census_records=(
                    observation_record(
                        6, 1, "models/test.mdl", model_length=len(image)
                    )
                    + image_record(7, 1, image)
                ),
                actor_records=actor_record(8, 1, "models/test.mdl"),
            )
            generation = verify_generation(session)
            self.assertTrue(generation["verdict"]["generations_complete"])
            self.assertTrue(generation["verdict"]["attributions_agree"])
            census = verify_census(session, join_source=False)
            self.assertTrue(census["verdict"]["census_complete"])
            sequences = calibrate(session)["integrity"]["sequence_numbers"]
            self.assertTrue(sequences["dense"], sequences)
            self.assertEqual(sequences["gaps"], [])

    def test_calibration_closes_byte_accounting_over_the_actor_stream(
        self,
    ) -> None:
        # Record size stays a closed form of kind and bone count across four
        # streams, and the composed-pose trailer is part of it, so summing it
        # has to reproduce every stream's file size exactly.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=(
                    animation_record(b"BASE", 2, 101, client_entity=0x1FFC)
                    + animation_record(b"FINL", 3, 102, client_entity=0x1FFC)
                ),
                actor_records=(
                    actor_record(4, 99, "models/test.mdl")
                    + actor_record(5, 400, "models/test.mdl", reason=3)
                ),
            )
            finalize(session)
            report = calibrate(session)
            for stream in ("pose", "animation", "actor"):
                self.assertTrue(
                    report["volume"]["byte_closure"][stream]["closes"], stream
                )
            sequences = report["integrity"]["sequence_numbers"]
            self.assertTrue(sequences["dense"], sequences)
            self.assertEqual(sequences["records"], 5)

    def test_composed_pose_carries_the_root_transform_and_base_carries_none(
        self,
    ) -> None:
        # The root/entity transform is the third argument only the composed-pose
        # stage receives, so the trailer is on FINL and nowhere else.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=(
                    animation_record(b"BASE", 2, 101, client_entity=0x1FFC)
                    + animation_record(b"FINL", 3, 102, client_entity=0x1FFC)
                ),
                actor_records=actor_record(4, 99, "models/test.mdl"),
            )
            finalize(session)
            connection = sqlite3.connect(session / "capture.sqlite")
            try:
                rows = dict(
                    connection.execute(
                        "SELECT kind, root_transform_bytes FROM records "
                        "WHERE kind IN ('BASE', 'FINL')"
                    )
                )
            finally:
                connection.close()
            self.assertEqual(rows["FINL"], ROOT_TRANSFORM_BYTES)
            self.assertEqual(rows["BASE"], 0)
            report = verify_actors(session)
            self.assertTrue(report["placement"]["available"])
            self.assertEqual(report["placement"]["composed_poses"], 1)
            self.assertEqual(
                report["placement"]["composed_poses_carrying_a_transform"], 1
            )
            self.assertEqual(
                report["placement"]["other_kinds_carrying_a_transform"], 0
            )

    def test_draw_record_keeps_the_render_info_span_it_decodes_two_fields_from(
        self,
    ) -> None:
        # The engine writes eight dwords into the render info and the studio
        # draw reads them back. Only +0x00 and +0x18 are decoded; the rest are
        # kept as bytes because what they mean is still a hypothesis. The two
        # decoded columns must agree with the span they were read from, or the
        # span is not the struct those fields came from.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_session(
                session,
                pose_records=pose_record(
                    1,
                    100,
                    "models/test.mdl",
                    studio_hdr=0x1234,
                    client_entity=0x5678,
                ),
                animation_records=animation_record(b"BASE", 2, 101),
            )
            finalize(session)
            connection = sqlite3.connect(session / "capture.sqlite")
            try:
                studio_hdr, entity, info = connection.execute(
                    "SELECT studio_hdr, client_entity, render_info "
                    "FROM records WHERE kind = 'POSE'"
                ).fetchone()
            finally:
                connection.close()
            words = json.loads(info)
            self.assertEqual(len(words), RENDER_INFO_BYTES // 4)
            self.assertEqual(words[0], studio_hdr)
            self.assertEqual(words[6], entity)
            self.assertEqual(studio_hdr, 0x1234)
            self.assertEqual(entity, 0x5678)

    def test_calibration_closes_byte_accounting_over_the_census_stream(
        self,
    ) -> None:
        # The census stream is measured from its own tables, so its bytes have
        # to close against its file size the same way the event streams do.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            image = model_image(0x3000, "models/test.mdl")
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=animation_record(b"BASE", 2, 101),
                census_records=(
                    observation_record(
                        3, 99, "models/test.mdl", model_length=len(image)
                    )
                    + image_record(4, 99, image)
                ),
            )
            finalize(session)
            report = calibrate(session)
            closure = report["volume"]["byte_closure"]
            for stream in ("pose", "animation", "census"):
                self.assertTrue(closure[stream]["closes"], stream)
            # The hook stamps one counter across all three streams, so density
            # is only a proof that nothing was lost if the census tables are
            # counted too; reading `records` alone turns them into holes.
            sequences = report["integrity"]["sequence_numbers"]
            self.assertTrue(sequences["dense"], sequences)
            self.assertEqual(sequences["records"], 4)
            kinds = {
                entry["kind"]: entry
                for entry in report["volume"]["census_per_kind"]
            }
            self.assertEqual(kinds["MOBS"]["records"], 1)
            self.assertEqual(kinds["MIMG"]["records"], 1)
            self.assertEqual(
                kinds["MIMG"]["bytes"], MODEL_IMAGE_HEADER.size + len(image)
            )
            # The census adds no event record, so the studio-header census the
            # calibration already published is unchanged by it.
            self.assertEqual(report["census"]["distinct_studio_headers"], 1)

    def test_generation_verdict_survives_a_census_stream(self) -> None:
        # The census keeps its rows out of `records`, so CAP2.1's coverage
        # section still sees only records that carry a generation. This is the
        # guard on that separation.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            image = model_image(0x3000, "models/test.mdl")
            write_generation_session(
                session,
                [
                    {
                        "entity": 0x2004,
                        "model": "models/test.mdl",
                        "checksum": 0x3000,
                        "studio_hdr": 0x1000,
                    }
                ],
                census_records=(
                    observation_record(
                        900, 1, "models/test.mdl", model_length=len(image)
                    )
                    + image_record(901, 1, image)
                ),
            )
            report = verify_generation(session)
            self.assertEqual(report["coverage"]["unassigned"], 0)
            self.assertTrue(report["verdict"]["generations_complete"])
            self.assertTrue(report["verdict"]["attributions_agree"])

    def test_calibration_closes_byte_accounting_and_reports_lost_records(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            poses = b"".join(
                pose_record(
                    index + 1,
                    100 + index * FRAME_TICKS,
                    "models/test.mdl",
                    bone_count=index + 1,
                )
                for index in range(40)
            )
            # Sequence 43 never reaches the file, so the global counter is no
            # longer dense and the report has to say so.
            animations = b"".join(
                animation_record(
                    b"BASE", sequence, 100 + sequence * FRAME_TICKS, bone_count=33
                )
                for sequence in (41, 42, 44)
            )
            write_session(
                session, pose_records=poses, animation_records=animations
            )
            finalize(session)

            report = calibrate(session)
            self.assertTrue(
                all(
                    entry["closes"]
                    for entry in report["volume"]["byte_closure"].values()
                )
            )
            sequences = report["integrity"]["sequence_numbers"]
            self.assertFalse(sequences["dense"])
            self.assertEqual(sequences["gaps"], [{"after": 42, "missing": 1}])
            self.assertEqual(
                report["census"]["client_entities"]["intersection"], 1
            )
            self.assertIsNone(report["volume"]["queue_peak"])

    def test_calibration_derives_the_run_zero_from_the_casting_batch(self) -> None:
        # The map load draws its own character models one second before the
        # arrival trigger casts the sire and understudies; the zero is the
        # later batch, not the first character frame.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            batches = (
                (0, ("character/npc/unique/sheriff.mdl", "character/pc/extra.mdl")),
                (
                    QPC_FREQUENCY,
                    ("character/pc/sire.mdl", "character/pc/understudy.mdl"),
                ),
                (
                    3 * QPC_FREQUENCY,
                    (
                        "character/npc/seat_a.mdl",
                        "character/npc/seat_b.mdl",
                        "character/npc/seat_c.mdl",
                    ),
                ),
            )
            records = []
            sequence = 0
            for offset, models in batches:
                for index, model in enumerate(models):
                    sequence += 1
                    records.append(
                        pose_record(
                            sequence,
                            1_000_000 + offset,
                            model,
                            checksum=0x3000 + sequence,
                            client_entity=0x2000 + sequence,
                        )
                    )
            write_session(
                session,
                pose_records=b"".join(records),
                animation_records=animation_record(b"BASE", sequence + 1, 1_000_000),
            )
            finalize(session)

            zero = calibrate(session)["zero"]
            self.assertEqual(zero["trigger_batch"]["seconds"], 0.0)
            self.assertEqual(
                zero["trigger_batch"]["models"],
                ["character/pc/sire.mdl", "character/pc/understudy.mdl"],
            )
            self.assertEqual(zero["map_load_batch"]["seconds"], -1.0)
            self.assertEqual(zero["largest_batch_after_trigger"]["seconds"], 2.0)
            self.assertFalse(zero["console_stamp"]["accepted"])
            # A database predating recipes still derives exactly this zero.
            self.assertEqual(zero["rule_name"], "player_cast_batch")
            self.assertEqual(zero["rule_source"], "map-default")

    def test_calibration_reports_the_recipe_run_zero_rule_on_another_scene(
        self,
    ) -> None:
        # A scene entered from a save inside it never casts two player models,
        # which under the theatre's rule leaves the zero underived and every
        # relative timestamp below it None. The recipe names its own rule.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            records = []
            sequence = 0
            for offset, model in (
                (0, "character/npc/common/gangmember_male_2.mdl"),
                (0, "character/npc/common/gangmember_male_2_alt.mdl"),
                (2 * QPC_FREQUENCY, "character/npc/common/cop.mdl"),
            ):
                sequence += 1
                records.append(
                    pose_record(
                        sequence,
                        1_000_000 + offset,
                        model,
                        checksum=0x3000 + sequence,
                        client_entity=0x2000 + sequence,
                    )
                )
            write_session(
                session,
                pose_records=b"".join(records),
                animation_records=animation_record(b"BASE", sequence + 1, 1_000_000),
                map_name="sp_tutorial_1",
                run_zero_rule="map_load_batch",
                boundary={
                    "probe": False,
                    "arm_marker": None,
                    "arm_qpc": 1_000_000,
                    "qpc_frequency": QPC_FREQUENCY,
                    "signal": "console-log",
                    "beats": [
                        {
                            "marker": "ELYSIUM_CAP27_BEAT_FIGHT",
                            "qpc": 1_000_000 + 2 * QPC_FREQUENCY,
                        }
                    ],
                },
            )
            finalize(session)

            report = calibrate(session)
            zero = report["zero"]
            self.assertEqual(zero["rule_name"], "map_load_batch")
            self.assertEqual(zero["rule_source"], "capture-metadata")
            self.assertNotIn("reason", zero)
            self.assertEqual(zero["derived_qpc"], 1_000_000)
            self.assertEqual(zero["trigger_batch"]["seconds"], 0.0)
            # No authored arm marker means no tolerance test to accept or fail.
            self.assertIsNone(zero["console_stamp"]["accepted"])
            self.assertIsNotNone(zero["console_stamp"]["reason"])
            # A beat is placed on the derived zero like any other milestone.
            self.assertEqual(
                zero["beats"],
                [{"marker": "ELYSIUM_CAP27_BEAT_FIGHT", "seconds": 2.0}],
            )
            # A run with no blend grid says so rather than omitting the field.
            self.assertIsNone(zero["first_multi_blend_contribution"])
            self.assertTrue(
                any("run-zero rule" not in note for note in report["limitations"])
            )

    def test_calibration_comparison_across_scenes_states_non_applicability(
        self,
    ) -> None:
        theatre = {
            "session": "cap11_a",
            "identity": {"map": "sp_theatre"},
            "zero": {"rule_name": "player_cast_batch", "map_load_batch": None},
            "census": {
                "models": [
                    {
                        "model_name": "a.mdl",
                        "bone_count": 1,
                        "records": 100,
                        "first_seconds": 0.0,
                    }
                ]
            },
            "volume": {"per_kind": [{"records": 100, "bytes": 1000}]},
        }
        tutorial = {
            "session": "cap27_b",
            "identity": {"map": "sp_tutorial_1"},
            "zero": {"rule_name": "map_load_batch", "map_load_batch": None},
            "census": {"models": [{"model_name": "b.mdl", "bone_count": 2}]},
            "volume": {"per_kind": [{"records": 250, "bytes": 4000}]},
        }
        across = calibrate_theatre_capture.compare([theatre, tutorial])
        self.assertFalse(across["same_map"])
        self.assertEqual(across["maps"], ["sp_theatre", "sp_tutorial_1"])
        # The first run's whole cast would otherwise read as missing.
        self.assertEqual(across["models_only_in_some_runs"], [])
        self.assertNotIn("delta_percent", across["totals"][1])
        self.assertIn("sp_tutorial_1", across["statement"])
        self.assertIn("coverage rather than agreement", across["statement"])

        within = calibrate_theatre_capture.compare(
            [theatre, {**theatre, "session": "cap11_b"}]
        )
        self.assertTrue(within["same_map"])
        self.assertIn("delta_percent", within["totals"][1])

    def test_entity_join_proves_one_pointer_space_from_a_constant_delta(self) -> None:
        # Every skeletal entity sits a fixed offset above the draw stream's
        # render-info field, in every model, so the streams join as they are.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_join_session(
                session,
                {
                    "character/npc/a.mdl": (
                        0x3001,
                        [0x40000000, 0x40001000],
                        [0x40000008, 0x40001008],
                    ),
                    "character/npc/b.mdl": (0x3002, [0x40002000], [0x40002008]),
                },
            )

            report = verify(session)
            verdict = report["verdict"]
            self.assertTrue(verdict["one_pointer_space"])
            self.assertEqual(verdict["constant_delta"], "0x00000008")
            self.assertEqual(verdict["constant_delta_signed"], 8)
            self.assertEqual(verdict["join_owner"], "CAP1.3-delta")
            self.assertEqual(report["space"]["shared_values"], 0)
            self.assertEqual(report["cross_field"]["carrying_fields"], [])
            candidates = report["delta"]["candidates"]
            self.assertEqual(len(candidates), 1)
            self.assertTrue(candidates[0]["covers_all"])
            self.assertEqual(candidates[0]["bijective_models"], 2)
            self.assertEqual(candidates[0]["resolved_animation_entities"], 3)
            self.assertEqual(candidates[0]["pose_with_counterpart"], 3)
            # Timing never derives the delta; it only reports on the pairs the
            # delta already produced.
            self.assertFalse(report["timing"]["authoritative"])
            self.assertEqual(report["timing"]["pairs"], 3)

    def test_entity_join_survives_a_skeletal_actor_drawn_as_another_model(
        self,
    ) -> None:
        # One model animates two actors and draws one of them; the second is
        # drawn under a different model. That is model attribution, not a
        # pointer-space failure, so the delta still stands.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_join_session(
                session,
                {
                    "character/npc/doppleganger.mdl": (
                        0x3001,
                        [0x40000000],
                        [0x3FFFFFFC, 0x40001FFC],
                    ),
                    "character/npc/host.mdl": (0x3002, [0x40002000], [0x40001FFC]),
                },
            )

            report = verify(session)
            candidate = report["delta"]["candidates"][0]
            self.assertTrue(report["verdict"]["one_pointer_space"])
            self.assertEqual(candidate["delta_signed"], -4)
            self.assertTrue(candidate["covers_all"])
            self.assertEqual(candidate["bijective_models"], 1)
            divergent = candidate["models_not_bijective"]
            self.assertEqual(len(divergent), 1)
            self.assertEqual(
                divergent[0]["model_name"], "character/npc/doppleganger.mdl"
            )
            self.assertEqual(divergent[0]["unmatched_drawn_as_another_model"], 1)

    def test_entity_join_hands_a_disjoint_pointer_space_to_the_generation_join(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_join_session(
                session,
                {
                    "character/npc/a.mdl": (
                        0x3001,
                        [0x40000000, 0x40001000],
                        [0x50000000, 0x50004000],
                    ),
                    "character/npc/b.mdl": (0x3002, [0x40002000], [0x50008000]),
                },
            )

            report = verify(session)
            verdict = report["verdict"]
            self.assertFalse(verdict["one_pointer_space"])
            self.assertIsNone(verdict["constant_delta"])
            self.assertFalse(verdict["ambiguous"])
            self.assertEqual(verdict["join_owner"], "CAP2.1-generation")
            self.assertIn("CAP2.1", verdict["statement"])
            self.assertEqual(report["delta"]["candidates"], [])
            self.assertEqual(report["delta"]["shared_models"], 2)
            self.assertEqual(report["cross_field"]["carrying_fields"], [])
            # A negative verdict must not fall back to correlating by time.
            self.assertEqual(report["timing"]["pairs"], 0)
            self.assertEqual(
                verdict["derivation"],
                "model identity only; no timestamp correlation",
            )

    def test_entity_join_reports_several_surviving_deltas_as_ambiguity(self) -> None:
        # Two differences hold across every shared model, so neither is proven.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_join_session(
                session,
                {
                    "character/npc/a.mdl": (
                        0x3001,
                        [0x40000000],
                        [0x40000008, 0x40000010],
                    ),
                    "character/npc/b.mdl": (
                        0x3002,
                        [0x40002000],
                        [0x40002008, 0x40002010],
                    ),
                },
            )

            report = verify(session)
            verdict = report["verdict"]
            self.assertTrue(verdict["ambiguous"])
            self.assertFalse(verdict["one_pointer_space"])
            self.assertEqual(verdict["join_owner"], "CAP2.1-generation")
            self.assertEqual(
                [entry["delta"] for entry in report["delta"]["candidates"]],
                ["0x00000008", "0x00000010"],
            )
            row = report["per_model"]["shared_models"][0]
            self.assertIsNone(row["constant_delta"])
            self.assertEqual(row["distinct_differences"], 2)
            self.assertTrue(all(entry["candidate"] for entry in row["differences"]))

    def test_entity_join_ignores_the_bracket_records_sharing_its_table(
        self,
    ) -> None:
        # A bracket record names a frame or a renderable and carries neither a
        # checksum nor a bone count. Grouping it with the records that do name a
        # model put every bracket under one null checksum whose bone count no
        # row could supply, so the join failed on any bracketed capture.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_generation_session(
                session, [{"entity": 0x2004, "checksum": 0x3000}]
            )
            report = verify(session)
            self.assertEqual(
                [entry["checksum"] for entry in report["per_model"]["shared_models"]],
                ["0x00003000"],
            )
            # The bracket's CModelRender singleton is not an entity in either
            # pointer space, so it must not reach the populations the coverage
            # test is measured over.
            self.assertEqual(report["space"]["animation"]["distinct"], 1)
            self.assertEqual(report["space"]["pose"]["distinct"], 1)
            self.assertNotIn(
                address(MODEL_RENDER), report["space"]["animation_values"]
            )

    def test_entity_join_names_a_draw_field_that_already_carries_the_instance(
        self,
    ) -> None:
        # The five raw DrawModel arguments and the render-info pointer are
        # already captured, so a join hiding in one of them costs no new hook.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_join_session(
                session, {"character/npc/a.mdl": (0x3001, [0x40000000], [0x4000])}
            )

            report = verify(session)
            self.assertEqual(report["cross_field"]["carrying_fields"], ["model_info"])
            carried = next(
                entry
                for entry in report["cross_field"]["fields"]
                if entry["field"] == "model_info"
            )
            self.assertEqual(carried["entities_stable"], 1)
            # One shared model is one coincidence, not a pointer-space rule.
            self.assertFalse(report["verdict"]["one_pointer_space"])
            self.assertEqual(report["verdict"]["join_owner"], "CAP2.1-generation")

    def test_player_animation_inventory_preserves_duplicate_sequences_and_grid(self) -> None:
        data = bytearray(4096)
        data[:4] = b"IDST"
        struct.pack_into("<i", data, 4, 2531)
        struct.pack_into("<i", data, 240, 1)
        struct.pack_into("<ii", data, 264, 2, 2048)
        struct.pack_into("<ii", data, 272, 2, 512)
        for index in range(2):
            base = 512 + index * 764
            label = 3600 + index * 32
            activity = label + 12
            struct.pack_into("<i", data, base, label - base)
            struct.pack_into("<i", data, base + 4, activity - base)
            struct.pack_into("<i", data, base + 12, -1)
            struct.pack_into("<i", data, base + 16, 1)
            struct.pack_into("<i", data, base + 52, 2)
            struct.pack_into("<h", data, base + 56, index)
            struct.pack_into("<h", data, base + 56 + 16 * 2, 1 - index)
            struct.pack_into("<2i", data, base + 572, 2, 1)
            data[label : label + 10] = b"duplicate\0"
            data[activity : activity + 9] = b"ACT_TEST\0"
        for index in range(2):
            base = 2048 + index * 72
            name = 3800 + index * 24
            struct.pack_into("<i", data, base, name - base)
            struct.pack_into("<f", data, base + 4, 30.0)
            struct.pack_into("<i", data, base + 12, 81)
            struct.pack_into("<i", data, base + 48, 512)
            data[name : name + 7] = f"@anim{index}".encode() + b"\0"

        parsed = parse_owner("models/test.mdl", bytes(data))
        self.assertEqual(
            [sequence["label"] for sequence in parsed["sequences"]],
            ["duplicate", "duplicate"],
        )
        self.assertEqual(
            parsed["sequences"][0]["blend_grid"][1][0],
            1,
        )
        self.assertEqual(
            [cell["animation_index"] for cell in parsed["sequences"][0]["active_blend_cells"]],
            [0, 1],
        )
        self.assertEqual(len(parsed["sequences"][0]["descriptor_hex"]), 764 * 2)

    def test_player_animation_inventory_reads_only_indexed_body_slots(self) -> None:
        slots = player_slots(
            b'''ClanDataTables
            {
                ClanData
                {
                    General
                    {
                        Clan "Tremere"
                        M_Body "models/npc/not_player.mdl"
                        M_Body0 "models/character/pc/male/test.mdl"
                        M_Body1 "models/character/pc/male/test.mdl"
                        F_Body0 "models/character/pc/female/test.mdl"
                    }
                }
            }'''
        )
        self.assertEqual(len(slots), 3)
        self.assertEqual({slot["model"] for slot in slots}, {
            "models/character/pc/male/test.mdl",
            "models/character/pc/female/test.mdl",
        })

    def test_player_sequence_seed_recipe_orders_reset_arm_and_trigger(self) -> None:
        rendered = build_config("load Vampire-002", "howl")
        lines = rendered.splitlines()
        forward = lines.index("+forward")
        release = lines.index("-forward")
        triggers = [
            index
            for index, line in enumerate(lines)
            if line == "player_sequence howl"
        ]
        self.assertLess(lines.index("thirdperson"), forward)
        self.assertLess(lines.index("cl_mouselook 0"), lines.index("load Vampire-002"))
        self.assertLess(lines.index("cl_mouseenable 0"), lines.index("load Vampire-002"))
        self.assertEqual(lines[forward + 1], "wait")
        self.assertEqual(release, forward + 2)
        self.assertEqual(len(triggers), SEQUENCE_REPETITIONS)
        self.assertEqual(lines[triggers[0] - 1], "echo ELYSIUM_CAP11_ARM")
        self.assertEqual(lines[triggers[1] - 1], "wait")
        between_triggers = lines[triggers[0] + 1 : triggers[1]]
        self.assertEqual(between_triggers[0], "echo ELYSIUM_CAP11_TRIGGERED")
        self.assertEqual(
            between_triggers.count("wait"),
            81 + REPEAT_GAP_WAITS,
        )
        self.assertNotIn("host_timescale 0", lines)
        self.assertFalse(any(line.startswith("writeconfig") for line in lines))
        self.assertEqual(ARM_AFTER_TARGET_SECONDS, 0.0)
        self.assertEqual(
            MAX_PRE_SEQUENCE_FRAMES,
            TARGET_READY_WAITS + 1 + MOVEMENT_SETTLE_WAITS,
        )
        self.assertEqual(
            lines.count("wait"),
            PRE_THIRD_PERSON_WAITS
            + TARGET_READY_WAITS
            + 1
            + MOVEMENT_SETTLE_WAITS
            + 81
            + REPEAT_GAP_WAITS
            + POST_SEQUENCE_WAITS,
        )

    def test_player_sequence_seed_reads_one_active_load_command(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "elysium_load.cfg"
            path.write_text(
                "// owner save\n\nload Vampire-002\n",
                encoding="utf-8",
            )
            self.assertEqual(read_load_command(path), "load Vampire-002")

    def test_player_sequence_seed_acceptance_requires_long_match(self) -> None:
        capture = {
            "complete": True,
            "records": {"dropped": 0, "incomplete": 0},
        }
        clean_pairs = [[live, live + 1] for live in range(8, 78)]
        clean = assess_seed(
            capture,
            {
                "clip": {"frames": 81},
                "live_to_authored_alignment": {"aligned_pairs": clean_pairs},
            },
        )
        self.assertTrue(clean["passed"])
        interrupted = assess_seed(
            capture,
            {
                "clip": {"frames": 81},
                "live_to_authored_alignment": {
                    "aligned_pairs": [[live, live - 1] for live in range(6, 16)]
                },
            },
        )
        self.assertFalse(interrupted["passed"])
        self.assertFalse(interrupted["checks"]["contiguous_authored_run"])

    def test_native_capture_project_has_explicit_win32_presets(self) -> None:
        presets = json.loads(
            (NATIVE_ROOT / "CMakePresets.json").read_text(encoding="utf-8")
        )
        configure = {
            preset["name"]: preset for preset in presets["configurePresets"]
        }
        for name, configuration in (
            ("win32-debug", "Debug"),
            ("win32-release", "Release"),
        ):
            self.assertEqual(
                configure[name]["cacheVariables"]["CMAKE_BUILD_TYPE"],
                configuration,
            )
            self.assertEqual(configure[name]["inherits"], "win32-base")
        base = configure["win32-base"]
        self.assertEqual(base["generator"], "Ninja")
        self.assertEqual(
            base["cacheVariables"]["ELYSIUM_TARGET_ARCH"], "x86"
        )
        self.assertIn("ELYSIUM_NATIVE_BUILD_ROOT", base["binaryDir"])

        cmake = (NATIVE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("CMAKE_MSVC_RUNTIME_LIBRARY", cmake)
        self.assertIn("/W4", cmake)
        self.assertIn("/WX", cmake)
        self.assertIn("CMAKE_SIZEOF_VOID_P EQUAL 4", cmake)

    def test_synthetic_retail_contract_names_modules_and_hook_targets(self) -> None:
        cmake = (NATIVE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        target = (NATIVE_ROOT / "synthetic_retail.cpp").read_text(
            encoding="utf-8"
        )
        module = (NATIVE_ROOT / "synthetic_module.cpp").read_text(
            encoding="utf-8"
        )
        for name in ("client.dll", "engine.dll", "StudioRender.dll"):
            self.assertIn(name, cmake)
            self.assertIn(name, target)
        self.assertIn("ElysiumSyntheticHookTarget", target)
        self.assertIn("ElysiumSyntheticHookTarget", module)
        self.assertIn("synthetic_retail_lifecycle", cmake)
        self.assertIn("event=shutdown_complete modules=3", cmake)

    def test_native_launcher_creates_and_verifies_a_suspended_process(self) -> None:
        cmake = (NATIVE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        launcher = (NATIVE_ROOT / "retail_launcher.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("retail_launcher.cpp", cmake)
        self.assertIn("retail_supervision.cpp", cmake)
        self.assertIn("synthetic_suspended_launch", cmake)
        self.assertIn("CREATE_SUSPENDED", launcher)
        self.assertIn("CREATE_UNICODE_ENVIRONMENT", launcher)
        self.assertIn("CreateProcessW(", launcher)
        self.assertIn("SuspendThread(thread.Get())", launcher)
        self.assertIn("previousSuspendCount != 1", launcher)
        self.assertIn("BuildEnvironment(", launcher)
        self.assertIn("QuoteArgument(", launcher)
        self.assertIn("Unofficial_Patch", launcher)
        public_driver = (
            REPO_ROOT
            / "research"
            / "tooling"
            / "capture"
            / "retail_capture_launch.py"
        ).read_text(encoding="utf-8")
        self.assertIn('run_native("build"', public_driver)
        self.assertIn('"--inject-and-terminate"', public_driver)
        self.assertIn('"retail_probe_host.dll"', public_driver)
        self.assertIn('"--startup-profile"', public_driver)
        self.assertIn('target_arguments[:1] == ["--"]', public_driver)
        self.assertIn('"--target-argument"', public_driver)
        self.assertIn('"--capture-hook"', public_driver)
        self.assertIn('"--capture-stop"', public_driver)

    def test_bootstrap_injection_uses_a_versioned_loadlibrary_handshake(self) -> None:
        cmake = (NATIVE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        launcher = (NATIVE_ROOT / "retail_launcher.cpp").read_text(
            encoding="utf-8"
        )
        host = (NATIVE_ROOT / "retail_probe_host.cpp").read_text(
            encoding="utf-8"
        )
        observer = (NATIVE_ROOT / "module_observer.cpp").read_text(
            encoding="utf-8"
        )
        contract = (NATIVE_ROOT / "bootstrap_contract.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("add_library(retail_probe_host SHARED", cmake)
        self.assertIn("synthetic_bootstrap_termination", cmake)
        self.assertIn('"LoadLibraryW"', launcher)
        self.assertIn("CreateRemoteThread(", launcher)
        self.assertNotIn("manual map", launcher.lower())
        self.assertIn("BootstrapVersion = 5", contract)
        self.assertIn("ModuleObserverArmed", contract)
        self.assertIn("TransportArmed", contract)
        self.assertIn('"LdrRegisterDllNotification"', observer)
        self.assertIn("BootstrapState::Ready", host)
        self.assertIn("bootstrap->WaitReady", launcher)
        self.assertLess(
            launcher.index("bootstrap->WaitReady"),
            launcher.rindex("ResumeThread(thread.Get())"),
        )

    def test_hook_declarations_select_shared_instruction_aware_backends(self) -> None:
        capture_root = REPO_ROOT / "research" / "tooling" / "capture"
        registry = json.loads(
            (capture_root / "contracts" / "binary_profiles.json").read_text(
                encoding="utf-8"
            )
        )
        targets = {
            target["semantic_label"]: target
            for profile in registry["profiles"]
            for target in profile["targets"]
        }
        self.assertEqual(
            targets["client.resolve_virtual_model_pose"]["backend"],
            "inline_detour",
        )
        self.assertEqual(
            targets["studiorender.draw_model"]["backend"],
            "vtable_replacement",
        )
        self.assertEqual(
            targets["client.get_studio_hdr"]["backend"], "none"
        )
        for label in (
            "client.setup_bones",
            "engine.model_render_draw_model",
            "engine.model_render_draw_model_shadow",
            "client.base_entity_construct",
            "client.base_entity_destruct",
        ):
            self.assertEqual(targets[label]["backend"], "inline_detour")
            self.assertEqual(targets[label]["calling_convention"], "thiscall")
        # Every declaration is checked against the case specification, which
        # only happens for a target that names its source function.
        for target in targets.values():
            self.assertIn("source_function_label", target)
        # The lifetime pair is the shared base of the client entity hierarchy,
        # not a skeletal class: a vtable carrying the pose slots belongs to one
        # concrete class, so hooking its constructor would miss most actors.
        self.assertEqual(
            targets["client.base_entity_construct"]["source_function_label"],
            "c_baseentity_construct",
        )
        self.assertEqual(
            targets["client.base_entity_destruct"]["source_function_label"],
            "c_baseentity_destruct",
        )
        backend = (NATIVE_ROOT / "hook_backend.cpp").read_text(
            encoding="utf-8"
        )
        retained_probe = (capture_root / "live_pose_hook.cpp").read_text(
            encoding="utf-8"
        )
        cmake = (NATIVE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("DecodeInstruction(", backend)
        self.assertIn("RelativeDestination(", backend)
        self.assertIn("RelativeKind::ShortCondition", backend)
        self.assertIn("hook_backends_instruction_aware", cmake)
        self.assertIn("HookBackends::Install(", retained_probe)
        self.assertNotIn("VirtualProtect(", retained_probe)
        self.assertNotIn("trampoline", retained_probe.lower())

    def test_bracket_detours_pop_and_release_under_an_unwind(self) -> None:
        # The bracket hooks are the only ones that run work before the
        # original. An unwind out of retail that skipped the pop would leak a
        # generation, and one that skipped the refcount would leave
        # RemoveHooks spinning forever, so both live in __finally.
        retained_probe = (
            REPO_ROOT
            / "research"
            / "tooling"
            / "capture"
            / "live_pose_hook.cpp"
        ).read_text(encoding="utf-8")
        for original in (
            "gOriginalSetupBones(",
            "gOriginalModelRenderDrawModel(",
            "gOriginalModelRenderDrawModelShadow(",
        ):
            call = retained_probe.index(original)
            closing = retained_probe.index("__finally", call)
            self.assertLess(retained_probe.rindex("__try", 0, call), call)
            body = retained_probe[closing : retained_probe.index("}", closing)]
            self.assertIn("EndBracket(", body)
            self.assertIn("InterlockedDecrement(&gActiveHooks)", body)
        # The lifetime detours hold no bracket, but they still hold the unload
        # refcount across the original, so an unwind must release it.
        for original in (
            "gOriginalBaseEntityConstruct(",
            "gOriginalBaseEntityDestruct(",
        ):
            call = retained_probe.index(original)
            closing = retained_probe.index("__finally", call)
            self.assertLess(retained_probe.rindex("__try", 0, call), call)
            body = retained_probe[closing : retained_probe.index("}", closing)]
            self.assertIn("InterlockedDecrement(&gActiveHooks)", body)

    def test_supervision_owns_children_and_finalizes_partial_captures(self) -> None:
        cmake = (NATIVE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        launcher = (NATIVE_ROOT / "retail_launcher.cpp").read_text(
            encoding="utf-8"
        )
        supervision = (NATIVE_ROOT / "retail_supervision.cpp").read_text(
            encoding="utf-8"
        )
        for name in (
            "synthetic_supervision_normal_exit",
            "synthetic_supervision_timeout",
            "synthetic_supervision_crash",
            "synthetic_supervision_collector_exit",
        ):
            self.assertIn(name, cmake)
        self.assertIn("RunSupervision(request)", launcher)
        self.assertIn("JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE", supervision)
        self.assertIn("SetConsoleCtrlHandler(", supervision)
        self.assertIn("CTRL_C_EVENT", supervision)
        self.assertNotIn("GenerateConsoleCtrlEvent", supervision)
        self.assertIn("MoveFileExW(", supervision)
        self.assertIn("WaitForCaptureDone(", supervision)
        self.assertIn('"capture_done=%d\\n"', supervision)
        self.assertIn('"state=%s\\n"', supervision)
        for reason in ("process-crash", "collector-exit", "timeout", "ctrl-c"):
            self.assertIn(f'"{reason}"', supervision)

    def test_attach_fallback_is_explicit_and_non_owning(self) -> None:
        launcher = (NATIVE_ROOT / "retail_launcher.cpp").read_text(
            encoding="utf-8"
        )
        cmake = (NATIVE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        attach = (
            REPO_ROOT
            / "research"
            / "tooling"
            / "capture"
            / "retail_capture_attach.py"
        ).read_text(encoding="utf-8")
        self.assertIn("--attach-pid", launcher)
        self.assertIn("OpenProcess(", launcher)
        self.assertIn("mode=attached", launcher)
        self.assertIn("mode=launched", launcher)
        self.assertIn("synthetic_attach_fallback", cmake)
        self.assertIn("retail_capture_launch", attach)
        self.assertNotIn("TerminateProcess", attach)

    def test_lifecycle_soak_runs_one_hundred_complete_capture_cycles(self) -> None:
        cmake = (NATIVE_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        soak = (NATIVE_ROOT / "verify_lifecycle_soak.ps1").read_text(
            encoding="utf-8"
        )
        collector = (NATIVE_ROOT / "synthetic_collector.cpp").read_text(
            encoding="utf-8"
        )
        contract = (
            NATIVE_ROOT / "synthetic_capture_contract.h"
        ).read_text(encoding="utf-8")
        self.assertIn("synthetic_lifecycle_soak_100", cmake)
        self.assertIn("-Cycles 100", cmake)
        self.assertIn("Get-SelfHandleCount", soak)
        self.assertIn("Assert-ProcessExited", soak)
        self.assertIn("event=module_unloaded", soak)
        self.assertIn("SyntheticTraceContract", collector)
        self.assertIn("elysium.synthetic-capture-trace", contract)
        self.assertIn("MOVEFILE_WRITE_THROUGH", collector)

    def test_actor_verifier_credits_reuse_separated_by_a_destruction(
        self,
    ) -> None:
        """An address rebuilt as a different actor is two witnessed lifetimes.

        A theatre run reuses an address across a destruction rather than
        changing its model in place. The census records construct/first/destruct
        twice, which accounts for the second identity exactly; requiring an
        identity-change observation as well would fault a complete census.
        """
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            entity = 0x1FFC
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=(
                    animation_record(
                        b"BASE", 2, 101, client_entity=entity, checksum=0x3000
                    )
                    + animation_record(
                        b"BASE", 3, 200, client_entity=entity, checksum=0x4000
                    )
                ),
                actor_records=(
                    actor_lifetime_record(4, 90, 4, entity=entity)
                    + actor_record(
                        5, 91, "models/first.mdl", entity=entity,
                        checksum=0x3000,
                    )
                    + actor_lifetime_record(6, 150, 5, entity=entity)
                    + actor_lifetime_record(7, 160, 4, entity=entity)
                    + actor_record(
                        8, 161, "models/second.mdl", entity=entity,
                        checksum=0x4000,
                    )
                    + actor_lifetime_record(9, 250, 5, entity=entity)
                ),
                done=(
                    "complete=1\nqueued=9\nwritten=9\ndropped=0\n"
                    "actor_records=6\nactor_resident=0\nactor_vanished=0\n"
                    "actor_overflow=0\nactor_faults=0\n"
                ),
            )
            finalize(session)
            report = verify_actors(session)
            self.assertEqual(report["reuse"]["reused_entity_addresses"], 1)
            self.assertEqual(
                report["reuse"]["extra_identities_at_reused_addresses"], 1
            )
            # No identity change at all; the destructions are what separate them.
            self.assertEqual(report["reuse"]["identity_change_observations"], 0)
            self.assertEqual(report["reuse"]["destruction_observations"], 2)
            self.assertEqual(report["reuse"]["unwitnessed"], [])
            self.assertTrue(report["reuse"]["changes_account_for_reuse"])

    # --- CAP2.4 source attribution per contribution ------------------------

    def _attribution_session(
        self,
        session: Path,
        contributions: bytes,
        *,
        done_extra: str = "",
    ) -> None:
        """A run whose contributions hang off one bracketed pose build.

        The owner is a bank model: the census observes it and carries its image,
        but no evaluation names it, which is the population CAP2.2's census
        could not reach.
        """
        write_session(
            session,
            pose_records=pose_record(
                1, 100, "models/test.mdl", generation=1, client_entity=0x1FFC
            ),
            animation_records=(
                bracket_record(
                    b"PBLD", 2, 100, 101, generation=1, client_entity=0x1FFC
                )
                + animation_record(
                    b"BASE", 3, 102, client_entity=0x1FF8, generation=1
                )
            ),
            census_records=(
                # The entity's own model, which the evaluation names.
                observation_record(4, 88, "models/test.mdl")
                + image_record(
                    5, 89, model_image(0x3000, "models/test.mdl")
                )
                # And the bank the contributions resolve through, which no
                # evaluation names and only the contribution path observes.
                + observation_record(
                    6,
                    90,
                    "models/bank.mdl",
                    studio_hdr=CONTRIBUTION_OWNER_HDR,
                    checksum=0xABCD,
                )
                + image_record(
                    7,
                    91,
                    model_image(0xABCD, "models/bank.mdl"),
                    studio_hdr=CONTRIBUTION_OWNER_HDR,
                    checksum=0xABCD,
                )
            ),
            contribution_records=contributions,
            done=(
                "complete=1\nqueued=8\nwritten=8\ndropped=0\nqueue_peak=12\n"
                "unbracketed=0\nbracket_overflow=0\n"
                "contribution_sequences=1\ncontribution_animations=1\n"
                "contribution_faults=0\ncontribution_overflow=0\n"
                "contribution_unscoped=0\n" + done_extra
            ),
        )
        finalize(session)

    def _clean_contributions(self) -> bytes:
        # A 9x1 resolved to cell 4 fires that cell and the one after it, so a
        # faithful scope carries two decoded cells rather than one.
        return (
            contribution_record(
                b"SEQP", 8, 103, sequence_index=2, num_blends=9,
                group_size=(9, 1), blend_cell=(4, 0), blend_weight=(0.5, 0.0),
            )
            + contribution_record(b"ANIM", 9, 104, animation_index=5)
            + contribution_record(b"ANIM", 10, 105, animation_index=6)
        )

    def test_attribution_verifier_accepts_a_fully_attributed_run(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._attribution_session(session, self._clean_contributions())
            report = verify_attribution(session)
            self.assertTrue(report["support"]["carries_contributions"])
            self.assertEqual(report["coverage"]["sequences"], 1)
            self.assertEqual(report["coverage"]["cells"], 2)
            self.assertEqual(report["coverage"]["unscoped"], 0)
            self.assertEqual(report["coverage"]["unbracketed"], 0)
            self.assertEqual(report["owners"]["unobserved_count"], 0)
            self.assertEqual(report["owners"]["without_image_count"], 0)
            # The owner animates no actor, which is exactly why the
            # contribution path has to observe it.
            self.assertEqual(report["owners"]["bank_only_owners"], 1)
            self.assertEqual(report["indices"]["out_of_range_count"], 0)
            self.assertEqual(report["indices"]["misplaced_count"], 0)
            self.assertTrue(report["blends"]["closed"])
            self.assertEqual(report["blends"]["grids"], {"9x1": 1})
            self.assertTrue(report["faults"]["clean"])
            self.assertTrue(report["overhead"]["byte_closure"])
            self.assertTrue(report["verdict"]["attribution_complete"])
            self.assertTrue(report["verdict"]["sources_resolved"])

    def test_a_four_cell_grid_reports_a_3x3_off_centre_on_both_axes(self) -> None:
        """The evidence CAP2.7 exists to produce.

        Every multi-blend sequence three theatre captures reached is a 9x1
        firing two cells. All 49 3x3 grids in the game are weapon aim layers on
        aim_yaw and aim_pitch, which no cutscene enters. This proves the whole
        path -- probe record layout, finalizer, blend detail, declared-versus-
        observed span, verdict -- without a game.
        """
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._attribution_session(
                session,
                contribution_record(
                    b"SEQP", 8, 103, sequence_index=2, num_blends=9,
                    group_size=(3, 3), param_index=(2, 3),
                    blend_cell=(1, 1), blend_weight=(0.4, 0.6),
                )
                # A 3x3 resolved to (1, 1) reads four slots, so a faithful
                # scope carries four decoded cells.
                + b"".join(
                    contribution_record(
                        b"ANIM", 9 + index, 104 + index, animation_index=5
                    )
                    for index in range(4)
                ),
            )
            report = verify_attribution(session)
            blended = report["blends"]
            self.assertEqual(blended["grids"], {"3x3": 1})
            detail = blended["grid_detail"]["3x3"]
            self.assertEqual(
                [axis["param_index"] for axis in detail["axes"]], [[2], [3]]
            )
            for axis in detail["axes"]:
                self.assertEqual(axis["size"], 3)
                self.assertEqual(axis["cells"], {"1": 1})
                self.assertEqual(axis["weights"]["interior"], 1)
                self.assertTrue(axis["off_centre"])
            self.assertTrue(detail["off_centre_on_both_axes"])
            self.assertEqual(blended["off_centre_grids"], ["3x3"])
            self.assertEqual(
                blended["corpus_grid_coverage"]["reached"], ["3x3"]
            )
            self.assertIn("9x1", blended["corpus_grid_coverage"]["unreached"])

            # Two quantities from two observation points: the axis cells the
            # blend resolver witnessed, and the cell records the decoder wrote.
            spanned = report["declared_cells"]
            self.assertEqual(spanned["by_declared_span"], {"4": 1})
            self.assertEqual(spanned["disagreeing"], 0)
            self.assertTrue(spanned["closed"])
            self.assertEqual(report["multiplicity"]["cells_per_sequence"]["4"], 1)

            self.assertTrue(report["verdict"]["sources_resolved"])
            self.assertEqual(report["verdict"]["blend_grids"], {"3x3": 1})
            self.assertEqual(report["verdict"]["off_centre_grids"], ["3x3"])
            self.assertIn("3x3", report["verdict"]["statement"])
            self.assertEqual(missing_grids(report, ["3x3"]), [])
            self.assertEqual(missing_grids(report, ["9x1"]), ["9x1"])

    def test_a_centred_grid_is_not_reported_as_off_centre(self) -> None:
        # A 3x3 whose every record sits at cell zero with weight zero read the
        # centre cell alone. It still appears in the shape histogram, which is
        # why the histogram alone cannot evidence the four-cell path.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._attribution_session(
                session,
                contribution_record(
                    b"SEQP", 8, 103, sequence_index=2, num_blends=9,
                    group_size=(3, 3), param_index=(2, 3),
                    blend_cell=(0, 0), blend_weight=(0.0, 0.0),
                )
                + b"".join(
                    contribution_record(
                        b"ANIM", 9 + index, 104 + index, animation_index=5
                    )
                    for index in range(4)
                ),
            )
            report = verify_attribution(session)
            self.assertEqual(report["blends"]["grids"], {"3x3": 1})
            detail = report["blends"]["grid_detail"]["3x3"]
            for axis in detail["axes"]:
                self.assertEqual(axis["cells"], {"0": 1})
                self.assertEqual(axis["weights"]["at_zero"], 1)
                self.assertFalse(axis["off_centre"])
            self.assertFalse(detail["off_centre_on_both_axes"])
            self.assertEqual(report["blends"]["off_centre_grids"], [])
            # Reaching no off-centre grid is a coverage fact, not an
            # attribution failure, so the verdict itself still holds.
            self.assertTrue(report["verdict"]["sources_resolved"])
            self.assertEqual(missing_grids(report, ["3x3"]), ["3x3"])

    def test_attribution_comparison_across_scenes_reports_grid_coverage(
        self,
    ) -> None:
        # Owner count is a property of a scene's cast, so two scenes differ on
        # it by construction and a difference is not a disagreement. What does
        # compare across scenes is which grids each one reached.
        theatre = {
            "session": "cap11_a",
            "identity": {"map": "sp_theatre"},
            "coverage": {"sequences": 3440, "cells": 3600},
            "owners": {"identities": 34, "bank_only_owners": 17},
            "blends": {"grids": {"9x1": 3440}, "off_centre_grids": ["9x1"]},
            "declared_cells": {"by_declared_span": {"2": 3440}},
            "verdict": {"sources_resolved": True},
        }
        tutorial = {
            **theatre,
            "session": "cap27_b",
            "identity": {"map": "sp_tutorial_1"},
            "owners": {"identities": 12, "bank_only_owners": 4},
            "blends": {
                "grids": {"9x1": 900, "3x3": 40},
                "off_centre_grids": ["3x3", "9x1"],
            },
            "declared_cells": {"by_declared_span": {"2": 900, "4": 40}},
        }
        across = compare_attribution([theatre, tutorial])
        self.assertFalse(across["same_map"])
        self.assertNotIn("disagree", across["statement"])
        self.assertIn("coverage rather than agreement", across["statement"])
        self.assertIn("3x3", across["statement"])
        self.assertEqual(across["rows"][1]["four_cell_scopes"], 40)
        self.assertEqual(across["rows"][0]["four_cell_scopes"], None)
        # A run that failed still reads as a disagreement, whatever the maps.
        failing = compare_attribution(
            [theatre, {**tutorial, "verdict": {"sources_resolved": False}}]
        )
        self.assertIn("did not resolve every source", failing["statement"])

    def test_attribution_verifier_reports_a_cell_outside_every_scope(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._attribution_session(
                session,
                contribution_record(
                    b"SEQP", 8, 103, sequence_index=2,
                )
                + contribution_record(
                    b"ANIM", 9, 104, animation_index=5, scope=0
                ),
            )
            report = verify_attribution(session)
            self.assertEqual(report["coverage"]["unscoped"], 1)
            self.assertFalse(report["verdict"]["attribution_complete"])
            self.assertIn("outside every", report["verdict"]["statement"])

    def test_attribution_verifier_reports_a_contribution_outside_a_pose_build(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._attribution_session(
                session,
                contribution_record(
                    b"SEQP", 8, 103, sequence_index=2, generation=0
                )
                + contribution_record(b"ANIM", 9, 104, animation_index=5),
            )
            report = verify_attribution(session)
            self.assertEqual(report["coverage"]["unbracketed"], 1)
            self.assertFalse(report["verdict"]["attribution_complete"])

    def test_attribution_verifier_reports_an_owner_without_a_census_row(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._attribution_session(
                session,
                contribution_record(
                    b"SEQP", 8, 103, sequence_index=2, checksum=0x1234
                )
                + contribution_record(
                    b"ANIM", 9, 104, animation_index=5, checksum=0x1234
                ),
            )
            report = verify_attribution(session)
            self.assertEqual(report["owners"]["unobserved_count"], 1)
            self.assertFalse(report["verdict"]["sources_resolved"])
            self.assertIn("census observation", report["verdict"]["statement"])

    def test_attribution_verifier_reports_an_index_beyond_the_owners_count(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._attribution_session(
                session,
                contribution_record(
                    b"SEQP", 8, 103, sequence_index=CONTRIBUTION_NUM_SEQ
                )
                + contribution_record(b"ANIM", 9, 104, animation_index=5),
            )
            report = verify_attribution(session)
            self.assertEqual(report["indices"]["out_of_range_count"], 1)
            self.assertFalse(report["verdict"]["sources_resolved"])

    def test_attribution_verifier_reports_a_descriptor_off_its_stride(
        self,
    ) -> None:
        """A pointer that is not index * stride means one of the two is wrong."""
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._attribution_session(
                session,
                contribution_record(
                    b"SEQP", 8, 103, sequence_index=2,
                    sequence_descriptor=CONTRIBUTION_OWNER_HDR
                    + CONTRIBUTION_SEQ_INDEX_OFF
                    + 2 * 764
                    + 8,
                )
                + contribution_record(b"ANIM", 9, 104, animation_index=5),
            )
            report = verify_attribution(session)
            self.assertEqual(report["indices"]["misplaced_count"], 1)
            self.assertFalse(report["verdict"]["sources_resolved"])

    def test_attribution_verifier_reports_a_blend_grid_that_does_not_close(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._attribution_session(
                session,
                contribution_record(
                    b"SEQP", 8, 103, sequence_index=2, num_blends=9,
                    group_size=(3, 1),
                )
                + contribution_record(b"ANIM", 9, 104, animation_index=5),
            )
            report = verify_attribution(session)
            self.assertEqual(report["blends"]["group_product_mismatch"], 1)
            self.assertFalse(report["verdict"]["sources_resolved"])

    def test_attribution_verifier_reports_an_unwitnessed_blend_weight(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._attribution_session(
                session,
                contribution_record(
                    b"SEQP", 8, 103, sequence_index=2, faults=1 << 5
                )
                + contribution_record(b"ANIM", 9, 104, animation_index=5),
            )
            report = verify_attribution(session)
            self.assertEqual(report["blends"]["unwitnessed"], 1)
            self.assertEqual(
                report["faults"]["by_name"], {"blend_unwitnessed": 1}
            )
            self.assertFalse(report["verdict"]["sources_resolved"])

    def test_attribution_verifier_answers_a_capture_without_contributions(
        self,
    ) -> None:
        """A database predating the stream is answered, not rejected."""
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=animation_record(b"BASE", 2, 101),
            )
            finalize(session)
            report = verify_attribution(session)
            self.assertFalse(report["support"]["carries_contributions"])
            self.assertFalse(report["verdict"]["judgeable"])
            self.assertIn(
                "cannot be judged", report["verdict"]["statement"]
            )

    def test_contributions_are_events_rather_than_a_dictionary(self) -> None:
        """They join the generation spine, so they belong in `records`."""
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._attribution_session(session, self._clean_contributions())
            connection = sqlite3.connect(session / "capture.sqlite")
            try:
                kinds = {
                    row[0]
                    for row in connection.execute(
                        "SELECT DISTINCT kind FROM records"
                    )
                }
                self.assertIn("SEQP", kinds)
                self.assertIn("ANIM", kinds)
                tables = {
                    row[0]
                    for row in connection.execute(
                        "SELECT name FROM sqlite_master WHERE type = 'table'"
                    )
                }
                self.assertNotIn("contributions", tables)
            finally:
                connection.close()

    def test_calibration_closes_byte_accounting_over_the_contribution_stream(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._attribution_session(session, self._clean_contributions())
            report = calibrate(session)
            self.assertTrue(report["volume"]["byte_closure"])

    def test_prior_verdicts_survive_a_contribution_stream(self) -> None:
        """The four earlier tasks re-establish on the widened database."""
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._attribution_session(session, self._clean_contributions())
            generation = verify_generation(session)
            self.assertEqual(
                generation["coverage"]["unassigned"], 0, generation["verdict"]
            )
            census = verify_census(session, join_source=False)
            self.assertEqual(census["coverage"]["identities_unobserved"], 0)

    def test_contribution_layouts_match_the_probe_that_writes_them(
        self,
    ) -> None:
        # The C++ struct is the definition and the reader re-declares it, so
        # nothing but this test stops the two drifting apart.
        source = (
            REPO_ROOT
            / "research"
            / "tooling"
            / "capture"
            / "live_pose_hook.cpp"
        ).read_text(encoding="utf-8")
        for struct_name, layout in (
            ("ContributionFileHeader", CONTRIBUTION_FILE_HEADER),
            ("ContributionRecordHeader", CONTRIBUTION_RECORD_HEADER),
        ):
            self.assertIn(
                f"sizeof({struct_name}) == {layout.size}", source, struct_name
            )
        self.assertIn('std::memcpy(contributionHeader.magic, "ELCON2", 6)', source)
        # The owner census is what makes a bank model joinable at all.
        self.assertIn("ObserveStudioHeader(ownerHdr, checksum)", source)
        # The cell frame emits its record after the original returns, so the
        # accumulator has to be cleared before the call rather than after it.
        # Order is the whole correctness argument, so it is asserted directly.
        body = source[source.index("void __cdecl HookDecodeSelectedBones(") :]
        self.assertLess(
            body.index("ResetChannelAccumulator();"),
            body.index("gOriginalDecodeSelectedBones("),
        )

    # --- CAP2.5 consumed byte spans ---

    def _span_session(self, session: Path, contributions: bytes) -> None:
        self._attribution_session(session, contributions)

    @staticmethod
    def _query(session: Path, statement: str, *parameters: object) -> list[tuple]:
        # A context manager around sqlite3.connect commits but does not close,
        # and Windows will not delete the session directory while the handle
        # is open, so every read here closes explicitly.
        connection = sqlite3.connect(session / "capture.sqlite")
        try:
            return connection.execute(statement, parameters).fetchall()
        finally:
            connection.close()

    def _spans(self, session: Path, kind: str) -> list[tuple[int, int, str]]:
        spans: list[tuple[int, int, str]] = []
        for (blob,) in self._query(
            session,
            "SELECT intervals FROM span_sets WHERE kind = ? ORDER BY id",
            kind,
        ):
            spans.extend(decode_intervals(bytes(blob)))
        return spans

    def _walk(self, index: int, frame: int) -> list[tuple[int, int, str]]:
        walker = Walker(model_image(0xABCD, "models/bank.mdl"), False)
        return merge(resolve_cell(walker, index, frame, (0, 1), (0, 1)))

    def test_span_resolver_walks_a_cell_to_its_exact_bytes(self) -> None:
        """A one-run track costs its header and the two keys around the frame.

        Clip 4 samples frame 0 of a four-frame clip whose only track is bone 1's
        rotY, so the walk reads one run header, the two keys bracketing the
        frame, that channel's rotscale, and the bind components of every channel
        that is not animated. Nothing else in the image is touched.
        """
        spans = self._walk(4, sampled_frame(DEFAULT_CLIPS[4].numframes))
        block = animation_block_offset(4)
        self.assertIn(
            (block, block + 2 * ANIM_RECORD_STRIDE, ROLE_ANIM_RECORD), spans
        )
        headers = [span for span in spans if span[2] == ROLE_TRACK_HEADER]
        keys = [span for span in spans if span[2] == ROLE_TRACK_KEY]
        self.assertEqual(len(headers), 1)
        self.assertEqual(keys, [(headers[0][1], headers[0][1] + 4, ROLE_TRACK_KEY)])
        # Only numframes and animindex are read out of the 72-byte descriptor,
        # so the other 64 bytes stay unclaimed.
        descriptor = [
            span for span in spans if span[2] == ROLE_ANIMATION_DESCRIPTOR
        ]
        self.assertEqual([end - start for start, end, _ in descriptor], [4, 4])

    def test_span_resolver_reads_two_bytes_of_every_run_it_skips(self) -> None:
        """The walk stops at the frame; a skipped run costs its header alone.

        This is the difference CAP4.2 exists to see. The exporter walks every
        key of every run; retail reads two bytes of each run it passes over and
        never looks at their keys.
        """
        spans = self._walk(6, sampled_frame(DEFAULT_CLIPS[6].numframes))
        headers = [span for span in spans if span[2] == ROLE_TRACK_HEADER]
        keys = [span for span in spans if span[2] == ROLE_TRACK_KEY]
        # Three runs of two keys each; the frame lands in the second, so the
        # first is skipped and only the second's keys are read.
        self.assertEqual(len(headers), 2)
        self.assertEqual([end - start for start, end, _ in headers], [2, 2])
        self.assertEqual(headers[1][0] - headers[0][0], 6)
        self.assertEqual(keys, [(headers[1][1], headers[1][1] + 4, ROLE_TRACK_KEY)])

    def test_span_store_keeps_the_frame_out_of_its_shapes(self) -> None:
        """One clip at two frames is one shape and two covered frames.

        Keying a span set by its frame is what turned a 4 GB capture into a 13
        GB one, because a cutscene samples one clip at thousands of them. The
        frame-varying part is the track walk, which lands in the union instead.
        """
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._span_session(
                session,
                contribution_record(b"SEQP", 8, 103, sequence_index=2, bones=2)
                + contribution_record(
                    b"ANIM", 9, 104, animation_index=6, bones=2, channel_frame=0
                )
                + contribution_record(
                    b"ANIM", 10, 105, animation_index=6, bones=2, channel_frame=2
                ),
            )
            report = resolve_spans(session)
            self.assertEqual(report["counts"]["faulted"], 0)
            self.assertEqual(report["counts"]["shape_hits"], 1)
            self.assertEqual(
                self._query(session, "SELECT DISTINCT frame FROM record_span_sets "
                            "WHERE frame IS NOT NULL ORDER BY frame"),
                [(0,), (2,)],
            )
            # No track span reaches the shape; the union carries both frames'.
            self.assertEqual(
                [
                    role
                    for role in {span[2] for span in self._spans(session, "ANIM")}
                    if role in (ROLE_TRACK_HEADER, ROLE_TRACK_KEY)
                ],
                [],
            )
            covered = self._query(
                session, "SELECT sum(\"end\" - start) FROM model_coverage"
            )[0][0]
            self.assertGreater(covered, 0)

    def test_span_resolver_stops_at_a_zero_weight_record(self) -> None:
        """A zero-weight record costs four bytes and ends the decode."""
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._span_session(
                session,
                contribution_record(
                    b"SEQP", 8, 103, sequence_index=2, bones=2
                )
                + contribution_record(
                    b"ANIM", 9, 104, animation_index=3, bones=2
                ),
            )
            resolve_spans(session)
            spans = self._spans(session, "ANIM")
            self.assertEqual(
                [span[2] for span in spans],
                [ROLE_HEADER_FIELD]
                + [ROLE_ANIMATION_DESCRIPTOR, ROLE_ANIMATION_DESCRIPTOR]
                + [ROLE_ANIM_RECORD, ROLE_ANIM_RECORD],
            )
            # NumBones@240 and BoneIndex@244 are adjacent, so they merge.
            self.assertEqual(
                sorted(end - start for start, end, _ in spans), [4, 4, 4, 4, 8]
            )

    def test_span_intervals_round_trip_through_their_blob(self) -> None:
        """The store is delta encoded, so the decoder is what keeps it readable."""
        spans = [
            (240, 248, ROLE_HEADER_FIELD),
            (2060, 2064, ROLE_ANIMATION_DESCRIPTOR),
            (2000, 2032, ROLE_ANIM_RECORD),
            (0x10000, 0x10002, ROLE_TRACK_HEADER),
        ]
        self.assertEqual(decode_intervals(encode_intervals(spans)), spans)

    def test_span_resolver_records_a_cell_that_decoded_nothing(self) -> None:
        """A mask selecting no bone is a real outcome, not a lost witness.

        The cell frame reads the bone count, the bone array base, numframes and
        animindex before it looks at the mask, then decodes nothing -- so such
        a cell consumes exactly sixteen bytes.
        """
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._span_session(
                session,
                contribution_record(
                    b"SEQP", 8, 103, sequence_index=2, bones=2, mask_bones=()
                )
                + contribution_record(
                    b"ANIM", 9, 104, animation_index=4, bones=2,
                    channel_bones=(),
                ),
            )
            report = resolve_spans(session)
            self.assertEqual(report["counts"]["faulted"], 0)
            self.assertEqual(report["counts"]["decoded_nothing"], 1)
            spans = self._spans(session, "ANIM")
            self.assertEqual(
                [span[2] for span in spans],
                [ROLE_HEADER_FIELD]
                + [ROLE_ANIMATION_DESCRIPTOR, ROLE_ANIMATION_DESCRIPTOR],
            )
            verdict = verify_spans(session)["verdict"]
            self.assertTrue(verdict["roots_agree"], verdict["statement"])
            self.assertTrue(verdict["spans_resolved"])

    def test_span_resolver_deduplicates_identical_cells(self) -> None:
        """Two cells of one shape cost one span set and two references."""
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._span_session(
                session,
                contribution_record(b"SEQP", 8, 103, sequence_index=2, bones=2)
                + contribution_record(b"ANIM", 9, 104, animation_index=4, bones=2)
                + contribution_record(b"ANIM", 10, 105, animation_index=4, bones=2),
            )
            report = resolve_spans(session)
            self.assertEqual(report["counts"]["shape_hits"], 1)
            self.assertEqual(report["counts"]["faulted"], 0)
            self.assertEqual(
                self._query(
                    session,
                    "SELECT (SELECT count(*) FROM span_sets), "
                    "(SELECT count(*) FROM record_span_sets)",
                ),
                [(2, 3)],
            )

    def test_span_resolver_refuses_a_database_it_already_resolved(self) -> None:
        """A finalized database is evidence, so the pass runs over it once."""
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._span_session(
                session,
                contribution_record(b"SEQP", 8, 103, sequence_index=2, bones=2)
                + contribution_record(b"ANIM", 9, 104, animation_index=4, bones=2),
            )
            resolve_spans(session)
            with self.assertRaises(ValueError) as raised:
                resolve_spans(session)
            self.assertIn("resolved once", str(raised.exception))

    def test_span_resolver_records_the_digest_it_read(self) -> None:
        """Writing into the evidence file invalidates the finalizer's hash.

        The digest taken before the pass travels in the database, so anything
        comparing against the one `result.json` carries reads that instead of
        concluding the evidence was altered.
        """
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._span_session(
                session,
                contribution_record(b"SEQP", 8, 103, sequence_index=2, bones=2)
                + contribution_record(b"ANIM", 9, 104, animation_index=4, bones=2),
            )
            report = resolve_spans(session)
            stored = dict(
                self._query(
                    session,
                    "SELECT key, value FROM capture_metadata "
                    "WHERE key LIKE 'spans_%'",
                )
            )
            self.assertEqual(
                json.loads(stored["spans_source_database_sha256"]),
                report["source_database_sha256"],
            )
            self.assertEqual(report["integrity_check"], "ok")

    def test_span_resolver_faults_an_index_outside_the_owner(self) -> None:
        """An unresolvable cell is recorded as one, not skipped."""
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._span_session(
                session,
                contribution_record(b"SEQP", 8, 103, sequence_index=2, bones=2)
                + contribution_record(
                    b"ANIM", 9, 104, animation_index=1, bones=2,
                    channel_bones=(0, 5),
                ),
            )
            report = resolve_spans(session)
            self.assertEqual(report["counts"]["faulted"], 1)
            self.assertTrue(
                any("bone 5" in reason for reason in report["faults"])
            )

    def _resolved_span_session(
        self, session: Path, contributions: bytes
    ) -> dict:
        self._span_session(session, contributions)
        resolve_spans(session)
        return verify_spans(session)

    def test_span_verifier_accepts_a_witnessed_run(self) -> None:
        """Every witnessed pointer lands where the independent walker predicts."""
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            report = self._resolved_span_session(
                session,
                contribution_record(b"SEQP", 8, 103, sequence_index=2, bones=2)
                + contribution_record(b"ANIM", 9, 104, animation_index=4, bones=2),
            )
            self.assertTrue(report["verdict"]["judgeable"])
            self.assertTrue(report["verdict"]["roots_agree"])
            self.assertTrue(report["verdict"]["spans_resolved"])
            self.assertEqual(report["roots"]["misplaced_records"], 0)
            self.assertEqual(report["roots"]["misplaced_bones"], 0)
            self.assertEqual(report["frames"]["disagreeing"], 0)
            self.assertEqual(report["gating"]["decoded_beyond_mask"], 0)

    def test_span_verifier_reports_a_misplaced_record_pointer(self) -> None:
        """A witnessed base off the animindex indirection fails the check.

        This is what separates evidence from convention: the probe latches a
        pointer and the walker predicts one, and nothing reconciles them.
        """
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            block = animation_block_offset(4)
            report = self._resolved_span_session(
                session,
                contribution_record(b"SEQP", 8, 103, sequence_index=2, bones=2)
                + contribution_record(
                    b"ANIM", 9, 104, animation_index=4, bones=2,
                    channel_record_base=CONTRIBUTION_OWNER_HDR + block + 16,
                ),
            )
            self.assertEqual(report["roots"]["misplaced_records"], 1)
            self.assertFalse(report["verdict"]["roots_agree"])
            self.assertIn("do not land where", report["verdict"]["statement"])

    def test_span_verifier_reports_a_frame_that_disagrees_with_the_cycle(
        self,
    ) -> None:
        """floor((numframes - 1) * cycle) is now a claim a capture can refute."""
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            report = self._resolved_span_session(
                session,
                contribution_record(b"SEQP", 8, 103, sequence_index=2, bones=2)
                + contribution_record(
                    b"ANIM", 9, 104, animation_index=6, bones=2,
                    channel_frame=5,
                ),
            )
            self.assertEqual(report["frames"]["disagreeing"], 1)
            self.assertFalse(report["verdict"]["roots_agree"])
            self.assertIn("numframes", report["verdict"]["statement"])

    def test_span_verifier_reports_a_bitmap_wider_than_its_mask(self) -> None:
        """A cell decoding a bone its sequence did not select is a failure."""
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            report = self._resolved_span_session(
                session,
                contribution_record(
                    b"SEQP", 8, 103, sequence_index=2, bones=1
                )
                + contribution_record(
                    b"ANIM", 9, 104, animation_index=4, bones=2,
                    channel_bones=(0, 1),
                ),
            )
            self.assertEqual(report["gating"]["decoded_beyond_mask"], 1)
            self.assertFalse(report["verdict"]["roots_agree"])
            self.assertIn("mask does not select", report["verdict"]["statement"])

    def test_a_channel_fault_is_not_an_attribution_fault(self) -> None:
        """CAP2.4 judges its own bits, not every bit in the word.

        A cell whose mask selects no bone carries the unwitnessed-channel bit,
        and that record named its source perfectly well; counting it would make
        a widened probe read as an attribution regression.
        """
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._span_session(
                session,
                contribution_record(
                    b"SEQP", 8, 103, sequence_index=2, bones=2, mask_bones=()
                )
                + contribution_record(
                    b"ANIM", 9, 104, animation_index=4, bones=2,
                    channel_bones=(), faults=1 << 8,
                ),
            )
            report = verify_attribution(session)
            self.assertEqual(report["faults"]["records_with_a_fault"], 0)
            self.assertTrue(report["faults"]["clean"])

    def test_resolving_leaves_every_earlier_verifier_able_to_read_it(
        self,
    ) -> None:
        """The pass writes into evidence four other tools already read.

        Every capture_metadata value is JSON and every verifier loads it that
        way, so a bare string written here is a syntax error in all of them
        rather than a missing key.
        """
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._span_session(
                session,
                contribution_record(b"SEQP", 8, 103, sequence_index=2, bones=2)
                + contribution_record(b"ANIM", 9, 104, animation_index=4, bones=2),
            )
            resolve_spans(session)
            for key, value in self._query(
                session, "SELECT key, value FROM capture_metadata"
            ):
                json.loads(value)
            # Each verifier has its own verdict shape, so what is asserted is
            # that all four still read the database at all.
            for verifier in (
                verify_attribution,
                verify_generation,
                verify_census,
                verify_actors,
            ):
                self.assertIn("verdict", verifier(session))

    def test_span_verifier_answers_a_database_without_channels(self) -> None:
        """A CAP2.4 database is answered as unjudgeable, never raised on."""
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._attribution_session(session, self._clean_contributions())
            report = verify_spans(session)
            self.assertFalse(report["verdict"]["judgeable"])
            self.assertIn("resolve_consumed_spans", report["verdict"]["statement"])

    def test_span_verifier_holds_shapes_identical_across_runs(self) -> None:
        """One shape has one answer, whatever frame each run sampled it at.

        The consumed union is deliberately not the claim: which frames a run
        samples depends on where the operator reached the trigger, so two honest
        runs cover different bytes of the same clip. The walk being a function
        of the shape alone is what must hold.
        """
        reports = []
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            for directory, cycle in ((first, 0.0), (second, CONTRIBUTION_CYCLE)):
                reports.append(
                    self._resolved_span_session(
                        Path(directory),
                        contribution_record(
                            b"SEQP", 8, 103, sequence_index=2, bones=2
                        )
                        + contribution_record(
                            b"ANIM", 9, 104, animation_index=6, bones=2,
                            cycle=cycle,
                        ),
                    )
                )
            across = compare_spans(reports)
            self.assertEqual(across["divergent_shapes"], [])
            self.assertGreater(across["shared_shapes"], 0)
            self.assertIn("byte-identical spans", across["statement"])
            # The two runs sampled different frames, so their unions differ --
            # and that is reported as coverage, not as a disagreement.
            self.assertNotEqual(
                reports[0]["spans"]["bytes_per_model"],
                reports[1]["spans"]["bytes_per_model"],
            )

    def test_channel_decoder_hooks_are_installed_before_the_cell_frame(
        self,
    ) -> None:
        """A cell must never be live while the decoders feeding it are not.

        Removal runs the other way for the same reason the blend resolver does:
        the consumer goes first, so a cell never reports an unwitnessed decode
        that in fact happened.
        """
        source = (
            REPO_ROOT
            / "research"
            / "tooling"
            / "capture"
            / "live_pose_hook.cpp"
        ).read_text(encoding="utf-8")
        install = source[source.index("bool InstallHooks(") :]
        cell = install.index("&HookDecodeSelectedBones)")
        for detour in ("&HookDecodeBoneQuaternion)", "&HookDecodeBonePosition)"):
            self.assertLess(install.index(detour), cell, detour)
        remove = source[source.index("void RemoveHooks()") :]
        for phase in ("Disable", "Release"):
            consumer = remove.index(f"{phase}(&gDecodeSelectedBonesHook)")
            for handle in (
                "gDecodeBonePositionHook",
                "gDecodeBoneQuaternionHook",
            ):
                self.assertLess(consumer, remove.index(f"{phase}(&{handle})"))

    def test_contribution_targets_are_declared_against_the_specification(
        self,
    ) -> None:
        """The generator asserts image_base + rva equals the spec's address."""
        profiles = json.loads(
            (
                REPO_ROOT
                / "research"
                / "tooling"
                / "capture"
                / "contracts"
                / "binary_profiles.json"
            ).read_text(encoding="utf-8")
        )
        client = next(
            profile
            for profile in profiles["profiles"]
            if profile["module"] == "client.dll"
        )
        targets = {
            target["semantic_label"]: target for target in client["targets"]
        }
        for label, source_label in (
            ("client.evaluate_sequence_pose", "evaluate_sequence_pose"),
            ("client.decode_selected_bones", "decode_selected_bones"),
            ("client.resolve_blend_axis_weight", "resolve_blend_axis_weight"),
        ):
            self.assertIn(label, targets)
            self.assertEqual(
                targets[label]["source_function_label"], source_label
            )
            # Every exit of the three is a bare RET, so the caller cleans.
            self.assertEqual(targets[label]["calling_convention"], "cdecl")
        # evaluate_sequence_pose begins MOV AL,[0x104902c9]. That operand is an
        # absolute address the loader rewrites whenever client.dll is not at its
        # preferred base, which it never is under ASLR, so the declaration has
        # to name the span rather than let a fixed comparison reject it.
        operand = targets["client.evaluate_sequence_pose"]["relocated_operand"]
        self.assertEqual(operand, {"offset": 1, "size": 4})
        self.assertEqual(
            targets["client.evaluate_sequence_pose"]["expected_bytes"],
            "a0c9024910",
        )
        # The other two carry no absolute operand, so they declare none.
        for label in (
            "client.decode_selected_bones",
            "client.resolve_blend_axis_weight",
        ):
            self.assertIsNone(targets[label].get("relocated_operand"))

    def test_relocated_operand_reaches_both_generated_registries(self) -> None:
        """The backend compares against the declaration the generator emits."""
        header = (
            REPO_ROOT
            / "research"
            / "tooling"
            / "capture"
            / "native"
            / "generated_binary_profiles.h"
        ).read_text(encoding="utf-8")
        index = header.index('"client.evaluate_sequence_pose"')
        block = header[index : header.index("},", index)]
        # Rva, ExpectedBytes, count, then the operand offset and size.
        self.assertIn("5u,\n        1u,\n        4u,", block)
        registry = (
            REPO_ROOT
            / "research"
            / "tooling"
            / "capture"
            / "generated_binary_profiles.py"
        ).read_text(encoding="utf-8")
        self.assertIn("'relocated_operand': {'offset': 1, 'size': 4}", registry)
        backend = (
            NATIVE_ROOT / "hook_backend.cpp"
        ).read_text(encoding="utf-8")
        # Rebased, not masked: the operand is still compared, byte for byte.
        self.assertIn("MatchesDeclaredPrologue", backend)
        self.assertIn("active.Profile->Pe.PreferredImageBase", backend)

    # ------------------------------------------------------------------
    # CAP2.6 -- trigger and scene events
    # ------------------------------------------------------------------

    def _scene_session(
        self,
        session: Path,
        scene_records: bytes,
        *,
        actor_records: bytes | None = None,
        done_extra: str = "",
    ) -> None:
        write_session(
            session,
            pose_records=pose_record(1, 100, "models/courtroom_bip1.mdl"),
            animation_records=animation_record(
                b"BASE", 2, 101, client_entity=0x1FFC
            ),
            actor_records=(
                actor_records
                if actor_records is not None
                else actor_record(3, 99, "models/courtroom_bip1.mdl")
            ),
            scene_records=scene_records,
            done=(
                "complete=1\nqueued=2\nwritten=2\ndropped=0\nqueue_peak=42\n"
                "scene_faults=0\nscene_overflow=0\nscene_unscoped=0\n"
                "scene_truncated=0\n" + done_extra
            ),
        )
        finalize(session)

    def _clean_scene_records(self) -> bytes:
        """A scene that starts, applies its animation set, binds one actor and
        dispatches one sequence event -- plus the client change it produced."""
        return (
            scene_request_record(b"SCNE", 10, 200, SCENE_STARTED)
            + scene_request_record(
                b"SANM",
                11,
                201,
                SCENE_ANIMSET,
                text1="models/cinematic/courtroom_bip1.mdl",
            )
            + scene_request_record(
                b"SBND",
                12,
                202,
                SCENE_BOUND,
                target_entity=0x7000,
                target_ref_handle=(7 << 13) | 40,
                actor_name="Courtroom_bip1",
                text2="Bip02>Bip01",
            )
            + scene_request_record(
                b"SEVT",
                13,
                203,
                SCENE_EVENT,
                event_type=SEQUENCE_EVENT,
                event_start=0.5,
                event_end=15.3,
                scene_time=0.5,
                actor_name="Courtroom_bip1",
                text0="entire_scene",
            )
            + sequence_change_record(14, 204)
        )

    def test_scene_finalizer_keeps_requests_out_of_the_event_table(
        self,
    ) -> None:
        # A scene request is neither a dictionary row nor an event on the
        # generation spine, so it lands in its own tables and never widens
        # `records` with a generation no scene record could carry.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._scene_session(session, self._clean_scene_records())
            connection = sqlite3.connect(session / "capture.sqlite")
            try:
                events = connection.execute(
                    "SELECT count(*) FROM scene_events"
                ).fetchone()[0]
                changes = connection.execute(
                    "SELECT count(*) FROM sequence_changes"
                ).fetchone()[0]
                scene_rows_in_records = connection.execute(
                    "SELECT count(*) FROM records WHERE stream_name = 'scene'"
                ).fetchone()[0]
                index = connection.execute(
                    "SELECT target_entity_index FROM scene_events"
                    " WHERE kind = 'SBND'"
                ).fetchone()[0]
            finally:
                connection.close()
            self.assertEqual(events, 4)
            self.assertEqual(changes, 1)
            self.assertEqual(scene_rows_in_records, 0)
            # The low 13 bits of the handle, decoded once at finalize time.
            self.assertEqual(index, 40)

    def test_scene_verifier_accepts_a_fully_attributed_run(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._scene_session(session, self._clean_scene_records())
            report = verify_scene(session)
            self.assertTrue(report["support"]["carries_scene"])
            self.assertTrue(report["support"]["carries_indices"])
            self.assertEqual(report["coverage"]["unscoped"], 0)
            self.assertEqual(report["coverage"]["scenes_started"], 1)
            self.assertEqual(report["binding"]["bindings_resolved"], 1)
            self.assertEqual(report["binding"]["bindings_with_an_index"], 1)
            self.assertEqual(report["activity"]["sequence_events"], 1)
            self.assertFalse(report["activity"]["activity_enum_observed"])
            self.assertTrue(report["truncation"]["lossless"])
            self.assertTrue(report["faults"]["clean"])
            self.assertTrue(report["overhead"]["byte_closure"])
            self.assertTrue(report["verdict"]["judgeable"])
            self.assertTrue(report["verdict"]["requests_complete"])
            self.assertTrue(report["verdict"]["requests_joined"])

    def test_scene_verifier_reproduces_the_renderable_offset(self) -> None:
        # CAP1.3's fixed +4 re-derived from a population it never measured:
        # both terms are recorded separately on every sequence change.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._scene_session(session, self._clean_scene_records())
            report = verify_scene(session)
            self.assertEqual(report["join"]["renderable_pairs"], 1)
            self.assertEqual(report["join"]["renderable_agreeing"], 1)
            self.assertTrue(report["join"]["joins"])

    def test_scene_verifier_reports_an_index_serving_two_client_entities(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._scene_session(
                session,
                self._clean_scene_records(),
                # One index, two live client entities, with no destruction
                # between them to separate the two actors.
                actor_records=(
                    actor_record(3, 99, "models/courtroom_bip1.mdl")
                    + actor_record(
                        4,
                        99,
                        "models/other.mdl",
                        entity=0x4000,
                        renderable=0x4004,
                    )
                ),
            )
            report = verify_scene(session)
            self.assertEqual(report["join"]["client_index_conflicts"], 1)
            self.assertFalse(report["verdict"]["requests_joined"])
            self.assertIn(
                "named more than one entity", report["verdict"]["statement"]
            )

    def test_scene_verifier_reports_a_record_outside_every_scene_scope(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._scene_session(
                session,
                scene_request_record(b"SCNE", 10, 200, SCENE_STARTED)
                + scene_request_record(
                    b"SBND",
                    11,
                    201,
                    SCENE_BOUND,
                    scope=0,
                    target_entity=0x7000,
                    target_ref_handle=(7 << 13) | 40,
                ),
            )
            report = verify_scene(session)
            self.assertEqual(report["coverage"]["unscoped"], 1)
            self.assertFalse(report["verdict"]["requests_complete"])
            self.assertIn(
                "outside every scene scope", report["verdict"]["statement"]
            )

    def test_scene_verifier_reports_an_event_whose_scene_never_started(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._scene_session(
                session,
                self._clean_scene_records()
                + scene_request_record(
                    b"SEVT",
                    15,
                    205,
                    SCENE_EVENT,
                    scene_entity=0x9999,
                    event_type=GESTURE_EVENT_TYPE,
                    text0="entire_scene",
                ),
            )
            report = verify_scene(session)
            self.assertEqual(
                report["coverage"]["events_without_a_started_scene"], 1
            )
            self.assertIn(
                "never saw start", report["verdict"]["statement"]
            )

    def test_scene_verifier_counts_an_actor_that_resolved_to_nothing(
        self,
    ) -> None:
        # The engine logs and drops the event, and the rest of the scene
        # continues, so this is an outcome to report rather than a failure.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._scene_session(
                session,
                self._clean_scene_records()
                + scene_request_record(
                    b"SBND",
                    15,
                    205,
                    SCENE_BOUND,
                    actor_name="Missing_Actor",
                    faults=1 << 5,
                ),
            )
            report = verify_scene(session)
            self.assertEqual(report["binding"]["bindings_unresolved"], 1)
            self.assertEqual(
                report["binding"]["unresolved_actors"][0]["actor"],
                "Missing_Actor",
            )
            self.assertTrue(report["faults"]["clean"])
            self.assertTrue(report["verdict"]["requests_complete"])

    def test_scene_verifier_measures_the_longest_string_it_saw(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._scene_session(
                session,
                scene_request_record(
                    b"SCNE",
                    10,
                    200,
                    SCENE_STARTED,
                    scene_file="s" * 60,
                ),
            )
            report = verify_scene(session)
            self.assertEqual(report["truncation"]["longest_scene_file"], 60)
            self.assertEqual(
                report["truncation"]["field_widths"]["scene_file"], 128
            )
            self.assertTrue(report["truncation"]["lossless"])

    def test_scene_verifier_reports_a_truncated_field(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._scene_session(
                session,
                scene_request_record(
                    b"SCNE", 10, 200, SCENE_STARTED, faults=1 << 2
                ),
            )
            report = verify_scene(session)
            self.assertEqual(report["truncation"]["truncated_records"], 1)
            self.assertFalse(report["truncation"]["lossless"])
            self.assertFalse(report["faults"]["clean"])

    def test_scene_verifier_resolves_a_caller_against_the_vampire_spec(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._scene_session(session, self._clean_scene_records())
            report = verify_scene(session)
            self.assertTrue(report["callers"]["available"])
            labels = {
                site["nearest_preceding_label"]
                for site in report["callers"]["sites"]
            }
            # The synthetic caller sits inside DispatchStartEvent's own range.
            self.assertIn("scene_dispatch_start_event", labels)
            self.assertEqual(report["callers"]["unresolved_records"], 0)

    def test_scene_verifier_answers_a_capture_without_a_scene_stream(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=animation_record(b"BASE", 2, 101),
            )
            finalize(session)
            report = verify_scene(session)
            self.assertFalse(report["support"]["carries_scene"])
            self.assertFalse(report["verdict"]["judgeable"])
            self.assertIn(
                "carries no scene stream", report["verdict"]["statement"]
            )

    def test_scene_verifier_answers_a_capture_without_entity_indices(
        self,
    ) -> None:
        # Every binding resolved to nothing, so no index was ever recorded.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._scene_session(
                session,
                scene_request_record(b"SCNE", 10, 200, SCENE_STARTED),
            )
            report = verify_scene(session)
            self.assertTrue(report["support"]["carries_scene"])
            self.assertFalse(report["support"]["carries_indices"])
            self.assertFalse(report["verdict"]["judgeable"])
            self.assertIn(
                "predates the entity-index fields",
                report["verdict"]["statement"],
            )

    def test_finalizer_reads_both_actor_stream_versions(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_session(
                session,
                pose_records=pose_record(1, 100, "models/test.mdl"),
                animation_records=animation_record(
                    b"BASE", 2, 101, client_entity=0x1FFC
                ),
                actor_records=actor_record(
                    3, 99, "models/test.mdl", version=2
                ),
                actor_version=2,
            )
            finalize(session)
            connection = sqlite3.connect(session / "capture.sqlite")
            try:
                row = connection.execute(
                    "SELECT ref_handle, entity_index FROM actor_observations"
                ).fetchone()
            finally:
                connection.close()
            self.assertEqual(row, (None, None))

    def test_prior_verdicts_survive_a_scene_stream(self) -> None:
        # Every earlier verifier has to keep judging a database that carries
        # the sixth stream and the widened actor stream.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._scene_session(session, self._clean_scene_records())
            # Each earlier verifier still reaches a verdict on the widened
            # database rather than failing to read it.
            generation = verify_generation(session)
            self.assertTrue(generation["verdict"]["statement"])
            # The five scene rows stay out of the generation verifier's
            # population entirely: it counts the pose and animation records and
            # nothing else, so no scene record can read as unassigned.
            self.assertEqual(
                generation["coverage"]["assigned"]
                + generation["coverage"]["unassigned"],
                2,
            )
            actors = verify_actors(session)
            self.assertTrue(actors["support"]["carries_actors"])
            self.assertTrue(actors["verdict"]["statement"])
            census = verify_census(session)
            self.assertTrue(census["verdict"]["statement"])
            attribution = verify_attribution(session)
            self.assertTrue(attribution["verdict"]["statement"])
            self.assertIsNotNone(verify(session))

    def test_scene_verifier_closes_the_anim_set_on_the_owner_checksum(
        self,
    ) -> None:
        # The decisive check, exercised over the verifier's own join: four
        # values recorded by four different hooks have to agree, and none is
        # derived from another. The contribution and census rows stand in for
        # what a real capture's other streams supply.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._scene_session(session, self._clean_scene_records())
            connection = sqlite3.connect(session / "capture.sqlite")
            try:
                connection.execute(
                    "INSERT INTO model_headers (stream_name, ordinal,"
                    " sequence_number, qpc, thread_id, studio_hdr, checksum,"
                    " previous_checksum, bone_count, bone_index, model_length,"
                    " include_model_count, include_model_index,"
                    " studio_version, reason, image_captured, generation,"
                    " model_name, raw_header)"
                    " VALUES ('census', 99, 99, 99, 7, 4096, 12345, 0, 1, 0, 0,"
                    " 0, 0, 2531, 1, 1, 0,"
                    " 'models/cinematic/Courtroom_bip1.mdl', x'00')"
                )
                # An evaluation is scoped by the pose build's bracket, which is
                # entered on the renderable rather than on the entity, so the
                # owner is reached through generation_entity.
                connection.execute(
                    "UPDATE records SET owner_checksum = 12345,"
                    " generation_entity = 8192, qpc = 300 WHERE id ="
                    " (SELECT min(id) FROM records)"
                )
                connection.commit()
            finally:
                connection.close()
            report = verify_scene(session)
            owner = report["join"]["anim_set_owner"]
            self.assertTrue(owner["available"])
            self.assertEqual(owner["compared"], 1)
            self.assertEqual(owner["agreeing"], 1)
            self.assertEqual(owner["disagreeing"], 0)
            self.assertTrue(report["join"]["joins"])
            self.assertIn(
                "animated under the animation set",
                report["verdict"]["statement"],
            )

    def test_an_anim_set_binding_without_a_renderable_is_counted_not_dropped(
        self,
    ) -> None:
        # A binding whose entity index the census never observed a renderable
        # for was skipped before the denominator was incremented, so it left
        # numerator and denominator alike and the join read as closed over a
        # population it never saw. A lost join is not an acceptable answer.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._scene_session(
                session,
                self._clean_scene_records(),
                # The scene binds handle index 40; this actor carries 41, so
                # the binding resolves to no renderable at all.
                actor_records=actor_record(
                    3,
                    99,
                    "models/courtroom_bip1.mdl",
                    ref_handle=(7 << 13) | 41,
                ),
            )
            connection = sqlite3.connect(session / "capture.sqlite")
            try:
                connection.execute(
                    "INSERT INTO model_headers (stream_name, ordinal,"
                    " sequence_number, qpc, thread_id, studio_hdr, checksum,"
                    " previous_checksum, bone_count, bone_index, model_length,"
                    " include_model_count, include_model_index,"
                    " studio_version, reason, image_captured, generation,"
                    " model_name, raw_header)"
                    " VALUES ('census', 99, 99, 99, 7, 4096, 12345, 0, 1, 0, 0,"
                    " 0, 0, 2531, 1, 1, 0,"
                    " 'models/cinematic/Courtroom_bip1.mdl', x'00')"
                )
                connection.execute(
                    "UPDATE records SET owner_checksum = 12345,"
                    " generation_entity = 8192, qpc = 300 WHERE id ="
                    " (SELECT min(id) FROM records)"
                )
                connection.commit()
            finally:
                connection.close()
            report = verify_scene(session)
            animset = report["join"]["anim_set_owner"]
            self.assertTrue(animset["available"])
            self.assertEqual(animset["bindings"], 1)
            self.assertEqual(animset["compared"], 0)
            self.assertEqual(animset["without_a_renderable"], 1)
            self.assertEqual(
                animset["without_a_renderable_detail"][0]["entity_index"], 40
            )

    def test_the_unjoined_aggregate_names_every_shard_with_its_own_denominator(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._scene_session(session, self._clean_scene_records())
            report = aggregate_integrity(session, join_source=False)

            names = [row["name"] for row in report["shards"]]
            self.assertEqual(len(names), len(set(names)))
            self.assertEqual(len(report["shards"]), len(SHARDS))
            for row in report["shards"]:
                self.assertIn(
                    row["status"],
                    ("present", "unavailable", "tool-unavailable", "missing"),
                )
                self.assertTrue(row["bucket"])
                if row["expected_nonzero"]:
                    self.assertTrue(row["because"], row["name"])

            # No bucket sums counts across shards: they measure different
            # populations, so a total would mean nothing.
            for entry in report["by_bucket"].values():
                self.assertNotIn("count", entry)
                self.assertLessEqual(entry["available"], entry["shards"])
                self.assertLessEqual(entry["nonzero"], entry["available"])

            # A shard whose non-zero value is a known property of the runtime
            # is reported with its reason and never folded into a pass.
            accounted = {row["name"] for row in report["accounted_nonzero"]}
            self.assertIn(
                "draws submitted by a frame that built no pose", accounted
            )
            for row in report["accounted_nonzero"]:
                self.assertTrue(row["because"])
                self.assertNotIn(
                    row["name"], {r["name"] for r in report["unexpected_nonzero"]}
                )

    def test_the_aggregate_separates_an_absent_stream_from_a_wrong_key(
        self,
    ) -> None:
        # Without this the table rots silently the first time a verifier
        # renames a key: the shard drops out and the population it counted
        # reads as accounted for.
        available = {"coverage": {"unassigned": 3, "records": 9}}
        self.assertEqual(
            resolve_shard(available, ("coverage", "unassigned")), (3, "present")
        )
        self.assertEqual(
            resolve_shard(available, ("coverage", "renamed")), (None, "missing")
        )
        # A whole section absent, None, or marked unavailable is a stream this
        # capture does not carry -- not a defect in the table.
        self.assertEqual(
            resolve_shard(available, ("requests", "unattributed")),
            (None, "unavailable"),
        )
        self.assertEqual(
            resolve_shard({"requests": None}, ("requests", "x")),
            (None, "unavailable"),
        )
        self.assertEqual(
            resolve_shard(
                {"requests": {"available": False, "reason": "no stream"}},
                ("requests", "unattributed"),
            ),
            (None, "unavailable"),
        )
        # Counts arrive as integers, as lists whose length is the count, and as
        # decimal strings, because the hook's counters travel through done.txt.
        self.assertEqual(
            resolve_shard({"a": {"b": [1, 2]}}, ("a", "b")), (2, "present")
        )
        self.assertEqual(
            resolve_shard({"a": {"b": "7"}}, ("a", "b")), (7, "present")
        )
        self.assertEqual(
            resolve_shard({"a": {"b": True}}, ("a", "b")), (None, "missing")
        )

    def test_every_declared_shard_path_exists_in_its_verifier_report(
        self,
    ) -> None:
        # Walked against live reports rather than a fixture of expectations,
        # so a verifier renaming a key fails here instead of silently dropping
        # the population that key counted.
        for name, build in (
            ("scene", lambda s: self._scene_session(s, self._clean_scene_records())),
            (
                "attribution",
                lambda s: self._attribution_session(s, self._clean_contributions()),
            ),
        ):
            with tempfile.TemporaryDirectory() as directory:
                session = Path(directory)
                build(session)
                report = aggregate_integrity(session, join_source=False)
                self.assertEqual(
                    report["missing_paths"],
                    [],
                    f"{name}: {[row['path'] for row in report['missing_paths']]}",
                )
                self.assertTrue(report["verdict"]["table_intact"])
                self.assertGreater(report["verdict"]["shards_available"], 20)

    def test_each_declared_sidecar_is_the_file_its_verifier_writes(self) -> None:
        # --reuse-reports reads these instead of re-running eight verifiers
        # over a multi-gigabyte database. A stale name would make it read
        # someone else's report, or fail claiming the verifier had not run.
        modules = {
            "calibrate": "calibrate_theatre_capture",
            "generation": "verify_pose_build_generation",
            "census": "verify_model_skeleton_census",
            "actors": "verify_actor_identity_lifetime",
            "join": "verify_entity_pointer_join",
            "attribution": "verify_source_attribution",
            "spans": "verify_consumed_spans",
            "scene": "verify_scene_requests",
        }
        self.assertEqual(set(modules), set(SIDECARS))
        self.assertEqual(set(modules), {shard.tool for shard in SHARDS})
        for tool, module in modules.items():
            source = (
                REPO_ROOT
                / "research"
                / "tooling"
                / "capture"
                / f"{module}.py"
            ).read_text(encoding="utf-8")
            self.assertIn(
                f'session / "{SIDECARS[tool]}"',
                source,
                f"{module} does not write {SIDECARS[tool]}",
            )

    def test_an_unexpected_nonzero_shard_fails_the_aggregate_verdict(self) -> None:
        clean = {
            "calibrate": {
                "identity": {"map": "sp_theatre"},
                "volume": {"per_kind": [], "bytes_written": 10, "queue_peak": 4},
                "integrity": {
                    "hook": {"dropped": "0"},
                    "failures": {
                        "hook_skipped": {"count": 0},
                        "hook_filtered": {"count": 0},
                        "incomplete_streams": {"count": 0},
                    },
                    "sequence_numbers": {"gaps": [], "duplicates": 0},
                },
            }
        }
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            report = aggregate_integrity(session, reports=clean)
            self.assertTrue(report["verdict"]["integrity_complete"])
            self.assertEqual(report["unexpected_nonzero"], [])

            dropped = json.loads(json.dumps(clean))
            dropped["calibrate"]["integrity"]["hook"]["dropped"] = "3"
            report = aggregate_integrity(session, reports=dropped)
            self.assertFalse(report["verdict"]["integrity_complete"])
            self.assertEqual(len(report["unexpected_nonzero"]), 1)
            self.assertIn("unaccounted for", report["verdict"]["statement"])

    def test_scene_verifier_reports_an_animset_no_owner_matches(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._scene_session(session, self._clean_scene_records())
            connection = sqlite3.connect(session / "capture.sqlite")
            try:
                connection.execute(
                    "INSERT INTO model_headers (stream_name, ordinal,"
                    " sequence_number, qpc, thread_id, studio_hdr, checksum,"
                    " previous_checksum, bone_count, bone_index, model_length,"
                    " include_model_count, include_model_index,"
                    " studio_version, reason, image_captured, generation,"
                    " model_name, raw_header)"
                    " VALUES ('census', 99, 99, 99, 7, 4096, 12345, 0, 1, 0, 0,"
                    " 0, 0, 2531, 1, 1, 0, 'models/other/unrelated.mdl',"
                    " x'00')"
                )
                # An evaluation is scoped by the pose build's bracket, which is
                # entered on the renderable rather than on the entity, so the
                # owner is reached through generation_entity.
                connection.execute(
                    "UPDATE records SET owner_checksum = 12345,"
                    " generation_entity = 8192, qpc = 300 WHERE id ="
                    " (SELECT min(id) FROM records)"
                )
                connection.commit()
            finally:
                connection.close()
            report = verify_scene(session)
            owner = report["join"]["anim_set_owner"]
            self.assertEqual(owner["disagreeing"], 1)
            self.assertFalse(report["join"]["joins"])
            self.assertIn(
                "animated under no model matching",
                report["verdict"]["statement"],
            )

    def test_calibration_closes_byte_accounting_over_the_scene_stream(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._scene_session(session, self._clean_scene_records())
            report = calibrate(session)
            closure = report["volume"]["byte_closure"]["scene"]
            self.assertTrue(closure["closes"])
            self.assertEqual(
                closure["derived_payload_bytes"],
                4 * SCENE_REQUEST_HEADER.size + SEQUENCE_CHANGE_HEADER.size,
            )

    def test_sequence_density_counts_the_scene_tables(self) -> None:
        # The CAP2.2 defect in a new place: scene rows draw from the same global
        # counter, so a density check that did not read their tables would turn
        # CAP1.2's loss proof into a false alarm.
        from research.tooling.capture.calibrate_theatre_capture import (
            SEQUENCE_TABLES,
        )

        self.assertIn("scene_events", SEQUENCE_TABLES)
        self.assertIn("sequence_changes", SEQUENCE_TABLES)

    def test_scene_layouts_match_the_probe_that_writes_them(self) -> None:
        source = (
            REPO_ROOT
            / "research"
            / "tooling"
            / "capture"
            / "live_pose_hook.cpp"
        ).read_text(encoding="utf-8")
        self.assertEqual(SCENE_FILE_HEADER.size, 128)
        self.assertEqual(SCENE_REQUEST_HEADER.size, 600)
        self.assertEqual(SEQUENCE_CHANGE_HEADER.size, 68)
        self.assertEqual(ACTOR_OBSERVATION_HEADER.size, 128)
        self.assertIn("sizeof(SceneFileHeader) == 128", source)
        self.assertIn("sizeof(SceneRequestHeader) == 600", source)
        self.assertIn("sizeof(SequenceChangeHeader) == 68", source)
        self.assertIn("sizeof(ActorObservationHeader) == 128", source)
        self.assertIn('std::memcpy(sceneHeader.magic, "ELSCN1", 6)', source)
        self.assertIn('std::memcpy(actorHeader.magic, "ELACT3", 6)', source)

    def test_scene_hooks_are_installed_inside_out_and_removed_outside_in(
        self,
    ) -> None:
        source = (
            REPO_ROOT
            / "research"
            / "tooling"
            / "capture"
            / "live_pose_hook.cpp"
        ).read_text(encoding="utf-8")
        install = source.index("bool InstallSceneHooks(")
        install_end = source.index("bool InstallHooks(", install)
        block = source[install:install_end]
        # A frame that opens a scope must not be live while the frames that
        # record inside it are not.
        order = [
            block.index('"client.maintain_sequence_transitions"'),
            block.index('"vampire.scene_find_named_entity"'),
            block.index('"vampire.scene_apply_anim_set"'),
            block.index('"vampire.scene_on_finished"'),
            block.index('"vampire.scene_cancel_playback"'),
            block.index('"vampire.scene_dispatch_start_event"'),
            block.index('"vampire.scene_start_playback"'),
        ]
        self.assertEqual(order, sorted(order))

        remove = source.index("void RemoveHooks()")
        removal = source[remove:]
        disable = removal.index("HookBackends::Disable(&gSceneStartPlaybackHook)")
        for handle in (
            "gSceneDispatchStartEventHook",
            "gSceneCancelPlaybackHook",
            "gSceneOnFinishedHook",
            "gSceneApplyAnimSetHook",
            "gSceneFindNamedEntityHook",
            "gMaintainSequenceTransitionsHook",
        ):
            later = removal.index(f"HookBackends::Disable(&{handle})")
            self.assertGreater(later, disable)
            disable = later
        release = removal.index("HookBackends::Release(&gSceneStartPlaybackHook)")
        for handle in (
            "gSceneDispatchStartEventHook",
            "gSceneCancelPlaybackHook",
            "gSceneOnFinishedHook",
            "gSceneApplyAnimSetHook",
            "gSceneFindNamedEntityHook",
            "gMaintainSequenceTransitionsHook",
        ):
            later = removal.index(f"HookBackends::Release(&{handle})")
            self.assertGreater(later, release)
            release = later

    def test_sequence_change_hook_reads_the_history_before_and_after(
        self,
    ) -> None:
        source = (
            REPO_ROOT
            / "research"
            / "tooling"
            / "capture"
            / "live_pose_hook.cpp"
        ).read_text(encoding="utf-8")
        start = source.index("HookMaintainSequenceTransitions(")
        block = source[start : source.index("\nconst elysium", start)]
        before = block.index("ReadTransitionCount(entity, &before)")
        call = block.index("gOriginalMaintainSequenceTransitions(")
        after = block.index("ReadTransitionCount(entity, &after)")
        self.assertLess(before, call)
        self.assertLess(call, after)
        # Emitted only when the history actually grew.
        self.assertIn("after != before", block)
        # Never CurrentGeneration: that would move a total CAP2.1 measured.
        self.assertIn("EnclosingGeneration()", source)

    def test_scene_targets_are_declared_against_the_specification(self) -> None:
        registry = json.loads(
            (
                REPO_ROOT
                / "research"
                / "tooling"
                / "capture"
                / "contracts"
                / "binary_profiles.json"
            ).read_text(encoding="utf-8")
        )
        profiles = {
            profile["module"]: profile for profile in registry["profiles"]
        }
        self.assertIn("vampire.dll", profiles)
        vampire = profiles["vampire.dll"]
        spec = json.loads(
            (
                REPO_ROOT
                / "research"
                / "cases"
                / "animation-pose"
                / "specs"
                / "scene_requests.json"
            ).read_text(encoding="utf-8")
        )
        functions = {
            entry["label"]: int(entry["address"], 16)
            for entry in spec["functions"]
        }
        image_base = int(spec["binary"]["image_base"], 0)
        self.assertEqual(len(vampire["targets"]), 6)
        for target in vampire["targets"]:
            # An incrementally linked module holds E9 thunks in its vtable
            # slots, so a vtable backend would validate a thunk.
            self.assertEqual(target["kind"], "inline")
            self.assertEqual(target["backend"], "inline_detour")
            self.assertNotIn("relocated_operand", target)
            label = target["source_function_label"]
            self.assertEqual(
                functions[label], image_base + int(target["rva"], 0)
            )
        widths = {
            target["source_function_label"]: len(target["expected_bytes"]) // 2
            for target in vampire["targets"]
        }
        # The inline backend refuses a decoded prologue wider than the
        # declaration, so the nine- and ten-byte widths are required.
        self.assertEqual(widths["scene_start_playback"], 9)
        self.assertEqual(widths["scene_cancel_playback"], 9)
        self.assertEqual(widths["scene_on_finished"], 10)

    def test_vampire_profile_names_the_module_the_process_loads(self) -> None:
        source = (
            REPO_ROOT
            / "research"
            / "tooling"
            / "capture"
            / "capture_theatre.py"
        ).read_text(encoding="utf-8")
        self.assertIn('"vampire.dll": _game_dll(game_root)', source)
        # Patch-first, falling through to the base install.
        self.assertIn('"Unofficial_Patch" / "dlls" / "vampire.dll"', source)
        self.assertIn('"Vampire" / "dlls" / "vampire.dll"', source)

    # --- CAP3: the join spine, the roll-up, and the payload store ----------

    def _indexable_session(self, session: Path) -> None:
        """One session carrying enough of every stream for the spine to join."""
        write_generation_session(
            session,
            [
                {
                    "entity": 0x1000,
                    "checksum": 0xAA,
                    "bones": 3,
                    "frame": b"DBLD",
                },
                {
                    "entity": 0x2000,
                    "checksum": 0xBB,
                    "bones": 2,
                    "frame": b"SHDW",
                },
            ],
        )

    def test_the_spine_materializes_every_bracket_as_one_generation(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._indexable_session(session)
            result = index_capture(session, rollup=False)
            connection = sqlite3.connect(session / "capture.sqlite")
            try:
                brackets = connection.execute(
                    "SELECT count(*) FROM generation_bracket"
                ).fetchone()[0]
                records = connection.execute(
                    "SELECT count(*) FROM records "
                    "WHERE kind IN ('PBLD', 'DBLD', 'SHDW')"
                ).fetchone()[0]
                # Every bracket record becomes exactly one generation, and no
                # generation is opened twice.
                self.assertEqual(brackets, records)
                self.assertEqual(
                    connection.execute(
                        "SELECT count(*) FROM (SELECT generation FROM "
                        "generation_bracket GROUP BY generation "
                        "HAVING count(*) > 1)"
                    ).fetchone()[0],
                    0,
                )
                # The bracket owner is the renderable and the evaluations name
                # the C_BaseAnimating; both are stored as recorded, so the
                # offset between them is measured here rather than assumed.
                self.assertEqual(
                    connection.execute(
                        "SELECT count(*) FROM pose_group WHERE actor_entity "
                        "IS NOT NULL AND bracket_entity != actor_entity + 4"
                    ).fetchone()[0],
                    0,
                )
            finally:
                connection.close()
            self.assertEqual(result["counts"]["pose_groups"], 2)

    def test_the_spine_refuses_a_second_pass_without_rewrite(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._indexable_session(session)
            index_capture(session, rollup=False)
            with self.assertRaises(ValueError):
                index_capture(session, rollup=False)
            # The guard makes a second pass deliberate, not impossible.
            index_capture(session, rollup=False, rewrite=True)
            connection = sqlite3.connect(session / "capture.sqlite")
            try:
                for table in INDEX_TABLES:
                    connection.execute(f"SELECT count(*) FROM {table}")
            finally:
                connection.close()

    def test_the_roll_up_is_stored_with_the_counts_cap33_names(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._indexable_session(session)
            index_capture(session, rollup=True, join_source=False)
            connection = sqlite3.connect(session / "capture.sqlite")
            try:
                stored = {
                    key
                    for (key,) in connection.execute(
                        "SELECT key FROM integrity_summary"
                    )
                }
                shards = connection.execute(
                    "SELECT count(*) FROM integrity_shard"
                ).fetchone()[0]
            finally:
                connection.close()
            self.assertEqual(shards, len(SHARDS))
            # The database reports its own counts rather than needing the
            # verifiers re-run to learn them.
            for key in ("hook", "failures", "sequence_numbers", "volume", "zero"):
                self.assertIn(key, stored)
            # The spine's own unjoined counts are not in the roll-up, which is
            # computed before this pass writes, so they are stored beside it.
            self.assertIn("spine", stored)

    def test_compaction_keeps_every_record_byte_for_byte(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._indexable_session(session)
            database = session / "capture.sqlite"
            connection = sqlite3.connect(database)
            try:
                before = connection.execute(
                    "SELECT * FROM records ORDER BY id"
                ).fetchall()
                columns = [
                    row[1]
                    for row in connection.execute("PRAGMA table_info(records)")
                ]
            finally:
                connection.close()
            report = compact(session)
            connection = sqlite3.connect(database)
            try:
                after = connection.execute(
                    "SELECT * FROM records ORDER BY id"
                ).fetchall()
                columns_after = [
                    row[1]
                    for row in connection.execute("PRAGMA table_info(records)")
                ]
                kind = connection.execute(
                    "SELECT type FROM sqlite_master WHERE name = 'records'"
                ).fetchone()[0]
            finally:
                connection.close()
            # The view is the flat table's shape, column for column and in the
            # same order, so a reader unpacking a row positionally is unaffected.
            self.assertEqual(columns, columns_after)
            self.assertEqual(before, after)
            self.assertEqual(kind, "view")
            self.assertLess(report["counts"]["distinct_payloads"], len(before))

    def test_compaction_preserves_the_unique_constraint_and_its_autoindex(
        self,
    ) -> None:
        # PRAGMA table_info reports neither table-level UNIQUE nor foreign
        # keys, so a base table generated from it would silently drop both --
        # and with the constraint goes the autoindex two verifiers search on.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._indexable_session(session)
            compact(session)
            connection = sqlite3.connect(session / "capture.sqlite")
            try:
                unique = [
                    row
                    for row in connection.execute(
                        f"SELECT name, \"unique\", origin FROM "
                        f"pragma_index_list('{EVENT_TABLE}')"
                    )
                    if row[2] == "u"
                ]
                parents = {
                    row[0]
                    for row in connection.execute(
                        f"SELECT \"table\" FROM "
                        f"pragma_foreign_key_list('{EVENT_TABLE}')"
                    )
                }
                targets = {
                    row[0]: row[1]
                    for row in connection.execute(
                        "SELECT name, tbl_name FROM sqlite_master "
                        "WHERE type = 'index' AND name LIKE 'records_%'"
                    )
                }
            finally:
                connection.close()
            self.assertEqual(len(unique), 1)
            self.assertIn("streams", parents)
            self.assertIn("payloads", parents)
            self.assertTrue(targets)
            for name, table in targets.items():
                self.assertEqual(table, EVENT_TABLE, name)

    def test_event_table_ddl_refuses_a_database_with_no_payload_column(
        self,
    ) -> None:
        with self.assertRaises(ValueError):
            event_table_ddl(
                "CREATE TABLE records (\n    id INTEGER PRIMARY KEY\n)"
            )

    def test_compaction_assigns_payload_ids_in_first_use_order(self) -> None:
        # Two passes over one input must produce the same file: the digest of a
        # compacted database is recorded provenance, and an id assignment that
        # followed whatever scan the planner picked would move under it.
        digests = []
        for _ in range(2):
            with tempfile.TemporaryDirectory() as directory:
                session = Path(directory)
                self._indexable_session(session)
                compact(session)
                connection = sqlite3.connect(session / "capture.sqlite")
                try:
                    digests.append(
                        connection.execute(
                            "SELECT group_concat(id || ':' || "
                            "hex(sha256), '|') FROM (SELECT id, sha256 "
                            "FROM payloads ORDER BY id)"
                        ).fetchone()[0]
                    )
                    dense = connection.execute(
                        "SELECT min(id), max(id), count(*) FROM payloads"
                    ).fetchone()
                finally:
                    connection.close()
        self.assertEqual(digests[0], digests[1])
        self.assertEqual(dense[0], 1)
        self.assertEqual(dense[1], dense[2])

    def test_every_verifier_reads_a_compacted_database_the_same_way(
        self,
    ) -> None:
        # The one test that catches a verifier silently changing its answer
        # because `records` became a view. Both sessions carry the same name so
        # the reports differ in nothing but what the pass actually changed.
        with tempfile.TemporaryDirectory() as directory:
            flat = Path(directory) / "flat" / "run"
            compacted = Path(directory) / "compacted" / "run"
            flat.parent.mkdir()
            flat.mkdir()
            self._indexable_session(flat)
            compacted.parent.mkdir()
            shutil.copytree(flat, compacted)
            compact(compacted)

            def scrub(value: object, root: Path) -> object:
                if isinstance(value, dict):
                    return {
                        key: scrub(item, root)
                        for key, item in value.items()
                        if not key.startswith("compact_")
                    }
                if isinstance(value, list):
                    return [scrub(item, root) for item in value]
                if isinstance(value, str):
                    for form in (str(root), root.as_posix()):
                        value = value.replace(form, "SESSION")
                return value

            for name, run in integrity_tools(False).items():
                left = scrub(run(flat), flat)
                right = scrub(run(compacted), compacted)
                if name == "calibrate":
                    # The file size is what this pass changes; everything the
                    # calibration says about the capture must not move.
                    for report in (left, right):
                        report.pop("database", None)
                self.assertEqual(
                    json.dumps(left, sort_keys=True, default=repr),
                    json.dumps(right, sort_keys=True, default=repr),
                    f"{name} reads a compacted database differently",
                )

    def test_the_sequence_counter_still_covers_events_through_a_view(
        self,
    ) -> None:
        # A catalogue query asking only for tables would drop `records` from
        # the union and report a dense counter as full of holes, turning
        # CAP1.2's loss proof into a false alarm.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._indexable_session(session)
            compact(session)
            connection = sqlite3.connect(session / "capture.sqlite")
            try:
                union = calibrate_theatre_capture.sequence_union(connection)
                continuity = calibrate_theatre_capture.sequence_continuity(
                    connection
                )
            finally:
                connection.close()
            self.assertIn("FROM records", union)
            self.assertTrue(continuity["dense"])
            self.assertEqual(continuity["gaps"], [])

    def test_the_span_resolver_runs_on_a_compacted_database(self) -> None:
        # Its record_span_sets foreign key names `records`, which compaction
        # turns into a view. SQLite accepts the CREATE either way and refuses
        # only at the first insert -- after the previous dictionary is dropped.
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._span_session(session, self._clean_contributions())
            resolve_spans(session)
            compact(session)
            resolve_spans(session, rewrite=True)
            connection = sqlite3.connect(session / "capture.sqlite")
            try:
                parents = {
                    row[0]
                    for row in connection.execute(
                        "SELECT \"table\" FROM "
                        "pragma_foreign_key_list('record_span_sets')"
                    )
                }
                self.assertEqual(
                    connection.execute(
                        "PRAGMA foreign_key_check"
                    ).fetchall(),
                    [],
                )
            finally:
                connection.close()
            self.assertIn(EVENT_TABLE, parents)

    def test_the_index_verifier_judges_a_spine_it_can_read(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._indexable_session(session)
            unindexed = verify_index(session)
            self.assertFalse(unindexed["verdict"]["judgeable"])
            index_capture(session, rollup=True, join_source=False)
            report = verify_index(session)
            self.assertTrue(report["verdict"]["judgeable"])
            self.assertTrue(report["verdict"]["spine_joined"])
            self.assertTrue(report["verdict"]["payloads_intact"])
            self.assertFalse(report["support"]["carries_payload_store"])
            compact(session)
            compacted = verify_index(session)
            self.assertTrue(compacted["support"]["carries_payload_store"])
            self.assertTrue(compacted["verdict"]["payloads_intact"])
            self.assertEqual(
                compacted["payloads"]["dangling_payload_ids"], 0
            )
            self.assertEqual(
                compacted["payloads"]["payloads_not_matching_their_digest"], 0
            )


BANK_CHECKSUM = 0xABCD
BANK_MODEL = "models/bank.mdl"
BANK_LABELS = ("stand", "walk", "idle", "turn")
# Every sequence carries an activity literal. A descriptor whose activity
# index is zero reads the label index field as a string, so a fixture that
# leaves it unset compares garbage against the manifest.
BANK_ACTIVITIES = ("ACT_STAND", "ACT_WALK", "ACT_IDLE", "ACT_TURN")
FIRED_SEQUENCE = 2
FIRED_ANIMATION = 0


class SourceJoinTests(unittest.TestCase):
    """CAP4.1: a fired identity against the install, the export and CAP0.5.

    Every arm is synthetic. The install is a callable over an in-memory image,
    the export manifest and the inventory are real files in a temporary tree,
    and the model image is the same v2531 fixture the rest of this suite uses,
    so nothing here depends on a game install.
    """

    def _bank(self, **kwargs: object) -> bytes:
        options: dict[str, object] = {
            "labels": BANK_LABELS,
            "activities": BANK_ACTIVITIES,
            "sequences": len(BANK_LABELS),
        }
        options.update(kwargs)
        return model_image(BANK_CHECKSUM, BANK_MODEL, **options)

    def _contributions(
        self, *, sequence_index: int = FIRED_SEQUENCE,
        animation_index: int = FIRED_ANIMATION,
    ) -> bytes:
        return contribution_record(
            b"SEQP", 8, 103, sequence_index=sequence_index
        ) + contribution_record(
            b"ANIM", 9, 104, animation_index=animation_index
        )

    def _session(
        self,
        session: Path,
        *,
        image: bytes | None = None,
        contributions: bytes | None = None,
    ) -> None:
        write_session(
            session,
            pose_records=pose_record(
                1, 100, "models/test.mdl", generation=1, client_entity=0x1FFC
            ),
            animation_records=(
                bracket_record(
                    b"PBLD", 2, 100, 101, generation=1, client_entity=0x1FFC
                )
                + animation_record(
                    b"BASE", 3, 102, client_entity=0x1FF8, generation=1
                )
            ),
            census_records=(
                observation_record(4, 88, "models/test.mdl")
                + image_record(5, 89, model_image(0x3000, "models/test.mdl"))
                + observation_record(
                    6, 90, BANK_MODEL,
                    studio_hdr=CONTRIBUTION_OWNER_HDR, checksum=BANK_CHECKSUM,
                )
                + image_record(
                    7, 91, self._bank() if image is None else image,
                    studio_hdr=CONTRIBUTION_OWNER_HDR, checksum=BANK_CHECKSUM,
                )
            ),
            contribution_records=(
                self._contributions() if contributions is None else contributions
            ),
            done=(
                "complete=1\nqueued=8\nwritten=8\ndropped=0\nqueue_peak=12\n"
                "unbracketed=0\nbracket_overflow=0\n"
                "contribution_sequences=1\ncontribution_animations=1\n"
                "contribution_faults=0\ncontribution_overflow=0\n"
                "contribution_unscoped=0\n"
            ),
        )
        finalize(session)

    def _reader(self, installed: bytes | None):
        """A patch-first install carrying one model, or carrying nothing."""

        def read(key: str) -> dict[str, object] | None:
            if installed is None or key != BANK_MODEL:
                return None
            return {"data": installed, "layer": "patch", "path": "/patch/" + key}

        return read

    def _export(
        self,
        root: Path,
        clips: dict[str, object] | None = None,
        blends: dict[str, object] | None = None,
    ) -> Path:
        """A manifest whose one bank names the owner model by its install key.

        `blends` writes the bank's blend sidecar as the exporter does, which is
        what the animation arm resolves a non-base cell through.
        """
        if clips is None:
            clips = {
                BANK_LABELS[FIRED_SEQUENCE]: {
                    "activity": BANK_ACTIVITIES[FIRED_SEQUENCE],
                    "weight": 0, "flags": 0, "frames": 3, "fps": 30.0,
                }
            }
        (root / "npc").mkdir(parents=True, exist_ok=True)
        bank: dict[str, object] = {
            "glb": "banks/bank.glb", "model": BANK_MODEL, "clips": clips,
        }
        if blends is not None:
            (root / "npc" / "blends").mkdir(parents=True, exist_ok=True)
            (root / "npc" / "blends" / "bank.json").write_text(
                json.dumps({"stem": "bank", "model": BANK_MODEL, "grids": blends}),
                encoding="utf-8",
            )
            bank["blends"] = "blends/bank.json"
        (root / "npc" / "npc_manifest.json").write_text(
            json.dumps(
                {
                    "manifest_version": 4,
                    "npcs": {},
                    "banks": {"bank": bank},
                    "animated_props": {},
                    "cinematics": {},
                }
            ),
            encoding="utf-8",
        )
        return root

    def _inventory(
        self, root: Path, image: bytes, *, digest: str | None = None,
        cells: list[dict[str, int]] | None = None,
    ) -> Path:
        sequence_offset = CONTRIBUTION_SEQ_INDEX_OFF + SEQ_DESC_STRIDE * FIRED_SEQUENCE
        animation_offset = CONTRIBUTION_ANIM_INDEX_OFF + 72 * FIRED_ANIMATION
        sequence_bytes = image[sequence_offset : sequence_offset + SEQ_DESC_STRIDE]
        animation_bytes = image[animation_offset : animation_offset + 72]
        root.mkdir(parents=True, exist_ok=True)
        (root / "manifest.json").write_text(
            json.dumps({"tool": {"git": {"commit": "def", "dirty": False}}}),
            encoding="utf-8",
        )
        (root / "owners.jsonl").write_text(
            json.dumps(
                {
                    "owner_model": BANK_MODEL,
                    "model_sha256": hashlib.sha256(image).hexdigest(),
                }
            )
            + "\n",
            encoding="utf-8",
        )
        (root / "sequences.jsonl").write_text(
            json.dumps(
                {
                    "identity": f"{BANK_MODEL}#sequence:{FIRED_SEQUENCE}",
                    "owner_model": BANK_MODEL,
                    "sequence_index": FIRED_SEQUENCE,
                    "label": BANK_LABELS[FIRED_SEQUENCE],
                    "source_span": {
                        "offset": sequence_offset,
                        "length": SEQ_DESC_STRIDE,
                    },
                    "descriptor_sha256": (
                        digest or hashlib.sha256(sequence_bytes).hexdigest()
                    ),
                    "active_blend_cells": (
                        [{"row": 0, "column": 0,
                          "animation_index": FIRED_ANIMATION}]
                        if cells is None
                        else cells
                    ),
                    "unknown_tail_hex": "00ff",
                }
            )
            + "\n",
            encoding="utf-8",
        )
        (root / "animations.jsonl").write_text(
            json.dumps(
                {
                    "identity": f"{BANK_MODEL}#animation:{FIRED_ANIMATION}",
                    "owner_model": BANK_MODEL,
                    "animation_index": FIRED_ANIMATION,
                    "source_span": {"offset": animation_offset, "length": 72},
                    "descriptor_sha256": hashlib.sha256(
                        animation_bytes
                    ).hexdigest(),
                }
            )
            + "\n",
            encoding="utf-8",
        )
        return root

    def _join(
        self, directory: str, *, image: bytes | None = None,
        contributions: bytes | None = None, installed: bytes | None = -1,
        export: bool = True, inventory: bool = True,
        clips: dict[str, object] | None = None,
        digest: str | None = None, cells: list[dict[str, int]] | None = None,
        blends: dict[str, object] | None = None,
    ) -> dict[str, object]:
        root = Path(directory)
        session = root / "session"
        session.mkdir()
        captured = self._bank() if image is None else image
        self._session(session, image=captured, contributions=contributions)
        source = captured if installed == -1 else installed
        return verify_join(
            session,
            export_root=(
                self._export(root / "export", clips, blends) if export else None
            ),
            inventory=(
                self._inventory(root / "inv", source or captured,
                                digest=digest, cells=cells)
                if inventory
                else root / "absent"
            ),
            install_reader=self._reader(source),
            vtmb_root="/synthetic",
        )

    # ---- arm A: the patch-first installed bytes ----

    def test_a_fired_identity_resolves_to_the_installed_descriptor(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._join(directory)
            installed = report["install"]
            self.assertTrue(installed["available"])
            self.assertEqual(installed["owners"], 1)
            self.assertEqual(installed["owners_resolved"], 1)
            self.assertEqual(installed["joined_identities"], 2)
            self.assertEqual(installed["counts"]["install_path_absent"], 0)
            self.assertEqual(
                installed["counts"]["descriptor_offset_disagrees"], 0
            )
            self.assertEqual(installed["unresolved_identities"], [])
            self.assertEqual(installed["owner_entries"][0]["layer"], "patch")
            self.assertTrue(report["verdict"]["sources_joined"])

    def test_coverage_is_reported_by_identity_and_by_multiplicity(self) -> None:
        """A one-record identity and a many-record one are one row and two
        orders of magnitude apart, so both denominators have to be carried."""
        with tempfile.TemporaryDirectory() as directory:
            report = self._join(
                directory,
                contributions=(
                    contribution_record(
                        b"SEQP", 8, 103, sequence_index=FIRED_SEQUENCE
                    )
                    + b"".join(
                        contribution_record(
                            b"ANIM", 9 + index, 104 + index,
                            animation_index=FIRED_ANIMATION,
                        )
                        for index in range(5)
                    )
                ),
            )
            self.assertEqual(report["corpus"]["identities"], 2)
            self.assertEqual(report["corpus"]["records"], 6)
            self.assertEqual(report["corpus"]["sequence"]["records"], 1)
            self.assertEqual(report["corpus"]["animation"]["records"], 5)
            self.assertEqual(report["install"]["joined_identities"], 2)
            self.assertEqual(report["install"]["joined_records"], 6)

    def test_an_install_without_the_path_is_a_defect(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._join(directory, installed=None, inventory=False)
            self.assertEqual(
                report["install"]["counts"]["install_path_absent"], 2
            )
            self.assertEqual(
                report["install"]["records_by_count"]["install_path_absent"], 2
            )
            self.assertIn(
                "install_path_absent", report["verdict"]["defects"]
            )
            self.assertFalse(report["verdict"]["sources_joined"])

    def test_an_installed_file_carrying_another_checksum_is_a_defect(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            other = bytearray(self._bank())
            struct.pack_into("<I", other, 8, 0x1234)
            report = self._join(directory, installed=bytes(other), inventory=False)
            self.assertEqual(
                report["install"]["counts"]["installed_checksum_differs"], 2
            )
            self.assertFalse(report["verdict"]["sources_joined"])

    def test_an_index_outside_the_installed_count_is_a_defect(self) -> None:
        """The count is read from the installed file, not from the capture."""
        with tempfile.TemporaryDirectory() as directory:
            narrowed = bytearray(self._bank())
            struct.pack_into("<i", narrowed, 272, FIRED_SEQUENCE)
            report = self._join(directory, installed=bytes(narrowed), inventory=False)
            self.assertEqual(
                report["install"]["counts"]["index_outside_declared_count"], 1
            )
            self.assertFalse(report["verdict"]["sources_joined"])

    def test_a_captured_offset_the_install_does_not_place_is_a_defect(self) -> None:
        """The falsifiable check: CAP2.4 ran this predicate against the image
        the probe copied, and running it against the installed file is what
        makes agreement evidence rather than one reader agreeing with itself."""
        with tempfile.TemporaryDirectory() as directory:
            moved = bytearray(self._bank())
            struct.pack_into(
                "<i", moved, 276, CONTRIBUTION_SEQ_INDEX_OFF + SEQ_DESC_STRIDE
            )
            report = self._join(directory, installed=bytes(moved), inventory=False)
            self.assertEqual(
                report["install"]["counts"]["descriptor_offset_disagrees"], 1
            )
            entry = next(
                item
                for item in report["install"]["unresolved_identities"]
                if item["fault"] == "descriptor_offset_disagrees"
            )
            self.assertNotEqual(
                entry["image_offset"], entry["expected_offset"]
            )
            self.assertFalse(report["verdict"]["sources_joined"])

    def test_a_descriptor_differing_only_at_0xc_is_accounted(self) -> None:
        """The loader rewrites StudioSeqDesc+0xc in place, so a captured
        descriptor differing there and nowhere else is that fixup."""
        with tempfile.TemporaryDirectory() as directory:
            captured = bytearray(self._bank())
            base = CONTRIBUTION_SEQ_INDEX_OFF + SEQ_DESC_STRIDE * FIRED_SEQUENCE
            struct.pack_into("<i", captured, base + 0x0C, 17)
            report = self._join(
                directory, image=bytes(captured), installed=self._bank()
            )
            self.assertEqual(
                report["install"]["counts"]["descriptor_differs_only_at_0xc"], 1
            )
            self.assertEqual(
                report["install"]["counts"]["descriptor_differs_elsewhere"], 0
            )
            self.assertIn(
                "descriptor_differs_only_at_0xc", report["verdict"]["accounted"]
            )
            self.assertTrue(report["verdict"]["sources_joined"])

    def test_a_descriptor_differing_elsewhere_is_a_defect(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            captured = bytearray(self._bank())
            base = CONTRIBUTION_SEQ_INDEX_OFF + SEQ_DESC_STRIDE * FIRED_SEQUENCE
            struct.pack_into("<i", captured, base + 0x20, 9)
            report = self._join(
                directory, image=bytes(captured), installed=self._bank()
            )
            self.assertEqual(
                report["install"]["counts"]["descriptor_differs_elsewhere"], 1
            )
            self.assertFalse(report["verdict"]["sources_joined"])

    def test_an_unreadable_install_does_not_join_and_does_not_crash(self) -> None:
        """An absent install is an answer, but it is a failing one: resolving
        the installed bytes is what this task is."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            session = root / "session"
            session.mkdir()
            self._session(session)
            original = verify_source_join.patch_first_reader

            def absent() -> object:
                raise FileNotFoundError("no install root configured")

            verify_source_join.patch_first_reader = absent
            try:
                report = verify_join(session, inventory=root / "absent")
            finally:
                verify_source_join.patch_first_reader = original
            self.assertFalse(report["install"]["available"])
            self.assertIn("FileNotFoundError", report["install"]["reason"])
            self.assertFalse(report["verdict"]["sources_joined"])

    # ---- arm B: the current exported animation ----

    def test_a_fired_sequence_resolves_to_an_exported_clip(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._join(directory)
            export = report["export"]
            self.assertTrue(export["available"])
            self.assertEqual(export["joined_identities"], 2)
            sequence = next(
                item for item in export["outcomes"] if item["kind"] == "SEQP"
            )
            self.assertEqual(sequence["outcome"], "joined")
            self.assertEqual(sequence["label"], BANK_LABELS[FIRED_SEQUENCE])
            self.assertEqual(sequence["clip"]["stem"], "bank:bank")

    def test_an_owner_the_export_never_seeded_is_accounted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            session = root / "session"
            session.mkdir()
            self._session(session)
            (root / "export" / "npc").mkdir(parents=True)
            (root / "export" / "npc" / "npc_manifest.json").write_text(
                json.dumps({"manifest_version": 4, "npcs": {}, "banks": {}}),
                encoding="utf-8",
            )
            report = verify_join(
                session,
                export_root=root / "export",
                inventory=root / "absent",
                install_reader=self._reader(self._bank()),
            )
            self.assertEqual(
                report["export"]["counts"]["export_has_no_such_owner"], 2
            )
            self.assertIn(
                "export_has_no_such_owner", report["verdict"]["accounted"]
            )
            self.assertTrue(report["verdict"]["sources_joined"])

    def test_a_label_a_lower_index_already_owns_is_accounted(self) -> None:
        """`local_sequences` dedupes by lowercased label, first wins, and drops
        the index, so a later sequence sharing a label is unexportable."""
        with tempfile.TemporaryDirectory() as directory:
            image = self._bank(labels=("stand", "walk", "idle", "IDLE"))
            report = self._join(
                directory,
                image=image,
                installed=image,
                contributions=self._contributions(sequence_index=3),
                inventory=False,
            )
            outcome = next(
                item
                for item in report["export"]["outcomes"]
                if item["kind"] == "SEQP"
            )
            self.assertEqual(
                outcome["outcome"], "label_owned_by_another_sequence_index"
            )
            self.assertEqual(outcome["owned_by"], FIRED_SEQUENCE)
            self.assertTrue(report["verdict"]["sources_joined"])

    def test_a_base_cell_outside_the_animation_count_is_accounted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            image = self._bank(base_cells=(0, 0, 99, 0))
            report = self._join(
                directory, image=image, installed=image, inventory=False
            )
            outcome = next(
                item
                for item in report["export"]["outcomes"]
                if item["kind"] == "SEQP"
            )
            self.assertEqual(outcome["outcome"], "base_cell_out_of_range")
            self.assertEqual(outcome["declared_animations"], CONTRIBUTION_NUM_ANIM)
            self.assertTrue(report["verdict"]["sources_joined"])

    def test_a_fired_cell_no_sidecar_names_is_accounted(self) -> None:
        """An export whose bank authors no blend sidecar reaches a cell only if
        it is some fired sequence's base cell, so a fired non-base cell has no
        clip and is accounted rather than reported as an unexplained absence."""
        with tempfile.TemporaryDirectory() as directory:
            report = self._join(
                directory,
                contributions=self._contributions(animation_index=5),
                inventory=False,
            )
            outcome = next(
                item
                for item in report["export"]["outcomes"]
                if item["kind"] == "ANIM"
            )
            self.assertEqual(outcome["outcome"], "blend_cell_not_exported")
            self.assertIn(
                "blend_cell_not_exported", report["verdict"]["accounted"]
            )
            self.assertTrue(report["verdict"]["sources_joined"])

    def test_a_fired_cell_the_blend_sidecar_names_joins_to_its_clip(self) -> None:
        """CAP5.3's export shortfall, closed and measured from the export.

        The same fired non-base cell joins once the bank ships a blend sidecar
        naming a clip for its animation index. The bridge is that index rather
        than a replay of the exporter's rules, so the outcome carries the cell's
        own axis position and clip name and is a claim about what was written.
        """
        with tempfile.TemporaryDirectory() as directory:
            report = self._join(
                directory,
                contributions=self._contributions(animation_index=5),
                inventory=False,
                clips={
                    BANK_LABELS[FIRED_SEQUENCE]: {
                        "activity": BANK_ACTIVITIES[FIRED_SEQUENCE],
                        "weight": 0, "flags": 0, "frames": 3, "fps": 30.0,
                    },
                    "turn": {"activity": "", "weight": 0, "flags": 0,
                             "frames": 2, "fps": 30.0},
                },
                blends={
                    BANK_LABELS[FIRED_SEQUENCE]: {
                        "groupsize": [2, 1],
                        "cells": [
                            {"axis": [0, 0], "anim": FIRED_ANIMATION,
                             "clip": BANK_LABELS[FIRED_SEQUENCE]},
                            {"axis": [1, 0], "anim": 5, "clip": "turn"},
                        ],
                    }
                },
            )
            outcome = next(
                item
                for item in report["export"]["outcomes"]
                if item["kind"] == "ANIM"
            )
            self.assertEqual(outcome["outcome"], "joined")
            self.assertEqual(outcome["blend_cell"]["clip"], "turn")
            self.assertEqual(outcome["blend_cell"]["axis"], [1, 0])
            self.assertEqual(
                outcome["blend_cell"]["label"], BANK_LABELS[FIRED_SEQUENCE]
            )
            self.assertEqual(
                report["export"]["counts"].get("blend_cell_not_exported", 0), 0
            )
            self.assertTrue(report["verdict"]["sources_joined"])

    def test_a_cell_naming_a_clip_the_glb_lacks_is_not_a_join(self) -> None:
        """The sidecar promises a clip; the manifest says what actually baked.

        A cell whose clip is absent from the owning stem's clip table is not
        evidence the animation reached the export, so it falls back to the
        base-cell arm rather than joining on a name nothing carries.
        """
        with tempfile.TemporaryDirectory() as directory:
            report = self._join(
                directory,
                contributions=self._contributions(animation_index=5),
                inventory=False,
                blends={
                    BANK_LABELS[FIRED_SEQUENCE]: {
                        "groupsize": [2, 1],
                        "cells": [
                            {"axis": [0, 0], "anim": FIRED_ANIMATION,
                             "clip": BANK_LABELS[FIRED_SEQUENCE]},
                            {"axis": [1, 0], "anim": 5, "clip": "never_baked"},
                        ],
                    }
                },
            )
            outcome = next(
                item
                for item in report["export"]["outcomes"]
                if item["kind"] == "ANIM"
            )
            self.assertEqual(outcome["outcome"], "blend_cell_not_exported")
            self.assertTrue(report["verdict"]["sources_joined"])

    def test_exported_metadata_disagreeing_with_the_install_is_a_defect(self) -> None:
        """The manifest and this run decoded the same descriptor, so the fields
        they both carry have to agree or one of them read the wrong bytes."""
        with tempfile.TemporaryDirectory() as directory:
            report = self._join(
                directory,
                inventory=False,
                clips={
                    BANK_LABELS[FIRED_SEQUENCE]: {
                        "activity": "ACT_WALK", "weight": 7, "flags": 0,
                        "frames": 3, "fps": 30.0,
                    }
                },
            )
            self.assertEqual(
                report["export"]["counts"]["export_clip_meta_disagrees"], 1
            )
            outcome = next(
                item
                for item in report["export"]["outcomes"]
                if item["outcome"] == "export_clip_meta_disagrees"
            )
            self.assertIn("activity", outcome["disagreements"])
            self.assertIn("weight", outcome["disagreements"])
            self.assertFalse(report["verdict"]["sources_joined"])

    # ---- arm C: the CAP0.5 inventory ----

    def test_the_inventory_descriptor_digest_agrees_with_the_install(self) -> None:
        """Two readers of one install, neither importing the other's decode."""
        with tempfile.TemporaryDirectory() as directory:
            report = self._join(directory)
            inventory = report["inventory"]
            self.assertTrue(inventory["available"])
            self.assertEqual(inventory["joined_identities"], 2)
            self.assertEqual(inventory["inventory_tool_git"], "def")
            self.assertEqual(
                inventory["unknown_descriptor_bytes"][0]["unknown_hex"], "00ff"
            )
            self.assertTrue(report["verdict"]["sources_joined"])

    def test_an_inventory_digest_that_disagrees_is_a_defect(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._join(directory, digest="0" * 64)
            self.assertEqual(
                report["inventory"]["counts"][
                    "inventory_descriptor_digest_disagrees"
                ],
                1,
            )
            self.assertFalse(report["verdict"]["sources_joined"])

    def test_a_fired_cell_the_inventory_never_declared_is_a_defect(self) -> None:
        """The inventory declares which cells a sequence can fire; the capture
        witnessed which ones did. A witnessed cell outside the declaration
        means one of the two is wrong about the same bytes."""
        with tempfile.TemporaryDirectory() as directory:
            report = self._join(
                directory,
                cells=[{"row": 0, "column": 0, "animation_index": 4}],
            )
            self.assertEqual(
                report["inventory"]["counts"]["fired_cell_not_declared"], 1
            )
            self.assertFalse(report["verdict"]["sources_joined"])

    def test_an_owner_outside_the_inventory_bound_is_accounted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            session = root / "session"
            session.mkdir()
            self._session(session)
            empty = root / "inv"
            empty.mkdir()
            (empty / "manifest.json").write_text("{}", encoding="utf-8")
            (empty / "owners.jsonl").write_text("", encoding="utf-8")
            report = verify_join(
                session,
                export_root=self._export(root / "export"),
                inventory=empty,
                install_reader=self._reader(self._bank()),
            )
            self.assertEqual(
                report["inventory"]["counts"][
                    "inventory_does_not_cover_this_owner"
                ],
                2,
            )
            self.assertIn(
                "inventory_does_not_cover_this_owner",
                report["verdict"]["accounted"],
            )
            self.assertTrue(report["verdict"]["sources_joined"])

    # ---- shape, provenance and declaration ----

    def test_an_absent_export_is_zero_coverage_and_not_a_failure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            session = root / "session"
            session.mkdir()
            self._session(session)
            report = verify_join(
                session,
                export_root=root / "absent",
                inventory=root / "absent",
                install_reader=self._reader(self._bank()),
            )
            self.assertFalse(report["export"]["available"])
            self.assertFalse(report["inventory"]["available"])
            self.assertTrue(report["verdict"]["sources_joined"])

    def test_every_declared_population_is_a_defect_or_is_accounted(self) -> None:
        """A population no declaration covers must not read as coverage."""
        self.assertEqual(
            set(verify_source_join.DEFECTS) & set(verify_source_join.ACCOUNTED),
            set(),
        )
        with tempfile.TemporaryDirectory() as directory:
            report = self._join(directory)
            self.assertEqual(report["verdict"]["unclassified"], {})
            for arm in ("install", "export", "inventory"):
                for name, value in (report[arm].get("counts") or {}).items():
                    if not value:
                        continue
                    self.assertIn(
                        name,
                        set(verify_source_join.DEFECTS)
                        | set(verify_source_join.ACCOUNTED),
                        f"{arm}.{name} is neither declared nor accounted",
                    )

    def test_the_report_records_the_provenance_of_every_arm(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._join(directory)
            owner = report["install"]["owner_entries"][0]
            self.assertEqual(len(owner["installed_sha256"]), 64)
            self.assertEqual(owner["install_key"], BANK_MODEL)
            self.assertEqual(report["identity"]["vtmb_root"], "/synthetic")
            self.assertEqual(len(report["export"]["sha256"]), 64)
            self.assertEqual(len(report["inventory"]["manifest_sha256"]), 64)
            self.assertEqual(report["identity"]["map"], "sp_theatre")

    def test_the_installed_descriptor_is_preserved_as_raw_bytes(self) -> None:
        """The evidence gate asks for the relevant input spans as raw bytes, so
        the report answers without the install it was joined against."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            session = root / "session"
            session.mkdir()
            image = self._bank()
            self._session(session, image=image)
            report = verify_join(
                session,
                export_root=root / "absent",
                inventory=root / "absent",
                install_reader=self._reader(image),
            )
            base = CONTRIBUTION_SEQ_INDEX_OFF + SEQ_DESC_STRIDE * FIRED_SEQUENCE
            expected = image[base : base + SEQ_DESC_STRIDE]
            entry = next(
                item
                for item in report["install"]["resolved_identities"]
                if item["kind"] == "SEQP"
            )
            self.assertEqual(entry["descriptor_hex"], expected.hex())
            self.assertEqual(
                entry["descriptor"]["label"], BANK_LABELS[FIRED_SEQUENCE]
            )
            self.assertEqual(
                entry["descriptor_sha256"],
                hashlib.sha256(expected).hexdigest(),
            )

    def test_install_key_normalizes_a_shipped_model_name(self) -> None:
        """Guards the helper the census and this tool now share."""
        self.assertEqual(
            install_key("/character/shared/male/Stances.mdl"),
            "models/character/shared/male/stances.mdl",
        )
        self.assertEqual(
            install_key("character\\pc\\male\\body.mdl"),
            "models/character/pc/male/body.mdl",
        )
        self.assertEqual(install_key(BANK_MODEL), BANK_MODEL)

    def test_two_sessions_are_reported_side_by_side(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            first = self._join(directory)
        with tempfile.TemporaryDirectory() as directory:
            second = self._join(directory)
        comparison = verify_source_join.compare([first, second])
        self.assertEqual(len(comparison["identities"]), 2)
        self.assertIn("side by side", comparison["statement"])


GESTURE_EVENT_TYPE = 6
SEQUENCE_EVENT = 7


class DecoderCoverageTests(unittest.TestCase):
    """CAP4.2: the harness that measures what the exporter's decoder reads.

    Nothing here decides what the decoder ought to read. It checks that every
    route the decoder takes to the image is measured, that a route the harness
    does not model raises instead of returning bytes unrecorded, and that the
    instrumentation does not survive the block it was installed for.
    """

    def test_struct_shim_records_every_offset_and_is_restored(self) -> None:
        from elysium_pipeline.formats import mdl_skel

        original = mdl_skel.struct
        image = model_image(0xABCD, BANK_MODEL)
        with instrumented(mdl_skel, image) as (data, recorder):
            self.assertIsNot(mdl_skel.struct, original)
            mdl_skel.read_bones(data)
        self.assertIs(mdl_skel.struct, original)
        self.assertGreater(recorder.reads, 0)
        self.assertEqual(recorder.outside, 0)

    def test_bone_reads_are_exactly_the_fields_read_bones_names(self) -> None:
        """The measured set is checked against the offsets, not against itself.

        `read_bones` reads eight fields of each 160-byte record plus the name
        string each one points at, and nothing else. Computing that set here
        from the fixture's own layout is what makes the harness evidence rather
        than a recording of whatever happened.
        """
        from elysium_pipeline.formats import mdl_skel

        bones = (("Bip01", -1, (0.0, 0.0, 0.0)), ("Bip01 Spine", 0, (1.0, 2.0, 3.0)))
        image = model_image(0xABCD, BANK_MODEL, bones=bones)
        with instrumented(mdl_skel, image) as (data, recorder):
            mdl_skel.read_bones(data)
        # NumBones@240 and BoneIndex@244, then each record's own fields.
        expected: set[int] = set(range(240, 248))
        for index, (name, _, _) in enumerate(bones):
            base = BONE_ARRAY_OFFSET + BONE_STRIDE * index
            for low, size in (
                (0, 4), (4, 4), (32, 12), (44, 16), (60, 12), (72, 16),
                (88, 48), (136, 4),
            ):
                expected.update(range(base + low, base + low + size))
            start = base + struct.unpack_from("<i", image, base)[0]
            # The terminator is read too: the scan stops on it.
            expected.update(range(start, start + len(name) + 1))
        covered = {
            offset
            for low, high in _intervals(recorder.coverage().bits, len(image))
            for offset in range(low, high)
        }
        self.assertEqual(covered, expected)

    def test_rle_run_headers_and_dynamic_key_widths_are_measured(self) -> None:
        """`_rle_channel` reaches the image by two routes and both are recorded.

        The run header is raw indexing and the keys are a format string built
        from it, so a harness that watched only one of them would report a
        track our decoder half-reads.
        """
        from elysium_pipeline.formats import mdl_skel

        image = model_image(0xABCD, BANK_MODEL)
        block = animation_block_offset(6)
        with instrumented(mdl_skel, image) as (data, recorder):
            bones = mdl_skel.read_bones(data)
        with instrumented(mdl_skel, image) as (data, recorder):
            descriptor = CONTRIBUTION_ANIM_INDEX_OFF + 72 * 6
            mdl_skel.read_anim(data, bones, descriptor, DEFAULT_CLIPS[6].numframes)
        covered = _intervals(recorder.coverage().bits, len(image))
        track = block + struct.unpack_from("<i", image, block + 4 + 3 * 4)[0]
        # Three runs of two keys: six bytes each, read end to end.
        self.assertTrue(
            any(low <= track and high >= track + 18 for low, high in covered),
            covered,
        )

    def test_an_unmodelled_route_raises_rather_than_reading(self) -> None:
        from elysium_pipeline.formats import mdl_skel

        with instrumented(mdl_skel, b"\x00" * 64) as (data, _):
            with self.assertRaises(UnmeasuredRead):
                data.decode("ascii")
            with self.assertRaises(UnmeasuredRead):
                data[::2]
            with self.assertRaises(UnmeasuredRead):
                mdl_skel.struct.unpack("<i", data)
            with self.assertRaises(UnmeasuredRead):
                mdl_skel.struct.iter_unpack

    def test_a_read_past_the_image_is_counted_not_recorded(self) -> None:
        from elysium_pipeline.formats import mdl_skel

        with instrumented(mdl_skel, b"\x00" * 8) as (data, recorder):
            with self.assertRaises(struct.error):
                mdl_skel.struct.unpack_from("<i", data, 6)
        # Counted rather than swallowed: a decoder running off the image is a
        # finding, and the offset it reached is not coverage of the image.
        self.assertEqual(recorder.outside, 1)
        self.assertEqual(recorder.trace, [])

    def test_the_glb_writer_reads_no_byte_read_anim_does_not(self) -> None:
        """Instrumenting the decoder measures the export path.

        `mdl_gltf._bake_animation` re-reads `animindex` and the seven channel
        offsets itself rather than taking them from `read_anim`. Both land
        inside what `read_anim` already read, so measuring `mdl_skel` alone
        measures what the exporter reads off an image.
        """
        from elysium_pipeline.formats import mdl_skel

        image = model_image(0xABCD, BANK_MODEL)
        descriptor = CONTRIBUTION_ANIM_INDEX_OFF + 72 * 6
        with instrumented(mdl_skel, image) as (data, recorder):
            bones = mdl_skel.read_bones(data)
            mdl_skel.read_anim(data, bones, descriptor, DEFAULT_CLIPS[6].numframes)
        covered = recorder.coverage().bits
        records = descriptor + struct.unpack_from("<i", image, descriptor + 48)[0]
        for offset, size in (
            (descriptor + 48, 4),
            *((records + bone * ANIM_RECORD_STRIDE + 4, 28) for bone in range(2)),
        ):
            for byte in range(offset, offset + size):
                self.assertTrue(
                    covered[byte >> 3] & (1 << (byte & 7)),
                    f"offset {byte} is read by the glb writer and not by read_anim",
                )


class ByteCoverageTests(unittest.TestCase):
    """CAP4.2: retail's consumed bytes against the current decoder's.

    The session is the same synthetic capture the span tasks use, resolved by
    the real `resolve_consumed_spans` pass, so the retail arm here is the one
    that runs on a real capture rather than a stand-in for it.
    """

    def _bank(self, **kwargs: object) -> bytes:
        return model_image(
            0xABCD,
            BANK_MODEL,
            labels=BANK_LABELS,
            activities=BANK_ACTIVITIES,
            sequences=len(BANK_LABELS),
            base_cells=(0, 4, 5, 6),
            **kwargs,
        )

    def _unreadable_grid_bank(self) -> bytes:
        """A bank whose sequence 1 declares extents its `numblends` denies.

        `groupsize[0] * groupsize[1] == numblends` holds on all 294 authored
        multi-blend sequences, so a descriptor where it does not is one the
        format does not explain and `read_grid` falls back to the base cell.
        That is the one range on this fixture retail reads and we do not, which
        is what the missing-list machinery below is exercised against.
        """
        return self._bank(
            grids={1: Grid((3, 1), {(0, 0): 4, (1, 0): 4, (2, 0): 6}, numblends=5)}
        )

    #: Where the two cells that grid fires land in the image. Axis 0 resolves to
    #: cell 1, so the evaluator takes cell 1 and the one after it — slots 16 and
    #: 32 down the fixed row stride, four bytes our fallback never reaches.
    UNREADABLE_GRID_CELLS = tuple(
        CONTRIBUTION_SEQ_INDEX_OFF + SEQ_DESC_STRIDE + 56 + slot * 2
        for slot in (1 * BLEND_ROW_STRIDE, 2 * BLEND_ROW_STRIDE)
    )

    def _unreadable_grid_contributions(self) -> bytes:
        return (
            contribution_record(
                b"SEQP", 8, 103, sequence_index=1, num_blends=5,
                group_size=(3, 1), blend_cell=(1, 0),
            )
            + contribution_record(b"ANIM", 9, 104, animation_index=4)
            + contribution_record(b"ANIM", 10, 105, animation_index=6)
        )

    def _session(
        self,
        session: Path,
        contributions: bytes | None = None,
        image: bytes | None = None,
    ) -> None:
        write_session(
            session,
            pose_records=pose_record(
                1, 100, "models/test.mdl", generation=1, client_entity=0x1FFC
            ),
            animation_records=(
                bracket_record(
                    b"PBLD", 2, 100, 101, generation=1, client_entity=0x1FFC
                )
                + animation_record(
                    b"BASE", 3, 102, client_entity=0x1FF8, generation=1
                )
            ),
            census_records=(
                observation_record(4, 88, "models/test.mdl")
                + image_record(5, 89, model_image(0x3000, "models/test.mdl"))
                + observation_record(
                    6, 90, BANK_MODEL,
                    studio_hdr=CONTRIBUTION_OWNER_HDR, checksum=0xABCD,
                )
                + image_record(
                    7, 91, self._bank() if image is None else image,
                    studio_hdr=CONTRIBUTION_OWNER_HDR, checksum=0xABCD,
                )
            ),
            contribution_records=(
                contributions
                if contributions is not None
                else (
                    contribution_record(b"SEQP", 8, 103, sequence_index=1)
                    + contribution_record(b"ANIM", 9, 104, animation_index=4)
                    + contribution_record(b"ANIM", 10, 105, animation_index=6)
                )
            ),
            done=(
                "complete=1\nqueued=8\nwritten=8\ndropped=0\nqueue_peak=12\n"
                "unbracketed=0\nbracket_overflow=0\n"
                "contribution_sequences=1\ncontribution_animations=2\n"
                "contribution_faults=0\ncontribution_overflow=0\n"
                "contribution_unscoped=0\n"
            ),
        )
        finalize(session)
        resolve_spans(session)

    def _report(self, directory: str, **kwargs: object) -> dict:
        session = Path(directory)
        self._session(session)
        return verify_coverage(session, **kwargs)

    def test_the_two_arms_describe_the_same_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory)
        verdict = report["verdict"]
        self.assertTrue(verdict["judgeable"], verdict)
        self.assertEqual(verdict["defects"], {})
        self.assertEqual(verdict["unclassified"], {})
        self.assertTrue(verdict["byte_coverage_compared"], verdict["statement"])
        self.assertTrue(report["retail"]["available"])
        self.assertTrue(report["decoder"]["available"])

    def test_the_rewalk_reproduces_the_stored_union(self) -> None:
        """The self-check that makes the difference below it trustworthy.

        The stored union came from one pass over the records; the re-walk comes
        from the dictionary that pass wrote. Agreeing byte for byte is what says
        neither drifted, and a disagreement invalidates every set derived from
        them.
        """
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory)
        retail = report["retail"]
        self.assertEqual(retail["counts"]["rewalk_union_differs_from_stored"], 0)
        self.assertEqual(retail["counts"]["retail_walk_faulted"], 0)
        self.assertEqual(retail["stored_bytes"], retail["rewalk_bytes"])
        self.assertGreater(retail["stored_bytes"], 0)

    def test_the_sets_partition_every_byte_of_every_owner_image(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory)
        totals = report["difference"]["totals"]
        self.assertEqual(
            totals["both_bytes"]
            + totals["retail_only_bytes"]
            + totals["ours_only_bytes"]
            + totals["neither_bytes"],
            totals["image_bytes"],
        )
        self.assertEqual(
            totals["retail_bytes"], totals["both_bytes"] + totals["retail_only_bytes"]
        )
        self.assertEqual(
            totals["ours_bytes"], totals["both_bytes"] + totals["ours_only_bytes"]
        )

    def test_a_missing_span_carries_the_bytes_nobody_read(self) -> None:
        """The report outlives the gitignored capture it was taken over.

        A range the decoder never reads is a claim about a value nobody looked
        at, so the value comes along: here the two cells the unreadable grid
        fired carry the animation indices retail resolved them to, which is what
        says the fallback dropped a real cell rather than the decode being wrong.
        """
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._session(
                session,
                self._unreadable_grid_contributions(),
                image=self._unreadable_grid_bank(),
            )
            report = verify_coverage(session)
        spans = report["difference"]["owners"][0]["retail_only_spans"]
        self.assertTrue(spans)
        cells = {
            int(start, 16): struct.unpack("<h", bytes.fromhex(body))[0]
            for start, _, body in spans
            if int(start, 16) in self.UNREADABLE_GRID_CELLS and len(body) == 4
        }
        self.assertEqual(
            cells, dict(zip(self.UNREADABLE_GRID_CELLS, (4, 6))), spans
        )

    def test_the_report_names_the_build_it_is_a_difference_from(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory)
        identity = report["identity"]
        self.assertTrue(identity["modules"])
        self.assertEqual(
            identity["decoder_module"], "elysium_pipeline.formats.mdl_skel"
        )
        self.assertEqual(len(identity["decoder_sha256"]), 64)

    def test_the_missing_list_is_empty_once_the_five_ranges_are_carried(
        self,
    ) -> None:
        """CAP4.2's whole finding, inverted by CAP5.3.

        Five field ranges were read by retail and by nobody offline:
        `StudioAnimRecord.weight`@0, and `numblends`@52, `groupsize`@572,
        `paramindex`@580 and the fired grid cells of `StudioSeqDesc`. The
        decoder now reads all five — the weight as the zero test that ends a
        bone's decode, the other four as the blend space `local_sequences`
        carries out beside each clip — so the difference has nothing left to
        name on a corpus whose grids the format explains.
        """
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory)
        self.assertEqual(report["difference"]["missing_by_field"], [])
        self.assertEqual(report["difference"]["totals"]["retail_only_bytes"], 0)

    def test_the_grid_cells_are_read_at_the_address_retail_resolved_them_at(
        self,
    ) -> None:
        """The address, checked from both sides at once.

        Axis 0 takes the fixed 16-short row stride. The retail arm claims the
        cells the evaluator resolved and the decoder reads the cells inside the
        extents; if the two disagreed about which axis strides, a fired cell
        would land in the missing list. A 3x1 grid separates them, because a
        transposed read would name slots 1 and 2 where retail named 16 and 32.
        """
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._session(
                session,
                (
                    contribution_record(
                        b"SEQP", 8, 103, sequence_index=1, num_blends=3,
                        group_size=(3, 1), blend_cell=(1, 0),
                    )
                    + contribution_record(b"ANIM", 9, 104, animation_index=4)
                    + contribution_record(b"ANIM", 10, 105, animation_index=6)
                ),
                image=self._bank(
                    grids={1: Grid((3, 1), {(0, 0): 4, (1, 0): 4, (2, 0): 6})}
                ),
            )
            report = verify_coverage(session)
        self.assertEqual(report["difference"]["missing_by_field"], [])
        self.assertEqual(report["difference"]["totals"]["retail_only_bytes"], 0)

    def test_retail_reaches_no_track_run_our_walk_skipped(self) -> None:
        """The one containment the comparison is entitled to assert.

        Retail walks a track to the sampled frame and reads two keys; the
        exporter materializes every key of every run it enters. So the only
        retail track byte that can escape ours is the quaternion look-ahead, two
        bytes wide. Anything wider is a run our walk skipped.
        """
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory)
        self.assertEqual(
            report["difference"]["counts"]["retail_track_span_wider_than_one_key"],
            0,
        )

    def test_the_quaternion_look_ahead_is_named_rather_than_read_as_a_field(
        self,
    ) -> None:
        """A sampled last frame sends the look-ahead past the track it walked.

        Clip 4's only track is a rotation channel of one run, and it is the last
        thing in its animation block. Sampling its final frame makes the
        quaternion decoder reach for the following run's first key, which is
        past the track entirely. Those bytes are retail's and not ours, and
        calling them a field we failed to read would name the wrong thing.
        """
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._session(
                session,
                contribution_record(b"SEQP", 8, 103, sequence_index=0, cycle=1.0)
                + contribution_record(
                    b"ANIM", 9, 104, animation_index=4, cycle=1.0
                ),
            )
            report = verify_coverage(session)
        counts = report["difference"]["counts"]
        self.assertGreater(
            counts["retail_track_bytes_past_our_whole_track_walk"], 0, counts
        )
        self.assertEqual(counts["retail_track_span_wider_than_one_key"], 0)
        self.assertTrue(report["difference"]["track_lookahead_by_field"])
        self.assertNotIn(
            "mstudioanimvalue_t:*",
            {entry["field"] for entry in report["difference"]["missing_by_field"]},
        )
        self.assertTrue(report["verdict"]["byte_coverage_compared"])

    def test_bytes_read_by_neither_arm_stay_unknown(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory)
        self.assertGreater(report["difference"]["totals"]["neither_bytes"], 0)
        self.assertTrue(report["difference"]["unknown_by_field"])

    def test_a_fired_identity_carries_its_own_difference(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._session(
                session,
                self._unreadable_grid_contributions(),
                image=self._unreadable_grid_bank(),
            )
            report = verify_coverage(session)
        identities = report["difference"]["identities"]
        self.assertTrue(identities)
        # Two animations and one sequence fired, and only the sequence is short.
        # The difference is attributed to the identity that caused it rather
        # than smeared across every identity the run touched.
        self.assertEqual(
            [(entry["kind"], entry["index"]) for entry in identities], [("SEQP", 1)]
        )
        for entry in identities:
            self.assertGreater(entry["retail_only_bytes"], 0)
            self.assertTrue(entry["retail_only_fields"])

    def test_every_emitted_population_is_declared_once(self) -> None:
        """No count reaches the verdict without a declaration behind it."""
        self.assertEqual(set(DEFECTS) & set(ACCOUNTED), set())
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory)
        declared = set(DEFECTS) | set(ACCOUNTED)
        for arm in ("retail", "decoder", "difference", "installed"):
            for name in (report[arm].get("counts") or {}):
                self.assertIn(name, declared, f"{arm}.{name}")

    def test_an_absent_install_is_a_shortfall_rather_than_a_defect(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._session(session)
            report = verify_coverage(
                session, installed=True, install_reader=lambda key: None
            )
        self.assertEqual(report["loader_written"]["owners_resolved"], 0)
        self.assertEqual(
            report["installed"]["counts"]["installed_image_read_set_differs"], 0
        )
        self.assertGreater(report["installed"]["counts"]["installed_image_absent"], 0)
        self.assertEqual(report["verdict"]["defects"], {})
        self.assertTrue(report["verdict"]["byte_coverage_compared"])

    def test_the_installed_arm_holds_when_the_loader_rewrote_a_byte(self) -> None:
        """A loader-written byte must not change which bytes we read.

        The captured image and its installed source differ in `StudioBone.Flags`
        among others, and no branch of `local_sequences` or `read_anim` reads
        one. Answering that with a second run rather than asserting it is what
        keeps our arm a property of the source.
        """
        installed = bytearray(self._bank())
        struct.pack_into("<i", installed, BONE_ARRAY_OFFSET + 136, 0x8)
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._session(session)
            report = verify_coverage(
                session,
                installed=True,
                install_reader=lambda key: (
                    bytes(installed) if key == BANK_MODEL else None
                ),
            )
        self.assertEqual(report["installed"]["owners_compared"], 1)
        self.assertEqual(
            report["installed"]["counts"]["installed_image_read_set_differs"], 0
        )
        self.assertEqual(report["installed"]["loader_written_bytes"], 1)

    def test_a_missing_range_the_loader_wrote_is_marked_as_one(self) -> None:
        """A byte the loader rewrote is not a byte the file carries.

        `StudioSeqDesc`+0xc is the activity dword the loader writes at load, and
        retail's walker claims nothing there — but when a range does appear on
        the missing side it must be reported as a runtime value rather than as
        source bytes the decoder failed to read. The range here is a cell of the
        grid whose extents the format cannot explain, which is the one thing
        this fixture leaves unread.
        """
        installed = bytearray(self._unreadable_grid_bank())
        for offset in self.UNREADABLE_GRID_CELLS:
            # Differing in both bytes of the cell, so the overlay is the whole
            # field rather than whichever byte happened to change.
            struct.pack_into("<h", installed, offset, 0x0202)
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._session(
                session,
                self._unreadable_grid_contributions(),
                image=self._unreadable_grid_bank(),
            )
            report = verify_coverage(
                session,
                install_reader=lambda key: (
                    bytes(installed) if key == BANK_MODEL else None
                ),
            )
        overlay = report["loader_written"]
        self.assertTrue(overlay["available"], overlay)
        self.assertEqual(overlay["owners_resolved"], 1)
        self.assertEqual(overlay["missing_bytes_the_loader_wrote"], 4)
        self.assertEqual(
            [entry["field"] for entry in overlay["by_field"]],
            ["StudioSeqDesc.anim[16][16]"],
        )

    def test_a_layout_names_the_field_at_a_known_offset(self) -> None:
        layout = Layout(self._bank())
        self.assertEqual(layout.num_bones, 2)
        self.assertEqual(layout.seq_index, CONTRIBUTION_SEQ_INDEX_OFF)
        self.assertEqual(
            layout.attribute([(BONE_ARRAY_OFFSET + 136, BONE_ARRAY_OFFSET + 140)]),
            {
                "StudioBone.Flags": {
                    "field": "StudioBone.Flags",
                    "region": "bone_array",
                    "bytes": 4,
                    "first_offset": BONE_ARRAY_OFFSET + 136,
                }
            },
        )
        # A gap between two declared regions is bounded by the next one rather
        # than swallowing every structure above it.
        _, end, kind, _ = layout.region_at(HEADER_BYTES + 4)
        self.assertEqual(kind, "unindexed")
        self.assertLessEqual(end, BONE_ARRAY_OFFSET)

    def test_a_label_string_is_attributed_to_the_descriptor_that_named_it(
        self,
    ) -> None:
        """The trace is ordered, so a read outside the array has a cause.

        `local_sequences` follows a descriptor-relative index to a label that
        lives past the array. Attributing it by offset alone would leave it
        unowned; attributing it to the descriptor last touched puts it on the
        identity whose difference it belongs to.
        """
        from elysium_pipeline.formats import mdl_skel

        image = self._bank()
        with instrumented(mdl_skel, image) as (data, recorder):
            mdl_skel.local_sequences(data)
        attributed = _sequence_attribution(recorder, Layout(image))
        base = CONTRIBUTION_SEQ_INDEX_OFF + SEQ_DESC_STRIDE * 1
        label = base + struct.unpack_from("<i", image, base)[0]
        self.assertTrue(
            any(low <= label < high for low, high in attributed[1]),
            attributed[1],
        )
        self.assertFalse(
            any(low <= label < high for low, high in attributed.get(0, ())),
        )

    def test_a_database_without_spans_is_unjudgeable_rather_than_clean(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            write_join_session(session, {"models/test.mdl": (0x3000, [0x2000], [])})
            report = verify_coverage(session)
        self.assertFalse(report["verdict"]["judgeable"])
        self.assertFalse(report["verdict"]["byte_coverage_compared"])
        self.assertIn("resolve_consumed_spans", report["verdict"]["statement"])

    def test_two_sessions_are_reported_side_by_side(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            first = self._report(directory)
        with tempfile.TemporaryDirectory() as directory:
            second = self._report(directory)
        comparison = compare_coverage([first, second])
        self.assertEqual(len(comparison["retail_only_bytes"]), 2)
        self.assertIn("side by side", comparison["statement"])


# --- CAP4.3 transform difference ---------------------------------------------

# A skeleton whose binds carry real rotations and whose third bone does not
# inherit its parent's rotation, which is the one thing the split arm
# discriminates on. Rotations are exact quaternions so the hand-written oracle
# and the numpy evaluator can be required to agree to floating-point noise
# rather than to a band.
HALF = math.sqrt(0.5)
ROTATED_BONES = (
    ("Bip01", -1, (0.0, 0.0, 0.0), (0.0, 0.0, HALF, HALF), 0),
    ("Bip01 Spine", 0, (1.0, 2.0, 3.0), (HALF, 0.0, 0.0, HALF), 0),
    ("Bip01 Neck", 1, (0.0, 4.0, 0.0), (0.0, HALF, 0.0, HALF), 0x2),
    ("Bip01 Head", 2, (0.0, 1.0, 0.0), (0.0, 0.0, 0.0, 1.0), 0),
)


def _identity_pose(bones) -> list[tuple[tuple[float, ...], tuple[float, ...]]]:
    """The bind pose as a local pose: every bone at its own bind transform."""
    return [
        (spec[2], spec[3])
        for spec in (_bone_spec(entry) for entry in bones)
    ]


def _bind_locals(bones):
    return [(spec[2], spec[3]) for spec in (_bone_spec(entry) for entry in bones)]


class DecoderPoseTests(unittest.TestCase):
    """CAP4.3: the transform arithmetic the difference arms are built from.

    Every expected value here is produced by the hand-written helpers at the top
    of this module rather than by `decoder_pose`, so an agreement is between two
    independent formulations and not a tautology.
    """

    def _skeleton(self, bones=ROTATED_BONES, **kwargs):
        image = model_image(0x3000, "models/rotated.mdl", bones=bones, **kwargs)
        return decoder_pose.skeleton_from_image(image, 0x3000)

    def test_frame_interpolation_flips_a_quaternion_to_the_nearer_hemisphere(
        self,
    ) -> None:
        """The Prince bank's synthetic shape: adjacent keys change sign.

        A quaternion and its negation name the same endpoint, but mixing their
        raw components takes the long way between frames. Retail chooses the
        nearer sign before normalized lerp through `FUN_1010a0b0`.
        """
        first = (0.0, 0.0, 0.0, 1.0)
        second = (0.0, 0.0, -HALF, -HALF)
        clip = decoder_pose.Clip(
            positions=np.zeros((2, 1, 3), dtype=np.float32),
            quaternions=np.asarray([[first], [second]], dtype=np.float32),
            frames=2,
            fps=30.0,
            bones=1,
        )

        _, sampled = decoder_pose.sample_clip(
            clip, np.array([0]), np.array([0.5]), "linear"
        )
        expected = _nlerp_one(first, second, 0.5)
        self.assertTrue(np.allclose(sampled[0, 0], expected, atol=1.0e-7))

    def test_blend_cells_use_normalized_lerp_instead_of_slerp(self) -> None:
        """The walk-grid shape, independently separating the two operations."""
        first_quaternion = (0.0, 0.0, 0.0, 1.0)
        second_quaternion = (HALF, 0.0, 0.0, HALF)
        first = (
            np.zeros((1, 1, 3), dtype=np.float64),
            np.asarray([[first_quaternion]], dtype=np.float64),
        )
        second = (
            np.asarray([[[4.0, 8.0, 12.0]]]),
            np.asarray([[second_quaternion]], dtype=np.float64),
        )

        position, quaternion = decoder_pose.blend_cells(
            first, second, np.array([0.25])
        )
        expected = _nlerp_one(first_quaternion, second_quaternion, 0.25)
        counterfactual = _slerp_one(first_quaternion, second_quaternion, 0.25)
        self.assertTrue(np.allclose(position[0, 0], (1.0, 2.0, 3.0)))
        self.assertTrue(np.allclose(quaternion[0, 0], expected, atol=1.0e-9))
        self.assertFalse(np.allclose(quaternion[0, 0], counterfactual, atol=1.0e-4))

    def test_a_skeleton_decodes_its_binds_flags_and_inverse_binds(self) -> None:
        skeleton = self._skeleton()
        self.assertEqual(skeleton.bones, 4)
        self.assertEqual(skeleton.faults, ())
        self.assertEqual(list(skeleton.parent), [-1, 0, 1, 2])
        self.assertEqual(list(skeleton.depth), [0, 1, 2, 3])
        self.assertEqual(list(skeleton.split), [False, False, True, False])
        expected = _bind_world(ROTATED_BONES)
        for index, matrix in enumerate(expected):
            self.assertTrue(
                np.allclose(
                    skeleton.pose_to_bone[index],
                    np.asarray(_invert_rigid_3x4(matrix)).reshape(3, 4),
                    atol=1.0e-6,
                )
            )

    def test_a_parent_at_or_after_its_child_is_a_named_fault(self) -> None:
        skeleton = self._skeleton(
            bones=(
                ("Bip01", -1, (0.0, 0.0, 0.0)),
                ("Bip01 Spine", 1, (1.0, 0.0, 0.0)),
            )
        )
        self.assertIn("skeleton_parent_not_topological", skeleton.faults)

    def test_the_split_branch_takes_its_rotation_from_the_root(self) -> None:
        skeleton = self._skeleton()
        locals_ = _bind_locals(ROTATED_BONES)
        root = (0.0, -1.0, 0.0, 5.0, 1.0, 0.0, 0.0, -7.0, 0.0, 0.0, 1.0, 2.0)
        for split in (True, False):
            expected = _expected_world(ROTATED_BONES, locals_, root, split=split)
            actual = decoder_pose.compose(
                decoder_pose.matrices_from_local(
                    np.array([p for p, _ in locals_]),
                    np.array([q for _, q in locals_]),
                ),
                skeleton,
                np.asarray(root, dtype=np.float64).reshape(3, 4),
                split=split,
            )
            for index, matrix in enumerate(expected):
                self.assertTrue(
                    np.allclose(
                        actual[index], np.asarray(matrix).reshape(3, 4), atol=1.0e-9
                    ),
                    f"split={split} bone {index}",
                )

    def test_the_two_rules_differ_only_on_the_flagged_bone_and_below(self) -> None:
        skeleton = self._skeleton()
        locals_ = _bind_locals(ROTATED_BONES)
        local = decoder_pose.matrices_from_local(
            np.array([p for p, _ in locals_]), np.array([q for _, q in locals_])
        )
        split = decoder_pose.compose(local, skeleton, None, split=True)
        ordinary = decoder_pose.compose(local, skeleton, None, split=False)
        differs = [
            not np.allclose(split[index], ordinary[index], atol=1.0e-9)
            for index in range(skeleton.bones)
        ]
        # Bone 2 carries the flag; bone 3 inherits the divergence and nothing
        # above bone 2 moves at all.
        self.assertEqual(differs, [False, False, True, True])
        self.assertEqual(list(skeleton.descendants(2)), [False, False, True, True])

    def test_bone_to_world_equals_the_root_times_the_model_space_pose(self) -> None:
        skeleton = self._skeleton()
        locals_ = _bind_locals(ROTATED_BONES)
        local = decoder_pose.matrices_from_local(
            np.array([p for p, _ in locals_]), np.array([q for _, q in locals_])
        )
        angle = 0.7
        root = np.asarray(
            (
                math.cos(angle), -math.sin(angle), 0.0, 11.0,
                math.sin(angle), math.cos(angle), 0.0, -3.0,
                0.0, 0.0, 1.0, 0.5,
            ),
            dtype=np.float64,
        ).reshape(3, 4)
        for split in (True, False):
            model = decoder_pose.compose(local, skeleton, None, split=split)
            world = decoder_pose.compose(local, skeleton, root, split=split)
            self.assertTrue(
                np.allclose(decoder_pose.multiply(root, model), world, atol=1.0e-9),
                f"split={split}",
            )

    def test_a_seeded_bone_is_passed_through_and_its_children_compose_off_it(
        self,
    ) -> None:
        skeleton = self._skeleton()
        locals_ = _bind_locals(ROTATED_BONES)
        local = decoder_pose.matrices_from_local(
            np.array([p for p, _ in locals_]), np.array([q for _, q in locals_])
        )
        seed = np.tile(np.asarray(decoder_pose.IDENTITY), (skeleton.bones, 1, 1))
        seed[1, :, 3] = (9.0, 9.0, 9.0)
        selected = np.array([True, False, True, True])
        world = decoder_pose.compose(
            local, skeleton, None, split=False, seed=seed, selected=selected
        )
        self.assertTrue(np.allclose(world[1], seed[1], atol=1.0e-12))
        expected = _expected_world(
            ROTATED_BONES,
            locals_,
            None,
            split=False,
            seed=[tuple(matrix.ravel()) for matrix in seed],
            selected={0, 2, 3},
        )
        for index, matrix in enumerate(expected):
            self.assertTrue(
                np.allclose(world[index], np.asarray(matrix).reshape(3, 4), atol=1.0e-9)
            )

    def test_the_palette_of_the_bind_pose_is_the_identity(self) -> None:
        skeleton = self._skeleton()
        locals_ = _bind_locals(ROTATED_BONES)
        local = decoder_pose.matrices_from_local(
            np.array([p for p, _ in locals_]), np.array([q for _, q in locals_])
        )
        # The ordinary hierarchy, because a flagged bone's inverse bind is the
        # conventional one and the palette is what proves it.
        world = decoder_pose.compose(local, skeleton, None, split=False)
        skin = decoder_pose.palette(world, skeleton.pose_to_bone)
        self.assertTrue(
            np.allclose(skin, np.asarray(decoder_pose.IDENTITY), atol=1.0e-5)
        )

    def test_the_regenerated_inverse_bind_reproduces_a_conventional_pose_to_bone(
        self,
    ) -> None:
        skeleton = self._skeleton()
        self.assertTrue(
            np.allclose(
                decoder_pose.regenerated_inverse_bind(skeleton),
                skeleton.pose_to_bone,
                atol=1.0e-5,
            )
        )

    def test_a_pose_to_bone_that_is_not_the_conventional_inverse_differs(
        self,
    ) -> None:
        skeleton = self._skeleton(pose_to_bone="identity")
        difference = decoder_pose.translation_error(
            decoder_pose.regenerated_inverse_bind(skeleton), skeleton.pose_to_bone
        )
        self.assertGreater(float(difference.max()), 1.0)

    def test_the_axis_interp_rule_matches_an_independent_transcription(
        self,
    ) -> None:
        skeleton = self._skeleton(bones=PROCEDURAL_BONES, procedural={3: AXIS_RULE})
        self.assertEqual(len(skeleton.axis_interp), 1)
        self.assertEqual(skeleton.axis_interp[0].bone, 3)
        self.assertEqual(skeleton.axis_interp[0].control, 1)
        self.assertEqual(list(skeleton.procedural), [False, False, False, True])
        locals_ = _bind_locals(PROCEDURAL_BONES)
        local = decoder_pose.matrices_from_local(
            np.array([p for p, _ in locals_]), np.array([q for _, q in locals_])
        )
        root = np.asarray(TRANSFORM_ROOT, dtype=np.float64).reshape(3, 4)
        world = decoder_pose.compose(
            local, skeleton, root, split=True, procedural=True
        )
        expected = _expected_world(
            PROCEDURAL_BONES, locals_, TRANSFORM_ROOT, split=True,
            procedural={3: AXIS_RULE},
        )
        for index, matrix in enumerate(expected):
            self.assertTrue(
                np.allclose(
                    world[index], np.asarray(matrix).reshape(3, 4), atol=1.0e-9
                ),
                f"bone {index}",
            )
        # The rule replaces the animated local rather than adjusting it, so the
        # driven bone moves relative to composing without it.
        plain = decoder_pose.compose(local, skeleton, root, split=True)
        self.assertGreater(
            float(decoder_pose.matrix_rotation_angle_degrees(world[3], plain[3])),
            1.0,
        )

    def test_an_unreadable_procedural_rule_is_a_named_fault(self) -> None:
        skeleton = self._skeleton(procedural={3: None})
        self.assertIn("procedural_rule_unreadable", skeleton.faults)
        self.assertEqual(skeleton.axis_interp, ())
        self.assertTrue(bool(skeleton.procedural[3]))

    # ---- the owner-to-entity bone correspondence ----

    #: An ordinary character: one biped, plus a bone that is not part of one.
    TARGET_NAMES = ("Bip01", "Bip01 Spine", "Bip01 Head", "Dummy01")
    #: A cinematic bank: two complete bipeds side by side, plus a camera dummy
    #: belonging to neither, which is the shape 41 of the 85 multi-actor
    #: cinematic models have.
    BANK_NAMES = (
        "Bip01", "Bip01 Spine", "Bip01 Head",
        "Bip02", "Bip02 Spine", "Bip02 Head",
        "Dummy01",
    )

    def _pairs(self, **kwargs):
        target, source = decoder_pose.correspondence(
            self.TARGET_NAMES, self.BANK_NAMES, **kwargs
        )
        return {
            self.TARGET_NAMES[t]: self.BANK_NAMES[s]
            for t, s in zip(target.tolist(), source.tolist())
        }

    def test_the_split_reaches_the_chain_the_scene_named_and_not_the_first_one(
        self,
    ) -> None:
        """What `export_cinematic` ships, and why the export is already right.

        The bank is written out once per `BipNN` root with the prefix folded
        onto `Bip01`, so an actor the scene puts on `Bip02` loads a bank whose
        `Bip01 Spine` is the bank's `Bip02 Spine`. Plain name matching would
        hand it the bank's own `Bip01` chain — a real bone carrying another
        actor's pose.
        """
        split = self._pairs(matching="cinematic_split", owner_family="bip02")
        self.assertEqual(split["Bip01 Spine"], "Bip02 Spine")
        self.assertEqual(split["Bip01 Head"], "Bip02 Head")
        plain = self._pairs(matching="name")
        self.assertEqual(plain["Bip01 Spine"], "Bip01 Spine")

    def test_the_split_and_the_family_rule_reach_the_same_chain(self) -> None:
        """The two are the same correspondence over the bones both reach.

        This is what says the shipped export pays no family cost at all: the
        rule read off retail's own mask and the rule the bake writes agree bone
        for bone on every biped bone.
        """
        split = self._pairs(matching="cinematic_split", owner_family="bip02")
        family = self._pairs(
            matching="family_name", entity_family="bip01", owner_family="bip02"
        )
        biped = {name: bone for name, bone in family.items() if name != "Dummy01"}
        self.assertEqual(
            {name: bone for name, bone in split.items() if name != "Dummy01"}, biped
        )

    def test_the_split_drops_the_bone_that_belongs_to_no_biped(self) -> None:
        """The one place the two part company, pinned rather than assumed.

        `export_cinematic` writes only the chosen chain into a bank, so a bone
        with no `BipNN` head reaches no target; the family rule still matches it
        by its plain name. Measured over the install this moves nothing — the
        only such names any character skeleton also carries come from three
        banks no `logic_choreographed_scene` names — so the divergence is
        recorded here rather than closed.
        """
        split = self._pairs(matching="cinematic_split", owner_family="bip02")
        family = self._pairs(
            matching="family_name", entity_family="bip01", owner_family="bip02"
        )
        self.assertNotIn("Dummy01", split)
        self.assertEqual(family["Dummy01"], "Dummy01")

    def test_a_single_skeleton_bank_is_not_split_and_matches_by_name(self) -> None:
        """`export_cinematic` hands a one-root model to `export_bank` whole."""
        self.assertEqual(
            self._pairs(matching="cinematic_split", owner_family=None),
            self._pairs(matching="name"),
        )

    def test_the_rotation_metric_reads_zero_for_a_quaternion_and_its_negation(
        self,
    ) -> None:
        quaternion = np.array([[0.1, 0.2, 0.3, 0.927361849]])
        self.assertAlmostEqual(
            float(decoder_pose.rotation_angle_degrees(quaternion, -quaternion)[0]),
            0.0,
            places=9,
        )
        matrix = decoder_pose.matrices_from_local(np.zeros((1, 3)), quaternion)
        self.assertAlmostEqual(
            float(decoder_pose.matrix_rotation_angle_degrees(matrix, matrix)[0]),
            0.0,
            places=9,
        )

    def test_a_row_scale_deficit_is_not_read_as_a_rotation(self) -> None:
        """The float32 noise a captured matrix carries is not an angle.

        A row 3e-4 short moves `(trace - 1) / 2` to 0.9991, and `arccos` turns
        that into 2.4 degrees of rotation that is not there. It is the ordinary
        precision of a `matrix3x4_t` the game wrote, and it is measured as
        orthonormality rather than as an angle.
        """
        matrix = decoder_pose.matrices_from_local(
            np.zeros(3), np.array([0.1, 0.2, 0.3, 0.927361849])
        )
        shrunk = matrix.copy()
        shrunk[..., :3] *= 1.0 - 3.0e-4
        self.assertLess(
            float(decoder_pose.matrix_rotation_angle_degrees(matrix, shrunk)),
            1.0e-6,
        )
        # The deficit is still reported, as the thing it actually is.
        self.assertAlmostEqual(
            float(decoder_pose.row_length_error(shrunk)), 3.0e-4, places=9
        )

    def test_a_ninety_degree_difference_reads_as_ninety_degrees(self) -> None:
        a = np.array([[0.0, 0.0, 0.0, 1.0]])
        b = np.array([[0.0, 0.0, HALF, HALF]])
        self.assertAlmostEqual(
            float(decoder_pose.rotation_angle_degrees(a, b)[0]), 90.0, places=6
        )

    def test_row_length_error_measures_the_worst_row(self) -> None:
        matrix = np.asarray(decoder_pose.IDENTITY).copy()
        matrix[1, :3] *= 1.25
        self.assertAlmostEqual(
            float(decoder_pose.row_length_error(matrix)), 0.25, places=9
        )

    def test_the_accumulator_holds_exact_maxima_and_band_counts(self) -> None:
        accumulator = decoder_pose.BoneAccumulator(2, decoder_pose.BANDS["model_translation"])
        accumulator.add(np.array([[0.001, 0.5], [0.05, 0.3]]), np.array([3, 1]))
        self.assertEqual(list(accumulator.count), [4, 4])
        self.assertAlmostEqual(float(accumulator.maximum[0]), 0.05)
        self.assertAlmostEqual(float(accumulator.maximum[1]), 0.5)
        # Bone 0: 0.001 (excellent, x3) and 0.05 (investigate, x1).
        self.assertEqual(list(accumulator.banded[0]), [3, 1, 0])
        # Bone 1: both above 0.2, so both definite.
        self.assertEqual(list(accumulator.banded[1]), [0, 0, 4])
        self.assertEqual(accumulator.summary()["bands"]["definite"], 4)

    def test_the_accumulator_quantile_brackets_the_true_quantile(self) -> None:
        accumulator = decoder_pose.BoneAccumulator(1, decoder_pose.BANDS["rotation_degrees"])
        values = np.linspace(0.01, 1.0, 100).reshape(100, 1)
        accumulator.add(values, 1)
        true_p99 = float(np.quantile(values, 0.99))
        reported = float(accumulator.quantile(0.99)[0])
        # The histogram reports the upper edge of the bucket the value falls in,
        # so it is an upper bound accurate to one bucket ratio.
        self.assertGreaterEqual(reported, true_p99)
        self.assertLessEqual(reported, true_p99 * decoder_pose.QUANTILE_RESOLUTION)

    def test_an_evaluation_payload_reads_back_as_two_separate_arrays(self) -> None:
        pose = [((1.0, 2.0, 3.0), (0.0, 0.0, 0.0, 1.0)), ((4.0, 5.0, 6.0), (0.0, 0.0, HALF, HALF))]
        record = animation_record(
            b"FINL", 1, 100, bone_count=2, local_pose=pose, selected={1}
        )
        payload = record[ANIMATION_RECORD_HEADER.size:]
        positions, quaternions = decoder_pose.payload_local_pose(payload, 2)
        self.assertTrue(np.allclose(positions, [[1, 2, 3], [4, 5, 6]]))
        self.assertTrue(np.allclose(quaternions[1], [0.0, 0.0, HALF, HALF]))
        self.assertEqual(list(decoder_pose.payload_selected(payload, 2)), [False, True])

    def test_a_draw_payload_reads_back_as_two_matrix_arrays(self) -> None:
        world = [tuple(range(12)), tuple(range(12, 24))]
        skin = [tuple(range(24, 36)), tuple(range(36, 48))]
        record = pose_record(
            1, 100, "models/x.mdl", bone_count=2, bone_to_world=world, skin_palette=skin
        )
        payload = record[POSE_RECORD_HEADER.size:]
        first = decoder_pose.payload_matrices(payload, 2)
        second = decoder_pose.payload_matrices(payload, 2, offset=2 * 48)
        self.assertTrue(np.allclose(first[1].ravel(), list(range(12, 24))))
        self.assertTrue(np.allclose(second[0].ravel(), list(range(24, 36))))


TRANSFORM_CHECKSUM = 0x3000
TRANSFORM_MODEL = "models/rotated.mdl"
TRANSFORM_ENTITY = 0x1FF8
TRANSFORM_RENDERABLE = TRANSFORM_ENTITY + 4
TRANSFORM_ROOT = (
    0.0, -1.0, 0.0, 12.0,
    1.0, 0.0, 0.0, -4.0,
    0.0, 0.0, 1.0, 3.0,
)
# A pose that is not the bind pose, so a wrong composition rule has somewhere to
# go: every bone is displaced and two of them are rotated.
#
# The last bone's offset is deliberately off-axis. The split and ordinary rules
# differ on bone 2 by a rotation about Y, so a child displaced along Y alone
# would land in the same place under both and the translation metrics would read
# zero while only the rotation moved.
MOVED_POSE = (
    ((0.5, -0.25, 2.0), (0.0, 0.0, 0.0, 1.0)),
    ((1.0, 2.0, 3.5), (0.0, 0.0, HALF, HALF)),
    ((0.0, 4.0, 0.25), (HALF, 0.0, 0.0, HALF)),
    ((0.75, 1.0, -0.5), (0.0, 0.0, 0.0, 1.0)),
)

# A skeleton whose control bone carries an oblique bind rotation. Exact 90°
# binds drive the axis-interpolation rule onto one table entry, so the selection
# runs and the two slerps never do; an oblique one gives a driver with three
# non-zero components and exercises the whole rule.
OBLIQUE = tuple(v / math.sqrt(0.3**2 + 0.2**2 + 0.1**2 + 0.9**2)
                for v in (0.3, 0.2, 0.1, 0.9))
PROCEDURAL_BONES = (
    ("Bip01", -1, (0.0, 0.0, 0.0), (0.0, 0.0, HALF, HALF), 0),
    ("Bip01 Spine", 0, (1.0, 2.0, 3.0), OBLIQUE, 0),
    ("Bip01 Neck", 1, (0.0, 4.0, 0.0), (0.0, HALF, 0.0, HALF), 0x2),
    ("Bip01 L Bicep", 1, (0.75, 1.0, -0.5), (0.0, 0.0, 0.0, 1.0), 0),
)

# One `ProcType == 1` table: `(control, axis, pos[6], quat[6])`. The six
# rotations are distinct so the interpolation has somewhere to go, and the six
# positions repeat one value, which is what the shipped tables overwhelmingly
# do — see `docs/vtmb/procedural_bones.md`.
AXIS_RULE = (
    1,
    2,
    ((0.4, 1.1, -0.2),) * 6,
    (
        (0.0, 0.0, 0.0, 1.0),
        (0.0, 0.0, HALF, HALF),
        (HALF, 0.0, 0.0, HALF),
        (0.0, HALF, 0.0, HALF),
        (0.0, 0.0, -HALF, HALF),
        (-HALF, 0.0, 0.0, HALF),
    ),
)

# The basis the model exporter writes its glbs in, restated here so the expected
# value of a converted table never comes from the exporter that produced it. M is
# a proper rotation, `(x, y, z) -> (x, z, -y)`, and the scale is inches to metres.
GLTF_M = ((1.0, 0.0, 0.0), (0.0, 0.0, 1.0), (0.0, -1.0, 0.0))
GLTF_SCALE = 0.0254


def _raw_axis_interp(image: bytes, bone: int) -> bytes:
    """The 176-byte `mstudioaxisinterpbone_t` a bone record's `ProcIndex` points at."""
    record = struct.unpack_from("<i", image, 244)[0] + BONE_STRIDE * bone
    offset = record + struct.unpack_from("<i", image, record + 144)[0]
    return bytes(image[offset : offset + 176])


def _decoded_axis_interp(raw: bytes):
    """A raw rule record as `_axis_interp_local` takes it.

    The record stores float32, so a comparison that fed the exporter the stored
    bytes and the transcription the fixture's own float64 would be reading two
    different tables and calling the difference a conversion error.
    """
    control, axis = struct.unpack_from("<ii", raw, 0)
    positions = [struct.unpack_from("<3f", raw, 8 + 12 * i) for i in range(6)]
    quaternions = [struct.unpack_from("<4f", raw, 80 + 16 * i) for i in range(6)]
    return control, axis, positions, quaternions


def _to_gltf_3x4(matrix: tuple[float, ...]) -> tuple[float, ...]:
    """A Source-basis 3x4 in the glb's basis.

    A change of basis conjugates a transform, so the rotation goes to `M R M^T`
    while the translation is carried through `M` once and scaled.
    """
    rotation = [
        [
            sum(
                GLTF_M[row][k] * matrix[k * 4 + l] * GLTF_M[column][l]
                for k in range(3)
                for l in range(3)
            )
            for column in range(3)
        ]
        for row in range(3)
    ]
    translation = [
        GLTF_SCALE * sum(GLTF_M[row][k] * matrix[k * 4 + 3] for k in range(3))
        for row in range(3)
    ]
    return tuple(
        value for row in range(3) for value in (*rotation[row], translation[row])
    )


def _exported_axis_interp_local(rule, driver_axes, world, bones):
    """The local an exported rule produces, read out of the exported table alone.

    The same rule as `_axis_interp_local`, stated the way the sidecar states it: the
    driver is `axis` rotated by the control bone's local rotation rather than a
    column named by an index, and term `k`'s signed weight is its dot product with
    `driver_axes[k]` rather than its `k`-th component. Nothing here knows which
    Source axis any of that came from, which is the point — a table that named the
    wrong axis after conversion would evaluate to a different correction here and
    the same one under `_axis_interp_local`.
    """
    control = rule["control_index"]
    frame = world[control]
    driver = [
        sum(frame[row * 4 + column] * rule["axis"][column] for column in range(3))
        for row in range(3)
    ]
    parent = _bone_spec(bones[control])[1]
    if parent >= 0:
        outer = world[parent]
        driver = [
            sum(outer[row * 4 + column] * driver[row] for row in range(3))
            for column in range(3)
        ]
    weights = [sum(a * b for a, b in zip(axis, driver)) for axis in driver_axes]
    picked = tuple(2 * k if weights[k] >= 0.0 else 2 * k + 1 for k in range(3))
    a1, a2, a3 = (abs(value) for value in weights)
    positions, quaternions = rule["pos"], rule["quat"]
    if a1 + a2 > 0.0:
        scale = 1.0 / (a1 + a2 + a3)
        quaternion = _slerp_one(
            _slerp_one(quaternions[picked[1]], quaternions[picked[0]], a1 / (a1 + a2)),
            quaternions[picked[2]],
            a3 * scale,
        )
        position = tuple(
            a1 * scale * positions[picked[0]][axis_index]
            + a2 * scale * positions[picked[1]][axis_index]
            + a3 * scale * positions[picked[2]][axis_index]
            for axis_index in range(3)
        )
    else:
        quaternion = quaternions[picked[2]]
        position = positions[picked[2]]
    return _matrix_3x4(position, quaternion)


#: A 9x1 walk grid on `move_and_ranged`'s own shape: one axis driven by `move_yaw`
#: over its whole -180..180 range, nine cells down the fixed 16-short row stride,
#: and the wrap cell repeating the animation the base cell already names.
NINE_BY_ONE = Grid(
    (9, 1),
    {(0, 0): 0, (1, 0): 1, (2, 0): 2, (3, 0): 3, (4, 0): 4,
     (5, 0): 5, (6, 0): 6, (7, 0): 2, (8, 0): 0},
    paramindex=(0, -1),
)

#: A 3x3 aim layer: both axes bound, over the +/-45 degree range the weapon-aim
#: sequences carry. Nine distinct cells, so a transposed cell address names a
#: different animation on six of them.
THREE_BY_THREE = Grid(
    (3, 3),
    {(0, 0): 0, (0, 1): 1, (0, 2): 2,
     (1, 0): 3, (1, 1): 4, (1, 2): 5,
     (2, 0): 6, (2, 1): 2, (2, 2): 1},
    paramindex=(2, 3),
    paramstart=(-45.0, -45.0),
    paramend=(45.0, 45.0),
)

#: The four pose parameters both `move_and_ranged` banks declare, in their order.
POSE_PARAMETERS = (
    ("move_yaw", 1, -180.0, 180.0, 360.0),
    ("hit_yaw", 1, -180.0, 180.0, 360.0),
    ("aim_yaw", 0, -45.0, 45.0, 0.0),
    ("aim_pitch", 0, -45.0, 45.0, 0.0),
)


class BlendGridTests(unittest.TestCase):
    """CAP5.3: the blend grid the exporter carries out beside the clips.

    `local_sequences` used to bake cell `[0][0]` and read nothing else, so a
    9x1 walk grid shipped as one clip out of nine and the fields naming the
    other eight went unread. What is checked here is the decode — extents, the
    pose-parameter binding, and the cell address that retail's own witnessed
    cells fix — and the export that turns every cell into a clip without
    blending any of them.
    """

    def _image(self, grids, *, labels=("walk", "aim", "idle", "turn"), **kwargs):
        return model_image(
            TRANSFORM_CHECKSUM,
            TRANSFORM_MODEL,
            labels=labels,
            grids=grids,
            pose_parameters=POSE_PARAMETERS,
            **kwargs,
        )

    def _sequences(self, image):
        from elysium_pipeline.formats import mdl_skel

        return {seq.label: seq for seq in mdl_skel.local_sequences(image)}

    def _with_movements(self, image, animation, records):
        """Append real 44-byte movement records and point one animdesc at them."""
        data = bytearray(image)
        descriptor = CONTRIBUTION_ANIM_INDEX_OFF + animation * ANIM_DESC_STRIDE
        movement = len(data)
        struct.pack_into("<ii", data, descriptor + 16, len(records), movement - descriptor)
        for record in records:
            data += struct.pack("<ii9f", *record)
        return bytes(data)

    def test_movement_records_decode_and_report_source_ground_speed(self) -> None:
        """The record layout and Source's distance/duration ground-speed calculation."""
        from elysium_pipeline.formats import mdl_skel

        records = (
            (2, 0x10C0, 2.0, 3.0, 0.0, 1.0, 0.0, 0.0, 5.0, 1.0, 0.0),
            (3, 0x10C0, 3.0, 4.0, 0.0, 1.0, 0.0, 0.0, 12.0, 0.0, 0.0),
        )
        image = self._with_movements(self._image({0: NINE_BY_ONE}), 4, records)
        _name, descriptor, frames, fps = mdl_skel.local_animation(image, 4)
        decoded = mdl_skel.read_movements(image, descriptor)
        self.assertEqual(len(decoded), 2)
        self.assertEqual(decoded[0].endframe, 2)
        self.assertEqual(decoded[0].motionflags, 0x10C0)
        self.assertEqual(decoded[0].vector, (1.0, 0.0, 0.0))
        self.assertEqual(decoded[-1].position, (12.0, 0.0, 0.0))

        summary = mdl_skel.movement_summary(image, descriptor, frames, fps)
        self.assertIsNotNone(summary)
        self.assertAlmostEqual(summary.cycle_seconds, 0.1, places=6)
        self.assertAlmostEqual(summary.ground_distance_cm, 30.48, places=5)
        self.assertAlmostEqual(summary.ground_speed_cm_s, 304.8, places=4)

    def test_absent_or_malformed_movement_keeps_the_fallback(self) -> None:
        """A damaged optional array never reads beyond the image or invents a speed."""
        from elysium_pipeline.formats import mdl_skel

        image = self._image({0: NINE_BY_ONE})
        _name, descriptor, frames, fps = mdl_skel.local_animation(image, 4)
        self.assertEqual(mdl_skel.read_movements(image, descriptor), ())
        self.assertIsNone(mdl_skel.movement_summary(image, descriptor, frames, fps))

        malformed = bytearray(image)
        struct.pack_into("<ii", malformed, descriptor + 16, 2, len(malformed) - descriptor - 10)
        self.assertEqual(mdl_skel.read_movements(malformed, descriptor), ())
        self.assertIsNone(mdl_skel.movement_summary(malformed, descriptor, frames, fps))

    def test_a_resolved_cell_exports_its_motion_summary(self) -> None:
        """The neutral walk cell carries the scalar its Unreal motor consumes."""
        from elysium_pipeline.formats import mdl_gltf, mdl_skel

        records = (
            (3, 0x10C0, 3.0, 4.0, 0.0, 1.0, 0.0, 0.0, 12.0, 0.0, 0.0),
        )
        image = self._with_movements(self._image({0: NINE_BY_ONE}), 4, records)
        _extra, blends = mdl_gltf.blend_clip_plan(image, mdl_skel.local_sequences(image))
        forward = next(cell for cell in blends["walk"]["cells"] if cell["axis"] == [4, 0])
        self.assertEqual(forward["clip"], "aim#4")
        self.assertEqual(
            forward["motion"],
            {
                "cycle_seconds": 0.1,
                "ground_distance_cm": 30.48,
                "ground_speed_cm_s": 304.8,
            },
        )
        self.assertNotIn("motion", blends["walk"]["cells"][0])

    def test_a_nine_by_one_grid_reads_its_extents_binding_and_every_cell(self) -> None:
        """The shape the theatre corpus fires throughout, read end to end."""
        sequences = self._sequences(self._image({0: NINE_BY_ONE}))
        grid = sequences["walk"].grid
        self.assertEqual(grid.numblends, 9)
        self.assertEqual(grid.groupsize, (9, 1))
        self.assertEqual(grid.paramindex, (0, -1))
        self.assertEqual(grid.paramstart, (-180.0, 0.0))
        self.assertEqual(grid.paramend, (180.0, 0.0))
        self.assertEqual(
            [(cell.axis0, cell.axis1, cell.anim) for cell in grid.cells],
            [(0, 0, 0), (1, 0, 1), (2, 0, 2), (3, 0, 3), (4, 0, 4),
             (5, 0, 5), (6, 0, 6), (7, 0, 2), (8, 0, 0)],
        )
        # The clip the sequence still bakes is the base cell's, unchanged.
        self.assertEqual(sequences["walk"].base, grid.cells[0].anim * ANIM_DESC_STRIDE
                         + CONTRIBUTION_ANIM_INDEX_OFF)

    def test_a_three_by_three_grid_takes_axis_zero_down_the_row_stride(self) -> None:
        """The cell address, in the orientation retail's own witness fixes.

        A capture of `smith_aim_layer` records the cell `[1, 0]` decoding the
        four animations at rows 1-2 and columns 0-1 of the inline array. So axis
        0 takes the fixed 16-short row stride and axis 1 the column, and the
        transposed address would name six of these nine cells wrongly.
        """
        grid = self._sequences(self._image({1: THREE_BY_THREE}))["aim"].grid
        self.assertEqual(grid.groupsize, (3, 3))
        self.assertEqual(grid.paramindex, (2, 3))
        self.assertEqual(
            {(cell.axis0, cell.axis1): cell.anim for cell in grid.cells},
            THREE_BY_THREE.cells,
        )

    def test_a_single_cell_sequence_reads_a_one_by_one_grid(self) -> None:
        """The 913-of-1,166 case: a sequence that is a clip and nothing more."""
        grid = self._sequences(self._image({}))["walk"].grid
        self.assertEqual(grid.numblends, 1)
        self.assertEqual(grid.groupsize, (1, 1))
        self.assertEqual(grid.paramindex, (-1, -1))
        self.assertEqual(len(grid.cells), 1)
        self.assertEqual((grid.cells[0].axis0, grid.cells[0].axis1), (0, 0))

    def test_extents_disagreeing_with_numblends_fall_back_to_the_base_cell(self) -> None:
        """`groupsize[0] * groupsize[1] == numblends` holds on all 294 authored
        multi-blend sequences, so a descriptor where it does not is one this
        format does not explain — and it yields the clip it always did rather
        than a grid of whatever the inline array happens to hold."""
        broken = Grid((9, 1), {(0, 0): 3, (1, 0): 1}, numblends=5)
        grid = self._sequences(self._image({0: broken}))["walk"].grid
        self.assertEqual(grid.numblends, 5)
        self.assertEqual(grid.groupsize, (9, 1))
        self.assertEqual([cell.anim for cell in grid.cells], [3])

    def test_the_pose_parameters_a_grid_axis_binds_to_are_read(self) -> None:
        """A `paramindex` is an index into this model's own array, so the axis
        cannot be wrapped or normalized without the record it names."""
        from elysium_pipeline.formats import mdl_skel

        parameters = mdl_skel.pose_parameters(self._image({0: NINE_BY_ONE}))
        self.assertEqual(
            [(p.index, p.name, p.flags, p.start, p.end, p.loop) for p in parameters],
            [(index, name, flags, start, end, loop)
             for index, (name, flags, start, end, loop) in enumerate(POSE_PARAMETERS)],
        )
        grid = self._sequences(self._image({0: NINE_BY_ONE}))["walk"].grid
        self.assertEqual(parameters[grid.paramindex[0]].name, "move_yaw")
        self.assertEqual(parameters[grid.paramindex[0]].loop, 360.0)

    def test_every_cell_becomes_its_own_clip_and_none_of_them_are_blended(self) -> None:
        """The export shortfall CAP4.1 measured, closed.

        Ten fired cells over 6,878 records had no exported counterpart because
        the base cell was the only clip. Every cell now names one, keyed by the
        animation's own name, and the grid ships beside them rather than being
        mixed into them — evaluate-then-blend is the host's job.
        """
        from elysium_pipeline.formats import mdl_gltf, mdl_skel

        image = self._image({0: NINE_BY_ONE, 1: THREE_BY_THREE})
        sequences = mdl_skel.local_sequences(image)
        extra, blends = mdl_gltf.blend_clip_plan(image, sequences)

        # One extra clip per distinct animation the two grids reach that the
        # base-cell bake does not: animations 1-6 over the two of them, each
        # named by the animation's own name. `idle`, `aim` and `turn` are also
        # sequence labels in this fixture, so those three take the index
        # disambiguation and the rest read as the animation the content named.
        self.assertEqual(
            sorted(clip.label for clip in extra),
            ["aim#4", "dead", "idle#2", "run", "skip", "turn#5"],
        )
        self.assertEqual(len({clip.base for clip in extra}), len(extra))

        walk = blends["walk"]
        self.assertEqual(walk["groupsize"], [9, 1])
        self.assertEqual(walk["paramindex"], [0, -1])
        self.assertEqual(
            [cell["clip"] for cell in walk["cells"]],
            # Cell 0 keeps the sequence label; cells 7 and 8 repeat animations
            # cells 2 and 0 already named, so they resolve to the same clips
            # rather than baking the tracks twice.
            ["walk", "run", "idle#2", "dead", "aim#4", "turn#5", "skip", "idle#2",
             "walk"],
        )
        self.assertEqual(
            [cell["axis"] for cell in blends["aim"]["cells"]],
            [[0, 0], [0, 1], [0, 2], [1, 0], [1, 1], [1, 2], [2, 0], [2, 1], [2, 2]],
        )
        # Nothing in the plan carries a blended clip: every cell names a clip
        # that decodes one animation of the model, and the weights that mix them
        # are absent because they are not the exporter's to apply.
        bases = {clip.label: clip.base for clip in (*sequences, *extra)}
        for grid in blends.values():
            for cell in grid["cells"]:
                self.assertIn(cell["clip"], bases)

    def test_a_cell_outside_the_animation_count_is_carried_as_unresolved(self) -> None:
        """A cell the model's own declaration cannot answer is a shortfall the
        sidecar names, not a grid quietly shortened to the cells that worked."""
        from elysium_pipeline.formats import mdl_gltf, mdl_skel

        image = self._image({0: Grid((3, 1), {(0, 0): 0, (1, 0): 1, (2, 0): 99})})
        _extra, blends = mdl_gltf.blend_clip_plan(
            image, mdl_skel.local_sequences(image)
        )
        self.assertEqual(
            [cell["clip"] for cell in blends["walk"]["cells"]],
            ["walk", "run", None],
        )

    def test_a_grid_whose_cells_did_not_bake_is_dropped_rather_than_promised(
        self,
    ) -> None:
        """`_bake_animation` returns nothing for a clip whose tracks came out
        empty, so a cell naming it would promise an animation the glb does not
        carry. Below two surviving cells there is no blend space left."""
        from elysium_pipeline.formats import mdl_gltf

        blends = {
            "walk": {"groupsize": [3, 1], "cells": [
                {"axis": [0, 0], "clip": "walk"},
                {"axis": [1, 0], "clip": "run"},
                {"axis": [2, 0], "clip": "idle"},
            ]},
            "aim": {"groupsize": [2, 1], "cells": [
                {"axis": [0, 0], "clip": "aim"},
                {"axis": [1, 0], "clip": "idle"},
            ]},
        }
        kept = mdl_gltf._reconcile_blends(blends, {"walk", "run", "aim"})
        self.assertEqual(sorted(kept), ["walk"])
        self.assertEqual(
            [cell["clip"] for cell in kept["walk"]["cells"]], ["walk", "run", None]
        )

    def test_a_zero_weight_record_decodes_to_zero_rather_than_to_a_pose(self) -> None:
        """CAP5.3's second half, and the one claim on this page that no captured
        byte backs.

        Both retail channel decoders compare `weight`@0 against zero before
        anything else and, on zero, write a zero position and a zero quaternion
        and return. All 2,254 decoded `(owner, animation, bone)` triples of the
        retail corpus carry 1.0, so the branch is transcribed from the
        decompiled decoders and exercised only here. It is **not** validated
        against retail bytes, and this fixture is the whole of its evidence.
        """
        from elysium_pipeline.formats import mdl_skel

        image = self._image({})
        bones = mdl_skel.read_bones(image)
        # `@dead` carries weight 0.0 on both bones; `@walk` carries 1.0 on both.
        dead = CONTRIBUTION_ANIM_INDEX_OFF + ANIM_DESC_STRIDE * 3
        frames = mdl_skel.read_anim(image, bones, dead, 2)
        for frame in frames:
            for position, quaternion in frame:
                self.assertEqual(position, (0.0, 0.0, 0.0))
                self.assertEqual(quaternion, (0.0, 0.0, 0.0, 0.0))

        # The zero is the weight's doing, not the clip's: the same bones under a
        # weight of 1.0 fall back to their bind values on an unanimated channel.
        alive = CONTRIBUTION_ANIM_INDEX_OFF + ANIM_DESC_STRIDE * 2
        for position, quaternion in mdl_skel.read_anim(image, bones, alive, 1)[0]:
            self.assertNotEqual(quaternion, (0.0, 0.0, 0.0, 0.0))
        self.assertEqual(
            mdl_skel.read_anim(image, bones, alive, 1)[0][1][0], bones[1].pos
        )


class ProceduralRuleExportTests(unittest.TestCase):
    """CAP7.1: the `ProcType == 1` rule table the model exporter carries out.

    A rule's six entries and its axis index are Source quantities and the export's
    change of basis conjugates a bone local, so the table is checked in both
    directions: back into VtMB's basis against the bytes it was read from, and
    forward against the transcription of the rule this module already holds. A
    table that named the wrong axis after conversion passes neither.
    """

    def _image(self, axis: int = 2, rule=None) -> bytes:
        control, _, positions, quaternions = rule or AXIS_RULE
        return model_image(
            TRANSFORM_CHECKSUM,
            TRANSFORM_MODEL,
            bones=PROCEDURAL_BONES,
            procedural={3: (control, axis, positions, quaternions)},
        )

    def _rules(self, image):
        from elysium_pipeline.formats import mdl_gltf, mdl_skel

        return mdl_gltf.axis_interp_rules(image, mdl_skel.read_bones(image))

    def test_the_exporter_carries_the_source_axes_into_the_glb_basis(self) -> None:
        """The three vectors every exported rule is read against.

        Source Y becomes negative glTF Z and Source Z becomes glTF Y, so an export
        that carried the axis index through unchanged would name the wrong one on
        two rules in three. This is the assertion that says so out loud.
        """
        from elysium_pipeline.formats import mdl_gltf

        self.assertTrue(np.allclose(np.asarray(mdl_gltf.M), np.asarray(GLTF_M)))
        self.assertEqual(mdl_gltf.SCALE, GLTF_SCALE)
        self.assertEqual(
            mdl_gltf.DRIVER_AXES,
            [[1.0, 0.0, 0.0], [0.0, 0.0, -1.0], [0.0, 1.0, 0.0]],
        )

    def test_the_table_takes_the_same_basis_change_the_mesh_and_clips_take(self) -> None:
        """The quaternion route the table uses names the rotation `conv_quat` does.

        The table is read back and re-evaluated rather than only drawn, so it goes
        through the quaternion rather than the rotation matrix -- which keeps the
        representative the model authored and inverts to the float32 it was read
        from. That is a different route to the same conjugation, and this is what
        says so: the two agree as rotations to floating-point noise.
        """
        from elysium_pipeline.formats import mdl_gltf

        for quaternion in (*AXIS_RULE[3], OBLIQUE, (0.0, HALF, 0.0, -HALF)):
            with self.subTest(quaternion=quaternion):
                exact = mdl_gltf.conv_quat_exact(quaternion)
                self.assertTrue(
                    np.allclose(
                        mdl_gltf.rot_matrix(exact),
                        mdl_gltf.rot_matrix(mdl_gltf.conv_quat(quaternion)),
                        atol=1.0e-12,
                    )
                )
                self.assertEqual(mdl_gltf.unconv_quat(exact), tuple(quaternion))

    def test_the_exported_table_inverts_to_the_bytes_it_was_read_from(self) -> None:
        from elysium_pipeline.formats import mdl_gltf

        for axis in range(3):
            with self.subTest(axis=axis):
                image = self._image(axis)
                rules, faults = self._rules(image)
                self.assertEqual(faults, [])
                self.assertEqual(len(rules), 1)
                rule = rules[0]
                self.assertEqual(rule["bone"], "Bip01 L Bicep")
                self.assertEqual(rule["bone_index"], 3)
                self.assertEqual(rule["control"], "Bip01 Spine")
                self.assertEqual(rule["control_index"], 1)
                # The axis is carried as a direction, so recovering the index it
                # was written from is a lookup rather than a conversion.
                rebuilt = struct.pack(
                    "<ii", rule["control_index"], mdl_gltf.DRIVER_AXES.index(rule["axis"])
                )
                rebuilt += struct.pack(
                    "<18f",
                    *[c for entry in rule["pos"] for c in mdl_gltf.unconv_pos(entry)],
                )
                rebuilt += struct.pack(
                    "<24f",
                    *[c for entry in rule["quat"] for c in mdl_gltf.unconv_quat(entry)],
                )
                self.assertEqual(rebuilt, _raw_axis_interp(image, 3))

    def test_the_exported_table_evaluates_to_the_converted_correction(self) -> None:
        from elysium_pipeline.formats import mdl_gltf

        locals_ = _bind_locals(PROCEDURAL_BONES)
        # The control bone's bind is oblique, so the driver has three non-zero
        # components on every axis and the whole rule runs rather than landing on
        # one table entry.
        world = _expected_world(PROCEDURAL_BONES, locals_, TRANSFORM_ROOT, split=True)
        converted = [_to_gltf_3x4(matrix) for matrix in world]
        for axis in range(3):
            with self.subTest(axis=axis):
                image = self._image(axis)
                raw = _decoded_axis_interp(_raw_axis_interp(image, 3))
                self.assertEqual(raw[1], axis)
                expected = _to_gltf_3x4(_axis_interp_local(raw, world, PROCEDURAL_BONES))
                rules, _ = self._rules(image)
                produced = _exported_axis_interp_local(
                    rules[0], mdl_gltf.DRIVER_AXES, converted, PROCEDURAL_BONES
                )
                for index, (a, b) in enumerate(zip(produced, expected)):
                    self.assertAlmostEqual(a, b, places=9, msg=f"element {index}")
                # The rule has to be doing work, or agreeing about nothing would
                # pass: the correction is not the bone's own animated local.
                self.assertFalse(
                    np.allclose(produced, _to_gltf_3x4(world[3]), atol=1.0e-3)
                )

    def test_a_rule_that_does_not_resolve_is_a_named_fault(self) -> None:
        rules, faults = self._rules(self._image(rule=(1, 2, *AXIS_RULE[2:])))
        self.assertEqual(len(rules), 1)
        self.assertEqual(faults, [])

        # `ProcIndex` left at zero resolves onto the bone record itself, whose
        # first field is a string index rather than a bone.
        image = model_image(
            TRANSFORM_CHECKSUM, TRANSFORM_MODEL,
            bones=PROCEDURAL_BONES, procedural={3: None},
        )
        rules, faults = self._rules(image)
        self.assertEqual(rules, [])
        self.assertEqual(len(faults), 1)
        self.assertIn("control bone", faults[0])

    def test_a_rule_naming_an_axis_outside_the_three_is_a_named_fault(self) -> None:
        image = bytearray(self._image())
        record = struct.unpack_from("<i", image, 244)[0] + BONE_STRIDE * 3
        offset = record + struct.unpack_from("<i", image, record + 144)[0]
        struct.pack_into("<i", image, offset + 4, 3)
        rules, faults = self._rules(bytes(image))
        self.assertEqual(rules, [])
        self.assertEqual(len(faults), 1)
        self.assertIn("axis 3", faults[0])


# --- CAP5.8: the include-model remap and the route that decides it ------------

# A 56-byte remap record, written field by field so the test states the layout
# rather than importing the reader's own constants for it:
#   short +0x00 source bone, negative selecting the including model's bind pose
#   byte  +0x02 branch selector, non-zero taking the chain rebase
#   byte  +0x03 transform the position through the record matrix
#   short +0x04, +0x06 the chain branch's two bone indices
#   float +0x08 a row-major 3x4
def remap_record(
    source: int,
    *,
    selector: int = 0,
    transform: int = 0,
    chain: tuple[int, int] = (0, 0),
    matrix: tuple[float, ...] = (
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
    ),
) -> bytes:
    record = bytearray(56)
    struct.pack_into("<hBBhh", record, 0, source, selector, transform, *chain)
    struct.pack_into("<12f", record, 8, *matrix)
    return bytes(record)


# A matrix that both rotates a quarter turn about Z and translates, so a
# transformed position is nowhere near the copied one and the witness has
# something to separate.
MOVED_REMAP = (
    0.0, -1.0, 0.0, 10.0,
    1.0, 0.0, 0.0, 20.0,
    0.0, 0.0, 1.0, 30.0,
)


def include_image(model_name: str, includes: tuple[str, ...]) -> bytes:
    """The smallest v2531 header carrying `StudioModelGroup` entries.

    Only `NumIncludeModels`@404, `IncludeModelIndex`@408 and each group's
    `FilenameIndex`@0 matter to the include graph, so nothing else is written.
    """
    groups = 512
    blob = bytearray(groups + 116 * len(includes))
    struct.pack_into("<4sI", blob, 0, b"IDST", 2531)
    blob[12 : 12 + len(model_name)] = model_name.encode("ascii")
    struct.pack_into("<ii", blob, 404, len(includes), groups)
    for index, path in enumerate(includes):
        entry = groups + 116 * index
        struct.pack_into("<i", blob, entry, len(blob) - entry)
        blob += path.encode("ascii") + b"\0"
    return bytes(blob)


class IncludeRemapRecordTests(unittest.TestCase):
    """The 56-byte record as `dispatch_model_pose` reads it.

    Expected values are written by `remap_record` above and by the arithmetic
    spelled out in each test, never by `decoder_pose`, so an agreement is between
    two formulations of the same layout.
    """

    def test_the_record_decodes_every_field_the_dispatcher_reads(self) -> None:
        records = b"".join(
            (
                remap_record(3, transform=1, matrix=MOVED_REMAP),
                remap_record(-1),
                remap_record(0xFFFF - 0x10000, selector=2, chain=(7, 9)),
            )
        )
        remap = decoder_pose.bone_remap(records, 3)
        self.assertEqual(list(remap.source), [3, -1, -1])
        self.assertEqual(list(remap.transform), [True, False, False])
        self.assertEqual(list(remap.chain), [False, False, True])
        self.assertEqual(remap.chain_bones[2].tolist(), [7, 9])
        self.assertTrue(
            np.allclose(remap.matrix[0], np.asarray(MOVED_REMAP).reshape(3, 4))
        )
        # The identity default is what an untransformed record carries, and a
        # reader that mistook the field offset would not land on it.
        self.assertTrue(np.allclose(remap.matrix[1][:, :3], np.eye(3)))

    def test_a_source_of_0xffff_is_minus_one_because_the_field_is_signed(self) -> None:
        # The dispatcher branches on the sign of a `short`, so every value with
        # the top bit set selects the bind pose rather than only the sentinel.
        for raw in (-1, -2, -32768):
            remap = decoder_pose.bone_remap(remap_record(raw), 1)
            self.assertEqual(int(remap.source[0]), -1, msg=f"source {raw}")

    def test_the_transform_is_transformpoint_on_the_bones_whose_byte_is_set(
        self,
    ) -> None:
        records = b"".join(
            (
                remap_record(0, transform=1, matrix=MOVED_REMAP),
                remap_record(1, matrix=MOVED_REMAP),
            )
        )
        remap = decoder_pose.bone_remap(records, 2)
        position = np.array([[[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]]])
        moved = decoder_pose.apply_remap(position, remap, np.array([0, 1]))

        # TransformPoint, written out: row . xyz + row translation.
        matrix = np.asarray(MOVED_REMAP).reshape(3, 4)
        expected = [
            sum(matrix[row][column] * position[0][0][column] for column in range(3))
            + matrix[row][3]
            for row in range(3)
        ]
        self.assertTrue(np.allclose(moved[0][0], expected))
        self.assertTrue(np.allclose(moved[0][0], [8.0, 21.0, 33.0]))
        # The clear byte leaves its bone alone rather than transforming it too.
        self.assertTrue(np.allclose(moved[0][1], [4.0, 5.0, 6.0]))


class IncludeRouteTests(unittest.TestCase):
    """CAP5.8: which contributions the include remap runs for.

    `dispatch_model_pose` walks a group's remap array only after resolving the
    virtual sequence index through that group, so the local path and an owner the
    scene posed an actor on directly both carry no transform.
    """

    TREE = {
        "models/npc.mdl": ("shared/aggregator.mdl",),
        "models/shared/aggregator.mdl": ("shared/bank.mdl", "shared/frenzy.mdl"),
        "models/shared/bank.mdl": (),
        "models/shared/frenzy.mdl": (),
        "models/cinematic/whole_cast.mdl": (),
    }

    def _load(self, key: str) -> bytes | None:
        includes = self.TREE.get(key)
        if includes is None:
            return None
        return include_image(key, includes)

    def test_a_key_normalises_whatever_shape_it_arrives_in(self) -> None:
        for raw in (
            "models/shared/bank.mdl",
            "/models/shared/bank.mdl",
            "models\\shared\\Bank.MDL",
            "shared/bank",
        ):
            self.assertEqual(
                decoder_pose.normalise_model_key(raw), "models/shared/bank.mdl"
            )

    def test_reachability_is_transitive_and_excludes_the_model_itself(self) -> None:
        reached = decoder_pose.include_reachability(self._load, "models/npc.mdl")
        self.assertEqual(
            reached,
            frozenset(
                {
                    "models/shared/aggregator.mdl",
                    "models/shared/bank.mdl",
                    "models/shared/frenzy.mdl",
                }
            ),
        )
        self.assertNotIn("models/npc.mdl", reached)
        self.assertNotIn("models/cinematic/whole_cast.mdl", reached)

    def test_an_unreadable_model_contributes_no_edges_rather_than_raising(self) -> None:
        self.assertEqual(
            decoder_pose.include_reachability(self._load, "models/absent.mdl"),
            frozenset(),
        )

    def test_a_cycle_terminates_and_visits_each_model_once(self) -> None:
        loop = {
            "models/a.mdl": ("b.mdl",),
            "models/b.mdl": ("a.mdl", "c.mdl"),
            "models/c.mdl": (),
        }
        reached = decoder_pose.include_reachability(
            lambda key: include_image(key, loop[key]) if key in loop else None,
            "models/a.mdl",
        )
        self.assertEqual(reached, frozenset({"models/b.mdl", "models/c.mdl"}))

    def _routes(self, groups):
        names = {
            0x01: "models/npc.mdl",
            0x02: "models/shared/bank.mdl",
            0x03: "models/cinematic/whole_cast.mdl",
            0x04: "models/shared/frenzy.mdl",
        }
        return include_routes(names, groups, self._load)

    def test_the_route_follows_the_include_tree_and_nothing_else(self) -> None:
        remap = decoder_pose.bone_remap(
            remap_record(0, transform=1, matrix=MOVED_REMAP), 1
        )
        routes, counts = self._routes(
            {0x01: [("models/shared/aggregator.mdl", remap)]}
        )
        # A bank under the entity model's own group is reached by a group walk.
        self.assertIn((0x01, 0x02), routes)
        # Its own model is the local path, and a whole-cast bank is nobody's
        # include — the scene posed the actor on it above the dispatcher.
        self.assertNotIn((0x01, 0x01), routes)
        self.assertNotIn((0x01, 0x03), routes)
        # `bank` and `frenzy` sit under the group; the model itself and the
        # whole-cast bank do not.
        self.assertEqual(counts["census_pairs_resolved_to_an_include_group"], 2)
        self.assertEqual(counts["census_pairs_matching_more_than_one_include_group"], 0)
        self.assertEqual(counts["remap_bones_the_matched_groups_disagree_on"], 0)

    def test_an_owner_under_two_groups_merges_them_and_counts_the_dispute(
        self,
    ) -> None:
        # The include tree is a DAG: `frenzy` hangs under both groups, so the
        # pair matches twice and the two arrays disagree on the one bone.
        tree = dict(self.TREE)
        tree["models/shared/other.mdl"] = ("shared/frenzy.mdl",)
        self.TREE = tree
        transformed = decoder_pose.bone_remap(
            remap_record(0, transform=1, matrix=MOVED_REMAP), 1
        )
        plain = decoder_pose.bone_remap(remap_record(0), 1)
        routes, counts = self._routes(
            {
                0x01: [
                    ("models/shared/aggregator.mdl", transformed),
                    ("models/shared/other.mdl", plain),
                ]
            }
        )
        self.assertEqual(counts["census_pairs_matching_more_than_one_include_group"], 1)
        self.assertEqual(counts["remap_bones_the_matched_groups_disagree_on"], 1)
        # A disputed bone keeps its untransformed position rather than taking a
        # guess from whichever group was listed first.
        self.assertFalse(bool(routes[(0x01, 0x04)].transform[0]))
        # The bank only the first group reaches keeps its transform.
        self.assertTrue(bool(routes[(0x01, 0x02)].transform[0]))

    def test_an_including_model_the_install_lacks_takes_no_route(self) -> None:
        remap = decoder_pose.bone_remap(remap_record(0, transform=1), 1)
        routes, counts = self._routes({0x09: [("models/shared/bank.mdl", remap)]})
        self.assertEqual(routes, {})
        self.assertEqual(counts["including_models_whose_own_image_the_install_lacks"], 1)

    def test_the_candidate_table_declares_the_three_transform_rules(self) -> None:
        rules = {name: transform for name, _, _, transform, _ in CLIP_CANDIDATES}
        self.assertEqual(rules["complete"], "none")
        self.assertEqual(rules["include_remap"], "always")
        self.assertEqual(rules["include_route"], "route")
        # The ladder attributes a record with the most complete rule available,
        # which is the recovered route and not either half of it.
        self.assertEqual(CLIP_LADDER_CANDIDATE, "include_route")


class RouteWitnessTests(unittest.TestCase):
    """Reading the route out of retail's own captured positions.

    Retail's side of each case is a literal written here; our side is the source
    position the decode produced. The classifier is what is under test, so
    neither input comes from it.
    """

    SOURCE = np.array([[[1.0, 2.0, 3.0]]])
    # `MOVED_REMAP` applied to SOURCE, by hand.
    TRANSFORMED = np.array([[[8.0, 21.0, 33.0]]])

    def _witness(self, retail, *, routed, matrix=MOVED_REMAP):
        counts = {
            name: 0
            for name in (
                "route_witness_transformed",
                "route_witness_copied",
                "route_witness_undecided",
                "route_witness_matches_neither",
                "route_witness_disagrees_with_the_include_graph",
            )
        }
        remap = decoder_pose.bone_remap(
            remap_record(0, transform=1, matrix=matrix), 1
        )
        _fold_route_witness(
            counts,
            {},
            0x01,
            0x02,
            remap,
            routed,
            self.SOURCE,
            np.asarray(retail),
            np.array([[True]]),
            np.array([1]),
            np.array([0]),
        )
        return counts

    def test_retail_landing_on_the_transformed_position_witnesses_the_transform(
        self,
    ) -> None:
        counts = self._witness(self.TRANSFORMED[0], routed=True)
        self.assertEqual(counts["route_witness_transformed"], 1)
        self.assertEqual(counts["route_witness_copied"], 0)
        self.assertEqual(counts["route_witness_disagrees_with_the_include_graph"], 0)

    def test_retail_landing_on_the_source_position_witnesses_the_copy(self) -> None:
        counts = self._witness(self.SOURCE[0], routed=False)
        self.assertEqual(counts["route_witness_copied"], 1)
        self.assertEqual(counts["route_witness_transformed"], 0)
        self.assertEqual(counts["route_witness_disagrees_with_the_include_graph"], 0)

    def test_the_graph_resolving_against_the_witness_is_a_counted_disagreement(
        self,
    ) -> None:
        # The graph says the pair was routed; retail's own bytes say the position
        # was copied. That is the count that would refute the reachability rule.
        counts = self._witness(self.SOURCE[0], routed=True)
        self.assertEqual(counts["route_witness_copied"], 1)
        self.assertEqual(counts["route_witness_disagrees_with_the_include_graph"], 1)

    def test_a_position_matching_neither_candidate_is_its_own_population(self) -> None:
        counts = self._witness([[-40.0, 12.0, 5.0]], routed=True)
        self.assertEqual(counts["route_witness_matches_neither"], 1)
        self.assertEqual(counts["route_witness_transformed"], 0)
        self.assertEqual(counts["route_witness_copied"], 0)

    def test_a_matrix_that_barely_moves_the_bone_witnesses_nothing(self) -> None:
        # Inside the excellent band the two routes are the same answer, so the
        # observation is excluded rather than credited to whichever is nearer.
        near = (
            1.0, 0.0, 0.0, 1.0e-4,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
        )
        counts = self._witness(self.SOURCE[0], routed=True, matrix=near)
        self.assertEqual(counts["route_witness_undecided"], 1)
        self.assertEqual(counts["route_witness_copied"], 0)
        self.assertEqual(counts["route_witness_disagrees_with_the_include_graph"], 0)

    def test_both_refutation_counts_fail_the_verdict_rather_than_being_accounted(
        self,
    ) -> None:
        for name in (
            "route_witness_matches_neither",
            "route_witness_disagrees_with_the_include_graph",
        ):
            self.assertIn(name, TRANSFORM_DEFECTS)
            self.assertNotIn(name, TRANSFORM_ACCOUNTED)


class TransformDifferenceTests(unittest.TestCase):
    """CAP4.3 pass one: the bone-to-world and skin-palette difference.

    Every session is synthetic and game-independent. The retail side is written
    by the hand-rolled helpers at the top of this module, so a stage agreeing
    with the tool is two independent formulations agreeing and not the tool
    agreeing with itself.
    """

    def _payloads(
        self,
        *,
        bones=ROTATED_BONES,
        pose=MOVED_POSE,
        root=TRANSFORM_ROOT,
        split: bool = True,
        selected: set[int] | None = None,
        pose_to_bone: str = "inverse",
        perturb: dict[int, tuple[float, float, float]] | None = None,
        procedural: dict[int, tuple] | None = None,
    ):
        """The captured bone-to-world and palette for one pose, by hand."""
        count = len(bones)
        chosen = set(range(count)) if selected is None else selected
        seed = [IDENTITY_3X4] * count
        world = _expected_world(
            bones, list(pose), root, split=split, seed=seed, selected=chosen,
            procedural=procedural,
        )
        for index, offset in (perturb or {}).items():
            row = list(world[index])
            for axis in range(3):
                row[axis * 4 + 3] += offset[axis]
            world[index] = tuple(row)
        binds = _bind_world(bones)
        inverse = [
            IDENTITY_3X4 if pose_to_bone == "identity" else _invert_rigid_3x4(matrix)
            for matrix in binds
        ]
        skin = [_multiply_3x4(world[i], inverse[i]) for i in range(count)]
        return world, skin, chosen

    def _session(
        self,
        session: Path,
        *,
        bones=ROTATED_BONES,
        pose=MOVED_POSE,
        root=TRANSFORM_ROOT,
        split: bool = True,
        selected: set[int] | None = None,
        pose_to_bone: str = "inverse",
        perturb: dict[int, tuple[float, float, float]] | None = None,
        procedural: dict[int, tuple] | None = None,
        world: list | None = None,
        skin: list | None = None,
        carry_generation: int | None = None,
        client_entity: int = TRANSFORM_RENDERABLE,
        image: bytes | None = None,
        extra_pose: bytes = b"",
        index_it: bool = True,
    ) -> None:
        built, built_skin, chosen = self._payloads(
            bones=bones, pose=pose, root=root, split=split, selected=selected,
            pose_to_bone=pose_to_bone, perturb=perturb, procedural=procedural,
        )
        world = built if world is None else world
        skin = built_skin if skin is None else skin
        count = len(bones)
        write_session(
            session,
            pose_records=pose_record(
                1, 100, TRANSFORM_MODEL,
                bone_count=count,
                checksum=TRANSFORM_CHECKSUM,
                client_entity=client_entity,
                generation=2,
                carry_generation=1 if carry_generation is None else carry_generation,
                bone_to_world=world,
                skin_palette=skin,
            ) + extra_pose,
            animation_records=(
                bracket_record(
                    b"PBLD", 2, 100, 101,
                    generation=1, client_entity=TRANSFORM_RENDERABLE,
                )
                + animation_record(
                    b"FINL", 3, 101,
                    bone_count=count,
                    checksum=TRANSFORM_CHECKSUM,
                    client_entity=TRANSFORM_ENTITY,
                    generation=1,
                    local_pose=list(pose),
                    selected=chosen,
                    root=root,
                )
            ),
            census_records=(
                observation_record(
                    4, 88, TRANSFORM_MODEL,
                    checksum=TRANSFORM_CHECKSUM, bone_count=count,
                )
                + image_record(
                    5, 89,
                    image
                    if image is not None
                    else model_image(
                        TRANSFORM_CHECKSUM, TRANSFORM_MODEL,
                        bones=bones, pose_to_bone=pose_to_bone,
                        procedural=procedural,
                    ),
                    checksum=TRANSFORM_CHECKSUM,
                )
            ),
        )
        finalize(session)
        if index_it:
            index_capture(session, rollup=False)

    def _report(self, directory: str, **kwargs) -> dict:
        session = Path(directory)
        self._session(session, **kwargs)
        return verify_transform(session)

    def _bands(self, report, stage, candidate, metric):
        """One metric's per-bone band split.

        Distinct from `records_by_band`, which counts a record once at its worst
        band over every metric and bone rather than once per bone.
        """
        return report["stages"][stage]["candidates"][candidate][metric][
            "bone_observations_by_band"
        ]

    def _records_by_band(self, report, stage, candidate):
        return report["stages"][stage]["candidates"][candidate]["records_by_band"]

    # ---- stage 3: bone-to-world ----

    def test_bone_to_world_reproduces_the_captured_draw_under_the_split_rule(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory)
        stage = report["stages"]["bone_to_world"]
        self.assertTrue(stage["available"], stage.get("reason"))
        self.assertEqual(stage["records"], 1)
        for metric in ("model_translation", "world_translation", "rotation_degrees"):
            bands = self._bands(report, "bone_to_world", "split", metric)
            self.assertEqual(bands["investigate"] + bands["definite"], 0, metric)
        self.assertTrue(report["verdict"]["transform_difference_compared"])

    def test_the_conventional_hierarchy_differs_only_on_the_flagged_subtree(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory)
        conventional = report["stages"]["bone_to_world"]["candidates"]["conventional"]
        self.assertGreater(conventional["rotation_degrees"]["max"], 0.5)
        self.assertGreater(conventional["world_translation"]["max"], 0.2)
        # One record, counted once at its worst band rather than once per bone.
        self.assertEqual(
            self._records_by_band(report, "bone_to_world", "conventional"),
            {"excellent": 0, "investigate": 0, "definite": 1},
        )
        over = [
            row
            for row in report["clusters"]
            if row["candidate"] == "conventional" and row["stage"] == "bone_to_world"
        ]
        # Bone 2 carries `Flags & 0x2` and bone 3 hangs below it; nothing above
        # bone 2 can move, because the two rules agree everywhere else.
        self.assertEqual({row["bone"] for row in over}, {2, 3})
        self.assertGreater(
            report["stages"]["bone_to_world"]["counts"][
                "stage3_conventional_hierarchy_over_the_band"
            ],
            0,
        )

    def test_each_metric_carries_its_own_record_band_split(self) -> None:
        """A clean translation beside a wrong rotation must stay readable.

        Collapsing the metrics into one worst-of number would report a stage
        whose positions are exact and whose rotations are not as uniformly
        broken, which is the opposite of what such a run is evidence for.
        """
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory)
        candidate = report["stages"]["bone_to_world"]["candidates"]["conventional"]
        self.assertEqual(
            candidate["rotation_degrees"]["records_by_band"]["definite"], 1
        )
        self.assertEqual(
            candidate["model_translation"]["records_by_band"]["definite"], 1
        )
        # The combined figure counts the record once, not once per metric.
        self.assertEqual(sum(candidate["records_by_band"].values()), 1)

    def test_the_flagged_bones_are_reported_apart_from_the_ordinary_ones(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory)
        candidates = report["stages"]["bone_to_world"]["candidates"]
        split = candidates["split"]["rotation_degrees"][
            "bone_observations_by_inheritance"
        ]
        ordinary = candidates["conventional"]["rotation_degrees"][
            "bone_observations_by_inheritance"
        ]
        # One bone of the four carries `Flags & 0x2`. The split rule is right on
        # it and the ordinary hierarchy is wrong on it, which is the whole claim
        # the two candidates exist to separate.
        self.assertEqual(split["split_inheritance"]["excellent"], 1)
        self.assertEqual(split["split_inheritance"]["definite"], 0)
        self.assertEqual(ordinary["split_inheritance"]["definite"], 1)
        self.assertEqual(ordinary["split_inheritance"]["excellent"], 0)

    def test_a_procedural_bone_is_labelled_and_reported_apart(self) -> None:
        """`ProcType != 0` names a stage the plain hierarchy does not evaluate.

        A hierarchy difference and a missing procedural stage look identical in
        the numbers and are different work, so the classification comes from
        what the bone is rather than from how large its error was.
        """
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(
                directory, procedural={3: None}, perturb={3: (0.0, 0.0, 9.0)}
            )
        cluster = next(
            row
            for row in report["clusters"]
            if row["candidate"] == "split" and row["bone"] == 3
        )
        self.assertEqual(cluster["proc_type"], 1)
        self.assertEqual(
            cluster["candidate_cause"], "controllers and procedural order"
        )
        split = report["stages"]["bone_to_world"]["candidates"]["split"][
            "world_translation"
        ]["bone_observations_by_procedural"]
        self.assertEqual(split["procedural"]["definite"], 1)
        self.assertEqual(split["ordinary"]["definite"], 0)

    def test_an_unreadable_procedural_rule_is_counted_and_the_model_still_compares(
        self,
    ) -> None:
        """A rule that will not read costs its bone the correction, not the model."""
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory, procedural={3: None})
        self.assertEqual(
            report["skeletons"]["counts"]["procedural_rule_unreadable"], 1
        )
        self.assertEqual(report["skeletons"]["skeletons"], 1)
        self.assertTrue(report["stages"]["bone_to_world"]["available"])
        self.assertTrue(report["verdict"]["transform_difference_compared"])

    def test_the_procedural_candidate_reproduces_a_draw_the_rule_produced(
        self,
    ) -> None:
        """The rule is what closes the gap, and its absence is what opens it.

        The captured side applies the axis-interpolation table; the plain
        hierarchy cannot reach it, and the candidate that evaluates the table
        does. Both sides are built from independent transcriptions of the rule.
        """
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory, procedural={3: AXIS_RULE})
        candidates = report["stages"]["bone_to_world"]["candidates"]
        for metric in ("world_translation", "rotation_degrees"):
            with_rule = candidates["split_procedural"][metric]["records_by_band"]
            self.assertEqual(
                with_rule["investigate"] + with_rule["definite"], 0, metric
            )
        # Without it the driven bone is somewhere else entirely.
        self.assertGreater(
            candidates["split"]["rotation_degrees"]["max"], 0.5
        )
        self.assertEqual(
            report["stages"]["bone_to_world"]["counts"][
                "stage3_procedural_rule_over_the_band"
            ],
            0,
        )
        self.assertGreater(
            report["stages"]["bone_to_world"]["counts"][
                "stage3_records_in_the_definite_band"
            ],
            0,
        )

    def test_the_ladder_does_not_report_a_difference_the_rule_explains(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory, procedural={3: AXIS_RULE})
        self.assertEqual(report["ladder"]["by_stage"]["bone_to_world"], 0)
        self.assertEqual(report["ladder"]["by_stage"]["none"], 1)
        self.assertIn("split_procedural", report["ladder"]["population"])

    def test_the_root_transform_is_divided_out_for_the_model_space_metric(
        self,
    ) -> None:
        """Two runs differing only in where the entity stands.

        The model-space numbers must be identical and the world-space ones must
        not, which is what makes entity placement separable from the
        composition rule rather than folded into it.
        """
        moved = (0.0, -1.0, 0.0, 900.0, 1.0, 0.0, 0.0, -4.0, 0.0, 0.0, 1.0, 3.0)
        reports = []
        for root in (TRANSFORM_ROOT, moved):
            with tempfile.TemporaryDirectory() as directory:
                # A wrong palette in both runs so there is a non-zero number to
                # compare rather than two zeros agreeing trivially.
                reports.append(
                    self._report(
                        directory, root=root, perturb={3: (0.0, 0.0, 5.0)}
                    )
                )
        first, second = (
            report["stages"]["bone_to_world"]["candidates"]["split"]
            for report in reports
        )
        self.assertAlmostEqual(
            first["model_translation"]["max"],
            second["model_translation"]["max"],
            places=4,
        )
        self.assertGreater(first["model_translation"]["max"], 0.2)

    def test_an_unselected_bone_is_seeded_from_retail_and_never_compared(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory, selected={0, 1, 3})
        stage = report["stages"]["bone_to_world"]
        self.assertEqual(stage["counts"]["bones_outside_the_selected_mask"], 1)
        # Bone 3's parent is bone 2, which retail did not write, so bone 3
        # composes off retail's own value for it.
        self.assertEqual(
            stage["counts"][
                "bones_seeded_from_retail_because_their_parent_was_unselected"
            ],
            1,
        )
        bands = self._bands(report, "bone_to_world", "split", "world_translation")
        self.assertEqual(bands["investigate"] + bands["definite"], 0)

    def test_a_wrong_bone_is_named_and_its_descendants_carry_it(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory, perturb={1: (0.0, 0.0, 4.0)})
        first = report["ladder"]["by_bone"][0]
        self.assertEqual(first["stage"], "bone_to_world")
        self.assertEqual(first["bone"], 1)
        spread = report["ladder"]["descendant_propagation"][0]
        self.assertEqual(spread["first_bone"], "Bip01 Spine")
        # Only the perturbed bone moved, so the subtree below it is not carrying
        # the error and the rate says so rather than the report implying spread.
        self.assertEqual(spread["non_descendants"]["over_band"], 0)

    def test_a_root_transform_that_is_not_rigid_is_excluded_rather_than_inverted(
        self,
    ) -> None:
        """A singular root has no inverse, so the record leaves the comparison.

        The theatre corpus carries such roots on four named NPCs, so this is a
        property of the run rather than a broken read; the distinct count is
        what tells the two apart, and it is reported beside the total.
        """
        singular = (
            0.0, 1.0, -329.8, -3374.25,
            0.0, 1.0, 0.0, 907.575,
            0.0, 0.0, 1.0, -321.8,
        )
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory, root=singular)
        stage = report["stages"]["bone_to_world"]
        self.assertEqual(stage["counts"]["root_transform_not_rigid"], 1)
        self.assertEqual(stage["distinct_non_rigid_roots"], 1)
        self.assertGreater(stage["worst_non_rigid_root_row_length"], 1.0)
        self.assertEqual(stage["records"], 0)
        self.assertEqual(report["verdict"]["defects"], {})
        self.assertIn(
            "root_transform_not_rigid", report["verdict"]["accounted_counts"]
        )
        # The palette stage does not depend on the root, so it still runs.
        self.assertEqual(report["stages"]["skin_palette"]["records"], 1)
        self.assertTrue(report["verdict"]["transform_difference_compared"])

    def test_a_non_topological_parent_is_a_defect(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(
                directory,
                bones=(
                    ("Bip01", -1, (0.0, 0.0, 0.0)),
                    ("Bip01 Spine", 1, (1.0, 0.0, 0.0)),
                ),
                pose=MOVED_POSE[:2],
            )
        self.assertEqual(
            report["skeletons"]["counts"]["skeleton_parent_not_topological"], 1
        )
        self.assertFalse(report["verdict"]["transform_difference_compared"])

    # ---- stage 4: the skin palette ----

    def test_the_skin_palette_is_the_bone_to_world_times_pose_to_bone(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory)
        stage = report["stages"]["skin_palette"]
        self.assertTrue(stage["available"], stage.get("reason"))
        self.assertEqual(stage["records"], 1)
        for metric in ("translation", "rotation_degrees"):
            bands = self._bands(report, "skin_palette", "pose_to_bone", metric)
            self.assertEqual(bands["investigate"] + bands["definite"], 0, metric)

    def test_a_pose_to_bone_that_is_not_the_conventional_inverse_is_priced(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory, pose_to_bone="identity")
        # The stored bind still reproduces the palette retail wrote, because
        # retail wrote it with that bind; what changes is what the regenerated
        # one costs.
        bands = self._bands(report, "skin_palette", "pose_to_bone", "translation")
        self.assertEqual(bands["investigate"] + bands["definite"], 0)
        self.assertGreater(
            report["stages"]["skin_palette"]["counts"][
                "stage4_regenerated_inverse_bind_over_the_band"
            ],
            0,
        )
        self.assertGreater(report["static_bind"]["bones_over_the_band"], 0)

    def test_a_wrong_palette_is_reported_as_the_skin_stage(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            world, skin, _ = self._payloads()
            broken = list(skin)
            row = list(broken[2])
            row[7] += 3.0
            broken[2] = tuple(row)
            self._session(session, world=world, skin=broken)
            report = verify_transform(session)
        bands = self._bands(report, "skin_palette", "pose_to_bone", "translation")
        self.assertGreater(bands["definite"], 0)
        # The bone-to-world stage is clean, so the ladder must attribute the
        # record to the palette rather than to the stage before it.
        self.assertEqual(report["ladder"]["by_stage"]["bone_to_world"], 0)
        self.assertEqual(report["ladder"]["by_stage"]["skin_palette"], 1)

    def test_a_bone_the_renderer_never_wrote_is_excluded_from_both_metrics(
        self,
    ) -> None:
        """Two zero matrices are identical, and the rotation metric says 120.

        `acos((trace(0) - 1) / 2)` is `acos(-0.5)`, so an unwritten slot reads
        as the largest possible disagreement while the translation reads as a
        perfect one. Neither is a comparison, so the bone leaves both.
        """
        zero = (0.0,) * 12
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            world, skin, _ = self._payloads()
            world, skin = list(world), list(skin)
            world[3], skin[3] = zero, zero
            self._session(session, world=world, skin=skin)
            report = verify_transform(session)
        for stage in ("bone_to_world", "skin_palette"):
            self.assertEqual(
                report["stages"][stage]["counts"]["bones_the_renderer_never_wrote"],
                1,
                stage,
            )
        rotation = self._bands(
            report, "skin_palette", "pose_to_bone", "rotation_degrees"
        )
        self.assertEqual(rotation["investigate"] + rotation["definite"], 0)
        self.assertTrue(report["verdict"]["transform_difference_compared"])

    def test_a_draw_with_no_composed_pose_still_reaches_the_palette_stage(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory, carry_generation=99)
        self.assertFalse(report["stages"]["bone_to_world"]["available"])
        self.assertEqual(report["stages"]["skin_palette"]["records"], 1)
        self.assertEqual(
            report["pairing"]["counts"][
                "draws_whose_consumed_pose_build_produced_no_composed_pose"
            ],
            1,
        )
        self.assertTrue(report["verdict"]["transform_difference_compared"])

    def test_a_draw_naming_another_actor_is_counted_rather_than_compared(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory, client_entity=TRANSFORM_ENTITY + 64)
        self.assertFalse(report["stages"]["bone_to_world"]["available"])
        self.assertEqual(
            report["pairing"]["counts"][
                "draws_whose_carry_generation_names_another_actor"
            ],
            1,
        )
        self.assertTrue(report["verdict"]["transform_difference_compared"])

    # ---- mechanics, verdict and shape ----

    def test_a_repeated_payload_is_compared_once_and_counted_many_times(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            world, skin, _ = self._payloads()
            extra = b"".join(
                pose_record(
                    ordinal, 100 + ordinal, TRANSFORM_MODEL,
                    bone_count=len(ROTATED_BONES),
                    checksum=TRANSFORM_CHECKSUM,
                    client_entity=TRANSFORM_RENDERABLE,
                    generation=2,
                    carry_generation=1,
                    bone_to_world=world,
                    skin_palette=skin,
                )
                for ordinal in (11, 12, 13)
            )
            self._session(session, extra_pose=extra)
            report = verify_transform(session)
            compact(session)
            compacted = verify_transform(session)
        self.assertEqual(report["stages"]["skin_palette"]["records"], 4)
        self.assertEqual(compacted["stages"]["skin_palette"]["records"], 4)
        # Uncompacted, identity is the row; compacted, it is the bytes.
        self.assertEqual(report["stages"]["skin_palette"]["distinct_payload_keys"], 4)
        self.assertEqual(
            compacted["stages"]["skin_palette"]["distinct_payload_keys"], 1
        )
        self.assertEqual(
            report["stages"]["skin_palette"]["candidates"],
            compacted["stages"]["skin_palette"]["candidates"],
        )
        self.assertEqual(
            report["pairing"]["counts"]["payload_store_absent"], 4
        )
        self.assertNotIn(
            "payload_store_absent", compacted["pairing"]["counts"]
        )

    def test_two_runs_over_one_session_produce_the_same_report(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._session(session)
            first = verify_transform(session)
            second = verify_transform(session)
        for report in (first, second):
            for stage in ("bone_to_world", "skin_palette"):
                report["stages"][stage].pop("elapsed_seconds", None)
        self.assertEqual(first, second)

    def test_the_sample_bound_is_counted_rather_than_silently_dropping_records(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._session(session)
            report = verify_transform(session, sample=0, batch=1)
        excluded = report["stages"]["skin_palette"]["counts"][
            "records_excluded_by_the_sample_bound"
        ]
        self.assertEqual(excluded, 1)
        self.assertIn(
            "records_excluded_by_the_sample_bound",
            report["verdict"]["accounted_counts"],
        )

    def test_every_reported_list_is_bounded_by_its_cap(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory, perturb={1: (0.0, 0.0, 4.0)})
        self.assertLessEqual(len(report["clusters"]), MAX_REPORTED_CLUSTERS)
        self.assertLessEqual(len(report["worst_bones"]), MAX_REPORTED_BONES)
        self.assertLessEqual(len(report["ladder"]["by_bone"]), MAX_REPORTED_BONES)
        self.assertLessEqual(len(report["static_bind"]["worst"]), MAX_REPORTED_MODELS)
        for row in report["clusters"]:
            self.assertLessEqual(len(row["exemplars"]), MAX_REPORTED_EXEMPLARS)

    def test_every_emitted_population_is_declared_once(self) -> None:
        self.assertEqual(set(TRANSFORM_DEFECTS) & set(TRANSFORM_ACCOUNTED), set())
        emitted: set[str] = set()
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory, perturb={1: (0.0, 0.0, 4.0)})
        sections = [report["skeletons"], report["pairing"], report["static_bind"]]
        sections += [report["stages"][name] for name in ("bone_to_world", "skin_palette")]
        for section in sections:
            emitted |= set(section.get("counts") or {})
        undeclared = emitted - set(TRANSFORM_DEFECTS) - set(TRANSFORM_ACCOUNTED)
        self.assertEqual(undeclared, set())

    def test_a_mismatch_is_accounted_rather_than_a_defect(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory, perturb={1: (0.0, 0.0, 40.0)})
        self.assertGreater(
            report["stages"]["bone_to_world"]["counts"][
                "stage3_records_in_the_definite_band"
            ],
            0,
        )
        self.assertEqual(report["verdict"]["defects"], {})
        self.assertTrue(report["verdict"]["transform_difference_compared"])

    def test_an_absent_entity_image_is_a_defect(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._session(session)
            connection = sqlite3.connect(session / "capture.sqlite")
            try:
                connection.execute("DELETE FROM model_images")
                connection.commit()
            finally:
                connection.close()
            report = verify_transform(session)
        self.assertGreater(
            report["stages"]["skin_palette"]["counts"]["entity_image_absent"], 0
        )
        self.assertFalse(report["verdict"]["transform_difference_compared"])

    def test_a_database_without_the_spine_is_unjudgeable_rather_than_failing(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._session(session, index_it=False)
            report = verify_transform(session)
        self.assertFalse(report["verdict"]["judgeable"])
        self.assertFalse(report["verdict"]["transform_difference_compared"])
        self.assertIn("index_capture_database", report["verdict"]["statement"])

    def test_a_session_with_no_contributions_reports_a_reason_per_stage(
        self,
    ) -> None:
        """The clip stages need a bound `BASE`; without one they say so.

        A session carrying draws and composed poses but no contribution stream
        can still be differenced at the two composition stages, so the clip
        stages report why they were not reached rather than the whole pass
        refusing to judge.
        """
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory)
        for name in ("decoded_locals", "composed_locals"):
            stage = report["stages"][name]
            self.assertFalse(stage["available"])
            self.assertIn("contribution", stage["reason"])
        self.assertFalse(report["binding"]["available"])
        self.assertTrue(report["verdict"]["transform_difference_compared"])

    def test_nothing_is_written_into_the_capture(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._session(session)
            database = session / "capture.sqlite"
            before = hashlib.sha256(database.read_bytes()).hexdigest()
            report = verify_transform(session)
            (session / REPORT_NAME).write_text(json.dumps(report))
            after = hashlib.sha256(database.read_bytes()).hexdigest()
        self.assertEqual(before, after)

    def test_the_report_names_the_decoder_the_evaluator_and_the_exporter(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory)
        identity = report["identity"]
        self.assertEqual(identity["decoder_module"], decoder_pose.SHIPPED)
        for key in ("decoder_sha256", "evaluator_sha256", "exporter_sha256"):
            self.assertRegex(identity[key], r"^[0-9a-f]{64}$")
        self.assertIn("11.3", identity["bands"]["source"])
        self.assertGreater(identity["bands"]["quantile_resolution"], 1.0)

    def test_the_report_states_that_the_composition_stages_are_not_shipped_code(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory)
        for name in ("bone_to_world", "skin_palette"):
            self.assertIn(
                "no shipped Elysium implementation",
                report["stages"][name]["implementation"],
            )

    def test_the_summary_names_the_first_mismatching_stage(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory, perturb={1: (0.0, 0.0, 4.0)})
        text = summarize_transform(report)
        self.assertIn("first mismatching stage", text)
        self.assertIn("first mismatching bone", text)
        self.assertIn("bone_to_world", text)

    def test_a_cluster_key_is_stable_across_two_sessions(self) -> None:
        keys = []
        for _ in range(2):
            with tempfile.TemporaryDirectory() as directory:
                report = self._report(directory, perturb={1: (0.0, 0.0, 4.0)})
            keys.append([row["cluster_key"] for row in report["clusters"]])
        self.assertEqual(keys[0], keys[1])
        self.assertTrue(all("Bip01" in key for key in keys[0]))

    def test_two_sessions_are_reported_side_by_side(self) -> None:
        reports = []
        for _ in range(2):
            with tempfile.TemporaryDirectory() as directory:
                reports.append(self._report(directory, perturb={1: (0.0, 0.0, 4.0)}))
        comparison = compare_transform(reports)
        self.assertEqual(len(comparison["bone_to_world_split_procedural"]), 2)
        self.assertTrue(comparison["clusters_in_every_run"])
        self.assertIn("coverage", comparison["statement"])



# --- CAP4.3 pass two: the decoded and composed locals -------------------------
#
# The clip stages measure the shipped decoder, so a fixture has to carry a real
# animation block: RLE runs, per-bone records, and an animdesc the witnessed
# animation index reaches. `posscale` and `rotscale` are 1.0 in `model_image`,
# so a hand-written sample is the raw key itself and the oracle below owes
# nothing to the code it checks.

CLIP_FRAMES = 5
# One track per bone, each on a different channel, so a wrong channel order
# shows as a wrong bone rather than as noise. Bone 2 carries `Flags & 0x2` and
# bone 3 is its child, which keeps the split subtree animated.
THEATRE_CLIP = Clip(
    "@theatre",
    CLIP_FRAMES,
    tracks={
        (0, 0): Track((CLIP_FRAMES, (10, 20, 30, 40, 50))),
        (1, 2): Track((CLIP_FRAMES, (-4, -3, -2, -1, 0))),
        (2, 6): Track((CLIP_FRAMES, (2, 3, 4, 5, 6))),
        (3, 4): Track((CLIP_FRAMES, (1, 2, 3, 4, 5))),
    },
)
THEATRE_CLIPS = (THEATRE_CLIP,)

# A bank holding two complete bipeds, the shape a cinematic model has. The
# `Bip02` chain carries the entity's own binds and is the one the mask selects;
# the `Bip01` chain deliberately does not, so matching on the plain name reaches
# a real bone with the wrong pose rather than reaching nothing.
FAMILY_BONES = (
    ("Bip01", -1, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0), 0),
    ("Bip01 Spine", 0, (9.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0), 0),
    ("Bip01 Neck", 1, (0.0, 9.0, 0.0), (0.0, 0.0, 0.0, 1.0), 0x2),
    ("Bip01 Head", 2, (0.0, 0.0, 9.0), (0.0, 0.0, 0.0, 1.0), 0),
    ("Bip02", -1, (0.0, 0.0, 0.0), (0.0, 0.0, HALF, HALF), 0),
    ("Bip02 Spine", 4, (1.0, 2.0, 3.0), (HALF, 0.0, 0.0, HALF), 0),
    ("Bip02 Neck", 5, (0.0, 4.0, 0.0), (0.0, HALF, 0.0, HALF), 0x2),
    ("Bip02 Head", 6, (0.0, 1.0, 0.0), (0.0, 0.0, 0.0, 1.0), 0),
    # A bone the bank has and the character does not, which is the ordinary
    # shape of a shared bank: most of what it decodes belongs to somebody else.
    ("Bip02 Ponytail1", 6, (0.0, 2.0, 0.0), (0.0, 0.0, 0.0, 1.0), 0),
)
FAMILY_CLIP = Clip(
    "@theatre",
    CLIP_FRAMES,
    tracks={
        (bone + 4, channel): track
        for (bone, channel), track in THEATRE_CLIP.tracks.items()
    },
)
FAMILY_CLIPS = (FAMILY_CLIP,)
FAMILY_CHECKSUM = 0x3100
FAMILY_MODEL = "models/bank.mdl"
#: Where the bank's second biped starts, which is the chain the entity's own
#: `Bip01` bones correspond to.
FAMILY_OFFSET = 4

# The same bank with its spare bone named for a camera dummy rather than for a
# biped. A bone belonging to no `BipNN` chain is what 41 of the 85 multi-actor
# cinematic models carry; `export_cinematic` writes it into no per-root bank, and
# a contribution whose mask selects only bones like it names no family at all.
SPLIT_BONES = FAMILY_BONES[:-1] + (
    ("Dummy01", 6, (0.0, 2.0, 0.0), (0.0, 0.0, 0.0, 1.0), 0),
)
SPLIT_CLIPS = FAMILY_CLIPS
#: The index of that bone in `SPLIT_BONES`.
SPLIT_DUMMY = len(SPLIT_BONES) - 1


def _track_samples(track: Track) -> list[int]:
    """One channel's per-frame keys, expanded by hand from its runs."""
    keys: list[int] = []
    for total, run in track.runs:
        for frame in range(total):
            keys.append(run[min(frame, len(run) - 1)])
    return keys


def _clip_frame(bones, clip: Clip, frame: int):
    """One frame of a clip, decoded by hand.

    Position is a delta on the bind and rotation replaces the bind component,
    which is A.4's asymmetry and the one thing worth writing out twice.
    """
    out = []
    for index, entry in enumerate(bones):
        _, _, position, quaternion, _ = _bone_spec(entry)
        pos = list(position)
        quat = list(quaternion)
        for channel in range(CHANNEL_COUNT):
            track = clip.tracks.get((index, channel))
            if track is None:
                continue
            keys = _track_samples(track)
            sample = keys[min(frame, len(keys) - 1)]
            if channel < 3:
                pos[channel] = position[channel] + sample
            else:
                quat[channel - 3] = float(sample)
        norm = math.sqrt(sum(value * value for value in quat)) or 1.0
        out.append((tuple(pos), tuple(value / norm for value in quat)))
    return out


def _clip_local(bones, clip: Clip, frame: int, fraction: float):
    """A clip sampled between two frames, by hand."""
    first = _clip_frame(bones, clip, frame)
    if not fraction:
        return first
    second = _clip_frame(bones, clip, min(frame + 1, clip.numframes - 1))
    out = []
    for (position, quaternion), (next_position, next_quaternion) in zip(first, second):
        mixed = [
            a * (1.0 - fraction) + b * fraction
            for a, b in zip(quaternion, next_quaternion)
        ]
        norm = math.sqrt(sum(value * value for value in mixed)) or 1.0
        out.append(
            (
                tuple(
                    a * (1.0 - fraction) + b * fraction
                    for a, b in zip(position, next_position)
                ),
                tuple(value / norm for value in mixed),
            )
        )
    return out


class ClipStageTests(unittest.TestCase):
    """CAP4.3 pass two: the decoded locals, the composed locals, and the chain.

    Every session is synthetic and game-independent. The pose retail is said to
    have produced is written by the hand-rolled decoder above, so the shipped
    decoder agreeing with it is two independent readings of the same bytes
    agreeing and not the decoder agreeing with itself.
    """

    def _session(
        self,
        session: Path,
        *,
        bones=ROTATED_BONES,
        clips: tuple[Clip, ...] = THEATRE_CLIPS,
        owner_bones=None,
        owner_clips: tuple[Clip, ...] | None = None,
        cycle: float = CONTRIBUTION_CYCLE,
        animation_index: int = 0,
        mask_bones: tuple[int, ...] | None = None,
        base_pose=None,
        final_pose=None,
        selected: set[int] | None = None,
        sample_phase: float | None = None,
        perturb: dict[int, tuple[float, float, float]] | None = None,
        second_mask_bones: tuple[int, ...] | None = None,
    ) -> None:
        """One pose build: a fired contribution, its base pose, and its draw.

        `second_mask_bones` adds a second build on the same entity and owner
        whose contribution selects a different mask. It is what a per-pair fact
        is tested with: one build's mask can name a biped family while the
        other's names none, and the export ships one bank for both.
        """
        count = len(bones)
        owner = bones if owner_bones is None else owner_bones
        owner_clips = clips if owner_clips is None else owner_clips
        owner_count = len(owner)
        cross = owner_bones is not None
        checksum = FAMILY_CHECKSUM if cross else TRANSFORM_CHECKSUM
        frame = sampled_frame(owner_clips[animation_index].numframes, cycle)
        fraction = (owner_clips[animation_index].numframes - 1) * cycle - frame
        decoded = _clip_local(owner, owner_clips[animation_index], frame, fraction)
        if base_pose is None:
            base_pose = (
                decoded[FAMILY_OFFSET : FAMILY_OFFSET + count] if cross else decoded
            )
        if perturb:
            base_pose = list(base_pose)
            for index, offset in perturb.items():
                position, quaternion = base_pose[index]
                base_pose[index] = (
                    tuple(a + b for a, b in zip(position, offset)),
                    quaternion,
                )
        final_pose = base_pose if final_pose is None else final_pose
        chosen = set(range(count)) if selected is None else selected
        world = _expected_world(
            bones, list(final_pose), TRANSFORM_ROOT, split=True,
            seed=[IDENTITY_3X4] * count, selected=chosen,
        )
        binds = _bind_world(bones)
        skin = [
            _multiply_3x4(world[index], _invert_rigid_3x4(binds[index]))
            for index in range(count)
        ]
        write_session(
            session,
            pose_records=pose_record(
                6, 106, TRANSFORM_MODEL,
                bone_count=count,
                checksum=TRANSFORM_CHECKSUM,
                client_entity=TRANSFORM_RENDERABLE,
                generation=2,
                carry_generation=1,
                bone_to_world=world,
                skin_palette=skin,
            ),
            animation_records=(
                animation_record(
                    b"BASE", 3, 103,
                    bone_count=count,
                    checksum=TRANSFORM_CHECKSUM,
                    client_entity=TRANSFORM_ENTITY,
                    generation=1,
                    local_pose=list(base_pose),
                    selected=chosen,
                    sample_phase=cycle if sample_phase is None else sample_phase,
                )
                + animation_record(
                    b"FINL", 4, 104,
                    bone_count=count,
                    checksum=TRANSFORM_CHECKSUM,
                    client_entity=TRANSFORM_ENTITY,
                    generation=1,
                    local_pose=list(final_pose),
                    selected=chosen,
                    root=TRANSFORM_ROOT,
                )
                + bracket_record(
                    b"PBLD", 5, 100, 105,
                    generation=1, client_entity=TRANSFORM_RENDERABLE,
                )
                + (
                    animation_record(
                        b"BASE", 13, 113,
                        bone_count=count,
                        checksum=TRANSFORM_CHECKSUM,
                        client_entity=TRANSFORM_ENTITY,
                        generation=3,
                        local_pose=list(base_pose),
                        selected=chosen,
                        sample_phase=cycle if sample_phase is None else sample_phase,
                    )
                    + bracket_record(
                        b"PBLD", 14, 110, 115,
                        generation=3, client_entity=TRANSFORM_RENDERABLE,
                    )
                    if second_mask_bones is not None
                    else b""
                )
            ),
            contribution_records=(
                contribution_record(
                    b"ANIM", 1, 101,
                    bones=owner_count,
                    checksum=checksum,
                    animation_index=animation_index,
                    channel_bones=(
                        mask_bones
                        if mask_bones is not None
                        else tuple(range(owner_count))
                    ),
                    clips=owner_clips,
                    image_bones=owner_count,
                    cycle=cycle,
                )
                + contribution_record(
                    b"SEQP", 2, 102,
                    bones=owner_count,
                    checksum=checksum,
                    sequence_index=0,
                    mask_bones=(
                        mask_bones
                        if mask_bones is not None
                        else tuple(range(owner_count))
                    ),
                    clips=owner_clips,
                    image_bones=owner_count,
                    cycle=cycle,
                )
                + (
                    contribution_record(
                        b"ANIM", 11, 111,
                        scope=2,
                        generation=3,
                        bones=owner_count,
                        checksum=checksum,
                        animation_index=animation_index,
                        channel_bones=second_mask_bones,
                        clips=owner_clips,
                        image_bones=owner_count,
                        cycle=cycle,
                    )
                    + contribution_record(
                        b"SEQP", 12, 112,
                        scope=2,
                        generation=3,
                        bones=owner_count,
                        checksum=checksum,
                        sequence_index=0,
                        mask_bones=second_mask_bones,
                        clips=owner_clips,
                        image_bones=owner_count,
                        cycle=cycle,
                    )
                    if second_mask_bones is not None
                    else b""
                )
            ),
            census_records=(
                observation_record(
                    7, 88, TRANSFORM_MODEL,
                    checksum=TRANSFORM_CHECKSUM, bone_count=count,
                )
                + image_record(
                    8, 89,
                    model_image(
                        TRANSFORM_CHECKSUM, TRANSFORM_MODEL,
                        bones=bones, clips=clips,
                    ),
                    checksum=TRANSFORM_CHECKSUM,
                )
                + (
                    observation_record(
                        9, 90, FAMILY_MODEL,
                        checksum=FAMILY_CHECKSUM, bone_count=owner_count,
                    )
                    + image_record(
                        10, 91,
                        model_image(
                            FAMILY_CHECKSUM, FAMILY_MODEL,
                            bones=owner, clips=owner_clips,
                        ),
                        checksum=FAMILY_CHECKSUM,
                    )
                    if cross
                    else b""
                )
            ),
        )
        finalize(session)
        index_capture(session, rollup=False)

    def _report(self, directory: str, **kwargs) -> dict:
        session = Path(directory)
        self._session(session, **kwargs)
        return verify_transform(session)

    def _stage(self, report, name):
        stage = report["stages"][name]
        self.assertTrue(stage["available"], stage.get("reason"))
        return stage

    def _bands(self, report, stage, candidate):
        return report["stages"][stage]["candidates"][candidate]["records_by_band"]

    # ---- the binding ----

    def test_a_base_pose_binds_to_the_contribution_nested_below_it(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory)
        binding = report["binding"]
        self.assertTrue(binding["available"], binding.get("reason"))
        self.assertEqual(binding["base_evaluations"], 1)
        self.assertEqual(binding["paired_with_cells"], 1)
        self.assertEqual(binding["base_records_with_no_paired_contribution"], 0)
        self.assertEqual(binding["paired_records_whose_cycle_disagrees"], 0)
        self.assertEqual(
            binding["paired_records_whose_renderable_offset_disagrees"], 0
        )

    def test_a_cycle_that_disagrees_is_a_defect_rather_than_a_finding(self) -> None:
        """The pairing is checked, not assumed.

        Ranking two streams against each other would pair a base pose with the
        wrong sequence silently. Requiring the cycle to agree turns that into a
        stopped run, because differencing one sequence's decode against another
        sequence's output would report a decoder failure that is not there.
        """
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory, sample_phase=0.75)
        self.assertFalse(report["verdict"]["transform_difference_compared"])
        self.assertIn(
            "paired_records_whose_cycle_disagrees", report["verdict"]["defects"]
        )

    # ---- stage 1: the decoded locals ----

    def test_the_shipped_decoder_reproduces_the_captured_base_pose(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory)
        stage = self._stage(report, "decoded_locals")
        self.assertEqual(stage["records"], 1)
        self.assertEqual(stage["clips_decoded"], 1)
        self.assertIn("mdl_skel", stage["our_side"])
        self.assertEqual(
            self._bands(report, "decoded_locals", "complete"),
            {"excellent": 1, "investigate": 0, "definite": 0},
        )

    def test_a_wrong_local_names_its_bone_and_leaves_the_band(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory, perturb={2: (0.0, 0.0, 3.0)})
        self.assertEqual(
            self._bands(report, "decoded_locals", "complete"),
            {"excellent": 0, "investigate": 0, "definite": 1},
        )
        named = [
            row
            for row in report["worst_bones"]
            if row["stage"] == "decoded_locals" and row["candidate"] == "complete"
        ]
        self.assertEqual({row["bone"] for row in named}, {2})
        self.assertEqual(named[0]["bone_name"], "Bip01 Neck")

    def test_the_frame_key_candidate_prices_the_interpolation_the_export_defers(
        self,
    ) -> None:
        """A cycle landing between two frames separates the two candidates.

        `mdl_gltf` bakes one key per frame and leaves the interpolation to its
        runtime, so holding the nearest key against retail's own sample measures
        what that runtime owes rather than a decode error. The complete rule
        interpolates and reproduces the capture.
        """
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory, cycle=0.3)
        self.assertEqual(
            self._bands(report, "decoded_locals", "complete"),
            {"excellent": 1, "investigate": 0, "definite": 0},
        )
        self.assertEqual(
            self._bands(report, "decoded_locals", "frame_key"),
            {"excellent": 0, "investigate": 0, "definite": 1},
        )

    def test_the_biped_family_comes_off_the_owners_own_mask(self) -> None:
        """A bank holding two skeletons is told apart by the mask, not the name.

        The clip drives the bank's `Bip02` chain and the contribution's mask
        selects exactly that chain, so the family the entity's `Bip01` bones map
        to is witnessed rather than assumed. Plain name matching reaches the
        bank's own `Bip01` chain, which is a real bone carrying the wrong pose —
        the cost the current export pays on a cinematic bank.
        """
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(
                directory,
                owner_bones=FAMILY_BONES,
                owner_clips=FAMILY_CLIPS,
                mask_bones=(4, 5, 6, 7),
            )
        self.assertEqual(
            self._bands(report, "decoded_locals", "complete"),
            {"excellent": 1, "investigate": 0, "definite": 0},
        )
        self.assertEqual(
            self._bands(report, "decoded_locals", "bone_name"),
            {"excellent": 0, "investigate": 0, "definite": 1},
        )

    def test_the_shipped_split_takes_the_actors_own_chain(self) -> None:
        """The export already carries the family, and this is what says so.

        `export_cinematic` writes one bank per `BipNN` root folded onto `Bip01`
        and the runtime picks the root from the scene actor's `bonerename`, so
        the shipped path reaches the same chain the mask witnesses. Reading it
        as plain name matching is what put a cinematic bank's whole cast in the
        wrong pose in an earlier report.
        """
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(
                directory,
                owner_bones=FAMILY_BONES,
                owner_clips=FAMILY_CLIPS,
                mask_bones=(4, 5, 6, 7),
            )
        self.assertEqual(
            self._bands(report, "decoded_locals", "cinematic_split"),
            {"excellent": 1, "investigate": 0, "definite": 0},
        )
        self.assertEqual(
            self._bands(report, "decoded_locals", "bone_name"),
            {"excellent": 0, "investigate": 0, "definite": 1},
        )
        stage = self._stage(report, "decoded_locals")
        self.assertEqual(stage["cinematic_root_pairs_resolved"], 1)
        self.assertEqual(stage["counts"]["cinematic_root_unresolved"], 0)

    def test_a_one_skeleton_bank_leaves_the_two_export_candidates_equal(self) -> None:
        """Nothing to split means nothing to get wrong, and no pair to resolve."""
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory)
        self.assertEqual(
            self._bands(report, "decoded_locals", "cinematic_split"),
            self._bands(report, "decoded_locals", "bone_name"),
        )
        stage = self._stage(report, "decoded_locals")
        self.assertEqual(stage["cinematic_root_pairs_resolved"], 0)

    def test_the_root_is_a_property_of_the_pair_and_not_of_one_contribution(
        self,
    ) -> None:
        """A second build whose own mask names no biped still loads one bank.

        The scene names an actor's `bonerename` root once and every contribution
        under it plays that bank, including the ones whose mask selects only a
        camera or prop bone — 17,168 of the theatre corpus's 239,122. Resolving
        the root per contribution instead would drop those back onto plain name
        matching, which is the bank's own `Bip01` chain and another actor's
        pose.
        """
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(
                directory,
                owner_bones=SPLIT_BONES,
                owner_clips=SPLIT_CLIPS,
                mask_bones=(4, 5, 6, 7),
                second_mask_bones=(SPLIT_DUMMY,),
            )
        stage = self._stage(report, "decoded_locals")
        self.assertEqual(stage["records"], 2)
        self.assertEqual(stage["cinematic_root_pairs_resolved"], 1)
        self.assertEqual(stage["counts"]["cinematic_root_unresolved"], 0)
        # Both builds, including the one that named no family itself.
        self.assertEqual(
            self._bands(report, "decoded_locals", "cinematic_split"),
            {"excellent": 2, "investigate": 0, "definite": 0},
        )
        self.assertEqual(
            self._bands(report, "decoded_locals", "bone_name"),
            {"excellent": 0, "investigate": 0, "definite": 2},
        )

    def test_an_owner_bone_with_no_entity_bone_is_counted_rather_than_compared(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(
                directory,
                owner_bones=FAMILY_BONES,
                owner_clips=FAMILY_CLIPS,
                mask_bones=(4, 5, 6, 7, 8),
            )
        counts = report["stages"]["decoded_locals"]["counts"]
        self.assertEqual(counts["owner_bones_decoded_with_no_entity_target"], 1)

    def test_a_cell_that_decoded_no_bone_is_excluded_with_its_reason(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory, mask_bones=())
        stage = report["stages"]["decoded_locals"]
        self.assertEqual(stage["counts"]["cells_that_decoded_no_bone"], 1)
        self.assertFalse(stage["available"])
        self.assertIn(
            "cells_that_decoded_no_bone", report["verdict"]["accounted_counts"]
        )

    # ---- stage 2: the composed locals ----

    def test_a_final_pose_equal_to_its_base_pose_leaves_the_layer_stage_empty(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory)
        stage = self._stage(report, "composed_locals")
        self.assertEqual(
            self._bands(report, "composed_locals", "last_evaluation"),
            {"excellent": 1, "investigate": 0, "definite": 0},
        )
        self.assertIn("no offline model", stage["implementation"])

    def test_a_layer_stage_shows_at_the_composed_locals_and_not_at_the_decode(
        self,
    ) -> None:
        """The two stages separate a decode error from a blend or layer error.

        The decode still reproduces the base pose exactly; the final locals are
        somewhere else, which is the transition and controller stage acting.
        Reporting only one of the two would attribute this to the decoder.
        """
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            frame = sampled_frame(CLIP_FRAMES, CONTRIBUTION_CYCLE)
            decoded = _clip_local(ROTATED_BONES, THEATRE_CLIP, frame, 0.0)
            layered = list(decoded)
            position, quaternion = layered[1]
            layered[1] = (tuple(a + b for a, b in zip(position, (0.0, 0.0, 5.0))),
                          quaternion)
            self._session(session, final_pose=layered)
            report = verify_transform(session)
        self.assertEqual(
            self._bands(report, "decoded_locals", "complete"),
            {"excellent": 1, "investigate": 0, "definite": 0},
        )
        self.assertEqual(
            self._bands(report, "composed_locals", "last_evaluation"),
            {"excellent": 0, "investigate": 0, "definite": 1},
        )

    # ---- the chain ----

    def test_the_chain_attributes_a_decode_error_to_the_decode(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory, perturb={2: (0.0, 0.0, 3.0)})
        chain = report["chain"]
        self.assertTrue(chain["available"], chain.get("reason"))
        self.assertEqual(chain["by_stage"]["decoded_locals"], 1)
        self.assertEqual(chain["by_stage"]["bone_to_world"], 0)
        self.assertEqual(chain["by_stage"]["none"], 0)

    def test_the_chain_reaches_the_palette_when_every_stage_agrees(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory)
        chain = report["chain"]
        self.assertEqual(chain["by_stage"]["none"], 1)
        self.assertEqual(
            chain["candidates"]["chained"]["records_by_band"],
            {"excellent": 1, "investigate": 0, "definite": 0},
        )

    def test_the_chain_attributes_a_layer_error_to_the_composed_locals(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            frame = sampled_frame(CLIP_FRAMES, CONTRIBUTION_CYCLE)
            decoded = _clip_local(ROTATED_BONES, THEATRE_CLIP, frame, 0.0)
            layered = list(decoded)
            position, quaternion = layered[1]
            layered[1] = (tuple(a + b for a, b in zip(position, (0.0, 0.0, 5.0))),
                          quaternion)
            self._session(session, final_pose=layered)
            report = verify_transform(session)
        self.assertEqual(report["chain"]["by_stage"]["composed_locals"], 1)
        self.assertEqual(report["chain"]["by_stage"]["decoded_locals"], 0)

    # ---- ranking ----

    def test_worst_bones_is_ranked_inside_each_candidate(self) -> None:
        """A deliberately wrong candidate must not crowd out the complete rule.

        `conventional` and `regenerated` exist to be worse, so a list ranked
        across candidates is theirs alone and the rule under measurement ends up
        with no bone named. Ranking inside each candidate is what attributes a
        residual to a bone.
        """
        with tempfile.TemporaryDirectory() as directory:
            report = self._report(directory, perturb={2: (0.0, 0.0, 3.0)})
        pairs = {(row["stage"], row["candidate"]) for row in report["worst_bones"]}
        self.assertIn(("decoded_locals", "complete"), pairs)
        self.assertIn(("bone_to_world", "conventional"), pairs)
        for row in report["worst_bones"]:
            self.assertIn("candidate_rank_truncated", row)


if __name__ == "__main__":
    unittest.main()
