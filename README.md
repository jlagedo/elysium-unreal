# Elysium-Unreal

*Vampire: The Masquerade – Bloodlines* rebuilt as a playable game on **Unreal Engine 5.8 +
C++**. The runtime decodes the original game's proprietary formats into engine-neutral
intermediates (offline, in `tools/`) and builds all engine objects in code at map-load time
— no asset baking, no editor content pipeline.

**Status: M0 verified, M1 in progress** — world + lighting + props, the entity/I-O substrate,
brush movers (doors/buttons), signs/popups, decoded audio + sound schemes, an expression and
embedded-CPython scripting host, a glTFRuntime NPC skeletal-mesh spike, and cross-map landmark
travel, all proven on VtMB's tutorial map and several neighbors. Full phase-by-phase status
lives in **[`docs/roadmap.md`](docs/roadmap.md)**; the strategy/design reference is
**[`docs/rebuild-strategy.md`](docs/rebuild-strategy.md)**.

## Bring your own game

**No game content is distributed here.** The pipeline reads *your own VtMB install* and
writes to `tools/out/` (gitignored). The only committed assets are hand-authored and
game-agnostic (an empty boot map + one master material).

## Layout

| Path | What |
|---|---|
| `Source/ElysiumUE/` | the C++ runtime module (loads intermediates, builds the world) + runtime facts (`Source/ElysiumUE/CLAUDE.md`) |
| `tools/` | the offline Python decode/export pipeline (`UE_bsp_to_scene.py`, …) + format docs (`tools/CLAUDE.md`) |
| `docs/` | reverse-engineering reference + the rebuild strategy |
| `Content/` | committed assets only: `Elysium.umap` (boot) + the `M_VtMB_World`/`M_Sky`/`M_Gizmo*` master materials |
| `CLAUDE.md` | project fact sheet + documentation index (start here after this README) |

## Build & run (Windows, UE 5.8)

The `.bat` files pin `UE_ROOT=D:\Epic\UE_5.8` — edit to match your engine install. You must
also run the `tools/` pipeline against your VtMB install to export at least `sp_tutorial_1`.

```
build.bat            # compile the editor target (UnrealBuildTool)
editor.bat           # open in the Unreal editor (Play = PIE)
play.bat [map]       # launch standalone (default: sp_tutorial_1); WASD + mouse to fly
```

See **[`CLAUDE.md`](CLAUDE.md)** for the project fact sheet, the load-bearing rules, and the
documentation index; **[`Source/ElysiumUE/CLAUDE.md`](Source/ElysiumUE/CLAUDE.md)** for
engine/module facts and the runtime types.
