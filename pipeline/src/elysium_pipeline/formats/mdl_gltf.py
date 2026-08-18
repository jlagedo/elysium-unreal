"""Export a VtMB `.mdl` v2531 skeletal model to glTF 2.0 (.glb).

Decodes the skinned mesh (`mdl_skel.decode_skinned`), the `StudioBone` skeleton, its
animation clips and its facial flexes, converts them into glTF space (Y-up, metres, the same
`(x,y,z)->(x,z,-y)*0.0254` basis change the props use), and packs a `.glb` with a skin +
skeleton node hierarchy + animations + morph targets. glTFRuntime builds the `USkeletalMesh`
+ `UAnimSequence` + `UMorphTarget`s at runtime. Materials reference external PNGs decoded by
the shared `mdl._resolve_material` pipeline into `<out>/tex/`.

Two products (`npc_export.py` drives the batch):
- `export_npc` -> `<out>/<npc>.glb`: mesh + skeleton + the NPC's OWN clips (its dialogue
  anims) + one morph target per flex record (roadmap PL10).
- `export_bank` -> `<out>/banks/<bank>.glb`: skeleton + a shared bank's clips, no mesh -- an
  animation library retargeted onto NPC skeletons by bone name at load (A.7), so shared clips
  are stored once, not baked into every NPC.

The public tooling CLI exposes the single-clip probe through ``export model``.
"""
import json, struct, os
from collections import Counter
import numpy as np
from elysium_pipeline.formats import mdl, mdl_skel as S

SCALE = S.SCALE
# Source->glTF basis M: (x,y,z) -> (x, z, -y), a -90deg rotation about X.
M = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], dtype=np.float64)


def conv_pos(p):
    return (p[0] * SCALE, p[2] * SCALE, -p[1] * SCALE)


def conv_dir(p):
    """The same basis change without the inch->metre scale, for a direction (a normal, or a
    flex's normal delta) rather than a point."""
    return (p[0], p[2], -p[1])


def unconv_pos(p):
    """`conv_pos` inverted: a glTF-space point back to Source inches."""
    return (p[0] / SCALE, -p[2] / SCALE, p[1] / SCALE)


def unconv_dir(p):
    """`conv_dir` inverted: a glTF-space direction back into Source space."""
    return (p[0], -p[2], p[1])


def conv_quat(q):
    """Convert a Source quaternion (x,y,z,w) into glTF space by conjugating its
    rotation with M (R' = M R M^T), returned as (x,y,z,w)."""
    return mat_to_quat(M @ S.rot_matrix(q) @ M.T)


def conv_quat_exact(q):
    """The same basis change as `conv_quat`, taken through the quaternion rather than through
    the rotation matrix.

    M is a proper rotation, so conjugating a rotation by it carries the quaternion `(v, w)` to
    `(M v, w)`. Reaching it that way keeps two things the matrix route drops: which of the two
    quaternions naming the rotation comes back, and enough precision to invert to the float32
    the value was read from. A stored table is read back and re-evaluated rather than only
    drawn, so both matter to it and neither does to a baked mesh or clip."""
    return (*conv_dir(q[:3]), q[3])


def unconv_quat(q):
    """`conv_quat_exact` inverted."""
    return (*unconv_dir(q[:3]), q[3])


def mat_to_quat(R):
    t = R[0, 0] + R[1, 1] + R[2, 2]
    if t > 0:
        s = 0.5 / np.sqrt(t + 1.0)
        w = 0.25 / s
        x = (R[2, 1] - R[1, 2]) * s
        y = (R[0, 2] - R[2, 0]) * s
        z = (R[1, 0] - R[0, 1]) * s
    elif R[0, 0] > R[1, 1] and R[0, 0] > R[2, 2]:
        s = 2.0 * np.sqrt(1.0 + R[0, 0] - R[1, 1] - R[2, 2])
        w = (R[2, 1] - R[1, 2]) / s
        x = 0.25 * s
        y = (R[0, 1] + R[1, 0]) / s
        z = (R[0, 2] + R[2, 0]) / s
    elif R[1, 1] > R[2, 2]:
        s = 2.0 * np.sqrt(1.0 + R[1, 1] - R[0, 0] - R[2, 2])
        w = (R[0, 2] - R[2, 0]) / s
        x = (R[0, 1] + R[1, 0]) / s
        y = 0.25 * s
        z = (R[1, 2] + R[2, 1]) / s
    else:
        s = 2.0 * np.sqrt(1.0 + R[2, 2] - R[0, 0] - R[1, 1])
        w = (R[1, 0] - R[0, 1]) / s
        x = (R[0, 2] + R[2, 0]) / s
        y = (R[1, 2] + R[2, 1]) / s
        z = 0.25 * s
    q = np.array([x, y, z, w])
    return q / np.linalg.norm(q)


def quat_to_mat4(q, t):
    x, y, z, w = q
    m = np.eye(4)
    m[:3, :3] = [
        [1-2*(y*y+z*z), 2*(x*y-z*w),   2*(x*z+y*w)],
        [2*(x*y+z*w),   1-2*(x*x+z*z), 2*(y*z-x*w)],
        [2*(x*z-y*w),   2*(y*z+x*w),   1-2*(x*x+y*y)],
    ]
    m[:3, 3] = t
    return m


class Gltf:
    def __init__(self):
        self.bin = bytearray()
        self.bufferViews = []
        self.accessors = []

    def _view(self, data, target=None):
        while len(self.bin) % 4:
            self.bin.append(0)
        off = len(self.bin)
        self.bin += data
        bv = {"buffer": 0, "byteOffset": off, "byteLength": len(data)}
        if target:
            bv["target"] = target
        self.bufferViews.append(bv)
        return len(self.bufferViews) - 1

    def accessor(self, arr, comp, typ, target=None, mm=False):
        arr = np.ascontiguousarray(arr)
        bv = self._view(arr.tobytes(), target)
        n = arr.shape[0]
        acc = {"bufferView": bv, "componentType": comp, "count": n, "type": typ}
        if mm:
            acc["min"] = arr.min(0).tolist()
            acc["max"] = arr.max(0).tolist()
        self.accessors.append(acc)
        return len(self.accessors) - 1

    def sparse_accessor(self, count, indices_view, values):
        """A VEC3 float accessor of `count` elements that is zero everywhere except at the
        vertices `indices_view` names -- a morph target, where a flex moves a few hundred of
        a mesh's vertices and leaves the rest alone. Omitting `bufferView` on the accessor
        makes the base implicitly zero, so only the moved vertices are stored.

        `indices_view` is shared between a target's POSITION and NORMAL accessors: one flex
        moves the same vertex set in both. It must sit at byteOffset 0 -- glTFRuntime reads
        `sparse.values.byteOffset` but never applies it, so values views are never shared."""
        values = np.ascontiguousarray(values, dtype=np.float32)
        self.accessors.append({
            "componentType": FLOAT, "count": count, "type": "VEC3",
            "sparse": {"count": int(values.shape[0]),
                       "indices": {"bufferView": indices_view, "byteOffset": 0,
                                   "componentType": U32},
                       "values": {"bufferView": self._view(values.tobytes()),
                                  "byteOffset": 0}},
        })
        return len(self.accessors) - 1


FLOAT, U8, U16, U32 = 5126, 5121, 5123, 5125
ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963


def _skeleton_nodes(bones):
    """One glTF node per bone (converted bind-local TRS) + parent->children links, in bone
    order so node index == bone index (what `_bake_animation` targets)."""
    nodes = []
    for b in bones:
        t = conv_pos(b.pos)
        q = conv_quat(b.quat)
        nodes.append({
            "name": b.name,
            "translation": [float(t[0]), float(t[1]), float(t[2])],
            "rotation": [float(q[0]), float(q[1]), float(q[2]), float(q[3])],
        })
    for i, b in enumerate(bones):
        if b.parent != -1:
            nodes[b.parent].setdefault("children", []).append(i)
    return nodes


def _inverse_bind_accessor(g, bones):
    """Inverse-bind matrices (glTF space) from FK over the converted bind locals."""
    Gbind = [None] * len(bones)
    for b in bones:
        lm = quat_to_mat4(conv_quat(b.quat), conv_pos(b.pos))
        Gbind[b.index] = lm if b.parent == -1 else Gbind[b.parent] @ lm
    ibm = np.array([np.linalg.inv(Gbind[i]).T.reshape(16) for i in range(len(bones))],
                   dtype=np.float32)
    return g.accessor(ibm, FLOAT, "MAT4")


def _bake_animation(g, d, bones, label, animdesc_base, nframes, fps):
    """One glTF animation from a decoded clip: a rotation and/or translation sampler per
    animated bone, targeting the bone's node by index (identity -- the clip bakes against its
    own model's skeleton; the cross-skeleton retarget is by bone name at load). Returns the
    animation dict, or None if the clip animates no channel."""
    frames = S.read_anim(d, bones, animdesc_base, nframes)
    recs = animdesc_base + struct.unpack_from("<i", d, animdesc_base + 48)[0]
    times = np.arange(nframes, dtype=np.float32) / (fps or 30.0)
    time_acc = g.accessor(times.reshape(-1, 1), FLOAT, "SCALAR", mm=True)
    channels, samplers = [], []
    for b in bones:
        offs = struct.unpack_from("<7i", d, recs + b.index * 32 + 4)
        if any(offs[3:]):
            qout = np.array([conv_quat(frames[f][b.index][1]) for f in range(nframes)],
                            dtype=np.float32)
            samplers.append({"input": time_acc, "output": g.accessor(qout, FLOAT, "VEC4"),
                             "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1,
                             "target": {"node": b.index, "path": "rotation"}})
        if any(offs[:3]):
            tout = np.array([conv_pos(frames[f][b.index][0]) for f in range(nframes)],
                            dtype=np.float32)
            samplers.append({"input": time_acc, "output": g.accessor(tout, FLOAT, "VEC3"),
                             "interpolation": "LINEAR"})
            channels.append({"sampler": len(samplers) - 1,
                             "target": {"node": b.index, "path": "translation"}})
    if not channels:
        return None
    return {"name": label.lstrip("@"), "channels": channels, "samplers": samplers}


def _morph_names(descs, slots):
    """Unique per-model morph-target names: the flexdesc's FACS name, suffixed `#k` when a
    flexdesc contributes more than one ramp.

    Uniqueness is load-bearing. glTFRuntime keys a `UMorphTarget` by name and its `Merge`
    duplicate strategy fuses same-named pieces, which is exactly how a target that spans two
    materials is reunited -- so two *different* ramps sharing a name would fuse as well."""
    seen, out = Counter(), []
    for flexdesc, _ramp in slots:
        nm = descs[flexdesc] if 0 <= flexdesc < len(descs) else f"flex{flexdesc}"
        out.append(nm if not seen[nm] else f"{nm}#{seen[nm]}")
        seen[nm] += 1
    return out


def _build_morphs(g, d, built, anorms):
    """Bake the model's `StudioFlex` records into per-primitive glTF morph targets (PL10).

    One target per distinct `(flexdesc, target ramp)` across the whole model -- the flex
    *record*, not the flexdesc. A flexdesc can carry two flexes on one mesh under different
    ramps (the eyelid pairs hinge one flexdesc into a lower and an upper half), and the ramp
    is a runtime remap of that morph's weight, so the two are separate morphs. The ramp
    itself is not baked: it rides in the facial manifest, where 12.3 evaluates it.

    The target list is unified over every primitive and written in the same order on each,
    which is what glTF requires (weights are mesh-level) and what lets
    `mesh.extras.targetNames` name them positionally. A primitive a target does not touch
    still carries it, as a one-entry zero sparse accessor -- glTFRuntime applies the names
    first and drops the empty ones after (`bIgnoreEmptyMorphTargets`). A target that spans
    two materials therefore lands as one same-named piece per primitive, which the runtime
    reunites with `MorphTargetsDuplicateStrategy::Merge`.

    Returns the manifest rows, index-aligned with the morph targets written into
    `built["primitives"]`."""
    descs = S.flex_descs(d)
    slot_of, slots = {}, []
    deltas = {}                                  # (slot, material) -> {vertex: [dp3, dn3]}
    for rec in built["mesh_map"]:
        remap = rec["remap"]
        for fx in S.mesh_flexes(d, rec["model_base"], rec["mesh_index"]):
            key = (fx["flexdesc"], tuple(fx["targets"]))
            if key not in slot_of:
                slot_of[key] = len(slots)
                slots.append(key)
            bucket = deltas.setdefault((slot_of[key], rec["material"]), {})
            for local, dv, nv in S.vert_anims(d, fx, anorms):
                # A vertex no triangle references never entered the surface (decode_skinned
                # dedupes on use), so it has no morph slot either.
                si = remap.get(rec["vertex_offset"] + local)
                if si is None:
                    continue
                e = bucket.setdefault(si, [0.0] * 6)
                if dv:
                    for k, c in enumerate(conv_pos(dv)):
                        e[k] += c
                if nv:
                    for k, c in enumerate(conv_dir(nv)):
                        e[3 + k] += c
    if not slots:
        return []

    for pi, mn in enumerate(built["matnames"]):
        nvert = len(built["surfaces"][mn]["pos"])
        zero, targets = None, []
        for slot in range(len(slots)):
            bucket = deltas.get((slot, mn))
            if not bucket:
                if zero is None:
                    # One accessor for both attributes of every untouched target of this
                    # primitive: a single zero delta at vertex 0 (`sparse.count` must be >= 1).
                    z = g.sparse_accessor(nvert, g._view(np.zeros(1, np.uint32).tobytes()),
                                          np.zeros((1, 3), np.float32))
                    zero = {"POSITION": z, "NORMAL": z}
                targets.append(zero)
                continue
            keys = sorted(bucket)
            iv = g._view(np.array(keys, dtype=np.uint32).tobytes())
            vals = np.array([bucket[k] for k in keys], dtype=np.float32)
            targets.append({"POSITION": g.sparse_accessor(nvert, iv, vals[:, :3]),
                            "NORMAL": g.sparse_accessor(nvert, iv, vals[:, 3:])})
        built["primitives"][pi]["targets"] = targets

    names = _morph_names(descs, slots)
    built["target_names"] = names
    return [{"name": nm, "flexdesc": fd, "targets": [round(t, 6) for t in ramp]}
            for nm, (fd, ramp) in zip(names, slots)]


def facial_rig(d, morphs):
    """The half of the face a morph target cannot hold (`docs/vtmb/facial_animation.md`).

    A morph is a vertex displacement; VtMB drives it through three layers above that --
    44 flex controllers, 60 RPN flex rules that combine them into flexdesc weights, and the
    per-flex trapezoid each weight is remapped through. All three are runtime evaluation, so
    they ship as data beside the glb and 12.3 replays them. `mouth` is Source's
    amplitude-driven jaw, which runs off the line's audio envelope rather than the rules."""
    return {
        "flexdescs": S.flex_descs(d),
        "controllers": [{"name": nm, "type": t, "min": lo, "max": hi}
                        for t, nm, lo, hi in S.flex_controllers(d)],
        # An op is `[name]`, or `[name, operand]` for the three that take one: CONST carries a
        # float, FETCH1/FETCH2 a flex-controller index. The other opcodes read their arguments
        # off the stack, and the 4 bytes their union would occupy are undefined -- so they are
        # not carried.
        "rules": [{"flexdesc": fd,
                   "ops": [[op] if op not in ("CONST", "FETCH1", "FETCH2")
                           else [op, fv if op == "CONST" else iv] for op, iv, fv in ops]}
                  for fd, ops in S.flex_rules(d)],
        "mouths": [{"bone": b, "forward": list(f), "flexdesc": fd} for b, f, fd in S.mouths(d)],
        # The lipsync blend width, per model: a phoneme's own duration clamped to this pair
        # decides how long it ramps in before its authored start and out to its authored end.
        # The fourth input of 12.5's per-line join, and the only one not already on disk.
        "phoneme_filter": [round(v, 6) for v in S.phoneme_filter(d)],
        "morphs": morphs,
    }


def _build_skinned(g, idx, d, v, model_path, out_dir, anorms=None):
    """Skeleton nodes + skin + per-material mesh primitives + materials into `g`.

    Returns a dict the assemblers finish: nodes (with the mesh node appended), meshes, skins,
    materials, images, textures, bones. The mesh/material path is the game-verified single-clip
    path, unchanged -- only the animation set differs between products. With `anorms` (the
    unit-vector table out of the user's own `StudioRender.dll`) the model's flexes are baked
    into morph targets as well, and `facial` carries the rows describing them."""
    from elysium_pipeline.formats import install

    bones = S.read_bones(d)
    mesh_map = []
    surfaces = S.decode_skinned(d, v, mesh_map)
    os.makedirs(os.path.join(out_dir, "tex"), exist_ok=True)
    search = mdl.search_paths(d)
    read_bytes = lambda k: install.read(idx, k)
    tex_cache = {}

    skin_bones = bones

    nodes = _skeleton_nodes(skin_bones)
    ibm_acc = _inverse_bind_accessor(g, skin_bones)

    # --- materials ---
    matnames = list(surfaces.keys())
    images, textures, materials, mat_index, matinfo = [], [], [], {}, {}
    for mn in matnames:
        matinfo[mn] = mdl._resolve_material(mn, search, read_bytes, out_dir, tex_cache)
        albedo = matinfo[mn]["albedo"]
        # Double-sided like the rest of the project (world/props render CullMode Disabled):
        # VtMB character meshes have open/thin geometry (tank-top neck & armholes, mouth, eye
        # sockets) that shows the culled interior when orbited.
        mat = {"name": mdl.sanitize(mn), "doubleSided": True,
               "pbrMetallicRoughness": {"metallicFactor": 0.0, "roughnessFactor": 1.0}}
        if albedo:
            ti = len(images)
            images.append({"uri": f"tex/{albedo}"})
            textures.append({"source": ti})
            mat["pbrMetallicRoughness"]["baseColorTexture"] = {"index": len(textures) - 1}
        else:
            mat["pbrMetallicRoughness"]["baseColorFactor"] = [0.6, 0.6, 0.62, 1.0]
        mat_index[mn] = len(materials)
        materials.append(mat)

    # --- mesh primitives (one per material) ---
    primitives = []
    for mn in matnames:
        s = surfaces[mn]
        pos = np.array([conv_pos(p) for p in s["pos"]], dtype=np.float32)
        uv = np.array(s["uv"], dtype=np.float32)
        joints = np.array(s["joints"], dtype=np.uint16)
        weights = np.array(s["weights"], dtype=np.float32)
        tris = np.array(s["tris"], dtype=np.uint32).reshape(-1)
        # per-vertex normals from face normals (glTF space)
        nrm = np.zeros_like(pos)
        tv = tris.reshape(-1, 3)
        fn = np.cross(pos[tv[:, 1]] - pos[tv[:, 0]], pos[tv[:, 2]] - pos[tv[:, 0]])
        for k in range(3):
            np.add.at(nrm, tv[:, k], fn)
        ln = np.linalg.norm(nrm, axis=1, keepdims=True)
        nrm = nrm / np.where(ln == 0, 1, ln)
        primitives.append({
            "attributes": {
                "POSITION": g.accessor(pos, FLOAT, "VEC3", ARRAY_BUFFER, mm=True),
                "NORMAL": g.accessor(nrm.astype(np.float32), FLOAT, "VEC3", ARRAY_BUFFER),
                "TEXCOORD_0": g.accessor(uv, FLOAT, "VEC2", ARRAY_BUFFER),
                "JOINTS_0": g.accessor(joints, U16, "VEC4", ARRAY_BUFFER),
                "WEIGHTS_0": g.accessor(weights, FLOAT, "VEC4", ARRAY_BUFFER),
            },
            "indices": g.accessor(tris, U32, "SCALAR", ELEMENT_ARRAY_BUFFER),
            "material": mat_index[mn],
        })

    mesh_node = len(nodes)
    nodes.append({"name": mdl.sanitize(os.path.basename(model_path)), "mesh": 0, "skin": 0})
    built = dict(bones=bones, skin_bones=skin_bones,
                 nodes=nodes, mesh_node=mesh_node, primitives=primitives,
                 ibm_acc=ibm_acc, materials=materials, images=images, textures=textures,
                 surfaces=surfaces, matnames=matnames, mesh_map=mesh_map, target_names=[])
    built["facial"] = (facial_rig(d, _build_morphs(g, d, built, anorms))
                       if anorms and S.flex_descs(d) else None)
    # Independent of the flex rig: 57 of the 59 player bodies carry eyeballs and no flex data
    # at all, so their irises aim while their lids cannot move.
    built["eyes"], built["eye_faults"] = S.eye_records(d, bones, mesh_map, matinfo, mdl.sanitize)
    return built


def _bone_subtree_sizes(bones):
    """Per-bone descendant count, from each bone's OWN parent chain rather than a child-list
    build -- a few bones deep on any of these rigs, negligible against reading a whole skeleton
    once."""
    sizes = [0] * len(bones)
    for b in bones:
        p = b.parent
        while p >= 0:
            sizes[p] += 1
            p = bones[p].parent
    return sizes


def _choose_skeleton_root(bones, roots):
    """The parent-less bone with the LARGEST descendant subtree, ties broken by lowest original
    index so the choice is deterministic -- the same rule `UE_mdl_skeletal.unreal_bones` applies,
    so the two exporters agree on which bone a forked skeleton's real root is (verified against
    the corpus: `regular_cop` bone 0 is a childless `tongue` leaf and bone 1 is `Bip01` with 76
    descendants; `prophet` bone 0 is `Bip01` and bone 59 is a childless `Tube01` leaf -- so
    neither "first parent-less bone" nor a hardcoded name picks correctly for both)."""
    sizes = _bone_subtree_sizes(bones)
    return min(roots, key=lambda i: (-sizes[i], i))


def _reparent_bind(root_bone, stray_bone):
    """`stray_bone`'s bind restated as a child local under `root_bone`, preserving its
    MODEL-SPACE transform -- the same composition `UE_mdl_skeletal.unreal_bones` performs,
    carried out here through the rotation matrix this module already carries every bone bind
    through (`conv_quat`), rather than the Hamilton-product convention the ESKM writer uses.

    A parent-less StudioBone has nothing to be relative to, so its stored (pos, quat) already
    names its model-space bind -- exactly like the root it is being folded under. Composing
    `new_local = root^-1 * stray` in that shared Source-space convention, before either value
    passes through `conv_pos`/`conv_quat`, reproduces `stray_bone`'s original model-space
    transform once ordinary FK recomposes it under `root_bone`."""
    root_r = S.rot_matrix(root_bone.quat)
    local_pos = tuple(float(c) for c in
                       root_r.T @ (np.array(stray_bone.pos, dtype=np.float64)
                                  - np.array(root_bone.pos, dtype=np.float64)))
    stray_r = S.rot_matrix(stray_bone.quat)
    local_quat = tuple(float(c) for c in mat_to_quat(root_r.T @ stray_r))
    return local_pos, local_quat


def _assemble_skinned(built, animations):
    """glTF dict for a skinned mesh + skeleton + the given animations."""
    bones = built["bones"]
    skin_bones = built.get("skin_bones") or bones
    nodes = built["nodes"]
    roots = [b.index for b in bones if b.parent == -1]
    # glTFRuntime traverses the skeleton from a SINGLE root down `children`, so any bone not
    # reachable from that root is dropped from its bone map -- a vertex weighted to it then aborts
    # the whole mesh load (and leaves a half-built USkeletalMesh that faults on GC). A few VtMB
    # skeletons have more than one parent-less bone (e.g. regular_cop bones 0/1, prophet bones
    # 0/59). Rather than unifying them under a synthetic node, the largest-subtree root
    # (`_choose_skeleton_root`) stays the scene root and absorbs every other one as a `children`
    # entry, with that bone's OWN node re-expressed as a child local under it
    # (`_reparent_bind`) -- the node index stays `bone.index` for every bone either way (the
    # node-index == bone-index invariant `_bake_animation` targets, and skin.joints stays
    # range(len(bones)), so JOINTS_0 values still map 1:1 to the real bones).
    if len(roots) > 1:
        skel_root = _choose_skeleton_root(bones, roots)
        for r in roots:
            if r == skel_root:
                continue
            local_pos, local_quat = _reparent_bind(bones[skel_root], bones[r])
            nodes[r]["translation"] = [float(c) for c in conv_pos(local_pos)]
            nodes[r]["rotation"] = [float(c) for c in conv_quat(local_quat)]
            nodes[skel_root].setdefault("children", []).append(r)
    else:
        skel_root = roots[0]
    # Morph weights are mesh-level in glTF, so the target NAMES are too: `extras.targetNames`
    # is read positionally against each primitive's target list, which is why every primitive
    # carries the same targets in the same order (`_build_morphs`).
    mesh = {"primitives": built["primitives"]}
    if built.get("target_names"):
        mesh["weights"] = [0.0] * len(built["target_names"])
        mesh["extras"] = {"targetNames": built["target_names"]}
    gltf = {
        "asset": {"version": "2.0", "generator": "elysium mdl_gltf"},
        "scene": 0,
        "scenes": [{"nodes": [skel_root, built["mesh_node"]]}],
        "nodes": nodes,
        "meshes": [mesh],
        "skins": [{"inverseBindMatrices": built["ibm_acc"],
                   "joints": list(range(len(skin_bones))), "skeleton": skel_root}],
        "materials": built["materials"],
    }
    if animations:
        gltf["animations"] = animations
    if built["images"]:
        gltf["images"] = built["images"]
        gltf["textures"] = built["textures"]
        gltf["samplers"] = [{}]
        for t in built["textures"]:
            t["sampler"] = 0
    return gltf, skel_root


def _reconcile_blends(blends, baked):
    """Drop every grid cell whose clip did not bake, and every grid left with fewer than two.

    `_bake_animation` returns nothing for a sequence whose tracks come out empty, so a cell
    naming it would promise a clip the glb does not carry — the same reconciliation the NPC
    manifest runs over its resolved clip vocabulary."""
    out = {}
    for label, grid in blends.items():
        cells = [
            cell if cell["clip"] in baked else {**cell, "clip": None}
            for cell in grid["cells"]
        ]
        if sum(1 for cell in cells if cell["clip"]) < 2:
            continue
        out[label] = {**grid, "cells": cells}
    return out


def export_npc(idx, model_path, out_dir, stem=None, anorms=None,
               measure_extents=False):
    """Write `<out_dir>/<stem>.glb`: skinned mesh + skeleton + the NPC's OWN clips (the
    dialogue anims that live only in this .mdl) + its facial morph targets. Shared clips come
    from bank glbs applied by bone name at runtime. Returns
    {stem, glb, model, bones, split_bones, clips:[mdl_skel.Seq,...], clip_extents, facial,
    procedural, procedural_faults} — the clip list is what actually baked, so a sequence whose
    tracks came out empty is absent, and `facial` is None for a model with no flex rig (or when
    the `anorms` table could not be read).

    `measure_extents` fills `clip_extents` with `clip_extent` per baked label. It decodes every
    clip a second time, which is worth it for the handful of animated props whose render bound
    depends on it and is not worth it for the NPC corpus, so it is off by default.
    `split_bones` preserves the target model's StudioBone `Flags & 0x2` inventory for the
    runtime pose builder; it is application metadata, not an alternate channel decode.
    `procedural` and `eyes` are the model's `ProcType == 1` rule table and its `StudioEyeball`
    records, both in **Source** space (`mdl_skel.axis_interp_records` / `mdl_skel.eye_records`).
    They are sidecar payloads rather than glTF content, and the exporter that writes them --
    `UE_mdl_skeletal.axis_interp_rules` / `.eye_rig` -- is what states them in a frame.
"""
    dv = mdl.load(idx, model_path)
    if not dv:
        raise SystemExit(f"model not found: {model_path}")
    d, v = dv
    stem = stem or mdl.sanitize(os.path.basename(model_path)[:-4])

    g = Gltf()
    built = _build_skinned(g, idx, d, v, model_path, out_dir, anorms)
    own = S.local_sequences(d)
    extra, blends = S.blend_clip_plan(d, own)
    animations, labels, extents = [], [], {}
    for c in own + extra:
        anim = _bake_animation(g, d, built["bones"], c.label, c.base, c.frames, c.fps)
        if anim:
            animations.append(anim)
            labels.append(c)
            if measure_extents:
                extents[c.label] = S.clip_extent(d, built["bones"], c.base, c.frames)
    blends = _reconcile_blends(blends, {c.label for c in labels})
    gltf, _root = _assemble_skinned(built, animations)
    gltf["accessors"] = g.accessors
    gltf["bufferViews"] = g.bufferViews
    gltf["buffers"] = [{"byteLength": len(g.bin)}]

    glb = os.path.join(out_dir, stem + ".glb")
    _write_glb(gltf, g.bin, glb)
    tris = sum(len(s["tris"]) for s in built["surfaces"].values())
    face = built["facial"]
    morphs = f", {len(face['morphs'])} morphs" if face and face["morphs"] else ""
    rules, rule_faults = S.axis_interp_records(d, built["bones"])
    driven = f", {len(rules)} driven bones" if rules else ""
    grids = f", {len(blends)} blend grids" if blends else ""
    eyes = built["eyes"]
    eyeballs = f", {len(eyes['eyeballs'])} eyeballs" if eyes else ""
    print(f"  npc {stem}: {len(built['bones'])} bones, {tris} tris, {len(labels)} own clips"
          f"{morphs}{driven}{grids}{eyeballs}"
          f" -> {glb} ({os.path.getsize(glb) // 1024} KB)")
    for fault in rule_faults:
        print(f"  ! {stem}: procedural rule - {fault}")
    for fault in built["eye_faults"]:
        print(f"  ! {stem}: eyeball - {fault}")
    for orphan in S.autolayer_orphans(labels):
        print(f"  ! {stem}: autolayer target '{orphan}' baked no clip")
    return dict(stem=stem, glb=os.path.basename(glb), model=model_path,
                bones=len(built["bones"]),
                split_bones=[b.name for b in built["bones"] if b.flags & 0x2],
                clips=labels, clip_extents=extents,
                facial=face, blends=S.blend_sidecar(d, blends, labels),
                procedural=rules, procedural_faults=rule_faults,
                eyes=eyes, eye_faults=built["eye_faults"])


def export_bank(idx, model_path, out_dir, stem):
    """Write `<out_dir>/banks/<stem>.glb`: the bank skeleton (named bone nodes) + all its
    clips, no mesh/materials -- an animation library retargeted onto NPC skeletons by bone
    name at load (A.7). Returns {stem, glb, model, clips:[mdl_skel.Seq,...], blends} or None
    if the bank defines no animated clip (aggregator/plumbing models). `clips` carries one
    entry per baked animation, which for a multi-cell sequence is every cell rather than the
    base alone; `blends` is the grid table naming which clip each cell is."""
    from elysium_pipeline.formats import install

    key = model_path[:-4] if model_path.lower().endswith(".mdl") else model_path
    d = install.read(idx, key + ".mdl")
    if not d:
        return None
    clips = S.local_sequences(d)
    if not clips:
        return None
    bones = S.read_bones(d)
    extra, blends = S.blend_clip_plan(d, clips)

    g = Gltf()
    nodes = _skeleton_nodes(bones)
    animations, labels = [], []
    for c in clips + extra:
        anim = _bake_animation(g, d, bones, c.label, c.base, c.frames, c.fps)
        if anim:
            animations.append(anim)
            labels.append(c)
    if not animations:
        return None
    blends = _reconcile_blends(blends, {c.label for c in labels})
    root = next(b.index for b in bones if b.parent == -1)

    banks_dir = os.path.join(out_dir, "banks")
    os.makedirs(banks_dir, exist_ok=True)
    gltf = {
        "asset": {"version": "2.0", "generator": "elysium mdl_gltf bank"},
        "scene": 0,
        "scenes": [{"nodes": [root]}],
        "nodes": nodes,
        "animations": animations,
        "accessors": g.accessors,
        "bufferViews": g.bufferViews,
        "buffers": [{"byteLength": len(g.bin)}],
    }
    glb = os.path.join(banks_dir, stem + ".glb")
    _write_glb(gltf, g.bin, glb)
    grids = f", {len(blends)} blend grids" if blends else ""
    hosts = sum(1 for c in labels if c.autolayers)
    layered = f", {hosts} autolayer hosts" if hosts else ""
    print(f"  bank {stem}: {len(bones)} bones, {len(labels)} clips{grids}{layered} "
          f"-> {glb} ({os.path.getsize(glb) // 1024} KB)")
    for orphan in S.autolayer_orphans(labels):
        print(f"  ! {stem}: autolayer target '{orphan}' baked no clip")
    return dict(stem=stem, glb="banks/" + os.path.basename(glb), model=model_path,
                clips=labels, blends=S.blend_sidecar(d, blends, labels))


def export_cinematic(idx, model_path, out_dir, stem):
    """Write one bank per bone root of a cinematic `.mdl` -> `<out_dir>/banks/<stem>__<root>.glb`.

    A cinematic model is a whole multi-actor performance packed into one file: N co-located
    skeletons (`Bip01`..`BipNN`, ~67 bones each) carrying a single clip, usually `entire_scene`.
    A choreo scene's `bonerename "BipNN" "Bip01"` is how each actor picks its own root out of
    that one clip (`docs/vtmb/choreographed_scenes.md`).

    Each root is emitted as its own bank with the prefix folded back to `Bip01`, so the existing
    bone-name retarget applies it to an ordinary NPC skeleton with no new runtime path. A model
    with a single root is written once, unsuffixed, exactly like any other bank.

    Returns a list of bank dicts (as `export_bank`), each with an extra `root` key, or None.
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
        one = export_bank(idx, model_path, out_dir, stem)
        if one:
            one["root"] = roots[0] if roots else None
        return [one] if one else None

    # Blend-grid cells bake as clips here for the reason they do anywhere else; the per-root
    # split below reads them through the same loop, so a cell is a clip on every root.
    extra, blends = S.blend_clip_plan(d, clips)
    clips = clips + extra

    # Decode each clip once against the FULL skeleton: the animation records are indexed by the
    # model's own bone order, so a per-root subset has to read through the original indices.
    decoded = {}
    for c in clips:
        decoded[c.label] = S.read_anim(d, bones, c.base, c.frames)

    banks_dir = os.path.join(out_dir, "banks")
    os.makedirs(banks_dir, exist_ok=True)
    out = []

    for root in roots:
        low = root.lower()
        sub = [b for b in bones if (S._bone_root(b.name) or "").lower() == low]
        if not sub:
            continue
        old2new = {b.index: i for i, b in enumerate(sub)}

        g = Gltf()
        nodes = []
        for b in sub:
            t = conv_pos(b.pos)
            q = conv_quat(b.quat)
            # Fold this actor's prefix back to Bip01 so the clip retargets onto a normal skeleton.
            name = b.name
            if name[:len(root)].lower() == low:
                name = "Bip01" + name[len(root):]
            nodes.append({
                "name": name,
                "translation": [float(t[0]), float(t[1]), float(t[2])],
                "rotation": [float(q[0]), float(q[1]), float(q[2]), float(q[3])],
            })
        for i, b in enumerate(sub):
            if b.parent in old2new:
                nodes[old2new[b.parent]].setdefault("children", []).append(i)

        animations, labels = [], []
        for c in clips:
            frames = decoded[c.label]
            recs = c.base + struct.unpack_from("<i", d, c.base + 48)[0]
            times = np.arange(c.frames, dtype=np.float32) / (c.fps or 30.0)
            time_acc = g.accessor(times.reshape(-1, 1), FLOAT, "SCALAR", mm=True)
            channels, samplers = [], []
            for b in sub:
                offs = struct.unpack_from("<7i", d, recs + b.index * 32 + 4)
                node = old2new[b.index]
                if any(offs[3:]):
                    qout = np.array([conv_quat(frames[f][b.index][1]) for f in range(c.frames)],
                                    dtype=np.float32)
                    samplers.append({"input": time_acc, "output": g.accessor(qout, FLOAT, "VEC4"),
                                     "interpolation": "LINEAR"})
                    channels.append({"sampler": len(samplers) - 1,
                                     "target": {"node": node, "path": "rotation"}})
                if any(offs[:3]):
                    tout = np.array([conv_pos(frames[f][b.index][0]) for f in range(c.frames)],
                                    dtype=np.float32)
                    samplers.append({"input": time_acc, "output": g.accessor(tout, FLOAT, "VEC3"),
                                     "interpolation": "LINEAR"})
                    channels.append({"sampler": len(samplers) - 1,
                                     "target": {"node": node, "path": "translation"}})
            if channels:
                animations.append({"name": c.label.lstrip("@"), "channels": channels,
                                   "samplers": samplers})
                labels.append(c)
        if not animations:
            continue

        # The subtree's own root: the one bone whose parent is outside the subtree.
        sub_root = next((old2new[b.index] for b in sub if b.parent not in old2new), 0)
        name = f"{stem}__{low}"
        gltf = {
            "asset": {"version": "2.0", "generator": "elysium mdl_gltf cinematic"},
            "scene": 0,
            "scenes": [{"nodes": [sub_root]}],
            "nodes": nodes,
            "animations": animations,
            "accessors": g.accessors,
            "bufferViews": g.bufferViews,
            "buffers": [{"byteLength": len(g.bin)}],
        }
        glb = os.path.join(banks_dir, name + ".glb")
        _write_glb(gltf, g.bin, glb)
        rooted = _reconcile_blends(blends, {c.label for c in labels})
        grids = f", {len(rooted)} blend grids" if rooted else ""
        print(f"  cinematic {name}: {len(sub)} bones, {len(labels)} clips{grids} "
              f"-> {glb} ({os.path.getsize(glb) // 1024} KB)")
        out.append(dict(stem=name, glb="banks/" + os.path.basename(glb), model=model_path,
                        clips=labels, blends=S.blend_sidecar(d, rooted, labels), root=root))

    return out or None


def export(model_path, anim_name=None, out_dir=None, *, index=None):
    """Single-clip probe (the CLI / 8.2 spike): mesh + skeleton + one named clip, found by
    sequence label first, then by raw anim name."""
    from elysium_pipeline.formats import install

    idx = index if index is not None else install.build_index()
    dv = mdl.load(idx, model_path)
    if not dv:
        raise SystemExit(f"model not found: {model_path}")
    d, v = dv
    local = S.local_sequences(d)
    if anim_name is None:
        clip = next(iter(local), None)
        if clip is None:
            raise SystemExit(f"model has no local animation clips: {model_path}")
        anim_name = clip.label
    want = anim_name.lstrip("@").lower()
    clip = next((c for c in local if c.label.lower() == want), None)
    if clip is None:
        found = S.find_anim(d, anim_name)
        if not found:
            raise SystemExit(f"anim not found: {anim_name}")
        ab, nframes, fps = found
        clip = S.Seq(label=anim_name, base=ab, frames=nframes, fps=fps,
                     activity="", actweight=0, flags=0)

    if out_dir is None:
        raise ValueError("out_dir is required")
    g = Gltf()
    built = _build_skinned(g, idx, d, v, model_path, out_dir, S.load_anorms())
    anim = _bake_animation(g, d, built["bones"], clip.label, clip.base, clip.frames, clip.fps)
    gltf, _root = _assemble_skinned(built, [anim] if anim else [])
    gltf["accessors"] = g.accessors
    gltf["bufferViews"] = g.bufferViews
    gltf["buffers"] = [{"byteLength": len(g.bin)}]
    name = mdl.sanitize(os.path.basename(model_path)[:-4])
    _write_glb(gltf, g.bin, os.path.join(out_dir, name + ".glb"))
    tris = sum(len(s["tris"]) for s in built["surfaces"].values())
    print(f"wrote {out_dir}/{name}.glb  ({len(built['bones'])} bones, "
          f"{len(built['materials'])} materials, {tris} tris, anim '{clip[0]}' {clip[2]}f, "
          f"{len(built['target_names'])} morph targets)")


def _write_glb(gltf, bin_data, path):
    js = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    while len(js) % 4:
        js += b" "
    bd = bytes(bin_data)
    while len(bd) % 4:
        bd += b"\0"
    total = 12 + 8 + len(js) + 8 + len(bd)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A)); f.write(js)
        f.write(struct.pack("<II", len(bd), 0x004E4942)); f.write(bd)
