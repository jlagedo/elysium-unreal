// Content-free Substrate automation for UElysiumMaterialProvenance: the record a baked material
// instance carries from its `vtmb:material:` unit (docs/architecture/seam_map_material.md ->
// "Import" -> "Provenance").
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumMaterialProvenance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#if WITH_EDITORONLY_DATA
#include "UObject/MetaData.h"
#endif

static constexpr EAutomationTestFlags GElysiumMaterialProvenanceTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// Trimmed from a real staged sidecar -- brick/floorasan.provenance.json, a Lit unit with a
	// non-empty proxies[] and an $envmap -- copied out of a real `uv run elysium import materials`
	// stage run (E:\elysium-work\import\materials\brick\floorasan.provenance.json) rather than
	// hand-authored, so the shape here is exactly what `stage_unit` writes, not what the reader
	// used to assume it wrote (C2). `assetPath`/`unitGlb`/`sourceSha256`/`physMaterial` are added
	// as the top-level keys `pipeline/unreal/import_materials.py` merges in from the manifest
	// entry before calling `ApplyJson` -- see `FromJson`'s "--- identity ---" comment.
	const TCHAR* GSidecar = TEXT(R"json({
  "anomalies": [],
  "assetId": "vtmb:material:brick/floorasan",
  "assetPath": "/ElysiumBaked/Materials/brick/MI_floorasan",
  "blendMode": "Opaque",
  "comments": [],
  "coverage": {"totalKeys": 4, "unmappedKeys": []},
  "curve": null,
  "environment": {"envMapSymbol": "env_cubemap", "envMapTintChromatic": false},
  "ignoreZ": false,
  "ignoreZNamedDivergence": false,
  "isDecalSurface": false,
  "master": "/Game/ElysiumGenerated/Materials/V2/M_V2_Lit",
  "materialPath": "brick/floorasan",
  "materialReferences": [],
  "maxLight": null,
  "minLight": null,
  "omissions": [
    {"reason": "separator-bytes-carry-no-keyvalues-meaning", "role": "keyvalues-insignificant-whitespace"}
  ],
  "parameters": [
    {"block": "", "index": 0, "key": "$basetexture", "sourceKey": "$basetexture", "value": "brick/floora", "valueType": "string"},
    {"block": "", "index": 1, "key": "$surfaceprop", "sourceKey": "$surfaceprop", "value": "brick", "valueType": "string"},
    {"block": "", "index": 2, "key": "$envmap", "sourceKey": "$envmap", "value": "env_cubemap", "valueType": "string"},
    {"block": "", "index": 3, "key": "$envmapmask", "sourceKey": "$envmapmask", "value": "brick/floora_ref", "valueType": "string"}
  ],
  "patchBase": null,
  "patchKind": null,
  "patchOf": null,
  "patched": false,
  "physMaterial": "/ElysiumBaked/SurfaceProperties/PM_brick",
  "physMaterialFallback": false,
  "proxies": [
    {"arguments": {"resultvar": "$envmaptint[0]", "scale": "1.0"}, "destination": "runtime", "index": 0, "kind": "globalwetness", "sourceName": "GlobalWetness"},
    {"arguments": {"resultvar": "$envmaptint[1]", "scale": "1.0"}, "destination": "runtime", "index": 1, "kind": "globalwetness", "sourceName": "GlobalWetness"}
  ],
  "runtime": [
    {"arguments": {"resultvar": "$envmaptint[0]", "scale": "1.0"}, "kind": "globalwetness"}
  ],
  "settingsVersion": "elysium-material-import-v2",
  "shader": "lightmappedgeneric",
  "shaderFamily": "lightmappedgeneric",
  "shaderResolved": true,
  "sourceShader": "LightmappedGeneric",
  "sourceSha256": "source-sha",
  "spriteOrientation": null,
  "spriteOrigin": null,
  "subdivSize": null,
  "surfaceClass": "brick",
  "surfaceClassIndex": 7,
  "surfaceClassSource": "surfaceprop",
  "textureBindings": [
    {"asset": "vtmb:texture:brick/floora", "parameter": "BaseTexture"},
    {"asset": "vtmb:texture:brick/floora_ref", "parameter": "EnvMapMask"}
  ],
  "twoSided": false,
  "unitGlb": "materials/brick/floorasan.glb",
  "unitSchemaVersion": "1.1.0",
  "unitSha256": "73ea5df0614356dabd15cad310024a6b36de7bfe3419fd7f0152855f62ed80fd",
  "wetnessScale": 1.0
})json");

	UMaterialInstanceConstant* NewInstance()
	{
		return NewObject<UMaterialInstanceConstant>(GetTransientPackage(), NAME_None, RF_Transient);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMaterialProvenanceApplyJsonTest,
	"Elysium.Substrate.MaterialProvenance.ApplyJson", GElysiumMaterialProvenanceTestFlags)
bool FElysiumMaterialProvenanceApplyJsonTest::RunTest(const FString&)
{
	UMaterialInstanceConstant* Instance = NewInstance();

	FString Error;
	UElysiumMaterialProvenance* Record = UElysiumMaterialProvenance::ApplyJson(Instance, GSidecar, Error);
	if (!Record)
	{
		AddError(FString::Printf(TEXT("ApplyJson rejected the sidecar: %s"), *Error));
		return false;
	}
	TestTrue(TEXT("no error on success"), Error.IsEmpty());
	TestTrue(TEXT("the record is outered to the material"), Record->GetOuter() == Instance);

	// --- identity: AssetId/MaterialPath/UnitSchemaVersion/UnitSha256/SettingsVersion come from the
	// sidecar proper; AssetPath/UnitGlb/SourceSha256 come from the manifest-entry keys the importer
	// merges in.
	TestEqual(TEXT("AssetId"), Record->AssetId, FString(TEXT("vtmb:material:brick/floorasan")));
	TestEqual(TEXT("MaterialPath"), Record->MaterialPath, FString(TEXT("brick/floorasan")));
	TestEqual(TEXT("AssetPath (merged from the manifest entry)"), Record->AssetPath,
		FString(TEXT("/ElysiumBaked/Materials/brick/MI_floorasan")));
	TestEqual(TEXT("UnitGlb (merged from the manifest entry)"), Record->UnitGlb,
		FString(TEXT("materials/brick/floorasan.glb")));
	TestEqual(TEXT("UnitSha256"), Record->UnitSha256,
		FString(TEXT("73ea5df0614356dabd15cad310024a6b36de7bfe3419fd7f0152855f62ed80fd")));
	TestEqual(TEXT("SourceSha256 (merged from the manifest entry)"), Record->SourceSha256, FString(TEXT("source-sha")));
	TestEqual(TEXT("SettingsVersion"), Record->SettingsVersion, FString(TEXT("elysium-material-import-v2")));

	// --- shader: top-level shaderFamily/shaderResolved, not a nested shaderResolution object.
	TestEqual(TEXT("Shader"), Record->Shader, FString(TEXT("lightmappedgeneric")));
	TestEqual(TEXT("SourceShader"), Record->SourceShader, FString(TEXT("LightmappedGeneric")));
	TestEqual(TEXT("ResolvedFamily (from top-level shaderFamily)"), Record->ResolvedFamily, FString(TEXT("lightmappedgeneric")));
	TestTrue(TEXT("ShaderResolved"), Record->ShaderResolved);

	// --- build decisions ---
	TestEqual(TEXT("Master"), Record->Master, FString(TEXT("/Game/ElysiumGenerated/Materials/V2/M_V2_Lit")));
	TestEqual(TEXT("BlendMode"), Record->BlendMode, FString(TEXT("Opaque")));
	TestFalse(TEXT("TwoSided"), Record->TwoSided);
	TestEqual(TEXT("SurfaceClass"), Record->SurfaceClass, FName(TEXT("brick")));
	TestEqual(TEXT("SurfaceClassIndex"), Record->SurfaceClassIndex, 7);
	TestEqual(TEXT("SurfaceClassSource"), Record->SurfaceClassSource, FString(TEXT("surfaceprop")));
	TestFalse(TEXT("PhysMaterialFallback"), Record->PhysMaterialFallback);
	TestEqual(TEXT("EnvMapSymbol (from the environment{} object)"), Record->EnvMapSymbol, FString(TEXT("env_cubemap")));
	TestEqual(TEXT("SurfacePropertyAsset (merged from the manifest entry's physMaterial)"),
		Record->SurfacePropertyAsset, FString(TEXT("/ElysiumBaked/SurfaceProperties/PM_brick")));
	TestTrue(TEXT("PatchOf empty (install unit)"), Record->PatchOf.IsEmpty());
	TestTrue(TEXT("PatchKind empty (install unit)"), Record->PatchKind.IsEmpty());

	// --- placement / map / runtime-factory ---
	TestFalse(TEXT("IsDecalSurface"), Record->IsDecalSurface);
	TestFalse(TEXT("IgnoreZ"), Record->IgnoreZ);
	TestEqual(TEXT("WetnessScale"), Record->WetnessScale, 1.0f);

	// --- content ---
	if (TestEqual(TEXT("Parameters count"), Record->Parameters.Num(), 4))
	{
		TestEqual(TEXT("parameter 0 key"), Record->Parameters[0].Key, FString(TEXT("$basetexture")));
		TestEqual(TEXT("parameter 0 value"), Record->Parameters[0].Value, FString(TEXT("brick/floora")));
		TestEqual(TEXT("parameter 1 sourceKey"), Record->Parameters[1].SourceKey, FString(TEXT("$surfaceprop")));
	}
	TestEqual(TEXT("Blocks count (not part of this stage's sidecar)"), Record->Blocks.Num(), 0);

	if (TestEqual(TEXT("Proxies count"), Record->Proxies.Num(), 2))
	{
		TestEqual(TEXT("proxy 0 kind"), Record->Proxies[0].Kind, FString(TEXT("globalwetness")));
		TestEqual(TEXT("proxy 0 sourceName"), Record->Proxies[0].SourceName, FString(TEXT("GlobalWetness")));
		TestEqual(TEXT("proxy 0 destination"), Record->Proxies[0].Destination, FString(TEXT("runtime")));
		if (const FString* Scale = Record->Proxies[0].Arguments.Find(TEXT("scale")))
		{
			TestEqual(TEXT("proxy 0 argument scale"), *Scale, FString(TEXT("1.0")));
		}
		else
		{
			AddError(TEXT("proxy 0 has no scale argument"));
		}
	}

	if (TestEqual(TEXT("TextureBindings count"), Record->TextureBindings.Num(), 2))
	{
		TestEqual(TEXT("binding 0 parameter"), Record->TextureBindings[0].Parameter, FString(TEXT("BaseTexture")));
		TestEqual(TEXT("binding 0 asset"), Record->TextureBindings[0].Asset, FString(TEXT("vtmb:texture:brick/floora")));
	}
	TestEqual(TEXT("Dependencies count (materialReferences[] was empty)"), Record->Dependencies.Num(), 0);
	TestEqual(TEXT("Omissions count"), Record->Omissions.Num(), 1);
	TestEqual(TEXT("CoverageTotalKeys"), Record->CoverageTotalKeys, 4);
	TestTrue(TEXT("CoverageUnmappedKeys empty"), Record->CoverageUnmappedKeys.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMaterialProvenanceReapplyReplacesTest,
	"Elysium.Substrate.MaterialProvenance.ReapplyReplaces", GElysiumMaterialProvenanceTestFlags)
bool FElysiumMaterialProvenanceReapplyReplacesTest::RunTest(const FString&)
{
	UMaterialInstanceConstant* Instance = NewInstance();
	FString Error;

	UElysiumMaterialProvenance* First = UElysiumMaterialProvenance::ApplyJson(Instance, GSidecar, Error);
	if (!First)
	{
		AddError(FString::Printf(TEXT("first ApplyJson failed: %s"), *Error));
		return false;
	}

	const TCHAR* SecondSidecar = TEXT(R"json({
  "assetId": "vtmb:material:brick/floorasan",
  "master": "/Game/ElysiumGenerated/Materials/V2/M_V2_LitTranslucent",
  "surfaceClass": "wood",
  "surfaceClassIndex": 70,
  "parameters": [
    {"index": 0, "block": "", "key": "$basetexture", "sourceKey": "$basetexture", "value": "wood/plank", "valueType": "string"}
  ]
})json");

	UElysiumMaterialProvenance* Second = UElysiumMaterialProvenance::ApplyJson(Instance, SecondSidecar, Error);
	if (!Second)
	{
		AddError(FString::Printf(TEXT("second ApplyJson failed: %s"), *Error));
		return false;
	}
	TestTrue(TEXT("re-apply produces a distinct record"), Second != First);
	TestEqual(TEXT("second record's Master"), Second->Master, FString(TEXT("/Game/ElysiumGenerated/Materials/V2/M_V2_LitTranslucent")));
	TestEqual(TEXT("second record's SurfaceClass"), Second->SurfaceClass, FName(TEXT("wood")));
	TestEqual(TEXT("second record's Parameters is the new set, not a merge"), Second->Parameters.Num(), 1);

	// Replaces rather than accumulates: the material carries exactly one record, and it is the
	// second one.
	const UElysiumMaterialProvenance* Found = UElysiumMaterialProvenance::Find(Instance);
	TestTrue(TEXT("Find answers the second record"), Found == Second);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMaterialProvenanceRejectsNonObjectTest,
	"Elysium.Substrate.MaterialProvenance.RejectsNonObject", GElysiumMaterialProvenanceTestFlags)
bool FElysiumMaterialProvenanceRejectsNonObjectTest::RunTest(const FString&)
{
	UMaterialInstanceConstant* Instance = NewInstance();
	FString Error;

	for (const TCHAR* Body : {TEXT("[]"), TEXT("5"), TEXT("\"just a string\""), TEXT("not json at all")})
	{
		UElysiumMaterialProvenance* Record = UElysiumMaterialProvenance::ApplyJson(Instance, Body, Error);
		TestNull(*FString::Printf(TEXT("ApplyJson('%s') returns null"), Body), Record);
		TestFalse(*FString::Printf(TEXT("ApplyJson('%s') names a reason"), Body), Error.IsEmpty());
	}

	// A null material is refused too.
	UElysiumMaterialProvenance* NullResult = UElysiumMaterialProvenance::ApplyJson(nullptr, GSidecar, Error);
	TestNull(TEXT("ApplyJson(nullptr, ...) returns null"), NullResult);
	TestFalse(TEXT("null material names a reason"), Error.IsEmpty());

	// No record was ever attached.
	TestNull(TEXT("Find answers null"), UElysiumMaterialProvenance::Find(Instance));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMaterialProvenanceRegistryTagsTest,
	"Elysium.Substrate.MaterialProvenance.RegistryTags", GElysiumMaterialProvenanceTestFlags)
bool FElysiumMaterialProvenanceRegistryTagsTest::RunTest(const FString&)
{
	// A throwaway package: the stamp writes package metadata, and the transient package outlives
	// this test, so the keys must not land there.
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Temp/ElysiumMaterialProvenanceTest_%s"),
		*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	Package->SetFlags(RF_Transient);
	UMaterialInstanceConstant* Instance = NewObject<UMaterialInstanceConstant>(Package, NAME_None, RF_Transient);
	FString Error;

	// Without a record there is nothing to publish.
	bool bStamped = true;
	UElysiumMaterialProvenance::StampRegistryTags(Instance, bStamped, Error);
	TestFalse(TEXT("no record, no stamp"), bStamped);
	TestFalse(TEXT("no record names a reason"), Error.IsEmpty());

	UElysiumMaterialProvenance::ApplyJson(Instance, GSidecar, Error);
#if WITH_EDITORONLY_DATA
	UElysiumMaterialProvenance::StampRegistryTags(Instance, bStamped, Error);
	if (!bStamped)
	{
		AddError(FString::Printf(TEXT("StampRegistryTags failed: %s"), *Error));
		return false;
	}
	FMetaData& Meta = Instance->GetPackage()->GetMetaData();
	TestEqual(TEXT("ElysiumAssetId"), Meta.GetValue(Instance, UElysiumMaterialProvenance::TagAssetId),
		FString(TEXT("vtmb:material:brick/floorasan")));
	// This stage's sidecar carries no `resolvedPrograms`, so the published tag falls back to
	// ResolvedFamily (the sidecar's `shaderFamily`).
	TestEqual(TEXT("ElysiumShaderProgram (falls back to ResolvedFamily)"),
		Meta.GetValue(Instance, UElysiumMaterialProvenance::TagShaderProgram), FString(TEXT("lightmappedgeneric")));
	TestEqual(TEXT("ElysiumMaster"), Meta.GetValue(Instance, UElysiumMaterialProvenance::TagMaster),
		FString(TEXT("/Game/ElysiumGenerated/Materials/V2/M_V2_Lit")));
	TestEqual(TEXT("ElysiumSurfaceClass"), Meta.GetValue(Instance, UElysiumMaterialProvenance::TagSurfaceClass),
		FString(TEXT("brick")));
	for (const FName& Tag : { UElysiumMaterialProvenance::TagAssetId, UElysiumMaterialProvenance::TagShaderProgram,
		UElysiumMaterialProvenance::TagMaster, UElysiumMaterialProvenance::TagSurfaceClass })
	{
		Meta.RemoveValue(Instance, Tag);
	}
#else
	UElysiumMaterialProvenance::StampRegistryTags(Instance, bStamped, Error);
	TestFalse(TEXT("editor-only outside the editor"), bStamped);
#endif
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMaterialProvenanceSidecarKeysAreCoveredTest,
	"Elysium.Substrate.MaterialProvenance.SidecarKeysAreCovered", GElysiumMaterialProvenanceTestFlags)
bool FElysiumMaterialProvenanceSidecarKeysAreCoveredTest::RunTest(const FString&)
{
	// C2: every top-level key GSidecar carries must be one `FromJson` actually reads (a key
	// present in a real staged sidecar that the reader silently ignores is exactly the class of
	// defect this test exists to catch); "manifest" is not a real sidecar key, it never appears
	// (the merge adds flat top-level keys, not a nested object -- see FromJson's comment).
	static const TCHAR* SidecarTopLevelKeys[] = {
		TEXT("anomalies"), TEXT("assetId"), TEXT("assetPath"), TEXT("blendMode"), TEXT("comments"),
		TEXT("coverage"), TEXT("curve"), TEXT("environment"), TEXT("ignoreZ"),
		TEXT("ignoreZNamedDivergence"), TEXT("isDecalSurface"), TEXT("master"), TEXT("materialPath"),
		TEXT("materialReferences"), TEXT("maxLight"), TEXT("minLight"), TEXT("omissions"),
		TEXT("parameters"), TEXT("patchBase"), TEXT("patchKind"), TEXT("patchOf"), TEXT("patched"),
		TEXT("physMaterial"), TEXT("physMaterialFallback"), TEXT("proxies"), TEXT("runtime"),
		TEXT("settingsVersion"), TEXT("shader"), TEXT("shaderFamily"), TEXT("shaderResolved"),
		TEXT("sourceShader"), TEXT("sourceSha256"), TEXT("spriteOrientation"), TEXT("spriteOrigin"),
		TEXT("subdivSize"), TEXT("surfaceClass"), TEXT("surfaceClassIndex"), TEXT("surfaceClassSource"),
		TEXT("textureBindings"), TEXT("twoSided"), TEXT("unitGlb"), TEXT("unitSchemaVersion"),
		TEXT("unitSha256"), TEXT("wetnessScale"),
	};
	// Keys FromJson reads, grepped from ElysiumMaterialProvenance.cpp's own Str/Bool/Int/Float/Arr/
	// Obj/Strings calls against O (or a sub-object of O) -- kept in sync by hand, the same
	// discipline the pipeline test file uses in the other direction.
	static const TCHAR* ReadByFromJson[] = {
		TEXT("assetId"), TEXT("materialPath"), TEXT("assetPath"), TEXT("unitGlb"),
		TEXT("unitSchemaVersion"), TEXT("unitSha256"), TEXT("sourceSha256"), TEXT("settingsVersion"),
		TEXT("shader"), TEXT("sourceShader"), TEXT("shaderFamily"), TEXT("shaderResolved"),
		TEXT("master"), TEXT("blendMode"), TEXT("twoSided"), TEXT("surfaceClass"), TEXT("surfaceClassIndex"),
		TEXT("surfaceClassSource"), TEXT("physMaterialFallback"), TEXT("environment"), TEXT("physMaterial"),
		TEXT("patchBase"), TEXT("patchKind"), TEXT("isDecalSurface"), TEXT("ignoreZ"), TEXT("spriteOrigin"),
		TEXT("spriteOrientation"), TEXT("minLight"), TEXT("maxLight"), TEXT("wetnessScale"), TEXT("subdivSize"),
		TEXT("curve"), TEXT("proxies"), TEXT("textureBindings"), TEXT("materialReferences"), TEXT("anomalies"),
		TEXT("omissions"), TEXT("comments"), TEXT("coverage"), TEXT("parameters"),
	};

	TSet<FString> Covered;
	for (const TCHAR* Key : ReadByFromJson)
	{
		Covered.Add(Key);
	}
	// `patched`, `patchOf` (the {x,y,z} coordinate, distinct from PatchOf/patchBase), `runtime` and
	// `ignoreZNamedDivergence` are intentionally not read into any FElysiumMaterialProvenance field
	// today (the first two are redundant with data this record already carries or does not need;
	// `runtime` restates the scalar/vector side effects proxies[] already carries the arguments
	// for; `ignoreZNamedDivergence` is a named-divergence flag for the placement lane's own
	// bookkeeping) -- named here so the test states the omission rather than silently passing it.
	static const TCHAR* KnowinglyUncovered[] = {
		TEXT("patched"), TEXT("patchOf"), TEXT("runtime"), TEXT("ignoreZNamedDivergence"),
	};
	for (const TCHAR* Key : KnowinglyUncovered)
	{
		Covered.Add(Key);
	}

	for (const TCHAR* Key : SidecarTopLevelKeys)
	{
		if (!Covered.Contains(Key))
		{
			AddError(FString::Printf(TEXT("sidecar key %s is not read by FromJson"), Key));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
