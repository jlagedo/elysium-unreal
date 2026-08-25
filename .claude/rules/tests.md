---
paths:
  - "Source/ElysiumUE/Private/Tests/**"
  - "pipeline/tests/**"
---

# Test authoring policy

How to run a tier, how to scope an export, and what a change authorizes are the **`elysium-testing`**
skill. These are the constraints on the test being written, and every one of them fails silently —
the run stays green and the suite is smaller than the source says.

- **A test name is a leaf.** Unreal's automation registry turns a name that is a strict prefix of
  another registered name into a *branch*, and the test registered under it never runs and never
  reports. `IMPLEMENT_COMPLEX_AUTOMATION_TEST` inverts this: its macro name is a branch by design,
  so the violation is a simple test sharing or sitting under it. `uv run elysium doctor` refuses a
  collision and a duplicate.
- **A missing prerequisite abstains; a present but broken one fails.** An abstention is
  `AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: <reason>"))`, which the runner counts apart from an
  execution and which fails a tier that abstained entirely. Never gate on `TestNotNull` — it
  registers a failure and returns — and never write a prose "skipping".
- **Drive time by calling `Tick(dt)` directly.** No latent automation commands, no assertion on
  wall-clock duration, and no dependence on a frame count the engine may schedule differently under
  load. The suite has no flaky test and no quarantine lane to put one in.
- **Randomness comes from a named stream.** `ElysiumRng::Stream(EElysiumRngStream::...)`, never
  `FMath::Rand*`. A test that reseeds states which stream it drives.
- **Assert the invariant, not the census.** A literal corpus count — "all 88 wires" — fires on every
  legitimate corpus change and teaches the next reader to edit the number. Assert that each wire
  resolves, that none dangle, that none duplicate.
- **A probe that misses is a failure or is not a probe.** Resolving an asset by constructed name and
  ignoring the miss buys a passing test and a log full of `LogUObjectGlobals: Failed to find
  object`. Resolve through the manifest that says what should exist, and fail on an absence that
  manifest does not license.
- **A production reader must resolve through the scratch root.** `ElysiumScratchContentRoot.h`
  overrides the `-ElysiumContentRoot` command-line pin as well as the environment variable; without
  it a test reading through `FElysiumContentPaths` reads the real corpus.
