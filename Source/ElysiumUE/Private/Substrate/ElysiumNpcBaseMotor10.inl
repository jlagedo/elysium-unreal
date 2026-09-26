// `CAI_BaseNPC`'s declarations of the `Motor10` family (story 5 step 5),
// moved from `ElysiumNpcMotor10*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseMotor10.cpp`.

/** What this family's seams were ASKED, so a test can assert that a body reached its call and that
 *  the refusal was the recovered one. Read by the test suite and by nothing else. Separate from
 *  family Motor's `MotorSeams` so that family's own cases keep their exact tallies. */
struct FMotor10SeamLedger
{
	int32 MoveTraceSweeps = 0;            // `0x102e6d70`
	int32 LastMoveTraceKind = INDEX_NONE;
	int32 LastMoveTraceMask = 0;
	float LastMoveTraceExtent = 0.f;
	int32 StepToPointCalls = 0;           // `0x102e0bd0`
	FVector LastStepEndUnits = FVector::ZeroVector;
	int32 LocalNavigatorAsks = 0;         // `motor->+0x10` slot 6
	int32 MoveExecuteFailures = 0;        // `motor` slot 10
	int32 SetOriginCalls = 0;             // `UTIL_SetOrigin 0x101cf5c0`
	FVector LastSetOriginUnits = FVector::ZeroVector;
	int32 NavGateJumpTeardowns = 0;       // `nav->+0x20` slot 8
	int32 NavGateClimbTeardowns = 0;      // `nav->+0x20` slot 5
	int32 NavGateWrongTypeWarnings = 0;   // the gate's DevMsg
	int32 NavGatePasses = 0;              // `0x102f13d0`
	int32 NavMoveOverrideAsks = 0;        // navigator slot 16
	int32 NavEnactCalls = 0;              // navigator slot 15
	int32 LastNavEnactArgument = 0;
	int32 NavMoveTails = 0;               // navigator slot 6
	int32 HintActivityAsks = 0;           // owner slot 569
	int32 WeaponOwnsAsks = 0;             // Weapon_OwnsThisType
	int32 LowAimWarnings = 0;             // `0x1027e590` "NPC in standoff lacks needed low aim…"
	int32 BuildLocalRouteAsks = 0;        // `0x10304130`
	int32 SplicePathAsks = 0;             // `0x10319f30`
};


//
// Retail's `AILocalMoveGoal_t` as much of it as `CAI_Motor#19` (`0x102e14a0`), its helper
// `0x102e1560` and `CAI_Motor#20` (`0x102e1760`) reach. Four words, all read from the listing:
//
//   `+0x0c..+0x14`  `dir`            the unit direction the step travels
//   `+0x28`         `maxDist`        the distance the goal still has room for
//   `+0x34`         `pMoveTarget`    the entity the goal EXPECTS to be blocked by
//   `+0x38`         `flags`          bit `0x2` is the only one `0x102e1560` tests
//
// SOURCE units, as every retail position word in this port is. Carried as a declared view rather
// than as NPC state because a move goal is the CALLER's block, not the body's: retail builds one on
// the stack per interval and hands it down.
struct FLocalMoveGoal
{
	FVector DirUnits = FVector::ZeroVector;   // +0x0c / +0x10 / +0x14
	float MaxDistanceUnits = 0.f;             // +0x28
	FElysiumEntity* ExpectedBlocker = nullptr;// +0x34
	uint32 Flags = 0;                         // +0x38, bit 0x2
};

/** `AIMoveTrace_t`, the 0x38-byte (14-dword) record `0x102e6d70` zeroes and fills. The layout is
 *  read out of `0x102e6d70`'s own initialiser, which writes `[0]`, `[1..3]`, `[4..6]`, `[7]` and
 *  `[9]` by index before dispatching on the trace kind:
 *
 *    `+0x00` `fStatus`          an `AIMoveResult_t`; the trace's own answer is `fStatus >= 0`
 *    `+0x04` `vEndPosition`     seeded to the START point
 *    `+0x10` `vHitNormal`       seeded to `vec3_origin` (`DAT_1070d1b0`)
 *    `+0x1c` `pObstruction`     seeded null
 *    `+0x20` `flDistObstructed`
 *    `+0x24` `flTotalDist`      seeded 0
 *    `+0x28` an `EHANDLE`, `+0x2c`, `+0x30`, `+0x34` — VtMB's four extra words
 *
 *  **`+0x2c` and `+0x30` have no recovered meaning and `CAI_Motor#20` does not copy them out.**
 *  That is not an oversight in the port: the listing's copy block at `102e189c`–`102e18ee` writes
 *  `+0x00`, `+0x04`, `+0x08`, `+0x0c`, `+0x10`, `+0x14`, `+0x18`, `+0x1c`, `+0x20`, `+0x24`, the
 *  `EHANDLE` at `+0x28` through its assignment operator, and `+0x34` — and nothing else. */
struct FMotorMoveTrace
{
	int32 Status = 0;                                  // +0x00 fStatus
	FVector EndPositionUnits = FVector::ZeroVector;    // +0x04
	FVector HitNormal = FVector::ZeroVector;           // +0x10
	FElysiumEntity* Obstruction = nullptr;             // +0x1c
	float DistObstructedUnits = 0.f;                   // +0x20
	float TotalDistUnits = 0.f;                        // +0x24
	FElysiumEntityHandle ObstructionHandle;            // +0x28
	int32 Word2c = 0;                                  // +0x2c  NOT copied out by slot 20
	int32 Word30 = 0;                                  // +0x30  NOT copied out by slot 20
	int32 Word34 = 0;                                  // +0x34
};

/** `CAI_Motor::m_vecVelocity`, `CAI_Motor+0x3c` — the datamap names it (`vtmb_fields CAI_Motor`:
 *  `+0x3c m_vecVelocity undefined4[3]`, `+0x54 m_facingQueue`), and both step bodies write all
 *  three components of it from the goal direction scaled by the current speed. SOURCE units.
 *
 *  **A CORRECTION TO A NEIGHBOUR'S NOTE, recorded and not edited into its file.** Family
 *  TroikaHelpers' `FTroikaMotorSeams::FacingQueueCount` is commented "`CAI_Motor+0x54
 *  m_facingQueue`'s live count, `CAI_Motor+0x3c`". The queue is at `+0x54`; `+0x3c` is
 *  `m_vecVelocity` and is a `Vector`, not a count. The two members therefore stand for two
 *  different words and the offset in that comment is the one that is wrong. */
FVector MotorVelocityUnits = FVector::ZeroVector;

/** `navigator+0x51` — the byte the gate clears and `MoveNormal`'s tail tests before it dispatches
 *  navigator slot 6. Retail name **unrecovered**; it is the navigator's own word and lives here
 *  beside family Motor's `FNavigator` rather than inside it, because that view is 29c-1's and this
 *  is a 29d word. */
bool bNavigatorByte51 = false;

// Family TroikaHelpers' `FNavMoveInfo` (`CAI_Navigator#17`, `0x102eee40`) is what the enact takes,
// and `ElysiumNpcTroikaHelpers.inl` is included AFTER this file in `ElysiumNpc.h`'s
// alphabetical block. A nested class may be declared here and defined later in the same class body,
// which is what this line does; it is not a second type.
struct FNavMoveInfo;

/** SEAM for `thunk_FUN_101cf390(this, mins, maxs)` = `UTIL_SetSize` — the bounds write both hull
 *  bodies make. `FElysiumEntity` carries no collision box (family Motor's `RetailCollisionExtents`
 *  records the same gap), and the mins/maxs come off family Motor's `RetailHullExtents`, which
 *  answers the ZERO box, so what is recovered is that the write happened and with what. SOURCE
 *  units. */
FVector LastSetSizeMinsUnits = FVector::ZeroVector;

FVector LastSetSizeMaxsUnits = FVector::ZeroVector;

int32 SetSizeCalls = 0;

/** SEAM for `thunk_FUN_10272f40(this)` — `CAI_BaseNPC::SetupVPhysicsHull`, the VPhysics shadow
 *  rebuild both hull bodies run when `m_pPhysicsObject` (`+0x36c`) is live. **SEAM**: there is no
 *  VPhysics shadow here; counted. */
int32 VPhysicsHullRebuilds = 0;

/** `+0x036c`, the physics object pointer both hull bodies gate the rebuild on. **SEAM**: this
 *  substrate stands no physics object on the kernel surface, so it answers false and the rebuild is
 *  skipped — which is retail's own arm for an NPC with no `VPhysicsGetObject()`. Carried as a bool
 *  a case can raise, because the gate is the recovered half. */
bool bHasVPhysicsObject = false;

/** The six-line ERROR block `SetHullSizeNormal` prints when `(NAI_Hull::Bits(m_eHull) & usedBits)
 *  != NAI_Hull::Bits(m_eHull)` — i.e. when this body's hull was never precached. The six literals
 *  are read out of the listing at `102730a2`–`102730e2`. Counted, with the hull it complained
 *  about. */
int32 HullNotPrecachedWarnings = 0;

int32 LastHullNotPrecached = 0;

mutable FMotor10SeamLedger Motor10Seams;
