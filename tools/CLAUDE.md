# Elysium — offline tools (`tools/`)

Python converters that decode *Vampire: The Masquerade – Bloodlines* (VtMB, 2004,
early Source engine) proprietary formats offline into OBJ+MTL+PNG/DDS (+ glTF and
JSON/text sidecars) that the `ElysiumUE` C++ runtime loads at map-load time. This file
documents the VtMB **input formats** and their **standalone decoders** — the
reverse-engineered specs the converters read.

The export pipeline, the sidecar contracts each writer emits, and the runtime that
consumes them are described in `../docs/rebuild-strategy.md`. `UE_bsp_to_scene.py` (the
flagship map exporter) and its per-feature sidecar writers (world/material/lighting/
water/decal/skybox/rope) write the intermediates the runtime reads back. `write_ropes` resolves
each `move_rope`/`keyframe_rope` chain (`NextKey` linkage) into per-segment `<map>.ropes` cable lines
(12 tokens: `tex ax ay az bx by bz width_cm rest_cm nodes texscale flags`) and decodes the rope
material texture, dropping coincident-node (zero-length) links; the runtime builds one
`UCableComponent` per line (roadmap 8.7). Both classnames construct the same stock Source
`CRopeKeyframe` (RE'd in `vampire.dll`), so chain roles are topological, not classname-derived.
`NextKey` binds **first-match by entity-lump order** — the engine's `FindEntityByName(NULL, …)` rule.
A map can reuse rope targetnames across separate installations (`sp_tutorial_1` reuses `tele4..tele9`
twice, ~200 m apart); a last-wins index cross-linked them into map-spanning cables, so first-wins is
used. The emitted numbers are the RE'd `CRopeKeyframe` state, not the raw keyvalues: `nodes` is
`m_nSegments`, which VtMB derives from **`Type`** (0 → 10, 1 → 4, else → 2, clamped `[2, 10]`) — not
from `Subdiv`, which is client-side render tessellation — so a `Type 2` rope is one span between two
locked points and cannot sag; `rest_cm` is the *simulated rest length*, resolved offline because the
engine's arithmetic is integral and spans both DLLs (`RopeThink` folds `Slack` into `m_RopeLength`,
then `RecomputeSprings` adds `Slack` a second time, subtracts a flat 100 units, and integer-divides
by `nodes − 1`) — most ropes land at or under their straight span and hang taut, so `rest_cm` below
`|B − A|` is expected; `flags` carries `Dangling` (1, unpins the far end), `Collide` (2),
`Barbed` (4), `Breakable` (8); `RopeShader` overrides `RopeMaterial` when present. Numbers parse with
C `atof` semantics (a leading-prefix parse), because Hammer wrote a few origins with a comma decimal
separator (`hw_jewelry_1`'s chandelier ropes) that a bare `float()` rejects. A `NextKey` naming no
node is map-data breakage (122 across the 108 maps) and is logged, not silently dropped. Full RE:
`../docs/entity_visuals.md` R3.

**The `UE_` convention.** An exporter prefixed `UE_` emits **Unreal-native** output —
centimetres, Z-up, left-handed, triangle winding pre-reversed — so the C++ runtime reads
every file 1:1 with no coordinate conversion (`source_to_unreal` / `source_dir_to_unreal`
in `bsp.py`, applied once at export). An exporter **without** `UE_` (`mdl.py`,
`mdl_gltf.py`, `mdl_skel.py`, `bsp_to_obj.py`, the `probe_*`/`lightmap` helpers) still
emits the old Godot Y-up/metres space (`source_to_godot`) and is **flagged for review** —
convert + rename it before treating its output as Unreal space. (`mdl.py`'s `write_obj_scene`
takes a keyword `ue_space=True` that emits Unreal cm/Z-up/left-handed with winding reversed;
`UE_bsp_to_scene.py`'s prop path uses it, so prop meshes + the `.props` sidecar are now
Unreal-native and runtime-consumed. `mdl.py` still defaults to Godot space for its other
callers, so it keeps the non-`UE_` name.) **`mdl_gltf.py` is exempt from the pre-conversion
rule**: its `.glb` (mesh + StudioBone skeleton + one animation, `out/npc/<stem>.glb`) is a
**standard glTF 2.0** file — self-describing Y-up/metres/right-handed — so the runtime's
glTFRuntime loader applies the glTF→Unreal basis/scale change itself (default config:
`SceneScale 100`, `TransformBaseType::Default`). The `UE_` pre-conversion is only for dumb
containers (OBJ, plain sidecars) the runtime reads verbatim; a self-describing container the
loader reorients does not need it, so `mdl_gltf.py` keeps its non-`UE_` name while feeding the
P8 NPC path directly.

The deep reverse-engineering reference docs (`entity_io.md`, `python_bridge.md`, etc.)
live in this repo's `../docs/`. (The read-only Godot project at `E:\dev\elysium` holds
an earlier copy plus the un-ported C#/Godot runtime — consulted as a porting reference,
not the source of truth for these docs.)

## Asset resolution (`tools/install.py`)

The patch-first search order governs **every** asset
class, not just the menu's: maps, models, materials, particles, and the script
layer all resolve `Unofficial_Patch\` before `Vampire\` and the VPKs.
`install.build_index(dirs)` merges the install into one table keyed by lowercase,
forward-slashed, install-relative paths, with loose entries shadowing VPK ones;
`install.read(idx, key)` fetches the bytes from wherever they live, and
`install.map_path(name)` resolves a map **name** to the `.bsp` the engine would load
(patch shadows retail); a caller-supplied *path* is honoured as-is, so batch callers must
enumerate names, not glob a tree — `install.all_map_names()` returns the patch-first union of
map stems (all 108, patch-only maps included), and `export_all.py` resolves each through
`map_path`. Every read in `menu_extract`, `bsp_to_scene`, and `mdl` goes through it. Over the
converters' trees (materials, models, maps, resource, particles, scripts, vdata)
the merge is 69,708 files: 3,620 patch overrides shadow the VPKs and 2,239 exist
only in the patch.

Reading the VPKs alone builds a menu the install does not run — 2507 patch files
shadow the VPKs (over the menu's own three trees),
including `trackerscheme.res` (the scheme behind every font and colour), 79
`vamp_mainfont` pages, `vtm_title` (1024×512, where retail's is 512×256 and stops
at "THE MASQUERADE"), the blood-cel rates (40/30 → 20/15), and `m_clans_emmiter`
(wrapper restored, five more clans, plus `bloodlinestemp`/`vtm_glowtemp` — a
floating BLOODLINES wordmark and glow).

Particles resolve by **walking the graph**, not globbing: the scene names
emitters, emitters name particles, particles name sprites. A glob over `m_*.txt`
misses whatever an override adds under another name.

`MainMenu` composes `MenuBackground3D` + the title art + `GameMenuPanel`(main) +
theme music, and opens dialogs on menu commands. The title is `title.png`
(`interface/mainmenu/vtm_title`) drawn as **one** image at `TitleRect` — VGUI
scales art by width/640 and height/480 independently, so it stretches with the
window rather than holding its authored aspect (hence `StretchMode.Scale`).
`TextureRect` needs `ExpandMode = IgnoreSize`, or the texture's own pixel size
becomes the control's *minimum* and a smaller rect silently clamps up to it. The pause
overlay (`GameManager`) reuses `GameMenuPanel`(pause) over a dimmed world. Both fall
back to a plain neutral menu when `content/ui/` is absent.

That composition and those VGUI scaling rules describe the **Godot prototype's** faithful port.
Elysium-Unreal does not port VGUI: the UI is re-skinned on a modern resolution-independent stack
(`docs/remaster-direction.md` axis 1, roadmap 8.6). What `menu_extract` produces is therefore
consumed as **design intent + source art** — screen inventory, panel anatomy, palette,
iconography, strings, and the title/background art — not as a runtime layout description. The
`.res` coordinates and the `.fnt` bitmap pages stay worth extracting as proportion and metric
reference; the runtime type is vector, so the glyph atlases never ship. Roadmap **PL8** is the
task that widens the extract to that whole inventory.

## VPK archives (original VtMB format)

`pack000.vpk … pack103.vpk` in the game's `Vampire\` folder. ~67,469 files indexed.
No file header; file data is concatenated from offset 0. A flat directory sits near
the tail. Directory entry:
```
uint32 name_len          # exact length, NOT null-terminated
char   name[name_len]    # path, mixed case, '\' or '/'
uint32 file_offset       # within this same .vpk
uint32 file_size
```
Footer at EOF (~9 bytes): `[uint32 count][uint32 dir_offset][uint8 0]`; `dir_offset`
is readable at `filesize-5`. `pack010.vpk` (savegame templates) uses a different
layout and `pack100/102.vpk` are empty 9-byte stubs — `vpk.index_all` skips
unparseable packs. Each pack is self-contained.

`tools/vpk.py`: `index_all(dir)` → `{lowercase_name: (path, offset, size)}`;
`extract(entry)` → bytes.

## BSP format (VtMB = version 17)

Container of 64 lumps. Header: `int ident "VBSP" (0x50534256)`, `int version (17)`,
`lump_t[64]`, `int mapRevision`. `lump_t` (16B): `int fileofs, filelen, version; char[4] fourCC`.
Lump directory starts at byte 8.

Lumps used: 0 ENTITIES (text KeyValues), 1 PLANES, 2 TEXDATA, 3 VERTEXES,
4 VISIBILITY (PVS — the 3D-skybox split), 5 NODES, 6 TEXINFO,
7 FACES, 8 LIGHTING (baked lightmaps — decoded for *analysis* by `lightmap.py`, not baked
for runtime), 10 LEAFS,
12 EDGES, 13 SURFEDGES, 14 MODELS, 15 WORLDLIGHTS (real-time light sources),
16 LEAFFACES, 17 LEAFBRUSHES, 18 BRUSHES, 19 BRUSHSIDES (collision),
26 DISPINFO / 33 DISP_VERTS / 48 DISP_TRIS (displacement terrain),
35 GAME_LUMP (static/detail props),
40 PAKFILE (embedded zip; holds cubemap-patched material VMTs), 43 TEXDATA_STRING_DATA,
44 TEXDATA_STRING_TABLE.

**VISIBILITY (4)**: `int numclusters; int byteofs[numclusters][2]` (`[0]` = PVS,
`[1]` = PAS), then run-length-coded cluster bit rows — a `0x00` byte is followed by a
count of zero bytes. `dleaf_t` (LEAFS 10) is the modern **32-byte** form: `contents`
i @0, `cluster` h @4, `firstleafface` H @20, `numleaffaces` H @22, `firstleafbrush`
H @24. `dnode_t` (NODES 5, 32B): `planenum` i @0, `children[2]` i @4.
`bsp.point_leaf(data, pt)` walks the node tree to a leaf; `bsp.pvs_faces(data,
origin)` returns the model-0 faces visible from a point.

`tools/bsp.py` is the shared reader: `read_lump`/`lump_ptr`, the struct-offset
constants, `source_to_godot`, `strings_from_blob`, plus `read_game_lump(data) ->
{fourcc: (version, bytes)}`, `read_pakfile(data) -> {name: bytes}`, and
`read_dispinfos`/`read_dispverts` (displacements). Struct
layouts are cross-checked against ata4/bspsrc (public domain; cloned at
`E:\dev\bspsrc` as a read-only reference — its `bspsrc-lib` has VtMB-specific
`DFaceVTMB`/`DDispInfo`/`DStaticPropV4` classes).

Struct layouts (v17 — probe real bytes, several differ from the modern wiki spec):
- **`dface_t` is 104 bytes** (VtMB `DFaceVTMB`; modern is 56). Full layout:
  `avgLightColor[8]` int32 @0, `planenum` uint16 @32, `side` @34, `onnode` @35,
  `firstedge` int32 @36, `numedges` int16 @40, `texinfo` int16 @42, `dispinfo`
  int16 @44 (`-1` = not a displacement), `surfaceFogVolumeID` uint16 @46,
  `styles[8]` @48, `day[8]` @56, `night[8]` @64 (three 8-entry lightstyle arrays
  where modern Source has one `styles[4]@68`; only `styles` is live — see below),
  `lightofs` int32
  @72 (`-1` = unlit), `area` float @76, `LightmapMins[2]` int32 @80,
  `LightmapSize[2]` int32 @88 (luxels; actual dims are size+1), `origFace` @96,
  `smoothingGroups` @100. Face count = FACES_len / 104.
  `day`/`night` are **dead**: `0x00` on every face of all 108 maps (where `styles`
  uses `0xFF` for an unused slot), and no `engine.dll` code reads offsets 56–71 —
  the FACES lump's three consumers are `Mod_LoadFaces`, the face-centroid builder
  and `CMod_LoadDispInfo`. Lump 8 therefore holds one bake, keyed by `styles[8]`
  alone. The names are bspsrc's guess at the reserved space; the "two full bakes"
  reading is retired. Evidence: `../docs/sky-ambience.md` → "K4 … (settled)".
- VERTEXES: `float[3]` (12B). EDGES: `uint16[2]` (4B). SURFEDGES: signed int32.
- TEXINFO: 72B. `sAxis float[4]` @0, `tAxis float[4]` @16, `texdata` int32 @68.
- TEXDATA: 32B. `nameStringTableID` int32 @12, `width` @16, `height` @20.
- TEXDATA_STRING_TABLE: int32 byte-offsets into TEXDATA_STRING_DATA (null-terminated strings).
- **GAME_LUMP (35)**: dir header `int count`, then per entry `char[4] fourCC,
  uint16 flags, uint16 version, int fileofs, int filelen`. fourCC is stored
  byte-reversed (`sprp`→`prps` on disk); `fileofs` is absolute from the BSP start.
  Entries seen: `sprp` (static props, v4), `dprp` (detail props, v2), `dplt`.
  The `sprp` payload: `int nameCount; char[128] modelDict[]; int leafCount;
  uint16 leaf[]; int propCount; DStaticProp prop[]`. VtMB uses **`DStaticPropV4`
  (56B)**: `Vector origin; Vector angles; uint16 propType (→modelDict); uint16
  firstLeaf, leafCount; byte solid, flags; int skin; float fadeMin, fadeMax;
  Vector lightingOrigin`. (ch_hub_1 = 500 props / 124 models; la_hub_1 = 864.)

Face polygon reconstruction (no stored vertices): walk `surfedges[firstedge .. +numedges]`;
each surfedge `se` → edge index: `se>=0` uses `edges[se][0]`, `se<0` uses `edges[-se][1]`
(sign = winding). Collect corners → fan-triangulate.

Face → material: `face.texinfo → texinfo.texdata → texdata.nameStringTableID →
stringtable[id] → stringdata → name` (e.g. `"BUILDING/CHINABLDG04"`).

UVs (planar projection, computed in **source** coords before axis swap):
`u = (pos·sAxis.xyz + sAxis.w) / texWidth`, `v = (pos·tAxis.xyz + tAxis.w) / texHeight`,
texWidth/Height from TEXDATA.

Coordinate transforms in `bsp.py` (Source is Z-up right-handed, 1 unit = 1 inch):
- **`source_to_unreal`** (the `UE_` pipeline): `(sx,sy,sz) → (sx, -sy, sz) * 2.54` →
  Unreal cm, Z-up, left-handed. Both spaces are Z-up, so it's just an inch→cm scale plus a
  Y negation to flip handedness; the negation is a reflection (det −1), so triangle winding
  is reversed once at OBJ-write time. Directions use `source_dir_to_unreal` (Y negate, no
  scale). The runtime reads the result verbatim.
- **`source_to_godot`** (legacy, non-`UE_` exporters only): `(sx,sy,sz) → (sx, sz, -sy) *
  0.0254` → Godot Y-up, metres.

`TOOLS/*` materials (toolsnodraw, toolsclip, toolstrigger, toolsskybox, toolshint,
toolsareaportal, …) are invisible engine surfaces — skip their faces.

Cubemap-patched material names appear as `maps/<mapname>/<mat>` and
`maps/<mapname>/<mat>_<x>_<y>_<z>`. Strip the `maps/<mapname>/` prefix and any
trailing `_x_y_z` to get the base material; base VMTs live in the VPKs, patched
VMTs live in lump 40 (PAKFILE). The `_x_y_z` suffix (or its absence) also names the
face's **baked env cubemap** (`materials/maps/<map>/c<x>_<y>_<z>` or the map-wide
`cubemapdefault`), embedded in PAKFILE — the exporter recovers this to split env
surfaces per cubemap. CUBEMAPS lump 42 holds the sample
origins (`dcubemapsample_t` 16B: `origin[3]` int32, `size` int32; `size 0` = engine
default). VtMB cubemaps are **VTF 7.1 = 7 faces** (six axes + a legacy spheremap,
dropped): DXT cubes ship the large mips zlib'd in `.ttz` (small mips in `.tth`);
recompiled maps store uncompressed **BGR888 inline in the `.tth`, no `.ttz`**.
`tex_to_png.decode_cubemap` handles both, slicing the full-res mip's first six faces.

## Lighting analysis (offline, not part of the export)

Non-`UE_` helpers that read the baked lighting to *understand* how VtMB lit the world — they
produce no runtime intermediate:

- **`lightmap.py`** — decodes LIGHTING (lump 8): per-face RGBE8888 luxel grids
  (`(sizeX+1)×(sizeY+1)`, `linear = mantissa · 2^signed_exp`), the ground truth VRAD baked
  from the WORLDLIGHTS sources. Visualizes it top-down.
- **`probe_light_calibration.py`** — fits the runtime `UElysiumLightRig` model against that
  baked ground truth (distance falloff, occlusion via BSP leaf-solid tracing, ambient), to
  calibrate the dynamic-light parameters *by data* instead of by eye. Its headline finding
  (VtMB's look is indirect-bounce-dominated, so real-time GI is load-bearing) is written up
  in `../docs/rendering-perf.md` → "Why Lumen is load-bearing".
- **`probe_daynight.py`** — scans **every** map in the install for a second lightmap bake and
  anything that could select one: the byte histogram of `dface_t`'s `styles`/`day`/`night`
  arrays, the per-face closure of lump 8 against a single bake (grids stored per lightstyle),
  the complete `worldspawn` key inventory, and a day/night regex sweep over every entity key
  and value. All three come back negative — the finding behind "K4 … (settled)" in
  `../docs/sky-ambience.md`. Re-run after an install change to confirm it still holds.
- **`probe_skyambient.py`** — the RE-A5 / K6 instrument: what VRAD did with `light_environment`
  at bake time, read out of its output because **VtMB ships no map compiler**. It recovers the
  keyvalue → `dworldlight_t.intensity` transfer —
  `(colour/255)^2.2 · (brightness/255) · (const + 100·linear + 10000·quadratic)`, exact on all
  16,378 origin-matched lights of all 108 maps — whose third factor is the light's own falloff
  denominator at d = 100 units, so a compiled intensity is that light's radiance at 2.54 m. Lump
  8 stores radiance ×255, so `stored luxel = 255 · intensity / falloff` and a light of brightness
  `B` lands `(colour/255)^2.2 · B` at 100 units; the ×255 is measured off the sun, whose
  sky-gated luxels sit on that ceiling at ×1.01 while carrying the sun's own chromaticity.
  `--inventory` quotes the sky pair against each map's own lump-8 percentiles (the sun's ceiling
  is a median 332% of the median lit face, the skyambient's 121% — a first-class term wherever
  sky is visible); the per-map run adds a tracing half — luxel world positions rebuilt from
  `lightmapVecs` + plane, sky visibility by a vectorised convex-brush trace classified on
  `SURF_SKY` (0x4), with the sun as the control on the method. **`--provenance` is the one to
  run first for any lump-8 work:** the Unofficial Patch recompiles 20 maps and adds 7 with a
  later Source VRAD, so 27 of 108 bakes are not Troika's, and the probe refuses a non-retail
  bake unless asked. Write-up: `../docs/sky-ambience.md` → "K6 …".

## Sky-face orientation (offline check)

**`probe_sky_orientation.py`** verifies the RE'd Source sky convention against the decoded
`out/<map>/tex/sky_*.png`: it scores the horizon ring over every cyclic order × per-face
mirror and `up`/`dn` over all eight dihedral transforms, by seam error against the cube the
`engine.dll` tables predict, and reports the winner's margin so a low-contrast face reads as a
tie rather than a match. The convention itself — face→axis binding (`rt`=+X, `lf`=−X, `bk`=+Y,
`ft`=−Y, `up`=+Z, `dn`=−Z), the per-face basis, the `1 − t` texcoord flip, and the fact that
**no face needs a rotation or mirror** — is written up in `../docs/sky-ambience.md` → "K1 …
(settled)". The decoded faces are therefore already the canonical orientation.

**`probe_sky_inventory.py`** is the whole-game sky/ambience inventory (RE-A7, K8): per map, the
`skyname` and whether its six faces resolve, the `light_environment` rows, the WORLDLIGHTS type
histogram (the type-3/type-5 sky pair), the `toolsskybox` face count, both fog sets
(`worldspawn`'s and the `sky_camera`'s), and the 3D-skybox split — sky BSP `area`, its faces,
and the static props / point entities / brush entities / worldlights inside it. Membership is
the engine's own rule, `area(point_leaf(x)) == area(point_leaf(sky_camera.origin))` with `area`
the low 9 bits of the `uint16` at leaf+6; a brush entity's point is its `models[N]` bbox centre
plus its `origin` key, because vbsp re-centres the brushes of an entity that carries one. Prints
a rollup, `--markdown` prints the doc tables, and it writes `out/_sky/inventory.json`. Findings:
`../docs/sky-ambience.md` → "The full-game inventory".

**`sky_probe.py`** is the in-game half (RE-A2): it authors six self-describing sky faces —
suffix in large type, the predicted Source axis, a `TOP` banner and an up arrow, four
distinctly-shaped corner markers, and the neighbour each edge meets in the unfolded cross —
encodes them with `tex_from_png.encode_like` against the set they shadow, and installs them
as loose `materials/skybox/` files the engine resolves before the VPKs. `--install` /
`--uninstall` / `--status`; a manifest under `out/_skyprobe/` records every written path and
sha256, and the `pier` set (the only sky the Unofficial Patch ships loose) is moved aside on
install and restored on uninstall. Reference PNGs land in `out/_skyprobe/` and feed the
runtime-side cube probe (B1) too. The prediction table and capture protocol:
`../docs/sky-ambience.md` → "The in-game check (RE-A2)".

## Textures (.tth / .ttz — no .vtf on disk)

Each texture is a pair under `materials/`: `<name>.tth` (header) + `<name>.ttz` (data).
- `.tth`: `"TTH\0"` sig, a mip offset/size table, then an **embedded standard VTF
  header** starting at the `"VTF\0"` marker. Relative to that marker: `width` uint16 @16,
  `height` uint16 @18, `reflectivity` float[3] @32, `highResFormat` uint32 @52,
  `mipCount` uint8 @56. `reflectivity` is the average albedo `vtex` computed from the texture;
  VtMB's engine multiplies every bounce ray by it when building a model's ambient cube
  (`../docs/sky-ambience.md` → "K3 / K5"), so it is a decodable input, not dead header space.
- `.ttz`: zlib-compressed (`78 da`) raw image data = DXT mip pyramid, ordered
  **smallest→largest** (full-res mip is LAST).
- Formats seen: DXT5 (enum 15, most common), DXT1 (13), DXT3 (14), BGR888 (3),
  BGRA8888 (12), RGBA8888 (0).

Decode (`tools/tex_to_png.py`): decompress `.ttz`, slice the last mip (size from the
block formula), wrap DXT blocks in a minimal DDS header, let PIL decode → RGBA.

Full `.tth` layout, as needed to **write** one (`tools/tex_from_png.py`, the inverse):
`"TTH\0"`, `uint16 version` (1), `uint8 mip_count`, `uint8 inline_mips`, `uint32
vtf_blob_len` (bytes from the `"VTF\0"` marker to EOF), then `mip_count + 1` pairs of
`uint32 raw_offset, uint32 ttz_prefix`, then the embedded VTF 7.1 header (64 B), a
low-res DXT1 16×16 thumbnail (128 B), and the `inline_mips` **smallest** mips. A mip's
`raw_offset` is its position in the reconstructed `[header][thumbnail][mips]` image
counted from the `"VTF\0"` marker, so the first mip sits at 192; `ttz_prefix` is how many
compressed bytes precede it, which works because the `.ttz` is one zlib stream with a
`Z_SYNC_FLUSH` between mips (`decompressobj().decompress(ttz[:prefix])` yields exactly the
mips before it). Entry `mip_count` holds the two totals — end offset and `.ttz` length.
Inline mips carry prefix 0. Retail textures keep their three smallest mips inline; the
patch's uncompressed re-exports keep none and leave the per-mip columns unfilled, so only
the totals row is load-bearing. `encode_like(template_tth, img)` clones a shipped
texture's format, flags and mip policy, which is how a probe texture ships in the
container the engine expects; BGR888 round-trips bit-exact, DXT5 within a re-encode.

The writer exists for RE probes that need the *original game* to draw an authored image —
VtMB has no loose `.vtf` path. It produces nothing the runtime consumes.

Material resolution: material name → `materials/<name>.vmt` → `$basetexture` →
`materials/<basetexture>.tth`/`.ttz`.

## VMT materials (`tools/vmt.py`)

KeyValues text. `parse(text, resolve_include)` returns `basetexture` (normalized,
`\`→`/`, lowercased), `selfillum`, `translucent`, `alphatest`. Shader `"patch"`
follows one `include`. `$selfillum "1"` means the base texture's **alpha channel is
the emission mask** — emission = `RGB × (alpha/255)`, only masked pixels glow.

**What a VMT's shader actually does is shipped as data, not compiled into a binary.**
`materials/dxshaders/*.psh` are readable **ps.1.1 assembly source** with Valve's own comments
intact, and `shaders/vsh/*.vcs` / `shaders/psh/*.vcs` are the compiled combos (a small offset
header, then one DX8 bytecode program per static combo, each ending `ff ff 00 00`). Both
resolve through `install.build_index` like any other asset. So "what does this material do to
colour" is a file read, not a decompile — `unlitgeneric.psh` is `tex t0; mul r0, t0, v0`, and
`lightmappedgeneric.psh` ends `mul_x2 r0.rgb, c0, r0   ; * 2 * (overbrightFactor/2)`, which is
where `../docs/sky-ambience.md` → "K7" and `../docs/color_gamma.md` get the sky-vs-world
brightness relationship from. Nothing here decodes them; there is no runtime consumer.

## Collision models (`.phy`, `tools/phy.py`)

A `.mdl` ships with a sibling `.phy` — the VPhysics collision model, a set of authored **convex
hulls** plus the model's mass. 2,854 in the retail VPKs, more added/replaced loose by the
Unofficial Patch. VtMB is Source 2003, so it uses the *legacy* surface header (`IVPS` at +0x2C, no
`VPHY` magic). `phy.py` decodes the ledge tree into hulls and emits `props/<stem>.phys` for
`prop_physics` models. Format map + the empirically settled axis mapping: `docs/phy_vphysics.md`.

## Static props / models (`.mdl` v2531, `tools/mdl.py`)

VtMB studio models are **studiomdl version 2531** (an early Source fork, far outside
the modern 44–49 range). Divergences from modern Source: **no `.vvd`** (vertex data is
embedded in the `.mdl` itself), the `.vtx` variant is **`.dx80.vtx`** (not `.dx90`),
`MDLHeader.Name` is `char[128]` (so `Length` @140), and vertices come in three formats
keyed by `StudioModel.VertexListType`: **SKINNED** (44B, exact pos/normal/uv),
**UNSKINNED** (12B) and **COMPRESSED** (8B) — the latter two hull-interpolate quantized
positions. Full struct map: `docs/mdl_v2531.md`; byte-probe: `tools/probe_mdl.py`;
reference parsers cloned read-only at `tools/re/VAMPTools` + `tools/re/Crowbar`.
Ghidra (`tools/ghidra/`) is set up for decompiling the VtMB engine/game binaries
(`Bin/engine.dll`, `Vampire/dlls/vampire.dll`, `Bin/StudioRender.dll`, …) when the
format or runtime behavior can't be settled from data alone.

`tools/mdl.py` decodes an `.mdl`+`.dx80.vtx` (LOD0) into per-material meshes and
`write_obj_scene` emits an OBJ+MTL+`tex/` (Source→Godot verts by default, or Source→Unreal
cm/Z-up with winding reversed when called `ue_space=True`; UVs as-is), reusing the
world VMT+TTH/TTZ pipeline for materials (`materials/<searchpath><name>.vmt` →
`$basetexture`). The `.vtx` vertex table comes in **two forms** — a flat `u16` id, or a
12-byte bone record holding the id at +10 — which no header flag distinguishes (the
patch's 22 recompiled static props use the record form); `mdl._vtable_form` detects it
per stripgroup. Model texture search paths come with either separator (`/` retail, `\`
patch) and may already end in one, so `mdl._norm` folds both to a single `/` — without
it the patch's models resolve no material and render flat grey. `bsp_to_scene.write_props`
reads **GAME_LUMP `sprp`** (v4, 56B `DStaticPropV4`: origin, QAngle angles,
`propType`→model dict, `solid` byte @30, `skin` int @32), dedupes the map's unique models, decodes each
once into `<out>/props/<safename>.obj` (Unreal space) with a shared `props/tex/`, and writes
a `<map>.props` sidecar — one prop per line: `safename ox oy oz qx qy qz qw solid skin`
(10 fields; origin `source_to_unreal`, rotation `source_angles_to_unreal_quat`). `solid != 0`
gates collision; `skin != 0` names an alternate skin family (157 of the install's 6,470 placed
props), applied offline by the bake as material overrides — a GAME_LUMP prop is not an entity,
so its skin never changes at runtime.

**Skin families (`props/<stem>.skins`).** `StudioMesh.Material` is a **skinref**, not a texture
index: `skinTable[family][skinref]` remaps it, which is how one model draws several skins.
`mdl.skin_table`/`skin_families` decode the table (`NumSkinRefs`@308 / `NumSkinFamilies`@312 /
`SkinIndex`@316), `decode` resolves a mesh's material through family 0, and `write_obj_scene`
resolves **every** family's materials into the `.mtl` (so each gets a decoded texture and a baked
material instance) and writes `props/<stem>.skins` — one line per alternate family that repaints
something: `<family> <authored material>=<family material> ...`, sanitized names matching the
`usemtl` keys. Family 0 is the identity row on all 4,445 readable models, so single-family models
export byte-identically to before; 201 models install-wide carry alternates. Full table + a worked
example: `../docs/mdl_v2531.md`. Props carry **no per-prop
tint/colour** — like the world, they are lit at runtime by the `LightRig`'s real Godot
lights.

`.ents`-**referenced** static-mesh models decode through the **same** path
(`decode_prop_models` shared by `write_props` and `write_entities`, one `props/` dir + one
texture cache, so a model referenced by both a GAME_LUMP prop and an entity decodes once).
Every `.ents` entity whose `model` key is a static `.mdl` — `prop_dynamic`/`prop_physics`
plus the `prop_button`/`prop_doorknob(_electronic)`/`prop_sign`/`prop_switch`/`prop_hacking`/
`item_container(_animated/_lock)` family — gets that model decoded into `props/<safe>.obj`
and is annotated in `<map>.ents` with **`model_mesh`** = the decoded OBJ stem and **`model_quat`**
= the Unreal-space placement rotation (`source_angles_to_unreal_quat` of the entity's `angles`, so
the runtime reads orientation verbatim like it does `origin`; identity when `angles` is absent). Both
present only when the decode succeeded; the origin stays the entity's own converted `origin`, not a
`.props` line. Skeletal `npc_*` models are **excluded** — they belong to the glTFRuntime
NPC track (roadmap 8.2/8.5), not this static-geometry path. Tutorial: 160 entity props / 64
unique models. The runtime consumer is roadmap 8.3 (`prop_dynamic` → `FElysiumProp`) / 8.4 (physics).

**Physics collision (8.4).** Each `prop_physics`-referenced model additionally gets a
`props/<stem>.phys` sidecar (`phy.py`), decoded from the model's sibling **`.phy`** — VtMB's own
VPhysics collision model, the convex hulls the original game simulates against, resolved through the
install index so a patched `.phy` shadows the VPK's. Format: `mass <kg>` then two lines per hull,
`hull <flat Unreal-cm verts>` + `tris <flat corner indices>`. Every ledge is convex by construction
and `phy.py` asserts it (`F = 2V − 4`, which holds across all 2,854 retail files / 7,889 hulls), so a
mis-parse is a hard error rather than a wrong collider. Nothing is decomposed or approximated: the
bake hands each hull to Geometry Script's hull builder alone and gets the same hull back. Coordinates
are `(x, −z, −y) × 100`; the keyvalues tail carries the authored per-model mass. Full format map,
the axis-mapping evidence, and the missing-collision behaviour: **`docs/phy_vphysics.md`**.
`phys_hinge` (and the `phys_*` constraint family) additionally get
**`hinge_axis`** in `<map>.ents` = the normalized Unreal-space hinge direction
(`source_dir_to_unreal` of the raw-Source `origin`→`hingeaxis` line), read verbatim; the pivot is the
entity's already-converted `origin`. (CoACD decomposes per map, not cross-map — a scaling cost for the
full export, tracked for P10.)

`WorldLoader.LoadProps` groups instances by model, builds each unique model's mesh + a
convex hull once, and renders each model as one **`MultiMeshInstance3D`** — per-instance
transform (Source→Godot `basis = M·AngleMatrix(pitch,yaw,roll)·M⁻¹`, origin
`(x,z,−y)·0.0254`, `M` = the `(x,y,z)→(x,z,−y)` basis change). `MaterialFactory.BuildProp`
builds the **same real-time shaded material as the world** (`shaded.gdshader`, lit by the
rig) with alpha-masked self-illum (`$selfillum` → `_ke`); `$additive` glow overlays stay
full-bright self-lit. Props Source marks solid get a shared `ConvexPolygonShape3D` (from
the render mesh) on a per-instance `StaticBody3D`. ch_hub_1 = 500 props / 124 models
(331 solid).

## Skeletal NPCs (`.mdl` v2531 — `mdl_skel.py` / `mdl_gltf.py` / `npc_export.py`)

The animated half of the `.mdl` (bones, skin, RLE animation tracks) decodes in `mdl_skel.py`;
the full struct map is `docs/animation_and_movers.md` Part A. VtMB NPCs carry only their own
clips (mostly dialogue) and pull locomotion/combat/idle from **shared animation banks** via the
studiohdr include-model mechanism — a recursive DAG (`NumIncludeModels`@404 /
`IncludeModelIndex`@408 → `StudioModelGroup[]`, stride 116). `mdl_skel.resolve_tree` walks it
(cycle-deduped) and `local_sequences` reads each model's own clips (`StudioSeqDesc` label →
`anim[0][0]` → local anim). Every bank bone name is present in the NPC skeleton, so clips
retarget by bone name with no proportion rig.

`npc_export.py` is the batch driver (`export_all.py --npc`, the heaviest offline pass). It scans
`out/*/*.ents` for `npc_*` `model` keys and writes under `out/npc/`:

- **`<npc>.glb`** (`mdl_gltf.export_npc`) — skinned mesh + skeleton + the NPC's **own** clips. A
  skeleton with more than one parent-less bone (`regular_cop`, `prophet`) is unified under a synthetic
  `__elysium_skeleton_root` node so glTFRuntime's single-root bone-map traversal reaches every bone —
  it is appended after the mesh node (keeping node-index == bone-index) and is not a `skin.joints`
  entry, so `JOINTS_0` still maps 1:1.
- **`banks/<bank>.glb`** (`mdl_gltf.export_bank`) — a shared bank's skeleton + all its clips,
  **no mesh**; decoded once and shared by every NPC. Bank stems keep the sub-path
  (`character_shared_male_misc`) so the male/female (and clan) banks that share a basename stay
  distinct.
- **`npc_manifest.json`** — per NPC, `{clip → owning-stem}` (own clips point at the NPC itself),
  plus a `banks` index. The runtime (roadmap 8.5) reads this, loads a clip's owning glb once, and
  applies it to the NPC skeletal mesh by bone name via glTFRuntime — VtMB's virtualmodel
  bank-sharing, not a per-NPC monolith (which would be ~94 MB × the cast ≈ 4.2 GB; the shared set
  is ~410 MB: 45 NPCs / 62 banks). `mdl_gltf` writes **standard glTF 2.0** (self-describing space),
  so it keeps its non-`UE_` name and needs no pre-conversion. `mdl_gltf.export` (single clip) is
  the 8.2 spike/CLI probe.

## Texture upscaling (`upscale_bench.py`)

Standalone tuning tool, not part of the map pipeline. Compares super-resolution
models on extracted VtMB textures. Loads any spandrel-supported architecture
(ESRGAN / RealPLKSR / DAT / Compact / HAT / SPAN) from `.pth`/`.safetensors`.
Splits RGB and alpha and upscales each independently (VtMB PNGs carry real RGB
under transparent pixels and a smooth alpha mask), `--seamless` wraps tiling
textures before upscaling, and it emits per-model outputs plus contact sheets.
`sky_upscale.py` is the skybox-aware variant (ring-composite → upscale → re-slice,
writing `out/<map>/tex_hi/`); `retex_dds.py` emits block-preserving `.dds` siblings
with mips. These three are the scaffolding for the **asset-enhancement track**
(delight → super-resolve → PBR synthesis, as an opt-in A/B layer that keeps VtMB's
style) — plan, sequencing, and the adjudication test in `../docs/asset-enhancement.md`.

## Game logic (embedded Python 2.1 — `docs/python_bridge.md`)

VtMB runs its story on a **stock CPython 2.1** (`Bin/vampire_python21.dll`, magic 60202,
API version 1010). `engine.dll` boots the VM (5 unique symbols; the `Py_SetGameInterface`
export is a dead `RET` stub); **`Vampire/dlls/vampire.dll` owns the entire script API** (44).
The scripting is all readable data; the rules it calls are compiled in `vampire.dll`.

Python lives in **five surfaces, two languages**:
- `Vampire/python/**/*.py` — 27 level scripts, 690 functions, **loose plain text**. The
  VPKs' 24 `.pyc` are stale and unreachable (CPython 2.1 predates `zipimport`, and can't
  read a VPK); the loose tree is what runs. `worldspawn.levelscript` names the hub module
  (92 of 101 maps): `"levelscript" "chinatown"` → `python/chinatown/chinatown.py`.
- `dlg/*.dlg` (VPK) — 8,355 conditions (field 4, eval) + 2,988 actions (field 5, exec) in
  **`dlgexpr`, not Python**: an engine skill-check (implicit `>=`) joined to a Python
  expression by `&`.
- `maps/*.bsp` — entity outputs carry **7** comma fields, not Source's 5; **field 6 is a
  Python call string** (6,956 of 24,081 engine-loaded outputs, 1,591 of 16,125 retail).
  `engine.dll` formats `__main__.%s` around it.
- `logic_pythoncheck` — `python_script` is an expression gating `OnTrue`/`OnFalse` (51).
- `cfg/*.cfg` ↔ `__main__.ccmd` — the **bidirectional console surface**. A script executes a
  console command by attribute-assigning on the console object (`c.patchtype = ""` runs the
  alias `patchtype`), and an unrecognised console command falls through to Python. The
  Unofficial Patch's Basic/Plus switch is exactly this: two `user.cfg` variants differing only
  in `alias patchtype "setBasic()"` vs `"setPlus()"`, so `setPlus`/`setBasic` are named nowhere
  in the `.py`/`.ents`/`.dlg`/`.bsp` trees. Ignition is `logic_auto.OnMapLoad -> unhidePlus()`
  on 107 of 108 maps. Mirrored offline by `UE_extract_cfg.py` (PL5d); the runtime console bridge is
  roadmap 9.3b.

**Offline delivery (`UE_extract_scripts.py`, roadmap 5.1 / PL2).** The two plain-text script
surfaces are copied **verbatim** into the runtime's mirror — `python/**/*.py` → `out/scripts/`
(41 files: 35 patch + 6 retail-only; the 24 VPK `.pyc` are stale/unreachable and ignored),
`dlg/**/*.dlg` → `out/dlg/` (147; the patch's loose `dlg/` fully overlays the 138 VPK) —
resolved patch-first (patch loose > retail loose > VPK), no parse/transcode. Whole-game, not
map-scoped, so `export_all.py` runs it once at the end of a run (`--no-scripts` to skip). The
runtime scripting host (roadmap 5.2+) reads `out/scripts` + `out/dlg` from disk.

**Offline delivery (`UE_extract_vdata.py`, roadmap PL5b).** VtMB's whole RPG/rules layer is
Valve-KeyValues **text** under `vdata/` (stats/feats/rules/dice/clans/quests/items/weapons/
vendors/stealth/disposition/sound-schemes/strings/camera/hacking). This copies it **verbatim**
→ `out/vdata/` (465 files: `system` 97 + `items` 244 + `camerashots` 66 + `hackterminals` 57 +
`precache` 1), patch-first, no parse/transcode; `vdata/signs/` (owned by `UE_extract_signs.py`)
and `stealth.xls` excluded. Whole-game, so `export_all.py` runs it once at end of a run
(`--no-vdata` to skip). The engine (`vampire.dll`) loads each table by name; the per-table
consumer + roadmap-task map is `docs/vdata-catalog.md`. Runtime consumers are built task by
task (9.4 sheet/quests/XP, 9.6 dice, 10.7 the rest).

**Offline delivery (`UE_extract_cfg.py`, roadmap PL5d).** The `cfg/*.cfg` alias + cvar tables (Valve
console syntax) copied **verbatim** → `out/cfg/`, patch-first, no parse/transcode — `user.cfg` carries
the Basic/Plus `patchtype` alias. Whole-game, so `export_all.py` runs it once at end of a run
(`--no-cfg` to skip). The runtime console bridge (`FElysiumConsole`, roadmap 9.3b) seeds its alias/cvar
store from this mirror, and the CPython VM points its `nt.getcwd`/`sys.moddir` at `out/` so VtMB's
file-touching scripts (`FixKeyBindings` reads `cfg/config.cfg`) resolve here.

`vampire.dll` registers module **`vampire`** (11 globals: `FindPlayer`, `FindEntityByName`,
`ChangeMap`, `ScheduleTask`, …) plus old-style classes `Entity` (`__getattr__`/`__setattr__`
+ 13 base methods) and Character (24: `SetQuest`, `SetDisposition`, `SeductiveFeed`, …).
All are star-imported into a flat, bidirectional **`__main__`**, alongside `G`, `pc`, `npc`
and `vamputil.py`'s 46 helpers. `ScheduleTask(delay, "<source>")` defers a *string*
evaluated later against `__main__`, so a live `__main__` dict + runtime evaluator are
mandatory. `G` is an engine-owned flat int namespace (~900 flags) and the save unit
(pickled); 208 of the 345 flags the scripts read are written only by `.dlg`.

**Entity methods are not bound anywhere.** `Entity.__getattr__` (`0x10195510`) resolves a
name against the entity's **Source datamap**, walking `baseMap` up the class chain: a field
carrying an `inputFunc` becomes a bound callable; otherwise its value is marshalled by
`fieldType`. So `npc.ScriptHide()` and the I/O wire `OnTrigger → ScriptHide` are the *same
lookup* — Python attribute names are Hammer keyvalue/input names. Entities are a C++
pointer boxed in a `PyCObject` tagged `_entity_ptr_`; unknown names fall through to the
instance `__dict__`.

**VtMB's `fieldtype_t` ≠ modern Source's** — it lacks `FIELD_QUATERNION` and `FIELD_TICK`,
so every code from 4 up is shifted (4=`INTEGER`, 5=`BOOLEAN`, 14=`POSITION_VECTOR`,
15=`TIME`, 16=`MODELNAME`). `typedescription_t` is **44 bytes** (52 modern):
`fieldType`@0, `fieldOffset`@8, `externalName`@0x14, `inputFunc`@0x1C. `datamap_t.baseMap`
@0xC; `GetDataDescMap()` is vtable +0x148. Reading VtMB with a modern `datamap.h`
corrupts every typed field.

## Savegames (`.sav` — `tools/sav.py` / `tools/probe_sav.py`)

`sav.py` decodes a VtMB savegame; `probe_sav.py` is the CLI over it. Both read a save file the
caller points them at and touch no game install, so they are **not** part of the export pipeline —
they are RE instruments for the save/load design. Full format spec: `../docs/savegame_format.md`.

A `.sav` is early-Source `CSaveRestore` output: `'JSAV'`, version 117, a 16383-slot sparse symbol
table, a six-field global header (`mapName`, `mapCount`, `comment`, `userName`, `GLOBAL`), then one
embedded section per visited map × 3 (`.HL1` server, `.HL2` client, `.HL3` transition list). Two
Troika divergences from stock Source: every section is **zlib-deflated in ≤512 KiB chunks**
(`section := char name[260]; int rawLen; (int compLen; byte zlib[compLen])*` — loop on `rawLen`,
never assume one stream), and a fifth save-restore block handler, `CPython_SaveRestoreBlockHandler`
(RTTI, `vampire.dll`), carries the script layer's `G` namespace as a **protocol-0 pickle**
alongside `Entities`/`EventQueue`/`Physics`/`AI`.

Inside a section everything is one flat record stream — `short size; short token; byte data[size]`
— where `token` indexes the symbol table, so the field names are the game's own datamap
`externalName` strings (`m_iVAttributesBase[ v_attribute_strength ]`, `m_QuestList`); a save is a
self-describing datamap dump. The writer **omits any all-zero field**, so readers match by name,
never by position. `.HL1` block bodies are addressed relative to `baseFilePos = dataSize -
bodySpan`, which is where the global `Save Header`/`ADJACENCY`/`LIGHTSTYLE` preamble ends.

Pickles load through a restricted unpickler that refuses class construction.

## Ghidra RE workspace (`tools/ghidra/`)

Headless-Ghidra workspace for decompiling the VtMB engine/game binaries (`Bin/engine.dll`,
`Vampire/cl_dlls/GameUI.dll`, `Vampire/dlls/vampire.dll`, `Bin/StudioRender.dll`, …) when a
format or runtime behavior can't be settled from data alone. Self-contained: the full Ghidra
**12.1.2** distribution is vendored at `tools/ghidra_12.1.2_PUBLIC/` (gitignored) — no external
install needed; only Java 21+ on PATH. `tools/re/ghidra_extract_mechanics.{java,py}` is a
standalone mechanics extractor.

`tools/ghidra/run.ps1` is the headless runner. The whole `tools/ghidra/` tree is a local-only
RE reference — gitignored, never committed: the hand-authored scripts (`run.ps1`, `README.md`,
the `.java` scripts) alongside `project/` (the analyzed Ghidra DB) and `out/` (decompilation
dumps) derived from the user's own binaries. `tools/re/` is the same — local-only, gitignored.

Scripts are the `.java` files in `tools/ghidra/`, passed as `-Script <name>` without the
extension. `EnableAIF` is a pre-script: it enables analysis for vtable-only-reached code.

`-Program` picks the imported binary (`engine.dll`, `GameUI.dll`, …). **Multi-value args take one
address per run** — the arg string is re-split by `run.ps1`, `analyzeHeadless`, and Ghidra, so a
`funcs=a;b` list mis-pairs every following `key=value` and writes to the default `out=`. Give each
address its own run and its own `out=`. `README.md` holds the full arg reference, decompiler
caveats, and known GameUI.dll addresses.
