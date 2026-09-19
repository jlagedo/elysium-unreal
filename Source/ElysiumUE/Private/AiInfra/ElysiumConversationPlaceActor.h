#pragma once

#include "CoreMinimal.h"

#include "AiInfra/ElysiumInfraActor.h"

#include "ElysiumConversationPlaceActor.generated.h"

// An `intersting_place_conversation` row (`CAI_InterestingPlaceConverstation`, datamap
// `0x1060c2c0`).
UCLASS(NotBlueprintable)
class AElysiumConversationPlaceActor final : public AElysiumInfraActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Keyfields")
	FElysiumConversationPlaceKeyfields Conversation;

	virtual FName FamilyTag() const override;
	virtual void GetKeyfieldViews(TArray<ElysiumKeyfieldAccess::FView>& OutViews) override;
	using AElysiumInfraActor::GetKeyfieldViews;
};
