#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

	#include "ElysiumEntityDefs.h"
	#include "ElysiumEntityWorld.h"
	#include "HAL/PlatformProcess.h"
	#include "Substrate/ElysiumNpc.h"
	#include "Substrate/ElysiumNpcConditions.h"
	#include "Substrate/ElysiumNpcFlags.h"
	#include "Substrate/ElysiumSchedule.h"
	#include "Tests/ElysiumNpcTestFixture.h"

// Story 29e, family Maintain19. Each case names the listing instruction that supplies the arm.

static constexpr EAutomationTestFlags GMaintain19Flags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	struct FMaintain19Fixture
	{
		FElysiumNpcWorldFixture World;
		FElysiumNpc*			Npc = nullptr;
		FElysiumNpc*			Other = nullptr;

		FMaintain19Fixture()
			: World([] {
					FElysiumNpcWorldBuilder Builder(TEXT("maintain19_kernel"), 2919);
					Builder.AddEntity(TEXT("worldspawn"), TEXT("world"));
					FElysiumEntityDef& Subject = Builder.AddNpc(TEXT("subject"));
					Subject.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
					FElysiumEntityDef& Other = Builder.AddNpc(TEXT("other"), FVector(200.f, 0.f, 0.f));
					Other.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
					FElysiumEntityDef& Open =
						Builder.AddEntity(TEXT("scripted_sequence"), TEXT("open_sequence"));
					Open.Keys.Add(TEXT("m_iszEntity"), TEXT("subject"));
					Open.Keys.Add(TEXT("m_iszPostIdle"), TEXT("idle"));
					Open.Keys.Add(TEXT("spawnflags"), TEXT("256"));
					FElysiumEntityDef& Locked =
						Builder.AddEntity(TEXT("scripted_sequence"), TEXT("locked_sequence"));
					Locked.Keys.Add(TEXT("m_iszEntity"), TEXT("subject"));
					Locked.Keys.Add(TEXT("m_iszPostIdle"), TEXT("idle"));
					Locked.Keys.Add(TEXT("spawnflags"), TEXT("288"));
					return Builder; }(), [](FElysiumRecordingServices& Services) {
					Services.bProvideNpcMotor = true;
					Services.bNpcActivitiesResolve = true; })
		{
			Npc = World.Npc(TEXT("subject"));
			Other = World.Npc(TEXT("other"));
			FElysiumNpcWorldFixture::Quiet({ Npc, Other });
		}
	};

	struct FMaintainRunner final : IElysiumScheduleRunner
	{
		FElysiumNpcConditions Conditions;
		EElysiumScheduleId	  Selected = EElysiumScheduleId::IdleStand;
		int32				  Ideal = 0;
		bool				  bStateMismatch = false;
		bool				  bDoor = false;
		bool				  bSpecialNav = false;
		bool				  bChooseNew = false;
		bool				  bAiStep = false;
		bool				  bContinuous = false;
		bool				  bExternalReturn = false;
		int32				  SelectCalls = 0;
		int32				  PrepareCalls = 0;
		int32				  CommitCalls = 0;
		int32				  MissingCalls = 0;
		int32				  MaintainCalls = 0;
		int32				  OnStartCalls = 0;
		int32				  StartOverlayCalls = 0;
		int32				  RunOverlayCalls = 0;
		int32				  ContinuousWrites = 0;
		int32				  SpecialMarks = 0;
		int32				  AiStepAdvances = 0;
		int32				  AiStepFreezes = 0;
		int32				  ScheduleChanges = 0;
		int32				  ScheduleDoneCalls = 0;
		double				  Time = 0.0;
		double				  WaitPvsDelaySeconds = 0.0;
		int32				  WaitPvsCalls = 0;

		virtual float RunSpecialIdleActivity(double) override { return 1.f; }
		virtual bool  IsBodyVisible() const override { return false; }
		virtual float PlayActivity(const FString&) override { return 1.f; }
		virtual float RandomSeconds(float Max) override { return Max; }
		virtual bool WaitPvs() override
		{
			++WaitPvsCalls;
			if (WaitPvsDelaySeconds > 0.0)
			{
				FPlatformProcess::Sleep(static_cast<float>(WaitPvsDelaySeconds));
			}
			return true;
		}
		virtual void  ClearConditions() override { Conditions.Reset(); }
		virtual bool  HasMaintenanceCondition(EElysiumNpcCond Cond) const override
		{
			return Conditions.Has(Cond);
		}
		virtual void TaskFail(int32) override { Conditions.Set(EElysiumNpcCond::TaskFailed); }
		virtual void ScheduleDone() override
		{
			++ScheduleDoneCalls;
			Conditions.Set(EElysiumNpcCond::ScheduleDone);
		}
		virtual void OnScheduleChange(EElysiumScheduleId) override { ++ScheduleChanges; }
		virtual double ScheduleTime() const override { return Time; }
		virtual bool IsSpecialNavigation() const override { return bSpecialNav; }
		virtual void MarkSpecialNavigationScheduleEnd() override { ++SpecialMarks; }
		virtual bool ConsumeChooseNewSchedule() override
		{
			const bool Result = bChooseNew;
			bChooseNew = false;
			return Result;
		}
		virtual bool ScheduleStateDiffersFromIdeal() const override { return bStateMismatch; }
		virtual void PrepareScheduleReselect() override { ++PrepareCalls; }
		virtual bool ConsumeBlockedDoorForSchedule(double) override { return bDoor; }
		virtual void CommitIdealStateForSchedule() override
		{
			++CommitCalls;
			bStateMismatch = false;
		}
		virtual EElysiumScheduleId SelectScheduleForMaintenance(double,
			int32& OutIdealScheduleRetail) override
		{
			++SelectCalls;
			OutIdealScheduleRetail = ElysiumScheduleNumber(Selected);
			return Selected;
		}
		virtual void SetIdealScheduleForMaintenance(int32 RetailId) override { Ideal = RetailId; }
		virtual void MissingSchedule() override { ++MissingCalls; }
		virtual void MaintainActivity() override { ++MaintainCalls; }
		virtual void MaintenanceOnStartSchedule(int32) override { ++OnStartCalls; }
		virtual void MaintenanceStartTaskOverlay() override { ++StartOverlayCalls; }
		virtual bool MaintenanceIsCurTaskContinuousMove() override { return bContinuous; }
		virtual void RememberContinuousMove() override { ++ContinuousWrites; }
		virtual void RunTaskOverlay() override { ++RunOverlayCalls; }
		virtual bool IsAiStepMode() const override { return bAiStep; }
		virtual void AdvanceAiStepDebugIndex() override { ++AiStepAdvances; }
		virtual void FreezeForAiStep() override { ++AiStepFreezes; }
		virtual bool TakeExternalExecutorReturn() override
		{
			const bool Result = bExternalReturn;
			bExternalReturn = false;
			return Result;
		}
	};

	struct FEmptyTaskScope
	{
		FElysiumSchedule*		 Program = nullptr;
		TArray<FElysiumTaskStep> Saved;

		explicit FEmptyTaskScope(EElysiumScheduleId Id)
		{
			Program = const_cast<FElysiumSchedule*>(ElysiumScheduleFor(Id));
			check(Program != nullptr);
			Saved = MoveTemp(Program->Tasks);
		}
		~FEmptyTaskScope()
		{
			Program->Tasks = MoveTemp(Saved);
		}
	};

	struct FWaitPvsTaskScope
	{
		FElysiumSchedule*		 Program = nullptr;
		TArray<FElysiumTaskStep> Saved;

		FWaitPvsTaskScope(EElysiumScheduleId Id, int32 Count)
		{
			Program = const_cast<FElysiumSchedule*>(ElysiumScheduleFor(Id));
			check(Program != nullptr);
			Saved = MoveTemp(Program->Tasks);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				FElysiumTaskStep Step;
				Step.Task = EElysiumTask::WaitPvs;
				Program->Tasks.Add(Step);
			}
		}
		~FWaitPvsTaskScope()
		{
			Program->Tasks = MoveTemp(Saved);
		}
	};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMaintain19SetScheduleTest,
	"Elysium.Substrate.NpcKernelMaintain19.SetSchedule", GMaintain19Flags)
bool FElysiumNpcKernelMaintain19SetScheduleTest::RunTest(const FString&)
{
	FMaintain19Fixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc))
		return false;
	FElysiumNpc& N = *F.Npc;
	N.SetRetailClassForTests(TEXT("CNPC_VWerewolf"));
	N.SetState(1);
	N.bNpcIsAlive = true;
	N.WerewolfScheduleStack.Reset();

	// 102ae750 translates first; Werewolf 0x43 -> 0x15c, whose missing blob takes story 25's
	// untranslated IDLE_STAND fallback. ForceScheduleChange and base SetSchedule each dispatch 435.
	N.SetSchedule(0x43, false);
	TestEqual(TEXT("102ae758 translated through slot 440"), N.LastTranslateScheduleRetail, 0x15c);
	TestEqual(TEXT("102ae7b7 + 10280e53 dispatch slot 435 twice"),
		N.WerewolfScheduleStack.Num(), 2);
	TestEqual(TEXT("102cc229 installs literal IDLE_STAND on the miss"), N.Schedule.Current,
		EElysiumScheduleId::IdleStand);

	N.WerewolfScheduleStack.Reset();
	N.SetState(7);
	N.SetSchedule(1, true);
	TestEqual(TEXT("102ae788 DEAD refuses even a forced install"), N.WerewolfScheduleStack.Num(), 0);
	N.WriteNpcStateRetail(1);
	N.WriteIdealStateRetail(7);
	N.SetSchedule(1, true);
	TestEqual(TEXT("102ae790 ideal DEAD independently refuses"), N.WerewolfScheduleStack.Num(), 0);

	N.WriteIdealStateRetail(1);
	// `IsAlive 0x100b4dc0` reads the life-state transaction, not Troika's separate m_bIsAlive byte.
	N.SetDeathReportedForRestore(true);
	N.SetSchedule(1, false);
	TestEqual(TEXT("102ae7aa dead/dying refuses without force"), N.WerewolfScheduleStack.Num(), 0);
	N.SetSchedule(1, true);
	TestEqual(TEXT("102ae7ae force bypasses IsAlive and dispatches both changes"),
		N.WerewolfScheduleStack.Num(), 2);
	N.SetDeathReportedForRestore(false);

	FElysiumRecordingNpcMotor* Motor = F.World.Services.NpcMotors.IsEmpty()
		? nullptr
		: F.World.Services.NpcMotors[0].Get();
	if (!TestNotNull(TEXT("motor"), Motor))
		return false;
	N.NpcFlags.Set(EElysiumNpcFlag::PRESERVE_PATH);
	Motor->Navigation.Type = EElysiumNpcNavType::Climb;
	N.ForceScheduleChange(EElysiumScheduleId::IdleStand, false);
	TestTrue(TEXT("102ae68d NAV_CLIMB preserves the path bit"),
		N.NpcFlags.Has(EElysiumNpcFlag::PRESERVE_PATH));
	Motor->Navigation.Type = EElysiumNpcNavType::Ground;
	N.ForceScheduleChange(EElysiumScheduleId::IdleStand, false);
	TestFalse(TEXT("102ae69b ordinary navigation clears the path bit"),
		N.NpcFlags.Has(EElysiumNpcFlag::PRESERVE_PATH));

	FElysiumEntity* Open = F.World.World.FindByName(TEXT("open_sequence"));
	if (TestNotNull(TEXT("open sequence"), Open))
	{
		F.World.World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")),
			FElysiumVariant::Void(), 0.0, FElysiumEntityHandle::Invalid(), Open->Handle);
		F.World.World.Tick(0.0);
		TestTrue(TEXT("102ae546 resolves the live cine owner"), N.ScriptOwner == Open->Handle);
		N.ForceScheduleChange(EElysiumScheduleId::IdleStand, false);
		TestFalse(TEXT("102ae654 cancels the interruptable cine"), N.ScriptOwner.IsSet());
	}

	FElysiumEntity* Locked = F.World.World.FindByName(TEXT("locked_sequence"));
	if (TestNotNull(TEXT("locked sequence"), Locked))
	{
		F.World.World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")),
			FElysiumVariant::Void(), 0.0, FElysiumEntityHandle::Invalid(), Locked->Handle);
		F.World.World.Tick(0.1);
		TestFalse(TEXT("102ae5ad reads the locked cine's +0x5f90"),
			Locked->IsScriptedSequenceInterruptable());
		N.ForceScheduleChange(EElysiumScheduleId::IdleStand, false);
		TestFalse(TEXT("102ae60e warning is non-refusing and cancellation still runs"),
			N.ScriptOwner.IsSet());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMaintain19OnScheduleChangeTest,
	"Elysium.Substrate.NpcKernelMaintain19.OnScheduleChange", GMaintain19Flags)
bool FElysiumNpcKernelMaintain19OnScheduleChangeTest::RunTest(const FString&)
{
	FMaintain19Fixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc) || !TestNotNull(TEXT("other"), F.Other))
		return false;
	FElysiumNpc& N = *F.Npc;
	N.SetRetailClassForTests(TEXT("CAI_BaseNPCTroika"));
	N.OpeningDoor = F.Other->Handle;
	N.bOpeningDoorWait = true;
	N.AlternateAi = 1;
	N.NpcFlags.Set(EElysiumNpcFlag2::SLEEP_BOUNDING_BOX);
	N.ScheduleHost.SavedSleepExtents = FVector(3.f);
	N.ScheduleHost.MemoryBits = 0x2000u;
	N.OnScheduleChange(EElysiumScheduleId::Fail);
	TestFalse(TEXT("102a09f8/102a0a07 closes and forgets m_hOpeningDoor"), N.OpeningDoor.IsSet());
	TestFalse(TEXT("slot 532 also clears the wait byte"), N.bOpeningDoorWait);
	TestFalse(TEXT("102a0a79 clears SLEEP_BOUNDING_BOX"),
		N.NpcFlags.Has(EElysiumNpcFlag2::SLEEP_BOUNDING_BOX));
	TestEqual(TEXT("102a0af8 clears memory 0x2000"), N.ScheduleHost.MemoryBits, 0u);

	N.SetRetailClassForTests(TEXT("CNPC_VGargoyle"));
	N.SpeciesShunnedFindCount = 2;
	N.OnScheduleChange(EElysiumScheduleId::IdleStand);
	TestEqual(TEXT("10378fe4 decrements Gargoyle pillar shun"), N.SpeciesShunnedFindCount, 1);
	N.NpcFlags.Set(EElysiumNpcFlag::PRESERVE_PATH);
	N.OnScheduleChange(EElysiumScheduleId::IdleStand);
	TestEqual(TEXT("10378fd8 PRESERVE_PATH freezes it"), N.SpeciesShunnedFindCount, 1);

	N.NpcFlags.Clear(EElysiumNpcFlag::PRESERVE_PATH);
	N.SetRetailClassForTests(TEXT("CNPC_VHengeyokai"));
	N.PathMode = 7;
	N.SpeciesShunnedFindCount = 2;
	N.OnScheduleChange(EElysiumScheduleId::IdleStand);
	TestEqual(TEXT("103830b0 clears Hengeyokai path mode"), N.PathMode, 0);
	TestEqual(TEXT("103830be decrements fish shun"), N.SpeciesShunnedFindCount, 1);

	N.SetRetailClassForTests(TEXT("CNPC_VTzimisce"));
	N.PathMode = 4;
	N.SpeciesShunnedFindCount = 3;
	N.OnScheduleChange(EElysiumScheduleId::IdleStand);
	TestEqual(TEXT("103bf630 clears Tzimisce path mode"), N.PathMode, 0);
	TestEqual(TEXT("103bf63e decrements body shun"), N.SpeciesShunnedFindCount, 2);

	N.SetRetailClassForTests(TEXT("CNPC_VWerewolf"));
	N.WerewolfScheduleStack.Reset();
	for (int32 Index = 0; Index < 51; ++Index)
		N.WerewolfScheduleStack.Add(TEXT("old"));
	N.OnScheduleChange(EElysiumScheduleId::Fail);
	TestEqual(TEXT("103ced8a drops before append and holds 51"), N.WerewolfScheduleStack.Num(), 51);
	TestEqual(TEXT("103cee0e appends the incoming schedule"), N.WerewolfScheduleStack.Last(),
		FString(TEXT("SCHED_FAIL")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMaintain19TaskMovementCompleteTest,
	"Elysium.Substrate.NpcKernelMaintain19.TaskMovementComplete", GMaintain19Flags)
bool FElysiumNpcKernelMaintain19TaskMovementCompleteTest::RunTest(const FString&)
{
	FMaintain19Fixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc))
		return false;
	FElysiumNpc&			   N = *F.Npc;
	FElysiumRecordingNpcMotor* Motor = F.World.Services.NpcMotors.IsEmpty()
		? nullptr
		: F.World.Services.NpcMotors[0].Get();
	if (!TestNotNull(TEXT("motor"), Motor))
		return false;

	N.Schedule.TaskStatus = EElysiumTaskStatus::New;
	N.TaskMovementComplete();
	TestEqual(TEXT("10273edc NEW becomes RUNNING_TASK"), N.Schedule.TaskStatus,
		EElysiumTaskStatus::RunningTask);
	N.Schedule.TaskStatus = EElysiumTaskStatus::Running;
	N.TaskMovementComplete();
	TestEqual(TEXT("10273edc RUNNING becomes RUNNING_TASK"), N.Schedule.TaskStatus,
		EElysiumTaskStatus::RunningTask);
	N.Schedule.TaskStatus = EElysiumTaskStatus::RunningMovement;
	N.Cognition.Conditions.Reset();
	N.TaskMovementComplete();
	TestEqual(TEXT("10273eec RUNNING_MOVEMENT completes"), N.Schedule.TaskStatus,
		EElysiumTaskStatus::Complete);
	N.Schedule.TaskStatus = EElysiumTaskStatus::RunningMovement;
	N.Cognition.Conditions.Set(EElysiumNpcCond::TaskFailed);
	N.TaskMovementComplete();
	TestEqual(TEXT("10273eec TaskComplete(false) cannot overwrite TASK_FAILED"),
		N.Schedule.TaskStatus, EElysiumTaskStatus::RunningMovement);
	N.Cognition.Conditions.Reset();
	N.Schedule.TaskStatus = EElysiumTaskStatus::RunningTask;
	N.TaskMovementComplete();
	TestEqual(TEXT("10273ef3 duplicate completion writes nothing"), N.Schedule.TaskStatus,
		EElysiumTaskStatus::RunningTask);

	Motor->Navigation.Type = EElysiumNpcNavType::Ground;
	Motor->Navigation.bActiveGoal = true;
	N.IdealActivityNumber = 99;
	N.TaskMovementComplete();
	TestEqual(TEXT("10273f20 chooses GetStoppedActivity"), N.IdealActivityNumber, 1);
	TestFalse(TEXT("10273f3a/10273f46 stop then clear the goal"), Motor->Navigation.bActiveGoal);
	FElysiumEntity* Script = F.World.World.FindByName(TEXT("open_sequence"));
	if (TestNotNull(TEXT("script"), Script))
	{
		N.ScriptOwner = Script->Handle;
		N.IdealActivityNumber = 77;
		N.Schedule.TaskStatus = EElysiumTaskStatus::Complete;
		N.TaskMovementComplete();
		TestEqual(TEXT("10273f0a script state skips the stopped activity"),
			N.IdealActivityNumber, 77);
		N.ScriptOwner = FElysiumEntityHandle::Invalid();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMaintain19SabbatTaskFailTest,
	"Elysium.Substrate.NpcKernelMaintain19.SabbatTaskFail", GMaintain19Flags)
bool FElysiumNpcKernelMaintain19SabbatTaskFailTest::RunTest(const FString&)
{
	FMaintain19Fixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc))
		return false;
	FElysiumNpc& N = *F.Npc;
	N.SetRetailClassForTests(TEXT("CNPC_VSabbatLeader"));
	N.SetState(1);
	N.bNpcIsAlive = true;
	N.Cognition.Conditions.Reset();
	N.SabbatLeaderRouteFailCount = 5;
	N.FailureType = 0;
	N.TaskFail(0x0c);
	TestEqual(TEXT("103a9463 increments m_RouteFailCount"), N.SabbatLeaderRouteFailCount, 6);
	TestEqual(TEXT("103a9489 flips m_FailureType"), N.FailureType, 1);
	TestEqual(TEXT("103a94af installs 0x161 by the flipped value"), N.LastSetScheduleRetail, 0x161);
	TestFalse(TEXT("103a94bc returns without base TASK_FAILED"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));
	N.SabbatLeaderRouteFailCount = 6;
	N.TaskFail(0x0f);
	TestEqual(TEXT("103a94ce the next flip installs 0x160"), N.LastSetScheduleRetail, 0x160);
	TestEqual(TEXT("and the flipped value is zero"), N.FailureType, 0);

	N.Cognition.Conditions.Reset();
	N.SabbatLeaderRouteFailCount = 4;
	N.TaskFail(0x0c);
	TestEqual(TEXT("103a9485 below six chains to Troika"), N.SabbatLeaderRouteFailCount, 5);
	TestTrue(TEXT("the chained base raises TASK_FAILED"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));
	N.Cognition.Conditions.Reset();
	N.TaskFail(0x0b);
	TestTrue(TEXT("103a9458 outside 12..15 also chains"),
		N.Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMaintain19LoopTest,
	"Elysium.Substrate.NpcKernelMaintain19.MaintainSchedule", GMaintain19Flags)
bool FElysiumNpcKernelMaintain19LoopTest::RunTest(const FString&)
{
	// Null schedule: select, install and start task zero in the same loop.
	FMaintainRunner		  Runner;
	FElysiumScheduleState State;
	Runner.Time = 0.75;
	TestTrue(TEXT("10281c46 null schedule reselects and runs"),
		ElysiumSchedule::Tick(State, Runner, 1.0, &Runner.Conditions, false));
	TestEqual(TEXT("10280e69 SetSchedule stamps timeStarted"), State.ScheduleStartedAt, 0.75);
	TestEqual(TEXT("10281da2 StartTask stamps timeCurTaskStarted"), State.TaskStartedAt, 1.0);
	TestTrue(TEXT("10281d29 OnStartSchedule fires for task zero"), Runner.OnStartCalls > 0);
	TestTrue(TEXT("10281e89 StartTaskOverlay is reached while running"), Runner.StartOverlayCalls > 0);
	TestTrue(TEXT("10281eee/102821b0 MaintainActivity runs"), Runner.MaintainCalls >= 2);

	// A reduced pass admits one completed task. The ordinary pass also stops after the recovered
	// 8 ms cycle budget, independently of its ten-completion ceiling.
	{
		FWaitPvsTaskScope WaitTasks(EElysiumScheduleId::IdleStand, 3);
		State.Clear();
		Runner.WaitPvsCalls = 0;
		ElysiumSchedule::Start(State, EElysiumScheduleId::IdleStand, Runner);
		ElysiumSchedule::Tick(State, Runner, 1.1, &Runner.Conditions, true);
		TestEqual(TEXT("1028190e reduced maintenance completes one task"), Runner.WaitPvsCalls, 1);

		State.Clear();
		Runner.WaitPvsCalls = 0;
		Runner.WaitPvsDelaySeconds = 0.012;
		ElysiumSchedule::Start(State, EElysiumScheduleId::IdleStand, Runner);
		ElysiumSchedule::Tick(State, Runner, 1.2, &Runner.Conditions, false);
		TestEqual(TEXT("10282179 8 ms budget stops before the second task"), Runner.WaitPvsCalls, 1);
		Runner.WaitPvsDelaySeconds = 0.0;
	}

	// The continuous-move tail writes memory before RunTaskOverlay, with both calls observable.
	Runner.bContinuous = true;
	State.Current = EElysiumScheduleId::IdleStand;
	State.TaskIndex = 2; // TASK_WAIT, a running task with a future deadline
	State.TaskStatus = EElysiumTaskStatus::Running;
	State.TaskEndsAt = 10.0;
	Runner.Conditions.Reset();
	ElysiumSchedule::Tick(State, Runner, 2.0, &Runner.Conditions, true);
	TestTrue(TEXT("102820e6 continuous movement sets the memory fact"), Runner.ContinuousWrites > 0);
	TestTrue(TEXT("102820f2 RunTaskOverlay follows it"), Runner.RunOverlayCalls > 0);

	// `ai_step` returns immediately after NextScheduledTask and skips the common +0x5bb8 store.
	Runner.bAiStep = true;
	State.Current = EElysiumScheduleId::IdleStand;
	State.TaskIndex = 0;
	State.TaskStatus = EElysiumTaskStatus::Complete;
	State.bDidMaintainSchedule = false;
	ElysiumSchedule::Tick(State, Runner, 3.0, &Runner.Conditions, false);
	TestEqual(TEXT("102821fb increments the debug index"), Runner.AiStepAdvances, 1);
	TestFalse(TEXT("10282269 ai_step skips +0x5bb8"), State.bDidMaintainSchedule);
	Runner.bAiStep = false;
	State.TaskIndex = 2;
	State.TaskStatus = EElysiumTaskStatus::Running;
	State.TaskEndsAt = 20.0;
	Runner.bAiStep = true;
	ElysiumSchedule::Tick(State, Runner, 3.5, &Runner.Conditions, true);
	TestEqual(TEXT("102821c2 common ai_step exit runs the freeze arm"), Runner.AiStepFreezes, 1);
	Runner.bAiStep = false;

	// A zero-task program reaches the exact error exit and also skips +0x5bb8.
	{
		FEmptyTaskScope Empty(EElysiumScheduleId::IdleStand);
		State.Clear();
		State.Current = EElysiumScheduleId::IdleStand;
		State.bDidMaintainSchedule = false;
		Runner.MissingCalls = 0;
		TestFalse(TEXT("1028226c zero-task program exits"),
			ElysiumSchedule::Tick(State, Runner, 4.0, &Runner.Conditions, false));
		TestEqual(TEXT("1028226c emits the missing-schedule arm"), Runner.MissingCalls, 1);
		TestFalse(TEXT("10282336 error exit skips +0x5bb8"), State.bDidMaintainSchedule);
	}

	// Special navigation marks both bits before the ordinary failure route.
	State.Clear();
	ElysiumSchedule::Start(State, EElysiumScheduleId::IdleStand, Runner);
	Runner.Conditions.Set(EElysiumNpcCond::TaskFailed);
	Runner.bSpecialNav = true;
	ElysiumSchedule::Tick(State, Runner, 5.0, &Runner.Conditions, true);
	TestEqual(TEXT("10281045 special navigation end is marked"), Runner.SpecialMarks, 1);

	// The independent CHOOSE_NEW_SCHEDULE and state-mismatch invalidators both select immediately.
	Runner = FMaintainRunner();
	ElysiumSchedule::Start(State, EElysiumScheduleId::IdleStand, Runner);
	Runner.bChooseNew = true;
	ElysiumSchedule::Tick(State, Runner, 6.0, &Runner.Conditions, true);
	TestTrue(TEXT("10281075 CHOOSE_NEW_SCHEDULE reaches selection"), Runner.SelectCalls > 0);
	Runner.bStateMismatch = true;
	const int32 SelectBefore = Runner.SelectCalls;
	ElysiumSchedule::Tick(State, Runner, 7.0, &Runner.Conditions, true);
	TestTrue(TEXT("102819ec state mismatch reaches selection"), Runner.SelectCalls > SelectBefore);
	TestTrue(TEXT("10281b63 commits the ideal state before selection"), Runner.CommitCalls > 0);

	// The port represents retail patrol schedules as an existing external executor. A null
	// slot-438 adapter therefore returns at the same 10281b89 selection edge and must not take
	// 102cc229's missing-ID fallback.
	Runner = FMaintainRunner();
	ElysiumSchedule::Start(State, EElysiumScheduleId::IdleStand, Runner);
	Runner.Selected = EElysiumScheduleId::None;
	Runner.bStateMismatch = true;
	Runner.bExternalReturn = true;
	TestFalse(TEXT("10281b89 external schedule answer returns to the caller"),
		ElysiumSchedule::Tick(State, Runner, 8.0, &Runner.Conditions, true));
	TestEqual(TEXT("10281be5 adapter leaves no substitute program"), State.Current,
		EElysiumScheduleId::None);
	TestEqual(TEXT("10280e53 external handoff still dispatches slot 435"),
		Runner.ScheduleChanges, 2);
	TestEqual(TEXT("the executor answer does not reach the missing error"), Runner.MissingCalls, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcKernelMaintain19DoorAndMissingTest,
	"Elysium.Substrate.NpcKernelMaintain19.DoorAndMissing", GMaintain19Flags)
bool FElysiumNpcKernelMaintain19DoorAndMissingTest::RunTest(const FString&)
{
	FMaintain19Fixture F;
	if (!TestNotNull(TEXT("subject"), F.Npc) || !TestNotNull(TEXT("other"), F.Other))
		return false;
	FElysiumNpc& N = *F.Npc;
	N.SetRetailClassForTests(TEXT("CAI_BaseNPCTroika"));
	N.SetState(1);
	N.Schedule.Clear();
	ElysiumSchedule::Start(N.Schedule, EElysiumScheduleId::IdleStand, N);
	N.Cognition.Conditions.Set(EElysiumNpcCond::TaskFailed);
	N.NpcFlags.Set(EElysiumNpcFlag2::IGNORE_DOOR_FAILURE);
	N.NpcFlags.SetRawWord2Bits(FElysiumNpcFlags::Word2UnnamedBit31);
	N.BlockedDoor = F.Other->Handle;
	N.BlockedDoorExpiresAt = 10.0;
	N.MaintainSchedule(5.0, true);
	TestTrue(TEXT("10281a67 future deadline latches the door"), N.BlockedDoor.IsSet());
	TestFalse(TEXT("10281a29 clears flags2 0x200"),
		N.NpcFlags.Has(EElysiumNpcFlag2::IGNORE_DOOR_FAILURE));
	TestFalse(TEXT("10281a29 mask also clears bit 31"),
		N.NpcFlags.HasRawWord2Bits(FElysiumNpcFlags::Word2UnnamedBit31));

	N.Schedule.Clear();
	ElysiumSchedule::Start(N.Schedule, EElysiumScheduleId::IdleStand, N);
	N.Cognition.Conditions.Set(EElysiumNpcCond::TaskFailed);
	N.NpcFlags.Set(EElysiumNpcFlag2::IGNORE_DOOR_FAILURE);
	N.BlockedDoor = F.Other->Handle;
	N.BlockedDoorExpiresAt = 5.0;
	N.MaintainSchedule(5.0, true);
	TestFalse(TEXT("10281a5f equal/expired deadline invalidates the handle"), N.BlockedDoor.IsSet());

	N.Schedule.Clear();
	N.Cognition.Conditions.Reset();
	N.ActivityNumber = 99;
	N.MissingSchedule();
	TestEqual(TEXT("10282280 missing schedule calls slot 310 SetActivity(ACT_IDLE)"),
		N.ActivityNumber, 1);
	TestFalse(TEXT("10282336 missing exit skips +0x5bb8"), N.Schedule.bDidMaintainSchedule);

	ElysiumSchedule::Start(N.Schedule, EElysiumScheduleId::IdleStand, N);
	N.ScheduleHost.CacheInterruptTime = -1.0;
	N.Cognition.CustomInterruptConditions.Reset();
	N.Cognition.GatheredAt = 8.0;
	int32					 Ideal = 0;
	const EElysiumScheduleId Selected = N.SelectScheduleForMaintenance(8.0, Ideal);
	TestEqual(TEXT("102814de refreshes m_flCacheInterruptTime before selection"),
		N.ScheduleHost.CacheInterruptTime, 8.0);
	TestTrue(TEXT("1026a1d9 caches the running program's positive interrupt mask"),
		N.Cognition.CustomInterruptConditions.Has(EElysiumNpcCond::NewEnemy));
	TestEqual(TEXT("102814d0 stores the selector's global id as m_IdealSchedule"),
		Ideal, ElysiumScheduleNumber(EElysiumScheduleId::IdleDisposition));
	TestEqual(TEXT("102cc1f0 resolves the selector through slot 440 and lookup"),
		Selected, EElysiumScheduleId::IdleDisposition);

	FElysiumEntity* Open = F.World.World.FindByName(TEXT("open_sequence"));
	FElysiumEntity* Locked = F.World.World.FindByName(TEXT("locked_sequence"));
	TestTrue(TEXT("+0x5f90 clear sequence is interruptable"),
		Open != nullptr && Open->IsScriptedSequenceInterruptable());
	TestFalse(TEXT("spawnflag 0x20 clears +0x5f90"),
		Locked != nullptr && Locked->IsScriptedSequenceInterruptable());
	return true;
}

#endif
