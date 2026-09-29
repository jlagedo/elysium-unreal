#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumWorldServices.h"

// What the port's navigator last concluded about its route: the port's own word, not a retail one.
// Retail raises its outcomes as calls (`OnNavComplete 0x102eea90`, `OnNavFailed 0x102eeae0`) and
// keeps only the `+0x1c` latch; the port records WHICH outcome so the kernel's writers and the
// tests can read it back. `Door` is `OnNavFailed(0x0e)` from the simplify pass's door probe
// (`0x102f06e0` -> slot 531, `0x102f08e7`); `NpcBlocked` is a blocked step whose obstruction is an
// NPC (the `-3` arm of `Move 0x102eff40`, `0x102f0169`).
enum class EElysiumNpcNavOutcomeKind : uint8
{
	None,
	Arrived,
	Failed,
	Door,
	NpcBlocked,
};

struct FElysiumNpcNavOutcome
{
	EElysiumNpcNavOutcomeKind Kind = EElysiumNpcNavOutcomeKind::None;
	// The `TaskFail` code the outcome raised: `0x0c` FAIL_NO_ROUTE, `0x0d` no goal type (`Move`
	// entry, `0x102f0081`), `0x0e` the door. 0 for `None` / `Arrived`.
	int32 FailCode = 0;
	// The obstruction's entity for `NpcBlocked` (`trace+0x1c -> +0x94`, `0x102ef3e0`); unset else.
	FElysiumEntityHandle Blocker;
};

// `CAI_Navigator` (`npc+0x5d34`, block `operator_new(0x68)` / `0x6c` humanoid) and the `CAI_Path` it
// owns at `nav+0x30`, as far as the port's kernel reads them. Navigator words are `nav+N`, path words
// `path+N`. Grown into the navigator object for 0018 story 5; the struct started as family Motor's
// port-side record (moved verbatim out of `ElysiumNpcBaseMotor.inl`, commit `eadead74`).
//
// **The route itself is not here.** The waypoint list (`path+0x24` and its `+0x30` chain) is the
// Unreal body's path follower; this object keeps the words the kernel reads and writes, and the
// movement facts come from `IElysiumNpcMotor::SampleMoveFacts`. Defaults are the path constructor's
// (`0x1030bec0`) and the reset's (`0x1030bb30`) values unless a comment says otherwise.
struct FElysiumNpcNavigator
{
	// --- Navigator words -------------------------------------------------------------------------

	// `nav+0x14` -- the squared distance, SOURCE units², from the point the route was searched from
	// to the installed path's endpoint (`0x102ed430`'s tail over `0x1000f89e(path)`). Written by
	// `InstallPathNoGoal`; nothing in this substrate reads it yet.
	float EndpointDistanceSqrUnits = 0.0f;

	// `nav+0x18` -- `m_navType`, the native navigation type. `FUN_1027d990` reads it (29 direct
	// callers) and `FUN_1027d9b0` writes it through `0x102eeba0`. Vocabulary: 0 ground, 1 jump, 2
	// fly, 3 climb (`EElysiumNpcNavType`); constructor default 0. The write is also pushed on to
	// `IElysiumNpcMotor::SetNavigationType`.
	int32 NavType = 0;

	// `nav+0x1c` -- set to 1 by `OnNavFailed` (`0x102eeae0`, `CAI_Navigator#10`) and by
	// `OnNavComplete` (`0x102eea90`, `CAI_Navigator#8`): the "this route has ended" latch; `Move`
	// zeroes it at its loop head (`0x102f00e9`) and exits the loop on it.
	bool bNavFailed = false;

	// `nav+0x40` -- `m_timePathRebuildMax`, the route search time. One non-zero writer:
	// `SetRouteSearchTime 0x102886f0` (`TASK_SET_ROUTE_SEARCH_TIME 0x50`). Read by the route build
	// `0x102f1dc0`: 0 fails a missing route at once (`0x102f1f00` `OnNavFailed(0xc, 1)`), anything else
	// defers it and sets `m_afMemory` bit `0x20`. Zeroed by `0x102f28a0`; the constructor
	// `0x102eca50` does not touch `+0x40..+0x4c`. Seconds.
	float RouteSearchTime = 0.f;

	// `nav+0x44` -- `m_timePathRebuildDelay`, the retry interval `0x102f1dc0` adds to `curtime` for
	// `+0x4c`. NEVER written non-zero by any code (only `0x102f28a0`'s zero), so retail retries every
	// think until `+0x48` (R1 §1). Seconds; 0 is retail's.
	float RouteRetryInterval = 0.f;

	// `nav+0x48` -- the deferred route's give-up time (`curtime + +0x40`, `0x102f1f36`); past it the
	// next build fails with `OnNavFailed(0xc, 1)` (`0x102f1f5a`).
	double RouteGiveUpTime = 0.0;

	// `nav+0x4c` -- the deferred route's next retry time (`curtime + +0x44`, `0x102f1f27`); the
	// compare is strict (`+0x4c < curtime`, `0x102f1f73`).
	double RouteRetryTime = 0.0;

	// `nav+0x50` (`m_fRememberStaleNodes`, the stale mark's gate) has no port word: its writer is
	// unrecovered (R3) and the port keeps no link table for `0x102f1fa0` to mark (`NavMarkStaleLink`).

	// The NPC-blocker hold (`0x102ef3e0`, R3 "The -3 arm as settled"). `nav+0x51` the hold byte
	// (cleared by the `MoveNormal` gate each pass); `nav+0x54` the remembered blocker (EHANDLE, -1 at
	// the ctor and by `0x102eeb70`); `nav+0x58` hold-until and `nav+0x60` forget-at (-1.0 at the ctor
	// and by `0x102eeb70`); `nav+0x5c` the hold (0.25 s) and `nav+0x64` the window (3.0 s), both
	// ctor constants (`0x102eca50`). Read by `NavBlockerHold` inside `NavigatorMoveStep`.
	bool bBlockerHold = false;
	FElysiumEntityHandle BlockerEntity;
	double BlockerHoldUntil = -1.0;
	float BlockerHoldSeconds = 0.25f;
	double BlockerForgetAt = -1.0;
	float BlockerWindowSeconds = 3.0f;
	// Port-only, the hold's probe words (`NavMoveNormalPass`, the NAMED MODERNIZATION there). Retail
	// re-probes the step every think, so each `0x102ef3e0` call is a fresh contact at `curtime`; the
	// port's probe is the body's leg, whose one `Blocked` end is re-read by every think until a leg is
	// issued again. `bBlockerHoldStanding`: a hold was armed (or held) on the body's current ended
	// request -- re-reading that same end is no new contact, only "has the hold run?".
	// `bBlockerHoldReissued`: the hold has run and re-issued the head leg, retail's post-hold probe;
	// `BlockerProbeAt` is that probe's `curtime` and `BlockerProbeRemainingUnits` the body's distance
	// left when it was sent. All four are cleared by `0x102eeb70` and by a new leg (`NavIssueLeg`).
	bool bBlockerHoldStanding = false;
	bool bBlockerHoldReissued = false;
	double BlockerProbeAt = -1.0;
	float BlockerProbeRemainingUnits = 0.f;

	// --- Path words (`CAI_Path` at `nav+0x30`) ----------------------------------------------------

	// `path+0x1` -- the pedestrian byte: set when the path's route is built for a type-8 goal
	// (interesting place, pedestrian), cleared by the path reset. Read by the local route attempt
	// (`0x102f2060`: only when `path+1 == 0`) and the door probe's trace mask (`0x102f06e0`: `0x2600b`
	// instead of `0x2400b`). In the port it selects the pedestrian query filter on the request.
	bool bPedestrian = false;

	// `path+0x10` -- `m_bPaused`, read by `0x102ee2e0` (`Move`'s first gate, `0x102effab`: paused ->
	// return, no fail), set by `0x102ee2a0` (`0x1030be80`), cleared by `0x102ee2c0` (`0x1030bea0`).
	bool bPaused = false;

	// `path+0x24 != 0` -- a current (head) waypoint exists; what `IsGoalActive` `0x102ee6a0` reads.
	// The waypoint list is the body's; this is the substrate's record that the route stands.
	bool bHasHeadWaypoint = false;
	// The head waypoint's goal bit, `(wp+0x28 >> 3) & 1` (`0x1030bd50`, read through `0x102ee660`
	// `CurWaypointIsGoal`): the waypoint being walked to is the route's last. 0 with no head.
	bool bHeadIsGoal = false;

	// `path+0x28` -- `m_goalTolerance`, which `SetGoal 0x102ecd20` resolves from goal word `[8]` and
	// writes (`0x102ecec7`), and whose -1.0 "keep" arm reads back. Also written by `0x102ee1c0` (the
	// tolerance tails of `0x102a1910`); zeroed by the path reset `0x1030bb30`. Read through
	// `0x102ee1a0`. NOT `m_flGoalTolerance` (`npc+0x6320`). Centimetres.
	float GoalToleranceCm = 0.f;

	// `path+0x2c` -- `m_movementActivity`, written by `0x102ee250` (`SetGoal`'s word `[5]`), read by
	// `0x102ee3f0` / `0x102ee510`. The path constructor (`0x1030bec0`) and the reset (`0x1030bb30`)
	// both store 1 (ACT_IDLE), so this is 1 from construction and after every reset, never
	// "unwritten".
	int32 MovementActivity = 1;

	// `path+0x30` -- `m_target`, the goal's target entity handle (`SetGoal` `[10]`, cleared by
	// `SetGoal` flag 2 through `0x100a0ae0(.., NULL)` and by the path reset, `1030bb8c` := -1), read
	// through `0x102ee160`.
	FElysiumEntityHandle TargetEntity;

	// `path+0x34..+0x3c` -- `m_vecTargetOffset` (`SetGoal` flag 2 stores `vec3_origin`); subtracted
	// from the goal position by `ActualGoalPosition` `0x102ee140`. Centimetres.
	FVector TargetOffsetCm = FVector::ZeroVector;

	// `path+0x40` -- `m_waypointTolerance`, `SetGoal` stores the pathing hull * 0.5 (`0x102ececa`).
	// Centimetres.
	float WaypointToleranceCm = 0.f;

	// `path+0x44` -- the last node passed (-1 on every find and at the path ctor; `AdvancePath`
	// stores the popped node waypoint's `+0x10`). Its retail reader, the stale mark's gate
	// (`0x102f1fa0`: `path+0x44 != -1`), is a seam with no link table, so nothing reads it yet.
	int32 LastNodePassed = INDEX_NONE;

	// `path+0x4c..+0x54` -- `m_goalPos`, the raw goal position (`0x1030ba30`). Centimetres.
	FVector GoalPosCm = FVector::ZeroVector;

	// `path+0x5c` -- `m_goalType` (`GoalType_t`, guard byte `path+0x58`): 0 none, 1 target entity, 2
	// enemy, 3 path corner, 4 location, 5 (no issuer), 6 cover, 7 best-unknown, 8 interesting place
	// (pedestrian), 9 interesting place (animal). Written by `SetGoal` through `0x1030ba50` and by
	// `0x102ed430`'s goal-less install (4); read through `0x102ee620` / `0x100113d8`.
	int32 GoalType = 0;

	// `path+0x60` -- the goal flags `SetGoal` copies from word `[9]` (its only writer; the path
	// reset `0x1030bb30` zeroes it): 1 face the path, 2 node route (one issuer, Troika task `0xc7`),
	// 4 re-path on target move (no issuer), 8 (no issuer, inert). Read through `0x102ee640`.
	int32 GoalFlags = 0;

	// `SetGoal`'s arrival words `[6]` / `[7]` (`0x1030b550` / `0x1030b5b0`) and destination node
	// `[4]` (`0x102ee9c0`). Their path offsets are not in the story-5 reads (unrecovered).
	int32 ArrivalActivity = INDEX_NONE;
	int32 ArrivalSequence = INDEX_NONE;
	int32 GoalNode = INDEX_NONE;

	// --- The port's own words ---------------------------------------------------------------------

	FElysiumNpcNavOutcome LastOutcome;

	// The travel request the head waypoint's leg was issued with (`FElysiumNpcBase::NavIssueLeg`):
	// destination, arrival radius, gait and speed, and the pedestrian multiplier as it was DRAWN --
	// retail draws `RandomInt(5, 10)` once per route build (`0x102fe9f0`), so a re-issue of the same
	// leg must not draw again. Read by the NPC-blocker hold's re-issue and the move step's log. The
	// path reset empties it with the waypoint list.
	FElysiumNpcMoveRequest HeadLegRequest;
	bool bHeadLegRequestSet = false;

	// `CAI_Navigator::vfunc3` (`0x102ecb50`) copies three of the owner NPC's own pointers —
	// `m_pMotor` (+0x5d44), `m_pMoveProbe` (+0x5d40), `m_pLocalNavigator` (+0x5d38) — into
	// navigator+0x20/+0x24/+0x28 and stores its argument at +0x2c. The three pointers do not exist
	// here, so what survives the port is the FACT that the snapshot was taken and the argument it
	// was taken with. Read by the test and by nothing else.
	bool bSnapshotTaken = false;
	int32 SnapshotArgument = 0;

	// Port-only: how many goal-less installs this navigator took (`InstallPathNoGoal`). The tests'
	// witness that the wander pick installed a PATH and never went through `SetGoal`.
	int32 PathNoGoalInstalls = 0;

	// --- Getters (SDK names where R1 matched one), each answering retail's no-goal value ----------

	// `0x102ee680` -- `GetGoalType() != 0`. No goal: false.
	bool IsGoalSet() const;
	// `0x102ee6a0` -- `nav+0x30 != 0 && path+0x24 != 0`. No goal: false.
	bool IsGoalActive() const;
	// `0x102ee620` (`0x100113d8`) -- `path+0x5c`. No goal: 0.
	int32 GetGoalType() const;
	// `0x102ee140` (`0x1000f89e`) -- `ActualGoalPosition`: `path+0x4c` minus `path+0x34`, never
	// "none"; `(0,0,0)` after a reset. Centimetres.
	FVector GetGoalPos() const;
	// `0x1030bb30` -- the path reset both reset arms end on (`0x102f28a0`: `SetGoal` flag bit 1 and
	// `ClearGoal 0x102ee270`): goal type 0, goal position and target offset to the origin, the goal
	// flags (`path+0x60`) 0, movement activity 1, goal tolerance 0, the target handle (`path+0x30`)
	// -1, the paused byte (`path+0x10`) 0; the pedestrian byte, the head waypoint and its goal bit
	// cleared (the waypoint list is emptied, with the port's recorded head-leg request). The reset
	// also zeroes `path+0x20` (`1030bb82`), which the port keeps on the NPC as `NavPathScalar20`: the
	// caller (`NavClearRoute`) owns that word.
	void ResetPath();
	// `0x102ee3f0` -- `path+0x2c`. No goal: 1 (ACT_IDLE).
	int32 GetMovementActivity() const;
	// `0x1027d990` -- `nav+0x18`. Default 0 (ground).
	int32 GetNavType() const;
	// `0x102ee1a0` -- `path+0x28`. No goal: 0.0.
	float GetGoalTolerance() const;
	// `0x102ee640` -- `path+0x60`. No goal: 0.
	int32 GetGoalFlags() const;
	// `0x102ee2e0` -- `path+0x10` `m_bPaused`. Default false.
	bool IsPaused() const;
	// `0x102ee160` -- `path+0x30` (the caller resolves it; unset = retail's NULL).
	FElysiumEntityHandle GetTarget() const;
	// `0x102ee660` -- the head waypoint's goal bit. No head: false.
	bool CurWaypointIsGoal() const;
};
