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
	// A sidecar in the shape the offline stage writes: every section present so each parser
	// branch is exercised, with values that are not the struct defaults.
	const TCHAR* GSidecar = TEXT(R"json({
  "assetId": "vtmb:material:brick/brickwall001a",
  "materialPath": "brick/brickwall001a",
  "assetPath": "/ElysiumBaked/Materials/brick/MI_brickwall001a",
  "unitGlb": "materials/brick/brickwall001a.glb",
  "unitSchemaVersion": "1.0.0",
  "unitSha256": "unit-sha",
  "sourceSha256": "source-sha",
  "settingsVersion": "elysium-material-import-v1",
  "shader": "vertexlitgeneric_maskedenvmapv2",
  "sourceShader": "",
  "shaderResolution": {
    "family": "vertexlitgeneric",
    "resolutionInputs": ["$envmapmask", "$bumpmap"],
    "resolutionReason": "static switch bUseEnvMapMask=true, bUseBumpMap=false",
    "resolvedPrograms": [
      {"pixelShader": "ps_maskedenvmapv2", "vertexShader": "vs_generic", "condition": "default", "drawPass": "opaque"},
      {"pixelShader": "ps_maskedenvmapv2_bump", "vertexShader": "vs_generic_bump", "condition": "bUseBumpMap=true", "drawPass": "opaque"}
    ]
  },
  "master": "/Game/ElysiumGenerated/Materials/V2/M_V2_Lit",
  "blendMode": "opaque",
  "twoSided": false,
  "surfaceClass": "brick",
  "surfaceClassIndex": 3,
  "envMapSymbol": "env_cubemap",
  "envMapAssetId": "vtmb:texture:maps/sm_hub_1/c1_2_0",
  "envMapProbePath": "/ElysiumBaked/Textures/maps/sm_hub_1/TC_c1_2_0.TC_c1_2_0",
  "surfacePropertyAsset": "/ElysiumBaked/SurfaceProperties/PM_brick",
  "patch": {"asset": "", "kind": ""},
  "parameters": [
    {"index": 0, "block": "root", "key": "BaseTexture", "sourceKey": "$basetexture", "value": "brick/brickwall001a", "valueType": "string", "offset": 12},
    {"index": 1, "block": "root", "key": "SurfaceClassIndex", "sourceKey": "$surfaceprop", "value": "brick", "valueType": "string", "offset": 48}
  ],
  "blocks": [
    {"name": "root", "sourceName": "VertexLitGeneric", "path": "brick/brickwall001a.vmt", "parent": ""}
  ],
  "proxies": [
    {"kind": "sine", "name": "GlowPulse", "sourceName": "Sine", "parameterIndices": [1], "arguments": {"sinemin": "0.1", "sinemax": "0.9"}, "destination": "graph-node"}
  ],
  "textureBindings": [
    {"parameter": "BaseTexture", "value": "brick/brickwall001a", "kind": "color", "asset": "vtmb:texture:brick/brickwall001a", "resolved": true, "usedLinearTwin": false},
    {"parameter": "EnvMask", "value": "brick/brickwall001a_mask", "kind": "mask", "asset": "vtmb:texture:brick/brickwall001a_mask", "resolved": true, "usedLinearTwin": true}
  ],
  "dependencies": [
    {"role": "surface-property", "parameter": "$surfaceprop", "asset": "vtmb:surface-property:brick", "resolved": true}
  ],
  "anomalies": ["repeated-scalar-key key=basetexture offset=12"],
  "omissions": ["v0 lightmap stage dropped -- Lumen owns lighting"],
  "comments": ["worked example, mirrors seam_map_material.md"],
  "coverage": {"percent": 100.0, "unresolved": [], "unsupported": []}
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

	TestEqual(TEXT("AssetId"), Record->AssetId, FString(TEXT("vtmb:material:brick/brickwall001a")));
	TestEqual(TEXT("UnitSha256"), Record->UnitSha256, FString(TEXT("unit-sha")));
	TestEqual(TEXT("SourceSha256"), Record->SourceSha256, FString(TEXT("source-sha")));
	TestEqual(TEXT("Shader"), Record->Shader, FString(TEXT("vertexlitgeneric_maskedenvmapv2")));
	TestEqual(TEXT("ResolvedFamily"), Record->ResolvedFamily, FString(TEXT("vertexlitgeneric")));
	TestEqual(TEXT("ResolutionInputs count"), Record->ResolutionInputs.Num(), 2);
	TestEqual(TEXT("Master"), Record->Master, FString(TEXT("/Game/ElysiumGenerated/Materials/V2/M_V2_Lit")));
	TestEqual(TEXT("SurfaceClass"), Record->SurfaceClass, FName(TEXT("brick")));
	TestEqual(TEXT("SurfaceClassIndex"), Record->SurfaceClassIndex, 3);
	TestEqual(TEXT("EnvMapSymbol"), Record->EnvMapSymbol, FString(TEXT("env_cubemap")));
	TestEqual(TEXT("EnvMapAssetId"), Record->EnvMapAssetId, FString(TEXT("vtmb:texture:maps/sm_hub_1/c1_2_0")));
	TestTrue(TEXT("PatchOf empty (install unit)"), Record->PatchOf.IsEmpty());

	if (TestEqual(TEXT("ResolvedPrograms count"), Record->ResolvedPrograms.Num(), 2))
	{
		TestEqual(TEXT("first program condition"), Record->ResolvedPrograms[0].Condition, FString(TEXT("default")));
		TestEqual(TEXT("first program pixel shader"), Record->ResolvedPrograms[0].PixelShader, FString(TEXT("ps_maskedenvmapv2")));
	}

	if (TestEqual(TEXT("Parameters count"), Record->Parameters.Num(), 2))
	{
		TestEqual(TEXT("parameter 0 key"), Record->Parameters[0].Key, FString(TEXT("BaseTexture")));
		TestEqual(TEXT("parameter 0 source key"), Record->Parameters[0].SourceKey, FString(TEXT("$basetexture")));
		TestEqual(TEXT("parameter 1 offset"), Record->Parameters[1].Offset, 48);
	}
	TestEqual(TEXT("Blocks count"), Record->Blocks.Num(), 1);

	if (TestEqual(TEXT("Proxies count"), Record->Proxies.Num(), 1))
	{
		TestEqual(TEXT("proxy kind"), Record->Proxies[0].Kind, FString(TEXT("sine")));
		TestEqual(TEXT("proxy destination"), Record->Proxies[0].Destination, FString(TEXT("graph-node")));
		if (const FString* SineMin = Record->Proxies[0].Arguments.Find(TEXT("sinemin")))
		{
			TestEqual(TEXT("proxy argument sinemin"), *SineMin, FString(TEXT("0.1")));
		}
		else
		{
			AddError(TEXT("proxy 0 has no sinemin argument"));
		}
	}

	if (TestEqual(TEXT("TextureBindings count"), Record->TextureBindings.Num(), 2))
	{
		TestTrue(TEXT("second binding used its linear twin"), Record->TextureBindings[1].UsedLinearTwin);
	}
	TestEqual(TEXT("Dependencies count"), Record->Dependencies.Num(), 1);
	TestEqual(TEXT("Anomalies count"), Record->Anomalies.Num(), 1);
	TestEqual(TEXT("Omissions count"), Record->Omissions.Num(), 1);
	TestEqual(TEXT("Comments count"), Record->Comments.Num(), 1);
	TestEqual(TEXT("CoveragePercent"), Record->CoveragePercent, 100.0f);

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
  "assetId": "vtmb:material:brick/brickwall001a",
  "master": "/Game/ElysiumGenerated/Materials/V2/M_V2_LitTranslucent",
  "surfaceClass": "wood",
  "surfaceClassIndex": 7,
  "parameters": [
    {"index": 0, "block": "root", "key": "BaseTexture", "sourceKey": "$basetexture", "value": "wood/plank", "valueType": "string", "offset": 0}
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
		FString(TEXT("vtmb:material:brick/brickwall001a")));
	TestEqual(TEXT("ElysiumShaderProgram (default-condition pixel program)"),
		Meta.GetValue(Instance, UElysiumMaterialProvenance::TagShaderProgram), FString(TEXT("ps_maskedenvmapv2")));
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

#endif // WITH_DEV_AUTOMATION_TESTS
