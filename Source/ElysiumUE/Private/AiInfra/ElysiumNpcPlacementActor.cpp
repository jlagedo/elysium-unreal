#include "AiInfra/ElysiumNpcPlacementActor.h"

#include "ElysiumBakedTags.h"

FName AElysiumNpcPlacementActor::FamilyTag() const
{
	return ElysiumBakedTags::InfraNpc;
}

void AElysiumNpcPlacementActor::GetKeyfieldViews(TArray<ElysiumKeyfieldAccess::FView>& OutViews)
{
	OutViews.Add({ FElysiumNpcKeyfields::StaticStruct(), &Npc });
	Super::GetKeyfieldViews(OutViews);
}
