#include "ElysiumMapEnvironment.h"

#include "ElysiumContentPaths.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumMapEnvironment, Log, All);

FElysiumEnvDef UElysiumMapEnvironment::ToEnvDef() const
{
	FElysiumEnvDef Out;
	Out.bSky = bSky;
	Out.SkyName = SkyName;
	Out.SkyConvention = SkyConvention;
	Out.bFog = bFog;
	Out.FogColor = FogColor;
	Out.FogStartCm = FogStartCm;
	Out.FogEndCm = FogEndCm;
	Out.bSkyFog = bSkyFog;
	Out.SkyFogColor = SkyFogColor;
	Out.SkyFogStartCm = SkyFogStartCm;
	Out.SkyFogEndCm = SkyFogEndCm;
	return Out;
}

FElysiumSkyDef UElysiumMapEnvironment::ToSkyDef() const
{
	FElysiumSkyDef Out;
	// The identity (scale 1, bValid false) when this map has no miniature -- which is 65 of the
	// 108 maps, and not an error.
	Out.bValid = bHasSkyMiniature;
	Out.OriginCm = SkyOriginCm;
	Out.Scale = bHasSkyMiniature ? SkyScale : 1.f;
	return Out;
}

FElysiumSpawnDef UElysiumMapEnvironment::ToSpawnDef() const
{
	FElysiumSpawnDef Out;
	Out.bValid = bHasSpawn;
	Out.OriginCm = SpawnOriginCm;
	Out.YawDeg = SpawnYawDeg;
	return Out;
}

namespace ElysiumMapEnvironmentSource
{
	const TCHAR* ToString(EElysiumMapEnvironmentSource Source)
	{
		switch (Source)
		{
		case EElysiumMapEnvironmentSource::Asset:   return TEXT("asset");
		default:                                    return TEXT("none");
		}
	}

	EElysiumMapEnvironmentSource Load(const FString& MapName, FElysiumEnvDef& OutEnv,
		FElysiumSkyDef& OutSky, bool& bOutHasSpawn, FVector& OutSpawnLocation, float& OutSpawnYaw)
	{
		OutEnv = FElysiumEnvDef();
		OutSky = FElysiumSkyDef();
		bOutHasSpawn = false;
		OutSpawnLocation = FVector::ZeroVector;
		OutSpawnYaw = 0.f;

		const FString AssetPath = FElysiumContentPaths::BakedMapEnvironment(MapName);
		if (const UElysiumMapEnvironment* Asset = LoadObject<UElysiumMapEnvironment>(nullptr, *AssetPath))
		{
			OutEnv = Asset->ToEnvDef();
			OutSky = Asset->ToSkyDef();
			const FElysiumSpawnDef Spawn = Asset->ToSpawnDef();
			bOutHasSpawn = Spawn.bValid;
			OutSpawnLocation = Spawn.OriginCm;
			OutSpawnYaw = Spawn.YawDeg;
			UE_LOG(LogElysiumMapEnvironment, Log, TEXT("%s: environment from %s"), *MapName,
				*AssetPath);
			return EElysiumMapEnvironmentSource::Asset;
		}

		// 0018 story 21-1: no sidecar arm behind this any more. A map with no environment asset has
		// no fog, no sky and no player start, so it fails rather than opening into the defaults.
		UE_LOG(LogElysiumMapEnvironment, Error,
			TEXT("'%s': no environment asset at %s; run: uv run elysium bake map --maps %s"),
			*MapName, *AssetPath, *MapName);
		return EElysiumMapEnvironmentSource::None;
	}
}
