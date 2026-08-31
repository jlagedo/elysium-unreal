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
