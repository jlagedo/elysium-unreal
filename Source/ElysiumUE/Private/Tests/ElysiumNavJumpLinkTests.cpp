#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AIController.h"
#include "AbstractNavData.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "ElysiumContentPaths.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavLinkCustomComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Tests/ElysiumPlayerWorldFixture.h"
#include "Visual/ElysiumNavJumpLink.h"
#include "Visual/ElysiumNpcBody.h"

namespace ElysiumNavJumpTests
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask
	| EAutomationTestFlags::ProductFilter;

void AddBox(UWorld* World, FVector Centre, FVector Extent)
{
	AActor* Actor = World->SpawnActor<AActor>();
	UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
	Actor->SetRootComponent(Box);
	Box->InitBoxExtent(Extent);
	Box->SetWorldLocation(Centre);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent();
	Actor->AddInstanceComponent(Box);
}

AElysiumNpcBody* AddBody(FPlayerWorldFixture& Fixture)
{
	AddBox(Fixture.World, FVector(300, 0, -50), FVector(1000, 500, 50));
	AElysiumNpcBody* Body = Fixture.World->SpawnActor<AElysiumNpcBody>();
	Body->InitializeAtFeet(FVector::ZeroVector, 0.f);
	Body->SetRuntimeReady(true);
	Body->SpawnDefaultController();
	return Body;
}

FVector Feet(AElysiumNpcBody* Body)
{
	return Body->GetActorLocation() - FVector(0, 0, Body->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNavJumpFlightTest,
	"Elysium.Substrate.NavJumpLink.Flight", ElysiumNavJumpTests::Flags)
bool FElysiumNavJumpFlightTest::RunTest(const FString&)
{
	using namespace ElysiumNavJumpTests;
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	AElysiumNpcBody* Body = AddBody(Fixture);
	AElysiumNavJumpLink* Link = Fixture.World->SpawnActor<AElysiumNavJumpLink>();
	if (!TestNotNull(TEXT("body"), Body) || !TestNotNull(TEXT("smart link"), Link)) return false;
	AAIController* Controller = Cast<AAIController>(Body->GetController());
	if (!TestNotNull(TEXT("native crowd controller"), Controller)) return false;
	UPathFollowingComponent* Following = Controller->GetPathFollowingComponent();
	const FVector Start = Feet(Body), Destination = Start + FVector(600, 0, 0);
	Link->ConfigureJumpLink(Start, Destination, 22, 8, 11);
	FNavPathSharedPtr Path = MakeShared<FAbstractNavigationPath, ESPMode::ThreadSafe>();
	Path->GetPathPoints().Emplace(Start);
	Path->GetPathPoints().Emplace(Destination);
	Path->MarkReady();
	FAIMoveRequest Request(Destination);
	Request.SetAcceptanceRadius(10.f);
	Request.SetUsePathfinding(false);
	Following->RequestMove(Request, Path);
	TestEqual(TEXT("the native follower owns an active request before the jump callback"),
		Following->GetStatus(), EPathFollowingStatus::Moving);
	TestTrue(TEXT("the capsule starts grounded"), Body->SampleNavigation().bGrounded);
	TestTrue(TEXT("only the smart link participates in navigation"), Link->PointLinks.IsEmpty()
		&& Link->bSmartLinkIsRelevant);
	// The actual native smart-link delegate used by path following. A content-free fixture
	// supplies the callback event; the baked-content case separately verifies the real actors.
	Link->GetSmartLinkComp()->OnLinkMoveStarted(Following, Destination);
	TestEqual(TEXT("launch preserves the native path request"),
		Following->GetStatus(), EPathFollowingStatus::Moving);
	TestEqual(TEXT("native callback publishes Jump"), Body->SampleNavigation().Type, EElysiumNpcNavType::Jump);
	TestFalse(TEXT("launch immediately publishes airborne"), Body->SampleNavigation().bGrounded);
	FVector SampledFeet;
	float SampledYaw = 0;
	TestEqual(TEXT("airborne link remains Moving, independent of goal proximity"),
		Body->Sample(SampledFeet, SampledYaw), EElysiumNpcMoveStatus::Moving);

	float Peak = 0;
	for (int32 Step = 0; Step < 400 && Body->SampleNavigation().Type == EElysiumNpcNavType::Jump; ++Step)
	{
		Body->GetCharacterMovement()->TickComponent(1.f / 120.f, LEVELTICK_All, nullptr);
		Peak = FMath::Max(Peak, float(Feet(Body).Z - Start.Z));
		Body->Tick(1.f / 120.f);
	}
	TestTrue(TEXT("CharacterMovement physically lifts and translates the capsule"),
		Peak > 100.f && FVector::Dist2D(Feet(Body), Destination) < 70.f);
	TestTrue(TEXT("capsule actually lands on collision"), Body->SampleNavigation().bGrounded);
	TestEqual(TEXT("landing restores Ground"), Body->SampleNavigation().Type, EElysiumNpcNavType::Ground);
	Link->GetSmartLinkComp()->OnLinkMoveFinished(Following);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNavJumpStopTest,
	"Elysium.Substrate.NavJumpLink.StopPreservesFlight", ElysiumNavJumpTests::Flags)
bool FElysiumNavJumpStopTest::RunTest(const FString&)
{
	using namespace ElysiumNavJumpTests;
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	AElysiumNpcBody* Body = AddBody(Fixture);
	AElysiumNavJumpLink* Link = Fixture.World->SpawnActor<AElysiumNavJumpLink>();
	if (!TestTrue(TEXT("begin real native flight"), Body->BeginNavigationJump(Link, Feet(Body) + FVector(600, 0, 0))))
		return false;
	const FVector Velocity = Body->GetCharacterMovement()->Velocity;
	Body->ClearNavigationGoal();
	FElysiumNpcNavigationSample Sample = Body->SampleNavigation();
	TestTrue(TEXT("clear goal preserves airborne Jump and velocity"),
		!Sample.bActiveGoal && !Sample.bGrounded && Sample.Type == EElysiumNpcNavType::Jump
		&& Sample.VelocityCmPerSecond.Equals(Velocity));
	TestTrue(TEXT("clear goal leaves physical integration active"), Body->GetCharacterMovement()->IsActive());
	// A collision can stop an airborne capsule. The engine adapter must expose this state
	// unchanged so requirement 13's recovered STOP_MOVING arm can fail on its next think.
	Body->GetCharacterMovement()->Velocity = FVector::ZeroVector;
	Body->Tick(1.f / 60.f);
	Sample = Body->SampleNavigation();
	TestTrue(TEXT("native motor exposes the stuck-on-top input to the task host"),
		Sample.Type == EElysiumNpcNavType::Jump && !Sample.bGrounded && Sample.VelocityCmPerSecond.IsNearlyZero());
	Body->SetNavigationType(EElysiumNpcNavType::Ground);
	Body->Tick(1.f / 60.f);
	TestEqual(TEXT("task's Ground write cancels link bookkeeping without rearming Jump"),
		Body->SampleNavigation().Type, EElysiumNpcNavType::Ground);
	TestTrue(TEXT("the task's type write does not freeze the falling capsule"), Body->GetCharacterMovement()->IsActive());
	Body->Stop();
	TestFalse(TEXT("hard stop deactivates movement"), Body->GetCharacterMovement()->IsActive());
	TestEqual(TEXT("hard stop leaves Ground"), Body->SampleNavigation().Type, EElysiumNpcNavType::Ground);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNavJumpUnavailableTest,
	"Elysium.Substrate.NavJumpLink.UnavailableAndTeardown", ElysiumNavJumpTests::Flags)
bool FElysiumNavJumpUnavailableTest::RunTest(const FString&)
{
	using namespace ElysiumNavJumpTests;
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	AElysiumNpcBody* Body = AddBody(Fixture);
	AElysiumNavJumpLink* Link = Fixture.World->SpawnActor<AElysiumNavJumpLink>();
	const FVector Destination = Feet(Body) + FVector(600, 0, 0);
	Body->SetFrozen(true);
	TestFalse(TEXT("frozen body cannot enter an authored jump"), Body->BeginNavigationJump(Link, Destination));
	TestEqual(TEXT("refusal keeps Ground"), Body->SampleNavigation().Type, EElysiumNpcNavType::Ground);
	Body->SetFrozen(false);
	TestTrue(TEXT("unfrozen body begins flight"), Body->BeginNavigationJump(Link, Destination));
	TestFalse(TEXT("a second callback cannot replace an in-flight jump"), Body->BeginNavigationJump(Link, Destination));
	Body->SetEnabled(false);
	TestEqual(TEXT("disable tears down special navigation state"), Body->SampleNavigation().Type, EElysiumNpcNavType::Ground);
	TestFalse(TEXT("disable tears down integration"), Body->GetCharacterMovement()->IsActive());
	Body->SetEnabled(true);
	AddBox(Fixture.World, FVector(300, 0, 150), FVector(30, 200, 150));
	TestFalse(TEXT("a blocked native capsule arc is refused before takeoff"), Body->BeginNavigationJump(Link, Destination));
	TestEqual(TEXT("failed move probe never publishes Jump"), Body->SampleNavigation().Type, EElysiumNpcNavType::Ground);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNavJumpBakedTutorialTest,
	"Elysium.Content.NavJumpLink.Tutorial", ElysiumNavJumpTests::Flags)
bool FElysiumNavJumpBakedTutorialTest::RunTest(const FString&)
{
	const FString Package = FElysiumContentPaths::BakedLevel(TEXT("sp_tutorial_1"));
	UWorld* Baked = LoadObject<UWorld>(nullptr, *(Package + TEXT(".sp_tutorial_1")));
	if (!TestNotNull(TEXT("tutorial baked level exists"), Baked)
		|| !TestNotNull(TEXT("tutorial persistent level exists"), Baked->PersistentLevel.Get())) return false;
	const TMap<int32, FIntPoint> Expected = {{22, {8, 11}}, {24, {8, 15}}, {30, {9, 42}},
		{88, {32, 36}}, {110, {49, 53}}, {115, {51, 53}}, {147, {70, 71}},
		{163, {76, 74}}, {218, {101, 106}}};
	TSet<int32> Found;
	for (AActor* Actor : Baked->PersistentLevel->Actors)
	{
		AElysiumNavJumpLink* Link = Cast<AElysiumNavJumpLink>(Actor);
		if (!Link) continue;
		const FIntPoint* Pair = Expected.Find(Link->SourceLinkIndex);
		TestTrue(TEXT("the baked connection names the decoded AIN endpoints"), Pair
			&& Pair->X == Link->SourceNode && Pair->Y == Link->DestinationNode);
		TestFalse(TEXT("no duplicate AIN jump actors"), Found.Contains(Link->SourceLinkIndex));
		Found.Add(Link->SourceLinkIndex);
		TestTrue(TEXT("baked link uses the native traversal callback path"),
			Link->PointLinks.IsEmpty() && Link->bSmartLinkIsRelevant && Link->IsSmartLinkEnabled());
		FVector Start, End;
		ENavLinkDirection::Type Direction;
		Link->GetSmartLinkComp()->GetLinkData(Start, End, Direction);
		TestTrue(TEXT("connection retains both directions and distinct native endpoints"),
			Direction == ENavLinkDirection::BothWays && !Start.Equals(End));
	}
	TestEqual(TEXT("all nine tutorial human AIN jumps survive baking and reloading"), Found.Num(), Expected.Num());
	return true;
}

#endif
