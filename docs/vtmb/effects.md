# Effects — every visual and impulse effect VtMB authors

VtMB does not ship Valve's later `.pcf` particle editor. Troika forked `engine.dll` and
authored a text-driven particle language under `particles/`, then placed it from map
entities, animation events, discipline records, weapon items, an impact table, and the
main menu. Separate Source leftovers (`env_steam`, `func_dustmotes`, `env_shake`,
`env_fade`) and two physics-impulse entities sit beside that system. This document is
the inventory of those families and the contracts a remaster has to cover.

Unreal reproduction — modern look, reproduced logic — lives in
`docs/architecture/effects-architecture.md`. Rain, wetness and the particle-definition
*grammar* live in `docs/vtmb/weather.md`. Collision models named `.phy` are not effects;
they live in `docs/vtmb/phy_vphysics.md`.

Status of any runtime or bake work lives only in `docs/project/roadmap.md`.

---

## 1. `.phy` is not an effect

A sibling `.phy` next to a `.mdl` is **VPhysics collision**: convex hulls plus an authored
mass, decoded to `props/<stem>.phys` and consumed by `prop_physics` / `prop_dynamic`.
Format, coordinates, and the missing-collision rule are `docs/vtmb/phy_vphysics.md`.

A burning barrel, a flying car, or a warehouse blast uses that collision body **and**
separate effect entities. The `.phy` never names a sprite, a particle definition, or a
render mode. Treating a `.phys` sidecar as a smoke or fire asset is a category error.

Two map classes *do* carry `phys` in the classname. They are impulse sources, not
collision files and not particle systems:

| Class | Count on the 23 exported maps | Input | What it does |
|---|---:|---|---|
| `env_physimpact` | 11 | `Impact` | applies a directional impulse to one named physics body |
| `env_physexplosion` | 8 | `Explode` | applies a radial impulse; `targetentityname` names the body |

Both cluster on `sm_junkyard_1` (cars, exploding barrels) and `sm_warehouse_1` (the
scripted blast). Keys: `magnitude`, plus `directionentityname` / `target_position`
(`env_physimpact`) or `radius` / `targetentityname` (`env_physexplosion`). They move
already-simulated Chaos/VPhysics bodies. They do not draw.

A typical junkyard barrel is therefore four authored pieces, not one:

1. `prop_physics` + the model's `.phy` — the body that can fly.
2. `env_particle` `barrelfireemitter` — the fire/smoke look.
3. `params_explosion` — flash light, damage, optional shake, a *different* particle
   (`Explosion2_emitter`), and a sound.
4. `env_physexplosion` — the kick that throws the barrel.

The remaster has to keep those four roles distinct. Combining them into "the fire
effect" loses either the look, the damage, or the physics.

---

## 2. The particle framework

### 2.1 It is Troika's, not Source 2007

`func_precipitation` is absent from every game DLL. The particle keywords
(`movealign`, `theta_speed`, `min_frames`, `"collide spawn for %s"`) live in
`Bin/engine.dll`. Valve's `.pcf` system arrives in Source 2007, three years later, and
is a different design. `particle_definition` always names a `particles/<name>.txt`
file — bare, with the directory, or with the `.txt` suffix; all three spellings resolve
to the same definition.

The public FGD (`@PointClass env_particle`, `@SolidClass func_particle`) agrees. There
is no `info_particle_system` and no `.pcf` in the install.

### 2.2 The shipped corpus (patch-first mirror)

`$ELYSIUM_EXPORT_ROOT/particles/` holds **1,698** definitions and **318** TGA sprites
(each also as a normalized PNG). Parser: `pipeline/src/elysium_pipeline/formats/particles.py`.
Mirror: `pipeline/src/elysium_pipeline/exporters/UE_extract_particles.py`.

Roles, distinguished by content rather than by a declared type:

| Role | Count | Shape |
|---|---:|---|
| Drawing particle | 1,054 | has `sprite`, no `spawn` |
| Emitter-only wrapper | 590 | has `spawn`, no `sprite` |
| Both | 32 | draws and spawns children |
| Neither | 22 | empty / template / broken |
| `collide { }` | 81 | impact child and/or decal |
| `precipitation "1"` | 17 | gated by `particles_enable_precipitation` |

The grammar — scalar / `a~b` range / `a,b(n)` keyframe, emitter vs particle keys — is
`docs/vtmb/weather.md` → "The particle-definition format". This document does not
repeat it.

Name-prefix census of the 1,698 files (a definition can match more than one row):

| Intent (from the filename) | Files |
|---|---:|
| Discipline (`d_*`, Animalism, Thaumaturgy, …) | 646 |
| Muzzle / combat / explosion / blast | 308 |
| Glow / aura / teleport / summon / charge | 305 |
| Fire / flame / ember / torch | 219 |
| Blood / feed / vomit | 210 |
| Smoke / steam / fog / cigar / ash | 143 |
| Named boss (Andrei, Chang, Ming, Sheriff, Tzimisce, Sabbat) | 121 |
| Body / bone / trail attach | 73 |
| Water / splash / drip / spray | 50 |
| Dust / debris / glass / break | 37 |
| Menu / widescreen / cursor | 33 |
| Lights / flare / citylight / airplane | 32 |
| Rain / weather | 32 |
| Unclassified by name | 254 |

The unclassified remainder is still the same language: moths, flies, frost, electrical
strikes, HUD shock, drag sparks, health orbs, cutscene panels, demo clouds. There is
no second particle format hiding in that tail.

### 2.3 Keys the current compiler rejects

`compile_definition` is strict: a live key outside the known contract is an export
error, and the map exporter records the root as `unresolved` rather than dropping the
whole map. Across the 23 exported maps, 21 roots fail for these reasons:

| Unsupported field | What it means | Typical consumer |
|---|---|---|
| `normal`, `refract` | a second texture used as a heat-haze / refraction card | `fire_heat` on every `barrelfireemitter` |
| `rotate` (particle body; spawn already allows it) | sprite spin spelling variant | debris (`debries`) |
| `timescale` (inside `spawn`) | moth path timing | `moth_emitter` |
| `frames` inside `spawn` | per-child lifetime override | gasoline-trail fire |
| `sortfront` enabled | translucent draw-order hint | blood-guardian, warehouse sparks |
| `lighting` | lit vs unlit sprite | Potence death-blow smoke |
| `radius` on a drawing particle | size-as-radius spelling | warehouse HUD explosion |
| malformed brace files | hand-edited / patch-broken text | `fire2_emitter`, Tourette suicide |

`refract` + `normal` is the one that takes out every barrel fire. The heat card is a
presentation layer on top of flames/smoke/embers; a remaster can drop the 2004
refraction trick and still keep the fire.

---

## 3. How the game places an effect

Five independent producers name into the same `particles/*.txt` pool. Covering the
maps' `env_particle` entities is not covering the game.

### 3.1 Map entities (the placed set)

Counts are over the 23 currently exported maps.

| Class | Count | Role |
|---|---:|---|
| `env_sprite` | 1,424 | additive billboards: coronas, volume-light shafts, candles, cop flashers, lightning, moon |
| `infodecal` | 879 | projected blood, bullet holes, graffiti, posters, stains |
| `env_particle` | 188 | point emitter; `particle_definition` names the root |
| `params_particle` | 49 | reusable named template, almost always dialog Dominate / Presence |
| `env_fade` | 37 | full-screen colour fade |
| `func_particle` | 24 | brush-volume emitter (rain boxes) |
| `env_physimpact` | 11 | directional physics impulse |
| `env_physexplosion` | 8 | radial physics impulse |
| `func_dustmotes` | 8 | Source dust volume (warehouse only) |
| `params_explosion` | 7 | explosion *recipe*: light + damage + shake + particle + sound |
| `env_shake` | 6 | camera shake |
| `env_shooter` | 6 | launches gib `.mdl`s (warehouse body-part fling) |
| `env_steam` | 5 | Valve leftover steam jet (clinic pipes) |
| `env_particle_hud` | 1 | HUD-space particle (warehouse explosion) |
| `point_explosion` | 1 | named consumer of a `params_explosion` |

`env_particle` I/O is recovered (`docs/vtmb/entity_io.md` → "`env_particle` attachment"):
`TurnOn` always restarts, `TurnOff` stops feeding and lets live particles finish,
`SetRateScale` / `SetRampTime` own the fade, `attach_type` 0/1/2/3 are
origin / tree / point / treecolor. Values 5, 9, 10, 11 appear and are not fully
decoded; 11 is the rain follow-emitter.

Placed `env_particle` roots on those maps, collapsed by intent:

| Definition (normalized) | Count | Intent |
|---|---:|---|
| `waterdrops_timer` | 46 | drip under overhangs |
| `dialog_domination_emitter` / `dialog_presence_emitter` | 23 + 23 | conversation-power FX, also the `params_particle` pair |
| `gasoline_fire_emitter` (+ start / wall / trail) | 22 | scripted fire (junkyard) |
| `barrelfireemitter` | 17 | barrel / drum fire |
| `steamrelease_constant_emitter` / `steamrelease_timer` | 13 | steam jets |
| `moth_emitter` | 9 | moths |
| `airplane_emitter` | 7 | distant airplane sprite |
| warehouse explosion family | 14 | scripted blast |
| `rain_follow_emitter` | 4 | viewer-tracking rain |
| `cigar_emitter` / cig-glow / cig-smoke | 8 | smoking NPCs |
| `fire1_emitter` / `fire2_emitter` / `stovefireemitter` | 5 | generic / stove fire |
| `starynight_emitter` / `glowywierd_emitter` | 2 | decorative night motes |
| discipline / cinematic one-offs | rest | wolf form, pestilence, obfuscate, potence, embrace bleed, prince decapitation, … |

`func_particle` is rain only: 14× `rain_box_noprecip_emitter`, 10× `rain_box_emitter`.

`params_particle` is almost a constant pair on every map — `dominate_particles` and
`presence_particles` at the origin, `attach_type` 2 — plus a few extras on the
junkyard. The conversation powers look up this named template rather than spawning a
fresh `env_particle`.

`env_steam` is **not** the Troika language. It is Valve's leftover jet:
`SpreadSpeed`, `Speed`, `StartSize`, `EndSize`, `Rate`, `JetLength`, `rendercolor`.
All five live on `sm_medical_1` pipe fittings. `env_particle` also receives a
`JetLength` input (2 wires), so some Troika emitters were driven with the Valve
length key; the particle files themselves do not declare it.

`func_dustmotes` is also Valve: a brush volume (`model *N`), `SpawnRate` 30,
`SpriteName materials/particle/sparkles.vmt`, pale colour, short life. Eight of them
fill `sm_warehouse_1`. They never name a `particles/*.txt`.

### 3.2 Animation events

Client `C_BaseAnimating::FireEvent` (`0x100935a0`) is the live effect bus for
anything attached to a playing sequence (`docs/vtmb/animation_and_movers.md`):

| Event | Role |
|---|---|
| 5001 / 5011 / 5021 / 5031 | player muzzle flashes |
| 5003 / 5013 / 5023 / 5033 | NPC muzzle flashes |
| 5002 | disabled spark warning |
| 5004 / 5005 | sound / `Disciplines/` sound |
| 5103 | effect teardown |
| 5111–5119 | attachment / origin emitter variants |
| 5120 | options effect from weapon attachment `slampoint` |
| 6001–6004 / 6011–6014 | viewmodel attachment bursts |

The corpus actually uses 5001, 5003, 5005, 5101–5102, 5105, 5112, 5115–5118, 5120,
6001, 6002, 6013. Feeding starts `force_feeding_emitter` from event 5116
(`docs/vtmb/feeding.md`). The Unreal bake emits **no** AnimNotify; reproducing these
is runtime work over the decoded event list.

### 3.3 Discipline records

`vdata/system/disciplinetgt_000.txt` … `_004.txt` carry `Particle_FirstPerson`,
`Particle_ThirdPerson` and (where present) `Particle_Hit`. The first-person / third-person
mode tag is recovered (`docs/vtmb/camera-view-modes.md`); whether a live emitter is
stopped, restarted or faded on a camera swap is not.

`vdata/system/stats.txt` (and the vampire / hunter variants) name the renewable-discipline
body and HUD emitters: Fortitude aura 1–5, Potence 1–5 blood-point hand/flash, Celerity
view, Bloodbuff / Blood Healing view, Protean. These are not map entities. They start
when the discipline commits and die when the active slot clears
(`docs/vtmb/disciplines.md`).

Named families in those tables: Animalism (bats, beetles, ravens, wolves, pestilence,
brood, bloodsuckers), Dementation, Dominate (including possession and suicide-pact),
Presence, Thaumaturgy (blood strike / shot / rip / boil / shield / theft / cauldron).
That is most of the 646 `d_*` files.

### 3.4 Weapon items

Ranged `vdata/items/item_w_*.txt` name `muzzleflash_particle` / `viewmuzzleflash_particle`
and a per-gun emitter (`w_thirtyeight_emitter`, `w_ithaca_m_37_emitter`, …). The flaming
crossbow and flamethrower name fire emitters; Ming Xiao spit and the Tzimisce head name
vomit / launch emitters. These fire from the animation-event path above, not from a map
entity.

### 3.5 Impact table

`vdata/system/particleimpacttable.txt` is a **surface × weapon** matrix. Each
`Material` block has a `Default_Effect` and optional per-weapon overrides. Surfaces
with their own default:

| Surface | Default emitter |
|---|---|
| concrete | `Impact_Concrete_Emitter` |
| metal / vent / grate / computer / gargoyle_soak | `Impact_Metal_Emitter` |
| wood | `Impact_Wood_Emitter` |
| dirt | `Impact_dirt_Emitter` |
| glass | `Impact_Glass_Emitter` |
| flesh | `Impact_Flesh_Emitter` |
| flesh_soak | `Impact_Soak_Emitter` |
| gargoyle | `Impact_Gargoyle_Emitter` |
| fortitude_soak | `Impact_Fortitude_Soak_Emitter` |
| bloodshield_soak / special_dmg_soak | `Impact_Bloodshield_Soak_Emitter` |
| ming_xiao | `Ming_Xiao_Hit_Emmitter` |
| ming_xiao_tentacle | `Ming_Xiao_Baby_Hit_Emmitter` |

Weapon overrides that matter: shotgun / MAC-10 variants per surface, flaming-crossbow
→ `Impact_FX_Explosion`, flamethrower / torch on flesh →
`flamethrower_hit_flame-emitter`, claws → `ImpactClaws_flesh_Emitter`, melee →
`ImpactFX_Melee_Generic`, most firearms on flesh → `ImpactFX_Ranged_Generic`.

Audio for the same hit is a different table (`docs/vtmb/surface_properties.md`). The
impact particle and the impact sound are sibling lookups, not one asset.

### 3.6 Main menu

`resource/mainmenuparticles.txt` is a 3D scene in the same language: camera, skybox,
three emitter groups (clouds + fire, clan seals, blood cels). Reconstruction:
`docs/vtmb/m0_menu_build.md` §9.

---

## 4. Effect families the remaster has to handle

This is the closed list of *kinds*. Every shipped effect is one of these, or a bundle
of them (the barrel in §1).

### 4.1 Sprite billboards (`env_sprite`)

Camera-facing additive quads. Material selects the family
(`docs/vtmb/entity_visuals.md` §4):

| Material | Count | Intent |
|---|---:|---|
| `sprites/glowa` | 969 | lamp / bulb corona |
| `sprites/volumelighta` / `volumelightb` (+ `_proxyfade`) | 186 | light shaft, elongated |
| `sprites/streetlight3_proxyfade` | 74 | street-lamp halo |
| `sprites/glowb` | 58 | second corona |
| `sprites/candle` | 44 | flame card, `framerate` animates |
| `sprites/coplights` / `coplightsb` | 50 | cop-car flasher |
| `sprites/lightning` / `lightningglow` | 42 | lightning flash (parented to `lightningrotator`) |
| `sprites/moonhalola` | 1 | moon |

`rendermode` 3 is occlusion-faded glow; 5 is plain additive. StartOff sprites stay
dark until I/O. This is a billboard problem, not a particle-graph problem.

### 4.2 Fire and embers

Emitter wrappers spawn the same four drawing leaves: a flame card (`Flamemass` /
`FirePlace_Flames`), a glow, `Smoke1`, `FlameEmbers1`. Optional fifth: `Fire_Heat`
(refraction). Placed as barrel / stove / gasoline / generic `fire1`. Also body-fire
(`body_fire_emitter`), crossbow flame, flamethrower, Molotov, torch, and several
discipline / boss casts.

Intent: a local rising, flickering volume with sparks and a smoke plume. Not a
fluid-dynamics authoring. The heat card is a 2004 refraction sprite.

### 4.3 Smoke, steam, fog, ash, cigars

Continuous, slow, often additive grey sprites (`furball`, `cloud`) with
`radius_speed` + `elevation_speed`. Steam is a directional cone (`phi`/`theta` ±10°,
rate 50). Cigar smoke is the same language attached to a bone (`attach_type` 2).
Ash and rainfog are large, slow, camera-facing cards. `blowsmoke_*` is an animated
exhale.

`env_steam` on the clinic is the Valve jet with the same intent and none of the
Troika keys.

### 4.4 Rain, drips, lightning, wetness

Owned by `docs/vtmb/weather.md`. Follow emitter + box volumes + `WaterDrops_Timer` +
sprite lightning + `FadeGlobalWetness`. Largest coherent consumer of the particle
language, not the only one.

### 4.5 Dust motes and hanging particulates

`func_dustmotes` on the warehouse, plus `starynight_emitter` / `glowywierd_emitter`
and moth / fly emitters. Intent: sparse, slow, local motes that sell volume.

### 4.6 Blood, feeding, vomit

Sprays, drips, trails, explosions, splash decals. Feeding uses
`force_feeding_emitter`. Embrace uses neck-bleed emitters. Thaumaturgy is the large
authored set (blood strike / shot / boil / shield). Impact on flesh is
`Impact_Flesh_Emitter` and friends. Several collide-blocks lay a blood decal.

### 4.7 Muzzle flash, tracers, shells

Per-gun first- and third-person bursts from animation events + item records. A
muzzle is a 3-burst flash card with a smoke child, not a continuous emitter. Tracers
are `bullettrail_emitter` (distort + ring). Shell ejection, when present, is a mesh
or sprite burst from the same event bus.

### 4.8 Impact hits

The `particleimpacttable.txt` matrix. Concrete dust, metal sparks, wood chips, dirt
puff, glass shards, flesh spray, plus soak / Fortitude / Bloodshield specials and
Ming Xiao. A hit is a short burst at a contact point, sometimes with a decal.

### 4.9 Explosions (the bundle)

`params_explosion` is the recipe, `point_explosion` (and scripted I/O) is the trigger:

| Key group | What it authors |
|---|---|
| `dl_color` / `dl_radius` / `dl_time` / `dl_decay` | a point light that dies |
| `dmg_amount` / `dmg_radius` / `damage_players|npcs|breakables` | damage |
| `shk_amp` / `shk_freq` / `shk_dur` / `shk_radius` / `shk_inair` | camera shake |
| `particle` | a Troika emitter (`Explosion2_emitter`, `LightBulb_Pop_emitter`, …) |
| `snd_name` / `snd_dist` / `snd_pitch_*` | sound |

`env_physexplosion` / `env_physimpact` are the physics half of the same beat.
`env_shake` is the standalone shake (elevator, bobcat, warehouse blast).

### 4.10 Debris, gibs, breakables

`env_shooter` launches one or more `.mdl`s (`models/gibs/hgibs*`) with velocity,
variance, lifetime. Warehouse scripted dismemberment. `func_breakable` is the brush
that breaks; its *look* is a model swap plus whatever particle the mapper wired.
Particle-side debris (`debries`, column-break, barrel explosion FX) is sprites, not
gibs.

### 4.11 Discipline and conversation presentation

Cast bursts, target auras, body smoke, HUD overlays, first-person view emitters,
resist flashes, form transitions (wolf, obfuscate in/out). Dialog Dominate / Presence
use the per-map `params_particle` pair. Celerity's `Fx_Motion_Trail` and Obfuscate
translucency are trait flags that a renderer consumes; they are not particle files
(`docs/vtmb/disciplines.md`, `docs/vtmb/game_runtime.md`).

Auspex aura-sight and Protean heat vision are vision modes (post-process / material),
not emitters, even though Auspex also ships aura sprites.

### 4.12 Boss and cinematic one-shots

Andrei, Chang, Ming Xiao, Sheriff, Lasombra, Tzimisce, prince decapitation, Tourette
suicide, warehouse HUD explosion, cutscene widescreen panels. Same language, unique
roots, often `attach_type` point/tree on a named parent.

### 4.13 Screen and HUD

`env_fade` (already a real class) is a colour fade with duration / hold.
`env_particle_hud` and the `d_*_hud_*` / `hud_*` definitions draw in view space.
Menu particles are a dedicated scene.

### 4.14 Projected decals

`infodecal` is the authored layer (blood, holes, graffiti) and is already a bake
product (`UDecalComponent`). Particle `collide { decal { particle … } }` is a
*runtime* decal spawned at an impact. Same Unreal primitive, different lifetime.

### 4.15 Water surface

Map water is a material / Single Layer Water problem (`docs/project/plans/world.md`
7.3), not a particle. Splashes, bubbles and rain-on-water are particles that land on
it.

### 4.16 Fog and atmosphere

Height fog, sky ambient and the 3D skybox are `docs/vtmb/sky-ambience.md`. Particle
fog (`rainfog`, `rainmist`, Animalism ground fog) is a local card emitter, not the
map fog.

### 4.17 Lights that pretend to be effects

Switchable `light` / `light_spot` (8 named lights in the whole exported set), fire
point-lights inside `params_explosion`, candle flicker via `env_sprite` frame
animation. The look of a fire is particles; the illumination is a light.

---

## 5. What is already decoded vs what is still open

Decoded and in the export:

- the particle language and the patch-first mirror;
- per-map `<map>.particles.json` (placed `env_particle` + the
  `force_feeding_emitter` gameplay root + unresolved list);
- sprite TGA → PNG;
- `env_sprite` `.sprites` sidecar;
- `infodecal` `.decals` sidecar;
- `.phy` → `.phys` for physics props;
- rain / wetness contract for `sm_hub_1` (`docs/vtmb/weather.md`).

Open, and they matter for a remaster even if the *look* is modernized:

- `attach_type` values above 3 (5, 9, 10, 11) — 11 is the rain-follow hypothesis;
- `func_particle` volume sampling;
- `frames` / `fps` / `v(n)` keyframe units (data-supported, not retail-captured);
- sprite blend vs `mask` (additive vs translucent);
- first-person / third-person particle lifetime across a camera swap;
- `env_steam` / `func_dustmotes` as Valve classes (keys are public, runtime not RE'd
  in this repo);
- exact `env_physimpact` spawnflag bits.

None of those block listing the families or choosing an Unreal stand-in. They
constrain how faithfully a Niagara system is *driven*, not whether fire is fire.

---

## 6. Related docs

- `docs/architecture/effects-architecture.md` — Unreal mapping and what to open in
  the 5.8 install.
- `docs/vtmb/weather.md` — grammar, rain, wetness.
- `docs/vtmb/phy_vphysics.md` — `.phy` collision.
- `docs/vtmb/entity_visuals.md` — sprites, ropes, render keys.
- `docs/vtmb/entity_io.md` — `env_particle` attach / TurnOn / TurnOff.
- `docs/vtmb/disciplines.md` — when discipline FX start and stop.
- `docs/vtmb/animation_and_movers.md` — animation-event IDs.
- `docs/vtmb/surface_properties.md` — impact *audio* matrix.
- `docs/vtmb/camera-view-modes.md` — first / third person particle tags.
- `docs/vtmb/m0_menu_build.md` — menu particle scene.
- `docs/vtmb/feeding.md` — `force_feeding_emitter`.
