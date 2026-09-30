// `CAI_BaseNPC`'s bodies of the `Conditions` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseConditions.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "Substrate/ElysiumMover.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcConditionsBodiesShared.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// `CAI_BaseNPC::RangeAttack1Conditions` (`0x1026d890`), in SOURCE UNITS.
	constexpr float GCondRange1TooCloseForRangedUnits = ElysiumNpcTunables::Hundred;
	constexpr float GCondRange1TooCloseToAttackUnits = ElysiumNpcTunables::TwoHundred;
	constexpr float GCondRange1TooFarUnits = ElysiumNpcTunables::OneThousandTwentyFour;
	// The facing dot `RangeAttack1Conditions` compares (`FCOMP double ptr [0x10449270]`).
	constexpr double GCondAttackFacingDot = ElysiumNpcTunables::HalfDouble;
}

// --- Moved from `ElysiumNpcConditionsBodies.cpp` (story 5 step 5) ---

void FElysiumNpcBase::ClearAttackConditions()
{
	// 0x1026dc80. Eleven `ClearCondition` calls, in retail's order and no other work at all. The
	// list is fixed in the binary; it is NOT derived from the weapon, the state or the capability
	// word, which is why it is spelled out rather than computed.
	static const EElysiumNpcCond Clears[] = {
		EElysiumNpcCond::CanRangeAttack1,          // 0x4f
		EElysiumNpcCond::CanRangeAttack2,          // 0x50
		EElysiumNpcCond::CanMeleeAttack1,          // 0x51
		EElysiumNpcCond::CanMeleeAttack2,          // 0x52
		EElysiumNpcCond::ExtendedBlockedByFriend,  // 0x2e
		EElysiumNpcCond::WaitingAttackTime,        // 0x2f
		EElysiumNpcCond::WeaponHasLos,             // 0x62
		EElysiumNpcCond::WeaponBlockedByFriend,    // 99 = 0x63
		EElysiumNpcCond::WeaponPlayerInSpread,     // 100 = 0x64
		EElysiumNpcCond::WeaponPlayerNearTarget,   // 0x65
		EElysiumNpcCond::WeaponSightOccluded,      // 0x66
	};
	for (const EElysiumNpcCond Cond : Clears)
	{
		Cognition.Conditions.Clear(Cond);
	}
}

void FElysiumNpcBase::ClearSenseConditions()
{
	// 0x1026e5c0, whose whole body is `ClearConditions(0x105c97dc, 0xe)`.
	//
	// The table is fourteen dwords in `.rdata`, READ OUT of retail (`vampire.dll` file offset
	// `0x5c97dc - 0x10000000 + .rdata delta`): `43 45 46 44 5b 5a 6a 6d 6e 6f 6b 6c 71 5e`. It is
	// the union of the SEE family (`0x105c979c`, 6) and part of the HEAR family (`0x105c97b4`, 10),
	// and the two the HEAR sweep clears that this list does NOT are load-bearing:
	// `HEAR_BULLET_IMPACT` (0x70) and `HEAR_FLINCH` (0x72) SURVIVE a sense clear.
	static const EElysiumNpcCond Clears[] = {
		EElysiumNpcCond::SeeHate,           // 0x43
		EElysiumNpcCond::SeeDislike,        // 0x45
		EElysiumNpcCond::SeeEnemy,          // 0x46
		EElysiumNpcCond::SeeFear,           // 0x44
		EElysiumNpcCond::SeeNemesis,        // 0x5b
		EElysiumNpcCond::SeePlayer,         // 0x5a
		EElysiumNpcCond::HearDanger,        // 0x6a
		EElysiumNpcCond::HearCombat,        // 0x6d
		EElysiumNpcCond::HearWorld,         // 0x6e
		EElysiumNpcCond::HearPlayer,        // 0x6f
		EElysiumNpcCond::HearThumper,       // 0x6b
		EElysiumNpcCond::HearBugbait,       // 0x6c
		EElysiumNpcCond::HearPhysicsDanger, // 0x71
		EElysiumNpcCond::Smell,             // 0x5e
	};
	for (const EElysiumNpcCond Cond : Clears)
	{
		Cognition.Conditions.Clear(Cond);
	}
}

void FElysiumNpcBase::RemoveIgnoredConditions()
{
	// 0x1026d7f0, read off the listing (the decompiled C reports a damaged jump table). The whole
	// body is a guard and a dispatch: while `m_NPCState` is 4 (SCRIPT) and `m_hCine` (`+0x5d74`)
	// still resolves through the entity table, call THAT entity's own slot 459. Outside state 4 —
	// and with a dead cine handle — it writes nothing at all.
	if (NpcKernelConditionsShared::CondRetailStateId(Mind.State()) != 4)
	{
		return;
	}
	if (!ScriptOwner.IsSet() || World == nullptr || World->Resolve(ScriptOwner) == nullptr)
	{
		return;
	}
	// That entity's own slot 459, dispatched virtually: the director's body
	// (`FElysiumScriptedSequence::RemoveIgnoredConditions`, `0x101a89a0`).
	if (FElysiumNpcBase* Cine = World->Resolve(ScriptOwner)->AsNpcBase())
	{
		Cine->RemoveIgnoredConditions();
	}
}

int32 FElysiumNpcBase::RangeAttack1Conditions(float FlDot, float FlDist)
{
	// 0x1026d890. A nested threshold tree over the DISTANCE, then one facing test; it RETURNS a
	// condition number and sets nothing — `GatherAttackConditions` (`0x1026dd10`) is what calls
	// `SetCondition` on the answer. Every comparison is strict except the dot, which carries the
	// equal bit (`TEST AH,0x5 / JNP` at `1026d8ec`).
	//
	// `CNPC_VBatSwarm::vfunc553` (`0x103675e0`) and `CNPC_VSheriffSwarm::vfunc553` (`0x103b2590`)
	// are the only overrides and both are unmodified forwards to this body, so there is no species
	// table here: the base answer IS every class's answer.
	if (FlDist < GCondRange1TooCloseForRangedUnits)
	{
		return static_cast<int32>(EElysiumNpcCond::TooCloseForRanged);   // 8
	}
	if (FlDist < GCondRange1TooCloseToAttackUnits)
	{
		return static_cast<int32>(EElysiumNpcCond::TooCloseToAttack);    // 0x5f
	}
	if (FlDist > GCondRange1TooFarUnits)
	{
		return static_cast<int32>(EElysiumNpcCond::TooFarToAttack);      // 0x60
	}
	return static_cast<double>(FlDot) >= GCondAttackFacingDot
		? static_cast<int32>(EElysiumNpcCond::CanRangeAttack1)           // 0x4f
		: static_cast<int32>(EElysiumNpcCond::NotFacingAttack);          // 0x61
}

int32 FElysiumNpcBase::NavType() const
{
	// `0x1027d990` = `m_pNavigator(+0x5d34)->m_navType(+0x18)`: family Motor's stored word, read
	// through its body (`NavGetType`), not a second answer to the same question.
	return NavGetType();
}

bool FElysiumNpcBase::FCanCheckAttacksBase() const
{
	// 0x10270840. Four terms, and the two nav-type refusals come FIRST: a climbing or jumping body
	// never evaluates its conditions at all.
	const int32 Nav = NavType();
	if (Nav == NpcKernelConditionsShared::GCondNavClimb || Nav == NpcKernelConditionsShared::GCondNavJump)
	{
		return false;
	}
	return Cognition.Conditions.Has(EElysiumNpcCond::SeeEnemy)         // 0x46
		&& !Cognition.Conditions.Has(EElysiumNpcCond::EnemyTooFar);    // 0x55
}

void FElysiumNpcBase::SetAlternateAiIdealYaw(float YawDegrees)
{
	// `0x102e0b40` (the motor's yaw clock reset) then `0x102e1c10(motor, yaw, -1.0)` (set the ideal
	// yaw and update). Story 8 (L05 integration): the two are RunTask19's motor primitives now.
	// `YawDegrees` is a RETAIL (Source) yaw, the `motor+0x34` convention.
	MotorMoveStop();                                     // 0x102e0b40
	MotorSetIdealYawAndUpdate(YawDegrees, -1.0f);        // 0x102e1c10
}

bool FElysiumNpcBase::StartOpeningDoor(FElysiumEntity& Door)
{
	// `FUN_10298840`, in its order:
	//   1. the door's slot 246 `GetNPCOpenData(this, &data, m_bOpeningDoorWait)` again;
	//   2. `RestartIdealActivity(data.Activity)` (`0x10289ee0`) -- with NO -1 test (`10298882`), so
	//      a -1 answer restarts activity -1;
	//   3. the lock test `0x100eec70(door, this)`: REFUSED (true) -> the door-blocked notice
	//      `0x1027de00` and FALSE; accepted -> the door's `AcceptInput("Open", this, this)`
	//      (vtable `+0x1d8`, the string at `0x1056512c`) and TRUE.
	// (The earlier comment here had the lock test's arms the other way round: the notice fires on
	// REFUSE, as `ElysiumNpcBaseSenses.cpp`'s `OnDoorBlocked` states.)
	FElysiumDoorBase* DoorBase = Door.AsDoorBase();
	const FElysiumDoorNpcOpenData Data = DoorBase != nullptr
		? DoorBase->GetNPCOpenData(this, bOpeningDoorWait) : FElysiumDoorNpcOpenData();
	RestartIdealActivityId(Data.Activity);                                   // 0x10289ee0
	if (DoorBase == nullptr || DoorBase->IsUseRefused(Handle))               // 0x100eec70(door, npc)
	{
		OnDoorBlocked(Door);                                                 // 0x1027de00
		return false;
	}
	if (World != nullptr)
	{
		// Synchronous, through chokepoint 1: retail calls the door's `AcceptInput` directly from
		// inside the think, so the open lands inside this call (the §2.5.1 in-handler seam).
		World->AcceptInput(Door.Handle, FName(TEXT("Open")), FElysiumVariant::Void(), Handle, Handle);
	}
	return true;
}

void FElysiumNpcBase::OnDoorFullyOpen(FElysiumEntity* Door)
{
	// `0x1027dd10`, called by `DoorHitTop 0x100f0860` on the door's activator NPC.
	if (!IsAlive())                                                          // slot 158
	{
		return;
	}
	if (Door == nullptr)
	{
		return;
	}
	// The door I am holding: slot 532(1) (`+0x850`), reason "fully open".
	const FElysiumEntity* Opening = World != nullptr ? World->Resolve(OpeningDoor) : nullptr;
	if (Opening == Door)
	{
		Slot532(1);
	}
	// Un-pause the navigator ONLY if paused (`0x102ee2e0` then `0x102ee2c0`).
	if (Navigator.IsPaused())
	{
		NavStopMoving();
	}
	BaseScheduleHost.bShouldMove = true;                                     // +0x1a40 = 1
	ValidateNavGoal();                                                       // slot 528 (+0x840)
	// `+0x98 m_pBaseNPCTroika`: modes 1 and 2 (facing the door, waiting on it) end.
	if (FElysiumNpc* const Troika = AsNpc())
	{
		if (Troika->AlternateAi == 1 || Troika->AlternateAi == 2)
		{
			Troika->AlternateAi = 0;
		}
	}
}

// --- Moved from `ElysiumNpcConditionsBodies.cpp` (story 5 step 5) ---

void FElysiumNpcBase::ClearCineIgnoredConditions(FElysiumNpcBase& Partner)
{
	// 0x101a89a0's write list, applied to the director's NPC. Three damage
	// conditions, then `m_bCondTookDamage` (+0x5b80), then ten more — in retail's order, which is
	// NOT sorted and is reproduced as written.
	Partner.Cognition.Conditions.Clear(EElysiumNpcCond::LightDamage);       // 0x4c
	Partner.Cognition.Conditions.Clear(EElysiumNpcCond::HeavyDamage);       // 0x4d
	Partner.Cognition.Conditions.Clear(EElysiumNpcCond::RepeatedDamage);    // 0x4e
	Partner.Cognition.bCondTookDamage = false;                              // +0x5b80
	Partner.Cognition.Conditions.Clear(EElysiumNpcCond::InvestigateLevel);        // 0x1e
	Partner.Cognition.Conditions.Clear(EElysiumNpcCond::CriminalFleeLevel);       // 0x1f
	Partner.Cognition.Conditions.Clear(EElysiumNpcCond::SupernaturalFleeLevel);   // 0x21
	Partner.Cognition.Conditions.Clear(EElysiumNpcCond::HearFlinch);              // 0x72
	Partner.Cognition.Conditions.Clear(EElysiumNpcCond::CriminalAttackLevel);     // 0x20
	Partner.Cognition.Conditions.Clear(EElysiumNpcCond::SupernaturalAttackLevel); // 0x22
	Partner.Cognition.Conditions.Clear(EElysiumNpcCond::InvestigateSound);        // 0x25
	Partner.Cognition.Conditions.Clear(EElysiumNpcCond::InvestigateSight);        // 0x26
	Partner.Cognition.Conditions.Clear(EElysiumNpcCond::Comfort);                 // 0x27
	Partner.Cognition.Conditions.Clear(EElysiumNpcCond::BeingAttacked);           // 10 = 0x0a
}
