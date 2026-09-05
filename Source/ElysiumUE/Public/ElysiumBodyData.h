#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Visual/ElysiumNpcClips.h"
#include "ElysiumBodyData.generated.h"

class UAnimSequence;
class UBlendSpace;

USTRUCT()
struct FElysiumBodyAnimationRef
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") FString Owner;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") FString OwnerRoot;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") FString Label;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") FString Host;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") TSoftObjectPtr<UAnimSequence> Sequence;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") TSoftObjectPtr<UBlendSpace> BlendSpace;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") TSoftObjectPtr<UAnimSequence> BaseCell;
};

/** Fields needed to score unloaded candidates, plus already-resolved asset/layer addresses. */
USTRUCT()
struct FElysiumBodySequence
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") FString Label;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") FString Owner;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") FString OwnerRoot;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") int32 RawIndex = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") FString Activity;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") int32 Weight = 0;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") int32 Flags = 0;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") int32 Frames = 0;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") float Fps = 30.f;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") float Fade = .2f;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") float ReachCm = 0.f;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") float LowReachCm = -1.f;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") int32 ComboMask = -1;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") bool bHasCombo = false;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") FElysiumBodyAnimationRef Assets;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") TArray<FElysiumBodyAnimationRef> Layers;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") TArray<FString> DeclaredLayers;

	FElysiumNpcClip SelectionClip() const;
};

USTRUCT()
struct FElysiumBodyIncludeOwner
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString AssetId;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") int32 SequenceBase = 0;
};

UCLASS()
class ELYSIUMUE_API UElysiumBodyData final : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString AssetId;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString OwnerRoot;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") TArray<FElysiumBodyIncludeOwner> IncludeOwners;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") TArray<FElysiumBodySequence> Sequences;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") TMap<FString,TSoftObjectPtr<UAnimSequence>> NativeSequences;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Animation") TMap<FString,TSoftObjectPtr<UBlendSpace>> NativeBlendSpaces;
#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString AuthoringEvidence;
#endif
	/** Builds only values; it never resolves any soft animation pointer. */
	FElysiumNpcClipSet SelectionVocabulary(const FString& SelectorStem) const;
	const FElysiumBodySequence* Find(const FString& Label, const FString& Owner = FString()) const;
	void GatherAnimationPaths(TSet<FSoftObjectPath>& Out) const;
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static UElysiumBodyData* ApplyJson(UElysiumBodyData* Asset, const FString& Json, FString& OutError);
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString Verify(UElysiumBodyData* Asset, const FString& Json);
};
