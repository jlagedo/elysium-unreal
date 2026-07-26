"""VtMB `.mdl` v2531 skeletal decode — bones, skin, and animation tracks.

Companion to `mdl.py` (static geometry). Decodes the animated half documented in
`docs/animation_and_movers.md` Part A: the `StudioBone` skeleton, per-vertex skin
weights, and the `{valid,total}` RLE animation tracks (7 channels/bone: posXYZ +
quat XYZW). Positions/rotations stay in **Source** coordinates (inches, Z-up); the
glTF writer applies the Source->glTF/Godot basis change.

Include-model banks (the shared animation libraries an NPC idles/moves/fights from) are
resolved here: `resolve_tree` walks the studiohdr include tree transitively and
`local_sequences` reads each model's own clips, so the caller can bake an NPC's own clips
and each shared bank's clips into separate glTF assets keyed by bone name (A.7).

Not decoded here: procedural bones (`ProcType`!=0), IK, animation events
(`numevents`/`eventindex`, located but unread), blend spaces (`numblends`>1 — the `[0][0]`
base cell is taken), and root motion (clips bake in place).
"""
import struct
from collections import namedtuple

_i32 = lambda b, o: struct.unpack_from("<i", b, o)[0]
_u16 = lambda b, o: struct.unpack_from("<H", b, o)[0]
_h16 = lambda b, o: struct.unpack_from("<h", b, o)[0]
_f32 = lambda b, o: struct.unpack_from("<f", b, o)[0]
_vec3 = lambda b, o: struct.unpack_from("<3f", b, o)
_quat = lambda b, o: struct.unpack_from("<4f", b, o)


def _cstr(b, o):
    e = b.index(b"\0", o)
    return b[o:e].decode("ascii", "replace")


def _cstr_rel(b, base, field_off):
    """A studio string index stored relative to its own record base. Index 0 means unset
    (Source's convention), which several `StudioSeqDesc.szactivitynameindex` carry."""
    rel = _i32(b, base + field_off)
    return _cstr(b, base + rel) if rel else ""


def _qmul(a, b):
    """Hamilton product of quaternions (x,y,z,w)."""
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw*bx + ax*bw + ay*bz - az*by,
            aw*by - ax*bz + ay*bw + az*bx,
            aw*bz + ax*by - ay*bx + az*bw,
            aw*bw - ax*bx - ay*by - az*bz)


# BONEFLAG_ORIENTATION (bone flag 0x2, always `Bip01 Spine1` on a VtMB biped): the
# bone's animation rotation is stored with a constant 120deg axis-permutation
# post-composed into every keyframe (Q_ORIENT = (-0.5,-0.5,-0.5,0.5)); its bind
# rotation is not. Right-multiplying the decoded animation quaternion by
# Q_ORIENT^-1 = (0.5,0.5,0.5,0.5) recovers the real local rotation. Spine1 parents the
# whole upper body, so leaving it permuted busts the torso + arms (legs branch below).
# Game-confirmed: client.dll FUN_10091110 special-cases bone.flags & 2. See
# docs/animation_and_movers.md A.4a.
BONEFLAG_ORIENTATION = 0x2
_Q_ORIENT_INV = (0.5, 0.5, 0.5, 0.5)


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
    inverse-bind (row-major, 12 floats). flags@136 carries the studio bone flags;
    bit 0x2 (BONEFLAG_ORIENTATION) marks a bone whose animation rotation is stored in
    a swapped axis frame (see mdl_gltf.orient_matrix)."""
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
# a recursive tree (docs/animation_and_movers.md A.7). NumIncludeModels@404 /
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

#: One game-facing sequence. The first four fields are the bake inputs; `activity`,
#: `actweight` and `flags` are the engine's own selection keys (see `local_sequences`).
Seq = namedtuple("Seq", "label base frames fps activity actweight flags")


def local_sequences(d):
    """This model's own game-facing sequences -> list[Seq].

    StudioSeqDesc[NumLocalSeq@272] (stride 764): label@0 (rel. seq base), anim[0][0]@56 (the
    blend grid's base cell) -> a local anim index into LocalAnims. For VtMB NPC/bank models
    the mapping is 1 seq <-> 1 anim (numblends 1), so the grid beyond [0][0] is ignored and a
    label whose base cell is out of range is skipped. Deduped by lowercased label (first wins);
    the label is the name the game references (scripted_sequence `m_iszPlay`, activities).

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
        a0 = _h16(d, sb + 56)
        key = label.lower()
        if not label or key in seen or not (0 <= a0 < na):
            continue
        seen.add(key)
        ab = abase + a0 * 72
        out.append(Seq(label=label, base=ab, frames=_i32(d, ab + 12), fps=_f32(d, ab + 4),
                       activity=_cstr_rel(d, sb, 4), actweight=_i32(d, sb + 16),
                       flags=_i32(d, sb + 8)))
    return out


def resolve_tree(load, model_key):
    """Depth-first include-model resolution with cycle dedup -> ordered [(key, d), ...]: the
    NPC model first, then its banks transitively (docs/animation_and_movers.md A.7).

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
    channel not animated, use bind value). Sample*scale is a delta on the bind."""
    animindex = _i32(d, animdesc_base + 48)
    recs = animdesc_base + animindex
    frames = [[None] * len(bones) for _ in range(numframes)]
    for bi, bone in enumerate(bones):
        rb = recs + bi * 32
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
            if bone.flags & BONEFLAG_ORIENTATION:
                q = _qmul(q, _Q_ORIENT_INV)   # strip the stored axis-permutation
            frames[f][bi] = (pos, q)
    return frames


def decode_skinned(d, v):
    """Decode LOD0 geometry per material, carrying skin + the raw vertex id.

    Like `mdl.decode` but for skinned characters: returns a dict material -> surface
    where surface = {pos:[(x,y,z)], uv:[(u,v)], joints:[[b0..b3]], weights:[[w..]],
    tris:[(i,j,k)]} in **Source** coords, one deduped vertex list per material. Only
    the SKINNED (44B) vertex format occurs on characters; skin comes from the same
    StudioVertex BoneWeight this reads positions from."""
    import mdl
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
            model_base = mbp + model_index + m * 160
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
                mesh_base = model_base + mesh_index + mi * 60
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
