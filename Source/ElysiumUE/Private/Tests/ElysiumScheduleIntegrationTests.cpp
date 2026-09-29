#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/ElysiumNpcTestFixture.h"
#include "Substrate/ElysiumScheduleCorpus.h"
#include "Substrate/ElysiumScheduleNumbers.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleClassSpaceIntegrationTest,
	"Elysium.Substrate.ScheduleIntegration.ClassSpace",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleClassSpaceIntegrationTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("schedule_spaces"), 904);
	Builder.AddNpc(TEXT("runner"), FVector::ZeroVector, TEXT("npc_VTzimisceRunner"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Npc = F.Npc(TEXT("runner"));
	if (!TestNotNull(TEXT("runner NPC"), Npc)) return false;
	FElysiumNpcWorldFixture::Quiet({Npc});
	const auto& Corpus = FElysiumScheduleCorpus::Get();
	const auto* Program = Corpus.Manager().FindByName(TEXT("SCHED_VTZIMISCERUNNER_WAIT_FOR_MELEE_ADVANCE"));
	if (!TestNotNull(TEXT("retail runner program"), Program)) return false;
	TestEqual(TEXT("slot 580 translates the species-local id"), Npc->ResolveScheduleId(0x156), Program->GlobalId);
	TestEqual(TEXT("inverse translates through the same class"), Npc->LocalScheduleId(Program->GlobalId), 0x156);
	TestTrue(TEXT("the species program starts"), ElysiumSchedule::Start(Npc->Schedule, 0x156, *Npc));
	TestEqual(TEXT("no IDLE_STAND substitution"), Npc->Schedule.Current, Program->GlobalId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleFlagIntegrationTest,
	"Elysium.Substrate.ScheduleIntegration.FlagWords",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleFlagIntegrationTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("schedule_flags"), 905);
	Builder.AddNpc(TEXT("guard"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder));
	FElysiumNpc* Npc = F.Npc(TEXT("guard"));
	if (!TestNotNull(TEXT("NPC"), Npc)) return false;
	FElysiumNpcWorldFixture::Quiet({Npc});
	const auto* Program = FElysiumScheduleCorpus::Get().Manager().FindByName(TEXT("SCHED_TROIKA_COMBAT_WAIT"));
	if (!TestNotNull(TEXT("retail combat wait"), Program)) return false;
	ElysiumSchedule::Start(Npc->Schedule, Program->GlobalId, *Npc);
	ElysiumSchedule::Tick(Npc->Schedule, *Npc, 0.0);
	TestTrue(TEXT("TASKS_FACE_ENEMY written to word two"), Npc->NpcFlags.Has(EElysiumNpcFlag2::TASKS_FACE_ENEMY));
	TestFalse(TEXT("word one's FINDING_BODY is untouched"), Npc->NpcFlags.Has(EElysiumNpcFlag::FINDING_BODY));
	// `TASK_SET_NPC_FLAG` (0x100)'s Troika arm `0x102a585d` with a word-one operand (story 8 wave 2:
	// the port's `SetNpcFlag` verb deleted; the arm is reached through slot 442).
	FElysiumScheduleStep SetFlag;
	const FElysiumLocalIdSpace* TaskSpace = Npc->IdSpace(EElysiumIdCategory::Task);
	SetFlag.TaskId = TaskSpace != nullptr ? TaskSpace->LocalToGlobal(0x100) : INDEX_NONE;
	SetFlag.SetRawWord(0x00080000u);
	Npc->StartTaskSlot442(&SetFlag);
	TestTrue(TEXT("word-one operands still work"), Npc->NpcFlags.Has(EElysiumNpcFlag::NO_DIALOG));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleConditionSpaceIntegrationTest,
	"Elysium.Substrate.ScheduleIntegration.ConditionSpace",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleConditionSpaceIntegrationTest::RunTest(const FString&)
{
	auto& Corpus = FElysiumScheduleCorpus::Get();
	if (!Corpus.EnsureLoaded()) return false;
	const auto* Space = Corpus.SpaceFor(TEXT("CNPC_VWerewolf"), EElysiumIdCategory::Condition);
	const auto* Program = Corpus.Manager().FindByName(TEXT("SCHED_VWEREWOLF_FAIL"));
	if (!TestNotNull(TEXT("werewolf space"), Space) || !TestNotNull(TEXT("werewolf fail text"), Program)) return false;
	const int32 Global = Corpus.Namespace(EElysiumIdCategory::Condition).Find(TEXT("COND_VWEREWOLF_CAN_TELEPORT"));
	TestNotEqual(TEXT("species local and global ordinal differ"), Global - ElysiumScheduleId::GlobalBase, 0x77);
	const FElysiumNpcConditions Local = FElysiumNpcConditions::Of({EElysiumNpcCond::CanTeleport});
	const auto GlobalConditions = Local.ToGlobalOrdinals(Space);
	TestTrue(TEXT("retail producer meets the parsed interrupt"), Program->Interrupts.Intersects(GlobalConditions));
	TestTrue(TEXT("mask query sees the class-local condition"), Program->Interrupts.ToLocalOrdinals(Space).Has(EElysiumNpcCond::CanTeleport));
	TestTrue(TEXT("condition conversion round trips"), GlobalConditions.ToLocalOrdinals(Space) == Local);
	struct FConditionRunner final : IElysiumScheduleRunner
	{
		const FElysiumLocalIdSpace* Space = nullptr;
		bool bInterrupted = false;
		virtual const FElysiumLocalIdSpace* ConditionIdSpace() const override { return Space; }
		virtual void RecordScheduleEvent(const FString& Text) override
		{
			bInterrupted |= Text.Contains(TEXT("interrupted by"));
		}
	} Runner;
	Runner.Space = Space;
	FElysiumScheduleState State;
	ElysiumSchedule::Start(State, Program->GlobalId, Runner);
	TestTrue(TEXT("the live mask accessor uses the same class space"),
		ElysiumSchedule::MaskHasCondition(State, Runner, EElysiumNpcCond::CanTeleport));
	ElysiumSchedule::Tick(State, Runner, 0.0, &Local);
	TestTrue(TEXT("Tick observes the species producer, not bit 0x8f in the local set"), Runner.bInterrupted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScheduleExecutableWitnessTest,
	"Elysium.Substrate.ScheduleIntegration.ChaseFailureWitness",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumScheduleExecutableWitnessTest::RunTest(const FString&)
{
	FElysiumNpcWorldBuilder Builder(TEXT("schedule_witness"), 906);
	Builder.AddNpc(TEXT("guard")).Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/santa_monica/smiling_jack/smiling_jack.mdl"));
	Builder.AddNpc(TEXT("enemy"), FVector(500, 0, 0)).Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/santa_monica/smiling_jack/smiling_jack.mdl"));
	FElysiumNpcWorldFixture F(MoveTemp(Builder), [](auto& Services)
	{
		Services.bProvideNpcMotor = true;
		Services.bNpcActivitiesResolve = true;
	});
	FElysiumNpc* Npc = F.Npc(TEXT("guard"));
	FElysiumNpc* Enemy = F.Npc(TEXT("enemy"));
	if (!Npc || !Enemy) return false;
	FElysiumNpcWorldFixture::Quiet({Npc, Enemy});
	FElysiumNpcWorldFixture::PrepareForKernelDrive(Npc);
	Npc->BaseMemory.Enemy = Enemy->Handle;
	FElysiumRecordingNpcMotor* Motor = nullptr;
	for (const auto& Candidate : F.Services.NpcMotors)
		if (Candidate->Owner == Npc->Handle) Motor = Candidate.Get();
	if (!TestNotNull(TEXT("recording motor"), Motor)) return false;
	TArray<FVector> SightPoints;
	F.Services.LineOfSightQuery = [&SightPoints](const FVector&, const FVector& Point)
	{
		SightPoints.Add(Point);
		return Point.Y >= -1.0; // origin exposed, first left candidate in cover
	};
	// Story 8 wave 2: the witness runs through the real slot 442 / 444 arms, which read the world's
	// `curtime`; each pass ticks the (quiet) world to its time first, then runs the interpreter.
	auto Maintain = [&F, Npc](double T, const FElysiumNpcConditions* Conditions = nullptr)
	{
		if (F.World.NowSeconds() < T)
		{
			F.World.Tick(T);
		}
		return ElysiumSchedule::Tick(Npc->Schedule, *Npc, T, Conditions);
	};
	const double Start = F.World.NowSeconds();
	ElysiumSchedule::Start(Npc->Schedule, ElysiumSched::SCHED_TROIKA_CHASE_ENEMY_FAILED, *Npc);
	Maintain(Start);
	TestEqual(TEXT("retail 0.2s wait runs first"), Npc->Schedule.TaskIndex, 1);
	// `m_flWaitFinished` (+0x5db4), the base TASK_WAIT arm's stamp (`0x10286511`); the RunTask arm
	// completes at an ordered `curtime >= m_flWaitFinished` (`0x10288c2a`).
	Maintain(Npc->BaseScheduleHost.WaitFinished);
	TestEqual(TEXT("retail witness reaches WAIT_FOR_MOVEMENT"), Npc->Schedule.TaskIndex, 7);
	TestEqual(TEXT("origin then first left candidate, no right query"), SightPoints.Num(), 2);
	TestTrue(TEXT("48 Source units to the left"), FMath::IsNearlyEqual(Motor->RequestedFeet.Y, -48.0 * ElysiumMove::U, 0.01));
	TestTrue(TEXT("FORCE_RELAXED_ANIMS runs after finding cover"), Npc->NpcFlags.Has(EElysiumNpcFlag::FORCE_RELAXED_ANIMS));
	Motor->SampleStatus = EElysiumNpcMoveStatus::Reached;
	// The think's `PerformMovement` (`0x1026c120`) -> `CAI_Navigator::Move`: the route's end runs
	// `OnNavComplete` and the owner's `TaskMovementComplete` (`0x10273ec0`).
	Npc->NavigatorMoveStep();
	// One pass: the arrival completes WAIT_FOR_MOVEMENT and the remaining tasks run in the same
	// `MaintainSchedule` loop up to the authored final wait.
	Maintain(Npc->BaseScheduleHost.WaitFinished + 0.1);
	TestEqual(TEXT("all twelve tasks reached, final authored wait running"), Npc->Schedule.TaskIndex, 11);
	TestTrue(TEXT("INCOVER remembered"), (Npc->BaseScheduleHost.MemoryBits & 2u) != 0);
	TestFalse(TEXT("no task failed along the successful witness"), Npc->Cognition.Conditions.Has(EElysiumNpcCond::TaskFailed));
	const int32 WitnessId = Npc->Schedule.Current;
	const double LastWaitEnd = Npc->BaseScheduleHost.WaitFinished;
	Maintain(LastWaitEnd - 0.01);
	TestEqual(TEXT("final wait holds until its authored deadline"), Npc->Schedule.Current, WitnessId);
	Maintain(LastWaitEnd + 0.01);
	TestNotEqual(TEXT("the complete witness returns to selection"), Npc->Schedule.Current, WitnessId);

	// Failure is the other authored arm, not the old whitelist's IDLE_STAND fallback.
	F.Services.LineOfSightQuery = [](const FVector&, const FVector&) { return true; };
	ElysiumSchedule::Start(Npc->Schedule, ElysiumSched::SCHED_TROIKA_CHASE_ENEMY_FAILED, *Npc);
	const double FailureStart = LastWaitEnd + 1.0;
	Maintain(FailureStart);
	Maintain(FailureStart + 0.3);
	TestEqual(TEXT("no cover has retail failure 8"), Npc->BaseScheduleHost.FailureReason, 8);
	Maintain(FailureStart + 0.4, &Npc->Cognition.Conditions);
	TestEqual(TEXT("STANDOFF translates to the loaded Troika program"), Npc->LocalScheduleId(Npc->Schedule.Current), 0xc1);
	return true;
}
#endif
