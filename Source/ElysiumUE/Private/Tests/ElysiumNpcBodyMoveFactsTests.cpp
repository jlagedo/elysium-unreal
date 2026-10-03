#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "AIController.h"
#include "AITypes.h"
#include "AbstractNavData.h"
#include "Components/CapsuleComponent.h"
#include "ElysiumPawn.h"
#include "Engine/World.h"
#include "Navigation/PathFollowingComponent.h"
#include "Tests/ElysiumPlayerWorldFixture.h"
#include "Visual/ElysiumNpcBody.h"

// The body's private move record, opened and closed the way `MoveTo` does, so a case can drive the
// follower with an abstract path (no navmesh) and still go through the body's own handlers.
struct FElysiumNpcBodyMoveTestAccess
{
	static void Begin(AElysiumNpcBody* Body, const FElysiumNpcMoveRequest& Request)
	{
		Body->BeginMoveFacts(Request);
	}
	static void End(AElysiumNpcBody* Body, FAIRequestID RequestId) { Body->EndMoveIssue(RequestId); }
	static void Bind(AElysiumNpcBody* Body, UPathFollowingComponent* Following)
	{
		Body->BindMoveFinished(Following);
	}
	static void FinishImmediately(AElysiumNpcBody* Body, UPathFollowingComponent* Following,
		EElysiumNpcMoveResultCode Result, EElysiumNpcMoveResultFlags ExtraFlags)
	{
		Body->FinishRequestImmediately(Following, Result, ExtraFlags);
	}
	// `MoveTo` sets this once the follower has accepted the request; the abstract-path helper below
	// bypasses `MoveTo`, so a case that needs the body's own "a request stands" gate sets it here.
	static void SetMoveRequested(AElysiumNpcBody* Body, bool bRequested) { Body->bMoveRequested = bRequested; }
	static void SetPathPartial(AElysiumNpcBody* Body, bool bPartial) { Body->bRequestPathPartial = bPartial; }
	static void Deliver(AElysiumNpcBody* Body, uint32 RequestId, const FPathFollowingResult& Result)
	{
		Body->OnMoveRequestFinished(FAIRequestID(RequestId), Result);
	}
	static void RecordContact(AElysiumNpcBody* Body, const FElysiumEntityHandle& Blocker)
	{
		Body->RecentBlocker = Blocker;
		Body->RecentBlockerFrame = GFrameCounter;
	}
};

namespace ElysiumNpcBodyMoveTests
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask
	| EAutomationTestFlags::ProductFilter;

AElysiumNpcBody* SpawnBody(FPlayerWorldFixture& Fixture)
{
	AElysiumNpcBody* Body = Fixture.World->SpawnActor<AElysiumNpcBody>();
	if (Body == nullptr) return nullptr;
	Body->InitializeAtFeet(FVector::ZeroVector, 0.f);
	Body->SetRuntimeReady(true);
	return Body;
}

AElysiumNpcBody* SpawnBodyWithController(FPlayerWorldFixture& Fixture)
{
	AElysiumNpcBody* Body = SpawnBody(Fixture);
	if (Body != nullptr) Body->SpawnDefaultController();
	return Body;
}

FElysiumNpcMoveRequest MakeRequest(const FVector& Destination, float ToleranceCm)
{
	FElysiumNpcMoveRequest Request;
	Request.DestinationCm = Destination;
	Request.AcceptanceToleranceCm = ToleranceCm;
	Request.SpeedCmPerSecond = 150.f;
	return Request;
}

// Hands the follower a two-point abstract path the way the smart-link cases do, under the body's own
// record: `Begin` before the request, `End` once the follower has answered with an id.
FAIRequestID IssueAbstractRequest(AElysiumNpcBody* Body, UPathFollowingComponent* Following,
	const FElysiumNpcMoveRequest& Request)
{
	FVector Feet;
	float Yaw = 0.f;
	Body->SampleTransform(Feet, Yaw);
	FElysiumNpcBodyMoveTestAccess::Begin(Body, Request);
	FNavPathSharedPtr Path = MakeShared<FAbstractNavigationPath, ESPMode::ThreadSafe>();
	Path->GetPathPoints().Emplace(Feet);
	Path->GetPathPoints().Emplace(Request.DestinationCm);
	Path->MarkReady();
	FAIMoveRequest Move(Request.DestinationCm);
	Move.SetAcceptanceRadius(10.f);
	Move.SetUsePathfinding(false);
	const FAIRequestID Id = Following->RequestMove(Move, Path);
	FElysiumNpcBodyMoveTestAccess::End(Body, Id);
	return Id;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcBodyFollowerRequestTest,
	"Elysium.Arm.Visual.NpcBody.MoveRequest.FollowerRequest", ElysiumNpcBodyMoveTests::Flags)
bool FElysiumNpcBodyFollowerRequestTest::RunTest(const FString&)
{
	using namespace ElysiumNpcBodyMoveTests;
	// Retail's goal-waypoint arrival, 0.0625 units in cm.
	const float RetailToleranceCm = 0.0625f * 2.54f;
	FElysiumNpcMoveRequest Request = MakeRequest(FVector(100, 0, 0), RetailToleranceCm);

	FElysiumNpcFollowerRequest Follower = AElysiumNpcBody::ResolveFollowerRequest(Request);
	TestEqual(TEXT("the request's tolerance is kept exactly for the body's own reach test"),
		Follower.ExactToleranceCm, RetailToleranceCm);
	TestEqual(TEXT("the follower is handed the named floor where the tolerance is finer than it lands"),
		Follower.AcceptanceRadiusCm, ElysiumNpcBodyMove::FollowerArrivalFloorCm);

	Request.AcceptanceToleranceCm = 30.f;
	Follower = AElysiumNpcBody::ResolveFollowerRequest(Request);
	TestEqual(TEXT("a tolerance above the floor reaches the follower unclamped"), Follower.AcceptanceRadiusCm, 30.f);
	TestEqual(TEXT("and stays the exact reach test"), Follower.ExactToleranceCm, 30.f);

	Request.PartialPath = EElysiumNpcPartialPath::Refuse;
	TestFalse(TEXT("Refuse: no partial path"), AElysiumNpcBody::ResolveFollowerRequest(Request).bAllowPartialPath);
	Request.PartialPath = EElysiumNpcPartialPath::Accept;
	TestTrue(TEXT("Accept: partial path"), AElysiumNpcBody::ResolveFollowerRequest(Request).bAllowPartialPath);

	Request.PedestrianCostMultiplier = 0;
	Follower = AElysiumNpcBody::ResolveFollowerRequest(Request);
	TestFalse(TEXT("multiplier 0: the default filter"), Follower.bUsePedestrianFilter);
	Request.PedestrianCostMultiplier = 7;
	Follower = AElysiumNpcBody::ResolveFollowerRequest(Request);
	TestTrue(TEXT("multiplier 7: the pedestrian filter"), Follower.bUsePedestrianFilter);
	TestEqual(TEXT("at the drawn price"), Follower.PedestrianCostMultiplier, 7);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcBodyMoveFactsNoControllerTest,
	"Elysium.Arm.Visual.NpcBody.MoveFacts.NoController", ElysiumNpcBodyMoveTests::Flags)
bool FElysiumNpcBodyMoveFactsNoControllerTest::RunTest(const FString&)
{
	using namespace ElysiumNpcBodyMoveTests;
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	AElysiumNpcBody* Body = SpawnBody(Fixture);
	if (!TestNotNull(TEXT("body"), Body)) return false;
	TestNull(TEXT("a standing body has no controller"), Body->GetController());
	FElysiumNpcMoveFacts Facts;
	Facts.RemainingDistance2DCm = 123.f;
	TestFalse(TEXT("no follower, no facts"), Body->SampleMoveFacts(Facts));
	TestEqual(TEXT("and the caller's record is untouched"), Facts.RemainingDistance2DCm, 123.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcBodyMoveFactsRefusedTest,
	"Elysium.Arm.Visual.NpcBody.MoveFacts.RefusedRequest", ElysiumNpcBodyMoveTests::Flags)
bool FElysiumNpcBodyMoveFactsRefusedTest::RunTest(const FString&)
{
	using namespace ElysiumNpcBodyMoveTests;
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	AElysiumNpcBody* Body = SpawnBodyWithController(Fixture);
	if (!TestNotNull(TEXT("body"), Body)) return false;
	FElysiumNpcMoveFacts Facts;
	TestTrue(TEXT("a body with a follower reports facts before any request"), Body->SampleMoveFacts(Facts));
	TestFalse(TEXT("nothing is alive before a request"), Facts.bRequestAlive);
	TestFalse(TEXT("nothing has ended before a request"), Facts.bRequestEnded);
	TestEqual(TEXT("no result before a request"), Facts.ResultCode, EElysiumNpcMoveResultCode::None);

	// The transient world carries no navmesh: the destination cannot be projected, so the request is
	// refused and the follower reports it Invalid.
	const FVector Destination(500, 0, 0);
	TestFalse(TEXT("a request no navmesh can serve is refused"),
		Body->MoveTo(MakeRequest(Destination, 0.16f)));
	TestTrue(TEXT("the refusal leaves facts to read"), Body->SampleMoveFacts(Facts));
	TestFalse(TEXT("a refused request is not alive"), Facts.bRequestAlive);
	TestTrue(TEXT("the follower reported an end"), Facts.bRequestEnded);
	TestEqual(TEXT("as Invalid"), Facts.ResultCode, EElysiumNpcMoveResultCode::Invalid);
	TestFalse(TEXT("with no path"), Facts.bHasPath);
	FVector Feet;
	float Yaw = 0.f;
	Body->SampleTransform(Feet, Yaw);
	TestTrue(TEXT("the distance is measured to the requested destination"),
		FMath::IsNearlyEqual(Facts.RemainingDistance2DCm, static_cast<float>(FVector::Dist2D(Feet, Destination)), 0.01f));
	TestTrue(TEXT("and dz is dest minus feet"),
		FMath::IsNearlyEqual(Facts.RemainingDzCm, static_cast<float>(Destination.Z - Feet.Z), 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcBodyMoveFactsAlreadyAtGoalTest,
	"Elysium.Arm.Visual.NpcBody.MoveFacts.AlreadyAtGoal", ElysiumNpcBodyMoveTests::Flags)
bool FElysiumNpcBodyMoveFactsAlreadyAtGoalTest::RunTest(const FString&)
{
	using namespace ElysiumNpcBodyMoveTests;
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	AElysiumNpcBody* Body = SpawnBodyWithController(Fixture);
	if (!TestNotNull(TEXT("body"), Body)) return false;
	AAIController* Controller = Cast<AAIController>(Body->GetController());
	if (!TestNotNull(TEXT("controller"), Controller)) return false;
	UPathFollowingComponent* Following = Controller->GetPathFollowingComponent();
	if (!TestNotNull(TEXT("follower"), Following)) return false;

	FElysiumNpcBodyMoveTestAccess::Bind(Body, Following);
	FElysiumNpcBodyMoveTestAccess::Begin(Body, MakeRequest(FVector::ZeroVector, 0.16f));
	FElysiumNpcBodyMoveTestAccess::FinishImmediately(Body, Following, EElysiumNpcMoveResultCode::Success,
		EElysiumNpcMoveResultFlags::AlreadyAtGoal);
	FElysiumNpcMoveFacts Facts;
	if (!TestTrue(TEXT("facts"), Body->SampleMoveFacts(Facts))) return false;
	TestFalse(TEXT("an immediate finish leaves nothing alive"), Facts.bRequestAlive);
	TestTrue(TEXT("it ended"), Facts.bRequestEnded);
	TestEqual(TEXT("in success"), Facts.ResultCode, EElysiumNpcMoveResultCode::Success);
	TestTrue(TEXT("flagged already at goal"),
		EnumHasAllFlags(Facts.ResultFlags, EElysiumNpcMoveResultFlags::AlreadyAtGoal));
	TestFalse(TEXT("with no path"), Facts.bHasPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcBodyMoveFactsEndsTest,
	"Elysium.Arm.Visual.NpcBody.MoveFacts.EndsAndNewRequest", ElysiumNpcBodyMoveTests::Flags)
bool FElysiumNpcBodyMoveFactsEndsTest::RunTest(const FString&)
{
	using namespace ElysiumNpcBodyMoveTests;
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	AElysiumNpcBody* Body = SpawnBodyWithController(Fixture);
	if (!TestNotNull(TEXT("body"), Body)) return false;
	AAIController* Controller = Cast<AAIController>(Body->GetController());
	if (!TestNotNull(TEXT("controller"), Controller)) return false;
	UPathFollowingComponent* Following = Controller->GetPathFollowingComponent();
	if (!TestNotNull(TEXT("follower"), Following)) return false;
	FElysiumNpcBodyMoveTestAccess::Bind(Body, Following);

	const FVector Destination(300, 400, 50);
	FVector Feet;
	float Yaw = 0.f;
	Body->SampleTransform(Feet, Yaw);

	// A live request: alive, on a path whose last corner is the destination.
	const FAIRequestID First = IssueAbstractRequest(Body, Following, MakeRequest(Destination, 0.16f));
	FElysiumNpcMoveFacts Facts;
	if (!TestTrue(TEXT("the follower took the request"), First.IsValid()
		&& Following->GetStatus() == EPathFollowingStatus::Moving)) return false;
	TestTrue(TEXT("facts"), Body->SampleMoveFacts(Facts));
	TestTrue(TEXT("the request is alive"), Facts.bRequestAlive);
	TestFalse(TEXT("and has not ended"), Facts.bRequestEnded);
	TestTrue(TEXT("the follower holds a path"), Facts.bHasPath);
	TestTrue(TEXT("a non-navmesh path's points are corners as authored"), Facts.bHasNextCorner);
	TestTrue(TEXT("its next corner is the destination"), Facts.NextCornerCm.Equals(Destination));
	TestTrue(TEXT("and that corner is the last"), Facts.bCurrentCornerIsLast);
	TestTrue(TEXT("the 2-D distance is measured to the exact destination"),
		FMath::IsNearlyEqual(Facts.RemainingDistance2DCm, static_cast<float>(FVector::Dist2D(Feet, Destination)), 0.01f));
	TestTrue(TEXT("and dz is dest minus feet"),
		FMath::IsNearlyEqual(Facts.RemainingDzCm, static_cast<float>(Destination.Z - Feet.Z), 0.01f));

	// A finish carrying some other request's id is not this request's end.
	FElysiumNpcBodyMoveTestAccess::Deliver(Body, First.GetID() + 50,
		FPathFollowingResult(EPathFollowingResult::Blocked, FPathFollowingResultFlags::None));
	Body->SampleMoveFacts(Facts);
	TestFalse(TEXT("a stale id does not end the current request"), Facts.bRequestEnded);

	// A blocked end, with a contact the capsule recorded: Blocked, the blocker latched, request dead.
	const FElysiumEntityHandle Blocker(7, 3);
	FElysiumNpcBodyMoveTestAccess::RecordContact(Body, Blocker);
	Following->OnPathFinished(EPathFollowingResult::Blocked, FPathFollowingResultFlags::None);
	Body->SampleMoveFacts(Facts);
	TestFalse(TEXT("a blocked request is no longer alive"), Facts.bRequestAlive);
	TestTrue(TEXT("it ended"), Facts.bRequestEnded);
	TestEqual(TEXT("as Blocked"), Facts.ResultCode, EElysiumNpcMoveResultCode::Blocked);
	TestTrue(TEXT("naming the NPC the capsule swept into"), Facts.BlockingEntity == Blocker);
	TestFalse(TEXT("the follower's path is gone with the request"), Facts.bHasPath);

	// A new request clears every ended fact and the recorded blocker.
	const FAIRequestID Second = IssueAbstractRequest(Body, Following, MakeRequest(Destination, 0.16f));
	if (!TestTrue(TEXT("the follower took the second request"), Second.IsValid())) return false;
	Body->SampleMoveFacts(Facts);
	TestTrue(TEXT("the second request is alive"), Facts.bRequestAlive);
	TestFalse(TEXT("the ended facts were cleared"), Facts.bRequestEnded);
	TestEqual(TEXT("with no result"), Facts.ResultCode, EElysiumNpcMoveResultCode::None);
	TestEqual(TEXT("no flags"), Facts.ResultFlags, EElysiumNpcMoveResultFlags::None);
	TestFalse(TEXT("and no blocker"), Facts.BlockingEntity.IsSet());

	// A success end: no blocker, no flags.
	Following->OnPathFinished(EPathFollowingResult::Success, FPathFollowingResultFlags::None);
	Body->SampleMoveFacts(Facts);
	TestTrue(TEXT("the second request ended"), Facts.bRequestEnded);
	TestEqual(TEXT("in success"), Facts.ResultCode, EElysiumNpcMoveResultCode::Success);
	TestFalse(TEXT("naming no blocker"), Facts.BlockingEntity.IsSet());

	// An aborted end carries the engine's abort details.
	const FAIRequestID Third = IssueAbstractRequest(Body, Following, MakeRequest(Destination, 0.16f));
	if (!TestTrue(TEXT("the follower took the third request"), Third.IsValid())) return false;
	Following->OnPathFinished(EPathFollowingResult::Aborted,
		FPathFollowingResultFlags::InvalidPath | FPathFollowingResultFlags::NewRequest);
	Body->SampleMoveFacts(Facts);
	TestEqual(TEXT("Aborted"), Facts.ResultCode, EElysiumNpcMoveResultCode::Aborted);
	TestTrue(TEXT("with the abort details"), EnumHasAllFlags(Facts.ResultFlags,
		EElysiumNpcMoveResultFlags::InvalidPath | EElysiumNpcMoveResultFlags::NewRequest));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcBodyMoveFactsBlockerAndPartialTest,
	"Elysium.Arm.Visual.NpcBody.MoveFacts.BlockerAndPartial", ElysiumNpcBodyMoveTests::Flags)
bool FElysiumNpcBodyMoveFactsBlockerAndPartialTest::RunTest(const FString&)
{
	using namespace ElysiumNpcBodyMoveTests;
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	AElysiumNpcBody* Body = SpawnBodyWithController(Fixture);
	if (!TestNotNull(TEXT("body"), Body)) return false;
	AElysiumPawn* Player = Fixture.SpawnPawn(FVector(2000, 0, 0));
	if (!TestNotNull(TEXT("player pawn"), Player)) return false;
	const FElysiumEntityHandle PlayerHandle(9, 4);
	Player->SetPlayerEntity(PlayerHandle);
	AAIController* Controller = Cast<AAIController>(Body->GetController());
	if (!TestNotNull(TEXT("controller"), Controller)) return false;
	UPathFollowingComponent* Following = Controller->GetPathFollowingComponent();
	if (!TestNotNull(TEXT("follower"), Following)) return false;
	FElysiumNpcBodyMoveTestAccess::Bind(Body, Following);

	const FVector Destination(300, 400, 50);
	if (!TestTrue(TEXT("the follower took the request"),
		IssueAbstractRequest(Body, Following, MakeRequest(Destination, 0.16f)).IsValid())) return false;
	FElysiumNpcBodyMoveTestAccess::SetMoveRequested(Body, true);
	UCapsuleComponent* Capsule = Body->GetCapsuleComponent();
	FElysiumNpcMoveFacts Facts;

	// Another actor walking into this body (`bSelfMoved` false) is not this body's obstruction.
	Body->NotifyHit(Capsule, Player, nullptr, false, FVector::ZeroVector, FVector::UpVector,
		FVector::ZeroVector, FHitResult());
	Body->SampleMoveFacts(Facts);
	TestFalse(TEXT("a toucher that walked into the body is not named"), Facts.BlockingEntity.IsSet());

	// This body's own sweep into the player is, and the player's entity is the handle named.
	Body->NotifyHit(Capsule, Player, nullptr, true, FVector::ZeroVector, FVector::UpVector,
		FVector::ZeroVector, FHitResult());
	Body->SampleMoveFacts(Facts);
	TestTrue(TEXT("the player this body walked into is named by its entity"),
		Facts.BlockingEntity == PlayerHandle);

	// A partial path's end is the follower's, not an arrival at the goal.
	FElysiumNpcBodyMoveTestAccess::SetPathPartial(Body, true);
	Body->SampleMoveFacts(Facts);
	TestTrue(TEXT("the partial path is a fact"), Facts.bPathPartial);
	Following->OnPathFinished(EPathFollowingResult::Success, FPathFollowingResultFlags::None);
	FVector Feet;
	float Yaw = 0.f;
	TestEqual(TEXT("a Success at a partial path's end is not Reached"), Body->Sample(Feet, Yaw),
		EElysiumNpcMoveStatus::Failed);

	// The same end on a full path is the named modernization's arrival.
	if (!TestTrue(TEXT("the follower took the second request"),
		IssueAbstractRequest(Body, Following, MakeRequest(Destination, 0.16f)).IsValid())) return false;
	FElysiumNpcBodyMoveTestAccess::SetMoveRequested(Body, true);
	Following->OnPathFinished(EPathFollowingResult::Success, FPathFollowingResultFlags::None);
	TestEqual(TEXT("a Success on a full path is Reached"), Body->Sample(Feet, Yaw),
		EElysiumNpcMoveStatus::Reached);
	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
