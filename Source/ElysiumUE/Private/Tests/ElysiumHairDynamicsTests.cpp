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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumBreastDynamicsBakedScopeTest,
	"Elysium.Content.Characters.BreastDynamicsBakedScope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FElysiumBreastDynamicsBakedScopeTest::RunTest(const FString& Parameters)
{
	TestNull(TEXT("breast proof has no runtime feature CVar"),
		IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.BreastDynamics")));

	USkeletalMesh* Jeanette = LoadObject<USkeletalMesh>(nullptr,
		*FElysiumContentPaths::BakedCharacterMesh(TEXT("jeanette")),
		nullptr, LOAD_NoWarn | LOAD_Quiet);
	USkeletalMesh* Lily = LoadObject<USkeletalMesh>(nullptr,
		*FElysiumContentPaths::BakedCharacterMesh(TEXT("lily")),
		nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Jeanette == nullptr && Lily == nullptr)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no breast body is baked; run a focused character export"));
		return true;
	}

	if (Jeanette != nullptr)
	{
		const UElysiumHairDynamicsAssetUserData* Hair =
			Cast<UElysiumHairDynamicsAssetUserData>(Jeanette->GetAssetUserDataOfClass(
				UElysiumHairDynamicsAssetUserData::StaticClass()));
		if (Hair == nullptr || Hair->Bodies.Num() == 0)
		{
			AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: jeanette is baked without breast bodies; re-export jeanette"));
		}
		else if (TestEqual(TEXT("jeanette has two breast bodies"), Hair->Bodies.Num(), 2))
		{
			TSet<FName> Names;
			for (const FElysiumHairDynamicsBodyConfig& Body : Hair->Bodies)
			{
				Names.Add(Body.BoundBone);
				TestTrue(TEXT("jeanette breast cone is within the bake ceiling"),
					Body.ConeAngleDegrees >= 0.0f && Body.ConeAngleDegrees <= 90.0f);
			}
			TestTrue(TEXT("jeanette right breast"), Names.Contains(FName(TEXT("right breast"))));
			TestTrue(TEXT("jeanette left breast"), Names.Contains(FName(TEXT("left breast"))));
			TestEqual(TEXT("jeanette hair chains stay a pair"), Hair->Chains.Num(), 2);
		}
	}

	if (Lily != nullptr)
	{
		const UElysiumHairDynamicsAssetUserData* Hair =
			Cast<UElysiumHairDynamicsAssetUserData>(Lily->GetAssetUserDataOfClass(
				UElysiumHairDynamicsAssetUserData::StaticClass()));
		if (Hair == nullptr || Hair->Bodies.Num() == 0)
		{
			AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: lily is baked without breast bodies; re-export lily"));
		}
		else
		{
			TestEqual(TEXT("lily has two breast bodies"), Hair->Bodies.Num(), 2);
			TestEqual(TEXT("lily carries no hair proof chains"), Hair->Chains.Num(), 0);
			TSet<FName> Names;
			for (const FElysiumHairDynamicsBodyConfig& Body : Hair->Bodies)
			{
				Names.Add(Body.BoundBone);
			}
			TestTrue(TEXT("lily BoobRight01"), Names.Contains(FName(TEXT("BoobRight01"))));
			TestTrue(TEXT("lily BoobLeft03"), Names.Contains(FName(TEXT("BoobLeft03"))));
		}
	}
	return true;
}

#endif
