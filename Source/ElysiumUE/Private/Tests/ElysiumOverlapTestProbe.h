#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "ElysiumOverlapTestProbe.generated.h"

// Engine-backed automation probe: dynamic component-overlap delegates require a reflected UObject
// receiver, so this tiny test-only object records the notification without adding a production hook.
UCLASS()
class UElysiumOverlapTestProbe final : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	int32 BeginCount = 0;

	UPROPERTY()
	TObjectPtr<AActor> LastOther = nullptr;

	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComponent, int32 OtherBodyIndex, bool bFromSweep,
		const FHitResult& SweepResult)
	{
		(void)OverlappedComponent;
		(void)OtherComponent;
		(void)OtherBodyIndex;
		(void)bFromSweep;
		(void)SweepResult;
		++BeginCount;
		LastOther = OtherActor;
	}
};
