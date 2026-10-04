# Step 2, wave H — the harness wave: briefs

The first five harness gaps of `stories/v1/triage.md` § "The harness wave". Three coders on
disjoint files, then one integrator who builds, runs and commits. The bug protocol's tool class:
the harness is fixed, no record is weakened.

| brief | covers | files (only these) |
|---|---|---|
| A — the stage | H1 (stage half), H4 | `Source/ElysiumUE/Private/Debug/ElysiumArenaStage.cpp`, `.h` |
| B — the record and the runner | H1 (runner half), H3, H5 (probes, actions) | `Private/Debug/ElysiumArenaScenario.cpp`, `.h`, `ElysiumArenaScenarioRunner.cpp`, `.h`, `Arena/README.md` |
| C — the room and the taps | H2, H5 (the stealth-kill event) | `Private/Debug/ElysiumArenaBuilder.cpp`, `.h`, `Substrate/ElysiumGrapple.cpp`, the trace seam's own files (the event-kind table and its sink), `stories/wave2/seam.md` |

## Rules for every coder

- Read `CLAUDE.md`, `docs/specs/0002-npc-ai/spec.md` § Standing rules and § The bug protocol,
  `Arena/README.md`, `stories/wave2/seam.md`, and `stories/v1/triage.md` § "The harness wave" and
  § "Records whose verdict depends on occlusion".
- **You never build**, never launch the editor, never run `uv run elysium arena` or a test suite.
  The integrator does. Write code that compiles by reading the headers you call.
- Touch only your brief's files. If the job needs a line in another file (an accessor that does
  not exist, a kind string in another lane's table), do not edit it: write the exact line and its
  place in your report, for the integrator.
- No record under `Arena/scenarios/` is edited by a coder. The integrator updates records.
- The harness observes; it never changes what the game does. A tap emits only when a sink is
  attached. A reset restores what a freshly spawned player has, through the game's own functions.
- Match the surrounding code's comment density and naming. No new test of a port mechanism; a
  harness self-test record (`Arena/scenarios/_selftest/`) is described in your report for the
  integrator to add.
- The query budget (10 s warns, 60 s stops; never read a file over ~200 KB whole), text through
  the built-in Grep / Read / Glob tools, no sleep and no polling loop.
- Do not commit. Report ≤300 words: what changed per file, every line another file needs, the
  self-test records to add, what you could not do.

## The shared names (fixed here so the lanes agree)

- The new trace kind is `stealthkill`; its text is `<victim targetname or none> <admit|refuse>
  gate=<gate name>`, the gate names retail's (`ray`, `valid_target`, `deaf_arc`, `can_grapple`),
  one event per `FindVictim` query that has a candidate or is refused. C emits it and registers
  the kind in the seam; B accepts `stealthkill` as an `expect` / `never` kind.
- `never` gains `from` (scenario seconds), `after` (an expect label, with optional `delay`) and
  `at_most` (a whole number ≥ 0; absent means 0: the first match fails, as today).
- New script actions: `player_crouch` (`"on": true|false`), `light_pin` (`"value": <0..1>` or
  `null` to release; the existing `debug_stealth_light` door, `Substrate/ElysiumStealth.h:60`).
- New probes, `who: "player"`: `player_weapon` (string: the active item's classname, `none`),
  `player_crouched` (bool), `player_grappling` (bool).
