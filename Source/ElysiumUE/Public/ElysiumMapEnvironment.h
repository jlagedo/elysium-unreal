#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ElysiumEnvironment.h"
#include "ElysiumMapEnvironment.generated.h"

// One map's `.env` / `.sky` / `.spawn` values as cooked content. A transport change and nothing
// else: every field below is what the three sidecars already carry, copied straight into the
// `FElysiumEnvDef` / `FElysiumSkyDef` / `FElysiumSpawnDef` the runtime has always consumed. The fog
// values in particular are a taste-free passthrough feeding `UElysiumMapVisuals::ApplySceneFog`
// unchanged -- nothing here is a tuning knob; the height-fog actor's own component properties are
// the tuning surface, edited directly in the level (its Cog sliders retired by R4.3).
//
// Authored by `pipeline/unreal/import_map_environment.py` from the stage
// (`elysium_pipeline.importers.map_environment`), which reads `<map>.env`/`.sky`/`.spawn` verbatim
// and asserts parity against them.
UCLASS(BlueprintType)
class ELYSIUMUE_API UElysiumMapEnvironment : public UDataAsset
{
	GENERATED_BODY()

public:
	// The map stem this table belongs to.
	UPROPERTY(EditAnywhere, Category = "Map")
	FString MapName;

	// `<map>.env` -- the 2D sky flag/name/orientation and the WORLD's fog (`worldspawn`'s).
	UPROPERTY(EditAnywhere, Category = "Sky")
	bool bSky = false;
	UPROPERTY(EditAnywhere, Category = "Sky")
	FString SkyName;
	UPROPERTY(EditAnywhere, Category = "Sky")
	int32 SkyConvention = 0;

	UPROPERTY(EditAnywhere, Category = "Fog")
	bool bFog = false;
	UPROPERTY(EditAnywhere, Category = "Fog")
	FLinearColor FogColor = FLinearColor::Black;
	UPROPERTY(EditAnywhere, Category = "Fog")
	float FogStartCm = 0.f;
	UPROPERTY(EditAnywhere, Category = "Fog")
	float FogEndCm = 0.f;

	// `<map>.env` -- the 3D-SKYBOX PASS's own fog (`sky_camera`'s), already carried into world
	// units at export.
	UPROPERTY(EditAnywhere, Category = "Fog")
	bool bSkyFog = false;
	UPROPERTY(EditAnywhere, Category = "Fog")
	FLinearColor SkyFogColor = FLinearColor::Black;
	UPROPERTY(EditAnywhere, Category = "Fog")
	float SkyFogStartCm = 0.f;
	UPROPERTY(EditAnywhere, Category = "Fog")
	float SkyFogEndCm = 0.f;

	// `<map>.sky` -- the 3D-skybox miniature's placement transform. Absent (bHasSkyMiniature =
	// false) on the maps with no `sky_camera`, which leaves the identity `FElysiumSkyDef` itself
	// defaults to.
	UPROPERTY(EditAnywhere, Category = "Sky Miniature")
	bool bHasSkyMiniature = false;
	UPROPERTY(EditAnywhere, Category = "Sky Miniature")
	FVector SkyOriginCm = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Category = "Sky Miniature")
	float SkyScale = 1.f;

	// `<map>.spawn` -- `info_player_start`'s origin (Source feet) and yaw. Absent (bHasSpawn =
	// false) on a map with no `info_player_start`.
	UPROPERTY(EditAnywhere, Category = "Spawn")
	bool bHasSpawn = false;
	UPROPERTY(EditAnywhere, Category = "Spawn")
	FVector SpawnOriginCm = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Category = "Spawn")
	float SpawnYawDeg = 0.f;

	// The plain substrate structs the runtime already consumes. Every field copies straight across;
	// nothing is derived or renormalized here.
	FElysiumEnvDef ToEnvDef() const;
	FElysiumSkyDef ToSkyDef() const;
	FElysiumSpawnDef ToSpawnDef() const;
};

// Where one map's environment came from. Returned by `Load` so a caller can log or assert the
// transport it actually got rather than the one it assumed.
enum class EElysiumMapEnvironmentSource : uint8
{
	None,      // neither an asset nor a readable sidecar
	Asset,     // /ElysiumBaked/<map>/DA_<map>_Environment
	Sidecar,   // <map>.env / <map>.sky / <map>.spawn
};

namespace ElysiumMapEnvironmentSource
{
	// The one entry point for "give me this map's environment". A map listed in
	// `UElysiumMapTransportSettings::MapsOnNewTransport` (R4.6) tries the baked asset first, falling
	// back if it turns out missing; an unlisted map goes straight to the `.env`/`.sky`/`.spawn`
	// sidecars, each parsed independently as `AElysiumMapActor`/`UElysiumMapVisuals` have always
	// parsed them.
	ELYSIUMUE_API EElysiumMapEnvironmentSource Load(const FString& MapName, FElysiumEnvDef& OutEnv,
		FElysiumSkyDef& OutSky, bool& bOutHasSpawn, FVector& OutSpawnLocation, float& OutSpawnYaw);

	// "asset" / "sidecar" / "none", for logs and test messages.
	ELYSIUMUE_API const TCHAR* ToString(EElysiumMapEnvironmentSource Source);
}
