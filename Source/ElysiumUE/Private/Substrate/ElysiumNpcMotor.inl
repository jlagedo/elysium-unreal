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

// The CURRENT activity number every fill of slot 516 switches on is +0x0fec `m_Activity`, which the
// **Positions** family declares as `ActivityNumber`; the yaw ladders below read that member rather
// than a second copy of the same word. The Facing family carries the IDEAL one beside it
// (`IdealActivityNumber`, +0x0ff0).

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

/** `FUN_1029f6c0` — resolve a `CAI_Node` through the navigator's node array (`nav+0x2c`, count at
 *  `[0]`, entries at `[1]`) using the index the argument's route step carries, and answer
 *  `node+0xa0`. **SEAM**: there is no node graph; answers 0, which is retail's own answer for a
 *  null argument or a -1 index. */
int32 NavNodeWordAt(int32 RouteStepIndex) const;

// --- The motor seams -----------------------------------------------------------------------------

/** `thunk_FUN_102e7270(m_pMoveProbe, …)` — `CAI_MoveProbe::CheckStandPosition`, the hull/pathfinder
 *  probe `CanStandAt` (`0x102a0ed0`) brackets with `m_bForceNPCCheck`. **SEAM**: the mover answers
 *  no hull probe; answers false. */
bool MoveProbeCheckStandPosition(const FVector& PositionUnits, int32 ProbeFlags) const;

/** `CBaseEntity::m_edtDerivedType` (+0x004c) — the derived-type word the collision-ignore chain
 *  tests bitwise (`& 0x2`, `& 0x12`, `& 0x14`, `& 0x16`) and the cover chooser tests as `& 4`
 *  PHYSICS_PROP (`docs/vtmb/npc-ai/programs.md` § "The cover and kick chooser"). **SEAM**: only bit
 *  2 is recovered; this runtime stands no derived-type word at all, so it answers 0 and every one of
 *  those gates falls through. What each remaining bit MEANS is **unrecovered**. */
static int32 RetailDerivedType(const FElysiumEntity& Entity);

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

// --- The non-slot bodies of this family ----------------------------------------------------------

/** `CAI_BaseNPCTroika::CanStandAt` `0x102a0ed0` — `m_bForceNPCCheck` around the move probe. */
bool CanStandAt(const FVector& PositionUnits, int32 Flags);

/** `CCineNPC`/`CCineAI`/`CCineAISchedule`'s shared slot 178 `Blocked` (`0x101a7580`) — a `return;`
 *  with the argument ignored. Answers whether the class this NPC IS is one of the three, which is
 *  the whole of the recovered behaviour. */
bool BlockedIsNoOp() const;

/** The arm all three of `CAI_BaseNPCTroika` / `CNPC_VDog` / `CNPC_VTzimisce` take when
 *  `m_afMemory & 0x2000` (AT_COVER_HINT) is set: `ABS(GetIdealYawSpeed()) * cvar`, floored at 1.0.
 *  The cvar differs per species and each names its own address. */
float MaxYawSpeedTurningArm(const TCHAR* ConVarAddress);

/** `CAI_BaseNPCTroika::NavIgnoreCollision`'s and `ShouldIgnoreCollision`'s shared head: the
 *  `m_bForceNPCCheck` / `NAV_IGNORE_NPC` / `m_hKickPhysicsProp` / combat-weapon gates both chains run
 *  before they diverge (`0x1029afc0` and `0x1029b180`, arms 1–3). */
bool IgnoreCollisionSharedHead(const FElysiumEntity* Other) const;

/** `FUN_102bf7e0` `0x102bf7e0` — stop an active goal, then set `m_bShouldMove` unconditionally.
 *  Target is 29c's best guess at the retail name; the body is exact. */
void ResumeScheduledMove();

// --- The movement-tunables table ----------------------------------------------------------------
//
// Every row carries the retail class it came from AND the retail address of the body, so a reader
// can check it against `docs/vtmb/npc-kernel/slots.md`. The species answers of slots 68/69, 516 and
// the `SetupJump` rises are their classes' own overrides (story 5 step 4); this table's rows are the
// Troika line, the non-Troika branch and the debug hull, which the Troika bodies read.

static const FJumpTunableSpecies* JumpTunableSpeciesRows(int32& OutCount);
