# Spike: baking `sp_tutorial_1` into native Unreal assets

Branch `spike/uasset-bake`, stacked on `spike/lumen-cards`. **Exploratory — not a decision.**

The Lumen surface-cache spike (`lumen-coverage-spike.md`) ended on a negative result: runtime-built
meshes can be given card representations, but hand-fitting them offline into a `.cards` sidecar
bought no measurable quality. The wall behind that result is that a runtime `UStaticMesh` built with
`BuildFromMeshDescriptions(bFastBuild)` can never have what the editor build produces — DDC-fitted
surfel cards, Nanite, distance fields, real LODs, BC7/BC5 compression.

This spike asks what the map looks like when it stops fighting that wall: `sp_tutorial_1` decoded
once, offline, into real `.uasset` content and a real `.umap`, with Nanite and Lumen on everything
the engine allows. It deliberately contradicts two of `CLAUDE.md`'s load-bearing rules — *"no
`.uasset` baking, no editor content loop"* and *"build all engine objects in code at map-load
time"* — to measure what those rules cost. Nothing here is wired into the shipping path and there
are no A/B flags: the branch either earns a `decisions.md` entry or gets discarded.

## Shape

Four choices frame it:

| Question | Answer |
|---|---|
| What becomes saved editor content | The whole **look** — geometry, materials, textures, props, lights, sky. The `.ents` entity substrate still runs at runtime. |
| How meshes are authored | **Geometry Script, in-editor**, straight from the existing sidecars. No new export format; the vertices are the ones the runtime path uses today, so the comparison is apples-to-apples. |
| World mesh granularity | **Spatial cells only, 2048 cm.** The cell × face-normal split from the previous spike was a workaround for the bounds-card fallback, which the DDC surfel fit replaces. |
| Where the assets live | A **gitignored plugin content mount**, `Plugins/ElysiumBaked/Content/` → `/ElysiumBaked/`. |

The mount is the bring-your-own-game posture applied to a new artefact class: the baked assets are
derived from the user's own VtMB install, so they are gitignored and regenerable exactly like
`tools/out/`. Only `ElysiumBaked.uplugin` is committed. Keeping them in a separate plugin rather
than under `Content/` means the committed tree and the derived tree cannot be confused.

## The pipeline

```
bake.bat [map] [stages]
  -> UnrealEditor-Cmd -run=pythonscript -script=tools/bake_map.py -BakeMap= -BakeStages=
       tools/bake_lib.py    sidecar readers + editor asset factories
       tools/bake_map.py    the six stages
bake_verify.py              reads the result back off the assets, not off the bake's own log
```

Stages, each independently runnable (`bake.bat sp_tutorial_1 world,level`):

| Stage | Reads | Writes |
|---|---|---|
| `textures` | `.mtl` + `tex/`, `props/tex/` | `Texture2D`, sRGB/`TC_NORMALMAP`/`TC_MASKS` by role |
| `materials` | `.mtl` | `MaterialInstanceConstant` off the four committed masters |
| `world` | `.obj`, `.blend` | one `SM_World_*` per 2048 cm cell |
| `sky` | `_sky.obj` | `SM_Sky_*` |
| `props` | `props/*.obj` | one `SM_*` per model |
| `level` | `.props`, `.lights`, `.env`, `.spawn` | the `.umap` |

Material selection, parameter names and light calibration are lifted verbatim from
`FElysiumMaterialFactory` and `UElysiumLightRig`, so a baked instance lands on the same values the
runtime MID would have bound.

## What it produces

Verified by `bake_verify.py` reading the assets back:

```
MaterialInstanceConstant     710
StaticMesh                   339
Texture2D                    694
World                          1
meshes 339, Nanite on 311, off 28
1379 material slots (0 unbound)
level: 1324 actors
  StaticMeshActor 927   PointLight 224   SpotLight 170
  DirectionalLight 1    SkyLight 1       PlayerStart 1
```

Geometry, with a per-mesh triangle count checked against the source:

| | meshes | triangles | dropped |
|---|---|---|---|
| world | 116 | 29,522 | 0 |
| sky | 2 | 3,494 | 0 |
| props | 221 | 134,006 | 0 |

The 28 non-Nanite meshes are exactly the translucent + additive surfaces (52 `blend 1` materials in
this map). Nanite is a whole-mesh setting and does not support translucency, so the world chunker
splits each cell into a Nanite bucket and a non-Nanite sibling, and a prop model with any
translucent slot falls back wholesale.

Cost, cold: textures 14 s, materials 7 s, world 94 s, sky 3 s, props 137 s, level 2 s — **about
4 minutes** for one map, once.

## What it changes

**Lumen surface-cache coverage is total and free.** `r.Lumen.Visualize.CardPlacement 1` shows
fitted cards on every wall, floor, prop and ornament. This is the entire `.cards` sidecar from the
previous spike — the offline surfel fit, the content hashing, the `-ElysiumCards` harness — replaced
by the DDC doing its normal job. Epic's own wording confirms the mechanism: *"Unlike static meshes,
[skeletal meshes] don't have offline generated cards."* Static mesh assets do, and it needs no
Nanite.

**The look changes substantially**, and that is the headline. With real card coverage the indirect
bounce Lumen produces is far stronger than on the runtime path; the same alley vantage goes from
moody and mostly black to bright and filled. VtMB's look is indirect-bounce-dominated
(`rendering-perf.md`), so this is the bounce finally arriving — but the light rig's constants
(`PointSpotScale 0.003`, `MaxBrightness 8.0`, skylight fill 0.6) were calibrated against a scene
that had almost no bounce. They now over-light. Any decision to adopt this owes a recalibration
pass against the baked lightmaps, not a straight port of the current numbers.

**Nanite costs nothing and buys nothing here.** 101 FPS with it on, 100 without, at the same
vantage — expected at ~30k world triangles. Its value is not throughput; it is that the ISM/Lumen
question the previous spike could not settle stops mattering.

**The baked path is slower than the runtime path at the same vantage** — 96 FPS baked vs 123
runtime at the alley spawn. 927 `StaticMeshActor`s and 395 light actors replace a handful of
components and one ISM per model, and full card coverage means far more surface cache to keep
updated. This is a real cost and not yet investigated.

## Engine facts this pinned down

- **`FDynamicMesh3` silently drops non-manifold triangles.** `AppendBuffersToMesh` refuses any
  triangle that would make an edge non-manifold, logging to `LogGeometry` and continuing. VtMB prop
  models share vertices freely, so an indexed append loses faces. Emitting every triangle with its
  own three vertices makes the soup manifold-by-construction; shading is unaffected because normals
  are accumulated over the model's original shared indices first. **Any bake must check the mesh's
  triangle count against its source** — the drop is otherwise invisible.
- **Face-normal sign.** `source_to_unreal` negates Y, a reflection, so the exporter reverses winding
  at OBJ-write time. The outward normal is therefore `(c − a) × (b − a)`, not the right-handed
  `(b − a) × (c − a)`. With the wrong sign every triangle on the map's floor plane points straight
  down and the map lights inside-out — geometry and materials look perfect in unlit view, which
  makes it easy to misread as a lighting bug.
- **`GeometryScriptCreateNewStaticMeshAssetOptions.enable_nanite` does not reach the asset.** Set
  `nanite_settings` on the `UStaticMesh` after creation; the assignment runs `PostEditChange`, which
  rebuilds with Nanite. A verification pass caught this — the bake reported success on all 339
  meshes while every one had Nanite off.
- **A fresh commandlet has not indexed a new mount.** `does_asset_exist` reports False for assets
  already on disk, and `create_asset` then trips the unattended overwrite guard. Scan the mount with
  `AssetRegistry.scan_paths_synchronous(force_rescan=True)` first.
- **`ADirectionalLight` and `ASkyLight` expose only `ALight::LightComponent`** as `light_component`;
  only point and spot get the typed accessor.
- **`unreal.StaticMaterial` takes no `imported_material_slot_name` keyword.**
- **A `SkyLight` with `SLS_SpecifiedCubemap` and no cubemap** resolves to flat constant ambient of
  its light colour, independent of any capture. A captured-scene skylight samples VtMB's near-black
  2D sky and leaves the map unlit — the map actor's existing choice, and the bake has to copy it.
- **`cmd` splits arguments on commas** regardless of quoting from PowerShell, so a comma-separated
  stage list arrives as separate `%n` tokens and must be rejoined.

## Gaps

Nothing below is baked; the level is the map's look, not the map.

- **Collision** is whatever the static-mesh build produced, not the `.hulls` / `.dispcol` brush
  collider the project treats as the walkable surface. The pawn stands and walks in the yard but
  falls through at the spawn point.
- **Decals** (123), **ropes** (70), **sprites** (96) — not placed.
- **Post-process**: no `.cube` colour-grade LUT volume, so the baked level is ungraded where the
  runtime path is graded. Part of the brightness delta is this, not GI.
- **Lightstyle animation**: styled sources (flicker, fluorescent) are placed at their unanimated
  base intensity. A baked light actor has nowhere to run the curve.
- **3D skybox transform**: sky meshes are placed at their raw exported coordinates; the `.sky`
  sidecar's origin/scale are not applied.
- **The entity substrate** does not run against this level — no doors, triggers, scripting, NPCs,
  `+use`. The runtime would have to spawn into the baked world rather than build it.
- **One map.** Nothing has been said about the other 107, their bake cost, or the DDC/asset-registry
  scale of ~1,000 assets per map.

## Repro

```
bake.bat sp_tutorial_1                       # all stages, ~4 min cold
bake.bat sp_tutorial_1 world,level           # re-run a subset
tools/bake_verify.py                         # read the result back off the assets
UnrealEditor.exe ElysiumUE.uproject /ElysiumBaked/sp_tutorial_1/sp_tutorial_1 -game -dx12 -ElysiumNewGame=0
```

Requires the `GeometryScripting` and `ElysiumBaked` plugins (both enabled in the `.uproject`) and an
export of the map under `tools/out/`.
