#pragma once

#include "CoreMinimal.h"

#include "AiInfra/ElysiumInfraActor.h"

#include "ElysiumHintActor.generated.h"

// A hint row: every `info_node*` / `info_hint` row `CNodeEnt::Spawn` (`0x102d78d0`) turns into a
// `CAI_Hint` — patrol points included (type 10000). Its live entity is `ai_hint`.
UCLASS(NotBlueprintable)
class AElysiumHintActor final : public AElysiumInfraActor
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Elysium|Keyfields")
	FElysiumHintKeyfields Hint;

	virtual FName FamilyTag() const override;
	virtual void GetKeyfieldViews(TArray<ElysiumKeyfieldAccess::FView>& OutViews) override;
	using AElysiumInfraActor::GetKeyfieldViews;
};
