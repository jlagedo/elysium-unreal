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

from elysium_pipeline.asset_names import rig_bone_name
from elysium_pipeline.formats import bsp, mdl, mdl_secondary_motion as SM, mdl_skel as S

from elysium_pipeline.skeletal_stage.payload import (
    VERSION, MAGIC, BASE_SEPARATOR, MAX_INFLUENCES, SPLIT_ROTATION, DELTA_SEQUENCE,
    DRIVER_AXES,
    _string,
    _conv_pos,
    _conv_dir,
    _conv_quat,
    unreal_axis_rules,
    unreal_eye_rig,
    unreal_swings,
    unreal_envelopes,
    _qmul,
    _qconj,
    _qrotate,
    _qnorm,
    _split_ancestors,
    _split_rotation_tracks,
    _bind_world_rotation,
    _bone_subtree_sizes,
    _reparent_local,
    _single_root,
    unreal_bones,
    _skel_section,
    _ref_pose_rows,
    _attachment_section,
    _matl_section,
    _surface_normals,
    _model_space_pose,
    _reskin_surfaces,
    _mesh_section,
    _morph_section,
    _morph_names,
    _bone_mask,
    _owned_bones,
    _owns_split_bone,
    _composed_frames,
    _authored_channels,
    _owned_channels,
    _clip_payload,
    _cell_names,
    _derived_bindings,
    _grid_animations,
    _anim_section,
    _mask_section,
    _assemble,
)


def _dynamics_section(model_path, blob, bones):
    """The deliberately narrow, baked-native AnimDynamics POC recipe for this body."""
    chains = SM.anim_dynamics_poc_chains(model_path, blob, bones)
    if not chains:
        return b"", 0
    out = bytearray(struct.pack("<I", len(chains)))
    for chain in chains:
        # The chain binds by name against the SKEL section, whose names pass `rig_bone_name`.
        out += _string(rig_bone_name(chain.first_bone))
        out += _string(rig_bone_name(chain.chain_end))
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
        d, bones, own + extra, bone_map, len(rows), masks, ensure_labels or (),
        reparented=reparented)

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
    rows, bone_map, reparented = unreal_bones(bones)
    masks = {}
    anim_payload, count = _anim_section(d, bones, clips + extra, bone_map, len(rows), masks,
                                        reparented=reparented)
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


from elysium_pipeline.skeletal_stage.cinematics import actor_rows as _cinematic_rows


def write_cinematic(idx, model_path, out_dir, stem):
    """Write one bank per bone root of a cinematic `.mdl` -> `<out_dir>/banks/<stem>__<root>.eskm`.

    The `.eskm` twin of `mdl_gltf.export_cinematic`, and it exists for the same reason the rest of
    this module does: the mount is the only build of a character, so every clip a scene can name
    must be present as a native container and then as one shared-bank asset
    (the character verifier).

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
        rows, order, reparented = _cinematic_rows(sub, root)
        bone_map = [-1] * len(bones)
        for index, slot in order.items():
            bone_map[index] = slot
        masks = {}
        anim_payload, count = _anim_section(d, bones, all_clips, bone_map, len(rows), masks,
                                            reparented=reparented)
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
