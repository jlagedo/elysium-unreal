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

Task worktrees and QA lanes share Git objects, Git LFS storage, the UE installation, and the
read-only VtMB installation. They never share `$ELYSIUM_EXPORT_ROOT`, `Binaries`, `Intermediate`,
`Saved`, generated or baked `Content`, `.venv`, `Plugins/External`, or a mutable checkout. A task
worktree is mutable development state; a QA lane is an immutable candidate and the two lifecycles
are not interchangeable.

## Parallel QA lanes

`uv run elysium lane` owns persistent detached Git worktrees used for export, bake,
automation, and live acceptance while development continues on `main`. A lane has two
independent roots:

- a sibling checkout, defaulting to `<development-checkout>-<lane>`;
- `$ELYSIUM_WORK_ROOT/lanes/<lane>`, containing that checkout's export corpus, reports,
  logs, caches, and lane record.

`lane create <name> [--at <ref>]` creates the detached worktree, writes its ignored
`.elysium.local.env`, adopts its dedicated `work/exports` root, and records the exact
candidate commit. It does not copy mutable generated packages from another checkout.
Run `deps sync` and a real `build` in the new worktree before exporting or launching it.
Creation and dispatch reject a commit that predates the lane runtime, because commands from
that checkout could not participate in its ownership lease.

`lane dispatch <name> [--at <ref>]` advances an existing clean lane to another exact
commit and resets its automated and live evidence to `pending`. Dispatch refuses a dirty
worktree, a different Git object store, an active lane command, or an Unreal process with
that lane's project open. Ignored generated output survives the detached switch, preserving
that lane's incremental export and bake state.

`lane status [<name>]` reports source/candidate agreement, dirtiness, the active owner,
disk-build readiness, corpus completeness, baked-map count, and the two evidence states.
`lane mark [<name>] --automated pending|passed|failed --live pending|passed|failed` records
the owner verdict and a snapshot of the commit, editor module, export manifest, and promoted
bake receipts. Marking records evidence; it does not run or reinterpret a test.
`passed` requires a successful managed automation/verification or play/debug run for the
same candidate, so a label cannot outrun its evidence.
Any later dependency, build, export, verification, automation, editor, play, or debug
activity resets the evidence domains it can invalidate before it starts.

Build, dependency restore, project-file generation, export, bake, verification, automation,
editor, play, and debug commands take one OS-released lease below the resolved export root.
The lease serializes all mutable activity within one lane and reports its command and PID;
different export roots remain independent. Directly launched Unreal processes are also
detected before managed activity starts.

Git objects and Git LFS storage, the UE installation, and the read-only VtMB installation
are shared. `$ELYSIUM_EXPORT_ROOT`, `Plugins/ElysiumBaked/Content`, generated `Content/`,
`Binaries`, `Intermediate`, `Saved`, `.venv`, and `Plugins/External` remain lane-local. Never
hardlink or junction those mutable trees between lanes.

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
profiling, probes, screenshots, movement, greenroom, modelroom, QA lanes, research, IDE
setup, task worktrees, and MCP startup. `uv run elysium reconstruct --clean --rebuild` is the clean-checkout path
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
`$ELYSIUM_WORK_ROOT/reports/tests/`; the generated corpus is an input to a content test, not
the home of its result.
