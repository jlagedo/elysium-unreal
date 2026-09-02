#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ElysiumSkyBakeLibrary.generated.h"

class UTextureCube;

/**
 * The one editor-only question `pipeline/unreal/bake_map.py` asks that has no Python scripting
 * surface of its own (R5.2, `docs/architecture/seam_map_map_lighting.md` -> "## Import" ->
 * "Sky baked (R5.2)"): building the six-face sky cubemap.
 *
 * The pixel work — the K1 x K2 face->slice rotation table and the solid-angle-weighted
 * upper-hemisphere mean that turns VtMB's `emit_skyambient` magnitude into a SkyLight
 * intensity — already exists once, in `ElysiumEnvironment::BuildSkyCubeFrom`, because the
 * runtime still builds the same cube at load for every map this settings page has not
 * converted. This library is a thin UFUNCTION face onto that SAME function, aimed at a
 * persistent package instead of a transient one, so the bake's join and the runtime's join
 * are one computation asked twice rather than two computations that merely claim to agree.
 */
UCLASS()
class ELYSIUMUE_API UElysiumSkyBakeLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Builds `<PackagePath>` as a persistent `UTextureCube` from the six faithful (non-enhanced)
	 * `shared/tex/skybox_<skyname><face>.png` faces named by `SkyName`, and returns it saved
	 * into that package (`RF_Public | RF_Standalone`, ready for `EditorAssetLibrary.save_asset`).
	 * `OutUpperMean` receives the cube's solid-angle-weighted upper-hemisphere mean linear
	 * radiance, the same number `ElysiumMapVisuals::SkyAmbientIntensity` divides
	 * `emit_skyambient`'s magnitude by at runtime.
	 *
	 * The faithful set only — never `tex_hi` — because a bake is asked once and the faithful
	 * decode is VtMB's own data (the runtime `elysium.EnhancedTextures` opt-in retired at R6.5;
	 * the face PNGs' own promotion to first-class imported textures is a later task).
	 *
	 * Returns null, `OutUpperMean` 0, and creates nothing when any of the six faces is missing
	 * or non-square — the same failure the runtime's own `HasSkyFaces`/`BuildSkyCubeFrom` report.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Sky")
	static UTextureCube* BakeSkyCubeAsset(const FString& SkyName, const FString& PackagePath,
		float& OutUpperMean);
};
