#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "ElysiumMapTransportSettings.generated.h"

/**
 * The explicit per-map cutover switch (R4.6, `docs/architecture/seam_map_map.md` -> "## Import" ->
 * "The explicit per-map cutover flag (R4.6)"), Project Settings -> Elysium -> Map Transport,
 * tracked at `Config/DefaultElysium.ini`.
 *
 * R4.1/R4.2/R4.4 each gave their own resolver an "asset wins when present" rule, which answered
 * "is this map on the new transport" only by accident of whichever producer last ran. This page is
 * the one tracked, reviewable list that answers it on purpose: a listed map's entity/collision/
 * environment resolvers attempt their baked asset first (falling back to the sidecars if the asset
 * turns out to be missing or unreadable); an unlisted map never attempts the asset at all and keeps
 * running the pre-R4 sidecar path exactly as it always has, whether or not an asset happens to
 * exist for it on disk. R4.3's light-calibration merge is deliberately NOT gated by this list — see
 * `docs/architecture/seam_map_map_lighting.md` -> "## Import" -> "Cutover" for why.
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
}
