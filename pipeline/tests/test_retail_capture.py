from __future__ import annotations

import math

import struct

import unittest

import numpy as np
import pytest

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

# A skeleton whose binds carry real rotations and whose third bone does not
# inherit its parent's rotation, which is the one thing the split arm
# discriminates on. Rotations are exact quaternions so the hand-written oracle
# and the numpy evaluator can be required to agree to floating-point noise
# rather than to a band.
HALF = math.sqrt(0.5)

def _bind_locals(bones):
    return [(spec[2], spec[3]) for spec in (_bone_spec(entry) for entry in bones)]

TRANSFORM_CHECKSUM = 0x3000

TRANSFORM_MODEL = "models/rotated.mdl"

TRANSFORM_ROOT = (
    0.0, -1.0, 0.0, 12.0,
    1.0, 0.0, 0.0, -4.0,
    0.0, 0.0, 1.0, 3.0,
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

#: `bsp.source_to_unreal` as a basis matrix and a scale: the Y negation that flips handedness,
#: and inches to centimetres. Stated here rather than imported so the assertion is between two
#: formulations.
UNREAL_M = ((1.0, 0.0, 0.0), (0.0, -1.0, 0.0), (0.0, 0.0, 1.0))

UNREAL_SCALE = 2.54

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

def _to_basis_3x4(matrix: tuple[float, ...], basis, scale: float) -> tuple[float, ...]:
    """A Source-basis 3x4 in another basis.

    A change of basis conjugates a transform, so the rotation goes to `M R M^T`
    while the translation is carried through `M` once and scaled.
    """
    rotation = [
        [
            sum(
                basis[row][k] * matrix[k * 4 + l] * basis[column][l]
                for k in range(3)
                for l in range(3)
            )
            for column in range(3)
        ]
        for row in range(3)
    ]
    translation = [
        scale * sum(basis[row][k] * matrix[k * 4 + 3] for k in range(3))
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
        assert len(decoded) == 2
        assert decoded[0].endframe == 2
        assert decoded[0].motionflags == 0x10C0
        assert decoded[0].vector == (1.0, 0.0, 0.0)
        assert decoded[-1].position == (12.0, 0.0, 0.0)

        summary = mdl_skel.movement_summary(image, descriptor, frames, fps)
        assert summary is not None
        assert summary.cycle_seconds == pytest.approx(0.1, abs=1e-6)
        assert summary.ground_distance_cm == pytest.approx(30.48, abs=1e-5)
        assert summary.ground_speed_cm_s == pytest.approx(304.8, abs=1e-4)

    def test_absent_or_malformed_movement_keeps_the_fallback(self) -> None:
        """A damaged optional array never reads beyond the image or invents a speed."""
        from elysium_pipeline.formats import mdl_skel

        image = self._image({0: NINE_BY_ONE})
        _name, descriptor, frames, fps = mdl_skel.local_animation(image, 4)
        assert mdl_skel.read_movements(image, descriptor) == ()
        assert mdl_skel.movement_summary(image, descriptor, frames, fps) is None

        malformed = bytearray(image)
        struct.pack_into("<ii", malformed, descriptor + 16, 2, len(malformed) - descriptor - 10)
        assert mdl_skel.read_movements(malformed, descriptor) == ()
        assert mdl_skel.movement_summary(malformed, descriptor, frames, fps) is None

    def test_a_resolved_cell_exports_its_motion_summary(self) -> None:
        """The neutral walk cell carries the scalar its Unreal motor consumes."""
        from elysium_pipeline.formats import mdl_gltf, mdl_skel

        records = (
            (3, 0x10C0, 3.0, 4.0, 0.0, 1.0, 0.0, 0.0, 12.0, 0.0, 0.0),
        )
        image = self._with_movements(self._image({0: NINE_BY_ONE}), 4, records)
        _extra, blends = mdl_skel.blend_clip_plan(image, mdl_skel.local_sequences(image))
        forward = next(cell for cell in blends["walk"]["cells"] if cell["axis"] == [4, 0])
        assert forward["clip"] == "aim#4"
        assert (forward["motion"] == {
                "cycle_seconds": 0.1,
                "ground_distance_cm": 30.48,
                "ground_speed_cm_s": 304.8,
            })
        assert "motion" not in blends["walk"]["cells"][0]

    def test_a_single_cell_sequence_carries_its_movement_records(self) -> None:
        """The melee case: every attack is one cell, and the lunge is only in this array."""
        from elysium_pipeline.formats import mdl_skel

        records = (
            (2, 0x1040, 2.0, 4.0, 0.0, 1.0, 0.0, 0.0, 3.0, 0.0, 0.0),
            (4, 0x1040, 6.0, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0),
        )
        # No grid at all, so all four sequences are the single cell a blend-cell walk never
        # reaches; `aim` alone selects the animation carrying the records.
        image = self._with_movements(
            self._image({}, base_cells=(0, 4, 0, 0)), 4, records)
        sequences = self._sequences(image)
        assert len(sequences["aim"].grid.cells) == 1
        assert len(sequences["aim"].movement) == 2
        assert sequences["aim"].movement[0].endframe == 2
        # Asked and empty is the other answer, and it is not the same as never asked.
        assert sequences["idle"].movement == ()

        extra, blends = mdl_skel.blend_clip_plan(image, list(sequences.values()))
        assert (extra, blends) == ([], {})
        sidecar = mdl_skel.blend_sidecar(image, blends, list(sequences.values()))
        assert (sidecar["movement_fields"] == ["end_frame", "flags", "v0_cm", "v1_cm", "yaw_deg",
                          "dir_x", "dir_y", "dir_z", "pos_x_cm", "pos_y_cm", "pos_z_cm"])
        assert sorted(sidecar["movement"]) == ["aim"]
        # The path is piecewise and the cumulative position returns to zero: a scalar summary
        # would call this "no movement" while the file states a real displacement out and back.
        assert (sidecar["movement"]["aim"] == [[2, 0x1040, 5.08, 10.16, 0.0, 1.0, 0.0, 0.0, 7.62, 0.0, 0.0],
                          [4, 0x1040, 15.24, 0.0, 0.0, -1.0, 0.0, 0.0, 0.0, 0.0, 0.0]])
        assert (mdl_skel.movement_summary(
            image, sequences["aim"].base, sequences["aim"].frames, sequences["aim"].fps) is None)

    def test_movement_rows_are_stated_unreal_native(self) -> None:
        """The sidecar states the path in centimetres on Unreal axes, converted exactly once."""
        from elysium_pipeline.formats import mdl_skel

        # A record with a real Y and Z: 8,169 of the 13,505 shipped records state a non-zero Y,
        # so the reflection is observable rather than a formality.
        records = ((3, 0x11C0, 4.0, 6.0, 90.0, 0.0, -1.0, 0.0, 0.0, -10.0, 2.0),)
        image = self._with_movements(
            self._image({}, base_cells=(0, 4, 0, 0)), 4, records)
        sequences = self._sequences(image)
        row, = mdl_skel.blend_sidecar(image, {}, list(sequences.values()))["movement"]["aim"]
        assert row == [3, 0x11C0, 10.16, 15.24, -90.0, 0.0, 1.0, 0.0, 0.0, 25.4, 5.08]
        # The reflection turns a zero component into `-0.0`; an axis-aligned path reads as one.
        assert not any(math.copysign(1.0, value) < 0.0 for value in row if value == 0.0)

    def test_a_model_whose_sequences_state_no_movement_says_so(self) -> None:
        """Asked-and-empty ships the column list and no rows, which is not silence."""
        from elysium_pipeline.formats import mdl_skel

        image = self._image({})
        sequences = list(self._sequences(image).values())
        assert all(clip.movement == () for clip in sequences)
        table = mdl_skel.movement_table(sequences)
        assert "movement_fields" in table
        assert "movement" not in table
        # Nothing else is authored either, so there is no sidecar to carry the column list.
        assert mdl_skel.blend_sidecar(image, {}, sequences) == {}

    def test_a_nine_by_one_grid_reads_its_extents_binding_and_every_cell(self) -> None:
        """The shape the theatre corpus fires throughout, read end to end."""
        sequences = self._sequences(self._image({0: NINE_BY_ONE}))
        grid = sequences["walk"].grid
        assert grid.numblends == 9
        assert grid.groupsize == (9, 1)
        assert grid.paramindex == (0, -1)
        assert grid.paramstart == (-180.0, 0.0)
        assert grid.paramend == (180.0, 0.0)
        assert ([(cell.axis0, cell.axis1, cell.anim) for cell in grid.cells] == [(0, 0, 0), (1, 0, 1), (2, 0, 2), (3, 0, 3), (4, 0, 4),
             (5, 0, 5), (6, 0, 6), (7, 0, 2), (8, 0, 0)])
        # The clip the sequence still bakes is the base cell's, unchanged.
        assert (sequences["walk"].base == grid.cells[0].anim * ANIM_DESC_STRIDE
                         + CONTRIBUTION_ANIM_INDEX_OFF)

    def test_a_three_by_three_grid_takes_axis_zero_down_the_row_stride(self) -> None:
        """The cell address, in the orientation retail's own witness fixes.

        A capture of `smith_aim_layer` records the cell `[1, 0]` decoding the
        four animations at rows 1-2 and columns 0-1 of the inline array. So axis
        0 takes the fixed 16-short row stride and axis 1 the column, and the
        transposed address would name six of these nine cells wrongly.
        """
        grid = self._sequences(self._image({1: THREE_BY_THREE}))["aim"].grid
        assert grid.groupsize == (3, 3)
        assert grid.paramindex == (2, 3)
        assert {(cell.axis0, cell.axis1): cell.anim for cell in grid.cells} == THREE_BY_THREE.cells

    def test_a_single_cell_sequence_reads_a_one_by_one_grid(self) -> None:
        """The 913-of-1,166 case: a sequence that is a clip and nothing more."""
        grid = self._sequences(self._image({}))["walk"].grid
        assert grid.numblends == 1
        assert grid.groupsize == (1, 1)
        assert grid.paramindex == (-1, -1)
        assert len(grid.cells) == 1
        assert (grid.cells[0].axis0, grid.cells[0].axis1) == (0, 0)

    def test_extents_disagreeing_with_numblends_fall_back_to_the_base_cell(self) -> None:
        """`groupsize[0] * groupsize[1] == numblends` holds on all 294 authored
        multi-blend sequences, so a descriptor where it does not is one this
        format does not explain — and it yields the clip it always did rather
        than a grid of whatever the inline array happens to hold."""
        broken = Grid((9, 1), {(0, 0): 3, (1, 0): 1}, numblends=5)
        grid = self._sequences(self._image({0: broken}))["walk"].grid
        assert grid.numblends == 5
        assert grid.groupsize == (9, 1)
        assert [cell.anim for cell in grid.cells] == [3]

    def test_the_pose_parameters_a_grid_axis_binds_to_are_read(self) -> None:
        """A `paramindex` is an index into this model's own array, so the axis
        cannot be wrapped or normalized without the record it names."""
        from elysium_pipeline.formats import mdl_skel

        parameters = mdl_skel.pose_parameters(self._image({0: NINE_BY_ONE}))
        assert ([(p.index, p.name, p.flags, p.start, p.end, p.loop) for p in parameters] == [(index, name, flags, start, end, loop)
             for index, (name, flags, start, end, loop) in enumerate(POSE_PARAMETERS)])
        grid = self._sequences(self._image({0: NINE_BY_ONE}))["walk"].grid
        assert parameters[grid.paramindex[0]].name == "move_yaw"
        assert parameters[grid.paramindex[0]].loop == 360.0

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
        extra, blends = mdl_skel.blend_clip_plan(image, sequences)

        # One extra clip per distinct animation the two grids reach that the
        # base-cell bake does not: animations 1-6 over the two of them, each
        # named by the animation's own name. `idle`, `aim` and `turn` are also
        # sequence labels in this fixture, so those three take the index
        # disambiguation and the rest read as the animation the content named.
        assert sorted(clip.label for clip in extra) == ["aim#4", "dead", "idle#2", "run", "skip", "turn#5"]
        assert len({clip.base for clip in extra}) == len(extra)

        walk = blends["walk"]
        assert walk["groupsize"] == [9, 1]
        assert walk["paramindex"] == [0, -1]
        assert ([cell["clip"] for cell in walk["cells"]] == ["walk", "run", "idle#2", "dead", "aim#4", "turn#5", "skip", "idle#2",
             "walk"])
        assert [cell["axis"] for cell in blends["aim"]["cells"]] == [[0, 0], [0, 1], [0, 2], [1, 0], [1, 1], [1, 2], [2, 0], [2, 1], [2, 2]]
        # Nothing in the plan carries a blended clip: every cell names a clip
        # that decodes one animation of the model, and the weights that mix them
        # are absent because they are not the exporter's to apply.
        bases = {clip.label: clip.base for clip in (*sequences, *extra)}
        for grid in blends.values():
            for cell in grid["cells"]:
                assert cell["clip"] in bases

    def test_a_cell_outside_the_animation_count_is_carried_as_unresolved(self) -> None:
        """A cell the model's own declaration cannot answer is a shortfall the
        sidecar names, not a grid quietly shortened to the cells that worked."""
        from elysium_pipeline.formats import mdl_gltf, mdl_skel

        image = self._image({0: Grid((3, 1), {(0, 0): 0, (1, 0): 1, (2, 0): 99})})
        _extra, blends = mdl_skel.blend_clip_plan(
            image, mdl_skel.local_sequences(image)
        )
        assert [cell["clip"] for cell in blends["walk"]["cells"]] == ["walk", "run", None]

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
        assert sorted(kept) == ["walk"]
        assert [cell["clip"] for cell in kept["walk"]["cells"]] == ["walk", "run", None]

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
                assert position == (0.0, 0.0, 0.0)
                assert quaternion == (0.0, 0.0, 0.0, 0.0)

        # The zero is the weight's doing, not the clip's: the same bones under a
        # weight of 1.0 fall back to their bind values on an unanimated channel.
        alive = CONTRIBUTION_ANIM_INDEX_OFF + ANIM_DESC_STRIDE * 2
        for position, quaternion in mdl_skel.read_anim(image, bones, alive, 1)[0]:
            assert quaternion != (0.0, 0.0, 0.0, 0.0)
        assert mdl_skel.read_anim(image, bones, alive, 1)[0][1][0] == bones[1].pos

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
        from elysium_pipeline.exporters import UE_mdl_skeletal as UEK
        from elysium_pipeline.formats import mdl_skel

        records, faults = mdl_skel.axis_interp_records(image, mdl_skel.read_bones(image))
        return UEK.unreal_axis_rules(records), faults

    def test_the_exporter_carries_the_source_axes_into_the_unreal_basis(self) -> None:
        """The three vectors every exported rule is read against.

        Source Y becomes negative Unreal Y, so an export that carried the axis index
        through unchanged would name a sign-flipped axis on one rule in three. This
        is the assertion that says so out loud.
        """
        from elysium_pipeline.exporters import UE_mdl_skeletal as UEK

        assert UEK.DRIVER_AXES == [[1.0, 0.0, 0.0], [0.0, -1.0, 0.0], [0.0, 0.0, 1.0]]
        # `-0.0` is normalized away, so the table reads as the signed unit vectors it is.
        for axis in UEK.DRIVER_AXES:
            for component in axis:
                assert not (math.copysign(1.0, component) < 0.0 and component == 0.0)

    def test_the_table_takes_the_same_basis_change_the_mesh_and_clips_take(self) -> None:
        """The quaternion route the table uses names the rotation the matrix does.

        The table is read back and re-evaluated rather than only drawn, so it goes
        through the quaternion rather than the rotation matrix -- which keeps the
        representative the model authored and inverts to the float32 it was read
        from. That is a different route to the same conjugation, and this is what
        says so: the two agree as rotations to floating-point noise.
        """
        from elysium_pipeline.formats import bsp, mdl_skel

        basis = np.asarray(UNREAL_M)
        for quaternion in (*AXIS_RULE[3], OBLIQUE, (0.0, HALF, 0.0, -HALF)):
            with self.subTest(quaternion=quaternion):
                exact = bsp.source_quat_to_unreal(*quaternion)
                assert (np.allclose(
                        mdl_skel.rot_matrix(exact),
                        basis @ mdl_skel.rot_matrix(quaternion) @ basis,
                        atol=1.0e-12,
                    ))
                # A component negation is its own inverse, so the exported table
                # recovers the bytes it was read from exactly rather than nearly.
                assert bsp.source_quat_to_unreal(*exact) == tuple(quaternion)

    def test_the_exported_table_inverts_to_the_bytes_it_was_read_from(self) -> None:
        from elysium_pipeline.exporters import UE_mdl_skeletal as UEK

        # `source_to_unreal` and `source_quat_to_unreal` are their own inverses up to the
        # inch-to-centimetre scale, so undoing them is a division and two sign flips.
        def unconv_pos(p):
            return (p[0] / UNREAL_SCALE, -p[1] / UNREAL_SCALE, p[2] / UNREAL_SCALE)

        def unconv_quat(q):
            return (-q[0], q[1], -q[2], q[3])

        for axis in range(3):
            with self.subTest(axis=axis):
                image = self._image(axis)
                rules, faults = self._rules(image)
                assert faults == []
                assert len(rules) == 1
                rule = rules[0]
                assert rule["bone"] == "Bip01 L Bicep"
                assert rule["bone_index"] == 3
                assert rule["control"] == "Bip01 Spine"
                assert rule["control_index"] == 1
                # The axis is carried as a direction, so recovering the index it
                # was written from is a lookup rather than a conversion.
                rebuilt = struct.pack(
                    "<ii", rule["control_index"], UEK.DRIVER_AXES.index(rule["axis"])
                )
                rebuilt += struct.pack(
                    "<18f",
                    *[c for entry in rule["pos"] for c in unconv_pos(entry)],
                )
                rebuilt += struct.pack(
                    "<24f",
                    *[c for entry in rule["quat"] for c in unconv_quat(entry)],
                )
                assert rebuilt == _raw_axis_interp(image, 3)

    def test_the_exported_table_evaluates_to_the_converted_correction(self) -> None:
        from elysium_pipeline.exporters import UE_mdl_skeletal as UEK

        locals_ = _bind_locals(PROCEDURAL_BONES)
        # The control bone's bind is oblique, so the driver has three non-zero
        # components on every axis and the whole rule runs rather than landing on
        # one table entry.
        world = _expected_world(PROCEDURAL_BONES, locals_, TRANSFORM_ROOT, split=True)
        to_unreal = lambda m: _to_basis_3x4(m, UNREAL_M, UNREAL_SCALE)
        converted = [to_unreal(matrix) for matrix in world]
        for axis in range(3):
            with self.subTest(axis=axis):
                image = self._image(axis)
                raw = _decoded_axis_interp(_raw_axis_interp(image, 3))
                assert raw[1] == axis
                expected = to_unreal(_axis_interp_local(raw, world, PROCEDURAL_BONES))
                rules, _ = self._rules(image)
                produced = _exported_axis_interp_local(
                    rules[0], UEK.DRIVER_AXES, converted, PROCEDURAL_BONES
                )
                for index, (a, b) in enumerate(zip(produced, expected)):
                    assert a == pytest.approx(b, abs=1e-9)
                # The rule has to be doing work, or agreeing about nothing would
                # pass: the correction is not the bone's own animated local.
                assert not np.allclose(produced, to_unreal(world[3]), atol=1.0e-3)

    def test_a_rule_that_does_not_resolve_is_a_named_fault(self) -> None:
        rules, faults = self._rules(self._image(rule=(1, 2, *AXIS_RULE[2:])))
        assert len(rules) == 1
        assert faults == []

        # `ProcIndex` left at zero resolves onto the bone record itself, whose
        # first field is a string index rather than a bone.
        image = model_image(
            TRANSFORM_CHECKSUM, TRANSFORM_MODEL,
            bones=PROCEDURAL_BONES, procedural={3: None},
        )
        rules, faults = self._rules(image)
        assert rules == []
        assert len(faults) == 1
        assert "control bone" in faults[0]

    def test_a_rule_naming_an_axis_outside_the_three_is_a_named_fault(self) -> None:
        image = bytearray(self._image())
        record = struct.unpack_from("<i", image, 244)[0] + BONE_STRIDE * 3
        offset = record + struct.unpack_from("<i", image, record + 144)[0]
        struct.pack_into("<i", image, offset + 4, 3)
        rules, faults = self._rules(bytes(image))
        assert rules == []
        assert len(faults) == 1
        assert "axis 3" in faults[0]
