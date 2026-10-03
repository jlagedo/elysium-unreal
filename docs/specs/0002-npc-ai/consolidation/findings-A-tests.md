# Findings A: the NPC / world-AI test corpus, audited (2026-09-30)

- **Scope:** 127 files in `Source/ElysiumUE/Private/Tests/`, 98,364 lines: 119 test `.cpp` files, 7 support headers, the generated `ElysiumNpcKernelOverrideCensus.cpp`.
- **Tests:** 1,324 declarations, all `IMPLEMENT_SIMPLE` except the `Mover` complex test (9 cases): 1,332 runnable cases.
- **Per-file data:** `findings-A-tests.tsv`, including every seam and port-only test name.
- **Method:** a pattern scan of every test body plus hand reads. Nothing was built or run. Categories (c) and (e) are pattern matches, ±10%.

## 1. Categories

| | What | Tests | Notes |
|---|---|---|---|
| a | Generated census, shape and bindings | **36** | `KernelShape` 6, `Bindings` 3, `ClassTree` 2, `SpeciesBindings` 5, `ChainSlots` 4, `ClassFactory` 6, `Tunables` 1; plus census-style cases elsewhere (`Closure.EverySlot…`, `ArmCoverage` ×3, `Species.SlotTable`, `RetailHull.Table`). Another **58** rule tests embed a census cross-check (`ElysiumNpcTestCensus::BodyOf`). `OverrideCensus.cpp` holds 1,050 generated rows and no tests. |
| b | Rule tests that cite a retail address and assert an arm's outcome | **1,081** | 1,089 tests cite at least one address. The `NpcKernel*` family files are almost all (b): one case per rule row, named for its address. |
| c | Seam tests: assert that a seam answers nothing, false, -1 or null | **75** (109 assertions) | Counted when the assertion message names the seam, "unrecovered", "no source" or "no producer". A looser match gives 116. Densest: `EntityChain`, `Damage`, `Combat10`. Recurring seams: squad, sequence lookup (-1), node/hint store, ConVars, studio header, attack coordinator, weapon capability word, navigator goal. |
| d | Tests that pin port-only mechanisms | **28** | Below. |
| e | Integration: the world loop runs with the NPC awake | **37** | Below. |
| – | None of the above | 213 | 120 NPC rules that cite a doc section but no address (`NpcSenses`, `Stealth`, `Witness`, the sweeps); 67 infrastructure, parser or player tests; 26 Content/Visual tests. |

**The 28 port-only tests (d):**
- **Body-owner arbiter, 16:** all 3 in `NpcMind`; `AiScriptedSchedule` ×4; `NpcCombat.Chase` and `.Death`; `NpcEnemy.StateMachine`; `Anim10.Slot359`; `KernelSchedule.TaskDistanceAndOrders`; `Select19.PatrolOutranksUseInteresting`; `KernelShape.FieldOwners`; `ThinkCadence.ResetSites`; `ScriptedSequenceBodyClaim`.
- **`ElysiumStub` tallies, 7:** `ChainSlots` ×2, `Slots.Defaults`, `Motor10.TestHullSpawn`, `Species.WiredSlot488DeathSound`, `AiScriptedSchedule.NamedSchedule`, `Npc.TeleportToEntity`.
- **Ambient and external executor, 4:** `KernelHints.Interest` reads `AmbientPhase`; `Maintain19.MaintainSchedule` reads `TakeExternalExecutorReturn`; `Select19.Case1Idle` and `StartTask19.Species.Animal` read `CurrentAmbientSpot`.
- **Dead path, 1:** `Npc.ActivityResolve` calls `FElysiumNpc::PlayActivity`, which nothing in production calls.
- **Never named in any test:** `ThinkAmbient`, `ThinkPatrol`, `bMoveIssued`, `bWalkingAnimation`, `FailedSpotIndices`, `FireAnimatingSlot`. `EElysiumBodyOwner::Follower` appears only in `NpcMind`.

**The 37 integration tests (e):** about 20 run the NPC think with a schedule installed (`AiScriptedSchedule` ×4; `NpcCrosswalk` ×3; `NpcTests` ×5; `KernelDirector` ×4; `ThinkCadence` ×3; `NpcEnemy.StateMachine`, `NpcSenses.Suppression`, `Think19.Species.Zombie`, `NpcMaker`). The rest are not NPC-kernel tests (Scene ×3, Sequence ×7, Stance ×2, the doors in `Mover`). Two suites never call `Think`: `ScheduleIntegration` calls `ElysiumSchedule::Tick` on a quiet NPC; `NpcWitness` calls `TickSight` and `GatherConditions` on quiet NPCs. The kernel family files never run the think: their fixtures quiet the NPC in the constructor.

## 2. Duplication

The 29c/29d/29e family files barely overlap. 351 kernel-ledger addresses are cited in 2+ files, but only **63** are asserted (in a test name or message) in 2+ files and **12** in 3+. The older suites (`NpcEnemy`, `NpcCombat`, `NpcSenses`, `AiScriptedSchedule`, `NpcTests`) re-assert functions the family files own:

| Address | Function | Files |
|---|---|---|
| `0x10270b20` | `GatherEnemyConditions` | 8 |
| `0x1029a0b0` | `NPCInit` | 7 |
| `0x1029adb0` | Troika `TaskFail` | 7 |
| `0x1032b9b0` | `Event_Killed` | 6 |
| `0x1029f5d0` | patrol release | 6 |
| `0x102ae920` / `0x102af660` / `0x10280e50` / `0x10269d30` | `PreSelectSchedule` / `SelectSchedule` / `SetSchedule` / `HasInterruptCondition` | 5 each |
| `0x102ee6a0` | `IsGoalActive` | 5, asserted in all 5 |
| `0x10279dd0` / `0x10279a50` / `0x102ad660` | `ChooseEnemy` / `SetEnemy` / `SelectIdealState` | 4 each |

File pairs sharing the most addresses: `Director`–`Script19` 11; `HintContent`–`HintSearch` 10 (by design); `AiScriptedSchedule`–`NpcEnemy` 8; `NpcEnemy`–`Damage19` 7.

## 3. The walk-then-animate defect

**No test would have caught it.** None runs a schedule through `Think` where a path task is followed by an activity task and checks that the activity plays or completes. None runs `TASK_DO_PATROL_INTEREST_ACTIVITY` or `TASK_DO_INTEREST_ACTIVITY`. None calls `PlaySequenceClip` or `ResetSequenceInfo` while the body owner is anything but `None`.

| Test | What it does | Where it stops |
|---|---|---|
| `Npc.ActivityResolve` (`ElysiumNpcTests.cpp:1300`) | Runs `FOLLOW_PATROL_PATH_WALK` through real `World.Tick`, checks the **walk** clip | Stops at t=1.0 mid-leg; the motor never reports `Reached`. Its activity half calls `PlayActivity` directly (owner `None`, no program, a dead function). |
| `ScheduleIntegration.ChaseFailureWitness` | Runs `CHASE_ENEMY_FAILED` to task 11 | Quiet NPC, driver `ElysiumSchedule::Tick`, no clip or owner check; `SET_ACTIVITY` and `WAIT` finish on the clock. |
| `NpcCombat.Chase` | Checks the chase claims the Schedule owner and **"the replacement keeps the schedule body owner"** | Treats the blocking state as correct. |
| `NpcCrosswalk.ThinkWaitsAtRed` | Real think over `WALK_TO_INTERESTING_PLACE` | Ends at the crosswalk. |
| `Select19.PatrolOutranksUseInteresting` | Patrol vs use-interesting | Sets `TaskIndex = 6`, skipping the walk and the activity. |
| `AiScriptedSchedule.MoveToGoal` | Real think; walks, arrives, owner returns to `None` | No activity after the move. |
| `RunTask19.Base.ScriptArms`, `StartTask19.Base.Activities…`, `RunAi19`, `StartTask19.Species` | The tasks that wait for a sequence | Set `bSequenceFinished` by hand. |

Several seam tests pin the sequence-lookup seams at -1 (`Facing.SetTurnActivity`, `Closure.CoverAndReloadActivity`, `Combat10.DodgeTest`, `Conditions.Werewolf`, `Anim.SceneEvents`), so the kernel's sequence path depends entirely on the `PlaySequenceClip` bridge the defect blocks.

**A test that would catch it** runs `FOLLOW_PATROL_PATH_WALK` with a motor and activity resolution on and checks: after `Reached`, a `PlayNpcClip` for the interest activity; `DO_PATROL_INTEREST_ACTIVITY` completing; `NEXT_PATROL_POINT` advancing. And `PlayNpcClip ACT_IDLE` after `WAIT_FOR_MOVEMENT` in `CHASE_ENEMY_FAILED`.

## 4. Cost

**Compile:** `OverrideCensus.cpp` 5,210 lines, 1,051 `TDeclaredOn<>` checks over 55 headers (the heaviest); four files with 111 includes each (`Movement` 2,073 lines, `NpcTests` 1,714, `Sequence` 1,658, `Scene` 1,503 — a leftover preamble from a split); the largest kernel files `StartTask_4` 2,190, `KernelSpecies` 2,016, `NpcCombat` 1,943, `Anim10` 1,595, `Combat10` 1,535, `Damage` 1,523; `ElysiumTestServices.h` (2,504 lines; pulls the skeletal-mesh, light and anim-instance headers) reaches 102 of the 120 in-scope `.cpp` files.

**Run time:** content/editor tier — `GeometryService` ×6 (tutorial collision + `CreatePhysicsMeshes` per case), `GeometryNav`, `NavArea` ×3, `NavJumpLink` ×2, `InfraContent` ×3, `HintContent` ×6, the Places/Crosswalk hub cases, `ScheduleCorpus.Deployed`; UWorld creators — `Mover` (9 worlds), `InfraActors` ×8, `NpcBodyMoveFacts` ×6, `NpcMotorSeam` ×4; headless high volume — every kernel case builds a whole entity world (`KernelDamage` 44 per file, `Motor` 36, `Script19` 34, `Director` 32, `Bosses` 30, `TroikaHelpers` 26); the census walks build one world per class.

## Not determined

Real compile and run times; the exact (c)/(e) boundaries; addresses cited inside a function not folded onto it; indirect pinning of port-only flags through behaviour; whether spawning an NPC loads the schedule corpus (16 files call it directly).
