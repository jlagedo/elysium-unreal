#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"                  // ElysiumMove::U — the one Source-unit conversion
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// Story 29c-1, family **Conditions** — the condition set and the state machine's edges, at
// `order.md` layers 0–9. 32 rows: the seven slot bodies (459, 463, 477, 553, 554, 560, 564), the
// three species `SelectIdealState` overrides, the four species `OnStateChange` shapes, the two flee
// arms of `PreSelectIdealState`, the two condition refreshers, the disturbed latch, the
// alternate-AI door transaction and the three cop/hunter pursuit counters. The walked prose is
// `docs/vtmb/npc-ai/conditions-and-states.md`.
//
// WHAT THIS FAMILY FOUND that 29c's one-line walks did not have:
//
//  * `thunk_FUN_10269b50` is `ClearCondition`, not "force". Two rows read the other way round —
//    `CNPC_VWerewolf::GatherAttackConditions` SUPPRESSES the melee pair rather than raising it, and
//    `UpdateConditionDeathTriggered`'s "one-shot latch" is not one (see its own body).
//  * Every threshold below is READ OUT OF `vampire.dll`'s `.rdata` at the file offset the image
//    base gives, the method `conditions-and-states.md` already used for `_DAT_104454c4`. Nothing in
//    this file is a chosen number.
//  * Retail `NPC_STATE` ids, from `0x1027e660`'s name table and `0x1026e3e0`'s switch: 1 IDLE,
//    2 COMBAT, 3 ALERT, 4 SCRIPT, 6 PRONE, 7 DEAD, 8 FLEE, 0xb HUNT, 0xe the criminal window.
//    `FElysiumNpc::NpcStateFlags` already carries that mapping and this file uses the same one.

namespace
{
	// --- The recovered constants, by their `.rdata` address ------------------------------------
	// `CAI_BaseNPC::RangeAttack1Conditions` (`0x1026d890`), in SOURCE UNITS.
	constexpr float GCondRange1TooCloseForRangedUnits = 100.0f;   // `_DAT_10450564`
	constexpr float GCondRange1TooCloseToAttackUnits = 200.0f;    // `_DAT_104492b8`
	constexpr float GCondRange1TooFarUnits = 1024.0f;             // `_DAT_1045d650`
	// `CAI_BaseNPC::RangeAttack2Conditions` (`0x1026d920`).
	constexpr float GCondRange2TooCloseForRangedUnits = 64.0f;    // `_DAT_10451acc`
	constexpr float GCondRange2TooFarUnits = 512.0f;              // `_DAT_10483aac`
	// `_DAT_10449270`, a DOUBLE (`FCOMP double ptr`) — the facing dot both bodies share.
	constexpr double GCondAttackFacingDot = 0.5;

	// `FUN_102b2570`'s attack-period numerator: the re-raise window is
	// `m_flLastMeleeStepbackTime + 3.0 / m_flSpeedScale`.
	constexpr float GCondStepbackPeriodNumerator = 3.0f;          // `_DAT_10449258`
	// Its two draws, verbatim: `RandomInt(0,99) < 20` and `RandomInt(0,10) == 0`.
	constexpr int32 GCondStepbackChancePercent = 20;
	constexpr int32 GCondStepbackRareDenominator = 10;
	// `m_bfNPCFrenziedFlags & 0x400`, the frenzy bit the rare arm requires CLEAR. 29b's
	// `FElysiumNpcFlags::FrenziedWord` is the word; `0x400` has no recovered name.
	constexpr uint32 GCondFrenziedSuppressesStepback = 0x400u;
	// The active weapon's flag word (`weapon vtable +0x5a0`) bit 30 — "this weapon is blocked". No
	// recovered name; it is the one term `SHOULD_KICK`'s re-raise needs and this runtime's weapon
	// carries no flag word at all, so the arm asks the seam below.

	// `FUN_10290040`, the door transaction's two mode-2 expiries.
	constexpr double GCondDoorWaitExpireSeconds = 1.0;            // `_DAT_104454c0`
	constexpr double GCondDoorOpenExpireSeconds = 5.0;            // `_DAT_10454110`

	// `CAI_BaseNPCTroika::OnStateChange`'s hunt-state re-arm, `RandomFloat(10, 20)`
	// (`0x41200000` / `0x41a00000` pushed at `102ae1c0`).
	constexpr float GCondHuntExpireMinSeconds = 10.0f;
	constexpr float GCondHuntExpireMaxSeconds = 20.0f;
	// Its criminal-window arm, `0x10293d90(this, 2.0)`: `m_flCriminalIgnoreTimer = curtime + 2.0`.
	constexpr double GCondCriminalIgnoreSeconds = 2.0;

	// `m_afMemory &= 0x07ffffff` — the top FIVE bits of the memory word, dropped on every state
	// change that actually changed the state.
	constexpr uint32 GCondMemoryKeepMask = 0x07ffffffu;

	// `ClearHintNode(this, 5.0)`, the reuse delay the tail imposes.
	constexpr float GCondStateChangeHintReuseSeconds = 5.0f;

	// `CNPC_VGhoulCroucher::OnDisturbed`'s `AddEntityRelationship(player, D_HT, 10)`.
	constexpr int32 GCondDisturbedHatePriority = 10;

	// Source's `Navigation_t`: the two values `CAI_BaseNPC::FCanCheckAttacks` refuses on.
	constexpr int32 GCondNavGround = 0;
	constexpr int32 GCondNavJump = 1;
	constexpr int32 GCondNavClimb = 3;

	// `bits_CAP_WEAPON_MELEE_ATTACK1`. `CAI_BaseNPCTroika::FCanCheckAttacks`'s own arm tests AH's
	// sign bit, which is EAX bit 15 — this mask, not bit 31.
	constexpr int32 GCondCapWeaponMeleeAttack1 = 0x8000;

	// `CNPC_VWerewolf`'s condition `0x7a`. It is above the base registrar's `0x76` and belongs to a
	// Werewolf-line table the census has not decoded, so it is spelled as the number.
	constexpr int32 GCondWerewolfDeathTriggered = 0x7a;
	// `m_Activity == 0x11d` and `m_DoorState == 1`, the pair the latch fires on.
	constexpr int32 GCondWerewolfDeathActivity = 0x11d;
	constexpr int32 GCondWerewolfDoorStateOpen = 1;

	EElysiumNpcCond CondOf(int32 RetailCondition)
	{
		return static_cast<EElysiumNpcCond>(RetailCondition);
	}

	// This runtime's state vocabulary onto retail's `m_NPCState` ids — the same table
	// `FElysiumNpc::NpcStateFlags` uses, restated here as a free function because every body in this
	// file switches on the retail id rather than on the port enum. The retail states this runtime has
	// no member for (8 FLEE, 0xb HUNT, 0xe the criminal window) are unreachable through it, which is
	// why the rules below are written over the RAW id and the dispatch is the only place that maps.
	int32 CondRetailStateId(EElysiumNpcState State)
	{
		switch (State)
		{
		case EElysiumNpcState::Idle:     return 1;
		case EElysiumNpcState::Combat:   return 2;
		case EElysiumNpcState::Alert:    return 3;
		case EElysiumNpcState::Scripted: return 4;
		case EElysiumNpcState::Prone:    return 6;
		case EElysiumNpcState::Dead:     return 7;
		}
		return 0;
	}
}

// =================================================================================================
// Slot 560 — `CAI_BaseNPC::ClearAttackConditions` (`0x1026dc80`)
// =================================================================================================

void FElysiumNpc::ClearAttackConditions()
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

// =================================================================================================
// Slot 477 — `CAI_BaseNPC::ClearSenseConditions` (`0x1026e5c0`)
// =================================================================================================

void FElysiumNpc::ClearSenseConditions()
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

// =================================================================================================
// Slot 459 — `RemoveIgnoredConditions` (`0x1026d7f0`, and the cine body `0x101a89a0`)
// =================================================================================================

void FElysiumNpc::ClearCineIgnoredConditions(FElysiumNpc& Partner)
{
	// 0x101a89a0, the cine entity's own slot-459 body, applied to its scene partner. Three damage
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

FElysiumNpc* FElysiumNpc::CineIgnoredConditionsPartner() const
{
	// SEAM. Retail's chain is `m_hCine (+0x5d74)` -> the cine entity's slot 459 -> its own
	// `m_hTargetEnt` -> that entity's `+0x94` (its NPC). This runtime's `FElysiumEntity::ScriptOwner`
	// carries the first hop, but no scripted-scene object here carries a target entity or the
	// `0x101a8930` "already in this state" predicate the body gates on, so the walk cannot start.
	return nullptr;
}

void FElysiumNpc::RemoveIgnoredConditions()
{
	// 0x1026d7f0, read off the listing (the decompiled C reports a damaged jump table). The whole
	// body is a guard and a dispatch: while `m_NPCState` is 4 (SCRIPT) and `m_hCine` (`+0x5d74`)
	// still resolves through the entity table, call THAT entity's own slot 459. Outside state 4 —
	// and with a dead cine handle — it writes nothing at all.
	if (CondRetailStateId(Mind.State()) != 4)
	{
		return;
	}
	if (!ScriptOwner.IsSet() || World == nullptr || World->Resolve(ScriptOwner) == nullptr)
	{
		return;
	}
	FElysiumNpc* Partner = CineIgnoredConditionsPartner();
	if (Partner == nullptr)
	{
		return;
	}
	ClearCineIgnoredConditions(*Partner);
}

// =================================================================================================
// Slots 553 / 554 — the two ranged attack bands (`0x1026d890`, `0x1026d920`)
// =================================================================================================

int32 FElysiumNpc::RangeAttack1Conditions(float FlDot, float FlDist)
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

int32 FElysiumNpc::RangeAttack2Conditions(float FlDot, float FlDist)
{
	// 0x1026d920. The same shape with its own numbers and ONE FEWER BAND: there is no
	// `TOO_CLOSE_TO_ATTACK` rung, so anything past 64 units and inside 512 is a candidate. Same
	// overrides, same forwards (`0x10367610`, `0x103b25c0`).
	if (FlDist < GCondRange2TooCloseForRangedUnits)
	{
		return static_cast<int32>(EElysiumNpcCond::TooCloseForRanged);   // 8
	}
	if (FlDist > GCondRange2TooFarUnits)
	{
		return static_cast<int32>(EElysiumNpcCond::TooFarToAttack);      // 0x60
	}
	return static_cast<double>(FlDot) >= GCondAttackFacingDot
		? static_cast<int32>(EElysiumNpcCond::CanRangeAttack2)           // 0x50
		: static_cast<int32>(EElysiumNpcCond::NotFacingAttack);          // 0x61
}

// =================================================================================================
// Slot 564 — `FCanCheckAttacks` (`0x102953a0` over `0x10270840`)
// =================================================================================================

int32 FElysiumNpc::NavType() const
{
	// SEAM for `0x1027d990` = `m_pNavigator(+0x5d34)->m_navType(+0x18)`. The shape map binds
	// `+0x5d34` to `FElysiumScriptedCharacter::Motor`, and that motor seam answers no nav type.
	// `NAV_GROUND` is the answer because it is the one every walking body has in retail and because
	// it is the value that does NOT suppress: a seam must not invent a refusal.
	return GCondNavGround;
}

bool FElysiumNpc::FCanCheckAttacksBase() const
{
	// 0x10270840. Four terms, and the two nav-type refusals come FIRST: a climbing or jumping body
	// never evaluates its conditions at all.
	const int32 Nav = NavType();
	if (Nav == GCondNavClimb || Nav == GCondNavJump)
	{
		return false;
	}
	return Cognition.Conditions.Has(EElysiumNpcCond::SeeEnemy)         // 0x46
		&& !Cognition.Conditions.Has(EElysiumNpcCond::EnemyTooFar);    // 0x55
}

bool FElysiumNpc::FCanCheckAttacks()
{
	// 0x102953a0, the Troika-line body every `npc_V*` classname reaches. Its own arm is a
	// SUPPRESSION in front of the base body: a class whose capability word carries
	// `bits_CAP_WEAPON_MELEE_ATTACK1` (0x8000 — the body tests AH's sign bit, which is EAX bit 15,
	// not bit 31) and which HAS an active weapon and is NOT already `m_bInMelee` (+0x6078) answers
	// false outright. Everything else tail-calls `CAI_BaseNPC::FCanCheckAttacks`.
	//
	// The capability word is the port's recovered one (`ElysiumNpcCond::WeaponCapability`) rather
	// than slot 513's generated stub, which tallies a stub and answers 0 — reading the stub here
	// would make the arm permanently dead.
	const int32 Capabilities =
		ElysiumNpcCond::CapabilityBits(ElysiumNpcCond::WeaponCapability(*this));
	if ((Capabilities & GCondCapWeaponMeleeAttack1) != 0)
	{
		FElysiumItem* Active = Inventory.Active(*this);
		const bool bHasActiveWeapon = Active != nullptr && Active->AsWeapon() != nullptr;
		if (bHasActiveWeapon && !bInMelee)
		{
			return false;
		}
	}
	return FCanCheckAttacksBase();
}

// =================================================================================================
// Slot 463 — `OnStateChange`
// =================================================================================================

namespace
{
	const FElysiumNpc::FStateChangeSpecies GCondStateChangeSpecies[] = {
		// The seven holster/draw classes. `FElysiumNpc::ApplyStateWeaponVisibility` already carries
		// the body; these rows are what says WHICH classes reach it, joined to the census.
		{ TEXT("CNPC_VGuard1"),            TEXT("0x1037d020"),
			FElysiumNpc::EStateChangeSpecies::HolsterOnState },
		{ TEXT("CNPC_VHunter"),            TEXT("0x10388880"),
			FElysiumNpc::EStateChangeSpecies::HolsterOnState },
		{ TEXT("CNPC_VGhoulCroucher"),     TEXT("0x103871c0"),
			FElysiumNpc::EStateChangeSpecies::HolsterOnState },
		{ TEXT("CNPC_VHumanCombatant"),    TEXT("0x103871c0"),
			FElysiumNpc::EStateChangeSpecies::HolsterOnState },
		{ TEXT("CNPC_VHumanCombatPatrol"), TEXT("0x103871c0"),
			FElysiumNpc::EStateChangeSpecies::HolsterOnState },
		{ TEXT("CNPC_VSabbatGunman"),      TEXT("0x103871c0"),
			FElysiumNpc::EStateChangeSpecies::HolsterOnState },
		{ TEXT("CNPC_VStalker"),           TEXT("0x103871c0"),
			FElysiumNpc::EStateChangeSpecies::HolsterOnState },
		{ TEXT("CNPC_VYukie"),             TEXT("0x103871c0"),
			FElysiumNpc::EStateChangeSpecies::HolsterOnState },
		{ TEXT("CNPC_ProneDialog"),        TEXT("0x103871c0"),
			FElysiumNpc::EStateChangeSpecies::HolsterOnState },
		// The facial half.
		{ TEXT("CNPC_VTzimisce"),          TEXT("0x103ba2c0"),
			FElysiumNpc::EStateChangeSpecies::FacialExpression },
		// The two cameras: an EMPTY slot-463 body.
		{ TEXT("CNPC_VCamera"),            TEXT("0x10368ea0"),
			FElysiumNpc::EStateChangeSpecies::Suppressed },
		{ TEXT("CNPC_VCameraSecurity"),    TEXT("0x10368ea0"),
			FElysiumNpc::EStateChangeSpecies::Suppressed },
		// The HL2 line. No `npc_V*` classname reaches it; carried so the census can be checked.
		{ TEXT("CAI_BaseHumanoid"),        TEXT("0x10260630"),
			FElysiumNpc::EStateChangeSpecies::HumanoidPreStep },
	};
}

const FElysiumNpc::FStateChangeSpecies* FElysiumNpc::StateChangeSpeciesRows(int32& OutCount)
{
	OutCount = UE_ARRAY_COUNT(GCondStateChangeSpecies);
	return GCondStateChangeSpecies;
}

const FElysiumNpc::FStateChangeSpecies* FElysiumNpc::StateChangeSpeciesOf(const TCHAR* InRetailClass)
{
	if (InRetailClass == nullptr)
	{
		return nullptr;
	}
	for (const FStateChangeSpecies& Row : GCondStateChangeSpecies)
	{
		if (FCString::Stricmp(Row.RetailClass, InRetailClass) == 0)
		{
			return &Row;
		}
	}
	return nullptr;
}

const FElysiumNpc::FStateChangeSpecies* FElysiumNpc::StateChangeSpecies() const
{
	// The vtable's own rule: walk the base chain upward and stop at the first class that fills the
	// slot. A class the census does not carry still answers its own row, which is what lets a test
	// exercise `CAI_BaseHumanoid` by name.
	const FElysiumNpcClass* Cls = RetailClass();
	while (Cls != nullptr)
	{
		if (const FStateChangeSpecies* Row = StateChangeSpeciesOf(Cls->Name))
		{
			return Row;
		}
		Cls = ElysiumNpcKernelClass::Find(Cls->Base);
	}
	return nullptr;
}

const TCHAR* FElysiumNpc::StateChangeExpressionName(EElysiumNpcState NewState)
{
	// `CNPC_VTzimisce::vfunc463` (`0x103ba2c0`) maps the NEW state to an index into
	// `PTR_s_normal_10653120`, whose four entries were read out of `.rdata`:
	//   0 "normal", 1 "angry", 2 "scream", 3 "dead".
	// The switch names three of them — idle -> 0, alert/combat/hunt -> 1, dead -> 3 — and "scream"
	// is reached by no state. Every other state falls through writing nothing.
	switch (CondRetailStateId(NewState))
	{
	case 1:               return TEXT("normal");   // index 0
	case 2: case 3:       return TEXT("angry");    // index 1; retail also lists 0xb HUNT here
	case 7:               return TEXT("dead");     // index 3
	default:              return nullptr;
	}
}

void FElysiumNpc::SetDefaultExpression(const TCHAR* ExpressionName, float BlendSeconds)
{
	// SEAM for `CBaseCombatCharacter::LookupExpressionIndex` + `0x103b9f90(this, index, 1.0)`. There
	// is no `SetExpression` in this runtime — the script API lists it as a stub and family Sounds
	// already recorded the same gap for `CNPC_VTzimisce::PainSound`. The NAME is stored because
	// this runtime names expressions (`NoDeformExpression`, +0x64d0, is the same shape); the blend
	// is dropped and named here rather than faked.
	(void)BlendSeconds;
	DefExpression = ExpressionName != nullptr ? FString(ExpressionName) : FString();
}

void FElysiumNpc::OnStateChangeTroika(EElysiumNpcState OldState, EElysiumNpcState NewState)
{
	// 0x102ae140. Read in two halves, because the body's `goto` structure is exactly that: an
	// "actually changed" half and an unconditional tail every call runs.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const int32 NewRetail = CondRetailStateId(NewState);

	if (OldState != NewState)
	{
		// Slot 610 (`0x102adfe0`), the DEFAULT-expression selector — the one the Tzimisce override
		// above front-runs. It is a generated stub in this runtime, so calling it records a stub
		// tally and writes nothing; the call is kept because retail's order is
		// `vfunc610` -> weapon visibility -> the switch, and dropping it would lose that order.
		Slot610(NewState);

		// `0x102ae310`, the Troika-line weapon show/hide. NOT PORTED HERE and named rather than
		// approximated: its body is a four-arm rule over `GetEnemy()`, two dialogue-partner probes
		// (`+0x62ec` / `+0x6300`, each testing that entity's `+0x571`), the state set
		// {1, 4, 0xc, 0xd} and `IRelationType(m_hClosestPlayer)`, and it hides or unhides through
		// the weapon's own `+0x4f0` / `+0x4ec`. `FElysiumNpc::ApplyStateWeaponVisibility` is the
		// SPECIES half (the seven classes' own slot-463 body), which is a different and simpler
		// rule; conflating the two would give every NPC in the cast a holster policy retail gives
		// nine of them. `0x102ae310` is not one of this family's rows.

		bool bReturnToInitialPosArm = true;
		switch (NewRetail)
		{
		case 2:   // COMBAT
		case 3:   // ALERT
			break;
		case 7:   // DEAD — releases the patrol route and SKIPS `m_bReturnToInitialPos`
			// `if (m_sppPatrolPath +0x6590) 0x1029f5d0(&m_sppPatrolPath)` — the release is guarded
			// on the route being installed, which for this runtime's resolved-point array is
			// "non-empty".
			if (!PatrolPoints.IsEmpty())
			{
				PatrolPoints.Reset();
			}
			bReturnToInitialPosArm = false;
			break;
		case 8:   // FLEE
		{
			// `m_OnStateFleeing` fires with the ENEMY as activator, or with this NPC when there is
			// no enemy — retail substitutes `this`, it does not skip the output.
			const FElysiumEntity* Enemy = World != nullptr && Senses.Memory.Enemy.IsSet()
				? World->Resolve(Senses.Memory.Enemy) : nullptr;
			FireOutput(FName(TEXT("OnStateFleeing")), Enemy != nullptr ? Enemy->Handle : Handle);
			break;
		}
		case 0xb: // HUNT
			HuntExpireTime = Now + static_cast<double>(
				ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(
					GCondHuntExpireMinSeconds, GCondHuntExpireMaxSeconds));
			break;
		case 0xe: // the criminal window
			// `0x10293d90(this, 2.0)`: `m_flCriminalIgnoreTimer (+0x6398) = curtime + 2.0`. It FALLS
			// THROUGH to the `m_bReturnToInitialPos` write, which is why this arm does not break
			// early in retail either.
			Witness.Channel(ElysiumNpcWitness::EChannel::Criminal).IgnoreUntil =
				Now + GCondCriminalIgnoreSeconds;
			break;
		default:
			bReturnToInitialPosArm = false;
			break;
		}
		if (bReturnToInitialPosArm)
		{
			bReturnToInitialPos = true;   // +0x6494
		}

		// The changed-half tail: drop the top five bits of `m_afMemory` (+0x5d8c) and, unless the
		// body is climbing or jumping, clear `PRESERVE_PATH` (+0x14b8 bit 0x8).
		ScheduleHost.MemoryBits &= GCondMemoryKeepMask;
		if (NavType() != GCondNavClimb && NavType() != GCondNavJump)
		{
			NpcFlags.Clear(EElysiumNpcFlag::PRESERVE_PATH);
		}
	}

	// The unconditional tail — it runs even when the state did NOT change, which is the whole reason
	// retail's entry `goto` exists. Six writes in order.
	NpcFlags.Clear(EElysiumNpcFlag::MADE_HUNT_PATH);   // +0x14b8 bit 0x1000
	HuntPatrolPoints.Reset();                          // 0x1029f5d0(&m_sppPatrolPathHunt) +0x6594
	NpcFlags.Clear(EElysiumNpcFlag::AT_CROSSWALK);     // +0x14b8 bit 0x4
	bGoToIdleState = false;                            // +0x63fc
	ClearScheduleHint(GCondStateChangeHintReuseSeconds);  // ClearHintNode(this, 5.0) 0x10295ab0
	Cognition.bCondTookDamage = false;                 // +0x5b80

	// `CAI_BaseNPC::OnStateChange` (`0x1026e3e0`): assign `m_bfNPCStateFlags` from the new state.
	// The byte is a pure function of the state and this runtime DERIVES it
	// (`FElysiumNpc::NpcStateFlags` -> `FElysiumNpcFlags::NpcStateFlagsForRetailState`), so there is
	// nothing to store — the recovered table is already the port's, and the call below is the
	// assertion that it agrees.
	(void)FElysiumNpcFlags::NpcStateFlagsForRetailState(NewRetail);
}

void FElysiumNpc::OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState)
{
	// Slot 463's dispatch: the species pre-step, then the Troika-line body every chaining override
	// ends in.
	if (const FStateChangeSpecies* Row = StateChangeSpecies())
	{
		switch (Row->Shape)
		{
		case EStateChangeSpecies::Suppressed:
			// `CNPC_VCamera::OnStateChange` (`0x10368ea0`) — an empty body. It does not chain, so a
			// camera's state change writes NOTHING: not `m_bReturnToInitialPos`, not the hint
			// release, not even the base state-flag byte.
			return;
		case EStateChangeSpecies::HolsterOnState:
			// Story 29d, family SpeciesLifecycle10: two of the nine classes carry a PRE-STEP ahead of
			// the shared switch — `CNPC_VGuard1::OnStateChange` (`0x1037d020`) and
			// `CNPC_VHunter::OnStateChange` (`0x10388880`), whose first halves run before the first
			// `GetActiveWeapon` in both bodies. Every other class in this row reaches the switch
			// directly, which is what the arm answers for them.
			StateChangeSpeciesPreStep(OldState, NewState);
			// The seven classes' own body: hide on IDLE, unhide on ALERT / COMBAT / 11, then chain.
			ApplyStateWeaponVisibility(NewState);
			break;
		case EStateChangeSpecies::FacialExpression:
			// `CNPC_VTzimisce::vfunc463`: the map runs only on a real transition; the chain into the
			// Troika body runs either way.
			if (OldState != NewState)
			{
				if (const TCHAR* Name = StateChangeExpressionName(NewState))
				{
					SetDefaultExpression(Name, 1.0f);
				}
			}
			break;
		case EStateChangeSpecies::HumanoidPreStep:
			// `CAI_BaseHumanoid::OnStateChange` (`0x10260630`) skips the Troika body entirely and
			// chains STRAIGHT to `CAI_BaseNPC::OnStateChange`. Its two-call pre-step (vtable `+0x934`
			// with the new state, then `0x10260670` with the result) is UNRECOVERED — both are HL2-line
			// bodies outside this closure — and no `npc_V*` classname reaches this row.
			(void)FElysiumNpcFlags::NpcStateFlagsForRetailState(CondRetailStateId(NewState));
			return;
		}
	}
	OnStateChangeTroika(OldState, NewState);
}

// =================================================================================================
// `SelectIdealState`, slot 461 — the three species overrides
// =================================================================================================

namespace
{
	const FElysiumNpc::FIdealStateSpecies GCondIdealStateSpecies[] = {
		{ TEXT("CNPC_VCamera"),            TEXT("0x10369060"),
			FElysiumNpc::EIdealStateSpecies::AlwaysAlert },
		{ TEXT("CNPC_VCameraSecurity"),    TEXT("0x10369060"),
			FElysiumNpc::EIdealStateSpecies::AlwaysAlert },
		{ TEXT("CNPC_VMingXiao"),          TEXT("0x103945a0"),
			FElysiumNpc::EIdealStateSpecies::MingXiao },
		{ TEXT("CNPC_VMingXiaoTentacle"),  TEXT("0x1039e310"),
			FElysiumNpc::EIdealStateSpecies::MingXiaoTentacle },
	};
}

const FElysiumNpc::FIdealStateSpecies* FElysiumNpc::IdealStateSpeciesRows(int32& OutCount)
{
	OutCount = UE_ARRAY_COUNT(GCondIdealStateSpecies);
	return GCondIdealStateSpecies;
}

const FElysiumNpc::FIdealStateSpecies* FElysiumNpc::IdealStateSpeciesOf(const TCHAR* InRetailClass)
{
	if (InRetailClass == nullptr)
	{
		return nullptr;
	}
	for (const FIdealStateSpecies& Row : GCondIdealStateSpecies)
	{
		if (FCString::Stricmp(Row.RetailClass, InRetailClass) == 0)
		{
			return &Row;
		}
	}
	return nullptr;
}

EElysiumNpcState FElysiumNpc::SelectIdealStateSpecies(EIdealStateSpecies Rule, bool bAlive,
	EElysiumNpcState Current, EElysiumNpcState Ideal, bool bHasEnemy)
{
	switch (Rule)
	{
	case EIdealStateSpecies::AlwaysAlert:
		// `CNPC_VCamera::FUN_10369060` — three writes to the file/line ideal-state trace (`+0x1b38`
		// / `+0x1b3c` / `+0x1b40`, which the shape map records ABSENT) and then
		// `m_IdealNPCState = 3`. There is NO test: a camera's ideal state is a constant.
		return EElysiumNpcState::Alert;   // retail 3

	case EIdealStateSpecies::MingXiao:
	{
		// `CNPC_VMingXiao::vfunc461`. The recovered body writes `m_IdealNPCState = 7` when
		// `!(IsAlive() && m_NPCState != 7)` and then ALWAYS overwrites it from the enemy test — the
		// dead write is unreachable in the same call, which is retail's own dead code and is
		// reproduced by leaving the branch here with nothing it can keep.
		const bool bDeadWrite = !(bAlive && Current != EElysiumNpcState::Dead);
		(void)bDeadWrite;
		return bHasEnemy ? EElysiumNpcState::Combat : EElysiumNpcState::Idle;   // retail 2 / 1
	}

	case EIdealStateSpecies::MingXiaoTentacle:
		// `CNPC_VMingXiaoTentacle::vfunc461`. Here the dead arm is REAL: a tentacle whose CURRENT or
		// IDEAL state is already 7 stays 7 and never reaches the enemy test.
		if (Current == EElysiumNpcState::Dead || Ideal == EElysiumNpcState::Dead)
		{
			return EElysiumNpcState::Dead;   // retail 7
		}
		return bHasEnemy ? EElysiumNpcState::Combat : EElysiumNpcState::Idle;
	}
	return Current;
}

bool FElysiumNpc::SelectIdealStateForSpecies(EElysiumNpcState& OutIdeal) const
{
	const FElysiumNpcClass* Cls = RetailClass();
	const FIdealStateSpecies* Row = nullptr;
	while (Cls != nullptr && Row == nullptr)
	{
		Row = IdealStateSpeciesOf(Cls->Name);
		Cls = ElysiumNpcKernelClass::Find(Cls->Base);
	}
	if (Row == nullptr)
	{
		return false;
	}
	OutIdeal = SelectIdealStateSpecies(Row->Rule, !IsInert(), Mind.State(), Mind.IdealState(),
		Senses.Memory.Enemy.IsSet());
	return true;
}

// =================================================================================================
// `RequestDesiredState` — the two flee arms of `PreSelectIdealState`
// =================================================================================================

int32 FElysiumNpc::RequestFleeDesiredState(EElysiumNpcCond GateCondition, int32 RetailSourceLine)
{
	// `FUN_102ad260` (0x21) and `FUN_102ad2d0` (0x1f), which differ only in the condition and the
	// source line. The gate is `HasInterruptCondition` (`0x10269d30`) — an INSTALLED schedule whose
	// mask lists the condition AND the condition standing. A gathered law level with no program
	// asking for it does not make an NPC flee.
	if (!ElysiumSchedule::HasInterruptCondition(Schedule, *this, Cognition.Conditions, GateCondition))
	{
		return 0;
	}
	// `if (m_NPCState != 8) m_bfAINPCFlags |= 0x100` — `INITIAL_FLEE`, armed only on the way IN. This
	// runtime has no state 8, so the test can never be false and the flag is always armed; the
	// comparison is written out rather than folded so the day a flee state lands it is already here.
	if (Mind.State() != EElysiumNpcState::Dead && CondRetailStateId(Mind.State()) != 8)
	{
		NpcFlags.Set(EElysiumNpcFlag::INITIAL_FLEE);
	}
	Mind.RequestDesiredState(8, RetailSourceLine);
	return 8;
}

// =================================================================================================
// The alternate-AI door transaction
// =================================================================================================

void FElysiumNpc::EnterAlternateAi()
{
	// `FUN_10298800`, three stores in order.
	ScheduleHost.bShouldMove = false;                 // +0x1a40
	StopMoving();                                     // 0x102ee2a0 on m_pNavigator (+0x5d34)
	AlternateAi = 1;                                  // +0x644c
}

bool FElysiumNpc::OpeningDoorFacingPoint(const FElysiumEntity& Door, bool bWait,
	FVector& OutPointCm) const
{
	// SEAM for the door's own vtable `+0x3d8`, called as `(this, &point, m_bOpeningDoorWait)`. This
	// runtime's doors carry no NPC-open point, so the transaction takes retail's
	// `iStack_10 == -1` arm.
	(void)Door;
	(void)bWait;
	(void)OutPointCm;
	return false;
}

void FElysiumNpc::SetAlternateAiIdealYaw(float YawDegrees)
{
	// SEAM for `0x102e0b40` (reset steering) + `0x102e1c10(motor, yaw, -1.0)` (set the ideal yaw,
	// unlimited turn rate). Family Hints stands `SetMotorHintYaw` over the same absent motor word;
	// this one is kept separate because it is a different retail call with a different turn-rate
	// argument, and folding them would hide that.
	(void)YawDegrees;
}

// `FacingIdeal` (`0x10278c80`) is family **Facing**'s body and is called, not restated.

bool FElysiumNpc::StartOpeningDoor(FElysiumEntity& Door)
{
	// SEAM for `FUN_10298840`. Its body asks the door for its point again, `RestartIdealActivity`s
	// onto it (`0x10289ee0`), and then either `0x1027de00` (the push) when `0x100eec70` accepts the
	// pair, or the door's own vtable `+0x1d8` use handler. None of the four has a source here.
	(void)Door;
	return false;
}

bool FElysiumNpc::RunAlternateAiOpeningDoor(double Now)
{
	// `FUN_10290040`, `RunAlternateAI`'s mode-1 arm.
	//
	// 1. A dead `m_hOpeningDoor` (+0x5d24) RESETS the mode to 0 and answers false — the one place
	//    the transaction lets go of the body.
	if (World == nullptr || !OpeningDoor.IsSet() || World->Resolve(OpeningDoor) == nullptr)
	{
		AlternateAi = 0;
		return false;
	}
	FElysiumEntity* Door = World->Resolve(OpeningDoor);

	// 2. Ask the door where to stand. A refusal answers false WITHOUT resetting the mode: the
	//    transaction stays installed and asks again next think.
	FVector PointCm = FVector::ZeroVector;
	if (!OpeningDoorFacingPoint(*Door, bOpeningDoorWait, PointCm))
	{
		return false;
	}

	// 3. Face it — `0x102e0b40` on the motor, `VecToYaw` (`0x101d2c70`) over the returned direction,
	//    then `0x102e1c10(motor, yaw, -1.0)`. The yaw arithmetic is retail's and is done here; the
	//    write is a seam.
	SetAlternateAiIdealYaw(
		static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(PointCm.Y, PointCm.X))));

	// 4. Only once FACING does the transaction advance. Both advance arms set mode 2 and stamp an
	//    expiry; the WAIT arm uses 1.0 s and the open arm 5.0 s, and the wait arm does not run the
	//    follow-up at all.
	if (FacingIdeal())
	{
		if (bOpeningDoorWait)
		{
			AlternateAi = 2;
			AlternateAiExpireTime = Now + GCondDoorWaitExpireSeconds;
			return true;
		}
		if (StartOpeningDoor(*Door))
		{
			AlternateAi = 2;
			AlternateAiExpireTime = Now + GCondDoorOpenExpireSeconds;
		}
	}
	// Retail returns 1 from here whether or not it advanced: the transaction still owns the body.
	return true;
}

// =================================================================================================
// The disturbed latch
// =================================================================================================

bool FElysiumNpc::IsDisturbed() const
{
	// `CNPC_VGhoulCroucher::IsDisturbed` (`0x1037bb20`). The 121 bytes are an AI scope-trace push and
	// pop around one read; the body IS `return m_bWasDisturbed`.
	return bWasDisturbed;
}

void FElysiumNpc::OnDisturbed(FElysiumEntity* Disturber)
{
	// `CNPC_VGhoulCroucher::OnDisturbed` (`0x1037b6e0`). The whole body is under one latch: a second
	// disturbance writes nothing and fires nothing.
	if (bWasDisturbed)
	{
		return;
	}
	bWasDisturbed = true;     // +0x6666
	bUnawareExited = false;   // +0x6667

	// The split is on the disturber's cached `CBasePlayer*` (`+0xa8`) — "was it a player", the same
	// read the see-unknown sweep makes. This runtime asks the world for its player and compares, the
	// way `ShouldInvestigate` does.
	const FElysiumPlayer* Player = World != nullptr ? World->FindPlayer() : nullptr;
	const bool bDisturberIsPlayer = Disturber != nullptr && Player != nullptr
		&& static_cast<const FElysiumEntity*>(Player) == Disturber;
	FName Output;
	if (!bDisturberIsPlayer)
	{
		// `0x10293a80 SetClosestPlayer` — the generic handler, which rewrites `m_hClosestPlayer`
		// (+0x628c) from the nearest live player within 20000 units, or clears it when there is none.
		Senses.SetClosestPlayer(*this, World != nullptr ? World->NowSeconds() : 0.0);
		Output = FName(TEXT("OnDisturbed"));          // +0x6674
	}
	else
	{
		// The player arm does NOT run the sweep: it writes the DISTURBER's handle straight into
		// `m_hClosestPlayer` and fires the other output.
		Senses.Memory.ClosestPlayer = Player->Handle;
		Output = FName(TEXT("OnDisturbedByPlayer"));  // +0x668c
	}
	FireOutput(Output, Disturber != nullptr ? Disturber->Handle : FElysiumEntityHandle::Invalid());

	// Then, and only when `m_hClosestPlayer` now resolves to a live entity,
	// `AddEntityRelationship(player, D_HT, 10)`. Note the handle is re-read: on the non-player arm
	// this is whatever the sweep just found, which may be a player the disturbance had nothing to do
	// with. Reproduced.
	if (World == nullptr || !Senses.Memory.ClosestPlayer.IsSet())
	{
		return;
	}
	const FElysiumEntity* Closest = World->Resolve(Senses.Memory.ClosestPlayer);
	if (Closest == nullptr)
	{
		return;
	}
	Relationships.SetEntity(Senses.Memory.ClosestPlayer, EElysiumRelationship::Hate,
		GCondDisturbedHatePriority);
}

// =================================================================================================
// The cop / hunter pursuit counters — the PLAYER's `+0x1d10` and `+0x1d14`
// =================================================================================================

void FElysiumNpc::PromoteCriminalWindowNpcs(FElysiumEntityWorld& InWorld)
{
	// SEAM for `FUN_103705e0`, the first of `OnCopPursuitStart`'s two hooks: walk the global NPC
	// list and put every body whose `GetState()` (slot 464) is **14** into state **2** via
	// `0x102ae840(npc, 2, false)`. This runtime's `EElysiumNpcState` has no member for retail state
	// 14 — the criminal window `UpdateIdealState` names — so the predicate is unsatisfiable and the
	// sweep visits nobody. The walk is written out rather than omitted so the day state 14 exists
	// the hook is already wired.
	(void)InWorld;
}

void FElysiumNpc::OnCopPursuitStart(FElysiumPlayer& Player)
{
	// `FUN_1017f650`. The hooks run on the PRE-increment count — the test is `== 0`, and the
	// increment follows it, so a second cop joining a pursuit runs neither hook.
	if (Player.Police.CopsInPursuit == 0)
	{
		if (Player.World != nullptr)
		{
			PromoteCriminalWindowNpcs(*Player.World);
		}
		// `0x1017f980`: fire the game-rules output at `+0x480` with the player as activator AND
		// caller, then zero `player + 0x1d1c`. The output half is what `ElysiumLaw`'s pursuit edge
		// already fires (`OnStartCopPursuitMode`), which is why the increment below goes through
		// `SetCopPursuitCount` rather than touching the counter directly. UNRECOVERED: `+0x1d1c`,
		// which the census does not name and no port member carries.
	}
	ElysiumLaw::SetCopPursuitCount(Player, Player.Police.CopsInPursuit + 1);
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("CSActs: OnCopPursuitStart - %d in pursuit"),
		Player.Police.CopsInPursuit);
}

void FElysiumNpc::OnHunterPursuitStart(FElysiumPlayer& Player)
{
	// `FUN_1017f7b0` — the same shape with ONE hook (`0x1017fa40`, the game-rules output at
	// `+0x4e0`), which `ElysiumLaw`'s `OnStartHunterPursuitMode` edge already fires.
	ElysiumLaw::SetHunterPursuitCount(Player, Player.Police.HuntersInPursuit + 1);
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("CSActs: OnHunterPursuitStart - %d in pursuit"),
		Player.Police.HuntersInPursuit);
}

void FElysiumNpc::OnHunterPursuitStop(FElysiumPlayer& Player)
{
	// `FUN_1017f830`. The ASYMMETRY with the two start bodies is retail's: this one DECREMENTS
	// FIRST and then tests the post-decrement count against zero, where the start bodies test first
	// and increment after. Both edges therefore fire exactly once per 0<->1 crossing.
	//
	// DIVERGENCE (named, and pre-existing): retail does not clamp — an unbalanced stop drives
	// `+0x1d14` negative and the next start then has no 0->1 edge to fire. `ElysiumLaw::
	// SetHuntersInPursuit` clamps at zero, so this runtime re-fires the start edge where retail
	// would have swallowed it. The clamp is `ElysiumLaw`'s and is not this family's to remove.
	ElysiumLaw::SetHunterPursuitCount(Player, Player.Police.HuntersInPursuit - 1);
	UE_LOG(LogElysiumNpcEnt, Verbose, TEXT("CSActs: OnHunterPursuitStop - %d in pursuit"),
		Player.Police.HuntersInPursuit);
}

// =================================================================================================
// The two condition refreshers
// =================================================================================================

void FElysiumNpc::RefreshCombatConditions()
{
	// `FUN_102b2570`. The one producer of `SHOULD_STEPBACK` (0x0e), `SHOULD_KICK` (0x0f) and
	// `TOO_FAR_FOR_MELEE` (0x09) in the whole closure.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;

	// 1. Both products are cleared UNCONDITIONALLY, before any gate. A body that leaves melee keeps
	//    neither.
	Cognition.Conditions.Clear(EElysiumNpcCond::ShouldStepback);   // 0x0e
	Cognition.Conditions.Clear(EElysiumNpcCond::ShouldKick);       // 0x0f

	// 2. Not in melee (`m_bInMelee` +0x6078) is the whole body's gate.
	if (!bInMelee)
	{
		return;
	}

	// 3. `ENEMY_TOO_FAR` (0x55) standing short-circuits the rest into ONE write: `TOO_FAR_FOR_MELEE`
	//    (0x09). Note it is a plain `HasCondition`, not the interrupt form.
	if (Cognition.Conditions.Has(EElysiumNpcCond::EnemyTooFar))
	{
		Cognition.Conditions.Set(EElysiumNpcCond::TooFarForMelee);
		return;
	}

	// 4. The stepback arm. Its FIRST term is a two-way timer test, and the OR is retail's:
	//    `m_flLastMeleeStepbackTime < m_flLastAttackTime` (I have attacked since I last stepped back)
	//    OR `m_flLastMeleeStepbackTime + 3.0 / m_flSpeedScale < curtime` (the period elapsed).
	//    `m_flSpeedScale` is `CBaseCombatCharacter +0x1488`, which this runtime reads through
	//    `ElysiumWeapons::AttackSpeedScale` (1.0 until a feat carries a scalar).
	const float SpeedScale =
		FMath::Max(ElysiumWeapons::AttackSpeedScale(*this), KINDA_SMALL_NUMBER);
	const bool bStepbackWindowOpen =
		LastMeleeStepbackTime < LastAttackTime
		|| LastMeleeStepbackTime + static_cast<double>(GCondStepbackPeriodNumerator / SpeedScale)
			< Now;
	if (bStepbackWindowOpen)
	{
		FRandomStream& Rng = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
		// Arm A: too close, and a 20-in-100 draw.
		bool bStepback = Cognition.Conditions.Has(EElysiumNpcCond::TooCloseToAttack)   // 0x5f
			&& Rng.RandRange(0, 99) < GCondStepbackChancePercent;
		if (!bStepback)
		{
			// Arm B: NOT frenzied (`m_bfNPCFrenziedFlags & 0x400` clear), and none of
			// `TOO_FAR_FOR_MELEE` (9), `TOO_FAR_TO_ATTACK` (0x60), `CAN_MELEE_ATTACK1` (0x51)
			// standing, and a 1-in-11 draw (`RandomInt(0, 10) == 0`).
			bStepback = !NpcFlags.HasFrenzied(GCondFrenziedSuppressesStepback)
				&& !Cognition.Conditions.Has(EElysiumNpcCond::TooFarForMelee)
				&& !Cognition.Conditions.Has(EElysiumNpcCond::TooFarToAttack)
				&& !Cognition.Conditions.Has(EElysiumNpcCond::CanMeleeAttack1)
				&& Rng.RandRange(0, GCondStepbackRareDenominator) == 0;
		}
		if (bStepback)
		{
			Cognition.Conditions.Set(EElysiumNpcCond::ShouldStepback);
		}
	}

	// 5. The kick arm, evaluated whether or not step 4 fired. Four terms in order:
	//    the running program's mask must LIST `SHOULD_KICK` (`ConditionInterruptsCurrentSchedule`
	//    `0x10269c70` — the mask alone, not the condition); `m_flNextAttack` (+0x1564) must have
	//    passed; `TOO_CLOSE_TO_ATTACK` must stand; and there must be an active weapon whose flag word
	//    (`weapon vtable +0x5a0`) carries bit 30.
	if (!ElysiumSchedule::MaskHasCondition(Schedule, *this, EElysiumNpcCond::ShouldKick))
	{
		return;
	}
	if (!(NextAttackTime < Now))
	{
		return;
	}
	if (!Cognition.Conditions.Has(EElysiumNpcCond::TooCloseToAttack))
	{
		return;
	}
	FElysiumItem* Active = Inventory.Active(*this);
	if (Active == nullptr || Active->AsWeapon() == nullptr)
	{
		return;
	}
	if (WeaponFlagBlocksAttack())
	{
		Cognition.Conditions.Set(EElysiumNpcCond::ShouldKick);
	}
}

bool FElysiumNpc::WeaponFlagBlocksAttack() const
{
	// SEAM for the active weapon's flag word, `weapon vtable +0x5a0` bit 30 (`uVar1 >> 0x1e & 1`),
	// the last term of `RefreshCombatConditions`' kick arm. This runtime's `FElysiumWeapon` carries
	// no retail flag word at all — the capability answer (`ElysiumNpcCond::WeaponCapability`) is two
	// named bits and deliberately not a register — so nothing can answer it and the kick condition
	// is never raised. UNRECOVERED: the bit's name.
	return false;
}

void FElysiumNpc::RefreshOccludedCondition(EElysiumNpcCond Cond, double& InOutStamp, double Now)
{
	// `FUN_1028e700`, the occlusion debounce. It is a SUPPRESSOR, not a producer: the condition its
	// caller already raised is taken away again until `m_flOccludedDelay` (+0x62c8) has elapsed
	// since the first pass that raised it.
	if (!Cognition.Conditions.Has(Cond))
	{
		// Dropped: reset the sentinel so the next raise re-arms a fresh delay. The sentinel is
		// `_DAT_104454c4` = 0.0f.
		InOutStamp = 0.0;
		return;
	}
	if (InOutStamp == 0.0)
	{
		InOutStamp = static_cast<double>(OccludedDelay) + Now;
	}
	if (Now < InOutStamp)
	{
		Cognition.Conditions.Clear(Cond);
	}
}

// =================================================================================================
// `CNPC_VWerewolf::UpdateConditionDeathTriggered` (`0x103cc890`)
// =================================================================================================

void FElysiumNpc::UpdateConditionDeathTriggered()
{
	// The body, in order — and the order is the point.
	//
	// `ClearCondition(0x7a)` runs FIRST and UNCONDITIONALLY. The `if (!HasCondition(0x7a))` that
	// guards the output therefore ALWAYS passes: the "fire once" the guard reads as is not one, and
	// `m_OnConditionDeathTriggered` fires on EVERY pass while the activity/door pair holds. That is
	// the shipped behaviour and it is reproduced; the guard is kept in place rather than removed so
	// a reader can see what it was meant to be.
	Cognition.Conditions.Clear(CondOf(GCondWerewolfDeathTriggered));

	if (CurrentRetailActivityId() != GCondWerewolfDeathActivity   // m_Activity +0xfec == 0x11d
		|| WerewolfDoorState != GCondWerewolfDoorStateOpen)       // m_DoorState +0x6680 == 1
	{
		return;
	}

	if (!Cognition.Conditions.Has(CondOf(GCondWerewolfDeathTriggered)))
	{
		// `thunk_FUN_10265a90(this, GetEnemy())` — retail's "myself" self-reference write, then the
		// output with the enemy as activator and this NPC as caller.
		const FElysiumEntity* Enemy = World != nullptr && Senses.Memory.Enemy.IsSet()
			? World->Resolve(Senses.Memory.Enemy) : nullptr;
		FireOutput(FName(TEXT("OnConditionDeathTriggered")),
			Enemy != nullptr ? Enemy->Handle : FElysiumEntityHandle::Invalid());
	}
	Cognition.Conditions.Set(CondOf(GCondWerewolfDeathTriggered));
}
