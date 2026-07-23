# Elysium — offline tools (`tools/`)

Python converters that decode *Vampire: The Masquerade – Bloodlines* (VtMB, 2004,
early Source engine) proprietary formats offline into OBJ+MTL+PNG/DDS (+ glTF and
JSON/text sidecars) that the `ElysiumUE` C++ runtime loads at map-load time. This file
documents the VtMB **input formats** and their **standalone decoders** — the
reverse-engineered specs the converters read.

The export pipeline, the sidecar contracts each writer emits, and the runtime that
consumes them are described in `../docs/rebuild-strategy.md`. `UE_bsp_to_scene.py` (the
flagship map exporter) and its per-feature sidecar writers (world/material/lighting/
water/decal/skybox) write the intermediates the runtime reads back.

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
`install.map_path(name)` resolves a map name to the `.bsp` the engine would load.
Every read in `menu_extract`, `bsp_to_scene`, and `mdl` goes through it. Over the
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
  `styles[8]` @48, `day[8]` @56, `night[8]` @64 (VtMB's day/night lightmapping
  system — 8 lightstyles each, not the modern `styles[4]@68`), `lightofs` int32
  @72 (`-1` = unlit), `area` float @76, `LightmapMins[2]` int32 @80,
  `LightmapSize[2]` int32 @88 (luxels; actual dims are size+1), `origFace` @96,
  `smoothingGroups` @100. Face count = FACES_len / 104.
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

Two non-`UE_` helpers read the baked lighting to *understand* how VtMB lit the world — they
produce no runtime intermediate:

- **`lightmap.py`** — decodes LIGHTING (lump 8): per-face RGBE8888 luxel grids
  (`(sizeX+1)×(sizeY+1)`, `linear = mantissa · 2^signed_exp`), the ground truth VRAD baked
  from the WORLDLIGHTS sources. Visualizes it top-down.
- **`probe_light_calibration.py`** — fits the runtime `UElysiumLightRig` model against that
  baked ground truth (distance falloff, occlusion via BSP leaf-solid tracing, ambient), to
  calibrate the dynamic-light parameters *by data* instead of by eye. Its headline finding
  (VtMB's look is indirect-bounce-dominated, so real-time GI is load-bearing) is written up
  in `../docs/rendering-perf.md` → "Why Lumen is load-bearing".

## Textures (.tth / .ttz — no .vtf on disk)

Each texture is a pair under `materials/`: `<name>.tth` (header) + `<name>.ttz` (data).
- `.tth`: `"TTH\0"` sig, a mip offset/size table, then an **embedded standard VTF
  header** starting at the `"VTF\0"` marker. Relative to that marker: `width` uint16 @16,
  `height` uint16 @18, `highResFormat` uint32 @52, `mipCount` uint8 @56.
- `.ttz`: zlib-compressed (`78 da`) raw image data = DXT mip pyramid, ordered
  **smallest→largest** (full-res mip is LAST).
- Formats seen: DXT5 (enum 15, most common), DXT1 (13), DXT3 (14), BGR888 (3),
  BGRA8888 (12), RGBA8888 (0).

Decode (`tools/tex_to_png.py`): decompress `.ttz`, slice the last mip (size from the
block formula), wrap DXT blocks in a minimal DDS header, let PIL decode → RGBA.

Material resolution: material name → `materials/<name>.vmt` → `$basetexture` →
`materials/<basetexture>.tth`/`.ttz`.

## VMT materials (`tools/vmt.py`)

KeyValues text. `parse(text, resolve_include)` returns `basetexture` (normalized,
`\`→`/`, lowercased), `selfillum`, `translucent`, `alphatest`. Shader `"patch"`
follows one `include`. `$selfillum "1"` means the base texture's **alpha channel is
the emission mask** — emission = `RGB × (alpha/255)`, only masked pixels glow.

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
`propType`→model dict, `solid` byte @30), dedupes the map's unique models, decodes each
once into `<out>/props/<safename>.obj` (Unreal space) with a shared `props/tex/`, and writes
a `<map>.props` sidecar — one prop per line: `safename ox oy oz qx qy qz qw solid`
(9 fields; origin `source_to_unreal`, rotation `source_angles_to_unreal_quat`). `solid != 0`
gates collision. Props carry **no per-prop
tint/colour** — like the world, they are lit at runtime by the `LightRig`'s real Godot
lights.

`.ents`-**referenced** static-mesh models decode through the **same** path
(`decode_prop_models` shared by `write_props` and `write_entities`, one `props/` dir + one
texture cache, so a model referenced by both a GAME_LUMP prop and an entity decodes once).
Every `.ents` entity whose `model` key is a static `.mdl` — `prop_dynamic`/`prop_physics`
plus the `prop_button`/`prop_doorknob(_electronic)`/`prop_sign`/`prop_switch`/`prop_hacking`/
`item_container(_animated/_lock)` family — gets that model decoded into `props/<safe>.obj`
and is annotated in `<map>.ents` with **`model_mesh`** = the decoded OBJ stem (present only
when the decode succeeded; the transform stays the entity's own `origin`/`angles`, not a
`.props` line). Skeletal `npc_*` models are **excluded** — they belong to the glTFRuntime
NPC track (roadmap 8.2/8.5), not this static-geometry path. Tutorial: 160 entity props / 64
unique models. The runtime consumer is roadmap 8.3 (dynamic) / 8.4 (physics).

`WorldLoader.LoadProps` groups instances by model, builds each unique model's mesh + a
convex hull once, and renders each model as one **`MultiMeshInstance3D`** — per-instance
transform (Source→Godot `basis = M·AngleMatrix(pitch,yaw,roll)·M⁻¹`, origin
`(x,z,−y)·0.0254`, `M` = the `(x,y,z)→(x,z,−y)` basis change). `MaterialFactory.BuildProp`
builds the **same real-time shaded material as the world** (`shaded.gdshader`, lit by the
rig) with alpha-masked self-illum (`$selfillum` → `_ke`); `$additive` glow overlays stay
full-bright self-lit. Props Source marks solid get a shared `ConvexPolygonShape3D` (from
the render mesh) on a per-instance `StaticBody3D`. ch_hub_1 = 500 props / 124 models
(331 solid).

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

Python lives in **four surfaces, two languages**:
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

**Offline delivery (`UE_extract_scripts.py`, roadmap 5.1 / PL2).** The two plain-text script
surfaces are copied **verbatim** into the runtime's mirror — `python/**/*.py` → `out/scripts/`
(41 files: 35 patch + 6 retail-only; the 24 VPK `.pyc` are stale/unreachable and ignored),
`dlg/**/*.dlg` → `out/dlg/` (147; the patch's loose `dlg/` fully overlays the 138 VPK) —
resolved patch-first (patch loose > retail loose > VPK), no parse/transcode. Whole-game, not
map-scoped, so `export_all.py` runs it once at the end of a run (`--no-scripts` to skip). The
runtime scripting host (roadmap 5.2+) reads `out/scripts` + `out/dlg` from disk.

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

Scripts (`-Script <name>`, no `.java`): `DumpMenu` (recon — RTTI classes, menu strings + xrefs,
seed decompiles), `DumpGrep` (regex recon over strings/classes/func names → decompile matches),
`DumpFuncs` (targeted decompiler — follows seed funcs + callees + vftables → C pseudocode),
`DumpAsm` (raw disassembly of a function or flat run), `DumpXrefs` (every ref to an address +
containing function), `DumpConst` (dword at an address as hex/int/float), `DumpFieldRefs`,
`DumpInfo`, `EnableAIF` (pre-script: enables analysis for vtable-only-reached code).

```powershell
tools/ghidra/run.ps1 -Import "<game>\Vampire\cl_dlls\GameUI.dll"   # one-time import + auto-analyze
tools/ghidra/run.ps1 -Script DumpMenu                              # recon → out/menu_recon.txt
tools/ghidra/run.ps1 -Script DumpFuncs -Args "funcs=10003ef0 vtables=1004ff3c out=$PWD\tools\ghidra\out\basepanel.txt"
```

`-Program` picks the imported binary (`engine.dll`, `GameUI.dll`, …). **Multi-value args take one
address per run** — the arg string is re-split by `run.ps1`, `analyzeHeadless`, and Ghidra, so a
`funcs=a;b` list mis-pairs every following `key=value` and writes to the default `out=`. Give each
address its own run and its own `out=`. `README.md` holds the full arg reference, decompiler
caveats, and known GameUI.dll addresses.
