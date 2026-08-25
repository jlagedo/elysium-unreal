#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "AnimGraphNode_Base.h"
#include "ElysiumPostAdditiveNode.h"

#include "AnimGraphNode_ElysiumPostAdditive.generated.h"

/**
 * The graph-editor face of `FAnimNode_ElysiumPostAdditive` -- VtMB's post-multiply additive.
 *
 * It exists so the node is placeable: the player graph is generated from C++ and exported to
 * tracked T3D text, and both the generator's `FindObject<UClass>` lookup and the T3D importer's
 * class resolution address a `UAnimGraphNode_Base` subclass by path
 * (`/Script/ElysiumUEAnimGraph.AnimGraphNode_ElysiumPostAdditive`). The composition rule itself
 * is entirely in the runtime node; nothing here interprets a pose.
 */
UCLASS()
class UAnimGraphNode_ElysiumPostAdditive : public UAnimGraphNode_Base
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, Category = Settings)
	FAnimNode_ElysiumPostAdditive Node;

	// UEdGraphNode interface
	virtual FText GetTooltipText() const override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FLinearColor GetNodeTitleColor() const override;
	// End of UEdGraphNode interface

	// UAnimGraphNode_Base interface
	virtual FString GetNodeCategory() const override;
	// End of UAnimGraphNode_Base interface
};
