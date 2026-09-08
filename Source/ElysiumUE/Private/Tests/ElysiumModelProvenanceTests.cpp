// Content-free Substrate automation for UElysiumModelProvenance: the record a baked static mesh
// carries from its `vtmb:model:` unit.
//
// Reconciled against the landed stage (R1.4): this fixture is now the shape
// `importers/models.py::stage_unit` actually writes, not the camelCase transliteration an earlier
// draft of this file guessed while the Python side was still being written in parallel. The key
// set is pinned on the Python side by `pipeline/tests/test_model_provenance_keys.py` against
// `pipeline/tests/fixtures/model_provenance_keys.json`, which this file's fixture is composed
// from; neither may drift without the other failing. `shapeCount` is the one key the editor
// import adds on top of the sidecar (the cooked simple-shape count, which the offline stage
// cannot know).
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumModelProvenance.h"
#include "Engine/StaticMesh.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#if WITH_EDITORONLY_DATA
#include "UObject/MetaData.h"
#endif

static constexpr EAutomationTestFlags GElysiumModelProvenanceTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	const TCHAR* GModelSidecar = TEXT(R"json({
  "assetId": "vtmb:model:character/npc/unique/downtown/lacroix/lacroix",
  "modelPath": "character/npc/unique/downtown/lacroix/lacroix.mdl",
  "stem": "models_character_npc_unique_downtown_lacroix_lacroix",
  "unitSchemaVersion": "2.0.0",
  "unitSha256": "unit-sha",
  "sourceSha256": [
    {"role": "mdl", "sha256": "mdl-sha"},
    {"role": "vtx-dx80", "sha256": "vtx-sha"},
    {"role": "vtx-dx7-2bone", "sha256": "vtx-cmp-sha"},
    {"role": "phy", "sha256": "phy-sha"}
  ],
  "settingsVersion": "elysium-model-import-v2",
  "shape": "skeletal",
  "family": "character",
  "roles": ["character-body"],
  "slots": [
    {"index": 0, "slotName": "tankwht", "sourceName": "tankwht", "sourcePath": "materials/character/lacroix/tankwht.vmt", "skinReference": 0, "mdlSlotIndex": 0, "materialId": "vtmb:material:character/lacroix/tankwht", "materialAsset": "/ElysiumBaked/Materials/character/lacroix/MI_tankwht", "resolved": true, "isSentinel": false, "surfaceProperty": "flesh"},
    {"index": 1, "slotName": "metalox", "sourceName": "metalox", "sourcePath": "", "skinReference": 1, "mdlSlotIndex": 1, "materialId": "vtmb:missing-material:1:metalox", "materialAsset": "/Game/ElysiumGenerated/Materials/V2/MI_V2_Missing", "resolved": false, "isSentinel": true, "surfaceProperty": ""}
  ],
  "skinFamilies": [
    {"family": 0, "slots": ["tankwht", "metalox"], "materials": ["/ElysiumBaked/Materials/character/lacroix/MI_tankwht", "/Game/ElysiumGenerated/Materials/V2/MI_V2_Missing"], "materialIds": ["vtmb:material:character/lacroix/tankwht", null]},
    {"family": 1, "slots": ["tankwht", "metalox"], "materials": ["/ElysiumBaked/Materials/character/lacroix/MI_tankwht_alt", "/Game/ElysiumGenerated/Materials/V2/MI_V2_Missing"], "materialIds": ["vtmb:material:character/lacroix/tankwht_alt", null]}
  ],
  "familyCount": 2,
  "lods": [
    {"index": 0, "mesh": 0, "switchPoint": 0.0, "screenSize": 1.0, "sections": 3, "triangles": 5000, "primitiveCount": 3, "dropped": false},
    {"index": 1, "mesh": 1, "switchPoint": 10.0, "screenSize": 0.1, "sections": 3, "triangles": 2000, "primitiveCount": 3, "dropped": false}
  ],
  "bNanite": true,
  "naniteVetoSlot": "",
  "naniteVetoMaterial": "",
  "collisionMode": "phy",
  "hullCount": 15,
  "solidCount": 1,
  "shapeCount": 15,
  "massKg": 82.5,
  "collisionTraceFlag": "CTF_UseSimpleAndComplex",
  "hullBounds": {"min": [-10.0, -10.0, 0.0], "max": [10.0, 10.0, 180.0]},
  "jointIdentity": null,
  "surfaceProperty": "flesh",
  "surfacePropertySource": "physSolid",
  "physMaterial": "/ElysiumBaked/SurfaceProperties/PM_flesh",
  "anomalies": [
    {"kind": "duplicateSlotName", "collided": "tankwht"},
    {"row": "degenerate-normal", "vertex": 12, "lod": 0}
  ],
  "omissions": [
    {"row": "reserved-field", "reason": "v2531-has-no-header-keyvalues-region", "field": "mdl.keyValues"},
    {"kind": "noPhysicsSolidsBoxFallback", "hullBounds": null}
  ],
  "coverage": {"unresolved": "0", "unsupported": "0"}
})json");

	UStaticMesh* NewMesh()
	{
		return NewObject<UStaticMesh>(GetTransientPackage(), NAME_None, RF_Transient);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelProvenanceApplyJsonTest,
	"Elysium.Substrate.ModelProvenance.ApplyJson", GElysiumModelProvenanceTestFlags)
bool FElysiumModelProvenanceApplyJsonTest::RunTest(const FString&)
{
	UStaticMesh* Mesh = NewMesh();

	FString Error;
	UElysiumModelProvenance* Record = UElysiumModelProvenance::ApplyJson(Mesh, GModelSidecar, Error);
	if (!Record)
	{
		AddError(FString::Printf(TEXT("ApplyJson rejected the sidecar: %s"), *Error));
		return false;
	}
	TestTrue(TEXT("no error on success"), Error.IsEmpty());
	TestTrue(TEXT("the record is outered to the mesh"), Record->GetOuter() == Mesh);

	// --- identity ---
	TestEqual(TEXT("AssetId"), Record->AssetId,
		FString(TEXT("vtmb:model:character/npc/unique/downtown/lacroix/lacroix")));
	TestEqual(TEXT("Stem"), Record->Stem,
		FString(TEXT("models_character_npc_unique_downtown_lacroix_lacroix")));
	TestEqual(TEXT("UnitSha256"), Record->UnitSha256, FString(TEXT("unit-sha")));
	// One `{role, sha256}` row per source member, flattened `role:sha256` -- which member a digest
	// belongs to is the reason this field is an array on the model lane at all.
	if (TestEqual(TEXT("SourceSha256 count (mdl, both vtx variants, phy)"), Record->SourceSha256.Num(), 4))
	{
		TestEqual(TEXT("SourceSha256[0]"), Record->SourceSha256[0], FString(TEXT("mdl:mdl-sha")));
		TestEqual(TEXT("SourceSha256[3]"), Record->SourceSha256[3], FString(TEXT("phy:phy-sha")));
	}
	TestEqual(TEXT("Shape"), Record->Shape, FString(TEXT("skeletal")));
	TestEqual(TEXT("Family"), Record->Family, FString(TEXT("character")));
	if (TestEqual(TEXT("Roles count"), Record->Roles.Num(), 1))
	{
		TestEqual(TEXT("Roles[0]"), Record->Roles[0], FString(TEXT("character-body")));
	}

	// --- material binding: the sentinel slot is not this lane's own resolution failure ---
	if (TestEqual(TEXT("Slots count"), Record->Slots.Num(), 2))
	{
		TestEqual(TEXT("slot 0 SlotName"), Record->Slots[0].SlotName, FString(TEXT("tankwht")));
		TestTrue(TEXT("slot 0 Resolved"), Record->Slots[0].Resolved);
		TestFalse(TEXT("slot 0 not a sentinel"), Record->Slots[0].IsSentinel);
		TestTrue(TEXT("slot 1 IsSentinel"), Record->Slots[1].IsSentinel);
		TestEqual(TEXT("slot 0 MaterialAssetId reads the stage's `materialId`"),
			Record->Slots[0].MaterialAssetId,
			FString(TEXT("vtmb:material:character/lacroix/tankwht")));
		TestEqual(TEXT("slot 1 MaterialAsset (sentinel)"), Record->Slots[1].MaterialAsset,
			FString(TEXT("/Game/ElysiumGenerated/Materials/V2/MI_V2_Missing")));
	}
	// Each staged family carries parallel `slots`/`materials` arrays over every skin-table column;
	// the record zips them, so a family states what it paints at each slot.
	if (TestEqual(TEXT("SkinFamilies count"), Record->SkinFamilies.Num(), 2))
	{
		if (TestEqual(TEXT("family 0 override count"), Record->SkinFamilies[0].Overrides.Num(), 2))
		{
			TestEqual(TEXT("family 0 slot 0"), Record->SkinFamilies[0].Overrides[0].SlotName,
				FString(TEXT("tankwht")));
		}
		if (TestEqual(TEXT("family 1 override count"), Record->SkinFamilies[1].Overrides.Num(), 2))
		{
			TestEqual(TEXT("family 1 override slot"), Record->SkinFamilies[1].Overrides[0].SlotName,
				FString(TEXT("tankwht")));
			TestEqual(TEXT("family 1 override material"),
				Record->SkinFamilies[1].Overrides[0].MaterialAsset,
				FString(TEXT("/ElysiumBaked/Materials/character/lacroix/MI_tankwht_alt")));
		}
	}

	// --- geometry ---
	if (TestEqual(TEXT("Lods count"), Record->Lods.Num(), 2))
	{
		TestEqual(TEXT("lod 0 ScreenSize"), Record->Lods[0].ScreenSize, 1.0f);
		TestEqual(TEXT("lod 1 SwitchPoint"), Record->Lods[1].SwitchPoint, 10.0f);
		TestFalse(TEXT("lod 1 not dropped"), Record->Lods[1].Dropped);
	}
	TestTrue(TEXT("bNanite"), Record->bNanite);

	// --- collision ---
	TestEqual(TEXT("CollisionMode"), Record->CollisionMode, FString(TEXT("phy")));
	TestEqual(TEXT("HullCount"), Record->HullCount, 15);
	TestEqual(TEXT("ShapeCount"), Record->ShapeCount, 15);
	TestEqual(TEXT("MassKg"), Record->MassKg, 82.5f);
	TestEqual(TEXT("HullBounds.Min"), Record->HullBounds.Min, FVector(-10.0, -10.0, 0.0));
	TestEqual(TEXT("HullBounds.Max"), Record->HullBounds.Max, FVector(10.0, 10.0, 180.0));

	// --- surface property ---
	TestEqual(TEXT("SurfaceProperty"), Record->SurfaceProperty, FString(TEXT("flesh")));
	TestEqual(TEXT("SurfacePropertySource"), Record->SurfacePropertySource, FString(TEXT("physSolid")));
	TestEqual(TEXT("PhysMaterial"), Record->PhysMaterial, FString(TEXT("/ElysiumBaked/SurfaceProperties/PM_flesh")));

	// --- content ---
	// Two row spellings share each array: this lane's own rows label with `kind`, and the unit's
	// own export rows -- merged in verbatim by the stage -- label with `row`. Both must read.
	if (TestEqual(TEXT("Anomalies count"), Record->Anomalies.Num(), 2))
	{
		TestEqual(TEXT("anomaly 0 kind"), Record->Anomalies[0].Kind, FString(TEXT("duplicateSlotName")));
		if (const FString* Collided = Record->Anomalies[0].Extra.Find(TEXT("collided")))
		{
			TestEqual(TEXT("anomaly 0 extra.collided"), *Collided, FString(TEXT("tankwht")));
		}
		else
		{
			AddError(TEXT("anomaly 0 has no collided in Extra"));
		}
		TestEqual(TEXT("anomaly 1 kind falls back to the unit's own `row`"),
			Record->Anomalies[1].Kind, FString(TEXT("degenerate-normal")));
	}
	if (TestEqual(TEXT("Omissions count"), Record->Omissions.Num(), 2))
	{
		TestEqual(TEXT("omission 0 reason"), Record->Omissions[0].Reason,
			FString(TEXT("v2531-has-no-header-keyvalues-region")));
		if (const FString* Row = Record->Omissions[0].Extra.Find(TEXT("row")))
		{
			TestEqual(TEXT("omission 0 extra.row"), *Row, FString(TEXT("reserved-field")));
		}
		else
		{
			AddError(TEXT("omission 0 has no row in Extra"));
		}
		TestEqual(TEXT("omission 1 reason falls back to this lane's own `kind`"),
			Record->Omissions[1].Reason, FString(TEXT("noPhysicsSolidsBoxFallback")));
	}
	if (const FString* Unresolved = Record->Coverage.Find(TEXT("unresolved")))
	{
		TestEqual(TEXT("coverage.unresolved"), *Unresolved, FString(TEXT("0")));
	}
	else
	{
		AddError(TEXT("Coverage has no unresolved field"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelProvenanceToleratesMissingFieldsTest,
	"Elysium.Substrate.ModelProvenance.ToleratesMissingFields", GElysiumModelProvenanceTestFlags)
bool FElysiumModelProvenanceToleratesMissingFieldsTest::RunTest(const FString&)
{
	// A minimal sidecar -- most of a real one's keys absent -- must not fail ApplyJson; every
	// missing field keeps its declared default, exactly as UElysiumMaterialProvenance::FromJson
	// tolerates an optional key the stage has not started publishing yet.
	const TCHAR* MinimalSidecar = TEXT(R"json({
  "assetId": "vtmb:model:scenery/physics/cannister/cannister01",
  "shape": "static"
})json");

	UStaticMesh* Mesh = NewMesh();
	FString Error;
	UElysiumModelProvenance* Record = UElysiumModelProvenance::ApplyJson(Mesh, MinimalSidecar, Error);
	if (!Record)
	{
		AddError(FString::Printf(TEXT("ApplyJson rejected the minimal sidecar: %s"), *Error));
		return false;
	}

	TestEqual(TEXT("AssetId still reads"), Record->AssetId,
		FString(TEXT("vtmb:model:scenery/physics/cannister/cannister01")));
	TestEqual(TEXT("Shape still reads"), Record->Shape, FString(TEXT("static")));

	TestTrue(TEXT("ModelPath defaults empty"), Record->ModelPath.IsEmpty());
	TestTrue(TEXT("SourceSha256 defaults empty"), Record->SourceSha256.IsEmpty());
	TestTrue(TEXT("Roles defaults empty"), Record->Roles.IsEmpty());
	TestTrue(TEXT("Slots defaults empty"), Record->Slots.IsEmpty());
	TestTrue(TEXT("SkinFamilies defaults empty"), Record->SkinFamilies.IsEmpty());
	TestTrue(TEXT("Lods defaults empty"), Record->Lods.IsEmpty());
	TestFalse(TEXT("bNanite defaults false"), Record->bNanite);
	TestEqual(TEXT("HullCount defaults zero"), Record->HullCount, 0);
	TestEqual(TEXT("MassKg defaults zero"), Record->MassKg, 0.0f);
	TestEqual(TEXT("HullBounds.Min defaults zero"), Record->HullBounds.Min, FVector::ZeroVector);
	TestTrue(TEXT("SurfaceProperty defaults empty"), Record->SurfaceProperty.IsEmpty());
	TestTrue(TEXT("Anomalies defaults empty"), Record->Anomalies.IsEmpty());
	TestTrue(TEXT("Omissions defaults empty"), Record->Omissions.IsEmpty());
	TestTrue(TEXT("Coverage defaults empty"), Record->Coverage.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelProvenanceReapplyReplacesTest,
	"Elysium.Substrate.ModelProvenance.ReapplyReplaces", GElysiumModelProvenanceTestFlags)
bool FElysiumModelProvenanceReapplyReplacesTest::RunTest(const FString&)
{
	UStaticMesh* Mesh = NewMesh();
	FString Error;

	UElysiumModelProvenance* First = UElysiumModelProvenance::ApplyJson(Mesh, GModelSidecar, Error);
	if (!First)
	{
		AddError(FString::Printf(TEXT("first ApplyJson failed: %s"), *Error));
		return false;
	}

	const TCHAR* SecondSidecar = TEXT(R"json({
  "assetId": "vtmb:model:scenery/furniture/fancybed/fancybed",
  "shape": "static",
  "collisionMode": "bbox",
  "hullBounds": {"min": [-50.0, -80.0, 0.0], "max": [50.0, 80.0, 60.0]}
})json");

	UElysiumModelProvenance* Second = UElysiumModelProvenance::ApplyJson(Mesh, SecondSidecar, Error);
	if (!Second)
	{
		AddError(FString::Printf(TEXT("second ApplyJson failed: %s"), *Error));
		return false;
	}
	TestTrue(TEXT("re-apply produces a distinct record"), Second != First);
	TestEqual(TEXT("second record's AssetId"), Second->AssetId,
		FString(TEXT("vtmb:model:scenery/furniture/fancybed/fancybed")));
	TestEqual(TEXT("second record's CollisionMode"), Second->CollisionMode, FString(TEXT("bbox")));
	TestTrue(TEXT("second record's Slots is the new (empty) set, not a merge"), Second->Slots.IsEmpty());

	// Replaces rather than accumulates: the mesh carries exactly one record, and it is the second one.
	const UElysiumModelProvenance* Found = UElysiumModelProvenance::Find(Mesh);
	TestTrue(TEXT("Find answers the second record"), Found == Second);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelProvenanceRejectsNonObjectTest,
	"Elysium.Substrate.ModelProvenance.RejectsNonObject", GElysiumModelProvenanceTestFlags)
bool FElysiumModelProvenanceRejectsNonObjectTest::RunTest(const FString&)
{
	UStaticMesh* Mesh = NewMesh();
	FString Error;

	for (const TCHAR* Body : {TEXT("[]"), TEXT("5"), TEXT("\"just a string\""), TEXT("not json at all")})
	{
		UElysiumModelProvenance* Record = UElysiumModelProvenance::ApplyJson(Mesh, Body, Error);
		TestNull(*FString::Printf(TEXT("ApplyJson('%s') returns null"), Body), Record);
		TestFalse(*FString::Printf(TEXT("ApplyJson('%s') names a reason"), Body), Error.IsEmpty());
	}

	// A null mesh is refused too.
	UElysiumModelProvenance* NullResult = UElysiumModelProvenance::ApplyJson(nullptr, GModelSidecar, Error);
	TestNull(TEXT("ApplyJson(nullptr, ...) returns null"), NullResult);
	TestFalse(TEXT("null mesh names a reason"), Error.IsEmpty());

	// No record was ever attached.
	TestNull(TEXT("Find answers null"), UElysiumModelProvenance::Find(Mesh));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelProvenanceRegistryTagsTest,
	"Elysium.Substrate.ModelProvenance.RegistryTags", GElysiumModelProvenanceTestFlags)
bool FElysiumModelProvenanceRegistryTagsTest::RunTest(const FString&)
{
	// A throwaway package: the stamp writes package metadata, and the transient package outlives
	// this test, so the keys must not land there.
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/ElysiumModelProvenanceTest_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	Package->SetFlags(RF_Transient);
	UStaticMesh* Mesh = NewObject<UStaticMesh>(Package, NAME_None, RF_Transient);
	FString Error;

	// Without a record there is nothing to publish.
	bool bStamped = true;
	UElysiumModelProvenance::StampRegistryTags(Mesh, bStamped, Error);
	TestFalse(TEXT("no record, no stamp"), bStamped);
	TestFalse(TEXT("no record names a reason"), Error.IsEmpty());

	UElysiumModelProvenance::ApplyJson(Mesh, GModelSidecar, Error);
#if WITH_EDITORONLY_DATA
	UElysiumModelProvenance::StampRegistryTags(Mesh, bStamped, Error);
	if (!bStamped)
	{
		AddError(FString::Printf(TEXT("StampRegistryTags failed: %s"), *Error));
		return false;
	}
	FMetaData& Meta = Mesh->GetPackage()->GetMetaData();
	TestEqual(TEXT("ElysiumAssetId"), Meta.GetValue(Mesh, UElysiumModelProvenance::TagAssetId),
		FString(TEXT("vtmb:model:character/npc/unique/downtown/lacroix/lacroix")));
	TestEqual(TEXT("ElysiumModelShape"), Meta.GetValue(Mesh, UElysiumModelProvenance::TagModelShape),
		FString(TEXT("skeletal")));
	TestEqual(TEXT("ElysiumNanite"), Meta.GetValue(Mesh, UElysiumModelProvenance::TagNanite), FString(TEXT("true")));
	for (const FName& Tag : { UElysiumModelProvenance::TagAssetId, UElysiumModelProvenance::TagModelShape,
		UElysiumModelProvenance::TagNanite })
	{
		Meta.RemoveValue(Mesh, Tag);
	}
#else
	UElysiumModelProvenance::StampRegistryTags(Mesh, bStamped, Error);
	TestFalse(TEXT("editor-only outside the editor"), bStamped);
#endif
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
