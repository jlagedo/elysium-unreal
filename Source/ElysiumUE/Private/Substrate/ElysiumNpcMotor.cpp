#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcMotorShared.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMotor2Shared.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumSchedule.h"

// Story 29c-1, family **Motor** — `CAI_Motor`, `CAI_Navigator`, and everything the NPC asks of its
// motor: the step and jump tunables, the jump setup/legality chain, the two collision-ignore
// chains, the yaw-speed ladder, the ground and stuck probes, and the move-done / nav-failure hooks.
//
// 73 rows of `order.md` layers 0–9, ported arm by arm in retail's order. The step, jump, yaw-ladder
// and ground-probe thresholds are bound to the tunables table (`ElysiumNpcKernelTunables.h`, every
// row re-read from the pinned `vampire.dll` by `gen_kernel_tunables --check`); every other threshold
// below was read out of the image's `.rdata` at its cited address (image base `0x10000000`), so the
// numbers are recovered facts, not estimates. The walked prose is `docs/vtmb/npc-ai/shape.md`.
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

	constexpr float GYawRun = ElysiumNpcTunables::YawSpeedRun;
	constexpr float GYawFloor = ElysiumNpcTunables::One;

	constexpr int32 GActWalk = 9;

	// The two `m_bfAINPCFlags` (+0x14b8) bits this file tests are named in `ElysiumNpcFlags.h`:
	// `PLAYING_FACE_ANIM` (0x8000000), which suppresses the yaw ladder, and `SLEEPING` (0x20000),
	// which both ignore chains test on the OTHER entity.
	// `bits_CAP_MOVE_SHOOT`, bit 6 of the capability word, which the base `ShouldMoveAndShoot`
	// returns (`0x10278c60`: `CapabilitiesGet() >> 6 & 1`).
	constexpr int32 GCapMoveShoot = 6;
	// The active weapon's capability mask `CAI_BaseNPCTroika::ShouldMoveAndShoot` requires.
	constexpr uint32 GWeaponMoveShootMask = 0x6000;

	// --- The movement-tunables table ------------------------------------------------------------

	// The `ConVar*` globals this family's ladders read, as `IsCommand() ? 0.0f : m_fValue`. The two
	// `MaxYawSpeed` constructs in its own body carry a recovered name and default (read from
	// `.rdata` at the ctor's argument addresses); the other four are tunables-table rows.
	constexpr FElysiumNpc::FRetailYawConVar GRetailYawConVars[] =
	{
		{ TEXT("debug_slow_idle_yaw_speed"), TEXT("0x10924e94"), 20.0f, ElysiumNpcTunables::EConVar::Count },
		{ TEXT("debug_slow_walk_yaw_speed"), TEXT("0x1092411c"), 25.0f, ElysiumNpcTunables::EConVar::Count },
		// The turning-arm scalar. `CAI_BaseNPCTroika` reads `0x10924c94`, `CNPC_VDog` reads
		// `0x1093ad24` and `CNPC_VTzimisce` reads `0x1093c9fc` — three distinct cvars, ".15" each.
		{ TEXT("debug_turn_scalar"), TEXT("0x10924c94"), 0.0f, ElysiumNpcTunables::EConVar::DebugTurnScalar },
		{ TEXT("debug_dog_turn_scalar"), TEXT("0x1093ad24"), 0.0f, ElysiumNpcTunables::EConVar::DebugDogTurnScalar },
		{ TEXT("tzimisce_turn_scalar"), TEXT("0x1093c9fc"), 0.0f, ElysiumNpcTunables::EConVar::TzimisceTurnScalar },
		// The idle-arm alternative `MaxYawSpeed` falls to when turning anims are ON: "90".
		{ TEXT("debug_turning_speed"), TEXT("0x10923e84"), 0.0f, ElysiumNpcTunables::EConVar::DebugTurningSpeed },
	};

}

// -------------------------------------------------------------------------------------------------
// The navigator seam.
// -------------------------------------------------------------------------------------------------

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
	// The same body as family BaseHelpers' `PatrolNodeInterestRecord` -- one retail function, one
	// port body -- here with retail's own null (0) for "no hint" rather than `INDEX_NONE`. The
	// argument is the node id the route step carries (the caller has read `path+0x14[path+0x10]`).
	const int32 Hint = PatrolNodeInterestRecord(RouteStepIndex);
	return Hint == INDEX_NONE ? 0 : Hint;
}

// -------------------------------------------------------------------------------------------------
// The motor seams.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::MoveProbeCheckStandPosition(const FVector& PositionUnits, int32 ProbeFlags) const
{
	// `thunk_FUN_102e7270(m_pMoveProbe, pos, …, 0, 0)` — `CAI_MoveProbe::CheckStandPosition`.
	// **SEAM**: `m_pMoveProbe` (+0x5d40) is a CHAIN row onto a mover that answers no hull probe.
	(void)PositionUnits;
	(void)ProbeFlags;
	++MotorSeams.MoveProbeChecks;
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

const FElysiumNpc::FRetailYawConVar* FElysiumNpc::RetailYawConVars(int32& OutCount)
{
	OutCount = UE_ARRAY_COUNT(GRetailYawConVars);
	return GRetailYawConVars;
}

float FElysiumNpc::RetailYawConVarValue(const TCHAR* Address)
{
	// Retail reads each of these as `cvar->IsCommand() ? 0.0f : cvar->m_fValue` (`+0x28`); a ConVar
	// object is never a ConCommand, so the answer is the value. An address that names none of them
	// answers 0.0.
	if (Address == nullptr)
	{
		return 0.f;
	}
	for (const FRetailYawConVar& Row : GRetailYawConVars)
	{
		if (FCString::Strcmp(Row.Address, Address) == 0)
		{
			return Row.Table == ElysiumNpcTunables::EConVar::Count ? Row.Default : ElysiumNpcTunables::ConVarFloat(Row.Table);
		}
	}
	return 0.f;
}

// -------------------------------------------------------------------------------------------------
// The species call-outs.
// -------------------------------------------------------------------------------------------------

bool FElysiumNpc::NavHintNodeType(int32 HintNode, int32& OutType) const
{
	// `CAI_Hint+0x5dc m_nHintType`, read off the live `ai_hint` (`FElysiumHint::HintType`). A word of
	// the HINT, not of its network node. False for an index that names no live hint.
	FHintWords Words;
	if (!HintWords(HintNode, Words))
	{
		return false;
	}
	OutType = Words.HintType;
	return true;
}

bool FElysiumNpc::NavHintNodeOrigin(int32 HintNode, FVector& OutOriginUnits) const
{
	// The hint's `GetAbsOrigin()` (vtable `+0x364`) -- the HINT entity's own origin, which every
	// caller here reads directly (`SelectJumpbaseNode`, `SetupSuperJump`, `FindTacticalHintNode`'s
	// lean), not `CAI_Hint::GetPosition` (`0x102d1180`, the node). RETAIL-frame Source units, the
	// frame those callers measure the NPC in (`MotorTailSourceOf`).
	FHintWords Words;
	if (!HintWords(HintNode, Words))
	{
		return false;
	}
	OutOriginUnits = NpcKernelMotor2Shared::MotorTailSourceOf(Words.OriginCm);
	return true;
}

bool FElysiumNpc::NavAllHintNodes(TArray<int32>& OutHintNodes) const
{
	// The global `CAI_Hint` list `DAT_10925450`, next link `+0x5d8`, head first: the world's live
	// hint list in its own order (`GlobalHintList`, `FElysiumEntityWorld::HintList`). False only
	// with no world behind this NPC.
	OutHintNodes = GlobalHintList();
	return World != nullptr;
}

// -------------------------------------------------------------------------------------------------
// The movement tunables — slots 521, 522, 523, 524.
// -------------------------------------------------------------------------------------------------

float FElysiumNpc::GetMaxJumpSpeed() const
{
	// slot 523. `CAI_BaseNPCTroika::GetMaxJumpSpeed` `0x101aa670` returns `_DAT_1044faa8` = 36.0 —
	// a DIFFERENT constant from the base's `0x101a6b60`, which returns the same 18.0 as its step
	// height. `CAI_TestHull::GetMaxJumpSpeed` `0x102d72d0` is its own class's override
	// (`FElysiumNpcTestHull`), on the `CAI_BaseNPC` line and never below this one.
	return NpcKernelMotorShared::GMaxJumpSpeedTroika;
}

// -------------------------------------------------------------------------------------------------
// Slot 516 — the yaw-speed ladder.
// -------------------------------------------------------------------------------------------------

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
	// The cvar differs per species; all three ship ".15".
	const float Ideal = GetIdealYawSpeed();
	const float Scale = RetailYawConVarValue(ConVarAddress);
	const float Result = FMath::Abs(Ideal) * Scale;
	return Result <= GYawFloor ? GYawFloor : Result;
}

float FElysiumNpc::MaxYawSpeed()
{
	// slot 516, `CAI_BaseNPCTroika::MaxYawSpeed` `0x10297ce0`. `CNPC_VDog`, `CNPC_VMingXiao`,
	// `CNPC_VTzimisce` and `CNPC_VWerewolf` override it on their C++ classes (story 5 step 3); the
	// werewolf's body is a scope-trace push/pop around a direct call into this one.

	// `CAI_BaseNPCTroika::MaxYawSpeed` `0x10297ce0`, arm for arm.
	if ((BaseScheduleHost.MemoryBits & NpcKernelMotorShared::GMemoryTurning) != 0)
	{
		return MaxYawSpeedTurningArm(TEXT("0x10924c94"));
	}
	if (NpcFlags.Has(EElysiumNpcFlag::PLAYING_FACE_ANIM))
	{
		return NpcKernelMotorShared::GYawDefault;
	}
	const int32 Activity = ActivityNumber;
	if (Activity < NpcKernelMotorShared::GActCrouchWalk + 1)
	{
		if (NpcKernelMotorShared::GActCrouchIdle - 1 < Activity)
		{
			return NpcKernelMotorShared::GYawCrouch;
		}
		switch (Activity)
		{
		case NpcKernelMotorShared::GActIdle:
		case NpcKernelMotorShared::GActIdleAngry:
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
			return NpcKernelMotorShared::GYawCrouch;
		case GActWalk:
			if ((NpcStateFlags() & 0x80) == 0)
			{
				return RetailYawConVarValue(TEXT("0x1092411c"));
			}
			break;   // in combat, ACT_WALK falls out of the switch to the default
		case NpcKernelMotorShared::GActRun:
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
			return NpcKernelMotorShared::GYawCrouch;
		default:
			break;
		}
	}
	return NpcKernelMotorShared::GYawDefault;
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
			if (Other->AsNpcBase() != nullptr)   // `other+0x94 m_pBaseNPC`
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
	// slot 68, `CAI_BaseNPCTroika::ShouldIgnoreCollision` `0x1029afc0`. `CNPC_VMingXiaoTentacle`
	// (`0x1039eb50`), `CNPC_VRat` (`0x103ad6d0`) and `CNPC_VWerewolf` (`0x103d9ab0`) override it on
	// their C++ classes (story 5 step 3): each runs its own arm and then calls this body directly.

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
	if (BaseScheduleHost.HintNode != INDEX_NONE && NavHintNodeType(BaseScheduleHost.HintNode, HintType)
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

bool FElysiumNpc::NavIgnoreCollision(FElysiumEntity* Other)
{
	// slot 69, `CAI_BaseNPCTroika::NavIgnoreCollision` `0x1029b180`. Gargoyle (`0x10379490`),
	// Hengeyokai (`0x10380f90`), MingXiao (`0x10396fd0`), Tzimisce (`0x103bfa00`), Werewolf
	// (`0x103d9ba0`) and the MingXiao tentacle (`0x1039eb90`) override it on their C++ classes (story 5
	// step 3): each runs its own arm and then calls this body directly.

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
	const bool bHasEnemy = BaseMemory.Enemy.IsSet() && World != nullptr
		&& World->Resolve(BaseMemory.Enemy) != nullptr;
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

// -------------------------------------------------------------------------------------------------
// The non-slot bodies.
// -------------------------------------------------------------------------------------------------

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
	BaseScheduleHost.bShouldMove = true;
}

