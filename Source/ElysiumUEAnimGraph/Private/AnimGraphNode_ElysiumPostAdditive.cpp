#include "AnimGraphNode_ElysiumPostAdditive.h"

#define LOCTEXT_NAMESPACE "ElysiumAnimGraph"

FText UAnimGraphNode_ElysiumPostAdditive::GetTooltipText() const
{
	return LOCTEXT("ElysiumPostAdditive_Tooltip",
		"Compose a VtMB _delta onto a pose with retail's post-multiply rule: "
		"q = normalize(q * scale(delta, alpha)), pos += delta.pos * alpha.");
}

FText UAnimGraphNode_ElysiumPostAdditive::GetNodeTitle(ENodeTitleType::Type) const
{
	return LOCTEXT("ElysiumPostAdditive_Title", "Apply VtMB Delta (post-multiply)");
}

FLinearColor UAnimGraphNode_ElysiumPostAdditive::GetNodeTitleColor() const
{
	return FLinearColor(0.75f, 0.75f, 0.75f);
}

FString UAnimGraphNode_ElysiumPostAdditive::GetNodeCategory() const
{
	return TEXT("Animation|Blends");
}

#undef LOCTEXT_NAMESPACE
