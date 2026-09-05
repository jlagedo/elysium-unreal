#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ElysiumGeometryVerificationLibrary.generated.h"

class USkeletalMesh;

/** Read-only native facts for validation/native_geometry.py. No build or expected-answer code. */
UCLASS()
class ELYSIUMUE_API UElysiumGeometryVerificationLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Capture LOD0 mesh description, source/render map, CPU render buffers and morph LOD data.
	 * Returns an error (and empty JSON) if evidence is unavailable. Caller must supply a saved,
	 * freshly reloaded, compilation-complete mesh. Does not save, build, load another asset,
	 * evaluate skinning, inspect packed GPU morph streams, or prove rendered acceptance.
	 * Exported skin weights are global reference-bone indices and uint16 raw values; 8-bit
	 * buffer values are expanded by the engine to multiples of 257, NOT raw bytes.
	 * Tangent capture v1 includes saved X/sign and reconstructed authoring Y, plus
	 * built X/Y and packed-normal W (sign). Zero tangents are never filled or normalized here.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters|Verification")
	static FString CaptureGeometry(const USkeletalMesh* Mesh, FString& OutJson);
};
