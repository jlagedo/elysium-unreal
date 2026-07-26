# The `.uasset` bake: the map's look as native Unreal content

**Adopted** — `decisions.md` 2026-07-26 (cont. 6), roadmap 0.9; the work is on `main`. This
document is the spike record that earned that call, kept as the pipeline's reference: the split,
the six stages, and the engine facts it pinned down. It reads as of `sp_tutorial_1`, the map it
was measured on; the architecture now carries every exported map.

The Lumen surface-cache spike (`lumen-coverage-spike.md`) ended on a negative result: runtime-built
meshes can be given card representations, but hand-fitting them offline into a `.cards` sidecar
bought no measurable quality. The wall behind that result is that a runtime `UStaticMesh` built with
`BuildFromMeshDescriptions(bFastBuild)` can never have what the editor build produces — DDC-fitted
surfel cards, Nanite, distance fields, real LODs, BC7/BC5 compression.

This spike stops fighting that wall. `sp_tutorial_1` is decoded once, offline, into real `.uasset`
content and a real `.umap`, and **the game runs on it**: `play.bat` opens the baked level and every
system the project has runs against it. There are no A/B flags. Measuring what the old
build-everything-at-map-load rule cost is what retired it; the charter docs now describe the
architecture below.

## The split

| | |
|---|---|
| **Baked** — real assets in the `.umap` | world + 3D-skybox geometry, materials, textures, static props, projected decals, lights, sky light, height fog |
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
| `materials` | `.mtl` | `MaterialInstanceConstant` off the five committed masters — a `decal 1` surface is a projector, not geometry, so it splits off onto `M_Decal` in its own package |
| `world` | `.obj`, `.blend` | one `SM_World_*` per 2048 cm cell |
| `sky` | `_sky.obj` | `SM_Sky_*` |
| `props` | `props/*.obj`, `props/*.skins`, `props/*.phys` | one `SM_*` per model + `DA_<map>_PropSkins` |
| `level` | `.props`, `.decals`, `.lights`, `.env`, `.sky`, `.spawn` | the `.umap` |

Material selection and parameter names are lifted from `FElysiumMaterialFactory`. Light *values* are
not: the bake writes a reasonable starting point, and `UElysiumLightRig::Adopt` re-derives every
intensity, reach, falloff and specular from the raw `.lights` row at load. So the live calibration —
not whatever the bake happened to write — is what the map renders, and a Cog slider drag and a fresh
load agree exactly.

## What it produces

```
MaterialInstanceConstant     710      level: 1447 actors
StaticMesh                   339        StaticMeshActor 927   PointLight 224
Texture2D                    694        SpotLight 170         DecalActor 123
World                          1        DirectionalLight 1    SkyLight 1
meshes 339, Nanite on 311, off 28      PlayerStart 1
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
baked 'sp_tutorial_1': 1446 actors (116 world, 2 sky, 809 props, 123 decals), 395 lights, 2561 hulls
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
`MaxBrightness 8.0`) were calibrated against a scene with almost no bounce. That is why the Lights
Cog window also owns the sky and fog: how much the sky contributes and how much the per-source rig
must carry is one decision, not two. The recalibration adoption owed was paid by the sky + ambience
rework (`roadmap-archive.md` → SKY, Phases B–C).

**Nanite costs nothing and buys nothing here.** Expected at ~30k world triangles. Its value is not
throughput; it is that the ISM/Lumen question the previous spike could not settle stops mattering.

**Perf holds.** The earlier 96-vs-123 FPS reading predates every change here (sky-transform fix,
ray-tracing exclusion, collision profiles, cubemap IBL) and should not be quoted. `profile.bat`
against the baked level reports Lumen reflections 0.15–0.22 ms and Total GPU 5.08–5.54 ms — inside
the committed baseline either way (`roadmap-archive.md` → SKY C5).

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
- **The bake overwrites; it does not prune.** Re-baking after a change that moves or drops an
  asset leaves the old one on the mount, unreferenced but still in the registry. The material
  stage authors a package's whole set in one pass, so it now sweeps whatever else is in there;
  the mesh stages do not, and a shrunken export still leaves orphan `SM_*`.
- **A fresh commandlet has not indexed a new mount.** `does_asset_exist` reports False for assets
  already on disk and `create_asset` then trips the unattended overwrite guard. Scan the mount with
  `AssetRegistry.scan_paths_synchronous(force_rescan=True)` first — **and that is not always enough**:
  it still reports False for some assets present on disk, so anything the bake re-authors resolves
  by load-first-then-create, not by an existence check.
- **`unreal.EditorAssetLibrary.load_asset` logs a hard `Error` when the registry has no such asset**,
  which on a first bake is the normal path and makes a clean run report `Failure - 1 error(s)`. Use
  `unreal.load_asset` (LoadObject) for a load that is allowed to miss.
- **A bake that dies mid-run still leaves its assets on disk** — the commandlet saves dirty packages
  on exit, so a half-populated asset survives. Every stage must re-author in full rather than assume
  a clean slate.
- **A USTRUCT's generated Python type takes no constructor kwargs** unless its properties are
  Blueprint-exposed: `unreal.ElysiumSkinOverride(slot_name=…)` raises
  `TypeError: call() takes at most 0 arguments`. Populate through `set_editor_property`.
- **Custom `UDataAsset` subclasses are authorable from Python** — `unreal.DataAssetFactory` with
  `data_asset_class` set, then `create_asset`. Nested USTRUCT arrays holding `TObjectPtr<UObject>`
  round-trip through save/reload as live hard references, which is what makes the prop skin table
  (`DA_<map>_PropSkins`) a real asset rather than a text sidecar the runtime parses.
- **`FKConvexElem` is not authorable from Python.** It is a bare `USTRUCT()` — not `BlueprintType` —
  so raw hull point sets cannot be pushed into a `UBodySetup` from a bake script. The supported route
  is Geometry Script: `GeometryScript_Collision.generate_collision_from_mesh` per hull with
  `ConvexHulls` / `max_convex_hulls_per_mesh = 1` / `simplify_hulls = False` (which returns an
  already-convex input unchanged), `combine_simple_collision_array`, then
  `set_simple_collision_of_static_mesh`. Bool options drop the `b` prefix in Python
  (`simplify_hulls`, `emit_transaction`), and `combine_simple_collision` takes a `UPARAM(ref)`, so it
  hands the merged struct back rather than mutating in place.
- **A rigid body needs `CTF_UseSimpleAndComplex`**, not the `CTF_UseComplexAsSimple` the rest of the
  bake uses: Chaos can only simulate against simple shapes, while the debug pick traces complex for
  its face index. Both get cooked, so one asset serves a simulating prop and a static placement of
  the same model.
- **A `BodySetup`'s mass override does not reach a runtime-built component.**
  `UBodySetup::CalculateMass` reads the owning primitive's own `FBodyInstance` whenever there is one,
  and a component created at runtime never seeds that from the asset — so the baked value is ignored
  and Chaos computes mass from hull volume instead (a 3 kg bin came out 48 kg). The asset is still
  the right place to *carry* the value; the consumer has to re-apply it with `SetMassOverrideInKg`.
- **`create_new_static_mesh_asset_from_mesh` asserts `NumUVs > 0`** — a mesh with no UV channel takes
  the editor down with `Assertion failed: NumUVs > 0` rather than failing gracefully.
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

## The decals

VtMB's `infodecal` layer — blood, bullet holes, graffiti, band posters, rust stains — is 123
projectors on `sp_tutorial_1`, exported as a `.decals` sidecar of 15-token lines. The bake places
one `ADecalActor` each.

A decal surface is flagged `decal 1` in the shared `<map>.mtl`, and the material stage splits those
off onto `M_Decal` in `Materials/Decals/`: an `infodecal` is a projector, not geometry, so instancing
it off a world master would author 27 translucent material instances nothing can use. The textures
are already in the map's texture package, since they ride the same `.mtl` the world surfaces do.

Orientation is the runtime path's, verbatim. A deferred decal maps texture **U → local Z** and
**V → local Y**, not the intuitive Y=U/Z=V, so the surface horizontal (`SDir`, the U/s texture axis)
goes on local Z and the vertical falls out as the derived Y. `MakeRotFromXZ(Normal, SDir)` builds a
valid right-handed rotation from the two; a three-axis matrix would be reflected, because the
exporter's s/t frame is left-handed with respect to the normal. Local +X is the room-facing normal,
so the component projects along its −X into the wall, and `DecalSize` is the box **half**-size
(X = 16 cm projection reach, Y = vertical, Z = horizontal). `FadeScreenSize 0` — VtMB decals persist
at any distance.

Sort order is the sidecar's own line order, so two decals on one wall layer the way the map author
stacked them rather than in undefined order. Deferred decals write into the GBuffer before the
lighting pass, so each is lit exactly like the wall it lands on, Lumen bounce included, and Nanite
receives them normally.

## Gaps

- **Sprites** (96) are not placed. Decals and ropes are.
- **Post-process**: no `.cube` colour-grade LUT. Note the runtime path does not grade either — its
  LUT block was commented out with `// TEMP: disabled for a test` and never restored. Deliberately
  left off (owner call) until the sky/ambient recalibration settles.
- **Volumetric fog** is enabled on the baked height fog, but only maps whose `.env` turns fog on get
  a fog actor at all — and `sp_tutorial_1` has fog off, so it shows nothing there.
- **NPCs** stay on glTFRuntime. Skeletal meshes get no offline cards anyway.
- **Not all 108 maps.** The 10 exported maps bake and run; the remaining ~98 have not been through
  it, so their bake cost and the DDC/asset-registry scale at ~1,700 assets per map are still
  unmeasured in aggregate.
- **Packaging.** The mount is gitignored, so a package must either bake on first run or ship the
  user-side bake tooling (roadmap 10.5).

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
