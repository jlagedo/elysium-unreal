# Repository and workspace policy

The checkout contains authored project source only. User-owned game data, generated
packages, decompilation, dependency source, build output, and research evidence live
outside Git.

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

- Unreal packages and containers: `.uasset`, `.umap`, `.ubulk`, `.uexp`, `.uptnl`,
  `.pak`, `.ucas`, `.utoc`;
- raw VtMB containers and assets such as `.bsp`, `.mdl`, `.phy`, `.vpk`, `.tth`,
  `.ttz`, `.vtf`, `.vmt`, `.vcd`, `.dlg`, and `.sav`;
- extracted binaries, game-derived export, and baked content;
- Ghidra projects, decompilation, dumps, extracted game trees, and third-party reference
  source;
- captures, generated reports, logs, backups, scratch files, `.orig`, and
  `.codex-patch/**`;
- downloaded dependency source under `Plugins/External/`.

Git LFS is not an exception. `dev/elysium.ps1 doctor` enforces the boundary and the
tracked pre-commit hook runs the repository-only check. Configure it with:

```powershell
git config core.hooksPath .githooks
```

## Dependencies

`dev/dependencies.lock.json` pins fetched source and artifacts.
`dev/elysium.ps1 bootstrap` populates ignored managed locations, applies the tracked
patches under `dev/dependencies/patches/`, verifies post-patch tree hashes, and writes an
ownership marker. Never place project source in `Plugins/External/`.

## Public command

`dev/elysium.ps1` is the sole development entrypoint. It owns bootstrap, doctor, build,
content, export, bake, tests, play, profiling, probes, screenshots, movement, greenroom,
modelroom, research, IDE setup, and MCP startup. No compatibility wrappers exist.
