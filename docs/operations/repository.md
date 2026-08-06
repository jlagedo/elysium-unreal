# Repository and workspace policy

The checkout contains authored project source and original project-owned Unreal packages only.
User-owned game data, generated packages, decompilation, dependency source, build output, and
research evidence live outside Git.

## Local paths

`.elysium.local.env` is ignored. Copy `dev/paths.example.env` and set:

- `ELYSIUM_UE_ROOT`;
- `ELYSIUM_VTMB_ROOT`;
- `ELYSIUM_WORK_ROOT`.

The default export root is `$ELYSIUM_WORK_ROOT/exports`. Research output, cache, logs, and
scratch state use sibling directories below the work root. `ELYSIUM_EXPORT_ROOT` may
override the export location. Command parameters override process environment; process
environment overrides the local env file; Unreal may then be safely auto-detected. VtMB
and work roots are never guessed from the repository.

## Git boundary

The repository rejects:

- Unreal packages and containers: `.uasset`, `.umap`, `.ubulk`, `.uexp`, `.uptnl`, `.pak`,
  `.ucas`, `.utoc`, except original project-authored package files below
  `Content/ElysiumAuthored/`;
- raw VtMB containers and assets such as `.bsp`, `.mdl`, `.phy`, `.vpk`, `.tth`,
  `.ttz`, `.vtf`, `.vmt`, `.vcd`, `.dlg`, and `.sav`;
- extracted binaries, game-derived export, and baked content;
- Ghidra projects, decompilation, dumps, extracted game trees, and third-party reference
  source;
- captures, generated reports, logs, backups, scratch files, `.orig`, and
  `.codex-patch/**`;
- downloaded dependency source under `Plugins/External/`.

Git LFS is required for `Content/ElysiumAuthored/**` and is not an exception anywhere else. The
authored namespace may contain original project camera/cinematic assets and other independently
owned work; copying game-derived exports, packages, transforms, timings, or scripts into it is a
policy violation. Generated `/Game/Elysium`, `/Game/VtMB/**`, and `/ElysiumBaked/**` packages keep
their ignored/regenerable contracts.

`uv run elysium doctor` enforces the path boundary and the tracked pre-commit hook runs the
repository-only check. Configure it with:

```powershell
git config core.hooksPath .githooks
```

## Dependencies

`dev/dependencies.lock.json` pins fetched source and artifacts.
`uv run elysium deps sync` populates ignored managed locations, applies the tracked
patches under `dev/dependencies/patches/`, verifies post-patch tree hashes, and writes an
ownership marker. Never place project source in `Plugins/External/`.

## Public command

`uv run elysium` is the sole development entrypoint. It owns dependency synchronization,
repository diagnostics, UE compilation, export and bake orchestration, tests, play,
profiling, probes, screenshots, movement, greenroom, modelroom, research, IDE setup, and
MCP startup. `uv run elysium reconstruct --clean --rebuild` is the clean-checkout path
that restores dependencies, compiles the editor, exports and bakes the corpus, verifies
the products, and runs the required tests. No compatibility wrappers exist.

The focused surfaces are `export map`, `export model`, and `export bundle`. The complete
profiles are `export grid` and `export all`; only those profiles and `reconstruct` accept
`--clean`.

Complete exports record their profile, tool and dependency fingerprints, task
dependencies, expected outputs, and completion state in
`$ELYSIUM_EXPORT_ROOT/.elysium-manifest.json`. Incremental runs reuse only matching
complete tasks whose outputs still exist; `--force` bypasses that cache.

`--clean` validates the work-root ownership marker before deleting only the configured
export corpus, `Content/VtMB/`, `Content/Elysium.umap`, and
`Plugins/ElysiumBaked/Content/`. It immediately writes
`$ELYSIUM_EXPORT_ROOT/.elysium-incomplete`; successful export, package generation, bake,
and verification remove the marker. Repository diagnostics and content tests refuse or skip an
incomplete corpus rather than treating it as valid. Runtime gameplay does not consult the marker;
it loads the generated artifacts that are actually present and reports a missing map or sidecar at
the point of use.
