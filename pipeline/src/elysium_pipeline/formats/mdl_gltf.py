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

SCALE = 0.0254
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


def load_anorms():
    """The unit-vector table a compressed vertex-animation record indexes, or None with a
    warning if the user's `StudioRender.dll` cannot be read. It is compiled into the renderer
    rather than shipped as data, so it is extracted from the install at export time and never
    committed; without it the flexes have directions but no magnitudes, and the export drops
    morph targets rather than baking wrong ones."""
    try:
        return S.read_anorms()
    except (OSError, RuntimeError, struct.error) as e:
        print(f"  ! no unit-vector table ({e}) - exporting without morph targets")
        return None


def rot_matrices(q):
    """`rot_matrix` over an (N,4) array of (x,y,z,w) quaternions -> (N,3,3)."""
    x, y, z, w = q[:, 0], q[:, 1], q[:, 2], q[:, 3]
    return np.stack([
        1-2*(y*y+z*z), 2*(x*y-z*w),   2*(x*z+y*w),
        2*(x*y+z*w),   1-2*(x*x+z*z), 2*(y*z-x*w),
        2*(x*z-y*w),   2*(y*z+x*w),   1-2*(x*x+y*y),
    ], axis=-1).reshape(-1, 3, 3)


def clip_extent(d, bones, animdesc_base, nframes):
    """The furthest any bone reaches from the model origin over one clip, in this glb's
    metres -- the radius a renderer needs to keep the posed model on screen.

    This is the *measured* answer to the question `mdl_skel.Seq.bbmin`/`bbmax` already
    answers from the file. Both exist because a descriptor carrying zeros would otherwise
    hand the runtime a bound smaller than the geometry it has to cover, and this corpus does
    ship zeroed bounds -- every model's header `ViewBBMin`/`ViewBBMax` is (0,0,0).

    Composed in Source space and scaled once at the end: M is a rotation, so a magnitude
    survives the basis change and the per-frame conversion is wasted work here."""
    if not bones or nframes <= 0:
        return 0.0
    frames = S.read_anim(d, bones, animdesc_base, nframes)
    n = len(bones)
    lt = np.array([[frames[f][i][0] for i in range(n)] for f in range(nframes)], dtype=np.float64)
    lq = np.array([[frames[f][i][1] for i in range(n)] for f in range(nframes)], dtype=np.float64)
    lr = rot_matrices(lq.reshape(-1, 4)).reshape(nframes, n, 3, 3)
    wt, wr = np.empty_like(lt), np.empty_like(lr)
    for i, b in enumerate(bones):
        p = b.parent
        # A root, or a parent declared after its child -- which the composition cannot honour
        # and no v2531 skeleton writes. Either way the bone stands on the model origin.
        if not 0 <= p < i:
            wt[:, i], wr[:, i] = lt[:, i], lr[:, i]
            continue
        wt[:, i] = wt[:, p] + np.einsum("fab,fb->fa", wr[:, p], lt[:, i])
        wr[:, i] = np.einsum("fab,fbc->fac", wr[:, p], lr[:, i])
    return float(np.abs(wt).max()) * SCALE


def rot_matrix(q):
    """Source-space 3x3 rotation matrix of a quaternion (x,y,z,w)."""
    x, y, z, w = q
    return np.array([
        [1-2*(y*y+z*z), 2*(x*y-z*w),   2*(x*z+y*w)],
        [2*(x*y+z*w),   1-2*(x*x+z*z), 2*(y*z-x*w)],
        [2*(x*z-y*w),   2*(y*z+x*w),   1-2*(x*x+y*y)],
    ])


def conv_quat(q):
    """Convert a Source quaternion (x,y,z,w) into glTF space by conjugating its
    rotation with M (R' = M R M^T), returned as (x,y,z,w)."""
    return mat_to_quat(M @ rot_matrix(q) @ M.T)


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


AXIS_INTERP = 1                    # StudioBone.ProcType; the only rule kind VtMB declares
BONE_STRIDE = 160
BONE_FLAGS, BONE_PROC_TYPE, BONE_PROC_INDEX = 136, 140, 144
AXIS_INTERP_BYTES = 176
SOURCE_AXES = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
# The three Source axes a rule's terms are indexed by, carried into glTF space by the basis
# change the mesh and the clips go through. `-0.0` is normalized away so the exported vectors
# read as the signed unit vectors they are.
DRIVER_AXES = [[c + 0.0 for c in conv_dir(a)] for a in SOURCE_AXES]


def axis_interp_rules(d, bones):
    """Every `ProcType == 1` correction table the model declares, in the glb's own basis.

    A procedural bone ignores its animation channels: its local transform is recomputed from
    the current orientation of a control bone through a six-entry table authored into the
    model, after the locals blend and the hierarchy composes. That correction is in no clip,
    so it ships as data beside the glb. The rule, its evidence and the order it evaluates in
    are `docs/vtmb/procedural_bones.md`.

    The basis is the difficulty. The six entries and the axis index are Source quantities, and
    a change of basis conjugates a bone local -- so the axis a rule names is not the same axis
    afterwards, and may be negated. Carrying the axis as a *direction* and the three term
    weights as the images of the Source axes states the rule in the glb's own space: with `w`
    the control bone's local rotation applied to `axis`, term `k`'s signed weight is
    `dot(DRIVER_AXES[k], w)`, a positive weight selecting entry `2k` and a negative one
    `2k+1`. The entries go through `conv_pos` and `conv_quat`, the same functions that write
    the mesh and the clips, which makes the table and the artifact consistent by construction
    rather than by agreement.

    Returns `(rules, faults)`. A rule that does not resolve inside the image, or names a
    control bone or an axis out of range, is a named fault rather than a silent drop.
    """
    base = struct.unpack_from("<i", d, 244)[0]
    rules, faults = [], []
    for b in bones:
        record = base + b.index * BONE_STRIDE
        proc_type = struct.unpack_from("<i", d, record + BONE_PROC_TYPE)[0]
        # Either field identifies a driven bone on its own over the shipped corpus, so a
        # disagreement is a model this reader has not seen before rather than a preference.
        if bool(b.flags & 0x1) != bool(proc_type):
            faults.append(f"{b.name}: Flags 0x{b.flags:x} and ProcType {proc_type} disagree")
        if not proc_type:
            continue
        if proc_type != AXIS_INTERP:
            faults.append(f"{b.name}: ProcType {proc_type} is not axis interpolation")
            continue
        offset = record + struct.unpack_from("<i", d, record + BONE_PROC_INDEX)[0]
        if offset < 0 or offset + AXIS_INTERP_BYTES > len(d):
            faults.append(f"{b.name}: ProcIndex resolves to {offset}, outside the image")
            continue
        control, axis = struct.unpack_from("<ii", d, offset)
        if not 0 <= control < len(bones):
            faults.append(f"{b.name}: control bone {control} outside 0..{len(bones) - 1}")
            continue
        if not 0 <= axis <= 2:
            faults.append(f"{b.name}: axis {axis} outside 0..2")
            continue
        pos = [struct.unpack_from("<3f", d, offset + 8 + 12 * i) for i in range(6)]
        quat = [struct.unpack_from("<4f", d, offset + 80 + 16 * i) for i in range(6)]
        rules.append({
            "bone": b.name, "bone_index": b.index,
            "control": bones[control].name, "control_index": control,
            "axis": list(DRIVER_AXES[axis]),
            "pos": [[float(c) for c in conv_pos(p)] for p in pos],
            "quat": [[float(c) for c in conv_quat_exact(q)] for q in quat],
        })
    return rules, faults


def eye_rig(d, bones, mesh_map, matinfo):
    """The model's `StudioEyeball` records, in the glb's own basis, joined to the eye meshes.

    Two per character model. The record carries the eye's bone and resting basis, the iris
    scale, and the eyelid flexdescs the renderer's eye pass writes back into the flex weights
    after the rules have run -- so it is the authored bridge between the four eyelid rules and
    the lid morphs, not a reconstruction. `StudioMesh.materialtype == 1` flags which meshes the
    pass applies to and `materialparam` says which eyeball, which is how a material name (and
    therefore the `.vmt`'s `$iris` and `$vampire`) reaches a record.

    The bone rides as a **name**: a model with more than one parent-less bone gets a synthetic
    root appended at assembly, so the runtime skeleton's bone order is not the `.mdl`'s.

    `uppertarget`/`lowertarget` stay verbatim -- they are linear offsets read through
    `asin(t / radius)` against a radius in the same units, so converting either alone breaks
    the ratio. Format and the whole system they feed: `docs/vtmb/facial_animation.md`.

    Returns `(rig, faults)`; `rig` is None when the model authors no eyeball.
    """
    faults = []
    eye_mat, seen = {}, {}
    for r in mesh_map:
        seen.setdefault(r["material"], set()).add(r.get("materialtype", 0))
        if r.get("materialtype") != 1:
            continue
        param = r.get("materialparam", 0)
        if param not in (0, 1):
            faults.append(f"eye mesh {r['material']}: materialparam {param} outside 0..1")
            continue
        eye_mat.setdefault(param, r["material"])
    for name, types in seen.items():
        if len(types) > 1:
            faults.append(f"{name}: used by both eye and non-eye meshes, so the primitives merge")

    records = []
    for model_base in dict.fromkeys(r["model_base"] for r in mesh_map):
        for e in S.eyeballs(d, model_base):
            if not 0 <= e["bone"] < len(bones):
                faults.append(f"eyeball {e['index']}: bone {e['bone']} outside 0..{len(bones) - 1}")
                continue
            mat = eye_mat.get(e["index"])
            info = matinfo.get(mat) or {}
            records.append({
                "index": e["index"],
                "bone": bones[e["bone"]].name, "bone_index": e["bone"],
                "org": [float(c) for c in conv_pos(e["org"])],
                "up": [float(c) for c in conv_dir(e["up"])],
                "forward": [float(c) for c in conv_dir(e["forward"])],
                "zoffset": float(e["zoffset"]), "radius": float(e["radius"]),
                "iris_scale": float(e["iris_scale"]),
                "upperflexdesc": e["upperflexdesc"], "lowerflexdesc": e["lowerflexdesc"],
                "uppertarget": [round(t, 6) for t in e["uppertarget"]],
                "lowertarget": [round(t, 6) for t in e["lowertarget"]],
                "upperlidflexdesc": e["upperlidflexdesc"],
                "lowerlidflexdesc": e["lowerlidflexdesc"],
                "material": mdl.sanitize(mat) if mat else None,
                "iris": ("tex/" + info["iris"]) if info.get("iris") else None,
                "vampire": bool(info.get("vampire")),
            })
            if mat is None:
                faults.append(f"eyeball {e['index']}: no mesh carries materialtype 1 for it")

    if not records:
        return None, faults
    if len(records) != 2:
        faults.append(f"{len(records)} eyeball records; every shipped character carries two")
    return {"eyeballs": records,
            "meshes": [{"material": mdl.sanitize(m), "eyeball": p}
                       for p, m in sorted(eye_mat.items())]}, faults


def _append_cloth(bones, surfaces, plan):
    """Append a synthesised garment lattice to the skeleton and move the shell onto it.

    The lattice goes at the **end** of the bone list, which is what keeps every real
    bone's index -- and so its glTF node index, the invariant `_bake_animation` targets
    -- exactly where it was. No real bone is renumbered, reparented or reweighted; only
    shell vertices change, and only their joint/weight pairs.

    `bones` is copied rather than mutated: animation decoding and the procedural rule
    walk both index the model's own bone array through `d`, so they have to keep seeing
    it at its original length. `surfaces` is rewritten in place.

    The planner is passed in by the caller (`enhancement/cloth.py`), so this module
    stays a format reader and gains no dependency on the enhancement layer.
    """
    index_of = {b.name: b.index for b in bones}
    extended = list(bones)
    for lb in plan.bones:
        parent = index_of.get(lb.parent)
        if parent is None:
            raise ValueError(f"cloth bone {lb.name} names unknown parent {lb.parent}")
        index_of[lb.name] = len(extended)
        extended.append(S.Bone(
            index=len(extended), name=lb.name, parent=parent,
            # Source units and xyzw, exactly as a StudioBone carries them, so
            # `_skeleton_nodes` and `_inverse_bind_accessor` convert these the same way
            # they convert every other bone.
            pos=lb.pos, quat=lb.quat,
            # A lattice bone carries no animation channels and no procedural rule, so the
            # decode scales are inert and the flags must stay clear -- `split_bones` and
            # `axis_interp_rules` both select on flags.
            posscale=(1.0, 1.0, 1.0), rotscale=(1.0, 1.0, 1.0, 1.0),
            pose_to_bone=(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0),
            flags=0,
        ))

    for mat, per_vert in plan.weights.items():
        surface = surfaces[mat]
        for vi, terms in per_vert.items():
            js = [index_of[n] for n, _ in terms][:4]
            ws = [float(w) for _, w in terms][:4]
            # glTF JOINTS_0/WEIGHTS_0 are VEC4; a zero weight makes its joint index inert.
            js += [0] * (4 - len(js))
            ws += [0.0] * (4 - len(ws))
            surface["joints"][vi] = js
            surface["weights"][vi] = ws
    return extended


def _build_skinned(g, idx, d, v, model_path, out_dir, anorms=None, cloth_planner=None):
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

    # The garment lattice, when the caller asked for one. `bones` stays the model's own
    # array for everything that indexes back into `d`; `skin_bones` is what the skeleton,
    # the inverse binds and `skin.joints` are built from.
    cloth_plan = cloth_planner(bones, surfaces) if cloth_planner is not None else None
    skin_bones = bones if cloth_plan is None else _append_cloth(bones, surfaces, cloth_plan)

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
    built = dict(bones=bones, skin_bones=skin_bones, cloth=cloth_plan,
                 nodes=nodes, mesh_node=mesh_node, primitives=primitives,
                 ibm_acc=ibm_acc, materials=materials, images=images, textures=textures,
                 surfaces=surfaces, matnames=matnames, mesh_map=mesh_map, target_names=[])
    built["facial"] = (facial_rig(d, _build_morphs(g, d, built, anorms))
                       if anorms and S.flex_descs(d) else None)
    # Independent of the flex rig: 57 of the 59 player bodies carry eyeballs and no flex data
    # at all, so their irises aim while their lids cannot move.
    built["eyes"], built["eye_faults"] = eye_rig(d, bones, mesh_map, matinfo)
    return built


def _assemble_skinned(built, animations):
    """glTF dict for a skinned mesh + skeleton + the given animations."""
    bones = built["bones"]
    # The skin addresses every node the skeleton carries, which on a cloth build is the
    # model's bones plus the appended lattice. Roots come off the model's own array: a
    # lattice bone always parents into it, so it can never introduce a new root.
    skin_bones = built.get("skin_bones") or bones
    nodes = built["nodes"]
    roots = [b.index for b in bones if b.parent == -1]
    # glTFRuntime traverses the skeleton from a SINGLE root down `children`, so any bone not
    # reachable from that root is dropped from its bone map -- a vertex weighted to it then aborts
    # the whole mesh load (and leaves a half-built USkeletalMesh that faults on GC). A few VtMB
    # skeletons have more than one parent-less bone (e.g. regular_cop bones 0/1, prophet bones 0/59),
    # so unify them under one synthetic root. It is appended AFTER the mesh node (preserving the
    # node-index == bone-index invariant _bake_animation targets) and is NOT a joint (skin.joints
    # stays range(len(bones)), so JOINTS_0 values still map 1:1 to the real bones).
    if len(roots) > 1:
        skel_root = len(nodes)
        nodes.append({"name": "__elysium_skeleton_root", "children": list(roots)})
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


def blend_clip_plan(d, clips):
    """Expand a model's sequences into the clips their blend grids need -> (extra, blends).

    A multi-cell sequence selects a different animation per pose-parameter value, and baking
    the base cell alone drops the rest — the male and female `move_and_ranged` `walk` grids
    are nine `walk_0`..`walk_315` animations behind one label. `extra` is one `mdl_skel.Seq`
    per animation an active cell selects that the base-cell bake does not already reach,
    labelled by the animation's own name, so every cell ships as its own clip. `blends` maps a
    sequence label to `{numblends, groupsize, paramindex, paramstart, paramend, cells}`, each
    cell carrying its position, the owner-local animation index it selects, and the clip that
    animation baked as. The index travels because it is the identity a contribution record
    names, so a cell joins back to the model image it was read from.

    **Nothing is blended here.** The grid rides beside the clips because the mix depends on a
    pose parameter the exporter cannot know, and because blending clips is not the same pose
    as blending the transforms they decode to — the host evaluates each cell and mixes the
    results (`docs/vtmb/animation_and_movers.md` A.3).

    A cell whose animation index falls outside the model's declaration resolves to `None`,
    which is a shortfall carried in the sidecar rather than a silently shortened grid."""
    by_base = {}
    taken = set()
    for c in clips:
        by_base.setdefault(c.base, c.label)
        taken.add(c.label.lower())

    extra, blends = [], {}
    for c in clips:
        grid = c.grid
        if len(grid.cells) <= 1:
            continue
        cells = []
        for cell in grid.cells:
            found = S.local_animation(d, cell.anim)
            if found is None:
                cells.append(
                    {"axis": [cell.axis0, cell.axis1], "anim": cell.anim, "clip": None}
                )
                continue
            name, ab, frames, fps = found
            if ab not in by_base:
                # The animation's own name is the cell's clip name. It collides with a
                # sequence label only where content gave a sequence and an animation the
                # same string, so the index disambiguates and the common case reads
                # `walk_45` rather than a synthetic id.
                base_name = name or f"anim{cell.anim}"
                clip, suffix = base_name, 0
                while clip.lower() in taken:
                    suffix += 1
                    clip = (f"{base_name}#{cell.anim}" if suffix == 1
                            else f"{base_name}#{cell.anim}#{suffix}")
                taken.add(clip.lower())
                by_base[ab] = clip
                extra.append(S.Seq(label=clip, base=ab, frames=frames, fps=fps,
                                   activity="", actweight=0, flags=0))
            cells.append(
                {"axis": [cell.axis0, cell.axis1], "anim": cell.anim,
                 "clip": by_base[ab]}
            )
        blends[c.label] = {
            "numblends": grid.numblends,
            "groupsize": list(grid.groupsize),
            "paramindex": list(grid.paramindex),
            "paramstart": [round(v, 4) for v in grid.paramstart],
            "paramend": [round(v, 4) for v in grid.paramend],
            "cells": cells,
        }
    return extra, blends


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


def blend_sidecar(d, blends):
    """The blend table a model ships beside its clips, or `{}` when it authors no grid.

    The pose parameters travel with it because a grid's `paramindex` is an index into this
    model's own array — the axis cannot be named, wrapped or normalized without it."""
    if not blends:
        return {}
    return {
        "pose_parameters": [
            {"index": p.index, "name": p.name, "flags": p.flags,
             "start": round(p.start, 4), "end": round(p.end, 4), "loop": round(p.loop, 4)}
            for p in S.pose_parameters(d)
        ],
        "grids": blends,
    }


def export_npc(idx, model_path, out_dir, stem=None, anorms=None, cloth_planner=None,
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
    `procedural` is the model's `ProcType == 1` rule table in this glb's basis
    (`axis_interp_rules`), which the runtime evaluates after the hierarchy composes.

    `cloth_planner`, when given, is a callable `(bones, surfaces) -> plan | None` that may
    append a synthesised garment lattice (`enhancement/cloth.py`). Left at None -- which is
    every ordinary export -- nothing about the output changes."""
    dv = mdl.load(idx, model_path)
    if not dv:
        raise SystemExit(f"model not found: {model_path}")
    d, v = dv
    stem = stem or mdl.sanitize(os.path.basename(model_path)[:-4])

    g = Gltf()
    built = _build_skinned(g, idx, d, v, model_path, out_dir, anorms, cloth_planner)
    own = S.local_sequences(d)
    extra, blends = blend_clip_plan(d, own)
    animations, labels, extents = [], [], {}
    for c in own + extra:
        anim = _bake_animation(g, d, built["bones"], c.label, c.base, c.frames, c.fps)
        if anim:
            animations.append(anim)
            labels.append(c)
            if measure_extents:
                extents[c.label] = clip_extent(d, built["bones"], c.base, c.frames)
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
    rules, rule_faults = axis_interp_rules(d, built["bones"])
    driven = f", {len(rules)} driven bones" if rules else ""
    grids = f", {len(blends)} blend grids" if blends else ""
    eyes = built["eyes"]
    eyeballs = f", {len(eyes['eyeballs'])} eyeballs" if eyes else ""
    garment = (f", +{len(built['cloth'].bones)} cloth bones"
               if built.get("cloth") else "")
    print(f"  npc {stem}: {len(built['bones'])} bones, {tris} tris, {len(labels)} own clips"
          f"{morphs}{driven}{grids}{eyeballs}{garment}"
          f" -> {glb} ({os.path.getsize(glb) // 1024} KB)")
    for fault in rule_faults:
        print(f"  ! {stem}: procedural rule - {fault}")
    for fault in built["eye_faults"]:
        print(f"  ! {stem}: eyeball - {fault}")
    return dict(stem=stem, glb=os.path.basename(glb), model=model_path,
                bones=len(built["bones"]),
                split_bones=[b.name for b in built["bones"] if b.flags & 0x2],
                clips=labels, clip_extents=extents,
                facial=face, blends=blend_sidecar(d, blends),
                procedural=rules, procedural_faults=rule_faults,
                eyes=eyes, eye_faults=built["eye_faults"],
                cloth=built.get("cloth"))


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
    extra, blends = blend_clip_plan(d, clips)

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
    print(f"  bank {stem}: {len(bones)} bones, {len(labels)} clips{grids} "
          f"-> {glb} ({os.path.getsize(glb) // 1024} KB)")
    return dict(stem=stem, glb="banks/" + os.path.basename(glb), model=model_path,
                clips=labels, blends=blend_sidecar(d, blends))


def _bone_root(name):
    """A bone's root token if it names one of a cinematic model's actor skeletons.

    `Bip01 Spine1` -> `Bip01`; `Dummy01` -> None. The match is on the leading word only, so
    every bone of one actor's skeleton answers the same root.
    """
    head = name.split()[0] if name.split() else name
    low = head.lower()
    if low.startswith("bip") and low[3:].isdigit():
        return head
    return None


def cinematic_roots(bones):
    """The distinct `BipNN` roots a model carries, in first-seen order."""
    roots, seen = [], set()
    for b in bones:
        r = _bone_root(b.name)
        if r and r.lower() not in seen:
            seen.add(r.lower())
            roots.append(r)
    return roots


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
    roots = cinematic_roots(bones)
    if len(roots) <= 1:
        one = export_bank(idx, model_path, out_dir, stem)
        if one:
            one["root"] = roots[0] if roots else None
        return [one] if one else None

    # Blend-grid cells bake as clips here for the reason they do anywhere else; the per-root
    # split below reads them through the same loop, so a cell is a clip on every root.
    extra, blends = blend_clip_plan(d, clips)
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
        sub = [b for b in bones if (_bone_root(b.name) or "").lower() == low]
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
                        clips=labels, blends=blend_sidecar(d, rooted), root=root))

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
    built = _build_skinned(g, idx, d, v, model_path, out_dir, load_anorms())
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
