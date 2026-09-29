# Standing `dead` rows after wave 1, routed to the wave-3 lane that owns the file

Each lane: delete the body and its tests, or report a live observer for re-verdict (a `rule`
caller reading the value, a SAVE field, a script name). Address → target → port sites.

## Lane M (motor / facing / positions)
- 0x101a6b80 `GetJumpGravity` — BaseMotor.cpp:24, :408
- 0x10278cb0 `AddFacingTarget` — BaseFacing.cpp:60, Facing.cpp:41
- 0x10278e00 `GetFacingDirection` — BaseFacing.cpp:111
- 0x1027d9f0 `OverrideMoveFacing` — BaseFacing.cpp:120
- 0x1028b0b0 `IsValidShootPosition` — BasePositions2.cpp:129 (M owns this file for the row)
- 0x102c79e0 `StandoffTranslateActivity` — BaseMotor10.cpp:805, :893, Motor10.cpp:116
- 0x102c87a0 / 0x102c8830 / 0x102cdc50 `StandoffInputActivate` / `Deactivate` / `StandoffGoalUpdateOnRemove` — BaseMotor10.cpp:708-747, Motor10.cpp:112
- 0x10026e70 `BaseEntityIsMoving` — BaseMotor.cpp:441 (and `ElysiumMover.h:61`, the mover line's own `IsMoving`, which is OVERRIDDEN_BELOW and stays)

## Lane E (entity chain)
- 0x10026730 `Slot38` — EntitySlotBodies.cpp:459
- 0x10026f20 `ReflectGauss` — EntitySlotBodies.cpp:487
- 0x1009af00 `DebugGetClassName` — declaration only
- 0x101a6640 `GetLocalTaskId` — BaseEntityChain.cpp:55; **callers** BaseRunTask.cpp:313, BaseStartTask.cpp:266: read them; if a rule body reads the value, report for re-verdict
- 0x101a6ce0 `Slot579` — BaseEntityChain.cpp:103

## Lane P (hints / species)
- 0x1026a910 `GetHintDelay` — BaseHints.cpp:204, HintsShared.h:27

## Lane F (misc / lifecycle / schedule / anim)
- 0x1026d920 `RangeAttack2Conditions` — BaseConditions.cpp:31, :146, Conditions.h
- 0x1026da90 `MeleeAttack2Conditions` — BaseHelpers.cpp:105
- 0x10279060 `PlayScene` — BaseAnim.cpp:516
- 0x1027e120 `IsTemplate` — BaseLifecycle.cpp:30, :102
- 0x1028b0f0 `GetSlotSchedule` — BaseSchedule.cpp:163
- 0x102947e0 `Slot497`, 0x1029f890 `Slot23` — KernelBaseHelpers.cpp:690, :628
- 0x102c7600 `StandoffSelect` — BaseLifecycle.cpp:36, :406, :412
- 0x102cd2d0 (standoff goal `Spawn`) — BaseLifecycle.cpp:34, :229
- 0x103a7650 `PrescheduleThink` — Lifecycle.cpp:251

Closed in the ledger without a body (generated stubs): 0x101a6440 / 60 / 80, 0x101a64a0.
