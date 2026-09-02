# Effects — every visual and impulse effect VtMB authors

VtMB does not ship Valve's later `.pcf` particle editor. Troika forked `engine.dll` and
authored a text-driven particle language under `particles/`, then placed it from map
entities, animation events, discipline records, weapon items, an impact table, and the
main menu. Separate Source leftovers (`env_steam`, `func_dustmotes`, `env_shake`,
`env_fade`) and two physics-impulse entities sit beside that system. This document is
the inventory of those families and the contracts a reconstruction has to cover.

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

| Class | Count (108 maps) | Input | What it does [decompiled] |
|---|---:|---|---|
| `env_physimpact` | 130 | `Impact` | a trace from the entity along its direction (flag 2: infinite ray); `mag = (flags & 11) ? magnitude : (1 − trace.fraction) × magnitude` — flag 1 no fall-off, 4 multiply by mass, 8 ignore the surface normal — then `ApplyForceOffset(−normal × mag × phys_pushscale, endpos)` on the hit body. Corpus flags: 1 (60), 15 (16), 7 (14), 14 (12), 12 (10) |
| `env_physexplosion` | 61 | `Explode` | reads **no spawnflags**; for each body within `radius` (else `magnitude × 2.5`) passes a zero force into `TakeDamage(DMG_BLAST, max(0, magnitude − dist × 0.4))`; the push is `CBaseEntity::VPhysicsTakeDamage` (`vampire.dll 0x100A1580`): a **world-only** trace from the explosion to the body; with clear LOS `force = normalize(body − origin) × damage × phys_pushscale` at the **centre of mass** (no torque, no division by mass); with blocked LOS, nothing |

Both cluster on `sm_junkyard_1` (cars, exploding barrels) and `sm_warehouse_1` (the
scripted blast). Keys: `magnitude`, plus `directionentityname` / `target_position`
(`env_physimpact`) or `radius` / `targetentityname` (`env_physexplosion`). They move
already-simulated Chaos/VPhysics bodies. They do not draw. The Unreal impulse is
`docs/architecture/physics-architecture.md`'s seam; `env_physexplosion`'s LOS-gated
centre-of-mass push is **not** a linear-falloff radial impulse, and
`effects-architecture.md` §5.10 states which seam call each class makes.

A typical junkyard barrel is therefore four authored pieces, not one:

1. `prop_physics` + the model's `.phy` — the body that can fly.
2. `env_particle` `barrelfireemitter` — the fire/smoke look.
3. `params_explosion` — flash light, damage, optional shake, a *different* particle
   (`Explosion2_emitter`), and a sound.
4. `env_physexplosion` — the kick that throws the barrel.

The reconstruction has to keep those four roles distinct. Combining them into "the fire
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

A definition file name can carry a space — the install ships
`particles/tz_ bloodtrickle_emitter.txt`, and the engine resolves it like any other file — so
the parser accepts spaces in source names; the Unreal-side asset name folds them.

Roles, distinguished by content rather than by a declared type:

| Role | Count | Shape |
|---|---:|---|
| Drawing particle | 1,054 | has `sprite`, no `spawn` |
| Emitter-only wrapper | 590 | has `spawn`, no `sprite` |
| Both | 32 | draws and spawns children |
| Neither | 22 | empty / template / broken |
| `collide { }` | 81 | impact child and/or decal |
| `precipitation "1"` | 17 | gated by `particles_enable_precipitation`, and at runtime by the particle's leaf sky bit (§2.4) |

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

### 2.3 Keys the legacy compiler rejects

`compile_definition` (the legacy lane) is strict: a live key outside its closed contract is
an export error, and the map exporter records the root as `unresolved` rather than dropping
the whole map. Corpus-wide, **42 of the 155 placed roots never resolve, the top four among
them** — `fire2_emitter` (204 rows, 6 maps), `fire3_emitter` (140, 13 maps),
`barrelfireemitter` (115, **every map**), `moth_emitter` (31, 10 maps):

| Field the compiler rejects | What the runtime does with it (§2.4) | Typical consumer |
|---|---|---|
| `normal`, `refract` | a second sprite drawn as a DUDV refraction card on `engine/particlerefract` | `fire_heat` on every `barrelfireemitter` |
| `timescale` (inside `spawn`) | divides the child's lifetime | `moth_emitter` |
| `sortfront` enabled | forces the particle to the front of the sort | blood-guardian, warehouse sparks |
| `lighting` | a per-particle branch sampling at the particle position | Potence death-blow smoke |
| `rotate` (particle body), `radius` on a drawing particle, `frames` inside `spawn` | **not in the runtime's key tables — read by nothing** | debris (`debries`), warehouse HUD explosion, gasoline-trail fire |
| malformed brace files | the engine's lexer structures what it can | `fire2_emitter`, Tourette suicide |

The V2 particle unit (`docs/architecture/seam_map_particle.md`) decodes every one of these
like any other key, and only three placed references are truly broken
(`d_animalism_pestilence_cast_emitter`, `smoke3`, a `{` typo). The heat card is authored
intent, not an accident of 2004: it is wired on the refraction master, never dropped
(`effects-architecture.md` §5.4).

### 2.4 The runtime, decoded [decompiled]

`CParticleManager` (`engine.dll`, vtable `0x201751a4`). The authored surface is **two key
tables dumped from the binary** — 18 particle keys and 20 spawn keys, each
`{name, default, isAngle}` — plus a dozen flags. There is nothing else.

| Particle key (default) | Spawn key (default) |
|---|---|
| `red green blue color mask` (255) | `red green blue color mask` (255) |
| `refract` (1), `width height size` (1) | `refract width height size` (1) |
| `rotation` (0, angle) | `rotation` (0, angle) |
| `radius_speed theta_speed phi_speed` (0) | `radius` (0), `theta phi` (0, angle) |
| `x_speed y_speed z_speed elevation_speed` (0) | `x y z elevation` (0) |
| `parent_speed` (1) | `timescale` (1), `rate` (0), `burst` (0) |

Scalars and flags: `fps` (default **30**), `frames` (default = `fps`, a one-second life),
`min_frames` / `max_frames` (default = `frames`), `loop`, `flat`, `sortfront`, `movealign`,
`lighting`, `no_z_test`, `depth_offset`, `precipitation`, `surface_color` /
`use_surface_color` / `ignore_surface_color`, `sprite`, `normal`, `spawn {}`, `collide {}`.
Spawn blocks also accept `distance "1"` (rate becomes particles per unit travelled) and
collide blocks accept `drag`, `self`, `vdecal_first` / `vdecal_last` / `vdecal_angle_spread`.

- **Lifetime.** `lifetime = frames / fps` seconds; each particle rolls an inverse age rate in
  `[fps / max_frames, fps / min_frames]` and advances a **normalized age in [0, 1]**. `loop`
  wraps the age. `timescale` divides the child's lifetime.
- **Ramps.** `a,b(n)`: `(n)` is a **frame index normalized by `frames`**, negative wraps from
  the end; the first keyframe must sit at 0; interpolation is **linear in normalized age**;
  the angle keys (`rotation`, `theta`, `phi`) interpolate the shortest way round. `a~b` inside
  a keyframe is rolled per particle. This closes `weather.md`'s items 4 and 5 and RE23.
- **Emission.** `rate` is particles per second through an accumulator, **sub-frame
  interpolated** along the emitter's movement; `burst` is keyframe index 19 and adds
  `RandomFloat(v0, v1)` to the accumulator when its keyframe fires (the only key allowed a
  first keyframe at `t ≠ 0`). One per-emitter float (`particle + 0x19c`) multiplies **both**
  `rate` and `burst`; `SetRateScale`, `func_particle`'s volume scalar and the rain follow all
  drive that one float. No per-frame cap.
- **Motion.** Every particle carries a spherical offset `(radius, theta, phi)` **around the
  emitter origin**, rebuilt each frame from `radius_speed / theta_speed / phi_speed`;
  `x/y/z_speed` are velocities in the **emitter's basis** (X forward, Y up, Z right);
  `elevation_speed` is world Z. `parent_speed` (default **1**) is the fraction of the parent's
  movement live particles inherit, so **live particles follow a moving parent**.
- **Drawing: one material, one blend, and `mask` is a blend interpolant.** Every particle draws
  on `engine/particlenormal.vmt`, shader `Sprite`, `$spriterendermode 8`, from one procedural
  atlas of all 318 sprites (decoded at gamma 2.2), in batches of 1,024. Mode 8 in
  `stdshader_dx8.dll` (`FUN_1000ECA0`) is `BlendFunc(ONE, ONE_MINUS_SRC_ALPHA)`, depth test
  on, depth write off, vertex colour modulated:

  ```
  dst = tex.rgb × vcol.rgb  +  dst × (1 − tex.a × vcol.a)
  ```

  Vertex alpha never multiplies the source colour; it only erases the destination. `mask 0`
  is **pure additive**, `mask 255` is an **occluding alpha-blended card**, and every value
  between is a continuous additive↔translucent knob — which is why rain's `mask "0"` draws.
  Intensity is `red/green/blue × color × the spawn-side scales`, folded by `1/255³` so
  all-255 is unity. `surface_color 0` / `use_surface_color 0` / `ignore_surface_color 1` are
  three spellings of one opt-out that pins the tint to white every frame; there is no
  lightmap or surface sample anywhere in the particle code — the "surface colour" is the
  creator's RGB triple (`env_particle`'s `m_fRed/Green/BlueScale`, or the impact spawn's
  constant `0.8`).
- **Facing, sort, depth.** `movealign` aligns the quad's vertical axis to the velocity; `flat`
  uses the particle's own basis (also the forced mode of a collide decal); `sortfront` forces
  the particle to the front of the sort; `no_z_test` is a second render list drawn after the
  first; `lighting` takes a per-particle branch that samples at the particle position
  (INFERRED that the sample is light — whether `0x200d3840` is that sample is the one open
  item; six placed leaves use it); `depth_offset` is world units of view depth, applied to the
  sort key and the quad centre. `normal` + `refract` draw the batch on
  `engine/particlerefract` (a DUDV card, DX8+).
- **Size.** The atlas stores normalized aspect half-extents (long axis 0.5), so `halfX =
  aspectX × width × size × spawnWidth`, `halfY = aspectY × height × size × spawnHeight`. For a
  square sprite `size` is the quad edge in inches; `raindrops2` (5 × 25 `DropletFast`,
  `size 3`, `height 10`) is a **1.5 cm × 76 cm** streak, not the 7.6 × 25.4 `weather.md`
  once read.
- **Collision.** With `collide {}` authored, each frame's movement is traced with the engine
  ray trace, mask `SOLID | WINDOW | GRATE | MOVEABLE` — world **and** brush entities. On hit
  every collide record runs: a `particle` record spawns at the impact, a `vdecal_*` record
  lays a random decal from the range, `self` keeps the particle alive with `v' = bounce ×
  normal + friction × tangent`, then `gravity` and `pow(drag, dt)`. Defaults bounce 1,
  friction 1, gravity 0, drag 1.
- **Precipitation is geometry-gated at runtime** (`0x200d3f10`): a particle under a
  `precipitation "1"` root dies the moment its BSP leaf lacks the sky-visible bit. Rain stops
  under cover regardless of the authored `_NoPrecip` pairs.
- **Emitter lifecycle.** create (`vfunc13`), tick returning world bounds (`vfunc15`), **stop =
  clear `loop`, set flag 0x200, feeding stops, live particles finish** (`vfunc16`), free
  (`vfunc14`).

What the placed closures use (155 roots, definitions not placements): `movealign` 168,
`depth_offset` 160, `width` 149, `burst` 72, `flat` 54, `sortfront` 34 (all Animalism /
Dominate / blood-guardian casts), `precipitation` 19, `normal` + `refract` 8 (`fire_heat`, four
discipline / boss cards, `warrens_tube_water_fx1`), `lighting` 6, `timescale` 4, `no_z_test` 4
(the ethereal flame), `use_surface_color` 1, `distance` 0; `collide {}` on 26 roots / 209
placements (rain, drips, blood, sparks), `decal {}` on 16 / 174, `self {}` on 3; twenty
definitions are `both` roles (`drip`, `raindrops2`, `debries`, `airplaine`); the deepest placed
root has 20 leaves at depth 4 (`blood_guardian_summon_emitter`), the fire roots 2–5.

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
`TurnOn` (`0x100fb7a0`) always restarts — the client rebuilds the emitter whenever the
activation timestamp changes; `TurnOff` stops feeding and lets live particles finish;
`SetRateScale` / `SetRampTime` write a target the client approaches **linearly** into the one
rate float of §2.4. The `attach_type` enum has **19 values**, dumped at `vampire.dll
0x105a7000`, and the animation-event spawn `mode` argument is the same enum:

| # | Name | # | Name |
|---|---|---|---|
| 0 | `FollowOrigin` | 10 | `PlayerBox` (`spawnbounds` cube round the viewer, wrapping) |
| 1 | `BoneTree` | 11 | `PlayerSky` (as 10, spawns above the viewer — the rain follow) |
| 2 | `BoneSinglePoint` | 12 / 13 | `PlayerSphereEdge` / `FollowPlayerSphereEdge` |
| 3 | `BoneTreeWithColors` | 14 | `ScreenCenter` |
| 4 | `BoneHitboxVolumes` | 15 | `BrushEmitter` (forced by `func_particle`) |
| 5 | `ScreenBorder` | 16 | `ScreenRandom` |
| 6 | `ModelAttachment` (origin **and basis** each tick; the muzzle mode) | 17 | `ModelAttachmentNoFollow` |
| 7 | `ScreenBottomAndSides` | 18 | random point on the parent's visible skin |
| 8 | `EntitySimulatedPoint` | 9 | `EntityBox` (random point in the parent's render OBB) |

Corpus usage over 1,304 rows: `0` 1,085 · `1` 111 · `2` 45 · `-1` 29 (falls to the switch
default; treated as origin, INFERRED) · `11` 14 · `6` 11 · `17` 5 · `10` 2 · `9` 1 · `5` 1 (the
`sm_warehouse_1` HUD blast). Modes 4, 7, 8, 12–14, 16, 18 are placed nowhere and reachable only
from code that passes 1, 2 or 6. **No spawnflag is read**; `active` (constructor 1) is the
start-off (274 rows author `active 0`). The other keyfields: `attach_point` (`m_nAttachPoint`),
`spawnbounds` (`m_fSpawnBounds`, default **512**; 228 rows author it) — the FGD's `bounds`
(1,110 rows) is **not a keyfield** and is read by nothing — `ramp_scale` (16 rows at 2, 10 at
0.5, 5 at 0), `ramp_time` (up to 10 s), and the code-only `m_fRed/Green/Blue/MaskScale` /
`m_fSizeScale` (no key name, all 1). **`JetLength` is not an input on this class** — the two
map wires are dead. `Activate` removes the entity if the definition does not resolve.

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

`func_particle` (98 rows corpus-wide) happens to be used only for rain boxes
(`rain_box_noprecip_emitter`, `rain_box_emitter`), but the class has **no rain-specific
behaviour** [decompiled]: it spawns at a uniform random point in the brush's **world-aligned
AABB, with no solid test**; `Activate` sets `sizeScalar = |dx| · |dy| · |dz| × 2⁻²¹` (a
128-unit cube = 1.0), clamped to `[0.01, 100]` with a warning, and **always forces
`attach_type 15`**; the client writes `sizeScalar × rampedRateScale` into the one rate float,
so the box volume scales `rate` and `burst` alike.

`params_particle` (227 rows) is almost a constant pair on every map — `dominate_particles`
and `presence_particles` at the origin, `attach_type` 2 — plus a few extras on the junkyard.
**It is a precache stub** [decompiled]: its only reader is its own `Precache`. The dialog and
discipline auras are created by name from the discipline record walker (`vampire.dll
0x101dd090`), not by looking up this entity; there is nothing to build for it beyond an inert
class.

`env_steam` (11 rows, all `type` 0 normal) is **not** the Troika language. It is Valve's
leftover jet, `CSteamJet` [decompiled]: `lifetime = JetLength / Speed`; square spread
`fwd · Speed + up · ±SpreadSpeed + right · ±SpreadSpeed`; roll `rand(0, 360)` spinning at a
hardcoded `±8 deg/s`; `alpha = renderamt / 255 × sin(π · life / die)`; **size ramps by raw
elapsed seconds**, `StartSize + (EndSize − StartSize) × t`, not by normalized life. Three
parameter sets in the corpus (`Speed` 30 / 120 / 160, `JetLength` 128 / 80 / 120, `Rate` 26 /
35 / 24). No `Rollspeed`, no `JetLength` input. The two `JetLength` wires on `env_particle`
are dead (above); no Troika emitter was ever driven by the Valve key.

`func_dustmotes` (82 rows) is also Valve, `C_Func_Dust` [decompiled], CPU: a brush volume
(`model *N`); it retries up to 10 times for a point **inside the brush solid**; velocity
`±SpeedMax` on all axes, **no gravity**; X/Y ease toward the engine wind; `alpha = (viewZ /
DistMax + 1) × sin(π · life / dieTime) × Alpha`, drawn only when `≥ 0.5`, culled past
`DistMax`; SCALEMOTES gives constant screen size. Corpus values are nearly uniform: `SpawnRate`
10 or 20, size 7–12, `SpeedMax` 2, life 3–5 s, `DistMax` 1024, colour `205 201 182`, alpha
100; the warehouse eight are rate 30, size 5–15, `203 202 217`, alpha 90. They never name a
`particles/*.txt`.

`env_beam` (47 rows, all `sprites/beama`, `TextureScroll 35`, `Radius 256`) [decompiled]:
**`EndWidth = BoltWidth × 0.1`** — every VtMB beam tapers; 42 are continuous (`life 0`), 5
strike (`life .1`, `StrikeTime` 2–5); `NoiseAmplitude` (0, 15 or 200) scales by beam length
/ 100 over 128 divisions; additive; a strobing beam is a **temp entity**, only the continuous
one is a persistent `CBeam`; `damage` (8 rows at 1, 7 at 600, 5 at 100) is one trace along the
straight axis. Troika's `impact_particle` (19 rows) and `faces_player` (26 rows at 1) are
**inert**: the first is precached and never spawned, the second latches a send-prop no client
code reads. No `HDRColorScale`, `TouchType`, `framerate`.

### 3.2 Animation events

Client `C_BaseAnimating::FireEvent` (`0x100935a0`) is the live effect bus for
anything attached to a playing sequence (`docs/vtmb/animation_and_movers.md`):

| Event | Role |
|---|---|
| 5001 / 5011 / 5021 / 5031 | player muzzle flashes |
| 5003 / 5013 / 5023 / 5033 | NPC muzzle flashes |
| 5002 | disabled spark warning |
| 5004 / 5005 | sound / `Disciplines/` sound |
| 5103 | **remove all model decals** — not effect teardown; nothing in the bus stops an emitter |
| 5111–5114 | emitter on attachment 1–4 |
| 5115 / 5116 | emitter on `eyes` / `mouth` |
| 5117 | emitter at the origin (mode 1) |
| 5118 | emitter on a **bone named in a `;`-split option string** (mode 2) |
| 5119 | emitter on `crotch` |
| 5120 | emitter on the weapon's `slampoint` |
| 6001–6004 | shell ejection on attachment 1..4, repeated |
| 6011–6014 | clip ejection on attachment 1..4, once |

Every 511x name is the raw `.mdl` option string, never an item or discipline record, and
the spawn mode it passes is the `env_particle` enum of §3.1. The corpus actually uses 5001,
5003, 5005, 5101–5102, 5105, 5112, 5115–5118, 5120,
6001, 6002, 6013. Feeding starts `force_feeding_emitter` from event 5116
(`docs/vtmb/feeding.md`). The two 60xx families are named by their own diagnostics —
`"weapon does not have attachment for shell ejection!"` and the clip-ejection twin — in both
`C_BaseViewModel::FireEvent` (`0x100ab530`) and the weapon hook (`0x1009c970`). The attachment
index is the event id less 6000 or 6010; 6001–6004 parse `"%d %d"` where the second integer is a
repeat count defaulting to 1, 6011–6014 parse a single integer and fire once. Both spawn a client
temp-entity at the attachment's origin and angles through the temp-entity manager, so a shell or
magazine is a dropped prop, not a particle handle. The two handlers split on camera mode: the
viewmodel's runs only when `ShouldDrawLocalPlayer` is false and the world weapon's only when it is
true, so exactly one spawns the effect. What the parsed integers select is not recovered.
**What would close it:** the manager's dispatch body, cross-referenced against the authored option
strings on `v_m37`'s `fire01`/`fireempty01`. The Unreal bake emits **no** AnimNotify; reproducing these
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

Ranged `vdata/items/item_w_*.txt` name a per-gun emitter (`w_thirtyeight_emitter`,
`w_ithaca_m_37_emitter`, …) in four muzzle keys, plus `projectile_particles` for the tracer
(default `BulletTrail_Emitter`). The flaming crossbow and flamethrower name fire emitters;
Ming Xiao spit and the Tzimisce head name vomit / launch emitters. These fire from the
animation-event path above, not from a map entity.

#### A muzzle flash is two named emitters plus a one-frame dynamic light [static-verified]

There is no muzzle sprite and no temp entity. A muzzle flash is **two mechanisms with
separate triggers**, and reproducing only one of them loses half the effect:

1. a *flash* emitter and a *smoke* emitter, started by name on a numbered attachment from a
   client-only animation event (below);
2. a **dynamic light**, set as an entity effect bit by the server and consumed once by the
   client's per-frame animating body (§ *The light*).

The particle half is the one the item record names. The light half carries no authored
parameters at all — its colour, radius and lifetime are constants in `client.dll`.

The event ID encodes the attachment slot and the shooter:

| Event | `GetAttachment` index | Shooter |
|---|---:|---|
| 5001 / 5011 / 5021 / 5031 | 1 / 2 / 3 / 4 | player |
| 5003 / 5013 / 5023 / 5033 | 1 / 2 / 3 / 4 | NPC |

The handler derives 0–3 from the ID and asks for index + 1; attachments are 1-based, and the
request is skipped when the model declares fewer. Resolved origin and angles go to
`CTempEnts` slot 6 (`client.dll 0x1003cc90`), which forks on a first-person flag into
`0x1003d950` or `0x1003d9c0`. Both then do the same thing twice — flash, then smoke —
through the named-emitter spawn `0x100b47b0(name, origin, angles, entity, mode 6,
attachmentIndex, 1,1,1,1)`, and force one immediate simulate tick so the burst lands on the
frame that fired it. A name the particle registry cannot resolve produces
`could not create particle of type %s` and no effect.

The two names come from the parsed item record. All four keys are 128-byte strings and each
carries a default, so a gun that names none still flashes:

| Key | Record offset | Default |
|---|---|---|
| `muzzleflash_particle` | `+0x50170` | `MuzzleFlash_Emitter` |
| `muzzlesmoke_particle` | `+0x501f0` | `MuzzleSmoke_Emitter` |
| `viewmuzzleflash_particle` | `+0x50270` | `MuzzleFlash_Emitter` |
| `viewmuzzlesmoke_particle` | `+0x502f0` | `MuzzleSmoke_Emitter` |

**Almost no gun sets them.** Of the 16 `camera_class ranged` records, four name an emitter, two
blank the keys, and the other ten omit them and take the parser default. The whole of Troika's
per-weapon muzzle art is three statements:

| Profile | Weapons | `muzzleflash_particle` | `muzzlesmoke_particle` | first-person variant |
|---|---|---|---|---|
| revolver — flash **and** smoke | `thirtyeight`, `colt_anaconda` | `W_ThirtyEight_Emitter` | `W_ThirtyEight_Emitter-Smoke` | same as world |
| shotgun — own flash, **no** smoke | `ithaca_m_37` | `W_ithaca_m_37_Emitter` | `Blank_emitter` | `W_ithaca_m_37_View_Emitter` |
| | `supershotgun` | `W_SuperShotgun_Emitter` | `Blank_emitter` | `W_SuperShotgun_View_Emitter` |
| none at all | `crossbow`, `crossbow_flaming` | `""` | `""` | `""` |
| generic default | `deserteagle`, `glock_17c`, `mac_10`, `uzi`, `steyr_aug`, `remington_m_700`, `rem_m_700_bach`, `flamethrower`, `mingxiao_spit`, `tzimisce2_head` | *(absent)* → `MuzzleFlash_Emitter` | *(absent)* → `MuzzleSmoke_Emitter` | *(absent)* → `MuzzleFlash_Emitter` |

The shotguns are the only records whose first-person emitter differs from their world one, so the
`view*` keys buy VtMB exactly two distinct assets across the whole arsenal. The crossbows' blank
is load-bearing in both halves: an empty `muzzleflash_particle` also fails the server gate in
*The light* below, so a crossbow throws neither particles nor a dynamic light.

Which pair is read, and when the emitters start, depends on who is shooting:

| Case | Body | Timing | Pair |
|---|---|---|---|
| local player, first person | `C_BaseViewModel::DrawModel` `0x100aba00` | latched at `+0x78c`, emitted on the next draw | `view*` |
| local player third person, and the world weapon model | `C_BaseCombatWeapon::DrawModel` `0x1009c070` | latched at `+0x8cf`, emitted on the next draw | non-`view` |
| NPC | `C_BaseCombatCharacter::FireEvent` `0x10099c00` | immediate | `view*` |

The latch is `{bool pending, Vector origin, QAngle angles, float time, byte type, int
attachment}`, written by the event and consumed by the next `DrawModel`. The weapon's event
hook (`0x1009c970`) declines — returning false so the viewmodel handles it — when the owner
is the local player and the camera is not third-person; that is what routes first person to
the viewmodel and everything else to the world model. The NPC body prefers an attachment on
the *held weapon* and falls back to one on the character.

Two Troika behaviours to reproduce rather than tidy:

- The NPC path reads the **`view*` pair** (`0x10099cbe` / `0x10099ccf`), not the world pair.
  It is only observable on a gun whose `view*` keys differ from its world keys.
- The flash is one frame late in first and third person, because it is latched at event time
  and started from `DrawModel`. Only the NPC path starts on the event itself.

#### The light [static-verified]

The light does not ride the animation event, the item record, or the particle system. It is
Source's entity-effect-bit path, and it is the only part of a muzzle flash the *server*
triggers.

`CWeaponRanged::Shot` (`vampire.dll 0x102387b0`), in the same body that spends ammunition
and builds the fire packet, reads the weapon's item record and sets bit `0x2` on the
**shooter's** `m_fEffects` (server `+0x19c`) — `GetOwner()`, not the weapon:

```
10238881  CALL 0x10010ea6                   ; weapon->GetItemRecord()
10238886  MOV  CL, byte ptr [EAX + 0x50170] ; muzzleflash_particle[0]
1023888c  TEST CL, CL
1023888e  JZ   0x1023889e                   ; empty -> no bit, no light
10238890  MOV  EAX, dword ptr [ESI + 0x19c] ; ESI = the shooter
10238896  OR   AL, 0x2
10238898  MOV  dword ptr [ESI + 0x19c], EAX
```

That is the **only** site in either DLL that sets bit `0x2`, and it is gated on the weapon
naming a non-empty `muzzleflash_particle` — a gun with no muzzle particle also gets no
light.

The bit networks to the client, where `C_BaseAnimating`'s per-frame body (slot 17,
`client.dll 0x10093370`) consumes it:

| Property | Value |
|---|---|
| position | the entity's **attachment 1**, always — not the 1..4 the event selected |
| colour | 255 / 192 / 64, exponent 10 |
| radius | 100 Source units (254 cm) |
| decay | 2000 |
| life | `curtime` + a shared `.rdata` constant; Valve's published `MuzzleFlashCallback` uses the same constant at 0.05 s, and the byte value is not read out of the corpus |
| repeat | none — the body clears bit `0x2` after allocating, so one bit set is one light |

`decay` is Source's radius shrink rate in units per second, so the radius ramps linearly to
zero and 2000 × 0.05 s = 100 — exactly the initial radius. The light dies at the same
instant it reaches zero size, which is independent evidence that the unread life constant is
0.05 s.

It is additionally gated on the model declaring at least one attachment and on the global
no-dynamic-lights ConVar at `0x105fb298`, which gates every `CL_AllocDlight` site in the
client (explosions, gunshot decals, temp entities) rather than muzzle flashes specifically.

Both halves are therefore faithful behaviour to reproduce, but they are *not* one effect:
the particles follow the event's attachment 1..4 and the item record's per-gun emitter, and
the light is a fixed-colour constant at attachment 1 on the shooter.

Source's own muzzle *sprite* presentation ships in `client.dll` and is unreachable. The
sprite temp entity (`C_TEMuzzleFlash`, `effects/muzzleflash1`…`4`,
`MuzzleFlash_Pistol_Player` at `0x1003d770`) and the `MuzzleFlash` client effect at
`0x10044870` — a smoke puff plus a dlight with the same colour and radius constants, gated
on the registered `muzzleflash_light` ConVar (`0x100446f0`) — are reached only from
`CTempEnts` slots 22 and 24 and `CEffectsClient` slot 5, which VtMB's weapon code never
calls. The `muzzleflash_light` ConVar therefore does **not** gate the light VtMB actually
shows; the effect-bit path at `0x10093370` never reads it.

Separately, the flamethrower resolves an attachment named literally `muzzle` and drives
`Flamethrower_Muzzle_Active_emitter` / `Flamethrower_Muzzle_Inactive_emitter`
(`0x100280a0`). `ent_muzzle` is a server console command that draws the muzzleflash
attachment point.

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

The table is read **client-side** off `C_TEGunshotDecal` [decompiled]: the surface character
(a 23-entry table, soak-remapped `F → K`, Fortitude `R`, Bloodshield `Z`, misc `Q`, `A → B`)
× the weapon column (`m_iImpactID`, an `item_w_`-stripped index fixed at precache). The decal
is a **parallel** lookup on the **unremapped** character (`concrete / metal / wood / glass /
flesh / soak` × 5 + scorch + blood). Audio for the same hit is a different table
(`docs/vtmb/surface_properties.md`). The impact particle, the decal and the impact sound are
sibling lookups, not one asset.

### 3.6 Main menu

`resource/mainmenuparticles.txt` is a 3D scene in the same language: camera, skybox,
three emitter groups (clouds + fire, clan seals, blood cels). Reconstruction:
`docs/vtmb/m0_menu_build.md` §9.

---

## 4. Effect families the reconstruction has to handle

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
fluid-dynamics authoring. The heat card is a refraction sprite (`normal` + `refract`, §2.4)
and is authored intent, reproduced on the refraction master rather than dropped.

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

Per-gun first- and third-person bursts from animation events + item records; the chain,
the four record keys and the absent dynamic light are §3.4. A muzzle is a 3-burst flash
card with a smoke child, not a continuous emitter, and the smoke is a second named
emitter rather than a child of the flash. Tracers are `bullettrail_emitter` (distort +
ring) from `projectile_particles`. Shell ejection, when present, is a mesh or sprite
burst from the same event bus.

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

`params_explosion` has exactly **20 keys**; `damage_players / npcs / breakables` default
**false**; there is no decal key and no decal path; `env_explosion` does not exist
[decompiled]. `point_explosion`'s order: (1) the dynamic light if `dl_radius > 1` (`dl_color`,
`dl_exponent`, `dl_radius`, `dl_time`, radius shrinking at `dl_decay` units/s); (2) damage if
`dmg_amount > 0`, DMG_BLAST, `adjusted = dmg − dist × dmg / radius` floored at 1, an LOS ray
unless flag 0x200, targets by the three bools; (3) `TE ParticleEffect(origin, angles, name)` —
no mode argument; (4) `UTIL_ScreenShake`; (5) the sound with `snd_dist` PAS and a random
pitch. Corpus: `explosion2_emitter` 25 of 52 recipes; spawnflags mostly 111 / 47.

`env_physexplosion` / `env_physimpact` are the physics half of the same beat (§1).
`env_shake` (60 rows) is the standalone shake (elevator, bobcat, warehouse blast). Its flags
[decompiled]: 1 global (radius 0), 4 in-air, 8 physics (a vphysics motion controller on
bodies in range; one row, `13`, in the corpus). `UTIL_ScreenShake` per player: `localAmp =
amp × (1 − dist / radius)`, **linear**. The client's `CalcShake` per frame: every
`1 / frequency` seconds re-roll `offset = ±amp` (3 axes) and `angle = ±amp × 0.25`;
`frac = remaining / duration`; `s = frac² × sin(curtime × frequency / frac)`; view origin
`+= s × offset`, **roll** `+= s × angle`; `amp −= amp × dt / (frequency × duration)`. Units:
inches of translation, `amp × 0.25` degrees of roll.

### 4.10 Debris, gibs, breakables

`env_shooter` (21 rows, all `Simulation` 0) launches one or more `.mdl`s (`models/gibs/*`,
structural debris) [decompiled]: a real server gib per shot, `velocity = scatter(angles,
variance) × m_flVelocity`, angular velocity `(100–200, 100–300, 0)`, life `±5 % ×
m_flGibLife` (default **25 s**), `delay` default 0.001, **MOVETYPE_BOUNCE with a zero-extent
bbox** (the gib bounces as a point), up to 5 blood decals on landing. `nogibshadows` /
`gibgravityscale` do not exist. Warehouse scripted dismemberment. `func_breakable` is the brush
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
`env_particle_hud` (3 rows) and the `d_*_hud_*` / `hud_*` definitions draw in view space
through attach modes 5 / 7 / 14 / 16; menu particles are a dedicated scene. **The HUD and the
main menu are out of scope by owner decision (2026-09-02)** — new authored assets, not
reproductions — so these rows and modes are inventory only.

### 4.14 Projected decals

`infodecal` is the authored layer (blood, holes, graffiti) and is already a bake
product (`UDecalComponent`). Particle `collide { decal { particle … } }` is a
*runtime* decal spawned at an impact (16 placed roots / 174 placements), and the
`vdecal_first` / `vdecal_last` / `vdecal_angle_spread` collide keys lay a random decal from
a numbered range (0 placed uses). Same Unreal primitive, different lifetime; the runtime
spawn is R7.2's decal seam.

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
- `env_sprite` rows: the staged `sprites[]` table of the V2 map bake (`<map>.sprites` retired, R6.1);
- `infodecal` `.decals` sidecar;
- `.phy` → `.phys` for physics props;
- rain / wetness contract for `sm_hub_1` (`docs/vtmb/weather.md`).

Closed by the 2026-09-02 decompile pass (§2.4, §3.1, §4.9–4.10): the 19-value
`attach_type` enum and its identity with the animation-event spawn mode; `func_particle`'s
AABB sampling and volume scalar; `frames` / `fps` / `v(n)` semantics; the mode-8 blend and
`mask` as its interpolant; `env_steam` and `func_dustmotes` as Valve classes; the
`env_physimpact` flag bits; the explosion order and defaults; the shake math; `env_shooter`.

Open, and they matter for a reconstruction even if the *look* is modernized:

- first-person / third-person particle lifetime across a camera swap
  (`docs/vtmb/camera-view-modes.md`'s open item);
- whether `engine.dll 0x200d3840` is the `lighting` light sample (six placed leaves);
- `BoneTreeWithColors` (mode 3): where its per-segment tint comes from;
- what the 60xx shell / clip ejection integers select (§3.2).

None of those block the families or the stand-ins. They constrain how faithfully a Niagara
system is *driven*, not whether fire is fire.

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
