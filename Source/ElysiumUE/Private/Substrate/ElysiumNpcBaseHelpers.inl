// `CAI_BaseNPC`'s helper declarations (story 5 step 5), moved from `ElysiumNpcKernelBaseHelpers.inl`
// and `ElysiumNpcTroikaHelpers.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseHelpers.cpp`.

/** SEAM for the four `CAI_Motor` calls the seven bodies above make that have no mover here:
 *  `thunk_FUN_102e2840` (the state reset), `thunk_FUN_102e2690` (`SetAbsVelocity`),
 *  `thunk_FUN_102e1c10` (reissue the move at a yaw and a speed) and `thunk_FUN_102e12c0` (the base
 *  speed). The first two are counted; the reissue records its arguments; the base speed answers
 *  `0.0`. */
struct FTroikaMotorSeams
{
	int32 StateResets = 0;
	int32 VelocitySets = 0;
	FVector LastVelocityUnits = FVector::ZeroVector;
	int32 MoveReissues = 0;
	float LastReissueYaw = 0.f;
	float LastReissueSpeed = 0.f;
	int32 ForcedActivities = 0;
	int32 LastForcedActivity = INDEX_NONE;
	/** The owner's vtable `+0xf8` (slot 62) move dispatch `FUN_102e0f90` ends its near arm on, and
	 *  the `+0x340` (slot 208) and `SetSolid` calls `FUN_102e0ea0` makes. Unidentified in the
	 *  census; counted. */
	int32 OwnerMoveDispatches = 0;
	int32 OwnerSlot208Dispatches = 0;
	int32 SolidSets = 0;
	/** `m_flMoveInterval`, `CAI_Motor+0x30` — the one motor word these bodies read AND write. */
	float MoveInterval = 0.f;
	/** `CAI_Motor+0x54 m_facingQueue`'s live count, `CAI_Motor+0x3c`. */
	int32 FacingQueueCount = 0;
	/** The owner's clip test, vtable `+0x838`. True REFUSES the steer, which is retail's guard. */
	bool bSteerClipped = false;
	/** `thunk_FUN_102e2820(sequence, "move_yaw")` — does the playing sequence carry the pose
	 *  parameter? False takes the pseudo-yaw arm. */
	bool bHasMoveYawPoseParam = false;
	/** `thunk_FUN_102e27d0(this, "move_yaw", yaw)` — the pose-parameter write, recorded. */
	int32 PoseParamWrites = 0;
	float LastPoseParamYaw = 0.f;
	/** `thunk_FUN_102d61b0(hull)` — the hull table's speed floor. Unrecovered; answers `0.0`. */
	float HullSpeedFloorUnits = 0.f;
	/** The motor's own speed ceiling, vtable `+0x40`. Unrecovered; answers `0.0`. */
	float SpeedCeilingUnits = 0.f;
};


/** Family Hints owns `FHintWords`, the typed view of one `CAI_Hint`'s datamap. Forward-declared
 *  here because this family's `.inl` is included ahead of that one and the pure rules below take it
 *  by reference; reusing it is the point — a second copy of a hint's words is exactly what the two
 *  families must not stand. */
struct FHintWords;

// From `ElysiumNpcTroikaHelpers.inl`: the navigator and hint helpers base bodies use.

/** `CAI_Navigator+0x54`, `+0x58` and `+0x60` — the three words `0x102eeb70` resets, written as
 *  `-1`, `-1.0` and `-1.0`. Their retail names are **unrecovered**: the corpus holds the reset and
 *  no reader that pins any of the three. Declared by offset so `FUN_102eea70` / `FUN_102eeac0`
 *  write the words retail writes rather than recording a bare "reset happened".
 *
 *  `CAI_Navigator+0x18` (`NavType`) and `+0x1c` (`bNavFailed`) belong to family **Motor**'s
 *  `FNavigator Navigator` and are read and written through it rather than duplicated here. */
int32 NavigatorWord0x54 = -1;

float NavigatorWord0x58 = -1.f;

float NavigatorWord0x60 = -1.f;

/** The `thunk_FUN_102ddc40(nav+0x28)` route clear at the head of the same reset. **SEAM**: family
 *  Motor's standing fact is that this substrate keeps no route, so the clear is counted and clears
 *  nothing. */
int32 NavigatorRouteClears = 0;

/** `CAI_StandoffBehavior+0x19` — the byte `vfunc3` (`0x102c7410`) reads first and CLEARS on its
 *  two refusal arms. Carried on the leaf because this runtime stands no behaviour object; family
 *  **Lifecycle** made the same call for `FStandoffWords` and this is the one word that view does
 *  not carry. Its retail name is **unrecovered**; 29c read it as "the owner's weapon-drawn cache". */
bool bStandoffRangedCache = false;

/** `CAI_StandoffBehavior+0x50` — the float `vfunc5` (`0x102c7530`) copies into the owner's
 *  `m_flDistTooFar` (`+0x5de4`, `FElysiumNpcBase::DistTooFar`). Name **unrecovered**; it is the
 *  behaviour's own authored stand-off distance. */
float StandoffDistTooFar = 0.f;

/** `CBaseEntity+0x1fc`, which `vfunc5` writes `2` into. Below the shape map's band and
 *  **unrecovered** — no corpus body in layers 0–9 reads it. Declared by offset so the write lands
 *  somewhere rather than being dropped. */
int32 Field_0x01fc = 0;

/** One live `CAI_Motor` facing-queue entry as `FUN_102e2180` reads it: the target position
 *  (`thunk_FUN_102d8a90`) and the blend weight (`thunk_FUN_102d8bc0`). SOURCE units. */
struct FFacingQueueEntry
{
	FVector TargetUnits = FVector::ZeroVector;
	float Weight = 0.f;
};

mutable FTroikaMotorSeams TroikaMotor;

/** `CAI_Navigator#17` `0x102eee40` (`MoveCalcBase`) — the move-info block built from the current path:
 *  the path point, the per-axis delta (nav type 0: z dropped and a 2-D normalise; otherwise a 3-D
 *  one), its length, the motor's base speed and the goal tolerance (the motor's `+0x30` scaled by it,
 *  floored at the length), the move target (`0x102ecc40`), bit 0 when the head waypoint is the goal
 *  (`wp+0x28 & 8`), else bit 2 when the next waypoint's move type differs. SOURCE units. */
struct FNavMoveInfo
{
	FVector TargetUnits = FVector::ZeroVector;   // out[0..2]  the path point
	FVector DeltaUnits = FVector::ZeroVector;    // out[3..5]  target - my origin, normalised in place
	FVector DirUnits = FVector::ZeroVector;      // out[6..8]  a copy of the normalised delta
	float BaseSpeedUnits = 0.f;                  // out[9]
	float DistanceUnits = 0.f;                   // out[10] the length the normalise returned
	float GoalToleranceUnits = 0.f;              // out[11]
	int32 NavType = 0;                           // out[12]
	FElysiumEntityHandle MoveTarget;             // out[13] pMoveTarget (`0x102ecc40`), NOT a radius
	uint32 Flags = 0;                            // out[14] bit0 head is the goal, bit2 move type changes
	bool bHasPath = false;                       // out[15] — the path pointer retail stores
};

/** The `CAI_Path` reads `0x102eee40` makes, from `IElysiumNpcMotor::SampleMoveFacts`: the current
 *  point (`0x10012805`, the follower's next corner) and the head-is-goal test (`0x1030bd50`, the
 *  corner being walked to is the path's last). The next waypoint's move type (`path+0x24 -> +0x30 ->
 *  +0x2c`) is not a fact the follower reports, so `bNextWaypointKindDiffers` is always false. No
 *  facts (a motor that reports none, or a follower holding no path): `bValid` false and the move info
 *  comes back with `bHasPath` clear. */
struct FNavPathSample
{
	bool bValid = false;
	FVector PointUnits = FVector::ZeroVector;
	bool bHeadIsGoal = false;
	bool bNextWaypointKindDiffers = false;
};

/** `CAI_Hint::GetPosition` (`0x102d1180(hint, npc, &out)`) -- the ONE port body of the hint's
 *  position, centimetres: an unbound hint (`m_nNodeID +0x5e4 == -1`) answers its own origin; a bound
 *  one answers its network node at this NPC's PATHING hull (`0x102f46d0`, `+0x156c`), or
 *  `vec3_origin` when the id is outside the network. The point `0x102961a0` rays to,
 *  `TASK_GET_PATH_TO_HINTNODE`'s destination, and (through `HintStandPosition`, in units) the
 *  Troika line's cover / cower stand. False only for an index that names no live hint. */
bool HintPositionCm(int32 HintNode, FVector& OutPointCm) const;

/** `+0x156c`, the PATHING hull every `0x102f46d0` / `CAI_Navigator` read hands `GetPosition`:
 *  `FElysiumNpc::PathingHullKind` on the Troika line, the standing `HullKind` on a base-only NPC. */
int32 RetailPathingHull() const;

/** `GetCurTask()->iTask` (`0x1028a150`), the retail task NUMBER of the running program's current
 *  step: `FElysiumScheduleStep::TaskId`, which is retail's global id. False with no running step
 *  (retail's NULL task). */
bool CurrentRetailTaskNumber(int32& OutTaskNumber) const;

/** SEAM for `0x102d1540` — "is this hint free, or already mine?". Retail: the hint's `m_hHintOwner`
 *  (`+0x5e0`) is me, or `curtime >= m_flNextUseTime` (`+0x5ec`) and the owner handle is dead. Family
 *  Hints ports the same three words as `IsHintUnusable` from the other side; this is the OWNER-only
 *  half `IsUnusableNode` (slot 527) negates, and it answers true (free) with no hint store. */
bool IsHintAvailableToMe(int32 HintNode) const;

/** `CAI_BaseNPC::GetNavTargetEntity` (`0x102729d0`). NAMED: the SDK twin's two arms
 *  (`GOALTYPE_ENEMY` -> `GetEnemy()`, `GOALTYPE_TARGETENT` -> `GetTarget()`) are retail's modes 2
 *  and 1 exactly; retail adds a third, mode 7 `GOALTYPE_COVER`, through the navigator. */
FElysiumEntity* GetNavTargetEntity() const;

/** `CAI_BaseNPC::RememberUnreachable` (`0x10274080`). NAMED by the SDK twin, arm for arm: a
 *  BACKWARD scan for an existing record refreshes its expiry, a miss appends one, and the entity's
 *  current position is written either way. The duration is baked (`_DAT_10449258`). */
void RememberUnreachable(FElysiumEntity* Entity);

/** `CAI_BaseNPC::SetDefaultEyeOffset` (`0x10274ca0`). NAMED by the SDK twin and by its own string,
 *  `"WARNING: %s has no eye offset in .qc!"`. */
void SetDefaultEyeOffset();

/** `CAI_BaseNPC::GetScriptCustomMoveActivity` (`0x10289fe0`). NAMED by the SDK twin: the cine's
 *  `m_iszCustomMove` (`+0x5f50` on `m_hCine`) as an activity, else as a sequence, else `ACT_WALK`.
 *  Returns a retail `Activity` number — 9 `ACT_WALK` or 0x18 `ACT_SCRIPT_CUSTOM_MOVE`. */
int32 GetScriptCustomMoveActivity() const;

/** `thunk_FUN_102d12e0(hint)` — the ONE port body of the hint's yaw (RETAIL frame, degrees): a
 *  network-bound hint answers its node's yaw (`0x102f47b0`, 0.0 for an id outside the network), an
 *  unbound one its own `GetAbsAngles().y`. `ApplyHintLeanOffset`, `FindTacticalHintNode` and the
 *  StartTask facing arms (`StartTaskHintYaw`) all read it. False only for an index that names no
 *  live hint. */
bool HintYaw(int32 HintNode, float& OutYaw) const;

/** SEAM for slot 16 (vtable `+0x40`), the attack-extent margin `FindTacticalHintNode` caches into
 *  `m_vecSavedSleepExtents` (`+0x65d0`) before `SetAbsoluteAttackExtents`. Answers the zero vector,
 *  which is the margin `FElysiumEntity::SetAttackExtents` already stands for. SOURCE units. */
FVector HintAttackExtentsUnits() const;

/** SEAM for `thunk_FUN_102ee3f0(m_pNavigator)` — the navigator's current LINK activity
 *  `StopScheduledMove` (`0x102bf770`) compares `m_IdealActivity` (`+0x0ff0`) against. Family
 *  **Motor** records the same absent link object (`NavLinkActivity`); this answers `-1`. */
int32 NavCurrentLinkActivity() const;

/** SEAM for `thunk_FUN_102cc1f0(this, localId)` — `CAI_Behavior::GetSchedule(localId)`, the
 *  behaviour-local schedule id `StandoffVfunc20` / `StandoffVfunc21` compare `m_pSchedule` against.
 *  There is no behaviour-local id space here; answers `None`, so the compare fails and neither body
 *  clears its condition. */
int32 StandoffScheduleForLocalId(int32 LocalId) const;

/** SEAM for slot 513 (vtable `+0x804`), the owner capability word `StandoffVfunc3` tests
 *  `0x8000000` in. Answers `0`, which CLOSES the gate — and closing it is what clears
 *  `bStandoffRangedCache`, so the refusal is still observable. */
uint32 StandoffOwnerCapabilityWord() const;

/** SEAM for `CBaseAnimating::SelectHeaviestSequence(owner, 8, -1)` — the sequence lookup
 *  `StandoffVfunc3` requires to answer a non-negative index. Answers `INDEX_NONE`. */
int32 SelectHeaviestSequence(int32 Activity, int32 CurrentSequence) const;

/** SEAM for the owner's discipline cast counter, which `StandoffVfunc3` requires to be exactly
 *  `2`. No such counter on this leaf; answers `0`. */
int32 DisciplineCastCounter() const;

/** `FUN_102e2180`'s blend, as a pure function of the surviving entries: for each, accumulate
 *  `(target - self) * weight + accumulator * (1 - weight)` and then normalise the accumulator.
 *  Retail normalises the per-entry delta too and then **throws that away**, blending the RAW delta
 *  — the registers it multiplies are loaded before the call. Reproduced verbatim. */
static FVector BlendFacingQueue(TArrayView<const FFacingQueueEntry> Entries,
	const FVector& SelfUnits);

/** `CAI_Motor#3` `0x102e0ea0` — the motor's init: force activity `0x33` through the owner
 *  (vtable `+0x4d8`), reset the motor state, `SetSolid(2)`, zero `m_flGravity` (`+0x3ec`) and
 *  dispatch the owner's `+0x340` with `(0, 5, 0)`. */
void FUN_102e0ea0();

/** `CAI_Motor#4` `0x102e0f90` — ground deceleration toward `GoalUnits`, scaled by `_DAT_10450564`:
 *  under `m_flMoveInterval * scale` it draws the interval down by `distance * _DAT_10450aa4` and
 *  issues the move; at or above it, it zeroes the interval and restarts at speed `-1.0`. Answers
 *  retail's `1` / `0`. */
bool FUN_102e0f90(const FVector& GoalUnits, float Yaw);

/** `CAI_Motor#6` `0x102e1180` — stop and face: `SetAbsVelocity(goal)`, force activity `0x2c`,
 *  then reissue the move at the goal's own yaw and speed `-1.0`. */
void FUN_102e1180(const FVector& GoalUnits);

/** `CAI_Motor#8` `0x102e1270` — the full stop: `SetAbsVelocity(0,0,0)` then force activity `0x30`.
 *  Also fills `CAI_HumanoidMotor#8`. */
void FUN_102e1270();

/** `CAI_Motor#15` `0x102e2180` — the facing-queue average: drop every expired entry (stride
 *  `0x24`), then blend each survivor's target-minus-self delta into a running accumulator by that
 *  entry's own weight against `1.0 - weight`, normalising before and after each blend. Answers the
 *  accumulator in SOURCE units and the number of entries that survived. */
FVector FUN_102e2180(int32& OutSurvivors);

/** `CAI_Motor#17` `0x102e2580` — the move speed: the base speed clamped DOWN to the motor's own
 *  ceiling (vtable `+0x40`) and then UP to the hull table's floor for this hull. SOURCE units. */
float FUN_102e2580() const;

/** `CAI_Motor#18` `0x102e19e0` — the steering write. Guarded by the owner's clip test (vtable
 *  `+0x838`); on a miss it takes the yaw toward the goal, asks whether the playing sequence carries
 *  the named pose parameter `"move_yaw"`, and writes the final yaw either to the owning NPC's
 *  `m_flDesiredMoveYaw` (`+0x63ec`) or through the motor's own pose-parameter setter. */
void FUN_102e19e0(const FVector& GoalUnits);

/** `CAI_Navigator#7` `0x102eea70` and `CAI_Navigator#11` `0x102eeac0` — **byte-identical bodies at
 *  two distinct slots**: the shared `0x102eeb70` reset (`+0x54 := -1`, `+0x58 := -1.0`,
 *  `+0x60 := -1.0`, clear the route at `+0x28`) and then `+0x1c := 1`, family Motor's
 *  `Navigator.bNavFailed`. One method carries both, and the two `FUN_` names forward to it so each
 *  slot still has a body of its own. */
void NavStopAndMarkDirty();

void FUN_102eea70();

void FUN_102eeac0();

FNavMoveInfo FUN_102eee40() const;

FNavPathSample NavPathSample() const;

/** `CAI_StandoffBehavior::vfunc3` `0x102c7410` — may this standoff do its ranged thing? Reads
 *  `+0x19` first (false there answers false outright), then the owner's capability bit `0x8000000`,
 *  then a non-negative `SelectHeaviestSequence(owner, 8, -1)`, then `m_iDisciplineCastCounter == 2`
 *  AND a live active weapon. Both of the two middle refusals CLEAR `+0x19`. */
bool StandoffVfunc3();

/** `CAI_StandoffBehavior::vfunc5` `0x102c7530` — release the owner's claimed hint node
 *  (`+0x5ddc`) when it is live and this behaviour owns it, zero it, copy the behaviour's `+0x50`
 *  into the owner's `m_flDistTooFar` (`+0x5de4`) and force the owner's `+0x1fc` to `2`. */
void StandoffVfunc5();

/** `CAI_StandoffBehavior::vfunc20` `0x102c7960` and `vfunc21` `0x102c79a0` — **byte-identical
 *  bodies at two distinct slots**: when the running program is the behaviour's local schedule
 *  `0x17`, clear `COND_NEW_ENEMY` (`0x54`). One method, two entry points, as the navigator pair
 *  above. */
void StandoffClearNewEnemyOnLocalSchedule();

void StandoffVfunc20();

void StandoffVfunc21();

