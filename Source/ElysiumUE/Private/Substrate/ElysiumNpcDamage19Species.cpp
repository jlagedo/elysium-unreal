// Story 0019/8 (29e under the strict verdict), family **Damage19** -- the species classes' bodies.
//
// Every body is the retail override at the address its banner names, read off the listing
// (`vtmb_asm`) and checked against the reading packet (`families-19-29/Damage19-READING.md`, pass R's
// second-judge table included). Every arm carries the address of the instruction it came from. A
// species body that retail chains to the parent through a direct thunk calls that parent by its
// qualified name: every slot-390 chain below lands on `0x102beda0` through
// `0x10001b45 -> 0x10385a50 -> 0x10012611`, which is `FElysiumNpc::OnTakeDamage_Alive` (the Troika
// slot-390 body; the corpus label "OnTakeDamage" is a doc name, pass R). The `g_ScopeTraceStack`
// push/pop several of these bodies wrap themselves in is the debug ring and stays absent.
//
// Owns (Damage19's `rule` rows): 0x10378d30 CNPC_VGargoyle::PlayerKnockbackReaction, 0x1037a5b0
// CNPC_VGargoyle::UpdatePresenceEffect, 0x10380320 CNPC_VHengeyokai::PlayerKnockbackReaction,
// 0x10381b10 CNPC_VHengeyokai::UpdatePresenceEffect, 0x103ab270
// CNPC_VSabbatLeader::UpdatePresenceEffect, 0x103c43f0
// CNPC_VTzimisceRunner::PlayerKnockbackReaction, 0x1035e6d0
// CNPC_VAndreiBlood::OnTakeDamage_Alive, 0x103601a0 CNPC_VAnimal::FUN_103601a0, 0x10363c70
// CNPC_VBach::vfunc390, 0x10378c10 CNPC_VGargoyle::vfunc390, 0x1037bc90
// CNPC_VGhoulCroucher::OnTakeDamage_Alive, 0x103801d0 CNPC_VHengeyokai::vfunc390, 0x1038e880
// CNPC_VManBat::vfunc390, 0x10395ae0 CNPC_VMingXiao::vfunc390, 0x1039e890
// CNPC_VMingXiaoTentacle::vfunc390, 0x103aa480 CNPC_VSabbatLeader::OnTakeDamage_Alive, 0x103b0e90
// CNPC_VSheriffMan::OnTakeDamage_Alive, 0x103cccc0 CNPC_VWerewolf::OnTakeDamage, 0x103e06d0
// CNPC_VZombie::OnTakeDamage. (0x102bed30, slot 142's Troika-line body, is `ElysiumNpcDamage19.cpp`'s.)

#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcAnimal.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcZombie.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

namespace
{
	// `CVStatList_t::GetValue` on the type-0 list (`+0x13bc`/`+0x13c0`, the first entry whose `+0x10`
	// is 0, else `DAT_109f0b40`): `FElysiumCombatCharacter::TypedStatValue(0, id)`. Stat `0x11` is
	// the wound CAP and stat `0x0f` the accumulated WOUNDS (family Combat10's standing fact).
	constexpr int32 GDamage19SpeciesStatListType = 0;
	constexpr int32 GDamage19SpeciesStatCap = 0x11;
	constexpr int32 GDamage19SpeciesStatWounds = 0x0f;

	// `RandomInt(1, 100)` on `DAT_1070b244` (`PUSH 0x64 / PUSH 0x1`) and the `> 0x32` refusal.
	constexpr int32 GDamage19SpeciesRollLow = 1;
	constexpr int32 GDamage19SpeciesRollHigh = 0x64;
	constexpr int32 GDamage19SpeciesRollPass = 0x32;
	// `CNPC_VTzimisceRunner`'s substituted activity, `MOV [ESP+0x8],0x79` at 0x103c43f0.
	constexpr int32 GDamage19RunnerKnockbackActivity = 0x79;

	// `CNPC_VBach`'s shield mask, `TEST EAX,0x4000002` at 0x10363ca4 (DMG_BULLET | 0x4000000).
	constexpr uint32 GDamage19BachShieldBits = 0x4000002u;
	// `CNPC_VGargoyle`'s knockback veto bit, `TEST EAX,0x4000000` at 0x10378c35.
	constexpr uint32 GDamage19GargoyleNoKnockbackBits = 0x4000000u;
	// `AddEntityRelationship(player, 1 /* D_HT */, 10)` at 0x10378c73..0x10378c7a.
	constexpr int32 GDamage19GargoyleHatePriority = 10;
	// `CNPC_VManBat`'s floor, `CMP [ESI+0x210],0x19` at 0x1038e880.
	constexpr int32 GDamage19ManBatHealthFloor = 0x19;
	// `CNPC_VMingXiaoTentacle`'s forced program and trace line.
	constexpr int32 GDamage19TentacleHitSchedule = 0x168;
	constexpr int32 GDamage19TentacleHitLine = 0x549;
	// `CNPC_VSheriffMan`'s species condition, `PUSH 0x79` at 0x103b100c (class-local ordinal).
	constexpr EElysiumNpcCond GDamage19SheriffDeadCond = static_cast<EElysiumNpcCond>(0x79);
	// `CNPC_VHengeyokai`'s explosion classname, the literal at `0x1056b504`.
	const TCHAR* const GDamage19HengeyokaiExplosion = TEXT("point_explosion");
	// `CNPC_VZombie`'s programs and trace lines (`hl2_dll\NPC_VZombie.cpp`).
	constexpr int32 GDamage19ZombieHurtSchedule = 0x164;
	constexpr int32 GDamage19ZombieHurtLine = 0x469;
	constexpr int32 GDamage19ZombieFallbackSchedule = 0x162;
	constexpr int32 GDamage19ZombieFallbackLine = 0x46e;
	constexpr int32 GDamage19ZombieExemptSchedule = 0x161;
	const TCHAR* const GDamage19ZombieHeadshotDamage = TEXT("zombie_headshot_dmg_emitter");   // 0x106655f8
	const TCHAR* const GDamage19ZombieHeadshotDeath = TEXT("zombie_headshot_death_emitter");  // 0x1066561c
	// `CNPC_VMingXiao`'s melee-weapon test on the inflictor's slot-360 word.
	constexpr uint32 GDamage19MingXiaoMeleeBits = 0x18000u;
	// `CNPC_VMingXiao`'s hitgroup -> tentacle table (`FUN_10395650`'s switch).
	constexpr int32 GDamage19MingXiaoNoTentacle = -1;
	constexpr int32 GDamage19MingXiaoHeadHit = -2;

	float Damage19SpeciesMagnitude(const FElysiumNpcBase::FElysiumTakeDamageInfo& Info)
	{
		// `CVDmg_t::GetDmg()` on word 0 when present, the packet's `+0x30` float otherwise.
		return Info.Dmg != nullptr ? static_cast<float>(Info.Dmg->GetDmg()) : Info.Damage;
	}

	uint32 Damage19SpeciesCombinedBits(const FElysiumNpcBase::FElysiumTakeDamageInfo& Info)
	{
		// The packet's `+0x38` OR'd with the descriptor's `m_bdmgTypes` (`+0x10`) when present.
		return Info.Dmg != nullptr ? (Info.Dmg->DmgMask | Info.DamageBits) : Info.DamageBits;
	}

	// The "neuter" both scripted-death floors apply to their packet COPY: `+0x30 = 0` and, with a
	// descriptor, its `m_iDiceAmt` (`+0x4`, `BaseDamage`) and `m_iToHitSuccesses` (`+0xc`,
	// `ExtraInput`). The descriptor is SHARED with the caller's packet (the copy is shallow), so the
	// zeroing reaches the original descriptor too — reproduced.
	void Damage19SpeciesNeuter(FElysiumNpcBase::FElysiumTakeDamageInfo& Copy)
	{
		Copy.Damage = 0.f;
		if (Copy.Dmg != nullptr)
		{
			Copy.Dmg->BaseDamage = 0;
			Copy.Dmg->ExtraInput = 0;
		}
	}

	FElysiumEntity* Damage19SpeciesAttacker(const FElysiumNpc& Npc,
		const FElysiumNpcBase::FElysiumTakeDamageInfo& Info)
	{
		// `info+0x2c`, the attacker.
		return (Npc.World != nullptr && Info.Attacker.IsSet()) ? Npc.World->Resolve(Info.Attacker) : nullptr;
	}

	bool Damage19SpeciesIsPlayer(const FElysiumNpc& Npc, const FElysiumEntity* Entity)
	{
		// `+0xa8`, `CBaseEntity`'s `CBasePlayer*` self-downcast: non-null on exactly the player.
		return Entity != nullptr && Npc.World != nullptr && Entity->Handle == Npc.World->PlayerHandle();
	}

	int32 Damage19SpeciesRoll()
	{
		// `(*DAT_1070b244)->vtable[2](1, 100)`, `RandomInt`.
		return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
			.RandRange(GDamage19SpeciesRollLow, GDamage19SpeciesRollHigh);
	}

	// The presence dismissal and the three words, shared by the three byte-identical slot-313 bodies.
	void Damage19SpeciesClearPresence(FElysiumNpc& Npc)
	{
		Npc.DismissPresenceEffect();            // 0x101e3ff0(&DAT_10739a4c, this)
		Npc.FriendPresenceEffect = 0;           // +0x0e80
		Npc.EnemyPresenceEffect = 0;            // +0x0e88
		Npc.EnemyPresencePercent = 0.f;         // +0x0e84
	}

	// `+0xa0` on the inflictor: `CBaseEntity`'s `CBaseCombatWeapon*` self-downcast.
	const FElysiumEntity* Damage19SpeciesWeaponOf(const FElysiumEntity* Entity)
	{
		const FElysiumItem* Item = Entity != nullptr ? Entity->AsItem() : nullptr;
		return (Item != nullptr && Item->AsWeapon() != nullptr) ? Entity : nullptr;
	}

	// A weapon's slot-360 word tested against `0x18000`: the weapon record's family, which is the
	// port's reading of the same bits (`ElysiumNpcCond::MeleeCapabilityBits`).
	bool Damage19SpeciesWeaponIsMelee(const FElysiumEntity* Weapon)
	{
		const FElysiumItem* Item = Weapon != nullptr ? Weapon->AsItem() : nullptr;
		const FElysiumItemDef* Record = Item != nullptr ? Item->Data() : nullptr;
		(void)GDamage19MingXiaoMeleeBits;
		return Record != nullptr && Record->IsControllableWeapon()
			&& Record->Type == EElysiumItemType::WeaponMelee;
	}
}

// =================================================================================================
// Slot 142 — `CNPC_VWerewolf::OnTakeDamage` `0x103cccc0`, 175 bytes.
// =================================================================================================

int32 FElysiumNpcWerewolf::OnTakeDamage(void* Arg0)
{
	// A default-constructed `CTakeDamageInfo` on the stack (`0x101c2690`), the caller's 19 words
	// copied over it (`0x103ccd36`), and `0x101c2a50(copy, 0)` zeroing the COPY's `+0x44` (a word the
	// port's packet does not carry). The copy is never read again: this override does NOT modify the
	// damage — the original goes on.
	FElysiumTakeDamageInfo Copy;                                            // 0x103ccd31
	if (const FElysiumTakeDamageInfo* Info = static_cast<const FElysiumTakeDamageInfo*>(Arg0))
	{
		Copy = *Info;                                                       // 0x103ccd36 (guarded: retail copies from null)
	}
	(void)Copy;                                                             // 0x103ccd4d
	return FElysiumNpc::OnTakeDamage(Arg0);                                 // 0x103ccd55 -> 0x102bed30
}

// =================================================================================================
// Slot 142 — `CNPC_VZombie::OnTakeDamage` `0x103e06d0`, 491 bytes.
// =================================================================================================

int32 FElysiumNpcZombie::ZombieHeadThresholdCvar() const
{
	// SEAM (header): the unconstructed ConVar at `DAT_1094049c` reads 0.
	return 0;
}

int32 FElysiumNpcZombie::OnTakeDamage(void* Arg0)
{
	FElysiumTakeDamageInfo* Info = static_cast<FElysiumTakeDamageInfo*>(Arg0);
	// 1. Stat 0x0f (wounds) then stat 0x11 (cap), each through the type-0 list walk.
	const int32 Wounds = TypedStatValue(GDamage19SpeciesStatListType, GDamage19SpeciesStatWounds); // 0x103e0732
	const int32 Cap = TypedStatValue(GDamage19SpeciesStatListType, GDamage19SpeciesStatCap);       // 0x103e07a2
	// 2. The amount, then the head threshold: 0 when the ConVar's slot 1 answers true, else its
	//    `+0x2c` int. `m_bShouldGib` when `(threshold - wounds + cap) < amount` (`FILD` / `FCOMP` /
	//    `TEST AH,0x5 / JP`: strict, unordered skips).
	if (Info != nullptr)   // crash guard: retail reads the packet unconditionally
	{
		const float Amount = Damage19SpeciesMagnitude(*Info);              // 0x103e07b1 / 0x103e07b3 / 0x103e07c6
		const int32 Threshold = ZombieHeadThresholdCvar() - Wounds + Cap;  // 0x103e07cd..0x103e07ee
		if (static_cast<double>(Threshold) < static_cast<double>(Amount)) // 0x103e07f6 / 0x103e07ff
		{
			bZombieShouldGib = true;                                        // 0x103e0801
		}
	}
	// 3. `CAI_BaseNPCTroika::OnTakeDamage` (0x102bed30) with the ORIGINAL packet; its answer is kept
	//    and returned.
	const int32 Result = FElysiumNpc::OnTakeDamage(Arg0);                   // 0x103e080b
	// 4. The running schedule's id (`m_pSchedule+0x1c`, -1 with none).
	const int32 ScheduleId = Schedule.IsRunning() ? Schedule.Current : INDEX_NONE; // 0x103e0812..0x103e081f
	// 5. A live answer and a live schedule whose class-local id is not 0x161 take 0x164; every other
	//    case takes 0x162 (FORCED) unless `m_bShouldRagdoll`, which skips the program entirely.
	if (Result != 0                                                         // 0x103e0824
		&& ScheduleId != INDEX_NONE                                         // 0x103e0829
		&& GlobalToLocalId(ClassScheduleIdSpace(), ScheduleId)              // 0x103e082f / 0x103e0838
			!= GDamage19ZombieExemptSchedule)                               // 0x103e083d / 0x103e0842
	{
		RecordScheduleEvent(FString::Printf(TEXT("OnTakeDamage NPC_VZombie.cpp:0x%x -> SetSchedule 0x%x"),
			GDamage19ZombieHurtLine, GDamage19ZombieHurtSchedule));          // 0x103e0846 / 0x103e0874
		SetSchedule(GDamage19ZombieHurtSchedule, false);                    // 0x103e0850 / 0x103e087e
	}
	else if (!bZombieShouldRagdoll)                                         // 0x103e0857 / 0x103e085f
	{
		RecordScheduleEvent(FString::Printf(TEXT("OnTakeDamage NPC_VZombie.cpp:0x%x -> SetSchedule 0x%x"),
			GDamage19ZombieFallbackLine, GDamage19ZombieFallbackSchedule));  // 0x103e0863 / 0x103e0874
		SetSchedule(GDamage19ZombieFallbackSchedule, true);                 // 0x103e086d / 0x103e087e
	}
	// 6. The head-hit byte (`+0x66e1`) dispatches the damage emitter on a live answer and the death
	//    emitter on a zero one — `0x103e05e0`, a `CPASFilter` temp-entity particle at the look data.
	if (bZombieHeadHit)                                                     // 0x103e0883 / 0x103e088b
	{
		const TCHAR* const Emitter = Result != 0                            // 0x103e088d / 0x103e0891
			? GDamage19ZombieHeadshotDamage : GDamage19ZombieHeadshotDeath;
		const int32 Index = CreateNamedEmitter(Emitter, EyePosition() / ElysiumMove::U, 0,
			FElysiumEntityHandle::Invalid(), nullptr);                      // 0x103e0898 / 0x103e08ac
		StartNamedEmitter(Index);
	}
	return Result;                                                          // 0x103e08a4 / 0x103e08b8
}

// =================================================================================================
// Slot 313 — `CNPC_VGargoyle` `0x1037a5b0`, `CNPC_VHengeyokai` `0x10381b10` (byte-identical) and
// `CNPC_VSabbatLeader` `0x103ab270` (the same body inside a scope-trace push/pop).
// =================================================================================================

void FElysiumNpcGargoyle::UpdatePresenceEffect()
{
	Damage19SpeciesClearPresence(*this);   // 0x1037a5b9, then +0xe80 0x1037a5c0, +0xe88 0x1037a5c6, +0xe84 0x1037a5cc
}

void FElysiumNpcHengeyokai::UpdatePresenceEffect()
{
	Damage19SpeciesClearPresence(*this);   // 0x10381b19, then +0xe80 0x10381b20, +0xe88 0x10381b26, +0xe84 0x10381b2c
}

void FElysiumNpcSabbatLeader::UpdatePresenceEffect()
{
	Damage19SpeciesClearPresence(*this);   // 0x103ab2c7, then +0xe80 0x103ab2ce, +0xe88 0x103ab2d4, +0xe84 0x103ab2da
}

// =================================================================================================
// Slot 320 — `CNPC_VGargoyle` `0x10378d30`, `CNPC_VHengeyokai` `0x10380320`,
// `CNPC_VTzimisceRunner` `0x103c43f0`.
// =================================================================================================

bool FElysiumNpcGargoyle::PlayerKnockbackReaction(FElysiumEntity* Arg0, int32 Arg1)
{
	// `m_iCanKnockback` (+0x6688) 0 refuses before the roll.
	if (GargoyleCanKnockback == 0)                                          // 0x10378d33 / 0x10378d3b
	{
		return false;                                                       // 0x10378d6b
	}
	// A flat 50 %: a roll above 0x32 refuses; otherwise the Troika reaction runs and TRUE is
	// answered whatever it answered.
	if (Damage19SpeciesRoll() > GDamage19SpeciesRollPass)                   // 0x10378d49 / 0x10378d4f
	{
		return false;                                                       // 0x10378d6b
	}
	FElysiumNpc::PlayerKnockbackReaction(Arg0, Arg1);                       // 0x10378d5d
	return true;                                                            // 0x10378d65
}

bool FElysiumNpcHengeyokai::PlayerKnockbackReaction(FElysiumEntity* Arg0, int32 Arg1)
{
	// The ungated twin of the Gargoyle body: no `m_iCanKnockback` test.
	if (Damage19SpeciesRoll() > GDamage19SpeciesRollPass)                   // 0x1038032f / 0x10380335
	{
		return false;                                                       // 0x10380351
	}
	FElysiumNpc::PlayerKnockbackReaction(Arg0, Arg1);                       // 0x10380343
	return true;                                                            // 0x1038034b
}

bool FElysiumNpcTzimisceRunner::PlayerKnockbackReaction(FElysiumEntity* Arg0, int32 Arg1)
{
	// The caller's activity is REPLACED by 0x79 and the Troika body's answer tail-returned.
	(void)Arg1;
	return FElysiumNpc::PlayerKnockbackReaction(Arg0, GDamage19RunnerKnockbackActivity); // 0x103c43f0 / 0x103c43f8
}

// =================================================================================================
// Slot 390 — `CNPC_VAndreiBlood::OnTakeDamage_Alive` `0x1035e6d0`, 452 bytes.
// =================================================================================================

int32 FElysiumNpcAndreiBlood::OnTakeDamage_Alive(void* Arg0)
{
	const FElysiumTakeDamageInfo* Info = static_cast<const FElysiumTakeDamageInfo*>(Arg0);
	if (Info == nullptr)
	{
		return 0;   // crash guard: retail copies the packet out of a null pointer
	}
	// 1. The 19-dword packet copied to the stack; the amount off the COPY.
	FElysiumTakeDamageInfo Copy = *Info;                                    // 0x1035e733
	const float Amount = Damage19SpeciesMagnitude(Copy);                    // 0x1035e73b / 0x1035e73d / 0x1035e750
	// 2. Stat 0x11 (cap) then stat 0x0f (wounds).
	const int32 Cap = TypedStatValue(GDamage19SpeciesStatListType, GDamage19SpeciesStatCap);       // 0x1035e7ae
	const int32 Wounds = TypedStatValue(GDamage19SpeciesStatListType, GDamage19SpeciesStatWounds); // 0x1035e819
	// 3. Activated: the hit counter moves, and the copy is forwarded UNTOUCHED while
	//    `cap > wounds + amount` (`TEST AH,0x41 / JP`: > and unordered skip). The moment it is not,
	//    `m_bDead` is set and the copy neutered. Not activated: ALWAYS neutered, the counter still.
	bool bNeuter = true;
	if (bAndreiActivated)                                                   // 0x1035e822 / 0x1035e82a
	{
		++AndreiHitCounter;                                                 // 0x1035e830 / 0x1035e83f
		if (static_cast<double>(Cap) <= static_cast<double>(Wounds) + static_cast<double>(Amount)) // 0x1035e845 / 0x1035e84c
		{
			bAndreiDead = true;                                             // 0x1035e84e
		}
		else
		{
			bNeuter = false;                                                // -> 0x1035e877
		}
	}
	if (bNeuter)
	{
		Damage19SpeciesNeuter(Copy);                                        // 0x1035e859 / 0x1035e863 / 0x1035e865 / 0x1035e870
	}
	// 4. The COPY goes on.
	return FElysiumNpc::OnTakeDamage_Alive(&Copy);                          // 0x1035e87e
}

// =================================================================================================
// Slot 390 — `CNPC_VAnimal` `0x103601a0` (also Dog, Rat, Scurrying, Zombie), 48 bytes.
// =================================================================================================

int32 FElysiumNpcAnimal::OnTakeDamage_Alive(void* Arg0)
{
	// A player attacker (`[info+0x2c]+0xa8` equal to the local player, `0x101cda50`) LATCHES
	// `m_bPlayerAttackedMe` (+0x6660); nothing here ever clears it. The packet goes on unchanged.
	if (const FElysiumTakeDamageInfo* Info = static_cast<const FElysiumTakeDamageInfo*>(Arg0))
	{
		// Crash guard: a null packet or attacker faults in retail; both read as "not the player".
		if (Damage19SpeciesIsPlayer(*this, Damage19SpeciesAttacker(*this, *Info))) // 0x103601a7..0x103601b9
		{
			bPlayerAttackedMe = true;                                       // 0x103601bb
		}
	}
	return FElysiumNpc::OnTakeDamage_Alive(Arg0);                           // 0x103601c5
}

// =================================================================================================
// Slot 390 — `CNPC_VBach::vfunc390` `0x10363c70`, 165 bytes (pass R: BOTH WRONG corrected).
// =================================================================================================

int32 FElysiumNpcBach::OnTakeDamage_Alive(void* Arg0)
{
	// 1. `m_bCanFightYet` (+0x66a8) clear refuses the packet flat with 0.
	if (!bCanFightYet)                                                      // 0x10363c77 / 0x10363c81
	{
		return 0;                                                           // 0x10363c84 / 0x10363c8a
	}
	FElysiumTakeDamageInfo* Info = static_cast<FElysiumTakeDamageInfo*>(Arg0);
	if (Info == nullptr)
	{
		return 0;   // crash guard: retail reads the packet's word 0
	}
	// 2. The combined bits, then `+0x66a6 = 1` on every non-refused packet.
	const uint32 Bits = Damage19SpeciesCombinedBits(*Info);                 // 0x10363c95
	bBachShieldFlagB = true;                                                // 0x10363ca9
	if ((Bits & GDamage19BachShieldBits) != 0)                              // 0x10363ca4 / 0x10363cb0
	{
		if (bBachShieldActive)                                              // 0x10363cb2 / 0x10363cb8
		{
			// 3a. The shield eats the shot: the COPY with `+0x30` and `+0x34` zeroed goes on.
			//     `+0x34` (`m_flMaxDamage`) has no port word.
			FElysiumTakeDamageInfo Copy = *Info;                            // 0x10363cc3
			Copy.Damage = 0.f;                                              // 0x10363ccc
			return FElysiumNpc::OnTakeDamage_Alive(&Copy);                  // 0x10363cdc
		}
		// 3b. Shield down: `m_flNextShieldTime` (+0x6688) zeroed — re-armed at once — and the
		//     original goes on.
		BachNextShieldTime = 0.0;                                           // 0x10363ce9
		return FElysiumNpc::OnTakeDamage_Alive(Arg0);                       // 0x10363cf2
	}
	// 4. Any other type re-arms the holy light (`m_flNextHolyLightTime`, +0x6690) and goes on.
	BachNextHolyLightTime = 0.0;                                            // 0x10363cff
	return FElysiumNpc::OnTakeDamage_Alive(Arg0);                           // 0x10363d08
}

// =================================================================================================
// Slot 390 — `CNPC_VGargoyle::vfunc390` `0x10378c10`, 124 bytes.
// =================================================================================================

int32 FElysiumNpcGargoyle::OnTakeDamage_Alive(void* Arg0)
{
	// 1. `m_iCanKnockback = 1`, then 0 again when the combined bits carry 0x4000000.
	GargoyleCanKnockback = 1;                                               // 0x10378c18
	if (const FElysiumTakeDamageInfo* Info = static_cast<const FElysiumTakeDamageInfo*>(Arg0))
	{
		if ((Damage19SpeciesCombinedBits(*Info) & GDamage19GargoyleNoKnockbackBits) != 0) // 0x10378c26 / 0x10378c3a
		{
			GargoyleCanKnockback = 0;                                       // 0x10378c3c
		}
	}
	// 2. UNCONDITIONALLY: `AddEntityRelationship(m_hClosestPlayer resolved or NULL, D_HT, 10)`.
	//    A NULL entity adds a row keyed on the invalid handle, which no live lookup matches; the
	//    port's store keys by handle and writes nothing for it (named, unobservable divergence).
	const FElysiumEntityHandle Closest = Senses.Memory.ClosestPlayer;       // 0x10378c46
	if (World != nullptr && Closest.IsSet() && World->Resolve(Closest) != nullptr) // 0x10378c4f / 0x10378c6b
	{
		Relationships.SetEntity(Closest, EElysiumRelationship::Hate,
			GDamage19GargoyleHatePriority);                                 // 0x10378c7a
	}
	// 3. The packet goes on unchanged.
	return FElysiumNpc::OnTakeDamage_Alive(Arg0);                           // 0x10378c82
}

// =================================================================================================
// Slot 390 — `CNPC_VGhoulCroucher::OnTakeDamage_Alive` `0x1037bc90`, 150 bytes.
// =================================================================================================

int32 FElysiumNpcGhoulCroucher::OnTakeDamage_Alive(void* Arg0)
{
	// Both statements UNCONDITIONAL and before the chain: `OnDisturbed(info+0x2c)`, then
	// `m_bUnawareExited = 1`. Any damage from any source ends the crouch.
	const FElysiumTakeDamageInfo* Info = static_cast<const FElysiumTakeDamageInfo*>(Arg0);
	OnDisturbed(Info != nullptr ? Damage19SpeciesAttacker(*this, *Info) : nullptr); // 0x1037bd01 / 0x1037bd05
	bUnawareExited = true;                                                  // 0x1037bd0d
	return FElysiumNpc::OnTakeDamage_Alive(Arg0);                           // 0x1037bd14
}

// =================================================================================================
// Slot 390 — `CNPC_VHengeyokai::vfunc390` `0x103801d0`, 154 bytes.
// =================================================================================================

void FElysiumNpcHengeyokai::HengeyokaiEnterMorphSeam()
{
	// SEAM (header): L12's `0x103830e0`, counted.
	++HengeyokaiEnterMorphCalls;
}

int32 FElysiumNpcHengeyokai::OnTakeDamage_Alive(void* Arg0)
{
	// 1. The Troika chain runs FIRST; its answer is this body's answer.
	const int32 Result = FElysiumNpc::OnTakeDamage_Alive(Arg0);             // 0x103801da
	const FElysiumTakeDamageInfo* Info = static_cast<const FElysiumTakeDamageInfo*>(Arg0);
	const FElysiumEntity* Attacker = Info != nullptr ? Damage19SpeciesAttacker(*this, *Info) : nullptr; // 0x103801df
	if (Attacker == nullptr)
	{
		return Result;   // crash guard: retail reads `[attacker+0x11c]` unconditionally
	}
	// 2. The attacker's classname (`+0x11c`) against "point_explosion": the pointer-equal short
	//    circuit (0x103801ee) is the same answer as the string test below. For this literal the
	//    computed length is 15 (so the `+0x26c` arm at 0x10380208 is dead) and its last byte is not
	//    '*' (so the `_strnicmp` arm at 0x1038024a is dead): a null classname compares as "" through
	//    `_stricmp`.
	const FString Classname = Attacker->Def != nullptr ? Attacker->Def->Classname : FString(); // 0x103801e4 / 0x10380224
	if (Classname.Equals(GDamage19HengeyokaiExplosion, ESearchCase::IgnoreCase)) // 0x1038022f / 0x10380259
	{
		HengeyokaiEnterMorphSeam();                                         // 0x1038025d
	}
	return Result;                                                          // 0x10380262
}

// =================================================================================================
// Slot 390 — `CNPC_VManBat::vfunc390` `0x1038e880`, 47 bytes.
// =================================================================================================

int32 FElysiumNpcManBat::OnTakeDamage_Alive(void* Arg0)
{
	// Under 25 health, an attacker that is not the player is refused outright with 1.
	if (Health < GDamage19ManBatHealthFloor)                                // 0x1038e880 / 0x1038e88d
	{
		const FElysiumTakeDamageInfo* Info = static_cast<const FElysiumTakeDamageInfo*>(Arg0);
		// Crash guard: a null packet or attacker faults in retail; both read as "not the player".
		const FElysiumEntity* Attacker = Info != nullptr ? Damage19SpeciesAttacker(*this, *Info) : nullptr;
		if (!Damage19SpeciesIsPlayer(*this, Attacker))                      // 0x1038e893 / 0x1038e89c
		{
			return 1;                                                       // 0x1038e89e / 0x1038e8a3
		}
	}
	return FElysiumNpc::OnTakeDamage_Alive(Arg0);                           // 0x1038e8aa
}

// =================================================================================================
// Slot 390 — `CNPC_VMingXiao::vfunc390` `0x10395ae0`, 117 bytes, and its two helpers.
// =================================================================================================

int32 FElysiumNpcMingXiao::MingXiaoMeleeSpreadChance() const
{
	// SEAM (header): the tuning record's `+0x2c` cell answers 0.
	return 0;
}

int32 FElysiumNpcMingXiao::MingXiaoMeleeTentacleIndex()
{
	// `0x103952b0`, the jump table at `0x10395440` over `m_LastHitGroup - 1`.
	switch (LastHitGroup)                                                   // 0x103952b4
	{
	case 1:
		return GDamage19MingXiaoHeadHit;                                    // 0x103952cb
	case 4:
		if (IsTentacleConnected(1)) { return 1; }                           // 0x103952d7
		[[fallthrough]];   // into case 8's block (0x103952f0)
	case 8:
		if (IsTentacleConnected(3)) { return 3; }                           // 0x103952f0
		return IsTentacleConnected(5) ? 5 : GDamage19MingXiaoNoTentacle;    // 0x10395309 / 0x10395312
	case 5:
		if (IsTentacleConnected(0)) { return 0; }                           // 0x10395321
		[[fallthrough]];   // into case 9's block (0x10395337)
	case 9:
		if (IsTentacleConnected(2)) { return 2; }                           // 0x10395337
		[[fallthrough]];   // into case 7's block (0x10395368)
	case 7:
		return IsTentacleConnected(4) ? 4 : GDamage19MingXiaoNoTentacle;    // 0x10395368 / 0x10395371
	case 6:
		return IsTentacleConnected(5) ? 5 : GDamage19MingXiaoNoTentacle;    // 0x10395350 / 0x10395359
	default:
		break;                                                              // 0x103952be -> 0x10395380
	}
	int32 Index = GDamage19MingXiaoNoTentacle;                              // 0x1039538d
	const int32 Roll = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 99); // 0x10395390
	if (Roll < MingXiaoMeleeSpreadChance())                                 // 0x1039539f / 0x103953a5
	{
		if (IsTentacleConnected(1)) { return 1; }                           // 0x103953af
		if (IsTentacleConnected(0)) { return 0; }                           // 0x103953c8
		if (IsTentacleConnected(3)) { return 3; }                           // 0x103953de
		if (IsTentacleConnected(2)) { return 2; }                           // 0x103953f7
		if (IsTentacleConnected(5)) { return 5; }                           // 0x10395410
		if (IsTentacleConnected(4)) { Index = 4; }                          // 0x10395429 / 0x10395432
	}
	return Index;                                                           // 0x10395437
}

int32 FElysiumNpcMingXiao::MingXiaoHitTentacleIndex(const FElysiumEntity* Weapon)
{
	// `0x10395650`: a melee weapon defers to the melee map; otherwise the hitgroup table.
	if (Weapon != nullptr && Damage19SpeciesWeaponIsMelee(Weapon))          // 0x10395650 slot 360 & 0x18000
	{
		return MingXiaoMeleeTentacleIndex();                                // thunk 0x103952b0
	}
	switch (LastHitGroup)                                                   // +0x1594
	{
	case 1: return GDamage19MingXiaoHeadHit;
	case 4: return 1;
	case 5: return 0;
	case 6: return 5;
	case 7: return 4;
	case 8: return 3;
	case 9: return 2;
	default: return GDamage19MingXiaoNoTentacle;
	}
}

void FElysiumNpcMingXiao::MingXiaoApplyTentacleDamageSeam(int32 TentacleIndex,
	FElysiumTakeDamageInfo& Info, const FElysiumEntity* Weapon)
{
	// SEAM (header): L12's `0x10395750`, counted.
	(void)Info;
	(void)Weapon;
	++MingXiaoApplyTentacleDamageCalls;
	MingXiaoLastAppliedTentacleIndex = TentacleIndex;
}

int32 FElysiumNpcMingXiao::OnTakeDamage_Alive(void* Arg0)
{
	// 1. A proxy (`m_iTentacleID != -1`, `0x10398870`) chains with the packet unmodified.
	if (IsMingXiaoProxy())                                                  // 0x10395ae6 / 0x10395aed
	{
		return FElysiumNpc::OnTakeDamage_Alive(Arg0);                       // 0x10395af6
	}
	const FElysiumTakeDamageInfo* Info = static_cast<const FElysiumTakeDamageInfo*>(Arg0);
	if (Info == nullptr)
	{
		return 0;   // crash guard: retail copies the packet out of a null pointer
	}
	// 2. The head: the 19-dword copy, the copy's `+0x28` object and its `+0xa0` weapon cast (NULL
	//    for a null object, `0x10395b1b`). `+0x28` is the packet's inflictor, which the port carries
	//    on the descriptor (`FElysiumDmg::Inflictor`).
	FElysiumTakeDamageInfo Copy = *Info;                                    // 0x10395b11
	const FElysiumEntity* Inflictor = (World != nullptr && Copy.Dmg != nullptr && Copy.Dmg->Inflictor.IsSet())
		? World->Resolve(Copy.Dmg->Inflictor) : nullptr;                    // 0x10395b13 / 0x10395b19
	const FElysiumEntity* Weapon = Damage19SpeciesWeaponOf(Inflictor);      // 0x10395b1f
	// 3. The tentacle index, then the damage router on the COPY, then the Troika chain with the
	//    MODIFIED copy — the base never sees the original packet.
	const int32 TentacleIndex = MingXiaoHitTentacleIndex(Weapon);           // 0x10395b2d
	MingXiaoApplyTentacleDamageSeam(TentacleIndex, Copy, Weapon);           // 0x10395b3b
	return FElysiumNpc::OnTakeDamage_Alive(&Copy);                          // 0x10395b47
}

// =================================================================================================
// Slot 390 — `CNPC_VMingXiaoTentacle::vfunc390` `0x1039e890`, 61 bytes.
// =================================================================================================

int32 FElysiumNpcMingXiaoTentacle::OnTakeDamage_Alive(void* Arg0)
{
	// Three unconditional statements before the chain: every hit forces program 0x168.
	TentacleHideReadyTimer = 0.0;                                           // 0x1039e89a
	RecordScheduleEvent(FString::Printf(TEXT("OnTakeDamage_Alive NPC_VMingXiaoTentacle.cpp:0x%x -> SetSchedule 0x%x"),
		GDamage19TentacleHitLine, GDamage19TentacleHitSchedule));            // 0x1039e8a4 / 0x1039e8ae
	SetSchedule(GDamage19TentacleHitSchedule, false);                       // 0x1039e8b8
	return FElysiumNpc::OnTakeDamage_Alive(Arg0);                           // 0x1039e8c4
}

// =================================================================================================
// Slot 390 — `CNPC_VSabbatLeader::OnTakeDamage_Alive` `0x103aa480`, 101 bytes.
// =================================================================================================

int32 FElysiumNpcSabbatLeader::OnTakeDamage_Alive(void* Arg0)
{
	// A pure forward inside the (absent) scope-trace push/pop: the leader's damage row IS the base.
	return FElysiumNpc::OnTakeDamage_Alive(Arg0);                           // 0x103aa4d4
}

// =================================================================================================
// Slot 390 — `CNPC_VSheriffMan::OnTakeDamage_Alive` `0x103b0e90`, 471 bytes.
// =================================================================================================

int32 FElysiumNpcSheriffMan::OnTakeDamage_Alive(void* Arg0)
{
	const FElysiumTakeDamageInfo* Info = static_cast<const FElysiumTakeDamageInfo*>(Arg0);
	if (Info == nullptr)
	{
		return 0;   // crash guard: retail copies the packet out of a null pointer
	}
	// 1. The copy and its amount; stat 0x11 (cap) then stat 0x0f (wounds).
	FElysiumTakeDamageInfo Copy = *Info;                                    // 0x103b0ef3
	const float Amount = Damage19SpeciesMagnitude(Copy);                    // 0x103b0efb / 0x103b0efd / 0x103b0f10
	const int32 Cap = TypedStatValue(GDamage19SpeciesStatListType, GDamage19SpeciesStatCap);       // 0x103b0f6e
	const int32 Wounds = TypedStatValue(GDamage19SpeciesStatListType, GDamage19SpeciesStatWounds); // 0x103b0fd9
	// 2. A lethal packet (`cap <= wounds + amount`; `TEST AH,0x41 / JP` skips on > and unordered):
	//    `m_takedamage = 0`, species condition 0x79, `m_bDead = 1`, and the copy neutered.
	//    Otherwise a teleporting sheriff neuters the copy too.
	if (static_cast<double>(Cap) <= static_cast<double>(Wounds) + static_cast<double>(Amount)) // 0x103b0fee / 0x103b0ff5
	{
		TakeDamageMode = 0;                                                 // 0x103b0ff7
		Cognition.Conditions.Set(GDamage19SheriffDeadCond);                 // 0x103b1010
		bSheriffDead = true;                                                // 0x103b1015
		Damage19SpeciesNeuter(Copy);                                        // 0x103b102c / 0x103b1038 / 0x103b1043
	}
	else if (bSheriffTeleporting)                                           // 0x103b101e / 0x103b1026
	{
		Damage19SpeciesNeuter(Copy);                                        // 0x103b102c / 0x103b1038 / 0x103b1043
	}
	// 3. The COPY goes on.
	return FElysiumNpc::OnTakeDamage_Alive(&Copy);                          // 0x103b1051
}
