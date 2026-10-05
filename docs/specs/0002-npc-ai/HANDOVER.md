# Handover — spec 0002 (the character AI), 2026-10-05 11:10

A temporary note for the session that picks this up after a restart. You are the coordinator, in
`E:\dev\elysium-unreal`, branch `spec-0002/step-2`. Read `AGENTS.md`, then this, then load the
`codex-cli` skill (`~/.claude/skills/codex-cli/SKILL.md`).

## The goal (the owner's, verbatim in intent)

Work alone, every sub-agent on Codex (`gpt-6.1-sol`, unsandboxed; `high` for readers, planners,
judges, integrators; `medium` for coders and compile gates). Run V4c, V4d, V5b, V6, V7, V10, V12 and
the second full run in order, each closing green; settle unread retail first; pull forward what a
record needs; anything needing a later spec, the pipeline or a re-bake goes to an adversarial judge;
commit once per green wave; never push. **Stop before V8** (the owner live), then one summary: what
closed, what moved, every judge ruling, what is broken.

## State

| wave | state | commit |
|---|---|---|
| V4c attack producers, pick stream, corpse, dead enemy, teams | closed | `d0f79574` |
| V4d ragdoll from the `.phy` (30 bodies baked) | closed | `a5b58f37` |
| V5b reload, capability word, interrupt-cache tail | closed | `9e29f419` |
| **V6** session, clock, lifecycle | **integrating, uncommitted** | — |
| V7 the 19 inputs | planned, briefs final (`stories/v7/`) | — |
| V10 + V12 sound life, footsteps | planned, briefs final (`stories/v10/`) | — |
| second full run | not started | — |

Totals at V5b: arena 116 pass / 1 fail `rollcall_vzombie` (H11) / 15 expected-fail / 2
unexpected-pass (`hear_world_investigate`, `interest_mode_never`, N4); default 169 / 0; arm 1,636 / 0;
kernel 7/7. Nothing pushed since `2ac33a53`.

## V6, right now

- The main checkout holds all of V6 uncommitted (~60 files); it compiles. Safety snapshot of the tree:
  `refs/backup/v6-wip-2` (`de82b138`); the earlier `refs/backup/v6-wip` is the tree right after the
  compile gate.
- The integrator is a Codex run: folder `E:\elysium-work\codex\V6-int\` (`brief.md`, `events.jsonl`,
  `last.md` when it ends), thread `01a10c1d-42a8-73a2-8554-ea601721fe89`. At 11:04 it was at 219
  steps: default tier 169 / 0; N9 settled (the maker's box has no alive test: the first child's corpse
  refuses the second, as retail `0x1034b692..0x1034b737`; the record was corrected); both
  flamethrower real-reload records pass (the judge's fourth sitting); it was working through the
  remaining named records, then the arm tier and the full arena.
- **First step after a restart:** `git log --oneline -3`; is there a `fix(npc): V6` commit? If yes,
  read its message and go on to V7. If not: is the run alive (`events.jsonl` still growing, a
  `codex.exe` process)? Alive: put a watcher on it. Dead without `last.md`: resume the thread
  (`codex exec resume <thread> -m gpt-6.1-sol -c model_reasoning_effort='"high"'
  --dangerously-bypass-approvals-and-sandbox --json -o last2.md - < followup.md`) with "continue from
  the working tree; the brief is unchanged", or, if the thread is very long, start a fresh integrator
  on `V6-int/brief.md` plus what `events.jsonl`'s last agent messages say is done.

## Then

1. **V7:** create `E:\elysium-work\worktrees\v7` (`git worktree add -b spec-0002/v7-coders … HEAD`),
   three coders at `medium` on `stories/v7/brief-V7-1…`, `-2…`, `-3…` (the wrapper brief of
   `E:\elysium-work\codex\V6-1\brief.md` is the template: read from the main checkout, write only in
   the worktree, never build); commit on the worktree branch; `git diff <base> <branch> | git apply`
   into the main checkout; a `medium` compile gate (owed lines, build until it compiles, nothing
   else; the template is `E:\elysium-work\codex\V6-gate\brief.md`); a `high` integrator (template
   `E:\elysium-work\codex\V6-int\brief.md`).
2. **V10 + V12** the same way (`stories/v10/`); its first step is the N4 diagnostic record.
3. **The second full run:** every scenario, both tiers; then stop before V8 and write the summary.

## The method that worked

- Coders in a worktree outside the repo, a compile gate, then a fresh integrator per pass; builds are
  cheap (about 2 minutes): cap runs, not builds. Default tier first after a build; records by name,
  looped; arm; kernel check; the full arena once.
- A wave's own records are the wave's: no `known_red` without the retail reason and the owner the
  judge named.
- Every Codex run gets one background watcher (end, `turn.failed`, 10–20 minutes of silence). Claude
  Code kills background shells when the machine is critically low on memory (Unreal fills it): the
  owner restarts with `CLAUDE_CODE_DISABLE_BG_SHELL_PRESSURE_REAP=1`. The Codex runs themselves
  survive that.
- This session cannot edit the main checkout directly: doc edits are made in
  `E:\elysium-work\worktrees\coord` (branch `spec-0002/coord`; rebase it onto the work branch first),
  committed there and cherry-picked.
- Worktrees (`coord`, `v4d`, `v5b`, `v6`) are never removed by the coordinator; see `AGENTS.md`.
  `E:\dev\elysium-unreal-task-life5` is not this spec's.
- `AGENTS.md` may show the owner's uncommitted edits and `Session/ElysiumRng.cpp` a line-ending
  residue: never stage either.

## Judge's rulings

J1–J14: `stories/v1/triage.md` (two sections "Judge's rulings, V4"). Third sitting (17 rulings on the
V6 / V7 / V10 plans): `stories/v1/judge-third-sitting.md` — the arena clock starts at retail's 1.0;
Bloodshield's status service pulled into V7; the 64-slot sound allocator pulled into V10; N4 closes
only on three green boot orders; corpse visual persistence filed to 0014 / 0017; speculative
pipeline work in V10 refused. Fourth sitting: `stories/v1/judge-fourth-sitting.md` — retail NPCs
never reach the flamethrower's `Attack` and its sequences author no shot event; the real reload gets
its live run from a staged empty clip (done inside V6).

## Known broken or open

- `rollcall_vzombie` (H11); the nine hidden-species rollcalls; N4's intermittents (V10).
- Four bodies have no `.phy` and no ragdoll: `rat`, `rat_swimming`, `wolf_form`,
  `mercuriodamagedstreet`.
- A restored corpse keeps its dead mind and fallen body but its life / health words reset (V6 is
  fixing the logical half; the visual pose is filed to 0014).
- Unavailable and named: the event-free body seek (presentation continuation on restore), the
  conversation producer, Presence (0006), the player's single-round reload continuation.
- The old "victim dies on the fifth hit" never reproduced; three `damage_*` records guard the path.
