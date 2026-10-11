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
};
