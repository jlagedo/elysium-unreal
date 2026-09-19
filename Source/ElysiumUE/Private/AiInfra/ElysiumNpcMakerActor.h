#pragma once

#include "CoreMinimal.h"

#include "AiInfra/ElysiumInfraActor.h"

#include "ElysiumNpcMakerActor.generated.h"

// An `npc_maker` / `npc_maker_fleshpile` / `npc_maker_zombie` row. `CNPCMaker` is a
// `CAI_BaseNPCTroika` in retail, so its row authors the maker's own keyfields AND the child
// NPC's: `MakeNPC` (`0x1034b7b0`) replays the maker's raw keyvalue block onto every child
// (`m_sRefMapDataBuffer +0x76cc`). The child's rows live in `ChildTemplate`; a key there is the
// child's, carried on the maker exactly as authored.
UCLASS(NotBlueprintable)
class AElysiumNpcMakerActor final : public AElysiumInfraActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Keyfields")
	FElysiumMakerKeyfields Maker;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Keyfields")
	FElysiumNpcKeyfields ChildTemplate;

	virtual FName FamilyTag() const override;
	virtual void GetKeyfieldViews(TArray<ElysiumKeyfieldAccess::FView>& OutViews) override;
	using AElysiumInfraActor::GetKeyfieldViews;
};
