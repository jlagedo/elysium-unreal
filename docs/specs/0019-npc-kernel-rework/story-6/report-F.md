# Lane F report · flex, overlay, test hull, makers, misc

(Saved by the orchestrator from the lane's final answer. Nothing built or run; corpus server down.)

**Closed (59 rows):** 0010: 11 Flex rows and `GetExpressionEventParams` (0x102c1c10). 0015: 7 Overlay rows. 0018/3: 4 TestHull overrides (`ObjectCaps`, `StepHeight`, `GetMaxJumpSpeed`, `IsJumpLegal`). Makers and InterestingPlace: 14 rows (save walk, Replication, C++ virtual dispatch, `CRT:operator delete`). Component factories: 7 rows (slots 424–430). Motor and navigator helpers in `BaseHelpers2`: 6 rows. Residue dead rows: 10. Tests trimmed.

**Contradicts the brief:** most Flex and Overlay bodies did real work, not refusals; none had a caller at run time, so they came out. No existing seam matched (`SetFlexControllers` needs a skeletal mesh the kernel does not hold). The live flinch already goes through `PlayReactionActivity`.

**Left, with reason:** `SetViewtarget` (0x100b5b00) read at `EntityWorld.cpp:1005`; 0x10098a40, 0x100994c0, 0x100995e0, 0x100997f0 read by StartTask and Misc; `FUN_102e1270` (RunTask calls it; the retail activity-0x30 force, slot 342, is unported and the motor's `Stop()` does more than retail's velocity zero); `FUN_102eea70` (StartTask calls it, `bNavFailed` is read); TestHull `Spawn` (0x102d72f0, a fixture spawns through it); Think / `ElysiumNpc.cpp` / `ElysiumNpcBase.cpp` rows are call sites only, bodies in M's and P's files.

**Fixtures naming the test hull:** `FSpawn19Fixture` (KernelSpawnTests); the class-factory, retail-hull, Motor and Motor10 tests; the NavigatorTests probe was removed with its test.

**Unrecovered:** whether the maker `HeadDirection` bodies (0x1034adf0 and siblings) are thunks or tail-calls to slot 368/369.

**Needs another owner.** `Public/ElysiumFlexSlotBodies.inl`: drop `FlexControllerRange`, `ReleaseSceneEvent`, `SceneEventReleases`. `Public/ElysiumAnimatingOverlaySlotBodies.inl`: drop `SetOverlayLayer`, `AllocateGestureLayer`, `NormalizePoseParameter`. `ElysiumInterestingPlace.h:55,68-69`: three declarations. Lane P: `BaseSaveRestore10.cpp:358` calls `CreateComponents`; `Rat.cpp:51-52` writes the two members kept in `BaseMisc.inl`. Orchestrator: generated default stubs for the dispatched slots 280 (`RunTaskSpecies.cpp:1470`), 424, 497, 540, 547, 554, 556; `LookupFlexController` has no caller; no reader left for tunables `TestHullJumpMax*` and `Forty`; knockback cells to record for 0010: 1.0/0.25/0.8/0.5 (0x102c1c10), 1.0/0.1/1.0/0.2 and 1.0/0.25/2.0/0.5 (0x10014ba0).

**Ownership flags:** edited `EntityChainTests.cpp` (lane E's): removed the flex and scene-event cases, replaced the `SetLayer` case with a hand-armed layer; edited the residue files outside the list; removed the three maker calls to `FElysiumNpcBase::Precache` (0x1027bb50, `Bake`). `targets-F.tsv`: 59 rows.
