#pragma once

#include "CoreMinimal.h"
#include "Navigation/NavLinkProxy.h"
#include "ElysiumNavJumpLink.generated.h"

class UNavLinkCustomComponent;

// The editor bakes one actor per human AIN jump connection. The native smart link owns
// engine traversal only; the entity's schedule observes its motor's navigation sample.
UCLASS(NotBlueprintable)
class AElysiumNavJumpLink final : public ANavLinkProxy
{

	GENERATED_BODY()

public:
	AElysiumNavJumpLink(const FObjectInitializer& ObjectInitializer);

	UFUNCTION(BlueprintCallable, Category="Elysium|Bake")
	void ConfigureJumpLink(FVector RelativeStart, FVector RelativeEnd,
		int32 InSourceLinkIndex, int32 InSourceNode, int32 InDestinationNode);

	UPROPERTY(VisibleAnywhere, Category="Elysium|Navigation")
	int32 SourceLinkIndex = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Navigation")
	int32 SourceNode = INDEX_NONE;
	UPROPERTY(VisibleAnywhere, Category="Elysium|Navigation")
	int32 DestinationNode = INDEX_NONE;

private:
	void OnJumpLinkReached(UNavLinkCustomComponent* Link, UObject* PathingAgent,
		const FVector& Destination);
};
