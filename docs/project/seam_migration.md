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
  line index. The stable key becomes the lump-15 record ordinal, with a one-shot remapper for
  existing `_lights/*.json`.
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
plan is "## Plan — maps track" below; the shared-MI switch (SF-6.1) is blocked until decal fog
and wetness have a home that is not a per-map material instance.

## Plan — surfaces track (export gap, surface properties, materials, reflections)

Owner instructions (2026-08-31): plan the whole surface chain in the smallest possible tasks;
**do not touch the legacy pipeline** (`bake_map.py`, `make_*_materials.py`, the runtime factory)
until the new one is buttoned up layer by layer — export first, then each import layer, then the
owner tunes on knobs, and only then is anything wired to consumers. This section is the plan;
scheduling and status go to `roadmap.md` when a task is picked up. IDs are `SF-<phase>.<n>`; the
earlier `SF-A/B/C/D` names are kept in brackets where a task moved.

**Finding that reorders everything.** The full export_v2 run is complete for install members, but
the texture and material seams enumerate VPK and loose files only. The 9,576 files embedded in the
108 BSPs' PAKFILE zips — 7,501 patched `.vmt`, 1,325 probe `.tth`, 750 `.ttz` — are recorded by
the corpus index under each map's `embedded[]` with `asset: null` ("became nothing"), while
`seam_map_map.md` → "PAKFILE routing" states they become ordinary units and
`unit_contract/origin.py` already defines the `bsp-pakfile` origin. The map root's
`cubemaps[].resolved` and `pakfile.entries[].unit` resolve against the zip, not against a unit on
disk, so nothing failed. The contract exists; the exporter half was never built. Phase 1 closes
this finding: SF-1.3/1.4 publish the 1,325 probe and 7,501 patched-material units and SF-1.5
claims every one of them in the corpus index's `embedded[]`, driving `summary.embeddedUnclaimed`
to 0.

### Phase 1 — export layer complete (export_v2)

- **SF-1.1 Make the gap visible** [A1]. Corpus index `summary` counts embedded members with
  `asset: null`; `uv run elysium doctor` reports the number. Done when the count (9,576) prints and
  a test pins it. No behaviour change.
- **SF-1.2 Answer the 575** [new]. Establish what the `.tth` probes without a `.ttz` twin are
  (header-only? mip-less?) and record it in `seam_map_texture.md`. Read-only investigation.
- **SF-1.3 Probe textures as units** [A2]. The texture seam takes a second enumeration over each
  BSP's PAKFILE `.tth`/`.ttz` and emits `textures/maps/<map>/c<x>_<y>_<z>.glb` with the
  `bsp-pakfile` origin, capsule bytes included. Done when 1,325 units exist and validate.
- **SF-1.4 Patched materials as units** [A3]. The material seam does the same for PAKFILE `.vmt`,
  emitting `materials/maps/<map>/<mat>_<x>_<y>_<z>.glb`; `$envmap` resolves to the 1.3 texture
  unit; the base material and the probe origin are recorded as dependencies (`role: material`,
  `patchOf`; `cubemapOrigin`). Done when 7,501 units exist and validate.
- **SF-1.5 Index claims them** [A4]. `_pakfile_members` finds every embedded key claimed; the 1.1
  count goes to 0; the map validator fails a map whose `cubemaps[]`/`textures[]`/
  `pakfile.entries[]` names a unit that is not on disk. Same task: texture edges in `index.glb`
  carry `parameter` (slice-2 follow-up).
- **SF-1.6 Re-export, re-import textures** [A5]. Full `export_v2` run, doctor, then
  `uv run elysium import textures` picks up the 1,325 probes as `TC_` under
  `/ElysiumBaked/Textures/maps/<map>/`. Ledger rows and counts in this file updated.

### Phase 2 — import layer: surface properties (slice 3)

- **SF-2.1 Asset class** [B1]. `UElysiumPhysicalMaterial : UPhysicalMaterial` with the fields
  Unreal lacks (movement, footstep pools, impact matrix, sound-script IDs, `gameMaterial`), a
  provenance `UAssetUserData`, and `EPhysicalSurface` entries in `DefaultEngine.ini` for the
  compact classes. Substrate test for the JSON apply.
- **SF-2.2 Stage** [B2]. Python resolves each unit's `base` chain to flat values (the doc says
  inheritance is the consumer's), writes a manifest + sidecar. Test: `weapon` root, a three-deep
  chain, a repeated-scalar anomaly.
- **SF-2.3 Import** [B3]. Editor script writes `/ElysiumBaked/SurfaceProperties/PM_<name>` (63),
  recipe stamps, registry tags, idempotent rerun. Sound references stored as asset IDs; they flip
  to hard `USoundWave` refs in the sound slice.
- **SF-2.4 Docs** [B4]. `seam_map_surface_property.md` gains "## Import"; ledger row here.

### Phase 3 — import layer: materials, design (slice 4, docs only)

- **SF-3.1 Master inventory** [D1]. Table: 42 resolved programs + the 8 real unresolved families
  (`worldvertextransition`, `decalmodulate`, `refract`, `cable`, `shatteredglass`, `cloud`,
  `heatglow`, `worldtwotextureblend`) → master → blend mode → parameters. Each row cites its
  shader-program unit. Debug/tool families listed as "no master, provenance only". The `EnvMap`
  feature is specified here with knobs, not values: mask → roughness/specular curve,
  `$envmaptint` grey-vs-chromatic split, authored fixed cube as a literal additive sample scaled
  by a knob, `$envmapmode` sphere variant [C4, C5 as specs].
- **SF-3.2 Parameter table** [D2]. All 229 VMT keys → destination (texture / scalar / vector /
  static switch / master choice / physical material / runtime / provenance-only). Rule: a key with
  no destination fails staging.
- **SF-3.3 Proxy policy** [D3]. The 20 proxy kinds → shader-time node (`Sine`, `TextureScroll`,
  `AnimatedTexture`, `TextureTransform`, noise), runtime C++ (`PlayerProximity`, `PlayerPosition`,
  `PlayerSpeed`, `GlobalWetness`, `TextConsole`), or provenance-only. One table.
- **SF-3.4 Naming, identity, knob contract** [D4]. `/ElysiumBaked/Materials/<dir>/MI_<stem>`;
  patched map materials under `maps/<map>/`; new masters under
  `Content/ElysiumGenerated/Materials/V2/` beside, not over, the legacy set. The knob contract:
  which values are settings, which are class-table rows, which are per-instance from the VMT.
  Settled entry.

### Phase 4 — import layer: materials, build (slice 4)

- **SF-4.1 Knobs** [C0, legacy rewire removed]. (1) `UElysiumSurfaceSettings : UDeveloperSettings`
  — `DefaultSpecular`, `DefaultRoughness`, `DefaultMetallic`, `LightSpecularScale`,
  `MaskRoughnessMin/Max`, `MaskSpecularScale`, `EnvTintScale`, `FixedCubeStrength`,
  `CaptureRadius`; `PostEditChangeProperty` writes them into a new `MPC_ElysiumSurfaces`
  collection, so a PIE session follows the slider; saved to git-tracked
  `Config/DefaultElysium.ini`. (2) `UElysiumSurfaceCalibration : UDataAsset` — one row per surface
  class (roughness, specular, metallic), regenerating a 64×1 lookup texture on edit. Nothing legacy
  reads either. Done when a Substrate test round-trips both and the ini persists an edit.
- **SF-4.2 Provenance class** [D5]. `UElysiumMaterialProvenance : UAssetUserData` (raw ordered
  parameters, source SHA, resolved program, proxies, anomalies, coverage) + registry tags
  (`ElysiumShaderProgram`, `ElysiumMaster`). Substrate test.
- **SF-4.3 Masters, one task per family, transcribed from the shader units** [D6]: 4.3a Lit
  (opaque/masked/translucent; selfillum, envmap, bump switches), 4.3b Unlit (+`$ignorez`,
  `$vertexcolor`/`$vertexalpha`), 4.3c Eyes, 4.3d Water, 4.3e Sprite, 4.3f Refract, 4.3g Decal,
  4.3h Additive, 4.3i TwoTexture/VertexTransition. New `make_v2_materials.py`, one graph per
  family, each citing the program it transcribes; every master reads `MPC_ElysiumSurfaces` and
  the class LUT; lighting terms (`v0`, lightmap) are Lumen's. Legacy `make_*_materials.py`
  untouched.
- **SF-4.4 Stage** [D7]. GLB → manifest: master by resolved program; texture params →
  `/ElysiumBaked/Textures` (`_linear` twin for data-class bindings); scalars/vectors/switches by
  3.2; `PhysMaterial` by `$surfaceprop` (Phase 2 assets); class index by `$surfaceprop` → VMT top
  directory → family default; `$envmap` concrete → `TC_`, symbol → runtime bind; proxies by 3.3.
  Every unmapped key is a listed stage failure.
- **SF-4.5 Import** [D8]. Editor script writes the `MI_` assets, applies provenance, stamps,
  saves; compile check per instance; `import_report.json`; full corpus first run; idempotent
  rerun.
- **SF-4.6 Parity oracle** [D9]. A numpy ps.1.x interpreter over `shader-programs/source/*`
  units, run against one master's post-lighting terms on fixed inputs; extend per 4.3 family.
  Lighting terms excluded by design.
- **SF-4.7 Lookdev map** [new]. A generated `/ElysiumBaked/Lookdev/Materials.umap`: a grid of
  spheres and planes, one per selected `MI_` (a named review set of 20-24 entries: a plaster
  wall, a tiled floor, a chrome fixture, Asylum glass, a wet street, an eye, water, a translucent
  fixed-cube glass and two patched map instances so the instance-of-instance path is on the map
  too), lit by a rig of point lights centred on the grid and an exposure pinned independent of
  the physical camera. This is how the new masters are looked at before any real map uses them,
  and it is the PIE window for Phase 5. A review-set entry whose `MI_` is not staged yet renders
  on a loud, unmistakable placeholder and the generator run itself fails
  (`uv run elysium import materials --lookdev --lookdev-allow-missing` to accept that on
  purpose); `lookdev_report.json` under `$ELYSIUM_WORK_ROOT/reports/lookdev/` carries the
  placed/missing counts the CLI prints. Touches nothing legacy.

### Phase 5 — owner tunes on knobs (lookdev)

**How the owner tunes (2026-08-31).** The concrete procedure, once the material import and lookdev
map are current: open the editor; open `/ElysiumBaked/Lookdev/Materials`; Alt+P to play in the
editor (PIE stays on the lookdev map — a `GameMode` override, not a real-map launch). Edit → Project
Settings → Elysium → Surfaces exposes the sixteen global knobs — `ClassInfluence`,
`DefaultRoughness`/`DefaultSpecular`/`DefaultMetallic`, `MaskRoughnessMin`/`MaskRoughnessMax`,
`MaskSpecularScale`, `MaskMetallicMax`, `EnvTintScale`, `ChromaticTintStrength`, `ChromaThreshold`,
`FixedCubeStrength`, `Overbright`, `LightSpecularScale`, `CaptureRadius`, `DecalDepthOffset` — and an
edit auto-writes the git-tracked `Config/DefaultElysium.ini` (`UElysiumSurfaceSettings`'s
`PostEditChangeProperty` mirrors it into `MPC_ElysiumSurfaces` live, so PIE follows the slider with
no restart); commit the ini. Content Browser → `/Game/ElysiumGenerated/Materials/V2/
DA_SurfaceCalibration` is the per-class table (72 rows): edit a row's roughness/specular/metallic and
Ctrl+S saves the LUT texture regeneration together with the asset — the generator (`make_surface_
knobs.py`) never resets a tuned row on a later run. Per-material `MI_` edits made directly in the
Material Instance editor are throwaway: the next `uv run elysium import materials` overwrites every
instance from its manifest, by design (no per-material tuning layer). After a knob or class-table
edit, `uv run elysium import materials --lookdev` regenerates the review map so the next PIE look
reflects it; `pipeline/unreal/lookdev_set.json` is the review list the owner extends when a surface
needs its own sphere/plane in the grid, and `--lookdev-set <path>` points the generator at a
different one (a scratch set while drafting new entries, say) without touching the tracked file.
`--lookdev` never accepts `--select`/`--force` — those are the corpus-staging flags, and
`--lookdev` stages nothing. No status marks belong in this note — `roadmap.md` owns task status.

- **SF-5.1 Defaults** [C1]. Owner opens the lookdev map in PIE, tunes the 4.1 globals in Project
  Settings, Ctrl+S. The committed ini is the deliverable.
- **SF-5.2 Class table** [C2]. Owner fills and tunes the 4.1 data asset rows in PIE, saves. The
  asset is the deliverable; `docs/vtmb/surface_properties.md` only points at it.
- **SF-5.3 EnvMap knobs** [C4, C5]. Owner tunes the mask curve, tint split and fixed-cube
  strength on the review set's masked and Asylum surfaces; the result is written as a Settled
  entry here.

### Phase 6 — wiring (the only phase that touches consumers)

- **SF-6.1 Map materials** [D10a]. The map bake's `material_for` returns the imported `MI_` by
  `vtmb:material:*`; the per-map material packages stop being written. First real-map PIE look.
- **SF-6.2 Reflection captures** [C3]. The map bake places one `SphereReflectionCapture` per
  `cubemaps[]` origin from the export_v2 map unit, radius from the 4.1 setting; light
  `specular_scale` comes from `LightSpecularScale`.
- **SF-6.3 Characters** [D10b]. Character bake slots and skin families → `MI_`.
- **SF-6.4 Runtime factory** [D10c]. `FElysiumMaterialFactory::Create(MI_)` with the runtime
  binds only (`env_cubemap` symbol, 3.3 runtime proxies, fog primitive data); Cog Environment
  sliders become views onto `UElysiumSurfaceSettings`.
- **SF-6.5 Wield and UI sprites** [D10d].
- **SF-6.6 Deferred readers from slice 2.** Sky cube faces, rope/cable materials, eye irises,
  `tex_hi` flip off loose files (`shared/tex`, `npc/tex`, `retex_dds`).
- **SF-6.7 Second tuning pass** [new]. Owner re-tunes 5.1–5.3 on real maps (Santa Monica street,
  Asylum bar, Venture Tower lobby) in PIE; commits ini and asset.

### Phase 7 — retire

- **SF-7.1 Retire legacy** [D11]. Legacy masters, per-map material packages,
  `/ElysiumBaked/Shared/Textures`, the legacy `SourceCube` wetness path (owner decision under
  5.3), `tex/cube/` sidecars, `MPC_ElysiumEnvironment`'s surface scalars. Ledger rows here move
  to retired.

## Plan — maps track (sidecar convergence, entity contract, map life)

Owner instructions (2026-08-31): converge the runtime sidecars into the Unreal map — real level
objects where the validation says they can be, per-map assets where they cannot — wire the V2
materials, cubemaps, fog and every dormant visual lane, and finish able to **drop the legacy
pipeline entirely and run the game on the new one**. The I/O contract must not break. Tasks are
the smallest possible; IDs are `MP-<phase>.<n>`. The validation findings this plan answers are in
the Settled entry "Map convergence validated" above. Cutover is gated **per map, never per
system** — `LoadMap`'s failure ladder is conjunctive (collision, entity world and nav all fail the
barrier outright), so a system half-migrated across all maps boots nothing, while an unmigrated
map on the legacy path always boots.

### Phase 1 — instruments and guards (nothing moves yet)

- **MP-1.1 Baseline shots.** `validation/shots_diff.py` reference frames for a representative map
  set (every hub + one instance of each district type), captured on the current build. The
  rendered half of this refactor (lights, fog, sky, materials) has no test tier; this is its only
  instrument. Nothing moves until the baseline exists.
- **MP-1.2 Censuses.** Per-map `.ents` census (entity count, brush/hull/output totals per class),
  light census (rows per type, styled-light count), and an effects-entity census
  (`env_sprite`/`env_steam`/`env_fire`/`env_embers`/`point_spotlight`/… occurrences per map — the
  life sweep left prevalence unverified). Pinned as a JSON the differ and later phases read.
- **MP-1.3 Recipe closes over what it absorbs.** `level_sidecar_recipe` covers `.props`,
  `.decals`, `.lights`, `.env`, `.sky`, `.spawn` only; extend it to `.ents`, `.hulls`, `.dispcol`,
  `.ropes` and `_lights/<map>.json` **before** any content moves into the level, and verify a
  touched sidecar dirties the `.umap`. Otherwise the tracker serves stale levels that look like
  runtime bugs.
- **MP-1.4 Travel gate successor.** `Travel` refuses a map without `<map>.obj`. Define the new
  lane's readiness artifact, teach the gate to accept either, and have the new lane emit it from
  day one — the gate must never be absent between lanes.

### Phase 2 — the new producer, byte-comparable (game untouched)

- **MP-2.1 The join, stated.** The entities GLB is lump 0 only; `hulls`, `contents`,
  `blocks_player`, `brush_mesh` and sky-membership are root-unit facts. Either publish them on the
  root unit as stated fields or specify the root+entities join the producer performs — decided in
  `seam_map_map.md` before code. The hull solver ports **verbatim** (its tolerances are
  load-bearing); the world-space-hulls-with-origin-relative-attachment invariant is verified on a
  rotating door and then stated once in the seam doc.
- **MP-2.2 Producer emits legacy sidecars.** A new `map_sidecars` producer reads the four GLB
  units (+ nav-graph seam, the fifth source the "4 units" framing omitted) and emits
  byte-comparable `.ents`/`.hulls`/`.dispcol`/`.lights`/`.env`/`.sky`/`.spawn`/`.ropes` — proving
  the join with every existing test tier still valid and the game completely untouched.
- **MP-2.3 The differ.** Per-map differ against the legacy exporter's output over all 108 maps;
  hull vertex counts and AABBs per brush entity compared explicitly. Byte-equal or
  named-divergence-only is the acceptance.
- **MP-2.4 Named divergences, one commit each.** Datamap output typing (`outputLike` demotions —
  with the `FElysiumSaveVersion` bump and the `OutputTimesRemaining` length-gate turned into a
  loud warning), key folding, `param` stripping, `delay` atof-vs-float, `extra` retention,
  `times==0→-1` ownership (exactly one owner). Each with its own shot diff and Substrate pin
  (`Outputs.Num()` per class on a golden def set). None ride along inside the format change.
- **MP-2.5 Legacy exporter retired.** `.weather`/`.particles` re-pointed at the new producer
  (they re-read the `.ents` the old exporter wrote); `UE_bsp_to_scene.py` deleted. The game is
  byte-identical at this point.

### Phase 3 — transport: assets instead of loose files

- **MP-3.1 Entity asset.** Per-map `UElysiumMapEntities` data asset the producer generates;
  it deserializes **into** `FElysiumEntityDef` (the plain struct stays — the whole Substrate tier
  constructs it directly), asserts def-count and index parity against the `.ents` it replaces,
  then the `.ents` reader is deleted. One row per lump block, lump order, `worldspawn` included,
  no drops — the entity index is the save file's primary key.
- **MP-3.2 Hull payload asset.** Cooked collision (convex elements per brush entity, trimesh for
  world/displacement) as a per-map asset with an `IInterface_CollisionDataProvider` vessel;
  `ElysiumMapCollision::Build` and `BuildBrushBody` consume it. Component ownership, spawnflag
  solidity, dormancy and mobility stay at runtime; the nav volume keeps its bounds source and the
  collision-ready barrier its signal. `.hulls`/`.dispcol` readers deleted after Content-tier
  parity.
- **MP-3.3 Environment assets.** `.env`/`.sky`/`.spawn` values into a per-map map-info asset
  (fog stays per-primitive CPD — the values feed the same stamping path); the light overlay
  re-keyed to the lump-15 ordinal with a one-shot `_lights/*.json` remapper; Cog windows read and
  write the assets. Regenerating an asset must not require a level rebake — that property is what
  the sidecars were for, and it is kept.
- **MP-3.4 Per-map cutover flag.** A map on the new transport boots from assets; any other map
  boots the legacy path unchanged. First converted map: one hub, shot-diffed against MP-1.1.

### Phase 4 — the map bake rebuilt on the new lane

- **MP-4.1 Geometry from GLB.** World/sky/brush meshes and props baked from the root unit's
  scenes instead of `.obj`/`.props` (placements are already scene nodes — this also settles the
  "Where do the props go?" open question: placements fold into the level, the census stays in
  provenance). The `.obj` gate flips to the MP-1.4 artifact.
- **MP-4.2 Decal fog and wetness homes.** The two per-map-state axes that force per-map material
  instances today: decal fog (a `UDecalComponent` carries no CPD) and weather wetness. Design
  task — candidate answers: MID-at-load for decals only, or a per-map MIC child of the V2 master
  with the fog/wetness parameters as the only overrides. Blocks MP-4.3.
- **MP-4.3 V2 materials wired (SF-6.1 lands here).** `material_for` returns the imported `MI_` by
  `vtmb:material:*`; per-map material packages stop being written; PAKFILE-patched units resolve
  by their `maps/<map>/` unit ids. The ~217 proxy-animated materials come alive by the rebind
  alone. Shot-diff per map; the legacy master set stays until Phase 6.
- **MP-4.4 Reflection captures (SF-6.2 lands here).** `SphereReflectionCapture` per `cubemaps[]`
  origin, radius from `CaptureRadius`, built under `-AllowCommandletRendering` — a Lumen fallback
  lane, knob-scaled, per the settled reflections direction.
- **MP-4.5 Light values from the lighting unit.** The bake reads `worldLights[]` (lump-15
  ordinals) instead of `.lights`; editor levels stop lying (the rig's calibration knobs and
  per-frame lightstyle animation stay runtime, reading the MP-3.3 assets).

### Phase 5 — life (each task is one visible lane, shot-diffed)

- **MP-5.1 Lightstyles everywhere.** The rig's pattern animator (styles 0–11 exist) driven for
  every styled light row, not only the animated-rig path. VtMB's flicker is atmosphere signature.
- **MP-5.2 Water.** `M_V2_Water` bound on water surfaces; leaf data (lumps 36/46) and the
  `.water` sidecar's successor feed extents and cheap-water distances; `EElysiumWaterLevel`
  already reads the volumes.
- **MP-5.3 Detail props.** `dprp` placements baked (instanced static meshes / sprite cards),
  `detailPropLighting[]` consulted; exteriors stop reading as bare.
- **MP-5.4 Sprites and coronas.** The `.sprites` lane's successor consumed: `env_sprite` glows
  via the V2 Sprite master (the exported-but-never-read regression closed).
- **MP-5.5 Effects entity family.** From the MP-1.2 census, implement by prevalence:
  `point_spotlight` beams, `env_steam`, `env_embers`, `env_fire`, `env_lightglow`, `env_sun` —
  Niagara/material lanes per class, provenance-only where the census says a class never ships.
- **MP-5.6 Decals on the V2 Decal master.** After MP-4.2; legacy `M_Decal` retired with Phase 6.
- **MP-5.7 3D-skybox composition pass.** Miniature richness (props, fog banding) reviewed on the
  converted hubs; owner tuning session on the existing knobs.

### Phase 6 — retire (with SF-7.1)

- **MP-6.1 Runtime readers deleted.** The per-map sidecar parsers (`.ents`, `.hulls`, `.dispcol`,
  `.ropes`, `.env`, `.lights`, `.sky`, `.spawn`) go once all 108 maps are on the new transport;
  the boot map / green room (`BuildStageWorld`, no sidecars by design) keeps working throughout.
- **MP-6.2 Legacy bake and masters deleted.** `bake_map.py`'s legacy lanes, the legacy world
  master set, per-map material packages — folded with SF-7.1's list. Full-corpus rebake, doctor,
  full shot-diff set against MP-1.1, and a playthrough smoke of the hub chain is the acceptance.

## Open questions

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
shared-vs-per-map split the corpus bake depends on. Needs a test, not an argument. The maps track
picks the fold-into-the-level answer (MP-4.1); this question closes when that task lands.

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
