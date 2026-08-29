#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "AnimGraphNode_Base.h"
#include "ElysiumBankRemapNode.h"

#include "AnimGraphNode_ElysiumBankRemap.generated.h"

/**
 * The graph-editor face of `FAnimNode_ElysiumBankRemap` -- retail's per-body bank bone-remap.
 *
 * It exists so the node is placeable: the player graph is generated from C++ and exported to
 * tracked T3D text, and both the generator's `FindObject<UClass>` lookup and the T3D importer's
 * class resolution address a `UAnimGraphNode_Base` subclass by path
 * (`/Script/ElysiumUEAnimGraph.AnimGraphNode_ElysiumBankRemap`). The remap itself is entirely in
 * the runtime node; nothing here interprets a pose.
 */
UCLASS()
class UAnimGraphNode_ElysiumBankRemap : public UAnimGraphNode_Base
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_ElysiumBankRemap Node;

	//~ Begin UEdGraphNode Interface
	virtual FText		 GetTooltipText() const override;
	virtual FText		 GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	//~ End UEdGraphNode Interface

	//~ Begin UAnimGraphNode_Base Interface
	virtual FString GetNodeCategory() const override;
	//~ End UAnimGraphNode_Base Interface
};
