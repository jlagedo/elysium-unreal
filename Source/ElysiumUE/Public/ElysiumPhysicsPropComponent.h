#pragma once
#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "ElysiumPhysicsPropComponent.generated.h"

/** Render mesh retains its general collision cook. A physics prop uses the model creator's
 * selected native BodySetup, leaving unrelated static/model collision consumers unchanged. */
UCLASS()
class ELYSIUMUE_API UElysiumPhysicsPropComponent : public UStaticMeshComponent
{
	GENERATED_BODY()
public:
	bool bUseSelectedBody = false;
	UPROPERTY(Transient) TObjectPtr<UBodySetup> RecipeBody;
	virtual UBodySetup* GetBodySetup() override;
	virtual void OnDestroyPhysicsState() override;
	virtual void OnUpdateTransform(EUpdateTransformFlags Flags, ETeleportType Teleport = ETeleportType::None) override;
#if WITH_EDITOR
	virtual void PreStaticMeshCompilation() override;
	virtual void PostStaticMeshCompilation() override;
#endif
	UFUNCTION() void NativeWake(UPrimitiveComponent* Component, FName BoneName);
	UFUNCTION() void NativeSleep(UPrimitiveComponent* Component, FName BoneName);
protected:
	virtual bool ShouldCreatePhysicsState() const override;
private:
	bool HasIndependentPhysicsInputs() const;
};
