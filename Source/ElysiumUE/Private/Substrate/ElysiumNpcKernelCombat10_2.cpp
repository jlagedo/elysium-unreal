#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 29d, family **Combat10** — slot 605 `SelectScheduleRangedCombat`, its five species arms and
// the three helpers they offer. The rest of the family is in `ElysiumNpcKernelCombat10.cpp`; the
// declarations and this family's standing facts are in `ElysiumNpcKernelCombat10.inl`.
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
	// `_DAT_10463584` — **15.0**, read out of the pinned `vampire.dll` at file offset `0x463584`.
	// The amount `0x102b7cf0`'s `0x8f` arm advances `m_flNextDodgeTime` (`+0x65a4`) by.
	constexpr float GTauntTimerAdvance = 15.0f;

	// `_DAT_104492b8` — 200.0, the margin `0x102b8620` adds to the melee-range convar.
	constexpr float GRangedSpacingMargin = 200.0f;

	// The three roll thresholds, all literals in the bodies.
	constexpr int32 GDodgeRollThreshold = 0x4b;      // 75 — `0x102b7f40` and the two inlined twins
	constexpr int32 GMeleeSwitchRollThreshold = 0x19;  // 25 — the `0xe8` arm
	constexpr int32 GSpacingRollThreshold = 0x18;    // 24 — `0x102b8620`'s `0xe5` gate (`> 0x18`)
	constexpr int32 GTauntRollThreshold = 0x45;      // 69 — `0x102b7cf0`'s `0x8f` gate (`> 0x45`)
	constexpr int32 GCoverRollThreshold = 0x3b;      // 59 — `0x102b7cf0`'s `0x8d` gate (`> 0x3b`)

	// Retail activity `ACT_…` 0x10, the weighted sequence every dodge test asks for.
	constexpr int32 GDodgeActivity = 0x10;

	// `m_bfNPCFrenziedFlags` (`+0x5b84`) bit `0x2000`. UNNAMED in retail — no name table covers this
	// word (`ElysiumNpcFlags.h`) — and both values `DoPossession` (`0x3b1c`) and `DoFrenzy`
	// (`0x9fbd`) carry it, so the gate reads "this body is under one of the two AI disciplines".
	constexpr uint32 GFrenziedDisciplineBit = 0x2000;

	// The `.cpp` names retail stamps.
	const TCHAR* const GTroikaFile = TEXT("AI_BaseNPCTroika.cpp");
	const TCHAR* const GHumanFile = TEXT("NPC_VHuman.cpp");
	const TCHAR* const GAsianVampireFile = TEXT("NPC_AVampire.cpp");
	const TCHAR* const GBachFile = TEXT("NPC_VBach.cpp");
	const TCHAR* const GMingXiaoFile = TEXT("NPC_VMingXiao.cpp");
	const TCHAR* const GSheriffManFile = TEXT("NPC_VSheriffMan.cpp");

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

	int32 RangedRoll()
	{
		// `(**(code **)(*DAT_1070b244 + 8))(0, 99)` — `RandomInt(0, 99)`.
		return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 99);
	}

	bool HasUsableMeleeWeaponPort(const FElysiumNpc& Npc)
	{
		// Slot 307 `HasUsableMeleeWeapon` (`vt+0x4cc`). The generated slot is a stub answering false;
		// the port carries the fact through the item catalogue, so the capability word is the answer
		// and the retail slot is named beside it — the same reading
		// `ElysiumNpcKernelSchedule.cpp`'s `HasUsableRangedWeaponPort` takes for slot 308.
		return ElysiumNpcCond::WeaponCapability(Npc) == ElysiumNpcCond::ECapability::Melee;
	}

	bool HasUsableRangedWeaponPort(const FElysiumNpc& Npc)
	{
		return ElysiumNpcCond::WeaponCapability(Npc) == ElysiumNpcCond::ECapability::Ranged;
	}

	FElysiumEntity* RangedEnemy(FElysiumNpc& Npc)
	{
		// Slot 167 `GetEnemy` (`vt+0x29c`).
		return Npc.World != nullptr
			? const_cast<FElysiumEntity*>(
				ElysiumNpcCond::ResolveEnemyHandle(*Npc.World, Npc.Senses.Memory.Enemy))
			: nullptr;
	}

	// Slot 599 on `GetEnemy()` — the pair every selector runs together.
	bool EnemySlot599(FElysiumNpc& Npc)
	{
		// SEAM as family Schedule's: the ledger types slot 599's parameter `int`, so the generated
		// signature cannot carry the enemy pointer retail passes and the port passes 0. Named rather
		// than hidden, and the enemy is resolved anyway because retail's `GetEnemy()` call is an
		// observable dispatch.
		(void)RangedEnemy(Npc);
		return Npc.Slot599(0);
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
	if (SelectWeightedSequenceForActivity(GDodgeActivity) == 0)
	{
		return false;
	}
	// `102b7f5f`: `!COND_STOP_BACKUP (0x2c)` and a roll under 75.
	if (!Cognition.Conditions.Has(EElysiumNpcCond::StopBackup) && RangedRoll() < GDodgeRollThreshold)
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
// family **Motor**'s seam (`ElysiumNpcKernelMotor.inl`, `ShouldMoveAndShoot`'s gate) and is the same
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
		if (ScheduleHost.HintNode == 0)
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
		FElysiumEntity* const Enemy = RangedEnemy(*this);   // slot 167
		Slot601(Enemy);                                      // vt+0x964
		RecordScheduleEvent(FString::Printf(
			TEXT("RangedWeaponPrePass %s:%d -> 0xe9"), GTroikaFile, 0x5ea2));
		return 0xe9;
	}

	// `102b87e8`: slot 307 `HasUsableMeleeWeapon` — the melee spacing tail.
	if (HasUsableMeleeWeaponPort(*this))
	{
		if (EnemySlot599(*this))
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
		if (RangedRoll() > GSpacingRollThreshold
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
	FElysiumEntity* const Enemy = RangedEnemy(*this);
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
		const int32 Roll = RangedRoll();
		if (Roll > GTauntRollThreshold)
		{
			// `102b7dcb`: `m_flNextDodgeTime (+0x65a4) += _DAT_10463584` — **15.0**, recovered out of
			// the pinned image. Retail ADDS to the stamp; it does not re-base it on curtime.
			NextDodgeTime += GTauntTimerAdvance;
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
		&& NpcFlags.HasFrenzied(GFrenziedDisciplineBit)
		&& !NpcFlags.Has(EElysiumNpcFlag2::D_INSANE)
		&& !bStayEntrenched)
	{
		// `102b7e9a`: `m_flNextDodgeTime = curtime + RandomFloat(10.0, 20.0)` (`0x41200000`,
		// `0x41a00000`), then a second roll.
		NextDodgeTime = Now
			+ ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(10.0f, 20.0f);
		if (RangedRoll() > GCoverRollThreshold)
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
	// The species dispatch, on `OverrideOf(RetailClass(), 605)` exactly as slot 604's is. `BodyOf`
	// would answer the Troika address for a class with no override, which is the same body the tail
	// runs, so `OverrideOf` is the reader.
	const FElysiumNpcClassSlot* const Override =
		ElysiumNpcKernelClass::OverrideOf(RetailClass(), 605);
	if (Override != nullptr && SpeciesDispatchingSlot != 605)
	{
		// `CNPC_VHuman` and the 35 species that inherit its body.
		if (FCString::Strcmp(Override->Address, TEXT("0x10386560")) == 0)
		{
			FSpeciesDispatchScope Scope(*this, 605);
			return HumanSelectScheduleRangedCombat(Arg);
		}
		if (FCString::Strcmp(Override->Address, TEXT("0x103620d0")) == 0)
		{
			FSpeciesDispatchScope Scope(*this, 605);
			return AsianVampireSelectScheduleRangedCombat(Arg);
		}
		if (FCString::Strcmp(Override->Address, TEXT("0x103642f0")) == 0)
		{
			FSpeciesDispatchScope Scope(*this, 605);
			return BachSelectScheduleRangedCombat(Arg);
		}
		if (FCString::Strcmp(Override->Address, TEXT("0x103967d0")) == 0)
		{
			FSpeciesDispatchScope Scope(*this, 605);
			return MingXiaoSelectScheduleRangedCombat(Arg);
		}
		if (FCString::Strcmp(Override->Address, TEXT("0x103afdb0")) == 0)
		{
			FSpeciesDispatchScope Scope(*this, 605);
			return SheriffManSelectScheduleRangedCombat(Arg);
		}
	}
	// `CAI_BaseNPCTroika#605` and the 20 classes that share it, and the arm a plain `npc_VCop`
	// reaches (its census classname list is null, so `RetailClass()` is null).
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
	if (Conds.Has(EElysiumNpcCond::TooCloseForRanged) && HasUsableMeleeWeaponPort(*this)
		&& EnemySlot599(*this))
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
		if (HasUsableMeleeWeaponPort(*this) && RangedRoll() < GMeleeSwitchRollThreshold
			&& EnemySlot599(*this))
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
	if (HasUsableMeleeWeaponPort(*this) && EnemySlot599(*this))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe8"), GTroikaFile, 0x5ded));
		return 0xe8;
	}
	RecordScheduleEvent(FString::Printf(
		TEXT("SelectScheduleRangedCombat %s:%d -> 0xb9"), GTroikaFile, 0x5df1));
	return 0xb9;
}

// --- `CNPC_VHuman::SelectScheduleRangedCombat` `0x10386560`, 802 bytes ---------------------------

int32 FElysiumNpc::HumanSelectScheduleRangedCombat(int32 Arg)
{
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	// `10386566`: arm 1 — `m_bInMelee`.
	if (bInMelee)
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe3"), GHumanFile, 0x6cf));
		return 0xe3;
	}
	// `10386588`: arm 2 — the cover hint. An EMPTY `m_pShootAtHint` (`+0x6444`) is filled from slot
	// 609 (`vt+0x984`, argument 0); when it is STILL empty the whole arm is SKIPPED.
	bool bHasCoverHint = ScheduleHost.ShootAtHintNode != 0;
	if (!bHasCoverHint)
	{
		// Slot 609's port body answers NULL because hints are bare indices here; the INDEX it found
		// is `FindShootAtHintNode`'s, so the virtual is dispatched for its species gate and the store
		// reads the index. The search itself is family Hints' seam over an absent hint list and
		// answers nothing, so running it twice is observationally identical to running it once.
		//
		// Retail's `+0x6444` is a POINTER and `!= 0` means "found"; the port's word is an INDEX and
		// family Hints' miss is `INDEX_NONE`. The miss is therefore folded onto `0`, which is the
		// value this word's other readers (`NPCThink`, `NPCInit`, `OnRestore`) already treat as
		// "no hint" — otherwise `-1` would read as a resolved hint and fire the arm.
		Slot609(false);
		const int32 FoundHint = FindShootAtHintNode(false);
		ScheduleHost.ShootAtHintNode = FoundHint > 0 ? FoundHint : 0;
		bHasCoverHint = ScheduleHost.ShootAtHintNode != 0;
	}
	if (bHasCoverHint && !Conds.Has(EElysiumNpcCond::WaitingAttackTime))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xec"), GHumanFile, 0x6e4));
		return 0xec;
	}
	// `103865d7`: arm 3 — `COND 0x8` AND slot 307 AND slot 599 on the enemy.
	if (Conds.Has(EElysiumNpcCond::TooCloseForRanged) && HasUsableMeleeWeaponPort(*this)
		&& EnemySlot599(*this))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe3"), GHumanFile, 0x6e9));
		return 0xe3;
	}
	// `1038661d`: arm 4 — `COND 0x3c`.
	if (Conds.Has(EElysiumNpcCond::WeaponThroughWall))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xb8"), GHumanFile, 0x6ee));
		return 0xb8;
	}
	// `1038663d`: the three helpers, first non-zero wins, IN THIS ORDER.
	if (const int32 PrePass = RangedWeaponPrePass(); PrePass != 0)
	{
		return PrePass;
	}
	const int32 Door = SelectDoorObstructionSchedule();   // 0x102b7370
	if (Door != ElysiumScheduleId::None)
	{
		return Door;
	}
	if (const int32 Reaction = SelectCombatReactionSchedule(); Reaction != 0)
	{
		return Reaction;
	}
	// `10386675`: the SPLIT. The discipline test is on the ENEMY's `+0x9c` — its combat-character
	// self-downcast, which is the entity itself for a combat character and null otherwise (family
	// Senses10's standing fact one).
	if (!Conds.Has(EElysiumNpcCond::TooCloseToAttack)
		&& !Conds.Has(EElysiumNpcCond::TooCloseForRanged))
	{
		FElysiumEntity* const Enemy = RangedEnemy(*this);
		const FElysiumEntity* const EnemyCombat =
			Enemy != nullptr ? Enemy->AsCombatCharacter() : nullptr;
		if (!RangedDisciplineGate(EnemyCombat))
		{
			if (const int32 Slot606Answer = Slot606(Arg); Slot606Answer != 0)
			{
				return Slot606Answer;
			}
			if (Conds.Has(EElysiumNpcCond::ExtendedBlockedByFriend))
			{
				RecordScheduleEvent(FString::Printf(
					TEXT("SelectScheduleRangedCombat %s:%d -> 0xbd"), GHumanFile, 0x736));
				return 0xbd;
			}
			if (Conds.Has(EElysiumNpcCond::TooFarToAttack))
			{
				RecordScheduleEvent(FString::Printf(
					TEXT("SelectScheduleRangedCombat %s:%d -> 0xb1"), GHumanFile, 0x73b));
				return 0xb1;
			}
			return 0;
		}
	}
	// `103866f2`: the dodge/spacing branch, identical in shape to the Troika base's.
	if (!Conds.Has(EElysiumNpcCond::WaitingAttackTime)
		&& !Conds.Has(EElysiumNpcCond::WeaponBlockedByFriend))
	{
		if (ShouldDodgeRangedAttack())
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xef"), GHumanFile, 0x70f));
			return 0xef;
		}
		if (HasUsableMeleeWeaponPort(*this) && RangedRoll() < GMeleeSwitchRollThreshold
			&& EnemySlot599(*this))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xe8"), GHumanFile, 0x713));
			return 0xe8;
		}
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xf0"), GHumanFile, 0x717));
		return 0xf0;
	}
	if (ShouldDodgeRangedAttack())
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xb8"), GHumanFile, 0x720));
		return 0xb8;
	}
	if (HasUsableMeleeWeaponPort(*this) && EnemySlot599(*this))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe8"), GHumanFile, 0x724));
		return 0xe8;
	}
	RecordScheduleEvent(FString::Printf(
		TEXT("SelectScheduleRangedCombat %s:%d -> 0xb9"), GHumanFile, 0x728));
	return 0xb9;
}

// --- `CNPC_VAsianVampire::SelectScheduleRangedCombat` `0x103620d0`, 546 bytes ---------------------

int32 FElysiumNpc::AsianVampireSelectScheduleRangedCombat(int32 /*Arg*/)
{
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	// `1036213a`: arm 1 — `m_bInMelee`.
	if (bInMelee)
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe3"), GAsianVampireFile, 0x26a));
		return 0xe3;
	}
	// `10362163`: arm 2 — the same COND `0x8` / slot 307 / slot 599 triple.
	if (Conds.Has(EElysiumNpcCond::TooCloseForRanged) && HasUsableMeleeWeaponPort(*this)
		&& EnemySlot599(*this))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe3"), GAsianVampireFile, 0x284));
		return 0xe3;
	}
	// `103621c2`: arm 3 — `COND 0x3c` answers **0xf0**, where the Troika base answers 0xb8.
	if (Conds.Has(EElysiumNpcCond::WeaponThroughWall))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xf0"), GAsianVampireFile, 0x289));
		return 0xf0;
	}
	// `103621f4`: arm 4 — the weapon pre-pass. This body offers NEITHER the door helper NOR the
	// combat-reaction prologue, and it has no dodge helper, no slot-606 arm and no cover-hint arm.
	if (const int32 PrePass = RangedWeaponPrePass(); PrePass != 0)
	{
		return PrePass;
	}
	// `10362210`: COND `0x5f` OR COND `0x8`, and neither `0x2f` nor `0x63` → 0xf0.
	if (Conds.Has(EElysiumNpcCond::TooCloseToAttack) || Conds.Has(EElysiumNpcCond::TooCloseForRanged))
	{
		if (!Conds.Has(EElysiumNpcCond::WaitingAttackTime)
			&& !Conds.Has(EElysiumNpcCond::WeaponBlockedByFriend))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xf0"), GAsianVampireFile, 0x2af));
			return 0xf0;
		}
	}
	// `10362253`: `COND_SEE_ENEMY (0x46)` and not `0x48` and not `0x60` → 0xf0.
	if (Conds.Has(EElysiumNpcCond::SeeEnemy) && !Conds.Has(EElysiumNpcCond::EnemyOccluded)
		&& !Conds.Has(EElysiumNpcCond::TooFarToAttack))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xf0"), GAsianVampireFile, 0x2e5));
		return 0xf0;
	}
	// `103622b1`: `COND_ENEMY_UNREACHABLE (0x59)` → `GetJumpSchedule`, else 0xe8.
	if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> jump"), GAsianVampireFile, 0x2ce));
		return GetJumpSchedule(RangedEnemy(*this));
	}
	RecordScheduleEvent(FString::Printf(
		TEXT("SelectScheduleRangedCombat %s:%d -> 0xe8"), GAsianVampireFile, 0x2d2));
	return 0xe8;
}

// --- `CNPC_VBach::SelectScheduleRangedCombat` `0x103642f0`, 413 bytes ----------------------------

int32 FElysiumNpc::BachSelectScheduleRangedCombat(int32 Arg)
{
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	// `103642f6`: arm 1 — Bach's own COND `0x7b`. It stamps `+0x6690` (a Bach-line word with no port
	// member; `CNPC_VScurrying` owns the same offset in family Senses10) with
	// `curtime + _DAT_10463584` (**15.0**) and answers 0x15a.
	if (Conds.Has(GBachCondReposition))
	{
		BachRepositionTimer = (World != nullptr ? World->NowSeconds() : 0.0) + GTauntTimerAdvance;
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

	// `10364447`: `CNPC_VHuman::SelectScheduleRangedCombat` `0x10386560`, a DIRECT non-virtual call —
	// the situation `FSpeciesDispatchScope` exists for, expressed here by naming the arm.
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

// --- `CNPC_VMingXiao::SelectScheduleRangedCombat` `0x103967d0`, 794 bytes ------------------------

int32 FElysiumNpc::MingXiaoSelectScheduleRangedCombat(int32 Arg)
{
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	// The inlined dodge decision — the FIRST arm of `0x102b7f40` only, at the same `0x4b`. Ming Xiao
	// does NOT get the discipline arm or the `COND 0x3c` arm the shared helper carries.
	const auto InlineDodge = [this, &Conds]() -> bool
	{
		return SelectWeightedSequenceForActivity(GDodgeActivity) != 0
			&& !Conds.Has(EElysiumNpcCond::StopBackup)
			&& RangedRoll() < GDodgeRollThreshold;
	};

	// `103967d6`: arm 1.
	if (bInMelee)
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe3"), GMingXiaoFile, 0xb12));
		return 0xe3;
	}
	// `103967f8`: arm 2 — the cover hint, exactly as the human's.
	bool bHasCoverHint = ScheduleHost.ShootAtHintNode != 0;
	if (!bHasCoverHint)
	{
		Slot609(false);
		const int32 FoundHint = FindShootAtHintNode(false);
		ScheduleHost.ShootAtHintNode = FoundHint > 0 ? FoundHint : 0;   // as the human arm folds it
		bHasCoverHint = ScheduleHost.ShootAtHintNode != 0;
	}
	if (bHasCoverHint && !Conds.Has(EElysiumNpcCond::WaitingAttackTime))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xec"), GMingXiaoFile, 0xb27));
		return 0xec;
	}
	// `10396847`: arm 3.
	if (Conds.Has(EElysiumNpcCond::TooCloseForRanged) && HasUsableMeleeWeaponPort(*this)
		&& EnemySlot599(*this))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe3"), GMingXiaoFile, 0xb2c));
		return 0xe3;
	}
	// **There is NO `COND 0x3c` arm here** — the human's arm 4 is absent.
	// `1039688d`: the three helpers, same order as the human's.
	if (const int32 PrePass = RangedWeaponPrePass(); PrePass != 0)
	{
		return PrePass;
	}
	const int32 Door = SelectDoorObstructionSchedule();   // 0x102b7370
	if (Door != ElysiumScheduleId::None)
	{
		return Door;
	}
	if (const int32 Reaction = SelectCombatReactionSchedule(); Reaction != 0)
	{
		return Reaction;
	}
	// `103968c3`: the SPLIT — and **there is NO discipline gate**, so the slot-606 branch is entered
	// on the two conditions alone.
	if (!Conds.Has(EElysiumNpcCond::TooCloseToAttack)
		&& !Conds.Has(EElysiumNpcCond::TooCloseForRanged))
	{
		if (const int32 Slot606Answer = Slot606(Arg); Slot606Answer != 0)
		{
			return Slot606Answer;
		}
		if (Conds.Has(EElysiumNpcCond::ExtendedBlockedByFriend))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xbd"), GMingXiaoFile, 0xb6e));
			return 0xbd;
		}
		if (Conds.Has(EElysiumNpcCond::TooFarToAttack))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xb1"), GMingXiaoFile, 0xb73));
			return 0xb1;
		}
		return 0;
	}
	if (!Conds.Has(EElysiumNpcCond::WaitingAttackTime)
		&& !Conds.Has(EElysiumNpcCond::WeaponBlockedByFriend))
	{
		if (InlineDodge())
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xef"), GMingXiaoFile, 0xb49));
			return 0xef;
		}
		if (HasUsableMeleeWeaponPort(*this) && RangedRoll() < GMeleeSwitchRollThreshold
			&& EnemySlot599(*this))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xe8"), GMingXiaoFile, 0xb4d));
			return 0xe8;
		}
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xf0"), GMingXiaoFile, 0xb51));
		return 0xf0;
	}
	if (InlineDodge())
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xb8"), GMingXiaoFile, 0xb58));
		return 0xb8;
	}
	if (HasUsableMeleeWeaponPort(*this) && EnemySlot599(*this))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe8"), GMingXiaoFile, 0xb5c));
		return 0xe8;
	}
	RecordScheduleEvent(FString::Printf(
		TEXT("SelectScheduleRangedCombat %s:%d -> 0xb9"), GMingXiaoFile, 0xb60));
	return 0xb9;
}

// --- `CNPC_VSheriffMan::SelectScheduleRangedCombat` `0x103afdb0`, 984 bytes ----------------------

int32 FElysiumNpc::SheriffManSelectScheduleRangedCombat(int32 /*Arg*/)
{
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	// The same inlined dodge as Ming Xiao's — the first arm of `0x102b7f40` only.
	const auto InlineDodge = [this, &Conds]() -> bool
	{
		return SelectWeightedSequenceForActivity(GDodgeActivity) != 0
			&& !Conds.Has(EElysiumNpcCond::StopBackup)
			&& RangedRoll() < GDodgeRollThreshold;
	};

	// `103afe1a`: arm 1.
	if (bInMelee)
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe3"), GSheriffManFile, 0x2b1));
		return 0xe3;
	}
	// `103afe43`: arm 2.
	if (Conds.Has(EElysiumNpcCond::TooCloseForRanged) && HasUsableMeleeWeaponPort(*this)
		&& EnemySlot599(*this))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe3"), GSheriffManFile, 0x2cb));
		return 0xe3;
	}
	// `103afea2`: arm 3.
	if (Conds.Has(EElysiumNpcCond::WeaponThroughWall))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xb8"), GSheriffManFile, 0x2d0));
		return 0xb8;
	}
	// `103afed4`: the three helpers, offered SEPARATELY (three `if`s, not one chained condition).
	if (const int32 PrePass = RangedWeaponPrePass(); PrePass != 0)
	{
		return PrePass;
	}
	const int32 Door = SelectDoorObstructionSchedule();   // 0x102b7370
	if (Door != ElysiumScheduleId::None)
	{
		return Door;
	}
	if (const int32 Reaction = SelectCombatReactionSchedule(); Reaction != 0)
	{
		return Reaction;
	}
	// `103aff2a`: the SPLIT. Neither COND `0x5f` nor COND `0x8` → the sheriff's OWN branch, which
	// has no slot-606 arm at all.
	if (!Conds.Has(EElysiumNpcCond::TooCloseToAttack)
		&& !Conds.Has(EElysiumNpcCond::TooCloseForRanged))
	{
		// `103aff56`: `COND_SEE_ENEMY` and not `0x48` and not `0x60` → DECLINE.
		if (Conds.Has(EElysiumNpcCond::SeeEnemy) && !Conds.Has(EElysiumNpcCond::EnemyOccluded)
			&& !Conds.Has(EElysiumNpcCond::TooFarToAttack))
		{
			return 0;
		}
		// `103affa6`: `COND_ENEMY_UNREACHABLE` → 0x15a, else 0xe8.
		if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0x15a"), GSheriffManFile, 0x314));
			return 0x15a;
		}
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe8"), GSheriffManFile, 0x31a));
		return 0xe8;
	}
	// `103b0026`: the dodge/spacing branch.
	if (!Conds.Has(EElysiumNpcCond::WaitingAttackTime)
		&& !Conds.Has(EElysiumNpcCond::WeaponBlockedByFriend))
	{
		if (InlineDodge())
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xef"), GSheriffManFile, 0x2ed));
			return 0xef;
		}
		if (HasUsableMeleeWeaponPort(*this) && RangedRoll() < GMeleeSwitchRollThreshold
			&& EnemySlot599(*this))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xe8"), GSheriffManFile, 0x2f1));
			return 0xe8;
		}
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xf0"), GSheriffManFile, 0x2f5));
		return 0xf0;
	}
	if (InlineDodge())
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xb8"), GSheriffManFile, 0x2fc));
		return 0xb8;
	}
	if (HasUsableMeleeWeaponPort(*this) && EnemySlot599(*this))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe8"), GSheriffManFile, 0x300));
		return 0xe8;
	}
	RecordScheduleEvent(FString::Printf(
		TEXT("SelectScheduleRangedCombat %s:%d -> 0xb9"), GSheriffManFile, 0x304));
	return 0xb9;
}
