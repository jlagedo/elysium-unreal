// `CAI_BaseNPC`'s bodies of the `Damage` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseDamage.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumDecalSubsystem.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRetailSite.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumBloodEffects.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDamageShared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumReactions.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	constexpr double DamageBleedFloor = ElysiumNpcTunables::OneDouble;
	constexpr float GearDamage = ElysiumNpcTunables::Hundredth;   // SDK 2013's HITGROUP_GEAR damage
	constexpr float HeavyDamageThreshold = ElysiumNpcTunables::Twenty;   // `IsHeavyDamage`'s only number
	constexpr double DeadDamageScale = ElysiumNpcTunables::TenthDouble;
	constexpr float DeadImpulseZDrop = ElysiumNpcTunables::Ten;     // reused as a Z offset
	// `CAI_BaseNPC::TraceAttack`'s own bit masks and ids.
	constexpr uint32 DmgShock = 0x100u;           // the one bit that suppresses the bleed
	constexpr int32 HitGroupGeneric = 0;
	constexpr int32 HitGroupGear = 10;
	// `CAI_BaseNPC::OnTakeDamage_Dead`'s bit test.
	constexpr uint32 DeadDamageBits = 0xe1u;
	// The combined `DMG_` bits every body in this family reads: the packet's own word OR'd with the
	// descriptor's `m_bdmgTypes` when there is a descriptor, and the packet's alone when there is
	// not. Retail spells this three times; it is one rule.
	uint32 CombinedDamageBits(const FElysiumNpcBase::FElysiumTakeDamageInfo& Info)
	{
		return Info.Dmg != nullptr ? (Info.Dmg->DmgMask | Info.DamageBits) : Info.DamageBits;
	}
	// The magnitude every body in this family reads: `CVDmg_t::GetDmg()` when there is a descriptor,
	// `m_flDamage` when there is not.
	float DamageMagnitude(const FElysiumNpcBase::FElysiumTakeDamageInfo& Info)
	{
		return Info.Dmg != nullptr ? static_cast<float>(Info.Dmg->GetDmg()) : Info.Damage;
	}
}

// --- Moved from `ElysiumNpcDamage.cpp` (story 5 step 5) ---

bool FElysiumNpcBase::CandidateIsStandable(const FElysiumEntity* Candidate) const
{
	// SEAM for slot 164 `IsStandable` on ANOTHER entity. `CBaseEntity::IsStandable`
	// (`0x100b50a0`) is four lines and every one of them reads a word this substrate does not
	// carry:
	//     if (GetSolidFlags() & 0x10) return false;                  // FSOLID_NOT_STANDABLE
	//     int s = GetSolid();                                        // +0x170
	//     if (s == 1 || s == 6 || s == 2) return true;               // BSP, VPHYSICS, BBOX
	//     return IsBSPModel();                                       // thunk_FUN_100b5110
	// There is no solid TYPE and no solid FLAG word on `FElysiumEntity`, so this answers TRUE —
	// the arm a solid body takes, and the permissive one for the single caller below. It does NOT
	// dispatch slot 164, which is another family's generated stub: asking a stub would make this
	// body's answer a function of when that story lands rather than of what retail reads.
	(void)Candidate;
	return true;
}

bool FElysiumNpcBase::IsLightDamage(float Damage, int32 DamageBits)
{
	// 0x10266630, the whole body: `return 0.0f < damage`. STRICTLY greater — a zero-damage hit is
	// not light damage. The `int` second argument (retail's `bitsDamageType`) is not read.
	(void)DamageBits;
	return NpcKernelDamageShared::DamageZero < Damage;
}

bool FElysiumNpcBase::IsHeavyDamage(float Damage, int32 DamageBits)
{
	// 0x10266660, the whole body: `return 20.0f < damage` (`_DAT_1044eb0c`). STRICTLY greater, so a
	// hit measuring exactly 20 is light and not heavy. SDK 2013's `CAI_BaseNPC::IsHeavyDamage`
	// returns a flat false; this fork's threshold is the divergence and it is the recovered fact.
	(void)DamageBits;
	return HeavyDamageThreshold < Damage;
}

void FElysiumNpcBase::TraceAttack(void* InInfo, const FVector& DirUnits, void* InTrace)
{
	FElysiumTakeDamageInfo* Info = static_cast<FElysiumTakeDamageInfo*>(InInfo);
	FElysiumTraceHit* Trace = static_cast<FElysiumTraceHit*>(InTrace);
	if (Info == nullptr || Trace == nullptr)
	{
		return;   // the port's one refusal: retail would have dereferenced both
	}

	// --- `0x10266780`, arm by arm ----------------------------------------------------------------

	// 1. `m_fNoDamageDecal = false` runs BEFORE the `m_takedamage` test, so a body that takes no
	//    damage still has the flag cleared.
	bNoDamageDecal = false;
	if (TakeDamageMode == 0)
	{
		return;
	}

	// 2. `CVDmg_t::EvadeCheck(info->m_pDmg, engine->IndexOfEdict(m_pPev))`. A true answer returns
	//    with nothing written.
	if (TraceAttackEvadeCheck(Info->Dmg))
	{
		return;
	}

	// 3. The 0x4c-byte sub-packet copy — `CTakeDamageInfo subInfo = info` — `REP MOVSD` of 0x13
	//    dwords. Everything after this reads the COPY, and the original is what the flinch gets.
	FElysiumTakeDamageInfo SubInfo = *Info;

	// 4. `SetLastHitGroup(ptr->hitgroup)` and `m_nForceBone = (short)ptr->physicsbone`, in that
	//    order. The bone is sign-extended from a `short` (`MOVSX ECX, word ptr [ESI + 0x48]`).
	LastHitGroup = Trace->HitGroup;
	ForceBone = static_cast<int32>(static_cast<int16>(Trace->PhysicsBone));

	// 5. `CVDmg_t::Apply(info->m_pDmg, victimIndex, info.GetAttacker())` — the ONE damage
	//    resolution, whose integer answer seeds the local magnitude. It runs unconditionally, before
	//    the hitgroup switch, and it is the number the health commit later spends.
	float Magnitude = 0.f;
	if (Info->Dmg != nullptr)
	{
		FElysiumCombatCharacter* Attacker = nullptr;
		if (World != nullptr && Info->Attacker.IsSet())
		{
			if (FElysiumEntity* Ent = World->Resolve(Info->Attacker))
			{
				Attacker = Ent->AsCombatCharacter();
			}
		}
		ElysiumDamage::Apply(*Info->Dmg, Attacker, *this,
			FElysiumDamageContext::FromCharacter(*this));
		Magnitude = static_cast<float>(Info->Dmg->CommittedDamage());
	}

	// 6. The hitgroup switch. **It scales the LOCAL float only** — nothing here is written back into
	//    either packet, which is the family's first standing fact. The jump table at `0x10266a24`
	//    is indexed by `hitgroup - 1` over ten entries:
	//        1 head, 2 chest, 3 stomach, 4/5 arms, 6/7 legs -> cvar * apply
	//        10 gear -> the flat 0.01 constant, AND `ptr->hitgroup = 0`
	//        anything else -> the apply result unchanged
	switch (Trace->HitGroup)
	{
	case 1:
	case 2:
	case 3:
	case 4:
	case 5:
	case 6:
	case 7:
		Magnitude *= HitGroupDamageScaleCvar(Trace->HitGroup);
		break;
	case HitGroupGear:
		// `FSTP ST0` discards the apply result entirely before loading the constant.
		Magnitude = GearDamage;
		Trace->HitGroup = HitGroupGeneric;
		break;
	default:
		break;
	}

	// 7. From here the value tested is the SUB-PACKET's own `GetDmg()` when it carries a descriptor,
	//    and the scaled local float only when it does not — `MOV ECX,[ESP+0x10]; TEST ECX,ECX`.
	const bool bHasDescriptor = SubInfo.Dmg != nullptr;
	const double Tested = bHasDescriptor ? static_cast<double>(SubInfo.Dmg->GetDmg())
										 : static_cast<double>(Magnitude);

	// 8. `subInfo.GetDamage() >= 1.0` (a DOUBLE at `0x10449280`) and no `DMG_SHOCK`. Failing either
	//    skips straight to the flinch — and, crucially, leaves `m_fNoDamageDecal` FALSE, because
	//    retail's raise of that flag is on the SURVIVED-HEADSHOT arm and on nothing else.
	if (Tested >= DamageBleedFloor && (CombinedDamageBits(SubInfo) & DmgShock) == 0)
	{
		// 9. The survived-headshot arm: a head hit the victim lives through raises
		//    `m_fNoDamageDecal` and skips the blood, the bleed AND the flinch is still run after.
		//    The comparison is `m_iHealth - damage > 0` read off `TEST AH,0x41 / JNP`.
		bool bSkipBleed = false;
		if (Trace->HitGroup == NpcKernelDamageShared::HitGroupHead)
		{
			const double Survived = static_cast<double>(Health) - Tested;
			if (Survived > 0.0)
			{
				bNoDamageDecal = true;
				bSkipBleed = true;
			}
		}

		if (!bSkipBleed)
		{
			// 10. `SpawnBlood(ptr->endpos, BloodColor(), damage)` then
			//     `TraceBleed(subInfo.m_pDmg, vecDir, ptr)`. The colour is slot 145 on THIS body.
			const float Amount = static_cast<float>(Tested);
			SpawnBlood(Trace->EndPosUnits, BloodColor(), Amount);
			TraceBleed(SubInfo.Dmg, DirUnits, Trace);
		}
	}

	// 11. `DamageFlinch(info, vecDir, ptr)` — slot 292, and it takes the ORIGINAL packet, not the
	//     sub-packet. It runs on EVERY path past the `m_takedamage`/evade gates, including the
	//     under-1.0 one and the survived-headshot one.
	DamageFlinch(Info, DirUnits, Trace);

	// 12. The tail: `subInfo.SetAmmoType(ptr->+0x50)`, `subInfo.+0x28 = info.+0x28`, then
	//     `AddMultiDamage(subInfo, this)`.
	SubInfo.AmmoType = Trace->AmmoType;
	SubInfo.Attacker = Info->Attacker;
	AddMultiDamage(SubInfo);
}

int32 FElysiumNpcBase::OnTakeDamage_Dead(void* InInfo)
{
	FElysiumTakeDamageInfo* Info = static_cast<FElysiumTakeDamageInfo*>(InInfo);
	if (Info == nullptr)
	{
		return 1;   // retail answers a flat 1 on every path
	}

	// 1. The global death-throw impulse. Seeded from `vec3_origin` (`DAT_1070d1b0..b8`) and only
	//    overwritten when `info.m_hAttacker` (+0x28) is live:
	//        Vector a = attacker->WorldSpaceCenter();   // slot 192, +0x300
	//        a.z -= 10.0;                               // _DAT_1044e664
	//        Vector b = this->WorldSpaceCenter();
	//        impulse = a - b;  VectorNormalize(impulse);   // 0x1057966c, length discarded
	//        _DAT_1070ba40/44/48 = impulse;
	//    It is a FILE-STATIC triple, so there is ONE death-throw impulse per level shared by every
	//    body, not one per NPC — the same shape family Bosses recorded for the ManBat watchdog. The
	//    port carries it as such.
	const FElysiumEntity* Attacker =
		(World != nullptr && Info->Attacker.IsSet()) ? World->Resolve(Info->Attacker) : nullptr;
	if (Attacker != nullptr)
	{
		FVector A = Attacker->Origin / ElysiumMove::U;
		A.Z -= DeadImpulseZDrop;
		const FVector B = Origin / ElysiumMove::U;
		DeathThrowImpulse() = (A - B).GetSafeNormal();
	}

	// 2. `(m_bdmgTypes | m_bitsDamageType) & 0xe1` — the low-byte test: DMG_CRUSH|DMG_BURN|
	//    DMG_CLUB|DMG_SLASH's high pair, i.e. `1 | 0x20 | 0x40 | 0x80`. `TEST AL,0xe1` is a BYTE
	//    test, so nothing above bit 7 can open this arm.
	if ((CombinedDamageBits(*Info) & DeadDamageBits) == 0)
	{
		return 1;
	}
	// 3. `m_takedamage != 1` — a body set to DAMAGE_EVENTS_ONLY takes nothing here.
	if (TakeDamageMode == 1)
	{
		return 1;
	}

	// 4. `m_iHealth = (int)(m_iHealth - GetDmg() * 0.1)` — `_DAT_104493d0`, a DOUBLE 0.1. A corpse
	//    takes a TENTH of the incoming damage into its engine-space health, which is what later
	//    drives the gib threshold; the sheet's own damage counter is untouched.
	const double Amount = static_cast<double>(DamageMagnitude(*Info));
	Health = static_cast<int32>(static_cast<double>(Health) - Amount * DeadDamageScale);
	return 1;
}

// --- Moved from `ElysiumNpcDamage.cpp` (story 5 step 5) ---

FVector& FElysiumNpcBase::DeathThrowImpulse()
{
	return NpcKernelDamageShared::GDeathThrowImpulse;
}

float FElysiumNpcBase::HitGroupDamageScaleCvar(int32 HitGroup) const
{
	// SEAM for `DAT_109201ac` / `DAT_1090fe74` / `DAT_109204f4` / `DAT_1090fdc4` / `DAT_1092023c`
	// — SDK 2013's `sk_npc_head` / `_chest` / `_stomach` / `_arm` / `_leg`. All five pointer cells
	// live past `.data`'s raw size and no corpus function constructs them, so their names and
	// defaults are UNRECOVERED. Retail's inlined read is
	// `if (cvar->IsCommand()) 0.0f else cvar->m_flValue`; an unconstructed cvar answers 0.0f.
	(void)HitGroup;
	return NpcKernelDamageShared::DamageZero;
}

bool FElysiumNpcBase::TraceAttackEvadeCheck(const FElysiumDmg* Dmg) const
{
	// SEAM for `CVDmg_t::EvadeCheck` (`0x10012783`). This is not an absence: `combat-and-damage.md`
	// § "`CVDmg_t`: the 17-word damage descriptor" records that **the installed generic EvadeCheck
	// callback returns zero** and that real defense lives in the ranged/melee attack code. False is
	// the recovered answer.
	(void)Dmg;
	return false;
}

void FElysiumNpcBase::SpawnBlood(const FVector& PositionUnits, int32 BloodColor, float Damage)
{
	// `thunk_FUN_102699e0` -> `FUN_102699e0` 0x102699e0 -- `SpawnBlood(ptr->endpos, BloodColor(),
	// damage)`: the `__ftol` of the damage, the attack-direction global, then the dispatcher
	// `FUN_101cfb30` 0x101cfb30 behind the admission gate `FUN_101cf9b0` 0x101cf9b0
	// (`Substrate/ElysiumBloodEffects.h`, `walks/L0-r013.md`, `walks/L0-r014.md`); the world carries the
	// recipients and the embodiment the mechanical arm's temp entities draw through. Recorded as well.
	FSpawnBloodCall Call;
	Call.PositionUnits = PositionUnits;
	Call.BloodColor = BloodColor;
	Call.Damage = Damage;
	SpawnBloodCalls.Add(Call);
	FElysiumEntityRetailSites Sites(World, *this);
	ElysiumBlood::SpawnBlood(PositionUnits, static_cast<uint32>(BloodColor), Damage, World, &Sites);
}

void FElysiumNpcBase::AddMultiDamage(const FElysiumTakeDamageInfo& SubInfo)
{
	// SEAM for `thunk_FUN_101c2d20(subInfo, this)` — SDK 2013's `AddMultiDamage`. This runtime
	// commits through `FElysiumCombatCharacter::TakeDamage`/`CommitDamage` rather than a per-frame
	// accumulator, so the packet is recorded and nothing is spent here.
	MultiDamageAccumulator.Add(SubInfo);
}

