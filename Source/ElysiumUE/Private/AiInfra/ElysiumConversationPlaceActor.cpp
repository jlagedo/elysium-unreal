#include "AiInfra/ElysiumConversationPlaceActor.h"

#include "ElysiumBakedTags.h"

FName AElysiumConversationPlaceActor::FamilyTag() const
{
	return ElysiumBakedTags::InfraConversation;
}

void AElysiumConversationPlaceActor::GetKeyfieldViews(TArray<ElysiumKeyfieldAccess::FView>& OutViews)
{
	OutViews.Add({ FElysiumConversationPlaceKeyfields::StaticStruct(), &Conversation });
	Super::GetKeyfieldViews(OutViews);
}
