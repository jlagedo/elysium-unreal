# Rain, drips, splashes, stains and lightning — Niagara reference examples

Scope: real, opened-and-quoted examples of Unreal Niagara work that reproduces the VtMB weather
family described in `docs/vtmb/weather.md` and `docs/vtmb/effects.md` §4.4 — the ceiling drip
(`waterdrops_timer` bursting `WaterDrops_Emitter` every 20–80 frames), the viewer-tracking
`rain_follow_emitter` and the brush-volume `rain_box_emitter` / `rain_box_noprecip_emitter`, the
`raindrops2` fall that is the only layer carrying a `collide {}` block, its two collide records
(`spawn { particle rainsplash_new }` and `decal { particle "RainStain" }`), the `rainsplash` /
`rainring` impact bursts with one-shot ramps over normalized age, and the `env_sprite` +
`logic_timer` lightning rig. Each section names the Niagara mechanism the example actually uses
(Collision module CPU vs GPU, Generate/Receive Collision Event, Particle Attribute Reader /
Spawn Particles from Other Emitter, Niagara Data Channels, Decal Renderer vs Decal Component
Renderer, Light Renderer) and whether the child spawn is per-collision-instance or
per-parent-particle, because that is the axis on which the VtMB `collide { spawn {} }` contract
either reproduces faithfully or does not. Every URL here was opened with kagi_extract or
WebFetch; where a page is JS-rendered and returns only a title to a direct fetch, that is called
out and the quoted body text is Kagi's index of the page, not a paraphrase.

### Events and Event Handlers in Niagara Effects for Unreal Engine

- **URL:** https://dev.epicgames.com/documentation/en-us/unreal-engine/events-and-event-handlers-in-niagara-effects-for-unreal-engine
- **Author / credibility:** Epic Games, official UE 5.x documentation. Primary source.
- **What it is:** Reference documentation for the event system.
- **Technique:** The canonical child-spawn mechanism. `Generate Collision Event` goes in the
  Particle Update group of the *parent*; a second emitter adds an `Event Handler Properties`
  stage plus a matching `Receive Collision Event` module. The docs state the hard constraints we
  have to design around: "Currently, events with GPU simulations will not work. Events only work
  with CPU simulation."; "In order to use events, make sure to enable Requires Persistent IDs in
  the Emitter Properties of your emitters."; "You need to add a **Collision** module to an
  emitter before you can add a **Generate Collision Event** to that emitter." Event Handler
  Properties is where "you can choose what particles are affected by the event, how many times
  the event occurs per frame, and if the event spawns particles you can select how many are
  spawned."
- **Per-instance:** Yes — one event per collision, and the handler spawns N particles per event.
  This is the exact shape of VtMB's `collide { spawn { particle rainsplash_new } }`.
- **Closeness to VtMB:** Structurally exact for the collide-spawn record. Diverges on the
  `self { bounce / friction }` half of the collide block, which Niagara's Collision module
  handles separately as restitution/friction on the module itself.
- **Reuse:** The default mechanism for `raindrops2` → `rainsplash_new`, and for
  `waterdrops_timer`'s drip → `RainSplash` burst. Forces the fall layer onto CPU sim.

### Content Examples sample project — Niagara map, "2.6 Collision" and "2.4 Location Events"

- **URL:** https://dev.epicgames.com/documentation/unreal-engine/niagara-content-examples?application_version=4.27
- **Author / credibility:** Epic Games, official documentation for the Content Examples sample
  project (free on Fab). Primary source.
- **What it is:** Sample project map, one numbered stand per technique.
- **Technique:** 2.6 Collision — "This example shows how to handle collision events being sent to
  a different emitter which spawns particles in response to that collision event. Collision
  queries are done with either line traces on the CPU or depth buffer/distance field checks on
  the GPU. The query results are saved and can also be used by subsequent modules." 2.4 Location
  Events — "a lead particle emitter that sends its location as an event to the other two emitters
  in this particle system, allowing them to spawn that location. The event is sent at the very
  end of the frame after the particle's position and velocity are solved."
- **Per-instance:** Yes for 2.6 — one collision, one event, a burst in another emitter.
- **Closeness to VtMB:** 2.6 is the minimal reproduction of one `collide { spawn {} }` record.
  The CPU line trace is also the closest analogue of VtMB's per-frame movement trace against
  `SOLID | WINDOW | GRATE | MOVEABLE` (world and brush entities).
- **Reuse:** Open the stand as the reference stack before authoring the splash emitter; copy the
  module ordering (Collision, then Generate Collision Event, both after Solve Forces and Velocity).

### Niagara Examples Pack (Epic, free on Fab, UE 5.7)

- **URL:** https://www.fab.com/listings/0e188eca-4e54-4fb2-a9ed-d8b8a565e600 — announcement
  corroborated at https://80.lv/articles/epic-releases-over-50-free-niagara-systems-for-unreal-engine-5-7
  (Fab and unrealengine.com/news both return HTTP 403 to a direct fetch; the 80.lv article was
  extracted in full, and the Fab description quoted below is Kagi's index of the listing page.)
- **Author / credibility:** Epic Games. Free, first-party. Highest-credibility pack available.
- **What it is:** Content pack — "more than 50 VFX Niagara systems" plus "a dedicated gallery
  gameplay level that you can open and play through" (80.lv). Fab description: "They are authored
  with best practices in mind, including scalability, Effect Types, Niagara Data Channels, and
  Lightweight emitters, plus limiting user parameters and emitter counts. The pack includes
  systems for explosions, bullet impacts, and trails; sparks, fire, and smoke; mist cards; player
  and weapon buff/debuff; animation-notify-based footstep effects; pings and markers; lightning;
  hit dissolves; and more."
- **Technique:** Impact and footstep systems "make use of Niagara Data Channels ... and Animation
  Notify Blueprints" (80.lv). The pack's own tutorial page states there are "two methods of impact
  spawning demonstrated within the pack: Spawning a system per impact and using Niagara Data
  Channels to burst impacts within a single system."
  (https://dev.epicgames.com/community/learning/tutorials/YGJm/unreal-engine-niagara-examples-pack-impacts
  — JS-rendered; body text via Kagi index.)
- **Per-instance:** Both modes ship side by side, which is exactly the decision we face for the
  corpus's 26 collide roots / 209 collide placements.
- **Closeness to VtMB:** No rain system, but the impact and lightning systems are the closest
  first-party reference for a burst-at-a-contact-point library, and the Effect Type + scalability
  authoring is what a 1,000 drops/s budget needs.
- **Reuse:** Import and read the impact systems and the lightning system; adopt the Effect Type
  and Lightweight-emitter conventions for the rain system and its splash/stain children.

### Niagara Data Channels Overview

- **URL:** https://dev.epicgames.com/documentation/unreal-engine/niagara-data-channels-overview
- **Author / credibility:** Epic Games, official documentation. Primary source.
- **What it is:** Reference documentation for NDC.
- **Technique:** "One common use case for Data Channels is Niagara impact effects, where the
  player may spawn the same Niagara System multiple times during gameplay. Each system spawns and
  executes individually. This can become expensive if the player spawns many such systems rapidly.
  Niagara Data Channels provides an alternative where you can optimize burst Niagara Systems by
  combining them into a large shared simulation. So instead of spawning multiple separate Niagara
  Systems, you spawn a single system that handles all burst particles assigned to the data
  channel." Requires a Data Channel Asset (payload variables, including "enumerators such as
  collision channel, physical surface, or Niagara Execution State" and "variables that represent
  specific Chaos destruction and Niagara collision events"), a listener system with "Infinite Loop
  Behavior" and a "Complete if Unused" module, two Scratchpad modules (one reading the channel,
  one spawning from what it read), and a Blueprint that writes the payload.
- **Per-instance:** No — deliberately not per-instance; it collapses many one-shot systems into
  one shared simulation keyed by island bounds.
- **Closeness to VtMB:** Not the shape of `collide { spawn {} }`, which is intra-system. It is the
  right shape for the *other* VtMB spawn producer — the `particleimpacttable.txt` matrix and the
  runtime decal seam (§4.14), where many small bursts are triggered from game code.
- **Reuse:** Candidate for the R7.2 decal seam and impact-table bursts, not for the rain
  fall→splash relation.

### Render Module Reference for Niagara Effects — Decal Renderer

- **URL:** https://dev.epicgames.com/documentation/en-us/unreal-engine/render-module-reference-for-niagara-effects-in-unreal-engine
- **Author / credibility:** Epic Games, official documentation. Primary source.
- **What it is:** Renderer reference; the Decal Renderer is the native 5.x answer to a particle
  that lays a decal.
- **Technique:** "Use the **Decal Renderer** to spawn and project decals onto surfaces. The Decal
  Renderer uses the same technology as the Decal Actor." Source Mode: "When set to Particles,
  decals will be rendered for each particle in the simulation. When set to Emitter, only one decal
  will be rendered for the emitter." Bindings that matter here: Position (`Particles.Position`),
  **Decal Orientation Binding** (`Particles.DecalOrientation`, defaulting to
  `X: -0.5, Y: 0.5, Z: 0.5, W: 0.5`), **Decal Size Binding** (`Particles.DecalSize`, default
  `50, 50, 50`, and "In this case, X is the vertical size"), **Decal Fade Binding** ("You can
  query this value using the Decal Lifetime Opacity material node"), Decal Color
  (`Particles.Color`), Decal Visible and Renderer Visibility Tag. Distinct from the **Component
  Renderer**, which the same page flags Experimental — "We do not recommend shipping projects with
  Experimental features" — and warns about: "Since each component has its own tick each frame in
  addition to the Niagara system, spawning a lot of effects with Component Renderers can impact
  performance."
- **Per-instance:** Yes with Source Mode = Particles — one decal per particle.
- **Closeness to VtMB:** The faithful home for `decal { particle "RainStain" }`. The orientation
  binding gives the collide-normal alignment VtMB gets by forcing `flat` on a collide decal; the
  fade binding gives the age ramp.
- **Reuse:** `RainStain` and the `vdecal_first` / `vdecal_last` range become a Decal Renderer
  emitter driven by the same collision event as the splash — no DecalComponent spawning, no
  experimental path.

### How to Create Particle Effects That Emit Light in Niagara

- **URL:** https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-create-particle-effects-that-emit-light-in-niagara-for-unreal-engine
- **Author / credibility:** Epic Games, official step-by-step tutorial. Primary source.
- **What it is:** Tutorial building a Fountain-template system with a Collision module and a Light
  Renderer.
- **Technique:** Two things we need on one page. Collision: "Without setting up collision, the
  particles in your effect will just fall through the floor or any other solid objects in the
  level. To add a **Collision** module, click the **Plus sign** (**+**) icon for **Particle
  Update** and select **Collision > Collision**." Then the ordering gotcha — "The **Collision**
  module is inserted at the bottom of the stack, after the **Solve Forces and Velocity** module.
  This causes an error. Click **Fix Issue** to move the Collision module and resolve the error."
  Light: add a **Light Renderer** under Render, set **Radius Scale**, and use **Color Add**, where
  the fields are labelled X/Y/Z but "correspond to RGB values, with **X=Red**, **Y=Green**, and
  **Z=Blue**". Also documents emissive intensity: "if you set a color value greater than 1, it
  becomes emissive color."
- **Per-instance:** One light per particle.
- **Closeness to VtMB:** VtMB's lightning is sprites on a `logic_timer`, and the only dynamic
  light in the corpus comes from `params_explosion`'s `dl_*` keys — so a Light Renderer pulse on a
  lightning card is a named modernization, not a reproduction.
- **Reuse:** The Collision-module placement rule verbatim; the Light Renderer as the optional
  flash pulse riding the lightning sprite's one-shot ramp.

### How to create Rain in Unreal Engine 4 Niagara (Kids With Sticks / *Rogue Spirit*)

- **URL:** https://kidswithsticks.com/how-to-create-rain-in-unreal-engine-4-niagara/
- **Author / credibility:** Kids With Sticks, the studio behind the shipped game *Rogue Spirit*;
  the post is an explicit breakdown of that game's rain. Shipped-title breakdown, highest
  practical value in this list.
- **What it is:** Full written tutorial with per-module screenshots (UE 4.26).
- **Technique:** Five emitters in one system. Drops: CPU sim, fixed bounds, `Spawn Rate` driven
  from a `User.SpawnRate` parameter, `Box Location` for the spawn volume, `Add Velocity`,
  `Collision`, `Scale Sprite Size by Speed`, `Generate Collision Event`, Sprite Renderer "aligned
  by Velocity". The rationale is stated plainly: "We are using CPU simulation as we need
  collisions to make the splashes. If you don't want to have splashes it would be much faster to
  use GPU Sim without a Collision module." Splashes: two mesh emitters, each with an `Event
  Handler` + `Receive Collision Event` — "We are randomly spawning 0 or 1 particle here as we want
  some variety with another splat" — scaled over `NormalizedAge` and faded with `Scale Color`.
  Dips: a sprite emitter on the same collision event with `Add Velocity in Cone`, `Gravity Force`,
  `Drag`, velocity-aligned. Wet decals: a `Decal Component Renderer` needing "an empty sprite
  renderer to properly draw Decal Components", scaled by writing `Particles.Scale` through a "Set
  new or existing parameter directly" module. Attachment: "The most important part here is to
  attach Rain Actor to the player on first enabling."
- **Per-instance:** Yes — one collision event per drop, 0–1 splash of each mesh type plus N dips.
- **Closeness to VtMB:** The closest single reference to `rain_follow_emitter` + `raindrops2` +
  `rainsplash_new` + `RainStain` found anywhere. Differences: their splashes are meshes where
  VtMB's `rainsplash` / `rainring` are sprite cards (`point_16`, `Ringlet`) with radius/size
  ramps; their decals go through the Component Renderer, which the 5.x Decal Renderer replaces.
- **Reuse:** The whole emitter topology; the `User.SpawnRate` knob (our `SetRateScale` /
  `ramp_scale` / `ramp_time` seam); fixed bounds on every emitter; and the honest caveats — "You
  will see that CPU collisions are costly", plus the alternative "you can spawn splashes using
  Distance Field ... Then use Move To Nearest Distance Field Surface GPU Module", which lets the
  fall layer go GPU when per-drop stains are not needed.

### Niagara Advanced Guide — Particle Attribute Reader (Content Examples 2.1 / 2.2 / 2.3)

- **URL:** https://heyyocg.link/en/ue4-26-niagara-advanced-particle-attribute-reader-basic/
- **Author / credibility:** yohanashima — "I am a VFX and Technical Artist working in a game and
  film industry"; a module-by-module dissection of Epic's own *Niagara Advanced* Content Examples
  map. Secondary, but reads the primary sample directly.
- **What it is:** Written breakdown of three Epic sample stands.
- **Technique:** The non-event child-spawn path. "With the Particle Attribute Reader, you can
  obtain attribute information from the same emitter or another emitter's particles. To fetch the
  information, you need to specify which particle's data to retrieve using either the Execution
  Index or Particle ID. The reader and the data source must be the same Sim Target." Stand 2.3
  uses the packaged pair: "the use of two modules: 'Spawn Particles from Other Emitter' and
  'Sample Particles from Other Emitter.' These two modules must be used together." Spawn-rate
  semantics: "if you check 'Calculate Spawn Rate Per Particle,' the spawn rate will be applied per
  particle of the source emitter. In this case, 22 x 50 = 1100 particles will be spawned per
  second", bounded by a "Spawn Rate Per Particle Cap". Sampling mode is Sequential or Random, and
  each inherited attribute (Position, Color, Velocity, Mass) can be Disabled, Apply to Attribute,
  or Output Only. On identity: "Execution Index refers to the order in which the processing is
  done. When particles are continuously born and die, new particles might get the same Execution
  Index as dead particles. On the other hand Particle ID is unique."
- **Per-instance:** Per *parent particle*, continuously — not per collision. Works on GPU sim.
- **Closeness to VtMB:** The faithful mechanism for VtMB's non-collide parent/child relation —
  `waterdrops_timer`'s `spawn { particle WaterDrops_Emitter; burst … }` and every emitter-level
  `spawn { rate … }` block — where a child streams off a live parent rather than off an impact.
  It is the wrong mechanism for `collide { spawn {} }`.
- **Reuse:** Use for the emitter→child `spawn {}` blocks (drops2 at 1,000/s, rainfog at 70/s, the
  drip burst) and keep events strictly for the collide records.

### Rain effect in Unreal (Real Time VFX thread)

- **URL:** https://realtimevfx.com/t/rain-effect-in-unreal/24111
- **Author / credibility:** Mohandish, on realtimevfx.com, the industry RTVFX forum. Practitioner
  post with video.
- **What it is:** Personal-work breakdown thread.
- **Technique:** The split worth copying: "I have 90% of my rain as GPU particles and 10% running
  on CPU so that I can generate collision events that trigger splashes." Puddles are kept out of
  Niagara entirely: "The puddle is pure shader and is not affected by the Niagara system."
- **Per-instance:** Splashes are per-collision on the CPU tenth; the GPU nine-tenths never
  collides.
- **Closeness to VtMB:** Directly answers the cost problem `rain_follow_emitter` creates —
  `raindrops2` is authored at 1,000/s and is the only layer with `collide {}`, so a 10 % CPU
  collider plus a 90 % GPU visual layer reproduces the look at a fraction of the trace budget.
- **Reuse:** Split `raindrops2` into a GPU visual emitter and a low-rate CPU collider emitter;
  keep the wet-surface response in the material — which is what VtMB's `GlobalWetness` /
  `$envmaptint` proxy already is — rather than in particles.

### [Niagara 4.25] Near Surface Location Mini Tutorial

- **URL:** https://realtimevfx.com/t/niagara-4-25-near-surface-location-mini-tutorial/14247
- **Author / credibility:** Niels, on realtimevfx.com; a series of implementation-reference mini
  tutorials, shipped with a pastebin of the raw module script.
- **What it is:** Custom module script tutorial.
- **Technique:** "We will be using Niagara's collision trace logic on the CPU to find surface
  positions near our particle system. We can then use this information to exclude particles from
  spawning, and setting their initial position." The module raycasts from the system centre to a
  point within a radius via the **Perform Collision Query Sync CPU** node; a valid hit snaps the
  particle to the surface, an invalid one falls back to a default position or kills the particle.
  Distributed as "a pastebin to the raw code of the module", pasted directly into the module
  editor.
- **Per-instance:** Per spawned particle at spawn time — one trace, no event.
- **Closeness to VtMB:** Not a VtMB mechanism, but the cheap substitute for two of them: it can
  place splash/ripple particles on the floor with no falling drop at all, and its hit/no-hit test
  is the same shape as the `precipitation "1"` sky-visibility gate (spawn only where a trace
  upward reaches sky).
- **Reuse:** Candidate implementation for the puddle-ripple layer and for a cheap
  `rain_box_noprecip_emitter` variant; also the worked example of `Perform Collision Query Sync
  CPU` if we write our own precipitation-gate scratch module.

### [Niagara 4.25] Particle Decals Mini Tutorial

- **URL:** https://realtimevfx.com/t/niagara-4-25-particle-decals-mini-tutorial/13177
- **Author / credibility:** Niels, realtimevfx.com, crediting @imbueFX for the core material node.
- **What it is:** Material + emitter technique for fake decals, pre-dating the native renderer.
- **Technique:** "The goal is to use WorldPositionBehindTranslucency ... to recreate the behaviour
  of decals for translucent and additive materials." A cube mesh with a centred pivot on a Mesh
  Renderer; the material reconstructs the particle-local position from
  `WorldPositionBehindTranslucency` instead of the fragment position, scales/offsets/saturates to
  a single UV tile, computes a z-mask to fade the decal outside its z-range, and uses translucent
  or additive blend with back-face culling disabled and depth testing off in the advanced
  translucency settings.
- **Per-instance:** One "decal" per mesh particle.
- **Closeness to VtMB:** Very close in *blend* terms. VtMB draws everything, decals included,
  through one mode where `mask` interpolates additive↔translucent
  (`dst = tex.rgb × vcol.rgb + dst × (1 − tex.a × vcol.a)`), and a real deferred decal cannot be
  additive. `RainStain` (`Water_Droplet`) is exactly such a card.
- **Reuse:** The fallback if the native Decal Renderer's deferred-decal material domain cannot
  express `mask`-blended stains. Record it as the alternative, not the default.

### Niagara Particle kill on collision (Epic Developer Community Forums)

- **URL:** https://forums.unrealengine.com/t/niagara-particle-kill-on-collision/447828
- **Author / credibility:** Epic Developer Community forum thread; community answers, but the
  variable it turns on is verifiable engine surface.
- **What it is:** Q&A thread whose question is literally ours: "I have a rain particle, I need it
  to kill the particles that collide that way the rain doesn't come through the ceiling."
- **Technique:** Four variants, all keyed on one built-in. The accepted answer writes a minimal
  scratch module and sets "the 'Kill' bool to 'Particles.HasCollided', which is a builtin var".
  Others drive Lifetime with `Particles.HasCollided` as the alpha, use Advanced Aging Rate to age
  the particle out on impact, or wire the particle-kill module to `hasCollided`.
- **Per-instance:** Per particle, no event needed.
- **Closeness to VtMB:** This is the box-rain containment rule — `rain_box_emitter` fills a brush
  volume and its drops must die at the floor rather than pass through — and the crude form of the
  precipitation gate, which in VtMB kills a `precipitation "1"` particle the moment its BSP leaf
  lacks the sky-visible bit.
- **Reuse:** `Particles.HasCollided` for the volume floor kill; the real sky-bit gate stays the
  named follow-up in `effects-architecture.md` §5.11.

### Dripping in Unreal Engine 5.1 Niagara Tutorial (CGHOW / Ashif Ali)

- **URL:** https://cghow.com/dripping-in-unreal-engine-5-1-niagara-tutorial/
- **Author / credibility:** Ashif Ali (CGHOW), one of the highest-output published Niagara
  tutorial authors; video plus a written breakdown on the site, project files behind Patreon.
- **What it is:** Step-by-step tutorial, UE 5.1, texture → material → Niagara.
- **Technique:** Per the page's own breakdown: a hand-painted splatter texture; a material with a
  mask and radial effect "set up to dissolve over time, controlled by a parameter"; in Niagara he
  "adjust[s] particle spawn rates, align[s] velocities, and set[s] up collision detection to
  ensure particles react when they hit an imaginary plane"; dynamic parameters randomize the
  dissolve and the texture tiling; "multiple Niagara emitters, including spawning particles from
  one emitter to another, creating a more dynamic and flexible dripping effect that can be
  randomized across different locations"; particle size varies with speed; and the collision
  module is used "to remove particles when they collide with a surface".
- **Per-instance:** The emitter-to-emitter spawn is per parent particle; the collision use here is
  kill-on-hit, not spawn-on-hit.
- **Closeness to VtMB:** The randomized-interval, multi-point drip matches `waterdrops_timer`,
  which bursts `WaterDrops_Emitter` every 20–80 frames — a random-interval burst, not a steady
  rate — and which is placed by hand and dense (36 in `sm_hub_1`). The dissolve material is a
  modernization with no VtMB counterpart.
- **Reuse:** The randomized burst interval and the velocity-aligned, speed-scaled droplet; not the
  dissolve material.

### Setting Up Lightning Effects with Unreal Engine 5 (80.lv)

- **URL:** https://80.lv/articles/how-to-set-up-lightning-effects-with-unreal-engine-5
- **Author / credibility:** Dmitrii Sokolov (balaganvfx), VFX Artist at Saber Interactive, writing
  a technique breakdown for 80.lv. Named professional, primary breakdown.
- **What it is:** Article-length breakdown of three lightning approaches.
- **Technique:** Sprite-sheet cards, not ribbons, for the basic bolt: textures generated with
  After Effects' Advanced Lightning and packed with Sheetah; "due to the new default anti-aliasing
  in Unreal Engine 5, I must use the Additive material type to prevent ghosting during fast
  particle animations"; noise texture and UV distortion for the wobble, with "a gradient mask for
  the distortion so that the beginning of the lightning does not move and stays in place";
  "Lightning should have a short lifetime (0.3-0.4 sec). We create the lightning bolts using a
  regular Sprite Renderer in Niagara. To switch between sprites, use the Sub UVAnimation module",
  timed so the particle switches sprite once during its life; and "we offset the pivot to one side
  and set them to be Velocity Aligned" so the bolt is fixed in 3D space instead of camera-facing.
  A second method connects a Sprite Renderer to surfaces using the **Line** module plus distance
  fields instead of a Ribbon Renderer, with the gotcha "you need to turn off the sprite rotation
  (set Sprite Rotation in Initialize Particle module to Unset)". A third packs three bolts into
  RGB channels and picks one in HLSL from a pseudo-random sequence generated by multiplying time
  by 0.618.
- **Per-instance:** One bolt per particle, one-shot.
- **Closeness to VtMB:** Very close in kind. VtMB has no lightning *entity* — the flash is
  `lightning.vmt` / `lightningglow.vmt` `env_sprite`s parented to a `func_rotating` named
  `lightningrotator` and blinked by `logic_timer`s across five outdoor maps. An additive,
  velocity-aligned, pivot-offset sprite card with a 0.3–0.4 s life and a sub-UV flip is the same
  object, built properly.
- **Reuse:** The additive-over-masked rule for UE5 anti-aliasing, the 0.3–0.4 s life, the
  pivot-offset velocity alignment, and the switch-sub-UV-once trick, for the lightning rig.

### Unreal Engine 5 — Rain and Thunder Tutorial (CodeLikeMe)

- **URL:** https://dev.epicgames.com/community/learning/tutorials/5nKZ/unreal-engine-5-rain-and-thunder-tutorial
  — the EDC page is JS-rendered and returns only a title to a direct fetch; the author's own
  announcement thread was fetched in full at
  https://forums.unrealengine.com/t/community-tutorial-unreal-engine-5-rain-and-thunder-tutorial/620738
- **Author / credibility:** Thilina Premasiri (CodeLikeMe), a long-running UE tutorial channel;
  published on Epic's own Developer Community.
- **What it is:** Video tutorial series, system-level and Blueprint-driven.
- **Technique:** A player-anchored weather rig rather than a single-emitter trick: "no matter
  where the player would go, if it is raining, the rain particles will always be around the player
  character and the thunder will appear randomly in distance", with "All the sounds for rain and
  thunder effects will be added." Built for open-world maps, re-using effects from the author's
  earlier rain series.
- **Per-instance:** N/A — this is the actor/manager layer above the emitters.
- **Closeness to VtMB:** This is the `attach_type 11` (`PlayerSky`) behaviour of `sm_hub_1`'s two
  `rain_emitter` entities — a `spawnbounds` cube round the viewer that wraps and spawns above the
  player — plus the randomly-timed distant flash the `logic_timer` rig provides. Weaker on emitter
  internals than the other entries; take the manager shape only.
- **Reuse:** The follow-actor + audio + randomized-thunder manager pattern for
  `rain_follow_emitter` and the `rain_on_timer` / `rain_off_timer` cycle, which
  `AElysiumEffectActor` and the valve classes already have a home for.

## Patterns worth copying

- **Events for collide records, Attribute Reader for spawn records.** `Generate Collision Event` +
  `Receive Collision Event` is per-impact and reproduces `raindrops2`'s
  `collide { spawn { rainsplash_new } }` exactly; `Spawn Particles from Other Emitter` +
  `Sample Particles from Other Emitter` is per-parent-particle and reproduces the emitter-level
  `spawn { rate }` / `burst` blocks (`rain_follow_emitter`, `waterdrops_timer`). Do not mix them.
- **The fall layer must be CPU, and only the part that collides.** Events are CPU-only, so split
  `raindrops2` into a GPU visual majority and a low-rate CPU collider (Mohandish's 90/10) rather
  than paying 1,000 line traces/s for the authored 1,000 drops/s.
- **Collision goes before Solve Forces and Velocity**, and Niagara offers "Fix Issue" when the
  module lands at the bottom of the stack — documented in Epic's own particle-light tutorial.
- **`Particles.HasCollided` is the volume floor kill** for `rain_box_emitter` /
  `rain_box_noprecip_emitter`, and the crude stand-in for the `precipitation "1"` sky-leaf gate
  until §5.11 lands.
- **`Box Location` plus one exposed rate scalar is the whole box/follow contract.** Box Location is
  the brush volume; the `User.SpawnRate`-style parameter is `SetRateScale` / `ramp_scale` /
  `ramp_time`, the single per-emitter float VtMB multiplies into both `rate` and `burst`.
- **Attach the rain actor to the player on activation** instead of moving the emitter per frame —
  the `attach_type 11` (`PlayerSky`) 512-unit `spawnbounds` cube of `sm_hub_1`'s `rain_emitter`
  pair, and what both Kids With Sticks and CodeLikeMe do.
- **Splashes are one-shot ramps over normalized age.** Scale size and fade alpha by
  `NormalizedAge`, random lifetime, 0–N particles per collision event — the direct translation of
  `rainsplash`'s radius ramp 100→0 and `rainring`'s size 1→10 with a brightness fade.
- **`RainStain` is a Decal Renderer emitter with Source Mode = Particles**, driven by the same
  collision event as the splash, with `Particles.DecalOrientation` from the collision normal and
  `Particles.DecalFade` carrying the age ramp. Keep the `WorldPositionBehindTranslucency` mesh
  trick in reserve for stains that genuinely need `mask`-style additive blending.
- **Avoid the Decal Component Renderer.** Epic marks the Component Renderer Experimental and warns
  about its per-component tick; Kids With Sticks' own post-ship verdict was that the decals "will
  break your game". The corpus's 174 `decal {}` placements need the native renderer.
- **Lightning is an additive, velocity-aligned, pivot-offset sprite card with a 0.3–0.4 s life and
  a single sub-UV switch** — the properly-built version of the `env_sprite` + `logic_timer` rig,
  with a Light Renderer pulse recorded as an explicit modernization (the corpus has no dynamic
  light outside `params_explosion`'s `dl_*` keys).
