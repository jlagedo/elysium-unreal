#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "ElysiumPhysicsData.h"
#include "ElysiumContentPaths.h"
#include "JsonObjectConverter.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "UObject/UnrealType.h"

namespace
{
	FElysiumPhysicsNamedNumber Number(const TCHAR* Name, double Value)
	{
		FElysiumPhysicsNamedNumber Result; Result.Name = Name; Result.Value = Value; return Result;
	}
	FString Fixture(bool bMissingBone = false)
	{
		FElysiumPhysicsSourceData Data;
		Data.SchemaVersion = TEXT("1.0.0"); Data.AssetId = TEXT("vtmb:model:test/rig");
		Data.AssetPath = TEXT("/ElysiumBaked/Models/test/DA_rig_physics");
		Data.SourceGlbSha256 = FString::ChrN(64, TEXT('a')); Data.StagedBodySha256 = FString::ChrN(64, TEXT('b'));
		Data.GeometryFrame = TEXT("IVP metres, axis-only"); Data.bHasPhysics = true;
		Data.HeaderParameters.Add(Number(TEXT("solidCount"), 1));
		auto& Bone = Data.Bones.AddDefaulted_GetRef();
		Bone.Index = 0; Bone.Parent = -1; Bone.SourceName = TEXT("root"); Bone.NativeName = TEXT("root");
		Bone.PoseToBone = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
		auto& Solid = Data.Solids.AddDefaulted_GetRef();
		Solid.Ordinal = 0; Solid.BinaryIndex = 0; Solid.SourceOffset = 16;
		Solid.AuthoredIndex.bPresent = true; Solid.AuthoredIndex.Value = 7;
		Solid.SourceName = bMissingBone ? TEXT("absent") : TEXT("root");
		Solid.SourceBoneIndex = bMissingBone ? -1 : 0; Solid.NativeBoneName = bMissingBone ? NAME_None : FName(TEXT("root"));
		Solid.MassCenterIvp = {0, 0, 0}; Solid.RotationInertiaIvp = {1, 2, 3};
		Solid.Parameters = {Number(TEXT("index"), 7), Number(TEXT("mass"), 3.125), Number(TEXT("massbias"), 2)};
		auto& Hull = Solid.Hulls.AddDefaulted_GetRef();
		Hull.SolidOrdinal = 0; Hull.LedgeOrdinal = 0; Hull.SourceOffset = 64;
		Hull.PositionAccessor = 0; Hull.IndexAccessor = 1; Hull.Indices = {0, 2, 1};
		Hull.Vertices.SetNum(3); Hull.Vertices[1].X = 1; Hull.Vertices[2].Y = 2;
		auto& KV = Data.KeyValues.AddDefaulted_GetRef(); KV.BlockType = TEXT("future");
		KV.Pairs.SetNum(2); KV.Pairs[0].Key = TEXT("same"); KV.Pairs[0].Value = TEXT("first");
		KV.Pairs[1].Key = TEXT("same"); KV.Pairs[1].Value = TEXT("second");
		if (bMissingBone)
		{
			auto& Gap = Data.Gaps.AddDefaulted_GetRef(); Gap.Kind = TEXT("source-bone"); Gap.Ordinal = 0;
			Gap.Field = TEXT("name"); Gap.SourceName = TEXT("absent"); Gap.Reason = TEXT("source MDL has no such bone");
		}
		Data.SourceEvidenceJson = FString(TEXT(R"JSON({
		 "physics":{"header":{"solidCount":1},"coordinateSystem":"IVP metres, axis-only",
		 "solids":[{"index":0,"sourceOffset":16,"properties":{"index":7,"name":"SOURCE_BONE","mass":3.125,"massbias":2},
		 "massCenter":[0,0,0],"rotationInertia":[1,2,3],"hulls":[{"sourceOffset":64,"positions":0,"indices":1,"unknownLedge":[9,8]}]}],
		 "constraints":[],"editParams":[],"keyValues":[{"type":"future","pairs":[{"key":"same","value":"first"},{"key":"same","value":"second"}]}],
		 "unknown":{"preserve":[null,true,"raw"]}},
		 "bones":[{"index":0,"parent":-1,"name":"root","poseToBone":[1,0,0,0,0,1,0,0,0,0,1,0]}],
		 "accessors":[{"index":0,"accessor":{"count":3}},{"index":1,"accessor":{"count":3}}]})JSON"))
		 .Replace(TEXT("SOURCE_BONE"), *Solid.SourceName);
		FString Json;
		FJsonObjectConverter::UStructToJsonObjectString(Data, Json);
		return Json;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPhysicsSourceDataTest, "Elysium.Substrate.PhysicsSourceData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumPhysicsSourceDataTest::RunTest(const FString&)
{
	const FString Json = Fixture(); FString Error;
	const FString ExpectedPackage = TEXT("/ElysiumBaked/Models/test/DA_rig_physics");
	const FString ExpectedObject = ExpectedPackage + TEXT(".DA_rig_physics");
	const FString ResolvedObject = FElysiumContentPaths::BakedUnit(TEXT("vtmb:model:test/rig"), TEXT("DA"), TEXT("physics"));
	TestEqual(TEXT("C++ resolver returns canonical object path"), ResolvedObject, ExpectedObject);
	TestEqual(TEXT("package projection matches the Python fixture"), FPackageName::ObjectPathToPackageName(ResolvedObject), ExpectedPackage);
	auto* Asset = NewObject<UElysiumPhysicsData>();
	if (!TestNotNull(TEXT("source data imports transactionally"), UElysiumPhysicsData::ApplyJson(Asset, Json, Error)))
	{ AddError(Error); return false; }
	TestEqual(TEXT("author mass remains unchanged"), Asset->Data.Solids[0].Parameters[1].Value, 3.125);
	TestEqual(TEXT("duplicate KV remains ordered"), Asset->Data.KeyValues[0].Pairs[1].Value, FString(TEXT("second")));
	TestEqual(TEXT("original triangle order remains"), Asset->Data.Solids[0].Hulls[0].Indices[1], 2);
	TestEqual(TEXT("stored AssetPath remains a package"), Asset->Data.AssetPath, ExpectedPackage);
	const auto Reject = [&](const FElysiumPhysicsSourceData& Bad, const TCHAR* Field)
	{
		FString BadJson; FJsonObjectConverter::UStructToJsonObjectString(Bad, BadJson);
		TestNull(FString(Field) + TEXT(" refuses invalid input"), UElysiumPhysicsData::ApplyJson(Asset, BadJson, Error));
		TestTrue(FString(Field) + TEXT(" diagnostic names the field"), Error.StartsWith(FString(TEXT("physics source: ")) + Field + TEXT(":")));
		TestEqual(FString(Field) + TEXT(" refusal preserves the complete original"), UElysiumPhysicsData::Verify(Asset, Json), FString());
	};
	FElysiumPhysicsSourceData Bad = Asset->Data;
	Bad.AssetPath = ExpectedObject; Reject(Bad, TEXT("assetPath"));
	TestTrue(TEXT("path error reports expected package and object forms"), Error.Contains(ExpectedPackage) && Error.Contains(ExpectedObject));
	Bad = Asset->Data; Bad.AssetPath = TEXT("/ElysiumBaked/Models/test/DA_rig_other"); Reject(Bad, TEXT("assetPath"));
	Bad = Asset->Data; Bad.AssetPath.Empty(); Reject(Bad, TEXT("assetPath"));
	Bad = Asset->Data; Bad.SchemaVersion = TEXT("2.0.0"); Reject(Bad, TEXT("schemaVersion"));
	Bad = Asset->Data; Bad.AssetId = TEXT("vtmb:texture:test/rig"); Reject(Bad, TEXT("assetId"));
	Bad = Asset->Data; Bad.AssetId = TEXT("vtmb:model:test/../rig"); Reject(Bad, TEXT("assetId"));
	Bad = Asset->Data; Bad.SourceGlbSha256 = FString::ChrN(64, TEXT('g')); Reject(Bad, TEXT("sourceGlbSha256"));
	Bad = Asset->Data; Bad.StagedBodySha256 = TEXT("short"); Reject(Bad, TEXT("stagedBodySha256"));
	Bad = Asset->Data; Bad.GeometryFrame = TEXT("Unreal centimetres"); Reject(Bad, TEXT("geometryFrame"));
	TSharedPtr<FJsonObject> WrongType;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), WrongType)) { AddError(TEXT("invalid test fixture JSON")); return false; }
	WrongType->SetNumberField(TEXT("schemaVersion"), 1.);
	FString WrongTypeJson; FJsonSerializer::Serialize(WrongType.ToSharedRef(), TJsonWriterFactory<>::Create(&WrongTypeJson));
	TestNull(TEXT("wrong field type remains refused"), UElysiumPhysicsData::ApplyJson(Asset, WrongTypeJson, Error));
	TestTrue(TEXT("strict type diagnostic includes full field path"), Error.Contains(TEXT("projection.SchemaVersion: expected string")));
	TestEqual(TEXT("strict type refusal preserves original"), UElysiumPhysicsData::Verify(Asset, Json), FString());
	TArray<uint8> Bytes;
	{
		FMemoryWriter Buffer(Bytes, true); FObjectAndNameAsStringProxyArchive Archive(Buffer, false);
		Archive.SetFilterEditorOnly(true); Asset->Serialize(Archive);
	}
	auto* Restored = NewObject<UElysiumPhysicsData>();
	{
		FMemoryReader Buffer(Bytes, true); FObjectAndNameAsStringProxyArchive Archive(Buffer, false);
		Archive.SetFilterEditorOnly(true); Restored->Serialize(Archive);
	}
	TestEqual(TEXT("editor-filtered serialization preserves typed data and complete evidence"), UElysiumPhysicsData::Verify(Restored, Json), FString());
	const auto* EvidenceProperty = FindFProperty<FProperty>(FElysiumPhysicsSourceData::StaticStruct(), TEXT("SourceEvidenceJson"));
	TestTrue(TEXT("evidence is a cooked property"), EvidenceProperty && !EvidenceProperty->HasAnyPropertyFlags(CPF_EditorOnly));
	Restored->Data.Solids[0].Hulls[0].Vertices[1].X += 1.;
	TestFalse(TEXT("geometry edits fail verification"), UElysiumPhysicsData::Verify(Restored, Json).IsEmpty());
	// Replace only the typed parameter: retain original evidence to detect source disagreement.
	FElysiumPhysicsSourceData Changed = Asset->Data; Changed.Solids[0].Parameters[1].Value = 3.5;
	FString ChangedJson; FJsonObjectConverter::UStructToJsonObjectString(Changed, ChangedJson);
	TestNull(TEXT("different physical parameter is rejected"), UElysiumPhysicsData::ApplyJson(Asset, ChangedJson, Error));
	TestEqual(TEXT("rejection leaves original asset intact"), UElysiumPhysicsData::Verify(Asset, Json), FString());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPhysicsSourceGapTest, "Elysium.Substrate.PhysicsSourceGaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FElysiumPhysicsSourceGapTest::RunTest(const FString&)
{
	const FString Json = Fixture(true); FString Error;
	auto* Asset = NewObject<UElysiumPhysicsData>();
	if (!TestNotNull(TEXT("source gap is retained"), UElysiumPhysicsData::ApplyJson(Asset, Json, Error)))
	{ AddError(Error); return false; }
	TestTrue(TEXT("gap is visible to future builder"), Asset->HasSourceGaps());
	TestEqual(TEXT("missing bone does not remove its hull"), Asset->Data.Solids[0].Hulls.Num(), 1);
	FElysiumPhysicsSourceData MissingGap = Asset->Data; MissingGap.Gaps.Empty();
	FString Changed; FJsonObjectConverter::UStructToJsonObjectString(MissingGap, Changed);
	TestNull(TEXT("silent source gap removal is rejected"), UElysiumPhysicsData::ApplyJson(Asset, Changed, Error));
	TestEqual(TEXT("gap rejection is transactional"), UElysiumPhysicsData::Verify(Asset, Json), FString());
	return true;
}
#endif
