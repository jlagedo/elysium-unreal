#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Visual/ElysiumNpcVisual.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Tests/AutomationCommon.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWieldOwnershipTest,
	"Elysium.Substrate.WieldOwnership",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FElysiumWieldOwnershipTest::RunTest(const FString&)
{
	FTestWorldWrapper World;
	if (!World.CreateTestWorld(EWorldType::Game) || !World.BeginPlayInTestWorld())
	{
		World.ForwardErrorMessages(this);
		return false;
	}
	AActor* Owner = World.GetTestWorld()->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("shared visual owner"), Owner)) return false;
	const auto Component = [Owner](const TCHAR* Name, USkeletalMeshComponent* Parent, bool bWeapon)
	{
		USkeletalMeshComponent* Result = NewObject<USkeletalMeshComponent>(Owner, Name);
		if (Parent) Result->SetupAttachment(Parent);
		if (bWeapon) Result->ComponentTags.Add(ElysiumNpcVisual::WieldComponentTag());
		Owner->AddInstanceComponent(Result);
		Result->RegisterComponent();
		return Result;
	};
	USkeletalMeshComponent* BodyA = Component(TEXT("BodyA"), nullptr, false);
	USkeletalMeshComponent* BodyB = Component(TEXT("BodyB"), nullptr, false);
	USkeletalMeshComponent* RigidA = Component(TEXT("RigidA"), BodyA, true);
	USkeletalMeshComponent* RigidB = Component(TEXT("RigidB"), BodyB, true);
	USkeletalMeshComponent* Orphan = Component(TEXT("Orphan"), nullptr, true);
	TestEqual(TEXT("rigid weapon found without a leader pose"), ElysiumNpcVisual::FindWieldModel(BodyA), RigidA);
	ElysiumNpcVisual::ClearWieldModel(BodyA);
	TestNull(TEXT("cleared body has no weapon"), ElysiumNpcVisual::FindWieldModel(BodyA));
	TestEqual(TEXT("another body's rigid weapon survives"), ElysiumNpcVisual::FindWieldModel(BodyB), RigidB);
	TestFalse(TEXT("orphan cleaned"), Orphan->IsRegistered());
	USkeletalMeshComponent* SkeletalA = Component(TEXT("SkeletalA"), BodyA, true);
	SkeletalA->SetLeaderPoseComponent(BodyA);
	TestEqual(TEXT("leader-bound weapon found through the same attachment ownership"),
		ElysiumNpcVisual::FindWieldModel(BodyA), SkeletalA);
	ElysiumNpcVisual::ClearWieldModel(BodyA);
	TestTrue(TEXT("other body remains registered after both sweeps"), RigidB->IsRegistered());
	return true;
}
#endif
