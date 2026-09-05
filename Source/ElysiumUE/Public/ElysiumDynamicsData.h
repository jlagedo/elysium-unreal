#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Visual/ElysiumHairDynamicsData.h"
#include "ElysiumDynamicsData.generated.h"

/** Original declaration plus its explicit projection. Source-only rows are retained too. */
USTRUCT()
struct FElysiumSecondaryMotionRecord
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") int32 SourceOffset = 0;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") int32 FirstBone = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") int32 TerminalBone = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") float UnusedAuthoredPreset = 0.f;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") float Gravity = 0.f;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") float Damping = 0.f;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") float SpringExponent = 0.f;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") float MaxAngleDegrees = 0.f;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") TArray<int32> BoneIndices;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") TArray<FName> BoneNames;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Projection") FString Projection;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Projection") int32 RecipeIndex = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Projection") FString Reason;
};

/** Generated data is not an install gate. Authored tuning still admits and overrides chains. */
UCLASS()
class ELYSIUMUE_API UElysiumDynamicsData final : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString AssetId;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString ProjectionPolicy;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") TArray<FElysiumSecondaryMotionRecord> Records;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Projection") TArray<FElysiumHairDynamicsChainConfig> Chains;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Projection") TArray<FElysiumHairDynamicsBodyConfig> Bodies;
#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString AuthoringEvidence;
#endif
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static UElysiumDynamicsData* ApplyJson(UElysiumDynamicsData* Asset, const FString& Json, FString& OutError);
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString Verify(UElysiumDynamicsData* Asset, const FString& Json);
};
