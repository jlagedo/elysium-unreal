#include "ElysiumMapEnvironment.h"

#include "ElysiumContentPaths.h"
#include "ElysiumMapTransportSettings.h"

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
	// The identity (scale 1, bValid false) when this map has no miniature -- exactly what
	// `FElysiumSkyDef::Parse` leaves an unread map at.
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
		case EElysiumMapEnvironmentSource::Sidecar: return TEXT("sidecar");
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
		// R4.6: an unlisted map never attempts the asset, even if one exists on disk -- the tracked
		// flag list, not asset presence, decides the transport from here forward.
		if (ElysiumMapTransport::IsMapOnNewTransport(MapName))
		{
			// Quiet: a listed map whose asset is not yet baked falls back below rather than warning.
			if (const UElysiumMapEnvironment* Asset = LoadObject<UElysiumMapEnvironment>(
				nullptr, *AssetPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
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
		}

		const bool bHaveEnv = FElysiumEnvDef::Parse(FElysiumContentPaths::MapEnv(MapName), OutEnv);
		const bool bHaveSky = FElysiumSkyDef::Parse(FElysiumContentPaths::MapSky(MapName), OutSky);
		FElysiumSpawnDef Spawn;
		bOutHasSpawn = FElysiumSpawnDef::Parse(FElysiumContentPaths::MapSpawn(MapName), Spawn);
		OutSpawnLocation = Spawn.OriginCm;
		OutSpawnYaw = Spawn.YawDeg;
		if (!bHaveEnv && !bHaveSky && !bOutHasSpawn)
		{
			return EElysiumMapEnvironmentSource::None;
		}
		UE_LOG(LogElysiumMapEnvironment, Log,
			TEXT("%s: environment from .env/.sky/.spawn (no %s)"), *MapName, *AssetPath);
		return EElysiumMapEnvironmentSource::Sidecar;
	}
}
