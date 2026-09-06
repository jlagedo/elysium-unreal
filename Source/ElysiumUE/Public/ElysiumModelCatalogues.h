#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ElysiumWieldTable.h"
#include "ElysiumModelCatalogues.generated.h"

class UAnimSequence;
class UBlendSpace;
class USkeletalMesh;
class USkeleton;
class UStaticMesh;
class UMaterialInterface;
class UElysiumBodyData;

/** Authored absence must never become a successful empty-hand answer. */
UENUM()
enum class EElysiumCatalogueReferenceState : uint8 { Real, Empty, Null, Absent };

UENUM()
enum class EElysiumCatalogueWieldResult : uint8 { Found, NoGeometry, WorldModel, UnknownItem, SourceAbsent, InvalidCatalogue };

USTRUCT()
struct FElysiumCatalogueWieldRef
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere) FString AssetId;
	UPROPERTY(VisibleAnywhere) FString Source;
	UPROPERTY(VisibleAnywhere) EElysiumCatalogueReferenceState State = EElysiumCatalogueReferenceState::Empty;
	UPROPERTY(VisibleAnywhere) TSoftObjectPtr<USkeletalMesh> Mesh;
};

USTRUCT()
struct FElysiumCatalogueWieldItem
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere) FString AssetId;
	UPROPERTY(VisibleAnywhere) FString Classname;
	UPROPERTY(VisibleAnywhere) bool bShowsWieldModel = true;
	UPROPERTY(VisibleAnywhere) FElysiumCatalogueWieldRef Female;
	UPROPERTY(VisibleAnywhere) FElysiumCatalogueWieldRef Male;
	/** Complete item projection, including unused gameplay selection facts; never parsed in a tick. */
	UPROPERTY(VisibleAnywhere) FString SourceEvidence;
};

USTRUCT()
struct FElysiumCatalogueWieldBone
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere) FName Name;
	UPROPERTY(VisibleAnywhere) int32 Parent = INDEX_NONE;
	UPROPERTY(VisibleAnywhere) FVector Position = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere) FQuat Rotation = FQuat::Identity;
};

USTRUCT()
struct FElysiumCatalogueWieldBody
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere) FString AssetId;
	UPROPERTY(VisibleAnywhere) bool bMountPresent = false;
	UPROPERTY(VisibleAnywhere) bool bHandPresent = false;
	UPROPERTY(VisibleAnywhere) FName MountParent;
	UPROPERTY(VisibleAnywhere) TArray<FName> MatchedBones;
	UPROPERTY(VisibleAnywhere) TArray<FName> UnmatchedBones;
};

USTRUCT()
struct FElysiumCatalogueWieldModel
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere) FString AssetId;
	UPROPERTY(VisibleAnywhere) TSoftObjectPtr<USkeletalMesh> Mesh;
	UPROPERTY(VisibleAnywhere) TSoftObjectPtr<USkeleton> Skeleton;
	UPROPERTY(VisibleAnywhere) EElysiumWieldBinding Binding = EElysiumWieldBinding::None;
	UPROPERTY(VisibleAnywhere) FName MountBone;
	UPROPERTY(VisibleAnywhere) FName HandBone;
	UPROPERTY(VisibleAnywhere) FName CollapseBone;
	UPROPERTY(VisibleAnywhere) FString Grip;
	UPROPERTY(VisibleAnywhere) FString ReferencePoseSource;
	UPROPERTY(VisibleAnywhere) FString ReferenceOwner;
	UPROPERTY(VisibleAnywhere) FString ReferenceClipLabel;
	UPROPERTY(VisibleAnywhere) TSoftObjectPtr<UAnimSequence> ReferenceClip;
	UPROPERTY(VisibleAnywhere) TMap<FString, TSoftObjectPtr<UAnimSequence>> NativeSequences;
	UPROPERTY(VisibleAnywhere) TArray<FElysiumCatalogueWieldBone> ReferencePose;
	UPROPERTY(VisibleAnywhere) bool bHasTrailTip = false;
	UPROPERTY(VisibleAnywhere) FName TrailTipBone;
	UPROPERTY(VisibleAnywhere) FVector TrailTipPosition = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere) FQuat TrailTipRotation = FQuat::Identity;
	UPROPERTY(VisibleAnywhere) TMap<FString, FElysiumCatalogueWieldBody> Bodies;
	/** Complete staged body.wield, including checks, raw source transforms and anomalies. */
	UPROPERTY(VisibleAnywhere) FString DecisionEvidence;

	void GatherPaths(TSet<FSoftObjectPath>& Out) const;
};

USTRUCT()
struct FElysiumWieldCatalogueData
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere) TMap<FString, FElysiumCatalogueWieldItem> Items;
	UPROPERTY(VisibleAnywhere) TMap<FString, FElysiumCatalogueWieldModel> Models;
	UPROPERTY(VisibleAnywhere) FString SourceGaps;
};

/** Models/_Corpus/DA_WieldModels. Prepared by the owning subsystem before equip. */
UCLASS()
class ELYSIUMUE_API UElysiumWieldCatalogue final : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere) FElysiumWieldCatalogueData Data;
	/** Pure lookup; callers diagnose failures and asynchronously prepare the returned soft refs. */
	EElysiumCatalogueWieldResult Resolve(const FString& Classname, bool bFemale,
		const FElysiumCatalogueWieldModel*& OutModel, FString& OutError) const;
	UFUNCTION(BlueprintCallable, Category="Elysium|Catalogues")
	static UElysiumWieldCatalogue* ApplyJson(UElysiumWieldCatalogue* Asset, const FString& Json, FString& OutError);
	UFUNCTION(BlueprintCallable, Category="Elysium|Catalogues")
	static FString Verify(UElysiumWieldCatalogue* Asset, const FString& Json);
};

USTRUCT()
struct FElysiumCataloguePlacedClip
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere) int32 Index = 0;
	UPROPERTY(VisibleAnywhere) FString Label;
	UPROPERTY(VisibleAnywhere) FString Activity;
	UPROPERTY(VisibleAnywhere) int32 Weight = 0;
	UPROPERTY(VisibleAnywhere) int32 Flags = 0;
	UPROPERTY(VisibleAnywhere) int32 Frames = 0;
	UPROPERTY(VisibleAnywhere) double Fps = 0.;
	UPROPERTY(VisibleAnywhere) double BoundsRadiusCm = 0.;
	UPROPERTY(VisibleAnywhere) FString Owner;
	UPROPERTY(VisibleAnywhere) TSoftObjectPtr<UAnimSequence> Sequence;
	UPROPERTY(VisibleAnywhere) TSoftObjectPtr<UBlendSpace> BlendSpace;
	UPROPERTY(VisibleAnywhere) TSoftObjectPtr<UAnimSequence> BaseCell;
	UPROPERTY(VisibleAnywhere) FString State;
};

USTRUCT()
struct FElysiumCataloguePlacedModel
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere) FString AssetId;
	UPROPERTY(VisibleAnywhere) FString ModelPath;
	UPROPERTY(VisibleAnywhere) bool bSourceAbsent = false;
	UPROPERTY(VisibleAnywhere) FString SourceReason;
	UPROPERTY(VisibleAnywhere) TArray<FString> Roles;
	UPROPERTY(VisibleAnywhere) TSoftObjectPtr<UStaticMesh> StaticMesh;
	UPROPERTY(VisibleAnywhere) TSoftObjectPtr<USkeletalMesh> SkeletalMesh;
	UPROPERTY(VisibleAnywhere) TSoftObjectPtr<UElysiumBodyData> BodyData;
	UPROPERTY(VisibleAnywhere) bool bHasCloth = false;
	UPROPERTY(VisibleAnywhere) bool bStaticEquivalentProven = false;
	UPROPERTY(VisibleAnywhere) bool bStaticEquivalent = false;
	UPROPERTY(VisibleAnywhere) bool bStaticRestSuffices = false;
	/** Existing static-source lane; independent of enhanced rest-equivalence acceptance. */
	UPROPERTY(VisibleAnywhere) bool bStaticSourceRepresentation = false;
	UPROPERTY(VisibleAnywhere) bool bStaticTopologyEquivalent = false;
	/** Indices into Clips in the original candidate order. */
	UPROPERTY(VisibleAnywhere) TArray<int32> RestCandidates;
	UPROPERTY(VisibleAnywhere) TArray<FElysiumCataloguePlacedClip> Clips;
	UPROPERTY(VisibleAnywhere) bool bFullClipsRequired = false;
	UPROPERTY(VisibleAnywhere) TArray<FString> RequiredClips;
	UPROPERTY(VisibleAnywhere) FString PlacementEvidence;
	UPROPERTY(VisibleAnywhere) TMap<FString, TSoftObjectPtr<UAnimSequence>> NativeSequences;
	UPROPERTY(VisibleAnywhere) TMap<FString, TSoftObjectPtr<UBlendSpace>> NativeBlendSpaces;
	UPROPERTY(VisibleAnywhere) TArray<FString> AcceptanceIssues;
	UPROPERTY(VisibleAnywhere) FString SourceEvidence;
	const FElysiumCataloguePlacedClip* SelectRest(int32 PlacementToken) const;
	bool CanUseStatic(bool bNeedsAnimation) const;
	void GatherPaths(TSet<FSoftObjectPath>& Out) const;
};

USTRUCT()
struct FElysiumPlacedCatalogueData
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere) TMap<FString, FElysiumCataloguePlacedModel> Models;
};

/** Models/_Corpus/DA_PlacedModels. Model identity/state/dispatch remain with Source entities. */
UCLASS()
class ELYSIUMUE_API UElysiumPlacedModelCatalogue final : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere) FElysiumPlacedCatalogueData Data;
	const FElysiumCataloguePlacedModel* FindModel(const FString& AssetId) const { return Data.Models.Find(AssetId); }
	UFUNCTION(BlueprintCallable, Category="Elysium|Catalogues")
	static UElysiumPlacedModelCatalogue* ApplyJson(UElysiumPlacedModelCatalogue* Asset, const FString& Json, FString& OutError);
	UFUNCTION(BlueprintCallable, Category="Elysium|Catalogues")
	static FString Verify(UElysiumPlacedModelCatalogue* Asset, const FString& Json);
};

USTRUCT()
struct FElysiumCatalogueSkinCell
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere) int32 SkinReference = 0;
	UPROPERTY(VisibleAnywhere) int32 SourceSlot = 0;
	UPROPERTY(VisibleAnywhere) FString MaterialId;
	/** Hard reference deliberately retains every alternate (including undrawn columns) in cook. */
	UPROPERTY(VisibleAnywhere) TObjectPtr<UMaterialInterface> Material = nullptr;
	UPROPERTY(VisibleAnywhere) bool bSourceAbsent = false;
};

USTRUCT()
struct FElysiumCatalogueSkinFamily
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere) int32 Index = 0;
	UPROPERTY(VisibleAnywhere) TArray<FElysiumCatalogueSkinCell> Cells;
};

USTRUCT()
struct FElysiumCatalogueSkinSlot
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere) int32 Index = 0;
	UPROPERTY(VisibleAnywhere) FName SlotName;
	UPROPERTY(VisibleAnywhere) TArray<int32> SkinReferences;
};

USTRUCT()
struct FElysiumCatalogueSkinRepresentation
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere) FString Kind;
	UPROPERTY(VisibleAnywhere) TSoftObjectPtr<UStaticMesh> StaticMesh;
	UPROPERTY(VisibleAnywhere) TSoftObjectPtr<USkeletalMesh> SkeletalMesh;
	UPROPERTY(VisibleAnywhere) TArray<FElysiumCatalogueSkinSlot> Slots;
	UPROPERTY(VisibleAnywhere) TArray<FElysiumCatalogueSkinFamily> Families;
};

USTRUCT()
struct FElysiumCatalogueSkinModel
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere) FString AssetId;
	UPROPERTY(VisibleAnywhere) int32 FamilyCount = 0;
	UPROPERTY(VisibleAnywhere) int32 SkinReferenceCount = 0;
	UPROPERTY(VisibleAnywhere) TArray<FElysiumCatalogueSkinRepresentation> Representations;
	UPROPERTY(VisibleAnywhere) FString SourceOnlyReason;
	UPROPERTY(VisibleAnywhere) FString SourceEvidence;
};

USTRUCT()
struct FElysiumPropSkinCatalogueData
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere) TMap<FString, FElysiumCatalogueSkinModel> Models;
};

struct FElysiumCatalogueResolvedMaterial
{
	int32 SlotIndex = INDEX_NONE;
	FName SlotName;
	UMaterialInterface* Material = nullptr;
};

/** Models/_Corpus/DA_PropSkins. All families, no truncated index space or runtime material loads. */
UCLASS()
class ELYSIUMUE_API UElysiumPropSkinCatalogue final : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere) FElysiumPropSkinCatalogueData Data;
	/** Complete target material assignment, so skin N -> 0 restores the native base too. */
	bool ResolveMaterials(const FString& AssetId, bool bSkeletal, int32 Family,
		TArray<FElysiumCatalogueResolvedMaterial>& Out, FString& OutError) const;
	UFUNCTION(BlueprintCallable, Category="Elysium|Catalogues")
	static UElysiumPropSkinCatalogue* ApplyJson(UElysiumPropSkinCatalogue* Asset, const FString& Json, FString& OutError);
	UFUNCTION(BlueprintCallable, Category="Elysium|Catalogues")
	static FString Verify(UElysiumPropSkinCatalogue* Asset, const FString& Json);
};

// --- Ornaments: the runtime follow model an animation event attaches ---------------------------
//
// `CBaseCombatCharacter::HandleAnimEvent` (`0x1032e330`) events 4100/4101/4102 build a model path
// out of the record's own `options` field and spawn a `prop_dynamic_ornament` on it
// (`FUN_10190e50`), bone-merged onto the character (`FUN_10191170`: `SetAimEnt`, `SetParent`
// attachment 0, `SetOwnerEntity`, movetype none). The shipped rigs are 13-bone `Bip01`-rooted
// merge skeletons with a single rest sequence and no attachments, so the port's expression of one
// is a leader-posed skeletal component, exactly as a wield model is.
//
// **The key is the retail-formatted path, not a model id.** Retail never strips an extension: 4100
// formats `"%s.mdl"` and 4102 formats `"%s_%s.mdl"` with the gender word, so a record whose options
// already carry `.mdl` legitimately produces `wineglass.mdl_male.mdl`. The catalogue is keyed on
// exactly what the handler builds, lowercased, so the runtime never has to un-format anything.
USTRUCT()
struct FElysiumCatalogueOrnamentModel
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere) FString AssetId;
	UPROPERTY(VisibleAnywhere) TSoftObjectPtr<USkeletalMesh> Mesh;
	UPROPERTY(VisibleAnywhere) TSoftObjectPtr<USkeleton> Skeleton;
	/** The path the retail handler formats exists in no shipped VPK: the row is a recorded absence. */
	UPROPERTY(VisibleAnywhere) bool bSourceAbsent = false;
	/** The merge rig's bones, in file order — the evidence that this is a leader-pose rig. */
	UPROPERTY(VisibleAnywhere) TArray<FName> Bones;

	void GatherPaths(TSet<FSoftObjectPath>& Out) const;
};

USTRUCT()
struct FElysiumOrnamentCatalogueData
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere) TMap<FString, FElysiumCatalogueOrnamentModel> Models;
};

/** Models/_Corpus/DA_OrnamentModels. Keyed by the retail-formatted `.mdl` path, lowercased. */
UCLASS()
class ELYSIUMUE_API UElysiumOrnamentCatalogue final : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere) FElysiumOrnamentCatalogueData Data;
	/** Pure lookup on the already-formatted, already-lowercased retail path. */
	const FElysiumCatalogueOrnamentModel* FindModel(const FString& RetailPath) const
	{
		return Data.Models.Find(RetailPath);
	}
	UFUNCTION(BlueprintCallable, Category="Elysium|Catalogues")
	static UElysiumOrnamentCatalogue* ApplyJson(UElysiumOrnamentCatalogue* Asset, const FString& Json, FString& OutError);
	UFUNCTION(BlueprintCallable, Category="Elysium|Catalogues")
	static FString Verify(UElysiumOrnamentCatalogue* Asset, const FString& Json);
};
