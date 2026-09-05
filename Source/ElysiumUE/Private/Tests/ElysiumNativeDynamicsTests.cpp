#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ElysiumCharacterProvenance.h"
#include "ElysiumDynamicsData.h"
#include "Visual/ElysiumNativeAnimationData.h"
#include "Visual/ElysiumBodyAnimInstance.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumHairDynamicsConfig.h"
#include "Visual/ElysiumNpcVisual.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNativeDynamicsTest,"Elysium.Content.NativeDynamics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumNativeDynamicsTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> Game(NewObject<UGameInstance>());
	TStrongObjectPtr<UElysiumNativeAnimationData> Native(NewObject<UElysiumNativeAnimationData>(Game.Get()));
	const TArray<FString> Models{TEXT("jeanette"),TEXT("malkavian_female_armor_0"),TEXT("damsel")};
	FString Error;
	auto Load=Native->PrepareMany(Models,Error);
	if (!TestTrue(TEXT("native dynamics bodies prepare"),Load.IsValid())) { AddError(Error); return false; }
	Load->WaitUntilComplete();
	if (!TestTrue(TEXT("native request finishes"),Native->FinishPreparation(Load,Error))) { AddError(Error); return false; }
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
	{ TestWorld.ForwardErrorMessages(this); return false; }
	int32 Admitted=0,Unadmitted=0;
	for (const auto& Model : Models)
	{
		auto* Mesh=Native->Mesh(Model,Error);
		if (!TestNotNull(TEXT("native dynamics mesh is resident"),Mesh)) continue;
		const auto* Metadata=UElysiumCharacterProvenance::Find(Mesh);
		if (!TestNotNull(TEXT("mesh has provenance"),Metadata)
			|| !TestNotNull(TEXT("dynamics loads through the mesh reference"),Metadata->Dynamics.Get())) continue;
		TestEqual(TEXT("dynamics belongs to this mesh's source unit"),Metadata->Dynamics->AssetId,Metadata->AssetId);
		TestTrue(TEXT("source declarations are available at runtime"),!Metadata->Dynamics->Records.IsEmpty());
		auto* Owner=TestWorld.GetTestWorld()->SpawnActor<AActor>();
		if (!TestNotNull(TEXT("dynamic body owner spawns"),Owner)) continue;
		auto* Component=NewObject<USkeletalMeshComponent>(Owner);
		Component->SetSkeletalMeshAsset(Mesh);
		Component->SetAnimationMode(EAnimationMode::AnimationBlueprint);
		Component->SetAnimInstanceClass(UElysiumBipedAnimInstance::StaticClass());
		Owner->SetRootComponent(Component); Component->RegisterComponent();
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		auto* Instance=Cast<UElysiumBodyAnimInstance>(Component->GetAnimInstance());
		if (!TestNotNull(TEXT("native dynamics host installs"),Instance)) continue;
		const auto* Authored=UElysiumHairDynamicsConfig::FindModel(Metadata->AssetId);
		const bool Installed=ElysiumNpcVisual::InstallHairDynamics(Component,Metadata->AssetId);
		if (Authored && !Authored->Chains.IsEmpty())
		{
			++Admitted;
			TestTrue(TEXT("authored tuning admits the native source recipe"),Installed);
			TestEqual(TEXT("only authored chains are installed"),Instance->GetHairDynamicsChainCount(),Authored->Chains.Num());
		}
		else
		{
			++Unadmitted;
			TestFalse(TEXT("generated data does not widen the install gate"),Installed);
			TestEqual(TEXT("unadmitted generated chains remain inactive"),Instance->GetHairDynamicsChainCount(),0);
		}
		TestEqual(TEXT("cooked body recipes are not installed"),Instance->GetHairDynamicsBodyCount(),0);
	}
	TestTrue(TEXT("real authored chains exercised the positive gate"),Admitted>=2);
	TestTrue(TEXT("a real unadmitted body exercised the negative gate"),Unadmitted>=1);
	return true;
}
#endif
