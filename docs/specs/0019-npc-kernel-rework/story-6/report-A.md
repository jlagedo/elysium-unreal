# Lane A report: the Debug families

(Saved by the orchestrator from the lane's final answer; the lane could not create the file.)

**Deleted:** all 16 `Substrate/*Debug*` files and `Tests/ElysiumNpcKernelDebug{,10}Tests.cpp`.

**Prints cut:** BaseMotor 2, BaseRunTask 3 (one `TaskName`), StartTask_2 4 (plus the two kick formats), Misc2Species 3, RunAiSpecies 1, BaseStartTask 1 and RunTaskSpecies 1 (`TaskName`), Think 1 (`NPCThinkDebugPre`), ThinkSpecies 1 (ring dump), BaseBoss 1 (the whole slot-76 override). Also removed: Conditions10's debug-string family (5 bodies, all its constants), KernelBaseHelpers' `TraceMessageBare`, and `MotorResetToDefault` with its climb-arm call (`0x102e1110`: climb teardown only, and no shipped link climbs). Combat10 now uses the literal `2`. Both scrambler bodies are gone; `SecureUnscrambleLevel` had no caller. Tests that exercised these are gone too (Conditions10, Combat10, BaseHelpers, Lifecycle).

**Moved:** `RetailBonePosition` → `Geometry.cpp`, declared in `KernelBaseHelpers.inl` (Geometry.inl is not mine). `SequenceDescriptor` (RunTask `0x97` reads it), the nearest-node cluster (`Positions.cpp` and Zombie `0x150` read it) and `HintOverlayWords` (kept only for the dead `StandoffTranslateActivity`) → `KernelBaseHelpers.cpp`, declared in the new `ElysiumNpcKernelBaseHelpersBase.inl`. B holds the new definitions of `RetailFieldOfViewDot`, `PlayerHeightenedAlert` and `PlayerCopsInPursuitCount`.

**Against the brief:** no ManBat code calls `SecureUnhashLevel`; ManBat has its own scrambler.

**Refused:** none. No `rule` row targets a Debug-file method.

**Needs another owner:**
1. `ElysiumNpcBase.h:271-272`: replace with `#include "Substrate/ElysiumNpcKernelBaseHelpersBase.inl"`. `ElysiumNpc.h:984-985`: delete.
2. `ElysiumNpcBaseBoss.h:17` and `:28-31`: delete.
3. `ElysiumNpcDamaged.cpp:17` and `:45-54` (GetDebugName Warning prints): delete. `StartTaskSpecies.cpp:80` and `:3620-3622` (DevWarning): delete.
4. Leftover Debug-header includes and uses: EntitySlotBodies:32, CombatCharacterSlotBodies:35, Hengeyokai:23, MingXiao:34, Newscaster:21, Tzimisce:31-32, Werewolf:33-34 and :635, Zombie:26.
5. `Anim10Tests.cpp:98,653,773,1143,1297` (`SetDebugTraceByte`) and `RetailHullTests.cpp:101-104`: delete.
6. Stale declarations that do no harm: `Combat10.inl:112-120`, `BaseLifecycle.inl:98`, `Conditions10.inl:159-182`.

**Untouched (outside this brief):** other dead rows that cite my non-Debug files (Slot23/497, GetLocalTaskId, GetJumpGravity, IsTemplate, standoff, Crow). → orchestrator: swept as the wave-1 residue after the first regeneration.

**`targets-A.tsv`:** 24 rows, all `-`.
