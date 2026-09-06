#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectKey.h"
#include "ElysiumModelCatalogues.h"

class UWorld;

/** One map/lab owner retains the handle. Covers both map-owned and motor-owned bodies
 * in its world. All equip queries use supplied resident objects and never load. */
class FElysiumPreparedWieldModels final : public FGCObject
{
public:
	static bool GatherPaths(const UElysiumWieldCatalogue* Catalogue, TSet<FSoftObjectPath>& Out, FString& Error);
	static TSharedPtr<FElysiumPreparedWieldModels> Create(UObject* Owner, uint64 Epoch,
		UElysiumWieldCatalogue* Catalogue, const TArray<UObject*>& ResidentAssets, FString& Error);
	static TSharedPtr<FElysiumPreparedWieldModels> ForOwner(const UObject* Owner);
	static void Release(const UObject* Owner);
	virtual ~FElysiumPreparedWieldModels() override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FElysiumPreparedWieldModels"); }
	EElysiumCatalogueWieldResult Resolve(const FString& Classname, bool bFemale,
		const FElysiumCatalogueWieldModel*& OutModel, FString& Error) const;
	USkeletalMesh* Mesh(const FElysiumWieldModelRef& Ref, const USkeletalMesh* Wearer, FString& Error) const;
	const UElysiumWieldCatalogue* Catalogue() const;
	static FElysiumWieldModelRef AttachmentRef(const FElysiumCatalogueWieldModel& Model);
private:
	FElysiumPreparedWieldModels(UObject* Owner, uint64 Epoch);
	bool IsCurrent() const;
	FObjectKey Key;
	TWeakObjectPtr<UObject> Owner;
	uint64 Epoch;
	TObjectPtr<UElysiumWieldCatalogue> Data;
	TMap<FSoftObjectPath, TObjectPtr<UObject>> Assets;
};
