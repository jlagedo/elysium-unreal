# NPC AI plan — incapacitation, the flag word, the interrupt kernel, followers, squads

**Plan of record for one thread of work, written so a fresh session can continue it without the
transcript.** It began 2026-09-07 as "a fed-upon NPC resumes activity at once" and became a
subsystem. It carries status on purpose (owner ruling: one file to track this work); the roadmap
rows it feeds are `13.5 Combat AI` and `9.9 NPC disposition` in `plans/gameplay.md`. When a section
lands for good, delete it here and let the roadmap carry the tick.

Owner rulings from the thread, binding on everything below:
1. **Build systems, not fixes.** A reported symptom is a question about VtMB; the deliverable is
   the whole retail behaviour with its substrate wired in (CLAUDE.md).
2. **A "defect" claim needs a proven, reachable path in shipped data**, not a mask read. The game
   shipped and plays. (See §4 for how that went.)
3. **Recovered behaviour is ported, never parked**; divergences are owner-named modernizations.
4. **One tracker.** Update this file; do not open another.
5. **The port is a VM host: retail bugs are reproduced verbatim.** Code above the host was
   authored against the bug. Modernization has two halves — visual-only (Unreal renders it
   better; adopt freely) and an algorithm Unreal already ships (adopt only with the retail
   contract and event sequencing kept). Nothing that changes event order or state is a
   modernization. Under this rule: pathfinding is Unreal NavMesh with the nav-type / failure
   contract kept; sensing order, schedule selection and think cadence stay retail (no
   AIPerception, no Behavior/State Trees).

The retail facts are at full length in `docs/vtmb/npc-ai-reverse-engineering.md` (sections "The
incapacitation tasks and the NPC flag word", "`DELAY_INTERRUPTS`, decoded", "`MaintainSchedule`,
walked", "Two Troika virtuals, identified", "`m_hFollowerBoss`", "The three `GatherConditions`
sweeps", "The base condition table", "`BuildScheduleTestBits`") and `docs/vtmb/feeding.md` ("Step
4, decoded"). This file is the digest, the port map and the queue.

---

## 1. The retail model, in one page

**The trance.** `CBaseCombatCharacter::FeedInterrupt` (`0x1033a9e0`), on a victim whose
`BloodPool` (stat index 0xc) is still ≥ 1: `npc = victim->+0x98; if (npc && npc->IRelationType(
attacker) != D_HT /*1*/) { npc->slot614() /*0x102c23f0: think timers := curtime*/;
npc->SetSchedule(0xfb, false); }`. `0xfb` = `SCHED_TROIKA_MESMERIZED`, blob at `0x105e6f40`, the
only producer in the shipped game:

```
TASK_MAKE_OBLIVIOUS TRUE; SET_NPC_FLAG D_IS_BUSY; SET_NPC_FLAG DONT_INVESTIGATE;
SET_NPC_FLAG NO_DIALOG; SET_ACTIVITY ACT_DISPOSITION_MESMERIZED; WAIT 30; WAIT_RANDOM 120
Interrupts: LIGHT_DAMAGE HEAVY_DAMAGE REPEATED_DAMAGE     Flags: DELAY_INTERRUPTS
```

30–150 s. The program cannot start until `LeaveGrappleState` clears the pair: `RunAlternateAI`
(`0x1028fd80`) returns true while `+0x1538` (partner) is valid and `+0x153c == 1` (victim), and
`NPCThink` (`0x10292de0`) then skips `RunAI` (slot `0x6c0`). It ends by being replaced: still
`D_IS_BUSY` when the waits run out, `SelectSchedule` (`0x102af660`) case 1 step 0 returns `0x6b`
`SCHED_TROIKA_IDLE_DISPOSITION`, and that install's schedule-change virtual releases everything.

**The flag word.** `m_bfAINPCFlags` `+0x14b8` / `m_bfAINPCFlags2` `+0x14bc`; 62 names parsed from
`NPCFlag:<name>` by `0x1030cbd0` (word two's return carries `0x80000000` as the routing marker).
`TASK_SET_NPC_FLAG` `0x100` (arm `0x102a585d`) / `TASK_CLEAR_NPC_FLAG` `0x101` are
`StartTask`-only. `D_IS_BUSY` bit 0 → its only reader is `IsBusyWithDiscipline` (`0x1033e2b0`;
17 callers incl. `SelectSchedule`, every dialogue entry, `BuildScheduleTestBits`). `NO_DIALOG` bit
19 → the dialogue gate (slot 295, `0x102c21c0` + `CPayphone` `0x101aaee0`) beside
`NO_DIALOG_PERSISTENT` (flags2 `0x10000000`, set only by `SCHED_TROIKA_D_AFRAID`/`D_POSSESSION`).
`DONT_INVESTIGATE` bit 26 → the interest predicate only. **Nothing in schedule data clears these
bits.** `OnScheduleChange` (slot 435, `0x102a0940`), first line of every `SetSchedule`
(`0x10280e50`) and gated on `PRESERVE_PATH`: `flags1 &= 0xbbf4b97e`, `flags2 &= 0x77fff14f`,
navigator/motor/goal reset, and if `MADE_OBLIVIOUS` was set, clear it and decrement.

**Obliviousness.** `TASK_MAKE_OBLIVIOUS` `0x131` (arm `0x102a72e3`; operand float, `TRUE`→1.0):
`flags2 |= 0x80001000`; `0x1026d130` = `SetEnemy(NULL)` + squad disconnect (`0x1026d050`) +
`++m_iIsOblivious` (`+0x5bb4`, int refcount, datamap `FTYPEDESC_SAVE`); `OnIncapacitatedStart`
(`+0x5fd4`). FALSE arm (`0x102a731a`, dead in data) mirrors with `OnIncapacitatedEnd`. Other
increments (cine `vfunc583`, grapple entry `0x1026cdc0`, fed-upon `0x1026cec0`) pair their own
decrements and do NOT set the bit. Consumers of the counter, exhaustive: `PerformSensing`
(`0x1026e4f0`) skips the whole sense pass; `UpdatePoseParameters` (slot 314, `0x102bf070`) drops
aim; the Nosferatu-witness predicate (slot 587, `0x1028ef20`) rejects; `CStealthKillRules::
FindVictim` (`0x101be1f0`) allows backstab from any angle.

**The interrupt kernel.** `DELAY_INTERRUPTS` is the only schedule flag (bit 0 of
`CAI_Schedule+0x18`; parser `0x1030d7e0`). Sole tester `IsScheduleValid` (`0x10280ff0`, inside
`MaintainSchedule`'s loop at `0x102819d5`): `if (!(!m_bDidMaintainSchedule && flags&1)) evaluate
mask`. `m_bDidMaintainSchedule` (`+0x5bb8`): 0 at spawn (`0x10273ad0`), 0 on every `SetSchedule`,
1 at `MaintainSchedule`'s single common-exit store (`0x10282342`) — **one think of immunity,
re-armed by every install.** `SetSchedule` also zeroes the 192-bit condition set at `+0x5c5c`.
`MaintainSchedule` (`0x102817c0`): bound 10 (1 when its bool arg is set), continues only while
tasks complete, reselects on `m_NPCState != m_IdealNPCState` regardless of the flag; its error
exit is harmless (runs after the loop's own `SetSchedule`). Effective mask = authored ∪
`BuildScheduleTestBits` (`0x102ad140`, see doc); `CacheInterruptConditions` (`0x1026a0f0`) always
adds `NPC_FREEZE` 0x75. `m_bfNPCStateFlags` per state (`0x1026e3e0`): idle 0x31, alert 0x39
(bits 4/5 set), combat 0x8f (clear).

**Interest.** `0x102b3270 ShouldInvestigate(candidate, bCombat)`: reject on
`DONT_INVESTIGATE|IN_FLEE_SCHED` (`0x4000080`), `stay_entrenched` (`+0x6435`), null, my
`m_hFollowerBoss`; the committed enemy → true; then `investigate_mode` (`+0x6338`) or with
`bCombat` `investigate_mode_combat` (`+0x633c`): 0 never, 1 hated players, 2 non-neutral players,
3 any player, 4 anything hated, 5 anything non-neutral, 6 anything, else DevWarning + false.
Shipped default 4. Callers: the see-unknown sweep `0x102b15c0` (0), the sound sweep `0x102b1cd0`
(world/physics/player 0, bullet-impact/combat 1), the vision producer slot 472 `0x102b3e00` (0).
Third `GatherConditions` call `0x102b1a20` = comfort list.

**Followers.** `+0x647c` = `m_hFollowerBoss`, resolved from keyfield `follower_boss`
(`m_sFollowerBoss` `+0x6478`; `follower_type` `+0x6480`). Writers: ctor (`0xffffffff`) and
`SetFollowerBoss` (`0x102c44e0`) only. Slot 607 (`0x102b93c0`), one dispatch in `SelectSchedule`
case 1 ahead of patrol: distance² to boss vs `+0x6484` backAway → `0x10c`, `+0x648c` runTo →
`0x113`, `+0x6488` walkTo → `0x112`, else `0x115`. `IRelationType` (`0x10299da0`) composes over
it and over a `D_INSANE` (flags2 `0x20000`) term.

**Cached self-pointers.** `+0x94` `CAI_BaseNPC*` (ctor `0x1027c300`), `+0x98`
`CAI_BaseNPCTroika*` (ctor `0x1028d230`), `+0x9c` `CBaseCombatCharacter*` (ctor `0x10326de0`).
`Disposition_t`: `D_ER 0, D_HT 1, D_FR 2, D_LI 3, D_NU 4`.

---

## 2. Landed and committed

First slice: `26a33d75` (committed by a peer session with camera work). Second slice: the
2026-09-07 commit that adds this file (`git log -- docs/project/plans/npc-ai.md`). Substrate
561/561, policy 10/10 at that point.

| Piece | Where |
|---|---|
| `SCHED_TROIKA_MESMERIZED` 0xfb, decoded mask, `DELAY_INTERRUPTS`; `BeginPostFeedTrance` with the `D_HT` guard | `Substrate/ElysiumFeedSchedules.{h,cpp}` |
| Producer wire (surviving branch) | `ElysiumFeed.cpp` `CompleteFeedTransaction` |
| Flag words, refcount, 62-name table, `OnScheduleChange` masks, `ParseName`, save | `Substrate/ElysiumNpcFlags.{h,cpp}` |
| `EElysiumTask::MakeOblivious`, `SetNpcFlag`; `FElysiumTaskStep::Flag` | `Substrate/ElysiumSchedule.h/.cpp` (`BeginTask` switch) |
| Runner verbs `MakeOblivious`, `SetNpcFlag`, `ClearConditions`, `OnScheduleChange`, `BuildScheduleTestBits` | `IElysiumScheduleRunner` + `FElysiumNpc` overrides (`ElysiumNpc.cpp`, after `RememberFact`) |
| `bDidMaintainSchedule`; `Start` = `OnScheduleChange` + `ClearConditions` + re-arm; delay gate; bound 10 | `ElysiumSchedule.cpp` `Start`/`Tick` |
| `EElysiumScheduleId::Mesmerized` + meta `{0xfb, "SCHED_TROIKA_MESMERIZED"}` | `ElysiumSchedule.h/.cpp` `MetaFor` |
| CHOSEN law conditions removed from the idle mask (overlay provides them) | `ElysiumSchedule.cpp` registry |
| `IsBusyWithDiscipline` override off the bit; `HasDialogSuppressFlag` = `NO_DIALOG \|\| NO_DIALOG_PERSISTENT` | `ElysiumNpc.h` |
| `NpcFlags` member, serialized in `SerializeScheduleBlock` ahead of the restore | `ElysiumNpc.h/.cpp` |
| Sense pass gated on `IsOblivious()`; `GatherSight` gated the same way (NOT on `DONT_INVESTIGATE`) | `RunConditionPass`; `ElysiumNpcConditions.cpp` |
| Trance exit: `SelectIdleSchedule` step 1 `\|\| IsBusyWithDiscipline()` | `ElysiumNpc.cpp` |
| Running program pre-empts patrol/ambient executors; `OnScheduleChange` motor stop unless `PRESERVE_PATH` | `ThinkSchedulePolicy`; `FElysiumNpc::OnScheduleChange` |
| `ShouldInvestigate` + `EElysiumInvestigateMode`; `InvestigateMode(Combat)` keyfields read | `ElysiumNpcConditions.{h,cpp}` |
| Condition enum: `SeeFear 0x44, SeeEnemy 0x46, HearDanger 0x6a, HearCombat 0x6d, HearWorld 0x6e, HearPlayer 0x6f, InvestigateLevel 0x1e, Comfort 0x27, SquadSeeEnemy 0x31, HearFlinch 0x72, NpcFreeze 0x75` | `ElysiumNpcConditions.h` |
| `IsFeedAutoAcceptState` answers the real `ACT_DISPOSITION_MESMERIZED` | `ElysiumFeed.cpp` |

Tests (all `uv run elysium test Elysium.Substrate.<...>`): `Schedule.Mesmerized`,
`Schedule.DelayInterrupts`, `Schedule.TestBitsOverlay`, `FeedTrance.Standing`,
`FeedTrance.Hostile`, `FeedTrance.Patrol` (not `Feeding.*` — a dotted prefix of an existing test
name turns that test into a tree branch that never runs; the commit hook checks this), `NpcConditions.InvestigateModes`,
`NpcConditions.InvestigateGates`. Tiers: `Elysium.Substrate` (~560, ~75 s), `policy`, `content`.

Not this thread's: `Source/ElysiumUE/Private/Player/*`, `Debug/ElysiumGreenRoomRun.cpp`,
`Substrate/ElysiumEntityWorld.cpp`, `pipeline/**`, `docs/vtmb/camera-view-modes.md`,
`docs/vtmb/choreographed_scenes.md`, `docs/vtmb/retail-defects.md`, `plans/spine.md`,
`roadmap.md` — a peer session's camera/light-store work. Never commit those from here.

---

## 3. Port map — how the pieces fit (read before building)

**Schedule kernel** (`Substrate/ElysiumSchedule.{h,cpp}`). A schedule = `FElysiumSchedule{Id,
Tasks[], FailSchedule, Interrupts, bDelayInterrupts}`; a task = `FElysiumTaskStep{Task, Param,
Activity, Target, Flag}`. Registry: `ElysiumScheduleFor(Id)`, `ElysiumSchedule::Register(...)`
from a domain file's static registrar (pattern: `ElysiumNpcCombatSchedules.cpp`,
`ElysiumFeedSchedules.cpp`). Identity: `EElysiumScheduleId` + `MetaFor` `{number, "SCHED_..."}` —
number 0 when the registration site is undecoded. Adding a task: enum member with its retail id
and arm address in the comment; `ElysiumTaskName`; a `case` in `BeginTask` (and `ContinueTask` if
it runs across thinks); a runner verb on `IElysiumScheduleRunner` (default = honest no-body
answer), `FElysiumNpc` override, and a recording override in the tests' `FRecordingRunner`
(`ElysiumScheduleTests.cpp`) / `FKernelRunner` (`ElysiumNpcEnemyTests.cpp`). `Start` is the
single install choke point (six callers) and runs retail's whole `SetSchedule` rule. `Tick`
checks interrupts once at the top (equivalent to retail's in-loop check because every install
zeroes conditions), then runs ≤10 task completions. `TASK_SET_SCHEDULE` transfers in place.
Failed → `FailScheduleOverride` or `FailSchedule` or end. **No `TaskFail` yet** (see §5).

**The NPC** (`Substrate/ElysiumNpc.{h,cpp}`, `FElysiumNpc : FElysiumScriptedCharacter,
IElysiumScheduleRunner`). `Think()` phases in order: `ThinkDead`, `RunAdmissionBarrier`,
`ResolveLoadout`, `ReplayDeferredScriptedOrder`, `RunConditionPass` (senses → `ElysiumNpcEnemy::
GatherConditions` → `UpdateIdealState`; an ideal-state change clears the running program),
`PumpStateChange`, `TickFeed` (a paired victim's think ends here — retail's `RunAlternateAI`),
`TickScriptWatchdog`, `ThinkInDialog`, `ThinkScriptOwned`, `ThinkSchedulePolicy` (combat /
scripted order / **any running program** pre-empt the executors → `ThinkStanceOrIdle`),
`ThinkAutonomous` (patrol / ambient / stance). `ThinkStanceOrIdle` ticks `Schedule` with
`Cognition.Conditions`, and on end releases the body and calls `SelectSchedule` →
`SelectIdleSchedule` (retail case-1 order: busy/choreo → follower [unbuilt] → patrol/ambient →
alert lookaround → door obstruction → return-to-initial → disposition idle) / `SelectAlertSchedule`
/ `SelectCombatSchedule`. `StartNamedSchedule(name, surface, detail)` is the forced-install door
(script `ChangeSchedule`, discipline `AI_Schedule`, the feed) and stamps `NextThink = now`.
State: `Mind.State()` (`EElysiumNpcState::Idle/Alert/Combat/Scripted/Dead`), `Mind.Owner()`
(body arbiter: `EElysiumBodyOwner::Schedule/ScriptedSchedule/Patrol/Sequence/...`),
`AcquireScheduleBody`/`ReleaseScheduleBody` (parks a patrol route). Memory: `Senses.Memory.Enemy`
(handle), `Senses.Memory.bPlayerLos/ClosestPlayer`, `Cognition.Conditions` (session-only, gathered
per pass). Relations: `Relationships.Resolve(handle, classname)` → `EElysiumRelationship::
Hate/Fear/Like/Neutral`; `SetEntity(handle, value, priority)`; derived D_HT rows from damage
(`ElysiumNpcEnemy`). Flags: `NpcFlags` (`FElysiumNpcFlags`), `IsOblivious()`,
`IsBusyWithDiscipline()`. Outputs: `FireOutput(FName, activatorHandle)`. Trace:
`RecordScheduleEvent` → `GetMind().Trace()` (ring buffer, ~20 rows); debug rows via
`GetDebugState(Rows)` (keys `Mind`, `Mind transition`, `Schedule`, `Conditions`, `Body owner`,
`In dialog`, ...). Save: `SerializeScheduleBlock` (schedule identity only + `NpcFlags`; a restore
re-`Start`s the program, whose `OnScheduleChange` releases and whose first tasks re-set).

**Feeding** (`Substrate/ElysiumFeed.cpp`, methods on `FElysiumCombatCharacter` in
`Public/ElysiumPlayer.h`). `AttemptFeed(victim)` → `StartFeedPair`; `TickFeed` advances from the
feeder's think; `FeedInterrupt()` → `CompleteFeedTransaction(bKeepReleaseTail)`: reads
`BloodPoolValue()`, fires `OnFedUponEnd`, depleted → `OnKilled()`, else
`ElysiumFeedSchedules::BeginPostFeedTrance(victim, *this)`, then `EndFeedGrapple()`.
`IsFeedAutoAcceptState()` = running Mesmerized, else the disposition-name stand-in for
`ACT_DISORIENTED/LOST/COWER` (still open — no producers).

**Test fixtures.** Headless world: `FElysiumRecordingServices Services; FElysiumEntityWorld
World(nullptr, nullptr, Services.Bundle());` — set `Services.bProvideNpcMotor = true` for a motor
and **`Services.bNpcActivitiesResolve = true` or every `TASK_SET_ACTIVITY` fails** (the double's
default is the old-export fallback). NPC def needs a `model` key
(`models/character/npc/common/blueblood/male/Blueblood_Male.mdl`) to get a body. Sequence:
`World.Load(defs); SpawnPlayer(); Activate(0); Tick(0)` (admission), then two forced thinks
(`Npc->NextThink = 0; World.Tick(t)`), then `Npc->NextThink = ELYSIUM_NEVER_THINK` to quiet.
Patrol: `World.AcceptInput(TEXT("!self"), FName("FollowPatrolPath"), FElysiumVariant::String(
"route_1 route_2"), h, h)` with `info_node_patrol_point` defs. Feed: `Player->AttemptFeed(*Npc)`
after `Npc->Disposition = TEXT("cower")` (auto-accept), advance with `World.RunPlayerThink(t);
World.Tick(t)`, cut with `Player->FeedInterrupt()`. Outputs: wire `OnX → math_counter Add 1` in
the def and read `ElysiumSaveTestHelpers::SaveTestCounterValue(World.FindByName(name))`. Motor
recording: `Services.Saw/Count(TEXT("NpcMotor MoveTo"/"Face"/"Stop"/"Launch"))`. Full example:
`Tests/ElysiumFeedTranceTests.cpp` (`FTranceFixture`), `Tests/ElysiumAiScriptedScheduleTests.cpp`
(`FAiScheduleFixture`), `Tests/ElysiumNpcInvestigateTests.cpp`.

**Corpus tooling** (MCP server `vtmb-corpus`, deferred — load with
`ToolSearch("select:mcp__vtmb-corpus__vtmb_grep,...vtmb_func,...vtmb_code,...vtmb_asm,
...vtmb_callers,...vtmb_callees,...vtmb_string,...vtmb_fields,...vtmb_readers,...vtmb_slot,
...vtmb_vtable,...vtmb_globals")`). `vtmb_grep` prints ≤6 lines per function; `vtmb_readers`
(typed + untyped) is the "nothing else touches this" proof; `vtmb_slot(index = byte offset / 4)`;
**vtables are capped at 600 slots** — for offsets ≥ `0x960` read the pointer from the pinned DLL
(`E:\dev_game\Vampire The Masquerade - Bloodlines\Vampire\dlls\vampire.dll`, base `0x10000000`,
sha256 `c546f4de…a76f`) with a python PE section walk and follow the `E9` thunk;
`vftable_CAI_BaseNPCTroika` `0x1049a25c`, `vftable_CAI_BaseNPC` `0x104995c4`. Schedule blobs are
NUL-terminated ASCII `\n\tSchedule\n\t\tSCHED_…\tTasks\t\t…` — byte-scan them. Jump tables the
decompiler drops: `CAI_BaseNPCTroika::StartTask` `0x102a1910` (head `0x102a193c`, idx = id−5,
byte `0x102a7ab8`, dword `0x102a77f8`), `CAI_BaseNPC::StartTask` `0x102827f0` (idx = id−1, byte
`0x10287138`, dword `0x10286f8c`), `CAI_BaseNPC::RunTask` `0x10288780` (idx = id−2, byte
`0x10289794`, dword `0x10289724`), `CAI_BaseNPCTroika::RunTask` `0x102aacf0` (idx = id−2, byte
`0x102ac844`, dword `0x102ac760`). Registrars: tasks `FUN_10316ff0` (ids 0–276) and the Troika
one at `0x1031918a` (277–329); conditions `0x102c8ce0`; schedules `FUN_102b9810`; activities
`FUN_104126e0`; NPC flags `0x1030cbd0`; schedule flags `0x1030d7e0`. Recovery agents: Opus,
general-purpose, given the tool list, the established facts, and a "cite addresses / state what is
unrecovered" contract; for contested claims run a prosecution and a defence in parallel.

**Environment.** `uv run elysium build` / `uv run elysium test <filter>`; a peer session often
holds the editor or the "generated-state lane" — retry, never kill it (`TaskStop` for your own
runs only). Build is blocked while any editor process has the project. Bash truncates ~8 KB; write
files with Write/Edit.

---

## 4. The one contested claim — settled

**The `TaskFail` refcount leak: verified BOUNDED** by a prosecution (PROVEN) and a defence
(BOUNDED) that agreed on every fact. Mechanism, from the listing: `CAI_BaseNPCTroika::TaskFail`
(slot 448, `0x1029adb0`; base `CAI_BaseNPC::TaskFail(const char*)` `0x10273fc0` with the
`"TaskFail -> %s"` DevMsg, reason table `0x106152b0`, `SetCondition(0x5c)`) applies `flags2 &=
0x7fffe24f` at `0x1029aeb2` (clears `MADE_OBLIVIOUS`) and `flags1 &= 0xa3f40178`, never
decrementing `+0x5bb4`; `OnScheduleChange` uses `0x77fff14f` and decrements on the kept bit. No
other writer of `+0x5bb4` exists; it is saved. **Reachability is one corner:** across every task
following `TASK_MAKE_OBLIVIOUS` in all 35 shipped schedules, only `TASK_STOP_MOVING` (34/35) can
fail — `FAIL_STUCK_ONTOP` at `0x10288963`: active nav goal, `NAV_JUMP`, not `FL_ONGROUND`,
`|v| ≤ 0.01` — an NPC wedged mid-air when a discipline forces the schedule (`CAI_Navigator::
OnNavFailed` `0x102eeae0` is a second source). **`SCHED_TROIKA_MESMERIZED` has no
`TASK_STOP_MOVING` and cannot leak.** A leaked NPC looks normal (TaskFail's flags1 mask clears
`D_IS_BUSY`/`NO_DIALOG`/`DONT_INVESTIGATE`) but never senses again and is stealth-killable
face-on — silent, consistent with a shipped game. Troika authored `TASK_SET_FAIL_SCHEDULE
Idle_Stand` on 22/35 and `TASK_SET_PRESERVE_PATH 0` on 33/35.

Ruling (owner, 2026-09-07, under ruling 5): **not a blocker; port the leak verbatim.** When
`TaskFail` is built, apply both masks exactly — `MADE_OBLIVIOUS` cleared, `+0x5bb4` NOT
decremented — and write a trace row when the bit was set so the leak stays observable. Record it
in `docs/vtmb/retail-defects.md` as "bounded: NAV_JUMP + discipline; reproduced" **at that time**
(that file is currently a peer's working copy; do not touch it until their commit lands).

---

## 5. Build queue, in order, in plain names

Order under ruling 5: kernel failure gaps → think cadence → followers → sweeps → squads.

### Next: the schedule kernel's two failure gaps

**5a. `TaskFail` — the failure virtual.** Retail slot 448. Base `0x10273fc0`: record the reason
(`+0x5c50`; name table `0x106152b0`, e.g. `0x05 "Schedule not found"`, `0x17 "No player"`,
`0x1c "Stuck on top of something"`, `0x29 "NPC had no follower boss"`), clear `m_bShouldMove`,
DevMsg, `SetCondition(COND_TASK_FAILED 0x5c)`. Troika `0x1029adb0` before chaining: the
interesting-place teardown (`0x102b53d0`, "Leaving interesting place (TaskFail)"), clear
`PRESERVE_PATH` unless nav type is CLIMB/JUMP, motor speed/yaw reset, all four think stamps :=
curtime, `m_flGoalTolerance = 0`, interrupt distances 0, `m_hMoveTargetEnt` cleared, kick-prop
handle released, `m_afMemory &= 0x0fffffff`, `flags2 &= 0x7fffe24f`, `flags1 &= 0xa3f40178`,
`SLEEP_BOUNDING_BOX` restore, `ClearHintNode(5.0)`, `m_bPatrolPathUseHint = 0`. Port: the kernel's
`Failed` result calls a runner verb `TaskFail(reason)` before routing to the fail schedule;
`FElysiumNpcFlags::OnTaskFail()` holds the two masks verbatim (ruling in §4: the refcount is
NOT decremented; trace row when the bit was set);
`COND_TASK_FAILED` (0x5c) and `COND_SCHEDULE_DONE` (0x5d) join the condition enum. Also add
`FAIL_STUCK_ONTOP`'s test to `StopMoving`: the port has the airborne state (`AElysiumNpcBody::
Launch` sets `MOVE_Falling`, `bLaunched`) — `TASK_STOP_MOVING` fails when launched, not on ground,
`|v| ≤ 0.01`. *Tests:* kernel test that a failing task records the reason and the fail schedule's
install releases flags; `OnTaskFail` leaves the refcount as retail does (a test asserts the
leak). *Deps:* none.

**5b. `TASK_SET_ACTIVITY` completes on a miss.** Retail arm `0x102a1c0f` (id `0x4b`) has no fail
path; the port fails the task on an unresolvable activity. Fix: complete, record the miss in the
trace, let the body's own ladder (`animation_and_movers.md`) answer; then drop
`bNpcActivitiesResolve = true` from the trance fixture and confirm it still holds. *Deps:* none.

### Then: the Troika reduced-think mode (the VM clock — before followers, so their tests run on the right cadence)

`CAI_BaseNPC::RunAI(bool)` `0x1026f110`; `CAI_BaseNPCTroika::NPCThink` passes
`!(m_flNextAIThink − curtime < frametime)` (`0x10290700`/`0x10290660`): set → skip
`GatherConditions` (slot 433), `MaintainSchedule` bound 1, skip the end-of-pass clear of
`LIGHT_DAMAGE`/`HEAVY_DAMAGE`/`WAS_BUMPED` 0x38. Cadences `m_flNextUpdateThink` `+0x6244`,
`m_flNextNormalThink` `+0x6248`, `m_flNextAIThink` `+0x6250`; the writers are `0x10290720`/
`0x10290b60`/`0x1029bd40` in `NPCThink` — recover them first. *Deps:* none.

### Then: followers (`m_hFollowerBoss`)

Parse `follower_boss` / `follower_type`; `SetFollowerBoss(name)` through the `!player`/`!self`/
`!enemy` resolver with the retail refusals (self; squad member → `Error`); the `SetFollowerBoss`
entity input (`0x102c3350`); `CNPC_VPedestrian::Activate` clears it; `Npc_Follower_Info` from
Rules.txt (`FUN_102c4680`: `FollowerDistanceBackAway/WalkTo/RunTo` per type, clamps `walkTo ≥
backAway + overlap`, `runTo ≥ walkTo + overlap`); slot 607 as `SelectIdleSchedule`'s step between
busy/choreo and patrol; the four programs `SCHED_TROIKA_FOLLOWER_BACKAWAY` `0x10c`,
`_FOLLOW_WALK` `0x112`, `_FOLLOW_RUN` `0x113`, `_WAIT` `0x115` (blobs from `0x105e3100`; they use
`COND_INSIDE/OUTSIDE_INTERRUPT_DIST_F` 0x19/0x18 — needs the interrupt-distance producer in
`GatherConditions` via `GetFollowerBoss`), tasks `0x86–0x88` `TASK_FIND_FOLLOWER_BACKAWAY_
{SIMPLE,NODE,ASTAR}` (fail 0x29 when the boss is dead); `GetFollowerBoss()` (slot 293). Then
**`FElysiumNpc::IRelationType(target)`** (`0x10299da0`): self → D_ER; target NPC with `D_INSANE`
+ my closest player not hated and not my enemy → D_HT; target's boss hated/my enemy → D_HT; my
boss == target → D_LI; else inherit `boss->IRelationType(target)`, upgraded to D_HT if the boss
hates/targets it or it hates/targets the boss; else base table — and route the feed guard
(`BeginPostFeedTrance`) and `GatherSight` through it. Also `CNPC_VHuman::SelectIdealState`
(`0x103851e0`): enemy gone → follower to alert (idle under `no_alert_state`), non-follower to the
hunt state `0xb` when the ConVar at `DAT_1092447c` is on; the interest predicate's boss reject;
`CBasePlayer::UpdateClientActionState` (`0x101755d0`/`0x10174580`): a follower reads as ally on
the target HUD. *Acceptance:* a `follower_boss !player` NPC walks/runs/waits by the radii; the
feed guard uses the composed relation. *Deps:* 5a for task 0x86–0x88 failures.

### Then: the three condition sweeps

See-unknown `0x102b15c0` (`m_hBestSeeUnknown` `+0x6088` via slot 586, player-only; 1.5 s grace
`+0x6084`; `m_vecLastSeeUnknownPos` `+0x6090`; one-shot `MADE_INITIAL_RESPONSE` roll over
`m_iSeeUnknownRepeatSightings` `+0x60a4` and `full_investigate` `+0x6340` setting
`ATTACK_UNKNOWN`/`IGNORE_UNKNOWN`; 2-D closing speed vs `20.0f` → `UNKNOWN_ADVANCING/HOLDING/
RETREATING` 0x05–0x07 — reproduce the dead `HOLDING`; `INVESTIGATE_SIGHT` 0x26; producer slot 472
`0x102b3e00` sets `SEE_UNKNOWN` and fires `OnUnknownVisionPlayer`). Sound `0x102b1cd0` (six
records: `m_LastSoundWorld` `+0x61e4`, `PhysicsDanger` `+0x6134`, `Danger` `+0x6108`, `Player`
`+0x61b8`, `BulletImpact` `+0x618c`, `Combat` `+0x6160`; gate `m_flNextInvestigateSoundTime`
`+0x623c`; arm = `HasCondition(HEAR_X) && (schedule interrupts on HEAR_X || ShouldInvestigate)` →
`INVESTIGATE_SOUND` 0x25; `HEAR_DANGER` skips the predicate; last-wins combat > bullet > player >
danger > physics > world; `HEAR_FLANK_SOUND` 0x33; `SEE_SOUND_SOURCE` 0x2d tail rate-limited by
`+0x6418`). Comfort `0x102b1a20` (idle only, 0.2–0.4 s, global `AddToComfortList`
`0x10323630`/`Remove` `0x10323770`, nearest ≤ 1024 u, ≤3 per target via `+0xE94`, `COMFORT`
0x27). These are `ShouldInvestigate`'s callers. *Deps:* the 32-program `INVESTIGAT` family is
undecoded — decode it (byte-scan + `FUN_102b9810`) so the conditions have something to select.

### Then: squads

`CAI_Squad` at `+0x5da4`, `m_iSquadDisconnected` `+0x5bb0`, leave `0x10316700`, rejoin
`0x10009601`, `D_DISCONNECT_SQUAD`, the `SQUAD_SEE_ENEMY` 0x31 / `SQUAD_NEW_ENEMY` producers,
`IGNORE_SQUAD_SEE_ENEMY`, the `squadslot` namespace (table `0x10920484`). The disconnect/rejoin
in `MakeOblivious`/`OnScheduleChange` are named seams today. *Deps:* followers (they refuse
squads).

### Elsewhere: jump links (world plan)

The `.ain` node graph is decoded (`pipeline/.../formats/nav_graph_glb`, links' 23 fields raw)
but not consumed; retail's jump waypoints (`CAI_Navigator` `0x102eed6f` sets `NAV_JUMP`) are UE
`NavLinkProxy`s the bake does not emit. Belongs in `plans/world.md`; noted here so it is not lost.

---

## 6. Still open from the trance itself

- `ACT_DISORIENTED`, `ACT_LOST`, `ACT_COWER` auto-accept states have no producers
  (`IsFeedAutoAcceptState` still uses the disposition-name stand-in for them). The producers are
  `SCHED_TROIKA_DISORIENTED` (the `D_*` chain target), `LOST` and the `COWER` family (24 programs).
- `OnIncapacitatedEnd` fires only from the dead FALSE arm and the cine/grapple exits — port matches.
- Live check (owner-piloted): feed a standing civilian on `sp_tutorial_1`; expect the trance on
  the first think after the release clip, conversation refused, a second feed accepted with no
  roll, and a normal idle after 30–150 s.

---

## 7. Unrecovered

Slot 587's authored spelling and parameter (project name `CanWitnessSupernatural()`); slots 434,
436, 445, 447 in the `MaintainSchedule`/`RunAI` chain; `CAI_BaseNPCTroika+0x6081`,
`+0x60dc/+0x60e0` (`m_InvestigateSound`, types 1 and 0x10), `m_flNextInvestigateSoundTime`'s
writer; `m_bfAINPCFlags` bits 21/25 setters; `m_bfNPCFrenziedFlags` bits `0x8`/`0x10`/`0x2000`;
`CBasePlayer+0x1ED0`/`+0x1ea8` SendProps and `+0x1538/153c/1540` on the player; the ConVar names
behind `DAT_1092447c`/`DAT_10924f74`; ~20 derived-class condition tables (ids > 0x76); the
`Disposition_t` behind `COND_SEE_DISLIKE`; discipline record fields `+0x370/+0x371` ("make
follower" vs "calm"); `stay_entrenched` (unparsed); hint type `0x2774`; whether `CNPC_VGargoyle`
(`0x10378fc0`), `VHengeyokai` (`0x10383090`), `VTzimisce` (`0x103bf610`), `VWerewolf`
(`0x103ced10`) chain their slot-435 overrides to the Troika `OnScheduleChange`; the generic
flag applier `0x101de6e1`/`0x101def10` (mask from `[obj+0xa8]`, zero recovered callers).
