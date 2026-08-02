"""Export a VtMB BSP to a textured OBJ+MTL scene with decoded PNG textures.

UE_* exporter: verified to emit **Unreal-native** intermediates directly — all
geometry and sidecars are in Unreal space (centimetres, Z-up, left-handed), with
triangle winding pre-reversed, so the C++ runtime reads every file 1:1 with no
coordinate conversion.

Pulls together everything we reverse-engineered:
  * geometry  : v17 dface_t (104B) -> surfedges -> edges -> vertexes
  * materials : face -> texinfo(72B) -> texdata(32B) -> string table -> name
  * UVs       : planar projection of each vertex onto texinfo s/t axes
  * textures  : material name -> VMT ($basetexture) -> TTH/TTZ -> DXT -> PNG

TOOLS/* faces (nodraw, clip, trigger, skybox, hint...) are skipped - they are
invisible engine surfaces, not geometry to draw.
"""
import struct, os, re, json
import numpy as np
from elysium_pipeline.formats import install, vmt
from elysium_pipeline.formats import bsp as B
from elysium_pipeline.formats import mdl as MDL
from elysium_pipeline.formats import phy
from elysium_pipeline.formats.glass import derive_normal as derive_glass_normal, is_glass
from elysium_pipeline.enhancement import retex_dds
from elysium_pipeline.formats.tex_to_png import (
    decode as decode_texture, decode_cubemap, dudv_to_normal,
    reflectivity as tth_reflectivity,
)
from elysium_pipeline.formats.bsp import (read_lump, source_to_unreal, source_dir_to_unreal, source_angles_to_unreal_quat,
                 strings_from_blob,
                 read_pakfile, read_game_lump, INCH_TO_CM, FACE_SIZE, FE_OFS, NE_OFS,
                 TI_OFS, DISP_OFS, TEXINFO_SIZE, TI_SAXIS, TI_TAXIS, TI_TEXDATA,
                 TEXDATA_SIZE, TD_NAMEID, TD_W, TD_H)

# --- the 3D skybox: what belongs to the miniature rather than the playable world ---

class SkyScope:
    """Which of a map's content is 3D-skybox miniature, and the transform that places it.

    The engine's own membership rule, not a proxy for it: `Draw3dSkyboxworld` builds an
    area-bit vector holding only `m_skybox3d.area` and hands it to the ordinary world +
    renderable draw path, so the pass sees exactly one BSP area's leaves and everything the
    client leaf system holds in them -- world brushes, static props, brush entities, sprites,
    particles, animating props (`../docs/vtmb/sky-ambience.md` -> "The 3D skybox ... (RE-A8)").
    So the test is `area(point_leaf(x)) == area(point_leaf(sky_camera.origin))`, applied to
    every content class alike.

    The placement transform is `world(v) = scale * (v - origin)`, `scale` an integer keyvalue
    that reads 16 on all 43 maps that have a `sky_camera`.

    **Two `sky_camera`s** (only `la_malkavian_4` in the whole game): the first by entity-lump
    order wins -- the engine's own `FindEntityByName(NULL, ...)` first-match rule, the same one
    the rope chains and VRAD's sky-ambient resolution follow.
    """

    def __init__(self, data, ents):
        self.ok = False
        self.faces = set()
        self.scale = 16.0
        self.origin_src = None
        self.areas = None
        self._nodes = None
        self._planes = None
        self.area = -1
        self._model_bbox = {}

        blocks = [m.group(0) for m in re.finditer(r"\{[^{}]*\}", ents)
                  if '"sky_camera"' in m.group(0)]
        if not blocks:
            return
        if len(blocks) > 1:
            print(f"  ! {len(blocks)} sky_camera entities; taking the first (entity-lump order)")
        o = re.search(r'"origin"\s+"(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)"', blocks[0])
        if not o:
            return
        self.origin_src = tuple(float(x) for x in o.groups())
        s = re.search(r'"scale"\s+"(-?[\d.]+)"', blocks[0])
        if s:
            # CSkyCamera.scale is FIELD_INTEGER, so a fractional authored value truncates.
            self.scale = float(int(float(s.group(1)))) or 16.0

        # Pre-read the node tree once: point_leaf walks it per classified point, and a map
        # classifies every prop, entity and worldlight it has.
        self._nodes = read_lump(data, 5)
        self._planes = np.frombuffer(read_lump(data, 1), dtype=np.float32).reshape(-1, 5)
        self.areas = B.leaf_areas(data)
        self.area = self.area_of(self.origin_src)
        self.faces = B.area_faces(data, self.areas, self.area)
        self.ok = True

        # dmodel_t (48 B): mins[3]f maxs[3]f origin[3]f headnode i firstface i numfaces i.
        # A brush entity's classification point is its model bbox centre PLUS its own `origin`
        # key: vbsp re-centres the brushes of an entity that carries one, so the stored bbox is
        # model-local (the sky's func_rotating ferris wheel sits at (0, 0, -15)).
        models_l = read_lump(data, 14)
        for mi in range(len(models_l) // 48):
            mins = struct.unpack_from("<3f", models_l, mi * 48)
            maxs = struct.unpack_from("<3f", models_l, mi * 48 + 12)
            self._model_bbox[mi] = tuple((mins[k] + maxs[k]) * 0.5 for k in range(3))

    def area_of(self, src_pt):
        # point_leaf reads `data` only when nodes/planes are absent, and both are supplied.
        li = B.point_leaf(None, src_pt, self._nodes, self._planes)
        return int(self.areas[li]) if 0 <= li < len(self.areas) else -1

    def is_sky(self, src_pt):
        """Is this Source-space point inside the miniature's BSP area?"""
        return bool(self.ok) and src_pt is not None and self.area_of(src_pt) == self.area

    def entity_point(self, origin_src, model_key=""):
        """The Source-space point an entity classifies by.

        A point entity classifies by its own `origin`. A **brush** entity (`model` `*N`)
        classifies by its model's bbox centre plus that origin, because vbsp re-centres the
        brushes of an entity that carries an origin -- the stored bbox is model-local, so the
        bbox alone would put the sky's ferris wheel at (0, 0, -15) and the origin alone would
        put a brush entity with no origin key at the world origin.
        """
        m = re.match(r"\*(\d+)$", (model_key or "").strip())
        if m:
            c = self._model_bbox.get(int(m.group(1)))
            return None if c is None else tuple(origin_src[k] + c[k] for k in range(3))
        return origin_src


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

def write_collision(data, out_dir, base, sky=None):
    """Emit `<base>.hulls`: one world brush per line as flat Unreal-space verts (cm).

    The 3D-skybox miniature's own brushes are dropped: the miniature is backdrop the player can
    never reach, and its geometry is drawn at `scale x (v - origin)` while a hull would collide
    at the raw miniature coordinates it was authored at -- an invisible wall standing where
    nothing is drawn. A brush is classified by its hull's centroid, the same BSP-area test every
    other content class uses."""
    nodes, leafs = read_lump(data, 5), read_lump(data, 10)
    leafbrushes, brushes, sides = read_lump(data, 17), read_lump(data, 18), read_lump(data, 19)
    planes = np.frombuffer(read_lump(data, 1), dtype=np.float32).reshape(-1, 5)[:, :4].copy()
    world = _model_brushes(nodes, leafs, leafbrushes, 0)
    n = n_sky = 0
    with open(os.path.join(out_dir, base + ".hulls"), "w") as f:
        for bi in sorted(world):
            cont, pts = _brush_hull(planes, sides, brushes, bi)
            if pts is None or not (cont & BLOCK_MASK):
                continue
            if sky is not None and sky.is_sky(tuple(np.asarray(pts).mean(axis=0))):
                n_sky += 1
                continue
            uniq = {(round(p[0], 1), round(p[1], 1), round(p[2], 1)): p for p in pts}
            g = [source_to_unreal(sx, sy, sz) for sx, sy, sz in uniq.values()]
            f.write(" ".join(f"{c:.4f}" for v in g for c in v) + "\n")
            n += 1
    print(f"collision hulls: {n} world brushes ({n_sky} skybox brushes dropped) -> {base}.hulls")

# --- real-time light rig: WORLDLIGHTS (lump 15) -> `<base>.lights` -----------

def write_lights(data, out_dir, base, sky=None):
    """Emit `<base>.lights`: one WORLDLIGHTS source per line, in Unreal space, for
    the runtime LightRig to spawn an Unreal light per source (the real-time lighting
    model that replaces the baked lightmap). Intensity stays raw linear RGB - the
    runtime splits it into a normalized colour and a scalar energy.

    Line: type ox oy oz  dx dy dz  ir ig ib  radius_cm  stopdot stopdot2 exponent  style  sky
      type      0 emit_surface, 1 point, 2 spot, 3 skylight, 5 skyambient
      o*        origin, Unreal centimetres  (sx,-sy,sz)*INCH_TO_CM
      d*        beam direction, Unreal unit vector (nx,-ny,nz); 0 0 0 if none
      i*        intensity, raw linear RGB (colour*brightness). Six decimals, not three: the
                type-5 skyambient row IS a lump-8 luxel value / 255 and runs as low as 0.005
                (C1), so three would quantise the sky's whole ambient level to one figure.
      radius_cm cutoff radius in centimetres (0 = no cutoff)
      stopdot/stopdot2/exponent  spot cone (cos inner / cos outer / falloff exp)
      style     lightstyle index (0 = constant)
      sky       1 = the source sits inside the 3D-skybox miniature's BSP area

    A `sky` source never lit the playable world -- the miniature is a separate render pass at
    1/scale in a corner of the map -- but it was not dead data either: VtMB's light cache reads
    lump 15 to light the miniature's props (`../docs/vtmb/sky-ambience.md` -> "K3 / K5"). So the flag
    means "belongs to the miniature", and the consumers carry it into the sky transform rather
    than deleting it. It also takes those sources out of the fill-vs-fixture sample, which had
    been measuring lights placed at miniature coordinates as if they lit the map."""
    wl = B.read_worldlights(data)
    n_sky = 0
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
            # The sun and the skyambient are directionless global terms, not placed sources, so
            # they are never miniature content whatever leaf their row's origin happens to fall in.
            in_sky = int(w["type"] not in (3, 5) and sky is not None and sky.is_sky(w["origin"]))
            n_sky += in_sky
            f.write(f"{w['type']} {ox:.4f} {oy:.4f} {oz:.4f} "
                    f"{ux:.4f} {uy:.4f} {uz:.4f} "
                    f"{ir:.6f} {ig:.6f} {ib:.6f} {w['radius'] * INCH_TO_CM:.4f} "
                    f"{w['stopdot']:.4f} {w['stopdot2']:.4f} {w['exponent']:.3f} "
                    f"{w['style']} {in_sky}\n")
    from collections import Counter
    tc = dict(sorted(Counter(w["type"] for w in wl).items()))
    print(f"lights: {len(wl)} worldlights ({n_sky} in the sky area) -> {base}.lights  by type {tc}")

# --- env_sprite coronas: glow billboards at light sources -------------------

def write_sprites(data, out_dir, base, idx, sky=None):
    """Emit `<base>.sprites`: env_sprite glow billboards (the soft coronas VtMB places at
    lamps/bulbs). Each sprite's Sprite VMT `$basetexture` is decoded to `tex/spr_*.png`;
    one line per sprite: `png ox oy oz w_cm h_cm r g b amt orient sky`, where the world size is
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

    lines, n_sky = [], 0
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
        # The moon and the lit-window glows are miniature content on every Santa Monica map
        # (54 of sm_hub_1's sprites), so they carry the same sky flag everything else does.
        in_sky = int(sky is not None and sky.is_sky((float(o[0]), float(o[1]), float(o[2]))))
        n_sky += in_sky
        lines.append(f"tex/{png} {ux:.4f} {uy:.4f} {uz:.4f} "
                     f"{scale*w*INCH_TO_CM:.4f} {scale*h*INCH_TO_CM:.4f} "
                     f"{r} {g} {bb} {amt} {orient} {in_sky}")

    if lines:
        with open(os.path.join(out_dir, base + ".sprites"), "w") as f:
            f.write("\n".join(lines) + "\n")
    print(f"sprites: {len(lines)} env_sprite coronas ({n_sky} sky, {len(tex_cache)} textures) "
          f"-> {base}.sprites")

def write_ropes(data, out_dir, base, idx):
    """Emit `<base>.ropes`: the overhead cables VtMB strings between poles/buildings.

    A rope is a chain of `move_rope`/`keyframe_rope` nodes linked by `NextKey` (a node's
    `NextKey` = the targetname of the next node). Both classnames construct the *same*
    `CRopeKeyframe` (vampire.dll factories `0x1019d680`/`0x1019d6f0`), so the roles are
    resolved topologically here, not by classname. Each consecutive pair is one cable
    segment; the runtime (roadmap 8.7) builds one `UCableComponent` per line.

    One line per segment, 14 whitespace-separated tokens:
      `tex ax ay az bx by bz width_cm rest_cm nodes texscale flags bump matflags`

    `tex`/`bump` are the decoded rope material PNGs (`tex/rope_*.png`, `tex/rope_*_n.png`) or `-`
    when absent or undecodable, `a`/`b` are the two node origins (Unreal cm, source_to_unreal like
    `.ents`), and the segment parameters come from the *start* node A.

    The parameters are the RE'd `CRopeKeyframe` state, not the raw keyvalues:

    - `width_cm` — `Width`, inches -> cm (ctor default 2).
    - `rest_cm` — the **simulated rest length** of the strand, i.e. the total length of rope
      the solver has to place between the two endpoints. Any surplus over the straight span
      is the entire sag, so this is the one number the look depends on. It is *not* the raw
      `Slack`; it comes out of a two-stage computation split across the server and client
      halves of `CRopeKeyframe` (all offsets from its datamap at `0x1019d8d0`):

        `m_RopeLength = (int)|B-A| + m_Slack`     RopeThink,        vampire.dll 0x1019efb0
        `springDist   = (m_RopeLength + m_Slack - 100) / (nodes - 1)`
                                                  RecomputeSprings, client.dll  0x100bf1a0
        `m_flSpringDist = max(0, springDist)`      ResetSpringLength, client.dll 0x10128ae0
        `rest = springDist * (nodes - 1)`

      Three things fall out of that and none are guessable from the keyvalues: `Slack` is
      applied **twice** (once server-side into `m_RopeLength`, again client-side); a flat
      **-100 units** is then subtracted; and the divide is an *integer* one, so the per-segment
      length truncates. The -100 dominates at VtMB's scale — authored `Slack` is 0..100 across
      the whole game, so most ropes come out at or below their straight span and hang taut.
      The `max(0, ...)` floor lets a short rope collapse to a dead-straight chord.
    - `nodes` — the simulated node count `m_nSegments`. It comes from the **`Type`** keyvalue,
      not `Subdiv`: `CRopeKeyframe::KeyValue` (`0x1019f2b0`) maps Type 0 -> 10, 1 -> 4,
      anything else -> 2, and `Activate` (`0x1019e310`) clamps it to [2, 10]. With no `Type` at
      all the ctor default 5 stands. A `Type 2` rope therefore has **two** nodes — one straight
      segment between two locked points, which cannot sag at all. `Subdiv` is the *client-side
      render* tessellation between those nodes (capped by the `rope_subdiv` cvar in client.dll),
      not the simulation resolution, so it is not emitted.
    - `texscale` — `TextureScale`, the along-length tiling factor (ctor default 4, datamap-
      clamped to [0.1, 10]).
    - `flags` — the RE'd bits `KeyValue` sets: 1 = `Dangling` (clears `ROPE_LOCK_END_POINT` in
      `m_fLockedPoints`, so the far end hangs free), 2 = `Collide`, 4 = `Barbed`, 8 = `Breakable`.
    - `matflags` — the rope VMT's shader mode, so the runtime instances the right world master
      rather than assuming opaque: 1 = `$alphatest`, 2 = `$translucent`, 4 = `$envmap`. This is
      load-bearing for chains — `cable/chain` and `cable/chainb` are `$alphatest 1` over a texture
      that is ~47% cut out (the gaps between the links), so rendering them opaque turns a chain
      into a solid tube with a chain painted on it. Their `$envmap` mask is the normal map's alpha
      (`$normalmapalphaenvmapmask`), which needs no separate texture.

    `RopeShader` (0/1/2 -> `cable/cable`, `cable/rope`, `cable/chain`) overrides `RopeMaterial`
    when present, matching `KeyValue`. `MoveSpeed`/`MoveTime`/`Tension`/`PositionInterpolator`
    are keyframe-path behaviour the rope renderer ignores -> rendered at rest.
    """
    blocks = _parse_ent_blocks(read_lump(data, 0).decode("ascii", "replace"))
    pak = read_pakfile(data)

    def read_bytes(key):
        key = key.lower()
        return pak[key] if key in pak else install.read(idx, key)

    def read_text(key):
        b = read_bytes(key)
        return b.decode("ascii", "replace") if b is not None else None

    tex_cache = {}   # rope material name -> png filename | None

    # RopeShader index -> material, from CRopeKeyframe::KeyValue (0x1019f2b0).
    ROPE_SHADER = {0: "cable/cable", 1: "cable/rope", 2: "cable/chain"}
    # Type -> m_nSegments, same function; every other value falls to 2. With no Type key the
    # ctor default (0x1019dc80: `MOV [ESI+0x468], 5`) stands.
    TYPE_NODES = {0: 10, 1: 4}
    DEFAULT_NODES = 5
    # The flat shortening RecomputeSprings applies, in Source units (client.dll 0x100bf1a1:
    # `LEA EAX,[EAX + EDX*0x1 + -0x64]`).
    SLACK_FUDGE = -100
    # `matflags` bits — the rope VMT's shader mode, mirroring the .mtl's illum 4 / blend / envmap.
    MAT_MASKED, MAT_TRANSLUCENT, MAT_ENVMAP = 1, 2, 4

    def decode_rope_png(stem, key):
        """Decode one TTH/TTZ pair to `tex/<stem>.png`, RGBA. Returns the filename or None."""
        tth, ttz = read_bytes(f"materials/{key}.tth"), read_bytes(f"materials/{key}.ttz")
        if not (tth and ttz):
            return None
        try:
            img = decode_texture(tth, ttz).convert("RGBA")
        except Exception:
            return None
        fn = stem + ".png"
        img.save(os.path.join(out_dir, "tex", fn))
        return fn

    def decode_rope_mat(mat):
        """Rope material -> (albedo_png, bump_png, matflags). Both PNGs may be None.

        The rope VMTs are not plain opaque: `cable/chain` and `cable/chainb` are `$alphatest 1`
        with a texture that is ~47% cut out (the gaps between the links), and every one of them
        carries a `$bumpmap`. Dropping those renders a chain as a solid tube with a chain painted
        on it, so the shader flags travel with the segment.
        """
        if mat not in tex_cache:
            albedo = bump = None
            flags = 0
            vmt_txt = read_text(f"materials/{mat}.vmt")
            if vmt_txt:
                info = vmt.parse(vmt_txt, resolve_include=lambda p: read_text(
                    p if p.lower().endswith(".vmt") else p + ".vmt"))
                stem = "rope_" + sanitize(mat)
                if info.get("basetexture"):
                    albedo = decode_rope_png(stem, info["basetexture"])
                if info.get("bumpmap"):
                    bump = decode_rope_png(stem + "_n", info["bumpmap"])
                flags = ((MAT_MASKED if info.get("alphatest") else 0)
                         | (MAT_TRANSLUCENT if info.get("translucent") else 0)
                         # $envmap on a rope is always `env_cubemap` and the mask is the normal
                         # map's alpha ($normalmapalphaenvmapmask), so there is no separate mask
                         # texture to emit — the runtime's uniform-envmap path covers it.
                         | (MAT_ENVMAP if info.get("envmap") else 0))
            tex_cache[mat] = (albedo, bump, flags)
        return tex_cache[mat]

    # Collect every rope node, and index by targetname for NextKey lookup. A node may itself lack a
    # targetname (it can only be a chain *start* then, never a NextKey target) — so iterate all nodes
    # as potential segment starts, but resolve B through the name index.
    #
    # A targetname can **repeat across separate wire installations** (sp_tutorial_1 reuses tele4..tele9
    # in two areas ~200 m apart). The engine (CRopeKeyframe::Activate in vampire.dll — a stock Source
    # rope) resolves NextKey with FindEntityByName(NULL, name), i.e. the **first** entity of that name
    # in spawn/entity order — which is the entity-lump order, i.e. the order these blocks appear. So the
    # index is **first-wins** (not last-wins, which cross-linked the two installations into 200 m
    # cables, and not a nearest-position heuristic — though on the tutorial first-wins and nearest give
    # the identical result, because the lump orders each installation's nodes contiguously).
    rope_nodes = []
    by_name = {}   # targetname -> FIRST node with that name (entity-lump order = engine spawn order)
    for b in blocks:
        d = {k.lower(): v for k, v in b}
        if d.get("classname") in ("keyframe_rope", "move_rope"):
            rope_nodes.append(d)
            tn = d.get("targetname", "")
            if tn and tn not in by_name:
                by_name[tn] = d

    def atof(s, default=0.0):
        """C `atof` semantics: parse the leading numeric prefix, ignore the rest.

        Hammer wrote a few origins with a comma decimal separator (`hw_jewelry_1`'s
        chandelier ropes are `"-3496,92 -3147,1 140"`). The engine reads those through
        `atof`, which stops at the comma -> -3496 / -3147; a bare `float()` raises and
        takes the whole map export down with it."""
        m = re.match(r"\s*[-+]?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?", s or "")
        return float(m.group(0)) if m else float(default)

    def origin_of(d):
        o = d.get("origin", "0 0 0").split()
        return source_to_unreal(*(atof(c) for c in o)) if len(o) == 3 else None

    def fnum(d, key, default):
        v = d.get(key, "")
        return atof(v, default) if v else float(default)

    lines = []
    dangling = []
    for a in rope_nodes:
        nk = a.get("nextkey", "")
        if not nk:
            continue                          # chain end -> no outgoing segment
        b = by_name.get(nk)                    # engine's FindEntityByName(NULL,...): first of that name
        if b is None:
            # The map itself names a node that does not exist (a mapper typo — 122 of these
            # across the 108 maps). The engine warns and draws nothing; so do we, but say so
            # rather than dropping it silently, because it looks identical to a linking bug.
            dangling.append(f"{a.get('targetname', '?')}->{nk}")
            continue
        pa, pb = origin_of(a), origin_of(b)
        if pa is None or pb is None:
            continue
        # Coincident nodes (a chain artifact — same origin) make a zero-length cable that renders
        # nothing; drop it so every emitted segment has a real span.
        if sum((x - y) ** 2 for x, y in zip(pa, pb)) < 1.0:   # < 1 cm apart
            continue
        if "ropeshader" in a:
            mat = ROPE_SHADER.get(int(fnum(a, "ropeshader", "0")), "cable/cable")
        else:
            mat = (a.get("ropematerial") or "cable/cable").replace("\\", "/").lower()
        png, bump_png, matflags = decode_rope_mat(mat)
        width_cm = fnum(a, "width", "2") * INCH_TO_CM
        nodes = (max(2, min(10, TYPE_NODES.get(int(fnum(a, "type", "0")), 2)))
                 if "type" in a else DEFAULT_NODES)
        # Rest length, in Source units throughout — the engine's arithmetic is integral and
        # the truncation is load-bearing, so do it before converting to cm (see the docstring).
        span_u = sum((x - y) ** 2 for x, y in zip(pa, pb)) ** 0.5 / INCH_TO_CM
        slack_u = int(fnum(a, "slack", "0"))
        rope_len_u = int(span_u) + slack_u
        # C integer division truncates toward zero, so int(x / y), not x // y.
        spring_u = max(0, int((rope_len_u + slack_u + SLACK_FUDGE) / (nodes - 1)))
        rest_cm = spring_u * (nodes - 1) * INCH_TO_CM
        texscale = min(10.0, max(0.1, fnum(a, "texturescale", "4")))
        flags = ((1 if fnum(a, "dangling", "0") else 0)
                 | (2 if fnum(a, "collide", "0") else 0)
                 | (4 if fnum(a, "barbed", "0") else 0)
                 | (8 if fnum(a, "breakable", "0") else 0))
        lines.append(f"{('tex/' + png) if png else '-'} "
                     f"{pa[0]:.4f} {pa[1]:.4f} {pa[2]:.4f} {pb[0]:.4f} {pb[1]:.4f} {pb[2]:.4f} "
                     f"{width_cm:.4f} {rest_cm:.4f} {nodes} {texscale:.4f} {flags} "
                     f"{('tex/' + bump_png) if bump_png else '-'} {matflags}")

    if lines:
        with open(os.path.join(out_dir, base + ".ropes"), "w") as f:
            f.write("\n".join(lines) + "\n")
    print(f"ropes: {len(lines)} cable segments ({len(rope_nodes)} nodes, "
          f"{sum(1 for v in tex_cache.values() if v)} textures) -> {base}.ropes")
    if dangling:
        print(f"  ! {len(dangling)} NextKey name(s) match no rope node (map data): "
              f"{', '.join(dangling[:6])}{' ...' if len(dangling) > 6 else ''}")

# --- entities: the Source I/O layer (docs/vtmb/entity_io.md) ---------------------

def _parse_ent_blocks(text):
    """ENTITIES lump text -> [[(key, value), ...]], order and repeats preserved
    (an entity may carry several outputs on the same key)."""
    return [re.findall(r'"([^"]*)"\s+"([^"]*)"', b)
            for b in re.findall(r"\{([^{}]*)\}", text, re.S)]


def _split_output(value):
    """target,input,param,delay,times[,python[,extra]] -> dict, or None.

    VtMB writes 7 comma fields where Source writes 5; field 5 is a Python call
    string the engine evaluates as `__main__.<expr>`. See docs/vtmb/entity_io.md."""
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


def decode_prop_models(idx, model_paths, propdir, tex_cache, valid):
    """Decode each unique `.mdl` in `model_paths` into `propdir/<safe>.obj` (Unreal
    space, winding reversed) sharing `tex_cache`, and return `{model_path: safe}` for
    the models that decoded. A model whose stem is already in `valid` (decoded earlier
    this run, e.g. by the other prop path) is reused, not re-decoded; each new decode
    adds its stem to `valid`. Prints one line per failure. Shared by the GAME_LUMP
    static-prop path (`write_props`) and the `.ents`-referenced prop path
    (`write_entities`) so a model referenced by both decodes once into one `props/` dir."""
    os.makedirs(propdir, exist_ok=True)
    read_bytes = lambda key: install.read(idx, key)
    resolved, ok, missing = {}, 0, 0
    for model_path in sorted(set(model_paths)):
        stem = model_path[:-4] if model_path.endswith(".mdl") else model_path
        safe = MDL.sanitize(stem)
        if safe in valid:                       # already decoded this run
            resolved[model_path] = safe; ok += 1; continue
        dv = MDL.load(idx, model_path)
        if not dv:
            missing += 1; continue
        try:
            meshes = MDL.decode(*dv)
            MDL.write_obj_scene(meshes, safe, propdir, MDL.search_paths(dv[0]), read_bytes,
                                tex_cache, skins=MDL.skin_families(dv[0]))
            valid.add(safe); resolved[model_path] = safe; ok += 1
        except Exception as e:
            print(f"  prop decode failed {model_path}: {e}"); missing += 1
    return resolved, ok, missing


def write_entities(data, out_dir, base, idx, propdir, tex_cache, valid, sky=None,
                   brush_meshes=None):
    """Emit `<base>.ents` (JSON): every entity's keyvalues + outputs, and for brush
    entities ("model" "*N") their brush volumes as convex hulls.

    An entity inside the 3D-skybox miniature's BSP area is annotated `"sky": true` — the moon
    and window-glow `env_sprite`s, the `logic_timer`s that blink them, the cloud-plane
    `prop_dynamic`s, the pier's `func_rotating` ferris wheel. It stays a live entity with its
    real I/O; only its *placement* moves into the sky transform, so the wheel still turns and
    the glows still blink (`../docs/vtmb/sky-ambience.md` -> B7, owner call 2026-07-26).

    Entities carrying a static `.mdl` `model` key (prop_dynamic/prop_physics and the
    prop_button/prop_doorknob/prop_sign/prop_switch/prop_hacking/item_container family)
    also get that model decoded into `props/` (shared with the GAME_LUMP static props)
    and the entity annotated with `model_mesh` = the decoded OBJ stem plus `model_quat` =
    the Unreal-space placement rotation (source_angles_to_unreal_quat of the entity's
    `angles`, so the runtime reads orientation verbatim like it does origin). Skeletal
    `npc_*` models are excluded — they belong to the glTFRuntime NPC track (roadmap 8.2/8.5).

    This is the data the runtime needs to spawn the interaction layer - trigger
    volumes, use volumes, doors. Tools-only surfaces remain collision/I/O records
    without a mesh. StartHidden renderable brushes keep their mesh annotation because
    visibility is a reversible runtime state.

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

    brush_meshes = brush_meshes or {}
    out, n_brush, n_hull, n_out, n_sky = [], 0, 0, 0, 0
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

        # 3D-skybox membership, by the same BSP-area test every other content class uses.
        if sky is not None:
            pt = sky.entity_point(so, keys.get("model", ""))
            if pt is not None and sky.is_sky(pt):
                e["sky"] = True
                n_sky += 1

        # phys_hinge (and the phys_* constraint family): `hingeaxis` is a second Source point;
        # the hinge axis is the line origin->hingeaxis. Pre-convert its *direction* to Unreal
        # here (runtime reads verbatim, never converts) — source_dir_to_unreal of the delta,
        # normalized. Coincident points -> world Z. (roadmap 8.4; keys are raw Source.)
        ha = keys.get("hingeaxis", "").split()
        if len(ha) == 3 and len(og) == 3:
            d = np.array(source_dir_to_unreal(float(ha[0]) - so[0], float(ha[1]) - so[1], float(ha[2]) - so[2]))
            nrm = float(np.linalg.norm(d))
            e["hinge_axis"] = [round(float(c), 6) for c in (d / nrm)] if nrm > 1e-6 else [0.0, 0.0, 1.0]

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
                if mi in brush_meshes:
                    e["brush_mesh"] = brush_meshes[mi]
                n_brush += 1
                n_hull += len(hulls)

        # CFuncElevator's floor table is an array of absolute Source Z coordinates. Convert it
        # here, alongside origin/hulls, so the runtime never performs Source->Unreal math.
        if e["classname"].lower() == "func_elevator":
            floors = []
            for floor in range(1, 9):
                try:
                    src_z = float(keys.get("floor%d" % floor, "0"))
                except ValueError:
                    src_z = 0.0
                floors.append(round(float(source_to_unreal(0.0, 0.0, src_z)[2]), 5))
            e["elevator_floors"] = floors

        # StartHidden: the entity spawns fully OFF - SOLID_NONE, think disabled and
        # undrawn - until a ScriptUnhide input restores it (docs/vtmb/entity_io.md).
        e["start_hidden"] = keys.get("StartHidden", "0") == "1"
        if outputs:
            e["outputs"] = outputs
        e["keys"] = keys
        out.append(e)

    # Static-mesh props referenced by entities: decode each model once into props/ and
    # annotate the entity with `model_mesh` (the decoded OBJ stem). Filter to `.mdl`
    # `model` keys (skips brush "*N" and sprite "materials/*.vmt") and drop skeletal
    # npc_* (the NPC/glTFRuntime track owns those).
    ent_model_of = {}                            # index in `out` -> model_path
    model_paths = set()
    for i, e in enumerate(out):
        if e["classname"].lower().startswith("npc_"):
            continue
        mk = e["keys"].get("model", "").replace("\\", "/").lower()
        if mk.endswith(".mdl"):
            model_paths.add(mk); ent_model_of[i] = mk
    resolved, pok, pmiss = decode_prop_models(idx, model_paths, propdir, tex_cache, valid)
    n_prop = 0
    phys_models = {}                            # 8.4: stem -> .mdl key, for the .phy sidecar
    for i, mk in ent_model_of.items():
        safe = resolved.get(mk)
        if safe:
            out[i]["model_mesh"] = safe; n_prop += 1
            if out[i]["classname"].lower() == "prop_physics":
                phys_models[safe] = mk
            # Pre-convert the entity's Source QAngle to an Unreal rotation quaternion here (the
            # same source_angles_to_unreal_quat the .props path uses), so the runtime reads it 1:1
            # with no coordinate math — origin is already Unreal-space, this makes orientation so
            # too. Absent/short `angles` -> identity. (roadmap 8.3; keys.angles is raw Source.)
            ang = out[i]["keys"].get("angles", "").split()
            pyr = [float(x) for x in ang] if len(ang) == 3 else [0.0, 0.0, 0.0]
            out[i]["model_quat"] = [round(float(c), 6) for c in source_angles_to_unreal_quat(*pyr)]

    # 8.4: emit each prop_physics model's own VPhysics collision -- the convex hulls VtMB
    # simulates against, straight out of the model's sibling `.phy` -- as `<stem>.phys`.
    phy.write_physics_phys(idx, propdir, phys_models)

    path = os.path.join(out_dir, base + ".ents")
    with open(path, "w") as f:
        json.dump({"map": base, "entities": out}, f, separators=(",", ":"))
    print(f"entities: {len(out)} ({n_brush} brush, {n_hull} hulls, {n_out} outputs, "
          f"{n_sky} sky) -> {base}.ents")
    print(f"entity props: {n_prop} placed / {len(model_paths)} models "
          f"({pok} decoded, {pmiss} missing)")


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
def write_props(data, out_dir, base, idx, propdir, tex_cache, valid, sky=None):
    """Parse GAME_LUMP sprp (VtMB v4, 56B DStaticPropV4): a model-name dict + per-
    prop origin/angles/solid/skin. Decode each unique model once (shared texture cache)
    into out_dir/props/<safename>.obj (Unreal space), and write <base>.props (one prop
    per line: `safename ox oy oz qx qy qz qw solid skin sky`). `solid` (0 = non-solid) gates
    collision; `skin` names an alternate skin family from the model's own skin table
    (props/<stem>.skins), which the bake applies as material overrides on the placed actor;
    `sky` marks a prop that belongs to the 3D-skybox miniature, which the bake places under
    the sky transform instead of in the playable world (roofline cutouts, cloud planes, the
    pier ferris wheel -- 1,043 props game-wide).

    Unreal-native: the prop meshes are written via mdl.write_obj_scene
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

    props = []          # (model_path, origin, angles, solid, skin)
    model_paths = set()
    for i in range(prop_count):
        po = p + i * size
        origin = struct.unpack_from("<3f", payload, po)
        angles = struct.unpack_from("<3f", payload, po + 12)
        prop_type = struct.unpack_from("<H", payload, po + 24)[0]
        solid = struct.unpack_from("<B", payload, po + 30)[0]
        # DStaticPropV4.skin @32 -- the alternate skin family this placement draws (157 of the
        # install's 6,470 placed props use one). Applied offline by the bake: a GAME_LUMP prop is
        # not an entity, so its skin never changes at runtime.
        skin = struct.unpack_from("<i", payload, po + 32)[0]
        if prop_type >= len(names):
            continue
        model_path = names[prop_type].replace("\\", "/").lower()
        model_paths.add(model_path)
        props.append((model_path, origin, angles, solid, skin))

    resolved, ok, missing = decode_prop_models(idx, model_paths, propdir, tex_cache, valid)
    solid_n = skin_n = sky_n = 0
    with open(os.path.join(out_dir, base + ".props"), "w") as f:
        for model_path, (ox, oy, oz), (pitch, yaw, roll), solid, skin in props:
            safe = resolved.get(model_path)
            if safe is None:                 # an .obj left over from an earlier export
                continue                     # must not stand in for a failed decode
            solid_n += solid != 0
            skin_n += skin != 0
            in_sky = int(sky is not None and sky.is_sky((ox, oy, oz)))
            sky_n += in_sky
            ux, uy, uz = source_to_unreal(ox, oy, oz)
            qx, qy, qz, qw = source_angles_to_unreal_quat(pitch, yaw, roll)
            f.write(f"{safe} {ux:.4f} {uy:.4f} {uz:.4f} "
                    f"{qx:.6f} {qy:.6f} {qz:.6f} {qw:.6f} {solid} {skin} {in_sky}\n")
    placed = sum(1 for pr in props if pr[0] in resolved)
    print(f"props: {placed} placed ({solid_n} solid, {skin_n} skinned, {sky_n} sky) / "
          f"{len(model_paths)} models ({ok} decoded, {missing} missing) -> {base}.props")


def main(bsp_path, out_dir, *, index=None):
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
    # A profile export builds this expensive patch-first index once and shares it
    # across every map.  Direct library callers may still omit it.
    idx = index if index is not None else install.build_index()
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

    # The 3D skybox is one BSP area's worth of content -- the engine's own membership rule,
    # applied here to every content class alike (world faces, static props, entities and
    # worldlights), not just to the backdrop geometry. See SkyScope.
    ents = read_lump(data, 0).decode("ascii", "replace")
    sky = SkyScope(data, ents)
    sky_anchor = sky.origin_src
    sky_scale = sky.scale
    sky_faces = sky.faces
    if sky.ok:
        print(f"3D skybox: area {sky.area}, {len(sky_faces)} world faces, scale {sky_scale:g}")
    else:
        print("3D skybox: no sky_camera - every face exports as world")

    # Brush-entity geometry is emitted as one LOCAL-space mesh per BSP model and excluded from
    # the static world. The runtime attaches that mesh to the entity's collision body, whose
    # origin is the door hinge / mover pivot. Model 0 remains the static world.
    # The decal projector pass below still needs each face's authored world offset.
    model_origin = {}
    for m in re.finditer(r"\{[^{}]*\}", ents):
        blk = m.group(0)
        mo = re.search(r'"model"\s+"\*(\d+)"', blk)
        if not mo:
            continue
        og = re.search(r'"origin"\s+"(-?[\d.]+) (-?[\d.]+) (-?[\d.]+)"', blk)
        if og:
            model_origin[int(mo.group(1))] = tuple(float(x) for x in og.groups())
    models_l = read_lump(data, 14)
    MODEL_SIZE = 48   # dmodel_t: mins[3]f maxs[3]f origin[3]f headnode i firstface i numfaces i
    face_model = {}
    face_offset = {}
    for mi in range(len(models_l) // MODEL_SIZE):
        ff, nf = struct.unpack_from("<2i", models_l, mi * MODEL_SIZE + 40)
        for f in range(ff, ff + nf):
            face_model[f] = mi
            if mi in model_origin:
                face_offset[f] = model_origin[mi]

    # Two scenes: "world" (playable map) and "sky" (3D skybox miniature). Per scene:
    # positions, uvs (albedo), groups (material->tris), and blend (per-vertex
    # WorldVertexTransition alpha, 0..1; 0 for every non-displacement or non-blend
    # vertex). blend rides a `.blend` sidecar and becomes vertex COLOR.r. (Decals are
    # not meshed -- they ride the `.decals` projector sidecar; see the infodecal block.)
    scenes = {"world": ([], [], {}, []), "sky": ([], [], {}, [])}
    brush_scenes = {}

    skipped_tools = 0
    disp_collision = []   # world-space (Unreal cm) disp triangles for concave collision
    # Env-patched materials split into one OBJ group per cubemap: the same base
    # material near two env_cubemaps samples two baked cubemaps, so each becomes its
    # own surface. The group key carries the cubemap ('<mat>@<cube>'); these map it
    # back to the base material (for VMT/texture lookup) and the cubemap stem.
    gkey_base, gkey_cube, gkey_raw = {}, {}, {}
    n_faces = len(faces_l) // FACE_SIZE
    for fi in range(n_faces):
        base = fi * FACE_SIZE
        firstedge = struct.unpack_from("<i", faces_l, base + FE_OFS)[0]
        numedges = struct.unpack_from("<h", faces_l, base + NE_OFS)[0]
        ti = struct.unpack_from("<h", faces_l, base + TI_OFS)[0]
        disp = struct.unpack_from("<h", faces_l, base + DISP_OFS)[0]   # -1 = flat
        if numedges < 3 or ti < 0:
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
        gkey_raw[gkey] = raw_name.lower().replace("\\", "/").strip("/")

        # gather source-space corners first (for UVs and classification)
        src = []
        for k in range(numedges):
            se = surfedges[firstedge + k]
            v = edges[se][0] if se >= 0 else edges[-se][1]
            src.append(vertexes[v])
        mi = face_model.get(fi, 0)
        is_brush = mi > 0
        is_sky = not is_brush and fi in sky_faces
        if is_brush:
            positions, uvs, groups, blend = brush_scenes.setdefault(mi, ([], [], {}, []))
        else:
            positions, uvs, groups, blend = scenes["sky" if is_sky else "world"]

        # Append one vertex (position + planar albedo UV); returns its index. Shared by
        # flat faces and displacement grid vertices. BSP submodel vertices are already
        # brush-local; the entity origin is supplied later by the runtime body.
        def emit(sx, sy, sz, a=0.0):
            u = (sx*s[0] + sy*s[1] + sz*s[2] + s[3]) / tw   # planar UV projection
            vv = (sx*t[0] + sy*t[1] + sz*t[2] + t[3]) / th
            # Model 0 and the sky miniature are already world-authored. Brush models stay
            # entity-local: the runtime body supplies origin/rotation/scale.
            positions.append(source_to_unreal(sx, sy, sz))  # -> Unreal cm
            uvs.append((u, vv))
            blend.append(a)                                 # WVT blend alpha (0 = tex1)
            return len(positions) - 1

        tris = groups.setdefault(gkey, [])
        if 0 <= disp < len(dispinfos) and len(src) == 4:
            gtris = disp_grid(src, dispinfos[disp], dispverts, emit)
            tris.extend(gtris)
            if not is_sky and not is_brush:   # world terrain only; brush hulls own mover collision
                for (a, bb, cc) in gtris:
                    disp_collision.append((positions[a], positions[bb], positions[cc]))
        else:
            corners = [emit(sx, sy, sz) for (sx, sy, sz) in src]   # flat fan
            for k in range(1, len(corners) - 1):
                tris.append((corners[0], corners[k], corners[k+1]))

    all_mats = set(scenes["world"][2]) | set(scenes["sky"][2])
    for scene in brush_scenes.values():
        all_mats.update(scene[2])
    print(f"faces: {n_faces}  materials: {len(all_mats)}  skipped TOOLS: {skipped_tools}"
          f"  brush models: {len(brush_scenes)}")
    print(f"  world groups: {len(scenes['world'][2])}   sky groups: {len(scenes['sky'][2])}")
    # sorted -> deterministic material order in the .mtl/.obj (was set-iteration order)
    groups = {}
    for mat in sorted(all_mats):
        indices = scenes["world"][2].get(mat, []) + scenes["sky"][2].get(mat, [])
        for scene in brush_scenes.values():
            indices += scene[2].get(mat, [])
        groups[mat] = indices

    # --- resolve + decode each material's texture ---
    # (read_material_bytes/read_material_text + the install index and PAKFILE are
    # defined at the top of main, since the bump-lightmap pre-pass needs them too.)
    from PIL import Image, ImageChops
    os.makedirs(os.path.join(out_dir, "tex"), exist_ok=True)
    img_cache = {}       # basetexture -> decoded RGBA PIL image (or None)
    refl_cache = {}      # basetexture -> vtex's average albedo (r,g,b) 0..1, or None
    albedo_cache = {}    # basetexture -> albedo png filename
    emis_cache = {}      # basetexture -> emission png filename
    normal_cache = {}    # normalmap -> normal png filename
    cube_cache = {}      # cube id -> bool (six face PNGs written under tex/cube/)
    mask_cache = {}      # envmapmask source key -> mask png filename (or None)
    mat_info = {}        # gkey -> (albedo_png, emission_png, alphatest, translucent)
    refl_info = {}       # gkey -> vtex's average albedo (r,g,b), for the bounce term
    env_info = {}        # gkey -> dict(cube, mask, tint, contrast, saturation)
    blend_info = {}      # gkey -> second albedo png (WorldVertexTransition tex2)
    bump_info = {}       # gkey -> normal-map png ($bumpmap; perturbs the reflection)
    glass_info = set()   # gkeys routed to UE's dedicated thin-glass master
    refract_info = {}    # gkey -> dict(normalmap, amount), explicit Source Refract cards
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
            # `vtex`'s average albedo, straight off the embedded VTF header. VtMB's light cache
            # multiplies every bounce ray by the reflectivity of the material it hit when it
            # builds a model's ambient cube, so it is the missing input for any reproduction of
            # the bounce term (RE-A3). Free here -- the header is already in hand.
            refl_cache[bt] = tth_reflectivity(tth) if tth else None
        return img_cache[bt]

    for gkey in groups:
        mat = gkey_base.get(gkey, gkey)     # base material for VMT/texture lookup
        material_path = mat
        cube = gkey_cube.get(gkey)          # baked cubemap stem, or None
        vmt_txt = read_material_text(f"materials/{mat}.vmt")
        if vmt_txt is None:
            # Not every 'maps/<mapname>/<mat>' name is a cubemap patch of a generically-named
            # base material -- some (e.g. VBSP's per-water-volume depth-blend instances) are
            # genuinely map-local materials that exist ONLY under their full path in this map's
            # own PAKFILE, with no base-name equivalent anywhere in the install. Retry under the
            # untouched raw name before giving up.
            raw = gkey_raw.get(gkey)
            if raw and raw != mat:
                vmt_txt = read_material_text(f"materials/{raw}.vmt")
                if vmt_txt is not None:
                    material_path = raw
        info = {"basetexture": None, "selfillum": False, "translucent": False, "alphatest": False,
                "water": False, "normalmap": None, "fogcolor": None, "fogstart": None,
                "fogend": None, "reflecttint": None, "refract": False,
                "dudvmap": None, "refractamount": None}
        if vmt_txt:
            info = vmt.parse(vmt_txt, resolve_include=lambda p: read_material_text(
                p if p.lower().endswith(".vmt") else p + ".vmt"))
        glass = is_glass(info, material_path)
        if glass:
            glass_info.add(gkey)
        bt = info["basetexture"]
        alphatest = info["alphatest"]
        translucent = info["translucent"]
        additive = info.get("additive", False)
        keep_alpha = alphatest or translucent or additive   # each blend mode reads the alpha channel
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
        # Refract is a separate framebuffer-distortion surface. It frequently has no
        # $basetexture at all: the rain-window cards in sp_theatre carry only a signed
        # UVWQ8888 $dudvmap and $refractamount. Prefer a tangent $normalmap when both old/new
        # hardware paths are authored; otherwise bias the signed DUDV vectors into a UE normal.
        if info.get("refract"):
            npng = None
            nm = info.get("normalmap") or info.get("dudvmap")
            if nm:
                nimg = get_img(nm)
                if nimg is not None:
                    cache_key = ("refract", nm, bool(info.get("normalmap")))
                    if cache_key not in normal_cache:
                        nfn = sanitize(nm) + "_refract_n.png"
                        normal = (nimg.convert("RGB") if info.get("normalmap")
                                  else dudv_to_normal(nimg))
                        normal.save(os.path.join(out_dir, "tex", nfn))
                        normal_cache[cache_key] = nfn
                    npng = normal_cache[cache_key]
            refract_info[gkey] = {
                "normalmap": npng,
                "amount": float(info.get("refractamount") or 0.0),
            }
        # $envmap. The surface is reflective because its VMT says so -- not because a baked
        # cube decoded. The runtime resolves the reflection through Lumen and never samples
        # `tex/cube/` (docs/vtmb/reflections.md), so gating the channel on the cube would silently
        # matte any surface whose cube failed to decode, and every `$envmap env_cubemap` face
        # VBSP left unpatched. The cube is still decoded where one exists, for the record and
        # for anything that wants the original's own reflection source.
        env_ref = info.get("envmap")
        mask_img = None
        if bt and env_ref:
            cube_id = cube or (sanitize(env_ref) if env_ref != "env_cubemap" else "env_cubemap")
            if cube or env_ref != "env_cubemap":
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
            mask_png = None
            em = info.get("envmapmask")
            if em:
                source_mask = get_img(em)
                mask_img = source_mask.convert("L") if source_mask is not None else None
                if em not in mask_cache:
                    mask_cache[em] = None
                    if mask_img is not None:
                        mfn = sanitize(em) + "_envmask.png"
                        mask_img.save(os.path.join(out_dir, "tex", mfn))
                        mask_cache[em] = mfn
                mask_png = mask_cache[em]
            elif info.get("basealphaenvmapmask"):
                # INVERTED: lightmappedgeneric_basealphamaskedenvmap masks the cube with
                # `1-t3.a`, not the alpha itself (docs/vtmb/reflections.md).
                mk = bt + "#a"
                if mk not in mask_cache:
                    aimg = get_img(bt)
                    mask_cache[mk] = None
                    if aimg is not None:
                        mfn = sanitize(bt) + "_envmask.png"
                        alpha = aimg.convert("RGBA").getchannel("A")
                        mask_img = ImageChops.invert(alpha)
                        mask_img.save(os.path.join(out_dir, "tex", mfn))
                        mask_cache[mk] = mfn
                elif get_img(bt) is not None:
                    mask_img = ImageChops.invert(get_img(bt).convert("RGBA").getchannel("A"))
                mask_png = mask_cache[mk]
            # $envmapcontrast/$envmapsaturation are parsed but NOT emitted: VtMB's shipped
            # DX8 shaders carry no term for either (docs/vtmb/reflections.md), and the whole
            # game authors saturation zero times and contrast 18 times out of 2,610.
            env_info[gkey] = {
                "cube": cube_id,
                "mask": mask_png,
                "tint": info.get("envmaptint") or [1.0, 1.0, 1.0],
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
        elif glass and bt and get_img(bt) is not None:
            # VtMB's window albedos already paint the uneven/rippled glass. Convert that
            # authored signal into a mild tangent normal for UE's Pixel Normal Offset path;
            # the env mask keeps frame bars geometrically flat. An authored $bumpmap above
            # always takes precedence.
            mask_id = info.get("envmapmask") or (
                "#basealpha" if info.get("basealphaenvmapmask") else "#alpha")
            glass_key = ("glass", bt, mask_id)
            if glass_key not in normal_cache:
                nfn = sanitize(bt) + "_glass_n.png"
                derive_glass_normal(get_img(bt), mask_img).save(
                    os.path.join(out_dir, "tex", nfn))
                normal_cache[glass_key] = nfn
            bump_info[gkey] = normal_cache[glass_key]

        mat_info[gkey] = (albedo_png, emis_png, alphatest, translucent, additive)
        if bt and refl_cache.get(bt):
            refl_info[gkey] = refl_cache[bt]

        # validate: a rendered non-water material must resolve to an albedo.
        if not info["water"] and not info.get("refract"):
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
    _tinted = sum(1 for e in env_info.values() if max(e["tint"]) - min(e["tint"]) >= 0.02)
    print(f"envmap: {len(env_info)} reflective surfaces ({len(mask_cache)} masks, "
          f"{_tinted} chromatic tint); {sum(cube_cache.values())} cubemaps decoded")
    print(f"glass: {len(glass_info)} thin-refraction material(s)")
    print(f"refract: {len(refract_info)} framebuffer-distortion material(s)")
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
    # R_DecalCreate). We recover each decal's projector -- the visible face its origin
    # projects squarely onto (nearest by plane distance), the room-facing normal, the
    # face's texture axes, and the half-extents (texture dims x $decalscale; R_DecalSize:
    # GetMappingWidth/Height * $decalScale) -- and write one line per decal to a
    # `<base>.decals` sidecar. The runtime (roadmap 7.2) builds one deferred
    # UDecalComponent per line, so the decal is lit exactly like the wall it projects
    # onto (Lumen indirect included). docs/map_completion_plan.md E.
    decal_mats = set()
    decal_lines = []   # one projector line per placed decal -> <base>.decals
    planes = np.frombuffer(read_lump(data, 1), dtype=np.float32).reshape(-1, 5)

    # A decal's host face is drawn double-sided (world CullMode = Disabled), so VtMB
    # authors the wall with arbitrary winding -- the face normal may point into the
    # sealed interior, away from the room the decal is meant to be seen from. Resolve
    # the room side from BSP leaf solidity and face the decal that way.
    d_nodes, d_leafs = read_lump(data, 5), read_lump(data, 10)
    LEAF_SZ = 32
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
    fN, fD, fPoly, fBasis = [], [], [], []
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
        fN.append(nrm); fD.append(dd); fPoly.append(poly2)
        fBasis.append((pts[0], a, bax, fi, off, np.array(sax[:3]), np.array(tax[:3])))
    fN = np.array(fN) if fN else np.zeros((0, 3))
    fD = np.array(fD) if fD else np.zeros((0,))

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
        mat_info[tex] = (albedo_cache[bt], emis_png, False, True, False)   # alpha-blended, not additive
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
        # (room) side. The runtime aims the UDecalComponent's -X (its projection axis)
        # into the wall along -nrm, so nrm is the room-facing outward normal.
        proj = p - nrm * (float(np.dot(nrm, p)) - fD[best[1]])
        nrm = _room_normal(proj, nrm, p)
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
        # Emit the projector for a runtime deferred UDecalComponent (roadmap 7.2): the
        # projected centre, the room normal (projection axis), the two surface tangent
        # axes, and the half-extents -- all in Unreal space (point via source_to_unreal,
        # unit directions via source_dir_to_unreal, extents inch->cm). One line per decal.
        lx, ly, lz = source_to_unreal(*proj)
        nx, ny, nz = source_dir_to_unreal(*nrm)
        sx_, sy_, sz_ = source_dir_to_unreal(*s_dir)
        tx_, ty_, tz_ = source_dir_to_unreal(*t_dir)
        decal_lines.append(
            f"{base_material(tm.group(1))} {lx:.4f} {ly:.4f} {lz:.4f} "
            f"{nx:.6f} {ny:.6f} {nz:.6f} {sx_:.6f} {sy_:.6f} {sz_:.6f} "
            f"{tx_:.6f} {ty_:.6f} {tz_:.6f} {hw * INCH_TO_CM:.4f} {hh * INCH_TO_CM:.4f}")
        n_decal += 1
    assert len(decal_lines) == n_decal, "decal sidecar line count != placed count"
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

    # Sky-face orientation contract (the `UE_` convention extended to sky, sky-ambience B2).
    # The decoded PNGs are emitted VERBATIM, and that is already the canonical orientation:
    # VtMB's own draw tables bind rt=+X, lf=-X, bk=+Y, ft=-Y, up=+Z, dn=-Z in Source space,
    # image row 0 is the top of the face, and no face carries a rotation or a mirror
    # (docs/vtmb/sky-ambience.md -> "K1 ... (settled)"). The horizon ring reads bk -> rt -> ft -> lf
    # left-to-right and cyclically; `up` joins rt's top edge and `dn` its bottom.
    # `skyconv` is the version of that contract, so a consumer that assembles a cube
    # (BuildSkyCube) can refuse a sidecar written under a convention it does not know.
    SKY_CONVENTION = 1

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

    # Fog is TWO different things, and they belong to two different renders (RE-A8/RE-A9):
    #
    #   worldspawn's set is the WORLD's fog. It is what the player stands in.
    #   sky_camera's set is the 3D-SKYBOX PASS's own fog -- `Enable3dSkyboxFog` pushes it for the
    #     miniature's draw and pops it again -- and its distances are in miniature units, so a
    #     world-space equivalent is `x scale`.
    #   The 2D backdrop is fogged by NEITHER: every sky face in the game carries `$nofog 1`,
    #     which the shader turns into FogMode(0), so no fog reaches it at any distance.
    #
    # Sourcing the world's fog from the sky_camera (which is what this wrote before) is wrong
    # three ways: it fogs 5 maps whose worldspawn never asked, it drops fog on 12 maps that did
    # ask but have no sky_camera at all, and on 31 of the 43 maps that have both, it uses the
    # wrong numbers.
    wspawn = ent_block("worldspawn")
    scam = ent_block("sky_camera")

    def fog_block(blk, dist_scale):
        col = blk_vec(blk, "fogcolor") or [0.0, 0.0, 0.0]
        return {"on": blk_f(blk, "fogenable") >= 1.0,
                "rgb": [max(0.0, c) / 255.0 for c in col],
                "start": blk_f(blk, "fogstart") * dist_scale * INCH_TO_CM,
                "end": blk_f(blk, "fogend") * dist_scale * INCH_TO_CM}

    world_fog = fog_block(wspawn, 1.0)
    sky_fog = fog_block(scam, sky_scale if sky.ok else 1.0)

    with open(os.path.join(out_dir, base + ".env"), "w") as o:
        o.write(f"skybox {1 if sky_ok else 0}\n")
        if skyname:
            o.write(f"skyname {skyname}\n")
        o.write(f"skyconv {SKY_CONVENTION}\n")
        for prefix, f in (("", world_fog), ("sky", sky_fog)):
            o.write(f"{prefix}fog {1 if f['on'] else 0}\n")
            o.write(f"{prefix}fogcolor {f['rgb'][0]:.4f} {f['rgb'][1]:.4f} {f['rgb'][2]:.4f}\n")
            o.write(f"{prefix}fogstart {f['start']:.4f}\n")   # cm; the sky set is x scale
            o.write(f"{prefix}fogend {f['end']:.4f}\n")
    print(f"fog: world {'on' if world_fog['on'] else 'off'} "
          f"{world_fog['start']:.0f}->{world_fog['end']:.0f}cm, "
          f"skybox {'on' if sky_fog['on'] else 'off'} "
          f"{sky_fog['start']:.0f}->{sky_fog['end']:.0f}cm")

    def write_obj(suffix, scene, subdir="", mtl=None):
        positions, uvs, groups, blend = scene
        if not positions:
            return False
        target_dir = os.path.join(out_dir, subdir)
        os.makedirs(target_dir, exist_ok=True)
        stem = base + suffix
        with open(os.path.join(target_dir, stem + ".obj"), "w") as o:
            o.write(f"mtllib {mtl or (base + '.mtl')}\n")
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
            with open(os.path.join(target_dir, stem + ".blend"), "w") as o:
                for a in blend:
                    o.write(f"{a:.4f}\n")
        return True

    write_obj("", scenes["world"])
    write_obj("_sky", scenes["sky"])
    brush_meshes = {}
    brush_dir = os.path.join(out_dir, "brushes")
    os.makedirs(brush_dir, exist_ok=True)
    wanted_brush_files = set()
    for mi in sorted(brush_scenes):
        stem = "brush_%d" % mi
        if write_obj("_%d" % mi, brush_scenes[mi], subdir="brushes",
                     mtl="../%s.mtl" % base):
            # write_obj prefixes the map base; normalize to the stable public stem.
            old_obj = os.path.join(brush_dir, "%s_%d.obj" % (base, mi))
            new_obj = os.path.join(brush_dir, stem + ".obj")
            os.replace(old_obj, new_obj)
            old_blend = os.path.join(brush_dir, "%s_%d.blend" % (base, mi))
            new_blend = os.path.join(brush_dir, stem + ".blend")
            if os.path.isfile(old_blend):
                os.replace(old_blend, new_blend)
                wanted_brush_files.add(os.path.basename(new_blend))
            brush_meshes[mi] = stem
            wanted_brush_files.add(os.path.basename(new_obj))
    for leaf in os.listdir(brush_dir):
        if (leaf.endswith(".obj") or leaf.endswith(".blend")) and leaf not in wanted_brush_files:
            os.remove(os.path.join(brush_dir, leaf))
    print(f"brush meshes: {len(brush_meshes)} -> brushes/")

    # Decal projector sidecar (roadmap 7.2): one line per placed infodecal, consumed by
    # the runtime as a deferred UDecalComponent (material + centre + normal + s/t axes +
    # half-extents, Unreal cm). Built in the infodecal block above; materials ride the
    # shared <base>.mtl (map_Kd / map_Ke / decal 1). Absent when the map has no decals.
    if decal_lines:
        with open(os.path.join(out_dir, base + ".decals"), "w") as o:
            o.write("\n".join(decal_lines) + "\n")

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

    # Shared prop-model decode state: one props/ dir + texture cache + decoded-stem set
    # feed both the GAME_LUMP static-prop path (write_props) and the .ents-referenced
    # prop path (write_entities), so a model referenced by both decodes once.
    propdir = os.path.join(out_dir, "props")
    prop_tex_cache, prop_valid = {}, set()

    write_collision(data, out_dir, base, sky)
    write_entities(data, out_dir, base, idx, propdir, prop_tex_cache, prop_valid, sky,
                   brush_meshes)
    write_lights(data, out_dir, base, sky)
    write_sprites(data, out_dir, base, idx, sky)
    write_ropes(data, out_dir, base, idx)
    # Concave displacement collision: one triangle per line (9 Unreal-space floats).
    # Convex brushes cannot represent sculpted terrain, so the runtime loads these
    # as triangle meshes alongside the .hulls convex bodies.
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
    write_props(data, out_dir, base, idx, propdir, prop_tex_cache, prop_valid, sky)

    obj_path = os.path.join(out_dir, base + ".obj")
    with open(mtl_path, "w") as m:
        for mat, (albedo_png, emis_png, alphatest, translucent, additive) in mat_info.items():
            m.write(f"newmtl {mat}\n")
            if albedo_png:
                m.write(f"map_Kd tex/{albedo_png}\n")
            elif mat in water_info or "water" in mat:
                m.write("Kd 0.05 0.10 0.13\n")   # dark water placeholder (behind the shader)
            else:
                m.write("Kd 0.35 0.35 0.38\n")   # generic missing-tex fallback
            if emis_png:
                m.write(f"map_Ke tex/{emis_png}\n")   # alpha-masked self-illum
            if mat in refract_info:
                r = refract_info[mat]
                m.write(f"refract {r['amount']:.6f}\n")  # Source framebuffer distortion
                if r["normalmap"]:
                    m.write(f"refractmap tex/{r['normalmap']}\n")
            elif mat in water_info:
                m.write("water 1\n")                   # our flag: water shader surface
            elif additive:
                m.write("additive 1\n")                # our flag: additive glow overlay (unlit)
            elif translucent:
                m.write("blend 1\n")                   # our flag: alpha-blended
            elif alphatest:
                m.write("illum 4\n")                   # our flag: alpha-tested (scissor)
            if mat in decal_mats:
                m.write("decal 1\n")                   # our flag: projected decal (render above wall)
            if mat in env_info:
                e = env_info[mat]
                # $envmap: the surface reflects. 'envmap <id>' names the cube VtMB itself
                # sampled ('env_cubemap' where VBSP patched none, and the six faces are
                # under tex/cube/<id>_{0..5}.png where one decoded); the Lumen path uses the
                # mask and the tint and resolves the reflection against the live scene.
                m.write(f"envmap {e['cube']}\n")
                if e["mask"]:
                    m.write(f"envmapmask tex/{e['mask']}\n")
                t = e["tint"]
                m.write(f"envtint {t[0]:.4f} {t[1]:.4f} {t[2]:.4f}\n")
            if mat in glass_info:
                m.write("glass 1\n")                 # our semantic: UE thin refractive glass
            if mat in blend_info:
                # WorldVertexTransition second texture; mixed by vertex COLOR.r.
                m.write(f"basetex2 tex/{blend_info[mat]}\n")
            if mat in bump_info:
                # normal map: perturbs the reflection normal (needs mesh tangents).
                m.write(f"bumpmap tex/{bump_info[mat]}\n")
            if mat in refl_info:
                # `vtex`'s own average albedo for this surface, off the `.tth`'s embedded VTF
                # header. VtMB's light cache multiplies every bounce ray by the reflectivity of
                # the material it hit when it builds a model's ambient cube (RE-A3), so this is
                # the input any reproduction of the bounce term needs. Nothing in the render
                # path reads it — it is data for the calibration, carried because it is free.
                r_, g_, b_ = refl_info[mat]
                m.write(f"reflectivity {r_:.4f} {g_:.4f} {b_:.4f}\n")
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

# The default library output is the configured external export root.
from elysium_pipeline.paths import export_root

_OUT_ROOT = os.fspath(export_root())

def _export(name_or_path, out_dir=None, *, index=None):
    """Export one map. `name_or_path` is a bare map name (resolved patch-first through
    install.map_path) or an explicit .bsp path (taken as given)."""
    bsp = install.map_path(name_or_path)
    base = os.path.splitext(os.path.basename(bsp))[0]
    out = out_dir if out_dir else os.path.join(_OUT_ROOT, base)
    print(f"map: {bsp}")
    main(bsp, out, index=index)
