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

## Parallel task worktrees

`uv run elysium worktree` owns mutable detached worktrees for implementation tasks delegated to
Claude or Codex. The primary checkout remains on `main`; each parallel agent receives one task
worktree and works only below that returned path. Task worktrees do not introduce feature branches
or pull requests. Their clean detached-HEAD commits are reviewed and cherry-picked directly onto
`main` by the coordinating agent.

`worktree create <name> [--at <ref>]` runs only from the primary `main` checkout. It creates:

- a sibling checkout, defaulting to `<primary-checkout>-task-<name>`;
- `$ELYSIUM_WORK_ROOT/worktrees/<name>`, containing that task's export root, logs, caches, and
  ownership record;
- an ignored `.elysium.local.env` and `.elysium-task-worktree.json` marker that bind the checkout
  to those exact roots.

The starting ref is a committed snapshot. Uncommitted changes in `main` are reported but are never
copied into the task checkout. Creation rejects refs that predate the task-worktree runtime. In the
new checkout, run `uv run elysium deps sync` before the first build. The agent may run ordinary
incremental `uv run elysium build`, focused Python tests, and focused `uv run elysium test <filter>`
within the Fast QA contract. Builds sharing one Unreal installation may serialize behind Unreal's
own build mutex; a waiting build is not permission to bypass that mutex or launch retries.

Task worktrees are code-and-test environments. `reconstruct`, every `export` command, `run
editor|play`, every `debug` harness, `gr`, and `mcp` are primary-checkout-only and fail before
launch when invoked from a registered task worktree. This keeps authoritative generated assets,
editor and Live Coding state, interactive play, and visual acceptance on `main`. Verification and
focused automation may read the task's isolated products, but no ignored package or export product
is transferred back to `main`.

`worktree status [<name>] [--json]` reports the base and current commits, dirtiness, generated-state
owner, and commits whose patches are not yet present on `main`. The task agent finishes by making a
clean detached-HEAD commit and returning its hash and validation evidence. After the coordinator
cherry-picks and validates it on `main`, `worktree close <name>` removes the checkout and its
regenerable work root. Close refuses an active Unreal process, a busy or dirty checkout, merge
commits, a changed ancestry, or any commit whose patch Git cannot find on `main`.

Task worktrees share Git objects, Git LFS storage, the read-only VtMB installation, and the
downloaded-dependency cache. They never share `$ELYSIUM_EXPORT_ROOT`, `Binaries`, `Intermediate`,
`Saved`, generated or baked `Content`, `.venv`, `Plugins/External`, a mutable checkout, or — for
any two that build at the same time — an Unreal installation.

## Build slots

UnrealBuildTool takes a global single-instance mutex named from its own assembly path, so one
Unreal installation builds one thing at a time and two checkouts sharing it wait on each other.
Concurrency comes from **build slots**: one complete installation per checkout that may build
concurrently. The machine carries the primary installation and sibling copies suffixed `_agent<N>`.

`worktree create --ue-root <install>` assigns a slot, refuses one another live checkout already
claims, and warns when a checkout inherits the primary's and therefore serializes.
`--build-jobs N` writes `MaxParallelActions` into that checkout's own
`Saved/UnrealBuildTool/BuildConfiguration.xml` — the only scope that binds, because an installed
engine skips its own `Engine/Saved` configuration and the `%APPDATA%` one is shared by every slot.
Size the slots so their actions sum to roughly the logical core count. `worktree status` reports
which slot each task holds.

Each checkout also receives its own UnrealBuildTool log, accelerator trace, and accelerator port.
All three otherwise default to a single machine-wide file or a fixed port that every installation
resolves identically, and concurrent builds die racing the same log before reaching a compiler.

Provisioning a slot copies the primary installation whole. **Directory exclusions must be
path-anchored**: excluding `Intermediate`, `Saved`, or `DerivedDataCache` by name also strips the
engine source modules carrying those names and the precompiled UnrealBuildTool rules assembly an
installed engine refuses to regenerate, producing an installation that inspects clean and fails on
its first build. `validate_engine_root` rejects such a copy at `worktree create`.

A secondary slot builds and runs focused automation tests. Export, bake, editor, play, debug, MCP,
and authored-asset work run from the primary checkout, which resolves to the primary installation —
the export corpus, the baked mount, and the warm derived-data cache all belong to it.

Build, dependency restore, project-file generation, export, bake, verification, automation,
editor, play, and debug commands take one OS-released lease below the resolved export root. The
lease serializes all mutable activity within one checkout and reports its command and PID;
different export roots remain independent. Directly launched Unreal processes are also detected
before managed activity starts, matched on the checkout's own project path.

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
profiling, probes, screenshots, movement, greenroom, modelroom, research, IDE
setup, task worktrees and their build slots, and MCP startup. `uv run elysium reconstruct --clean --rebuild` is the clean-checkout path
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

Automation JSON and HTML are retained per run below
`$ELYSIUM_WORK_ROOT/reports/tests/`, capped at the newest fifty runs; the generated corpus is an
input to a content test, not the home of its result. The durable record of a run is its journal
under `$ELYSIUM_WORK_ROOT/logs/`, not the report directory.
