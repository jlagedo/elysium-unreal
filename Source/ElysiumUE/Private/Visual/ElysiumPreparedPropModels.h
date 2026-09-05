#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectKey.h"
#include "ElysiumModelCatalogues.h"
#include "Visual/ElysiumNpcClips.h"
#include "Visual/ElysiumBlendGrids.h"

class UElysiumEntityBodies;
class UElysiumBodyData;
class UMeshComponent;
class UPrimitiveComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
struct FElysiumCompositionRig;
struct FElysiumPlacedModelRequest;

/** Main retains one handle per map owner/epoch. Registry entries are weak, and only
 * explicitly supplied resident objects are used. No function performs asset/file I/O.
 */
class FElysiumPreparedPropModels final : public FGCObject
{
public:
	static TSharedPtr<FElysiumPreparedPropModels> Create(UObject* Owner, uint64 Epoch,
		UElysiumPlacedModelCatalogue* Placed, UElysiumPropSkinCatalogue* Skins,
		const TArray<FString>& ModelIds, const TArray<UObject*>& ResidentAssets, FString& OutError);
	static bool GatherPaths(const UElysiumPlacedModelCatalogue* Placed, const UElysiumPropSkinCatalogue* Skins,
		const TArray<FString>& ModelIds, TSet<FSoftObjectPath>& OutPaths, FString& OutError);
	virtual ~FElysiumPreparedPropModels() override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FElysiumPreparedPropModels"); }
	uint64 GetEpoch() const { return Epoch; }
	const FElysiumCataloguePlacedModel* Model(const FString& Id, FString& Error) const;
	const FElysiumCataloguePlacedClip* Clip(const FString& Id, const FString& Label, FString& Error) const;
	const FElysiumAnimatedPropEntry* CompatibilityView(const FString& Id) const;
	UStaticMesh* StaticMesh(const FString& Id, FString& Error) const;
	USkeletalMesh* SkeletalMesh(const FString& Id, FString& Error) const;
	UObject* Resident(const FSoftObjectPath& Path, FString& Error) const;
	UAnimSequence* Sequence(const FString& Id, const FString& Label, FString& Error,
		const FElysiumPoseParams& Pose = FElysiumPoseParams::Neutral()) const;
	TSharedPtr<const FElysiumCompositionRig> Composition(const FString& Id) const;
	bool ApplySkin(UMeshComponent* Component, const FString& Id, int32 Family, FString& Error) const;
	bool IsExplicitlyGeometryless(const FString& Id) const;
	void GatherResidentSequences(TSet<UAnimSequence*>& Out) const;
private:
	FElysiumPreparedPropModels(UObject* Owner, uint64 InEpoch);
	bool IsCurrent() const;
	UObject* FindResident(const FSoftObjectPath& Path, FString& Error) const;
	FObjectKey OwnerKey;
	TWeakObjectPtr<UObject> Owner;
	uint64 Epoch = 0;
	TObjectPtr<UElysiumPlacedModelCatalogue> Placed;
	TObjectPtr<UElysiumPropSkinCatalogue> Skins;
	TMap<FSoftObjectPath, TObjectPtr<UObject>> Assets;
	TSet<FString> Admitted;
	TMap<FString, FElysiumAnimatedPropEntry> Views;
	TMap<FString, TSharedPtr<FElysiumBlendTable>> Grids;
	TMap<FString, TSharedPtr<const FElysiumCompositionRig>> Rigs;
};

/** General result needed by the shared embodiment adapter: its old Visual field is skeletal-only. */
struct FElysiumPreparedPropBody
{
	FString ModelId;
	UPrimitiveComponent* Visual = nullptr;
	UPrimitiveComponent* Attach = nullptr;
	USkeletalMeshComponent* SkeletalVisual = nullptr;
	UStaticMeshComponent* StaticVisual = nullptr;
	UStaticMeshComponent* PhysicsProxy = nullptr;
	bool IsValid() const { return Visual && Attach; }
};

namespace ElysiumPreparedProps
{
	/** Explicit model ID or full models/...mdl source path; never infer a flattened stem. */
	FString ModelId(const FString& SourceOrId);
	TSharedPtr<FElysiumPreparedPropModels> ForOwner(const UObject* Owner);
	/** Call at map teardown BEFORE releasing/changing the owner's epoch. */
	void Release(const UObject* Owner);
	/** Dispose components created by a failed presentation transaction, including new garments. */
	void DestroyVisual(UPrimitiveComponent* Visual);
	/** Main's new result adapter calls this; bNeedsAnimation is entity semantics, not a feature flag. */
	FElysiumPreparedPropBody Build(UElysiumEntityBodies& Factory, const FElysiumPlacedModelRequest& Request,
		bool bNeedsAnimation, FString& OutError);
}
