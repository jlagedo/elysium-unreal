#pragma once

#include "CoreMinimal.h"

#include "ElysiumHairDynamicsData.generated.h"

/** A complete stock-AnimDynamics chain recipe; it contains no VtMB runtime rule.
 * VisibleAnywhere so the values are readable through reflection (editor Python included);
 * the bake is the only writer. */
USTRUCT()
struct FElysiumHairDynamicsChainConfig
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Elysium") FName BoundBone;
	UPROPERTY(VisibleAnywhere, Category="Elysium") FName ChainEnd;
	UPROPERTY(VisibleAnywhere, Category="Elysium") float GravityScale = 1.0f;
	UPROPERTY(VisibleAnywhere, Category="Elysium") float Damping = 0.9f;
	UPROPERTY(VisibleAnywhere, Category="Elysium") float AngularSpring = 0.0f;
	UPROPERTY(VisibleAnywhere, Category="Elysium") float ConeAngleDegrees = 0.0f;
};

/** A complete stock-AnimDynamics single-body recipe for a one-bone breast record. */
USTRUCT()
struct FElysiumHairDynamicsBodyConfig
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, Category="Elysium") FName BoundBone;
	UPROPERTY(VisibleAnywhere, Category="Elysium") float GravityScale = 1.0f;
	UPROPERTY(VisibleAnywhere, Category="Elysium") float Damping = 0.9f;
	UPROPERTY(VisibleAnywhere, Category="Elysium") float AngularSpring = 0.0f;
	UPROPERTY(VisibleAnywhere, Category="Elysium") float ConeAngleDegrees = 0.0f;
};
