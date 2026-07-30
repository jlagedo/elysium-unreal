# Elysium — offline tools (`tools/`)

Python converters that decode *Vampire: The Masquerade – Bloodlines* (VtMB, 2004, early Source
engine) proprietary formats offline into OBJ+MTL+PNG/DDS (+ glTF and JSON/text sidecars) that the
`ElysiumUE` C++ runtime loads at map-load time. **This file is pipeline orientation only — which
script reads/writes what, in what order, whole-game or per-map, committed or gitignored.** VtMB's
own file formats live in the RE topic docs (`bsp_format.md`, `vpk_format.md`, `texture_format.md`,
`mdl_v2531.md`, `phy_vphysics.md`, `facial_animation.md`, `savegame_format.md`,
`choreographed_scenes.md`, `python_bridge.md`, `script_api.md`, `vdata-catalog.md`, …); the export
pipeline, the sidecar contracts each writer emits, and the consuming runtime are described in
`../docs/rebuild-strategy.md`.

**The `UE_` convention** (repo-root `CLAUDE.md` → "The `UE_` exporter convention") is implemented
by `source_to_unreal`/`source_dir_to_unreal` in `bsp.py`. Without the prefix (`mdl.py`,
`mdl_skel.py`, `bsp_to_obj.py`, the `probe_*`/`lightmap` helpers) an exporter still emits the
legacy Godot Y-up/metres space and is flagged for review before its output is treated as Unreal
space. `mdl.py`'s `write_obj_scene` takes `ue_space=True` for its Unreal-native callers (the prop
path); it keeps its non-`UE_` name because its other callers still default to Godot space.
`mdl_gltf.py` is the one standing exemption — its `.glb` is standard glTF 2.0 (self-describing),
so glTFRuntime reorients it at load and needs no pre-conversion.

The deep RE reference docs live in `../docs/`. (The read-only Godot project at `E:\dev\elysium`
holds an earlier prototype — consulted only as a porting reference, not a source of truth here.)

## Asset resolution (`tools/install.py`)

Every converter reads through one patch-first index rather than a raw VPK or directory read —
`menu_extract`, `bsp_to_scene`, and `mdl` all go through it. Format + the patch-first resolution
order: `vpk_format.md`.

## VPK archives (original VtMB format)

`pack000.vpk … pack103.vpk` in the game's `Vampire\` folder. Reader: `tools/vpk.py`. Format:
`vpk_format.md`.

## BSP format (VtMB = version 17)

`maps/*.bsp`, read by `tools/bsp.py` (shared reader) and exported by `UE_bsp_to_scene.py` (the
flagship map exporter) and its per-feature sidecar writers (world/material/lighting/water/decal/
skybox/rope). Container + lump struct formats: `bsp_format.md`. The Source→Unreal/Godot
coordinate transforms live once in `bsp.py` (repo-root `CLAUDE.md` → "Coordinates are read
verbatim"; `rebuild-strategy.md` → "Coordinate conventions").

`write_ropes` resolves each `move_rope`/`keyframe_rope` chain into the `.ropes` sidecar (format:
`rebuild-strategy.md`'s sidecar table); the rope math RE (why `rest_cm` is a simulated length, the
`NextKey` first-match rule, the comma-decimal `atof` parse) is `entity_visuals.md` R3.

## Textures (`.tth`/`.ttz`) and VMT materials

Reader: `tools/tex_to_png.py` (decode), `tools/vmt.py` (material parse). Container + material
formats: `texture_format.md`. The writer half, `tools/tex_from_png.py`, exists only for RE probes
that need the original game to draw an authored image (`sky_probe.py`, below) — it produces
nothing the runtime consumes.

## Lighting and sky analysis (offline, not part of the export)

Non-`UE_` helpers that read the baked lighting/sky data to *understand* how VtMB lit the world —
they produce no runtime intermediate. Findings are `sky-ambience.md` and `rendering-perf.md`; each
tool below is the instrument, not the finding.

- **`lightmap.py`** — decodes LIGHTING (lump 8) into per-face luxel grids; visualizes top-down.
- **`probe_light_calibration.py`** — fits the runtime `UElysiumLightRig` model against that baked
  ground truth, to calibrate dynamic-light parameters by data. Headline finding:
  `rendering-perf.md` → "Why Lumen is load-bearing".
- **`probe_daynight.py`** — whole-install scan for a second lightmap bake / day-night selector.
  Re-run after an install change to confirm the negative finding still holds
  (`sky-ambience.md` → "K4").
- **`probe_skyambient.py`** — the RE-A5/K6 instrument recovering the `light_environment` →
  `dworldlight_t.intensity` transfer VRAD baked (VtMB ships no map compiler, so this is read out
  of VRAD's own output). `--inventory` / `--provenance` (run first — 27 of 108 maps are not a
  Troika bake) / per-map tracing. Write-up: `sky-ambience.md` → "K6".

### Sky-face orientation (offline check)

`UE_bsp_to_scene` writes the six decoded sky faces verbatim and records the orientation
convention as `skyconv 1` in `<map>.env` (`sky-ambience.md` → "K1"); the runtime's own half of the
assembly is "K2" and lives in the runtime, not here.

- **`probe_sky_orientation.py`** — verifies the convention against the decoded faces
  independently of the decompile.
- **`probe_sky_inventory.py`** — whole-game sky/ambience inventory (RE-A7, K8); `--markdown` /
  `--json`. Findings: `sky-ambience.md` → "The full-game inventory".
- **`sky_probe.py`** — the in-game half (RE-A2): authors self-describing sky faces and installs
  them as loose files ahead of the VPKs (`--install`/`--uninstall`/`--status`). Protocol:
  `sky-ambience.md` → "The in-game check (RE-A2)".

## Collision models (`.phy`, `tools/phy.py`)

Decodes each `prop_physics` model's sibling `.phy` into `props/<stem>.phys`. Format + axis
mapping: `phy_vphysics.md`.

## Static props / models (`.mdl` v2531, `tools/mdl.py`)

Struct format: `mdl_v2531.md`; byte-probe: `tools/probe_mdl.py`; reference parsers cloned
read-only at `tools/re/VAMPTools` + `tools/re/Crowbar`. The Ghidra workspace (`tools/ghidra/`,
below) is available when a format or runtime behaviour can't be settled from data alone.

`tools/mdl.py` decodes an `.mdl`+`.dx80.vtx` (LOD0) into per-material meshes; `write_obj_scene`
emits an OBJ+MTL+`tex/`, reusing the world VMT+TTH/TTZ pipeline for materials. Model texture
search paths come with either separator (`/` retail, `\` patch) and may already end in one —
`mdl._norm` folds both to a single `/`; without it the patch's models resolve no material and
render flat grey (a gotcha, not a format fact).

`bsp_to_scene.write_props` reads the BSP's static-prop lump, dedupes the map's unique models,
decodes each once into `<out>/props/<safename>.obj` with a shared `props/tex/`, and writes the
`.props` sidecar (format: `rebuild-strategy.md`). Alternate skin families (`.skins` sidecar):
`mdl_v2531.md`.

`.ents`-**referenced** static-mesh models (`prop_dynamic`/`prop_physics` plus the
`prop_button`/`prop_doorknob(_electronic)`/`prop_sign`/`prop_switch`/`prop_hacking`/
`item_container(_animated/_lock)` family) decode through the **same** path
(`decode_prop_models`, shared with `write_props` — one `props/` dir, one texture cache) and get
the `model_mesh`/`model_quat` annotation on their `.ents` record (format: `rebuild-strategy.md`).
Skeletal `npc_*` models are **excluded** — they belong to the glTFRuntime NPC track below.

BSP entity submodels with renderable surfaces are partitioned out of the static map OBJ into
`out/<map>/brushes/brush_<model>.obj` in entity-local Unreal space. Their `.ents` rows carry
`brush_mesh`; tools-only triggers remain meshless, and `StartHidden` brushes retain the asset
for runtime visibility changes.

`phys_hinge` (and the `phys_*` constraint family) additionally get `hinge_axis` in `<map>.ents`
(format: `rebuild-strategy.md`); the physics hull sidecar itself is `props/<stem>.phys`
(`phy_vphysics.md`). CoACD decomposes per map, not cross-map — a scaling cost for the full export.

## Skeletal characters — NPCs + the player bodies (`.mdl` v2531 — `mdl_skel.py` / `mdl_gltf.py` / `npc_export.py`)

The animated half of the `.mdl` (bones, skin, RLE animation tracks, facial flex block) decodes in
`mdl_skel.py`; struct map: `animation_and_movers.md` Part A and `facial_animation.md`. The
studiohdr include-model bank-sharing mechanism (how an NPC pulls locomotion/combat/idle clips from
shared banks) is RE'd there too.

`npc_export.py` is the batch driver (`export_all.py --npc`, the heaviest offline pass). It runs
two seeds into one product: `npc_models_from_ents` scans `out/*/*.ents` for `npc_*` models, and
`pc_models_from_clandoc` reads the **player bodies** out of `out/vdata/system/clandoc000.txt` (no
entity on any map references a player model, so the rulebook is the only seed that reaches them).
Both write under `out/npc/`:

- **`<npc>.glb`** (`mdl_gltf.export_npc`) — skinned mesh + skeleton + the NPC's own clips + facial
  morph targets.
- **`banks/<bank>.glb`** (`mdl_gltf.export_bank`) — a shared bank's skeleton + all its clips, no
  mesh; decoded once and shared by every NPC referencing it.
- **`facial/<npc>.json`** (`mdl_gltf.facial_rig`) — the flex-rig layers above the morph targets
  (controllers, RPN rules, per-morph ramps). Shape: `facial_animation.md`.
- **`npc_manifest.json`** — the clip resolution map (`{clip label → owning stem}`) plus per-clip
  metadata, deduped so a clip resolved by many NPCs is stored once. The runtime loads a clip's
  owning glb once and applies it to the NPC skeletal mesh by bone name via glTFRuntime.

A third seed, `cinematic_models_from_ents`, reads the `BaseAnim`/`MaleAnim`/`FemaleAnim` keys off
each exported map's `logic_choreographed_scene` entities — the `models/cinematic/**` anim sets no
`npc_*` entity and no rulebook table reaches. `mdl_gltf.export_cinematic` writes these as
**one bank per bone root** (`<stem>__bipNN.glb`, each root's prefix folded back to `Bip01`), and
`npc_index.json` grows a `cinematics` map from the model key to those banks. Why per root:
`choreographed_scenes.md` → "The animation set".

`mdl_gltf.export` (single clip) is the spike/CLI probe.

`validate_skeletal_pipeline.py` is the independent patch-first fidelity audit. It
compares the user's source `.mdl`/`.dx80.vtx` bytes with generated GLB binds,
inverse binds, skin payloads, timelines, and animation samples, and separately
reports source semantics the exporter/runtime does not consume. Run:

```powershell
python tools/validate_skeletal_pipeline.py --report tools/out/_tests/skeletal_pipeline_validation.json
```

A non-zero exit means the generated corpus differs from the current
exporter/source contract.

`capture_live_pose.py` is the hash-gated, read-only retail pose probe. It derives
the requested model checksum and bone count from the patch-first install, polls
StudioRender's live `boneToWorld` and skin-palette buffers, and writes
game-derived sessions under `out/_live_pose/`. `compare_pose_captures.py` removes
the entity root transform before comparing two sessions.
`validate_live_pose_capture.py` verifies `boneToWorld * poseToBone` and compares
the live pose with either an included-model clip or one named actor root in an
external cinematic MDL. Findings and the fixed-step console protocol live in
`animation_and_movers.md` A.4b.

`build_live_pose_capture.py` builds the x86 `live_pose_hook.dll` and injector
used for a whole-scene retail trace. Unlike the polling probe, this is an
in-process hook: `capture_live_scene.py` hash-gates `StudioRender.dll`, injects
the hook, records every `CStudioRender::DrawModel` submission to an `ELPOSE2`
stream, restores the vtable entry on stop, and writes a model/entity index.
`extract_live_scene_model.py` reduces one model/entity stream to consecutive
pose changes. `archive_courtroom_poses.py` stores every authored channel from
the seven courtroom cinematic MDLs so visibility-culling gaps in the live
trace still have a complete source timeline.
`analyze_live_animation_stages.py` reads the companion `ELANIM2` resolver
stream, pairs BASE/final records by pose-buffer identity, applies the recorded
bone masks, and can join exact phase samples to an extracted held-entry pose.
`analyze_cinematic_pose_composition.py` joins an extracted actor stream to that
archive and compares direct-copy and donor-bind/rest-frame composition laws.
All binaries, traces, extracted matrices, and reports remain game-derived under
`out/_live_pose/`.

## Texture upscaling (`upscale_bench.py`, `sky_upscale.py`, `retex_dds.py`)

Standalone tuning tools, not part of the map pipeline — scaffolding for the asset-enhancement
track (plan, sequencing, adjudication test: `asset-enhancement.md`). `upscale_bench.py` compares
super-resolution models on extracted textures; `sky_upscale.py` is the skybox-aware variant
(writes `out/<map>/tex_hi/`, selected at runtime by `elysium.EnhancedTextures`) and refuses to
write until every face passes an absolute-texel drift check against its source; `retex_dds.py`
emits block-preserving `.dds` siblings with mips.

## Choreographed scenes (`.vcd` — `probe_scenes.py`)

`probe_scenes.py` surveys every choreographed scene the merged install resolves. Format + findings
(the event-type enum, actor binding, the timing model, the completion contract):
`choreographed_scenes.md`.

**Offline delivery (`UE_extract_scenes.py`).** Copies three plain-text trees **verbatim**,
patch-first, no parse/transcode: `.vcd` scenes → `out/scenes/`, `.lip` phoneme sidecars →
`out/lip/`, `expressions/*.txt` phoneme→controller tables → `out/expressions/` (the sibling
`.vfe` is Faceposer's compiled form of the same data, not mirrored). Each tree keeps the
mirror-of-`sound/` layout the engine addresses. Whole-game; `export_all.py` runs it once at the
end of a run (`--no-scenes` to skip).

## Facial animation (`.mdl` flex chunks, `.lip`, `expressions/` — `probe_facial.py`)

`probe_facial.py` surveys and validates all three facial surfaces the merged install carries.
Format + findings (the flex-rule opcode set, both vertex-animation encodings, the `.lip` grammar):
`facial_animation.md`. Read-only over the install; produces no runtime intermediate — the offline
delivery is the NPC export above.

## Audio mirror and survey

`UE_extract_sounds.py` mirrors map- and scheme-referenced WAV/MP3 files patch-first into
`out/sound/` and writes the mover sound-group manifest. `audio_surface_survey.py` is the read-only
cross-surface check over exported `.ents` plus mirrored Python: audio entities, I/O wires,
environmental room types, typed `soundgroup` use and script audio calls (`--json` writes the full
ledger). VtMB behavior and formats live in `audio_pipeline.md`; the Unreal design is
`audio-architecture.md`.

## Game logic (embedded Python 2.1)

VtMB runs its story on a stock CPython 2.1 (`Bin/vampire_python21.dll`). The scripting surfaces,
the datamap-reflection bridge, and the engine method tables are `python_bridge.md`; the per-name
call inventory is `script_api.md` (built by `script_api_survey.py`, read-only over `out/`,
`--json` writes the ranked ledger).

**Offline delivery.** Three whole-game passes, each verbatim/no-parse, patch-first, run once at
the end of `export_all.py` (flag to skip in parens):

- **`UE_extract_scripts.py`** (`--no-scripts`) — `python/**/*.py` → `out/scripts/`, `dlg/**/*.dlg`
  → `out/dlg/`. The runtime scripting host reads both from disk.
- **`UE_extract_vdata.py`** (`--no-vdata`) — the whole `vdata/` RPG/rules KeyValues tree →
  `out/vdata/` (`vdata/signs/` and `stealth.xls` excluded). Per-table consumer map:
  `vdata-catalog.md`.
- **`UE_extract_cfg.py`** (`--no-cfg`) — `cfg/*.cfg` alias + cvar tables → `out/cfg/`. The runtime
  console bridge (`FElysiumConsole`) seeds its alias/cvar store from this mirror.

## Savegames (`tools/sav.py` / `tools/probe_sav.py`)

Decodes a VtMB savegame the caller points them at; touches no game install, so **not** part of the
export pipeline — an RE instrument for the save/load design. Format: `savegame_format.md`.

## Ghidra RE workspace (`tools/ghidra/`)

Headless-Ghidra workspace for decompiling the VtMB engine/game binaries (`Bin/engine.dll`,
`Vampire/cl_dlls/GameUI.dll`, `Vampire/dlls/vampire.dll`, `Bin/StudioRender.dll`, …) when a format
or runtime behaviour can't be settled from data alone. Self-contained: the full Ghidra **12.1.2**
distribution is vendored at `tools/ghidra_12.1.2_PUBLIC/` (gitignored) — no external install
needed, only Java 21+ on PATH. `tools/re/ghidra_extract_mechanics.{java,py}` is a standalone
mechanics extractor.

`tools/ghidra/run.ps1` is the headless runner. The whole `tools/ghidra/` tree is a local-only RE
reference — gitignored, never committed. `tools/re/` is the same.

`tools/ghidra_context.py` is the tracked wrapper for investigations that must survive a Ghidra
project re-import. It reads a tracked specification under `tools/research_specs/`, serializes
`DumpFuncs`/`DumpAsm`/`DumpXrefs`/`DumpFieldRefs` runs with the required lock-release delay, and
rebuilds a binary-derived context pack under `tools/ghidra/out/<topic>/`. Each specification pins
the source DLL by size and hashes and carries working addresses, confidence, eliminated leads,
relationships, and open questions. Pass the user's DLL with `--binary <path>` to reject a
different build before using its addresses. Confirmed VtMB facts still belong only in their owning
`docs/` topic.

Scripts are the `.java` files in `tools/ghidra/`, passed as `-Script <name>` without the
extension. `EnableAIF` is a pre-script: it enables analysis for vtable-only-reached code.
`-Program` picks the imported binary. **Multi-value args take one address per run** — the arg
string is re-split by `run.ps1`, `analyzeHeadless`, and Ghidra itself, so a `funcs=a;b` list
mis-pairs every following `key=value`; give each address its own run and its own `out=`.
`README.md` holds the full arg reference, decompiler caveats, and known `GameUI.dll` addresses.
