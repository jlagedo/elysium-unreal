# Step 2, V1 + V2 — the inventory and the first full run: briefs

Three authors write the scenario records that prove the landed work, each running and triaging its
own; then one reviewer checks every record against retail and consolidates the triage.

| brief | covers | model |
|---|---|---|
| `brief-P-perception.md` | idle and cadence, senses, memory, investigation, the mind's conditions | Opus/high |
| `brief-K-kernel.md` | the loop in combat, movement, cover, damage and death, the player's verbs on an NPC | Opus/high |
| `brief-W-world.md` | the class roll call, scripts and I/O, patrols and places, makers, the two maps | Opus/high |
| `brief-review.md` | V2: every record against retail, the whole suite once, the consolidated triage | Opus/high |

## What a record is for

A record states **what retail does**, in order, with deadlines, as trace events
(`Arena/README.md` is the schema; `stories/wave2/seam.md` the event kinds). The port either does it
(`pass`) or does not, and then the record keeps saying what retail does and carries `known_red`.
A record is never tuned to what the port does today: that is the failure this step exists to end
(1,722 green unit tests, and an NPC that never fired).

## Rules every author follows

- Read `AGENTS.md`, `docs/specs/0002-npc-ai/spec.md` (§ standing rules, § the known reds, § Step 2),
  `Arena/README.md`, `stories/wave2/seam.md`, `Arena/scenarios/cover.json` (the model record) and
  your brief.
- **Retail first.** Before writing a record, read the behaviour's retail source: the schedule text
  under `Content/ElysiumCorpus/` (the program's real task order and interrupt list), the walked
  prose under `docs/vtmb/` (find it with `uv run elysium research where <address or name>`,
  `section`, `cited`), and the listing through the `vtmb-corpus` MCP tools where neither settles it.
  `about` cites the schedule text or address the expectations come from.
- **Expectations**: the program's observable spine, not every task — the schedule installed, the
  tasks whose completion a shipped program depends on, the conditions that break it, what it
  writes (a hint claimed, an output fired, a death). Deadlines come from the program's own waits
  plus honest slack; a deadline is there to catch a hang, not to pin a frame. Use `never` for what
  must not happen (a `taskfail` storm, a death, a schedule that retail cannot select here).
- **You may run your own records**: `uv run elysium arena <names…>` (a headless boot, no build;
  it waits for the checkout lease, so runs queue). Read the verdict and the trace file it names.
  You never build, and you change no C++, Python or generated file. If the harness cannot express
  what a record needs (an action, a probe, an event kind, a stage prop), write the record as far
  as it goes, and list the gap in your report; do not work around it with a weaker expectation.
- **When a record is red**, read its trace and decide which it is, in your lane's triage file
  (`stories/v1/triage-<lane>.md`, one entry per red: record, first unmet expectation, what the
  trace shows instead with times, the retail source, the class):
  1. *record error* — you misread retail or the schema: fix the record;
  2. *harness gap or fault* — the runner or a tap is wrong or missing: report it, leave the record;
  3. *a known red* (spec § "The known reds", 1–11): set `known_red: "<n>: <what the trace shows>"`;
  4. *a new red* — the port diverges from retail somewhere not listed: set
     `known_red: "new: <one line>"` and write the divergence down with its retail address and the
     port's `file:line` where you can find it. Do not fix it.
- **The query budget** (10 s warns, 60 s stops) and **no polling, no shell for text** bind you.
  A boot is a run, not a query; give it a timeout of a few minutes and wait for it.
- Records go under `Arena/scenarios/<your lane's folder>/`, names unique and prefixed by family
  (`sense_cone_enter`, `combat_melee_swing`). One behaviour per record; a record under a minute
  of game time unless the behaviour itself is longer.
- **The inventory**: for every landed story your brief assigns you, one row in
  `stories/v1/inventory-<lane>.md`: story, what it claims, the record(s) that prove it, their
  result, or why no scenario can observe it (and what does).
- Do not commit. Report ≤300 words: records written (pass / known red / new red / error), the
  harness gaps, the stories left without a record and why.
