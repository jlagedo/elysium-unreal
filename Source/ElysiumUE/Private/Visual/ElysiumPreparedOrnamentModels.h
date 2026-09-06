#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectKey.h"
#include "ElysiumModelCatalogues.h"

class USkeletalMesh;

// The resident half of `DA_OrnamentModels`, on `FElysiumPreparedWieldModels`' own contract.
//
// An ornament is created from an ANIMATION EVENT — `CBaseCombatCharacter::HandleAnimEvent`
// (`0x1032e330`) 4100/4102 — which means the request arrives mid-frame, on a cycle boundary, with
// no chance to wait for a stream. Retail could afford `CreateNoSpawn` + `SetModel` there because
// the model was already precached; the same guarantee here is residency, prepared once with the
// map's other native model contexts and never loaded on the event.
//
// World-keyed like the wield scope, so a body whose owner is a motor rather than the map actor
// still finds the map's context, and an outgoing map's teardown cannot unregister an incoming one.
class FElysiumPreparedOrnamentModels final : public FGCObject
{
public:
	static bool GatherPaths(const UElysiumOrnamentCatalogue* Catalogue, TSet<FSoftObjectPath>& Out, FString& Error);
	static TSharedPtr<FElysiumPreparedOrnamentModels> Create(UObject* Owner, uint64 Epoch,
		UElysiumOrnamentCatalogue* Catalogue, const TArray<UObject*>& ResidentAssets, FString& Error);
	static TSharedPtr<FElysiumPreparedOrnamentModels> ForOwner(const UObject* Owner);
	static void Release(const UObject* Owner);
	virtual ~FElysiumPreparedOrnamentModels() override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FElysiumPreparedOrnamentModels"); }

	// The row one retail-formatted path names, or null. The path is expected already lowercased,
	// because the handler that formats it is the one that lowercases it.
	const FElysiumCatalogueOrnamentModel* FindModel(const FString& RetailPath) const;
	// The resident mesh for a row — never a load. Null both for an unbaked row and for a row the
	// catalogue records as absent from the shipped install; `OutError` distinguishes them.
	USkeletalMesh* Mesh(const FString& RetailPath, FString& OutError) const;
	const UElysiumOrnamentCatalogue* Catalogue() const;

private:
	FElysiumPreparedOrnamentModels(UObject* Owner, uint64 Epoch);
	bool IsCurrent() const;
	FObjectKey Key;
	TWeakObjectPtr<UObject> Owner;
	uint64 Epoch;
	TObjectPtr<UElysiumOrnamentCatalogue> Data;
	TMap<FSoftObjectPath, TObjectPtr<UObject>> Assets;
};
