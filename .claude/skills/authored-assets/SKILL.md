---
name: authored-assets
description: Edit a live-authored Unreal package under Content/ElysiumAuthored — rain, melee weapon trails, hair dynamics, cloth tuning, eye tuning. Use when a task touches those effects or names NS_ElysiumRain, NS_ElysiumMeleeTrail, M_ElysiumRain, M_ElysiumMeleeTrail, MI_ElysiumRain*, T_Rain*, DA_HairDynamics, DA_ClothTuning, DA_EyeTuning, /Game/ElysiumAuthored, or asks to change a Niagara system, material, or tuning data asset the project owns. These have no generator and never come off the export/bake path; they are read and written through the `elysium` editor MCP server.
---

# Live-authored Unreal packages

`Content/ElysiumAuthored/**` (mounted `/Game/ElysiumAuthored/**`, Git LFS) is the one tracked
namespace for original `.uasset` packages. Its residents are **edited live in the editor and saved
in place**. There is no generator, no text source, and no export or bake stage that produces them.

If a task sends you looking for a Python generator or an exporter for one of these, stop — there
isn't one, and adding one is a defect.

## Two lanes, never mixed

Niagara in this project runs on two lanes, and the split is the whole rule (owner call
2026-09-03):

- **Authored.** The hero systems (`NS_ElysiumRain`, `NS_ElysiumMeleeTrail`, `NS_ElysiumDust`,
  `NS_ElysiumSteam`, `NS_ElysiumBeam`), plus the **base emitters and module scripts** under
  `Content/ElysiumAuthored/VFX/Base/` — one base emitter per VtMB leaf archetype, stock Niagara
  modules only, plus the project's own ramp sampler and spherical-offset module scripts. These
  are hand-authored live in the editor, LFS-tracked, and reviewed like code. This skill covers
  them.
- **Generated.** One `NS_<root>` per placed VtMB particle root, written by the bake into
  `Content/ElysiumGenerated/VFX/` (**gitignored**) by `pipeline/unreal/make_particle_systems.py`
  → `UElysiumParticleAssetBuilder`, headless, inheriting the base emitters above and overriding
  parameter values only. **Never hand-edit one**, never open one to "fix" it, never commit one —
  change the generator or the base emitter and re-bake.

So: a Python generator for a base emitter is a defect; a hand edit to an `NS_<root>` under
`ElysiumGenerated` is a defect too, and it will be silently thrown away by the next bake.

## The register, and what is *not* authored

Four of the five effects are split: the **system and its tuning** are authored, the
**game-derived payload** they consume is generated. Change the wrong half and you either lose
work on the next `reconstruct` or put game-derived bytes inside the tracked set.

| Effect | Authored — editor + MCP only | Generated — regenerate, never hand-edit |
|---|---|---|
| Rain | `VFX/NS_ElysiumRain`, `M_ElysiumRain`, `MI_ElysiumRainStreak`, `MI_ElysiumRainMist`, `T_Rain*` | the per-map height-masked material instances bound at runtime through `User.RainStreakMaterial` / `User.RainMistMaterial` (`ElysiumMapActorWeather.cpp`) |
| Melee weapon trail | `VFX/NS_ElysiumMeleeTrail`, `M_ElysiumMeleeTrail` | the wield mesh's `TrailTip` socket, baked with the mesh (`ElysiumMeleeTrail.cpp` drives `User.TrailPointA/B` off it) |
| Hair dynamics | `Hair/DA_HairDynamics` — the chain set and every parameter | the `SK_*` bodies and `SKEL_*` skeletons the chains name |
| Cloth | `Cloth/DA_ClothTuning` — material and solver judgment values | `/ElysiumBaked/Characters/Cloth/CLOTH_<stem>`, built by `pipeline/unreal/make_cloth_assets.py` |
| Eye tuning (R4.5) | `Eyes/DA_EyeTuning` — the corpus-wide `EyeSize`/`EyeShift` baseline | none — no generated payload pairs with it; every rendered eye composes against the same two scalars (`ElysiumEyes::ComposeTuning`) |

The namespace boundary is `Content/CLAUDE.md`.

## Never

- Hand-edit, script, or programmatically rewrite a `.uasset` under `Content/ElysiumAuthored/`.
  It is an opaque LFS binary; the editor is the only writer.
- Add a generator under `pipeline/unreal/` for an authored package, or let any generator or
  `--clean` operation write into or remove `/Game/ElysiumAuthored/**`.
- Run an export or bake to "fix" one of these. Export and bake never touch the authored namespace.
- Copy game-derived bytes, transforms, timings, or references into an authored package — that
  breaks bring-your-own-game, which is the legal posture, not a preference.

## The editing loop

The editor MCP server is named **`elysium`**, not `unreal-mcp` (root `CLAUDE.md` → "Authored
live"). Tool search is on: `list_toolsets`, `describe_toolset` and `call_tool` are the only
advertised tools, and calls run on the game thread — issue them **one at a time, never in
parallel**.

**Gotcha:** `call_tool` takes `toolset_name` and a `tool_name` **stripped of its toolset prefix**.
`describe_toolset` prints fully qualified names, so passing one back verbatim fails with
`Unknown tool`.

```
call_tool(toolset_name="editor_toolset.toolsets.object.ObjectTools",
          tool_name="get_properties",            # not the dotted name describe_toolset printed
          arguments={"instance": {"refPath": "/Game/ElysiumAuthored/Hair/DA_HairDynamics.DA_HairDynamics"},
                     "properties": ["Stems"]})
```

1. **Read before writing.** `ObjectTools.get_properties` returns the live values as JSON; feed the
   same shape back to `set_properties`. `ObjectTools.list_properties` when you do not know the
   schema.
2. **Write**, then **read back** to confirm the values landed.
3. **Save** with `AssetTools.save_assets(["/Game/ElysiumAuthored/..."])`, then confirm the file
   actually reached disk with `git status --short Content/ElysiumAuthored/`. A `set_properties`
   that succeeds only dirties the in-memory package.

Which toolset applies:

- **Data assets** (`DA_HairDynamics`, `DA_ClothTuning`, `DA_EyeTuning`) — `ObjectTools`, as above.
- **Material instances** — `MaterialInstanceTools`, or `ObjectTools` for raw properties.
- **Niagara systems** (`NS_*`) — `NiagaraToolsets.NiagaraToolset_System` through `call_tool`
  (the engine's experimental plugin, enabled in the uproject). Read
  `references/niagara-toolset.md` first: it holds the measured gotchas that cost a session.

## Verifying

Editing an authored asset changes content, not code, so the check is visual: `uv run elysium run
play` (or `run editor`) and look, using the smallest unit that proves the change — one map, one
NPC, one placed model. Do not start a build, a test sweep, an export, or a bake for a
content-only edit. If you also changed C++, the gate is `uv run elysium build` and a focused
`uv run elysium test <filter>`.

An authored-asset commit carries an unreadable LFS binary diff, so the message has to say which
asset, which values, and why.
