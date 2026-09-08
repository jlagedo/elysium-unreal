#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "ElysiumMapTransportSettings.generated.h"

/**
 * The explicit per-map cutover switch, Project Settings -> Elysium -> Map Transport,
 * tracked at `Config/DefaultElysium.ini`.
 *
 * Earlier resolvers each had their own "asset wins when present" rule, which answered
 * "is this map on the new transport" only by accident of whichever producer last ran. This page is
 * the one tracked, reviewable list that answers it on purpose: a listed map's entity/collision/
 * environment resolvers attempt their baked asset first (falling back to the sidecars if the asset
 * turns out to be missing or unreadable); an unlisted map never attempts the asset at all and keeps
 * running the legacy sidecar path exactly as it always has, whether or not an asset happens to
 * exist for it on disk. The light-calibration merge is deliberately NOT gated by this list.
 */
UCLASS(Config = Elysium, DefaultConfig, meta = (DisplayName = "Map Transport"))
class ELYSIUMUE_API UElysiumMapTransportSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UElysiumMapTransportSettings();

	/**
	 * Map stems (the `.ents`/`.hulls`/`.env` filename minus extension) whose entity, collision and
	 * environment resolvers read the baked `/ElysiumBaked/<map>/DA_<map>_*` assets. Matched
	 * case-insensitively. Empty by default: a fresh checkout boots every map on the legacy sidecar
	 * path, exactly as it did before R4.1.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Cutover")
	TArray<FName> MapsOnNewTransport;

	/**
	 * Map stems whose **geometry and props** are on the V2 lanes (R5.1): the bake authors this
	 * map's world, 3D-sky and brush meshes and its static-prop placements from the published map
	 * root unit (`vtmb:map:<map>`) instead of the `.obj`/`.props` sidecars, and the runtime
	 * resolves its prop meshes and skin table under `/ElysiumBaked/Meshes` (the R1 model corpus)
	 * instead of `/ElysiumBaked/Shared/Meshes`. Matched case-insensitively.
	 *
	 * Deliberately a second list rather than a reuse of `MapsOnNewTransport`: the two answer
	 * different questions and, today, over different map sets. `sp_theatre` is on the entity/
	 * collision/environment transport because those assets exist for it, but the R1 model import is
	 * map-scoped and has only staged the three-map working corpus, so pointing `sp_theatre`'s prop
	 * resolver at the V2 root would resolve nothing. A map joins this list when its models have
	 * been imported and its level re-baked on the V2 lane, which is a different event from its
	 * entity assets landing.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Cutover")
	TArray<FName> MapsOnV2Models;
};

namespace ElysiumMapTransport
{
	// The pure resolution rule, over an explicit settings object -- testable without touching
	// Project Settings or GConfig, matching UElysiumLightRig::ApplySettings's own synthetic-settings
	// test shape (R4.3).
	ELYSIUMUE_API bool IsMapOnNewTransport(const FString& MapName,
		const UElysiumMapTransportSettings& Settings);

	// The live entry point every resolver calls: reads GetDefault<UElysiumMapTransportSettings>().
	ELYSIUMUE_API bool IsMapOnNewTransport(const FString& MapName);

	// The same rule over `MapsOnV2Models` (R5.1): is this map's model corpus the V2 one?
	ELYSIUMUE_API bool IsMapOnV2Models(const FString& MapName,
		const UElysiumMapTransportSettings& Settings);
	ELYSIUMUE_API bool IsMapOnV2Models(const FString& MapName);
}
