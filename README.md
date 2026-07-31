# Elysium-Unreal

*Vampire: The Masquerade – Bloodlines* rebuilt as a playable remaster on Unreal
Engine 5.8 and C++.

The repository contains the runtime, the offline decode/export pipeline, reproducible
research tooling, and project documentation. It contains no VtMB files, decompilation
results, generated Unreal packages, or downloaded plugin source. The pipeline reads the
user's own install and writes engine-neutral intermediates outside the checkout. Unreal
opens locally baked levels and reads those intermediates at runtime; it never invokes the
offline Python pipeline during play.

Current status lives only in [the roadmap](docs/project/roadmap.md). The governing
strategy and remaster boundaries are
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
| `dev/` | The public command, dependency lock, patches, bootstrap, and repository policy |

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
dev/elysium.ps1 bootstrap
dev/elysium.ps1 doctor
dev/elysium.ps1 build
dev/elysium.ps1 content
dev/elysium.ps1 export sp_tutorial_1
dev/elysium.ps1 bake sp_tutorial_1
dev/elysium.ps1 test Substrate
dev/elysium.ps1 play sp_tutorial_1
```

Run `dev/elysium.ps1` with `profile`, `probe`, `shots`, `move`, `greenroom`,
`modelroom`, `research`, `ide vscode`, or `mcp` for the corresponding development
surface. See [repository operations](docs/operations/repository.md) for the complete
contract.
