// Content-tier parity check for the props lane's model import: every model the three test-corpus maps (`sp_tutorial_1`, `sm_pawnshop_1`, `sm_hub_1`)
// reference must have landed a real `SM_` under `/ElysiumBaked/Meshes`, with the exact slot names
// `materialBindings.slots[]` names and a collision setup the placement lane can select without
// going back to the source.
//
// The ground truth is the staged `import/models/manifest.json` this machine's last `uv run
// elysium import models --maps sp_tutorial_1 --maps sm_pawnshop_1 --maps sm_hub_1` run wrote --
// not a re-derivation of "referenced" here, which could drift from the real selection and still
// agree with a bug in it (`.claude/rules/tests.md` -> "a probe that misses is a failure or is not
// a probe"). That manifest lives under `$ELYSIUM_WORK_ROOT/import/models/`, alongside every other
// oracle this suite reads directly from `$ELYSIUM_WORK_ROOT` (`ElysiumRigLayerTests.cpp` et al.),
// rather than under `$ELYSIUM_EXPORT_ROOT`/`FElysiumContentPaths::Root()`, which is the offline
// export tree, not the staged import.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

static constexpr EAutomationTestFlags GElysiumModelParityTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// `$ELYSIUM_WORK_ROOT/import/models/manifest.json`, or an empty string when
	// `ELYSIUM_WORK_ROOT` is not configured on this machine.
	FString ModelsManifestPath()
	{
		const FString WorkRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("ELYSIUM_WORK_ROOT"));
		if (WorkRoot.IsEmpty())
		{
			return FString();
		}
		return WorkRoot / TEXT("import") / TEXT("models") / TEXT("manifest.json");
	}

	// True (and an abstention already recorded) when either `ELYSIUM_WORK_ROOT` is unset or the
	// staged manifest is not on disk -- a missing prerequisite abstains rather than failing
	// (`.claude/rules/tests.md`).
	bool AbstainWithoutStagedManifest(FAutomationTestBase& Test, FString& OutManifestPath)
	{
		OutManifestPath = ModelsManifestPath();
		if (OutManifestPath.IsEmpty())
		{
			Test.AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: ELYSIUM_WORK_ROOT is not configured"));
			return true;
		}
		if (!IFileManager::Get().FileExists(*OutManifestPath))
		{
			Test.AddInfo(FString::Printf(
				TEXT("ELYSIUM_TEST_ABSTAIN: no staged models manifest at %s (run: uv run elysium ")
				TEXT("import models --maps sp_tutorial_1 --maps sm_pawnshop_1 --maps sm_hub_1 ")
				TEXT("--stage-only)"), *OutManifestPath));
			return true;
		}
		return false;
	}

	bool LoadManifestAssets(FAutomationTestBase& Test, const FString& ManifestPath,
		TArray<TSharedPtr<FJsonValue>>& OutAssets)
	{
		FString Text;
		if (!Test.TestTrue(TEXT("staged models manifest reads"),
			FFileHelper::LoadFileToString(Text, *ManifestPath)))
		{
			return false;
		}
		TSharedPtr<FJsonObject> Manifest;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!Test.TestTrue(TEXT("staged models manifest parses"),
			FJsonSerializer::Deserialize(Reader, Manifest) && Manifest.IsValid()))
		{
			return false;
		}
		OutAssets = Manifest->GetArrayField(TEXT("assets"));
		return Test.TestTrue(TEXT("staged models manifest carries at least one asset"),
			OutAssets.Num() > 0);
	}

	// `<Root>/SM_<stem>` -> `<Root>/SM_<stem>.SM_<stem>`, the `LoadObject` object-path shape every
	// other Content test builds from a `FElysiumContentPaths::Baked*` accessor -- built here by
	// hand because the V2 mesh root (`/ElysiumBaked/Meshes`) has no accessor yet: the flip that
	// gives it one is roadmap R5.1's own task, not this one's.
	FString ObjectPathFor(const FString& AssetPath)
	{
		FString Name;
		int32 SlashIndex = INDEX_NONE;
		if (AssetPath.FindLastChar(TEXT('/'), SlashIndex))
		{
			Name = AssetPath.Mid(SlashIndex + 1);
		}
		return AssetPath + TEXT(".") + Name;
	}
}

// The main parity sweep: every asset the staged manifest lists resolves to a real `UStaticMesh`
// with the manifest's own slot names, in order, and a collision setup that matches the manifest's
// own `mode`/`hullCount`/`massKg`.
// The loop always runs to completion; only the verdict afterward decides abstain vs. fail (below),
// so one broken asset among many resolved ones is a failure, never a silent abstain.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelParitySlotsAndCollisionTest,
	"Elysium.Content.ModelParity.SlotsAndCollision", GElysiumModelParityTestFlags)
bool FElysiumModelParitySlotsAndCollisionTest::RunTest(const FString&)
{
	FString ManifestPath;
	if (AbstainWithoutStagedManifest(*this, ManifestPath))
	{
		return true;
	}
	TArray<TSharedPtr<FJsonValue>> Assets;
	if (!LoadManifestAssets(*this, ManifestPath, Assets))
	{
		return false;
	}

	int32 MissingMeshes = 0, SlotMismatches = 0, CollisionMismatches = 0;
	int32 BboxAudited = 0, PhyAudited = 0;
	// Missing-mesh errors are deferred until after the full sweep: whether they are a genuine
	// per-asset failure or the whole run should abstain (nothing under /ElysiumBaked/Meshes was
	// ever imported) is a verdict only the total miss count can answer -- see below.
	TArray<TPair<FString, FString>> MissingMeshRows;

	for (const TSharedPtr<FJsonValue>& AssetValue : Assets)
	{
		const TSharedPtr<FJsonObject> Asset = AssetValue->AsObject();
		const FString AssetPath = Asset->GetStringField(TEXT("assetPath"));
		const FString Stem = Asset->GetStringField(TEXT("stem"));
		const FString ObjectPath = ObjectPathFor(AssetPath);

		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *ObjectPath);
		if (Mesh == nullptr)
		{
			++MissingMeshes;
			MissingMeshRows.Emplace(Stem, ObjectPath);
			continue;
		}

		// Slot names, in order: `materialBindings.slots[]` disambiguated names, exactly as landed
		// on the mesh via `safe_name(...)`.
		TArray<TSharedPtr<FJsonValue>> ManifestSlots = Asset->GetArrayField(TEXT("slots"));
		ManifestSlots.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
		{
			return A->AsObject()->GetIntegerField(TEXT("index"))
				< B->AsObject()->GetIntegerField(TEXT("index"));
		});
		const TArray<FStaticMaterial>& MeshMaterials = Mesh->GetStaticMaterials();
		bool bSlotsMatch = MeshMaterials.Num() == ManifestSlots.Num();
		if (bSlotsMatch)
		{
			for (int32 Index = 0; Index < ManifestSlots.Num(); ++Index)
			{
				const FName Expected(*ManifestSlots[Index]->AsObject()->GetStringField(
					TEXT("slotName")));
				if (MeshMaterials[Index].MaterialSlotName != Expected)
				{
					bSlotsMatch = false;
					break;
				}
			}
		}
		if (!bSlotsMatch)
		{
			++SlotMismatches;
			AddError(FString::Printf(
				TEXT("%s: baked slot names do not match the manifest (mesh has %d, manifest has %d)"),
				*Stem, MeshMaterials.Num(), ManifestSlots.Num()));
		}

		// Collision: uniform CTF_UseSimpleAndComplex, one convex hull per manifest hullCount for a
		// `.phy`-bearing model or exactly one box for the header-hull bbox fallback.
		const TSharedPtr<FJsonObject> Collision = Asset->GetObjectField(TEXT("collision"));
		const FString Mode = Collision->GetStringField(TEXT("mode"));
		const int32 ExpectedHullCount = static_cast<int32>(Collision->GetNumberField(TEXT("hullCount")));
		UBodySetup* Body = Mesh->GetBodySetup();
		bool bCollisionMatches = Body != nullptr && Body->CollisionTraceFlag == CTF_UseSimpleAndComplex;
		if (bCollisionMatches)
		{
			if (Mode == TEXT("phy"))
			{
				++PhyAudited;
				// `bake_lib.set_phy_collision` cooks each `.phy` ledge through GeometryScript's
				// convex-hull generator with box/sphere/capsule auto-detection explicitly off, so
				// every ledge lands as its own `FKConvexElem` -- the hull is reproduced, not
				// approximated: one convex shape per `physics.solids[i].hulls[j]` ledge,
				// unsimplified -- a cooked shape count that disagrees with the ledge count is a
				// stage failure.
				bCollisionMatches = Body->AggGeom.ConvexElems.Num() == ExpectedHullCount
					&& Body->AggGeom.GetElementCount() == ExpectedHullCount;
				double MassKg = 0.0;
				if (bCollisionMatches && Collision->TryGetNumberField(TEXT("massKg"), MassKg))
				{
					bCollisionMatches = Body->DefaultInstance.bOverrideMass
						&& FMath::IsNearlyEqual(Body->DefaultInstance.GetMassOverride(),
							static_cast<float>(MassKg), 0.01f);
				}
			}
			else if (Mode == TEXT("bbox"))
			{
				++BboxAudited;
				bCollisionMatches = Body->AggGeom.ConvexElems.Num() == 0
					&& Body->AggGeom.BoxElems.Num() == 1;
			}
			else
			{
				bCollisionMatches = false;
			}
		}
		if (!bCollisionMatches)
		{
			++CollisionMismatches;
			AddError(FString::Printf(TEXT("%s: baked collision setup does not match the manifest ")
				TEXT("(mode=%s, expected hullCount=%d, actual shapes=%d [convex=%d box=%d])"),
				*Stem, *Mode, ExpectedHullCount,
				Body ? Body->AggGeom.GetElementCount() : -1,
				Body ? Body->AggGeom.ConvexElems.Num() : -1, Body ? Body->AggGeom.BoxElems.Num() : -1));
		}
	}

	// The verdict: nothing at all resolved (every referenced asset missing) means the baked mount
	// itself is absent from this run's content -- abstain, not hundreds of failures that all say
	// the same thing. Anything short of that -- even one resolved asset among many missing ones --
	// is a genuine parity gap and fails loudly, closing the hole where a probe that only checked
	// `Assets[0]` would abstain the whole sweep on exactly the failure it exists to catch.
	if (MissingMeshes == Assets.Num())
	{
		AddInfo(FString::Printf(
			TEXT("ELYSIUM_TEST_ABSTAIN: the baked mount has no mesh for any of the %d referenced ")
			TEXT("model(s) (run: uv run elysium import models --maps sp_tutorial_1 --maps ")
			TEXT("sm_pawnshop_1 --maps sm_hub_1)"), Assets.Num()));
		return true;
	}
	for (const TPair<FString, FString>& Row : MissingMeshRows)
	{
		AddError(FString::Printf(TEXT("%s: referenced model has no baked SM_ at %s"),
			*Row.Key, *Row.Value));
	}

	TestEqual(TEXT("every referenced model has a baked SM_"), MissingMeshes, 0);
	TestEqual(TEXT("every baked SM_'s slot names match the manifest"), SlotMismatches, 0);
	TestEqual(TEXT("every baked SM_'s collision setup matches the manifest"), CollisionMismatches, 0);
	AddInfo(FString::Printf(
		TEXT("model parity: %d audited (%d bbox, %d phy), %d missing, %d slot mismatch(es), ")
		TEXT("%d collision mismatch(es)"), Assets.Num(), BboxAudited, PhyAudited, MissingMeshes,
		SlotMismatches, CollisionMismatches));
	return true;
}

// Spot-check named in the R1.6 notes: the sentinel unit `SM_models_scenery_structural_warrens_
// floorblock`, whose one slot has no real material (a `vtmb:missing-material:` sentinel) and so
// must bind `MI_V2_Missing` -- proving the model and material lanes join correctly, not just that
// a mesh exists.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelParitySentinelUnitTest,
	"Elysium.Content.ModelParity.SentinelUnit", GElysiumModelParityTestFlags)
bool FElysiumModelParitySentinelUnitTest::RunTest(const FString&)
{
	const TCHAR* ObjectPath = TEXT(
		"/ElysiumBaked/Meshes/SM_models_scenery_structural_warrens_floorblock."
		"SM_models_scenery_structural_warrens_floorblock");
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, ObjectPath);
	if (Mesh == nullptr)
	{
		AddInfo(FString::Printf(
			TEXT("ELYSIUM_TEST_ABSTAIN: the baked mount has no mesh at %s (run: uv run elysium ")
			TEXT("import models --maps sp_tutorial_1 --maps sm_pawnshop_1 --maps sm_hub_1)"),
			ObjectPath));
		return true;
	}

	const TArray<FStaticMaterial>& Materials = Mesh->GetStaticMaterials();
	if (TestEqual(TEXT("the sentinel unit has one slot"), Materials.Num(), 1))
	{
		TestEqual(TEXT("the sentinel slot binds MI_V2_Missing"),
			Materials[0].MaterialInterface != nullptr
				? Materials[0].MaterialInterface->GetPathName()
				: FString(),
			FString(TEXT("/Game/ElysiumGenerated/Materials/V2/MI_V2_Missing.MI_V2_Missing")));
	}

	UBodySetup* Body = Mesh->GetBodySetup();
	if (TestNotNull(TEXT("the sentinel unit has a body setup"), Body))
	{
		TestEqual(TEXT("the sentinel unit cooks simple AND complex collision"),
			Body->CollisionTraceFlag, CTF_UseSimpleAndComplex);
		// The manifest's own record: one `.phy` solid, one hull -- not the bbox fallback, since
		// this unit ships a `.phy` despite its slot's material being unresolved. Exactly one
		// `FKConvexElem`, not a substituted `FKBoxElem`: `bake_lib.set_phy_collision` disables
		// GeometryScript's box/sphere/capsule auto-detection, so a box-shaped ledge -- exactly
		// what a "floorblock" is -- still reproduces as the authored convex hull.
		TestEqual(TEXT("the sentinel unit cooks exactly one convex hull"),
			Body->AggGeom.ConvexElems.Num(), 1);
		TestEqual(TEXT("the sentinel unit cooks exactly one simple shape"),
			Body->AggGeom.GetElementCount(), 1);
		TestTrue(TEXT("the sentinel unit carries its authored mass"),
			Body->DefaultInstance.bOverrideMass && Body->DefaultInstance.GetMassOverride() > 0.0f);
	}
	return true;
}

// Spot-check named in the R1.6 notes: a multi-LOD unit's dropped/kept LOD rows land as real
// `FStaticMeshSourceModel` entries, not silently collapsed to one.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelParityMultiLodUnitTest,
	"Elysium.Content.ModelParity.MultiLodUnit", GElysiumModelParityTestFlags)
bool FElysiumModelParityMultiLodUnitTest::RunTest(const FString&)
{
	const TCHAR* ObjectPath = TEXT(
		"/ElysiumBaked/Meshes/SM_models_scenery_furniture_bench_bencha."
		"SM_models_scenery_furniture_bench_bencha");
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, ObjectPath);
	if (Mesh == nullptr)
	{
		AddInfo(FString::Printf(
			TEXT("ELYSIUM_TEST_ABSTAIN: the baked mount has no mesh at %s (run: uv run elysium ")
			TEXT("import models --maps sp_tutorial_1 --maps sm_pawnshop_1 --maps sm_hub_1)"),
			ObjectPath));
		return true;
	}

	// The manifest's own record for this unit: 3 LOD rows, none dropped.
	TestEqual(TEXT("the multi-LOD unit carries all 3 baked LODs"), Mesh->GetNumLODs(), 3);
	for (int32 LodIndex = 0; LodIndex < Mesh->GetNumLODs(); ++LodIndex)
	{
		TestTrue(FString::Printf(TEXT("LOD %d has render triangles"), LodIndex),
			Mesh->GetNumTriangles(LodIndex) > 0);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
