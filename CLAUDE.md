# Elysium-Unreal

Elysium-Unreal is *Vampire: The Masquerade – Bloodlines* (VtMB, 2004, early Source engine) rebuilt
as a playable game — **modernized** — on **Unreal Engine 5.8 + C++**.

## Build & run (Windows)

- `.elysium.local.env` at the repository root holds this machine's roots;
- `ELYSIUM_UE_ROOT` — the Unreal Engine 5.8 install.
- `ELYSIUM_VTMB_ROOT` — the VtMB game install; the source corpus, read and never written.
- `ELYSIUM_WORK_ROOT` — the out-of-repo scratch tree for exports, bakes and logs.
- `uv run elysium` is the only public command surface;
- The running game receives `-ElysiumContentRoot` and reads the export corpus from disk.

## Project layout

- `Source/ElysiumUE/` — C++ runtime
- `Source\ElysiumUEAnimGraph` - The editor-only module holding the graph-node faces (title, colour, tooltip) of the project's two custom animation nodes, split out because UAnimGraphNode_Base cannot link into a packaged game.
- `pipeline/` — Python pipeline; owns build, asset management, VtMB asset decoders and `.uasset` bakes

### Content - Unreal

- `Content/` — Main unreal folder
  `Content/ElysiumAuthored/**` - Manual authored assets git tracked
  `Content/ElysiumGenerated/**` - Generated content from pipeline (vtmb based or generated helpers)
- `Plugins/` — assets directly generated from VTMB install and baked into Unreal Assets.

## Project rules

- Save game files are disposable, we have not released and don't try to migrate or keep compatibility
