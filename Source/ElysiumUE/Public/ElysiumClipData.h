#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimMetaData.h"
#include "ElysiumAnimEvent.h"
#include "ElysiumClipMovement.h"
#include "Visual/ElysiumNpcClips.h"
#include "Visual/ElysiumBlendGrids.h"
#include "ElysiumClipData.generated.h"

class UAnimationAsset;

USTRUCT()
struct FElysiumClipAxis
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") FString Name;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") int32 Flags = 0;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") float Loop = 0.f;
};

/** Authored clip facts travel with native sequences and blend spaces. Events remain data,
 * never notifies, and movement remains a path, never extracted root motion. */
UCLASS(EditInlineNew)
class ELYSIUMUE_API UElysiumClipData final : public UAnimMetaData
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString AssetId;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString OwnerRoot;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString Label;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString SourceLabel;
	/** Selection fields are also projected onto body rows, so scoring never loads a clip. */
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") FElysiumNpcClip Descriptor;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") TArray<FElysiumAnimEvent> Events;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") FElysiumClipMovementPath Movement;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") bool bMovementStated = false;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") float CycleSeconds = 0.f;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") float GroundDistanceCm = 0.f;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") float GroundSpeedCmPerSecond = 0.f;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") TArray<FElysiumClipAxis> Axes;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") TArray<FElysiumPoseParamDesc> PoseParams;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") FElysiumBlendGrid Grid;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") bool bHasGrid = false;

	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static UElysiumClipData* ApplyJson(UAnimationAsset* Asset, const FString& Json, FString& OutError);
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString Verify(UAnimationAsset* Asset, const FString& Json);
};
