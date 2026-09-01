// Content-free Substrate automation for `UElysiumMapEnvironment`'s conversions to the plain
// substrate structs — the R4.4 transport's asset-side reader
// (docs/architecture/seam_map_map.md -> "Import — environment"). The asset is a transport change
// and nothing else, so what is asserted here is that `ToEnvDef`/`ToSkyDef`/`ToSpawnDef` copy every
// field straight across, and that the three "this map has none" states — no sky faces, no
// miniature, no spawn — read back exactly as `FElysiumEnvDef`/`FElysiumSkyDef`/`FElysiumSpawnDef`'s
// own defaults do.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumMapEnvironment.h"

static constexpr EAutomationTestFlags GElysiumMapEnvironmentTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	UElysiumMapEnvironment* BuildFullyAuthored()
	{
		UElysiumMapEnvironment* Asset =
			NewObject<UElysiumMapEnvironment>(GetTransientPackage(), NAME_None, RF_Transient);
		Asset->MapName = TEXT("sm_probe");

		Asset->bSky = true;
		Asset->SkyName = TEXT("pier");
		Asset->SkyConvention = 1;

		Asset->bFog = true;
		Asset->FogColor = FLinearColor(0.0667f, 0.0784f, 0.098f);
		Asset->FogStartCm = 1270.f;
		Asset->FogEndCm = 12700.f;

		Asset->bSkyFog = true;
		Asset->SkyFogColor = FLinearColor(1.f, 1.f, 1.f);
		Asset->SkyFogStartCm = 20320.f;
		Asset->SkyFogEndCm = 203200.f;

		Asset->bHasSkyMiniature = true;
		Asset->SkyOriginCm = FVector(-3152.14, -57.15, 12647.295);
		Asset->SkyScale = 16.f;

		Asset->bHasSpawn = true;
		Asset->SpawnOriginCm = FVector(-5032.0448, 6568.2622, 388.62);
		Asset->SpawnYawDeg = -90.f;

		return Asset;
	}
}

// Every field copies straight from the reflected asset to the plain struct the runtime consumes —
// no derivation, no renormalisation.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapEnvironmentConvertsFieldForFieldTest,
	"Elysium.Substrate.MapEnvironment.ConvertsFieldForField", GElysiumMapEnvironmentTestFlags)
bool FElysiumMapEnvironmentConvertsFieldForFieldTest::RunTest(const FString&)
{
	const UElysiumMapEnvironment* Asset = BuildFullyAuthored();

	const FElysiumEnvDef Env = Asset->ToEnvDef();
	TestTrue(TEXT("sky flag"), Env.bSky);
	TestEqual(TEXT("sky name"), Env.SkyName, FString(TEXT("pier")));
	TestEqual(TEXT("sky convention"), Env.SkyConvention, 1);
	TestTrue(TEXT("world fog on"), Env.bFog);
	TestEqual(TEXT("world fog color"), Env.FogColor, FLinearColor(0.0667f, 0.0784f, 0.098f));
	TestEqual(TEXT("world fog start"), Env.FogStartCm, 1270.f);
	TestEqual(TEXT("world fog end"), Env.FogEndCm, 12700.f);
	TestTrue(TEXT("sky fog on"), Env.bSkyFog);
	TestEqual(TEXT("sky fog color"), Env.SkyFogColor, FLinearColor(1.f, 1.f, 1.f));
	TestEqual(TEXT("sky fog start"), Env.SkyFogStartCm, 20320.f);
	TestEqual(TEXT("sky fog end"), Env.SkyFogEndCm, 203200.f);

	const FElysiumSkyDef Sky = Asset->ToSkyDef();
	TestTrue(TEXT("miniature valid"), Sky.bValid);
	TestEqual(TEXT("miniature origin"), Sky.OriginCm, FVector(-3152.14, -57.15, 12647.295));
	TestEqual(TEXT("miniature scale"), Sky.Scale, 16.f);

	const FElysiumSpawnDef Spawn = Asset->ToSpawnDef();
	TestTrue(TEXT("spawn valid"), Spawn.bValid);
	TestEqual(TEXT("spawn origin"), Spawn.OriginCm, FVector(-5032.0448, 6568.2622, 388.62));
	TestEqual(TEXT("spawn yaw"), Spawn.YawDeg, -90.f);
	return true;
}

// A default-constructed asset (no miniature, no spawn authored) reads back exactly the identity
// `FElysiumSkyDef`/`FElysiumSpawnDef::Parse` leaves an unread map at — scale 1, both invalid — so a
// caller cannot tell "converted asset with nothing authored" from "no asset, no sidecar" by the
// struct alone; only the source enum `Load` returns can.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapEnvironmentDefaultsMatchTheSidecarIdentityTest,
	"Elysium.Substrate.MapEnvironment.DefaultsMatchTheSidecarIdentity",
	GElysiumMapEnvironmentTestFlags)
bool FElysiumMapEnvironmentDefaultsMatchTheSidecarIdentityTest::RunTest(const FString&)
{
	const UElysiumMapEnvironment* Asset =
		NewObject<UElysiumMapEnvironment>(GetTransientPackage(), NAME_None, RF_Transient);

	const FElysiumSkyDef Sky = Asset->ToSkyDef();
	TestFalse(TEXT("no miniature authored"), Sky.bValid);
	TestEqual(TEXT("identity scale"), Sky.Scale, 1.f);

	const FElysiumSpawnDef Spawn = Asset->ToSpawnDef();
	TestFalse(TEXT("no spawn authored"), Spawn.bValid);

	const FElysiumEnvDef Env = Asset->ToEnvDef();
	TestFalse(TEXT("no sky faces"), Env.bSky);
	TestFalse(TEXT("no world fog"), Env.bFog);
	TestFalse(TEXT("no sky fog"), Env.bSkyFog);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
