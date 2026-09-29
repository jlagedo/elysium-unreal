# Lane M · the Motor families (wave 3)

Read `README.md` first, then `report-S.md` (the seam surface wave 2 added; the header
`ElysiumWorldServices.h` is frozen — you may not edit it; if a seam is missing, report it).
You own, and only you may edit (under `Source/ElysiumUE/Private/`):

- `Substrate/ElysiumNpcBaseMotor.cpp`, `ElysiumNpcMotor.cpp`, `ElysiumNpcBaseMotor10.cpp`,
  `ElysiumNpcMotor10.cpp`, `ElysiumNpcMotorShared.h`, every `Substrate/*Motor*.inl`
- `Substrate/ElysiumNpcBaseFacing.cpp` and its `.inl`
- `Substrate/ElysiumNpcCombat10.cpp` (only the yaw-sweep caller at ~`:594`)
- `Substrate/ElysiumNpcConditions2.cpp` (only the `CheckOnGround` caller at ~`:203`)
- `Substrate/ElysiumNpcGeometry.cpp` (the trace funnel and `MotorSetOriginToTraceEnd` users; NOT `ResolveStandingOnHead`)
- `Substrate/ElysiumNpcWerewolf.cpp/.h` (its 18 `mechanism` rows and the fake-hull check)
- `Substrate/ElysiumNpcBaseThink.cpp` (the slot-584 trigger only)
- `Tests/ElysiumNpcKernelMotorTests.cpp`, `Motor10Tests.cpp`, `FacingTests.cpp`, `GeometryTests.cpp`,
  `WerewolfTests.cpp`, `Conditions2Tests.cpp`, `ThinkTests.cpp` for what you change

Not yours: `ElysiumNpcBase.h`, `ElysiumNpc.h` (report a declaration to remove), `ElysiumNpcMingXiao*.cpp`
(lane P; it deletes species `MaxYawSpeed` / `CheckStuck` overrides in step with your base decision),
`ElysiumEntitySlotBodies.cpp` (lane E), generated files.

## Rows

Every `mechanism` row in `docs/vtmb/npc-kernel/seam-list.md` § "Port bodies owed a seam" whose
"Port sites" names your files (Motor 22, BaseMotor 19, Motor10 5, BaseMotor10 5, MotorShared 4,
BaseFacing 11, Werewolf 18, Geometry's motor rows). Build the list first, one line per row.

## What the survey found (verify, then act)

- **No live caller, only tests:** `MotorMoveGroundStep` (`0x102e1760`, `BaseMotor10.cpp:476`),
  `MoveGroundExecute` → `MotorMoveGroundExecuteWalk` (`0x102e1560`, `:391/:419`),
  `NavigatorMoveNormal` / `NavigatorMoveGate` (`0x102efd50`, `:264-389`; the live path is
  `NavMoveNormalPass`, `BaseMotor.cpp:1067`), `MaxYawSpeed` (`Motor.cpp:275`, yaw 45/30 in
  `MotorShared.h:22-23`), `MotorStepToPoint` and the navigator count-only stubs (`:157-262`),
  `MotorApplyIntervalMovement` (`BaseMotor.cpp:155`, moves nothing). Delete body and tests;
  the service answers: ground step / walk execute → `CMC`; navigator move → `UPathFollowingComponent`
  (the live `NavigatorMoveStep` already forwards); yaw → `CMC` (`Face` on the motor drives
  `RotationRate`, `ElysiumNpcBody.cpp:592-601`). Their thresholds (0.5, 0.1, mask `0x202400b`,
  100.0 at `BaseMotor10.cpp:19-42`; 0.01; 45/30) : if a tunables row already holds them
  (`rg` `kernel_tunables.tsv`), nothing; else list the cell in the report for the orchestrator
  to add — the value stays reachable as data even when its consumer is gone (story 4's rule).
- **Own math to replace:** `CheckOnGround` (`BaseMotor.cpp:614`) → the floor facts seam from
  wave 2; the rule that reads the result (`Conditions2.cpp:203`) keeps retail's condition
  logic, only the measurement moves. `KernelHullTrace` (`:179`) is the shared funnel: keep it
  as the one call into `TraceRetail`; `MotorSetOriginToTraceEnd` (`BaseMotor10.cpp:195`) writes
  the origin itself: forward to the motor / actor transform seam (`SetOrigin` already forwards
  in `ElysiumEntitySlotBodies.cpp:131`; call the chain's `SetOrigin`, do not touch E's file).
- **`MoveLimit` = `MotorMoveTraceSweep` (`BaseMotor10.cpp:77`)** is already a `NavRaycast` seam
  call. Its arms `:115-122`: jump → route through the navigator's jump-link path (0018/5,
  `ElysiumNpcBodyNavigation.cpp:46`; legality per direction is row 13's — do not add it);
  fly → leave the admitting answer with a comment naming row 18 (0018/12) and the retail arm
  `0x102e…` (cite); climb → dead (story 1: `CAI_Motor` slots 3–5), the arm goes. The ground arm
  stays the NavMesh raycast, named as the divergence it is (0018/6 R1 §6): `TestGroundMove`,
  `CheckStep`'s per-step `CanStandOn` and the final delta-z rule do not run — write that in the
  comment at the arm with the three retail addresses.
- **Nav filter arms (f)** at `BaseMotor.cpp:280-282` ("NOT PORTED": `CNavPropertyDatabase`,
  slot 91, gamerules groups): these are trace-filter rules the collision service applies. Read
  retail `CTraceFilterNav` (`0x102eb170` first gate is unrecovered, RE-BACKLOG 39) and decide per
  arm: slot 91 is a chain slot (`docs/vtmb/npc-kernel/slots.md`) — if the port has a body for it,
  call it from the filter; `CNavPropertyDatabase` and gamerules groups have no port source →
  leave the seam answering "nothing" with the retail name (README rule). Do not build a database.
- **Werewolf fake-hull check** (`Werewolf.cpp:879-885`) compares `RetailCollisionExtents(*Enemy)`
  (a local box) where retail (`0x1039….`, cite from `functions.md` / `vtmb_func`) reads world
  bounds (`WorldSpaceCenter` ± extents, or `CollisionProp()->WorldSpaceAABB`). Fix to world
  bounds through the existing `SurroundingBounds` forward; one line plus a test.
- **Slot-584 tail:** the per-NPC 0.8 s pass `0x1028d8d0` is unwired (`BaseThink.cpp:50-53`); the
  body exists (`ElysiumNpcClosure.cpp:647`, E's file — call it, do not edit it). Read
  `0x1028d8d0` (`vtmb_func`) for the exact cadence and guard, wire it in `NPCThink`'s retail
  position, and test the cadence with the recording clock the Think tests use.
- The slot-523 accessor is still named `GetMaxJumpSpeed` (used as the stand drop at
  `Motor.cpp:143`): the rename is the orchestrator's (`signatures.tsv`); you only note every
  call site so the regenerated name can be applied.

## Deliver

`report-M.md` (≤300 words): rows closed by file and service, bodies deleted (names), seams
called, tunables cells to add, the fly / (f) arms left named, call sites of slot 523,
declarations to remove from `ElysiumNpcBase.h` / `ElysiumNpc.h` ("Needs another owner"),
`targets-M.tsv`.

## Added after wave 1 (read before starting)

- **Standing `dead` rows in your files:** `residue-dead.md` § "Lane M". Delete body and tests or
  report a live observer for re-verdict; add each closed address to your `targets-M.tsv` with `-`.
- **The seam surface wave 2 landed** is in `report-S.md`: `IElysiumNpcMotor::SampleFloor`
  (`FElysiumNpcFloorFacts`: on-ground, walkable, ground entity, normal, distance, step height, walkable
  Z / angle), `SetMoveIgnore(handle, bool)`, `HasPath()`. Step height and slope are on the FLOOR facts,
  not the move facts. The floor probe reaches ~step height + 2.4 cm; retail's `CheckOnGround` trace
  reaches 4.0 units, so the kernel tests `FloorDistanceCm` against 4.0 itself. The ignore seam is a
  list: a species `NavIgnoreCollision` that decided contact by contact must register its entities
  ahead of the move, and say so in a comment.
- **Lane A already moved** `RetailBonePosition` into `ElysiumNpcGeometry.cpp` (declared in
  `ElysiumNpcKernelBaseHelpers.inl`); the Debug files are gone. `ElysiumNpcKernelShapeMap.cpp` is a
  hand-kept ledger the bindings generator reads: a deleted word needs its row turned to
  `ELYSIUM_NPC_WORD_ABSENT` there (orchestrator does it; report the offset).
