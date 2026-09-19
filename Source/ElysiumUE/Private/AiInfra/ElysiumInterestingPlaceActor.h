#pragma once

#include "CoreMinimal.h"

#include "AiInfra/ElysiumInfraActor.h"

#include "ElysiumInterestingPlaceActor.generated.h"

// An `intersting_place` row (`CAI_InterestingPlace`, datamap `0x1060bdd8`).
UCLASS(NotBlueprintable)
class AElysiumInterestingPlaceActor final : public AElysiumInfraActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Keyfields")
	FElysiumPlaceKeyfields Place;

	virtual FName FamilyTag() const override;
	virtual void GetKeyfieldViews(TArray<ElysiumKeyfieldAccess::FView>& OutViews) override;
	using AElysiumInfraActor::GetKeyfieldViews;
};
