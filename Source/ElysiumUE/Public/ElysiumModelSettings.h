#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "ElysiumModelSettings.generated.h"

/**
 * The model-import lane's LOD mapping knobs (`docs/architecture/seam_map_model.md` -> "Import" ->
 * "Geometry"): VtMB's `switchPoints` grows with distance while Unreal's `ScreenSize` shrinks with
 * it, so the stage maps one to the other through one reciprocal constant and a floor/ceiling clamp,
 *
 * ```text
 * ScreenSize[0] = 1.0
 * ScreenSize[k] = clamp(LodSwitchConstant / switchPoint[k], LodScreenSizeFloor, LodScreenSizeCeiling)
 * ```
 *
 * read out of `Config/DefaultElysium.ini` section `[/Script/ElysiumUE.ElysiumModelSettings]`,
 * exactly as the material stage reads `ChromaThreshold` off `UElysiumSurfaceSettings`. The three
 * values ride in the model-import recipe, so changing one re-imports the multi-LOD units and
 * nothing else -- they are a wiring default, not a tuning judgement ("What this lane does not do":
 * the owner's LOD pass happens on this page after the roadmap, "wire first, tune later").
 *
 * Unlike `UElysiumSurfaceSettings`, this page pushes nothing at runtime: the three knobs are
 * bake-time inputs to an offline Python stage, not scalars a running material graph samples, so
 * there is no `MPC_*` to keep in sync and no `PostEditChangeProperty` override.
 */
UCLASS(Config = Elysium, DefaultConfig, meta = (DisplayName = "Models"))
class ELYSIUMUE_API UElysiumModelSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UElysiumModelSettings();

	/** The reciprocal constant: `ScreenSize[k] = clamp(LodSwitchConstant / switchPoint[k], Floor, Ceiling)`. */
	UPROPERTY(EditAnywhere, Config, Category = "LOD", meta = (ClampMin = "0.0"))
	float LodSwitchConstant = 1.0f;

	/** The minimum `ScreenSize` a computed LOD row may clamp to. */
	UPROPERTY(EditAnywhere, Config, Category = "LOD", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LodScreenSizeFloor = 0.001f;

	/** The maximum `ScreenSize` a computed LOD row may clamp to (LOD 0 itself is always `1.0`). */
	UPROPERTY(EditAnywhere, Config, Category = "LOD", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LodScreenSizeCeiling = 0.9f;
};
