# Lane S · the seam surface (wave 2, alone)

Read `README.md` in this directory first. You own, and only you may edit:

- `Source/ElysiumUE/Public/ElysiumWorldServices.h`
- `Source/ElysiumUE/Private/Visual/ElysiumNpcBody.h`, `ElysiumNpcBody.cpp`, `ElysiumNpcBodyGeometry.cpp`,
  `ElysiumNpcBodyNavigation.cpp` (and any other `Visual/ElysiumNpcBody*.cpp`)
- `Source/ElysiumUE/Private/Tests/ElysiumTestServices.h` (the recording motor / services)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcNavigator.h` only if a fact word must live there
- One new test file `Source/ElysiumUE/Private/Tests/ElysiumNpcMotorSeamTests.cpp`

No consumer moves in this wave: you add the seams and leave every existing caller alone. After
this wave the header is frozen for wave 3's four lanes, so the surface must be complete.

## Why

Wave 3 replaces these hand-ported mechanism bodies with seam calls and needs two facts the
movement seam (`IElysiumNpcMotor`, `ElysiumWorldServices.h:310`; `FElysiumNpcMoveFacts` `:229`)
does not carry yet:

1. **Floor facts** for `CheckOnGround` (`ElysiumNpcBaseMotor.cpp:614`, ~75 lines of own trace
   math, called by `ElysiumNpcConditions2.cpp:203`) and the four `CanStandOn` /
   `GetGroundVelocityToApply` rows (`ElysiumEntitySlotBodies.cpp:496,527,772,785`, whose
   "Unreal service" is "UCharacterMovementComponent floor test via the 0018/5 movement seam").
   Retail's ground question is `CAI_BaseNPC::CheckOnGround` (`0x1027b8a0`-area; read
   `docs/vtmb/npc-kernel/functions.md` for the exact address and `docs/vtmb/npc-ai/index.md`
   for its section): it asks "is there ground under the hull within the step height, and what
   entity is it". The seam answers: `bOnGround`, `GroundEntityHandle` (an `FElysiumEntityHandle`
   or the id type the motor already uses for the enemy/target), `FloorNormal`, `FloorDistance`,
   `bWalkable`. Source: `UCharacterMovementComponent::CurrentFloor` (`FFindFloorResult`) on the
   live body (`ElysiumNpcBody.cpp:1176` already reads it), the recording motor's lambda in tests.
2. **Collision-ignore** for the rows whose service is "UPrimitiveComponent IgnoreActorWhenMoving
   (collision filter)" / "movement collision-ignore filter via the 0018/5 movement seam"
   (`rg -n "IgnoreActorWhenMoving|collision-ignore" docs/vtmb/npc-kernel/seam-list.md` lists
   them with their retail functions). Retail's is the `m_hIgnoreEntity`-style word
   (`CAI_BaseNPC` `+0x…`, see `fields.md`; the port body names it). The seam:
   `SetMoveIgnore(handle, bool)` on the motor; the live body calls
   `UPrimitiveComponent::IgnoreActorWhenMoving` on the capsule; the recording motor records it.

Also carry, because wave 3 will need them and no spec owns them:

3. `HasPath()` on the motor (rows whose service is "UPathFollowingComponent moving-goal
   tracking"): true when the path-following component has an active request. The live body
   already knows (`MoveTo`, `ElysiumNpcBody.cpp:615`).
4. The step height and max slope as facts on `FElysiumNpcMoveFacts` if they are not there
   (rows "UCharacterMovementComponent MaxStepHeight via the 0018/5 movement seam"): read
   `MaxStepHeight` / `GetWalkableFloorAngle` from the component; the recording motor answers
   the retail defaults from the tunables table (`ElysiumNpcKernelTunables.h`, step 18).

## Rules

- Facts, never verdicts: the seam returns what Unreal measured; the rule that reads it stays in
  the kernel (0018/5's contract, `docs/contracts/` — read the movement seam contract if one is
  there; `rg -l "FElysiumNpcMoveFacts" docs/`).
- Retail names in comments: every new field names the retail word or function it stands for.
- Every new seam has a recording-motor implementation so wave 3's tests can assert it, and one
  test in the new test file that drives the live body on the test map fixture the existing motor
  tests use (`rg -n "AElysiumNpcBody" Source/ElysiumUE/Private/Tests/*.cpp | head`) — floor facts
  on the floor and 3 m up; ignore set and cleared.
- Do not change any existing signature. Add only.

## Deliver

`report-S.md` (≤300 words): the new surface as a signature list (one line each, with the
retail word it stands for), what the live body reads for each, the test names. No tsv.
