#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSchedule.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 29d, families **Anim10** and **SpeciesAnim10**, part two: the three melee selectors of slot
// 604 and the zombie idle gate of slot 509. `ElysiumNpcKernelAnim10.cpp` carries activity,
// sequence, pose and model. `CAI_BaseHumanoid::MaintainEyeDirection` (`0x1025fa50`) was deleted by
// 0019 story 5 step 1: the class has no instance (`population.md`); its census row remains.

namespace
{
	// Unit-prefixed: adaptive unity merges anonymous namespaces.

	// --- The melee selectors' `.rdata` and schedule numbers ----------------------------------------
	//
	// `DAT_10924a1c` is the melee-range ConVar every selector thresholds on,
	// `debug_melee_advance_combatmove_dist` "100". `MeleeRangeUnits()` (family TroikaHelpers) reads
	// it and is called here so the six bodies cannot drift.
	constexpr float GAnim10_2FarMargin = 200.0f;       // _DAT_104492b8
	constexpr float GAnim10_2HeightBand = ElysiumNpcTunables::SixtyFour;
	constexpr double GAnim10_2TimerUnarmed = -1.0;     // 0xbf800000
	constexpr float GAnim10_2RetryMin = 3.0f;          // RandomFloat(3.0, 4.0)
	constexpr float GAnim10_2RetryMax = 4.0f;
	constexpr int32 GAnim10_2RollFloor = 0x18;         // `CMP EAX,0x19; JL` — the roll must EXCEED 24

	// The two source-file strings the selector trace stamps into `+0x1b30`.
	constexpr TCHAR GAnim10_2FileHuman[] = TEXT("NPC_VHuman.cpp");        // 0x1063f724
	constexpr TCHAR GAnim10_2FileMingXiao[] = TEXT("NPC_VMingXiao.cpp");  // 0x10647090
	constexpr TCHAR GAnim10_2FileBach[] = TEXT("NPC_VBach.cpp");          // 0x1062eadc

	// Bach's two authored weapon classnames.
	constexpr TCHAR GAnim10_2BachRifle[] = TEXT("item_w_rem_m_700_bach");  // 0x105c1c70
	constexpr TCHAR GAnim10_2BachKatana[] = TEXT("item_w_katana");         // 0x10587668
	constexpr float GAnim10_2BachFailDelay = ElysiumNpcTunables::Fifteen;

	// `CNPC_VZombie::vfunc509`'s two weights and the schedule id that swaps them.
	constexpr int32 GAnim10_2ZombieIdleWeight = 999;
	constexpr int32 GAnim10_2ZombieComfortWeight = 0x14;
	constexpr int32 GAnim10_2ScheduleComfort = 0x12f;   // SCHED_TROIKA_COMFORT

	FRandomStream& Anim10_2Rng()
	{
		return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	}

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

	// The height-difference retry stamp all six slot-604 bodies run, byte for byte:
	//
	//   armed = false;
	//   if (m_flEnemyHeightDiff <= 64.0)              m_flMeleeHeightDiffTimer = -1.0f;
	//   else if (m_flMeleeHeightDiffTimer == -1.0f)   m_flMeleeHeightDiffTimer = curtime +
	//                                                     RandomFloat(3.0, 4.0);
	//   else if (m_flMeleeHeightDiffTimer <= curtime) armed = true;
	//
	// Family Schedule spells the identical helper for the five bodies it ported; it is a file-static
	// there, so the six lines are repeated here with the same citation rather than a second reading
	// being made. The `<=` on the first arm and on the expiry are both read at `10386117`
	// (`AND EAX,0x4100; JNZ`) and `10386161` (`AND EAX,0x100; JNZ`).
	bool Anim10_2TickHeightDiffTimer(FElysiumNpc& Npc, double Now)
	{
		if (Npc.ScheduleHost.EnemyHeightDiffUnits <= GAnim10_2HeightBand)
		{
			Npc.MeleeHeightDiffTimer = GAnim10_2TimerUnarmed;
			return false;
		}
		if (Npc.MeleeHeightDiffTimer == GAnim10_2TimerUnarmed)
		{
			Npc.MeleeHeightDiffTimer =
				Now + Anim10_2Rng().FRandRange(GAnim10_2RetryMin, GAnim10_2RetryMax);
			return false;
		}
		return Npc.MeleeHeightDiffTimer <= Now;
	}

	// `(**(code **)(*(int *)this + 0x4d0))()` — slot 308 `HasUsableRangedWeapon`, the split every
	// arm of every melee selector turns on. The generated slot is a stub answering false; family
	// Schedule reads the port's own catalogue answer instead and names slot 308 beside it, and so
	// does this.
	bool Anim10_2HasRangedWeapon(const FElysiumNpc& Npc)
	{
		return ElysiumNpcCond::WeaponCapability(Npc) == ElysiumNpcCond::ECapability::Ranged;
	}
}

// =================================================================================================
// Family SpeciesAnim10 — slot 604's three species arms.
// =================================================================================================

int32 FElysiumNpc::SelectScheduleMeleeCombatHuman()
{
	// `CNPC_VHuman::SelectScheduleMeleeCombat` `0x10385e40`, 1,449 bytes, slot 604 for 34 census
	// classes. It REPLACES the Troika body `0x102b6c30` wholesale and never chains it.
	//
	// Every one of the float compares below was decoded off the LISTING, because the decompiler
	// renders three of them as `(a < b) != (a == b)` and `(a < b) == (a == b)`, which are `a <= b`
	// and `a > b` and are easy to read the wrong way round. The instruction is cited at each.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	FElysiumEntity* Enemy = GetEnemy();   // vtable +0x29c, fetched ONCE and reused
	const float Range = MeleeRangeUnits();              // DAT_10924a1c
	const float Distance = ScheduleHost.EnemyDistUnits; // +0x6268 m_flEnemyDist
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	if (!bInMelee)   // +0x6078 m_bInMelee
	{
		if (!Slot599(0))   // vtable +0x95c — "should I enter melee"
		{
			// `10385f25`: the melee failure gate `0x102b6fe0` is offered FIRST and any non-zero
			// answer returns.
			const int32 Gate = MeleeScheduleFailureGate(Enemy);
			if (Gate != 0)
			{
				return Gate;
			}
			// `10385f5b TEST AH,0x5; JP` — the roll is reached when `range + 200 >= distance`, i.e.
			// the far arm needs `range + 200 < distance` STRICTLY.
			if (Range + GAnim10_2FarMargin < Distance)
			{
				RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe7"), GAnim10_2FileHuman, 1563));
				return 0xe7;
			}
			// `10385f99 CMP EAX,0x19; JL` — the roll must EXCEED 24, and `10385fc6 AND EAX,0x4100;
			// JZ` — the second distance test passes on `range <= distance`.
			if (Anim10_2Rng().RandRange(0, 99) > GAnim10_2RollFloor && Range <= Distance)
			{
				RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe5"), GAnim10_2FileHuman, 1575));
				return 0xe5;
			}
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe4"), GAnim10_2FileHuman, 1571));
			return 0xe4;
		}
	}
	else if (Slot602())   // vtable +0x968 — "should I leave melee"
	{
		Slot601(Enemy);   // vtable +0x964
		if (Anim10_2HasRangedWeapon(*this))   // vtable +0x4d0, slot 308
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe9"), GAnim10_2FileHuman, 1535));
			return 0xe9;
		}
		// `10385ee1 TEST AH,0x41; JP` — `2 * range <= distance` takes the far arm. **AT OR beyond**
		// twice the range, not strictly beyond; the checklist's walk said "exceeds".
		if (Range + Range <= Distance)
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe7"), GAnim10_2FileHuman, 1541));
			return 0xe7;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe4"), GAnim10_2FileHuman, 1545));
		return 0xe4;
	}

	// --- The common tail (10386011) -----------------------------------------------------------------
	//
	// TWO offers, `0x102b7370` then `0x102b6fe0`, and either non-zero answer returns. The first has
	// no port body of its own; family Schedule left it as the entrenched-cover helper's sibling and
	// it is reached here through `ScheduleEntrenchedCoverOffer`, which answers 0.
	// `thunk_FUN_102b7370(this)` — `SelectDoorObstructionSchedule`, which `FElysiumNpc` already
	// carries (`ElysiumNpc.cpp`) as an `int32`; converted back to retail's number
	// because retail's `if (answer != 0) return answer` is over the raw one.
	const int32 DoorOffer =
		SelectDoorObstructionSchedule();
	if (DoorOffer != 0)
	{
		return DoorOffer;
	}
	const int32 Gate = MeleeScheduleFailureGate(Enemy);
	if (Gate != 0)
	{
		return Gate;
	}

	// The condition ladder, in strict retail order. The first two go through `0x10269d30`
	// (`HasInterruptCondition`) and the rest through `0x10269aa0` (`HasCondition`) — two different
	// questions, and the split is retail's.
	if (ElysiumSchedule::HasInterruptCondition(Schedule, *this, Conds,
			EElysiumNpcCond::ShouldDodge))
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd5"), GAnim10_2FileHuman, 1597));
		return 0xd5;
	}
	if (ElysiumSchedule::HasInterruptCondition(Schedule, *this, Conds,
			EElysiumNpcCond::ShouldBlock))
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd6"), GAnim10_2FileHuman, 1601));
		return 0xd6;
	}
	// BOTH conditions are read before either is tested (`1038608e` / `10386097`), so the second read
	// happens even when the first is set.
	const bool bKick = Conds.Has(EElysiumNpcCond::ShouldKick);
	const bool bStepback = Conds.Has(EElysiumNpcCond::ShouldStepback);
	if (bKick)
	{
		// `1038638f`: kick alone answers 0xdb; kick AND stepback flips a coin and answers 0xdb on a
		// 1 and 0xd3 on anything else.
		if (!bStepback || Anim10_2Rng().RandRange(0, 1) == 1)
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xdb"), GAnim10_2FileHuman, 1611));
			return 0xdb;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd3"), GAnim10_2FileHuman, 1615));
		return 0xd3;
	}
	if (bStepback)
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd3"), GAnim10_2FileHuman, 1615));
		return 0xd3;
	}
	if (Conds.Has(EElysiumNpcCond::CanMeleeAttack1))
	{
		if (Anim10_2HasRangedWeapon(*this))
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xdc"), GAnim10_2FileHuman, 1625));
			return 0xdc;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xdd"), GAnim10_2FileHuman, 1629));
		return 0xdd;
	}

	// The height-difference retry stamp. It is TICKED here whatever the arms below decide, which is
	// why it is not folded into the test that reads it.
	const bool bRetryExpired = Anim10_2TickHeightDiffTimer(*this, Now);

	if (Anim10_2HasRangedWeapon(*this)
		&& (Conds.Has(EElysiumNpcCond::TooFarForMelee) || Conds.Has(EElysiumNpcCond::InterruptTime)
			|| Conds.Has(EElysiumNpcCond::EnemyUnreachable) || bRetryExpired))
	{
		Slot601(Enemy);
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe9"), GAnim10_2FileHuman, 1663));
		return 0xe9;
	}
	if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
	{
		Slot601(Enemy);
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0x17"), GAnim10_2FileHuman, 1671));
		return 0x17;
	}
	if (!Conds.Has(EElysiumNpcCond::TooFarForMelee) && !Conds.Has(EElysiumNpcCond::TooFarToAttack))
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 199"), GAnim10_2FileHuman, 1729));
		return 199;
	}
	if (Enemy != nullptr)
	{
		// `10386254`: the enemy's `WorldSpaceCenter` (slot 192) is read into a stack vector and
		// DISCARDED, then `0x102a11d0` decides.
		(void)ElysiumCameraShots::SurroundingBounds(*Enemy).GetCenter();   // slot 192
		if (ScheduleMeleeReachGate())
		{
			if (Anim10_2HasRangedWeapon(*this))
			{
				RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe0"), GAnim10_2FileHuman, 1688));
				return 0xe0;
			}
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe1"), GAnim10_2FileHuman, 1692));
			return 0xe1;
		}
	}
	FScheduleHintSearchRequest CoverRequest;
	CoverRequest.bRequest2 = true;
	CoverRequest.bRequest4 = true;   // `thunk_FUN_102b7690(this, 0, 1, 0, 1)`
	const int32 CoverOffer = SelectCoverOrKickSchedule(CoverRequest);
	if (CoverOffer != 0)
	{
		return CoverOffer;
	}
	// `102862f6 TEST AH,0x41; JNP` — the near pair needs `distance < range` STRICTLY (the checklist's
	// walk said "at or below") and the retry stamp NOT expired.
	if (Distance < Range && !bRetryExpired)
	{
		if (Anim10_2HasRangedWeapon(*this))
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd1"), GAnim10_2FileHuman, 1719));
			return 0xd1;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd2"), GAnim10_2FileHuman, 1723));
		return 0xd2;
	}
	if (Anim10_2HasRangedWeapon(*this))
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xca"), GAnim10_2FileHuman, 1708));
		return 0xca;
	}
	RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xcb"), GAnim10_2FileHuman, 1712));
	return 0xcb;
}

int32 FElysiumNpc::SelectScheduleMeleeCombatMingXiao()
{
	// `CNPC_VMingXiao::SelectScheduleMeleeCombat` `0x10396050`, 1,522 bytes. The same skeleton as
	// `0x10385e40` with FOUR stated differences, each marked below.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const float Range = MeleeRangeUnits();
	const float Distance = ScheduleHost.EnemyDistUnits;
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	if (!bInMelee)
	{
		// **DIFFERENCE 1**: MingXiao re-fetches `GetEnemy()` at every use rather than caching it, and
		// its not-engaged arm does NOT offer the melee failure gate `0x102b6fe0` at all.
		if (!Slot599(0))
		{
			// **DIFFERENCE 2**: the distance is tested BEFORE the roll, where the human body offers
			// the gate first. `10396095` is the same `range + 200 < distance` strict compare.
			if (Range + GAnim10_2FarMargin < Distance)
			{
				RecordScheduleEvent(
					FString::Printf(TEXT("%s:%d -> 0xe7"), GAnim10_2FileMingXiao, 2644));
				return 0xe7;
			}
			if (Anim10_2Rng().RandRange(0, 99) > GAnim10_2RollFloor && Range <= Distance)
			{
				RecordScheduleEvent(
					FString::Printf(TEXT("%s:%d -> 0xe5"), GAnim10_2FileMingXiao, 2656));
				return 0xe5;
			}
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe4"), GAnim10_2FileMingXiao, 2652));
			return 0xe4;
		}
	}
	else if (Slot602())
	{
		Slot601(GetEnemy());
		if (Anim10_2HasRangedWeapon(*this))
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe9"), GAnim10_2FileMingXiao, 2623));
			return 0xe9;
		}
		if (Range + Range <= Distance)
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe7"), GAnim10_2FileMingXiao, 2629));
			return 0xe7;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe4"), GAnim10_2FileMingXiao, 2633));
		return 0xe4;
	}

	// **DIFFERENCE 3**: the common tail offers ONLY `0x102b7370`; there is no second
	// `0x102b6fe0` offer.
	// `thunk_FUN_102b7370(this)` — `SelectDoorObstructionSchedule`, which `FElysiumNpc` already
	// carries (`ElysiumNpc.cpp`) as an `int32`; converted back to retail's number
	// because retail's `if (answer != 0) return answer` is over the raw one.
	const int32 DoorOffer =
		SelectDoorObstructionSchedule();
	if (DoorOffer != 0)
	{
		return DoorOffer;
	}

	// **DIFFERENCE 4a**: an extra arm the human body lacks, and it OPENS the ladder — `COND 0x48
	// ENEMY_OCCLUDED`, read through `HasCondition` and not through the interrupt form.
	if (Conds.Has(EElysiumNpcCond::EnemyOccluded))
	{
		if (Anim10_2HasRangedWeapon(*this))
		{
			Slot601(GetEnemy());
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe9"), GAnim10_2FileMingXiao, 2675));
			return 0xe9;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xcd"), GAnim10_2FileMingXiao, 2685));
		return 0xcd;
	}
	if (ElysiumSchedule::HasInterruptCondition(Schedule, *this, Conds,
			EElysiumNpcCond::ShouldDodge))
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd5"), GAnim10_2FileMingXiao, 2692));
		return 0xd5;
	}
	// **DIFFERENCE 4b**: there is NO `COND 0x0d SHOULD_BLOCK` arm at all.
	const bool bKick = Conds.Has(EElysiumNpcCond::ShouldKick);
	const bool bStepback = Conds.Has(EElysiumNpcCond::ShouldStepback);
	if (bKick)
	{
		if (!bStepback || Anim10_2Rng().RandRange(0, 1) == 1)
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xdb"), GAnim10_2FileMingXiao, 2702));
			return 0xdb;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd3"), GAnim10_2FileMingXiao, 2706));
		return 0xd3;
	}
	if (bStepback)
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd3"), GAnim10_2FileMingXiao, 2706));
		return 0xd3;
	}
	if (Conds.Has(EElysiumNpcCond::CanMeleeAttack1))
	{
		if (Anim10_2HasRangedWeapon(*this))
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xdc"), GAnim10_2FileMingXiao, 2716));
			return 0xdc;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xdd"), GAnim10_2FileMingXiao, 2720));
		return 0xdd;
	}

	const bool bRetryExpired = Anim10_2TickHeightDiffTimer(*this, Now);
	if (Anim10_2HasRangedWeapon(*this)
		&& (Conds.Has(EElysiumNpcCond::TooFarForMelee) || Conds.Has(EElysiumNpcCond::InterruptTime)
			|| Conds.Has(EElysiumNpcCond::EnemyUnreachable) || bRetryExpired))
	{
		Slot601(GetEnemy());
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe9"), GAnim10_2FileMingXiao, 2754));
		return 0xe9;
	}
	if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
	{
		Slot601(GetEnemy());
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0x17"), GAnim10_2FileMingXiao, 2762));
		return 0x17;
	}
	if (!Conds.Has(EElysiumNpcCond::TooFarForMelee) && !Conds.Has(EElysiumNpcCond::TooFarToAttack))
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 199"), GAnim10_2FileMingXiao, 2820));
		return 199;
	}
	FElysiumEntity* Enemy = GetEnemy();
	if (Enemy != nullptr)
	{
		(void)ElysiumCameraShots::SurroundingBounds(*Enemy).GetCenter();   // slot 192
		if (ScheduleMeleeReachGate())
		{
			if (Anim10_2HasRangedWeapon(*this))
			{
				RecordScheduleEvent(
					FString::Printf(TEXT("%s:%d -> 0xe0"), GAnim10_2FileMingXiao, 2779));
				return 0xe0;
			}
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe1"), GAnim10_2FileMingXiao, 2783));
			return 0xe1;
		}
	}
	FScheduleHintSearchRequest CoverRequest;
	CoverRequest.bRequest2 = true;
	CoverRequest.bRequest4 = true;   // `thunk_FUN_102b7690(this, 0, 1, 0, 1)`
	const int32 CoverOffer = SelectCoverOrKickSchedule(CoverRequest);
	if (CoverOffer != 0)
	{
		return CoverOffer;
	}
	if (Distance < Range && !bRetryExpired)
	{
		if (Anim10_2HasRangedWeapon(*this))
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd1"), GAnim10_2FileMingXiao, 2810));
			return 0xd1;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd2"), GAnim10_2FileMingXiao, 2814));
		return 0xd2;
	}
	if (Anim10_2HasRangedWeapon(*this))
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xca"), GAnim10_2FileMingXiao, 2799));
		return 0xca;
	}
	RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xcb"), GAnim10_2FileMingXiao, 2803));
	return 0xcb;
}

int32 FElysiumNpc::SelectScheduleMeleeCombatBach()
{
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
		BachFailStamp = Now + GAnim10_2BachFailDelay;   // curtime + _DAT_10463584 (15.0f)
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

// =================================================================================================
// Family SpeciesAnim10 — slot 509's zombie arm.
// =================================================================================================

bool FElysiumNpc::ShouldPlayIdleSoundZombieArm() const
{
	// `CNPC_VZombie#509` is `0x103e0fa0` and is the ONLY override of slot 509 in the census; every
	// other class inherits the Troika body `0x10294040`. Keyed on the row's retail ADDRESS, the
	// convention family Precache10 set.
	const FElysiumNpcClassSlot* Override = ElysiumNpcKernelClass::OverrideOf(RetailClass(), 509);
	return Override != nullptr
		&& FCString::Strcmp(Override->Address, TEXT("0x103e0fa0")) == 0;
}

bool FElysiumNpc::ShouldPlayIdleSoundZombie()
{
	// `CNPC_VZombie::vfunc509` `0x103e0fa0`, 166 bytes, `CNPC_VZombie#509` only. It REPLACES the
	// Troika body `0x10294040` wholesale: there is no `IsInDialog` refusal, no `m_NPCState` test and
	// no `SF_NPC_GAG` test in it at all, which is why a zombie vocalises in states where a human
	// would not.
	//
	// Every refusal returns false through the same `return (uint)piVar3 & 0xffffff00` low-byte clear.

	// 1. `m_bIsBCCTargetable` clear -> false. SEAM: family Sounds10 recorded that this byte
	//    (`+0x7ec` on `CBaseCombatCharacter`) has no port member, so the arm is not tested; stated
	//    here rather than silently dropped.

	// 2. A LIVE `m_hDialogPartner` (+0x0fe8) -> false. The port stands for it with "this character
	//    owns the open dialogue session", the same reading family Sounds10 made.
	if (World != nullptr)
	{
		const FElysiumEntityHandle DialogOwner = World->GetOpenDialogOwner();
		if (DialogOwner.IsSet() && DialogOwner.Index == Handle.Index)
		{
			return false;
		}
	}

	// 3. `IsBusyWithDiscipline()` -> false.
	if (IsBusyWithDiscipline())
	{
		return false;
	}

	// 4. The weight. 999 by default; a RUNNING schedule whose local id (slot 447
	//    `GetLocalScheduleId`) is `0x12f SCHED_TROIKA_COMFORT` drops it to 20 — a 1-in-21 roll — AND
	//    skips the float-sound arm entirely. `GetLocalScheduleId` answers -1 for every id today
	//    (family Sounds10's note: no schedule text is parsed), so the comfort branch is unreachable
	//    until story 10i registers the schedule.
	int32 Weight = GAnim10_2ZombieIdleWeight;
	bool bComforting = false;
	if (Schedule.IsRunning())
	{
		if (GetLocalScheduleId(Schedule.Current) == GAnim10_2ScheduleComfort)
		{
			Weight = GAnim10_2ZombieComfortWeight;
			bComforting = true;
		}
	}

	// 5. Otherwise slot 510 `ShouldPlayFloatSound` decides: true plays slot 507 `FloatSound` and
	//    returns FALSE. The float sound is played INSTEAD of an idle sound, not beside it.
	if (!bComforting)
	{
		if (ShouldPlayFloatSound())
		{
			FloatSound();
			return false;
		}
	}

	// 6. True only when the roll is EXACTLY 0.
	return Anim10_2Rng().RandRange(0, Weight) == 0;
}
