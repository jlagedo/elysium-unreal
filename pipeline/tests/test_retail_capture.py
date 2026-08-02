from __future__ import annotations

import json
from pathlib import Path
import sqlite3
import struct
import tempfile
import unittest

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
    build_config as build_theatre_config,
)
from research.tooling.capture.calibrate_theatre_capture import calibrate
from research.tooling.capture.verify_entity_pointer_join import address, verify
from research.tooling.capture.finalize_capture_database import (
    ACTOR_FILE_HEADER,
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
    finalize,
)
from research.tooling.capture.verify_pose_build_generation import (
    verify as verify_generation,
)
from research.tooling.capture.verify_model_skeleton_census import (
    verify as verify_census,
)
from research.tooling.capture.verify_actor_identity_lifetime import (
    verify as verify_actors,
)
from research.tooling.capture.verify_source_attribution import (
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
) -> bytes:
    payload = bytes(bone_count * 12 * 4 * 2)
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
) -> bytes:
    # The composed-pose stage is the only one that receives a root transform, so
    # a FINL record carries the trailer and a BASE record carries none unless a
    # test is deliberately building a malformed one.
    carries_root = magic == b"FINL" if root_transform is None else root_transform
    payload = bytes(bone_count * 7 * 4 + ((bone_count + 31) // 32) * 4)
    root_bytes = ROOT_TRANSFORM_BYTES if carries_root else 0
    if carries_root:
        # A real root/entity transform: identity rotation and a translation, so
        # the verifier's unit-determinant check has something to measure rather
        # than a zero-filled block that would fail it for the wrong reason.
        payload += struct.pack(
            "<12f",
            1.0, 0.0, 0.0, 64.0,
            0.0, 1.0, 0.0, -32.0,
            0.0, 0.0, 1.0, 16.0,
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
            0.25,
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


def actor_lifetime_record(
    sequence: int, qpc: int, reason: int, *, entity: int = 0x1FFC
) -> bytes:
    """A construction or destruction: an address and a time, no identity."""
    return ACTOR_OBSERVATION_HEADER.pack(
        b"ACTR",
        ACTOR_OBSERVATION_HEADER.size,
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
) -> bytes:
    return ACTOR_OBSERVATION_HEADER.pack(
        b"ACTR",
        ACTOR_OBSERVATION_HEADER.size,
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
# posX, posY, posZ, rotX, rotY, rotZ, rotW, in the order the per-bone record
# declares them.
CHANNEL_COUNT = 7


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
    bones: tuple[tuple[str, int, tuple[float, float, float]], ...] = (
        ("Bip01", -1, (0.0, 0.0, 0.0)),
        ("Bip01 Spine", 0, (1.0, 2.0, 3.0)),
    ),
    clips: tuple[Clip, ...] = DEFAULT_CLIPS,
    sequences: int = CONTRIBUTION_NUM_SEQ,
) -> bytes:
    """A minimal v2531 model image the pipeline bone decoder can read.

    Bind rotations are identity and each bone's ``poseToBone`` is the exact
    inverse of its composed bind, so a correctly decoded skeleton returns the
    identity and the census verifier's soundness check has something real to
    measure.

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

    world = {}
    for index, (name, parent, pos) in enumerate(bones):
        base = BONE_ARRAY_OFFSET + BONE_STRIDE * index
        origin = world.get(parent, (0.0, 0.0, 0.0))
        composed = tuple(origin[axis] + pos[axis] for axis in range(3))
        world[index] = composed
        struct.pack_into("<i", blob, base, len(blob) - base)
        blob += name.encode("ascii") + b"\0"
        struct.pack_into("<i", blob, base + 4, parent)
        struct.pack_into("<3f", blob, base + 32, *pos)
        struct.pack_into("<4f", blob, base + 44, 0.0, 0.0, 0.0, 1.0)
        struct.pack_into("<3f", blob, base + 60, 1.0, 1.0, 1.0)
        struct.pack_into("<4f", blob, base + 72, 1.0, 1.0, 1.0, 1.0)
        struct.pack_into(
            "<12f",
            blob,
            base + 88,
            1.0, 0.0, 0.0, -composed[0],
            0.0, 1.0, 0.0, -composed[1],
            0.0, 0.0, 1.0, -composed[2],
        )
        struct.pack_into("<i", blob, base + 136, 0)

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
    # sizes 1, with cell zero naming animation zero.
    for index in range(sequences):
        desc = CONTRIBUTION_SEQ_INDEX_OFF + SEQ_DESC_STRIDE * index
        blob += bytearray(SEQ_DESC_STRIDE)
        struct.pack_into("<i", blob, desc + 52, 1)
        struct.pack_into("<ii", blob, desc + 572, 1, 1)
        struct.pack_into("<ii", blob, desc + 580, -1, -1)
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
            0,
            -1,
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
    actor_version: int = 2,
    done: str = "complete=1\nqueued=2\nwritten=2\ndropped=0\n",
    boundary: dict[str, object] | None = None,
    console: str | None = None,
) -> None:
    (session / "launch.json").write_text(
        json.dumps(
            {
                "created_utc": "test",
                "tool_git": {"commit": "abc", "dirty": False},
                "map": "sp_theatre",
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
    (session / "recipe.cfg").write_text("map sp_theatre\n", encoding="ascii")
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
        lines = build_theatre_config().splitlines()
        map_line = lines.index("map sp_theatre")
        self.assertEqual(lines[map_line - 1], "echo ELYSIUM_CAP11_MAP_SP_THEATRE")
        self.assertEqual(lines[:map_line].count("wait"), PRE_MAP_WAITS)
        self.assertLess(lines.index("host_framerate 0.033333333"), map_line)
        self.assertNotIn("player_sequence", "\n".join(lines))

    def test_theatre_recipe_leaves_input_live_and_retail_running(self) -> None:
        # The operator walks onto the arrival trigger, and the hook only
        # flushes while retail is alive, so neither may be taken away.
        lines = build_theatre_config().splitlines()
        for forbidden in ("cl_mouselook 0", "cl_mouseenable 0", "quit"):
            self.assertNotIn(forbidden, lines)

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
                actor_records=actor_record(3, 99, "models/test.mdl"),
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
        return contribution_record(
            b"SEQP", 8, 103, sequence_index=2, num_blends=9,
            group_size=(9, 1), blend_cell=(4, 0), blend_weight=(0.5, 0.0),
        ) + contribution_record(b"ANIM", 9, 104, animation_index=5)

    def test_attribution_verifier_accepts_a_fully_attributed_run(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            session = Path(directory)
            self._attribution_session(session, self._clean_contributions())
            report = verify_attribution(session)
            self.assertTrue(report["support"]["carries_contributions"])
            self.assertEqual(report["coverage"]["sequences"], 1)
            self.assertEqual(report["coverage"]["cells"], 1)
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


if __name__ == "__main__":
    unittest.main()
