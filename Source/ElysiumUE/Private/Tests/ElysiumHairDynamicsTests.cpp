#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Algo/AllOf.h"

#include "ElysiumContentPaths.h"
#include "Visual/ElysiumHairDynamicsData.h"

#include "Engine/SkeletalMesh.h"
#include "HAL/IConsoleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumHairDynamicsBakedScopeTest,
	"Elysium.Content.Characters.HairDynamicsBakedScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumHairDynamicsBakedScopeTest::RunTest(const FString& Parameters)
{
	TestNull(TEXT("hair proof has no runtime feature CVar"),
		IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.HairDynamics")));

	struct FSubject
	{
		const TCHAR* Stem;
		bool bPlayerMaterial;
		int32 ExpectedChains;
		const TCHAR* First;
		const TCHAR* Last;
	};
	const FSubject Subjects[] = {
		{ TEXT("malkavian_female_armor_0"), true, 1, TEXT("Bone05"), TEXT("Bone09") },
		{ TEXT("jeanette"), false, 2, TEXT("Bone01"), TEXT("Bone13") },
	};

	TArray<USkeletalMesh*> Meshes;
	for (const FSubject& Subject : Subjects)
	{
		Meshes.Add(LoadObject<USkeletalMesh>(nullptr,
			*FElysiumContentPaths::BakedCharacterMesh(Subject.Stem, Subject.bPlayerMaterial),
			nullptr, LOAD_NoWarn | LOAD_Quiet));
	}
	if (Algo::AllOf(Meshes, [](const USkeletalMesh* Mesh) { return Mesh == nullptr; }))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: neither hair proof body is baked; run the focused character export"));
		return true;
	}

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Subjects); ++Index)
	{
		const FSubject& Subject = Subjects[Index];
		USkeletalMesh* Mesh = Meshes[Index];
		if (!TestNotNull(*FString::Printf(TEXT("%s proof mesh is baked"), Subject.Stem), Mesh))
		{
			continue;
		}
		const UElysiumHairDynamicsAssetUserData* Hair =
			Cast<UElysiumHairDynamicsAssetUserData>(Mesh->GetAssetUserDataOfClass(
				UElysiumHairDynamicsAssetUserData::StaticClass()));
		if (!TestNotNull(*FString::Printf(TEXT("%s carries hair metadata"), Subject.Stem), Hair))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s has exact chain count"), Subject.Stem),
			Hair->Chains.Num(), Subject.ExpectedChains);
		if (!Hair->Chains.IsEmpty())
		{
			TestEqual(TEXT("first selected bound bone"), Hair->Chains[0].BoundBone,
				FName(Subject.First));
			TestEqual(TEXT("last selected chain end"), Hair->Chains.Last().ChainEnd,
				FName(Subject.Last));
			if (FStringView(Subject.Stem).Equals(TEXT("jeanette")) && Hair->Chains.Num() == 2)
			{
				TestEqual(TEXT("Jeanette first chain end"), Hair->Chains[0].ChainEnd,
					FName(TEXT("Bone07")));
				TestEqual(TEXT("Jeanette second bound bone"), Hair->Chains[1].BoundBone,
					FName(TEXT("Bone09")));
			}
		}
	}
	return true;
}

#endif
