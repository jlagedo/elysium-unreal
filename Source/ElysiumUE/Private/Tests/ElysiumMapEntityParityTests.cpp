// Content-tier parity for the R4.1 entity transport (`docs/architecture/seam_map_map_entities.md`
// -> "Import"): the baked `UElysiumMapEntities` and the `<map>.ents` document it replaces must
// produce the same `FElysiumEntityDef` array, through their own deserializers.
//
// This is the parity that matters, and it is deliberately not the stage's. The stage compares rows
// it just derived against the sidecar file, both from the same Python producer; these tests run the
// two **C++** readers -- `UElysiumMapEntities::Deserialize` and `FElysiumEntityDefs::Parse` -- over
// the two shipped artifacts and compare the defs they hand the entity world. Nothing else checks
// that the asset the editor authored says what the file says.
//
// Ground truth for "which maps" is the staged manifest this machine's last `uv run elysium import
// map-entities` wrote under `$ELYSIUM_WORK_ROOT/import/map_entities/`, the same oracle
// `ElysiumModelParityTests.cpp` reads for the model lane -- not a re-derivation of the selection
// here, which could drift from the real one and still agree with a bug in it.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumMapEntities.h"
#include "ElysiumMapTransportSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

static constexpr EAutomationTestFlags GElysiumMapEntityParityTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// `$ELYSIUM_WORK_ROOT/import/map_entities/manifest.json`, or empty when the root is unset.
	FString EntityManifestPath()
	{
		const FString WorkRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("ELYSIUM_WORK_ROOT"));
		if (WorkRoot.IsEmpty())
		{
			return FString();
		}
		return WorkRoot / TEXT("import") / TEXT("map_entities") / TEXT("manifest.json");
	}

	// The map stems the last stage run named, or an abstention already recorded and false.
	bool StagedEntityMaps(FAutomationTestBase& Test, TArray<FString>& OutMaps)
	{
		const FString Path = EntityManifestPath();
		if (Path.IsEmpty())
		{
			Test.AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: ELYSIUM_WORK_ROOT is not configured"));
			return false;
		}
		if (!IFileManager::Get().FileExists(*Path))
		{
			Test.AddInfo(FString::Printf(
				TEXT("ELYSIUM_TEST_ABSTAIN: no staged map-entity manifest at %s (run: uv run ")
				TEXT("elysium import map-entities --maps sp_tutorial_1 --maps sm_pawnshop_1 ")
				TEXT("--maps sm_hub_1)"), *Path));
			return false;
		}

		FString Text;
		if (!Test.TestTrue(TEXT("staged map-entity manifest reads"),
			FFileHelper::LoadFileToString(Text, *Path)))
		{
			return false;
		}
		TSharedPtr<FJsonObject> Manifest;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!Test.TestTrue(TEXT("staged map-entity manifest parses"),
			FJsonSerializer::Deserialize(Reader, Manifest) && Manifest.IsValid()))
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Row : Manifest->GetArrayField(TEXT("maps")))
		{
			OutMaps.Add(Row->AsObject()->GetStringField(TEXT("map")));
		}
		if (OutMaps.Num() == 0)
		{
			Test.AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the staged manifest names no map"));
			return false;
		}
		return true;
	}

	// Both readings of one map, at the identity sky (the transform is asserted on the Substrate
	// tier; here the two paths must agree on the raw rows). False when the map has no asset yet,
	// which is the normal state of a map R4.1 has not converted.
	bool ReadBothWays(const FString& Map, FElysiumEntityDefs& OutFromAsset,
		FElysiumEntityDefs& OutFromSidecar, FString& OutReason)
	{
		const FString AssetPath = FElysiumContentPaths::BakedMapEntities(Map);
		const UElysiumMapEntities* Asset = LoadObject<UElysiumMapEntities>(
			nullptr, *AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Asset == nullptr)
		{
			OutReason = FString::Printf(TEXT("no baked asset at %s"), *AssetPath);
			return false;
		}
		const FString EntsPath = FElysiumContentPaths::MapEnts(Map);
		if (!FElysiumEntityDefs::Parse(EntsPath, OutFromSidecar))
		{
			OutReason = FString::Printf(TEXT("no readable %s"), *EntsPath);
			return false;
		}
		Asset->Deserialize(OutFromAsset);
		return true;
	}
}

// Def count and transport: every staged map's asset produces exactly as many defs as its `.ents`
// does, and `ElysiumEntityDefSource::Load` answers from the asset for a map listed on
// `UElysiumMapTransportSettings::MapsOnNewTransport`, and from the sidecar for one that is staged
// but not listed (R4.6's cutover is the explicit flag, not the asset's mere presence, so this is the
// assertion that the cutover actually fires -- and only for the maps it was told to fire for).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapEntityDefCountParityTest,
	"Elysium.Content.MapEntities.DefCountParity", GElysiumMapEntityParityTestFlags)
bool FElysiumMapEntityDefCountParityTest::RunTest(const FString&)
{
	TArray<FString> Maps;
	if (!StagedEntityMaps(*this, Maps))
	{
		return true;
	}

	int32 Compared = 0;
	TArray<FString> Unreadable;
	for (const FString& Map : Maps)
	{
		FElysiumEntityDefs FromAsset, FromSidecar;
		FString Reason;
		if (!ReadBothWays(Map, FromAsset, FromSidecar, Reason))
		{
			Unreadable.Add(FString::Printf(TEXT("%s: %s"), *Map, *Reason));
			continue;
		}
		++Compared;
		TestEqual(FString::Printf(TEXT("%s: def count, asset vs .ents"), *Map),
			FromAsset.Num(), FromSidecar.Num());
		TestEqual(FString::Printf(TEXT("%s: the asset names its own map"), *Map),
			FromAsset.MapName, FromSidecar.MapName);

		// The resolver must choose the asset for a map listed on MapsOnNewTransport, and the
		// sidecar for a staged map that is not listed; a mismatch either way would make every
		// other assertion here true of a transport nobody is actually running.
		FElysiumEntityDefs Resolved;
		const EElysiumEntityDefSource Source = ElysiumEntityDefSource::Load(Map, Resolved);
		const bool bListed = ElysiumMapTransport::IsMapOnNewTransport(Map);
		const FString ExpectedSource = bListed ? TEXT("asset") : TEXT("sidecar");
		TestEqual(FString::Printf(TEXT("%s: the resolver's transport matches MapsOnNewTransport"),
			*Map), FString(ElysiumEntityDefSource::ToString(Source)), ExpectedSource);
		TestEqual(FString::Printf(TEXT("%s: the resolver's def count"), *Map),
			Resolved.Num(), FromSidecar.Num());
	}

	if (Compared == 0)
	{
		AddInfo(FString::Printf(
			TEXT("ELYSIUM_TEST_ABSTAIN: no staged map could be read both ways (%s)"),
			*FString::Join(Unreadable, TEXT("; "))));
		return true;
	}
	for (const FString& Row : Unreadable)
	{
		AddError(FString::Printf(TEXT("%s (other maps in the same run resolved)"), *Row));
	}
	return true;
}

// Per-index field parity: row `i` of the asset and entity `i` of the `.ents` describe the same
// entity, field for field. The ordinal is the running game's entity handle and a save key, so the
// comparison is by index and never by name.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapEntityFieldParityTest,
	"Elysium.Content.MapEntities.FieldParity", GElysiumMapEntityParityTestFlags)
bool FElysiumMapEntityFieldParityTest::RunTest(const FString&)
{
	TArray<FString> Maps;
	if (!StagedEntityMaps(*this, Maps))
	{
		return true;
	}

	//: How many differing entities one map may name before the rest are rolled up: a broken
	//: transport differs on every row, and 5,000 identical errors help nobody.
	constexpr int32 ReportLimit = 8;

	int32 Compared = 0, TotalDefs = 0;
	for (const FString& Map : Maps)
	{
		FElysiumEntityDefs FromAsset, FromSidecar;
		FString Reason;
		if (!ReadBothWays(Map, FromAsset, FromSidecar, Reason))
		{
			continue;   // the count test above owns the verdict on an unreadable map
		}
		++Compared;
		const int32 Count = FMath::Min(FromAsset.Num(), FromSidecar.Num());
		TotalDefs += Count;

		int32 Differing = 0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			const FElysiumEntityDef& A = FromAsset.Defs[Index];
			const FElysiumEntityDef& S = FromSidecar.Defs[Index];
			TArray<FString> Fields;

			if (A.Classname != S.Classname) { Fields.Add(TEXT("classname")); }
			if (A.TargetName != S.TargetName) { Fields.Add(TEXT("targetname")); }
			if (A.Origin != S.Origin) { Fields.Add(TEXT("origin")); }
			if (A.Model != S.Model) { Fields.Add(TEXT("model")); }
			if (A.Contents != S.Contents) { Fields.Add(TEXT("contents")); }
			if (A.bBlocksPlayer != S.bBlocksPlayer) { Fields.Add(TEXT("blocks_player")); }
			if (A.BrushMesh != S.BrushMesh) { Fields.Add(TEXT("brush_mesh")); }
			if (A.CullMaxCm != S.CullMaxCm) { Fields.Add(TEXT("cull_max_cm")); }
			if (A.bStartHidden != S.bStartHidden) { Fields.Add(TEXT("start_hidden")); }
			if (A.bSky != S.bSky) { Fields.Add(TEXT("sky")); }
			if (A.ModelMesh != S.ModelMesh) { Fields.Add(TEXT("model_mesh")); }
			if (A.ModelQuat != S.ModelQuat) { Fields.Add(TEXT("model_quat")); }
			if (A.HingeAxis != S.HingeAxis) { Fields.Add(TEXT("hinge_axis")); }
			if (A.ElevatorFloors != S.ElevatorFloors) { Fields.Add(TEXT("elevator_floors")); }

			if (A.Keys.Num() != S.Keys.Num())
			{
				Fields.Add(TEXT("keys"));
			}
			else
			{
				for (const TPair<FString, FString>& Pair : S.Keys)
				{
					const FString* Mine = A.Keys.Find(Pair.Key);
					if (Mine == nullptr || *Mine != Pair.Value)
					{
						Fields.Add(FString::Printf(TEXT("keys[%s]"), *Pair.Key));
						break;
					}
				}
			}

			if (A.Hulls.Num() != S.Hulls.Num())
			{
				Fields.Add(TEXT("hulls"));
			}
			else
			{
				for (int32 Hull = 0; Hull < S.Hulls.Num(); ++Hull)
				{
					if (A.Hulls[Hull].Vertices != S.Hulls[Hull].Vertices)
					{
						Fields.Add(FString::Printf(TEXT("hulls[%d]"), Hull));
						break;
					}
				}
			}

			if (A.Outputs.Num() != S.Outputs.Num())
			{
				Fields.Add(TEXT("outputs"));
			}
			else
			{
				for (int32 Out = 0; Out < S.Outputs.Num(); ++Out)
				{
					const FElysiumOutputDef& AO = A.Outputs[Out];
					const FElysiumOutputDef& SO = S.Outputs[Out];
					if (AO.Name != SO.Name || AO.Target != SO.Target || AO.Input != SO.Input
						|| AO.Param != SO.Param || AO.Delay != SO.Delay || AO.Times != SO.Times
						|| AO.Python != SO.Python)
					{
						Fields.Add(FString::Printf(TEXT("outputs[%d]"), Out));
						break;
					}
				}
			}

			if (Fields.Num() > 0)
			{
				++Differing;
				if (Differing <= ReportLimit)
				{
					AddError(FString::Printf(
						TEXT("%s[%d] (%s): asset and .ents differ on %s"),
						*Map, Index, *S.Classname, *FString::Join(Fields, TEXT(", "))));
				}
			}
		}
		if (Differing > ReportLimit)
		{
			AddError(FString::Printf(TEXT("%s: %d of %d entities differ (first %d named above)"),
				*Map, Differing, Count, ReportLimit));
		}
		AddInfo(FString::Printf(TEXT("%s: %d entity def(s) compared, %d differing"),
			*Map, Count, Differing));
	}

	if (Compared == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no staged map could be read both ways"));
		return true;
	}
	AddInfo(FString::Printf(TEXT("%d map(s), %d entity def(s) compared field for field"),
		Compared, TotalDefs));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
