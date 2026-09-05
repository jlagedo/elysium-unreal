#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ElysiumBodyData.h"
#include "ElysiumMapBakeLibrary.h"
#include "Visual/ElysiumAnimationResolve.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumBodyDataTest, "Elysium.Substrate.BodyData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumBodyDataTest::RunTest(const FString&)
{
	const FString PackageName = TEXT("/Game/ElysiumGenerated/Tests/R8/DA_BodyData");
	UPackage* Package = CreatePackage(*PackageName);
	Package->FullyLoad();
	UElysiumBodyData* Data = FindObject<UElysiumBodyData>(Package, TEXT("DA_BodyData"));
	if (!Data) Data = NewObject<UElysiumBodyData>(Package, TEXT("DA_BodyData"), RF_Public | RF_Standalone);
	const FString Json = TEXT(R"JSON({"schemaVersion":"1.0.0","assetId":"vtmb:model:body","ownerRoot":"",
		"includeOwners":[{"assetId":"vtmb:model:body","sequenceBase":0},{"assetId":"vtmb:model:bank","sequenceBase":7}],
		"sequences":[{"label":"attack","owner":"vtmb:model:bank","ownerRoot":"","rawIndex":8,
			"activity":"ACT_ATTACK","weight":2,"flags":0,"frames":31,"fps":30,"fade":0.2,
			"reachCm":25.4,"lowReachCm":0,"comboMask":0,"hasCombo":true,
			"assets":{"owner":"vtmb:model:bank","ownerRoot":"","label":"attack","host":"",
				"sequence":"/ElysiumBaked/Models/test/A_Unloaded.A_Unloaded","blendSpace":"","baseCell":""},
			"layers":[],"declaredLayers":[]}]})JSON");
	FString Error;
	if (!TestNotNull(TEXT("body JSON imports"), UElysiumBodyData::ApplyJson(Data, Json, Error)))
	{
		AddError(Error); return false;
	}
	FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
	if (!TestTrue(TEXT("body data saves"), UPackage::SavePackage(Package, Data,
		*FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()), Args))) return false;
	Data = nullptr; Package = nullptr;
	UElysiumMapBakeLibrary::UnloadBakedPackages(PackageName);
	Data = LoadObject<UElysiumBodyData>(nullptr, *PackageName);
	if (!TestNotNull(TEXT("body data reloads"), Data)) return false;
	TestEqual(TEXT("reflected body rows survive"), UElysiumBodyData::Verify(Data, Json), FString());
	const auto* Row = Data->Find(TEXT("ATTACK"), TEXT("VTMB:MODEL:BANK"));
	if (!TestNotNull(TEXT("qualified lookup is case insensitive"), Row)) return false;
	TestTrue(TEXT("animation remains unloaded before scoring"), Row->Assets.Sequence.IsPending());
	const FElysiumNpcClipSet Vocabulary = Data->SelectionVocabulary(TEXT("stable-cast-key"));
	TestEqual(TEXT("asset identity does not replace the selection seed"), Vocabulary.Stem, FString(TEXT("stable-cast-key")));
	TestTrue(TEXT("maximum reach uses body values"), FMath::IsNearlyEqual(Vocabulary.MaxReachCmForActivity(TEXT("ACT_ATTACK")), 25.4f));
	const auto Pick = ElysiumAnimResolve::PickByStateMask(Vocabulary, TEXT("ACT_ATTACK"), 0);
	TestEqual(TEXT("stated neutral mask remains selectable"), Pick.Label, FString(TEXT("attack")));
	TestEqual(TEXT("selection preserves owner identity"), Pick.Owner, FString(TEXT("vtmb:model:bank")));
	TestTrue(TEXT("scoring did not load the animation"), Row->Assets.Sequence.IsPending());
	TSet<FSoftObjectPath> Paths; Data->GatherAnimationPaths(Paths);
	TestEqual(TEXT("preload closure names one native animation"), Paths.Num(), 1);
	return true;
}
#endif
