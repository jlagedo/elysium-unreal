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
| `<map>.hulls`, `.dispcol`, `.spawn`, `.ropes` | UE_bsp_to_scene | runtime (`.ropes` carries a `vtmb:material:` id since R6.5; the cable binds the imported `MI_`) |
| `<map>.props`, `.decals`, `.water`, `.materials.json`, `.weather.json`, `.particles.json`, `tex/cube/*` | UE_bsp_to_scene | bake |
| `<map>.sprites` | UE_bsp_to_scene | **retired** (R6.1: the V2 bake places the sprites off the staged `sprites[]` table) |
| `shared/tex/*`, `npc/tex/*` | UE_extract_corpus, npc_export | **both** (runtime: legacy-map sky cube faces, eye irises; rope/cable materials stopped at R6.5, and `tex_hi/` has no reader) |
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
| `signs/tex/*`, `backgrounds.json` | UE_extract_signs | **retired** (R6.6: sign backgrounds are the texture lane's `T_`) |
| `ui/strings.json` | UE_extract_ui | runtime |
| `ui/art/**`, `ui/menu/**`, `ui/effects/*` | UE_extract_ui | **retired** (R6.6: every UI image is the texture lane's `T_`; the wallpaper is `UElysiumUISettings::MenuWallpaper`) |
| `ui/resource/`, `ui/manifest.json` | UE_extract_ui | none |
| `particles/**` (raw mirror + `*.png`) | UE_extract_particles | bake |
| `hud/use_icons.png` + `.json` | UE_use_icons | **retired** (R6.6: 72 brushes on the `hud/context_icons/` `T_` assets; the exporter is deleted) |

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
  corpus index carries the edge and, since SF-1.5, the `parameter` it was read from): a texture is
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
- **The 1,325 SF-1.3 reflection probes land in the same slice.** Once SF-1.5/1.6 claim and
  publish them, `uv run elysium import textures` picks them up as `TC_` under
  `/ElysiumBaked/Textures/maps/<map>/` alongside the rest: 1,325 imported, 11,825 reused, 0
  pruned, 0 failed (`import_report.json`), 13,150 texture assets on disk in total.

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
`Config/DefaultElysium.ini`) for global scalars, mirrored into `MPC_ElysiumSurfaces` on edit;
a `UDataAsset` edited as a grid for per-class tables, regenerating a lookup texture on edit; and
the Material Instance editor for one material. Forbidden: an agent tuning values in a
build–launch–look cycle, an agent capturing frames to "calibrate", and any constant that needs
taste living in a Python or C++ literal. `validation/shots_diff.py` may record before/after; it is
never the tuning method. PIE is the viewing window (`debug-tooling.md`: PIE is a viewer, and it
picks up settings and collection edits without restart).

**The 575 ttz-less probes are ordinary small inline textures (2026-08-31).** SF-1.2 investigated,
read-only, ahead of SF-1.3. Of the 1,325 baked reflection probes embedded in the 108 map BSPs'
PAKFILE zips, 575 carry no `.ttz` twin because each one's whole admitted mip pyramid — all 7 VTF
faces down to 1×1 — already fits inside the `.tth`'s inline image range; the map compiler simply
wrote no external `.ttz` stream for them. Confirmed on all 575: the outer mip table's declared
`.ttz` length is `0`, the inline bytes already exceed 7 full-res faces, and
`tex_to_png.decode_cubemap(tth, ttz=None)` decodes all six kept faces cleanly. 552 are uncompressed
`BGR888`, 23 are small `DXT5` `cubemapdefault` probes; the split is the ordinary per-texture
inline/external size threshold every VtMB texture uses, not a clean format rule — 27 `BGR888`
positioned probes and 78 `DXT5` `cubemapdefault` probes still carry a `.ttz`. Detail:
`seam_map_texture.md` → "Probes without a `.ttz`". SF-1.3 needs no special handling: the texture
unit contract's `.ttz` is already optional and `texture_glb.decode` already carries it as such.

Owner answers on the knob set (2026-08-31): **no per-material tuning layer** — the global
settings and the class table are the whole authoring surface, and an edit to an imported `MI_`
is a throwaway experiment the next import overwrites; **class key** is `$surfaceprop` when
present, else the VMT's top directory (`brick/`, `concrete/`, `metal/`, `glass/`, `wood/`, …),
else the shader-family default row; **light specular scale is one global knob**, no per-map
override; SF-C0-before-Track-A was superseded the same day by "new pipeline first" (see the Plan). The settings object is the single writer of
`MPC_ElysiumSurfaces`'s surface scalars — the Cog Environment window's sliders become views
onto the same settings object rather than a second writer. Knobs are editor-only; a packaged build
never re-runs the editor push, it reads whatever values were cooked into `MPC_ElysiumSurfaces` as
of the last editor push (`UElysiumSurfaceSettings::PushToCollection`, re-run automatically on every
editor boot so the cooked collection cannot drift from the tracked ini between an edit and the next
cook).

**Material import design (2026-08-31).** SF-3.1–3.4, docs only. The whole design is
`docs/architecture/seam_map_material.md` → "Import"; the numbers come from a corrected scan of all
11,624 install material units (`$ELYSIUM_WORK_ROOT/import/design/scan_materials_v2.py`).

**Master set.** Nine masters under `Content/ElysiumGenerated/Materials/V2/`: `M_V2_Lit`,
`M_V2_LitTranslucent`, `M_V2_Unlit`, `M_V2_Eyes`, `M_V2_Water`, `M_V2_Sprite`, `M_V2_Refract`,
`M_V2_Decal`, `M_V2_TwoTexture`. The split is by Unreal *material-only* property — shading model,
translucency lighting mode, refraction, the modulate blend — because blend mode, two-sidedness and
the opacity-mask clip value are per-instance `BasePropertyOverrides` and therefore do not multiply
masters. That is why there is no separate Additive master, and why one Lit master carries opaque
and masked alike. All 54 resolved pixel+vertex program pairs map into those nine; 11,544 of the
11,624 units take a master and the remaining 80, in 36 debug and tool families, are imported as
provenance-only instances. The `vertexlitgeneric` bump-mapping second pass is *replaced* rather
than transcribed — it is a normal-mapped cube reflection, which is what Lumen already does — so it
collapses into two switches on the first-pass instance instead of a tenth master.

**Naming.** `vtmb:material:<dir>/<stem>` → `/ElysiumBaked/Materials/<dir>/MI_<safe stem>`, mirroring
the install path exactly as the texture slice mirrors it; SF-1.4's 7,501 patched map materials land
under `maps/<map>/<dir>/` and are parented to **their base instance, not to a master**, overriding
only what their `replace`/`insert` blocks name — VtMB's `include` semantics in the one Unreal
mechanism with the same shape. The class index and the `PhysMaterial` come from one resolution —
`$surfaceprop` (4,605 units), else the VMT's top directory when it names a surface class (558),
else the shader family's default row (6,461) — so a surface's sound and its shine can never
disagree.

**No silent drop.** All 229 authored parameter keys of the install corpus — plus `include`, the
one key the patched corpus adds — all 25 proxy kinds and all 54 program pairs have a named
destination in the doc. A unit carrying anything not in those three tables is a stage
failure with the unit named, never an instance written with the unknown part quietly missing.

**Proxy policy.** Of the 25 kinds over 217 materials, `sine`, `animatedtexture`, `texturescroll`,
`texturetransform`, `linearramp` and the `add`/`subtract`/`multiply`/`abs`/`exponential` arithmetic
become material-graph nodes; `globalwetness`, `playerproximity`, `playerposition`, `playerspeed`,
`textconsole`, `shadow` and `breakablesurface` are bound later by the runtime factory; `camo`,
`waterlod`, `lampbeam`, `lamphalo` and `particlesphereproxy` are provenance only. `lessorequal`
(a branch that selects between two parameters *by name*) and the two noise proxies (per-frame
random) are honestly not expressible as nodes, so any chain containing one goes to the runtime
whole — never half a chain in the graph and half in C++.

**Reflection contract.** `$envmap` presence (2,610 units) means the surface is reflective and
nothing more; the image is Lumen's. The masks — `$envmapmask` 2,368, `$normalmapalphaenvmapmask`
136, `$basealphaenvmapmask` 14 (inverted), none 92 — drive roughness and specular through the
`MaskRoughnessMin/Max` and `MaskSpecularScale` knobs, never opacity. `$envmaptint` splits on a
measured 0.02 channel spread into 362 grey (a specular scale through `EnvTintScale`) and 104
chromatic (metallic plus base-colour tint — VtMB's own hand-authored metal mask). The 2,217
`env_cubemap` symbols bind no asset and rely on the reflection captures SF-6.2 places; a patched
map material's concrete `TC_maps/<map>/c…` is recorded on the instance and never sampled, because
probes are not reflection content — and since 7,448 of the 7,501 patch rows set nothing but
`$envmap`, most patched instances override nothing at all; only the 342 authored `envmap/*` cubes
keep a literal additive cube sample, scaled by `FixedCubeStrength` and switched to black for ray and Lumen-card passes.
The `$envmapsphere` variant needs no master and no switch: of its 11 users (review finding 8 —
corrected from an earlier miscount of 12), nine are `shadertest/` or `dev/` and the other two are
one break-glass pair, one of which has no `$envmap` at all.
Non-`$envmap` surfaces take `DefaultSpecular`/`DefaultRoughness`/`DefaultMetallic` modulated by
their class-table row — the repudiated three-zeroes rule's replacement.

**Revised after review (2026-08-31).** An independent review of the design found holes; the
revision is in place in `seam_map_material.md` → "Import" and the rulings it encodes are:
**names** — the design doc's spellings are the contract (`NormalMap`, `EnvMapMask`, `EnvMap`,
`EnvMapTint`, `SelfIllumAmount`, `Use…` switches, `M_V2_Lit` with no `_Opaque` suffix), and the
build-mechanics note was corrected to match, together with its patched path
(`maps/<map>/<dir…>/MI_<stem>` — flattening the subdirectories collides on 113 names and loses 115
units), its `$alphatest` clip (**0.5**, transcribing Source's `AlphaFunc GEQUAL 0.5`;
`$alphatestreference` is a registered parameter with no corpus author, so it is a latent key mapped
to `OpacityMaskClipValue`), and three withdrawn claims: `$additive` sets a blend override and never
`MSM_UNLIT`, `texkill` is a clip plane and never `BLEND_MASKED`, and
`MaterialInstanceBasePropertyOverrides.h` lives under `Runtime/Engine/Public/Materials/`.
**Static switches are not capped** — the corpus realizes 61 combinations on Lit and 31 on Unlit,
and the cook pays per realized combination, which SF-4.5 measures rather than assumes.
**Five knobs added** to `UElysiumSurfaceSettings` → `MPC_ElysiumSurfaces`: `MaskMetallicMax`,
`ChromaticTintStrength`, `ChromaThreshold` (0.02, the grey/chromatic split, read by the stage from
the ini and never a literal), `Overbright` (2.0) and `DecalDepthOffset` — which is a knob *only*,
and left the per-instance parameter list.
**`SurfaceClassIndex` is stable**: each calibration row carries a fixed `Index` assigned at seeding
from the pinned `SURFACE_CLASSES` list (`importers/materials.py`, `default` at 0, **72 rows**, so
the LUT is 128×1), the texel is the row's `Index` and not its array position, every master exposes
`SurfaceClassLUT` and the three reads are named `ClassRoughness`/`ClassSpecular`/`ClassMetallic`.
**Class fallback** is `$surfaceprop` lower-cased (with `cloth` 30, `bone` 9, `asphalt` 2 and
`leather` 1 getting class rows of their own, their `PhysMaterial` still falling back to
`PM_default`), else the VMT top directory when it is on a **curated 16-name allowlist**
(`grass` added), else a **per-family default row** — `default` for Lit/Unlit/Sprite/Decal/
TwoTexture, `flesh` for Eyes, `water` for Water, `glass` for Refract — with `surfaceClassSource`
recorded. Tiers now measure 4,605 / 559 / 6,460.
**`env_cubemap` is not a runtime bind** — Lumen and SF-6.2's captures supply the image with nothing
bound — and patched concrete cubes, `cubemapdefault` copies, `$crackmaterial` and `$bottommaterial`
are provenance only; the `BottomMaterial` texture slot was deleted from `M_V2_Water`.
**Precedence**: an authored `envmap/*` unit takes the fixed-cube add (× `EnvMapTint` ×
`FixedCubeStrength`) and **not** the chromatic branch, which is `Metallic = mask * MaskMetallicMax`
and `BaseColor = lerp(BaseColor, BaseColor * EnvMapTint, ChromaticTintStrength)`.
**Self-illum is a named divergence**: the master implements the plain spelling
(`BaseColor *= 1 - BaseTexture.a`, `Emissive = BaseTexture.rgb * SelfIllumTint * BaseTexture.a *
SelfIllumAmount`), while the V2 masked spelling multiplies and squares the base; both instruction
lists are printed.
**Patched-corpus counts corrected**: 7,499 patches plus 2 standalone `maps/sm_tattoo/…`
`lightmappedgeneric` materials (the "2 pure aliases" claim is withdrawn), `patchOf` filename-derived
from the coordinate suffix, 7,450 `$envmap` rows, 7,431 instances overriding nothing — a considered
cost — and only the 49 `$waterdepth` rows of the 68 non-`$envmap` rows changing a material
parameter.
The revision also filled the holes the review named: water and TwoTexture now host their proxies
and keys, `M_V2_Unlit`'s normal-map rule is retracted (0 users), scroll splits into independent
base and normal lanes, `ForceRefract` is dropped (0 users), **every parameter on every master has a
stated default and a `_linear`-twin classification**, `VampireEyes` is exposed with a graph
identical to the non-vampire path pending decompilation, and `$spriterendermode` becomes blend
rows.

Provisional owner calls now open, all listed in the design: the `$alphatest` clip of **0.5**;
`decalmodulate` → `BLEND_Modulate` instead of retail's wireframe fallback (Unlit, out of the Lumen
surface cache, not Nanite-compatible; and the `Ray Tracing Quality Switch` does not gate the path
tracer, so the path tracer sees the fixed-cube add); the fixed cube going to **Emissive** behind
that switch rather than VtMB's pre-lighting composite point; **`$envmapsphere` gets nothing** — 9
of its 11 users (review finding 8 — corrected from an earlier miscount of 12/10) select a
`*_EnvMapSphere*` vertex program but are `shadertest/`/`dev/` and the two shipped users are one
break-glass pair; **`$ignorez`** — the 5 `unlitgeneric` world users
re-route to `M_V2_Sprite` (already Unlit, two-sided, depth-test-off), the 3 `sprite` ones are
already there, the 9 `wireframe` ones are no-master, and the 1 `vertexlitgeneric` keeps `M_V2_Lit`
with the flag as a named divergence — no `M_V2_UnlitNoDepth`; and **`Detail` is dropped** — all 28
`$detail` authors are `shadertest/*` or a `shatteredglass` crack-material path, and this build's
`VertexLitGeneric` registers no `$detail` parameter at all.

**Surface properties import (2026-08-31).** SF-2.1–2.4. The lane is
`uv run elysium import surface-properties` and its contract is
`docs/architecture/seam_map_surface_property.md` → "Import". It mirrors the texture lane: an
offline Python stage over the published units writes a manifest plus one sidecar per unit, and a
headless editor phase authors the assets, stamps recipes and prunes. First run 63 imported,
0 failed; rerun 63 reused. The owner calls it rests on:

- **`SurfaceType` maps the compact material class, not the entry.** One
  `UElysiumPhysicalMaterial` exists per entry at `/ElysiumBaked/SurfaceProperties/PM_<name>`, and
  a hit's `PhysMaterial` *is* that asset, so entry identity needs no `EPhysicalSurface` slot — and
  could not have one, since the engine has 62 rows and the table has 63 entries. The 17 rows added
  to `Config/DefaultEngine.ini` are the distinct `gamematerial` letters (`VtmbGameMaterial_A` …
  `_Y`), assigned by alphabetical order of the letter so the mapping is derivable. After
  inheritance 56 of the 63 assets carry a letter (26 entries declare one); 7 keep
  `SurfaceType_Default`.
- **Inheritance is flattened at the stage, and the walk is recorded.** The seam deliberately
  publishes only what each entry declares, so resolving the chain is the consumer's job and this
  lane is the consumer. Root-first walk; a child's declared scalar overrides the parent's; a
  child's declared **pool replaces** the parent's pool *for that slot*, because Source copies the
  parent `surfacedata_t` and re-parses the child's keys — so an entry that names one `stepleft`
  supplies the whole left-footstep pool rather than appending to the four it inherited. Per field,
  the unit that supplied it is recorded in the provenance's `FieldOrigins`; that map is what makes
  a flattened asset auditable. A cycle or a dangling base refuses the unit and every descendant of
  it, naming the ancestor. Measured: 21 roots, 42 inheriting, deepest chain 4 units.
- **`default` is not an implicit parent.** No entry bases on it, so the movement fields it alone
  declares are not inherited; the asset class's defaults happen to equal `default`'s values, and
  the provenance shows no origin for them rather than claiming `default` authored them.
- **Asset naming is `PM_<safe_name(unit key)>` and is load-bearing.** Phase 4's material import
  resolves a VMT's `$surfaceprop` to that exact path by folding the name the same way.
- **Sound references stay strings** (`vtmb:sound:*`, `vtmb:sound-script:*`) on the asset. The
  sound slice flips them to hard `USoundWave` references; nothing downstream can bind audio yet.
- **The recipe hashes the whole chain, not just the unit.** A flattened asset's values change when
  an *ancestor's* GLB changes and its own bytes do not, so the recipe carries a `chainSha256` over
  every unit in the chain beside the unit's own hash. Editing `metal` re-authors `metalgrate`,
  `metalpanel` and `canister` and nothing else.
- **Provenance rides as `UElysiumSurfacePropertyProvenance : UAssetUserData`**, with `AssetId`,
  `GameMaterial` and `SourceName` published as asset-registry tags. `UPhysicalMaterial` implements
  no asset-user-data interface, so `UElysiumPhysicalMaterial` implements `IInterface_AssetUserData`
  itself the way `UTexture` does — which is also what gives a re-import its replace-rather-than-
  accumulate behaviour.
- **`elasticity` is carried twice.** The shipped range is 0.001–2 and Unreal's `Restitution` is a
  0–1 bounciness, so the asset holds the clamp *and* `RawElasticity`. `friction` is **not** clamped
  despite running to 100: Unreal's friction is not a 0–1 quantity, and clamping it would quietly
  change a surface the table meant to be extreme.
- **A `gamematerial` letter with no row is a stage failure**, not a silent default: the ini row has
  to exist for the class to mean anything, and a corpus that grew a letter should say so once
  rather than land 63 assets with one quietly wrong. A pytest pins the ini rows against the staging
  table.

**Material import landed (2026-08-31).** SF-4.5/4.7. `uv run elysium import materials` lands the
full 19,125-instance corpus with 0 failures; `--lookdev` builds the review map. Per-master counts:
`M_V2_Lit` 7,926, `M_V2_Unlit` 1,945, `M_V2_LitTranslucent` 1,114, `M_V2_Eyes` 406, `M_V2_TwoTexture`
95, `M_V2_Sprite` 67, `M_V2_Decal` 38, `M_V2_Water` 24, `M_V2_Refract` 11 — 11,626 ordinary units,
plus 7,499 patched map instances parented to their base instance rather than a master. 80 units
(36 debug/tool families) landed provenance-only. 159 distinct `(parent, switch-combination, blend
override)` permutations were compile-probed this run — none reported zero pixel-shader
instructions. First full run: 11,141 imported, 7,984 reused (from an earlier partial/smoke run),
0 failed, 681 s wall time. Rerun for idempotency: 0 imported, 19,125 reused, 0 failed, 8 s.
Lookdev: all 20 review-set entries placed (0 missing), 32 `StaticMeshActor`s all resolved to a real
`MI_` instance (0 placeholders).

**Revised the same day — the review pass closing findings 1-9, then a closing-verification fix
round.** The first `--stage-only` pass against findings 1-9 stated **48** loud stage failures
(`REQUIRED_TEXTURE_SLOTS`, finding 5, turning a `textureClassMismatch` on the one slot with no
other colour source into a per-unit failure) — mostly `sprites/`/`particle/`/`objects/` flipbook
units whose referenced texture staged as a `Texture2DArray` where a plain `Texture2D` was
expected. The first real full-corpus editor import under that state exposed two live defects (the
compile probe's hit-proxy/depth-only requirement was never true for a headless commandlet, and
`set_material_instance_static_switch_parameter_value`/the base-property-override write both
allocate on every call regardless of `bUpdateMaterialInstance`, the actual cost behind an observed
~1 instance/s import rate) — both fixed, and closing the loop on the 48 failures at the same time:
44 of them are exactly the `Texture2DArray`-where-`Texture2D`-was-wanted shape a static frame-0
fallback resolves (Source itself samples frame 0 of a multi-frame texture when nothing animates
it), so the stage now binds the array instead of failing the unit. **Final `--stage-only` numbers:
19,121 of the 19,125 units stage, 4 fail** — `dev/ocean`, `dev/oceanbeneath`, `envmap/gioint`,
`skybox/hav_env`, each a `TextureCube`-where-`Texture2D`-(or vice versa)-was-wanted mismatch with
no frames array to fall back onto (the `$envmapsphere`/break-glass pair the design already named).
Anomaly rollup over the 19,121 staged units: `textureClassMismatch` 97 (up from 53 — every
fallback-rescued unit still records the real mismatch, only its consequence changed),
`selfIllumOnUnlitSurface` 9, `misspelledKey` 4, `translucentValue` 2. Omission rollup:
`unitDivergenceProvenanceOnly` 15, `proxyTargetProvenanceOnly` **12** (13 until R7.1: ruling J
consumes the `objects/surf` `$temp[0]` sine into `SineUVTranslate`, so that one row stops being an
omission -- re-measured 2026-09-04 over the staged tree, 12 rows on 7 units),
`animatedFramesArrayUnavailable` 12 — the same 12 units the "known-stale" list names
(`sprites/mflash_{colt,mac10,shotgun}`, `models/scenery/structural/controlpanel/screenf`,
`models/scenery/structural/sewerparts/water_fall_{big,small}`,
`models/scenery/structural/temple/water_fall_small`,
`models/scenery/structural/plaguebearer_sewer/plague_waterfall{,01}`,
`models/scenery/structural/warrens/warr01_waterslide`,
`models/scenery/structural/bradbury/blood_pool`) — those aren't flipbook-array-unavailable
themselves (their own textures did stage), but every one of the 12 units this omission was
originally measured against still spot-checks with `UseAnimatedFrames`/`UseAnimatedNormalFrames`
explicit `False`, not merely absent. A 500-unit sample of the corpus's 7,499 patched instances
resolves a non-empty root `master` in every case (finding 7). `uv run elysium import materials`
(the full editor pass, re-landing all 19,121 staged instances against the new recipe shape) was
re-launched with both live fixes applied; see the commit history for the final
imported/reused/failed/seconds count once that run's `import_report.json` is captured. One
cross-cutting consequence for the lookdev lane (owned by another agent, not touched here):
`lookdev_set.json`'s `"Sprite (coplights)"` entry names `sprites/coplights`, which the static
frame-0 fallback now resolves cleanly, so that particular cross-reference risk did not
materialize.

**Closing verification run confirmed the masters against a real editor pass (2026-08-31).** The
numbers above were recorded from an import that ran against the *old* on-disk masters — the
graph-authoring edits in `make_v2_materials.py` (flipbook sampler, water refraction/fog, vampire
eyes, fixed-cube strength, `UseBaseTexture` defaults, plus the module's own
`_probe_all_switches_true` compile probe) had landed as source but never actually gone through a
real headless-editor `-PolicyForce=1` rebuild. This run did that: `UnrealEditor-Cmd.exe
ElysiumUE.uproject -run=pythonscript -script=pipeline/unreal/build_content.py
-AllowCommandletRendering -PolicyGenerators=make_v2_materials.py -PolicyForce=1` (the CLI's
`generate_policy_content` does not itself plumb `-PolicyForce`, so a from-scratch master rebuild is
this direct commandlet invocation, with `PYTHONPATH`/`UE_PYTHONPATH` set to the repo root and
`pipeline/src` the way `ProjectConfig.apply_environment` sets them for every other launch). All nine
masters recompiled clean and the all-switches-true probe passed for each — `M_V2_Lit`,
`M_V2_LitTranslucent`, `M_V2_Unlit`, `M_V2_Eyes`, `M_V2_Water`, `M_V2_Sprite`, `M_V2_Refract`,
`M_V2_Decal`, `M_V2_TwoTexture` — 0 errors, 7 warnings (benign linker "Error opening file" notices
from the forced texture-asset delete/recreate, and a "reference gathering" mode switch), commandlet
exit 0. `uv run elysium test Elysium.Policy.V2MasterParams`: 1 of 1 passed. Re-running `uv run
elysium import materials` against the rebuilt masters reused the existing stage: 0 imported, 19,125
reused, 0 pruned, 0 failed, 5 s — the manifest's parameter values (including the switches
`34e3fc01` staged) had already landed on every instance in the prior run, so only the master graphs
needed the real rebuild; the immediate idempotency rerun reported the identical 0/19,125/0/0. The
manifest carries `UseNormalMap: true` on 534 instances, `UseBaseTexture: false` on 338 (21,702
`true`), and 118 `BaseTextureFrames`/`NormalMapFrames` texture-array bindings (68/50).
`--lookdev` regenerated `/ElysiumBaked/Lookdev/Materials`: 20/20 entries placed, 0 missing.
`uv run elysium build` (0 errors), `uv run elysium test Substrate` (407/407), `uv run elysium
doctor` (repository policy passed, 22 pre-existing warnings) and `uv run pytest` (2981 passed) all
confirmed clean on the same run.

Two editor-only defects surfaced and were fixed:
- `import_materials.py` called `MaterialEditingLibrary.get_statistics` (the compile probe) on an
  instance whose static-switch `update_material_instance()` had already run *before* its
  `TwoSided` base-property override was applied, so the probed shader resource's cache key
  reflected a snapshot older than the override. Whenever `TwoSided` alone is the reason the
  editor's hit-proxy shader permutation is needed (opaque, writes every pixel, nothing else
  forcing it), UE 5.8 asserts rather than recompiles:
  `Assertion failed: ShaderMapId.ContainsShaderType(ShaderType, kUniqueShaderPermutationId)` /
  `"missing expected shader type FHitProxyVS"`. Reproduced deterministically on `cable/MI_cable`.
  Fixed by moving the one `update_material_instance()` call to after both switches and
  base-property overrides land.
- The offline stage (`importers/materials.py`) guessed a texture parameter's asset prefix
  (`T_`/`TC_`) from the parameter name alone, never checking what class the referenced texture
  unit actually staged as. Three corpus patterns broke that guess and produced 169 "texture not
  found" failures on the first full run: an `animatedtexture` unit's `frames > 1` source stages
  only as a `Texture2DArray` (`TA_`), never a plain `T_`, so its own static `BaseTexture`/
  `NormalMap`/`DuDvMap` slot named a `T_` asset nothing ever wrote (70 units); the `$envmapsphere`
  family (mostly `shadertest`/`dev`) points `EnvMap` at a flat `T_` sphere-map where a `TC_` cube
  is expected (9 units); and the `envmap/gioint`/`skybox/hav_env` break-glass pair do the reverse,
  a literal cubemap bound as a plain `BaseTexture` (2 units). Fixed by reading the texture's own
  sidecar (`faces`/`frames`, mirroring `textures.py::_texture_class`) and skipping the bind —
  leaving the master's default, recording a `textureClassMismatch` anomaly — when the staged class
  disagrees with what the parameter expects.

**Final verification run, after the static frame-0 fallback and the perf fix (2026-08-31).**
Closes the loop the two entries above left open: the nine masters' `b4576d12` graph edits (sine
lane widths, animated normal unpack, usage flags, TwoTexture UVs/opacity mask, `GRAPH_VERSION` 3)
still needed the same real `-PolicyForce=1` commandlet rebuild (`docs/project/seam_migration.md`'s
own recorded invocation, above) run again on top of `73814227`'s fixes. That run: all nine masters
recompiled clean, 0 errors, 7 warnings (the same benign linker/reference-gathering notices), the
all-switches-true probe passing for every one (pixel-shader instruction counts: `M_V2_Lit` 298,
`M_V2_LitTranslucent` 1,889, `M_V2_Unlit` 174, `M_V2_TwoTexture` 251, `M_V2_Eyes` 221, `M_V2_Water`
1,864, `M_V2_Sprite` 164, `M_V2_Refract` 1,839, `M_V2_Decal` 150). `uv run elysium build` (0
errors), `uv run elysium test Elysium.Policy.V2MasterParams` (1 of 1) and `uv run elysium test
Substrate` (408 of 408) all passed. A master-graph rebuild does not change any instance's own
recipe, so `uv run elysium import materials --force` (a from-scratch rebuild of all 19,121 staged
instances, to measure the perf fix at full scale) landed 19,121 imported, 0 reused, 0 pruned, 0
failed, 660 s (983 distinct permutations compile-probed) — roughly 29 instances/s overall, against
the ~1 instance/s the unoptimized full-state switch/override writes measured before `73814227`;
`phaseSeconds` in `import_report.json` puts `switches` at 0.8 s and `basePropertyOverrides` at
0.7 s total across all 19,121 instances (was the dominant cost), with the real per-instance work —
`probe` 248 s, `save` 79 s, `updateMaterialInstance` 76 s, `textures` 45 s, `scalarsVectors` 43 s —
now what the wall clock actually measures. An immediate unforced rerun confirmed idempotency: 0
imported, 19,121 reused, 0 failed, 8.5 s. Spot-checks: `sprites/mflash_colt` (one of the 12
`animatedFramesArrayUnavailable` units, whose own texture never staged as an array) carries no
stray `UseAnimatedFrames` override beyond what the manifest states (`False`, `FrameRate` 30 —
its `animatedtexture` proxy's own authored rate, recorded but never sampled); `sprites/coplights`
(one of the original 48) turned out to carry a real `animatedtexture` proxy of its own
(`animatedtextureframerate=4`), so it resolved through that proxy path rather than the static
fallback — `BaseTextureFrames` bound, `UseAnimatedFrames` on, `FrameRate` **4**, not 0. Units that
actually took the static frame-0 fallback (no proxy of their own) do show `FrameRate` 0 exactly —
confirmed on `sprites/candle` and `particle/smokeb`: `UseAnimatedFrames` on and overridden,
`FrameRate` 0.0, `BaseTextureFrames` bound to their own `TA_` sibling. `--lookdev` regenerated
`/ElysiumBaked/Lookdev/Materials`: 22 of 22 entries placed, 0 missing (`lookdev_set.json` grew from
20 to 22 entries in the same window as the frame-0 fix, unrelated to it). `uv run pytest`: 3,045
passed. `uv run elysium doctor`: repository policy passed, 22 pre-existing warnings (unchanged).

**Map convergence validated, and three of its five opening premises reversed (2026-08-31).** The
owner's direction: converge the map sidecars into the Unreal map — real objects where that is
true, assets where it is not — wire the V2 materials, cubemaps, fog and every dormant visual lane
into the maps, and end with the legacy exporter+bake+runtime-sidecar path deleted and the game
running entirely off the new lane. A four-angle validation (adversarial design review, engine-source
fact-check against the UE 5.8 install, a field-by-field `.ents`-vs-entities-GLB contract diff, and
a corpus-wide missing-life sweep) confirmed the framing — new lane beside legacy, then delete —
and reversed three specifics:

- *Brush bodies are not baked as level actors.* Ownership (`RouteTouch` casts `GetOwner()` to the
  map actor), per-entity spawnflag solidity patched after spawn, dormancy's forced
  `UpdateOverlaps`, Movable mobility under the mover, and the entity-index save key all live on
  map-actor components built at load. The bake ships the cooked **hull payload** (the expensive
  part) as a per-map asset; `BuildBrushBody`/`ElysiumMapCollision::Build` keep constructing the
  components from it. Adoption-by-tag for bodies is rejected outright.
- *"Merge-by-name" light overrides cannot exist.* `dworldlight_t` rows are anonymous; the Cog
  overlay, the baked `elysium.src` tag and the rig's `RowBySource` join are all keyed by `.lights`
  line index. The stable key becomes the lump-15 record ordinal. (The one-shot remapper
  planned here was dropped the same day: no `_lights/*.json` exists on disk, and the owner
  re-tunes in the editor.)
- *Lights, sky, fog and the skylight cubemap stay runtime-tunable.* Lightstyles animate per frame;
  `LightScale`/`LightFit`/`LightCurve`/`SkyProbe`/`EnhancedTextures` are open calibrations; fog is
  per-primitive CPD so the miniature and world fog differently at equal depth. These values move
  from loose sidecars to per-map **data assets** (regenerable without a level rebake, editable
  live — the knobs mandate), never to baked actor mutation.

The contract diff established that `.ents` is a *join*, not a projection: hulls, contents,
`blocks_player`, `brush_mesh` and sky-membership come from root-unit lumps the entities GLB never
carries, and the exporter's hull solver tolerances (1e-6 determinant, 0.05 halfspace, 0.1 dedupe)
are load-bearing. Save games apply entity state **by index** (`ElysiumEntityWorldPersistence.cpp`),
so the replacement must emit one row per lump block in lump order, no drops, no reorders, and the
cutover bumps `FElysiumSaveVersion` so old snapshots are refused rather than misapplied. Output
typing moves to the datamap (`outputLike` demotions) as a *named* divergence with its own
save-schema bump — the restore path length-gates `OutputTimesRemaining` and silently drops
counters on a cardinality change. Engine-source verdicts (all confirmed): custom trimesh collision
needs an `IInterface_CollisionDataProvider` outer (the ProceduralMeshComponent pattern) or Chaos
silently cooks nothing; physics cooking is paid once at the normal cook, not at bake-save;
`UCableComponent` rebuilds sim state from saved properties; reflection captures build headlessly
under `-AllowCommandletRendering` and remain a Lumen *fallback* lane (consistent with "baked
probes are not reflection content" above — a capture of the Lumen-lit scene is the intended
image, not a regression); `SLS_SpecifiedCubemap` refilters automatically on registration; classic
persistent levels carry no 5.8 deprecation pressure; per-map `UDataAsset`s hard-referenced from a
level actor cook and load with it; 400–700 Movable lights ride VSM page caching but owe a real
profiling pass. The life sweep found the wiring targets: ~217 world materials with animation
proxies flattened to static by the legacy `.mtl` path (the V2 masters already implement them — a
rebind is the win), lightstyle flicker dead-flat everywhere, detail props (`dprp`) exported and
never placed, water shipped as a flat translucent film beside a finished `M_V2_Water`, the
`.sprites` corona sidecar written and never read, ~7,487 of 7,501 cubemap-patched materials
falling back to generic reflection, and an untouched steam/embers/fire/beam entity family. The
plan lives in "## Roadmap — one pipeline" below (the maps track was merged into it the same
day); the shared-MI switch is blocked until decal fog and wetness have a home that is not a
per-map material instance (R5.3).

**The Unreal editor is the tuning surface (2026-08-31).** Owner ruling, generalising the knobs
mandate: "we are simplifying and adopting Unreal — if something needs tuning we already have a
full editor surface." Cog was an early-days debug tool; anything it tunes that the editor can
tune is dropped from Cog, not migrated within it. The full audit (15 windows, 74 cvars, ~100
commands) classified every surface: **keep as debug** — entity inspector, I/O wire debugging,
event-queue stepping, logic quick-fire, maps/travel, world viz, audio probe, NPC window, scripting
VM, the green room (which keeps the Cog plugin alive; it is debug tooling, not a tuning UI);
**drop to editor** — every lights/sky/fog/rig slider, the per-light edit grid, weather
enhancement/streak values, green-room eye tuning; **delete** — the light-leak A/B scaffolding,
`elysium.SkyProbe` (RE-A2 closed), the `MaterialOverrides` cvar group (superseded by
`UElysiumSurfaceSettings`), dead `elysium.RainMist`. Seven more taste values live only in C++
literals behind cvars (`MenuScrim`, `JawSpeechLevel`, `JawSmoothing`, `CameraCutSeconds`,
`MusicCrossfade`, `SchemeRandomBase`, `LoadingScreenMinTime`) — same defect, same cure: settings
pages. Lane verdicts: **lights** — the bake writes final calibrated values (plus the MegaLights
properties, today restated every load); global calibration moves to a
`UElysiumLightingSettings : UDeveloperSettings` page pushing per-world on terminal value-set; per-
light hand-tunes are a per-map `UElysiumLightCalibration : UDataAsset` keyed by the lump-15
ordinal (merge rows, rebake-proof) that a **slim runtime rig** applies — the rig survives because
lightstyles animate per frame, but its wholesale re-derivation dies. No `_lights/*.json` remapper:
no such file exists on disk and the owner re-tunes in the editor. **Sky** — fully bakeable: cube
import keyed by sky name (six game-wide), `SLS_SpecifiedCubemap` assignment, and the
`CubeUpperMean` intensity join all run at bake; the runtime sky-assembly path deletes whole.
**Fog** — the per-primitive CPD values are a pure function of `.env` with no taste in them; they
move to the per-map asset feeding the unchanged stamping path, and the height-fog actor is edited
directly (its Cog sliders were already being discarded on save). Residual constraints: the green
room parses `sp_theatre.ents` directly and must move to the entity asset before the readers die;
`Elysium.Substrate.LightRig` asserts the runtime derivation and re-homes to bake verification; the
seven "Enhanced rain" literals exist nowhere but a Cog button and get captured into their settings
home before the window goes.

**Props/models seam validated — the next lane, and it goes first (2026-08-31).** Owner ruling:
props are built and baked before maps — maps place props by name, props are where the V2
materials meet meshes, and the legacy shared CorpusBake must die with the old pipeline anyway.
Corpus verdict: **ready, no export gap.** 4,445 model units on disk (5.90 GB, the largest seam);
every `models/**.mdl` install member is a unit; index `unclaimed: 0`, 10/10 cross-unit checks;
the 96 orphan `.vtx`/`.phy` residue rows are proven unreachable. Full-graph closure: 10,613
`role=model` edges, **zero unresolved from any of the 108 maps** (36 danglers are retail data
bugs from entities/vdata — doppleganger, `models/missing.mdl`, item placeholders → shipped
placeholder). Placement census: 26,203 static-prop records over 1,922 models (1,468 with
`skin != 0`), 143,412 detail-prop records over 41 models. Dependency closure: materials **green**
(10,265 model→material edges 100 % resolved, all 4,850 targets have their `MI_` on disk),
textures **green**, surface properties **amber** (79 edges over 17 unknown names — `cloth`,
`bone`, the shipped typo `defualt`… → documented fallback to default), physics **amber**
(VPhysics present on 2,211 of 3,185 placed models and cookable, but **collision authority is the
placement record**: 2,530 records demand VPHYSICS on 264 models with no `.phy`, 3,428 override a
present `.phy`, 6,971 place with no collision at all). Identity join verified end-to-end: map
`staticProps[].asset`, detail-prop nodes, entity `model.asset` and the unit's `identity.asset`
all speak `vtmb:model:*`; the per-placement `skin` joins by `props[i].node`. Gaps the import
contract must decide (ranked): the `vtmb:missing-material:` sentinel is **invisible to every
closure check** (1,523 slots / 597 units corpus-wide; 98 slots on 66 placed static props;
`warnings_for` never reads `coverage.omittedProven`, and the index keeps only the first reason
per label — both validator bugs to fix); skin families are a consumer join and **59 placements
index out of range** (engine clamps — the import must too), and the primitive's
`ELYSIUM_material_reference` is the family-0 answer only — the lane reads
`materialBindings.skinFamilies[skin][skinReference]`; `seam_map_model.md` has two false claims to
correct (v2531 has **no** header KeyValues region — `prop_data` does not exist in the seam; mass
and surfaceprop come from `physics.solids[].properties` — and static units **do** carry one
reference-pose animation); LOD policy undecided (VTX LOD chains up to 7 deep, `switchPoints` with
`-1.0` shadow rows, plus per-placement fade distances); bodygroups have no seam vocabulary (14
multi-submodel models; `sprp` carries no selector — static props bake submodel 0); no
`TEXCOORD_1` anywhere (Lumen-only, or generate lightmap UVs); 7 units with no admitted VTX
topology (none placed — skip loudly); 784 published units nothing references (import the 3,677
referenced; the rest provenance-only). The legacy naming contract is **load-bearing**: four
independent C++ call sites recompute `static_stem` live, so the fold
(`mdl.sanitize` + `safe_name`), the flat `SM_<stem>` layout under the shared mesh root, and
material **slot names** (`safe_name(material)` — skin swaps and the skeletal twin's
`BindMapMaterials` bind by slot name) must be reproduced exactly; mesh source format, LOD
strategy, the 7-master MIC scheme and verification (none exists today over the shared corpus) are
free to change.

**Props lane, R1.1–R1.4 landed on the test corpus (2026-08-31).** `seam_map_model.md` gained its
`## Import` contract (+516 lines) and lost two false claims (v2531 has no header KeyValues region;
static units carry one reference-pose animation). Decisions the contract took beyond the settled
list, all flagged: V2 root `/ElysiumBaked/Meshes` (flip = `FElysiumContentPaths::BakedSharedMeshes()`
alone, R5.1); import set 3,661 (16 of the 3,677 referenced ids are dangling → `SM_elysium_missing_model`);
LOD `ScreenSize = clamp(LodSwitchConstant / switchPoint, floor, ceiling)` on a
`UElysiumModelSettings` page (bake-time inputs, no runtime push); a `mdl.header.surfaceProperty`
tier between physics solid and default; duplicate folded slot names disambiguated `<name>_<slot>`
(fixes a latent legacy bug — Unreal resolves a duplicate slot name to the first index); uniform
`CTF_UseSimpleAndComplex` with the header-hull bbox **carried** by the asset when no `.phy` exists
while the placement decides use-vs-inert (VtMB stands such a prop inert). Validator: sentinel
slots now surface per unit and as `sentinelReferences` in the index summary + a doctor line
(backward-safe); the first-reason-per-label dedupe bug is fixed. Stage: `uv run elysium import
models --maps …` (unscoped refused), 414 units for the three test maps in ~6 s, 0 failed, anomalies
`missingMaterialSentinel=1, multiSubmodelBakedZero=3, surfacePropertyUnknown=2`. Import: **414
imported, 0 failed, 129.5 s** (`author` 77 s, `geometry` 21 s, `collision` 4.9 s), rerun **0
imported / 414 reused, 17.4 s**; `noPhysicsSolidsBoxFallback=131`. Spot-checks: a 7-LOD skeletal
unit lands as 6 LODs (the `-1.0` row dropped) with 15 convex shapes and authored mass; the
sentinel unit binds `MI_V2_Missing` (magenta/black checker on `M_V2_Unlit`, authored beside the
masters); a Nanite veto is recorded by slot and material. The provenance-shape bug recurred as
predicted (six real key mismatches between the parallel-built stage and reader — every one would
have read back empty) and was caught: the reader changed, and
`test_the_sidecar_key_set_is_exactly_the_pinned_one` pins the shared key set; the naming twin is
a golden `model_names.json` read by both a pytest and `Elysium.Substrate.ModelNames.PropModelStem`.
Non-manifold sections are predicted in Python and unwelded before `FDynamicMesh3` sees them (one
section on one unit in the 414). Tests: 8 + 8 + 5 pytest, 7 + 1 Substrate; Substrate 416/416.

**Props lane, R1.5–R1.6 landed on the test corpus (2026-08-31).** R1.5 (`15a3fb28`): the corpus
skin table, `/ElysiumBaked/Meshes/DA_ElysiumPropSkins`, authored as a finalize step of `import
models` from the staged manifest's per-unit `skinFamilies`/`familyCount` — family 0 and any family
identical to it get no row, and `FamilyCount` rides beside the trimmed `Families` array so
`UElysiumPropSkinSet::Find` clamps an out-of-range skin index to the model's last family (the 59
placements the R1.1–R1.4 census found) without loading the mesh; legacy `DA_ElysiumPropSkins` is
untouched and `Find` falls back to its pre-clamp behavior when `FamilyCount` is unset. R1.6
(`ab550d12`): the first shared-corpus parity check, `Elysium.Content.ModelParity.*`, reading the
staged manifest as ground truth against the real baked `SM_` corpus (slot names, collision setup),
plus a 6-mesh props row in the tracked lookdev map; closed the `POLICY_GENERATOR_OUTPUTS` follow-up
above (`MI_V2_Missing`/`T_V2_MissingChecker` now listed).

**Collision auto-detection divergence found and fixed same day (2026-08-31), ruled by the existing contract.** R1.6's
own parity test discovered `bake_lib.set_phy_collision` left `GeometryScriptCollisionFromMeshOptions`'s
`bAutoDetectBoxes`/`bAutoDetectSpheres`/`bAutoDetectCapsules` at the engine default (**true**), so a
box-shaped `.phy` ledge silently cooked as an `FKBoxElem` instead of the `FKConvexElem` the
"### Collision" contract above promises ("one convex shape per ledge... reproduced, not
approximated"); measured on the landed 414: 96 of 283 `.phy`-bearing meshes carried at least one
`FKBoxElem` (83 box-only, 13 mixed, 187 convex-only), and 0 sphere/capsule substitutions fired in
this corpus but the full 3,661-unit run (the step right after R1.6) was the real exposure. Ruling:
the contract is correct as written, so `set_phy_collision` now sets all three auto-detect flags
`False` and `SETTINGS_VERSION` bumped to `elysium-model-import-v2` to force a re-import; the 414
were re-baked (`uv run elysium import models --maps sp_tutorial_1 --maps sm_pawnshop_1 --maps
sm_hub_1`, 414 imported / 0 reused / 0 failed, 257.0 s), and `Elysium.Content.ModelParity.
SlotsAndCollision` now asserts `ConvexElems.Num() == hullCount` (not just total shape count) —
3 of 3 ModelParity tests pass, "414 audited (131 bbox, 283 phy), 0 missing, 0 slot mismatch(es), 0
collision mismatch(es)". The same test's mount-liveness probe used to abstain the entire sweep on
`Assets[0]` alone; it now runs every asset and abstains only when none resolve, failing loudly
otherwise. `make_missing()` (the code `5416ba8f` broke and `ab550d12` only patched the test doubles
for) gained its own coverage: parent master, bound checker texture, every switch, and the blend/
two-sided overrides, plus reuse-vs-`-PolicyForce` behavior.

**Baseline shots landed on the test corpus (2026-09-01).** R2.1 (`shots_diff.py`, no C++ change —
`FElysiumShotRun`/`ElysiumVantages.h` already existed from P2.9/sky-ambience B6). `--save` now
writes a `baseline.json` beside every promoted map's copied capture, naming the build commit
(`git rev-parse HEAD`, `"unknown"` off a checkout), the map, and the camera set — the sorted
vantage names whose capture actually `ok`'d, so a failed vantage never earns a place in what a
later diff compares against. `uv run elysium debug shots <map>` captured every configured vantage
for the three-map test corpus real-RHI at DX12/SM6, 2560×1440: `sp_tutorial_1` 6/6 (`spawn`,
`t1`–`t4`, `t1sky`), `sm_pawnshop_1` 4/4 (`spawn`, `p1`–`p3`), `sm_hub_1` 4/4 (`spawn`, `h1`,
`h2`, `h1sky`) — 14/14 `ok: true`, none dropped. `--save` promoted all three under
`$ELYSIUM_WORK_ROOT/exports/_shots/_baseline/` at commit `8077e5b5f9027362140a12e5c36b8d69aa065709`;
a same-commit re-diff proved the comparator end to end — 14 vantage(s) compared, 0 over the
default 0.5%-changed / 2-level tolerance. This is a regression witness only, per the "wire first,
tune later" rule below: nothing here judges a look, only whether a later change moved one.
The comparator reads that `baseline.json` back on every diff and names the baseline commit and
camera count in each map's header (`sm_hub_1: (baseline 8077e5b5f902, 4 cam)`), says so explicitly
when a baseline predates R2.1, and degrades to a per-map "manifest unreadable" line on a
truncated or mis-encoded manifest instead of aborting the run (`fd2087fd`, `98d3b942`).
`pipeline/tests/test_shots_diff.py` pins the `baseline.json` shape (the three keys, the sorted
camera list, an excluded failed capture), `git_commit`'s unknown-outside-a-checkout and real-HEAD
cases, a `--save` integration case that writes the file for real, and the three header branches
(named commit, pre-R2.1, corrupt manifest). Known gap: `compare()` — the pixel-diff core — still
has no direct unit coverage; the shape-mismatch and changed-percent branches are exercised only by
the real-data run.

**Per-map censuses landed on the test corpus (2026-08-31).** R2.2 (`validation/map_census.py`, no
C++ change): re-aggregates the entities, lighting and root export_v2 GLB units into one JSON
document per map — `entities.classes[]` (count, `brushCount`, `hullCount`, `outputCount` per
classname; `brushCount` is the class's entities whose `model` names a brush model, `hullCount` the
subset whose brush model also owns a `physics.models[]` entry in the root unit), `lights.types[]`
(count and `styledCount`, i.e. `style != 0`, per `dworldlight_t.type`), and zero-filled occurrence
counts for the R7.5 effects vocabulary (`env_sprite`, `env_steam`, `env_fire`, `env_embers`,
`env_lightglow`, `point_spotlight`, `env_sun`, `env_smoketrail`, `func_smokevolume`,
`env_dustmote`). Run on the three-map test corpus at `c9201d4fc5a55a21ff6a4239adc63be8b7516dc1` and
pinned under `$ELYSIUM_WORK_ROOT/exports_v2/_census/<map>.json`: `sp_tutorial_1` 1,868 entities/80
classes, 396 lights (224 point/14 styled, 170 spot/4 styled, 1 skylight, 1 skyambient — the sky
pair together, as `seam_map_map_lighting.md` states), 96 `env_sprite`, no other effects class;
`sm_pawnshop_1` 468 entities/57 classes, 161 lights (43 point, 118 spot, none styled), 74
`env_sprite`; `sm_hub_1` 2,597 entities/71 classes, 687 lights (20 emit_surface, 300 point/1
styled, 367 spot, no sky pair — matching the lighting seam's stated corpus fact), 309 `env_sprite`.
On all three maps every brush-model entity's `hullCount` equals its `brushCount` — vbsp compiles a
PHYSCOLLIDE entry for every brush model, not only the ones a class handler treats specially — so
the census's own numbers are the first evidence for R3.1's "world-space-hulls invariant" rather
than an assumption going in. `pipeline/tests/test_map_census.py` pins the three aggregation
functions against synthetic rows (brush/hull/output totals, type grouping with a styled light,
zero-filled effects classes) and one `write_glb`-built fixture through `census_for_map`/
`write_census` end to end, plus the named error on a unit that has not been exported.

**Level recipe closes over its runtime sidecars (2026-08-31).** R2.3/MP-1.3
(`pipeline/unreal/bake_map.py`, no C++ change, no rebake run). `level_sidecar_recipe` parsed only
`.props`/`.decals`/`.lights`/`.env`/`.sky`/`.spawn` into the level's recipe fingerprint; `.ents`,
`.hulls`, `.dispcol` and `.ropes` are read by `AElysiumMapActor` at map load, not by the bake, so a
touched one moved no field the tracker compared and the level kept whatever stamp it already
carried — a stale level that would read as a runtime bug. The recipe now carries a
`runtime_sidecars` block with a whole-file SHA-256 per sidecar (`ents_sha256`, `hulls_sha256`,
`dispcol_sha256`, `ropes_sha256`, each `None` when the file is absent — `sm_pawnshop_1` ships no
`.dispcol`, the real shape a synthetic-only test would have missed). Nothing is parsed into a
baked actor from these files; the digest exists only so the tracker's existing stamp-compare
dirties the level package. `_level_recipe` routes the digest through `Bake._file_sha256`
(`ContentDigestCache`), the same cache every other content hash in the map bake already uses, so a
repeat bake over unchanged sidecars pays no rehash. `pipeline/tests/test_level_sidecar_recipe.py`
(7 cases, loading the real module through the existing `unreal`-stub pattern) prove: all four
sidecars carry a digest; touching any one of the four changes the recipe and only that sidecar's
own digest key, leaving the other three digests and every parsed field (`props`, `decals`,
`lights`, `environment`, `sky`, `spawn`) unchanged; two calls over an untouched sidecar set produce
byte-identical recipes; and a missing sidecar digests to `None` rather than failing. `uv run
pytest pipeline/tests/test_level_sidecar_recipe.py pipeline/tests/test_contracts.py
pipeline/tests/test_bake_orchestration.py`: 87 passed. Two stale doc claims from before this
decision landed corrected in the same commit: `uasset-bake-spike.md`'s "outside every bake
fingerprint" and `runtime-data-compilation.md`'s "deliberately remain outside the Unreal bake
fingerprints" both predate the 2026-08-31 owner decision (`814905ee`) that put MP-1.3 on the maps
track; the runtime-side gap (G3) they otherwise describe — the running game has no receipt of its
own — is unchanged and still open.

**Travel's export gate accepts the new lane's marker (2026-08-31).** R2.4/MP-1.4
(`Source/ElysiumUE/Private/ElysiumContentPaths.h`, `.../Public/ElysiumMapSubsystem.h`,
`.../Private/Map/ElysiumMapSubsystem.cpp`, docs only besides). The ruling is in
`docs/architecture/map-architecture.md` "The export-readiness gate": a new
`FElysiumContentPaths::MapExportReady(Map)` names `<map>.ready`, an empty presence-only marker
beside a map's other sidecars under `MapDir`, that the R3.2 producer will write once every sidecar
`Travel` depends on is confirmed complete on disk for that map — nothing writes it yet. `Travel`'s
`.obj`-only refusal (`ElysiumMapSubsystem.cpp` ~L334) and the separate, previously-duplicated check
in `ExportedMaps()` both now route through one new predicate, `UElysiumMapSubsystem::
HasTravelableExport(Map)` (static, file-only, no `UWorld`), which accepts either `MapObj` or
`MapExportReady` — so the two call sites can never disagree on what Travel will accept, and the gate
is satisfied by whichever producer ran, never by both. Two new `Elysium.Substrate` tests
(`ElysiumMapExportGateTests.cpp`, `FElysiumScratchContentRoot`): `MapExportGate` drives all four
states over one map — neither artifact (refused), `.obj` alone (accepted), the marker alone with no
`.obj` present (accepted), both together (accepted, not required) —
`MapExportGateMarkerIsPresenceOnly` proves the marker's bytes are never read (a non-empty marker
still accepts) and that a same-named directory at the marker's path does not satisfy the
`FPaths::FileExists` check. `uv run elysium build`: `Result: Succeeded`. `uv run elysium test
Elysium.Substrate`: 423 of 423 test(s) executed in 5.9s, exit code 0 (up from 421 before this task's
two additions). Nothing emits `MapExportReady` yet; R3.2 is the first producer, and R5.1 retires the
`.obj` branch once the bake itself stops reading `.obj`.

**Roadmap R2 landed (2026-08-31).** Stage "R2 — instruments and guards" (MP-1) is done, four
tasks, each already has its own detailed Settled entry above; this rolls the stage up.

- **R2.1** (`dd5cc68b`, review fixes `fd2087fd`, `98d3b942`, `c9201d4f`) — `shots_diff.py --save`
  writes a `baseline.json` (build commit, map, sorted vantage list of only the vantages that `ok`'d)
  beside every promoted capture. 14/14 vantages captured real-RHI DX12/SM6 2560×1440 across the
  three-map test corpus (`sp_tutorial_1` 6/6, `sm_pawnshop_1` 4/4, `sm_hub_1` 4/4), saved at
  `8077e5b5f902`; a same-commit re-diff proved the comparator end to end (14 compared, 0 over
  tolerance). Review rounds added: the comparator names the baseline commit/camera count in each
  map's header, states explicitly when a baseline predates R2.1, and degrades to a per-map
  "manifest unreadable" line on a corrupt manifest instead of aborting. `test_shots_diff.py` pins
  the manifest shape, `git_commit`'s unknown-outside-a-checkout/real-HEAD cases, a real `--save`
  integration case and all three header branches. Known gap, still open: `compare()` itself has no
  direct unit coverage. Scoped to the three-map corpus; the original wider reach (every hub + one
  district type) re-promotes when the corpus widens.
- **R2.2** (`441f6a32`) — `validation/map_census.py` aggregates each map's root/entities/lighting
  export_v2 units into one pinned JSON: `entities.classes[]` (count/`brushCount`/`hullCount`/
  `outputCount`), `lights.types[]` (count/`styledCount`), and zero-filled counts for the ten R7.5
  effects classes. Run at `c9201d4f`: `sp_tutorial_1` 1,868 entities/80 classes, 396 lights (224
  point/14 styled, 170 spot/4 styled, sky pair), 96 `env_sprite`; `sm_pawnshop_1` 468 entities/57
  classes, 161 lights (43 point, 118 spot, 0 styled), 74 `env_sprite`; `sm_hub_1` 2,597 entities/71
  classes, 687 lights (20 emit_surface, 300 point/1 styled, 367 spot, no sky pair), 309
  `env_sprite`. On all three, every brush-model class's `hullCount` equals its `brushCount` — first
  evidence for R3.1's world-space-hulls invariant, though the reviewer's own recount calls it
  near-tautological (vbsp emits a PHYSCOLLIDE for every brush model) and flags it as an invariant
  witness rather than a class-handler discriminator. `test_map_census.py` pins the three
  aggregation functions plus one end-to-end `write_glb`-built fixture. Decisions: `hullCount` reads
  literally off `physics.models[]`, `styledCount` off `style != 0`; the tool is an internal library
  module (importable + `argparse main()`) like `shots_diff.py`, not wired into the public CLI;
  output is pinned under `$ELYSIUM_WORK_ROOT/exports_v2/_census/<map>.json`, local and gitignored.
  Follow-ups: R3.3/R7.5 to decide staleness detection or regen-on-demand for the pinned JSONs;
  the hullCount==brushCount hypothesis wants checking across all 108 maps, not just three. Review
  nits recorded, none blocking: classname matching is case-sensitive (an `Env_Sprite` misspelling
  would miss the effects vocabulary — matches the exporter's own census, so consistent rather than
  wrong); `sourceCommit` is computed off the `pipeline/` directory rather than the repo root
  (same answer, inconsistent spelling versus `shots_diff.py`); no test covers `main()` or a null
  classname (both untriggered on the three test maps); the roadmap's R2.1 entry is dated
  2026-09-01 while sitting above R2.2's 2026-08-31 — a pre-existing date typo worth a one-word fix
  next touch.
- **R2.3** (`c2ea28c8`) — `level_sidecar_recipe` (`pipeline/unreal/bake_map.py`) gained a
  `runtime_sidecars` block: a whole-file SHA-256 per `.ents`/`.hulls`/`.dispcol`/`.ropes` (`None`
  when absent, as `sm_pawnshop_1`'s missing `.dispcol` proves), so a change to a sidecar the bake
  never parses (read only by `AElysiumMapActor` at load) still dirties the level's fingerprint
  instead of leaving a stale package. Nothing is parsed from these files into a baked actor; the
  digest exists purely to feed the existing stamp compare, routed through `Bake._file_sha256`/
  `ContentDigestCache` so a repeat bake pays no rehash. 7 pytest cases (parametrized four ways)
  prove per-sidecar isolation, an untouched set reproducing the same recipe dict, and the
  missing-file→`None` case; `uv run pytest` across the three touched test files: 87 passed. Two
  stale doc claims ("outside every bake fingerprint") in `uasset-bake-spike.md` and
  `runtime-data-compilation.md` were corrected in the same commit; the separate runtime-side G3 gap
  (the running game still has no receipt of its own) stays open. Side effect worth flagging: this
  changes every map's level fingerprint, so the next scoped bake re-authors all three `.umap`s once
  rather than reusing — intended, not a defect. Review nit: the implementer's own task report was a
  placeholder (`'summary': 'test'` etc.); the commit itself was reviewed directly and is sound, but
  the reported numbers for this task should not be trusted, only the commit and the doc text it
  produced. Minor coverage gaps noted, not blocking: assertions are on the recipe dict rather than
  `bake_lib.recipe_fingerprint`'s serialized output, and nothing exercises the `digest=` injection
  path into `Bake._file_sha256`.
- **R2.4** (`7b1ab6d5`) — `docs/architecture/map-architecture.md` gained "The export-readiness
  gate": `FElysiumContentPaths::MapExportReady(Map)` names an empty, presence-only `<map>.ready`
  marker beside a map's other sidecars, which the R3.2 producer will write once its sidecars are
  complete — nothing emits it yet. `Travel`'s `.obj`-only refusal and the separately-duplicated
  check in `ExportedMaps()` now both route through one static predicate,
  `UElysiumMapSubsystem::HasTravelableExport(Map)`, accepting either `MapObj` or `MapExportReady`
  so the two call sites can never diverge. Two new `Elysium.Substrate` tests
  (`ElysiumMapExportGateTests.cpp`) cover both-accept, neither-refuse and presence-only semantics
  (a non-empty marker still accepts; a same-named directory does not). `uv run elysium build`
  succeeded; `Elysium.Substrate` 423/423 (up from 421). Decision: kept deliberately distinct from
  the existing "MapReady" runtime-activation vocabulary, called out in the doc. Follow-ups: R3.2 is
  the only intended writer, untested against a real producer until it lands; R5.1 retires the
  `.obj` branch once the bake stops reading `.obj`. Review nits, none blocking: the test name
  `MapExportGate` is a strict string prefix of `MapExportGateMarkerIsPresenceOnly` (both still run,
  since Unreal splits on `.`, but a filter of the short name can't isolate the first test alone);
  one scratch-root leaf string doesn't match its test's own name; the "Travel actually consults
  this predicate" coupling is asserted only by reading the source, not by a Policy-tier test; the
  per-map sidecar tables in `rebuild-strategy.md`/`uasset-bake-spike.md` don't list `.ready` yet —
  add the row when R3.2 starts writing it.

**The `.ents` join is stated, and reproduced from the V2 units (2026-08-31).** R3.1/MP-2.1
(`docs/architecture/seam_map_map.md` → "Producer join: the entities+root join behind `.ents`"; docs
plus one pytest module, no producer code). The section names, per `entities[].model` brush index,
the exact root-unit path behind each of the five joined facts — `hulls` through
`models[N].headNode` → `bsp.nodes[].children` → `bsp.leafs[].firstLeafBrush`/`numLeafBrushes` →
`bsp.leafBrushes.values[]` → `collision.brushes[]`/`brushSides[]` → `planes[]`; `contents` as the OR
over **hull-producing** brushes only; `blocks_player` as `contents & 0x1400B`; `brush_mesh` from
`models[N].firstFace`/`numFaces` → `faces[]` → `texinfos[]` → `textures[].asset` minus TOOLS
materials and minus the entities unit's `func_areaportalwindow` backing join; `sky` from the
model-0 point-leaf walk plus `bsp.leafs[].area` and the first `sky_camera`. `models[N].origin` is
explicitly *not* part of the join (zero on every brush model). The hull solver is stated verbatim
from `_model_brushes`/`_brush_hull` — bevel sides skipped, `<4` survivors ⇒ no hull, every plane
triple with `|det| ≥ 1e-6` solved, a point kept when `n·x − d ≤ 0.05` for *every* surviving plane,
dedupe on `round(·, 1)` Source units keeping first-appearance order and last-seen value, then
`× 2.54` with the Y flip and `round(·, 4)`.

Two numeric facts the port depends on, both measured. **Precision:** root planes are published in
glTF metres, and inverting that transform in binary64 leaves 4,772 of the three maps' 35,594 plane
distances not bit-equal to the BSP's float32 (max |Δ| 9.09e-13 Source inches) while **zero** fail in
binary32 — the solver must hold recovered planes as float32, and solving in float64 measurably
changes hull vertex sets (a first pass at float64 mis-matched 96 of 377 brush entities; float32
matched all 377). **One pre-declared divergence:** `_brush_hull` unpacks `dbrushside_t` as `"<hhhh"`,
so `planenum` is signed; `la_hub_1` alone carries 33,294 planes and 216 of its 63,096 brushsides
name a plane ≥ 32,768, which the legacy reader wraps to the tail of the plane array. A faithful port
reading the root unit's unsigned `brushSides[].plane` therefore diverges from the legacy exporter on
`la_hub_1` and nowhere else; the R3.3 differ must expect that by name — it is a fix, not a
regression.

**The hull frame, verified once on a real rotating door** (`sm_pawnshop_1` `havenrm`,
`func_door_rotating`, `model "*18"`, legacy `.ents` + V2 root unit, data check, no game run): the
solver applies no per-entity transform, so a hull rides in whatever frame vbsp compiled, and the
rule is unconditional — **world = entity `origin` + hull vertex**. The door's `origin` key
`-2008.5 -2559 199` → Unreal `[-5101.59, 6499.86, 505.46]`; its single 8-vertex hull spans
`[-3.81, -2.54, -139.7] … [3.81, 134.62, 139.7]` cm, exactly `models[18]`'s own bbox
(`(-1.5, -53, -55) … (1.5, 1, 55)` Source) with `models[18].origin` zero — so for this entity the
hull is *not* world-space, `origin` is simultaneously the hinge, and only `origin + hull` lands the
door in the map. The decisive evidence is instancing rather than magnitude: 8 of `sm_pawnshop_1`'s
10 `func_door_rotating` entities share three brush models (`*18` ×2, `*31` ×3, `*33` ×3) at eight
distinct origins. The converse is authored in the same corpus — `sp_tutorial_1`'s
`trigger_changelevel` family mixes both, `*74` re-centred on its origin and `*142` left thousands of
units from it — so the producer classifies nothing and applies the one rule.

**The whole join was then executed against the published V2 units alone** (root + entities, no BSP
read) and diffed against the legacy sidecars for the three-map corpus: 377 brush entities, 950
hulls, **7,542 hull vertices reproduced exactly**, 0 `contents` and 0 `blocks_player` mismatches,
the meshed-model sets equal (`sp_tutorial_1` 73, `sm_pawnshop_1` 28, `sm_hub_1` 55, with 5 and 8
`func_areaportalwindow` backings correctly suppressed), and all 261 `sky` rows equal. The section
also transcribes the full `.ents` field list the producer must reproduce — emission order, per-field
rounding (origin 5, hinge 6, hull 4, quat 6), last-wins unfolded keys, the exact-case `StartHidden`
and `floor1`…`floor8` lookups, the `*N` range guard, `npc_*` exclusion for `model_mesh`, the output
row's seven members and `separators=(",", ":")` — and points the six known field-level differences
(datamap output typing, key folding, `param` stripping, `delay` via `atof`, dropped `extra`, `times`
normalization) at R3.4 rather than deciding them here. `pipeline/tests/test_map_producer_join.py`
pins the solver (box corners with a bevel side ignored, dedupe under a duplicated plane, the
`<4`-sides abstain, the headnode-scoped tree walk) and the door invariant against the real corpus;
5 passed in 0.40 s. Follow-ups: R3.2 owes the field list, which is where the remaining risk now
lives — the join itself is proven; the float32 rule and the `la_hub_1` overflow both want asserting
in the R3.3 differ; the invariant was verified on three maps, not 108.

**The producer emits the legacy sidecars, byte for byte bar one (2026-08-31).** R3.2/MP-2.2
(`pipeline/src/elysium_pipeline/exporters/UE_map_sidecars.py`, one new pytest module, no game
change, no legacy file touched). The producer reads a map's root, entities and lighting units and
writes `.ents`, `.hulls`, `.dispcol`, `.lights`, `.env`, `.sky`, `.spawn`, `.ropes` plus the R2.4
`<map>.ready` marker into `$ELYSIUM_EXPORT_V2_ROOT/_sidecars/<map>/` — a separate tree from the
legacy export root, so nothing it writes can shadow a legacy sidecar. It takes the `UE_` prefix
because it emits Unreal-native centimetres (`pipeline/CLAUDE.md` → "Coordinate contract"); it is
run directly (`uv run python -m elysium_pipeline.exporters.UE_map_sidecars <map>…`), not wired into
`uv run elysium`, mirroring `map_census`. The visibility unit is listed in `unit_paths` for
completeness and read by nothing: no sidecar in this set needs a lump it owns. The nav-graph seam
is likewise untouched — the legacy exporter reads none of it.

**Result on the three-map corpus, against the sidecars `UE_bsp_to_scene.py` wrote:** 21 of the 23
files byte-identical — `.ents`, `.hulls`, `.lights`, `.env`, `.sky`, `.spawn`, `.ropes` on all
three maps; the two `.dispcol` (only `sp_tutorial_1` and `sm_hub_1` have one) are the exception —
**4,119,685 bytes identical**, including 4,933 `.ents` entity rows, 7,589 world brush hulls
(2,371 + 1,376 + 3,842,
with 190/12/69 3D-skybox brushes dropped), 1,244 worldlights, and 167 rope segments. Run time 8 s
for all three maps.

Three legacy facts the port had to reproduce that the R3.1 field list did not name, all found by
the byte diff and none of them a choice made here. **(1) Line endings.** Every legacy writer is a
bare `open(path, "w")`, so the shipped sidecars carry CRLF; forcing LF shortened
`sm_pawnshop_1.hulls` by exactly its 1,376 rows. **(2) The entity lump is read as text, and the
text is not what the entities unit publishes.** `sm_hub_1`'s block 1611 `logic_auto` authors
`setArea(\"santa_monica\")` inside an output value; the legacy pair regex `"([^"]*)"\s+"([^"]*)"`
stops at the escaped quote and re-pairs the rest of the block, so the shipped `.ents` loses that
entity's `origin` (it emits `[0,0,0]`) and gains a key spelled `),`. The producer therefore
reconstructs the lump text from the unit's ordered `keyValues[]` and runs the legacy regexes
verbatim, rather than reading the unit structurally and silently "fixing" the entity. **(3) The
escapes have to go back in**: the unit's lexer unescapes `\"`, so the reconstruction re-escapes and
checks every keyvalue against the unit's own `byteLength` — an escape rule that does not round-trip
raises instead of quietly producing a different lump. That check passed on all 4,933 entities.

**One measured divergence, and it is a seam limit rather than a port defect: `.dispcol`.** The
legacy exporter builds each displacement grid vertex in binary64 from the VERTEXES and DISP_VERTS
lumps. The root unit publishes neither numerically — a displacement face contributes no
world-scene vertices (`map_glb/decode.py`: "the displacement mesh states this face's geometry")
and `displacements[]` carries no `vector`/`dist` rows — so the only published form of that geometry
is the displacement mesh's float32 `POSITION` accessor. Row count and row order reproduce exactly
(3,584 rows on `sp_tutorial_1`, 288 on `sm_hub_1`), and the binary32 rule brings the values within
0.0019 cm, but that still moves the 4th decimal the sidecar prints: 228 of 3,584 and 100 of 288
rows come out byte-identical, 17,908 of 32,256 and 372 of 2,592 printed floats differ, max |Δ|
0.0019 cm and 0.0006 cm. This is the second pre-declared divergence after R3.1's `la_hub_1`
`planenum` overflow, and R3.3's differ must expect it by name. **Open decision for R3.4:** publish
DISP_VERTS (or the raw VERTEXES table) numerically in the map root unit so `.dispcol` becomes
byte-reproducible, or accept the 4th-decimal divergence as named. Nothing here decides that.

`pipeline/tests/test_map_sidecars.py` pins the four pure functions a whole-map differ could only
report as an unexplained delta — the hull solver's bevel skip and its `<4`-sides abstain, the
output splitter's four legacy field rules (`param` unstripped, `delay` via `float()`, `times` to
`-1`, `extra` dropped), the binary32 coordinate inversion (1.5 Source inches does not return
bit-exactly in binary64 and does in binary32; plus a plane row's normal/distance split), and the
entity-lump reconstruction of the embedded quote. 5 passed in 0.18 s; `test_map_producer_join.py`
and `test_contracts.py` re-run green beside them (68 passed in 2.25 s). Follow-ups: `.ready` now
has a writer, so R2.4's "untested against a real producer" is closed for these three maps only;
`.sprites`, `.props`, `.decals`, `.water`, `.cube`, the `.obj`/`.mtl`/`.blend` set and the
`brushes/` meshes are still legacy-only and are R5.1's, not this task's; the `.env` `skybox` flag
and the `.ropes` material files resolve against the shared corpus (`UE_extract_corpus`'s output,
not the map exporter's), which is what the legacy exporter did and what survives R3.5; the run
covered 3 maps of 108, and `la_hub_1` is expected to diverge by name.

**The differ lands: byte-equal or named-divergence-only on the three-map corpus (2026-09-01).**
R3.3/MP-2.3 (`pipeline/src/elysium_pipeline/validation/map_sidecar_diff.py`, one new pytest
module, no exporter change). Compares the R3.2 producer's sidecars under
`$ELYSIUM_EXPORT_V2_ROOT/_sidecars/<map>/` against the legacy exporter's under
`$ELYSIUM_EXPORT_ROOT/<map>/` for each of the eight sidecars (`.ready` is producer-only and never
compared): a byte comparison for every suffix, plus a structural diff for `.ents` — per-entity
field diff by lump-order index (added/removed/changed keys, the `outputs` list), and `hulls`
reported as per-hull vertex counts and one AABB per brush-entity row rather than the raw flat
arrays. `.dispcol` gets its own classifier, `classify_dispcol`, which only accepts a difference as
the pre-declared seam-precision limit when row count and row shape match exactly and every row's
max |Δ| stays under a 0.01 cm tolerance (measured max on the corpus is 0.0019 cm) — a shape
mismatch or a larger delta reports `"unexpected"` instead. `la_hub_1`'s pre-declared
`planenum`-overflow divergence (R3.1) short-circuits `diff_map` by name before any file is read, so
a future 108-map run never byte-compares a map the legacy exporter itself corrupted. A map's
`classification` rolls up to `"byte_equal"`, `"named_divergence_only"`, or
`"unexpected_divergence"` — only the first two are a pass.

Run on the scoped three-map corpus (`uv run python -m elysium_pipeline.validation.map_sidecar_diff
sp_tutorial_1 sm_pawnshop_1 sm_hub_1`, reports pinned under
`$ELYSIUM_WORK_ROOT/exports_v2/_sidecar_diff/<map>.json`): `sm_pawnshop_1` is `"byte_equal"` (no
`.dispcol` on either side); `sp_tutorial_1` and `sm_hub_1` are `"named_divergence_only"` — in both,
every sidecar but `.dispcol` is byte-identical (`.ents` 848,942 / 1,064,174 bytes, `.hulls` 601,377
/ 898,511 bytes, `.lights`, `.env`, `.sky`, `.spawn`, `.ropes` all equal) and `classify_dispcol`
proves the `.dispcol` divergence is the named one, not a fresh defect. The `.ents` structural diff
finds **zero** entity-level differences across all three maps' 1,868 + 468 + 2,597 = 4,933 entity
rows — the same byte-equality the R3.2 SHA comparison already showed, now demonstrated at the field
level rather than assumed from whole-file equality. `pipeline/tests/test_map_sidecar_diff.py` (5
cases) pins `file_diff`'s three presence states, `classify_dispcol`'s tolerance boundary and its
shape-mismatch rejection, `entity_field_diff`'s five diff kinds (added/removed/changed/outputs/
hull-count/AABB) against synthetic rows, `ents_structural_diff`'s index alignment including a
producer-only extra row, and `diff_map`'s three classifications plus the `la_hub_1` short-circuit,
all against `tmp_path` fixtures rather than the real corpus. `uv run pytest
pipeline/tests/test_map_sidecar_diff.py pipeline/tests/test_map_sidecars.py
pipeline/tests/test_map_producer_join.py pipeline/tests/test_map_census.py`: 20 passed in 0.55 s.

Scope: this run covers the three-map test corpus only, per this task's explicit instruction; the
108-map run R3.3's roadmap text names is a separately owner-approved step and was not run here.
`la_hub_1`'s short-circuit is therefore unexercised against a real file on disk in this run — it is
covered by a synthetic `tmp_path` test instead, since the map is outside the working corpus. The
`.dispcol` tolerance (0.01 cm) is a margin chosen above the two measured maxima (0.0019 cm,
0.0006 cm), not a re-derivation from a third data point; a future map with a larger seam-precision
gap would need the constant revisited, not assumed forever safe.

**The six named `.ents` divergences land as producer flags, decided or measured (2026-09-01).**
R3.4/MP-2.4, eight commits, `pipeline/src/elysium_pipeline/exporters/UE_map_sidecars.py` +
`pipeline/tests/test_map_sidecars.py` + `docs/architecture/seam_map_map.md` → "Producer join": new
"R3.4 — the six divergences" and "R3.4 — the two the port surfaced" subsections, plus one C++
change (`ElysiumEntityWorldPersistence.cpp` + a new Substrate test). Each of the six field-list
divergences `seam_map_map.md` named lands as a flag on a new `EntityDivergences` dataclass, every
flag defaulting to the legacy/byte-comparable reading so `write_sidecars`'s default output is
unchanged (re-verified against the three-map corpus: `sm_pawnshop_1` still `byte_equal`,
`sp_tutorial_1`/`sm_hub_1` still `named_divergence_only` with `.dispcol` the only file that
differs, exactly as R3.3 measured):

1. **Datamap output typing** (`datamap_output_typing`) swaps the `^(On|Out)` shape test for the
   class's datamap (the same tables `map_entities_glb.decode._is_output` reads). Measured delta on
   the three-map corpus: **zero** — both shipped examples (`game_ui`'s promotions,
   `trigger_player_activity_level`'s demotion) live on `la_hub_1`/`sm_diner_1`, outside it.
2. **Key folding** (`fold_keys`) makes two spellings of one key one `keys` slot instead of two,
   matching the entities unit's own folded-identity rule; the winning slot keeps the *last*
   occurrence's own spelling, never a forced lowercase. Measured delta: **zero** — no entity among
   the 4,933 repeats a key under two spellings.
3. **`param` stripping** (`strip_param`) strips the output row's `param` field the way the other
   string fields already are. Measured delta: **zero** — no authored `parameter` carries
   whitespace.
4. **`delay` via `atof`** (`delay_atof`) reads `delay` with the module's own longest-numeric-prefix
   `atof()` instead of a plain `float()`. Measured delta: **zero** — every authored `delay` is
   already `float()`-parseable.
5. **`extra` field** (`keep_extra`) adds field 6 verbatim, present only when authored. **Not**
   zero: retail writes seven fields on almost every output, so this flag measures `extra: ""` added
   to 1,027/1,028 outputs on `sp_tutorial_1`, 121/121 on `sm_pawnshop_1` and 800/802 on `sm_hub_1`,
   moving 338/34/228 `.ents` entity rows off `byte_equal` — the one flag of the six whose corpus
   delta is large rather than a synthetic-only edge case.
6. **`times` normalization** needed no flag: `ElysiumEntityDefs.cpp` already rewrites an authored
   `0` to `-1` once, at read time, with exactly one owner (not the exporter) — R3.4's only action
   was confirming that and recording it.

The two divergences the port surfaced (not from the field-list comparison) were decided rather
than coded: **`.dispcol` precision** stays a named divergence — the measured drift (max
0.0019 cm / 0.0006 cm) is already under `map_sidecar_diff`'s 0.01 cm tolerance and feeds collision
only, and publishing `DISP_VERTS` numerically would be a schema change to the map root unit (a
different seam) for precision nothing downstream needs. **`sm_hub_1`'s embedded-quote block** needs
no producer option: the corruption is an artifact of this producer's text-reconstruction-plus-regex
path, absent from the entities unit's own structural read, so R4.1's `UElysiumMapEntities`
inherits the fix by construction once it deserializes the entities unit directly.

The `OutputTimesRemaining` restore gate in `ElysiumEntityWorldPersistence.cpp` (datamap output
typing's runtime half) now logs a warning on a cardinality mismatch instead of silently keeping the
freshly-built counters, pinned by a new `Elysium.Substrate.SaveOutputCardinality` test. No
`FElysiumSaveVersion` bump: the roadmap's own 2026-09-01 "no save-file compatibility at build time"
ruling — recorded in this file the same day, ahead of this task — supersedes the bump this
section's history and `seam_map_map.md`'s prior text once called for; the docs were followed over
the older task note.

`uv run pytest pipeline/tests/test_map_sidecars.py pipeline/tests/test_map_producer_join.py`: 17
passed in 0.4 s (7 new test cases pin the flags, including the two synthetic edge cases the
three-map corpus does not exercise itself: a case-variant key repeat and a trailing-junk delay).
`uv run elysium test Substrate`: 424 of 424 passed in 3.8 s, including the new cardinality test.
`uv run elysium build`: succeeded. Each flag's re-run of the R3.3 differ used a scratch
`producer_root` (`diff_map(map, producer_root=scratch)`) rather than the default `_sidecars/`
tree, so the default corpus output was never overwritten mid-task; a final default-flags run
re-confirmed the unchanged classification. Follow-ups: the flags are `UE_map_sidecars.py`'s own,
not yet consumed by any caller — R4.1's `UElysiumMapEntities` deserializer is a separate module
that reads the entities GLB unit directly and was always going to need its own datamap-typing/
key-folding/atof logic rather than importing this producer's; these flags exist to prove and
measure the corrected reading ahead of that work, not to be imported by it. The 108-map run stays
out of scope, as it has for the whole R3 track.

**The producer becomes the default sidecar path, proven on a real scoped bake (2026-09-01).**
R3.5/MP-2.5, `pipeline/src/elysium_pipeline/exporters/export_all.py` (+7 lines, new
`rewrite_sidecars_via_producer`) and `pipeline/src/elysium_pipeline/exporters/UE_bsp_to_scene.py`
(one additive `return` at the end of `main`, nothing removed). `export_maps` now calls the R3.2
producer (`UE_map_sidecars.write_sidecars`) immediately after `UE_bsp_to_scene.main` on every
export, overwriting the eight legacy sidecars `main` just wrote into the same directory with the
producer's bytes; `.weather`/`.particles` are then re-run against the producer's `.ents` rather than
the legacy one `main` already used and discarded. Weather's mesh-derived `cover_triangles`/bounds
are geometry, not entity data, so `main` hands them back in its new return value instead of the
re-run recomputing them. `UE_bsp_to_scene.py` is not deleted or gutted — every sidecar-writing
function it owns is untouched, only unwired from this default path; it stays directly callable
(e.g. by a future 108-map differ run) — deletion is R8, once that wider run has built confidence.

Verified on a **real scoped bake**, not just the offline export: `uv run elysium export map
sp_tutorial_1 sm_pawnshop_1 sm_hub_1 --force` (full bake, not `--intermediate-only`) — 452+ assets
saved, 0 failed, all three `/ElysiumBaked/<map>/<map>` levels saved. `uv run elysium debug shots
sp_tutorial_1 sm_pawnshop_1 sm_hub_1` captured 6/6, 4/4, 4/4 vantages (14/14), matching R2.1's
counts exactly — all three maps still boot headlessly end to end.

**Review correction (2026-09-01):** the original report re-ran R3.3's differ against its default
roots after the bake and read the resulting `byte_equal` on all three maps as the `.dispcol`
divergence closing "by construction". That was a self-comparison, not a result: `rewrite_sidecars_
via_producer` overwrites `$ELYSIUM_EXPORT_ROOT/<map>/` — the differ's default `legacy_dir()` — with
the producer's own bytes (including the R2.4 `.ready` marker `UE_map_sidecars.write_sidecars`
writes), so the default-rooted differ compares the producer against itself and could never report
anything but `byte_equal` after this task's change lands. `map_sidecar_diff.py` now detects this
exactly (a `<map>.ready` marker inside the *legacy* directory) and returns a distinct
`self_comparison` classification instead of `byte_equal`, with a `--legacy-root` flag to point the
differ at a real legacy tree (built by calling `UE_bsp_to_scene.main` directly). No re-bake was
needed for this correction: `pipeline/tests/test_map_sidecar_diff.py` pins the new classification
on a synthetic tree. The R3.3 `named_divergence_only`-only-on-`.dispcol` result this task's own
Settled entry recorded (pre-R3.5, real legacy tree) is the last trustworthy comparison; a real
post-R3.5 comparison needs a fresh `--legacy-root` run, not yet done here.

The shot-diff against the R2.1 baseline (`8077e5b5f902`) is clean for two of three maps —
`sm_pawnshop_1` and `sm_hub_1` both 0.00% changed on every vantage, pixel-identical — but
`sp_tutorial_1` fails all 6 vantages (69–97% changed). This is **not** an R3.5 regression: a control
run with this task's two source changes fully reverted (`git stash`), rebaking `sp_tutorial_1` on
the 100%-legacy path and re-diffing, reproduced the same failure with the same magnitudes (spawn
97.27%, t1 87.91%, t1sky 85.09%, t2 80.02%, t3 78.18%, t4 71.14% — within noise of the R3.5-path
numbers). Visual inspection of both the R3.5-path and legacy-only captures against the baseline
shows identical scene composition (same geometry, same decal/prop placement); the divergence is the
baseline images themselves carrying a whole-frame vertical-streak blur artifact that neither a
fresh legacy bake nor a fresh producer bake reproduces, plus one close-up vantage (`t4`) with a
face-only diff consistent with idle-animation phase, not geometry. `sp_tutorial_1`'s bake had not
been fully rebaked since 2026-08-22 (`logs/20260822*-export-map.log`) until this task's runs — ten
days and R1.5 through R3.4 of independent work — which is the far more likely source than a same-
bytes sidecar swap this task's own differ run proves byte-identical. Filed as a stale/anomalous
`sp_tutorial_1` baseline rather than fixed here: re-saving a baseline is a call for the owner, not a
scoped task, and out of the "wire first, tune later" mandate for this task. Follow-up: re-`--save`
the `sp_tutorial_1` baseline once an owner has looked at the `_diff/sp_tutorial_1_*.png` artifacts
this run left under `$ELYSIUM_WORK_ROOT/exports/_shots/`.

`uv run pytest pipeline/tests/test_map_sidecar_default_path.py` (3 new cases, orchestration-level,
mocking the producer/`weather`/`particles` so no BSP or V2 units are needed) plus the touched
suites: `pipeline/tests/test_map_sidecars.py pipeline/tests/test_map_producer_join.py
pipeline/tests/test_export_orchestration.py pipeline/tests/test_area_portal_window_translation.py
pipeline/tests/test_item_models.py` — 152 passed. No C++ touched, so no `Elysium.Substrate`/`build`
re-run.

**Review correction (2026-09-01), incremental-cache visibility:** `rewrite_sidecars_via_producer`'s
`UE_map_sidecars`/`particles`/`weather` imports lived in that sibling function's own body, not
`export_maps`'s. `_map_tasks`'s incremental-build fingerprint is
`_DecoderClosures.function_entries('...export_all', 'export_maps')`, which walks only
`export_maps`'s own AST — so an edit to `UE_map_sidecars.py` (an `EntityDivergences` default, a
hull/rope fix) would not invalidate a map's cached export task without `--force`, silently falsifying
the "the BSP is the only file input" invariant `_map_tasks`'s own comment states. Fixed by hoisting
the three imports into `export_maps`'s body and handing the modules into
`rewrite_sidecars_via_producer` as keyword arguments; pinned by a new
`test_export_maps_closure_includes_the_sidecar_producer` asserting `UE_map_sidecars`/`particles`/
`weather` are in that closure.

**Roadmap R3 landed (2026-08-31).** Map producer parity, five tasks, `5047bef2` → `b6065f8f`, all
on the three-map test corpus (`sp_tutorial_1`, `sm_pawnshop_1`, `sm_hub_1`); this is a rollup of the
per-task Settled entries above.

R3.1 (`5047bef2`, MP-2.1, docs + one pytest module) states the entities+root join and the hull
solver verbatim in `seam_map_map.md` → "Producer join" and reproduces it against the V2 units
alone: 377 brush entities, 950 hulls, 7,542 hull vertices, 261 sky rows, 0 `contents`/
`blocks_player` mismatches. Decided: recovered planes must be held as binary32 (float64
mis-matched 96/377 brush entities' hulls; binary32 matched all 377); hulls ride in vbsp's compiled
model frame, `world = origin + hull` unconditionally, verified on both extremes (`havenrm`'s `*18`
door, `sp_tutorial_1`'s `trigger_changelevel` pair); `la_hub_1`'s signed-`planenum` int16 overflow
(216 of 63,096 brushsides, 33,294 planes) is a pre-declared, named divergence, not a future
regression. Follow-up: the field list is R3.2's; the 108-map run stays owner-approved and unrun.
Owner note: the "world-space hulls" heading in `seam_map_map.md` still contradicts the ruling its
own body states — cosmetic, unfixed.

R3.2 (`cecac0ab`, MP-2.2, new `UE_map_sidecars.py`) writes `.ents`/`.hulls`/`.dispcol`/`.lights`/
`.env`/`.sky`/`.spawn`/`.ropes` + `.ready` into `_sidecars/<map>/`; 21 of 23 files byte-identical to
legacy (4,119,685 bytes), only `.dispcol` differs (max |Δ| 0.0019/0.0006 cm — a seam-precision
limit, the root unit publishes no DISP_VERTS, only the coarser float32 mesh POSITION accessor, not
a port defect). Three undocumented legacy behaviours reproduced by the byte diff: CRLF line
endings, `sm_hub_1`'s embedded-quote `logic_auto` block reconstructed from `keyValues[]` and
re-escaped verbatim rather than silently "fixed", and per-keyvalue `byteLength` round-trip
validation (4,933/4,933 passed). Follow-up owed to R3.4: the `.dispcol` ruling. Owner notes: the
rope `atof` default fallback isn't fully verbatim (bites only a non-numeric-but-present value,
unreached on this corpus); `.ents` reconstruction is unverified against non-ASCII entity bytes
off-corpus; `.ready` landed under `_sidecars/`, not yet at `MapExportReady`'s path (converges at
R3.5).

R3.3 (`f89f3dca`, MP-2.3, new `map_sidecar_diff.py`) byte-diffs plus structurally diffs `.ents`,
adds `classify_dispcol` (0.01 cm tolerance) and a named `la_hub_1` short-circuit. Run on the
three-map corpus: `sm_pawnshop_1` byte-equal, `sp_tutorial_1`/`sm_hub_1` named-divergence-only
(`.dispcol` only), 0 structural diffs across 4,933 `.ents` rows. The 108-map run was explicitly not
run, per this task's scope. Owner note: the `la_hub_1` short-circuit is wider than the doc's
declared divergence — it silently absorbs any suffix, not just `.hulls`/`.ents`, worth narrowing
before a 108-map run.

R3.4 (`db3f1460`…`4c8a25f4`, 8 commits, MP-2.4) lands the six named `.ents` divergences as opt-in
`EntityDivergences` flags on `UE_map_sidecars.py`, defaulting off so the byte-comparable default
output is unchanged (re-confirmed against R3.3's classification). Measured per-flag entity-diff
counts on the three-map corpus: `datamap_output_typing`/`fold_keys`/`strip_param`/`delay_atof` all
0/0/0 (their trigger cases live outside the corpus, on `la_hub_1`/`sm_diner_1`); `keep_extra`
338/34/228 (every diff is the added `extra:""` field — retail writes 7 output fields almost
everywhere); `times` normalization needed no flag, already single-owned by `ElysiumEntityDefs.cpp`.
Decided rather than coded: `.dispcol` stays a named divergence (already under the differ's
tolerance; fixing it is a schema change to a different seam); `sm_hub_1`'s embedded-quote
corruption needs no flag (R4.1's direct GLB read inherits the fix by construction). The
`OutputTimesRemaining` silent-drop now warns instead (new Substrate test); no `FElysiumSaveVersion`
bump, per the roadmap's own 2026-09-01 no-save-compatibility ruling overriding the older task note.
Owner note: no pin exists yet for the `times` single-owner invariant (prose only); a stale
docstring in `split_output` still contradicts both the shipped code and this ruling.

R3.5 (`20aa7b9f`, `b6065f8f`, MP-2.5) wires the R3.2 producer into `export_maps` as the default
sidecar path, overwriting the legacy writer's own output in place; `.weather`/`.particles` re-run
against the producer's `.ents`. Verified on a real scoped bake: `--force` full bake, 452+ assets
saved / 0 failed on all three maps, 14/14 shot vantages captured. Two CONFIRMED review findings both
fixed the same day (`b6065f8f`): the incremental-build fingerprint wasn't seeing edits to
`UE_map_sidecars`/`particles`/`weather` (imports hoisted into `export_maps`'s own body, a closure
test added), and the differ's default legacy root pointed at the producer's own overwritten output,
so a default-rooted post-bake run could only ever report `byte_equal` against itself — fixed with a
new `self_comparison` classification and a `--legacy-root` flag; the earlier "`.dispcol` closes by
construction" claim was corrected in place as the self-comparison artifact it was.
`sp_tutorial_1`'s shot-diff failure against the R2.1 baseline (69–97%) was shown, by a full
revert-and-rebake control, to be a pre-existing stale baseline, not an R3.5 regression. Follow-up: a
real post-R3.5 parity comparison via `--legacy-root` has still not been run; owed before R8 deletes
`UE_bsp_to_scene.py`.

Across R3: the three-map corpus stayed the only one touched throughout (the 108-map differ run is
named five separate times as a still-pending, separately owner-approved step); no C++ change beyond
R3.4's warning log and its test; `seam_map_map.md` → "Producer join" is now the full spec (join,
hull solver, field list, the six R3.4 divergences, the two the port surfaced); `UE_bsp_to_scene.py`
is unwired but not deleted (R8's job). R4 (transport: assets instead of loose files) is next.

**R4.1 — the entity table ships as cooked content (2026-09-01).** Per-map `UElysiumMapEntities`
(`/ElysiumBaked/<map>/DA_<map>_Entities`), one reflected row per lump block, deserializing into the
unchanged plain `FElysiumEntityDef`; the contract is `seam_map_map_entities.md` → "## Import"
(new, +125 lines). The lane is the two-phase shape `import models` has: `uv run elysium import
map-entities --maps <stem>…` (refuses to run unscoped, no `--all`) stages
`$ELYSIUM_WORK_ROOT/import/map_entities/manifest.json` from the R3.2 producer's own entity join and
`pipeline/unreal/import_map_entities.py` authors the assets headless. `UE_map_sidecars.write_entities`
was split into `build_entities` (the rows) + a thin writer, and its preamble into `prepare_join`, so
the `.ents` file and the asset read **one** join and neither can drift; the three maps' `.ents`
bytes are unchanged by the split (848,942 / 206,930 / 1,064,174 — byte-identical to the shipped
files).

Numbers: staged 4,933 rows over the three test maps in **4.2 s**, parity `mismatchCount: 0` on all
three; authored in **15.6 s** wall (1.48 / 0.09 / 0.39 s per asset), re-run **0 imported / 4
reused**. Asset sizes 2.23 MB (`sp_tutorial_1`, 1,868 rows) and 2.87 MB (`sm_hub_1`, 2,597) against
`.ents` of 0.85 MB / 1.06 MB. `Elysium.Content.MapEntities.FieldParity`: **5,700 entity defs
compared field for field over four maps, 0 differing**; `DefCountParity` also asserts the resolver
answers `asset`, so the cutover is proven to fire rather than assumed.

**The parity test earned its keep on its first run.** A reflected `FQuat` property does not survive
a package save at its authored width — `0.707107` reads back `0.7071070075035095`, the nearest
binary32 — while `FVector` (three doubles) round-trips exactly. 226 of the 4,933 entities differed
on `model_quat` alone; the row now stores four `double` components and composes the quat in
`ModelRotation()`. Found by the test, not by inspection, and recorded in the seam doc.

Cutover: `ElysiumEntityDefSource::Load` is the one entry point (asset first, `.ents` second, which
source answered logged and returned). **The asset's presence is R4.1's flag** — no per-map switch
was introduced, because the mount already answers the question and R4.6 owns the explicit flag. The
`.ents` reader stays: it is the fallback for the 100+ unconverted maps, and R4.6's proof needs both
paths alive to diff. Consumers moved: `ElysiumMapActorLifecycle.cpp`'s map load and the green
room's `sp_theatre` camera-track read (the one direct `Parse` call outside map load) — `sp_theatre`
therefore got its asset too (767 rows, entities only: no export, no bake, no geometry), a
deliberate fourth map named here because the green room cannot otherwise be shown to be on the
asset. Tests: 7 pytest (`test_map_entity_asset.py`), 2 Substrate
(`Elysium.Substrate.MapEntities.DeserializeRows`/`SkyTransform`), 2 Content; Substrate 426/426,
34 pytest over the five producer/importer modules, `doctor` clean.

**R4.2 — collision ships as cooked content (2026-09-01).** Per-map `UElysiumMapCollisionPayload`
(`/ElysiumBaked/<map>/DA_<map>_Collision`), a `UDataAsset` that implements
`IInterface_CollisionDataProvider` and carries three authored `UBodySetup`s: the world's convex
brush set (`<map>.hulls`), the displacement trimesh (`<map>.dispcol`, cooked from the asset's own
triangle soup, which is what the interface exists for) and one convex body per brush entity, keyed
by lump ordinal. The contract is `seam_map_map.md` → "## Import" (new, +170 lines), written before
any code: identity, the three payloads, the body-setup flag table, the frames, the one transform,
both parity assertions and the cutover.

The lane is the two-phase shape `import models` and `import map-entities` have: `uv run elysium
import map-collision --maps <stem>…` (refuses to run unscoped, no `--all`) stages
`$ELYSIUM_WORK_ROOT/import/map_collision/manifest.json` from the sidecars themselves — **not** from
a second port of `brush_hull`, which would be a second set of tolerances — and
`pipeline/unreal/import_map_collision.py` authors and cooks the assets headless.

Numbers, three maps: staged 7,589 world hulls / 58,508 hull vertices / 3,872 displacement
triangles / 377 brush bodies into a 2.29 MB manifest; authored and cooked in **4.4 s** total (3.55
/ 0.22 / 0.61 s per asset), assets 3.09 / 1.53 / 2.98 MB. `Elysium.Content.MapCollision.WorldParity`
compares convex count and every hull's vertices against `<map>.hulls` and the triangle count against
`<map>.dispcol`; `…BrushParity` compares **377 brush bodies convex for convex against the defs
`ElysiumEntityDefSource::Load` produces, 0 differing**; `…PhysicsMeshes` asserts every setup creates
its Chaos structures. Headless boot of all three maps (`-nullrhi -testexit="Activating after"`):
the payload is adopted, the collision-ready barrier is satisfied, activation completes.

**The one transform, and why it is not optional.** `UElysiumMapEntities::Deserialize` scales a
`sky` brush entity's hulls by the map's `.sky` scale, so the def the runtime holds is not what the
`.ents` document stores — and a cooked convex cannot be rescaled afterwards. The stage therefore
applies that scale when it authors a `sky` body (4 entities of 377 on this corpus, all at scale
16), and `BrushParity` compares against the *deserialized* def, which is the only place the two
rules could disagree.

**The load-time number, stated honestly.** A per-transport A/B (payload assets moved aside and
restored) gives `Build` at 143.1 / 93.2 / 185.9 ms from the payload against 21.1 / 7.6 / 20.5 ms
from the sidecars — but those measure different work. The sidecar path's `Build` only parses and
*schedules*: `bUseAsyncCooking` spends the cook on worker threads after `Build` returns. The
payload path loads a multi-megabyte package and creates every Chaos structure synchronously, and in
an editor build each is a DDC lookup where a cooked build reads the package's own buffers. Both
report the barrier `Ready` by the time construction completes and total activation is within noise
(1.3–3.6 s, dominated by navigation/audio/animation preload). What R4.2 lands is "the cook happens
once, offline", not "the map loads faster"; an asynchronous adopt is a tuning question left open.

Cutover: `UElysiumMapCollision::Build` tries the payload and falls back to `LoadHulls`/`LoadDispCol`,
reporting `EElysiumCollisionSource`; `FElysiumEntityWorld::BuildBrushBody` adopts the payload's body
for its lump ordinal and cooks from `Def.Hulls` otherwise (a runtime-created entity always cooks,
correctly — it has no authored collision). **The asset's presence is the flag**, as in R4.1. Both
barriers keep their sources: the readiness barrier still polls the components' own body setups, and
the nav bounds are still the union of the live components' bounds — the displacement component
gained explicit local bounds (`UElysiumCollisionOnlyMeshComponent`, extracted from the hull
component so both share one rule) because on the payload path it has no render section to bound it
and an unbounded component is invisible to the navigation octree. The roadmap line said the
`.hulls`/`.dispcol` readers would be **deleted** here; they are **kept** — they are the fallback for
every unconverted map and R8.1 owns reader deletion, which is where the line now points. Tests: 6
pytest (`test_map_collision_asset.py`), 3 Content (`Elysium.Content.MapCollision.*`); Substrate
426/426, Policy 9/9, `Elysium.Content.Map*` 6/6.

**R4.3 — light tuning is an editor surface (2026-09-01).** Global calibration moved off
`UElysiumLightRig`'s own hardcoded field defaults and three console variables
(`elysium.LightScale`/`LightFit`/`LightCurve`) onto `UElysiumLightingSettings : UDeveloperSettings`
(Project Settings → Elysium → Lighting, tracked `Config/DefaultElysium.ini`); per-light hand-tunes
moved off the Lights Cog window's `_lights/<map>.json` survey onto a per-map
`UElysiumLightCalibration : UDataAsset` (`/ElysiumBaked/<map>/DA_<map>_LightCalibration`) with
merge rows keyed by the `.lights` line (`SourceIndex`) — each override its own on/off switch plus a
value, so a hand pass moves one attribute without restating the rest of the light. The contract is
`seam_map_map_lighting.md` → "## Import" (new, +90 lines), written before any code: the settings
field table (mapping every retired literal/cvar to its new home), the row schema, the join and
cutover, and the Cog window's new scope.

Every value shipped is today's faithful default, unchanged — no look-tuning, per "Wire first, tune
later". **No remapper**: no `_lights/*.json` survey existed on disk anywhere in the corpus at the
time this task landed (confirmed empty), so there was nothing to migrate; the calibration asset
ships with zero rows for every map and the owner re-tunes fresh in the editor. Two new rig methods
carry the join: `ApplySettings` copies the settings object's fields into the rig's own mirrors
(kept separate, not a pointer, so a per-instance PIE edit still works) and `ApplyCalibrationAsset`
applies a calibration asset's rows through the existing per-source setters plus two new ones,
`SetSourceReach`/`SetSourceColor` (matching `SetSourceIntensity`'s shape). `Adopt` calls both, in
order (settings first, then a quiet `LoadObject<UElysiumLightCalibration>` at
`FElysiumContentPaths::BakedMapLightCalibration`, `LOAD_NoWarn | LOAD_Quiet`) — the asset's presence
is the cutover flag, as in R4.1/R4.2. `UElysiumLightingSettings::PushToWorlds` pushes on the
terminal `ValueSet` only (unlike `UElysiumSurfaceSettings`, which follows an interactive drag live):
a light rig carries real per-light state (shadows, MegaLights, source shape) that 60 ticks of one
slider drag should not pay to re-derive.

The Cog Lights window is now **read-only**: deleted the "Rig tuning" tab (every calibration
slider), the "Sky & fog" tab (sky light/height fog/skylight-leaking, which have no live tuning
surface until R4.4's environment asset — a known, accepted gap; their *derived* values still apply
at load), the per-light editor and its gizmo, batch enable/disable, and the JSON survey's
Save/Reload. Kept — the viewing tab the roadmap line names: visibility toggle, per-source list,
world-marker click-to-select, Isolate (display-only), and a read-only per-light readout including
the authored-batch identification `docs/vtmb/light-attribution.md`'s hand survey used (now count
text, not on/off buttons). That doc gained a correction note pointing hand-survey work at the new
calibration asset.

`Elysium.Substrate.LightRig`'s JSON-survey assertions were deleted with `LoadSurvey` and replaced
with coverage of `ApplySettings`/`ApplyCalibrationAsset` (synthetic, `NewObject`-built settings and
calibration objects — no baked asset, no scratch content root needed, since the test calls the rig
directly rather than reading through `FElysiumContentPaths`). The existing derivation-math
assertions (non-inverse-square falloff, MegaLights, shadows-from-calibration, spot cone) are **not**
re-homed yet: the roadmap line's "re-homed to bake verification" is where they belong once R5.6
bakes final light values and gives them something to be verified against, which does not exist
before that task lands — re-homing now would delete `ApplyToSource` coverage with nothing to
replace it (done at R5.6: `bake_verify.verify_lights_baked`, see that Settled entry). Substrate 426/426 tests (`Elysium.Substrate.LightRig` now covers two more behaviors
alongside its existing derivation assertions); Content `Elysium.Content.MapEntities.*` 2/2 and
`Elysium.Content.MapCollision.*` 3/3, re-run as a regression check even though R4.3 touches neither
lane directly.

**Incidental fix, same commit.** `Elysium.Content.MapEntities.*`'s helper functions
(`ManifestPath`/`StagedMaps`, anonymous-namespace) were byte-identical duplicates of
`Elysium.Content.MapCollision.*`'s own — dormant until this task's edits shifted the adaptive-unity
build's file bucketing and put both files in the same translation unit for the first time, where an
anonymous namespace is TU-wide and the duplicate definitions became a hard redefinition error.
Renamed the entity-side pair to `EntityManifestPath`/`StagedEntityMaps`; no behavior change.

**Roadmap R4 landed (2026-08-31).** All six R4 tasks are in on `main`; R4.1–R4.3 have their own
Settled entries above (commits `807ccefe`/`1b187f57`/`de6c0a94`), so this entry covers the stage as
a whole and gives R4.4–R4.6 the detail they never got their own entry for.

R4.4 (`71d6b455`) put the map's environment on cooked content: per-map `UElysiumMapEnvironment`
(`<map>.env`'s 2D-sky flag and two fog sets, `<map>.sky`'s 3D-skybox miniature transform,
`<map>.spawn`'s initial placement), the `import map-environment` lane and
`ElysiumMapEnvironmentSource::Load`; contract in `seam_map_map.md` → "## Import — environment",
numbers in that section's "Measured" table. Fog values are the unchanged, taste-free
`ApplySceneFog` inputs; the height-fog actor's own component properties stayed the direct-edit
tuning surface R4.3 left them as. The `.env`/`.sky`/`.spawn` readers were kept, not deleted (R4.6's
proof needs both paths alive to diff); reader deletion moved to R8.1, matching R4.1/R4.2.

R4.5 (`31a1222e`) gave the seven orphan cvar-only knobs editor homes: five `Config = Elysium,
DefaultConfig` `UDeveloperSettings` pages (`UElysiumUISettings`, `UElysiumChoreoSettings`,
`UElysiumAudioSettings`, `UElysiumSessionSettings`, plus the Cog "Enhanced defaults" literals into a
new `UElysiumWeatherSettings`), every default the retired cvar's own unchanged default, and
green-room eye tuning onto a real authored asset (`/Game/ElysiumAuthored/Eyes/DA_EyeTuning`,
`UElysiumEyeTuningConfig`) composed component-wise with the Green Room's live nudge via
`ElysiumEyes::ComposeTuning`, both landing as a no-op at shipped zero defaults. Three dead
mechanisms were deleted outright rather than homed (`elysium.RainMist`, `elysium.SkyProbe` +
`_skyprobe`, the skylight-leak A/B cvars and `ApplyPostProcessKnobs`), plus the `MaterialOverrides`
cvar group and `ApplyMaterialOverrides` — the last one honestly flagged as superseded in *purpose*
by `UElysiumSurfaceSettings` but not in *wiring* (that settings object only reaches the post-R5.4
V2 masters), with the shipped render proved identical anyway since every retired default sat
neutral. → lands: zero taste values living in C++ literals or cvars.

R4.6 (`3ea1c790`, fixed by `f3d2c871`) replaced the implicit "asset wins when present" rule
R4.1/R4.2/R4.4 each grew independently with one tracked, reviewable flag:
`UElysiumMapTransportSettings` (`Config = Elysium, DefaultConfig`, `MapsOnNewTransport:
TArray<FName>`), checked by all three whole-swap resolvers via
`ElysiumMapTransport::IsMapOnNewTransport(MapName)` before their own `LoadObject`. Contract in
`seam_map_map.md` → "## Import" → "The explicit per-map cutover flag (R4.6)"; R4.3's light
calibration stays deliberately ungated (additive, no legacy fallback). The first landing's
`Config/DefaultElysium.ini` listed only the three corpus maps and missed `sp_theatre` — a fourth
already-converted map (asset exists, read live by the green room's theatre camera track) — which
made `Elysium.Content.MapEntities.DefCountParity` go red (5/7 pass) and, more importantly, made
`ElysiumEntityDefSource::Load` silently fall back to the sidecar for a map the running game reads
live: a real transport regression, not just a test one, caught by review and closed same-day by
`f3d2c871`, which added `sp_theatre` to the ini (no bake) and rewrote `DefCountParity` to assert the
resolver's source against `IsMapOnNewTransport` per map instead of hardcoding `"asset"`, so it stays
correct as more maps get baked ahead of their ini line. `Elysium.Content.Map` tier back to 7/7;
`Elysium.Substrate.MapTransport` 2/2 throughout. All three corpus maps were headlessly booted and
shot-diffed against the R2.1 baseline (`8077e5b5f902`): all 14 vantages fail default tolerance, but
a `MapsOnNewTransport`-emptied control run reproduces the same magnitudes, proving the divergence is
accumulated R4.1–R4.5 drift, not R4.6's flag — full numbers in `seam_map_map.md` → "## Import" →
"Shot-diff against the R2.1 baseline (2026-09-01)". Re-saving the baseline is an owner call, open
the same way R3.5 already filed it for `sp_tutorial_1` alone.

Open follow-ups carried out of the stage: `Elysium.Content.MapEnvironmentParityTests.cpp` still
hardcodes its resolver assertion to `"asset"` rather than checking `IsMapOnNewTransport` — the same
failure mode `f3d2c871` just fixed for entities, latent until an environment asset ships ahead of
its ini line (not a defect today: all three env assets on disk match the three listed maps).
`Elysium.Content.MapCollision.*` has no equivalent resolver assertion at all. No Substrate test
asserts the tracked `Config/DefaultElysium.ini` actually loads into
`GetDefault<UElysiumMapTransportSettings>()`; only the Content tier observes that today. R8.1 still
owns deleting the `.ents`/`.hulls`/`.dispcol`/`.env`/`.sky`/`.spawn` sidecar readers R4.1/R4.2/R4.4
each kept as fallback. The R2.1 shot baseline re-save is an open owner call. A `--legacy-root`
parity comparison remains owed from earlier stages and untouched here.

**R5.1 — a map's geometry and props come off the root unit (2026-09-01).** The first task that
authors a map from the corpus instead of from the legacy exporter's intermediates. New offline
reader `elysium_pipeline.importers.map_geometry` + new editor lane `pipeline/unreal/bake_map_v2.py`
(a `bake_map.Bake` subclass that replaces two inputs and one stage and nothing else); contract in
`seam_map_map.md` → "## Import — geometry and placements" (+150 lines). Selected per map by a second
tracked list on the R4.6 settings page, `MapsOnV2Models`; every other map's bake is untouched.

**Parity is exact, and it is the real verification.** The reader reproduces the legacy
`.obj`/`_sky.obj`/`brushes/`/`.props`/`.blend` output on all three working maps — **identical**
vertex counts, group counts, per-group triangle counts, brush-model sets, and every placement's
stem, position, rotation, `solid`, `skin` and 3D-skybox flag: `sp_tutorial_1` 38,971 verts / 24,799
tris / 276 groups + 5,997 / 3,573 sky + 73 brush models + 809 placements; `sm_pawnshop_1` 15,507 /
9,177 / 132 + 930 / 488 + 28 + 194; `sm_hub_1` 41,901 / 24,434 / 278 + 1,037 / 539 + 55 + 1,043.
The only residual is the unit's binary32 `POSITION` against the legacy OBJ's four printed decimals:
max |Δ| 0.0008 cm and 7.2e-5 UV, the same seam-precision limit R3.2 measured on `.dispcol`. The
world/sky/brush face split, the `tools/` and areaportal-backing drops and the sky-area test are the
R3.2 producer's own (`prepare_join`), imported rather than re-derived, so the map bake and the
`.hulls`/`.ents` sidecars cannot diverge.

**Two things the parity run caught that no triangle count would have.** (1) The unit publishes a
`DISP_VERT` alpha as the lump's own 0..255 byte where the bake's blend channel is 0..1; unnormalized
it would have tinted every sculpted surface hard onto tex2 with every count still matching (the
pytest case now pins the `.blend` sidecar to 1e-4). (2) 43 placements over the three maps stand on
authored skeletal rest poses through `npc_index` rather than on a static twin; the first draft of
`_place_props` dropped that path, which the legacy lane had.

**The staged pair, and why the lane is split.** Reading the unit needs `numpy` (sky-area BSP walk,
accessor decode) and Unreal's embedded CPython does not carry it — the first bake attempt died on
exactly that. So the read runs offline from `unreal.bake_maps` right before the commandlet launches
and lands `manifest.json` + a packed little-endian vertex file under
`$ELYSIUM_WORK_ROOT/import/map_geometry/<map>/` (1.70 / 0.55 / 1.50 MB, ~1.5 s per map); the editor
half reads it with `json`/`array` alone. Same shape as the model, material and texture lanes.

**Bake, on the three maps:** 508 assets saved, 0 failed, all three `/ElysiumBaked/<map>/<map>` levels
saved — `sp_tutorial_1` 110 world chunks + 73 brush + 2 sky meshes, 809 prop actors (30 miniature,
611 solid, 30 skinned, 367 distance-faded, 4 on rest poses); `sm_pawnshop_1` 38 + 28 + 4, 194 props
(21, 121, 0, 80, 0); `sm_hub_1` 194 + 55 + 4, 1,043 props (49, 666, 11, 424, 39). One pre-existing
warning survives, not a regression: `sp_tutorial_1`'s single sky chunk has 86 material sections and
Nanite caps at 64, so it renders without Nanite — the legacy `_sky.obj` had the same 86 groups in
the same cell.

**Rulings written into `seam_map_map.md` before the code.** The model lane left "a VPHYSICS
placement of a model that ships no `.phy`: inert (faithful) or boxed (playable)" to the placement
lane; this lane takes **boxed**, and the rule collapses to `solid != 0` blocks / `solid == 0` does
not, on whatever simple collision the model asset carries. Reasons in the doc, in order: the
`CPhysicsProp::CreateVPhysics` warning path that ruling cites is the *entity* path and a GAME_LUMP
prop never simulates; a placement authored `solid 6` is authored to block and the missing `.phy` is
a gap in the model, not a statement about the placement; and the legacy lane already blocked on them
(complex-as-simple), so standing them inert would be a gameplay regression introduced by a transport
change. Fade is `flags & 0x1` **and** `fadeMaxDist > 0` → `LDMaxDrawDistance = fadeMaxDist × 2.54`
(442 of `sp_tutorial_1`'s 809 records carry `(0, 0)`, and culling those at zero would empty the map).
A miniature placement is never solid whatever its byte says. This closes the "Where do the props
go?" open question on the fold-into-the-level answer.

**The flip, and why it is a second list.** `FElysiumContentPaths::BakedMeshes()` is the V2 root and
`BakedMeshesFor(Map)` chooses between it and `BakedSharedMeshes()`; `BakedPropMesh`, `BakedItemMesh`
and `BakedPropSkins` compose from it and each now **requires** a map argument, so no call site can
silently land on the legacy root for a converted map. It is deliberately **not**
`MapsOnNewTransport`: `sp_theatre` is on that list (its entity assets exist) but its models have not
been imported, so reusing it would point its props at an empty root. `Travel`'s gate follows the
same flag — the R2.4 `.ready` marker always accepts, the legacy `.obj` accepts only off the V2 lane,
because once a map is cut over nothing reads its `.obj` and a stale one must not vouch for the
sidecars beside it. The `.obj` branch is not retired outright: 105 maps are still on the legacy lane
and only three carry a `.ready` marker today, so R8.1 keeps that deletion, as R2.4 said it would.

**The flip found a real hole in R1's map-scoped selection, and it is closed.** An `item_*` entity
carries no `model` keyvalue — the runtime folds a stem out of `vdata/items` and resolves it through
`items/ground_models.json` — so the map units' `dependencies[]` never name item ground models and
R1's `--maps` run had not staged them. The moment the runtime's model root flipped, **18 stems over
41 placements** across the three maps drew nothing (`prop '<stem>': no baked mesh`, 21/14/6 per map,
0 in every pre-R5.1 run). `select_for_maps` now also stages the whole item ground corpus (124
models; the whole table, because the player can drop any carried item on any map, so the question
has no map-scoped answer), documented in `seam_map_model.md` → "Scope and selection". Re-staged
537 / 536 (one loud skip: `weapons/w_null` declares body parts and publishes no VTX topology),
imported **122 new, 414 reused, 0 failed, 0 pruned in 19.0 s**, corpus 414 → 536 meshes. Re-run:
**0 unresolved on all three maps.**

**Headless boot: all three maps boot and capture, 6/6 + 4/4 + 4/4 = 14/14 vantages.** The pixel
comparison, however, could not attribute anything, and the measurement says why: **two consecutive
captures of the identical build and the identical baked levels differ by up to 94.05% of pixels**
(`sp_tutorial_1` spawn, mean 27.4, p99 169) — `sm_pawnshop_1` spawn 85.88%, `sm_hub_1` spawn
51.93%, six of the fourteen vantages over 40%. The V2-vs-legacy numbers are the same order on the
same vantages (worst 95.09%), so at this vantage set the shot harness is a **did-it-boot /
did-it-appear witness only**, not a pixel-regression witness; its own noise floor exceeds the
signal. The V2-vs-legacy heat maps show whole-frame edge shimmer with identical scene composition —
nothing missing, nothing moved. This also re-reads R4.6's "a control run reproduces the same
magnitudes": the shared cause is the harness, not accumulated drift. Filed as a follow-up, not
fixed here (it is an instrument problem and out of a cutover task's scope).

Tests: 5 pytest cases (`pipeline/tests/test_map_geometry.py`, 9 with parametrization) — the frame
algebra against `source_to_unreal`/`source_quat_to_unreal`, the placement mapping (`solid` 0..6,
fade needing both the flag and a distance), the scene split and `.blend` against the legacy exporter
on the real corpus, every `.props` row field for field, and that the two cutover lists are distinct;
3 C++ (`Elysium.Substrate.ModelCorpusRoot.FlagIsItsOwnList`, `.PathsFollowTheFlag` — the first
assertion anywhere that the tracked ini reaches a path accessor — and
`Elysium.Substrate.MapExportGateObjOnlyCountsOffTheV2Lane`). `uv run elysium build`: Succeeded.
`uv run elysium test Elysium.Substrate`: **436 of 436 in 4.0 s**. `uv run pytest` over the five
touched modules: **56 passed**.

Side finding, fixed in passing: `importers.models._read_model_settings` has been silently returning
its defaults since R4.6, because `configparser` rejects Unreal's `+Key=` array syntax as a duplicate
option and the read is wrapped in `except configparser.Error`. `strict=False` now, and the new
`elysium_pipeline.map_transport` (the Python twin of `ElysiumMapTransport`) reads the ini line by
line, which is what that format actually is.

Follow-ups: the shot harness's run-to-run non-determinism above, which blocks pixel regression for
every later task in R5; detail props (`dprp`) are R7.3's and this lane skips them by name; the sky
*dome* is R5.2's (this lane authors only the miniature's own geometry); the V2 lane still reads
`<map>.mtl`, `.env`, `.decals`, `.weather` and `.lights` for everything that is not geometry, which
is R5.3/R5.4/R5.6's; `--all` has still never run, so the V2 corpus stays a 536-model subset and the
flag list stays three maps.

**R5.2 — sky baked (2026-09-01).** Finishes the SkyLight actor `_place_sky` had always placed
half-empty (null cubemap, the raw `emit_skyambient` magnitude as a placeholder intensity, "handed
its real cubemap at load" by its own comment) instead of replacing it: the runtime's own
`ElysiumEnvironment::BuildSkyCubeFrom` — the K1 x K2 face-rotation table and the solid-angle-weighted
upper-hemisphere mean, unchanged — now runs once at bake, aimed at a persistent package through a
one-function editor library, `UElysiumSkyBakeLibrary::BakeSkyCubeAsset`
(`Source/ElysiumUE/Public/ElysiumSkyBakeLibrary.h`), instead of a transient one at every load.
Contract in `seam_map_map_lighting.md` -> "## Import" -> "Sky baked (R5.2)".

**One cube per sky NAME, not per map** — `/ElysiumBaked/Sky/Textures/TC_Sky_<name>` — because the
game shares six skies between 108 maps; `/ElysiumBaked/Sky/Meshes/SM_SkyDome` is one shared box
(`ElysiumMapVisuals.cpp`'s own `BuildSkyBox`, reproduced vertex-for-vertex in Python via
`bake_lib.build_dynamic_mesh`/`create_static_mesh`, no `unreal` import needed to state or test the
geometry itself); `/ElysiumBaked/Sky/Materials/MI_Sky_<name>` binds each sky's cube at `Brightness`
1 (the faithful default). The faithful (non-`tex_hi`) face set only — a bake is asked once, so it
picks VtMB's own data over the opt-in enhanced substitution, which stops applying to a converted
map's sky specifically. **Cutover rides `MapsOnV2Models`, not a new list** — R5.1's own entry had
already scoped the dome there, and the cube/SkyLight values mean nothing without the geometry that
displays them. `ElysiumMapVisuals::ApplyEnvironment` gained a `MapName` parameter for exactly this
gate: on a `MapsOnV2Models` map it runs `ApplySceneFog` and returns, never touching `SkyLight`,
`SkyDomeMesh` or `SkyMid` — the baked actor (adopted as before, off `elysium.skylight`) and the new
baked backdrop (adopted off the new `elysium.skydome` tag into `BakedSkyDomeActor`, wired into
`elysium.togglesky`) already carry the real values. Every other map's runtime path is byte-identical
to before. Deleting that path outright stays R8's, matching every other legacy-path retirement in
this roadmap — this task **bypasses**, it does not delete, exactly as the task notes said it should.

**Bake, on the three maps (2026-09-01, `uv run elysium export map sp_tutorial_1 sm_pawnshop_1
sm_hub_1 --force`).** Two distinct skies: `sp_tutorial_1` is `la` (cube upper-hemisphere mean
0.00335), `sm_pawnshop_1` and `sm_hub_1` both `pier` (0.01120, identical to five significant figures
on both bakes — the same six source PNGs reproduced, not cached; the bake re-authors each sky's
package on every run rather than skipping a found asset, since content-addressed skipping was not
worth the complexity at three maps sharing two skies). One `SM_SkyDome`, two `TC_Sky_*`, two
`MI_Sky_*` assets landed under `/ElysiumBaked/Sky/`, confirmed on disk. First bake attempt raised
`StaticMeshComponent: Failed to find property 'collision_enabled'` — `set_editor_property` does not
reach a component's collision state; fixed to the dedicated `set_collision_enabled` call, re-run
clean.

**Headless boot, all three maps, 14/14 vantages (6 + 4 + 4), each map's log carrying the new gate's
own line** (`sky '<name>': baked (MapsOnV2Models) — runtime assembly skipped`) confirming the
runtime path is actually bypassed, not merely present alongside the old one. Per R5.1's own
measurement that the shot harness disagrees with itself by up to 94% of pixels between two identical
captures, no pixel-regression comparison is drawn here either — this is a did-it-boot /
did-it-appear witness only, as that entry already established for the rest of R5.

Tests: 5 pytest (`pipeline/tests/test_bake_map_sky.py` — the intensity join's three cases,
including the black-cube/`KINDA_SMALL_NUMBER` guard, the dome geometry against the runtime's own
vertex/triangle table, and the sky packages named by sky rather than by map) and 2 C++
(`Elysium.Substrate.SkyBake.MissingFacesReturnsNull`, `.BadPackagePathReturnsNull` — the two
failure paths that need no staged corpus; the join's success path is `BuildSkyCubeFrom`'s own,
unchanged, and is exercised by the real bake above). `uv run elysium build`: Succeeded. `uv run
elysium test Elysium.Substrate`: **438 of 438 in 4.2 s** (436 before, +2 new).
`uv run elysium test Elysium.Content.Map`: **7 of 7**. `uv run pytest` over the touched modules:
**101 passed**.

Follow-ups: no per-content-hash skip for the sky bake (every map re-decodes its six PNGs even when
unchanged — cheap at three maps and two skies, worth revisiting once more maps join the flag);
`ToggleSkybox`'s baked-dome half is wired but not itself tested; R6.4 is still where the sky face
PNGs themselves become first-class imported textures with provenance, a different question from
which set this bake samples; R8.1 still owns deleting the runtime path this task only bypassed.

**R5.4 — V2 materials wired (2026-09-02).** A converted map's every world, brush-model and
3D-skybox face binds the `MI_` the material lane imported for its `vtmb:material:*` unit — a
PAKFILE-patched face by its `maps/<map>/…` id — and no per-map world material package is written
for it any more. Contract in `seam_map_map.md` → "## Import — materials (R5.4)"; the master-side
ruling in `seam_map_material.md` → "Scene fog on the world masters (R5.4)".

**Resolution is offline, through the material lane's own sidecars.** The R5.1 reader now records
the unit each face group resolved (`Scene.units`), and `map_geometry.stage_map` turns that into the
manifest's `materials` table — asset path by `importers.materials.asset_path_for`, root master and
blend through the sidecar's `master`/`patchBase` — reading `<key>.provenance.json` rather than
`manifest.json`, which describes the material lane's last run and shrinks to one directory under
`--select`. A unit the lane never staged fails the map naming every missing key; an instance the
editor cannot load fails again by asset path. `bake_map_v2.MapBakeV2` overrides `material_for`,
`resolve_materials`, `_material_sets` (world set empty → `/ElysiumBaked/<map>/Materials` pruned)
and the wet-cubemap import; `_chunk_world`'s Nanite predicate reads the root instance's blend.
Measured: 419 / 160 / 323 face groups bound (241 / 66 / 134 patched), 0 unresolved; every baked
chunk and brush references only `/ElysiumBaked/Materials/…` (1,399 / 361 / 1,439 refs, 0 per-map,
0 legacy shared); 185 / 70 / 234 assets saved; 14/14 vantages boot with `0 unbound or
default-bound slots`.

**The acceptance check found the fog gap, and it is closed.** *"The V2 Unlit/Lit masters' fog CPD
path must match what `ApplySceneFog` stamps"* — there was none: no V2 master read
`mat_fog.fog_from_primitive`, so the rebind (and every R1 prop already on a V2 `MI_`) would have
fogged nothing while `ApplySceneFog` stamped 885 / 211 / 1,130 primitives. `M_V2_Lit`,
`M_V2_LitTranslucent`, `M_V2_Unlit`, `M_V2_TwoTexture` and `M_V2_Refract` now end with
`make_v2_materials._scene_fog` — the legacy graph's own term, CPD 0..3 / 4 / 5, `lerp(shaded,
fogColour, f)` over BaseColor, Specular and Emissive — with one instance value, `FogInscatter`
(0 on an `Additive` blend: Source fogs additive surfaces to black; the stage writes it). Declared
in `ElysiumSurfaceParams.h` / `EXPOSED_PARAMS` / the `*_PARAM_TABLE`s (the three-way pin) and
pinned a fourth way by index: the new offline test parses `ElysiumFog.h`'s slots and asserts each
fog node on each of the five masters is CPD-driven at exactly that index. `GRAPH_VERSION` 4;
`uv run elysium export bundle policy` regenerated the nine masters.

**Decals do not rebind, on a domain fact recorded for R7.6.** `UDecalComponent` renders only an
`MD_DeferredDecal` material (`DecalComponent.cpp` substitutes the default decal material for
anything else) and every V2 master is `MD_Surface`; the three maps' 97 distinct `.decals`
materials are `$decal` surfaces on `M_V2_LitTranslucent` (96) / `M_V2_Unlit` (1), not
`decalmodulate` units. So `_place_decals` keeps the legacy per-map `M_Decal` MIC —
`/ElysiumBaked/<map>/Materials/Decals` (27 / 14 / 61) is the one per-map material package a
converted map still authors — and R5.3's "MID-at-load" plan becomes R7.6's ruling to make, with
`ElysiumFog::ApplyToDecalMID` waiting for it.

**Provenance report, as data.** `materials_report.json` beside each staged pair classifies every
bound material (V2 master/blend/class, the legacy master `Bake._master_for` would have chosen and
its class, live proxies, wetness, `$decal`). Animated now: `sm_pawnshop_1` `dev/dev_tvmonitor1a`
(sine) and `signs/newsticker` (texturescroll); `sm_hub_1` `dev/dev_waterbeneath2@cubemapdefault`
and `water/sewer_water` (animatedtexture + texturescroll, on `M_V2_Water`) — 4 of the ~217,
because the three maps bind 902 groups of 19,121 units. Appearance class changed: 3 + 6 — seven
`$decal` world faces the legacy lane bound to the deferred-decal `M_Decal` as a mesh slot (a
surface mesh cannot draw that domain) and now draw as translucent surfaces, plus the two `sm_hub_1`
water faces on `M_V2_Water` with the lane's `Opaque` override (R7.2's). Wetness-driven: 0 / 9 /
14. The imported instances predated R5.3, so the 14 `sm_hub_1` wet units and the 2 additive units
were re-staged and re-imported **by directory** (7 launches, 16 instances rewritten, 555 reused,
never the corpus).

**Not a look verdict.** Frame means rose on every vantage (`sm_hub_1` (46, 31, 14) → (85, 62,
33); `sm_pawnshop_1` similar) and 96–100 % of pixels moved against the pre-R5.4 captures — every
surface changed shading model, so the R5.1 noise floor is not the ceiling and no pixel verdict is
drawn; boot, audit and the byte scan are the witnesses.

Tests: 4 pytest in `test_map_geometry.py` (patched-id resolution and root blend, loud failure
naming every unstaged unit, the report's classification, and the real three-map corpus resolving
every group to an instance that exists on disk), 1 in `test_materials_stage.py`
(`FogInscatter` on Additive only, the lane declared on exactly the five masters), 1 in
`test_make_v2_materials_editor.py` (CPD indices against `ElysiumFog.h`); the existing
header/table pins cover the new names, `Elysium.Policy.V2MasterParams` grew the four names per
master. `uv run elysium build`: Succeeded. `Elysium.Policy`: **9 of 9**. `Elysium.Substrate`:
**439 of 439**. `uv run pytest` over the eight touched modules: **290 passed**.

Follow-ups: the decal master (deferred-decal domain) was R7.6's and is answered — R7.2 re-cut
`M_V2_Decal` and retired `M_Decal`; R7.2 owns the scene-fog term
on `M_V2_Water` (its `FogColor`/`FogStart`/`FogEnd` are the VMT water-fog keys) and the two
water surfaces now on it with an `Opaque` override; R5.5 gives the 441 map-scoped patched
instances their probe; the material lane's `manifest.json` is per-run and a `--select` import
leaves it describing one directory — nothing in this task reads it any more, but `bake_verify`
and the lookdev should be checked before anyone else relies on it; the shot harness's identical
`sm_hub_1` frame means across four vantages predate this task.

**Review fix (2026-09-02): the Nanite predicate needed the master's own capability, not just the
blend.** The landed `_chunk_world` gate above (`MaterialBinding.opaque`, "reads the root
instance's blend") let the lane's `Opaque` override on `water/sewer_water` and
`dev/dev_waterbeneath2` (both `M_V2_Water`, built without `used_with_nanite`) place both faces in
a Nanite chunk; Unreal drew UE's default material for them in-game (`LogMaterial: Warning: ...
missing usage flag Nanite!`, only visible in a boot log, not the bake exit or the material-slot
audit). `MaterialBinding.opaque` now also requires the master be in `NANITE_CAPABLE_MASTERS`
(`M_V2_Lit`, `M_V2_LitTranslucent`, `M_V2_Unlit`, `M_V2_TwoTexture` — the four `make_v2_materials`
actually builds with `used_with_nanite=True`); a new `test_map_geometry.py` row pins an
`Opaque`-overridden `M_V2_Water`/`M_V2_Refract` instance to `opaque is False` and a same-blend
`M_V2_Lit` instance to `True`. Rescoped `uv run elysium export map sm_hub_1` (no `--force`) and
`uv run elysium debug shots sm_hub_1 --no-open` confirm the fix: `world: 194 chunk meshes` (back
to the pre-R5.4 total; 158 Nanite + 36 `T_`, up from 175's 158 + 17 because the two water faces
and, transitively, the cells they shared with the seven legitimately-reclassified decal/glass
faces now land in `T_`), material audit still `0 unbound or default-bound slots` over 502 mesh
assets, and zero `missing usage flag Nanite` lines in the rebuilt boot log (2 occurrences before
the fix, one per affected instance, `20260902T015018.928063Z-debug-shots.log:1711/1713`). `uv run
pytest pipeline/tests/test_map_geometry.py pipeline/tests/test_materials_stage.py`: **91 passed**.

**R5.5 — reflection captures (2026-09-02).** Per `cubemaps[]` origin (the one placement frame the
node translation already uses; a sample inside the 3D-skybox area takes the miniature transform for
both position and radius, the same owner-call rule miniature lights and props already follow — 0
such samples on the working corpus), one `SphereReflectionCapture` is placed at
`UElysiumSurfaceSettings.CaptureRadius` and the bake calls `UElysiumMapBakeLibrary::
BuildReflectionCaptures` (`GEditor->BuildReflectionCaptures` under `WITH_EDITOR`) before saving, so
`<map>_BuiltData` carries a rendered cube per capture rather than an empty actor. Contract in
`seam_map_map.md` → "## Import — reflection captures (R5.5)". `LightSpecularScale` flips 0→1 with
the capture work, riding the same settings CDO the lights now stamp from
(`light_specular_scale()`) instead of a deleted `UElysiumLightingSettings::SpecularScale` literal.

**Measured on the scoped rebake, `uv run elysium export map sp_tutorial_1 sm_pawnshop_1
sm_hub_1`.** Captures placed / built into `MapBuildData`: sp_tutorial_1 24/24, sm_pawnshop_1 14/14,
sm_hub_1 19/19 (57/57, 0 in a 3D skybox, `CaptureRadius` 1500 cm); "all 3 map bake(s) completed",
`REBAKE_EXIT=0`. `/ElysiumBaked/<map>/<map>_BuiltData.uasset` (physically
`Plugins/ElysiumBaked/Content/<map>/<map>_BuiltData.uasset`) exists and is fresh for all three.
Boot witness, `uv run elysium debug shots <map> --no-open` per map, one process at a time: all
three exit 0 with `0 unbound or default-bound slots` (sp_tutorial_1 464 mesh assets / 1191
components / 6 vantages; sm_pawnshop_1 179 mesh assets / 322 components / 4 vantages; sm_hub_1 502
mesh assets / 1534 components / 4 vantages). `Elysium.Content.MapBake.ReflectionCapturesBuilt`
(the registry re-count, `Level->MapBuildData->GetReflectionCaptureBuildData` read directly off the
loaded package): **Success**, 0 failed, 0 warnings, all three baked `MapsOnV2Models` levels checked
(none abstained) — `placed == components == built` on every map.

Tests: `uv run elysium build`: Succeeded. `uv run elysium test Substrate`: **441 of 441**, including
the two content-free `Elysium.Substrate.MapBake.*` rows (null world, empty world) and the extended
`Elysium.Substrate.LightRig` (`LightSpecularScale` reaches the rig mirror and the point light).
`uv run pytest pipeline/tests/test_map_geometry.py pipeline/tests/test_bake_map_captures.py
pipeline/tests/test_bake_map_sky.py pipeline/tests/test_unreal_launch_args.py
pipeline/tests/test_bake_orchestration.py`: **54 passed**, including the corpus-gated
`test_reader_stands_one_capture_per_lump_42_sample_on_the_working_corpus` (24/14/19, matching the
bake).

**R5.6 — lights final (2026-09-02).** On a `MapsOnV2Models` map the baked light actor is the truth:
`bake_map_v2._place_lights` places one actor per lump-15 `worldLights[]` row of the staged
`lights[]` table (manifest **v4**; `UE_map_sidecars.light_rows` is the one producer both the
staged table and `<map>.lights` are formatted from, so the two agree by construction) and writes
every value `UElysiumLightRig::ApplyToSource` used to derive at every load — position (miniature
transform for a sky row), normalised colour, intensity under the page's ceiling, reach with the
fallback/sky floor, non-inverse-square falloff with the page's exponent, spot cones from the
stopdot cosines, shadows per type and page flag, `SpecularScale` (R5.5's CDO), the Lumen/fog
scales, the sun angles — plus `bAllowMegaLights = true` / `MegaLightsShadowMethod = RayTracing` on
every local light. `derive_light` is that derivation as a pure Python function fed from the
`UElysiumLightingSettings` CDO (`lighting_calibration()`; every field is in the level recipe, so an
edited page re-authors the level on the next `export map`). Runtime: `UElysiumMapVisuals::
AdoptBakedLevel` routes a converted map to `UElysiumLightRig::AdoptBaked`, which opens no file and
derives nothing — the actor's values are the source's baseline, `RevertSource`/`ApplyLiveTuning`
return to them, and the rig still applies the R4.3 calibration asset by the same `SourceIndex` =
lump-15 ordinal = `elysium.src=<n>` tag and animates lightstyles off the new `elysium.style=<s>`
tag (`elysium.type=<t>` feeds the viewer and the batch toggle). Every other map runs `Adopt` on
`<map>.lights` byte for byte; the reader's deletion is R8.1's. Ruling in `seam_map_map_lighting.md`
→ "## Import" → "Lights final (R5.6)"; the four derivation assertions the R4.3 entry left in
`Elysium.Substrate.LightRig` are re-homed onto the bake's own output in
`bake_verify.verify_lights_baked` (falloff, MegaLights + RT method, shadows-per-page, `type`/`style`
tags) beside a light-count parity check against the legacy `.lights`.

**Measured on the scoped rebake, `uv run elysium export map sp_tutorial_1 sm_pawnshop_1
sm_hub_1`** (the recipe tracker resumed the interrupted first run; no `--force`): staged rows
396 / 161 / 687 (`worldLights` count, lump order); placed sp_tutorial_1 **395** (58 in the 3D
skybox; the 396th row is the type-5 skyambient, which tints the SkyLight and places none),
sm_pawnshop_1 **161** (31 sky), sm_hub_1 **687** (60 sky; 20 of them texlights); 0 rows with
`max(rgb) <= 0`; captures unchanged at 24/14/19 built; "all 3 map bake(s) completed",
`REBAKE_EXIT=0`. `uv run elysium verify maps sp_tutorial_1 sm_pawnshop_1 sm_hub_1`: lights parity
**395 actors / 395 placing rows / 395 matched**, **161 / 161 / 161**, **687 / 687 / 687**; the
pre-existing per-row reach and cone checks (`verify_lights`, `737683ba`) pass on all 395 / 161 /
687 against the `.lights` sidecar; **0 light findings**. The command still exits 5 with 77
findings on the three maps, none about lights and none new here: `baked glass material instance
missing: /ElysiumBaked/<map>/Materials/MI_glass_*` (the legacy `.mtl` glass check still expects
the per-map `MI_` packages R5.4 stopped writing for converted maps) and the prop `alpha material
exported an RGB albedo` / `baked glass albedo is not alpha-capable` texture checks — a follow-up
for the verify script, filed below. Boot witness, `uv run elysium debug shots <map> --no-open`,
one process at a time, all three exit 0: rig log `LightRig: adopted 395 baked lights (final
values, MapsOnV2Models; 10 animated) +sun` (styles 1 and 6 animate; the 8 rows on styles 32–34
are entity-switched and clamp to 0 exactly as the legacy `Adopt` does), `161 baked lights (…; 0
animated)`, `687 baked lights (…; 1 animated)`; no calibration asset exists for any of the three,
so no rows applied; material audit 0 unbound slots on all three (464/1191, 179/322, 502/1534);
6/6 + 4/4 + 4/4 vantages captured. `shots_diff.py` against the only promoted baseline
(`8077e5b5`, R2.1, pre-R5): all 14 vantages over 0.5% (95.69–100.00% changed, sp_tutorial_1 mean
44.66–66.12, sm_pawnshop_1 59.17–68.11, sm_hub_1 32.93–37.17) — the same magnitude R5.1 measured
between two captures of an identical build, so per that finding the harness remains a
did-it-boot / did-it-appear witness and attributes nothing to this task. No screenshot was read.

Tests: `uv run elysium build`: Succeeded. `uv run elysium test Substrate`: **442 of 442 in 3.9 s**,
the new leaf `Elysium.Substrate.LightRigBaked` (tag parse with legacy defaults, adopt keeps the
actor's values, a settings push leaves them alone, a calibration row applies by `SourceIndex`, the
`type`/`style` tags reach the source, revert returns to the bake). `uv run pytest
pipeline/tests/test_bake_map_lights.py pipeline/tests/test_map_geometry.py
pipeline/tests/test_bake_map_captures.py pipeline/tests/test_bake_map_sky.py
pipeline/tests/test_unreal_launch_args.py pipeline/tests/test_bake_orchestration.py`: **61
passed** — the lights module's 7 cases pin `derive_light` against `ApplyToSource` (ceiling and
extended ceiling, texlight no-shadow and fallback reach, spot cones and the inner≤outer clamp, sun
lux floor and no MegaLights, the miniature transform and reach floor), `light_rows`' load-time
fixups and the exact `.lights` line, the shared manifest version, and — corpus-gated — that the
staged rows format back to all three `.lights` files line for line (396 / 161 / 687).

Follow-ups: `bake_verify.py`'s `.mtl` glass check and the prop alpha-texture checks fail every
converted map for reasons that predate this task (above); the Cog Lights viewer and
`ElysiumLightProbe` read a source's `Mag` (0 on a baked source, the bake consumed it) — a viewer
concern, filed not fixed; the `.lights` reader, `Adopt` and the `Elysium.Substrate.LightRig`
derivation assertions retire together at R8.1.

**Roadmap R5 landed (2026-08-31).** Stage "R5 — the map bake rebuilt on the GLB corpus" (MP-4) is
done, six tasks; R5.1, R5.2, R5.4, R5.5 and R5.6 each already have their own detailed Settled
entries above (commits `d4000638`; `5d9efdb7`/`44c9e56d`; `cc5062d9`/`4963ab17`; `eb87314e`;
`f29191d7`), so this entry rolls the stage up and gives R5.3 the detail it never got its own entry
for in this file — its ruling lives in `seam_map_material.md` → "Decal fog and wetness homes
(R5.3)".

- **R5.1** (`d4000638`) — a map's geometry and props come off the root unit: every `staticProps[]`
  record becomes one baked actor on the R1 corpus mesh, folded into the level rather than a
  companion data asset. See "R5.1 — a map's geometry and props come off the root unit" above.
- **R5.2** (`5d9efdb7`, fix `44c9e56d`) — the SkyLight actor's cubemap is baked from the root unit
  instead of left blank; the fix closed an empty-`Source` regression the first landing shipped.
  See "R5.2 — sky baked" above.
- **R5.3** (`a2df23de`) — decal fog and wetness get a V2-master home. `M_V2_Decal` declares
  `FogColor`/`FogStart`/`FogInvRange` (`mat_fog.fog_from_params`) with `ElysiumFog::
  ApplyToDecalMID` (`Source/ElysiumUE/Public/ElysiumFog.h`) as the one shared setter for both a
  future runtime decal spawn and the bake-time placement call, defaulting neutral/unfogged;
  wetness needed no per-map or per-placement instance at all, since its live half is already the
  global `MPC_ElysiumEnvironment` (`GlobalWetness`/`WetnessOutputScale`,
  `AElysiumMapActor::ApplyWeatherTuning`'s sole writer) — only the static per-unit multiplier
  (`WetnessScale`, plus its `WetnessDriven` gate) needed to become a real scalar, added to
  `M_V2_Lit`/`M_V2_LitTranslucent`, reversing that one table row's earlier "provenance only" call
  for those two masters specifically. Ruling picked MID-at-load for fog over a per-map
  `MaterialInstanceConstant` child, because the latter would keep every decal/wetness-bearing
  material map-scoped forever (19 `globalwetness` + 38 `decalmodulate` units), defeating R5.4's
  shared-`MI_` switch for exactly the surfaces that need it most. Verified after commit: build ok,
  `Elysium.Substrate` 439/439 (new `FogDecalMID`), Policy 9/9 after `uv run elysium export bundle
  policy` regenerated the stale V2 masters (gitignored), pytest 118/118. Not landed in this commit,
  explicitly deferred to R5.4 and landed there: the decal placement lane actually calling
  `ApplyToDecalMID` (`_place_decals` still bound the legacy per-map `M_Decal` MIC at R5.3 time) and
  any live re-import/re-bake of the three maps. Follow-up, pre-existing and untracked: legacy
  `M_Decal` fails to compile for `PCD3D_SM6`.
- **R5.4** (`cc5062d9`, review fix `4963ab17`) — the V2 bake's `material_for` binds the imported
  `MI_` by `vtmb:material` id and per-map material packages stop for converted maps; the review fix
  gated Nanite on the bound master's own capability rather than blend mode alone, restoring
  `sm_hub_1`'s chunk count 175→194 and clearing 2 "missing usage flag Nanite" boot warnings. The
  decal half of R5.3's plan moved out to R7.6 on a domain fact found here: a `UDecalComponent`
  renders only `MD_DeferredDecal`, and every V2 master is `MD_Surface`. See "R5.4 — V2 materials
  wired" above.
- **R5.5** (`eb87314e`) — one `SphereReflectionCapture` per `cubemaps[]` row of the root unit,
  radius from the `UElysiumSurfaceSettings` CDO, built via `BuildReflectionCaptures` with a
  fail-if-mismatch guard against the placed count; `LightSpecularScale` reaches the rig and every
  non-overridden light from the same CDO. See "R5.5 — reflection captures" above.
- **R5.6** (`f29191d7`) — lights final: one `light_rows()` producer feeds both the staged
  `lights[]` table (manifest v4) and the legacy `.lights` sidecar; `derive_light` restates
  `ApplyToSource` in pure Python off the `UElysiumLightingSettings` CDO; `MapsOnV2Models` maps
  route through `AdoptBaked` (actor snapshot as baseline; `.lights` reader bypassed, not deleted —
  R8.1 owns deletion, 105 maps still adopt through it). See "R5.6 — lights final" above.

Follow-ups carried out of the stage: `bake_verify.py`'s legacy `.mtl` glass-`MI_` check and the
prop alpha/albedo texture checks fail every converted map for pre-R5.6 reasons (R5.4 stopped
per-map material packages) — `uv run elysium verify maps` on the three corpus maps exits 5 with 77
non-light findings, filed as needing a V2-lane-aware rewrite; no shot baseline has been promoted
since `8077e5b5` (pre-R5), so the harness's run-to-run noise floor still blocks pixel regression
for every R5 task; the Cog Lights viewer and `ElysiumLightProbe` read a baked source's `Mag`, which
the bake consumes to 0 (viewer-only); the cross-check between `NANITE_CAPABLE_MASTERS`
(`map_geometry.py`) and `make_v2_materials.py`'s `nanite=True` call sites is still hand-maintained,
not a shared constant or pytest.

**R6.4 — brush fade distances (2026-09-02).** `func_lod`'s `DisappearDist` reaches the brush it
hides as a cull range, and both distance-culled brush classes have a class. **The bake has no
brush actor to write onto** — a brush entity's mesh (`SM_brush_<N>`) is never placed in the level;
the runtime attaches it to the convex entity body (`BuildBrushBody` → `BuildBrushVisual`) — so the
bake's product for a brush entity is its entity-table row, and the distance lands there the way
`elevator_floors` and `blocks_player` already do: `UE_map_sidecars.brush_cull_max_cm` is the pure
rule (`func_lod` with a `brush_mesh` and `DisappearDist > 0` → `cull_max_cm = DisappearDist ×
2.54`, C `atof`, 4 decimals; nothing else), `build_entities` writes the field, the `.ents` reader,
the R4.1 asset row (`FElysiumMapEntityRow::CullMaxCm`, stage recipe version 2) and both parity
checks carry it, and `FElysiumEntityWorld::BuildBrushBody` calls `SetCullDistance(cull_max_cm ×
body scale)` on the visual the moment it is attached — the runtime derives nothing. The join is
the row's own `model`; VtMB's `C_Func_LOD::ShouldDraw` (client.dll `100bb710`) is a hard draw/no-
draw on view distance with a hysteresis band, not an alpha fade, so `LDMaxDrawDistance` is the
faithful mapping and the same one R5.1 gives a `FADES` prop. New leaves `func_lod`
(`DisappearDist`) and `func_areaportalwindow` (`FadeStartDist`/`FadeDist`/`TranslucencyLimit`/
`BackgroundBModel`, for the debug view) in `ElysiumBrushFadeClasses.cpp`; the `Stubs` test's
"unregistered classname" moved to `info_node_cover_corner`. Ruling in `seam_map_map.md` →
"Import — geometry and placements" → "Brush fade distances (R6.4)", the field-list row beside
`brush_mesh`, and the row-shape table in `seam_map_map_entities.md`.

**`func_areaportalwindow` writes no cull range, and that is an owner call filed to R7.** All 13
rows on the three maps are point rows (`model` absent — 0 of 13 carry one; the R6 census's 246 are
the same shape): the distances govern the `target` backing brush whose mesh the exporter omits by
the standing ruling in `docs/vtmb/entity_io.md`, and `BackgroundBModel` is the foreground glass
VtMB keeps drawn at every distance, so culling it at `FadeDist` would invert the behaviour. The
one faithful reading — re-mesh the backing and give it `MinDrawDistance = FadeDist × 2.54`
(transparent near, black far, VtMB's two end states) — reverses that ruling, so it is stated in the
R7 list rather than taken here. The class exists and the numbers are inspectable.

**Measured.** `uv run elysium export map sp_tutorial_1 sm_pawnshop_1 sm_hub_1` (no `--force`; the
map tasks re-ran on the producer change, the bake reused every asset — the level recipe is
untouched): `.ents` rewritten with `cull_max_cm` on **8 / 12 / 13** `func_lod` rows (every meshed
`func_lod` on the three maps; 2,200–4,000 units → 5,588–10,160 cm; 0 rows of any other class carry
one). `uv run elysium import map-entities --maps sp_tutorial_1 --maps sm_pawnshop_1 --maps
sm_hub_1`: staged 4,933 rows, parity **0 mismatches** on all three, **3 imported / 0 reused / 0
failed** (recipe v2 re-authored every listed map). `uv run elysium verify maps …`: new
`verify_brush_cull` reports `8 func_lod row(s) with a cull range in sp_tutorial_1.ents, 8 matched
in /ElysiumBaked/sp_tutorial_1/DA_sp_tutorial_1_Entities`, **12 / 12**, **13 / 13**; the command
still exits 5 with the same **77** pre-existing non-light findings (R5.6's `.mtl` glass and prop
alpha checks), **0 new**. Boot witness, `uv run elysium debug shots <map> --no-open`, one process
at a time, all three exit 0: defs from the re-authored `DA_<map>_Entities` asset on each, rig
`adopted 395 / 161 / 687 baked lights` unchanged, material audit 0 unbound slots (464/1191,
179/322, 502/1534), **6/6 + 4/4 + 4/4** vantages captured; no `brush '…': no baked mesh` warning.
No screenshot was read.

Tests: `uv run elysium build`: Succeeded. `uv run elysium test Substrate`: **443 of 443 in 6.4 s**,
the new leaf `Elysium.Substrate.BrushCull` (a func_lod's `cull_max_cm` is the attached visual's
`LDMaxDrawDistance` unchanged, a miniature func_lod's rides the ×16 body scale, a brush with no
range stays at 0, both classes spawn as real classes, the areaportalwindow carries its names and
has no body). `uv run pytest pipeline/tests/test_map_sidecars.py
pipeline/tests/test_map_entity_asset.py`: **29 passed** — 7 parametrized cases pin
`brush_cull_max_cm` (the ×2.54, the `atof` prefix read, zero and absent as "never culled", the
other classes and the areaportalwindow as `None`) and a corpus-gated case per map asserts every
meshed `func_lod` row's `cull_max_cm` against its own `DisappearDist` and that no other row
carries one.

Follow-ups: the `func_areaportalwindow` owner call above (R7 list); `bake_verify.py`'s 77
pre-existing findings are unchanged and still R5.6's follow-up.

**R6.2 — switched lights and lightstyles everywhere (2026-09-02).** `light` and `light_spot` are
a real leaf (`ElysiumLightClasses.cpp`, `FElysiumLight`) and the rig owns Source's lightstyle
pattern table. The join is by **style**, as VRAD made it: the leaf never touches a source — its
`TurnOn`/`TurnOff`/`Toggle`/`SetPattern`/`FadeToPattern` write its style's pattern through
`IElysiumEmbodiment::SetLightStylePattern` (map actor → `UElysiumLightRig::SetStylePattern`), and
the rig's per-frame tick scales every source tagged `elysium.style=<s>` (R5.6) by that pattern on
its own clock — texlight rows included, since a type-0 row carries the same tag. The rig's
64-entry table is seeded with the twelve engine patterns and `"m"` elsewhere and survives
`Adopt`/`AdoptBaked`; **styles ≥ 12 are no longer clamped to 0 on either lane**, so the entity-
switched rows animate. Semantics read off the corpus, not HL2: `CLight::Spawn` (`10130460`;
`START_OFF` → `"a"`, else the authored `pattern`, else `"m"`), on (`10130610`; the pattern only
when ≥ 2 letters and not starting `'a'`, else `"m"`), off (`10130690`; `"a"`), toggle
(`101306f0`), `SetPattern` (`10130780`), `FadeToPattern` (`10130800`) and `FadeThink`
(`101308d0`; one letter per step towards the target's first letter, the whole pattern on arrival,
re-thinking every `fade_time` — VtMB's own key, `0.05` on every corpus light, floored at 0.05
where retail floors against an unrecovered cvar); `ScriptHide`/`Kill` turn off first and
`ScriptUnhide` turns on, as retail's overrides do. A style < 32 takes no input. Pattern and fade
state save in the leaf's block and re-publish on load. `light_dynamic` (`FElysiumLightDynamic`)
is the one light with no lump-15 row: a runtime point/spot through the legacy `ApplyToSource`
path — new `UElysiumLightRig::AddRuntimeSource`/`RemoveRuntimeSource` build a non-baked
`FLightSource` from the raw magnitude, reach and cosines and derive it under the page (retires
with the `.lights` lane, R9) — with `TurnOn`/`TurnOff`/`Toggle` by visibility, on at spawn
(`CDynamicLight::Spawn` `10056a90`), and `parentname` attachment through the ordinary
`ResolveParentAttachment` walk via the new `FElysiumEntity::GetAttachChild()` hook. Its magnitude
is a **stated convention** (no VRAD row exists): `pow(c/255, 2.2) × S × 100/2.55` is the lump-15
intensity a `light` receives for `_light "r g b S"` (fitted ratio-exact on `sp_tutorial_1`'s
switched rows), with `S = 100 × 2^brightness`. The two stub rows for `light`/`light_spot` are
gone. Ruling in `seam_map_map_lighting.md` → "## Import" → "Switched lights and lightstyles
(R6.2)".

**Boot witness, through the rig's own log (no screenshot read).** `uv run elysium debug shots
sp_tutorial_1 --no-open`: `LightRig: adopted 395 baked lights (final values, MapsOnV2Models; 18
animated, 8 switched) +sun` — the R5.6 line read `10 animated` with the 8 rows on styles 32–34
clamped to 0; they are the 8 switched. The leaf's spawn writes follow in the same log, one per
light entity: `LightRig: style 32 <- 'm' (2 sources)` ×2 (both `chop_light` rows), `style 33 <-
'm' (1 source)` (`houselights`), `style 34 <- 'm' (5 sources)` ×5 (`tunnel_lights`) — every
switched row reached by its own light's pattern, all `'m'` because no corpus light on the map
authors `START_OFF` or a `pattern`. `sm_pawnshop_1`: `161 … 0 animated, 0 switched`; `sm_hub_1`:
`687 … 1 animated, 0 switched` (its one styled row is style 10, engine-animated). All three exit
0, 6/6 + 4/4 + 4/4 vantages, material audit 0 unbound (464/1191, 179/322, 502/1534). No
`light_dynamic` exists on the working corpus (36 rows elsewhere), so its witness is the Substrate
leaf only. Ritual runs for the record: `export map` on the three maps reused every asset (no bake
input changed), `verify maps` still exits 5 with the same 77 pre-existing findings, brush cull
8/12/13 and lights parity 395/161/687 unchanged.

Tests: `uv run elysium build`: Succeeded. `uv run elysium test Substrate`: **445 of 445 in
6.5 s** — `Elysium.Substrate.LightSwitch` (spawn writes for on / `START_OFF` / authored pattern
and nothing for style 0; the tutorial's `prop_switch → chop_light.Toggle` off and on; `TurnOn`
on a dark pattern writing `"m"` and restoring a multi-letter one; `SetPattern` verbatim;
`FadeToPattern` stepping `n`, `o` on the world clock at `fade_time` and finishing on `"pq"` then
stopping; `ScriptHide`/`ScriptUnhide`; the rig keeping style 32 on an adopted source, counting it
switched, `'a'` → multiplier 0 and `'m'` → 1, refusing an empty pattern and style 64) and
`Elysium.Substrate.LightDynamic` (a spot and a point stood through `BuildDynamicLight`, the
white/brightness-0 spec at `100 × 100/2.55`, reach `distance × 2.54`, the spot offered as the
attach child, on at spawn, `TurnOff`/`Toggle` by visibility). The first run of `LightSwitch`
failed on the test, not the leaf: it expected the fade to start from `'m'` after a `TurnOn`,
but retail's `TurnOn` leaves `m_iszPattern` untouched (the leaf does too), so the test now sets
the pattern before fading. No pytest module is touched (no producer change).

Follow-ups: the `light_dynamic` magnitude convention above is stated, not recovered — a
`light_dynamic` map (none on the working corpus) is where it gets its first look, with the
`.lights` lane it rides on until R9; the Cog Lights viewer does not yet show the pattern table
(the leaf's debug state shows its own style, pattern and the rig's current pattern).

**R6.3 — detail props (2026-09-02).** The `dprp` game lump is placed: **143,412 records over 41
models on 52 of 108 maps** corpus-wide (every one a version-2 model record, no sprites; 35,521
with a non-zero `swayAmount`), and on the working corpus `sp_tutorial_1` **6,031 over 6 models**
(`grassa`/`grassb`, three `rocks/small*`, `junk4`; every `swayAmount` 0), `sm_pawnshop_1` **0**,
`sm_hub_1` **528 over 3** (`weedb`/`weedc`/`weedd`; 231 swaying). The offline reader
(`map_geometry._detail_placements`, manifest **v5**, `details.models[]` + compact
`details.records[]` rows) resolves every record in lump order to its R1 stem through the frame a
static prop takes, and the editor half (`bake_map_v2._place_details`) spawns **one
`AElysiumDetailPropActor` per model per map** — a plain actor whose root is a
`UInstancedStaticMeshComponent` on `/ElysiumBaked/Meshes/SM_<stem>` — with one instance per
record in lump order, so instance `k` is the model's `k`-th record and `bake_verify` counts them
back. No detail model needed staging: each record is already a `dependencies[]` model row, so
R1's map-scoped selection had all nine on disk. Ruling in `seam_map_map.md` → "Detail props
(R6.3)"; the record contract in `docs/vtmb/bsp_format.md`.

**What the record bakes to.** No collision (`dprp` is read by `client.dll` alone —
`CDetailObjectSystem`/`CDetailModel`, `100e0d90`/`100e0250`…; the server never sees a detail
object), no shadow (VRAD never lit by one; the record's `lighting`/`lightStyles` are its baked
answer for the 2004 renderer and the R5.6 actors light the instances instead, so the bytes are not
applied), the scene-fog CPD stamp every prop takes, a miniature record on the static prop's sky
transform in its own `_sky` component (none on the three maps), and **the cull range is VtMB's
own**: `Instances->SetCullDistances(762, 1524)` cm from two new Models-page fields,
`DetailDrawDistanceCm` = `cl_detaildist` **600 in** and `DetailFadeRangeCm` = `cl_detailfade`
**300 in** — read off the shipped `client.dll` (`CDetailObjectSystem::vfunc10` registers both;
the default strings `"600"`/`"300"` sit at `102b9fa0`/`102b9f8c`), never a literal. VtMB's alpha
ramp over the band (`1 − (d² − (dist − fade)²)/(dist² − (dist − fade)²)`, the factor that function
computes) is not reproduced: the ISM culls hard at `cl_detaildist` and exposes the band as
`PerInstanceFadeAmount`, filed below.

**Sway is wired, and the amplitude is an owner call (→ R7.6).** `swayAmount / 255` rides as
per-instance custom data float 0 and the three model masters (`M_V2_Lit`, `M_V2_LitTranslucent`,
`M_V2_Unlit` — every corpus detail material is `unlitgeneric` → Unlit) carry one World Position
Offset term, `make_v2_materials._detail_sway` (`GRAPH_VERSION` 4 → 5): `sin(Time + (x + y)/2.54) ×
sway × saturate((local.z − min.z)/(max.z − min.z)) × DetailSwayAmplitude` along world `(1, 1, 0)`,
behind a static switch **`UseDetailSway`** (default off) so a switched-off branch compiles to the
constant zero `HLSLMaterialTranslator::IsMaterialPropertyUsed` does not count as a WPO use and no
world chunk, prop or character on the master pays for it. The bake authors, once per detail
material, `/ElysiumBaked/Meshes/Detail/MI_DetailSway_<material path>` — a child of the imported
`MI_` whose only own value is the switch — and binds it on the component's slots (**9** on the
corpus: `grassa`, `grassb`, `rocksmall`, `trashpile`, `weedb`, `weedbleaves`, `weedc`, `weedda`,
`weeddb`); a detail material on a master without the switch is a named bake failure. The
amplitude is `UElysiumSurfaceSettings.DetailSwayAmplitude` → `MPC_ElysiumSurfaces`, default
**12.7 cm**: VtMB's client never reads the byte (no sway cvar, no sine in `CDetailModel`), so the
number is Source's own first reading of it (`cl_detail_max_sway` 5 units) and the choice is on the
R7 list. Ruling in `seam_map_material.md` → "Detail sway on the model masters (R6.3)", the switch
in the Lit/Unlit tables, the knob in the knob contract. `UElysiumModelSettings` gained
`BlueprintType` so the editor's Python can read the page (`unreal.ElysiumModelSettings` was not
exported — the lighting page is only because of its `BlueprintCallable` push).

**Runtime.** `ElysiumBakedTags::Detail` (`elysium.detail`) + `DetailModel(stem)`;
`UElysiumMapVisuals::AdoptBakedLevel` buckets the actors, sums `DetailInstanceCount` /
`DetailModelCount` (Cog Maps/Status rows, the boot line), and `elysium.props` hides them with the
static props. `AuditMaterials` walks the new components like any other.

**Measured on the scoped rebake, `uv run elysium export map sp_tutorial_1 sm_pawnshop_1
sm_hub_1`** (all three levels re-authored; `REBAKE_EXIT=0`): staged `detailProps` **6,031 / 0 /
528**; bake `details: 6031 instances over 6 component(s) (0 in the 3D skybox, 0 swaying); cull
762..1524 cm; 4 sway material(s)`, `the unit places no detail props`, `528 instances over 3
component(s) (0 in the 3D skybox, 231 swaying); cull 762..1524 cm; 5 sway material(s)`; levels
saved in 48.6 / 6.5 s / hub in the same run. `uv run elysium verify maps …`:
`details: 6031 instances over 6 component(s), 6031 staged records over 6 model group(s), 6
matched; cull 762..1524 cm`, `0 / 0 / 0 matched` on the pawnshop, `528 / 3 / 3 matched` on the
hub; **77 findings, the same 77 pre-existing non-light ones, 0 new** (still exit 5, still R5.6's
follow-up). Boot witness, `uv run elysium debug shots <map> --no-open`, one at a time, all exit
0: `baked 'sp_tutorial_1': 1448 actors (…), 395 lights, 2371 hulls, 6031 detail instances over 6
models`, `'sm_pawnshop_1': … 0 detail instances over 0 models`, `'sm_hub_1': … 528 detail
instances over 3 models`; material audit **0 unbound** on all three (470/1197, 179/322, 505/1537
— the six and three instanced components are in the walk); **6/6 + 4/4 + 4/4** vantages
captured. No screenshot was read; per R5.1 the harness attributes nothing to this task.

Tests: `uv run elysium build`: Succeeded. `uv run elysium export bundle policy`: the nine masters
regenerated, all-switches-true probes 320 / 1,908 / 189 pixel-shader instructions on
Lit / LitTranslucent / Unlit (the sway branch compiled on each). `uv run elysium test Policy`:
**9 of 9** (`V2MasterParams` now pins `UseDetailSway` on the three). `uv run elysium test
Substrate`: **446 of 446 in 6.6 s**, the new leaf `Elysium.Substrate.DetailProps` (the two page
defaults as `600 × 2.54` / `300 × 2.54`, `DetailSwayAmplitude` as `5 × 2.54` and bound, the tag
helpers, and the spawned actor's shape: ISM root, static, `NoCollision`, no shadow, one custom
data float). `uv run pytest` over `test_map_geometry.py test_bake_map_details.py
test_bake_map_lights.py test_make_v2_materials_editor.py test_materials_stage.py
test_bake_map_captures.py test_bake_map_sky.py test_bake_orchestration.py test_matgraph.py
test_import_materials_editor.py`: **196 passed** — the record → placement mapping on a synthetic
unit (dictionary join, lump order, the frame, backslashed paths, the raw byte, the miniature flag,
the loud out-of-range failure), the corpus-gated "every record of the root unit is placed and the
per-model counts are the unit's own" on the three maps, the editor half's grouping and sway
normalisation and sky transform, the eleven-column row round-trip and the shared manifest
version, and the existing three-way name pin now covering `UseDetailSway`.

Side finding, fixed in passing: the generator's fake editor accepts any pin name, so the first
policy run failed live on `TransformPosition`'s unnamed input (`connect(…, "Input")` refused;
`""` is the first pin) — the fake still cannot see this class of error.

Follow-ups: VtMB's alpha ramp over the `cl_detailfade` band (a dithered opacity on
`PerInstanceFadeAmount`, masked masters only — Unreal has no per-instance alpha on an opaque
draw) is not reproduced, the hard cutoff is; the sway amplitude is R7.6's owner call; the
`MI_DetailSway_*` children are not pruned by any map bake (shared package, like the meshes) and a
retired detail material leaves its child behind until a corpus-scoped prune exists; the 32
detail models outside the working corpus are staged like any other model when their maps join
`MapsOnV2Models`, and every one of the 41 is `scenery/`, so the master check has no known
failing case.

**R6.1 — sprites on the V2 Sprite master (2026-09-02).** Every `env_sprite` is drawn, coronas
included (owner, 2026-09-02): the offline stage reads each `env_sprite` block of the producer join
in lump order (`map_geometry._sprite_records`, manifest **v6**), resolves it through the material
lane's provenance sidecar (the imported `MI_`, the VMT's `$spriteorientation`) and the texture
lane's (the base texture's size) into one `sprites[]` row (`resolve_sprite_table`), and the editor
half (`bake_map_v2._place_sprites`) spawns **one `AElysiumSpriteActor` per row**: the `MI_` re-parented
once per `(MI_, blend)` through `/ElysiumBaked/Sprites/MI_Sprite_<material>_<blend>` (the blend
the entity's `rendermode` selects through the `$spriterendermode` row table, `UseVertexColor` and
`UseVertexAlpha` on), `SizeInches = (clamp(scale, 0, 8) or 1) × texture`, `rendercolor`/`renderamt`
/`renderfx`, `parallel_upright`, `CSprite::Spawn`'s hidden rule (`start_hidden`, or named without
spawnflag 1), the miniature transform for a sky row, and the tags `elysium.sprite` +
`elysium.ent=<lump ordinal>`. The runtime half is the task's one piece of rendering code:
`UElysiumSpriteComponent` / `FElysiumSpriteSceneProxy` draws one camera-facing (or yaw-only) quad
per view with Source's glow rule read off `client.dll`'s `GlowBlend` (`100c24a0`) — a rendermode-3
corona's width is `size × dist / 200` (screen-constant), its brightness `clamp(19000 / dist², 0.05,
1)`, and every sprite's blend is multiplied by the **visible fraction of a per-sprite GPU occlusion
query** — a `4 × 4` grid of sub-primitive boxes tiling a square `3/128` of the view distance at the
origin, answered by the renderer one frame later and smoothed at `r_glowfadein` 0.2 s up /
`r_glowfadeout` 0.1 s down. All seven numbers are `UElysiumSpriteSettings` fields (Project Settings →
Elysium → Sprites) shipped at VtMB's values. `FElysiumEnvSprite` restates `CSprite` (spawn on when
unnamed or Start On; `HideSprite`/`ShowSprite`/`TurnOn`/`TurnOff`/`ToggleSprite`; the base chain's
`ScriptHide`/`ScriptUnhide`/`Kill`) and publishes `bOn && !IsInert()` through the new
`IElysiumEmbodiment::SetBakedSpriteVisible(EntityIndex, bVisible)` → `UElysiumMapVisuals::
SetSpriteVisible`, which finds the actor by its entity tag. `<map>.sprites` and
`UE_bsp_to_scene.write_sprites` are gone. Ruling in `seam_map_map.md` → "Sprites (R6.1)".

**Two corpus facts the bullet did not carry, read off the binaries.** `renderfx` 14
(`kRenderFxNoDissipation`) skips the whole distance block of `GlowBlend` (`100c30e9`): no
`19000 / dist²` and **no screen-constant scaling** — the corona stays at `scale × texture` and only
the visibility fade applies; **207 of sm_hub_1's 309 rows and 55 of sp_tutorial_1's 96 carry it**,
so most corpus coronas are fixed-size halos. And `kRenderWorldGlow` (9) keeps its world size too
(`100c3197`); only mode 3 scales. Both are in the rule and its tests. The census: sm_hub_1's 309
`env_sprite` rows are **113 mode 3 + 154 mode 5 + 42 mode 1** (the bullet's "309 coronas" was the
row count), sp_tutorial_1 96 (87 + 9), sm_pawnshop_1 74 (28 + 46).

**Measured on the scoped rebake, `uv run elysium export map sp_tutorial_1 sm_pawnshop_1
sm_hub_1`** (all three levels re-authored; `REBAKE_EXIT=0`): `sprites: 96 placed (87 glow, 0 hidden
at spawn, 0 in the 3D skybox); 4 material child(ren)`, `74 placed (28 glow, 6 hidden at spawn, 40 in
the 3D skybox); 5 material child(ren)`, `309 placed (113 glow, 44 hidden at spawn, 54 in the 3D
skybox); 9 material child(ren)`; levels saved in 18.3 / 5.7 / 12.2 s. `uv run elysium verify maps
…`: `sprites: 96 actors, 96 staged rows, 96 matched (87 glow)`, `74 / 74 / 74 (28 glow)`, `309 /
309 / 309 (113 glow)` — the first verify caught the bake writing `unreal.Color` positionally
(FColor's BGRA layout swapped red and blue on every tinted corona), fixed with named channels and a
`sprite_actor_shape` recipe term; **77 findings, the same 77 pre-existing non-light ones, 0 new**
(still exit 5, still R5.6's follow-up). Boot witness, `uv run elysium debug shots <map> --no-open`,
one at a time, all exit 0: `baked 'sp_tutorial_1': 1544 actors (110 world, 32 sky, 775 props, 123
decals), 395 lights, 2371 hulls, 6031 detail instances over 6 models, 96 sprites (87 glow)`,
`'sm_pawnshop_1': 513 actors (…), 161 lights, 1376 hulls, 0 detail instances over 0 models, 74
sprites (28 glow)`, `'sm_hub_1': 2463 actors (194 world, 53 sky, 955 props, 219 decals), 687 lights,
3842 hulls, 528 detail instances over 3 models, 309 sprites (113 glow)`; material audit **0
unbound** on all three (470/1197, 179/322, 1537 components); **6/6 + 4/4 + 4/4** vantages captured.
No screenshot was read; per R5.1 the harness attributes nothing to this task.

Tests: `uv run elysium build`: Succeeded. `uv run elysium test Substrate`: **447 of 447 in 6.6 s**
(446 − the retired `EnvSprite` no-op leaf of `ElysiumWorldEffectsTests.cpp`, whose premise this
task reverses, + 2), the two new leaves `Elysium.Substrate.EnvSprite` (the spawn rule for unnamed / named / Start On /
`start_hidden`, the five inputs, `ScriptHide`/`ScriptUnhide` restoring the leaf's own state, `Kill`,
every write observed on the recording double by entity index) and `Elysium.Substrate.SpriteGlow`
(the seven page defaults and `FromSettings`, the size rule for modes 3 / 3+fx14 / 9 / 5, the
brightness clamp and its NoDissipation and plain-mode exits, the two smoothing rates, the query
footprint, the tag helpers, the actor's shape and its two bounds). `uv run pytest` over
`test_bake_map_sprites.py test_map_geometry.py test_bake_map_details.py test_bake_map_lights.py
test_bake_map_captures.py test_bake_map_sky.py test_bake_orchestration.py test_map_entities_glb.py`:
**144 passed** — the record reader on a synthetic join (lump ordinal, the frame, the scale clamp and
zero default, colour clamps, the hidden rule, the sky flag), the sidecar join (asset path, texture
size, orientation, the mode table with mode 6 failing, the loud missing-material and missing-texture
errors), the editor half's `sprite_actor_values` (size, tags, label, the sky transform, the child
name), the shared manifest version, and — corpus-gated — every `env_sprite` block of the three maps
as a row with the hidden rule re-derived from its own keys.

Follow-ups: the visible fraction is a 16-box boolean grid, not Source's pixel count (the page's
`SpriteQueryGrid` is the one number with no VtMB twin); plain sprites (modes 1/5) ride the same
query because `M_V2_Sprite` is depth-test-off on the master — the R7.7 owner call; `$spriteorigin`
is not applied (every quad is centre-anchored; 54 corpus units author one, all `[0.5 0.5]` on the
three maps' sprites) and `$spriteorientation oriented` (3 units) draws as a full billboard; the
`frame`/`framerate` keys are not read (a `candle`'s animation is its `MI_`'s own `animatedtexture`
lane); a sprite takes no scene-fog stamp (the master carries no fog term); a corona's bounds hold
its quad out to 200 m, past which the frustum test drops it; the `MI_Sprite_*` children are shared
and unpruned like the R6.3 sway children.

**R6.5 — ropes on `MI_`, and the factory shape (2026-09-02).** The overhead cables were the last
runtime consumer of the six legacy world masters, and `FElysiumMaterialFactory::Build` — master by
blend flag, textures through `FElysiumTextureCache`, three cvars for the feature scalars — existed
for them alone. Now the `.ropes` line is **12 tokens led by the material's unit id**
(`vtmb:material:<key> ax ay az bx by bz width_cm rest_cm nodes texscale flags`; `RopeShader` 0/1/2
→ `cable/cable`/`cable/rope`/`cable/chain`, else `RopeMaterial`, through `shared_corpus.material_key`),
written identically by `UE_map_sidecars.write_ropes` (the R3.5 default) and `UE_bsp_to_scene.write_ropes`
(the byte-comparable twin the R3.3 differ still diffs), and neither reads the shared corpus any
more: the decoded `tex`/`bump` paths and the `matflags` bits restated what the `MI_` already
carries. `UElysiumMapVisuals::BuildRopes` resolves the id by the R5.4 naming rule in C++
(`FElysiumContentPaths::BakedMaterial` = `importers.materials.asset_path_for`, `MaterialSafeName` =
`asset_names.safe_name`, pinned against the Python's own outputs including the `metalox_-64_128_0`
→ `metalox__64_128_0` double underscore), loads one `MI_` per distinct id per map and binds
`FElysiumMaterialFactory::Create(MI_, this)` — the whole factory now: a `UMaterialInstanceDynamic`
parented to the imported instance with **no override of its own**, wetness staying the
`MPC_ElysiumEnvironment` write and scene fog the primitive stamp. An id whose asset does not load
is a warning naming the path and a cable on the engine default, never another master. Retired with
the builder: `elysium.EmissiveScale`/`BumpScale`/`EnvReflect`, `elysium.EnhancedTextures` and
`SharedTexHiDir` (the legacy sky assembly samples the faithful `tex/` set, as the R5.2 bake already
did), the map visuals' texture cache, and the Cog "Material look" tab's hardcoded
Asphalt/Six-streets/Seven-surfaces wetness table. The six legacy masters, `make_world_materials.py`
and the legacy `bake_map.py` binding on unconverted maps stay for R9.2 — they now have **no runtime
reader**. Not gated on `MapsOnV2Models`: the material lane imported the whole install, so every
map's ropes resolve, and one code path is the point. Ruling in `seam_map_material.md` → "Ropes on
`MI_`, and the factory shape (R6.5)".

**Measured on the scoped rebake, `uv run elysium export map sp_tutorial_1 sm_pawnshop_1 sm_hub_1`**
(no `--force`; the producer edit invalidated the sidecar stage, the three levels re-authored and
saved, `REBAKE_EXIT=0`): `ropes: 70 cable segments (107 nodes, 3 materials)`, `21 (30 nodes, 2)`,
`76 (99 nodes, 3)` — sp_tutorial_1 48 `cable/cable` + 19 `cable/chain` + 3 `cable/chainb`,
sm_pawnshop_1 20 `cable/cable` + 1 `cable/chainb`, sm_hub_1 72 `cable/cable` + 3
`cable/cautiontape` + 1 `cable/chainb`. `uv run elysium verify maps …`: the new `verify_ropes`
reports `70 segments over 3 material(s), 3 bound under /ElysiumBaked/Materials/`, `21 / 2 / 2`,
`76 / 3 / 3` (every id folds to an existing `MaterialInstanceConstant`; the first run instead
tripped on `importers.materials` importing numpy, which the editor's Python lacks, so the verify
restates the fold through the pure `asset_names.safe_name` the way `bake_map_v2` restates its
roots); **77 findings, the same 77 pre-existing `.mtl` glass / prop alpha ones, 0 new**, still
exit 5 (R5.6's follow-up). Boot witness, `uv run elysium debug shots <map> --no-open`, one at a
time, all exit 0: `ropes: 70 cables over 3 material(s), 0 unresolved`, `21 / 2 / 0`, `76 / 3 / 0`
(no `LogElysiumVisuals: Warning: ropes` on any map); `baked 'sp_tutorial_1': 1544 actors …`,
`'sm_pawnshop_1': 513 …`, `'sm_hub_1': 2463 …` unchanged from R6.1; material audit **0 unbound** on
all three (470/1197, 179/322, 505/1537); **6/6 + 4/4 + 4/4** vantages captured. No screenshot was
read; per R5.1 the harness attributes nothing to this task.

Tests: `uv run elysium build`: Succeeded (twice — the first Substrate run caught my own wrong
expectation for the double-underscore fold, not the code). `uv run elysium test Substrate`: **392 of
392 in 6.6 s** by the report's own count (391 + 1), the new leaf `Elysium.Substrate.MaterialFactory`
(`Create(nullptr)` → null; `Create(MI_)`'s parent is the instance itself, not its master; zero
scalar, vector, texture and static-switch overrides) and `Elysium.Substrate.Ropes` re-pinned on the
12-token line (the material id, a pre-R6.5 14-token line and a bare-key line both dropped, the
Type-2/Dangling/clamp cases kept) plus eight `BakedMaterial`/`MaterialSafeName` cases (install,
nested, patched-map and cubemap-coordinate keys, the strip and `unnamed` rules, non-ids folding to
nothing). `uv run elysium test Elysium.Content.TutorialRopes`: 1 of 1 — every id on the tutorial
folds to a package that exists under `/ElysiumBaked/Materials/`, and the tutorial still names
`cable/chain`. `uv run pytest test_map_sidecars.py test_map_sidecar_diff.py
test_map_sidecar_default_path.py test_bake_orchestration.py`: **63 passed** — `rope_material_id`
over the shader row / authored material / backslash / `materials/…vmt` spellings, `write_ropes` on
a synthetic chain (12 tokens, the start node's shader row, Type 2 → two nodes, Dangling), and —
corpus-gated — every exported line of the three maps folding under the package root.

Follow-ups: `ElysiumLightProbe`'s emissive test still reads the legacy `EmissiveScale` name and
sees no V2 `SelfIllumAmount` (a viewer concern, filed not fixed); the `elysium.Ropes` A/B cvar
predates this task and is untouched; the R3.3 differ still compares `.ropes` byte for byte and
both producers moved together, so a 108-map run owes no divergence entry; the cable takes no
scene-fog stamp (it never did); the six legacy masters and their bake lane retire at R9.2.

**R6.6 — UI art off loose files (2026-09-02).** Not a HUD rework: every widget and layout stands
as it was, and only the image source moved. `ElysiumUI::LoadPngTexture` — the runtime PNG decode
behind ten sites — is deleted, and every picture a screen draws is now the **`T_` asset the
texture lane already publishes**, resolved by the same install path the screen always named:
`ElysiumUI::ArtTexture(path)` (`Private/UI/ElysiumUiArt.h`) normalises the key (case, `\`,
`materials/`, a trailing `.png`) and folds it through `FElysiumContentPaths::BakedTexture` —
the C++ twin of `importers.textures.asset_path_for(key, "Texture2D")` — to
`/ElysiumBaked/Textures/<dir>/T_<safe stem>`; a path whose `T_` does not exist falls through to the
`MI_` of the same path and its `BaseTexture`, the material lane's own VMT join, because **317 of
the 1,078 UI-tree materials name another texture** (`hud/disciplines/bloodheal_hud` →
`bloodheal_base`, every `_sel`, `general_items/flyer` → `lillyonbeachphoto`; measured over the
install, and identity for every path a screen names by hand). `FElysiumUiArtCache` (the sheet),
`UElysiumChargenPopup::Art` and `UElysiumHUDWidget::HudArtBrush` keep their caches, UV
sub-rectangles and 9-slices and swap only the load. **The use-icon ring drops its composited
atlas** (owner call): `ElysiumUseIconName(N)` is the one table, `ElysiumUI::UseIconArt(N)` folds it
to `hud/context_icons/<name>` with the compositor's two on-disk aliases (`Stakeable` → `stakable`,
`valve` → `valvewheel`), and the widget holds 72 brushes on 72 `T_` plus the ring's — the same
48-px cell, full UV; `UE_use_icons.py`, `hud/use_icons.png/.json` and the `use-icons` bundle are
gone. **Sign backgrounds draw**: `FElysiumSignData::Background.ImageName` (parsed since the sign
work, read by nothing until now) resolves to its `T_` and becomes the panel's inner plate behind
the body text, width and padding unchanged. The title lockup is `interface/mainmenu/vtm_title`, the
feed-vision mask `effects/spotlight`, the character stage's wallpaper quad
`interface/charactermaintenance/background`. The menu seal draws the clan's
`cm_clan_symbol_<clan>` and nothing in the front end (VtMB's `mm_*` seals are `particles/*.tga`, a
source no lane publishes — the named divergence, filed as **R7.8**); the front-end wallpaper, never
VtMB art, is `UElysiumUISettings::MenuWallpaper` (Project Settings → Elysium → UI → Menu, a soft
texture reference, unset by default). Retired from the loose root and the exporters: `ui/art/**`,
`ui/menu/**` (title, sprites, the six `MM_Skybox` faces, the particle closure), `ui/effects/`,
`signs/tex/` + `backgrounds.json`, `hud/use_icons.*`; `UE_extract_ui` keeps `resource/*.res`,
`strings.json`, `manifest.json`, `UE_extract_signs` keeps the definitions, and the eight
`FElysiumContentPaths::Ui*`/`Sign*` art helpers are deleted. Ruling in `ui-architecture.md` → "9.
Art from assets (R6.6)", the consumer note in `seam_map_texture.md` → "## Import".

**Measured.** No bake input changed; `uv run elysium export map sp_tutorial_1 sm_pawnshop_1
sm_hub_1` resumed as a no-op on the three levels (`REBAKE_EXIT=0`) and `uv run elysium verify maps
…` reports the R6.5 numbers unchanged (`ropes: 70 / 3 / 3`, `21 / 2 / 2`, `76 / 3 / 3` bound; **77
findings, the same 77 pre-existing, 0 new**, exit 5). Live witness through the editor bridge, `uv
run elysium run play` driven by the MCP tools (a diagnosis aid, not acceptance): the front end
came up with the title lockup drawn off `T_vtm_title` (`menu shown (main)`; `menu wallpaper: none
set (UElysiumUISettings::MenuWallpaper)`, the one wallpaper line, is the settings field being
empty), `elysium_new_game` → sp_tutorial_1 with `spawn_done` and `sign_active: true`: the first
tutorial popup drew on its `hud/signs` background with the HUD's clothing icon, life bar, blood
hearts and Masquerade/Humanity chrome around it; the whole session log holds **zero** `LoadPng`,
"no HUD art", "no sheet art", "no imported art", "no menu seal" or "no title lockup" lines, and its
only `.png` mentions are the two screenshots the bridge saved. Boot witness, `uv run elysium debug
shots <map> --no-open`, one at a time, all exit 0: `baked 'sp_tutorial_1': 1544 actors …`,
`'sm_pawnshop_1': 513 …`, `'sm_hub_1': 2463 …`, ropes `70 / 21 / 76` with 0 unresolved, material
audit **0 unbound** on all three (470/1197, 179/322, 505/1537), **6/6 + 4/4 + 4/4** vantages, 0
PNG-read or art-miss lines in any of the three logs. The two bridge screenshots were read as
did-it-appear only.

Tests: `uv run elysium build`: Succeeded. `uv run elysium test Substrate`: **393 of 393 in 6.8 s**,
the new leaf `Elysium.Substrate.UiArt` (the `BakedTexture` fold for nested, bare and
illegal-character keys and the empty key; `ArtKey`'s normalisation of authored spellings and
legacy `.png` names; `UseIconArt` for slots 1, 9, 11, 65, 72, 0 and 73, all 72 slots naming
`hud/context_icons/` art and the three duplicate pairs agreeing; `ClanSigilArt` over 2, 8, 0, 1
and 9). `uv run elysium test Elysium.Content.UiArt` (new, `ElysiumContentTests.cpp`): **151 of 151
names resolved** — the literal HUD tables (area, category, section glyphs), the sheet, popup, title
and mask paths, the seven sigils, the ring and the 72 icons, plus every `BackgroundImage` the
runtime's own `FElysiumSignData::Load` reads out of the 278 exported definitions (246 carry one) —
against the real `/ElysiumBaked/Textures` mount. `uv run pytest` over
`test_profile_package_contract.py test_export_orchestration.py test_cli_contract.py`: **136
passed** (the profiles' bundle set without `use-icons`, the bundle task table, the CLI contract).

Follow-ups: the sign background is drawn as the panel's plate, not at the authored `XPos/YPos/
Wide/Tall` through `ElysiumSign::RectToScreen` (the panel keeps its own 620-px layout; the
authored rect is parsed and unused, as before); `FElysiumSignData::Load` warns once per
`newspaper_*` dispatch wrapper it opens (30 files with no `SignData` block — the Content test walks
them all and inherits the noise); `ui/strings.json` and `signs/*.txt` are still loose reads (text,
not art) for R9.2; the `_sel`/`_hud` art variants and the armour portraits stay unresolved by the
HUD as they were (the fallback join would serve them the day the HUD names them); `elysium.
DrawSigns` predates this task.

**R6.7 — 3D-skybox composition (2026-09-02).** The wiring pass over the miniature: with R5.1
(props), R5.5 (captures), R5.6 (lights), R6.1 (sprites) and R6.3 (detail props) live, every class
the corpus places inside the `sky_camera`'s area already carried the producer join's `sky` flag
and was placed through `world(v) = scale × (v − origin)` — the audit found the composition
**almost** complete and closed three gaps, none of them a look. Ruling in `seam_map_map.md` →
"3D-skybox composition (R6.7)": one membership rule, one transform, one fog set, one scope marker,
for every lane. (1) **The transform's source.** `MapBakeV2` still inherited `Bake._read_sky`, so a
converted map's sky chunks, props, details, sprites, lights and captures were placed through the
legacy `<map>.sky` sidecar while the staged manifest carried the unit's own `sky` block beside
them; `_read_sky` now answers from `_StagedGeometry` (`miniature_transform`), the block is in the
level recipe, and a map with a `sky_camera` but no sky *faces* — which `write_sky` never writes a
sidecar for — no longer collapses its sky-flagged props onto `16 × p`. (2) **The scope marker.** A
miniature detail component and a miniature sprite carried only their class tags (`Sky/Details`,
`Sky/Sprites` folders are editor-only metadata), so the runtime could not tell them from the
world's: `UElysiumMapVisuals::ApplySceneFog` stamped **no** detail component at all (the bake's
default slot was the only writer, so `elysium.Fog` and a `.env` re-export never reached the
instanced grass) and `ToggleSkybox` left a miniature's details and sprites standing. Both now
carry `elysium.sky` beside their class tag (`detail_actor_tags`, `sprite_actor_values`;
`ElysiumBakedTags::InMiniature`), `AdoptBakedLevel` buckets by the class tags first so the marker
never lands a detail or sprite in the static-mesh sky bucket, `ApplySceneFog` stamps every detail
component with the set its marker names, `ToggleSkybox` hides the miniature's details and sprites
with its chunks and props, and the boot line and Cog rows count them (`n components in the 3D
skybox`, `n in the 3D skybox`). `DETAIL_ACTOR_SHAPE` 2 / `SPRITE_ACTOR_SHAPE` 3 re-author the
levels. (3) **Nothing counted the miniature back.** `bake_verify.verify_sky_scope` now matches, on
every `MapsOnV2Models` map whose manifest says `sky.ok`, the sky-flagged placements, detail groups
and sprite rows to their `elysium.sky` actors through the transform (position and scale per row,
instance 0 of a sky detail component) and checks that every sky prop and sky detail component
carries the same fog slots the sky chunks carry. The sky *fog* itself needed no change: the
`sky_camera`'s set was already stamped on sky chunks, sky props and sky detail components at bake
and re-stamped on the first two at load; the third is the runtime gap (2) closed.

**What the corpus says.** All three working maps have a `sky_camera` (`scale` 16; `sp_tutorial_1`
at `(6825.6, −4500.9, 7248.5)` cm, `sm_pawnshop_1` and `sm_hub_1` share `(−3152.1, −57.2,
12647.3)`), not two: the miniature holds **30 / 21 / 49** props, **0 / 40 / 54** sprites, **58 /
31 / 60** lights, **0 / 0 / 0** detail records and **0 / 0 / 0** cubemap samples. `sp_tutorial_1`
authors no fog on either set; the other two fog the world `1270→12700 cm` and the miniature
`20320→203200 cm` (`.env`, distances already `× 16`). No corpus map places a detail record in its
miniature, so the sky detail path is exercised by the Substrate leaf and the verify rule, not by a
baked actor.

**Measured on the scoped rebake, `uv run elysium export map sp_tutorial_1 sm_pawnshop_1
sm_hub_1`** (all three levels re-authored by the recipe terms; "all 3 map bake(s) completed"):
`level: 809 prop actors (30 in the 3D skybox)` / `194 (21)` / `1043 (49)`; `sprites: 96 placed
(…, 0 in the 3D skybox)` / `74 (…, 40 in the 3D skybox)` / `309 (…, 54 in the 3D skybox)`;
`details: 6031 instances over 6 component(s) (0 in the 3D skybox, …)` / none / `528 over 3 (0 in
the 3D skybox, 231 swaying)`; `fog: world off, 3D skybox off` / `world 1270->12700cm, 3D skybox
20320->203200cm` ×2; lights 395 (58) / 161 (31) / 687 (60); levels saved in 9.1 / 4.4 / 9.6 s.
`uv run elysium verify maps …`: `sky scope: scale 16 about (6825.6, -4500.9, 7248.5); props 30
actors / 30 staged / 30 matched; details 0 / 0 / 0; sprites 0 / 0 / 0; sky chunk fog slots start
20320, 1/range 0.000000`, `(-3152.1, -57.1, 12647.3); props 21 / 21 / 21; details 0 / 0 / 0;
sprites 40 / 40 / 40; … start 20320, 1/range 0.000005`, `props 49 / 49 / 49; details 0 / 0 / 0;
sprites 54 / 54 / 54; … 1/range 0.000005` (`1 / (203200 − 20320)`); lights parity 395 / 161 /
687, details 6 / 0 / 3 matched, sprites 96 / 74 / 309 matched; **77 findings, the same 77
pre-existing non-light ones (46 legacy glass `MI_`, 31 prop alpha/albedo), 0 new** (still exit 5,
still R5.6's follow-up). Boot witness, `uv run elysium debug shots <map> --no-open`, one at a
time, all exit 0: `fog: world off on 891 primitives, 3D skybox off on 32` (110 chunks + 775 props
+ 6 detail components; 2 sky chunks + 30 sky props), `world 1270->12700cm on 211 primitives, 3D
skybox 20320->203200cm on 25`, `on 1152 primitives, … on 53` — the detail components are in the
world count for the first time; `baked 'sp_tutorial_1': 1544 actors (110 world, 32 sky, 775
props, 123 decals), 395 lights, 2371 hulls, 6031 detail instances over 6 models (0 components in
the 3D skybox), 96 sprites (87 glow, 0 in the 3D skybox)`, `'sm_pawnshop_1': 513 actors (38, 25,
173, 38), 161 lights, 1376 hulls, 0 detail instances …, 74 sprites (28 glow, 40 in the 3D
skybox)`, `'sm_hub_1': 2463 actors (194, 53, 955, 219), 687 lights, 3842 hulls, 528 detail
instances over 3 models (0 …), 309 sprites (113 glow, 54 in the 3D skybox)`; material audit **0
unbound** on all three (470/1197, 179/322, 505/1537); **6/6 + 4/4 + 4/4** vantages captured. No
screenshot was read; per R5.1 the harness attributes nothing to this task.

Tests: `uv run elysium build`: Succeeded (after one side fix, below). `uv run elysium test
Substrate`: **450 of 450 in 4.0 s**, the new leaf `Elysium.Substrate.SkyScope` (the marker
helper beside the model and entity tags; a spawned level of one sky chunk, one prop, a world and
a sky detail component and a sky sprite adopted by class first with the two sky counts read off
the marker; `ApplyEnvironment` with the world set off and the sky set `100..200 cm` stamping the
sky chunk and the sky detail with `1/range 0.01` and the prop and the world detail with 0;
`ToggleSkybox` hiding the chunk, the sky detail and the sky sprite and leaving the prop and the
world detail standing, then showing them again). `uv run pytest` over `test_bake_map_sky_scope.py
test_bake_map_sprites.py test_bake_map_details.py test_map_geometry.py test_bake_map_lights.py
test_bake_map_captures.py test_bake_map_sky.py test_bake_orchestration.py`: **74 passed** — the
three new cases pin, per lane, a sky-flagged sprite row and a sky-flagged detail record to the
miniature transform (`(1010, 2020, 3030)` about a camera at `(1000, 2000, 3000)` → `(160, 320,
480)`, scale 16) and the marker beside the class tags, a world row to neither, and the lane's
transform to the manifest's own `sky` block.

Side finding, fixed in passing: R6.6 defined the static log category `LogElysiumUiArt` in both
`ElysiumUiArt.cpp` and `ElysiumUiArtCache.cpp`; the new test file shifted the unity blobs so the
two landed in one translation unit and the build failed on a struct redefinition. The cache's is
`LogElysiumUiArtCache` now.

Follow-ups: a sky sprite is the one miniature class drawn unfogged — `M_V2_Sprite` carries no fog
term (R6.1's follow-up; Source's sprite shader fogs a card to the fog colour, or to black when
additive), so the term belongs with whatever next touches that master (R7.7); `verify_lights`
still reads the legacy `.sky` for a sky source's reach scale (the check is R4.3's, against the
`.lights` sidecar, and retires with it at R9); `RegisterRuntimeBrush`'s sky-scope brush entities
and the `.ents` join's `sky` field are the runtime's own and untouched; the legacy lane's
`_read_sky` and `write_sky` stay for the 105 unconverted maps until R9.

**Roadmap R6 landed (2026-09-02).** Stage "R6 — wiring: what is already understood goes live on
the V2 lane" (was R6.3–6.4 in part, R7.1, R7.3, R7.4, R7.7 before the owner's re-cut of
2026-09-02, which split the old "consumers beyond maps" and "life" stages by how much was still
undecided: R6 wiring with no design doubt, R7 the families that need a ruling first, R8 the
skeletal lane, R9 retire; nothing deferred out of the three stages, a surfaced owner call moves
to R7 rather than blocking the stage) is done, seven tasks, each with its own detailed Settled
entry above (commits `7bb14f1a`; `823c65b2`; `d9dc0d64`; `3d7800f6`; `b4a55186`; `b8882c96`;
`be2d539e`), landed in the order R6.4, R6.2, R6.3, R6.1, R6.5, R6.6, R6.7. None was reported
blocked or partial. Per map, on the three working-corpus maps (`MapsOnV2Models`); `--all` has
still never run.

- **R6.1** (`3d7800f6`) — sprites on the V2 Sprite master: every `env_sprite` is one
  `AElysiumSpriteActor` in the baked level off the staged `sprites[]` table (manifest v6), the
  glow rule and its per-sprite GPU occlusion query in a custom proxy, seven `UElysiumSpriteSettings`
  fields at VtMB's values, `FElysiumEnvSprite` driving visibility by entity index. See "R6.1 —
  sprites on the V2 Sprite master" above.
- **R6.2** (`823c65b2`) — switched lights and lightstyles everywhere: `light`/`light_spot` write
  their style's pattern on the rig's clock through `ApplyToSource`, `light_dynamic` stands
  through the same path. See "R6.2 — switched lights and lightstyles everywhere" above.
- **R6.3** (`d9dc0d64`) — detail props: every `dprp` record instanced off the root unit (manifest
  v5), one `AElysiumDetailPropActor` per model per map, culled at `cl_detaildist` /
  `cl_detailfade` from the Models page, swaying by `swayAmount` behind the masters'
  `UseDetailSway` switch. See "R6.3 — detail props" above.
- **R6.4** (`7bb14f1a`) — brush fade distances: `func_lod`'s `DisappearDist` rides the entity row
  as `cull_max_cm` and the world applies it to the attached brush visual. See "R6.4 — brush fade
  distances" above.
- **R6.5** (`b4a55186`) — ropes on `MI_`: the `.ropes` line carries the `vtmb:material` id, the
  cable binds the imported instance, `FElysiumMaterialFactory::Create(MI_)` is the whole factory,
  the legacy world masters lose their last runtime reader. See "R6.5 — ropes on `MI_`, and the
  factory shape" above.
- **R6.6** (`b8882c96`) — UI art off loose files: every screen draws the texture lane's `T_` assets
  by install path through `ElysiumUI::ArtTexture`, the use-icon atlas and the PNG decoder are gone,
  sign backgrounds draw. See "R6.6 — UI art off loose files" above.
- **R6.7** (`be2d539e`) — 3D-skybox composition: the V2 lane places the miniature through the
  manifest's own transform, miniature detail components and sprites carry the `elysium.sky`
  scope marker, the runtime fogs and toggles them with the miniature, `bake_verify` counts the
  miniature back. See "R6.7 — 3D-skybox composition" above.

Follow-ups carried out of the stage: `bake_verify.py`'s 77 pre-existing non-light findings
(46 legacy glass `MI_`, 31 prop alpha/albedo) still fail every converted map with exit 5 and
need the V2-lane-aware rewrite R5 filed; the shot harness's run-to-run noise floor still blocks
pixel regression (no baseline promoted since `8077e5b5`); the `func_areaportalwindow` owner call
(R6.4) and the detail sway amplitude (R6.3 → R7.6), the depth-tested plain sprites question
(R6.1 → R7.7, now carrying the sprite master's missing fog term from R6.1/R6.7) sit on the R7
list; VtMB's alpha ramp over the `cl_detailfade` band is not reproduced (R6.3); `$spriteorigin`,
`$spriteorientation oriented`, `frame`/`framerate` and the 200 m corona bounds are R6.1's open
edges; the `MI_DetailSway_*` and `MI_Sprite_*` children are shared and unpruned by any map bake;
`light_dynamic`'s magnitude convention is stated, not recovered, and the Cog Lights viewer shows
no pattern table (R6.2); `ElysiumLightProbe` still reads the legacy `EmissiveScale` and the cable
takes no scene-fog stamp (R6.5); the sign background draws as the panel's plate rather than at the
authored rect, `ui/strings.json` and `signs/*.txt` stay loose reads for R9.2, and the `_sel`/`_hud`
art variants and armour portraits stay unresolved by the HUD (R6.6); `verify_lights` still reads
the legacy `.sky` and the legacy lane keeps `_read_sky`/`write_sky` for the 105 unconverted maps
until R9 (R6.7).

**R7.1 — water on the V2 lane (2026-09-04).** VtMB's water is four stacked facts the 2004 engine
happens to share a name for — a surface look, a `CONTENTS_WATER` volume, an underside face set and
the eye-under-the-plane fog — and the ruling keeps them apart onto four different Unreal
mechanisms rather than reproducing the two 2004 render-target passes. Full ruling and engine
citations in `docs/architecture/water-architecture.md`; contract in `seam_map_material.md` →
`M_V2_Water` and `seam_map_map.md` → "Import — water volumes (R7.1)". `GRAPH_VERSION` 8 → 9,
`MANIFEST_VERSION` 8 → 9 (`pipeline/src/elysium_pipeline/importers/map_geometry.py:59`,
`pipeline/unreal/bake_map_v2.py:59`).

**Ten rulings** (A–J; each a transcription unless named a modernization):
- **A — `M_V2_Water` is a Single Layer Water master.** `MSM_SingleLayerWater`, `BLEND_Opaque`,
  one-sided, never Nanite; the VMT's own `$fogenable`/`$fogcolor`/`$fogstart`/`$fogend` become
  Absorption/Scattering (`σ = WaterFogScale / range`, `range = max((FogEnd−FogStart)×2.54, 1)`,
  the colour gamma-decoded through the new `Graph.pow`), `$refracttint` is Color Scale Behind
  Water, `$reflecttint`'s luma scales the class specular, `CheapWater` multiplies extinction ×16.
  *Named modernization*: the two 2004 render-target passes and the in-volume fog become SLW's
  refraction, Lumen's reflection and SLW's absorption/scattering — the three things the roadmap
  line asked to answer in one place (`pipeline/unreal/make_v2_materials.py::_build_water`, `:2266`).
- **B — `water.volumes[]` is a new stage product**, one row per real `LEAFWATERDATA` record
  (sentinels and a `la_bradbury_3` shadow row dropped and named in `dropped[]`), each carrying
  `surfaceZCm`/`minZCm`, the fog keys resolved through `surfaceTexInfoID` → texinfo → material, and
  the volume's `CONTENTS_WATER` brushes as convex plane sets in Unreal cm — a brush counts only
  when a non-bevel side authors `%compilewater`, which is how a `tools_shadow`-sided caster is told
  apart from real `func_detail` water (`importers/map_geometry.py:71-227`).
- **C — one `AElysiumWaterVolumes` actor per converted map**, bake-placed and tagged
  `elysium.water`, adopted by `UElysiumMapVisuals`; `AElysiumMapActor::UpdatePlayerWater` (called
  from `PreMoveTick`) classifies the player's feet/waist/eyes against it — `CheckWater`'s three
  point queries, level 0–3 — before the move reads the level
  (`Source/ElysiumUE/Private/Map/ElysiumMapActor.cpp`, `ElysiumWaterVolumes.h`'s
  `ElysiumWater::ClassifyBody`).
- **D — underwater is a post-process linear fog.** `M_ElysiumUnderwater` (`MD_PostProcess`,
  `lerp(scene, FogColor, saturate((SceneDepth−FogStart)×FogInvRange))`) is registered by the water
  actor as an `IInterface_PostProcessVolume`; the engine reads `bIsEnabled` before it ever calls
  `EncompassesPoint` (`World.cpp` `DoPostProcessVolume`), so the gate is `bIsEnabled`, set per view
  from `UWorld::OnBeginPostProcessSettings` the way the Water plugin's own
  `UUnderwaterPostProcessVolume` does it — the only shipped precedent for a volume whose shape the
  engine cannot evaluate itself.
- **E — the underside faces stay.** The corpus already carries `dev/dev_waterbeneath2` as real
  down-facing faces on every top plane; a self-bottomed unit's own name sets a new `Underside`
  switch (zero specular, zero extinction) because the engine strips reflection from every
  down-facing water face and SLW's camera-under-water branch is dead code in 5.8 (§8, below).
- **F — `$forcecheap` is a look, not a LOD.** `CheapWater` multiplies extinction ×16 (the body
  reads as its `$fogcolor` at any depth) and leaves the reflection to Lumen. *Named modernization*:
  the cheap cubemap is replaced by the same Lumen mirror the expensive path uses.
- **G — `leafMinDist[]` is provenance.** The engine never loads lump 46; nothing is derived from
  it — a fact, not a choice, and the retraction of the roadmap line's old "underwater from
  `leafMinDist`" premise.
- **H — no scene-fog term on the water surface.** The R6 follow-up ("`ElysiumFog` term on
  `M_V2_Water`") closes as *not on this master*: the refracted world and the Lumen mirror already
  carry their own per-primitive fog. *Named modernization* — VtMB draws the surface under the
  world fog; here the surface's own scatter and specular are unfogged.
- **I — the camera clearance offset is transcribed.** `CViewRender::GetWaterOffset`
  (`cl_waterdist` 4 in) is `ElysiumCam::SolveWaterOffset` in closed form — treading (level 2)
  raises the view until clear of the plane, submerged (level 3) lowers it until back under —
  applied in `UElysiumCameraComponent::ApplyBaseToView` outside the boom-blend block, since the
  case it exists for (a treading or swimming body) is first person, where that blend is 0. *Named
  divergence*: the original's one-unit Z-step quantization is dropped for the exact closed-form
  distance.
- **J — `SineUVTranslate` on `M_V2_Lit`.** The `sine` → `texturetransform` UV slide the
  `sm_pier_1` `objects/surf` wave cards author (amplitude, offset, `SinePeriod`/`SineTimeOffset`
  shared across the one sine a master supports); default `(0,0,0,0)` is neutral
  (`importers/materials.py:186-371`).

**Out of scope, named (ruling list item 10 in the architecture doc):** drips, mist, splashes
(R7.3's families — VtMB itself spawns no splash on entry), `dsp_water` and wade footsteps
(`plans/audio.md`), NPC water levels (the entity substrate's), `trigger_hurt` pools (already
entities).

**What the ruling retracts** from the pre-ruling text: five engine claims (SLW's camera-under-water
branch does not exist as `bCameraIsUnderWater`; Opacity is coverage, not murk; coefficients are
1/cm not 1/m; Lumen honours SLW roughness — it is not a forced mirror; MegaLights still lights
water forward through the light grid), the "underwater from `leafMinDist`" wording, the "22 maps"
count (25 maps carry `LEAFWATERDATA`, 24 have a drawable water brush, 22 units sit on the `Water`
shader — the counts diverge by feature, not by disagreement), and the assumption that the V2
lane's normal lane was already right on water — it was not: the `animatedtexture` proxy bound the
29-frame **DUDV** array into `NormalMapFrames` and never set `UseNormalMap`, so every water
instance shipped with a flat normal on both the legacy and V2 lanes until this fix
(`importers/materials.py:141-149`).

**Named divergences beside the faithful behaviour** (owner-visible, full list in
`water-architecture.md` §9): two render targets collapse to one SLW pass (no planar camera, no
DUDV RT offset); linear volume fog becomes exponential extinction, half-distance matched by
`WaterFogScale`; `$refractamount`/`$reflectamount` and `mat_waterswirl` are declared, not wired
(the normal is used at unit strength); `$reflecttint` reaches the reflection only as luma, not red
tint; Fresnel is SLW's own Schlick, not `(1−N·V)^5` with R0=0; the underwater post-process *adds*
to the per-primitive scene fog rather than replacing it (the volume fog's range is far inside the
world fog's on the corpus, so the double term is invisible in practice); fog-volume selection is
point-in-brush of the view location, not a PVS walk; the water surface itself takes no scene fog
(ruling H); water faces are lit forward by Lumen and the light grid — VtMB's are `SURF_NOLIGHT`.

**Lane changes.** `pipeline/unreal/make_v2_materials.py`: `_build_water` → the SLW graph, shading
model set *after* the graph is built (authoring under `MSM_SINGLE_LAYER_WATER` mid-build logs a
spurious "Failed to compile Material" per write); `REQUIRED_MPC_SCALARS` gains `WaterFogScale`;
`M_ElysiumUnderwater` generated beside the V2 masters. `pipeline/unreal/matgraph.py`: `Graph.pow`
routed through `_binop` wired `A`/`B` pins that `UMaterialExpressionPower` does not have (it names
`Base`/`Exponent`, reachable as `Exp` through `UMaterialGraphNode::GetShortenPinName`) — dead code
with no call site until `_build_water`'s fog-colour gamma decode, fixed and pinned by
`pipeline/tests/test_matgraph.py`. `pipeline/unreal/make_surface_knobs.py` /
`UElysiumSurfaceSettings`: the `WaterFogScale` row. `importers/materials.py`: the `Underside`
switch, the `SineUVTranslate` lane, the normal-frames fix. `importers/map_geometry.py`:
`water.volumes[]`, `MANIFEST_VERSION` 9. `pipeline/unreal/bake_map_v2.py` / `bake_map.py`:
`_place_water` (one actor, `FElysiumWaterVolume`/`FElysiumWaterBrush` structs), `TAG_WATER`,
`WATER_ACTOR_SHAPE`; the `_set` failure message's hardcoded `effects:` prefix generalized to
`bake:` now that `_place_water` shares the helper. `pipeline/unreal/bake_verify.py`:
`verify_water`, `MapsOnV2Models`-gated. `Source/ElysiumUE`: `AElysiumWaterVolumes` +
`FElysiumWaterVolume`/`FElysiumWaterBrush`, the `elysium.water` tag
(`ElysiumBakedTags::Water`), `UElysiumMapVisuals::GetWaterVolumes`,
`AElysiumMapActor::UpdatePlayerWater`, `UElysiumCameraComponent::SetWaterState`,
`ElysiumCam::SolveWaterOffset`. `Config/DefaultElysium.ini`: `WaterFogScale=1.386294`; `sm_pier_1`
added to both `MapsOnNewTransport` and `MapsOnV2Models`.

**Owner calls.** The one open question the ruling carried into the task, §7's camera clearance
offset, resolved **transcribed** (ruling I above) rather than left for a later swim-able slice.
Two calls surfaced during landing, both accepted: `+MapsOnNewTransport=sm_pier_1` alongside
`+MapsOnV2Models=sm_pier_1` (`Elysium.Content.MapEnvironment.FieldParity` reads the DA assets, not
the sidecar, and those assets are dead weight without the R4.6 cutover flag); and Water's
`BaseTexture` slot stays required (`dev/ocean`/`dev/oceanbeneath` — 29-frame DUDV VTFs with no
`BaseTextureFrames` lane on the master — stay unstaged, since `UseBaseTexture` defaults off,
Opacity is 0 without it, and `pipeline/tests/test_materials_stage.py::test_basetexture_multiframe_array_stays_a_failure_on_water`
pins the current behaviour deliberately; neither map needs them — `sm_pier_1`'s ocean card is
`water/blackwater`).

**Measured (2026-09-04, foreground, this machine).** `uv run pytest` **3,104 passed, 0 failed**
(3,103 before, +1 for the `Graph.pow` pin); the plan's named subset **173 passed, 0 failed**.
`uv run elysium build`: Succeeded, 0 errors. `uv run elysium test substrate`: **457 of 457
executed, 0 failed** — `Elysium.Substrate.Water`, `Elysium.Substrate.WaterActor` and
`Elysium.Substrate.Camera` all Success. `uv run elysium test policy`: **10 of 10, 0 failed**,
`Elysium.Policy.V2MasterParams` Success. `uv run elysium export bundle policy`: 0 "SingleLayerWater
materials requires the use of SingleLayerWaterMaterial output node", 0 "No inputs to Single Layer
Water", 0 "Failed to compile Material" (unwrapped grep — the earlier run's "0 Failed" reading had
been defeated by the console log's ~75-column hard wrap splitting the phrase); `M_V2_Water`
probes **933** pixel-shader instructions on the all-switches-true permutation and saves;
`M_ElysiumUnderwater` saves. `uv run elysium import materials`: **33 imported, 19,676 reused, 0
pruned**, exit 6 on 4 pre-existing failures unrelated to water (`dev/ocean`, `dev/oceanbeneath`,
`envmap/gioint`, `skybox/hav_env`); `Underside=True` on exactly **1** instance
(`MI_dev_waterbeneath2`, parent `M_V2_Water`), `SineUVTranslate` on exactly **1**
(`MI_surf`, parent `M_V2_LitTranslucent`, `[0.5, 0.0, 0.0, 0.0]`). Staged manifests, both
`MANIFEST_VERSION` 9, one volume, zero dropped: `sm_hub_1` — index 0, `surfaceZCm` −14937.74,
`minZCm` −14988.54, `vtmb:material:water/sewer_water`, `fogEnable` true, `fogColor` (0.019608,
0.019608, 0.0), 2.54/2600.96 cm, 1 brush of 6 planes; `sm_pier_1` — index 0, `surfaceZCm`
−1582.42, `minZCm` −1666.24, `vtmb:material:water/invisible_water`, `fogEnable` true, `fogColor`
(0.086275, 0.078431, 0.039216), 2.54/1016.0 cm, 1 brush of 6 planes. Both bakes: `[bake] water: 1
actor placed with 1 volume(s)`; `verify_water`: `1 staged rows, 1 matched` on both maps. No
`Failed to compile Material Instance` anywhere in `Saved/Logs/ElysiumUE.log` or its eight backups
across both map bakes; no leftover UnrealEditor processes.

**Order of work.** (i) the ruling into `water-architecture.md`, `seam_map_material.md` →
`M_V2_Water`, `seam_map_map.md` → "Import — water volumes (R7.1)"; (ii) `matgraph.py`'s `Graph.pow`
fix (a prerequisite the SLW fog-colour decode exposed); (iii) `make_v2_materials.py`'s `_build_water`
+ `M_ElysiumUnderwater`, `make_surface_knobs.py`'s `WaterFogScale`; (iv) `importers/materials.py`
(`Underside`, `SineUVTranslate`, the normal-frames fix) and `importers/map_geometry.py`
(`water.volumes[]`); (v) `bake_map_v2.py`'s `_place_water` and `bake_verify.py`'s `verify_water`;
(vi) `Source/ElysiumUE` — `AElysiumWaterVolumes`, `UpdatePlayerWater`, `SolveWaterOffset`,
`SetWaterState`, the post-process registration; (vii) `sm_pier_1` onto both map-transport flags,
masters regenerated, both maps re-staged and re-baked, the automation tiers, the in-game witness.
Tests at the seam only: the pytest pins named above, `Elysium.Substrate.Water`/`WaterActor`/`Camera`,
the camera clearance-band tests in `Source/ElysiumUE/Private/Tests/ElysiumCameraTests.cpp`, and
`verify_water` in `bake_verify.py`. → lands: water that behaves like water on `sm_hub_1` and
`sm_pier_1`; roadmap 7.3 named R7.1 and marked landed.

**Open, named, not this task's boundary.** `export map --verify` still exits non-zero on both maps
on legacy per-map glass/prop-alpha checks a V2 bake can never satisfy (`bake_map_v2.py` authors no
per-map `MI_glass_*`) — pre-existing, unrelated to water. `test content` exits with 5 pre-existing
failures, none water-related (reflection captures, `ChangeLevelInputs`' pin count, `FanDuration`,
`RigCompose`, `SantaMonicaRain`'s missing V2 wet-cubemap MICs). The in-game witness
(`docs/architecture/water-architecture.md` §11) ran twice on 2026-09-04: the sewer surface draws,
reflects and refracts; `Feet` reads on the sewer floor and in the pier's ocean band; the pier's
invisible ocean drew as a `tools/toolsinvisible` sheet until `meshed_faces` honoured `noDraw` and
the map re-baked (five chunk meshes rebuilt); and the "underwater post-process does not fog"
finding of the first pass was two measurement artefacts (the underside surface's own depth, and the
engine merging same-material blendables by volume priority) — the fog applies, and on a 50 cm canal
it is invisible by the authored numbers. `Waist`/`Eyes` have no in-game witness until a deep
volume converts. `import models --maps <one map>` still replaces rather than merges the shared
staged-models manifest — a trap for the next scoped run, named but not fixed here.

**R7.2 — decals on the V2 lane (2026-09-03).** A decal is a *projection*, and Unreal makes that a
material domain, which is the one material property an instance cannot override. So the lane is
built around a second instance rather than a second blend mode: `M_V2_Decal` is re-cut as the
`MD_DeferredDecal` / `BLEND_Translucent` / DefaultLit projector master, every unit that is ever
projected stages `MI_<unit>_Decal` beside its surface `MI_<unit>`, and the three consumers — the
bake's `.decals` placement, an `isDecalSurface` face group's mesh slot, and the runtime's
`UElysiumDecalSubsystem::Lay` — all resolve that one twin by name. Contract in
`seam_map_material.md` → "Two instances", "`M_V2_Decal`", "The eight real unresolved families"
(owner call A) and the knob table; the map half in `seam_map_map.md` → "Import — materials
(R5.4)". Roadmap 3.13 closed as **extend**; roadmap 7.2's line rewritten.

**The master.** `BaseTexture.rgb × Color` → BaseColor, `.a × Alpha` → Opacity, a new
`Emissive`/`EmissiveScale` pair for the 3 `$selfillum` units, a new `Unlit` static switch for the
28 `unlitgeneric` ones (DefaultLit has no unlit model to fall back to, so the switch moves the
fogged base colour onto Emissive and zeroes BaseColor), the `(U, 1−V)` flip ported from the
retiring `make_decal_material.py`, R5.3's fog parameters unchanged. Roughness, Specular, Metallic
and Normal are deliberately unconnected — the wall keeps its own surface under the stain, which is
what a lightmapped `$decal` did. `UseVertexColor` is gone (a projected decal has no vertex colour)
and `DecalDepthOffset` is retired outright rather than relocated. `GRAPH_VERSION` 6 → 7.
A generator ordering fix landed with it: the blend mode is now set **before** the domain, because
a fresh `UMaterial` is `BLEND_Opaque` and every `set_editor_property` recompiles, so
domain-then-blend spent one intermediate compile in the invalid state and logged the engine's
*"Material using the DeferredDecal domain can only use the Blend Modes …"* warning for a master
that was about to be valid — the same line the retired `M_Decal` used to fail on, which is exactly
why it must not appear for a healthy one.

**Owner call A** — the 38 `decalmodulate` units draw as translucent decals; the divergence is "an
impact hole is a translucent stain", and under DBuffer the engine rewrites Modulate to Translucent
regardless. **Owner call B** — `UElysiumDecalSubsystem` shipped with its first caller: it adopts
every baked `elysium.decal` component at load and owns its fog MID (so `ApplySceneFog` reaches
every decal live — 3.13's *extend*), pools and caps runtime stains
(`UElysiumSurfaceSettings::MaxLaidDecals`, 2048 as a stated stand-in), answers the `DECALLIST`
shape, and the ranged shot's forward trace in `FElysiumWeapon::CommitQueuedAttack` lays the
`C_TEGunshotDecal` table's hole. The trace runs on `ECC_GameTraceChannel2` (`ElysiumPick`), not
`ECC_Visibility`: only the baked *render* geometry blocks that channel and only it carries the
face's `PM_<class>`, while the `.hulls` collider is material-less, so a visibility trace would stop
on a PLAYERCLIP volume and stain every surface concrete.

**Measured (2026-09-03, foreground, this machine).** `uv run elysium export bundle policy`:
`M_V2_Decal` compiles across 42 shadermap permutations with **zero** `LogMaterial: Warning` lines,
zero *"DeferredDecal domain"* warnings and zero `M_Decal` references anywhere in the log.
`uv run elysium import materials`: **19,709 imported, 0 reused, 0 pruned, 0 failed** (the master
rebuild reparents every instance in the corpus, so nothing is cached) over 19,121 units — **588
projector twins**, exactly 550 `$decal` + 38 `decalmodulate`, of which 34 carry `Unlit`, 3 carry
`Emissive`, and **23 bind no `BaseTexture`** (the debug/wireframe families that author `$decal` and
take no master; referenced by no `.decals` line and no face group in the 108-map export, staged
because the rule is the unit's own authored key). The 4 stage failures (`dev/ocean`,
`dev/oceanbeneath`, `envmap/gioint`, `skybox/hav_env`) are pre-existing and unrelated.
`uv run elysium export map` on the three working maps: decal actors **123 / 38 / 219**, unchanged;
`materials: 0 built / 0 reused / 27 pruned`, `0 / 0 / 14`, `0 / 2 / 61` — nothing authored on
either lane, the legacy per-map `M_Decal` MICs pruned, and both `/ElysiumBaked/<map>/Materials` and
`.../Materials/Decals` left empty on all three (the hub's 2 reused are its weather rain instances).
All 380 `.decals` lines over 97 distinct materials resolved a twin — a miss is the named failure
`decal material has no projector instance:`, and none fired. `isDecalSurface` face groups on the
projector instance: **0 / 3 / 4** (`decals/signs/number{0,5,8}` are the pawnshop door numbers),
each with `opaque` false in the staged row, so the Nanite split leaves them in a plain section for
the mesh-decal pass. Booted all three (`uv run elysium debug probe`): `adopted 123 | 38 | 219 baked
decal(s)` and the same count of decal MIDs on the fog line, zero `Default Material will be used`
and zero `LogMaterial: Warning` lines in any of the three logs.

**Tests.** `uv run elysium test Policy` 10 of 10 executed, 10 succeeded, 0 failed (including
`Elysium.Policy.V2MasterParams`, grown by the master's new pins). `uv run elysium test Substrate`
455 of 455 executed, 399 succeeded + 56 with warnings, 0 failed, 0 notRun — the seven decal leaves
are `DecalLay`, `DecalLifetime`, `DecalCap`, `DecalRecords`, `DecalImpactTrace`, `FogDecalMID` and
the pre-existing `Decals`. `uv run elysium test Elysium.Content.TutorialDecals` 1 of 1, succeeded.
`uv run pytest` **3,255 passed, 0 failed**. `uv run elysium doctor`: repository policy passed
(29 pre-existing export warnings). One red test predating this work was fixed rather than carried:
`test_make_surface_knobs.py::test_default_ini_matches_header_field_initializers` could not parse
R6.3's `DetailSwayAmplitude = 5.0f * 2.54f`, because its header regex only accepted a bare literal;
it now evaluates a product of float literals (and rounds both sides to six decimals, since the two
are the same `float` at run time and differ in Python's last double bit), which is the form a knob
stated in source units should keep.

**Retired with it:** `make_decal_material.py`, `M_Decal.uasset`, its `POLICY_GENERATOR_OUTPUTS`
row and `build_content.GENERATORS` step, the legacy `("decal", "M_Decal")` master rule and every
decal branch under it, the V2 lane's last `.mtl` read (`decal_mats`), the per-map `Materials/Decals`
package on both lanes, and `DecalDepthOffset` from the settings header, the binding table,
`Config/DefaultElysium.ini`, the MPC scalar list and the knob test. No live `M_Decal` reference
remains in `pipeline/` or `Source/`.

**Adversarial review, same day — four fixes and one measurement.** (1) **The shot's ray was
mirrored.** `FElysiumWeapon::TraceShotImpact` built its forward vector as
`FRotator(Angles.X, Angles.Y, Angles.Z).Vector()`, but `Angles` is Source QAngles while `Origin`
is an Unreal world position: the handedness reflection reverses pitch and yaw, which is exactly
why the player writes `Angles = ElysiumPlayerView::ToSource(View)` and every motor call passes
`-Angles.Y`. A shot fired at Unreal yaw 90° traced towards −90° and stained the wrong wall; only a
trace that meets real geometry can show it, and the probe harness fires nothing. Now
`ElysiumPlayerView::ToUnreal(Attacker.Angles).Vector()` — the same frame
`AElysiumMapActor::QueryAimTarget` picks the victim along, so the mark and the target come off one
aim. *(The pre-existing `AcquireMeleeOpponent` and `ElysiumDisciplines::AcquirePrimary` cones carry
the same raw expression; they are the substrate lane's and untouched here, and no test pins their
sign because every one of them stages yaw 0.)* (2) **`bake_verify._material_slot` still named the
retired per-map `Materials/Decals` package** for a `$decal` surface, which now contradicts both
lanes' `_material_sets`; it returns the map's one `Materials` package. (3) The stage's claim that
*"the corpus carries no patched `$decal` unit"* is **false** — there are 10 (`glass/libwndwf` on
`hw_warrens_5`/`la_library_1`, `objects/blastdoortrim` on `la_library_1`). Reading
`isDecalSurface`/`decalAsset` off the root sidecar makes them bind the root's twin, which loses
nothing: measured over the whole 19,709-entry manifest, the only non-empty patched delta anywhere
is `WaterDepth` (49 water units), and VBSP's patch is `$envmap`, for which `M_V2_Decal` has no pin.
Both comments now state the measurement. (4) **The impact table's `flesh` rows are unreachable from
the world trace**, and the subsystem header said the opposite. Only `ElysiumPickOnly` (baked render
geometry) and `ElysiumPropSolid` block `ECC_GameTraceChannel2`; an NPC capsule is `Pawn`-profiled,
its mesh `NoCollision`, and a brush entity sets `ECR_Ignore` — so a shot at a body marks the wall
behind it, which is what `CommitQueuedAttack` already says the mark is for, and `flesh/blood` /
`flesh/soak` are reached only by a caller that knows the surface without tracing (the gib blood and
the soak column, R7.3). Also corrected: `ElysiumFog.h`'s "parented to the shared imported
`MI_<unit>`" (it is the projector twin, on a subsystem-owned MID) and `uasset-bake-spike.md`'s two
`M_Decal` / `Materials/Decals` rows.

**Follow-ups.** The 36 unmatched `infodecal` entities (9 maps, 15 of them `hw_609_1`) still want a
wider plane-distance search and a re-measure — never a guessed face. The 105 unconverted maps still
hold their stale per-map `Materials/Decals` MICs on the mount; each is pruned by that map's own next
bake, and their parent `M_Decal` no longer exists. `MaxLaidDecals` stays 2048 as a stated stand-in
until `r_decals`' default is read off `engine.dll` `staticinit_2007af40`. `Records()`/`Restore()`
round-trip but nothing writes them into the save's reserved `Maps` block yet
(`save-architecture.md` → "Decals as save state"). **Looked at, same day:** the `debug shots`
vantages before/after (`E:/elysium-work/scratch/decals/pawnshop_*_before_after.png`,
`hub_*_before_after.png`) — the pawnshop corridor's door numbers `507`/`503` read upright and
unmirrored on the projector lane; the hub's own vantages face the haven bum and show no decal —
and two in-game frames through the editor MCP after `elysium.dlg.choose` closed his dialogue:
`scratch/decals/hub_sntgaragee.png` (the `1E` pillar sign, the `Main Street` / `2nd Street`
signs and the band posters, every one a `.decals` projector, lit as its wall, text the right way
round) and `hub_parkingb_floor.png` (the floor marking sits under a parked car; inconclusive).
The pawnshop captures also show the ceiling-lamp coronas blown out to white discs against the
2026-09-02 frames — **not this change**: `UElysiumSpriteComponent` still writes its colour as
vertex colour (`ElysiumSpriteComponent.cpp` 193–206) while `4313d0ec` moved the sprite masters
to `ParticleColor`; R7.3's follow-up. Still open: a hand-placed `hits/concrete/impact3` on
`sp_soc_4` once it converts, and **no shot has been fired in a real map yet** — the ray fix above
is reasoned from the frame contract and covered by `Elysium.Substrate.DecalImpactTrace`'s own
world, not by a bullet hole anyone has looked at.


## Roadmap — one pipeline

The single track. The surfaces and maps plans merged here (2026-08-31, owner: "consolidate — not
two disconnected paths, they both interchange"); old `SF-`/`MP-` ids are kept in brackets where a
task moved. Governing principle: **the Unreal editor is the tuning surface** — the bake writes
final values, runtime applies state, never taste; anything tunable gets a settings page, a data-
asset grid, or a direct actor edit, and nothing else. Cutover is gated per map, never per system.
Each task is one small deliverable; a landed task moves its result to Settled and comes off this
list.

**No save-file compatibility at build time (owner, 2026-09-01).** Old snapshots are disposable
until the pipeline is one; no task spends effort on `FElysiumSaveVersion` gates, refusal paths or
index remapping for saves that predate it. Entity order = lump order remains a rule only because
the index is the running game's own handle; it owes nothing to yesterday's save.

**Wire first, tune later (owner, 2026-08-31).** This roadmap makes things go live — appear,
move, work. Exposure, brightness, look-tuning of any kind are **not in it**: nothing can be
judged until everything is live, so every knob and settings surface built here ships with the
faithful VtMB-derived value and is left alone. Agents never read screenshots to tune — shots are
a did-it-appear / did-it-regress witness only. The tuning sessions (the old SF-5.x lookdev pass
and the real-map second pass) happen after the roadmap, on the editor surfaces it builds.

**Landed and closed (was surfaces Phases 1–5):** textures including the 1,325 PAKFILE probes;
surface properties (63 `PM_`); the nine V2 masters and 19,121 `MI_` with provenance and
idempotency; the 16-knob settings page + `DA_SurfaceCalibration`; the lookdev map. Full record in
the Settled entries above. The owner's tuning sessions (was SF-5.1–5.3 and SF-6.7) are
deliberately **after** this roadmap — see "Wire first, tune later" below.

### R1 — props (the models lane; first, everything downstream places these)

**R1.1–R1.4 landed (2026-08-31)** — contract (`afe67526`), validator visibility (`b66262f5`),
stage (`4218c32d`), C++ provenance/settings (`12339a27`), editor import (`5416ba8f`); numbers in
the Settled entry "Props lane, R1.1–R1.4 landed on the test corpus". Working corpus for the whole
of R1 is `sp_tutorial_1`, `sm_pawnshop_1`, `sm_hub_1` (414 models); the full 3,661-unit run is a
separately approved step, still pending.

**R1.5–R1.6 landed (2026-08-31)** — skins asset (`15a3fb28`), verify + lookdev (`ab550d12`), review
fix for the collision auto-detection divergence the verify step's own test found (same day);
numbers in the Settled entries "Props lane, R1.5–R1.6 landed on the test corpus" and "Collision
auto-detection divergence found and fixed same day". R1 is done; R2 is next.

### R2 — instruments and guards (nothing else moves first) [MP-1]

**R2 landed** — see Settled.

### R3 — map producer parity (game untouched) [MP-2]

**R3 landed** — see Settled.

### R4 — transport: assets instead of loose files [MP-3]

**R4 landed** — see Settled.

### R5 — the map bake rebuilt on the GLB corpus [MP-4]

**R5 landed** — see Settled.

### R6 — wiring: what is already understood goes live on the V2 lane [was R6.3–6.4 in part, R7.1, R7.3, R7.4, R7.7]

**R6 landed** — see Settled.

### R7 — design: the families with a choice to make first [was R7.2, R7.5, R7.6, R6.2 remainder]

Each task opens with its ruling, written into the owning seam doc before code; exploration is
allowed the way `effects-architecture.md` §6 says (scratch folder, no bake, no game code), then
wiring. Independent of R8; **order is R6 → R7 → R8** (owner call, 2026-09-02): the visible world
upgrades land first and the design questions are settled while the map work is fresh, at the
cost of the biggest rewrite and the retire stage waiting behind them.

- **R7.1 Water** [R7.2 / MP-5.2]. **Ruled and landed, 2026-09-04** — Settled below as
  "R7.1 — water on the V2 lane". `M_V2_Water` is re-cut as a Single Layer Water master (SLW
  answers `$reflecttexture`, `$refracttexture` and the in-volume fog in one place, ruling A);
  `water.volumes[]` is a new stage product joining every real `LEAFWATERDATA` row to its
  `CONTENTS_WATER` brushes (ruling B); one `AElysiumWaterVolumes` actor per map is bake-placed
  and classifies feet/waist/eyes pre-move (ruling C); underwater is a post-process linear fog the
  actor registers as an `IInterface_PostProcessVolume` (ruling D); `$bottommaterial` becomes the
  `Underside` switch (ruling E); `leafMinDist[]` stays provenance-only, never loaded by the engine
  (ruling G). The "22 maps" count and the `leafMinDist`-drives-underwater premise are both
  retracted — see the Settled entry's "What the ruling retracts".
- **R7.2 Decals** [R7.6 / MP-5.6] — **reviewed on the real census, 2026-09-03; two owner calls
  open, everything else forced.** Why it was deferred: R5.3 planned the decal rebind onto an MID
  over the shared `MI_<unit>`, and R5.4 found (a) a `UDecalComponent` draws only an
  `MD_DeferredDecal` material (`FDeferredDecalProxy` substitutes the engine default for anything
  else — `DecalComponent.cpp` 12–19, 58–60) while every V2 master is `MD_Surface`, and (b)
  `M_V2_Decal` (Unlit, `BLEND_Modulate`) was built for the 38 `decalmodulate` units, but the
  materials the `.decals` sidecars name are `$decal` surfaces on `M_V2_LitTranslucent`. The
  projector lane and the modulate family had been conflated, so the bake kept the legacy per-map
  `M_Decal` MICs — the one per-map material package a converted map still authors — and the
  design moved here.
  **Census (108-map export, `E:/elysium-work/exports`, provenance under `import/materials`).**
  5,143 `infodecal` entities → 5,095 projector lines on 92 maps (36 unmatched — no face within
  64 units or outside every candidate polygon, on 9 maps, 15 of them `hw_609_1`; 12 skipped for
  a missing `origin`/`texture`), 473 distinct materials, all resolving: lightmappedgeneric 426
  (3,599 lines), unlitgeneric 28 (1,255), vertexlitgeneric 9 (157, the `*_model` blood variants),
  decalmodulate 10 (84). Of the 550 `$decal 1` units, 448 are projector-only, 15 are both a
  projector and a real `usemtl` world face, 9 are world-face only, 78 are unused; 53 of the 473
  projector materials are ordinary world paths (`carpet/malkrugb`, `signs/exit`, …) reused as
  projectors, all `$decal`. **The 38 `decalmodulate` units are exactly the runtime impact set**
  — `decals/hits/{concrete,metal,wood,glass,flesh}/*` (the `C_TEGunshotDecal` table's
  surface × 5, `soak1-5` for the soak column; the `scorch` column has no material in the corpus)
  plus `decals/break1-3` — none carries `$decal`, all carry `$decalscale`; 10 of them are also
  hand-placed as `infodecal`, 25 appear in no map at all. No `vdecal_*` material exists in the
  corpus or the packs and the collide key has 0 placed uses; `collide { decal { particle } }`
  (16 roots / 174 placements) lays a *particle's sprite* as a decal. VtMB's other runtime decal
  producers are the client temp entities `C_TEGunshotDecal`, `C_TEPlayerDecal`,
  `C_TEFootprintDecal`, `C_TEDecal`, `C_TEWorldDecal`, `C_TEBSPDecal`, all gated by `r_decals`
  (`engine.dll` `201a13d4`; the cap's default is in `staticinit_2007af40`, not yet read), and the
  save game persists them as `DECALLIST` (`savegame_format.md`). Decal VMTs use 18 keys in total;
  the canonical unit is `$basetexture $translucent 1 $decal 1 $decalscale 0.25`; `$selfillum` on
  3, `$additive` on 1, `$alphatest` on 1.
  **Engine facts (5.8 source, verified).** A deferred-decal material may blend Translucent,
  AlphaComposite or Modulate (`MaterialShared.cpp` 6492); on a DBuffer platform — `r.DBuffer` is
  1 and the project does not override it — Modulate is rewritten to Translucent
  (`DecalRenderingCommon.cpp` 47–49), and an Emissive pin goes through its own pass after the
  base pass (93–95). DBuffer decals write BaseColor / Normal / Roughness before the base pass,
  the receiver gated by `MaterialDecalResponse` (default ColorNormalRoughness on every generated
  master), Nanite receives through the `RECEIVE_DECAL` stencil bit, and the decal is then lit
  by Lumen and MegaLights exactly like the wall — the modern twin of Source's lightmapped
  `$decal` face. Decals are not captured into the Lumen surface cache (no loss: a VtMB decal
  never bounced either). A non-Nanite static-mesh section whose material is decal-domain draws
  in the **mesh-decal** pass (`PostProcessMeshDecals.cpp` 255), the native answer to Source's
  `$decal` polygon offset. `UGameplayStatics::SpawnDecalAtLocation/Attached(LifeSpan)`,
  `UDecalComponent::SetFadeOut` and the `DecalLifetimeOpacity` node are the runtime tools. The
  legacy `M_Decal` failure is *"Material using the DeferredDecal domain can only use the Blend
  Modes Translucent, AlphaComposite(Premultiplied Alpha), or Modulate"* — a warning, so every
  legacy decal has been drawing `WorldGridMaterial` since the 2026-08-31 package move; the
  legacy generator never checks `recompile_material`'s errors, `make_v2_materials.py` does.
  **Rulings (forced by the facts above, recorded as such).** (1) **One decal master,
  `M_V2_Decal`, re-cut as `MD_DeferredDecal` / `BLEND_Translucent` / DefaultLit**: `BaseTexture`
  RGB → BaseColor, A → Opacity, `Color`, `Alpha`, `Emissive` + `EmissiveScale` (the 3
  `$selfillum` units), an `Unlit` static switch (the 28 UnlitGeneric units: Emissive = texture,
  BaseColor unwritten), the `(U, 1−V)` flip the legacy graph carries, and the R5.3 fog home
  (`FogColor`/`FogStart`/`FogInvRange`, `mat_fog.fog_from_params`) unchanged. Roughness,
  Specular, Metallic and Normal are **not** connected: the wall keeps its own surface under the
  decal, which is what a lightmapped `$decal` did. Nine masters stay nine. (2) **Every
  `$decal` unit and every `decalmodulate` unit stages a second shared instance,
  `MI_<unit>_Decal`, parented to the decal master** (`/ElysiumBaked/Materials/Decals`, ~590
  instances), alongside the surface `MI_<unit>` the 24 world-face units still need; the
  materials table carries `decalAsset` beside `asset`. `_place_decals` binds `MI_<unit>_Decal`
  by `vtmb:material` id (a missing one is a named failure, never the error material); the
  per-map `Materials/Decals` package, the V2 lane's last `.mtl` read (`decal_mats`),
  `make_decal_material.py`, its `export_manager` row and `M_Decal` retire. (3) **The
  `isDecalSurface` face groups bind `MI_<unit>_Decal` as their mesh slot and draw as mesh
  decals** — non-Nanite (the master sets no `used_with_nanite`, and the Nanite split already
  follows the bound master, R5.4's review fix), coplanar with the wall, no z-fight, lit as the
  wall. This is the first consumer of the `isDecalSurface` provenance flag; the
  `DecalDepthOffset` knob (`UElysiumSurfaceSettings`, `MPC_ElysiumSurfaces`, the knob test)
  retires with nothing to bias. (4) **Fog is a load-time MID, owned by the decal subsystem (B)**: a bake
  cannot save a `UMaterialInstanceDynamic` into the level, so at map load the subsystem adopts
  every `elysium.decal` actor, parents an MID to the bound instance and calls
  `ElysiumFog::ApplyToDecalMID`; `ApplySceneFog` re-stamps through the same owner — the R5.3
  ruling landing where it always had to, and roadmap 3.13 ("decal fog: accept or extend")
  closes as *extend*: a fog change now reaches every decal live, baked or laid.
  (5) **Projector geometry stays the exporter's**: one nearest face within 64 units, the room
  side from leaf solidity, sort order = line order, `FadeScreenSize 0`; the component's 16 cm
  reach reproduces `R_DecalShoot`'s spread across neighbouring faces natively (already the
  accepted modernization). The `LowPriority` spawnflag is dropped (it only governs replacement
  under the cap). The 36 unmatched decals are a follow-up: widen the plane-distance search and
  re-measure, never guess a face.
  **Owner call A — the 38 `decalmodulate` units draw as translucent decals.** Retail drew them
  as wireframe (the shader is absent from `stdshader_dx8.dll`), R5 chose Modulate as the
  stand-in, and under DBuffer the engine would rewrite Modulate to Translucent regardless. The
  divergence is "an impact hole is a translucent stain", named beside the family in
  `seam_map_material.md`; the alternative (turn `r.DBuffer` off to keep a true modulate) trades
  Nanite decal receiving and the emissive pass for 38 materials that never drew, and is
  rejected unless the owner wants it. **Owner call B (recommended: yes) — `UElysiumDecalSubsystem` ships in R7.2
  as the one owner of every decal in a world, with its first real caller.** A `UWorldSubsystem`:
  (a) adopts the baked `elysium.decal` actors at load and owns their fog MIDs (ruling 4);
  (b) `Lay(FElysiumDecalRequest{material id or texture, location, normal, half-size, lifetime,
  attach component})` → a `UDecalComponent` from a pool on one hidden actor, bound to
  `MI_<unit>_Decal` (or an MID off the master with `BaseTexture` bound, for a collide sprite),
  fog applied, oriented from the hit normal with the surface tangent the way `_place_decals`
  does, `SortOrder` from a running serial so later stains layer over earlier ones as
  `R_DecalCreate`'s list does; (c) a cap (`UElysiumSurfaceSettings.MaxLaidDecals`, VtMB's
  `r_decals` default once read off `staticinit_2007af40`) with oldest-first recycling;
  (d) `Records()` — the `DECALLIST` shape (material, position, normal, `saveentityindex`) for
  the save's reserved `Maps` slot, re-laid on load through the same `Lay`. The Niagara decal
  renderer is **not** used: a laid decal outlives its particle. **Callers landed with it**, not
  after it: the ranged shot's forward world trace — the SEAM `ElysiumWeaponClasses.h` names
  ("retail traces forward first and accepts a valid obstruction hit") — becomes an engine line
  trace whose `FHitResult` physical material is the imported `UElysiumPhysicalMaterial`, whose
  surface character indexes the `C_TEGunshotDecal` table (`effects.md` §3.5: surface × weapon
  column → `decals/hits/<surface>/*`, `scorch` empty) and lays the hole; the R7.3 A2 collide
  archetype calls `Lay` with the particle's sprite (174 placements). The gib blood waits only
  because `env_shooter` itself is R7.3's explosion slice; it calls the same `Lay`. The
  alternative — leave the seam to its first caller — is the pattern this note keeps paying
  for (R5.3 → R5.4 → R7.6 → R7.2) and is rejected.
  **Order of work.** (i) rulings into `seam_map_material.md` ("`M_V2_Decal`", "The eight real
  unresolved families", "Decal fog and wetness homes", the knob table) and `seam_map_map.md`
  ("Import — materials (R5.4)", the decal placement note); (ii) `make_v2_materials.py`
  `_build_decal` re-cut + `DECAL_PARAM_TABLE`, `ElysiumSurfaceParamsDecal`, the three-way pin
  and `Elysium.Policy.V2MasterParams`; (iii) `importers/materials.py` — the second instance
  for `$decal` / `decalmodulate`, `map_geometry.py`'s `decalAsset` column,
  `materials_report.json`'s `decal` class meaning "projector instance bound"; (iv)
  `bake_map_v2.py` — `_place_decals` by id, the empty decal set, `isDecalSurface` groups on the
  projector instance, the per-map `Decals` package pruned; (v) runtime — the subsystem (B: adopt + fog,
  `Lay`, pool + cap, records), the ranged world trace and its impact-table caller, the A2 hook,
  `DecalDepthOffset` deleted; (vi) retire `M_Decal` and its
  generator, `uv run elysium export bundle policy`; (vii) re-stage and re-bake the three
  working maps, then a contact sheet the way `effects_authoring.md` does it (pawnshop door
  numbers, hub blood on the floor, a hand-placed `hits/concrete/impact3` on `sp_soc_4` once it
  converts). Tests at the seam only: the pytest pins for the master's parameters and the
  two-instance rule, `Elysium.Policy.V2MasterParams` grown by the master, one Substrate test on
  `Lay` (orientation, lifetime, cap recycling, the record round-trip) and one on the ranged
  trace laying the table's hole, `Elysium.Content.TutorialDecals` unchanged. **Measured** = decal
  actor counts per map unchanged (123 / 38 / 219 on the three), zero per-map material packages,
  zero `M_Decal` references, the 36 unmatched re-counted. → lands: decals on the V2 lane, the
  legacy `M_Decal` retired, roadmap 3.13 closed, bullet holes in the world, the stain seam live.
  **Landed (2026-09-03), Settled above as "R7.2 — decals on the V2 lane".** **Measured:**
  `M_V2_Decal` compiles with zero errors and zero warnings across 42 permutations and the log
  carries no `M_Decal` reference; the material stage names **588** projector twins (550 `$decal` +
  38 `decalmodulate`; 34 `Unlit`, 3 `Emissive`, 23 with no `BaseTexture` — the debug families that
  author `$decal` and take no master), and `uv run elysium import materials` reports
  *19,709 imported, 0 reused, 0 pruned, 0 failed* (the master rebuild reparents the whole corpus)
  beside the 4 pre-existing stage failures; the three working maps re-bake to **123 / 38 / 219**
  decal actors, unchanged, with `materials: 0 built / 0 reused / 27 | 14 | 61 pruned` — zero
  per-map material packages authored on either lane — every one of the 380 `.decals` lines (97
  distinct materials) bound to a `_Decal` twin, and **0 / 3 / 4** `isDecalSurface` face groups on
  the projector instance with `opaque` false, outside the Nanite buckets. All three boot: *adopted
  123 | 38 | 219 baked decal(s)*, the same count of decal MIDs on the fog line, no material
  fallback logged. The 36 unmatched `infodecal` entities are unchanged and stay a follow-up.
  **Tests:** `Elysium.Policy` 10/10 (`V2MasterParams` grown by the master), `Elysium.Substrate`
  455/455 with the six new decal leaves, `Elysium.Content.TutorialDecals` 1/1,
  `uv run pytest` 3,255 passed, `uv run elysium doctor` passed. Frames: the before/after
  `debug shots` composites and the two hub MCP captures under `E:/elysium-work/scratch/decals/`
  (garage signs and posters draw as projected decals; the corona blow-out in the same frames is
  R7.3's `ParticleColor` change, not this lane's).
- **R7.3 Effects families** [R7.5 / MP-5.5] — **on the real census, ruled 2026-09-02**
  (`effects-architecture.md` §5). R2.2's vocabulary was a guess: `env_fire`, `env_embers`,
  `env_lightglow`, `point_spotlight`, `env_sun` have **0** placements in 108 maps. What the
  corpus places: `env_particle` 1,304 over 155 roots (class exists; the legacy Niagara flatten
  fails the four most-placed fire roots), `func_particle` 98 (stub; a brush AABB emitter with a
  volume scalar, no rain-specific body), `func_dustmotes` 82, `env_beam` 47, `env_steam` 11 (no
  class), `params_particle` 227 (**a precache stub** — the dialog auras are created by name from
  the discipline walker, nothing to build beyond an inert class), and the explosion bundle —
  `point_explosion` 93, `params_explosion` 52 (an inert data holder), `env_physexplosion` 61,
  `env_physimpact` 130, `env_shake` 60, `env_shooter` 21 (stubs; the impulse is
  `physics-architecture.md`'s seam). `env_particle_hud` (3) and the main-menu particle scene are
  **out of scope by owner decision** (the menu and HUD are new authored assets). The rulings:
  A1 stage off the V2 particle unit (`seam_map_map.md` → "Import — effects (R7.3)": `effects[]`
  + `particleTrees{}` + the dust / steam / beam rows), **B3 (revised 2026-09-03)** one Niagara
  system generated per placed root, `NS_<root>`, written by the bake into the gitignored
  `Content/ElysiumGenerated/VFX/` and composed headlessly by `UElysiumParticleAssetBuilder` from
  hand-authored base emitters under `Content/ElysiumAuthored/VFX/Base/` — one emitter per drawing
  leaf, the leaf's numbers, curves, sprite and child relations written as parameter values, the
  runtime writing system-level user parameters only — with `DA_EffectFamilies` overrides
  unchanged, C1 one `AElysiumEffectActor` per row in the baked level, the real family systems
  `NS_ElysiumDust` / `NS_ElysiumSteam` / `NS_ElysiumBeam`, P1 the two impulse methods with the
  explosion bundle; the ambient set first (stage product, base emitters, the generator, effect
  actor, `env_particle` retargeted, `func_particle`, dust / steam / beam on the three working
  maps), then the explosion bundle on `sm_junkyard_1` / `sm_warehouse_1`. The first cut of B3 was
  a single 20-slot floor asset `NS_ElysiumParticle`; it never compiled outside the Niagara
  editor, needed per-instance emitter readers the engine forbids, and dropped leaves on its fixed
  slot layout, so work was **paused 2026-09-02** pending a strategy
  (`niagara_authoring_strategy.md`) and **re-ruled 2026-09-03** to the generated lane above, with
  a per-archetype owner verdict recorded in `effects_authoring.md` before an archetype is scaled
  to the corpus.
  **State on 2026-09-03 (end of day).** Landed on the generated lane: the headless generator
  (`make_root_systems.py` + `UElysiumParticleAssetBuilder::BuildRootSystem`, compile +
  `IsReadyToRun` gate, `Content/ElysiumGenerated/VFX/NS_<root>`), the first base emitter
  `Content/ElysiumAuthored/VFX/Base/E_VtMBLeaf` (all stock modules; VtMB ramps as two curves
  lerped by the particle's material random; a `DynamicMaterialParameters` lane for `refract`),
  the effect actor binding `NS_<root>` when the package exists and the floor otherwise, and the
  one approved archetype, A1 `BarrelFireEmitter` (three owner review rounds, sheets under
  `E:/elysium-work/scratch/effects/sheets/`). Two material-lane defects fell out of the review:
  Niagara's sprite vertex factory hardcodes `VertexColor` to white, so the three sprite masters
  now read `ParticleColor` (this was the in-game "black card" and the hard-edged authored steam),
  and `M_V2_Refract` is now a DUDV offset with a per-particle strength. **Missing:** the other
  eight archetypes (A5 steam plume, A8 timer chain, A2 drip with collide → splash, A3
  precipitation, A6 moth, A9 muzzle flash, A10 one-shot casts, A7 airplane carrier — order and
  representative roots in `effects_authoring.md`); in the generator, the spherical-offset motion
  (`theta`/`phi` spawn angles, `radius_speed`/`theta_speed`/`phi_speed`, needed from A5 on),
  `collide → spawn` as Collision + collision events (A2), `spawn{rate|burst}` children through
  the emitter-level attribute reader (A7; the module is added, unverified), a Collision module
  on the base emitter, and the particle-count gate (`ProbeSystem` cannot build a system instance
  in a commandlet); the generator is not yet a bake stage (run by hand from the staged document
  at `E:/elysium-work/scratch/effects/staged.json`, which the census agent staged in memory — the
  map producer does not write `particleTrees{}` to disk); the in-game half of the contact sheet;
  the world refract/heatglow brushes after the `M_V2_Refract` mode change (unverified); the
  authored `NS_ElysiumSteam` re-tuned on the corrected masters; `NS_ElysiumParticle` and the ~200
  lines of slot fitting in `AElysiumEffectActor::WriteTree` deleted once no map falls back to
  them; the seam test over every generated system (`effects-architecture.md` §5.3 gate 1); the
  Measured lines and the Settled entry. **Next steps, in order:** (1) owner looks at the hub
  barrels in game and confirms A1; (2) A5 `SteamRelease_Constant_Emitter` — the spherical-offset
  module on `E_VtMBLeaf`, one generated system, sheet, verdict; (3) A8, then A2 (the collision
  event seam, 41 placements on the working maps); (4) wire `make_root_systems.py` into the V2
  bake behind the staged `particleTrees{}` and add the per-system seam test; (5) A3, A6, A9,
  A10, A7; (6) scale to the three maps, play run, retire the floor, Measured + Settled.
  → lands: the ambient set and the explosion bundle.
- **R7.4 Runtime material binds** [R6.2 remainder / SF-6.4]. Through R6.5's `Create(MI_)`:
  `textconsole` (4) with `func_monitor` 22 / `point_camera` 37 / `security_camera` 18 as
  render-target screens; `breakablesurface` (2) + `$crackmaterial` with `func_breakable_surf`
  130 (shatter); `shadow` (2); `playerproximity` 3 / `playerposition` 4 / `playerspeed` 6;
  `lessorequal` 4. Reflect/refract go with R7.1; the obfuscate noise chains are R8's. → lands:
  screens, shatter, player-driven surfaces.
- **R7.5 `func_areaportalwindow` distance cover** [owner call surfaced by R6.4, 2026-09-02].
  R6.4 landed `func_lod`'s cull range and gave `func_areaportalwindow` a class, but wrote no
  distance for it: all 246 corpus rows (13 on the working maps) are **point** rows, and their
  `FadeStartDist`/`FadeDist` govern the black `target` backing brush whose mesh the exporter omits
  by the standing ruling in `docs/vtmb/entity_io.md` ("Elysium does not reproduce this
  distance-faded PVS cover"), while `BackgroundBModel` is the foreground glass VtMB keeps fully
  drawn — culling *that* at `FadeDist` would invert the behaviour. **The choice:** (a) keep the
  standing divergence — windows stay open at every distance, the exterior shows through, and the
  class stays a record of its numbers; or (b) re-mesh the backing (drop `visibility_backing_models`
  from the producer join, which changes the R5.1 parity counts by 5 / 0 / 8 brush models) and give
  it `MinDrawDistance = FadeDist × 2.54` — transparent within `FadeStartDist`, black beyond
  `FadeDist`, VtMB's two end states without the `TranslucencyLimit` 0.2 near-blend — so distance
  windows read black from afar as in 2004, at the cost of one black brush per window standing
  in Lumen's scene. Nothing else in R6/R7 depends on the answer.
- **R7.6 Detail sway amplitude** [owner call surfaced by R6.3, 2026-09-02]. R6.3 wired the sway
  the owner asked for — `swayAmount / 255` per instance into one World Position Offset term on
  `M_V2_Lit`/`M_V2_LitTranslucent`/`M_V2_Unlit`, on the shared `Time` clock — and found that
  **VtMB's own client never reads the byte**: `CDetailModel` in `client.dll` is five functions
  (construct, destroy, the lighting product and a draw-colour multiply, `100e0250`…`100e0300`),
  `CDetailObjectSystem::vfunc10` (`100e0d90`) registers only `cl_detaildist`/`cl_detailfade`, and no
  sine or sway cvar exists in the detail path. VBSP authored the byte (35,521 non-zero records)
  for a feature this engine build shipped without, so there is no VtMB amplitude to transcribe.
  The shipped default is the first Source build's that did read it — `swayAmount / 255 ×
  cl_detail_max_sway`, Valve's shipped 5 world units, **12.7 cm** on the Surfaces page
  (`DetailSwayAmplitude`), with the base pinned and the tip moving and Source's per-object phase
  `(x + y)` in inches. **The choice:** keep Source's 5 units (the weeds move as a 2007 Source map's
  would), set another number once the `sm_hub_1` weeds are seen live, or 0 (the byte carried, the
  term compiled, nothing moving — VtMB's own 2004 behaviour). Nothing else depends on the answer;
  it is one settings field, no rebake.

- **R7.7 Depth-tested plain sprites** [owner call surfaced by R6.1, 2026-09-02]. R6.1 draws
  every `env_sprite` on the imported `MI_` of `M_V2_Sprite`, and that master is
  **depth-test-off** by the material lane's ruling (`$ignorez`, material-only, never a
  per-instance override). Faithful for the coronas (Source's `kRenderGlow` never depth-tests; the
  pixel-visibility query is what hides a halo behind a wall), wrong for the plain cards — modes
  5 (Additive: `volumelight*` shafts 1,552 corpus-wide, `candle`, `lightning*`) and 1 (Color: 472)
  are depth-tested in VtMB. R6.1's shipped rule gates every sprite by the same occlusion query, so
  a shaft behind a wall does not draw through it, but a card half behind a table fades as a whole
  instead of being clipped. **The choice:** (a) keep the query gating for the plain modes (no
  draw-through, whole-card fading, nothing new in the material lane); or (b) give the material
  lane a depth-tested twin of the master (`M_V2_SpriteZ`, `bDisableDepthTest` off, the same graph)
  and have the bake parent the mode-1/5 children to it — VtMB's exact clipping at the cost of a
  second sprite master and one more row in the master inventory. Nothing else depends on the
  answer; the query stays either way. Whichever way it goes, the sprite master's missing fog term
  (R6.1/R6.7 follow-up: a miniature sprite is drawn unfogged) rides on the same edit.
- **R7.8 The menu seal's source art** [owner call surfaced by R6.6, 2026-09-02]. VtMB's front-end
  seals are the menu particle scene's `particles/mm_<clan>.tga` sprites (15 of them: the seven
  playable clans, the Camarilla ankh `mm_cam`, and the sects and hunter factions) — loose TGAs
  under `particles/`, which no lane publishes: the texture lane's units are `materials/**`
  `.tth/.ttz` pairs. R6.6 retired the PNG read and draws the rail's watermark from the same clan's
  `interface/charactermaintenance/cm_clan_symbol_<clan>` `T_` — the sigil the character sheet
  already flies — and **nothing in the front end**, where no character exists and the ankh has
  no material twin. **The choice:** (a) keep that (one art path per clan, no new unit family, the
  front end unwatermarked); or (b) admit `particles/*.tga` as texture units so the fifteen
  `mm_*` sprites — and the blood cels and glow the same scene draws — import as `T_` and the rail
  flies VtMB's own seal, `mm_cam` in the front end, at the cost of a new source kind in the
  texture producer (`seam_map_texture.md` → "Texture unit") and its provenance. Nothing else
  depends on the answer.

**Owned elsewhere, not deferred.** Two groups the census surfaces belong to other plans and are
named here so they are not lost: the unread audio products (`audio/maps/*.json`, `schemes.json`,
`entity_events.json`) and the `ambient_soundscheme` stub (179) are `plans/audio.md`'s; gameplay
and AI classes with no leaf (`prop_haunted` 145, `prop_ragdoll` 52, `prop_destructable` 45,
`mover_keyframe`/`func_keyframed_mover` 177, `trigger_push` 27, `prop_keypad` 17, `func_pushable`
11, the `npc_*` leaves) are `plans/gameplay.md` and `plans/three-cs.md`'s. This roadmap is the
asset lane.

### R8 — characters: the skeletal lane rebuilt on the GLB corpus [was R6.1 / SF-6.3, R6.3 wield / SF-6.5, R6.4 irises]

**Designed on measurements, 2026-09-04 — the plan is `characters_r8.md`** (rulings D1–D12,
the lane, the tasks, the ranked risks, the owner calls with their defaults, and the 15 defects
the exploration found; the thirteen measurement reports behind it are under
`$ELYSIUM_WORK_ROOT/_r8_explore/`). What the exploration settled, in one paragraph: the V2 model
unit carries **every** datum the legacy skeletal exporter reads (masks strictly binary corpus-wide,
the `<layer>@<host>` fan-out reproduced from the GLB 1,505/1,505 on the largest bank, the skin
table and the VPhysics rig the `.eskm` never had); what is missing is every *derivation* (~37
rules, ~2,000 lines) and every bake *decision* — so R8 is a port of rules onto a stage and a
re-plumb of the C++ builders, which stay because nothing else can author a `USkeletalMesh`.
**Every asset lands on one baked-asset standard** (owner ruling, 2026-09-04, written into
`seam_map_unit_contract.md` → "Baked assets"): the mount mirrors `exports_v2` —
`/ElysiumBaked/<Kind>/<dir>/<Prefix>_<base>`, per-label products under `<base>/`, corpus-wide
assets under `<Kind>/_Corpus/` — ids are the only address and stems resolve through a cast
table, so the retail captures keep their keys; the landed lanes that drift (the flat `Meshes/`,
the per-map root folders, sprites, sky, lookdev) move in R8.0 rather than in a later project.
The retail-capture parity numbers are pinned as equalities; the character mount (3.9 GB, 39 % of
the plugin) is the biggest lever on mount size. Corrections the plan applies to this text: `models/character` is **485** units, not
489; the "Nosferatu/Malkavian obfuscate noise chains" are four CRT-static TV screens on
`M_V2_TwoTexture`, not character materials; `mouthshader` is a per-slot boolean on two teeth
materials with no consumer; `identity.shape: bank` classifies nothing and retires.

Cutover is gated **per body** on the R4.6 shape: a tracked `ModelsOnV2` list of unit ids the
resolver and the pipeline both read, the new lane landing beside the legacy roots under its own
kind root, nothing overwritten, the legacy roots deleted when the list is complete. Order: the
56 player bodies, the three test maps' casts, the rest; banks and props flip wholesale with R8.1
on a byte-equal payload against a frozen copy of the legacy `npc/` + `items/` export.

- **R8.0 Preflight.** The baked-asset standard implemented — one resolver twin
  (`asset_paths.baked_path` / `BakedUnit`) with a golden fixture, the models re-imported to
  `Models/<dir>/SM_<base>`, the map root to `Maps/`, sprites and sky and lookdev moved, the
  stem folds and stem accessors deleted, every map re-baked; the rulings into their seam docs
  (`seam_map_material.md` ×3, `seam_map_model.md` → "## Import — skeletal",
  `animation-architecture.md`, `physics-architecture.md` L0 deleted); the six live defects fixed
  (`placed_models.py`'s split-rotation tuple, the rigid-wield `FindWieldModel` predicate, the
  index's `wield`/`ground-item` swap, the missing `ClanDataTables` projector, the `SetModel`
  basename fold, the missing `safe_name` on the prop path); the legacy export frozen; T-A5
  marked DONE; T-C8 measured.
  → lands: one naming standard on the whole mount; the design on record; the baseline cannot
  move under the rebuild.
- **R8.1 Producer parity.** `validation/skeletal_diff.py` first, then `importers/characters.py`
  + `skeletal_stage/` porting the rules and writing the legacy product set at the legacy paths
  (game and bake untouched) plus the staged `import/characters/` tree; whole-cast run;
  `npc_export`, `UE_mdl_skeletal`, `UE_mdl_cloth`, `mdl_gltf`, `UE_extract_wield`,
  `UE_extract_items` deleted. → lands: one producer for the cast.
- **R8.2 Skeletal bake off the unit.** The skinned master pair (`M_V2_LitSkinned` +
  `…Translucent`: clothing usage, masked + dither, `ModelAlpha`, `UseAlphaTest`), `M_V2_Eyes`
  completed (the five basis vectors, `Flatten`, `Vampire` as a scalar, `Iris` on the instance),
  `M_PlayerBody` / `M_Eyes` retired; the `import characters` lane on the props template with the
  existing builders, slots on the 2,027 V2 `MI_`, `UElysiumCharacterProvenance`, prune, the
  map bake's memory drain, T-B3's guard, cloth re-pointed (render maps re-keyed), `DYN_<stem>`;
  the asset differ `verify characters --legacy-mount` and the crossfade-residual instrument.
  → lands: bodies on V2.
- **R8.3 Wield off the unit.** Skeletal always; the ref-pose re-skin moves into the build;
  `wield_corpus` unchanged over GLB bones; `DA_WieldModels` kept with `AssetId`; `M_Wield_*`,
  `bake_wield.py`, the private texture closure and `ground_models.json` retire; viewmodels stay
  LIFE6's. → lands: items on V2.
- **R8.4 Animated props.** Slots bound at bake, `BindMapMaterials` deleted, `ConfigureRest`
  narrowed, static equivalence recomputed from the unit, `DA_ElysiumPlacedModels` keyed by id.
  → lands: one material authority for a placed model.
- **R8.5 Cooked.** Facial / eyes / procedural on the mesh as user data; the sequence table and
  autolayer view in `DA_ElysiumBody_<stem>`; clip columns, events and movement as
  `UAnimMetaData` (never notifies, never root motion); T-B2 in the blend-space writer; the
  expression-table lane; the nine readers, `FElysiumTextureCache` and the `npc/` / `items/`
  trees retire. PHYS1 follows as its own row on the staged `physics` payload.
  → lands: no loose read under `npc/`.

### R9 — retire [was R8; MP-6, SF-7.1]

Settled entries above and the seam docs written before 2026-09-02 say **R8.1 / R8.2** for these
two tasks; they are the same tasks.

- **R9.1 Runtime readers deleted** [R8.1 / MP-6.1]. Every per-map sidecar parser, once all 108
  maps are on the new transport; the boot map / green room stage world keeps working throughout.
  → lands: the game reads cooked content and the corpus only.
- **R9.2 Legacy bake, masters and Cog tuning deleted** [R8.2 / MP-6.2]. `bake_map.py` legacy
  lanes, the legacy world/prop master set, per-map material packages,
  `/ElysiumBaked/Shared/Textures`, the `UE_extract_*` family the R8 producers replaced, the Cog
  tuning tabs and calibration cvars (the debug windows stay). Also what is left of the legacy
  particle lane R7.3 kept alive because the 105 unconverted maps still run on it:
  `formats/particles.py::compile_definition`, the `<map>.particles.json` writer
  (`UE_bsp_to_scene`), the Fountain-template flatten granularity inside `make_particle_
  systems.py` (its per-map `/ElysiumBaked/<map>/Particles/` output behind `--particles`), and
  `UE_extract_particles.py`'s PNG derivative (the rain material's sprite mirror,
  `_ensure_particle_mirror`) — the converted maps read the `particles/<sprite>` texture units and
  the staged `effects[]` instead. `make_particle_systems.py` and `UElysiumParticleAssetBuilder`
  themselves **stay**: under the revised B3 they are the generator of `NS_<root>`, and only the
  legacy granularity retires with the legacy bake. Acceptance: full-corpus rebake,
  doctor, the full R2.1 shot set, and a hub-chain playthrough. → lands: **one pipeline, and the
  game runs on it.**

## Open questions

**Where do the props go?** A map's geometry, materials and textures already bake to `.uasset`, but
static-prop placement still travels as the `<map>.props` sidecar that
[bake_map.py](pipeline/unreal/bake_map.py) reads. In export_v2 the placements are already inside
the `vtmb:map:` unit as scene nodes (`seam_map_map.md`, "Static props"), so the question is on the
bake side, and there are two candidate answers:

- fold the placements into the baked `.umap` as actors, so opening the level is all it takes; or
- emit a companion `UDataAsset` the runtime spawns from, keeping placement inspectable and
  reloadable without a level rebake.

**Closed (2026-09-01, R5.1): folded into the level.** Every `staticProps[]` record is one actor in
the baked `.umap`, on the R1 corpus mesh, with `solid`, `skin` and the FADES cull distance baked
onto the component. Nothing needs to change a GAME_LUMP placement without a level rebake — it is not
an entity, it has no I/O, and it never changes skin or solidity at run time — so the companion
`UDataAsset` would have bought reloadability nobody asks for at the cost of a second placement
authority. The shared-vs-per-map split is untouched: the map's level references the corpus meshes,
it does not copy them. Ruling and the per-placement mapping: `seam_map_map.md` → "## Import —
geometry and placements".

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
