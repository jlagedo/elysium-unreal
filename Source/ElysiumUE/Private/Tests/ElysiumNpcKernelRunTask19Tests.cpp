// Story 0019/8 (29e under the strict verdict), family **RunTask19** -- the family's tests.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelRunTask19.` and the retail address.
//
// Owns (RunTask19's `rule` rows): 0x10288780 CAI_BaseNPC::RunTask, 0x102aacf0
// CAI_BaseNPCTroika::RunTask, 0x1038d130 CNPC_VManBat::RunTask, 0x1035f940 CNPC_VAnimal::RunTask,
// 0x10384ab0 CNPC_VHuman::RunTask, 0x10393930 CNPC_VMingXiao::RunTask, 0x1039d750
// CNPC_VMingXiaoTentacle::RunTask, 0x103bb1e0 CNPC_VTzimisce::RunTask, 0x103c3870
// CNPC_VTzimisceRunner::RunTask, 0x103cdfb0 CNPC_VWerewolf::RunTask, 0x103652b0
// CNPC_VBach::RunTask, 0x10374a20 CNPC_VDog::RunTask, 0x103793e0 CNPC_VGargoyle::RunTask,
// 0x1037b9f0 CNPC_VGhoulCroucher::RunTask, 0x10380cb0 CNPC_VHengeyokai::RunTask, 0x103b38a0
// CNPC_VTaxiDriver::RunTask, 0x103c5f40 CNPC_VVampireBoss::RunTask, 0x103e01d0
// CNPC_VZombie::RunTask, 0x1035d8b0 CNPC_VAndreiBlood::RunTask, 0x103612e0
// CNPC_VAsianVampire::RunTask, 0x1036bfc0 CNPC_VChangBros::RunTask, 0x103a8990
// CNPC_VSabbatLeader::RunTask, 0x103af780 CNPC_VSheriffMan::RunTask.
//
// Lane L05. Each case drives slot 444 directly on an NPC stood as the retail class the case
// exercises, with the task as a `FElysiumScheduleStep`, and asserts the task status, the failure
// code, and the words the arm writes. A spine body reached through a species (the base body on a
// Troika NPC) is called qualified, which is the direct thunk retail's chain takes.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include <type_traits>

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcAndreiBlood.h"
#include "Substrate/ElysiumNpcAsianVampire.h"
#include "Substrate/ElysiumNpcBach.h"
#include "Substrate/ElysiumNpcChangBros.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcDog.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcGargoyle.h"
#include "Substrate/ElysiumNpcGhoulCroucher.h"
#include "Substrate/ElysiumNpcHengeyokai.h"
#include "Substrate/ElysiumNpcHuman.h"
#include "Substrate/ElysiumNpcManBat.h"
#include "Substrate/ElysiumNpcMingXiao.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcSabbatLeader.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcSheriffMan.h"
#include "Substrate/ElysiumNpcTaxiDriver.h"
#include "Substrate/ElysiumNpcTzimisce.h"
#include "Substrate/ElysiumNpcTzimisceRunner.h"
#include "Substrate/ElysiumNpcVampireBoss.h"
#include "Substrate/ElysiumNpcWerewolf.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumNpcZombie.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleText.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

static constexpr EAutomationTestFlags GRunTask19Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace RunTask19TestShared
{
	// One NPC stood as `RetailClass` (the bare Troika line for `CAI_BaseNPCTroika`), quiet, with a
	// cleared schedule, plus the fixture's player.
	template <class T>
	struct TFixture
	{
		FElysiumNpcWorldFixture World;
		T* Npc = nullptr;

		explicit TFixture(const TCHAR* RetailClass)
			: World([RetailClass]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("runtask19_kernel"), 0x1944u);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					Builder.AddNpcOfClass(TEXT("npc"), FVector::ZeroVector, RetailClass);
					return Builder;
				}())
		{
			if constexpr (std::is_same_v<T, FElysiumNpc>)
			{
				Npc = World.Npc(TEXT("npc"));
			}
			else
			{
				Npc = World.NpcAs<T>(TEXT("npc"));
			}
			FElysiumNpcWorldFixture::Quiet({ Npc });
			FElysiumNpcWorldFixture::PrepareForKernelDrive(Npc);
		}

		double Now() const { return World.World.NowSeconds(); }
	};

	FElysiumScheduleStep Step(int32 TaskId, float Data = 0.f)
	{
		FElysiumScheduleStep Out;
		Out.TaskId = TaskId;
		Out.Data = Data;
		return Out;
	}

	// A fresh task: running, no conditions (TASK_FAILED blocks `TaskComplete(false)`), no failure.
	void Reset(FElysiumNpcBase& Npc)
	{
		Npc.Schedule.TaskStatus = EElysiumTaskStatus::Running;
		Npc.Cognition.Conditions.Reset();
		Npc.BaseScheduleHost.FailureReason = 0;
	}

	bool Completed(const FElysiumNpcBase& Npc)
	{
		return Npc.Schedule.TaskStatus == EElysiumTaskStatus::Complete;
	}

	bool Failed(const FElysiumNpcBase& Npc, int32 Reason)
	{
		return Npc.Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed)
			&& Npc.BaseScheduleHost.FailureReason == Reason;
	}

	void FinishActivity(FElysiumNpcBase& Npc)
	{
		Npc.bSequenceFinished = true;
		Npc.SequenceNumber = Npc.IdealSequence;
	}
}

// =================================================================================================
// `CAI_BaseNPC::RunTask` `0x10288780` -- one case per arm group, driven qualified on a Troika NPC.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19BaseWaitTest,
	"Elysium.Substrate.NpcKernelRunTask19.Base.Wait", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19BaseWaitTest::RunTest(const FString&)
{
	// `0x10288bb6` (tasks 2 / 0x67): complete once curtime is no longer below `m_flWaitFinished`.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x02);
	Reset(N);
	N.BaseScheduleHost.WaitFinished = F.Now() + 5.0;
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestFalse(TEXT("0x10288c2a: a future wait keeps running"), Completed(N));
	N.BaseScheduleHost.WaitFinished = F.Now();
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestTrue(TEXT("0x10288c34: at the stamp it completes"), Completed(N));
	S = Step(0x67);
	Reset(N);
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestTrue(TEXT("TASK_WAIT_RANDOM shares the arm"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19BaseWaitFaceEnemyTest,
	"Elysium.Substrate.NpcKernelRunTask19.Base.WaitFaceEnemy", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19BaseWaitFaceEnemyTest::RunTest(const FString&)
{
	// `0x10288bc1` (4, 0xb0, 0xb1): motor stop (`motor+0x2c = -1`), the enemy LKP aim when slot 364
	// refuses it, then the wait test.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x04);
	Reset(N);
	N.BaseScheduleHost.WaitFinished = F.Now() + 5.0;
	N.MotorYawClock = 3.f;
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestFalse(TEXT("still waiting"), Completed(N));
	TestTrue(TEXT("0x102e0b40 reset or 0x102e1e20 restamped the yaw clock"), N.MotorYawClock != 3.f);
	N.BaseScheduleHost.WaitFinished = F.Now() - 1.0;
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestTrue(TEXT("0x10288c34 completes past the stamp"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19BaseWaitPvsTest,
	"Elysium.Substrate.NpcKernelRunTask19.Base.WaitPvs", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19BaseWaitPvsTest::RunTest(const FString&)
{
	// `0x10288b7b`: spawnflag 0x400 completes at once (`0x1028970f`).
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x05);
	Reset(N);
	N.SpawnFlags |= 0x400;
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestTrue(TEXT("SF_NPC_ALWAYSTHINK completes"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19BaseMoveToTargetRangeTest,
	"Elysium.Substrate.NpcKernelRunTask19.Base.MoveToTargetRange", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19BaseMoveToTargetRangeTest::RunTest(const FString&)
{
	// `0x10288c43` (0xb): no `m_hTargetEnt` -> TaskFail(1) at line 0xc09.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x0b, 64.f);
	Reset(N);
	N.SetTarget(FElysiumEntityHandle::Invalid());
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestTrue(TEXT("0x10288c8b TaskFail(1)"), Failed(N, 1));
	TestFalse(TEXT("and no completion"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19BaseNoEntryArmsTest,
	"Elysium.Substrate.NpcKernelRunTask19.Base.EpilogueArmsAndDefault", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19BaseNoEntryArmsTest::RunTest(const FString&)
{
	// 0x1f / 0x68 / 0x76 / 0x77 sit on the epilogue `0x10289718`; an id with no entry (0x49,
	// `TASK_SOUND_DIE`) takes `0x102896f5`: DevMsg "No RunTask entry for %s" and TaskComplete.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	for (const int32 Id : { 0x1f, 0x68, 0x76, 0x77 })
	{
		FElysiumScheduleStep S = Step(Id);
		Reset(N);
		N.FElysiumNpcBase::RunTaskSlot444(&S);
		TestFalse(FString::Printf(TEXT("task 0x%x keeps running"), Id), Completed(N));
	}
	FElysiumScheduleStep S = Step(0x49);
	Reset(N);
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestTrue(TEXT("0x10289713: the default arm completes"), Completed(N));
	S = Step(0x500);
	Reset(N);
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestTrue(TEXT("0x10288798: an id past 0xb1 takes the same arm"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19BasePathArmsTest,
	"Elysium.Substrate.NpcKernelRunTask19.Base.PathTimedAndWithinDist", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19BasePathArmsTest::RunTest(const FString&)
{
	// `0x10289614` (0x24 / 0x27): past the wait -> m_bShouldMove = 0, complete, ClearGoal.
	// `0x10289599` (0x25 / 0x26): within the resolved range of the goal -> the same exit.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x24);
	Reset(N);
	N.BaseScheduleHost.bShouldMove = true;
	N.BaseScheduleHost.WaitFinished = F.Now() - 1.0;
	const int32 Clears = N.NavigationGoalClears;
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestTrue(TEXT("0x10288eff completes"), Completed(N));
	TestFalse(TEXT("0x1028963f clears m_bShouldMove"), N.BaseScheduleHost.bShouldMove);
	TestEqual(TEXT("0x10288f0a clears the goal"), N.NavigationGoalClears, Clears + 1);
	S = Step(0x25, 0.f);
	Reset(N);
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestTrue(TEXT("0x10289602: at the goal the range test passes"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19BaseActivityArmsTest,
	"Elysium.Substrate.NpcKernelRunTask19.Base.ActivityFinishedArms", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19BaseActivityArmsTest::RunTest(const FString&)
{
	// 0x2a (`0x102891cf`), 0x39..0x3c / 0x52 / 0x53 (`0x102891c8`), 0x34.. (`0x102891f4`),
	// 0x54..0x56 (`0x102887c6`): each completes on slot 251 and waits without it.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	for (const int32 Id : { 0x2a, 0x39, 0x52, 0x34, 0x3e, 0x55 })
	{
		FElysiumScheduleStep S = Step(Id);
		Reset(N);
		N.bSequenceFinished = false;
		N.FElysiumNpcBase::RunTaskSlot444(&S);
		TestFalse(FString::Printf(TEXT("0x%x waits on the activity"), Id), Completed(N));
		FinishActivity(N);
		N.FElysiumNpcBase::RunTaskSlot444(&S);
		TestTrue(FString::Printf(TEXT("0x%x completes once it finishes"), Id), Completed(N));
	}
	const int32 Reissues = N.TroikaMotor.MoveReissues;
	FElysiumScheduleStep S = Step(0x34);
	Reset(N);
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestTrue(TEXT("0x10289280: the attack arm aims the motor (0x102e1c10)"),
		N.TroikaMotor.MoveReissues > Reissues);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19BaseFacingArmsTest,
	"Elysium.Substrate.NpcKernelRunTask19.Base.FacingArms", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19BaseFacingArmsTest::RunTest(const FString&)
{
	// 0x2b.. (`0x10288b4c`) and 0x6a / 0x6b (`0x102887ad`): UpdateYaw(-1) then FacingIdeal; 0x2e
	// (`0x102889b5`): stop, aim at the LKP at -1.0, FacingIdeal; 0x2d (`0x10288a18`): the player,
	// SetTurnActivity, and the wait-plus-yaw completion.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	for (const int32 Id : { 0x2b, 0x2f, 0x66, 0x6a, 0x2e })
	{
		FElysiumScheduleStep S = Step(Id);
		Reset(N);
		const int32 Updates = N.MotorUpdateYawCalls;
		N.FElysiumNpcBase::RunTaskSlot444(&S);
		TestTrue(FString::Printf(TEXT("0x%x turned the motor"), Id), N.MotorUpdateYawCalls > Updates);
		TestTrue(FString::Printf(TEXT("0x%x completes facing the ideal"), Id), Completed(N));
	}
	FElysiumScheduleStep S = Step(0x2d);
	Reset(N);
	N.BaseScheduleHost.WaitFinished = F.Now() + 5.0;
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestFalse(TEXT("0x10288aed: TASK_FACE_PLAYER waits out m_flWaitFinished"), Completed(N));
	N.BaseScheduleHost.WaitFinished = F.Now() - 1.0;
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestTrue(TEXT("0x10288b13: then completes with the yaw under 10"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19BaseHintAndWeaponFailsTest,
	"Elysium.Substrate.NpcKernelRunTask19.Base.HintAndWeaponFails", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19BaseHintAndWeaponFailsTest::RunTest(const FString&)
{
	// 0x30 with no `m_pHintNode` -> TaskFail(4) at 0xb5c; 0x72 with no target -> TaskFail(3) at
	// 0xc3a; 0x71 on a finished activity with no weapon target completes (crash guard).
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x30);
	Reset(N);
	N.BaseScheduleHost.HintNode = INDEX_NONE;
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestTrue(TEXT("0x10288893 TaskFail(4)"), Failed(N, 4));
	S = Step(0x72);
	Reset(N);
	N.SetTarget(FElysiumEntityHandle::Invalid());
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestTrue(TEXT("0x10288f33 TaskFail(3)"), Failed(N, 3));
	S = Step(0x71);
	Reset(N);
	FinishActivity(N);
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestTrue(TEXT("0x10289713: an unowned (absent) weapon completes"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19BaseReloadAndSequenceTest,
	"Elysium.Substrate.NpcKernelRunTask19.Base.ReloadAndSetActivity", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19BaseReloadAndSequenceTest::RunTest(const FString&)
{
	// 0x38 (`0x102890f3`): with no active weapon a finished activity completes at `0x10289713`;
	// 0x4b (`0x102889a2`): `m_nSequence == m_nIdealSequence` completes.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x38);
	Reset(N);
	N.bSequenceFinished = false;
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestFalse(TEXT("0x1028916f: the reload waits on the activity"), Completed(N));
	FinishActivity(N);
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestTrue(TEXT("and completes"), Completed(N));
	S = Step(0x4b);
	Reset(N);
	N.SequenceNumber = 3;
	N.IdealSequence = 4;
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestFalse(TEXT("different sequences keep running"), Completed(N));
	N.IdealSequence = 3;
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestTrue(TEXT("0x10288c34 on the ideal sequence"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19BaseDieTest,
	"Elysium.Substrate.NpcKernelRunTask19.Base.Die", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19BaseDieTest::RunTest(const FString&)
{
	// `0x10288fc4` (0x5f): finished with m_flCycle >= 1.0 -> m_lifeState 2, the (4,4,1)/(-4,-4,0)
	// hull and, without SF 0x200, the SOUND_CARCASS insert (0x20, 0x180, 30.0). No completion.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x5f);
	Reset(N);
	FinishActivity(N);
	N.SequenceCycle = 0.5f;
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestNotEqual(TEXT("0x10288fe9: a cycle below 1.0 waits"), N.AnimEventLifeStateWord, 2);
	N.SequenceCycle = 1.f;
	N.SpawnFlags &= ~0x200;
	const int32 Sizes = N.SetSizeCalls;
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestEqual(TEXT("0x10288ff7 m_lifeState = 2"), N.AnimEventLifeStateWord, 2);
	TestEqual(TEXT("0x102890a3 UTIL_SetSize"), N.SetSizeCalls, Sizes + 1);
	TestEqual(TEXT("the maxs are (4,4,1)"), N.LastSetSizeMaxsUnits, FVector(4.f, 4.f, 1.f));
	TestEqual(TEXT("0x102890e1 SOUND_CARCASS"), N.InsertedAiSoundType, 0x20);
	TestEqual(TEXT("volume 0x180"), N.InsertedAiSoundVolume, 0x180);
	TestFalse(TEXT("the arm never completes"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19BaseScriptArmsTest,
	"Elysium.Substrate.NpcKernelRunTask19.Base.ScriptArms", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19BaseScriptArmsTest::RunTest(const FString&)
{
	// 0x60 with no live `m_hCine` -> "Cine died!" and complete (`0x1028942a`); 0x62 with the
	// sequence finished and no cine -> complete (`0x1028970f`); 0x62 unfinished waits.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x60);
	Reset(N);
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestTrue(TEXT("a dead cine completes"), Completed(N));
	S = Step(0x62);
	Reset(N);
	N.bSequenceFinished = false;
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestFalse(TEXT("0x10289448 waits on m_bSequenceFinished"), Completed(N));
	N.bSequenceFinished = true;
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestTrue(TEXT("then completes"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19BaseStopAndMovementTest,
	"Elysium.Substrate.NpcKernelRunTask19.Base.StopMovingAndWaitForMovement", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19BaseStopAndMovementTest::RunTest(const FString&)
{
	// 0x69 (`0x102888d4`) on the ground nav type: ideal activity from `0x1027a6c0`, m_bShouldMove 0,
	// complete. 0x6e (`0x10288f43`) with no goal: stop, complete, ClearGoal. 0x74 on the ground.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x69);
	Reset(N);
	N.NavSetType(0);
	N.BaseScheduleHost.bShouldMove = true;
	N.FElysiumNpcBase::RunTaskSlot444(&S);
	TestTrue(TEXT("0x10288993 completes"), Completed(N));
	TestFalse(TEXT("0x1028898c m_bShouldMove = 0"), N.BaseScheduleHost.bShouldMove);
	TestEqual(TEXT("0x10288983 the stopped activity is ACT_IDLE"), N.IdealActivityNumber, 1);
	// The default fixture provides no mover (`bProvideNpcMotor` false): no goal, the flag arm.
	{
		S = Step(0x6e);
		Reset(N);
		const int32 Clears = N.NavigationGoalClears;
		N.FElysiumNpcBase::RunTaskSlot444(&S);
		TestTrue(TEXT("0x10288eff: no goal completes"), Completed(N));
		TestEqual(TEXT("0x10288f0a ClearGoal"), N.NavigationGoalClears, Clears + 1);
		S = Step(0x74);
		Reset(N);
		N.Flags &= ~1;
		N.FElysiumNpcBase::RunTaskSlot444(&S);
		TestFalse(TEXT("0x102896e0: airborne waits"), Completed(N));
		N.Flags |= 1;
		N.FElysiumNpcBase::RunTaskSlot444(&S);
		TestTrue(TEXT("FL_ONGROUND completes"), Completed(N));
	}
	return true;
}

// =================================================================================================
// `CAI_BaseNPCTroika::RunTask` `0x102aacf0` -- one case per arm group.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19TroikaChainsTest,
	"Elysium.Substrate.NpcKernelRunTask19.Troika.ChainsToBase", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19TroikaChainsTest::RunTest(const FString&)
{
	// Index 0 `0x102aad61` (2/0x67/0x68) aims and falls to the base; index 0x38 and out-of-range ids
	// go straight to `CAI_BaseNPC::RunTask`; 0xed sits on the epilogue.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x02);
	Reset(N);
	N.BaseScheduleHost.WaitFinished = F.Now() - 1.0;
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("TASK_WAIT completes through the base arm"), Completed(N));
	S = Step(0x49);
	Reset(N);
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x49 reaches the base default"), Completed(N));
	S = Step(0xed);
	Reset(N);
	N.RunTaskSlot444(&S);
	TestFalse(TEXT("0x102aad71: TASK_DO_COMFORT_LOOP keeps running"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19TroikaWaitFaceEnemyTest,
	"Elysium.Substrate.NpcKernelRunTask19.Troika.WaitFaceEnemyAndCheer", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19TroikaWaitFaceEnemyTest::RunTest(const FString&)
{
	// Index 1 `0x102ab659` (4/0xb0/0xb1): no override and no enemy -> TaskFail(6) at 0x407c.
	// Index 0x34 `0x102ac169` (0x128) the same at 0x4304.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	for (const int32 Id : { 0x04, 0x128 })
	{
		FElysiumScheduleStep S = Step(Id);
		Reset(N);
		N.ShootTargetOverride = FElysiumEntityHandle::Invalid();
		ElysiumNpcEnemy::SetEnemy(N, FElysiumEntityHandle::Invalid());
		N.RunTaskSlot444(&S);
		TestTrue(FString::Printf(TEXT("0x%x TaskFail(6)"), Id), Failed(N, 6));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19TroikaWaitPvsTest,
	"Elysium.Substrate.NpcKernelRunTask19.Troika.WaitPvs", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19TroikaWaitPvsTest::RunTest(const FString&)
{
	// Index 2 `0x102aad7e`: spawnflag 0x400 completes (`0x102ac11b`).
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x05);
	Reset(N);
	N.SpawnFlags |= 0x400;
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("SF_NPC_ALWAYSTHINK completes"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19TroikaFacingArmsTest,
	"Elysium.Substrate.NpcKernelRunTask19.Troika.FacingArms", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19TroikaFacingArmsTest::RunTest(const FString&)
{
	// Index 4 (0x2e), 0x17 (0xa7/0x11d/0x12e), 0x2a (0xf7): complete on FacingIdeal. Index 0x18
	// (0xb2) with no interesting place -> TaskFail(0x22) at 0x4105; 0x1a (0xb4) -> TaskFail(0x23).
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	for (const int32 Id : { 0x2e, 0xa7, 0x11d, 0x12e, 0xf7 })
	{
		FElysiumScheduleStep S = Step(Id);
		Reset(N);
		N.RunTaskSlot444(&S);
		TestTrue(FString::Printf(TEXT("0x%x completes facing"), Id), Completed(N));
	}
	FElysiumScheduleStep S = Step(0xb2);
	Reset(N);
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x102ab961 TaskFail(0x22)"), Failed(N, 0x22));
	S = Step(0xb4);
	Reset(N);
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x102aba59 TaskFail(0x23)"), Failed(N, 0x23));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19TroikaAttackArmsTest,
	"Elysium.Substrate.NpcKernelRunTask19.Troika.AttackArms", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19TroikaAttackArmsTest::RunTest(const FString&)
{
	// Index 5 `0x102ab0a9` (0x34) with no burst completes on the activity (`0x102ab1da`); a burst
	// with no weapon completes at `0x102ab181`. Index 6 / 7 / 0x0e / 0x0f: activity waits.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	for (const int32 Id : { 0x34, 0x35, 0x36, 0x92, 0x93 })
	{
		FElysiumScheduleStep S = Step(Id);
		Reset(N);
		N.BurstFireCount = 0;
		N.bSequenceFinished = false;
		N.RunTaskSlot444(&S);
		TestFalse(FString::Printf(TEXT("0x%x waits"), Id), Completed(N));
		FinishActivity(N);
		N.RunTaskSlot444(&S);
		TestTrue(FString::Printf(TEXT("0x%x completes"), Id), Completed(N));
	}
	FElysiumScheduleStep S = Step(0x34);
	Reset(N);
	N.BurstFireCount = 2;
	N.bSequenceFinished = false;
	if (N.ActiveWeaponEntity() == nullptr)
	{
		N.RunTaskSlot444(&S);
		TestTrue(TEXT("0x102ab181: a burst with no weapon completes"), Completed(N));
		TestEqual(TEXT("and the count is not spent"), N.BurstFireCount, 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19TroikaSetActivityTest,
	"Elysium.Substrate.NpcKernelRunTask19.Troika.SetActivityAndCower", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19TroikaSetActivityTest::RunTest(const FString&)
{
	// Index 8 `0x102aad1f` (0x4b): the sequence, else the one-second watchdog. Index 0x27 (0xe7):
	// the sequence only.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x4b);
	Reset(N);
	N.SequenceNumber = 1;
	N.IdealSequence = 2;
	N.BaseScheduleHost.WaitFinished = F.Now() + 1.0;
	N.RunTaskSlot444(&S);
	TestFalse(TEXT("0x102aad49 keeps running inside the watchdog"), Completed(N));
	N.BaseScheduleHost.WaitFinished = F.Now() - 0.1;
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x102aad4f completes on the watchdog"), Completed(N));
	S = Step(0xe7);
	Reset(N);
	N.RunTaskSlot444(&S);
	TestFalse(TEXT("TASK_SET_COWER has no watchdog"), Completed(N));
	N.IdealSequence = 1;
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x102aad49 on the ideal sequence"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19TroikaDieTest,
	"Elysium.Substrate.NpcKernelRunTask19.Troika.DieArms", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19TroikaDieTest::RunTest(const FString&)
{
	// Index 0x0a `0x102abb90` (0x5f): ideal ACT_IDLE opens the gate; 0x5f credits SELF; 0xdf
	// (`0x102abc76`) is a bare Die(0,0,0). Neither completes.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x5f);
	Reset(N);
	N.bSequenceFinished = false;
	N.IdealActivityNumber = 7;
	N.RunTaskSlot444(&S);
	TestEqual(TEXT("0x102abbbe: neither gate arm -> no Die"), N.RunTaskDieCalls, 0);
	N.IdealActivityNumber = 1;
	N.AnimEventLifeStateWord = 1;
	N.RunTaskSlot444(&S);
	TestEqual(TEXT("0x102abc1e Die"), N.RunTaskDieCalls, 1);
	TestTrue(TEXT("0x102abc03: TASK_DIE credits itself"), N.LastDieCredit == N.Handle);
	TestEqual(TEXT("0x102abc0d: m_lifeState 1 -> 0"), N.AnimEventLifeStateWord, 0);
	TestFalse(TEXT("no completion"), Completed(N));
	S = Step(0xdf);
	Reset(N);
	N.RunTaskSlot444(&S);
	TestEqual(TEXT("0x102abc7e Die(0,0,0)"), N.RunTaskDieCalls, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19TroikaMovementTest,
	"Elysium.Substrate.NpcKernelRunTask19.Troika.WaitForMovementAndPatrol", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19TroikaMovementTest::RunTest(const FString&)
{
	// Index 0x0b `0x102aaf2e` (0x6e) with no goal: m_bShouldMove 0, complete, ClearGoal. Index
	// 0x0c / 0x0d (0x7a / 0x7b) reach the patrol step `0x102aa860` on the right path.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	// The default fixture provides no mover (`bProvideNpcMotor` false): no goal, the flag arm.
	{
		FElysiumScheduleStep S = Step(0x6e);
		Reset(N);
		N.BaseScheduleHost.bShouldMove = true;
		N.RunTaskSlot444(&S);
		TestTrue(TEXT("0x102aaff2 completes"), Completed(N));
		TestFalse(TEXT("0x102aafeb m_bShouldMove = 0"), N.BaseScheduleHost.bShouldMove);
	}
	// 0x7a / 0x7b reach `0x102aa860` (L10's `IssuePatrolMoveRun`) on `+0x658c` / `+0x6594`. A cell
	// with no path fails `0x1d` (`0x102aa97d`); a path whose current node is -1 returns silently
	// (`0x102aa88a`). The hunt cell holds such a path, the plain cell none.
	FElysiumNpc::FPatrolPathRecord HuntPath;
	HuntPath.Count = 1;
	HuntPath.Nodes[0] = -1;
	N.PatrolPathCell.Path = nullptr;
	N.PatrolPathHuntCell.Path = &HuntPath;
	{
		FElysiumScheduleStep S = Step(0x7b);
		Reset(N);
		N.RunTaskSlot444(&S);
		TestFalse(TEXT("0x102ab033: the hunt path's -1 node returns without a fail"),
			N.Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));
	}
	FElysiumScheduleStep S = Step(0x7a);
	Reset(N);
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x102ab018: no m_sppPatrolPath fails 0x1d"), Failed(N, 0x1d));
	N.PatrolPathHuntCell.Path = nullptr;
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19TroikaKnockbackAndJumpTest,
	"Elysium.Substrate.NpcKernelRunTask19.Troika.KnockbackAndJump", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19TroikaKnockbackAndJumpTest::RunTest(const FString&)
{
	// Index 0x10 `0x102ac4ef` (0x94) and 0x37 `0x102ac69c` (0x13a): on the ground in NAV_JUMP they
	// land at once -- nav type 0, m_bJumping 0, complete.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	// The default fixture provides no mover (`bProvideNpcMotor` is false), so the grounded answer is
	// the `FL_ONGROUND` flag arm.
	for (const int32 Id : { 0x94, 0x13a })
	{
		FElysiumScheduleStep S = Step(Id);
		Reset(N);
		N.NavSetType(1);
		N.Flags |= 1;
		N.bJumping = true;
		N.RunTaskSlot444(&S);
		TestTrue(FString::Printf(TEXT("0x%x lands and completes"), Id), Completed(N));
		TestEqual(TEXT("NAV_GROUND"), N.NavGetType(), 0);
		TestFalse(TEXT("m_bJumping cleared"), N.bJumping);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19TroikaFinishingMoveTest,
	"Elysium.Substrate.NpcKernelRunTask19.Troika.FinishingMoveAndOnFire", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19TroikaFinishingMoveTest::RunTest(const FString&)
{
	// Index 0x13 `0x102ac2ed` (0x99) unfinished samples the bone time (`0x102ac4c7`); finished it
	// completes (`0x102ac496`). Index 0x15 `0x102abffe` (0x9d) finished completes (`0x102ac11b`);
	// 0x14 / 0x16 (0x9c / 0x9e) complete on the activity.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x99);
	Reset(N);
	N.bSequenceFinished = false;
	N.FinishingMoveBoneTrackLastTime = -5.0;
	N.RunTaskSlot444(&S);
	TestEqual(TEXT("0x102ac4c7 stamps the sample time"), N.FinishingMoveBoneTrackLastTime, F.Now());
	TestFalse(TEXT("still running"), Completed(N));
	FinishActivity(N);
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x102ac496 completes"), Completed(N));
	for (const int32 Id : { 0x9c, 0x9d, 0x9e })
	{
		S = Step(Id);
		Reset(N);
		FinishActivity(N);
		N.RunTaskSlot444(&S);
		TestTrue(FString::Printf(TEXT("0x%x completes"), Id), Completed(N));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19TroikaFlagArmsTest,
	"Elysium.Substrate.NpcKernelRunTask19.Troika.FlagClearingArms", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19TroikaFlagArmsTest::RunTest(const FString&)
{
	// 0xde (`0x102ab045`) clears with 0xbbf5ffff; the look tasks (`0x102ab76a`) clear 0x08000000;
	// 0x107 (`0x102ab7d8`) clears 0x4000; 0x116 (`0x102abc90`) clears 0x40000; 0xb8 clears 0x20000000.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	struct FRow { int32 Id; uint32 Cleared; int32 Ideal; };
	const FRow Rows[] = {
		{ 0xde, 0x440a0000u, 0x1068 },
		{ 0xf8, 0x08000000u, 0 },
		{ 0x107, 0x00004000u, 0 },
		{ 0x116, 0x00040000u, 0 },
		{ 0xb8, 0x20000000u, 0 },
	};
	for (const FRow& Row : Rows)
	{
		FElysiumScheduleStep S = Step(Row.Id);
		Reset(N);
		FinishActivity(N);
		N.IdealActivityNumber = Row.Ideal;
		N.BaseScheduleHost.WaitFinished = F.Now() - 1.0;
		N.NpcFlags.AssignAiFlagsWord(0xffffffffu);
		N.RunTaskSlot444(&S);
		TestTrue(FString::Printf(TEXT("0x%x completes"), Row.Id), Completed(N));
		TestTrue(FString::Printf(TEXT("0x%x clears its bits"), Row.Id),
			(N.NpcFlags.RawWord1() & Row.Cleared) == 0u);
	}
	FElysiumScheduleStep S = Step(0x107);
	Reset(N);
	FinishActivity(N);
	N.IdealActivityNumber = 0xf1d;
	N.RunTaskSlot444(&S);
	TestFalse(TEXT("0x102ab7f4: a dive still in 0xf1d does not complete"), Completed(N));
	TestEqual(TEXT("0x102ab7fd SetIdealActivity(0x1052)"), N.IdealActivityNumber, 0x1052);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19TroikaCircleArmsTest,
	"Elysium.Substrate.NpcKernelRunTask19.Troika.StepBackAndCircle", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19TroikaCircleArmsTest::RunTest(const FString&)
{
	// 0x11b (`0x102abd3b`): past the wait, m_flDesiredMoveYaw 0 and complete. 0x122 (`0x102abdb8`)
	// inside the wait with no sequence for 0x1121 or ACT_WALK -> TaskFail(0x15) at 0x4255.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x11b);
	Reset(N);
	N.ScheduleHost.DesiredMoveYaw = 30.f;
	N.BaseScheduleHost.WaitFinished = F.Now() - 1.0;
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x102ab931 completes"), Completed(N));
	TestEqual(TEXT("0x102abda9 m_flDesiredMoveYaw = 0"), N.ScheduleHost.DesiredMoveYaw, 0.f);
	S = Step(0x122);
	Reset(N);
	FinishActivity(N);
	N.BaseScheduleHost.WaitFinished = F.Now() + 5.0;
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x102abe8a TaskFail(0x15)"), Failed(N, 0x15));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19TroikaDialogTest,
	"Elysium.Substrate.NpcKernelRunTask19.Troika.RunDialogAndDisposition", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19TroikaDialogTest::RunTest(const FString&)
{
	// 0xb9 (`0x102ab303`): a finished dialogue (-1) completes and clears COND 0x6f. 0xba
	// (`0x102ab351`): slot 588 then the wait test.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CAI_BaseNPCTroika"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	FElysiumScheduleStep S = Step(0xb9);
	Reset(N);
	N.Cognition.Conditions.Set(EElysiumNpcCond::HearPlayer);
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x102ab313 completes"), Completed(N));
	TestFalse(TEXT("0x102ab31c ClearCondition(0x6f)"), N.Cognition.Conditions.Has(EElysiumNpcCond::HearPlayer));
	S = Step(0xba);
	Reset(N);
	N.BaseScheduleHost.WaitFinished = F.Now() - 1.0;
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x102aad4f: TASK_RUN_DISPOSITION completes past the wait"), Completed(N));
	return true;
}

// =================================================================================================
// The species overrides -- one case per row.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19AnimalTest,
	"Elysium.Substrate.NpcKernelRunTask19.Species.Animal", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19AnimalTest::RunTest(const FString&)
{
	// `0x1035f940` (on `CNPC_VRat`): 0x89 completes when curtime - m_flLastAttackTime exceeds the
	// task data (`0x1035fa2b`); 0x8b on the activity.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpc> F(TEXT("CNPC_VRat"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpc& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x89, 2.f);
	Reset(N);
	N.bSequenceFinished = false;
	N.LastAttackTime = F.Now() - 1.0;
	N.RunTaskSlot444(&S);
	TestFalse(TEXT("one second of two keeps running"), Completed(N));
	N.LastAttackTime = F.Now() - 3.0;
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("three seconds completes"), Completed(N));
	S = Step(0x8b);
	Reset(N);
	FinishActivity(N);
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x1035fa57 completes"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19DogTest,
	"Elysium.Substrate.NpcKernelRunTask19.Species.Dog", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19DogTest::RunTest(const FString&)
{
	// `0x10374a20`: TASK_WAIT in ALERT aims at the local player, then CNPC_VAnimal / the base wait.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpcDog> F(TEXT("CNPC_VDog"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpcDog& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x02);
	Reset(N);
	N.SetState(3);
	N.BaseScheduleHost.WaitFinished = F.Now() - 1.0;
	const int32 Reissues = N.TroikaMotor.MoveReissues;
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x10374a5f aims the motor at the player"), N.TroikaMotor.MoveReissues > Reissues);
	TestTrue(TEXT("0x10374a68 chains to the wait"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19ZombieTest,
	"Elysium.Substrate.NpcKernelRunTask19.Species.Zombie", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19ZombieTest::RunTest(const FString&)
{
	// `0x103e01d0`: 0x14e completes and sets misc flag 0x80000 (`0x103e0210`); 0x151 past the wait
	// stops and completes.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpcZombie> F(TEXT("CNPC_VZombie"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpcZombie& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x14e);
	Reset(N);
	FinishActivity(N);
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x103e0204 completes"), Completed(N));
	TestTrue(TEXT("AddMiscFlag(0x80000)"), (N.MiscFlags & 0x80000u) != 0);
	S = Step(0x151);
	Reset(N);
	N.BaseScheduleHost.WaitFinished = F.Now() - 1.0;
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x103e0282 completes"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19HumanTest,
	"Elysium.Substrate.NpcKernelRunTask19.Species.Human", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19HumanTest::RunTest(const FString&)
{
	// `0x10384ab0`: 0x8d with no enemy completes at once (`0x10384cfb`); 0x8b with no enemy keeps
	// running; 0x8e completes once m_flNextAttack has passed (`0x10384cb2`).
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpcHuman> F(TEXT("CNPC_VHuman"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpcHuman& N = *F.Npc;
	ElysiumNpcEnemy::SetEnemy(N, FElysiumEntityHandle::Invalid());
	FElysiumScheduleStep S = Step(0x8d);
	Reset(N);
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("no enemy completes"), Completed(N));
	S = Step(0x8b);
	Reset(N);
	N.RunTaskSlot444(&S);
	TestFalse(TEXT("0x10384b55: no enemy keeps the swing running"), Completed(N));
	S = Step(0x8e);
	Reset(N);
	N.bSequenceFinished = false;
	N.NextAttackTime = F.Now() + 5.0;
	N.RunTaskSlot444(&S);
	TestFalse(TEXT("waiting for m_flNextAttack"), Completed(N));
	N.NextAttackTime = F.Now();
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x10384cc9 completes"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19GargoyleTest,
	"Elysium.Substrate.NpcKernelRunTask19.Species.Gargoyle", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19GargoyleTest::RunTest(const FString&)
{
	// `0x103793e0`: 0x31 completes facing the ideal; 0x12f on the activity.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpcGargoyle> F(TEXT("CNPC_VGargoyle"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpcGargoyle& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x31);
	Reset(N);
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x1037943e completes"), Completed(N));
	S = Step(0x12f);
	Reset(N);
	N.bSequenceFinished = false;
	N.RunTaskSlot444(&S);
	TestFalse(TEXT("0x12f waits"), Completed(N));
	FinishActivity(N);
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("then completes"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19GhoulCroucherTest,
	"Elysium.Substrate.NpcKernelRunTask19.Species.GhoulCroucher", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19GhoulCroucherTest::RunTest(const FString&)
{
	// `0x1037b9f0`: 0x14b latches m_bUnawareExited (`0x1037ba8b`) only on a finished activity.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpcGhoulCroucher> F(TEXT("CNPC_VGhoulCroucher"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpcGhoulCroucher& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x14b);
	Reset(N);
	N.bSequenceFinished = false;
	N.bUnawareExited = false;
	N.RunTaskSlot444(&S);
	TestFalse(TEXT("unfinished leaves the byte"), N.bUnawareExited);
	FinishActivity(N);
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("+0x6667 = 1"), N.bUnawareExited);
	TestTrue(TEXT("and completes"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19TaxiDriverTest,
	"Elysium.Substrate.NpcKernelRunTask19.Species.TaxiDriver", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19TaxiDriverTest::RunTest(const FString&)
{
	// `0x103b38a0`: 0xb9 with the dialogue done completes and clears m_bFirstThink (`0x103b38df`).
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpcTaxiDriver> F(TEXT("CNPC_VTaxiDriver"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpcTaxiDriver& N = *F.Npc;
	FElysiumScheduleStep S = Step(0xb9);
	Reset(N);
	N.bTaxiFirstThink = true;
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x103b38cb completes"), Completed(N));
	TestFalse(TEXT("+0x6660 = 0"), N.bTaxiFirstThink);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19VampireBossAndSheriffTest,
	"Elysium.Substrate.NpcKernelRunTask19.Species.VampireBossAndSheriffMan", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19VampireBossAndSheriffTest::RunTest(const FString&)
{
	// `0x103af780`: SheriffMan swallows 0x154; everything else goes to `0x103c5f40`, which sends
	// all but 0x14d to CNPC_VHuman (0x8e completes on the activity there).
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpcSheriffMan> F(TEXT("CNPC_VSheriffMan"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpcSheriffMan& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x154);
	Reset(N);
	FinishActivity(N);
	N.RunTaskSlot444(&S);
	TestFalse(TEXT("0x154 is swallowed"), Completed(N));
	S = Step(0x8e);
	Reset(N);
	FinishActivity(N);
	N.FElysiumNpcVampireBoss::RunTaskSlot444(&S);
	TestTrue(TEXT("0x103c5f9d chains to CNPC_VHuman"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19AndreiBloodTest,
	"Elysium.Substrate.NpcKernelRunTask19.Species.AndreiBlood", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19AndreiBloodTest::RunTest(const FString&)
{
	// `0x1035d8b0`: 0x154 sets m_bForceTeleport and completes; 0x156 then completes on it.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpcAndreiBlood> F(TEXT("CNPC_VAndreiBlood"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpcAndreiBlood& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x154);
	Reset(N);
	FinishActivity(N);
	N.bAndreiForceTeleport = false;
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x1035d9e8 m_bForceTeleport = 1"), N.bAndreiForceTeleport);
	S = Step(0x156);
	Reset(N);
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x1035da3b completes"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19AsianVampireTest,
	"Elysium.Substrate.NpcKernelRunTask19.Species.AsianVampire", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19AsianVampireTest::RunTest(const FString&)
{
	// `0x103612e0`: 0x150 clears m_bPathBlocked and completes; 0x151 keeps running.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpcAsianVampire> F(TEXT("CNPC_VAsianVampire"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpcAsianVampire& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x150);
	Reset(N);
	N.bAsianVampirePathBlocked = true;
	N.RunTaskSlot444(&S);
	TestFalse(TEXT("0x10361369 m_bPathBlocked = 0"), N.bAsianVampirePathBlocked);
	TestTrue(TEXT("0x10361370 completes"), Completed(N));
	S = Step(0x151);
	Reset(N);
	N.RunTaskSlot444(&S);
	TestFalse(TEXT("0x151 runs on"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19ChangBrosTest,
	"Elysium.Substrate.NpcKernelRunTask19.Species.ChangBros", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19ChangBrosTest::RunTest(const FString&)
{
	// `0x1036bfc0`: 0x156 completes once m_fEnergyChargeTime passes; 0x157 spawns the ball once the
	// cycle reaches 0.591.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpcChangBros> F(TEXT("CNPC_VChangBros"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpcChangBros& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x156);
	Reset(N);
	N.ChangEnergyChargeTime = F.Now() + 5.0;
	N.RunTaskSlot444(&S);
	TestFalse(TEXT("still charging"), Completed(N));
	N.ChangEnergyChargeTime = F.Now();
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x1036c081 completes"), Completed(N));
	S = Step(0x157);
	Reset(N);
	N.bChangEnergyBallSpawned = false;
	N.SequenceCycle = 0.6f;
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x1036c1c4 m_bEnergyBallSpawned = 1"), N.bChangEnergyBallSpawned);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19SabbatLeaderTest,
	"Elysium.Substrate.NpcKernelRunTask19.Species.SabbatLeader", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19SabbatLeaderTest::RunTest(const FString&)
{
	// `0x103a8990`: 0x15e completes once m_fWarningFinishTime passes; 0x162 one second after
	// m_fTaskStartTime; 0x15d clears m_fEffects 0x20.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpcSabbatLeader> F(TEXT("CNPC_VSabbatLeader"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpcSabbatLeader& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x15e);
	Reset(N);
	N.SabbatLeaderWarningFinishTime = F.Now() - 1.0;
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x103a9079 completes"), Completed(N));
	S = Step(0x162);
	Reset(N);
	N.SabbatLeaderTaskStartTime = F.Now() - 0.5;
	N.RunTaskSlot444(&S);
	TestFalse(TEXT("half a second keeps running"), Completed(N));
	// `0x103a906b AND EAX,0x4100` / `0x103a9070 JNZ`: only an ordered `elapsed > 1.0` completes
	// (L05 integration: the case stood at exactly one second, which retail keeps running).
	N.SabbatLeaderTaskStartTime = F.Now() - 1.5;
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("past one second completes"), Completed(N));
	S = Step(0x15d);
	Reset(N);
	FinishActivity(N);
	N.EffectsWord |= 0x20u;
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x103a8f7e m_fEffects &= ~0x20"), (N.EffectsWord & 0x20u) == 0u);
	TestTrue(TEXT("and completes"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19ManBatTest,
	"Elysium.Substrate.NpcKernelRunTask19.Species.ManBat", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19ManBatTest::RunTest(const FString&)
{
	// `0x1038d130`: 0x163 waits out m_flCoastTimer; 0x159 with no fly-by target -> TaskFail(1);
	// an unhandled id goes to CAI_BaseNPC::RunTask DIRECT (0x49 -> the base default completes).
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpcManBat> F(TEXT("CNPC_VManBat"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpcManBat& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x163);
	Reset(N);
	N.ManBatCoastTimer = F.Now() + 3.0;
	N.RunTaskSlot444(&S);
	TestFalse(TEXT("coasting"), Completed(N));
	N.ManBatCoastTimer = F.Now();
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x1038dc74 completes"), Completed(N));
	S = Step(0x159);
	Reset(N);
	N.ManBatFlyByTarget = FElysiumEntityHandle::Invalid();
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x1038d6e8 TaskFail(1)"), Failed(N, 1));
	S = Step(0x49);
	Reset(N);
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x1038dc89 the base default"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19MingXiaoTest,
	"Elysium.Substrate.NpcKernelRunTask19.Species.MingXiao", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19MingXiaoTest::RunTest(const FString&)
{
	// `0x10393930`: 0x14b reaches `0x1039aa20`; 0x158 outside mode 3 releases the throw and completes.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpcMingXiao> F(TEXT("CNPC_VMingXiao"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpcMingXiao& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x14b);
	Reset(N);
	N.RunTaskSlot444(&S);
	TestEqual(TEXT("0x10393b1d"), N.MingXiaoTask14bCalls, 1);
	S = Step(0x158);
	Reset(N);
	FinishActivity(N);
	N.MingXiaoThrowableObjectMode = 1;
	N.RunTaskSlot444(&S);
	TestEqual(TEXT("0x10393d16 the tentacle grab 0x10398db0"), N.MingXiaoGrabCalls, 1);
	TestTrue(TEXT("0x10393d1f completes"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19MingXiaoTentacleTest,
	"Elysium.Substrate.NpcKernelRunTask19.Species.MingXiaoTentacle", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19MingXiaoTentacleTest::RunTest(const FString&)
{
	// `0x1039d750`: 0x14c with fewer than two flex controllers ends the frequent think and completes
	// (`0x1039da11`); 0x154 on the activity.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpcMingXiaoTentacle> F(TEXT("CNPC_VMingXiaoTentacle"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpcMingXiaoTentacle& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x14c);
	Reset(N);
	if (N.NumFlexControllers() <= 1)
	{
		N.RunTaskSlot444(&S);
		TestTrue(TEXT("0x1039da11 completes"), Completed(N));
	}
	S = Step(0x154);
	Reset(N);
	FinishActivity(N);
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x1039db96 completes"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19TzimisceTest,
	"Elysium.Substrate.NpcKernelRunTask19.Species.Tzimisce", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19TzimisceTest::RunTest(const FString&)
{
	// `0x103bb1e0`: 0xc1 on the activity; 0xc8 facing; 0xa7 never completes.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpcTzimisce> F(TEXT("CNPC_VTzimisce"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpcTzimisce& N = *F.Npc;
	FElysiumScheduleStep S = Step(0xc1);
	Reset(N);
	FinishActivity(N);
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x103bb2ec completes"), Completed(N));
	S = Step(0xc8);
	Reset(N);
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x103bb36a completes facing"), Completed(N));
	S = Step(0xa7);
	Reset(N);
	N.RunTaskSlot444(&S);
	TestFalse(TEXT("0xa7 only aims"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19TzimisceRunnerTest,
	"Elysium.Substrate.NpcKernelRunTask19.Species.TzimisceRunner", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19TzimisceRunnerTest::RunTest(const FString&)
{
	// `0x103c3870`: 0x122..0x124 past the wait zero m_flDesiredMoveYaw and complete.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpcTzimisceRunner> F(TEXT("CNPC_VTzimisceRunner"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpcTzimisceRunner& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x123);
	Reset(N);
	N.ScheduleHost.DesiredMoveYaw = 12.f;
	N.BaseScheduleHost.WaitFinished = F.Now() - 1.0;
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x103c3915 completes"), Completed(N));
	TestEqual(TEXT("0x103c390b m_flDesiredMoveYaw = 0"), N.ScheduleHost.DesiredMoveYaw, 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19WerewolfTest,
	"Elysium.Substrate.NpcKernelRunTask19.Species.Werewolf", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19WerewolfTest::RunTest(const FString&)
{
	// `0x103cdfb0`: 0x14a completes; 0x14b finished with no move hint -> TaskFail(4); 0x15f
	// finished writes m_lifeState 2 and completes.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpcWerewolf> F(TEXT("CNPC_VWerewolf"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpcWerewolf& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x14a);
	Reset(N);
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x14a completes"), Completed(N));
	S = Step(0x14b);
	Reset(N);
	FinishActivity(N);
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x103ce078 TaskFail(4)"), Failed(N, 4));
	S = Step(0x15f);
	Reset(N);
	FinishActivity(N);
	N.RunTaskSlot444(&S);
	TestEqual(TEXT("0x103ce491 m_lifeState = 2"), N.AnimEventLifeStateWord, 2);
	TestTrue(TEXT("0x103ce49b completes"), Completed(N));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19BachTest,
	"Elysium.Substrate.NpcKernelRunTask19.Species.Bach", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19BachTest::RunTest(const FString&)
{
	// `0x103652b0`: 0xb0 with no active weapon returns running (`0x103652eb`); 0x14a keeps running.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpcBach> F(TEXT("CNPC_VBach"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpcBach& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x14a);
	Reset(N);
	N.RunTaskSlot444(&S);
	TestFalse(TEXT("0x103652d0: 0x14a runs on"), Completed(N));
	if (N.ActiveWeaponEntity() == nullptr)
	{
		S = Step(0xb0);
		Reset(N);
		N.BaseScheduleHost.WaitFinished = F.Now() - 1.0;
		N.RunTaskSlot444(&S);
		TestFalse(TEXT("no weapon: running, no chain"), Completed(N));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelRunTask19HengeyokaiTest,
	"Elysium.Substrate.NpcKernelRunTask19.Species.Hengeyokai", GRunTask19Flags)
bool FElysiumNpcKernelRunTask19HengeyokaiTest::RunTest(const FString&)
{
	// `0x10380cb0`: 0x134 on the activity; 0x14b reaches `0x10383470`; 0xc8 completes facing.
	using namespace RunTask19TestShared;
	TFixture<FElysiumNpcHengeyokai> F(TEXT("CNPC_VHengeyokai"));
	if (!TestNotNull(TEXT("npc"), F.Npc)) return false;
	FElysiumNpcHengeyokai& N = *F.Npc;
	FElysiumScheduleStep S = Step(0x134);
	Reset(N);
	FinishActivity(N);
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0x10380dcc completes"), Completed(N));
	S = Step(0x14b);
	Reset(N);
	N.RunTaskSlot444(&S);
	TestEqual(TEXT("0x10380e90"), N.HengeyokaiTask14bCalls, 1);
	S = Step(0xc8);
	Reset(N);
	N.RunTaskSlot444(&S);
	TestTrue(TEXT("0xc8 completes facing"), Completed(N));
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
