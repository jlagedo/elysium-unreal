# Brief — make V4c's briefs final (one Codex planner, high effort; docs only)

You are a planner for spec 0002 in this repository. Read `AGENTS.md` first and follow it (the
query budget: 10 s warns, 60 s is a hard stop per query; never read a file over ~200 KB whole).

**You edit only** these files under `docs/specs/0002-npc-ai/stories/v4/`:
`brief-C1-attack-producers.md`, `brief-C2-pick-disposition-corpse.md`, `brief-C-integrator.md`,
the new `brief-C3-team-registry.md`, and `README.md` (only §4's V4c row and lane list).
**You edit no source code, no `Arena/` file, no other doc. You do not build, run tests, run the
arena or the game, and you do not commit.** Source code is read-only context.

## Inputs

- `docs/specs/0002-npc-ai/HANDOVER.md` (state; the owner's rulings — one shared random stream for
  every animation pick; the team registry is built in V4c; the real reload is V5's).
- `docs/specs/0002-npc-ai/stories/v4/packets-S13.md` (settled today: its "Changes to the plan" is
  your instruction list) and `packets-S12.md`, `packets-S11.md`, `packets-S10.md`, `packets-S5.md`
  item 3 (the melee contact's D1–D11).
- The three V4c briefs as they stand, and `docs/specs/0002-npc-ai/stories/v4/README.md` § "Rules
  for every agent of V4" and "Shared names".
- The code as landed: `git log --oneline -12`, and `git show -s 64895278` (V4o's commit message:
  what it left for V4c — the shot-timer estimate still in the code, a gunman that keeps a dead
  enemy) and `git show -s 88649932` (V11: `chase_melee` red on the swing clip's movement).

## The job

1. **Apply `packets-S13.md` § "Changes to the plan" to the briefs**, each item as an explicit
   numbered job line with its retail address and the port site (file, function — find it by
   searching the function name; line numbers go stale):
   - C2: the fade (the maker's `0x200`, the death-time install), the dead-enemy defect
     (`BestEnemy` testing `IsInert()` where retail tests `!IsAlive()`; the memory fidelity work
     the packet lists), the release-fallback fidelity of the feed victim.
   - C1: the contact predicate with the team test wired to C3's registry; the knockout
     attribution corrected (what the packet settled and what it left unverified — if a cause is
     still unverified, the brief tells the coder to read it from the listing first and names the
     addresses).
   - **C3 (new brief):** the team registry — the field (`+0x10b0`, a 16-bit team symbol), every
     writer and reader the packet verified, registration and the level-boundary resets, the
     minimal port state, the functions to port, the arm tests pinning addresses, and the Green
     Room record of two same-team NPCs (name, staging, expect / never, citations). Same structure
     as the other C briefs.
2. **The owner's ruling on the pick stream** into C2: NPC and non-NPC animation picks draw on one
   shared stream, as retail's single engine stream; name the port's stream and every pick site
   that moves to it (the packets map them).
3. **Disjoint files.** List each lane's files and check that no file is in two of C1, C2, C3.
   Where a change needs a file another lane owns, give it to the owner of that file as a job line,
   or state the function-level split in both briefs. Generated slot files (`…Slots.cpp`) are never
   hand-edited: a hand body goes in the matching `…SlotBodies.cpp` and the integrator adds the
   `kernel_verdicts.tsv` row.
4. **Every "unrecovered", "unread" or "not read" sentence** left in the three briefs: replace it
   with the settled instruction if a packet settles it; otherwise make it an explicit first step
   ("read `<address>` from the listing before writing; record it in `<doc>`").
5. **`brief-C-integrator.md`:** the lanes are now C1, C2, C3; the records to write or correct
   (`chase_melee`, `melee_enemy_blocked`, `corpse_fades`, `corpse_pedestrian_stays`, the team
   record, a dead-enemy record: after its enemy dies a gunman leaves combat — name it and state
   staging, expect / never with citations from the packet); read every diff for shadowed locals
   (C4458 / C4459 are errors here), missing includes and double definitions **before** the first
   build; one `uv run elysium build --arm`, cap two builds; the named records, the default tier,
   the arm tier, then the full arena once after the last build; one commit staged by explicit
   path with the verdict table in the message; never push; no file named `report*.md`.
6. Each coder brief ends with the same rules block the other briefs carry (touch only the listed
   files; never build, run or commit; retail first with the address at each line; a new
   divergence is recorded in the report, not adopted; shadowing is a compile error; report ≤300
   words with the lines owed by other files).

## Final message (under 300 words)

The files edited; the three lanes' file lists with the disjointness result; the records the
integrator will write; anything a packet did not settle that a lane must read first; anything that
needs the owner (a later phase, the pipeline, a re-bake).
