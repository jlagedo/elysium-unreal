# Effect examples — real Niagara work the reconstruction should copy

Collected 2026-09-02 for the Niagara authoring reset (`docs/project/niagara_authoring_strategy.md`).
Every entry was opened and quoted from the page itself; Fab and ArtStation refuse fetchers, so
pack contents are cited through articles that cover them and the listing URL is given beside the
caveat. Each file ends with a "Patterns worth copying" list keyed to the VtMB definitions by name.

| File | VtMB family (`docs/vtmb/effects.md`) | Examples |
|---|---|---|
| `fire-smoke-steam.md` | §4.2 fire and embers, §4.3 smoke, steam, cigars — `barrelfireemitter`, `fire1/2_emitter`, `molotov_emitter`, candles, `env_steam` | 15 |
| `rain-drips-splashes.md` | §4.4 rain, drips, lightning, wetness — `waterdrops_timer`, `rain_follow_emitter`, `rain_box_*`, `raindrops2` collide → `rainsplash_new` / `RainStain` | 15 |
| `dust-beams-sparks-lights.md` | §4.1 sprite glows, §4.5 dust motes, §4.7 muzzle flash and sparks — `func_dustmotes`, `env_beam`, `airplane_emitter`, `moth_emitter`, `sparks_*`, `muzzleflash_emitter_a/b` | 14 |

Findings that cut across the three files:

- **VtMB's one blend is Unreal's `BLEND_AlphaComposite`**: `dst = src.rgb + dst × (1 − src.a)`,
  with `mask` riding the vertex alpha. The sprite atlas must stay premultiplied on black; a fade
  must scale emissive and opacity together or a card whitens instead of vanishing.
- **Two child mechanisms for two VtMB constructs**: per-impact `collide { spawn {} }` is a
  Niagara Collision Event (CPU only, persistent IDs); per-parent-particle `spawn { rate/burst }`
  is `Spawn Particles from Other Emitter` with an emitter-level Particle Attribute Reader.
- **The collide block maps term for term** onto the stock Collision module (`bounce` →
  Restitution, `friction` → Friction) plus Gravity Force and Drag.
- **`env_beam` is Epic's own Beam stack** (Beam Emitter Setup / Spawn Beam / Beam Width /
  Update Beam + Jitter) with endpoints as user parameters; only the texture scroll is ours.
- **Epic's free Niagara Examples Pack (Fab, UE 5.7)** ships lightning, impacts and footsteps,
  including Data Channel-driven spawning beside the one-system-per-hit form.
- **`RainStain` belongs on the Decal Renderer** (Source Mode = Particles, orientation from the
  collision normal); the Decal Component Renderer is Experimental and warned against.
