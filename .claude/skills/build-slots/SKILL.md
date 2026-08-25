---
name: build-slots
description: Assign, provision or diagnose an Unreal build slot for a concurrent checkout. Use when creating an agent task worktree, when `uv run elysium build` blocks on the UnrealBuildTool mutex, when two checkouts need to build at the same time, or when a task names `worktree create`, `--ue-root`, `--build-jobs`, `MaxParallelActions`, `_agent<N>`, or a copied engine installation that fails at its first build.
---

# Build slots: one engine install per concurrent checkout

UnrealBuildTool takes a global single-instance mutex named from **its own assembly path**, so an
engine installation builds one thing at a time and two checkouts sharing one installation wait for
each other. When the mutex is held, wait for it to release and retry; **never kill the process
holding it.**

Concurrency therefore comes from **build slots**: one complete engine installation per checkout
that may build at the same time. The machine carries the primary installation plus sibling copies
named with an `_agent<N>` suffix; each checkout names its own in `.elysium.local.env`, and
`uv run elysium worktree status` reports which slot a task holds.

## Assigning a slot

- `worktree create --ue-root` assigns a slot and **refuses an installation another live checkout
  already claims**. Omitting `--ue-root` inherits the primary's slot and warns, because those
  builds then serialize.
- `--build-jobs` writes `MaxParallelActions` into the checkout's own
  `Saved/UnrealBuildTool/BuildConfiguration.xml`, which is the only scope that works: an installed
  engine ignores its own `Engine/Saved` configuration, and the machine-wide `%APPDATA%` one is
  shared by every slot. Size slots so their actions sum to roughly the logical core count.
- Each checkout also gets its own UnrealBuildTool log, accelerator trace, and accelerator port.
  Those default to one machine-wide file and a fixed port that every slot resolves identically, and
  concurrent builds otherwise die racing the same log before reaching a compiler.
- `worktree create` materializes the locked dependencies, so a new slot builds without a separate
  setup step. Downloaded archives are addressed by their own hash and live in the **primary work
  root's cache**, shared by every checkout, so a slot never re-fetches what the machine already has.

## What a secondary slot may do

**A secondary slot builds and runs focused automation tests. Export, bake, editor, play, debug,
MCP, and authored-asset work run from the primary checkout**, which resolves to the primary
installation — the export corpus, the baked `/ElysiumBaked` mount, and the warm derived-data cache
all belong to it. `assert_primary_operation` enforces the checkout half, and configuration
resolution enforces the installation half.

## Provisioning trap

Provisioning a slot copies the primary installation whole. **Directory exclusions must be
path-anchored**: excluding `Intermediate`, `Saved`, or `DerivedDataCache` by name also removes the
engine source modules that carry those names and the precompiled UnrealBuildTool rules assembly an
installed engine refuses to regenerate, producing an installation that looks complete and fails at
its first build. `validate_engine_root` rejects an installed engine whose rules assembly is
missing, so a bad slot fails at `worktree create` rather than mid-build.
