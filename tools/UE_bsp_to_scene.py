"""Export a VtMB BSP to a textured OBJ+MTL scene with decoded PNG textures.

UE_* exporter: verified to emit **Unreal-native** intermediates directly — all
geometry and sidecars are in Unreal space (centimetres, Z-up, left-handed), with
triangle winding pre-reversed, so the C++ runtime reads every file 1:1 with no
coordinate conversion. There is no Godot legacy in this file. (Exporters without the
UE_ prefix still emit the old Godot Y-up/metres space and are flagged for review.)

Pulls together everything we reverse-engineered:
  * geometry  : v17 dface_t (104B) -> surfedges -> edges -> vertexes
  * materials : face -> texinfo(72B) -> texdata(32B) -> string table -> name
  * UVs       : planar projection of each vertex onto texinfo s/t axes
  * textures  : material name -> VMT ($basetexture) -> TTH/TTZ -> DXT -> PNG

TOOLS/* faces (nodraw, clip, trigger, skybox, hint...) are skipped - they are
invisible engine surfaces, not geometry to draw.
"""
import struct, os, re, sys, json
import numpy as np
import install, vmt
import bsp as B
import mdl as MDL
import retex_dds
from tex_to_png import decode as decode_texture, decode_cubemap
from bsp import (read_lump, source_to_unreal, source_dir_to_unreal, source_angles_to_unreal_quat,
                 strings_from_blob,
                 read_pakfile, read_game_lump, INCH_TO_CM, FACE_SIZE, FE_OFS, NE_OFS,
                 TI_OFS, DISP_OFS, TEXINFO_SIZE, TI_SAXIS, TI_TAXIS, TI_TEXDATA,
                 TEXDATA_SIZE, TD_NAMEID, TD_W, TD_H)

# --- brush collision: convex hulls from the world model's brushes -----------
# CONTENTS flags that block the player: SOLID|WINDOW|GRATE|MOVEABLE|PLAYERCLIP.
# (Water and pure MONSTERCLIP are intentionally left passable.)
BLOCK_MASK = 0x1 | 0x2 | 0x8 | 0x4000 | 0x10000

def _model_brushes(nodes, leafs, leafbrushes, headnode=0):
    """Walk a BSP model's node tree collecting its leaf brushes.

    headnode 0 is the world model: world + func_detail, correctly placed, and
    excluding separate brush entities (doors, triggers, buttons), whose brushes are
    authored about their entity "origin". A brush entity's own headnode
    (dmodel_t @36) yields that entity's brushes instead - see write_entities."""
    lb = np.frombuffer(leafbrushes, dtype=np.uint16)
    out, stack = set(), [headnode]
    while stack:
        c = stack.pop()
        if c < 0:                                   # child < 0 -> leaf -(c)-1
            flb, nlb = struct.unpack_from("<HH", leafs, (-c - 1) * 32 + 24)
            out.update(int(lb[k]) for k in range(flb, flb + nlb))
        else:                                       # node: 32B, children @ +4
            ch0, ch1 = struct.unpack_from("<ii", nodes, c * 32 + 4)
            stack += [ch0, ch1]
    return out

def _brush_hull(planes, sides, brushes, bi):
    """A brush is the convex intersection of its sides' halfspaces (n.x <= d).
    Hull verts = every 3-plane intersection point that satisfies all planes."""
    fs, ns, cont = struct.unpack_from("<iii", brushes, bi * 12)
    P = []
    for s in range(fs, fs + ns):
        pn, _ti, _dp, bevel = struct.unpack_from("<hhhh", sides, s * 8)
        if bevel:                                   # bevels are redundant AABB planes
            continue
        P.append((planes[pn, :3], planes[pn, 3]))
    if len(P) < 4:
        return cont, None
    pts, m = [], len(P)
    for a in range(m):
        for b in range(a + 1, m):
            for c in range(b + 1, m):
                A = np.array([P[a][0], P[b][0], P[c][0]])
                if abs(np.linalg.det(A)) < 1e-6:
                    continue
                x = np.linalg.solve(A, np.array([P[a][1], P[b][1], P[c][1]]))
                if all(np.dot(P[i][0], x) - P[i][1] <= 0.05 for i in range(m)):
                    pts.append(x)
    return cont, (np.array(pts) if len(pts) >= 4 else None)

def write_collision(data, out_dir, base):
    """Emit `<base>.hulls`: one world brush per line as flat Unreal-space verts (cm)."""
    nodes, leafs = read_lump(data, 5), read_lump(data, 10)
    leafbrushes, brushes, sides = read_lump(data, 17), read_lump(data, 18), read_lump(data, 19)
    planes = np.frombuffer(read_lump(data, 1), dtype=np.float32).reshape(-1, 5)[:, :4].copy()
    world = _model_brushes(nodes, leafs, leafbrushes, 0)
    n = 0
    with open(os.path.join(out_dir, base + ".hulls"), "w") as f:
        for bi in sorted(world):
            cont, pts = _brush_hull(planes, sides, brushes, bi)
            if pts is None or not (cont & BLOCK_MASK):
                continue
            uniq = {(round(p[0], 1), round(p[1], 1), round(p[2], 1)): p for p in pts}
            g = [source_to_unreal(sx, sy, sz) for sx, sy, sz in uniq.values()]
            f.write(" ".join(f"{c:.4f}" for v in g for c in v) + "\n")
            n += 1
    print(f"collision hulls: {n} world brushes -> {base}.hulls")

# --- real-time light rig: WORLDLIGHTS (lump 15) -> `<base>.lights` -----------

def write_lights(data, out_dir, base):
    """Emit `<base>.lights`: one WORLDLIGHTS source per line, in Unreal space, for
    the runtime LightRig to spawn an Unreal light per source (the real-time lighting
    model that replaces the baked lightmap). Intensity stays raw linear RGB - the
    runtime splits it into a normalized colour and a scalar energy.

    Line: type ox oy oz  dx dy dz  ir ig ib  radius_cm  stopdot stopdot2 exponent  style
      type      0 emit_surface, 1 point, 2 spot, 3 skylight, 5 skyambient
      o*        origin, Unreal centimetres  (sx,-sy,sz)*INCH_TO_CM
      d*        beam direction, Unreal unit vector (nx,-ny,nz); 0 0 0 if none
      i*        intensity, raw linear RGB (colour*brightness)
      radius_cm cutoff radius in centimetres (0 = no cutoff)
      stopdot/stopdot2/exponent  spot cone (cos inner / cos outer / falloff exp)
      style     lightstyle index (0 = constant)"""
    wl = B.read_worldlights(data)
    with open(os.path.join(out_dir, base + ".lights"), "w") as f:
        for w in wl:
            ox, oy, oz = source_to_unreal(*w["origin"])
            ux, uy, uz = source_dir_to_unreal(*w["normal"])   # direction: Y negate, no scale
            m = (ux * ux + uy * uy + uz * uz) ** 0.5
            if m > 1e-6:
                ux, uy, uz = ux / m, uy / m, uz / m
            else:
                ux = uy = uz = 0.0
            ir, ig, ib = w["intensity"]
            f.write(f"{w['type']} {ox:.4f} {oy:.4f} {oz:.4f} "
                    f"{ux:.4f} {uy:.4f} {uz:.4f} "
                    f"{ir:.3f} {ig:.3f} {ib:.3f} {w['radius'] * INCH_TO_CM:.4f} "
                    f"{w['stopdot']:.4f} {w['stopdot2']:.4f} {w['exponent']:.3f} "
                    f"{w['style']}\n")
    from collections import Counter
    tc = dict(sorted(Counter(w["type"] for w in wl).items()))
    print(f"lights: {len(wl)} worldlights -> {base}.lights  by type {tc}")

# --- env_sprite coronas: glow billboards at light sources -------------------

def write_sprites(data, out_dir, base, idx):
    """Emit `<base>.sprites`: env_sprite glow billboards (the soft coronas VtMB places at
    lamps/bulbs). Each sprite's Sprite VMT `$basetexture` is decoded to `tex/spr_*.png`;
    one line per sprite: `png ox oy oz w_cm h_cm r g b amt orient`, where the world size is
    Source's `scale × textureSize` (inches → cm), `r g b`/`amt` are the entity's
    `rendercolor`/`renderamt` (additive tint), and orient is 0 (`vp_parallel`, full
    billboard) or 1 (`parallel_upright`, Y-axis only). `start_hidden` sprites are skipped
    (entity I/O that would switch them on is not ported)."""
    blocks = _parse_ent_blocks(read_lump(data, 0).decode("ascii", "replace"))
    pak = read_pakfile(data)

    def read_bytes(key):
        key = key.lower()
        return pak[key] if key in pak else install.read(idx, key)

    def read_text(key):
        b = read_bytes(key)
        return b.decode("ascii", "replace") if b is not None else None

    tex_cache = {}   # basetexture -> (png filename, width, height) | (None, 0, 0)

    def decode_sprite(bt):
        if bt not in tex_cache:
            tth, ttz = read_bytes(f"materials/{bt}.tth"), read_bytes(f"materials/{bt}.ttz")
            out = (None, 0, 0)
            if tth and ttz:
                try:
                    img = decode_texture(tth, ttz).convert("RGBA")
                    fn = "spr_" + sanitize(bt) + ".png"
                    img.save(os.path.join(out_dir, "tex", fn))
                    out = (fn, img.width, img.height)
                except Exception:
                    pass
            tex_cache[bt] = out
        return tex_cache[bt]

    lines = []
    for b in blocks:
        d = {k.lower(): v for k, v in b}
        if d.get("classname") != "env_sprite" or d.get("starthidden") == "1":
            continue
        model = d.get("model", "")
        vmt_txt = read_text(model if model.lower().endswith(".vmt") else model + ".vmt")
        if not vmt_txt:
            continue
        info = vmt.parse(vmt_txt, resolve_include=lambda p: read_text(
            p if p.lower().endswith(".vmt") else p + ".vmt"))
        bt = info.get("basetexture")
        if not bt:
            continue
        png, w, h = decode_sprite(bt)
        if not png:
            continue
        o = d.get("origin", "0 0 0").split()
        ux, uy, uz = source_to_unreal(float(o[0]), float(o[1]), float(o[2]))
        try:
            scale = float(d.get("scale", "1") or 1)
        except ValueError:
            scale = 1.0
        rc = (d.get("rendercolor", "255 255 255")).split()
        r, g, bb = (int(float(rc[i])) if i < len(rc) else 255 for i in range(3))
        try:
            amt = int(float(d.get("renderamt", "255") or 255))
        except ValueError:
            amt = 255
        orient = 1 if "parallel_upright" in vmt_txt.lower() else 0
        lines.append(f"tex/{png} {ux:.4f} {uy:.4f} {uz:.4f} "
                     f"{scale*w*INCH_TO_CM:.4f} {scale*h*INCH_TO_CM:.4f} "
                     f"{r} {g} {bb} {amt} {orient}")

    if lines:
        with open(os.path.join(out_dir, base + ".sprites"), "w") as f:
            f.write("\n".join(lines) + "\n")
    print(f"sprites: {len(lines)} env_sprite coronas ({len(tex_cache)} textures) -> {base}.sprites")

# --- entities: the Source I/O layer (docs/entity_io.md) ---------------------

def _parse_ent_blocks(text):
    """ENTITIES lump text -> [[(key, value), ...]], order and repeats preserved
    (an entity may carry several outputs on the same key)."""
    return [re.findall(r'"([^"]*)"\s+"([^"]*)"', b)
            for b in re.findall(r"\{([^{}]*)\}", text, re.S)]


def _split_output(value):
    """target,input,param,delay,times[,python[,extra]] -> dict, or None.

    VtMB writes 7 comma fields where Source writes 5; field 5 is a Python call
    string the engine evaluates as `__main__.<expr>`. See docs/entity_io.md."""
    if value.count(",") < 4:
        return None
    f = value.split(",")
    def num(s, d):
        try:
            return float(s)
        except ValueError:
            return d
    return {"target": f[0].strip(), "input": f[1].strip(), "param": f[2],
            "delay": num(f[3], 0.0), "times": int(num(f[4], -1)),
            "python": f[5].strip() if len(f) > 5 else ""}


def write_entities(data, out_dir, base):
    """Emit `<base>.ents` (JSON): every entity's keyvalues + outputs, and for brush
    entities ("model" "*N") their brush volumes as convex hulls.

    This is the data the runtime needs to spawn the interaction layer - trigger
    volumes, use volumes, doors. It is deliberately *unfiltered*: the render passes
    drop tools/* and StartHidden faces, but those same entities carry the level's
    behaviour, so dropping them from the data too would throw the game away.

    Hulls are entity-local Unreal centimetres and `origin` is the Unreal-space offset
    the engine translates the brush to at spawn; world = origin + hull (source_to_unreal
    is linear, so converting each separately and adding is equivalent). For
    func_door_rotating, `origin` is also the hinge.
    """
    nodes, leafs = read_lump(data, 5), read_lump(data, 10)
    leafbrushes, brushes, sides = read_lump(data, 17), read_lump(data, 18), read_lump(data, 19)
    planes = np.frombuffer(read_lump(data, 1), dtype=np.float32).reshape(-1, 5)[:, :4].copy()
    models_l = read_lump(data, 14)
    ents = read_lump(data, 0).decode("ascii", "replace")

    out, n_brush, n_hull, n_out = [], 0, 0, 0
    for pairs in _parse_ent_blocks(ents):
        keys, outputs = {}, []
        for k, v in pairs:
            o = _split_output(v) if re.match(r"^(On|Out)", k, re.I) else None
            if o:
                o["name"] = k
                outputs.append(o)
            else:
                keys[k] = v                      # last wins for plain keyvalues
        n_out += len(outputs)
        e = {"classname": keys.pop("classname", ""),
             "targetname": keys.pop("targetname", "")}

        og = keys.get("origin", "").split()
        so = [float(x) for x in og] if len(og) == 3 else [0.0, 0.0, 0.0]
        e["origin"] = [round(float(c), 5) for c in source_to_unreal(*so)]

        mdl = keys.get("model", "")
        if mdl.startswith("*"):
            mi = int(mdl[1:])
            if mi * 48 + 48 <= len(models_l):
                head = struct.unpack_from("<i", models_l, mi * 48 + 36)[0]
                hulls, cont_or = [], 0
                for bi in sorted(_model_brushes(nodes, leafs, leafbrushes, head)):
                    cont, pts = _brush_hull(planes, sides, brushes, bi)
                    if pts is None:
                        continue
                    cont_or |= cont
                    uniq = {(round(p[0], 1), round(p[1], 1), round(p[2], 1)): p for p in pts}
                    g = [source_to_unreal(sx, sy, sz) for sx, sy, sz in uniq.values()]
                    hulls.append([round(float(c), 4) for v in g for c in v])
                e["model"] = mi
                e["hulls"] = hulls
                e["contents"] = cont_or
                e["blocks_player"] = bool(cont_or & BLOCK_MASK)
                n_brush += 1
                n_hull += len(hulls)

        # StartHidden: the entity spawns fully OFF - SOLID_NONE, think disabled and
        # undrawn - until a ScriptUnhide input restores it (docs/entity_io.md).
        e["start_hidden"] = keys.get("StartHidden", "0") == "1"
        if outputs:
            e["outputs"] = outputs
        e["keys"] = keys
        out.append(e)

    path = os.path.join(out_dir, base + ".ents")
    with open(path, "w") as f:
        json.dump({"map": base, "entities": out}, f, separators=(",", ":"))
    print(f"entities: {len(out)} ({n_brush} brush, {n_hull} hulls, {n_out} outputs) -> {base}.ents")


def base_material(name):
    """Strip map-baked cubemap prefixes/suffixes to get the base material."""
    n = name.lower().replace("\\", "/")
    n = re.sub(r"/+", "/", n).strip("/")               # collapse '//' and edge slashes
    n = re.sub(r"^maps/[^/]+/", "", n)                 # drop 'maps/ch_hub_1/'
    n = re.sub(r"_-?\d+_-?\d+_-?\d+$", "", n)          # drop '_x_y_z' cubemap tag
    return n

def cubemap_of(name, mapbase):
    """The baked env cubemap a face samples, or None if not env-patched. VBSP
    patches each $envmap-carrying material instance to the nearest env_cubemap:
    'maps/<map>/<mat>' -> the map-wide 'cubemapdefault', 'maps/<map>/<mat>_x_y_z'
    -> the per-position 'c<x>_<y>_<z>' (the same _x_y_z base_material() strips).
    The tag is the cubemap texture stem under materials/maps/<map>/."""
    n = name.lower().replace("\\", "/")
    n = re.sub(r"/+", "/", n).strip("/")
    if not n.startswith(f"maps/{mapbase}/"):
        return None
    mm = re.search(r"_(-?\d+_-?\d+_-?\d+)$", n)
    return ("c" + mm.group(1)) if mm else "cubemapdefault"

def sanitize(name):
    return re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")


def disp_grid(src, dinfo, dispverts, emit):
    """Subdivide one displacement's flat quad into its sculpted grid.

    `src` = the face's 4 corners (source space, winding order). Orients the grid
    so the corner nearest `startpos` is (0,0) - the standard Source convention -
    then bilinearly interpolates the (psize+1)^2 grid and offsets each vertex by
    its DISP_VERT `vec*dist`. Vertices are appended via `emit(sx,sy,sz)->index`;
    returns the list of (i0,i1,i2) triangles.
    """
    power = dinfo["power"]
    psize = 1 << power
    n = psize + 1
    sp = dinfo["startpos"]
    d = [(c[0]-sp[0])**2 + (c[1]-sp[1])**2 + (c[2]-sp[2])**2 for c in src]
    si = d.index(min(d))                              # start corner -> grid (0,0)
    c0, c1, c2, c3 = (np.array(src[(si + k) % 4], dtype=np.float64) for k in range(4))
    vs = dinfo["vertstart"]
    idx = [[0] * n for _ in range(n)]
    for i in range(n):
        fi = i / psize
        a = c0 + (c1 - c0) * fi                        # edge c0->c1
        b = c3 + (c2 - c3) * fi                        # opposite edge c3->c2
        for j in range(n):
            base = a + (b - a) * (j / psize)
            vec, dist, alpha = dispverts[vs + i * n + j]
            p = base + np.array(vec) * dist
            # DISPVERT alpha (0..255) is the WorldVertexTransition blend weight.
            idx[i][j] = emit(float(p[0]), float(p[1]), float(p[2]), alpha / 255.0)
    tris = []
    for i in range(psize):
        for j in range(psize):
            v00, v01, v10, v11 = idx[i][j], idx[i][j+1], idx[i+1][j], idx[i+1][j+1]
            tris.append((v00, v10, v11))
            tris.append((v00, v11, v01))
    return tris


# --- static props: GAME_LUMP sprp -> .props sidecar + props/ model OBJs -----
def write_props(data, out_dir, base, idx):
    """Parse GAME_LUMP sprp (VtMB v4, 56B DStaticPropV4): a model-name dict + per-
    prop origin/angles/solid. Decode each unique model once (shared texture cache)
    into out_dir/props/<safename>.obj (Unreal space), and write <base>.props (one prop
    per line: `safename ox oy oz qx qy qz qw solid`). `solid` (0 = non-solid) gates
    collision.

    Unreal-native: the prop meshes are written via mdl.write_obj_scene(ue_space=True)
    (cm/Z-up/left-handed, winding reversed) and the origin/angles here are converted to
    source_to_unreal (origin) + source_angles_to_unreal_quat (a unit quaternion). The
    runtime reads both verbatim -- no coordinate conversion at load."""
    gl = read_game_lump(data)
    if "sprp" not in gl:
        return
    version, payload = gl["sprp"]
    p = 0
    name_count = struct.unpack_from("<i", payload, p)[0]; p += 4
    names = []
    for _ in range(name_count):
        names.append(payload[p:p + 128].split(b"\0", 1)[0].decode("ascii", "replace")); p += 128
    leaf_count = struct.unpack_from("<i", payload, p)[0]; p += 4
    p += leaf_count * (4 if version >= 12 else 2)
    prop_count = struct.unpack_from("<i", payload, p)[0]; p += 4
    size = (len(payload) - p) // prop_count if prop_count else 0

    props = []          # (safename, origin, angles, solid)
    used = {}           # model_path -> safename
    for i in range(prop_count):
        po = p + i * size
        origin = struct.unpack_from("<3f", payload, po)
        angles = struct.unpack_from("<3f", payload, po + 12)
        prop_type = struct.unpack_from("<H", payload, po + 24)[0]
        solid = struct.unpack_from("<B", payload, po + 30)[0]
        if prop_type >= len(names):
            continue
        model_path = names[prop_type].replace("\\", "/").lower()
        stem = model_path[:-4] if model_path.endswith(".mdl") else model_path
        safe = MDL.sanitize(stem)
        used[model_path] = safe
        props.append((safe, origin, angles, solid))

    propdir = os.path.join(out_dir, "props")
    os.makedirs(propdir, exist_ok=True)
    read_bytes = lambda key: install.read(idx, key)
    tex_cache, ok, missing = {}, 0, 0
    valid = set()               # models this run decoded; an .obj left over from an
                                # earlier export must not stand in for a failed decode
    for model_path, safe in sorted(used.items()):
        dv = MDL.load(idx, model_path)
        if not dv:
            missing += 1; continue
        try:
            meshes = MDL.decode(*dv)
            MDL.write_obj_scene(meshes, safe, propdir, MDL.search_paths(dv[0]), read_bytes,
                                tex_cache, ue_space=True)
            valid.add(safe); ok += 1
        except Exception as e:
            print(f"  prop decode failed {model_path}: {e}"); missing += 1
    solid_n = 0
    with open(os.path.join(out_dir, base + ".props"), "w") as f:
        for safe, (ox, oy, oz), (pitch, yaw, roll), solid in props:
            if safe not in valid:
                continue
            solid_n += solid != 0
            ux, uy, uz = source_to_unreal(ox, oy, oz)
            qx, qy, qz, qw = source_angles_to_unreal_quat(pitch, yaw, roll)
            f.write(f"{safe} {ux:.4f} {uy:.4f} {uz:.4f} "
                    f"{qx:.6f} {qy:.6f} {qz:.6f} {qw:.6f} {solid}\n")
    placed = sum(1 for pr in props if pr[0] in valid)
    print(f"props: {placed} placed ({solid_n} solid) / {len(used)} models "
          f"({ok} decoded, {missing} missing) -> {base}.props")


def main(bsp_path, out_dir):
    data = open(bsp_path, "rb").read()
    verts_l = read_lump(data, 3)
    edges_l = read_lump(data, 12)
    surf_l = read_lump(data, 13)
    faces_l = read_lump(data, 7)
    texinfo_l = read_lump(data, 6)
    texdata_l = read_lump(data, 2)
    names = strings_from_blob(read_lump(data, 43))
    table = list(struct.unpack_from("<%di" % (len(read_lump(data,44))//4),
                                    read_lump(data, 44), 0))

    os.makedirs(out_dir, exist_ok=True)
    lm_base = os.path.splitext(os.path.basename(bsp_path))[0]

    # Assets resolve from the map's embedded PAKFILE (lump 40: map-local +
    # cubemap-patched materials) first, then the install (patch before the VPKs -
    # see install.py). Defined up here because the bump-lightmap pre-pass below
    # needs each face's VMT before the atlas is built.
    idx = install.build_index()
    pak = read_pakfile(data)
    def read_material_bytes(key):
        key = key.lower()
        if key in pak:
            return pak[key]
        return install.read(idx, key)
    def read_material_text(key):
        b = read_material_bytes(key)
        return b.decode("ascii", "replace") if b is not None else None

    n_faces = len(faces_l) // FACE_SIZE

    def material_of(texinfo_idx):
        b = texinfo_idx * TEXINFO_SIZE
        s = struct.unpack_from("<4f", texinfo_l, b + TI_SAXIS)
        t = struct.unpack_from("<4f", texinfo_l, b + TI_TAXIS)
        td = struct.unpack_from("<i", texinfo_l, b + TI_TEXDATA)[0]
        tb = td * TEXDATA_SIZE
        name_id = struct.unpack_from("<i", texdata_l, tb + TD_NAMEID)[0]
        w = struct.unpack_from("<i", texdata_l, tb + TD_W)[0]
        h = struct.unpack_from("<i", texdata_l, tb + TD_H)[0]
        raw_name = names.get(table[name_id], "")
        return raw_name, s, t, max(w, 1), max(h, 1)

    vertexes = [struct.unpack_from("<fff", verts_l, i) for i in range(0, len(verts_l), 12)]
    edges = [struct.unpack_from("<HH", edges_l, i) for i in range(0, len(edges_l), 4)]
    surfedges = list(struct.unpack_from("<%di" % (len(surf_l)//4), surf_l, 0))
    dispinfos = B.read_dispinfos(data)   # [] on maps without displacements
    dispverts = B.read_dispverts(data)

    # The 3D skybox is what the sky_camera can see: the engine renders that pass
    # with visibility set up from the sky_camera's origin, and the skybox room is
    # sealed, so its PVS holds the miniature backdrop and nothing else. Faces the
    # sky_camera cannot see are the real map. (Distance to the sky_camera is not a
    # proxy for this: the patch's sp_tutorial_1 moves info_player_start 7,500
    # units, which swings any anchor bisector across the middle of the map.)
    ents = read_lump(data, 0).decode("ascii", "replace")
    def ent_origin(classname):
        for m in re.finditer(r"\{[^{}]*\}", ents):
            if f'"{classname}"' in m.group(0):
                o = re.search(r'"origin"\s+"(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)"', m.group(0))
                if o:
                    return tuple(float(x) for x in o.groups())
        return None
    def ent_field(classname, key, default=None):
        for m in re.finditer(r"\{[^{}]*\}", ents):
            if f'"{classname}"' in m.group(0):
                f = re.search(rf'"{key}"\s+"(-?[\d.]+)"', m.group(0))
                if f:
                    return float(f.group(1))
        return default
    sky_anchor = ent_origin("sky_camera")
    sky_scale = ent_field("sky_camera", "scale", 16.0)
    sky_faces = B.pvs_faces(data, sky_anchor) if sky_anchor else None
    if sky_anchor and sky_faces is None:
        print("  ! sky_camera sees no PVS - exporting every face as world")
    sky_faces = sky_faces or set()
    print(f"3D skybox: {len(sky_faces)} faces in the sky_camera's PVS")

    # Brush-entity placement: a brush model referenced by an entity ("model" "*N")
    # is authored centered at (0,0,0); the engine translates it to the entity's
    # "origin" at spawn (for doors, "origin" is also the hinge). Without this every
    # door / detail brush piles up at the world origin. Build face -> offset.
    model_origin = {}
    hidden_models = set()
    for m in re.finditer(r"\{[^{}]*\}", ents):
        blk = m.group(0)
        mo = re.search(r'"model"\s+"\*(\d+)"', blk)
        if not mo:
            continue
        mi = int(mo.group(1))
        og = re.search(r'"origin"\s+"(-?[\d.]+) (-?[\d.]+) (-?[\d.]+)"', blk)
        if og:
            model_origin[mi] = tuple(float(x) for x in og.groups())
        # "StartHidden" "1": the entity spawns invisible and a ScriptUnhide input
        # reveals it, so its faces are not part of the visible map (the buttons'
        # DEBUG/DEBUGEMPTY volumes, a func_brush's broken-window state, ...).
        # See docs/entity_io.md.
        if re.search(r'"StartHidden"\s+"1"', blk, re.I):
            hidden_models.add(mi)
    models_l = read_lump(data, 14)
    MODEL_SIZE = 48   # dmodel_t: mins[3]f maxs[3]f origin[3]f headnode i firstface i numfaces i
    face_offset = {}
    hidden_faces = set()
    for mi in range(len(models_l) // MODEL_SIZE):
        ff, nf = struct.unpack_from("<2i", models_l, mi * MODEL_SIZE + 40)
        if mi in hidden_models:
            hidden_faces.update(range(ff, ff + nf))
        off = model_origin.get(mi)
        if off is None:
            continue
        for f in range(ff, ff + nf):
            face_offset[f] = off

    # Three scenes: "world" (playable map), "sky" (3D skybox miniature), "decal".
    # Per scene: positions, uvs (albedo), groups (material->tris), and blend
    # (per-vertex WorldVertexTransition alpha, 0..1; 0 for every non-displacement or
    # non-blend vertex). blend rides a `.blend` sidecar and becomes vertex COLOR.r.
    scenes = {"world": ([], [], {}, []), "sky": ([], [], {}, []), "decal": ([], [], {}, [])}

    skipped_tools = 0
    skipped_hidden = 0
    disp_collision = []   # world-space (Unreal cm) disp triangles for concave collision
    # Env-patched materials split into one OBJ group per cubemap: the same base
    # material near two env_cubemaps samples two baked cubemaps, so each becomes its
    # own surface. The group key carries the cubemap ('<mat>@<cube>'); these map it
    # back to the base material (for VMT/texture lookup) and the cubemap stem.
    gkey_base, gkey_cube = {}, {}
    n_faces = len(faces_l) // FACE_SIZE
    for fi in range(n_faces):
        base = fi * FACE_SIZE
        firstedge = struct.unpack_from("<i", faces_l, base + FE_OFS)[0]
        numedges = struct.unpack_from("<h", faces_l, base + NE_OFS)[0]
        ti = struct.unpack_from("<h", faces_l, base + TI_OFS)[0]
        disp = struct.unpack_from("<h", faces_l, base + DISP_OFS)[0]   # -1 = flat
        if numedges < 3 or ti < 0:
            continue
        if fi in hidden_faces:
            skipped_hidden += 1
            continue
        raw_name, s, t, tw, th = material_of(ti)
        mat = base_material(raw_name)
        if mat.startswith("tools/"):
            skipped_tools += 1
            continue
        cube = cubemap_of(raw_name, lm_base)
        gkey = f"{mat}@{cube}" if cube else mat
        gkey_base[gkey] = mat
        gkey_cube[gkey] = cube

        # gather source-space corners first (for UVs and classification)
        src = []
        for k in range(numedges):
            se = surfedges[firstedge + k]
            v = edges[se][0] if se >= 0 else edges[-se][1]
            src.append(vertexes[v])
        # brush-entity origin offset (0 for world model 0); UVs/lightmap stay in
        # brush-local space, only the emitted world position is translated.
        ox, oy, oz = face_offset.get(fi, (0.0, 0.0, 0.0))
        # A brush entity's faces are its own model's, never in model 0's leaves,
        # so they are world by construction - as the engine draws them.
        is_sky = fi in sky_faces
        positions, uvs, groups, blend = scenes["sky" if is_sky else "world"]

        # Append one vertex (position + planar albedo UV); returns its index. Shared by
        # flat faces and displacement grid vertices. UVs use brush-local source coords;
        # only the emitted position gets the entity origin offset. (For displacements
        # sx,sy,sz is the displaced position.)
        def emit(sx, sy, sz, a=0.0):
            u = (sx*s[0] + sy*s[1] + sz*s[2] + s[3]) / tw   # planar UV projection
            vv = (sx*t[0] + sy*t[1] + sz*t[2] + t[3]) / th
            positions.append(source_to_unreal(sx+ox, sy+oy, sz+oz))  # -> Unreal cm
            uvs.append((u, vv))
            blend.append(a)                                 # WVT blend alpha (0 = tex1)
            return len(positions) - 1

        tris = groups.setdefault(gkey, [])
        if 0 <= disp < len(dispinfos) and len(src) == 4:
            gtris = disp_grid(src, dispinfos[disp], dispverts, emit)
            tris.extend(gtris)
            if not is_sky:   # concave terrain collision (skybox is backdrop only)
                for (a, bb, cc) in gtris:
                    disp_collision.append((positions[a], positions[bb], positions[cc]))
        else:
            corners = [emit(sx, sy, sz) for (sx, sy, sz) in src]   # flat fan
            for k in range(1, len(corners) - 1):
                tris.append((corners[0], corners[k], corners[k+1]))

    all_mats = set(scenes["world"][2]) | set(scenes["sky"][2])
    print(f"faces: {n_faces}  materials: {len(all_mats)}  skipped TOOLS: {skipped_tools}"
          f"  skipped StartHidden: {skipped_hidden}")
    print(f"  world groups: {len(scenes['world'][2])}   sky groups: {len(scenes['sky'][2])}")
    # sorted -> deterministic material order in the .mtl/.obj (was set-iteration order)
    groups = {m: scenes["world"][2].get(m, []) + scenes["sky"][2].get(m, []) for m in sorted(all_mats)}

    # --- resolve + decode each material's texture ---
    # (read_material_bytes/read_material_text + the install index and PAKFILE are
    # defined at the top of main, since the bump-lightmap pre-pass needs them too.)
    from PIL import Image
    os.makedirs(os.path.join(out_dir, "tex"), exist_ok=True)
    img_cache = {}       # basetexture -> decoded RGBA PIL image (or None)
    albedo_cache = {}    # basetexture -> albedo png filename
    emis_cache = {}      # basetexture -> emission png filename
    normal_cache = {}    # normalmap -> normal png filename
    cube_cache = {}      # cube id -> bool (six face PNGs written under tex/cube/)
    mask_cache = {}      # envmapmask source key -> mask png filename (or None)
    mat_info = {}        # gkey -> (albedo_png, emission_png, alphatest, translucent)
    env_info = {}        # gkey -> dict(cube, mask, tint, contrast, saturation)
    blend_info = {}      # gkey -> second albedo png (WorldVertexTransition tex2)
    bump_info = {}       # gkey -> normal-map png ($bumpmap; perturbs the reflection)
    water_info = {}      # water material -> dict(normalmap_png, fogcolor, fog, reflecttint)
    decoded = failed = 0
    # A rendered, non-water material that yields no albedo renders as a flat grey
    # fallback - which silently hides VMT parse/resolution bugs. Collect any such
    # material and fail the export, so a future regression surfaces here instead of
    # as an untextured wall in the viewer. `warnings` are the softer cases (a VMT
    # that genuinely declares no $basetexture, e.g. a colour-only shader).
    problems = []
    warnings = []

    def get_img(bt):
        if bt not in img_cache:
            tth, ttz = read_material_bytes(f"materials/{bt}.tth"), read_material_bytes(f"materials/{bt}.ttz")
            img = None
            if tth and ttz:
                try:
                    img = decode_texture(tth, ttz)
                except Exception:
                    img = None
            img_cache[bt] = img
        return img_cache[bt]

    for gkey in groups:
        mat = gkey_base.get(gkey, gkey)     # base material for VMT/texture lookup
        cube = gkey_cube.get(gkey)          # baked cubemap stem, or None
        vmt_txt = read_material_text(f"materials/{mat}.vmt")
        info = {"basetexture": None, "selfillum": False, "translucent": False, "alphatest": False,
                "water": False, "normalmap": None, "fogcolor": None, "fogstart": None,
                "fogend": None, "reflecttint": None}
        if vmt_txt:
            info = vmt.parse(vmt_txt, resolve_include=lambda p: read_material_text(
                p if p.lower().endswith(".vmt") else p + ".vmt"))
        bt = info["basetexture"]
        alphatest = info["alphatest"]
        translucent = info["translucent"]
        keep_alpha = alphatest or translucent   # both modes need the alpha channel
        albedo_png = emis_png = None
        if bt:
            img = get_img(bt)
            if img is None:
                failed += 1
            else:
                if bt not in albedo_cache:
                    fn = sanitize(bt) + ".png"
                    # keep alpha only for alpha-tested/translucent; else opaque RGB
                    (img if keep_alpha else img.convert("RGB")).save(os.path.join(out_dir, "tex", fn))
                    albedo_cache[bt] = fn
                    decoded += 1
                albedo_png = albedo_cache[bt]
                # $selfillum: emission masked by the texture's alpha channel
                if info["selfillum"]:
                    if bt not in emis_cache:
                        arr = np.asarray(img.convert("RGBA"), dtype=np.float32)
                        a = arr[:, :, 3:4] / 255.0
                        masked = (arr[:, :, :3] * a).clip(0, 255).astype("uint8")
                        efn = sanitize(bt) + "_ke.png"
                        Image.fromarray(masked, "RGB").save(os.path.join(out_dir, "tex", efn))
                        emis_cache[bt] = efn
                    emis_png = emis_cache[bt]
        # Water (the "Water" shader): no basetexture; decode its normal map for the
        # runtime water shader and stash fog/plane params for the .water sidecar.
        if info["water"]:
            npng = None
            nm = info["normalmap"]
            if nm:
                nimg = get_img(nm)
                if nimg is not None:
                    if nm not in normal_cache:
                        nfn = sanitize(nm) + "_n.png"
                        nimg.convert("RGB").save(os.path.join(out_dir, "tex", nfn))
                        normal_cache[nm] = nfn
                    npng = normal_cache[nm]
            water_info[gkey] = {
                "normalmap": npng,
                "fogcolor": info["fogcolor"] or [0.10, 0.10, 0.13],
                "fogstart": info["fogstart"] or 0.0,
                "fogend": info["fogend"] or 128.0,
                "reflecttint": info["reflecttint"] or [1.0, 1.0, 1.0],
            }
        # $envmap cubemap reflection: decode the baked cube this instance samples
        # (per-face `cube` from VBSP's patch; else a static named cubemap) into six
        # face PNGs under tex/cube/, decode its mask ($envmapmask texture, or the
        # base texture's alpha for $basealphaenvmapmask), and record tint/contrast/
        # saturation for the shader's additive reflection term.
        env_ref = info.get("envmap")
        if bt and (cube or (env_ref and env_ref != "env_cubemap")):
            cube_id = cube or sanitize(env_ref)
            cube_key = f"materials/maps/{lm_base}/{cube}" if cube else f"materials/{env_ref}"
            if cube_id not in cube_cache:
                ctth = read_material_bytes(cube_key + ".tth")
                cttz = read_material_bytes(cube_key + ".ttz")   # None for uncompressed cubes
                ok = False
                if ctth:
                    try:
                        cdir = os.path.join(out_dir, "tex", "cube")
                        os.makedirs(cdir, exist_ok=True)
                        for i, face in enumerate(decode_cubemap(ctth, cttz)):
                            face.convert("RGB").save(os.path.join(cdir, f"{cube_id}_{i}.png"))
                        ok = True
                    except Exception:
                        ok = False
                cube_cache[cube_id] = ok
            if cube_cache[cube_id]:
                mask_png = None
                em = info.get("envmapmask")
                if em:
                    if em not in mask_cache:
                        mimg = get_img(em)
                        mask_cache[em] = None
                        if mimg is not None:
                            mfn = sanitize(em) + "_envmask.png"
                            mimg.convert("L").save(os.path.join(out_dir, "tex", mfn))
                            mask_cache[em] = mfn
                    mask_png = mask_cache[em]
                elif info.get("basealphaenvmapmask"):
                    mk = bt + "#a"
                    if mk not in mask_cache:
                        aimg = get_img(bt)
                        mask_cache[mk] = None
                        if aimg is not None:
                            mfn = sanitize(bt) + "_envmask.png"
                            aimg.convert("RGBA").getchannel("A").save(os.path.join(out_dir, "tex", mfn))
                            mask_cache[mk] = mfn
                    mask_png = mask_cache[mk]
                sat = info.get("envmapsaturation")
                env_info[gkey] = {
                    "cube": cube_id,
                    "mask": mask_png,
                    "tint": info.get("envmaptint") or [1.0, 1.0, 1.0],
                    "contrast": info.get("envmapcontrast") or 0.0,
                    "saturation": 1.0 if sat is None else sat,
                }
        # WorldVertexTransition: second base texture, blended per-vertex against the
        # first by the displacement's DISPVERT alpha (the .blend sidecar).
        bt2 = info.get("basetexture2")
        if bt2:
            img2 = get_img(bt2)
            if img2 is not None:
                if bt2 not in albedo_cache:
                    fn2 = sanitize(bt2) + ".png"
                    img2.convert("RGB").save(os.path.join(out_dir, "tex", fn2))
                    albedo_cache[bt2] = fn2
                blend_info[gkey] = albedo_cache[bt2]

        # $bumpmap: tangent-space normal map. Perturbs the shading normal, which drives
        # both the real-time lighting and the $envmap reflection. Decoded for every
        # bumpmap material.
        bump = info.get("bumpmap")
        if bump:
            nimg = get_img(bump)
            if nimg is not None:
                if bump not in normal_cache:
                    nfn = sanitize(bump) + "_n.png"
                    nimg.convert("RGB").save(os.path.join(out_dir, "tex", nfn))
                    normal_cache[bump] = nfn
                bump_info[gkey] = normal_cache[bump]

        mat_info[gkey] = (albedo_png, emis_png, alphatest, translucent)

        # validate: a rendered non-water material must resolve to an albedo.
        if not info["water"]:
            if vmt_txt is None:
                problems.append(f"{mat}: no VMT found (materials/{mat}.vmt)")
            elif bt is None:
                if re.search(r"\$basetexture", vmt_txt, re.I):
                    problems.append(f"{mat}: VMT declares $basetexture but parser returned None")
                else:
                    warnings.append(f"{mat}: VMT has no $basetexture")
            elif albedo_png is None:
                problems.append(f"{mat}: $basetexture '{bt}' texture missing/undecodable")

    print(f"textures decoded: {decoded}  emission masks: {len(emis_cache)}  failed/missing: {failed}")
    print(f"envmap: {len(env_info)} surfaces reflect {sum(cube_cache.values())} cubemaps "
          f"({len(mask_cache)} masks)")
    for w in warnings:
        print(f"  warning: {w}")
    if problems:
        raise SystemExit(
            f"export aborted: {len(problems)} material(s) resolved to no texture "
            f"(flat-grey fallback):\n  " + "\n  ".join(problems))

    # --- infodecals: VtMB's decal layer -------------------------------------
    # VtMB leaves the OVERLAYS lump (45) empty; every poster/stain/sign/spray is an
    # `infodecal` entity (a `texture` + `origin`). The engine projects it onto the
    # surfaces within the decal's radius (engine.dll R_DecalShoot -> R_DecalNode ->
    # R_DecalCreate). We attach each to the single visible face its origin projects
    # squarely onto (nearest by plane distance), size the quad by the material's
    # texture dims x $decalscale (R_DecalSize: GetMappingWidth/Height * $decalScale),
    # and orient it to that face's texture axes. The decal mesh is lit like any world
    # surface by the real-time LightRig. docs/map_completion_plan.md E.
    decal_mats = set()
    dpos, duv, dgroups, _dblend = scenes["decal"]
    planes = np.frombuffer(read_lump(data, 1), dtype=np.float32).reshape(-1, 5)

    # A decal's host face is drawn double-sided (world CullMode = Disabled), so VtMB
    # authors the wall with arbitrary winding -- the face normal may point into the
    # sealed interior, away from the alley the decal is meant to be seen from. Nudging
    # blindly along that normal buries the decal in solid space, occluded by the opaque
    # wall. Resolve the room side from BSP leaf solidity and face the decal that way.
    d_nodes, d_leafs = read_lump(data, 5), read_lump(data, 10)
    LEAF_SZ = 32
    # How far a decal projects onto faces off its primary plane (source units) -- lets
    # it wrap around corners and across coplanar face splits. Kept shallow so the
    # perpendicular-wall smear stays a seam filler, not a deep streak.
    WRAP_DEPTH = float(os.environ.get("ELYSIUM_DECAL_WRAP", "4.0"))
    def _leaf_solid(pt):
        li = B.point_leaf(data, pt, d_nodes, planes)
        return bool(struct.unpack_from("<i", d_leafs, li * LEAF_SZ)[0] & 1)  # CONTENTS_SOLID

    def _room_normal(proj, nrm, p):
        """Flip `nrm` toward the open (non-solid) side of the face -- where the decal
        is visible. Ambiguous (both sides open/solid) -> the entity origin's side, else
        the authored normal."""
        plus, minus = _leaf_solid(proj + nrm * 2.0), _leaf_solid(proj - nrm * 2.0)
        if plus != minus:
            return nrm if minus else -nrm
        s = float(np.dot(p - proj, nrm))
        return -nrm if s < -1e-3 else nrm

    # index every visible (non-tools) face in Source space for the projection search
    fN, fD, fPoly, fBasis, f3D = [], [], [], [], []
    for fi in range(n_faces):
        fb = fi * FACE_SIZE
        ne = struct.unpack_from("<h", faces_l, fb + NE_OFS)[0]
        ti = struct.unpack_from("<h", faces_l, fb + TI_OFS)[0]
        if ne < 3 or ti < 0:
            continue
        raw_name, sax, tax, _tw, _th = material_of(ti)
        if base_material(raw_name).startswith("tools/"):
            continue
        fe = struct.unpack_from("<i", faces_l, fb + FE_OFS)[0]
        pn = struct.unpack_from("<H", faces_l, fb + 32)[0]
        side = struct.unpack_from("<b", faces_l, fb + 34)[0]
        off = np.array(face_offset.get(fi, (0.0, 0.0, 0.0)))
        pts = np.array([np.array(vertexes[edges[se][0] if se >= 0 else edges[-se][1]]) + off
                        for se in (surfedges[fe + k] for k in range(ne))])
        nrm = planes[pn, :3].astype(float)
        dd = float(planes[pn, 3]) + float(np.dot(planes[pn, :3], off))
        if side:
            nrm, dd = -nrm, -dd
        a = pts[1] - pts[0]; a = a / np.linalg.norm(a); bax = np.cross(nrm, a)
        poly2 = np.array([[np.dot(x - pts[0], a), np.dot(x - pts[0], bax)] for x in pts])
        fN.append(nrm); fD.append(dd); fPoly.append(poly2); f3D.append(pts)
        fBasis.append((pts[0], a, bax, fi, off, np.array(sax[:3]), np.array(tax[:3])))
    fN = np.array(fN) if fN else np.zeros((0, 3))
    fD = np.array(fD) if fD else np.zeros((0,))
    # per-face bounding sphere (source space) for the wrap radius pre-filter
    f_ctr = np.array([p.mean(0) for p in f3D]) if f3D else np.zeros((0, 3))
    f_rad = np.array([np.linalg.norm(p - c0, axis=1).max() for p, c0 in zip(f3D, f_ctr)]) \
        if f3D else np.zeros((0,))

    def _clip_hs(poly, n, d):
        """Sutherland-Hodgman clip of a 3D polygon to the halfspace dot(v,n) <= d."""
        out = []
        for i in range(len(poly)):
            a_, b_ = poly[i], poly[(i + 1) % len(poly)]
            da, db = float(np.dot(a_, n)) - d, float(np.dot(b_, n)) - d
            if da <= 1e-6:
                out.append(a_)
            if (da <= 1e-6) != (db <= 1e-6):
                out.append(a_ + (b_ - a_) * (da / (da - db)))
        return out

    def _inplane(poly2, q):
        """distance from 2D point q to convex polygon (0 if inside; seam-tolerant)."""
        e = np.roll(poly2, -1, axis=0) - poly2
        L = np.hypot(e[:, 0], e[:, 1]); L[L < 1e-9] = 1e-9
        w = q - poly2
        cr = (e[:, 0] * w[:, 1] - e[:, 1] * w[:, 0]) / L
        if np.all(cr >= -0.5) or np.all(cr <= 0.5):
            return 0.0
        t = np.clip((w[:, 0] * e[:, 0] + w[:, 1] * e[:, 1]) / (L * L), 0, 1)
        proj = poly2 + e * t[:, None]
        return float(np.min(np.hypot(*(q - proj).T)))

    decal_cache = {}   # texture -> (albedo_png, emis_png, w, h, scale, unlit) or None
    def _decal_material(tex):
        if tex in decal_cache:
            return decal_cache[tex]
        vtxt = read_material_text(f"materials/{tex}.vmt")
        info = vmt.parse(vtxt, resolve_include=lambda p: read_material_text(
            p if p.lower().endswith(".vmt") else p + ".vmt")) if vtxt else None
        img = get_img(info["basetexture"]) if info and info["basetexture"] else None
        if img is None:
            decal_cache[tex] = None
            return None
        bt = info["basetexture"]
        if bt not in albedo_cache:
            fn = sanitize(bt) + ".png"
            img.save(os.path.join(out_dir, "tex", fn))       # keep alpha (decals blend)
            albedo_cache[bt] = fn
        emis_png = None
        if info["selfillum"]:
            if bt not in emis_cache:
                arr = np.asarray(img.convert("RGBA"), dtype=np.float32)
                masked = (arr[:, :, :3] * (arr[:, :, 3:4] / 255.0)).clip(0, 255).astype("uint8")
                efn = sanitize(bt) + "_ke.png"
                Image.fromarray(masked, "RGB").save(os.path.join(out_dir, "tex", efn))
                emis_cache[bt] = efn
            emis_png = emis_cache[bt]
        unlit = (info["shader"] or "").startswith("unlit")
        res = (albedo_cache[bt], emis_png, img.size[0], img.size[1], info["decalscale"], unlit)
        decal_cache[tex] = res
        mat_info[tex] = (albedo_cache[bt], emis_png, False, True)   # alpha-blended
        decal_mats.add(tex)
        return res

    n_decal = n_decal_miss = 0
    for m in re.finditer(r"\{[^{}]*\}", ents):
        blk = m.group(0)
        if '"infodecal"' not in blk:
            continue
        om = re.search(r'"origin"\s+"(-?[\d.]+) (-?[\d.]+) (-?[\d.]+)"', blk)
        tm = re.search(r'"texture"\s+"([^"]+)"', blk)
        if not (om and tm):
            continue
        mat = _decal_material(base_material(tm.group(1)))
        if mat is None or len(fN) == 0:
            n_decal_miss += 1
            continue
        _albedo, _emis, tw, th, scale, unlit = mat
        p = np.array([float(x) for x in om.groups()])
        pd_all = np.abs(fN @ p - fD)
        best = None
        for j in np.where(pd_all <= 64.0)[0]:
            p0, av, bv, fi, off, sax, tax = fBasis[j]
            q = np.array([np.dot(p - p0, av), np.dot(p - p0, bv)])
            if _inplane(fPoly[j], q) <= 1.0 and (best is None or pd_all[j] < best[0]):
                best = (pd_all[j], j)
        if best is None:
            n_decal_miss += 1
            continue
        p0, av, bv, fi, off, sax, tax = fBasis[best[1]]
        nrm = fN[best[1]]
        # project the origin onto the face plane, then face the decal toward the open
        # side and nudge it that way (toward the room -- beats z-fight, avoids burial)
        proj = p - nrm * (float(np.dot(nrm, p)) - fD[best[1]])
        nrm = _room_normal(proj, nrm, p)
        c = proj + nrm * 0.25
        # decal basis = the face's texture axes made perpendicular to the normal.
        # The texinfo s/t sign is authored per face, so normalise it: t_dir (V) stays
        # as authored (keeps text upright), and s_dir (U) is signed so the frame is
        # left-handed w.r.t. the outward normal -- which, with V-down texture space,
        # is what reads un-mirrored from the +normal (room) side (verified by capture).
        s_dir = sax - nrm * np.dot(nrm, sax); s_dir /= np.linalg.norm(s_dir)
        t_dir = tax - nrm * np.dot(nrm, tax); t_dir /= np.linalg.norm(t_dir)
        if np.dot(np.cross(s_dir, t_dir), nrm) > 0:
            s_dir = -s_dir
        hw, hh = tw * scale / 2.0, th * scale / 2.0
        # Project the decal the way the engine does (R_DecalNode): clip every face
        # within reach to the projector box (the s/t footprint x a shallow depth) and
        # lay a fragment on each. A decal now wraps across coplanar face splits and
        # around corners, and is clipped at silhouette edges instead of poking a single
        # flat quad out past the wall. UVs come from the shared s/t projection so the
        # texture stays continuous across the fragments.
        cs, ct, cn = float(c @ s_dir), float(c @ t_dir), float(c @ nrm)
        box = [(s_dir, cs + hw), (-s_dir, -cs + hw), (t_dir, ct + hh),
               (-t_dir, -ct + hh), (nrm, cn + WRAP_DEPTH), (-nrm, -cn + WRAP_DEPTH)]
        reach = float(np.hypot(np.hypot(hw, hh), WRAP_DEPTH))
        tris = dgroups.setdefault(base_material(tm.group(1)), [])
        placed_frag = False
        for jf in np.where(np.linalg.norm(f_ctr - c, axis=1) <= reach + f_rad)[0]:
            poly = list(f3D[jf])
            for (nn, dd) in box:
                poly = _clip_hs(poly, nn, dd)
                if len(poly) < 3:
                    break
            if len(poly) < 3:
                continue
            frag = np.array(poly)
            if jf != best[1] and _leaf_solid(0.5 * (c + frag.mean(0))):
                continue                       # wrap face sits across solid from the decal
            rn = _room_normal(frag.mean(0), fN[jf], c)
            i0 = len(dpos)
            for vtx in frag:
                dpos.append(source_to_unreal(*(vtx + rn * 0.25)))
                duv.append((float((vtx - c) @ s_dir / (2 * hw) + 0.5),
                            float((vtx - c) @ t_dir / (2 * hh) + 0.5)))
            for k in range(1, len(frag) - 1):
                tris.append((i0, i0 + k, i0 + k + 1))
            placed_frag = True
        if not placed_frag:
            n_decal_miss += 1
            continue
        n_decal += 1
    print(f"decals: {n_decal} placed, {n_decal_miss} unmatched, {len(decal_mats)} materials")

    # --- write OBJ (world + skybox) + shared MTL ---
    base = os.path.splitext(os.path.basename(bsp_path))[0]
    mtl_path = os.path.join(out_dir, base + ".mtl")

    # --- 2D sky box faces + fog -> tex/sky_*.png + <base>.env ----------------
    # worldspawn "skyname" -> materials/skybox/<name>{up,dn,lf,rt,ft,bk}.tth/.ttz
    # (same TTH/TTZ decode as world textures). sky_camera carries the fog the
    # engine renders behind the 3D skybox; we reuse it as the world env fog.
    def ent_block(classname):
        for m in re.finditer(r'\{[^{}]*\}', ents):
            if f'"{classname}"' in m.group(0):
                return m.group(0)
        return ""
    def blk_f(blk, key, default=0.0):
        m = re.search(rf'"{key}"\s+"(-?[\d.]+)"', blk, re.I)
        return float(m.group(1)) if m else default
    def blk_vec(blk, key):
        m = re.search(rf'"{key}"\s+"([^"]+)"', blk, re.I)
        if not m:
            return None
        try:
            return [float(x) for x in m.group(1).split()][:3]
        except ValueError:
            return None

    sky_m = re.search(r'"skyname"\s+"([^"]+)"', ents, re.I)
    skyname = sky_m.group(1).lower() if sky_m else None
    sky_ok = False
    if skyname:
        got = 0
        for face in ("up", "dn", "lf", "rt", "ft", "bk"):
            tth = read_material_bytes(f"materials/skybox/{skyname}{face}.tth")
            ttz = read_material_bytes(f"materials/skybox/{skyname}{face}.ttz")
            if tth and ttz:
                try:
                    decode_texture(tth, ttz).convert("RGB").save(
                        os.path.join(out_dir, "tex", f"sky_{face}.png"))
                    got += 1
                except Exception:
                    pass
        sky_ok = got == 6
        print(f"sky '{skyname}': {got}/6 faces decoded")

    scam = ent_block("sky_camera")
    fog_on = blk_f(scam, "fogenable") >= 1.0
    fog_col = blk_vec(scam, "fogcolor") or [0.0, 0.0, 0.0]
    with open(os.path.join(out_dir, base + ".env"), "w") as o:
        o.write(f"skybox {1 if sky_ok else 0}\n")
        if skyname:
            o.write(f"skyname {skyname}\n")
        o.write(f"fog {1 if fog_on else 0}\n")
        r, g, b = (max(0.0, c) / 255.0 for c in fog_col)
        o.write(f"fogcolor {r:.4f} {g:.4f} {b:.4f}\n")
        o.write(f"fogstart {blk_f(scam, 'fogstart') * INCH_TO_CM:.4f}\n")   # -> cm
        o.write(f"fogend {blk_f(scam, 'fogend') * INCH_TO_CM:.4f}\n")

    def write_obj(suffix, scene):
        positions, uvs, groups, blend = scene
        if not positions:
            return
        with open(os.path.join(out_dir, base + suffix + ".obj"), "w") as o:
            o.write(f"mtllib {base}.mtl\n")
            for (x, y, z) in positions:
                o.write(f"v {x:.4f} {y:.4f} {z:.4f}\n")
            for (u, v) in uvs:
                o.write(f"vt {u:.5f} {v:.5f}\n")
            for mat, tris in groups.items():
                if not tris:
                    continue
                o.write(f"usemtl {mat}\n")
                for (a, b, c) in tris:
                    # Reverse winding (a, c, b): source_to_unreal negates Y (a reflection),
                    # so faces built in Source winding must flip to stay front-facing in
                    # Unreal's left-handed frame. The runtime reads these tris verbatim.
                    o.write(f"f {a+1}/{a+1} {c+1}/{c+1} {b+1}/{b+1}\n")
        # WorldVertexTransition blend sidecar: one alpha per vertex (v order). Only
        # written when the scene actually carries blend weights (disp terrain); the
        # loader stamps it onto vertex COLOR.r for the shader's tex1/tex2 mix.
        if len(blend) == len(positions) and any(blend):
            with open(os.path.join(out_dir, base + suffix + ".blend"), "w") as o:
                for a in blend:
                    o.write(f"{a:.4f}\n")

    write_obj("", scenes["world"])
    write_obj("_sky", scenes["sky"])
    write_obj("_decals", scenes["decal"])

    # sky_camera sidecar: the miniature backdrop must be placed at
    # world(v) = (v - origin) * scale (Source 3D-skybox transform). Origin is emitted in
    # Unreal cm (same space as the _sky.obj verts), so the runtime applies it directly.
    if sky_anchor is not None and scenes["sky"][0]:
        sox, soy, soz = source_to_unreal(*sky_anchor)
        with open(os.path.join(out_dir, base + ".sky"), "w") as o:
            o.write(f"origin {sox:.4f} {soy:.4f} {soz:.4f}\n")
            o.write(f"scale {sky_scale}\n")

    # player-spawn sidecar: info_player_start origin + yaw (Source angles are
    # "pitch yaw roll"; yaw is the middle value). Origin is emitted in Unreal cm and yaw
    # is negated (the Y flip reverses yaw sense), so the runtime spawns here directly.
    for m in re.finditer(r"\{[^{}]*\}", ents):
        if '"info_player_start"' in m.group(0):
            o = re.search(r'"origin"\s+"(-?[\d.]+) (-?[\d.]+) (-?[\d.]+)"', m.group(0))
            a = re.search(r'"angles"\s+"(-?[\d.]+) (-?[\d.]+) (-?[\d.]+)"', m.group(0))
            if o:
                yaw = float(a.group(2)) if a else 0.0
                sx2, sy2, sz2 = source_to_unreal(float(o.group(1)), float(o.group(2)), float(o.group(3)))
                with open(os.path.join(out_dir, base + ".spawn"), "w") as f:
                    f.write(f"origin {sx2:.4f} {sy2:.4f} {sz2:.4f}\n")
                    f.write(f"yaw {-yaw}\n")
            break

    write_collision(data, out_dir, base)
    write_entities(data, out_dir, base)
    write_lights(data, out_dir, base)
    write_sprites(data, out_dir, base, idx)
    # Concave displacement collision: one triangle per line (9 godot floats).
    # Convex brushes can't represent sculpted terrain, so the viewer loads these
    # as a ConcavePolygonShape3D alongside the .hulls convex bodies.
    if disp_collision:
        with open(os.path.join(out_dir, base + ".dispcol"), "w") as f:
            for tri in disp_collision:
                f.write(" ".join(f"{c:.4f}" for p in tri for c in p) + "\n")
        print(f"disp collision: {len(disp_collision)} triangles -> {base}.dispcol")
    # Water sidecar: per water material a surface plane height (Unreal Z, cm), the decoded
    # normal map, and fog/reflection params. Plane height = median Z of the material's
    # world-scene vertices (water faces are horizontal). The runtime renders these as
    # Single Layer Water at that Z and reflects via Lumen/SSR. Water that lives only in the
    # 3D-skybox backdrop (no world faces) is skipped here - a mirror plane in world space
    # is meaningless for the scaled miniature.
    wpositions, wgroups = scenes["world"][0], scenes["world"][2]
    def plane_z(mat):
        zs = sorted(wpositions[i][2] for tri in wgroups.get(mat, []) for i in tri)
        return zs[len(zs) // 2] if zs else None
    water_info = {m: w for m, w in water_info.items() if wgroups.get(m)}
    if water_info:
        with open(os.path.join(out_dir, base + ".water"), "w") as f:
            for mat, w in water_info.items():
                f.write(f"mat {mat}\n")
                f.write(f"plane {plane_z(mat):.4f}\n")
                if w["normalmap"]:
                    f.write(f"normalmap tex/{w['normalmap']}\n")
                fc = w["fogcolor"]
                f.write(f"fogcolor {fc[0]:.4f} {fc[1]:.4f} {fc[2]:.4f}\n")
                f.write(f"fogdist {w['fogstart']*INCH_TO_CM:.4f} {w['fogend']*INCH_TO_CM:.4f}\n")
                rt = w["reflecttint"]
                f.write(f"reflecttint {rt[0]:.4f} {rt[1]:.4f} {rt[2]:.4f}\n")
        print(f"water: {len(water_info)} surfaces -> {base}.water")

    # --- static props (GAME_LUMP sprp) -> props/ OBJs + <base>.props ----------
    # Per-prop ambient comes from WORLDLIGHTS (lump 15), the way the engine's lightcache
    # lights props (write_props → tint_at); the baked world lightmap is for surfaces.
    write_props(data, out_dir, base, idx)

    obj_path = os.path.join(out_dir, base + ".obj")
    with open(mtl_path, "w") as m:
        for mat, (albedo_png, emis_png, alphatest, translucent) in mat_info.items():
            m.write(f"newmtl {mat}\n")
            if albedo_png:
                m.write(f"map_Kd tex/{albedo_png}\n")
            elif mat in water_info or "water" in mat:
                m.write("Kd 0.05 0.10 0.13\n")   # dark water placeholder (behind the shader)
            else:
                m.write("Kd 0.35 0.35 0.38\n")   # generic missing-tex fallback
            if emis_png:
                m.write(f"map_Ke tex/{emis_png}\n")   # alpha-masked self-illum
            if mat in water_info:
                m.write("water 1\n")                   # our flag: water shader surface
            elif translucent:
                m.write("blend 1\n")                   # our flag: alpha-blended
            elif alphatest:
                m.write("illum 4\n")                   # our flag: alpha-tested (scissor)
            if mat in decal_mats:
                m.write("decal 1\n")                   # our flag: projected decal (render above wall)
            if mat in env_info:
                e = env_info[mat]
                # cubemap reflection: 'envmap <id>' -> tex/cube/<id>_{0..5}.png (six
                # faces, VTF order +x,-x,+y,-y,+z,-z), masked/tinted, additive.
                m.write(f"envmap {e['cube']}\n")
                if e["mask"]:
                    m.write(f"envmapmask tex/{e['mask']}\n")
                t = e["tint"]
                m.write(f"envtint {t[0]:.4f} {t[1]:.4f} {t[2]:.4f}\n")
                m.write(f"envparams {e['contrast']:.4f} {e['saturation']:.4f}\n")
            if mat in blend_info:
                # WorldVertexTransition second texture; mixed by vertex COLOR.r.
                m.write(f"basetex2 tex/{blend_info[mat]}\n")
            if mat in bump_info:
                # normal map: perturbs the reflection normal (needs mesh tangents).
                m.write(f"bumpmap tex/{bump_info[mat]}\n")
            m.write("\n")

    print(f"wrote {obj_path}")
    print(f"      {mtl_path}")
    print(f"      {os.path.join(out_dir,'tex')}/  ({decoded} PNGs)")

    # DDS siblings (original DXT blocks + mips) for every albedo the runtime prefers
    # over its PNG - world tex/ and props/tex/. Reuses the install index already built
    # above. PNG stays the fallback: a texture with no DXT source (generated _ke maps,
    # BGR888) gets none and the runtime's "failed to read .dds" log flags it unoptimized.
    flat, dropped = retex_dds.flat_index(idx)
    retex_dds.emit_dir(idx, flat, dropped, out_dir, base)

# The maps we actively test against. `python bsp_to_scene.py --all` re-exports the
# whole set (use it after any pipeline change so no scene is left stale); keep it in
# sync with what lives under tools/out/. Each is a hub or set-piece exercising a
# different feature: sm_hub_1 (animated lightstyles + props), hw_hub_1/hw_redspot_1
# (displacement terrain), la_hub_1 (largest prop count), ch_hub_1 (0-disp baseline),
# sp_tutorial_1 (patch-merged staging areas), sp_ninesintro (set piece).
TEST_MAPS = [
    "ch_hub_1", "hw_hub_1", "hw_redspot_1", "la_hub_1",
    "sm_hub_1", "sp_ninesintro", "sp_tutorial_1",
]

# The viewer only loads from tools/out/, so the default output anchors to this
# script's own directory (out/<name> beside bsp_to_scene.py) rather than the current
# working directory - running the exporter from anywhere lands in the right place.
_OUT_ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")

def _export(name_or_path, out_dir=None):
    """Export one map. `name_or_path` is a bare map name (resolved patch-first through
    install.map_path) or an explicit .bsp path (taken as given)."""
    bsp = install.map_path(name_or_path)
    base = os.path.splitext(os.path.basename(bsp))[0]
    out = out_dir if out_dir else os.path.join(_OUT_ROOT, base)
    print(f"map: {bsp}")
    main(bsp, out)

if __name__ == "__main__":
    arg = sys.argv[1] if len(sys.argv) > 1 else "ch_hub_1"
    if arg in ("--all", "all"):
        failed = []
        for i, name in enumerate(TEST_MAPS):
            print(f"\n===== [{i+1}/{len(TEST_MAPS)}] {name} =====")
            try:
                _export(name)
            except Exception as e:
                print(f"  EXPORT FAILED {name}: {e}")
                failed.append(name)
        print(f"\nall: {len(TEST_MAPS)-len(failed)}/{len(TEST_MAPS)} exported"
              + (f"; failed: {', '.join(failed)}" if failed else ""))
        sys.exit(1 if failed else 0)
    # Single map. An explicit out_dir (argv[2]) is honoured as-is, relative to CWD.
    _export(arg, sys.argv[2] if len(sys.argv) > 2 else None)
