#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"

#include "ElysiumHairDynamicsData.generated.h"

/** A complete stock-AnimDynamics recipe; it contains no VtMB runtime rule. */
USTRUCT()
struct FElysiumHairDynamicsChainConfig
{
	GENERATED_BODY()

	UPROPERTY() FName BoundBone;
	UPROPERTY() FName ChainEnd;
	UPROPERTY() float GravityScale = 1.0f;
	UPROPERTY() float Damping = 0.9f;
	UPROPERTY() float AngularSpring = 0.0f;
	UPROPERTY() float ConeAngleDegrees = 0.0f;
};

/** Generated skeletal-mesh metadata carried only by the two hair proof bodies. */
UCLASS()
class UElysiumHairDynamicsAssetUserData : public UAssetUserData
{
	GENERATED_BODY()

public:
	UPROPERTY() TArray<FElysiumHairDynamicsChainConfig> Chains;
};
