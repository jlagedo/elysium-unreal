// Story 0019/8 (29e under the strict verdict), family **Spawn19** -- the species classes' bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Every body here is the retail slot body of its class, arm by arm, each arm carrying the address of
// the instruction it came from. A body whose retail listing calls its parent's slot DIRECTLY calls the
// parent here the same way (`FElysiumNpcVampire::Spawn()` for `0x103c4ef0`, `FElysiumNpc::Spawn()` for
// the Troika body `0x10298d30`, whose port body the integrator swaps -- see `TroikaSpawnBody`).
// Walked prose: `docs/vtmb/npc-ai/lifecycle.md` § "Story 8, family Spawn19" (Spawn, 617 / 618) and
// § "The death chain, kill to corpse" (Event_Killed, CreateCorpse).
//
// Owns (Spawn19's `rule` rows): 0x103c60a0 CNPC_VVampireBoss::TransformationStart, 0x103c75f0
// CNPC_VVampireBoss::InputTransformModel, 0x103dfbb0 CNPC_VZombie::CreateCorpse, 0x103ab310
// CNPC_VSabbatLeader::TransformationStart, 0x10378da0 CNPC_VGargoyle::Event_Killed, 0x10380390
// CNPC_VHengeyokai::Event_Killed, 0x1038e8c0 CNPC_VManBat::Event_Killed, 0x1039e900
// CNPC_VMingXiaoTentacle::Event_Killed, 0x103be010 CNPC_VTzimisce::Event_Killed, 0x103c1d50
// CNPC_VTzimisceHeadClaw::Event_Killed, 0x10368b70 CNPC_VCamera::Spawn, 0x10395ba0
// CNPC_VMingXiao::Event_Killed, 0x101aa9c0 CPayphone::Spawn, 0x1035f510 CNPC_VAnimal::Spawn,
// 0x10384690 CNPC_VHuman::Spawn, 0x103927a0 CNPC_VMingXiao::Spawn, 0x1039c380
// CNPC_VMingXiaoTentacle::Spawn, 0x103b9060 CNPC_VTzimisce::Spawn, 0x103c1b90
// CNPC_VTzimisceHeadClaw::Spawn, 0x103c3b30 CNPC_VTzimisceRunner::Spawn, 0x103caa30
// CNPC_VWerewolf::Spawn, 0x10374000 CNPC_VDog::Spawn, 0x1037cda0 CNPC_VGuard1::Spawn, 0x10387110
// CNPC_VHumanCombatant::Spawn, 0x103a2540 CNPC_VPedestrian::Spawn, 0x103ac430
// CNPC_VScurrying::Spawn, 0x103c4ef0 CNPC_VVampire::Spawn, 0x103df170 CNPC_VZombie::Spawn,
// 0x1035cc20 CNPC_VAndreiBlood::Spawn, 0x10360c50 CNPC_VAsianVampire::Spawn, 0x10363850
// CNPC_VBach::Spawn, 0x1036afc0 CNPC_VChangBros::Spawn, 0x10371a20 CNPC_VCop::Spawn, 0x1037b040
// CNPC_VGhoulCroucher::Spawn, 0x1037fa00 CNPC_VHengeyokai::Spawn, 0x103887a0 CNPC_VHunter::Spawn,
// 0x10389390 CNPC_VLasombra::Spawn, 0x1038b030 CNPC_VManBat::Spawn, 0x103a4510
// CNPC_VPlayerController::Spawn, 0x103a6c80 CNPC_VSabbatLeader::Spawn, 0x103ad630 CNPC_VRat::Spawn,
// 0x103ae630 CNPC_VSheriffMan::Spawn, 0x103dd620 CNPC_VYukie::Spawn, 0x10375c50
// CNPC_VFrenzyShadow::Spawn. (`0x1037c1c0 CNPC_VGhoulCroucher::ScriptHide` is Script19's body,
// `GhoulCroucherScriptHide` in `ElysiumNpcScriptSpecies.cpp`; the lane's duplicate was folded.)

#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcAnimal.h"
#include "Substrate/ElysiumNpcAsianVampire.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcCamera.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcCop.h"
#include "Substrate/ElysiumNpcDog.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcGuard1.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcHuman.h"
#include "Substrate/ElysiumNpcHumanCombatant.h"
#include "Substrate/ElysiumNpcHunter.h"
#include "Substrate/ElysiumNpcLasombra.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcPayphone.h"
#include "Substrate/ElysiumNpcPedestrian.h"
#include "Substrate/ElysiumNpcRat.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcScurrying.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcVampire.h"
#include "Substrate/ElysiumNpcVampireBoss.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcYukie.h"
#include "Substrate/ElysiumNpcZombie.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumScheduleNumbers.h"

// The retail constants these bodies push, each once, named after the instruction that pushes it.
// File-static and `GSpawn19`-prefixed because a unity build concatenates this file with the
// family's other translation units.
namespace
{
	// `CAI_BaseNPC::CapabilitiesAdd` (`0x1026dc00`) masks.
	constexpr int32 GSpawn19CapSquad = 0x4000000;             // bits_CAP_SQUAD
	constexpr int32 GSpawn19CapUseWeapons = 0x200000;         // bits_CAP_USE_WEAPONS
	constexpr int32 GSpawn19CapMoveShoot = 0x40;              // bits_CAP_MOVE_SHOOT
	constexpr int32 GSpawn19CapMelee1 = 0x8000;               // bits_CAP_WEAPON_MELEE_ATTACK1
	constexpr int32 GSpawn19CapHumanB = 0x8000000;            // `0x1038469f`
	constexpr int32 GSpawn19CapDoors = 0xd00;                 // `0x103846b7`, USE | AUTO_DOORS | OPEN_DOORS
	constexpr int32 GSpawn19CapGuard = 0x200d00;              // Guard1 / Cop / Hunter / Yukie
	constexpr int32 GSpawn19CapAsian = 0x201000;              // AsianVampire / SheriffMan
	constexpr int32 GSpawn19CapChang = 0x209000;              // ChangBros / SabbatLeader
	constexpr int32 GSpawn19CapBachRemove = 0x1;              // `0x1036385d PUSH 0x1` CapabilitiesRemove
	// `CBaseCombatCharacter::AddClassRelationship(Class_T 1, disposition, priority)`: class 1 is the
	// player's, keyed `player` exactly as `CNPC_VPlayerController::Spawn` keys it; D_HT is 1.
	const TCHAR* const GSpawn19PlayerClass = TEXT("player");
	constexpr int32 GSpawn19VampirePriority = 0;              // `0x103c4ef4 PUSH 0`
	constexpr int32 GSpawn19ManBatPriority = 10;              // `0x1038b044 PUSH 0xa`
	// `CBaseCombatCharacter::AddMiscFlag(0x80000)` (`0x1033c6b0`) -- the Ming Xiao line's.
	constexpr uint32 GSpawn19MiscFlagMingXiao = 0x80000u;
	// `m_flFieldOfView` writes.
	constexpr float GSpawn19FovMinusHalf = -0.5f;             // `0xbf000000`
	constexpr float GSpawn19FovMinusOne = -1.0f;              // `0xbf800000`
	// `SetAbsoluteAttackExtents` literals, SOURCE units.
	const FVector GSpawn19ExtentsMingXiao(120.f, 120.f, 92.f);     // `0x103928fa..0x1039290d`
	const FVector GSpawn19ExtentsTzimisce(60.f, 60.f, 100.f);      // `0x103b90b2..0x103b90c2`
	const FVector GSpawn19ExtentsHeadClaw(45.f, 45.f, 100.f);      // `0x103c1c0e..0x103c1c29`
	const FVector GSpawn19ExtentsRunner(50.f, 50.f, 82.f);         // `0x103c3bc5..0x103c3bd5`
	const FVector GSpawn19ExtentsHengeyokai(50.f, 50.f, 100.f);    // `0x1037fa12..0x1037fa22`
	// `m_fFlags2` bits.
	constexpr uint32 GSpawn19Flag2Four = 0x4u;
	constexpr uint32 GSpawn19Flag2Ten = 0x10u;
	constexpr uint32 GSpawn19Flag2Twenty = 0x20u;
	// `m_bfNPCFrenziedFlags` bits.
	constexpr uint32 GSpawn19FrenzyTzimisceLine = 0x80u;      // `0x103c1c0b OR DL,0x80`
	constexpr uint32 GSpawn19FrenzyScurrying = 0x10000u;      // `0x103ac43e OR EAX,0x10000`
	// The five police levels a zombie or croucher is born deaf to crime at (`MOV EAX,0xf423f`).
	constexpr int32 GSpawn19LawNever = 999999;
	// The four stat templates and the reaction string the listings store (`-(str[0] != 0) & str`,
	// which stores the literal whenever its first byte is non-zero -- always, for these).
	const TCHAR* const GSpawn19TemplateCreation2 = TEXT("TzimisceCreation2");          // `0x1065d51c`
	const TCHAR* const GSpawn19TemplateCreation3 = TEXT("TzimisceCreation3");          // `0x1065decc`
	const TCHAR* const GSpawn19ReactionHateTen = TEXT("D_HT 10");                      // `0x1065d510`
	const TCHAR* const GSpawn19TemplateWerewolf = TEXT("Werewolf");                    // `0x106618d8`
	const TCHAR* const GSpawn19TemplateSabbatLeader = TEXT("VampireSabbatLeader");     // `0x1064eb28`
	const TCHAR* const GSpawn19TemplateRat = TEXT("Rat");                              // `0x10650370`
	const TCHAR* const GSpawn19TemplateStalkerBurning = TEXT("MalkMansionStalkerBurning"); // `0x1063b108`
	const TCHAR* const GSpawn19TemplateStalker = TEXT("MalkMansionStalker");           // `0x1063b0f0`
	const TCHAR* const GSpawn19TemplateCroucher = TEXT("MalkMansionCroucher");         // `0x1063b0d8`
	const TCHAR* const GSpawn19ModelStalker =
		TEXT("models/character/npc/unique/Malkavian_mansion/Stalker/stalker.mdl");          // `0x1063b088`
	const TCHAR* const GSpawn19ModelStalkerFemale =
		TEXT("models/character/npc/unique/Malkavian_mansion/Stalker_Female/stalker_female.mdl"); // `0x1063b028`
	const TCHAR* const GSpawn19ModelAndreiNoMouth =
		TEXT("models/character/npc/unique/hollywood/andrei/andrei_no_mouth.mdl");          // `0x1064ea80`
	const TCHAR* const GSpawn19VampireBossClassname = TEXT("npc_VVampireBoss");         // `0x1065e8dc`
	// The occluded-reaction keyfields the Tzimisce creations are born with: 0 / 0 / 0 / 0 / 100.
	constexpr int32 GSpawn19OccludedChaseOnly = 100;
	// Schedules the species bodies install through `0x102ae750`, class-local numbers.
	constexpr int32 GSpawn19SchedGargoyleDeath = ElysiumSched::SCHED_VGARGOYLE_DEATH;       // `0x10378dc6 PUSH 0x15c`
	constexpr int32 GSpawn19SchedZombieCollapse = ElysiumSched::SCHED_VZOMBIE_ANIMATED_DEATH;      // `0x103dfcbc PUSH 0x162`
	constexpr int32 GSpawn19SchedBossTransform = ElysiumSched::SCHED_VVAMPIREBOSS_TRANSFORM;       // `0x103c61f1 PUSH 0x158`
	constexpr int32 GSpawn19SchedBossMorph = ElysiumSched::SCHED_VVAMPIREBOSS_TRANSFORM_TO_BEAST;           // `0x103c765d PUSH 0x159`
	// The selector-trace lines the bodies stamp (`+0x1b30/+0x1b34`, absent words).
	constexpr int32 GSpawn19LineGargoyle = 0x248;             // NPC_VGargoyle.cpp
	constexpr int32 GSpawn19LineZombie = 0x2eb;               // NPC_VZombie.cpp
	constexpr int32 GSpawn19LineBossIdeal = 0x205;            // npc_VVampireBoss.cpp, +0x1b3c/+0x1b40
	constexpr int32 GSpawn19LineBossSchedule = 0x206;         // npc_VVampireBoss.cpp, +0x1b30/+0x1b34
	// `CNPC_VZombie::CreateCorpse`'s activity probe (`0x103dfc7c PUSH 0x21`).
	constexpr int32 GSpawn19ZombieCollapseActivity = 0x21;
	// `CNPC_VVampireBoss::TransformationStart`'s numbers.
	constexpr int32 GSpawn19BossSpawnFlag = 0x4;              // `0x103c6118 OR EBX,0x4`
	constexpr float GSpawn19BossSeekDist = 4096.f;            // `0x103c615e 0x45800000`
	constexpr uint32 GSpawn19BossEffectsHide = 0x60u;         // `0x103c6185 OR EBX,0x60`
	constexpr uint32 GSpawn19BossEffectsCopy = 0x10u;         // `CopyAnimationDataFrom`'s `| 0x10`
	constexpr uint8 GSpawn19BossNewAlpha = 1;                 // `0x103c61b5`
	constexpr int32 GSpawn19BossNewRenderFx = 0x1f;           // `0x103c61c1`
	constexpr int32 GSpawn19BossRenderMode = 2;               // `0x103c61cb` / `0x103c62f1`
	constexpr int32 GSpawn19BossNewState = 5;                 // `0x103c61f9 MOV ECX,0x5`
	constexpr int32 GSpawn19BossOldRenderFx = 0x1e;           // `0x103c62e7`
	// `CNPC_VSabbatLeader::TransformationStart`'s ideal activity (`0x103ab370 PUSH 0x113e`) and effects bit.
	constexpr int32 GSpawn19SabbatLeaderActivity = 0x113e;
	constexpr uint32 GSpawn19SabbatLeaderEffects = 0x10u;
	// `CPayphone::Spawn`'s numbers.
	constexpr double GSpawn19PayphoneThinkDelay = ElysiumNpcTunables::TenthDouble;  // `_DAT_104491b4` 0.1 (a float cell)
	constexpr int32 GSpawn19PayphoneHealth = 80000;           // `0x101aaa67 0x13880`
	constexpr int32 GSpawn19PayphoneFlag = 0x10000;           // `0x101aaa5b PUSH 0x10000` AddFlag
	constexpr uint32 GSpawn19SolidNotSolid = 0x4u;            // FSOLID_NOT_SOLID
	constexpr int32 GSpawn19SolidBbox = 2;                    // SOLID_BBOX
	// `CNPC_VWerewolf::Spawn`'s `AddFlag(0x2000)`.
	constexpr int32 GSpawn19WerewolfFlag = 0x2000;
	// `CNPC_VMingXiaoTentacle::Spawn`'s ignore-collision window (`_DAT_10454110` = 5.0).
	constexpr double GSpawn19TentacleIgnoreSeconds = ElysiumNpcTunables::Five;
	// `m_lifeState = LIFE_DYING` (`0x1039e92e` / `0x10395c29`).
	constexpr int32 GSpawn19LifeDying = 1;
	// `ming_xiao_grub_death` (`0x1093b9b0`, default "1") is the tunables table's `MingXiaoGrubDeath`
	// (story 8 wave 2), read at its one use.
	// The Hengeyokai's release window (`0x102c43b0(0.75)`).
	constexpr float GSpawn19HengeyokaiReleaseSeconds = 0.75f;

	double Spawn19SpeciesNow(const FElysiumEntity& Entity)
	{
		return Entity.World != nullptr ? Entity.World->NowSeconds() : 0.0;
	}

	// `vstdlib RandomFloat` / `RandomInt` (`DAT_1070b244` slot 1, `[0x109f3868]`) on the NPC stream.
	float Spawn19RandomFloat(float Min, float Max)
	{
		return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(Min, Max);
	}
	int32 Spawn19RandomInt(int32 Min, int32 Max)
	{
		return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(Min, Max);
	}

	// `UTIL_Remove` (`0x101cd940`) on another entity: `Kill`, the port's removal door.
	void Spawn19UtilRemove(FElysiumEntity* Entity)
	{
		if (Entity != nullptr)
		{
			Entity->Kill();
		}
	}
}

// =================================================================================================
// Slot 103 -- the `CAI_BaseNPCTroika` line's species `Spawn` bodies
// =================================================================================================

// 0x1035f510 CNPC_VAnimal::Spawn
void FElysiumNpcAnimal::Spawn()
{
	CapabilityWord |= GSpawn19CapSquad;                                                  // 0x1035f518
	FElysiumNpc::Spawn();                                                                // 0x1035f520 JMP -> 0x10298d30
}

// 0x10384690 CNPC_VHuman::Spawn -- also carries CNPC_VTaxiDriver.
void FElysiumNpcHuman::Spawn()
{
	CapabilityWord |= GSpawn19CapSquad;                                                  // 0x10384698
	CapabilityWord |= GSpawn19CapHumanB;                                                 // 0x103846a4
	CapabilityWord |= GSpawn19CapUseWeapons;                                             // 0x103846b0
	CapabilityWord |= GSpawn19CapDoors;                                                  // 0x103846bc
	FElysiumNpc::Spawn();                                                                // 0x103846c4 JMP -> 0x10298d30
}

// 0x10387110 CNPC_VHumanCombatant::Spawn -- also CNPC_ProneDialog, CNPC_VHumanCombatPatrol,
// CNPC_VSabbatGunman, CNPC_VStalker. The base runs FIRST and the bit lands after it.
void FElysiumNpcHumanCombatant::Spawn()
{
	FElysiumNpcHuman::Spawn();                                                           // 0x10387113 -> 0x10384690
	CapabilityWord |= GSpawn19CapMoveShoot;                                              // 0x1038711c
}

// 0x103c4ef0 CNPC_VVampire::Spawn -- also CNPC_VBrujah, CNPC_VGargoyle, CNPC_VVampireBoss.
void FElysiumNpcVampire::Spawn()
{
	Relationships.AddClassRelationship(GSpawn19PlayerClass, EElysiumRelationship::Hate,
		GSpawn19VampirePriority);                                                        // 0x103c4ef9 -> 0x10013cf5 -> 0x10332aa0 (1, 1, 0)
	FElysiumNpcHuman::Spawn();                                                           // 0x103c4f00 -> 0x10384690
	CapabilityWord |= GSpawn19CapMoveShoot;                                              // 0x103c4f09
}

// 0x1037cda0 CNPC_VGuard1::Spawn
void FElysiumNpcGuard1::Spawn()
{
	CapabilityWord |= GSpawn19CapGuard;                                                  // 0x1037cda8
	bGuard1HatesPlayer = false;                                                          // 0x1037cdad +0x6660
	FElysiumNpcHuman::Spawn();                                                           // 0x1037cdb7 JMP -> 0x10384690
}

// 0x103a2540 CNPC_VPedestrian::Spawn
void FElysiumNpcPedestrian::Spawn()
{
	CapabilityWord |= GSpawn19CapUseWeapons;                                             // 0x103a2548
	FElysiumNpcHuman::Spawn();                                                           // 0x103a2550 JMP -> 0x10384690
}

// 0x103887a0 CNPC_VHunter::Spawn
void FElysiumNpcHunter::Spawn()
{
	CapabilityWord |= GSpawn19CapGuard;                                                  // 0x103887a8
	FElysiumNpcHumanCombatant::Spawn();                                                  // 0x103887b0 JMP -> 0x10387110
}

// 0x103dd620 CNPC_VYukie::Spawn
void FElysiumNpcYukie::Spawn()
{
	CapabilityWord |= GSpawn19CapGuard;                                                  // 0x103dd628
	FElysiumNpcHumanCombatant::Spawn();                                                  // 0x103dd62f -> 0x10387110
	FieldOfViewDot = GSpawn19FovMinusOne;                                                // 0x103dd634 -- 360-degree cone, AFTER the base
}

// 0x10371a20 CNPC_VCop::Spawn
void FElysiumNpcCop::Spawn()
{
	CapabilityWord |= GSpawn19CapGuard;                                                  // 0x10371a28
	CopOldPlayerRelationType = 0;                                                        // 0x10371a2f +0x6668
	FElysiumNpcHumanCombatant::Spawn();                                                  // 0x10371a39 -> 0x10387110
	++CopAliveCensus();                                                                  // 0x10371a3e..0x10371a44 DAT_1093acac
	bCopCountedAlive = true;                                                             // 0x10371a49 +0x6671
	bCopCountedSecond = false;                                                           // 0x10371a50 +0x6672
}

// 0x1037b040 CNPC_VGhoulCroucher::Spawn
void FElysiumNpcGhoulCroucher::Spawn()
{
	CapabilityWord |= GSpawn19CapSquad;                                                  // 0x1037b049
	CapabilityWord |= GSpawn19CapUseWeapons;                                             // 0x1037b055
	CapabilityWord |= GSpawn19CapMelee1;                                                 // 0x1037b061
	AlternateEquipment.Reset();                                                          // 0x1037b070 m_altEquipment = NULL
	AdditionalEquipment.Reset();                                                         // 0x1037b076 m_spawnEquipment = NULL
	// The stat template, burning FIRST (`+0x6665`), then disturbed (`+0x6664`), else the croucher.
	if (bGhoulSpawnBurning)                                                              // 0x1037b066 / 0x1037b07c
	{
		StatTemplate = GSpawn19TemplateStalkerBurning;                                   // 0x1037b08c
	}
	else if (bGhoulSpawnDisturbed)                                                       // 0x1037b094 / 0x1037b09a
	{
		StatTemplate = GSpawn19TemplateStalker;                                          // 0x1037b0ac
	}
	else
	{
		StatTemplate = GSpawn19TemplateCroucher;                                         // 0x1037b0c4
	}
	FElysiumNpcHumanCombatant::Spawn();                                                  // 0x1037b0cc -> 0x10387110
	if (bGhoulSpawnDisturbed)                                                            // 0x1037b0d1 / 0x1037b0db
	{
		bWasDisturbed = true;                                                            // 0x1037b0e4 +0x6666
		bUnawareExited = true;                                                           // 0x1037b0ea +0x6667
		Spawn19SetModel(GSpawn19ModelStalker);                                           // 0x1037b0f2 slot 105
	}
	else
	{
		bWasDisturbed = false;                                                           // 0x1037b101
		bUnawareExited = false;                                                          // 0x1037b107
		Spawn19SetModel(GSpawn19ModelStalkerFemale);                                     // 0x1037b10d slot 105
	}
	UnawareType = Spawn19RandomInt(0, 3);                                                // 0x1037b116 / 0x1037b11c +0x6668
	PlInvestigate = GSpawn19LawNever;                                                    // 0x1037b12a
	PlCriminalFlee = GSpawn19LawNever;                                                   // 0x1037b130
	PlCriminalAttack = GSpawn19LawNever;                                                 // 0x1037b136
	PlSupernaturalFlee = GSpawn19LawNever;                                               // 0x1037b13c
	PlSupernaturalAttack = GSpawn19LawNever;                                             // 0x1037b142
}

// 0x1035cc20 CNPC_VAndreiBlood::Spawn
void FElysiumNpcAndreiBlood::Spawn()
{
	CapabilityWord |= GSpawn19CapUseWeapons;                                             // 0x1035cc78
	FElysiumNpcVampire::Spawn();                                                         // 0x1035cc7f -> 0x103c4ef0
	ActiveRunnerCount = 0;                                                               // 0x1035cc86 +0x66b8
	AndreiKillCount = 0;                                                                 // 0x1035cc8c +0x66bc
	bAndreiActivated = false;                                                            // 0x1035cc92 +0x66cc
	bAndreiDead = false;                                                                 // 0x1035cc98 +0x66cd
	bAndreiTriggerUnhide = false;                                                        // 0x1035cc9e +0x66ce
	AndreiTeleportWaitStartTime = Spawn19SpeciesNow(*this);                              // 0x1035ccaf +0x66d0 = curtime
	bAndreiForceTeleport = true;                                                         // 0x1035ccb5 +0x66d4
	AndreiHitCounter = 0;                                                                // 0x1035ccbc +0x66d8
	FUN_1035e950();                                                                      // 0x1035ccc2 -> m_iHitMax = RandomInt(2, 4)
}

// 0x10360c50 CNPC_VAsianVampire::Spawn
void FElysiumNpcAsianVampire::Spawn()
{
	CapabilityWord |= GSpawn19CapAsian;                                                  // 0x10360ca8
	FElysiumNpcVampire::Spawn();                                                         // 0x10360caf -> 0x103c4ef0
}

// 0x10363850 CNPC_VBach::Spawn -- every stamp starts at 0, so the first shield and the first weapon
// switch are both eligible on the first think.
void FElysiumNpcBach::Spawn()
{
	CapabilityWord |= GSpawn19CapUseWeapons;                                             // 0x10363858
	CapabilityWord &= ~GSpawn19CapBachRemove;                                            // 0x10363861 CapabilitiesRemove(1)
	FElysiumNpcVampire::Spawn();                                                         // 0x10363868 -> 0x103c4ef0
	bBachInStartingPosition = true;                                                      // 0x1036386f +0x66a1
	bBachSkipToWarning = false;                                                          // 0x10363876 +0x66a2
	bBachFireOccluded = false;                                                           // 0x1036387c +0x66a3
	bBachShieldActive = false;                                                           // 0x10363882 +0x66a5
	BachShieldTime = 0.0;                                                                // 0x10363888 +0x6684
	BachNextShieldTime = 0.0;                                                            // 0x1036388e +0x6688
	BachNextWeaponSwitchTime = 0.0;                                                      // 0x10363894 +0x668c
	BachNextHolyLightTime = 0.0;                                                         // 0x1036389a +0x6690
	BachWarningTime = 0.0;                                                               // 0x103638a0 +0x6694
	BaseScheduleHost.WaitFinished = 0.0;                                                 // 0x103638a6 +0x5db4 m_flWaitFinished
	BachSkipToWarningTime = 0.f;                                                         // 0x103638ac +0x6698
	BachTeleportState = 0;                                                               // 0x103638b2 +0x669c
	bBachShotLatch = false;                                                              // 0x103638b8 +0x66a4
	bBachShieldFlagB = false;                                                            // 0x103638be +0x66a6
	BachWasOccluded = 0;                                                                 // 0x103638c4 +0x6674
	BachOccludeEnterTime = 0.0;                                                          // 0x103638ca +0x6670
	BachReusedOccludeCount = 0;                                                          // 0x103638d0 +0x6678
	BachLastOccludeOriginUnits = FVector::ZeroVector;                                       // 0x103638d6..0x103638e2 +0x6664
	bBachCamperFlag = false;                                                             // 0x103638e8 +0x66a0
	BachGrenadeActive = 0;                                                               // 0x103638ee +0x667c
	bBachMovementSpot = false;                                                           // 0x103638f4 +0x66a7
	BachLastGrenadeTime = 0.0;                                                           // 0x103638fa +0x6680
	bCanFightYet = false;                                                                // 0x10363900 +0x66a8
}

// 0x1036afc0 CNPC_VChangBros::Spawn -- also CNPC_VChangBrosBlade, CNPC_VChangBrosClaw.
void FElysiumNpcChangBros::Spawn()
{
	CapabilityWord |= GSpawn19CapChang;                                                  // 0x1036b018
	FElysiumNpcVampire::Spawn();                                                         // 0x1036b01f -> 0x103c4ef0
}

// 0x1037fa00 CNPC_VHengeyokai::Spawn -- no CapabilitiesAdd (as Lasombra and ManBat).
void FElysiumNpcHengeyokai::Spawn()
{
	FElysiumNpcVampire::Spawn();                                                         // 0x1037fa06 -> 0x103c4ef0
	SetAbsoluteAttackExtents(GSpawn19ExtentsHengeyokai);                                 // 0x1037fa2a
	RemoveFlag2(GSpawn19Flag2Four);                                                      // 0x1037fa33
	AddFlag2(GSpawn19Flag2Twenty);                                                       // 0x1037fa3c
}

// 0x10389390 CNPC_VLasombra::Spawn
void FElysiumNpcLasombra::Spawn()
{
	FElysiumNpcVampire::Spawn();                                                         // 0x10389393 -> 0x103c4ef0
	LasombraCoverDisableOverride = 0.f;                                                  // 0x10389398 +0x6664
}

// 0x1038b030 CNPC_VManBat::Spawn -- the class-1 row re-added at priority 10 AFTER the base's 0.
void FElysiumNpcManBat::Spawn()
{
	FElysiumNpcVampire::Spawn();                                                         // 0x1038b033 -> 0x103c4ef0
	SetForceFrequentThink(true);                                                         // 0x1038b03e slot 416
	Relationships.AddClassRelationship(GSpawn19PlayerClass, EElysiumRelationship::Hate,
		GSpawn19ManBatPriority);                                                         // 0x1038b04c -> 0x10332aa0 (1, 1, 10)
}

// 0x103a6c80 CNPC_VSabbatLeader::Spawn -- the capability and the template BEFORE the base.
void FElysiumNpcSabbatLeader::Spawn()
{
	CapabilityWord |= GSpawn19CapChang;                                                  // 0x103a6cd8
	StatTemplate = GSpawn19TemplateSabbatLeader;                                         // 0x103a6ced +0x10e4
	FElysiumNpcVampire::Spawn();                                                         // 0x103a6cf3 -> 0x103c4ef0
}

// 0x103ae630 CNPC_VSheriffMan::Spawn
void FElysiumNpcSheriffMan::Spawn()
{
	CapabilityWord |= GSpawn19CapAsian;                                                  // 0x103ae688
	FElysiumNpcVampire::Spawn();                                                         // 0x103ae68f -> 0x103c4ef0
}

// 0x10374000 CNPC_VDog::Spawn
void FElysiumNpcDog::Spawn()
{
	const float Jitter = Spawn19RandomFloat(0.f, 1.f);                                  // 0x10374012 RandomFloat(0, 1)
	DogWord6688 = 0;                                                                     // 0x10374025 +0x6688
	DogStamp6674 = Spawn19SpeciesNow(*this) + Jitter;                                    // 0x10374020 / 0x1037402f +0x6674
	CapabilityWord |= GSpawn19CapSquad;                                                  // 0x10374035
	CapabilityWord |= GSpawn19CapUseWeapons;                                             // 0x10374041
	CapabilityWord |= GSpawn19CapMelee1;                                                 // 0x1037404d
	FElysiumNpcAnimal::Spawn();                                                          // 0x10374055 JMP -> 0x1035f510
}

// 0x103ac430 CNPC_VScurrying::Spawn -- base FIRST, then the data.
void FElysiumNpcScurrying::Spawn()
{
	FElysiumNpcAnimal::Spawn();                                                          // 0x103ac433 -> 0x1035f510
	FrenziedWord |= GSpawn19FrenzyScurrying;                                             // 0x103ac438..0x103ac443 +0x5b84
	ScurryingSpawnStamp = Spawn19SpeciesNow(*this);                                      // 0x103ac451 +0x668c = curtime
}

// 0x103ad630 CNPC_VRat::Spawn
void FElysiumNpcRat::Spawn()
{
	StatTemplate = GSpawn19TemplateRat;                                                  // 0x103ad63e +0x10e4
	FElysiumNpcScurrying::Spawn();                                                       // 0x103ad644 JMP -> 0x103ac430
}

// 0x103df170 CNPC_VZombie::Spawn
void FElysiumNpcZombie::Spawn()
{
	CapabilityWord |= GSpawn19CapSquad;                                                  // 0x103df17c
	CapabilityWord |= GSpawn19CapUseWeapons;                                             // 0x103df188
	CapabilityWord |= GSpawn19CapMelee1;                                                 // 0x103df194
	AlternateEquipment.Reset();                                                          // 0x103df1a1 +0x1a98
	AdditionalEquipment.Reset();                                                         // 0x103df1a7 +0x5dec
	Senses.Memory.NextFleeSoundTime = 0.0;                                               // 0x103df1ad +0x641c
	PlInvestigate = GSpawn19LawNever;                                                    // 0x103df1b3 +0x6348
	PlCriminalFlee = GSpawn19LawNever;                                                   // 0x103df1b9 +0x634c
	PlCriminalAttack = GSpawn19LawNever;                                                 // 0x103df1bf +0x6350
	PlSupernaturalFlee = GSpawn19LawNever;                                               // 0x103df1c5 +0x6354
	PlSupernaturalAttack = GSpawn19LawNever;                                             // 0x103df1cb +0x6358
	// `0x1042fde0(0)` and the scramble into `+0x6364`, the two tag bytes `+0x6361` / `+0x6360` from
	// UNINITIALISED stack locals (a retail defect: the port writes the defined 0 the Troika `NPCInit`
	// seam already writes), and `m_iPLSupernaturalLevelWitnessed` (`+0x6368`) = 0.
	SeedCriminalLevelWitnessed();                                                        // 0x103df1d1..0x103df215
	FElysiumNpcAnimal::Spawn();                                                          // 0x103df222 JMP -> 0x1035f510
}

// 0x103caa30 CNPC_VWerewolf::Spawn
void FElysiumNpcWerewolf::Spawn()
{
	// The scope-trace frame (`0x103caa35` null-this, `0x103caa3f` null-name) is the absent debug stack.
	StatTemplate = GSpawn19TemplateWerewolf;                                             // 0x103caaaa +0x10e4, BEFORE the base
	FElysiumNpc::Spawn();                                                                // 0x103caab0 -> 0x10298d30
	Flags |= GSpawn19WerewolfFlag;                                                       // 0x103caabc AddFlag(0x2000)
	bIsBccTargetable = true;                                                             // 0x103caac7 +0x1480
	bWerewolfPlayFrustration = false;                                                    // 0x103caace +0x66a9
	WerewolfWord66ac = 0;                                                                // 0x103caad5 +0x66ac
	WerewolfDoorState = 0;                                                               // 0x103caadf +0x6680 m_DoorState
	++Spawn19CollisionPartitionUpdates;                                                  // 0x103caae9 -> 0x100ddd90
	RemoveFlag2(GSpawn19Flag2Four);                                                      // 0x103caaf2
	AddFlag2(GSpawn19Flag2Ten);                                                          // 0x103caafb
}

// 0x103b9060 CNPC_VTzimisce::Spawn -- the base FIRST (unlike the HeadClaw and the Runner).
void FElysiumNpcTzimisce::Spawn()
{
	FElysiumNpc::Spawn();                                                                // 0x103b9066 -> 0x10298d30
	FieldOfViewDot = GSpawn19FovMinusHalf;                                               // 0x103b9071 +0x1574
	// The head-forward basis rotated 90 degrees in place: (x, y, z) -> (-y, x, z).
	const FVector Old = HeadLocalForward;                                                // 0x103b907d..0x103b9092
	HeadLocalForward = FVector(-Old.Y, Old.X, Old.Z);                                    // 0x103b9095 / 0x103b909d / 0x103b90a9
	SetAbsoluteAttackExtents(GSpawn19ExtentsTzimisce);                                   // 0x103b90ca
	RemoveFlag2(GSpawn19Flag2Four);                                                      // 0x103b90d3
	bInMelee = true;                                                                     // 0x103b90d8 +0x6078
}

// 0x103c1b90 CNPC_VTzimisceHeadClaw::Spawn -- the templates and the occlusion keyfields BEFORE the base.
void FElysiumNpcTzimisceHeadClaw::Spawn()
{
	StatTemplate = GSpawn19TemplateCreation2;                                            // 0x103c1ba4 +0x10e4
	PercentOccludedWait = 0;                                                             // 0x103c1bbc +0x6420
	PlayerReaction = GSpawn19ReactionHateTen;                                            // 0x103c1bc2 +0x63ac
	PercentOccludedCover = 0;                                                            // 0x103c1bca +0x6424
	PercentOccludedWalk = 0;                                                             // 0x103c1bd0 +0x6428
	PercentOccludedFlank = 0;                                                            // 0x103c1bd6 +0x642c
	PercentOccludedChase = GSpawn19OccludedChaseOnly;                                    // 0x103c1bdc +0x6430
	FElysiumNpc::Spawn();                                                                // 0x103c1be6 -> 0x10298d30
	CapabilityWord |= GSpawn19CapSquad;                                                  // 0x103c1bf2
	CapabilityWord |= GSpawn19CapUseWeapons;                                             // 0x103c1bfe
	FrenziedWord |= GSpawn19FrenzyTzimisceLine;                                          // 0x103c1c03..0x103c1c16
	SetAbsoluteAttackExtents(GSpawn19ExtentsHeadClaw);                                   // 0x103c1c31
	RemoveFlag2(GSpawn19Flag2Four);                                                      // 0x103c1c3a
}

// 0x103c3b30 CNPC_VTzimisceRunner::Spawn
void FElysiumNpcTzimisceRunner::Spawn()
{
	StatTemplate = GSpawn19TemplateCreation3;                                            // 0x103c3b45 +0x10e4
	PercentOccludedWait = 0;                                                             // 0x103c3b5d
	PlayerReaction = GSpawn19ReactionHateTen;                                            // 0x103c3b63 +0x63ac
	PercentOccludedCover = 0;                                                            // 0x103c3b6b
	PercentOccludedWalk = 0;                                                             // 0x103c3b71
	PercentOccludedFlank = 0;                                                            // 0x103c3b77
	PercentOccludedChase = GSpawn19OccludedChaseOnly;                                    // 0x103c3b7d
	FElysiumNpc::Spawn();                                                                // 0x103c3b87 -> 0x10298d30
	CapabilityWord |= GSpawn19CapSquad;                                                  // 0x103c3b93 (no 0x200000)
	bTzimisceRunnerForm = false;                                                         // 0x103c3b9e +0x6672
	bAllowsInterpenetratingAttacks = true;                                               // 0x103c3ba7 +0x0fe0
	FrenziedWord |= GSpawn19FrenzyTzimisceLine;                                          // 0x103c3ba4 / 0x103c3bae
	RunnerPotentialEnemy = FElysiumEntityHandle::Invalid();                              // 0x103c3bbb +0x6678
	SetAbsoluteAttackExtents(GSpawn19ExtentsRunner);                                     // 0x103c3bdd
	RemoveFlag2(GSpawn19Flag2Four);                                                      // 0x103c3be6
}

// 0x1039c380 CNPC_VMingXiaoTentacle::Spawn -- born invincible and non-colliding for five seconds.
void FElysiumNpcMingXiaoTentacle::Spawn()
{
	CapabilityWord |= GSpawn19CapSquad;                                                  // 0x1039c388
	CapabilityWord |= GSpawn19CapUseWeapons;                                             // 0x1039c394
	FElysiumNpc::Spawn();                                                                // 0x1039c39b -> 0x10298d30
	MiscFlags |= GSpawn19MiscFlagMingXiao;                                               // 0x1039c3a7 AddMiscFlag(0x80000)
	TentaclePhase = 0;                                                                   // 0x1039c3b0 +0x6670
	bInvincible = true;                                                                  // 0x1039c3b6 +0x63d8
	MingXiaoTentaclePhaseExpireTimer = 0.0;                                              // 0x1039c3bc +0x6674
	MingXiaoTentacleFailedEvadeTimer = 0.0;                                                    // 0x1039c3c2 +0x6678
	TentacleUpdateEvadeTimer = 0.0;                                                      // 0x1039c3c8 +0x667c
	TentacleHideReadyTimer = 0.0;                                                        // 0x1039c3ce +0x6680
	bIgnoreCollisionSpecies = true;                                                      // 0x1039c3e5 +0x6688
	bTentacleHitGroundSound = false;                                                     // 0x1039c3ed +0x6698
	bTentaclePlayedDeathAnim = false;                                                    // 0x1039c3f3 +0x6699
	TentacleIgnoreCollisionTimer = Spawn19SpeciesNow(*this) + GSpawn19TentacleIgnoreSeconds; // 0x1039c3dc / 0x1039c3f9 +0x6684
	RemoveFlag2(GSpawn19Flag2Four);                                                      // 0x1039c3ff
}

// 0x103927a0 CNPC_VMingXiao::Spawn
void FElysiumNpcMingXiao::Spawn()
{
	CapabilityWord |= GSpawn19CapSquad;                                                  // 0x103927ae
	CapabilityWord |= GSpawn19CapUseWeapons;                                             // 0x103927ba
	FElysiumNpc::Spawn();                                                                // 0x103927c1 -> 0x10298d30
	MiscFlags |= GSpawn19MiscFlagMingXiao;                                               // 0x103927cd AddMiscFlag(0x80000)
	FieldOfViewDot = GSpawn19FovMinusHalf;                                               // 0x103927d5
	MingXiaoParentMingZhao = FElysiumEntityHandle::Invalid();                            // 0x103927e1 +0x6670
	MingXiaoTentacleId = INDEX_NONE;                                                     // 0x103927e9 +0x6674
	bMingXiaoHasTransformed = false;                                                     // 0x103927ef +0x6678
	MingXiaoSeveredTentacleMask = 0u;                                                    // 0x103927f5 +0x6710
	MingXiaoProxyReadyTimer = 0.0;                                                       // 0x103927fb +0x66a4
	// `thunk_FUN_101e8da0(0x10739d08)`, the Ming Xiao tuning record, is `Select19MingXiaoTuningField`
	// (the Rules.txt `Ming_Xiao_Info` slice, L06/L12).
	MingXiaoSpitAttackTimer = FUN_10397f70([this](int32 Offset) { return Select19MingXiaoTuningField(Offset); })
		+ Spawn19SpeciesNow(*this);                                                      // 0x10392801 / 0x10392816 +0x66c0
	for (int32 Slot = 0; Slot < 6; ++Slot)                                               // 0x10392850 JL
	{
		SeveredTentacles[Slot] = FElysiumEntityHandle::Invalid();                        // 0x1039281c +0x66a8
		bProxyRegistered[Slot] = false;                                                  // 0x10392823 +0x6684
		Proxies[Slot] = FElysiumEntityHandle::Invalid();                                 // 0x1039282a +0x668c
		MingXiaoAttackTimers[Slot] = 0.0;                                                // 0x10392835 +0x66c4
		MingXiaoHitPoints[Slot] = Select19MingXiaoTuningField(0);                        // 0x10392830 / 0x10392838 / 0x1039283f +0x66dc = TentacleHPInitial
		MingXiaoRegrowTimers[Slot] = static_cast<double>(TNumericLimits<float>::Max());  // 0x10392842 +0x66f4 = FLT_MAX
	}
	MingXiaoConnectedTentacleCount = 6;                                                  // 0x10392854 +0x670c
	BodyGroup();                                                                         // 0x1039285e -> 0x10398800
	FElysiumEntity* const Melee = Spawn19BestMeleeWeapon();                              // 0x10392865
	MingXiaoMeleeWeapon = Melee != nullptr ? Melee->Handle : FElysiumEntityHandle::Invalid(); // 0x1039286c / 0x10392872 GetRefEHandle / 0x10392877 / 0x1039287f
	FElysiumEntity* const Ranged = GetBestRangedWeapon();                                // 0x1039288d slot 309
	MingXiaoRangedWeapon = Ranged != nullptr ? Ranged->Handle : FElysiumEntityHandle::Invalid(); // 0x10392895 / 0x1039289b GetRefEHandle / 0x103928a0 / 0x103928a8
	FElysiumEntity* const Switch = (World != nullptr && MingXiaoRangedWeapon.IsSet())
		? World->Resolve(MingXiaoRangedWeapon) : nullptr;                                // 0x103928bb / 0x103928d8 / 0x103928de
	Weapon_Switch(Switch, 0);                                                            // 0x103928f0 slot 388
	SetAbsoluteAttackExtents(GSpawn19ExtentsMingXiao);                                   // 0x10392915
	bNeverMeleeOpponent = true;                                                          // 0x1039291c +0x1482
	MingXiaoThrowableObjectMode = 0;                                                     // 0x10392923 +0x673c
	CoordinateTentacleId = 0;                                                            // 0x10392929 +0x6740
	bMingXiaoPlayedDeathAnim = false;                                                    // 0x1039292f +0x6744
	MingXiaoIdealRange = MingXiaoIdealRangeFromLimbs();                                  // 0x10392935 -> 0x103986b0 / 0x1039293a +0x6748
	MingXiaoChargeReadyTime = 0.0;                                                       // 0x10392940 +0x674c
	bBlockedByFriend = false;                                                            // 0x10392946 +0x6750
}

FElysiumEntity* FElysiumNpcMingXiao::Spawn19BestMeleeWeapon()
{
	return nullptr;
}

// 0x10368b70 CNPC_VCamera::Spawn -- also CNPC_VCameraSecurity. A camera runs no Troika and no base
// spawn: it is its own whole body.
void FElysiumNpcCamera::Spawn()
{
	CapabilityWord |= GSpawn19CapSquad;                                                  // 0x10368b7c
	Precache();                                                                          // 0x10368b85 slot 104
	Spawn19SetModel(Model);                                                              // 0x10368b94 / 0x10368ba2 null default / 0x10368bab slot 9 then 105
	BloodColorWord = Spawn19BloodColor;                                                  // 0x10368bb1 +0x1570
	EffectsWord = 0u;                                                                    // 0x10368bbb m_fEffects = 0
	Health = 1;                                                                          // 0x10368bc1 +0x210
	FieldOfViewDot = Spawn19TroikaFieldOfView;                                           // 0x10368bcb +0x1574
	WriteNpcStateRetail(0);                                                              // 0x10368bd5 m_NPCState = 0
	HackedGunPosUnits = FVector::ZeroVector;                                             // 0x10368bdb..0x10368be7
	Senses.ResetListenClock();                                                           // 0x10368bf3 m_flNextListenTime = 0
	CurrentSpotIndex = INDEX_NONE;                                                       // 0x10368bfb m_pInterestingPlace = 0
	// The two `m_Collision` scope-trace frames' null-name defaults (`0x10368c01`, `0x10368c70`) are
	// the absent debug stack.
	RetailSolidFlags = 0u;                                                               // 0x10368c57 SetSolidFlags(0)
	RetailSolidType = 0;                                                                 // 0x10368cc0 SetSolid(SOLID_NONE)
	++RetailSolidSets;
	SetMoveType(Spawn19MoveTypeStep, 0);                                                 // 0x10368cd6 slot 93 (4, 0)
	SetHullSizeNormal(false);                                                            // 0x10368cdf -> 0x10273070
	++Spawn19RelinkCalls;                                                                // 0x10368ce5 -> 0x101cf600
	NPCInit();                                                                           // 0x10368cf1 slot 420
	Senses.ResolveTuning(*this);                                                         // 0x10368cf9 -> 0x1028fb70 InitPerceptionDistances
	ApplyDisciplineSpawnFlags();                                                         // 0x10368d00 -> 0x1033df80
	EyeLookTargetHandle = FElysiumEntityHandle::Invalid();                               // 0x10368d0b +0x0e64 = -1
	RelativeEyeTarget = 0;                                                               // 0x10368d1b +0x5b94
	RepairPoliceLevels();                                                                // 0x10368d2d..0x10368d9e
	Hide();                                                                              // 0x10368da8 slot 66
	AddFlag2(GSpawn19Flag2Ten);                                                          // 0x10368db2 AddFlag2(0x10)
}

// 0x101aa9c0 CPayphone::Spawn -- BBOX plus NOT_SOLID and 80000 health: usable, never destroyed.
void FElysiumNpcPayphone::Spawn()
{
	// The two `m_Collision` scope-trace frames' null-name defaults (`0x101aa9f3`, `0x101aaaa5`) are
	// the absent debug stack.
	FElysiumNpc::Spawn();                                                                // 0x101aa9c4 -> 0x10298d30
	ArmThinkAt(Spawn19SpeciesNow(*this) + GSpawn19PayphoneThinkDelay);                  // 0x101aa9d4..0x101aa9df m_flNextThink
	SetMoveType(0, 0);                                                                   // 0x101aa9e5 slot 93 MOVETYPE_NONE
	RetailSolidType = GSpawn19SolidBbox;                                                 // 0x101aaa51 SetSolid(SOLID_BBOX)
	++RetailSolidSets;
	Health = GSpawn19PayphoneHealth;                                                     // 0x101aaa67 +0x210
	TakeDamageMode = 0;                                                                  // 0x101aaa71 +0x1fc DAMAGE_NO
	Flags |= GSpawn19PayphoneFlag;                                                       // 0x101aaa7b AddFlag(0x10000)
	SequenceNumber = 0;                                                                  // 0x101aaa82 +0x6f0 m_nSequence
	ResetSequenceInfo();                                                                 // 0x101aaa8c -> 0x10090950
	SequenceCycle = 0.f;                                                                 // 0x101aaa97 +0x6f8 m_flCycle
	RetailSolidFlags |= (RetailSolidFlags & 0xffffu) | GSpawn19SolidNotSolid;           // 0x101aab04 AddSolidFlags(w | 4)
	RemoveFlag2(GSpawn19Flag2Four);                                                      // 0x101aab14
	AddFlag2(GSpawn19Flag2Ten);                                                          // 0x101aab1d
}

// =================================================================================================
// Slot 144 -- the species `Event_Killed` bodies
// =================================================================================================

// 0x10378da0 CNPC_VGargoyle::Event_Killed -- a non-gibbing gargoyle plays a death PROGRAM instead.
void FElysiumNpcGargoyle::Event_Killed(void* Arg0)
{
	if (GargoyleDoingGibDeath != 0)                                                      // 0x10378da0 / 0x10378da8
	{
		FElysiumNpc::Event_Killed(Arg0);                                                 // 0x10378daa JMP -> 0x102bf340
		return;
	}
	RecordScheduleEvent(FString::Printf(TEXT("SelectSchedule NPC_VGargoyle.cpp:%d"),
		GSpawn19LineGargoyle));                                                          // 0x10378db6 / 0x10378dc0
	SetSchedule(GSpawn19SchedGargoyleDeath, true);                                       // 0x10378dca -> 0x102ae750(0x15c, 1)
}

// 0x10380390 CNPC_VHengeyokai::Event_Killed -- the carried body is dropped BEFORE the base death.
void FElysiumNpcHengeyokai::Event_Killed(void* Arg0)
{
	if (HengeyokaiCarryFormBit())                                                        // 0x10380393 / 0x1038039a
	{
		HengeyokaiDropCarriedBody();                                                     // 0x1038039e -> 0x103828a0
	}
	FElysiumNpc::Event_Killed(Arg0);                                                     // 0x103803aa -> 0x102bf340
}

void FElysiumNpcHengeyokai::HengeyokaiDropCarriedBody()
{
	// `0x103828a0`.
	if (!HengeyokaiCarryFormBit())                                                       // 0x10381c80
	{
		return;
	}
	HengeyokaiPickupTarget = FElysiumEntityHandle::Invalid();                            // +0x6664 = -1
	ArmIgnoreCollisionExpiry(GSpawn19HengeyokaiReleaseSeconds);                          // 0x102c43b0(0.75)
	if (World != nullptr && HengeyokaiPhysicsAnimlink.IsSet()
		&& World->Resolve(HengeyokaiPhysicsAnimlink) != nullptr)
	{
		RemovePhysAnimlink(HengeyokaiPhysicsAnimlink);                                   // 0x101cd970
	}
	HengeyokaiPhysicsAnimlink = FElysiumEntityHandle::Invalid();                         // +0x6690 = -1
	CallFormBit(false);                                                                  // 0x10381c00(0)
}

// 0x1038e8c0 CNPC_VManBat::Event_Killed -- four teardowns and the screech cone, all BEFORE the base.
void FElysiumNpcManBat::Event_Killed(void* Arg0)
{
	ManBatReleaseSlowedEntity(true);                                                     // 0x1038e8c5 -> 0x1038f020(1)
	Spawn19SetFlyMode(2);                                                                // 0x1038e8ce -> 0x1038c170(2)
	Spawn19DropCarried();                                                                // 0x1038e8d5 -> 0x1038f660
	Spawn19ReleaseMinions();                                                             // 0x1038e8dc -> 0x1038fd40
	// `+0x66a4`, the screech-cone emitter, DESTROYED when it still resolves.
	if (World != nullptr && ManBatScreechCone.IsSet())                                   // 0x1038e8ea
	{
		if (World->Resolve(ManBatScreechCone) != nullptr)                                // 0x1038e90c / 0x1038e912
		{
			Spawn19UtilRemove(World->Resolve(ManBatScreechCone));                        // 0x1038e91d / 0x1038e934 re-resolve / 0x1038e93d UTIL_Remove
			ManBatScreechCone = FElysiumEntityHandle::Invalid();                         // 0x1038e945
		}
	}
	FElysiumNpc::Event_Killed(Arg0);                                                     // 0x1038e956 -> 0x102bf340
}

void FElysiumNpcManBat::Spawn19SetFlyMode(int32 Mode)
{
	Spawn19DeathTeardown.Add(FString::Printf(TEXT("0x1038c170(%d)"), Mode));
}

void FElysiumNpcManBat::Spawn19DropCarried()
{
	Spawn19DeathTeardown.Add(TEXT("0x1038f660"));
}

void FElysiumNpcManBat::Spawn19ReleaseMinions()
{
	Spawn19DeathTeardown.Add(TEXT("0x1038fd40"));
}

// 0x1039e900 CNPC_VMingXiaoTentacle::Event_Killed -- the first kill plays the death program, the
// second one dies.
void FElysiumNpcMingXiaoTentacle::Event_Killed(void* Arg0)
{
	if (!bTentaclePlayedDeathAnim)                                                       // 0x1039e903 / 0x1039e90b
	{
		LifeState = GSpawn19LifeDying;                                      // 0x1039e92e m_lifeState = 1
		MingXiaoTentacleEnterDeath();                                                    // 0x1039e938 -> 0x1039e970
		return;                                                                          // 0x1039e93e
	}
	if (FElysiumEntity* const Head = MingXiaoTentacleHead())                             // 0x1039e90d -> 0x1039ede0 / 0x1039e914
	{
		Spawn19NotifyHeadOfDeath(Head);                                                  // 0x1039e919 -> 0x103979d0
	}
	FElysiumNpc::Event_Killed(Arg0);                                                     // 0x1039e925 -> 0x102bf340
}

void FElysiumNpcMingXiaoTentacle::Spawn19NotifyHeadOfDeath(FElysiumEntity* Head)
{
	(void)Head;
	++Spawn19HeadDeathNotices;
}

// 0x10395ba0 CNPC_VMingXiao::Event_Killed -- deferred to the death animation's second pass.
void FElysiumNpcMingXiao::Event_Killed(void* Arg0)
{
	if (!bMingXiaoPlayedDeathAnim)                                                       // 0x10395ba3 / 0x10395bab
	{
		LifeState = GSpawn19LifeDying;                                      // 0x10395c29 m_lifeState = 1
		MingXiaoEnterDeath();                                                            // 0x10395c33 -> 0x10395c70
		return;                                                                          // 0x10395c39
	}
	// A live parent Ming Zhao hears about this proxy first.
	if (World != nullptr && MingXiaoParentMingZhao.IsSet())                              // 0x10395bb6
	{
		FElysiumEntity* const ParentEntity = World->Resolve(MingXiaoParentMingZhao);     // 0x10395bd2 / 0x10395bd8
		FElysiumNpc* const ParentNpc = ParentEntity != nullptr ? ParentEntity->AsNpc() : nullptr;
		if (FElysiumNpcMingXiao* const Parent = ParentNpc != nullptr
			? ParentNpc->AsSpecies<FElysiumNpcMingXiao>() : nullptr)
		{
			Parent->FUN_10397a50(this, [Parent](int32 Offset)
				{ return Parent->Select19MingXiaoTuningField(Offset); });              // 0x10395bdd -> 0x10397a50
		}
	}
	// `ming_xiao_grub_death` read as `IsCommand() ? 0 : m_nValue`; only the HEAD tears its grubs down.
	if (ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::MingXiaoGrubDeath) != 0)                                           // 0x10395bea IsCommand / 0x10395bef / 0x10395bfc
	{
		if (!IsMingXiaoProxy())                                                          // 0x10395c00 -> 0x10398870 / 0x10395c07
		{
			MingXiaoKillTentacles();                                                     // 0x10395c0b -> 0x10397e90
			MingXiaoKillSpawnedBodies();                                                 // 0x10395c12 -> 0x10397f00
		}
	}
	FElysiumNpc::Event_Killed(Arg0);                                                     // 0x10395c1e -> 0x102bf340
}

// 0x103be010 CNPC_VTzimisce::Event_Killed
void FElysiumNpcTzimisce::Event_Killed(void* Arg0)
{
	if (TzimisceCarryFormBit())                                                          // 0x103be013 / 0x103be01a
	{
		VGargoyleGibCleanup();                                                           // 0x103be01e -> 0x103bf170 (the pickup release)
	}
	Spawn19RemoveNearbyType2();                                                          // 0x103be025 -> 0x103bdfc0, unconditional
	FElysiumNpc::Event_Killed(Arg0);                                                     // 0x103be031 -> 0x102bf340
}

void FElysiumNpcTzimisce::Spawn19RemoveNearbyType2()
{
	++Spawn19NearbyRemovals;
}

// 0x103c1d50 CNPC_VTzimisceHeadClaw::Event_Killed
void FElysiumNpcTzimisceHeadClaw::Event_Killed(void* Arg0)
{
	TzimisceHeadClawEndSlow(true);                                                       // 0x103c1d55 -> 0x103c2230(1)
	FElysiumNpc::Event_Killed(Arg0);                                                     // 0x103c1d61 -> 0x102bf340
}

// =================================================================================================
// Slot 301 -- `CNPC_VZombie::CreateCorpse` `0x103dfbb0`
// =================================================================================================

void FElysiumNpcZombie::CreateCorpse(const FVector& Arg0, void* Arg1)
{
	// COPY first, decide second: the force and the whole damage packet are saved before the split.
	ZombieDeathForceVector = Arg0;                                                       // 0x103dfbd7..0x103dfbe2 +0x6680
	ZombieDeathDamageInfo = Arg1 != nullptr
		? *static_cast<const FElysiumTakeDamageInfo*>(Arg1) : FElysiumTakeDamageInfo();  // 0x103dfbe9..0x103dfc86 +0x668c
	const int32 Sequence = SelectWeightedSequenceForActivity(GSpawn19ZombieCollapseActivity); // 0x103dfc89
	if (!bZombieShouldGib && Sequence != INDEX_NONE)                                     // 0x103dfc96 / 0x103dfc9b
	{
		if (bZombieShouldRagdoll)                                                        // 0x103dfca7
		{
			// The SAVED words, not the arguments.
			FElysiumCombatCharacter::CreateCorpse(ZombieDeathForceVector, &ZombieDeathDamageInfo); // 0x103dfcab -> 0x1032c0e0
			return;                                                                      // 0x103dfcb7
		}
		RecordScheduleEvent(FString::Printf(TEXT("SelectSchedule NPC_VZombie.cpp:%d"),
			GSpawn19LineZombie));                                                        // 0x103dfcc1 / 0x103dfccb
		SetSchedule(GSpawn19SchedZombieCollapse, true);                                  // 0x103dfcd5 -> 0x102ae750(0x162, 1)
		ThinkSet(StartNpcThinkFunction(), 0.0);                                          // 0x103dfce5 ThinkSet(LAB_1000f4e8, 0)
		ResetThinkTimers(Spawn19SpeciesNow(*this));                                      // 0x103dfcee slot 614
		return;                                                                          // 0x103dfcfb
	}
	CorpseGib();                                                                         // 0x103dfd02 slot 394
	Spawn19UtilRemove(this);                                                             // 0x103dfd09 -> 0x101cd940
}

// =================================================================================================
// Slots 617 / 618 -- the vampire boss line's protean swap
// =================================================================================================

// 0x103c75f0 CNPC_VVampireBoss::InputTransformModel
void FElysiumNpcVampireBoss::InputTransformModel(const FElysiumInputArgs& Args)
{
	(void)Args;
	// UNCONDITIONAL and first: every boss transforms into `npc_VVampireBoss`.
	VampireBossMonsterClassname = GSpawn19VampireBossClassname;                          // 0x103c763f +0x6694
	// `m_MorphModelName` (`+0x667c`), a null pointer read as the empty string.
	if (!VampireBossMorphModelName.IsEmpty())                                            // 0x103c7652 / 0x103c7659
	{
		VampireBossMonsterModelName = VampireBossMorphModelName;                         // 0x103c7662 +0x6680
		SetSchedule(GSpawn19SchedBossMorph, false);                                      // 0x103c7668 -> 0x102ae750(0x159, 0)
	}
}

// 0x103c60a0 CNPC_VVampireBoss::TransformationStart
void FElysiumNpcVampireBoss::TransformationStart()
{
	if (World == nullptr)
	{
		return;
	}
	// The factory `0x10136580` on `m_pszMonsterClassname` -- create, NOT spawn; its `+0x98`.
	FElysiumEntityDef NewDef;
	NewDef.Classname = VampireBossMonsterClassname;
	NewDef.Origin = Origin;
	const FElysiumEntityHandle NewHandle = World->CreateRuntimeEntityNoSpawn(MoveTemp(NewDef)); // 0x103c60fa
	FElysiumEntity* const NewEntity = World->Resolve(NewHandle);
	FElysiumNpc* const New = NewEntity != nullptr && !NewEntity->IsRecordOnly()
		? NewEntity->AsNpc() : nullptr;                                                  // 0x103c6104 / 0x103c610a
	if (New == nullptr)
	{
		// **Named crash guard**: a factory miss leaves `ESI` null and retail faults at `0x103c6110`.
		++Spawn19TransformCreateFailures;
		Spawn19UtilRemove(NewEntity);
		return;
	}
	New->SpawnFlags |= GSpawn19BossSpawnFlag;                                            // 0x103c6110..0x103c611b
	New->SetOrigin(GetAbsOrigin());                                                      // 0x103c6125 / 0x103c612e slot 217 -> slot 62
	// Slot 64 `SetAngles` is the generated stub; the write it stands for is the runtime angles.
	New->SetRuntimeAngles(GetAngles());                                                  // 0x103c613a / 0x103c6143 slot 221 -> slot 64
	New->SetOwnerEntity(Handle);                                                         // 0x103c614e slot 202
	// `CopyAnimationDataFrom(this)` (`0x10097310`): the words the port carries (the player
	// controller's copy in `FElysiumEntityWorld::GetControllerNPC` is the same list).
	New->Model = Model;                                                                  // 0x103c6157
	New->Skin = Skin;
	New->SequenceNumber = SequenceNumber;
	New->SequenceCycle = SequenceCycle;
	New->AnimTime = AnimTime;
	New->EffectsWord = EffectsWord | GSpawn19BossEffectsCopy;
	New->AuthoredVision = GSpawn19BossSeekDist;                                          // 0x103c615e +0x63b4 m_flSeekDistBase
	New->Spawn19SetModel(VampireBossMonsterModelName);                                   // 0x103c6171 slot 105
	World->CallEntitySpawn(*New);                                                        // 0x103c6178 DispatchSpawn
	New->EffectsWord |= GSpawn19BossEffectsHide;                                         // 0x103c617d..0x103c618d
	New->ResetThinkTimers(Spawn19SpeciesNow(*this));                                     // 0x103c6193 slot 614
	New->Spawn19SetModel(VampireBossMonsterModelName);                                   // 0x103c61a4 slot 105, AGAIN
	New->EffectsWord &= ~GSpawn19BossEffectsHide;                                        // 0x103c61b2 / 0x103c61bb
	New->RenderAlphaByte = GSpawn19BossNewAlpha;                                         // 0x103c61b5 +0x1a3
	New->RenderFxWord = GSpawn19BossNewRenderFx;                                         // 0x103c61c1 +0x168
	New->RenderMode = GSpawn19BossRenderMode;                                            // 0x103c61cb +0x16c
	New->ProteanTransformOther = Handle;                                                 // 0x103c61d9 / 0x103c61e0 +0x155c
	const double Now = Spawn19SpeciesNow(*this);                                         // 0x103c61e6 / 0x103c61f6
	ProteanTransformStartTime = Now;                                                     // 0x103c61fe US
	New->ProteanTransformStartTime = Now;                                                // 0x103c6204 the new body
	New->WriteNpcStateRetail(GSpawn19BossNewState);                                      // 0x103c620a +0x5cc0 = 5
	New->WriteIdealStateRetail(GSpawn19BossNewState);                                    // 0x103c6210 +0x5cc4 = 5
	New->bIsBccTargetable = true;                                                        // 0x103c6218 +0x1480
	New->RecordScheduleEvent(FString::Printf(TEXT("SelectIdealState npc_VVampireBoss.cpp:%d"),
		GSpawn19LineBossIdeal));                                                         // 0x103c621e / 0x103c6224
	New->RecordScheduleEvent(FString::Printf(TEXT("SelectSchedule npc_VVampireBoss.cpp:%d"),
		GSpawn19LineBossSchedule));                                                      // 0x103c622e / 0x103c6234
	New->SetSchedule(GSpawn19SchedBossTransform, false);                                 // 0x103c623e -> 0x102ae750(0x158, 0) ON THE NEW BODY
	New->PlInvestigate = PlInvestigate;                                                  // 0x103c6243 / 0x103c6249
	New->PlCriminalFlee = PlCriminalFlee;                                                // 0x103c624f / 0x103c6255
	New->PlCriminalAttack = PlCriminalAttack;                                            // 0x103c625b / 0x103c6261
	New->PlSupernaturalFlee = PlSupernaturalFlee;                                        // 0x103c6267 / 0x103c626d
	New->PlSupernaturalAttack = PlSupernaturalAttack;                                    // 0x103c6273 / 0x103c627b
	New->PercentOccludedWait = PercentOccludedWait;                                      // 0x103c6281 / 0x103c6287
	New->PercentOccludedCover = PercentOccludedCover;                                    // 0x103c628d / 0x103c6293
	New->PercentOccludedWalk = PercentOccludedWalk;                                      // 0x103c6299 / 0x103c629f
	New->PercentOccludedFlank = PercentOccludedFlank;                                    // 0x103c62a5 / 0x103c62b0
	New->PercentOccludedChase = PercentOccludedChase;                                    // 0x103c62b6 / 0x103c62c4
	// `__RTDynamicCast(new, CNPC_VVampireBoss)` (descriptor `0x1062a654`, `0x103c62ca`), then
	// `+0x66b0` `m_hTransformPartner`; the cast is null on a non-boss body, which skips the write --
	// retail's own gate, not a crash guard.
	FElysiumNpcVampireBoss* const NewBoss = New->AsSpecies<FElysiumNpcVampireBoss>();     // 0x103c62ca
	if (NewBoss != nullptr)                                                              // 0x103c62ca / 0x103c62d6
	{
		NewBoss->TransformPartner = Handle;                                              // 0x103c62dc / 0x103c62e1 +0x66b0
	}
	RenderFxWord = GSpawn19BossOldRenderFx;                                              // 0x103c62e7 US +0x168
	RenderMode = GSpawn19BossRenderMode;                                                 // 0x103c62f1 US +0x16c
	ProteanTransformOther = New->Handle;                                                 // 0x103c62ff / 0x103c6304 LAST
}

// 0x103ab310 CNPC_VSabbatLeader::TransformationStart -- the model, the activity and the hull BEFORE
// the boss body.
void FElysiumNpcSabbatLeader::TransformationStart()
{
	Spawn19SetModel(GSpawn19ModelAndreiNoMouth);                                         // 0x103ab36a slot 105
	SetIdealActivity(GSpawn19SabbatLeaderActivity);                                      // 0x103ab377 -> 0x10272650(0x113e)
	EffectsWord |= GSpawn19SabbatLeaderEffects;                                          // 0x103ab37c..0x103ab38b
	RenderFxWord = 0;                                                                    // 0x103ab391 +0x168
	RenderMode = 0;                                                                      // 0x103ab397 +0x16c
	HullKind = 0;                                                                        // 0x103ab39d +0x1568 m_eHull
	PathingHullKind = 0;                                                                 // 0x103ab3a3 +0x156c m_eDefaultHull
	SetHullSizeNormal(true);                                                             // 0x103ab3a9 -> 0x10273070(1)
	++Spawn19RelinkCalls;                                                                // 0x103ab3af -> 0x101cf600
	FElysiumNpcVampireBoss::TransformationStart();                                       // 0x103ab3b9 -> 0x103c60a0
}
