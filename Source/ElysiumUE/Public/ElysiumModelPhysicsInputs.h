#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "ElysiumModelPhysicsInputs.generated.h"

class UStaticMesh;
class UBodySetup;

/** Cooked inputs for PhysModelCreate 1002e3a0: first binary solid, first solid block.
 * Geometry and mass integration belong to Chaos. This is authored data, not an IVP layout. */
UCLASS()
class ELYSIUMUE_API UElysiumModelPhysicsInputs : public UAssetUserData
{
	GENERATED_BODY()
public:
	UPROPERTY(VisibleAnywhere) bool bSourceBody = false;
	UPROPERTY(VisibleAnywhere) float MassKg = 1.f;
	UPROPERTY(VisibleAnywhere) float InertiaScale = 1.f;
	UPROPERTY(VisibleAnywhere) float LinearDamping = .1f;
	UPROPERTY(VisibleAnywhere) float AngularDamping = .1f;
	UPROPERTY(VisibleAnywhere) float RotInertiaLimit = .5f;
	UPROPERTY(VisibleAnywhere) float Volume = 0.f;
	UPROPERTY(VisibleAnywhere) float Drag = 1.f;
	UPROPERTY(VisibleAnywhere) float RollingDrag = 0.f;
	UPROPERTY(VisibleAnywhere) bool bMassCenter = false;
	UPROPERTY(VisibleAnywhere) FVector MassCenterCm = FVector::ZeroVector;
	UPROPERTY(VisibleAnywhere) FString SurfaceProperty;
	UPROPERTY(VisibleAnywhere) FString SolidPairsJson;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UBodySetup> SelectedBody;
	UFUNCTION(BlueprintCallable, Category="Elysium|Physics")
	static void ApplyJson(UStaticMesh* Mesh, const FString& Json, bool& bOutOk, FString& OutError);
	static const UElysiumModelPhysicsInputs* Find(const UStaticMesh* Mesh);
};
