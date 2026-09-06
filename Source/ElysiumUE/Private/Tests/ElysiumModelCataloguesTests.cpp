#include "ElysiumModelCatalogues.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCatalogueWieldAbsences,
	"Elysium.Content.Catalogues.WieldAbsences", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElysiumCatalogueWieldAbsences::RunTest(const FString&)
{
	auto* Asset = NewObject<UElysiumWieldCatalogue>();
	auto& Item = Asset->Data.Items.Add(TEXT("item"));
	Item.Classname = TEXT("item");
	Item.Female.State = EElysiumCatalogueReferenceState::Absent;
	Item.Female.AssetId = TEXT("vtmb:model:error");
	Item.Male.State = EElysiumCatalogueReferenceState::Null;
	const FElysiumCatalogueWieldModel* Model = nullptr; FString Error;
	TestTrue(TEXT("explicit source absence fails"), Asset->Resolve(TEXT("ITEM"), true, Model, Error) == EElysiumCatalogueWieldResult::SourceAbsent);
	TestTrue(TEXT("diagnostic names source id"), Error.Contains(TEXT("vtmb:model:error")));
	TestTrue(TEXT("null is authored geometryless"), Asset->Resolve(TEXT("item"), false, Model, Error) == EElysiumCatalogueWieldResult::NoGeometry);
	Item.bShowsWieldModel = false;
	TestTrue(TEXT("world model gate precedes sex"), Asset->Resolve(TEXT("item"), true, Model, Error) == EElysiumCatalogueWieldResult::WorldModel);
	TestTrue(TEXT("unknown item fails"), Asset->Resolve(TEXT("missing"), true, Model, Error) == EElysiumCatalogueWieldResult::UnknownItem);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCatalogueSkinTransitions,
	"Elysium.Content.Catalogues.SkinTransitions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElysiumCatalogueSkinTransitions::RunTest(const FString&)
{
	auto* Asset = NewObject<UElysiumPropSkinCatalogue>();
	auto& Model = Asset->Data.Models.Add(TEXT("vtmb:model:test"));
	Model.FamilyCount = 3; Model.SkinReferenceCount = 2;
	auto& Rep = Model.Representations.AddDefaulted_GetRef(); Rep.Kind = TEXT("skeletal");
	auto& Slot = Rep.Slots.AddDefaulted_GetRef(); Slot.Index = 0; Slot.SlotName = TEXT("body"); Slot.SkinReferences.Add(1);
	auto* Base = NewObject<UMaterial>(); auto* Alternate = NewObject<UMaterial>();
	for (int32 I = 0; I < 3; ++I)
	{
		auto& Family = Rep.Families.AddDefaulted_GetRef(); Family.Index = I; Family.Cells.SetNum(2);
		Family.Cells[0].Material = Base; Family.Cells[1].Material = I == 1 ? Alternate : Base;
	}
	TArray<FElysiumCatalogueResolvedMaterial> Out; FString Error;
	TestTrue(TEXT("alternate resolves"), Asset->ResolveMaterials(TEXT("vtmb:model:test"), true, 1, Out, Error));
	TestTrue(TEXT("render slot uses skinref 1"), Out.Num() == 1 && Out[0].Material == Alternate);
	TestTrue(TEXT("base resolves after alternate"), Asset->ResolveMaterials(TEXT("vtmb:model:test"), true, 0, Out, Error));
	TestTrue(TEXT("base restores material"), Out.Num() == 1 && Out[0].Material == Base);
	TestTrue(TEXT("high family clamps to unchanged trailing row"), Asset->ResolveMaterials(TEXT("vtmb:model:test"), true, 88, Out, Error));
	TestTrue(TEXT("trailing family retained"), Out.Num() == 1 && Out[0].Material == Base);
	Rep.Families[1].Cells[1].Material = nullptr;
	TestFalse(TEXT("missing material is a failure"), Asset->ResolveMaterials(TEXT("vtmb:model:test"), true, 1, Out, Error));
	TestTrue(TEXT("failed assignment is atomic"), Out.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCatalogueClothStaticGate,
	"Elysium.Content.Catalogues.ClothStaticGate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElysiumCatalogueClothStaticGate::RunTest(const FString&)
{
	FElysiumCataloguePlacedModel Model;
	Model.StaticMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/ElysiumBaked/Models/SM_test.SM_test")));
	Model.bStaticRestSuffices = Model.bStaticEquivalent = Model.bStaticEquivalentProven = Model.bStaticTopologyEquivalent = true;
	TestTrue(TEXT("proved rest allows static"), Model.CanUseStatic(false));
	TestFalse(TEXT("requested animation needs skeletal"), Model.CanUseStatic(true));
	Model.bHasCloth = true;
	TestFalse(TEXT("cloth requires skeletal twin even when bind/rest match"), Model.CanUseStatic(false));
	Model.bHasCloth = false; Model.bStaticTopologyEquivalent = false;
	TestFalse(TEXT("submodel omission vetoes static"), Model.CanUseStatic(false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCatalogueJsonRoundtrip,
	"Elysium.Content.Catalogues.JsonRoundtrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElysiumCatalogueJsonRoundtrip::RunTest(const FString&)
{
#if WITH_EDITOR
	const FString Json = TEXT(R"JSON({"schemaVersion":"1.0.0","catalogueKind":"PlacedModels","data":{"models":{
	"vtmb:model:error":{"assetId":"vtmb:model:error","modelPath":"models/error.mdl","sourceAbsent":true,
	"sourceReason":"authored model absent from source","roles":[],"staticMesh":"","skeletalMesh":"","bodyData":"",
	"hasCloth":false,"staticEquivalentProven":false,"staticEquivalent":false,"staticRestSuffices":false,"staticSourceRepresentation":false,
	"staticTopologyEquivalent":false,"restCandidates":[],"clips":[],"fullClipsRequired":false,
	"requiredClips":[],"placementEvidence":"","nativeSequences":{},"nativeBlendSpaces":{},
	"acceptanceIssues":[],"sourceEvidence":"{}"}}}})JSON");
	auto* Asset = NewObject<UElysiumPlacedModelCatalogue>(); FString Error;
	TestNotNull(TEXT("JSON authoring accepts explicit absence"), UElysiumPlacedModelCatalogue::ApplyJson(Asset, Json, Error));
	TestTrue(TEXT("no conversion error"), Error.IsEmpty());
	const auto* Row = Asset->FindModel(TEXT("vtmb:model:error"));
	TestTrue(TEXT("cooked boolean survives reflection import"), Row && Row->bSourceAbsent);
	TestTrue(TEXT("reflected roundtrip is identical"), UElysiumPlacedModelCatalogue::Verify(Asset, Json).IsEmpty());
	FString Bad = Json.Replace(TEXT("\"acceptanceIssues\":[]"), TEXT("\"acceptanceIssues\":[\"missing native clip\"]"));
	TestNull(TEXT("unaccepted replacement rejected"), UElysiumPlacedModelCatalogue::ApplyJson(Asset, Bad, Error));
	TestTrue(TEXT("rejected authoring is atomic"), UElysiumPlacedModelCatalogue::Verify(Asset, Json).IsEmpty());
#endif
	return true;
}
#endif
