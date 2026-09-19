#include "AiInfra/ElysiumInterestingPlaceActor.h"

#include "ElysiumBakedTags.h"

FName AElysiumInterestingPlaceActor::FamilyTag() const
{
	return ElysiumBakedTags::InfraPlace;
}

void AElysiumInterestingPlaceActor::GetKeyfieldViews(TArray<ElysiumKeyfieldAccess::FView>& OutViews)
{
	OutViews.Add({ FElysiumPlaceKeyfields::StaticStruct(), &Place });
	Super::GetKeyfieldViews(OutViews);
}
