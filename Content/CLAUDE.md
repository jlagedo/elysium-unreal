# Committed content (`Content/`)

**Nothing game-sourced is committed.** Every asset here is hand-authored or freely-licensed and
game-agnostic; converted VtMB content lives only in the gitignored `tools/out/` (see the repo-root
`CLAUDE.md` → "Bring-your-own-game"). The whole list:

| Asset | Role |
|---|---|
| `Elysium.umap` | the empty boot persistent level |
| `VtMB/Materials/M_World_Opaque.uasset` | the opaque world master — albedo + alpha-masked `$selfillum` emissive + `$bumpmap` normal + `$envmap`→Lumen roughness + WorldVertexTransition `basetexture2` blend; usable with ISMs. The common surface case |
| `VtMB/Materials/M_World_Masked.uasset` | the `$alphatest` scissor variant — same graph, `BLEND_Masked`, two-sided (fences, grates, foliage cards) |
| `VtMB/Materials/M_World_Translucent.uasset` | the `$translucent` variant — same graph, `BLEND_Translucent`, per-pixel lit (glass) |
| `VtMB/Materials/M_Additive.uasset` | the `$additive` glow-overlay master — unlit, `BLEND_Additive`, `Albedo`→Emissive (light-fixture "on" panes, neon) |
| `VtMB/Materials/M_Sky.uasset` | the 2D-skybox cube master material (samples a runtime `UTextureCube` by view direction) |
| `VtMB/Materials/M_Decal.uasset` | the 7.2 deferred-decal master — `MD_DeferredDecal` + `BLEND_Translucent`, `Albedo` RGB→BaseColor + A→Opacity, alpha-masked `$selfillum` emissive, samples at `(U, 1-V)` (VtMB V is top-down); the bake instances it once per decal material and hangs those on the level's `ADecalActor`s |

The four surface masters (`M_World_*`, `M_Additive`) additionally carry **Source's distance fog as
a per-primitive term** (`tools/mat_fog.py`): `f = saturate((PixelDepth − FogStart) · FogInvRange)`
read from **Custom Primitive Data** slots 0–5, fading BaseColor/Specular/Emissive and adding the
fog's own colour. It exists because the world and the 3D-skybox miniature carry two different fogs
and share screen depth, which no engine-side fog mechanism can separate — see
`Source/ElysiumUE/Public/ElysiumFog.h`. Unwritten custom data reads as zero, which is "not fogged",
so the term is neutral on any primitive nobody stamped. `M_Decal` carries the same term from named
parameters instead (a `UDecalComponent` is a `USceneComponent` and has no custom primitive data);
`M_Sky` carries none at all, because the backdrop is never fogged (RE-A9).
| `VtMB/Materials/M_Gizmo.uasset` + `M_Gizmo_XRay.uasset` | the entity-gizmo ISM masters — unlit, two-sided, translucent, colour + opacity from per-instance custom data; `_XRay` disables the depth test |
| `Fonts/*.ttf` (+ `Fonts/OFL-*.txt`) | the sign/popup typeface set — loose SIL OFL 1.1 TTFs read verbatim off disk at draw time, not `.uasset`s |

## Fonts (`Content/Fonts/`)

The `game_sign`/popup panel's vector type. VtMB's authored face names map onto five freely-licensed
faces (each redistributable under SIL OFL 1.1, whose text ships alongside as `OFL-<family>.txt`):

| VtMB face | File | Role |
|---|---|---|
| ParagraphText / Trebuchet / Tahoma / Default | `IBMPlexSans-VF.ttf` | body + UI sans (variable; default instance = Regular) |
| Newsprint | `ZillaSlab-Regular.ttf` | editorial slab |
| Headline / Copperplate | `ZillaSlab-SemiBold.ttf` | headlines |
| Vamp_Handwriting1 | `Caveat-Regular.ttf` | handwritten notes |
| MainMenu | `PirataOne-Regular.ttf` | gothic masthead |

They are loose TTFs (not imported `UFont` assets): the runtime reads them off disk at map/draw
time via `FElysiumContentPaths::FontFile`, wraps each in a runtime-cached `UFont`, and hands it to
the sign panel as a `FSlateFontInfo` (`Source/ElysiumUE/Private/ElysiumSignFonts.{h,cpp}`,
`AElysiumHUD::DrawSignPanel`). The face→file map and per-face sizes live in `ElysiumSignFonts.cpp`.

## Why these are committed at all

A UMaterial shading graph and a `.umap` can only be compiled by the **editor** — the
standalone export cannot produce them. They are the accepted exception to "build everything in
code": authored offline by generators under `tools/`, then committed.

## Regenerating

Never hand-edit these in the editor and commit the result — edit the generator instead.
`content.bat` → `tools/build_content.py` rebuilds all of them in one headless editor session,
in dependency order:

1. `make_world_materials.py` — `M_World_Opaque` + `_Masked` + `_Translucent` + `M_Additive` (one shared graph; deletes the old `M_VtMB_World`)
2. `make_sky_material.py` — `M_Sky`
3. `make_gizmo_material.py` — `M_Gizmo` + `M_Gizmo_XRay`
4. `make_decal_material.py` — `M_Decal`
5. `make_boot_map.py` — `Elysium.umap`

Every generator is idempotent and also runnable standalone as a `-script`.
`python tools/export_all.py` invokes the umbrella at the end of a run (skip with
`--no-content`), so a generator cannot be forgotten and silently go stale.

**Registering a new committed asset** = add its generator filename to `GENERATORS` in
`tools/build_content.py`, and add a row to the table above.

The `Fonts/` TTFs are the exception: they are static freely-licensed files, not editor-built
`.uasset`s, so they have no generator and `content.bat` does not touch them. They are committed
once and only change when a face is added or replaced by hand.

`M_Water` (Single Layer Water) is the one world-surface master still unbuilt — see `docs/roadmap.md`
7.3. The `$envmap` reflection channel authored into `M_World_Opaque` is a Lumen roughness path, not a
baked-cube sample; 7.5 tunes it.
