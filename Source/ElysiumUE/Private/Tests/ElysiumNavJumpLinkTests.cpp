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
	"Elysium.Arm.NavJumpLink.Flight", ElysiumNavJumpTests::Flags)
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
	"Elysium.Arm.NavJumpLink.StopPreservesFlight", ElysiumNavJumpTests::Flags)
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
	"Elysium.Arm.NavJumpLink.UnavailableAndTeardown", ElysiumNavJumpTests::Flags)
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

namespace ElysiumNavJumpTests
{
/** What the bake recorded on one hull, summed over a level's jump-link actors. */
struct FHullCounts
{
	int32 Entries = 0;
	int32 JumpOnly = 0;
	int32 Usable = 0;
	int32 LegalForward = 0;
	int32 LegalBack = 0;
};

/**
 * Every baked jump link in `Level` is a RECORD and never a route (0018/7): retail's `0x102ff960`
 * step 2 refuses every jump-only word because no NPC holds `bits_CAP_MOVE_JUMP`. So each actor
 * must carry no simple link, no agent and a disabled smart link, keep both directions as data,
 * and hold one verdict per used hull. Returns the per-hull sums for the caller to pin.
 */
TMap<int32, FHullCounts> CheckRecords(FAutomationTestBase& Test, ULevel* Level,
	const TArray<int32>& UsedHulls, TSet<int32>& OutFound)
{
	TMap<int32, FHullCounts> Counts;
	for (AActor* Actor : Level->Actors)
	{
		AElysiumNavJumpLink* Link = Cast<AElysiumNavJumpLink>(Actor);
		if (!Link) continue;
		Test.TestFalse(TEXT("no duplicate AIN jump actors"), OutFound.Contains(Link->SourceLinkIndex));
		OutFound.Add(Link->SourceLinkIndex);
		Test.TestTrue(TEXT("no simple link rides beside the record"),
			Link->PointLinks.IsEmpty() && Link->SegmentLinks.IsEmpty());
		Test.TestFalse(TEXT("the smart link is disabled"), Link->IsSmartLinkEnabled());
		Test.TestEqual(TEXT("the smart link supports no agent"), Link->SupportedAgentBits(), 0);
		Test.TestFalse(TEXT("nothing about the record is traversable"), Link->IsTraversable());
		Test.TestTrue(TEXT("the pair stays bidirectional as data"), Link->bBidirectional);
		FVector Start, End;
		ENavLinkDirection::Type Direction;
		Link->GetSmartLinkComp()->GetLinkData(Start, End, Direction);
		Test.TestTrue(TEXT("the record keeps both directions and distinct endpoints"),
			Direction == ENavLinkDirection::BothWays && !Start.Equals(End));
		Test.TestEqual(TEXT("one verdict per used hull"), Link->HullVerdicts.Num(), UsedHulls.Num());
		for (int32 Index = 0; Index < Link->HullVerdicts.Num(); ++Index)
		{
			const FElysiumNavJumpHullVerdict& Verdict = Link->HullVerdicts[Index];
			Test.TestEqual(TEXT("verdicts follow UsedHullBits order"), Verdict.Hull,
				UsedHulls.IsValidIndex(Index) ? UsedHulls[Index] : INDEX_NONE);
			Test.TestFalse(TEXT("no NPC holds the jump capability"), Verdict.bCapabilityUsable);
			FHullCounts& Row = Counts.FindOrAdd(Verdict.Hull);
			++Row.Entries;
			Row.JumpOnly += Verdict.bJumpOnly ? 1 : 0;
			Row.Usable += Verdict.bCapabilityUsable ? 1 : 0;
			Row.LegalForward += Verdict.bLegalForward ? 1 : 0;
			Row.LegalBack += Verdict.bLegalBack ? 1 : 0;
		}
	}
	return Counts;
}

void PinHull(FAutomationTestBase& Test, const TMap<int32, FHullCounts>& Counts, int32 Hull,
	int32 Entries, int32 JumpOnly, int32 LegalForward, int32 LegalBack)
{
	const FHullCounts* Row = Counts.Find(Hull);
	if (!Test.TestNotNull(*FString::Printf(TEXT("hull %d has verdicts"), Hull), Row)) return;
	Test.TestEqual(*FString::Printf(TEXT("hull %d: a verdict on every record"), Hull), Row->Entries, Entries);
	Test.TestEqual(*FString::Printf(TEXT("hull %d: jump-only records"), Hull), Row->JumpOnly, JumpOnly);
	Test.TestEqual(*FString::Printf(TEXT("hull %d: usable by any NPC"), Hull), Row->Usable, 0);
	Test.TestEqual(*FString::Printf(TEXT("hull %d: geometry-legal src->dst"), Hull), Row->LegalForward, LegalForward);
	Test.TestEqual(*FString::Printf(TEXT("hull %d: geometry-legal dst->src"), Hull), Row->LegalBack, LegalBack);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNavJumpBakedTutorialTest,
	"Elysium.Content.NavJumpLink.Tutorial", ElysiumNavJumpTests::Flags)
bool FElysiumNavJumpBakedTutorialTest::RunTest(const FString&)
{
	using namespace ElysiumNavJumpTests;
	const FString Package = FElysiumContentPaths::BakedLevel(TEXT("sp_tutorial_1"));
	UWorld* Baked = LoadObject<UWorld>(nullptr, *(Package + TEXT(".sp_tutorial_1")));
	if (!TestNotNull(TEXT("tutorial baked level exists"), Baked)
		|| !TestNotNull(TEXT("tutorial persistent level exists"), Baked->PersistentLevel.Get())) return false;
	// The PATCH's graph -- 203 nodes, 429 links -- which is what the patched install runs and what
	// this map is baked from (0018 story 3). The first nine pairs are the base game's packed graph
	// unchanged; the rest are jumps on the 87 nodes the patch adds.
	const TMap<int32, FIntPoint> Expected = {{22, {8, 11}}, {24, {8, 15}}, {30, {9, 42}},
		{88, {32, 36}}, {110, {49, 53}}, {115, {51, 53}}, {147, {70, 71}},
		{163, {76, 74}}, {218, {101, 106}}, {237, {117, 136}}, {238, {117, 138}},
		{256, {122, 134}}, {261, {123, 136}}, {278, {131, 133}}, {279, {131, 136}},
		{281, {132, 134}}, {283, {133, 134}}, {285, {134, 136}}, {287, {135, 132}},
		{292, {137, 132}}, {390, {180, 185}}, {393, {181, 143}}, {399, {185, 186}},
		{402, {186, 187}}, {407, {189, 187}}};
	TSet<int32> Found;
	for (AActor* Actor : Baked->PersistentLevel->Actors)
	{
		AElysiumNavJumpLink* Link = Cast<AElysiumNavJumpLink>(Actor);
		if (!Link) continue;
		const FIntPoint* Pair = Expected.Find(Link->SourceLinkIndex);
		TestTrue(TEXT("the baked connection names the decoded AIN endpoints"), Pair
			&& Pair->X == Link->SourceNode && Pair->Y == Link->DestinationNode);
	}
	// UsedHullBits 0x80001: human (0) and rat (19), in that order.
	const TMap<int32, FHullCounts> Counts = CheckRecords(*this, Baked->PersistentLevel, {0, 19}, Found);
	TestEqual(TEXT("every tutorial human AIN jump is recorded once"), Found.Num(), Expected.Num());
	// The graph holds 25 human and 36 rat jump-only links (`test_map_jump_links.py`); the records
	// are the 25 human pairs, 17 of which are jump-only for the rat too. `IsJumpLegalGeometry`
	// with slot 521's 80 / 250 / 160 passes 8 human and 5 rat of them each way. Of the rest, one
	// pair per hull is refused src -> dst by the distance arm and dst -> src by the rise arm (80.1,
	// tested first); every other pair is refused both ways by the distance arm (160.1). None of it
	// routes: step 2 refuses all of them first.
	PinHull(*this, Counts, 0, 25, 25, 8, 8);
	PinHull(*this, Counts, 19, 25, 17, 5, 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNavJumpBakedHubTest,
	"Elysium.Content.NavJumpLink.Hub", ElysiumNavJumpTests::Flags)
bool FElysiumNavJumpBakedHubTest::RunTest(const FString&)
{
	using namespace ElysiumNavJumpTests;
	const FString Package = FElysiumContentPaths::BakedLevel(TEXT("sm_hub_1"));
	UWorld* Baked = LoadObject<UWorld>(nullptr, *(Package + TEXT(".sm_hub_1")));
	if (!TestNotNull(TEXT("hub baked level exists"), Baked)
		|| !TestNotNull(TEXT("hub persistent level exists"), Baked->PersistentLevel.Get())) return false;
	TSet<int32> Found;
	const TMap<int32, FHullCounts> Counts = CheckRecords(*this, Baked->PersistentLevel, {0, 19}, Found);
	TestEqual(TEXT("every hub human AIN jump is recorded once"), Found.Num(), 117);
	// Link 731 (160 <-> 151), the patrol cop's NavMesh-only shortcut before 0018/7: retail walks
	// s2 -> 151 -> s3 over links 691 and 1581 and never takes it. It stays as a disabled record.
	TestTrue(TEXT("link 731 is recorded"), Found.Contains(731));
	// Graph: 117 human, 103 rat jump-only links; the 117 records are jump-only for the rat on 68.
	// Every refused direction here is the distance arm's (160.1).
	PinHull(*this, Counts, 0, 117, 117, 22, 22);
	PinHull(*this, Counts, 19, 117, 68, 13, 13);
	return true;
}

#endif
