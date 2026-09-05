#if WITH_DEV_AUTOMATION_TESTS
#include "Visual/ElysiumPreparedPropModels.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	struct FPreparedPropFixture
	{
		TStrongObjectPtr<UObject> Owner{NewObject<UObject>()};
		FString Key = TEXT("prop_test_") + FGuid::NewGuid().ToString(EGuidFormats::Digits).ToLower() + TEXT("/prop");
		FString Id = TEXT("vtmb:model:") + Key;
		UElysiumPlacedModelCatalogue* Placed = NewObject<UElysiumPlacedModelCatalogue>();
		UElysiumPropSkinCatalogue* Skins = NewObject<UElysiumPropSkinCatalogue>();
		UStaticMesh* Mesh = nullptr;

		FPreparedPropFixture()
		{
			const FString Package = TEXT("/ElysiumBaked/Models/") + Key.LeftChop(4) + TEXT("SM_prop");
			Mesh = NewObject<UStaticMesh>(CreatePackage(*Package), FName(TEXT("SM_prop")), RF_Transient);
			auto* Base = NewObject<UMaterial>(); auto* Alternate = NewObject<UMaterial>();
			Mesh->GetStaticMaterials().Add(FStaticMaterial(Base, FName(TEXT("surface"))));
			auto& Skin = Skins->Data.Models.Add(Id); Skin.AssetId = Id; Skin.FamilyCount = 3; Skin.SkinReferenceCount = 1;
			auto& Rep = Skin.Representations.AddDefaulted_GetRef(); Rep.Kind = TEXT("static"); Rep.StaticMesh = Mesh;
			auto& Slot = Rep.Slots.AddDefaulted_GetRef(); Slot.SlotName = TEXT("surface"); Slot.SkinReferences = {0};
			for (int32 Index = 0; Index < 3; ++Index)
			{
				auto& Family = Rep.Families.AddDefaulted_GetRef(); Family.Index = Index;
				auto& Cell = Family.Cells.AddDefaulted_GetRef(); Cell.Material = Index == 1 ? Alternate : Base;
				Cell.MaterialId = Index == 1 ? TEXT("vtmb:material:alternate") : TEXT("vtmb:material:base");
			}
			auto& Row = Placed->Data.Models.Add(Id); Row.AssetId = Id; Row.ModelPath = TEXT("models/") + Key + TEXT(".mdl");
			Row.StaticMesh = Mesh; Row.bStaticEquivalentProven = Row.bStaticEquivalent = Row.bStaticRestSuffices = Row.bStaticTopologyEquivalent = true;
			for (int32 Index = 0; Index < 2; ++Index)
			{
				auto& Clip = Row.Clips.AddDefaulted_GetRef(); Clip.Index = Index;
				Clip.Label = Index == 0 ? TEXT("idle_a") : TEXT("idle_b"); Clip.Owner = Id;
				Clip.Weight = Index == 0 ? 1 : 3; Clip.Flags = Index; Clip.State = TEXT("static-rest-only");
				Row.RestCandidates.Add(Index);
			}
		}
		TSharedPtr<FElysiumPreparedPropModels> Prepare(uint64 Epoch, FString& Error)
		{
			return FElysiumPreparedPropModels::Create(Owner.Get(), Epoch, Placed, Skins, {Id}, {Mesh}, Error);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPreparedPropIdentity,
	"Elysium.Content.PreparedPropModels.IdentityAndInventory", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElysiumPreparedPropIdentity::RunTest(const FString&)
{
	FPreparedPropFixture F; FString Error;
	TestEqual(TEXT("full source spelling retains ID"), ElysiumPreparedProps::ModelId(TEXT("models/") + F.Key + TEXT(".mdl")), F.Id);
	TestTrue(TEXT("flattened stem is never inferred"), ElysiumPreparedProps::ModelId(TEXT("models_scenery_prop")).IsEmpty());
	TSet<FSoftObjectPath> Paths;
	TestTrue(TEXT("canonical paths collected"), FElysiumPreparedPropModels::GatherPaths(F.Placed, F.Skins, {F.Id}, Paths, Error));
	TestTrue(TEXT("exact declared mesh in preload set"), Paths.Contains(FSoftObjectPath(F.Mesh)));
	TestEqual(TEXT("duplicate paths coalesce"), Paths.Num(), 1);
	TestFalse(TEXT("missing resident asset refuses preparation"), FElysiumPreparedPropModels::Create(F.Owner.Get(), 1, F.Placed, F.Skins, {F.Id}, {}, Error).IsValid());
	const auto Ready = F.Prepare(1, Error);
	if (!TestTrue(TEXT("resident source accepted: ") + Error, Ready.IsValid())) return false;
	TestTrue(TEXT("loaded pointer returned without resolution"), Ready->StaticMesh(F.Id, Error) == F.Mesh);
	TestTrue(TEXT("unadmitted ID refuses lookup"), Ready->StaticMesh(TEXT("vtmb:model:elsewhere"), Error) == nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPreparedPropEpoch,
	"Elysium.Content.PreparedPropModels.EpochReplacement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElysiumPreparedPropEpoch::RunTest(const FString&)
{
	FPreparedPropFixture F; FString Error;
	auto Old = F.Prepare(1, Error); auto Current = F.Prepare(2, Error);
	if (!TestTrue(TEXT("epochs prepared"), Old.IsValid() && Current.IsValid())) return false;
	TestTrue(TEXT("old captured handle is invalidated"), Old->StaticMesh(F.Id, Error) == nullptr);
	Old.Reset();
	TestTrue(TEXT("old destruction preserves replacement"), ElysiumPreparedProps::ForOwner(F.Owner.Get()) == Current);
	ElysiumPreparedProps::Release(F.Owner.Get());
	TestFalse(TEXT("release removes lookup"), ElysiumPreparedProps::ForOwner(F.Owner.Get()).IsValid());
	TestTrue(TEXT("release invalidates captured handle"), Current->StaticMesh(F.Id, Error) == nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPreparedPropClothAndRest,
	"Elysium.Content.PreparedPropModels.ClothAndRest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElysiumPreparedPropClothAndRest::RunTest(const FString&)
{
	FPreparedPropFixture F; FString Error;
	const auto Ready = F.Prepare(1, Error);
	if (!TestTrue(TEXT("prepared"), Ready.IsValid())) return false;
	const auto* Row = Ready->Model(F.Id, Error); const auto* View = Ready->CompatibilityView(F.Id);
	if (!TestNotNull(TEXT("compatibility view"), View)) return false;
	for (int32 Token : {0, 1, 7, 119, MAX_int32})
		TestEqual(TEXT("placement token uses identical weighted rest choice"), Row->SelectRest(Token)->Label, View->RestSequence(Token));
	TestTrue(TEXT("static representation permitted only at rest"), Row->CanUseStatic(false));
	TestFalse(TEXT("animation demand vetoes static"), Row->CanUseStatic(true));
	TestTrue(TEXT("static-only descriptor never produces a guessed animation"), Ready->Sequence(F.Id, TEXT("idle_a"), Error) == nullptr);
	F.Placed->Data.Models[F.Id].bHasCloth = true;
	TestFalse(TEXT("cloth vetoes static even when geometry agrees"), F.Placed->Data.Models[F.Id].CanUseStatic(false));
	TestFalse(TEXT("missing cloth skeletal twin refuses preparation"), F.Prepare(2, Error).IsValid());
	TestFalse(TEXT("failed refresh cannot reuse prior context"), ElysiumPreparedProps::ForOwner(F.Owner.Get()).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPreparedPropRoots,
	"Elysium.Content.PreparedPropModels.GCRoots", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FElysiumPreparedPropRoots::RunTest(const FString&)
{
	FPreparedPropFixture F; FString Error;
	const auto Ready = F.Prepare(1, Error);
	if (!TestTrue(TEXT("prepared"), Ready.IsValid())) return false;
	TWeakObjectPtr<UStaticMesh> Mesh(F.Mesh);
	TWeakObjectPtr<UElysiumPropSkinCatalogue> Skins(F.Skins);
	CollectGarbage(RF_NoFlags);
	TestTrue(TEXT("prepared mesh is strongly held"), Mesh.IsValid());
	TestTrue(TEXT("catalogue and its hard material families survive GC"), Skins.IsValid());
	TArray<FElysiumCatalogueResolvedMaterial> Materials;
	TestTrue(TEXT("last unchanged family clamps and remains available"), F.Skins->ResolveMaterials(F.Id, false, 99, Materials, Error));
	TestTrue(TEXT("material retained"), Materials.Num() == 1 && Materials[0].Material != nullptr);
	return true;
}
#endif
