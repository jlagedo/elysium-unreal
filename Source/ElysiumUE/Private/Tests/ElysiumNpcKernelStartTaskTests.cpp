// Story 0019/8 (29e under the strict verdict), family **StartTask19** -- the family's tests.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Test names carry `Elysium.Substrate.NpcKernelStartTask19.` and the retail address.
//
// Owns (StartTask19's `rule` rows): 0x102827f0 CAI_BaseNPC::StartTask, 0x102a1910
// CAI_BaseNPCTroika::StartTask, 0x1035f650 CNPC_VAnimal::StartTask, 0x103847f0
// CNPC_VHuman::StartTask, 0x10392d80 CNPC_VMingXiao::StartTask, 0x1039c4c0
// CNPC_VMingXiaoTentacle::StartTask, 0x103ba7c0 CNPC_VTzimisce::StartTask, 0x103c1820
// CNPC_VTzimisceHeadClaw::StartTask, 0x103c35d0 CNPC_VTzimisceRunner::StartTask, 0x103ccda0
// CNPC_VWerewolf::StartTask, 0x103645a0 CNPC_VBach::StartTask, 0x10374940 CNPC_VDog::StartTask,
// 0x10375f50 CNPC_VFrenzyShadow::StartTask, 0x103790d0 CNPC_VGargoyle::StartTask, 0x1037b8b0
// CNPC_VGhoulCroucher::StartTask, 0x103805d0 CNPC_VHengeyokai::StartTask, 0x1038c390
// CNPC_VManBat::StartTask, 0x103a5650 CNPC_VSabbatGunman::StartTask, 0x103ac740
// CNPC_VScurrying::StartTask, 0x103b36d0 CNPC_VTaxiDriver::StartTask, 0x103c5ac0
// CNPC_VVampireBoss::StartTask, 0x103dfd80 CNPC_VZombie::StartTask, 0x1035d1b0
// CNPC_VAndreiBlood::StartTask, 0x103611a0 CNPC_VAsianVampire::StartTask, 0x1036b750
// CNPC_VChangBros::StartTask, 0x103a78c0 CNPC_VSabbatLeader::StartTask, 0x103aec70
// CNPC_VSheriffMan::StartTask.
//
// Lane L01 (story 8 pass I): `CAI_BaseNPCTroika::StartTask 0x102a1910`, the arms in
// `[0x102a1943, 0x102a5046)`. Every case drives slot 442 directly on a bare `CAI_BaseNPCTroika`
// (no species override in the way) and asserts on the words the arm writes and on its exit:
// `TaskComplete` (`fTaskStatus = COMPLETE`), `TaskFail(reason)` (`+0x5c50` and `COND_TASK_FAILED`),
// or RUNNING (neither). The motor is the recording double, so a goal the arm submits is visible as
// the literal it built (`StartTask19LastGoal`).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumLocalIdSpace.h"
#include "Substrate/ElysiumScheduleId.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleCorpus.h"
#include "Tests/ElysiumNpcTestFixture.h"
#include "Tests/ElysiumNpcTestCensus.h"

static constexpr EAutomationTestFlags GStartTask19Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// One bare `CAI_BaseNPCTroika` guard with the recording motor, and the player the fixture spawns
	// as the enemy / follower boss / closest player a case needs.
	struct FStartTask19Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc* Guard = nullptr;
		FElysiumPlayer* Player = nullptr;

		FStartTask19Fixture()
			: World([]
				{
					FElysiumNpcWorldBuilder Builder(TEXT("starttask19_kernel"), 1910);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					// A model, so the requested recording motor is built (no model, no body, no motor):
					// retail's navigator always stands.
					Builder.AddNpcOfClass(TEXT("guard"), FVector::ZeroVector, TEXT("CAI_BaseNPCTroika"))
						.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
					return Builder;
				}(),
				[](FElysiumRecordingServices& Services) { Services.bProvideNpcMotor = true; })
		{
			Guard = World.Npc(TEXT("guard"));
			Player = World.Player();
			FElysiumNpcWorldFixture::Quiet({ Guard });
			FElysiumNpcWorldFixture::PrepareForKernelDrive(Guard);
		}

		// The class-LOCAL task (the registrar's number the switch compares) as the GLOBAL id a schedule
		// step carries (`ElysiumScheduleText.h`); the body translates it back (slot 450's body).
		int32 GlobalTask(int32 LocalTask) const
		{
			const FElysiumLocalIdSpace* Space = Guard->IdSpace(EElysiumIdCategory::Task);
			return Space != nullptr ? Space->LocalToGlobal(LocalTask) : LocalTask;
		}

		// Clear the three words an exit writes, then run slot 442 on one step.
		void Start(int32 TaskId, float Data = 0.f)
		{
			Guard->Schedule.TaskStatus = EElysiumTaskStatus::New;
			Guard->BaseScheduleHost.FailureReason = 0;
			Guard->Cognition.Conditions.Clear(EElysiumNpcCond::TaskFailed);
			FElysiumScheduleStep Step;
			Step.TaskId = GlobalTask(TaskId);
			Step.Data = Data;
			Guard->StartTaskSlot442(&Step);
		}

		bool Completed() const
		{
			return Guard->Schedule.TaskStatus == EElysiumTaskStatus::Complete
				&& !Guard->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed);
		}
		bool FailedWith(int32 Reason) const
		{
			return Guard->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed)
				&& Guard->BaseScheduleHost.FailureReason == Reason;
		}
		bool Running() const
		{
			return Guard->Schedule.TaskStatus == EElysiumTaskStatus::New
				&& !Guard->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed);
		}
		void SetEnemyToPlayer()
		{
			Guard->BaseMemory.Enemy = Player != nullptr ? Player->Handle : FElysiumEntityHandle::Invalid();
		}
		double Now() const { return World.World.NowSeconds(); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19ArmTableTest,
	"Elysium.Substrate.NpcKernelStartTask19.ArmTable_0x102a1910", GStartTask19Flags)
bool FElysiumNpcKernelStartTask19ArmTableTest::RunTest(const FString&)
{
	// `0x102a1910`: the 74 ids of lane L01's 68 arms, each the registrar's (`FUN_10316ff0`) global id
	// for the name beside it -- the ids the switch compares `Step->TaskId` against.
	const TConstArrayView<FElysiumNpc::FStartTask19ArmRow> Rows = FElysiumNpc::StartTask19ArmRows();
	TestEqual(TEXT("74 task ids"), Rows.Num(), 74);
	TSet<int32> Ids;
	for (const FElysiumNpc::FStartTask19ArmRow& Row : Rows)
	{
		Ids.Add(Row.TaskId);
		TestTrue(FString::Printf(TEXT("%s is inside the dispatch span"), Row.TaskName),
			Row.TaskId >= 5 && Row.TaskId - 5 <= 0x144);
	}
	TestEqual(TEXT("every id once"), Ids.Num(), 74);
	FElysiumScheduleCorpus& Corpus = FElysiumScheduleCorpus::Get();
	if (Corpus.EnsureLoaded())
	{
		// The namespace answers the GLOBAL id; the switch compares retail's class-local `iTask`. The
		// root and Troika task spaces are seeded contiguously from the global base (`0x102ea0e0`), so
		// the local id is the global one less `ElysiumScheduleId::GlobalBase`.
		for (const FElysiumNpc::FStartTask19ArmRow& Row : Rows)
		{
			TestEqual(FString::Printf(TEXT("%s is registered at 0x%x"), Row.TaskName, Row.TaskId),
				Corpus.Namespace(EElysiumIdCategory::Task).Find(Row.TaskName) - ElysiumScheduleId::GlobalBase,
				Row.TaskId);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19SuggestStateTest,
	"Elysium.Substrate.NpcKernelStartTask19.SuggestState_0x102a1b2b", GStartTask19Flags)
bool FElysiumNpcKernelStartTask19SuggestStateTest::RunTest(const FString&)
{
	FStartTask19Fixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	// `0x102a1b4e`: the suggestion is written, then the task completes.
	N.FrenziedWord = 0;
	N.bNoAlertState = false;
	F.Start(0x06, 2.f);
	TestEqual(TEXT("suggested state written"), N.IdealStateRetail(), 2);
	TestTrue(TEXT("completes"), F.Completed());
	// `0x102a1b54`: frenzied -> idle (1) and alert (3) become 0xb (`0x102a1ba6` / `0x102a1b76`).
	N.FrenziedWord = 1;
	F.Start(0x06, 1.f);
	TestEqual(TEXT("frenzied idle -> 0xb"), N.IdealStateRetail(), 0xb);
	TestTrue(TEXT("completes"), F.Completed());
	F.Start(0x06, 3.f);
	TestEqual(TEXT("frenzied alert -> 0xb"), N.IdealStateRetail(), 0xb);
	F.Start(0x06, 2.f);
	TestEqual(TEXT("frenzied combat kept (0x102a1b62)"), N.IdealStateRetail(), 2);
	TestTrue(TEXT("completes"), F.Completed());
	// `0x102a1bc8`: `m_bNoAlertState` rewrites alert to idle (`0x102a1bed`).
	N.FrenziedWord = 0;
	N.bNoAlertState = true;
	F.Start(0x06, 3.f);
	TestEqual(TEXT("no-alert alert -> idle"), N.IdealStateRetail(), 1);
	TestTrue(TEXT("completes"), F.Completed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19ActivityArmsTest,
	"Elysium.Substrate.NpcKernelStartTask19.ActivityArms_0x102a1c0f", GStartTask19Flags)
bool FElysiumNpcKernelStartTask19ActivityArmsTest::RunTest(const FString&)
{
	FStartTask19Fixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	const double Now = F.Now();

	// TASK_SET_ACTIVITY `0x102a1c0f`: the one-second watchdog `+0x5db4`, and no exit.
	F.Start(0x4b, 0.f);
	TestEqual(TEXT("0x102a1c41 m_flWaitFinished = curtime + 1.0"), N.BaseScheduleHost.WaitFinished, Now + 1.0);
	TestTrue(TEXT("0x4b stays running"), F.Running());

	// TASK_TEST2 `0x102a1a60`: every arm of the inner switch completes.
	for (int32 Switch = 0; Switch <= 5; ++Switch)
	{
		N.StartTask19DebugTestSwitch2 = Switch;
		F.Start(0xa8);
		TestTrue(FString::Printf(TEXT("0xa8 switch %d completes"), Switch), F.Completed());
	}

	// TASK_TEST1 `0x102a1943`: weapon hide/unhide is gated on a weapon; the arm ends in the turn tail.
	const int32 FacesBefore = N.StartTask19MotorFaces;
	F.Start(0xa7);
	TestEqual(TEXT("0x102a1a53 debug box"), N.StartTask19DebugBoxes, 1);
	TestEqual(TEXT("0x102a44e9 the turn tail faces the jittered point"), N.StartTask19MotorFaces, FacesBefore + 1);
	TestTrue(TEXT("0xa7 runs"), F.Running());

	// TASK_KNOCKOUT `0x102a3339` / TASK_UNKNOCKOUT `0x102a3364`.
	N.NpcFlags.AssignAiFlagsWord(0);
	F.Start(0xdd);
	TestTrue(TEXT("0x102a3344 0x440a0000 OR'd"), N.NpcFlags.RawWord1() == 0x440a0000u);
	TestTrue(TEXT("0xdd runs"), F.Running());
	F.Start(0xde);
	TestTrue(TEXT("0xde clears nothing"), N.NpcFlags.RawWord1() == 0x440a0000u);
	TestTrue(TEXT("0xde runs"), F.Running());

	// TASK_RANGE_ATTACK2 `0x102a4576`, TASK_SPECIAL_ATTACK1/2, TASK_DO_JUMP_ACTIVITY: run.
	N.LastAttackTime = -1.0;
	F.Start(0x35);
	TestEqual(TEXT("0x102a4580 m_flLastAttackTime = curtime"), N.LastAttackTime, Now);
	TestTrue(TEXT("0x35 runs"), F.Running());
	F.Start(0x3e);
	TestTrue(TEXT("0x3e runs"), F.Running());
	F.Start(0x3f);
	TestTrue(TEXT("0x3f runs"), F.Running());
	F.Start(0xe0);
	TestTrue(TEXT("0xe0 runs"), F.Running());

	// TASK_DO_LOOP/BLEND(_LOOP)_ACTIVITY `0x102a4ee3` / `0x102a4f38` / `0x102a4fb1`: with no sequence
	// resolved (`0x10272130` answers sequence 0 on a body with no sequence table) they complete.
	int32 Sequence = 0, Translated = 0, WeaponAct = 0;
	N.ResolveActivityToSequence(5, Sequence, Translated, WeaponAct);
	for (const int32 Task : { 0xe1, 0xe2, 0xe3 })
	{
		F.Start(Task, 5.f);
		TestTrue(FString::Printf(TEXT("0x%x: sequence > 0 runs, else completes (0x102a4f0c JG)"), Task),
			Sequence > 0 ? F.Running() : F.Completed());
	}

	// TASK_PAUSE_MOVING `0x102a1f06`: `0x102bf770` then complete.
	F.Start(0xa6);
	TestTrue(TEXT("0xa6 completes"), F.Completed());

	// TASK_RUN_DIALOG `0x102a496b`: no dialogue activity (`0x102c1400` = -1) completes.
	F.Start(0xb9);
	TestTrue(TEXT("0xb9 completes on -1"), F.Completed());

	// TASK_FACE_HINTNODE `0x102a382a`: stop-turn, yaw, SetTurnActivity, no exit.
	const int32 StopsBefore = N.StartTask19MotorStopTurns;
	F.Start(0x2f);
	TestEqual(TEXT("0x102a3830 stop-turn"), N.StartTask19MotorStopTurns, StopsBefore + 1);
	TestTrue(TEXT("0x2f runs"), F.Running());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19ToleranceArmsTest,
	"Elysium.Substrate.NpcKernelStartTask19.ToleranceArms_0x102a4289", GStartTask19Flags)
bool FElysiumNpcKernelStartTask19ToleranceArmsTest::RunTest(const FString&)
{
	FStartTask19Fixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	const float Resolved = N.ResolveTaskDistance(40.f);
	const float HalfHull = N.StartTask19HullWidthUnits(N.HullKind) * 0.5f;

	// TASK_SET_TOLERANCE_DISTANCE `0x102a4289`: hull*0.5 + resolved, both navigator words.
	F.Start(0x4e, 40.f);
	TestEqual(TEXT("0x102a42c0 m_flGoalTolerance"), N.ScheduleHost.GoalToleranceCm, (HalfHull + Resolved) * ElysiumMove::U, 0.01f);
	TestEqual(TEXT("0x102a42cd goal tolerance"), N.Navigator.GoalToleranceCm / ElysiumMove::U, HalfHull + Resolved, 0.001f);
	TestEqual(TEXT("0x102a42df arrival distance"), N.NavPathScalar20, HalfHull + Resolved, 0.001f);
	TestTrue(TEXT("0x4e completes"), F.Completed());

	// TASK_SET_TOLERANCE_DISTANCE_ABS `0x102a42fa`: resolved alone.
	F.Start(0x4f, 40.f);
	TestEqual(TEXT("0x102a4316"), N.Navigator.GoalToleranceCm / ElysiumMove::U, Resolved, 0.001f);
	TestTrue(TEXT("0x4f completes"), F.Completed());

	// TASK_SET_MELEE_TOLERANCE_DISTANCE `0x102a434a` with no enemy: hull 0, no weapon -> resolved.
	N.BaseMemory.Enemy = FElysiumEntityHandle::Invalid();
	const float HalfHull0 = N.StartTask19HullWidthUnits(0) * 0.5f;
	F.Start(0x9f, 40.f);
	// With a weapon the addend is `weapon+0x8c0 * data` (its class word, 0018 story 8); without, the
	// resolved distance (`0x102a43fe`).
	const float MeleeAddend = N.ActiveWeaponEntity() != nullptr
		? N.StartTask19WeaponRangeWord(0x8c0) * 40.f : Resolved;
	TestEqual(TEXT("0x102a4376 / 0x102a43e2 hull(0)*0.5 + addend"), N.Navigator.GoalToleranceCm / ElysiumMove::U, HalfHull0 + MeleeAddend, 0.001f);
	TestTrue(TEXT("0x9f completes"), F.Completed());

	// TASK_SET_TOLERANCE_DIST_DLG `0x102a4c47`: 160 + resolved; HALF to the goal tolerance, full to
	// the arrival distance.
	F.Start(0xdb, 40.f);
	TestEqual(TEXT("0x102a4c66 m_flGoalTolerance"), N.ScheduleHost.GoalToleranceCm, (160.f + Resolved) * ElysiumMove::U, 0.01f);
	TestEqual(TEXT("0x102a42cd half"), N.Navigator.GoalToleranceCm / ElysiumMove::U, (160.f + Resolved) * 0.5f, 0.001f);
	TestEqual(TEXT("0x102a42df full"), N.NavPathScalar20, 160.f + Resolved, 0.001f);
	TestTrue(TEXT("0xdb completes"), F.Completed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19TimerArmsTest,
	"Elysium.Substrate.NpcKernelStartTask19.TimerAndFlagArms_0x102a4a5b", GStartTask19Flags)
bool FElysiumNpcKernelStartTask19TimerArmsTest::RunTest(const FString&)
{
	FStartTask19Fixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	const double Now = F.Now();

	// TASK_SET_INSIDE/OUTSIDE_INTERRUPT_DIST: `__ftol` then an INTEGER square.
	const int32 Truncated = static_cast<int32>(N.ResolveTaskDistance(10.7f));
	F.Start(0xcf, 10.7f);
	TestEqual(TEXT("0x102a4a7f +0x6324"), N.ScheduleHost.InsideInterruptDistanceSqr, static_cast<float>(Truncated * Truncated));
	TestTrue(TEXT("0xcf completes"), F.Completed());
	F.Start(0xd0, 10.7f);
	TestEqual(TEXT("0x102a4abb +0x6328"), N.ScheduleHost.OutsideInterruptDistanceSqr, static_cast<float>(Truncated * Truncated));
	TestTrue(TEXT("0xd0 completes"), F.Completed());

	// TASK_SET/ADD_RANDOM/CLEAR_INTERRUPT_TIME `+0x632c`.
	F.Start(0xd3, 2.f);
	TestEqual(TEXT("0x102a4ae3 curtime + data"), N.ScheduleHost.InterruptTime, Now + 2.0);
	TestTrue(TEXT("0xd3 completes"), F.Completed());
	F.Start(0xd4, 3.f);
	TestTrue(TEXT("0x102a4b16 += RandomFloat(0, 3)"),
		N.ScheduleHost.InterruptTime >= Now + 2.0 && N.ScheduleHost.InterruptTime <= Now + 5.0);
	TestTrue(TEXT("0xd4 completes"), F.Completed());
	F.Start(0xd5);
	TestEqual(TEXT("0x102a4b32 cleared"), N.ScheduleHost.InterruptTime, 0.0);
	TestTrue(TEXT("0xd5 completes"), F.Completed());

	// TASK_RUN_DISPOSITION / TASK_SPECIAL_IDLE_ACTIVITY `0x102a49bc`: the wait, no exit.
	F.Start(0xbc, 4.f);
	TestEqual(TEXT("0x102a49c7 m_flWaitFinished"), N.BaseScheduleHost.WaitFinished, Now + 4.0);
	TestTrue(TEXT("0xbc runs"), F.Running());
	F.Start(0xbd, 4.f);
	TestTrue(TEXT("0x102a49f4 random wait"), N.BaseScheduleHost.WaitFinished >= Now
		&& N.BaseScheduleHost.WaitFinished <= Now + 4.0);
	TestTrue(TEXT("0xbd runs"), F.Running());

	// TASK_SET_PRESERVE_PATH `0x102a3cfa`.
	F.Start(0xc4, 1.f);
	TestTrue(TEXT("0x102a3d2a set"), N.NpcFlags.Has(EElysiumNpcFlag::PRESERVE_PATH));
	TestTrue(TEXT("0xc4 completes"), F.Completed());
	F.Start(0xc4, 0.f);
	TestFalse(TEXT("0x102a3d0c clear"), N.NpcFlags.Has(EElysiumNpcFlag::PRESERVE_PATH));

	// TASK_ADD_EVENT_EXPRESSION `0x102a4a07`: in range adds, out of range warns; both complete.
	F.Start(0xbe, 1.f);
	TestEqual(TEXT("0x102a4a1b event 1"), N.StartTask19LastExpressionEvent, 1);
	TestTrue(TEXT("0xbe completes"), F.Completed());
	const int32 EventsBefore = N.StartTask19ExpressionEvents;
	F.Start(0xbe, 2.f);
	TestEqual(TEXT("0x102a4a16 event 2 refused"), N.StartTask19ExpressionEvents, EventsBefore);
	TestTrue(TEXT("0xbe completes anyway (0x102a4a49)"), F.Completed());

	// TASK_RANGE_ATTACK1 `0x102a4505`: no weapon -> 1, no exit.
	N.BurstFireCount = 7;
	F.Start(0x34);
	TestEqual(TEXT("0x102a455f m_iBurstFireCount = 1"), N.BurstFireCount, 1);
	TestTrue(TEXT("0x34 runs"), F.Running());

	// TASK_WALK_RUN_PATH(_COMBAT_SOUND) `0x102a4b4e` / `0x102a4bd8`: no sequence for ACT_RUN, so
	// ACT_WALK; `m_afMemory &= ~2`; complete.
	N.BaseScheduleHost.MemoryBits = 0x3u;
	F.Start(0xd6, 10.f);
	TestEqual(TEXT("0x102a4bae movement activity ACT_WALK"), N.Navigator.MovementActivity, 9);
	TestTrue(TEXT("0x102a4bbb m_afMemory &= ~2"), N.BaseScheduleHost.MemoryBits == 0x1u);
	TestTrue(TEXT("0xd6 completes"), F.Completed());
	N.BaseScheduleHost.MemoryBits = 0x2u;
	F.Start(0xd7);
	TestEqual(TEXT("0x102a4c1d ACT_WALK"), N.Navigator.MovementActivity, 9);
	TestTrue(TEXT("0x102a4c2a m_afMemory &= ~2"), N.BaseScheduleHost.MemoryBits == 0x0u);
	TestTrue(TEXT("0xd7 completes"), F.Completed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19EnemyGoalArmsTest,
	"Elysium.Substrate.NpcKernelStartTask19.EnemyGoalArms_0x102a33f9", GStartTask19Flags)
bool FElysiumNpcKernelStartTask19EnemyGoalArmsTest::RunTest(const FString&)
{
	FStartTask19Fixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;

	// No enemy: every enemy arm fails with 6 at its own line.
	N.BaseMemory.Enemy = FElysiumEntityHandle::Invalid();
	F.Start(0x0f);
	TestTrue(TEXT("0x102a344b no enemy -> 6"), F.FailedWith(6));
	F.Start(0xc7);
	TestTrue(TEXT("0x102a4822 no enemy -> 6"), F.FailedWith(6));
	F.Start(0x7f, 100.f);
	TestTrue(TEXT("0x102a3d5e no enemy -> 6"), F.FailedWith(6));
	F.Start(0x81, 100.f);
	TestTrue(TEXT("0x102a3f96 no enemy -> 6"), F.FailedWith(6));
	F.Start(0xa3);
	TestTrue(TEXT("0x102a24c3 no enemy -> 6"), F.FailedWith(6));
	F.Start(0xc5);
	TestTrue(TEXT("0x102a473a no enemy -> 6"), F.FailedWith(6));

	// With the player as the enemy.
	F.SetEnemyToPlayer();
	const int32 CallsBefore = N.StartTask19SetGoalCalls;
	F.Start(0x0f);
	TestEqual(TEXT("0x102a352c SetGoal called"), N.StartTask19SetGoalCalls, CallsBefore + 1);
	TestEqual(TEXT("goal type 2 (the enemy)"), N.StartTask19LastGoal.Type, 2);
	TestEqual(TEXT("tolerance -1"), N.StartTask19LastGoal.Tolerance, -1.f);
	TestTrue(TEXT("0x0f completes or fails 0xc on the SetGoal answer"), F.Completed() || F.FailedWith(0xc));

	F.Start(0xc7);
	TestEqual(TEXT("0x102a48f2 goal flags 2"), N.StartTask19LastGoal.GoalFlags, 2);
	TestEqual(TEXT("0xc7 goal type 4"), N.StartTask19LastGoal.Type, 4);
	TestTrue(TEXT("0xc7 SetGoal flags 0"), N.StartTask19LastGoalFlags == 0u);
	TestTrue(TEXT("0xc7 completes or fails 0xc"), F.Completed() || F.FailedWith(0xc));

	// No enemy-memory record for the player: `0x102e0290` answers false.
	N.EnemyMemory = FElysiumNpcEnemyMemory();
	F.Start(0x80, 100.f);
	TestTrue(TEXT("0x102a3f59 no LKP -> 6"), F.FailedWith(6));
	F.Start(0x81, 100.f);
	TestTrue(TEXT("0x102a425e no LKP -> 6"), F.FailedWith(6));

	// Flank: no node graph, so the LOS sweep refuses -> 0xb.
	F.Start(0xa3);
	TestTrue(TEXT("0x102a2857 no shoot position -> 0xb"), F.FailedWith(0xb));

	// TASK_SET_ENEMY_ELUDED `0x102a46fa` with an enemy: complete.
	F.Start(0xc5);
	TestTrue(TEXT("0x102a4728 completes"), F.Completed());

	// TASK_SET_TARGET_ELUDED `0x102a4763` with no `m_hTargetEnt`: fail 1.
	N.SetTarget(FElysiumEntityHandle::Invalid());
	F.Start(0xc6);
	TestTrue(TEXT("0x102a47e7 no target -> 1"), F.FailedWith(1));

	// TASK_FACE_ENEMY `0x102a4417`: either in the aim cone (complete) or the turn tail (running).
	const int32 FacesBefore = N.StartTask19MotorFaces;
	F.Start(0x2e);
	TestTrue(TEXT("0x2e completes in the cone or turns"), F.Completed()
		|| (F.Running() && N.StartTask19MotorFaces == FacesBefore + 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19CoverArmsTest,
	"Elysium.Substrate.NpcKernelStartTask19.CoverAndHintArms_0x102a20dc", GStartTask19Flags)
bool FElysiumNpcKernelStartTask19CoverArmsTest::RunTest(const FString&)
{
	FStartTask19Fixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	// The recording motor answers no node cover and the embodiment reports a clear line of sight, so
	// neither the lateral search nor `FindCoverPos` finds anything.
	F.Start(0xa0, 1.f);
	TestTrue(TEXT("0x102a2206 no cover -> 8"), F.FailedWith(8));
	TestEqual(TEXT("0x102a2137 max radius 150"), N.StartTask19LastCoverMaxUnits, 150.f);
	F.Start(0xa1, 1.f);
	TestTrue(TEXT("0x102a2486 no forward cover -> 8"), F.FailedWith(8));
	F.Start(0xa2, 1.f);
	TestTrue(TEXT("0x102a2303 no save-position cover -> 8"), F.FailedWith(8));
	TestEqual(TEXT("0x102a2253 min radius 32"), N.StartTask19LastCoverMinUnits, 32.f);

	// TASK_GET_PATH_TO_FLEE_NODE / COWER_NODE `0x102a2882`: no hint, both cover searches refused.
	N.NpcFlags.Clear(EElysiumNpcFlag::COWER_PATH);
	F.Start(0x83, 10.f);
	TestTrue(TEXT("0x102a2bad flee -> 0x18"), F.FailedWith(0x18));
	TestFalse(TEXT("0x83 leaves COWER_PATH"), N.NpcFlags.Has(EElysiumNpcFlag::COWER_PATH));
	const float Resolved = N.ResolveTaskDistance(10.f);
	TestEqual(TEXT("0x102a2afa the retry's inner radius is the resolved distance"),
		N.StartTask19LastCoverMinUnits, Resolved);
	TestEqual(TEXT("0x102a28ae outer radius d + 8192"), N.StartTask19LastCoverMaxUnits, Resolved + 8192.f);
	// 0x84 raises `COWER_PATH` (`0x102a2a7c OR AH,2`) and then fails 0x18; the failure's own mask
	// (`TaskFail 0x1029adb0`: `m_bfAINPCFlags &= 0xa3f40178`) clears bit 0x200 again, so the flag is
	// not observable after a refused search.
	F.Start(0x84, 10.f);
	TestFalse(TEXT("0x102a2a7c cower's COWER_PATH is cleared by the fail mask 0xa3f40178"),
		N.NpcFlags.Has(EElysiumNpcFlag::COWER_PATH));
	TestTrue(TEXT("0x84 -> 0x18"), F.FailedWith(0x18));
	F.Start(0x85, 10.f);
	TestFalse(TEXT("0x102a2bfc save-pos cower's COWER_PATH is cleared by the fail mask"),
		N.NpcFlags.Has(EElysiumNpcFlag::COWER_PATH));
	TestTrue(TEXT("0x102a2d70 -> 0x18"), F.FailedWith(0x18));

	// TASK_GET_PATH_TO_HINTNODE `0x102a371d` with no hint: fail 4.
	N.BaseScheduleHost.HintNode = INDEX_NONE;
	F.Start(0x16);
	TestTrue(TEXT("0x102a372d no hint -> 4"), F.FailedWith(4));

	// TASK_FIND_BACKAWAY_FROM_SAVEPOSITION `0x102a1c6a`: no navigator node -> 7.
	F.Start(0x5a, 64.f);
	TestTrue(TEXT("0x102a1ca3 no backaway node -> 7"), F.FailedWith(7));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19PatrolArmsTest,
	"Elysium.Substrate.NpcKernelStartTask19.PatrolFollowerHuntArms_0x102a39f4", GStartTask19Flags)
bool FElysiumNpcKernelStartTask19PatrolArmsTest::RunTest(const FString&)
{
	FStartTask19Fixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;

	// No patrol path: 0x7c fails 0x1d at its own line; 0x7a/0x7b through `0x102aa640`'s missing-path
	// arm; 0x7d/0x7e through `0x102aa9e0`'s assert arm.
	F.Start(0x7c);
	TestTrue(TEXT("0x102a3a02 no patrol path -> 0x1d"), F.FailedWith(0x1d));
	F.Start(0x7a);
	TestTrue(TEXT("0x7a -> 0x1d"), F.FailedWith(0x1d));
	F.Start(0x7b);
	TestTrue(TEXT("0x7b -> 0x1d"), F.FailedWith(0x1d));
	F.Start(0x7d);
	TestTrue(TEXT("0x102aa9e0 null path -> 0x1d"), F.FailedWith(0x1d));
	F.Start(0x7e);
	TestTrue(TEXT("0x7e -> 0x1d"), F.FailedWith(0x1d));
	// `0x102a3bac LEA EDX,[ESI+0x6594]`: the arm hands `m_sppPatrolPathHunt` (the pooled record,
	// family Script19's `PatrolPathHuntCell`), not the port's own `HuntPatrolPoints` route.
	FElysiumNpc::FPatrolPathRecord HuntRecord;
	N.PatrolPathHuntCell.Path = &HuntRecord;
	F.Start(0x7e);
	TestTrue(TEXT("0x102aa9e0 a live hunt path completes"), F.Completed());
	N.PatrolPathHuntCell.Path = nullptr;
	// `0x102a3b91` over a live 3-node type-0 `m_sppPatrolPath`: `0x102aa9e0` runs `NextPoint`
	// (`0x102aa9f3` -> `0x10307b80`), whose step `DAT_1049df2c[0]` = +1 moves the index 0 -> 1 in
	// range (no release, `0x102aa9fa`), then completes (`0x102aaa10`).
	FElysiumNpc::ResetPatrolPathPool();
	{
		const int32 Ids[] = { 30, 31, 32, -1 };
		N.BuildPatrolPath(&N.PatrolPathCell, 0, 0, 0, Ids, FElysiumNpc::EPatrolPathBuild::Replace);
	}
	if (TestNotNull(TEXT("the patrol cell holds a path"), N.PatrolPathCell.Path))
	{
		TestEqual(TEXT("0x10307c20 type 0 starts at index 0"), N.PatrolPathCell.Path->Current, 0);
		F.Start(0x7d);
		TestTrue(TEXT("0x102aaa10 0x7d completes"), F.Completed());
		TestNotNull(TEXT("0x102aa9fa an in-range step keeps the path"), N.PatrolPathCell.Path);
		if (N.PatrolPathCell.Path != nullptr)
		{
			TestEqual(TEXT("0x10307b96 0x7d advances the index 0 -> 1"), N.PatrolPathCell.Path->Current, 1);
		}
	}
	N.ReleasePatrolPath(&N.PatrolPathCell);
	FElysiumNpc::ResetPatrolPathPool();

	// Follower backaway without a boss: 0x29 at each arm's own line.
	N.FollowerBoss = FElysiumEntityHandle::Invalid();
	F.Start(0x86);
	TestTrue(TEXT("0x102a2f69 no boss -> 0x29"), F.FailedWith(0x29));
	F.Start(0x87);
	TestTrue(TEXT("0x102a306b no boss -> 0x29"), F.FailedWith(0x29));
	F.Start(0x88);
	TestTrue(TEXT("0x102a316d no boss -> 0x29"), F.FailedWith(0x29));

	// With the player as the boss: 0x86's move probe admits, so the shared goal is submitted (type
	// 4, ACT_RUN, tolerance -2, SetGoal flags 0) and the task runs; 0x87 / 0x88 find no node -> 7.
	N.FollowerBoss = F.Player->Handle;
	N.FollowerDistanceBackAway = 50.f;
	const int32 CallsBefore = N.StartTask19SetGoalCalls;
	F.Start(0x86);
	TestEqual(TEXT("0x102a4186 goal submitted"), N.StartTask19SetGoalCalls, CallsBefore + 1);
	TestEqual(TEXT("shared goal type 4"), N.StartTask19LastGoal.Type, 4);
	TestEqual(TEXT("shared goal ACT_RUN"), N.StartTask19LastGoal.Activity, 0x13);
	TestEqual(TEXT("shared goal tolerance -2"), N.StartTask19LastGoal.Tolerance, -2.f);
	TestTrue(TEXT("0x102a2f34 flags 0"), N.StartTask19LastGoalFlags == 0u);
	TestTrue(TEXT("0x86 runs"), F.Running());
	F.Start(0x87);
	TestTrue(TEXT("0x102a303e no node -> 7"), F.FailedWith(7));
	F.Start(0x88);
	TestTrue(TEXT("0x102a3140 no node -> 7"), F.FailedWith(7));

	// Hunt patrol list / target: no pathfinder -> 0x20.
	F.Start(0xae);
	TestTrue(TEXT("0x102a3c34 -> 0x20"), F.FailedWith(0x20));
	F.Start(0xaf);
	TestTrue(TEXT("0x102a3cd1 -> 0x20"), F.FailedWith(0x20));

	// Best unknown: none -> 0x21.
	F.Start(0x79);
	TestTrue(TEXT("0x102a35db no unknown -> 0x21"), F.FailedWith(0x21));

	// Interesting place: none -> 0x22 at both arms.
	F.Start(0xa4);
	TestTrue(TEXT("0x102a1f98 no place -> 0x22"), F.FailedWith(0x22));
	F.Start(0xa5);
	TestTrue(TEXT("0x102a20b1 place lost -> 0x22"), F.FailedWith(0x22));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19AttackArmsTest,
	"Elysium.Substrate.NpcKernelStartTask19.AttackArms_0x102a45c6", GStartTask19Flags)
bool FElysiumNpcKernelStartTask19AttackArmsTest::RunTest(const FString&)
{
	FStartTask19Fixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	// The kick needs the `0x40000000` capability bit AND the weapon-class cast, neither of which the
	// port's weapons answer, so it fails `0x1f` armed or not (`0x102a46c6`).
	F.Start(0x9a);
	TestTrue(TEXT("0x102a46c6 kick -> 0x1f"), F.FailedWith(0x1f));
	const bool bMeleeArmed = N.ActiveWeaponEntity() != nullptr
		&& (N.StartTask19WeaponCapabilityWord() & 0x18000u) != 0;
	const int32 SwingsBefore = N.StartTask19WeaponSwings;
	F.Start(0x36);
	if (bMeleeArmed)
	{
		TestEqual(TEXT("0x102a4608 weapon +0x518"), N.StartTask19WeaponSwings, SwingsBefore + 1);
		TestTrue(TEXT("0x36 runs"), F.Running());
	}
	else
	{
		TestTrue(TEXT("0x102a463e melee without a melee weapon -> 0x1f"), F.FailedWith(0x1f));
		F.Start(0x37);
		TestTrue(TEXT("0x37 -> 0x1f"), F.FailedWith(0x1f));
	}
	// TASK_WAIT_ATTACK_TIME1/2 `0x102a337d`: no weapon is the break tail; a weapon's deadline answers
	// curtime (the seam), which is `<= curtime` and completes too (`0x102a4e51`).
	F.Start(0xb0);
	TestTrue(TEXT("0x102a3388 / 0x102a33cf completes"), F.Completed());
	F.Start(0xb1);
	TestTrue(TEXT("0xb1 completes"), F.Completed());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19MovementArmsTest,
	"Elysium.Substrate.NpcKernelStartTask19.MovementAndDialogArms_0x102a1dcc", GStartTask19Flags)
bool FElysiumNpcKernelStartTask19MovementArmsTest::RunTest(const FString&)
{
	FStartTask19Fixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;

	// TASK_WAIT_FOR_MOVEMENT `0x102a1dcc` with no active goal: `m_bShouldMove = 0` and complete
	// (`0x102a1e1f..1e35`). Outside the teleport window (`curtime > m_flTeleportMoveTimer`) the
	// rescue returns.
	N.TeleportMoveTimer = -1.f;
	F.Start(0x6e);
	TestFalse(TEXT("0x102a1e1f m_bShouldMove = 0"), N.BaseScheduleHost.bShouldMove);
	TestTrue(TEXT("0x6e completes with no route"), F.Completed());
	// Inside the window the rescue probes the goal and writes the origin (slot 216).
	N.TeleportMoveTimer = static_cast<float>(F.Now() + 5.0);
	const int32 OriginsBefore = N.StartTask19SetAbsOriginCalls;
	F.Start(0x6e);
	TestEqual(TEXT("0x102a1eda slot 216 inside the window"), N.StartTask19SetAbsOriginCalls, OriginsBefore + 1);

	// Dialogue arms with no closest player.
	N.Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
	F.Start(0xd9);
	TestTrue(TEXT("0x102a4dcc no player -> 0x17"), F.FailedWith(0x17));
	F.Start(0xda);
	TestTrue(TEXT("0x102a4e8d no player completes"), F.Completed());
	F.Start(0xd8);
	TestFalse(TEXT("0x102a4cc1 m_hMoveTargetEnt = -1"), N.ScheduleHost.MoveTarget.IsSet());
	TestTrue(TEXT("0xd8 runs"), F.Running());

	// With the player closest.
	N.Senses.Memory.ClosestPlayer = F.Player->Handle;
	F.Start(0xd8);
	TestTrue(TEXT("0x102a4cbd m_hMoveTargetEnt = the player"), N.ScheduleHost.MoveTarget == F.Player->Handle);
	TestEqual(TEXT("0xd8 goal type 4"), N.StartTask19LastGoal.Type, 4);
	TestTrue(TEXT("0xd8 runs"), F.Running());
	// `m_flSpecialDistanceAccum` 0: `dist >= accum` tries ACT_RUN, which has no sequence -> 0x15.
	N.SpecialDistanceAccum = 0.f;
	F.Start(0xd9);
	TestTrue(TEXT("0x102a4e67 no run sequence -> 0x15"), F.FailedWith(0x15));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelStartTask19DieIfUnseenTest,
	"Elysium.Substrate.NpcKernelStartTask19.DieIfPlayerCantSee_0x102a3198", GStartTask19Flags)
bool FElysiumNpcKernelStartTask19DieIfUnseenTest::RunTest(const FString&)
{
	FStartTask19Fixture F;
	if (!TestNotNull(TEXT("guard"), F.Guard) || !TestNotNull(TEXT("player"), F.Player))
	{
		return false;
	}
	FElysiumNpc& N = *F.Guard;
	// A player inside the resolved distance: complete FIRST (`0x102a31ca`), then stay.
	N.Senses.Memory.ClosestPlayer = F.Player->Handle;
	N.Senses.Memory.ClosestPlayerDistanceCm = 100.f * ElysiumMove::U;
	const int32 RemovalsBefore = N.UtilRemoveCalls;
	F.Start(0xdc, 500.f);
	TestTrue(TEXT("0x102a31ca completes before any test"), F.Completed());
	TestEqual(TEXT("0x102a31f0 inside the distance: no removal"), N.UtilRemoveCalls, RemovalsBefore);
	// No player at all: complete, then the silent removal (`0x102b53d0`, `0x101cd940`).
	N.Senses.Memory.ClosestPlayer = FElysiumEntityHandle::Invalid();
	F.Start(0xdc, 500.f);
	TestTrue(TEXT("completes"), F.Completed());
	TestEqual(TEXT("0x102a3324 UTIL_Remove"), N.UtilRemoveCalls, RemovalsBefore + 1);
	return true;
}

#endif  // WITH_DEV_AUTOMATION_TESTS
