"""Transform arithmetic for differencing a capture against the offline decoder.

A library with no command of its own, beside `verify_transform_difference.py`
the way `decoder_coverage.py` sits beside `verify_byte_coverage.py`. It holds
three separable things:

1. the skeleton read out of a model image, which is `mdl_skel.read_bones` run
   **unmodified** and nothing else — the decoder under measurement is never
   edited, reimplemented or worked around here;
2. the composition and palette rules, which are **transcribed** from the
   confirmed decompilation and are not an Elysium code path: Unreal composes
   glTF conventionally through glTFRuntime, and `mdl_gltf` discards
   `StudioBone.poseToBone` and regenerates inverse binds by ordinary hierarchy
   FK, so there is no shipped implementation of a `Flags & 0x2` hierarchy or of
   a `poseToBone` palette to compare against;
3. the metrics and their reporting bands, which
   `docs/vtmb/vtmb-animation-reverse-engineering.md` 11.3 owns.

Every transform is a 3x4 row-major `float64` array shaped `(..., 3, 4)` — the
shape the probe copies out of `matrix3x4_t` and the shape `poseToBone` is stored
in. Everything stays in Source space, inches and Z-up: the capture is in Source
space and so is the decoder, so a basis change would only inject error.

Composition is written to run in model space and then to be placed by the root
frame, because `boneToWorld = root . modelSpace` holds exactly through the split
branch as well as the ordinary one. That identity is what lets entity placement
be divided out rather than estimated, and `compose` takes the root directly so a
caller never has to reproduce it.
"""

from __future__ import annotations

from dataclasses import dataclass
import math
import struct

import numpy as np

from elysium_pipeline.formats import mdl_skel


SHIPPED = "elysium_pipeline.formats.mdl_skel"

# Reporting bands, not pass/fail claims:
# docs/vtmb/vtmb-animation-reverse-engineering.md 11.3. The pair is
# (excellent ceiling, investigate ceiling); above the second is a definite
# mismatch. 11.3's closing rule travels with them — a band is never loosened to
# make something pass without recording why.
BANDS = {
    "local_translation": (0.01, 0.1),
    "model_translation": (0.02, 0.2),
    "world_translation": (0.02, 0.2),
    "rotation_degrees": (0.05, 0.5),
    "row_length": (1.0e-4, 1.0e-3),
}
BAND_NAMES = ("excellent", "investigate", "definite")

SPLIT_INHERITANCE = 0x2
PROCEDURAL = 0x1
AXIS_INTERP = 1

# `mstudioaxisinterpbone_t`: control, axis, pos[6], quat[6].
AXIS_INTERP_BYTES = 4 + 4 + 6 * 12 + 6 * 16

# MDLHeader and StudioBone displacements, from
# docs/vtmb/animation_and_movers.md. Format facts, not walk rules.
HEADER_BONE_INDEX = 244
BONE_PROC_TYPE = 140
BONE_PROC_INDEX = 144

IDENTITY = np.array(
    [[1.0, 0.0, 0.0, 0.0], [0.0, 1.0, 0.0, 0.0], [0.0, 0.0, 1.0, 0.0]],
    dtype=np.float64,
)

# The histogram every accumulator buckets into: 192 log-spaced buckets over
# [1e-9, 1e4], so a reported quantile is the upper edge of the bucket the true
# value falls in and is accurate to this ratio. Exact per-band counts do not come
# from the histogram; they are compared against the band edges directly.
HISTOGRAM_BUCKETS = 192
HISTOGRAM_LOW = 1.0e-9
HISTOGRAM_HIGH = 1.0e4
QUANTILE_RESOLUTION = float(
    10.0 ** (math.log10(HISTOGRAM_HIGH / HISTOGRAM_LOW) / HISTOGRAM_BUCKETS)
)


# --- skeletons ---------------------------------------------------------------


@dataclass(frozen=True)
class AxisInterp:
    """One `ProcType == 1` correction table, as the model stores it.

    The rule and its evidence are owned by `docs/vtmb/procedural_bones.md`; this
    is the table it names, read for evaluation and nothing else.
    """

    bone: int
    control: int
    axis: int
    position: np.ndarray    # (6, 3) — X+, X-, Y+, Y-, Z+, Z-
    quaternion: np.ndarray  # (6, 4) xyzw, same order


@dataclass(frozen=True)
class Skeleton:
    """One model's bones, as the offline decoder reads them.

    `faults` names structural problems rather than raising, because the tool
    counts them as declared populations and a raise would lose every other model
    in the same batch.
    """

    checksum: int
    names: tuple[str, ...]
    parent: np.ndarray          # (n,) int32, -1 for a root
    flags: np.ndarray           # (n,) int32
    bind_position: np.ndarray   # (n, 3)
    bind_quaternion: np.ndarray  # (n, 4) xyzw
    pose_to_bone: np.ndarray    # (n, 3, 4), the stored inverse bind
    depth: np.ndarray           # (n,) int32
    proc_type: np.ndarray       # (n,) int32, 0 = no procedural rule
    axis_interp: tuple[AxisInterp, ...]
    faults: tuple[str, ...]

    @property
    def bones(self) -> int:
        return len(self.names)

    @property
    def split(self) -> np.ndarray:
        """The bones whose rotation does not inherit from their parent."""
        return (self.flags & SPLIT_INHERITANCE) != 0

    @property
    def procedural(self) -> np.ndarray:
        """The bones a procedural rule drives after the hierarchy composes."""
        return self.proc_type != 0

    def descendants(self, index: int) -> np.ndarray:
        """A boolean mask of `index` and everything below it.

        One forward pass, which is only correct because the parent array is
        topological; `faults` says so when it is not.
        """
        mask = np.zeros(self.bones, dtype=bool)
        mask[index] = True
        for bone in range(index + 1, self.bones):
            parent = int(self.parent[bone])
            if parent >= 0 and mask[parent]:
                mask[bone] = True
        return mask


def skeleton_from_image(image: bytes, checksum: int = 0) -> Skeleton:
    """Decode a model image's skeleton with the shipped decoder, unmodified."""
    bones = mdl_skel.read_bones(image)
    count = len(bones)
    parent = np.array([bone.parent for bone in bones], dtype=np.int32)
    faults: list[str] = []
    if count and (
        int(parent.max(initial=-1)) >= count or int(parent.min(initial=-1)) < -1
    ):
        faults.append("skeleton_parent_out_of_range")
    # Retail composes a pose in one forward pass over the bone array, so a bone
    # whose parent sits at or after it would be composed against a slot that has
    # not been written yet. That is a different evaluation order, not a slower
    # one, and every walk below assumes the forward pass is sound.
    if any(index <= int(parent[index]) < count for index in range(count)):
        faults.append("skeleton_parent_not_topological")

    # `StudioBone.ProcType` at bone base + 140, from
    # `docs/vtmb/animation_and_movers.md`. It is read for attribution only and
    # never enters a composition: labelling a mismatching bone as one a
    # procedural rule drives is what tells a hierarchy difference apart from a
    # stage nothing offline evaluates. `mdl_skel` stays unmodified either way.
    stride, base = 160, 0
    if count:
        base = struct.unpack_from("<i", image, HEADER_BONE_INDEX)[0]
    proc_type = np.array(
        [
            struct.unpack_from("<i", image, base + stride * index + BONE_PROC_TYPE)[0]
            for index in range(count)
        ],
        dtype=np.int32,
    )

    # The `ProcType == 1` tables, at `ProcIndex` relative to the bone record. A
    # rule that does not fit the image, or names a control bone or axis outside
    # range, is dropped rather than evaluated: the driven bone then composes
    # ordinarily and the caller counts it.
    rules: list[AxisInterp] = []
    dropped = 0
    for index in range(count):
        record = base + stride * index
        if int(proc_type[index]) != AXIS_INTERP:
            dropped += 1 if int(proc_type[index]) else 0
            continue
        offset = record + struct.unpack_from("<i", image, record + BONE_PROC_INDEX)[0]
        if offset < 0 or offset + AXIS_INTERP_BYTES > len(image):
            dropped += 1
            continue
        control, axis = struct.unpack_from("<ii", image, offset)
        if not (0 <= control < count) or not (0 <= axis <= 2):
            dropped += 1
            continue
        rules.append(
            AxisInterp(
                bone=index,
                control=control,
                axis=axis,
                position=np.array(
                    struct.unpack_from("<18f", image, offset + 8), dtype=np.float64
                ).reshape(6, 3),
                quaternion=np.array(
                    struct.unpack_from("<24f", image, offset + 80), dtype=np.float64
                ).reshape(6, 4),
            )
        )
    if dropped:
        faults.append("procedural_rule_unreadable")

    depth = np.zeros(count, dtype=np.int32)
    if "skeleton_parent_not_topological" not in faults:
        for index in range(count):
            up = int(parent[index])
            depth[index] = 0 if up < 0 else depth[up] + 1

    return Skeleton(
        checksum=checksum,
        names=tuple(bone.name for bone in bones),
        parent=parent,
        flags=np.array([bone.flags for bone in bones], dtype=np.int32),
        bind_position=np.array(
            [bone.pos for bone in bones], dtype=np.float64
        ).reshape(count, 3),
        bind_quaternion=np.array(
            [bone.quat for bone in bones], dtype=np.float64
        ).reshape(count, 4),
        pose_to_bone=np.array(
            [bone.pose_to_bone for bone in bones], dtype=np.float64
        ).reshape(count, 3, 4),
        depth=depth,
        proc_type=proc_type,
        axis_interp=tuple(rules),
        faults=tuple(faults),
    )


# --- transforms --------------------------------------------------------------


def multiply(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """Compose two 3x4 transforms: the result applies `b` and then `a`."""
    rotation = a[..., :3] @ b[..., :3]
    translation = (a[..., :3] @ b[..., 3][..., None])[..., 0] + a[..., 3]
    return np.concatenate([rotation, translation[..., None]], axis=-1)


def invert_rigid(matrix: np.ndarray) -> np.ndarray:
    """Inverse of a rotation-and-translation 3x4.

    Only exact for a rigid transform; a caller differencing against a captured
    frame checks `row_length_error` first rather than assuming it.
    """
    rotation = np.swapaxes(matrix[..., :3], -1, -2)
    translation = -(rotation @ matrix[..., 3][..., None])[..., 0]
    return np.concatenate([rotation, translation[..., None]], axis=-1)


def rotation_from_quaternion(quaternion: np.ndarray) -> np.ndarray:
    """3x3 rotation of an (x, y, z, w) quaternion, normalized first."""
    quaternion = np.asarray(quaternion, dtype=np.float64)
    quaternion = quaternion / np.maximum(
        np.linalg.norm(quaternion, axis=-1, keepdims=True), 1.0e-30
    )
    x, y, z, w = np.moveaxis(quaternion, -1, 0)
    matrix = np.empty(quaternion.shape[:-1] + (3, 3), dtype=np.float64)
    matrix[..., 0, 0] = 1 - 2 * (y * y + z * z)
    matrix[..., 0, 1] = 2 * (x * y - z * w)
    matrix[..., 0, 2] = 2 * (x * z + y * w)
    matrix[..., 1, 0] = 2 * (x * y + z * w)
    matrix[..., 1, 1] = 1 - 2 * (x * x + z * z)
    matrix[..., 1, 2] = 2 * (y * z - x * w)
    matrix[..., 2, 0] = 2 * (x * z - y * w)
    matrix[..., 2, 1] = 2 * (y * z + x * w)
    matrix[..., 2, 2] = 1 - 2 * (x * x + y * y)
    return matrix


def matrices_from_local(position: np.ndarray, quaternion: np.ndarray) -> np.ndarray:
    """3x4 transforms from a per-bone position and quaternion."""
    rotation = rotation_from_quaternion(quaternion)
    translation = np.asarray(position, dtype=np.float64)[..., None]
    return np.concatenate([rotation, translation], axis=-1)


def payload_matrices(payload: bytes, bones: int, *, offset: int = 0) -> np.ndarray:
    """Read `bones` consecutive 3x4 float32 matrices out of a captured payload."""
    flat = np.frombuffer(
        payload, dtype="<f4", count=bones * 12, offset=offset
    ).astype(np.float64)
    return flat.reshape(bones, 3, 4)


def payload_local_pose(payload: bytes, bones: int) -> tuple[np.ndarray, np.ndarray]:
    """Read an evaluation payload's position and quaternion arrays.

    The hook copies two separate buffers, so the whole position array precedes
    the whole quaternion array rather than the two interleaving per bone.
    """
    positions = np.frombuffer(payload, dtype="<f4", count=bones * 3).astype(
        np.float64
    )
    quaternions = np.frombuffer(
        payload, dtype="<f4", count=bones * 4, offset=bones * 12
    ).astype(np.float64)
    return positions.reshape(bones, 3), quaternions.reshape(bones, 4)


def payload_selected(payload: bytes, bones: int) -> np.ndarray:
    """Read an evaluation payload's selected-bone mask as a boolean array."""
    words = (bones + 31) // 32
    raw = np.frombuffer(
        payload, dtype="<u4", count=words, offset=bones * 7 * 4
    )
    bits = np.unpackbits(raw.view(np.uint8), bitorder="little")
    return bits[:bones].astype(bool)


def slerp(a: np.ndarray, b: np.ndarray, alpha: np.ndarray) -> np.ndarray:
    """Shortest-arc quaternion interpolation, batched over the leading axes."""
    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64).copy()
    alpha = np.asarray(alpha, dtype=np.float64)[..., None]
    dot = np.sum(a * b, axis=-1, keepdims=True)
    b = np.where(dot < 0.0, -b, b)
    dot = np.clip(np.abs(dot), -1.0, 1.0)
    theta = np.arccos(dot)
    sine = np.sin(theta)
    safe = np.where(np.abs(sine) > 1.0e-12, sine, 1.0)
    mixed = np.sin((1.0 - alpha) * theta) / safe * a + np.sin(alpha * theta) / safe * b
    # Near-parallel inputs make the sine division useless; a straight lerp is
    # both stable and indistinguishable there.
    mixed = np.where(dot > 0.9995, (1.0 - alpha) * a + alpha * b, mixed)
    return mixed / np.maximum(
        np.linalg.norm(mixed, axis=-1, keepdims=True), 1.0e-30
    )


def axis_interp_local(
    rule: AxisInterp, world: np.ndarray, skeleton: Skeleton
) -> np.ndarray:
    """The local transform a `ProcType == 1` rule produces for its driven bone.

    Transcribed from `docs/vtmb/procedural_bones.md`, which owns the rule and the
    evidence for it. The driver is one column of the control bone's rotation
    taken back into its parent's frame — algebraically the same column of the
    control's own local rotation — whose three signed components select three of
    the six table entries and weight them.
    """
    axis = world[..., rule.control, :, rule.axis]
    parent = int(skeleton.parent[rule.control])
    if parent >= 0:
        axis = np.einsum("...ji,...j->...i", world[..., parent, :, :3], axis)

    positive = axis >= 0.0
    # X+ / X- are entries 0 and 1, Y+ / Y- are 2 and 3, Z+ / Z- are 4 and 5.
    picked = np.stack(
        [
            np.where(positive[..., 0], 0, 1),
            np.where(positive[..., 1], 2, 3),
            np.where(positive[..., 2], 4, 5),
        ],
        axis=-1,
    )
    weight = np.abs(axis)
    a1, a2, a3 = weight[..., 0], weight[..., 1], weight[..., 2]

    quaternion = rule.quaternion[picked]
    position = rule.position[picked]
    total = a1 + a2 + a3
    scale = np.where(total > 0.0, 1.0 / np.where(total > 0.0, total, 1.0), 0.0)
    pair = a1 + a2
    blended = slerp(
        quaternion[..., 1, :],
        quaternion[..., 0, :],
        np.where(pair > 0.0, a1 / np.where(pair > 0.0, pair, 1.0), 0.0),
    )
    mixed = slerp(blended, quaternion[..., 2, :], a3 * scale)
    interpolated = (
        (a1 * scale)[..., None] * position[..., 0, :]
        + (a2 * scale)[..., None] * position[..., 1, :]
        + (a3 * scale)[..., None] * position[..., 2, :]
    )
    # With no X or Y contribution the driver lies on the selected Z entry, and
    # the table entry is taken whole rather than interpolated against nothing.
    take = (pair > 0.0)[..., None]
    return matrices_from_local(
        np.where(take, interpolated, position[..., 2, :]),
        np.where(take, mixed, quaternion[..., 2, :]),
    )


def compose(
    local: np.ndarray,
    skeleton: Skeleton,
    root: np.ndarray | None = None,
    *,
    split: bool,
    seed: np.ndarray | None = None,
    selected: np.ndarray | None = None,
    procedural: bool = False,
) -> np.ndarray:
    """Bone-to-world for a batch of poses.

    Transcribed from `docs/vtmb/animation_and_movers.md` A.4a. This is not a
    shipped Elysium path — see the module docstring.

    `local` is `(N, n, 3, 4)`; `root` is `(N, 3, 4)` or None for model space.
    With `split`, a bone carrying `Flags & 0x2` takes its rotation from the root
    frame rather than from its parent, and its translation from its parent as
    usual. A bone outside `selected` is taken from `seed` rather than composed,
    which is how a stage is fed retail's own input for the slots retail wrote
    and nothing else; its children then compose off that seeded value.

    With `procedural`, a bone carrying a `ProcType == 1` table ignores its own
    animated local and takes the one the table produces instead. It is applied
    inside this walk rather than afterwards because the driven bone's children
    compose off the corrected matrix, and the driver is read from the control
    bone's already-composed world matrix. `docs/vtmb/procedural_bones.md` owns
    the rule.
    """
    local = np.asarray(local, dtype=np.float64)
    count = local.shape[-3]
    frame = IDENTITY if root is None else np.asarray(root, dtype=np.float64)
    rules = (
        {rule.bone: rule for rule in skeleton.axis_interp} if procedural else {}
    )
    world = np.empty_like(local)
    for index in range(count):
        parent = int(skeleton.parent[index])
        this = local[..., index, :, :]
        rule = rules.get(index)
        if rule is not None and parent >= 0:
            this = axis_interp_local(rule, world, skeleton)
        if parent < 0:
            composed = multiply(frame, this)
        elif split and bool(skeleton.split[index]):
            rotation = multiply(
                frame,
                np.concatenate(
                    [this[..., :3], np.zeros_like(this[..., 3][..., None])],
                    axis=-1,
                ),
            )[..., :3]
            translation = multiply(world[..., parent, :, :], this)[..., 3]
            composed = np.concatenate([rotation, translation[..., None]], axis=-1)
        else:
            composed = multiply(world[..., parent, :, :], this)
        if selected is not None and seed is not None:
            take = selected[..., index][..., None, None]
            composed = np.where(take, composed, seed[..., index, :, :])
        world[..., index, :, :] = composed
    return world


def palette(bone_to_world: np.ndarray, inverse_bind: np.ndarray) -> np.ndarray:
    """The skin palette: each bone's world frame times its inverse bind.

    Transcribed from the StudioRender palette loop; not a shipped Elysium path.
    """
    return multiply(bone_to_world, inverse_bind)


def regenerated_inverse_bind(skeleton: Skeleton) -> np.ndarray:
    """The inverse bind `mdl_gltf` writes, in Source space.

    `mdl_gltf._inverse_bind_accessor` discards `StudioBone.poseToBone` and
    inverts an ordinary hierarchy FK over the bind locals instead. Reproducing it
    here in Source space is what lets the difference against the stored
    `poseToBone` be priced without a glTF basis change in the way.
    """
    local = matrices_from_local(skeleton.bind_position, skeleton.bind_quaternion)
    world = np.empty_like(local)
    for index in range(skeleton.bones):
        parent = int(skeleton.parent[index])
        world[index] = (
            local[index] if parent < 0 else multiply(world[parent], local[index])
        )
    return invert_rigid(world)


# --- metrics -----------------------------------------------------------------


def translation_error(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """Euclidean distance between two 3x4 arrays' translation columns."""
    return np.linalg.norm(a[..., 3] - b[..., 3], axis=-1)


def position_error(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """Euclidean distance between two position arrays."""
    return np.linalg.norm(
        np.asarray(a, dtype=np.float64) - np.asarray(b, dtype=np.float64), axis=-1
    )


def rotation_angle_degrees(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """Angle between two quaternions, in degrees.

    The absolute dot product is what makes a quaternion and its negation read as
    the same rotation, which they are; a capture and a decode routinely disagree
    on the sign alone.
    """
    a = np.asarray(a, dtype=np.float64)
    b = np.asarray(b, dtype=np.float64)
    a = a / np.maximum(np.linalg.norm(a, axis=-1, keepdims=True), 1.0e-30)
    b = b / np.maximum(np.linalg.norm(b, axis=-1, keepdims=True), 1.0e-30)
    dot = np.clip(np.abs(np.sum(a * b, axis=-1)), 0.0, 1.0)
    return np.degrees(2.0 * np.arccos(dot))


def _orthonormalized(matrix: np.ndarray) -> np.ndarray:
    """A 3x4's rotation block with each row scaled to unit length."""
    rows = matrix[..., :3]
    return rows / np.maximum(
        np.linalg.norm(rows, axis=-1, keepdims=True), 1.0e-30
    )


def matrix_rotation_angle_degrees(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """Angle of the relative rotation between two 3x4 arrays, in degrees.

    Both rotation blocks are scaled to unit rows first, because `arccos` near
    one is violently sensitive to scale: a row 3e-4 short — which is the ordinary
    float32 noise a captured `matrix3x4_t` carries — moves `(trace - 1) / 2` to
    0.9991 and reads as a 2.4 degree rotation that is not there. Orthonormality
    is a real property and is measured on its own by `row_length_error`; folding
    it into the angle would report it twice and as the wrong thing.
    """
    relative = np.swapaxes(_orthonormalized(a), -1, -2) @ _orthonormalized(b)
    trace = np.trace(relative, axis1=-2, axis2=-1)
    return np.degrees(np.arccos(np.clip((trace - 1.0) * 0.5, -1.0, 1.0)))


def is_frame(matrix: np.ndarray) -> np.ndarray:
    """Whether a 3x4 holds a frame at all, rather than an unwritten slot.

    A slot nothing wrote is zeros, and two zero matrices compare as identical on
    translation and as `acos(-0.5)` — 120 degrees — on rotation. Neither number
    is a comparison, so a caller excludes the bone instead of reporting either.
    The test is the determinant rather than a tolerance, because the distinction
    is between a rotation and no rotation and not between two rotations.
    """
    return np.abs(np.linalg.det(matrix[..., :3])) > 0.5


def row_length_error(matrix: np.ndarray) -> np.ndarray:
    """The worst departure from unit length among a 3x4's three rotation rows."""
    lengths = np.linalg.norm(matrix[..., :3], axis=-1)
    return np.max(np.abs(lengths - 1.0), axis=-1)


def band_of(values: np.ndarray, edges: tuple[float, float]) -> np.ndarray:
    """0 excellent, 1 investigate, 2 definite mismatch."""
    excellent, investigate = edges
    return (
        (values > excellent).astype(np.int8) + (values > investigate).astype(np.int8)
    )


class BoneAccumulator:
    """Per-bone error statistics folded from batches, holding no value list.

    A corpus is hundreds of thousands of records over up to 96 bones, so a
    reader that kept the values would not fit. Maxima and band counts are exact;
    quantiles come from a log-spaced histogram and are the upper edge of the
    bucket the true value falls in, accurate to `QUANTILE_RESOLUTION`.
    """

    def __init__(self, bones: int, edges: tuple[float, float]) -> None:
        self.bones = bones
        self.edges = edges
        self.maximum = np.zeros(bones, dtype=np.float64)
        self.count = np.zeros(bones, dtype=np.int64)
        self.banded = np.zeros((bones, len(BAND_NAMES)), dtype=np.int64)
        self.histogram = np.zeros((bones, HISTOGRAM_BUCKETS), dtype=np.int64)

    def add(self, values: np.ndarray, multiplier: np.ndarray | int = 1) -> None:
        """Fold `(N, n)` or `(n,)` errors in, each weighted by its record count."""
        values = np.atleast_2d(np.asarray(values, dtype=np.float64))
        weight = np.broadcast_to(
            np.asarray(multiplier, dtype=np.int64).reshape(-1, 1), values.shape
        )
        self.maximum = np.maximum(self.maximum, values.max(axis=0))
        self.count += weight.sum(axis=0)
        band = band_of(values, self.edges)
        for index in range(len(BAND_NAMES)):
            self.banded[:, index] += np.where(band == index, weight, 0).sum(axis=0)
        bucket = np.clip(
            np.floor(
                np.log10(np.maximum(values, HISTOGRAM_LOW) / HISTOGRAM_LOW)
                / math.log10(HISTOGRAM_HIGH / HISTOGRAM_LOW)
                * HISTOGRAM_BUCKETS
            ).astype(np.int64),
            0,
            HISTOGRAM_BUCKETS - 1,
        )
        flat = (
            np.arange(self.bones, dtype=np.int64)[None, :] * HISTOGRAM_BUCKETS
            + bucket
        )
        self.histogram += (
            np.bincount(
                flat.ravel(),
                weights=weight.ravel(),
                minlength=self.bones * HISTOGRAM_BUCKETS,
            )
            .reshape(self.bones, HISTOGRAM_BUCKETS)
            .astype(np.int64)
        )

    def quantile(self, q: float) -> np.ndarray:
        """Per-bone upper bound on the `q` quantile, from the histogram."""
        total = self.histogram.sum(axis=1)
        cumulative = np.cumsum(self.histogram, axis=1)
        target = np.ceil(total * q)[:, None]
        index = np.argmax(cumulative >= np.maximum(target, 1), axis=1)
        edge = HISTOGRAM_LOW * (
            (HISTOGRAM_HIGH / HISTOGRAM_LOW) ** ((index + 1) / HISTOGRAM_BUCKETS)
        )
        return np.where(total > 0, edge, 0.0)

    def summary(self) -> dict:
        """The reported shape: exact maximum and band split, bounded quantiles."""
        total = int(self.count.sum())
        return {
            "values": total,
            "max": float(self.maximum.max(initial=0.0)),
            "median": float(self.quantile(0.5).max(initial=0.0)),
            "p99": float(self.quantile(0.99).max(initial=0.0)),
            "bands": {
                name: int(self.banded[:, index].sum())
                for index, name in enumerate(BAND_NAMES)
            },
        }
