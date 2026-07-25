# Spike: `sp_tutorial_1` as native Unreal content

Branch `spike/uasset-bake`, stacked on `spike/lumen-cards`. **Exploratory — not a decision.**

The Lumen surface-cache spike (`lumen-coverage-spike.md`) ended on a negative result: runtime-built
meshes can be given card representations, but hand-fitting them offline into a `.cards` sidecar
bought no measurable quality. The wall behind that result is that a runtime `UStaticMesh` built with
`BuildFromMeshDescriptions(bFastBuild)` can never have what the editor build produces — DDC-fitted
surfel cards, Nanite, distance fields, real LODs, BC7/BC5 compression.

This spike stops fighting that wall. `sp_tutorial_1` is decoded once, offline, into real `.uasset`
content and a real `.umap`, and **the game runs on it**: `play.bat` opens the baked level and every
system the project has runs against it. It deliberately contradicts two of `CLAUDE.md`'s
load-bearing rules — *"no `.uasset` baking, no editor content loop"* and *"build all engine objects
in code at map-load time"* — to measure what those rules cost. There are no A/B flags: the branch
either earns a `decisions.md` entry or gets discarded.

## The split

| | |
|---|---|
| **Baked** — real assets in the `.umap` | world + 3D-skybox geometry, materials, textures, static props, lights, sky light, height fog |
| **Runtime** — built by `AElysiumMapActor` | `.hulls`/`.dispcol` collision, `.ropes` cables, the sky cubemap + backdrop, the `.ents` entity substrate and every entity-driven body, NPC glTF skeletals, audio, dialogue, scripting |

`UElysiumMapSubsystem::Travel` opens `/ElysiumBaked/<map>/<map>` directly — each map is its own
level, and the `/Game/Elysium` shell is now only the boot world. The map actor is spawned into that
level and **adopts** it: one pass over the actors, bucketed by the tag the bake stamped on them
(`ElysiumBakedTags.h` ↔ `TAG_*` in `bake_map.py`). Tags rather than Outliner folders, because folder
paths are editor-only metadata and vanish in a `-game` build.

The mount is the bring-your-own-game posture applied to a new artefact class: the baked assets are
derived from the user's own VtMB install, so `Plugins/ElysiumBaked/Content/` is gitignored and
regenerable exactly like `tools/out/`. Only `ElysiumBaked.uplugin` is committed.

## The pipeline

```
bake.bat [map] [stages]
  -> UnrealEditor-Cmd -run=pythonscript -script=tools/bake_map.py -BakeMap= -BakeStages=
       tools/bake_lib.py    sidecar readers + editor asset factories
       tools/bake_map.py    the six stages
bake_verify.py              reads the result back off the assets, not off the bake's own log
```

| Stage | Reads | Writes |
|---|---|---|
| `textures` | `.mtl` + `tex/`, `props/tex/` | `Texture2D`, sRGB/`TC_NORMALMAP`/`TC_MASKS` by role |
| `materials` | `.mtl` | `MaterialInstanceConstant` off the four committed masters |
| `world` | `.obj`, `.blend` | one `SM_World_*` per 2048 cm cell |
| `sky` | `_sky.obj` | `SM_Sky_*` |
| `props` | `props/*.obj` | one `SM_*` per model |
| `level` | `.props`, `.lights`, `.env`, `.sky`, `.spawn` | the `.umap` |

Material selection and parameter names are lifted from `FElysiumMaterialFactory`. Light *values* are
not: the bake writes a reasonable starting point, and `UElysiumLightRig::Adopt` re-derives every
intensity, reach, falloff and specular from the raw `.lights` row at load. So the live calibration —
not whatever the bake happened to write — is what the map renders, and a Cog slider drag and a fresh
load agree exactly.

## What it produces

```
MaterialInstanceConstant     710      level: 1324 actors
StaticMesh                   339        StaticMeshActor 927   PointLight 224
Texture2D                    694        SpotLight 170         DirectionalLight 1
World                          1        SkyLight 1            PlayerStart 1
meshes 339, Nanite on 311, off 28
1379 material slots (0 unbound)
```

| | meshes | triangles | dropped |
|---|---|---|---|
| world | 116 | 29,522 | 0 |
| sky | 2 | 3,494 | 0 |
| props | 221 | 134,006 | 0 |

The 28 non-Nanite meshes are exactly the translucent + additive surfaces. Nanite is a whole-mesh
setting and does not support translucency, so the world chunker splits each cell into a Nanite bucket
and a non-Nanite sibling, and a prop model with any translucent slot falls back wholesale.

Cost, cold: **about 4 minutes** for one map, once.

At load, with everything running:

```
LightRig: adopted 395 baked lights (10 animated) +sun +skyambient
brush collision: 2561 convex hulls / displacement collision: 3584 triangles
ropes: 70 cables
sky 'la': cubemap IBL + backdrop
baked 'sp_tutorial_1': 1323 actors (116 world, 2 sky, 809 props), 395 lights, 2561 hulls
world 'sp_tutorial_1' live: 1868 entities (185 brush bodies), epoch 1
loaded sp_tutorial_1 in 2.50s
```

## What it changes

**Lumen surface-cache coverage is total and free.** `r.Lumen.Visualize.CardPlacement 1` shows fitted
cards on every wall, floor, prop and ornament. This is the entire `.cards` sidecar from the previous
spike — the offline surfel fit, the content hashing, the `-ElysiumCards` harness — replaced by the
DDC doing its normal job. That harness is deleted on this branch.

**The sky finally lights the world.** The sky light was `SLS_SpecifiedCubemap` with *no cubemap*,
which resolves to a flat constant ambient — unshadowed fill reaching every interior through solid
walls. It now takes the map's real sky cubemap with the lower hemisphere black, so Lumen does real
sky occlusion and an interior is dark because it cannot see the sky. Epic's own wording is the
argument: *"A Sky Light should be used instead of the Ambient Cubemap to represent the sky's light
because Sky Lights support local shadowing, which prevents indoor areas from getting light from the
sky."*

**The look changes substantially, and it over-lights.** Real card coverage means far more indirect
bounce than the runtime path ever had, and the light rig's constants (`PointSpotScale 0.003`,
`MaxBrightness 8.0`) were calibrated against a scene with almost no bounce. Adoption owes a
recalibration pass, which is why the Lights Cog window now also owns the sky and fog: how much the
sky contributes and how much the per-source rig must carry is one decision, not two.

**Nanite costs nothing and buys nothing here.** Expected at ~30k world triangles. Its value is not
throughput; it is that the ISM/Lumen question the previous spike could not settle stops mattering.

**Perf is unmeasured on this branch.** The earlier 96-vs-123 FPS reading predates every change here
(sky-transform fix, ray-tracing exclusion, collision profiles, cubemap IBL) and should not be
quoted. `profile.bat` has not been run against the baked level.

## Engine facts this pinned down

- **A master material needs `bUsedWithNanite`.** Outside the editor no new shader permutation can be
  compiled, so a Nanite mesh whose material lacks the flag renders in **default grey** — 545
  warnings and a grey map. The editor hides this by compiling on demand. Exactly the same failure
  mode as the existing `used_with_instanced_static_meshes` flag, and it is silent in PIE.
- **Only a collision *profile name* survives a `.umap` save/load.** Per-channel responses set
  alongside it are discarded when loading re-applies the profile — the actors came back with
  `ECR_Ignore` on the pick channel and the debug pick silently found nothing. Declare a named
  profile in `DefaultEngine.ini` and set that. List every channel explicitly: an omitted one falls
  back to its `DefaultResponse`, and for `ElysiumUse`/`Visibility` that is Block.
- **Python binds a game trace channel under its configured `Name`**, not its slot:
  `unreal.CollisionChannel.ECC_ELYSIUM_PICK`, not `ECC_GAME_TRACE_CHANNEL2`. The response enum is
  `unreal.CollisionResponseType`; `unreal.CollisionResponse` is a struct.
- **`FDynamicMesh3` silently drops non-manifold triangles.** VtMB prop models share vertices freely,
  so an indexed append loses faces. Emitting every triangle with its own three vertices makes the
  soup manifold-by-construction; shading is unaffected because normals are accumulated over the
  model's original shared indices first. **Any bake must check the mesh's triangle count against its
  source** — the drop is otherwise invisible.
- **Face-normal sign.** `source_to_unreal` negates Y, a reflection, so the exporter reverses winding
  at OBJ-write time. The outward normal is `(c − a) × (b − a)`, not the right-handed
  `(b − a) × (c − a)`. With the wrong sign the map lights inside-out while looking perfect unlit.
- **`GeometryScriptCreateNewStaticMeshAssetOptions.enable_nanite` does not reach the asset.** Set
  `nanite_settings` on the `UStaticMesh` after creation; the assignment runs `PostEditChange`.
- **A fresh commandlet has not indexed a new mount.** `does_asset_exist` reports False for assets
  already on disk and `create_asset` then trips the unattended overwrite guard. Scan the mount with
  `AssetRegistry.scan_paths_synchronous(force_rescan=True)` first.
- **`ADirectionalLight`/`ASkyLight` expose only `ALight::LightComponent`** as `light_component`.
- **`unreal.StaticMaterial` takes no `imported_material_slot_name` keyword.**
- **`cmd` splits arguments on commas** regardless of quoting, so a stage list arrives as separate
  `%n` tokens and must be rejoined.
- **Epic on hardware-ray-traced Lumen:** *"Large meshes that overlap the entire scene are a
  performance issue, such as a skybox. These meshes should have Visible in Ray Tracing disabled."*
  The 3D skybox is scaled 16× and encloses the playable space, so it and the backdrop dome are both
  excluded. Also: *"Lumen Scene Lighting selects a small subset of the most important lights per
  surface cache tile, which makes its performance less sensitive to the total number of lights"* —
  so 395 lights is not the cost driver.

## The debug pick

World and prop surface picking moved from CPU triangle casts to one physics line trace on a
dedicated `ElysiumPick` channel (`ECC_GameTraceChannel2`, default Ignore), resolving the material via
`GetMaterialFromCollisionFaceIndex`. The channel is separate from `ECC_Visibility` because the
walkable surface is the `.hulls` brush collider, which carries no material and no face — a pick on a
shared channel reports the invisible clip volume instead of the wall that was clicked.

Verified in-game: `pick: world surface 'plaster_socwllb [slot 8]'` — the slot name is the same OBJ
group key the runtime path reported.

What is lost: the exact BSP-face flood highlight. A baked static mesh keeps no CPU-side section
geometry, so the highlight is now an oriented patch at the impact point plus the component's bounds.
The gizmo and brush-body sources are unchanged.

## Gaps

- **Decals** (123) and **sprites** (96) are not placed. Ropes are; decals were dropped with the
  runtime world build and have no baked equivalent yet.
- **Post-process**: no `.cube` colour-grade LUT. Note the runtime path does not grade either — its
  LUT block was commented out with `// TEMP: disabled for a test` and never restored. Deliberately
  left off (owner call) until the sky/ambient recalibration settles.
- **Volumetric fog** is enabled on the baked height fog, but only maps whose `.env` turns fog on get
  a fog actor at all — and `sp_tutorial_1` has fog off, so it shows nothing there.
- **`prop_physics`** stays on the runtime build: simulation needs the CoACD-decomposed
  `props/<stem>.hulls` as a per-asset body setup. `prop_dynamic` loads the baked mesh.
- **NPCs** stay on glTFRuntime. Skeletal meshes get no offline cards anyway.
- **Perf** is unmeasured (above).
- **One map.** Nothing has been said about the other 107, their bake cost, or the DDC/asset-registry
  scale of ~1,700 assets per map.

## Repro

```
content.bat                                  # masters need bUsedWithNanite
bake.bat sp_tutorial_1                       # all stages, ~4 min cold
bake.bat sp_tutorial_1 world,level           # re-run a subset
tools/bake_verify.py                         # read the result back off the assets
play.bat sp_tutorial_1                       # the game, on the baked level
```

Requires the `GeometryScripting` and `ElysiumBaked` plugins (both enabled in the `.uproject`) and an
export of the map under `tools/out/`. A map with no bake is refused by `Travel` with the command to
run. Note the running game holds the `.umap` open — quit before re-baking the `level` stage.
