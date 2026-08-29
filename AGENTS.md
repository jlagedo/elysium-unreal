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
- `pipeline/` — Python pipeline; owns build, asset management, VtMB asset decoders and `.uasset` bakes
- `Content/` — tracked authored assets plus generated and baked package mounts.
  `Content/ElysiumAuthored/**` (Git LFS) is edited live and saved in place, and has no generator;
  the `authored-assets` skill owns its register and procedure.
- `Plugins/` — includes ElysiumBaked, a generated `.uasset` mount where only the `.uplugin` is tracked
- `research/` — research cases and tooling
- `docs/` — documentation (project, architecture, vtmb, recovered, operations, openspec)
- `Config/` — Unreal project config

## Rules

- **Work lands as a complete change, not behind a switch or feature flags or cvars**
- Log every unexpected failure once, where it is owned, at warning or worse, naming the operation and the object or input.
