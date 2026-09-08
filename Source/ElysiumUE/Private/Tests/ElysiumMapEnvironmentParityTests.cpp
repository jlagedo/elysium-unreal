// Content-tier parity for the environment transport: the baked `UElysiumMapEnvironment` and the `<map>.env` / `<map>.sky` /
// `<map>.spawn` sidecars it replaces must produce the same values.
//
// This is the parity that matters, and it is deliberately not the stage's. The stage compares
// values it just read against the files it read them from; these tests run the two **C++**
// readers -- `UElysiumMapEnvironment::ToEnvDef`/`ToSkyDef`/`ToSpawnDef` and
// `FElysiumEnvDef`/`FElysiumSkyDef`/`FElysiumSpawnDef::Parse` -- over the two shipped artifacts and
// compare what each hands the map actor. Nothing else checks that the asset the editor authored
// says what the sidecars say.
//
// Ground truth for "which maps" is the staged manifest this machine's last `uv run elysium import
// map-environment` wrote under `$ELYSIUM_WORK_ROOT/import/map_environment/`, the same oracle the
// entity and collision lanes read.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumMapEnvironment.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

static constexpr EAutomationTestFlags GElysiumMapEnvironmentParityTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// `$ELYSIUM_WORK_ROOT/import/map_environment/manifest.json`, or empty when the root is unset.
	// Prefixed (unlike the sibling entity/collision test files' `ManifestPath`/`StagedMaps`) so a
	// future unity-build bucketing shift cannot repeat the dormant duplicate-symbol collision R4.3
	// found between two anonymous namespaces of the same names.
	FString EnvironmentManifestPath()
	{
		const FString WorkRoot = FPlatformMisc::GetEnvironmentVariable(TEXT("ELYSIUM_WORK_ROOT"));
		if (WorkRoot.IsEmpty())
		{
			return FString();
		}
		return WorkRoot / TEXT("import") / TEXT("map_environment") / TEXT("manifest.json");
	}

	// The map stems the last stage run named, or an abstention already recorded and false.
	bool StagedEnvironmentMaps(FAutomationTestBase& Test, TArray<FString>& OutMaps)
	{
		const FString Path = EnvironmentManifestPath();
		if (Path.IsEmpty())
		{
			Test.AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: ELYSIUM_WORK_ROOT is not configured"));
			return false;
		}
		if (!IFileManager::Get().FileExists(*Path))
		{
			Test.AddInfo(FString::Printf(
				TEXT("ELYSIUM_TEST_ABSTAIN: no staged map-environment manifest at %s (run: uv run ")
				TEXT("elysium import map-environment --maps sp_tutorial_1 --maps sm_pawnshop_1 ")
				TEXT("--maps sm_hub_1)"), *Path));
			return false;
		}

		FString Text;
		if (!Test.TestTrue(TEXT("staged map-environment manifest reads"),
			FFileHelper::LoadFileToString(Text, *Path)))
		{
			return false;
		}
		TSharedPtr<FJsonObject> Manifest;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!Test.TestTrue(TEXT("staged map-environment manifest parses"),
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

	// False when the map has no asset yet, which is the normal state of a map R4.4 has not
	// converted.
	bool ReadBothWays(const FString& Map, FElysiumEnvDef& OutEnvFromAsset,
		FElysiumSkyDef& OutSkyFromAsset, FElysiumSpawnDef& OutSpawnFromAsset,
		FElysiumEnvDef& OutEnvFromSidecar, FElysiumSkyDef& OutSkyFromSidecar,
		FElysiumSpawnDef& OutSpawnFromSidecar, FString& OutReason)
	{
		const FString AssetPath = FElysiumContentPaths::BakedMapEnvironment(Map);
		const UElysiumMapEnvironment* Asset = LoadObject<UElysiumMapEnvironment>(
			nullptr, *AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (Asset == nullptr)
		{
			OutReason = FString::Printf(TEXT("no baked asset at %s"), *AssetPath);
			return false;
		}
		OutEnvFromAsset = Asset->ToEnvDef();
		OutSkyFromAsset = Asset->ToSkyDef();
		OutSpawnFromAsset = Asset->ToSpawnDef();

		FElysiumEnvDef::Parse(FElysiumContentPaths::MapEnv(Map), OutEnvFromSidecar);
		FElysiumSkyDef::Parse(FElysiumContentPaths::MapSky(Map), OutSkyFromSidecar);
		FElysiumSpawnDef::Parse(FElysiumContentPaths::MapSpawn(Map), OutSpawnFromSidecar);
		return true;
	}
}

// Field-for-field parity, across all three sidecars, for every staged map; and the resolver must
// actually choose the asset for a converted map, not silently fall back to the sidecar it agrees
// with by construction.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapEnvironmentFieldParityTest,
	"Elysium.Content.MapEnvironment.FieldParity", GElysiumMapEnvironmentParityTestFlags)
bool FElysiumMapEnvironmentFieldParityTest::RunTest(const FString&)
{
	TArray<FString> Maps;
	if (!StagedEnvironmentMaps(*this, Maps))
	{
		return true;
	}

	int32 Compared = 0;
	TArray<FString> Unreadable;
	for (const FString& Map : Maps)
	{
		FElysiumEnvDef EnvAsset, EnvSidecar;
		FElysiumSkyDef SkyAsset, SkySidecar;
		FElysiumSpawnDef SpawnAsset, SpawnSidecar;
		FString Reason;
		if (!ReadBothWays(Map, EnvAsset, SkyAsset, SpawnAsset, EnvSidecar, SkySidecar, SpawnSidecar,
			Reason))
		{
			Unreadable.Add(FString::Printf(TEXT("%s: %s"), *Map, *Reason));
			continue;
		}
		++Compared;

		TestEqual(FString::Printf(TEXT("%s: sky flag"), *Map), EnvAsset.bSky, EnvSidecar.bSky);
		TestEqual(FString::Printf(TEXT("%s: sky name"), *Map), EnvAsset.SkyName, EnvSidecar.SkyName);
		TestEqual(FString::Printf(TEXT("%s: sky convention"), *Map), EnvAsset.SkyConvention,
			EnvSidecar.SkyConvention);
		TestEqual(FString::Printf(TEXT("%s: world fog flag"), *Map), EnvAsset.bFog, EnvSidecar.bFog);
		TestEqual(FString::Printf(TEXT("%s: world fog color"), *Map), EnvAsset.FogColor,
			EnvSidecar.FogColor);
		TestEqual(FString::Printf(TEXT("%s: world fog start"), *Map), EnvAsset.FogStartCm,
			EnvSidecar.FogStartCm);
		TestEqual(FString::Printf(TEXT("%s: world fog end"), *Map), EnvAsset.FogEndCm,
			EnvSidecar.FogEndCm);
		TestEqual(FString::Printf(TEXT("%s: sky fog flag"), *Map), EnvAsset.bSkyFog,
			EnvSidecar.bSkyFog);
		TestEqual(FString::Printf(TEXT("%s: sky fog color"), *Map), EnvAsset.SkyFogColor,
			EnvSidecar.SkyFogColor);
		TestEqual(FString::Printf(TEXT("%s: sky fog start"), *Map), EnvAsset.SkyFogStartCm,
			EnvSidecar.SkyFogStartCm);
		TestEqual(FString::Printf(TEXT("%s: sky fog end"), *Map), EnvAsset.SkyFogEndCm,
			EnvSidecar.SkyFogEndCm);

		TestEqual(FString::Printf(TEXT("%s: miniature valid"), *Map), SkyAsset.bValid,
			SkySidecar.bValid);
		TestEqual(FString::Printf(TEXT("%s: miniature origin"), *Map), SkyAsset.OriginCm,
			SkySidecar.OriginCm);
		TestEqual(FString::Printf(TEXT("%s: miniature scale"), *Map), SkyAsset.Scale,
			SkySidecar.Scale);

		TestEqual(FString::Printf(TEXT("%s: spawn valid"), *Map), SpawnAsset.bValid,
			SpawnSidecar.bValid);
		TestEqual(FString::Printf(TEXT("%s: spawn origin"), *Map), SpawnAsset.OriginCm,
			SpawnSidecar.OriginCm);
		TestEqual(FString::Printf(TEXT("%s: spawn yaw"), *Map), SpawnAsset.YawDeg,
			SpawnSidecar.YawDeg);

		// The resolver must actually choose the asset for a converted map; a silent fallback would
		// make every other assertion here true of a transport nobody is using.
		FElysiumEnvDef ResolvedEnv;
		FElysiumSkyDef ResolvedSky;
		bool bResolvedHasSpawn = false;
		FVector ResolvedSpawnLoc = FVector::ZeroVector;
		float ResolvedSpawnYaw = 0.f;
		const EElysiumMapEnvironmentSource Source = ElysiumMapEnvironmentSource::Load(
			Map, ResolvedEnv, ResolvedSky, bResolvedHasSpawn, ResolvedSpawnLoc, ResolvedSpawnYaw);
		TestEqual(FString::Printf(TEXT("%s: the resolver reads the asset, not the sidecar"), *Map),
			FString(ElysiumMapEnvironmentSource::ToString(Source)), FString(TEXT("asset")));
		TestEqual(FString::Printf(TEXT("%s: the resolver's sky name"), *Map), ResolvedEnv.SkyName,
			EnvSidecar.SkyName);
		TestEqual(FString::Printf(TEXT("%s: the resolver's spawn flag"), *Map), bResolvedHasSpawn,
			SpawnSidecar.bValid);
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
	AddInfo(FString::Printf(TEXT("%d map(s) compared field for field across .env/.sky/.spawn"),
		Compared));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
