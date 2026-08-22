"""Export a VtMB `.mdl` v2531 skeletal model to Elysium's own `.eskm` container.

`.eskm` is what the character bake reads. It is Unreal-native by the `UE_` convention --
centimetres, Z-up, left-handed, triangle winding already reversed, quaternions already
conjugated into the reflected frame -- so `FElysiumSkeletalSource` on the C++ side reads
every number verbatim and the engine's own authoring path (`FMeshDescription` +
`FSkeletalMeshAttributes`) receives it unchanged.

Two products, the same container either way:

- `write_model` -> `<out>/<stem>.eskm`: skeleton + LOD0 geometry + facial morph targets +
  the model's OWN clips.
- `write_bank`  -> `<out>/banks/<stem>.eskm`: skeleton + clips, no geometry -- a shared
  animation library bound to a target skeleton by bone name.

A bank carries a skeleton section for exactly one reason: its clip tracks address bones by
index, and the names in that section are what turn an index into a bone on the skeleton
being bound to.

Container layout, little-endian throughout. A string is `u32 length` then that many UTF-8
bytes, unterminated.

    header      char magic[4] = "ESKM", u32 version, u32 sectionCount, u32 reserved
    directory   sectionCount x { char tag[4], u64 offset, u64 size }   offsets from file start

    "SKEL"  u32 boneCount
            boneCount x { string name, i32 parent, f32 t[3], f32 q[4] }
    "ATCH"  u32 attachmentCount
            attachmentCount x { string name, u32 bone, f32 t[3], f32 q[4] }
    "DYNM"  u32 chainCount
            chainCount x { string firstBone, string chainEnd,
                           f32 gravityScale, f32 damping,
                           f32 angularSpring, f32 coneAngleDegrees }
    "BDYN"  u32 bodyCount
            bodyCount x { string boundBone,
                           f32 gravityScale, f32 damping,
                           f32 angularSpring, f32 coneAngleDegrees }
    "MATL"  u32 materialCount
            materialCount x { string name, string albedo }             albedo relative to the
                                                                       export root, "" if none
    "MESH"  u32 vertexCount, u32 triangleCount, u32 sectionCount
            sectionCount x { string material, u32 firstTriangle, u32 triangleCount }
            vertexCount   x { f32 p[3], f32 n[3], f32 uv[2], u16 bone[4], f32 weight[4] }
            triangleCount x { u32 index[3] }
    "MORF"  u32 morphCount
            morphCount x { string name, u32 deltaCount,
                           deltaCount x { u32 vertex, f32 dp[3], f32 dn[3] } }
    "MASK"  u32 maskCount
            maskCount x { u32 boneCount, boneCount x u8 }       1 = the clip owns this bone
    "ANIM"  u32 clipCount
            clipCount x { string name, u32 frameCount, f32 frameRate, u32 flags,
                          i32 mask, u32 trackCount,
                          trackCount x { u32 bone, u8 hasTranslation, u8 hasRotation,
                                         hasTranslation ? f32 t[3] x frameCount,
                                         hasRotation    ? f32 q[4] x frameCount } }

A clip's `mask` indexes "MASK", or is -1 when the clip owns every bone. The table is
de-duplicated across the file because a bank states only a handful of distinct masks over
hundreds of clips.

The directory exists so a reader can skip a section it does not understand and so a bank,
which has no geometry, is the same file shape as a body rather than a special case.
"""
import os
import struct

from elysium_pipeline.formats import bsp, mdl, mdl_secondary_motion as SM, mdl_skel as S

#: Bumped whenever a section's payload changes meaning. `FElysiumSkeletalSource` refuses a
#: file it does not recognise rather than reading a stale layout as if it were current.
#:
#: 4 -- a clip names the clip it is a difference FROM, empty for a pose of its own. An additive is
#: emitted once per declaring host, composed onto that host's own pose, because converting VtMB's
#: post-multiplied delta into Unreal's pre-multiplied one is a conjugation by the base's rotation
#: and the answer differs by up to 81 deg across the hosts one delta serves.
#:
#: 3 -- a clip carries the index of its per-bone `weight`@0 mask, and the masks themselves ship as
#: a de-duplicated "MASK" table. Without it a bone the clip leaves at its bind pose and a bone the
#: clip does not own are both "no track", which is the same bytes for two opposite results when a
#: partial-body overlay is composed.
#:
#: 2 -- ANIM rotations for a bone carrying `SPLIT_ROTATION` are written pre-corrected, so ordinary
#: inheritance reproduces the pose VtMB draws and the runtime applies no rule of its own. A version
#: 1 container states the same bytes with the opposite meaning, and nothing in the payload
#: distinguishes them, so a stale file has to be refused rather than read.
#: 5 -- "MESH" carries the authored per-vertex shading normal. VtMB stores one on every skinned
#: vertex, and it holds the artist's smoothing including the split normals at a hard edge, which no
#: averaging over adjacent faces can reproduce. A version 4 container states a vertex record of a
#: different width, so a stale file has to be refused rather than read.
#: 6 -- an owned bone carries a complete donor-local translation and rotation even when one or all
#: seven RLE offsets are zero. Zero weight remains no track. This makes the baked clip's pose
#: self-describing instead of letting Unreal substitute a consuming family's reference local.
#: 7 -- the host of a derived additive carries a complete bind track for every bone the additive
#: owns, including owned bones with seven zero RLE offsets. Both sides of Unreal's additive
#: subtraction now state the same donor local instead of one side falling through to family bind.
#: 8 -- the "MESH" influence block is four slots wide, carrying the fourth bone a VtMB vertex may
#: weight rather than the three a 2004 hardware palette could hold.
VERSION = 8

#: Separates an additive's own label from the label of the host it was composed onto, in the name
#: of a derived clip. A VtMB sequence label never contains it, so the split is unambiguous.
BASE_SEPARATOR = "@"

MAGIC = b"ESKM"

#: A VtMB SKINNED `StudioVertex` weights up to four bones: `BoneWeight` stores three weight bytes
#: and four bone indices, and the fourth weight is the shortfall `255 - sum` (`mdl_skel.read_skin`).
#: The block is fixed-width at four, so a reader needs no per-vertex length and an unused slot is
#: a zero weight.
MAX_INFLUENCES = 4

#: StudioBone flag 0x2 (`docs/vtmb/animation_and_movers.md`): the bone's ROTATION declines its
#: parent and roots in the character, while its translation still rides the parent. One bone per
#: biped, always `Bip01 Spine1`.
SPLIT_ROTATION = 0x2

#: StudioSeqDesc flag 0x4 (`STUDIO_DELTA`): the clip stores a difference from a base pose rather
#: than a pose of its own.
DELTA_SEQUENCE = 0x4


def _string(text):
    raw = (text or "").encode("utf-8")
    return struct.pack("<I", len(raw)) + raw


def _conv_pos(p):
    return bsp.source_to_unreal(p[0], p[1], p[2])


def _conv_dir(p):
    return bsp.source_dir_to_unreal(p[0], p[1], p[2])


def _conv_quat(q):
    return bsp.source_quat_to_unreal(q[0], q[1], q[2], q[3])


#: The three Source axes a procedural rule's terms are indexed by, carried into Unreal space by
#: the same basis change the skeleton and the clips go through. `+ 0.0` normalizes `-0.0` away so
#: the exported vectors read as the signed unit vectors they are.
DRIVER_AXES = [[c + 0.0 for c in _conv_dir(a)] for a in S.SOURCE_AXES]


def unreal_axis_rules(records):
    """`mdl_skel.axis_interp_records` stated Unreal-native -> the `procedural/` sidecar's rows.

    The basis is the difficulty. The six entries and the axis index are Source quantities, and
    a change of basis conjugates a bone local -- so the axis a rule names is not the same axis
    afterwards, and may be negated. Carrying the axis as a *direction* and the three term
    weights as the images of the Source axes states the rule in the body's own space: with `w`
    the control bone's local rotation applied to `axis`, term `k`'s signed weight is
    `dot(DRIVER_AXES[k], w)`, a positive weight selecting entry `2k` and a negative one `2k+1`.

    The entries go through the same `_conv_pos`/`_conv_quat` that write the skeleton and the
    clips, which makes the table and the container consistent by construction rather than by
    agreement -- and leaves the runtime nothing to convert.
    """
    return [{
        "bone": r["bone"], "bone_index": r["bone_index"],
        "control": r["control"], "control_index": r["control_index"],
        "axis": list(DRIVER_AXES[r["axis_index"]]),
        "pos": [[float(c) for c in _conv_pos(p)] for p in r["pos"]],
        # Exact rather than through the rotation matrix: `source_quat_to_unreal` is a component
        # negation, so it keeps which of the two quaternions naming the rotation comes back and
        # enough precision to invert to the float32 the value was read from. A stored table is
        # read back and re-evaluated rather than only drawn, so both matter to it.
        "quat": [[float(c) for c in _conv_quat(q)] for q in r["quat"]],
    } for r in records]


def unreal_eye_rig(rig):
    """`mdl_skel.eye_records` stated Unreal-native -> the `eyes/` sidecar's payload.

    `org` is a point and `up`/`forward` are directions, so they take the position and direction
    conversions respectively. Everything else the record carries is a magnitude, a ratio or a
    name and is already frame-free; `uppertarget` in particular is a linear offset read through
    `asin(t / radius)` against a radius in the same units, so converting it alone would break
    the ratio.

    Returns a new payload; the records it was given are left alone, so a caller that reports on
    them afterwards still sees what the file said.
    """
    if not rig:
        return rig
    return {
        **rig,
        "eyeballs": [{
            **e,
            "org": [float(c) for c in _conv_pos(e["org"])],
            "up": [float(c) for c in _conv_dir(e["up"])],
            "forward": [float(c) for c in _conv_dir(e["forward"])],
        } for e in rig["eyeballs"]],
    }


def unreal_swings(records):
    """`mdl_skel.read_swing_records` stated Unreal-native -> the clip sidecar's `swings` rows.

    `a`/`b` are bone-local *points*, so they take `_conv_pos` -- the same inch-to-centimetre
    scale and Y reflection that `_attachment_section` puts an attachment's bone-local translation
    through and that `_skeleton_section` puts every bone's own bind translation through. That is
    what keeps the segment inside the bone it is stated in: the bone frame the runtime holds is
    the reflected one, so a segment converted any other way would sweep the mirrored side of the
    limb.

    Everything else the record carries is already frame-free. The window pair is a fraction of the
    clip cycle and a fraction has no units, the knockback names and the bone name are names, and
    the two bytes are raw bytes. `degenerate` is stated on every row rather than only the four it
    is true on, because a consumer forbidden to repair authored data has to read the flag rather
    than infer it from a window it re-tests itself.
    """
    return [{
        "start": round(r.start, 6),
        "end": round(r.end, 6),
        "bone": r.bone,
        # `+ 0.0` normalizes the `-0.0` the Y reflection produces on a zero component away, the
        # same way `DRIVER_AXES` does, so a segment reads as the axis-aligned one it is.
        "a_cm": [round(float(c) + 0.0, 4) for c in _conv_pos(r.a)],
        "b_cm": [round(float(c) + 0.0, 4) for c in _conv_pos(r.b)],
        "kb_names": [list(bucket) for bucket in r.knockback],
        "b8": r.byte_b8,
        "ba": r.byte_ba,
        "degenerate": r.degenerate,
    } for r in records]


def _qmul(a, b):
    """Hamilton product, matching `FUN_1010a450` and Unreal's `FQuat::operator*` convention."""
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def _qconj(q):
    """The inverse of a unit quaternion."""
    return (-q[0], -q[1], -q[2], q[3])


def _qrotate(q, v):
    """`v` rotated by unit quaternion `q`, via the sandwich product `q * (v, 0) * q^-1`, in the
    same Hamilton convention as `_qmul`."""
    x, y, z, _w = _qmul(_qmul(q, (v[0], v[1], v[2], 0.0)), _qconj(q))
    return (x, y, z)


#: Below this length a stored quaternion is not a rotation at all. VtMB's masked `_layer` clips
#: store all-zero rotations for the bones their per-bone weight mask excludes -- the accumulator
#: multiplies those bones out rather than posing them, so the value is padding the file carries
#: rather than data anything reads.
_QUAT_EPSILON = 1e-6


def _qnorm(q):
    """The unit form of `q`, or None when it carries no rotation at all."""
    length = (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]) ** 0.5
    if length < _QUAT_EPSILON:
        return None
    return (q[0] / length, q[1] / length, q[2] / length, q[3] / length)


def _split_ancestors(bones, index):
    """`index`'s ancestors, root first. Four bones deep on a biped, not the whole rig."""
    chain = []
    parent = bones[index].parent
    while parent >= 0:
        chain.append(parent)
        parent = bones[parent].parent
    chain.reverse()
    return chain


def _split_rotation_tracks(bones, frames, frame_count):
    """Rewrite each split bone's rotation so ORDINARY inheritance reproduces retail's pose.

    VtMB poses a bone carrying `SPLIT_ROTATION` by taking its animated rotation as the
    model-space rotation outright -- the parent's rotation is skipped -- while the translation
    still composes through the parent. Unreal has no such rule and no way to express one on an
    asset, so the clip is rewritten to hold what conventional inheritance needs in order to
    land in the same place:

        local'[i] = world_rot(parent)^-1 * local[i]

    `world_rot(parent)` is composed from this clip's OWN frame, which is why the correction is
    per frame and per clip rather than a constant baked once. Children of the split bone
    inherit the corrected rotation and need no rewrite of their own, and the bind pose needs
    none either -- VtMB's own `poseToBone` inverse binds are conventional, so only the live
    pose ever disagreed.

    A clip carrying no rotation channel for the bone still gets a track. The bone holds its
    bind rotation there and retail applies the rule to that too, so leaving the channel absent
    would bake the conventional bind where retail draws the declined one.

    Where the ancestor chain does not compose to a rotation at all, the authored rotation is kept
    unchanged. A masked `*_layer` zeroes the bones its weight mask excludes, and the upper-body
    mask -- the only one of the four that owns this bone -- excludes the whole chain above it, so
    the parent rotation this divides by is not in the file: at runtime it arrives from whatever
    host the overlay rides.

    **Substituting the BIND chain there does not work, and the reason is measured.** The chain is
    `Bip01`, `Bip01 Pelvis`, `Bip01 Spine`, and retail's rule skips all three -- `Bip01` is an
    ordinary animated bone, not the entity transform, so its own rotation is declined along with
    the rest. Every clip turns it: 63 deg from bind on a weapon idle, 82-99 on a walk or run. An
    overlay normalised against the bind chain therefore arrives rotated by the character's own
    root rotation, which is worse than the model-space value it was correcting.

    So this clip needs the host's chain, and the host is named -- by the same autolayer table
    that names an additive's base. The fix is `_composed_frames`' shape: emit the overlay once
    per declaring host, against that host's own pose.

    Returns {bone index: [quaternion per frame]}, empty when the model flags no split bone. A
    bone absent from the returned map keeps whatever channel the clip authored for it, which for
    an unauthored one is no channel at all.
    """
    split = [b.index for b in bones if (b.flags & SPLIT_ROTATION) and b.parent >= 0]
    # A bone the clip carries no animation record for at all reads back as a ZERO quaternion, not
    # as its bind rotation -- `read_anim` only fills the bones it finds a record for. That is not a
    # rotation to correct, and emitting it would be strictly worse than emitting nothing: an absent
    # channel leaves the bone on the container's bind pose, while a zero quaternion reaches the
    # runtime as IDENTITY, because `FElysiumSkeletalSource::Load` normalises every key it reads.
    # 18 of `move_and_ranged`'s masked `_layer` sequences reach this bone that way.
    split = [index for index in split
             if all(_qnorm(frames[frame][index][1]) is not None
                    for frame in range(frame_count))]
    if not split:
        return {}
    needed = set()
    for index in split:
        needed.update(_split_ancestors(bones, index))
    # StudioBone order states a parent before its children, so one ascending pass composes the
    # chain. Only the flagged bones' ancestors are composed -- four bones on a biped, against
    # eighty for the whole rig, over every frame of every clip in the cast.
    order = sorted(needed)
    out = {index: [] for index in split}
    for frame in range(frame_count):
        world = {}
        for index in order:
            local = frames[frame][index][1]
            parent = bones[index].parent
            if parent < 0 or (bones[index].flags & SPLIT_ROTATION):
                world[index] = _qnorm(local)
            elif world[parent] is None:
                world[index] = None
            else:
                world[index] = _qnorm(_qmul(world[parent], local))
        for index in split:
            local = frames[frame][index][1]
            parent_world = world[bones[index].parent]
            out[index].append(local if parent_world is None
                              else _qmul(_qconj(parent_world), local))
    return out


def _bone_subtree_sizes(parents):
    """Per-index descendant count, from each row's OWN parent chain rather than a child-list
    build -- a few bones deep on any of these rigs, negligible against reading a whole skeleton
    once."""
    sizes = [0] * len(parents)
    for i, p in enumerate(parents):
        while p >= 0:
            sizes[p] += 1
            p = parents[p]
    return sizes


def _reparent_local(root_pos, root_quat, stray_pos, stray_quat):
    """`(stray_pos, stray_quat)` restated as a child local under `(root_pos, root_quat)`,
    preserving its MODEL-SPACE transform.

    A StudioBone with no parent has nothing to be relative to, so its stored (pos, quat) already
    names its model-space bind -- exactly like the root it is being folded under here. Composing
    `new_local = root^-1 * stray` in that shared Source-space convention, before either value
    passes through `_conv_pos`/`_conv_quat`, reproduces `stray`'s original model-space transform
    once ordinary FK recomposes it under `(root_pos, root_quat)`.

    Raises where the composed rotation degenerates to a zero quaternion, rather than baking a
    silently wrong (or NaN) bind -- the caller names which bones and skeleton were involved."""
    root_inv = _qconj(root_quat)
    delta = tuple(s - r for s, r in zip(stray_pos, root_pos))
    new_pos = _qrotate(root_inv, delta)
    new_quat = _qnorm(_qmul(root_inv, stray_quat))
    if new_quat is None:
        raise ValueError("reparented bone rotation degenerates to a zero quaternion")
    return new_pos, new_quat


def _single_root(rows):
    """`rows` (`[(name, parent, pos, quat)]`, `parent` local to this same list, `-1` for a root)
    restated single-rooted -> (rows, permutation, reparented).

    Unreal's reference skeleton asserts on a second parent-less bone. A few VtMB skeletons fork
    (`regular_cop` bones 0/1, `prophet` bones 0/59, and a cinematic actor's own `BipNN` subset can
    fork the same way). Splicing a synthetic bone above the fork satisfies single-rootedness but
    breaks a different, unwritten Unreal contract: `FSkeletonRemapping::GenerateMapping`
    (`SkeletonRemapping.cpp`) matches bones by name and then forces index 0 onto index 0
    regardless of what it is named, so a shared animation bank's real `Bip01` track lands on the
    synthetic bone and the body's own `Bip01` is left on its static bind -- the two-actor pose
    bug this fixes.

    So the fork is resolved onto one of ITS OWN bones instead: the parent-less bone with the
    LARGEST descendant subtree, ties broken by lowest original index so the choice is
    deterministic across runs. Every other parent-less bone is reparented onto it with
    `_reparent_local`, which keeps its model-space bind exactly where it was, and the whole list
    is re-emitted in topological order -- parents before children, which a newly-reparented bone
    may now require (e.g. `regular_cop`'s `tongue`, bone 0, moving under `Bip01`, bone 1, the
    larger subtree).

    Returns the re-ordered rows, `permutation` (original row index -> emitted slot; the identity
    when `rows` was already single-rooted), and `reparented` (`{stray original index: chosen
    root's original index}`, empty when nothing forked) -- the last of which lets a caller redo
    this same composition over a DIFFERENT set of per-bone values (a reference-pose override)
    instead of over the bind this function reads out of `rows` itself.
    """
    n = len(rows)
    parents = [row[1] for row in rows]
    roots = [i for i, p in enumerate(parents) if p < 0]
    if len(roots) <= 1:
        return list(rows), list(range(n)), {}

    sizes = _bone_subtree_sizes(parents)
    chosen = min(roots, key=lambda i: (-sizes[i], i))
    root_name, _root_parent, root_pos, root_quat = rows[chosen]

    rows = list(rows)
    reparented = {}
    for r in roots:
        if r == chosen:
            continue
        name, _parent, pos, quat = rows[r]
        try:
            new_pos, new_quat = _reparent_local(root_pos, root_quat, pos, quat)
        except ValueError as exc:
            raise ValueError(
                f"cannot reparent bone {name!r} onto root {root_name!r}: {exc}") from exc
        rows[r] = (name, chosen, new_pos, new_quat)
        parents[r] = chosen
        reparented[r] = chosen

    emitted = [False] * n
    order = []

    def _emit(i):
        if emitted[i]:
            return
        if parents[i] >= 0:
            _emit(parents[i])
        emitted[i] = True
        order.append(i)

    for i in range(n):
        _emit(i)

    position_of = [0] * n
    for slot, original in enumerate(order):
        position_of[original] = slot
    new_rows = [(rows[i][0], -1 if parents[i] < 0 else position_of[parents[i]],
                rows[i][2], rows[i][3]) for i in order]
    return new_rows, position_of, reparented


def unreal_bones(bones):
    """The model's bones in Unreal reference-skeleton order -> (rows, bone_map, reparented).

    A row is `(name, parent, source position, source quaternion)`. `bone_map` takes an original
    StudioBone index to its emitted index, and every other section indexes bones through it -- a
    vertex's influences and a clip's tracks both address the emitted list. `reparented` is
    `_single_root`'s fork-resolution record, `{stray original index: chosen root's original
    index}`; `_ref_pose_rows` is the one caller that needs it, to redo the same composition over a
    caller-supplied override instead of over this function's own bind."""
    rows = [(b.name, b.parent, b.pos, b.quat) for b in bones]
    return _single_root(rows)


def _skel_section(rows):
    out = bytearray(struct.pack("<I", len(rows)))
    for name, parent, pos, quat in rows:
        out += _string(name)
        out += struct.pack("<i", parent)
        out += struct.pack("<3f", *_conv_pos(pos))
        out += struct.pack("<4f", *_conv_quat(quat))
    return bytes(out)


def _ref_pose_rows(rows, bone_map, ref_pose, context, reparented=None):
    """`rows` with each real bone's (pos, quat) replaced by `ref_pose`'s entry for it.

    `ref_pose` is indexed by ORIGINAL StudioBone index -- `wield_corpus.bake_pose`'s shape, in
    source-space conventions, before `unreal_bones` renumbers into emitted order -- so this
    inverts `bone_map` (original -> emitted) to find, for each row, the original index it came
    from. `bone_map` is a genuine permutation over every StudioBone (`unreal_bones`), so every row
    has one.

    `reparented` (`unreal_bones`'s third return, `{stray original index: chosen root's original
    index}`) names the rows `_single_root` folded onto a different bone's local frame. Such a row
    cannot take `ref_pose`'s entry for it directly -- that entry is the bone's own MODEL-SPACE
    override, the same convention a parent-less StudioBone's stored bind already carries, and this
    row's parent is no longer `-1`. It is recomposed the same way `_single_root` composed the
    bind: relative to the CHOSEN root's own `ref_pose` entry, through the same `_reparent_local`
    math, so the override reproduces the model-space pose the caller asked for once ordinary FK
    recomposes it under the reparented row's new parent. Every other row substitutes `ref_pose`
    directly, and passes through `_skel_section`'s own `_conv_pos`/`_conv_quat` exactly like a
    bind value does -- this only changes which transform reaches that conversion, never how.

    A length that does not match `bone_map` -- one entry per StudioBone -- is a caller defect: a
    silently truncated or padded override would bake a wrong reference pose with nothing in the
    file to say so, so this fails loudly instead of guessing.
    """
    if ref_pose is None:
        return rows
    if len(ref_pose) != len(bone_map):
        raise ValueError(
            f"{context}: ref_pose has {len(ref_pose)} entries, expected {len(bone_map)} "
            f"(one per StudioBone)")
    reparented = reparented or {}
    origin = [None] * len(rows)
    for original, emitted in enumerate(bone_map):
        origin[emitted] = original
    out = []
    for row, (name, parent, pos, quat) in enumerate(rows):
        original = origin[row]
        chosen = reparented.get(original)
        if chosen is None:
            out.append((name, parent) + tuple(ref_pose[original]))
            continue
        root_pos, root_quat = ref_pose[chosen]
        stray_pos, stray_quat = ref_pose[original]
        try:
            new_pos, new_quat = _reparent_local(root_pos, root_quat, stray_pos, stray_quat)
        except ValueError as exc:
            raise ValueError(
                f"{context}: ref_pose reparent of StudioBone {original} ({name!r}) onto "
                f"StudioBone {chosen}: {exc}") from exc
        out.append((name, parent, new_pos, new_quat))
    return out


def _attachment_section(d, bone_map, extra_attachments=None):
    """Model-authored bone-local attachments, resolved into the emitted skeleton indices.

    `extra_attachments`, when given, is a list of `mdl_skel.Attachment` records synthesised by
    the caller (not read from `d`) -- e.g. the wield bake's `TrailTip` socket -- appended after
    the model's own authored attachments. `record.bone` is a raw StudioBone index into the same
    `bones` array `bone_map` was built from, exactly like an authored record's.
    """
    records = list(S.attachments(d)) + list(extra_attachments or ())
    if not records:
        return b""
    out = bytearray(struct.pack("<I", len(records)))
    for record in records:
        out += _string(record.name)
        out += struct.pack("<I", bone_map[record.bone])
        out += struct.pack("<3f", *_conv_pos(record.pos))
        out += struct.pack("<4f", *_conv_quat(record.quat))
    return bytes(out)


def _matl_section(matnames, matinfo):
    out = bytearray(struct.pack("<I", len(matnames)))
    for name in matnames:
        albedo = (matinfo.get(name) or {}).get("albedo") or ""
        out += _string(name)
        out += _string(f"tex/{albedo}" if albedo else "")
    return bytes(out)


def _surface_normals(surface):
    """One surface's per-vertex shading normals, Unreal-native and unit length.

    VtMB authors a normal on every skinned vertex (`StudioVertex.VecNormal`,
    `docs/vtmb/mdl_v2531.md`), and it carries the artist's smoothing -- including the split
    normals at a hard edge, where studiomdl duplicated the vertex so each copy could face its own
    way. Averaging adjacent face normals cannot reproduce a split by construction, so the authored
    value is the only source for it.

    **Converted, never negated.** The Source->Unreal Y flip is orthogonal, so a normal carries
    through `_conv_dir` exactly like a position and needs no sign compensation. The reflection is
    answered once, by reversing triangle winding in `_mesh_section`, and answering it a second
    time here would leave every normal facing into the surface it belongs to.

    The check that settles the sign is the divergence theorem, because a wrong answer fails it: a
    closed mesh whose triangles wind so that `cross(b - a, c - a)` faces out has positive signed
    volume. Over the exported winding that quantity is NEGATIVE on every model in the cast, so
    outward is the other order -- which is what the fallback below computes, and what the
    converted authored normal already agrees with. Comparing a normal against a face normal of
    one's own choosing does not settle it; both can carry the same convention error and agree.

    A few vertices across the corpus store `(0,0,0)`. Those, and only those, take the
    area-weighted geometric normal, so every exported vertex carries a usable one and the bake is
    never left deciding what a missing normal means.
    """
    out = [_conv_dir(n) for n in surface["nrm"]]
    missing = [i for i, n in enumerate(out) if n == (0.0, 0.0, 0.0)]
    if not missing:
        return out

    positions = [_conv_pos(p) for p in surface["pos"]]
    accumulated = [[0.0, 0.0, 0.0] for _ in out]
    for i, j, k in surface["tris"]:
        # The exported winding is (i, k, j), whose outward direction is cross(j - i, k - i) --
        # the order that makes the mesh's signed volume positive, per the docstring.
        ax, ay, az = (positions[j][c] - positions[i][c] for c in range(3))
        bx, by, bz = (positions[k][c] - positions[i][c] for c in range(3))
        # Unnormalised: the magnitude is twice the triangle area, which is the weighting.
        face = (ay * bz - az * by, az * bx - ax * bz, ax * by - ay * bx)
        for corner in (i, j, k):
            for c in range(3):
                accumulated[corner][c] += face[c]

    for i in missing:
        x, y, z = accumulated[i]
        length = (x * x + y * y + z * z) ** 0.5
        # A vertex no triangle references, or one whose faces cancel exactly. Up is arbitrary but
        # deterministic, and it beats emitting a zero the bake would have to interpret.
        out[i] = (x / length, y / length, z / length) if length > 1e-12 else (0.0, 0.0, 1.0)
    return out


def _model_space_pose(bones, locals_, context):
    """Each bone's `(pos, quat)` MODEL-SPACE transform, composed by ordinary FK over `locals_`.

    `locals_` is one source-space `(pos, quat)` per StudioBone, parent-relative -- `Bone.pos`/
    `Bone.quat`'s own convention, in which a parent-less bone's local already IS its model-space
    transform. Composition is deliberately CONVENTIONAL, with no `SPLIT_ROTATION` branch: these
    frames exist to relate geometry to the SKEL section's rows, and every consumer of those rows
    (Unreal's reference skeleton, `CalculateInvRefMatrices`) composes them by plain inheritance,
    so a rule applied here and not there would re-open the very disagreement this closes.
    StudioBone order states a parent before its children, so one ascending pass composes it.

    A zero quaternion cannot name a rotation, so it fails loudly with the bone rather than baking
    a silently wrong frame -- `read_anim` writes one for a masked-out bone, and a caller passing
    such a pose here is a caller defect, not data to guess around.
    """
    out = []
    for index, bone in enumerate(bones):
        pos, quat = locals_[index]
        quat = _qnorm(quat)
        if quat is None:
            raise ValueError(
                f"{context}: bone {index} ({bone.name!r}) carries a zero quaternion")
        if bone.parent < 0:
            out.append((tuple(pos), quat))
            continue
        parent_pos, parent_quat = out[bone.parent]
        out.append((
            tuple(p + o for p, o in zip(parent_pos, _qrotate(parent_quat, pos))),
            _qmul(parent_quat, quat),
        ))
    return out


def _reskin_surfaces(surfaces, bones, ref_pose, context):
    """Restate every surface's geometry from the container's bind space into `ref_pose`'s space,
    in place.

    The MESH section's vertices are the model's bind-pose model-space geometry, and Unreal reads
    them as component-space positions OF THE REFERENCE POSE the SKEL section states
    (`CalculateInvRefMatrices` derives the skinning inverses from those rows). Overriding the
    reference pose without restating the geometry therefore bakes a self-contradictory container:
    the drawn mesh sits off its own skeleton by exactly each skinned bone's bind->override
    model-space delta -- 68 cm on `w_m_baseball_bat`, 120-144 cm on the pistols, the wield
    corpus's placement defect. So each vertex is pushed through its influences' rigid moves
    `T(b) = M_ref(b) * M_bind(b)^-1` -- ordinary linear-blend skinning from the bind frame to the
    override frame -- and its authored normal rotates the same way, leaving the drawn composition
    `W(b) * M_ref(b)^-1 * v_ref` equal to retail's `W(b) * M_bind(b)^-1 * v_bind` on every bone.

    Influence handling mirrors what actually reaches the file and the runtime builder: the first
    `MAX_INFLUENCES` slots (`_mesh_section` truncates there), out-of-range joints resolved to
    bone 0 and a weightless vertex bound wholly to bone 0 (`ElysiumSkeletalBuild`'s own
    fallbacks), weights renormalised over what remains. A zero authored normal stays zero, so
    `_surface_normals`' geometric fallback still recognises it -- and reconstructs from the
    restated positions.
    """
    if len(ref_pose) != len(bones):
        raise ValueError(
            f"{context}: ref_pose has {len(ref_pose)} entries, expected {len(bones)} "
            f"(one per StudioBone)")
    bind_ms = _model_space_pose(bones, [(b.pos, b.quat) for b in bones], f"{context} (bind)")
    ref_ms = _model_space_pose(bones, ref_pose, f"{context} (ref_pose)")
    moves = []
    for (bind_pos, bind_quat), (ref_pos, ref_quat) in zip(bind_ms, ref_ms):
        quat = _qmul(ref_quat, _qconj(bind_quat))
        offset = tuple(r - m for r, m in zip(ref_pos, _qrotate(quat, bind_pos)))
        moves.append((offset, quat))
    for surface in surfaces.values():
        new_pos, new_nrm = [], []
        for position, normal, joints, weights in zip(surface["pos"], surface["nrm"],
                                                     surface["joints"], surface["weights"]):
            influences = [(int(j) if 0 <= int(j) < len(bones) else 0, float(w))
                          for j, w in zip(joints[:MAX_INFLUENCES], weights[:MAX_INFLUENCES])
                          if float(w) > 0.0] or [(0, 1.0)]
            total = sum(w for _j, w in influences)
            px = py = pz = nx = ny = nz = 0.0
            for joint, weight in influences:
                offset, quat = moves[joint]
                mx, my, mz = _qrotate(quat, position)
                px += weight * (mx + offset[0])
                py += weight * (my + offset[1])
                pz += weight * (mz + offset[2])
                rx, ry, rz = _qrotate(quat, normal)
                nx += weight * rx
                ny += weight * ry
                nz += weight * rz
            new_pos.append((px / total, py / total, pz / total))
            length = (nx * nx + ny * ny + nz * nz) ** 0.5
            new_nrm.append((nx / length, ny / length, nz / length) if length > 1e-12
                           else (0.0, 0.0, 0.0))
        surface["pos"] = new_pos
        surface["nrm"] = new_nrm


def _mesh_section(surfaces, matnames, bone_map):
    """Concatenate the per-material surfaces into one vertex list plus a section table.

    `decode_skinned` dedupes vertices per material, so each surface indexes from zero;
    Unreal wants a single vertex list with polygon groups over it. The running offset that
    joins them is also what the morph section's vertex ids are rebased by, so both are
    computed here and returned together."""
    vertices, triangles, sections, offsets = bytearray(), bytearray(), bytearray(), {}
    vertex_total = triangle_total = 0
    for name in matnames:
        surface = surfaces[name]
        offsets[name] = vertex_total
        sections += _string(name) + struct.pack("<II", triangle_total, len(surface["tris"]))
        normals = _surface_normals(surface)
        for position, normal, uv, joints, weights in zip(surface["pos"], normals, surface["uv"],
                                                         surface["joints"], surface["weights"]):
            vertices += struct.pack("<3f", *_conv_pos(position))
            vertices += struct.pack("<3f", *normal)
            vertices += struct.pack("<2f", float(uv[0]), float(uv[1]))
            # `decode_skinned` already pads to four; the pad keeps a hand-built surface valid.
            influences = list(zip(joints, weights))[:MAX_INFLUENCES]
            influences += [(0, 0.0)] * (MAX_INFLUENCES - len(influences))
            vertices += struct.pack("<4H", *[bone_map[int(j)] if 0 <= int(j) < len(bone_map)
                                             else 0 for j, _w in influences])
            vertices += struct.pack("<4f", *[float(w) for _j, w in influences])
        # The Source->Unreal Y negation is a reflection, so a triangle kept in its authored
        # order would face inward. Reversed once, here, and never again downstream.
        for i, j, k in surface["tris"]:
            triangles += struct.pack("<3I", i + vertex_total, k + vertex_total,
                                     j + vertex_total)
        vertex_total += len(surface["pos"])
        triangle_total += len(surface["tris"])
    header = struct.pack("<III", vertex_total, triangle_total, len(matnames))
    return header + bytes(sections) + bytes(vertices) + bytes(triangles), offsets


def _morph_section(d, mesh_map, matnames, offsets, anorms):
    """Bake `StudioFlex` records into morph targets over the concatenated vertex list.

    One target per distinct `(flexdesc, target ramp)` across the whole model, matching what
    the facial sidecar names: a flexdesc can carry two flexes on one mesh under different
    ramps -- the eyelid pairs hinge one flexdesc into a lower and an upper half -- and the
    ramp remaps that morph's weight at runtime, so the two are separate morphs.

    Returns (payload, names) with `names` in target order, which is the order the facial
    manifest's rows are already index-aligned to."""
    descs = S.flex_descs(d)
    slot_of, slots = {}, []
    deltas = {}                                  # slot -> {vertex: [dp3, dn3]}
    for rec in mesh_map:
        remap = rec["remap"]
        base = offsets.get(rec["material"])
        if base is None:
            continue
        for flex in S.mesh_flexes(d, rec["model_base"], rec["mesh_index"]):
            key = (flex["flexdesc"], tuple(flex["targets"]))
            if key not in slot_of:
                slot_of[key] = len(slots)
                slots.append(key)
            bucket = deltas.setdefault(slot_of[key], {})
            for local, dv, nv in S.vert_anims(d, flex, anorms):
                # A vertex no triangle references never entered the surface
                # (`decode_skinned` dedupes on use), so it has no morph slot either.
                surface_index = remap.get(rec["vertex_offset"] + local)
                if surface_index is None:
                    continue
                entry = bucket.setdefault(base + surface_index, [0.0] * 6)
                if dv:
                    for axis, value in enumerate(_conv_pos(dv)):
                        entry[axis] += value
                if nv:
                    for axis, value in enumerate(_conv_dir(nv)):
                        entry[3 + axis] += value
    if not slots:
        return b"", []

    names = _morph_names(descs, slots)
    out = bytearray(struct.pack("<I", len(slots)))
    for slot, name in enumerate(names):
        bucket = deltas.get(slot, {})
        out += _string(name)
        out += struct.pack("<I", len(bucket))
        for vertex in sorted(bucket):
            out += struct.pack("<I", vertex)
            out += struct.pack("<6f", *bucket[vertex])
    return bytes(out), names


def _morph_names(descs, slots):
    """Unique per-model morph-target names: the flexdesc's FACS name, suffixed `#k` when a
    flexdesc contributes more than one ramp. Uniqueness is load-bearing -- a morph target is
    keyed by name on the skeletal mesh, so two same-named ramps would fuse into one."""
    from collections import Counter
    seen, out = Counter(), []
    for flexdesc, _ramp in slots:
        name = descs[flexdesc] if 0 <= flexdesc < len(descs) else f"flex{flexdesc}"
        out.append(name if not seen[name] else f"{name}#{seen[name]}")
        seen[name] += 1
    return out


def _bone_mask(d, bones, clip, bone_map, emitted):
    """This clip's per-bone `weight`@0 gate over the emitted bone list, or None when the clip
    owns every bone.

    `weight`@0 is a binary authored mask, not a factor: it takes only 0.0 and 1.0 across the
    whole install, and both retail channel decoders test it against zero before anything else
    and, on zero, write a zero position and a zero quaternion and read no track at all. So the
    zero set names the bones the animation does NOT own -- the bones a partial-body overlay
    leaves to whatever pose it is composed over (`docs/vtmb/animation_and_movers.md` A.4).

    It cannot be recovered from the tracks. A bone the clip owns and does not animate holds its
    bind pose -- a real authored pose, 1,346 records across the shipped `*_layer` clips -- and a
    bone outside the mask animates nothing either, so both arrive as "no track" once the
    channel-less tracks are dropped. Collapsing them either drops the first or stomps the base
    pose on the second.
    """
    records = clip.base + struct.unpack_from("<i", d, clip.base + 48)[0]
    mask = bytearray(emitted)
    owned = 0
    present = 0
    for bone in bones:
        if bone_map[bone.index] < 0:
            # Outside this container's emitted set -- one actor's slice of a cinematic model's
            # co-located skeletons. It has no row to gate.
            continue
        present += 1
        if struct.unpack_from("<f", d, records + bone.index * 32)[0] != 0.0:
            mask[bone_map[bone.index]] = 1
            owned += 1
    return None if owned == present else bytes(mask)


def _owned_bones(d, bones, clip):
    """The bone indices this clip's `weight`@0 mask owns."""
    records = clip.base + struct.unpack_from("<i", d, clip.base + 48)[0]
    return {b.index for b in bones
            if struct.unpack_from("<f", d, records + b.index * 32)[0] != 0.0}


def _owns_split_bone(d, bones, clip):
    """Whether this clip's mask owns a `SPLIT_ROTATION` bone.

    Exactly one of the four masks a bank ships does -- the 49-bone upper-body gate the
    `*_aim_layer` and `*_bobble_layer` families carry. It is the only mask whose clips cannot
    normalize against their own frames, because it excludes the whole chain above that bone.
    """
    split = {b.index for b in bones if b.flags & SPLIT_ROTATION and b.parent >= 0}
    return bool(split) and bool(split & _owned_bones(d, bones, clip))


def _composed_frames(d, bones, delta, host, owned=None):
    """`host`'s frame 0 with `delta` accumulated onto it, per frame of the delta.

    Retail's additive combine is `out.quat = out.quat * scale(delta, s)` and
    `out.pos += delta.pos * s` (`docs/vtmb/animation_and_movers.md`) -- the delta lands on the
    RIGHT, in the bone's own frame. Every `EAdditiveAnimationType` puts it on the left instead,
    so the conversion is a conjugation by the base's rotation, `Q_R * Q_d * Q_R^-1`.

    That conjugation is never computed here, because Unreal's compressor performs it. Marking a
    sequence additive against a base makes `BakeOutAdditiveIntoRawData` subtract that base from
    the raw keys, and `(Q_R * Q_d) * Q_R^-1` is exactly the conjugated delta. So what the file
    has to carry is the COMPOSED pose -- retail's own result at weight 1 -- and the name of the
    base it was composed onto.

    `owned` switches the combine from an additive's to an OVERLAY's. Retail composes a masked
    `*_layer` with `nlerp(out, layer, s)`, which at full weight replaces the bones the mask owns
    rather than accumulating onto them -- so the composed pose is the host's with those bones
    taken from the layer.

    Every bone is emitted into the frame, not only the ones the clip animates, so the split
    bone's ancestor chain is present for `_split_rotation_tracks` to compose. THAT is the whole
    reason an overlay derives per host: the chain is what the clip's own mask excludes, the bind
    pose is no substitute for it (`_split_rotation_tracks`), and the host is the pose the
    autolayer table names. The caller drops the bones the clip does not own.
    """
    # Frame 0 only: an additive's base is one pose, so decoding the host's whole cycle to read
    # its first frame is work per binding rather than per host.
    host_frames = S.read_anim(d, bones, host.base, 1)
    delta_frames = S.read_anim(d, bones, delta.base, delta.frames)
    # A bone the host carries no record for reads back as zeros rather than as its bind, and
    # retail accumulates onto a pose where such a bone holds its bind. Resolve that once.
    base = []
    for bone in bones:
        pos, quat = host_frames[0][bone.index]
        rot = _qnorm(quat)
        base.append((pos if rot is not None else bone.pos, rot or bone.quat))

    out = []
    for frame in range(delta.frames):
        row = []
        for bone in bones:
            base_pos, base_rot = base[bone.index]
            own_pos, own_rot = delta_frames[frame][bone.index]
            rot = _qnorm(own_rot)
            if owned is not None:
                # Overlay: replace where the mask owns the bone, keep the host everywhere else.
                # A bone the mask owns but the clip does not animate holds its BIND -- a real
                # authored pose, which is the same thing the mask table exists to preserve.
                if bone.index not in owned:
                    row.append((base_pos, base_rot))
                else:
                    row.append((own_pos if rot is not None else bone.pos,
                                rot or bone.quat))
                continue
            if rot is None:
                row.append((base_pos, base_rot))
                continue
            row.append((tuple(b + v for b, v in zip(base_pos, own_pos)),
                        _qmul(base_rot, rot)))
        out.append(row)
    return out


def _authored_channels(d, bones, clip):
    """[(has translation, has rotation)] per bone, from the clip's own animation record.

    The seven offsets at `+4` are the per-channel RLE pointers, the first three positional and the
    last four rotational; a zero there means the bone holds its bind value for the whole clip.
    """
    records = clip.base + struct.unpack_from("<i", d, clip.base + 48)[0]
    out = []
    for bone in bones:
        offsets = struct.unpack_from("<7i", d, records + bone.index * 32 + 4)
        out.append((any(offsets[:3]), any(offsets[3:])))
    return out


def _owned_channels(d, bones, clip):
    """Both complete local channels for every bone the clip's weight mask owns."""
    owned = _owned_bones(d, bones, clip)
    return [(bone.index in owned, bone.index in owned) for bone in bones]


def _clip_payload(d, bones, clip, bone_map, emitted, masks, frames=None, base_label="",
                  label=None, owned=None, base_channels=None, forced_channels=None):
    """One complete owned pose, or None if the clip's mask owns no emitted bone.

    The seven offsets at `+4` only say which components have samples. On an owned bone retail
    fills every absent component from the DONOR bind, while a zero-weight bone contributes no
    pose at all. ESKM therefore writes complete translation and rotation tracks for the former and
    no track for the latter. Leaving an owned channel implicit would make Unreal substitute the
    consuming family's reference pose and silently change the donor pose.

    `frames` overrides the decode, which is how a derived additive ships the pose it was
    composed into rather than the bytes at `clip.base`; `label` and `base_label` name the
    derived clip and the clip it is a difference from. The channel gate still reads the
    original record, so a derived clip carries exactly the bones its own delta owned."""
    if frames is None:
        frames = S.read_anim(d, bones, clip.base, clip.frames)

    # Channel presence is still needed by the derived additive bookkeeping below, but it does not
    # decide whether the clip exists. Ownership does: a one-frame clip with no RLE offsets is a real
    # held donor-bind pose when its weight record owns bones.
    channels = _authored_channels(d, bones, clip)
    clip_owned = _owned_bones(d, bones, clip)
    has_owned = any(bone.index in clip_owned and bone_map[bone.index] >= 0 for bone in bones)
    has_forced = bool(forced_channels) and any(
        bone_map[bone.index] >= 0 and any(forced_channels[bone.index]) for bone in bones)
    if not has_owned and not has_forced:
        return None

    # A raw delta is not a pose, so there is no parent chain to divide out of it; the derived
    # clip that ships in its place IS a pose (its host's, with the delta on it) and normalizes
    # like any other. `base_label` distinguishes that derived form from the raw record.
    split_rotations = ({} if (clip.flags & DELTA_SEQUENCE and not base_label)
                       else _split_rotation_tracks(bones, frames, clip.frames))

    tracks = bytearray()
    count = 0
    for bone in bones:
        if bone_map[bone.index] < 0:
            # Not emitted by this container. A cinematic model packs several actors' skeletons
            # into one file and each is written as its own bank, so the clip is decoded against
            # the whole bone list and written against one actor's slice of it.
            continue
        has_translation, has_rotation = channels[bone.index]
        rotations = split_rotations.get(bone.index)
        has_rotation = has_rotation or rotations is not None
        if base_label and owned is None:
            # A derived ADDITIVE ships the bones ITS OWN delta authored, plus the bones its HOST
            # tracks -- and nothing else.
            #
            # Unreal bakes an additive down over every bone of the family skeleton, subtracting the
            # base pose from the additive pose. A bone neither side owns falls through to that same
            # family reference on both sides, so it subtracts to exact identity and needs no track.
            #
            # A bone the HOST tracks must still ship even where the delta authored nothing, or the
            # additive resolves to the reference pose against a base that does not -- the same
            # error with the sign flipped. Its composed value is the host's own, so it subtracts to
            # identity too.
            #
            # The remaining case -- the DELTA authors a bone its host does not -- is closed from
            # the other side, by `forced_channels` below: the host ships a bind track wherever any
            # of its additives ships one, so both sides of the subtraction name this container's
            # bind rather than one of them falling through to the family's reference pose.
            host_translation, host_rotation = (
                base_channels[bone.index] if base_channels else (False, False))
            has_translation = has_translation or host_translation
            has_rotation = has_rotation or host_rotation
        elif owned is not None:
            # A derived OVERLAY ships only the bones its mask owns; the rest come from whatever
            # the layered blend is composed over at runtime, which is what the mask is for. An
            # owned bone always ships, including one the clip leaves at its bind pose.
            if bone.index not in owned:
                continue
            has_translation = has_rotation = True
        elif forced_channels is not None:
            # A HOST ships a complete bind track for every bone any additive declared against it
            # owns.
            #
            # Unreal bakes an additive as (additive pose - base pose) over the whole skeleton, and
            # resolves an untracked bone on either side to the family skeleton's reference pose.
            # So where the delta tracks a bone and the host does not, the additive side names this
            # container's bind and the base side names the family's, and the difference between
            # them survives into the composed pose -- measured at 19.74 degrees on
            # `Bip01 R Clavicle` and 17.5 deg / 8.57 cm on `Bip01 Neck` across the 63-bank family.
            #
            # The track costs one bone's frames and is exactly the value the base already resolved
            # to in retail, so this states what was always meant rather than changing a pose.
            forced_translation, forced_rotation = forced_channels[bone.index]
            has_translation = has_translation or forced_translation
            has_rotation = has_rotation or forced_rotation
        carried_by_base = bool(base_label and owned is None and base_channels
                               and any(base_channels[bone.index]))
        carried_by_force = bool(forced_channels and any(forced_channels[bone.index]))
        if bone.index not in clip_owned and not carried_by_base and not carried_by_force:
            # In particular, a split bone outside a partial-body mask must not acquire the
            # normalized rotation `_split_rotation_tracks` can compute from its zero pose slot.
            continue
        if bone.index in clip_owned:
            # Complete, self-describing donor local. `read_anim` has already filled each absent
            # component from this container's MDL bind and normalized the quaternion.
            has_translation = has_rotation = True
        if not (has_translation or has_rotation):
            continue
        tracks += struct.pack("<I2B", bone_map[bone.index], int(has_translation),
                              int(has_rotation))
        # A host may mask this bone out even though an additive declared against the host owns it.
        # Retail's host pose still has the donor bind there; read_anim correctly returns zeros for
        # the masked-out record, so a forced track must state that bind explicitly rather than
        # serialising those mask sentinels as a real zero transform.
        force_bind = carried_by_force and bone.index not in clip_owned
        if has_translation:
            for frame in range(clip.frames):
                position = bone.pos if force_bind else frames[frame][bone.index][0]
                tracks += struct.pack("<3f", *_conv_pos(position))
        if has_rotation:
            for frame in range(clip.frames):
                quat = (bone.quat if force_bind else
                        rotations[frame] if rotations is not None else
                        frames[frame][bone.index][1])
                tracks += struct.pack("<4f", *_conv_quat(quat))
        count += 1
    if not count:
        return None
    # De-duplicated across the file: a bank states a handful of distinct masks over hundreds of
    # clips (five over the male `move_and_ranged`'s 722 animations), so this is a table plus a
    # reference rather than a per-bone array per clip.
    mask = _bone_mask(d, bones, clip, bone_map, emitted)
    index = -1 if mask is None else masks.setdefault(mask, len(masks))
    header = _string((label or clip.label).lstrip("@"))
    header += _string(base_label)
    header += struct.pack("<IfIiI", clip.frames, clip.fps or 30.0, clip.flags, index, count)
    return header + bytes(tracks)


def _cell_names(clips):
    """{animdesc base: the name a grid cell standing that animation goes by}.

    A cell names an ANIMATION; a clip is named for the SEQUENCE that stands it. The two strings
    part company at a grid's base cell, the one animation a grid sequence declares under its own
    label -- a 3x3 aim grid's base cell is the animation `x_aim_UR` standing under the sequence
    label `x_aim_layer`. The blend sidecar resolves every cell through this same table
    (`mdl_skel.blend_clip_plan`), so a composed cell named any other way is a sample the blend
    space asks for under a name nothing was ever written under, and the grid loses that corner.
    """
    names = {}
    for clip in clips:
        names.setdefault(clip.base, clip.label)
    return names


def _derived_bindings(d, bones, clips, names):
    """Every (layer, declaring host) pair that has to bake against that host, host-sorted.

    `numautolayers`@660 names the sequences a host is composed with. Two kinds need the host's
    own pose to be written correctly, and the table is what names it:

    - a `DELTA_SEQUENCE`, because VtMB post-multiplies its delta and Unreal pre-multiplies, and
      the conversion is a conjugation by the base's rotation (see `_composed_frames`);
    - a masked overlay whose mask owns the split bone, because the chain that bone's rotation
      has to be expressed against is exactly what its mask excludes (`_split_rotation_tracks`).

    A masked overlay that owns no split bone needs nothing: it is already an ordinary
    parent-relative pose and ships once. So does a layer no host declares -- there is no base
    to write it against, and retail only ever reaches one through this table.

    Returns [(layer, host, owned or None)], `owned` set for the overlay kind.
    """
    by_label = {c.label.lower(): c for c in clips}
    pairs = []
    for host in clips:
        for target in host.autolayers:
            layer = by_label.get(target.lower())
            if layer is None:
                continue
            if layer.flags & DELTA_SEQUENCE:
                pairs.append((layer, host, None))
                continue
            # A grid stands a different animation per cell, and each is its own record with its
            # own mask -- so a 3x3 aim grid derives nine times against the same host, not once
            # against its base cell. The blend space is built over the cells, so a cell left raw
            # would carry the un-resolved rotation into eight of the nine samples.
            for cell in _grid_animations(d, layer, names):
                if _owns_split_bone(d, bones, cell):
                    pairs.append((cell, host, _owned_bones(d, bones, cell)))
    return sorted(pairs, key=lambda p: (p[0].label.lower(), p[1].label.lower()))


def _grid_animations(d, seq, names):
    """The animations a sequence stands, as `Seq` rows -- its grid's cells, or itself.

    Deduped by animation index: a fan duplicates its clip at both ends of a wrapping axis
    (`move_yaw`), so the same animation appears under two cells. `names` is `_cell_names`, which
    is what makes a cell's label the one the blend sidecar samples it by."""
    seen, out = set(), []
    for cell in seq.grid.cells:
        got = S.local_animation(d, cell.anim)
        if got is None or cell.anim in seen:
            continue
        seen.add(cell.anim)
        name, base, frames, fps = got
        out.append(S.Seq(names.get(base, name), base, frames, fps,
                         seq.activity, seq.actweight, seq.flags))
    return out or [seq]


def _anim_section(d, bones, clips, bone_map, emitted, masks, ensure_labels=()):
    """The clips that actually baked, in declaration order. A sequence whose tracks came out
    empty is absent rather than present-and-silent, which is the same rule the manifest's
    clip list already follows.

    A clip the autolayer table binds to a host is emitted once per declaring host, named
    `<clip>@<host>` and carrying that host's label as its base (`_derived_bindings`).

    A raw ADDITIVE still ships beside its derived forms: it is the label the model references,
    and it is the clip retail would have accumulated. A raw OVERLAY that owns the split bone
    does not, and must not -- its rotation for that bone is model-space and there is no chain in
    the file to resolve it against, so ordinary FK reads it as the upper body folded about the
    waist. The derived forms are the only correct way to stand one.

    `masks` accumulates the file's distinct bone masks in index order; it is written out as the
    "MASK" section once every clip has been read."""
    names = _cell_names(clips)
    bindings = _derived_bindings(d, bones, clips, names)
    # An overlay that ships only in derived form is one NOTHING else still reaches under its plain
    # label. A cell can belong to two grids at once -- one an autolayer target, the other declared
    # by no host -- and suppressing the raw form then leaves the second grid with holes where its
    # shared cells were. The unbound grid is orphan content either way, but it has to be
    # self-consistent rather than half-built.
    bound = {t.lower() for host in clips for t in host.autolayers}
    still_reached = set()
    for seq in clips:
        if seq.label.lower() in bound:
            continue
        for cell in _grid_animations(d, seq, names):
            still_reached.add(cell.label.lower())
    unresolvable = {layer.label.lower() for layer, _host, owned in bindings
                    if owned is not None} - still_reached

    # Per host, the union of the BONES its ADDITIVES own -- complete donor locals the host has to
    # carry so both sides of Unreal's subtraction name this container's bind pose. An owned bone
    # remains semantic even when all seven RLE offsets are zero. An overlay is excluded: it is
    # masked and composed toward its own pose rather than differenced.
    forced = {}
    for layer, host, owned in bindings:
        if owned is not None:
            continue
        union = forced.setdefault(host.label.lower(), [(False, False)] * len(bones))
        for index, (translation, rotation) in enumerate(_owned_channels(d, bones, layer)):
            union[index] = (union[index][0] or translation, union[index][1] or rotation)

    ensured = {str(label).lower() for label in ensure_labels}
    payloads = [p for p in (_clip_payload(
                                d, bones, c, bone_map, emitted, masks,
                                forced_channels=([(True, True)] * len(bones)
                                                 if c.label.lower() in ensured
                                                 else forced.get(c.label.lower())))
                            for c in clips if c.label.lower() not in unresolvable)
                if p is not None]
    for layer, host, owned in bindings:
        payload = _clip_payload(
            d, bones, layer, bone_map, emitted, masks,
            frames=_composed_frames(d, bones, layer, host, owned),
            base_label=host.label.lstrip("@"), owned=owned,
            base_channels=None if owned is not None else _owned_channels(d, bones, host),
            label=f"{layer.label.lstrip('@')}{BASE_SEPARATOR}{host.label.lstrip('@')}")
        if payload is not None:
            payloads.append(payload)
    return struct.pack("<I", len(payloads)) + b"".join(payloads), len(payloads)


def _mask_section(masks, emitted):
    """The distinct bone masks, in the order the clips referenced them."""
    if not masks:
        return b""
    out = bytearray(struct.pack("<I", len(masks)))
    for mask in masks:
        out += struct.pack("<I", emitted) + mask
    return bytes(out)


def _assemble(sections):
    """Header + directory + payloads. Sections with an empty payload are omitted, which is
    how a bank ends up with no "MESH" without the reader needing a flag for it."""
    present = [(tag, payload) for tag, payload in sections if payload]
    header = struct.pack("<4sIII", MAGIC, VERSION, len(present), 0)
    directory_size = len(present) * struct.calcsize("<4sQQ")
    offset = len(header) + directory_size
    directory = bytearray()
    for tag, payload in present:
        directory += struct.pack("<4sQQ", tag, offset, len(payload))
        offset += len(payload)
    return header + bytes(directory) + b"".join(payload for _tag, payload in present)


def _dynamics_section(model_path, blob, bones):
    """The deliberately narrow, baked-native AnimDynamics POC recipe for this body."""
    chains = SM.anim_dynamics_poc_chains(model_path, blob, bones)
    if not chains:
        return b"", 0
    out = bytearray(struct.pack("<I", len(chains)))
    for chain in chains:
        out += _string(chain.first_bone)
        out += _string(chain.chain_end)
        out += struct.pack(
            "<4f", chain.gravity_scale, chain.damping,
            chain.angular_spring, chain.cone_angle_degrees)
    return bytes(out), len(chains)


def _breast_section(_model_path, _blob, _bones):
    """Single-body breast recipes. Disabled: the host is not shipping them."""
    return b"", 0


def _write_container(path, blob):
    """Write one `.eskm`, skipping a byte-identical rewrite.

    A container is content-addressed downstream: the bake plan asks whether a body's bytes moved,
    through a stat-keyed digest cache. Rewriting an unchanged container would answer yes on mtime
    alone and re-bake a body, its clips and the digest of every container the family declares.
    """
    try:
        with open(path, "rb") as handle:
            if handle.read() == blob:
                return
    except OSError:
        pass
    with open(path, "wb") as handle:
        handle.write(blob)


def write_model(idx, model_path, out_dir, stem=None, anorms=None, clip_labels=None,
                ensure_labels=None, ref_pose=None, extra_attachments=None):
    """Write `<out_dir>/<stem>.eskm` and return a summary dict.

    `anorms` is the unit-vector table read out of the user's own `StudioRender.dll`; without
    it a compressed vertex-animation record has directions but no magnitudes, so the morph
    section is omitted rather than baked wrong.

    `ref_pose`, when given, overrides the "SKEL" section's reference pose one bone at a time --
    a sequence of `(pos, quat)` indexed by original StudioBone index, in the same source-space
    conventions as `Bone.pos`/`Bone.quat` (`wield_corpus.bake_pose`'s shape). It exists for the
    wielded-weapon models whose faithful reference pose is the model's own clip at frame 0 rather
    than its container bind. The "MESH" geometry is restated into that pose in the same write
    (`_reskin_surfaces`) -- vertices are meaningful only against the reference pose they are
    stored with, so an override that left them in bind space would bake a container that
    disagrees with itself. Left `None`, the container's own bind pose is written exactly as
    before -- byte-identical output for every caller that does not pass it.

    `extra_attachments`, when given, is a list of `mdl_skel.Attachment` records appended to the
    model's own authored attachments in the "ATCH" section -- see `_attachment_section`. Left
    `None`, byte-identical output for every caller that does not pass it."""
    loaded = mdl.load(idx, model_path)
    if not loaded:
        raise SystemExit(f"model not found: {model_path}")
    d, v = loaded
    stem = stem or mdl.sanitize(os.path.basename(model_path)[:-4])

    from elysium_pipeline.formats import install
    bones = S.read_bones(d)
    mesh_map = []
    surfaces = S.decode_skinned(d, v, mesh_map)
    matnames = list(surfaces.keys())

    os.makedirs(os.path.join(out_dir, "tex"), exist_ok=True)
    search = mdl.search_paths(d)
    tex_cache = {}
    matinfo = {name: mdl._resolve_material(name, search, lambda k: install.read(idx, k),
                                           out_dir, tex_cache) for name in matnames}

    if ref_pose is not None:
        # BEFORE the mesh section reads the surfaces: the SKEL override below changes the frame
        # the vertices are interpreted in, so the vertices move into it in the same write.
        _reskin_surfaces(surfaces, bones, ref_pose, model_path)
        if anorms and S.flex_descs(d):
            # Morph deltas are bind-space vertex offsets and nothing restates them; baking them
            # against an overridden reference pose would be silently wrong on every frame. No
            # caller combines the two today -- refuse loudly rather than let one start to.
            raise ValueError(f"{model_path}: ref_pose override cannot carry a morph section")

    rows, bone_map, reparented = unreal_bones(bones)
    dynamics_payload, dynamics_count = _dynamics_section(model_path, d, bones)
    breast_payload, breast_count = _breast_section(model_path, d, bones)
    mesh_payload, offsets = _mesh_section(surfaces, matnames, bone_map)
    morph_payload, morph_names = (_morph_section(d, mesh_map, matnames, offsets, anorms)
                                  if anorms and S.flex_descs(d) else (b"", []))

    own = S.local_sequences(d)
    if clip_labels is not None:
        wanted = {str(label).lower() for label in clip_labels}
        own = [clip for clip in own if clip.label.lower() in wanted]
    extra, _blends = S.blend_clip_plan(d, own)
    masks = {}
    anim_payload, clip_count = _anim_section(
        d, bones, own + extra, bone_map, len(rows), masks, ensure_labels or ())

    blob = _assemble([
        (b"SKEL", _skel_section(_ref_pose_rows(rows, bone_map, ref_pose, model_path, reparented))),
        (b"ATCH", _attachment_section(d, bone_map, extra_attachments)),
        (b"DYNM", dynamics_payload),
        (b"BDYN", breast_payload),
        (b"MATL", _matl_section(matnames, matinfo)),
        (b"MESH", mesh_payload),
        (b"MORF", morph_payload),
        (b"MASK", _mask_section(masks, len(rows))),
        (b"ANIM", anim_payload),
    ])
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, stem + ".eskm")
    _write_container(path, blob)
    triangles = sum(len(s["tris"]) for s in surfaces.values())
    vertices = sum(len(s["pos"]) for s in surfaces.values())
    print(f"  eskm {stem}: {len(bones)} bones, {vertices} verts, {triangles} tris, "
          f"{len(morph_names)} morphs, {clip_count} clips, {dynamics_count} hair POC chain(s), "
          f"{breast_count} breast body(ies) "
          f"-> {path} ({os.path.getsize(path) // 1024} KB)")
    return dict(stem=stem, eskm=os.path.basename(path), model=model_path,
                bones=len(bones), vertices=vertices, triangles=triangles,
                morphs=morph_names, materials=matnames, clips=clip_count,
                hair_dynamics=dynamics_count, breast_dynamics=breast_count)


def write_bank(idx, model_path, out_dir, stem):
    """Write `<out_dir>/banks/<stem>.eskm` -- skeleton + clips, no geometry. Returns None for
    an aggregator model that defines no animated clip."""
    from elysium_pipeline.formats import install

    key = model_path[:-4] if model_path.lower().endswith(".mdl") else model_path
    d = install.read(idx, key + ".mdl")
    if not d:
        return None
    clips = S.local_sequences(d)
    if not clips:
        return None
    bones = S.read_bones(d)
    extra, _blends = S.blend_clip_plan(d, clips)
    rows, bone_map, _reparented = unreal_bones(bones)
    masks = {}
    anim_payload, count = _anim_section(d, bones, clips + extra, bone_map, len(rows), masks)
    if not count:
        return None

    banks_dir = os.path.join(out_dir, "banks")
    os.makedirs(banks_dir, exist_ok=True)
    path = os.path.join(banks_dir, stem + ".eskm")
    _write_container(path, _assemble([(b"SKEL", _skel_section(rows)),
                                      (b"MASK", _mask_section(masks, len(rows))),
                                      (b"ANIM", anim_payload)]))
    print(f"  eskm bank {stem}: {len(bones)} bones, {count} clips, {len(masks)} bone mask(s) "
          f"-> {path} ({os.path.getsize(path) // 1024} KB)")
    return dict(stem=stem, eskm="banks/" + os.path.basename(path), model=model_path,
                bones=len(bones), clips=count)


def _cinematic_rows(sub, root):
    """One actor's bones as (rows, {StudioBone index: emitted index}), prefix folded to `Bip01`.

    Folded for the reason the glb half folds: a scene's `bonerename "BipNN" "Bip01"` is how an
    actor picks its own skeleton out of the shared performance, so writing the bank under the
    ordinary names is what lets the existing bone-name binding apply it to an ordinary body with
    no runtime rule of its own.

    Parents are remapped into the subset. A subset normally has exactly one bone whose parent
    lies outside it -- that actor's own root -- and goes through the same `_single_root`
    resolution as any other multi-rooted rig when it does not.
    """
    low = root.lower()
    order = {b.index: slot for slot, b in enumerate(sub)}
    rows = [((("Bip01" + b.name[len(root):]) if b.name[:len(root)].lower() == low else b.name),
             order.get(b.parent, -1), b.pos, b.quat)
            for b in sub]
    rows, position_of, _reparented = _single_root(rows)
    return rows, {index: position_of[slot] for index, slot in order.items()}


def write_cinematic(idx, model_path, out_dir, stem):
    """Write one bank per bone root of a cinematic `.mdl` -> `<out_dir>/banks/<stem>__<root>.eskm`.

    The `.eskm` twin of `mdl_gltf.export_cinematic`, and it exists for the same reason the rest of
    this module does: the mount is the only build of a character, so every clip a scene can name
    must be present as a native container and then as one shared-bank asset
    (`Elysium.Content.BakedClipCoverage`).

    A cinematic model is a whole multi-actor performance in one file: N co-located skeletons
    (`Bip01`..`BipNN`) sharing one clip, usually `entire_scene`. Every clip is decoded once
    against the FULL bone list, because the animation records are indexed by the model's own bone
    order, and each actor's bank is written through a bone map that emits only that actor's slice.

    Returns a list of bank dicts (as `write_bank`), each with an extra `root` key, or None.
    """
    from elysium_pipeline.formats import install

    key = model_path[:-4] if model_path.lower().endswith(".mdl") else model_path
    d = install.read(idx, key + ".mdl")
    if not d:
        return None
    clips = S.local_sequences(d)
    if not clips:
        return None
    bones = S.read_bones(d)
    roots = S.cinematic_roots(bones)
    if len(roots) <= 1:
        # A single-root performance is an ordinary bank, written unsuffixed like any other.
        one = write_bank(idx, model_path, out_dir, stem)
        if one:
            one["root"] = roots[0] if roots else None
        return [one] if one else None

    extra, _blends = S.blend_clip_plan(d, clips)
    all_clips = clips + extra
    banks_dir = os.path.join(out_dir, "banks")
    os.makedirs(banks_dir, exist_ok=True)

    out = []
    for root in roots:
        low = root.lower()
        sub = [b for b in bones if (S._bone_root(b.name) or "").lower() == low]
        if not sub:
            continue
        rows, order = _cinematic_rows(sub, root)
        bone_map = [-1] * len(bones)
        for index, slot in order.items():
            bone_map[index] = slot
        masks = {}
        anim_payload, count = _anim_section(d, bones, all_clips, bone_map, len(rows), masks)
        if not count:
            continue
        name = f"{stem}__{low}"
        path = os.path.join(banks_dir, name + ".eskm")
        _write_container(path, _assemble([(b"SKEL", _skel_section(rows)),
                                          (b"MASK", _mask_section(masks, len(rows))),
                                          (b"ANIM", anim_payload)]))
        print(f"  eskm cinematic {name}: {len(sub)} bones, {count} clips, {len(masks)} bone "
              f"mask(s) -> {path} ({os.path.getsize(path) // 1024} KB)")
        out.append(dict(stem=name, eskm="banks/" + os.path.basename(path), model=model_path,
                        bones=len(rows), clips=count, root=root))
    return out or None
