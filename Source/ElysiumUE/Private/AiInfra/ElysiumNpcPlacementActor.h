#pragma once

#include "CoreMinimal.h"

#include "AiInfra/ElysiumInfraActor.h"

#include "ElysiumNpcPlacementActor.generated.h"

// A placed NPC row — every `npc_*` classname a map authors that is not a maker. The keyfields are
// the `CAI_BaseNPCTroika` chain down to `CBaseToggle`; the live entity is whatever class the
// registry resolves the classname to (a classname with no class stays an inert record).
UCLASS(NotBlueprintable)
class AElysiumNpcPlacementActor final : public AElysiumInfraActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Keyfields")
	FElysiumNpcKeyfields Npc;

	virtual FName FamilyTag() const override;
	virtual void GetKeyfieldViews(TArray<ElysiumKeyfieldAccess::FView>& OutViews) override;
	using AElysiumInfraActor::GetKeyfieldViews;
};
