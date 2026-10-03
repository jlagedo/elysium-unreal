# Brief — V2: the records against retail, the whole suite once, the consolidated triage

Runs after the three authors report. You are a fresh reader: you did not write the records.

## Job

1. **Every record against retail.** For each record under `Arena/scenarios/perception/`,
   `combat/` and `world/`: read its `about`, open the schedule text or the address it cites, and
   check the expectations are retail's — the order, the tasks named, the conditions, the
   deadlines' basis. Three faults to hunt:
   - a record that encodes what the port does (it passes, and retail would not do that);
   - a record that passes for the wrong reason (an empty or too-loose `match`, a deadline at the
     duration, an expectation any NPC would meet);
   - a `known_red` whose class is wrong (a record error or a harness gap filed as a game red, or
     a new red filed under a known number it does not match).
   Fix a record you can prove wrong from the retail source; note every change with its source in
   `stories/v1/review.md`. A doubt you cannot settle from the sources is listed, not guessed.
2. **The whole suite once**: `uv run elysium arena` (every record, one boot per stage). The
   result table per record: `pass`, `expected-fail` (with the known red), `fail`,
   `unexpected-pass`, `error`.
3. **The consolidated triage** — `stories/v1/triage.md`, from the three `triage-<lane>.md` files
   and your run:
   - per known red 1–11: the records that show it, and the one trace line that is its signature;
   - every new red: its retail chain (address, schedule text), the port's `file:line`, the records
     that show it, and the fix story that owns it (V3 the arbiter and scenes, V4 the animation
     chain, V5 attack conditions and interrupts, V6 session / clock / lifecycle, V7 inputs) or a
     new story proposed with its size;
   - the harness gaps, deduplicated, each with the records parked on it, ordered by how many
     records it unblocks;
   - the landed stories with no record, and why (from the three inventories, merged into
     `stories/v1/inventory.md`).
4. **The fix order**: for V3–V7 and any new story, which records turn green when it lands, so
   each fix story has its acceptance list.

The rules of `README.md` bind you (retail first, the query budget, no polling, no build, no code
edits). Do not commit. Report ≤300 words: the result table's totals, the new reds, the harness
gaps, the records you corrected.
