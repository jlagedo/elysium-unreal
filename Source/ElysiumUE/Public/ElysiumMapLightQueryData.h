#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ElysiumMapLightQueryData.generated.h"

class UElysiumMapCollisionPayload;

// Gameplay illumination retains every worldlight, including rows with no rendered light actor.
// Positions/radii are native cm. Attenuation coefficients are converted offline to cm powers.
USTRUCT()
struct FElysiumWorldLight
{
	GENERATED_BODY()
	UPROPERTY() FVector Position = FVector::ZeroVector;
	UPROPERTY() FVector Normal = FVector::ZeroVector;
	UPROPERTY() FVector Intensity = FVector::ZeroVector;
	UPROPERTY() int32 Type = 0;
	UPROPERTY() int32 Style = 0;
	UPROPERTY() int32 Cluster = -1;
	UPROPERTY() float Radius = 0;
	UPROPERTY() float Constant = 0;
	UPROPERTY() float Linear = 0;
	UPROPERTY() float Quadratic = 0;
	UPROPERTY() float StopDot = 0;
	UPROPERTY() float StopDot2 = 0;
	UPROPERTY() float Exponent = 1;
	// Type 4 reads original linear_attn as its distance budget, not radius (0x200a5620).
	UPROPERTY() float QuakeDistance = 0;
};

USTRUCT()
struct FElysiumLightQueryNode
{
	GENERATED_BODY()
	UPROPERTY() FVector Normal = FVector::ZeroVector;
	UPROPERTY() double Distance = 0;
	UPROPERTY() int32 Front = -1;
	UPROPERTY() int32 Back = -1;
};

USTRUCT()
struct FElysiumLightQueryRecords
{
	GENERATED_BODY()
	UPROPERTY() TArray<FElysiumWorldLight> Lights;
	UPROPERTY() TArray<FElysiumLightQueryNode> Nodes;
	UPROPERTY() TArray<int32> LeafClusters;
	UPROPERTY() TArray<uint8> Pvs;
	UPROPERTY() int32 NumClusters = 0;
	UPROPERTY() int32 HeadNode = 0;
};

// Native bake of the light service's inputs. Source I/O, entity identity and lightstyle state
// remain runtime-owned. The editor authors both cooked query colliders from V2 map units.
UCLASS(BlueprintType)
class ELYSIUMUE_API UElysiumMapLightQueryData : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY() FString MapName;
	UPROPERTY() FElysiumLightQueryRecords Records;
	UPROPERTY() TObjectPtr<UElysiumMapCollisionPayload> Occluders;
	UPROPERTY() TObjectPtr<UElysiumMapCollisionPayload> Sky;
	int32 ClusterAt(const FVector& PointCm) const;
	bool ClusterVisible(int32 From, int32 To) const;
	bool IsValidQuery() const;
#if WITH_EDITOR
	UFUNCTION(BlueprintCallable, Category="Elysium|Import")
	FString AuthorJson(const FString& Json);
#endif
};

namespace ElysiumWorldLight
{
	float DistanceFalloff(const FElysiumWorldLight& Light, const FVector& DeltaCm);
	float Angle(const FElysiumWorldLight& Light, const FVector& DirectionToLight);
	float Luminance(const FVector& Rgb);
	float StyleValue(const FString& Pattern, double Time);
	float Query(const UElysiumMapLightQueryData& Data, const FVector& PointCm,
		TFunctionRef<float(int32)> Style, TFunctionRef<bool(const FVector&, bool)> Trace);
}
