#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ElysiumPhysicsData.generated.h"

/** Presence is source data. A missing number is never an authored zero. */
USTRUCT()
struct FElysiumPhysicsOptionalNumber
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") bool bPresent = false;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") double Value = 0.;
};

USTRUCT()
struct FElysiumPhysicsNamedNumber
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FString Name;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") double Value = 0.;
};

USTRUCT()
struct FElysiumPhysicsSourcePoint
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") double X = 0.;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") double Y = 0.;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") double Z = 0.;
};

USTRUCT()
struct FElysiumPhysicsSourceBone
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") int32 Index = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") int32 Parent = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FString SourceName;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FName NativeName;
	// Original MDL inverse bind: row-major 3x4, Source inches. Not a native pose.
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") TArray<double> PoseToBone;
};

USTRUCT()
struct FElysiumPhysicsSourceHull
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") int32 SolidOrdinal = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") int32 LedgeOrdinal = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") int32 SourceOffset = 0;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") int32 PositionAccessor = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") int32 IndexAccessor = INDEX_NONE;
	// Exact published accessor order, IVP metres, axis-only. No simulation conversion.
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") TArray<FElysiumPhysicsSourcePoint> Vertices;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") TArray<int32> Indices;
};

USTRUCT()
struct FElysiumPhysicsSourceSolid
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") int32 Ordinal = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") int32 BinaryIndex = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FElysiumPhysicsOptionalNumber AuthoredIndex;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") int32 SourceOffset = 0;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FString SourceName;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FString SourceParent;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") int32 SourceBoneIndex = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") int32 ParentBoneIndex = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FName NativeBoneName;
	// Empty means absent; authored origin/angles are retained, not used for placement.
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") TArray<double> Origin;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") TArray<double> Angles;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") TArray<double> MassCenterIvp;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") TArray<double> RotationInertiaIvp;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FString SurfaceProperty;
	// Includes authored index/mass/damping/rotdamping/inertia/volume/massbias and
	// future numeric keys. Missing names stay absent. No physical defaults or tuning.
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") TArray<FElysiumPhysicsNamedNumber> Parameters;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") TArray<FElysiumPhysicsSourceHull> Hulls;
};

USTRUCT()
struct FElysiumPhysicsSourceAxis
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FString Name;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FElysiumPhysicsOptionalNumber Minimum;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FElysiumPhysicsOptionalNumber Maximum;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FElysiumPhysicsOptionalNumber Friction;
};

USTRUCT()
struct FElysiumPhysicsSourceConstraint
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") int32 Ordinal = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") int32 ParentSolidIndex = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") int32 ChildSolidIndex = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") int32 ParentSolidOrdinal = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") int32 ChildSolidOrdinal = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") TArray<FElysiumPhysicsSourceAxis> Axes;
};

USTRUCT()
struct FElysiumPhysicsKeyValue
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FString Key;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FString Value;
};

USTRUCT()
struct FElysiumPhysicsKeyValueBlock
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FString BlockType;
	// Ordered pairs, including repeated keys and unnamed/unknown block payloads.
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") TArray<FElysiumPhysicsKeyValue> Pairs;
};

USTRUCT()
struct FElysiumPhysicsEditParameters
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") int32 Ordinal = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") TArray<FElysiumPhysicsNamedNumber> Parameters;
};

USTRUCT()
struct FElysiumPhysicsSourceGap
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FString Kind;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") int32 Ordinal = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FString Field;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FString SourceName;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FString Reason;
};

USTRUCT()
struct FElysiumPhysicsSourceData
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FString SchemaVersion;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FString AssetId;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FString AssetPath;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FString SourceGlbSha256;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FString StagedBodySha256;
	// Deliberately COOKED. All physics/header/ledge fields, unknowns, ordered KV,
	// editparams/jointmerge/breaks, raw bone evidence and accessor descriptions survive.
	// This is audit evidence; runtime consumers use the typed resident fields below.
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FString SourceEvidenceJson;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") bool bHasPhysics = false;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FString GeometryFrame;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") TArray<FElysiumPhysicsNamedNumber> HeaderParameters;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") TArray<FElysiumPhysicsSourceBone> Bones;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") TArray<FElysiumPhysicsSourceSolid> Solids;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") TArray<FElysiumPhysicsSourceConstraint> Constraints;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") TArray<FElysiumPhysicsKeyValueBlock> KeyValues;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") TArray<FElysiumPhysicsEditParameters> EditParams;
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") TArray<FElysiumPhysicsSourceGap> Gaps;
};

/** Source-data prerequisite only. No PhysicsAsset, solver, tick, or loose-file reader. */
UCLASS()
class ELYSIUMUE_API UElysiumPhysicsData final : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere, Category="Elysium|PhysicsSource") FElysiumPhysicsSourceData Data;
	const FElysiumPhysicsSourceData& GetSourceData() const { return Data; }
	bool HasSourceGaps() const { return !Data.Gaps.IsEmpty(); }
	UFUNCTION(BlueprintCallable, Category="Elysium|PhysicsSource")
	static UElysiumPhysicsData* ApplyJson(UElysiumPhysicsData* Asset, const FString& Json, FString& OutError);
	UFUNCTION(BlueprintCallable, Category="Elysium|PhysicsSource")
	static FString Verify(UElysiumPhysicsData* Asset, const FString& Json);
};
