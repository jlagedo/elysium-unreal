#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ElysiumDynamicsData.h"
#include "ElysiumMapBakeLibrary.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDynamicsDataTest,"Elysium.Substrate.DynamicsData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumDynamicsDataTest::RunTest(const FString&)
{
	const FString Path=TEXT("/Game/ElysiumGenerated/Tests/R8/DYN_Records");
	auto* Package=CreatePackage(*Path); Package->FullyLoad();
	auto* Data=FindObject<UElysiumDynamicsData>(Package,TEXT("DYN_Records"));
	if (!Data) Data=NewObject<UElysiumDynamicsData>(Package,TEXT("DYN_Records"),RF_Public|RF_Standalone);
	const FString Json=TEXT(R"JSON({"schemaVersion":"1.0.0","assetId":"vtmb:model:test/records",
		"projectionPolicy":"provisional-animdynamics-v1","sourceRecordCount":2,
		"records":[
		 {"sourceOffset":500,"firstBone":1,"terminalBone":2,"unusedAuthoredPreset":4,"gravity":0.75,
		  "damping":0.2,"springExponent":2,"maxAngleDegrees":120,"boneIndices":[1,2],"boneNames":["hair1","hair2"],
		  "projection":"chain","recipeIndex":0,"reason":""},
		 {"sourceOffset":528,"firstBone":3,"terminalBone":-1,"unusedAuthoredPreset":7,"gravity":0.5,
		  "damping":0.1,"springExponent":3,"maxAngleDegrees":140,"boneIndices":[3],"boneNames":["curl"],
		  "projection":"source-only","recipeIndex":-1,"reason":"single head curl"}],
		"chains":[{"boundBone":"hair1","chainEnd":"hair2","gravityScale":0.75,"damping":0.7,
		 "angularSpring":0.04,"coneAngleDegrees":120}],"bodies":[]})JSON");
	FString Error;
	if (!TestNotNull(TEXT("typed dynamics imports"),UElysiumDynamicsData::ApplyJson(Data,Json,Error))) { AddError(Error); return false; }
	TestEqual(TEXT("source damping remains un-clamped"),Data->Records[0].Damping,.2f);
	TestEqual(TEXT("provisional mapping remains separate"),Data->Chains[0].Damping,.7f);
	FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
	if (!TestTrue(TEXT("dynamics saves"),UPackage::SavePackage(Package,Data,
		*FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension()),Args))) return false;
	Data=nullptr; Package=nullptr; UElysiumMapBakeLibrary::UnloadBakedPackages(Path);
	Data=LoadObject<UElysiumDynamicsData>(nullptr,*Path);
	if (!TestNotNull(TEXT("dynamics reloads"),Data)) return false;
	TestEqual(TEXT("all source and recipe fields survive reload"),UElysiumDynamicsData::Verify(Data,Json),FString());
	const FString Missing=Json.Replace(TEXT("\"sourceRecordCount\":2"),TEXT("\"sourceRecordCount\":3"));
	TestNull(TEXT("declared record loss refuses publication"),UElysiumDynamicsData::ApplyJson(Data,Missing,Error));
	TestEqual(TEXT("refusal preserves saved records"),UElysiumDynamicsData::Verify(Data,Json),FString());
	Data->Records[1].UnusedAuthoredPreset=8.f;
	TestFalse(TEXT("verification includes source-only and unused fields"),UElysiumDynamicsData::Verify(Data,Json).IsEmpty());
	return true;
}
#endif
