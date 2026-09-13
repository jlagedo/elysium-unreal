#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumSchedule.h"

// Story 29c-1, family **Motor** — `CAI_Motor`, `CAI_Navigator`, and everything the NPC asks of its
// motor: the step and jump tunables, the jump setup/legality chain, the two collision-ignore
// chains, the yaw-speed ladder, the ground and stuck probes, and the move-done / nav-failure hooks.
//
// 73 rows of `order.md` layers 0–9, ported arm by arm in retail's order. Every threshold below was
// read out of the pinned retail `vampire.dll`'s `.rdata` at its cited address (image base
// `0x10000000`), so the numbers are recovered facts, not estimates. The walked prose is
// `docs/vtmb/npc-ai/shape.md`.
//
// Two conventions govern the whole file and are stated once:
//
//   * **Units.** Every retail constant here is in SOURCE units, and every position a retail body
//     reads is `GetAbsOrigin()` in the same. This world's `Origin` is Unreal centimetres with Y
//     negated (`bsp.source_to_unreal`). `SourceOf`/`PortOf` below are the one place that conversion
//     happens, and every body works in retail's own numbers so a threshold stays checkable.
//   * **The seams.** There is no navigator, no node graph, no move probe, no hull table and no
//     collision-extent surface in this substrate. Each such input is asked through a named accessor
//     that answers NOTHING and cites the retail call. A body is never rewritten around a missing
//     input; it runs its recovered arms and the refusal lands where retail's own negative answer
//     would.

namespace
{
	// --- The recovered constants, by address ----------------------------------------------------

	constexpr float GStepHeightBase = 18.0f;        // _DAT_10453b94, `0x101a6b40` / `0x101a6b60`
	constexpr float GMaxJumpSpeedTroika = 36.0f;    // _DAT_1044faa8, `0x101aa670`
	constexpr float GJumpGravity = 350.0f;          // _DAT_10477ce8, `0x101a6b80`
	constexpr float GTestHullTunable = 40.0f;       // _DAT_10462950, `0x102d72b0` / `0x102d72d0`

	// `FUN_10280790`'s slack and its apex scale. Both are qword loads the x87 widens, so they are
	// doubles in `.rdata` and floats at the comparison.
	constexpr float GJumpLegalSlack = 0.1f;         // _DAT_104493d0
	constexpr float GJumpApexScale = 1.25f;         // _DAT_10460020

	// The yaw-speed ladder's constants. `GYawFloor` is also the clamp every "turning" arm ends on.
	constexpr float GYawDefault = 45.0f;            // _DAT_1049949c
	constexpr float GYawRun = 160.0f;               // _DAT_1047a3ac
	constexpr float GYawCrouch = 30.0f;             // _DAT_104492a8
	constexpr float GYawHumanoidMove = 15.0f;       // _DAT_10463584
	constexpr float GYawHumanoidCrouch = 60.0f;     // _DAT_104492a4
	constexpr float GYawGenericCrouch = 120.0f;     // _DAT_1044f00c
	constexpr float GYawTzimisceIdle = 5.0f;        // _DAT_10454110
	constexpr float GYawTzimisceDefault = 11.0f;    // _DAT_104cc504
	constexpr float GYawFloor = 1.0f;               // _DAT_104454c0

	// `CheckOnGround` `0x1026e5e0`.
	constexpr float GCheckOnGroundInterval = 0.5f;  // _DAT_104454d0
	constexpr double GCheckOnGroundSlack = -0.001;  // _DAT_10497530, a qword
	constexpr float GCheckOnGroundUp = 0.1f;        // _DAT_104493d0
	constexpr float GCheckOnGroundDown = 4.0f;      // _DAT_10449148
	constexpr float GTraceClearFraction = 1.0f;     // _DAT_10449280
	constexpr int32 GGroundTraceMask = 0x202400b;
	constexpr int32 GCoverTraceMask = 0x2804091;    // `ValidateNavGoal`'s

	// The two `SetupJump` rise constants the species table below carries. The rest of the jump
	// family's constants live beside their bodies in `ElysiumNpcKernelMotor2.cpp`.
	constexpr float GAsianJumpRise = 100.0f;        // _DAT_104a9310
	constexpr float GSheriffJumpRise = 400.0f;      // _DAT_104c614c

	// Retail activity numbers the yaw ladders switch on. Named so the switch reads like the binary.
	constexpr int32 GActIdle = 1;
	constexpr int32 GActIdleAngry = 5;
	constexpr int32 GActWalk = 9;
	constexpr int32 GActRunHumanoid = 0x12;
	constexpr int32 GActRun = 0x13;
	constexpr int32 GActCrouchIdle = 0x3b;
	constexpr int32 GActCrouchWalk = 0x3c;

	// `m_afMemory` (+0x5d8c) bit 0x2000 — the "turning" tag the turn ladder writes (the Facing
	// family's `bTagsTurnMemory`) and all three of the yaw-speed overrides branch on.
	constexpr uint32 GMemoryTurning = 0x2000;
	// The two `m_bfAINPCFlags` (+0x14b8) bits this file tests are named in `ElysiumNpcFlags.h`:
	// `PLAYING_FACE_ANIM` (0x8000000), which suppresses the yaw ladder, and `SLEEPING` (0x20000),
	// which both ignore chains test on the OTHER entity.
	// `bits_CAP_MOVE_SHOOT`, bit 6 of the capability word, which the base `ShouldMoveAndShoot`
	// returns (`0x10278c60`: `CapabilitiesGet() >> 6 & 1`).
	constexpr int32 GCapMoveShoot = 6;
	// The active weapon's capability mask `CAI_BaseNPCTroika::ShouldMoveAndShoot` requires.
	constexpr uint32 GWeaponMoveShootMask = 0x6000;

	// Conditions this family touches that `EElysiumNpcCond` does not name. Retail's `CAI_BaseNPC`
	// registrar is one dense namespace 0x00..0x76 and 0x73 sits in the unnamed tail of it; 0x7b is
	// above the base band entirely, so it is a species registration. Both are carried as retail's
	// own number.
	constexpr EElysiumNpcCond GCondOnGround = static_cast<EElysiumNpcCond>(0x73);
	constexpr EElysiumNpcCond GCondNavGoalInvalid = static_cast<EElysiumNpcCond>(0x39);

	// `TaskFail`'s reason on the `ValidateNavGoal` failure (`0x10280360`, `vtable+0x700` slot 448).
	constexpr int32 GFailNoCover = 0x1b;

	// --- Units ----------------------------------------------------------------------------------

	// This world's centimetres into retail's Source units. `Origin` is Unreal's axes, where
	// `bsp.source_to_unreal` negated Y; every retail body reads `GetAbsOrigin()`, so the sign comes
	// back here and stays back for the whole body.
	FVector SourceOf(const FVector& Cm)
	{
		return FVector(Cm.X / ElysiumMove::U, -Cm.Y / ElysiumMove::U, Cm.Z / ElysiumMove::U);
	}

	// --- The species tables ---------------------------------------------------------------------

	// Slot 516 `MaxYawSpeed`. `CAI_BaseNPCTroika`'s own body (`0x10297ce0`) is what slot 516 carries
	// for every spawnable species; these are the classes that replace it. `CNPC_VWerewolf`'s
	// override (`0x103d0a30`) is a scope-trace wrapper around an unconditional forward to the
	// Troika body, so it is listed for the ledger's sake and answers the family default.
	constexpr FElysiumNpc::FMaxYawSpeedSpecies GMaxYawSpeedSpecies[] =
	{
		{ TEXT("CAI_BaseHumanoid"),     TEXT("0x102624b0") },
		{ TEXT("CGeneric_NPC"),         TEXT("0x1035a810") },
		{ TEXT("CGeneric_NPC_bathack"), TEXT("0x1035b080") },
		{ TEXT("CGenericSabbat_NPC"),   TEXT("0x1035be80") },
		{ TEXT("CNPC_VDog"),            TEXT("0x10374130") },
		{ TEXT("CNPC_VMingXiao"),       TEXT("0x10394930") },
		{ TEXT("CNPC_VTzimisce"),       TEXT("0x103ba020") },
		{ TEXT("CNPC_VWerewolf"),       TEXT("0x103d0a30") },
		{ TEXT("CAI_BaseNPC"),          TEXT("0x10280bb0") },
	};

	// Slots 68 and 69. The Troika bodies (`0x1029afc0` / `0x1029b180`) are the shared chain; these
	// are the species arms that run in front of them. An empty address means the class does not
	// replace that slot at all.
	constexpr FElysiumNpc::FIgnoreCollisionSpecies GIgnoreCollisionSpecies[] =
	{
		{ TEXT("CNPC_VGargoyle"),           TEXT(""),           TEXT("0x10379490") },
		{ TEXT("CNPC_VHengeyokai"),         TEXT(""),           TEXT("0x10380f90") },
		{ TEXT("CNPC_VMingXiao"),           TEXT(""),           TEXT("0x10396fd0") },
		{ TEXT("CNPC_VMingXiaoTentacle"),   TEXT("0x1039eb50"), TEXT("0x1039eb90") },
		{ TEXT("CNPC_VRat"),                TEXT("0x103ad6d0"), TEXT("") },
		{ TEXT("CNPC_VTzimisce"),           TEXT(""),           TEXT("0x103bfa00") },
		{ TEXT("CNPC_VWerewolf"),           TEXT("0x103d9ab0"), TEXT("0x103d9ba0") },
	};

	// `SetupJump`. One behaviour, two species, one constant apart.
	constexpr FElysiumNpc::FSetupJumpSpecies GSetupJumpSpecies[] =
	{
		{ TEXT("CNPC_VAsianVampire"), TEXT("0x10361a70"), TEXT("_DAT_104a9310"), GAsianJumpRise },
		{ TEXT("CNPC_VSheriffMan"),   TEXT("0x103b1300"), TEXT("_DAT_104c614c"), GSheriffJumpRise },
	};

	// Slots 521/522/523 — the movement tunables. `CAI_BaseNPCTroika` is the row every spawnable
	// species answers with: step height from the base body slot 522 carries (`0x101a6b40`), jump
	// speed from Troika's own override of 523 (`0x101aa670`), jump legality from the base's 521
	// (`0x10280880`). `CAI_BaseNPC` is the branch answer for the non-Troika line, whose 523 returns
	// the SAME constant as its step height; `CAI_TestHull` is the debug hull, generous on all five.
	constexpr FElysiumNpc::FJumpTunableSpecies GJumpTunableSpecies[] =
	{
		{ TEXT("CAI_BaseNPCTroika"), TEXT("0x101aa670"), GStepHeightBase, GMaxJumpSpeedTroika,
			80.0f, 250.0f, 160.0f },
		{ TEXT("CAI_BaseNPC"), TEXT("0x101a6b60"), GStepHeightBase, GStepHeightBase,
			80.0f, 250.0f, 160.0f },
		{ TEXT("CAI_TestHull"), TEXT("0x102d72d0"), GTestHullTunable, GTestHullTunable,
			1024.0f, 1024.0f, 1024.0f },
	};

	// The four `ConVar*` globals this family's ladders read, as `IsCommand() ? 0.0f : m_fValue`.
	// The two `MaxYawSpeed` constructs in its own body carry a recovered name and default (read from
	// `.rdata` at the ctor's argument addresses); the other two live in uninitialised `.data` that no
	// corpus function constructs, exactly like the pair the Facing family recorded.
	constexpr FElysiumNpc::FRetailYawConVar GRetailYawConVars[] =
	{
		{ TEXT("debug_slow_idle_yaw_speed"), TEXT("0x10924e94"), 20.0f, true },
		{ TEXT("debug_slow_walk_yaw_speed"), TEXT("0x1092411c"), 25.0f, true },
		// The turning-arm scalar. `CAI_BaseNPCTroika` reads `0x10924c94`, `CNPC_VDog` reads
		// `0x1093ad24` and `CNPC_VTzimisce` reads `0x1093c9fc` — three distinct cvars, all three
		// unconstructed in the corpus, so all three are unrecovered.
		{ TEXT(""), TEXT("0x10924c94"), 0.0f, false },
		{ TEXT(""), TEXT("0x1093ad24"), 0.0f, false },
		{ TEXT(""), TEXT("0x1093c9fc"), 0.0f, false },
		// The idle-arm alternative `MaxYawSpeed` falls to when turning anims are ON.
		{ TEXT(""), TEXT("0x10923e84"), 0.0f, false },
	};

	// `thunk_FUN_101e8da0(0x10739d08)` — `CNPC_VMingXiao`'s playback/turn tuning record, read by
	// field offset. **SEAM**: this substrate holds no such table, so every field answers 0. The
	// Facing family records the same gap for the same record; the two are deliberately separate
	// file-local helpers rather than one shared member, because neither family owns the other's file.
	float MingXiaoTuningField(int32)
	{
		return 0.f;
	}
}

// -------------------------------------------------------------------------------------------------
// The navigator seam.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::NavGetType() const
{
	// `FUN_1027d990`: `return m_pNavigator->field_0x18;` — one word, 29 direct callers, the widest
	// read of this family.
	return Navigator.NavType;
}

void FElysiumNpc::NavSetType(int32 Type)
{
	// `FUN_1027d9b0` → `0x102eeba0`: `m_pNavigator->field_0x18 = value`.
	Navigator.NavType = Type;
	// The port's navigator IS `IElysiumNpcMotor`, and it carries the same four-value vocabulary
	// (`EElysiumNpcNavType`: Ground 0, Jump 1, Fly 2, Climb 3), so the stored word is pushed on.
	// A value outside the four has no counterpart and is stored only — retail stores anything.
	if (Motor != nullptr && Type >= 0 && Type <= 3)
	{
		Motor->SetNavigationType(static_cast<EElysiumNpcNavType>(Type));
	}
}

void FElysiumNpc::NavSnapshotOwnerPointers(int32 Argument)
{
	// `CAI_Navigator::vfunc3` `0x102ecb50`:
	//     this->+0x20 = owner->+0x5d44;   // m_pMotor
	//     this->+0x24 = owner->+0x5d40;   // m_pMoveProbe
	//     this->+0x28 = owner->+0x5d38;   // m_pLocalNavigator
	//     this->+0x2c = param_1;
	// All three owner words are `ELYSIUM_NPC_WORD_CHAIN` rows onto the one motor, so there are no
	// three pointers to snapshot. What survives is that the snapshot happened and what it was taken
	// with.
	Navigator.bSnapshotTaken = true;
	Navigator.SnapshotArgument = Argument;
}

bool FElysiumNpc::NavIsGoalActive() const
{
	// `thunk_FUN_102ee680(m_pNavigator)` — SDK `CAI_Navigator::IsGoalActive()`, which
	// `CAI_BaseNPC::IsMoving` (`0x10280300`) is a one-line forward to. The port's mover answers the
	// same question, so this is a wire and not a seam.
	return Motor != nullptr && Motor->SampleNavigation().bActiveGoal;
}

void FElysiumNpc::NavStopMoving()
{
	// `thunk_FUN_102ee2c0(m_pNavigator)` — SDK `CAI_Navigator::StopMoving()`.
	if (Motor != nullptr)
	{
		Motor->Stop();
	}
}

int32 FElysiumNpc::NavGoalState() const
{
	// `thunk_FUN_102ee620(m_pNavigator)` — the navigator's route/goal-state word, which
	// `ValidateNavGoal` requires to be exactly 6. **SEAM**: `IElysiumNpcMotor` keeps no goal type,
	// so this answers -1, which is "not that state" and is the arm retail takes for every other
	// value.
	return INDEX_NONE;
}

bool FElysiumNpc::NavGoalPosition(FVector& OutGoalUnits) const
{
	// `thunk_FUN_102ee140(m_pNavigator)` — the navigator's goal point. **SEAM**: the mover keeps no
	// readable goal; the caller is left with its own untouched vector.
	(void)OutGoalUnits;
	return false;
}

bool FElysiumNpc::NavLinkActivity(int32& OutActivity) const
{
	// `thunk_FUN_102ee6a0` (the pending link is valid) and `thunk_FUN_102ee510` (its cached
	// activity), the pair `FUN_1027a6c0` reads. **SEAM**, and the link object's retail identity is
	// **unrecovered** — the ledger itemises nothing past the `m_pNavigator` chain redirect.
	(void)OutActivity;
	return false;
}

void FElysiumNpc::MotorCancelLinkFacing()
{
	// `thunk_FUN_102e1e20(m_pMotor, -1)` — `FUN_10382d20`'s whole body. **SEAM**: the Facing family
	// established that this mover keeps no facing queue (`m_facingQueue`, motor+0x54), so the cancel
	// is recorded and cancels nothing.
	++MotorSeams.LinkFacingCancels;
}

int32 FElysiumNpc::NavNodeWordAt(int32 RouteStepIndex) const
{
	// `FUN_1029f6c0`:
	//     if (!route || !route->+4) return 0;
	//     int node = route->+4->[0x14 + route->+4->+0x10 * 4];
	//     if (node == -1) return 0;
	//     CAI_Node** nodes = m_pNavigator->+0x2c;     // [0] count, [1] array
	//     if (node < 0 || nodes[0] <= node) { ++DAT_106c994c; return 0; }   // the out-of-range tally
	//     CAI_Node* n = nodes[1][node];
	//     return n ? n->+0xa0 : 0;
	//
	// **SEAM**: there is no node graph. Every index is out of range on an empty array, which is the
	// arm that bumps retail's own counter and returns 0 — so the refusal here IS one of retail's.
	(void)RouteStepIndex;
	return 0;
}

// -------------------------------------------------------------------------------------------------
// The motor seams.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::MotorApplyIntervalMovement(const FVector& DeltaUnits, float YawDelta)
{
	// `thunk_FUN_102e0bd0(m_pMotor, delta, yaw, …)` — the apply half of `AutoMovement`.
	// **SEAM, and a named modernization**: Unreal's animation instance extracts and applies root
	// motion itself, which is the visual-only half this port adopts freely. What the substrate owes
	// retail is the GATE and the ORDER above it, and those are ported; the apply is recorded here
	// and moves nothing.
	(void)DeltaUnits;
	(void)YawDelta;
	++MotorSeams.IntervalMovementApplied;
	return false;
}

bool FElysiumNpc::AnimIntervalMovement(float Interval, FVector& OutDeltaUnits,
	float& OutYawDelta) const
{
	// `CBaseAnimating::GetIntervalMovement(m_flAnimTime - m_flPrevAnimTime, …)`. **SEAM**: the
	// animating tier publishes no interval movement to the kernel.
	(void)Interval;
	OutDeltaUnits = FVector::ZeroVector;
	OutYawDelta = 0.f;
	return false;
}

bool FElysiumNpc::MoveProbeCheckStandPosition(const FVector& PositionUnits, int32 ProbeFlags) const
{
	// `thunk_FUN_102e7270(m_pMoveProbe, pos, …, 0, 0)` — `CAI_MoveProbe::CheckStandPosition`.
	// **SEAM**: `m_pMoveProbe` (+0x5d40) is a CHAIN row onto a mover that answers no hull probe.
	(void)PositionUnits;
	(void)ProbeFlags;
	++MotorSeams.MoveProbeChecks;
	return false;
}

bool FElysiumNpc::KernelHullTrace(const FVector& StartUnits, const FVector& EndUnits,
	const FVector& HullMins, const FVector& HullMaxs, int32 Mask, FKernelHullTrace& OutTrace) const
{
	// `thunk_FUN_1026e940` and `(*DAT_1070b254)->TraceRay`, the engine hull trace the three probe
	// bodies of this family run. **SEAM**: nothing traces a hull for the kernel here. `OutTrace`
	// keeps its defaults — fraction 1.0, no entity — which is retail's own CLEAR result, and every
	// caller below branches on the `false` return rather than on the defaults.
	(void)StartUnits;
	(void)EndUnits;
	(void)HullMins;
	(void)HullMaxs;
	(void)Mask;
	(void)OutTrace;
	++MotorSeams.HullTraces;
	return false;
}

bool FElysiumNpc::RetailHullExtents(int32 Hull, FVector& OutMinsUnits, FVector& OutMaxsUnits) const
{
	// `thunk_FUN_102d6140(m_eHull)` / `thunk_FUN_102d6160(m_eHull)` — the shared hull table.
	// **SEAM**: no hull table.
	(void)Hull;
	OutMinsUnits = FVector::ZeroVector;
	OutMaxsUnits = FVector::ZeroVector;
	return false;
}

bool FElysiumNpc::RetailCollisionExtents(const FElysiumEntity& Entity, FVector& OutMinsUnits,
	FVector& OutMaxsUnits)
{
	// `m_Collision` (+0x270) slots +4 / +8 — `OBBMins()` / `OBBMaxs()`. **SEAM**: `FElysiumEntity`
	// carries no collision extents; the bodies below refuse rather than box a point.
	(void)Entity;
	OutMinsUnits = FVector::ZeroVector;
	OutMaxsUnits = FVector::ZeroVector;
	return false;
}

int32 FElysiumNpc::RetailDerivedType(const FElysiumEntity& Entity)
{
	// `CBaseEntity::m_edtDerivedType` (+0x004c). **SEAM**: this runtime stands no derived-type word.
	// Only bit 2 (`& 4` PHYSICS_PROP) is recovered anywhere in the oracle
	// (`docs/vtmb/npc-ai/programs.md` § "The cover and kick chooser"); what the bits this family
	// tests (`0x2`, `0x10`) mean is **unrecovered**. Answering 0 leaves every one of those gates
	// falling through to the next arm, which is retail's own answer for a plain entity.
	(void)Entity;
	return 0;
}

uint32 FElysiumNpc::RetailFlags2(const FElysiumEntity& Entity)
{
	// `CBaseEntity::GetFlags2()` (+0x438 `m_fFlags2`). **SEAM**: `FElysiumEntity::Flags` is the
	// first word (+0x434) only.
	(void)Entity;
	return 0;
}

bool FElysiumNpc::RetailIsStandable(const FElysiumEntity& Entity)
{
	// `CBaseEntity::IsStandable()` slot 164 `0x100b50a0`:
	//     if (GetSolidFlags() & 0x10) return false;
	//     int mt = GetMoveType();
	//     if (mt == 1 || mt == 6 || mt == 2) return true;
	//     return thunk_FUN_100b5110(this);
	// **SEAM**: no solid flags and no move type here. It answers FALSE, which is retail's own answer
	// for the first arm, and `CanStandOn` therefore refuses every non-null candidate.
	(void)Entity;
	return false;
}

uint32 FElysiumNpc::ActiveWeaponCapabilityWord() const
{
	// The active weapon's vtable +0x5a0 (slot 360, retail body `0x1014f930`). **SEAM**: no such
	// word on `FElysiumWeapon`; answering 0 closes `ShouldMoveAndShoot`'s Troika gate.
	return 0;
}

float FElysiumNpc::MotorMinStoppingDistanceUnits() const
{
	// `CAI_Motor#16` `0x102e1300`, which lives on `IElysiumNpcMotor` itself
	// (`MinStoppingDistanceUnits`). With no motor at all the interface's own floor is the answer.
	return Motor != nullptr ? Motor->MinStoppingDistanceUnits() : 10.0f;
}

const FElysiumNpc::FRetailYawConVar* FElysiumNpc::RetailYawConVars(int32& OutCount)
{
	OutCount = UE_ARRAY_COUNT(GRetailYawConVars);
	return GRetailYawConVars;
}

float FElysiumNpc::RetailYawConVarValue(const TCHAR* Address)
{
	// Retail reads each of these as `cvar->IsCommand() ? 0.0f : cvar->m_fValue` (`+0x28`). This
	// substrate has no console, so a cvar whose registered DEFAULT is recovered answers that default
	// — which is what a fresh game answers — and one whose default is not recovered answers 0.0,
	// which is retail's own `IsCommand()` arm.
	if (Address == nullptr)
	{
		return 0.f;
	}
	for (const FRetailYawConVar& Row : GRetailYawConVars)
	{
		if (FCString::Strcmp(Row.Address, Address) == 0)
		{
			return Row.bDefaultRecovered ? Row.Default : 0.f;
		}
	}
	return 0.f;
}

// -------------------------------------------------------------------------------------------------
// The species call-outs.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpc::ChangBrosSector(const FVector& PositionUnits) const
{
	// `CNPC_VChangBros::GetSector(pos)`. **SEAM**: this substrate has no sector partition. It
	// answers 4, which is the value `CheckForJumpAttack` tests for and which CLOSES its gate — the
	// jump attack is refused rather than allowed on a guess.
	(void)PositionUnits;
	return 4;
}

bool FElysiumNpc::SolveJumpArc(const FVector& FromUnits, const FVector& ToUnits,
	FVector& OutVelocityUnits) const
{
	// `thunk_FUN_102c4cc0(this, &outVelocity, from, to)`. **SEAM**: no arc solver here.
	(void)FromUnits;
	(void)ToUnits;
	(void)OutVelocityUnits;
	++MotorSeams.JumpArcSolves;
	return false;
}

void FElysiumNpc::CommitSetupJump()
{
	// `thunk_FUN_102c4e80(this)` — the commit every `SetupJump`/`SetupSuperJump` ends on, which
	// takes the three jump words just written and starts the leap. **SEAM**: recorded, starts
	// nothing.
	++MotorSeams.SetupJumpCommits;
}

bool FElysiumNpc::PositionClearForTeleport(const FVector& PositionUnits, float RadiusUnits) const
{
	// `CNPC_VAsianVampire::PositionClearForTeleport(pos, 150.0)`. **SEAM**: answers false, so
	// `SelectJumpbaseNode` finds no node rather than choosing one blind.
	(void)PositionUnits;
	(void)RadiusUnits;
	return false;
}

void FElysiumNpc::AddHintToStoredJumpPositions(int32 HintNode)
{
	// `CNPC_VAsianVampire::AddHintToStoredJumpPositions(hint)` — the ring write that pairs with
	// `IsPosNearStoredJumpPositions`: store the hint's origin at `m_iLastJumpPositionIdx` (+0x66d0)
	// and advance modulo the ring's two entries. The hint's ORIGIN is the seam.
	FVector HintOriginUnits = FVector::ZeroVector;
	if (!NavHintNodeOrigin(HintNode, HintOriginUnits))
	{
		return;
	}
	LastJumpPosition[LastJumpPositionIdx % 2] = HintOriginUnits;
	LastJumpPositionIdx = (LastJumpPositionIdx + 1) % 2;
}

bool FElysiumNpc::DistToHintCenterLine2DSqr(int32 HintNode, const FVector& PositionUnits,
	float& OutSqr) const
{
	// `CNPC_VVampireBoss::DistToHintCenterLine2D_2(hint, pos)`. **SEAM**: no hint geometry.
	(void)HintNode;
	(void)PositionUnits;
	(void)OutSqr;
	return false;
}

bool FElysiumNpc::NavHintNodeType(int32 HintNode, int32& OutType) const
{
	// `CAI_Hint+0x5dc m_nHintType`. **SEAM**: `ScheduleHost.HintNode` is a bare index and no store
	// carries hint types — the Squad family records the same gap for the global list.
	(void)HintNode;
	(void)OutType;
	return false;
}

bool FElysiumNpc::NavHintNodeOrigin(int32 HintNode, FVector& OutOriginUnits) const
{
	// The hint's `GetAbsOrigin()` (slot 217). **SEAM**, same store.
	(void)HintNode;
	(void)OutOriginUnits;
	return false;
}

bool FElysiumNpc::NavAllHintNodes(TArray<int32>& OutHintNodes) const
{
	// The global `CAI_Hint` list `DAT_10925450`, next link `+0x5d8`. **SEAM**: an empty list.
	OutHintNodes.Reset();
	return false;
}

FElysiumEntity* FElysiumNpc::MingXiaoTentacleCompanion() const
{
	// `thunk_FUN_1039ede0(this)`. **SEAM**: the tentacle proxy chain is the Squad family's
	// `Proxies[6]` and nothing links a head to it yet.
	return nullptr;
}

FElysiumEntity* FElysiumNpc::RatIgnoredGlobalEntity() const
{
	// `thunk_FUN_101cda50()` — a no-argument global read. **SEAM**, and the entity's retail
	// identity is **unrecovered**.
	return nullptr;
}

void FElysiumNpc::CrowOverrideMove(float Interval)
{
	// `thunk_FUN_10357be0(this, interval)` — `CNPC_Crow`'s own move handler. **SEAM**: recorded,
	// moves nothing. The ANSWER slot 525 gives on this arm ("true, I handled the move") is the
	// ported half and is what the caller observes.
	(void)Interval;
	++MotorSeams.CrowOverrideMoves;
}

bool FElysiumNpc::IsIgnoreCollisionEntityTail(const FElysiumEntity* Other) const
{
	// `CBaseAnimating::IsIgnoreCollisionEntity` `0x1008be20`: resolve `m_hIgnoreCollisionEntity`
	// (+0x055c) and compare it against the candidate. Nothing writes the handle in this runtime yet,
	// so the tail answers "not that entity" for everything.
	if (Other == nullptr || !IgnoreCollisionEntity.IsSet() || World == nullptr)
	{
		return false;
	}
	return World->Resolve(IgnoreCollisionEntity) == Other;
}

// -------------------------------------------------------------------------------------------------
// The species tables' readers.
// -------------------------------------------------------------------------------------------------

const FElysiumNpc::FMaxYawSpeedSpecies* FElysiumNpc::MaxYawSpeedSpeciesRows(int32& OutCount)
{
	OutCount = UE_ARRAY_COUNT(GMaxYawSpeedSpecies);
	return GMaxYawSpeedSpecies;
}

const FElysiumNpc::FIgnoreCollisionSpecies* FElysiumNpc::IgnoreCollisionSpeciesRows(int32& OutCount)
{
	OutCount = UE_ARRAY_COUNT(GIgnoreCollisionSpecies);
	return GIgnoreCollisionSpecies;
}

const FElysiumNpc::FSetupJumpSpecies* FElysiumNpc::SetupJumpSpeciesRows(int32& OutCount)
{
	OutCount = UE_ARRAY_COUNT(GSetupJumpSpecies);
	return GSetupJumpSpecies;
}

const FElysiumNpc::FSetupJumpSpecies* FElysiumNpc::SetupJumpSpeciesOf(const TCHAR* InRetailClass)
{
	if (InRetailClass == nullptr)
	{
		return nullptr;
	}
	for (const FSetupJumpSpecies& Row : GSetupJumpSpecies)
	{
		if (FCString::Strcmp(Row.RetailClass, InRetailClass) == 0)
		{
			return &Row;
		}
	}
	return nullptr;
}

const FElysiumNpc::FJumpTunableSpecies* FElysiumNpc::JumpTunableSpeciesRows(int32& OutCount)
{
	OutCount = UE_ARRAY_COUNT(GJumpTunableSpecies);
	return GJumpTunableSpecies;
}

const FElysiumNpc::FJumpTunableSpecies* FElysiumNpc::JumpTunableSpeciesOf(const TCHAR* InRetailClass)
{
	if (InRetailClass == nullptr)
	{
		return nullptr;
	}
	for (const FJumpTunableSpecies& Row : GJumpTunableSpecies)
	{
		if (FCString::Strcmp(Row.RetailClass, InRetailClass) == 0)
		{
			return &Row;
		}
	}
	return nullptr;
}

// -------------------------------------------------------------------------------------------------
// The movement tunables — slots 521, 522, 523, 524.
// -------------------------------------------------------------------------------------------------

float FElysiumNpc::StepHeight() const
{
	// slot 522. `CAI_BaseNPC::StepHeight` `0x101a6b40` returns `_DAT_10453b94` = 18.0 and IS the
	// body slot 522 carries on the Troika line. `CAI_TestHull::StepHeight` `0x102d72b0` returns
	// `_DAT_10462950` = 40.0. Three species override it for real outside this pack's rows.
	const FJumpTunableSpecies* Row = IsRetailClass(TEXT("CAI_TestHull"))
		? JumpTunableSpeciesOf(TEXT("CAI_TestHull"))
		: JumpTunableSpeciesOf(TEXT("CAI_BaseNPCTroika"));
	return Row != nullptr ? Row->StepHeight : GStepHeightBase;
}

float FElysiumNpc::GetMaxJumpSpeed() const
{
	// slot 523. `CAI_BaseNPCTroika::GetMaxJumpSpeed` `0x101aa670` returns `_DAT_1044faa8` = 36.0 —
	// a DIFFERENT constant from the base's `0x101a6b60`, which returns the same 18.0 as its step
	// height. `CAI_TestHull::GetMaxJumpSpeed` `0x102d72d0` returns 40.0.
	const FJumpTunableSpecies* Row = IsRetailClass(TEXT("CAI_TestHull"))
		? JumpTunableSpeciesOf(TEXT("CAI_TestHull"))
		: JumpTunableSpeciesOf(TEXT("CAI_BaseNPCTroika"));
	return Row != nullptr ? Row->MaxJumpSpeed : GMaxJumpSpeedTroika;
}

float FElysiumNpc::GetJumpGravity() const
{
	// slot 524. `CAI_BaseNPC::GetJumpGravity` `0x101a6b80` returns `_DAT_10477ce8` = 350.0, and no
	// class in the family overrides it. Zero dispatch sites in the closure, but the slot is filled,
	// so the body is not dead.
	return GJumpGravity;
}

bool FElysiumNpc::IsJumpLegalGeometry(const FVector& StartUnits, const FVector& ApexUnits,
	const FVector& EndUnits, float MaxRise, float MaxDrop, float MaxDistance)
{
	// `FUN_10280790(start, apex, end, maxRise, maxDrop, maxDistance)`, arm for arm. Every threshold
	// carries the same `_DAT_104493d0 = 0.1` slack except the apex, which is scaled by
	// `_DAT_10460020 = 1.25` instead.
	const float Rise = static_cast<float>(EndUnits.Z - StartUnits.Z);
	if (MaxRise + GJumpLegalSlack < Rise)
	{
		return false;
	}
	const float Drop = static_cast<float>(StartUnits.Z - EndUnits.Z);
	if (MaxDrop + GJumpLegalSlack < Drop)
	{
		return false;
	}
	const float ApexRise = static_cast<float>(ApexUnits.Z - StartUnits.Z);
	if (MaxRise * GJumpApexScale < ApexRise)
	{
		return false;
	}
	// The distance reuses `Drop` for the Z term — squared, so the sign does not matter, and the
	// listing computes it exactly this way.
	const float Distance = FMath::Sqrt(
		static_cast<float>((StartUnits.Y - EndUnits.Y) * (StartUnits.Y - EndUnits.Y))
		+ Drop * Drop
		+ static_cast<float>((StartUnits.X - EndUnits.X) * (StartUnits.X - EndUnits.X)));
	return !(MaxDistance + GJumpLegalSlack < Distance);
}

bool FElysiumNpc::IsJumpLegal(FVector& StartUnits, FVector& ApexUnits, FVector& EndUnits) const
{
	// slot 521. `CAI_BaseNPC::IsJumpLegal` `0x10280880` forwards to the geometry helper with
	// 80.0 / 250.0 / 160.0; `CAI_TestHull::IsJumpLegal` `0x102d7760` with 1024 / 1024 / 1024.
	const FJumpTunableSpecies* Row = IsRetailClass(TEXT("CAI_TestHull"))
		? JumpTunableSpeciesOf(TEXT("CAI_TestHull"))
		: JumpTunableSpeciesOf(TEXT("CAI_BaseNPCTroika"));
	const float Rise = Row != nullptr ? Row->JumpLegalRise : 80.0f;
	const float Drop = Row != nullptr ? Row->JumpLegalDrop : 250.0f;
	const float Distance = Row != nullptr ? Row->JumpLegalDistance : 160.0f;
	return IsJumpLegalGeometry(StartUnits, ApexUnits, EndUnits, Rise, Drop, Distance);
}

// -------------------------------------------------------------------------------------------------
// Slot 516 — the yaw-speed ladder.
// -------------------------------------------------------------------------------------------------

float FElysiumNpc::MaxYawSpeedBase()
{
	// `CAI_BaseNPC::MaxYawSpeed` `0x10280bb0` — one constant, `_DAT_1049949c` = 45.0, the same
	// number every other ladder in the family falls through to.
	return GYawDefault;
}

float FElysiumNpc::MaxYawSpeedHumanoid(int32 Activity)
{
	// `CAI_BaseHumanoid::MaxYawSpeed` `0x102624b0`, a three-arm switch on `m_Activity` (+0xfec).
	switch (Activity)
	{
	case GActWalk:
	case GActRunHumanoid:
	case GActRun:
		return GYawHumanoidMove;      // _DAT_10463584 = 15.0
	case GActCrouchIdle:
	case GActCrouchWalk:
		return GYawHumanoidCrouch;    // _DAT_104492a4 = 60.0
	default:
		return GYawDefault;           // _DAT_1049949c = 45.0
	}
}

float FElysiumNpc::MaxYawSpeedGeneric(int32 Activity)
{
	// `CGeneric_NPC::MaxYawSpeed` `0x1035a810`, duplicated byte for byte at
	// `CGeneric_NPC_bathack` `0x1035b080` and `CGenericSabbat_NPC` `0x1035be80`.
	if (Activity == GActRun)
	{
		return GYawRun;               // _DAT_1047a3ac = 160.0
	}
	if (GActCrouchIdle - 1 < Activity && Activity < GActCrouchWalk + 1)
	{
		return GYawGenericCrouch;     // _DAT_1044f00c = 120.0
	}
	return GYawDefault;
}

float FElysiumNpc::MaxYawSpeedTurningArm(const TCHAR* ConVarAddress)
{
	// The arm all three of `CAI_BaseNPCTroika` (`0x10297ce0`), `CNPC_VDog` (`0x10374130`) and
	// `CNPC_VTzimisce` (`0x103ba020`) take when `m_afMemory & 0x2000` is set:
	//
	//     float v = GetIdealYawSpeed();                 // vtable +0x3c8, slot 242
	//     float s = cvar->IsCommand() ? 0.0f : cvar->m_fValue;
	//     float r = ABS(v) * s;
	//     return r <= 1.0f ? 1.0f : r;                  // _DAT_104454c0
	//
	// The cvar differs per species and all three are **unrecovered**, so `s` is 0 and the arm lands
	// on retail's own floor.
	const float Ideal = GetIdealYawSpeed();
	const float Scale = RetailYawConVarValue(ConVarAddress);
	const float Result = FMath::Abs(Ideal) * Scale;
	return Result <= GYawFloor ? GYawFloor : Result;
}

float FElysiumNpc::MaxYawSpeedMingXiao(int32 Activity, TFunctionRef<float(int32)> TuningField)
{
	// `CNPC_VMingXiao::MaxYawSpeed` `0x10394930`: the tuning record `0x101e8da0(0x10739d08)`'s
	// +0x48 inside the half-open band `(0x1129, 0x112e)` and its +0x44 everywhere else.
	if (0x1129 < Activity && Activity < 0x112e)
	{
		return TuningField(0x48);
	}
	return TuningField(0x44);
}

float FElysiumNpc::MaxYawSpeedDog()
{
	// `CNPC_VDog::MaxYawSpeed` `0x10374130`. The Troika ladder with three differences: the fall-out
	// for a recognised activity is 40.0 rather than 45.0, there is no `debug_slow_*` arm at all, and
	// the turning cvar is the Dog's own `0x1093ad24`.
	if ((ScheduleHost.MemoryBits & GMemoryTurning) != 0)
	{
		return MaxYawSpeedTurningArm(TEXT("0x1093ad24"));
	}
	if (NpcFlags.Has(EElysiumNpcFlag::PLAYING_FACE_ANIM))
	{
		return GYawDefault;
	}
	const int32 Activity = ActivityNumber;
	if (Activity < GActCrouchWalk + 1)
	{
		if (GActCrouchIdle - 1 < Activity)
		{
			return GYawCrouch;        // _DAT_104492a8 = 30.0
		}
		if (Activity == GActIdle || Activity == GActIdleAngry)
		{
			// `m_bAllowTurningAnims` ORed with the cvar the Facing family recorded (`0x109247ec`):
			// when neither is on, the unrecovered `0x10923e84` decides; when either is, 30.0.
			if (!TurningAnimsEnabled())
			{
				return RetailYawConVarValue(TEXT("0x10923e84"));
			}
			return GYawCrouch;
		}
		if (Activity != GActRun)
		{
			return GYawDefault;
		}
	}
	else if (Activity != 0x1093 && (Activity < 0x1094 || 0x1096 < Activity))
	{
		return GYawDefault;
	}
	return GTestHullTunable;          // _DAT_10462950 = 40.0
}

float FElysiumNpc::MaxYawSpeedTzimisce()
{
	// `CNPC_VTzimisce::MaxYawSpeed` `0x103ba020` — a four-arm switch and the shared turning arm,
	// with the Tzimisce's own cvar `0x1093c9fc`.
	if ((ScheduleHost.MemoryBits & GMemoryTurning) != 0)
	{
		return MaxYawSpeedTurningArm(TEXT("0x1093c9fc"));
	}
	switch (ActivityNumber)
	{
	case GActIdle:
	case 0xfc:
	case 0xfd:
		return GYawTzimisceIdle;      // _DAT_10454110 = 5.0
	case GActRun:
		return GYawCrouch;            // _DAT_104492a8 = 30.0
	default:
		return GYawTzimisceDefault;   // _DAT_104cc504 = 11.0
	}
}

float FElysiumNpc::MaxYawSpeed()
{
	// slot 516. The species that replace the Troika ladder come first, in the order of
	// `GMaxYawSpeedSpecies`; everything else takes `CAI_BaseNPCTroika::MaxYawSpeed` `0x10297ce0`.
	//
	// `CNPC_VWerewolf::MaxYawSpeed` `0x103d0a30` is deliberately NOT an arm here: its whole body is
	// a scope-trace push/pop around an unconditional forward to the Troika body, so the werewolf's
	// retail answer IS the family default.
	if (IsRetailClass(TEXT("CNPC_VDog")))
	{
		return MaxYawSpeedDog();
	}
	if (IsRetailClass(TEXT("CNPC_VTzimisce")))
	{
		return MaxYawSpeedTzimisce();
	}
	if (IsRetailClass(TEXT("CNPC_VMingXiao")))
	{
		return MaxYawSpeedMingXiao(ActivityNumber, MingXiaoTuningField);
	}
	if (IsRetailClass(TEXT("CGeneric_NPC")) || IsRetailClass(TEXT("CGeneric_NPC_bathack"))
		|| IsRetailClass(TEXT("CGenericSabbat_NPC")))
	{
		return MaxYawSpeedGeneric(ActivityNumber);
	}

	// `CAI_BaseNPCTroika::MaxYawSpeed` `0x10297ce0`, arm for arm.
	if ((ScheduleHost.MemoryBits & GMemoryTurning) != 0)
	{
		return MaxYawSpeedTurningArm(TEXT("0x10924c94"));
	}
	if (NpcFlags.Has(EElysiumNpcFlag::PLAYING_FACE_ANIM))
	{
		return GYawDefault;
	}
	const int32 Activity = ActivityNumber;
	if (Activity < GActCrouchWalk + 1)
	{
		if (GActCrouchIdle - 1 < Activity)
		{
			return GYawCrouch;
		}
		switch (Activity)
		{
		case GActIdle:
		case GActIdleAngry:
			// `(m_NpcStateFlags >> 7) & 1` — the per-state capability byte (+0x5b64), bit 7, which
			// is set only for the combat (0x8f) and flee (0x85) states. Out of combat the
			// `debug_slow_idle_yaw_speed` cvar decides; in combat the turning-anims gate does.
			if ((NpcStateFlags() & 0x80) == 0)
			{
				return RetailYawConVarValue(TEXT("0x10924e94"));
			}
			if (!TurningAnimsEnabled())
			{
				return RetailYawConVarValue(TEXT("0x10923e84"));
			}
			return GYawCrouch;
		case GActWalk:
			if ((NpcStateFlags() & 0x80) == 0)
			{
				return RetailYawConVarValue(TEXT("0x1092411c"));
			}
			break;   // in combat, ACT_WALK falls out of the switch to the default
		case GActRun:
			return GYawRun;
		default:
			break;
		}
	}
	else
	{
		switch (Activity)
		{
		case 0x1093:
		case 0x1094:
		case 0x1095:
		case 0x1096:
			return GYawRun;
		case 0x1121:
			return GYawCrouch;
		default:
			break;
		}
	}
	return GYawDefault;
}

// -------------------------------------------------------------------------------------------------
// Slots 68 and 69 — the two collision-ignore chains.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::IgnoreCollisionSharedHead(const FElysiumEntity* Other) const
{
	// The head `CAI_BaseNPCTroika::ShouldIgnoreCollision` (`0x1029afc0`) and
	// `NavIgnoreCollision` (`0x1029b180`) share byte for byte:
	//
	//   1. Unless `m_bForceNPCCheck` (+0x63da) is set:
	//        - with `NAV_IGNORE_NPC` (word one, 0x40): the candidate being a `CAI_BaseNPC`
	//          (other+0x94 `m_pBaseNPC`) or a `CBasePlayer` (other+0xa8 `m_pPlayer`) ignores it;
	//        - without it: a candidate that is a `CAI_BaseNPCTroika` (other+0x98) AND is `SLEEPING`
	//          (its own `m_bfAINPCFlags & 0x20000`) ignores it.
	//   2. The candidate being `m_hKickPhysicsProp` (+0x643c) ignores it.
	//   3. The candidate being a `CBaseCombatWeapon` (other+0xa0 `m_pCombatWeapon`) ignores it.
	//
	// The five `other+0x9x` words are `CAI_BaseNPCTroika`'s self-cast cache (`layout.md`
	// +0x0094..+0x00a8), so each test is "is the candidate of that class", which this runtime can
	// answer for three of the five directly.
	if (Other == nullptr)
	{
		return false;
	}
	if (!bForceNpcCheck)
	{
		if (NpcFlags.Has(EElysiumNpcFlag::NAV_IGNORE_NPC))
		{
			if (Other->AsNpc() != nullptr)
			{
				return true;
			}
			if (World != nullptr && World->FindPlayer() == Other)
			{
				return true;
			}
		}
		else
		{
			const FElysiumNpc* OtherNpc = Other->AsNpc();
			if (OtherNpc != nullptr && OtherNpc->NpcFlags.Has(EElysiumNpcFlag::SLEEPING))
			{
				return true;
			}
		}
	}
	if (ScheduleHost.KickProp.IsSet() && World != nullptr
		&& World->Resolve(ScheduleHost.KickProp) == Other)
	{
		return true;
	}
	// `other+0xa0 m_pCombatWeapon` — "the candidate IS a combat weapon". **SEAM**: this runtime has
	// no self-cast cache, and `FElysiumEntity` exposes no weapon leaf test at this tier, so the arm
	// is reached and answers false. Stated rather than skipped.
	return false;
}

bool FElysiumNpc::ShouldIgnoreCollision(FElysiumEntity* Other)
{
	// slot 68. Species arms first, in the order of `GIgnoreCollisionSpecies`, then
	// `CAI_BaseNPCTroika::ShouldIgnoreCollision` `0x1029afc0`.

	// `CNPC_VMingXiaoTentacle::vfunc68` `0x1039eb50`: ignore unconditionally unless the candidate is
	// non-null AND (it is not a `CBaseCombatCharacter` (other+0x9c) OR `m_bIgnoreCollision` is
	// clear), in which case fall to the base.
	if (IsRetailClass(TEXT("CNPC_VMingXiaoTentacle")))
	{
		const bool bFallToBase = Other != nullptr
			&& (Other->AsCombatCharacter() == nullptr || !bIgnoreCollisionSpecies);
		if (!bFallToBase)
		{
			return true;
		}
	}
	// `CNPC_VRat::vfunc68` `0x103ad6d0`: a fixed global entity, else the base.
	else if (IsRetailClass(TEXT("CNPC_VRat")))
	{
		if (Other != nullptr && Other == RatIgnoredGlobalEntity())
		{
			return true;
		}
	}
	// `CNPC_VWerewolf::ShouldIgnoreCollision` `0x103d9ab0`: `m_edtDerivedType & 0x16`, then
	// `GetFlags2() & 8`, else the base.
	else if (IsRetailClass(TEXT("CNPC_VWerewolf")) && Other != nullptr)
	{
		if ((RetailDerivedType(*Other) & 0x16) != 0)
		{
			return true;
		}
		if ((RetailFlags2(*Other) & 8) != 0)
		{
			return true;
		}
	}

	if (IgnoreCollisionSharedHead(Other))
	{
		return true;
	}
	if (Other == nullptr)
	{
		return IsIgnoreCollisionEntityTail(Other);
	}
	// The hint-node arm: a claimed hint whose type is strictly inside `(0x283b, 0x283e)` combined
	// with `m_edtDerivedType & 0x14` on the candidate.
	int32 HintType = 0;
	if (ScheduleHost.HintNode != INDEX_NONE && NavHintNodeType(ScheduleHost.HintNode, HintType)
		&& 0x283b < HintType && HintType < 0x283e && (RetailDerivedType(*Other) & 0x14) != 0)
	{
		return true;
	}
	// `(other->m_edtDerivedType >> 1) & 1`.
	if (((RetailDerivedType(*Other) >> 1) & 1) != 0)
	{
		return true;
	}
	// `m_GrapplePartner` (+0x1538) resolved, with `m_GrappleRole` (+0x153c) not -1: a grapple
	// partner never collides with its own hold.
	if (Grapple.Role != EElysiumGrappleRole::None)
	{
		const FElysiumCombatCharacter* Partner = ResolveGrapplePartner();
		if (Partner != nullptr && static_cast<const FElysiumEntity*>(Partner) == Other)
		{
			return true;
		}
	}
	return IsIgnoreCollisionEntityTail(Other);
}

bool FElysiumNpc::GargoyleIgnoresClassname(const FString& Classname)
{
	// `CNPC_VGargoyle::NavIgnoreCollision` `0x10379490`'s three `FClassnameIs` compares, which are
	// case-insensitive (`__strcmpi`) and accept a trailing `*` as a prefix match — none of these
	// three carries one, so all three are whole-name compares.
	return Classname.Equals(TEXT("prop_dynamic"), ESearchCase::IgnoreCase)
		|| Classname.Equals(TEXT("func_brush"), ESearchCase::IgnoreCase)
		|| Classname.Equals(TEXT("func_door_rotating"), ESearchCase::IgnoreCase);
}

bool FElysiumNpc::NavIgnoreCollision(FElysiumEntity* Other)
{
	// slot 69. Species arms first, then `CAI_BaseNPCTroika::NavIgnoreCollision` `0x1029b180`.

	// `CNPC_VGargoyle` `0x10379490`: the `0x16` derived-type gate, then the three classnames.
	if (IsRetailClass(TEXT("CNPC_VGargoyle")) && Other != nullptr)
	{
		if ((RetailDerivedType(*Other) & 0x16) != 0)
		{
			return true;
		}
		if (GargoyleIgnoresClassname(Other->Def != nullptr ? Other->Def->Classname : FString()))
		{
			return true;
		}
	}
	// `CNPC_VHengeyokai` `0x10380f90`, `CNPC_VMingXiao` `0x10396fd0` and `CNPC_VTzimisce`
	// `0x103bfa00` are byte-identical: the `0x16` gate alone.
	else if (Other != nullptr
		&& (IsRetailClass(TEXT("CNPC_VHengeyokai")) || IsRetailClass(TEXT("CNPC_VMingXiao"))
			|| IsRetailClass(TEXT("CNPC_VTzimisce"))))
	{
		if ((RetailDerivedType(*Other) & 0x16) != 0)
		{
			return true;
		}
	}
	// `CNPC_VWerewolf` `0x103d9ba0`: the `0x16` gate then `GetFlags2() & 8`.
	else if (IsRetailClass(TEXT("CNPC_VWerewolf")) && Other != nullptr)
	{
		if ((RetailDerivedType(*Other) & 0x16) != 0)
		{
			return true;
		}
		if ((RetailFlags2(*Other) & 8) != 0)
		{
			return true;
		}
	}
	// `CNPC_VMingXiaoTentacle` `0x1039eb90`: the same shape as its slot 68 but falling to the NAV
	// base.
	else if (IsRetailClass(TEXT("CNPC_VMingXiaoTentacle")))
	{
		const bool bFallToBase = Other != nullptr
			&& (Other->AsCombatCharacter() == nullptr || !bIgnoreCollisionSpecies);
		if (!bFallToBase)
		{
			return true;
		}
	}

	if (IgnoreCollisionSharedHead(Other))
	{
		return true;
	}
	if (Other == nullptr)
	{
		return IsIgnoreCollisionEntityTail(Other);
	}
	// Where `ShouldIgnoreCollision` runs the hint-node range test, this chain branches on the NPC's
	// own `m_bNavIgnorePhysicsProps` (+0x65f7): `0x16` when it is set, `0x12` when it is not.
	const int32 DerivedMask = bNavIgnorePhysicsProps ? 0x16 : 0x12;
	if ((RetailDerivedType(*Other) & DerivedMask) != 0)
	{
		return true;
	}
	return IsIgnoreCollisionEntityTail(Other);
}

// -------------------------------------------------------------------------------------------------
// The remaining slots.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::IsMoving()
{
	// slot 153. `CAI_BaseNPC::FUN_10280300` `0x10280300` is a one-line forward to
	// `thunk_FUN_102ee680(m_pNavigator)`.
	return NavIsGoalActive();
}

bool FElysiumNpc::CanStandOn(FElysiumEntity* Other)
{
	// slot 166. `CNPC_VMingXiaoTentacle::vfunc166` `0x1039ebd0` adds one arm in front of the base:
	// the tentacle's own companion is never standable. Then `CAISound::FUN_10026f80` `0x10026f80`,
	// which is the body slot 166 carries for the whole family:
	//     if (other && !other->IsStandable()) return false;
	//     return true;
	if (IsRetailClass(TEXT("CNPC_VMingXiaoTentacle")) && Other == MingXiaoTentacleCompanion())
	{
		return false;
	}
	if (Other != nullptr && !RetailIsStandable(*Other))
	{
		return false;
	}
	return true;
}

void FElysiumNpc::GetGroundVelocityToApply(FVector& OutVelocity)
{
	// slot 210. `CAISound::FUN_10027370` `0x10027370` copies the three shared statics
	// `DAT_1070d1b0/b4/b8` into the out-parameter. All three sit in `.data`'s zero-initialised tail
	// (the section's raw data ends at `0x106b9000`) and no corpus function writes them, so the
	// contribution is exactly `vec3_origin`.
	OutVelocity = FVector::ZeroVector;
}

bool FElysiumNpc::OverrideMove(float Interval)
{
	// slot 525. `CNPC_Crow::vfunc525` `0x10357ba0` is the only species arm: at nav type 2 (Fly) it
	// hands the move to its own handler and answers true, otherwise it declines like the base.
	if (IsRetailClass(TEXT("CNPC_Crow")) && NavGetType() == 2)
	{
		CrowOverrideMove(Interval);
		return true;
	}
	// `CAI_BaseNPC::OverrideMove` `0x1027da90` — a scope-trace push/pop around an unconditional
	// false. The base DECLINES, and that is what every species override is measured against.
	return false;
}

bool FElysiumNpc::ShouldMoveAndShoot()
{
	// slot 575. `CAI_BaseNPCTroika::FUN_102bf4a0` `0x102bf4a0`:
	//     if (GetEnemy() && (m_bfAINPCFlags2 & 0x400) == 0x400) {
	//         if (GetActiveWeapon()) {
	//             if (GetActiveWeapon()->vtable[0x5a0]() & 0x6000)
	//                 return CAI_BaseNPC::ShouldMoveAndShoot();      // 0x10278c60
	//         }
	//         return false;      // note: the weapon arms fall out with ZERO, not with the enemy test
	//     }
	//     return false;
	const bool bHasEnemy = Senses.Memory.Enemy.IsSet() && World != nullptr
		&& World->Resolve(Senses.Memory.Enemy) != nullptr;
	if (bHasEnemy
		&& NpcFlags.Has(EElysiumNpcFlag2::MOVE_FACE_ENEMY))
	{
		const FElysiumEntity* Weapon = Inventory.ActiveWeapon.IsSet() && World != nullptr
			? World->Resolve(Inventory.ActiveWeapon)
			: nullptr;
		if (Weapon != nullptr && (ActiveWeaponCapabilityWord() & GWeaponMoveShootMask) != 0)
		{
			// `CAI_BaseNPC::FUN_10278c60` `0x10278c60`: `CapabilitiesGet() >> 6 & 1`, and
			// `CapabilitiesGet` (`0x1026db30`) is `m_afCapability` (+0x5cec) verbatim.
			return ((CapabilityWord >> GCapMoveShoot) & 1) != 0;
		}
	}
	return false;
}

bool FElysiumNpc::ValidateNavGoal()
{
	// slot 528. `CAI_BaseNPC::FUN_10280360` `0x10280360` — retail's `IsCoverPosition` check on the
	// goal the navigator is holding:
	//
	//     if (GetNavigator()->GetGoalType() != 6) return true;
	//     if (!GetEnemy()) return true;
	//     Vector goal = GetNavigator()->GetGoalPos();  goal.z = FUN_102f9c70(goal);
	//     Vector eye  = goal + GetViewOffset();        // vtable +0x854
	//     Ray_t  ray  = { eye -> GetEnemy()->EyePosition() (vtable +0x304) };
	//     TraceRay(ray, 0x2804091, filter(this, 0), &tr);
	//     if (tr.fraction == 1.0f) {                   // NOTHING blocks -> this is not cover
	//         if (!ConditionInterruptsCurrentSchedule(0x39)) {
	//             m_failText/-Line = "…AI_BaseNPC…", 0xbc;
	//             TaskFail(0x1b);                      // slot 448, already ported
	//             return false;
	//         }
	//         SetCondition(0x39);
	//     }
	//     return true;
	//
	// **SEAM**: `NavGoalState()` answers -1, so the gate never opens and the body answers true —
	// which is retail's own answer for every goal type but 6. The rest is written out so the arm is
	// here the day the mover carries a goal type.
	if (NavGoalState() != 6)
	{
		return true;
	}
	FElysiumEntity* Enemy = World != nullptr && Senses.Memory.Enemy.IsSet()
		? World->Resolve(Senses.Memory.Enemy)
		: nullptr;
	if (Enemy == nullptr)
	{
		return true;
	}
	FVector GoalUnits = FVector::ZeroVector;
	if (!NavGoalPosition(GoalUnits))
	{
		return true;
	}
	FKernelHullTrace Trace;
	if (!KernelHullTrace(GoalUnits, SourceOf(Enemy->Origin), FVector::ZeroVector,
		FVector::ZeroVector, GCoverTraceMask, Trace))
	{
		return true;
	}
	if (Trace.Fraction == GTraceClearFraction)
	{
		if (!ElysiumSchedule::MaskHasCondition(Schedule, *this, GCondNavGoalInvalid))
		{
			TaskFail(GFailNoCover);
			return false;
		}
		Cognition.Conditions.Set(GCondNavGoalInvalid);
	}
	return true;
}

void FElysiumNpc::MoveDone()
{
	// slot 133. `CAI_BaseNPC::FUN_101c1720` `0x101c1720` (read from the listing — the decompiler
	// could not recover the tail's jump table):
	//     MoverData_CurrPos = MoverData_TargetPos;          // +0x498 <- +0x494
	//     if (m_movementType == 1) LinearMoveDone();        // +0x558
	//     else if (m_movementType == 2) AngularMoveDone();
	//     m_movementType = 0;
	//     if (m_pfnMoveDone) (this->*m_pfnMoveDone)();      // +0x114
	//
	// The base slot-133 body `0x10026c50` is the last line alone, a tail JMP through `+0x114`.
	//
	// **SEAM.** None of the four words is an NPC's: +0x494/+0x498 and +0x558 are `CBaseEntity`'s
	// func_-mover state, which this runtime carries on `FElysiumMover` and which an NPC is never a
	// party to, and +0x114 is a member-function-pointer think vocabulary this runtime does not have.
	// What is recorded is that the dispatch was reached, which is the whole of what slot 133 does
	// for an NPC.
	++MotorSeams.MoveDone;
}

// -------------------------------------------------------------------------------------------------
// The non-slot bodies.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::AutoMovement()
{
	// `CAI_BaseNPC::AutoMovement` `0x10280a50`:
	//     vtable[1000/4 = 250]();                                   // first, unconditionally
	//     GetIntervalMovement(m_flAnimTime - m_flPrevAnimTime, …);
	//     if (GetMoveType() != 4) return false;                     // vtable +0x178, slot 94
	//     if (GetFlags() & 0x400) return false;                     // FL_FROZEN
	//     return thunk_FUN_102e0bd0(m_pMotor, delta, thunk_FUN_102729d0(this), yaw, …) == 1;
	//
	// **The gate is the retail contract.** `ElysiumNpc.cpp` already records that this runtime cedes
	// root-motion EXTRACTION to Unreal's animation instance (a visual-only modernization); what it
	// may not cede is WHEN the extraction is allowed to move the body, and that is `GetMoveType()`
	// being 4 with `0x400` clear, after slot 250 has run. Both are ported.
	// Slot 250 is `StudioFrameAdvance(float)` (`0x10098bb0`); retail passes no argument, so the
	// frame advance runs on the animating tier's own clock.
	StudioFrameAdvance(0.f);
	FVector DeltaUnits = FVector::ZeroVector;
	float YawDelta = 0.f;
	// `m_flAnimTime - m_flPrevAnimTime` (+0x174 - +0x170): the animating tier's own interval, which
	// is a CHAIN concern here, so the seam is asked for the interval as well as the delta.
	AnimIntervalMovement(0.f, DeltaUnits, YawDelta);
	if (GetMoveType() != 4)
	{
		return false;
	}
	if ((Flags & 0x400) != 0)
	{
		return false;
	}
	return MotorApplyIntervalMovement(DeltaUnits, YawDelta);
}

void FElysiumNpc::PerformMovement(float Interval, int32 MoveFlags)
{
	// `CAI_BaseNPC::PerformMovement` `0x1026c120` — VProf push/pop and an `rdtsc` pair around ONE
	// statement: `m_pNavigator->vtable[0x14/4 = 5](param_1, param_2)`, both parameters forwarded
	// untouched. The profiler bookkeeping is retail's own mechanism; the move step is the rule.
	//
	// **SEAM**: `IElysiumNpcMotor` has no per-interval move step to delegate to — it integrates on
	// the actor tick, which `FElysiumScriptedCharacter::SyncMovingRecord` states as this runtime's
	// named divergence from retail's "the entity origin IS the body". The delegate is recorded.
	MotorSeams.PerformMovementInterval = Interval;
	(void)MoveFlags;
	++MotorSeams.PerformMovement;
}

void FElysiumNpc::PostRun()
{
	// `CAI_BaseNPC::PostRun` `0x1026c7c0`. Everything but two lines is VProf scaffolding; the
	// retail content is the PAIRING and its ORDER:
	//     float dt = thunk_FUN_1026c540(this);        // the elapsed animation interval
	//     vtable[0x408/4 = 258](dt, this);            // DispatchAnimEvents, `0x10098c80`
	//     CBaseCombatCharacter::Weapon_FrameUpdate(dt);   // with the SAME number
	// The port's comment at `ElysiumNpc.cpp:895` discussed this ordering; this is the body.
	//
	// **SEAM**: `thunk_FUN_1026c540`'s interval is the animating tier's, which this substrate does
	// not publish to the kernel, so the pair runs with 0.0 and the ORDER is what is ported.
	const float Interval = 0.f;
	DispatchAnimEvents(Interval, this);
	MotorSeams.PostRunInterval = Interval;
	++MotorSeams.PostRunWeaponUpdates;
}

void FElysiumNpc::CheckOnGround()
{
	// `CAI_BaseNPC::CheckOnGround` `0x1026e5e0`, arm for arm.
	//
	//     if (HasCondition(0x73)) {
	//         if (!(GetFlags() & 1) && GetNavType() == 0) return;   // still airborne on the ground
	//         ClearCondition(0x73);                                  // 0x10269b50
	//         return;
	//     }
	//     if (GetNavType() != 0) return;                             // FUN_1027d990
	//     if (GetMoveType() == 7) return;
	//     if (curtime - m_flCheckOnGroundTime <= -0.001) return;     // _DAT_10497530
	//     m_flCheckOnGroundTime = curtime + 0.5;                     // _DAT_104454d0
	//     start = GetAbsOrigin() + (0,0,0.1);  end = GetAbsOrigin() - (0,0,4.0);
	//     TraceHull(start, end, OBBMins, OBBMaxs, 0x202400b, filter, &tr);
	//     if (tr.fraction == 1.0) { SetCondition(0x73); SetGroundEntity(NULL); return; }
	//     if (tr.m_pEnt && tr.m_pEnt != GetGroundEntity()) SetGroundEntity(tr.m_pEnt);
	//
	// `m_flCheckOnGroundTime` is the ONE bound word of this body (`FElysiumNpc::CheckOnGroundTime`);
	// the trace is a seam, so the two ground writes are unreachable today and the deadline is still
	// stamped, exactly as retail stamps it before tracing.
	if (Cognition.Conditions.Has(GCondOnGround))
	{
		if ((Flags & 1) == 0 && NavGetType() == 0)
		{
			return;
		}
		Cognition.Conditions.Clear(GCondOnGround);
		return;
	}
	if (NavGetType() != 0)
	{
		return;
	}
	if (GetMoveType() == 7)
	{
		return;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (Now - CheckOnGroundTime <= GCheckOnGroundSlack)
	{
		return;
	}
	CheckOnGroundTime = Now + GCheckOnGroundInterval;

	const FVector OriginUnits = SourceOf(Origin);
	const FVector StartUnits(OriginUnits.X, OriginUnits.Y, OriginUnits.Z + GCheckOnGroundUp);
	const FVector EndUnits(OriginUnits.X, OriginUnits.Y, OriginUnits.Z - GCheckOnGroundDown);
	FVector Mins = FVector::ZeroVector;
	FVector Maxs = FVector::ZeroVector;
	RetailCollisionExtents(*this, Mins, Maxs);
	FKernelHullTrace Trace;
	if (!KernelHullTrace(StartUnits, EndUnits, Mins, Maxs, GGroundTraceMask, Trace))
	{
		return;
	}
	if (Trace.Fraction == GTraceClearFraction)
	{
		Cognition.Conditions.Set(GCondOnGround);
		SetGroundEntity(nullptr);
		return;
	}
	if (Trace.HitEntity.IsSet() && World != nullptr)
	{
		FElysiumEntity* Hit = World->Resolve(Trace.HitEntity);
		if (Hit != nullptr && Hit != GetGroundEntity())
		{
			SetGroundEntity(Hit);
		}
	}
}

bool FElysiumNpc::CanStandAt(const FVector& PositionUnits, int32 InFlags)
{
	// `CAI_BaseNPCTroika::CanStandAt` `0x102a0ed0`:
	//     m_bForceNPCCheck = 1;
	//     result = m_pMoveProbe->CheckStandPosition(pos, …, 0, 0);
	//     m_bForceNPCCheck = 0;
	// The bracket is the whole of the recovered behaviour: for the duration of the probe the two
	// collision-ignore chains skip their NPC/player/sleeping arm, so the probe sees other NPCs as
	// solid. `m_bForceNPCCheck` (+0x63da) is bound but was read by nothing in the port until now.
	bForceNpcCheck = true;
	const bool bResult = MoveProbeCheckStandPosition(PositionUnits, InFlags);
	bForceNpcCheck = false;
	return bResult;
}

bool FElysiumNpc::OnObstructingDoorBase(float& InOutMoveGoalMaxDistance, int32 DoorState,
	float DistClear, EObstructingDoorResult& OutResult) const
{
	// `CAI_BaseNPC::FUN_1027dc80` `0x1027dc80`, the BASE branch of slot 531
	// `OnObstructingDoor(AILocalMoveGoal_t*, CBaseDoor*, float distClear, AIMoveResult_t*)`:
	//     if (moveGoal->+0x28 < distClear) return false;      // the door is further than the goal
	//     int state = door->+0x4f8;
	//     if (state != 1 && state != 3) return false;         // only opening / closing obstruct
	//     if (distClear < 0.1) { *result = -1; return true; } // _DAT_104493d0
	//     moveGoal->+0x28 = distClear;
	//     *result = 0;
	//     return true;
	if (InOutMoveGoalMaxDistance < DistClear)
	{
		return false;
	}
	if (DoorState != 1 && DoorState != 3)
	{
		return false;
	}
	if (DistClear < GJumpLegalSlack)
	{
		OutResult = EObstructingDoorResult::Illegal;
		return true;
	}
	InOutMoveGoalMaxDistance = DistClear;
	OutResult = EObstructingDoorResult::Ok;
	return true;
}

bool FElysiumNpc::BlockedIsNoOp() const
{
	// `CCineNPC::Blocked` `0x101a7580` — the shared slot 178 of `CCineAI`, `CCineAISchedule` and
	// `CCineNPC` is `return;` with the argument ignored. Retail's answer is that nothing happens
	// when a cine actor is blocked; this is which classes give it.
	return IsRetailClass(TEXT("CCineNPC")) || IsRetailClass(TEXT("CCineAI"))
		|| IsRetailClass(TEXT("CCineAISchedule"));
}

void FElysiumNpc::SetBlockedByFriend(bool bBlocked)
{
	// `FUN_1039aaf0` `0x1039aaf0`: `this->+0x6750 = param_1`.
	bBlockedByFriend = bBlocked;
}

bool FElysiumNpc::BlockedByFriend() const
{
	// `FUN_1039ab10` `0x1039ab10`: `return this->+0x6750;`.
	return bBlockedByFriend;
}

int32 FElysiumNpc::ResolveLinkActivity() const
{
	// `FUN_1027a6c0` `0x1027a6c0`:
	//     if (thunk_FUN_102ee6a0(m_pNavigator)) {
	//         int a = thunk_FUN_102ee510(m_pNavigator);
	//         if (a != -1) return a;
	//     }
	//     return 1;                                  // ACT_IDLE
	int32 Activity = INDEX_NONE;
	if (NavLinkActivity(Activity) && Activity != INDEX_NONE)
	{
		return Activity;
	}
	return GActIdle;
}

void FElysiumNpc::ClearLinkActivity()
{
	// `FUN_10382d20` `0x10382d20`: `thunk_FUN_102e1e20(m_pMotor, -1)`.
	MotorCancelLinkFacing();
}

void FElysiumNpc::NavOnNavFailed(int32 FailReason)
{
	// `CAI_Navigator::OnNavFailed` `0x102eeae0` (`CAI_Navigator#10`, and `CAI_Navigator#9`
	// `0x102eeb50` is a tail-jump into it):
	//     thunk_FUN_102eeb70(this);                             // the navigator's own reset
	//     owner->+0x1b44 = "E:\Vampire\main\dlls\ai_navigato…";  owner->+0x1b48 = 0x406;
	//     owner->vtable[0x700/4 = 448](reason);                 // TaskFail
	//     SetIdealActivity(owner, FUN_1027a6c0(owner));         // 0x10272650
	//     this->+0x1c = 1;
	//
	// The file/line pair is `ELYSIUM_NPC_WORD_ABSENT(0x1b44)` — the named failure reason and the
	// schedule trace rows carry that account here. `SetIdealActivity` is the Facing family's
	// `SetIdealActivityNumber`, reused rather than duplicated.
	TaskFail(FailReason);
	SetIdealActivityNumber(ResolveLinkActivity());
	Navigator.bNavFailed = true;
}

void FElysiumNpc::ResumeScheduledMove()
{
	// `FUN_102bf7e0` `0x102bf7e0`:
	//     if (thunk_FUN_102ee2e0(m_pNavigator)) thunk_FUN_102ee2c0(m_pNavigator);
	//     m_bShouldMove (+0x1a40) = 1;                // UNCONDITIONALLY, outside the if
	// The target name is 29c's best guess; the body is exact.
	if (NavIsGoalActive())
	{
		NavStopMoving();
	}
	ScheduleHost.bShouldMove = true;
}

bool FElysiumNpc::TranslateNavGoalPositionTzimisce(const FVector& GoalUnits,
	FVector& OutGoalUnits) const
{
	// `CNPC_VTzimisce::vfunc410` `0x103bf580`, the species branch of slot 410:
	//     if (m_ePathMode == 1) { if (GetEnemy()) return GetEnemy()->vtable[0x370/4 = 220](); }
	//     else if (m_ePathMode != 2) return goal;
	//     if (m_hPickupTarget resolves) return m_vecPickupTargetPos;
	//     return goal;
	// Note the fall-through: path mode 1 with no enemy does NOT return the goal, it drops into the
	// pickup arm. That is retail's own control flow and it is kept.
	if (PathMode == 1)
	{
		FElysiumEntity* Enemy = World != nullptr && Senses.Memory.Enemy.IsSet()
			? World->Resolve(Senses.Memory.Enemy)
			: nullptr;
		if (Enemy != nullptr)
		{
			OutGoalUnits = SourceOf(Enemy->Origin);
			return true;
		}
	}
	else if (PathMode != 2)
	{
		OutGoalUnits = GoalUnits;
		return false;
	}
	const bool bPickupLive = PickupTarget.IsSet() && World != nullptr
		&& World->Resolve(PickupTarget) != nullptr;
	if (bPickupLive)
	{
		OutGoalUnits = PickupTargetPos;
		return true;
	}
	OutGoalUnits = GoalUnits;
	return false;
}
