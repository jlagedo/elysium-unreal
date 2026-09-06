#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ElysiumCharacterProvenance.h"
#include "ElysiumFog.h"                // ElysiumLightStyle::SlotBrightness
#include "Visual/ElysiumNativeAnimationData.h"
#include "Visual/ElysiumNpcVisual.h"
#include "ChaosClothAsset/ClothComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Tests/AutomationCommon.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNativeClothTest,"Elysium.Content.NativeCloth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumNativeClothTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> Game(NewObject<UGameInstance>());
	TStrongObjectPtr<UElysiumNativeAnimationData> Native(NewObject<UElysiumNativeAnimationData>(Game.Get()));
	const TArray<FString> Models{TEXT("jeanette"),TEXT("tremere_female_armor_3"),
		TEXT("vtmb:model:scenery/structural/chinese/lantern")};
	FString Error;
	auto Load=Native->PrepareMany(Models,Error);
	if (!TestTrue(TEXT("cloth owners prepare"),Load.IsValid())) { AddError(Error); return false; }
	Load->WaitUntilComplete();
	if (!TestTrue(TEXT("cloth request finishes"),Native->FinishPreparation(Load,Error))) { AddError(Error); return false; }
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game) || !TestWorld.BeginPlayInTestWorld())
	{ TestWorld.ForwardErrorMessages(this); return false; }
	int32 MultiGarmentBodies=0;
	for (const auto& Model : Models)
	{
		auto* Mesh=Native->Mesh(Model,Error);
		if (!TestNotNull(TEXT("cloth mesh is resident"),Mesh)) continue;
		const auto* Data=UElysiumCharacterProvenance::Find(Mesh);
		if (!TestNotNull(TEXT("cloth owner has native metadata"),Data)) continue;
		if (!TestTrue(TEXT("cloth owner has declared garments"),Data->SourceGarmentCount>0)) continue;
		TestEqual(TEXT("every declared garment is referenced"),Data->ClothAssets.Num(),Data->SourceGarmentCount);
		MultiGarmentBodies+=Data->SourceGarmentCount>1?1:0;
		auto* Owner=TestWorld.GetTestWorld()->SpawnActor<AActor>();
		if (!TestNotNull(TEXT("cloth host actor spawns"),Owner)) continue;
		auto* Body=NewObject<USkeletalMeshComponent>(Owner);
		Body->SetSkeletalMeshAsset(Mesh);
		Owner->SetRootComponent(Body); Body->RegisterComponent();
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TestNotNull(TEXT("native garment installation uses the prepared assets"),ElysiumNpcVisual::InstallGarment(Body,Data->AssetId));
		TArray<UChaosClothComponent*> Garments; Owner->GetComponents(Garments);
		TestEqual(TEXT("all garments attach to the host"),Garments.Num(),Data->SourceGarmentCount);
		for (const auto* Garment : Garments)
		{
			TestTrue(TEXT("garment follows the correct skeletal body"),Garment->LeaderPoseComponent.Get()==Body);
			TestTrue(TEXT("garment participates in actor instance serialization"),Owner->GetInstanceComponents().Contains(Garment));
			// R7.4 (G6): the skinned masters multiply base colour by CPD slot 6; an installed garment
			// reads full brightness live and serialized, on the bake's path and the runtime's alike.
			TestEqual(TEXT("garment light-style slot is stamped live"),
				Garment->GetCustomPrimitiveData().Data.IsValidIndex(ElysiumLightStyle::SlotBrightness)
					? Garment->GetCustomPrimitiveData().Data[ElysiumLightStyle::SlotBrightness] : 0.f,
				ElysiumLightStyle::Unstyled);
			TestEqual(TEXT("and serialized"),
				Garment->GetDefaultCustomPrimitiveData().Data.IsValidIndex(ElysiumLightStyle::SlotBrightness)
					? Garment->GetDefaultCustomPrimitiveData().Data[ElysiumLightStyle::SlotBrightness] : 0.f,
				ElysiumLightStyle::Unstyled);
		}
		ElysiumNpcVisual::InstallGarment(Body,Data->AssetId);
		Garments.Reset(); Owner->GetComponents(Garments);
		TestEqual(TEXT("reinstallation replaces existing garments"),Garments.Num(),Data->SourceGarmentCount);
		ElysiumNpcVisual::InstallGarment(Body,FString());
		Garments.Reset(); Owner->GetComponents(Garments);
		TestTrue(TEXT("changing to an undressed body clears old garments"),Garments.IsEmpty());
		TestTrue(TEXT("undressing removes serialized instance references"),Owner->GetInstanceComponents().IsEmpty());
	}
	TestTrue(TEXT("the real multi-garment body was exercised"),MultiGarmentBodies>0);
	return true;
}
#endif
