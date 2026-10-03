# Findings B — port-only mechanisms and state-changing "modernizations" (2026-09-30)

Source paths are relative to `Source/ElysiumUE/Private/Substrate/` unless a path says otherwise. 272 rows in `findings-B-port-only.tsv`; the first column's `[n]` tag gives the brief section. Reach evidence from `docs/vtmb/npc-kernel/reach/`: `sp_tutorial_1` has 51 `CCineNPC`; `sm_hub_1` has 30 `CCineNPC`, 4 `CCineAI`, 1 `CCineAISchedule` and 28 `use_interesting` pedestrians. Every arbiter family except Follower is live on at least one of the two maps.

## 1. The body arbiter has no retail word

**Scale.** `EElysiumBodyOwner` touches **38 production sites in 7 files**: 9 claims, 15 release calls, 9 gates that read the owner, 5 calls through the script wrappers (`ElysiumScriptedCharacter.cpp`, `ElysiumScriptedSequence.cpp`); 8 debug reads; four test files pin it (`NpcMind.*`, `AiScriptedSchedule.*`, `NpcCombat.Chase/Death`, `NpcKernelSelect19.PatrolOutranks…`).

| Port site | Retail word |
|---|---|
| Sequence claim (`AcquireSequenceBody` and its 3 callers) | `m_hCine +0x5d74`, written by `PossessEntity 0x101a7880` with `SetState(4)`; admission `CanPlaySequence 0x10278090`, then `0x2e SCHED_AISCRIPT` with `m_scriptState +0x5d70` |
| Sequence release | `CineCleanup 0x1027d170`, `CancelScript 0x101a8c30`, `ExitScriptedSequence 0x1027d0a0`, `FixScriptNPCSchedule` slot 586 → `ClearSchedule 0x10280d30` |
| `ScriptedSchedule` claim | the order push `0x102ae840` (`m_eForcedState +0x65cc`, `m_bForceStateChange +0x1b28`), then executor `0x101a98c0` → `ScheduledMoveToGoalEntity 0x102800c0` / `ScheduledFollowPath 0x102801e0` |
| `Schedule` claim in `SetGoal` / `SetRandomGoal` / `AdvancePath` | none: the running program `m_pSchedule +0x5c38` owns the navigator |
| `Dialogue` claim | `m_hDialogPartner +0xfe8` / `IsInDialog 0x102c1170`, and `TASK_RUN_DIALOG 0xb9`; `COND_PROVOKED` ends the program, not the conversation |
| `Ambient` claim | the place's own claim, plus `SelectSchedule` case 1 `0x102af6f3` → program `0xff`; `OnScheduleChange 0x102a0940` releases the place |
| `Follower` | `m_hFollowerBoss +0x647c` and slot 607 `0x102b93c0`: a schedule, not a claim |
| `PlaySequenceClip` gate | none: `ResetSequenceInfo 0x10090950` plays on every body |
| Stance-transition gate | `SetDisposition 0x102c0f70`, gated only on `m_bDisableAI +0x6080`; writes `+0xff0 = 0xf1`, `+0x5ccc`, `+0x6f0`, `+0x6f8`, `+0xfec`, `+0x174`, then `ResetSequenceInfo` |
| `SaveBlockReason` | none: the datamap saves `m_NPCState`, `m_hCine`, `m_scriptState`; `DiscardScheduleState 0x1027be60` strips a dead cine |
| `SerializeMindBlock` | `m_NPCState +0x5cc0` / `m_IdealNPCState +0x5cc4` only |
| `ReleaseProgramBody`'s `Motor->Stop` | `OnScheduleChange 0x102a0940`, `ClearGoal` at `0x102a0992` under `!PRESERVE_PATH` (already ported, `ElysiumNpcMaintain.cpp:143`) |

What the arbiter changes that retail cannot observe the same way:
1. **`RefreshStateFromOwner` writes `m_NPCState`.** A Dialogue claim sets SCRIPT (4), and every release forces IDLE.
2. **Claim refusals add failure paths retail does not have.** A refused `Schedule` claim makes a move task fail; the `aiscripted_schedule` replay and the dialogue open can each be refused.
3. **The `PlaySequenceClip` gate is mislabelled a "named modernization".** On an owned body, a row that has never played gets `SequenceCycleRate 0`, so `m_bSequenceFinished` never rises and slot-251 waits hang. A program holds the `Schedule` owner from its first move until it ends.
4. **`Motor->Stop` stops the navigator a second time**, at the claim edge rather than on a schedule change.
5. **The parked slot is dead.** All four production `Acquire` calls pass `bSuspendCurrent=false`; `ForgetSuspended` has no callers; four comments still describe a parked patrol route, stale since story 8 wave 2.
6. **`Follower` is unreachable.** Nothing claims it and `IsAcquisitionAllowed` has no case for it.

## 2. Executors that are not kernel programs

`RouteScheduleMaintenance` (`ElysiumNpc.cpp:885`) runs five things ahead of `MaintainSchedule 0x102817c0` on every Troika think: `TickScriptWatchdog`, `ThinkInDialog`, `ThinkScriptOwned`, `ThinkSchedulePolicy`, `ThinkAutonomous`.
- **`ThinkAmbient`** has its own walk (24 cm tolerance, partial paths refused), a port-only `FailedSpotIndices` blacklist and port timers. Retail: program `0xff` plus the interest loop `0x102aa210`. While it owns every `use_interesting` body, no hub pedestrian can reach the crosswalk chain `0x100→0x102`.
- **`bReturnToExternalExecutorAfterSchedule`** leaves `MaintainSchedule` at three sites (`ElysiumSchedule.cpp:297/385/421`) with `Install(None)`. Retail installs the next program in the same pass.
- **`ThinkPatrol`** is gone.
- **`bMoveIssued`** stands for the navigator's `IsGoalActive` / `nav+0x14`; `TaskWalkRunPath` reads it for its activity choice — rename onto the navigator.
- **`bWalkingAnimation`** is written and never read: retire.
- **`ScheduleHost`** has 21 words with no offset, including `PendingFailureReason` (port-only), `Unknown6300` / `Unknown659c` and the eight think clocks.

## 3. `modernization` / `divergence` comments

200 distinct comments (spot-checked):

| Comment kind | Count | Visual-only | State-changing | Not a claim or stale |
|---|---|---|---|---|
| modernization | 68 | 45 | 22 | 1 |
| divergence | 132 | 60 | 49 | 23 |

The 71 state-changing comments cluster in six places: the scripted-beat stand-in (11 MOD); NavMesh standing in for retail probes and routes, including crosswalk curbs, the capped point pick and `TestGroundMove` (about 20); movement on the actor tick rather than inside `NPCThink` (4); the missing attack coordinator (4); maker orphan handling (4); the player-controller stand-in (3). All 22 state-changing MOD comments must be re-classed as divergences. Eight comments are stale: `ElysiumNpcSenses.h:204`, `ElysiumInterestingPlace.h:38`, `ElysiumNpc.cpp:1231`, `ElysiumScheduleText.h:35`, and four "fixed" notes.

## 4. SEAMs answering nothing

899 distinct SEAM comments; **814** answer nothing, false, `INDEX_NONE`, 0 or null (about 311 retail addresses; regex plus hand labels, ±5%). Inflated by stale declaration comments (`ElysiumNpcBaseSchedule.inl:9` says `ScheduleLocalToGlobal` answers -1, its body says the seam is CLOSED; `ElysiumNpcRunTask.inl:69` understates `RunDialogActivity`).

By family: combat/weapon 128, navigator 123, anim 104, physics/geometry 83, unattributed 53, entity/lifecycle 40, dialogue 31, damage 31, sounds 31, senses 30, squad 25, disciplines 24, cvar 23, hints 23, script 22, player/law 22, effects 17, schedule 2, memory 2.

**The 20 most consequential** (reached on both maps; the `[4]` rows): navigator — arrival tolerance `0x102f2ea0` under `TASK_WAIT_FOR_MOVEMENT` (armed 146/129), `FindLosPos 0x102edaa0`, the line-of-fire sweeps, the hunt-patrol pathfinder, the flee-node searches; combat and squad — the attack coordinator, the weapon capability word `0x1014f930`, the burst size and next-attack stamp, `m_pSquad`, squad enemy memory; also the `TASK_RUN_DIALOG` upkeep, which never fires `m_OnDialogEnd` from the task; the interest-loop body; `RequestClearSchedule` (0 callers); the `PlaySequenceClip` unknown-length row.

## 5. `STORY8-TWIN` markers and stub tallies

**`STORY8-TWIN`:** 5 markers in 3 files name 3 survivors: the beat, the dialogue clip hold and the ambient executor. All three are live on every Troika think.

**Animating and overlay stubs:** 44 (36 animating, 8 overlay); **8 on a live path**: slot 258 `DispatchAnimEvents` (`PostRun`, `ElysiumNpcBaseMotor.cpp:596`, calls the stub on every NPC think; events are dispatched instead by a world pass once per frame, `ElysiumEntityWorld.cpp:1889/2049` → `ElysiumAnimatingImpl.cpp:309`, on the Unreal clip phase and outside the think — event timing changes); `SetAttackExtentsForSequence` (every sequence change); `GetIdealYawSpeed` answers 0, so the turn rate floors at 1.0; `GetVelocity` answers zero; `BurnModel`, `SetPoseParameter02`, `GetGroundSpeedVelocity`, `AddExtraAnimationModels`.

**`ElysiumStub::Fired`:** 27 lines in 22 files. Live NPC ones: the registry-miss `IDLE_STAND` installs and six kernel arms; `SelectCoverOrKick`'s ranged-threat arm fires on every cover selection. The other five slot tiers hold 238 stubs; their liveness was not surveyed.

## 6. Interfaces and direct Unreal calls

| Interface | Methods |
|---|---|
| `IElysiumEmbodiment` | 131 (about 18 geometry/trace/PVS/light queries) |
| `IElysiumNpcMotor` | 34 (8 geometry) |
| `IElysiumAudio` | 17 |
| `IElysiumTravel` | 2 |
| `IElysiumPresenter` | 6 |
| `IElysiumWeather` | 6 |
| kernel-internal `IElysiumScheduleRunner` | 42 |

The sight policy (`ElysiumNpcSight`) is a namespace over `TraceRetail`. **Direct calls: 177 lines in 30 substrate files**, zero uses of `UAnimInstance`, `FCollisionQueryParams` or `GetWorld()`. Inside the NPC kernel 8: `ElysiumSchedule.cpp:280/514` `FPlatformTime` for the pass budget (nondeterministic, as retail's); `ElysiumAnimatingImpl.cpp:532/558/565/579/583/618` on `Visual`, visual only. Six draws from the global `FMath` RNG in the character idle (`ElysiumCombatCharacter.cpp:977–1037`, visual cadence). The other 169 lines are the entity tier (31) and props, movers, items.

## Unrecovered

Whether retail refuses a save while a conversation is open; the upkeep body of `0x102c1400`; liveness of the five non-animating slot-stub tiers; an out-of-scope state-changing divergence in `Visual/ElysiumNpcActivityTables.cpp` (the `Cover_Troika` row flagged at `ElysiumNpcClosure.cpp:395`).
