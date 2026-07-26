"""Shared VtMB BSP (v17) lump access and readers.

The single place struct layouts and the coordinate transform live, so the
converters/probes stop copy-pasting them. Layouts are cross-checked against
ata4/bspsrc (public domain): the VtMB-specific `DFaceVTMB` / `DTexInfo` /
`DTexData` structs and the GAME_LUMP directory parser in `BspFile.java`.

BSP is a container of 64 lumps. Header: `int ident "VBSP"`, `int version (17)`,
`lump_t[64]` (each `int fileofs, filelen, version; char[4] fourCC`), then
`int mapRevision`. The lump directory starts at byte 8.
"""
import struct, io, zipfile

INCH_TO_M = 0.0254
INCH_TO_CM = 2.54

# --- lump indices (the ones we touch) ---------------------------------------
L_ENTITIES        = 0
L_PLANES          = 1
L_TEXDATA         = 2
L_VERTEXES        = 3
L_VISIBILITY      = 4
L_NODES           = 5
L_TEXINFO         = 6
L_FACES           = 7
L_LIGHTING        = 8
L_LEAFS           = 10
L_EDGES           = 12
L_SURFEDGES       = 13
L_MODELS          = 14
L_LEAFFACES       = 16
L_LEAFBRUSHES     = 17
L_BRUSHES         = 18
L_BRUSHSIDES      = 19
L_DISPINFO        = 26
L_DISP_VERTS      = 33
L_DISP_TRIS       = 48
L_GAME_LUMP       = 35
L_PAKFILE         = 40
L_TEXDATA_STR_DATA  = 43
L_TEXDATA_STR_TABLE = 44

# --- dface_t : VtMB `DFaceVTMB`, 104 bytes ----------------------------------
# Full v17 layout (bspsrc DFaceVTMB), offsets in bytes:
#   avgLightColor[8] i @0 | pnum H @32 | side b @34 | onnode b @35 |
#   firstedge i @36 | numedges h @40 | texinfo h @42 | dispinfo h @44 |
#   surfaceFogVolumeID H @46 | styles[8] b @48 | day[8] b @56 | night[8] b @64 |
#   lightofs i @72 | area f @76 | LightmapMins[2] i @80 | LightmapSize[2] i @88 |
#   origFace i @96 | smoothingGroups i @100
# The three 8-byte lightstyle arrays (MAXLIGHTMAPS=8) sit where modern Source has one
# `styles[4]`@68. Only `styles`@48 is live: `day`@56 and `night`@64 are 0x00 on every
# face of all 108 maps and no engine code reads those offsets, so lump 8 holds exactly
# one bake (`../docs/sky-ambience.md` -> "K4"; probe: `probe_daynight.py`). The names
# are bspsrc's. We only read the offsets below, all confirmed correct.
FACE_SIZE       = 104
FE_OFS          = 36   # firstedge  int32
NE_OFS          = 40   # numedges   int16
TI_OFS          = 42   # texinfo    int16
DISP_OFS        = 44   # dispinfo   int16  (-1 = not a displacement) [Phase B]
FACE_LIGHTOFS   = 72   # lightofs   int32  (-1 = unlit)
FACE_LM_MINS    = 80   # LightmapMins[2] int32
FACE_LM_SIZE    = 88   # LightmapSize[2] int32 (luxels; actual dims = size+1)

# --- texinfo_t (72 B) / texdata_t (32 B) ------------------------------------
TEXINFO_SIZE, TI_SAXIS, TI_TAXIS, TI_TEXDATA = 72, 0, 16, 68
TI_LMVEC_S, TI_LMVEC_T = 32, 48   # lightmapVecs[0]/[1] (float[4] each)
TEXDATA_SIZE, TD_NAMEID, TD_W, TD_H = 32, 12, 16, 20

# --- dnode_t (32 B) / dleaf_t (32 B) / visibility ----------------------------
# dleaf_t is the modern 32-byte form (not the 56-byte version-0 variant with the
# ambient light cube): contents i @0, cluster h @4, area/flags @6, mins/maxs @8,
# firstleafface H @20, numleaffaces H @22, firstleafbrush H @24.
NODE_SIZE, ND_PLANE, ND_CHILDREN = 32, 0, 4
LEAF_SIZE, LF_CLUSTER, LF_FIRSTFACE, LF_NUMFACES, LF_FIRSTBRUSH = 32, 4, 20, 22, 24
LF_AREA = 6            # uint16 @6: area = the low 9 bits, flags = the top 7

# --- displacements (Phase B) ------------------------------------------------
# DISPINFO (lump 26): standard 176-byte DDispInfo. VtMB (appID 2600) uses the
# standard struct, NOT the 172-byte HL2-beta DDispInfoBSP17 (offsets confirmed
# by count-matching against DISP_VERTS/DISP_TRIS on hw_hub_1).
DI_SIZE      = 176
DI_STARTPOS  = 0    # startPosition float[3] (source space) - orients the grid
DI_VERTSTART = 12   # dispVertStart int32   (base index into DISP_VERTS)
DI_TRISTART  = 16   # dispTriStart  int32
DI_POWER     = 20   # power int32 (2..4); grid side = (1<<power)+1 verts
# DISP_VERTS (lump 33): DDispVert, 20 bytes. Displaced pos = base + vec*dist.
DV_SIZE  = 20
DV_VEC   = 0    # offset direction float[3]
DV_DIST  = 12   # magnitude float
DV_ALPHA = 16   # blend alpha float (two-texture WorldVertexTransition; Phase G)


def lump_ptr(data, i):
    """(fileofs, filelen) of lump `i` from the directory at byte 8."""
    return struct.unpack_from("<ii", data, 8 + i * 16)


def read_lump(data, i):
    """Raw bytes of lump `i`."""
    o, l = lump_ptr(data, i)
    return data[o:o + l]


def source_to_godot(x, y, z):
    """Source (Z-up, right-handed, inches) -> Godot (Y-up, metres).

    Legacy: only the un-converted (non-UE_) exporters still emit this space.
    The UE_ pipeline uses source_to_unreal below."""
    return (x * INCH_TO_M, z * INCH_TO_M, -y * INCH_TO_M)


def source_to_unreal(x, y, z):
    """Source (Z-up, right-handed, inches) -> Unreal (Z-up, left-handed, centimetres).

    Both spaces are Z-up, so this is just an inch->cm scale plus a Y negation to flip
    handedness. The Y negation makes this a reflection (determinant -1), so any
    triangle geometry emitted through it must have its winding reversed to stay
    front-facing (the UE_ exporter reverses winding once, at OBJ write time)."""
    return (x * INCH_TO_CM, -y * INCH_TO_CM, z * INCH_TO_CM)


def source_dir_to_unreal(x, y, z):
    """Direction vector Source -> Unreal: negate Y only (no scale). Re-normalise after."""
    return (x, -y, z)


def source_angles_to_unreal_quat(pitch, yaw, roll):
    """Source QAngle (pitch, yaw, roll degrees) -> Unreal rotation quaternion
    (qx, qy, qz, qw), in the reflected Unreal frame source_to_unreal produces.

    Build the Source rotation R (column-vector convention, v' = R*v) from the QAngle,
    then conjugate by the handedness reflection M = diag(1,-1,1): R_u = M*R*M. Both the
    prop mesh and its placement pass through the same reflection, so a (reflected) mesh
    vertex p_u maps to world as R_u*p_u + source_to_unreal(origin). det(M*R*M) = +1, so
    R_u stays a proper rotation and converts cleanly to a unit quaternion (standard
    column-convention mat->quat, matching Unreal FQuat::RotateVector = q*v*q^-1)."""
    import math
    py, yw, rl = math.radians(pitch), math.radians(yaw), math.radians(roll)
    sp, cp = math.sin(py), math.cos(py)
    sy, cy = math.sin(yw), math.cos(yw)
    sr, cr = math.sin(rl), math.cos(rl)
    # Source AngleMatrix: YAW about Z, PITCH about Y, ROLL about X (matrix[row][col]).
    R = [
        [cp * cy, sr * sp * cy - cr * sy, cr * sp * cy + sr * sy],
        [cp * sy, sr * sp * sy + cr * cy, cr * sp * sy - sr * cy],
        [-sp,     sr * cp,                cr * cp],
    ]
    s = (1.0, -1.0, 1.0)   # M = diag(1,-1,1); (M R M)[i][j] = s[i]*s[j]*R[i][j]
    U = [[s[i] * s[j] * R[i][j] for j in range(3)] for i in range(3)]
    t = U[0][0] + U[1][1] + U[2][2]
    if t > 0.0:
        r = math.sqrt(1.0 + t); f = 0.5 / r
        w, x, y, z = 0.5 * r, (U[2][1] - U[1][2]) * f, (U[0][2] - U[2][0]) * f, (U[1][0] - U[0][1]) * f
    elif U[0][0] >= U[1][1] and U[0][0] >= U[2][2]:
        r = math.sqrt(1.0 + U[0][0] - U[1][1] - U[2][2]); f = 0.5 / r
        w, x, y, z = (U[2][1] - U[1][2]) * f, 0.5 * r, (U[0][1] + U[1][0]) * f, (U[0][2] + U[2][0]) * f
    elif U[1][1] >= U[2][2]:
        r = math.sqrt(1.0 - U[0][0] + U[1][1] - U[2][2]); f = 0.5 / r
        w, x, y, z = (U[0][2] - U[2][0]) * f, (U[0][1] + U[1][0]) * f, 0.5 * r, (U[1][2] + U[2][1]) * f
    else:
        r = math.sqrt(1.0 - U[0][0] - U[1][1] + U[2][2]); f = 0.5 / r
        w, x, y, z = (U[1][0] - U[0][1]) * f, (U[0][2] + U[2][0]) * f, (U[1][2] + U[2][1]) * f, 0.5 * r
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    return (x / n, y / n, z / n, w / n)


def point_leaf(data, pt, nodes=None, planes=None):
    """Index of the leaf containing `pt` (source coords): walk model 0's node tree."""
    import numpy as np
    if nodes is None:
        nodes = read_lump(data, L_NODES)
    if planes is None:
        planes = np.frombuffer(read_lump(data, L_PLANES), dtype=np.float32).reshape(-1, 5)
    node = 0
    while node >= 0:
        pn = struct.unpack_from("<i", nodes, node * NODE_SIZE + ND_PLANE)[0]
        ch0, ch1 = struct.unpack_from("<ii", nodes, node * NODE_SIZE + ND_CHILDREN)
        n, d = planes[pn, :3], planes[pn, 3]
        node = ch0 if float(pt[0]*n[0] + pt[1]*n[1] + pt[2]*n[2]) - float(d) > 0 else ch1
    return -node - 1


def decompress_vis(vis, offset, numclusters):
    """One PVS row -> bool[numclusters]. Run-length coded: a 0x00 byte is followed
    by a count of zero bytes (the Quake/Source scheme)."""
    import numpy as np
    out = np.zeros(numclusters, dtype=bool)
    c, p = 0, offset
    while c < numclusters:
        b = vis[p]; p += 1
        if b:
            for bit in range(8):
                if c + bit < numclusters and (b & (1 << bit)):
                    out[c + bit] = True
            c += 8
        else:
            c += vis[p] * 8; p += 1
    return out


def leaf_areas(data):
    """Per-leaf BSP `area` (the low 9 bits of the uint16 at leaf+6), as a numpy uint16 array.

    The area is what the engine itself classifies the 3D-skybox pass by: `Draw3dSkyboxworld`
    builds an area-bit vector holding only `m_skybox3d.area` and hands it to the world render
    lists, so the pass sees exactly one area's leaves (`../docs/sky-ambience.md` -> "The 3D
    skybox ... (RE-A8, settled)"). Cheaper and more faithful than a PVS test, which is set up
    from a player-derived viewpoint and misses up to 31 faces."""
    import numpy as np
    leafs = read_lump(data, L_LEAFS)
    raw = np.frombuffer(leafs, dtype=np.uint16)
    return (raw[LF_AREA // 2::LEAF_SIZE // 2] & 0x1FF).copy()


def area_faces(data, areas, want_area):
    """Model-0 face indices whose leaf carries `want_area` (the union of the leafface ranges)."""
    import numpy as np
    leafs = read_lump(data, L_LEAFS)
    leaffaces = np.frombuffer(read_lump(data, L_LEAFFACES), dtype=np.uint16)
    out = set()
    for li in np.nonzero(areas == want_area)[0]:
        ff, nf = struct.unpack_from("<HH", leafs, int(li) * LEAF_SIZE + LF_FIRSTFACE)
        out.update(int(leaffaces[k]) for k in range(ff, ff + nf))
    return out


def pvs_faces(data, origin):
    """Model-0 face indices in the PVS of `origin` (source coords), or None if the
    map has no visibility data or the point lands in a clusterless (solid) leaf.

    This is how the engine decides what the 3D-skybox pass draws: `CSkyboxView`
    sets its visibility up from the `sky_camera` origin (`IVRenderView::
    ViewSetupVis(novis, numorigins, origin[])`), so the backdrop is exactly what
    that point can see. The skybox room is sealed off from the map, so its PVS is
    disjoint from every player-reachable one - verified on every exported map."""
    import numpy as np
    vis = read_lump(data, L_VISIBILITY)
    if len(vis) < 4:
        return None
    numclusters = struct.unpack_from("<i", vis, 0)[0]
    if numclusters <= 0:
        return None
    leafs = read_lump(data, L_LEAFS)
    li = point_leaf(data, origin)
    cluster = struct.unpack_from("<h", leafs, li * LEAF_SIZE + LF_CLUSTER)[0]
    if cluster < 0 or cluster >= numclusters:
        return None
    ofs = struct.unpack_from("<i", vis, 4 + cluster * 8)[0]   # [0]=PVS, [1]=PAS
    pvs = decompress_vis(vis, ofs, numclusters)
    leaffaces = np.frombuffer(read_lump(data, L_LEAFFACES), dtype=np.uint16)
    out = set()
    for l in range(len(leafs) // LEAF_SIZE):
        cl = struct.unpack_from("<h", leafs, l * LEAF_SIZE + LF_CLUSTER)[0]
        if cl >= 0 and pvs[cl]:
            ff, nf = struct.unpack_from("<HH", leafs, l * LEAF_SIZE + LF_FIRSTFACE)
            out.update(int(leaffaces[k]) for k in range(ff, ff + nf))
    return out


def pvs_reachable(data, origin):
    """Model-0 face indices in the transitive PVS closure from `origin`'s cluster:
    everything the engine could ever draw as the player moves through the connected
    visible space around `origin` (source coords, e.g. info_player_start). Seeds at
    the start cluster and floods the cluster visibility graph (cluster A links B when
    B is in A's PVS), so it is the visibility component the player lives in. Faces in
    the graph's other components are ones the engine never draws from that area. What
    that excluded geometry IS varies per map and is not determined here; on patched
    sp_tutorial_1 it includes the Hunter-campaign monastery hub (the cluster holding
    the Hunter landmarks/triggers vamputil.py gates). None if the map has no vis or
    `origin` is in solid.

    Unlike pvs_faces (a single point's PVS), this is the whole reachable area, so it
    is the faithful "what does the player ever see" set for culling merged staging."""
    import numpy as np
    vis = read_lump(data, L_VISIBILITY)
    if len(vis) < 4:
        return None
    numclusters = struct.unpack_from("<i", vis, 0)[0]
    if numclusters <= 0:
        return None
    leafs = read_lump(data, L_LEAFS)
    start = struct.unpack_from("<h", leafs, point_leaf(data, origin) * LEAF_SIZE + LF_CLUSTER)[0]
    if start < 0 or start >= numclusters:
        return None

    seen = set()
    frontier = [start]
    rows = {}
    while frontier:
        c = frontier.pop()
        if c in seen:
            continue
        seen.add(c)
        if c not in rows:
            ofs = struct.unpack_from("<i", vis, 4 + c * 8)[0]
            rows[c] = decompress_vis(vis, ofs, numclusters)
        for c2 in np.nonzero(rows[c])[0]:
            if int(c2) not in seen:
                frontier.append(int(c2))

    leaffaces = np.frombuffer(read_lump(data, L_LEAFFACES), dtype=np.uint16)
    out = set()
    for l in range(len(leafs) // LEAF_SIZE):
        cl = struct.unpack_from("<h", leafs, l * LEAF_SIZE + LF_CLUSTER)[0]
        if cl in seen and cl >= 0:
            ff, nf = struct.unpack_from("<HH", leafs, l * LEAF_SIZE + LF_FIRSTFACE)
            out.update(int(leaffaces[k]) for k in range(ff, ff + nf))
    return out


def read_pakfile(data):
    """Lump 40 embedded ZIP -> {lowercase_forward_slash_name: bytes}.

    Holds map-local materials (cubemap-patched VMTs) and models. Empty or
    absent -> {}.
    """
    raw = read_lump(data, L_PAKFILE)
    if len(raw) < 4:
        return {}
    try:
        zf = zipfile.ZipFile(io.BytesIO(raw))
    except zipfile.BadZipFile:
        return {}
    out = {}
    for info in zf.infolist():
        try:
            out[info.filename.lower().replace("\\", "/")] = zf.read(info)
        except Exception:
            pass
    return out


def read_game_lump(data):
    """Lump 35 (LUMP_GAME_LUMP) -> {fourcc: (version, payload_bytes)}.

    Directory: `int count`, then per entry `char[4] fourCC, uint16 flags,
    uint16 version, int fileofs, int filelen`. The offset is relative to the
    START OF THE BSP FILE (not the game lump), so we slice `data` directly.
    `flags==1` means the entry is compressed (LZMA) — not seen in VtMB; such
    payloads are returned raw and flagged by the caller if ever hit.

    The fourCC is stored byte-reversed on disk (VtMB static props read raw as
    `prps`); the key returned here is the conventional id (`sprp`). The `sprp`
    payload's internal layout is:
      int nameCount; char[128] modelDict[nameCount];
      int leafCount;  uint16 leaf[leafCount];   (uint32 if sprp version >= 12)
      int propCount;  DStaticProp prop[propCount]  (struct size = remaining/count)
    """
    lofs, llen = lump_ptr(data, L_GAME_LUMP)
    if llen < 4:
        return {}
    count = struct.unpack_from("<i", data, lofs)[0]
    out = {}
    p = lofs + 4
    for _ in range(count):
        fourcc, flags, version, ofs, length = struct.unpack_from("<4sHHii", data, p)
        p += 16
        name = fourcc[::-1].decode("ascii", "replace")   # disk order is reversed
        payload = data[ofs:ofs + length] if length >= 0 and ofs >= 0 else b""
        out[name] = (version, payload)
    return out


def read_dispinfos(data):
    """Lump 26 -> list of {startpos:(x,y,z), vertstart, tristart, power}.

    The list index equals the `dface_t.dispinfo` value, so a face with
    dispinfo >= 0 indexes straight into this list.
    """
    raw = read_lump(data, L_DISPINFO)
    out = []
    for o in range(0, len(raw) - DI_SIZE + 1, DI_SIZE):
        sp = struct.unpack_from("<3f", raw, o + DI_STARTPOS)
        vs, ts, power = struct.unpack_from("<3i", raw, o + DI_VERTSTART)
        out.append({"startpos": sp, "vertstart": vs, "tristart": ts, "power": power})
    return out


def read_worldlights(data):
    """Lump 15 (WORLDLIGHTS) -> list of dicts, one per light source. VtMB uses the
    standard 88-byte dworldlight_t (verified against real map data): origin@0,
    intensity@12 (linear RGB, colour*brightness), normal@24 (beam direction),
    cluster@36, type@40 (0 emit_surface, 1 point, 2 spotlight, 3 skylight,
    4 quakelight, 5 skyambient), style@44 (lightstyle index), stopdot@48/stopdot2@52/
    exponent@56 (spot cone), radius@60 (cutoff; 0 = none), constant/linear/quadratic
    attenuation@64/68/72. These are what the engine's lightcache samples to light
    static props (LEAF_AMBIENT is empty in VtMB), so the prop exporter uses them."""
    raw = read_lump(data, 15)
    SZ = 88
    out = []
    for o in range(0, len(raw) - SZ + 1, SZ):
        out.append({
            "origin":    struct.unpack_from("<3f", raw, o + 0),
            "intensity": struct.unpack_from("<3f", raw, o + 12),
            "normal":    struct.unpack_from("<3f", raw, o + 24),
            "type":      struct.unpack_from("<i",  raw, o + 40)[0],
            "style":     struct.unpack_from("<i",  raw, o + 44)[0],
            "stopdot":   struct.unpack_from("<f",  raw, o + 48)[0],
            "stopdot2":  struct.unpack_from("<f",  raw, o + 52)[0],
            "exponent":  struct.unpack_from("<f",  raw, o + 56)[0],
            "radius":    struct.unpack_from("<f",  raw, o + 60)[0],
            "attn":      struct.unpack_from("<3f", raw, o + 64),   # const, linear, quadratic
        })
    return out


def read_dispverts(data):
    """Lump 33 -> list of (vec(x,y,z), dist, alpha), one per disp grid vertex."""
    raw = read_lump(data, L_DISP_VERTS)
    out = []
    for o in range(0, len(raw) - DV_SIZE + 1, DV_SIZE):
        vec = struct.unpack_from("<3f", raw, o + DV_VEC)
        dist, alpha = struct.unpack_from("<2f", raw, o + DV_DIST)
        out.append((vec, dist, alpha))
    return out


def strings_from_blob(blob):
    """TEXDATA_STRING_DATA -> {byte_offset: string}."""
    out, start = {}, 0
    for i, b in enumerate(blob):
        if b == 0:
            out[start] = blob[start:i].decode("ascii", "replace")
            start = i + 1
    return out
