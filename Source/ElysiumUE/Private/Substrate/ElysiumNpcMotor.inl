// Story 29c-1, family **Motor** — the declarations of this family's layer 0–9 kernel bodies.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. A body that fills a Troika-line vtable slot is NOT declared here:
// the generator already declares that virtual, and this family only defines it. What lands here is
// the non-slot half — the retail helpers, free functions and non-virtual methods of layers 0–9
// whose overlay row names a port method on `FElysiumNpc` that did not exist before this story.
//
// The definitions are in `Substrate/ElysiumNpcMotor.cpp` and the tests in
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

// The CURRENT activity number every fill of slot 516 switches on is +0x0fec `m_Activity`, which the
// **Positions** family declares as `ActivityNumber`; the yaw ladders below read that member rather
// than a second copy of the same word. The Facing family carries the IDEAL one beside it
// (`IdealActivityNumber`, +0x0ff0).

// +0x1568 `CAI_BaseNPCTroika::m_eHull` — the STANDING hull. It sizes the collision box and every
// trace and line-of-sight helper resolves its extents from it (`CNPC_VWerewolf::GetGroundpoint`
// `0x103d6a40` among them). Below the shape map's band, so 29b did not bind it; the extents
// themselves come from the generated table (`RetailHullExtents` below).
//
// Both words are filled from `ElysiumRetailHulls::ClassHulls` by the body that wears this kernel,
// keyed on the retail class. A class with no row of its own inherits the nearest ancestor's, which
// is retail's own arrangement: `CAI_BaseNPC`'s constructor zeroes both before any derived
// constructor runs (`navigation-jump-links.md` § "The two hull words", 2026-09-20).
int32 HullKind = 0;

// +0x156c — the PATHING hull, and a different word from the one above on three species. It has no
// datamap record in retail and is never saved: `CAI_Navigator::SetGoal 0x102ecd2c` caches it and
// the whole A* family feeds it to `CAI_Node::GetPosition`, so it is what decides which NavMesh
// agent a body paths on. Seven `GetPosition` sites pass `m_eHull` instead, none of them routing —
// patrol-goal anchoring, the zombie's patrol arm, extrapolated routes and debug drawing.
//
// The Sheriff stands on hull 21 and paths on 0; Hengeyokai is the inverse (0 standing, 18
// pathing); Ming Xiao splits 15 / 16, which is what `MING_XIAO_PATHING_HULL` exists for. For every
// other species the two agree.
int32 PathingHullKind = 0;

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
	float Fraction = 1.f;                  // trace_t +0x2c — 1.0 is "nothing in the way"
	FElysiumEntityHandle HitEntity;        // trace_t::m_pEnt
	FVector PlaneNormal = FVector::ZeroVector;  // trace_t +0x18 plane.normal
	bool bAllSolid = false;                // trace_t +0x36 allsolid
	bool bStartSolid = false;              // trace_t +0x37 startsolid
	FVector EndPosUnits = FVector::ZeroVector;  // trace_t +0x0c endpos (the seam's clear answer: the end)
};
bool KernelHullTrace(const FVector& StartUnits, const FVector& EndUnits, const FVector& HullMins,
	const FVector& HullMaxs, int32 Mask, FKernelHullTrace& OutTrace) const;

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

/** The shared hull table's mins and maxs for a hull id, in SOURCE units.
 *
 *  Answers from the replayed `NAI_Hull` table (`Substrate/ElysiumRetailHullTable.h`,
 *  `docs/vtmb/data/hull_table.json`). False, with both left at zero, for a hull id retail's own
 *  table does not carry -- which is what every caller's failure arm was written against. */
bool RetailHullExtents(int32 Hull, EElysiumHullExtents Which, FVector& OutMinsUnits,
	FVector& OutMaxsUnits) const;

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

/** `CBaseEntity::IsStandable()` (slot 164, `0x100b50a0`) — solid flag `0x10` clear, then move type
 *  1 / 6 / 2, else `thunk_FUN_100b5110`. **SEAM**: this substrate carries no solid flags and no
 *  move type, so it answers FALSE, and `CanStandOn` therefore refuses every non-null candidate.
 *  That is the conservative refusal, stated rather than guessed: retail's own answer for an entity
 *  with `0x10` set is also false. */
static bool RetailIsStandable(const FElysiumEntity& Entity);

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
// The `ConVar*` globals the yaw ladders read, as `IsCommand() ? 0.0f : m_fValue` (the object's
// `+0x28`). Two are constructed in `MaxYawSpeed`'s own body and carry their recovered name and
// default here; the other four (the three turning scalars and `debug_turning_speed`) are rows of the
// tunables table (`docs/vtmb/npc-ai/convars.md`) and are read through it.
struct FRetailYawConVar
{
	const TCHAR* Name;
	const TCHAR* Address;    // the `DAT_` pointer the ladder reads (object + 4)
	float Default;           // the local default, when `Table` is `EConVar::Count`
	ElysiumNpcTunables::EConVar Table;
};
static const FRetailYawConVar* RetailYawConVars(int32& OutCount);
/** The value a retail `ConVar::GetFloat()` on one of the four answers today. */
static float RetailYawConVarValue(const TCHAR* Address);

// --- The species helpers the jump chain calls out to ---------------------------------------------

/** The claimed hint node's type word (`CAI_Hint+0x5dc m_nHintType`) and its origin. `HintNode` is a
 *  bare index in this runtime and no store carries hint types or positions yet — the Squad family
 *  records the same gap on the global hint list — so both answer nothing. */
bool NavHintNodeType(int32 HintNode, int32& OutType) const;
bool NavHintNodeOrigin(int32 HintNode, FVector& OutOriginUnits) const;

/** The global `CAI_Hint` list (`DAT_10925450`, next link `+0x5d8`) that `PlayerInNoJumpZone` and
 *  `SelectJumpbaseNode` walk end to end. **SEAM**: answers an empty list. */
bool NavAllHintNodes(TArray<int32>& OutHintNodes) const;

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

/** `CAI_BaseNPC::MaxYawSpeed` `0x10280bb0` — the base line's single constant. */
static float MaxYawSpeedBase();

/** The arm all three of `CAI_BaseNPCTroika` / `CNPC_VDog` / `CNPC_VTzimisce` take when
 *  `m_afMemory & 0x2000` (AT_COVER_HINT) is set: `ABS(GetIdealYawSpeed()) * cvar`, floored at 1.0.
 *  The cvar differs per species and each names its own address. */
float MaxYawSpeedTurningArm(const TCHAR* ConVarAddress);

/** `CAI_BaseNPCTroika::NavIgnoreCollision`'s and `ShouldIgnoreCollision`'s shared head: the
 *  `m_bForceNPCCheck` / `NAV_IGNORE_NPC` / `m_hKickPhysicsProp` / combat-weapon gates both chains run
 *  before they diverge (`0x1029afc0` and `0x1029b180`, arms 1–3). */
bool IgnoreCollisionSharedHead(const FElysiumEntity* Other) const;

/** `CAI_Navigator::OnNavFailed`'s activity resolution, `FUN_1027a6c0` `0x1027a6c0`: the link's
 *  cached activity when the link is valid and not -1, else 1 (`ACT_IDLE`). */
int32 ResolveLinkActivity() const;

/** `FUN_102bf7e0` `0x102bf7e0` — stop an active goal, then set `m_bShouldMove` unconditionally.
 *  Target is 29c's best guess at the retail name; the body is exact. */
void ResumeScheduledMove();

/** `CAI_BaseNPC::IsJumpLegal`'s shared geometry helper `FUN_10280790` `0x10280790`, as a pure
 *  function of the three points and the three thresholds, so both fills of slot 521 are one body. */
static bool IsJumpLegalGeometry(const FVector& StartUnits, const FVector& ApexUnits,
	const FVector& EndUnits, float MaxRise, float MaxDrop, float MaxDistance);

// --- The movement-tunables table ----------------------------------------------------------------
//
// Every row carries the retail class it came from AND the retail address of the body, so a reader
// can check it against `docs/vtmb/npc-kernel/slots.md`. The species answers of slots 68/69, 516 and
// the `SetupJump` rises are their classes' own overrides (story 5 step 4); this table's rows are the
// Troika line, the non-Troika branch and the debug hull, which the Troika bodies read.

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
