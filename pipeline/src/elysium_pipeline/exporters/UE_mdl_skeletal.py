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
    "MATL"  u32 materialCount
            materialCount x { string name, string albedo }             albedo relative to the
                                                                       export root, "" if none
    "MESH"  u32 vertexCount, u32 triangleCount, u32 sectionCount
            sectionCount x { string material, u32 firstTriangle, u32 triangleCount }
            vertexCount   x { f32 p[3], f32 uv[2], u16 bone[3], f32 weight[3] }
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

from elysium_pipeline.formats import bsp, mdl, mdl_skel as S

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
VERSION = 4

#: Separates an additive's own label from the label of the host it was composed onto, in the name
#: of a derived clip. A VtMB sequence label never contains it, so the split is unambiguous.
BASE_SEPARATOR = "@"

MAGIC = b"ESKM"

#: VtMB's SKINNED StudioVertex carries exactly three `BoneWeight` slots, so an influence
#: block is fixed-width and a reader needs no per-vertex length.
MAX_INFLUENCES = 3

#: Unreal's reference skeleton is single-rooted and asserts on a second parent-less bone, but a
#: few VtMB skeletons fork (regular_cop bones 0/1, prophet bones 0/59). Those get one synthetic
#: root above the real ones, under the name the runtime already knows.
SYNTHETIC_ROOT = "__elysium_skeleton_root"

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


def unreal_bones(bones):
    """The model's bones in Unreal reference-skeleton order -> (rows, index map).

    A row is `(name, parent, source position, source quaternion)`. The map takes an original
    StudioBone index to its emitted index, and every other section indexes bones through it --
    a vertex's influences and a clip's tracks both address the emitted list, so nothing
    downstream has to know whether a synthetic root was added."""
    roots = [b.index for b in bones if b.parent == -1]
    if len(roots) <= 1:
        return ([(b.name, b.parent, b.pos, b.quat) for b in bones], list(range(len(bones))))
    rows = [(SYNTHETIC_ROOT, -1, (0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0))]
    rows += [(b.name, 0 if b.parent == -1 else b.parent + 1, b.pos, b.quat) for b in bones]
    return rows, [i + 1 for i in range(len(bones))]


def _skel_section(rows):
    out = bytearray(struct.pack("<I", len(rows)))
    for name, parent, pos, quat in rows:
        out += _string(name)
        out += struct.pack("<i", parent)
        out += struct.pack("<3f", *_conv_pos(pos))
        out += struct.pack("<4f", *_conv_quat(quat))
    return bytes(out)


def _matl_section(matnames, matinfo):
    out = bytearray(struct.pack("<I", len(matnames)))
    for name in matnames:
        albedo = (matinfo.get(name) or {}).get("albedo") or ""
        out += _string(name)
        out += _string(f"tex/{albedo}" if albedo else "")
    return bytes(out)


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
        for position, uv, joints, weights in zip(surface["pos"], surface["uv"],
                                                 surface["joints"], surface["weights"]):
            vertices += struct.pack("<3f", *_conv_pos(position))
            vertices += struct.pack("<2f", float(uv[0]), float(uv[1]))
            vertices += struct.pack("<3H", *[bone_map[int(j)] if 0 <= int(j) < len(bone_map)
                                             else 0 for j in joints[:MAX_INFLUENCES]])
            vertices += struct.pack("<3f", *[float(w) for w in weights[:MAX_INFLUENCES]])
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

    A synthetic root is outside every mask: it is not a bone any animation was authored
    against, and nothing animates it, so both readings pose it identically.
    """
    records = clip.base + struct.unpack_from("<i", d, clip.base + 48)[0]
    mask = bytearray(emitted)
    owned = 0
    for bone in bones:
        if struct.unpack_from("<f", d, records + bone.index * 32)[0] != 0.0:
            mask[bone_map[bone.index]] = 1
            owned += 1
    return None if owned == len(bones) else bytes(mask)


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


def _clip_payload(d, bones, clip, bone_map, emitted, masks, frames=None, base_label="",
                  label=None, owned=None):
    """One clip's tracks, or None if it animates no channel at all.

    A bone gets a track only for the channels its animation record actually carries: the
    seven offsets at `+4` are the per-channel RLE pointers, the first three positional and
    the last four rotational, and a zero there means the bone holds its bind value. Writing
    a constant track for those would triple most clips for nothing.

    The one exception is a split bone, whose rewritten rotation is emitted whether or not the
    clip authored that channel -- see `_split_rotation_tracks`.

    `frames` overrides the decode, which is how a derived additive ships the pose it was
    composed into rather than the bytes at `clip.base`; `label` and `base_label` name the
    derived clip and the clip it is a difference from. The channel gate still reads the
    original record, so a derived clip carries exactly the bones its own delta owned."""
    if frames is None:
        frames = S.read_anim(d, bones, clip.base, clip.frames)
    records = clip.base + struct.unpack_from("<i", d, clip.base + 48)[0]

    # Read the authored channels first, so "this clip animates nothing" stays a property of
    # what VtMB wrote rather than of the correction below, and such a clip is still absent
    # rather than present-and-silent.
    channels = []
    for bone in bones:
        offsets = struct.unpack_from("<7i", d, records + bone.index * 32 + 4)
        channels.append((any(offsets[:3]), any(offsets[3:])))
    if not any(translation or rotation for translation, rotation in channels):
        return None

    # A raw delta is not a pose, so there is no parent chain to divide out of it; the derived
    # clip that ships in its place IS a pose (its host's, with the delta on it) and normalizes
    # like any other. `frames is None` is exactly the "not derived" test.
    split_rotations = ({} if (clip.flags & DELTA_SEQUENCE and not base_label)
                       else _split_rotation_tracks(bones, frames, clip.frames))

    tracks = bytearray()
    count = 0
    for bone in bones:
        has_translation, has_rotation = channels[bone.index]
        rotations = split_rotations.get(bone.index)
        has_rotation = has_rotation or rotations is not None
        if base_label and owned is None:
            # A derived ADDITIVE is a whole POSE, so every bone ships even where the delta owned
            # nothing. Unreal subtracts the base from every bone when it bakes an additive down,
            # and a bone with no track evaluates to the skeleton's reference pose rather than to
            # the base -- which would subtract into a spurious delta instead of an identity one.
            # Where the delta owned nothing the composed value IS the base, so those bones cost a
            # track and resolve to exact identity.
            has_translation = has_rotation = True
        elif owned is not None:
            # A derived OVERLAY ships only the bones its mask owns; the rest come from whatever
            # the layered blend is composed over at runtime, which is what the mask is for. An
            # owned bone always ships, including one the clip leaves at its bind pose.
            if bone.index not in owned:
                continue
            has_translation = has_rotation = True
        if not (has_translation or has_rotation):
            continue
        tracks += struct.pack("<I2B", bone_map[bone.index], int(has_translation),
                              int(has_rotation))
        if has_translation:
            for frame in range(clip.frames):
                tracks += struct.pack("<3f", *_conv_pos(frames[frame][bone.index][0]))
        if has_rotation:
            for frame in range(clip.frames):
                quat = (rotations[frame] if rotations is not None
                        else frames[frame][bone.index][1])
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
    (`mdl_gltf._blend_grids`), so a composed cell named any other way is a sample the blend space
    asks for under a name nothing was ever written under, and the grid loses that corner.
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


def _anim_section(d, bones, clips, bone_map, emitted, masks):
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
    payloads = [p for p in (_clip_payload(d, bones, c, bone_map, emitted, masks)
                            for c in clips if c.label.lower() not in unresolvable)
                if p is not None]
    for layer, host, owned in bindings:
        payload = _clip_payload(
            d, bones, layer, bone_map, emitted, masks,
            frames=_composed_frames(d, bones, layer, host, owned),
            base_label=host.label.lstrip("@"), owned=owned,
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


def write_model(idx, model_path, out_dir, stem=None, anorms=None):
    """Write `<out_dir>/<stem>.eskm` and return a summary dict.

    `anorms` is the unit-vector table read out of the user's own `StudioRender.dll`; without
    it a compressed vertex-animation record has directions but no magnitudes, so the morph
    section is omitted rather than baked wrong."""
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

    rows, bone_map = unreal_bones(bones)
    mesh_payload, offsets = _mesh_section(surfaces, matnames, bone_map)
    morph_payload, morph_names = (_morph_section(d, mesh_map, matnames, offsets, anorms)
                                  if anorms and S.flex_descs(d) else (b"", []))

    from elysium_pipeline.formats.mdl_gltf import blend_clip_plan
    own = S.local_sequences(d)
    extra, _blends = blend_clip_plan(d, own)
    masks = {}
    anim_payload, clip_count = _anim_section(d, bones, own + extra, bone_map, len(rows), masks)

    blob = _assemble([
        (b"SKEL", _skel_section(rows)),
        (b"MATL", _matl_section(matnames, matinfo)),
        (b"MESH", mesh_payload),
        (b"MORF", morph_payload),
        (b"MASK", _mask_section(masks, len(rows))),
        (b"ANIM", anim_payload),
    ])
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, stem + ".eskm")
    with open(path, "wb") as fh:
        fh.write(blob)
    triangles = sum(len(s["tris"]) for s in surfaces.values())
    vertices = sum(len(s["pos"]) for s in surfaces.values())
    print(f"  eskm {stem}: {len(bones)} bones, {vertices} verts, {triangles} tris, "
          f"{len(morph_names)} morphs, {clip_count} clips "
          f"-> {path} ({os.path.getsize(path) // 1024} KB)")
    return dict(stem=stem, eskm=os.path.basename(path), model=model_path,
                bones=len(bones), vertices=vertices, triangles=triangles,
                morphs=morph_names, materials=matnames, clips=clip_count)


def write_bank(idx, model_path, out_dir, stem):
    """Write `<out_dir>/banks/<stem>.eskm` -- skeleton + clips, no geometry. Returns None for
    an aggregator model that defines no animated clip."""
    from elysium_pipeline.formats import install
    from elysium_pipeline.formats.mdl_gltf import blend_clip_plan

    key = model_path[:-4] if model_path.lower().endswith(".mdl") else model_path
    d = install.read(idx, key + ".mdl")
    if not d:
        return None
    clips = S.local_sequences(d)
    if not clips:
        return None
    bones = S.read_bones(d)
    extra, _blends = blend_clip_plan(d, clips)
    rows, bone_map = unreal_bones(bones)
    masks = {}
    anim_payload, count = _anim_section(d, bones, clips + extra, bone_map, len(rows), masks)
    if not count:
        return None

    banks_dir = os.path.join(out_dir, "banks")
    os.makedirs(banks_dir, exist_ok=True)
    path = os.path.join(banks_dir, stem + ".eskm")
    with open(path, "wb") as fh:
        fh.write(_assemble([(b"SKEL", _skel_section(rows)),
                            (b"MASK", _mask_section(masks, len(rows))),
                            (b"ANIM", anim_payload)]))
    print(f"  eskm bank {stem}: {len(bones)} bones, {count} clips, {len(masks)} bone mask(s) "
          f"-> {path} ({os.path.getsize(path) // 1024} KB)")
    return dict(stem=stem, eskm="banks/" + os.path.basename(path), model=model_path,
                bones=len(bones), clips=count)
