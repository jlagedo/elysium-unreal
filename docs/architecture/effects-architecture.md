# Effects — Unreal reproduction

VtMB's effect inventory, formats and producers are `docs/vtmb/effects.md`. This
document is the Unreal side: the owner call, the seam between reproduced logic and
modern presentation, the 5.8 assets worth opening first, and — §5 — the R7.3 ruling
the implementers build against.

The goal is not a sprite-for-sprite port of the 2004 Troika renderer. The goal is
that a player who remembers a barrel fire, clinic steam, warehouse dust, a flesh
hit or a Dominate stare still *reads* those moments, built with Unreal's own FX
stack.

---

## 1. Owner call

**Presentation is modernized. Logic is reproduced.**

- **Reproduced:** when an effect starts and stops; who it is parented to; the
  authored rate / burst / ramp; the explosion bundle's damage, light lifetime,
  shake and physics impulse; the impact-table lookup (surface × weapon → *some*
  hit FX); first- vs third-person attachment; I/O (`TurnOn` restarts, `TurnOff`
  stops feeding).
- **Modernized:** the look. A barrel fire may be a short Niagara Fluids 2D gas or
  a layered sprite system that uses Troika's rates and a new flame material. It
  does not have to composite `Flamemass.tga` additively into an 8-bit framebuffer.
- **One presentation, not two.** There is no faithful-sprite mode and an enhanced
  mode. Tuning lives on the one system. This matches the weather rule in
  `docs/vtmb/weather.md`.
- **A light an emitter never had is not "the look."** Modernizing the flame material
  is presentation; adding an emissive source that changes what the room reads as lit
  is a divergence and needs an explicit owner call. The reverse also holds, and it is
  the easier mistake: a light VtMB *does* throw is reproduced work, and dropping it is
  a divergence too. VtMB lights a muzzle flash and an explosion, from different
  places — `params_explosion` authors `dl_color` / `dl_radius` / `dl_time` /
  `dl_decay`, while a muzzle flash carries no authored light parameters and takes
  fixed constants off an entity effect bit (`docs/vtmb/effects.md` §3.4). Check the
  owning topic for a light before deciding an effect has none.

**R7.3's governing rules (owner, 2026-09-02).** *No fidelity dropped: wire everything the
data and the engine allow; what cannot be wired now is deferred with a named road, never
dropped.* And: *where a VtMB behaviour is an engine limitation rather than authored intent
and Unreal expresses the system better, chase the improvement* — a gib bounces on its own
hull, not VtMB's zero-size point; §5.11's ledger names each such modernization beside the
faithful behaviour. The generated per-root systems draw the faithful behaviour; a family
override is the modernization on top of them, one presentation per root, never two at once.

**Out of scope by owner decision (2026-09-02):** the main menu and the HUD are new
authored assets, not reproductions. The menu particle scene, `env_particle_hud` (3
rows), the `d_*_hud_*` / `hud_*` definitions and the screen-space attach modes (5, 7,
14, 16) are not this document's.

`.phy` collision stays what it is — Chaos hulls on the prop — and is not an FX
problem (`docs/vtmb/phy_vphysics.md`). `env_physimpact` / `env_physexplosion` stay
impulse sources. They are never "implemented as a particle." **The impulse itself is
`docs/architecture/physics-architecture.md`'s**, delivered through that document's
`AddBodyImpulse` / `AddRadialImpulse` seam; this document owns only what the explosion
looks and sounds like.

---

## 2. What already exists in this project

Do not rebuild these to explore the look.

| Piece | Where | What it does |
|---|---|---|
| V2 particle lane | `exporters/particle_glb.py`, `formats/particle_glb/`, `validation/particle_glb.py` | one GLB unit per definition, tolerant, every key decoded; wired into `export_v2` (`GLB_SEAMS` step `particle`); its projection contract is `seam_map_particle.md` → "Semantics" |
| Legacy compiler | `formats/particles.py::compile_definition` | strict closed vocabulary; 42 of the 155 placed roots never resolve (the top four fail: `fire2_emitter` 204 rows, `fire3_emitter` 140, `barrelfireemitter` 115 on every map, `moth_emitter` 31) — **legacy lane only** |
| Per-map sidecar | `<map>.particles.json` | placed `env_particle` + closure + `unresolved`; written for all 108 maps by `UE_bsp_to_scene.py`; `bake_map_v2.py` never reads it — **legacy lane only** |
| Niagara generator | `pipeline/unreal/make_particle_systems.py` → `UElysiumParticleAssetBuilder` | today: one Fountain-based emitter per drawing leaf, `NS_<root>` per map, on the legacy `M_Additive`; built on `UNiagaraExternalEditUtilities` — **the lane the generator extends** (§5.3 re-points it at `particleTrees{}` and at the project's own base emitters) |
| `env_particle` runtime | `FElysiumEnvParticle` (`ElysiumWeatherClasses.cpp`), `AElysiumMapActor::ApplyEmitter` (`ElysiumMapActorWeather.cpp`) | end to end: `TurnOn`/`TurnOff`/`SetRateScale`/`SetRampTime`/`SetAttachType`, the linear ramp, attach 0/1/2, start-off, parent follow; lazily creates one `UNiagaraComponent` per entity index and sets `User.RateScale` — only exercised by rain today |
| Rain presentation | `/Game/ElysiumAuthored/VFX/NS_ElysiumRain` | tracked authored system (`RateScale`, `SpawnCenter`, `BoundsCm`, `LightResponse`, `Streak*`, `RainStreakMaterial`, `RainMistMaterial`); the follow-rain path (`RefreshFollowRain`) |
| Melee weapon trail | `/Game/ElysiumAuthored/VFX/NS_ElysiumMeleeTrail` | tracked authored ribbon; `ElysiumMeleeTrail.cpp` drives `User.TrailPointA/B` |
| Sprites (R6.1) | the staged `sprites[]` table → one `AElysiumSpriteActor` per row in the baked level | `env_sprite` coronas, shafts, candles, flashers, lightning — **the placement pattern R7.3 copies** |
| Masters | `M_V2_Sprite` (`used_with_niagara_sprites`, Translucent, depth-test-off, blend a per-instance override), `M_V2_Refract` | the Niagara-sprite master and the DUDV master (`seam_map_material.md`) |
| Fog on non-primitive materials | `ElysiumFog::ApplyToDecalMID` | packs `FogColor`/`FogStart`/`FogInvRange` into named MID parameters — the pattern a Niagara renderer material takes; `make_v2_materials.py` already zeroes the fog colour under additive blending, as Source does |
| Decals | `<map>.decals` → `UDecalComponent` | authored `infodecal` |
| `env_fade` | real entity class | screen fade |
| Prop `.phy` | `props/<stem>.phys` → Chaos | the barrel's *body*, not its fire |
| Stubs | `ElysiumStubClasses.cpp` | `func_particle`, `env_physimpact`, `env_physexplosion`, `env_shooter`, `env_shake`, `point_explosion`, `env_particle_hud` — I/O logged, nothing draws or impulses |
| No code at all | — | `params_particle`, `params_explosion`, `func_dustmotes`, `env_steam`, `env_beam` — inputs fall to the generic unresolved path |
| Physics impulse seam | `physics-architecture.md` §7 | designed, not built: `AddBodyImpulse` / `AddRadialImpulse` exist in no source file |
| Camera shake | `AElysiumPlayerCameraManager` | the engine's shake modifier slot is kept; no shake class, no caller |
| Damage | `ElysiumDamage` | the combat path; no radial call site |

The corpus (108 maps): `env_particle` 1,304 (155 roots), `params_particle` 227,
`func_particle` 98, `func_dustmotes` 82, `env_beam` 47, `env_steam` 11, `point_explosion` 93,
`params_explosion` 52, `env_physexplosion` 61, `env_physimpact` 130, `env_shake` 60,
`env_shooter` 21. Placed roots by intent (rows / maps): **fire** ≈ 550 (`fire2` 204/6, `fire3`
140/13, `barrelfire` 115/18, `fire1`, `torch`, `gasoline_fire`, `firegreen`, `fireplace`,
`trem_firewall`); **drips & water** ≈ 195 (`waterdrops_timer` 78/8, `drip` 48, the waterfall
and mist family); **blood** ≈ 55; **distant lights** (`carlight_emmiter` 83/16,
`citylights_emitter` 28/14, `airplane` 22/21); **motes** (`starynight` 27/22, `moth` 31/10,
`fly`, `glowywierd`); **steam** 36; **smoke** 18; **rain follow** 8; the warehouse blast family
≈ 25; one-offs ≈ 60.

---

## 3. The Unreal toolkit (this 5.8 install)

Everything below is already on disk at `D:\Epic\UE_5.8`, verified against the engine source
(2026-09-02). Open it in the editor; do not add Marketplace packs or Starter Content to the
game project to explore.

### 3.1 Niagara sprite / mesh / ribbon — the default

Plugin: `Engine/Plugins/FX/Niagara`.

**Programmatic authoring is `UNiagaraExternalEditUtilities` only** — banner-marked
experimental, C++-only, no `UFUNCTION`; there is no `UNiagaraSystemBlueprintLibrary`. The
stable surface is **hand-authored systems + runtime user parameters**:
`UNiagaraComponent::SetVariableFloat / Int / Bool / Vec2 / Vec3 / LinearColor / Object /
Material / Texture / TextureRenderTarget`, and the array data-interface setters
(`UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector` and family). A user
parameter can swap the sprite texture and the material at runtime; a renderer's material
can be bound to a user parameter, and its `MaterialParameters` binding writes Niagara
variables into the material's parameters (one MID per emitter).

Templates to open first (`/Niagara/DefaultAssets/Templates/`) — present in this install:
`Emitters/` `BlowingParticles ConfettiBurst DirectionalBurst DynamicBeam Fountain
HangingParticulates LocationBasedRibbon Minimal OmnidirectionalBurst RecycleParticlesInView
SimpleSpriteBurst SingleLoopingParticle StaticBeam UpwardMeshBurst`; `Systems/`
`AttributeReaderTrails DirectionalBurst(Lightweight) FountainLightweight MinimalLightweight
RadialBurst SimpleExplosion`.

| Asset | Kind | Use for |
|---|---|---|
| `Emitters/Fountain` | looping sprite column | fire plume, steam, fountain, ash |
| `Emitters/SimpleSpriteBurst` | one-shot sprites | muzzle, impact, blood hit, flash |
| `Emitters/OmnidirectionalBurst` | radial burst | explosion core, light-bulb pop |
| `Emitters/DirectionalBurst` | aimed burst | shotgun, steam jet, vomit, blood strike |
| `Emitters/HangingParticulates` | sparse slow motes | warehouse dust, star-night, moths (motion swapped) |
| `Emitters/BlowingParticles` | wind-carried | ash, cigar, paper, mist |
| `Emitters/ConfettiBurst` | many short cards | glass / wood chips (re-material) |
| `Systems/SimpleExplosion` | burst + debris | `params_explosion.particle` family override |
| `Emitters/DynamicBeam` / `StaticBeam` | beam | `env_beam` (`NS_ElysiumBeam` derives from `DynamicBeam`) |
| `Emitters/LocationBasedRibbon` | ribbon | tracers, blood return, Celerity trail |
| `BehaviorExamples/SpriteFacingAndAlignment` | facing | `movealign` (rain streaks, embers) |
| `BehaviorExamples/SubUVAnimation` | flipbook | candle, muzzle sheet, explosion sheet |
| `BehaviorExamples/KillParticles` + `Collision/*` | collide | rain splash, blood decal, sparks on floor |

Modules that map 1:1 onto Troika keys (`/Niagara/Content/Modules/`); §5.3's base emitters are
built from them and the generator writes their inputs, the family systems use them as authored:

| Troika idea | Niagara module |
|---|---|
| `rate` | `Emitter/SpawnRate` |
| `burst` | `Emitter/SpawnBurst_Instantaneous` |
| `radius` / `theta` / `phi` | `Spawn/Location/SphereLocation` or `ConeLocation` |
| `X/Y/Z_speed` | `Spawn/Velocity/AddVelocity` |
| `radius_speed` / `elevation_speed` | `Update/Forces/AccelerationForce` + radial, or Fluids |
| `movealign` | `Update/Renderers/Sprite/SpriteFacingAndAlignment` (velocity) |
| `size` / `height` ramp | `Update/Size/ScaleSpriteSize` |
| `red/green/blue` / `color` / `mask` | `Update/Color/Color` + `ScaleColor`; `mask` is the AlphaComposite interpolant |
| `depth_offset` / `sortfront` / `no_z_test` | the per-particle camera offset module; the renderer's `SortOrderHint` / translucency sort priority; a material with the depth test off — each has a native home |
| `collide` spawn | CPU collision (scene queries against the world and any collision-enabled body, bounce/friction) → `Events/GenerateCollisionEvent` → `ReceiveCollisionEvent` |
| `collide` decal | the collision event → `UElysiumDecalSubsystem::Lay` with the particle's own sprite texture (`FElysiumDecalRequest::Texture`, an MID off `M_V2_Decal` with `BaseTexture` bound) and a lifetime. **Not** `UNiagaraDecalRendererProperties`: a laid stain outlives the particle that laid it (R7.2 owner call B) |
| `both` nodes / child at the parent particle | `GenerateLocationEvent` → `ReceiveEvent` spawn between emitters of **one system** (events do not cross system instances — why a root's leaves are emitters of one generated system) |
| bone / point attach | `Spawn/Location/SocketLocation` / `SkeletalMeshLocation` |
| parent follow | `Update/Position/InheritSourceMovement` (`parent_speed`) |
| heat haze | the same sprite on `M_V2_Refract` with the `normal` sprite as DUDV — wired, not dropped |

Two production facts §3 used to omit:

- **Niagara Data Channels are production** (main `Niagara` module):
  `UNiagaraDataChannelLibrary::WriteToNiagaraDataChannel`, `UNiagaraDataChannelAsset`, and the
  **islands** spatial channel (`NiagaraDataChannel_Islands`). The C3 fallback in §5.1.
- **Scalability is production**: `UNiagaraEffectType` + `FNiagaraSystemScalabilitySettings`
  (distance cull, max instances), `UNiagaraSignificanceHandlerDistance`,
  `UNiagaraComponentPool` via `SpawnSystemAtLocation(..., PoolingMethod)`. §5.5 ships one
  effect type with the cull **off**.

**`BLEND_AlphaComposite`** (premultiplied alpha) is `src + dst × (1 − srcAlpha)` — exactly
VtMB's `$spriterendermode 8` (`docs/vtmb/effects.md` §2.4). With `Emissive = tex.rgb × colour`
and `Opacity = tex.a × mask`, the additive↔translucent knob transcribes without a second
material. **Fog on translucent sprites is automatic** for the engine's height fog
(`bUseTranslucencyVertexFog` defaults true; `bComputeFogPerPixel` exists); the project's own
fog term (`ElysiumMapVisuals` / `ElysiumFog`) is not the engine's and needs the MID pattern
(§5.4). Lumen lights translucency through `r.Lumen.TranslucencyVolume.*`;
`TLM_VolumetricPerVertexNonDirectional` is the lit-sprite mode. The **Niagara light
renderer** emits `FSimpleLightEntry`, honoured by MegaLights when `bAllowMegaLights`; keep
them few and small — cheaper than a `UPointLightComponent`, not a replacement for one.
**Per-particle texture selection**: a `Texture2DArray` data interface, or the renderer's
`MaterialParameters` binding (per-emitter MID); Troika sprites are single images, not sheets.

Epic's own sprite-smoke walkthrough is the proven recipe a *family override* for steam /
cigar / ash copies, then retunes:

<https://dev.epicgames.com/documentation/unreal-engine/how-to-create-a-smoke-effect-using-sprite-particles-in-niagara-for-unreal-engine>

Spawn Rate 50, random lifetime 2–3 s, random sprite 75–200, upward velocity, sphere spawn
radius, SubUV on an 8×8 smoke sheet, Z acceleration. That *is* Troika `Smoke1` /
`SteamRelease_Constant` with a better material.

### 3.2 Niagara Fluids — hero fire and smoke only

Plugin: `Engine/Plugins/FX/NiagaraFluids` (already present).

Epic's own split: **2D templates are for games, 3D templates are for
cinematics.** This project's hardware floor is an RTX 4060 at 1440p with Lumen +
MegaLights already on. 3D gas is a cutscene / one-off boss tool, not a barrel.

Open these, in this order (`/NiagaraFluids/Content/Templates/`):

| Asset | When to try it |
|---|---|
| `Gas/2D/Systems/Grid2D_Gas_SmokeFire` | **first fire.** One 2D grid, smoke + flame. Matches a barrel / stove / gasoline pool better than 3D. |
| `Gas/2D/Systems/Grid2D_Gas_Smoke` | steam plume, cigar, chimney, Animalism ground fog |
| `Gas/2D/Systems/Grid2D_Gas_MovingFire` | gasoline trail, wall-of-fire, Molotov path |
| `Gas/2D/Systems/Grid2D_Gas_Explosion` | warehouse / barrel *look* (the recipe in §5.10 still owns damage / shake / impulse) |
| `Gas/2D/Systems/Grid2D_Gas_Color` | coloured gas (Presence, Dominate, Auspex-adjacent) |
| `Gas/3D/Systems/Grid3D_Gas_Fire` | **do not place in a hub.** Reference only, or a theatre beat |
| `Gas/3D/Systems/Grid3D_Gas_Smoke` | same |
| `Gas/3D/Systems/Grid3D_Gas_Explosion` | same |
| `Liquid/2D/Systems/Grid2D_FLIP_Splash` | water hit, rain splash hero (rain itself stays sprites) |
| `Sand/Systems/SandSystem` | skip; no sand family in the corpus |

2D gas is a camera-aligned grid. It reads as volume from most gameplay cameras and
costs a fraction of 3D. A Santa Monica hub with seventeen barrel fires cannot run
seventeen 3D sims; it can run seventeen small sprite systems, or a handful of 2D
grids for the ones the player is next to.

The Baker is `UNiagaraBakerSettings` / `UNiagaraBakerOutputTexture2D` (atlas bake),
editor-only, no commandlet — there is no `FlipbookBaker`.

### 3.3 Not Niagara

| Intent | Unreal primitive | Notes |
|---|---|---|
| Coronas / volume-light shafts / candle / cop flash / lightning | `UElysiumSpriteComponent` on `AElysiumSpriteActor` | **All 6,449 `env_sprite` are drawn, coronas included (owner, 2026-09-02):** one billboard actor per entity on `M_V2_Sprite`, I/O driving visibility; rendermode 3/9 keep Source's glow rule with a per-corona GPU occlusion query. `seam_map_map.md` → "Sprites (R6.1)". |
| Authored decals | `UDecalComponent` | already baked |
| Runtime blood / rain stains | `UElysiumDecalSubsystem::Lay` (`ElysiumDecalSubsystem.h`) — a pooled `UDecalComponent` under the subsystem's own hidden actor, capped by `UElysiumSurfaceSettings::MaxLaidDecals` and recycled oldest-first | particle `collide.decal`, the gib blood; the collision event is R7.3's, the decal spawn landed in R7.2 with the ranged shot's `ElysiumImpactDecals::BuildImpactRequest` as its first caller |
| Screen fade | `env_fade` (done) | |
| Camera shake | `UCameraShakeBase` / `UCameraShakePattern` are **base Engine**; the Perlin, wave and `ULegacyCameraShake` patterns live in the **EngineCameras plugin** | `env_shake` and `params_explosion.shk_*` take a `UCameraShakePattern` subclass transcribing `CalcShake` (§5.10), played through `UGameplayStatics::PlayWorldCameraShake(Epicenter, InnerRadius 0, OuterRadius = radius, Falloff 1)` — **exactly** `UTIL_ScreenShake`'s linear falloff |
| Physics kick | the impulse seam in `physics-architecture.md` §7 (`UPrimitiveComponent::AddImpulse` / `AddImpulseAtLocation` / `AddRadialImpulse` underneath) | `env_physimpact` / `env_physexplosion`; `URadialForceComponent` and `ApplyRadialDamageWithFalloff` are **not** used — their curves are not VtMB's |
| Gibs | spawn the gib `SM_*` as a Chaos body with an impulse | `env_shooter` names `models/gibs/*` — props-lane units |
| Explosion light | `UPointLightComponent`, radius and intensity driven per tick from `dl_time` / `dl_decay` | do not put this inside Niagara; a Source dlight casts no shadow |
| Auspex / heat vision | post-process material | not an emitter |
| Obfuscate translucency | mesh opacity / custom depth | trait flag, not a particle |
| Celerity motion trail | Niagara ribbon on the body | `Fx_Motion_Trail` |
| Map fog / water | existing height fog + Single Layer Water | not this system |
| HUD / view-space FX, menu background | **out of scope** (owner, 2026-09-02) | new authored assets, not reproductions |

### 3.4 What not to reach for

- **Cascade.** The plugin is still in 5.8 for conversion. Do not author new Cascade.
- **Heterogeneous Volumes / Sparse Volume Textures.** Cinematic smoke. Wrong cost class
  for a 20k-triangle night street with hundreds of dynamic lights.
- **Niagara 3D Fluids on every placed emitter.** Same reason.
- **The NiagaraToolsets MCP for shipping assets.** It is an inspection and one-off tweak tool
  for authored assets: no compile call, no batch, a fresh system view model per call, and it
  cannot touch lightweight emitters or data channels. `UNiagaraExternalEditUtilities` itself
  **is** the generator's API (§5.3) — the experimental banner is acknowledged and the risk is
  taken deliberately, because the generator runs headless in the bake, over regenerable
  output, and the alternative (Epic's conversion-context / clipboard path) is reachable from
  the same generator if the API moves.
- **A second "faithful" material path.** The generated systems draw Troika's sprites on the V2
  masters (§5.4); the family override is the modernized art. There is no third.

---

## 4. Family → Unreal stand-in

A **generated `NS_<root>` draws every root** at R7.3 (§5.3); each row is what the *family override*
(`DA_EffectFamilies`, §5.5) opens when the tuning session authors that family.

| Family (`docs/vtmb/effects.md` §4) | First thing to open | Fallback if it is too expensive or too much look | Drive with |
|---|---|---|---|
| Fire / embers (barrel, stove, gasoline) | `Grid2D_Gas_SmokeFire` | Fountain × 3 (flame, smoke, ember) using the official sprite-smoke recipe for the plume | the staged tree: `barrelfireemitter` flame 40/s, smoke 5/s, ember 5/s; `Flamemass` / `FirePlace_Flames` / `Smoke1` / `FlameEmbers1` identify fire across all 200 fire files |
| Steam (clinic `env_steam` + Troika steam) | `NS_ElysiumSteam` (§5.6) for the Valve class; Fountain or `Grid2D_Gas_Smoke` as the Troika override | DirectionalBurst looping | `steam[]` rows; Troika `rate` 50 |
| Smoke / cigar / ash | Epic sprite-smoke how-to, then `BlowingParticles` | Fountain | the staged `radius_speed` / `elevation_speed` |
| Rain / drips | existing `NS_ElysiumRain` | — | weather's; the drips are generated roots |
| Lightning | `env_sprite` blink (already authored as timers) + a light pulse | — | timer graph in `docs/vtmb/weather.md` |
| Beams | `NS_ElysiumBeam` (§5.6) | — | `beams[]` rows |
| Dust motes / night motes | `NS_ElysiumDust` (§5.6) for `func_dustmotes`; `HangingParticulates` as the `starynight_emitter` override | Fountain at rate 30 | `dustmotes[]` rows; the generated system for the Troika motes |
| Moths / flies | `HangingParticulates` + `CurlNoiseForce` | simple sprite with a wandering velocity | `moth_emitter` (its `timescale` is a staged field now) |
| Blood hit / spray | `DirectionalBurst` | `SimpleSpriteBurst` | impact table + the staged blood trees |
| Blood trail / return | `LocationBasedRibbon` | sprite streak with `movealign` | Thaumaturgy return emitters |
| Muzzle flash | two `SimpleSpriteBurst` systems (flash + smoke) on the weapon's muzzle socket, **plus** a one-frame point light on the shooter's attachment 1 | SubUV flash sheet | item `muzzleflash_particle` / `muzzlesmoke_particle` (`view*` in first person) + events 5001/5003; the light is 255/192/64 at 254 cm off the `m_fEffects` bit |
| Tracers | ribbon | `movealign` sprite | `bullettrail_emitter` |
| Surface impact | `SimpleSpriteBurst` per surface group | one generic burst tinted by surface | `particleimpacttable.txt` (client-side, surface char × weapon column) |
| Explosion look | `SimpleExplosion` or `Grid2D_Gas_Explosion` | OmnidirectionalBurst | `params_explosion.particle` (`explosion2_emitter` 25 of 52) |
| Explosion light / damage / shake / impulse | point light + `ElysiumDamage` radial + `UElysiumCameraShakePattern` + the impulse seam | — | §5.10 |
| Gibs | spawn the baked gib mesh, Chaos impulse | Niagara mesh renderer | `env_shooter.shootmodel` |
| Dialog Dominate / Presence auras | small looping aura (Fountain or `Grid2D_Gas_Color`) attached to speaker/listener | sprite halo | created by name from the discipline record walker (`vampire.dll 0x101dd090`) through §5.9 — **`params_particle` is a precache stub and drives nothing** |
| Discipline body FX | the generated system through §5.9; overrides per family | — | `Particle_FirstPerson` / `ThirdPerson` / stats.txt (`plans/gameplay.md`) |
| Obfuscate / Celerity / Auspex vision | material / ribbon / post-process | do not fake with sprites | trait flags |
| Screen fade | `env_fade` | — | already a class |
| Runtime stain | Niagara decal or `UDecalComponent` | — | `collide.decal` → R7.2 |

---

## 5. The R7.3 ruling — the contract

Owner rulings, 2026-09-02 (the options note that carried them is folded in here and deleted):
**A1** stage off the V2 particle unit; **B3** the generic presentation with family overrides;
**C1** one actor per row in the baked level; **D–H** as below; **P1** the two impulse methods
land with the explosion bundle; order — **the ambient set first** (stage product, the generic
presentation, effect actor, `env_particle` retargeted, `func_particle`, dust / steam / beam,
on the three working maps), **then the explosion bundle** with P1 on `sm_junkyard_1` /
`sm_warehouse_1`.

**B3, revised (owner call, 2026-09-03).** The 20-slot generic floor `NS_ElysiumParticle` is
**retired**. It failed on four counts, all structural: it never compiled outside the Niagara
editor (the toolset builds its view model with no auto-compile and `Activate` then defers
silently); the per-instance parent/child readers it needed are impossible by engine design
(`FNiagaraDataInterfaceEmitterBinding::ResolveHandle` requires the data interface's outer to
be the system, and a user-parameter interface is copied onto the component); its fixed 8-root
/ 12-child slot layout dropped drawing leaves on placed roots; and at 660 modules and ~3,000
expression strings in one 37 MB blob nobody could read it. In its place: **one generated
Niagara system per placed root**, `NS_<root>`, written by the bake into the gitignored
`Content/ElysiumGenerated/VFX/` and composed headlessly by `UElysiumParticleAssetBuilder`,
one emitter per drawing leaf inherited from a hand-authored **base emitter** for the leaf's
archetype (§5.3). `DA_EffectFamilies`, `ET_ElysiumEffects`, the four `MI_Particle*` children
and the authored family systems `NS_ElysiumDust` / `NS_ElysiumSteam` / `NS_ElysiumBeam` are
unchanged, and so is the rest of the contract: A1, C1, D–H, P1 and the order above stand.
The research behind the revision is `docs/project/niagara_authoring_strategy.md` and
`docs/examples/`; the per-archetype owner verdicts are recorded in
`docs/project/effects_authoring.md`.

### 5.1 The data path

The map stage publishes, per converted map, `effects[]`, `particleTrees{}`, `dustmotes[]`,
`steam[]`, `beams[]` — every field named, in cm / degrees / seconds / 0..1, with its source —
and the V2 bake places one actor per row. That product is the contract
(`seam_map_map.md` → "Import — effects (R7.3)"); the unit rules it converts on are
`seam_map_particle.md` → "Semantics". The legacy `<map>.particles.json` lane and its per-map
`/ElysiumBaked/<map>/Particles/NS_<root>` flatten keep serving every map not on
`MapsOnV2Models`, byte for byte, until R9; the V2 systems of the same name live map-
independently under `/Game/ElysiumGenerated/VFX/` (§5.3) and never collide with them.

**C3 fallback, recorded:** one Niagara Data Channel world system per map, actors writing
islands entries — if C1's instance count ever hurts (≈ 12 rows per map; `sm_hub_1` is the
test). Not the first cut.

### 5.2 `AElysiumEffectActor`

One actor per `effects[]` row, `elysium.effect` + `elysium.ent=<index>`, root component a
`UNiagaraComponent`. The fields **are the row's fields, same names** (the bake writes them,
`bake_verify` reads them back):

```cpp
UCLASS() class AElysiumEffectActor : public AActor
{
    UPROPERTY() TObjectPtr<UNiagaraComponent> Niagara;     // the root component
    UPROPERTY() int32   EntityIndex = INDEX_NONE;          // effects[].index
    UPROPERTY() FName   Classname;                         // env_particle | func_particle
    UPROPERTY() FString Root;                              // vtmb:particle:<key>
    UPROPERTY() FString RootName;                          // as the entity spelled it
    UPROPERTY() int32   AttachType = 0;                    // the 19-value enum; func_particle 15
    UPROPERTY() FString ParentName;  FString AttachBone;  int32 AttachPoint = 0;
    UPROPERTY() bool    bActiveAtSpawn = true;  bool bStartHidden = false;
    UPROPERTY() float   SpawnBoundsCm = 1300.48f;          // spawnbounds × 2.54
    UPROPERTY() float   RampScale = 1.f;  float RampTime = 0.f;
    UPROPERTY() FBox    BoundsCm;                          // func_particle: the brush AABB (world)
    UPROPERTY() float   VolumeScale = 1.f;                 // func_particle: clamp(vol × 2^-21, 0.01, 100)
    UPROPERTY() FElysiumParticleTree Tree;                 // particleTrees{}[Root], nodes as staged
    UPROPERTY() TSoftObjectPtr<UNiagaraSystem> FamilySystem; // DA_EffectFamilies match, else null
    // The m_fRed/Green/BlueScale, m_fMaskScale, m_fSizeScale fields: no keyfield, 1 by default,
    // written by code producers (the impact spawn's 0.8). Not staged.
    UPROPERTY() FLinearColor Tint = FLinearColor::White;  float SizeScale = 1.f;
};
```

`FElysiumParticleNode` mirrors a `particleTrees{}.nodes[]` row field for field
(`Kind`, `Parent`, `Via`, the lifetimes, every ramp as `TArray<FElysiumRampKey>` of
`{T, Lo, Hi}`, the spawn block, the flags, `DepthOffsetCm`, the sprite and normal texture
paths with `Aspect`, the collide record). The staged tree stays on the actor because it is
the row's own data — the bake writes it, `bake_verify` reads it back, `DA_EffectFamilies`
pins read from it — but **the actor no longer transcribes it into the system**.

**Which system the actor plays.** `FamilySystem` if `DA_EffectFamilies` matched; otherwise
`/Game/ElysiumGenerated/VFX/NS_<root>` (§5.3), resolved by root key through
`DA_ElysiumParticleTrees` (§5.9) and set on the component with `SetAsset`. Every leaf's
numbers, curves, sprite, material and child relation are already **inside** that asset, so
the actor writes **system-level user parameters only** — `User.RateScale`, `User.Tint`,
`User.SizeScale`, `User.SpawnShape`, `User.SpawnBoxMin` / `Max`, `User.SkeletalMesh`,
`User.RootLifetime`, `User.RootLoop` and the fog three. The ~200 lines of per-slot fitting in
`AElysiumEffectActor::WriteTree` (slot assignment, the ramp lookup tables, the per-slot
texture and material writes) go away with the floor; what remains is the system-parameter
write and the attach-mode spawn-shape switch. A root with no generated system is the same
once-logged warning as an unresolved root, and the actor draws nothing.

**Runtime behaviour** (VtMB's, `docs/vtmb/effects.md` §2.4, §3.1):

| Call | What it does |
|---|---|
| `TurnOn()` | **restart**: re-resolve the parent (deferred to the first `TurnOn` as today — the parent may be created by a trigger), re-attach, `Niagara->ResetSystem()` + `Activate(true)`; VtMB's client rebuilds the emitter whenever the activation timestamp changes |
| `TurnOff()` | **let finish**: `Niagara->Deactivate()` — spawning stops, live particles finish on their own timeline (VtMB's stop clears `loop`, sets flag 0x200, feeding stops) |
| `Kill()` | `DeactivateImmediate()` + hidden; the entity is removed |
| `SetRate(float RateScale)` | writes **the one rate float** `User.RateScale = RateScale × VolumeScale` — the same float `rate` and `burst` both multiply in VtMB; `SetRateScale` / `SetRampTime` reach it through the substrate's linear approach, `func_particle`'s `volume_scale` is folded in here, the rain follow drives the same name on `NS_ElysiumRain` |
| `SetAttachType(int32)` | re-attach with the new mode (§5.7) |
| `SetTint(FLinearColor)`, `SetSizeScale(float)` | the five scale fields → `User.Tint`, `User.SizeScale` (code producers only) |

**How the substrate drives it — by entity index.** `FElysiumEnvParticle` keeps the I/O and
the ramp exactly as today (`InputSetRateScale` → linear approach from `RampStartScale` to
`RampTargetScale` over `RampDuration = ramp_time`; `RateAt(Now)` published every tick while
ramping) and publishes `FElysiumWeatherEmitterState` through `IElysiumWeather::ApplyEmitter`.
On a `MapsOnV2Models` map `AElysiumMapActor::ApplyEmitter` **retargets** from "lazily create
a component" to "drive the placed actor": `EffectActors[Entity.Index]` (bucketed by class in
`AdoptBakedLevel`, like `SpriteActors`), `bActive` rising → `TurnOn()`, falling → `TurnOff()`,
`RateScale` → `SetRate()`, `AttachType` → `SetAttachType()`; `RemoveEmitter` → `Kill()`. An
index with no actor (an unresolved root: VtMB removes the entity) is a once-logged warning,
the input still accepted. Two corrections the retarget lands in the leaf: the keyfield is
**`spawnbounds`** (`m_fSpawnBounds`), not the FGD's `bounds`, which no engine code reads;
and `JetLength` is accepted as VtMB's own no-op (logged once). `FElysiumFuncParticle` derives
from the leaf, forces `AttachType = 15` at activation, and carries the `TurnOn` / `TurnOff`
pair.

### 5.3 `NS_<root>` — the generated systems

**One Niagara system per placed root**, `NS_<root>`, generated by the bake into
`/Game/ElysiumGenerated/VFX/` (gitignored, regenerable, never hand-edited), effect type
`ET_ElysiumEffects` (§5.5). 155 placed roots today, 1,698 when the corpus is scaled; the
generated assets are a DDC and editor cost, not a frame cost (§7).

**The generator.** `pipeline/unreal/make_particle_systems.py` → `UElysiumParticleAssetBuilder`
— the lane that already exists (§2), re-pointed twice: from the legacy `<map>.particles.json`
flatten to the staged `particleTrees{}` product, and from the stock Fountain template to the
project's own base emitters. It composes headlessly over `UNiagaraExternalEditUtilities`,
**never opens the Niagara editor**, and runs inside the bake like every other generated asset.
If that API moves under us, Epic's own conversion-context / clipboard path
(`create_system_conversion_context` → `add_template_emitter` → `find_or_add_module_script` →
`set_parameter` → `finalize`) is the same composition driven from the same generator.

**Base emitters — authored once, by hand.** Under `Content/ElysiumAuthored/VFX/Base/`, tracked
in LFS, **stock Niagara modules only** plus two small module scripts the project owns: the
**VtMB ramp sampler** (two curves plus a per-particle random lerp between them, §5.11) and the
**spherical offset** (radius / theta / phi round the emitter origin, in the emitter basis).
Everything else is engine content. One base emitter per **leaf archetype**; the archetype list
and the numbers each one fixes are `docs/project/effects_authoring.md`'s, filled in as the
archetypes are authored and reviewed. A generated emitter **inherits** its base and overrides
parameter values only — never graph topology — which is why an inherited emitter costs exactly
what a standalone one costs at runtime (§7).

**One emitter per drawing leaf.** The generator walks the staged tree, drops one inherited
emitter per drawing node, names it after the node, and writes that node's numbers, curves,
sprite, material and child relations as **parameter values on that emitter**. Nothing is a
runtime slot index and nothing is an expression string. The corpus fits: 71% of roots have
exactly one drawing leaf, 86% have two or fewer, the 99th percentile is 7 and the maximum 23
(`niagara_authoring_strategy.md` §3). A root above Epic's 8-emitter validation budget is a
warning on the asset, not a failure — `blood_guardian_summon_emitter`'s 20 leaves all draw,
which the slotted floor could not do.

**The mapping — VtMB key → stock module.** Every row is a parameter write on the base emitter,
not a new graph:

| VtMB key (`seam_map_particle.md` → "Semantics") | Niagara |
|---|---|
| `radius`, `theta`, `phi` (+ their spawn ramps) | Shape Location (sphere), plus the project's spherical-offset module for the emitter-basis roll |
| `elevation_speed`, `x/y/z_speed` | Acceleration Force (world up) / Add Velocity in the emitter basis |
| `radius_speed`, `theta_speed`, `phi_speed` | the spherical-offset module's velocity terms |
| `parent_speed` | Inherit Velocity on the spawn script |
| `size`, `width`, `height` ramps | Scale Sprite Size, curve inputs through the ramp module |
| `red / green / blue / color / mask` ramps | Scale Color, curve inputs through the ramp module (`mask` on alpha; AlphaComposite makes it the blend interpolant) |
| `rotation`, `spawn_rotation` | Sprite Rotation Rate / Initial Sprite Rotation, shortest-arc unwrapped by the generator |
| `fps`, `frames`, `frame_min/max` | SubUV Animation (Linear mode, the frame range as the module's range) |
| `movealign` | Sprite Facing / Alignment = **Velocity Aligned** on the renderer |
| `flat` | renderer facing mode (custom facing vector, world up) — a base-emitter variant, since facing is per-renderer, not per-particle |
| `sortfront`, `depth_offset` | renderer Sort Order Hint and Camera Offset |
| `collide` | Collision (CPU, scene queries) |
| `collide.bounce`, `.friction` | Collision Restitution / Friction |
| `collide.gravity` | Gravity Force |
| `collide.drag` | Drag |
| `collide { spawn {} }` | Generate Collision Event on the parent emitter → Receive Collision Event + spawn on the child emitter |
| `spawn { rate }` / `spawn { burst }` on a child | Spawn Particles from Other Emitter, with an **emitter-level** Particle Attribute Reader bound by emitter name — resolvable precisely because the binding now lives inside the asset instead of on a component user parameter |
| `spawn { distance }` | the same spawn module in distance mode (rate × speed) |
| `collide { decal }` | Decal Renderer, Source Mode = **Particles**, orientation from the collision normal (R7.2's stain seam) |
| `no_z_test` | `MI_ParticleNoZ` on the renderer (§5.4) |
| `lighting` | `MI_ParticleLit` |
| `normal` + `refract` | `MI_ParticleRefract`, the DUDV sprite and the refract curve bound on the material |
| `lifetime`, `lifetime_min/max`, `loop`, `timescale` | Emitter State loop behaviour + Initialize Particle lifetime (random range); `timescale` already divided in by the stage |
| `surface_color_optout` | the generator pins that leaf's tint to white |

**System-level user parameters — the only thing the runtime writes.** Exact names, as
`AElysiumEffectActor` writes them today:

| Parameter | Type | Meaning |
|---|---|---|
| `User.RateScale` | float | the one rate float: `rampedScale × VolumeScale`, multiplying every emitter's rate and burst |
| `User.Tint` | LinearColor | `(m_fRedScale, m_fGreenScale, m_fBlueScale, m_fMaskScale)`, default white; multiplies every emitter's colour and mask |
| `User.SizeScale` | float | `m_fSizeScale`, default 1 |
| `User.SpawnShape` | int32 | `0` origin · `1` box (`SpawnBoxMin/Max`) · `2` skeleton segments (`SkeletalMesh`) |
| `User.SpawnBoxMin`, `User.SpawnBoxMax` | Vector, component-local cm | mode 15's brush AABB; mode 9's parent render bounds, refreshed per tick |
| `User.SkeletalMesh` | Skeletal Mesh DI | modes 1 / 3: the parent's mesh, spawn uniform along the skeleton segments |
| `User.FogColor`, `User.FogStart`, `User.FogInvRange` | LinearColor, float, float | the project fog set (`ElysiumFog`), world or sky by the actor's `elysium.sky` marker; bound through the renderer's `MaterialParameters` to the material's fog names |
| `User.RootLifetime`, `User.RootLoop` | float s, bool | the root clock: the `lifetime_s` and `loop` of the **nearest non-drawing wrapper** above the root-spawned leaves (node 0 when they hang off it directly — so `waterdrops_timer`'s 1.7 s one-shot root drips through its 0.67 s looping wrapper); every root-spawned emitter's rate and burst run over `frac(age / RootLifetime)`, and an unlooped clock feeds only its first period |

`User.LeafCount` and the whole `User.Leaf<ii>.*` family go away with the floor.

**The compile and readiness gate.** The generator compiles **explicitly** —
`UNiagaraSystem::RequestCompile(false)` followed by a wait
(`FAssetCompilingManager::FinishAllCompilation()`) — then asserts `IsReadyToRun()` before it
saves. Neither step is optional: asset creation alone requests no compile,
`GetSystemCompileState` reads a stale cache, and `UNiagaraComponent::Activate` then defers
silently with no log at any verbosity (`bAwaitingActivationDueToNotReady`). That silence is
exactly how the retired floor looked correct while drawing nothing.

**The three validation gates.** No archetype is scaled to the corpus until all three pass:

1. **Mechanical, in the bake.** Explicit compile, `IsReadyToRun()`, and — for every generated
   system — activate, tick, and assert a non-zero particle count per emitter. This is an
   `Elysium.Content.*` automation test (`Activate` blocks on compilation under
   `GIsAutomationTesting`), shaped after `NiagaraSystemAuditCommandlet`, with
   `UNiagaraValidationRule_*` (EmitterCount, BannedModules, UserDataInterfaces) attached to
   `ET_ElysiumEffects`.
2. **A contact sheet.** Every generated system placed in the witness level and captured, both
   in Simulate-in-Editor and in-game. Looking at the sheet is the only way a fidelity
   regression across 155 roots becomes visible.
3. **An owner verdict per archetype**, recorded in `docs/project/effects_authoring.md` before
   that archetype is generated across the corpus.

**The material rule stands** (§5.4): AlphaComposite, sprites premultiplied on black, and a fade
scales emissive and opacity **together** — scaling opacity alone whitens the card instead of
vanishing it.

### 5.4 Materials

VtMB draws every particle on **one material**, mode 8 (`docs/vtmb/effects.md` §2.4). The
material lane authors the children beside its masters (`make_v2_materials.py`,
`seam_map_material.md`), map-independent, and **the generator binds one of them onto each
generated emitter's sprite renderer by path** — a compile-time binding per emitter, not a
per-slot user parameter resolved at runtime:

| Child | Parent master | Overrides | Serves |
|---|---|---|---|
| `/ElysiumBaked/Materials/particles/MI_Particle` | `M_V2_SpriteZ` — the **depth-tested** twin of `M_V2_Sprite` (same graph, `bDisableDepthTest` off, `used_with_niagara_sprites`) | `BlendMode = AlphaComposite`, `UseVertexColor = UseVertexAlpha = true` | the default leaf: mode 8 is depth test on, depth write off |
| `/ElysiumBaked/Materials/particles/MI_ParticleLit` | `M_V2_SpriteZLit` — the twin with `TLM_VolumetricPerVertexNonDirectional` | as above | the 6 `lighting` leaves |
| `/ElysiumBaked/Materials/particles/MI_ParticleNoZ` | `M_V2_Sprite` (depth-test-off by the material lane's ruling) | `BlendMode = AlphaComposite`, vertex colour | the 4 `no_z_test` leaves |
| `/ElysiumBaked/Materials/particles/MI_ParticleRefract` | `M_V2_Refract` | `DuDvMap` and `RefractAmount` bound per emitter (`normal`, the `refract` ramp) | the 8 `normal` + `refract` leaves (`fire_heat`, four discipline / boss cards, `warrens_tube_water_fx1`) |

The leaf graph: `Emissive = BaseTexture.rgb × VertexColor.rgb`, `Opacity = BaseTexture.a ×
VertexColor.a` — with AlphaComposite that is `dst = tex × colour + dst × (1 − tex.a × mask)`,
VtMB's equation, so `mask 0` is additive and `mask 255` an occluding card on one material. The
`ElysiumFog` parameters (`FogColor`, `FogStart`, `FogInvRange`) go on the sprite masters with
the fog colour **scaled by the opacity** so an additive card fogs to black, as Source's sprite
shader does — the R6.1/R6.7 follow-up (a miniature sprite drawn unfogged) rides on the same
`GRAPH_VERSION` bump. `M_V2_Sprite` is depth-test-off on the master itself and the flag is not
instance-overridable, which is why the depth-tested twin exists: R7.7's option (b) names the
same `M_V2_SpriteZ`; whichever way R7.7 rules for the plain `env_sprite` cards, R7.3 adds the
twin for the generated leaves. VtMB composites in gamma space, Unreal in linear HDR; the
generated systems inherit the sprite lane's gamma answer for `env_sprite`. Troika's sprites
must stay **premultiplied on black**, and a fade scales emissive and opacity together.

### 5.5 `DA_EffectFamilies` and the effect type

`/Game/ElysiumAuthored/VFX/DA_EffectFamilies` (`UElysiumEffectFamilies : UDataAsset`,
editor-tuned, ships **empty** — the hero families are the tuning session's deliverable):

```cpp
USTRUCT() struct FElysiumEffectFamily
{
    UPROPERTY() FName Family;                               // fire, steam, dust, blood, explosion …
    UPROPERTY() TArray<FString> RootNames;                  // folded root keys (`barrelfireemitter`)
    UPROPERTY() TArray<FString> SpriteNames;                // leaf sprite stems (`flamemass`, `fireplace_flames`, `smoke1`, `flameembers1`)
    UPROPERTY() TSoftObjectPtr<UNiagaraSystem> System;      // the authored family system
    UPROPERTY() TMap<FName, FName> Pins;                    // tree field -> user parameter on System
};
UCLASS() class UElysiumEffectFamilies : public UDataAsset { UPROPERTY() TArray<FElysiumEffectFamily> Families; };
```

Match order: a root name, else any leaf sprite name in the tree. A matched actor plays
`System` **instead of** the generated `NS_<root>`, writing the tree fields `Pins` names (`Rate`, `Lifetime`,
`Size`, the colour, `SpawnRadius`, …) and the shared names (`User.RateScale`, `User.Tint`,
the fog three) — one presentation per root, never two at once. The bake resolves the match
into `FamilySystem` so the level shows it; the runtime re-resolves at adopt so a data-asset
edit needs no re-bake.

`/Game/ElysiumAuthored/VFX/ET_ElysiumEffects` (`UNiagaraEffectType`) is assigned to every
`NS_Elysium*` this task authors: `bCullByDistance = false`, no instance cap,
`SignificanceHandler = UNiagaraSignificanceHandlerDistance`, `UpdateFrequency = Continuous`,
`CullReaction = DeactivateResume`. VtMB draws every emitter regardless of distance, so the
cull ships **off** (the knob is on the asset at "infinite"; the tuning session may set one —
wire first, tune later).

### 5.6 The family systems

Each is an authored system under `/Game/ElysiumAuthored/VFX/`, effect type `ET_ElysiumEffects`,
driven by its actor from the staged row (`seam_map_map.md` → "Import — effects (R7.3)") and by
the class's inputs. Constants VtMB hardcodes are constants in the system, not pins.

**`NS_ElysiumDust`** — `func_dustmotes`, Valve `C_Func_Dust` transcribed
(`docs/vtmb/effects.md` §3.1): a mote spawns at a point **inside the brush solid** (the actor
rejection-samples the entity's convex set from `DA_<map>_Entities` by entity index, ten retries
per point, and publishes the candidates), velocity `±SpeedMax` on all three axes, **no
gravity**, X/Y easing toward the wind, `alpha = (viewZ / DistMax + 1) × sin(π · life / dieTime)
× Alpha` drawn only when `≥ 0.5`, culled past `DistMax`, SCALEMOTES constant screen size.

| Parameter | Type | Source |
|---|---|---|
| `User.SpawnPoints` | Vector array, component-local cm | the actor's pre-sampled in-solid points (256), one drawn at random per spawn |
| `User.SpawnRate` | float /s | `spawn_rate` |
| `User.Color` | LinearColor | `color`, `alpha` |
| `User.SpeedMax` | float cm/s | `speed_max_cm_s` |
| `User.SizeMin`, `User.SizeMax` | float cm | `size_min_cm`, `size_max_cm` |
| `User.LifetimeMin`, `User.LifetimeMax` | float s | `lifetime_min_s`, `lifetime_max_s` |
| `User.DistMax` | float cm | `dist_max_cm` |
| `User.Wind` | Vector cm/s | **zero** until the weather plan has a wind; the term is wired so a future wind drives it |
| `User.Frozen` | bool | `frozen` — pre-spawn and hold |
| `User.Active` | bool | `TurnOn` / `TurnOff`; `start_disabled` at spawn |

**`NS_ElysiumSteam`** — `env_steam`, Valve `CSteamJet` transcribed: `lifetime = JetLength /
Speed`; square spread `fwd · Speed + up · ±Spread + right · ±Spread`; roll `rand(0, 360)`
spinning at the hardcoded `±8 deg/s`; `alpha = renderamt / 255 × sin(π · life / die)`; **size
ramps by raw elapsed seconds**, `StartSize + (EndSize − StartSize) × t`, not by normalized life.

| Parameter | Type | Source |
|---|---|---|
| `User.SpreadSpeed`, `User.Speed` | float cm/s | `spread_speed_cm_s`, `speed_cm_s` |
| `User.StartSize`, `User.EndSize` | float cm | `start_size_cm`, `end_size_cm` |
| `User.Rate` | float /s | `rate` |
| `User.JetLength` | float cm | `jet_length_cm` |
| `User.Lifetime` | float s | `lifetime_s` |
| `User.Color` | LinearColor | `color`, `alpha` |
| `User.Material` | MaterialInterface | the §5.4 child; the heatwave `type` (0 rows) selects `MI_ParticleRefract` — the switch is carried, the material is check 6's |
| `User.Active` | bool | `TurnOn` / `TurnOff` / `Toggle`; `initial_state` at spawn |

**`NS_ElysiumBeam`** — `env_beam`, `DynamicBeam`-derived: endpoints from the two targetnames
(random pick among duplicates; `RandomArea` / `RandomPoint` within `Radius` when one is
missing), width tapering to **10 %**, noise `amplitude × length / 100` over **128** divisions,
`TextureScroll`, `sprites/beama` through the texture lane (the material lane's `MI_` on
`M_V2_Sprite`), additive, `rendercolor` / `renderamt`. Continuous for `life 0`; a striker
activates for `life` seconds every `StrikeTime` (random `0..StrikeTime` under flag 4).
`damage` is one trace along the straight axis into `ElysiumDamage`, on the actor, not in
Niagara.

| Parameter | Type | Source |
|---|---|---|
| `User.Start`, `User.End` | Vector world cm | the resolved endpoints, per strike |
| `User.Width`, `User.EndWidth` | float cm | `width_cm`, `end_width_cm` |
| `User.NoiseAmplitude` | float cm | `noise_amplitude_cm × length / 100` (the actor computes the length term) |
| `User.TextureScroll` | float /s | `texture_scroll` |
| `User.Material` | MaterialInterface | the `MI_` of `vtmb:material:sprites/beama` |
| `User.Color` | LinearColor | `color`, `alpha`; the `Color` / `Alpha` inputs write it |
| `User.Active` | bool | continuous on/off, or the striker's window |

### 5.7 Attach modes (R-D)

The 19-value enum (`vampire.dll 0x105a7000`; the animation-event spawn `mode` is the same
enum), implemented on the actor's component attachment and on the generated system's
`SpawnShape` / `SpawnBoxMin/Max` / `SkeletalMesh` user parameters (§5.3) — the spawn-shape
switch is the one place the actor still reshapes what a system does:

| Mode | VtMB | Here |
|---|---|---|
| `0`, `-1` | `FollowOrigin` (−1 falls to the switch default, INFERRED origin) | the entity's origin; parented only through `SetParent` |
| `1` | `BoneTree` — uniform along the skeleton segments | attach to the parent's body, `SpawnShape 2`, `User.SkeletalMesh` = the parent's mesh |
| `3` | `BoneTreeWithColors` | as `1`; the per-segment tint's source is not decoded beyond the name — recorded, the one open item of R-D |
| `2` | `BoneSinglePoint` | attach to the named socket (`bone`), today's rule |
| `6` | `ModelAttachment` — origin **and basis** each tick (the muzzle mode) | attach to the attachment socket `attach_point`, keep-relative zero, follows |
| `17` | `ModelAttachmentNoFollow` | snap once at `TurnOn`, no follow |
| `9` | `EntityBox` — random point in the parent's render OBB | `SpawnShape 1`, `SpawnBoxMin/Max` refreshed from the parent's bounds each tick |
| `10`, `11` | `PlayerBox` / `PlayerSky` — the `spawnbounds` cube round the viewer, wrapping; `11` spawns above the viewer (`mins.z = player.z`) | weather's rain follow (`NS_ElysiumRain`, `RefreshFollowRain`), unchanged |
| `15` | `BrushEmitter` — forced by `func_particle`; a uniform random point in the world-aligned AABB, **no solid test** | `SpawnShape 1`, the box = `BoundsCm`; `VolumeScale` folded into `User.RateScale` |
| `4`, `8`, `12`, `13`, `18` | placed nowhere, reachable from no code path | not implemented; the enum is complete; logged once if ever seen |
| `5`, `7`, `14`, `16` | screen-space | out of scope by owner decision |

### 5.8 The class list

| Class | Inputs | Body |
|---|---|---|
| `env_particle` (1,304) | `TurnOn` `TurnOff` `SetRateScale` `SetRampTime` `SetAttachType` `Kill` `SetParent` (base; no re-attach) `ScriptHide` `ScriptUnhide`; `JetLength` accepted as VtMB's no-op | `FElysiumEnvParticle` → the placed `AElysiumEffectActor` (§5.2) |
| `func_particle` (98) | `TurnOn` `TurnOff` + the `env_particle` set | `FElysiumFuncParticle`: attach 15 forced, the brush AABB, `volume_scale` into the one rate float; no rain-specific behaviour (the maps happen to use it only for rain boxes; the rain look is weather's) |
| `func_dustmotes` (82) | `TurnOn` `TurnOff` | `FElysiumFuncDustmotes` → `AElysiumDustActor` on `NS_ElysiumDust` |
| `env_steam` (11) | `TurnOn` `TurnOff` `Toggle` | `FElysiumEnvSteam` → `AElysiumSteamActor` on `NS_ElysiumSteam` |
| `env_beam` (47) | `TurnOn` `TurnOff` `Toggle` `StrikeOnce` `Width` `Noise` `Alpha` `Color` | `FElysiumEnvBeam` → `AElysiumBeamActor` on `NS_ElysiumBeam`; `impact_particle` and `faces_player` inert as in VtMB |
| `params_particle` (227) | none | **inert registration** — a precache stub in VtMB; its rows stop reporting as unresolved and nothing else |
| `params_explosion` (52) | none | **inert registration** — the recipe holder `point_explosion` reads by targetname; the join happens at the stage |
| `point_explosion` (93) | `Explode` | §5.10 |
| `env_shake` (60) | `StartShake` `StopShake` `Amplitude` `Frequency` | §5.10 |
| `env_physexplosion` (61) | `Explode` | §5.10, P1 |
| `env_physimpact` (130) | `Impact` | §5.10, P1 |
| `env_shooter` (21) | `Shoot` | §5.10 |
| `env_particle_hud` (3) | `TurnOn` | stays a stub — out of scope |

### 5.9 The producer entry point — "spawn root X at attachment Y with mode Z"

The deferred producers (impact table, muzzle pair, the 511x animation events, discipline /
dialog auras — `plans/gameplay.md`, the combat and animation plans) need exactly one call,
which the effects system exposes on the embodiment seam beside `PlayAttachedEffect` (which it
supersedes — feeding's 5116 becomes a mode-2 spawn):

```cpp
// Spawn one particle root as a transient AElysiumEffectActor on the root's generated
// NS_<root> (or its family override). Root is the folded vtmb:particle key; Parent may be
// null for a world spawn;
// AttachMode is the 19-value enum; AttachName the bone / attachment string, AttachPoint the
// numbered attachment. Returns an invalid handle when the root's tree is unknown.
virtual FElysiumEffectHandle SpawnParticleRoot(const FString& Root,
    const FElysiumEntityHandle* Parent, int32 AttachMode, FName AttachName, int32 AttachPoint,
    const FVector& OriginCm, const FRotator& Angles) { return {}; }
virtual void StopParticleRoot(const FElysiumEffectHandle& Handle) {}   // TurnOff: let finish
virtual void KillParticleRoot(const FElysiumEffectHandle& Handle) {}   // remove now
```

`DA_ElysiumParticleTrees` (`/ElysiumBaked/Particles/`, `UElysiumParticleTrees`, keyed by root
id) is the **root → `NS_<root>` lookup**: per root it carries the staged tree (for the family
pins and for `bake_verify`) and the soft pointer to the generated system in
`/Game/ElysiumGenerated/VFX/`, so a producer resolves a name to an asset without knowing the
map it came from. It is built by the same `importers.effects` tree builder the map stage runs,
over every root a producer's data names (item records, the impact table, the discipline
records, the animation-event option strings); R7.3 lands the builder and the map lane's roots
in it, and the plan that first needs a non-placed root adds that root — which also generates
its system. Nothing in VtMB's animation-event bus stops an emitter (5103 is "remove all
model decals"); a producer that started one stops it.

### 5.10 The explosion bundle (P1) — after the ambient set

`point_explosion` reads its `params_explosion` by targetname (joined at the stage) and its
`Explode` is the ordered recipe, also callable by breakables and the terminal (VtMB's other
`DoExplosion` callers):

| Leg | Faithful behaviour (`docs/vtmb/effects.md` §4.9) | Wiring |
|---|---|---|
| 1 light | if `dl_radius > 1`: `dl_color`, `dl_exponent`, `dl_radius` (in → cm), `dl_time` life, radius shrinking at `dl_decay` units/s | `UPointLightComponent` on the actor, radius and intensity per tick; no shadow (a Source dlight casts none) |
| 2 damage | if `dmg_amount > 0`: DMG_BLAST, `adjusted = dmg − dist × dmg / radius` floored at 1, LOS ray unless flag 0x200, targets by `damage_players / npcs / breakables` (default **false**) | `ElysiumDamage::ApplyRadial(...)` transcribing the falloff and the filter — not `ApplyRadialDamageWithFalloff` (a second damage model with a different curve) |
| 3 particle | `TE ParticleEffect(origin, angles, name)` — no mode argument | §5.9 at the origin with the entity's angles |
| 4 shake | `UTIL_ScreenShake`: `localAmp = amp × (1 − dist / radius)`, linear; client `CalcShake`: every `1 / frequency` s re-roll `offset = ±amp` (3 axes) and `angle = ±amp × 0.25`; `frac = remaining / duration`; `s = frac² × sin(curtime × frequency / frac)`; view `+= s × offset`, **roll** `+= s × angle`; `amp −= amp × dt / (frequency × duration)`; inches of translation, `amp × 0.25` degrees of roll; `shk_inair` gates on being airborne | `UElysiumCameraShakePattern : UCameraShakePattern` (base Engine, no plugin) transcribing `CalcShake` — `frac² × sin`, the re-roll, roll `amp × 0.25°`, translation `amp × 2.54` cm, the decay — played through `PlayWorldCameraShake(origin, 0, radius × 2.54, 1)`; `shk_inair` from the movement component's ground state |
| 5 sound | `snd_name` with `snd_dist` PAS and a random pitch in `snd_pitch_*` | the call site is wired here; the asset lane is `plans/audio.md`'s |

`env_shake` is the same shake path standalone: flag 1 → global (no epicentre), flag 4 → also
in air, `StartShake` / `StopShake` / `Amplitude` / `Frequency`. **Flag 8** (one row, `13`) →
deferred with a road: an oscillating `AddRadialImpulse` on bodies in range once the seam is
in (the physics plan).

**Physics (P1).** `AddBodyImpulse` and `AddRadialImpulse` land on the embodiment seam
(`physics-architecture.md` §7), minimal, plus one geometry query
`QueryPhysicsBodies(Origin, RadiusCm)` so a class can iterate bodies itself:

- `env_physexplosion` → per body within `radius` (else `magnitude × 2.5`), a **world-only**
  LOS trace from the explosion to the body; with clear LOS `force = normalize(body − origin) ×
  max(0, magnitude − dist × 0.4) × phys_pushscale` at the **centre of mass** through
  `AddBodyImpulse` (no torque, no division by mass); blocked LOS → nothing. It is **not**
  `AddRadialImpulse` (whose linear falloff is Unreal's, not VtMB's) — a divergence from §7's
  consumer table, stated here; `AddRadialImpulse` keeps the bullet / corpse pushes.
- `env_physimpact` → the trace (flag 2 infinite ray), `mag = (flags & 11) ? magnitude : (1 −
  trace.fraction) × magnitude` (flag 1 no fall-off, 4 multiply by mass, 8 ignore the surface
  normal), `AddBodyImpulse(−normal × mag × phys_pushscale, endpos)`.
- The Source → Chaos magnitude conversion is §8's `× 2.54` and is UNVERIFIED in the engine
  header; acceptance is did-it-move on the junkyard barrel, the number is the tuning session's.

**`env_shooter`.** A real Chaos body per shot from the gib mesh (`models/gibs/*`, structural
debris — props-lane units): `velocity = scatter(angles, variance) × m_flVelocity × 2.54`,
angular `(100–200, 100–300, 0)`, life `±5 % × m_flGibLife` (default 25 s) then removal, `delay`
between shots (default 0.001), the repeat flag, flag 2 (flaming) → an attached fire root through
§5.9. VtMB bounces a **point** (`MOVETYPE_BOUNCE`, zero-extent bbox); the gib here bounces on
its own hull — a modernization, not a loss. The up-to-five blood decals on landing call
`UElysiumDecalSubsystem::Lay` with `ElysiumImpactDecals::PoolFor("F", false)`'s
`decals/hits/flesh/blood` pool — the seam is landed (R7.2) and takes callers, so this one arrives
with `env_shooter` itself rather than as a stub against a name.
`nogibshadows` / `gibgravityscale` do not exist in VtMB.

### 5.11 Fidelity ledger (R-J) — every runtime feature, wired or deferred

| Feature | Status |
|---|---|
| `rate`, `burst` (keyframed), the one rate-scale float, `SetRateScale` / `SetRampTime` linear approach | wired |
| `frames / fps / min / max`, normalized age, `loop`, `timescale` | wired |
| ramps: linear in normalized age, `(n)` frame index, negative wrap, shortest-arc angles, `a~b` per particle | wired through the authored **ramp module**: two curves (the `lo` track and the `hi` track) plus a per-particle random lerp between them, the generator writing every keyframe as a curve key. No five-keyframe ceiling: the curve carries every key the stage staged (15 over the whole corpus by the strategy note's `TreeBuilder` pass, up to 83 on a placed body ramp by the stage's own histogram — `seam_map_map.md`) |
| spherical motion round the emitter, emitter-basis `x/y/z`, world `elevation`, `parent_speed` | wired |
| `movealign`, `flat`, `rotation`, `width / height / size` × sprite aspect | wired |
| `red / green / blue / color` × spawn scales, `mask` as the AlphaComposite interpolant, the three `surface_color` opt-outs (pin white) | wired |
| `depth_offset`, `sortfront`, `no_z_test` | wired (camera offset, sort priority, depth-test-off child) |
| `normal` + `refract` (8 leaves) | wired on `M_V2_Refract` |
| `lighting` (6 leaves) | wired as the lit child; whether VtMB's sample is light stays INFERRED (`0x200d3840`) |
| `collide`: world + brush-entity trace, `self` bounce / friction / gravity / drag, spawn child at impact | wired (CPU collision + event) |
| `collide → decal`, `vdecal_*` ranges | collision event wired; the decal spawn is `UElysiumDecalSubsystem::Lay` (**R7.2**, landed). No `vdecal_*` material exists in the corpus, so that half has nothing to lay |
| `both` nodes and depth-4 trees | wired (events between emitters of the generated system) |
| `distance` spawn key | 0 placed uses; wired as a rate × speed switch in the spawn module |
| `precipitation` gate by the leaf sky bit | **weather's** follow-up (needs the visibility unit's leaf sky bit); the flag is staged |
| attach `0 1 2 3 6 9 17 −1`, `10 / 11` (weather), `15` | wired; `3`'s segment tint recorded |
| attach `4 8 12 13 18` | not placed, not reachable; recorded |
| attach `5 7 14 16` | screen-space, out of scope by owner decision |
| `active`, `ramp_scale`, `ramp_time`, `spawnbounds`, the five scale fields, `TurnOn` restart, `TurnOff` let-finish, `Kill`, `SetParent` (no re-attach) | wired |
| `JetLength` on `env_particle` | VtMB no-op; logged |
| `func_particle` AABB, `volume / 128³` clamp, forced 15 | wired |
| `func_dustmotes` solid sampling, no gravity, wind term, alpha law, `DistMax`, SCALEMOTES, `Frozen` | wired (wind = 0 until **weather** has one) |
| `env_steam` all keys, raw-seconds ramp, `±8°/s` roll, heatwave switch | wired (heatwave material = check 6) |
| `env_beam` taper, noise, scroll, strikers, `Radius` random ends, `damage`, all inputs | wired; `impact_particle` / `faces_player` inert as in VtMB |
| `point_explosion` five legs in order; `params_explosion` flags | wired (sound asset lane = **audio**'s) — the explosion slice |
| `env_shake` math and flags 1 / 4; flag 8 | wired; flag 8 → **physics** plan (1 row) |
| `env_physexplosion` LOS + centre-of-mass force; `env_physimpact` four flags | wired (P1) — the explosion slice |
| `env_shooter` scatter, spin, life, delay, repeat, flaming; point bbox; blood decals | wired; hull instead of point (**modernization**); decals → **R7.2** — the explosion slice |
| `params_particle` / `params_explosion` | inert classes |
| impact table, muzzle pair + light, 511x animation-event spawns; discipline / dialog auras, first- vs third-person | §5.9's call; **combat / animation / gameplay plans** (the camera-swap rule is `camera-view-modes.md`'s open item) |
| Fluids / authored family art | **tuning sessions** through `DA_EffectFamilies`; the generated system keeps drawing until then |
| HUD / menu / screen-space | out of scope by owner decision |

---

## 6. Exploration checks before code (no bake, no game code)

Engine content, a scratch `/Game/ElysiumAuthored/FX/_Explore/` folder, a disposable map; one
editor session; each check has a yes/no result that goes into §3 as a fact. Checks **2, 3 and
7** as first written (the ramp lookup table, the `Texture2DArray` question, slot events) are
**retired with the floor**; they asked how to fit twenty leaves into one authored asset, which
is no longer the shape. In their place stand the three steps of the spike
(`niagara_authoring_strategy.md` §5), renumbered into those three slots below; they are the
go/no-go on §5.3 and run before any generator code.

- **Check 1 — the generic leaf is expressible in one Niagara emitter.** Emitter-basis
  spherical offset with three speeds, normalized age, linear ramps, `movealign`,
  AlphaComposite material. Author `Flames2` + `FlameGlow2` (the `fire2_emitter` leaves) by
  hand from the numbers. Does it appear? (Did-it-appear only.)
- **Check 2 — headless composition compiles and activates.** Build `NS_Spike` from a base
  emitter plus two stock modules under `-run=pythonscript`, finalize, wait for the compile,
  save. Accept only if the Niagara editor was never opened, the compile state is fresh, and a
  spawned `NiagaraActor` reports `is_active() == True`. This is the go/no-go on the whole
  generated-asset family; if it fails on the external-edit API, repeat it on the
  conversion-context path before any fallback.
- **Check 3 — one readable module beats 37 expression strings.** Hand-author the ramp sampler
  (§5.3), drop it on a CPU emitter fed `waterdrops_timer`'s size ramp, and confirm the sampled
  value against a hand computation. Watch for CPU-VM intrinsics that silently diverge
  (`smoothstep`).
- **Check 4 — project fog reaches a Niagara sprite.** With `ElysiumMapVisuals` fog active,
  does the sprite fog through the MID parameters? Settles §5.4's binding.
- **Check 5 — AlphaComposite on the sprite twin.** Does `mask 0` read additive and
  `mask 255` occlude, and does the lane's gamma answer for `env_sprite` hold?
- **Check 6 — refract card.** `fire_heat`'s `normal` sprite as DUDV on `M_V2_Refract` over a
  fire. Does the shimmer appear?
- **Check 7 — generation fixes parent/child and the dropped leaves.** Generate `drip`
  (collide → spawn child, emitter-level reader bound `Other → <parent emitter name>`) and
  `blood_guardian_summon_emitter` (20 leaves); place both in `sm_hub_1`. Accept if the child
  lands at the collision, all 20 leaves draw, no "Source emitter not found" appears, and hub
  frame cost stays inside rain's 1.0 ms discipline.
- **Check 8 — shake pattern without the plugin.** A `UCameraShakePattern` subclass
  transcribing `CalcShake`, played through `PlayWorldCameraShake(origin, 0, radius, 1)`. Does
  it shake, and does it stop at `radius`?
- **Check 9 — beam taper and noise.** `DynamicBeam` with width 6 → 0.6, noise 15 × length /
  100, additive `beama`. Does it read as the theatre beam?
- **Check 10 — cost on a hub.** `sm_hub_1`'s placed rows as generated-system instances, cull
  off. Frame cost against rain's 1.0 ms discipline; if it fails, the C3 fallback is the
  answer, not a cull.

Answered already: coronas are drawn on `M_V2_Sprite` with Source's glow rule (R6.1); ribbons
are the motion-trail answer (`NS_ElysiumMeleeTrail`).

---

## 7. Cost notes

The render path is already Lumen + MegaLights + VSM at 1440p
(`docs/architecture/rendering-perf.md`). Particles spend the translucency budget
weather already claimed.

- Sprite Niagara is cheap if overdraw is bounded (soft material, no full-screen
  sheets, GPU sim). The generated systems are CPU-sim (collision by scene query, events
  between emitters); the cost model is **emitters per root × placed instances of that root**,
  which check 10 measures. 71% of roots carry one emitter, 86% two or fewer.
- **The asset count is not a frame cost.** One `NS_<root>` per root — 155 now, 1,698 at full
  corpus — is a DDC and editor cost, not a runtime one; per Michael Galetzka (Epic), an
  inherited child emitter costs "exactly the same" at runtime as a standalone one. What is
  paid per frame is emitters that tick, and a zero-spawn emitter in an uber system still ticks
  its Emitter State — which is the cost the retired floor was paying twenty times over.
- Epic's default validation budget is **8 emitters per system**
  (`NiagaraValidationRules.h`); the handful of corpus roots above it are warnings on the
  asset, weighed per root against check 10.
- Scalability lives on `ET_ElysiumEffects` (§5.5): distance significance, an instance cap and
  a cull distance are one asset edit each, shipped off; `UNiagaraComponentPool` serves the
  §5.9 transient spawns.
- 2D Fluids: budget a handful of live grids, not one per barrel on a hub.
- 3D Fluids and heterogeneous volumes: theatre / boss only.
- Rain already has a 1.0 ms / 20% cap on the 5070 Ti measurement. New families
  inherit that discipline; cut look before raising authored rates.

---

## 8. Related docs

- `docs/vtmb/effects.md` — the inventory this mapping covers; §2.4 the decoded runtime.
- `docs/vtmb/weather.md` — rain as the worked example of the same seam.
- `docs/architecture/seam_map_particle.md` — the particle unit and its "Semantics" contract.
- `docs/architecture/seam_map_map.md` → "Import — effects (R7.3)" — the staged product and the
  per-map cutover.
- `docs/architecture/physics-architecture.md` §7 — the impulse seam.
- `docs/vtmb/phy_vphysics.md` — collision, not FX.
- `docs/project/reconstruction-direction.md` — presentation may modernize; logic reproduces.
- `docs/architecture/rendering-perf.md` — the budget these systems share.
- `docs/project/niagara_authoring_strategy.md` — the research behind the 2026-09-03 B3
  revision: what the slotted floor broke on, and the engine facts that bound §5.3.
- `docs/examples/` — real Niagara work per VtMB family, the reference the base emitters copy.
- `docs/project/effects_authoring.md` — the archetype list, the base emitters as they are
  authored, and the owner verdict per archetype.
