# Effects authoring — the review loop

> Working note, not owned documentation. Like `seam_migration.md` it exists to keep one piece of
> work straight while it happens: which effects are authored, in what order, and what the owner
> said about each. The contract is `docs/architecture/effects-architecture.md` §5; the strategy
> behind it is `niagara_authoring_strategy.md`; the reference examples are `docs/examples/`.

## The loop (owner call, 2026-09-03)

One archetype at a time. Nothing scales until a human has looked at it.

1. **Base emitter.** Authored once in the editor under `Content/ElysiumAuthored/VFX/Base/`, stock
   modules plus the two small module scripts (ramp sampler, spherical offset). Readable.
2. **One generated system.** The bake composes `NS_<root>` for the archetype's representative root
   from that base emitter, headless, compiled, gated on readiness and a particle count.
3. **Contact sheet.** The system on a labelled pedestal in the witness level, captured at a fixed
   camera in Simulate-in-Editor and in-game, side by side, into
   `E:/elysium-work/scratch/effects/sheets/<root>/`.
4. **Owner verdict.** The owner looks at the images and says what is wrong in their own words. The
   verdict is recorded below with the screenshot paths. A rejection fixes the base emitter, not the
   generator. Only a recorded yes lets the archetype scale to every root it covers.

Mechanical failures (does not compile, does not activate, an emitter with zero particles, a
"Source emitter not found" line) never reach the owner; the bake refuses them.

## What the working maps place

Census 2026-09-03 (`E:/elysium-work/scratch/effects/roots_census.md`, staged live through
`importers.effects`). Three maps, 85 placements, 21 resolved roots, 1 unresolved
(`d_animalism_pestilence_cast_emitter`, the only one corpus-wide). Eighteen of the tutorial's 29
rows are `active=false` — a dev gallery — so **8 distinct roots actually run** on the working maps.
None of the three maps places `func_dustmotes`, `env_steam` or `env_beam`; the authored family
systems have no consumer here.

Corpus-wide: 164 placed roots, 1,401 placements over 79 maps. Archetypes A1 + A5 + A2 below cover
72 roots and 1,059 placements (76 %).

## Archetypes, in authoring order

| # | Archetype | Representative root | Placed (3 maps / corpus) | What the owner should see |
|---|---|---|---|---|
| A1 | Looping multi-leaf fire cluster | `BarrelFireEmitter` | 5 / 721 | An orange flame card rising out of the barrel at 30 fps, white to amber over its life; a soft grey glowing smoke plume above; a few tiny embers streaking up on 2–5 s lives; one slow invisible refraction card wobbling the heat. |
| A5 | Looping single-leaf plume | `SteamRelease_Constant_Emitter` | 2 / 212 | A narrow pale grey steam cone, ±10°, growing 13 to 100 cm, spreading then slowing, drifting up and rolling over 2 s. |
| A8 | Timer chain → single leaf | `SteamRelease_Timer` | 6 / 41 | The same jet, puffing on a random 3.3–6.7 s period. |
| A2 | Drip with collide → splash | `WaterDrops_Timer` | 41 / 126 | A dark 1:5 droplet falls fast, hits the floor, dies, and throws three tiny white points up in a cone while a ring decal lands. |
| A11 | Runtime-triggered splash pair (no map placement) | `waterbigsplash_emitter` | 0 / 0 — fired by code, not placed | A wide translucent white-blue splash card fanning out low over the water on a hard vertical entry (`waterbigsplash`, `WaterSplashes.tga`); a lighter double flash of a soft cloud puff and a bright pinpoint burst on a shallower wade-through hit (`watersplash_emitter`'s `WaterSplash`/`Splash` pair, `cloud.tga` + `point_16.tga`). |
| A3 | Precipitation with collide → splash | `rain_follow_emitter` | 2 / 30 | Sheeting rain around the player, splashing into expanding translucent rings, with one huge faint fog card. |
| A6 | Frame leaves on a path (moth) | `Moth_Emitter` | 5 / 33 | Two moths flutter on slow wandering loops, wings flicking through four frames eight times a second. |
| A9 | One-shot burst card | `MuzzleFlash_emitter_a` | 3 / 63 | A 38 cm white flash card, three at once, random roll, gone in 0.13 s. `_b` and `_up` are offset variants. |
| A10 | One-shot rate cluster (cast / impact) | `d_animalism_wolf_into_emitter2` | 8 / 72 | A blue-white ember haze filling a 1.5 m ball and rising away over 1 s. |
| A7 | Carrier sprite spawning children | `Airplane_Emitter` | 2 / 36 | A tiny plane crosses the skybox over 100 s, strobe blinking, red and green wingtip lamps steady. |
| A4 | Colliding leaf, no collide-spawn | `Fire1_emitter` | 2 / 67 | Fire1's `collide{}` is all defaults; A1 with a Collision module. Folds into A1 once A1 is approved. |

Other roots on the working maps map onto these: `Fire2_emitter`, `d_animalism_pestilence_emitter`,
`D_Potence_1BP_Emitter_Hand` → A1; `Rain_box_NoPrecip_emitter` → A5 (StartHidden); the remaining
tutorial casts and `molotov_emitter` → A10.

**Water (the water-complete pass, `ELYSIUM_WORK_ROOT/scratch/water_audit/PLAN.md` Phase 0,
U1d/U3/F1-F3, off the three-map census).** `sp_soc_3` (not
one of the three working maps) places `drip_emitter` 40 times — the single root on that map — which
is `WaterDrops_Timer`'s own chain minus the outer timer/rate wrapper (`drip_emitter` spawns `Drip`
directly; `WaterDrops_Timer` spawns `WaterDrops_Emitter` which spawns `Drip`), so it folds into A2
rather than earning its own row. `sm_hub_1` places `WaterDrops_Timer` 36 of its 55 rows (the map's
single most-placed root) and `sp_tutorial_1` places it too, both already inside A2's `41 / 126`
count. `waterbigsplash_emitter` and `watersplash_emitter` (A11) are placed by **no** `env_particle`/
`func_particle` entity on any of the five staged maps, `sm_pier_1` included — U1d re-ran the census
against the pier and found zero water-drip/splash placements there, confirming U3/AUDIT G7: the
water-entry splash is the client's water-level transition (`FUN_10099630`), not a map-authored
effect, so its `NS_<root>` exists only because the C++ runtime looks it up by name on the event
(D1, `AElysiumEffectActor`'s `NS_<Stem>` convention), never through `particleTrees{}` placement
data. All four water trees resolve cleanly (zero unresolved children, zero missing textures) —
`pipeline/tests/test_effects.py::test_the_four_water_splash_and_drip_roots_stage_with_their_sprite_leaves`.

The splash pair's spawn rule is the water lane's, not this one's: big splash on `waterLevel 0 → ≥1`
with `velocity.z < −200 in/s`, wade splash while `0 < level < 3` above 50 in/s on a
`5.0 − horiz·7.8e-5` s cooldown, at `origin − vel.xy·0.035` snapped to the surface plane
(`+RandomInt(0,8)` in z for the wade one) — `ElysiumWater::DecideSplash`,
`docs/architecture/water-architecture.md` §8. Reading `waterbigsplash_emitter` out of the retail
pack member is **named divergence 21** in that document's §12 (the Unofficial Patch's own copy has
its spawn token commented out, `// removed by wesp`), and it is the only key in
`exporters.particle_glb.RETAIL_PROVENANCE_DIVERGENCE_UNITS`. Until the generator runs,
`AElysiumMapActor::RaiseWaterSplash` stands the floor system rather than failing — by design, so a
checkout without the roots still classifies, sounds and moves.

## Todo

- [x] Checkpoint commit (f939f049); floor asset left at its committed state.
- [x] Docs carry the revised ruling (effects-architecture §5, seam_map_map, seam_migration R7.3/R9.2, world plan, authored-assets skill).
- [x] Generator lane: `pipeline/unreal/make_root_systems.py` + `UElysiumParticleAssetBuilder::BuildRootSystem` compose `NS_<root>` headless from `particleTrees{}`, compile, gate on `IsReadyToRun()`, save to `/Game/ElysiumGenerated/VFX/`. Spike passed 2026-09-03: 21 roots built in 43 s off the stock Fountain template, never opened in the editor; `NS_BarrelFireEmitter` activates on fresh load with particles in all four emitters (`sheets/barrelfire_fountain/`). Still open: ramps and collide→spawn writes, particle-count gate, bake wiring, `E_VtMBLeaf` base emitter.
- [x] Contact-sheet capture, SIE half: `E:/elysium-work/scratch/effects/sheet.py <NS path> --label X` (pedestal, label, fixed camera, two frames, counts.txt). In-game half still needs a fixed view pose via `elysium_player_teleport` + `elysium_screenshot`.
- [x] Black card explained by the A1 round-2 root cause (sprite masters read `VertexColor`, which Niagara hardcodes to white, so every card drew `tex.a` as opacity); confirm gone on the hub and the tutorial in the next play run.
- [x] A1 `BarrelFireEmitter` — landed 2026-09-03 (three review rounds): base emitter `E_VtMBLeaf`, generated `NS_BarrelFireEmitter`, sprite masters on `ParticleColor`, refraction master on a DUDV offset with a per-particle strength, the effect actor binding `NS_<root>`. Sheets `sheets/a1_shipped_stripes/`, `sheets/a1_shipped_dark/`. Owner's in-game look on the hub barrels still to come.
- [ ] A5 `SteamRelease_Constant_Emitter`
- [ ] A8 `SteamRelease_Timer`
- [ ] A2 `WaterDrops_Timer` — covers `drip_emitter` (`sp_soc_3`, 40 placements) too, same chain
  minus the outer timer. Lane F staged both trees, sprites bound (`dropletfast.tga`,
  `point_16.tga`), zero unresolved children: `pipeline/tests/test_effects.py::test_the_four_water_splash_and_drip_roots_stage_with_their_sprite_leaves`.
  `-RootSystems=waterdrops_timer,drip_emitter` on the Phase 3 command below builds both from one
  staged document.
- [ ] A11 `waterbigsplash_emitter` / `watersplash_emitter` — the water-entry splash pair (owner
  decision 5, verdict D1/F1). Not placed on any map; staged as a synthetic root pair (no
  `env_particle` entity backs it) so the generator can still build `NS_<root>` for the C++ runtime
  to look up by name. `waterbigsplash_emitter` only resolves because of the F2 retail-provenance
  divergence (`exporters/particle_glb.RETAIL_PROVENANCE_DIVERGENCE_UNITS`) — the Unofficial
  Patch's own copy is an authored-empty stub. Sprites bound: `waterbigsplash` → `watersplashes.tga`,
  `watersplash` → `cloud.tga`, `splash` → `point_16.tga`.
- [ ] A3 `rain_follow_emitter`
- [ ] A6 `Moth_Emitter`
- [ ] A9 `MuzzleFlash_emitter_a`
- [ ] A10 `d_animalism_wolf_into_emitter2`
- [ ] A7 `Airplane_Emitter`
- [ ] Scale: every root on the three maps, bake + verify, play run, Measured lines, Settled entry, retire `NS_ElysiumParticle` and the slot fitting.

### Water roots — the Phase 3 command (A2 + A11, four roots, one run)

Lane F staged `E:/elysium-work/scratch/effects/staged.json` with `sm_hub_1`, `sm_pier_1`,
`sp_soc_3`, `sp_tutorial_1`, `sm_pawnshop_1` and a synthetic `__water_runtime_splash__` entry
carrying the two splash trees (built with `importers.effects.TreeBuilder.build` directly, since
no map places them). Reproduce it with:

```
uv run python -c "
import json
from elysium_pipeline.exporters import UE_map_sidecars as MS
from elysium_pipeline.importers import effects as fx
from elysium_pipeline.importers import map_geometry as MG
from elysium_pipeline.importers import textures as texture_lane
from elysium_pipeline import paths

MAPS = ['sm_hub_1', 'sm_pier_1', 'sp_soc_3', 'sp_tutorial_1', 'sm_pawnshop_1']
out = {m: fx.stage_effects_for_join(MS.prepare_join(m), map_name=m) for m in MAPS}

reader = fx.particle_unit_reader(paths.export_v2_root())
read_texture = MG.texture_sidecar_reader(texture_lane.staging_root(paths.work_root()))
builder = fx.TreeBuilder(reader, read_texture)
trees = {}
for key, name in (('waterbigsplash_emitter', 'waterbigsplash_emitter'), ('watersplash_emitter', 'watersplash_emitter')):
    tree = builder.build(key, name)
    trees[tree['root']] = tree
assert not builder.unresolved_children and not builder.missing_textures
out['__water_runtime_splash__'] = {'particleTrees': trees}
json.dump(out, open('E:/elysium-work/scratch/effects/staged.json', 'w'), indent=1)
"
```

(`read_texture` is the same real staged-sidecar reader `stage_effects_for_join` defaults to for a
map -- not a fake path -- so the splash pair's sprites resolve to the same `/ElysiumBaked/Textures/
particles/T_<stem>` asset paths the sprite masters actually import: `watersplashes`, `cloud`,
`point_16` are staged already, per `E:/elysium-work/import/textures/particles/*.provenance.json`.)

Then, from the repo root:

```
uv run elysium run editor -- -run=pythonscript ^
  -script=pipeline/unreal/make_root_systems.py ^
  -RootSystemStaged=E:/elysium-work/scratch/effects/staged.json ^
  -RootSystems=waterbigsplash_emitter,watersplash_emitter,waterdrops_timer,drip_emitter ^
  -AllowCommandletRendering
```

builds `NS_waterbigsplash_emitter`, `NS_watersplash_emitter`, `NS_WaterDrops_Timer`,
`NS_drip_emitter` under `/Game/ElysiumGenerated/VFX/` in one run (`-RootSystems` matches on the
normalized key, the tree's own `name`, or the asset name, casefolded, so the four tokens above are
enough regardless of which map's placement staged a given tree). Needs `E_VtMBLeaf` landed (it is,
off A1) and `-AllowCommandletRendering` for `IsReadyToRun()` to gate for real rather than refusing
every root on `FApp::CanEverRender() == false`.

## Verdicts

One entry per archetype: date, root, sheet folder, the owner's words, what changed.

### A1 `BarrelFireEmitter` — 2026-09-03, round 3: heat card fixed, landed

- `M_V2_Refract`: refraction mode `RM_2D_OFFSET`; offset = `DuDvMap.rg` (normal sampler, `rg*2-1`) × `RefractAmount × DynamicParameter.x × 0.002` × the card's own alpha; opacity and base colour take `ParticleColor` like the sprite masters; the master now compiles a Niagara sprite permutation. `E_VtMBLeaf` carries a stock `DynamicMaterialParameters` module (`Index 0 Param 1` = two curves lerped by `Particles.MaterialRandom`, default 1). The generator binds `DuDvMap` to the leaf's `normal` sprite and writes the `refract` ramp into that module.
- Sheets: `sheets/a1_heat_final_stripes/` (stripe edges bend around the flame, nothing else moves), `sheets/a1_shipped_stripes/` and `sheets/a1_shipped_dark/` on the re-authored shipped materials.
- Also fixed on the way: the particle material instances could never be re-authored (created over an existing package; registry check blind in a commandlet); the generator read its arguments off `sys.argv` (empty under `-run=pythonscript`) and force-deleted loaded Niagara packages (crash).
- Owner's words: pending the in-game look.

### A1 `BarrelFireEmitter` — 2026-09-03, round 2: flame and smoke pass, heat card rejected

- Root cause of round 1 (found in the live editor): Niagara's sprite vertex factory hardcodes `VertexColor` to white (`NiagaraSpriteVertexFactory.ush`, `GetMaterialPixelParameters`), and the three sprite masters built tint and opacity from a `VertexColor` node, so every `red/green/blue/color` and `mask` ramp was discarded and each card drew raw `tex.rgb` / `tex.a`. Fixed in `pipeline/unreal/matgraph.py` (`Graph.particle_color()`) and `make_v2_materials.py` (sprite masters read `ParticleColor`); needs the materials rebake. This is also the in-game black card and the hard-edged authored steam.
- Sheets: `sheets/a1_round4_linear/sie_20260903T021608_t3s.png` (lit wall), `sheets/a1_round5_dark/sie_20260903T021821_t3s.png` (dark backdrop), `sheets/a1_round6_noheat/` (heat card off).
- Owner's words: "looking fine without heat card, the refraction card is completely wrong".
- Round 3: the heat card. `MI_ParticleRefract` gets a flat default DuDv map and a static strength; the `refract` ramp reaches nothing. Fix in progress: colour-sampled DuDv on `M_V2_Refract`, strength from a `DynamicParameter`, a `DynamicMaterialParameters` module on `E_VtMBLeaf`, the generator binding `DuDvMap` and writing the ramp.

### A1 `BarrelFireEmitter` — 2026-09-03, rejected (round 1)

- Sheets: `E:/elysium-work/scratch/effects/sheets/a1_barrelfire_generated/sie_20260903T014352_t3s.png`, `_t4s.png`; hand-set reference `sheets/a1_barrelfire_hand/sie_20260903T013509_t3s.png`.
- Known before the verdict: `Fire_Heat` (refraction card) draws nothing yet, its `refract` ramp is a material parameter not bound; the smoke reads as a dark blob against the witness wall (ten `mask 0.235` cards compounding), which is the faithful blend and would read as a soft dark plume in a night alley; no spherical-offset motion yet (not needed for A1).
- Owner's words, with two VtMB screenshots of the hub barrel (`E:/elysium-work/scratch/effects/reference/vtmb_barrelfire_close.png`, `_wide.png`): "vtmb fire on barrel is much more translucent, and the smoke above is almost translucent, the dark stuff above dont exists, and the heat wave refractions not really working".
- Round 2 must fix: flame translucency (soft, see-through, warm; not a saturated opaque ball), smoke nearly invisible (no dark blob: the mask is not reaching vertex alpha, or the alpha path is wrong), refraction card bound and visible.

### A1 `BarrelFireEmitter` — 2026-09-03, round 2 (root cause: the sprite masters read `VertexColor`)

- Sheets: `E:/elysium-work/scratch/effects/sheets/a1_round4_linear/sie_20260903T021608_t3s.png`
  (lit witness wall, judged on this one), `sheets/a1_round5_dark/sie_20260903T021821_t3s.png`
  (`sheet.py --dark`, the VtMB night alley), `sheets/a1_round6_noheat/sie_20260903T021902_t3s.png`
  (`Fire_Heat` disabled, isolating the refraction card's speckle).
- **Root cause of both colour complaints, one bug:** `NiagaraSpriteVertexFactory.ush`'s
  `GetMaterialPixelParameters` hardcodes `Result.VertexColor = 1` and fills only
  `Result.Particle.Color`. `M_V2_Sprite` / `M_V2_SpriteZ` / `M_V2_SpriteZLit` multiplied the tint
  and the opacity by a `VertexColor` node, so on a Niagara sprite Emissive and Opacity read white:
  every `red/green/blue/color`/`mask` ramp the generator writes into `Particles.Color` was
  discarded and the cards drew at `tex.rgb` / `tex.a`. Proven by rewriting the `ScaleColor` curves
  in the running editor and seeing no pixel move (`sheets/a1_round2/`), then by pointing the same
  system at a scratch `ParticleColor` master and seeing the whole plume change
  (`sheets/a1_round3_particlecolor/`).
- The generator's numbers were right all along: `FirePlace_Flames` `Scale RGB` peaks 0.235 with
  `Scale Alpha` flat 0, `Smoke1` `Scale Alpha` peaks 0.235 — verified through `GetStackInputData`.
  No C++ change was needed and none was made.
- Fix: `matgraph.Graph.particle_color()`, used by `_build_sprite` and `_build_sprite_z` in place of
  `vertex_color`. `UseVertexColor` / `UseVertexAlpha` keep their names. **Needs a materials-v2
  re-bake** — `_source_hash()` covers both files, so the three masters re-author themselves.
- Measured on the lit wall: smoke transmittance 0.15 → 0.87 (no dark blob); flame core sRGB
  0.79/0.49/0.11 → 0.51/0.32/0.20, against the VtMB reference's 0.65-0.77/0.35-0.44.
- Still open: the heat card. `ConfigureRenderer` binds only `BaseTexture`, so `MI_ParticleRefract`
  keeps `DuDvMap = DefaultNormal` (flat, an exact no-op) and a static `RefractAmount 20`; the
  `refract` ramp (3 → 0) reaches nothing. It renders as white speckle, not a shimmer
  (`a1_round6_noheat` is the same frame without it). Needs, together: a colour-sampled `DuDvMap`
  slot on `M_V2_Refract` (the VtMB `normal` slot is a plain sprite, `T_cloud`, not a normal bake),
  a `DynamicParameter` node driving `RefractAmount`, a `DynamicMaterialParameters` module on
  `E_VtMBLeaf`, and the `normal` texture + `refract` ramp written by the generator.
