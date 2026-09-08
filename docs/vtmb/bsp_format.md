# BSP format (VtMB = version 17)

VtMB's compiled maps (`maps/*.bsp`) are a Source-engine BSP, version **17** — an early Source
2003 dialect with several VtMB-specific struct divergences from the modern (v19+) format. The
reader is `pipeline/src/elysium_pipeline/formats/bsp.py`; struct layouts are cross-checked against ata4/bspsrc (public domain,
cloned read-only at `E:\dev\bspsrc` — its `bspsrc-lib` has VtMB-specific `DFaceVTMB`/
`DDispInfo`/`DStaticPropV4` classes).

## Container

Header: `int ident "VBSP" (0x50534256)`, `int version (17)`, `lump_t[64]`, `int mapRevision`.
`lump_t` (16 B): `int fileofs, filelen, version; char[4] fourCC`. The lump directory starts at
byte 8; a BSP is a container of 64 lumps addressed by that directory, not by any fixed layout
after it.

Lumps used by this pipeline: 0 ENTITIES (text KeyValues — format and semantics: `docs/vtmb/entity_io.md`),
1 PLANES, 2 TEXDATA, 3 VERTEXES, 4 VISIBILITY (PVS — the 3D-skybox split), 5 NODES, 6 TEXINFO,
7 FACES, 8 LIGHTING (baked lightmaps — decoded for *analysis* only, not baked for runtime; the
WORLDLIGHTS sources that produced it are `docs/vtmb/lighting.md`), 10 LEAFS, 12 EDGES, 13 SURFEDGES,
14 MODELS, 15 WORLDLIGHTS (real-time light sources — full format: `docs/vtmb/lighting.md`), 16 LEAFFACES,
17 LEAFBRUSHES, 18 BRUSHES, 19 BRUSHSIDES (collision), 26 DISPINFO / 33 DISP_VERTS / 48 DISP_TRIS
(displacement terrain), 35 GAME_LUMP (static/detail props), 40 PAKFILE (embedded zip; holds
cubemap-patched material VMTs), 42 CUBEMAPS, 43 TEXDATA_STRING_DATA, 44 TEXDATA_STRING_TABLE.

## VISIBILITY (lump 4)

`int numclusters; int byteofs[numclusters][2]` (`[0]` = PVS, `[1]` = PAS), then run-length-coded
cluster bit rows — a `0x00` byte is followed by a count of zero bytes.

## Leaf/node tree

`dleaf_t` (LEAFS, lump 10) is the modern **32-byte** form: `contents` i @0, `cluster` h @4,
`firstleafface` H @20, `numleaffaces` H @22, `firstleafbrush` H @24. `dnode_t` (NODES, lump 5,
32 B): `planenum` i @0, `children[2]` i @4. `bsp.point_leaf(data, pt)` walks the node tree to a
leaf; `bsp.pvs_faces(data, origin)` returns the model-0 faces visible from a point.

### Leaf `contents` bits (recovered 2026-09-08)

The rest of `dleaf_t`: `mins[3]`/`maxs[3]` int16 @8/@14, `firstleafbrush` H @24,
`numleafbrushes` H @26, `leafWaterDataID` int16 @28, 2 B pad. `Mod_LoadLeafs` (`0x200b7da0`)
copies it into a 64-byte `mleaf_t` (`contents` @+4, `leafWaterDataID` @+0x38).
LEAFWATERDATA is lump 36, 12 B per entry (`surfaceZ` f, `minZ` f, `surfaceTexInfoID` h, pad),
loaded by `0x200b7f20` into `worldmodel+0xd8`.

VtMB's `bspflags.h` differs from the 2003 leak in the 0x100..0x2000 band. What the engine
reads, and what retail data carries (tallied over all 102 shipped maps):

| bit | engine reader | retail data |
|---|---|---|
| `0x100` | the light/shadow trace mask `0x4191` (`GetLightForPoint` `0x200a4e90`, `0x2006e3d0`, `0x200a9ee0`) — not the LOS mask `0x4091` | 944 `TOOLS/TOOLS_SHADOW` brushes across 57 maps; a shadow-only brush blocks the stealth light query and the shadow paths while staying invisible and non-solid (`docs/vtmb/stealth.md`, "The trace mask `0x4191`") |
| `0x200` | `CONTENTS_TESTFOGVOLUME`: `R_GetVisibleFogVolume` (`0x20081390`) reads it on the **eye's leaf** and only then walks the tree (`0x20081470`) for the first in-frustum leaf with `leafWaterDataID != -1` | leaf-only, 6,472 leaves in the 22 water maps, no brush |
| `0x400` | nothing | never set |
| `0x800` | precipitation: `CParticleManager` (`0x200d3f10`) kills every rain particle whose leaf lacks it; the system flag comes from the `precipitation` key in `particles/<name>.txt` (`0x200c8a90`), gated by cvar `particles_enable_precipitation` | leaf-only, 30,538 leaves in 53 maps, no brush. Matches "a vertical ray up from the leaf reaches a `toolsskybox` face before solid" on la_hub_1 / sm_hub_1 / ch_temple_1 (0..14 flagged leaves fail that test; 30..160 open leaves are unflagged, so the tool's test is a little stricter than a centre sample). Set by Troika's vvis/vbsp, not by any material |
| `0x1000` | msurface flag, not a leaf test | 127 `TOOLS/NOVIS` brushes and 10,882 leaves |
| `0x2000` | -- | `TOOLS/TOOLSNPCCLIP` brushes |

So a rebuilt map that never sets `0x800` on open-air leaves has no rain, and one that never
sets `0x200` on the leaves that see water has no visible fog volume (black or missing water
reflection); the vvis rule that matches the engine is the 2003 leak's "flag both leaves whose
`leafWaterDataID` differ", since the engine tests the eye's own leaf.

`pipeline/src/elysium_pipeline/formats/bsp.py` is the shared reader: `read_lump`/`lump_ptr`, the struct-offset constants,
`strings_from_blob`, plus `read_game_lump(data) -> {fourcc: (version, bytes)}`,
`read_pakfile(data) -> {name: bytes}`, and `read_dispinfos`/`read_dispverts` (displacements).

## Brushes, brush models and collision (recovered 2026-09-07)

`dbrushside_t` in VtMB's BSP is four little-endian int16s, so `planenum` is signed 16-bit;
`la_hub_1` has 216 of 63,096 brushsides naming a plane >= 32768, a signed-`planenum` int16 overflow.

`DISP_VERTS` per-vertex alpha is a raw `0..255` byte, not normalized.

Player blocking (`blocks_player`) is `contents & 0x1400B` over hull-producing brushes.

Brush model `models[N].origin` is always zero in VtMB's compiled BSPs; placement uses only the
entity's `origin` keyvalue.

vbsp emits a PHYSCOLLIDE entry for every brush-model entity, not only the classes that need one.

## Faces (`dface_t`, lump 7)

**VtMB's `dface_t` is 104 bytes** (`DFaceVTMB`; modern Source is 56). Full layout:
`avgLightColor[8]` int32 @0, `planenum` uint16 @32, `side` @34, `onnode` @35, `firstedge` int32
@36, `numedges` int16 @40, `texinfo` int16 @42, `dispinfo` int16 @44 (`-1` = not a
displacement), `surfaceFogVolumeID` uint16 @46, `styles[8]` @48, `day[8]` @56, `night[8]` @64
(three 8-entry lightstyle arrays where modern Source has one `styles[4]@68`), `lightofs` int32
@72 (`-1` = unlit), `area` float @76, `LightmapMins[2]` int32 @80, `LightmapSize[2]` int32 @88
(luxels; actual dims are size+1), `origFace` int32 @96, `numPrims` uint16 @100,
`firstPrimID` uint16 @102. Face count = `FACES_len / 104`.

bspsrc names one `smoothingGroups` int32 at @100; VtMB's engine does not. `Mod_LoadFaces`
(`0x200b73d0`) reads the two words separately -- `200b7648` moves `[EDI+0x64]` into the
`msurface`'s `+0x50`, `200b7650` moves `[EDI+0x66]` into `+0x52` -- and those are the
`numPrims`/`firstPrimID` pair `Shader_DrawSurfaceDynamic` (`0x2007d4e0`) reads first to decide
whether a face draws through the PRIMITIVES lump instead of its surfedge fan. No reader in the
corpus takes a smoothing dword. `hw_warrens_1` measures it: 66 faces name a run and lump 37 holds
exactly 66 records.

`day`/`night` are **dead**: `0x00` on every face of all 108 maps (where `styles` uses `0xFF` for
an unused slot), and no `engine.dll` code reads offsets 56–71 — the FACES lump's three consumers
are `Mod_LoadFaces`, the face-centroid builder, and `CMod_LoadDispInfo`. Lump 8 therefore holds
one bake, keyed by `styles[8]` alone; the "two full bakes" reading that bspsrc's field names
suggest is retired. Evidence and the lighting analysis this feeds: `docs/vtmb/sky-ambience.md` → "K4 …
(settled)".

**Other lump-7-adjacent structs**, all fixed layouts: VERTEXES = `float[3]` (12 B). EDGES =
`uint16[2]` (4 B). SURFEDGES = signed int32. TEXINFO = 72 B (`sAxis float[4]` @0, `tAxis float[4]`
@16, `texdata` int32 @68). TEXDATA = 32 B (`nameStringTableID` int32 @12, `width` @16, `height`
@20). TEXDATA_STRING_TABLE = int32 byte-offsets into TEXDATA_STRING_DATA (null-terminated
strings).

### Face polygon reconstruction

A face stores no vertex list, only edges: walk `surfedges[firstedge .. +numedges]`; each surfedge
`se` → edge index: `se>=0` uses `edges[se][0]`, `se<0` uses `edges[-se][1]` (sign = winding).
Collect corners, then fan-triangulate.

### Face → material

`face.texinfo → texinfo.texdata → texdata.nameStringTableID → stringtable[id] → stringdata →
name` (e.g. `"BUILDING/CHINABLDG04"`).

### UVs

Planar projection, computed in **source** coordinates before the axis swap: `u = (pos·sAxis.xyz
+ sAxis.w) / texWidth`, `v = (pos·tAxis.xyz + tAxis.w) / texHeight`, texWidth/Height from TEXDATA.

## Static/detail props (GAME_LUMP, lump 35)

Dir header `int count`, then per entry `char[4] fourCC, uint16 flags, uint16 version, int
fileofs, int filelen`. `fourCC` is stored byte-reversed (`sprp`→`prps` on disk); `fileofs` is
absolute from the BSP start. Entries seen: `sprp` (static props, v4), `dprp` (detail props, v2),
`dplt`.

The `sprp` payload: `int nameCount; char[128] modelDict[]; int leafCount; uint16 leaf[]; int
propCount; DStaticProp prop[]`. VtMB uses **`DStaticPropV4` (56 B)**: `Vector origin; Vector
angles; uint16 propType (→modelDict); uint16 firstLeaf, leafCount; byte solid, flags; int skin;
float fadeMin, fadeMax; Vector lightingOrigin`. (`ch_hub_1` = 500 props / 124 models; `la_hub_1`
= 864.)

The `dprp` payload is **version 2** on all 108 maps: `int nameCount; char[128] modelDict[]; int
objectCount; DetailObject obj[]` — no sprite dictionary between the two, and every record is a
model. `DetailObject` is 40 bytes: `Vector origin; QAngle angles; uint16 detailModel (→modelDict);
uint16 leaf; ColorRGBExp32 lighting; uint32 lightStyles; byte lightStyleCount; byte swayAmount;
byte shapeAngle; byte shapeSize`. Corpus: 143,412 records over 41 models on 52 maps; 35,521 carry
a non-zero `swayAmount`, which **this client never reads** (`CDetailModel`, `client.dll`
`100e0250`…`100e0300`, is construct/destroy/lighting only — the byte is VBSP's, authored for a
feature this engine build shipped without). The lump is client-only: `CDetailObjectSystem`
(`client.dll`) decodes it, the server never does, and a detail object has no collision.
`CDetailObjectSystem::vfunc10` (`100e0d90`) registers the two draw-distance ConVars,
`cl_detaildist` default **`"600"`** (`102b9fa0`) and `cl_detailfade` default **`"300"`**
(`102b9f8c`), inches, and computes the per-frame fade factor `1 / (dist² − (dist − fade)²)` for an
alpha ramp from `dist − fade` to `dist`.

## `TOOLS/*` materials

`toolsnodraw`, `toolsclip`, `toolstrigger`, `toolsskybox`, `toolshint`, `toolsareaportal`, … are
invisible engine surfaces — their faces are skipped.

## Cubemaps

Cubemap-patched material names appear as `maps/<mapname>/<mat>` and
`maps/<mapname>/<mat>_<x>_<y>_<z>`. Strip the `maps/<mapname>/` prefix and any trailing `_x_y_z`
to get the base material; base VMTs live in the VPKs, patched VMTs live in lump 40 (PAKFILE). The
`_x_y_z` suffix (or its absence) also names the face's baked env cubemap
(`materials/maps/<map>/c<x>_<y>_<z>` or the map-wide `cubemapdefault`), embedded in PAKFILE.
CUBEMAPS (lump 42) holds the sample origins: `dcubemapsample_t` (16 B): `origin[3]` int32,
`size` int32 (`0` = engine default). VtMB cubemaps are **VTF 7.1 = 7 faces** (six axes plus a
legacy spheremap, dropped); the container they ship in is `docs/vtmb/texture_format.md`.

## Coordinates

The Source→Unreal transforms live once in `pipeline/src/elysium_pipeline/formats/bsp.py` and are
described in the repo-root `CLAUDE.md` → "Coordinates are read verbatim" — not repeated here.
