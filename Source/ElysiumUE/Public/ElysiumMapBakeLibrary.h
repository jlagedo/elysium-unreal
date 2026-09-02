#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ElysiumMapBakeLibrary.generated.h"

class UWorld;

/**
 * The editor-only questions `pipeline/unreal/bake_map.py` asks about a level's reflection captures
 * (R5.5, `docs/architecture/seam_map_map.md` -> "## Import -- reflection captures (R5.5)"), which
 * have no Python scripting surface of their own: rendering the placed captures' contents into the
 * level's `UMapBuildDataRegistry`, and counting how many captures actually carry that data.
 *
 * The build is `UEditorEngine::BuildReflectionCaptures` -- the same call the editor's Build ->
 * Reflection Captures menu makes (waits for pending shader/asset compiles, refreshes the sky
 * captures, then `UReflectionCaptureComponent::UpdateReflectionCaptureContents`) -- so the bake's
 * capture and a hand build in the editor are one computation asked twice. It needs a rendering
 * commandlet (`-AllowCommandletRendering`), which `unreal.bake_maps` already launches with.
 */
UCLASS()
class ELYSIUMUE_API UElysiumMapBakeLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Build every reflection capture in `World` into its level's MapBuildData and return how many
	 * capture components then carry built data (`CountBuiltReflectionCaptures`). Returns -1 when
	 * the build cannot run at all: null world, or no editor engine (a `-game` process). The caller
	 * compares the count against what it placed; a short count is a bake failure, never a warning.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Map Bake")
	static int32 BuildReflectionCaptures(UWorld* World);

	/**
	 * How many reflection capture components in `World`'s persistent level resolve their
	 * `MapBuildDataId` to a registry entry with a rendered cube (`CubemapSize > 0` and captured
	 * bytes). `OutComponents` receives the number of capture components found at all, so a caller
	 * can tell "none built" from "none placed". Reads the level's own `MapBuildData` directly,
	 * so it works on a world loaded from its package without a world context (the Content test)
	 * as well as on the live editor world (the bake and `bake_verify.py`). Null world -> 0 / 0.
	 */
	UFUNCTION(BlueprintCallable, Category = "Elysium|Map Bake")
	static int32 CountBuiltReflectionCaptures(UWorld* World, int32& OutComponents);
};
