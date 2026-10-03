# 0002 npc-ai — consolidated 2026-09-30: the NPC, its world and its kernel; one spec, one sequence, one witness harness

**DRAFT for the owner's review.** On approval this text replaces `spec.md` (whose current text moves
verbatim to `record-2026-09-30.md` as the landed record), 0018 and 0019 close with a pointer here,
0003's kernel-side stories 1–2 fold in, and `TRACKER.md` is rewritten to the sequence below.

**Standing rules, in priority order.**
1. Follow retail. A behaviour is ported from the listing, every arm, in retail's order, with retail's
   constants and what it writes. A defect claim needs the retail chain that reaches it.
2. Diverge only where retail cannot be followed (Source's node graph, its motor, its studio sequence
   table), and then approximate with the retail contract kept: same inputs, same outcomes, same event
   order. A divergence is named at its line and in its story. A mechanism that changes state or event
   order is never a "modernization".
3. One or two tests that pin a recovered retail behaviour beat ten that pin the port. Every test
   cites the address or the schedule text it holds the port to; a test that pins a port-only
   mechanism is deleted with the mechanism.
4. No shortcuts. The kernel is a state machine tuned by 691 shipped programs; a gate, a guard or a
   stand-in that makes a symptom disappear is a defect until the retail chain behind it is read.
5. Token- and time-bounded. A story is one seam commit, then bodies in parallel lanes on disjoint
   files, one build and one test run per wave, an arena scenario as the acceptance, a full suite once
   at close. Briefs and findings are files; reports are ≤300 words.

## Why this rewrite

Three specs (0002 the mind, 0018 the world's objects, 0019 the kernel's data) were run as one serial
tracker of 50 rows. 0019 closed 2026-09-29; 0018 rows 1–7 and 21-1…7 landed; 0002's open rows were
cut to XS–S by the reach cut. On 2026-09-30, running the first controlled scenario (the Green Room
arena, a gunman taking cover from the player), the kernel loop was live and the hint claim chain was
right, but the NPC never fired and never finished a cover animation. The cause was not an open row.
It was a port-only mechanism from 2026-08-12 — the body arbiter (`EElysiumBodyOwner`,
`FElysiumNpcMind::Acquire`) — carried into 0019/8's sequence bridge as a "named modernization of
playback", which refuses the kernel's own activity commits while a schedule holds the body
(`ElysiumNpcAnim.cpp:408`). No test crossed that boundary: 1,722 unit tests, and none runs a schedule
that walks and then animates through `Think`. The same session found five more defects of the same
kind in a 140-second run (§ "The defects landed work carries"). Each open story was about to be built
on top of them.

So this spec does three things before it plans anything new: it names what is landed and what of it
is proven; it retires the port-only mechanisms that gate landed retail bodies; and it changes what a
test is here, so the class of defect that was found by hand is found by the suite.

## State of the tree, 2026-09-30

| Layer | Landed | Proven by | Standing |
|---|---|---|---|
| Kernel data: verdict overlay, datamap bindings, 691 schedule texts, tunables, class tree, deletions, mechanism seams, reach cut (0019/1–7) | all | generator `--check`s, census tests, `kernel --check` 7/7 | closed; RE-BACKLOG 40–44 open reads; 0019/3's corpus switch and 0019/4's 83 constants were never observed live |
| The loop: `NPCThink → RunAI → GatherConditions → MaintainSchedule → 437/438 → StartTask/RunTask` (0019/8) | all 380 `rule` rows | 17 family suites (446 cases), three IDLE map smokes | live; twins gone but 4; **its body contract is broken (§ defects 1–3); combat and death have never been observed on a retail map** |
| Senses, memory, conditions (0002/1–9, 10a–c, 15, 20) | all | unit suites, the tutorial witness tests | live; **0002's own witness (`thug_1`'s sneak-past, the stealth kill, the trance) has never been played**; `Sighted()` gate defect for a `SetRelationship`-flipped NPC |
| World objects: baked actors, collision, NavMesh, places, navigator, geometry, traversals, hints (0018/1–8) | all | content tests on the baked witnesses, `verify nav`, live smokes | live; 0018/8 tick pending; 0018/7's live half handed to the executor retirement; the mesh walks every jump-only pair retail refuses (§ defects 7) |
| Programs as data, the task arms, the selectors | 241 of 243 tutorial task identities armed | family suites | live; **every animation-ending task is gated by the arbiter** |
| Save / restore (0019/2 pass C, 0019/6) | the generated walk, `OnPostRestore` | `SaveRoundTrip` only; no live load verb exists | never observed live; restart-not-resume (§ defects 8) |
| Port-only executors: ambient places, patrol (retired), follower owner | — | executor-shaped tests | the ambient executor is still the live path for every `use_interesting` NPC |
| Green Room arena harness (`uv run elysium gr --arena --headless`, `gr_scenario`, `npc_trace`, `gr_hints --validate`) | 2026-09-30, uncommitted | run 6: claim → release → claim observed | the controlled witness this spec builds on |

The audit's count (§ Findings D): of 46 landed stories that change runtime behaviour, 29 have no live
check or an unobservable one and 6 were checked in part; 42 "named modernizations", of which 22
change state or event order and must be re-classed as divergences with owners.

## The defects landed work carries (found 2026-09-30, each with its retail chain)

1. **The body arbiter refuses the kernel.** `PlaySequenceClip` and the stance transition play only
   under owner `None` / `Dialogue`; the kernel's path tasks claim `Schedule` and release at program end.
   Every schedule that walks then animates (cover-out, range attack, melee, flinch, reload, comfort,
   cower) commits a sequence at rate 0; `IsActivityFinished` never fires. Retail has no arbiter:
   `ResetSequenceInfo 0x10090950` plays whatever `m_nSequence` holds; a scene holds an NPC through
   `m_scriptState +0x5d70` / `m_hCine` and `SCHED_SCRIPTED_*`; a dialogue through `m_hDialogPartner`
   and `TASK_RUN_DIALOG`; and `ClearSchedule 0x10280d30` ends a program. Consumers to retire:
   `AcquireScheduleBody` at `SetGoal` / `SetRandomGoal` / `AdvancePath` (a refused claim fails a task
   by a code retail never raises), `ReleaseProgramBody`'s `Motor->Stop` (retail's `OnScheduleChange`
   already clears the navigator), `SaveBlockReason` (retail saves anywhere), `SerializeMindBlock`'s
   owner word, the `Sequence` / `Dialogue` / `Ambient` / `Follower` / `ScriptedSchedule` claims.
2. **`GatherAttackConditions` never clears.** The port (`ElysiumNpcConditions.cpp:1018`) lacks the
   top clear of slot 560's eleven conditions (`0x1026de02`) and the tail that clears `0x08 / 0x5f /
   0x60 / 0x09 / 0x63` once any `CAN_*` holds and the blocked-by-friend timer lapsed
   (`0x1026dfd0..0x1026e107`; the two timers exist at `ElysiumNpcBase.h:185/189` and are never read).
   `TOO_CLOSE_TO_ATTACK` and `CAN_RANGE_ATTACK1` stack; `0xef` steps back forever.
3. **The animation chain under the kernel is half stub, half out of order.** Slot 258
   `DispatchAnimEvents` (`0x10091880`) on the NPC chain is a counting stub; the port fires events
   instead from `FElysiumEntityWorld::AdvanceAnimEvents` (a per-entity poll of the body's clip phase
   on the world tick, `ElysiumEntityWorld.cpp:1889`), not from `PostRun 0x1026c8c4` after
   `StudioFrameAdvance` — an event-order divergence nobody named. `SetAttackExtentsForSequence
   0x10090c80` fires at every spawn; a dead NPC stays "skeletal (standing)" with `on_ground=0`;
   `task_face_enemy` ran 13.4 s and the NPC never turned on `NOT_FACING_ATTACK` (cause unread:
   `MotorDeltaIdealYaw` is retail's since wave 2, so the question is whether `Motor->Face` turns the
   body); `CBaseCombatCharacter::FInViewCone(entity)` (slot 363, `0x10326750`) is a stub answering
   false, so `BEHIND_ENEMY 0x57` is set on every gather (`ElysiumNpcBaseConditions2.cpp:503`).
4. **`0xef` is a sink.** Its interrupt mask (retail's text) was not honoured for 99 s while
   `NOT_FACING_ATTACK` and `WEAPON_THROUGH_WALL` were set. Unread: whether the mask is applied or the
   conditions never reach `HasInterruptCondition`.
5. **Hidden and flipped NPCs never fight.** A `StartHidden 1` NPC after `ScriptUnhide` sits in
   `FALL_TO_GROUND` with `on_ground=false` and no body; an NPC whose relationship a script flips with
   `SetRelationship` never enters the sighted list; an open dialogue blinds perception; `map_load` of
   the same map keeps the previous session's entities. All four blocked story 8's live check.
6. **The executor release.** `ThinkSchedulePolicy` (`ElysiumNpc.cpp:985-988`) releases a held
   interesting place under any running program; retail releases only in `OnScheduleChange 0x102a0940`
   without `PRESERVE_PATH`. It breaks the crosswalk wait `0x100 → 0x102 → 0x100`.

Trace cosmetics found on the way: conditions `0x29` (`HINT_INVALID`) and `0x57` (`BEHIND_ENEMY`) print
unnamed; `npc_brief` mixes cm and units in the player line.

Found by the landed-story audit (§ Findings D), each a state or event-order change carried as a
"modernization" or a hand-off to no row:

7. **Reach beyond retail's graph.** The human mesh walks every jump-only pair retail's ground walk
   refuses (hub 117 of 117, tutorial 25 of 25) and joins zone pairs retail keeps apart. 0018's own
   boundary text says added reach changes which encounters can reach the player. Decision Q5.
8. **Restart, not resume.** Retail restores the task cursor `+0x5c50` and the animating words and
   resumes; the port restarts the program, which runs slot 435 and releases twelve saved rows.
9. **The primary-clip pick.** The sequence bridge plays one clip per activity where retail's
   `SelectWeightedSequence` draws among the activity's sequences by weight.
10. **The input surface.** 19 of retail's 33 NPC inputs are unregistered; the corpus fires
    `SetInvestigateModeCombat` 69×, `StayEntrenched` 67×, `SetInvestigateMode` 66×, `FleeAndDie`
    27×, `MakeInvincible` 19×, `SetFollowerBoss` 15×.
11. **`UpdateTargetPos 0x10271b10`** (the 80-unit re-path to a live `m_hTargetEnt`, comfort and
    the cine) is a seam verdicted to a closed story.
12. **Two absorbed stories never ticked:** 0002/25c and 26 landed under 0019/8 and are `[ ]` on no row.

## What a test is here (the doctrine)

Four kinds, and only these:
- **Census tests** (generated, byte-pinned): the shape, bindings, override and factory censuses,
  the tunables, the schedule corpus counts. Kept as they are; they cost nothing and catch drift.
- **Arm tests**: one function, one retail address, its arms in order, each asserting what the arm
  writes. One or two per body — the arm whose absence a shipped program would notice, and the
  edge (NaN / equality) the listing settles. Written from the walked prose, not from the port.
- **Scenario tests**: a whole program run through the real think loop over a built world with the
  real navigator, body and clock — the arena's `gr_scenario` in-process, headless, with an
  expectation script (trace lines in order, timing bounds). One per behaviour family. This is the
  test that finds the class of defect above, and no story ticks without one.
- **Witness runs**: the two maps, live, driven over MCP, at the end of a phase, not per story.

What goes: tests that pin a port-only mechanism (the arbiter, the executors, `bMoveIssued`,
`FailedSpotIndices`, the owner enum), tests that assert a seam "answers nothing" (the ledger's
seam list is the record; a seam landing is a body test, not a flipped assertion), duplicate arm
tests of one address across files, and tests that only count a stub firing. The triage is a story
(B1) with the audit's list as its input.

The corpus as audited (§ Findings A; 127 files, 98k lines, 1,324 tests in the NPC / world scope):
1,081 arm tests citing an address (kept), 36 census tests (kept), 75 seam-asserting tests (go, as
their seams land or at B1), 28 port-only tests (16 pin the arbiter, 7 count stubs, 4 read the
ambient executor, 1 calls a dead function — go with C1), 37 "integration" tests of which about 20
run the think and NONE runs a path task followed by an activity task; the two suites named as
witnesses (`ScheduleIntegration`, `NpcWitness`) drive `ElysiumSchedule::Tick` / `GatherConditions`
on a quiet NPC and never call `Think`. Duplication is not in the family suites but in the older
ones (`NpcEnemy`, `NpcCombat`, `NpcSenses`, `AiScriptedSchedule`, `NpcTests`): `GatherEnemyConditions`
is asserted in 8 files, `NPCInit` and Troika `TaskFail` in 7. The kernel task tests set
`bSequenceFinished` by hand, so the sequence path is tested nowhere below the arbiter's gate.

Costs, measured: the 1,722 Substrate cases run in 80 s of which `MapActorTeardown` is 51 s and
`SkeletalStageOwners` 12 s; the other 1,720 take 17 s. 1,232 of 1,722 pass "with warnings". The
editor boot dominates a run. So the runner, not the tests, is the time problem (A2). Compile:
`OverrideCensus.cpp` (5,210 lines, 1,051 template checks) and `ElysiumTestServices.h` (2,504
lines, included by 102 of 120 files) are the two costs worth cutting.

## The witnesses

- **The arena** (`docs/harness/green-room-arena.md`): a bare stage, one NPC spawned through the map's
  `Load` path, authored hints and places, the player at a seat, no other entity. Scenarios are
  data; the trace ring is the record. Every program family gets one scenario and the story's tick is
  that scenario green, in-process, on every run of `Elysium.Arena`.
- **`sp_tutorial_1`**, the sneak-past lesson on `thug_1` (unchanged from the first 0002 text) and the
  alley's `pt1..pt3`, `squad_warehouse`, the makers.
- **`sm_hub_1`** at idle: pedestrians, the crosswalk, the two patrol cops, the smoke-shop door, the
  makers.

## The method per story

1. **Seam first.** The story's first commit declares the interface or the named stubs (retail
   address, admitting default, the retail field they stand for) and the test names. It builds green
   on its own. Everything after it is bodies against a fixed surface, so lanes cannot collide on a
   header.
2. **Bodies in lanes.** Each lane owns disjoint files named in its brief; briefs and findings are
   files under the story's folder; reports ≤300 words. No worktrees.
3. **One build, one run, per wave.** Family filter between waves; the full suite once at close.
4. **The scenario is the acceptance.** A story's arena scenario is written with the seam commit
   (red), and the story closes when it is green and its arm tests are green. Map witness runs close a
   phase, not a story.
5. **Read before write.** A body whose retail walk is not in `docs/vtmb/` gets a reading packet
   first (`kernel_skeleton` brief → one reader → diff against the checklist row), never a guess.
6. **Ledger last.** `kernel --check`, the override census and `unported.tsv` are regenerated once
   at close; a verdict row a story closes carries its `[0019/1 …]` tag.

## Stories

Sizes: XS ≤ ½ day, S 1 day, M 2–3 days, L a week. Model: the lowest tier that reads the listing
well enough for the story's risk. Each story names the files its lanes own so waves are planned once.

### Part 0 — the consolidation commit (this spec landing; docs, ticks, no behaviour)

- [ ] **P0. The record.** This text replaces `spec.md`; the old text becomes `record-2026-09-30.md`;
  0018 and 0019 close with a pointer; `TRACKER.md` becomes the sequence below. Ticks with their
  record: 0002/25b, 25c, 26, 10j, 21b, 27, 13b, 10e, 10k (verified against Select19 / Maintain19 and
  the reach-cut clauses); every stale `Gap:` line struck. The 22 state-changing "modernizations"
  re-classed as named divergences at their lines, each with the story that owns it (the list is
  `consolidation/findings-D-landed.md` § 2). The orphaned hand-offs (§ 3 there) placed in the stories
  below or under "on demand". The arena harness committed. *Size:* S. *Model:* Sonnet/medium.

### Part A — the instrument (first, because every later story pays for it)

- [ ] **A1. The arena as an automation suite.** *Port today:* `gr_scenario cover` is a console verb
  over a hand-authored C++ spec; the observer is a human reading `npc_trace_tail` over MCP. *Job:* a
  scenario record (stage, NPC classname + keyvalues, hints, places, seat, triggers with times) as
  data under `Content/ElysiumAuthored/Arena/`; the stage built in-process by an automation test
  (`Elysium.Arena.<scenario>`) that ticks the world's clock and the body without rendering, drives
  the triggers, and diffs the trace ring against an expectation file (ordered lines with `≤ t`
  bounds); the same record runnable live by `gr_scenario <name>` for observation. The trace gains
  the two missing condition names and the unit fix in `npc_brief`. *Tests:* the harness's own (a
  scenario that must fail, a timing bound that must trip). *Size:* M. *Model:* Opus/high.
  *Owns:* `Debug/ElysiumArena*`, `Debug/ElysiumGreenRoomConsole.cpp`, `Tests/ElysiumArenaTests.cpp`.
- [ ] **A2. The runner and the lease.** *Measured (§ Findings C):* an `elysium test` run is 19 s
  median wall of which the tests are 0.1–0.7 s; boot is 96 %. The lease on the shared export root
  refused 2,470 `build` / `test` calls in a week across five checkouts, so agents poll; a `run play`
  holds it for its whole session. A `+`-joined filter already runs several prefixes in one boot but
  the report slug overflows `MAX_PATH` and the pipeline reports "matched no test". The headless
  runner cannot take tests through the game MCP (`-unattended` suppresses the server), so a warm
  editor is not the fix. *Job:* the lease keyed per checkout (`repo_root`) with `--wait`; `elysium
  test A B C` as one boot with a capped slug; `MapActorTeardown` and `SkeletalStageOwners` into an
  opt-in `Elysium.Slow` group; the 1,232 "with warnings" silenced at their source. *Acceptance:* a
  three-family run in one boot under 25 s wall; zero lease refusals in a day's journal. *Size:* S.
  *Model:* Sonnet/medium.
- [ ] **A3. The ledger, the pipeline tests and the MCP caps.** *Measured:* `research kernel
  --check` 20.3 s (the 470 s → 7 s fix of 09-28 is on `main`; the "15 minutes" were unrebased
  worktrees); standalone `kernel_shape --check` 17.1 s of which one memo cuts 4.1 s to 0.57 s with
  identical output; the corpus stage pickles to 50 MB and reloads in 0.22 s; `pytest` 4,417 tests ≈
  386 s of which 56 corpus-reading tests are 80 %, and one fails on `main` (`test_oracle_citations`:
  two briefs cite files that do not exist); `vtmb_asm` 148 KB, `vtmb_closure` 128 KB,
  `elysium_entity_get` ≈40 KB per entity fanning out to 10 by default, every game-tool result sent
  twice. *Job:* `functools.cache` on `sdk_members` and a regex `_strip`; the corpus stage and the
  SDK index pickled on an input hash; a per-file citation cache; `--check` short-circuits on an
  unchanged stamp hash; `--reach` in the gate; the `SyntaxWarning` gone; the `corpus` pytest marker
  off by default, `pytest-xdist`, one session-scoped kernel fixture, the dangling citations fixed;
  MCP defaults: `vtmb_asm max_lines=400`, `vtmb_closure sections=`/`brief`, `vtmb_code` 20 KB,
  `entity_get fields=`/`brief`/`limit=1` and `described` dropped, `console_exec max_lines=200`,
  `structuredContent` not duplicated. *Acceptance:* `kernel --check` under 5 s warm and under 1 s
  unchanged; default `pytest` under 20 s; no MCP reply over 20 KB by default. *Size:* S–M.
  *Model:* Sonnet/medium.

### Part B — the corpus

- [ ] **B1. The test triage.** *Input:* `consolidation/findings-A-tests.tsv` (per-file category,
  the 28 port-only tests by name, the 75 seam assertions, the duplicate addresses). *Job:* (1) the
  28 port-only and stub-count tests deleted with their mechanisms, in C1's and C2's waves, never
  ahead; (2) the 75 seam assertions deleted — a seam's record is the ledger's seam list, and a seam
  landing adds a body test; (3) the five older suites' duplicate assertions folded onto the family
  file that owns the address (`GatherEnemyConditions` ×8 → `Conditions19`; `NPCInit` ×7 →
  `Lifecycle19`; Troika `TaskFail` ×7 → `Maintain19`; the rest per the TSV); (4) `ScheduleIntegration`
  and `NpcWitness` rewritten to drive `Think` on an awake NPC or retired in favour of the arena
  scenarios; (5) `ElysiumTestServices.h` split so a kernel test does not pull the skeletal-mesh,
  light and anim-instance headers; the four 111-include preambles trimmed. *Acceptance:* every
  surviving test names an address, a schedule text or a scenario; the count and the compile time
  fall and are recorded. *Size:* S, spread over C1–C2's waves plus one closing wave.
  *Model:* Sonnet/medium.

### Part C — the body contract (the found defect class; before any program work)

- [ ] **C1. The arbiter retired; the NPC in `NPC_STATE_SCRIPT`.** (Absorbs 0003/1–2's kernel half,
  0002/11's executor retirement, 16a's `Follower` owner, and the four `STORY8-TWIN` survivors.)
  *Retail:* no arbiter. A scene: `m_scriptState +0x5d70` (0 none, 1 wait, 2 post-idle, 3 cleanup,
  4/5/6 walk/run/custom), `m_hTargetEnt` = the cine, `SelectSchedule 0x1028a380` case 4 → the
  `SCHED_SCRIPTED_*` programs (`0xf2/0xf4/0xf6/0xf8/0xf9` and the `_FAILED`s), tasks
  `WAIT_FOR_SCRIPT 0x60` / `PLAY_SCRIPT 0x62` / `PLAY_SCRIPT_POST_IDLE 99` / `PLANT_ON_SCRIPT 0x65` /
  `FACE_SCRIPT 0x66` / `ENABLE_SCRIPT 100`, `CineCleanup 0x1027d170`, `ExitScriptedSequence
  0x1027d0a0`. A dialogue: `m_hDialogPartner`, `0x6a RUN_DIALOG`, `m_bfNPCStateFlags` script bit.
  A pushed order: `CCineAISchedule` through `0x102ae780` and `PRESERVE_PATH`. An interesting place
  or a patrol: a program, released only by `OnScheduleChange 0x102a0940` → `0x102b53d0` without
  `PRESERVE_PATH`. The body always plays `m_nSequence` (`ResetSequenceInfo 0x10090950`).
  *Port today (§ Findings B, the retail word per site is its table 1):* the arbiter
  (`ElysiumNpcMindTypes.h`, `ElysiumNpcMind.*`) at 38 production sites in 7 files — 9 claims, 15
  releases, 9 owner gates, 5 script wrappers; `RefreshStateFromOwner` WRITES `m_NPCState` (a
  dialogue claim sets SCRIPT, every release forces IDLE); `RouteScheduleMaintenance`
  (`ElysiumNpc.cpp:885`) runs five port-only steps ahead of `MaintainSchedule` on every Troika think
  (`TickScriptWatchdog`, `ThinkInDialog`, `ThinkScriptOwned`, `ThinkSchedulePolicy`,
  `ThinkAutonomous`); `bReturnToExternalExecutorAfterSchedule` leaves `MaintainSchedule` with
  `Install(None)` at three sites where retail installs the next program in the same pass; the
  ambient executor (`ThinkAmbient`, `EAmbientPhase`, `Begin/FinishAmbientUse`, `FailedSpotIndices`,
  its own 24 cm walk and timers); `bMoveIssued` standing for the navigator's `IsGoalActive`;
  `bWalkingAnimation` written and never read; the parked-owner slot dead; the cine's
  `NpcScriptState` on the cine, not the NPC; the `TASK_RUN_DIALOG` upkeep never firing
  `m_OnDialogEnd` from the task.
  *Job:* (1) `m_scriptState` on the NPC, the Scripted arm of `SelectSchedule`, the scripted task
  arms over the navigator and the sequence bridge, `CineCleanup`; the cine writes the NPC's words and
  claims nothing. (2) The dialogue hold as `TASK_RUN_DIALOG` and the partner handle. (3) The ambient
  executor deleted; `use_interesting` bodies select `0xff` through retail's idle selector; the
  `0x102daac0` disable/kill walk; the crosswalk wait live. (4) The `Schedule` / `ScriptedSchedule`
  claims deleted; `SetGoal` / `SetRandomGoal` / `AdvancePath` never refuse for a claim. (5) The
  play hooks unconditional. (6) `SaveBlockReason`, the mind's owner word and `IsResumableOwner`
  deleted; save schema bumped. (7) `RouteScheduleMaintenance`'s five pre-steps and the external
  executor return retired: `NPCThink` runs retail's phases and nothing else; `bMoveIssued` renamed
  onto the navigator, `bWalkingAnimation` deleted. (8) `TASK_RUN_DIALOG`'s upkeep fires
  `m_OnDialogEnd`. (9) `ElysiumNpcMindTests`, the executor tests and every owner assertion deleted
  (B1's first batch). *Divergence kept, named:* a scene's clip is played by the
  scene's montage slot while `m_scriptState ∈ {1,2}`, which is where retail's `PLAY_SCRIPT` plays it
  too — no gate, the kernel's task is what plays it. *Tests:* arm — `SelectSchedule 0x1028a380`
  case 4 by `m_fMoveTo`; `OnScheduleChange`'s `PRESERVE_PATH`-gated place release. Scenarios —
  **cover-and-fire** (the gunman claims, plays cover-out, fires, releases: the 2026-09-30 run made
  green), **scripted-walk** (a `scripted_sequence` walks the NPC to a mark, plays, returns it idle),
  **place-visit** (a pedestrian runs `0xff → 0x100`, waits at a red curb, crosses on green, a second
  queues). *Consumes:* A1. *Provides:* the body every Part D program animates on. *Size:* L.
  *Model:* Opus/high, one Fable review of the seam commit. *Owns:* `ElysiumNpc.{h,cpp}`,
  `ElysiumNpcMind*`, `ElysiumNpcAnim.cpp`, `ElysiumScriptedSequence.cpp`, `ElysiumNpcScript.cpp`,
  `ElysiumNpcStartTask*` (script arms), `ElysiumNpcSelect.cpp` (case 4), `Public/ElysiumSaveTypes.h`,
  the tests named.
- [ ] **C2. The animation chain under the kernel.** *Retail:* `PostRun 0x1026c8c4`: `RunAnimation`,
  `StudioFrameAdvance` (slot 250), `DispatchAnimEvents` (slot 258, `0x10091880`: fire every record
  with `last ≤ cycle < now`, wrapped interval once, server band < 5000, weapon band 3000–3999 to
  `Operator_HandleAnimEvent`), `HandleAnimEvent 0x10274e30` (`AllowInterrupt`, `FireScriptEvent`,
  0x3e9–0x3eb landed), `SetAttackExtentsForSequence 0x10090c80`, `m_bSequenceFinished` from the
  advance, `IsActivityFinished` (slot 251) = finished ∧ sequence == ideal; death: slot 144 →
  `CreateCorpse 0x1032c0e0` → the ragdoll / static corpse and `on_ground`; facing:
  `MotorDeltaIdealYaw`, `UpdateYaw 0x102e1e20` (ported), the 15° `FacingIdeal 0x10278c80`.
  *Port today (§ Findings B: 8 animating stubs on the live path):* slot 258 and
  `SetAttackExtentsForSequence` are counting stubs on the NPC chain; events fire from the world
  tick's per-entity poll (`AdvanceAnimEvents`), after every NPC has thought, so a hit event and the
  schedule step that reads its condition land in different passes from retail's;
  `GetIdealYawSpeed` answers 0 so the turn rate floors at 1.0 (the 13.4 s `task_face_enemy`);
  `GetVelocity` and `GetGroundSpeedVelocity` answer zero; `SetPoseParameter02`, `BurnModel`,
  `AddExtraAnimationModels` count; the sequence bridge plays one clip per activity where retail's
  `SelectWeightedSequence` draws by weight; the corpse arm leaves the body standing; slot 363
  `FInViewCone(entity)` is a stub (`BEHIND_ENEMY` always). *Job:* slot 258 wired inside `PostRun`
  over the clip's baked event table (the poll retired for NPCs; a clip without a table fires nothing
  and says so once); `HandleAnimEvent`'s unbuilt arms; the attack extents from the sequence's bake;
  `GetIdealYawSpeed` from the sequence's yaw speed and `GetVelocity` from the motor; the weighted
  pick over the resolver's candidates on the `NpcSchedule` stream (or the divergence named where
  the bake carries one clip); the death pose landing on the body; slot 363 on the combat
  character. *Named modernization (visual):* the clip resolver stands for the studio sequence
  table; events come from the baked table, never from Unreal notifies. *Divergence kept, named:*
  the motor step runs on the body's tick under Unreal's movement component (0019/6's mechanism
  seam), not inside `NPCThink`. *Tests:* arm — `DispatchAnimEvents` interval and wrap; `IsActivityFinished`
  over a finite clip. Scenario — **melee-and-die** (a thug walks to the player, swings with its
  weapon's anim events firing the hit, is killed, lands the corpse). *Consumes:* C1. *Size:* M.
  *Model:* Opus/high. *Owns:* `ElysiumAnimating*`, `ElysiumAnimEvents.*`, `ElysiumNpcBaseAnim*`,
  `ElysiumNpcBaseMotor.cpp`, `ElysiumNpcDamage*` (death), the player's cone file.
- [ ] **C3. The attack conditions and the combat programs' interrupts.** *Retail:*
  `GatherAttackConditions` (`0x1026de02` clear, `0x1026dfd0..0x1026e107` tail, the two timers),
  slot 365 `0x1024f670` first-match (landed), the melee bands 64/256/180 and ranged 100/200/1024 or
  the weapon words (landed), `HasInterruptCondition 0x10269d30` against the program's mask, the
  `0xef` / `0xed` texts' interrupt lists, `NOT_FACING_ATTACK` / `WEAPON_THROUGH_WALL` producers.
  *Port today:* § defects 2 and 4; the weapon capability word `0x1014f930`, the burst size and
  the next-attack stamp are seams answering nothing on every combat think (0008's words, consumed
  here and named). *Job:* the two clears and the timers; a read of why `0xef`'s mask does not
  break (packet first); the `HINT_INVALID` / `BEHIND_ENEMY` names. *Tests:* arm —
  the gather over a stacked set answers exactly one attack condition; `0xef` breaks on
  `NOT_FACING_ATTACK`. Scenario — **cover-and-fire** (C1's) with the player moving to 96 cm and
  to 10 m: step back, re-face, fire. *Consumes:* C1, C2. *Size:* S. *Model:* Fable/medium.
  *Owns:* `ElysiumNpcConditions.cpp`, `ElysiumNpcBase.h` (timers), `ElysiumSchedule.cpp` (mask).
- [ ] **C4. Session, clock and lifecycle.** *Retail:* `curtime` restarts per level and
  `CWorld::Precache` re-runs `NPCInit` on a level change (the 0.8 s gate is relative to it); a
  restore reads the task cursor `+0x5c50` and the animating words (`m_nSequence`, `m_flCycle`,
  `m_flPlaybackRate`) and RESUMES the program; `ScriptHide 0x102c1ce0` / `ScriptUnhide` (landed) and
  the hidden body's ground snap on unhide; `SetRelationship` through `AddEntityRelationship
  0x10332ca0` (landed) and the sighted list's admission on the next look; `m_bInPlayerLOS` and the
  look pass during a dialogue; `OnTakeDamage_Alive 0x10265ed0` writing `m_vecLastDamageAttackPos`.
  *Port today:* one world clock across levels; `map_load` of the same map keeps the previous
  session's entities; no live load verb (restore is proven by `SaveRoundTrip` alone); the port
  restarts the restored program (0019/2 pass C's named divergence: slot 435 releases twelve saved
  rows); `thug_3` after `ScriptUnhide` sits in `FALL_TO_GROUND` with `on_ground=false` and no body;
  a `SetRelationship`-flipped NPC never enters `Sighted()`; an open dialogue blinds perception;
  `TakeDamage 1` raises health without a last-damage record. *Job:* the per-level clock as retail's
  (a session word beside the world's); `map_load` tearing the entity world down before the load;
  `elysium.load <slot>` as a console verb so a restore can be observed; resume at `+0x5c50` with the
  animating words restored (the divergence closed, not re-named); the four lifecycle chains read at
  their divergence and fixed there. *Consumed, named:* the flinch order stays a divergence until a
  hit producer dispatches slot 141 (0005's). *Tests:* arm — `NPCInit`'s late arm against a per-level
  clock; a restore resuming at the saved cursor with the clip at its cycle. Scenarios —
  **hidden-then-unhidden** (a maker child born hidden, unhidden by script, lands, sees, fights);
  **save-mid-schedule** (save during `RUN_PATH`, load, the same task continues). *Size:* M.
  *Model:* Opus/high. *Owns:* `ElysiumEntityWorld*` (clock, load), `ElysiumNpcBaseLifecycle*`,
  `ElysiumNpcLifecycle*`, `ElysiumEntityWorldPersistence.cpp`, `ElysiumNpcSenses.cpp` (the sighted
  admission), the debug subsystem (the verb).

### Part D — the program families (each absorbs its old rows; each has one scenario)

- [ ] **D1. The records and the input surface.** Old 25a (the dead `RequestClearSchedule` seam),
  10i's stale `-1` test, 12a's predicate arm, 10d's `+0x60dc` mirror, 16b's five flat-table reads
  swapped for `IRelationTypeOf`, 21c's `IsFeedAutoAcceptState` over the ideal activity, and **all 19
  unregistered NPC inputs** (`SetInvestigateMode(Combat)`, `StayEntrenched`, `AllowKickHintUse`,
  `FleeAndDie`, `Faint`, `MakeInvincible`, `SetFollowerBoss`, `WalkToNode` and the rest of retail's
  33, each bound to its landed body; the corpus fires them hundreds of times). Each is one line or
  one arm; done in one wave by one agent; one arm test where a line changes behaviour, one for the
  input table (every retail input name resolves). *Size:* S. *Model:* Sonnet/medium.
- [ ] **D2. Investigation: the sound list, the alert ladder, the programs.** Absorbs 0018/13
  (the shared `CSound` list with expiry and the type-bit producers: footsteps, doors, weapons,
  `ambient_generic sound_event`), 10d/10e/10f/10k (the arms are landed; the list is what they
  lack), 10h's `0xaf` builder over the hunt target and the hunt-patrol pathfinder seam,
  `SpeakVSound`'s table (`PLAY_SOUND` is silent today; the VSound table loads as data or the story
  records why not). *Retail:* `senses.md`
  § "The shared list itself" (64 records, drop-on-full, expiry + 4.0), `CommitBestSound
  0x102b4090`'s ladder, `0x102b1cd0`, the ladder `FUN_102b8980` on `m_eAlertLevel`. *Tests:* arm —
  the list's drop-on-full and expiry; `CommitBestSound`'s first-match order. Scenario —
  **footsteps-behind-the-block** (the player walks at 180/240 units: turn → step → look around →
  return to the stored position; then seen: `SEE_UNKNOWN` → `INVESTIGATE_UNKNOWN`). *Size:* M.
  *Model:* Opus/high.
- [ ] **D3. Places and patrols.** Absorbs 0018/10 (the registry, visitors, `Enable`/`Disable`, the
  trio as its API, the two outputs), 0018/11 (path records; the interest record is landed), 10g, 27,
  11's selector arms (landed) now live after C1. Also the hub's cop stall: the `copcar` prop and
  the three crossed `func_brush` windows into the collision contents (0018/3's marking, handed
  round three closed stories), RE-BACKLOG 45–47 (the door reads 0018/7 left), and two seams every
  walk reaches (§ Findings B): the arrival tolerance `0x102f2ea0` under `WAIT_FOR_MOVEMENT` and the
  interest-loop body. *Tests:* arm —
  `0x102daac0`'s two visitor arms; `NEXT_PATROL_POINT`'s advance and the spent path. Scenario —
  **place-visit** (C1's) plus **patrol-loop** (three points, ping-pong, an interest record taken
  by the strict `<` roll). *Size:* S–M. *Model:* Opus/medium.
- [ ] **D4. Cover, kick and the goal selectors.** Absorbs 0018/8's tick, 0018/9 (the cover search
  `0x10301720`, the lateral pre-check `0x102784a0`, the shoot node `0x10302e50`, back-away
  `0x10300b50` and its A* sibling, the hunt target `0x10306f60`, the cower arm, the `+1.0` cooldown
  claim), 12b (the kick chooser over `NpcKicked`, `FindKickPhysicsProp 0x102b6650` and the 10°
  predicate `0x102b62e0`, the impulse `0x102b6890` over 0005's physics seam), the weapon `+0x8c0`
  range consumer at `0x102b6b50`, `CAI_Hint::ObjectCaps 0x102d2ee0`, `FindLosPos 0x102edaa0` and
  the line-of-fire sweeps (seams on every combat think), RE-BACKLOG 39 (the player's `$contents`
  under a MONSTER ray) and 49 (whether restore re-runs `CAI_Hint::Spawn`).
  *Divergence kept, named:* candidates come from the place set
  nearest-first, reachability from Unreal (0018's boundary); the tests each place must pass are
  retail's, in order. *Tests:* arm — the cover search's six tests in order on a staged threat; the
  shoot node's strict bounds. Scenarios — **cover-and-fire** extended with a shoot node and a
  flank; **back-away** (melee retreat from a too-close player). *Size:* L. *Model:* Opus/high.
- [ ] **D5. Social: squads, followers, the coordinator, relationships, the logic entities.**
  Absorbs 0018/14, 17, 16a (`DIST:ACCUM`, `Npc_Follower_Info`, the input), 16b (the two flat-table
  consumers), 16c (the two apply-path calls), 0018/15 (the coordinator's three instances, cap 2,
  eviction), 0018/17 (the 16 hard-coded `AddClassRelationship` sites as a generated table; the
  three law stores), 0018/18 (`logic_npc_condition`, `logic_squad_condition`, `ai_changetarget`).
  *Tests:* arm — the squad's shared memory and the disconnect refcount; the follower ladder's
  three distances; the coordinator's eviction. Scenario — **squad-of-two** (one sees the player,
  the other gets `SQUAD_SEE_ENEMY` within 0.2 s and takes a melee slot; a follower keeps its band).
  *Size:* M. *Model:* Opus/high.
- [ ] **D6. Flee, cower, the player on the head.** Absorbs 21a (the two inputs, the `NPC_FLEE`
  row, the two raw-state reads, the flee-node search seams), 21c (`IsFeedAutoAcceptState` over the
  ideal activity), 28 (the player-side `PostThink` producer into `SetGroundEntity`). *Tests:* arm —
  case 8's chain order;
  the 2.0 s producer stamp. Scenario — **witness-and-flee** (a pedestrian sees a supernatural act,
  screams, runs to cover, cowers; the player stands on its head: dive). *Size:* S. *Model:*
  Opus/high.
- [ ] **D7. Makers and templates.** 0018/16: the maker adopted as its baked actor, keyfields from
  the datamap seam, the patch-first template set (36 files / 150 declarations), the inheritance
  rules against the hub's 48 and the tutorial's 14; hidden makers thinking (retail `Enable` on a
  hidden maker spawns while hidden; `+0xe4 m_pfnScriptSavedThink`, the one think record);
  `MemberSync 0x10337ca0` (reached once per child: `CVStatListManager::Update`, `m_iGender`).
  *Tests:* arm — inheritance and `Flag_*` lifecycle; the hidden maker's `Enable`. Scenario —
  **maker-cycle** (count, frequency, `InfChild`, `OnDeath` re-spawn). *Size:* S. *Model:*
  Sonnet/medium.
- [ ] **D8. The hub's species rows.** Old row 49: `CNPC_VCop`, `CNPC_VHuman`, `CNPC_VHunter`,
  `CNPC_VTaxiDriver`, `CCineAI`, `CCineAISchedule` — 65 unported `rule` rows (`Classify`, slots
  434/473/580, the taxi driver's damage chain), each an arm test. *Size:* S–M. *Model:*
  Sonnet/medium (Opus review).

### Part E — the witnesses

- [ ] **E1. The tutorial, played.** The sneak-past lesson end to end on `sp_tutorial_1` from a new
  game over MCP, with C1–D6's behaviours observed live: `thug_1` idles at `pt1`, hears, walks the
  ladder, sees inside the light scalar, commits, fires `OnFoundPlayer`; the stealth kill; the
  trance. 0 ensure / assert. Records what differs from retail and files it.
- [ ] **E2. The hub at idle.** 0018/20: the scene tests per map, the cook (the four editor-only
  guards), then 20 minutes played: pedestrians visit and cross, cops patrol, makers cycle, a save
  round-trips. Closes 0018/8's tick, 0018/7's handed acceptance and RE-BACKLOG 49.

### On demand (not in the sequence)

0018/12 the flying mover (no witness map among the six; `la_ventruetower_3`'s 70 claims are its
input); 0018/19 the debugger view (visual-only; A1's trace and `gr_hints` cover the need);
0018/21-8 / 21-9 / 21-10 (the corpus bake pass and the small-hull cell size); 0003/3–4 (the cine
entity's own lifecycle and the bodiless targets — 0003 keeps them); 0004's open rows; the species
outside the six maps' reach (`Weapon_Switch 0x1032dde0` for Ming Xiao and Bach, slot-166 dispatch,
the Werewolf fake-hull check, Andrei's fleshpile spawner, the controller's solidity and the
`0x1033f6d0` / `0x10170090` callers); 0018/6's leftovers (the closed-room fixture, `TestGroundMove`'s
stand chain, nav-filter arm (f), the path-test budget); `hw_warrens_4`'s `iris_clip` door; the ~280
`unported.tsv` rows outside both reach lists; `BeginNavigationJump`'s launch direction (dead by
content: no NPC can take a jump link).

## Sequence and waves

A1 → A2 ‖ A3 → C1 → C2 → C3 ‖ C4 → D1 → D2 → D3 → D4 → D5 → D6 ‖ D7 → E1 → D8 → E2.

Each story runs as: wave 0 the seam commit and the scenario (one agent, then me: build, red
scenario); wave 1 bodies in ≤3 lanes on disjoint files (one build, family filters); wave 2 review
by a fresh reader against the listing, fixes, the arm tests, the full suite once, the scenario
green, the ledger regenerated; tick. Budget per story: ≤5 agents, ≤2 builds, ≤2 full suite runs.

## What this closes

- 0019: closed already; its hand-offs (`handoff-story-8.md`) are placed: the think loop (landed),
  `CAI_Hint::ObjectCaps` (D4), the anim-event arms (C2), hidden makers never thinking and
  `MemberSync` (D7), the CCineAI slot-440 translation and `m_saved_troika_flags` (C1), Andrei's
  spawner, `Weapon_Switch`, slot-166 dispatch and the controller items (on demand).
- 0018: rows 8–20 absorbed above; 21-8/9/10 on demand; the spec closes with a pointer.
- 0003: stories 1–2 absorbed by C1; 3–4 stay in 0003.
- The tracker: rows 14–50 replaced by A1…E2.

## Decisions for the owner

Settled below by this spec's rules; each is a one-line confirmation or a reversal.

1. **The arbiter goes whole in C1**, including the `Sequence` and `Dialogue` owners, which means
   0003/1–2 land here now rather than later. The alternative (drop only the `Schedule` gate, keep
   the arbiter for scenes and dialogue) unblocks combat in a day but leaves two mechanisms and
   the event-order divergence; rejected by rule 4.
2. **Port-only tests are deleted, not converted** (B1). The ~45 owner assertions, the executor
   suites and the stub-count tests go with their mechanisms. Rejected: keeping them as "documentation".
3. **The arena scenario is a story's acceptance; map runs close a phase.** A story no longer needs
   a 20-minute live session to tick. E1 and E2 are where the maps are played.
4. **0018 and 0003/1–2 fold into 0002**; 0018's spec closes as 0019's did. Rejected: keeping
   three specs and one tracker, which is what produced the circles.
5. **0018/8 is ticked by D4**, not now: the claim chain was observed in the arena (run 6), but
   the combat-time selector that consumes it is D4's.
6. **Priority of C over D.** No program story starts until the body contract (C1–C3) is green in
   the arena. Rejected: finishing the XS rows first because they are small — they would be built
   on the gate.
7. **Model tiers as stated per story**; Opus for bodies against the listing, Sonnet for records
   and tooling, Fable for the seam commits and the reviews of C1 and C3.
8. **Resume, not restart** (C4): retail saves the task cursor and the animating words and resumes;
   the port's restart was a named divergence that releases twelve saved rows. Rule 1 decides it.
   Cost: the cursor and the clip's cycle join the save walk (schema bump; save files are disposable).
9. **The 75 seam assertions go** (B1): a seam's record is the ledger's seam list; a test that says
   "answers nothing" pins the absence of work, not a behaviour.
10. **The five port-only think steps go with the arbiter** (C1): `NPCThink` runs retail's phases
    and nothing else; what those steps did (the dialogue hold, the scripted beat, the watchdog, the
    policy routing) is either a retail program or nothing.
11. **The weighted sequence pick is ported** (C2). The clip resolver "carries no sequence weights"
    (`ElysiumNpcAnim.cpp:373`) but the `.mdl` sequence table does (`activityweight`, read by the
    pipeline's `mdl_skel` / `mdl_gltf`). C2's seam commit publishes the weight beside each clip the
    animation lane bakes and draws on the `NpcSchedule` stream; if a body's lane turns out to carry
    one clip per activity, that body's draw is trivially the primary and nothing is invented.

Open, needing the owner's word rather than a rule:
- Q1. Where does the consolidated spec live: replace `spec.md` in place (history to
  `record-2026-09-30.md`), or keep this file beside it?
- Q2. The lease (A2): keying it per checkout lets five checkouts build and test at once on one
  machine (32 GB; one editor peaks at 9–12 GB during a bake). Accept the memory contention, or
  cap concurrent editors at two with `--wait` queuing the rest?
- Q3. The VSound table (D2): load retail's `SOUND:` table as corpus data so `PLAY_SOUND` speaks, or
  leave NPC vocalisation silent until an audio story owns it?
- Q4. 0003/3–4 (the cine entity's own lifecycle, the bodiless targets): fold here too, or leave
  0003 open beside this spec?
- Q5. Reach beyond retail's graph (§ defect 7): the mesh walks every jump-only pair retail refuses
  and joins zones retail keeps apart. Under rule 2 navigation is the approximated layer, so the
  proposal is: keep the reach, record it per map as a named divergence, and revisit only when E1 or
  E2 shows an encounter reaching the player that retail's graph prevents. The alternative is to
  cut the mesh over each jump-only span (a nav modifier per pair, bake-time), which restores retail's
  sparseness at the cost of walkable floor the designers never blocked. Which?
- Q6. Scope of E1: play the tutorial only after D6 (the plan above), or run it once right after C4
  as a mid-course check (one session, findings filed, nothing fixed there) so Part D's stories start
  from a live baseline rather than the arena alone?

## Findings (the four audits; folded in as they report)

### Findings A — the test corpus
`consolidation/findings-A-tests.md` and `.tsv`. Headline: 1,324 tests in scope; 1,081 arm tests
with an address; 75 seam assertions; 28 port-only (16 arbiter); no test runs a walk-then-animate
program through `Think`; the witnesses drive `Tick`, not `Think`; duplication sits in five older
suites; `OverrideCensus.cpp` and `ElysiumTestServices.h` are the compile costs.

### Findings B — port-only mechanisms and the interface layer
`consolidation/findings-B-port-only.md` and `.tsv` (272 rows). Headline: the arbiter at 38 sites
in 7 files with the retail word for each; `RefreshStateFromOwner` writes `m_NPCState`; five
port-only think steps ahead of `MaintainSchedule`; 200 modernization/divergence comments, 71
state-changing, 22 of them labelled modernization; 814 seams answering nothing (combat 128,
navigator 123, anim 104), the 20 consequential ones placed above; 8 animating stubs on the live
path (slot 258 the worst); 177 direct Unreal lines in the substrate, 8 in the kernel, none through
`GetWorld()` — the interface layer holds.

### Findings C — tooling and time
`consolidation/findings-C-tooling.md`. Headline: the 15-minute ledger was fixed on `main` 09-28
and unrebased worktrees kept paying it; today `kernel --check` is 20 s and cacheable to <1 s; the
test runner is a 19 s boot around sub-second tests; the shared export-root lease refused 2,470
calls in a week; `pytest` is 6.4 min of which 80 % is 56 corpus tests; the MCP tools that blow a
context are named with their caps.

### Findings D — the landed stories
`consolidation/findings-D-landed.md` and `.tsv` (54 stories). Headline: 29 of 46 runtime stories
with no or an unobservable live check; the tutorial witness never played; combat, death and restore
never observed on a map; 22 of 42 "modernizations" change state or event order (the arbiter, the
reach beyond retail's graph, restart-not-resume the worst three); ~30 orphaned hand-offs, now
placed above; 0002/25c and 26 absorbed and never ticked.
