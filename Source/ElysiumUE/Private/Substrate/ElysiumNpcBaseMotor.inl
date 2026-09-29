// `CAI_BaseNPC`'s declarations of the `Motor` family (story 5 step 5),
// moved from `ElysiumNpcMotor*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseMotor.cpp`.

// `CAI_Navigator`'s port-side record lives in `Substrate/ElysiumNpcNavigator.h`.
using FNavigator = FElysiumNpcNavigator;


// `CBaseAnimating::m_hIgnoreCollisionEntity` (+0x055c) — the single entity
// `CBaseAnimating::IsIgnoreCollisionEntity` (`0x1008be20`) compares against, and the tail every
// `ShouldIgnoreCollision`/`NavIgnoreCollision` arm falls through to. Nothing in this runtime writes
// it yet (the visual layer states the same fact for bodies at `ElysiumNpcBody.cpp:843`), so the
// tail answers "not that entity" for every candidate.
FElysiumEntityHandle IgnoreCollisionEntity;

// +0x1568 `CAI_BaseNPCTroika::m_eHull` — the STANDING hull. It sizes the collision box and every
// trace and line-of-sight helper resolves its extents from it (`CNPC_VWerewolf::GetGroundpoint`
// `0x103d6a40` among them). Below the shape map's band, so 29b did not bind it; the extents
// themselves come from the generated table (`RetailHullExtents` below).
//
// Both words are written by CONSTRUCTORS, as retail's are: this default is `CAI_BaseNPC`'s zero
// (`0x1027c300`, store `0x1027c574`), and each class whose retail constructor stores a hull writes
// it in its own (`FElysiumNpcCamera` ... `FElysiumNpcTzimisceRunner`; the generated
// `ElysiumRetailHulls::ClassHulls` is the table they are tested against). A class with no store
// of its own keeps its nearest ancestor's (`navigation-jump-links.md` § "The two hull words").
int32 HullKind = 0;

FNavigator Navigator;

/** `UTIL_TraceHull 0x1026e940` / `(*DAT_1070b254)->TraceRay` as the kernel reads its `trace_t`: the
 *  engine hull trace `CheckOnGround` (`0x1026e5e0`), `CheckStandPosition` (`0x102e7270`),
 *  `IsValidCover` (`0x1028af20`), `IsAreaClear` (`0x102a0fb0`), `ValidateNavGoal` (`0x10280360`)
 *  and every other probe body run. Filled by `KernelHullTrace` from `IElysiumEmbodiment::
 *  TraceRetail` (0018 story 6). The defaults are retail's CLEAR trace (`fraction == 1.0`, no hit
 *  entity), which is also what a world with no collision answers. */
struct FKernelHullTrace
{
	float Fraction = 1.f;                  // trace_t +0x2c — 1.0 is "nothing in the way"
	FElysiumEntityHandle HitEntity;        // trace_t::m_pEnt
	FVector PlaneNormal = FVector::ZeroVector;  // trace_t +0x18 plane.normal
	bool bAllSolid = false;                // trace_t +0x36 allsolid
	bool bStartSolid = false;              // trace_t +0x37 startsolid
	FVector EndPosUnits = FVector::ZeroVector;  // trace_t +0x0c endpos (the seam's clear answer: the end)
};

/** Which of a hull row's two extent pairs is being asked for.
 *
 *  Retail's table carries both for every hull, read by four accessors: `0x102d6100` / `0x102d6120`
 *  answer the FULL box and `0x102d6140` / `0x102d6160` the SMALL one. They are not a scale of one
 *  another -- TZIMISCE1 and TZIMISCE2's small boxes are WIDER than their full ones -- so the
 *  choice is the caller's and has to be stated rather than inferred from the hull id. */
enum class EElysiumHullExtents : uint8
{
	Full,    // 0x102d6100 / 0x102d6120
	Small,   // 0x102d6140 / 0x102d6160
};

/** `CAI_BaseNPC::FUN_1027dc80` `0x1027dc80` — the BASE branch of slot 531 `OnObstructingDoor`. The
 *  Troika-line body slot 531 carries is `0x102984a0` and belongs to story 29d, so this lands as a
 *  named method rather than as a second definition of the generated virtual. */
enum class EObstructingDoorResult : int32 { Ok = 0, Illegal = -1 };

/** `FUN_1027d990` — `return m_pNavigator->field_0x18;`. The most-called body of this family. */
int32 NavGetType() const;

/** `FUN_1027d9b0` → `0x102eeba0` — `m_pNavigator->field_0x18 = value`, and the one push onto the
 *  port's mover, which IS this runtime's navigator. */
void NavSetType(int32 Type);

/** `CAI_Navigator::vfunc3` `0x102ecb50` — the owner-pointer snapshot above. */
void NavSnapshotOwnerPointers(int32 Argument);

/** `CAI_Navigator#10 OnNavFailed` `0x102eeae0`, and `CAI_Navigator#9` `0x102eeb50`, whose whole body
 *  is a tail-jump to slot 10 with its second argument shifted down one stack word — so slot 9 IS
 *  `OnNavFailed` under another index, which the 29c walk recorded as unrecovered and the listing
 *  settles. Runs the navigator's reset `0x102eeb70` first (the NPC-blocker memory), writes the
 *  file/line marker at owner+0x1b44/+0x1b48 (ABSENT in the shape map), calls `TaskFail` (slot 448)
 *  with the caller's reason, re-plays the resolved link activity through `SetIdealActivity`, and sets
 *  the failed latch. It does NOT clear the path: the head waypoint and the goal type stand. */
void NavOnNavFailed(int32 FailReason);

/** `CAI_Navigator::Move` (`0x102eff40`, navigator slot 5), which `PerformMovement` (`0x1026c120`)
 *  dispatches from `NPCThink` (R3, 0018 story 5 lane I). The entry gates in retail's order (paused,
 *  slot 525 `OverrideMove`, `m_bShouldMove`, no goal type `0x0d`, no head waypoint `0x0c`,
 *  `m_flMoveWaitFinished`), then the pass loop: each pass is `MoveNormal`'s arms decided from the
 *  body's move facts (`IElysiumNpcMotor::SampleMoveFacts`), result `1` re-enters, `0` ends the
 *  think's budget, a negative result reaches the failure tail (`0x102f0169`: the stale mark unless
 *  `-3`, then `OnNavFailed(0x0c)`), and the 17th dispatch fails `0x0c` whatever it answered.
 *  NAMED DIVERGENCE: the body walks the route on the actor tick, so a pass samples what the body did
 *  instead of stepping it. */
void NavigatorMoveStep();

/** `CAI_Navigator::OnNavComplete` `0x102eea90` (navigator slot 8): the reset `0x102eeb70`, the owner's
 *  `TaskMovementComplete` (`0x10273ec0`, through `0x102eccc0`: the goal waypoint's `AdvancePath`,
 *  then `ClearGoal` `0x102ee270`, so the goal type never outlives the arrival), `nav+0x1c = 1`. */
void NavOnNavComplete();

/** `0x102eeb70` -- the navigator's reset on every `OnNavFailed` and `OnNavComplete`: `nav+0x54 = -1`,
 *  `nav+0x58 = nav+0x60 = -1.0f`, then the local navigator's reset `0x1000b550` (a seam). */
void NavResetBlockerMemory();

/** `AIMoveResult_t`, what one pass hands `Move`'s loop (R3 "The motor status table"). */
enum class ENavMoveResult : int32
{
	ChangeType = 1,      // a waypoint advance (or a spliced detour): the loop re-enters
	Ok = 0,              // walked, held, or completed: the think's budget is spent
	BlockedEntity = -1,
	BlockedWorld = -2,
	BlockedNpc = -3,     // the only negative the failure tail does not stale-mark
	Illegal = -4,
};

/** One pass's reading of the body (`FElysiumNpcMoveFacts`, or the legacy `Sample` status from a
 *  motor that reports no facts). Facts, not a verdict: the pass decides. */
struct FNavStepFacts
{
	// `0x102ef510` (navigator slot 16): the head waypoint is reached -- inside the constant 0.0625
	// units (`0x10451f78`; 2-D on ground nav, 3-D otherwise), or the follower's own Success end.
	bool bWaypointReached = false;
	// The body gave the request up short of the waypoint (the follower ended it without Success).
	bool bGaveUp = false;
	// The obstruction the body names (`trace+0x1c`), unset for the world or an unnamed entity.
	FElysiumEntityHandle Blocker;
	// The body's distance left to the leg's destination, units (the `0x102ef510` distance: 2-D on
	// ground nav, 3-D otherwise); 0 when the body reports no facts.
	float RemainingUnits = 0.f;
};

/** Samples the body for one pass: the move facts first (`Sample` consumes a terminal status), then
 *  the entity record (`SampleMotorIntoEntity`, `GetOrigin` slot 220's source). */
FNavStepFacts NavSampleStep();

/** One `MoveNormal` pass (`0x102efaa0`) on the sampled facts: the gate `0x102efd50` (simplify pass,
 *  `nav+0x51 = 0`), the arrival test `0x102ef510` (`OnNavComplete` on the goal waypoint, else
 *  `AdvancePath`), the movement activity, then the blocked arms (motor code 4 on the move target,
 *  the NPC-blocker hold `0x102ef3e0`, the same-direction mover `0x102efde0`, the goal-tolerance
 *  completion `0x102ef760`). */
ENavMoveResult NavMoveNormalPass(const FNavStepFacts& Step);

/** `0x102ef3e0` -- the NPC-blocker hold (R3 "The -3 arm as settled"): arm when the blocker is not the
 *  remembered one or the 3.0 s window has run (`curtime - nav+0x60 > -0.001`), hold while `curtime -
 *  nav+0x58 <= -0.001`; otherwise answer `nav+0x51`. True = hold (no fail this pass). `CurTime` is the
 *  contact's `curtime`: the think's clock, or the post-hold probe's when the verdict is that probe's
 *  (`NavMoveNormalPass`). */
bool NavBlockerHold(const FElysiumEntityHandle& Blocker, double CurTime);

/** The NPC-blocked arm of `NavMoveNormalPass` (S3 `0x102ef350` / S7 on motor code 2 -> `0x102ef3e0`,
 *  then S4 `0x102ef0e0`), with the port's probe words (`FElysiumNpcNavigator::bBlockerHoldStanding`):
 *  true = the pass answers 0 (held, or the head leg re-issued as the post-hold probe); false = the
 *  NPC status stands (`-3`). */
bool NavNpcBlockerStep(const FNavStepFacts& Step);

/** Issues one leg of the navigator's route to the body (`IElysiumNpcMotor::MoveTo`) and records the
 *  request as the head leg's (`Navigator.HeadLegRequest`), so the NPC-blocker hold can re-issue the
 *  same leg without redrawing its pedestrian multiplier. A refused request clears the record. Every
 *  navigator leg issue goes through here. Answers whether the body accepted it. */
bool NavIssueLeg(const FElysiumNpcMoveRequest& Request);

/** The NPC-blocker hold's resume (port-only, the NAMED MODERNIZATION in `NavMoveNormalPass`): the
 *  recorded head-leg request handed to the body again, unchanged. False when no leg is recorded or the
 *  body refuses it. */
bool NavReissueHeadLeg();

/** One `Verbose` line on `LogElysiumNpcEnt` per move-step outcome: the NPC, the outcome (with its
 *  `TaskFail` code when non-zero), the head leg's destination (the goal position when no leg is
 *  recorded) and the 2-D distance left to it. */
void NavLogMoveStep(const TCHAR* Outcome, int32 FailCode = 0) const;

/** `0x102efde0` (sink slot 4, S4): a moving NPC going the same way is followed. SEAM answering no:
 *  its constants and the motor slot 16 gate distance are unrecovered (R3). */
bool NavFollowSameDirectionMover(const FElysiumEntityHandle& Blocker);

/** `0x102ef760` (sink slot 5, `OnMoveBlocked`), past the NPC sink's slot 5 (false): the stopped
 *  activity, unconditionally, then `dist(origin, 0x1030ba30 raw goal) < path+0x28 + 0.1` (2-D when
 *  `nav+0x18 == 0`, 3-D else, strict, units) -> `OnNavComplete`. True = completed. */
bool NavBlockedStepCompletes();

/** `0x102f1fa0(nav, seconds, NULL)` -- the stale mark `Move`'s failure tail writes. SEAM: gated inside
 *  on `nav+0x50 m_fRememberStaleNodes`, a head, `path+0x44 != -1` and the waypoint's node, it marks
 *  the node link (`link+0x64 |= 1`, `link+0x68 = curtime + seconds`); the port has no link table. */
void NavMarkStaleLink(float Seconds);

/** `SimplifyPath 0x102f13d0` -> `0x102f06e0` -> NPC slot 531, the only raiser of `OnNavFailed(0x0e)`.
 *  SEAM answering "no door refused": the door policy is 0018/7's. */
bool NavSimplifyPathDoorRefused();

/** `0x102ecc40` -- the move goal's target (`goal+0x34`): goal type 2 / 1 / 7 -> `GetNavTargetEntity`,
 *  any other type -> the `path+0x30` handle. Unset when it resolves to nothing. */
FElysiumEntityHandle NavMoveTarget() const;

/** `0x102e2d70`'s `+0x94` test: the obstruction is an NPC (the `-3` class). */
bool NavIsNpcBlocker(const FElysiumEntityHandle& Blocker) const;

/** The move step's retail calls the port reaches but cannot perform, and its pass count, so a case
 *  can assert which arm ran. */
struct FNavMoveStepSeams
{
	int32 NoRouteWarnings = 0;    // 0x102f0081 Warning("AIError: Move requested with no route!\n")
	int32 ClimbMotorResets = 0;   // 0x102f0198 climb: motor slot 5 `0x102e1110` + `SetNavType(0)`
	int32 VelocityStops = 0;      // 0x102f0198 motor slot 10 `0x102e1440` (velocity 0), SEAM
	int32 StaleMarkCalls = 0;     // 0x102f1fa0(nav, 4.0, NULL), SEAM
	int32 SimplifyPasses = 0;     // 0x102f13d0(nav, 0) from the MoveNormal gate, SEAM
	int32 LocalNavResets = 0;     // 0x1000b550 inside 0x102eeb70, SEAM
	int32 MoverFollowTests = 0;   // 0x102efde0, SEAM answering no
	int32 BlockerHoldArms = 0;    // 0x102ef49a
	int32 BlockerHoldReissues = 0; // port: the hold's head-leg re-issue (NAMED MODERNIZATION)
	int32 Passes = 0;             // dispatches of the last step's loop
};
FNavMoveStepSeams NavMoveStep;

/** `CAI_Navigator::AdvancePath` `0x102f0400` — a non-goal head waypoint was reached: its arms (flag
 *  `0x02` InPass input and the pass-waypoint re-find, flag `0x10` door transaction, flag `0x04` node
 *  pop into `path+0x44`) and the pop. Answers whether a head waypoint still stands. 0018/5 wave 1
 *  seam, defined in `ElysiumNpcBaseAdvancePath.cpp` (lane E); `NavMoveNormalPass` calls it (lane I). */
bool NavAdvancePath();

/** `IsGoalActive` `0x102ee6a0` -- `nav+0x30 != 0 && path+0x24 != 0`, a head waypoint exists:
 *  `Navigator.IsGoalActive()`. Not `0x102ee680` (`IsGoalSet`, `NavigatorIsGoalSet`). */
bool NavIsGoalActive() const;

/** `thunk_FUN_102ee2c0(m_pNavigator)` -> `0x1030bea0`: the paused byte's CLEAR, `path+0x10 := 0`
 *  (R1 §3). The pair `0x102bf7e0` makes is `if (0x102ee2e0) 0x102ee2c0; m_bShouldMove = 1` -- an
 *  un-pause, never a stop of the body. */
void NavStopMoving();

/** `thunk_FUN_102ee620(m_pNavigator)` — `GetGoalType()`, `path+0x5c` (`Navigator.GetGoalType()`):
 *  0 none, 1 target, 2 enemy, 3 path corner, 4 location, 6 cover, 7 best-unknown, 8 pedestrian
 *  place, 9 animal place. `ValidateNavGoal` requires exactly 6. No goal: 0. */
int32 NavGoalState() const;

/** `thunk_FUN_102ee6a0` (`IsGoalActive`) and `thunk_FUN_102ee510` (the path's movement activity,
 *  `Navigator.GetMovementActivity()`) — the pair `FUN_1027a6c0` reads. False without an active goal. */
bool NavLinkActivity(int32& OutActivity) const;

/** `thunk_FUN_102e0bd0(m_pMotor, …)` — `CAI_Motor::MoveGroundExecute`'s apply of one interval's
 *  root-motion delta, which `AutoMovement` (`0x10280a50`) calls under its gate. **SEAM**: Unreal's
 *  animation instance extracts and applies root motion itself, so this records that the gate was
 *  PASSED and applies nothing; the gate and the order above it are the retail contract this port
 *  keeps (see `docs/vtmb/npc-ai/shape.md` § `0x10280a50`). */
bool MotorApplyIntervalMovement(const FVector& DeltaUnits, float YawDelta);

/** `CBaseAnimating::GetIntervalMovement(flInterval, …)` — the per-frame root-motion delta
 *  `AutoMovement` blends. **SEAM**: the animating tier here publishes no interval movement to the
 *  kernel; answers false with the delta zeroed. */
bool AnimIntervalMovement(float Interval, FVector& OutDeltaUnits, float& OutYawDelta) const;

/** `UTIL_TraceHull 0x1026e940` under the kernel's trace filters (0018 story 6, R1 §3): one
 *  `IElysiumEmbodiment::TraceRetail` from `StartUnits` to `EndUnits` with the box `HullMins` ..
 *  `HullMaxs` (a zero box is a ray, `Start == End` an overlap) under the retail `Mask`, this NPC
 *  ignored, and then the filter's CHARACTER rule applied to the seam's character list
 *  (`KernelTraceKeepsCharacter`); the nearest kept character is folded into `Fraction` /
 *  `HitEntity` / `bStartSolid` when it is nearer than the world hit.
 *
 *  **Frame**: SOURCE units in the PORT's axes -- a position is `Cm / ElysiumMove::U`, Y not negated
 *  (the frame nearly every caller already hands in); the box is retail's own (Source axes), so its Y
 *  pair is mirrored at the seam. `EndPosUnits` comes back in the same frame.
 *
 *  False = no collision world behind this NPC (headless, a double that has not opted in): `OutTrace`
 *  keeps retail's clear defaults with `EndPosUnits = EndUnits`. Counted on `MotorSeams.HullTraces`. */
bool KernelHullTrace(const FVector& StartUnits, const FVector& EndUnits, const FVector& HullMins,
	const FVector& HullMaxs, int32 Mask, FKernelHullTrace& OutTrace) const;

/** The character half of the kernel's trace filters, over one body `TraceRetail` listed (R1 §3,
 *  R2 §1): `CTraceFilterNavGround 0x102e32d0` / `CTraceFilterNav 0x102e30d0` and the
 *  `CTraceFilterSimple::ShouldHitEntity 0x101d31c0` they end on. A character is considered at all
 *  only when the mask carries MONSTER `0x2000000` (`StandardFilterRules 0x101d3080`); then a combat
 *  character with `m_bIsBCCTargetable (+0x1480) == 0` or `m_bScriptHidden (+0xf4)` is skipped
 *  (`101d3284`), and -- unless the mask is `0x46004003` -- one whose own slot 68 ignores this NPC,
 *  or whom this NPC's slot 68 ignores, is skipped. This NPC's slot 68 is where `m_bForceNPCCheck`
 *  (`+0x63da`) acts (`IgnoreCollisionSharedHead`). True = the character blocks. */
bool KernelTraceKeepsCharacter(const FElysiumEntityHandle& Character, int32 Mask) const;

/** The shared hull table's mins and maxs for a hull id, in SOURCE units.
 *
 *  Answers from the replayed `NAI_Hull` table (`Substrate/ElysiumRetailHullTable.h`,
 *  `docs/vtmb/data/hull_table.json`). False, with both left at zero, for a hull id retail's own
 *  table does not carry -- which is what every caller's failure arm was written against. */
bool RetailHullExtents(int32 Hull, EElysiumHullExtents Which, FVector& OutMinsUnits,
	FVector& OutMaxsUnits) const;

/** `m_Collision` (`+0x270`) slots 1 / 2 (`CCollisionProperty +0x4 / +0x10`, `0x100dc810` /
 *  `0x100dc830`) — an entity's OBB mins and maxs, SOURCE units (retail axes), relative to its origin.
 *  For an NPC it is what `UTIL_SetSize` last wrote: the standing hull `m_eHull` (`+0x1568`, the box
 *  word, `HullKind`)'s FULL row (`SetHullSizeNormal 0x10273070`), or its SMALL row while
 *  `m_fIsUsingSmallHull` (`+0x5f2d`) stands (`SetHullSizeSmall 0x10273180`). For the player it is the
 *  `CGameMovement` hull (`0x1011e0d0`): `(-16,-16,0)..(16,16,72)` standing, `..(16,16,36)` ducked.
 *  **SEAM** for every other entity (props, brush entities): their `+0x274` / `+0x280` words have no
 *  source here; answers false with both left at zero. */
static bool RetailCollisionExtents(const FElysiumEntity& Entity, FVector& OutMinsUnits,
	FVector& OutMaxsUnits);

/** `CBaseEntity`'s OWN slot-153 body (`0x10026e70`, spelled `CAISound::FUN_10026e70` because
 *  `CAISound` is the class the corpus attributes it to) — `m_vecVelocity` compared COMPONENT-WISE
 *  for EXACT equality against `DAT_1070d1b0`/`b4`/`b8`, the image's shared zero vector, answering 0
 *  when all three match and 1 otherwise.
 *
 *  Slot 153 has two bodies in this family and this is the other one: every class on the NPC line —
 *  `CAI_BaseNPC` through every `CNPC_V*` leaf — carries `0x10280300`, which is `IsMoving()` above
 *  and forwards to the navigator; the 497 classes that are NOT NPCs carry this, including the five
 *  the kernel's closure walks (`CAISound`, `CAI_Hint`, `CAI_InterestingPlace`,
 *  `CAI_InterestingPlaceConverstation`, `CAI_StandoffGoal`). It is therefore **not** a species
 *  override of the NPC's slot and does not belong in `IsMoving`'s dispatch; it is the base entity's
 *  answer, and it lands here beside its twin under the class it came from, exactly as `CanStandOn`
 *  carries `CAISound::FUN_10026f80`.
 *
 *  No port caller: nothing in this runtime stands a `CAI_Hint` or a `CAISound` as an entity with a
 *  vtable, so this is reached only by the test that pins it — the shape family BaseHelpers already
 *  uses for `FUN_1028ebc0`. Exact equality is retail's and is kept: a velocity of `-0.0` on any axis
 *  compares equal to `0.0` and answers "not moving", which is the shipped answer. */
static bool BaseEntityIsMoving(const FElysiumEntity& Entity);

/** The active weapon's capability word (weapon vtable +0x5a0, slot 360, retail body `0x1014f930`),
 *  which `ShouldMoveAndShoot` requires to carry `0x6000`. **SEAM**: `FElysiumWeapon` stands no such
 *  word; answers 0, so the Troika gate closes and the base rung is never reached. */
uint32 ActiveWeaponCapabilityWord() const;

/** `CAI_Motor`'s deceleration query (`0x102e1300`, `CAI_Motor#16`) lives on `IElysiumNpcMotor`
 *  itself as `MinStoppingDistance()`; this is the NPC-side read, so a body cites the address at the
 *  point of use. Answers the interface's own floor when there is no motor. */
float MotorMinStoppingDistanceUnits() const;

/** `CBaseAnimating::IsIgnoreCollisionEntity(other)` (`0x1008be20`) — the tail of both
 *  collision-ignore chains: `m_hIgnoreCollisionEntity` resolved and compared against the candidate. */
bool IsIgnoreCollisionEntityTail(const FElysiumEntity* Other) const;

/** `CAI_BaseNPC::AutoMovement` `0x10280a50` — slot 250 first, then the interval movement, applied
 *  ONLY when `GetMoveType() == 4` and `FL_FROZEN 0x400` is clear. The gate is the retail contract a
 *  modernization has to keep; the extraction underneath it is Unreal's. */
bool AutoMovement();

/** `CAI_BaseNPC::PostRun` `0x1026c7c0` — `RunAnimation` (`0x1026c8c4`, its one caller), then own
 *  vtable +0x408 (slot 258 `DispatchAnimEvents`, `0x1026c8d8`) with that interval, then
 *  `CBaseCombatCharacter::Weapon_FrameUpdate` (`0x1026c8e0`) with the same number. Answers the
 *  interval, which the think hands to `PerformMovement`. */
float PostRun();

/** `CAI_BaseNPC::CheckOnGround` `0x1026e5e0` — the gated ground hull-trace and the two writes it
 *  can make (clear the ground entity, or adopt the traced one). */
void CheckOnGround();

bool OnObstructingDoorBase(float& InOutMoveGoalMaxDistance, int32 DoorState, float DistClear,
	EObstructingDoorResult& OutResult) const;

/** `CAI_BaseNPC::MaxYawSpeed` `0x10280bb0` — the base line's single constant. */
static float MaxYawSpeedBase();

/** `CAI_Navigator::OnNavFailed`'s activity resolution, `FUN_1027a6c0` `0x1027a6c0`: the link's
 *  cached activity when the link is valid and not -1, else 1 (`ACT_IDLE`). */
int32 ResolveLinkActivity() const;

/** `thunk_FUN_102ee140(m_pNavigator)` — `ActualGoalPosition`, `path+0x4c` minus `path+0x34`, in
 *  Source units and the port's axes (`Origin / U`, no Y reflection). Retail has no "none" answer:
 *  the reset leaves `(0,0,0)`. Always writes `OutGoalUnits` and answers true. */
bool NavGoalPosition(FVector& OutGoalUnits) const;

/** `thunk_FUN_102ee680(m_pNavigator)` — SDK `IsGoalSet`, `Navigator.IsGoalSet()`: the goal TYPE
 *  (`path+0x5c`) is non-zero. It is what `CAI_BaseNPC::IsMoving` (slot 153, `0x10280300`) forwards to
 *  and the test the `== 0` readers of `0x102ee620` make. NOT `NavIsGoalActive`. */
bool NavigatorIsGoalSet() const;

/** `0x102ee2e0` — `path+0x10` `m_bPaused` (`Navigator.IsPaused()`), which is what the
 *  `TASK_WAIT_FOR_MOVEMENT` arms and `0x102bf7e0` read as their first test. */
bool NavigatorIsPaused() const;

/** `CAI_BaseNPC::IsJumpLegal`'s shared geometry helper `FUN_10280790` `0x10280790`, as a pure
 *  function of the three points and the three thresholds, so both fills of slot 521 are one body. */
static bool IsJumpLegalGeometry(const FVector& StartUnits, const FVector& ApexUnits,
	const FVector& EndUnits, float MaxRise, float MaxDrop, float MaxDistance);

