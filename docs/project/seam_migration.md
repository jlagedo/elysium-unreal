# Seam Migration

> Working note, not owned documentation. It carries no task status — `docs/project/roadmap.md`
> owns that — and it is not the seam contract, which lives in `docs/architecture/seam_map*.md`.
> It exists to get the migration clear in my own head before any of it is promoted.

## What this is

The project began by learning how to work with VtMB assets and Unreal at the same time. That took
many iterations and a lot of exploration, and it produced a running POC. But as with anything built
by exploring, the knowledge came at the cost of a messy trail: the way an asset gets from the VtMB
install into the running game is not one path, it is several, laid down one after another as each
exporter was written.

The scope of this note is to settle that one question — how assets are exported from the VtMB
install, and how they are deployed into the game runtime — and nothing else.

## Current shape

Four separate mechanisms carry content into the running game today.

**1. The loose export root.** `FElysiumContentPaths::Root()`
([ElysiumContentPaths.h:14](Source/ElysiumUE/Private/ElysiumContentPaths.h#L14)) resolves
`-ElysiumContentRoot=`, else `ELYSIUM_EXPORT_ROOT`, else `$ELYSIUM_WORK_ROOT/exports`. It is
game-derived and gitignored, and the runtime reads it directly, at runtime, as loose files:

- `shared/` — decoded textures (`tex/`, optional `tex_hi/`), static model OBJ/MTL (`props/`),
  `manifest.json`, `materials.json`
- `<map>/` — `.obj`, `_sky.obj`, `.spawn`, `.sky`, `.env`, `.lights`, `.props`, `.ents`, `.hulls`,
  `.dispcol`, `.decals`, `.ropes`, plus per-map `tex/cube/`
- `npc/` — `.eskm` containers, `npc_index.json`, and the JSON sidecars beside them
  (`clips/`, `facial/`, `procedural/`, `blends/`, `eyes/`, `garment/`)
- `items/` — `ground_models.json`, `wield/` and `wield_models.json`
- verbatim mirrors of the install's own text trees — `scripts/`, `dlg/`, `cfg/`, `vdata/`,
  `scenes/`, `lip/`, `expressions/`, `signs/`, `ui/`, and `sound/`

**2. The `/ElysiumBaked` plugin mount.** Real `.uasset` content baked offline by
[bake_map.py](pipeline/unreal/bake_map.py), [bake_characters.py](pipeline/unreal/bake_characters.py)
and [bake_wield.py](pipeline/unreal/bake_wield.py): the shared texture/material/mesh corpus, one
`.umap` per map, the cast (skeletons, meshes, clips, blend spaces, cloth), animated props, and the
wield corpus. Game-derived and gitignored — only the `.uplugin` descriptor is committed.

**3. `/Game/ElysiumGenerated`.** Packages the project generates for itself rather than decodes:
world materials, audio routing, UI fonts, input assets, the boot map, the dialogue camera set, the
player animation blueprint.

**4. `/Game/ElysiumAuthored`.** The one tracked, hand-authored package namespace — nothing
generated, nothing derived from the install.

Two smaller paths sit outside all four: `Content/Fonts`, tracked OFL faces read off disk at draw
time, and `Saved/Elysium/ScriptFS`, the writable overlay VtMB's own scripts write into.

On the export side there are likewise two families. The original `UE_extract_*.py` set writes the
loose export root above and is what the runtime reads today. The newer `*_glb.py` set is
**export_v2**: one GLB unit per source kind, specified by `docs/architecture/seam_map*.md`, writing
to `exports_v2` ([paths.py:41](pipeline/src/elysium_pipeline/paths.py#L41)). Nothing in the runtime
reads `exports_v2` yet.

## Legacy export ledger

What the non-v2 family writes and who actually reads it. *runtime* = game C++ reads it loose at
runtime (the set `Content/ElysiumCorpus` must carry until that slice migrates); *bake* = input to a
`.uasset` bake (never deploys); *none* = written today, read by nothing.

| Product (under export root) | Exporter | Consumed by |
| --- | --- | --- |
| `<map>.obj`/`.mtl`, `<map>_sky.obj`, `brushes/*` | UE_bsp_to_scene | bake (`.obj` also runtime existence gate) |
| `<map>.ents`, `.lights`, `.env`, `.sky` | UE_bsp_to_scene | **both** |
| `<map>.hulls`, `.dispcol`, `.spawn`, `.ropes` | UE_bsp_to_scene | runtime |
| `<map>.props`, `.decals`, `.water`, `.materials.json`, `.weather.json`, `.particles.json`, `tex/cube/*` | UE_bsp_to_scene | bake |
| `<map>.sprites` | UE_bsp_to_scene | none |
| `shared/tex/*` (+ `tex_hi/`), `npc/tex/*` | UE_extract_corpus, npc_export | **both** (runtime: sky cube faces, rope/cable materials via `FElysiumTextureCache`, eye irises) |
| `shared/props/*`, `manifest.json`, `materials.json` | UE_extract_corpus | bake |
| `items/ground_models.json` | UE_extract_items | runtime |
| `items/wield/**`, `wield_models.json` | UE_extract_wield | bake |
| `npc/*.eskm`, `banks/`, `placed_models/`, `npc_manifest.json`, `tex/`, `garment/` | UE_mdl_skeletal, npc_export, UE_mdl_cloth | bake |
| `npc/npc_index.json`, `blends/`, `eyes/` | npc_export | **both** |
| `npc/clips/`, `facial/`, `procedural/` | npc_export | runtime |
| `sound/**`, `audio/catalog.json` | UE_extract_sounds | runtime |
| `audio/maps/*.json`, `schemes.json`, `entity_events.json` | UE_extract_sounds | none |
| `scenes/**`, `lip/**`, `expressions/*` | UE_extract_scenes | runtime |
| `scripts/**`, `dlg/**` | UE_extract_scripts | runtime |
| `cfg/*` | UE_extract_cfg | runtime |
| `vdata/**` | UE_extract_vdata | runtime (also npc_export input) |
| `signs/*.txt` | UE_extract_signs | runtime |
| `signs/tex/*`, `backgrounds.json` | UE_extract_signs | none |
| `ui/strings.json`, `ui/art/**`, `ui/menu/title.png` + `sprites/`, `ui/effects/*` | UE_extract_ui | runtime |
| `ui/resource/`, `ui/menu/skybox/`, `ui/menu/particles/`, `ui/manifest.json` | UE_extract_ui | none |
| `particles/**` (raw mirror + `*.png`) | UE_extract_particles | bake |
| `hud/use_icons.png` + `.json` | UE_use_icons | runtime |

## Proposal

Converge on one path. export_v2 GLB is the standard every export moves to, and everything that is a
better fit as an Unreal asset lands as one — a mesh, texture, material or sound as its native type,
and structured data as a `UDataAsset`, decided case by case rather than as a blanket rule. The
target is a single, unified runtime workflow rather than four.

Done, in this order:

1. **Exporter convergence.** The `UE_extract_*` family is retired or rewritten onto the export_v2
   GLB seam, so there is one export contract.
2. **Runtime consolidation.** The game resolves every resource through a single mechanism, with any
   exception named explicitly here rather than left implicit.

Baking does not go away. export_v2 produces the inspectable intermediate; the bake still turns it
into the `.uasset` content the game loads.

## Settled

**Overlay policy for vdata readers (owner call, 2026-08-30).** Scripts write vdata through the
ScriptFS overlay in two cases: the haven PC rewrites `vdata/hackterminals/haven_pc.txt` (real
gameplay — emails), and the hunter-mode easter egg copies `" - hunter"`/`" - vampire"` variants over
base files (stats, strings, signs, items). Decision: **terminal definitions stay overlay-first**
([ElysiumTerminal.cpp](Source/ElysiumUE/Private/Substrate/ElysiumTerminal.cpp)) and must not
regress when vdata moves to `Content/ElysiumCorpus`; **rulebook tables and signs read
corpus-only** — the hunter-mode variant swap is a named deliberate divergence, unsupported.
Faithful support would need overlay-aware reads plus cache invalidation and cross-session overlay
semantics; if ever wanted, it is a scoped task with the terminal loader as the reference. Promote
the divergence note to the owning vdata topic when this migration lands.

**Units are source capsules (owner call, 2026-08-30).** The unit contract's `reject_opaque_source`
rule — units must not embed their source bytes — was never an owner decision and is repudiated. An
export_v2 unit is a self-contained capsule: it carries the exact winning source bytes of each
member (BIN chunk, hash-checked against `sourceResolution`) alongside the full decode. The decode
and byte-ledger validation remain mandatory — the capsule never excuses decoding. The corpus
import lane extracts bytes from the capsule, so import reads only `exports_v2`. Rollout: required
for `vtmb:vdata:` now (schema 1.1.0); every other seam adopts when its slice migrates.

**Install curation (2026-08-30).** The install was found with all nine hunter-swappable vdata
base files byte-equal to their `" - hunter"` twins — the easter egg had been triggered in this
install and VtMB's own script overwrote them in place. Restored from the `" - vampire"` twins
(owner-approved, one-time write to the otherwise read-only install); every export before this
date carried hunter data in `stats`, `strings`, `credits`, `traiteffects000`, four armor items
and `signs/death`.

**Slice 1 layout (committed).** Runtime resolves content through `Content/` (`ElysiumAuthored`,
`ElysiumGenerated`, `ElysiumCorpus` — deployed loose corpus, gitignored) and the `/ElysiumBaked`
plugin; the export trees are build/review areas with no runtime reads. Slice 1 moves vdata
(minus `signs/`, which stays on the legacy flat export until its own slice): `uv run elysium
import vdata` deploys capsule bytes to `Content/ElysiumCorpus/vdata/**`, `VdataDir()` and the
ScriptFS `vdata/` mount flip to it.

**Slice 2 is textures (owner calls, 2026-08-30).** Signs were the obvious next vdata step, but a
sign's only non-text dependency is a material whose only dependency is a texture, and every
material, mesh and map consumer sits on the same foundation — so textures go first and signs wait
for the material slice. The lane, `uv run elysium import textures`, is specified in
`docs/architecture/seam_map_texture.md` → "Import". The owner calls it rests on:

- **Namespace mirrors the unit identity.** `vtmb:texture:hud/signs/notepad_yellow` →
  `/ElysiumBaked/Textures/hud/signs/T_notepad_yellow` (`TC_` cubemap, `TA_` frame array). No
  flattening: the legacy `Shared/Textures` bake drops stem collisions as "ambiguous source", which
  is a fidelity loss the new namespace removes. `/ElysiumBaked/Shared/Textures` stays untouched
  until the material slice re-points every material and deletes it.
- **Standard import, re-encode loss accepted.** `FTextureSource` holds no block-compressed format,
  and Unreal 5.8 refuses a block-compressed DDS outright on both import paths (Interchange and the
  legacy factory both map DXGI through `DXGIFormatGetClosestRawFormat`, which has no BC row — the
  first full run failed every BC unit with `DDS DXGIFormat not supported : 71 : BC1_UNORM`). So
  the lane decodes the KTX2 blocks itself with its own spec-exact BC1/BC2/BC3 decoder, stages
  uncompressed BGRA8, and Oodle re-encodes at build; bit-identity would need a custom `UTexture`
  subclass outside the DDC path. Accepted, on the condition that the lane reports the measured
  texel delta per unit (the staged decode against the built mip 0, both read by the same decoder,
  so the number is Unreal's re-encode alone) so the loss is a number, and the GLB stays the
  bit-exact record. Compression is source-preserving: BC stays
  `TC_Default` (never `TC_Normalmap`, which discards a channel the source had); uncompressed
  sources stay uncompressed.
- **Every authored mip is imported**, including the 65 short chains; what Unreal does with a
  chain that ends above 1×1 is observed on the first run and recorded, not assumed.
- **sRGB is decided from the material bindings**, read from the material units directly (the
  corpus index carries the edge but not the parameter — extending it is a follow-up): a texture is
  colour unless every binding is a data read (`$bumpmap`, `$normalmap`, `$dudvmap`,
  `$envmapmask`, `$masktexture`…) or it has no binding (461 units, recorded as `no evidence`).
- **Conflict textures get a linear twin.** 590 textures are bound as colour by one material and
  as data by another. Source filters the raw bytes; an sRGB asset is linearised before filtering,
  so a material-side `LinearToSrgb` is only exact on flat regions. Rejected: doubling all 11,243
  (95% waste) and a formula-only fix (inexact at edges and mips). Accepted: one asset per texture
  plus a `_linear` twin for the conflict set only, both carrying the same unit id.
- **Cubemaps (43) are included**, accepted headlessly: parity of every face against the cube the
  runtime builds today from the legacy PNG faces (`SkySlices`), which is the visually accepted sky,
  plus an edge-seam continuity check that needs no reference. A rendered frame is a courtesy check
  when the sky reader flips, not the gate.
- **Provenance rides as `UElysiumTextureProvenance : UAssetUserData`** — the one carrier that
  survives cook, reads at runtime and attaches from editor Python — with a few fields published as
  asset-registry tags for Content Browser search. Rejected: package metadata tags and
  `AssetImportData` (editor-only), one `UDataAsset` per texture (11k objects for nothing).
- **Hard references by default**, tuned to soft later only if boot time or hitches show it.
- **Reflectivity's consumer flips in the material slice**, when materials reach the new assets;
  this slice only carries the value faithfully.
- **The three loose-file readers** (sky cube faces, rope/cable materials, eye irises) flip in the
  next slice together with the `tex_hi` decision; `shared/tex`, `npc/tex` and `retex_dds` retire
  then.
- **The first run is the full corpus.** The lane isolates per-unit failures, resumes from recipe
  stamps and reports every failed unit with its reason, so a defect late in the run costs a
  relaunch, not the run.

**Baked reflection probes are not reflection content (owner call, 2026-08-31).** The 1,325
per-map `maps/<map>/c<x>_<y>_<z>` probes are renders of the 2004 lightmapped world; adding them
back as Source's additive cube term would put 2004 lighting into Lumen reflections, and every wet
floor would reflect a room Lumen is not lighting. They are exported and imported as units for
fidelity and provenance, and their **origins** (1,228 lump-42 samples) place Unreal reflection
captures; their **pixels** are never sampled by a surface. The ~340 materials that name an
authored fixed cube (`envmap/blood`, `envmap/specmap`, `envmap/asylum`, `envmap/hav`, …) are an
art choice of image rather than a room, and keep a literal cube sample.

**The matte-world premise is repudiated (owner call, 2026-08-31).** The legacy masters set every
non-`$envmap` surface to Specular 0 / Roughness 1 and every light to `specular_scale = 0`, on the
reading that "VtMB's world is METALLIC 0, SPECULAR 0, ROUGHNESS 1". That is a fact about Source's
lightmapped Lambert renderer, which had no specular term and made `$envmap` the only way to
shine; it is not a fact about the surfaces. Under Lumen every surface reflects by physics
(Specular, Roughness, Metallic) and the renderer supplies the image. Consequence: the three
zeroes flip; VtMB's cube data becomes the *how shiny / where* input — `$envmap` presence, the
per-texel masks, `$envmaptint` — never the *what is reflected*. The calibration values (default
specular, the roughness prior per surface class, the mask mapping) are open until the owner tunes
them on editor knobs (plan SF-C0..C4). Old Unreal-side translations in `docs/vtmb/reflections.md`
→ "The Unreal translation" and the legacy bake are starting values for those knobs, not decisions.

**Calibration happens on knobs inside the editor, never in a loop (owner call, 2026-08-31).**
Every value that needs a human eye is exposed as something the owner edits in the Unreal editor,
sees change live in PIE, and saves with Ctrl+S — and the saved file is the file the pipeline
reads. Three native surfaces, no Blueprint, no MCP, no debug window as the authoring path:
a `UDeveloperSettings` page (Project Settings → Elysium → Surfaces, saved to a git-tracked
`Config/DefaultElysium.ini`) for global scalars, mirrored into `MPC_ElysiumEnvironment` on edit;
a `UDataAsset` edited as a grid for per-class tables, regenerating a lookup texture on edit; and
the Material Instance editor for one material. Forbidden: an agent tuning values in a
build–launch–look cycle, an agent capturing frames to "calibrate", and any constant that needs
taste living in a Python or C++ literal. `validation/shots_diff.py` may record before/after; it is
never the tuning method. PIE is the viewing window (`debug-tooling.md`: PIE is a viewer, and it
picks up settings and collection edits without restart).

## Plan — surfaces track (export gap, surface properties, reflections, materials)

Owner instruction (2026-08-31): review the whole surface chain and plan it in the smallest
possible tasks. This section is the plan; scheduling and status go to `roadmap.md` when a task is
picked up. IDs are `SF-<track><n>`. Tracks A and C are independent and can run together; B is
small and precedes D; D is the material slice proper.

**Finding that reorders everything.** The full export_v2 run is complete for install members, but
the texture and material seams enumerate VPK and loose files only. The 9,576 files embedded in the
108 BSPs' PAKFILE zips — 7,501 patched `.vmt`, 1,325 probe `.tth`, 750 `.ttz` — are recorded by
the corpus index under each map's `embedded[]` with `asset: null` ("became nothing"), while
`seam_map_map.md` → "PAKFILE routing" states they become ordinary units and
`unit_contract/origin.py` already defines the `bsp-pakfile` origin. The map root's
`cubemaps[].resolved` and `pakfile.entries[].unit` resolve against the zip, not against a unit on
disk, so nothing failed. The contract exists; the exporter half was never built.

### Track A — close the PAKFILE export gap (export_v2)

- **SF-A1 Make the gap visible.** Corpus index `summary` counts embedded members with
  `asset: null`; `uv run elysium doctor` reports the number. Done when the count (9,576) prints and
  a test pins it. No behaviour change.
- **SF-A2 Probe textures as units.** The texture seam takes a second enumeration over each BSP's
  PAKFILE `.tth`/`.ttz` pairs and emits `textures/maps/<map>/c<x>_<y>_<z>.glb` with the
  `bsp-pakfile` origin, capsule bytes included. First establish what the 575 `.tth` without a
  `.ttz` are (header-only? mip-less?) and record it in `seam_map_texture.md`. Done when 1,325
  units exist and validate.
- **SF-A3 Patched materials as units.** The material seam does the same for PAKFILE `.vmt`,
  emitting `materials/maps/<map>/<mat>_<x>_<y>_<z>.glb`; `$envmap` resolves to the A2 texture unit;
  the base material and the probe origin are recorded as dependencies (`role: material`,
  `patchOf`; `cubemapOrigin`). Done when 7,501 units exist and validate.
- **SF-A4 Index claims them.** `_pakfile_members` finds every embedded key claimed; the A1 count
  goes to 0; the map validator fails a map whose `cubemaps[]`/`textures[]`/`pakfile.entries[]`
  names a unit that is not on disk. Same task: texture edges in `index.glb` carry `parameter`
  (slice-2 follow-up).
- **SF-A5 Re-export and re-import textures.** Full `export_v2` run, doctor, then
  `uv run elysium import textures` picks up the 1,325 probes as `TC_` under
  `/ElysiumBaked/Textures/maps/<map>/`. Ledger rows and counts in this file updated.

### Track B — surface properties import (slice 3, tiny)

- **SF-B1 Asset class.** `UElysiumPhysicalMaterial : UPhysicalMaterial` with the fields Unreal
  lacks (movement, footstep pools, impact matrix, sound-script IDs, `gameMaterial`), a provenance
  `UAssetUserData`, and `EPhysicalSurface` entries in `DefaultEngine.ini` for the compact classes.
  Substrate test for the JSON apply.
- **SF-B2 Stage.** Python resolves each unit's `base` chain to flat values (the doc says
  inheritance is the consumer's), writes a manifest + sidecar. Test: `weapon` root, a three-deep
  chain, a repeated-scalar anomaly.
- **SF-B3 Import.** Editor script writes `/ElysiumBaked/SurfaceProperties/PM_<name>` (63), recipe
  stamps, registry tags, idempotent rerun. Sound references stored as asset IDs; they flip to
  hard `USoundWave` refs in the sound slice.
- **SF-B4 Docs.** `seam_map_surface_property.md` gains "## Import"; ledger row here.

### Track C — reflections: knobs first, then the owner tunes in PIE

- **SF-C0 Knobs.** (1) `UElysiumSurfaceSettings : UDeveloperSettings` — `DefaultSpecular`,
  `DefaultRoughness`, `DefaultMetallic`, `LightSpecularScale`, `MaskRoughnessMin/Max`,
  `MaskSpecularScale`, `EnvTintScale`, `FixedCubeStrength`; `PostEditChangeProperty` writes them
  into `MPC_ElysiumEnvironment` and the map light rig, so a PIE session follows the slider.
  (2) `UElysiumSurfaceCalibration : UDataAsset` — one row per surface class (roughness, specular,
  metallic), regenerating a 64×1 lookup texture on edit; masters sample it by a class index the
  instance carries. (3) The legacy world masters read the collection and the LUT instead of
  `ROUGH_BASE`/`SPEC_BASE`/`ROUGH_REFLECT`/`SPEC_REFLECT`; `bake_map.py` reads the settings class
  for `specular_scale` instead of the literal. Done when dragging `DefaultSpecular` in Project
  Settings changes a running PIE map and Ctrl+S persists it to `Config/DefaultElysium.ini`.
- **SF-C1 Owner tunes the defaults.** Open a baked map in PIE, tune the C0 globals, save. No agent
  in the loop; the committed ini is the deliverable. Optional: `shots_diff.py` before/after as a
  record.
- **SF-C2 Owner tunes the class table.** Fill the C0 data asset (classes keyed by `$surfaceprop`;
  materials with none fall back to the shader-family default row), tune in PIE, save. The asset
  is the deliverable; `docs/vtmb/surface_properties.md` only points at it.
- **SF-C3 Probe origins → reflection captures.** `bake_map.py` reads the export_v2 map unit's
  `cubemaps[]` origins and places one `SphereReflectionCapture` per origin; the capture radius is
  a C0 setting. Faithful placement, modern content. Independent of A2/A3 — origins are in the map
  root already.
- **SF-C4 Mask and tint mapping.** The mask → roughness/specular curve and the `$envmaptint`
  grey-vs-chromatic split are C0 settings; the owner tunes them on two masked surfaces in PIE and
  the result is written as a Settled entry here and as the `EnvMap` feature spec for D1.
- **SF-C5 Authored fixed cubes.** `FixedCubeStrength` from C0 on the ~340 `envmap/*`-naming
  materials and the `$envmapmode` sphere variant (80); the owner looks at the Asylum cube once in
  PIE and settles literal-sample-or-not.

### Track D — materials import (slice 4)

- **SF-D1 Master inventory.** Table: 42 resolved programs + the 8 real unresolved families
  (`worldvertextransition`, `decalmodulate`, `refract`, `cable`, `shatteredglass`, `cloud`,
  `heatglow`, `worldtwotextureblend`) → master → blend mode → parameters. Each row cites its
  shader-program unit. Debug/tool families listed as "no master, provenance only".
- **SF-D2 Parameter table.** All 229 VMT keys → destination (texture / scalar / vector / static
  switch / master choice / physical material / runtime / provenance-only). Rule: a key with no
  destination fails staging.
- **SF-D3 Proxy policy.** The 20 proxy kinds → shader-time node (`Sine`, `TextureScroll`,
  `AnimatedTexture`, `TextureTransform`, noise), runtime C++ (`PlayerProximity`, `PlayerPosition`,
  `PlayerSpeed`, `GlobalWetness`, `TextConsole`), or provenance-only. One table.
- **SF-D4 Naming and identity.** `/ElysiumBaked/Materials/<dir>/MI_<stem>`; patched map materials
  under `maps/<map>/`; masters stay in `Content/ElysiumGenerated/Materials/`. Settled entry.
- **SF-D5 Provenance class.** `UElysiumMaterialProvenance : UAssetUserData` (raw ordered
  parameters, source SHA, resolved program, proxies, anomalies, coverage) + registry tags
  (`ElysiumShaderProgram`, `ElysiumMaster`). Substrate test.
- **SF-D6 Masters, one task per family, transcribed from the shader units:** D6a Lit
  (opaque/masked/translucent; selfillum, envmap, bump switches), D6b Unlit (+`$ignorez`,
  `$vertexcolor`/`$vertexalpha`), D6c Eyes, D6d Water, D6e Sprite, D6f Refract, D6g Decal,
  D6h Additive, D6i TwoTexture/VertexTransition. Each rewrites its `make_*_materials.py` graph
  and cites the program it transcribes; lighting terms (`v0`, lightmap) are Lumen's.
- **SF-D7 Stage.** GLB → manifest: master by resolved program; texture params →
  `/ElysiumBaked/Textures` (`_linear` twin for data-class bindings); scalars/vectors/switches by
  D2; `PhysMaterial` by `$surfaceprop`; `$envmap` concrete → `TC_`, symbol → runtime bind; proxies
  by D3. Every unmapped key is a listed stage failure.
- **SF-D8 Import.** Editor script writes the `MI_` assets, applies provenance, stamps, saves;
  compile check per instance; `import_report.json`; full corpus first run; idempotent rerun.
- **SF-D9 Parity oracle.** A numpy ps.1.x interpreter over `shader-programs/source/*` units, run
  against one master's post-lighting terms on fixed inputs; extend per D6 family. Lighting terms
  excluded by design.
- **SF-D10 Consumers, one task each:** D10a `bake_map.material_for` → `MI_`; D10b character bake
  slots and skin families; D10c `FElysiumMaterialFactory::Create(MI_)` with runtime binds
  (`env_cubemap` symbol, D3 runtime proxies, fog primitive data); D10d wield/UI sprites.
- **SF-D11 Retire.** Per-map material packages, `/ElysiumBaked/Shared/Textures`, the legacy
  `SourceCube` wetness path (owner decision under C4), `tex/cube/` sidecars. Ledger rows here
  move to retired.

### Deferred from slice 2, unchanged

Sky cube faces, rope/cable materials and eye irises still read loose files (`shared/tex`,
`npc/tex`, `retex_dds`, `tex_hi`); they flip after D10c since they are material consumers.

## Open questions

**575 probe `.tth` members with no `.ttz`.** Of the 1,325 PAKFILE probe textures, 750 have the
`.ttz` payload twin and 575 do not. Before SF-A2 emits them: header-only probes, mip-less small
cubes, or something the map seam should own? Answer belongs in `seam_map_texture.md`.

**Where do the props go?** A map's geometry, materials and textures already bake to `.uasset`, but
static-prop placement still travels as the `<map>.props` sidecar that
[bake_map.py](pipeline/unreal/bake_map.py) reads. In export_v2 the placements are already inside
the `vtmb:map:` unit as scene nodes (`seam_map_map.md`, "Static props"), so the question is on the
bake side, and there are two candidate answers:

- fold the placements into the baked `.umap` as actors, so opening the level is all it takes; or
- emit a companion `UDataAsset` the runtime spawns from, keeping placement inspectable and
  reloadable without a level rebake.

What would settle it: whether anything needs to change prop placement without rebaking the level
(a debug surface, a live tweak, a per-session variation), and whether folding them in breaks the
shared-vs-per-map split the corpus bake depends on. Needs a test, not an argument.

**Cubemap block rotation below 4×4.** The texture exporter rotates a BC-compressed cube face
into glTF orientation by permuting its 4×4 blocks and rewriting selector bits, which is exact only
while a level is at least 4×4: at 2×2 and 1×1 the valid texels move out of the valid region. The
import lane applies the exact inverse, so the staged DDS round-trips byte-for-byte and no asset is
affected, but the published payload's own orientation at those levels is wrong and
`seam_map_texture.md` → "faces" overstates it as exact. Owner call pending on whether to fix the
exporter (rotate the decoded texels of a sub-4×4 level instead) or document the bound.

**What happens to the resources that cannot become Unreal assets?** VtMB's level scripts and
dialogue are loose `.py` and `.dlg` text imported into an embedded CPython VM at map load; `cfg/`
is Valve console syntax the console bridge seeds itself from; `vdata/` is the whole RPG rules layer
as KeyValues. Some of these plausibly become `UDataAsset`s, but each is its own call — a script the
VM imports by path is not the same problem as a rules table read once at startup. Undecided, and
deliberately so: this is the question the migration has to answer, one resource at a time.
