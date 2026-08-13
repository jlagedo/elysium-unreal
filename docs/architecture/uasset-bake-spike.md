# The `.uasset` bake: the map's look as native Unreal content

This document owns the bake/runtime split, the seven offline stages, and the UE 5.8 engine facts
that require the map's look to be native content. A fast runtime `UStaticMesh` lacks the editor
build's fitted Lumen cards, Nanite data, distance fields, LODs, and BC texture compression; the
offline bake supplies those representations.

Each exported map produces a real `.umap` and native assets under the gitignored
`/ElysiumBaked` mount. Gameplay opens that level directly and adopts it; there is no second
runtime-rendering path for the baked look.

## The split

| | |
|---|---|
| **Baked** — real assets in the `.umap` | world + 3D-skybox geometry, materials, textures, static props, unplaced movable brush meshes, projected decals, lights, sky light, height fog |
| **Runtime** — built by `AElysiumMapActor` | `.hulls`/`.dispcol` collision, `.ropes` cables, the sky cubemap + backdrop, the `.ents` entity substrate and every entity-driven body, movable-brush placement, audio, dialogue, scripting |

This table describes the **map** bake. Character skeletal assets — skeletons, meshes, animation
sequences, blend profiles and blend spaces — are baked by their own commandlet onto the same mount
rather than built at runtime; `docs/project/roadmap.md`'s ANM programme owns that work and
`docs/architecture/animation-architecture.md` its design.

`UElysiumMapSubsystem::Travel` opens `/ElysiumBaked/<map>/<map>` directly — each map is its own
level, and the `/Game/Elysium` shell is now only the boot world. The map actor is spawned into that
level and **adopts** it: one pass over the actors, bucketed by the tag the bake stamped on them
(`ElysiumBakedTags.h` ↔ `TAG_*` in `bake_map.py`). Tags rather than Outliner folders, because folder
paths are editor-only metadata and vanish in a `-game` build.

The mount is the bring-your-own-game posture applied to a new artefact class: the baked assets are
derived from the user's own VtMB install, so `Plugins/ElysiumBaked/Content/` is gitignored and
regenerable exactly like `$ELYSIUM_EXPORT_ROOT/`. Only `ElysiumBaked.uplugin` is committed.

## The pipeline

```
uv run elysium export map <map> [--force]
  -> offline map export
  -> UnrealEditor-Cmd -run=pythonscript -script=pipeline/unreal/bake_map.py -BakeMap=
       pipeline/unreal/bake_lib.py    sidecar readers + editor asset factories
       pipeline/unreal/bake_map.py    the seven stages
  -> bake_verify.py                   reads the result back off the assets, not off the bake's own log
```

| Stage | Reads | Writes |
|---|---|---|
| `textures` | `.mtl` + `tex/`, `props/tex/` | `Texture2D`, alpha-capable source retained where the material needs it, sRGB/`TC_NORMALMAP`/`TC_MASKS` by role, existing packages replaced in place |
| `materials` | `.mtl` | `MaterialInstanceConstant` off the generated local masters — a `decal 1` surface is a projector, not geometry, so it splits off onto `M_Decal` in its own package |
| `world` | `.obj`, `.blend`, `brushes/brush_*.obj` | one `SM_World_*` per 2048 cm cell plus unplaced `/Brushes/SM_brush_*` assets |
| `sky` | `_sky.obj` | `SM_Sky_*` |
| `props` | `props/*.obj`, `props/*.skins`, `props/*.phys` | one `SM_*` per model + `DA_<map>_PropSkins` |
| `particles` | `.particles.json` + its referenced normalized sprites | one Niagara system per placed emitter closure under `/Particles` |
| `level` | `.props`, `.decals`, `.lights`, `.env`, `.sky`, `.spawn` | the `.umap` |

### Incremental invalidation

A normal export first fingerprints the bytes consumed by each stage and records coarse
`bake:<map>:<stage>` receipts in the generated export manifest. Rewriting an intermediate with
identical bytes does not invalidate its stage; adding, removing, or renaming an input does. The
receipt also carries the stage's generated package inventory, so a missing or unexpected owned
asset invalidates the stage even when its source files are unchanged.

Before Unreal launches, the driver freezes those stage fingerprints and their authoring-policy
fingerprints in a unique `.elysium-bake-runs/<run-id>/plan.json`. Inside the commandlet, every
desired object path gets a canonical semantic recipe and SHA-256 fingerprint. Verified per-map
recipes live in `.elysium-bake-assets/<map>.json`; a matching recipe resolves the existing asset
without configuring, dirtying, or saving it. A stale recipe authors only that asset. Textures hash
their source bytes and import role; materials hash parameters and referenced object paths; meshes
hash their section buffers, slots, materials, Nanite and collision policy; particles hash each
root's flattened definition closure; and the level hashes parsed placement/environment values plus
referenced asset-path inventories. Pixel changes therefore do not dirty materials, and in-place
mesh or material changes do not dirty the level.

Runtime-only sidecars such as `.ents`, `.hulls`, `.dispcol`, and `.ropes` are outside every bake
fingerprint. A particle-only change runs `-BakeStages=particles`; changed world, sky, or prop mesh
families also rebuild `level`, because that stage discovers their package membership when it places
actors. Maps with the same stale-stage set retain commandlet batching. The commandlet writes a
pending desired inventory with `built/reused/pruned` counts. Independent verification runs only for
a mutated map or a changed verifier contract. Input drift, import/save/prune failure, commandlet
failure, and verification failure promote neither asset nor stage receipts. The first schema-v1 run
is deliberately a full rebuild; `--force` always bypasses both receipt layers and runs all seven
stages.

The `sp_tutorial_1` acceptance inventory is 2,100 assets. A one-pixel change to
`tex/asphalt_asphalta.png` reports `1 built / 879 reused / 0 pruned`, changes only
`T_tex_asphalt_asphalta.uasset`, and completes in 47.932 s end to end (3.767 s commandlet script,
12.220 s verifier script). A combined material/world-chunk/prop/particle/placement edit changes
exactly those five packages; its byte-for-byte restore changes the same five back. The final no-op
export completes in 10.246 s and launches no Unreal process.

Material selection and parameter names are lifted from `FElysiumMaterialFactory`. Light *values* are
not: the bake writes a reasonable starting point, and `UElysiumLightRig::Adopt` re-derives every
intensity, reach, falloff and specular from the raw `.lights` row at load. So the live calibration —
not whatever the bake happened to write — is what the map renders, and a Cog slider drag and a fresh
load agree exactly.

### Material and texture semantics

The shared MTL contract keeps three alpha-bearing paths distinct. Generic `blend 1` remains on
`M_World_Translucent`; `glass 1` selects `M_World_Glass`; `refract <amount>` plus `refractmap`
selects `M_Refract` before either. Glass is lit reflective VtMB glass rendered as UE Thin
Translucent with Surface Forward Shading and Pixel Normal Offset. Its authored albedo alpha is
surface coverage, an authored bump map wins, and otherwise the exporter derives a restrained
tangent normal from the retained uneven-glass image inside the reflective/alpha region. Source
`Refract` is a separate clear distortion overlay: its signed DUDV or authored normal is imported as
a linear normal map, the authored amount offsets PNO from neutral 1.0, and the vector texture never
becomes pane colour.

Model textures are decoded once and retained as RGBA in the model-export basetexture cache. Opaque
materials write RGB, while `$translucent`, `$alphatest`, or `$additive` write RGBA. If an opaque
material encounters a shared basetexture first and a later material needs alpha, the exporter
promotes the cached PNG from that retained RGBA image; emissive and reflection-mask products use
the same original pixels. The Unreal import task always submits an existing `Texture2D` for
replacement rather than returning it untouched, so a focused re-export updates the package in
place without breaking material or mesh references.

Verification reads the saved assets rather than trusting exporter intent. Flagged prop albedos with
non-opaque source alpha require the `HasAlphaChannel` asset tag. Semantic glass additionally
requires `M_World_Glass`, an alpha-capable albedo, and a bound linear normal-compressed `BumpMap`;
Source Refract requires `M_Refract`, a bound linear normal-compressed `RefractMap`, and the exact
`SourceRefractAmount`. Map-specific expectations may pin known placements so a missing effect card
cannot pass by merely removing its material.

Renderable BSP entity models never belong to the baked static level. The exporter partitions
their faces out of `<map>.obj`, preserves materials, UVs, blend/cubemap assignment and
`StartHidden` geometry, and writes one pivot-local `brushes/brush_<model>.obj`. Tools-only
trigger surfaces have hulls but no `brush_mesh`. The world stage builds each annotation with
the map's shared materials and material-driven Nanite policy, without an actor or mesh
collision. World, sky, prop and brush stages each prune stale assets from the name family they
own before the level stage discovers actors. At entity build, the runtime attaches the mesh at
identity to the movable `UElysiumBrushComponent`; that body remains the authoritative collision,
transform, dormancy and teardown object.

## What it produces

```
MaterialInstanceConstant     764      level: 1442 actors
StaticMesh                   404        StaticMeshActor 921   PointLight 224
Texture2D                    866        SpotLight 170         DecalActor 123
World                          1        DirectionalLight 1    SkyLight 1
meshes 404, Nanite on 366, off 38      PlayerStart 1
1443 material slots (0 unbound)
```

| | meshes | triangles | dropped |
|---|---|---|---|
| world | 110 | 24,789 | 0 |
| sky | 2 | 3,573 | 0 |
| movable brushes | 73 | 5,114 | 0 |
| props | 219 | 133,590 | 0 |

The 38 non-Nanite meshes are exactly the translucent + additive surfaces. Nanite is a whole-mesh
setting and does not support translucency, so the world chunker splits each cell into a Nanite bucket
and a non-Nanite sibling, and a prop model with any translucent slot falls back wholesale.

Cost, cold: **about 4 minutes** for one map, once.

At load, with everything running:

```
LightRig: adopted 395 baked lights (10 animated) +sun +skyambient
brush collision: 2561 convex hulls / displacement collision: 3584 triangles
ropes: 70 cables
sky 'la': cubemap IBL + backdrop
baked 'sp_tutorial_1': 1441 actors (110 world, 2 sky, 809 props, 123 decals), 395 lights, 2561 hulls
world 'sp_tutorial_1' live: 1868 entities (185 brush bodies), epoch 1
loaded sp_tutorial_1 in 2.50s
```

## Runtime consequences

- Editor-built world and prop meshes have fitted Lumen surface-cache representations.
- The Sky Light uses the map's real cubemap with a black lower hemisphere, so local shadowing
  rather than flat ambient fill determines whether interiors see sky.
- `UElysiumLightRig::Adopt` still derives live light values from `.lights`; the bake owns native
  representation, not calibration.
- Nanite is representation infrastructure rather than a triangle-throughput requirement for these
  small maps.
- Performance measurements and their baseline live only in `docs/architecture/rendering-perf.md`.

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
- **Pruning must stay inside a stage-owned namespace.** World and sky share `/Meshes` but own
  separate `SM_World_` and `SM_Sky_` prefixes; prop meshes/data and particle `T_`, `MI_P_`, and
  `NS_` families are likewise swept independently. A failed owned-asset delete is a commandlet
  failure, not a successful zero-prune result.
- **A fresh commandlet has not indexed a new mount.** `does_asset_exist` reports False for assets
  already on disk and `create_asset` then trips the unattended overwrite guard. Scan the mount with
  `AssetRegistry.scan_paths_synchronous(force_rescan=True)` first — **and that is not always enough**:
  it still reports False for some assets present on disk, so anything the bake re-authors resolves
  by load-first-then-create, not by an existence check.
- **`unreal.EditorAssetLibrary.load_asset` logs a hard `Error` when the registry has no such asset**,
  which on a first bake is the normal path and makes a clean run report `Failure - 1 error(s)`. Use
  `unreal.load_asset` (LoadObject) for a load that is allowed to miss.
- **A bake that dies mid-run can still leave partial assets on disk.** Pending reports are not
  receipts: the outer driver promotes nothing unless every selected map passes commandlet, frozen-
  input, output-inventory, and independent-verifier checks. The previous verified recipes remain
  authoritative for the repair run.
- **An existing Niagara system can begin async compilation when loaded for replacement.** Force-
  deleting its package before that work drains can crash in `CoreUObject`. A standalone particle
  stage preloads its complete replacement set and calls `FAssetCompilingManager::FinishAllCompilation`
  before the first delete; minutes of preceding mesh work had previously hidden the race.
- **A dirty texture import must replace the existing asset in place.** Existence alone is not
  freshness; the source SHA-256 recipe decides. Replacement updates the one dirty package while
  every material and mesh reference remains valid. UE 5.8 exposes no usable `SourceFile.FileMD5`
  tag on these imports, so that tag remains diagnostic and the external SHA-256 is authoritative.
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

## Scope boundaries

Entity-driven sprites, ropes, NPCs, collision, scripting, and audio remain on the runtime side of
the split. Post-processing, fog calibration, horizontal map coverage, and packaged distribution
are tracked in `docs/project/roadmap.md`; this document does not mirror their status.

## Repro

```
uv run elysium export map sp_tutorial_1
uv run elysium export map sp_tutorial_1 --force
uv run elysium run play sp_tutorial_1
```

Requires the `GeometryScripting` and `ElysiumBaked` plugins (both enabled in the `.uproject`) and an
export of the map under `$ELYSIUM_EXPORT_ROOT/`. The export command generates the policy
assets, bakes the map, and verifies the saved packages. A map with no bake is refused by
`Travel` with the command to run. The running game holds the `.umap` open — quit before
forcing a re-export.
