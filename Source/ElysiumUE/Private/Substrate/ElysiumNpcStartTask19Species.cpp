// Story 0019/8 (29e under the strict verdict), family **StartTask19** -- the species classes'
// bodies, lane L04.
//
// Every body is the retail `StartTask` (slot 442) at the address its definition comment names, read
// off the listing (`vtmb_asm`) and the reading packet
// `$ELYSIUM_WORK_ROOT/research/npc-kernel-checklist/families-19-29/StartTask19-READING.md`; the walked
// prose is `docs/vtmb/npc-ai/story8/StartTask19-Species.md`. Each arm carries the address of the
// instruction it came from.
//
// **The task number.** Retail's schedule parser stores the CLASS-LOCAL task id in `Task_t::iTask`
// (`0x1030d850`), and every species switch below compares that local number. This runtime stores the
// GLOBAL id (`FElysiumScheduleStep::TaskId`, `ElysiumScheduleText.cpp` `ResolveTaskName`), so each
// body first translates it back through its own class's task space -- slot 450 `GetLocalTaskId`
// (`0x101a6640`), the same translation retail's own global/local pair performs -- and then switches on
// retail's numbers. A synthetic task a body builds on the stack (the head claw's `{0x4b, 1.0}`) is
// translated the other way before it is handed to the parent, so the parent sees what the runner
// would have handed it.
//
// **Parent chains.** Retail reaches its parent through a DIRECT thunk (`0x10010695` ->
// `CAI_BaseNPCTroika::StartTask 0x102a1910`, `0x10013336`/`0x1000a0c9` -> `CNPC_VHuman::StartTask
// 0x103847f0`, `0x10009b06` -> `CNPC_VAnimal::StartTask 0x1035f650`, ...), so each chain is the
// explicit port base call, never the virtual.
//
// **The trace stamps.** `+0x1b44`/`+0x1b48` (the file/line pair a `TaskFail` arm writes) and the
// scope-trace frames (`g_ScopeTraceStack`) are absent words; the landed convention
// (`ElysiumNpcFrenzyShadow.cpp` slot 442) is one `RecordScheduleEvent` row with the retail line.
//
// Owns (StartTask19's `rule` rows, lane L04): 0x1035f650 CNPC_VAnimal::StartTask, 0x103847f0
// CNPC_VHuman::StartTask, 0x10392d80 CNPC_VMingXiao::StartTask, 0x1039c4c0
// CNPC_VMingXiaoTentacle::StartTask, 0x103ba7c0 CNPC_VTzimisce::StartTask, 0x103c1820
// CNPC_VTzimisceHeadClaw::StartTask, 0x103c35d0 CNPC_VTzimisceRunner::StartTask, 0x103ccda0
// CNPC_VWerewolf::StartTask, 0x103645a0 CNPC_VBach::StartTask, 0x10374940 CNPC_VDog::StartTask,
// 0x103790d0 CNPC_VGargoyle::StartTask, 0x1037b8b0 CNPC_VGhoulCroucher::StartTask, 0x103805d0
// CNPC_VHengeyokai::StartTask, 0x1038c390 CNPC_VManBat::StartTask, 0x103a5650
// CNPC_VSabbatGunman::StartTask, 0x103ac740 CNPC_VScurrying::StartTask, 0x103b36d0
// CNPC_VTaxiDriver::StartTask, 0x103c5ac0 CNPC_VVampireBoss::StartTask, 0x103dfd80
// CNPC_VZombie::StartTask, 0x1035d1b0 CNPC_VAndreiBlood::StartTask, 0x103611a0
// CNPC_VAsianVampire::StartTask, 0x1036b750 CNPC_VChangBros::StartTask, 0x103a78c0
// CNPC_VSabbatLeader::StartTask, 0x103aec70 CNPC_VSheriffMan::StartTask, and (Damaged19)
// 0x10371b70 CNPC_VCop::StartTask. `0x10375f50 CNPC_VFrenzyShadow::StartTask` lives in
// `ElysiumNpcFrenzyShadow.cpp`.

#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcAnimal.h"
#include "Substrate/ElysiumNpcAsianVampire.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcCop.h"
#include "Substrate/ElysiumNpcDog.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcHuman.h"
#include "Substrate/ElysiumNpcMakerFleshpile.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcSabbatGunman.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcScurrying.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTaxiDriver.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcVampireBoss.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcZombie.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumLocalIdSpace.h"
#include "Substrate/ElysiumNpcDebug10Shared.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcFacingShared.h"
#include "Substrate/ElysiumNpcGait.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleCorpus.h"
#include "Substrate/ElysiumScheduleText.h"

// -------------------------------------------------------------------------------------------------
// Shared file-scope helpers. `static`, family-prefixed (unity builds concatenate this file with the
// other StartTask19 lanes'). Each names the retail call it stands for.
// -------------------------------------------------------------------------------------------------

/** Retail's `pTask->iTask`: the class-LOCAL number, recovered from the stored global id through slot
 *  450 `GetLocalTaskId` (`0x101a6640`, the `+0x18` task sub-space). -1 for a null task or an id no
 *  space in this class's chain holds -- retail has no such task, and -1 reaches every switch's
 *  default. */
static int32 Species19TaskLocal(FElysiumNpcBase& Npc, const FElysiumScheduleStep* Step)
{
	return Step != nullptr ? Npc.GetLocalTaskId(Step->TaskId) : INDEX_NONE;
}

/** A `Task_t` a body builds on its own stack (`{ iTask, flTaskData }`), with the local id translated
 *  forward through the class's task space so the parent body receives what the runner would hand it. */
static FElysiumScheduleStep Species19SyntheticStep(const FElysiumNpcBase& Npc, int32 LocalTask, float Data)
{
	FElysiumScheduleStep Step;
	const FElysiumLocalIdSpace* Space = Npc.IdSpace(EElysiumIdCategory::Task);
	Step.TaskId = Space != nullptr ? Space->LocalToGlobal(LocalTask) : INDEX_NONE;
	Step.Data = Data;
	return Step;
}

/** `gpGlobals->curtime` (`DAT_1070b228 + 0xc`). */
static double Species19Now(const FElysiumNpcBase& Npc)
{
	return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
}

/** `m_TaskFailTrace` `+0x1b44 = file, +0x1b48 = line`, then slot 448 `TaskFail(reason)` (`+0x700`,
 *  VIRTUAL). The two words are absent; the row carries the line, as `ElysiumNpcFrenzyShadow.cpp`'s
 *  `Fail` lambda does. */
static void Species19Fail(FElysiumNpcBase& Npc, const TCHAR* File, int32 Line, int32 Reason)
{
	Npc.RecordScheduleEvent(FString::Printf(TEXT("StartTask fail trace %s:%d"), File, Line));
	Npc.TaskFail(Reason);
}

/** Slot 448 `TaskFail(reason)` with NO trace stamp (the arms that call it bare). */
static void Species19FailBare(FElysiumNpcBase& Npc, int32 Reason)
{
	Npc.TaskFail(Reason);
}

/** `NAI_Hull::Width` `0x102d61b0`: `row[+0x18] - row[+0xc]` of the hull table row at `0x1060a750`,
 *  the FULL box's `maxs.y - mins.y`, Source units. The table is the replayed one family Motor stands
 *  (`RetailHullExtents`); an id the table lacks answers 0, which is what both extents read as then. */
static float Species19HullWidthUnits(const FElysiumNpcBase& Npc, int32 Hull)
{
	FVector Mins = FVector::ZeroVector;
	FVector Maxs = FVector::ZeroVector;
	Npc.RetailHullExtents(Hull, FElysiumNpcBase::EElysiumHullExtents::Full, Mins, Maxs);
	return static_cast<float>(Maxs.Y - Mins.Y);
}

/** `0x102e0b40(m_pMotor)`: `motor+0x2c = -1.0f`, the yaw-speed hold every species facing arm opens
 *  with -- family StartTask19's one port body, `FElysiumNpcBase::StartTaskMotorHoldYaw`. */
static void Species19MotorYawSpeedReset(FElysiumNpcBase& Npc)
{
	Npc.StartTaskMotorHoldYaw();
}

/** `0x102e2020(m_pMotor, target)`: the motor's ideal yaw at `target` (`0x102e2750` = slot 515
 *  `CalcIdealYaw`, the `+0x28` half-turn, the `+0x34` store) -- `FElysiumNpcBase::
 *  StartTaskMotorSetIdealYawToTarget`, the one port body. */
static void Species19MotorIdealYawTo(FElysiumNpcBase& Npc, const FVector& TargetCm)
{
	Npc.StartTaskMotorSetIdealYawToTarget(TargetCm);
}

/** `0x102e20b0(m_pMotor, target, speed)` is family RunTask19's `MotorSetIdealYawToTargetAndUpdate`
 *  (`CalcIdealYaw(target)` then `0x102e1c10(yaw, speed)`: the flip, the store, the `+0x38` speed word,
 *  `UpdateYaw`). */
static void Species19MotorIdealYawToAtSpeed(FElysiumNpcBase& Npc, const FVector& TargetCm, float Speed)
{
	Npc.MotorSetIdealYawToTargetAndUpdate(TargetCm, Speed);
}

/** `CAI_Navigator::SetGoal` `0x102ecd20` over the `AI_NavGoal_t` a species arm builds on its stack:
 *  family StartTask19's one port body, `FElysiumNpcBase::StartTaskSetGoal` (the route build
 *  `0x102f1dc0` with its navigator slot-2 complete and `OnNavFailed(0xc)`). Every species literal is a
 *  location goal (`GoalType` 4, or 9 for the interesting place), dest words `[4]` / `[6]` / `[7]` -1,
 *  flags 0; `ToleranceUnits` is `[8]` (the -1.0 / -2.0 sentinels pass through). */
static bool Species19SetGoal(FElysiumNpcBase& Npc, int32 GoalType, const FVector& GoalCm, int32 Activity,
	float ToleranceUnits, int32 SetGoalFlags)
{
	FElysiumNpcBase::FStartTaskNavGoal Goal;
	Goal.Type = GoalType;
	Goal.DestCm = GoalCm;
	Goal.bDestSet = true;
	Goal.MovementActivity = Activity;
	Goal.ToleranceUnits = ToleranceUnits;
	return Npc.StartTaskSetGoal(Goal, SetGoalFlags);
}

/** SEAM for `CBaseAnimating::GetSeqDesc(enemy->m_nSequence)` (`0x1000b4f6`) then the descriptor's
 *  `+0x2d8` word, the paired activity a humanoid's task `0x8b` copies off a downed enemy. The animating
 *  tier exposes no sequence descriptors to the kernel (`ComputeHitboxSurroundingBox`'s seam, family
 *  Positions2); this answers -1, retail's own "no descriptor / negative word" arm, which keeps the
 *  default activity. */
static int32 Species19EnemySequencePairedActivity(const FElysiumEntity& Enemy)
{
	(void)Enemy;
	return INDEX_NONE;
}

/** SEAM for `CBaseCombatCharacter::GetCharTemplate(this)` (`0x100040d9` -> `0x10207c40`), the
 *  character-template index the boss AOE arms refuse on when negative. No template index is carried on
 *  the NPC leaf (`ElysiumNpcPrecache10.inl`'s `FUN_10207e60` seam states the same absence); answers 0,
 *  the admitting value every templated boss takes. */
static int32 Species19CharTemplate(const FElysiumNpc& Npc)
{
	(void)Npc;
	return 0;
}

/** The four species slot-618 exertion bodies (`0x1037a100` gargoyle, `0x10381960` hengeyokai,
 *  `0x103c24d0` head claw, `0x103c3ff0` runner) are one shape over different three-row tables:
 *  `CPASAttenuationFilter(GetOrigin(), 0.8)`, then `EmitSound(filter, edict, 4, table[RandomInt(0, 2)],
 *  1.0, 0.8, 0, 100, 0, 0, 1, 0)`. The draw is made (`(*DAT_1070b244 + 8)(0, 2)`), because the stream
 *  position is observable. */
static void Species19EmitExertion(FElysiumNpc& Npc, const TCHAR* const (&Table)[3])
{
	const int32 Row = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 2);
	Npc.EmitNamedWav(&Npc, /*Channel*/ 4, Table[Row], ElysiumNpcTunables::One, /*Attenuation*/ 0.8f,
		/*Pitch*/ 100);
}

// The file names the `+0x1b44` stamp points at (`E:\Vampire\main\dlls\hl2_dll\...`).
static const TCHAR* const GSpecies19FileAnimal = TEXT("NPC_VAnimal.cpp");                 // 0x1062c318
static const TCHAR* const GSpecies19FileHuman = TEXT("NPC_VHuman.cpp");                   // 0x1063f724

// =================================================================================================
// CNPC_VTzimisceHeadClaw -- 0x103c1820, 100 bytes
// =================================================================================================

// The fat guy's three exerts, `0x1065ca78` (`TC_FatGuy`), the table slot 618 `0x103c24d0` indexes.
static const TCHAR* const GSpecies19FatGuyExerts[3] = {
	TEXT("character/monster/TC_FatGuy/Exert_Heavy_1.wav"),
	TEXT("character/monster/TC_FatGuy/Exert_Heavy_2.wav"),
	TEXT("character/monster/TC_FatGuy/Exert_Heavy_3.wav"),
};

void FElysiumNpcTzimisceHeadClaw::TransformationStartSlot618()
{
	// `0x103c24d0`, slot 618 on `CNPC_VTzimisceHeadClaw` (the class has no subclass, so the virtual
	// resolves here): the exertion sound and nothing else.
	Species19EmitExertion(*this, GSpecies19FatGuyExerts);
}

// Slot 442: `0x103c1820`.
int32 FElysiumNpcTzimisceHeadClaw::StartTaskSlot442(void* Task)
{
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	switch (TaskLocal)                                                   // 0x103c182d ADD -0x36; JA 0x103c1874 0x103c1835 JA; 0x103c183f table jump
	{
	case 0x122:
	case 0x123:
	case 0x124:
	{
		// The task is REPLACED: a stack `Task_t {0x4b, 1.0f}` goes to the Troika body and the original
		// id is never passed on.
		FElysiumScheduleStep Replacement = Species19SyntheticStep(*this, 0x4b, 1.0f);   // 0x103c184d / 0x103c1855
		return FElysiumNpc::StartTaskSlot442(&Replacement);                // 0x103c185d
	}
	case 0x36:
	case 0x37:
		TransformationStartSlot618();                                     // 0x103c186e CALL [EAX+0x9a8]
		break;                                                            // falls into 0x103c1874
	default:
		break;
	}
	return FElysiumNpc::StartTaskSlot442(Task);                          // 0x103c1877
}

// =================================================================================================
// CNPC_VTzimisceRunner -- 0x103c35d0, 248 bytes
// =================================================================================================

// The runner's three exerts, `0x1065d6a0` (`TC_Runner`), the table slot 618 `0x103c3ff0` indexes.
static const TCHAR* const GSpecies19RunnerExerts[3] = {
	TEXT("character/monster/TC_Runner/Exert_Heavy_1.wav"),
	TEXT("character/monster/TC_Runner/Exert_Heavy_2.wav"),
	TEXT("character/monster/TC_Runner/Exert_Heavy_3.wav"),
};

void FElysiumNpcTzimisceRunner::TransformationStartSlot618()
{
	// `0x103c3ff0`, slot 618 on `CNPC_VTzimisceRunner`: the exertion sound.
	Species19EmitExertion(*this, GSpecies19RunnerExerts);
}

// Slot 442: `0x103c35d0`.
int32 FElysiumNpcTzimisceRunner::StartTaskSlot442(void* Task)
{
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	switch (TaskLocal)                                                   // 0x103c35e2 JA 0x103c36bb 0x103c35f0 table jump
	{
	case 0x122:
	case 0x123:
	case 0x124:
		// `RestartIdealActivity(1)`, `m_flWaitFinished = m_flWaitFinishedDelta + curtime`, and return
		// WITHOUT the base.
		RestartIdealActivityId(1);                                        // 0x103c35fb
		BaseScheduleHost.WaitFinished =
			static_cast<double>(ScheduleHost.WaitFinishedDelta) + Species19Now(*this);   // 0x103c3607 / 0x103c3610
		return 0;                                                         // 0x103c3617
	case 0x130:
	{
		// A live `m_hPotentialEnemy` (+0x6678): its slot 220 `GetOrigin` into `m_vSavePosition`, then
		// `TaskComplete(0)`, and the base STILL runs with the same task. A dead handle writes nothing.
		FElysiumEntity* Potential = World != nullptr ? World->Resolve(RunnerPotentialEnemy) : nullptr;   // 0x103c3623..0x103c364f 0x103c3649 JNZ
		if (Potential == nullptr)
		{
			break;                                                        // -> 0x103c36bb
		}
		// The second resolve (`0x103c365a`..`0x103c3677`) re-reads the same handle; its null arm is (0x103c3671 JNZ)
		// unreachable after the first.
		SavePosition = Potential->Origin;                                 // 0x103c367b slot 220; 0x103c3685..0x103c3699
		TaskComplete(false);                                              // 0x103c369f
		return FElysiumNpc::StartTaskSlot442(Task);                      // 0x103c36a7
	}
	case 0x36:
	case 0x37:
		TransformationStartSlot618();                                     // 0x103c36b5 CALL [EAX+0x9a8]
		break;
	default:
		break;
	}
	return FElysiumNpc::StartTaskSlot442(Task);                          // 0x103c36be
}

// =================================================================================================
// CNPC_VTaxiDriver -- 0x103b36d0, 101 bytes
// =================================================================================================

// Slot 442: `0x103b36d0`.
int32 FElysiumNpcTaxiDriver::StartTaskSlot442(void* Task)
{
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	switch (TaskLocal)                                                   // 0x103b36e1 JA 0x103b3729 0x103b36eb table jump
	{
	case 0x2b:
	case 0x2e:
	case 0x2f:
	case 0x31:
	case 0xb2:
	case 0xba:
	case 0xbb:
	case 0xbc:
	case 0xbd:
	case 0xf8:
	case 0xf9:
	case 0xfb:
	case 0xfc:
	case 0xfd:
	case 0x11b:
	case 0x11d:
		// Suppressed: complete at once, the base never runs.
		TaskComplete(false);                                              // 0x103b36f6
		return 0;                                                         // 0x103b36fc
	case 0xb9:
		if (RunDialogActivity() == INDEX_NONE)                            // 0x103b3701 0x102c1400 / 0x103b370b
		{
			TaskComplete(false);                                          // 0x103b370f
			return 0;                                                     // 0x103b3715
		}
		SetActivity(0x114e);                                              // 0x103b371f slot 310
		return 0;                                                         // 0x103b3726
	default:
		break;
	}
	return FElysiumNpcHuman::StartTaskSlot442(Task);                     // 0x103b372c
}

// =================================================================================================
// CNPC_VSabbatGunman -- 0x103a5650, 97 bytes
// =================================================================================================

// Slot 442: `0x103a5650`.
int32 FElysiumNpcSabbatGunman::StartTaskSlot442(void* Task)
{
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	switch (TaskLocal)                                                   // 0x103a5662 JA 0x103a56a4 0x103a566c table jump
	{
	case 0x11b:
	case 0x11c:
	case 0x122:
	case 0x123:
	case 0x124:
	{
		// `m_flWaitFinishedDelta *= 1.0 / d`, `d` the `sabbat_gunman_speed_scalar` ConVar read the
		// retail way (`cv->vtable[4]() ? 0.0 : cv->m_fValue`, `0x103a567b`..`0x103a568f`), which
		// `ConVarFloat` states once. The `IsCommand` arm (divisor 0.0, `0x103a5682`) is the retail
		// divide by zero the verdict names; it is carried by the ConVar read, not guarded here.
		const float Divisor = ElysiumNpcTunables::ConVarFloat(
			ElysiumNpcTunables::EConVar::SabbatGunmanSpeedScalar); // 0x103a567b IsCommand / 0x103a5680 JZ
		ScheduleHost.WaitFinishedDelta =
			(ElysiumNpcTunables::One / Divisor) * ScheduleHost.WaitFinishedDelta;   // 0x103a5692 FDIVR; 0x103a5698 FMUL; 0x103a569e FSTP
		break;
	}
	default:
		break;
	}
	return FElysiumNpcHuman::StartTaskSlot442(Task);                     // 0x103a56a7 -> 0x103847f0 CNPC_VHuman::StartTask
}

// =================================================================================================
// CNPC_VCop -- 0x10371b70, 101 bytes (Damaged19)
// =================================================================================================

// Slot 442: `0x10371b70`.
int32 FElysiumNpcCop::StartTaskSlot442(void* Task)
{
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	if (TaskLocal == 0x107)                                              // 0x10371b76 SUB 0x107; JZ 0x10371bc8
	{
		TaskComplete(false);                                              // 0x10371bc8 / 0x10371bd0 (tail jump)
		return 0;
	}
	if (TaskLocal == 0x14a)                                              // 0x10371b7d SUB 0x43; JZ 0x10371b8b
	{
		// `m_hClosestPlayer` (+0x628c) resolved (null when stale), handed to slot 598 by TAIL JUMP.
		FElysiumEntity* Closest = World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;   // 0x10371b8b..0x10371bba
		Slot598(Closest);                                                 // 0x10371bc2 JMP [EDX+0x958]
		return 0;
	}
	return FElysiumNpcHuman::StartTaskSlot442(Task);                     // 0x10371b86 JMP 0x1000a0c9 -> 0x10387290 -> 0x103847f0 CNPC_VHuman::StartTask
}

// =================================================================================================
// CNPC_VDog -- 0x10374940, 163 bytes
// =================================================================================================

// Slot 442: `0x10374940`.
int32 FElysiumNpcDog::StartTaskSlot442(void* Task)
{
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	if (TaskLocal != 0x36)                                               // 0x10374950 JZ 0x10374973
	{
		if (TaskLocal == 0xbc)                                           // 0x10374957 JNZ 0x10374963
		{
			SetActivity(3);                                               // 0x1037495d slot 310
		}
		return FElysiumNpcAnimal::StartTaskSlot442(Task);                // 0x10374966
	}
	AutoMovement();                                                       // 0x10374975
	Species19MotorYawSpeedReset(*this);                                   // 0x10374980 0x102e0b40(m_pMotor)
	const FElysiumEntity* LkpKey = static_cast<const FElysiumNpc*>(this)->GetEnemy();   // 0x10374989 slot 167 (the LKP's key)
	FVector EnemyLkp = FVector::ZeroVector;                               // `vec3_origin` on a miss (0x102dfed0)
	EnemyLkp = Conditions19LastKnownPosition(LkpKey);                                     // 0x10374999 slot 541; 0x103749a1 -> 0x102dfed0
	Species19MotorIdealYawToAtSpeed(*this, EnemyLkp, -2.0f);              // 0x103749b6 0x102e20b0(motor, &lkp, 0xc0000000)
	RestartIdealActivityId(0x4b);                                         // 0x103749bf
	if (IsActivityFinished())                                             // 0x103749c8 slot 251; 0x103749d0
	{
		TaskComplete(false);                                              // 0x103749d6
	}
	return 0;                                                             // 0x103749e0
}

// =================================================================================================
// CNPC_VGhoulCroucher -- 0x1037b8b0, 250 bytes
// =================================================================================================

// Slot 442: `0x1037b8b0`.
int32 FElysiumNpcGhoulCroucher::StartTaskSlot442(void* Task)
{
	// The whole body runs inside a `g_ScopeTraceStack` frame named `"CNPC_VGhoulCroucher::StartTask"`
	// (`0x1037b8cd`..`0x1037b918`, popped on every exit) -- the debug ring, absent. (the name's own null tests 0x1037b8b5 JZ / 0x1037b8bf JNZ)
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	if (TaskLocal == 0x14a)                                              // 0x1037b920 SUB 0x14a; JZ 0x1037b977 (0x1037b925)
	{
		RestartIdealActivityId(UnawareTableA());                          // 0x1037b979 0x1037b870; 0x1037b97f
		if (IdealActivityNumber != UnawareTableA())                       // 0x1037b986; 0x1037b98b CMP [ESI+0xff0] / 0x1037b991 JZ
		{
			Species19FailBare(*this, 0x15);                               // 0x1037b999 slot 448
		}
		return 0;                                                         // 0x1037b9a7
	}
	if (TaskLocal == 0x14b)                                              // 0x1037b927 DEC; JZ 0x1037b93d (0x1037b928)
	{
		RestartIdealActivityId(UnawareTableB());                          // 0x1037b93f 0x1037b890; 0x1037b945
		if (IdealActivityNumber != UnawareTableB())                       // 0x1037b94c; 0x1037b951 / 0x1037b957 JZ
		{
			bUnawareExited = true;                                        // 0x1037b95f +0x6667 = 1
			Species19FailBare(*this, 0x15);                               // 0x1037b966 slot 448
		}
		return 0;                                                         // 0x1037b974 / 0x1037b9a7
	}
	return FElysiumNpcHuman::StartTaskSlot442(Task);                     // 0x1037b92d CALL 0x1000a0c9 -> 0x103847f0 CNPC_VHuman::StartTask
}

// =================================================================================================
// CNPC_VHuman -- 0x103847f0, 493 bytes
// =================================================================================================

// Slot 442: `0x103847f0`. Also carries the inherited body of CNPC_ProneDialog, CNPC_VBrujah,
// CNPC_VGuard1, CNPC_VHumanCombatPatrol, CNPC_VHumanCombatant, CNPC_VHunter, CNPC_VLasombra,
// CNPC_VPedestrian, CNPC_VPlayerController, CNPC_VVampire, CNPC_VWolfMorph, CNPC_VYukie.
int32 FElysiumNpcHuman::StartTaskSlot442(void* Task)
{
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	switch (TaskLocal)                                                   // 0x103847fe ADD -0x89; JA 0x103849cc (0x10384806); 0x10384814 table jump
	{
	case 0x89:
		LastAttackTime = Species19Now(*this);                             // 0x103848a8 +0x5d9c = curtime
		RestartIdealActivityId(0x4b);                                     // 0x103848ae
		return 0;
	case 0x8a:
		LastAttackTime = Species19Now(*this);                             // 0x103848c6
		RestartIdealActivityId(0x4b);                                     // 0x103848ce
		return 0;
	case 0x8b:
	{
		// A human working on a DOWNED enemy: face its remembered position and play the enemy's paired
		// sequence activity; with no enemy or no model, `0x51`.
		int32 Activity = 0x51;                                            // 0x103848e0 MOV EBX,0x51
		FElysiumEntity* Enemy = static_cast<const FElysiumNpc*>(this)->GetEnemy();   // 0x103848e5 slot 167
		if (Enemy != nullptr && Enemy->AsCombatCharacter() != nullptr)    // 0x103848ed / 0x103848f7 enemy+0x9c
		{
			Species19MotorYawSpeedReset(*this);                           // 0x103848ff 0x102e0b40
			const FElysiumEntity* LkpKey = static_cast<const FElysiumNpc*>(this)->GetEnemy();   // 0x10384908 slot 167 again
			FVector EnemyLkp = FVector::ZeroVector;
			EnemyLkp = Conditions19LastKnownPosition(LkpKey);                             // 0x10384918 slot 541; 0x10384920 -> 0x102dfed0
			Species19MotorIdealYawTo(*this, EnemyLkp);                    // 0x10384932 0x102e2020
			const int32 Paired = Species19EnemySequencePairedActivity(*Enemy);   // 0x10384940 GetSeqDesc; 0x10384949 +0x2d8 (null desc 0x10384947 JZ: 0x51 stays)
			if (Paired >= 0)                                              // 0x10384951 JL
			{
				Activity = Paired;                                        // 0x10384953
			}
		}
		RestartIdealActivityId(Activity);                                 // 0x10384958
		return 0;
	}
	case 0x8d:
		RestartIdealActivityId(0x1154);                                   // 0x1038498d PUSH; 0x10384994
		return 0;
	case 0x8e:
		RestartIdealActivityId(0x52);                                     // 0x1038496a
		return 0;
	case 0x8f:
		RestartIdealActivityId(0x1156);                                   // 0x1038497f
		return 0;
	case 0x90:
		RestartIdealActivityId(0x1152);                                   // 0x103849a9
		return 0;
	case 0x91:
		RestartIdealActivityId(0x1153);                                   // 0x103849be
		return 0;
	case 0x9f:
	{
		// With a weapon: `weapon->+0x8c0 * data + 2 * NAI_Hull::Width(m_eHull)` into the navigator's
		// PATH tolerance (`0x1038485b` thunk `0x1001402e` -> `0x102ee1c0` on `m_pNavigator +0x5d34`:
		// `path+0x28`, `NavPathToleranceCm`; `m_flGoalTolerance +0x6320` is not written), then
		// `TaskComplete`. Without one: line 0x138, `TaskFail(3)`.
		if (ActiveWeaponEntity() == nullptr)                              // 0x1038481d GetActiveWeapon; 0x10384824
		{
			Species19Fail(*this, GSpecies19FileHuman, 0x138, 3);          // 0x10384878 / 0x10384882 / 0x1038488c
			return 0;
		}
		const float Width = Species19HullWidthUnits(*this, HullKind);    // 0x10384833 0x102d61b0(m_eHull)
		float WeaponRangeUnits = 0.f;
		ActiveWeaponMaxRangeUnits(WeaponRangeUnits);                      // 0x10384843; 0x10384848 FLD [EAX+0x8c0]
		NavPathToleranceCm = (WeaponRangeUnits * Step->Data + (Width + Width)) * ElysiumMove::U;   // 0x1038484e FMUL; 0x10384838 FADD ST0,ST0; 0x1038485b 0x102ee1c0
		TaskComplete(false);                                              // 0x10384864
		return 0;
	}
	default:
		break;
	}
	return FElysiumNpc::StartTaskSlot442(Task);                          // 0x103849cf CAI_BaseNPCTroika::StartTask
}

// =================================================================================================
// CNPC_VAnimal -- 0x1035f650, 527 bytes
// =================================================================================================

// Slot 442: `0x1035f650`.
int32 FElysiumNpcAnimal::StartTaskSlot442(void* Task)
{
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	switch (TaskLocal)                                                   // 0x1035f65d ADD -0x89; JA 0x1035f84f (0x1035f665); 0x1035f673 table jump
	{
	case 0x89:
		LastAttackTime = Species19Now(*this);                             // 0x1035f707
		RestartIdealActivityId(0x4b);                                     // 0x1035f70d
		return 0;
	case 0x8a:
		LastAttackTime = Species19Now(*this);                             // 0x1035f724
		RestartIdealActivityId(0x4b);                                     // 0x1035f72c
		return 0;
	case 0x8b:
		RestartIdealActivityId(0x50);                                     // 0x1035f73d
		return 0;
	case 0x8e:
		RestartIdealActivityId(0x52);                                     // 0x1035f74e
		return 0;
	case 0x9f:
	{
		if (ActiveWeaponEntity() == nullptr)                              // 0x1035f67c / 0x1035f683
		{
			Species19Fail(*this, GSpecies19FileAnimal, 0x165, 3);         // 0x1035f6d8 / 0x1035f6e2 / 0x1035f6ec
			return 0;
		}
		const float Width = Species19HullWidthUnits(*this, HullKind);    // 0x1035f693 0x102d61b0(m_eHull)
		float WeaponRangeUnits = 0.f;
		ActiveWeaponMaxRangeUnits(WeaponRangeUnits);                      // 0x1035f6a3; 0x1035f6a8 FLD [EAX+0x8c0]
		// `0x1035f6bb` thunk `0x1001402e` -> `0x102ee1c0` on `m_pNavigator`: the PATH tolerance.
		NavPathToleranceCm = (WeaponRangeUnits * Step->Data + (Width + Width)) * ElysiumMove::U;   // 0x1035f6ae / 0x1035f698 / 0x1035f6bb
		TaskComplete(false);                                              // 0x1035f6c4
		return 0;
	}
	case 0xa5:
	{
		// No `m_pInterestingPlace` (+0x62ec): line 0x19e, `TaskFail(0x22)`. Otherwise a goal of type 9
		// at `m_vecInterestingPlace` (+0x62f0), the three entity words and the activity words -1,
		// tolerance -1.0 (`0x104a8730`), flags 0; accepted -> `TaskComplete`, refused -> line 0x199,
		// `TaskFail(0xc)`.
		if (CurrentAmbientSpot() == nullptr)                              // 0x1035f75b / 0x1035f765
		{
			Species19Fail(*this, GSpecies19FileAnimal, 0x19e, 0x22);      // 0x1035f82d / 0x1035f837 / 0x1035f841
			return 0;
		}
		// `m_vecInterestingPlace` is SOURCE units (`TASK_FIND_INTERESTING_PLACE` `0x102a1f42` writes it).
		if (Species19SetGoal(*this, 9, InterestingPlacePosition * ElysiumMove::U, INDEX_NONE,
				-1.f, 0))                                                 // 0x1035f76b..0x1035f7e8 0x102ecd20 / 0x1035f7f1 JZ
		{
			TaskComplete(false);                                          // 0x1035f7f4
			return 0;
		}
		Species19Fail(*this, GSpecies19FileAnimal, 0x199, 0xc);           // 0x1035f805 / 0x1035f80f / 0x1035f819
		return 0;
	}
	default:
		break;
	}
	return FElysiumNpc::StartTaskSlot442(Task);                          // 0x1035f852 CAI_BaseNPCTroika::StartTask
}

// =================================================================================================
// CNPC_VAndreiBlood -- 0x1035d1b0, 1366 bytes
// =================================================================================================

static const TCHAR* const GSpecies19FileAndreiBlood = TEXT("NPC_VAndreiBlood.cpp");       // 0x1062b1a0

/** The three Andrei sound arms' shared shape (`0x1035d221`.. / `0x1035d2f6`.. / `0x1035d547`..):
 *  `CPASAttenuationFilter(GetOrigin(), 0.8)`, then `EmitSound(filter, edict, 2, wav, 1.0, 0.8, 0, 100,
 *  0, 0, 1, 0)` (`0x1070b248` slot 3 over `0x1070b22c` slot `+0x8c`). */
static void Species19AndreiSound(FElysiumNpc& Npc, const TCHAR* Wav)
{
	Npc.EmitNamedWav(&Npc, /*Channel*/ 2, Wav, ElysiumNpcTunables::One, /*Attenuation*/ 0.8f, /*Pitch*/ 100);
}

// Slot 442: `0x1035d1b0`.
int32 FElysiumNpcAndreiBlood::StartTaskSlot442(void* Task)
{
	// A `g_ScopeTraceStack` frame named `"CNPC_VAndreiBlood::StartTask"` wraps the body -- absent.
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	switch (TaskLocal)                                                   // 0x1035d20c ADD -0x14b; JA 0x1035d6ee; JMP [0x1035d708] (0x1035d214 JA; 0x1035d21a table jump)
	{
	case 0x14b:
	{
		// Unhide at the hint. No hint: line 0x130, `TaskFail(4)`.
		FHintWords Hint;
		if (BaseScheduleHost.HintNode == INDEX_NONE || !HintWords(BaseScheduleHost.HintNode, Hint))   // 0x1035d3f1 / 0x1035d3f9
		{
			Species19Fail(*this, GSpecies19FileAndreiBlood, 0x130, 4);    // 0x1035d401 / 0x1035d40b / 0x1035d415
			return 0;
		}
		FVector HintOrigin = Hint.OriginCm;
		SetOrigin(HintOrigin);                                         // 0x1035d42f slot 217; 0x1035d438 slot 216 (0x100b2300 unparented: m_vecOrigin; slot 216 is an unported stub, slot 62's body is the one origin write)
		FVector Placed = Origin;                                          // 0x1035d442 slot 220
		Teleport(&Placed, nullptr, nullptr);                              // 0x1035d469 slot 181
		SetOrigin(Placed);                                                // 0x1035d478 slot 62
		SetHullSizeNormal(true);                                          // 0x1035d482 0x10273070
		bAndreiTriggerUnhide = true;                                      // 0x1035d48d +0x66ce = 1
		EffectsWord &= ~0x20u;                                            // 0x1035d494 m_fEffects &= ~0x20
		SolidFlagsWord &= ~0x4u;                                          // 0x1035d501 AND 0xfffffffb; 0x1035d505 (RemoveSolidFlags scope frame) (its name test 0x1035d4a5 JNZ)
		// `0x1035d513` ForceTransmit and `0x1035d51a` Relink: no transmit state or spatial partition.
		EffectsWord |= 0x10u;                                             // 0x1035d527
		TaskComplete(false);                                              // 0x1035d532
		return 0;
	}
	case 0x150:
		return 0;                                                         // jump table -> 0x1035d6f6 epilogue: nothing
	case 0x151:
		Species19AndreiSound(*this, TEXT("Character/Boss/Andrei/TeleportOut.wav"));   // 0x1035d29f / 0x1035d2a7 / 0x1035d2b9 the filter: 0x1035d22a / 0x1035d236 / 0x1035d248 / 0x1035d263 / 0x1035d26e, torn down 0x1035d2c0 / 0x1035d2c9
		StartBloodEmitter(TEXT("Andrei_Teleport_Out-Emitter"));           // 0x1035d2ce / 0x1035d2d5 0x10003a0d
		RestartIdealActivityId(0x113c);                                   // 0x1035d2e1; no completion
		return 0;
	case 0x152:
		Species19AndreiSound(*this, TEXT("Character/Boss/Andrei/TeleportIn.wav"));    // 0x1035d374 / 0x1035d37c / 0x1035d38e the filter: 0x1035d2ff / 0x1035d30b / 0x1035d31d / 0x1035d338 / 0x1035d343, torn down 0x1035d395 / 0x1035d39e
		StartBloodEmitter(TEXT("Andrei_Teleport_In-Emitter"));            // 0x1035d3a3 / 0x1035d3aa
		RestartIdealActivityId(0x113b);                                   // 0x1035d3b6; no completion
		return 0;
	case 0x153:
		BaseScheduleHost.HintNode = SelectTeleportNodeAndrei();           // 0x1035d3cd 0x10002216; 0x1035d3d6 +0x5ddc
		TaskComplete(false);                                              // 0x1035d3dc
		return 0;
	case 0x154:
		// The summon: the sound, `RestartIdealActivity(0x113a)`, the summon emitter, then the nearest
		// `npc_maker_fleshpile` inside 1024 of `GetAbsOrigin` makes a runner (slot 617 `MakeNPC(0)`).
		// No completion.
		Species19AndreiSound(*this, TEXT("Character/Boss/Andrei/Summon.wav"));        // 0x1035d5c5 / 0x1035d5cd / 0x1035d5df the filter: 0x1035d550 / 0x1035d55c / 0x1035d56e / 0x1035d589 / 0x1035d594, torn down 0x1035d648 / 0x1035d651 / 0x1035d662
		RestartIdealActivityId(0x113a);                                   // 0x1035d5e9
		StartSummonEmitter(TEXT("Andrei_Summon-Emitter"));                // 0x1035d5ee / 0x1035d5f5 0x10015910
		if (World != nullptr)
		{
			FElysiumNpcMakerFleshpile::SummonRunnerNear(*World, Origin);  // 0x1035d603 slot 217; 0x1035d614 0x100f7d50(1024.0); 0x1035d62c RTTI; 0x1035d63e CALL [EDX+0x9a4] (0x1035d61b JZ no maker / 0x1035d636 JZ failed cast)
		}
		return 0;
	case 0x155:
	{
		// `m_OnDeath.FireOutput(closest player or null, this, 0)` then `UTIL_Remove(this)`.
		const FElysiumEntity* Closest = World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;   // 0x1035d677..0x1035d6a3 (0x1035d680 JZ / 0x1035d69d JNZ)
		FireOutput(FName(TEXT("OnDeath")),
			Closest != nullptr ? Closest->Handle : FElysiumEntityHandle::Invalid());   // 0x1035d6af 0x10010794 (+0x5e24)
		UtilRemoveSelf();                                                 // 0x1035d6b5 0x10014614 -> 0x101cd940
		return 0;
	}
	case 0x156:
		AndreiTeleportWaitStartTime = Species19Now(*this);                // 0x1035d6d9 +0x66d0 = curtime; no completion
		return 0;
	default:
		break;
	}
	return FElysiumNpcVampireBoss::StartTaskSlot442(Task);               // 0x1035d6f1 0x1000de7c
}

// =================================================================================================
// CNPC_VAsianVampire -- 0x103611a0, 243 bytes
// =================================================================================================

static const TCHAR* const GSpecies19FileAsianVampire = TEXT("NPC_VAsianVampire.cpp");     // 0x1062cd2c

// Slot 442: `0x103611a0`.
int32 FElysiumNpcAsianVampire::StartTaskSlot442(void* Task)
{
	// A `g_ScopeTraceStack` frame named `"CNPC_VAsianVampire::StartTask"` wraps the body -- absent.
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	switch (TaskLocal) // 0x103611f7 SUB 0x150; 0x103611fc JZ / 0x10361203 JZ (0x151) / 0x10361206 JZ (0x152)
	{
	case 0x150:
		return 0;                                                         // 0x103611fc JZ 0x10361288: consumed, nothing
	case 0x151:
		// `m_pHintNode = SelectLedgeNode()`; null stamps line 0x110 and `TaskFail(1)` -- and BOTH
		// paths then reach the one shared `TaskComplete(0)`.
		BaseScheduleHost.HintNode = SelectLedgeNodeAsian();              // 0x10361250 0x1000c61c; 0x10361257 +0x5ddc
		if (BaseScheduleHost.HintNode == INDEX_NONE)                     // 0x1036125d
		{
			Species19Fail(*this, GSpecies19FileAsianVampire, 0x110, 1);   // 0x10361265 / 0x1036126f / 0x10361279
		}
		TaskComplete(false);                                              // 0x10361283
		return 0;
	case 0x152:
		BaseScheduleHost.HintNode = SelectJumpbaseNode();                 // 0x1036121d 0x10006a1e; 0x10361224 +0x5ddc
		if (BaseScheduleHost.HintNode == INDEX_NONE)                     // 0x1036122a
		{
			Species19Fail(*this, GSpecies19FileAsianVampire, 0x118, 1);   // 0x10361232 / 0x1036123c / 0x10361246
		}
		TaskComplete(false);                                              // 0x10361283
		return 0;
	default:
		break;
	}
	return FElysiumNpcVampireBoss::StartTaskSlot442(Task);               // 0x1036120b 0x1000de7c CNPC_VVampireBoss::StartTask
}

// =================================================================================================
// CNPC_VBach -- 0x103645a0, 2504 bytes
// =================================================================================================

static const TCHAR* const GSpecies19FileBach = TEXT("NPC_VBach.cpp");                     // 0x1062eadc
static const TCHAR* const GSpecies19BachRifle = TEXT("item_w_rem_m_700_bach");            // 0x105c1c70
static const TCHAR* const GSpecies19BachKatana = TEXT("item_w_katana");                   // 0x10587668
// The sniper-wait constants, read out of the pinned image (none is in the tunables table):
static constexpr float GSpecies19BachNonRifleWaitCut = 0.35f;         // `_DAT_1046dcdc`
static constexpr float GSpecies19BachAimBase = 0.2f;                  // `_DAT_10451ab4`
static constexpr float GSpecies19BachAimPerStatC = 0.08f;             // `_DAT_104aaac0`
static constexpr float GSpecies19BachAimPerStat109 = 0.1f;            // `_DAT_104491b4`
static constexpr float GSpecies19BachWarningLead = 1.5f;              // `_DAT_1044f02c`
static constexpr float GSpecies19BachCamperShort = 0.15f;             // `_DAT_104aaac4`
static constexpr float GSpecies19BachCamperNever = 1000000000.0f;     // `_DAT_104aaac8`
static constexpr float GSpecies19BachShieldInterval = 6.0f;           // `_DAT_1046bac0`
static constexpr float GSpecies19BachShieldDuration = 3.0f;           // `_DAT_10449258`
// `DAT_1062d210` -- the skip-to-warning time per teleport state, read out of the pinned image.
static constexpr float GSpecies19BachSkipToWarning[4] = { 1.3f, 1.0f, 1.2f, 1.2f };
// `CAI_BaseNPC::CapabilitiesAdd/Remove(1)` -- capability bit 0.
static constexpr int32 GSpecies19BachCapabilityBit = 0x1;

int32 FElysiumNpcBach::BachEnemyFeatValue(int32 FeatId, const FElysiumEntity* Enemy) const
{
	// SEAM for the global `CVFeatList_t` row walk (`DAT_10739d20` count, `DAT_10739d24` rows, row
	// `[0] == FeatId`, else the lazily built empty row `DAT_109f0ac0`) and `0x101e56e0(row,
	// enemy->+0x9c)`, the enemy's feat value. The feat rows are not loaded at the kernel tier; answers
	// 0, the empty row's own answer.
	(void)FeatId;
	(void)Enemy;
	return 0;
}

void FElysiumNpcBach::BachSetType3StatBase(int32 StatId, int32 Value)
{
	// SEAM for `CVStatList_t::SetBase(stat, value)` (`0x10011e46`) on this body's type-3 stat list (the
	// `+0x13bc`/`+0x13c0` walk for tag `+0x10 == 3`, else `DAT_109f0b40`). The port stands no type-3
	// container (`HasTypedStatList`), so the write lands nowhere; recorded.
	BachType3StatWrites.Add(FIntPoint(StatId, Value));
}

void FElysiumNpcBach::FUN_103656a0()
{
	// SEAM for `0x103656a0`, the holy-light equip: `Weapon_OwnsThisType("item_d_holy_light")`, else
	// `Weapon_Create` + `Inventory_Can_Insert` + slot 383 `Weapon_Equip`; slot 388 `Weapon_Switch`;
	// `SetIdealActivity(5)`. Not this lane's row; counted.
	++Fun103656a0Calls;
}

// Slot 442: `0x103645a0`.
int32 FElysiumNpcBach::StartTaskSlot442(void* Task)
{
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	const double Now = Species19Now(*this);
	auto IsRifle = [](const FElysiumEntity* Weapon)
	{
		return Weapon != nullptr && Weapon->Def != nullptr
			&& FCString::Stricmp(*Weapon->Def->Classname, GSpecies19BachRifle) == 0;   // GetClassname; __strcmpi
	};
	auto SwitchTo = [this](const TCHAR* Classname)
	{
		// `Inventory_Find(classname)` then, when found, slot 388 `Weapon_Switch(item, 0)`.
		if (FElysiumItem* Item = Inventory.FindOrdinary(*this, FString(Classname)))
		{
			Weapon_Switch(Item, 0);
		}
	};

	if (TaskLocal > 0x14c)                                               // 0x103645af CMP 0x14c; JG 0x10364aac (0x103645b4)
	{
		switch (TaskLocal)                                               // 0x10364aac SUB 0x14d; JA 0x10364f56 (0x10364ab4; 0x10364aba table jump)
		{
		case 0x14d:
		{
			// Stat `0xf` base 1 on the type-3 list, then the active weapon's `+0x518` and `+0x4f0(0)`.
			if (FElysiumEntity* Weapon = ActiveWeaponEntity())           // 0x10364ac3; 0x10364acc
			{
				BachSetType3StatBase(0xf, 1);                             // 0x10364b26 / 0x10364b28 / 0x10364b2c (the type-3 list walk 0x10364ad8 JLE / 0x10364ae8 JZ / 0x10364af0 JL, the lazy empty row 0x10364afc JNZ / 0x10364b0f / 0x10364b19: inside the seam)
				++BachWeaponVtable518Calls;                               // 0x10364b35 CALL [EDX+0x518] (SEAM: no weapon body)
				++BachWeaponVtable4f0Calls;                               // 0x10364b41 CALL [EAX+0x4f0](0) (SEAM)
				(void)Weapon;
			}
			TaskComplete(false);                                          // 0x10364b4b
			return 0;
		}
		case 0x14e:
			// The teleport-ring hint for the current state; the search installs and completes, or
			// fails 4 itself (`0x10365780`).
			switch (BachTeleportState)                                   // 0x10364b60 +0x669c (0x10364b6a JNZ / 0x10364b85 JNZ / 0x10364ba0 JNZ / 0x10364bba JNZ)
			{
			case 0: FindHintNode(0x4268, 2); return 0;                    // 0x10364b73
			case 1: FindHintNode(0x4269, 2); return 0;                    // 0x10364b8e
			case 2: FindHintNode(0x426a, 2); return 0;                    // 0x10364ba8
			case 3: FindHintNode(0x426b, 2); return 0;                    // 0x10364bc3
			default:
				Species19Fail(*this, GSpecies19FileBach, 0x350, 4);       // 0x10364bd6 / 0x10364be0 / 0x10364bea
				return 0;
			}
		case 0x14f:
		{
			// The teleport onto the hint.
			FHintWords Hint;
			if (BaseScheduleHost.HintNode == INDEX_NONE || !HintWords(BaseScheduleHost.HintNode, Hint))   // 0x10364bfa / 0x10364c06
			{
				Species19Fail(*this, GSpecies19FileBach, 0x355, 4);        // 0x10364c0c / 0x10364c16 / 0x10364c20
				return 0;
			}
			CapabilityWord &= ~GSpecies19BachCapabilityBit;               // 0x10364c32 CapabilitiesRemove(1)
			bBachMovementSpot = false;                                    // 0x10364c3f +0x66a7 = 0
			FVector HintOrigin = Hint.OriginCm;
			SetOrigin(HintOrigin);                                     // 0x10364c47 slot 217; 0x10364c50 slot 216 (0x100b2300 unparented: m_vecOrigin; slot 216 is an unported stub, slot 62's body is the one origin write)
			FVector Placed = Origin;                                      // 0x10364c5a slot 220
			Teleport(&Placed, nullptr, nullptr);                          // 0x10364c7f slot 181
			SetOrigin(Placed);                                            // 0x10364c8e slot 62
			// `0x10364c96` ForceTransmit and `0x10364c9d` Relink: no transmit state or partition.
			++BachTeleportState;                                          // 0x10364cad / 0x10364cb0
			if (BachTeleportState > 3)                                    // 0x10364cb6 / 0x10364cb8 JLE
			{
				BachTeleportState = 0;                                    // 0x10364cba
			}
			BachSetType3StatBase(0xd, 5);                                 // 0x10364d1b / 0x10364d1d / 0x10364d21 (the list walk 0x10364cca JLE / 0x10364cd9 JZ / 0x10364ce5 JL, the empty row 0x10364cf1 JNZ / 0x10364d04 / 0x10364d0e: inside the seam)
			BachNextShieldTime = BachNextShieldTime + static_cast<double>(GSpecies19BachShieldInterval);   // 0x10364d26 / 0x10364d2c / 0x10364d39
			bBachShieldActive = true;                                     // 0x10364d50
			BachShieldTime = Now + static_cast<double>(GSpecies19BachShieldDuration);   // 0x10364d45 / 0x10364d48 / 0x10364d57
			EmitNamedWav(this, /*Channel*/ 2, TEXT("Character/Boss/Bach/bach_shield.wav"),
				ElysiumNpcTunables::One, /*Attenuation*/ 0.8f, /*Pitch*/ 100);   // 0x10364dd2 / 0x10364dda / 0x10364dec (the filter: 0x10364d5d / 0x10364d69 / 0x10364d7b / 0x10364d96 / 0x10364da1, torn down 0x10364dfc / 0x10364e05 / 0x10364e19 JZ / 0x10364e21 JZ / 0x10364e28)
			TaskComplete(false);                                          // 0x10364df3
			return 0;
		}
		case 0x150:
			if (BachTeleportState == 1)                                   // 0x10364e43 / 0x10364e50
			{
				CapabilityWord |= GSpecies19BachCapabilityBit;            // 0x10364e55 CapabilitiesAdd(1)
				FindHintNode(0x426c, 2);                                  // 0x10364e63
				bBachMovementSpot = true;                                 // 0x10364e68
				return 0;
			}
			if (BachTeleportState == 0)                                   // 0x10364e78 (0x10364e7a JNZ)
			{
				CapabilityWord |= GSpecies19BachCapabilityBit;            // 0x10364e7f
				FindHintNode(0x426d, 2);                                  // 0x10364e8d
				return 0;
			}
			if (BachTeleportState == 3)                                   // 0x10364e9c (0x10364e9f JNZ)
			{
				CapabilityWord |= GSpecies19BachCapabilityBit;            // 0x10364ea4
				bBachMovementSpot = true;                                 // 0x10364ea9
			}
			// States 2 and 3 (and any other): skip to the warning and fail 4 at line 0x387.
			bBachSkipToWarning = true;                                    // 0x10364eb7 +0x66a2 = 1
			BachSkipToWarningTime = GSpecies19BachSkipToWarning[BachTeleportState & 3];   // 0x10364ebf [ECX*4+0x1062d210]; 0x10364ec8 +0x6698
			Species19Fail(*this, GSpecies19FileBach, 0x387, 4);           // 0x10364ebd / 0x10364ed8 / 0x10364ee2
			return 0;
		case 0x151:
			if (!bBachMovementSpot)                                       // 0x10364ef2 / 0x10364efa
			{
				CapabilityWord &= ~GSpecies19BachCapabilityBit;           // 0x10364f00 CapabilitiesRemove(1)
			}
			SwitchTo(GSpecies19BachRifle);                                // 0x10364f0c / 0x10364f1c (0x10364f13 JZ)
			bBachSniperWait = false;                                      // 0x10364f28 +0x66a4 = 0
			bBachSkipToWarning = true;                                    // 0x10364f2f +0x66a2 = 1
			BachSkipToWarningTime = GSpecies19BachSkipToWarning[BachTeleportState & 3];   // 0x10364f36 / 0x10364f3d
			TaskComplete(false);                                          // 0x10364f47
			return 0;
		default:
			return FElysiumNpcHuman::StartTaskSlot442(Task);             // 0x10364f59
		}
	}
	if (TaskLocal == 0x14c)                                              // 0x103645ba JZ 0x103649cd
	{
		FUN_103656a0();                                                   // 0x103649cf 0x1000d4d1
		EmitNamedWav(this, /*Channel*/ 2, TEXT("Character/Boss/Bach/bach_holy_light.wav"),
			ElysiumNpcTunables::One, /*Attenuation*/ 0.8f, /*Pitch*/ 100);   // 0x10364a52 / 0x10364a5a / 0x10364a6c (the filter: 0x103649dd / 0x103649e9 / 0x103649fb / 0x10364a16 / 0x10364a21, torn down 0x10364a83 / 0x10364a8c / 0x10364a9d)
		bBachInStartingPosition = false;                                  // 0x10364a73 +0x66a1 = 0
		TaskComplete(false);                                              // 0x10364a7a
		return 0;
	}
	if (TaskLocal > 0xbd)                                                // 0x103645c0 CMP 0xbd; JG 0x10364930 (0x103645c5)
	{
		if (TaskLocal == 0x14a)                                          // 0x10364930 SUB 0x14a; JZ 0x10364982 (0x10364935)
		{
			SwitchTo(GSpecies19BachRifle);                                // 0x10364989 / 0x10364999 (0x10364990 JZ)
			bBachSniperWait = false;                                      // 0x1036499f +0x66a4 = 0
			BachNextWeaponSwitchTime = Now + static_cast<double>(ElysiumNpcTunables::Half);   // 0x103649af / 0x103649b2 / 0x103649b8 +0x668c
			TaskComplete(false);                                          // 0x103649be
			return 0;
		}
		if (TaskLocal == 0x14b)                                          // 0x10364937 DEC; JNZ 0x10364f56 (0x10364938)
		{
			SwitchTo(GSpecies19BachKatana);                               // 0x10364945 / 0x10364955 (0x1036494c JZ)
			BachNextWeaponSwitchTime = Now + static_cast<double>(ElysiumNpcTunables::Half);   // 0x10364964 / 0x10364967 / 0x1036496d
			TaskComplete(false);                                          // 0x10364973
			return 0;
		}
		return FElysiumNpcHuman::StartTaskSlot442(Task);                 // 0x10364f59
	}
	if (TaskLocal >= 0xba)                                               // 0x103645cb CMP 0xba; JGE 0x10364f43 (0x103645d0)
	{
		TaskComplete(false);                                              // 0x10364f47: 0xba..0xbd complete at once
		return 0;
	}
	switch (TaskLocal)                                                   // 0x103645d6 SUB 0x34; JA 0x10364f56 (0x103645dc; 0x103645ea table jump)
	{
	case 0x34:
	case 0x35:
	{
		// Holding the rifle with `+0x66a4` clear only completes -- Bach does not turn while aimed.
		// Anything else runs the human base and THEN clears `+0x66a4`.
		const FElysiumEntity* Weapon = ActiveWeaponEntity();              // 0x103648c2
		if (Weapon != nullptr && !IsRifle(Weapon))                        // 0x103648cb / 0x103648da / 0x103648e4 (0x103648d4 the classname test)
		{
			const int32 Result = FElysiumNpcHuman::StartTaskSlot442(Task);   // 0x103648e9
			bBachSniperWait = false;                                      // 0x103648ee
			return Result;
		}
		if (!bBachSniperWait)                                             // 0x103648fe / 0x10364908
		{
			TaskComplete(false);                                          // 0x1036490b
			return 0;
		}
		const int32 Result = FElysiumNpcHuman::StartTaskSlot442(Task);   // 0x1036491b
		bBachSniperWait = false;                                          // 0x10364920
		return Result;
	}
	case 0xb0:
	case 0xb1:
	{
		// The sniper wait.
		const FElysiumEntity* Weapon = ActiveWeaponEntity();              // 0x103645f3
		if (Weapon == nullptr)                                            // 0x103645fc
		{
			TaskComplete(false);                                          // 0x103648b1
			return 0;
		}
		if (!IsRifle(Weapon))                                             // 0x1036460f __strcmpi; 0x10364619 (0x10364609 the classname test)
		{
			bBachSniperWait = true;                                       // 0x1036461e +0x66a4 = 1
			const int32 Result = FElysiumNpcHuman::StartTaskSlot442(Task);   // 0x10364625
			BaseScheduleHost.WaitFinished = BaseScheduleHost.WaitFinished
				- static_cast<double>(GSpecies19BachNonRifleWaitCut);     // 0x1036462a / 0x10364630 / 0x10364637
			return Result;
		}
		BaseScheduleHost.WaitFinished = Now;                              // 0x10364654 / 0x10364657
		if (bBachCamperFlag)                                              // 0x1036464c / 0x1036465d
		{
			if (BachWasOccluded != 0)                                     // 0x1036465f +0x6674 / 0x10364665 JZ
			{
				BaseScheduleHost.WaitFinished = Now + static_cast<double>(GSpecies19BachCamperNever);   // 0x1036466e / 0x10364671 / 0x10364677
				BachWarningTime = BaseScheduleHost.WaitFinished;          // 0x1036467d +0x6694
			}
			else
			{
				BaseScheduleHost.WaitFinished = Now + static_cast<double>(GSpecies19BachCamperShort);   // 0x10364688 / 0x1036469a
				bBachCamperFlag = false;                                  // 0x1036468e
				BachReusedOccludeCount = 0;                               // 0x10364694 +0x6678
				BachWarningTime = BaseScheduleHost.WaitFinished + static_cast<double>(GSpecies19BachCamperNever);   // 0x103646a0 / 0x103646a6
			}
		}
		else
		{
			if (!bBachSkipToWarning)                                      // 0x103646b1 +0x66a2 / 0x103646b7 JNZ
			{
				BaseScheduleHost.WaitFinished = Now + static_cast<double>(GSpecies19BachAimBase);   // 0x103646bd / 0x103646cf
				int32 StatC = 1;                                          // 0x103646c7 [ESP+0x58] = 1
				if (const FElysiumEntity* Enemy = static_cast<const FElysiumNpc*>(this)->GetEnemy())   // 0x103646d5 slot 167; 0x103646dd
				{
					StatC = BachEnemyFeatValue(0xc, Enemy);               // 0x103646e3..0x10364745 row 0xc; 0x1000f03d (the row walk 0x103646f9 JLE / 0x10364708 JZ / 0x1036470d JL, the lazy empty row 0x10364716 JNZ / 0x10364726 / 0x10364730; the value 0x10364740: inside the seam)
				}
				BaseScheduleHost.WaitFinished = static_cast<double>(static_cast<float>(StatC) * GSpecies19BachAimPerStatC)
					+ BaseScheduleHost.WaitFinished;                      // 0x10364749 FILD; 0x1036474d FMUL; 0x10364753 FADD; 0x10364776 FSTP
			}
			else
			{
				BaseScheduleHost.WaitFinished = Now + static_cast<double>(BachSkipToWarningTime);   // 0x10364760 FADD +0x6698; 0x10364776
				BachSkipToWarningTime = 0.f;                              // 0x10364766
				bBachSkipToWarning = false;                               // 0x1036476c
			}
			// `max(enemy feat 10, enemy feat 9) * 0.1`. Retail reads the enemy's `+0x9c` UNCHECKED here
			// (a null enemy faults); the port hands the seam null instead (crash guard).
			const FElysiumEntity* Enemy = static_cast<const FElysiumNpc*>(this)->GetEnemy();   // 0x1036477c / 0x103647e8 slot 167
			int32 Stat = BachEnemyFeatValue(10, Enemy);                   // 0x10364788..0x103647d9 row 10 (0x10364792 JLE / 0x103647a1 JZ / 0x103647a6 JL, 0x103647af JNZ / 0x103647bf / 0x103647c9: inside the seam)
			const int32 Stat9 = BachEnemyFeatValue(9, Enemy);             // 0x103647f4..0x10364854 row 9 (0x103647fe JLE / 0x1036480c JZ / 0x10364811 JL, 0x1036482a JNZ / 0x1036483a / 0x10364844: inside the seam)
			if (Stat < Stat9)                                             // 0x10364859 CMP EDI,EAX; JGE / 0x1036485b
			{
				Stat = Stat9;                                             // 0x1036485d
			}
			BaseScheduleHost.WaitFinished = static_cast<double>(static_cast<float>(Stat) * GSpecies19BachAimPerStat109)
				+ BaseScheduleHost.WaitFinished;                          // 0x10364861 / 0x10364865 / 0x1036486b / 0x10364871
			BachWarningTime = BaseScheduleHost.WaitFinished;              // 0x10364877 +0x6694
			BaseScheduleHost.WaitFinished += static_cast<double>(GSpecies19BachWarningLead);   // 0x1036487d / 0x10364883 / 0x10364889
		}
		if (BaseScheduleHost.HintNode == INDEX_NONE)                     // 0x1036488f +0x5ddc; 0x10364895
		{
			RestartIdealActivityId(5);                                    // 0x1036489f
		}
		return 0;
	}
	default:
		break;
	}
	return FElysiumNpcHuman::StartTaskSlot442(Task);                     // 0x10364f59 CNPC_VHuman::StartTask
}

// =================================================================================================
// CNPC_VChangBros -- 0x1036b750, 1617 bytes (also CNPC_VChangBrosBlade's and CNPC_VChangBrosClaw's)
// =================================================================================================

static const TCHAR* const GSpecies19FileChangBros = TEXT("NPC_VChangBros.cpp");           // 0x10630ff8
// `_DAT_104ada54` -- 1.5, the energy-charge delay; `_DAT_104ada40` -- 4.0, the united delay;
// `_DAT_104ada5c` -- 50.0, the blast point's lift; `DAT_104ada58` -- 1000.0 (`0x447a0000`), the AOE
// radius. None is in the tunables table.
static constexpr float GSpecies19ChangEnergyChargeDelay = 1.5f;
static constexpr float GSpecies19ChangUnitedDelay = 4.0f;
static constexpr float GSpecies19ChangBlastLift = 50.0f;
static constexpr float GSpecies19ChangBlastRadius = 1000.0f;
// `m_bfAINPCFlags2` (+0x14bc) mask task `0x153` keeps (`AND EAX,0x7ffffffd`, `0x1036b8e2`).
static constexpr uint32 GSpecies19ChangJumpFlags2 = 0x80000002u;

void FElysiumNpcChangBros::ChangCreateEmitter(const TCHAR* Name, const FVector* PositionUnits)
{
	// SEAM for `thunk_FUN_102c41b0(this, name, &position)` (`0x10015c76`), the named particle emitter
	// the Chang arms create (null position = at this NPC). The same seam `FElysiumNpcManBat` records
	// its teleport emitters through; recorded here, in SOURCE units.
	ChangEmitterPlacements.Add(FTeleportEmitterPlacement{ FString(Name),
		PositionUnits != nullptr ? *PositionUnits : Origin / ElysiumMove::U });
}

// Slot 442: `0x1036b750`. Also carries the inherited body of CNPC_VChangBrosBlade, CNPC_VChangBrosClaw.
int32 FElysiumNpcChangBros::StartTaskSlot442(void* Task)
{
	// A `g_ScopeTraceStack` frame named `"CNPC_VChangBros::StartTask"` wraps the body, and the
	// `AddSolidFlags`/`RemoveSolidFlags` calls nest their own -- absent.
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	switch (TaskLocal)                                                   // 0x1036b7ac ADD -0x13b; JA 0x1036bd89 (0x1036b7b4; 0x1036b7c2 table jump)
	{
	case 0x13b:
		if (GetSector(Origin) == 3)                                       // 0x1036bd68 slot 217; 0x1036bd71 GetSector; 0x1036bd76 / 0x1036bd79 JNZ
		{
			LastJumpTime = Species19Now(*this);                           // 0x1036bd83 +0x66cc = curtime
		}
		break;                                                            // falls into 0x1036bd89 (the base)
	case 0x150:
		Species19MotorYawSpeedReset(*this);                               // 0x1036b7f5 0x102e0b40
		SolidFlagsWord |= 0x4u;                                           // 0x1036b85e OR 4; 0x1036b862 (AddSolidFlags) (its scope name test 0x1036b802 JNZ)
		// `0x1036b873` Relink: no spatial partition.
		RestartIdealActivityId(0x1148);                                   // 0x1036b87f
		// FALLS THROUGH into 0x151.
		[[fallthrough]];
	case 0x151:
		Species19MotorYawSpeedReset(*this);                               // 0x1036b88a 0x102e0b40
		ChangCreateEmitter(TEXT("chang_teleport_in_emitter"), nullptr);   // 0x1036b895 / 0x1036b89c
		RecordHealthPercent();                                            // 0x1036b8a3 0x103c6a00
		FacingTime = Species19Now(*this);                                 // 0x1036b8b1 +0x66d0 = curtime
		return 0;
	case 0x152:
	{
		BaseScheduleHost.HintNode = SelectTeleportNodeChang();            // 0x1036b7cb 0x1000e35e; 0x1036b7d4 +0x5ddc
		TaskComplete(false);                                              // 0x1036b7da
		return 0;
	}
	case 0x153:
		Species19MotorYawSpeedReset(*this);                               // 0x1036b8cc 0x102e0b40
		NavSetType(0);                                                    // 0x1036b8d5 0x1027d9b0(NAV_GROUND)
		bJumping = false;                                                 // 0x1036b8e7 +0x6498 = 0
		NpcFlags.ClearRawWord2Bits(GSpecies19ChangJumpFlags2);            // 0x1036b8e2 / 0x1036b8ee +0x14bc
		CommitSetupJump();                                                // 0x1036b8f4 0x102c4e80
		ChangCreateEmitter(TEXT("chang_teleport_out_emitter"), nullptr);  // 0x1036b8ff / 0x1036b906
		RestartIdealActivityId(0x1149);                                   // 0x1036b912
		return 0;
	case 0x154:
		// `SelectLedgeNode`; null stamps line 0x1aa and `TaskFail(1)`, and BOTH paths complete.
		BaseScheduleHost.HintNode = ChangBrosSelectLedgeNode();           // 0x1036b929 0x10012f67; 0x1036b930
		if (BaseScheduleHost.HintNode == INDEX_NONE)                     // 0x1036b936
		{
			Species19Fail(*this, GSpecies19FileChangBros, 0x1aa, 1);      // 0x1036b93e / 0x1036b948 / 0x1036b952
		}
		TaskComplete(false);                                              // 0x1036b95c
		return 0;
	case 0x155:
		// `SetupSuperJump(m_pHintNode)`: the hint POINTER is pushed where the body tests a float against
		// 0.0, so a non-null hint enables it.
		SetupSuperJump(BaseScheduleHost.HintNode != INDEX_NONE ? 1.f : 0.f);   // 0x1036b971 +0x5ddc; 0x1036b97a 0x100119e1
		TaskComplete(false);                                              // 0x1036b983
		return 0;
	case 0x156:
	{
		if (FElysiumEntity* Closest = World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr)   // 0x1036b998..0x1036b9c4 (0x1036b9a1 JZ / 0x1036b9be JNZ)
		{
			Species19MotorYawSpeedReset(*this);                           // 0x1036b9cc 0x102e0b40
			Species19MotorIdealYawTo(*this, Closest->Origin);             // 0x1036b9dd slot 217; 0x1036b9e6 0x102e2020
		}
		RestartIdealActivityId(0x114c);                                   // 0x1036b9f2
		ChangEnergyChargeTime = Species19Now(*this) + static_cast<double>(GSpecies19ChangEnergyChargeDelay);   // 0x1036b9fd / 0x1036ba03 / 0x1036ba08 +0x66f0
		ClearBodyEmitterNames();                                          // 0x1036ba0e 0x1000312f
		SetBodyEmitterName(0, TEXT("chang_ball_charge_emitter"));         // 0x1036ba1c
		SetBodyEmitterName(1, TEXT("chang_ball_charge_emitter"));         // 0x1036ba2a
		VampireBossSpawnBodyEmitters();                                   // 0x1036ba31 0x1001177f
		return 0;
	}
	case 0x157:
		bChangEnergyBallSpawned = false;                                  // 0x1036ba4d +0x66d8 = 0
		RestartIdealActivityId(0x114a);                                   // 0x1036ba54
		LastAttackTime = Species19Now(*this);                             // 0x1036ba63 +0x5d9c = curtime
		return 0;
	case 0x158:
	{
		// `CheckJumpPathToHintNode(m_pHintNode)`: true completes, false stamps line 0x1ce and fails 1.
		// Retail passes the hint pointer unchecked; a hint the port cannot resolve is refused (crash
		// guard).
		FHintWords Hint;
		if (HintWords(BaseScheduleHost.HintNode, Hint) && CheckJumpPathToHintNode(Hint))   // 0x1036ba78 / 0x1036ba81 0x10006d70; 0x1036ba86 / 0x1036ba8a JZ
		{
			TaskComplete(false);                                          // 0x1036ba8e
			return 0;
		}
		Species19Fail(*this, GSpecies19FileChangBros, 0x1ce, 1);          // 0x1036baa7 / 0x1036bab1 / 0x1036babb
		return 0;
	}
	case 0x159:
	{
		const FElysiumEntity* United = SelectUnitedNode();                // 0x1036bad3 0x1001043d
		BaseScheduleHost.HintNode = United != nullptr ? United->Handle.Index : INDEX_NONE;   // 0x1036badc +0x5ddc
		TaskComplete(false);                                              // 0x1036bae2
		return 0;
	}
	case 0x15a:
		return 0;                                                         // jump table -> 0x1036bd91: nothing
	case 0x15b:
		SolidFlagsWord |= 0x4u;                                           // 0x1036bb63 OR AL,4; 0x1036bb66 (AddSolidFlags) (its scope's this/name tests 0x1036baf9 JZ / 0x1036bb03 JNZ)
		RestartIdealActivityId(0x114b);                                   // 0x1036bb79
		if (bChangCenterStored)                                           // 0x1036bb7e +0x66e8 / 0x1036bb86 JZ
		{
			Species19MotorYawSpeedReset(*this);                           // 0x1036bb8e 0x102e0b40
			Species19MotorIdealYawTo(*this, ChangArenaCenter);            // 0x1036bb93 +0x66dc; 0x1036bba2 0x102e2020
		}
		ChangUnitedTime = Species19Now(*this) + static_cast<double>(GSpecies19ChangUnitedDelay);   // 0x1036bbae / 0x1036bbb4 / 0x1036bbb7 +0x66d4
		return 0;
	case 0x15c:
		RestartIdealActivityId(0x114c);                                   // 0x1036bbd3
		return 0;
	case 0x15d:
	{
		Species19MotorYawSpeedReset(*this);                               // 0x1036bbee 0x102e0b40
		ChangLastUnitedAttackTime = Species19Now(*this);                  // 0x1036bc00 +0x66ec = curtime
		RestartIdealActivityId(0x114d);                                   // 0x1036bc08
		SolidFlagsWord &= ~0x4u;                                          // 0x1036bc71 AND 0xfffffffb; 0x1036bc75 (RemoveSolidFlags) (its scope name test 0x1036bc15 JNZ)
		if (ChangType != 0)                                               // 0x1036bc84 +0x66b8; 0x1036bc8c JNZ
		{
			return 0;
		}
		// The blast point: the arena centre lifted by 50 (Source units, as `m_vArenaCenter` is read).
		FVector BlastUnits = ChangArenaCenter / ElysiumMove::U;
		BlastUnits.Z += GSpecies19ChangBlastLift;                         // 0x1036bc9a FLD [0x104ada5c]; 0x1036bcc2 FADD
		ChangCreateEmitter(TEXT("chang_blast_emitter"), &BlastUnits);     // 0x1036bcb0 / 0x1036bcca
		FElysiumEntity* Closest = World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;   // 0x1036bccf..0x1036bd03 (0x1036bcd8 JZ / 0x1036bcf9 JNZ)
		if (Closest == nullptr || GetSector(Closest->Origin) == 4)       // 0x1036bd0b slot 217; 0x1036bd14 GetSector; 0x1036bd19 / 0x1036bd1c JZ
		{
			return 0;
		}
		if (Species19CharTemplate(*this) < 0)                                  // 0x1036bd20 GetCharTemplate; 0x1036bd27 JL
		{
			return 0;
		}
		// `CausePlayerAOEDamage(&point, 1000.0, template->+0xcc)`. The port's body takes (centre,
		// radius); the third word, the template record's `+0xcc`, has no parameter there.
		CausePlayerAOEDamage(BlastUnits, GSpecies19ChangBlastRadius);     // 0x1036bd2f 0x10738d10 row; 0x1036bd34 +0xcc; 0x1036bd4f 0x10013b24
		return 0;
	}
	default:
		break;
	}
	return FElysiumNpcVampireBoss::StartTaskSlot442(Task);               // 0x1036bd8c 0x1000de7c
}

// =================================================================================================
// CNPC_VGargoyle -- 0x103790d0, 615 bytes
// =================================================================================================

// The gargoyle's three exerts, `0x10639490`, the table slot 618 `0x1037a100` indexes.
static const TCHAR* const GSpecies19GargoyleExerts[3] = {
	TEXT("character/monster/gargoyle/exert_heavy_1.wav"),
	TEXT("character/monster/gargoyle/exert_heavy_2.wav"),
	TEXT("character/monster/gargoyle/exert_heavy_3.wav"),
};

// `_DAT_1046bad0` -- 400.0, the gib impulse's forward scale (not in the tunables table).
static constexpr float GSpecies19GargoyleGibForwardScale = 400.0f;

void FElysiumNpcGargoyle::TransformationStartSlot618()
{
	// `0x1037a100`, slot 618 on `CNPC_VGargoyle`: the exertion sound.
	Species19EmitExertion(*this, GSpecies19GargoyleExerts);
}

void FElysiumNpcGargoyle::GargoyleGibImpulse(const FVector& VelocityUnits, const FVector& AngularUnits)
{
	// SEAM for `m_pPhysicsObject->vtable[+0xa4](&velocity, &angular)` (`IPhysicsObject` slot 41,
	// `0x1037928c`). The port stands no physics object for a live NPC (`bHasPhysicsObject`, family
	// Senses10, is the "is one standing" answer); nothing receives the velocity.
	(void)VelocityUnits;
	(void)AngularUnits;
}

// Slot 442: `0x103790d0`.
int32 FElysiumNpcGargoyle::StartTaskSlot442(void* Task)
{
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	if (TaskLocal > 0xea)                                                // 0x103790de CMP 0xea; JG 0x103792b2 (0x103790e3)
	{
		if (TaskLocal == 0x12f)                                          // 0x103792b2 SUB 0x12f; JZ 0x10379325 (0x103792b7)
		{
			RestartIdealActivityId(0x5e);                                 // 0x10379329; no completion
			return 0;
		}
		if (TaskLocal == 0x130)                                          // 0x103792b9 DEC; JZ 0x103792cd (0x103792ba)
		{
			// `m_hPillarTarget` (+0x667c) through the entity list twice (`0x10009c8c`); a missing
			// target returns with nothing written and the task running.
			FElysiumEntity* Pillar = World != nullptr ? World->Resolve(GargoylePillarTarget) : nullptr;   // 0x103792da / 0x103792ea (the second lookup)
			if (Pillar == nullptr)                                        // 0x103792e1 JZ 0x1037932e
			{
				return 0;
			}
			SavePosition = Pillar->Origin;                                // 0x103792f3 slot 220; 0x103792fd..0x10379311
			TaskComplete(false);                                          // 0x10379317
			return 0;
		}
		return FElysiumNpcHuman::StartTaskSlot442(Task);                 // 0x103792bf
	}
	if (TaskLocal >= 0xe9)                                               // 0x103790ee JGE 0x103791cd
	{
		// The gib death: `m_iDoingGibDeath` (+0x6684) = 1 FIRST; then, only with a physics object,
		// the impulse (`GetVelocity() + forward * 400 + (0, 0, 100)`, angular zero) and
		// `m_OnGibDeath.FireOutput(this, this, 0)`; then the human base.
		GargoyleDoingGibDeath = 1;                                        // 0x103791d3
		if (bHasPhysicsObject)                                            // 0x103791cd +0x36c; 0x103791df
		{
			// Slot 221 `GetAngles` into `AngleVectors` (`0x10002310`, forward only); slot 199
			// `GetVelocity`. Source convention: X pitch, Y yaw.
			const float Pitch = FMath::DegreesToRadians(static_cast<float>(Angles.X));
			const float Yaw = FMath::DegreesToRadians(static_cast<float>(Angles.Y));
			const FVector ForwardDir(FMath::Cos(Pitch) * FMath::Cos(Yaw), FMath::Cos(Pitch) * FMath::Sin(Yaw),
				-FMath::Sin(Pitch));                                      // 0x1037920a / 0x10379211
			FVector Impulse = Velocity / ElysiumMove::U;                  // 0x10379224 slot 199
			Impulse.X += ForwardDir.X * GSpecies19GargoyleGibForwardScale;   // 0x1037922e / 0x10379268
			Impulse.Y += ForwardDir.Y * GSpecies19GargoyleGibForwardScale;   // 0x1037924a / 0x10379274
			Impulse.Z += ForwardDir.Z * GSpecies19GargoyleGibForwardScale + ElysiumNpcTunables::Hundred;   // 0x10379258 / 0x1037925e / 0x10379280
			GargoyleGibImpulse(Impulse, FVector::ZeroVector);             // 0x1037928c vtable +0xa4
			FireOutput(FName(TEXT("OnGibDeath")), Handle);                // 0x1037929c 0x100cd660(+0x6664, this, this, 0)
		}
		return FElysiumNpcHuman::StartTaskSlot442(Task);                 // 0x103792a4
	}
	if (TaskLocal == 0x31)                                               // 0x103790f4 JZ 0x10379124 (0x103790f7)
	{
		// The pillar approach: a live `m_hPillarTarget` resets the motor, sets the ideal yaw at the
		// pillar's slot 220 `GetOrigin` and calls slot 572 `SetTurnActivity`; on BOTH paths the task
		// completes only once `FacingIdeal` already answers true.
		if (FElysiumEntity* Pillar = World != nullptr ? World->Resolve(GargoylePillarTarget) : nullptr)   // 0x10379124..0x1037914f (0x1037912d JZ / 0x1037914a JNZ; the re-resolve 0x10379165 JZ / 0x10379182 JNZ)
		{
			Species19MotorYawSpeedReset(*this);                           // 0x10379157 0x102e0b40
			Species19MotorIdealYawTo(*this, Pillar->Origin);              // 0x10379194 slot 220; 0x1037919d 0x102e2020
			SetTurnActivity();                                            // 0x103791a6 slot 572
		}
		if (FacingIdeal())                                                // 0x103791ae 0x10278c80; 0x103791b5
		{
			TaskComplete(false);                                          // 0x103791bf
		}
		return 0;
	}
	if (TaskLocal >= 0x36 && TaskLocal <= 0x37)                          // 0x103790f9 JLE / 0x10379102 JG (0x103790fc / 0x10379105)
	{
		TransformationStartSlot618();                                     // 0x1037910d CALL [EAX+0x9a8]
		return FElysiumNpcHuman::StartTaskSlot442(Task);                 // 0x10379116
	}
	return FElysiumNpcHuman::StartTaskSlot442(Task);                     // 0x103792bf
}

// =================================================================================================
// CNPC_VHengeyokai -- 0x103805d0, 1055 bytes
// =================================================================================================

static const TCHAR* const GSpecies19FileHengeyokai = TEXT("NPC_VHengeyokai.cpp");         // 0x1063f48c

// The hengeyokai's three exerts, `0x1063bda4`, the table slot 618 `0x10381960` indexes.
static const TCHAR* const GSpecies19HengeyokaiExerts[3] = {
	TEXT("character/monster/hengeyokai/exert_heavy_1.wav"),
	TEXT("character/monster/hengeyokai/exert_heavy_2.wav"),
	TEXT("character/monster/hengeyokai/exert_heavy_3.wav"),
};

// `m_bfAINPCFlags2` (+0x14bc) bits task `0x14e` raises (`OR ECX,0x80000800`, `0x103809d4`).
static constexpr uint32 GSpecies19HengeyokaiFlags2 = 0x80000800u;

void FElysiumNpcHengeyokai::TransformationStartSlot618()
{
	// `0x10381960`, slot 618 on `CNPC_VHengeyokai`: the exertion sound.
	Species19EmitExertion(*this, GSpecies19HengeyokaiExerts);
}

void FElysiumNpcHengeyokai::FUN_103828a0()
{
	// SEAM for `0x103828a0`, the carried-body drop (family Spawn19's `Event_Killed` `0x10380390` runs
	// it too). Not this lane's row; counted.
	++Fun103828a0Calls;
}

void FElysiumNpcHengeyokai::FUN_103831c0()
{
	// SEAM for `0x103831c0`, the shark-form spawn: `SetIdealActivity(0x12a)`, the render/hull reset, a
	// `npc_VHengeyokai` proxy created, spawned, re-modelled and put on schedule `0x16f`. Not this
	// lane's row; counted.
	++Fun103831c0Calls;
}

void FElysiumNpcHengeyokai::FUN_10382bb0()
{
	// SEAM for `0x10382bb0`, the carry facing: with a live `m_hPickupTarget` it seeds the motor yaw
	// from `GetAngles().y` at speed 20.0 and then yaws at the enemy (or holds the angles). Not this
	// lane's row; counted.
	++Fun10382bb0Calls;
}

// Slot 442: `0x103805d0`.
int32 FElysiumNpcHengeyokai::StartTaskSlot442(void* Task)
{
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	if (TaskLocal > 0x134)                                               // 0x103805dd CMP 0x134; JG 0x103808d8 (0x103805e2)
	{
		switch (TaskLocal)                                               // 0x103808d8 SUB 0x135; JA 0x103808b4 (0x103808e0; 0x103808ea table jump)
		{
		case 0x135:
			FUN_103828a0();                                               // 0x103808f3 0x1000df6c
			TaskComplete(false);                                          // 0x103808fc
			return 0;
		case 0x136:
			// `0x10383130`: `SetSkinFadeTime(0)`, `FadeToSkin(0)`, the `Hengeyokai_freeze_emitter`; the
			// same body `TranslateScheduleRetail` counts as a thaw.
			++HengeyokaiThawCalls;                                        // 0x1038090b 0x1000ed6d
			TaskComplete(false);                                          // 0x10380914
			return 0;
		case 0x14a:
			FUN_103831c0();                                               // 0x10380923 0x1000bdf7
			TaskComplete(false);                                          // 0x1038092c
			return 0;
		case 0x14b:
			return 0;                                                     // 0x103808bc: nothing, not even TaskComplete
		case 0x14c:
		{
			// The model swap.
			TCHAR ModelName[] = TEXT("models/character/monster/Hengeyokai/hengeyokai.mdl");   // 0x1063f444
			SetModel(ModelName);                                          // 0x10380942 slot 105
			EffectsWord |= 0x10u;                                         // 0x10380950 m_fEffects |= 0x10
			// `m_nRenderFX` (+0x168) = 0, `m_nRenderMode` (+0x16c) = 0 (`0x1038095a` / `0x10380964`):
			// render words the kernel tier does not carry -- visual only.
			HullKind = HullIndexHengeyokai;                               // 0x1038096e m_eHull = 0x12
			SetHullSizeNormal(true);                                      // 0x10380978 0x10273070
			// `0x1038097e` `Relink` (`0x101cf600`): no spatial partition.
			TaskComplete(false);                                          // 0x1038098a
			return 0;
		}
		case 0x14d:
			ThinkSet(TEXT("0x101c0b10"), 0.0);                            // 0x1038099b PUSH 0x10015b68; 0x103809a2
			NextThink = static_cast<float>(Species19Now(*this) + static_cast<double>(ElysiumNpcTunables::Hundredth));   // 0x103809b0..0x103809b9 +0x17c
			TaskComplete(false);                                          // 0x103809bf
			return 0;
		case 0x14e:
			NpcFlags.SetRawWord2Bits(GSpecies19HengeyokaiFlags2);         // 0x103809cc..0x103809da +0x14bc
			TaskComplete(false);                                          // 0x103809e2
			return 0;
		default:
			return FElysiumNpcHuman::StartTaskSlot442(Task);             // 0x103808b7
		}
	}
	if (TaskLocal == 0x134)                                              // 0x103805e8 JZ 0x103808c4
	{
		RestartIdealActivityId(0x127);                                    // 0x103808cb; no completion
		return 0;
	}
	switch (TaskLocal)                                                   // 0x103805ee SUB 0x36; JA 0x103808b4 (0x103805f6; 0x10380604 table jump)
	{
	case 0x36:
	case 0x37:
		TransformationStartSlot618();                                     // 0x103808ae CALL [EAX+0x9a8]
		return FElysiumNpcHuman::StartTaskSlot442(Task);                 // 0x103808b7 (fall through)
	case 0xc8:
	{
		// Face the live pickup target; complete only once `FacingIdeal`.
		if (FElysiumEntity* PickupEntity = World != nullptr ? World->Resolve(HengeyokaiPickupTarget) : nullptr)   // 0x10380710..0x1038073b (0x10380719 JZ / 0x10380736 JNZ; the re-resolve 0x10380751 JZ / 0x1038076e JNZ)
		{
			Species19MotorYawSpeedReset(*this);                           // 0x10380743 0x102e0b40
			Species19MotorIdealYawTo(*this, PickupEntity->Origin);              // 0x10380780 slot 220; 0x10380789 0x102e2020
			SetTurnActivity();                                            // 0x10380792 slot 572
		}
		if (FacingIdeal())                                                // 0x1038079a 0x10278c80 / 0x103807a1 JZ
		{
			TaskComplete(false);                                          // 0x103807ab
		}
		return 0;
	}
	case 0xc9:
	{
		// The enemy (slot 167) and its remembered position; `COND 0x1b` AND the facing gate complete;
		// otherwise face the position, `SetTurnActivity`, and arm `m_flTaskFailTimer = curtime + 1.0`.
		FElysiumEntity* Enemy = static_cast<const FElysiumNpc*>(this)->GetEnemy();   // 0x103807bc slot 167
		FVector EnemyLkp = FVector::ZeroVector;
		EnemyLkp = Conditions19LastKnownPosition(Enemy);                                 // 0x103807ce slot 541; 0x103807d6 0x102dfed0
		if (Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(0x1b))  // 0x103807df HasCondition(0x1b)
			&& FUN_103822a0(Enemy))                                       // 0x103807eb 0x103822a0 (0x103807e6 JZ / 0x103807f2 JZ)
		{
			TaskComplete(false);                                          // 0x103807f8
			return 0;
		}
		Species19MotorYawSpeedReset(*this);                               // 0x1038080b 0x102e0b40
		Species19MotorIdealYawTo(*this, EnemyLkp);                        // 0x1038081d 0x102e2020
		SetTurnActivity();                                                // 0x10380826 slot 572
		HengeyokaiTaskFailTimer = Species19Now(*this) + static_cast<double>(ElysiumNpcTunables::One);   // 0x10380832 FLD curtime; 0x10380835 FADD [0x104454c0]; 0x1038083b +0x6674
		return 0;
	}
	case 0xca:
	{
		// The same over slot 168 `GetEnemy`, with no condition gate and no fail timer.
		FElysiumEntity* Enemy = GetEnemy();                               // 0x1038084c slot 168
		FVector EnemyLkp = FVector::ZeroVector;
		EnemyLkp = Conditions19LastKnownPosition(Enemy);                                 // 0x1038085e slot 541; 0x10380866 0x102dfed0
		if (FUN_103822a0(Enemy))                                          // 0x1038086e / 0x10380875 JNZ 0x103807f4
		{
			TaskComplete(false);                                          // 0x103807f8
			return 0;
		}
		Species19MotorYawSpeedReset(*this);                               // 0x10380881 0x102e0b40
		Species19MotorIdealYawTo(*this, EnemyLkp);                        // 0x10380893 0x102e2020
		SetTurnActivity();                                                // 0x1038089c slot 572
		return 0;
	}
	case 0x130:
	{
		// `m_ePathMode = 2` FIRST; a live pickup target: its slot 220 `GetOrigin` into
		// `m_vSavePosition`, `m_flGoalTolerance` into the navigator (`0x102ee1c0`, then `0x102f2fe0`),
		// `TaskComplete`. Dead: line 0x3b3, `TaskFail(1)`.
		HengeyokaiPathMode = 2;                                           // 0x10380626 +0x6680
		FElysiumEntity* PickupEntity = World != nullptr ? World->Resolve(HengeyokaiPickupTarget) : nullptr;   // 0x10380630..0x10380667 (0x10380639 JZ / 0x1038065d JNZ; the re-resolve 0x10380672 JZ / 0x10380689 JNZ)
		if (PickupEntity == nullptr)
		{
			Species19Fail(*this, GSpecies19FileHengeyokai, 0x3b3, 1);     // 0x103806ee / 0x103806f8 / 0x10380702
			return 0;
		}
		SavePosition = PickupEntity->Origin;                                    // 0x10380693 slot 220; 0x10380699..0x103806ad
		// `0x102ee1c0` (path `+0x28`) and `0x102f2fe0` (path `+0x20`) both take `m_flGoalTolerance`
		// (+0x6320, `0x103806b3` / `0x103806c5`) -- the navigator's words, not the schedule's.
		StartTask19SetNavTolerances(ScheduleHost.GoalToleranceCm / ElysiumMove::U,
			ScheduleHost.GoalToleranceCm / ElysiumMove::U);               // 0x103806c0 / 0x103806d2
		TaskComplete(false);                                              // 0x103806db
		return 0;
	}
	case 0x132:
	case 0x133:
		FUN_10382bb0();                                                   // 0x1038060d 0x1000ee8a
		RestartIdealActivityId(0x126);                                    // 0x10380619; no completion
		return 0;
	default:
		break;
	}
	return FElysiumNpcHuman::StartTaskSlot442(Task);                     // 0x103808b7 CNPC_VHuman::StartTask
}

// =================================================================================================
// CNPC_VManBat -- 0x1038c390, 2673 bytes
// =================================================================================================

// The fly-node search every flight arm makes: `0x102d1af0(this, 20000, type, 5000.0, 0, 0)`.
static constexpr int32 GSpecies19ManBatFlyHintType = 20000;           // `PUSH 0x4e20`
static constexpr float GSpecies19ManBatFlyRadius = 5000.0f;           // `PUSH 0x459c4000`
// `_DAT_10457f5c` -- 500.0, the scatter speed of task `0x14d`; `_DAT_1044e664` -- 10.0, the lift of
// task `0x15d`. Read out of the pinned image; neither is in the tunables table.
static constexpr float GSpecies19ManBatScatterSpeed = 500.0f;
static constexpr float GSpecies19ManBatLift = 10.0f;
// `0x3dcccccd` / `0x3f000000` -- the `RandomFloat(0.1, 0.5)` climb factor of task `0x14d`.
static constexpr float GSpecies19ManBatClimbMin = 0.1f;
static constexpr float GSpecies19ManBatClimbMax = 0.5f;
// The four throw rows task `0x153` picks among with `RandomInt(1, 4)`: model `0x10640ce0[i-1]`, bone
// `0x10640cf0[i-1]`, float `0x104bbaa4[i]` and attachment `0x10640cfc[i]` -- four OVERLAPPING tables,
// read out of the pinned image.
struct FSpecies19ManBatThrowRow
{
	const TCHAR* Model;
	const TCHAR* Bone;
	float Value;
	const TCHAR* Attachment;   // null where the table cell is 0
};
static const FSpecies19ManBatThrowRow GSpecies19ManBatThrowRows[4] = {
	{ TEXT("models/character/monster/manbat/Throw_Objects/ThrowTaxi.mdl"), TEXT("Bone01"), 50.0f, TEXT("Cab_Explosion") },
	{ TEXT("models/character/monster/manbat/Throw_Objects/supportb.mdl"), TEXT("Bone01"), 35.0f, nullptr },
	{ TEXT("models/character/npc/common/corpse/security_guard/sg_corpse_full.mdl"), TEXT("Bip01 R Foot"), 20.0f, nullptr },
	{ TEXT("models/character/npc/common/prostitute/prostitute_1/prostitute_1.mdl"), TEXT("Bip01 R Neck"), 20.0f, nullptr },
};

/** `0x1042fb50` then `0x103908c0` -- the ManBat's mode word as retail STORES it at `+0x6670`: the
 *  secure-type encode of the plain mode (`0x10015be0`/`0x10011509` -> `0x1042fb50`) folded through
 *  `0x103908c0` (inlined at the `0x157`/`0x159`/`0x15c`/`0x163` sites). */
static uint32 Species19ManBatScrambleMode(uint32 Mode)
{
	const uint32 A = Mode ^ 0xea3e269cu;
	const uint32 Encoded = A ^ (((((A & 0x67c8c535u) ^ 0xdcb8cc14u) + 0x18e71cecu) ^ 0x82aa05e1u) & 0x98373acau);   // 0x1042fb50
	return ((((Encoded & 0x00710935u) ^ 0x0034871du) + 0x004094abu) & 0x018ef6cau) ^ Encoded ^ 0x412a96ecu;   // 0x103908c0
}

/** `FUN_100f7b20` -- the nearest live entity NAMED `Name` (`FindEntityByName` `0x100f7770`, `+0x2e0`
 *  edict set) to `PointCm`; a zero radius means unbounded (`3.2212255e+09` squared), strictly-closer
 *  wins, first-listed on a tie. */
static FElysiumEntity* Species19FindNearestNamed(FElysiumEntityWorld& World, const TCHAR* Name,
	const FVector& PointCm)
{
	FElysiumEntity* Best = nullptr;
	double BestDistSq = TNumericLimits<double>::Max();
	for (const TUniquePtr<FElysiumEntity>& Entity : World.Entities())
	{
		if (!Entity.IsValid() || Entity->IsDead() || Entity->TargetName != Name)
		{
			continue;
		}
		const double DistSq = FVector::DistSquared(Entity->Origin, PointCm);
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Entity.Get();
		}
	}
	return Best;
}

void FElysiumNpcManBat::ManBatSetMode(int32 Mode)
{
	// `m_iMoveGoalNodeMode` is a secure word: `+0x6670` holds the encoded value (the two bytes at
	// `+0x666c`/`+0x666d` are the secure temp's own, stack garbage in retail -- absent). The port also
	// carries the DECODED mode (`ManBatMoveGoalNodeMode`, family Bosses), so both are written.
	ManBatHintModeWord = Species19ManBatScrambleMode(static_cast<uint32>(Mode));
	ManBatMoveGoalNodeMode = Mode;
}

void FElysiumNpcManBat::ManBatSetMoveGoalNodeId(int32 NodeId)
{
	// `m_iMoveGoalNodeID` (`+0x6674`). The port carries this ONE retail word twice
	// (`ManBatMoveGoalNodeId`, family Bosses, and `ManBatHintIndex`, family Hints10's reader); both
	// are written so neither reader goes stale.
	ManBatMoveGoalNodeId = NodeId;
	ManBatHintIndex = NodeId;
}

FElysiumEntity* FElysiumNpcManBat::ManBatFindFlyNode(int32 HintType)
{
	// `0x102d1af0(this, 20000, type, 5000.0, 0, 0)`: the ManBat's entity-answering form of the hint
	// search (family Bosses' seam, which answers null). The answer lands in `m_pFlyNode` (+0x6688).
	FElysiumEntity* Node = ManBatFindMoveGoalHint(HintType, GSpecies19ManBatFlyRadius);
	(void)GSpecies19ManBatFlyHintType;
	ManBatFlyNode = Node != nullptr ? Node->Handle : FElysiumEntityHandle::Invalid();
	return Node;
}

void FElysiumNpcManBat::ManBatSetAbsVelocityUnits(const FVector& VelocityUnits)
{
	// `CBaseEntity::SetAbsVelocity` (`0x1000489a`): the port's velocity is cm/s.
	Velocity = VelocityUnits * ElysiumMove::U;
}

void FElysiumNpcManBat::FUN_1038e720(const FVector& VelocityUnits)
{
	// SEAM for `0x1038e720`, the flap selector: unless `m_Activity` is one of `0x28/0x30/0xb0/0x4b/
	// 0x1171`, a climb at or above `_DAT_104492a8` takes `0x1038e640`, otherwise the yaw of the
	// velocity against `GetAngles().y` picks `0x1038e6a0`/`0x1038e6e0`, else `0x1038e640`/`0x1038e670`.
	// Not this lane's row; recorded.
	ManBatFlapSelectorCalls.Add(VelocityUnits);
}

void FElysiumNpcManBat::FUN_1038c250(FElysiumEntity* FlyNode)
{
	// SEAM for `0x1038c250`: `0x1038c170(1)`, level the pitch, then a velocity toward the fly node.
	// Not this lane's row; counted.
	(void)FlyNode;
	++Fun1038c250Calls;
}

void FElysiumNpcManBat::FUN_1038f660()
{
	// SEAM for `0x1038f660`, the carried-object release (`m_hPickupTarget`, `m_hPhysicsAnimlink`).
	// Not this lane's row; counted.
	++Fun1038f660Calls;
}

void FElysiumNpcManBat::FUN_1038c170(int32 Mode)
{
	// SEAM for `0x1038c170(mode)`, the flight/ground capability switch (`FL_FLY 0x400`, nav type,
	// capabilities 1/4, slot 93). Not this lane's row; recorded.
	ManBatFlightSwitchCalls.Add(Mode);
}

void FElysiumNpcManBat::FUN_1038fc80()
{
	// SEAM for `0x1038fc80` (task `0x15e`). Not this lane's row; counted.
	++Fun1038fc80Calls;
}

void FElysiumNpcManBat::FUN_1038fd40()
{
	// SEAM for `0x1038fd40` (task `0x15f`). Not this lane's row; counted.
	++Fun1038fd40Calls;
}

// Slot 442: `0x1038c390`.
int32 FElysiumNpcManBat::StartTaskSlot442(void* Task)
{
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	FElysiumNpcManBat& Self = *this;
	auto FlapFirst = [&Self]()
	{
		// `0x1038e640`: `SetIdealActivity(0x22)` then `m_flFlapTimer = curtime + 2.3` (family Bosses'
		// row table).
		if (const FFlapActivity* Row = FlapActivityOf(TEXT("0x1038e640")))
		{
			Self.SetFlapActivity(Row->Activity, Row->Seconds);
		}
	};
	auto SteerAndFlap = [this]()
	{
		// `0x1038b370(this, &velocity, 0.0)` then `0x1038e720(velocity)`.
		FVector VelocityUnits = FVector::ZeroVector;
		FUN_1038b370(0.f, VelocityUnits);
		FUN_1038e720(VelocityUnits);
	};
	switch (TaskLocal)                                                   // 0x1038c39e ADD -0x14a; JA 0x1038cdf0 (0x1038c3a6; 0x1038c3ae table jump)
	{
	case 0x14a:
		Species19MotorYawSpeedReset(*this);                               // 0x1038c3bb 0x102e0b40
		SetIdealActivity(0x28);                                           // 0x1038c3c4
		if (FElysiumEntity* Node = World != nullptr ? World->Resolve(ManBatFlyNode) : nullptr)   // 0x1038c3c9 +0x6688 / 0x1038c3d1 JZ
		{
			FUN_1038c250(Node);                                           // 0x1038c3da 0x10009db3
		}
		return 0;
	case 0x14b:
		FlapFirst();                                                      // 0x1038c3ea 0x10012e4f
		SteerAndFlap();                                                   // 0x1038c3f8 0x1000b36b; 0x1038c418 0x100155cd
		return 0;
	case 0x14c:
	{
		FUN_1038f660();                                                   // 0x1038c428 0x10005ca9
		FUN_1038c170(2);                                                  // 0x1038c431 0x100158f7
		ManBatSetMode(5);                                                 // 0x1038c436..0x1038c462 (0x1038c438 0x10015be0 / 0x1038c445 0x10009e58)
		SetIdealActivity(0x2f);                                           // 0x1038c468
		FVector VelocityUnits = AbsVelocityUnits();                       // 0x1038c471 slot 198
		// Retail reads `m_pFlyNode` unchecked here; a node the port cannot resolve aims at nothing
		// (crash guard).
		if (FElysiumEntity* Node = World != nullptr ? World->Resolve(ManBatFlyNode) : nullptr)
		{
			SolveThrowImpulse(Origin / ElysiumMove::U, Node->Origin / ElysiumMove::U, VelocityUnits);   // 0x1038c493 / 0x1038c49e slot 217; 0x1038c4ac 0x102c4cc0
		}
		ManBatSetAbsVelocityUnits(VelocityUnits);                         // 0x1038c4b8 SetAbsVelocity
		return 0;
	}
	case 0x14d:
	{
		// A random scatter: yaw `RandomInt(-180, 180)` quantized to the 16-bit angle, speed 500, and a
		// climb of `RandomFloat(0.1, 0.5) * 500`; `SetAbsVelocity` then the flap selector.
		Species19MotorYawSpeedReset(*this);                               // 0x1038c795
		FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
		const int32 Draw = Stream.RandRange(-180, 180);                   // 0x1038c7ac (*DAT_1070b244)+8
		const int32 Quantum = static_cast<int32>(static_cast<float>(Draw) * ElysiumNpcTunables::AngleQuantumInverse) & 0xffff;   // 0x1038c7b7 FMUL; 0x1038c7bd __ftol; 0x1038c7c2 AND 0xffff
		const float Yaw = static_cast<float>(Quantum) * ElysiumNpcTunables::AngleQuantum;   // 0x1038c7e1 FMUL [0x1044ffdc]
		const float Climb = Stream.FRandRange(GSpecies19ManBatClimbMin, GSpecies19ManBatClimbMax);   // 0x1038c7eb (*DAT_1070b244)+4
		const float Radians = FMath::DegreesToRadians(Yaw);               // 0x1038c7f2 FMUL [0x1044eb08]
		const FVector VelocityUnits(FMath::Cos(Radians) * GSpecies19ManBatScatterSpeed,
			FMath::Sin(Radians) * GSpecies19ManBatScatterSpeed,
			Climb * GSpecies19ManBatScatterSpeed);                        // 0x1038c801 FCOS; 0x1038c805 FSIN; 0x1038c80b..0x1038c829
		ManBatSetAbsVelocityUnits(VelocityUnits);                         // 0x1038c82d
		FUN_1038e720(VelocityUnits);                                      // 0x1038c84d
		return 0;
	}
	case 0x14e:
		ManBatSetMode(1);                                                 // 0x1038c586..0x1038c5bf (0x1038c588 / 0x1038c595)
		if (ManBatFindFlyNode(2) != nullptr)                              // 0x1038c5c5 (20000, 2, 5000.0); 0x1038c5d7
		{
			bManBatReachedMoveGoal = false;                               // 0x1038c5da +0x6664 = 0
			TaskComplete(false);                                          // 0x1038c5e0
			return 0;
		}
		Species19FailBare(*this, 4);                                      // 0x1038c5f2
		return 0;
	case 0x14f:
		if (!ManBatFlyNode.IsSet())                                       // 0x1038c4c6 / 0x1038c4d0
		{
			ManBatSetMode(0);                                             // 0x1038c4d2..0x1038c507 (0x1038c4d3 / 0x1038c4e0)
			if (ManBatFindFlyNode(0) == nullptr)                          // 0x1038c50d; 0x1038c51d
			{
				ManBatSetMoveGoalNodeId(1);                               // 0x1038c52d +0x6674 = 1
				ManBatFindFlyNode(0);                                     // 0x1038c537
			}
			if (!ManBatFlyNode.IsSet())                                   // 0x1038c545 / 0x1038c54b
			{
				Species19FailBare(*this, 4);                              // 0x1038c577
				return 0;
			}
		}
		bManBatReachedMoveGoal = false;                                   // 0x1038c555
		ManBatSetMoveGoalNodeId(ManBatMoveGoalNodeId + 1);                // 0x1038c54d / 0x1038c554 / 0x1038c55b
		TaskComplete(false);                                              // 0x1038c563
		return 0;
	case 0x150:
		SetIdealActivity(0x30);                                           // 0x1038c85f
		ManBatSetAbsVelocityUnits(FVector::ZeroVector);                   // 0x1038c868..0x1038c883
		return 0;
	case 0x151:
		ManBatSetMode(2);                                                 // 0x1038c601..0x1038c629 (0x1038c603 / 0x1038c610)
		ManBatSetMoveGoalNodeId(ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(1, 3));   // 0x1038c63b RandomInt(1, 3); 0x1038c640
		if (ManBatFindFlyNode(0) != nullptr)                              // 0x1038c654; 0x1038c666
		{
			bManBatReachedMoveGoal = false;                               // 0x1038c66d
			TaskComplete(false);                                          // 0x1038c673
			return 0;
		}
		Species19FailBare(*this, 4);                                      // 0x1038c780
		return 0;
	case 0x152:
		ManBatSetMode(3);                                                 // 0x1038c681..0x1038c6b9 (0x1038c683 / 0x1038c690)
		if (ManBatFindFlyNode(0) != nullptr)                              // 0x1038c6bf; 0x1038c6d1
		{
			bManBatReachedMoveGoal = false;                               // 0x1038c6dd
			SteerAndFlap();                                               // 0x1038c6e3 / 0x1038c703
			return 0;
		}
		Species19FailBare(*this, 4);                                      // 0x1038c780
		return 0;
	case 0x153:
	{
		// `RandomInt(1, 4)` picks one of the four overlapping rows; `0x1038f2c0` throws it. The port's
		// `ThrowModel` takes (model, parent) and carries neither the bone nor the float; the fourth
		// cell (the attachment) is the one it names as the parent.
		const int32 Row = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(1, 4);   // 0x1038c91d
		const FSpecies19ManBatThrowRow& Throw = GSpecies19ManBatThrowRows[Row - 1];   // 0x1038c920..0x1038c938
		if (ThrowModel(FString(Throw.Model), Throw.Attachment != nullptr ? FString(Throw.Attachment) : FString()))   // 0x1038c943 0x10004264 / 0x1038c94c JZ
		{
			TaskComplete(false);                                          // 0x1038c950
			return 0;
		}
		Species19FailBare(*this, 1);                                      // 0x1038c962
		return 0;
	}
	case 0x154:
	{
		int32 NodeId = 0;
		do
		{
			NodeId = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(1, 3);   // 0x1038c89d
		}
		while (NodeId == ManBatMoveGoalNodeId);                           // 0x1038c8a0 / 0x1038c8a6
		ManBatSetMoveGoalNodeId(NodeId);                                  // 0x1038c8aa
		if (FElysiumEntity* Node = ManBatFindFlyNode(0))                  // 0x1038c8be; 0x1038c8ce
		{
			MotorSetOriginToTraceEnd(Node->Origin / ElysiumMove::U);      // 0x1038c8d5 slot 217; 0x1038c8dd 0x100043cc -> UTIL_SetOrigin 0x101cf5c0
			ManBatFlyNode = FElysiumEntityHandle::Invalid();              // 0x1038c8e7 +0x6688 = 0
			TaskComplete(false);                                          // 0x1038c8ee
			return 0;
		}
		Species19FailBare(*this, 4);                                      // 0x1038c902
		return 0;
	}
	case 0x155:
		ManBatSetMode(4);                                                 // 0x1038c711..0x1038c749 (0x1038c713 / 0x1038c720)
		if (ManBatFindFlyNode(0) != nullptr)                              // 0x1038c74f; 0x1038c761
		{
			bManBatReachedMoveGoal = false;                               // 0x1038c768
			SetIdealActivity(0x116f);                                     // 0x1038c76e
			return 0;
		}
		Species19FailBare(*this, 4);                                      // 0x1038c780
		return 0;
	case 0x156:
	{
		// Face the closest player: pitch 0, yaw `VecToYaw(player - self)`, roll kept (slot 64).
		// Retail reads the player unchecked; with none the port keeps the angles (crash guard).
		FElysiumEntity* Closest = World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;   // 0x1038c971..0x1038c99b (0x1038c97a JZ / 0x1038c995 JNZ)
		if (Closest != nullptr)
		{
			const FVector Delta = Closest->Origin - Origin;               // 0x1038c9a1 / 0x1038c9ad slot 217; 0x1038c9b3..0x1038c9c0
			const float Yaw = NpcKernelFacingShared::RetailYawOf(Delta, static_cast<float>(Angles.Y));   // 0x1038c9fc 0x101d2c70
			SetAngles(FRotator(0.f, Yaw, static_cast<float>(Angles.Z)));   // 0x1038c9dd slot 221; 0x1038ca11 pitch 0; 0x1038ca19 slot 64
		}
		SetIdealActivity(0xb0);                                           // 0x1038ca26
		return 0;
	}
	case 0x157:
		ManBatSetMode(6);                                                 // 0x1038ca34..0x1038ca76 (0x1038ca36)
		ManBatFlyNode = FElysiumEntityHandle::Invalid();                  // 0x1038ca7c
		bManBatReachedMoveGoal = false;                                   // 0x1038ca82
		return 0;                                                         // no completion
	case 0x158:
	{
		SetIdealActivity(0x1170);                                         // 0x1038ca98
		FElysiumEntity* Closest = World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;   // 0x1038ca9d..0x1038cac4 (0x1038caa6 JZ / 0x1038cac2 JNZ)
		ManBatStartScreechCone(Closest);                                  // 0x1038cac9 / 0x1038cadc 0x100156d6
		return 0;
	}
	case 0x159:
	case 0x162:
		ManBatSetMode(7);                                                 // 0x1038caea..0x1038cb2a (0x1038caec)
		ManBatFlyByTarget = Senses.Memory.ClosestPlayer;                  // 0x1038cb30 / 0x1038cb38 +0x66ac = +0x628c
		ManBatFlyNode = FElysiumEntityHandle::Invalid();                  // 0x1038cb3e
		bManBatReachedMoveGoal = false;                                   // 0x1038cb44
		return 0;
	case 0x15a:
	case 0x161:
		SetIdealActivity(0x4b);                                           // 0x1038cd57
		return 0;
	case 0x15b:
		SetIdealActivity(0x1054);                                         // 0x1038cb5a
		return 0;
	case 0x15c:
		ManBatSetMode(8);                                                 // 0x1038cb68..0x1038cbb2 (0x1038cb6a)
		ManBatSetMoveGoalNodeId(1);                                       // 0x1038cbb9
		if (ManBatFindFlyNode(0) != nullptr)                              // 0x1038cbc3; 0x1038cbd5
		{
			bManBatReachedMoveGoal = false;                               // 0x1038cbdd
			SteerAndFlap();                                               // 0x1038cbe3 / 0x1038cc03
		}
		else
		{
			Species19FailBare(*this, 4);                                  // 0x1038cc0e
		}
		// FALLS THROUGH into 0x15d (`0x1038cc14`).
		[[fallthrough]];
	case 0x15d:
	{
		FVector Lifted = Origin;                                          // 0x1038cc18 slot 217
		Lifted.Z += GSpecies19ManBatLift * ElysiumMove::U;                // 0x1038cc2f / 0x1038cc32 FADD [0x1044e664]
		SetOrigin(Lifted);                                             // 0x1038cc41 slot 216 (0x100b2300 unparented: m_vecOrigin; slot 216 is an unported stub, slot 62's body is the one origin write)
		FlapFirst();                                                      // 0x1038cc49 0x10012e4f
		TaskComplete(false);                                              // 0x1038cc51
		return 0;
	}
	case 0x15e:
		FUN_1038fc80();                                                   // 0x1038cc61 0x10014c04
		TaskComplete(false);                                              // 0x1038cc6a
		return 0;
	case 0x15f:
		FUN_1038fd40();                                                   // 0x1038cc7a 0x10007a3b
		TaskComplete(false);                                              // 0x1038cc83
		return 0;
	case 0x160:
	{
		// The nearest entity NAMED "Cop" (`DAT_10642c04`) to this body's slot 220 `GetOrigin`; its
		// handle or -1 into `m_hFlyByTarget`; a live one takes mode 7, else `TaskFail(1)`.
		FElysiumEntity* Cop = World != nullptr
			? Species19FindNearestNamed(*World, TEXT("Cop"), Origin) : nullptr;   // 0x1038cc9a slot 220; 0x1038ccab 0x10009b8d / 0x1038ccb2 JZ
		ManBatFlyByTarget = Cop != nullptr ? Cop->Handle : FElysiumEntityHandle::Invalid();   // 0x1038ccb8..0x1038ccc5 +0x66ac
		if (World == nullptr || World->Resolve(ManBatFlyByTarget) == nullptr)   // 0x1038cccf..0x1038ccf9 (0x1038ccd8 JZ / 0x1038ccf5 JNZ)
		{
			Species19FailBare(*this, 1);                                  // 0x1038cd01
			return 0;
		}
		ManBatSetMode(7);                                                 // 0x1038cd10..0x1038cd38 (0x1038cd12 0x10011509 / 0x1038cd1f 0x10009e58)
		ManBatFlyNode = FElysiumEntityHandle::Invalid();                  // 0x1038cd3e
		bManBatReachedMoveGoal = false;                                   // 0x1038cd44
		return 0;
	}
	case 0x163:
		ManBatSetMode(9);                                                 // 0x1038cd65..0x1038cda7 (0x1038cd67 0x10011509)
		ManBatFlyNode = FElysiumEntityHandle::Invalid();                  // 0x1038cdad
		bManBatReachedMoveGoal = false;                                   // 0x1038cdb3
		ManBatCoastTimer = Species19Now(*this) + ElysiumNpcTunables::OneDouble;   // 0x1038cdbf / 0x1038cdc2 FADD double [0x10449280]; 0x1038cdc8 +0x66b4
		return 0;
	case 0x164:
		ClearHasPlayedFlyBySound();                                       // 0x1038cdd9 0x1000bd48 -> 0x10390040
		TaskComplete(false);                                              // 0x1038cde2
		return 0;
	default:
		break;
	}
	return FElysiumNpcHuman::StartTaskSlot442(Task);                     // 0x1038cdf3 0x10013336 CNPC_VHuman::StartTask
}

// =================================================================================================
// CNPC_VMingXiao -- 0x10392d80, 1772 bytes
// =================================================================================================

static const TCHAR* const GSpecies19FileMingXiao = TEXT("NPC_VMingXiao.cpp");             // 0x10647090
// `0x41a00000` -- 20.0, the yaw range the three MingXiao turn arms clamp the motor to.
static constexpr float GSpecies19MingXiaoYawRange = 20.0f;
// `0xc2b40000` / `0x42b40000` -- the `hit_yaw` draw of task `0x15b`; `0xc1700000` / `0x41700000`
// the jitter task `0x15c` adds.
static constexpr float GSpecies19MingXiaoHitYawSpread = 90.0f;
static constexpr float GSpecies19MingXiaoHitYawJitter = 15.0f;

/** SEAM for the motor's clamp pair `0x102e0a40(m_pMotor, yaw)` (`motor+0x18`, the clamp centre) and
 *  `0x102e0a60(m_pMotor, range)` (`motor+0x1c`). The port's motor carries neither word (its
 *  `ResetSteering` is only `0x102e0a60`'s 180.0 case), so nothing is written. */
static void Species19MotorYawClamp(FElysiumNpcBase& Npc, float CentreYaw, float Range)
{
	(void)Npc;
	(void)CentreYaw;
	(void)Range;
}

/** `thunk_FUN_101e8da0(0x10739d08)` -- `CNPC_VMingXiao`'s tuning record, read by field offset. The
 *  same seam `ElysiumNpcMingXiao.cpp`'s file-local `MingXiaoTuningField` stands: no such table here,
 *  every field answers 0. */
static float Species19MingXiaoTuningField(int32)
{
	return 0.f;
}

void FElysiumNpcMingXiao::FUN_1039a750()
{
	// SEAM for `0x1039a750`, the transform: `SetIdealActivity(1)`, the render/hull reset, a
	// `npc_VMingXiao` proxy created, spawned, re-modelled and put on schedule `0x157`, and the two
	// transform emitters. Not this lane's row; counted.
	++Fun1039a750Calls;
}

bool FElysiumNpcMingXiao::MingXiaoTentacleBonePosition(int32 Tentacle, FVector& OutCm) const
{
	// SEAM for `0x10398630`: `LookupBone(0x10398680(tentacle))` (a negative bone reads bone 0), then
	// `GetBonePosition`. No skeleton is readable at the kernel tier; answers false and leaves `OutCm`.
	(void)Tentacle;
	(void)OutCm;
	return false;
}

void FElysiumNpcMingXiao::MingXiaoEmitter(const TCHAR* Name, const TCHAR* Attachment)
{
	// SEAM for `0x102c4310(this, name, this, 1, -1, 0)` (`0x1000aa01`) and `0x102c42a0(this, name2,
	// "Bip01 Spine6", 0)` (`0x1000c392`), the named particle emitters (attached at a bone for the
	// second). Recorded.
	MingXiaoEmitters.Add(FString(Name) + (Attachment != nullptr ? FString(TEXT("@")) + Attachment : FString()));
}

// Slot 442: `0x10392d80`.
int32 FElysiumNpcMingXiao::StartTaskSlot442(void* Task)
{
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	switch (TaskLocal)                                                   // 0x10392d8e ADD -0x89; JA 0x1039345b (0x10392d98; 0x10392da6 table jump)
	{
	case 0x89:
		Species19MotorYawSpeedReset(*this);                               // 0x10392e33 0x102e0b40
		LastAttackTime = Species19Now(*this);                             // 0x10392e45
		RestartIdealActivityId(0x4b);                                     // 0x10392e4b
		return 0;
	case 0x8a:
		Species19MotorYawSpeedReset(*this);                               // 0x10392e5f
		LastAttackTime = Species19Now(*this);                             // 0x10392e6e
		RestartIdealActivityId(0x4b);                                     // 0x10392e76
		return 0;
	case 0x8b:
	{
		FElysiumEntity* Enemy = static_cast<const FElysiumNpc*>(this)->GetEnemy();   // 0x10392e88 slot 167
		if (Enemy != nullptr && Enemy->AsCombatCharacter() != nullptr)    // 0x10392e90 / 0x10392e9a +0x9c
		{
			Species19MotorYawSpeedReset(*this);                           // 0x10392ea2
			const FElysiumEntity* LkpKey = static_cast<const FElysiumNpc*>(this)->GetEnemy();      // 0x10392eab
			FVector EnemyLkp = FVector::ZeroVector;
			EnemyLkp = Conditions19LastKnownPosition(LkpKey);                             // 0x10392ebb / 0x10392ec3 -> 0x102dfed0
			Species19MotorIdealYawTo(*this, EnemyLkp);                    // 0x10392ed5 0x102e2020
		}
		RestartIdealActivityId(0x51);                                     // 0x10392ede, always
		return 0;
	}
	case 0x8e:
		RestartIdealActivityId(0x52);                                     // 0x10392ef0
		return 0;
	case 0x9f:
	{
		if (ActiveWeaponEntity() == nullptr)                              // 0x10392daf / 0x10392db6
		{
			Species19Fail(*this, GSpecies19FileMingXiao, 0x296, 3);        // 0x10392e0a / 0x10392e14 / 0x10392e1e
			return 0;
		}
		const float Width = Species19HullWidthUnits(*this, HullKind);    // 0x10392dc5 0x102d61b0
		float WeaponRangeUnits = 0.f;
		ActiveWeaponMaxRangeUnits(WeaponRangeUnits);                      // 0x10392dd5; 0x10392dda +0x8c0
		// `0x10392ded` thunk `0x1001402e` -> `0x102ee1c0` on `m_pNavigator`: the PATH tolerance.
		NavPathToleranceCm = (WeaponRangeUnits * Step->Data + (Width + Width)) * ElysiumMove::U;   // 0x10392de0 / 0x10392dca / 0x10392ded
		TaskComplete(false);                                              // 0x10392df6
		return 0;
	}
	case 0x14a:
		FUN_1039a750();                                                   // 0x10392f00 0x100111d0
		TaskComplete(false);                                              // 0x10392f09
		return 0;
	case 0x14b:
		return 0;                                                         // bare return, the task keeps running
	case 0x14c:
	{
		TCHAR ModelName[] = TEXT("models/character/monster/MingXiao/MingXiao.mdl");   // 0x10647058
		SetModel(ModelName);                                              // 0x10392f20 slot 105
		EffectsWord |= 0x10u;                                             // 0x10392f2e / 0x10392f33
		// `m_nRenderFX` (+0x168) = 0, `m_nRenderMode` (+0x16c) = 0: render words the kernel tier does
		// not carry -- visual only.
		HullKind = 0xf;                                                   // 0x10392f4d m_eHull = 0xf
		SetHullSizeNormal(true);                                          // 0x10392f57 0x10273070
		// `0x10392f5d` Relink: no spatial partition.
		bMingXiaoHasTransformed = true;                                   // 0x10392f67 +0x6678 = 1
		TaskComplete(false);                                              // 0x10392f70
		return 0;
	}
	case 0x14d:
		ThinkSet(TEXT("0x101c0b10"), 0.0);                                // 0x10392f82 PUSH 0x10015b68; 0x10392f89
		NextThink = static_cast<float>(Species19Now(*this) + static_cast<double>(ElysiumNpcTunables::Hundredth));   // 0x10392f97..0x10392fa0 +0x17c
		TaskComplete(false);                                              // 0x10392fa6
		return 0;
	case 0x14e:
		BeginDefeatSequenceOnce();                                        // 0x10392fb6 0x1000d9fe -> 0x10395ce0
		TaskComplete(false);                                              // 0x10392fbf
		return 0;
	case 0x14f:
		// `0x103937d0(task, tentacle, activity)`: the port's body takes (task, tentacle, tuning) and
		// carries no activity parameter; the activity (`0x112a`..`0x112d`) is named here.
		MingXiaoThrowAttack(TaskLocal, 0, Species19MingXiaoTuningField);  // 0x10392fcd PUSH 0x112a; 0x10392fd7
		return 0;
	case 0x150:
		MingXiaoThrowAttack(TaskLocal, 1, Species19MingXiaoTuningField);  // 0x10392fe5 PUSH 0x112b; 0x10392fef
		return 0;
	case 0x151:
		MingXiaoThrowAttack(TaskLocal, 2, Species19MingXiaoTuningField);  // 0x10392ffd PUSH 0x112c; 0x10393007
		return 0;
	case 0x152:
		MingXiaoThrowAttack(TaskLocal, 3, Species19MingXiaoTuningField);  // 0x10393015 PUSH 0x112d; 0x1039301f
		return 0;
	case 0x153:
		Species19MotorYawSpeedReset(*this);                               // 0x10393033
		Species19MotorYawClamp(*this, static_cast<float>(Angles.Y), GSpecies19MingXiaoYawRange);   // 0x1039303c slot 221; 0x1039304f 0x102e0a40; 0x1039305f 0x102e0a60
		RestartIdealActivityId(0x1131);                                   // 0x1039306b
		return 0;
	case 0x154:
		Species19MotorYawSpeedReset(*this);                               // 0x1039307f
		Species19MotorYawClamp(*this, static_cast<float>(Angles.Y), GSpecies19MingXiaoYawRange);   // 0x10393088 / 0x1039309b / 0x103930ab
		RestartIdealActivityId(0x1130);                                   // 0x103930b7
		return 0;
	case 0x155:
	{
		// `m_hRangedWeapon` (+0x6680) resolved (its `+0xa0` weapon record, 0 when stale) into slot 388
		// `Weapon_Switch(weapon, 0)`, `RestartIdealActivity(0x19)`, then `m_flSpitAttackTimer = 0x10397f70()
		// + curtime`. No completion.
		Species19MotorYawSpeedReset(*this);                               // 0x103930cb
		FElysiumEntity* Ranged = World != nullptr ? World->Resolve(MingXiaoRangedWeapon) : nullptr;   // 0x103930d0..0x10393102 (0x103930d9 JZ / 0x103930f6 JNZ / 0x103930fc JNZ)
		Weapon_Switch(Ranged, 0);                                         // 0x1039310f CALL [EAX+0x610]
		RestartIdealActivityId(0x19);                                     // 0x10393119
		MingXiaoSpitAttackTimer = static_cast<double>(FUN_10397f70(Species19MingXiaoTuningField))
			+ Species19Now(*this);                                        // 0x10393120 0x10011d42; 0x1039312c FADD; 0x1039312f +0x66c0
		return 0;
	}
	case 0x156:
	{
		const int32 Mode = static_cast<int32>(Step->Data);                // 0x1039313d FLD; 0x10393140 __ftol
		ThrowableObjectMode(Mode >= 0 && Mode <= 4 ? Mode : 0);           // 0x10393147 / 0x1039314c; 0x10393151 / 0x1039316c 0x10398d90
		TaskComplete(false);                                              // 0x1039315a / 0x10393175
		return 0;
	}
	case 0x157:
		// Goal type 4 at `m_vecPickupTargetPos` (+0x6720, SOURCE units), activity word `0x13`, three
		// -1 words, tolerance -1.0 (`0x104bc6cc`), flags 0: `SetGoal(record, 0)` and its answer is the
		// return value -- no completion, no failure.
		return Species19SetGoal(*this, 4, MingXiaoPickupTargetPos * ElysiumMove::U, 0x13,
			-1.f, 0) ? 1 : 0;    // 0x10393183..0x10393211 0x102ecd20
	case 0x158:
		RestartIdealActivityId(MingXiaoThrowingTentacle == 4 ? 0x112f : 0x112e);   // 0x1039321f +0x671c; 0x10393231 / 0x10393244 (0x1039322a JNZ)
		return 0;
	case 0x159:
		if (MingXiaoThrowableObjectMode == 3 || MingXiaoThrowableObjectMode == 4)   // 0x10393252 +0x673c (0x1039325b JZ / 0x10393260 JNZ)
		{
			MingXiaoThrowCleanup();                                       // 0x10393268 0x10003e3b -> 0x10398fd0
		}
		TaskComplete(false);                                              // 0x10393271 / 0x1039344d
		return 0;
	case 0x15a:
		Species19MotorYawSpeedReset(*this);                               // 0x10393285
		Species19MotorYawClamp(*this, static_cast<float>(Angles.Y), GSpecies19MingXiaoYawRange);   // 0x1039328e / 0x103932a1 / 0x103932b1
		RestartIdealActivityId(MingXiaoThrowingTentacle == 4 ? 0x1131 : 0x1130);   // 0x103932b6; 0x103932c8 / 0x103932db (0x103932c1 JNZ)
		return 0;
	case 0x15b:
	{
		const float HitYaw = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
			.FRandRange(-GSpecies19MingXiaoHitYawSpread, GSpecies19MingXiaoHitYawSpread);   // 0x103932fb RandomFloat(-90, 90)
		SetPoseParameter(TEXT("hit_yaw"), HitYaw, false);                 // 0x10393312 slot 345
		RestartIdealActivityId(0x73);                                     // 0x1039331e
		return 0;
	}
	case 0x15c:
	{
		// The bone of `m_eLastLostTentacle` (+0x6714); `-VecToYaw(GetAbsOrigin - bone)` (the listing
		// NEGATES the yaw, `FCHS` at `0x10393394`) plus `RandomFloat(-15, 15)` into `hit_yaw`.
		FVector BoneCm = FVector::ZeroVector;
		MingXiaoTentacleBonePosition(MingXiaoLastLostTentacle, BoneCm);  // 0x1039333a 0x1000a51f -> 0x10398630
		const FVector Delta = Origin - BoneCm;                            // 0x10393343 slot 217; 0x10393349..0x10393365
		const float Yaw = -NpcKernelFacingShared::RetailYawOf(Delta, static_cast<float>(Angles.Y));   // 0x10393386 0x101d2c70; 0x10393394 FCHS
		const float HitYaw = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
			.FRandRange(-GSpecies19MingXiaoHitYawJitter, GSpecies19MingXiaoHitYawJitter) + Yaw;   // 0x103933a6; 0x103933a9 FADD
		SetPoseParameter(TEXT("hit_yaw"), HitYaw, false);                 // 0x103933c1 slot 345
		RestartIdealActivityId(0x74);                                     // 0x103933cd
		return 0;
	}
	case 0x15d:
		MingXiaoEmitter(TEXT("Ming_xiao_tentacle_damage_emitter"), nullptr);   // 0x103933e2 / 0x103933e9
		TaskComplete(false);                                              // 0x103933f2
		return 0;
	case 0x15e:
		MingXiaoEmitter(TEXT("Ming_xiao_death_emitter"), nullptr);        // 0x10393407 / 0x1039340e
		MingXiaoEmitter(TEXT("Ming_xiao_death_emitter2"), TEXT("Bip01 Spine6"));   // 0x10393415 / 0x1039341b / 0x10393444
		TaskComplete(false);                                              // 0x1039344d
		return 0;
	case 0x15f:
		MingXiaoEmitter(TEXT("Ming_xiao_death_proxy_emitter"), nullptr);  // 0x10393429 / 0x10393430
		MingXiaoEmitter(TEXT("Ming_xiao_death_proxy_emitter2"), TEXT("Bip01 Spine6"));   // 0x10393437 / 0x1039343d / 0x10393444
		TaskComplete(false);                                              // 0x1039344d
		return 0;
	default:
		break;
	}
	return FElysiumNpc::StartTaskSlot442(Task);                          // 0x1039345e CAI_BaseNPCTroika::StartTask
}

// =================================================================================================
// CNPC_VMingXiaoTentacle -- 0x1039c4c0, 3486 bytes
// =================================================================================================

static const TCHAR* const GSpecies19FileTentacle = TEXT("NPC_VMingXiaoTentacle.cpp");     // 0x1064a398
static const TCHAR* const GSpecies19TentacleBabyModel =
	TEXT("models/character/monster/MingXiao/MingXiao_baby/MingXiao_baby.mdl");            // 0x1064a2f0
static const TCHAR* const GSpecies19TentacleTransformModel =
	TEXT("models/character/monster/MingXiao/MingXiao_transformation.mdl");                // 0x1064a2a0
// The node searches every evade arm makes: `0x102edae0(navigator, point, 512.0, 30000.0, &out)`.
static constexpr float GSpecies19TentacleNodeFlee = 512.0f;          // `PUSH 0x44000000`
static constexpr float GSpecies19TentacleNodeLimit = 30000.0f;       // `PUSH 0x46ea6000`
// The evade jitter bands (`0x42700000` 60.0, `0x42a00000` 80.0), the companion push (`_DAT_104492b8`
// 200.0) and its jitter (`0x41a00000` 20.0), the evade re-arm (`_DAT_10456854` 600.0), the scatter
// radius squared (`_DAT_1049dfe4` 262144.0 = 512^2), and the hide-ready draw (`0x40a00000` 5.0 ..
// `0x41700000` 15.0). Read out of the pinned image; none is in the tunables table.
static constexpr float GSpecies19TentacleNarrowBand = 60.0f;
static constexpr float GSpecies19TentacleWideBand = 80.0f;
static constexpr float GSpecies19TentaclePush = 200.0f;
static constexpr float GSpecies19TentaclePushJitter = 20.0f;
static constexpr float GSpecies19TentacleEvadeRearm = 600.0f;
static constexpr float GSpecies19TentacleScatterRadiusSqr = 262144.0f;
static constexpr float GSpecies19TentacleHideMin = 5.0f;
static constexpr float GSpecies19TentacleHideMax = 15.0f;
// `IsAreaClear`'s mask (`0x202400b`, `MASK_NPCSOLID`).
static constexpr int32 GSpecies19TentacleClearMask = 0x202400b;

void FElysiumNpcMingXiaoTentacle::TentacleCreateEmitter(const TCHAR* Name, const FVector& PositionUnits)
{
	// SEAM for `0x102c41d0(this, name, &position, 0, 0)` (`0x10003981`), the named particle emitter at a
	// point. Recorded in SOURCE units.
	TentacleEmitterPlacements.Add(FTeleportEmitterPlacement{ FString(Name), PositionUnits });
}

void FElysiumNpcMingXiaoTentacle::FUN_1039f310()
{
	// SEAM for `0x1039f310`, the phase-change teardown task `0x14a` runs before a new phase. Not this
	// lane's row; counted.
	++Fun1039f310Calls;
}

void FElysiumNpcMingXiaoTentacle::FUN_1039ef10()
{
	// SEAM for `0x1039ef10` (task `0x14e`). Not this lane's row; counted.
	++Fun1039ef10Calls;
}

// Slot 442: `0x1039c4c0`.
int32 FElysiumNpcMingXiaoTentacle::StartTaskSlot442(void* Task)
{
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	const double Now = Species19Now(*this);
	FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	// `SetGoal` over the evade record every arm builds: type 4 at the point, activity word `0x13`,
	// the other three -1, tolerance `_DAT_104bde70` (-1.0), flags 0.
	auto EvadeGoal = [this](const FVector& PointUnits, const TCHAR* Reason)
	{
		(void)Reason;
		return Species19SetGoal(*this, 4, PointUnits * ElysiumMove::U, 0x13, -1.f, 0);
	};

	switch (TaskLocal)                                                   // 0x1039c4d5 ADD -0x8b; JA 0x1039d249 (0x1039c4df; 0x1039c4ed table jump)
	{
	case 0x8b:
	{
		FElysiumEntity* Enemy = static_cast<const FElysiumNpc*>(this)->GetEnemy();   // 0x1039c5bd slot 167
		if (Enemy != nullptr && Enemy->AsCombatCharacter() != nullptr)    // 0x1039c5c5 / 0x1039c5cf +0x9c
		{
			Species19MotorYawSpeedReset(*this);                           // 0x1039c5d7
			const FElysiumEntity* LkpKey = static_cast<const FElysiumNpc*>(this)->GetEnemy();      // 0x1039c5e0
			FVector EnemyLkp = FVector::ZeroVector;
			EnemyLkp = Conditions19LastKnownPosition(LkpKey);                             // 0x1039c5f0 / 0x1039c5f8 -> 0x102dfed0
			Species19MotorIdealYawTo(*this, EnemyLkp);                    // 0x1039c60a 0x102e2020
		}
		RestartIdealActivityId(0x51);                                     // 0x1039c613, always
		return 0;
	}
	case 0x8e:
		RestartIdealActivityId(0x52);                                     // 0x1039c629
		return 0;
	case 0x9f:
	{
		// No weapon: line 0x181, `TaskFail(3)`. Otherwise the tolerance is MY hull's mins.x plus the
		// ENEMY's hull's mins.x (`0x102d6100` answers the mins vector; its first word is read) plus
		// range * data -- the two-hull form the human arm does not have.
		if (ActiveWeaponEntity() == nullptr)                              // 0x1039c4f6 / 0x1039c501
		{
			Species19Fail(*this, GSpecies19FileTentacle, 0x181, 3);       // 0x1039c592 / 0x1039c59c / 0x1039c5a6
			return 0;
		}
		// The enemy's `+0x9c` combat character's `+0x1568` hull, 0 with none. The port carries a hull
		// only on NPC leaves; a non-NPC combat character (the player) reads 0 (named).
		const FElysiumEntity* Enemy = static_cast<const FElysiumNpc*>(this)->GetEnemy();   // 0x1039c507 slot 167
		const FElysiumNpcBase* EnemyNpc = Enemy != nullptr ? Enemy->AsNpcBase() : nullptr;
		const int32 EnemyHull = (Enemy != nullptr && Enemy->AsCombatCharacter() != nullptr && EnemyNpc != nullptr)
			? EnemyNpc->HullKind : 0;                                     // 0x1039c511..0x1039c527 (0x1039c50f JZ / 0x1039c519 JZ)
		FVector MyMins = FVector::ZeroVector;
		FVector MyMaxs = FVector::ZeroVector;
		RetailHullExtents(HullKind, EElysiumHullExtents::Full, MyMins, MyMaxs);   // 0x1039c53c 0x102d6100(m_eHull)
		FVector EnemyMins = FVector::ZeroVector;
		FVector EnemyMaxs = FVector::ZeroVector;
		RetailHullExtents(EnemyHull, EElysiumHullExtents::Full, EnemyMins, EnemyMaxs);   // 0x1039c548
		float WeaponRangeUnits = 0.f;
		ActiveWeaponMaxRangeUnits(WeaponRangeUnits);                      // 0x1039c556; 0x1039c55b +0x8c0
		// `0x1039c575` thunk `0x1001402e` -> `0x102ee1c0` on `m_pNavigator`: the PATH tolerance.
		NavPathToleranceCm = (static_cast<float>(MyMins.X + EnemyMins.X) + WeaponRangeUnits * Step->Data)
			* ElysiumMove::U;                                             // 0x1039c561 FMUL; 0x1039c569 / 0x1039c56c / 0x1039c570; 0x1039c575
		TaskComplete(false);                                              // 0x1039c57e
		return 0;
	}
	case 0x14a:
	{
		// A new phase (only when it differs from `m_ePhase` +0x6670): the teardown, then 1 = invincible,
		// 2 = vulnerable, 3 and anything else (as phase 0) = invincible; each clears
		// `m_flPhaseExpireTimer` (+0x6674). Always completes.
		const int32 Phase = static_cast<int32>(Step->Data);               // 0x1039c63b FLD; 0x1039c63e __ftol
		if (Phase != TentaclePhase)                                       // 0x1039c645 / 0x1039c64d / 0x1039c64f JZ
		{
			FUN_1039f310();                                               // 0x1039c657 0x100072ac
			switch (Phase)                                                // 0x1039c65c CMP 3; JA 0x1039c6cb (0x1039c65f; 0x1039c661 table jump)
			{
			case 1:
				TentaclePhase = 1;                                        // 0x1039c670
				bInvincible = true;                                       // 0x1039c676 +0x63d8 = 1
				MingXiaoTentaclePhaseExpireTimer = 0.0;                   // 0x1039c67c
				break;
			case 2:
				TentaclePhase = 2;                                        // 0x1039c697
				bInvincible = false;                                      // 0x1039c6a1
				MingXiaoTentaclePhaseExpireTimer = 0.0;                   // 0x1039c6a7
				break;
			case 3:
				TentaclePhase = 3;                                        // 0x1039c6bf
				bInvincible = true;                                       // 0x1039c6d1
				MingXiaoTentaclePhaseExpireTimer = 0.0;                   // 0x1039c6d8
				break;
			default:
				TentaclePhase = 0;                                        // 0x1039c6cb
				bInvincible = true;                                       // 0x1039c6d1
				MingXiaoTentaclePhaseExpireTimer = 0.0;                   // 0x1039c6d8
				break;
			}
		}
		TaskComplete(false);                                              // 0x1039c682 / 0x1039c6ad / 0x1039c6e1
		return 0;
	}
	case 0x14b:
	{
		// The form swap on the truncated data word: 2 = the grub (no emitter), 3 = the transformation
		// (hull 0xf, attack extents 64/64/32, the baby-transform emitter), anything else = the
		// tentacle-to-grub swap (hull 0x11, the tentacle-transform emitter). All three re-hull and
		// complete.
		const int32 Form = static_cast<int32>(Step->Data);                // 0x1039c6f3 / 0x1039c6f6 __ftol
		const FVector HereUnits = Origin / ElysiumMove::U;                // 0x1039c701 slot 217; 0x1039c707..0x1039c71a
		const TCHAR* Emitter = nullptr;
		if (Form == 2)                                                    // 0x1039c724 [0x1039d380] (0x1039c71e JA)
		{
			SetModelName(FName(GSpecies19TentacleBabyModel));             // 0x1039c73e slot 212
			SetModelIndex(ModeIndexGrub);                                 // 0x1039c74f slot 10 (+0x6668)
			TCHAR ModelName[] = TEXT("models/character/monster/MingXiao/MingXiao_baby/MingXiao_baby.mdl");
			SetModel(ModelName);                                          // 0x1039c75b slot 105
			HullKind = 0x11;                                              // 0x1039c76a +0x1568
			PathingHullKind = 0x11;                                       // 0x1039c770 +0x156c
			SetAttackExtents(FVector::ZeroVector);                        // 0x1039c793 slot 15 (0, 0, 0)
		}
		else if (Form == 3)
		{
			SetModelName(FName(GSpecies19TentacleTransformModel));        // 0x1039c7ae slot 212
			SetModelIndex(ModeIndexGrubToProxy);                          // 0x1039c7bf slot 10 (+0x666c)
			TCHAR ModelName[] = TEXT("models/character/monster/MingXiao/MingXiao_transformation.mdl");
			SetModel(ModelName);                                          // 0x1039c7cb slot 105
			HullKind = 0xf;                                               // 0x1039c7da
			PathingHullKind = 0xf;                                        // 0x1039c7e0
			SetAttackExtents(FVector(64.f, 64.f, 32.f) * ElysiumMove::U); // 0x1039c7eb..0x1039c809 slot 15
			Emitter = TEXT("Ming_xiao_baby_transform_emitter");           // 0x1039c815
		}
		else
		{
			SetModelName(FName(GSpecies19TentacleBabyModel));             // 0x1039c82f slot 212
			SetModelIndex(ModeIndexTentacleToGrub);                       // 0x1039c840 slot 10 (+0x6664)
			TCHAR ModelName[] = TEXT("models/character/monster/MingXiao/MingXiao_baby/MingXiao_baby.mdl");
			SetModel(ModelName);                                          // 0x1039c84c slot 105
			HullKind = 0x11;                                              // 0x1039c85b
			PathingHullKind = 0x11;                                       // 0x1039c861
			SetAttackExtents(FVector::ZeroVector);                        // 0x1039c884 slot 15
			Emitter = TEXT("Ming_xiao_tentacle_transform_emitter");       // 0x1039c890
		}
		if (Emitter != nullptr)
		{
			TentacleCreateEmitter(Emitter, HereUnits);                    // 0x1039c897 0x10003981
		}
		SetHullSizeNormal(true);                                          // 0x1039c8a0 0x10273070
		// `0x1039c8a6` Relink: no spatial partition.
		TaskComplete(false);                                              // 0x1039c8b2
		return 0;
	}
	case 0x14c:
		SetForceFrequentThink(true);                                      // 0x1039c8ca slot 416; no completion
		return 0;
	case 0x14d:
		TaskComplete(false);                                              // 0x1039cbfc (the 0x14f fall-through target)
		return 0;
	case 0x14e:
		FUN_1039ef10();                                                   // 0x1039c8df 0x10005aba
		TaskComplete(false);                                              // 0x1039c8e8
		return 0;
	case 0x14f:
	{
		// The evade: a node 512..30000 from the enemy, jittered off it along its dominant axis up to
		// five times until the area is clear, then a run goal there.
		FElysiumEntity* Enemy = static_cast<const FElysiumNpc*>(this)->GetEnemy();   // 0x1039c8fe slot 167
		if (Enemy == nullptr)                                             // 0x1039c908
		{
			Species19Fail(*this, GSpecies19FileTentacle, 0x200, 6);       // 0x1039c90e / 0x1039c918 / 0x1039c922
			return 0;
		}
		const FVector EnemyUnits = Enemy->Origin / ElysiumMove::U;       // 0x1039c941 slot 217 (the enemy re-read 0x1039c937 slot 167)
		FVector NodeCm = FVector::ZeroVector;
		if (!NearestNavigatorNode(Enemy->Origin, GSpecies19TentacleNodeFlee, GSpecies19TentacleNodeLimit, NodeCm))   // 0x1039c975 0x102edae0 / 0x1039c97c JNZ
		{
			Species19Fail(*this, GSpecies19FileTentacle, 0x20b, 7);       // 0x1039c984 / 0x1039c98e / 0x1039c998
			return 0;
		}
		FVector NodeUnits = NodeCm / ElysiumMove::U;
		const float Dx = static_cast<float>(EnemyUnits.X - NodeUnits.X);  // 0x1039c9ab..0x1039c9b3
		const float Dy = static_cast<float>(EnemyUnits.Y - NodeUnits.Y);  // 0x1039c9b7..0x1039c9bf
		float XLo = 0.f, XHi = 0.f, YLo = 0.f, YHi = 0.f;
		if (FMath::Abs(Dy) < FMath::Abs(Dx))                              // 0x1039c9cf FCOMPP; 0x1039c9dc JP
		{
			// X dominant: the X band is one-sided away from the enemy, the Y band wide.
			if (Dx > ElysiumNpcTunables::Zero) { XLo = 0.f; XHi = GSpecies19TentacleNarrowBand; }   // 0x1039c9ed / 0x1039c9f1 (0x1039c9eb JNZ)
			else { XLo = -GSpecies19TentacleNarrowBand; XHi = 0.f; }                               // 0x1039c9fb / 0x1039c9ff
			YLo = -GSpecies19TentacleWideBand; YHi = GSpecies19TentacleWideBand;                   // 0x1039ca07 / 0x1039ca0f
		}
		else
		{
			if (Dy > ElysiumNpcTunables::Zero) { YLo = 0.f; YHi = GSpecies19TentacleNarrowBand; }   // 0x1039ca28 / 0x1039ca2c (0x1039ca26 JNZ)
			else { YLo = -GSpecies19TentacleNarrowBand; YHi = 0.f; }                               // 0x1039ca36 / 0x1039ca3a
			XLo = -GSpecies19TentacleWideBand; XHi = GSpecies19TentacleWideBand;                   // 0x1039ca42 / 0x1039ca4a
		}
		// Up to five candidates: Z lifted by half the step height, the Y draw BEFORE the X draw. A clear
		// one replaces the node; the fifth refusal keeps the node itself.
		for (int32 Try = 0; Try < 5; ++Try)                              // 0x1039ca6e EDI = 0; 0x1039cae3 / 0x1039cae4 (0x1039cae7 JGE)
		{
			const float Z = StepHeight() * ElysiumNpcTunables::Half + static_cast<float>(NodeUnits.Z);   // 0x1039ca74 slot 522; 0x1039ca7a / 0x1039ca8f
			const float Y = Stream.FRandRange(YLo, YHi) + static_cast<float>(NodeUnits.Y);   // 0x1039ca9a; 0x1039ca9d
			const float X = Stream.FRandRange(XLo, XHi) + static_cast<float>(NodeUnits.X);   // 0x1039cab3; 0x1039cab6
			const FVector Candidate(X, Y, Z);
			if (IsAreaClear(Candidate * ElysiumMove::U, GSpecies19TentacleClearMask))   // 0x1039cade 0x10001f50 -> 0x102a0fb0 (0x1039caeb JZ / 0x1039caf1 JZ)
			{
				NodeUnits = Candidate;                                    // 0x1039caf3..0x1039cb07
				break;
			}
		}
		if (!EvadeGoal(NodeUnits, TEXT("CNPC_VMingXiaoTentacle::StartTask 0x14f")))   // 0x1039cb0b..0x1039cbcb 0x102ecd20 / 0x1039cbd2 JZ
		{
			Species19Fail(*this, GSpecies19FileTentacle, 0x26b, 0xc);     // 0x1039cc14 / 0x1039cc1e / 0x1039cc28
			return 0;
		}
		TentacleUpdateEvadeTimer = static_cast<double>(Stream.FRandRange(ElysiumNpcTunables::One, 2.0f)) + Now;   // 0x1039cbe6 RandomFloat(1.0, 2.0); 0x1039cbef; 0x1039cbf2 +0x667c
		TaskComplete(false);                                              // 0x1039cbfc (falls into 0x14d)
		return 0;
	}
	case 0x150:
		return 0;                                                         // bare return, the task keeps running
	case 0x151:
	{
		// The hide behind the companion: 200 units beyond it along enemy -> companion, jittered by
		// +-20 per axis (drawn z, then y, then x).
		FElysiumEntity* Companion = MingXiaoTentacleCompanion();          // 0x1039cc3d 0x1001105e -> 0x1039ede0
		FElysiumEntity* Enemy = static_cast<const FElysiumNpc*>(this)->GetEnemy();   // 0x1039cc48 slot 167
		if (Companion == nullptr || Enemy == nullptr)                     // 0x1039cc54 / 0x1039cc5c
		{
			Species19Fail(*this, GSpecies19FileTentacle, 0x2a0, 1);       // 0x1039cf0e / 0x1039cf18 / 0x1039cf22
			return 0;
		}
		const FVector CompanionUnits = Companion->Origin / ElysiumMove::U;   // 0x1039cc66 slot 217
		FVector Direction = CompanionUnits - Enemy->Origin / ElysiumMove::U;   // 0x1039cc85 slot 217; 0x1039cc8b..0x1039cca4
		Direction.Normalize();                                            // 0x1039ccc7 VectorNormalize (length discarded)
		const float JitterZ = Stream.FRandRange(-GSpecies19TentaclePushJitter, GSpecies19TentaclePushJitter);   // 0x1039cce1
		const float JitterY = Stream.FRandRange(-GSpecies19TentaclePushJitter, GSpecies19TentaclePushJitter);   // 0x1039ccfa
		const float JitterX = Stream.FRandRange(-GSpecies19TentaclePushJitter, GSpecies19TentaclePushJitter);   // 0x1039cd13
		const FVector Candidate(
			CompanionUnits.X + Direction.X * GSpecies19TentaclePush + JitterX,   // 0x1039cd16 / 0x1039cd41 / 0x1039cd5d
			CompanionUnits.Y + Direction.Y * GSpecies19TentaclePush + JitterY,   // 0x1039cd20 / 0x1039cd49 / 0x1039cd67
			CompanionUnits.Z + Direction.Z * GSpecies19TentaclePush + JitterZ);  // 0x1039cd2a / 0x1039cd51 / 0x1039cd81
		if (!IsAreaClear(Candidate * ElysiumMove::U, GSpecies19TentacleClearMask))   // 0x1039cd98; 0x1039cd9f
		{
			Species19Fail(*this, GSpecies19FileTentacle, 0x29a, 0x1a);    // 0x1039cee1 / 0x1039ceeb / 0x1039cef5
			return 0;
		}
		if (!EvadeGoal(Candidate, TEXT("CNPC_VMingXiaoTentacle::StartTask 0x151")))   // 0x1039ce58 0x102ecd20 / 0x1039ce5f JZ
		{
			Species19Fail(*this, GSpecies19FileTentacle, 0x294, 0xc);     // 0x1039ceb4 / 0x1039cebe / 0x1039cec8
			return 0;
		}
		TentacleUpdateEvadeTimer = Now + static_cast<double>(GSpecies19TentacleEvadeRearm);   // 0x1039ce71 / 0x1039ce74 / 0x1039ce7a +0x667c
		TentacleHideReadyTimer = static_cast<double>(Stream.FRandRange(GSpecies19TentacleHideMin, GSpecies19TentacleHideMax)) + Now;   // 0x1039ce88; 0x1039ce93; 0x1039ce96 +0x6680
		TaskComplete(false);                                              // 0x1039ce9c
		return 0;
	}
	case 0x152:
	{
		// The scatter: with a live enemy inside 512 the node search centres on (enemy * 3 + scatter
		// centre) / 4; otherwise (or when that finds nothing) on the scatter centre itself.
		FVector NodeCm = FVector::ZeroVector;
		bool bFound = false;
		if (FElysiumEntity* Enemy = static_cast<const FElysiumNpc*>(this)->GetEnemy())   // 0x1039cf39 slot 167; 0x1039cf43
		{
			const FVector EnemyUnits = Enemy->Origin / ElysiumMove::U;   // 0x1039cf57 slot 217 (the enemy re-read 0x1039cf4d slot 167)
			const FVector Delta = EnemyUnits - Origin / ElysiumMove::U;   // 0x1039cf75 slot 217; 0x1039cf7b..0x1039cf8c
			if (static_cast<float>(Delta.SizeSquared()) < GSpecies19TentacleScatterRadiusSqr)   // 0x1039cf9f FCOMP [0x1049dfe4]; 0x1039cfb0 JP
			{
				const FVector CentreUnits = (EnemyUnits * 3.0 + TentacleScatterCenterUnits) * 0.25;   // 0x1039cfb6..0x1039d03d [0x10449258] 3.0, [0x1044bef8] 0.25
				bFound = NearestNavigatorNode(CentreUnits * ElysiumMove::U, GSpecies19TentacleNodeFlee,
					GSpecies19TentacleNodeLimit, NodeCm);                 // 0x1039d042 0x10012850 / 0x1039d049 JNZ
			}
		}
		if (!bFound)
		{
			if (!NearestNavigatorNode(TentacleScatterCenterUnits * ElysiumMove::U, GSpecies19TentacleNodeFlee,
					GSpecies19TentacleNodeLimit, NodeCm))                // 0x1039d04b..0x1039d067 (+0x668c) / 0x1039d06e JNZ
			{
				Species19Fail(*this, GSpecies19FileTentacle, 0x2c6, 7);   // 0x1039d076 / 0x1039d080 / 0x1039d08a
				return 0;
			}
		}
		if (!EvadeGoal(NodeCm / ElysiumMove::U, TEXT("CNPC_VMingXiaoTentacle::StartTask 0x152")))   // 0x1039d09d..0x1039d153 0x102ecd20 / 0x1039d15c JZ
		{
			Species19Fail(*this, GSpecies19FileTentacle, 0x2d4, 0xc);     // 0x1039d189 / 0x1039d193 / 0x1039d19d
			return 0;
		}
		TentacleUpdateEvadeTimer = Now + static_cast<double>(GSpecies19TentacleEvadeRearm);   // 0x1039d164 / 0x1039d167 / 0x1039d16d
		TaskComplete(false);                                              // 0x1039d173
		return 0;
	}
	case 0x153:
		NotifyOwnerOfMyMove();                                            // 0x1039d1b2 0x10011eb4 -> 0x1039ef60
		TaskComplete(false);                                              // 0x1039d1bb
		return 0;
	case 0x154:
		RestartIdealActivityId(0x49);                                     // 0x1039d1d1; no completion
		return 0;
	case 0x155:
		TentacleCreateEmitter(TEXT("Ming_xiao_baby_death_emitter"), Origin / ElysiumMove::U);   // 0x1039d1e7 slot 217; 0x1039d206 / 0x1039d211
		TaskComplete(false);                                              // 0x1039d21a
		return 0;
	case 0x156:
		BeginTentacleDefeatOnce();                                        // 0x1039d22e 0x10007266
		TaskComplete(false);                                              // 0x1039d237
		return 0;
	default:
		break;
	}
	return FElysiumNpc::StartTaskSlot442(Task);                          // 0x1039d24c CAI_BaseNPCTroika::StartTask
}

// =================================================================================================
// CNPC_VSabbatLeader -- 0x103a78c0, 3259 bytes
// =================================================================================================

static const TCHAR* const GSpecies19FileSabbatLeader = TEXT("NPC_VSabbatLeader.cpp");     // 0x1064ed7c
static const TCHAR* const GSpecies19SabbatAmbientRun =
	TEXT("Character/Monster/Andrei_Transformed/ambient_run.wav");                         // 0x1064ea40
// The constants the leader's arms read, out of the pinned image (none is in the tunables table):
static constexpr float GSpecies19SabbatDiveJumpHeight = 200.0f;      // `_DAT_104c3cb0`
static constexpr float GSpecies19SabbatLeapHeight = 35.0f;           // `DAT_104c3cb4`
static constexpr float GSpecies19SabbatLeapBackoff = 25.0f;          // `DAT_104c3cb8`
static constexpr float GSpecies19SabbatDiveOutFaceSpeed = 50.0f;     // `DAT_104c3cec`
static constexpr float GSpecies19SabbatWarningLead = 1.0f;           // `_DAT_104c3d04`
static constexpr float GSpecies19SabbatBlastLift = 80.0f;            // `_DAT_104c3d0c`
static constexpr float GSpecies19SabbatBlastRadius = 350.0f;         // `DAT_104c3d10`
static constexpr float GSpecies19SabbatDiveGravity = 0.1f;           // `0x3dcccccd` into `m_flGravity`
// `_DAT_1093c340`, the dive's flight time: `staticinit_103a5820` writes `_DAT_104c3cf4 * _DAT_104c3d2c`
// = 33.0 * (1/30) = 1.1.
static constexpr float GSpecies19SabbatDiveFlightTime = 33.0f * 0.033333335f;
// `m_bfAINPCFlags2` (+0x14bc) bits task `0x15f` raises (`OR ECX,0x80000800`, `0x103a8339`).
static constexpr uint32 GSpecies19SabbatFlags2 = 0x80000800u;

void FElysiumNpcSabbatLeader::SabbatStopSound(int32 Channel, const TCHAR* Wav)
{
	// SEAM for `IEngineSound::StopSound(entindex, channel, wav)` (`DAT_1070b248` slot 7, `+0x1c`),
	// which task `0x158` runs on the ambient run loop. The port's audio seam has no stop by name;
	// recorded.
	SabbatStoppedSounds.Add(FString::Printf(TEXT("%d:%s"), Channel, Wav));
}

void FElysiumNpcSabbatLeader::SabbatCreateEmitter(const TCHAR* Name, const FVector& PositionUnits)
{
	// SEAM for `thunk_FUN_102c41b0(this, name, &position)` (`0x10015c76`), recorded in SOURCE units
	// (the same seam `FElysiumNpcManBat::TeleportEmitterPlacements` records).
	SabbatEmitterPlacements.Add(FTeleportEmitterPlacement{ FString(Name), PositionUnits });
}

// Slot 442: `0x103a78c0`.
int32 FElysiumNpcSabbatLeader::StartTaskSlot442(void* Task)
{
	// A `g_ScopeTraceStack` frame named `"CNPC_VSabbatLeader::StartTask"` wraps the body, and the
	// `AddSolidFlags` call nests its own -- absent.
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	const double Now = Species19Now(*this);
	auto Sound = [this](const TCHAR* Wav)
	{
		// `CPASAttenuationFilter(GetSoundEmissionOrigin(), 0.8)` then `EmitSound(filter, edict, 2, wav,
		// 1.0, 0.8, 0, 100, ...)`.
		EmitNamedWav(this, /*Channel*/ 2, Wav, ElysiumNpcTunables::One, /*Attenuation*/ 0.8f, /*Pitch*/ 100);
	};

	// The prologue, every task: a spawned nova particle outlives only tasks 0x161/0x162.
	if (bSabbatLeaderParticleSpawned && (TaskLocal < 0x161 || TaskLocal > 0x162))   // 0x103a791b +0x66e4; 0x103a7928 / 0x103a792f (0x103a7923 JZ / 0x103a792d JL / 0x103a7934 JLE)
	{
		bSabbatLeaderParticleSpawned = false;                             // 0x103a7938
		KillBodyEmitters();                                               // 0x103a793f 0x1000f673
	}

	switch (TaskLocal) // 0x103a7947 CMP 0x157: 0x103a794c JG / 0x103a7952 JZ (0x157); 0x103a795d JG / 0x103a7963 JZ (0x151); 0x103a796e JG / 0x103a7970 JZ (0xe5); 0x103a7978 JA / 0x103a7982 table (0x36..0x93); 0x103a79e4 JZ (0x14e) / 0x103a79e9 JNZ (0x150); 0x103a7a5c JA / 0x103a7a62 table (0x152..0x156); 0x103a7c09 JA / 0x103a7c0f table (0x158..0x163)
	{
	case 0x36:
	case 0x37:
		AttackSound();                                                    // 0x103a798d CALL [EDX+0x9b4] slot 621
		break;                                                            // -> 0x103a7993 (the base)
	case 0x6e:
	case 0x92:
	case 0x93:
	case 0xe5:
		// The landing: the jump commit, and a body still on the jump nav type is stopped and put back
		// on the ground; then the base.
		CommitSetupJump();                                                // 0x103a79b1 0x10011a31 -> 0x102c4e80
		if (NavGetType() == 1)                                            // 0x103a79b8 0x100023a1 -> 0x1027d990; 0x103a79bd / 0x103a79c0 JNZ
		{
			FUN_102e1270();                                               // 0x103a79ca m_pMotor slot 8
			NavSetType(0);                                                // 0x103a79d1 0x10011e14
			bJumping = false;                                             // 0x103a79d6 +0x6498 = 0
		}
		break;                                                            // -> 0x103a7993
	case 0x14e:
		bSabbatLeaderActivated = true;                                    // 0x103a7a12 +0x66b8 = 1
		bIsBossMonster = true;                                            // 0x103a7a18 +0x6496 = 1
		LastAttackTime = Now;                                             // 0x103a7a27 +0x5d9c = curtime
		break;                                                            // -> 0x103a7993 (the base runs too)
	case 0x150:
		FindHintNode(0x3e80, 2);                                          // 0x103a79eb..0x103a79f4 SelectHintNode 0x103c59d0
		return 0;
	case 0x151:
		FindHintNode(0x3e80, 4);                                          // 0x103a7a32..0x103a7a3b
		return 0;
	case 0x152:
		FindHintNode(0x3e81, 2);                                          // 0x103a7a69..0x103a7a72
		return 0;
	case 0x153:
		FindHintNode(0x3e82, 2);                                          // 0x103a7a8b..0x103a7a94
		return 0;
	case 0x154:
		BaseScheduleHost.HintNode = SelectDiveInPoint();                  // 0x103a7aaf 0x1000194c; 0x103a7ab6
		if (BaseScheduleHost.HintNode == INDEX_NONE)                     // 0x103a7abe
		{
			Species19Fail(*this, GSpecies19FileSabbatLeader, 0x25a, 1);   // 0x103a7ac4 / 0x103a7ace / 0x103a7ad8
			return 0;
		}
		TaskComplete(false);                                              // 0x103a7af4
		return 0;
	case 0x155:
		BaseScheduleHost.HintNode = SelectTeleportArchway();              // 0x103a7b0f 0x1000d585; 0x103a7b18
		TaskComplete(false);                                              // 0x103a7b1e
		return 0;
	case 0x156:
		BaseScheduleHost.HintNode = SelectDiveOutPoint();                 // 0x103a7b39 0x1000d774; 0x103a7b42
		TaskComplete(false);                                              // 0x103a7b48
		return 0;
	case 0x157:
		Sound(GSpecies19SabbatAmbientRun);                                // 0x103a7bdf / 0x103a7be7 / 0x103a7bf9 (the filter: 0x103a7b6a / 0x103a7b76 / 0x103a7b88 / 0x103a7ba3 / 0x103a7bae; torn down with 0x158's at 0x103a7ca3)
		TaskComplete(false);                                              // 0x103a7c9a
		return 0;
	case 0x158:
		SabbatStopSound(2, GSpecies19SabbatAmbientRun);                   // 0x103a7c74 / 0x103a7c86 / 0x103a7c93 slot 7 (its filter: 0x103a7c1f / 0x103a7c2b / 0x103a7c3d / 0x103a7c58 / 0x103a7c63; torn down 0x103a7ca3)
		TaskComplete(false);                                              // 0x103a7c9a
		return 0;
	case 0x159:
		FindHintNode(0x3e83, 2);                                          // 0x103a7cbc..0x103a7cc5
		return 0;
	case 0x15a:
	{
		// The dive-jump set-up at the hint: origin, target, height 200, then complete.
		bSabbatLeaderLastAttackWasNova = false;                           // 0x103a7ce2 +0x66e5 = 0
		SabbatLeaderRouteFailCount = 0;                                   // 0x103a7ce9 +0x66bc = 0
		JumpOrigin = Origin / ElysiumMove::U;                             // 0x103a7cf3 slot 217; 0x103a7cfb..0x103a7d0d +0x649c
		FHintWords Hint;
		if (HintWords(BaseScheduleHost.HintNode, Hint))                   // 0x103a7d13 +0x5ddc (unchecked in retail: crash guard)
		{
			JumpTarget = Hint.OriginCm / ElysiumMove::U;                  // 0x103a7d1b; 0x103a7d25..0x103a7d37 +0x64a8
		}
		JumpHeight = GSpecies19SabbatDiveJumpHeight;                      // 0x103a7d3f / 0x103a7d45 +0x64b4
		bSabbatLeaderTrackPlayer = false;                                 // 0x103a7d4b +0x66d4 = 0
		bSabbatDiving = false;                                            // 0x103a7d52 +0x66d5 = 0
		TaskComplete(false);                                              // 0x103a7d59
		return 0;
	}
	case 0x15b:
	{
		// The leap at the player: a live closest player outside the no-jump zone takes the jump words,
		// the leap sound and `m_bTrackPlayer`; otherwise line 0x28d, `TaskFail(1)`.
		SabbatLeaderRouteFailCount = 0;                                   // 0x103a7d72
		FElysiumEntity* Player = World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;   // 0x103a7d7c..0x103a7db0 (0x103a7d85 JZ / 0x103a7da6 JNZ)
		if (Player == nullptr || PlayerInNoJumpZone())                    // 0x103a7db8 0x10013aca; 0x103a7dbf
		{
			Species19Fail(*this, GSpecies19FileSabbatLeader, 0x28d, 1);   // 0x103a7ebb / 0x103a7ec5 / 0x103a7ecf
			return 0;
		}
		SetJumpOriginAndTarget(Player, GSpecies19SabbatLeapHeight, GSpecies19SabbatLeapBackoff);   // 0x103a7dc5..0x103a7dd5 0x10011784 -> 0x102c4ad0
		Sound(TEXT("Character/Monster/Andrei_Transformed/Leap_Down_Attack_1.wav"));   // 0x103a7e5c / 0x103a7e64 / 0x103a7e76 (the filter: 0x103a7de3 / 0x103a7def / 0x103a7e01 / 0x103a7e1c / 0x103a7e27, torn down 0x103a7e93 / 0x103a7e9c)
		bSabbatLeaderTrackPlayer = true;                                  // 0x103a7e7d +0x66d4 = 1
		bSabbatDiving = false;                                            // 0x103a7e83
		TaskComplete(false);                                              // 0x103a7e8a
		return 0;
	}
	case 0x15c:
	{
		// The dive IN: gravity 0.1, the dive activity, invisible and non-solid, then a ballistic velocity
		// of flight time 1.1 onto the hint, jump nav, `m_bJumping`.
		bSabbatLeaderLastAttackWasNova = false;                           // 0x103a7ef5
		Gravity = GSpecies19SabbatDiveGravity;                            // 0x103a7efc +0x3ec m_flGravity
		SabbatLeaderRouteFailCount = 0;                                   // 0x103a7f06
		bSabbatLeaderTrackPlayer = false;                                 // 0x103a7f10
		bSabbatDiving = true;                                             // 0x103a7f17
		RestartIdealActivityId(0x113f);                                   // 0x103a7f1d
		EffectsWord |= 0x20u;                                             // 0x103a7f28
		SolidFlagsWord |= 0x4u;                                           // 0x103a7f92 OR AL,4; 0x103a7f95 (its scope name test 0x103a7f39 JNZ)
		// `0x103a7fa3` Relink: no spatial partition.
		FHintWords Hint;
		const FVector HintUnits = HintWords(BaseScheduleHost.HintNode, Hint)
			? Hint.OriginCm / ElysiumMove::U : Origin / ElysiumMove::U;   // 0x103a7fb2 / 0x103a7fbc slot 217 (unchecked in retail: crash guard)
		const FVector DeltaUnits = HintUnits - Origin / ElysiumMove::U;   // 0x103a7fc2..0x103a7fdb (0x103a7fac GetAbsOrigin)
		// `VectorNormalize` answers the length; the normalized vector is scaled by `length / 1.1`, which
		// is the delta over the flight time; Z is then REPLACED by the arc's launch speed.
		FVector VelocityUnits = DeltaUnits / GSpecies19SabbatDiveFlightTime;   // 0x103a7ffa / 0x103a8000 FDIV / 0x103a8006..0x103a8016
		const float SvGravityUnits = ElysiumMove::Gravity / ElysiumMove::U;   // 0x103a802a..0x103a804c `sv_gravity` (IsCommand ? 0.0 : m_fValue) (0x103a8038 IsCommand / 0x103a803d JZ)
		VelocityUnits.Z = Gravity * SvGravityUnits * GSpecies19SabbatDiveFlightTime * ElysiumNpcTunables::Half;   // 0x103a804f / 0x103a8055 / 0x103a805e / 0x103a8064
		Velocity = VelocityUnits * ElysiumMove::U;                        // 0x103a8070 SetAbsVelocity
		NavSetType(1);                                                    // 0x103a8078 0x10011e14(1)
		bJumping = true;                                                  // 0x103a807d +0x6498 = 1
		bSabbatLeaderLargeSplash = false;                                 // 0x103a8083 +0x66d6 = 0
		return 0;                                                         // no completion
	}
	case 0x15d:
	{
		// The dive OUT: visible again, face the closest player at speed 50, the splash sound and pool.
		bSabbatDiving = false;                                            // 0x103a80a2
		Unhide();                                                         // 0x103a80a9 slot 67
		// `0x103a80b1` ForceTransmit and `0x103a80b8` Relink: no transmit state or partition.
		EffectsWord |= 0x10u;                                             // 0x103a80c3
		if (FElysiumEntity* Player = World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr)   // 0x103a80cc..0x103a80f8 (0x103a80d5 JZ / 0x103a80f2 JNZ)
		{
			Species19MotorYawSpeedReset(*this);                           // 0x103a8100 0x102e0b40
			Species19MotorIdealYawToAtSpeed(*this, Player->Origin, GSpecies19SabbatDiveOutFaceSpeed);   // 0x103a8105 [0x104c3cec]; 0x103a8115 slot 217; 0x103a811e 0x102e20b0
		}
		Sound(TEXT("Character/Monster/Andrei_Transformed/dive_out_splash.wav"));   // 0x103a81a1 / 0x103a81a9 / 0x103a81bb (the filter: 0x103a812c / 0x103a8138 / 0x103a814a / 0x103a8165 / 0x103a8170)
		FHintWords Hint;
		const FElysiumEntity* HintEntity = World != nullptr && HintWords(BaseScheduleHost.HintNode, Hint)
			&& World->Entities().IsValidIndex(BaseScheduleHost.HintNode)
			? World->Entities()[BaseScheduleHost.HintNode].Get() : nullptr;
		SpawnBloodPoolEmitter(TEXT("andrei_splash_dive_out-emitter"), HintEntity);   // 0x103a81be / 0x103a81c5 / 0x103a81cc 0x1000136b
		RestartIdealActivityId(0x1140);                                   // 0x103a81d8; no completion
		return 0;
	}
	case 0x15e:
	{
		// The warning at the hint: teleport onto it, the warning splash sound, `m_fWarningFinishTime`.
		Species19MotorYawSpeedReset(*this);                               // 0x103a81e8
		FHintWords Hint;
		if (HintWords(BaseScheduleHost.HintNode, Hint))                   // 0x103a81ed (unchecked in retail: crash guard)
		{
			FVector HintOrigin = Hint.OriginCm;
			SetOrigin(HintOrigin);                                     // 0x103a81f7 slot 217; 0x103a8200 slot 216 (0x100b2300 unparented: m_vecOrigin; slot 216 is an unported stub, slot 62's body is the one origin write)
			FVector Placed = Origin;                                      // 0x103a820a slot 220
			Teleport(&Placed, nullptr, nullptr);                          // 0x103a8231 slot 181
			SetOrigin(Placed);                                            // 0x103a8240 slot 62
		}
		bSabbatDiving = false;                                            // 0x103a824f
		Sound(TEXT("Character/Monster/Andrei_Transformed/splash_warning.wav"));   // 0x103a82cb / 0x103a82d3 / 0x103a82e5 (the filter: 0x103a8256 / 0x103a8262 / 0x103a8274 / 0x103a828f / 0x103a829a, torn down 0x103a82ec / 0x103a82f5 / 0x103a8306)
		SabbatWarningFinishTime = Now + static_cast<double>(GSpecies19SabbatWarningLead);   // 0x103a8312 / 0x103a8318 / 0x103a831b +0x66dc
		return 0;                                                         // no completion
	}
	case 0x15f:
		NpcFlags.SetRawWord2Bits(GSpecies19SabbatFlags2);                 // 0x103a8331..0x103a833f +0x14bc
		TaskComplete(false);                                              // 0x103a8347
		return 0;
	case 0x160:
		Sound(TEXT("Character/Monster/Andrei_Transformed/roar_1.wav"));   // 0x103a83dc / 0x103a83e4 / 0x103a83f6 (the filter: 0x103a8369 / 0x103a8375 / 0x103a8387 / 0x103a83a2 / 0x103a83ad; its teardown loop 0x103a840e JL / 0x103a8417 / 0x103a841d JNZ, 0x103a8436 JZ / 0x103a843a JZ / 0x103a843d, 0x103a8456 JZ / 0x103a845e JZ / 0x103a8465)
		RestartIdealActivityId(0x1141);                                   // 0x103a8400; no completion
		return 0;
	case 0x161:
		bSabbatLeaderLastAttackWasNova = true;                            // 0x103a8485 +0x66e5 = 1
		VampireBossSpawnBodyEmitters();                                   // 0x103a848b 0x1001177f
		bSabbatLeaderParticleSpawned = true;                              // 0x103a8497 +0x66e4 = 1
		RestartIdealActivityId(0x1142);                                   // 0x103a849d
		return 0;
	case 0x162:
		RestartIdealActivityId(0x1143);                                   // 0x103a84bd
		return 0;
	case 0x163:
	{
		// The nova blast: the emitter 80 above `GetAbsOrigin`, the AOE there when the template
		// resolves, the attack activity and the attack stamp.
		FVector BlastUnits = Origin / ElysiumMove::U;                     // 0x103a84da slot 217
		BlastUnits.Z += GSpecies19SabbatBlastLift;                        // 0x103a84e4 / 0x103a8500
		SabbatCreateEmitter(TEXT("Andrei_blast_emitter"), BlastUnits);   // 0x103a8507 / 0x103a8512 0x10015c76
		if (Species19CharTemplate(*this) >= 0)                            // 0x103a8519; 0x103a8520 JL
		{
			CausePlayerAOEDamage(BlastUnits, GSpecies19SabbatBlastRadius);   // 0x103a8523 row; 0x103a852d +0xcc; 0x103a8548 (third word dropped, see ChangBros 0x15d) (the template row 0x103a8528 0x10014e57)
		}
		RestartIdealActivityId(0x1144);                                   // 0x103a8554
		LastAttackTime = Now;                                             // 0x103a8562
		return 0;
	}
	default:
		break;
	}
	return FElysiumNpcVampireBoss::StartTaskSlot442(Task);               // 0x103a7996 0x1000de7c
}

// =================================================================================================
// CNPC_VScurrying -- 0x103ac740, 635 bytes (also CNPC_VRat's)
// =================================================================================================

static const TCHAR* const GSpecies19FileScurrying = TEXT("NPC_VScurrying.cpp");           // 0x106502e0

// Slot 442: `0x103ac740`. Also carries the inherited body of CNPC_VRat.
int32 FElysiumNpcScurrying::StartTaskSlot442(void* Task)
{
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	if (TaskLocal == 0xbc)                                               // 0x103ac74d SUB 0xbc; JZ 0x103ac9a7 (0x103ac752)
	{
		SetActivity(3);                                                   // 0x103ac9ad slot 310; no completion
		return 0;
	}
	if (TaskLocal == 0x14b)                                              // 0x103ac75f DEC; JZ 0x103ac772 (0x103ac760)
	{
		TaskComplete(false);                                              // 0x103ac776
		return 0;
	}
	if (TaskLocal != 0x14a)                                              // 0x103ac758 SUB 0x8e; JZ 0x103ac783 (0x103ac75d)
	{
		return FElysiumNpcAnimal::StartTaskSlot442(Task);                // 0x103ac765
	}

	// The flee. A scarer that does not resolve AND a scare stamp curtime has passed: line 0xe9,
	// `TaskFail(6)`. `FCOMP` + `AND 0x4100` + `JNZ` proceeds while curtime <= the stamp.
	const double Now = Species19Now(*this);
	FElysiumEntity* Scarer = World != nullptr ? World->Resolve(ScurryingScarer) : nullptr;   // 0x103ac783..0x103ac7b1 (0x103ac792 JZ / 0x103ac7ac JNZ)
	if (Scarer == nullptr && !(Now <= ScurryingScareExpiry))              // 0x103ac7b8 FLD curtime; 0x103ac7bb FCOMP [+0x668c]; 0x103ac7c8
	{
		Species19Fail(*this, GSpecies19FileScurrying, 0xe9, 6);          // 0x103ac7d0 / 0x103ac7da / 0x103ac7e4
		return 0;
	}
	float FleeDistanceUnits = ScurryingFrightDistanceUnits;               // 0x103ac7f2 +0x6698
	FVector FleeFromCm = ScurryingScarePointCm;                           // 0x103ac7f8..0x103ac81b +0x6680..+0x6688
	if (Scarer != nullptr)                                                // 0x103ac81f..0x103ac83e (the handle re-resolved) (0x103ac839 JNZ; the third resolve 0x103ac855 JZ / 0x103ac86c JNZ)
	{
		FleeDistanceUnits = ScurryingDetectionDistanceUnits + ScurryingDetectionDistanceUnits;   // 0x103ac840 FLD +0x6690; 0x103ac84c FADD ST0,ST0
		FleeFromCm = Scarer->Origin;                                      // 0x103ac876 slot 217 GetAbsOrigin; 0x103ac87c..0x103ac88c
	}
	FVector DestinationCm = FVector::ZeroVector;
	if (!ScurryingFindFleeDestination(FleeFromCm, FleeDistanceUnits, &DestinationCm))   // 0x103ac8a1 0x103acba0; 0x103ac8a8
	{
		Species19Fail(*this, GSpecies19FileScurrying, 0xfa, 7);          // 0x103ac8b0 / 0x103ac8ba / 0x103ac8c4
		return 0;
	}
	// Goal type 4 at the destination, activity word `0x13` (ACT_RUN, record `+0x14`), the other three
	// words -1, tolerance -1.0 (`0x104c4910`), flags 0; `SetGoal(record, 0)`.
	if (Species19SetGoal(*this, 4, DestinationCm, 0x13, -1.f, 0))                   // 0x103ac8d2..0x103ac967 0x102ecd20 / 0x103ac970 JZ
	{
		TaskComplete(false);                                              // 0x103ac974
		return 0;
	}
	Species19Fail(*this, GSpecies19FileScurrying, 0x107, 0xc);            // 0x103ac985 / 0x103ac98f / 0x103ac999
	return 0;
}

// =================================================================================================
// CNPC_VSheriffMan -- 0x103aec70, 2170 bytes
// =================================================================================================

static const TCHAR* const GSpecies19FileSheriffMan = TEXT("npc_vsheriffman.cpp");         // 0x10651388
// `DAT_104c6154` -- 300.0 (`0x43960000`), the land-blast AOE radius (not in the tunables table).
static constexpr float GSpecies19SheriffBlastRadius = 300.0f;

void FElysiumNpcSheriffMan::SheriffCreateEmitter(const TCHAR* Name, const FVector& PositionUnits)
{
	// SEAM for `thunk_FUN_102c41b0(this, name, &position)` (`0x10015c76`), the sheriff's named particle
	// emitters (the same seam `FElysiumNpcManBat::TeleportEmitterPlacements` records). SOURCE units.
	SheriffEmitterPlacements.Add(FTeleportEmitterPlacement{ FString(Name), PositionUnits });
}

void FElysiumNpcSheriffMan::SheriffChooseBestMeleeWeapon()
{
	// `CBaseCombatCharacter::ChooseBestMeleeWeapon` (`0x1000600a` -> `0x10337230`), answer unread: family
	// StartTask19's one stand for that address, `FElysiumNpcBase::StartTaskChooseBestMeleeWeapon` (a
	// seam: no melee pick yet). Counted here for this class's tests.
	++ChooseBestMeleeWeaponCalls;
	(void)StartTaskChooseBestMeleeWeapon();
}

void FElysiumNpcSheriffMan::ShowAndSolidifyWeapon(const FElysiumEntityHandle& Weapon)
{
	// SEAM, the inverse of `HideAndUnsolidifyWeapon`: `m_fEffects &= ~0x20`, `RemoveSolidFlags(4)`,
	// `ForceTransmit` and `Relink` on the weapon. No `FElysiumEntity` word stands for either mask.
	FHideAndUnsolidifyCall Call;
	Call.Entity = Weapon;
	Call.EffectBits = 0x20u;
	Call.SolidBits = 0x4u;
	ShowAndSolidifyCalls.Add(Call);
}

bool FElysiumNpcSheriffMan::SheriffStandTraceStartSolid() const
{
	// SEAM for task `0x158`'s `UTIL_TraceHull(GetAbsOrigin, GetAbsOrigin, collision mins/maxs, mask
	// 0x202400b, CTraceFilterWorldOnly)` (`DAT_1070b254` slot 4) and its `startsolid` byte. No hull
	// trace at the kernel tier; the port's trace seams report a clear line, so this answers false.
	return false;
}

// Slot 442: `0x103aec70`.
int32 FElysiumNpcSheriffMan::StartTaskSlot442(void* Task)
{
	// A `g_ScopeTraceStack` frame named `"CNPC_VSheriffMan::StartTask"` wraps the body, and every
	// `AddSolidFlags`/`RemoveSolidFlags`/`UTIL_TraceHull` nests its own -- absent.
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	switch (TaskLocal)                                                   // 0x103aecd3 ADD -0x13b; JA 0x103af4cb (0x103aecdb; 0x103aece9 table jump)
	{
	case 0x13b:
	{
		// The land blast: the emitter at `GetAbsOrigin`, then the AOE there when the template resolves;
		// the base runs either way.
		const FVector OriginUnits = Origin / ElysiumMove::U;              // 0x103af466 slot 217
		SheriffCreateEmitter(TEXT("sheriff_landblast_emitter"), OriginUnits);   // 0x103af485 / 0x103af490
		if (Species19CharTemplate(*this) >= 0)                            // 0x103af497 GetCharTemplate; 0x103af49e JL
		{
			// `CausePlayerAOEDamage(&origin, 300.0, template->+0xcc)`; the port's body has no third
			// parameter (see `ChangBros` task `0x15d`).
			CausePlayerAOEDamage(OriginUnits, GSpecies19SheriffBlastRadius);   // 0x103af4a6 row; 0x103af4ab +0xcc; 0x103af4c6
		}
		break;                                                            // -> 0x103af4cb (the base)
	}
	case 0x150:
	{
		// Teleport out: `m_bTeleporting = 1`; the active weapon hidden and made non-solid; the teleport
		// emitter at `GetAbsOrigin`; then this body hidden, `EF_NODRAW`, non-solid. No completion.
		bSheriffTeleporting = true;                                       // 0x103aecf2 +0x66e4 = 1
		if (FElysiumEntity* Weapon = ActiveWeaponEntity())                // 0x103aecf9; 0x103aed07
		{
			Weapon->Hide();                                               // 0x103aed11 slot 66 on the weapon
			HideAndUnsolidifyWeapon(Weapon->Handle);                      // 0x103aed1d / 0x103aed89 / 0x103aed9a (its scope name test 0x103aed2d JNZ)
		}
		SheriffCreateEmitter(TEXT("sheriff_teleport_emitter"), Origin / ElysiumMove::U);   // 0x103aeda3 slot 217; 0x103aedc2 / 0x103aedcd
		Hide();                                                           // 0x103aedd6 slot 66
		EffectsWord |= 0x20u;                                             // 0x103aede2
		SolidFlagsWord |= 0x4u;                                           // 0x103aee4b OR AL,4; 0x103aee4e (its scope name test 0x103aedf2 JNZ)
		// `0x103aee5f` Relink: no spatial partition.
		return 0;
	}
	case 0x151:
		BaseScheduleHost.HintNode = SelectTeleportNodeSheriff();          // 0x103af08f 0x100117c5; 0x103af096
		if (BaseScheduleHost.HintNode == INDEX_NONE)                     // 0x103af09c
		{
			Species19Fail(*this, GSpecies19FileSheriffMan, 0x192, 1);     // 0x103af0a2 / 0x103af192 / 0x103af19c
		}
		TaskComplete(false);                                              // 0x103af1a6
		return 0;
	case 0x152:
		if (BaseScheduleHost.HintNode == INDEX_NONE)                     // 0x103af0b1 / 0x103af0b9
		{
			BaseScheduleHost.HintNode = SelectTeleportNodeSheriff();      // 0x103af0c1; 0x103af0c8
			if (BaseScheduleHost.HintNode == INDEX_NONE)                 // 0x103af0ce
			{
				Species19Fail(*this, GSpecies19FileSheriffMan, 0x19c, 1); // 0x103af0da / 0x103af0e4 / 0x103af0ee
			}
		}
		return 0;                                                         // no completion
	case 0x153:
	{
		// Teleport in, in retail's order.
		FHintWords Hint;
		if (BaseScheduleHost.HintNode != INDEX_NONE && HintWords(BaseScheduleHost.HintNode, Hint))   // 0x103aee69 / 0x103aee71
		{
			FVector HintOrigin = Hint.OriginCm;
			SetOrigin(HintOrigin);                                     // 0x103aee77 slot 217; 0x103aee80 slot 216 (0x100b2300 unparented: m_vecOrigin; slot 216 is an unported stub, slot 62's body is the one origin write)
			FVector Placed = Origin;                                      // 0x103aee8a slot 220
			Teleport(&Placed, nullptr, nullptr);                          // 0x103aeeb1 slot 181
			SetOrigin(Placed);                                            // 0x103aeec0 slot 62
		}
		if (FElysiumEntity* Weapon = ActiveWeaponEntity())                // 0x103aeec8; 0x103aeed6
		{
			Weapon->Unhide();                                             // 0x103aeee0 slot 67 on the weapon
			ShowAndSolidifyWeapon(Weapon->Handle);                        // 0x103aeeec / 0x103aef58 / 0x103aef69 / 0x103aef70 (its scope name test 0x103aeefc JNZ)
		}
		Unhide();                                                         // 0x103aef79 slot 67
		EffectsWord &= ~0x20u;                                            // 0x103aef85
		SolidFlagsWord &= ~0x4u;                                          // 0x103aefee AND AL,0xfb; 0x103aeff1 (its scope name test 0x103aef95 JNZ)
		// `0x103aefff` ForceTransmit and `0x103af006` Relink: no transmit state or spatial partition.
		SheriffChooseBestMeleeWeapon();                                   // 0x103af00d 0x1000600a
		KillTeleportBats();                                               // 0x103af014 0x10012e0e
		SheriffMatchOriginAnglesCalls.Add(FMatchOriginAnglesCall{ TEXT("bip01"), true, true });   // 0x103af024 0x1000577c("bip01", 1, 1)
		bSheriffTeleporting = false;                                      // 0x103af034 +0x66e4 = 0
		EffectsWord |= 0x10u;                                             // 0x103af031 / 0x103af03b
		RecordHealthPercent();                                            // 0x103af041 0x103c6a00
		SheriffCreateEmitter(TEXT("sheriff_teleport_emitter"), Origin / ElysiumMove::U);   // 0x103af04a slot 217; 0x103af069 / 0x103af074
		LastAttackTime = Species19Now(*this);                             // 0x103af082 +0x5d9c = curtime
		return 0;                                                         // no completion
	}
	case 0x154:
		FireOutput(FName(TEXT("OnFinishTransformation")), Handle);        // 0x103af0f9..0x103af103 0x10010794(+0x66b8, this, this, 0)
		return 0;
	case 0x155:
		BaseScheduleHost.HintNode = SelectCenterNode();                   // 0x103af10f 0x1000c257; 0x103af116
		if (BaseScheduleHost.HintNode == INDEX_NONE)                     // 0x103af11c
		{
			Species19Fail(*this, GSpecies19FileSheriffMan, 0x1ac, 1);     // 0x103af122 / 0x103af192 / 0x103af19c
		}
		TaskComplete(false);                                              // 0x103af1a6
		return 0;
	case 0x156:
		BaseScheduleHost.HintNode = SelectLedgeNode(false);               // 0x103af12e PUSH 0; 0x103af132 0x10006e74
		if (BaseScheduleHost.HintNode == INDEX_NONE)                     // 0x103af13f
		{
			Species19Fail(*this, GSpecies19FileSheriffMan, 0x1b4, 1);     // 0x103af147 / 0x103af151 / 0x103af15b
		}
		TaskComplete(false);                                              // 0x103af165
		return 0;
	case 0x157:
		BaseScheduleHost.HintNode = SelectLedgeNode(true);                // 0x103af16f PUSH 1; 0x103af173
		if (BaseScheduleHost.HintNode == INDEX_NONE)                     // 0x103af180
		{
			Species19Fail(*this, GSpecies19FileSheriffMan, 0x1bc, 1);     // 0x103af182 / 0x103af192 / 0x103af19c
		}
		TaskComplete(false);                                              // 0x103af1a6
		return 0;
	case 0x158:
		// The stand check: a start-solid hull at `GetAbsOrigin` stamps line 0x1c8 and fails 1 -- and
		// the body STILL sets the jump up, stamps the attack time and completes (the completion is then
		// refused by `COND_TASK_FAILED`).
		if (SheriffStandTraceStartSolid())                                // 0x103af1b0..0x103af3bf trace; 0x103af40f [ESP+0x9b] / 0x103af418 JZ. Inside the seam: collision mins/maxs 0x103af1c6 / 0x103af1cf, GetAbsOrigin 0x103af1d8 / 0x103af1e4, Ray_t::Init 0x103af278 JNP / 0x103af2e7 JP / 0x103af37b JNZ / 0x103af396 JNZ, and the debug-overlay box (0x103af3ca / 0x103af3cf JNZ / 0x103af3db JZ / 0x103af3fd), which stays absent
		{
			Species19Fail(*this, GSpecies19FileSheriffMan, 0x1c8, 1);     // 0x103af420 / 0x103af42a / 0x103af434
		}
		SheriffManSetupJump(BaseScheduleHost.HintNode != INDEX_NONE ? 1.f : 0.f);   // 0x103af43a +0x5ddc; 0x103af443 0x1000ec46 (the hint POINTER as the float)
		LastAttackTime = Species19Now(*this);                             // 0x103af455
		TaskComplete(false);                                              // 0x103af45b
		return 0;
	default:
		break;
	}
	return FElysiumNpcVampireBoss::StartTaskSlot442(Task);               // 0x103af4ce 0x1000de7c
}

// =================================================================================================
// CNPC_VTzimisce -- 0x103ba7c0, 1759 bytes
// =================================================================================================

static const TCHAR* const GSpecies19FileTzimisce = TEXT("NPC_VTzimisce.cpp");             // 0x1065c7e0
// `_DAT_10457f60` -- 150.0, the inside-interrupt pad task `0xd1` adds (not in the tunables table).
static constexpr float GSpecies19TzimisceInsidePad = 150.0f;

void FElysiumNpcTzimisce::FUN_103bf440()
{
	// SEAM for `0x103bf440`, the carry facing (`GetAngles().y` seeded into the motor at speed 20.0,
	// then a yaw at the enemy or the angles held) -- the Tzimisce twin of the hengeyokai's
	// `0x10382bb0`. Not this lane's row; counted.
	++Fun103bf440Calls;
}

bool FElysiumNpcTzimisce::FUN_103bf660()
{
	// `0x103bf660`, the pounce check, is family Conditions19's `TzimiscePounceTest` (the enemy's lead
	// position, the squared band, the hull trace that must end ON the enemy); counted for the tests.
	++Fun103bf660Calls;
	return TzimiscePounceTest();
}

// Slot 442: `0x103ba7c0`.
int32 FElysiumNpcTzimisce::StartTaskSlot442(void* Task)
{
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	// The shared "translate the ideal activity and compare it with `m_Activity`" tail
	// (`0x103bad14`..`0x103bad3a`): slot 375 then slot 381 then slot 376.
	auto CompleteWhenTranslated = [this](int32 Activity)
	{
		const int32 Early = NPC_EarlyTranslateActivity(Activity);        // 0x103bad18 CALL [EAX+0x5dc]
		const int32 Weapon = Weapon_TranslateActivity(Early);            // 0x103bad23 CALL [EDX+0x5f4]
		const int32 Final = NPC_TranslateActivity(Weapon);               // 0x103bad2e CALL [EDX+0x5e0]
		if (ActivityNumber == Final)                                      // 0x103bad34 CMP [ESI+0xfec]
		{
			TaskComplete(false);                                          // 0x103bad44
		}
	};
	switch (TaskLocal)                                                   // 0x103ba7cd ADD -3; JA 0x103ba817 (0x103ba7d6; 0x103ba7e0 table jump)
	{
	case 0x3:
		TaskComplete(false);                                              // 0x103bad40 / 0x103bad44
		return 0;
	case 0xf:
		PathMode = 1;                                                     // 0x103ba80d +0x668c = 1
		break;                                                            // FALLS INTO the base 0x103ba817
	case 0x89:
		LastAttackTime = Species19Now(*this);                             // 0x103ba831
		RestartIdealActivityId(0x4b);                                     // 0x103ba839
		return 0;
	case 0x8a:
		LastAttackTime = Species19Now(*this);                             // 0x103ba853
		RestartIdealActivityId(0x4b);                                     // 0x103ba859
		return 0;
	case 0x8b:
		RestartIdealActivityId(0x50);                                     // 0x103ba86a
		return 0;
	case 0x8e:
		RestartIdealActivityId(0x52);                                     // 0x103ba87b
		return 0;
	case 0xa7:
	{
		const FElysiumEntity* LkpKey = static_cast<const FElysiumNpc*>(this)->GetEnemy();          // 0x103ba7eb slot 167
		FVector EnemyLkp = FVector::ZeroVector;
		EnemyLkp = Conditions19LastKnownPosition(LkpKey);                                 // 0x103ba7fb slot 541; 0x103ba803 0x102dfed0
		Species19MotorYawSpeedReset(*this);                               // 0x103baba3 0x102e0b40
		Species19MotorIdealYawTo(*this, EnemyLkp);                        // 0x103babb5 0x102e2020
		SetTurnActivity();                                                // 0x103babbe slot 572
		return 0;
	}
	case 0xbf:
		Species19MotorYawSpeedReset(*this);                               // 0x103ba88e
		FUN_103bf440();                                                   // 0x103ba895 0x10009575
		RestartIdealActivityId(0xf2);                                     // 0x103ba8a1
		return 0;
	case 0xc0:
		Species19MotorYawSpeedReset(*this);                               // 0x103ba8b4
		FUN_103bf440();                                                   // 0x103ba8bb
		RestartIdealActivityId(0xf4);                                     // 0x103ba8c7
		return 0;
	case 0xc1:
		RestartIdealActivityId(bHeavyBodyTarget ? 0xf6 : 0xf8);           // 0x103ba8d4 +0x6688; 0x103ba8e5 / 0x103ba8f7 (0x103ba8de JZ)
		return 0;
	case 0xc2:
		VGargoyleGibCleanup();                                            // 0x103ba906 0x100043b3 -> 0x103bf170
		TaskComplete(false);                                              // 0x103ba90f
		return 0;
	case 0xc3:
	{
		// `m_ePathMode = 2`; a live pickup target: `m_vSavePosition = m_vecPickupTargetPos`, the lead
		// helper `0x102c3b50` over the target (writing the save position and `m_flGoalTolerance`), then
		// the tolerance into the navigator twice and `TaskComplete`. Dead: line 0x722, `TaskFail(1)`.
		PathMode = 2;                                                     // 0x103ba91c
		FElysiumEntity* PickupEntity = World != nullptr ? World->Resolve(PickupTarget) : nullptr;   // 0x103ba926..0x103ba95a (0x103ba92f JZ / 0x103ba950 JNZ)
		if (PickupEntity == nullptr)
		{
			Species19Fail(*this, GSpecies19FileTzimisce, 0x722, 1);       // 0x103baa10 / 0x103baa1a / 0x103baa24
			return 0;
		}
		SavePosition = PickupTargetPos * ElysiumMove::U;                  // 0x103ba960..0x103ba97d (+0x6674 is SOURCE units)
		float ToleranceUnits = ScheduleHost.GoalToleranceCm / ElysiumMove::U;
		ChaseLeadTolerance(PickupEntity, SavePosition, ToleranceUnits);         // 0x103ba9d8 0x100041d3 -> 0x102c3b50, &+0x6320 (its handle re-resolved 0x103ba989 JZ / 0x103ba9a6 JNZ)
		ScheduleHost.GoalToleranceCm = ToleranceUnits * ElysiumMove::U;   // the helper's out-word, `LEA EDI,[ESI+0x6320]` 0x103ba9ae
		StartTask19SetNavTolerances(ToleranceUnits, ToleranceUnits);      // 0x103ba9e6 0x102ee1c0 / 0x103ba9f4 0x102f2fe0, both [EDI]
		TaskComplete(false);                                              // 0x103ba9fd
		return 0;
	}
	case 0xc8:
	{
		if (FElysiumEntity* PickupEntity = World != nullptr ? World->Resolve(PickupTarget) : nullptr)   // 0x103baa32..0x103baa5d (0x103baa3b JZ / 0x103baa58 JNZ; the re-resolve 0x103baa73 JZ / 0x103baa90 JNZ)
		{
			Species19MotorYawSpeedReset(*this);                           // 0x103baa65
			Species19MotorIdealYawTo(*this, PickupEntity->Origin);              // 0x103baaa2 slot 220; 0x103baaab 0x102e2020
			SetTurnActivity();                                            // 0x103baab4 slot 572
		}
		if (FacingIdeal())                                                // 0x103baabc 0x10278c80; 0x103baac3
		{
			TaskComplete(false);                                          // 0x103baacd
		}
		return 0;
	}
	case 0xc9:
	{
		FElysiumEntity* Enemy = static_cast<const FElysiumNpc*>(this)->GetEnemy();   // 0x103baade slot 167
		FVector EnemyLkp = FVector::ZeroVector;
		EnemyLkp = Conditions19LastKnownPosition(Enemy);                                 // 0x103baaf0 / 0x103baaf8 -> 0x102dfed0
		if (Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(0x1b))  // 0x103bab01 HasCondition(0x1b) / 0x103bab08 JZ
			&& FUN_103be8e0(Enemy))                                       // 0x103bab0d 0x1000f6aa / 0x103bab14 JZ
		{
			TaskComplete(false);                                          // 0x103bab1a
			return 0;
		}
		Species19MotorYawSpeedReset(*this);                               // 0x103bab2d
		Species19MotorIdealYawTo(*this, EnemyLkp);                        // 0x103bab3f 0x102e2020
		SetTurnActivity();                                                // 0x103bab48
		TzimisceTaskFailTimer = Species19Now(*this) + static_cast<double>(ElysiumNpcTunables::One);   // 0x103bab53 / 0x103bab56 / 0x103bab5c +0x66a8
		return 0;
	}
	case 0xca:
	{
		FElysiumEntity* EnemyEntity = GetEnemy();                              // 0x103bab6e slot 168
		FVector TargetLkp = FVector::ZeroVector;
		TargetLkp = Conditions19LastKnownPosition(EnemyEntity);                                // 0x103bab80 / 0x103bab88 -> 0x102dfed0
		if (FUN_103be8e0(EnemyEntity))                                         // 0x103bab90; 0x103bab97 JNZ 0x103bad40
		{
			TaskComplete(false);                                          // 0x103bad44
			return 0;
		}
		Species19MotorYawSpeedReset(*this);                               // 0x103baba3
		Species19MotorIdealYawTo(*this, TargetLkp);                       // 0x103babb5
		SetTurnActivity();                                                // 0x103babbe
		return 0;
	}
	case 0xcb:
	{
		AutoMovement();                                                   // 0x103babce
		Species19MotorYawSpeedReset(*this);                               // 0x103babd9
		const FElysiumEntity* LkpKey = static_cast<const FElysiumNpc*>(this)->GetEnemy();          // 0x103babe2 slot 167
		FVector EnemyLkp = FVector::ZeroVector;
		EnemyLkp = Conditions19LastKnownPosition(LkpKey);                                 // 0x103babf2 / 0x103babfa -> 0x102dfed0
		Species19MotorIdealYawToAtSpeed(*this, EnemyLkp, -2.0f);          // 0x103bac0f 0x102e20b0(0xc0000000)
		LastAttackTime = Species19Now(*this);                             // 0x103bac24
		RestartIdealActivityId(0x102);                                    // 0x103bac2a
		if (IsActivityFinished())                                         // 0x103bac33; 0x103bac3b
		{
			TaskComplete(false);                                          // 0x103bac45
		}
		return 0;
	}
	case 0xcc:
	{
		if (static_cast<const FElysiumNpc*>(this)->GetEnemy() == nullptr) // 0x103bac56 slot 167; 0x103bac60
		{
			Species19Fail(*this, GSpecies19FileTzimisce, 0x78c, 6);       // 0x103bac66 / 0x103bac70 / 0x103bac7a
			return 0;
		}
		AutoMovement();                                                   // 0x103bac88
		const FElysiumEntity* LkpKey = static_cast<const FElysiumNpc*>(this)->GetEnemy();          // 0x103bac91
		FVector EnemyLkp = FVector::ZeroVector;
		EnemyLkp = Conditions19LastKnownPosition(LkpKey);                                 // 0x103baca1 / 0x103baca9 -> 0x102dfed0
		if (!FUN_103bf660())                                              // 0x103bacb5 0x10013769; 0x103bacbc
		{
			Species19Fail(*this, GSpecies19FileTzimisce, 0x7ab, 0x1a);    // 0x103bad57 / 0x103bad61 / 0x103bad6b
			return 0;
		}
		Species19MotorYawSpeedReset(*this);                               // 0x103bacc8
		Species19MotorIdealYawToAtSpeed(*this, EnemyLkp, -2.0f);          // 0x103bacdd
		LastAttackTime = Species19Now(*this);                             // 0x103bacf2
		RestartIdealActivityId(0x103);                                    // 0x103bacf8
		if (IsActivityFinished())                                         // 0x103bad01; 0x103bad09
		{
			CompleteWhenTranslated(0x103);                                // 0x103bad0f
		}
		return 0;
	}
	case 0xcd:
	{
		if (static_cast<const FElysiumNpc*>(this)->GetEnemy() == nullptr) // 0x103bad7d; 0x103bad87
		{
			Species19Fail(*this, GSpecies19FileTzimisce, 0x7b5, 6);       // 0x103bad8d / 0x103bad97 / 0x103bada1
			return 0;
		}
		AutoMovement();                                                   // 0x103badaf
		const FElysiumEntity* LkpKey = static_cast<const FElysiumNpc*>(this)->GetEnemy();          // 0x103badb8
		FVector EnemyLkp = FVector::ZeroVector;
		EnemyLkp = Conditions19LastKnownPosition(LkpKey);                                 // 0x103badc8 / 0x103badd0 -> 0x102dfed0
		Species19MotorYawSpeedReset(*this);                               // 0x103baddb
		Species19MotorIdealYawToAtSpeed(*this, EnemyLkp, -2.0f);          // 0x103badf0
		RestartIdealActivityId(0x104);                                    // 0x103badfc
		if (IsActivityFinished())                                         // 0x103bae05; 0x103bae0d
		{
			CompleteWhenTranslated(0x104);                                // 0x103bae13 / 0x103bae18
		}
		return 0;
	}
	case 0xce:
		AutoMovement();                                                   // 0x103bae1f
		RestartIdealActivityId(0x105);                                    // 0x103bae2b
		if (IsActivityFinished())                                         // 0x103bae34; 0x103bae3c
		{
			CompleteWhenTranslated(0x105);                                // 0x103bae44 / 0x103bae4b
		}
		return 0;
	case 0xd1:
	{
		// `m_flInsideInterruptDistanceSqr = (ResolveTaskDistance(data) + 150.0)^2`, Source units.
		const float Pad = ResolveTaskDistance(Step->Data) + GSpecies19TzimisceInsidePad;   // 0x103bae5e slot 418; 0x103bae64 FADD
		ScheduleHost.InsideInterruptDistanceSqr = Pad * Pad;              // 0x103bae6e FLD ST0; FMUL ST1; 0x103bae72 +0x6324
		TaskComplete(false);                                              // 0x103bae7a
		return 0;
	}
	case 0xd2:
		SetIdealActivity(static_cast<int32>(Step->Data));                 // 0x103bae87 FLD data; 0x103bae8a __ftol; 0x103bae92 0x10272650
		return 0;
	default:
		break;
	}
	return FElysiumNpc::StartTaskSlot442(Task);                          // 0x103ba81a CAI_BaseNPCTroika::StartTask
}

// =================================================================================================
// CNPC_VVampireBoss -- 0x103c5ac0, 911 bytes (the shared boss body)
// =================================================================================================

static const TCHAR* const GSpecies19FileVampireBoss = TEXT("npc_VVampireBoss.cpp");       // 0x1065ec20
// `m_fEffects` (+0x19c) bits this body moves: `0x20` (EF_NODRAW, the word family Positions carries
// as `EffectsWord`) and `0x10`.
static constexpr uint32 GSpecies19EffectNoDraw = 0x20u;
static constexpr uint32 GSpecies19EffectBit10 = 0x10u;
// `m_Collision` (+0x270) solid flags `+0x44`: `FSOLID_NOT_SOLID` (`AND AL,0xfb` at `0x103c5c66`).
static constexpr uint32 GSpecies19SolidNotSolid = 0x4u;

void FElysiumNpcVampireBoss::TransformationStartSlot618()
{
	// SEAM for slot 618 on the vampire-boss line: `CNPC_VVampireBoss::TransformationStart`
	// `0x103c60a0` (the boss body swap, family Spawn19's `rule` row) and `CNPC_SabbatLeader`'s
	// `0x103ab310` (also Spawn19's). Neither is this lane's row; the call site is, and it dispatches
	// VIRTUALLY (`CALL [EDX+0x9a8]`), so this is the virtual the integrator binds those bodies to.
	++TransformationStartCalls;
}

// Slot 442: `0x103c5ac0`. Also the direct parent body of CNPC_VAndreiBlood, CNPC_VAsianVampire,
// CNPC_VChangBros, CNPC_VSabbatLeader and CNPC_VSheriffMan.
int32 FElysiumNpcVampireBoss::StartTaskSlot442(void* Task)
{
	// A `g_ScopeTraceStack` frame named `"CNPC_VVampireBoss::StartTask"` wraps the body
	// (`0x103c5ac0`..`0x103c5b14`, popped on every exit) -- the debug ring, absent.
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const double Now = Species19Now(*this);
	VampireBossTaskStartTime = Now;                                       // 0x103c5b1e +0x669c = curtime, every task
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	switch (TaskLocal)                                                   // 0x103c5b2a CMP 0x14c; JG 0x103c5d5a (0x103c5b2f; 0x103c5b35 JZ 0x14c / 0x103c5b3e JZ 0x2f / 0x103c5b49 JZ 0x14a / 0x103c5b50 JNZ 0x14b; past 0x14c 0x103c5d66 JZ 0x14e / 0x103c5d69 JZ 0x14f)
	{
	case 0x2f:
	{
		// Face the claimed hint: motor reset, ideal yaw at the hint's slot 217 `GetAbsOrigin`, slot 572
		// `SetTurnActivity`; the task stays running.
		Species19MotorYawSpeedReset(*this);                               // 0x103c5d0e 0x102e0b40
		FHintWords Hint;
		if (HintWords(BaseScheduleHost.HintNode, Hint))                   // 0x103c5d13 +0x5ddc (no null test: crash guard)
		{
			Species19MotorIdealYawTo(*this, Hint.OriginCm);               // 0x103c5d23 slot 217; 0x103c5d2c 0x102e2020
		}
		SetTurnActivity();                                                // 0x103c5d35 slot 572
		return 0;                                                         // 0x103c5d48
	}
	case 0x14a:
	{
		// Hide: no hint -> line 0xee, `TaskFail(4)`. Otherwise `SetHullSizeSmall(true)`, slot 66
		// `Hide`, `m_fEffects |= 0x20`, `Relink`, `TaskComplete`.
		if (BaseScheduleHost.HintNode == INDEX_NONE)                     // 0x103c5ca2 / 0x103c5caa / 0x103c5cac JNZ
		{
			Species19Fail(*this, GSpecies19FileVampireBoss, 0xee, 4);      // 0x103c5cb2 / 0x103c5cbc / 0x103c5cc6
			return 0;
		}
		SetHullSizeSmall(true);                                           // 0x103c5cde 0x10273180
		Hide();                                                           // 0x103c5ce7 slot 66
		EffectsWord |= GSpecies19EffectNoDraw;                            // 0x103c5ced..0x103c5cf6
		// `0x103c5cfe` `CBaseEntity::Relink`: no spatial partition at this tier (family Positions'
		// `TeleportOut` states the same absence).
		TaskComplete(false);                                              // 0x103c5e3a
		return 0;
	}
	case 0x14b:
	{
		// Unhide at the hint, in retail's order.
		FHintWords Hint;
		if (BaseScheduleHost.HintNode == INDEX_NONE || !HintWords(BaseScheduleHost.HintNode, Hint))   // 0x103c5b56 / 0x103c5b5e
		{
			Species19Fail(*this, GSpecies19FileVampireBoss, 0xfd, 4);      // 0x103c5b66 / 0x103c5b70 / 0x103c5b7a
			return 0;
		}
		FVector HintOrigin = Hint.OriginCm;
		SetOrigin(HintOrigin);                                         // 0x103c5b94 slot 217; 0x103c5b9d slot 216 (0x100b2300 unparented: m_vecOrigin; slot 216 is an unported stub, slot 62's body is the one origin write)
		FVector Placed = Origin;                                          // 0x103c5ba7 slot 220; 0x103c5bad..0x103c5bc1
		Teleport(&Placed, nullptr, nullptr);                              // 0x103c5bce slot 181 (pos, 0, 0)
		SetOrigin(Placed);                                                // 0x103c5bdd slot 62
		SetHullSizeNormal(true);                                          // 0x103c5be7 0x10273070
		Unhide();                                                         // 0x103c5bf0 slot 67
		EffectsWord &= ~GSpecies19EffectNoDraw;                           // 0x103c5bf6..0x103c5bff
		SolidFlagsWord &= ~GSpecies19SolidNotSolid;                       // 0x103c5c62 AND AL,0xfb; 0x103c5c69 (inside a "CBaseEntity::RemoveSolidFlags" scope frame) (its name test 0x103c5c0d JNZ)
		// `0x103c5c7a` ForceTransmit and `0x103c5c81` Relink: no transmit state or spatial partition.
		VampireBossMatchOriginAnglesCalls.Add(FMatchOriginAnglesCall{ TEXT("bip01"), true, true });   // 0x103c5c91 0x1000577c("bip01", 1, 1)
		EffectsWord |= GSpecies19EffectBit10;                             // 0x103c5c96
		TaskComplete(false);                                              // 0x103c5e3a
		return 0;
	}
	case 0x14c:
		TransformationStartSlot618();                                     // 0x103c5d4f CALL [EDX+0x9a8]
		TaskComplete(false);                                              // 0x103c5e3a
		return 0;
	case 0x14d:
		return 0;                                                         // 0x103c5d5f JZ 0x103c5e3f: nothing, no completion
	case 0x14e:
	{
		// The monster model swap: no `m_pMonsterModelName` (+0x6680) -> line 0x131, `TaskFail(4)`.
		if (VampireBossMonsterModelName.IsEmpty())                        // 0x103c5dac / 0x103c5db4 / 0x103c5db8 JNZ
		{
			Species19Fail(*this, GSpecies19FileVampireBoss, 0x131, 4);     // 0x103c5dbc / 0x103c5dc6 / 0x103c5dd0
			return 0;
		}
		SetModel(VampireBossMonsterModelName.GetCharArray().GetData());   // 0x103c5de7 slot 105
		EffectsWord |= GSpecies19EffectBit10;                             // 0x103c5df5 / 0x103c5dfa
		// `m_nRenderFX` (+0x168) = 0 and `m_nRenderMode` (+0x16c) = 0 (`0x103c5e00` / `0x103c5e0a`):
		// render words the port does not carry at the kernel tier -- visual only.
		HullKind = 0;                                                     // 0x103c5e14 m_eHull +0x1568
		PathingHullKind = 0;                                              // 0x103c5e1e +0x156c
		SetHullSizeNormal(true);                                          // 0x103c5e28 0x10273070
		// `0x103c5e2e` Relink (cdecl): no spatial partition.
		TaskComplete(false);                                              // 0x103c5e3a
		return 0;
	}
	case 0x14f:
		// `ThinkSet(SUB_Remove 0x101c0b10, 0.0, 0)` then `m_flNextThink = curtime + 0.01`.
		ThinkSet(TEXT("0x101c0b10"), 0.0);                                // 0x103c5d87 PUSH 0x10015b68; 0x103c5d8e
		NextThink = static_cast<float>(Now + static_cast<double>(ElysiumNpcTunables::Hundredth));   // 0x103c5d98 FLD curtime; 0x103c5d9b FADD [0x10450aa4]; 0x103c5da1 +0x17c
		TaskComplete(false);                                              // 0x103c5e3a
		return 0;
	default:
		break;
	}
	return FElysiumNpcHuman::StartTaskSlot442(Task);                     // 0x103c5d6e CNPC_VHuman::StartTask
}

// =================================================================================================
// CNPC_VWerewolf -- 0x103ccda0, 3610 bytes
// =================================================================================================

// `werewolf_teleport_in_time` (object `0x1093d6f0`, its parent pointer `0x1093d6f4`; name
// `0x1065fb24`, default `"2.0"` at `0x1065fb44`, registered by `0x103c8470`). Task 2 inside schedule
// `0x158` reads it `IsCommand() ? 0.0 : m_fValue`. The convar is missing from
// `ElysiumNpcTunables::EConVar` (a hot header this lane may not edit), so the shipped default stands
// here, file-local, and the report asks the integrator to move it into the table.
static constexpr float GSpecies19WerewolfTeleportInTime = 2.0f;
// `_DAT_1044ffd0` (double 5.0) -- the 0x4e tolerance's constant term.
static constexpr double GSpecies19WerewolfToleranceSlack = 5.0;
// `_DAT_10449280` (double 1.0) -- task 0x14f's unseen-time floor.
static constexpr double GSpecies19WerewolfTeleportOutUnseen = 1.0;
// `0x40a00000` 5.0, `0x40400000` 3.0, `0x41700000` 15.0 -- the three blacklist lifetimes.
static constexpr float GSpecies19WerewolfTeleportBlacklist = 5.0f;
static constexpr float GSpecies19WerewolfMoveBlacklist = 3.0f;
static constexpr float GSpecies19WerewolfLeapBlacklist = 15.0f;
// `_DAT_10450564` 100.0 and `_DAT_1044bef8` 0.25 -- task 0x15a's jump-height terms.
static constexpr float GSpecies19WerewolfJumpRise = 100.0f;
static constexpr float GSpecies19WerewolfJumpRun = 0.25f;
// `0x40000000` 2.0 / `0x3f800000` 1.0 -- `m_fJumpGravity` for the leap and after it.
static constexpr float GSpecies19WerewolfLeapGravity = 2.0f;
// The two hint types the arms test: `0x3aa9` (the teleport arm's "filthy cheater" hint that needs no
// activity) and `0x3aa0` (the fence leap whose blacklist entry task 0x155 re-adds).
static constexpr int32 GSpecies19WerewolfHintNoActivity = 0x3aa9;
static constexpr int32 GSpecies19WerewolfHintFenceLeap = 0x3aa0;
// The failure retail hands slot 448 as a POINTER (`0x10661dac`, "Did not path out of player's
// sight") -- `AI_TaskFailureCode_t` carries a string by its address. The port's failure word is an
// int32 and 0x10661dac fits it, so the word holds exactly what retail's `+0x5c50` holds, and
// `m_failText` (`FailText`) carries the string the address names.
static constexpr int32 GSpecies19WerewolfPathOutOfSightFail = 0x10661dac;
static const TCHAR* const GSpecies19WerewolfPathOutOfSightText = TEXT("Did not path out of player's sight");

/** A hint node (the port's `CAI_Hint*`, an entity index) as the entity the blacklist and the
 *  `EHANDLE` constructor `0x103dbe70` take. Null for `INDEX_NONE` or a dead slot. */
static FElysiumEntity* Species19WerewolfHintEntity(const FElysiumNpcBase& Npc, int32 HintNode)
{
	if (Npc.World == nullptr || !Npc.World->Entities().IsValidIndex(HintNode))
	{
		return nullptr;
	}
	return Npc.World->Entities()[HintNode].Get();
}

/** The hint's words, or an invalid record for a null / dead hint -- the form the recovered hint
 *  helpers (`SetHintActivity`, `PositionAtHint`) take and refuse on. */
static FElysiumNpcBase::FHintWords Species19WerewolfHintWords(const FElysiumNpcBase& Npc, int32 HintNode)
{
	FElysiumNpcBase::FHintWords Words;
	if (HintNode != INDEX_NONE)
	{
		Npc.HintWords(HintNode, Words);
	}
	return Words;
}

/** `0x10366400`'s negation followed by `0x103662d0` -- "blacklist the hint unless it is listed and
 *  unexpired". The probe also evicts an expired row (swap-remove), which is its own recovered rule. */
static void Species19WerewolfBlacklistUnlessListed(FElysiumNpcWerewolf& Npc, int32 HintNode, float Seconds)
{
	FElysiumEntity* HintEntity = Species19WerewolfHintEntity(Npc, HintNode);
	if (!Npc.FUN_10366400(HintEntity))
	{
		Npc.FUN_103662d0(HintEntity != nullptr ? HintEntity->Handle : FElysiumEntityHandle::Invalid(), Seconds);
	}
}

/** `0x10366490 == 0` then `0x103662d0` -- the INDEX-OF test sites 0x155 and 0x156 make. The row is
 *  added only when the hint already sits at index 0; an absent hint (-1) is not added. Retail bug,
 *  reproduced (judge row 0x103cd6ab). */
static void Species19WerewolfBlacklistIfFirst(FElysiumNpcWerewolf& Npc, int32 HintNode, float Seconds)
{
	FElysiumEntity* HintEntity = Species19WerewolfHintEntity(Npc, HintNode);
	if (Npc.FUN_10366490(HintEntity) == 0)
	{
		Npc.FUN_103662d0(HintEntity != nullptr ? HintEntity->Handle : FElysiumEntityHandle::Invalid(), Seconds);
	}
}

/** `FindEntityByName(NULL, name, NULL, NULL)` (`0x100f7770`) then `___RTDynamicCast<CAI_Hint>` --
 *  the hint the last-used hint's `m_strTargetName` (`+0x468`) names, as a hint node, or `INDEX_NONE`
 *  when the name finds nothing or finds something that is not a hint. */
static int32 Species19WerewolfNamedHint(FElysiumNpcBase& Npc, const FString& Name)
{
	if (Npc.World == nullptr)
	{
		return INDEX_NONE;
	}
	FElysiumEntity* Found = Npc.World->FindByName(Name);
	return FElysiumHint::Cast(Found) != nullptr ? Found->Handle.Index : INDEX_NONE;
}

/** `0x102d40e0(hint)` -- `atof(m_iszUserData)` (`+0x5d4`), 0.0 on a null string. */
static float Species19WerewolfHintUserHeight(const FElysiumNpcBase& Npc, int32 HintNode)
{
	const FElysiumHint* Hint = FElysiumHint::Cast(Species19WerewolfHintEntity(Npc, HintNode));
	return Hint != nullptr ? FCString::Atof(*Hint->UserData) : 0.f;
}

/** A Werewolf groundpoint (SOURCE units, possibly `vec3_invalid`) into `m_vSavePosition`. Retail
 *  copies the three words; the port's `SavePosition` is centimetres, so a real point is scaled and
 *  the `FLT_MAX` sentinel is stored as the sentinel -- no unit conversion may be applied to it. */
static void Species19WerewolfStoreSavePosition(FElysiumNpcWerewolf& Npc, const FVector& GroundUnits)
{
	Npc.SavePosition = FElysiumNpcWerewolf::IsVec3Invalid(GroundUnits) ? GroundUnits : GroundUnits * ElysiumMove::U;
}

/** SEAM for `GetCharTemplate(player) == TemplateIndex("Player_Malkavian")` (`0x10337860` against
 *  `0x101d5bd0(0x10738d10, "Player_Malkavian")`). The player's sheet carries its clan but no
 *  character-template index, and the template table has no port; answers false (not the Malkavian
 *  template). */
static bool Species19WerewolfPlayerIsMalkavianTemplate(const FElysiumEntity& Player)
{
	(void)Player;
	return false;
}

/** `0x102f2ea0(m_pNavigator)` -- "already at the goal": not climbing/jumping (nav type 3 or 1), the
 *  2D distance to the goal (`0x102ee140`) inside the path tolerance (`0x102f2fc0`) and the height
 *  difference within slot 522, in which case it also runs the navigator's `+0x20` arrival. Family
 *  StartTask19 answers this address once, `FElysiumNpc::StartTask19NavArrived` (the port mover's own
 *  arrival sample). */
static bool Species19WerewolfNavAtGoal(FElysiumNpcWerewolf& Npc)
{
	return Npc.StartTask19NavArrived();
}

// Slot 442: `0x103ccda0`.
int32 FElysiumNpcWerewolf::StartTaskSlot442(void* Task)
{
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);            // 0x103cce1b slot 449 TaskName (trace only) (the body's scope frame and its name tests 0x103ccdb2 JZ / 0x103ccdbc JNZ: the debug ring, absent)
	const double Now = Species19Now(*this);

	switch (TaskLocal) // 0x103cce23 CMP 0x154: 0x103cce28 JG / 0x103cce2e JZ (0x154); 0x103cce39 JG / 0x103cce4a JG / 0x103cce50 JZ (0x14a) / 0x103cce59 JZ (2) / 0x103cce62 JZ (0x4e) / 0x103cce69 JNZ (0x100); 0x103cd14e JZ (0x14b) / 0x103cd155 JNZ (0x14c); 0x103cd2f6 JA / 0x103cd2fc table (0x14e..0x153); 0x103cd64f JA / 0x103cd655 table (0x155..0x161)
	{
	case 2:
	{
		// TASK_WAIT is the Werewolf's own only inside schedule 0x158 (`0x102cc1f0`: slot 440 then
		// slot 446, the miss installing 1); anywhere else it is the base's.
		if (Schedule.Current == ElysiumScheduleId::None)                  // 0x103ccefe / 0x103ccf04
		{
			break;
		}
		const int32 Translated = TranslateScheduleRetail(0x158);         // 0x103ccf11 0x102cc1f0 -> slot 440
		int32 Wanted = ResolveScheduleId(Translated);                     // slot 446
		if (ElysiumScheduleFor(Wanted) == nullptr)
		{
			RecordScheduleEvent(FString::Printf(TEXT("GetScheduleOfType(): No CASE for %d"), Translated));
			Wanted = ResolveScheduleId(ElysiumSched::IDLE_STAND);          // slot 446(1)
		}
		if (Schedule.Current != Wanted)                                   // 0x103ccf16 / 0x103ccf1c
		{
			break;
		}
		// `IsCommand()` answers false on a ConVar, so the `+0x28` float arm is the one that runs; the
		// other arm (`0x103ccf31`) would add 0.0. No completion: the wait finishes in RunTask.
		BaseScheduleHost.WaitFinished = Now + static_cast<double>(GSpecies19WerewolfTeleportInTime);   // 0x103ccf2a slot 1; 0x103ccf63 +0x28; 0x103ccf69 +0x5db4 (the IsCommand test 0x103ccf2f JZ)
		return 0;                                                         // 0x103ccf7d
	}
	case 0x4e:
	{
		// m_flGoalTolerance = ResolveTaskDistance(data) + (+0x66d0) + (+0x66cc) + 5.0, pushed into the
		// navigator twice, complete.
		const double Tolerance = static_cast<double>(ResolveTaskDistance(Step->Data))   // 0x103ccea1 slot 418
			+ static_cast<double>(WerewolfTeleportDistanceB)               // 0x103ccea7 FADD +0x66d0
			+ static_cast<double>(WerewolfTeleportDistanceA)               // 0x103ccead FADD +0x66cc
			+ GSpecies19WerewolfToleranceSlack;                            // 0x103cceb3 FADD [0x1044ffd0]
		SetGoalTolerance(static_cast<float>(Tolerance));                  // 0x103ccec1 FSTP +0x6320
		StartTask19SetNavTolerances(static_cast<float>(Tolerance), static_cast<float>(Tolerance));   // 0x103ccece 0x102ee1c0; 0x103ccee0 0x102f2fe0
		TaskComplete(false);                                              // 0x103ccee8
		return 0;
	}
	case 0x100:
	{
		// The base FIRST (a direct call), then m_bfAINPCFlags &= ~0x10000.
		const int32 Result = FElysiumNpc::StartTaskSlot442(Task);         // 0x103cce72 0x10010695 -> 0x102a1910
		NpcFlags.Clear(EElysiumNpcFlag::FORCE_RELAXED_ANIMS);             // 0x103cce7d AND ~0x10000; 0x103cce82 +0x14b8
		return Result;
	}
	case 0x14a:
	{
		// Acquire the closest player when there is no enemy OR no hint data (`+0x6720` count).
		if (static_cast<const FElysiumNpc*>(this)->GetEnemy() == nullptr  // 0x103ccf84 slot 167 / 0x103ccf8c JZ
			|| WerewolfHintGroundpoints.Num() == 0)                        // 0x103ccf8e +0x6720 / 0x103ccf94 JNZ
		{
			Senses.SetClosestPlayer(*this, Now);                          // 0x103ccf9c 0x10293a80
			FElysiumEntity* Player = World != nullptr && Senses.Memory.ClosestPlayer.IsSet()
				? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;  // 0x103ccfa1..0x103ccfd8 (0x103ccfac JZ / 0x103ccfd0 JNZ)
			if (Player != nullptr)
			{
				bWerewolfPlayerIsMalkavian = Species19WerewolfPlayerIsMalkavianTemplate(*Player);   // 0x103cd010 / 0x103cd019; 0x103cd023 +0x6710 (the re-resolve 0x103ccfe7 JZ / 0x103ccffe JNZ)
				Relationships.SetEntity(Player->Handle, EElysiumRelationship::Hate, 10);   // 0x103cd05e AddEntityRelationship(player, 1, 10) (0x103cd032 JZ / 0x103cd04f JNZ)
				ElysiumNpcEnemy::SetEnemy(*this, Player->Handle);         // 0x103cd094 0x10279a50 (0x103cd06c JZ / 0x103cd089 JNZ)
				SetTarget(Player->Handle);                                // 0x103cd0ca 0x10279cc0 (0x103cd0a2 JZ / 0x103cd0bf JNZ)
				Slot600(Player);                                          // 0x103cd102 slot 600 (0x103cd0d8 JZ / 0x103cd0f5 JNZ)
			}
		}
		if (static_cast<const FElysiumNpc*>(this)->GetEnemy() == nullptr) // 0x103cd10c slot 167; 0x103cd114
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s could not find an enemy... oh well!\n"),
				*NpcKernelDebug10Shared::GDebug10DebugName(this)));       // 0x103cd11c / 0x103cd127 DevWarning
		}
		TaskComplete(false);                                              // 0x103cd133 / 0x103cd84e -- both arms
		return 0;
	}
	case 0x14b:
	{
		// Zone bit 0 set: a random roar. Otherwise the enemy in the view cone plays 0x100 and out of it
		// the random pick: RandomInt(0, 1) nonzero -> 0x124, zero -> 0x125.
		int32 Activity = 0x100;                                           // 0x103cd26b EDI = 0x100
		bool bRandom = (WerewolfHintFlags & 1u) == 1u;                    // 0x103cd265..0x103cd276
		if (!bRandom)
		{
			bRandom = !FInViewCone(static_cast<const FElysiumNpc*>(this)->GetEnemy());   // 0x103cd27c slot 167; 0x103cd285 slot 363 / 0x103cd28d JNZ
		}
		if (bRandom)
		{
			const int32 Roll = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(0, 1);   // 0x103cd292 RandomInt(0, 1)
			Activity = Roll != 0 ? 0x124 : 0x125;                         // 0x103cd29d NEG / SBB / ADD 0x125
		}
		RestartIdealActivityId(Activity);                                 // 0x103cd2aa 0x10289ee0
		Species19MotorYawSpeedReset(*this);                               // 0x103cd2b5 0x102e0b40
		// Retail dereferences the enemy unchecked (`0x103cd2cf` slot 217 on it). CRASH GUARD: with no
		// enemy the facing write is skipped.
		if (const FElysiumEntity* Enemy = static_cast<const FElysiumNpc*>(this)->GetEnemy())   // 0x103cd2be slot 167
		{
			Species19MotorIdealYawTo(*this, Enemy->Origin);               // 0x103cd2cf slot 217; 0x103cd2d8 0x102e2020
		}
		return 0;                                                         // no completion
	}
	case 0x14c:
	{
		// Path out of the player's sight: done at once under condition 0x77; otherwise the
		// TASK_WAIT_FOR_MOVEMENT start shape over the navigator.
		if (Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(0x77))) // 0x103cd15f HasCondition(0x77) / 0x103cd166 JZ
		{
			TaskComplete(false);                                          // 0x103cd16b
			return 0;
		}
		if (NavIsGoalSet())                                               // 0x103cd187 0x102ee2e0 / 0x103cd18e JZ
		{
			// `0x102ee2c0` is `path+0x10 = 0` (`0x1030bea0`), NOT `StopMoving`: the motor keeps its
			// route (the base arm 0x5d's reading of the same pair, `0x1028675e`).
			++StartTaskNav.PathGoalFlagClears;                            // 0x103cd196 0x102ee2c0
		}
		// `0x102ee620` (the goal type) and `0x102ee6a0` (path with a current waypoint): the mover
		// carries ONE goal latch for both (the standing fact `NavIsGoalSet` states), which is
		// `NavIsGoalActive` -- `0x102ee680`, the same `0x100113d8(path)` read `0x102ee620` makes.
		if (!NavIsGoalActive())                                           // 0x103cd1a1 0x102ee620; 0x103cd1a8
		{
			BaseScheduleHost.bShouldMove = false;                         // 0x103cd1b3 +0x1a40
			BaseScheduleHost.FailText = GSpecies19WerewolfPathOutOfSightText;
			TaskFail(GSpecies19WerewolfPathOutOfSightFail);               // 0x103cd1b9 slot 448 (0x10661dac)
			StartTaskClearGoal();                                         // 0x103cd1c5 0x102ee270
			return 0;
		}
		if (!NavIsGoalActive())                                           // 0x103cd1e1 0x102ee6a0 / 0x103cd1e8 JNZ
		{
			BaseScheduleHost.bShouldMove = false;                         // 0x103cd1ec
			SetIdealActivity(ResolveLinkActivity());                      // 0x103cd1f2 0x1027a6c0; 0x103cd1fa
			return 0;
		}
		if (!Species19WerewolfNavAtGoal(*this))                           // 0x103cd216 0x102f2ea0 / 0x103cd21f JZ
		{
			BaseScheduleHost.bShouldMove = true;                          // 0x103cd247
			ValidateNavGoal();                                            // 0x103cd24e slot 528
			return 0;
		}
		BaseScheduleHost.bShouldMove = false;                             // 0x103cd228
		BaseScheduleHost.FailText = GSpecies19WerewolfPathOutOfSightText;
		TaskFail(GSpecies19WerewolfPathOutOfSightFail);                   // 0x103cd22e slot 448
		return 0;
	}
	case 0x14d:
		return 0;                                                         // 0x103cce3f -> 0x103cdba6, running
	case 0x14e:
		TeleportIn();                                                     // 0x103cd374 0x103d4d60
		TaskComplete(false);                                              // 0x103cd37c
		return 0;
	case 0x14f:
	{
		// Teleport out once unseen for 1.0 s (`curtime - +0x66ec`, floored at 0.0).
		double Unseen = Now - WerewolfLastSeenTime;                       // 0x103cd308 / 0x103cd30b +0x66ec
		if (!(ElysiumNpcTunables::Zero <= Unseen))                        // 0x103cd311 / 0x103cd320
		{
			Unseen = ElysiumNpcTunables::Zero;                            // 0x103cd324
		}
		if (Unseen < GSpecies19WerewolfTeleportOutUnseen)                 // 0x103cd32a FCOMP [0x10449280]; 0x103cd337 JP
		{
			Species19FailBare(*this, 0x1a);                               // 0x103cd33d slot 448
			return 0;
		}
		TeleportOut();                                                    // 0x103cd354 0x103d4a60
		TaskComplete(false);                                              // 0x103cd35c
		return 0;
	}
	case 0x150:
	{
		const FHintWords Hint = Species19WerewolfHintWords(*this, TeleportHintNode);   // 0x103cd392 +0x66b0
		if (TeleportHintNode == INDEX_NONE)                               // 0x103cd39a
		{
			Species19FailBare(*this, 4);                                  // 0x103cd3a2 slot 448
			return 0;
		}
		if (Hint.HintType == GSpecies19WerewolfHintNoActivity)            // 0x103cd3b9 +0x5dc; 0x103cd3c7
		{
			TaskComplete(false);                                          // 0x103cd3ca
			return 0;
		}
		if (!SetHintActivity(Hint))                                       // 0x103cd3e1 0x103d6000; 0x103cd3e8
		{
			Species19FailBare(*this, 0x15);                               // 0x103cd3f4 slot 448
		}
		return 0;
	}
	case 0x151:
	{
		// Chain to the next teleport hint: blacklist and release the current one, then take the hint its
		// `m_strTargetName` names.
		if (TeleportHintNode == INDEX_NONE)                               // 0x103cd40b / 0x103cd413
		{
			TaskComplete(false);                                          // 0x103cd84e
			return 0;
		}
		FElysiumEntity* HintEntity = Species19WerewolfHintEntity(*this, TeleportHintNode);
		FUN_103662d0(HintEntity != nullptr ? HintEntity->Handle : FElysiumEntityHandle::Invalid(),
			GSpecies19WerewolfTeleportBlacklist);                          // 0x103cd422 0x103dbe70; 0x103cd429 0x103662d0(5.0)
		WerewolfLastUsedTeleportHint = TeleportHintNode;                  // 0x103cd435 +0x66b4
		ReleaseHintNode(TeleportHintNode, ElysiumNpcTunables::Zero);      // 0x103cd43b 0x102d1420(0.0)
		ClearTeleportHint();                                              // 0x103cd442 0x103d4760
		const FHintWords LastUsed = Species19WerewolfHintWords(*this, WerewolfLastUsedTeleportHint);   // 0x103cd447 +0x66b4
		const int32 Next = Species19WerewolfNamedHint(*this, LastUsed.TargetName);   // 0x103cd452 +0x468; 0x103cd46f 0x100f7770; 0x103cd475 ___RTDynamicCast (the null-name arm 0x103cd456 JNZ)
		if (Next != INDEX_NONE)                                           // 0x103cd47f
		{
			SetTeleportHint(Next);                                        // 0x103cd484 0x103d45c0
		}
		if (TeleportHintNode == INDEX_NONE)                               // 0x103cd489 / 0x103cd48f
		{
			TaskComplete(false);                                          // 0x103cd84e
			return 0;
		}
		SnapToAnimationPoint();                                           // 0x103cd497 0x103d9f90
		if (!SetHintActivity(Species19WerewolfHintWords(*this, TeleportHintNode)))   // 0x103cd4a5; 0x103cd4ac
		{
			TaskComplete(false);                                          // 0x103cd4b5
		}
		return 0;
	}
	case 0x152:
		WerewolfHintFlags = 0;                                            // 0x103cd4d1 +0x66e8
		Species19WerewolfBlacklistUnlessListed(*this, TeleportHintNode, GSpecies19WerewolfTeleportBlacklist);   // 0x103cd4da 0x10366400; 0x103cd4f9 0x103662d0(5.0) (0x103cd4e1 JNZ / 0x103cd4f2 the handle)
		ClearTeleportHint();                                              // 0x103cd500
		SetHullSizeSmall(true);                                           // 0x103cd509 0x10273180(1)
		// `+0x66ec` is one retail word the port carries twice (`WerewolfLastSeenTime` / Lifecycle's
		// `WerewolfUnhideStamp`); both take the stamp, as `ElysiumNpcWerewolf.cpp` 0x103cac89 does.
		WerewolfLastSeenTime = Now;                                       // 0x103cd50e / 0x103cd517 +0x66ec
		WerewolfUnhideStamp = Now;
		TaskComplete(false);                                              // 0x103cd51f
		return 0;
	case 0x153:
	{
		if (MoveHintNode == INDEX_NONE)                                   // 0x103cd535 / 0x103cd53f
		{
			Species19FailBare(*this, 4);                                  // 0x103cd545 slot 448
			return 0;
		}
		// `0x103ceb60(m_pMoveHint)`: the hint debug-string builder, its string discarded -- nothing
		// observable, so nothing is called.                            // 0x103cd55d
		Species19WerewolfStoreSavePosition(*this,
			GetHintGroundpoint(Species19WerewolfHintWords(*this, MoveHintNode)));   // 0x103cd562; 0x103cd82c 0x103d6770; 0x103cd833..0x103cd845 +0x5dd0
		TaskComplete(false);                                              // 0x103cd84e
		return 0;
	}
	case 0x154:
	{
		// Face the move hint's yaw (slot 219 word +4), flipped by 180 under the motor's +0x28 byte.
		if (MoveHintNode == INDEX_NONE)                                   // 0x103cd571 / 0x103cd577
		{
			Species19FailBare(*this, 4);                                  // 0x103cd57f slot 448
			return 0;
		}
		Species19MotorYawSpeedReset(*this);                               // 0x103cd59c 0x102e0b40
		const FHintWords Hint = Species19WerewolfHintWords(*this, MoveHintNode);
		const float Yaw = static_cast<float>(Hint.Angles.Y);              // 0x103cd5a9 slot 219; 0x103cd5b5 [EAX+4]
		// `0x103cd5b8..0x103cd626` inline `0x10288670`: the motor `+0x28` half-turn (`0x103cd5c7` FCOMP
		// 180.0), then `+0x1c == 180.0` stores `+0x34` raw (`0x103cd5fb`) or through `0x102e0a80`
		// (`0x103cd621`) -- family StartTask19's `StartTaskMotorSetIdealYaw` (the `+0x28` byte is
		// `BaseScheduleHost.bMotorAnimationMovement`).
		StartTaskMotorSetIdealYaw(Yaw); // 0x103cd5c1 JZ (+0x28) / 0x103cd5d8 JNZ (< 180) / 0x103cd5f3 JNZ (+0x1c == 180.0)
		ReleaseMotorHintYaw();                                            // 0x103cd604 / 0x103cd631 0x102e1e20(-1)
		return 0;                                                         // no completion
	}
	case 0x155:
	{
		const FHintWords Hint = Species19WerewolfHintWords(*this, MoveHintNode);   // 0x103cd65c +0x66bc
		if (!SetHintActivity(Hint))                                       // 0x103cd665; 0x103cd66c
		{
			Species19FailBare(*this, 0x15);                               // 0x103cd674 slot 448
			return 0;
		}
		if (Hint.HintType != GSpecies19WerewolfHintFenceLeap)             // 0x103cd691 CMP +0x5dc, 0x3aa0; 0x103cd69b
		{
			return 0;
		}
		Species19WerewolfBlacklistIfFirst(*this, MoveHintNode, GSpecies19WerewolfLeapBlacklist);   // 0x103cd6a4 0x10366490; 0x103cd6ab; 0x103cd6c7 (15.0) (0x103cd6c0 the handle)
		return 0;                                                         // running either way
	}
	case 0x156:
	{
		// Chain to the next move hint.
		if (MoveHintNode == INDEX_NONE)                                   // 0x103cd6dd / 0x103cd6e5
		{
			TaskComplete(false);                                          // 0x103cd79d
			return 0;
		}
		ReleaseHintNode(MoveHintNode, ElysiumNpcTunables::Zero);          // 0x103cd6ec 0x102d1420(0.0)
		WerewolfLastUsedMoveHint = MoveHintNode;                          // 0x103cd6fa +0x66c0
		Species19WerewolfBlacklistIfFirst(*this, MoveHintNode, GSpecies19WerewolfMoveBlacklist);   // 0x103cd700 0x10366490; 0x103cd707; 0x103cd71f (3.0) (0x103cd718 the handle)
		ClearMoveHint();                                                  // 0x103cd726 0x103d4690
		const FHintWords LastUsed = Species19WerewolfHintWords(*this, WerewolfLastUsedMoveHint);   // 0x103cd72b +0x66c0
		const int32 Next = Species19WerewolfNamedHint(*this, LastUsed.TargetName);   // 0x103cd753 / 0x103cd759 (the null-name arm 0x103cd73a JNZ)
		if (Next != INDEX_NONE)                                           // 0x103cd763
		{
			SetMoveHint(Next, bRandomHint);                               // 0x103cd765 +0x66c8; 0x103cd76f 0x103d44e0
		}
		if (MoveHintNode == INDEX_NONE)                                   // 0x103cd774 / 0x103cd77c
		{
			TaskComplete(false);                                          // 0x103cd79d
			return 0;
		}
		if (SetHintActivity(Species19WerewolfHintWords(*this, MoveHintNode)))   // 0x103cd781; 0x103cd788
		{
			return 0;
		}
		// RETAIL DEFECT, reproduced: the failure FALLS INTO the completion (`0x103cd794` then
		// `0x103cd79a`), so the task both fails (0x15) and completes.
		Species19FailBare(*this, 0x15);                                   // 0x103cd794 slot 448
		TaskComplete(false);                                              // 0x103cd79d
		return 0;
	}
	case 0x157:
		Species19WerewolfBlacklistUnlessListed(*this, MoveHintNode, GSpecies19WerewolfMoveBlacklist);   // 0x103cd7bc 0x10366400; 0x103cd7db (3.0) (0x103cd7c3 JNZ / 0x103cd7d4 the handle)
		WerewolfHintFlags = 0;                                            // 0x103cd7e2 +0x66e8
		ClearMoveHint();                                                  // 0x103cd7e8
		SetHullSizeSmall(true);                                           // 0x103cd7f1 0x10273180(1)
		TaskComplete(false);                                              // 0x103cd7f9
		return 0;
	case 0x158:
		if (!FindBreakHint())                                             // 0x103cd811 0x103d0ec0; 0x103cd818
		{
			Species19FailBare(*this, 4);                                  // 0x103cd57f (the 0x154 fail block)
			return 0;
		}
		Species19WerewolfStoreSavePosition(*this,
			GetHintGroundpoint(Species19WerewolfHintWords(*this, WerewolfBreakHintNode)));   // 0x103cd81e +0x66c4; 0x103cd82c; 0x103cd833..0x103cd845
		TaskComplete(false);                                              // 0x103cd84e
		return 0;
	case 0x159:
		if (WerewolfBreakHintNode == INDEX_NONE)                          // 0x103cd864 / 0x103cd86c
		{
			Species19FailBare(*this, 0x15);                               // 0x103cdb52 slot 448
			return 0;
		}
		if (!SetHintActivity(Species19WerewolfHintWords(*this, WerewolfBreakHintNode)))   // 0x103cd875; 0x103cd87c
		{
			Species19FailBare(*this, 0x15);                               // 0x103cd888 slot 448
		}
		return 0;
	case 0x15a:
	{
		// The leap: origin -> the move hint's endpoint, gravity 2.0, height = the hint's user-data height
		// plus (dz + 100) when rising, else (|dy| + |dx|) * 0.25.
		if (MoveHintNode == INDEX_NONE)                                   // 0x103cd89f / 0x103cd8a9
		{
			Species19FailBare(*this, 4);                                  // 0x103cd8af slot 448
			return 0;
		}
		const FVector SelfUnits = Origin / ElysiumMove::U;                // 0x103cd8c8 slot 217
		const FHintWords Hint = Species19WerewolfHintWords(*this, MoveHintNode);
		const FVector EndUnits = GetHintEndpoint(Hint.bValid ? &Hint : nullptr) / ElysiumMove::U;   // 0x103cd8f0 0x103d6650
		const float DeltaZ = static_cast<float>(EndUnits.Z) - static_cast<float>(SelfUnits.Z);   // 0x103cd8f5 / 0x103cd8f9
		JumpOrigin = SelfUnits;                                           // 0x103cd909 / 0x103cd919 / 0x103cd923 +0x649c
		JumpTarget = EndUnits;                                            // 0x103cd92d / 0x103cd935 / 0x103cd93b +0x64a8
		float Extra = 0.f;
		if (DeltaZ > ElysiumNpcTunables::Zero)                            // 0x103cd90f FCOM 0.0; 0x103cd941 AND 0x4100; 0x103cd946
		{
			Extra = DeltaZ + GSpecies19WerewolfJumpRise;                  // 0x103cd948
		}
		else
		{
			Extra = (FMath::Abs(static_cast<float>(SelfUnits.Y) - static_cast<float>(EndUnits.Y))
				+ FMath::Abs(static_cast<float>(SelfUnits.X) - static_cast<float>(EndUnits.X)))
				* GSpecies19WerewolfJumpRun;                               // 0x103cd952..0x103cd968
		}
		const float UserHeight = Species19WerewolfHintUserHeight(*this, MoveHintNode);   // 0x103cd978 0x102d40e0
		JumpGravity = GSpecies19WerewolfLeapGravity;                      // 0x103cd984 +0x64b8
		JumpHeight = UserHeight + Extra;                                  // 0x103cd97d / 0x103cd98e +0x64b4
		TaskComplete(false);                                              // 0x103cd994
		return 0;
	}
	case 0x15b:
	{
		WerewolfCheckStuck(EStuckEscape::WarnOnly);                       // 0x103cd9ad 0x103cb920(0)
		const int32 Hint = MoveHintNode;                                  // 0x103cd9b2
		WerewolfHintFlags = 0;                                            // 0x103cd9be
		JumpGravity = ElysiumNpcTunables::One;                            // 0x103cd9c7 +0x64b8 = 1.0
		FElysiumEntity* HintEntity = Species19WerewolfHintEntity(*this, Hint);
		FUN_103662d0(HintEntity != nullptr ? HintEntity->Handle : FElysiumEntityHandle::Invalid(),
			GSpecies19WerewolfLeapBlacklist);                              // 0x103cd9d1 / 0x103cd9d8 (15.0), UNCONDITIONAL
		ClearMoveHint();                                                  // 0x103cd9df
		WerewolfSnapWordA = 0;                                            // 0x103cd9e7 +0x66a8 byte
		TaskComplete(false);                                              // 0x103cd9ed
		return 0;
	}
	case 0x15c:
		PositionAtHint(Species19WerewolfHintWords(*this, MoveHintNode)); // 0x103cda0c 0x103d6280
		RestartIdealActivityId(0x11c);                                    // 0x103cda18
		if (IdealActivityNumber != 0x11c)                                 // 0x103cda1d +0xff0
		{
			Species19FailBare(*this, 0x15);                               // 0x103cdb52
		}
		return 0;
	case 0x15d:
		RestartIdealActivityId(0x11d);                                    // 0x103cda33
		if (IdealActivityNumber != 0x11d)                                 // 0x103cda38
		{
			Species19FailBare(*this, 0x15);                               // 0x103cdb87
		}
		return 0;
	case 0x15e:
		RestartIdealActivityId(0x11f);                                    // 0x103cda4e
		if (IdealActivityNumber != 0x11f)                                 // 0x103cda53
		{
			Species19FailBare(*this, 0x15);                               // 0x103cdb52
		}
		return 0;
	case 0x15f:
		RestartIdealActivityId(0x11e);                                    // 0x103cda69
		if (IdealActivityNumber != 0x11e)                                 // 0x103cda6e / 0x103cda78
		{
			Species19FailBare(*this, 0x15);                               // 0x103cda80
			return 0;
		}
		SolidFlagsWord |= 0x4;                                            // 0x103cdaf4..0x103cdafc AddSolidFlags(m_Collision +0x2b4 | 4) (its scope name test 0x103cda9f JNZ)
		{
			const FElysiumEntity* Enemy = static_cast<const FElysiumNpc*>(this)->GetEnemy();   // 0x103cdb11 slot 167
			FireOutput(FName(TEXT("OnBeginCrushAnimation")),
				Enemy != nullptr ? Enemy->Handle : FElysiumEntityHandle::Invalid());   // 0x103cdb1e +0x6740
		}
		return 0;
	case 0x160:
		RestartIdealActivityId(0x120);                                    // 0x103cdb3b
		if (IdealActivityNumber != 0x120)                                 // 0x103cdb40 / 0x103cdb4a JZ
		{
			Species19FailBare(*this, 0x15);                               // 0x103cdb52
		}
		return 0;
	case 0x161:
		RestartIdealActivityId(0x121);                                    // 0x103cdb70
		if (IdealActivityNumber != 0x121)                                 // 0x103cdb75 / 0x103cdb7f JZ
		{
			Species19FailBare(*this, 0x15);                               // 0x103cdb87
		}
		return 0;
	default:
		break;
	}
	return FElysiumNpc::StartTaskSlot442(Task);                          // 0x103cdba1 0x10010695 -> 0x102a1910
}

// =================================================================================================
// CNPC_VZombie -- 0x103dfd80, 868 bytes
// =================================================================================================

/** `CVFeatList_t` (`0x10739d08`) fields, loaded by `0x101e6310` from `Rules.txt` with
 *  `KeyValues::GetFloat(key, default)`; the default is the loader's immediate. The same read
 *  `ElysiumNpcLifecycle19.cpp`'s file-local `Lifecycle19RulesFloat` makes for `+0x27c..+0x28c`. */
static float Species19RulesFloat(const FElysiumNpc& Npc, const TCHAR* Section, const TCHAR* Key,
	float ImageDefault)
{
	UElysiumSessionSubsystem* GameState = Npc.World != nullptr ? Npc.World->GetGameState() : nullptr;
	UElysiumRulebookSubsystem* Rules = GameState != nullptr ? GameState->Rulebook() : nullptr;
	return Rules != nullptr ? Rules->Rules().Flt(Section, Key, ImageDefault) : ImageDefault;
}

bool FElysiumNpcZombie::ZombieBeFedOnByZombie(FElysiumPlayer& Player)
{
	// SEAM for the player's slot 425 (`+0x6a4`), `CBasePlayer::BeFedOnByZombie` `0x10168700`: the
	// grapple-partner refusal, `StartGrappleAttack` mode 8 (`ACT_ZOMBIE_FEEDING_ENGAGE`), the weapon
	// hide, the latch `+0x14a8`, `m_bCantBreakGrapple`, `m_flBreakGrappleTimer` and
	// `m_flGrappleDamageTimer`. The player-side grapple start for mode 8
	// (`EElysiumGrappleType::ZombieFeedsPlayer`) has no port body; counted and refused. The zombie
	// ignores the answer.
	(void)Player;
	++ZombieBeFedOnCalls;
	return false;
}

// Slot 442: `0x103dfd80`. Walked in the body's own compare order.
int32 FElysiumNpcZombie::StartTaskSlot442(void* Task)
{
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	const int32 TaskLocal = Species19TaskLocal(*this, Step);
	if (TaskLocal > 0x150)                                               // 0x103dfd8e CMP 0x150; JG 0x103dffc2 (0x103dfd93)
	{
		if (TaskLocal == 0x151)                                          // 0x103dffc2 SUB 0x151; JZ 0x103e0088 (0x103dffc7)
		{
			// `m_bShouldMove = 1`, the navigator's movement activity `0x1014`, and
			// `m_flWaitFinished = RandomFloat(featlist+0x250, featlist+0x254) + curtime` -- the max
			// getter (`0x101e8ab0`) is called BEFORE the min (`0x101e8a90`).
			BaseScheduleHost.bShouldMove = true;                          // 0x103e0093 +0x1a40
			StartTaskSetMovementActivity(0x1014);                         // 0x103e009a 0x10006fd7 -> 0x102ee250 (path +0x2c), the base body
			const float Max = Species19RulesFloat(*this, TEXT("Zombie_Grapple_Info"),
				TEXT("LungeDistanceMax"), 160.f);                         // 0x103e00ac 0x101e8ab0 (+0x254, default 0x43200000)
			const float Min = Species19RulesFloat(*this, TEXT("Zombie_Grapple_Info"),
				TEXT("LungeDistanceMin"), 98.f);                          // 0x103e00ba 0x101e8a90 (+0x250, default 0x42c40000)
			const float Draw = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(Min, Max);   // 0x103e00c9 (*DAT_1070b244)+4
			BaseScheduleHost.WaitFinished = static_cast<double>(Draw) + Species19Now(*this);   // 0x103e00d2 FADD curtime; 0x103e00d5 FSTP +0x5db4
			return 0;
		}
		if (TaskLocal == 0x152)                                          // 0x103dffcd DEC; JZ 0x103e0035 (0x103dffce)
		{
			// `m_hClosestPlayer` (+0x628c) resolving to an entity whose `+0xa8` player record stands:
			// the player's slot 425 with this zombie. The answer is ignored; complete either way.
			FElysiumEntity* Closest = World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;   // 0x103e0035..0x103e0061 (0x103e003e JZ / 0x103e005b JNZ)
			FElysiumPlayer* Player = World != nullptr ? World->FindPlayer() : nullptr;
			if (Closest != nullptr && Player != nullptr && Closest->Handle == World->PlayerHandle())   // 0x103e0063 +0xa8; 0x103e006b
			{
				ZombieBeFedOnByZombie(*Player);                           // 0x103e0070 CALL [EAX+0x6a4]
			}
			TaskComplete(false);                                          // 0x103e007a
			return 0;
		}
		if (TaskLocal == 0x153)                                          // 0x103dffd0 DEC; JZ 0x103dffe4 (0x103dffd1)
		{
			// `+0x66e4` picks the activity; anything else keeps `m_Activity` (+0xfec).
			int32 Activity = ActivityNumber;                              // 0x103dffea +0xfec
			switch (ZombieFeedVariant)                                    // 0x103dffe4 +0x66e4 (0x103dfff1 JZ / 0x103dfff4 JZ / 0x103dfff7 JNZ)
			{
			case 1: Activity = 0x1098; break;                             // 0x103e0007
			case 2: Activity = 0x109b; break;                             // 0x103e0000
			case 3: Activity = 0x109e; break;                             // 0x103dfff9
			default: break;
			}
			RestartIdealActivityId(Activity);                             // 0x103e000f
			if (IdealActivityNumber != Activity)                          // 0x103e0014 CMP [ESI+0xff0] / 0x103e001a JZ
			{
				Species19FailBare(*this, 0x15);                           // 0x103e0026 slot 448
			}
			return 0;
		}
		return FElysiumNpcAnimal::StartTaskSlot442(Task);                // 0x103dffd6
	}
	if (TaskLocal == 0x150)                                              // 0x103dfd99 JZ 0x103dfe97
	{
		// The navigator scratch refresh (`navigator+8 = +0x156c`, `+0xc = gpGlobals+4`), then
		// `CAI_Pathfinder::NearestNodeToNPC` and `CAI_Node::GetPosition(m_eHull)`: no node is
		// `TaskFail(0x18)`; a node submits a type-4 goal at it with every activity word -1, tolerance
		// -1.0 (`0x104d1d00`), and `SetGoal(record, 4)` whose answer is NOT tested.
		FVector NodeUnits = FVector::ZeroVector;
		if (!NavigatorNearestNodePositionUnits(NodeUnits))               // 0x103dfecc 0x102f3c10; 0x103dff20 0x102fb0d0 (0x103dfec2 slot 220; 0x103dfed6 JZ; the node bounds 0x103dfee7 JL / 0x103dfeeb JGE and the DAT_106c994c miss count sit in the seam)
		{
			Species19FailBare(*this, 0x18);                               // 0x103dffb3
			return 0;
		}
		Species19SetGoal(*this, 4, NodeUnits * ElysiumMove::U, INDEX_NONE, -1.f, 4);                       // 0x103dff9f 0x102ecd20 (result discarded)
		return 0;
	}
	if (TaskLocal > 0x14e)                                               // 0x103dfd9f CMP 0x14e; JG 0x103dfe7a (0x103dfda4)
	{
		if (TaskLocal == 0x14f)                                          // 0x103dfe7a CMP 0x14f; 0x103dfe7f JNZ
		{
			SetIdealActivity(0x4a);                                       // 0x103dfe89 0x10272650
			return 0;
		}
		return FElysiumNpcAnimal::StartTaskSlot442(Task);                // 0x103dffd6
	}
	if (TaskLocal == 0x14e)                                              // 0x103dfdaa JZ 0x103dfe65
	{
		SetIdealActivity(0x1081);                                         // 0x103dfe6c 0x10272650
		return 0;
	}
	if (TaskLocal == 0x36)                                               // 0x103dfdb3 JZ 0x103dfdf0
	{
		AutoMovement();                                                   // 0x103dfdf2
		Species19MotorYawSpeedReset(*this);                               // 0x103dfdfd 0x102e0b40
		const FElysiumEntity* LkpKey = static_cast<const FElysiumNpc*>(this)->GetEnemy();   // 0x103dfe06 slot 167
		FVector EnemyLkp = FVector::ZeroVector;
		EnemyLkp = Conditions19LastKnownPosition(LkpKey);                                 // 0x103dfe16 slot 541; 0x103dfe1e 0x102dfed0
		Species19MotorIdealYawToAtSpeed(*this, EnemyLkp, -2.0f);          // 0x103dfe33 0x102e20b0(motor, &lkp, 0xc0000000)
		RestartIdealActivityId(0x4b);                                     // 0x103dfe3c
		if (IsActivityFinished())                                         // 0x103dfe45 slot 251 / 0x103dfe4d JZ
		{
			TaskComplete(false);                                          // 0x103dfe57
		}
		return 0;
	}
	if (TaskLocal == 0x14c)                                              // 0x103dfdb5 CMP 0x14c; JNZ 0x103dffd3 (0x103dfdba)
	{
		// The crawl-out: clear `m_iNeedsCrawlOutOfGround` when set, slot 67 `Unhide`, then
		// `RestartIdealActivity(0x1053)`, no completion.
		if (bZombieNeedsCrawlOutOfGround)                                 // 0x103dfdc0 / 0x103dfdc8
		{
			bZombieNeedsCrawlOutOfGround = false;                         // 0x103dfdca
		}
		Unhide();                                                         // 0x103dfdd5 CALL [EAX+0x10c]
		RestartIdealActivityId(0x1053);                                   // 0x103dfde2
		return 0;
	}
	return FElysiumNpcAnimal::StartTaskSlot442(Task);                    // 0x103dffd6 CNPC_VAnimal::StartTask
}
