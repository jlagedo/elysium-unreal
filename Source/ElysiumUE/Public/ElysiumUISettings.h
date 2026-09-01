#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "ElysiumUISettings.generated.h"

/**
 * Screen-presentation taste knobs, Project Settings -> Elysium -> UI, tracked at
 * `Config/DefaultElysium.ini` (R4.5, `docs/project/seam_migration.md` -> "Wire first, tune later").
 * `elysium.MenuScrim` was the only orphan value in this domain -- a hardcoded cvar default with no
 * settings-page home -- so this page carries just the one field today; it is the group's home for
 * whatever else the UI lane migrates next.
 *
 * Unlike `UElysiumSurfaceSettings`, nothing here drives a live scalar a running material samples:
 * the value is read at Slate rebuild time (`UElysiumMainMenu::RebuildWidget`), so an edit only has
 * to reach an *already-open* menu, which `UElysiumUISubsystem` does by rebuilding on the settings
 * object's `OnSettingChanged` (the stock `UDeveloperSettings` broadcast) rather than a bespoke push
 * function.
 */
UCLASS(Config = Elysium, DefaultConfig, meta = (DisplayName = "UI"))
class ELYSIUMUE_API UElysiumUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UElysiumUISettings();

	/**
	 * How far the backdrop is knocked back behind the classic centred-column menu layout, 0 (none)
	 * .. 1 (black) -- `CVMainMenu::PerformLayout`'s global dimmer. The rail layout (the default) does
	 * not use it: its veil is local, so the lit half of the scene is never paid for.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Menu", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MenuScrim = 0.22f;
};
