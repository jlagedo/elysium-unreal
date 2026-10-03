// 0019 story 6, lane S: the motor seam's floor facts, move-ignore filter and route-held fact
// (`IElysiumNpcMotor::SampleFloor` / `SetMoveIgnore` / `HasPath`), driven on the live body in the
// transient game world the body's move-facts cases use, and the recording motor's defaults.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AIController.h"
#include "AITypes.h"
#include "AbstractNavData.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumWorldServices.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "Substrate/ElysiumNpcKernelTunables.h"
#include "Tests/ElysiumPlayerWorldFixture.h"
#include "Tests/ElysiumTestServices.h"
#include "Visual/ElysiumNpcBody.h"

namespace ElysiumNpcMotorSeamTests
{
constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask
	| EAutomationTestFlags::ProductFilter;

// A static-world floor whose top face is z = 0, as the geometry cases build theirs.
UBoxComponent* AddFloor(UWorld* World)
{
	AActor* Actor = World->SpawnActor<AActor>();
	if (Actor == nullptr) return nullptr;
	UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
	Actor->SetRootComponent(Box);
	Box->InitBoxExtent(FVector(1000, 1000, 50));
	Box->SetWorldLocation(FVector(0, 0, -50));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionObjectType(ECC_WorldStatic);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent();
	Actor->AddInstanceComponent(Box);
	return Box;
}

AElysiumNpcBody* SpawnBody(FPlayerWorldFixture& Fixture, const FVector& Feet,
	const FElysiumEntityHandle& Owner = FElysiumEntityHandle())
{
	AElysiumNpcBody* Body = Fixture.World->SpawnActor<AElysiumNpcBody>();
	if (Body == nullptr) return nullptr;
	if (Owner.IsSet())
	{
		Body->SetOwningEntity(nullptr, Owner);
	}
	Body->InitializeAtFeet(Feet, 0.f);
	Body->SetRuntimeReady(true);
	return Body;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMotorSeamFloorTest,
	"Elysium.Arm.Visual.NpcBody.MotorSeam.Floor", ElysiumNpcMotorSeamTests::Flags)
bool FElysiumNpcMotorSeamFloorTest::RunTest(const FString&)
{
	using namespace ElysiumNpcMotorSeamTests;
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	if (!TestNotNull(TEXT("floor"), AddFloor(Fixture.World))) return false;
	AElysiumNpcBody* Body = SpawnBody(Fixture, FVector::ZeroVector);
	if (!TestNotNull(TEXT("body"), Body)) return false;
	const UCharacterMovementComponent* Movement = Body->GetCharacterMovement();
	if (!TestNotNull(TEXT("movement"), Movement)) return false;

	// On the floor: a walkable static-world surface right under the hull, inside retail's 4-unit reach.
	FElysiumNpcFloorFacts Facts;
	if (!TestTrue(TEXT("a body with a movement component answers"), Body->SampleFloor(Facts))) return false;
	TestTrue(TEXT("standing on the floor is on ground"), Facts.bOnGround);
	TestTrue(TEXT("and the floor is walkable"), Facts.bWalkable);
	TestTrue(TEXT("the floor normal is up"), Facts.FloorNormal.Equals(FVector::UpVector, 1e-3));
	TestTrue(TEXT("the gap is inside retail's 4.0-unit trace"),
		Facts.FloorDistanceCm >= 0.f && Facts.FloorDistanceCm <= 4.0f * ElysiumMove::U);
	TestFalse(TEXT("the static world is no entity (retail's worldspawn)"), Facts.GroundEntityHandle.IsSet());
	TestEqual(TEXT("the step height is the mover's"), Facts.MaxStepHeightCm, Movement->MaxStepHeight);
	TestTrue(TEXT("which is retail's 18 units"), FMath::IsNearlyEqual(Facts.MaxStepHeightCm,
		ElysiumNpcTunables::StepHeightBase * ElysiumMove::U, 0.01f));
	TestTrue(TEXT("the standable normal is retail's 0.7"),
		FMath::IsNearlyEqual(Facts.WalkableFloorZ, ElysiumMove::StandableZ, 1e-4f));
	TestTrue(TEXT("and the slope is its angle"), FMath::IsNearlyEqual(Facts.WalkableFloorAngleDegrees,
		FMath::RadiansToDegrees(FMath::Acos(ElysiumMove::StandableZ)), 0.01f));

	// 3 m up: nothing under the hull within the probe's reach. The motor still answers, and the
	// body's own numbers still come with it.
	Body->Teleport(FVector(0, 0, 300), 0.f);
	Facts = FElysiumNpcFloorFacts();
	if (!TestTrue(TEXT("a body in the air answers too"), Body->SampleFloor(Facts))) return false;
	TestFalse(TEXT("3 m up is not on ground"), Facts.bOnGround);
	TestFalse(TEXT("nor walkable"), Facts.bWalkable);
	TestFalse(TEXT("and names no ground entity"), Facts.GroundEntityHandle.IsSet());
	TestEqual(TEXT("with no gap measured"), Facts.FloorDistanceCm, 0.f);
	TestEqual(TEXT("the step height still comes"), Facts.MaxStepHeightCm, Movement->MaxStepHeight);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMotorSeamMoveIgnoreTest,
	"Elysium.Arm.Visual.NpcBody.MotorSeam.MoveIgnore", ElysiumNpcMotorSeamTests::Flags)
bool FElysiumNpcMotorSeamMoveIgnoreTest::RunTest(const FString&)
{
	using namespace ElysiumNpcMotorSeamTests;
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	const FElysiumEntityHandle HandleA(1, 1);
	const FElysiumEntityHandle HandleB(2, 1);
	AElysiumNpcBody* A = SpawnBody(Fixture, FVector::ZeroVector, HandleA);
	AElysiumNpcBody* B = SpawnBody(Fixture, FVector(200, 0, 0), HandleB);
	if (!TestNotNull(TEXT("body A"), A) || !TestNotNull(TEXT("body B"), B)) return false;
	const UCapsuleComponent* Capsule = A->GetCapsuleComponent();
	if (!TestNotNull(TEXT("A's capsule"), Capsule)) return false;

	TestEqual(TEXT("nothing is ignored at spawn"), Capsule->GetMoveIgnoreActors().Num(), 0);
	A->SetMoveIgnore(HandleB, true);
	TestTrue(TEXT("set: A's capsule skips B's body when it moves"),
		Capsule->GetMoveIgnoreActors().Contains(B));
	TestFalse(TEXT("B's own capsule is left alone"),
		B->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(A));
	A->SetMoveIgnore(HandleB, false);
	TestFalse(TEXT("cleared: A collides with B again"), Capsule->GetMoveIgnoreActors().Contains(B));
	A->SetMoveIgnore(FElysiumEntityHandle(9, 1), true);
	TestEqual(TEXT("an entity with no collision in this world is a no-op"),
		Capsule->GetMoveIgnoreActors().Num() + Capsule->GetMoveIgnoreComponents().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMotorSeamHasPathTest,
	"Elysium.Arm.Visual.NpcBody.MotorSeam.HasPath", ElysiumNpcMotorSeamTests::Flags)
bool FElysiumNpcMotorSeamHasPathTest::RunTest(const FString&)
{
	using namespace ElysiumNpcMotorSeamTests;
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	AElysiumNpcBody* Body = SpawnBody(Fixture, FVector::ZeroVector);
	if (!TestNotNull(TEXT("body"), Body)) return false;
	TestFalse(TEXT("no controller, no follower, no path"), Body->HasPath());

	Body->SpawnDefaultController();
	AAIController* Controller = Cast<AAIController>(Body->GetController());
	if (!TestNotNull(TEXT("controller"), Controller)) return false;
	UPathFollowingComponent* Following = Controller->GetPathFollowingComponent();
	if (!TestNotNull(TEXT("follower"), Following)) return false;
	TestFalse(TEXT("an idle follower holds no path"), Body->HasPath());

	// A two-point abstract path (no navmesh), as the move-facts cases hand the follower theirs.
	FVector Feet;
	float Yaw = 0.f;
	Body->SampleTransform(Feet, Yaw);
	const FVector Destination(300, 400, 0);
	FNavPathSharedPtr Path = MakeShared<FAbstractNavigationPath, ESPMode::ThreadSafe>();
	Path->GetPathPoints().Emplace(Feet);
	Path->GetPathPoints().Emplace(Destination);
	Path->MarkReady();
	FAIMoveRequest Move(Destination);
	Move.SetAcceptanceRadius(10.f);
	Move.SetUsePathfinding(false);
	const FAIRequestID Id = Following->RequestMove(Move, Path);
	if (!TestTrue(TEXT("the follower took the request"), Id.IsValid())) return false;
	TestTrue(TEXT("a request in flight holds a path"), Body->HasPath());

	Body->Stop();
	TestFalse(TEXT("stopped: the follower is idle and the path is gone"), Body->HasPath());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMotorSeamTurnHullTest,
	"Elysium.Arm.Visual.NpcBody.MotorSeam.TurnRateHullFacing", ElysiumNpcMotorSeamTests::Flags)
bool FElysiumNpcMotorSeamTurnHullTest::RunTest(const FString&)
{
	// 0019/6 lane R3: the turn rate the kernel states, the hull resize and the facing target.
	using namespace ElysiumNpcMotorSeamTests;
	FPlayerWorldFixture Fixture;
	if (!Fixture.CreateWorld(*this)) return false;
	AElysiumNpcBody* Body = SpawnBody(Fixture, FVector::ZeroVector, FElysiumEntityHandle(1, 1));
	if (!TestNotNull(TEXT("body"), Body)) return false;
	UCharacterMovementComponent* Movement = Body->GetCharacterMovement();
	UCapsuleComponent* Capsule = Body->GetCapsuleComponent();
	if (!TestNotNull(TEXT("mover"), Movement) || !TestNotNull(TEXT("capsule"), Capsule)) return false;

	const double RateBefore = Movement->RotationRate.Yaw;
	Body->Face(90.f);
	TestEqual(TEXT("no rate stated: the body keeps its own"), Movement->RotationRate.Yaw, RateBefore);
	Body->Face(90.f, 450.f);
	TestEqual(TEXT("a stated rate is the mover's before it turns"), Movement->RotationRate.Yaw, 450.0);

	const FVector FeetBefore = Body->GetActorLocation() - FVector(0, 0, Capsule->GetUnscaledCapsuleHalfHeight());
	Body->SetHullSize(FVector(-20, -20, 0), FVector(20, 20, 100));
	TestEqual(TEXT("radius from the box's X extent"), Capsule->GetUnscaledCapsuleRadius(), 20.f);
	TestEqual(TEXT("half-height from its Z span"), Capsule->GetUnscaledCapsuleHalfHeight(), 50.f);
	const FVector FeetAfter = Body->GetActorLocation() - FVector(0, 0, Capsule->GetUnscaledCapsuleHalfHeight());
	TestTrue(TEXT("the feet stay where they stood"), FeetAfter.Equals(FeetBefore, 0.01));

	Body->SetFacingTarget(FVector(0, 500, 0));
	TestTrue(TEXT("no controller yet: still facing along the path"), Movement->bOrientRotationToMovement);
	Body->SetFacingTarget(TOptional<FVector>());
	TestTrue(TEXT("cleared: along the path"), Movement->bOrientRotationToMovement
		&& !Movement->bUseControllerDesiredRotation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNpcMotorSeamRecordingTest,
	"Elysium.Arm.NpcMotorSeam.Recording", ElysiumNpcMotorSeamTests::Flags)
bool FElysiumNpcMotorSeamRecordingTest::RunTest(const FString&)
{
	// The double wave 3's kernel cases drive: retail's own numbers by default, a case states the rest.
	TArray<FString> Calls;
	FElysiumRecordingNpcMotor Motor;
	Motor.Calls = &Calls;
	const IElysiumNpcMotor& Seam = Motor;

	FElysiumNpcFloorFacts Facts;
	TestTrue(TEXT("the double reports a floor by default"), Seam.SampleFloor(Facts));
	TestTrue(TEXT("standing on the world's floor"), Facts.bOnGround && Facts.bWalkable
		&& !Facts.GroundEntityHandle.IsSet());
	TestTrue(TEXT("at retail's step height, _DAT_10453b94"), FMath::IsNearlyEqual(Facts.MaxStepHeightCm,
		ElysiumNpcTunables::StepHeightBase * ElysiumMove::U, 0.01f));
	TestTrue(TEXT("and retail's standable normal"),
		FMath::IsNearlyEqual(Facts.WalkableFloorZ, ElysiumMove::StandableZ, 1e-4f));
	Motor.bReportsFloor = false;
	Facts.FloorDistanceCm = 7.f;
	TestFalse(TEXT("headless: no floor facts"), Seam.SampleFloor(Facts));
	TestEqual(TEXT("and the caller's record is untouched"), Facts.FloorDistanceCm, 7.f);
	TestEqual(TEXT("samples are counted, not recorded"), Motor.FloorSamples, 2);

	const FElysiumEntityHandle Other(4, 2);
	Motor.SetMoveIgnore(Other, true);
	TestTrue(TEXT("the ignore set holds it"), Motor.MoveIgnored.Contains(Other));
	Motor.SetMoveIgnore(Other, false);
	TestEqual(TEXT("and lets it go"), Motor.MoveIgnored.Num(), 0);
	if (TestEqual(TEXT("both calls recorded"), Calls.Num(), 2))
	{
		TestEqual(TEXT("the set"), Calls[0], FString(TEXT("NpcMotor SetMoveIgnore 4 1")));
		TestEqual(TEXT("the clear"), Calls[1], FString(TEXT("NpcMotor SetMoveIgnore 4 0")));
	}

	TestFalse(TEXT("no request, no path"), Seam.HasPath());
	Motor.bMoving = true;
	TestTrue(TEXT("a request in flight holds one"), Seam.HasPath());
	Motor.HasPathOverride = false;
	TestFalse(TEXT("a case can state the follower dropped it"), Seam.HasPath());
	return true;
}

#endif
