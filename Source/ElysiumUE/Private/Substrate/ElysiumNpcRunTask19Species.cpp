// Story 0019/8 (29e under the strict verdict), family **RunTask19** -- the species classes' bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Owns (RunTask19's `rule` rows): 0x1038d130 CNPC_VManBat::RunTask, 0x1035f940
// CNPC_VAnimal::RunTask, 0x10384ab0 CNPC_VHuman::RunTask, 0x10393930 CNPC_VMingXiao::RunTask,
// 0x1039d750 CNPC_VMingXiaoTentacle::RunTask, 0x103bb1e0 CNPC_VTzimisce::RunTask, 0x103c3870
// CNPC_VTzimisceRunner::RunTask, 0x103cdfb0 CNPC_VWerewolf::RunTask, 0x103652b0
// CNPC_VBach::RunTask, 0x10374a20 CNPC_VDog::RunTask, 0x103793e0 CNPC_VGargoyle::RunTask,
// 0x1037b9f0 CNPC_VGhoulCroucher::RunTask, 0x10380cb0 CNPC_VHengeyokai::RunTask, 0x103b38a0
// CNPC_VTaxiDriver::RunTask, 0x103c5f40 CNPC_VVampireBoss::RunTask, 0x103e01d0
// CNPC_VZombie::RunTask, 0x1035d8b0 CNPC_VAndreiBlood::RunTask, 0x103612e0
// CNPC_VAsianVampire::RunTask, 0x1036bfc0 CNPC_VChangBros::RunTask, 0x103a8990
// CNPC_VSabbatLeader::RunTask, 0x103af780 CNPC_VSheriffMan::RunTask.
//
// Lane L05 (pass I). Each body is read off its listing (`vtmb_asm`); the parent it falls to is the
// class retail's direct thunk names (`CAI_BaseNPC::RunTask` for ManBat, `CAI_BaseNPCTroika::RunTask`,
// `CNPC_VHuman::RunTask`, `CNPC_VAnimal::RunTask` or `CNPC_VVampireBoss::RunTask`), written as the
// qualified call. The retail scope-trace push/pop some bodies bracket themselves with is the debug
// stack this runtime does not stand. Species task ids are compared as the class-LOCAL id (`RunTask19Species::LocalTaskOf`, slot 450), as the
// landed `FElysiumNpcFrenzyShadow::StartTaskSlot442` compares them (the class-local id). Walked
// prose: `docs/vtmb/npc-ai/story8/RunTask19.md`.

#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcAnimal.h"
#include "Substrate/ElysiumNpcAsianVampire.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcDog.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcHuman.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTaxiDriver.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcVampireBoss.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcZombie.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcFacingShared.h"
#include "Substrate/ElysiumNpcMotor2Shared.h"
#include "Substrate/ElysiumScheduleText.h"

namespace RunTask19Species
{
	constexpr float YawSpeedHold = -2.0f;       // `PUSH 0xc0000000`
	constexpr float YawSpeedDefault = -1.0f;    // `PUSH 0xbf800000`
	constexpr int32 UpdateYawDefault = -1;
	constexpr uint32 MemoryTurning = 0x2000u;   // `m_afMemory & 0x2000`

	// The per-class source files the fail traces stamp.
	const TCHAR* const GFileHuman = TEXT("E:\\Vampire\\main\\dlls\\hl2_dll\\NPC_VHuman.cpp");
	const TCHAR* const GFileManBat = TEXT("E:\\Vampire\\main\\dlls\\hl2_dll\\NPC_VManBat.cpp");
	const TCHAR* const GFileTzimisce = TEXT("E:\\Vampire\\main\\dlls\\hl2_dll\\NPC_VTzimisce.cpp");
	const TCHAR* const GFileBach = TEXT("E:\\Vampire\\main\\dlls\\hl2_dll\\NPC_VBach.cpp");
	const TCHAR* const GFileChangBros = TEXT("E:\\Vampire\\main\\dlls\\hl2_dll\\NPC_VChangBros.cpp");

	const FElysiumScheduleStep* StepOf(void* Task)
	{
		return static_cast<const FElysiumScheduleStep*>(Task);
	}

	/** Retail's `pTask->iTask`: the CLASS-LOCAL number every species switch compares. The step
	 *  carries the GLOBAL id (stated divergence, `ElysiumScheduleText.h`), so it is translated back
	 *  through the receiving class's task space -- slot 450 `GetLocalTaskId` (`0x101a6640`, no
	 *  override on any class), the translation StartTask19's species bodies use (`Species19TaskLocal`).
	 *  -1 for an id no space in the chain holds. */
	int32 LocalTaskOf(FElysiumNpcBase& Npc, const FElysiumScheduleStep* Step)
	{
		return Step != nullptr ? Npc.GetLocalTaskId(Step->TaskId) : INDEX_NONE;
	}

	double NowOf(const FElysiumNpc& Npc)
	{
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}

	// `+0x1b44 = file`, `+0x1b48 = line`, then slot 448.
	void FailAt(FElysiumNpc& Npc, const TCHAR* File, int32 Line, int32 Reason)
	{
		Npc.RecordScheduleEvent(FString::Printf(TEXT("RunTask fail trace %s:%d"), File, Line));
		Npc.TaskFail(Reason);
	}

	// Slot 167, `this->GetEnemy()` through the const overload (`vtable+0x29c`).
	FElysiumEntity* Enemy167(const FElysiumNpc& Npc)
	{
		return static_cast<const FElysiumNpcBase&>(Npc).GetEnemy();
	}

	// `GetEnemies()->LastKnownPosition(enemy)` (`0x102dfed0`), called with a NULL enemy too: the
	// record, else the last position-only record, else `vec3_origin` (L05 integration: was
	// `EnemyLastKnownPosition`, which skipped the walk with no enemy).
	FVector EnemyLkp(const FElysiumNpc& Npc)
	{
		return Npc.Conditions19LastKnownPosition(Enemy167(Npc));
	}

	// The shared facing block: slot 167, `0x102dfed0`, `0x102e20b0(motor, lkp, speed)`.
	void FaceEnemyLkp(FElysiumNpc& Npc, float YawSpeed)
	{
		Npc.MotorSetIdealYawToTargetAndUpdate(EnemyLkp(Npc), YawSpeed);
	}

	void TurnUnlessMemory(FElysiumNpc& Npc)
	{
		if ((Npc.BaseScheduleHost.MemoryBits & MemoryTurning) == 0)
		{
			Npc.SetTurnActivity();                                                // slot 572
		}
	}

	// `GetEnemy()` and its `+0x9c` combat-character view, the pair the melee arms test.
	FElysiumCombatCharacter* EnemyCharacter(const FElysiumNpc& Npc)
	{
		FElysiumEntity* Enemy = Enemy167(Npc);
		return Enemy != nullptr ? Enemy->AsCombatCharacter() : nullptr;
	}

	// `(0x5dc)(act)` then `(0x5f4)` then `(0x5e0)`: slots 375, 381 and 376 -- the activity the NPC
	// would actually play for `Activity`.
	int32 TranslatedActivity(FElysiumNpc& Npc, int32 Activity)
	{
		int32 Act = Npc.NPC_EarlyTranslateActivity(Activity);
		Act = Npc.Weapon_TranslateActivity(Act);
		return Npc.NPC_TranslateActivity(Act);
	}

	// `CPASAttenuationFilter` + `enginesound->EmitSound(filter, edict, channel, wav, 1.0, 0.8, 0, 100)`.
	void EmitWav(FElysiumNpc& Npc, int32 Channel, const TCHAR* Wav)
	{
		Npc.EmitNamedWav(&Npc, Channel, Wav, ElysiumNpcTunables::One, 0.8f, 100);
	}
}

// --- Seams declared on the species headers (lane L05) ------------------------------------------------

void FElysiumNpcManBat::ManBatLeaveFlight(int32 Arg)
{
	// `0x1038c170` -- SEAM (declaration).
	(void)Arg;
	++ManBatLeaveFlightCalls;
}

void FElysiumNpcManBat::ManBatFlyBySound()
{
	// `0x1038fe30` -- SEAM (declaration).
	++ManBatFlyBySoundCalls;
}

FElysiumEntity* FElysiumNpcManBat::ManBatNearestSpotlight() const
{
	// `0x100f7b20("Spotlight *", ...)` -- SEAM (declaration).
	return nullptr;
}

void FElysiumNpcManBat::ManBatKillSpotlight(FElysiumEntity& Light)
{
	// The spotlight kill -- SEAM (declaration).
	(void)Light;
	++ManBatSpotlightsKilled;
}

void FElysiumNpcHengeyokai::HengeyokaiTask14b()
{
	// `0x10383470` (story 8 wave 2: was a counted seam, blocked on `+0x1560`, now the Troika word
	// `ProteanTransformStartTime`). Nothing until `m_flProteanTransformStartTime + 2.0`
	// (`_DAT_104b6808`) `< curtime` (`10383489 TEST AH,5` / `1038348c JP`: ordered, strict); then the
	// type-0 stat list (`+0x13bc/+0x13c0`, else the global `0x109f0b40`) `Set(0xf, 0)`
	// (`103834e8 CALL 0x1000ccd9`), `+0x6694 m_bInSharkForm = 1` (`103834f1`), `TaskComplete(0)`
	// (`103834f8 CALL 0x1000ac68`).
	constexpr double TransformWaitSeconds = 2.0;   // `_DAT_104b6808`
	++HengeyokaiTask14bCalls;
	if (!(ProteanTransformStartTime + TransformWaitSeconds < RunTask19Species::NowOf(*this)))
	{
		return;
	}
	TypedStatSet(/*ListType*/ 0, /*stat 0x0f, the wound counter*/ 0x0f, 0);
	bHengeyokaiInSharkForm = true;
	TaskComplete(/*bIgnoreTaskFailed=*/false);
}

void FElysiumNpcMingXiao::MingXiaoTask14b()
{
	// `0x1039aa20` (story 8 wave 2: was a counted seam, blocked on `+0x1560`). Hengeyokai's
	// `0x10383470` without the `+0x6694` write: `+0x1560 + 2.0 (_DAT_10452dc4) < curtime`
	// (`1039aa39 TEST AH,5` / `1039aa3c JP`), `Set(0xf, 0)` (`1039aaa0`), `TaskComplete(0)`
	// (`1039aaa9`).
	constexpr double TransformWaitSeconds = 2.0;   // `_DAT_10452dc4`
	++MingXiaoTask14bCalls;
	if (!(ProteanTransformStartTime + TransformWaitSeconds < RunTask19Species::NowOf(*this)))
	{
		return;
	}
	TypedStatSet(/*ListType*/ 0, /*stat 0x0f, the wound counter*/ 0x0f, 0);
	TaskComplete(/*bIgnoreTaskFailed=*/false);
}

void FElysiumNpcMingXiao::MingXiaoTentacleGrab()
{
	++MingXiaoGrabCalls;              // `0x10398db0` -- SEAM (family Misc19's recorder)
}

bool FElysiumNpcMingXiaoTentacle::TentacleHintClear(const FVector& PositionCm) const
{
	// `0x1039ee20` (L05 integration: was a seam answering true). `NAI_Hull::Mins(15)` / `Maxs(15)`
	// (`0x102d6100` / `0x102d6120`) with X and Y doubled -- Z is NOT -- then
	// `CAI_BaseNPCTroika::IsAreaClear(pos, 0x202400b, &mins, &maxs)` (`0x102a0fb0`): the stationary
	// hull test with `m_bForceNPCCheck` (`+0x63da`) raised for the one trace. The port's
	// `IsAreaClear` takes only the OBB, so its body is spelled here with the explicit box.
	constexpr int32 TentacleHull = 15;
	constexpr int32 TentacleClearMask = 0x202400b;
	FVector Mins = FVector::ZeroVector;
	FVector Maxs = FVector::ZeroVector;
	RetailHullExtents(TentacleHull, EElysiumHullExtents::Full, Mins, Maxs);
	Mins.X *= 2.0; Mins.Y *= 2.0;
	Maxs.X *= 2.0; Maxs.Y *= 2.0;
	FElysiumNpcMingXiaoTentacle& Self = const_cast<FElysiumNpcMingXiaoTentacle&>(*this);
	Self.bForceNpcCheck = true;
	FKernelHullTrace Trace;
	const FVector AtUnits = PositionCm / ElysiumMove::U;
	KernelHullTrace(AtUnits, AtUnits, Mins, Maxs, TentacleClearMask, Trace);
	Self.bForceNpcCheck = false;
	return Trace.Fraction >= 1.0f && !Trace.bAllSolid && !Trace.bStartSolid;
}

float FElysiumNpcSabbatLeader::SabbatLeaderSplashCycle() const
{
	// `_DAT_1093c33c`, written once by the static initialiser `0x103a5870`: `FLD [0x104c3cf4]` (33.0f)
	// / `FDIV [0x104c3cf8]` (60.0f) / `FSTP float` -- frame 33 of a 60-frame splash, 0.55f (both
	// constants read from the image; L05 integration: was an "unrecovered" 0.0).
	return static_cast<float>(33.0f / 60.0f);
}

// --- `CNPC_VAndreiBlood::RunTask` `0x1035d8b0`, 430 bytes --------------------------------------------
int32 FElysiumNpcAndreiBlood::RunTaskSlot444(void* Arg0)
{
	const FElysiumScheduleStep* Step = RunTask19Species::StepOf(Arg0);
	if (Step == nullptr)
	{
		return 0;   // crash guard: retail reads the task id first
	}
	const double Now = RunTask19Species::NowOf(*this);
	switch (RunTask19Species::LocalTaskOf(*this, Step))                                                          // 0x1035d915
	// same arm: 0x1035d90f JA
	{
	case 0x150:
		FacePlayerAdvance();                                                       // 0x1035d91e
		return 0;
	case 0x151:
		if (0.5f <= SequenceCycle)                                                 // 0x1035d934 `_DAT_104a6f84`
		// same arm: 0x1035d941 JNZ
		{
			TakeDamageMode = 0;                                                    // 0x1035d943 `m_takedamage`
		}
		if (IsActivityFinished())                                                  // 0x1035d951
		// same arm: 0x1035d959 JZ
		{
			TaskComplete(false);                                                   // 0x1035d963
		}
		return 0;
	case 0x152:
		if (bAndreiTriggerUnhide)                                                  // 0x1035d979
		// same arm: 0x1035d97b JZ
		{
			Unhide();                                                              // 0x1035d981 slot 67
			bAndreiTriggerUnhide = false;                                          // 0x1035d987
			TakeDamageMode = 2;                                                    // 0x1035d98e
			AndreiHitCounter = 0;                                                  // 0x1035d998
			bAndreiForceTeleport = false;                                          // 0x1035d9a2
		}
		FacePlayerAdvance();                                                       // 0x1035d9ab
		if (IsActivityFinished())                                                  // 0x1035d9b4
		// same arm: 0x1035d9bc JZ
		{
			TaskComplete(false);                                                   // 0x1035d9c6
		}
		return 0;
	case 0x154:
		if (IsActivityFinished())                                                  // 0x1035d9da
		// same arm: 0x1035d9e2 JZ
		{
			bAndreiForceTeleport = true;                                           // 0x1035d9e8
			TaskComplete(false);                                                   // 0x1035d9ef
		}
		return 0;
	case 0x156:
		if (bAndreiForceTeleport                                                   // 0x1035da07
			|| AndreiHitMax <= AndreiHitCounter                                    // 0x1035da17
			|| 5.0f <= static_cast<float>(Now - AndreiTeleportWaitStartTime))      // 0x1035da28 `_DAT_104a6f88`
			// same arm: 0x1035da35 JNZ
		{
			TaskComplete(false);                                                   // 0x1035da3b
		}
		return 0;
	default:
		break;
	}
	return FElysiumNpcVampireBoss::RunTaskSlot444(Arg0);                           // 0x1035da4e
}

// --- `CNPC_VAnimal::RunTask` `0x1035f940`, 308 bytes -------------------------------------------------
// Also carries the inherited body of CNPC_VRat, CNPC_VScurrying.
int32 FElysiumNpcAnimal::RunTaskSlot444(void* Arg0)
{
	const FElysiumScheduleStep* Step = RunTask19Species::StepOf(Arg0);
	if (Step == nullptr)
	{
		return 0;
	}
	const double Now = RunTask19Species::NowOf(*this);
	switch (RunTask19Species::LocalTaskOf(*this, Step))                                                          // 0x1035f961
	// same arm: 0x1035f953 JA
	{
	case 0x36:
	case 0x37:
		AutoMovement();                                                            // 0x1035f96a
		// same arm: 0x1035f973 CALL, 0x1035f983 CALL
		RunTask19Species::FaceEnemyLkp(*this, RunTask19Species::YawSpeedHold);    // 0x1035f98b / 0x1035f9a0
		if (IsActivityFinished())                                                  // 0x1035f9a9
		// same arm: 0x1035f9b1 JZ
		{
			TaskComplete(false);                                                   // 0x1035f9bb
		}
		return 0;
	case 0x89:
	case 0x8a:
		AutoMovement();                                                            // 0x1035f9ca
		// same arm: 0x1035f9d3 CALL, 0x1035f9e3 CALL, 0x1035f9eb CALL
		RunTask19Species::FaceEnemyLkp(*this, RunTask19Species::YawSpeedHold);    // 0x1035fa00
		if (IsActivityFinished()                                                   // 0x1035fa09 / 0x1035fa11
			|| static_cast<float>(Now - LastAttackTime) > Step->Data)              // 0x1035fa2b
		{
			TaskComplete(false);                                                   // 0x1035fa31
		}
		return 0;
	case 0x8b:
	case 0x8e:
		AutoMovement();                                                            // 0x1035fa40
		if (IsActivityFinished())                                                  // 0x1035fa49
		// same arm: 0x1035fa51 JZ
		{
			TaskComplete(false);                                                   // 0x1035fa57
		}
		return 0;
	default:
		break;
	}
	return FElysiumNpc::RunTaskSlot444(Arg0);                                      // 0x1035fa67
}

// --- `CNPC_VAsianVampire::RunTask` `0x103612e0`, 272 bytes -------------------------------------------
int32 FElysiumNpcAsianVampire::RunTaskSlot444(void* Arg0)
{
	const FElysiumScheduleStep* Step = RunTask19Species::StepOf(Arg0);
	if (Step == nullptr)
	{
		return 0;
	}
	switch (RunTask19Species::LocalTaskOf(*this, Step))                                                          // 0x10361350
	// same arm: 0x10361342 JA
	{
	case 0x13a:
		FElysiumNpcVampireBoss::RunTaskSlot444(Arg0);                              // 0x10361386
		if (ActivityNumber != 0x2d                                                 // 0x1036138b
		// same arm: 0x10361392 JZ
			&& Velocity.Z / ElysiumMove::U <= 250.0f)                              // 0x10361398 slot 198 / 0x103613c1 `_DAT_104a930c`
		{
			RestartIdealActivityId(0x2d);                                          // 0x103613c7
		}
		return 0;
	case 0x150:
		// `SetupJump(m_pHintNode)` (`0x10361357 MOV EAX,[+0x5ddc]` / `PUSH EAX`, `0x10361a70`): the
		// pointer's bits reach a float parameter that the body only tests against 0.0, i.e. "a hint
		// node is held". The port holds a node INDEX, so it hands that fact over as 1.0 / 0.0 (L05
		// integration: the index itself was cast, so no node read -1.0 and node 0 read 0.0).
		AsianVampireSetupJump(BaseScheduleHost.HintNode != INDEX_NONE ? 1.0f : 0.0f); // 0x10361360
		bAsianVampirePathBlocked = false;                                          // 0x10361369
		TaskComplete(false);                                                       // 0x10361370
		return 0;
	case 0x151:
	case 0x152:
		return 0;
	default:
		break;
	}
	return FElysiumNpcVampireBoss::RunTaskSlot444(Arg0);                           // 0x103613dd
}

// --- `CNPC_VBach::RunTask` `0x103652b0`, 789 bytes ---------------------------------------------------
int32 FElysiumNpcBach::RunTaskSlot444(void* Arg0)
{
	const FElysiumScheduleStep* Step = RunTask19Species::StepOf(Arg0);
	if (Step == nullptr)
	{
		return 0;
	}
	const int32 Id = RunTask19Species::LocalTaskOf(*this, Step);
	const double Now = RunTask19Species::NowOf(*this);
	if (Id >= 0xb0 && Id <= 0xb1)                                                  // 0x103652c2 / 0x103652c9
	{
		FElysiumEntity* Weapon = ActiveWeaponEntity();                             // 0x103652e4
		if (Weapon == nullptr)                                                     // 0x103652eb
		// same arm: 0x103652f4 CALL
		{
			return 0;
		}
		const FString WeaponClass = Weapon->Class != nullptr ? Weapon->Class->ClassName.ToString() : FString();
		if (!WeaponClass.Equals(TEXT("item_w_rem_m_700_bach"), ESearchCase::IgnoreCase)) // 0x103652fa `__strcmpi`
		// same arm: 0x10365304 JZ
		{
			bBachShotLatch = true;                                                 // 0x10365309
			return FElysiumNpcHuman::RunTaskSlot444(Arg0);                         // 0x10365310
		}
		// The aim: the shoot-target override while in state 4 or 0xc, else the enemy's remembered
		// position; with neither, fail -- and still aim at the (uninitialised in retail, zero here)
		// point (named divergence).
		FVector Aim = FVector::ZeroVector;
		FElysiumEntity* Override = World != nullptr ? World->Resolve(ShootTargetOverride) : nullptr;
		const int32 State = NpcStateRetail();
		if (Override != nullptr && (State == 4 || State == 0xc))                   // 0x10365326..0x1036535b
		// same arm: 0x10365346 JNZ, 0x1036534b JZ, 0x10365356 JZ, 0x10365366 JZ, 0x1036537d JNZ
		{
			Aim = Override->Origin;                                                // 0x10365387 slot 217
		}
		else if (RunTask19Species::Enemy167(*this) != nullptr)                     // 0x103653a7
		// same arm: 0x103653b1 JZ, 0x103653b5 CALL, 0x103653c5 CALL
		{
			Aim = RunTask19Species::EnemyLkp(*this);                               // 0x103653cd
		}
		else
		{
			RunTask19Species::FailAt(*this, RunTask19Species::GFileBach, 0x428, 6); // 0x10365400
		}
		MotorSetIdealYawToTargetAndUpdate(Aim, RunTask19Species::YawSpeedHold);    // 0x10365416
		if (bBachCamperFlag)                                                       // 0x10365423
		{
			double Warning = 0.0;
			if (BachWasOccluded == 0)                                              // 0x1036542d
			{
				const double Wait = Now + 0.15;                                    // 0x1036544f `_DAT_104aaac4`
				bBachCamperFlag = false;                                           // 0x10365455
				BachReusedOccludeCount = 0;                                        // 0x1036545c
				BaseScheduleHost.WaitFinished = Wait;
				Warning = Wait + 1000000000.0;                                     // 0x1036546c `_DAT_104aaac8`
			}
			else
			{
				Warning = Now + 1000000000.0;                                      // 0x10365438
				BaseScheduleHost.WaitFinished = Warning;
			}
			BachWarningTime = Warning;                                             // 0x10365472
		}
		if (BachWarningTime <= Now)                                                // 0x10365480 / 0x1036548d
		// same arm: 0x1036549c CALL, 0x103654a8 CALL, 0x103654ba CALL, 0x103654d5 CALL, 0x103654e0 CALL
		{
			RunTask19Species::EmitWav(*this, 2, TEXT("Character/Boss/Bach/snipe_warn6.wav")); // 0x10365519 / 0x1036552b
			BachWarningTime += 999999.0;                                           // 0x10365534 `_DAT_104aaacc`
			// same arm: 0x10365542 CALL, 0x10365550 JL, 0x10365559 CALL, 0x1036555f JNZ, 0x1036556d CALL
			//   0x10365581 JZ, 0x10365585 JZ, 0x10365588 CALL
			// `0x10365840` is a one-byte `RET`.
		}
		if (!(Now >= BaseScheduleHost.WaitFinished))                               // 0x1036559a / 0x103655a2 AND 0x100 / 0x103655a7 (NaN runs)
		{
			return 0;
		}
		bBachShotLatch = true;                                                     // 0x103655b1
		TaskComplete(false);                                                       // 0x103655b8
		return 0;
	}
	if (Id == 0x14a)                                                               // 0x103652d0
	{
		return 0;
	}
	return FElysiumNpcHuman::RunTaskSlot444(Arg0);                                 // 0x103652d5
}

// --- `CNPC_VChangBros::RunTask` `0x1036bfc0`, 1,159 bytes ---------------------------------------------
// Also carries the inherited body of CNPC_VChangBrosBlade, CNPC_VChangBrosClaw.
int32 FElysiumNpcChangBros::RunTaskSlot444(void* Arg0)
{
	const FElysiumScheduleStep* Step = RunTask19Species::StepOf(Arg0);
	if (Step == nullptr)
	{
		return 0;
	}
	const double Now = RunTask19Species::NowOf(*this);
	if (!bChangCenterStored)                                                       // 0x1036c016
	// same arm: 0x1036c01e JNZ
	{
		StoreArenaCenter();                                                        // 0x1036c022
		// same arm: 0x1036c039 JA
	}
	switch (RunTask19Species::LocalTaskOf(*this, Step))                                                          // 0x1036c047
	{
	case 0x8b:
	{
		bool bRollArmed = false;
		AutoMovement();                                                            // 0x1036c2f0
		FElysiumCombatCharacter* Enemy = RunTask19Species::EnemyCharacter(*this);  // 0x1036c2f9 / 0x1036c307
		// same arm: 0x1036c301 JZ, 0x1036c30f JZ, 0x1036c319 CALL, 0x1036c329 CALL, 0x1036c331 CALL
		if (Enemy == nullptr)
		{
			return 0;
		}
		RunTask19Species::FaceEnemyLkp(*this, RunTask19Species::YawSpeedHold);    // 0x1036c346
		// `GetMeleeDiceRolls` on the ENEMY (`0x10345980`: the attacker's array `+0xa88`, keyed by the
		// defender, which `CalcAndStoreMeleeDiceRolls 0x10346380` fills) keyed by this body: the
		// enemy's swing at this NPC. The port stores that record on the DEFENDER keyed by the
		// attacker, so it is this body's row for the enemy (L05 integration: was inverted).
		const FElysiumMeleeRoll* Roll = FindMeleeRoll(Enemy->Handle);              // 0x1036c34e `GetMeleeDiceRolls`
		if (Roll != nullptr && MeleeRollBand(*Roll) == 0)                          // 0x1036c353 / 0x1036c359
		// same arm: 0x1036c355 JZ, 0x1036c360 JNZ
		{
			bRollArmed = true;                                                     // 0x1036c362
		}
		const bool bOver = IdealActivityNumber == 0x1157                          // 0x1036c364
		// same arm: 0x1036c36e JNZ
			? EnemyMeleeSwingOver(*Enemy)                                          // 0x1036c372
			: IsActivityFinished();                                                // 0x1036c37d
		if (!bOver)                                                                // 0x1036c385
		{
			return 0;
		}
		if (bRollArmed)                                                            // 0x1036c38b
		// same arm: 0x1036c38f JZ
		{
			TaskComplete(false);                                                   // 0x1036c393
			return 0;
		}
		RunTask19Species::FailAt(*this, RunTask19Species::GFileChangBros, 0x295, 0x21); // 0x1036c3c0
		return 0;
	}
	case 0x13a:
		FElysiumNpcVampireBoss::RunTaskSlot444(Arg0);                              // 0x1036c3d9
		if (ActivityNumber != 0x2d                                                 // 0x1036c3de
		// same arm: 0x1036c3e5 JZ, 0x1036c3eb CALL
			&& Velocity.Z / ElysiumMove::U <= 500.0f)                              // 0x1036c409 / 0x1036c414 `_DAT_104ada50`
		{
			RestartIdealActivityId(0x2d);                                          // 0x1036c41a
		}
		return 0;
	case 0x150:
	case 0x151:
	case 0x153:
	case 0x15d:
		MotorUpdateYaw(RunTask19Species::UpdateYawDefault);                        // 0x1036c09e
		AutoMovement();                                                            // 0x1036c0a5
		if (IsActivityFinished())                                                  // 0x1036c0ae / 0x1036c1f5
		// same arm: 0x1036c1f7 JZ
		{
			TaskComplete(false);                                                   // 0x1036c201
		}
		return 0;
	case 0x156:
		MotorUpdateYaw(RunTask19Species::UpdateYawDefault);                        // 0x1036c056
		AutoMovement();                                                            // 0x1036c05d
		if (Now >= ChangEnergyChargeTime)   /* NaN runs: 0x1036c072 AND 0x100 */                                        // 0x1036c06a / 0x1036c077
		{
			TaskComplete(false);                                                   // 0x1036c081
		}
		return 0;
	case 0x157:
		if (!bChangEnergyBallSpawned && 0.591f <= SequenceCycle)                   // 0x1036c1ab / 0x1036c1c0 `_DAT_104ada38`
		{
			bChangEnergyBallSpawned = true;                                        // 0x1036c1c4
			SpawnEnergyBall();                                                     // 0x1036c1cb
			KillBodyEmitters();                                                    // 0x1036c1d2
		}
		if (IsActivityFinished())                                                  // 0x1036c1db
		{
			TaskComplete(false);                                                   // 0x1036c201
		}
		return 0;
	case 0x15a:
	{
		FElysiumNpcChangBros* Other = GetOtherBrother();                           // 0x1036c1e5
		if (Other == nullptr)                                                      // 0x1036c1ec
		{
			RunTask19Species::FailAt(*this, RunTask19Species::GFileChangBros, 0x255, 1); // 0x1036c230
			return 0;
		}
		if (Other->ReadyForUnited())                                               // 0x1036c1f0
		{
			TaskComplete(false);                                                   // 0x1036c201
		}
		return 0;
	}
	case 0x15b:
	{
		MotorUpdateYaw(RunTask19Species::UpdateYawDefault);                        // 0x1036c0c1
		AutoMovement();                                                            // 0x1036c0c8
		if (!IsActivityFinished())                                                 // 0x1036c0d1
		// same arm: 0x1036c0d9 JZ
		{
			return 0;
		}
		ClearBodyEmitterNames();                                                   // 0x1036c0e1
		SetBodyEmitterName(3, ChangPowerupEmitterName());                          // 0x1036c0ef
		SetBodyEmitterName(2, ChangSpineEmitterName());                            // 0x1036c0fd
		VampireBossSpawnBodyEmitters();                                            // 0x1036c104
		KillCenterEmitter();                                                       // 0x1036c10b
		// `0x100fbc90("chang_center_emitter", m_vArenaCenter + (0, 0, 50.0), vec3_angle, -1.0)`.
		FVector CenterUnits = ChangArenaCenter / ElysiumMove::U;
		CenterUnits.Z += 50.0f;                                                    // 0x1036c11b `_DAT_104ada5c`
		// `0x1036c14c CALL 0x1000389b` -> `0x100fbc90`, the named-emitter create (family Damage's
		// `CreateNamedEmitter`; L05 integration: was the classname `CreateNamedEntity` seam, which
		// made no particle). The port's emitter is an effect, not an entity, so no `GetRefEHandle`
		// (`0x1036c15c`) answers: `m_hCenterEmitter` takes retail's null arm, -1 (`0x1036c184`).
		CreateNamedEmitter(TEXT("chang_center_emitter"), CenterUnits, /*AttachMode*/ 0,
			FElysiumEntityHandle(), nullptr);                                      // 0x1036c14c
			// same arm: 0x1036c156 JZ, 0x1036c15c CALL
		ChangCenterEmitter = FElysiumEntityHandle::Invalid();                      // 0x1036c165 / 0x1036c184
		TaskComplete(false);                                                       // 0x1036c16b / 0x1036c18e
		return 0;
	}
	case 0x15c:
	{
		AutoMovement();                                                            // 0x1036c248
		FElysiumNpcChangBros* Other = GetOtherBrother();                           // 0x1036c24f
		if (Other == nullptr)                                                      // 0x1036c258
		{
			RunTask19Species::FailAt(*this, RunTask19Species::GFileChangBros, 0x26b, 1); // 0x1036c2d6
			return 0;
		}
		if (ChangUnitedTime < Other->ChangUnitedTime)                              // 0x1036c260 / 0x1036c26b
		{
			ChangUnitedTime = Other->ChangUnitedTime;                              // 0x1036c273
		}
		if (ChangUnitedTime <= Now)                                                // 0x1036c282 / 0x1036c28f
		{
			KillBodyEmitters();                                                    // 0x1036c297
			KillCenterEmitter();                                                   // 0x1036c29e
			TaskComplete(false);                                                   // 0x1036c2a7
		}
		return 0;
	}
	default:
		break;
	}
	return FElysiumNpcVampireBoss::RunTaskSlot444(Arg0);                           // 0x1036c432
}

// --- `CNPC_VDog::RunTask` `0x10374a20`, 82 bytes -----------------------------------------------------
int32 FElysiumNpcDog::RunTaskSlot444(void* Arg0)
{
	const FElysiumScheduleStep* Step = RunTask19Species::StepOf(Arg0);
	if (Step != nullptr && (RunTask19Species::LocalTaskOf(*this, Step) == 2 || RunTask19Species::LocalTaskOf(*this, Step) == 0x67)             // 0x10374a2d / 0x10374a32
		&& NpcStateRetail() == 3)                                                  // 0x10374a34 `m_NPCState == ALERT`
		// same arm: 0x10374a3b JNZ
	{
		if (FElysiumPlayer* Player = World != nullptr ? World->FindPlayer() : nullptr) // 0x10374a3d `UTIL_GetLocalPlayer`
		// same arm: 0x10374a44 JZ
		{
			MotorSetIdealYawToTargetAndUpdate(Player->Origin, RunTask19Species::YawSpeedHold); // 0x10374a56 / 0x10374a5f
		}
	}
	return FElysiumNpcAnimal::RunTaskSlot444(Arg0);                                // 0x10374a68
}

// --- `CNPC_VGargoyle::RunTask` `0x103793e0`, 103 bytes -----------------------------------------------
int32 FElysiumNpcGargoyle::RunTaskSlot444(void* Arg0)
{
	const FElysiumScheduleStep* Step = RunTask19Species::StepOf(Arg0);
	if (Step == nullptr)
	{
		return 0;
	}
	if (RunTask19Species::LocalTaskOf(*this, Step) == 0x31)                                                      // 0x103793ec
	{
		RunTask19Species::TurnUnlessMemory(*this);                                 // 0x10379416 / 0x1037941c
		MotorUpdateYaw(RunTask19Species::UpdateYawDefault);                        // 0x1037942a
		if (FacingIdeal())                                                         // 0x10379431 / 0x10379438
		{
			TaskComplete(false);                                                   // 0x1037943e
		}
		return 0;
	}
	if (RunTask19Species::LocalTaskOf(*this, Step) == 0x12f)                                                     // 0x103793f3
	{
		if (IsActivityFinished())                                                  // 0x10379405
		{
			TaskComplete(false);                                                   // 0x1037943e
		}
		return 0;
	}
	return FElysiumNpcHuman::RunTaskSlot444(Arg0);                                 // 0x103793f8
}

// --- `CNPC_VGhoulCroucher::RunTask` `0x1037b9f0`, 198 bytes ------------------------------------------
int32 FElysiumNpcGhoulCroucher::RunTaskSlot444(void* Arg0)
{
	const FElysiumScheduleStep* Step = RunTask19Species::StepOf(Arg0);
	if (Step == nullptr)
	{
		return 0;
	}
	if (RunTask19Species::LocalTaskOf(*this, Step) == 0x14a)                                                     // 0x1037ba65
	// same arm: 0x1037b9f5 JZ, 0x1037b9ff JNZ
	{
		if (IsActivityFinished())                                                  // 0x1037ba98 / 0x1037baa0
		{
			TaskComplete(false);                                                   // 0x1037baa6
		}
		return 0;
	}
	if (RunTask19Species::LocalTaskOf(*this, Step) == 0x14b)                                                     // 0x1037ba68
	{
		if (IsActivityFinished())                                                  // 0x1037ba81 / 0x1037ba89
		{
			bUnawareExited = true;                                                 // 0x1037ba8b +0x6667
			TaskComplete(false);                                                   // 0x1037baa6
		}
		return 0;
	}
	return FElysiumNpcHuman::RunTaskSlot444(Arg0);                                 // 0x1037ba6d
}

// --- `CNPC_VHengeyokai::RunTask` `0x10380cb0`, 509 bytes ----------------------------------------------
int32 FElysiumNpcHengeyokai::RunTaskSlot444(void* Arg0)
{
	const FElysiumScheduleStep* Step = RunTask19Species::StepOf(Arg0);
	if (Step == nullptr)
	{
		return 0;
	}
	const double Now = RunTask19Species::NowOf(*this);
	switch (RunTask19Species::LocalTaskOf(*this, Step))
	{
	case 0x31:
	case 0xc8:
		RunTask19Species::TurnUnlessMemory(*this);                                 // 0x10380d86 / 0x10380d9d
		// same arm: 0x10380d89 JNZ, 0x10380d8f CALL, 0x10380da0 JNZ, 0x10380da6 CALL
		MotorUpdateYaw(RunTask19Species::UpdateYawDefault);                        // 0x10380db4
		if (FacingIdeal())                                                         // 0x10380dbb / 0x10380dc2
		{
			TaskComplete(false);                                                   // 0x10380dcc
		}
		return 0;
	case 0xc9:
	{
		FElysiumEntity* Enemy = RunTask19Species::Enemy167(*this);                 // 0x10380ced
		// same arm: 0x10380cc2 JG, 0x10380cc8 JZ, 0x10380cd1 JZ, 0x10380cdc JZ, 0x10380ce3 JNZ
		//   0x10380cff CALL
		const FVector Lkp = RunTask19Species::EnemyLkp(*this);                     // 0x10380d07
		RunTask19Species::TurnUnlessMemory(*this);                                 // 0x10380d12 / 0x10380d1b
		// same arm: 0x10380d15 JNZ
		MotorSetIdealYawToTargetAndUpdate(Lkp, RunTask19Species::YawSpeedDefault); // 0x10380d31
		if (!FUN_103822a0(Enemy))                                                  // 0x10380d39 / 0x10380d40
		{
			return 0;
		}
		if (Cognition.Conditions.HasOrdinal(0x1b)                                  // 0x10380d4a / 0x10380d51
			|| Now >= HengeyokaiTaskFailTimer)   /* NaN runs: 0x10380d64 AND 0x100 */                                   // 0x10380d5c / 0x10380d69
		{
			TaskComplete(false);                                                   // 0x10380d73 / 0x10380dcc
		}
		return 0;
	}
	case 0xca:
	{
		FElysiumEntity* Enemy = GetEnemy();                                        // 0x10380ddd slot 168
		// same arm: 0x10380def CALL
		const FVector Lkp = RunTask19Species::EnemyLkp(*this);                     // 0x10380df7
		RunTask19Species::TurnUnlessMemory(*this);                                 // 0x10380e02 / 0x10380e0b
		// same arm: 0x10380e05 JNZ
		MotorSetIdealYawToTargetAndUpdate(Lkp, RunTask19Species::YawSpeedDefault); // 0x10380e21
		if (FUN_103822a0(Enemy))                                                   // 0x10380e29 / 0x10380dc0
		// same arm: 0x10380e38 JA, 0x10380e42 JMP
		{
			TaskComplete(false);                                                   // 0x10380dcc
		}
		return 0;
	}
	case 0x132:
	case 0x133:
		ClearLinkActivity();                                                       // 0x10380e4b `0x10382d20`
		if (IsActivityFinished())                                                  // 0x10380e54
		// same arm: 0x10380e5c JZ
		{
			if (Motor != nullptr)
			{
				Motor->ResetSteering();                                            // 0x10380e69 `0x102e0a60(motor, 180.0)`
			}
			TaskComplete(false);                                                   // 0x10380e72
		}
		return 0;
	case 0x134:
		if (IsActivityFinished())                                                  // 0x10380e83
		{
			TaskComplete(false);                                                   // 0x10380dcc
		}
		return 0;
	case 0x14b:
		HengeyokaiTask14b();                                                       // 0x10380e90 `0x10383470`
		return 0;
	default:
		break;
	}
	return FElysiumNpcHuman::RunTaskSlot444(Arg0);                                 // 0x10380ea0
}

// --- `CNPC_VHuman::RunTask` `0x10384ab0`, 806 bytes ---------------------------------------------------
// Also carries the inherited body of CNPC_ProneDialog, CNPC_VBrujah, CNPC_VCop, CNPC_VFrenzyShadow,
// CNPC_VGuard1, CNPC_VHumanCombatPatrol, CNPC_VHumanCombatant, CNPC_VHunter, CNPC_VLasombra,
// CNPC_VPedestrian, CNPC_VPlayerController, CNPC_VSabbatGunman, CNPC_VVampire, CNPC_VWolfMorph,
// CNPC_VYukie.
int32 FElysiumNpcHuman::RunTaskSlot444(void* Arg0)
{
	const FElysiumScheduleStep* Step = RunTask19Species::StepOf(Arg0);
	if (Step == nullptr)
	{
		return 0;
	}
	const double Now = RunTask19Species::NowOf(*this);
	switch (RunTask19Species::LocalTaskOf(*this, Step))                                                          // 0x10384acd
	// same arm: 0x10384ac7 JA
	{
	case 0x89:
	case 0x8a:
		AutoMovement();                                                            // 0x10384ad6
		// same arm: 0x10384adf CALL, 0x10384aef CALL
		RunTask19Species::FaceEnemyLkp(*this, RunTask19Species::YawSpeedHold);    // 0x10384af7 / 0x10384b0c
		if (IsActivityFinished()                                                   // 0x10384b15 / 0x10384b1d
			|| static_cast<float>(Now - LastAttackTime) > Step->Data)              // 0x10384b31
		{
			TaskComplete(false);                                                   // 0x10384cc9
		}
		return 0;
	case 0x8b:
	{
		bool bRollArmed = false;
		AutoMovement();                                                            // 0x10384b44
		FElysiumCombatCharacter* Enemy = RunTask19Species::EnemyCharacter(*this);  // 0x10384b4d / 0x10384b63
		// same arm: 0x10384b6d CALL, 0x10384b7d CALL, 0x10384b85 CALL
		if (Enemy == nullptr)
		{
			return 0;
		}
		RunTask19Species::FaceEnemyLkp(*this, RunTask19Species::YawSpeedHold);    // 0x10384b9a
		// The enemy's swing at this NPC (see ChangBros 0x8b; `0x10384b9f PUSH ESI` / `MOV ECX,EDI`):
		// the port keeps it on the defender keyed by the attacker (L05 integration: was inverted).
		const FElysiumMeleeRoll* Roll = FindMeleeRoll(Enemy->Handle);              // 0x10384ba2 `GetMeleeDiceRolls`
		bool bOver = false;
		if (IdealActivityNumber == 0x1157)                                         // 0x10384ba7 / 0x10384bb1
		{
			bOver = EnemyMeleeSwingOver(*Enemy);                                   // 0x10384bed
		}
		else
		{
			float Threshold = 1.0f;                                                // `_DAT_104b73f0`
			if (Roll != nullptr)                                                   // 0x10384bb5
			{
				bRollArmed = MeleeRollBand(*Roll) == 0;                            // 0x10384bb9 / 0x10384bc0
				Threshold = 0.5f;                                                  // `_DAT_104b73ec`
			}
			// `Threshold <= m_flCycle` jumps straight to the finish (`0x10384bdd JNP`).
			bOver = Threshold <= SequenceCycle || IsActivityFinished();            // 0x10384bd2 / 0x10384be3
		}
		if (!bOver)                                                                // 0x10384bf4
		{
			return 0;
		}
		if (SelectWeightedSequenceForActivity(0x1155) != INDEX_NONE && bRollArmed) // 0x10384c03..0x10384c0f `0x10295460`
		// same arm: 0x10384c0b JZ
		{
			TaskComplete(false);                                                   // 0x10384c15
			return 0;
		}
		RunTask19Species::FailAt(*this, RunTask19Species::GFileHuman, 0x1da, 0x21); // 0x10384c3e
		return 0;
	}
	case 0x8d:
	{
		AutoMovement();                                                            // 0x10384cda
		FElysiumCombatCharacter* Enemy = RunTask19Species::EnemyCharacter(*this);  // 0x10384ce3 / 0x10384cf5
		// same arm: 0x10384ceb JZ
		if (Enemy == nullptr)
		{
			TaskComplete(false);                                                   // 0x10384cfb
			// same arm: 0x10384d0e CALL, 0x10384d1e CALL, 0x10384d26 CALL
			return 0;
		}
		RunTask19Species::FaceEnemyLkp(*this, RunTask19Species::YawSpeedHold);    // 0x10384d3b
		bool bHasSequence = false;
		const bool bPending = EnemySequenceEventsPending(*Enemy, bHasSequence);    // 0x10384d49..0x10384daf
		if (!bHasSequence || bPending)                                             // 0x10384d52 / 0x10384da4
		// same arm: 0x10384d57 CALL, 0x10384d8d JLE
		{
			return 0;
		}
		TaskComplete(false);                                                       // 0x10384db5
		return 0;
	}
	case 0x8e:
	case 0x8f:
	case 0x90:
	case 0x91:
		if (RunTask19Species::EnemyCharacter(*this) != nullptr)                    // 0x10384c52..0x10384c64
		// same arm: 0x10384c5a JZ, 0x10384c6a CALL, 0x10384c7a CALL, 0x10384c82 CALL
		{
			RunTask19Species::FaceEnemyLkp(*this, RunTask19Species::YawSpeedHold); // 0x10384c97
		}
		if (IsActivityFinished()                                                   // 0x10384ca0 / 0x10384ca8
			|| Now >= NextAttackTime)   /* NaN runs: 0x10384cba AND 0x100 */                                            // 0x10384cb2 `m_flNextAttack` +0x1564
			// same arm: 0x10384cbf JNZ
		{
			TaskComplete(false);                                                   // 0x10384cc9
		}
		return 0;
	default:
		break;
	}
	return FElysiumNpc::RunTaskSlot444(Arg0);                                      // 0x10384dc7
}

// --- `CNPC_VManBat::RunTask` `0x1038d130`, 2,923 bytes -------------------------------------------------
int32 FElysiumNpcManBat::RunTaskSlot444(void* Arg0)
{
	const FElysiumScheduleStep* Step = RunTask19Species::StepOf(Arg0);
	if (Step == nullptr)
	{
		return 0;
	}
	const double Now = RunTask19Species::NowOf(*this);
	auto Flap = [this]()
	{
		// `0x1038e640`: `SetIdealActivity(0x22)`, `m_flFlapTimer = curtime + 2.3`.
		if (const FFlapActivity* Row = FlapActivityOf(TEXT("0x1038e640")))
		{
			SetFlapActivity(Row->Activity, Row->Seconds);
		}
	};
	auto FlyByTargetAlive = [this]() -> FElysiumEntity*
	{
		FElysiumEntity* Target = World != nullptr ? World->Resolve(ManBatFlyByTarget) : nullptr;
		return Target != nullptr && Target->IsAlive() ? Target : nullptr;           // slot 158
	};
	switch (RunTask19Species::LocalTaskOf(*this, Step))                                                          // 0x1038d15b
	// same arm: 0x1038d14d JA
	{
	case 0x14a:
		if (FElysiumEntity* Node = World != nullptr ? World->Resolve(ManBatFlyNode) : nullptr) // 0x1038d16a
		// same arm: 0x1038d170 CALL, 0x1038d180 CALL
		{
			// Retail aims at the SUM of this body's and the node's absolute origins (reproduced).
			MotorSetIdealYawToTargetAndUpdate(Origin + Node->Origin, RunTask19Species::YawSpeedHold); // 0x1038d186..0x1038d1ca
		}
		if (IsActivityFinished())                                                  // 0x1038d1d3
		{
			TaskComplete(false);                                                   // 0x1038d84a
			Flap();                                                                // 0x1038d851
		}
		return 0;
	case 0x14b:
		if (!bManBatReachedMoveGoal)                                               // 0x1038d1e6
		{
			return 0;
		}
		if (ManBatHintMode(ManBatHintModeWord) == HintObfuscationFold(0xfa0b0695u)) // 0x1038d1f7 / 0x1038d21c / 0x1038d224
		// same arm: 0x1038d226 JNZ
		{
			if (FElysiumEntity* Node = World != nullptr ? World->Resolve(ManBatFlyNode) : nullptr)
			{
				MotorSetOriginToTraceEnd(NpcKernelMotor2Shared::MotorTailSourceOf(Node->Origin));            // 0x1038d232 / 0x1038d23a `UTIL_SetOrigin`
			}
		}
		ManBatFlyNode = FElysiumEntityHandle::Invalid();                           // 0x1038d27a
		TaskComplete(false);                                                       // 0x1038d284
		return 0;
	case 0x14c:
		if (!IsOnGroundFlag())                                                     // 0x1038d317 / 0x1038d31e
		{
			return 0;
		}
		Velocity = FVector::ZeroVector;                                            // 0x1038d343 `SetAbsVelocity`
		ManBatLeaveFlight(0);                                                      // 0x1038d34c `0x1038c170`
		ManBatFlyNode = FElysiumEntityHandle::Invalid();                           // 0x1038d355
		TaskComplete(false);                                                       // 0x1038d35f
		SetIdealActivity(0x1171);                                                  // 0x1038d36b
		// same arm: 0x1038d379 CALL, 0x1038d385 CALL, 0x1038d397 CALL, 0x1038d3b2 CALL, 0x1038d3bd CALL
		//   0x1038d3c6 CALL
		RunTask19Species::EmitWav(*this, 4, TEXT("character/male/sheriff_manbat/fall.wav")); // 0x1038d3ff / 0x1038d411
		// same arm: 0x1038d418 CALL, 0x1038d421 CALL
		return 0;
	case 0x14d:
		// `CalcAbsoluteVelocity` (`0x1038d2a6`) is a no-op here; aim at origin + absolute velocity.
		// same arm: 0x1038d2af CALL
		MotorSetIdealYawToTargetAndUpdate(Origin + Velocity, RunTask19Species::YawSpeedHold); // 0x1038d303
		return 0;
	case 0x150:
		if (IsActivityFinished())                                                  // 0x1038d437
		// same arm: 0x1038d43f JZ
		{
			ManBatLeaveFlight(0);                                                  // 0x1038d449
			TaskComplete(false);                                                   // 0x1038d452
			SetIdealActivity(1);                                                   // 0x1038d45b
		}
		return 0;
	case 0x152:
	case 0x155:
		if (bManBatReachedMoveGoal)                                                // 0x1038d24c
		{
			Velocity = FVector::ZeroVector;                                        // 0x1038d271
			ManBatFlyNode = FElysiumEntityHandle::Invalid();                       // 0x1038d27a
			TaskComplete(false);                                                   // 0x1038d284
			// same arm: 0x1038d2a2 JZ
		}
		return 0;
	case 0x156:
		if (!IsActivityFinished())                                                 // 0x1038d471
		// same arm: 0x1038d479 JZ
		{
			return 0;
		}
		ReleasePickupAnimlink(nullptr);                                            // 0x1038d481 `0x1038f790`
		TaskComplete(false);                                                       // 0x1038d84a
		Flap();                                                                    // 0x1038d851
		return 0;
	case 0x157:
		if (bManBatReachedMoveGoal)
		{
			TaskComplete(false);
		}
		return 0;
	case 0x158:
		if (IsActivityFinished())                                                  // 0x1038d838
		// same arm: 0x1038d840 JZ
		{
			TaskComplete(false);                                                   // 0x1038d84a
			Flap();                                                                // 0x1038d851
		}
		return 0;
	case 0x159:
	case 0x160:
	{
		FElysiumEntity* TargetEntity = FlyByTargetAlive();                               // 0x1038d494..0x1038d4fa
		// same arm: 0x1038d4b8 JNZ, 0x1038d4c2 JZ, 0x1038d4d1 JZ, 0x1038d4e8 JNZ, 0x1038d4f2 CALL
		//   0x1038d509 JZ, 0x1038d526 JNZ
		if (TargetEntity == nullptr)
		{
			TaskFail(1);                                                           // 0x1038d6e8
			// same arm: 0x1038d704 JZ, 0x1038d728 JNZ, 0x1038d72e JZ, 0x1038d739 JZ, 0x1038d750 JNZ
			//   0x1038d75a CALL, 0x1038d762 JZ, 0x1038d766 CALL, 0x1038d773 JZ, 0x1038d77d CALL
			//   0x1038d795 CALL
			return 0;
		}
		if (World != nullptr && TargetEntity == World->FindPlayer())                     // 0x1038d536 `target+0xa8`
		{
			ManBatFlyBySound();                                                    // 0x1038d53a `0x1038fe30`
		}
		if (!bManBatReachedMoveGoal)                                               // 0x1038d547
		// same arm: 0x1038d556 JZ, 0x1038d573 JNZ
		{
			return 0;
		}
		// Ten units beyond the target, on this body's side, standing on the target's box.
		FVector Pos = NpcKernelMotor2Shared::MotorTailSourceOf(TargetEntity->Origin);                             // 0x1038d57d
		FVector Away = NpcKernelMotor2Shared::MotorTailSourceOf(Origin) - Pos;                              // 0x1038d59b..0x1038d5aa
		Away.Z = 0.0;
		Away.Normalize();                                                          // 0x1038d5c4 `VectorNormalize`
		Pos.X += Away.X * 10.0f;                                                   // 0x1038d5d0 `_DAT_1044e664`
		Pos.Y += Away.Y * 10.0f;                                                   // 0x1038d5f1
		// same arm: 0x1038d5ff JZ, 0x1038d619 JNZ, 0x1038d630 JZ, 0x1038d647 JNZ
		FVector Mins = FVector::ZeroVector;
		FVector Maxs = FVector::ZeroVector;
		RetailCollisionExtents(*TargetEntity, Mins, Maxs);                               // 0x1038d65b / 0x1038d664
		Pos.Z = (Maxs.Z - Mins.Z) + Pos.Z + 1.0;                                   // 0x1038d66a..0x1038d67a
		MotorSetOriginToTraceEnd(Pos);                                             // 0x1038d684 slot 216
		SetIdealActivity(0x4b);                                                    // 0x1038d68e
		// same arm: 0x1038d69c JZ, 0x1038d6b9 JNZ
		Slot596(World != nullptr ? World->Resolve(ManBatFlyByTarget) : nullptr);   // 0x1038d6c6 slot 596
		TaskComplete(false);                                                       // 0x1038d6d0
		return 0;
	}
	case 0x15a:
	{
		if (!IsActivityFinished())                                                 // 0x1038d7ac
		// same arm: 0x1038d7b4 JZ
		{
			return 0;
		}
		FElysiumEntity* Weapon = ActiveWeaponEntity();                             // 0x1038d7bc
		if (Weapon != nullptr && (ActiveWeaponCapabilityWord() & 0x18000u) != 0)   // 0x1038d7c5 / 0x1038d7cb
		// same arm: 0x1038d7d6 JZ
		{
			LastAttackTime = Now;                                                  // 0x1038d7e3
			++ManBatWeaponAttacks;                                                 // 0x1038d7eb weapon slot 326
			TaskComplete(false);                                                   // 0x1038d7f5
			return 0;
		}
		RunTask19Species::FailAt(*this, RunTask19Species::GFileManBat, 0x4e1, 0x1f); // 0x1038d821
		return 0;
	}
	case 0x15b:
	{
		if (!IsActivityFinished())                                                 // 0x1038d867
		// same arm: 0x1038d86f JZ, 0x1038d87f CALL
		{
			return 0;
		}
		if (FElysiumEntity* Light = ManBatNearestSpotlight())                      // 0x1038d890
		// same arm: 0x1038d899 JZ, 0x1038d8a7 JZ
		{
			const FString Name = Light->TargetName;
			if (Name.Equals(TEXT("Spotlight 1"), ESearchCase::IgnoreCase)          // 0x1038d8a9..0x1038d905
			// same arm: 0x1038d8ae JZ, 0x1038d8c1 JNZ, 0x1038d8d9 JZ, 0x1038d8dd JNZ, 0x1038d8ea CALL
			//   0x1038d8f6 JNZ, 0x1038d914 JZ, 0x1038d925 JZ
				|| Name.Equals(TEXT("Spotlight 2"), ESearchCase::IgnoreCase))      // 0x1038d927..0x1038d981
				// same arm: 0x1038d92c JZ, 0x1038d93d JNZ, 0x1038d955 JZ, 0x1038d959 JNZ, 0x1038d966 CALL
				//   0x1038d972 JNZ, 0x1038d990 JZ
			{
				ManBatKillSpotlight(*Light);                                       // 0x1038d9ac..0x1038d9fd
				// same arm: 0x1038d9b5 CALL, 0x1038d9c1 CALL, 0x1038d9d1 CALL, 0x1038d9e1 CALL, 0x1038d9f2 CALL
				//   0x1038d9f9 JZ
			}
		}
		TaskComplete(false);                                                       // 0x1038da06
		return 0;
	}
	case 0x15c:
	{
		if (!bManBatReachedMoveGoal)                                               // 0x1038da20
		{
			return 0;
		}
		++ManBatMoveGoalNodeId;                                                    // 0x1038da37
		if (ManBatMaxScriptNode < ManBatMoveGoalNodeId)                            // 0x1038da3d / 0x1038da3f
		{
			TaskComplete(false);                                                   // 0x1038da43
			return 0;
		}
		FElysiumEntity* Node = ManBatFindMoveGoalHint(20000, 5000.0f);             // 0x1038da64 `0x102d1af0`
		ManBatFlyNode = Node != nullptr ? Node->Handle : FElysiumEntityHandle::Invalid(); // 0x1038da6c
		if (Node == nullptr)                                                       // 0x1038da76
		{
			TaskFail(4);                                                           // 0x1038dabc
			return 0;
		}
		bManBatReachedMoveGoal = false;                                            // 0x1038da7f
		FVector VelocityUnits = FVector::ZeroVector;
		FUN_1038b370(0.f, VelocityUnits);                                          // 0x1038da86
		ManBatWingTurnSelect(VelocityUnits);                                       // 0x1038daa6 `0x1038e720` (L10's body)
		return 0;
	}
	case 0x161:
	{
		FElysiumEntity* TargetEntity = FlyByTargetAlive();                               // 0x1038dad8..0x1038db3e
		// same arm: 0x1038dafc JNZ, 0x1038db06 JZ, 0x1038db15 JZ, 0x1038db2c JNZ, 0x1038db36 CALL
		if (TargetEntity == nullptr)
		{
			TaskFail(1);                                                           // 0x1038dc3f
			Flap();                                                                // 0x1038dc47
			return 0;
		}
		if (!IsActivityFinished())                                                 // 0x1038db48
		// same arm: 0x1038db50 JZ, 0x1038db5f JZ, 0x1038db7c JNZ
		{
			return 0;
		}
		// `ThrowModel(target->GetModelName(), "Bip01 R Neck", 20.0, 0)`; the port's form takes two.
		if (!ThrowModel(TargetEntity->Model, TEXT("Bip01 R Neck")))                      // 0x1038db8b / 0x1038dba8
		// same arm: 0x1038db92 JNZ, 0x1038dbaf JZ, 0x1038dbba JZ, 0x1038dbd7 JNZ
		{
			TaskFail(1);                                                           // 0x1038dc1f
			Flap();                                                                // 0x1038dc27
			return 0;
		}
		TargetEntity->Kill();                                                            // 0x1038dbe1 slot 119
		ManBatMoveGoalNodeId = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(1, 3); // 0x1038dbf3
		TaskComplete(false);                                                       // 0x1038dc00
		Flap();                                                                    // 0x1038dc07
		return 0;
	}
	case 0x162:
	{
		FElysiumEntity* TargetEntity = FlyByTargetAlive();
		if (TargetEntity == nullptr)
		{
			TaskFail(1);
			return 0;
		}
		ManBatFlyBySound();                                                        // `0x1038fe30`
		if (bManBatReachedMoveGoal)                                                // `switchD_1038d15b_caseD_157`
		{
			TaskComplete(false);
		}
		return 0;
	}
	case 0x163:
		if (Now >= ManBatCoastTimer)   /* NaN runs: 0x1038dc69 AND 0x100 */                                             // 0x1038dc61 / 0x1038dc6e
		{
			TaskComplete(false);                                                   // 0x1038dc74
		}
		return 0;
	default:
		break;
	}
	// CAI_BaseNPC::RunTask DIRECT (`0x1038dc89`) -- ManBat skips the Troika spine.
	return FElysiumNpcBase::RunTaskSlot444(Arg0);
}

// --- `CNPC_VMingXiao::RunTask` `0x10393930`, 1,239 bytes ----------------------------------------------
int32 FElysiumNpcMingXiao::RunTaskSlot444(void* Arg0)
{
	const FElysiumScheduleStep* Step = RunTask19Species::StepOf(Arg0);
	if (Step == nullptr)
	{
		return 0;
	}
	const double Now = RunTask19Species::NowOf(*this);
	// `thunk_FUN_101e8da0(0x10739d08)`, MingXiao's tuning record: no table stands here, every field
	// answers 0 (the same seam `ElysiumNpcMingXiao.cpp` states for this record).
	auto Tuning = [](int32) { return 0.f; };
	auto FaceIfOutOfCone = [this]()
	{
		if (RunTask19Species::Enemy167(*this) != nullptr)
		{
			const FVector Lkp = RunTask19Species::EnemyLkp(*this);
			if (!FInAimCone(Lkp))                                                  // slot 364
			{
				MotorSetIdealYawToTargetAndUpdate(Lkp, RunTask19Species::YawSpeedHold);
			}
		}
	};
	switch (RunTask19Species::LocalTaskOf(*this, Step))                                                          // 0x10393957
	// same arm: 0x10393949 JA
	{
	case 0x89:
	case 0x8a:
		AutoMovement();                                                            // 0x10393960
		// same arm: 0x10393969 CALL, 0x10393979 CALL, 0x10393981 CALL
		RunTask19Species::FaceEnemyLkp(*this, RunTask19Species::YawSpeedHold);    // 0x10393996
		if (IsActivityFinished()                                                   // 0x1039399f / 0x103939a7
			|| static_cast<float>(Now - LastAttackTime) > Step->Data)              // 0x103939b7 / 0x103939c1
		{
			TaskComplete(false);                                                   // 0x103939cb
		}
		return 0;
	case 0x8b:
		AutoMovement();                                                            // 0x103939dc
		if (RunTask19Species::EnemyCharacter(*this) != nullptr)                    // 0x103939e5..0x103939f7
		// same arm: 0x103939ed JZ, 0x103939fd CALL, 0x10393a0d CALL, 0x10393a15 CALL
		{
			RunTask19Species::FaceEnemyLkp(*this, RunTask19Species::YawSpeedHold); // 0x10393a2a
		}
		if (IsActivityFinished())                                                  // 0x10393a33
		// same arm: 0x10393a3b JZ
		{
			TaskComplete(false);                                                   // 0x10393a45
		}
		return 0;
	case 0x8e:
	{
		AutoMovement();                                                            // 0x10393a56
		FElysiumCombatCharacter* Enemy = RunTask19Species::EnemyCharacter(*this);  // 0x10393a5f..0x10393a71
		// same arm: 0x10393a67 JZ
		if (Enemy == nullptr)
		{
			TaskComplete(false);                                                   // 0x10393a77
			return 0;
		}
		bool bHasSequence = false;
		const bool bPending = EnemySequenceEventsPending(*Enemy, bHasSequence);    // 0x10393a8a..0x10393b06
		// same arm: 0x10393a98 CALL
		if (!bHasSequence || bPending)                                             // 0x10393aa1 / 0x10393af7
		// same arm: 0x10393aaa CALL, 0x10393ae0 JLE
		{
			return 0;
		}
		TaskComplete(false);                                                       // 0x10393b0c
		return 0;
	}
	case 0x14b:
		MingXiaoTask14b();                                                         // 0x10393b1d `0x1039aa20`
		// same arm: 0x10393b30 CALL, 0x10393b40 CALL, 0x10393b48 CALL
		return 0;
	case 0x14f:
	case 0x150:
	case 0x151:
	case 0x152:
		RunTask19Species::FaceEnemyLkp(*this, RunTask19Species::YawSpeedDefault); // 0x10393b5d
		AutoMovement();                                                            // 0x10393b64
		if (!IsActivityFinished())                                                 // 0x10393b6d
		// same arm: 0x10393b75 JZ, 0x10393b84 JZ, 0x10393ba1 JNZ, 0x10393ba7 JNZ
		{
			return 0;
		}
		// `m_hRangedWeapon`'s `+0xa0` combat-weapon view, or NULL, into slot 388 `Weapon_Switch`.
		Weapon_Switch(World != nullptr ? World->Resolve(MingXiaoRangedWeapon) : nullptr, 0); // 0x10393bba
		TaskComplete(false);                                                       // 0x10393bc4
		return 0;
	case 0x153:
	case 0x154:
	{
		AutoMovement();                                                            // 0x10393bd5
		FaceIfOutOfCone();                                                         // 0x10393bde..0x10393c22
		// same arm: 0x10393be6 JZ, 0x10393bf2 CALL, 0x10393bfa CALL, 0x10393c08 CALL, 0x10393c10 JNZ
		if (!IsActivityFinished())                                                 // 0x10393c2b
		// same arm: 0x10393c33 JZ
		{
			return 0;
		}
		if (MingXiaoThrowableObjectMode != 0)                                      // 0x10393c3f
		// same arm: 0x10393c41 JZ
		{
			LaunchRagdollTowardTarget();                                           // 0x10393c45 `0x103990c0`
		}
		if (Motor != nullptr)
		{
			Motor->ResetSteering();                                                // 0x10393c55 `0x102e0a60(motor, 180.0)`
		}
		const float Rest = FUN_103983d0(MingXiaoThrowingTentacle, Tuning);        // 0x10393c63
		MingXiaoAttackTimers[5] = Now + Rest;                                      // 0x10393c7b
		MingXiaoAttackTimers[4] = Now + Rest;
		TaskComplete(false);                                                       // 0x10393c81
		// same arm: 0x10393c94 CALL, 0x10393ca4 CALL, 0x10393cac CALL
		return 0;
	}
	case 0x155:
		RunTask19Species::FaceEnemyLkp(*this, RunTask19Species::YawSpeedDefault); // 0x10393cc1
		AutoMovement();                                                            // 0x10393cc8
		if (IsActivityFinished())                                                  // 0x10393cd1
		// same arm: 0x10393cd9 JZ
		{
			TaskComplete(false);                                                   // 0x10393ce3
		}
		return 0;
	case 0x158:
		AutoMovement();                                                            // 0x10393cf4
		if (IsActivityFinished())                                                  // 0x10393cfd
		// same arm: 0x10393d05 JZ
		{
			if (MingXiaoThrowableObjectMode != 3)                                  // 0x10393d0b
			// same arm: 0x10393d12 JZ
			{
				MingXiaoTentacleGrab();                                            // 0x10393d16 `0x10398db0`
			}
			TaskComplete(false);                                                   // 0x10393d1f
		}
		return 0;
	case 0x15a:
		AutoMovement();                                                            // 0x10393d30
		FaceIfOutOfCone();                                                         // 0x10393d39..0x10393d7d
		// same arm: 0x10393d41 JZ, 0x10393d4d CALL, 0x10393d55 CALL, 0x10393d63 CALL, 0x10393d6b JNZ
		if (!IsActivityFinished())                                                 // 0x10393d86
		// same arm: 0x10393d8e JZ
		{
			return 0;
		}
		if (MingXiaoThrowableObjectMode != 0)                                      // 0x10393d96
		// same arm: 0x10393d98 JZ
		{
			LaunchRagdollTowardTarget();                                           // 0x10393d9c
		}
		if (Motor != nullptr)
		{
			Motor->ResetSteering();                                                // 0x10393dac
		}
		TaskComplete(false);                                                       // 0x10393db5
		return 0;
	case 0x15b:
	case 0x15c:
		AutoMovement();                                                            // 0x10393dc6
		if (IsActivityFinished())                                                  // 0x10393dcf
		// same arm: 0x10393dd7 JZ
		{
			RestartIdealActivityId(1);                                             // 0x10393ddd
			TaskComplete(false);                                                   // 0x10393de6
		}
		return 0;
	default:
		break;
	}
	return FElysiumNpc::RunTaskSlot444(Arg0);                                      // 0x10393df8
}

// --- `CNPC_VMingXiaoTentacle::RunTask` `0x1039d750`, 1,127 bytes --------------------------------------
int32 FElysiumNpcMingXiaoTentacle::RunTaskSlot444(void* Arg0)
{
	const FElysiumScheduleStep* Step = RunTask19Species::StepOf(Arg0);
	if (Step == nullptr)
	{
		return 0;
	}
	const double Now = RunTask19Species::NowOf(*this);
	switch (RunTask19Species::LocalTaskOf(*this, Step))                                                          // 0x1039d77a
	// same arm: 0x1039d76c JA
	{
	case 0x8b:
		AutoMovement();                                                            // 0x1039d783
		if (RunTask19Species::EnemyCharacter(*this) != nullptr)                    // 0x1039d78c..0x1039d79e
		// same arm: 0x1039d794 JZ, 0x1039d7a4 CALL, 0x1039d7b4 CALL, 0x1039d7bc CALL
		{
			RunTask19Species::FaceEnemyLkp(*this, RunTask19Species::YawSpeedHold); // 0x1039d7d1
		}
		if (IsActivityFinished())                                                  // 0x1039d7da
		// same arm: 0x1039d7e2 JZ
		{
			TaskComplete(false);                                                   // 0x1039d7ec
		}
		return 0;
	case 0x8e:
	{
		AutoMovement();                                                            // 0x1039d7fd
		FElysiumCombatCharacter* Enemy = RunTask19Species::EnemyCharacter(*this);  // 0x1039d806..0x1039d818
		// same arm: 0x1039d80e JZ
		if (Enemy == nullptr)
		{
			TaskComplete(false);                                                   // 0x1039d81e
			return 0;
		}
		bool bHasSequence = false;
		const bool bPending = EnemySequenceEventsPending(*Enemy, bHasSequence);    // 0x1039d836..0x1039d8a4
		if (!bHasSequence || bPending)                                             // 0x1039d83f / 0x1039d895
		// same arm: 0x1039d848 CALL, 0x1039d87e JLE
		{
			return 0;
		}
		TaskComplete(false);                                                       // 0x1039d8aa
		return 0;
	}
	case 0x14c:
	{
		const int32 FlexCount = NumFlexControllers();                              // 0x1039d8bd `GetModelPtr` +0x160
		if (FlexCount <= 1)                                                        // 0x1039d8c6 / 0x1039d8d3
		{
			SetForceFrequentThink(false);                                          // 0x1039da07 slot 416
			TaskComplete(false);                                                   // 0x1039da11
			return 0;
		}
		// `engine->Time()` (slot 119) minus `timeCurTaskStarted`, clamped to [0, 4]. The port's
		// engine clock is curtime (named divergence: retail reads the engine's own time).
		float Blend = static_cast<float>(Now - Schedule.TaskStartedAt);           // 0x1039d8ed / 0x1039d8f3
		// `1039d903 AND EAX,0x4100` / `1039d908 JNZ`: only an ordered `Blend > 4.0` clamps high; the low
		// clamp is `TEST AH,5` / `JP` (an ordered `< 0.0`). A NaN blend passes both (L05 integration).
		if (Blend > 4.0f)                                                          // 0x1039d8fb `_DAT_10450aa0`
		// same arm: 0x1039d908 JNZ
		{
			Blend = 4.0f;                                                          // 0x1039d90a
		}
		else if (Blend < 0.0f)                                                     // 0x1039d918 / 0x1039d923
		{
			Blend = 0.0f;                                                          // 0x1039d925
		}
		const float Step4 = 4.0f / static_cast<float>(FlexCount - 1);             // 0x1039d946
		// same arm: 0x1039d950 JLE
		for (int32 Index = 0; Index < FlexCount; ++Index)                          // 0x1039d958..0x1039d9c9
		{
			float Weight = static_cast<float>(
				ElysiumNpcTunables::OneDouble - FMath::Abs(Blend - Index * Step4) / Step4); // 0x1039d95c..0x1039d96a
			if (Weight < 0.0f)                                                     // 0x1039d974 / 0x1039d97f
			{
				Weight = 0.0f;                                                     // 0x1039d981
			}
			FString Name = FString::Printf(TEXT("F%02d"), Index + 1);             // 0x1039d991 / 0x1039d999
			SetFlexWeight(Name.GetCharArray().GetData(), Weight);                  // 0x1039d9ac slot 280
			EffectsWord |= 0x10u;                                                  // 0x1039d9b8 `m_fEffects`
		}
		if (!(Blend >= 4.0f))                                                      // 0x1039d9cf / 0x1039d9d7 AND 0x100 / 0x1039d9dc (NaN runs)
		{
			return 0;
		}
		SetForceFrequentThink(false);                                              // 0x1039d9e8
		TaskComplete(false);                                                       // 0x1039d9f2
		return 0;
	}
	case 0x150:
	{
		// The type-19000 hint walk from the shared cursor (`DAT_1093bd34`), else from the list head
		// (`DAT_10925450`): the FIRST such hint is tested, its successor becomes the cursor, and the
		// body returns; a walk that finds none clears the cursor.
		const TArray<int32> Hints = GlobalHintList();
		FElysiumEntityHandle& Cursor = MingXiaoTentacleCache();                    // `DAT_1093bd34`
		const FElysiumEntity* CursorHint = World != nullptr ? World->Resolve(Cursor) : nullptr;
		int32 Start = 0;
		if (CursorHint != nullptr)                                                 // 0x1039da29 / 0x1039da2b
		// same arm: 0x1039da46 JNZ, 0x1039da50 JNZ, 0x1039da5a JZ
		{
			const int32 At = Hints.IndexOfByKey(CursorHint->Handle.Index);
			Start = At != INDEX_NONE ? At : Hints.Num();
		}
		for (int32 i = Start; i < Hints.Num(); ++i)                                // 0x1039da61..0x1039da71
		{
			FHintWords Words;
			if (!HintWords(Hints[i], Words) || Words.HintType != 19000)            // 0x1039da61 `+0x5dc == 19000`
			// same arm: 0x1039da67 JZ
			{
				continue;
			}
			if (TentacleHintClear(Words.OriginCm))                                 // 0x1039da87 / 0x1039daa8
			// same arm: 0x1039daaf JZ
			{
				// `CAI_Navigator::SetGoal(goal, 0)` (`0x1039db3b` -> `0x102ecd20`), the goal built at
				// `0x1039dab5..0x1039db34`: type 4, the hint's origin, activity 0x13, tolerance -1.0
				// (`_DAT_104bde70`), flags 0. The goal's target word is `DAT_1093bd30` (unrecovered;
				// no handle). Lane Script19's `Script19SetGoal` is that body (L05 integration: was a
				// recording seam that always refused).
				FScript19NavGoal ShootGoal;
				ShootGoal.Type = 4;                                                // 0x1039db0d
				ShootGoal.PositionCm = Words.OriginCm;                             // 0x1039dac1..0x1039dadf
				ShootGoal.Activity = 0x13;                                         // 0x1039db19
				ShootGoal.Tolerance = NavGoalToleranceKeep;                        // 0x1039dae3 / 0x1039db08
				ShootGoal.Flags = 0;
				if (Script19SetGoal(ShootGoal, 0, TEXT("tentacle shoot hint (0x1039d750)"))) // 0x1039db3b
				// same arm: 0x1039db42 JZ
				{
					TaskComplete(false);                                           // 0x1039db48
				}
			}
			const FElysiumEntity* Next = World != nullptr && Hints.IsValidIndex(i + 1)
				? World->Entities()[Hints[i + 1]].Get() : nullptr;
			Cursor = Next != nullptr ? Next->Handle : FElysiumEntityHandle::Invalid(); // 0x1039db53 / 0x1039db5f
			// same arm: 0x1039db55 JZ
			return 0;
		}
		Cursor = FElysiumEntityHandle::Invalid();                                  // 0x1039da73
		return 0;
	}
	case 0x154:
		AutoMovement();                                                            // 0x1039db76
		if (IsActivityFinished())                                                  // 0x1039db7f
		// same arm: 0x1039db87 JZ
		{
			RestartIdealActivityId(1);                                             // 0x1039db8d
			TaskComplete(false);                                                   // 0x1039db96
		}
		return 0;
	default:
		break;
	}
	return FElysiumNpc::RunTaskSlot444(Arg0);                                      // 0x1039dba8
}

// --- `CNPC_VSabbatLeader::RunTask` `0x103a8990`, 1,794 bytes -------------------------------------------
int32 FElysiumNpcSabbatLeader::RunTaskSlot444(void* Arg0)
{
	const FElysiumScheduleStep* Step = RunTask19Species::StepOf(Arg0);
	if (Step == nullptr)
	{
		return 0;
	}
	const double Now = RunTask19Species::NowOf(*this);
	// The prologue: `andrei_force_awaken` set, not yet activated and not state 5 -> with any player
	// within 500 units in 2-D (`0x10266c80`, the player's slot 192 point; the port reads its origin),
	// start the transformation.
	if (ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::AndreiForceAwaken) != 0 // 0x103a89ef..0x103a8a01
	// same arm: 0x103a89f6 JNZ
		&& !bSabbatLeaderActivated                                                 // 0x103a8a03
		// same arm: 0x103a8a09 JNZ
		&& NpcStateRetail() != 5)                                                  // 0x103a8a0b
		// same arm: 0x103a8a12 JZ, 0x103a8a1f CALL
	{
		const FElysiumPlayer* Player = World != nullptr ? World->FindPlayer() : nullptr;
		if (Player != nullptr
			&& NpcKernelMotor2Shared::Length2D((Player->Origin - Origin) / ElysiumMove::U) <= 500.0f) // 0x103a8a28 `DAT_104c3cd0`
			// same arm: 0x103a8a2f JZ
		{
			SabbatLeaderStartTransformation();                                        // 0x103a8a48
			// same arm: 0x103a8a59 JG, 0x103a8a5f JZ, 0x103a8a6a JG, 0x103a8a70 JZ, 0x103a8a7a JA
			//   0x103a8a84 JMP
		}
	}
	const int32 Id = RunTask19Species::LocalTaskOf(*this, Step);
	auto FaceEnemyOrigin = [this](float Speed)
	{
		if (FElysiumEntity* Enemy = RunTask19Species::Enemy167(*this))
		{
			MotorSetIdealYawToTargetAndUpdate(Enemy->Origin, Speed);
		}
	};
	switch (Id)
	{
	case 0x15c:
	{
		const float Yaw = NpcKernelFacingShared::RetailVecToYaw(Velocity);          // 0x103a8d0c slot 198 / 0x103a8d18
		MotorSetIdealYawAndUpdate(Yaw, RunTask19Species::YawSpeedDefault);         // 0x103a8d22
		TroikaMotor.MoveInterval = 0.f;                                            // motor +0x30
		Flags &= ~1;                                                               // 0x103a8d34 `RemoveFlag(FL_ONGROUND)`
		if (SabbatLeaderSplashCycle() <= SequenceCycle && !bSabbatLeaderLargeSplash) // 0x103a8d3f / 0x103a8d52
		// same arm: 0x103a8d4c JNZ, 0x103a8d58 JNZ
		{
			bSabbatLeaderLargeSplash = true;                                       // 0x103a8d6c
			const FElysiumEntity* Hint = World != nullptr && World->Entities().IsValidIndex(BaseScheduleHost.HintNode)
				? World->Entities()[BaseScheduleHost.HintNode].Get() : nullptr;
			SpawnBloodPoolEmitter(TEXT("bloodsplash_emitter"), Hint);              // 0x103a8d73
			SpawnBloodPoolEmitter(TEXT("andrei_splash_huge-emitter"), Hint);       // 0x103a8d86
			// same arm: 0x103a8d94 CALL, 0x103a8da0 CALL, 0x103a8db2 CALL, 0x103a8dcd CALL, 0x103a8dd8 CALL
			RunTask19Species::EmitWav(*this, 2,
				TEXT("Character/Monster/Andrei_Transformed/dive_in_splash.wav"));  // 0x103a8e0d / 0x103a8e1f
				// same arm: 0x103a8e26 CALL, 0x103a8e2f CALL, 0x103a8e40 CALL
		}
		if (!IsActivityFinished())                                                 // 0x103a8e49
		// same arm: 0x103a8e51 JZ
		{
			return 0;
		}
		Gravity = ElysiumNpcTunables::One;                                         // 0x103a8e5a `m_flGravity`
		NavSetType(0);                                                             // 0x103a8e64
		bJumping = false;                                                          // 0x103a8e6d
		Hide();                                                                    // 0x103a8e73 slot 66
		TaskComplete(false);                                                       // 0x103a8e7c
		// same arm: 0x103a8e9d JA, 0x103a8ea3 JMP
		return 0;
	}
	case 0x139:
		if (IsActivityFinished())                                                  // 0x103a8ab6
		// same arm: 0x103a8abe JZ, 0x103a8ac9 CALL, 0x103a8ad5 CALL, 0x103a8ae7 CALL, 0x103a8b02 CALL
		//   0x103a8b0d CALL
		{
			RunTask19Species::EmitWav(*this, 2,
				TEXT("Character/Monster/Andrei_Transformed/jump_retreat.wav"));   // 0x103a8b42 / 0x103a8b54
				// same arm: 0x103a8b5b CALL, 0x103a8b64 CALL, 0x103a8b73 JZ, 0x103a8b7c JNZ
		}
		return FElysiumNpcVampireBoss::RunTaskSlot444(Arg0);                       // 0x103a8a99
	case 0x13a:
		if (NavGetType() == 1 && IsOnGroundFlag())                                 // 0x103a8ca9 / 0x103a8cb5
		// same arm: 0x103a8cb1 JNZ, 0x103a8cbc JZ
		{
			LastAttackTime = Now;                                                  // 0x103a8cc9
			RecordHealthPercent();                                                 // 0x103a8ccf `0x103c6a00`
			RecordPlayerHealth();                                                  // 0x103a8cd6
		}
		if (bSabbatLeaderTrackPlayer && !PlayerIsFacingMe())                       // 0x103a8cdb / 0x103a8ce9
		// same arm: 0x103a8ce1 JZ, 0x103a8cf0 JNZ
		{
			SetJumpVelocityTowardPlayer();                                         // 0x103a8cf8
		}
		return FElysiumNpcVampireBoss::RunTaskSlot444(Arg0);
	case 0x13c:
		if (bSabbatLeaderTrackPlayer)                                              // 0x103a8b82
		// same arm: 0x103a8b88 JZ
		{
			AutoMovement();                                                        // 0x103a8b90
			const FElysiumEntity* Player =
				World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr; // 0x103a8b9b..0x103a8bc9
				// same arm: 0x103a8b9e JZ, 0x103a8bbf JNZ, 0x103a8bdf CALL
			if (Player != nullptr)
			{
				MotorSetIdealYawToTargetAndUpdate(Player->Origin, 50.0f);          // 0x103a8be8 `DAT_104c3cec`
				FVector Dir = Player->Origin - Origin;                             // 0x103a8bf1..0x103a8c37
				// same arm: 0x103a8bfd CALL
				const float Length = static_cast<float>((Dir / ElysiumMove::U).Size()); // 0x103a8c3b `VectorNormalize`
				if (Length > 9.999999747378752e-05f)                               // 0x103a8c41 / 0x103a8c4e `_DAT_104c3ce4`
				{
					Dir.Normalize();
					Velocity = Dir * (100.0f * ElysiumMove::U);                    // 0x103a8c54..0x103a8c9d `_DAT_104c3ce8`
				}
			}
		}
		return FElysiumNpcVampireBoss::RunTaskSlot444(Arg0);
	case 0xbc:
	case 0xbd:
		if (IsInDialog())                                                          // 0x103a8a8d / 0x103a8a94
		{
			return 0;
		}
		return FElysiumNpcVampireBoss::RunTaskSlot444(Arg0);
	case 0x15d:
		AutoMovement();                                                            // 0x103a8f2e
		MotorUpdateYaw(RunTask19Species::UpdateYawDefault);                        // 0x103a8f3b
		if (!IsActivityFinished())                                                 // 0x103a8f44
		// same arm: 0x103a8f4c JZ
		{
			return 0;
		}
		LastAttackTime = Now;                                                      // 0x103a8f5a
		RecordHealthPercent();                                                     // 0x103a8f62
		RecordPlayerHealth();                                                      // 0x103a8f69
		Unhide();                                                                  // 0x103a8f72 slot 67
		EffectsWord &= ~0x20u;                                                     // 0x103a8f7e
		// same arm: 0x103a8f8f JNZ
		SolidFlagsWord &= 0xfffbu;                                                 // 0x103a8fe8 / 0x103a8feb `RemoveSolidFlags(4)`
		TaskComplete(false);                                                       // 0x103a8ffd
		return 0;
	case 0x15e:
		if (Now > SabbatLeaderWarningFinishTime)                                   // 0x103a901f -> 0x103a906b AND 0x4100 / 0x103a9070 JNZ (only ordered >)
		{
			TaskComplete(false);                                                   // 0x103a9079
		}
		return 0;
	case 0x36:
	case 0x37:
	case 0x9a:
	case 0x160:
		AutoMovement();                                                            // 0x103a8eac
		// same arm: 0x103a8eb5 CALL, 0x103a8ebd JZ, 0x103a8ec3 CALL, 0x103a8ecd CALL
		FaceEnemyOrigin(10.0f);                                                    // 0x103a8ef9 `DAT_104c3cd8`
		if (IsActivityFinished())                                                  // 0x103a8f02
		// same arm: 0x103a8f0a JZ
		{
			TaskComplete(false);                                                   // 0x103a8f13
		}
		return 0;
	case 0x161:
	case 0x163:
		if (IsActivityFinished())                                                  // 0x103a902b
		// same arm: 0x103a9033 JZ
		{
			TaskComplete(false);                                                   // 0x103a903c
		}
		return 0;
	case 0x162:
		if (static_cast<float>(Now - SabbatLeaderTaskStartTime) > 1.0f)           // 0x103a9063 / 0x103a906b AND 0x4100 / 0x103a9070 `_DAT_104c3d08` (only ordered >)
		{
			TaskComplete(false);                                                   // 0x103a9079
		}
		return 0;
	default:
		break;
	}
	return FElysiumNpcVampireBoss::RunTaskSlot444(Arg0);                           // 0x103a8a99
}

// --- `CNPC_VSheriffMan::RunTask` `0x103af780`, 108 bytes -----------------------------------------------
int32 FElysiumNpcSheriffMan::RunTaskSlot444(void* Arg0)
{
	const FElysiumScheduleStep* Step = RunTask19Species::StepOf(Arg0);
	if (Step != nullptr && RunTask19Species::LocalTaskOf(*this, Step) == 0x154)                                  // 0x103af7d4 / 0x103af7da
	{
		// Swallowed: no movement, no completion.
		return 0;
	}
	return FElysiumNpcVampireBoss::RunTaskSlot444(Arg0);                           // 0x103af7dd
}

// --- `CNPC_VTaxiDriver::RunTask` `0x103b38a0`, 80 bytes -------------------------------------------------
int32 FElysiumNpcTaxiDriver::RunTaskSlot444(void* Arg0)
{
	const FElysiumScheduleStep* Step = RunTask19Species::StepOf(Arg0);
	if (Step == nullptr || RunTask19Species::LocalTaskOf(*this, Step) != 0xb9)                                   // 0x103b38a9 / 0x103b38b1
	{
		return FElysiumNpcHuman::RunTaskSlot444(Arg0);                             // 0x103b38b4
	}
	if (RunDialogActivity() == INDEX_NONE)                                         // 0x103b38bd / 0x103b38c5 `0x102c1400`
	{
		TaskComplete(false);                                                       // 0x103b38cb
		Cognition.Conditions.Clear(EElysiumNpcCond::HearPlayer);                   // 0x103b38d4 `ClearCondition(0x6f)`
		bTaxiFirstThink = false;                                                   // 0x103b38df +0x6660
		SetActivity(1);                                                            // 0x103b38e6 slot 310
	}
	return 0;
}

// --- `CNPC_VTzimisce::RunTask` `0x103bb1e0`, 1,069 bytes ----------------------------------------------
int32 FElysiumNpcTzimisce::RunTaskSlot444(void* Arg0)
{
	const FElysiumScheduleStep* Step = RunTask19Species::StepOf(Arg0);
	if (Step == nullptr)
	{
		return 0;
	}
	const double Now = RunTask19Species::NowOf(*this);
	switch (RunTask19Species::LocalTaskOf(*this, Step))                                                          // 0x103bb204
	// same arm: 0x103bb1f6 JA, 0x103bb20f CALL, 0x103bb21f CALL, 0x103bb227 CALL
	{
	case 0x89:
	case 0x8a:
		AutoMovement();                                                            // 0x103bb265
		// same arm: 0x103bb26e CALL, 0x103bb27e CALL, 0x103bb286 CALL
		RunTask19Species::FaceEnemyLkp(*this, RunTask19Species::YawSpeedHold);    // 0x103bb29b
		if (IsActivityFinished()                                                   // 0x103bb2a4 / 0x103bb2ac
			|| static_cast<float>(Now - LastAttackTime) > Step->Data)              // 0x103bb2c0
		{
			TaskComplete(false);                                                   // 0x103bb53e
		}
		return 0;
	case 0x8b:
	case 0x8e:
	case 0xcb:
		AutoMovement();                                                            // 0x103bb2d1
		[[fallthrough]];
	case 0xc1:
		if (IsActivityFinished())                                                  // 0x103bb2da
		// same arm: 0x103bb2e2 JZ
		{
			TaskComplete(false);                                                   // 0x103bb2ec
		}
		return 0;
	case 0xa7:
		RunTask19Species::FaceEnemyLkp(*this, RunTask19Species::YawSpeedDefault); // 0x103bb23c
		RunTask19Species::TurnUnlessMemory(*this);                                 // 0x103bb247 / 0x103bb254
		// same arm: 0x103bb24a JNZ
		return 0;
	case 0xbf:
	case 0xc0:
		FUN_103bf560();                                                            // 0x103bb2fc
		if (IsActivityFinished())                                                  // 0x103bb305
		// same arm: 0x103bb30d JZ
		{
			if (Motor != nullptr)
			{
				Motor->ResetSteering();                                            // 0x103bb31e `0x102e0a60(motor, 180.0)`
			}
			TaskComplete(false);                                                   // 0x103bb327
		}
		return 0;
	case 0xc8:
		RunTask19Species::TurnUnlessMemory(*this);                                 // 0x103bb33b / 0x103bb344
		// same arm: 0x103bb33e JNZ
		MotorUpdateYaw(RunTask19Species::UpdateYawDefault);                        // 0x103bb352
		if (FacingIdeal())                                                         // 0x103bb359
		// same arm: 0x103bb360 JZ
		{
			TaskComplete(false);                                                   // 0x103bb36a
		}
		return 0;
	case 0xc9:
	{
		FElysiumEntity* Enemy = RunTask19Species::Enemy167(*this);                 // 0x103bb37c
		// same arm: 0x103bb38e CALL
		const FVector Lkp = RunTask19Species::EnemyLkp(*this);                     // 0x103bb396
		RunTask19Species::TurnUnlessMemory(*this);                                 // 0x103bb3a1 / 0x103bb3aa
		// same arm: 0x103bb3a4 JNZ
		MotorSetIdealYawToTargetAndUpdate(Lkp, RunTask19Species::YawSpeedDefault); // 0x103bb3c0
		if (!FUN_103be8e0(Enemy))                                                  // 0x103bb3c8 / 0x103bb3cf
		{
			return 0;
		}
		if (Cognition.Conditions.HasOrdinal(0x1b)                                  // 0x103bb3d9 / 0x103bb3e0
			|| Now >= TzimisceTaskFailTimer)   /* NaN runs: 0x103bb3f7 AND 0x100 */                                     // 0x103bb3ef
		{
			TaskComplete(false);                                                   // 0x103bb53e
		}
		return 0;
	}
	case 0xca:
	{
		FElysiumEntity* Enemy = GetEnemy();                                        // 0x103bb405 slot 168
		// same arm: 0x103bb417 CALL
		const FVector Lkp = RunTask19Species::EnemyLkp(*this);                     // 0x103bb41f
		RunTask19Species::TurnUnlessMemory(*this);                                 // 0x103bb42a / 0x103bb433
		// same arm: 0x103bb42d JNZ
		MotorSetIdealYawToTargetAndUpdate(Lkp, RunTask19Species::YawSpeedDefault); // 0x103bb449
		if (FUN_103be8e0(Enemy))                                                   // 0x103bb451
		// same arm: 0x103bb458 JZ
		{
			TaskComplete(false);                                                   // 0x103bb462
		}
		return 0;
	}
	case 0xcc:
		AutoMovement();                                                            // 0x103bb472
		// same arm: 0x103bb47b CALL, 0x103bb48b CALL, 0x103bb493 CALL
		RunTask19Species::FaceEnemyLkp(*this, RunTask19Species::YawSpeedHold);    // 0x103bb4a8
		if (IsActivityFinished()                                                   // 0x103bb4b1
		// same arm: 0x103bb4b9 JZ
			&& ActivityNumber == RunTask19Species::TranslatedActivity(*this, 0x103)) // 0x103bb4c8..0x103bb534
		{
			TaskComplete(false);                                                   // 0x103bb53e
		}
		return 0;
	case 0xcd:
	case 0xce:
		AutoMovement();                                                            // 0x103bb4d2 / 0x103bb4f2
		if (IsActivityFinished()                                                   // 0x103bb4db / 0x103bb4fb
		// same arm: 0x103bb4e3 JZ, 0x103bb503 JZ
			&& ActivityNumber == RunTask19Species::TranslatedActivity(             // 0x103bb512..0x103bb534
			// same arm: 0x103bb51d CALL, 0x103bb528 CALL
				*this, RunTask19Species::LocalTaskOf(*this, Step) == 0xcd ? 0x104 : 0x105))
		{
			TaskComplete(false);                                                   // 0x103bb53e
		}
		return 0;
	case 0xd2:
	{
		FElysiumEntity* Enemy = GetEnemy();                                        // 0x103bb550 slot 168
		if (!IsTzimisceHintUsable(BaseScheduleHost.HintNode, Enemy))               // 0x103bb560 / 0x103bb567
		{
			RunTask19Species::FailAt(*this, RunTask19Species::GFileTzimisce, 0x911, 0x1a); // 0x103bb583
		}
		AutoMovement();                                                            // 0x103bb58b
		const int32 Wanted = static_cast<int32>(Step->Data);                       // 0x103bb595 `__ftol`
		const int32 Translated = RunTask19Species::TranslatedActivity(*this, Wanted); // 0x103bb59d..0x103bb5b3
		// same arm: 0x103bb5a8 CALL
		if (IsActivityFinished() && ActivityNumber == Translated)                  // 0x103bb5bf / 0x103bb5c9
		// same arm: 0x103bb5c7 JZ, 0x103bb5cf JNZ
		{
			TaskComplete(false);                                                   // 0x103bb5d5
			return 0;
		}
		SetIdealActivity(static_cast<int32>(Step->Data));                          // 0x103bb5e6 / 0x103bb5ee
		return 0;
	}
	default:
		break;
	}
	return FElysiumNpc::RunTaskSlot444(Arg0);                                      // 0x103bb5ff
}

// --- `CNPC_VTzimisceRunner::RunTask` `0x103c3870`, 192 bytes -------------------------------------------
int32 FElysiumNpcTzimisceRunner::RunTaskSlot444(void* Arg0)
{
	const FElysiumScheduleStep* Step = RunTask19Species::StepOf(Arg0);
	if (Step == nullptr || RunTask19Species::LocalTaskOf(*this, Step) < 0x122 || RunTask19Species::LocalTaskOf(*this, Step) > 0x124)           // 0x103c3881 / 0x103c388c
	{
		return FElysiumNpc::RunTaskSlot444(Arg0);                                  // 0x103c3924
	}
	AutoMovement();                                                                // 0x103c3894
	if (FElysiumEntity* Enemy = RunTask19Species::Enemy167(*this))                 // 0x103c389d / 0x103c38a5
	{
		MotorMoveStop();                                                           // 0x103c38ad `0x102e0b40`
		// same arm: 0x103c38b6 CALL
		MotorSetIdealYawToTargetAndUpdate(Enemy->Origin, RunTask19Species::YawSpeedDefault); // 0x103c38c0 / 0x103c38ea
	}
	const double Now = RunTask19Species::NowOf(*this);
	if (Now >= BaseScheduleHost.WaitFinished)                                     // 0x103c38f8 / 0x103c3900 AND 0x100 / 0x103c3905 (NaN runs)
	{
		ScheduleHost.DesiredMoveYaw = 0.f;                                         // 0x103c390b
		TaskComplete(false);                                                       // 0x103c3915
	}
	return 0;
}

// --- `CNPC_VVampireBoss::RunTask` `0x103c5f40`, 123 bytes -----------------------------------------------
int32 FElysiumNpcVampireBoss::RunTaskSlot444(void* Arg0)
{
	const FElysiumScheduleStep* Step = RunTask19Species::StepOf(Arg0);
	if (Step != nullptr && RunTask19Species::LocalTaskOf(*this, Step) == 0x14d)                                  // 0x103c5f94 / 0x103c5f9a
	{
		WaitForTransformation();                                                   // 0x103c5fac `0x103c63c0`
		return 0;
	}
	return FElysiumNpcHuman::RunTaskSlot444(Arg0);                                 // 0x103c5f9d
}

// --- `CNPC_VWerewolf::RunTask` `0x103cdfb0`, 1,456 bytes -----------------------------------------------
int32 FElysiumNpcWerewolf::RunTaskSlot444(void* Arg0)
{
	const FElysiumScheduleStep* Step = RunTask19Species::StepOf(Arg0);
	if (Step == nullptr)
	{
		return 0;
	}
	const double Now = RunTask19Species::NowOf(*this);
	(void)TaskName(RunTask19Species::LocalTaskOf(*this, Step));                                                  // 0x103ce029 slot 449, answer unused
	// same arm: 0x103cdfb9 JZ, 0x103cdfc3 JNZ, 0x103ce039 JA
	// `PUSH 0x10661dac` / slot 448: the SDK's text fail code (`MakeFailCode(const char*)`), the
	// string's address standing as the reason. The port's text-fail accessor is family Misc19's
	// `TaskFailText` (L05 integration: was the raw address as an int code).
	const TCHAR* const FailNotOutOfSight = TEXT("Did not path out of player's sight");
	switch (RunTask19Species::LocalTaskOf(*this, Step))                                                          // 0x103ce03f
	{
	case 0x14a:
	case 0x157:
		TaskComplete(false);
		return 0;
	case 0x14b:
		if (!IsActivityFinished())                                                 // 0x103ce04a / 0x103ce052
		{
			MotorUpdateYaw(RunTask19Species::UpdateYawDefault);                    // 0x103ce093
			return 0;
		}
		if (CheckAllMoveHints())                                                   // 0x103ce056 `0x103cfc50` (L11's body)
		// same arm: 0x103ce05f JZ
		{
			TaskComplete(false);                                                   // 0x103ce062
			return 0;
		}
		TaskFail(4);                                                               // 0x103ce078
		return 0;
	case 0x14c:
	{
		if (WerewolfShouldPursueEnemy() && FindMoveHint())                // 0x103ce0a7 / 0x103ce0b2
		// same arm: 0x103ce0ae JZ, 0x103ce0b9 JZ
		{
			TaskComplete(false);                                                   // 0x103ce0be
			return 0;
		}
		if (!WerewolfShouldPursueEnemy() && FindRandomMoveHint())         // 0x103ce0d2 / 0x103ce0dd
		// same arm: 0x103ce0d9 JNZ, 0x103ce0e4 JZ
		{
			TaskComplete(false);                                                   // 0x103ce0e9
			return 0;
		}
		if (Cognition.Conditions.HasOrdinal(0x77))                                 // 0x103ce0ff
		// same arm: 0x103ce108 JZ
		{
			TaskComplete(false);                                                   // 0x103ce10b
			return 0;
		}
		if (Cognition.Conditions.HasOrdinal(0x78))                                 // 0x103ce11f
		// same arm: 0x103ce126 JZ
		{
			TaskComplete(false);                                                   // 0x103ce12b
			return 0;
		}
		const float Limit = Step->Data;
		if ((Limit != ElysiumNpcTunables::Zero                                     // 0x103ce140 / 0x103ce14b
				&& Limit < static_cast<float>(Now - Schedule.TaskStartedAt))       // 0x103ce15c / 0x103ce166
			|| !NavIsGoalActive())                                                 // 0x103ce172 `0x102ee620`
			// same arm: 0x103ce179 JZ
		{
			BaseScheduleHost.bShouldMove = false;                                  // 0x103ce244
			if (WerewolfForceTeleportConVar() != 0)                                         // 0x103ce252..0x103ce261
			// same arm: 0x103ce257 JNZ
			{
				TaskComplete(false);                                               // 0x103ce266
				return 0;
			}
			if (FindEgressHint())                                         // 0x103ce27a
			// same arm: 0x103ce283 JZ
			{
				TaskComplete(false);                                               // 0x103ce286
				return 0;
			}
			TaskFailText(FailNotOutOfSight);                                             // 0x103ce29f
			NavClearGoal();                                                        // 0x103ce2ab
			return 0;
		}
		if (!NavigatorGoalIsActive())                                              // 0x103ce185 `0x102ee6a0`
		// same arm: 0x103ce18c JNZ
		{
			BaseScheduleHost.bShouldMove = false;                                  // 0x103ce190
			SetIdealActivity(ResolveLinkActivity());                               // 0x103ce196 / 0x103ce19e
			return 0;
		}
		if (NavArrivedWithinTolerance())                                           // 0x103ce1b6 `0x102f2ea0`
		// same arm: 0x103ce1bd JZ
		{
			BaseScheduleHost.bShouldMove = false;                                  // 0x103ce1bf
			if (WerewolfForceTeleportConVar() != 0)                                         // 0x103ce1cd..0x103ce1dc
			// same arm: 0x103ce1d2 JNZ
			{
				TaskComplete(false);                                               // 0x103ce1e1
				return 0;
			}
			if (FindEgressHint())                                         // 0x103ce1f5
			// same arm: 0x103ce1fe JZ
			{
				TaskComplete(false);                                               // 0x103ce201
				return 0;
			}
			TaskFailText(FailNotOutOfSight);                                             // 0x103ce21a
			return 0;
		}
		ValidateNavGoal();                                                         // 0x103ce231 slot 528
		return 0;
	}
	case 0x14d:
		if (FindTeleportHint())                                           // 0x103ce2bf
		// same arm: 0x103ce2c6 JZ
		{
			TaskComplete(false);                                                   // 0x103ce2cf
		}
		return 0;
	case 0x150:
	case 0x151:
	case 0x156:
		if (IsActivityFinished())                                                  // 0x103ce2e5 / 0x103ce382
		// same arm: 0x103ce38c JZ
		{
			SnapToAnimationPoint();                                                // 0x103ce38e
			TaskComplete(false);                                                   // 0x103ce396
			return 0;
		}
		UpdateFakeHull(Now);                                                       // 0x103ce3a8
		AutoMovement();                                                            // 0x103ce3af
		return 0;
	case 0x154:
		MotorUpdateYaw(RunTask19Species::UpdateYawDefault);                        // 0x103ce2f8
		if (FMath::Abs(MotorDeltaIdealYaw()) < ElysiumNpcTunables::Five)           // 0x103ce303 / 0x103ce30a / 0x103ce315
		{
			TaskComplete(false);                                                   // 0x103ce31e
		}
		return 0;
	case 0x155:
		if (!IsActivityFinished())                                                 // 0x103ce334 / 0x103ce33e
		{
			UpdateFakeHull(Now);                                                   // 0x103ce3a8
			AutoMovement();                                                        // 0x103ce3af
			return 0;
		}
		SnapToAnimationPoint();                                                    // 0x103ce340
		TaskComplete(false);                                                       // 0x103ce348
		if (ActivityNumber == 0x10b)                                               // 0x103ce34d / 0x103ce357
		{
			TeleportOut();                                                         // 0x103ce35f `0x103d4a60`
			SetSchedule(0x158, false);                                             // 0x103ce36c `0x102ae750`
		}
		return 0;
	case 0x159:
		if (IsActivityFinished())                                                  // 0x103ce3c5 / 0x103ce3cd
		{
			TaskComplete(false);                                                   // 0x103ce3d6
			WerewolfBreakHintNode = INDEX_NONE;                                    // 0x103ce3e1 `m_pBreakHint = 0`
			WerewolfHintFlags &= ~2u;                                              // 0x103ce3e7 / 0x103ce3ea
		}
		return 0;
	case 0x15c:
		if (IsActivityFinished())                                                  // 0x103ce400 / 0x103ce40a
		{
			TaskComplete(false);                                                   // 0x103ce411
			return 0;
		}
		UpdateFakeHull(Now);                                                       // 0x103ce4c5
		return 0;
	case 0x15d:
		if (IsActivityFinished())                                                  // 0x103ce427 / 0x103ce431
		{
			TaskComplete(false);                                                   // 0x103ce46e
			return 0;
		}
		UpdateFakeHull(Now);                                                       // 0x103ce433
		MeleeSwingUpdate();                                                        // 0x103ce43c slot 315
		return 0;
	case 0x15e:
		if (IsActivityFinished())                                                  // 0x103ce453 / 0x103ce45d
		{
			SnapToAnimationPoint();                                                // 0x103ce45f
			ClearMoveHint();                                                       // 0x103ce466
			TaskComplete(false);                                                   // 0x103ce46e
			return 0;
		}
		UpdateFakeHull(Now);                                                       // 0x103ce433
		MeleeSwingUpdate();                                                        // 0x103ce43c
		return 0;
	case 0x15f:
		if (IsActivityFinished())                                                  // 0x103ce484 / 0x103ce48e
		{
			AnimEventLifeStateWord = 2;                                                   // 0x103ce491 `m_lifeState = LIFE_DEAD`
			TaskComplete(false);                                                   // 0x103ce49b
			FElysiumEntity* Enemy = RunTask19Species::Enemy167(*this);             // 0x103ce4a6
			FireOutput(FName(TEXT("OnFinishCrushAnimation")),                      // 0x103ce4b3 `0x100cd660`
				Enemy != nullptr ? Enemy->Handle : FElysiumEntityHandle::Invalid());
			return 0;
		}
		UpdateFakeHull(Now);                                                       // 0x103ce4c5
		return 0;
	case 0x160:
		if (IsActivityFinished())                                                  // 0x103ce4db / 0x103ce4e3
		{
			RestartIdealActivityId(0x120);                                         // 0x103ce4ec
		}
		return 0;
	case 0x161:
		if (!IsActivityFinished())                                                 // 0x103ce502 / 0x103ce50c
		{
			UpdateFakeHull(Now);                                                   // 0x103ce52f
			MeleeSwingUpdate();                                                    // 0x103ce538
			return 0;
		}
		SnapToAnimationPoint();                                                    // 0x103ce50e
		ClearMoveHint();                                                           // 0x103ce515
		TaskComplete(false);                                                       // 0x103ce51d
		return 0;
	default:
		break;
	}
	return FElysiumNpc::RunTaskSlot444(Arg0);                                      // 0x103ce54e
}

// --- `CNPC_VZombie::RunTask` `0x103e01d0`, 347 bytes ----------------------------------------------------
int32 FElysiumNpcZombie::RunTaskSlot444(void* Arg0)
{
	const FElysiumScheduleStep* Step = RunTask19Species::StepOf(Arg0);
	if (Step == nullptr)
	{
		return 0;
	}
	const double Now = RunTask19Species::NowOf(*this);
	switch (RunTask19Species::LocalTaskOf(*this, Step))                                                          // 0x103e01e7
	// same arm: 0x103e01e1 JA
	{
	case 0x14c:
	case 0x14f:
	case 0x150:
		if (IsActivityFinished())                                                  // 0x103e0232 / 0x103e029a
		// same arm: 0x103e023a JZ
		{
			TaskComplete(false);                                                   // 0x103e0244
		}
		return 0;
	case 0x14e:
		if (IsActivityFinished())                                                  // 0x103e01f2
		// same arm: 0x103e01fa JZ
		{
			TaskComplete(false);                                                   // 0x103e0204
			MiscFlags |= 0x80000u;                                                 // 0x103e0210 `AddMiscFlag(0x80000)`
			// `CBaseCombatCharacter::CreateCorpse(&m_vecDeathForceVector, this + 0x668c)`, called
			// non-virtually (`0x10005c90`), so it is the combat character's body and not this class's.
			FElysiumCombatCharacter::CreateCorpse(ZombieDeathForceVector, &ZombieDeathDamageInfo); // 0x103e0225
		}
		return 0;
	case 0x151:
		if (BaseScheduleHost.WaitFinished < Now || !NavIsGoalActive())             // 0x103e0255 / 0x103e026a
		// same arm: 0x103e0262 JZ, 0x103e0271 JNZ
		{
			BaseScheduleHost.bShouldMove = false;                                  // 0x103e027b
			TaskComplete(false);                                                   // 0x103e0282
			NavClearGoal();                                                        // 0x103e028d
		}
		return 0;
	case 0x153:
		if (IsActivityFinished())                                                  // 0x103e02a6
		// same arm: 0x103e02ae JZ
		{
			TaskComplete(false);                                                   // 0x103e02b4
			return 0;
		}
		if (ZombieAiType == 7)                                                     // 0x103e02bd
		// same arm: 0x103e02c4 JNZ
		{
			const FElysiumEntity* Player =
				World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr; // 0x103e02cc..0x103e02f2
				// same arm: 0x103e02cf JZ, 0x103e02ec JNZ
			if (Player != nullptr)
			{
				// `0x102e2020(motor, player->GetAbsOrigin())`: `0x102e2750` (slot 515 `CalcIdealYaw`), then
				// the flip and the `+0x34` store -- family Hints' `SetMotorHintYaw` is that tail.
				SetMotorHintYaw(CalcIdealYaw(Player->Origin));                         // 0x103e02ff / 0x103e0308
				MotorUpdateYaw(RunTask19Species::UpdateYawDefault);                // 0x103e0315
			}
		}
		return 0;
	default:
		break;
	}
	return FElysiumNpcAnimal::RunTaskSlot444(Arg0);                                // 0x103e0322
}
