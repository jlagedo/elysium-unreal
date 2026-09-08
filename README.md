# Elysium-Unreal

Elysium-Unreal is a research project that reconstructs *Vampire: The Masquerade – Bloodlines*
(2004) gameplay on Unreal Engine 5.8, reading your own installed copy of the original game as
source data.

It exists to study how the retail game actually works and to see how far a modern engine can be
driven from recovered behavior. It ships no game content: assets, scripts and maps are read from
your installation at build time and never leave your machine. See [`NOTICE.md`](NOTICE.md).

This is a **reconstruction** workflow, not a port:

- Pipeline converts VtMB source files into engine-neutral intermediates.
- Unreal bakes visual look into local content packages.
- Runtime loads the baked world and reconstructs gameplay systems (entities, scripts, audio,
  interaction, save/load) from those intermediates.

## What it is

- `pipeline/`: Python tooling for export, import, bake, validation, and package generation.
- `Source/ElysiumUE/`: Unreal runtime and gameplay implementation.
- `Source/ElysiumUEAnimGraph/`: editor-only faces of the two custom animation nodes.
- `docs/`: vision, seam contracts, recovered retail notes, and open specs.
- `research/`: reverse-engineering cases and tooling.

## How it works (high level)

1. `ELYSIUM_VTMB_ROOT` is used as read-only source.
2. `uv run elysium export` writes intermediates under `ELYSIUM_EXPORT_ROOT` (defaults to
   `$ELYSIUM_WORK_ROOT/exports`). Lossless GLB units go under `ELYSIUM_EXPORT_V2_ROOT`
   (defaults to `$ELYSIUM_WORK_ROOT/exports_v2`).
3. Native Unreal commandlets bake map look into local packages.
4. At runtime, Unreal opens the baked map and builds entities + logic from the exported
   intermediates. The running game is given `-ElysiumContentRoot`.
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

Optional overrides:

```text
ELYSIUM_EXPORT_ROOT=D:\elysium-work\exports
ELYSIUM_EXPORT_V2_ROOT=D:\elysium-work\exports_v2
```

Resolution rule:

- CLI arguments override environment.
- Environment overrides `.elysium.local.env`.
- Project paths are never guessed from the checkout.

## Build from zero

Run this once to validate the workspace and rebuild the generated tree from the install:

```powershell
uv sync --locked
uv run elysium deps sync
uv run elysium doctor
uv run elysium reconstruct --rebuild
```

That path restores dependencies, rebuilds Unreal, and re-exports every map from a clean mount.
`reconstruct` always cleans and always forces; verifying the result is a separate
`uv run elysium test` run.

## Main day-to-day commands

```powershell
uv run elysium build
uv run elysium export map sp_tutorial_1
uv run elysium export map sp_tutorial_1 --intermediate-only
uv run elysium bake map --maps sp_tutorial_1
uv run elysium test substrate
uv run elysium test policy
uv run pytest
uv run elysium run editor
uv run elysium run play
uv run elysium run play sp_tutorial_1
uv run elysium run play gr
uv run elysium debug greenroom
uv run elysium debug modelroom
```

`run play` with no map boots to the menu. `run play gr` is the green room (one body, live).

## How this was built

Every line of code in this repository was written by AI agents. The author's contribution is
direction and review: goals, architecture, acceptance criteria, and the research questions the
agents were pointed at. No source file here was hand-written.

Two consequences worth stating plainly:

- Purely AI-generated work may not attract copyright in most jurisdictions, so the MIT grant in
  [`LICENSE`](LICENSE) may have limited force over much of this codebase. It stands as a statement
  of intent: use it freely.
- Read the code before you trust it, as you would any generated artifact.

## Legal

No game content is distributed here, and nothing in the pipeline circumvents copy protection.
[`NOTICE.md`](NOTICE.md) states the project's position on affiliation, trademarks, game content,
reverse engineering and contributions in full.

## Useful docs

- Vision: [docs/vision.md](docs/vision.md)
- Open specs: [docs/specs/](docs/specs/)
- Seam contracts: [docs/contracts/](docs/contracts/)
- Recovered retail facts: [docs/vtmb/](docs/vtmb/)
