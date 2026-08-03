"""VtMB `.mdl` v2531 skeletal decode — bones, skin, animation tracks, and the face.

Companion to `mdl.py` (static geometry). Decodes the animated half documented in
`docs/vtmb/animation_and_movers.md` Part A: the `StudioBone` skeleton, per-vertex skin
weights, and the `{valid,total}` RLE animation tracks (7 channels/bone: posXYZ +
quat XYZW). Positions/rotations stay in **Source** coordinates (inches, Z-up); the
glTF writer applies the Source->glTF basis change.

Include-model banks (the shared animation libraries an NPC idles/moves/fights from) are
resolved here: `resolve_tree` walks the studiohdr include tree transitively and
`local_sequences` reads each model's own clips, so the caller can bake an NPC's own clips
and each shared bank's clips into separate glTF assets keyed by bone name (A.7).

The **facial** half (`docs/vtmb/facial_animation.md`) decodes here too: the studiohdr flex
block (flex descs, the 44 flex controllers, the 60-rule RPN, `mstudiomouth_t`), the
per-mesh `StudioFlex` array, and both `StudioVertAnim` encodings. The compressed record
stores *directions*, not deltas — `read_anorms` pulls the unit-vector table it indexes out
of the user's own `StudioRender.dll`. `probe_facial.py` is the survey over the same
decoders; `mdl_gltf.py` bakes their output into glTF morph targets (roadmap PL10).

Blend spaces are decoded but not evaluated: `read_grid` carries a sequence's extents,
pose-parameter binding and every cell out of the descriptor, and `pose_parameters` reads the
axes those cells are driven by. Each cell stays its own clip — the mix belongs to the host,
because evaluate-then-blend and blend-then-evaluate are not the same pose.

`StudioEyeball` decodes here too: the count/index pair sits at `StudioModel`+192/+196 (not
Source's slot), and 301 models carry two records each — the whole character cast, players
included. The record names the eye's bone, its resting basis, the iris scale, and the eyelid
flexdescs the renderer's eye pass writes back into the flex weights; `StudioMesh.materialtype`
flags which meshes it applies to.

Not decoded here: procedural bones (`ProcType`!=0), IK, animation events
(`numevents`/`eventindex`, located but unread), and root motion (clips bake in place).
"""
import os
import struct
from collections import namedtuple

_i32 = lambda b, o: struct.unpack_from("<i", b, o)[0]
_u16 = lambda b, o: struct.unpack_from("<H", b, o)[0]
_h16 = lambda b, o: struct.unpack_from("<h", b, o)[0]
_f32 = lambda b, o: struct.unpack_from("<f", b, o)[0]
_vec3 = lambda b, o: struct.unpack_from("<3f", b, o)
_quat = lambda b, o: struct.unpack_from("<4f", b, o)

#: `StudioModel`, measured off every multi-model bodypart in the install (the flex walk
#: forced the correction; `docs/vtmb/facial_animation.md`). Only a bodygroup's second and later
#: models read differently from the older 160 — 4,423 of 4,444 models have one model per
#: bodypart, and no VtMB character does.
MODEL_STRIDE = 224
MESH_STRIDE = 60
#: `StudioEyeball`. Fixed by its consumer: the renderer's eye pass resolves a record as
#: `model_base + *(int*)(model_base + 0xC4) + materialparam * 0x8C`.
EYEBALL_STRIDE = 140


def _cstr(b, o):
    e = b.index(b"\0", o)
    return b[o:e].decode("ascii", "replace")


def _cstr_rel(b, base, field_off):
    """A studio string index stored relative to its own record base. Index 0 means unset
    (Source's convention), which several `StudioSeqDesc.szactivitynameindex` carry."""
    rel = _i32(b, base + field_off)
    return _cstr(b, base + rel) if rel else ""


class Bone:
    __slots__ = ("index", "name", "parent", "pos", "quat", "posscale", "rotscale",
                 "pose_to_bone", "flags")

    def __init__(self, **kw):
        for k, v in kw.items():
            setattr(self, k, v)


def read_bones(d):
    """StudioBone[NumBones] (160B each) -> list[Bone] in header order.

    pos/quat are parent-relative bind pose (Source inches / xyzw). posscale/rotscale
    multiply the animation short deltas. pose_to_bone is the 3x4 world->bone
    inverse-bind (row-major, 12 floats). flags@136 carries the studio bone flags. It
    does not alter v2531 animation-channel decoding; the retail quaternion evaluator
    never reads it (see `docs/vtmb/animation_and_movers.md` A.4a)."""
    n = _i32(d, 240)
    base = _i32(d, 244)
    bones = []
    for i in range(n):
        b = base + i * 160
        bones.append(Bone(
            index=i,
            name=_cstr(d, b + _i32(d, b)),
            parent=_i32(d, b + 4),
            pos=_vec3(d, b + 32),
            quat=_quat(d, b + 44),
            posscale=_vec3(d, b + 60),
            rotscale=_quat(d, b + 72),
            pose_to_bone=struct.unpack_from("<12f", d, b + 88),
            flags=_i32(d, b + 136),
        ))
    return bones


def find_anim(d, name):
    """Locate a local anim by name -> (animdesc_base, numframes, fps). Names carry a
    leading '@'; match with or without it."""
    n = _i32(d, 264)
    base = _i32(d, 268)
    want = name.lstrip("@").lower()
    for i in range(n):
        ab = base + i * 72
        nm = _cstr(d, ab + _i32(d, ab)).lstrip("@").lower()
        if nm == want:
            return ab, _i32(d, ab + 12), _f32(d, ab + 4)
    return None


# --- include-model banks (shared animation libraries) -----------------------------
# An NPC .mdl carries only its own clips (mostly dialogue); locomotion, combat and idle
# come from shared banks pulled in by the studiohdr include-model mechanism, which forms
# a recursive tree (docs/vtmb/animation_and_movers.md A.7). NumIncludeModels@404 /
# IncludeModelIndex@408 -> StudioModelGroup[] (stride 116: int FilenameIndex@0 relative to
# the group-entry base, int LabelIndex@4, int Filler[27]). Every bank bone name is present
# in the NPC's Biped skeleton, so a clip decoded on its owning model's own bones plays on
# any NPC by name-matching the tracks to the skeleton at load time (glTFRuntime does this).
_MODELGROUP_STRIDE = 116


def read_includes(d):
    """Direct include-model paths (StudioModelGroup[NumIncludeModels]@404), each normalized
    to a `models/`-rooted, forward-slashed key. One level deep -- walk transitively via
    `resolve_tree`."""
    n = _i32(d, 404)
    base = _i32(d, 408)
    out = []
    for i in range(n):
        gb = base + i * _MODELGROUP_STRIDE
        p = _cstr(d, gb + _i32(d, gb)).replace("\\", "/")
        if p:
            out.append(p if p.startswith("models/") else "models/" + p)
    return out


_SEQDESC_STRIDE = 764

#: `MAXSTUDIOBLENDS`. The inline `anim[16][16]` grid keeps this row stride whatever the
#: authored extents are, so an authored grid is a sub-rectangle of the fixed array.
MAXSTUDIOBLENDS = 16

_POSEPARAM_STRIDE = 20

#: One `mstudioposeparamdesc_t` (`NumLocalPoseParameters`@384 / `LocalPoseParamIndex`@388).
#: A non-zero `loop` is the wrap modulus an axis folds the parameter through before it
#: normalizes over `start`..`end`.
PoseParam = namedtuple("PoseParam", "index name flags start end loop")

#: One authored blend-grid cell: its position on the two axes and the local animation index
#: it selects. `anim` is read verbatim and may fall outside `NumLocalAnims`.
Cell = namedtuple("Cell", "axis0 axis1 anim")

#: One sequence's blend space. `numblends`@52 is the declared cell count and `groupsize`@572
#: the two axis extents; `paramindex`@580 names the pose parameter driving each axis (-1 when
#: unused) and `paramstart`@588 / `paramend`@596 give that axis's range in the parameter's own
#: units. `cells` is every cell inside the extents, row-major over axis 0.
Grid = namedtuple("Grid", "numblends groupsize paramindex paramstart paramend cells")

#: What a `Seq` built outside `local_sequences` carries — a raw animation baked as a clip
#: answers to no descriptor, so it declares no cells rather than claiming a base cell.
_NO_GRID = Grid(numblends=1, groupsize=(1, 1), paramindex=(-1, -1),
                paramstart=(0.0, 0.0), paramend=(0.0, 0.0), cells=())

#: One game-facing sequence. The first four fields are the bake inputs; `activity`,
#: `actweight` and `flags` are the engine's own selection keys (see `local_sequences`);
#: `grid` is the blend space the label names (see `read_grid`).
Seq = namedtuple("Seq", "label base frames fps activity actweight flags grid",
                 defaults=(_NO_GRID,))


def pose_parameters(d):
    """This model's pose parameters -> list[PoseParam] in header order.

    `NumLocalPoseParameters`@384 / `LocalPoseParamIndex`@388 address 20-byte records:
    `nameindex`@0 (relative to the record), `flags`@4, `start`@8, `end`@12, `loop`@16. The
    name is the durable key — a sequence axis binds by *index* into this array, so the two
    are read together or an axis cannot be named (`docs/vtmb/animation_and_movers.md` A.3)."""
    n = _i32(d, 384)
    base = _i32(d, 388)
    out = []
    for i in range(n):
        pb = base + i * _POSEPARAM_STRIDE
        out.append(PoseParam(index=i, name=_cstr_rel(d, pb, 0), flags=_i32(d, pb + 4),
                             start=_f32(d, pb + 8), end=_f32(d, pb + 12),
                             loop=_f32(d, pb + 16)))
    return out


def read_grid(d, sb):
    """One StudioSeqDesc's blend space at descriptor base `sb` -> Grid.

    `groupsize`@572 gives the extents and `numblends`@52 the cell count; the product of the
    two extents equals `numblends` on all 294 multi-blend sequences of the installed character
    tree, which is what establishes these as the axis extents. Extents outside 1..16 (or a
    product disagreeing with `numblends`) fall back to the base cell alone, so a descriptor
    the format cannot explain yields the clip it always did rather than a grid of noise.

    A cell sits at `56 + (i0 * 16 + i1) * 2` — axis 0 takes the fixed 16-short row stride and
    axis 1 the column. That orientation is retail's own: on a 3x3 `smith_aim_layer` the
    witnessed cell `[1, 0]` decodes the four animations at rows 1-2, columns 0-1, and the
    transposed address would name a different four.

    `paramindex`@580 binds each axis to a `pose_parameters` index and is -1 when the axis is
    unused; `paramstart`@588 / `paramend`@596 are that axis's range in the parameter's units."""
    numblends = _i32(d, sb + 52)
    groupsize = struct.unpack_from("<2i", d, sb + 572)
    paramindex = struct.unpack_from("<2i", d, sb + 580)
    grid = Grid(numblends=numblends, groupsize=groupsize, paramindex=paramindex,
                paramstart=struct.unpack_from("<2f", d, sb + 588),
                paramend=struct.unpack_from("<2f", d, sb + 596),
                cells=())
    n0, n1 = groupsize
    if not (1 <= n0 <= MAXSTUDIOBLENDS and 1 <= n1 <= MAXSTUDIOBLENDS) or n0 * n1 != numblends:
        n0 = n1 = 1
    cells = tuple(
        Cell(axis0=i0, axis1=i1,
             anim=_h16(d, sb + 56 + (i0 * MAXSTUDIOBLENDS + i1) * 2))
        for i0 in range(n0) for i1 in range(n1)
    )
    return grid._replace(cells=cells)


def local_animation(d, index):
    """The local animation at `index` -> (name, animdesc_base, numframes, fps), or None when
    the index falls outside `NumLocalAnims`@264. The name carries a leading '@' on the disk
    bytes; it is stripped here, exactly as `find_anim` matches."""
    n = _i32(d, 264)
    if not (0 <= index < n):
        return None
    ab = _i32(d, 268) + index * 72
    return _cstr(d, ab + _i32(d, ab)).lstrip("@"), ab, _i32(d, ab + 12), _f32(d, ab + 4)


def local_sequences(d):
    """This model's own game-facing sequences -> list[Seq].

    StudioSeqDesc[NumLocalSeq@272] (stride 764): label@0 (rel. seq base), anim[0][0]@56 (the
    blend grid's base cell) -> a local anim index into LocalAnims. A label whose base cell is
    out of range is skipped. Deduped by lowercased label (first wins); the label is the name
    the game references (scripted_sequence `m_iszPlay`, activities).

    `base`/`frames`/`fps` are the base cell's, so a single-cell sequence bakes as it always
    did. `grid` carries the whole blend space beside it — extents, pose-parameter binding and
    every cell — because a 9x1 walk grid selects a different animation per `move_yaw` and
    baking the base cell as the clip drops the other eight. The cells are data beside the
    clips: blending them is the host's job, not the exporter's.

    Three more fields carry how the *engine* picks a sequence, rather than how content names
    one. `szactivitynameindex`@4 is the activity literal (`ACT_IDLE`, `ACT_WALK`,
    `ACT_DISPOSITION`, ...), empty on a layer/plumbing sequence; `actweight`@16 is the
    weighted-random share among the sequences sharing an activity (`claws_aggressive_run` 7 vs
    its two alts at 3); `flags`@8 carries the studio sequence bits. The sibling `activity`@12
    int stays -1 on disk — the game DLL resolves the name to an enum at model load, so the
    *name* is the durable key. `numevents`/`eventindex`@20/24 are located but not decoded."""
    ns = _i32(d, 272); sbase = _i32(d, 276)
    na = _i32(d, 264); abase = _i32(d, 268)
    out, seen = [], set()
    for i in range(ns):
        sb = sbase + i * _SEQDESC_STRIDE
        label = _cstr_rel(d, sb, 0)
        # Read before the skips, so every declared descriptor's grid is read off the same
        # walk that reads its label rather than only the ones that survive the dedup.
        grid = read_grid(d, sb)
        a0 = grid.cells[0].anim
        key = label.lower()
        if not label or key in seen or not (0 <= a0 < na):
            continue
        seen.add(key)
        ab = abase + a0 * 72
        out.append(Seq(label=label, base=ab, frames=_i32(d, ab + 12), fps=_f32(d, ab + 4),
                       activity=_cstr_rel(d, sb, 4), actweight=_i32(d, sb + 16),
                       flags=_i32(d, sb + 8), grid=grid))
    return out


def resolve_tree(load, model_key):
    """Depth-first include-model resolution with cycle dedup -> ordered [(key, d), ...]: the
    NPC model first, then its banks transitively (docs/vtmb/animation_and_movers.md A.7).

    `load(key)` returns the model's raw `.mdl` bytes (or None if it cannot be read). A model
    reachable by several include paths (frenzy / pc_idles) is visited exactly once, so the
    order also fixes clip ownership: the first model that defines a label owns it."""
    order, seen = [], set()

    def visit(key):
        k = key.lower()
        if k in seen:
            return
        seen.add(k)
        d = load(key)
        if d is None:
            return
        order.append((key, d))
        for inc in read_includes(d):
            visit(inc)

    visit(model_key)
    return order


def bone_names(d):
    """Bone names in header order (for name-keyed retarget diagnostics)."""
    n = _i32(d, 240); base = _i32(d, 244)
    return [_cstr(d, base + i * 160 + _i32(d, base + i * 160)) for i in range(n)]


def _rle_channel(v, p, numframes):
    """Decode one `mstudioanimvalue_t` RLE channel into a per-frame value list.

    Each run: byte valid, byte total, then `valid` int16 keys. Frames [0,valid) take
    the explicit keys; [valid,total) clamp to the last key. Returns numframes shorts."""
    out = []
    remaining = numframes
    while remaining > 0:
        valid = v[p]
        total = v[p + 1]
        p += 2
        keys = struct.unpack_from(f"<{valid}h", v, p)
        p += 2 * valid
        for f in range(total):
            out.append(keys[min(f, valid - 1)])
        remaining -= total
    return out[:numframes]


def read_anim(d, bones, animdesc_base, numframes):
    """Decode an animation into per-frame per-bone (pos3, quat4) in Source coords.

    At animdesc_base + animindex(@48): one 32B record per bone (weight@0, offset[7]@4
    = posX,posY,posZ,rotX,rotY,rotZ,rotW, each relative to the record start; 0 =
    channel not animated, use bind value). Sample*scale is a delta on the bind.

    `weight`@0 is a zero test rather than a factor. Both retail channel decoders compare it
    against zero before anything else and, on zero, write a zero position and a zero
    quaternion and return — reading no channel offset, no track and no bind field. So a
    zero-weight bone yields exact zeros here too, not the bind pose and not an identity
    rotation. No record of the retail capture corpus carries anything but 1.0, so the branch
    is taken from the decompiled decoders and is exercised only by a synthetic regression;
    it is not validated against retail bytes."""
    animindex = _i32(d, animdesc_base + 48)
    recs = animdesc_base + animindex
    frames = [[None] * len(bones) for _ in range(numframes)]
    for bi, bone in enumerate(bones):
        rb = recs + bi * 32
        if _f32(d, rb) == 0.0:
            for f in range(numframes):
                frames[f][bi] = ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 0.0))
            continue
        offs = struct.unpack_from("<7i", d, rb + 4)
        chan = []
        for c in range(7):
            if offs[c]:
                chan.append(_rle_channel(d, rb + offs[c], numframes))
            else:
                chan.append(None)
        px, py, pz, qx, qy, qz, qw = chan
        bpos, bq = bone.pos, bone.quat
        ps, rs = bone.posscale, bone.rotscale
        for f in range(numframes):
            # position: sample*scale is a DELTA on the bind position
            pos = (
                bpos[0] + (px[f] * ps[0] if px else 0.0),
                bpos[1] + (py[f] * ps[1] if py else 0.0),
                bpos[2] + (pz[f] * ps[2] if pz else 0.0),
            )
            # rotation: each quaternion component is `sample*rotscale` when its channel
            # is animated, else the **bind** component (not 0). A channel with no offset
            # means that axis isn't animated, so it holds its bind value — filling it with
            # 0 collapses a bone whose bind carries a real rotation (the clavicles, whose
            # animations often store only W) onto identity, throwing the arm/upper body.
            # Where the bind component is ~0 (most limb bones) this equals a 0 fill, which
            # is why it only shows on bones with a large bind rotation.
            q = (
                qx[f] * rs[0] if qx else bq[0],
                qy[f] * rs[1] if qy else bq[1],
                qz[f] * rs[2] if qz else bq[2],
                qw[f] * rs[3] if qw else bq[3],
            )
            n = (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]) ** 0.5 or 1.0
            q = (q[0] / n, q[1] / n, q[2] / n, q[3] / n)
            frames[f][bi] = (pos, q)
    return frames


def decode_skinned(d, v, mesh_map=None):
    """Decode LOD0 geometry per material, carrying skin + the raw vertex id.

    Like `mdl.decode` but for skinned characters: returns a dict material -> surface
    where surface = {pos:[(x,y,z)], uv:[(u,v)], joints:[[b0..b3]], weights:[[w..]],
    tris:[(i,j,k)]} in **Source** coords, one deduped vertex list per material. Only
    the SKINNED (44B) vertex format occurs on characters; skin comes from the same
    StudioVertex BoneWeight this reads positions from.

    `mesh_map`, when given a list, is filled with one record per StudioMesh:
    `{material, model_base, mesh_index, vertex_offset, remap}` where `remap` maps the
    model-global vertex id to this material's surface index. That is the join the flex
    bake needs — a `StudioVertAnim` addresses a mesh-local vertex, and the surface
    dedupes across meshes, so nothing else can line the two up."""
    from elysium_pipeline.formats import mdl
    materials = []
    ntex = _i32(d, 292); tex_index = _i32(d, 296)
    for i in range(ntex):
        to = tex_index + i * 20
        materials.append(_cstr(d, to + _i32(d, to)))
    num_bp = _i32(d, 320); bp_index = _i32(d, 324)
    vtx_bp_off = _i32(v, 32)
    surfaces = {}
    for bp in range(num_bp):
        mbp = bp_index + bp * 16
        num_models = _i32(d, mbp + 4)
        model_index = _i32(d, mbp + 12)
        vbp = vtx_bp_off + bp * 8
        vtx_model_off = _i32(v, vbp + 4)
        for m in range(num_models):
            model_base = mbp + model_index + m * MODEL_STRIDE
            num_meshes = _i32(d, model_base + 136)
            mesh_index = _i32(d, model_base + 140)
            num_verts = _i32(d, model_base + 144)
            vertex_index = _i32(d, model_base + 148)
            vlist = _i32(d, model_base + 156)
            vstride = mdl.VSTRIDE.get(vlist, 44)
            skin = read_skin(d, model_base, vertex_index, num_verts)
            vmodel = vbp + vtx_model_off + m * 8
            vlod = vmodel + _i32(v, vmodel + 4)
            vtx_mesh_off = _i32(v, vlod + 4)
            for mi in range(num_meshes):
                mesh_base = model_base + mesh_index + mi * MESH_STRIDE
                material = _i32(d, mesh_base + 0)
                mesh_numverts = _i32(d, mesh_base + 8)
                vertex_offset = _i32(d, mesh_base + 12)
                vmesh = vlod + vtx_mesh_off + mi * 8
                num_sg = _u16(v, vmesh + 0)
                sg_off = _i32(v, vmesh + 4)
                matname = materials[material] if material < len(materials) else f"mat{material}"
                surf = surfaces.setdefault(matname, dict(pos=[], uv=[], joints=[],
                                                         weights=[], tris=[]))
                remap = {}
                for sg in range(num_sg):
                    sgb = vmesh + sg_off + sg * mdl.STRIPGROUP_STRIDE
                    sg_numverts = _u16(v, sgb + 0)
                    numstrips = _u16(v, sgb + 4)
                    vtable = sgb + _i32(v, sgb + 8)
                    itable = sgb + _i32(v, sgb + 12)
                    strip_off = _i32(v, sgb + 16)
                    form = mdl._vtable_form(v, vtable, sg_numverts, mesh_numverts)
                    vt_stride, vt_off = form
                    for s in range(numstrips):
                        sh = sgb + strip_off + s * 16
                        num_indices = _u16(v, sh + 0)
                        strip_index_off = _u16(v, sh + 2)
                        for k in range(0, num_indices, 3):
                            tri = []
                            for j in range(3):
                                local = _u16(v, itable + (strip_index_off + k + j) * 2)
                                gvid = vertex_offset + _u16(v, vtable + local * vt_stride + vt_off)
                                if gvid not in remap:
                                    remap[gvid] = len(surf["pos"])
                                    sv = model_base + vertex_index + gvid * vstride
                                    px, py, pz, u, vv = mdl._read_vertex(d, sv, vlist, None, None)
                                    bs, ws = skin[gvid]
                                    j4 = (bs + [0, 0, 0, 0])[:4]
                                    w4 = (ws + [0.0, 0.0, 0.0, 0.0])[:4]
                                    surf["pos"].append((px, py, pz))
                                    surf["uv"].append((u, vv))
                                    surf["joints"].append(j4)
                                    surf["weights"].append(w4)
                                tri.append(remap[gvid])
                            surf["tris"].append(tuple(tri))
                if mesh_map is not None:
                    # `materialtype`@24 is 1 on an eyeball mesh and `materialparam`@28 selects
                    # which eyeball; the draw loop dispatches its eye path on that field alone.
                    mesh_map.append(dict(material=matname, model_base=model_base,
                                         mesh_index=mi, vertex_offset=vertex_offset,
                                         materialtype=_i32(d, mesh_base + 24),
                                         materialparam=_i32(d, mesh_base + 28),
                                         remap=remap))
    return surfaces


def read_skin(d, model_base, vertex_index, num_vertices):
    """Per-vertex skin from the SKINNED StudioVertex BoneWeight (44B verts).

    BoneWeight @ vert+0: byte Weight[3], short Bone[3]@4, byte NumBones@10 (unused on
    VtMB — derive from nonzero weights). Returns [(bones3, weights3), ...] with
    weights normalized to sum 1."""
    out = []
    for i in range(num_vertices):
        sv = model_base + vertex_index + i * 44
        w = struct.unpack_from("<3B", d, sv + 0)
        bn = struct.unpack_from("<3h", d, sv + 4)
        infl = [(bn[k], w[k]) for k in range(3) if w[k] > 0]
        if not infl:
            infl = [(bn[0], 255)]
        s = sum(x[1] for x in infl) or 1
        out.append(([b for b, _ in infl], [wt / s for _, wt in infl]))
    return out


# --- the face (docs/vtmb/facial_animation.md) ------------------------------------------
# The studiohdr facial block starts at 344 -- eight bytes past a naive VAMPTools field
# walk, which drops two ints between LocalAttachmentIndex@332 and NumFlexDescs. These are
# the offsets every array closes on (count x stride exactly, on all 4,444 models).
H_NUM_FLEXDESC, H_FLEXDESC = 344, 348
H_NUM_FLEXCTRL, H_FLEXCTRL = 352, 356
H_NUM_FLEXRULE, H_FLEXRULE = 360, 364
H_NUM_MOUTH, H_MOUTH = 376, 380

FLEX_STRIDE = 32            # StudioFlex
VA_STRIDE = {1: 8, 0: 20}   # StudioFlex.VertAnimType -> StudioVertAnim stride

#: Source's `StudioFlexOp_t`. Only codes 1-7 appear across the 201 rigged VtMB models.
FLEX_OPS = {1: "CONST", 2: "FETCH1", 3: "FETCH2", 4: "ADD", 5: "SUB", 6: "MUL",
            7: "DIV", 8: "NEG", 9: "EXP", 10: "OPEN", 11: "CLOSE", 12: "COMMA",
            13: "MAX", 14: "MIN"}

# StudioRender.dll (imagebase 0x2C000000): the 5,314-entry unit-vector table a compressed
# vertex-animation record indexes by BYTE offset, and the two magnitude scales
# (`8.0` at 0x2C06C4FC for position, the `FADD ST0,ST0` doubling on the normal path).
ANORM_VA = 0x2C06E008
ANORM_COUNT = 5314
DELTA_SCALE = 8.0
NDELTA_SCALE = 2.0


def read_anorms(dll_path=None):
    """The unit-vector table out of the user's own `Bin/StudioRender.dll` -> [(x,y,z)] in
    Source space, indexed by `u16 // 12`.

    It is compiled into the renderer, not shipped as data, so it is game-derived like every
    other VtMB byte: extracted on demand at export time, never committed. Raises if the DLL
    is absent or the address falls outside every section."""
    if dll_path is None:
        from elysium_pipeline.formats import install
        dll_path = os.path.join(install.GAME_ROOT, "Bin", "StudioRender.dll")
    with open(dll_path, "rb") as f:
        data = f.read()
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, pe + 6)[0]
    optsz = struct.unpack_from("<H", data, pe + 20)[0]
    imgbase = struct.unpack_from("<I", data, pe + 24 + 28)[0]
    base = pe + 24 + optsz
    for i in range(nsec):
        s = base + i * 40
        va = imgbase + struct.unpack_from("<I", data, s + 12)[0]
        span = max(struct.unpack_from("<I", data, s + 8)[0],
                   struct.unpack_from("<I", data, s + 16)[0])
        if va <= ANORM_VA < va + span:
            off = struct.unpack_from("<I", data, s + 20)[0] + (ANORM_VA - va)
            return [struct.unpack_from("<3f", data, off + i * 12) for i in range(ANORM_COUNT)]
    raise RuntimeError(f"{dll_path}: no section holds {ANORM_VA:#x}")


def flex_descs(d):
    """`mstudioflexdesc_t[NumFlexDescs]` (4 B) -> the FACS names, index = flexdesc id.
    Every rigged VtMB character ships the same 65 in the same order."""
    n, base = _i32(d, H_NUM_FLEXDESC), _i32(d, H_FLEXDESC)
    return [_cstr_rel(d, base + i * 4, 0) for i in range(n)]


def flex_controllers(d):
    """`mstudioflexcontroller_t[NumFlexControllers]` (20 B) -> [(type, name, min, max)].

    Both string indices are relative to the record base. `localToGlobal`@8 is -1 on disk
    (the engine remaps it at load), so it is not carried. The 44 shipped controllers group
    by type into eyelid/brow/nose/mouth/phoneme."""
    n, base = _i32(d, H_NUM_FLEXCTRL), _i32(d, H_FLEXCTRL)
    out = []
    for i in range(n):
        b = base + i * 20
        out.append((_cstr_rel(d, b, 0), _cstr_rel(d, b, 4), _f32(d, b + 12), _f32(d, b + 16)))
    return out


def flex_rules(d):
    """`mstudioflexrule_t[NumFlexRules]` (12 B + an 8 B op array) -> [(flexdesc, ops)].

    An op is `(opname, int_operand, float_operand)`; the same 4 bytes read both ways, since
    `CONST` carries a float and `FETCH1`/`FETCH2` a controller index. The stack machine
    evaluates left to right and its final value is the weight of the rule's flexdesc."""
    n, base = _i32(d, H_NUM_FLEXRULE), _i32(d, H_FLEXRULE)
    out = []
    for i in range(n):
        b = base + i * 12
        numops, opidx = _i32(d, b + 4), _i32(d, b + 8)
        ops = []
        for q in range(numops):
            ob = b + opidx + q * 8
            code = _i32(d, ob)
            ops.append((FLEX_OPS.get(code, f"op{code}"), _i32(d, ob + 4), _f32(d, ob + 4)))
        out.append((_i32(d, b), ops))
    return out


def mouths(d):
    """`mstudiomouth_t[NumMouths]` (20 B) -> [(bone, forward, flexdesc)]. One per rigged
    character, pointing at the `mouth` flexdesc: Source's audio-amplitude jaw, driven by the
    envelope of the line rather than by the phoneme track."""
    n, base = _i32(d, H_NUM_MOUTH), _i32(d, H_MOUTH)
    return [(_i32(d, base + i * 20), _vec3(d, base + i * 20 + 4), _i32(d, base + i * 20 + 16))
            for i in range(n)]


def eyeballs(d, model_base):
    """`StudioEyeball[NumEyeballs]` (140 B) for one `StudioModel` -> [dict].

    `NumEyeballs`@192 / `EyeballIndex`@196, the latter relative to the model record. Two per
    character model, mirrored in Z.

    `uppertarget`/`lowertarget` are **linear offsets in eyeball units, not angles** — the
    renderer takes `asin(target / radius)`. Reading them as radians still moves a lid, which
    is what makes the error easy to keep.

    `texture`/`iris_material`/`glint_material` hold real texture-table indices but the renderer
    reads none of them (Source's `texture`/`unused1`/`unused2`); the bound material is the eye
    mesh's own skinref and the iris comes from the `.vmt`'s `$iris`. They are carried for
    diagnostics only. The 16 bytes at +124 are read by nothing, so there is no data-driven
    gaze limit."""
    n, rel = _i32(d, model_base + 192), _i32(d, model_base + 196)
    out = []
    for i in range(max(n, 0)):
        b = model_base + rel + i * EYEBALL_STRIDE
        out.append(dict(
            index=i, bone=_i32(d, b + 4),
            org=_vec3(d, b + 8), zoffset=_f32(d, b + 20), radius=_f32(d, b + 24),
            up=_vec3(d, b + 28), forward=_vec3(d, b + 40),
            iris_scale=_f32(d, b + 60),
            upperflexdesc=[_i32(d, b + 68 + 4 * q) for q in range(3)],
            lowerflexdesc=[_i32(d, b + 80 + 4 * q) for q in range(3)],
            uppertarget=[_f32(d, b + 92 + 4 * q) for q in range(3)],
            lowertarget=[_f32(d, b + 104 + 4 * q) for q in range(3)],
            upperlidflexdesc=_i32(d, b + 116), lowerlidflexdesc=_i32(d, b + 120),
            # unread by the renderer; kept so a survey can resolve the eye's texture names
            texture=_i32(d, b + 52), iris_material=_i32(d, b + 56),
            glint_material=_i32(d, b + 64)))
    return out


def mesh_flexes(d, model_base, mesh_index):
    """One `StudioMesh`'s `StudioFlex[]` -> [{flexdesc, targets[4], numverts, vertanimtype}].

    `StudioMesh.NumFlexes`@16 / `FlexIndex`@20, the latter relative to the mesh record;
    `base` is kept because `StudioFlex.VertIndex` is in turn relative to the flex record.
    `targets` is the trapezoid the flexdesc's weight is remapped through at draw time — a
    runtime remap, so it rides in the manifest rather than being baked into the morph."""
    mshb = model_base + _i32(d, model_base + 140) + mesh_index * MESH_STRIDE
    n, rel = _i32(d, mshb + 16), _i32(d, mshb + 20)
    out = []
    for i in range(max(n, 0)):
        b = mshb + rel + i * FLEX_STRIDE
        out.append(dict(base=b, flexdesc=_i32(d, b),
                        targets=[_f32(d, b + 4 + 4 * q) for q in range(4)],
                        numverts=_i32(d, b + 20), vertindex=_i32(d, b + 24),
                        vertanimtype=_i32(d, b + 28)))
    return out


def vert_anims(d, flex, anorms=None):
    """One flex's `StudioVertAnim[]` -> [(mesh-local vertex, delta, ndelta)] in Source inches.

    Type 1 (8 B, 17,846 of 17,960 flexes) stores **directions, not deltas**: two u16 byte
    offsets into `anorms` plus two `n/255` magnitudes, scaled by 8.0 (position) and 2.0
    (normal). Type 0 (20 B, only `mingxiao_transformation`) stores the position delta as a
    plain Vector, past the 8-inch ceiling the compressed form can reach. Without `anorms`
    only the vertex indices come back."""
    stride = VA_STRIDE.get(flex["vertanimtype"])
    if stride is None:
        return []
    out = []
    for q in range(flex["numverts"]):
        vb = flex["base"] + flex["vertindex"] + q * stride
        if vb + stride > len(d):
            break
        idx = _u16(d, vb)
        if flex["vertanimtype"] == 1:
            if anorms is None:
                out.append((idx, None, None))
                continue
            dm = d[vb + 6] / 255.0 * DELTA_SCALE
            nm = d[vb + 7] / 255.0 * NDELTA_SCALE
            dv = anorms[_u16(d, vb + 2) // 12]
            nv = anorms[_u16(d, vb + 4) // 12]
            out.append((idx, tuple(c * dm for c in dv), tuple(c * nm for c in nv)))
        else:
            nv = None
            if anorms is not None:
                nm = d[vb + 18] / 255.0 * NDELTA_SCALE
                nv = tuple(c * nm for c in anorms[_u16(d, vb + 16) // 12])
            out.append((idx, _vec3(d, vb + 4), nv))
    return out
