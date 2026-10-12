#pragma once
#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumEntityQueryBox.generated.h"

// A SOLID_BBOX/OBB query carrier, independent of m_pPhysicsObject and the visual mesh.
UCLASS()
class ELYSIUMUE_API UElysiumEntityQueryBox : public UBoxComponent
{
	GENERATED_BODY()
public:
	FElysiumEntityHandle Entity;
};
