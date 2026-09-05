#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ElysiumCastData.h"
#include "ElysiumMapBakeLibrary.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCastDataTest,"Elysium.Substrate.CastData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumCastDataTest::RunTest(const FString&)
{
	const FString PackageName=TEXT("/Game/ElysiumGenerated/Tests/R8/DA_CastData");
	UPackage* Package=CreatePackage(*PackageName); Package->FullyLoad();
	auto* Data=FindObject<UElysiumCastData>(Package,TEXT("DA_CastData"));
	if (!Data) Data=NewObject<UElysiumCastData>(Package,TEXT("DA_CastData"),RF_Public|RF_Standalone);
	const FString Json=TEXT(R"JSON({"schemaVersion":"1.0.0","models":{
		"vtmb:model:a/same":{"assetId":"vtmb:model:a/same","modelPath":"models/a/same.mdl","stem":"a_same","roles":[],
			"mesh":"/ElysiumBaked/Models/a/SK_same.SK_same","skeleton":"","bodyData":"/ElysiumBaked/Models/a/DA_same.DA_same"},
		"vtmb:model:b/same":{"assetId":"vtmb:model:b/same","modelPath":"models/b/same.mdl","stem":"b_same","roles":[],
			"mesh":"","skeleton":"","bodyData":""}},
		"aliases":{"models/a/same.mdl":"vtmb:model:a/same","hero":"vtmb:model:a/same"},
		"ambiguousAliases":{"same":["vtmb:model:a/same","vtmb:model:b/same"]},
		"cinematics":{"vtmb:model:a/same":{"assetId":"vtmb:model:a/same","modelPath":"models/a/same.mdl","roots":{
			"Bip01":{"assetId":"vtmb:model:a/same","ownerRoot":"Bip01","bodyData":"/ElysiumBaked/Models/a/DA_same_Bip01.DA_same_Bip01"},
			"Bip02":{"assetId":"vtmb:model:a/same","ownerRoot":"Bip02","bodyData":"/ElysiumBaked/Models/a/DA_same_Bip02.DA_same_Bip02"}}}}})JSON");
	FString Error;
	if (!TestNotNull(TEXT("cast imports"),UElysiumCastData::ApplyJson(Data,Json,Error))) { AddError(Error); return false; }
	FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
	if (!TestTrue(TEXT("cast saves"),UPackage::SavePackage(Package,Data,
		*FPackageName::LongPackageNameToFilename(PackageName,FPackageName::GetAssetPackageExtension()),Args))) return false;
	Data=nullptr; Package=nullptr; UElysiumMapBakeLibrary::UnloadBakedPackages(PackageName);
	Data=LoadObject<UElysiumCastData>(nullptr,*PackageName);
	if (!TestNotNull(TEXT("cast reloads"),Data)) return false;
	TestEqual(TEXT("all cast fields survive"),UElysiumCastData::Verify(Data,Json),FString());
	const auto* Model=Data->FindModel(TEXT("MODELS\\A\\SAME.MDL"),Error);
	if (!TestNotNull(TEXT("raw model path resolves"),Model)) return false;
	TestTrue(TEXT("lookup does not load the mesh"),Model->Mesh.IsPending());
	TestTrue(TEXT("human alias reaches the same record"),Data->FindModel(TEXT("hero"),Error)==Model);
	TestNull(TEXT("ambiguous basename does not choose a model"),Data->FindModel(TEXT("same"),Error));
	TestTrue(TEXT("collision names candidates"),Error.Contains(TEXT("vtmb:model:b/same")));
	const auto* Owner=Data->FindCinematic(TEXT("hero"),TEXT("bip02"),Error);
	if (TestNotNull(TEXT("exact actor root resolves"),Owner)) TestEqual(TEXT("actor identity is retained"),Owner->OwnerRoot,FString(TEXT("Bip02")));
	TestNull(TEXT("empty root does not choose between actors"),Data->FindCinematic(TEXT("hero"),FString(),Error));
	TestNull(TEXT("wrong root does not choose another actor"),Data->FindCinematic(TEXT("hero"),TEXT("Bip03"),Error));
	return true;
}
#endif
