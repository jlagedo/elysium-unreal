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
| A3 | Precipitation with collide → splash | `rain_follow_emitter` | 2 / 30 | Sheeting rain around the player, splashing into expanding translucent rings, with one huge faint fog card. |
| A6 | Frame leaves on a path (moth) | `Moth_Emitter` | 5 / 33 | Two moths flutter on slow wandering loops, wings flicking through four frames eight times a second. |
| A9 | One-shot burst card | `MuzzleFlash_emitter_a` | 3 / 63 | A 38 cm white flash card, three at once, random roll, gone in 0.13 s. `_b` and `_up` are offset variants. |
| A10 | One-shot rate cluster (cast / impact) | `d_animalism_wolf_into_emitter2` | 8 / 72 | A blue-white ember haze filling a 1.5 m ball and rising away over 1 s. |
| A7 | Carrier sprite spawning children | `Airplane_Emitter` | 2 / 36 | A tiny plane crosses the skybox over 100 s, strobe blinking, red and green wingtip lamps steady. |
| A4 | Colliding leaf, no collide-spawn | `Fire1_emitter` | 2 / 67 | Fire1's `collide{}` is all defaults; A1 with a Collision module. Folds into A1 once A1 is approved. |

Other roots on the working maps map onto these: `Fire2_emitter`, `d_animalism_pestilence_emitter`,
`D_Potence_1BP_Emitter_Hand` → A1; `Rain_box_NoPrecip_emitter` → A5 (StartHidden); the remaining
tutorial casts and `molotov_emitter` → A10.

## Todo

- [x] Checkpoint commit (f939f049); floor asset left at its committed state.
- [x] Docs carry the revised ruling (effects-architecture §5, seam_map_map, seam_migration R7.3/R9.2, world plan, authored-assets skill).
- [ ] Generator lane re-pointed: `make_particle_systems.py` + `UElysiumParticleAssetBuilder` read `particleTrees{}`, inherit from a base emitter, compile, gate on `IsReadyToRun()` + particle count, write to `Content/ElysiumGenerated/VFX/NS_<root>`.
- [ ] Contact-sheet capture: witness pedestal + fixed camera, SIE and in-game screenshots per root.
- [ ] Black card isolated in the hub (survives TurnOff; screen-space in the tutorial; suspect the depth-test-off material child).
- [ ] A1 `BarrelFireEmitter` — base emitter, generated system, sheet, verdict.
- [ ] A5 `SteamRelease_Constant_Emitter`
- [ ] A8 `SteamRelease_Timer`
- [ ] A2 `WaterDrops_Timer`
- [ ] A3 `rain_follow_emitter`
- [ ] A6 `Moth_Emitter`
- [ ] A9 `MuzzleFlash_emitter_a`
- [ ] A10 `d_animalism_wolf_into_emitter2`
- [ ] A7 `Airplane_Emitter`
- [ ] Scale: every root on the three maps, bake + verify, play run, Measured lines, Settled entry, retire `NS_ElysiumParticle` and the slot fitting.

## Verdicts

One entry per archetype: date, root, sheet folder, the owner's words, what changed.

(none yet)
