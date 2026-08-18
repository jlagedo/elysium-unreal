# Elysium-Unreal

Elysium-Unreal recreates *Vampire: The Masquerade – Bloodlines* gameplay on Unreal Engine 5.8 using your own copy of the original game as source data.

This is a **reconstruction** workflow, not a port:

- Pipeline converts VtMB source files into engine-neutral intermediates.
- Unreal bakes visual look into local content packages.
- Runtime loads the baked world and reconstructs gameplay systems (entities, scripts, audio, interaction, save/load) from sidecars.

## What it is

- `pipeline/`: Python tooling for export, validation, and package generation.
- `Source/ElysiumUE/`: Unreal runtime and gameplay implementation.
- `docs/`: design contracts, source behavior notes, and status.
- `research/`: reverse-engineering cases and tooling.

## How it works (high level)

1. `ELYSIUM_VTMB_ROOT` is used as read-only source.
2. `UV elysium export` writes intermediates under `ELYSIUM_EXPORT_ROOT`.
3. Native Unreal commandlets bake map look into local packages.
4. At runtime, Unreal opens the baked map and builds entities + logic from exported sidecars.
5. Generated content and game-derived outputs are not committed.

## Requirements

- Windows 64-bit.
- Unreal Engine 5.8.
- Visual Studio (as declared in [`.vsconfig`](.vsconfig)).
- [`uv`](https://docs.astral.sh/uv/) (Python tooling).
- Legal local VtMB installation.
- External work root for generated/intermediate data.
- DX12/SM6 + DXR-capable GPU for rendered play.

## Setup and configuration

Create a local environment file:

```powershell
Copy-Item dev/paths.example.env .elysium.local.env
```

Then edit `.elysium.local.env`:

```text
ELYSIUM_UE_ROOT=D:\Epic\UE_5.8
ELYSIUM_VTMB_ROOT=E:\Games\Vampire The Masquerade - Bloodlines
ELYSIUM_WORK_ROOT=D:\elysium-work
```

Optional override:

```text
ELYSIUM_EXPORT_ROOT=E:\elysium-work\exports
```

Resolution rule:

- CLI arguments override environment.
- Environment overrides `.elysium.local.env`.
- Project paths are never guessed from the checkout.

## Build from zero

Run this once to validate workspace and perform a full rebuild:

```powershell
uv sync --locked
uv run elysium deps sync
uv run elysium doctor
uv run elysium reconstruct --clean --rebuild
```

That path restores dependencies, builds Unreal, exports and bakes configured maps, and runs required checks.

## Main day-to-day commands

```powershell
uv run elysium build
uv run elysium export map sp_tutorial_1
uv run elysium export map sp_tutorial_1 --intermediate-only
uv run elysium test Substrate
uv run elysium run editor
uv run elysium run play sp_tutorial_1
uv run elysium run play gr
uv run elysium debug greenroom
uv run elysium debug modelroom
```

## Useful docs

- Status and task tracking: [docs/project/roadmap.md](docs/project/roadmap.md)
- Core strategy and ownership model: [docs/project/rebuild-strategy.md](docs/project/rebuild-strategy.md)
- Remaster contract: [docs/project/remaster-direction.md](docs/project/remaster-direction.md)
- Runtime architecture: [docs/architecture/](docs/architecture/)
- VtMB factual notes: [docs/vtmb/](docs/vtmb/)
- Repository policy and commands: [docs/operations/repository.md](docs/operations/repository.md)
