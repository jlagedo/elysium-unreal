# Report S · the seam surface

(Saved by the orchestrator from the lane's final answer. Nothing compiled or tested by the lane.)

## New surface (`Source/ElysiumUE/Public/ElysiumWorldServices.h`, additions only)

- **`struct FElysiumNpcFloorFacts`**: the answer to the trace in `CheckOnGround 0x1026e5e0`.
  `bOnGround` (retail's fraction != 1.0); `bWalkable` (the standable-normal test); `GroundEntityHandle`
  (retail's `tr.m_pEnt` into `m_hGroundEntity +0x384`; unset = the static world); `FloorNormal`
  (`tr.plane.normal`); `FloorDistanceCm`; `MaxStepHeightCm` (slot 522 `StepHeight 0x101a6b40`);
  `WalkableFloorZ` / `WalkableFloorAngleDegrees` (retail's standable normal 0.7).
- **`bool IElysiumNpcMotor::SampleFloor(FElysiumNpcFloorFacts&) const`**: false by default (no movement component).
- **`void IElysiumNpcMotor::SetMoveIgnore(const FElysiumEntityHandle&, bool)`**: stands for
  `m_hIgnoreCollisionEntity +0x55c`, set by `StartIgnoringCollision 0x1008bb70`, cleared by
  `StopIgnoringCollisionWithEntity 0x1008bd30`.
- **`bool IElysiumNpcMotor::HasPath() const`**: stands for `CAI_Navigator::IsGoalActive 0x102ee6a0`.

## What the live body reads (`Visual/ElysiumNpcBodyGeometry.cpp`, declarations in `ElysiumNpcBody.h`)

- **Floor:** a fresh `UCharacterMovementComponent::FindFloor` (`CurrentFloor` goes stale on a sleeping or
  teleported body). Also reports `MaxStepHeight`, `GetWalkableFloorZ`, `GetWalkableFloorAngle`. The hit
  becomes an entity handle the way `HandleForActor` does it.
- **Ignore:** `IgnoreActorWhenMoving` for an NPC body or the player's pawn; `IgnoreComponentWhenMoving`
  for a brush or prop component on the owning map.
- **HasPath:** the path follower is not Idle and `HasValidPath()`.
- **Recording motor (`Tests/ElysiumTestServices.h`):** `Floor` defaults to retail's numbers
  (`StepHeightBase * U`, 0.7); `bReportsFloor` switches it off; `FloorSamples` counts; `MoveIgnored`
  holds the ignore set and each call is recorded; `HasPathOverride`.

## Tests (`Tests/ElysiumNpcMotorSeamTests.cpp`, new)

`Elysium.Visual.NpcBody.MotorSeam.Floor` (on the floor, then 3 m up), `…MotorSeam.MoveIgnore`
(set, cleared, unknown handle), `…MotorSeam.HasPath`, `Elysium.Substrate.NpcMotorSeam.Recording`.

## Departures from the brief

- Step height and slope sit on the floor facts, not on `FElysiumNpcMoveFacts`: `SampleMoveFacts`
  returns false for a body that has never travelled, and `StepHeight` readers (`StartTask 0x102a1910`)
  ask before any travel.
- The floor probe reaches about `MaxStepHeight` + 2.4 cm below the capsule; retail's trace goes 4.0
  units. The kernel checks `FloorDistanceCm` against the 4.0-unit reach itself.
- The per-species `NavIgnoreCollision` checks (`0x10379490`, `0x10380f90`, `0x103bfa00`) decide
  contact by contact inside the probe; this seam is a list, so wave 3 registers those entities ahead
  of time. A callback into the kernel would be a backchannel the contract forbids.
- The ignore is one-way. Whether retail's filter checks both sides is unrecovered (the corpus MCP
  server was down).
- No `docs/vtmb` note written (not in the ownership list) — the orchestrator folds it into the landing.
