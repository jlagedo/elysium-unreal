# Elysium-Unreal

*Vampire: The Masquerade – Bloodlines* rebuilt as a playable game on **Unreal Engine 5.8 +
C++**. The runtime decodes the original game's proprietary formats into engine-neutral
intermediates (offline, in `tools/`) and builds all engine objects in code at map-load time
— no asset baking, no editor content pipeline.

**Status: M0** — world + 3D-skybox geometry, textures, trimesh collision, a free-fly pawn,
map switching, and a debug HUD, proven on VtMB's tutorial map. The full plan (parity with a
prior prototype, then the game layer: entities, I/O, scripting, audio, dialogue) lives in
**[`docs/rebuild-strategy.md`](docs/rebuild-strategy.md)**.

## Bring your own game

**No game content is distributed here.** The pipeline reads *your own VtMB install* and
writes to `tools/out/` (gitignored). The only committed assets are hand-authored and
game-agnostic (an empty boot map + one master material).

## Layout

| Path | What |
|---|---|
| `Source/ElysiumUE/` | the C++ runtime module (loads intermediates, builds the world) |
| `tools/` | the offline Python decode/export pipeline (`bsp_to_scene.py`, …) + format docs (`tools/CLAUDE.md`) |
| `docs/` | reverse-engineering reference + the rebuild strategy |
| `Content/` | committed assets only: `Elysium.umap` (boot), `VtMB/Materials/M_VtMB_World` |
| `CLAUDE.md` | project fact sheet + documentation map (start here after this README) |

## Build & run (Windows, UE 5.8)

The `.bat` files pin `UE_ROOT=D:\Epic\UE_5.8` — edit to match your engine install. You must
also run the `tools/` pipeline against your VtMB install to export at least `sp_tutorial_1`.

```
build.bat            # compile the editor target (UnrealBuildTool)
editor.bat           # open in the Unreal editor (Play = PIE)
play.bat [map]       # launch standalone (default: sp_tutorial_1); WASD + mouse to fly
```

See **[`CLAUDE.md`](CLAUDE.md)** for engine/module facts, the runtime types, coordinate
conventions, and the full documentation map.
