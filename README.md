# Elysium-Unreal

*Vampire: The Masquerade – Bloodlines* rebuilt as a playable remaster on Unreal
Engine 5.8 and C++.

The repository contains the runtime, the offline decode/export pipeline, reproducible
research tooling, and project documentation. It contains no VtMB files, decompilation
results, generated Unreal packages, or downloaded plugin source. The pipeline reads the
user's own install and writes engine-neutral intermediates outside the checkout. Unreal
opens locally baked levels and reads those intermediates at runtime; it never invokes the
offline Python pipeline during play.

Project priority and roll-up status live in
[the master roadmap](docs/project/roadmap.md); detailed retail-capture and
original-runtime animation/facial research status lives in its declared
[scoped tracker](docs/project/retail-capture-roadmap.md). The governing strategy and
remaster boundaries are
[rebuild-strategy.md](docs/project/rebuild-strategy.md) and
[remaster-direction.md](docs/project/remaster-direction.md).

## Repository boundaries

| Path | Ownership |
|---|---|
| `Source/`, `Config/` | Unreal runtime source and configuration |
| `Content/Fonts/` | Licensed loose source fonts |
| `Content/VtMB/`, `Content/Elysium.umap` | Generated local `/Game` packages; ignored |
| `Plugins/ElysiumBaked/Content/` | Generated local `/ElysiumBaked` packages; ignored |
| `Plugins/External/` | Bootstrap-managed plugin source; ignored |
| `pipeline/` | Offline formats, exporters, processors, validation, and editor-only generators |
| `research/` | Reproducible probes, capture source, Ghidra automation, and hash-pinned cases |
| `docs/` | Project, architecture, VtMB facts, recovered facts, and operations |
| `dev/` | Dependency locks and patches plus repository policy support |

Game-derived exports, research evidence, caches, logs, and scratch state live under
`ELYSIUM_WORK_ROOT`, outside this repository.

## Local setup

Copy `dev/paths.example.env` to `.elysium.local.env` and configure:

```text
ELYSIUM_UE_ROOT=D:\Epic\UE_5.8
ELYSIUM_VTMB_ROOT=E:\Games\Vampire The Masquerade - Bloodlines
ELYSIUM_WORK_ROOT=E:\elysium-work
```

Then use the single command surface:

```powershell
uv run elysium deps sync
uv run elysium doctor
uv run elysium build
uv run elysium export grid
uv run elysium export map sp_tutorial_1
uv run elysium test Substrate
uv run elysium run play sp_tutorial_1
uv run elysium reconstruct --clean --rebuild
```

Use `uv run elysium debug` for profiling, probes, screenshots, movement, greenroom, and
modelroom harnesses. Research, IDE, and MCP integration are available through `research`,
`ide vscode`, and `mcp`. See [repository operations](docs/operations/repository.md) for
the complete contract.

For a clean standalone game launch, use `play.bat [map]`. It reads the same local path
configuration, supplies the export corpus and DX12, opens Unreal's live log console, and does not
enable a development harness.
