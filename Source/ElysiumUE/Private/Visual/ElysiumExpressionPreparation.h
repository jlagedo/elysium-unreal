#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"

class UElysiumCastData;
class UElysiumExpressionTables;
class FElysiumEntity;
struct FElysiumExpressionTable;

/** Owned by the map's preparation handle. No constructor/resolver performs asset I/O.
 * FGCObject pins the loaded cast and corpus; the epoch registry holds weak handles only.
 * Dropping the last handle unregisters this epoch and releases all prepared views.
 */
class FElysiumExpressionPreparation final : public FGCObject
{
public:
	static TSharedPtr<FElysiumExpressionPreparation> Create(uint32 Epoch, UElysiumCastData* Cast, FString& OutError);
	virtual ~FElysiumExpressionPreparation() override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FElysiumExpressionPreparation"); }
	TSharedPtr<const FElysiumExpressionTable> Event(const FString& Param, const FString& Class, FString& OutError) const;
	TSharedPtr<const FElysiumExpressionTable> Phonemes(const FElysiumEntity& Actor, FString& OutDiagnostic) const;
	TSharedPtr<const FElysiumExpressionTable> ModelSelection(const FElysiumEntity& Actor, const FString& Class, FString& OutDiagnostic) const;
	int32 NumPreparedTables() const { return Views.Num(); }
private:
	FElysiumExpressionPreparation(uint32 InEpoch, UElysiumCastData* InCast);
	uint32 Epoch = 0;
	TObjectPtr<UElysiumCastData> CastData;
	TObjectPtr<UElysiumExpressionTables> Corpus;
	TMap<FString, TSharedPtr<const FElysiumExpressionTable>> Views;
};

namespace ElysiumExpressions
{
	/** Pass an already-loaded DA_Cast after native asset preparation; retain the result per map. */
	TSharedPtr<FElysiumExpressionPreparation> PrepareResident(uint32 Epoch, UElysiumCastData* Cast, FString& OutError);
	/** Runtime entry points. Missing preparation is an error, never a loose-file fallback. */
	TSharedPtr<const FElysiumExpressionTable> LoadPreparedEvent(uint32 Epoch, const FString& Param,
		const FString& Class, FString& OutError);
	TSharedPtr<const FElysiumExpressionTable> LoadPreparedPhonemes(const FElysiumEntity& Actor, FString& OutDiagnostic);
	TSharedPtr<const FElysiumExpressionTable> LoadPreparedModelSelection(const FElysiumEntity& Actor,
		const FString& Class, FString& OutDiagnostic);
}
