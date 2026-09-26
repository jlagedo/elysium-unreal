#include "Substrate/ElysiumNpcBach.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSchedule.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelAnim10_2Shared.h"
#include "Substrate/ElysiumNpcKernelCombat10_2Shared.h"
#include "Substrate/ElysiumNpcKernelDamage2Shared.h"
#include "Substrate/ElysiumNpcKernelLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcKernelPrecache10Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcKernelSpeciesMisc10_2Shared.h"
#include "Substrate/ElysiumNpcKernelState19Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	constexpr TCHAR GAnim10_2FileBach[] = TEXT("NPC_VBach.cpp");          // 0x1062eadc
	// Bach's two authored weapon classnames.
	constexpr TCHAR GAnim10_2BachRifle[] = TEXT("item_w_rem_m_700_bach");  // 0x105c1c70
	constexpr TCHAR GAnim10_2BachKatana[] = TEXT("item_w_katana");         // 0x10587668
	constexpr float GAnim10_2BachFailDelay = ElysiumNpcTunables::Fifteen;
	int32 Anim10_2RetailNpcState(EElysiumNpcState State)
	{
		switch (State)
		{
		case EElysiumNpcState::Idle:     return 1;
		case EElysiumNpcState::Combat:   return 2;
		case EElysiumNpcState::Alert:    return 3;
		case EElysiumNpcState::Scripted: return 4;
		case EElysiumNpcState::Prone:    return 6;
		case EElysiumNpcState::Dead:     return 7;
		default:                         return 0;
		}
	}
	const TCHAR* const GBachFile = TEXT("NPC_VBach.cpp");
	// Retail condition ids this family reads that `EElysiumNpcCond` does not name: `CNPC_VBach`'s
	// three, which live above the base registrar's `0x76` in a Bach-line table the census does not
	// carry. Named by NUMBER rather than folded into the enum, because a name this project cannot
	// recover would be an invented one.
	constexpr EElysiumNpcCond GBachCondReposition = static_cast<EElysiumNpcCond>(0x7b);
	constexpr EElysiumNpcCond GBachCondMelee = static_cast<EElysiumNpcCond>(0x7a);
	constexpr EElysiumNpcCond GBachCondRanged = static_cast<EElysiumNpcCond>(0x79);
	// Retail's `m_NPCState` id for this runtime's typed state — the same table
	// `ElysiumNpcKernelConditions.cpp` keeps as a file static, restated because Bach's tail switches
	// on the RAW id.
	int32 Combat10RangedRetailStateId(EElysiumNpcState State)
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
	// The two Bach weapon classnames, `s_item_w_katana_10587668` and
	// `s_item_w_rem_m_700_bach_105c1c70`.
	const TCHAR* const GBachKatana = TEXT("item_w_katana");
	const TCHAR* const GBachRifle = TEXT("item_w_rem_m_700_bach");
	constexpr float GrenadeCooldown = ElysiumNpcTunables::Five;
	constexpr float GrenadeThinkDelay = 3.0f;       // _DAT_10449258
	constexpr float GrenadeThinkSlack = ElysiumNpcTunables::Hundredth;
	constexpr double GMiscHalfDouble = ElysiumNpcTunables::HalfDouble;
	constexpr float GMiscYawHigh = ElysiumNpcTunables::OneTwenty;
	// `CNPC_VBach`'s two answers per slot, and the shared refusal.
	constexpr int32 GMiscBachRange1Answer = 0x4f;   // COND_CAN_RANGE_ATTACK1
	constexpr int32 GMiscBachRange2Answer = 0x50;   // COND_CAN_RANGE_ATTACK2
	constexpr int32 GMiscBachRefusal = 0x61;        // COND_NOT_FACING_ATTACK
	const TCHAR* const GBachWeapons[] = {
		TEXT("item_w_grenade_frag"),
		TEXT("item_w_katana"),
		TEXT("item_w_rem_m_700_bach"),
	};
	const TCHAR* const GBachSounds[] = {
		TEXT("Character/Boss/Bach/bach_grenade.wav"),
		TEXT("Character/Boss/Bach/bach_shield.wav"),
		TEXT("Character/Boss/Bach/bach_camp_warn.wav"),
		TEXT("Character/Boss/Bach/bach_holy_light.wav"),
		TEXT("Character/Boss/Bach/snipe_warn6.wav"),
	};
	// The two `m_NPCState` values slot 609's species gate admits: `NPC_STATE_SCRIPT` (4) and 0xc,
	// which this runtime's `EElysiumNpcState` has no member for. Both are compared as raw numbers.
	constexpr int32 ShootAtHintStateScript = 4;
	constexpr int32 ShootAtHintStateTwelve = 0xc;
	// `CNPC_VBach`'s slot-606 condition, `COND_ENEMY_OCCLUDED`.
	constexpr EElysiumNpcCond BachOccludeCondition = EElysiumNpcCond::EnemyOccluded;
	// The NPC's `m_NPCState` in RETAIL's ordinals. Family **Anim** keeps an identical private copy
	// for the same reason: neither family owns the other's file, and slot 609's gate compares raw
	// numbers so the mapping has to happen before the comparison.
	int32 SpeciesRetailNpcState(EElysiumNpcState State)
	{
		switch (State)
		{
		case EElysiumNpcState::Idle:     return 1;
		case EElysiumNpcState::Combat:   return 2;
		case EElysiumNpcState::Alert:    return 3;
		case EElysiumNpcState::Scripted: return 4;
		case EElysiumNpcState::Prone:    return 6;
		case EElysiumNpcState::Dead:     return 7;
		default:                         return 0;
		}
	}
	constexpr TCHAR GTranslate19BachKatana[] = TEXT("item_w_katana");   // 0x10587668
}

const FElysiumNpcClass* FElysiumNpcBach::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 606: `0x10364280`, the arm-then-fire gate that calls the Troika body `0x102b8320` directly.
/** `0x10364280` — `CNPC_VBach`'s slot 606. `Arg` is the base body's own argument, passed straight
 *  through on the delegating arm and read by nothing in the gate. */
int32 FElysiumNpcBach::Slot606(int32 Arg)
{
	// `0x10364280`, the whole body:
	//
	//     if (!HasCondition(COND_ENEMY_OCCLUDED 0x48)) { m_bFireOccluded = 0; return 0; }
	//     if (m_bFireOccluded)  return Base606(this, param_1);      // thunk 0x102b8320
	//     m_bFireOccluded = 1;
	//     return 0;
	//
	// An ARM-THEN-FIRE gate: the first pass on which Bach sees the condition only raises its own
	// flag and refuses; the second and later passes delegate to the Troika body. The flag is cleared
	// the moment the condition drops, so the delay is re-paid every time the enemy goes behind
	// cover — it is a one-pass hysteresis, not a one-shot.
	//
	// The base `0x102b8320` is family **TroikaHelpers**' `Slot606`. It is CALLED, so Bach's gate
	// wraps the real rule rather than replacing it. Retail's answers are the Troika body's numbers;
	// `0` is its "no opinion". The call is DIRECT (thunk `0x100026b7`), so it is qualified to the
	// Troika owner and never re-enters `FElysiumNpcBach::Slot606`.
	if (!Cognition.Conditions.Has(BachOccludeCondition))
	{
		bBachFireOccluded = false;
		return 0;
	}
	if (bBachFireOccluded)
	{
		return FElysiumNpc::Slot606(Arg);
	}
	bBachFireOccluded = true;
	return 0;
}

// Slot 609: `0x103661f0`, a state gate that tail-calls the Troika hint search `0x102b6b50`
// directly on admission and otherwise zeroes `m_pShootAtHintNode` and answers NULL.
void* FElysiumNpcBach::Slot609(bool bForce)
{
	if (!FUN_103661f0(bForce))
	{
		return nullptr;
	}
	return FElysiumNpc::Slot609(bForce);
}

// Slot 420: `0x10363940`.
// `0x10363940`
void FElysiumNpcBach::NPCInit()
{
	TroikaNPCInit();
	DistTooFar = SwarmDistTooFar;                                        // 0x477fff00
}

// Slot 104: `0x103637b0`.
// 0x103637b0
void FElysiumNpcBach::Precache()
{
	// `CNPC_VBach::Precache` `0x103637b0` — the Troika body, then THREE weapons and then FIVE
	// sounds. The weapons-before-sounds order is this arm's fact.
	TroikaPrecache();
	for (const TCHAR* Weapon : GBachWeapons)
	{
		NpcKernelPrecache10Shared::Precache10Other(*this, Weapon);
	}
	for (const TCHAR* Sound : GBachSounds)
	{
		NpcKernelPrecache10Shared::Precache10Sound(*this, Sound);
	}
}

// Slot 463: `0x103639b0`. While `m_bCanFightYet` is 0, ALERT / COMBAT snap back through
// `SetState(old)` and never reach the Troika body; otherwise a direct call into it.
void FElysiumNpcBach::OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState)
{
	if (BachOnStateChange(LastOnStateChangeOldRetail, LastOnStateChangeNewRetail))
	{
		return;
	}
	OnStateChangeTroika(OldState, NewState);
}

// Slot 461: `0x10363b40`, the selector tag 0xe and then a direct call into the human line's `0x103851e0`.
int32 FElysiumNpcBach::SelectIdealStateRetail()
{
	SelectIdealStateSelector = 0xe;
	return HumanSelectIdealState();
}

// Slot 604: `0x10364080`, which replaces the Troika body wholesale; its argument is read by no arm.
/** `CNPC_VBach::SelectScheduleMeleeCombat` (`0x10364080`), 395 bytes. A weapon-discipline prologue —
 *  Bach must be holding the right gun or the right sword for the condition he is in — and then the
 *  HUMAN body, with a `+0x6444` clear and a forced `0x159` when that answered zero. */
int32 FElysiumNpcBach::SelectScheduleMeleeCombat(int32 Unused)
{
	(void)Unused;
	// `CNPC_VBach::SelectScheduleMeleeCombat` `0x10364080`, 395 bytes, `CNPC_VBach#604` only.
	//
	// A weapon-DISCIPLINE prologue — Bach must be holding the right gun or the right sword for the
	// condition he is in — and then the HUMAN body, with a `+0x6444` clear and a forced `0x159` when
	// that answered zero. All three conditions (0x7b, 0x7a, 0x79) are read through `0x10269aa0`,
	// `HasCondition`; none of them has a producer in this runtime, which is stated at the call.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	// `0x7b`: the fail arm. It stamps a curtime deadline at `+0x6690` and returns 0x15a.
	if (Conds.Has(static_cast<EElysiumNpcCond>(0x7b)))
	{
		BachNextHolyLightTime = Now + GAnim10_2BachFailDelay;   // curtime + _DAT_10463584 (15.0f)
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0x15a"), GAnim10_2FileBach, 622));
		return 0x15a;
	}

	// `CBaseCombatCharacter::GetActiveWeapon()` is fetched ONCE here and the `0x7a` condition read
	// immediately after it, before either is used.
	const FElysiumEntity* Weapon = ActiveWeaponEntity();
	const bool bKatanaCondition = Conds.Has(static_cast<EElysiumNpcCond>(0x7a));
	if (Weapon == nullptr)
	{
		if (bKatanaCondition)
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0x158"), GAnim10_2FileBach, 652));
			return 0x158;
		}
		if (Conds.Has(static_cast<EElysiumNpcCond>(0x79)))
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0x159"), GAnim10_2FileBach, 656));
			return 0x159;
		}
	}
	else if (!bKatanaCondition)
	{
		if (Conds.Has(static_cast<EElysiumNpcCond>(0x79)))
		{
			// Armed, rifle condition: the weapon must BE the rifle, or the body refuses with 0x159.
			// A match falls through to slot 605 `SelectScheduleRangedCombat` (vtable +0x974) and
			// returns ITS answer — the one arm of this body that leaves the melee family entirely.
			if (Weapon->Def == nullptr
				|| !Weapon->Def->Classname.Equals(GAnim10_2BachRifle, ESearchCase::IgnoreCase))
			{
				RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0x159"), GAnim10_2FileBach, 640));
				return 0x159;
			}
			return SelectScheduleRangedCombat(0);
		}
	}
	else
	{
		// Armed, katana condition: the weapon must BE the katana, or 0x158.
		if (Weapon->Def == nullptr
			|| !Weapon->Def->Classname.Equals(GAnim10_2BachKatana, ESearchCase::IgnoreCase))
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0x158"), GAnim10_2FileBach, 633));
			return 0x158;
		}
	}

	// The fall-through: `thunk_FUN_10385e40(this, param_1)` — the HUMAN body, called DIRECTLY and
	// not through the vtable, so a class that overrode slot 604 does not re-enter here.
	const int32 Answer = SelectScheduleMeleeCombatHuman();
	// `+0x6444` is cleared unless `m_NPCState` is 4 or 0xc. It is cleared on EVERY path out of the
	// human body, including the ones that answered non-zero.
	const int32 State = Anim10_2RetailNpcState(Mind.State());
	if (State != 4 && State != 0xc)
	{
		BachClearWord = 0;
	}
	if (Answer == 0)
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0x159"), GAnim10_2FileBach, 669));
		return 0x159;
	}
	return Answer;
}

// Slot 605: `0x103642f0`
/** `CNPC_VBach::SelectScheduleRangedCombat` (`0x103642f0`), 413 bytes — the one arm that CHAINS:
 *  its katana/rifle classname tests fall through to the human body, and its own tail then rewrites
 *  the answer. */
int32 FElysiumNpcBach::SelectScheduleRangedCombat(int32 Arg)
{
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	// `103642f6`: arm 1 — Bach's own COND `0x7b`. It stamps `m_flNextHolyLightTime` (`+0x6690`) with
	// `curtime + _DAT_10463584` (**15.0**), which closes the teleport arm of `0x10363db0` for 15
	// seconds, and answers 0x15a.
	if (Conds.Has(GBachCondReposition))
	{
		BachNextHolyLightTime = (World != nullptr ? World->NowSeconds() : 0.0) + NpcKernelCombat10_2Shared::GTauntTimerAdvance;
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0x15a"), GBachFile, 700));
		return 0x15a;
	}

	// `1036433d`: the active weapon is fetched ONCE and COND `0x7a` tested ONCE, before the split.
	const FElysiumEntity* const Weapon = ActiveWeaponEntity();
	const bool bMeleeCond = Conds.Has(GBachCondMelee);
	const FElysiumItem* const WeaponItem = Weapon != nullptr ? Weapon->AsItem() : nullptr;
	const FString WeaponClassname = WeaponItem != nullptr ? WeaponItem->ClassName() : FString();

	if (Weapon == nullptr)
	{
		// `10364355`: with NO weapon, COND `0x7a` → 0x158 and COND `0x79` → 0x159.
		if (bMeleeCond)
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0x158"), GBachFile, 0x2da));
			return 0x158;
		}
		if (Conds.Has(GBachCondRanged))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0x159"), GBachFile, 0x2de));
			return 0x159;
		}
	}
	else if (bMeleeCond)
	{
		// `1036439b`: WITH a weapon and COND `0x7a` — a classname that is not the katana answers
		// 0x158; the katana itself dispatches slot 604 and RETURNS its answer.
		if (!WeaponClassname.Equals(GBachKatana, ESearchCase::IgnoreCase))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0x158"), GBachFile, 0x2c8));
			return 0x158;
		}
		return SelectScheduleMeleeCombat(Arg);   // `vt+0x970` — slot 604
	}
	else if (Conds.Has(GBachCondRanged))
	{
		// `103643f7`: COND `0x79` — a classname that is not Bach's rifle answers 0x159. The matching
		// rifle falls THROUGH to the human body, which is the only path that reaches it with 0x79 up.
		if (!WeaponClassname.Equals(GBachRifle, ESearchCase::IgnoreCase))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0x159"), GBachFile, 0x2d3));
			return 0x159;
		}
	}

	// `10364447`: `CNPC_VHuman::SelectScheduleRangedCombat` `0x10386560`, a DIRECT non-virtual call,
	// expressed by naming the human line's body.
	const int32 HumanAnswer = HumanSelectScheduleRangedCombat(Arg);

	// `1036445a`: UNCONDITIONALLY — an `m_NPCState` (`+0x5cc0`) that is neither 4 nor 0xc clears the
	// cover hint `+0x6444`. This runs whatever the human body answered.
	const int32 RetailState = Combat10RangedRetailStateId(Mind.State());
	if (RetailState != 4 && RetailState != 0xc)
	{
		ScheduleHost.ShootAtHintNode = 0;
	}
	// `1036447a`: a human answer of 0 with `COND_SEE_ENEMY` up is REWRITTEN to 0x15f.
	if (HumanAnswer == 0 && Conds.Has(EElysiumNpcCond::SeeEnemy))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0x15f"), GBachFile, 0x2ed));
		return 0x15f;
	}
	return HumanAnswer;
}

// Slot 440: `0x10363a30`.
// `0x10363a30`
// `0x10363a30`, `CNPC_VBach::TranslateSchedule`, the body of `FElysiumNpcBach::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpcBach::TranslateScheduleRetail(int32 ScheduleNumber)
{
	if (ScheduleNumber == 0xb1) { return 0x15f; }
	if (ScheduleNumber == 0xdc) { return 0x15e; }
	if (ScheduleNumber == 0xef) { return 0x15f; }
	if (ScheduleNumber == 0x17 || ScheduleNumber == 0xed || ScheduleNumber == 0x15f)
	{
		if (!bBachMovementSpot)
		{
			return 0x15f;
		}
		// `10363a93`: the katana arm, four gates in retail's order. Each miss falls to
		// Troika.
		FElysiumItem* const Active = Inventory.Active(*this);
		FElysiumWeapon* const Weapon = Active != nullptr ? Active->AsWeapon() : nullptr;
		if (Weapon != nullptr && Weapon->Def != nullptr
			&& Weapon->Def->Classname.Equals(GTranslate19BachKatana,
				ESearchCase::IgnoreCase)
			// `10363ac2 TEST EAX,EAX / JZ`: sequence index **zero refuses**, unlike every
			// other `SelectWeightedSequence` probe in the port, which tests INDEX_NONE.
			&& SelectWeightedSequenceForActivity(0x10) != 0
			// `10363acf`: `STOP_BACKUP 0x2c` SET sends the arm to Troika.
			&& !Cognition.Conditions.Has(EElysiumNpcCond::StopBackup))
		{
			return 0x160;
		}
	}
	return TroikaTranslateScheduleRetail(ScheduleNumber);
}

// Slot 566: `0x10365800`, whose miss falls through into the Troika body `0x10295c20` directly.
bool FElysiumNpcBach::FValidateHintType(void* Hint)
{
	// `16999 < t && t < 0x426e` (17000..17005) accepts OUTRIGHT; every other type FALLS THROUGH
	// into the Troika body `0x10295c20`, called directly, rather than answering false.
	// Retail dereferences the hint unchecked; a hint the seam could not resolve reads type 0
	// here and falls through to the Troika body, whose null arm refuses it.
	const FHintWords* Words = static_cast<const FHintWords*>(Hint);
	const int32 HintType = Words != nullptr ? Words->HintType : 0;
	if (16999 < HintType && HintType < 0x426e)
	{
		return true;
	}
	return FElysiumNpc::FValidateHintType(Hint);
}

// Slot 546: `0x10362df0`, the class's own schedule id space.
const TCHAR* FElysiumNpcBach::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093a608`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VBach"), TEXT("0x10362df0"), TEXT("0x1093a608") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// Slot 561: `0x10363db0` — the shield, teleport and weapon-switch block, then
// `CAI_BaseNPC::GatherAttackConditions` (`0x1026dd10`) directly. The distance argument IS read by the
// Bach block, unlike the base, which takes the port's own committed enemy.
void FElysiumNpcBach::GatherAttackConditions(FElysiumEntity* Enemy, float DistanceUnits)
{
	BachGatherAttackConditions(DistanceUnits);
	FElysiumNpc::GatherAttackConditions(Enemy, DistanceUnits);
}

// --- Moved from `ElysiumNpcKernelAnim10_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcKernelCombat10_2.cpp` (story 5 step 4) ---

// --- `CNPC_VBach::SelectScheduleRangedCombat` `0x103642f0`, 413 bytes ----------------------------

// --- Moved from `ElysiumNpcKernelDamage2.cpp` (story 5 step 4) ---

void FElysiumNpcBach::ThrowGrenade(const FString& GrenadeTargetName, float Force)
{
	// 1. The cooldown, and it is the FIRST thing: `curtime - m_flLastGrenadeTime >= 5.0`
	//    (`_DAT_10454110`), inclusive at the edge.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (!(static_cast<double>(GrenadeCooldown) <= Now - BachLastGrenadeTime))
	{
		return;
	}
	// 2. The named target must exist. A missing one leaves the cooldown UNSTAMPED, so the next think
	//    tries again immediately.
	FElysiumEntity* Spot = World != nullptr ? World->FindByName(GrenadeTargetName) : nullptr;
	if (Spot == nullptr)
	{
		return;
	}
	// 3. The stamp happens before the create, so a failed create still spends the cooldown.
	BachLastGrenadeTime = Now;

	// 4. `CBaseEntity::Create("item_w_grenade_frag", target->GetAbsOrigin())`.
	const FElysiumEntityHandle Grenade =
		CreateNamedEntity(TEXT("item_w_grenade_frag"), Spot->Origin / ElysiumMove::U);
	if (!Grenade.IsSet())
	{
		return;   // retail's own `if (this_00 != 0)` guard
	}

	// 5. `m_takedamage = 2` (DAMAGE_YES), `m_iHealth = 1`, `m_pfnTouch = null` — a grenade that a
	//    single point of damage detonates and that nothing can trigger by touch. Then
	//    `VPhysicsInitNormal(2, 0, false)`; a failure `Msg`es "No physics data for grenade" and
	//    removes it.
	// 6. `forward * force` as the velocity with a zero angular velocity, then
	//    `m_flNextThink = curtime + 3.0 + 0.01` (`_DAT_10449258` and `_DAT_10450aa4`) written into
	//    both the grenade's own `+0x7c` word and its think field.
	(void)Force;
	(void)GrenadeThinkDelay;
	(void)GrenadeThinkSlack;
	// 7. `m_bCamperFlag = 0` — the LAST thing, and it is on the THROWER, not the grenade.
	bBachCamperFlag = false;
}

// --- Moved from `ElysiumNpcKernelLifecycle19_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcKernelMisc.cpp` (story 5 step 4) ---

int32 FElysiumNpcBach::BachRangeAttack1Conditions(float Dot, float DistUnits) const
{
	// `CNPC_VBach::vfunc553` `0x10364500`, slot 553 `RangeAttack1Conditions(flDot, flDist)`:
	//     if (flDot >= (float)_DAT_10449270) return 0x4f;          // 0.5, a DOUBLE in .rdata
	//     if (flDist <= _DAT_1044f00c) return 0x4f;                // 120.0f
	//     return 0x61;
	//
	// **It is an OR, not an AND**, and that is the shipped oddity: Bach can range-attack whenever he
	// is roughly facing the target OR the target is inside 120 units, where the ordinary shape would
	// require both. `0x4f` is `COND_CAN_RANGE_ATTACK1` and `0x61` is `COND_NOT_FACING_ATTACK`; note
	// that a target that is merely too far answers "not facing", which is the wrong refusal and is
	// retail's.
	//
	// The listing settles both polarities: `FCOMP double [0x10449270]` with `TEST AH,0x5 / JP` is
	// `>=` (equal takes the accept), and `AND EAX,0x4100 / JZ` on the second is `>` (equal takes the
	// accept there too).
	return (static_cast<double>(Dot) >= GMiscHalfDouble || DistUnits <= GMiscYawHigh)
		? GMiscBachRange1Answer : GMiscBachRefusal;
}

int32 FElysiumNpcBach::BachRangeAttack2Conditions(float Dot, float DistUnits) const
{
	// `CNPC_VBach::vfunc554` `0x10364550`, slot 554 `RangeAttack2Conditions(flDot, flDist)`. The
	// SAME two thresholds in the same order; only the accepted answer differs — `0x50`
	// (`COND_CAN_RANGE_ATTACK2`) instead of `0x4f`. One behaviour written twice, which is why the
	// gate is not restated.
	return BachRangeAttack1Conditions(Dot, DistUnits) == GMiscBachRange1Answer
		? GMiscBachRange2Answer : GMiscBachRefusal;
}

// --- Moved from `ElysiumNpcKernelPrecache10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcKernelSpecies.cpp` (story 5 step 4) ---

// -------------------------------------------------------------------------------------------------
// Slot 606 — `CNPC_VBach::FUN_10364280` `0x10364280`.
// -------------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------------
// Slot 609 — `CNPC_VBach`'s state gate (byte-identical to the dead swarms' `0x10367740` and
// `0x103b26f0`).
// -------------------------------------------------------------------------------------------------

bool FElysiumNpcBach::FUN_103661f0(bool bArg)
{
	// `0x103661f0`, forty-four bytes:
	//
	//     if (m_NPCState != 4 && m_NPCState != 0xc) { m_pShootAtHintNode = 0; return NULL; }
	//     return Base609(this, param_1);                          // thunk 0x102b6b50
	//
	// A pure STATE GATE in front of the Troika hint search: only a scripted body (4) or a body in
	// retail state 0xc may look for a shoot-at hint, and any other state has its cached hint
	// CLEARED as a side effect of asking. `EElysiumNpcState` carries no member for retail 0xc, so
	// that half of the test can never pass here and is written as the raw number with this note —
	// exactly what family Conditions did for retail state 8.
	//
	// `bArg` is the base body's own argument and is not read by the gate.
	(void)bArg;
	const int32 State = SpeciesRetailNpcState(Mind.State());
	if (State != ShootAtHintStateScript && State != ShootAtHintStateTwelve)
	{
		// `m_pShootAtHintNode = 0` (+0x6444). Zero, not `INDEX_NONE`: retail writes a NULL POINTER
		// and this runtime carries the word as a node index whose "none" is 0 — family
		// TroikaHelpers' `FindShootAtHintNode` reads it back with `!= 0` on the same convention.
		ScheduleHost.ShootAtHintNode = 0;
		return false;
	}
	return true;
}

// --- Moved from `ElysiumNpcKernelSpeciesMisc10_2.cpp` (story 5 step 4) ---

void FElysiumNpcBach::BachGatherAttackConditions(float DistanceUnits)
{
	FElysiumNpcConditions& Conds = Cognition.Conditions;
	// `10363dc6`: the WHOLE shield/teleport block is gated on `HasCondition(0x4c LIGHT_DAMAGE)` or
	// `HasCondition(0x4d HEAVY_DAMAGE)`.
	const bool bDamaged = Conds.Has(static_cast<EElysiumNpcCond>(0x4c))
		|| Conds.Has(static_cast<EElysiumNpcCond>(0x4d));
	if (bDamaged)
	{
		// `10363de3`: `m_bCondTookDamage` (+0x5b80) is cleared FIRST, inside the block.
		Cognition.bCondTookDamage = false;
		const double Now = NpcKernelSpeciesMisc10_2Shared::SpeciesMisc10_2Now(*this);
		// `10363de3`/`10363df5`: the distance at or above `DAT_1062d200[m_iBachTeleportState]`
		// (**768, 768, 384, 384**) takes the SHIELD arm (`TEST AH,0x5 / JP`). Otherwise
		// `10363dfa FCOMP [ESI+0x6690] / AND 0x4100 / JNZ` also takes it while `curtime` is at or
		// before `m_flNextHolyLightTime`: only a CLOSE Bach whose holy-light stamp has PASSED
		// teleports. (Story 5 step 4r: the port had this compare inverted, so a Spawn-zeroed stamp
		// always shielded and a fresh COND `0x7b` stamp opened the teleport instead of closing it.)
		const int32 StateIndex = FMath::Clamp(BachTeleportState, 0, 3);
		const bool bShieldArm = DistanceUnits >= BachTeleportDistanceUnits[StateIndex]
			|| Now <= BachNextHolyLightTime;
		if (bShieldArm)
		{
			// `10363e1a`: the shield only fires when `m_flNextShieldTime` (+0x6688) is past.
			if (BachNextShieldTime < Now)
			{
				// `10363e96`: `CVStatList_t::SetBase(stat 0xd, 5)` on the type-3 (scripted) list,
				// found by the `+0x13bc`/`+0x13c0` walk for tag `+0x10 == 3` and falling back to the
				// lazily built global. Family Combat10's typed-stat seam is that walk.
				TypedStatSet(/*ListType*/ 3, /*StatId*/ 0xd, 5);
				// `10363eb3`: `m_flNextShieldTime = curtime + _DAT_1044eb0c` (**20.0**).
				BachNextShieldTime = Now + 20.0;
				// `10363ecb`: `m_bShieldActive = 1` and `m_flShieldTime = curtime + _DAT_1046bac0`
				// (**6.0**).
				bBachShieldActive = true;
				BachShieldTime = Now + 6.0;
				// `10363f3a`: the shield sound, through a `CPASAttenuationFilter` built from slot
				// 222 at attenuation 0.8, on channel 2 at volume 1.0 and pitch 100.
				EmitNamedWav(this, /*Channel*/ 2, TEXT("Character/Boss/Bach/bach_shield.wav"),
					/*Volume*/ 1.f, /*Attenuation*/ 0.8f, /*Pitch*/ 100);
			}
		}
		else
		{
			// `10363e09`: the cvar touch, then condition `0x7b` (teleport, `10363e14`).
			Conds.Set(static_cast<EElysiumNpcCond>(0x7b));
		}
		// `10363f81`: `+0x66a6` is cleared on EITHER branch, and inside the damage gate.
		bBachShieldFlagB = false;
	}
	// `10363f87`: INDEPENDENTLY of all of the above — when `m_flNextWeaponSwitchTime` (+0x668c) is
	// past, condition `0x79` at or above `_DAT_104704d0` (**72.0**) and `0x7a` below it.
	if (BachNextWeaponSwitchTime < NpcKernelSpeciesMisc10_2Shared::SpeciesMisc10_2Now(*this))
	{
		Conds.Set(static_cast<EElysiumNpcCond>(DistanceUnits >= 72.f ? 0x79 : 0x7a));
	}
	// `10363fc5`: `CAI_BaseNPC::GatherAttackConditions` runs LAST and unmodified — the caller does
	// that, so nothing else happens here.
}

// --- Moved from `ElysiumNpcKernelState19.cpp` (story 5 step 4) ---

bool FElysiumNpcBach::BachOnStateChange(int32 OldRetail, int32 NewRetail)
{
	if (!bCanFightYet && (NewRetail == 2 || NewRetail == 3))
	{
		SetState(OldRetail);
		return true;
	}
	return false;
}

// --- Moved from `ElysiumNpcKernelTranslate19.cpp` (story 5 step 4) ---

