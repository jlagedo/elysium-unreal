// Story 0019/8 (29e under the strict verdict), family **Select19** -- the species classes' bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Owns (Select19's `rule` rows): 0x10394120 CNPC_VMingXiao::PreSelectSchedule, 0x1035fb50
// CNPC_VAnimal::SelectSchedule, 0x10384ee0 CNPC_VHuman::SelectSchedule, 0x103941e0
// CNPC_VMingXiao::SelectSchedule, 0x103a46b0 CNPC_VPlayerController::PreSelectSchedule, 0x103aa510
// CNPC_VSabbatLeader::PreSelectSchedule, 0x103bb7c0 CNPC_VTzimisce::SelectSchedule, 0x103c1610
// CNPC_VTzimisceHeadClaw::SelectSchedule, 0x103c3310 CNPC_VTzimisceRunner::SelectSchedule,
// 0x10360eb0 CNPC_VAsianVampire::SelectSchedule, 0x1036b250 CNPC_VChangBros::SelectSchedule,
// 0x103742d0 CNPC_VDog::SelectSchedule, 0x10375d90 CNPC_VFrenzyShadow::SelectSchedule, 0x103788d0
// CNPC_VGargoyle::SelectSchedule, 0x1037d130 CNPC_VGuard1::SelectSchedule, 0x1037fca0
// CNPC_VHengeyokai::SelectSchedule, 0x103872d0 CNPC_VHumanCombatant::SelectSchedule, 0x103a29f0
// CNPC_VPedestrian::SelectSchedule, 0x103a70c0 CNPC_VSabbatLeader::SelectSchedule, 0x103ac610
// CNPC_VScurrying::SelectSchedule, 0x103ae8c0 CNPC_VSheriffMan::SelectSchedule, 0x103cee70
// CNPC_VWerewolf::SelectSchedule, 0x103dceb0 CNPC_VWolfMorph::SelectSchedule, 0x103df2e0
// CNPC_VZombie::SelectSchedule, 0x10371ee0 CNPC_VCop::SelectSchedule, 0x1037bd60
// CNPC_VGhoulCroucher::SelectSchedule, 0x10387d20 CNPC_VHumanCombatPatrol::SelectSchedule,
// 0x103dd6b0 CNPC_VYukie::SelectSchedule.
//
// The three rows whose body already landed in their class files in story 5 fold A2 —
// `CNPC_VFrenzyShadow::SelectSchedule` (`ElysiumNpcFrenzyShadow.cpp`), `CNPC_VWolfMorph::
// SelectSchedule` (`ElysiumNpcWolfMorph.cpp`) and `CNPC_VPlayerController::PreSelectSchedule`
// (`ElysiumNpcPlayerController.cpp`) — are not redefined here; their tail into `CNPC_VHuman` is an
// integrator redirect (the L06 report).
//
// A species body that retail chains to its parent through a DIRECT thunk calls the parent body by
// its qualified name (`FElysiumNpcHuman::SpeciesSelectSchedule()`, `TroikaSelectSchedule()`), never
// the virtual. Every arm carries the address of the listing instruction it came from.

#include "Substrate/ElysiumNpcAnimal.h"
#include "Substrate/ElysiumNpcAsianVampire.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcCop.h"
#include "Substrate/ElysiumNpcDog.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcGuard1.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcHuman.h"
#include "Substrate/ElysiumNpcHumanCombatPatrol.h"
#include "Substrate/ElysiumNpcHumanCombatant.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcPedestrian.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcScurrying.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcYukie.h"
#include "Substrate/ElysiumNpcZombie.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleNumbers.h"

namespace NpcSelect19Species
{
	// The `+0x1b30` source files, recorded by `SelectTrace` (the words themselves stay ABSENT).
	const TCHAR* const GFileAnimal = TEXT("NPC_VAnimal.cpp");                 // `0x1062c318`
	const TCHAR* const GFileHuman = TEXT("NPC_VHuman.cpp");                   // `0x1063f724`
	const TCHAR* const GFileMingXiao = TEXT("NPC_VMingXiao.cpp");             // `0x10647090`
	const TCHAR* const GFileTzimisce = TEXT("NPC_VTzimisce.cpp");             // `0x1065c7e0`
	const TCHAR* const GFileGargoyle = TEXT("NPC_VGargoyle.cpp");             // `0x1063a854`
	const TCHAR* const GFilePedestrian = TEXT("NPC_VPedestrian.cpp");         // `0x1064b820`
	const TCHAR* const GFileScurrying = TEXT("NPC_VScurrying.cpp");           // `0x106502e0`
	const TCHAR* const GFileWerewolf = TEXT("NPC_VWerewolf.cpp");             // `0x10662220`
	const TCHAR* const GFileZombie = TEXT("NPC_VZombie.cpp");                 // `0x106655c0`
	const TCHAR* const GFileCop = TEXT("NPC_VCop.cpp");                       // `0x106366c0`
	const TCHAR* const GFileGhoulCroucher = TEXT("NPC_VGhoulCroucher.cpp");   // `0x1063b2e0`
	const TCHAR* const GFileCombatPatrol = TEXT("npc_VHumanCombatPatrol.cpp");   // `0x1063fcf4`

	// Retail NPC states.
	constexpr int32 GStateIdle = 1;
	constexpr int32 GStateCombat = 2;
	constexpr int32 GStateAlert = 3;
	constexpr int32 GStateTransform = 5;
	constexpr int32 GStateCrimSusp = 0xc;

	constexpr int32 GDispositionHate = 1;   // `D_HT`

	// The weapon word's melee mask (`TEST EAX,0x18000`).
	constexpr uint32 GWeaponMeleeBits = 0x18000u;

	// Cells read out of the pinned image (`vampire.dll`, image base `0x10000000`), none of them a row
	// of `ElysiumNpcKernelTunables.h`.
	constexpr double GInvestigateSoundDelay = 2.0;     // `_DAT_10452dc4` f32
	constexpr float GSabbatPlayerOffsetUnits = 20.0f;  // `_DAT_1044eb0c` f32
	constexpr float GSheriffHealthLostFloor = 0.05f;   // `_DAT_104c6140` f32
	constexpr float GSheriffAttackIdleSeconds = 3.0f;  // `DAT_104c6150` dword `0x40400000`
	constexpr double GRunnerAdvanceDistance = 256.0;   // `_DAT_104cdce8` f64
	constexpr float GPedestrianSoundNearSqr = 256.0f;  // `_DAT_1044ddb0` f32, compared SQUARED
	constexpr float GMeleeFarSqr = 160000.0f;          // `_DAT_104b73e8` f32
	constexpr float GMeleeNearSqr = 40000.0f;          // `_DAT_104b73e4` f32
	constexpr float GMingXiaoChargeRange = ElysiumNpcTunables::OneTwenty;   // `_DAT_1044f00c` = 120.0

	// Class-local conditions (each class's own `space.json` registrations).
	constexpr EElysiumNpcCond GCondDogShouldSnarl = static_cast<EElysiumNpcCond>(0x7b);         // COND_VDOG_SHOULD_SNARL
	constexpr EElysiumNpcCond GCondDogPlayerBefriended = static_cast<EElysiumNpcCond>(0x7d);    // COND_VDOG_PLAYER_BEFRIENDED
	constexpr EElysiumNpcCond GCondScurryPlayerTooClose = static_cast<EElysiumNpcCond>(0x78);   // COND_VSCURRYING_PLAYER_TOOCLOSE
	constexpr EElysiumNpcCond GCondZombiePlayerAttacked = static_cast<EElysiumNpcCond>(0x79);   // COND_VZOMBIE_PLAYER_ATTACKED
	constexpr EElysiumNpcCond GCondMingCanAttackFirst = static_cast<EElysiumNpcCond>(0x77);     // COND_VMING_XIAO_CAN_ATTACK_FRONT_RIGHT
	constexpr EElysiumNpcCond GCondMingCanSpit = static_cast<EElysiumNpcCond>(0x7d);            // COND_VMING_XIAO_CAN_ATTACK_SPIT
	constexpr EElysiumNpcCond GCondMingMeleeHelpless = static_cast<EElysiumNpcCond>(0x7e);      // COND_VMING_XIAO_MELEE_HELPLESS
	constexpr EElysiumNpcCond GCondTzimDropBody = static_cast<EElysiumNpcCond>(0x77);           // COND_VTZIMISCE_SHOULD_DROP_BODY
	constexpr EElysiumNpcCond GCondTzimForceThrow = static_cast<EElysiumNpcCond>(0x78);         // COND_VTZIMISCE_FORCE_THROW_BODY
	constexpr EElysiumNpcCond GCondWolfCanTeleport = static_cast<EElysiumNpcCond>(0x77);        // COND_VWEREWOLF_CAN_TELEPORT
	constexpr EElysiumNpcCond GCondWolfCanSpecialMove = static_cast<EElysiumNpcCond>(0x78);     // COND_VWEREWOLF_CAN_SPECIAL_MOVE
	constexpr EElysiumNpcCond GCondWolfDeathTriggered = static_cast<EElysiumNpcCond>(0x7a);     // COND_VWEREWOLF_DEATH_TRIGGERED
	constexpr EElysiumNpcCond GCondWolfShouldBreakHint = static_cast<EElysiumNpcCond>(0x7b);    // COND_VWEREWOLF_SHOULD_BREAKHINT
	constexpr EElysiumNpcCond GCondChangJumpAttack = static_cast<EElysiumNpcCond>(0x79);        // COND_VCHANGBROS_TIME_TO_JUMP_ATTACK
	constexpr EElysiumNpcCond GCondChangTeleport = static_cast<EElysiumNpcCond>(0x7a);          // COND_VCHANGBROS_TIME_TO_TELEPORT
	constexpr EElysiumNpcCond GCondChangUnited = static_cast<EElysiumNpcCond>(0x7c);            // COND_VCHANGBROS_TIME_TO_UNITED_ATTACK
	constexpr EElysiumNpcCond GCondSabbatTimeToJump = static_cast<EElysiumNpcCond>(0x79);       // COND_VSABBATLEADER_TIME_TO_JUMP
	constexpr EElysiumNpcCond GCondHaveEnemyThrowLos = static_cast<EElysiumNpcCond>(0x1b);      // COND_HAVE_ENEMY_THROW_LOS

	int32 Roll(int32 Min, int32 Max)
	{
		return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(Min, Max);
	}

	FElysiumEntity* Resolve(const FElysiumNpc& Npc, const FElysiumEntityHandle& Handle)
	{
		return Npc.World != nullptr && Handle.IsSet() ? Npc.World->Resolve(Handle) : nullptr;
	}

	// Slot 167 `GetEnemy() const` (`vtable +0x29c`); a const receiver selects it over slot 168.
	FElysiumEntity* Slot167Enemy(const FElysiumNpc& Npc)
	{
		return Npc.GetEnemy();
	}

	bool HasInterrupt(FElysiumNpc& Npc, EElysiumNpcCond Cond)
	{
		return ElysiumSchedule::HasInterruptCondition(Npc.Schedule, Npc, Npc.Cognition.Conditions, Cond);
	}

	double Now(const FElysiumNpc& Npc)
	{
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}

	// Squared distance between two origins in SOURCE units (the port's origins are centimetres).
	float DistSqrUnits(const FVector& ACm, const FVector& BCm)
	{
		return static_cast<float>(FVector::DistSquared(ACm, BCm) / (ElysiumMove::U * ElysiumMove::U));
	}

	// The shared weapon split of `CNPC_VHuman` and its three children: the active weapon's slot `+0x5a0`
	// word (0 with no weapon) decides slot 604 (`+0x970`, melee) or slot 605 (`+0x974`, ranged), and
	// both receive the word (second judge, pass R).
	int32 WeaponSplit(FElysiumNpc& Npc)
	{
		const uint32 Word = Npc.SelectActiveWeaponWord();
		if ((Word & GWeaponMeleeBits) != 0)
		{
			return Npc.SelectScheduleMeleeCombat(static_cast<int32>(Word));
		}
		return Npc.SelectScheduleRangedCombat(static_cast<int32>(Word));
	}

	// `0x1017e6f0(player, 0)` — `cvar_pl_investigate_level.SetValue(-1)` then the player's
	// `m_LevelInvestigateAct (+0x1cdc) = 0`. The player half is `ElysiumLaw::SetInvestigateLevel`; the
	// ConVar half has no port ConVar (named, not written). A null player is retail's own fault arm
	// (it writes through `this`); the port skips it (crash guard).
	void ClearPlayerInvestigateLevel(FElysiumPlayer* Player)
	{
		if (Player != nullptr)
		{
			ElysiumLaw::SetInvestigateLevel(*Player, 0);
		}
	}
}

// =================================================================================================
// `CNPC_VAnimal::SelectSchedule` `0x1035fb50`, 213 bytes.
// =================================================================================================

int32 FElysiumNpcAnimal::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	SelectScheduleSelector = 5;                                    // 0x1035fb5a
	if (NpcFlags.Has(EElysiumNpcFlag::DO_STARTLED))                // 0x1035fb6b
	{
		NpcFlags.Clear(EElysiumNpcFlag::DO_STARTLED);              // 0x1035fb77
		return SelectTrace(GFileAnimal, 0x204, 0x15d);             // 0x1035fb7d SCHED_VANIMAL_STARTLED
	}
	const int32 State = NpcStateRetail();
	if (State == GStateIdle)                                       // 0x1035fb97
	{
		int32 PatrolSchedule = 0;
		if (SelectPatrolPathObject(PatrolSchedule))                // 0x1035fb9c (+0x6590)
		{
			return PatrolSchedule;                                 // 0x1035fba1 — no stamp
		}
		if (bUseInteresting)                                       // 0x1035fbc4
		{
			if (NavigatorPathType() != 9                           // 0x1035fbd3
				&& CurrentSpotIndex == INDEX_NONE)                 // 0x1035fbe3 (+0x62ec)
			{
				return SelectTrace(GFileAnimal, 0x217, 0x156);     // 0x1035fbe5 SCHED_VANIMAL_WALK_TO_INTERESTING_PLACE_SETUP
			}
			return SelectTrace(GFileAnimal, 0x213, 0x157);         // 0x1035fbef SCHED_VANIMAL_WALK_TO_INTERESTING_PLACE
		}
	}
	else if (State == GStateAlert)                                 // 0x1035fba7 / 0x1035fbb2
	{
		if (const int32 Unknown = SelectUnknownAlertSchedule(); Unknown != 0)   // 0x1035fba9 CALL 0x102b8a60
		{
			return Unknown;
		}
		if (const int32 Sound = SelectSoundAlertSchedule(); Sound != 0)   // 0x1035fbb4? CALL 0x102b9060
		{
			return Sound;
		}
	}
	return TroikaSelectSchedule();                                 // 0x1035fc0a JMP 0x10015596
}

// =================================================================================================
// `CNPC_VDog::SelectSchedule` `0x103742d0`, 128 bytes.
// =================================================================================================

int32 FElysiumNpcDog::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	SelectScheduleSelector = 0xd;                                  // 0x103742d6
	if (Cognition.Conditions.Has(GCondDogShouldSnarl))             // 0x103742e6
	{
		Cognition.Conditions.Clear(GCondDogShouldSnarl);           // 0x103742ea CALL 0x10269b50
		return 0x166;                                              // 0x103742f2 SCHED_VDOG_SNARL
	}
	bool bAskBefriended = false;
	switch (NpcStateRetail())                                      // 0x103742f8
	{
	case GStateIdle:                                               // 0x103742ff
	{
		int32 PatrolSchedule = 0;
		if (SelectPatrolPathObject(PatrolSchedule))                // 0x10374302 (+0x6590)
		{
			return PatrolSchedule;                                 // 0x10374314
		}
		if (!bUseInteresting)                                      // 0x10374305 (+0x63d9)
		{
			return 0x164;                                          // 0x10374307 SCHED_VDOG_LOITER
		}
		bAskBefriended = true;
		break;
	}
	case GStateCombat:                                             // 0x1037431b
		Cognition.bCondTookDamage = false;
		break;
	case GStateAlert:                                              // 0x10374312
		bAskBefriended = true;
		break;
	default:
		break;
	}
	if (bAskBefriended && HasInterrupt(*this, GCondDogPlayerBefriended))   // 0x10374322 / 0x1037432c
	{
		return 0x167;                                              // SCHED_VDOG_MADEFRIEND
	}
	const int32 Animal = FElysiumNpcAnimal::SpeciesSelectSchedule();   // 0x1037433d CALL 0x1035fb50
	if (Animal == ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION)     // 0x1037434c CMP EAX,0x6b
	{
		return 0x164;                                              // SCHED_VDOG_LOITER
	}
	return Animal;
}

// =================================================================================================
// `CNPC_VScurrying::SelectSchedule` `0x103ac610`, 238 bytes.
// =================================================================================================

int32 FElysiumNpcScurrying::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	const double T = Now(*this);
	SelectScheduleSelector = 0x20;                                 // 0x103ac616
	if (T < ScurryingFrightEndTime)                                // 0x103ac630 JP (curtime < m_flFrightEndTime)
	{
		return SelectTrace(GFileScurrying, 0xbe, 0x162);           // 0x103ac632 SCHED_VSCURRYING_EVADE
	}
	if (Cognition.Conditions.Has(GCondScurryPlayerTooClose))       // 0x103ac658
	{
		return SelectTrace(GFileScurrying, 0xc3, 0x162);           // 0x103ac65a
	}
	if (Cognition.Conditions.Has(EElysiumNpcCond::HearBulletImpact))   // 0x103ac680
	{
		// `0x101b99d0(&m_LastSoundBulletImpact)` — the sound record's origin.
		ScurryingFrightOrigin = Senses.Memory.LastSoundBulletImpact.Position;   // 0x103ac68b..0x103ac6a6
		ScurryingFrightEndTime = static_cast<double>(ScurryingFrightDurationSeconds) + T;   // 0x103ac6c9
		return SelectTrace(GFileScurrying, 0xca, 0x162);           // 0x103ac6ae
	}
	int32 Animal = FElysiumNpcAnimal::SpeciesSelectSchedule();     // 0x103ac6d7
	if (Animal == ElysiumSched::SCHED_TROIKA_IDLE_DISPOSITION)     // 0x103ac6e1
	{
		SelectTrace(GFileScurrying, 0xd1, 0x161);                  // 0x103ac6e3
		Animal = 0x161;                                            // SCHED_VSCURRYING_LOITER
	}
	return Animal;
}

// =================================================================================================
// `CNPC_VZombie::SelectSchedule` `0x103df2e0`, 524 bytes.
// =================================================================================================

int32 FElysiumNpcZombie::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	const FElysiumNpcConditions& Cond = Cognition.Conditions;
	SelectScheduleSelector = 0x2b;                                 // 0x103df2e6
	if (bZombieNeedsCrawlOutOfGround)                              // 0x103df2f6
	{
		return SelectTrace(GFileZombie, 0x145, ZombieCrawlScheduleRetailId);   // 0x103df2f8 0x161
	}
	if ((EffectsWord & 0x40u) != 0)                                // 0x103df31f `m_fEffects & EF_NODRAW`
	{
		Unhide();                                                  // 0x103df325 slot 67, vtable +0x10c
	}
	if (!IsAlive())                                                // 0x103df32d slot 158 / 0x103df337
	{
		CreateCorpse(ZombieDeathForceVector, &ZombieDeathDamageInfo);   // 0x103df349 slot 301
	}
	if (ZombieAiType == 8 || ZombieAiType == 7)                    // 0x103df357 / 0x103df35c
	{
		// `LIGHT_DAMAGE` is asked twice in a row (`0x103df369`, `0x103df376`); retail's, reproduced.
		if (Cond.Has(EElysiumNpcCond::LightDamage)                 // 0x103df369
			|| Cond.Has(EElysiumNpcCond::LightDamage)              // 0x103df376
			|| Cond.Has(EElysiumNpcCond::HeavyDamage)              // 0x103df383
			|| Cond.Has(GCondZombiePlayerAttacked))                // 0x103df390
		{
			ZombieAiType = 1;                                      // 0x103df3bb
		}
		else if (!Cond.Has(EElysiumNpcCond::GiveWay))              // 0x103df39d
		{
			return SelectTrace(GFileZombie, 0x160, 0x16b);         // 0x103df39f SCHED_VZOMBIE_FEAR_SOMETHING
		}
	}
	const int32 State = NpcStateRetail();                          // 0x103df3c5
	if (State == GStateIdle)                                       // 0x103df3ce
	{
		if (ZombieAiType == 1)                                     // 0x103df44e
		{
			SetState(GStateCombat);                                // 0x103df457 CALL 0x1026e340(2)
			if (Cond.Has(EElysiumNpcCond::SeeEnemy))               // 0x103df45d
			{
				return SelectTrace(GFileZombie, 0x171, 0x165);     // SCHED_VZOMBIE_MELEE_ATTACK
			}
			return SelectTrace(GFileZombie, 0x175, 0x169);         // SCHED_VZOMBIE_IDLE
		}
		if (ZombieAiType == 5)                                     // 0x103df453
		{
			return SelectTrace(GFileZombie, 0x17b, 0x169);         // 0x103df482
		}
		int32 PatrolSchedule = 0;
		if (SelectPatrolPathObject(PatrolSchedule))                // 0x103df4b2 (+0x6590)
		{
			return SelectTrace(GFileZombie, 0x181, PatrolSchedule);   // 0x103df4b4
		}
	}
	else if (State > GStateIdle && State < 4)                      // 0x103df3d0 JLE / 0x103df3d9 JG
	{
		// Slot 167's enemy and its `+0xa8 m_pPlayer`: only a PLAYER enemy passes, and then only when
		// `0x10146a80(player)` (the obfuscation test) answers false.
		FElysiumEntity* const Enemy = Slot167Enemy(*this);         // 0x103df3df
		FElysiumPlayer* const Player = World != nullptr ? World->FindPlayer() : nullptr;
		const bool bPlayerEnemy = Enemy != nullptr && Player != nullptr
			&& static_cast<FElysiumEntity*>(Player) == Enemy;      // 0x103df3eb / 0x103df3f5
		if (bPlayerEnemy && Cond.Has(EElysiumNpcCond::SeeEnemy)    // 0x103df402
			&& !Player->IsObfuscatedForSenses())                   // 0x103df40d CALL 0x10146a80
		{
			return SelectTrace(GFileZombie, 400, 0x165);           // 0x103df40f
		}
		return SelectTrace(GFileZombie, 0x194, 0x169);             // 0x103df42b
	}
	return FElysiumNpcAnimal::SpeciesSelectSchedule();             // 0x103df4d0 JMP 0x1035fb50
}

// =================================================================================================
// `CNPC_VHuman::SelectSchedule` `0x10384ee0`, 340 bytes. Carries `CNPC_VBrujah`, `CNPC_VLasombra`,
// `CNPC_VPlayerController`, `CNPC_VVampire` and `CNPC_VVampireBoss` by inheritance.
// =================================================================================================

int32 FElysiumNpcHuman::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	SelectScheduleSelector = 0x14;                                 // 0x10384ee9
	if (NpcStateRetail() == GStateCombat)                          // 0x10384ef3 / 0x10384ef6
	{
		Cognition.bCondTookDamage = false;                         // 0x10384efe
		if (Cognition.Conditions.Has(EElysiumNpcCond::DetectedAttack))   // 0x10384f05 / 0x10384f0c
		{
			// `GetEnemies()` (slot 541) `0x102dfa20(memory, attacker)` — does this NPC's enemy memory
			// hold the detected attacker? (`+0x65c0`, resolved; a stale handle is a null entity.)
			FElysiumEntity* const Attacker = Resolve(*this, Senses.Memory.DetectedAttackAttacker);   // 0x10384f12..3e
			const FElysiumNpcEnemyMemoryRecord* const Record =
				Attacker != nullptr ? EnemyMemory.Find(Attacker->Handle) : nullptr;   // 0x10384f45 / 0x10384f4d
			if (Record == nullptr)                                 // 0x10384f54
			{
				return SelectTrace(GFileHuman, 0x269, 0x56);       // 0x10384fcf SCHED_TROIKA_ALERT_TURN_TO_DETECTED_ATTACK
			}
			// `0x102e0150` LastTimeSeen; `FLD 1.0 / FADD / FLD curtime / FCOMPP / AND 0x100` — below
			// `seen + 1.0` (or unordered) keeps selecting; at or past it turns to the attack.
			if (!(Now(*this) < Record->LastSeenTime + ElysiumNpcTunables::OneDouble))   // 0x10384f91..0x10384fb2
			{
				return SelectTrace(GFileHuman, 0x261, 0x56);       // 0x10384fb4
			}
		}
		if (const int32 Weapon = WeaponSplit(*this); Weapon != 0)  // 0x10384fec..0x1038502a
		{
			return Weapon;
		}
	}
	return TroikaSelectSchedule();                                 // 0x1038502f JMP 0x10015596
}

// =================================================================================================
// `CNPC_VHumanCombatant::SelectSchedule` `0x103872d0`, 103 bytes. Carries `CNPC_ProneDialog` and
// `CNPC_VSabbatGunman`.
// =================================================================================================

int32 FElysiumNpcHumanCombatant::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	SelectScheduleSelector = 0x15;                                 // 0x103872d6
	if (NpcStateRetail() == GStateCombat)                          // 0x103872e6
	{
		Cognition.bCondTookDamage = false;                         // 0x103872e8
		if (const int32 Weapon = WeaponSplit(*this); Weapon != 0)  // 0x103872f6..0x1038732d
		{
			return Weapon;                                         // 0x10387324
		}
	}
	return FElysiumNpcHuman::SpeciesSelectSchedule();              // 0x1038732f
}

// =================================================================================================
// `CNPC_VYukie::SelectSchedule` `0x103dd6b0`, 103 bytes.
// =================================================================================================

int32 FElysiumNpcYukie::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	SelectScheduleSelector = 0x2a;                                 // 0x103dd6b6
	if (NpcStateRetail() == GStateCombat)                          // 0x103dd6c6
	{
		Cognition.bCondTookDamage = false;                         // 0x103dd6c8
		if (const int32 Weapon = WeaponSplit(*this); Weapon != 0)  // 0x103dd6d6..0x103dd70d
		{
			return Weapon;                                         // 0x103dd704
		}
	}
	return FElysiumNpcHumanCombatant::SpeciesSelectSchedule();     // 0x103dd70f
}

// =================================================================================================
// `CNPC_VHumanCombatPatrol::SelectSchedule` `0x10387d20`, 233 bytes.
// =================================================================================================

int32 FElysiumNpcHumanCombatPatrol::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	const FElysiumNpcConditions& Cond = Cognition.Conditions;
	SelectScheduleSelector = 0x16;                                 // 0x10387d26
	const int32 State = NpcStateRetail();
	int32 PatrolSchedule = 0;
	if (State == GStateCombat)                                     // 0x10387d36
	{
		if (!IsBusyWithDiscipline()                                // 0x10387d7c
			&& (!Cond.Has(EElysiumNpcCond::SeeEnemy)               // 0x10387d89
				|| Cond.Has(EElysiumNpcCond::EnemyOccluded))       // 0x10387d96
			&& SelectPatrolPathObject(PatrolSchedule))             // 0x10387da0 (+0x6590)
		{
			// The node's `m_iSchedule` is returned as it stands, zero included (no `+4 != 0` test here).
			return SelectTrace(GFileCombatPatrol, 0x121, PatrolSchedule);   // 0x10387da2
		}
		if (const int32 Weapon = WeaponSplit(*this); Weapon != 0)  // 0x10387dc4..0x10387dfb
		{
			return Weapon;                                         // 0x10387df2
		}
	}
	else if (State == GStateAlert                                  // 0x10387d39
		&& !IsBusyWithDiscipline()                                 // 0x10387d46
		&& SelectPatrolPathObject(PatrolSchedule))                 // 0x10387d54
	{
		return SelectTrace(GFileCombatPatrol, 0x116, PatrolSchedule);   // 0x10387d5a
	}
	return FElysiumNpcHumanCombatant::SpeciesSelectSchedule();     // 0x10387e01
}

// =================================================================================================
// `CNPC_VGhoulCroucher::SelectSchedule` `0x1037bd60`, 215 bytes. Writes no selector id.
// =================================================================================================

int32 FElysiumNpcGhoulCroucher::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	if (!IsDisturbed())                                            // 0x1037bdc9 / 0x1037bdd0
	{
		return SelectTrace(GFileGhoulCroucher, 0x23f, 0x158);      // 0x1037bdd2 SCHED_VGHOUL_CROUCHER_UNAWARE
	}
	if (!bUnawareExited)                                           // 0x1037bdff
	{
		return SelectTrace(GFileGhoulCroucher, 0x243, 0x159);      // 0x1037be01 SCHED_VGHOUL_CROUCHER_UNAWARE_EXIT
	}
	return FElysiumNpcHumanCombatant::SpeciesSelectSchedule();     // 0x1037be26
}

// =================================================================================================
// `CNPC_VGuard1::SelectSchedule` `0x1037d130`, 203 bytes.
// =================================================================================================

int32 FElysiumNpcGuard1::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	SelectScheduleSelector = 0x12;                                 // 0x1037d136
	if (NpcFlags.Has(EElysiumNpcFlag::DO_STARTLED))                // 0x1037d14b
	{
		NpcFlags.Clear(EElysiumNpcFlag::DO_STARTLED);              // 0x1037d14d
		return 0xf1;                                               // 0x1037d156 SCHED_TROIKA_STARTLED — no stamp
	}
	if (NpcStateRetail() == GStateCrimSusp)                        // 0x1037d163
	{
		if (HasInterrupt(*this, EElysiumNpcCond::InvestigateLevel)   // 0x1037d174
			&& Resolve(*this, Senses.Memory.ClosestPlayer) != nullptr)   // 0x1037d17f / 0x1037d1a1 / 0x1037d1a7
		{
			// The second, serial-only resolution (`0x1037d1b2` / `0x1037d1c9`) cannot fail once the
			// first passed; its null arm (`0x1037d1db`, helper on a null `this`) is unreachable here.
			ClearPlayerInvestigateLevel(SelectResolvePlayer(Senses.Memory.ClosestPlayer));   // 0x1037d1cf CALL 0x1000c838
			return 0x6d;                                           // 0x1037d1d4 SCHED_TROIKA_START_PLAYER_DIALOG
		}
		SetState(GStateIdle);                                      // 0x1037d1ee CALL 0x1026e340(1)
	}
	return FElysiumNpcHuman::SpeciesSelectSchedule();              // 0x1037d1f6 JMP 0x10015ad2
}

// =================================================================================================
// `CNPC_VCop::SelectSchedule` `0x10371ee0`, 482 bytes.
// =================================================================================================

int32 FElysiumNpcCop::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	SelectScheduleSelector = 0xc;                                  // 0x10371ee6
	const int32 State = NpcStateRetail();
	if (State == GStateIdle)                                       // 0x10371ef6
	{
		if (bCameFromSpawner)                                      // 0x10371eff
		{
			int32 PatrolSchedule = 0;
			const bool bPatrol = SelectPatrolPathObject(PatrolSchedule);   // 0x10371f05 (+0x6590)
			FElysiumEntity* const Player = Resolve(*this, Senses.Memory.ClosestPlayer);   // 0x10371f0e..3f
			bool bBudget = !bPatrol;                               // 0x1037204b
			if (Player != nullptr
				&& !PlayerHeightenedAlert(Player)                  // 0x10371fd0 CALL 0x1017f8d0 / 0x10371fde
				&& PlayerCopsInPursuitCount(Player) == 0)          // 0x1037201f CALL 0x1017f770 / 0x1037204b
			{
				bBudget = true;
			}
			if (bBudget)                                           // LAB_10372055
			{
				if (!bCopCountedSecond)                            // 0x10372055 / 0x1037205d (+0x6672)
				{
					if (CopAliveCensus() - CopSecondCensus() <= 3)   // 0x1037205f..0x10372070 JG
					{
						return SelectTrace(GFileCop, 0x412, 0x170);   // 0x10372072 SCHED_VCOP_WANDER_PATROL
					}
					bCopCountedSecond = true;                      // 0x10372093
					++CopSecondCensus();                           // 0x1037209a INC [0x1093acb0]
				}
				return SelectTrace(GFileCop, 0x40e, 0x16e);        // 0x103720a5 SCHED_VCOP_WANDER_AND_VANISH
			}
		}
	}
	else if (State == GStateCrimSusp)                              // 0x10371f86? / 0x10371fa1
	{
		if (HasInterrupt(*this, EElysiumNpcCond::InvestigateLevel)   // 0x10371fba
			&& Resolve(*this, Senses.Memory.ClosestPlayer) != nullptr)
		{
			ClearPlayerInvestigateLevel(SelectResolvePlayer(Senses.Memory.ClosestPlayer));   // CALL 0x1017e6f0
			return 0x6d;                                           // SCHED_TROIKA_START_PLAYER_DIALOG
		}
		SetState(GStateIdle);                                      // CALL 0x1026e340(1)
	}
	return FElysiumNpcHumanCombatant::SpeciesSelectSchedule();     // LAB_10371f8f
}

// =================================================================================================
// `CNPC_VPedestrian::SelectSchedule` `0x103a29f0`, 430 bytes.
// =================================================================================================

int32 FElysiumNpcPedestrian::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	const FElysiumNpcConditions& Cond = Cognition.Conditions;
	SelectScheduleSelector = 0x1d;                                 // 0x103a29f6
	if (bPedestrianFirstThink)                                     // 0x103a2a05
	{
		bPedestrianFirstThink = false;                             // 0x103a2a07
		return SelectTrace(GFilePedestrian, 0x1b4, 0xfe);          // SCHED_TROIKA_START_WAITING
	}
	if (Cond.Has(EElysiumNpcCond::PassOut)                         // 0x103a2a34
		&& !IsBusyWithDiscipline())                                // 0x103a2a3f
	{
		return SelectTrace(GFilePedestrian, 0x1b9, 0xfa);          // 0x103a2a41 SCHED_TROIKA_KNOCKOUT
	}
	if (NpcFlags.Has(EElysiumNpcFlag::DO_STARTLED))                // 0x103a2a6a
	{
		NpcFlags.Clear(EElysiumNpcFlag::DO_STARTLED);
		return SelectTrace(GFilePedestrian, 0x1c0, 0xf1);          // 0x103a2a6c SCHED_TROIKA_STARTLED
	}
	const int32 State = NpcStateRetail();
	if ((State == GStateIdle || State == GStateAlert)              // 0x103a2a96 / 0x103a2a9b
		&& Cond.Has(EElysiumNpcCond::InvestigateSound))            // 0x103a2aac
	{
		Senses.Memory.NextInvestigateSoundTime = Now(*this) + GInvestigateSoundDelay;   // 0x103a2ac1
		Senses.CommitBestSound(*this, Cognition.Conditions);       // 0x103a2ac7
		// Slot 220 `GetOrigin()` against `m_BestSound.m_vecOrigin` (`+0x60d0`), SQUARED against 256.0
		// (second judge: C0 set — below or unordered — goes straight to `0x157`).
		const float DistSqr = DistSqrUnits(Origin, Senses.Memory.BestSound.Position);   // 0x103a2ad2..0x103a2b15
		if (DistSqr >= GPedestrianSoundNearSqr                     // 0x103a2b15 JNZ
			&& !NpcFlags.Has(EElysiumNpcFlag::SKIPPED_SOUND)       // 0x103a2b25
			&& Roll(0, 99) < 0x19)                                 // 0x103a2b39 JGE
		{
			SelectTrace(GFilePedestrian, 0x1d7, 0x158);            // 0x103a2b3b
			NpcFlags.Set(EElysiumNpcFlag::SKIPPED_SOUND);          // 0x103a2b50 OR 0x100000
			return 0x158;                                          // SCHED_VPEDESTRIAN_WAIT_HEARD_SOUND
		}
		SelectTrace(GFilePedestrian, 0x1de, 0x157);                // 0x103a2b67
		NpcFlags.Clear(EElysiumNpcFlag::SKIPPED_SOUND);            // 0x103a2b80 AND 0xffefffff
		NpcFlags.Set(EElysiumNpcFlag::INITIAL_FLEE);               //            OR 0x100
		return 0x157;                                              // SCHED_VPEDESTRIAN_TURN_TO_SOUND
	}
	return FElysiumNpcHuman::SpeciesSelectSchedule();              // 0x103a2b96
}

// =================================================================================================
// `CNPC_VAsianVampire::SelectSchedule` `0x10360eb0`, 326 bytes. No file/line stamps.
// =================================================================================================

int32 FElysiumNpcAsianVampire::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	SelectScheduleSelector = 6;                                    // 0x10360f07
	FElysiumEntity* const Player = Resolve(*this, Senses.Memory.ClosestPlayer);   // 0x10360f14..37
	if (Player != nullptr && IRelationType(Player) != GDispositionHate)   // 0x10360f41 slot 404 / 0x10360f47
	{
		return FElysiumNpcHuman::SpeciesSelectSchedule();          // 0x10360f49
	}
	if (Select19ConVarEnabled(ESelect19ConVar::AsianVampForceJumpUp))   // 0x10360f67 / 0x10360f73
	{
		return 0x15a;                                              // 0x10360f75 SCHED_VASIANVAMPIRE_JUMP_UP
	}
	if (!bAsianVampirePathBlocked)                                 // 0x10360f90 (+0x66d4)
	{
		if (!StationaryForTooLong()                                // 0x10360fc7
			&& !StandingOnPlayer())                                // 0x10360fd2
		{
			return FElysiumNpcHuman::SpeciesSelectSchedule();      // 0x10360fd4
		}
	}
	else if (bInMelee)                                             // 0x10360f9a (+0x6078)
	{
		Slot601(Slot167Enemy(*this));                              // 0x10360fa9 vtable +0x964
		return 0x15c;                                              // 0x10360faf SCHED_VASIANVAMPIRE_SWITCH_TO_RANGED
	}
	return GetJumpSchedule();                                      // 0x10360fe7 CALL 0x10362430
}

// =================================================================================================
// `CNPC_VChangBros::SelectSchedule` `0x1036b250`, 403 bytes. Carries `CNPC_VChangBrosBlade` and
// `CNPC_VChangBrosClaw`. No file/line stamps.
// =================================================================================================

int32 FElysiumNpcChangBros::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	const FElysiumNpcConditions& Cond = Cognition.Conditions;
	SelectScheduleSelector = 10;                                   // 0x1036b2b2
	// `0x100290c0(g_EntityList, &m_hClosestPlayer)` — the handle resolved.
	if (FElysiumEntity* const Player = Resolve(*this, Senses.Memory.ClosestPlayer))   // 0x1036b2bf
	{
		if (IRelationType(Player) != GDispositionHate)             // 0x1036b2c8 slot 404 / 0x1036b2cf
		{
			return FElysiumNpcHuman::SpeciesSelectSchedule();      // LAB_1036b3d2
		}
	}
	if (Select19ConVarEnabled(ESelect19ConVar::ChangBrosForceUnitedAttack))   // 0x1036b2e2 / 0x1036b2ef
	{
		return 0x15e;                                              // SCHED_VCHANGBROS_UNITED_MOVE
	}
	if (Select19ConVarEnabled(ESelect19ConVar::ChangBrosForceTeleport))       // 0x1036b30f / 0x1036b31b
	{
		return 0x15a;                                              // SCHED_VCHANGBROS_TELEPORT
	}
	if (Select19ConVarEnabled(ESelect19ConVar::ChangBrosForceLedgeAttack))    // 0x1036b33b / 0x1036b347
	{
		return 0x15c;                                              // SCHED_VCHANGBROS_JUMP_TO_LEDGE
	}
	if (Cond.Has(GCondChangUnited))                                // 0x1036b365
	{
		return 0x15e;
	}
	if (Cond.Has(GCondChangJumpAttack))                            // 0x1036b383
	{
		return 0x15c;
	}
	if (Cond.Has(GCondChangTeleport))                              // 0x1036b3a1
	{
		return 0x15a;
	}
	if (Cond.Has(EElysiumNpcCond::EnemyUnreachable))               // 0x1036b3bf
	{
		return 0x15d;                                              // SCHED_VCHANGBROS_SUPER_JUMP
	}
	return FElysiumNpcHuman::SpeciesSelectSchedule();              // 0x1036b3d2 CALL 0x10384ee0
}

// =================================================================================================
// `CNPC_VGargoyle::SelectSchedule` `0x103788d0`, 263 bytes.
// =================================================================================================

bool FElysiumNpcGargoyle::Select19GargoyleFindPillar()
{
	GargoylePillarTarget = FElysiumEntityHandle::Invalid();       // `0x10378ec0` miss arm: `+0x667c = -1`
	return false;
}

bool FElysiumNpcGargoyle::Select19GargoyleHasPath(const FVector& FromUnits, const FVector& ToUnits) const
{
	(void)FromUnits;
	(void)ToUnits;
	return false;
}

int32 FElysiumNpcGargoyle::Select19GargoyleFindPillarSchedule()
{
	// `0x10378f80`.
	if (Select19GargoyleFindPillar())                              // CALL 0x10378ec0
	{
		NpcFlags.Set(EElysiumNpcFlag::FINDING_BODY);               // CALL 0x10379000(1): `+0x14b8 |= 0x10`
		return 0x15a;                                              // SCHED_VGARGOYLE_FIND_PILLAR
	}
	GargoyleShunnedFindPillar = 2;                                 // `+0x6680 = 2`
	return 0;
}

int32 FElysiumNpcGargoyle::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	SelectScheduleSelector = 0x11;                                 // 0x103788d8
	// 0x103788e0..0x10378946: the first stat list whose `+0x10` is 0 (else the lazily-built empty
	// global `DAT_109f0b40`) and `CVStatList_t::IsEqual(list, 0x0f, 0x11)` — the dead test, which this
	// runtime spells `IsInert() || HasReportedDeath()` (family Anim10, `PlayReaction`).
	if (IsInert() || HasReportedDeath())                           // 0x10378946
	{
		return SelectTrace(GFileGargoyle, 0x174, 0x15c);           // 0x10378948 SCHED_VGARGOYLE_DEATH
	}
	if (NpcStateRetail() == GStateCombat)                          // 0x10378974
	{
		FElysiumEntity* const Enemy = Slot167Enemy(*this);         // 0x1037897a
		if (Enemy != nullptr                                       // 0x10378984
			&& !IsUnreachable(Enemy)                               // 0x1037898b slot 530 / 0x10378993
			&& Select19GargoyleHasPath(Origin / ElysiumMove::U, Enemy->Origin / ElysiumMove::U))   // 0x103789b8 CALL 0x102ee380 / 0x103789bf
		{
			return FElysiumNpcHuman::SpeciesSelectSchedule();      // LAB_103789cc
		}
		if (const int32 Pillar = Select19GargoyleFindPillarSchedule(); Pillar != 0)   // 0x103789c3 / 0x103789ca
		{
			return Pillar;
		}
	}
	return FElysiumNpcHuman::SpeciesSelectSchedule();              // 0x103789cc CALL 0x10384ee0
}

// =================================================================================================
// `CNPC_VHengeyokai::SelectSchedule` `0x1037fca0`, 604 bytes. No file/line stamps.
// =================================================================================================

bool FElysiumNpcHengeyokai::Select19HengeyokaiFindFish()
{
	return false;
}

int32 FElysiumNpcHengeyokai::Select19HengeyokaiMeleeAttackSchedule()
{
	using namespace NpcSelect19Species;
	// `0x10382f60`.
	HengeyokaiShunnedFindFish = 0;                                 // `+0x6678 = 0`
	if (!HasInterrupt(*this, EElysiumNpcCond::InterruptTime))      // `0x1a`
	{
		if (FElysiumEntity* const Enemy = Slot167Enemy(*this))
		{
			// Slot 220 `GetOrigin()` of the enemy and of this body, squared, SOURCE units.
			const float DistSqr = DistSqrUnits(Enemy->Origin, Origin);
			if (GMeleeFarSqr < DistSqr)
			{
				return Roll(0, 99) > 0x45 ? 0x16a : 0x15c;         // `((0x45 < r) - 1 & -14) + 0x16a`
			}
			if (GMeleeNearSqr < DistSqr)
			{
				return Roll(0, 99) > 0x27 ? 0x16a : 0x15c;
			}
			return 0x16a;                                          // SCHED_VHENGEYOKAI_MELEE_ATTACK1_WALK
		}
	}
	return 0x15c;                                                  // SCHED_VHENGEYOKAI_MELEE_ATTACK1
}

int32 FElysiumNpcHengeyokai::Select19HengeyokaiFindFishSchedule()
{
	using namespace NpcSelect19Species;
	// `0x10382d40`. Enemy null answers "unreachable" (`cVar8 = 1`).
	FElysiumEntity* const Enemy = Slot167Enemy(*this);
	const bool bUnreachable = Enemy == nullptr || IsUnreachable(Enemy);
	const bool bTooFar = Cognition.Conditions.Has(EElysiumNpcCond::TooFarForMelee);   // read, unused (retail)
	(void)bTooFar;
	bool bSearch = true;
	if (!bUnreachable)
	{
		// `RandomInt(0, 99)` against 0x50, or 0x28 when a fish was just found; at or above refuses.
		const int32 Threshold = bHengeyokaiJustFoundFish ? 0x28 : 0x50;
		bSearch = Roll(0, 99) < Threshold;
	}
	if (bSearch && Select19HengeyokaiFindFish())                   // CALL 0x10381cd0
	{
		// The found-fish arm (distance compare against the enemy's last known position, the
		// `FINDING_BODY` set and `0x16b`) is unreachable while the search seam answers false.
		NpcFlags.Set(EElysiumNpcFlag::FINDING_BODY);               // 0x10381ba0(this, 1)
		bHengeyokaiJustFoundFish = true;
		return 0x16b;                                              // SCHED_VHENGEYOKAI_FIND_FISH
	}
	HengeyokaiShunnedFindFish = 2;                                 // LAB_10382da8: `+0x6678 = 2`
	bHengeyokaiJustFoundFish = false;                              //              `+0x667c = 0`
	return 0;
}

int32 FElysiumNpcHengeyokai::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	const FElysiumNpcConditions& Cond = Cognition.Conditions;
	SelectScheduleSelector = 0x13;                                 // 0x1037fca6
	const int32 State = NpcStateRetail();
	if (State != GStateCombat)                                     // 0x1037fcb7
	{
		if (State != GStateTransform)                              // 0x1037fcbc
		{
			return FElysiumNpcHuman::SpeciesSelectSchedule();      // 0x1037fef3
		}
		return 0x16f;                                              // SCHED_VHENGEYOKAI_TRANSFORM
	}
	Cognition.bCondTookDamage = false;                             // 0x1037fcc2
	if (Cond.Has(EElysiumNpcCond::EnemyDead)                       // 0x1037fcdc
		&& HengeyokaiCarryFormBit())                               // 0x1037fce0 CALL 0x10381c80 / 0x1037fce7
	{
		return 0x15e;                                              // SCHED_VHENGEYOKAI_DROP_FISH
	}
	if (NpcFlags.Has(EElysiumNpcFlag::FINDING_BODY))               // 0x1037fcf3 CALL 0x10381be0 / 0x1037fcfc
	{
		NpcFlags.Clear(EElysiumNpcFlag::FINDING_BODY);             // 0x1037fd04 CALL 0x10381ba0(0)
		if (HasInterrupt(*this, EElysiumNpcCond::CanMeleeAttack1)) // 0x1037fd0d / 0x1037fd16
		{
			HengeyokaiPickupTarget = FElysiumEntityHandle::Invalid();   // `m_hPickupTarget = -1`
			SetIgnoreCollisionExpiry(0.0f);                        // 0x1037fd24 CALL 0x102c43b0
			return Select19HengeyokaiMeleeAttackSchedule();        // 0x1037fd2d tail JMP 0x10382f60
		}
		if (!HasInterrupt(*this, EElysiumNpcCond::TooCloseToAttack)   // 0x1037fd3b
			&& !HasInterrupt(*this, EElysiumNpcCond::TooCloseForRanged))   // 0x1037fd48
		{
			if (const int32 Fish = Select19HengeyokaiFindFishSchedule(); Fish != 0)   // 0x1037fd4c / 0x1037fd53
			{
				return Fish;
			}
			HengeyokaiPickupTarget = FElysiumEntityHandle::Invalid();
			SetIgnoreCollisionExpiry(0.0f);                        // 0x1037fd66
			return 0x15f;                                          // SCHED_VHENGEYOKAI_MELEE_IDLE
		}
		HengeyokaiPickupTarget = FElysiumEntityHandle::Invalid();
		SetIgnoreCollisionExpiry(0.0f);                            // 0x1037fd81
		return Select19HengeyokaiMeleeAttackSchedule();            // 0x1037fd8a tail JMP 0x10382f60
	}
	if (HengeyokaiCarryFormBit())                                  // 0x1037fd8f / 0x1037fd98
	{
		if (FormBitTimerExpired()                                  // 0x1037fd9e CALL 0x10381ca0 / 0x1037fda5
			&& !IsUnreachable(Slot167Enemy(*this)))                // 0x1037fdb4 slot 530 / 0x1037fdbc
		{
			return 0x161;                                          // SCHED_VHENGEYOKAI_FACE_THROW_TARGET_FORCED
		}
		if (Cond.Has(EElysiumNpcCond::EnemyOccluded)               // 0x1037fdd1
			|| !Cond.Has(EElysiumNpcCond::SeeEnemy))               // 0x1037fde2
		{
			return 0x166;                                          // SCHED_VHENGEYOKAI_CHASE_ENEMY_LKP_FISH
		}
		if (Cond.Has(EElysiumNpcCond::TooCloseToAttack)            // 0x1037fdf3
			|| Cond.Has(EElysiumNpcCond::TooCloseForRanged))       // 0x1037fe04
		{
			return 0x15e;                                          // SCHED_VHENGEYOKAI_DROP_FISH
		}
		if (!Cond.Has(GCondHaveEnemyThrowLos))                     // 0x1037fe15
		{
			return 0x166;
		}
		if (!bHengeyokaiDidFakeThrow                               // 0x1037fe1f (+0x667d)
			&& Roll(0, 99) < 5)                                    // 0x1037fe2d / 0x1037fe33
		{
			bHengeyokaiDidFakeThrow = true;                        // 0x1037fe35
			return FUN_103822a0(Slot167Enemy(*this)) ? 0x164 : 0x162;   // 0x1037fe49 FAKE / FORCED... `& 2`
		}
		return FUN_103822a0(Slot167Enemy(*this)) ? 0x163 : 0x160;   // 0x1037fe6a THROW_FISH / FACE_THROW_TARGET
	}
	NpcFlags.Clear(EElysiumNpcFlag::PRESERVE_PATH);                // 0x1037fe86 AND 0xfffffff7
	const int32 Fish = Select19HengeyokaiFindFishSchedule();       // 0x1037fe8d CALL 0x10382d40
	if (Fish != 0)                                                 // 0x1037fe94
	{
		return Fish;
	}
	if (IsUnreachable(Slot167Enemy(*this)))                        // 0x1037fea7 slot 530 / 0x1037feaf
	{
		return 0x167;                                              // SCHED_VHENGEYOKAI_CHASE_ENEMY_FAILED
	}
	if (Cond.Has(EElysiumNpcCond::EnemyOccluded))                  // 0x1037fec4
	{
		return 0x165;                                              // SCHED_VHENGEYOKAI_CHASE_ENEMY_LKP
	}
	if (Cond.Has(EElysiumNpcCond::TooFarForMelee))                 // 0x1037fed9
	{
		return Cond.Has(EElysiumNpcCond::SeeEnemy) ? 0x169 : 0x165;   // `(-(c != 0) & 4) + 0x165`
	}
	return FElysiumNpcHuman::SpeciesSelectSchedule();              // 0x1037fef3 JMP 0x10015ad2
}

// =================================================================================================
// `CNPC_VMingXiao::PreSelectSchedule` `0x10394120`, 132 bytes.
// =================================================================================================

float FElysiumNpcMingXiao::Select19MingXiaoTuningField(int32 Offset) const
{
	(void)Offset;
	return 0.f;
}

int32 FElysiumNpcMingXiao::PreSelectSchedule()
{
	using namespace NpcSelect19Species;
	SelectScheduleSelector = 0x19;                                 // 0x10394126
	if (NpcStateRetail() == GStateTransform)                       // 0x10394133
	{
		return SelectTrace(GFileMingXiao, 0x4f6, 0x157);           // 0x10394135 SCHED_VMING_XIAO_TRANSFORM_DUMMY
	}
	// `m_hRangedWeapon` (`+0x6680`) resolved: slot 388 `Weapon_Switch(entity->+0xa0, 0)`, else
	// `Weapon_Switch(NULL, 0)`. Both answer 0.
	FElysiumEntity* const Ranged = Resolve(*this, MingXiaoRangedWeapon);   // 0x10394158 / 0x10394179 / 0x1039417f
	Weapon_Switch(Ranged, 0);                                      // 0x10394187 / 0x1039419b vtable +0x610
	return 0;
}

// =================================================================================================
// `CNPC_VMingXiao::SelectSchedule` `0x103941e0`, 705 bytes.
// =================================================================================================

int32 FElysiumNpcMingXiao::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	const FElysiumNpcConditions& Cond = Cognition.Conditions;
	const double T = Now(*this);
	const int32 State = NpcStateRetail();
	SelectScheduleSelector = 0x19;                                 // 0x103941e9
	if (State != GStateIdle)                                       // 0x103941f5
	{
		if (State == GStateCombat)                                 // 0x103941fc
		{
			Cognition.bCondTookDamage = false;                     // 0x10394206
			if (const int32 Grabbed = FUN_10396dc0(); Grabbed != 0)   // 0x1039420d / 0x1039421c
			{
				return Grabbed;
			}
			for (int32 Slot = 0; Slot < 6; ++Slot)                 // 0x10394224..0x10394237 JL
			{
				if (Cond.Has(static_cast<EElysiumNpcCond>(static_cast<int32>(GCondMingCanAttackFirst) + Slot)))   // 0x10394231
				{
					return SelectTrace(GFileMingXiao, 0x530, 0x158 + Slot);   // 0x10394262 TENTACLE_ATTACK_*
				}
			}
			if (Cond.Has(GCondMingCanSpit))                        // 0x10394244
			{
				return SelectTrace(GFileMingXiao, 0x53d, 0x15e);   // 0x10394246 SCHED_VMING_XIAO_ATTACK_SPIT
			}
			if (Cond.Has(EElysiumNpcCond::EnemyOccluded))          // 0x1039428a
			{
				return SelectTrace(GFileMingXiao, 0x543, 0x160);   // 0x1039428c SCHED_VMING_XIAO_CHASE_ENEMY_LKP
			}
			// `0x10396bc0`. The port's body takes the `RandomInt(0, 99)` draw and the tuning ceiling
			// from its caller; retail draws AFTER the live-object and two timer gates, so the draw is
			// made here only when those gates are open, keeping the stream's order retail's.
			const bool bThrowGateOpen = Resolve(*this, MingXiaoThrowObject) == nullptr
				&& !(T < MingXiaoAttackTimers[5]) && !(T < MingXiaoAttackTimers[4]);
			const int32 Draw = bThrowGateOpen ? Roll(0, 99) : 0;
			const int32 Ceiling = static_cast<int32>(Select19MingXiaoTuningField(8));
			if (const int32 Throw = MingXiaoFindThrowObject(Draw, Ceiling); Throw != 0)   // 0x103942aa / 0x103942b1
			{
				return Throw;
			}
			if (IsMingXiaoProxy())                                 // 0x103942b9 CALL 0x10398870 / 0x103942c0
			{
				return SelectTrace(GFileMingXiao, 0x55a, 0x162);   // 0x103942c2 SCHED_VMING_XIAO_CLOSE_DISTANCE
			}
			if (Select19ConVarEnabled(ESelect19ConVar::MingXiaoCharge)   // 0x103942eb / 0x103942fc
				&& ScheduleHost.EnemyDistUnits < GMingXiaoChargeRange)   // 0x10394313 JP (+0x6268 < 120.0)
			{
				if (MingXiaoChargeReadyTime <= T)                  // 0x1039432e
				{
					if (MingXiaoConnectedTentacleCount < 3         // 0x10394337 JGE
						&& Select19ConVarEnabled(ESelect19ConVar::MingXiaoCharge))   // 0x10394340 / 0x1039434c
					{
						MingXiaoChargeReadyTime = static_cast<double>(Select19MingXiaoTuningField(0x10)) + T;   // 0x1039435e
					}
					else
					{
						MingXiaoChargeReadyTime = static_cast<double>(Select19MingXiaoTuningField(0xc)) + T;   // 0x10394366
					}
					return SelectTrace(GFileMingXiao, 0x56c, 0x15f);   // 0x10394380 SCHED_VMING_XIAO_CHARGE_ATTACK
				}
				if (Cond.Has(GCondMingMeleeHelpless))              // 0x103943a9
				{
					MingXiaoChargeReadyTime = T;                   // 0x103943b5
					int32 Unused = 0;
					if (FUN_10398030(2, false, Unused))            // 0x103943c3 / 0x103943ca
					{
						return SelectTrace(GFileMingXiao, 0x578, 0x15a);
					}
					if (FUN_10398030(3, false, Unused))            // 0x103943f5
					{
						return SelectTrace(GFileMingXiao, 0x57c, 0x15b);
					}
					if (FUN_10398030(0, false, Unused))            // 0x10394420
					{
						return SelectTrace(GFileMingXiao, 0x580, 0x158);
					}
					if (FUN_10398030(1, false, Unused))            // 0x1039444b
					{
						return SelectTrace(GFileMingXiao, 0x584, 0x159);
					}
				}
			}
			return SelectTrace(GFileMingXiao, 0x58a, 0x165);       // 0x10394469 SCHED_VMING_XIAO_HOLD_DISTANCE
		}
		if (State != GStateAlert)                                  // 0x103941ff
		{
			return TroikaSelectSchedule();                         // 0x10394205 JMP 0x10015596
		}
	}
	return SelectTrace(GFileMingXiao, 0x51c, ElysiumSched::SCHED_TROIKA_IDLE_STAND);   // 0x10394485 0x44
}

// =================================================================================================
// `CNPC_VSabbatLeader::PreSelectSchedule` `0x103aa510`, 150 bytes.
// =================================================================================================

int32 FElysiumNpcSabbatLeader::PreSelectSchedule()
{
	using namespace NpcSelect19Species;
	SelectScheduleSelector = 0x1f;                                 // 0x103aa566
	if (NpcStateRetail() == GStateCombat                           // 0x103aa574
		&& Slot167Enemy(*this) == nullptr)                         // 0x103aa57c / 0x103aa582
	{
		WriteNpcStateRetail(GStateIdle);                           // 0x103aa584 `m_NPCState = 1` — a raw write,
		WriteIdealStateRetail(GStateIdle);                         // 0x103aa58e `m_IdealNPCState = 1`, not SetState
	}
	return FElysiumNpc::PreSelectSchedule();                       // 0x103aa595 CALL 0x102ae920
}

// =================================================================================================
// `CNPC_VSabbatLeader::SelectSchedule` `0x103a70c0`, 564 bytes. No file/line stamps.
// =================================================================================================

int32 FElysiumNpcSabbatLeader::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	SelectScheduleSelector = 0x1f;                                 // 0x103a711a
	if (NpcStateRetail() == GStateTransform)                       // 0x103a7127
	{
		return 0x158;                                              // 0x103a7129 SCHED_VVAMPIREBOSS_TRANSFORM
	}
	if (Select19ConVarEnabled(ESelect19ConVar::AndreiForcePlayerCollision))   // 0x103a714a / 0x103a7157
	{
		SetSelect19ConVar(ESelect19ConVar::AndreiForcePlayerCollision, 0);    // 0x103a7162 ConVar::SetValue(0)
		if (FElysiumEntity* const Player = Resolve(*this, Senses.Memory.ClosestPlayer))   // 0x103a716e / 0x103a7179
		{
			// Slot 217 `GetAbsOrigin()` + (20.0, 0, 0), then slot 216 `SetAbsOrigin` (`vtable +0x360`).
			FVector Target = Player->Origin + FVector(GSabbatPlayerOffsetUnits * ElysiumMove::U, 0.0, 0.0);
			SetAbsOrigin(Target);                                  // 0x103a71ad
		}
	}
	CheckStuck();                                                  // 0x103a71b8 CALL 0x103ab580
	if (SabbatLeaderRoarAttackCount < 1)                           // 0x103a71c5 JG
	{
		SabbatLeaderRoarAttackCount = 3;                           // 0x103a71c7
		return 0x165;                                              // SCHED_VSABBATLEADER_ROAR
	}
	if (bSabbatLeaderActivated)                                    // 0x103a71ed
	{
		if (Select19ConVarEnabled(ESelect19ConVar::AndreiForceJumpAttack))   // 0x103a7200 / 0x103a720c
		{
			return 0x15b;                                          // SCHED_VSABBATLEADER_RUN_TO_BOTTOM
		}
		if (bSabbatLeaderActivated)                                // 0x103a722a (retail re-reads it)
		{
			if (Select19ConVarEnabled(ESelect19ConVar::AndreiForceChargeAttack))   // 0x103a723d / 0x103a7249
			{
				return 0x166;                                      // SCHED_VSABBATLEADER_CHARGE_ATTACK
			}
			if (bSabbatLeaderActivated                             // 0x103a7267
				&& Cognition.Conditions.Has(GCondSabbatTimeToJump))   // 0x103a7274
			{
				if (!bSabbatLeaderLastAttackWasNova)               // 0x103a7284
				{
					const int32 Pick = Roll(0, 2);                 // 0x103a7292
					if (Pick == 0)                                 // 0x103a7298
					{
						return 0x164;                              // SCHED_VSABBATLEADER_DIVE_IN
					}
					return Pick != 1 ? 0x166 : 0x15b;              // 0x103a72b3 / 0x103a72bd
				}
				return Roll(0, 1) == 0 ? 0x164 : 0x15b;            // 0x103a72a7..0x103a72cc
			}
		}
	}
	return FElysiumNpcHuman::SpeciesSelectSchedule();              // 0x103a72e0 CALL 0x10384ee0
}

// =================================================================================================
// `CNPC_VSheriffMan::SelectSchedule` `0x103ae8c0`, 394 bytes. No file/line stamps.
// =================================================================================================

int32 FElysiumNpcSheriffMan::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	SelectScheduleSelector = 0x21;                                 // 0x103ae919
	if (bSheriffActivated)                                         // 0x103ae926
	{
		int32 PlayerHeight = 0;
		int32 SelfHeight = 0;
		CategorizeHeights(PlayerHeight, SelfHeight);               // 0x103ae93c CALL 0x103b1680
		if (NpcStateRetail() == GStateTransform)                   // 0x103ae950
		{
			return 0x158;                                          // SCHED_VVAMPIREBOSS_TRANSFORM
		}
		if (!bSheriffDead)                                         // 0x103ae96e
		{
			// `sheriff_force_teleport` is read as `0 < m_nValue`, not `!= 0` (`0x103ae9a4 JLE`).
			if (Select19ConVarInt(ESelect19ConVar::SheriffForceTeleport) > 0)   // 0x103ae998 / 0x103ae9a4
			{
				SetSelect19ConVar(ESelect19ConVar::SheriffForceTeleport, 0);    // ConVar::SetValue(0)
				return 0x15a;                                      // SCHED_VSHERIFFMAN_TELEPORT
			}
			if (PlayerHeight == 0)                                 // 0x103ae9cc
			{
				if (SelfHeight == 1)                               // 0x103ae9d3
				{
					return 0x15c;                                  // SCHED_VSHERIFFMAN_EXTREME_JUMP_DOWN
				}
			}
			else if (PlayerHeight == 1 && SelfHeight == 0)         // 0x103ae9ec / 0x103ae9f4
			{
				return 0x15d;                                      // SCHED_VSHERIFFMAN_EXTREME_JUMP_UP
			}
			if (HealthPercentLostSinceRecord() < GSheriffHealthLostFloor   // 0x103aea0a CALL 0x103c6a20 / 0x103aea1e
				&& !LastAttackTimeElapsed(GSheriffAttackIdleSeconds))      // 0x103aea29 CALL 0x103c67f0 / 0x103aea30
			{
				return FElysiumNpcHuman::SpeciesSelectSchedule();  // LAB_103ae977
			}
			return 0x15a;                                          // 0x103aea36
		}
		KillSheriff();                                             // 0x103ae970 CALL 0x103b10f0
	}
	return FElysiumNpcHuman::SpeciesSelectSchedule();              // 0x103ae977 CALL 0x10384ee0
}

// =================================================================================================
// `CNPC_VTzimisce::SelectSchedule` `0x103bb7c0`, 2620 bytes, and its unnamed sub-selectors.
// =================================================================================================

bool FElysiumNpcTzimisce::Select19TzimisceFindBody()
{
	return false;
}

bool FElysiumNpcTzimisce::Select19TzimiscePounceProbe()
{
	return false;
}

bool FElysiumNpcTzimisce::Select19SequencePastHalf() const
{
	return false;
}

int32 FElysiumNpcTzimisce::Select19TzimisceFindBodySchedule()
{
	using namespace NpcSelect19Species;
	// `0x103bc4e0`. Enemy null answers "unreachable" (`cVar3 = 1`).
	FElysiumEntity* const Enemy = Slot167Enemy(*this);             // 0x103bc4e6 slot 167
	const bool bUnreachable = Enemy == nullptr || IsUnreachable(Enemy);   // 0x103bc4f4 slot 530
	const bool bTooFar = Cognition.Conditions.Has(EElysiumNpcCond::TooFarForMelee);   // 0x103bc50a
	bool bSearch = true;
	if (!bUnreachable)
	{
		if (!bTooFar || TzimisceShunnedFindBody != 0)              // `+0x66b8`
		{
			bSearch = false;
		}
		else
		{
			// `RandomInt(0, 99)` (`PUSH 0x63; PUSH 0x0`, `0x103bc52f`) against 0x55, or 0x37 when a body
			// was just found (`+0x66bc`); at or above refuses (`SBORROW` = `JGE`).
			const int32 Threshold = bTzimisceJustFoundBody ? 0x37 : 0x55;   // 0x103bc53c / 0x103bc546
			bSearch = Roll(0, 99) < Threshold;
		}
	}
	if (bSearch && Select19TzimisceFindBody())                     // CALL 0x103be180
	{
		// The found-body arm (the enemy LKP distance and angle test, `m_hPickupTarget` release on a
		// refusal, `0x103be050(this, 1)`, and `0x18f` / `0x190` by `90000.0` squared units) is
		// unreachable while the search seam answers false.
		NpcFlags.Set(EElysiumNpcFlag::FINDING_BODY);               // 0x103bc719 CALL 0x103be050(1)
		bTzimisceJustFoundBody = true;                             // `+0x66bc = 1`
		return 0x18f;                                              // SCHED_VTZIMISCE_FIND_BODY
	}
	TzimisceShunnedFindBody = 2;                                   // LAB_103bc556: `+0x66b8 = 2`
	bTzimisceJustFoundBody = false;                                //               `+0x66bc = 0`
	return 0;
}

int32 FElysiumNpcTzimisce::Select19TzimisceMeleeAttackSchedule()
{
	using namespace NpcSelect19Species;
	// `0x103bc820`.
	TzimisceShunnedFindBody = 0;                                   // `+0x66b8 = 0`
	if (Cognition.Conditions.Has(EElysiumNpcCond::CanMeleeAttack1))
	{
		return SelectTrace(GFileTzimisce, 0xb2c, 0x177);           // SCHED_VTZIMISCE_MELEE_ATTACK1_STATIONARY
	}
	if (HasInterrupt(*this, EElysiumNpcCond::InterruptTime))
	{
		return SelectTrace(GFileTzimisce, 0xb32, 0x178);           // SCHED_VTZIMISCE_MELEE_ATTACK1
	}
	FElysiumEntity* const Enemy = Slot167Enemy(*this);
	if (Enemy == nullptr)
	{
		return SelectTrace(GFileTzimisce, 0xb5c, 0x178);
	}
	const float DistSqr = DistSqrUnits(Origin, Enemy->Origin);     // slot 220 on both
	if (GMeleeFarSqr < DistSqr)
	{
		if (Roll(0, 99) < 0x46)
		{
			return SelectTrace(GFileTzimisce, 0xb40, 0x178);
		}
		return SelectTrace(GFileTzimisce, 0xb44, 0x179);           // SCHED_VTZIMISCE_MELEE_ATTACK1_WALK
	}
	if (DistSqr <= GMeleeNearSqr)
	{
		return SelectTrace(GFileTzimisce, 0xb58, 0x179);
	}
	if (Roll(0, 99) < 0x28)
	{
		return SelectTrace(GFileTzimisce, 0xb4e, 0x178);
	}
	return SelectTrace(GFileTzimisce, 0xb52, 0x179);
}

int32 FElysiumNpcTzimisce::Select19TzimisceMeleeIdleContinuation()
{
	using namespace NpcSelect19Species;
	// `0x103bca20`.
	if (HasInterrupt(*this, EElysiumNpcCond::ShouldDodge))         // `0xc`
	{
		return SelectTrace(GFileTzimisce, 0xb66, 0x173);           // SCHED_VTZIMISCE_MELEE_DODGE
	}
	if (HasInterrupt(*this, EElysiumNpcCond::BeingAttacked)        // `0xa`
		|| HasInterrupt(*this, EElysiumNpcCond::ShouldBlock))      // `0xd`
	{
		return SelectTrace(GFileTzimisce, 0xb6a, 0x174);           // SCHED_VTZIMISCE_MELEE_BLOCK
	}
	if (Cognition.Conditions.Has(EElysiumNpcCond::ScheduleDone))   // `0x5d`
	{
		return Select19TzimisceMeleeAttackSchedule();              // tail 0x103bc820
	}
	return SelectTrace(GFileTzimisce, 0xb7c, 0x170);               // SCHED_VTZIMISCE_MELEE_IDLE
}

int32 FElysiumNpcTzimisce::Select19TzimisceMeleeAdvanceContinuation()
{
	using namespace NpcSelect19Species;
	// `0x103bcaf0`: done -> MELEE_IDLE, else the running program's own local id again
	// (`GetScheduleId(space, m_pSchedule->+0x1c)` through slot 580's space, `0x102ea280`).
	if (Cognition.Conditions.Has(EElysiumNpcCond::ScheduleDone))
	{
		return SelectTrace(GFileTzimisce, 0xb85, 0x170);
	}
	return GetLocalScheduleId(Schedule.Current);
}

int32 FElysiumNpcTzimisce::Select19TzimisceMeleeRetreatContinuation()
{
	using namespace NpcSelect19Species;
	// `0x103bcb60`.
	if (Cognition.Conditions.Has(EElysiumNpcCond::EnemyOccluded))
	{
		return SelectTrace(GFileTzimisce, 0xb93, 0x171);           // SCHED_VTZIMISCE_MELEE_ADVANCE
	}
	if (Cognition.Conditions.Has(EElysiumNpcCond::ScheduleDone))
	{
		return SelectTrace(GFileTzimisce, 0xb98, 0x170);
	}
	return GetLocalScheduleId(Schedule.Current);
}

int32 FElysiumNpcTzimisce::Select19TzimisceMeleeAttackContinuation()
{
	using namespace NpcSelect19Species;
	// `0x103bcc00`: `m_fSequencePastHalf` (`+0x568`) skips the two interrupt tests.
	if (!Select19SequencePastHalf()
		&& (HasInterrupt(*this, EElysiumNpcCond::BeingAttacked)
			|| HasInterrupt(*this, EElysiumNpcCond::ShouldBlock)))
	{
		return SelectTrace(GFileTzimisce, 0xbd6, 0x174);
	}
	if (Cognition.Conditions.Has(EElysiumNpcCond::ScheduleDone))
	{
		BaseScheduleHost.MemoryBits &= 0xbfffffffu;                // `m_afMemory &= ~0x40000000`
		return SelectTrace(GFileTzimisce, 0xbde, 0x170);
	}
	return Select19TzimisceMeleeAttackSchedule();                  // tail 0x103bc820
}

int32 FElysiumNpcTzimisce::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	const FElysiumNpcConditions& Cond = Cognition.Conditions;
	SelectScheduleSelector = 0x26;                                 // 0x103bb7d0
	if (NpcFlags.Has(EElysiumNpcFlag::DO_STARTLED))                // 0x103bb7e0
	{
		NpcFlags.Clear(EElysiumNpcFlag::DO_STARTLED);
		return SelectTrace(GFileTzimisce, 0x939, 0x187);           // 0x103bb7e2 SCHED_VTZIMISCE_STARTLED
	}
	if (Roll(0, 99) < 0x32)                                        // 0x103bb816 / 0x103bb81c JGE
	{
		++Select19TzimisceFidgetCalls;                             // 0x103bb822 slot 627 (seam)
	}
	const int32 State = NpcStateRetail();
	if (State == GStateCombat)                                     // 0x103bb831
	{
		Cognition.bCondTookDamage = false;                         // 0x103bbb21
		if (Cond.Has(EElysiumNpcCond::EnemyDead) && TzimisceCarryFormBit())   // 0x103bbb33 / 0x103bbb3e
		{
			return SelectTrace(GFileTzimisce, 0x962, 0x198);       // SCHED_VTZIMISCE_DROP_BODY
		}
		if (Cond.Has(GCondTzimDropBody) && TzimisceCarryFormBit()) // 0x103bbb6b / 0x103bbb76
		{
			return SelectTrace(GFileTzimisce, 0x96a, 0x198);
		}
		if (Cond.Has(GCondTzimForceThrow) && TzimisceCarryFormBit())   // 0x103bbba3 / 0x103bbbae
		{
			return SelectTrace(GFileTzimisce, 0x972, 0x195);       // SCHED_VTZIMISCE_THROW_BODY
		}
		if (!bTzimisceFirstEnemy                                   // 0x103bbbd8 (+0x6689)
			&& Cond.Has(EElysiumNpcCond::NewEnemy))                // 0x103bbbe5
		{
			return SelectTrace(GFileTzimisce, 0x980, 5);           // 0x103bbbe7 WAKE_ANGRY
		}
		// 0x103bbc09..0x103bbc1d: the active weapon's slot `+0x5a0` word is read and discarded.
		if (!NpcFlags.Has(EElysiumNpcFlag::FINDING_BODY))          // 0x103bbc2b CALL 0x103be090 / 0x103bbc34
		{
			if (TzimisceCarryFormBit())                            // 0x103bbce9 / 0x103bbcf2
			{
				if (FUN_103be150()                                 // 0x103bbcf8 / 0x103bbcff
					&& !IsUnreachable(Slot167Enemy(*this)))        // 0x103bbd0e slot 530 / 0x103bbd16
				{
					return SelectTrace(GFileTzimisce, 0x9c3, 0x193);   // SCHED_VTZIMISCE_FACE_THROW_TARGET_FORCED
				}
				if ((Cond.Has(EElysiumNpcCond::EnemyOccluded)      // 0x103bbd43
						|| !Cond.Has(EElysiumNpcCond::SeeEnemy))   // 0x103bbd50
					&& !Cond.Has(GCondHaveEnemyThrowLos))          // 0x103bbd5d
				{
					return SelectTrace(GFileTzimisce, 0x9cb, 0x169);   // SCHED_VTZIMISCE_CHASE_ENEMY_LKP_BODY
				}
				if (Cond.Has(EElysiumNpcCond::TooCloseToAttack)    // 0x103bbd8a
					|| Cond.Has(EElysiumNpcCond::TooCloseForRanged))   // 0x103bbd9b
				{
					return SelectTrace(GFileTzimisce, 0x9d0, 0x198);
				}
				if (!Cond.Has(GCondHaveEnemyThrowLos))             // 0x103bbdac
				{
					return SelectTrace(GFileTzimisce, 0x9ec, 0x169);
				}
				if (!bTzimisceDidFakeThrow                         // 0x103bbdba (+0x66b4)
					&& Roll(0, 99) < 5)                            // 0x103bbdc8 / 0x103bbdce
				{
					bTzimisceDidFakeThrow = true;
					if (FUN_103be8e0(Slot167Enemy(*this)))         // 0x103bbde4 / 0x103bbdf5
					{
						return SelectTrace(GFileTzimisce, 0x9df, 0x196);   // SCHED_VTZIMISCE_THROW_BODY_FAKE
					}
					return SelectTrace(GFileTzimisce, 0x9dc, 0x194);       // SCHED_VTZIMISCE_FACE_THROW_TARGET_FAKE
				}
				if (FUN_103be8e0(Slot167Enemy(*this)))             // 0x103bbe30 / 0x103bbe41
				{
					return SelectTrace(GFileTzimisce, 0x9e8, 0x195);
				}
				return SelectTrace(GFileTzimisce, 0x9e5, 0x192);   // SCHED_VTZIMISCE_FACE_THROW_TARGET
			}
			NpcFlags.Clear(EElysiumNpcFlag::PRESERVE_PATH);        // 0x103bbeaf AND 0xfffffff7
			const int32 Body = Select19TzimisceFindBodySchedule(); // 0x103bbeb6 CALL 0x103bc4e0
			if (Body != 0)                                         // 0x103bbebd
			{
				return Body;
			}
			if (IsUnreachable(Slot167Enemy(*this)))                // 0x103bbed0 slot 530 / 0x103bbeda
			{
				switch (SelectTzimisceHintNode(Slot167Enemy(*this)))   // 0x103bbeeb CALL 0x103bfa50 / 0x103bbef8
				{
				case 14000:
					return SelectTrace(GFileTzimisce, 0xa03, 0x17c);   // SCHED_VTZIMISCE_CLAW_LEFT_ATTACK_SETUP
				case 0x36b1:
					return SelectTrace(GFileTzimisce, 0xa04, 0x17e);   // SCHED_VTZIMISCE_CLAW_RIGHT_ATTACK_SETUP
				case 0x36ba:
					return SelectTrace(GFileTzimisce, 0xa05, 0x17f);   // SCHED_VTZIMISCE_CLAW_SPECIAL_LEFT
				case 0x36bb:
					return SelectTrace(GFileTzimisce, 0xa06, 0x180);   // SCHED_VTZIMISCE_CLAW_SPECIAL_RIGHT
				default:
					return SelectTrace(GFileTzimisce, 0xa07, 0x16a);   // SCHED_VTZIMISCE_CHASE_ENEMY_FAILED
				}
			}
			if (Cond.Has(EElysiumNpcCond::EnemyOccluded))          // 0x103bbfb6
			{
				return SelectTrace(GFileTzimisce, 0xa0e, 0x167);   // SCHED_VTZIMISCE_CHASE_ENEMY_LKP
			}
			if (!IsUnreachable(Slot167Enemy(*this)))               // 0x103bbfed
			{
				// 0x103bbffa `GetEnemies()->GetLastKnownPosition(enemy)`: read, unused.
				if (Select19ConVarEnabled(ESelect19ConVar::TzimiscePounce)   // 0x103bc021 / 0x103bc02e
					&& Select19TzimiscePounceProbe())              // 0x103bc037 CALL 0x103bf660 / 0x103bc03e
				{
					TzimisceShunnedFindBody = 0;                   // 0x103bc040 `m_iShunnedFindBody = 0`
					return SelectTrace(GFileTzimisce, 0xa1a, 0x186);   // SCHED_VTZIMISCE_POUNCE_ATTACK
				}
				if (Cond.Has(EElysiumNpcCond::TooFarForMelee))     // 0x103bc075
				{
					if (!Cond.Has(EElysiumNpcCond::SeeEnemy))      // 0x103bc08c
					{
						return SelectTrace(GFileTzimisce, 0xa27, 0x167);
					}
					return SelectTrace(GFileTzimisce, 0xa22, 0x168);   // SCHED_VTZIMISCE_CHASE_ENEMY_TIMED
				}
			}
			// The running-program continuations (`m_pSchedule == GetScheduleOfType(id)`).
			if (Schedule.Current != ElysiumScheduleId::None)       // 0x103bc0c2
			{
				if (SelectRunningScheduleIs(0x170))                // 0x103bc0d6
				{
					return Select19TzimisceMeleeIdleContinuation();    // CALL 0x103bca20
				}
				if (SelectRunningScheduleIs(0x171))                // 0x103bc103
				{
					return Select19TzimisceMeleeAdvanceContinuation(); // CALL 0x103bcaf0
				}
				if (SelectRunningScheduleIs(0x172)                 // 0x103bc130
					|| SelectRunningScheduleIs(0x174)              // 0x103bc152
					|| SelectRunningScheduleIs(0x173))             // 0x103bc170
				{
					return Select19TzimisceMeleeRetreatContinuation(); // LAB_103bc1ed CALL 0x103bcb60
				}
				if (SelectRunningScheduleIs(0x178)                 // 0x103bc18a
					|| SelectRunningScheduleIs(0x179)              // 0x103bc1a4
					|| SelectRunningScheduleIs(0x17a))             // 0x103bc1bc
				{
					return Select19TzimisceMeleeAttackContinuation();  // LAB_103bc1de CALL 0x103bcc00
				}
			}
			return SelectTrace(GFileTzimisce, 0xa53, 0x170);       // 0x103bc1be SCHED_VTZIMISCE_MELEE_IDLE
		}
		NpcFlags.Clear(EElysiumNpcFlag::FINDING_BODY);             // 0x103bbc3c CALL 0x103be050(0)
		if (HasInterrupt(*this, EElysiumNpcCond::CanMeleeAttack1)) // 0x103bbc45 / 0x103bbc4e
		{
			PickupTarget = FElysiumEntityHandle::Invalid();        // `m_hPickupTarget = -1`
			SetIgnoreCollisionExpiry(0.0f);                        // 0x103bbc5c CALL 0x102c43b0
			return Select19TzimisceMeleeAttackSchedule();          // 0x103bbc63 CALL 0x103bc820
		}
		if (HasInterrupt(*this, EElysiumNpcCond::TooCloseToAttack) // 0x103bbc71 / 0x103bbc78
			|| HasInterrupt(*this, EElysiumNpcCond::TooCloseForRanged))   // 0x103bbc7e / 0x103bbc85
		{
			PickupTarget = FElysiumEntityHandle::Invalid();
			SetIgnoreCollisionExpiry(0.0f);                        // 0x103bbcd6
			return Select19TzimisceMeleeAttackSchedule();          // 0x103bbcdd
		}
		if (const int32 Body = Select19TzimisceFindBodySchedule(); Body != 0)   // 0x103bbc89 / 0x103bbc90
		{
			return Body;
		}
		PickupTarget = FElysiumEntityHandle::Invalid();
		SetIgnoreCollisionExpiry(0.0f);                            // 0x103bbca3
		return SelectTrace(GFileTzimisce, 0x9af, 0x170);           // 0x103bbca8
	}
	if (State == GStateAlert)                                      // 0x103bb838
	{
		if (const int32 Unknown = SelectUnknownAlertSchedule(); Unknown != 0)   // 0x103bbaf7 / 0x103bbafe
		{
			return Unknown;
		}
		if (const int32 Sound = SelectSoundAlertSchedule(); Sound != 0)   // 0x103bbb06 / 0x103bbb0d
		{
			return Sound;
		}
	}
	else if (State == 0xb)                                         // 0x103bb841 — HUNT
	{
		if (TzimisceCarryFormBit())                                // 0x103bb849 / 0x103bb850
		{
			return SelectTrace(GFileTzimisce, 0xa60, 0x198);
		}
		if (Cond.Has(EElysiumNpcCond::SeeUnknown))                 // 0x103bb876 / 0x103bb87d
		{
			return SelectTrace(GFileTzimisce, 0xa66, 0x15d);       // SCHED_VTZIMISCE_HUNT_INVESTIGATE_UNKNOWN
		}
		if (Cond.Has(EElysiumNpcCond::HearDanger)                  // 0x103bb8aa
			|| Cond.Has(EElysiumNpcCond::HearCombat)               // 0x103bb8bb
			|| Cond.Has(EElysiumNpcCond::HearWorld)                // 0x103bb8cc
			|| Cond.Has(EElysiumNpcCond::HearBulletImpact)         // 0x103bb8dd
			|| Cond.Has(EElysiumNpcCond::HearPlayer))              // 0x103bb8ee
		{
			Senses.Memory.NextInvestigateSoundTime = Now(*this) + GInvestigateSoundDelay;   // 0x103bbac9
			Senses.CommitBestSound(*this, Cognition.Conditions);   // 0x103bbad0
			return SelectTrace(GFileTzimisce, 0xa72, 0x15c);       // SCHED_VTZIMISCE_HUNT_INVESTIGATE
		}
		const int32 Draw = Roll(0, 99);                            // 0x103bb900
		if (Draw < 0x14)                                           // 0x103bb906
		{
			return SelectTrace(GFileTzimisce, 0xa81, 0x15f);       // SCHED_VTZIMISCE_HUNT_TURN_LEFT
		}
		if (Draw < 0x28)                                           // 0x103bb92b
		{
			return SelectTrace(GFileTzimisce, 0xa85, 0x160);       // SCHED_VTZIMISCE_HUNT_TURN_RIGHT
		}
		if (Draw < 0x2d)                                           // 0x103bb950
		{
			return SelectTrace(GFileTzimisce, 0xa89, 0x161);       // SCHED_VTZIMISCE_HUNT_ROAR
		}
		if (NpcFlags.Has(EElysiumNpcFlag::MADE_HUNT_PATH))         // 0x103bb97d
		{
			return SelectTrace(GFileTzimisce, 0xaa7, 0x15b);       // SCHED_VTZIMISCE_HUNT
		}
		FElysiumEntity* const Enemy = GetEnemy();                  // 0x103bb987 slot 168
		if (Enemy == nullptr)                                      // 0x103bb98f
		{
			return SelectTrace(GFileTzimisce, 0xa92, 0x15a);       // SCHED_VTZIMISCE_HUNT_SETUP_NO_ENEMY
		}
		switch (SelectTzimisceHintNode(Enemy))                     // 0x103bb9b4 / 0x103bb9c1 / table 0x103bc1fc
		{
		case 14000:
			return SelectTrace(GFileTzimisce, 0xa9a, 0x17c);
		case 0x36b1:
			return SelectTrace(GFileTzimisce, 0xa9b, 0x17e);
		case 0x36ba:
			return SelectTrace(GFileTzimisce, 0xa9c, 0x17f);
		case 0x36bb:
			return SelectTrace(GFileTzimisce, 0xa9d, 0x180);
		default:
			break;
		}
		if (Slot167Enemy(*this) != nullptr)                        // 0x103bba5a / 0x103bba6c
		{
			return SelectTrace(GFileTzimisce, 0xaa1, 0x159);       // SCHED_VTZIMISCE_HUNT_SETUP
		}
		return SelectTrace(GFileTzimisce, 0xaa0, 0x15a);
	}
	return TroikaSelectSchedule();                                 // 0x103bbb15 JMP 0x10015596
}

// =================================================================================================
// `CNPC_VTzimisceHeadClaw::SelectSchedule` `0x103c1610`, 166 bytes. Writes no selector id.
// =================================================================================================

int32 FElysiumNpcTzimisceHeadClaw::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	if (NpcStateRetail() == GStateCombat)                          // 0x103c161d
	{
		Cognition.bCondTookDamage = false;                         // 0x103c1623
		if (Cognition.Conditions.Has(EElysiumNpcCond::ShouldCharge))   // 0x103c162c / 0x103c1633
		{
			return 0x158;                                          // 0x103c1635 SCHED_TZIMISCEHEADCLAW_CHARGE
		}
		const uint32 Word = SelectActiveWeaponWord();              // 0x103c163d..0x103c165d
		if ((Word & GWeaponMeleeBits) == 0)                        // 0x103c1667
		{
			if (FUN_103c24a0()                                     // 0x103c1669 / 0x103c1674
				|| Roll(0, 100) < 0x28)                            // 0x103c1680 / 0x103c1694 JL — RandomInt(0, 100)
			{
				return 0xca;                                       // SCHED_TROIKA_MELEE_ADVANCE
			}
			if (const int32 Ranged = SelectScheduleRangedCombat(static_cast<int32>(Word)); Ranged != 0)   // 0x103c169d / 0x103c16a3
			{
				return Ranged;
			}
		}
		else if (const int32 Melee = SelectScheduleMeleeCombat(static_cast<int32>(Word)); Melee != 0)   // 0x103c1679
		{
			return Melee;
		}
	}
	return TroikaSelectSchedule();                                 // 0x103c16a5 JMP 0x10015596
}

// =================================================================================================
// `CNPC_VTzimisceRunner::SelectSchedule` `0x103c3310`, 299 bytes. Writes no selector id.
// =================================================================================================

int32 FElysiumNpcTzimisceRunner::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	if (NpcStateRetail() == GStateCombat)                          // 0x103c331e
	{
		Cognition.bCondTookDamage = false;                         // 0x103c3324
		const int32 Melee = SelectScheduleMeleeCombat(static_cast<int32>(SelectActiveWeaponWord()));   // 0x103c3347 slot 604
		FElysiumEntity* const Potential = Resolve(*this, RunnerPotentialEnemy);   // 0x103c335d..0x103c338a (+0x6678)
		if (Potential == nullptr)
		{
			if (Melee != 0)                                        // 0x103c342f
			{
				return Melee;
			}
		}
		else
		{
			// A live potential enemy DISCARDS the melee answer unless it is one of five ids and the
			// potential enemy stands more than 256 units away in 2-D (then `0x157`): every other case
			// falls to the Troika body with the melee answer thrown away. Retail's, reproduced.
			switch (Melee)                                         // 0x103c3390..0x103c33a0 JA
			{
			case 199:
			case 200:
			case 0xe4:
			case 0xe7:
			case 0x156:
			{
				// Slot 217 on both; `sqrt(dx*dx + dy*dy)` (`PTR_thunk_FUN_101371d0`) against 256.0.
				const double Dx = (Potential->Origin.X - Origin.X) / ElysiumMove::U;
				const double Dy = (Potential->Origin.Y - Origin.Y) / ElysiumMove::U;
				if (GRunnerAdvanceDistance < FMath::Sqrt(Dx * Dx + Dy * Dy))   // 0x103c341c / 0x103c3422
				{
					return 0x157;                                  // SCHED_VTZIMISCERUNNER_ADVANCE_ON_POTENTIAL_ENEMY
				}
				break;
			}
			default:
				break;
			}
		}
	}
	return TroikaSelectSchedule();                                 // 0x103c3431 JMP 0x10015596
}

// =================================================================================================
// `CNPC_VWerewolf::SelectSchedule` `0x103cee70`, 1481 bytes.
// =================================================================================================

bool FElysiumNpcWerewolf::Select19CheckAllRandomMoveHints()
{
	return false;
}

bool FElysiumNpcWerewolf::Select19FindRandomMoveHint()
{
	return false;
}

int32 FElysiumNpcWerewolf::SpeciesSelectSchedule()
{
	using namespace NpcSelect19Species;
	const FElysiumNpcConditions& Cond = Cognition.Conditions;
	SelectScheduleSelector = 0x29;                                 // 0x103ceedb
	SelectIdealStateRetail();                                      // 0x103ceee8 slot 461 (`vtable +0x734`), answer discarded
	if (Select19ConVarEnabled(ESelect19ConVar::WerewolfForceTeleport))   // 0x103ceefb / 0x103cef07
	{
		ClearMoveHint();                                           // 0x103cef0b
		return SelectTrace(GFileWerewolf, 0x8d8, 0x157);           // SCHED_VWEREWOLF_RUN_TO_TELEPORT
	}
	if (!IsViewable())                                             // 0x103cef39 slot 163 / 0x103cef41
	{
		return SelectTrace(GFileWerewolf, 0x8dd, 0x158);           // SCHED_VWEREWOLF_DO_TELEPORT
	}
	switch (NpcStateRetail())                                      // 0x103cef71 JA / table 0x103cf43c
	{
	case 0:
	case 1:                                                        // 0x103cefef
		if (Slot167Enemy(*this) == nullptr)                        // 0x103ceff3
		{
			return SelectTrace(GFileWerewolf, 0x8f8, 0x156);       // SCHED_VWEREWOLF_CONSIDER_SITUATION
		}
		return SelectTrace(GFileWerewolf, 0x8fc, 0x157);
	case 2:
	case 3:
	case 8:
	case 10:
	case 0xe:                                                      // 0x103cf03d
	{
		if (Slot167Enemy(*this) == nullptr)                        // 0x103cf041 / 0x103cf049
		{
			return SelectTrace(GFileWerewolf, 0x909, 0x156);
		}
		if (FElysiumEntity* const Enemy = Slot167Enemy(*this); Enemy != nullptr && !Enemy->IsAlive())   // 0x103cf07c / 0x103cf08c / 0x103cf094
		{
			return SelectTrace(GFileWerewolf, 0x915, 0x157);
		}
		if (Cond.Has(GCondWolfCanTeleport))                        // 0x103cf0c8
		{
			TeleportOut();                                         // 0x103cf0ca
			return SelectTrace(GFileWerewolf, 0x91d, 0x158);
		}
		if (Cond.Has(GCondWolfShouldBreakHint)                     // 0x103cf0fd
			&& (WerewolfHintFlags & 0x100u) == 0)                  // 0x103cf10f (+0x66e8)
		{
			return SelectTrace(GFileWerewolf, 0x928, 0x15e);       // SCHED_VWEREWOLF_PLAYER_ON_BREAKABLE
		}
		if (Cond.Has(EElysiumNpcCond::TooCloseToAttack))           // 0x103cf141
		{
			return SelectTrace(GFileWerewolf, 0x92f, ElysiumSched::SCHED_TROIKA_MELEE_STEPBACK);   // 0xd3
		}
		if (bWerewolfPlayFrustration)                              // 0x103cf170 (+0x66a9)
		{
			bWerewolfPlayFrustration = false;
			return SelectTrace(GFileWerewolf, 0x935, 0x15d);       // SCHED_VWEREWOLF_UNREACHABLE
		}
		if (Cond.Has(GCondWolfCanSpecialMove)                      // 0x103cf1a9
			&& !Cond.Has(EElysiumNpcCond::CanMeleeAttack1)         // 0x103cf1b6
			&& !Cond.Has(EElysiumNpcCond::CanMeleeAttack2)         // 0x103cf1c3
			&& Cond.Has(EElysiumNpcCond::EnemyUnreachable))        // 0x103cf1d0
		{
			SelectTrace(GFileWerewolf, 0x93f, 0);
			return SelectScheduleForHint(MoveHintNode);            // 0x103cf1ef CALL 0x103ce9b0
		}
		if (Cond.Has(EElysiumNpcCond::EnemyUnreachable))           // 0x103cf209
		{
			return SelectTrace(GFileWerewolf, 0x947, 0x157);
		}
		break;
	}
	case 7:                                                        // 0x103cef86
		if (!IsAlive())                                            // 0x103cef8a slot 158 / 0x103cef92
		{
			return SelectTrace(GFileWerewolf, 0x8e8, 0x162);       // SCHED_VWEREWOLF_PLAY_DEAD
		}
		if (Cond.Has(GCondWolfDeathTriggered))                     // 0x103cefc4
		{
			return SelectTrace(GFileWerewolf, 0x8ed, 0x161);       // SCHED_VWEREWOLF_DO_DEATH_FINALE
		}
		break;
	case 9:                                                        // 0x103cf39f
		if (Cond.Has(GCondWolfCanTeleport))                        // 0x103cf3ac
		{
			TeleportOut();                                         // 0x103cf3ae
			return SelectTrace(GFileWerewolf, 0x98c, 0x158);
		}
		if (Select19CheckAllRandomMoveHints())                     // 0x103cf3d8 CALL 0x103cf770 / 0x103cf3e9
		{
			SelectTrace(GFileWerewolf, 0x991, 0);
			return SelectScheduleForHint(MoveHintNode);            // 0x103cf3fe
		}
		return SelectTrace(GFileWerewolf, 0x995, 0x157);           // 0x103cf40d
	case 0xb:                                                      // 0x103cf234
		if (Cond.Has(GCondWolfDeathTriggered))                     // 0x103cf23f
		{
			return SelectTrace(GFileWerewolf, 0x95b, 0x161);
		}
		if (Cond.Has(GCondWolfCanTeleport))                        // 0x103cf273
		{
			TeleportOut();                                         // 0x103cf275
			return SelectTrace(GFileWerewolf, 0x961, 0x158);
		}
		if (Cond.Has(GCondWolfShouldBreakHint)                     // 0x103cf2a8
			&& (WerewolfHintFlags & 0x100u) == 0)                  // 0x103cf2bc
		{
			return SelectTrace(GFileWerewolf, 0x96b, 0x15e);
		}
		if (!Cond.Has(GCondWolfCanSpecialMove))                    // 0x103cf2f0
		{
			if (Select19FindRandomMoveHint())                      // 0x103cf34f CALL 0x103d14f0 / 0x103cf360
			{
				SelectTrace(GFileWerewolf, 0x97d, 0);
				return SelectScheduleForHint(MoveHintNode);        // 0x103cf375
			}
			return SelectTrace(GFileWerewolf, 0x981, 0x157);       // 0x103cf384
		}
		if (!Cond.Has(EElysiumNpcCond::CanMeleeAttack1)            // 0x103cf2fb
			&& !Cond.Has(EElysiumNpcCond::CanMeleeAttack2)         // 0x103cf30c
			&& Cond.Has(EElysiumNpcCond::EnemyUnreachable))        // 0x103cf31d
		{
			SelectTrace(GFileWerewolf, 0x977, 0);
			return SelectScheduleForHint(MoveHintNode);            // 0x103cf340
		}
		break;
	default:                                                       // 0x103cf428
		break;
	}
	return TroikaSelectSchedule();                                 // 0x103cf42a CALL 0x10015596
}
