# Dust, beams, sparks and lights — Niagara reference examples

Reference material for the ambient half of the effects work: real, opened, linkable Niagara
examples that reproduce what `docs/vtmb/effects.md` §4.1 / §4.5 / §4.7 / §4.12 / §4.16 inventory
and `docs/architecture/effects-architecture.md` §5.5–5.8 model — `func_dustmotes` (slow motes in a
brush volume with camera-distance fade and a wind term), `env_beam` (a textured, noisy, tapering
beam between two named endpoints with a strike timer), `airplane_emitter` (a distant body with
blinking red/green/white child sprites that ride the parent), `moth_emitter` (motes circling a
lamp), `sparks_*` / `impactfx_sparks` (short additive streaks with gravity, drag and a bounce),
the muzzle pair (`muzzleflash_emitter_a/b` plus its smoke), and `env_sprite` glow cards
(`sprites/glowa`, `streetlight3_proxyfade`) that need a distance and occlusion fade instead of
Source's `rendermode 3` glow-occlusion query. Every URL below was opened with `kagi_extract` or
`WebFetch` and the quoted lines are what the page shows; nothing here is a plan or a ruling — the
contract stays in `effects-architecture.md`. Collected 2026-09-02.

### Create a Realistic Dust Particle Effect (Unreal Editor for Fortnite), parts 1–3

- **URL** — https://dev.epicgames.com/documentation/en-us/fortnite/dust-tutorial-1-create-a-dust-particle-material-and-niagara-system-in-unreal-editor-for-fortnite (part 2: `…/dust-tutorial-2-modify-the-emitter-in-unreal-editor-for-fortnite`; part 3: `…/dust-tutorial-3-edit-the-spawn-effect-shape-in-unreal-editor-for-fortnite`)
- **Author / credibility** — Epic Games official documentation (UEFN; the Niagara stack is the same as UE5's).
- **Kind** — step-by-step tutorial, three pages, exact module values.
- **Technique** — CPU sprite emitter built from the **Hanging Particulates** template: a Translucent
  material (texture RGB → Base Color, alpha divided by 1.0 → Opacity), then **Shape Location** with
  `Shape Primitive = Box/Plane`, Box Size 450 cubed and a **Random Range Vector** Box Midpoint;
  **Initialize Particle** with `Lifetime Mode = Random` 5–8 s, `Sprite Size Mode = Random Uniform`
  2.0–3.5, `Sprite Rotation Mode = Random`, `Sprite UV Mode = Random X/Y`; **Scale Sprite Size**
  curve over life; **Spawn Rate** as a `Random Range Float` 75–200 with "Uncheck the **Recalculate
  Random Each Loop** option". Part 3 swaps the primitive to **Cylinder** (height 200, radius 25),
  adds a Random Vector non-uniform scale and an Axis-Angle rotation, and halves the rate to 25–50.
  Part 2 closes on the box version ("This visual effect is perfect for a room where a large window
  lets in sunlight.") and part 3 opens with the shaft ("This effect works great for a room with a
  large amount of light, but what if you want to use this effect in a shaft of light?").
- **Proximity to the VtMB look** — very close for `func_dustmotes` and the Troika night-mote roots
  (`starynight_emitter`, `glowywierd_emitter`): sparse, slow, per-particle random size/rotation, a
  box or shaft volume. It has no wind term, no camera-distance kill and no in-solid rejection, which
  is exactly the delta `NS_ElysiumDust` already carries.
- **Reuse** — the module list and the value ranges as the starting tune for `NS_ElysiumDust`
  (`SizeMin/SizeMax`, `LifetimeMin/Max`, `SpawnRate`), and the Box→Cylinder swap as the shape
  answer for a `func_dustmotes` brush versus a light-shaft placement.

### Niagara Content Examples map (Epic) — 1.4 Sprite Facing, 2.1 Static Beams, 2.2 Dynamic Beams, 2.6 Collision, 3.2 Renderer Overrides

- **URL** — https://dev.epicgames.com/documentation/en-us/unreal-engine/niagara-content-examples?application_version=4.27
- **Author / credibility** — Epic Games official documentation for the free Content Examples project.
- **Kind** — sample project index; each numbered stand is an openable system in the map.
- **Technique** — the stands that matter here, quoted: **2.1 Static Beams** "demonstrates using
  static beams spawned using static start and end points rather than dynamically updating per frame.
  Unlike Cascade, each beam segment is a simulated particle which can be further influenced by forces
  or other effects." **2.2 Dynamic Beams** "demonstrates dynamic beams that have end points and
  tangents which are recalculated every frame." **2.6 Collision** "shows how to handle collision
  events being sent to a different emitter which spawns particles in response to that collision
  event. Collision queries are done with either line traces on the CPU or depth buffer/distance field
  checks on the GPU." **1.4 Sprite Facing** shows "sprites that can face the camera, or that can face
  any arbitrary vector". **3.2 Renderer Overrides**: "The sprite renderer used here is being given a
  new position as an offset from the arrow mesh position, and it is being driven by a different color
  attribute than the arrow mesh. Both are part of the same, single emitter."
- **Proximity to the VtMB look** — 2.2 is the `env_beam` case (endpoints recomputed every frame
  because the two targetnames can move); 2.6 is the particle `collide {}` block, including the
  spawn-on-impact record; 1.4 is `movealign` / `flat`; 3.2 is the cheapest reading of
  `airplane_emitter` — child sprites offset from a parent inside one emitter.
- **Reuse** — open 2.2 before tuning `NS_ElysiumBeam`'s per-strike endpoint refresh; open 2.6 before
  wiring the R7.2 runtime-decal / impact-spawn seam; 3.2 is a one-emitter alternative to an attribute
  reader for the aircraft lights.

### How to Create a Beam Effect in Niagara for Unreal Engine

- **URL** — https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-create-a-beam-effect-in-niagara-for-unreal-engine
- **Author / credibility** — Epic Games official Niagara tutorial.
- **Kind** — tutorial with the full module stack and an actor-driven endpoint.
- **Technique** — "In Niagara, the Ribbon Renderer is used along with specific modules that indicate
  that the ribbon is being used as a beam." Start from the **Static Beam** template; the three
  beam-specific modules are **Beam Emitter Setup** (Emitter Update — `Beam Start` / `Beam End` with
  **Absolute Start** and **Absolute End** checked, optional `Use Beam Tangents` for an arc),
  **Spawn Beam** (Particle Spawn; "You do not need to set anything with this module, it just needs to
  be present") and **Beam Width** (`Float from Curve`, Ramp Up Down template, Scale Curve 5).
  `Spawn Burst Instantaneous` 35 sets the segment count; the ribbon renderer's **Tessellation →
  Curve Tension** (0.5) controls spikiness. Jaggedness comes from **Jitter Position** in Particle
  Update placed *below* an added **Update Beam** module, with `Jitter Delay = -0.01` and
  `Jitter Amount = 15`. The end point is bound to a level actor via a Scratch Pad dynamic input
  (Actor Component Interface → Get Transform → Position) promoted to a renamed User parameter
  `Beam_End`, then set per instance through **Override Parameters → Source Actor**.
- **Proximity to the VtMB look** — the structural match for `env_beam`: a fixed segment count along a
  spline (VtMB uses 128 divisions), a width curve (VtMB tapers to 10 %), a per-frame noise
  displacement (VtMB's `noise amplitude × length / 100`) and a user-parameter endpoint pair. It does
  not scroll the texture; that stays the material's `TextureScroll` on our side.
- **Reuse** — `User.Start` / `User.End` / `User.Width` / `User.NoiseAmplitude` map onto Beam Emitter
  Setup + Beam Width + Jitter Position; the Scratch-Pad-to-User-parameter recipe is the pattern for
  driving both endpoints from `AElysiumBeamActor` each strike.

### "How to make a beam-ribbon hybrid in Niagara" (Epic Developer Community forums)

- **URL** — https://forums.unrealengine.com/t/how-to-make-a-beam-ribbon-hybrid-in-niagara-tutorial/1278861
- **Author / credibility** — forum user **RhythmScript**, 2023-08-31; a written tutorial that dissects
  the stock beam modules rather than re-skinning them.
- **Kind** — community tutorial / technique breakdown.
- **Technique** — the clearest statement of what a Niagara beam actually is: a beam "takes a fixed set
  of particles (usually spawned with Burst Instantaneous), places them at intervals along the beam,
  and manually defines a ribbon link order for each one", and therefore "a beam is not a path. When
  you define a beam, you are not defining a trajectory that particles will follow, you are defining a
  sequence of positions particles will inhabit." The hybrid removes **Spawn Beam**, duplicates
  **Update Beam** to drive position from `Particles.NormalizedAge` instead of `RibbonLinkOrder`, adds
  a Spawn Rate, and lerps between the beam-defined position and the particle's own position; leftover
  `RibbonLinkOrder` references cause "shoelacing" artifacts.
- **Proximity to the VtMB look** — explains why an `env_beam` reproduction should stay a burst-spawned
  beam (a geometric line with noise) and not a trail; VtMB's beam is exactly "a sequence of
  positions", rebuilt per frame.
- **Reuse** — the rule that segment count is a burst count, and the warning about mixing
  `RibbonLinkOrder` with age-driven positions, if `NS_ElysiumBeam` ever needs a moving or decaying beam.

### "[Niagara 4.25] Particle Location Module Mini Tutorial" (Real Time VFX)

- **URL** — https://realtimevfx.com/t/niagara-4-25-particle-location-module-mini-tutorial/13133
- **Author / credibility** — **Niels Dewitte**, game VFX artist (author of the VFExtra module pack);
  realtimevfx.com is the industry VFX forum.
- **Kind** — module-building mini tutorial (module graph, screenshots).
- **Technique** — "Using the particle attribute reader data interface we can make a reasonably
  optimized particle location module." The child emitter takes a **Particle Attribute Reader**
  parameter naming the parent emitter, calls **Get Num Particles**, picks a **Random Range Integer**
  in `[0, count-1]`, reads position (and velocity) with **Get Vector By Index**, gates on the reader's
  `Valid` output, subtracts the owner position so downstream offset modules still work, and multiplies
  the inherited velocity by an `inheritVelocity` parameter. Hard limit: "the source and target sim
  targets need to be the same! GPU and CPU sim targets cannot read each other's attributes right now."
  Supporting note, also opened —
  https://raw.githubusercontent.com/ibbles/LearningUnrealEngine/master/Niagara%20attribute%20reader.md
  — adds that reads can be by **ID or Index**, that the `Emitter Name` property is usually only
  settable at the System level, and that "The data will be one frame behind".
- **Proximity to the VtMB look** — this is the general form of VtMB's `parent_speed` / attach-tree
  behaviour: a child emitter whose particles sit on a parent particle. For `airplane_emitter` the
  parent is one long-lived body particle and the children are the nav lights.
- **Reuse** — if the aircraft lights become their own emitter (rather than Content Examples 3.2's
  single-emitter offset), this is the module to copy — with the CPU/GPU sim-target rule and the
  one-frame lag written into the family's notes.

### "Niagara: orbiting around a point?" (Real Time VFX)

- **URL** — https://realtimevfx.com/t/niagara-orbiting-around-a-point/25863
- **Author / credibility** — asked by **goldylox** (2024-03-05), answered by **Niels** (Dewitte).
- **Kind** — technique thread with a working recipe.
- **Technique** — do not fight `Rotate Around Point` (it overwrites position). Instead: "Create a
  vector offset with sin and cos using the current particle age (or normalized age)", add it to the
  particle's own position, "set into `Particles.FinalPosition` (or any other position parameter that
  isn't `Particles.Position`)", then "In the renderer module of the emitter, set the position binding
  to `Particles.FinalPosition`." The simulated position stays the orbit centre; the renderer draws the
  offset one.
- **Proximity to the VtMB look** — `moth_emitter` is a lamp-centred circling swarm; VtMB does it with a
  spherical `(radius, theta, phi)` offset rebuilt each frame around the emitter origin
  (`effects.md` §2.4), which is literally a sin/cos offset from an unmoved centre. This recipe is the
  same maths in Niagara terms.
- **Reuse** — the renderer position-binding trick is the cheapest way to express VtMB's
  `theta_speed` / `phi_speed` / `radius_speed` triple on top of an otherwise still particle — for
  moths, flies, and any Troika root whose only motion is the spherical offset.

### "Niagara Orbit Module" (Epic Developer Community forums)

- **URL** — https://forums.unrealengine.com/t/niagara-orbit-module/442114
- **Author / credibility** — asked by **MikeZeen** (porting Cascade orbits), answered by
  **EtherionDesigns**, with a caveat from **Anthony_Attwood**.
- **Kind** — Q&A thread; the naming answer that saves an hour.
- **Technique** — "Orbit in Cascade is called Vortex in Niagara." The caveat: "Vortex seems to have an
  origin pull and I don't know how to stop this from happening" — the Vortex force adds a radial
  component, so a clean constant-radius circle still wants the sin/cos offset above.
- **Proximity to the VtMB look** — moderate. Vortex Force gives a lazy, drifting circulation, which is
  the right feel for moths near a lamp; a pure geometric orbit is closer to VtMB's authored
  `theta_speed`.
- **Reuse** — Vortex Force as the "wandering circle" variant of `moth_emitter`, the sin/cos offset as
  the strict one; both are cheap enough to try in the tuning session.

### How to Create a Sparks Effect in Niagara for Unreal Engine

- **URL** — https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-create-a-sparks-effect-in-niagara-for-unreal-engine
- **Author / credibility** — Epic Games official Niagara tutorial (rebuilds the Starter Content sparks).
- **Kind** — tutorial, three emitters, every value listed.
- **Technique** — three CPU emitters in one system: smoke (`M_smoke_subUV`), a central spark burst
  (`M_Spark`), and the radial sparks. The radial emitter is the one that matters: material
  `M_Radial_Gradient` with the Sprite Renderer's **Alignment** set to **Velocity Aligned**; Spawn Rate
  500; lifetime 0.2–0.7 s; emissive colour `RGB 2 / 8 / 20`; `Add Velocity` random range
  `X/Y −100…90, Z 300…500`; `Shape Location` sphere radius 2; then in Particle Update **Scale
  Velocity**, **Gravity Force** `Z = −4500`, **Drag** 1.7, and a **Collision** module — "This module
  will make sure any sparks that hit something like a floor will collide and bounce back" — with
  `Restitution 0.4` and `Friction 0.2`; finally **Scale Sprite Size by Speed** (X 0→0.5, Y 3→6,
  velocity threshold 2000) so fast sparks stretch and slow ones shorten.
- **Proximity to the VtMB look** — very close to `sparks_*` and `impactfx_sparks`: VtMB's collide block
  is `v' = bounce × normal + friction × tangent` plus gravity and `pow(drag, dt)`; restitution and
  friction are the same two numbers, and `movealign` is the same as Velocity Aligned.
- **Reuse** — the mapping VtMB `bounce` → Collision `Restitution`, VtMB `friction` → Collision
  `Friction`, VtMB `gravity` → Gravity Force, VtMB `drag` → Drag; plus **Scale Sprite Size by Speed**
  as a modernization of the fixed-size streak (a named improvement, not a VtMB behaviour).

### How to Create Particle Effects That Emit Light in Niagara

- **URL** — https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-create-particle-effects-that-emit-light-in-niagara-for-unreal-engine
- **Author / credibility** — Epic Games official Niagara tutorial.
- **Kind** — tutorial; adds a second renderer to a Fountain emitter.
- **Technique** — a **Light Renderer** added alongside the Sprite Renderer on the same emitter, so
  every particle also is a light: `Radius Scale 5.0`, `Color Add` given as X/Y/Z = R/G/B. It also
  states the emissive rule for particle colour: "if you set a color value greater than 1, it becomes
  emissive color", and adds a **Collision** module with the "Fix Issue" stack-order correction.
- **Proximity to the VtMB look** — VtMB never lights the world from particles (`effects.md` §4.17:
  "The look of a fire is particles; the illumination is a light"), so this is the modernization lane,
  not a reproduction: the muzzle flash's one-frame dynamic light, the airplane's nav lights and a
  moth-lamp can all be a Light Renderer instead of a separate actor.
- **Reuse** — the muzzle pair's one-frame light (255/192/64) and the aircraft lights; keep the light
  count small — VtMB's authored intent is a sprite, the light is ours.

### Render Module Reference for Niagara Effects (renderer parameter reference)

- **URL** — https://dev.epicgames.com/documentation/en-us/unreal-engine/render-module-reference-for-niagara-effects-in-unreal-engine
- **Author / credibility** — Epic Games official reference page for every Niagara renderer.
- **Kind** — parameter reference (Component, Light, Mesh, Ribbon, Sprite, Decal renderers).
- **Technique** — the sprite knobs our floor and family systems bind: **Material** ("The material
  selected must have the **Use with Niagara Sprites** flag checked"), **Alignment** ("defines how the
  particle alignment is affected by other parameters… the **Unaligned** setting indicates that only
  **Particle.SpriteRotation** and **FacingMode** parameters affect the alignment of the particle"),
  **Facing Mode** ("defines how the sprite particle orients itself relative to the camera"),
  **Sub Image Size** for SubUV lookups, and **Sort Mode** — "**None**… **View Depth**: This sorts by
  depth to the camera's near plane. **View Distance**: This sorts by the distance to the camera's
  origin. **Custom Ascending**…". The **Light Renderer** exposes `Radius Scale` and a
  `Volumetric Scattering Binding`. The **Component Renderer** "provides a way for you to spawn any
  type of component, and update its properties with data from the particle simulation" (its example is
  a Point Light) but is flagged Experimental: "We do not recommend shipping projects with Experimental
  features."
- **Proximity to the VtMB look** — this is where VtMB's drawing flags land: `movealign` → Alignment,
  `flat` → Facing Mode / custom facing, `sortfront` and `depth_offset` → Sort Mode plus the material's
  depth handling, the sprite atlas → Sub Image Size.
- **Reuse** — the authoritative parameter names for the family systems and for
  `effects-architecture.md` §5.11's fidelity ledger; plus the note that a per-particle Point Light via
  the Component Renderer is Experimental, so the airplane lights should be a Light Renderer or plain
  sprites.

### Particles Material Functions — `3dParticleOpacity` (camera-distance fade)

- **URL** — https://dev.epicgames.com/documentation/en-us/unreal-engine/particles-material-functions-in-unreal-engine
- **Author / credibility** — Epic Games official material-function reference.
- **Kind** — reference page for the stock particle material functions.
- **Technique** — `3dParticleOpacity`'s purpose is "to help setup particles such that they fade away as
  they move away from the camera". The camera-falloff group: **Use Near Camera Falloff** — "Setting
  this to *true* makes the particles falloff as they approach the camera"; **Near Camera Falloff Start
  Distance**; **Near Camera Fade Distance (1/n)** — "The distance from the camera where the opacity
  fades to 0. Enter the reciprocal of the target distance. For example, to fade the particle out over
  256 units enter 1/256"; plus a depth-texture path and **Use Particle Alpha** to multiply by the
  particle colour's alpha.
- **Proximity to the VtMB look** — serves two VtMB behaviours directly: `func_dustmotes`'
  `alpha = (viewZ / DistMax + 1) × …` with its `DistMax` cull, and the `_proxyfade` glow materials
  (`streetlight3_proxyfade`, `volumelighta_proxyfade`) whose whole job is a distance fade.
- **Reuse** — the reciprocal-distance input convention for the dust material and for the `env_sprite`
  glow master; it keeps the fade in the material, where VtMB also computed it per particle, instead of
  spending a Niagara module.

### "Screenspace 'Light Shafts' shader for local particle flares" (Real Time VFX)

- **URL** — https://realtimevfx.com/t/screenspace-light-shafts-shader-for-local-particle-flares/22568
- **Author / credibility** — **Figment** (2023-03-09), a working VFX artist posting progress and the
  final cost verdict in the same thread.
- **Kind** — technique thread with an outcome, including the negative result.
- **Technique** — occlusion for a glow card without an occlusion query: on the sprite's material
  "sample the scene depth and pixel depth and do an 'if' statment to color it black and white (black
  is all geo in front, white is behind", then blur that mask for the shaft. The author got it working
  with custom nodes and no render target, but reported: "it ended up being a bit too expensive on
  consoles as it was sampling every pixel in the glow and doing a radial blur with 16-18 samples,
  every frame", and closed with "I would look into unreals voxel/volumetric fog for your light shafts."
- **Proximity to the VtMB look** — this is the Unreal answer to Source's `rendermode 3`
  occlusion-faded glow (969 `sprites/glowa` placements plus the `_proxyfade` set): VtMB ran a hardware
  occlusion test per glow sprite; Unreal has no equivalent per-sprite query, so it is either a
  scene-depth comparison in the material or volumetric fog doing the job physically.
- **Reuse** — the scene-depth-vs-pixel-depth comparison as the cheap per-sprite occlusion fade for
  `env_sprite` glows, with the recorded warning that the *blurred* version is console-expensive; and
  volumetric fog as the modernization for `sprites/volumelighta` shafts.

### Niagara Examples Pack (Epic Games, free on Fab, UE 5.7)

- **URL** — https://80.lv/articles/epic-releases-over-50-free-niagara-systems-for-unreal-engine-5-7
  (article opened; it links the download at https://www.fab.com/listings/0e188eca-4e54-4fb2-a9ed-d8b8a565e600
  and the announcement at
  https://www.unrealengine.com/news/discover-over-50-free-niagara-systems-ready-to-use-in-unreal-engine-5-7
  — both refuse automated fetches, so the contents below are quoted from the 80.lv piece)
- **Author / credibility** — pack authored by Epic Games; coverage by **Emma Collins**, 80 Level,
  2026-01-29.
- **Kind** — free first-party asset pack plus a playable gallery level. Licence: Epic-published free
  Fab listing — read the listing's licence in the launcher before shipping any asset from it.
- **Technique** — "Epic introduced the first release of the Niagara Examples Pack for Unreal Engine
  5.7. The pack is available for free on Fab… features more than 50 VFX Niagara systems." Contents:
  "explosions, bullet impacts, trails to sparks, fire, smoke, mist cards, and player and weapon
  buffs/debuffs… Animation-Notify-based footstep effects, pings and markers, lightning, hit
  dissolves". "The pack also includes a dedicated gallery gameplay level that you can open and play
  through", and "the collection features impact and footstep systems, which make use of Niagara Data
  Channels and Animation Notify Blueprints."
- **Proximity to the VtMB look** — the impact and footstep systems are structurally our problem: one
  authored system serving many runtime hit events, which is what `particleimpacttable.txt` and the
  5001 / 5003 animation events need. The pack's muzzle-flash system is the one the community forum
  thread on the pack discusses porting to other weapons.
- **Reuse** — read the Data-Channel impact setup before implementing `SpawnParticleRoot`
  (`effects-architecture.md` §5.9): it is Epic's answer to "many small transient spawns" and a
  candidate for the impact table without an actor per hit. The sparks, lightning and mist-card systems
  are direct references for `sparks_*`, the sprite lightning and `rainfog`.

### Muzzle Flashes Vol. 1 — Niagara (Gabriel Aguiar Prod.)

- **URL** — https://www.gabrielaguiarprod.com/product-page/muzzle-flashes-vol-1-niagara
- **Author / credibility** — **Gabriel Aguiar**, a widely-followed game-VFX author (YouTube tutorials
  plus Fab/Marketplace packs); the page carries a showcase video.
- **Kind** — commercial asset pack, US$15, "From Unreal Engine 4.24 to 5". Not free — a reference for
  structure, not a source of assets.
- **Technique** — "25 Muzzle Flashes done entirely with **Niagara** and specifically made to be super
  **customizable** by you." Stated build: "**Type of Emitters**: NIAGARA | GPU | Mesh Emitters",
  25 Niagara systems from only **4 emitters**, 29 textures, 44 materials, 24 blueprints, 4 meshes.
- **Proximity to the VtMB look** — the ratio is the lesson, not the art style: 25 muzzle looks off 4
  reusable emitters is exactly `DA_EffectFamilies`' one-system-many-roots shape, and VtMB's muzzle is
  likewise a small burst card plus a separately named smoke emitter (`effects.md` §3.4 / §4.7).
- **Reuse** — evidence that the muzzle family should be two small burst systems (flash + smoke)
  parameterised per weapon, matching `effects-architecture.md` §4's "two `SimpleSpriteBurst` systems…
  plus a one-frame point light" row — and that mesh (cone/star) emitters, not only sprites, are the
  modern muzzle idiom.

## Patterns worth copying

- Build `NS_ElysiumDust` on the **Hanging Particulates** stack (Shape Location box → cylinder, random
  lifetime/size/rotation, random-range spawn rate) and keep our extra terms — in-solid rejection,
  `User.Wind`, `User.DistMax` — as the delta over Epic's tutorial.
- Put `func_dustmotes`' distance term and every `_proxyfade` glow fade in the **material**, using
  `3dParticleOpacity`'s reciprocal `Near Camera Fade Distance` convention, not in a Niagara module.
- `NS_ElysiumBeam` is **Beam Emitter Setup + Spawn Beam + Beam Width + Update Beam + Jitter Position**
  on a Ribbon Renderer; the segment count is the `Spawn Burst Instantaneous` count (VtMB's 128
  divisions) and the width curve is the 10 % taper.
- Drive `User.Start` / `User.End` the way Epic's beam tutorial drives `Beam_End`: a User parameter
  written from outside per strike, so `AElysiumBeamActor` owns endpoint resolution and the striker.
- Keep a beam a **burst-spawned sequence of positions**, never a trail: leftover `RibbonLinkOrder`
  wiring plus age-driven positions is what produces shoelacing (RhythmScript).
- `airplane_emitter`'s nav lights: first try one emitter with a **renderer position/colour override**
  (Content Examples 3.2); reach for the **Particle Attribute Reader** module only if they need their
  own emitter — same sim target on both, one frame of lag.
- `moth_emitter` is a sin/cos offset written to `Particles.FinalPosition` with the renderer's position
  binding repointed — the direct Niagara spelling of VtMB's `(radius, theta, phi)` spherical offset;
  **Vortex Force** is the looser, drifting variant.
- `sparks_*` / `impactfx_sparks` map term-for-term onto **Gravity Force + Drag + Collision**
  (Restitution = VtMB `bounce`, Friction = VtMB `friction`) with the Sprite Renderer set to
  **Velocity Aligned** for `movealign`; `Scale Sprite Size by Speed` is the modernization.
- `muzzleflash_emitter_a/b` stays two small burst systems (flash + smoke) parameterised per weapon,
  plus a **Light Renderer** or one-frame point light — VtMB draws the sprite, the light is ours.
- Before building `SpawnParticleRoot` for the impact table and the animation-event bus, read Epic's
  **Niagara Data Channel** impact and footstep systems in the free Niagara Examples Pack: one
  persistent system fed by many events beats one actor per hit.
