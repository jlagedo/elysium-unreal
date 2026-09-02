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
`unitDivergenceProvenanceOnly` 15, `proxyTargetProvenanceOnly` 13,
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
replace it. Substrate 426/426 tests (`Elysium.Substrate.LightRig` now covers two more behaviors
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

Follow-ups: R7.6 decides the decal master (deferred-decal domain); R7.2 owns the scene-fog term
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

**R5.1 landed (2026-09-01)** — see Settled.

**R5.2 landed (2026-09-01)** — see Settled.

**R5.3 landed (2026-09-01)** — ruling and landing note in `seam_map_material.md` → "Decal fog and
wetness homes (R5.3)".

**R5.4 landed (2026-09-02)** — see Settled. The decal half of R5.3's plan moved to R7.6 on a domain
fact (a `UDecalComponent` renders only `MD_DeferredDecal`; every V2 master is `MD_Surface`).

**R5.5 landed (2026-09-02)** — see Settled.
- **R5.6 Lights final** [MP-4.5]. The bake writes the VtMB-derived values from `worldLights[]`
  plus the MegaLights properties; the runtime derivation deleted (the rig applies only the R4.3
  asset and lightstyles); `.lights` reader deleted. No look-tuning — the derivation is the same
  math, computed once at bake. → lands: the editor level is the truth.

### R6 — consumers beyond maps [SF-6.3–6.6]

- **R6.1 Characters** [SF-6.3]. Character bake slots and skin families → `MI_`. → lands: bodies
  on V2.
- **R6.2 Runtime factory** [SF-6.4]. `FElysiumMaterialFactory::Create(MI_)` with runtime binds
  only (`env_cubemap` symbol, runtime proxies, fog primitive data); the Cog environment sliders
  die in favour of the settings pages. → lands: dynamic materials on V2.
- **R6.3 Wield and UI sprites** [SF-6.5]. → lands: items on V2.
- **R6.4 Deferred slice-2 readers** [SF-6.6]. Sky cube faces, rope/cable materials, eye irises,
  `tex_hi` flip off loose files. → lands: no loose-file texture reads.

### R7 — life (each task one visible lane, shot-diffed) [MP-5]

- **R7.1 Lightstyles everywhere** [MP-5.1]. → lands: flicker and pulse, the VtMB signature.
- **R7.2 Water** [MP-5.2]. `M_V2_Water` on water surfaces; leaf data and extents wired. → lands:
  water that behaves like water.
- **R7.3 Detail props** [MP-5.3]. The 143,412 `dprp` placements over 41 models as instanced
  meshes/cards, `detailPropLighting[]` consulted. → lands: exteriors stop reading bare.
- **R7.4 Sprites and coronas** [MP-5.4]. `env_sprite` via the V2 Sprite master. → lands: glows.
- **R7.5 Effects entity family** [MP-5.5]. From the R2.2 census, by prevalence: `point_spotlight`
  beams, `env_steam`, `env_embers`, `env_fire`, `env_lightglow`, `env_sun`. → lands: the ambient
  set.
- **R7.6 Decals on the V2 Decal master** [MP-5.6]. → lands: legacy `M_Decal` retired.
- **R7.7 3D-skybox wiring pass** [MP-5.7]. Miniature props/fog wired so the skybox composes with
  everything the earlier stages made live; no look judgement. → lands: the miniature complete.

### R8 — retire [MP-6, SF-7.1]

- **R8.1 Runtime readers deleted** [MP-6.1]. Every per-map sidecar parser, once all 108 maps are
  on the new transport; the boot map / green room stage world keeps working throughout. → lands:
  the game reads cooked content and the corpus only.
- **R8.2 Legacy bake, masters and Cog tuning deleted** [MP-6.2]. `bake_map.py` legacy lanes, the
  legacy world/prop master set, per-map material packages, `/ElysiumBaked/Shared/Textures`, the
  Cog tuning tabs and calibration cvars (the debug windows stay). Acceptance: full-corpus rebake,
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
