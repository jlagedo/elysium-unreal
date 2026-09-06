# Weather & wetness — the rain system

VtMB rains. Santa Monica's hub, the pier, the junkyard and downtown all run authored
precipitation, the world has a global wetness channel the level scripts drive, and five
outdoor maps carry a lightning rig. None of it is Source's weather: Troika removed Valve's
precipitation entity and built their own particle system inside a forked `engine.dll`.

This doc holds the engine-neutral facts and the Unreal plan. Status and next work live in
`docs/project/roadmap.md` — task **7.9** (the runtime), **PL12** (mirror the particle definitions),
**RE23** (close the format and the wetness semantics).

Related: `docs/vtmb/sky-ambience.md` (the 3D skybox miniature the rain draws inside), `docs/vtmb/entity_io.md`
(the I/O bus `FadeGlobalWetness` arrives on), `docs/vtmb/audio_pipeline.md` (the SoundScheme layer),
`docs/architecture/rendering-perf.md` (the translucency budget this has to fit).

## Confidence

The **entity inventory**, **patch-first particle closure**, **material proxy shape**, and
`sm_hub_1` timer graph are verified from the user's current merged install and the generated
intermediates. The **particle-definition grammar and its runtime semantics** are decoded from
`engine.dll`'s `CParticleManager` (2026-09-02; `docs/vtmb/effects.md` §2.4 owns the facts,
`docs/architecture/seam_map_particle.md` → "Semantics" the unit rules): lifetime, ramps,
emission, motion, size, the blend and `mask`, collision and the precipitation gate are
VERIFIED. `attach_type=11` is the dumped enum's `PlayerSky`. Wetness time units and
`ambient_generic` rain fades remain retail-validation hypotheses.

## It is not Source's weather

Stock Source ships `func_precipitation`, a brush entity present since HL2. VtMB has no trace of
it. String counts over the user's install:

| String | `vampire.dll` | `client.dll` | `engine.dll` |
|---|---|---|---|
| `func_precipitation` | 0 | 0 | 0 |
| `func_particle` | 2 | 0 | — |
| `env_particle` | 5 | 1 | — |
| `particle_definition` | 1 | 0 | — |
| `FadeGlobalWetness` | 2 | 0 | — |
| `GlobalWetness` | 2 | 2 | — |

The **parser lives in `Bin/engine.dll`**, not in the game DLLs — it is the only module in the
install carrying the particle keywords (`movealign`, `theta_speed`, `min_frames`) and the
parser's own diagnostics (`"collide spawn for %s"`, `"min_frames values for %s"`). Troika forked
the engine for this. `engine.dll` also holds the gate cvar **`particles_enable_precipitation`**.

Valve's own text-driven particle system (`.pcf` + Particle Editor) arrives in Source 2007,
three years later, and is a different design.

## The authored inventory

### Precipitation volumes and emitters

Two classnames, both keyed by `particle_definition` naming a file under `particles/`.
`func_particle` is a brush volume; `env_particle` is a point emitter.

| Map | Class | Definition | Count |
|---|---|---|---|
| `sm_pier_1` | `func_particle` | `Rain_Box_emitter` | 10 |
| `la_hub_1` | `func_particle` | `particles/Rain_box_NoPrecip_emitter.txt` | 10 |
| `sm_hub_1` | `func_particle` | `particles/Rain_box_NoPrecip_emitter.txt` | 4 |
| `sm_hub_1` | `env_particle` | `rain_follow_emitter` | 2 |
| `sm_junkyard_1` | `env_particle` | `rain_follow_emitter` | 1 |
| `sm_oceanhouse_1` | `env_particle` | `particles/rain_follow_emitter.txt` | 1 |

The key accepts both a bare definition name and a full `particles/<name>.txt` path; the two
forms appear on the same classname in the same map set.

`WaterDrops_Timer` is the drip layer under overhangs, placed by hand and dense: 36 in
`sm_hub_1`, 5 in `sp_tutorial_1`, 3 in `la_hub_1`.

**The two classes' keyvalue/I/O surface is public** — the community `vampire.fgd`
(Antitribu's mirror of Troika's own Bloodlines SDK) declares both, `func_particle` inheriting
`env_particle` with no additions beyond the solid-brush volume itself:

| Key/input | Type | Default | Role |
|---|---|---|---|
| `active` | choice | `1` (Yes) | spawns active — the `start_hidden`-equivalent; there is no separate hidden flag |
| `attach_type` | integer | `0` | the 19-value enum, dumped (`docs/vtmb/effects.md` §3.1); `11` = `PlayerSky`, `10` = `PlayerBox`, `15` = `BrushEmitter` (forced by `func_particle`) |
| `bone` | choice | `<none>` | attach point on a parent skeletal model (`Bip01 Head`/`L Hand`/`R Hand`/`Neck`/`Pelvis`/`Spine`…) |
| `particle_definition` | choice | `fire1_emitter` | the `particles/<name>.txt` file, matching the entity survey above |
| `bounds` | integer | `512` | **an FGD key the engine never reads** — `CEnvParticle`'s datamap has no such keyfield; the real one is `spawnbounds` (`m_fSpawnBounds`, default 512), authored on 228 rows corpus-wide, all 512 |
| `ramp_scale` / `ramp_time` | float | `1` / `0` | `m_fRateScaleTarget` / `m_fRampTime`, read by `SetRateScale` / `SetRampTime` |
| `spawnflags` bit 1 | flag | off | **no spawnflag is read** by the class |
| `TurnOn` / `TurnOff` | input | — | the standard on/off pair; `TurnOn` restarts |
| `SetAttachType` / `SetRateScale` / `SetRampTime` | input | — | live retune, approached linearly into the one rate float |

This answers "what we do not know" item 7 below: scripts **can** turn a volume on and off
per-instance (`TurnOn`/`TurnOff`), and the spawn state is the `active` keyvalue, not a
spawnflag. Source: `vampire.fgd` (`@PointClass env_particle`, `@SolidClass func_particle`),
fetched from the Antitribu Bloodlines-SDK mirror on GitHub — a public, community-authored
file — corrected against the decompiled datamap (`docs/vtmb/entity_io.md` → "`env_particle`
attachment"), which is why `bounds` above is dead and `spawnbounds` is not in the FGD at all.

The `_follow_` / `_box_` split is the system's shape. A follow emitter tracks the viewer and
supplies rain wherever they are; the boxes are fixed volumes. Both appear in `sm_hub_1`
simultaneously.

### Global wetness

Every map sets three keys on `worldspawn`. All 22 exported maps ship `5.0 / 10.0 / 0.0` except
`sm_hub_1`, which ships `10.0 / 20.0 / 0.0`.

| Key | Value | Role |
|---|---|---|
| `wetness_fadein` | `5.0` (`sm_hub_1`: `10.0`) | fade-in rate *(inferred: seconds)* |
| `wetness_fadeout` | `10.0` (`sm_hub_1`: `20.0`) | fade-out rate *(inferred: seconds)* |
| `wetness_fadetarget` | `0.0` everywhere | the map's authored starting target |

The authored target is dry on every map, so wetness is driven at runtime — the level scripts
fire the **`FadeGlobalWetness`** input (float target) and the map's own rates carry the
interpolation.

The material side is explicit. A wettable VMT contains three `GlobalWetness` proxy blocks whose
`resultVar`s are `$envmaptint[0]`, `[1]`, and `[2]`; each block has a `scale`. This makes the
faithful effect a per-channel reflection-tint response, not generic albedo darkening. The 14
wettable materials used by `sm_hub_1` have equal RGB triples and therefore collapse without loss
to one scalar each: asphalt `0.56`, six street materials `0.60`, and seven curb, sidewalk,
grass, stone, and tile materials `1.00`. A missing channel, duplicate channel, malformed scale,
unequal RGB triple, or proxy without `$envmap` is an export error.

Those 14 values are the **Unofficial Patch authoring**, which is the project's default source:
the pipeline resolves the installed game patch-first, just as that installation runs. The packed
base-game versions of the same 14 VMT names carry `GlobalWetness` on only five materials, all at
scale `1.0`; the patch changes asphalt to `0.56` and adds or retunes the street, curb, stone, and
tile coverage above. This is provenance, not an A/B mode: the rebuild consumes the patch-first
14-material contract and records the vanilla comparison so it is not mislabeled as original-retail
material authoring. The BSP-side `10.0 / 20.0 / 0.0` keys and delayed wet/dry I/O are identical
between base and patched `sm_hub_1`.

### `sm_hub_1`'s authored cycle

`sm_hub_1` starts dry. `rain_on_timer` starts enabled and fires after a random **180–300 s**;
`rain_off_timer` starts disabled and, once enabled by the on event, fires after **180–500 s**.
Each timer disables itself after one second and enables the other, so the dry/rain cycle repeats.

The on event starts `rain_sounds`, fans `SetRateScale 1` out to both `rain_emitter` entities,
enables the authored rain spots, and queues `world.FadeGlobalWetness 1` after ten seconds. The
off event mirrors that graph with `StopSound`, `SetRateScale 0`, disabled rain spots, and a
ten-second-delayed `FadeGlobalWetness 0`. The world then interpolates for its own authored 10 s
wet or 20 s dry duration. Both emitters are born `active=1`, `ramp_scale=0`, `ramp_time=10`,
share targetname `rain_emitter`, use `attach_type=11` (`PlayerSky`: the `spawnbounds` cube
round the viewer, wrapping, spawning above the viewer), and author `bounds` 512 and 256 —
a key the engine never reads, so **both run at the `spawnbounds` default of 512 units
(1,300.48 cm)**.

### Lightning

A consistent hand-built rig on five outdoor maps: `lightning.vmt` and `lightningglow.vmt`
`env_sprite`s parented to a `func_rotating` named `lightningrotator`, blinked by `logic_timer`s.

| Map | `env_sprite` | `func_rotating` | `logic_timer` |
|---|---|---|---|
| `sm_hub_1` | 6 | 1 | 3 |
| `sm_junkyard_1` | 12 | 2 | 6 |
| `sm_oceanhouse_1` | 6 | 1 | 3 |
| `sm_pawnshop_1` | 6 | 1 | 3 |
| `sm_pier_1` | 6 | 1 | 3 |

There is no lightning *entity*. The flash is sprites on a timer, and the rhythm is authored
per map in the timers' own keys.

### Audio

The cycling `sm_hub_1` sound is `area/Santa_Monica/rain_light_loop.wav` (`smpl` loop chunk),
everywhere + start-silent (`spawnflags 17`), attached to `!player`. Authored `fadein=10` /
`fadeout=10` are `CAmbientGeneric` KeyValue **LFO envelope** fields (`vfunc110` `0x101ada80`,
`atof` → `<< 8` into `m_dpv`), **not** I/O seconds — there is no `InputFadeIn` on the class
(`docs/vtmb/audio_pipeline.md` §7). The rain timers still `PlaySound` / `StopSound` the
entity. Separate sewer ambience uses `Environmental/Weather/rainsewers.wav` four times in
this map (`plus_drop_sound`) and is outside the outdoor-rain slice.

## The particle-definition format

The current patch-first mirror holds **1,698 `.txt`** definitions and **318 `.tga`** sprites.
The base install contributes 1,594 definitions and 309 sprites. The format is
engine-wide — the same files drive fire, muzzle flashes, discipline effects and the main-menu
background — so this section describes a general VtMB system that weather happens to be the
largest coherent consumer of.

### Envelope

One brace block, Valve-KeyValues-shaped, always rooted at the literal token `Particle`.
Quoted values throughout. `//` comments a line.

Files come in two roles, distinguished by content rather than by any declared type: an
**emitter** carries `spawn` sub-blocks naming other definitions, and a **particle** carries a
`sprite` and its motion.

### Emitter — patch-first `particles/rain_follow_emitter.txt`

```
Particle
{
	loop "1"
	precipitation "1"

	spawn
	{
		particle "raindrops2"
		rate "1000"
		radius "0"
		theta "0"
	}

	spawn
	{
		particle "rainfog"
		rate "70"
		radius "800"
		theta "0~360"
		phi "0"
	}

}
```

The `spawn` block's `particle` value is a definition name, resolved without the `particles/`
prefix or the `.txt` extension and case-insensitively. The base-game definition has the older
drop layers and commented `RainSheets`/`RainMist2` blocks; the patch-first definition above is
the one selected by the current merged install and consumed by the rebuild.

### Particle — `particles/raindrops2.txt`

```
Particle
{
	sprite		"DropletFast"
	frames		"15"
	movealign	"1"
	X_speed		"20"
	Y_speed		"20"
	Z_speed		"-400~-600"
	size		"3"
	height		"10"
	color		"0,80(10)"
	mask		"0"
	precipitation	"1"

	collide
	{
		spawn
		{
		particle 	rainsplash_new
		friction 	"0"
		Bounce		"0"
		}

		decal
		{
		particle 	"RainStain"
		}
	}
}
```

### Value grammar

Three forms, freely nested:

| Form | Example | Meaning |
|---|---|---|
| scalar | `size "3"` | constant |
| `a~b` | `theta "0~360"` | uniform random in range, rolled per particle (inside a keyframe too) |
| `a,b,…` | `size "1,10"`, `red "0,100,100"` | keyframe ramp over the particle's **normalized age**, linear between keyframes, held after the last; angle keys interpolate the shortest way round |
| `v(n)` | `color "0,80(10)"` | a keyframe pinned at **frame index `n` normalized by `frames`** (`t = n / frames`; negative `n` wraps from the end); the first keyframe must sit at 0 |

The forms compose: `rate "15,5~50,20,5~50,15"` is a five-keyframe ramp whose second and fourth
keyframes are random ranges, and `rotate "-180~180,-180~180"` ramps between two independent
random rolls. All VERIFIED from `CParticleManager` (`docs/vtmb/effects.md` §2.4).

### Keys observed

Emitter-level:

| Key | Example | Role |
|---|---|---|
| `loop` | `"1"` | wrap the normalized age on expiry |
| `fps` | `"20"` | the frame rate `frames` counts in; default **30** |
| `frames` / `min_frames` / `max_frames` | `"70"` / `"20"` / `"80"` | `lifetime = frames / fps` seconds; each particle rolls its inverse age rate in `[fps / max_frames, fps / min_frames]` |
| `precipitation` | `"1"` | gated by `particles_enable_precipitation`, and at runtime by the leaf sky bit (below) |
| `spawn { }` | — | child emission, repeatable |

Inside `spawn` (the 20-key table is `docs/vtmb/effects.md` §2.4):

| Key | Example | Role |
|---|---|---|
| `particle` | `"raindrops2"` | child definition name |
| `rate` | `"1000"` | particles per second through an accumulator, sub-frame interpolated along the emitter's movement; keyframed over the emitter's age |
| `burst` | `"1"` | particles added when a keyframe fires (`RandomFloat(lo, hi)`); the one key allowed a first keyframe at `t ≠ 0` |
| `radius` | `"1~200"` | spawn offset from the emitter origin, units |
| `theta` / `phi` | `"0~360"` / `"0"` | spherical spawn direction, degrees |
| `timescale` | `"2"` | divides the child's lifetime |

Particle-level:

| Key | Example | Role |
|---|---|---|
| `sprite` | `"DropletFast"` | resolves to `particles/<name>.tga`, case-insensitive; one atlas of all 318 |
| `frames` | `"15"` | lifetime in frames at `fps` |
| `movealign` | `"1"` | orient the sprite to its velocity — what makes rain a streak |
| `X_speed` / `Y_speed` / `Z_speed` | `"20"` / `"20"` / `"-400~-600"` | velocity in the **emitter's basis** (X forward, Y up, Z right), units/s |
| `theta_speed` / `phi_speed` / `radius_speed` | `"0"` / `"0"` / `"100,0"` | the spherical offset round the emitter origin, rebuilt each frame |
| `rotation` | `"0"` | sprite roll, degrees (shortest arc); `rotate` on a particle body is not a runtime key |
| `size` | `"1,10"`, `"2~4"` | the quad edge in units for a square sprite, rampable |
| `height` / `width` | `"10"` | per-axis multipliers: `halfX = aspectX × width × size`, `halfY = aspectY × height × size`, the aspect being the sprite's normalized half-extents (long axis 0.5) |
| `red` / `green` / `blue` | `"0,100,100"` | per-channel ramp, 255 = unity |
| `color` | `"100,20"` | combined brightness ramp |
| `mask` | `"180,0"` | the **blend interpolant**: 0 pure additive, 255 an occluding alpha-blended card (mode 8, `dst = tex × colour + dst × (1 − tex.a × mask)`) |
| `parent_speed` | `"0"` | the fraction of the parent's movement live particles inherit; default 1 |
| `precipitation` | `"1"` / `"0"` | the gate flag |
| `collide { }` | — | collision response |

Inside `collide`: a `spawn` block emitting a definition at the impact point, a `decal` block
laying one on the surface, `self` with `bounce` / `friction` / `gravity` / `drag` keeping the
particle alive (`v' = bounce × normal + friction × tangent`, then gravity and `pow(drag, dt)`),
and `vdecal_first` / `vdecal_last` / `vdecal_angle_spread`. The trace runs against the world
**and** brush entities (`SOLID | WINDOW | GRATE | MOVEABLE`). `raindrops2` writes its
`friction` / `Bounce` inside the nested `spawn` block, where they are not spawn keys.

## The weather asset set

33 rain-related definitions ship. The ones the maps reach, directly or transitively:

| File | Role |
|---|---|
| `rain_follow_emitter` | viewer-tracking emitter — the current patch closure is drops2 ×1,000/s + rainfog ×70/s |
| `rain_box_emitter` | volume emitter — drops (ramped 15→50→20→50→15) + drops2 + mist |
| `rain_box_noprecip_emitter` | the sheltered variant — `RainDrops_NoPrecip` ×250/s, nothing else |
| `rain_emitter` | volume emitter at heavier rates, `radius "1~200"` |
| `raindrops` | the fall — `DropletFast`, Z −100, motion-aligned |
| `raindrops2` | the heavy fall — Z −300, and the only layer that **collides** |
| `raindrops_noprecip` | `raindrops` with `precipitation "0"` |
| `rainsplash` | impact — `point_16`, radius ramp 100→0 |
| `rainsplashdummy` | what `raindrops2` spawns on collide |
| `rainring` | impact ring — `Ringlet`, size 1→10 with a brightness fade |
| `rainstain` | the wet decal `raindrops2` lays on impact — `Water_Droplet` |
| `rainmist` / `rainmist2` | volumetric haze — `size "30"`, slow upward drift |
| `rainsheets` | large translucent sheets — `FurBall`, `size "300~500"` |
| `waterdrops_timer` | the drip emitter — bursts `WaterDrops_Emitter` every 20–80 frames |

## Findings worth flagging

- **The base definition's extra layers shipped disabled.** Its `RainSheets` and `RainMist2`
  spawn blocks are commented out. The patch-first override replaces the live closure with
  `raindrops2` and `rainfog`; the rebuild does not re-enable either commented block literally.
- **`rainmist` names a sprite that does not exist.** It declares `sprite "could"`; the install
  has `particles/cloud.tga` and no `could.tga`. `rain_box_emitter` spawns `RainMist` live, not
  commented. Whether the engine substitutes a default or the mist silently fails to draw is
  unresolved — it needs a runtime check against the original.
- **The `_NoPrecip` pairs are half of the shelter mechanism.** Definitions come in matched
  pairs differing only in the `precipitation` flag, so a sheltered volume keeps rendering when
  `particles_enable_precipitation` is off — an authored quality setting. The other half is
  geometry: **a `precipitation "1"` particle dies the moment its BSP leaf lacks the
  sky-visible bit** (`engine.dll 0x200d3f10`), so rain stops under cover regardless of the
  pairs. Reproducing that gate needs the visibility unit's leaf sky bit — this plan's named
  follow-up (`effects-architecture.md` §5.11).
- **Only `raindrops2` collides in the resolved follow closure.** Its 1,000 drops/s spawn
  `rainsplash_new` and lay `rainstain`; `rainfog` has no collision relation.

For `sm_hub_1`, the resolved Unreal-space contract is **1.5 × 76 cm** motion-aligned streaks
(`DropletFast` 5 × 25, `size 3`, `height 10` through the aspect formula — not the 7.62 × 25.4
the size key alone suggested) at `(50.8, 50.8 up, -1016…-1524)` cm/s in the emitter basis,
63.5 cm maximum authored impact rings, 5.08–10.16 cm stains, and 1,270 cm fog sprites. On
the V2 lane every live field in this closure is a staged tree field
(`seam_map_map.md` → "Import — effects (R7.3)"); the legacy compiler still fails an
unsupported field rather than ignoring it.

## What we do not know

Tracked as **RE23**.

1. **Exact retail interpolation and units.** The VMT proxies prove that global wetness scales
   `$envmaptint` per channel, and `sm_hub_1`'s timers prove who targets it and when. Retail capture
   must still confirm that `10.0`/`20.0` and the emitter/audio ramps are seconds with linear
   interpolation.
2. ~~`attach_type=11`~~ **Closed (decompiled):** `PlayerSky` — the `spawnbounds` cube round
   the viewer, wrapping, spawning above the viewer. Live particles follow a moving parent by
   `parent_speed` (default 1); it is not only new spawns that move.
3. ~~`bounds`~~ **Closed:** not a keyfield; the engine reads `spawnbounds` (default 512), so
   both `sm_hub_1` emitters run the same 1,300.48 cm cube. It is the spawn extent, not a rate.
4. ~~`frames` / `fps`~~ **Closed:** `lifetime = frames / fps` seconds, `fps` default 30,
   normalized age in `[0, 1]`.
5. ~~`v(n)`~~ **Closed:** a frame index normalized by `frames`, negative from the end, linear
   in normalized age.
6. ~~Emitter volume sampling~~ **Closed:** `func_particle` (`attach_type` 15) spawns at a
   uniform random point in the brush's world-aligned AABB with no solid test, and scales
   `rate` and `burst` by `|dx|·|dy|·|dz| × 2⁻²¹` clamped to `[0.01, 100]`; `radius` is the
   per-particle spherical offset round that point, as for a point emitter.
7. ~~`func_particle` sampling~~ **Closed** with 6; the rain boxes' *look* is still this plan's.
8. ~~Sprite render mode~~ **Closed:** one material, `$spriterendermode 8`, `mask` is the
   blend interpolant between additive (0) and an occluding alpha card (255)
   (`docs/vtmb/effects.md` §2.4).

The community record helps only partway: `vampire.fgd` defines both `env_particle` and
`func_particle`, but annotates the wetness and particle-specific keys as untested or unknown.
`lightningrotator` is an instance name, not a class, and appears in none of the public FGD
files. No public decoder for `particles/*.txt` exists.

## Unreal translation boundary

General UE weather practice is an implementation-options catalogue, not evidence about VtMB.
Camera-local GPU rain, distant rain sheets, dynamic wind, puddle accumulation, cloud and fog
transitions, exposure traces, Niagara Data Channels, Niagara Fluids, and ray-traced collision are
all valid techniques in other games; none follows from this map's particle, entity, material, or
audio data. Public examples from other games likewise do not establish VtMB behavior.

The translation keeps three categories separate:

| Category | `sm_hub_1` contract |
|---|---|
| Reproduced Logic | Timer schedule, targetname fanout, emitter rate ramps, audio I/O, wetness target and transition state, authored particle rates, and the collision child relationship |
| Native technical bridge | One Unreal presentation asset may render the resolved live closure; static cover data may replace the original renderer's geometry query without adding rain content |
| Presentation not implied by source | Extra near/mid/far layers, wind, puddles, storm lighting or atmosphere, indoor-zone logic, fluids, and re-enabled disabled definitions |

The resolved live follow closure has two root layers, not a generic three-layer production-rain
recipe: `raindrops2` at 1,000/s and `rainfog` at 70/s. `rainsplash_new` and `rainstain` are children
of an actual `raindrops2` collision; they are not free-running ambient impact layers. `rainfog` is
live patch content, not an optional invented mist enhancement. A future Unreal presentation must
preserve those relationships regardless of whether Niagara represents them as emitters, renderer
states, or particle attributes.

The owner constraint remains one presentation implementation with tuning, never separate faithful
and enhanced systems. The distribution rule for the `radius=0` drop block is now decoded:
`attach_type=11` is a `spawnbounds` **cube** round the viewer (512 units on both emitters — the
authored `bounds` 512 / 256 are a dead key), wrapping, with spawns above the viewer; and
`precipitation=1` kills a drop whose leaf has no sky bit. The drop count is `rate × the one
rate float`, never the box volume (that scalar is `func_particle`'s, mode 15).

### Wetness presentation slice

The material slice and the future particle presentation share one weather authority. The entity
world owns the authored current/target transition and continues to process and serialize timers
while a debug override is visible. `env_particle` state crosses the same `IElysiumWeather` seam as
values, but this slice creates no Niagara components.

One shared world-material graph computes:

```
wet             = saturate(GlobalWetness · authoredScale · WetnessOutputScale)
sourceWeight     = lerp(1, RainSourceRetain, RainEnhancement)
sourceReflection = SourceCube((UE.X, -UE.Y, UE.Z)) · EnvMaskLinear · wet · sourceWeight
enhancedCoverage = RainEnhancement · wet · EnvMaskCoarse · Exposure · Upward
```

`GlobalWetness` is the presented `0..1` state. `WetnessOutputScale` defaults to `1.0` and is an
owner tuning multiplier; the patch-authored per-material values remain `0.56`, `0.60`, and `1.00`.
Materials without a complete `GlobalWetness` proxy remain unchanged. The 14 `sm_hub_1` proxy
materials bind the one exported 32x32 LDR `cubemapdefault`; the cube is colour, while the
`$envmapmask` is linear `TC_Masks` data read through UE's `Masks` sampler. The bake derives
`EnvMaskCoarseMip` from the longest image axis: the present 512-axis masks select mip 6 and the
1024-axis masks select mip 7, producing the intended roughly 8-texel coarse footprint. Inactive
`BaseTex2`, bump, and emissive slots in the shared master are not missing wetness layers: none of
these 14 VMTs authors those channels.

Source-reference presentation is `RainEnhancement=0`. The primary-view graph adds the authored
cube/mask/wetness term through Emissive while keeping the stable albedo in Base Color. This maps the
visible additive response into UE's dynamic-light scene; it does not claim VtMB marks `$envmap` as
emissive. `Ray Tracing Quality Switch` replaces that view-dependent term with black for Lumen card
capture and ray-hit evaluation. At this endpoint wetness does not change PBR roughness or specular.
This is the wet-material specialization of the general `$envmap` translation in
`docs/vtmb/reflections.md`.

Raising `RainEnhancement` does not select another material or weather system. It enables a stable,
low-frequency PBR response in the same graph: a coarse mip of the mask gates the wet region,
the rain-height texture suppresses that response under cover, the world normal limits it to upward-
facing surfaces, `RainWetRoughness` reduces roughness, and `RainWetSpecular` sets the dielectric
reflection target.
At the same time `RainSourceRetain` reduces the source cube at the enhanced endpoint so the two
terms do not remain at full strength together. The raw, high-frequency mask continues to control
the source cube only; using it directly as roughness is the rejected glitter/glare interpretation.
Enhanced base-colour darkening remains a restrained optional term on upward-facing exposed
surfaces. It does not alter the authored proxy scale.

`cubemapdefault` is never assigned to the map Sky Light and is not converted into point, spot, or
rect lights. The Sky Light continues to use the map's separate `sky_` cube and authored type-5
sky-ambient magnitude; `sm_hub_1` has no such row, so its source-reference magnitude is zero.
Promoting the material cube to a Sky Light would globally relight dry and non-proxy surfaces and
duplicate radiance already represented by the reconstructed `.lights` rig.

The Cog window `Elysium.Environment` is the one tuning surface. It exposes the live authored and
presented values, transition time, patch material groups, output scale, source cube identity,
source retain, enhanced roughness/specular/darkening, reflection debug views, and authored timer
buttons. Its manual wetness override replaces presentation only: the entity state and scheduled
I/O keep running, and selecting **Follow authored** reveals the current authored value without
restarting the cycle. Debug views isolate the linear mask, wet factor, cube sample, completed source
term, and enhanced coverage without creating alternate materials.

The panel also exposes the light rig's existing `SpecularScale` as **Local-light specular**, shared
live with `Elysium.Lights` rather than stored as a second environment value. Zero is the source-light
baseline. A non-zero value affects every non-overridden light and is an independent presentation
diagnostic, never an automatic consequence of wetness.

| Console variable | Default | Live role |
|---|---:|---|
| `elysium.EnvironmentWetnessOverride` | `0` | Select manual or authored presentation |
| `elysium.EnvironmentWetness` | `1.0` | Manual presented wetness |
| `elysium.EnvironmentWetnessScale` | `1.0` | Output multiplier after the authored material scale |
| `elysium.RainEnhancement` | `0.0` | Blend source-reference and enhanced response in the same graph |
| `elysium.RainSourceRetain` | `1.0` initially | Source-cube fraction at full enhancement; tuned before enhanced acceptance |
| `elysium.RainWetDarken` | `0.06` | Maximum full-wet enhanced base-colour darkening |
| `elysium.RainWetRoughness` | `0.10` | Maximum full-wet enhanced roughness reduction |
| `elysium.RainWetSpecular` | `0.50` | Full-wet enhanced dielectric specular target |
| `elysium.RainReflectionDebug` | `0` | Final, mask, wet factor, cube, source term, or enhanced-coverage view |
| `elysium.RainLightResponse` | `0.25` | Retained particle tuning; no effect while Niagara is disconnected |

### Static cover data

`sm_hub_1.weather.json` is the versioned seam for the patch-first particle, entity, and cover facts;
the resolved VMT scalars are carried by `globalwetness` tokens in `sm_hub_1.mtl`. Its
map footprint is **28,971.24 × 19,639.28 cm**, from Unreal-space `(-8285.48, -8585.20)` to
`(20685.76, 11054.08)`. The generated 2048² R16 maximum-height texture uses 245,567 world and
rain-blocking static-geometry triangles and has 964,071 covered texels. Sky, decals, ropes, water,
and non-cover effects do not contribute. Sentinel zero means no cover; other samples decode as
`min_z_cm + (sample - 1) × z_scale_cm`.

Using this texture to find the first rain-blocking surface is a technical translation of the
authored `raindrops2.collide` behavior, not evidence that VtMB used a height texture. It is suitable
for this static map only. It can terminate a drop and locate its authored splash and stain without
introducing Scene Depth, Distance Fields, ray tracing, an upward player trace, and hand-authored
indoor zones as competing solutions.

### Translation gates and deferred scope

The entity world can own wetness transitions and emitter ramps without deciding how they render;
those values serialize so logic does not restart after a save. A presentation is not a faithful
baseline until original-retail evidence settles initial dry, onset, sustained rain, cover, rain-off,
time units, audio fades and density; `attach_type=11`, `spawnbounds`, lifetime/keyframe units and
the sprite blend/`mask` semantics are settled from the binary (above). Apparent drop width, length,
lifetime, fog visibility, impact frequency, and camera-motion behavior are comparison evidence,
not artist tuning targets until then.

Deferred source-defined work remains the sewer `WaterDrops_Timer` layer, fixed `func_particle`
rain boxes, NPC shelter behavior, lightning, other maps, and the general particle runtime. The
disabled base-game `RainSheets`/`RainMist2` blocks remain disabled. Enhanced darkening,
roughness/specular response, and source-cube retention are owner-tunable in the one graph and have
no effective contribution while `RainEnhancement=0`; local-light response and any additional mist
stay behind the particle retail gate.

When presentation work resumes, measure fresh dry/rain A/B pairs on one fixed generated asset set
at the existing `sm_hub_1` vantages. Acceptance includes a labelled cube-orientation proof, linear
mask and white-fallback validation, `RainEnhancement=0` source-cube isolation with local-light
specular zero, a Surface Cache view free of the view-dependent cube, and the tuned enhanced
crossfade. At 2560×1440 native on the measured RTX 5070 Ti, rain may add at most 1.0 ms GPU or 20%;
reduce presentation tuning before changing authored rates. An RTX 4060-class result is not claimed
without that hardware.
