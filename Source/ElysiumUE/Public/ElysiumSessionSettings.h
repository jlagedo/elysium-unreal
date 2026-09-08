#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "ElysiumSessionSettings.generated.h"

/**
 * Game-flow taste knobs, Project Settings -> Elysium -> Session, tracked at
 * `Config/DefaultElysium.ini`.
 * Read at map-load time (`UElysiumGameFlowSubsystem::OnPrepareLoadingScreen`); nothing here is
 * live-pushed, the same way `UElysiumModelSettings`'s bake-time knobs are not.
 */
UCLASS(Config = Elysium, DefaultConfig, meta = (DisplayName = "Session"))
class ELYSIUMUE_API UElysiumSessionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UElysiumSessionSettings();

	/**
	 * Minimum seconds the loading screen stays up, spent inside the engine's own
	 * `WaitForMovieToFinish` (i.e. before the map actor's build pass). The floor stops a fast local
	 * load reading as a flicker.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Loading", meta = (ClampMin = "0.0"))
	float LoadingScreenMinTime = 0.75f;
};
