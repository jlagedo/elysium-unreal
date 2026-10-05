#include "Misc/AutomationTest.h"
#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS && !UE_BUILD_SHIPPING
#include "Debug/ElysiumArenaScenario.h"
#include "Debug/ElysiumArenaV6Adapters.h"
#include "ElysiumEntityWorld.h"
#include "Debug/ElysiumArenaScenarioRunner.h"
#include "HAL/PlatformTime.h"

// Harness-only transaction law, anchored to capture/apply 0x200975f0/0x1011a620.
// No world boot, storage write, arbitrary NPC poke or simulated consumer success.
struct FElysiumV6ArenaPersistenceFixture
{
	static FElysiumArenaScenario Record()
	{
		FElysiumArenaScenario Scenario;
		Scenario.Name = TEXT("transaction_fixture");
		Scenario.Duration = 20.0;
		Scenario.bExpectFail = true;
		FElysiumArenaAction Action;
		Action.Do = EElysiumArenaAction::Save;
		Action.Slot = TEXT("unit");
		Scenario.Script.Add(Action);
		return Scenario;
	}
	static void Arm(FElysiumArenaScenarioRunner& Runner)
	{
		Runner.bStarted = true;
		Runner.bZeroKnown = Runner.bZeroApplied = true;
		Runner.bTransactionPending = true;
		Runner.TransactionAction = 0;
		Runner.StartWall = Runner.TransactionWall = FPlatformTime::Seconds();
		Runner.ActionFired.Init(true, 1);
	}
	static void Fence(FElysiumArenaScenarioRunner& Runner, ElysiumArenaStage::EFence Phase, const FString& Reason = FString())
	{
		ElysiumArenaStage::FTransactionFence Event;
		Event.OperationId = 11;
		Event.Phase = Phase;
		Event.Reason = Reason;
		Runner.OnTransactionFence(Event);
	}
	static bool Pending(const FElysiumArenaScenarioRunner& Runner) { return Runner.bTransactionPending; }
	static bool Monotonic()
	{
		FElysiumArenaScenarioRunner Runner(Record(), ElysiumArenaStage::FHost());
		Arm(Runner);
		Runner.SegmentWorld = 40.0;
		FElysiumArenaScenarioRunner::FEvent Before;
		Runner.StampEvent(Before, 43.0);
		Runner.LastNow = Before.Time;
		Runner.Rebind(nullptr); // deliberate no-world gap freezes the segment offset
		Runner.SegmentWorld = 1.0;
		FElysiumArenaScenarioRunner::FEvent After;
		Runner.StampEvent(After, 1.2);
		return FMath::IsNearlyEqual(Before.Time, 3.0) && FMath::IsNearlyEqual(After.Time, 3.2);
	}
	static bool StateSurvives()
	{
		FElysiumArenaScenarioRunner Runner(Record(), ElysiumArenaStage::FHost());
		Arm(Runner);
		Runner.NextExpect = 1;
		Runner.MatchTimes.Add(2.0);
		Runner.NeverCounts.Add(1);
		Runner.Consumed.Add(0);
		Runner.bZeroApplied = true;
		FElysiumArenaScenarioRunner::FTrackedEntity Old;
		Runner.TrackedEntities.Add(FElysiumEntityHandle(3, 1), Old);
		Runner.Rebind(nullptr);
		return Runner.NextExpect == 1 && Runner.MatchTimes[0] == 2.0 && Runner.NeverCounts[0] == 1
			&& Runner.Consumed.Contains(0) && Runner.ActionFired[0] && Runner.bZeroApplied && Runner.TrackedEntities.IsEmpty();
	}
	static bool EqualityCannotPass()
	{
		FElysiumArenaScenarioRunner Runner(Record(), ElysiumArenaStage::FHost());
		FElysiumArenaScenarioRunner::FCheckpoint Saved;
		Saved.bWritten = Saved.bApplied = true;
		FElysiumArenaValue Before, After;
		Before.Type = After.Type = FElysiumArenaValue::EType::Number;
		Before.Number = 2.0; After.Number = 3.0;
		Saved.Captured.Add(TEXT("npc\ntask.index"), Before);
		Saved.Applied.Add(TEXT("npc\ntask.index"), After);
		Runner.Checkpoints.Add(TEXT("mid"), Saved);
		FElysiumArenaAction Action;
		Action.Checkpoint = TEXT("mid");
		FElysiumArenaWitness Word;
		Word.Who = TEXT("npc"); Word.Field = TEXT("task.index");
		Action.Fields.Add(Word);
		FString Error;
		return !Runner.CompareCheckpoint(Action, Error) && Error.Contains(TEXT("restore equality failed"));
	}
	static void Timeout(FElysiumArenaScenarioRunner& Runner)
	{
		Arm(Runner);
		Runner.TransactionWall -= 61.0;
		Runner.Tick();
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV6ArenaSchemaTest, "Elysium.Arm.V6.ArenaPersistence.Schema",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumV6ArenaSchemaTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	auto Parse = [](const FString& Script, const FString& Probes, const FString& Stage, FString& Error)
	{
		FElysiumArenaScenario Record;
		return ElysiumArenaScenario::ParseText(FString::Printf(TEXT("{\"name\":\"schema\",\"about\":\"typed harness unit\",\"stage\":\"%s\",\"seed\":1,\"duration\":5,\"script\":[%s],\"probes\":[%s]}"),
			*Stage, *Script, *Probes), TEXT("v6-unit.json"), Record, Error);
	};
	// Admission receives the same live entity world as the witness/fixture callbacks.
	FElysiumEntityWorld AdmissionWorld(nullptr, nullptr);
	ElysiumArenaStage::FHost AdmissionHost;
	AdmissionHost.ValidateWitnessAdmission = ElysiumArenaValidateV6Admission;
	FElysiumArenaScenario EmptyAdmission;
	FString AdmissionError;
	TestTrue(TEXT("no requested witness needs no admission"), AdmissionHost.ValidateWitnessAdmission(AdmissionWorld, EmptyAdmission, AdmissionError));
	TestTrue(TEXT("empty requirement has no error"), AdmissionError.IsEmpty());
	const FString Alive = TEXT("{\"at\":\"end\",\"who\":\"player\",\"probe\":\"alive\",\"equals\":true}");
	const FString Save = TEXT("{\"t\":0,\"do\":\"save\",\"slot\":\"unit\",\"checkpoint\":\"mid\",\"fields\":[{\"who\":\"npc\",\"field\":\"task.index\"}]}");
	FString Error;
	TestTrue(TEXT("typed save schema"), Parse(Save, Alive, TEXT("arena"), Error));
	TestFalse(TEXT("missing slot"), Parse(TEXT("{\"t\":0,\"do\":\"save\"}"), Alive, TEXT("arena"), Error));
	TestFalse(TEXT("duplicate checkpoint"), Parse(Save + TEXT(",") + Save, Alive, TEXT("arena"), Error));
	TestFalse(TEXT("missing checkpoint"), Parse(TEXT("{\"t\":0,\"do\":\"restore_compare\",\"checkpoint\":\"missing\",\"fields\":[{\"who\":\"npc\",\"field\":\"task.index\"}]}"), Alive, TEXT("arena"), Error));
	TestFalse(TEXT("unknown word"), Parse(Save.Replace(TEXT("task.index"), TEXT("opaque.bytes")), Alive, TEXT("arena"), Error));
	TestFalse(TEXT("epoch equality excluded"), Parse(Save.Replace(TEXT("task.index"), TEXT("world_generation")), Alive, TEXT("arena"), Error));
	TestFalse(TEXT("unsupported map fixture"), Parse(TEXT("{\"t\":0,\"do\":\"invalid_marker\",\"target\":\"npc\"}"), Alive, TEXT("map:sp_tutorial_1"), Error));
	TestFalse(TEXT("unknown transaction key"), Parse(Save.Replace(TEXT("\"slot\":"), TEXT("\"command\":\"god\",\"slot\":")), Alive, TEXT("arena"), Error));
	TestFalse(TEXT("incompatible probe scalar"), Parse(TEXT(""), TEXT("{\"at\":\"applied\",\"who\":\"npc\",\"probe\":\"witness\",\"field\":\"hidden\",\"equals\":1}"), TEXT("arena"), Error));
	TestTrue(TEXT("explicit map spawn door"), Parse(TEXT("{\"t\":0,\"do\":\"spawn\",\"row\":{\"name\":\"relay\",\"classname\":\"logic_relay\",\"at\":[0,0,0]}}"), Alive, TEXT("map:sp_tutorial_1"), Error));
	TestTrue(TEXT("apply fence empty coordinator"), Parse(TEXT(""), TEXT("{\"at\":\"applied\",\"who\":\"world\",\"probe\":\"witness\",\"field\":\"coordinator.normal.count\",\"equals\":0}"), TEXT("arena"), Error));
	const FString Initial = TEXT("{\"who\":\"npc\",\"weapon\":\"item_w_flamethrower\",\"magazine\":0,\"reserve\":250,\"fake_reload_count\":8}");
	auto ParseInitial = [&](const FString& Fixture, const FString& Stage)
	{
		FElysiumArenaScenario Scenario;
		return ElysiumArenaScenario::ParseText(FString::Printf(TEXT("{\"name\":\"initial\",\"about\":\"fourth-sitting fixture\",\"stage\":\"%s\",\"duration\":30,\"initial_weapon_state\":[%s],\"probes\":[{\"at\":\"end\",\"who\":\"player\",\"probe\":\"alive\",\"equals\":true}]}"), *Stage, *Fixture), TEXT("initial-unit.json"), Scenario, Error);
	};
	TestTrue(TEXT("explicit initial weapon fixture schema"), ParseInitial(Initial, TEXT("arena")));
	TestFalse(TEXT("initial negative amount refuses"), ParseInitial(Initial.Replace(TEXT("\"magazine\":0"), TEXT("\"magazine\":-1")), TEXT("arena")));
	TestFalse(TEXT("initial noninteger refuses"), ParseInitial(Initial.Replace(TEXT("\"reserve\":250"), TEXT("\"reserve\":1.5")), TEXT("arena")));
	TestFalse(TEXT("duplicate owner refuses before any setup"), ParseInitial(Initial + TEXT(",") + Initial, TEXT("arena")));
	TestFalse(TEXT("wrong host refuses"), ParseInitial(Initial, TEXT("map:sp_tutorial_1")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV6ArenaFenceTest, "Elysium.Arm.V6.ArenaPersistence.Fences",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumV6ArenaFenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	using ElysiumArenaStage::EFence;
	const FElysiumArenaScenario Record = FElysiumV6ArenaPersistenceFixture::Record();
	FElysiumArenaScenarioRunner Accepted(Record, ElysiumArenaStage::FHost());
	FElysiumV6ArenaPersistenceFixture::Arm(Accepted);
	FElysiumV6ArenaPersistenceFixture::Fence(Accepted, EFence::Captured);
	TestTrue(TEXT("capture is not successful storage"), FElysiumV6ArenaPersistenceFixture::Pending(Accepted));
	FElysiumV6ArenaPersistenceFixture::Fence(Accepted, EFence::Written);
	TestFalse(TEXT("write result unblocks"), FElysiumV6ArenaPersistenceFixture::Pending(Accepted));
	FElysiumArenaScenarioRunner Failed(Record, ElysiumArenaStage::FHost());
	FElysiumV6ArenaPersistenceFixture::Arm(Failed);
	FElysiumV6ArenaPersistenceFixture::Fence(Failed, EFence::Failed, TEXT("native slot write failed"));
	TestTrue(TEXT("asynchronous failure ends record"), Failed.IsDone());
	TestEqual(TEXT("exact failure retained"), Failed.GetResult().UnmetReason, FString(TEXT("native slot write failed")));
	FElysiumArenaScenarioRunner Unfenced(Record, ElysiumArenaStage::FHost());
	FElysiumV6ArenaPersistenceFixture::Arm(Unfenced);
	FElysiumV6ArenaPersistenceFixture::Fence(Unfenced, EFence::Written);
	TestTrue(TEXT("unfenced write cannot pass"), Unfenced.IsDone());
	FElysiumArenaScenarioRunner TimedOut(Record, ElysiumArenaStage::FHost());
	FElysiumV6ArenaPersistenceFixture::Timeout(TimedOut);
	TestEqual(TEXT("bounded no-world gap"), TimedOut.GetResult().UnmetReason, FString(TEXT("transaction wall timeout")));
	FElysiumArenaScenario WrongMapRecord = Record;
	WrongMapRecord.Script[0].Do = EElysiumArenaAction::Travel;
	WrongMapRecord.Script[0].Map = TEXT("sp_tutorial_1");
	FElysiumArenaScenarioRunner WrongMap(WrongMapRecord, ElysiumArenaStage::FHost());
	FElysiumV6ArenaPersistenceFixture::Arm(WrongMap);
	FElysiumV6ArenaPersistenceFixture::Fence(WrongMap, EFence::Rebinding);
	TestEqual(TEXT("wrong map refuses before rebind"), WrongMap.GetResult().UnmetReason, FString(TEXT("wrong map at reconstruction fence")));
	TestTrue(TEXT("scenario clock survives rewind"), FElysiumV6ArenaPersistenceFixture::Monotonic());
	TestTrue(TEXT("runner equality failure cannot pass"), FElysiumV6ArenaPersistenceFixture::EqualityCannotPass());
	TestTrue(TEXT("labels never counts fired action and zero remain; old removals cleared"), FElysiumV6ArenaPersistenceFixture::StateSurvives());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumV6ArenaEqualityTest, "Elysium.Arm.V6.ArenaPersistence.Equality",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumV6ArenaEqualityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FElysiumArenaValue Saved, Applied;
	Saved.Type = Applied.Type = FElysiumArenaValue::EType::Number;
	Saved.Number = 12.5; Applied.Number = 12.6;
	TestFalse(TEXT("lost interval fails equality"), ElysiumArenaScenario::WitnessEqual(Saved, Applied, 0.0));
	TestTrue(TEXT("declared conversion tolerance"), ElysiumArenaScenario::WitnessEqual(Saved, Applied, 0.11));
	const FElysiumArenaValue Time = ElysiumArenaScenario::RebaseWitness(TEXT("task.started"), Saved, 10.0, 40.0);
	TestEqual(TEXT("TIME carries relative interval"), Time.Number, 42.5);
	TestEqual(TEXT("weapon FLOAT remains raw"), ElysiumArenaScenario::RebaseWitness(TEXT("weapon.next_primary"), Saved, 10.0, 40.0).Number, 12.5);
	Saved.Number = 0.0;
	TestEqual(TEXT("zero wait sentinel"), ElysiumArenaScenario::RebaseWitness(TEXT("task.wait"), Saved, 10.0, 40.0).Number, 0.0);
	Saved.Type = Applied.Type = FElysiumArenaValue::EType::String;
	Saved.String = TEXT("#12"); Applied.String = TEXT("#13");
	TestFalse(TEXT("same-name different entity cannot compare equal"), ElysiumArenaScenario::WitnessEqual(Saved, Applied, 0.0));
	return true;
}
#endif
