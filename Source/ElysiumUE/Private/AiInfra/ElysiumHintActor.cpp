#include "AiInfra/ElysiumHintActor.h"

#include "ElysiumBakedTags.h"

FName AElysiumHintActor::FamilyTag() const
{
	return ElysiumBakedTags::InfraHint;
}

void AElysiumHintActor::GetKeyfieldViews(TArray<ElysiumKeyfieldAccess::FView>& OutViews)
{
	OutViews.Add({ FElysiumHintKeyfields::StaticStruct(), &Hint });
	Super::GetKeyfieldViews(OutViews);
}
