#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "Visual/ElysiumFacialRig.h"
#include "Visual/ElysiumEyeRig.h"
#include "Visual/ElysiumCompositionRig.h"
#include "ElysiumCharacterProvenance.generated.h"

class UMaterialInterface;
class USkeletalMesh;

USTRUCT()
struct ELYSIUMUE_API FElysiumCharacterMaterialSlot
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") int32 SourceIndex = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FName Name;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString MaterialId;
};

USTRUCT()
struct ELYSIUMUE_API FElysiumCharacterSkinFamily
{
	GENERATED_BODY()
	/** Skin-reference column -> material declaration index; these are distinct number spaces. */
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") TArray<int32> TextureIndices;
	/** Indexed by the original skin-reference column, including columns with no drawn section. */
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") TArray<TSoftObjectPtr<UMaterialInterface>> Materials;
};

/** Source identity and build evidence for skeletal projections and their shared skeletons. */
UCLASS(BlueprintType)
class ELYSIUMUE_API UElysiumCharacterProvenance final : public UAssetUserData
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Source") FString AssetId;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Source") FString OwnerRoot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Source") FString UnitSha256;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Source") FString PayloadSha256;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Source") TArray<FString> SourceUnits;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Build") FString BankFamilyTreeSha256;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Elysium|Build") FString RecipeFingerprint;
	/** Explicitly distinguishes an authored empty rig from an asset awaiting its data bake. */
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") bool bHasMeshData = false;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString ModelPath;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString Stem;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FElysiumFacialRig Facial;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FElysiumEyeSet Eyes;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FElysiumCompositionRig Composition;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") TArray<FElysiumCharacterMaterialSlot> MaterialSlots;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") TArray<FElysiumCharacterSkinFamily> SkinFamilies;
#if WITH_EDITORONLY_DATA
	/** Inspectable authoring evidence; runtime behavior uses typed cooked data, not this JSON. */
	UPROPERTY(VisibleAnywhere, Category="Elysium|Build") FString AuthoringEvidence;
#endif
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static UElysiumCharacterProvenance* ApplyJson(UObject* Asset, const FString& Json, FString& OutError);
	/** Compare saved reflected mesh values against the staged projection, without changing the asset. */
	UFUNCTION(BlueprintCallable, Category="Elysium|Characters")
	static FString VerifyMeshData(UObject* Asset, const FString& Json);
	static const UElysiumCharacterProvenance* Find(const USkeletalMesh* Mesh);
};
