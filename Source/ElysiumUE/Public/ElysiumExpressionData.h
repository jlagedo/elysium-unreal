#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ElysiumExpressionData.generated.h"

struct FElysiumExpressionTable;

/** Source row order and spelling are data; lookups never rewrite either. */
USTRUCT()
struct FElysiumNativeExpressionRow
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|Expressions") int32 Index = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Expressions") FString Name;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Expressions") FString Class;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Expressions") int32 PhonemeCode = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Expressions") FString Description;
	// Doubles also retain the author's decimal values. Runtime VFE numbers are exact float32.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Expressions") TArray<double> Values;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Expressions") TArray<double> Weights;
};

USTRUCT()
struct FElysiumNativeExpressionTable
{
	GENERATED_BODY()
	// Presence is independent of cardinality: crooked_cop has zero keys and 32 rows.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Expressions") bool bPresent = false;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Expressions") bool bHasWeighting = false;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Expressions") TArray<FString> Keys;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Expressions") TArray<FElysiumNativeExpressionRow> Rows;
	int32 FindRow(const FString& Name) const;
	int32 FindRowByPhonemeCode(int32 Code) const;
};

/** One GLB identity, including VFE-only, authoring-only and undecoded units. */
UCLASS()
class ELYSIUMUE_API UElysiumExpressionData final : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString AssetId;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString Stem;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString SourceKind;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString SourceGlbSha256;
	// Cooked evidence, never parsed by a runtime consumer. Includes all unknown fields,
	// opaque hex ranges, original mappings/settings, authoring differences and byte ledgers.
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString SourceDocumentJson;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Expressions") FString RuntimeStatus;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Expressions") FElysiumNativeExpressionTable Table;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Expressions") FElysiumNativeExpressionTable Authoring;

	bool IsRuntimeReady() const { return RuntimeStatus == TEXT("ready") && Table.bPresent; }
	// Call during preparation, cache by AssetId for the map epoch. This copies values
	// for the existing evaluator; it neither evaluates nor opens any files/packages.
	TSharedPtr<const FElysiumExpressionTable> PrepareLegacyView(FString& OutError) const;
	UFUNCTION(BlueprintCallable, Category="Elysium|Expressions")
	static UElysiumExpressionData* ApplyJson(UElysiumExpressionData* Asset, const FString& Json, FString& OutError);
	UFUNCTION(BlueprintCallable, Category="Elysium|Expressions")
	static FString Verify(UElysiumExpressionData* Asset, const FString& Json);
};

/** Native projection of a model GLB's selectedTables row; owned by the body writer. */
USTRUCT()
struct FElysiumExpressionSelection
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|Expressions") FString TableClass;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Expressions") FString PrimaryAssetId;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Expressions") TArray<FString> FallbackAssetIds;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Source") FString SourceSelectionJson;
};

/** Load this small corpus once asynchronously and retain it for the map epoch.
 * Hard references retain every cooked unit. All resolver methods use resident objects only.
 */
UCLASS()
class ELYSIUMUE_API UElysiumExpressionTables final : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category="Elysium|Expressions") TMap<FString, TObjectPtr<UElysiumExpressionData>> Tables;
	const UElysiumExpressionData* ResolveEvent(const FString& Param, const FString& Class, FString& OutError) const;
	// Male/generic is an explicit actor fact supplied by the caller, not a filename guess.
	// Returns diagnostics on unsupported data or broken preparation instead of falling
	// through to a different face. TXT-only selection may fall back with a diagnostic.
	const UElysiumExpressionData* ResolveSelection(const FElysiumExpressionSelection& Selection,
		bool bMale, FString& OutDiagnostic) const;
	UFUNCTION(BlueprintCallable, Category="Elysium|Expressions")
	static UElysiumExpressionTables* ApplyJson(UElysiumExpressionTables* Asset, const FString& Json, FString& OutError);
	UFUNCTION(BlueprintCallable, Category="Elysium|Expressions")
	static FString Verify(UElysiumExpressionTables* Asset, const FString& Json);
};
