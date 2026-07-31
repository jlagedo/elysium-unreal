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

The **entity inventory** and the **file inventory** are verified — read off our own exports and
the user's own VPKs. The **particle-definition grammar** is a partial reconstruction from the
shipped data plus parser diagnostic strings in `engine.dll`; the key roles marked *(inferred)*
below are not confirmed against the decompile. The **wetness semantics** are unknown — see
"What we do not know".

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
| `attach_type` | integer | `0` | undocumented even by the FGD's own author ("Unknown yet") |
| `bone` | choice | `<none>` | attach point on a parent skeletal model (`Bip01 Head`/`L Hand`/`R Hand`/`Neck`/`Pelvis`/`Spine`…), untested |
| `particle_definition` | choice | `fire1_emitter` | the `particles/<name>.txt` file, matching the entity survey above |
| `bounds` | integer | `512` | "Bounds (Intensity)", untested |
| `ramp_scale` / `ramp_time` | float | `1` / `0` | undocumented |
| `spawnflags` bit 1 | flag | off | undocumented even by the FGD's own author |
| `TurnOn` / `TurnOff` | input | — | the standard on/off pair |
| `SetAttachType` / `SetRateScale` / `SetRampTime` | input | — | live retune |

This answers "what we do not know" item 7 below: scripts **can** turn a volume on and off
per-instance (`TurnOn`/`TurnOff`), and the spawn state is the `active` keyvalue, not a
spawnflag. Source: `vampire.fgd` (`@PointClass env_particle`, `@SolidClass func_particle`),
fetched from the Antitribu Bloodlines-SDK mirror on GitHub — a public, community-authored
file, not our own decompile, so treat the *values* as corroboration rather than as
Ghidra-grade fact.

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

`ambient_generic`s under `sound/Environmental/Weather/`: `rainsewers.wav` (10 in `la_hub_1`,
4 in `sm_hub_1`) and `rain1.wav` (1 in `sm_pier_1`).

## The particle-definition format

`particles/` holds **1,594 `.txt`** definitions and **309 `.tga`** sprites. The format is
engine-wide — the same files drive fire, muzzle flashes, discipline effects and the main-menu
background — so this section describes a general VtMB system that weather happens to be the
largest coherent consumer of.

### Envelope

One brace block, Valve-KeyValues-shaped, always rooted at the literal token `Particle`.
Quoted values throughout. `//` comments a line.

Files come in two roles, distinguished by content rather than by any declared type: an
**emitter** carries `spawn` sub-blocks naming other definitions, and a **particle** carries a
`sprite` and its motion.

### Emitter — `particles/rain_follow_emitter.txt`

```
Particle
{
	loop "1"
	precipitation "1"

	spawn
	{
		particle "RainDrops"
		rate "800"
		radius "0"
		theta "0"
	}

	spawn
	{
		particle "RainDrops2"
		rate "50"
		radius "0"
		theta "0"
	}

//	spawn
	{
		particle	"RainSheets"
		rate		"100"
		radius		"800"
		theta		"0~360"
		phi		"0"
	}

//	spawn
	{
		particle "RainMist2"
		rate "100"
		radius "1"
		theta "0~360"
	}

}
```

The `spawn` block's `particle` value is a definition name, resolved without the `particles/`
prefix or the `.txt` extension and case-insensitively.

### Particle — `particles/raindrops2.txt`

```
Particle
{
	sprite		"DropletFast"
	frames		"150"
	movealign	"1"
	X_speed		"20"
	Y_speed		"20"
	Z_speed		"-300"
	size		"3"
	height		"10"
	color		"0,80(10)"
	mask		"0"
	precipitation	"1"

	collide
	{
		spawn
		{
		particle 	RainSplashDummy
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
| `a~b` | `theta "0~360"` | uniform random in range, rolled per particle |
| `a,b,…` | `size "1,10"`, `red "0,100,100"` | keyframe ramp over the particle's life |
| `v(n)` | `color "0,80(10)"` | a ramp keyframe with an explicit position *(inferred: frame index)* |

The forms compose: `rate "15,5~50,20,5~50,15"` is a five-keyframe ramp whose second and fourth
keyframes are random ranges, and `rotate "-180~180,-180~180"` ramps between two independent
random rolls.

### Keys observed

Emitter-level:

| Key | Example | Role |
|---|---|---|
| `loop` | `"1"` | restart on expiry |
| `fps` | `"20"` | tick rate *(inferred)* |
| `frames` / `min_frames` / `max_frames` | `"70"` / `"20"` / `"80"` | lifetime in frames, with an optional random range *(inferred)* |
| `precipitation` | `"1"` | gated by `particles_enable_precipitation` |
| `spawn { }` | — | child emission, repeatable |

Inside `spawn`:

| Key | Example | Role |
|---|---|---|
| `particle` | `"RainDrops"` | child definition name |
| `rate` | `"800"` | particles per second |
| `burst` | `"1"` | one-shot instead of continuous |
| `radius` | `"1~200"` | spawn offset from the emitter origin |
| `theta` / `phi` | `"0~360"` / `"0"` | spherical spawn direction, degrees |

Particle-level:

| Key | Example | Role |
|---|---|---|
| `sprite` | `"DropletFast"` | resolves to `particles/<name>.tga`, case-insensitive |
| `frames` | `"150"` | lifetime *(inferred)* |
| `movealign` | `"1"` | orient the sprite to its velocity — what makes rain a streak |
| `X_speed` / `Y_speed` / `Z_speed` | `"20"` / `"20"` / `"-300"` | initial velocity |
| `theta_speed` / `phi_speed` / `radius_speed` | `"0"` / `"0"` / `"100,0"` | angular and radial motion |
| `rotation` / `rotate` | `"0"` / `"-180~180,-180~180"` | sprite roll |
| `size` | `"1,10"`, `"2~4"` | sprite size, rampable |
| `height` | `"10"` | second axis, for stretched streaks *(inferred)* |
| `red` / `green` / `blue` | `"0,100,100"` | per-channel ramp |
| `color` | `"100,20"` | combined brightness ramp |
| `mask` | `"180,0"` | alpha/mask ramp |
| `precipitation` | `"1"` / `"0"` | the gate flag |
| `collide { }` | — | collision response |

Inside `collide`: a `spawn` block (with `friction` and `Bounce`) emitting a definition at the
impact point, and a `decal` block laying one on the surface. `engine.dll` also carries
`collide_axes`, `collide self` and `collide_wireframe`, so the collision path has more surface
than the weather files exercise.

## The weather asset set

33 rain-related definitions ship. The ones the maps reach, directly or transitively:

| File | Role |
|---|---|
| `rain_follow_emitter` | viewer-tracking emitter — drops ×800/s + drops2 ×50/s |
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

- **Half the rain shipped disabled.** In `rain_follow_emitter` the `RainSheets` and `RainMist2`
  spawn blocks are commented out. The follow path — the one that covers the player everywhere —
  runs two layers of the four that were authored.
- **`rainmist` names a sprite that does not exist.** It declares `sprite "could"`; the install
  has `particles/cloud.tga` and no `could.tga`. `rain_box_emitter` spawns `RainMist` live, not
  commented. Whether the engine substitutes a default or the mist silently fails to draw is
  unresolved — it needs a runtime check against the original.
- **The `_NoPrecip` pairs are the shelter mechanism.** Definitions come in matched pairs
  differing only in the `precipitation` flag, so a sheltered volume keeps rendering when
  `particles_enable_precipitation` is off. This is an authored quality setting, not a
  geometry-driven one.
- **Only `raindrops2` collides.** Splashes, rings and stains are all downstream of that single
  layer; `raindrops` (the denser one, ×800/s on the follow emitter) passes through the world.

## What we do not know

Tracked as **RE23**.

1. **`FadeGlobalWetness` semantics.** What the float target scales — a material parameter, a
   texture blend, a reflection term — and which surfaces it reaches. `GlobalWetness` appears in
   both `vampire.dll` and `client.dll`, so the value crosses to the render side.
2. **Who calls it.** The level scripts fire it, but the call sites are not yet surveyed against
   the exported `$ELYSIUM_EXPORT_ROOT/scripts/`, so we cannot say whether wetness tracks story beats or is set
   once per map.
3. **Whether the fade rates are seconds or frames.**
4. **`frames` / `fps` semantics** — lifetime in ticks at a declared rate is the reading the
   data supports, unconfirmed.
5. **The `v(n)` keyframe position unit** — frame index or percentage.
6. **Emitter volume sampling** — how `func_particle` distributes spawns through a brush volume,
   and what `radius` means when the emitter is a volume rather than a point. The public FGD's
   `func_particle` block adds nothing over `env_particle` (no per-axis density key), so this
   stays a decompile-only question.
7. ~~Whether `func_particle`/`env_particle` respect the standard I/O and `start_hidden`
   surface~~ — **resolved above**, off the public FGD: yes (`TurnOn`/`TurnOff`), and the
   spawn-state key is `active`, not `start_hidden`.
8. **Sprite render mode** — additive vs translucent, and whether `mask` is alpha or a separate
   mask channel.

The community record helps only partway: `vampire.fgd` (Antitribu mirror) *does* define both
`env_particle` and `func_particle` (keyvalue/I/O table above) — corrects the earlier read of
this doc, which had them absent — but still annotates all three wetness keys and every
particle-specific key `"Not tested yet..."`/`"Unknown yet..."`, and `lightningrotator` (an
instance name, not a class — see "Lightning" above) appears nowhere in any of the three public
FGD files (`vampire.fgd`, `vampire-base.fgd`, `vampire-adds.fgd`), consistent with it being a
plain `func_rotating` given that targetname rather than a distinct entity. No public decoder
for `particles/*.txt` exists.

## The Unreal plan

Five systems, not one. Per `docs/project/remaster-direction.md` the split is clean: the **appearance** is
Presentation and is built native, the **authoring** — which volumes, which shelters, which
timers, which script calls — is Logic and is reproduced from the exported entity data.

1. **Surface wetness.** A Material Parameter Collection scalar every world material reads:
   roughness down, base colour slightly darkened, normals flattened. Driven by
   `FElysiumWorldEvents::GlobalWetness` (the input already lands there) interpolated at the
   map's own `wetness_fadein`/`fadeout` rates. Masked by up-facing normal and by occlusion.
   With HWRT Lumen and the `$envmap` channel already built, this is the largest look delta for
   the least work, and it needs no particle RE.
2. **Occlusion.** Rain falling through roofs is the tell. The maps are static and small and the
   world geometry is already on disk, so the top-down max-Z height map is **baked offline** from
   `$ELYSIUM_EXPORT_ROOT/<map>/<map>.obj` rather than captured at runtime — `worldspawn`'s `world_mins`/`world_maxs`
   give the footprint (`sm_hub_1` is 11406 × 7732 cm, so 1024² is ~11 cm/texel). Sampled by
   world XY→UV from both the particles and the wetness material. No SceneCapture, no Global
   Distance Field dependency.
3. **Falling rain.** One camera-anchored GPU Niagara system with bounds wrapping — which is what
   `rain_follow_emitter` already is. `movealign` → velocity-aligned sprites; `frames` → SubUV;
   `a,b` ramps → curves over age; `a~b` → uniform ranges. The authored `func_particle` volumes
   stay as the region definition; the height map handles within-region shelter, which 10
   hand-placed boxes can only approximate.
4. **Impacts and drips.** `rainsplash`/`rainring`/`rainstain` become a ground-impact emitter
   masked by the same height map. The `WaterDrops_Timer` placements stay as authored point
   emitters — they are deliberate content, not simulation.
5. **Atmosphere and lightning.** The disabled `rainsheets`/`rainmist` layers map to volumetric
   fog density rather than more sprites; the maps already carry `fogenable`/`fogstart`/`fogend`.
   Lightning keeps its authored timer rhythm and replaces the sprite flash with a real light
   pulse, so Lumen bounces it down the street — an effect the original renderer could not
   produce. Re-enabling the disabled layers' *intent* is a divergence and needs an owner call,
   recorded here beside the faithful behaviour.

**Budget.** The risk is translucent overdraw, not particle count — rain is thin, numerous and
screen-filling. Two engine facts to verify before committing rain to a lit translucent
material: how MegaLights treats translucency, and whether the VSM pass stays neutral.
`uv run elysium debug profile` and `uv run elysium debug shots` already cover the outdoor vantages, so this is measurable.
