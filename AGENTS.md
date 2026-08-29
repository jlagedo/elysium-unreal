# Elysium-Unreal

Elysium-Unreal is *Vampire: The Masquerade – Bloodlines* (VtMB, 2004, early Source engine) rebuilt
as a playable game — **remastered** — on **Unreal Engine 5.8 + C++**.

## Build & run (Windows)

- `.elysium.local.env` at the repository root holds this machine's roots;
- `ELYSIUM_UE_ROOT` — the Unreal Engine 5.8 install.
- `ELYSIUM_VTMB_ROOT` — the VtMB game install; the source corpus, read and never written.
- `ELYSIUM_WORK_ROOT` — the out-of-repo scratch tree for exports, bakes and logs.
- `uv run elysium` is the only public command surface; `uv run elysium --help` is the live list of commands.
- The running game receives `-ElysiumContentRoot` and reads the export corpus from disk.
- `uv run elysium build`, then `uv run elysium test <tier>` — the **`elysium-testing`** skill owns the rest.

## General commands

- `uv run elysium doctor` — repository policy, local paths, dependency ownership, generated prerequisites
- `uv run elysium deps sync | check` — restore and verify locked dependencies
- `uv run elysium build`
- `uv run elysium reconstruct` — rebuild the generated tree from the VtMB install
- `uv run elysium run editor | play`
- `uv run elysium gr` — interactive green room; `--drive` puts the body on the player pawn, `--arena` adds the AI room
- `uv run elysium export grid | all | map | characters | wield | model | prop | material | texture | placed-model | bundle`
- `uv run elysium export_v2 character-glb | characters-glb | texture-glb | textures-glb` — isolated lossless GLB export pipelines (under construction; they will replace `export`)

## Project layout

- `Source/ElysiumUE/` — C++ runtime
- `Source\ElysiumUEAnimGraph` - The editor-only module holding the graph-node faces (title, colour, tooltip) of the project's two custom animation nodes, split out because UAnimGraphNode_Base cannot link into a packaged game.
- `pipeline/` — Python pipeline; owns build, asset management, VtMB asset decoders and `.uasset` bakes
- `research/` — research cases and tooling
- `docs/` — documentation (project, architecture, vtmb, recovered, operations, openspec)

### Content - Unreal

- `Content/` — Main unreal folder
  `Content/ElysiumAuthored/**` - Manual authored assets git tracked
  `Content/ElysiumGenerated/**` - Generated content from pipeline (vtmb based or generated helpers)
- `Plugins/` — assets directly generatedt from VTMB isntall and baked into Unreal Assets.
- `Config/` — Unreal project config

## Rules

- Log every unexpected failure once, where it is owned, at warning or worse, naming the operation and the object or input.
