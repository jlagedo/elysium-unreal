#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcCombat10_2Shared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 29d, family **Combat10** — slot 605 `SelectScheduleRangedCombat`, its five species arms and
// the three helpers they offer. The rest of the family is in `ElysiumNpcCombat10.cpp`; the
// declarations and this family's standing facts are in `ElysiumNpcCombat10.inl`.
//
// **A combat-schedule selector is an ORDERED body.** The arms below are in retail's order and the
// first that answers wins; the order is the behaviour, not a set of conditions. Every arm carries
// the address of the instruction that decides it, and every return carries the `.cpp` line retail
// stamps into `+0x1b30`/`+0x1b34` — which the shape map records ABSENT and `RecordScheduleEvent`
// stands in for.
//
// Every one of these bodies answers a RAW RETAIL SCHEDULE NUMBER, which is family Schedule's
// standing fact two: most of what they name is not a registered program, so the recovered number is
// the deliverable and the caller maps it.

namespace
{

	// `_DAT_104492b8` — 200.0, the margin `0x102b8620` adds to the melee-range convar.
	constexpr float GRangedSpacingMargin = 200.0f;

	constexpr int32 GSpacingRollThreshold = 0x18;    // 24 — `0x102b8620`'s `0xe5` gate (`> 0x18`)
	constexpr int32 GTauntRollThreshold = 0x45;      // 69 — `0x102b7cf0`'s `0x8f` gate (`> 0x45`)
	constexpr int32 GCoverRollThreshold = 0x3b;      // 59 — `0x102b7cf0`'s `0x8d` gate (`> 0x3b`)

	// `m_bfNPCFrenziedFlags` (`+0x5b84`) bit `0x2000`. UNNAMED in retail — no name table covers this
	// word (`ElysiumNpcFlags.h`) — and both values `DoPossession` (`0x3b1c`) and `DoFrenzy`
	// (`0x9fbd`) carry it, so the gate reads "this body is under one of the two AI disciplines".
	constexpr uint32 GFrenziedDisciplineBit = 0x2000;

	// The `.cpp` names retail stamps.
	const TCHAR* const GTroikaFile = TEXT("AI_BaseNPCTroika.cpp");

	bool HasUsableRangedWeaponPort(const FElysiumNpc& Npc)
	{
		return ElysiumNpcCond::WeaponCapability(Npc) == ElysiumNpcCond::ECapability::Ranged;
	}

}

// =================================================================================================
// `FUN_102b7f40` — the dodge test, 93 bytes.
// =================================================================================================

bool FElysiumNpc::ShouldDodgeRangedAttack()
{
	// `102b7f46`: `SelectWeightedSequence(ACT 0x10, -1)`. **Retail tests `!= 0`, not `!= -1`** —
	// so the "no such sequence" answer `-1` PASSES this gate. Family Facing's seam answers `-1`,
	// which is therefore the admitting arm and the body below is reachable.
	if (SelectWeightedSequenceForActivity(NpcKernelCombat10_2Shared::GDodgeActivity) == 0)
	{
		return false;
	}
	// `102b7f5f`: `!COND_STOP_BACKUP (0x2c)` and a roll under 75.
	if (!Cognition.Conditions.Has(EElysiumNpcCond::StopBackup) && NpcKernelCombat10_2Shared::RangedRoll() < NpcKernelCombat10_2Shared::GDodgeRollThreshold)
	{
		return true;
	}
	// `102b7f85`: the discipline test — reached even when `COND 0x2c` stands.
	if (RangedDisciplineGate(this))
	{
		return true;
	}
	// `102b7f96`: `COND_WEAPON_THROUGH_WALL (0x3c)`.
	if (Cognition.Conditions.Has(EElysiumNpcCond::WeaponThroughWall))
	{
		return true;
	}
	// `102b7fa6`: the tail clears the low byte — false.
	return false;
}

bool FElysiumNpc::RangedDisciplineGate(const FElysiumEntity* /*Subject*/) const
{
	// SEAM for `0x101e3f50(&DAT_10739a4c, entity)`. `DAT_10739a4c` is an unnamed discipline record
	// and no port discipline is bound to it, so this answers false — which ADMITS the slot-606
	// branch of the Troika base and the human arm rather than skipping it, and which makes this arm
	// of the dodge test decline rather than accept.
	return false;
}

bool FElysiumNpc::RangedGateConVarEnabled(ElysiumNpcTunables::EConVar ConVar)
{
	// Retail runs the block only when the object's vtable `+0x04` bool is CLEAR and its `+0x2c` int
	// is non-zero; the object is a ConVar, so the int decides.
	return ElysiumNpcTunables::ConVarInt(ConVar) != 0;
}

// =================================================================================================
// `FUN_102b8620` — the ranged weapon pre-pass, 688 bytes.
// =================================================================================================

// `ActiveWeaponCapabilityWord` — the weapon vtable `+0x5a0` read, tested against `0x6000` — is
// family **Motor**'s seam (`ElysiumNpcMotor.inl`, `ShouldMoveAndShoot`'s gate) and is the same
// read with the same mask. Reused here rather than stood a second time; it answers `0`, so the
// reload arm is skipped, which is the arm the pre-pass takes for a weapon that declares no reload
// capability.

int32 FElysiumNpc::ActiveWeaponFirstAmmoEntry() const
{
	// `weapon+0x74c` — `m_iMagazineCurAmts[0]`, which `FElysiumItem::MagazineCount` already IS.
	const FElysiumItem* const Item = Inventory.Active(*this);
	return Item != nullptr ? Item->MagazineCount : 0;
}

bool FElysiumNpc::ActiveWeaponWantsReload() const
{
	// SEAM for the active weapon's own vtable `+0x460`, the "may this be reloaded" test the cover
	// pair sits behind. No weapon vtable stands here; false skips the pair, which is retail's answer
	// for a weapon that cannot be reloaded, and lets the body reach its spacing tail.
	return false;
}

int32 FElysiumNpc::ActiveWeaponReserveAmmo() const
{
	// `thunk_FUN_103346c0(this, weapon+0x744)` — `GetAmmoCount(ammoType)`, the RESERVE. The port's
	// reserve is keyed by the item data's magazine type, which is `FElysiumItem::AmmoType`.
	const FElysiumItem* const Item = Inventory.Active(*this);
	return Item != nullptr ? Inventory.Reserve(Item->AmmoType) : 0;
}

int32 FElysiumNpc::RangedWeaponPrePass()
{
	// `102b8626`: the reload gate — the convar block, a live weapon, its `+0x5a0 & 0x6000`, and
	// `m_iFakeReloadCount` (`+0x65f0`) below 1.
	const FElysiumEntity* const Weapon = ActiveWeaponEntity();
	if (RangedGateConVarEnabled(ElysiumNpcTunables::EConVar::DebugAllowFakeReload) && Weapon != nullptr
		&& (ActiveWeaponCapabilityWord() & 0x6000u) != 0 && FakeReloadCount < 1)
	{
		// `102b867e`: `thunk_FUN_102c54c0(this)` — unported, counted.
		++RangedReloadPrepCalls;
		// `102b8692`: `m_pHintNode` (`+0x5ddc`) decides which reload program.
		if (BaseScheduleHost.HintNode == 0)
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("RangedWeaponPrePass %s:%d -> 0xc4"), GTroikaFile, 0x5e8f));
			return 0xc4;
		}
		RecordScheduleEvent(FString::Printf(
			TEXT("RangedWeaponPrePass %s:%d -> 0xc6"), GTroikaFile, 0x5e93));
		return 0xc6;
	}

	// `102b86ba`: a weapon with a NON-EMPTY magazine declines the whole pre-pass.
	if (Weapon != nullptr && ActiveWeaponFirstAmmoEntry() > 0)
	{
		return 0;
	}

	// `102b86e5`: an empty weapon that CAN be reloaded and has reserve rounds takes cover to do it.
	if (Weapon != nullptr && ActiveWeaponWantsReload())
	{
		if (ActiveWeaponEntity() == nullptr)
		{
			return 0;   // `102b8706`, retail's second null check
		}
		if (ActiveWeaponReserveAmmo() < 1)
		{
			return 0;   // `102b8724`, no reserve to reload from
		}
		// `102b8730`: `COND_SEE_ENEMY (0x46)` or `COND_NEW_ENEMY (0x54)` picks the near variant.
		if (!Cognition.Conditions.Has(EElysiumNpcCond::SeeEnemy)
			&& !Cognition.Conditions.Has(EElysiumNpcCond::NewEnemy))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("RangedWeaponPrePass %s:%d -> 0xc3"), GTroikaFile, 0x5ecf));
			return 0xc3;
		}
		RecordScheduleEvent(FString::Printf(
			TEXT("RangedWeaponPrePass %s:%d -> 0xc2"), GTroikaFile, 0x5ecb));
		return 0xc2;
	}

	// `102b87b3`: slot 308 `HasUsableRangedWeapon` — draw the ranged weapon.
	if (HasUsableRangedWeaponPort(*this))
	{
		FElysiumEntity* const Enemy = NpcKernelCombat10_2Shared::RangedEnemy(*this);   // slot 167
		Slot601(Enemy);                                      // vt+0x964
		RecordScheduleEvent(FString::Printf(
			TEXT("RangedWeaponPrePass %s:%d -> 0xe9"), GTroikaFile, 0x5ea2));
		return 0xe9;
	}

	// `102b87e8`: slot 307 `HasUsableMeleeWeapon` — the melee spacing tail.
	if (NpcKernelCombat10_2Shared::HasUsableMeleeWeaponPort(*this))
	{
		if (NpcKernelCombat10_2Shared::EnemySlot599(*this))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("RangedWeaponPrePass %s:%d -> 0xe3"), GTroikaFile, 0x5ea8));
			return 0xe3;
		}
		// `102b8824`: the melee-range convar, plus `_DAT_104492b8` (200.0).
		if (MeleeRangeUnits() + GRangedSpacingMargin < ScheduleHost.EnemyDistUnits)
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("RangedWeaponPrePass %s:%d -> 0xe7"), GTroikaFile, 0x5eae));
			return 0xe7;
		}
		// `102b8860`: the roll is drawn BEFORE the second distance test, so it is consumed either
		// way. `> 0x18`, not `>=`.
		if (NpcKernelCombat10_2Shared::RangedRoll() > GSpacingRollThreshold
			&& MeleeRangeUnits() <= ScheduleHost.EnemyDistUnits)
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("RangedWeaponPrePass %s:%d -> 0xe5"), GTroikaFile, 0x5eba));
			return 0xe5;
		}
		RecordScheduleEvent(FString::Printf(
			TEXT("RangedWeaponPrePass %s:%d -> 0xe4"), GTroikaFile, 0x5eb6));
		return 0xe4;
	}

	// `102b88b8`: no usable weapon at all.
	RecordScheduleEvent(FString::Printf(
		TEXT("RangedWeaponPrePass %s:%d -> 0x98"), GTroikaFile, 0x5ec1));
	return 0x98;
}

// =================================================================================================
// `FUN_102b7cf0` — the taunt-and-cover prologue, 462 bytes.
// =================================================================================================

int32 FElysiumNpc::SelectCombatReactionSchedule()
{
	// `102b7cf6`: `COND_LOST_ENEMY (0x47)` — the ONE arm outside the enemy gate below.
	if (Cognition.Conditions.Has(EElysiumNpcCond::LostEnemy))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectCombatReactionSchedule %s:%d -> 0x10"), GTroikaFile, 0x5d24));
		return 0x10;
	}

	// `102b7d2a`: **CORRECTION.** One block opens on `GetEnemy() != 0 && CanSeekCover()` (slot 592,
	// `vt+0x940`) and EVERYTHING below is inside it — the cover offer, the convar gate, the taunt
	// arm and the `SEE_ENEMY` arm alike. The checklist's walk reads as though the convar-gated half
	// sits beside it.
	FElysiumEntity* const Enemy = NpcKernelCombat10_2Shared::RangedEnemy(*this);
	if (Enemy == nullptr || !CanSeekCover())
	{
		return 0;
	}

	// `102b7d4a`: `0x102b7690(1, 1, 1, 1)` — the entrenched cover / kick-prop selector, family
	// Schedule's body. Any non-zero answer wins.
	const FScheduleHintSearchRequest Request{ true, true, true, true };
	if (const int32 Cover = SelectCoverOrKickSchedule(Request); Cover != 0)
	{
		return Cover;
	}

	// `102b7d69`: `DAT_109248f4`, `debug_allow_dodge` — shipped "0", so as shipped this body
	// answers 0 here: no dodge when the cover offer found nothing (the ConVar's own help text).
	if (!RangedGateConVarEnabled(ElysiumNpcTunables::EConVar::DebugAllowDodge))
	{
		return 0;
	}

	// `102b7d8a`: `m_bfAINPCFlags & DODGING (0x800)` is CONSUMED here.
	if (NpcFlags.Has(EElysiumNpcFlag::DODGING))
	{
		NpcFlags.Clear(EElysiumNpcFlag::DODGING);
		const int32 Roll = NpcKernelCombat10_2Shared::RangedRoll();
		if (Roll > GTauntRollThreshold)
		{
			// `102b7dcb`: `m_flNextDodgeTime (+0x65a4) += _DAT_10463584` — **15.0**, recovered out of
			// the pinned image. Retail ADDS to the stamp; it does not re-base it on curtime.
			NextDodgeTime += NpcKernelCombat10_2Shared::GTauntTimerAdvance;
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectCombatReactionSchedule %s:%d -> 0x8f"), GTroikaFile, 0x5d53));
			return 0x8f;
		}
		// `102b7df0`: `FORCED_OCCLUDE (0x10000000)` is SET and the taunt answers 0x8e.
		NpcFlags.Set(EElysiumNpcFlag::FORCED_OCCLUDE);
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectCombatReactionSchedule %s:%d -> 0x8e"), GTroikaFile, 0x5d4c));
		return 0x8e;
	}

	// `102b7e12`: the five-term cover arm, in retail's order — `COND_SEE_ENEMY (0x46)`, the dodge
	// stamp PAST curtime, the frenzied word's `0x2000`, `m_bfAINPCFlags2` LACKING `D_INSANE`
	// (`0x20000`), and `m_bStayEntrenched` (`+0x6435`) CLEAR.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (Cognition.Conditions.Has(EElysiumNpcCond::SeeEnemy)
		&& NextDodgeTime < Now
		&& HasFrenzied(GFrenziedDisciplineBit)
		&& !NpcFlags.Has(EElysiumNpcFlag2::D_INSANE)
		&& !bStayEntrenched)
	{
		// `102b7e9a`: `m_flNextDodgeTime = curtime + RandomFloat(10.0, 20.0)` (`0x41200000`,
		// `0x41a00000`), then a second roll.
		NextDodgeTime = Now
			+ ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(10.0f, 20.0f);
		if (NpcKernelCombat10_2Shared::RangedRoll() > GCoverRollThreshold)
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectCombatReactionSchedule %s:%d -> 0x8d"), GTroikaFile, 0x5d65));
			return 0x8d;
		}
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectCombatReactionSchedule %s:%d -> 0x8c"), GTroikaFile, 0x5d61));
		return 0x8c;
	}
	return 0;
}

// =================================================================================================
// Slot 605 — the dispatcher and its six arms.
// =================================================================================================

int32 FElysiumNpc::SelectScheduleRangedCombat(int32 Arg)
{
	// `CAI_BaseNPCTroika#605` and the classes that share it. The human line's `0x10386560` (which
	// `CNPC_VCop` inherits) and AsianVampire, Bach, MingXiao and SheriffMan override slot 605 on
	// their C++ classes (story 5 step 3).
	return TroikaSelectScheduleRangedCombat(Arg);
}

// --- `CAI_BaseNPCTroika::SelectScheduleRangedCombat` `0x102b7fc0`, 677 bytes ----------------------

int32 FElysiumNpc::TroikaSelectScheduleRangedCombat(int32 Arg)
{
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	// `102b7fc6`: arm 1 — `m_bInMelee` (`+0x6078`).
	if (bInMelee)
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe3"), GTroikaFile, 0x5d98));
		return 0xe3;
	}
	// `102b7fe6`: arm 2 — `COND_TOO_CLOSE_FOR_RANGED (0x8)` AND slot 307 AND slot 599 on the enemy.
	if (Conds.Has(EElysiumNpcCond::TooCloseForRanged) && NpcKernelCombat10_2Shared::HasUsableMeleeWeaponPort(*this)
		&& NpcKernelCombat10_2Shared::EnemySlot599(*this))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe3"), GTroikaFile, 0x5db2));
		return 0xe3;
	}
	// `102b8027`: arm 3 — `COND_WEAPON_THROUGH_WALL (0x3c)`.
	if (Conds.Has(EElysiumNpcCond::WeaponThroughWall))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xb8"), GTroikaFile, 0x5db7));
		return 0xb8;
	}
	// `102b8047`: arm 4 — the weapon pre-pass wins whenever it answers.
	if (const int32 PrePass = RangedWeaponPrePass(); PrePass != 0)
	{
		return PrePass;
	}
	// `102b8056`: the SPLIT. Neither `COND_TOO_CLOSE_TO_ATTACK (0x5f)` nor `COND 0x8`, and the
	// discipline test false → the slot-606 branch; anything else → the dodge/spacing branch.
	if (!Conds.Has(EElysiumNpcCond::TooCloseToAttack) && !Conds.Has(EElysiumNpcCond::TooCloseForRanged)
		&& !RangedDisciplineGate(this))
	{
		// `102b8091`: slot 606 (`vt+0x978`) wins if non-zero.
		if (const int32 Slot606Answer = Slot606(Arg); Slot606Answer != 0)
		{
			return Slot606Answer;
		}
		// `102b80a3`: `COND_EXTENDED_BLOCKED_BY_FRIEND (0x2e)`.
		if (Conds.Has(EElysiumNpcCond::ExtendedBlockedByFriend))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xbd"), GTroikaFile, 0x5dff));
			return 0xbd;
		}
		// `102b80bd`: `COND_TOO_FAR_TO_ATTACK (0x60)`.
		if (Conds.Has(EElysiumNpcCond::TooFarToAttack))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xb1"), GTroikaFile, 0x5e04));
			return 0xb1;
		}
		return 0;
	}
	// `102b8121`: neither `COND_WAITING_ATTACK_TIME (0x2f)` nor `COND_WEAPON_BLOCKED_BY_FRIEND
	// (0x63)`.
	if (!Conds.Has(EElysiumNpcCond::WaitingAttackTime)
		&& !Conds.Has(EElysiumNpcCond::WeaponBlockedByFriend))
	{
		if (ShouldDodgeRangedAttack())
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xef"), GTroikaFile, 0x5dd8));
			return 0xef;
		}
		if (NpcKernelCombat10_2Shared::HasUsableMeleeWeaponPort(*this) && NpcKernelCombat10_2Shared::RangedRoll() < NpcKernelCombat10_2Shared::GMeleeSwitchRollThreshold
			&& NpcKernelCombat10_2Shared::EnemySlot599(*this))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xe8"), GTroikaFile, 0x5ddc));
			return 0xe8;
		}
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xf0"), GTroikaFile, 0x5de0));
		return 0xf0;
	}
	// `102b818c`: the same pair, with 0xb8 in place of 0xef and no roll before slot 599.
	if (ShouldDodgeRangedAttack())
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xb8"), GTroikaFile, 0x5de9));
		return 0xb8;
	}
	if (NpcKernelCombat10_2Shared::HasUsableMeleeWeaponPort(*this) && NpcKernelCombat10_2Shared::EnemySlot599(*this))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe8"), GTroikaFile, 0x5ded));
		return 0xe8;
	}
	RecordScheduleEvent(FString::Printf(
		TEXT("SelectScheduleRangedCombat %s:%d -> 0xb9"), GTroikaFile, 0x5df1));
	return 0xb9;
}

