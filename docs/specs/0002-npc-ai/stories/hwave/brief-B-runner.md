# Brief B — the record and the runner (H1's runner half, H3, H5's probes and actions)

Read `README.md` here first. Your files: `Source/ElysiumUE/Private/Debug/ElysiumArenaScenario.cpp`
and `.h`, `ElysiumArenaScenarioRunner.cpp` and `.h`, `Arena/README.md`.

## H1 — a failed stage errors every later record of the boot

In the full run two camera records failed to stage and 44 later records reported `error`. A
record whose stage fails is `error`; the next record gets a fresh stage and runs. Brief A makes
the stage's failure state releasable; you make the runner go on (find where the boot gives up and
what it checks). Keep the bound that ends a record whose stage never activates.

## H3 — `never` with a start and a count

Today a `never` covers `[0, until]` and fails on the first match. Records cannot say "no
`taskfail` storm" (one legal failure, then none), "nothing after the death", or "no schedule
install while thinking is disabled". Add, strictly parsed like every other field:

- `from` (scenario seconds) or `after` (an `expect` label, optional `delay`): the window opens
  there. `after` a label that is never met: the window never opens (the unmet expectation already
  fails the run).
- `at_most` (whole number ≥ 0, default 0): the run fails at match number `at_most + 1` inside the
  window. The failure's `reason` in `index.json` states the count and the bound.
- `until` unchanged. A record with an open `never` still runs to its duration.

## H5 — the player's probes and actions

Names are fixed in `README.md` § "The shared names".

- Probes with `who: "player"`: `player_weapon`, `player_crouched`, `player_grappling`. Read them
  through accessors the player already has; if one is missing, do not add it elsewhere: report the
  exact accessor needed.
- Actions: `player_crouch` (`on`), `light_pin` (`value` or `null`). The crouch goes through the
  same input door `player_walk` uses (the router's replay), not a console string; the light pin
  through the existing `debug_stealth_light` door.
- The kind `stealthkill` accepted in `expect` / `never` (brief C emits it).
- Every script action that runs is written to the trace as kind `script` with its `do` and target
  (triage H13's script half), so a trace shows what the harness did and when.

## `Arena/README.md`

Document all of the above in the tables that exist; add `stealthkill` and `script` to the kinds.
Fix the text detail of triage H14: the README says a schedule prints `(0xNN)`; the trace prints
the number without padding.

## Self-tests to describe (the integrator adds them)

A `never` with `at_most: 1` that passes on one match and one that fails on two; a `never` `after`
a label that ignores a match before it.
