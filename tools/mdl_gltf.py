"""Export a VtMB `.mdl` v2531 skeletal model to glTF 2.0 (.glb) for Godot.

Decodes the skinned mesh (`mdl_skel.decode_skinned`), the `StudioBone` skeleton, and
one named animation, converts them into glTF/Godot space (Y-up, metres, the same
`(x,y,z)->(x,z,-y)*0.0254` basis change the props use), and packs a single `.glb`
with a skin + skeleton node hierarchy + an animation. Godot's glTF importer builds the
`Skeleton3D`/`Skin`/`AnimationPlayer` natively. Materials reference external PNGs
decoded by the shared `mdl._resolve_material` pipeline into `<out>/tex/`.

CLI: python tools/mdl_gltf.py <model-path-in-vpk> <anim-name> [<out_dir>]
"""
import json, struct, os, sys
import numpy as np
import install, mdl, mdl_skel as S

SCALE = 0.0254
# Source->Godot basis M: (x,y,z) -> (x, z, -y), a -90deg rotation about X.
M = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], dtype=np.float64)


def conv_pos(p):
    return (p[0] * SCALE, p[2] * SCALE, -p[1] * SCALE)


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


FLOAT, U8, U16, U32 = 5126, 5121, 5123, 5125
ARRAY_BUFFER, ELEMENT_ARRAY_BUFFER = 34962, 34963


def export(model_path, anim_name, out_dir):
    idx = install.build_index()
    dv = mdl.load(idx, model_path)
    if not dv:
        raise SystemExit(f"model not found: {model_path}")
    d, v = dv
    bones = S.read_bones(d)
    surfaces = S.decode_skinned(d, v)
    found = S.find_anim(d, anim_name)
    if not found:
        raise SystemExit(f"anim not found: {anim_name}")
    ab, nframes, fps = found
    frames = S.read_anim(d, bones, ab, nframes)

    os.makedirs(os.path.join(out_dir, "tex"), exist_ok=True)
    search = mdl.search_paths(d)
    read_bytes = lambda k: install.read(idx, k)
    tex_cache = {}

    g = Gltf()

    # --- skeleton nodes (one per bone) ---
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

    # global bind (glTF space) via FK over converted locals -> inverse bind matrices
    Gbind = [None] * len(bones)
    for b in bones:
        lm = quat_to_mat4(conv_quat(b.quat), conv_pos(b.pos))
        Gbind[b.index] = lm if b.parent == -1 else Gbind[b.parent] @ lm
    ibm = np.array([np.linalg.inv(Gbind[i]).T.reshape(16) for i in range(len(bones))],
                   dtype=np.float32)
    ibm_acc = g.accessor(ibm, FLOAT, "MAT4")

    # --- materials ---
    matnames = list(surfaces.keys())
    images, textures, materials, mat_index = [], [], [], {}
    for mn in matnames:
        albedo, _emis, _add = mdl._resolve_material(mn, search, read_bytes, out_dir, tex_cache)
        # Double-sided like the rest of the project (world/props render CullMode
        # Disabled): VtMB character meshes have open/thin geometry (tank-top neck &
        # armholes, mouth, eye sockets) that shows the culled interior when orbited.
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
    nodes.append({"name": mdl.sanitize(os.path.basename(model_path)),
                  "mesh": 0, "skin": 0})

    # --- animation ---
    animidx = struct.unpack_from("<i", d, ab + 48)[0]
    recs = ab + animidx
    times = np.arange(nframes, dtype=np.float32) / fps
    time_acc = g.accessor(times.reshape(-1, 1), FLOAT, "SCALAR", mm=True)
    channels, samplers = [], []
    for b in bones:
        offs = struct.unpack_from("<7i", d, recs + b.index * 32 + 4)
        rot_anim = any(offs[3:])
        pos_anim = any(offs[:3])
        if rot_anim:
            qout = np.array([conv_quat(frames[f][b.index][1]) for f in range(nframes)],
                            dtype=np.float32)
            si = len(samplers)
            samplers.append({"input": time_acc,
                             "output": g.accessor(qout, FLOAT, "VEC4"),
                             "interpolation": "LINEAR"})
            channels.append({"sampler": si, "target": {"node": b.index, "path": "rotation"}})
        if pos_anim:
            tout = np.array([conv_pos(frames[f][b.index][0]) for f in range(nframes)],
                            dtype=np.float32)
            si = len(samplers)
            samplers.append({"input": time_acc,
                             "output": g.accessor(tout, FLOAT, "VEC3"),
                             "interpolation": "LINEAR"})
            channels.append({"sampler": si, "target": {"node": b.index, "path": "translation"}})

    root = next(b.index for b in bones if b.parent == -1)
    gltf = {
        "asset": {"version": "2.0", "generator": "elysium mdl_gltf"},
        "scene": 0,
        "scenes": [{"nodes": [root, mesh_node]}],
        "nodes": nodes,
        "meshes": [{"primitives": primitives}],
        "skins": [{"inverseBindMatrices": ibm_acc,
                   "joints": list(range(len(bones))), "skeleton": root}],
        "animations": [{"name": anim_name.lstrip("@"),
                        "channels": channels, "samplers": samplers}],
        "materials": materials,
        "accessors": g.accessors,
        "bufferViews": g.bufferViews,
        "buffers": [{"byteLength": len(g.bin)}],
    }
    if images:
        gltf["images"] = images
        gltf["textures"] = textures
        gltf["samplers"] = [{}]
        for t in textures:
            t["sampler"] = 0

    _write_glb(gltf, g.bin, os.path.join(out_dir, mdl.sanitize(
        os.path.basename(model_path)[:-4]) + ".glb"))
    name = mdl.sanitize(os.path.basename(model_path)[:-4])
    print(f"wrote {out_dir}/{name}.glb  ({len(bones)} bones, {len(surfaces)} materials, "
          f"{sum(len(s['tris']) for s in surfaces.values())} tris, "
          f"anim '{anim_name}' {nframes}f @ {fps:.0f}fps)")


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


if __name__ == "__main__":
    model = sys.argv[1] if len(sys.argv) > 1 else \
        "models/character/npc/common/gangmember_male_2/gangmember_male_2.mdl"
    anim = sys.argv[2] if len(sys.argv) > 2 else "patron_barstand"
    out = sys.argv[3] if len(sys.argv) > 3 else "out/skeltest"
    export(model, anim, out)
