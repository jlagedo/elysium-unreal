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
 *  `m_flDistTooFar` (`+0x5de4`, `FElysiumNpc::DistTooFar`). Name **unrecovered**; it is the
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

/** `CAI_Navigator#17` `0x102eee40` — the move-info block built from the current path: the path
 *  point, the per-axis delta (2-D at nav type 0, 3-D otherwise), its length, the motor's base speed
 *  and the goal tolerance (the motor's `+0x30` scaled by it, floored at the length), the navigator
 *  radius, bit 0 for a straight-line path and bit 2 for a waypoint whose kind differs from its
 *  owner's. SOURCE units. */
struct FNavMoveInfo
{
	FVector TargetUnits = FVector::ZeroVector;   // out[0..2]  the path point
	FVector DeltaUnits = FVector::ZeroVector;    // out[3..5]  target - my origin
	FVector DirUnits = FVector::ZeroVector;      // out[6..8]  the same delta, copied
	float BaseSpeedUnits = 0.f;                  // out[9]
	float DistanceUnits = 0.f;                   // out[10]
	float GoalToleranceUnits = 0.f;              // out[11]
	int32 NavType = 0;                           // out[12]
	float Radius = 0.f;                          // out[13]
	uint32 Flags = 0;                            // out[14] bit0 straight line, bit2 kind change
	bool bHasPath = false;                       // out[15] — the path pointer retail stores
};

/** SEAM for the `CAI_Path` reads `0x102eee40` makes: the current path point (`0x10012805`), the
 *  straight-line test (`0x1030bd50`), the navigator radius (`0x102ecc40`) and the next waypoint's
 *  kind against its owner's (`path+0x24`, `+0x30`, `+0x2c`). There is no path object here; the
 *  whole block answers false and the move info comes back with `bHasPath` clear. */
struct FNavPathSample
{
	bool bValid = false;
	FVector PointUnits = FVector::ZeroVector;
	bool bStraightLine = false;
	bool bNextWaypointKindDiffers = false;
	float RadiusUnits = 0.f;
};
