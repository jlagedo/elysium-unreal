#include "AnimGraphNode_ElysiumBankRemap.h"

#define LOCTEXT_NAMESPACE "ElysiumAnimGraph"

FText UAnimGraphNode_ElysiumBankRemap::GetTooltipText() const
{
	return LOCTEXT("ElysiumBankRemap_Tooltip",
		"Carry one composed bank closure onto the playing body with retail's own bone remap: "
		"copy, a pure translation where exactly one bind sits at the origin, or a shortest-arc "
		"rotation scaled by the bind lengths. Translation only; rotation is never touched.");
}

FText UAnimGraphNode_ElysiumBankRemap::GetNodeTitle(ENodeTitleType::Type) const
{
	return LOCTEXT("ElysiumBankRemap_Title", "Remap VtMB Bank Closure");
}

FLinearColor UAnimGraphNode_ElysiumBankRemap::GetNodeTitleColor() const
{
	return FLinearColor(0.75f, 0.75f, 0.75f);
}

FString UAnimGraphNode_ElysiumBankRemap::GetNodeCategory() const
{
	return TEXT("Animation|Blends");
}

#undef LOCTEXT_NAMESPACE
