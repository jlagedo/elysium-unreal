// Story 29c-1, family **Motor** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcKernelMotor.cpp` and the tests in
// `Tests/ElysiumNpcKernelMotorTests.cpp`. One file per family rather than 915 declarations
// appended to an already-oversized header: the family boundary is what this story ports by.
//
// The family is `CAI_Motor` / `CAI_Navigator` and everything the NPC asks of its motor: the step
// and jump tunables, the jump setup/legality chain, the collision-ignore pair, the yaw-speed
// ladder, the ground and stuck probes, and the move-done / nav-failure hooks.
//
// THE STANDING FACT OF THIS FAMILY: **this substrate has no navigator and no node graph.**
// `m_pNavigator` (+0x5d34), `m_pLocalNavigator` (+0x5d38), `m_pPathfinder` (+0x5d3c), `m_pMoveProbe`
// (+0x5d40) and `m_pMotor` (+0x5d44) are all `ELYSIUM_NPC_WORD_CHAIN` rows onto
// `FElysiumScriptedCharacter::Motor`, which is an `IElysiumNpcMotor` — a mover that takes a
// destination, a yaw and a speed and reports arrival. It keeps no route, no goal type, no hull
// probe and no `CAI_Node` array. Every body below is ported verbatim and then asks a seam declared
// here; each seam answers NOTHING and names the retail call it stands for. Nothing here invents a
// route to make a body "work".

// --- Words this family needed that 29b did not declare -------------------------------------------
//
// All of them sit outside the hand-written shape map's band (`ElysiumNpcKernelShapeMap.cpp` binds
// `0x1a40`..`0x665a`, the words `CAI_BaseNPC` itself carries): one is `CBaseAnimating`-tier, one is
// `CAI_BaseNPCTroika`'s hull word below the band, and the rest are species leaves' own. Several of
// them share an offset with another species' word — `+0x66b8` is `CNPC_VChangBros::m_ChangType`
// (Squad's `.inl`), `CNPC_VManBat::m_bHasPlayedFlyBySound` (Sounds') AND
// `CNPC_VAsianVampire::m_vLastJumpPosition[2]`; `+0x66d0` is `CNPC_VChangBros::m_fFacingTime`
// (Facing's) and `CNPC_VAsianVampire::m_iLastJumpPositionIdx`. That is retail's own leaf-local
// reuse of the same bytes, and this runtime carries one leaf, so they are separate members.

// `CBaseAnimating::m_hIgnoreCollisionEntity` (+0x055c) — the single entity
// `CBaseAnimating::IsIgnoreCollisionEntity` (`0x1008be20`) compares against, and the tail every
// `ShouldIgnoreCollision`/`NavIgnoreCollision` arm falls through to. Nothing in this runtime writes
// it yet (the visual layer states the same fact for bodies at `ElysiumNpcBody.cpp:843`), so the
// tail answers "not that entity" for every candidate.
FElysiumEntityHandle IgnoreCollisionEntity;

// +0x6750 `CNPC_VMingXiao::m_bBlockedByFriend` — the one-field state `0x1039aaf0` writes and
// `0x1039ab10` reads. Census name only; no other retail body in layers 0–9 touches it.
bool bBlockedByFriend = false;

// +0x66cc `CNPC_VChangBros::m_fLastJumpTime` (`FIELD_TIME`) — the stamp `CheckForJumpAttack`
// (`0x1036c8d0`) measures both itself and every squad sibling against. An absolute curtime stamp,
// carried as double like every other stamp in this runtime.
double LastJumpTime = 0.0;

// +0x66b8 `CNPC_VAsianVampire::m_vLastJumpPosition[2]` (six floats) and +0x66d0
// `m_iLastJumpPositionIdx` — the two-entry ring `IsPosNearStoredJumpPositions` (`0x103618a0`) walks.
// SOURCE units, as every retail position word here is.
FVector LastJumpPosition[2] = { FVector::ZeroVector, FVector::ZeroVector };
int32 LastJumpPositionIdx = 0;

// +0x66d8 `CNPC_VAsianVampire::m_fMovedTimeStamp` and +0x66dc `m_vMovedPosition` — the stationary
// watchdog `UpdateMovedTimeStamp` (`0x10362540`) stamps and `StationaryForTooLong` (`0x10362670`)
// reads. The stamp is an absolute curtime, the position SOURCE units.
double MovedTimeStamp = 0.0;
FVector MovedPosition = FVector::ZeroVector;

// `CNPC_VTzimisce`'s pickup triple, read by its slot 410 `TranslateNavGoalPosition` (`0x103bf580`):
// +0x6670 `m_hPickupTarget`, +0x6674 `m_vecPickupTargetPos` (SOURCE units) and +0x668c
// `m_ePathMode`. Note the offsets: `m_ePathMode` is the HIGHEST of the three, not the lowest — the
// ledger's one-line walk of that body has the triple in the wrong order.
FElysiumEntityHandle PickupTarget;
FVector PickupTargetPos = FVector::ZeroVector;
int32 PathMode = 0;

// +0x6688 `CNPC_VMingXiaoTentacle::m_bIgnoreCollision` — the tentacle's own gate on slots 68 and 69
// (`0x1039eb50`, `0x1039eb90`).
bool bIgnoreCollisionSpecies = false;

// The CURRENT activity number every fill of slot 516 switches on is +0x0fec `m_Activity`, which the
// **Positions** family declares as `ActivityNumber`; the yaw ladders below read that member rather
// than a second copy of the same word. The Facing family carries the IDEAL one beside it
// (`IdealActivityNumber`, +0x0ff0).

// +0x1568 `CAI_BaseNPCTroika::m_eHull` — the hull id `CNPC_VWerewolf::GetGroundpoint`
// (`0x103d6a40`) resolves its trace extents from. Below the shape map's band, so 29b did not bind
// it; the extents themselves are a seam (`RetailHullExtents` below).
int32 HullKind = 0;

// --- The navigator seam --------------------------------------------------------------------------
//
// Retail's `CAI_Navigator` as much of it as this family's four rows reach. It is declared as a
// nested type rather than a free `FElysiumNpcNavigator` in a file of its own because four one-line
// retail bodies do not justify a new substrate class, and because the five navigator words are
// already CHAIN rows onto the motor — a second owner for them would be a second answer to the same
// question. **Named decision**, stated here and in the story report.
struct FNavigator
{
	// `CAI_Navigator+0x18` — the native navigation type. `FUN_1027d990` reads it (29 direct callers,
	// the widest read in this family) and `FUN_1027d9b0` writes it through `0x102eeba0`. The port's
	// `EElysiumNpcNavType` is the same four-value vocabulary (Ground 0, Jump 1, Fly 2, Climb 3), so
	// the write is pushed on to `IElysiumNpcMotor::SetNavigationType` as well as stored.
	int32 NavType = 0;

	// `CAI_Navigator+0x1c` — set to 1 by `OnNavFailed` (`0x102eeae0`, `CAI_Navigator#10`) and by
	// nothing else in the closure. Retail's "this navigator has failed" latch; no consumer in this
	// substrate reads it yet.
	bool bNavFailed = false;

	// `CAI_Navigator::vfunc3` (`0x102ecb50`) copies three of the owner NPC's own pointers —
	// `m_pMotor` (+0x5d44), `m_pMoveProbe` (+0x5d40), `m_pLocalNavigator` (+0x5d38) — into
	// navigator+0x20/+0x24/+0x28 and stores its argument at +0x2c. The three pointers do not exist
	// here, so what survives the port is the FACT that the snapshot was taken and the argument it
	// was taken with. Read by the test and by nothing else.
	bool bSnapshotTaken = false;
	int32 SnapshotArgument = 0;
};
FNavigator Navigator;

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
 *  settles. Writes the file/line marker at owner+0x1b44/+0x1b48 (ABSENT in the shape map), calls
 *  `TaskFail` (slot 448) with the caller's reason, re-plays the resolved link activity through
 *  `SetIdealActivity`, and sets the failed latch. */
void NavOnNavFailed(int32 FailReason);

/** `thunk_FUN_102ee680(m_pNavigator)` — SDK `CAI_Navigator::IsGoalActive()`. The port's mover
 *  answers the same question through `SampleNavigation().bActiveGoal`. */
bool NavIsGoalActive() const;

/** `thunk_FUN_102ee2c0(m_pNavigator)` — SDK `CAI_Navigator::StopMoving()`. Wired to the mover's
 *  own `Stop()`. */
void NavStopMoving();

/** `thunk_FUN_102ee620(m_pNavigator)` — the navigator's route/goal-state word, which
 *  `ValidateNavGoal` requires to be exactly 6. **SEAM**: `IElysiumNpcMotor` keeps no goal type, so
 *  this answers -1 and `ValidateNavGoal` takes retail's own "not that state" arm. */
int32 NavGoalState() const;

/** `thunk_FUN_102ee140(m_pNavigator)` — the navigator's current goal position, SOURCE units.
 *  **SEAM**: the mover keeps no readable goal; answers false and leaves `OutGoal` untouched. */
bool NavGoalPosition(FVector& OutGoalUnits) const;

/** `thunk_FUN_102ee6a0` (is the pending link valid) and `thunk_FUN_102ee510` (its cached activity)
 *  — the pair `FUN_1027a6c0` reads. **SEAM**: no link objects here; answers false. The link's
 *  retail identity is **unrecovered**. */
bool NavLinkActivity(int32& OutActivity) const;

/** `thunk_FUN_102e1e20(m_pMotor, -1)` — `FUN_10382d20`'s cancel of the motor's queued facing/link
 *  state. **SEAM**: shares the Facing family's finding that this mover keeps no facing queue. */
void MotorCancelLinkFacing();

/** `FUN_1029f6c0` — resolve a `CAI_Node` through the navigator's node array (`nav+0x2c`, count at
 *  `[0]`, entries at `[1]`) using the index the argument's route step carries, and answer
 *  `node+0xa0`. **SEAM**: there is no node graph; answers 0, which is retail's own answer for a
 *  null argument or a -1 index. */
int32 NavNodeWordAt(int32 RouteStepIndex) const;

// --- The motor seams -----------------------------------------------------------------------------

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

/** `thunk_FUN_102e7270(m_pMoveProbe, …)` — `CAI_MoveProbe::CheckStandPosition`, the hull/pathfinder
 *  probe `CanStandAt` (`0x102a0ed0`) brackets with `m_bForceNPCCheck`. **SEAM**: the mover answers
 *  no hull probe; answers false. */
bool MoveProbeCheckStandPosition(const FVector& PositionUnits, int32 ProbeFlags) const;

/** `thunk_FUN_1026e940` / `(*DAT_1070b254)->TraceRay` — the engine hull trace `CheckOnGround`
 *  (`0x1026e5e0`), `ValidateNavGoal` (`0x10280360`) and `GetGroundpoint` (`0x103d6a40`) run, all
 *  three with mask `0x202400b`. **SEAM**: nothing in this substrate traces a hull for the kernel;
 *  answers false, which every caller reads as retail's CLEAR trace (`fraction == 1.0`) and no hit
 *  entity. */
struct FKernelHullTrace
{
	float Fraction = 1.f;                  // trace_t +0x?? — 1.0 is "nothing in the way"
	FElysiumEntityHandle HitEntity;        // trace_t::m_pEnt
};
bool KernelHullTrace(const FVector& StartUnits, const FVector& EndUnits, const FVector& HullMins,
	const FVector& HullMaxs, int32 Mask, FKernelHullTrace& OutTrace) const;

/** `thunk_FUN_102d6140(m_eHull)` / `thunk_FUN_102d6160(m_eHull)` — the shared hull table's mins and
 *  maxs for a hull id. **SEAM**: no hull table here; answers false with both left at zero. */
bool RetailHullExtents(int32 Hull, FVector& OutMinsUnits, FVector& OutMaxsUnits) const;

/** `m_Collision`'s slots +4 / +8 — an entity's OBB mins and maxs, SOURCE units, which `CheckStuck`
 *  (`0x103ab580`) builds both boxes from. **SEAM**: `FElysiumEntity` carries no collision extents;
 *  answers false with both left at zero. */
static bool RetailCollisionExtents(const FElysiumEntity& Entity, FVector& OutMinsUnits,
	FVector& OutMaxsUnits);

/** `CBaseEntity::m_edtDerivedType` (+0x004c) — the derived-type word the collision-ignore chain
 *  tests bitwise (`& 0x2`, `& 0x12`, `& 0x14`, `& 0x16`) and the cover chooser tests as `& 4`
 *  PHYSICS_PROP (`docs/vtmb/npc-ai/programs.md` § "The cover and kick chooser"). **SEAM**: only bit
 *  2 is recovered; this runtime stands no derived-type word at all, so it answers 0 and every one of
 *  those gates falls through. What each remaining bit MEANS is **unrecovered**. */
static int32 RetailDerivedType(const FElysiumEntity& Entity);

/** `CBaseEntity::GetFlags2()` bit 3 — the second flag word `CNPC_VWerewolf`'s two collision-ignore
 *  overrides test. **SEAM**: `FElysiumEntity::Flags` is the first word only; answers 0. */
static uint32 RetailFlags2(const FElysiumEntity& Entity);

/** `CBaseEntity::IsStandable()` (slot 164, `0x100b50a0`) — solid flag `0x10` clear, then move type
 *  1 / 6 / 2, else `thunk_FUN_100b5110`. **SEAM**: this substrate carries no solid flags and no
 *  move type, so it answers FALSE, and `CanStandOn` therefore refuses every non-null candidate.
 *  That is the conservative refusal, stated rather than guessed: retail's own answer for an entity
 *  with `0x10` set is also false. */
static bool RetailIsStandable(const FElysiumEntity& Entity);

/** The active weapon's capability word (weapon vtable +0x5a0, slot 360, retail body `0x1014f930`),
 *  which `ShouldMoveAndShoot` requires to carry `0x6000`. **SEAM**: `FElysiumWeapon` stands no such
 *  word; answers 0, so the Troika gate closes and the base rung is never reached. */
uint32 ActiveWeaponCapabilityWord() const;

/** What the seams above were ASKED, so a test can assert that a body reached its motor call and
 *  that the refusal was the recovered one. Read by the test suite and by nothing else. */
struct FMotorSeamLedger
{
	int32 MoveDone = 0;                  // slot 133's tail dispatch of `m_pfnMoveDone` (+0x114)
	int32 IntervalMovementApplied = 0;   // `AutoMovement`'s gated `thunk_FUN_102e0bd0`
	int32 PerformMovement = 0;           // the navigator's vtable slot 5 delegate
	float PerformMovementInterval = 0.f;
	int32 PostRunWeaponUpdates = 0;      // `PostRun`'s ordered pair, tallied on the second half
	float PostRunInterval = 0.f;
	int32 SetupJumpCommits = 0;          // `thunk_FUN_102c4e80`
	int32 CrowOverrideMoves = 0;         // `thunk_FUN_10357be0`
	int32 MoveProbeChecks = 0;           // `CanStandAt`'s `thunk_FUN_102e7270`
	int32 HullTraces = 0;                // every `KernelHullTrace` caller
	int32 JumpArcSolves = 0;             // `thunk_FUN_102c4cc0`
	int32 LinkFacingCancels = 0;         // `thunk_FUN_102e1e20(-1)`
};
mutable FMotorSeamLedger MotorSeams;

/** `CAI_Motor`'s deceleration query (`0x102e1300`, `CAI_Motor#16`) lives on `IElysiumNpcMotor`
 *  itself as `MinStoppingDistance()`; this is the NPC-side read, so a body cites the address at the
 *  point of use. Answers the interface's own floor when there is no motor. */
float MotorMinStoppingDistanceUnits() const;

// --- The console variables this family's ladders read --------------------------------------------
//
// Four `ConVar*` globals, read as `IsCommand() ? 0.0f : m_fValue` (the object's `+0x28`). Two are
// constructed in `MaxYawSpeed`'s own body and so their NAME and DEFAULT are recovered facts read out
// of `.rdata`; two live in uninitialised `.data` that no corpus function ever constructs, exactly
// like the pair the Facing family recorded, and are **unrecovered**.
//
// The seam answers "this substrate has no console", which lands every read on retail's own
// `IsCommand()` arm — 0.0 — except for the two whose registered default IS recovered.
struct FRetailYawConVar
{
	const TCHAR* Name;       // empty where the name is unrecovered
	const TCHAR* Address;
	float Default;
	bool bDefaultRecovered;
};
static const FRetailYawConVar* RetailYawConVars(int32& OutCount);
/** The value a retail `ConVar::GetFloat()` on one of the four answers today. */
static float RetailYawConVarValue(const TCHAR* Address);

// --- The species helpers the jump chain calls out to ---------------------------------------------

/** `CNPC_VChangBros::GetSector(pos)` — the sector id `CheckForJumpAttack` compares against 4 for
 *  both the player and itself. **SEAM**: this substrate has no sector partition; answers 4, which
 *  is the value that CLOSES the gate, so the jump attack is refused rather than allowed on a guess. */
int32 ChangBrosSector(const FVector& PositionUnits) const;

/** `thunk_FUN_102c4cc0(this, out, from, to)` — retail's jump-arc solver, which
 *  `SetJumpVelocityTowardPlayer` (`0x103aad40`) feeds the lead position and then assigns straight to
 *  `SetAbsVelocity`. **SEAM**: no solver here; answers false and the velocity is left alone. */
bool SolveJumpArc(const FVector& FromUnits, const FVector& ToUnits, FVector& OutVelocityUnits) const;

/** `thunk_FUN_102c4e80(this)` — the commit every `SetupJump`/`SetupSuperJump` ends on, which takes
 *  the three jump words this family has just written and starts the leap. **SEAM**: records that
 *  the commit was reached and starts nothing. */
void CommitSetupJump();

/** `CNPC_VAsianVampire::PositionClearForTeleport(pos, 150.0)` (`_DAT_104a9320`) — the clearance test
 *  `SelectJumpbaseNode` filters hint nodes with. **SEAM**: answers false, so the search finds no
 *  node rather than choosing one blind. */
bool PositionClearForTeleport(const FVector& PositionUnits, float RadiusUnits) const;

/** `CNPC_VAsianVampire::AddHintToStoredJumpPositions(hint)` — the ring write that pairs with
 *  `IsPosNearStoredJumpPositions`. Ported: it stores the hint's origin at `m_iLastJumpPositionIdx`
 *  and advances the index modulo 2. The hint's ORIGIN is the seam. */
void AddHintToStoredJumpPositions(int32 HintNode);

/** `CNPC_VVampireBoss::DistToHintCenterLine2D_2(hint, pos)` — the squared 2-D distance from a
 *  position to a hint's centre line that `PlayerInNoJumpZone` (`0x103a9e70`) thresholds at 100.0.
 *  **SEAM**: no hint geometry here; answers false and the zone test finds nobody inside. */
bool DistToHintCenterLine2DSqr(int32 HintNode, const FVector& PositionUnits, float& OutSqr) const;

/** The claimed hint node's type word (`CAI_Hint+0x5dc m_nHintType`) and its origin. `HintNode` is a
 *  bare index in this runtime and no store carries hint types or positions yet — the Squad family
 *  records the same gap on the global hint list — so both answer nothing. */
bool NavHintNodeType(int32 HintNode, int32& OutType) const;
bool NavHintNodeOrigin(int32 HintNode, FVector& OutOriginUnits) const;

/** The global `CAI_Hint` list (`DAT_10925450`, next link `+0x5d8`) that `PlayerInNoJumpZone` and
 *  `SelectJumpbaseNode` walk end to end. **SEAM**: answers an empty list. */
bool NavAllHintNodes(TArray<int32>& OutHintNodes) const;

/** `thunk_FUN_1039ede0(this)` — `CNPC_VMingXiaoTentacle`'s companion/head entity, which its slot 166
 *  excludes from the standable test. **SEAM**: the tentacle proxy chain is the Squad family's
 *  `Proxies[6]` and nothing links a head to it yet; answers null. */
FElysiumEntity* MingXiaoTentacleCompanion() const;

/** `thunk_FUN_101cda50()` — the fixed global entity `CNPC_VRat::ShouldIgnoreCollision` compares
 *  against. **SEAM**, and its retail identity is **unrecovered**: the body takes no argument and
 *  reads a global; answers null. */
FElysiumEntity* RatIgnoredGlobalEntity() const;

/** `thunk_FUN_10357be0(this, interval)` — `CNPC_Crow`'s own move handler, the arm its slot 525
 *  `OverrideMove` takes at nav type 2 (Fly). **SEAM**: records the call and moves nothing; the
 *  ANSWER slot 525 gives (true, "I handled the move") is the ported half. */
void CrowOverrideMove(float Interval);

/** `CBaseAnimating::IsIgnoreCollisionEntity(other)` (`0x1008be20`) — the tail of both
 *  collision-ignore chains: `m_hIgnoreCollisionEntity` resolved and compared against the candidate. */
bool IsIgnoreCollisionEntityTail(const FElysiumEntity* Other) const;

// --- The non-slot bodies of this family ----------------------------------------------------------

/** `CAI_BaseNPC::AutoMovement` `0x10280a50` — slot 250 first, then the interval movement, applied
 *  ONLY when `GetMoveType() == 4` and `FL_FROZEN 0x400` is clear. The gate is the retail contract a
 *  modernization has to keep; the extraction underneath it is Unreal's. */
bool AutoMovement();

/** `CAI_BaseNPC::PerformMovement(a, b)` `0x1026c120` — VProf scaffolding around one delegating call
 *  to the navigator's vtable slot 5, both parameters forwarded. */
void PerformMovement(float Interval, int32 MoveFlags);

/** `CAI_BaseNPC::PostRun` `0x1026c7c0` — the PAIRING and its ORDER: dispatch own vtable +0x408 with
 *  the elapsed interval from `thunk_FUN_1026c540`, then `CBaseCombatCharacter::Weapon_FrameUpdate`
 *  with the same number. */
void PostRun();

/** `CAI_BaseNPC::CheckOnGround` `0x1026e5e0` — the gated ground hull-trace and the two writes it
 *  can make (clear the ground entity, or adopt the traced one). */
void CheckOnGround();

/** `CAI_BaseNPCTroika::CanStandAt` `0x102a0ed0` — `m_bForceNPCCheck` around the move probe. */
bool CanStandAt(const FVector& PositionUnits, int32 Flags);

/** `CAI_BaseNPC::FUN_1027dc80` `0x1027dc80` — the BASE branch of slot 531 `OnObstructingDoor`. The
 *  Troika-line body slot 531 carries is `0x102984a0` and belongs to story 29d, so this lands as a
 *  named method rather than as a second definition of the generated virtual. */
enum class EObstructingDoorResult : int32 { Ok = 0, Illegal = -1 };
bool OnObstructingDoorBase(float& InOutMoveGoalMaxDistance, int32 DoorState, float DistClear,
	EObstructingDoorResult& OutResult) const;

/** `CCineNPC`/`CCineAI`/`CCineAISchedule`'s shared slot 178 `Blocked` (`0x101a7580`) — a `return;`
 *  with the argument ignored. Answers whether the class this NPC IS is one of the three, which is
 *  the whole of the recovered behaviour. */
bool BlockedIsNoOp() const;

/** `0x1039aaf0` / `0x1039ab10` — `CNPC_VMingXiao::m_bBlockedByFriend`'s setter and getter. */
void SetBlockedByFriend(bool bBlocked);
bool BlockedByFriend() const;

/** `CNPC_VChangBros::CheckForJumpAttack` `0x1036c8d0`. */
bool CheckForJumpAttack();

/** `CNPC_VSabbatLeader::CheckStuck` `0x103ab580`. */
void CheckStuck();

/** `CNPC_VWerewolf::GetGroundpoint` `0x103d6a40`. */
FVector GetGroundpoint(const FVector& PointUnits) const;

/** `CNPC_VAsianVampire::GetJumpSchedule` `0x10362430`. */
int32 GetJumpSchedule() const;

/** `CNPC_VAsianVampire::IsPosNearStoredJumpPositions` `0x103618a0`. */
bool IsPosNearStoredJumpPositions(const FVector& PositionUnits) const;

/** `CAI_BaseHumanoid::MaxYawSpeed` `0x102624b0`, the branch answer for `CAI_BaseHumanoid#516`.
 *  Slot 516 itself is the Troika body (`0x10297ce0`) and every spawnable species dispatches there. */
static float MaxYawSpeedHumanoid(int32 Activity);

/** `CGeneric_NPC::MaxYawSpeed` `0x1035a810`, byte-identical at `CGeneric_NPC_bathack`
 *  (`0x1035b080`) and `CGenericSabbat_NPC` (`0x1035be80`). */
static float MaxYawSpeedGeneric(int32 Activity);

/** `CAI_BaseNPC::MaxYawSpeed` `0x10280bb0` — the base line's single constant. */
static float MaxYawSpeedBase();

/** `CNPC_VMingXiao::MaxYawSpeed` `0x10394930` — the tuning record's +0x48 in the 0x112a–0x112d band
 *  and +0x44 elsewhere, read through the record seam. */
static float MaxYawSpeedMingXiao(int32 Activity, TFunctionRef<float(int32)> TuningField);

/** `CNPC_VDog::MaxYawSpeed` `0x10374130` and `CNPC_VTzimisce::MaxYawSpeed` `0x103ba020` — the two
 *  species that replace the Troika ladder wholesale rather than adding an arm to it. Both take the
 *  same "turning" arm at `m_afMemory & 0x2000`; that arm is shared with the Troika body and lives in
 *  `MaxYawSpeedTurningArm` below. */
float MaxYawSpeedDog();
float MaxYawSpeedTzimisce();

/** The arm all three of `CAI_BaseNPCTroika` / `CNPC_VDog` / `CNPC_VTzimisce` take when
 *  `m_afMemory & 0x2000` (AT_COVER_HINT) is set: `ABS(GetIdealYawSpeed()) * cvar`, floored at 1.0.
 *  The cvar differs per species and each names its own address. */
float MaxYawSpeedTurningArm(const TCHAR* ConVarAddress);

/** `CAI_BaseNPCTroika::NavIgnoreCollision`'s and `ShouldIgnoreCollision`'s shared head: the
 *  `m_bForceNPCCheck` / `NAV_IGNORE_NPC` / `m_hKickPhysicsProp` / combat-weapon gates both chains run
 *  before they diverge (`0x1029afc0` and `0x1029b180`, arms 1–3). */
bool IgnoreCollisionSharedHead(const FElysiumEntity* Other) const;

/** `CNPC_VGargoyle::NavIgnoreCollision` `0x10379490`'s classname filter, as a pure function so the
 *  three names it matches are assertable without an entity. */
static bool GargoyleIgnoresClassname(const FString& Classname);

/** `CAI_Navigator::OnNavFailed`'s activity resolution, `FUN_1027a6c0` `0x1027a6c0`: the link's
 *  cached activity when the link is valid and not -1, else 1 (`ACT_IDLE`). */
int32 ResolveLinkActivity() const;

/** `FUN_10382d20` `0x10382d20` — the other half of the same unrecovered link object: cancel the
 *  motor's queued facing/link state with -1. */
void ClearLinkActivity();

/** `FUN_102bf7e0` `0x102bf7e0` — stop an active goal, then set `m_bShouldMove` unconditionally.
 *  Target is 29c's best guess at the retail name; the body is exact. */
void ResumeScheduledMove();

/** `CNPC_VAsianVampire::SelectJumpbaseNode` `0x10361730` — nearest teleport-clear hint of type
 *  18000, then remember it. */
int32 SelectJumpbaseNode();

/** `CNPC_VSabbatLeader::SetJumpVelocityTowardPlayer` `0x103aad40`. */
void SetJumpVelocityTowardPlayer();

/** `CNPC_VSabbatLeader::PlayerInNoJumpZone` `0x103a9e70`. */
bool PlayerInNoJumpZone() const;

/** `CNPC_VAsianVampire::SetupJump` `0x10361a70` (rise constant 100.0) and `CNPC_VSheriffMan::SetupJump`
 *  `0x103b1300` (400.0) — one behaviour, two species constants, so one method and a data table. */
void SetupJump(float Enabled);

/** `CNPC_VChangBros::SetupSuperJump` `0x1036e160`. */
void SetupSuperJump(float Enabled);

/** `CNPC_VAsianVampire::StationaryForTooLong` `0x10362670` and `UpdateMovedTimeStamp` `0x10362540`. */
bool StationaryForTooLong() const;
void UpdateMovedTimeStamp();

/** `CNPC_VTzimisce::vfunc410` `0x103bf580` — the species branch of slot 410. Slot 410's own body is
 *  the base `0x101a6420` and remains the generator's. */
bool TranslateNavGoalPositionTzimisce(const FVector& GoalUnits, FVector& OutGoalUnits) const;

/** `CAI_BaseNPC::IsJumpLegal`'s shared geometry helper `FUN_10280790` `0x10280790`, as a pure
 *  function of the three points and the three thresholds, so both fills of slot 521 are one body. */
static bool IsJumpLegalGeometry(const FVector& StartUnits, const FVector& ApexUnits,
	const FVector& EndUnits, float MaxRise, float MaxDrop, float MaxDistance);

// --- The species tables --------------------------------------------------------------------------
//
// Every row carries the retail class it came from AND the retail address of the body, so a reader
// can check it against `docs/vtmb/npc-kernel/slots.md`.

/** Slot 516 `MaxYawSpeed`: the classes that replace the Troika ladder, and with what. */
struct FMaxYawSpeedSpecies
{
	const TCHAR* RetailClass;
	const TCHAR* Body516;
};
static const FMaxYawSpeedSpecies* MaxYawSpeedSpeciesRows(int32& OutCount);

/** Slots 68/69 `ShouldIgnoreCollision` / `NavIgnoreCollision`: the species that add an arm in front
 *  of the Troika bodies, with the address of each arm. An empty string means the class does not
 *  replace that slot. */
struct FIgnoreCollisionSpecies
{
	const TCHAR* RetailClass;
	const TCHAR* Body68;
	const TCHAR* Body69;
};
static const FIgnoreCollisionSpecies* IgnoreCollisionSpeciesRows(int32& OutCount);

/** The two `SetupJump` species and their rise constant, read out of `.rdata`. */
struct FSetupJumpSpecies
{
	const TCHAR* RetailClass;
	const TCHAR* Body;
	const TCHAR* RiseConstant;
	float Rise;
};
static const FSetupJumpSpecies* SetupJumpSpeciesRows(int32& OutCount);
static const FSetupJumpSpecies* SetupJumpSpeciesOf(const TCHAR* InRetailClass);

/** Slots 521/522/523 `IsJumpLegal` / `StepHeight` / `GetMaxJumpSpeed`: the movement tunables, one
 *  row per class that answers them differently from the Troika line. */
struct FJumpTunableSpecies
{
	const TCHAR* RetailClass;
	const TCHAR* Body;
	float StepHeight;
	float MaxJumpSpeed;
	float JumpLegalRise;
	float JumpLegalDrop;
	float JumpLegalDistance;
};
static const FJumpTunableSpecies* JumpTunableSpeciesRows(int32& OutCount);
static const FJumpTunableSpecies* JumpTunableSpeciesOf(const TCHAR* InRetailClass);
