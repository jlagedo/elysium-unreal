# Reference examples: fire, smoke, steam, heat haze

Linkable, opened-and-quoted examples for reproducing the VtMB fire/smoke/steam family
(`docs/vtmb/effects.md` §4.2, §4.3) in Niagara: `barrelfireemitter` (flame card `Flamemass`
+ glow + `Smoke1` + `FlameEmbers1` + `Fire_Heat`), `fire1_emitter` / `fire2_emitter`,
`molotov_emitter`, torches and `sprites/candle`, cigar/cigarette smoke, and
`steamrelease_constant_emitter` / `env_steam`. The VtMB runtime (§2.4) is a single sprite
material, one blend mode `BlendFunc(ONE, ONE_MINUS_SRC_ALPHA)` in which the `mask` key is a
continuous additive-to-translucent interpolant, keyframed ramps in normalized age, and children
spawned at parent particles — so the sources below were chosen for what they say about
**premultiplied-alpha blending**, **sub-UV flame cards**, **curves over normalized age**,
**refraction cards** and **low-rate long-life sprite plumes**, not for looking pretty.
Every URL here was opened with `kagi_extract` or `WebFetch`; quotes are from the page.

### Material Blend Modes in Unreal Engine (Epic documentation)

- **URL** — https://dev.epicgames.com/documentation/unreal-engine/material-blend-modes-in-unreal-engine?lang=en-US
- **Author / credibility** — Epic Games, official UE5 documentation. Primary source.
- **What it is** — Reference page, with the blend-mode formula table.
- **Technique** — Gives `AlphaComposite (Premultiplied Alpha)`: "Final Color = Source Color +
  Dest Color * (1 - Source Opacity)". Also: "AlphaComposite works by multiplying the underlying
  scene color by the inverse of the material's opacity so that when the material is added to the
  scene color, areas of high opacity appear more saturated"; and on plain Additive: "One drawback
  of Additive Materials is that they are often difficult to see against light colored backgrounds
  … A solution is to use the AlphaComposite Blend Mode instead."
- **Closeness to VtMB** — Exact. AlphaComposite *is* the VtMB particle blend. `mask 0` →
  opacity 0 → pure additive; `mask 255` → opacity = texture alpha → occluding card. One material
  covers flame, glow, smoke, embers and rain.
- **Reuse** — Make the sprite master material `AlphaComposite`, Unlit; feed
  `Emissive = tex.rgb * ParticleColor.rgb` and `Opacity = tex.a * (mask/255) * ParticleAlpha`.
  Direct translation of `dst = tex.rgb×vcol.rgb + dst×(1 − tex.a×vcol.a)`.

### AlphaComposite finally! (Epic Developer Community Forums)

- **URL** — https://forums.unrealengine.com/t/alphacomposite-finally/56936
- **Author / credibility** — "Moss", 1 March 2016, written up after a Julian Love (Riot) GDC talk
  on "BlendAdd". Long-cited thread.
- **What it is** — Forum explainer.
- **Technique** — States the blend exactly: `res.rgb = src.rgb + (dst.rgb * (1.0 - src.a))`,
  `res.a = src.a + (dst.a * (1.0 - src.a))`. Why it beats Additive: additive colours "go to white
  with more and more overdraws while the left side still holds the details"; with AlphaComposite
  "the alpha part is already within the color of the material, this way you can control your
  opacity and highlight better". Authoring note: "I actually feed the opacity not directly in the
  opacity pin, I pre-multiply it with the actual color part to control the final opacity there."
- **Closeness to VtMB** — The equation is character-for-character the mode-8 sprite shader
  decoded in §2.4.
- **Reuse** — The premultiply-in-the-graph trick: our `mask` ramp multiplies both the emissive and
  the opacity, which is how one card goes from `mask 0` glow to `mask 255` smoke.

### Niagara Flipbook Baker Quick Start Guide (Epic documentation)

- **URL** — https://dev.epicgames.com/documentation/unreal-engine/niagara-flipbook-baker-quick-start-guide-in-unreal-engine?lang=en-US
- **Author / credibility** — Epic Games, official.
- **What it is** — Tutorial: bake a Niagara sim to a sprite sheet, then drive it from a Sprite
  Renderer.
- **Technique** — Baker panel, `Frames Per Dimension` 8×8 into a 1024×1024 atlas ("For ideal
  behavior, set your Texture Size to be a power of 2, and the Frames Per Dimension to be a number
  that divides evenly into that texture size"); then a `SubUVAnimation` module in Particle Update
  with start/end frame 0–63 and `Sub Image Size` 8×8 on the Sprite Renderer. Crucially, on the
  material: "By default, the color channels are premultiplied with black. To avoid black fringing
  around the sprites, you can use a divide node to remove this from the RGB channels" — RGB ÷ A
  into Emissive, A into Opacity.
- **Closeness to VtMB** — The sub-UV path is how a `Flamemass` / `FirePlace_Flames` card with
  `fps 30` / `frames` animates. The divide-by-alpha note is the canonical answer to the "black
  card" problem.
- **Reuse** — Atlas sizing rule; the divide node if we ever author a straight-alpha flame sheet;
  the Baker itself if a Niagara Fluids flame proves too heavy for the three working maps.

### Create a Sprite Particle Effect in Niagara — sprite smoke (Epic documentation)

- **URL** — https://dev.epicgames.com/documentation/en-us/unreal-engine/create-a-sprite-particle-effect-in-niagara?application_version=4.27
- **Author / credibility** — Epic Games, official; the canonical Niagara smoke-column how-to.
- **What it is** — Step-by-step tutorial building a rising smoke plume.
- **Technique** — Full module stack with numbers: Sprite Renderer on `M_smoke_subUV`,
  `Sub Image Size` 8×8, `Sub UV Blending Enabled`; Emitter Update = `Spawn Rate` 50 (burst module
  deleted), Life Cycle `Self` / Infinite; Particle Spawn = Lifetime `Random Range Float` 2.0–3.0,
  Sprite Size 75–200, `Sprite Rotation Mode: Direct Normalized Angle (0-1)` random 0.25–0.5,
  `Add Velocity` min (0,0,50) max (1,1,200), `Sphere Location` radius 64, `SubUV Animation` mode
  Linear with 64 frames; Particle Update = `Acceleration Force` Z 500 (with the documented "unmet
  dependencies / Fix Issue" reorder before `Solve Forces and Velocity`).
- **Closeness to VtMB** — This is `Smoke1` / `furball` / `cloud`: continuous, slow, camera-facing,
  randomly rotated, rising. Spawn rate 50 matches the Troika steam rate exactly.
- **Reuse** — The whole stack as the base of the smoke-column emitter; `Sphere Location` radius is
  the VtMB `radius` spawn key, `Acceleration Force` Z is `elevation_speed`.

### How to Create a Steam Effect in Niagara (Epic documentation)

- **URL** — https://dev.epicgames.com/documentation/unreal-engine/how-to-create-a-steam-effect-in-niagara-for-unreal-engine?lang=en-US
- **Author / credibility** — Epic Games, official; companion page to the smoke how-to.
- **What it is** — Tutorial: retune the smoke emitter into a directional steam jet.
- **Technique** — Same sprite/sub-UV emitter, duplicated and retuned: `Spawn Rate` **30**;
  Lifetime **3.0–7.0**; colour pinned white (1,1,1); Sprite Size **100–200**; `Add Velocity`
  min (16, −5, 35) max (32, 5, 50) — a narrow ±5 lateral cone with forward+up bias;
  `Sphere Location` radius 20; Particle Update `Acceleration Force` min (25, −10, 15)
  max (55, 10, 25); `Scale Color` set to the **Pulse Out** curve template; `Drag` **0.8** added
  ahead of `Solve Forces and Velocity`.
- **Closeness to VtMB** — Very close to `steamrelease_constant_emitter` / `env_steam`: a
  directional cone (VtMB `phi`/`theta` ±10°, rate 50), long life, white, untinted.
- **Reuse** — These numbers as the starting point for the clinic `env_steam`; the narrow Y range
  is our ±10° cone, `Drag` is the collide-block `drag`, and Pulse Out is the alpha ramp.

### How to Create Particle Effects That Emit Light in Niagara (Epic documentation)

- **URL** — https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-create-particle-effects-that-emit-light-in-niagara-for-unreal-engine
- **Author / credibility** — Epic Games, official.
- **What it is** — Tutorial adding a `Light Renderer` alongside a sprite emitter.
- **Technique** — Second renderer in the Render group: `Light Renderer` with `Radius Scale` 5.0
  and `Color Add` (X=Red, Y=Green, Z=Blue). Also the emissive rule: "if you set a color value
  greater than 1, it becomes emissive color" — how a sprite glows without a light.
- **Closeness to VtMB** — VtMB fire emitters have **no** dynamic light (§3.4 records the absent
  dynamic light; the "surface colour" is only the creator's RGB triple). This is therefore a
  **named modernization**, not a reproduction.
- **Reuse** — One `Light Renderer` on the flame emitter for barrels, torches and molotov, radius
  and colour driven from the same authored RGB the VtMB definition already carries.

### Using Refraction — heat haze from a panned normal map (Epic documentation)

- **URL** — https://dev.epicgames.com/documentation/en-us/unreal-engine/using-refraction?application_version=4.27
- **Author / credibility** — Epic Games, official material documentation.
- **What it is** — Reference plus tutorial on the Refraction input.
- **Technique** — Translucent blend mode; a Lerp between two IOR constants driven by `Fresnel`
  into Refraction; then the two extensions that matter here — plug a **normal map into the Normal
  input** ("By changing the Normal map, you can affect the way the Refraction looks in interesting
  ways"), and add a **Panner** on that normal map's UVs: "While this method might be useful for a
  glass window, it would be very useful for Visual Effects such as heat haze or distortion from a
  huge explosion." Also documents `Refraction Depth Bias`, "a way to prevent closer objects from
  rendering into the distorted surface at acute viewing angles".
- **Closeness to VtMB** — Direct. VtMB's `normal` + `refract` keys draw the batch on
  `engine/particlerefract`, a DUDV card; `Fire_Heat` is exactly a panned-normal refraction sprite.
- **Reuse** — The refraction master for `Fire_Heat` and the other seven `normal`+`refract` leaves;
  `refract` (default 1) becomes the normal-strength scalar; Refraction Depth Bias fixes the card
  cutting into the drum mesh.

### Heat Distortion VFX for Fire in UE5 Niagara — Photoshop & Normal Map workflow (CGHOW)

- **URL** — https://cghow.com/heat-distortion-vfx-for-fire-in-ue5-niagara-photoshop-normal-map-workflow-%F0%9F%94%A5%F0%9F%92%A8%E2%9C%A8/
- **Author / credibility** — Ashif Ali (CGHOW), real-time VFX artist with a 1000+ tutorial Niagara
  catalogue; published 16 December 2025.
- **What it is** — End-to-end tutorial, texture through emitter.
- **Technique** — "I will show you how to create a seamless noise texture in Photoshop", converted
  to a normal map; then "a specialized Refraction Material in UE5 that uses our normal map to warp
  the background pixels dynamically"; then a dedicated **"Distortion Emitter" to layer over
  existing fire or lava effects**, with explicit control of "the intensity and scale of the
  shimmering effect to match different heat sources".
- **Closeness to VtMB** — The layering model matches: VtMB does not distort the flame card, it
  adds `Fire_Heat` as a **fifth leaf** under the same emitter root.
- **Reuse** — Keep heat haze as its own emitter inside the barrel/fire system rather than a
  material feature of the flame; expose one intensity scalar so `barrelfireemitter`,
  `molotov_emitter` and the torches share one distortion emitter at different strengths.

### Niagara Examples Pack — 50+ free Niagara systems (Epic Games, Fab)

- **URL (opened)** — https://80.lv/articles/epic-releases-over-50-free-niagara-systems-for-unreal-engine-5-7
  · pack listing: https://www.fab.com/listings/0e188eca-4e54-4fb2-a9ed-d8b8a565e600
  (Fab returns HTTP 403 to fetchers; the 80 Level article is the source actually opened and quoted)
- **Author / credibility** — Epic Games; reported by Emma Collins, 80 Level, 29 January 2026.
  First-party sample content, free.
- **What it is** — Free sample pack for UE 5.7 plus an Epic Developer Community tutorial course
  (`dev.epicgames.com/community/learning/courses/qXe/unreal-engine-niagara-examples-pack`), one
  chapter of which covers the master material `M_SmokeAndFire_Sprites` used by the pack's
  explosion and smoke assets.
- **Technique** — "more than 50 VFX Niagara systems … explosions, bullet impacts, trails to
  sparks, fire, smoke, mist cards"; authored with "scalability, Effect Types, Niagara Data
  Channels, and Lightweight emitters, plus limiting user parameters and emitter counts"; ships a
  "dedicated gallery gameplay level" that "illustrates the use of each system".
- **Closeness to VtMB** — The fire/smoke/spark systems and the single shared sprite master
  material are structurally what we need; the pack's mist cards are the `ash` / `rainfog` idiom.
- **Reuse** — Read `M_SmokeAndFire_Sprites` as the model for one shared sprite master with
  per-instance parameters; adopt Effect Types and Lightweight emitters as the scalability spine
  for ambient effects on the three working maps.

### Opacity of AlphaComposite (Premultiplied Alpha) — fire flipbook turns white, not transparent

- **URL** — https://forums.unrealengine.com/t/opacity-of-alphacomposite-premultiplied-alpha/2632342
- **Author / credibility** — "Manilloun", 6 August 2025, Epic Developer Community Forums (Asset
  Creation). Unanswered when read — recorded as a documented pitfall, not as a fix.
- **What it is** — Report from someone doing exactly our job: a Niagara fire flipbook on
  AlphaComposite that must fade out over particle lifetime.
- **Technique / failure** — "when I reduce opacity via the Dynamic Parameter, my flipbook textures
  simply turn white instead of becoming more transparent."
- **Closeness to VtMB** — The trap our `mask` translation walks into. Under `src + dst*(1-a)`,
  dropping only opacity leaves `src.rgb` at full strength: the card stops occluding but keeps
  adding light, so it blows out white.
- **Reuse** — Multiply the emissive by the same fade the opacity gets (see the AlphaComposite
  thread above). Our per-particle ramp must scale **both**, which is exactly the VtMB semantics:
  `red/green/blue × color` drives the source colour, `mask` drives the erase term, independently.

### What's the Difference Between a Black and Transparent Background? (Real Time VFX)

- **URL** — https://realtimevfx.com/t/whats-the-difference-between-a-black-and-transparent-background/27398
- **Author / credibility** — realtimevfx.com, the industry VFX forum; answers by **Torbach**,
  a long-standing contributor. Posted 3 September 2024.
- **What it is** — Texture-authoring Q&A on why VFX source art sits on black.
- **Technique** — "for the additive blend mode you don't need Transparency/Alpha and can store
  this RGB 24bit"; "For pre-multiplied blend mode the black is expected, hence the term 'pre'
  multiplied"; and the diagnosis of the black-card artefact: "'black borders' on Alpha blending is
  because the image **was** premultiplied by being on black". The fix for straight alpha blending
  is white/near-white RGB with the shape carried entirely in alpha.
- **Closeness to VtMB** — VtMB's 318-sprite atlas is decoded at gamma 2.2 into exactly this
  premultiplied-on-black form and drawn with `ONE, ONE_MINUS_SRC_ALPHA`, so black backgrounds are
  correct for us and must **not** be "fixed".
- **Reuse** — Import the decoded VtMB sprites as-is, treat them as premultiplied, use
  AlphaComposite. Do not run a straight-alpha unpremultiply on `T_flamemass`, `glowa`, `candle` or
  `furball`; that would break the blend we are reproducing.

### Cigarette smoke trail VFX UE4 question (Real Time VFX)

- **URL** — https://realtimevfx.com/t/cigarette-smoke-trail-vfx-ue4-question/14370
- **Author / credibility** — realtimevfx.com; asked by **jkl320**, answered by **raytheonly**,
  26 August 2020.
- **What it is** — Practitioner thread on the ribbon-versus-sprite choice for a thin smoke trail.
- **Technique** — Ribbon trail, but "instead of a thin gradient use a tileable texture, that looks
  more like smoke", and "You could even use two trails with the same noise a bit offset, so it has
  a more 'dancy' feeling". The asker settles on "ribbons and flow maps in combination with some
  particles".
- **Closeness to VtMB** — Partial, and instructive. VtMB cigar smoke is *not* a ribbon: it is the
  same sprite language attached to a bone (`attach_type 2`), with `parent_speed` making live
  particles follow the moving hand. The thread is the argument for why a ribbon looks better — and
  why we should not take it.
- **Reuse** — Keep the sprite emitter (faithful), but steal the two-offset-layer trick as two
  sprite emitters at slightly different rates; attach to the socket and set the component to
  local space so `parent_speed 1` reads correctly.

### CGHOW — Free Sparks & Embers Pack (Epic Developer Community Forums / Fab)

- **URL** — https://forums.unrealengine.com/t/cghow-sparks-embers/2730482
- **Author / credibility** — Ashif Ali (CGHOW), 22 June 2026. Free pack, distributed on Fab.
- **What it is** — Free Niagara pack with ten worked ember/spark examples.
- **Technique** — "Fully Customizable Niagara Systems" with "User Parameters for Color, Size,
  Speed, Velocity, Lifetime, Spawn Area, and More"; "Parameter-Driven Controls"; "Optimized for
  Real-Time Performance". Included examples: "Blacksmith Forge", "Welding Torch", **"Campfire
  Embers"**, "Grinding Wheel", "Industrial Saw", "Electrical Short Circuit", "Bullet Impact
  Sparks", "Space Capsule Re-entry", "Magical Brazier", "Generic Sparks".
- **Closeness to VtMB** — "Campfire Embers" and "Magical Brazier" are the `FlameEmbers1` leaf of
  `barrelfireemitter` and the torches; "Bullet Impact Sparks" maps onto the impact table (§4.8).
- **Reuse** — The user-parameter surface as the model for our ember emitter (colour, size, speed,
  lifetime, spawn area = the VtMB `radius` / `elevation_speed` / `frames` keys by another name), so
  one ember emitter serves barrel, torch, molotov and forge.

### Designing and Animating a Fireball VFX in Unreal Engine 5 (80 Level)

- **URL** — https://80.lv/articles/how-to-design-and-animate-a-fireball-vfx-in-unreal-engine-5
- **Author / credibility** — VoidFX, real-time VFX artist, interviewed by Gloria Levine for
  80 Level, 3 December 2025. Node- and module-level breakdown.
- **What it is** — Artist breakdown of a sprite-card fire built from a sub-UV flame sheet.
- **Technique** — Master material is "unlit shading model and additive blend mode", in four
  groups: **Distortion** (a noise texture into the main texture's UV socket), **Dissolve** (the
  same noise into opacity, "which makes the animation interesting by removing the main texture
  from the black parts of the noise"), **Main Texture**, **Final Output** — everything "multiplied
  with a particle color node to make the material's color and alpha editable in the Niagara
  System", plus a **Depth Fade** node "to hide seams" and "so that the flames have softer edges
  when they come in contact with a mesh". Fire emitter: `SimpleSpriteBurst` preset, loop Infinite,
  `SpawnBurstInstantaneous` deleted for a `Spawn Rate` with a random range, `Initialize Particle`
  with random lifetime / sprite size / rotation and particle-colour glow of 8.0, `Add Velocity`,
  `Shape Location` (sphere) "to control the width of the flame emission", `SubUVAnimation` with
  `Sub Image Size` 8×6, `Scale Sprite Size` "curve to give the particles a smaller size on the
  spawn & death, and a bigger size in the middle of the lifetime", and `Scale Color` alpha curve
  "to make the particles spawn fading in and die fading out". Sparks add `Curl Noise Force`.
- **Closeness to VtMB** — Structurally the closest single source to `fire1_emitter` /
  `barrelfireemitter`: sub-UV flame card, additive, per-particle colour, size and alpha curves
  over life, separate spark emitter. Missing only the refraction leaf and the `mask` knob.
- **Reuse** — `Scale Sprite Size` and `Scale Color` curves are the home for VtMB's keyframed
  `size` / `width` / `height` and `color` / `mask` ramps in normalized age; Depth Fade for the
  flame card intersecting the oil drum; `Shape Location` sphere radius as `radius`.

### Quake 1's First Level Recreated in Unreal Engine 5: Breakdown (80 Level)

- **URL** — https://80.lv/articles/quake-1-s-first-level-recreated-in-unreal-engine-5-breakdown
- **Author / credibility** — Tatiana Ivanova, Environment/Level Designer, 80 Level, 25 August
  2025. Non-commercial fan reconstruction of a 1996 id Software level in UE 5.5.
- **What it is** — The nearest published analogue to this project: a 1990s BSP shooter level
  rebuilt in UE5 with its sprite-era effects re-authored in Niagara and materials.
- **Technique** — "For visual expressiveness, I added Niagara effects: smoke above lava, splashes,
  and incandescent particles ejected where lava touches surfaces. I also implemented a randomized
  flicker used in torches and bulbs". The flicker is **material-driven, not particle-driven**:
  "a Time node sets the cycle, Noise introduces randomness, and parameters control speed,
  threshold, and brightness. With Step and Lerp, I shape transitions between min/max intensity,
  and Clamp limits the range. Driving Emissive Color produces a pulsing, slightly unpredictable
  light that reads naturally in motion." Atmosphere is Exponential Height Fog plus per-zone local
  Volumetric Fog. Rendering gotcha: "with Temporal/Spatial Samples > 1, Niagara simulations run
  faster due to sub-frame stepping. Setting both sample counts to 1 restored the correct
  simulation speed."
- **Closeness to VtMB** — Same problem class: 2000s sprite/BSP effects rebuilt as Niagara plus
  material animation, with lighting and fog carrying the mood the original faked.
- **Reuse** — Material-driven flicker for torches, `sprites/candle` and the barrel glow instead of
  a per-particle flicker (cheaper, and closer to the VtMB `framerate`-animated sprite card); the
  MRQ sample-count warning for any capture we take of the ambient effects.

## Patterns worth copying

- **`AlphaComposite` is the VtMB blend, not an approximation.** `Final Color = Source Color +
  Dest Color * (1 - Source Opacity)` is the mode-8 shader of §2.4. Build one sprite master on
  AlphaComposite/Unlit and drive `Opacity = tex.a × (mask/255) × ParticleAlpha`; `mask 0` falls
  out as pure additive (`glowa`, the `fire1`/`fire2` "flare" glow), `mask 255` as an occluding
  card (`Smoke1`, `furball`). One material for the whole family.
- **Fade emissive and opacity together, or the card goes white.** The documented AlphaComposite
  failure mode. Our per-particle ramp must scale both, matching VtMB's separate
  `red/green/blue × color` (source) and `mask` (erase) terms.
- **Keep the VtMB sprites premultiplied-on-black.** Black backgrounds are correct for
  premultiplied blending; the "black border" artefact appears only if you alpha-blend a
  premultiplied image. Do not unpremultiply `T_flamemass`, `sprites/candle`, `glowa` or `furball`.
- **Flame cards are Sprite Renderer + `Sub Image Size` + `SubUVAnimation` (Linear).** VtMB's `fps`
  (30) and `frames` map onto frame count and lifetime; `lifetime = frames / fps` becomes
  `Initialize Particle → Lifetime`. Applies to `Flamemass`, `FirePlace_Flames`, `sprites/candle`.
- **VtMB ramps in normalized age are `Scale Color` and `Scale Sprite Size` curves.** Every
  `a,b(n)` keyframe list on `size` / `width` / `height` / `color` / `mask` has a one-to-one home
  there; the `Pulse Out` curve template is the standard spawn-in/die-out alpha shape for smoke
  and steam.
- **Heat haze is a separate emitter, not a flame-material feature.** A translucent
  normal-map + Panner refraction card layered over the fire with one intensity scalar — the
  faithful translation of the `Fire_Heat` fifth leaf and the eight `normal`+`refract` roots. Use
  `Refraction Depth Bias` where the card cuts into the drum or torch mesh.
- **Steam is low rate, long life, narrow cone, white, dragged.** Epic's numbers (rate 30, life
  3–7 s, size 100–200, velocity ±5 lateral with forward+up bias, drag 0.8) are the starting point
  for `steamrelease_constant_emitter` and the clinic `env_steam`; VtMB's rate 50 and `phi`/`theta`
  ±10° slot straight in.
- **`Sphere`/`Shape Location` radius = `radius`; `Acceleration Force` Z = `elevation_speed`;
  `Drag` = the collide-block `drag`.** The spherical-offset motion model of §2.4 has a stock
  Niagara module per key; no scratch-pad module is needed for the fire family.
- **Cigar smoke stays sprites, attached to a bone, in local space.** Ribbons look better but are
  the wrong language for `attach_type 2` + `parent_speed 1`; take only the two-offset-layer trick
  from the ribbon thread.
- **Light and flicker are the named modernizations.** VtMB fire carries no dynamic light: add a
  `Light Renderer` to the barrel/torch/molotov flame, and drive torch and candle flicker from the
  material (Time → Noise → Step/Lerp/Clamp → Emissive) rather than per-particle, recorded beside
  the faithful behaviour per `docs/CLAUDE.md`.
