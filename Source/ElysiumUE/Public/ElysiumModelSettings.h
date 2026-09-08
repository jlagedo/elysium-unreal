#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"

#include "ElysiumModelSettings.generated.h"

/**
 * The model-import lane's LOD mapping knobs: VtMB's `switchPoints` grows with distance while Unreal's `ScreenSize` shrinks with
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
 * nothing else -- they are a wiring default, not a tuning judgement: the owner's LOD pass happens
 * on this page later, "wire first, tune later".
 *
 * Unlike `UElysiumSurfaceSettings`, this page pushes nothing at runtime: the three knobs are
 * bake-time inputs to an offline Python stage, not scalars a running material graph samples, so
 * there is no `MPC_*` to keep in sync and no `PostEditChangeProperty` override.
 *
 * `BlueprintType` so the class is exported to the editor's Python (`unreal.ElysiumModelSettings`):
 * the map bake reads the detail-prop distances off this CDO (`bake_map_v2.detail_cull`),
 * exactly as it reads the lighting page, and the Python glue exports only Blueprint-visible
 * classes -- `UElysiumLightingSettings` is visible through its `BlueprintCallable` push, this page
 * has no such function, so the type flag is the honest spelling.
 */
UCLASS(Config = Elysium, DefaultConfig, BlueprintType, meta = (DisplayName = "Models"))
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

	// --- detail props ----
	/**
	 * The distance past which a `dprp` detail instance is culled: VtMB's `cl_detaildist`, default
	 * 600 inches (`CDetailObjectSystem::vfunc10`, client.dll 100e0d90), in centimetres. The map
	 * bake writes it as every detail component's `InstanceEndCullDistance`; an edit re-authors the
	 * converted levels on the next `export map`.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Detail Props", meta = (ClampMin = "0.0"))
	float DetailDrawDistanceCm = 600.0f * 2.54f;

	/**
	 * The width of VtMB's fade band below `DetailDrawDistanceCm`: `cl_detailfade`, default 300
	 * inches, in centimetres. The bake writes `InstanceStartCullDistance = Draw - Fade`; the
	 * engine culls hard at `Draw` and exposes the band as `PerInstanceFadeAmount`, which no master
	 * reads yet.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Detail Props", meta = (ClampMin = "0.0"))
	float DetailFadeRangeCm = 300.0f * 2.54f;
};
