#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ElysiumCharacterModelAdmission.h"
#include "Visual/ElysiumNativeAnimationData.h"
#include "Engine/GameInstance.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCharacterAdmissionTicketsTest, "Elysium.Substrate.CharacterModelAdmission.Replacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumCharacterAdmissionTicketsTest::RunTest(const FString&)
{
	FElysiumCharacterModelRequests Requests;
	const FElysiumEntityHandle Actor(4, 70), Other(5, 70);
	const FString A = TEXT("vtmb:model:fixture/a"), B = TEXT("vtmb:model:fixture/b");
	const auto FirstA = Requests.Begin(Actor, A, 1);
	const auto Independent = Requests.Begin(Other, A, 1);
	TestTrue(TEXT("initial request current"), Requests.IsCurrent(FirstA, 70, Actor, A, true));
	const auto ReplacedB = Requests.Begin(Actor, B, 2);
	TestFalse(TEXT("model replacement rejects old completion"), Requests.IsCurrent(FirstA, 70, Actor, B, true));
	const auto LatestA = Requests.Begin(Actor, A, 3);
	TestFalse(TEXT("A-B-A must reject old A despite identical model and entity"), Requests.IsCurrent(FirstA, 70, Actor, A, true));
	TestFalse(TEXT("old completion cannot consume newest request"), Requests.Remove(ReplacedB));
	TestTrue(TEXT("newest generation remains current"), Requests.IsCurrent(LatestA, 70, Actor, A, true));
	TestTrue(TEXT("same model on another actor is independent"), Requests.IsCurrent(Independent, 70, Other, A, true));
	TestTrue(TEXT("one successful completion consumes request"), Requests.Remove(LatestA));
	TestFalse(TEXT("duplicate callback cannot install twice"), Requests.IsCurrent(LatestA, 70, Actor, A, true));
	TestFalse(TEXT("duplicate consumption fails"), Requests.Remove(LatestA));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCharacterAdmissionRetirementTest, "Elysium.Substrate.CharacterModelAdmission.Retirement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumCharacterAdmissionRetirementTest::RunTest(const FString&)
{
	FElysiumCharacterModelRequests Requests;
	const FElysiumEntityHandle Actor(2, 80);
	const FString Id = TEXT("vtmb:model:fixture/model");
	const auto Ticket = Requests.Begin(Actor, Id, 9);
	int32 Installs = 0;
	auto Deliver = [&](uint32 WorldEpoch, FElysiumEntityHandle Handle, const FString& Model, bool bAlive)
	{
		if (Requests.IsCurrent(Ticket, WorldEpoch, Handle, Model, bAlive) && Requests.Remove(Ticket)) ++Installs;
	};
	Deliver(81, Actor, Id, true);
	Deliver(80, FElysiumEntityHandle(3, 80), Id, true);
	Deliver(80, FElysiumEntityHandle(2, 81), Id, true);
	Deliver(80, Actor, TEXT("vtmb:model:fixture/replacement"), true);
	Deliver(80, Actor, Id, false);
	TestEqual(TEXT("epoch/index/model/death mutations install nothing"), Installs, 0);
	Deliver(80, Actor, Id, true); Deliver(80, Actor, Id, true);
	TestEqual(TEXT("only current live completion installs exactly once"), Installs, 1);
	const auto Cancelled = Requests.Begin(Actor, Id, 10);
	Requests.Cancel(Actor);
	TestFalse(TEXT("cancelled request cannot install"), Requests.IsCurrent(Cancelled, 80, Actor, Id, true));
	const auto OldWorld = Requests.Begin(Actor, Id, 11);
	Requests.Reset();
	TestFalse(TEXT("map teardown invalidates all pending callbacks"), Requests.IsCurrent(OldWorld, 80, Actor, Id, true));
	TestEqual(TEXT("teardown clears readiness count"), Requests.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNativeAdmissionCancelTest, "Elysium.Substrate.CharacterModelAdmission.NativeCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumNativeAdmissionCancelTest::RunTest(const FString&)
{
	// Cancel before the queued discovery tick: this fixture never reads a package or starts a load.
	auto* Game = NewObject<UGameInstance>();
	auto* Native = NewObject<UElysiumNativeAnimationData>(Game);
	FString Error; int32 Callbacks = 0;
	const FString Id = TEXT("vtmb:model:fixture/unloaded");
	TestFalse(TEXT("readiness query does not load a cast"), Native->IsModelReady(Id));
	const uint64 First = Native->AdmitModelAsync(Id, 0, [&Callbacks](bool, const FString&) { ++Callbacks; }, Error);
	TestTrue(TEXT("valid async request gets a cancellation token"), First != 0);
	TestEqual(TEXT("completion never runs inline"), Callbacks, 0);
	TestEqual(TEXT("request retained while discovery is queued"), Native->NumPendingModelAdmissions(), 1);
	Native->CancelModelAdmission(First);
	TestEqual(TEXT("cancel removes queued request"), Native->NumPendingModelAdmissions(), 0);
	TestEqual(TEXT("cancelled completion suppressed"), Callbacks, 0);
	const uint64 Second = Native->AdmitModelAsync(Id, 0, [&Callbacks](bool, const FString&) { ++Callbacks; }, Error);
	TestTrue(TEXT("new request cannot reuse a stale cancellation token"), Second > First);
	Native->CancelModelAdmission(First);
	TestEqual(TEXT("old cancellation does not remove replacement"), Native->NumPendingModelAdmissions(), 1);
	Native->ReleaseEpoch(0);
	TestEqual(TEXT("native epoch release cancels pending discovery"), Native->NumPendingModelAdmissions(), 0);
	TestEqual(TEXT("retirement never calls installation"), Callbacks, 0);
	TestEqual(TEXT("foreign native epoch is refused"), Native->AdmitModelAsync(Id, 1,
		[](bool, const FString&) {}, Error), uint64(0));
	return true;
}
#endif
