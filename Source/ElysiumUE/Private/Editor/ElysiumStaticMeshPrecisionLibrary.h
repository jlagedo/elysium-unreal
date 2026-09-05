#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ElysiumStaticMeshPrecisionLibrary.generated.h"

class UStaticMesh;

/** Editor/commandlet build utility. No editor subsystems, asset windows, saves or source UV edits. */
UCLASS()
class ELYSIUMUE_API UElysiumStaticMeshPrecisionLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Set full float UVs and high (16-bit) tangent storage on every source LOD, rebuild once,
	 * and finish compilation. Empty string is success; other settings are preserved.
	 * ExpectedLodCount is the importer-owned count, so a missing LOD cannot be a silent pass.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Editor|StaticMesh")
	static FString ApplyPrecision(UStaticMesh* Mesh, int32 ExpectedLodCount);

	/** Finish pending compilation, then check every source/build LOD's precision flags.
	 * Does not change settings, rebuild, save, or claim source/rendered geometry equivalence.
	 */
	UFUNCTION(BlueprintCallable, Category="Elysium|Editor|StaticMesh")
	static FString VerifyPrecision(UStaticMesh* Mesh, int32 ExpectedLodCount);
};
