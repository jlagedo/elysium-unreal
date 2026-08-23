---
name: authored-assets
description: Edit a live-authored Unreal package under Content/ElysiumAuthored — rain, melee weapon trails, hair dynamics, cloth tuning. Use when a task touches those effects or names NS_ElysiumRain, NS_ElysiumMeleeTrail, M_ElysiumRain, M_ElysiumMeleeTrail, MI_ElysiumRain*, T_Rain*, DA_HairDynamics, DA_ClothTuning, /Game/ElysiumAuthored, or asks to change a Niagara system, material, or tuning data asset the project owns. These have no generator and never come off the export/bake path; they are read and written through the `elysium` editor MCP server.
---

# Live-authored Unreal packages

`Content/ElysiumAuthored/**` (mounted `/Game/ElysiumAuthored/**`, Git LFS) is the one tracked
namespace for original `.uasset` packages. Its residents are **edited live in the editor and saved
in place**. There is no generator, no text source, and no export or bake stage that produces them.

If a task sends you looking for a Python generator or an exporter for one of these, stop — there
isn't one, and adding one is a defect.

## The register, and what is *not* authored

Three of the four effects are split: the **system and its tuning** are authored, the
**game-derived payload** they consume is generated. Change the wrong half and you either lose
work on the next `reconstruct` or put game-derived bytes inside the tracked set.

| Effect | Authored — editor + MCP only | Generated — regenerate, never hand-edit |
|---|---|---|
| Rain | `VFX/NS_ElysiumRain`, `M_ElysiumRain`, `MI_ElysiumRainStreak`, `MI_ElysiumRainMist`, `T_Rain*` | the per-map height-masked material instances bound at runtime through `User.RainStreakMaterial` / `User.RainMistMaterial` (`ElysiumMapActorWeather.cpp`) |
| Melee weapon trail | `VFX/NS_ElysiumMeleeTrail`, `M_ElysiumMeleeTrail` | the wield mesh's `TrailTip` socket, baked with the mesh (`ElysiumMeleeTrail.cpp` drives `User.TrailPointA/B` off it) |
| Hair dynamics | `Hair/DA_HairDynamics` — the chain set and every parameter | the `SK_*` bodies and `SKEL_*` skeletons the chains name |
| Cloth | `Cloth/DA_ClothTuning` — material and solver judgment values | `/Game/VtMB/Cloth/CLOTH_<stem>`, built by `pipeline/unreal/make_cloth_assets.py` |

Design and behaviour for these live in `docs/architecture/effects-architecture.md`,
`docs/architecture/animation-architecture.md` §8, and `docs/architecture/bouncy-boobs-dynamics.md`.
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

- **Data assets** (`DA_HairDynamics`, `DA_ClothTuning`) — `ObjectTools`, as above.
- **Material instances** — `MaterialInstanceTools`, or `ObjectTools` for raw properties.
- **Niagara systems** (`NS_*`) — **there is no Niagara toolset.** Graph and emitter edits are the
  owner's, in the editor UI. Read state and report a concrete recommendation; do not claim a
  change you could not make.

## Verifying

Editing an authored asset changes content, not code, so the check is visual: `uv run elysium run
play` (or `run editor`) and look, using the smallest unit that proves the change — one map, one
NPC, one placed model. Do not start a build, a test sweep, an export, or a bake for a
content-only edit. If you also changed C++, the gate is `uv run elysium build` and a focused
`uv run elysium test <filter>`.

An authored-asset commit carries an unreadable LFS binary diff, so the message has to say which
asset, which values, and why.
