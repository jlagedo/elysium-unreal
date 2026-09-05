#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "ElysiumSkyProvenance.generated.h"

class UTextureCube;

/** Six-unit texture-lane projection. Full source evidence survives save/cook with the cube. */
UCLASS(BlueprintType)
class ELYSIUMUE_API UElysiumSkyProvenance final : public UAssetUserData
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Sky") FString SkyName;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Sky") double UpperHemisphereMean = 0.0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Sky") int32 MipCount = 0;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Sky") FString RecipeSha256;
	/** All six GLB hashes/extensions, face rotations, mip hashes, mean method and build policy. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Sky") FString ProvenanceJson;

	/** Editor-only: validate every imported source face/mip against the staged RGBA32F hashes,
	 * then attach. Failure leaves any previous record intact. Python returns (record, error). */
	UFUNCTION(BlueprintCallable, Category="Elysium|Sky")
	static UElysiumSkyProvenance* ApplyJson(UTextureCube* Cube, const FString& Json, FString& OutError);

	UFUNCTION(BlueprintCallable, Category="Elysium|Sky")
	static const UElysiumSkyProvenance* Find(const UTextureCube* Cube);
};
