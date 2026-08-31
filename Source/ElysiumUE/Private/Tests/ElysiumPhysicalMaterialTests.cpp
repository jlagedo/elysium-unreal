// Content-free Substrate automation for UElysiumPhysicalMaterial: the flattened surface entry a
// baked physical material carries from its `vtmb:surface-property:` unit, and the provenance record
// that says which entry in the base chain authored each field
// (docs/architecture/seam_map_surface_property.md -> "Import").
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumPhysicalMaterial.h"
#include "ElysiumSurfacePropertyProvenance.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#if WITH_EDITORONLY_DATA
#include "UObject/MetaData.h"
#endif

static constexpr EAutomationTestFlags GElysiumPhysicalMaterialTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// A sidecar in the shape the offline stage writes for `canister`, the deepest chain in the
	// table (canister -> metalpanel -> metalgrate -> metal): every section present so each parser
	// branch is exercised, with values that are not the class defaults. `elasticity` is above the
	// engine's 0-1 restitution range on purpose, and the impact matrix carries a two-deep
	// variation pool.
	const TCHAR* GSidecar = TEXT(R"json({
  "assetId": "vtmb:surface-property:canister",
  "name": "canister",
  "sourceName": "Canister",
  "assetPath": "/ElysiumBaked/SurfaceProperties/PM_canister",
  "unitGlb": "surface-properties/canister.glb",
  "unitSchemaVersion": "1.0.0",
  "unitSha256": "unit-sha",
  "settingsVersion": "elysium-surfaceproperty-import-v2",
  "baseChain": ["metal", "metalgrate", "metalpanel"],
  "gameMaterial": "M",
  "surfaceType": "SurfaceType7",
  "physics": {"friction": 0.8, "elasticity": 2.0, "density": 2.7, "rawDensity": 2700.0, "thickness": 0.1},
  "movement": {"maxSpeedFactor": 0.5, "jumpFactor": 0.25, "climbable": true},
  "footsteps": {
    "left": ["vtmb:sound:surfaces/metal/stepleft1.wav", "vtmb:sound:surfaces/metal/stepleft2.wav"],
    "right": ["vtmb:sound:surfaces/metal/stepright1.wav"]
  },
  "impacts": {
    "bullet": {"soak": ["vtmb:sound:a.wav"],
               "norm": ["vtmb:sound:b.wav", "vtmb:sound:c.wav"],
               "crit": []},
    "blade": {"soak": [], "norm": ["vtmb:sound:d.wav"], "crit": []}
  },
  "bulletImpactLegacy": ["vtmb:sound:surfaces/paper/impact1.wav"],
  "sounds": {"impact": ["vtmb:sound-script:metal.impact"],
             "scrape": ["vtmb:sound-script:metal.scrape"]},
  "fieldOrigins": {
    "physics.friction": "metal",
    "physics.elasticity": "metalgrate",
    "physics.density": "metal",
    "physics.thickness": "canister",
    "gameMaterial": "metal",
    "footsteps.left": "metalpanel",
    "impacts.bullet.norm": "canister"
  },
  "anomalies": ["repeated-scalar-key key=friction offset=42"],
  "coverage": {"percent": 100.0, "unresolved": [], "unsupported": []}
})json");

	UElysiumPhysicalMaterial* NewSurface()
	{
		return NewObject<UElysiumPhysicalMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPhysicalMaterialApplyJsonTest,
	"Elysium.Substrate.PhysicalMaterial.ApplyJson", GElysiumPhysicalMaterialTestFlags)
bool FElysiumPhysicalMaterialApplyJsonTest::RunTest(const FString&)
{
	UElysiumPhysicalMaterial* Surface = NewSurface();

	bool bOk = false;
	FString Error;
	UElysiumPhysicalMaterial::ApplyJson(Surface, GSidecar, bOk, Error);
	if (!bOk)
	{
		AddError(FString::Printf(TEXT("ApplyJson rejected the sidecar: %s"), *Error));
		return false;
	}
	TestTrue(TEXT("no error on success"), Error.IsEmpty());

	// Native slots.
	TestEqual(TEXT("Friction"), Surface->Friction, 0.8f);
	// `density` in the sidecar is already the engine's g/cm3 (the stage divides the table's kg/m3
	// by 1000); `RawDensity` is the authored kg/m3 value, mirroring RawElasticity.
	TestEqual(TEXT("Density"), Surface->Density, 2.7f);
	TestEqual(TEXT("RawDensity"), Surface->RawDensity, 2700.0f);
	TestEqual(TEXT("SurfaceType"), int32(Surface->SurfaceType.GetValue()), int32(SurfaceType7));

	// Elysium slots.
	TestEqual(TEXT("Thickness"), Surface->Thickness, 0.1f);
	TestEqual(TEXT("MaxSpeedFactor"), Surface->MaxSpeedFactor, 0.5f);
	TestEqual(TEXT("JumpFactor"), Surface->JumpFactor, 0.25f);
	TestTrue(TEXT("bClimbable"), Surface->bClimbable);
	TestEqual(TEXT("GameMaterial"), Surface->GameMaterial, FString(TEXT("M")));
	TestEqual(TEXT("SourceName"), Surface->SourceName, FString(TEXT("Canister")));
	TestEqual(TEXT("AssetId"), Surface->AssetId, FString(TEXT("vtmb:surface-property:canister")));

	// The chain is root first, and the unit itself is not in it.
	if (TestEqual(TEXT("BaseChain length"), Surface->BaseChain.Num(), 3))
	{
		TestEqual(TEXT("BaseChain root first"), Surface->BaseChain[0], FString(TEXT("metal")));
		TestEqual(TEXT("BaseChain leaf-most base last"), Surface->BaseChain[2], FString(TEXT("metalpanel")));
	}

	// Pools keep their source order and their length: a repeat is a variation alternate, not an
	// override, so collapsing one would make every footstep identical.
	if (TestEqual(TEXT("FootstepsLeft pool"), Surface->FootstepsLeft.Num(), 2))
	{
		TestEqual(TEXT("first left step"), Surface->FootstepsLeft[0],
			FString(TEXT("vtmb:sound:surfaces/metal/stepleft1.wav")));
	}
	TestEqual(TEXT("FootstepsRight pool"), Surface->FootstepsRight.Num(), 1);
	TestEqual(TEXT("BulletImpactLegacy"), Surface->BulletImpactLegacy.Num(), 1);
	TestEqual(TEXT("SoundScriptImpact"), Surface->SoundScriptImpact.Num(), 1);
	TestEqual(TEXT("SoundScriptScrape"), Surface->SoundScriptScrape.Num(), 1);

	TestEqual(TEXT("two weapon rows"), Surface->Impacts.Num(), 2);
	if (const FElysiumImpactOutcomes* Bullet = Surface->Impacts.Find(TEXT("bullet")))
	{
		TestEqual(TEXT("bullet soak"), Bullet->Soak.Num(), 1);
		TestEqual(TEXT("bullet norm pool"), Bullet->Norm.Num(), 2);
		TestEqual(TEXT("bullet crit empty"), Bullet->Crit.Num(), 0);
	}
	else
	{
		AddError(TEXT("the impact matrix has no bullet row"));
	}

	// Provenance rides along, outered to the material so it saves inside the asset's package.
	const UElysiumSurfacePropertyProvenance* Record = UElysiumPhysicalMaterial::FindProvenance(Surface);
	if (!Record)
	{
		AddError(TEXT("ApplyJson attached no provenance record"));
		return false;
	}
	TestTrue(TEXT("the record is outered to the material"), Record->GetOuter() == Surface);
	TestEqual(TEXT("record AssetId"), Record->AssetId, FString(TEXT("vtmb:surface-property:canister")));
	TestEqual(TEXT("record SourceName"), Record->SourceName, FString(TEXT("Canister")));
	TestEqual(TEXT("record UnitGlb"), Record->UnitGlb, FString(TEXT("surface-properties/canister.glb")));
	TestEqual(TEXT("record UnitSha256"), Record->UnitSha256, FString(TEXT("unit-sha")));
	TestEqual(TEXT("record SettingsVersion"), Record->SettingsVersion,
		FString(TEXT("elysium-surfaceproperty-import-v2")));
	TestEqual(TEXT("record BaseChain"), Record->BaseChain.Num(), 3);
	TestEqual(TEXT("record Anomalies"), Record->Anomalies.Num(), 1);
	TestEqual(TEXT("record CoveragePercent"), Record->CoveragePercent, 100.0f);

	// The origin map is what makes a flattened asset auditable: each field names the entry in the
	// chain that declared it, and an inherited value never claims to be the unit's own.
	if (const FString* Origin = Record->FieldOrigins.Find(TEXT("physics.friction")))
	{
		TestEqual(TEXT("friction came from the chain root"), *Origin, FString(TEXT("metal")));
	}
	else
	{
		AddError(TEXT("FieldOrigins has no row for physics.friction"));
	}
	if (const FString* Origin = Record->FieldOrigins.Find(TEXT("physics.thickness")))
	{
		TestEqual(TEXT("thickness is the unit's own"), *Origin, FString(TEXT("canister")));
	}
	TestEqual(TEXT("FieldOrigins rows"), Record->FieldOrigins.Num(), 7);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPhysicalMaterialClampsTest,
	"Elysium.Substrate.PhysicalMaterial.Clamps", GElysiumPhysicalMaterialTestFlags)
bool FElysiumPhysicalMaterialClampsTest::RunTest(const FString&)
{
	// `elasticity` runs to 2 in the shipped table and down to 0.001, and the engine's Restitution
	// is a 0-1 bounciness. The asset carries the clamp the solver can use and the number the entry
	// actually wrote, so a value outside the range is recorded rather than lost.
	UElysiumPhysicalMaterial* Surface = NewSurface();
	bool bOk = false;
	FString Error;

	UElysiumPhysicalMaterial::ApplyJson(Surface, GSidecar, bOk, Error);
	TestTrue(TEXT("the sidecar applied"), bOk);
	TestEqual(TEXT("Restitution clamps down to 1"), Surface->Restitution, 1.0f);
	TestEqual(TEXT("RawElasticity keeps the authored 2"), Surface->RawElasticity, 2.0f);

	UElysiumPhysicalMaterial::ApplyJson(Surface, TEXT("{\"physics\": {\"elasticity\": -0.5}}"), bOk, Error);
	TestTrue(TEXT("the negative sidecar applied"), bOk);
	TestEqual(TEXT("Restitution clamps up to 0"), Surface->Restitution, 0.0f);
	TestEqual(TEXT("RawElasticity keeps the authored -0.5"), Surface->RawElasticity, -0.5f);

	// A friction of 100 is in the table too; it is not clamped, because Unreal's Friction is not a
	// 0-1 quantity and clamping it would quietly change a surface the table meant to be extreme.
	UElysiumPhysicalMaterial::ApplyJson(Surface, TEXT("{\"physics\": {\"friction\": 100.0}}"), bOk, Error);
	TestTrue(TEXT("the friction sidecar applied"), bOk);
	TestEqual(TEXT("Friction is carried as written"), Surface->Friction, 100.0f);

	// An undeclared `elasticity` resets Restitution and RawElasticity independently to their own
	// class defaults -- not to a derived zero. Restitution's own default is 0.3 and
	// RawElasticity's is 0.0, so only a declared value ties the two together: a sidecar that
	// declares a value drives both, and one that omits `elasticity` reverts both to their own
	// defaults, whatever the asset carried before.
	const UElysiumPhysicalMaterial* Defaults = GetDefault<UElysiumPhysicalMaterial>();
	UElysiumPhysicalMaterial::ApplyJson(Surface, TEXT("{\"physics\": {\"elasticity\": 0.6}}"), bOk, Error);
	TestTrue(TEXT("the elasticity sidecar applied"), bOk);
	TestEqual(TEXT("Restitution takes the declared elasticity"), Surface->Restitution, 0.6f);
	UElysiumPhysicalMaterial::ApplyJson(Surface, TEXT("{\"physics\": {\"friction\": 0.5}}"), bOk, Error);
	TestTrue(TEXT("the elasticity-less sidecar applied"), bOk);
	TestEqual(TEXT("Restitution resets to the class default, not 0"), Surface->Restitution,
		Defaults->Restitution);
	TestEqual(TEXT("RawElasticity resets to the class default"), Surface->RawElasticity,
		Defaults->RawElasticity);

	// An entry with no `gamematerial` anywhere in its chain gets the default row, not a guess.
	UElysiumPhysicalMaterial* Weapon = NewSurface();
	UElysiumPhysicalMaterial::ApplyJson(
		Weapon, TEXT("{\"assetId\": \"vtmb:surface-property:weapon\", \"gameMaterial\": \"\","
			" \"surfaceType\": \"SurfaceType_Default\"}"), bOk, Error);
	TestTrue(TEXT("the empty root applied"), bOk);
	TestEqual(TEXT("no gameMaterial is SurfaceType_Default"),
		int32(Weapon->SurfaceType.GetValue()), int32(SurfaceType_Default));
	TestTrue(TEXT("no gameMaterial letter"), Weapon->GameMaterial.IsEmpty());
	// A row name the enum does not know is the same answer, never a wrong slot.
	UElysiumPhysicalMaterial::ApplyJson(Weapon, TEXT("{\"surfaceType\": \"SurfaceTypeNope\"}"), bOk, Error);
	TestTrue(TEXT("the unknown row applied"), bOk);
	TestEqual(TEXT("an unknown row is SurfaceType_Default"),
		int32(Weapon->SurfaceType.GetValue()), int32(SurfaceType_Default));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPhysicalMaterialReapplyReplacesTest,
	"Elysium.Substrate.PhysicalMaterial.ReapplyReplaces", GElysiumPhysicalMaterialTestFlags)
bool FElysiumPhysicalMaterialReapplyReplacesTest::RunTest(const FString&)
{
	UElysiumPhysicalMaterial* Surface = NewSurface();
	bool bOk = false;
	FString Error;
	UElysiumPhysicalMaterial::ApplyJson(Surface, GSidecar, bOk, Error);
	TestTrue(TEXT("the first apply landed"), bOk);

	// A re-import re-authors in place. The pools are rebuilt rather than appended to, the record
	// is replaced rather than accumulated, and a physics/movement scalar the second sidecar omits
	// resets to its class default rather than keeping the first apply's value -- the stage emits
	// every one of those keys on every apply, so an absent key means the source stopped declaring
	// it, not that the field is new.
	UElysiumPhysicalMaterial::ApplyJson(Surface, TEXT(R"json({
      "assetId": "vtmb:surface-property:metal",
      "sourceName": "metal",
      "baseChain": [],
      "footsteps": {"left": ["vtmb:sound:one.wav"], "right": []},
      "impacts": {"fist": {"norm": ["vtmb:sound:two.wav"]}},
      "sounds": {},
      "fieldOrigins": {"footsteps.left": "metal"}
    })json"), bOk, Error);
	if (!bOk)
	{
		AddError(FString::Printf(TEXT("the second apply was rejected: %s"), *Error));
		return false;
	}
	TestEqual(TEXT("AssetId replaced"), Surface->AssetId, FString(TEXT("vtmb:surface-property:metal")));
	TestEqual(TEXT("BaseChain replaced"), Surface->BaseChain.Num(), 0);
	TestEqual(TEXT("FootstepsLeft replaced, not appended"), Surface->FootstepsLeft.Num(), 1);
	TestEqual(TEXT("FootstepsRight cleared"), Surface->FootstepsRight.Num(), 0);
	TestEqual(TEXT("the impact matrix is the second sidecar's"), Surface->Impacts.Num(), 1);
	TestTrue(TEXT("the second matrix has the fist row"), Surface->Impacts.Contains(TEXT("fist")));
	TestEqual(TEXT("sound scripts cleared"), Surface->SoundScriptImpact.Num(), 0);
	// An omitted physics block resets every physics scalar to its class default (S2), rather than
	// keeping the first apply's 0.8 -- the omission means the second sidecar's unit declares none.
	const UElysiumPhysicalMaterial* Defaults = GetDefault<UElysiumPhysicalMaterial>();
	TestEqual(TEXT("an omitted physics block resets Friction to the class default"),
		Surface->Friction, Defaults->Friction);
	TestEqual(TEXT("...and Restitution"), Surface->Restitution, Defaults->Restitution);
	TestEqual(TEXT("...and RawElasticity"), Surface->RawElasticity, Defaults->RawElasticity);
	TestEqual(TEXT("...and Density"), Surface->Density, Defaults->Density);
	TestEqual(TEXT("...and RawDensity"), Surface->RawDensity, Defaults->RawDensity);

	int32 Records = 0;
	if (const TArray<UAssetUserData*>* All = Surface->GetAssetUserDataArray())
	{
		for (UAssetUserData* Data : *All)
		{
			Records += Data && Data->IsA<UElysiumSurfacePropertyProvenance>() ? 1 : 0;
		}
	}
	TestEqual(TEXT("exactly one provenance record after re-apply"), Records, 1);
	const UElysiumSurfacePropertyProvenance* Record = UElysiumPhysicalMaterial::FindProvenance(Surface);
	if (Record)
	{
		TestEqual(TEXT("Find follows the replacement"), Record->AssetId,
			FString(TEXT("vtmb:surface-property:metal")));
		TestEqual(TEXT("the record's origin map is the second sidecar's"), Record->FieldOrigins.Num(), 1);
		TestEqual(TEXT("the record's anomalies are cleared"), Record->Anomalies.Num(), 0);
	}
	else
	{
		AddError(TEXT("the second apply attached no record"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPhysicalMaterialRejectsBadJsonTest,
	"Elysium.Substrate.PhysicalMaterial.RejectsBadJson", GElysiumPhysicalMaterialTestFlags)
bool FElysiumPhysicalMaterialRejectsBadJsonTest::RunTest(const FString&)
{
	UElysiumPhysicalMaterial* Surface = NewSurface();
	bool bOk = false;
	FString Error;
	UElysiumPhysicalMaterial::ApplyJson(Surface, GSidecar, bOk, Error);
	TestTrue(TEXT("the good sidecar landed"), bOk);
	const UElysiumSurfacePropertyProvenance* Record = UElysiumPhysicalMaterial::FindProvenance(Surface);

	// Malformed input, and well-formed JSON that is not an object, is refused with a reason and
	// leaves the material as it was.
	const TCHAR* Rejected[] = { TEXT("not json"), TEXT("[]"), TEXT("5"), TEXT("\"x\""), TEXT("") };
	for (const TCHAR* Document : Rejected)
	{
		const FString Label = FString::Printf(TEXT("refused: %s"), Document);
		bOk = true;
		UElysiumPhysicalMaterial::ApplyJson(Surface, Document, bOk, Error);
		TestFalse(*Label, bOk);
		TestFalse(*(Label + TEXT(" names a reason")), Error.IsEmpty());
		TestTrue(*(Label + TEXT(" left the record")),
			UElysiumPhysicalMaterial::FindProvenance(Surface) == Record);
		TestEqual(*(Label + TEXT(" left the values")), Surface->Friction, 0.8f);
	}

	// A null material, and a plain UPhysicalMaterial that is not one of ours, are refused by name
	// rather than half-applied: the importer creates the asset with the Elysium class, so a stock
	// physical material at the target path is a defect, not a thing to fill in.
	bOk = true;
	UElysiumPhysicalMaterial::ApplyJson(nullptr, GSidecar, bOk, Error);
	TestFalse(TEXT("null material refused"), bOk);
	TestFalse(TEXT("null material names a reason"), Error.IsEmpty());

	UPhysicalMaterial* Stock = NewObject<UPhysicalMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	bOk = true;
	UElysiumPhysicalMaterial::ApplyJson(Stock, GSidecar, bOk, Error);
	TestFalse(TEXT("a stock UPhysicalMaterial is refused"), bOk);
	TestFalse(TEXT("the wrong class names a reason"), Error.IsEmpty());
	TestNull(TEXT("nothing was attached to the stock material"),
		UElysiumPhysicalMaterial::FindProvenance(Stock));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPhysicalMaterialRegistryTagsTest,
	"Elysium.Substrate.PhysicalMaterial.RegistryTags", GElysiumPhysicalMaterialTestFlags)
bool FElysiumPhysicalMaterialRegistryTagsTest::RunTest(const FString&)
{
	// A throwaway package: the stamp writes package metadata, and the transient package outlives
	// this test, so the keys must not land there.
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/ElysiumPhysicalMaterialTest_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	Package->SetFlags(RF_Transient);
	UElysiumPhysicalMaterial* Surface = NewObject<UElysiumPhysicalMaterial>(Package, NAME_None, RF_Transient);
	FString Error;

	// Without a record there is nothing to publish.
	bool bStamped = true;
	UElysiumPhysicalMaterial::StampRegistryTags(Surface, bStamped, Error);
	TestFalse(TEXT("no record, no stamp"), bStamped);
	TestFalse(TEXT("no record names a reason"), Error.IsEmpty());

	bool bOk = false;
	UElysiumPhysicalMaterial::ApplyJson(Surface, GSidecar, bOk, Error);
	TestTrue(TEXT("the sidecar applied"), bOk);
#if WITH_EDITORONLY_DATA
	UElysiumPhysicalMaterial::StampRegistryTags(Surface, bStamped, Error);
	if (!bStamped)
	{
		AddError(FString::Printf(TEXT("StampRegistryTags failed: %s"), *Error));
		return false;
	}
	FMetaData& Meta = Surface->GetPackage()->GetMetaData();
	TestEqual(TEXT("ElysiumAssetId"), Meta.GetValue(Surface, UElysiumPhysicalMaterial::TagAssetId),
		FString(TEXT("vtmb:surface-property:canister")));
	TestEqual(TEXT("ElysiumGameMaterial"), Meta.GetValue(Surface, UElysiumPhysicalMaterial::TagGameMaterial),
		FString(TEXT("M")));
	TestEqual(TEXT("ElysiumSourceName"), Meta.GetValue(Surface, UElysiumPhysicalMaterial::TagSourceName),
		FString(TEXT("Canister")));
	for (const FName& Tag : { UElysiumPhysicalMaterial::TagAssetId,
		UElysiumPhysicalMaterial::TagGameMaterial, UElysiumPhysicalMaterial::TagSourceName })
	{
		Meta.RemoveValue(Surface, Tag);
	}
#else
	UElysiumPhysicalMaterial::StampRegistryTags(Surface, bStamped, Error);
	TestFalse(TEXT("editor-only outside the editor"), bStamped);
#endif

	// A stock physical material has no record to publish, whatever the target is.
	UPhysicalMaterial* Stock = NewObject<UPhysicalMaterial>(Package, NAME_None, RF_Transient);
	bStamped = true;
	UElysiumPhysicalMaterial::StampRegistryTags(Stock, bStamped, Error);
	TestFalse(TEXT("a stock material carries no record"), bStamped);
	bStamped = true;
	UElysiumPhysicalMaterial::StampRegistryTags(nullptr, bStamped, Error);
	TestFalse(TEXT("null material refused"), bStamped);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
