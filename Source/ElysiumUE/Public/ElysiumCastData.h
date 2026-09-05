#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ElysiumCastData.generated.h"

class USkeletalMesh;
class USkeleton;
class UElysiumBodyData;
class UElysiumExpressionTables;

USTRUCT()
struct FElysiumCastModel
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString AssetId;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString ModelPath;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString Stem;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") TArray<FString> Roles;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Assets") TSoftObjectPtr<USkeletalMesh> Mesh;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Assets") TSoftObjectPtr<USkeleton> Skeleton;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Assets") TSoftObjectPtr<UElysiumBodyData> BodyData;
};

USTRUCT()
struct FElysiumCastAliasCandidates
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") TArray<FString> AssetIds;
};

USTRUCT()
struct FElysiumCinematicOwnerRef
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString AssetId;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString OwnerRoot;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Assets") TSoftObjectPtr<UElysiumBodyData> BodyData;
};

USTRUCT()
struct FElysiumCastCinematic
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString AssetId;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString ModelPath;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") TMap<FString,FElysiumCinematicOwnerRef> Roots;
};

/** Whole-corpus identity/alias lookup. A short name never chooses arbitrarily between models. */
UCLASS()
class ELYSIUMUE_API UElysiumCastData final : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") TMap<FString,FElysiumCastModel> Models;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") TMap<FString,FString> Aliases;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") TMap<FString,FElysiumCastAliasCandidates> AmbiguousAliases;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") TMap<FString,FElysiumCastCinematic> Cinematics;
	/** Cook reachability and map preparation root for every expression-table unit. */
	UPROPERTY(VisibleAnywhere, Category="Elysium|Assets") TObjectPtr<UElysiumExpressionTables> ExpressionTables;
#if WITH_EDITORONLY_DATA
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString AuthoringEvidence;
#endif
	const FElysiumCastModel* FindModel(const FString& Name, FString& OutError) const;
	const FElysiumCinematicOwnerRef* FindCinematic(const FString& Model, const FString& Root, FString& OutError) const;
	/** Preparation-time adapter for remaining authored/debug aliases. Never use from a tick. */
	static FString ModelIdForPreparation(const FString& Name, FString& OutError);
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static UElysiumCastData* ApplyJson(UElysiumCastData* Asset, const FString& Json, FString& OutError);
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString Verify(UElysiumCastData* Asset, const FString& Json);
};
