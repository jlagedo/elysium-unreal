---
name: elysium-testing
description: Run, scope or write tests for this project — the C++ automation tiers (Substrate, Policy, Content) and the Python unittest suite — and decide how much export/bake work a change authorizes. Use when a task asks to test, verify or validate a change, names `uv run elysium test`, an `Elysium.Substrate.*`/`Elysium.Policy.*` filter, `ElysiumTestServices.h`, a new automation test, or a Python test under `pipeline/tests/`.
---

# Testing and QA scope

## C++ automation

Build with `uv run elysium build`. Test with `uv run elysium test <tier>`.

| Tier | Needs | Covers |
|---|---|---|
| `Substrate` | nothing | the substrate, scripting, session, player and UI layers, against the recording doubles in `Private/Tests/ElysiumTestServices.h` |
| `Policy` | a generated `/Game` package | material masters, declared input assets, audio routing — checks that need a real asset graph but not the user's game |
| `Content` | `$ELYSIUM_EXPORT_ROOT` and the baked mount | the export corpus and what the bake wrote from it |

A bare word that is not one of the three is refused rather than run: `Automation RunTest` matches
by substring and reports success for a selection that matched nothing, so an unrecognized tier
would otherwise be a green run of zero tests. A fully qualified filter (anything containing a dot)
is passed through untouched. The runner also fails a run whose report counts failures, and one
whose tests all abstained — a tier that proved nothing is not a pass.

**Cost is dominated by editor-commandlet boot, not by the tests.** The whole `Substrate` tier
executes in roughly three seconds inside a run of about twenty; a single narrow filter costs the
same twenty. So a narrow filter buys focus and a readable failure, not time, and the choice
between one filter and the whole tier is not a cost decision. `Content` is the tier that costs
real time, and it is the one worth scoping.

If the UnrealBuildTool mutex is held by another process, wait for it to release and retry rather
than killing the holder (the `build-slots` skill).

## Writing a test

**A test name must be a leaf.** Unreal's automation registry turns a name that is a strict prefix
of another registered name into a branch of the test tree, and the test registered under it never
runs and never reports. `uv run elysium doctor` refuses a prefix collision and a duplicated name.

**A missing prerequisite abstains; a present but broken one fails.** An abstention is
`AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: <reason>"))`, which the runner counts separately from an
execution. Never gate on `TestNotNull`, which registers a failure and then returns, and never
write a prose "skipping" — both produce a green run that proved nothing.

First add content-free `Substrate` coverage against `Private/Tests/ElysiumTestServices.h`; add
`Content` coverage only when the real export corpus is required.

## Python

Python tests are `unittest`, and pytest is not installed. Run one module from the repo root as
`uv run python -m unittest pipeline.tests.<module>`; `unittest discover -s pipeline/tests` fails
because that directory is not an importable package.

## Scope is gated

Start with the narrowest owning automation filter, pure-rules test, or Python test method, and the
smallest export/bake selector. An exporter or Unreal-generator change does not authorize a complete
profile as validation. If the requested change affects export or bake products and the owner has
not named the scope, ask for the exact map, model, placed model, NPC/body stem, bundle, or
generator before launching it. A complete tier is a separately planned and accepted operation.

Never invoke `reconstruct`, `export grid|all`, unscoped `export characters`, a full policy/cast
bake, broad `--force`/`--clean`, or the complete Python suite without first stating the command,
scope, reason, and expected cost and receiving explicit owner acceptance. Use focused commands and
receipt-backed no-op checks during iteration; broader release acceptance is a separately approved
step.

**A live run is proposed, never assumed — ask the owner first, with a recommendation.**
