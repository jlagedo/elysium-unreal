# Effects — Unreal reproduction

VtMB's effect inventory, formats and producers are `docs/vtmb/effects.md`. This
document is the Unreal side: the owner call, the seam between reproduced logic and
modern presentation, and the 5.8 assets worth opening first.

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
| Particle compiler | `pipeline/src/elysium_pipeline/formats/particles.py` | strict KeyValues → Unreal-cm JSON; unknown live keys fail the root |
| Particle mirror | `$ELYSIUM_EXPORT_ROOT/particles/` | 1,698 `.txt` + 318 TGA/PNG |
| Per-map sidecar | `<map>.particles.json` | placed `env_particle` + closure + `unresolved` |
| Niagara flatten | `pipeline/unreal/make_particle_systems.py` | one Fountain-based emitter per drawing leaf, `NS_<root>` |
| Rain presentation | `/Game/ElysiumAuthored/VFX/NS_ElysiumRain` | tracked authored system; live follow-rain binds each map's baked height-masked material instances through `User.RainStreakMaterial`/`User.RainMistMaterial` (`ElysiumMapActorWeather.cpp`); other emitters load their baked `NS_*` |
| Melee weapon trail | `/Game/ElysiumAuthored/VFX/NS_ElysiumMeleeTrail` | tracked authored ribbon; `ElysiumMeleeTrail.cpp` drives `User.TrailPointA/B` off the wield mesh's baked `TrailTip` socket during a live melee swing |
| Sprites sidecar | `<map>.sprites` | `env_sprite` coronas |
| Decals | `<map>.decals` → `UDecalComponent` | authored `infodecal` |
| `env_fade` | real entity class | screen fade |
| Prop `.phy` | `props/<stem>.phys` → Chaos | the barrel's *body*, not its fire |

The Fountain flatten is a *data pipe*, not the look target. It is the right way to
preserve rates, lifetimes and sprite identities while the presentation of each
*family* is replaced by a small set of authored Unreal systems.

`func_particle`, `env_physimpact`, `env_physexplosion`, `env_shooter`,
`env_particle_hud`, `env_shake`, `point_explosion` are stub classes today
(`ElysiumStubClasses.cpp`). Their I/O is logged; nothing draws or impulses yet.

---

## 3. The Unreal toolkit (this 5.8 install)

Everything below is already on disk at `D:\Epic\UE_5.8`. Open it in the editor;
do not add Marketplace packs or Starter Content to the game project to explore.

### 3.1 Niagara sprite / mesh / ribbon — the default

Plugin: `Engine/Plugins/FX/Niagara`.

Templates to open first
(`/Niagara/DefaultAssets/Templates/`):

| Asset | Kind | Use for |
|---|---|---|
| `Emitters/Fountain` | looping sprite column | fire plume, steam, fountain, ash |
| `Emitters/SimpleSpriteBurst` | one-shot sprites | muzzle, impact, blood hit, flash |
| `Emitters/OmnidirectionalBurst` | radial burst | explosion core, light-bulb pop |
| `Emitters/DirectionalBurst` | aimed burst | shotgun, steam jet, vomit, blood strike |
| `Emitters/HangingParticulates` | sparse slow motes | warehouse dust, star-night, moths (motion swapped) |
| `Emitters/BlowingParticles` | wind-carried | ash, cigar, paper, mist |
| `Emitters/ConfettiBurst` | many short cards | glass / wood chips (re-material) |
| `Systems/SimpleExplosion` | burst + debris | `params_explosion.particle` stand-in |
| `Emitters/DynamicBeam` / `StaticBeam` | beam | rare; Dominate/Presence stare if a beam reads better than sprites |
| `Emitters/LocationBasedRibbon` | ribbon | tracers, blood return, Celerity trail |
| `BehaviorExamples/SpriteFacingAndAlignment` | facing | `movealign` (rain streaks, embers) |
| `BehaviorExamples/SubUVAnimation` | flipbook | candle, muzzle sheet, explosion sheet |
| `BehaviorExamples/KillParticles` + `Collision/*` | collide | rain splash, blood decal, sparks on floor |

Modules that map 1:1 onto Troika keys
(`/Niagara/Content/Modules/`):

| Troika idea | Niagara module |
|---|---|
| `rate` | `Emitter/SpawnRate` |
| `burst` | `Emitter/SpawnBurst_Instantaneous` |
| `radius` / `theta` / `phi` | `Spawn/Location/SphereLocation` or `ConeLocation` |
| `X/Y/Z_speed` | `Spawn/Velocity/AddVelocity` |
| `radius_speed` / `elevation_speed` | `Update/Forces/AccelerationForce` + radial, or Fluids |
| `movealign` | `Update/Renderers/Sprite/SpriteFacingAndAlignment` (velocity) |
| `size` / `height` ramp | `Update/Size/ScaleSpriteSize` |
| `red/green/blue` / `color` / `mask` | `Update/Color/Color` + `ScaleColor` |
| `collide` spawn | `Events/GenerateCollisionEvent` → `ReceiveCollisionEvent` |
| `collide` decal | `Modules/Decal/*` or spawn a `UDecalComponent` |
| bone / point attach | `Spawn/Location/SocketLocation` / `SkeletalMeshLocation` |
| parent follow | `Update/Position/InheritSourceMovement` |
| heat haze | a second emitter with a refraction material, **or** drop it |

Epic's own sprite-smoke walkthrough is the proven recipe this project should copy
for steam / cigar / ash, then retune:

<https://dev.epicgames.com/documentation/unreal-engine/how-to-create-a-smoke-effect-using-sprite-particles-in-niagara-for-unreal-engine>

Spawn Rate 50, random lifetime 2–3 s, random sprite 75–200, upward velocity,
sphere spawn radius, SubUV on an 8×8 smoke sheet, Z acceleration. That *is*
Troika `Smoke1` / `SteamRelease_Constant` with a better material.

### 3.2 Niagara Fluids — hero fire and smoke only

Plugin: `Engine/Plugins/FX/NiagaraFluids` (already present).

Epic's own split: **2D templates are for games, 3D templates are for
cinematics.** This project's floor is an RTX 4060 at 1440p with Lumen +
MegaLights already on. 3D gas is a cutscene / one-off boss tool, not a barrel.

Open these, in this order
(`/NiagaraFluids/Content/Templates/`):

| Asset | When to try it |
|---|---|
| `Gas/2D/Systems/Grid2D_Gas_SmokeFire` | **first fire.** One 2D grid, smoke + flame. Matches a barrel / stove / gasoline pool better than 3D. |
| `Gas/2D/Systems/Grid2D_Gas_Smoke` | steam plume, cigar, chimney, Animalism ground fog |
| `Gas/2D/Systems/Grid2D_Gas_MovingFire` | gasoline trail, wall-of-fire, Molotov path |
| `Gas/2D/Systems/Grid2D_Gas_Explosion` | warehouse / barrel *look* (the recipe in §5 still owns damage / shake / impulse) |
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

### 3.3 Not Niagara

| Intent | Unreal primitive | Notes |
|---|---|---|
| Coronas / volume-light shafts / candle / cop flash / lightning | `UMaterialBillboardComponent` or a 1-particle Niagara | already the `.sprites` plan. Lumen + bloom replace a lot of `glowa`. |
| Authored decals | `UDecalComponent` | already baked |
| Runtime blood / rain stains | Niagara Decal renderer, or spawn a short-lived `UDecalComponent` | particle `collide.decal` |
| Screen fade | `env_fade` (done) | |
| Camera shake | `UCameraShakeBase` / `ULegacyCameraShake` | `env_shake` and `params_explosion.shk_*` |
| Physics kick | the impulse seam in `docs/architecture/physics-architecture.md` (`FBodyInstance::AddImpulse` / `AddRadialImpulse` underneath) | `env_physimpact` / `env_physexplosion` |
| Gibs | `UNiagaraComponent` mesh renderer, **or** spawn the gib `SM_*` with a Chaos impulse | `env_shooter` already names `.mdl`s |
| HUD / view-space FX | `NiagaraUIRenderer` plugin (in this engine) or a camera-attached Niagara | `env_particle_hud`, discipline HUD, muzzle first-person |
| Auspex / heat vision | post-process material | not an emitter |
| Obfuscate translucency | mesh opacity / custom depth | trait flag, not a particle |
| Celerity motion trail | Niagara ribbon on the body, or `UMotionTrailTool` is an editor tool — runtime is a ribbon | `Fx_Motion_Trail` |
| Explosion light | `UPointLightComponent`, lifetime from `dl_time` / `dl_decay` | do not put this inside Niagara unless you already have a light module you like |
| Map fog / water | existing height fog + Single Layer Water | not this system |
| Menu background | a dedicated Niagara system on the menu map | `docs/vtmb/m0_menu_build.md` |

### 3.4 What not to reach for

- **Cascade.** The plugin is still in 5.8 for conversion. Do not author new
  Cascade.
- **Heterogeneous Volumes / Sparse Volume Textures.** Cinematic smoke. Wrong
  cost class for a 20k-triangle night street with hundreds of dynamic lights.
- **Niagara 3D Fluids on every placed emitter.** Same reason.
- **Porting `particles/*.tga` as the long-term look.** Keep them as *reference*
  and as a fallback albedo. New flipbooks / tilable noise for fire, smoke, blood
  are the remaster.
- **One Niagara system per Troika file.** 1,698 systems is unmaintainable. One
  (or a few) systems *per family* in §4, parameterized by the compiled JSON
  (rate, colour, size, attach).

---

## 4. Family → Unreal stand-in

Each row is "open this, retune to the compiled rates, ship that family."

| Family (`docs/vtmb/effects.md` §4) | First thing to open | Fallback if it is too expensive or too much look | Drive with |
|---|---|---|---|
| Fire / embers (barrel, stove, gasoline) | `Grid2D_Gas_SmokeFire` | Fountain × 3 (flame, smoke, ember) using the official sprite-smoke recipe for the plume | `barrelfireemitter` JSON: flame 40/s, smoke 5/s, ember 5/s |
| Steam (clinic `env_steam` + Troika steam) | Fountain or `Grid2D_Gas_Smoke`, cone location, white | DirectionalBurst looping | `SpreadSpeed` 15, `Speed` 120, `JetLength` 80, or Troika `rate` 50 |
| Smoke / cigar / ash | Epic sprite-smoke how-to, then `BlowingParticles` | Fountain | compiled `radius_speed` / `elevation_speed` |
| Rain / drips | existing `NS_ElysiumRain` | — | weather sidecar |
| Lightning | `env_sprite` blink (already authored as timers) + a light pulse | Niagara burst of a long card | timer graph in `docs/vtmb/weather.md` |
| Dust motes / night motes | `HangingParticulates` | Fountain at rate 30, size 5–15, alpha 90 | `func_dustmotes` keys, or `starynight_emitter` |
| Moths / flies | `HangingParticulates` + `CurlNoiseForce` | simple sprite with a wandering velocity | `moth_emitter` (compiler currently rejects `timescale`) |
| Blood hit / spray | `DirectionalBurst` | `SimpleSpriteBurst` | impact table + compiled blood FX |
| Blood trail / return | `LocationBasedRibbon` | sprite streak with `movealign` | Thaumaturgy return emitters |
| Muzzle flash | two `SimpleSpriteBurst` systems (flash + smoke) on the weapon's muzzle socket, **plus** a one-frame point light on the shooter's attachment 1 | SubUV flash sheet | item `muzzleflash_particle` / `muzzlesmoke_particle` (`view*` in first person) + events 5001/5003; the light is 255/192/64 at 254 cm off the `m_fEffects` bit, not an item key |
| Tracers | ribbon | `movealign` sprite | `bullettrail_emitter` |
| Surface impact | `SimpleSpriteBurst` per surface group (concrete/metal/wood/dirt/glass/flesh) | one generic burst tinted by surface | `particleimpacttable.txt` |
| Explosion look | `SimpleExplosion` or `Grid2D_Gas_Explosion` | OmnidirectionalBurst | `params_explosion.particle` |
| Explosion light / damage / shake / impulse | point light + damage commit + `UCameraShakeBase` + `AddRadialImpulse` | — | the other `params_explosion` / `env_phys*` keys |
| Gibs | spawn the baked gib mesh, Chaos impulse | Niagara mesh renderer | `env_shooter.shootmodel` |
| Coronas / shafts / candles / cop lights | billboard + Lumen; candle = SubUV | 1-particle Niagara | `.sprites` |
| Conversation Dominate / Presence | small looping aura (Fountain or `Grid2D_Gas_Color`) attached to speaker/listener | sprite halo | `params_particle` pair on every map |
| Discipline body / HUD | camera-attached Niagara or NiagaraUIRenderer | — | `Particle_FirstPerson` / `ThirdPerson` / stats.txt |
| Obfuscate / Celerity / Auspex vision | material / ribbon / post-process | do not fake with sprites | trait flags |
| Screen fade | `env_fade` | — | already a class |
| Menu scene | one Niagara system, three emitters | — | `mainmenuparticles.txt` |
| Runtime stain | Niagara decal or `UDecalComponent` | — | `collide.decal` |

---

## 5. How a modern system is driven (the seam)

The compiled `<map>.particles.json` and the vdata tables stay the **authority for
parameters**. A family system exposes Niagara user parameters:

- `SpawnRate`, `BurstCount`
- `SpriteSize`, `Lifetime`
- `Color`, `Alpha`
- `Velocity`, `ConeAngle`
- `AttachSocket` / `AttachMode` (origin / point / follow-viewer)
- `Active`, `RateScale`

The entity or the animation-event / discipline / impact producer writes those
parameters and calls `Activate` / `Deactivate`. It does not spawn a unique asset
per Troika filename.

`TurnOn` = restart (re-`Activate`, reset clock). `TurnOff` = stop spawning, let
particles die. That is already how Niagara `Emitter State` works if Loop Behavior
is infinite and you zero `SpawnRate`.

Unresolved compiler roots (barrel heat card, moths, some explosions) do **not**
block the family: skip the unsupported leaf (`Fire_Heat`) or author the moth
system by hand from the file's intent.

---

## 6. What to open this week (no bake, no game code)

Work in the Unreal editor against engine content. Duplicate a template into a
scratch `/Game/ElysiumAuthored/FX/_Explore/` folder (project-owned, not
game-derived) or into a disposable map. Do not run `export` or `build`.

Suggested order, each one is a single proven example:

1. **Sprite smoke** — follow Epic's how-to, drop it next to a clinic-pipe
   screenshot or the `steamrelease_constant` numbers. This unlocks steam, cigar,
   ash, Animalism fog.
2. **`Grid2D_Gas_SmokeFire`** — one barrel. Compare to `barrelfireemitter` in
   the original (or a capture). Decide sprite-vs-2D-gas for *all* placed fires.
3. **`SimpleSpriteBurst`** — a flesh hit and a concrete hit, two materials, same
   system. This unlocks the impact table.
4. **`SimpleExplosion` + a point light + a camera shake + `AddRadialImpulse`**
   on a Chaos barrel. This *is* the junkyard barrel bundle. Four systems, one
   beat.
5. **`HangingParticulates`** — warehouse air. Compare to `func_dustmotes`
   (rate 30, size 5–15, colour 203 202 217, alpha 90).
6. **Billboard corona** — one `glowa` at a lamp, additive, Lumen on. Decide how
   much of the 969 coronas Lumen + bloom already replace.
7. **Ribbon** — answered: `NS_ElysiumMeleeTrail` is a shipping
   `LocationBasedRibbon`-derived system (blade-spanning strip via
   `CustomSideVector` facing over two user positions), so ribbons are the
   motion-trail answer; a tracer or Celerity trail starts from the same shape.

After those seven, every family in §4 has a chosen stand-in. The remaining work
is parameterization and I/O, not research.

---

## 7. Cost notes

The render path is already Lumen + MegaLights + VSM at 1440p
(`docs/architecture/rendering-perf.md`). Particles spend the translucency budget
weather already claimed.

- Sprite Niagara is cheap if overdraw is bounded (soft material, no full-screen
  sheets, GPU sim).
- 2D Fluids: budget a handful of live grids, not one per barrel on a hub.
- 3D Fluids and heterogeneous volumes: theatre / boss only.
- Rain already has a 1.0 ms / 20% cap on the 5070 Ti measurement. New families
  inherit that discipline; cut look before raising authored rates.

---

## 8. Related docs

- `docs/vtmb/effects.md` — the inventory this mapping covers.
- `docs/vtmb/weather.md` — rain as the worked example of the same seam.
- `docs/vtmb/phy_vphysics.md` — collision, not FX.
- `docs/project/remaster-direction.md` — presentation may modernize; logic
  reproduces.
- `docs/architecture/uasset-bake-spike.md` — the `particles` bake stage.
- `docs/architecture/rendering-perf.md` — the budget these systems share.
