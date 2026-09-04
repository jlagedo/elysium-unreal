#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "ElysiumSpriteSettings.generated.h"

/**
 * The `env_sprite` runtime rule's knobs (R6.1, `docs/architecture/seam_map_map.md` -> "Sprites
 * (R6.1)"), read out of `Config/DefaultElysium.ini` section `[/Script/ElysiumUE.ElysiumSpriteSettings]`
 * and applied by `FElysiumSpriteSceneProxy` at creation (a page edit reaches the next map load).
 *
 * Every default is VtMB's own, read off the shipped `client.dll` (`GlowBlend`, `100c24a0`): the
 * glow falloff `19000 / dist^2` (`102324b0`), its `0.05` floor (`101e55b0`), the screen-constant
 * scale `dist / 200` (`102261e0`), the pixel-visibility smoothing `r_glowfadein` 0.2 s /
 * `r_glowfadeout` 0.1 s (`102b1fd8` / `102b639c`), and the query quad -- `dist x 3/128` for
 * `rendermode` 3 alone (`102324d0`, the screen-constant quad) and a fixed 3 Source units for every
 * other mode (`10225158`). `SpriteQueryGrid` alone has no VtMB twin: Source counted the quad's
 * pixels, Unreal answers a sub-primitive occlusion query per box, so the fraction is sampled
 * over a grid. Wiring defaults, not a tuning judgement ("wire first, tune later").
 */
UCLASS(Config = Elysium, DefaultConfig, BlueprintType, meta = (DisplayName = "Sprites"))
class ELYSIUMUE_API UElysiumSpriteSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UElysiumSpriteSettings();

	/** Source's `GLOW_PROP`: a corona's brightness is `GlowFalloff / dist^2` (dist in Source inches). */
	UPROPERTY(EditAnywhere, Config, Category = "Glow", meta = (ClampMin = "0.0"))
	float GlowFalloff = 19000.0f;

	/** The floor of that brightness (the ceiling is 1). */
	UPROPERTY(EditAnywhere, Config, Category = "Glow", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GlowMinBrightness = 0.05f;

	/** A rendermode-3 corona's world width is `scale x texture x dist x GlowSizePerDistance`: screen-constant. VtMB's `1/200`. */
	UPROPERTY(EditAnywhere, Config, Category = "Glow", meta = (ClampMin = "0.0"))
	float GlowSizePerDistance = 0.005f;

	/** Seconds for the visible fraction to rise from 0 to 1 (`r_glowfadein`). */
	UPROPERTY(EditAnywhere, Config, Category = "Glow", meta = (ClampMin = "0.0"))
	float GlowFadeInSeconds = 0.2f;

	/** Seconds for the visible fraction to fall from 1 to 0 (`r_glowfadeout`). */
	UPROPERTY(EditAnywhere, Config, Category = "Glow", meta = (ClampMin = "0.0"))
	float GlowFadeOutSeconds = 0.1f;

	/** A rendermode-3 corona's occlusion sample half-size at the origin, as a fraction of the view distance (VtMB's `3/128`, `102324d0`). */
	UPROPERTY(EditAnywhere, Config, Category = "Occlusion", meta = (ClampMin = "0.0"))
	float SpriteQueryFootprintPerDistance = 3.0f / 128.0f;

	/** Every other mode's occlusion sample half-size, in Source units (VtMB's fixed `3.0`, `10225158`). */
	UPROPERTY(EditAnywhere, Config, Category = "Occlusion", meta = (ClampMin = "0.0"))
	float SpriteQueryFixedHalfInches = 3.0f;

	/** The sample is a `Grid x Grid` tiling of boxes; the visible fraction is visible boxes over the grid. */
	UPROPERTY(EditAnywhere, Config, Category = "Occlusion", meta = (ClampMin = "1", ClampMax = "8"))
	int32 SpriteQueryGrid = 4;
};
