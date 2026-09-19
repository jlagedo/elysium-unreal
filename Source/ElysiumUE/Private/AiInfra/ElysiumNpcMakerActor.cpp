#include "AiInfra/ElysiumNpcMakerActor.h"

#include "ElysiumBakedTags.h"

FName AElysiumNpcMakerActor::FamilyTag() const
{
	return ElysiumBakedTags::InfraMaker;
}

void AElysiumNpcMakerActor::GetKeyfieldViews(TArray<ElysiumKeyfieldAccess::FView>& OutViews)
{
	// The maker's own rows first, then the inherited Troika chain, then `CBaseEntity`: the order
	// retail's datamap walk resolves a key in.
	OutViews.Add({ FElysiumMakerKeyfields::StaticStruct(), &Maker });
	OutViews.Add({ FElysiumNpcKeyfields::StaticStruct(), &ChildTemplate });
	Super::GetKeyfieldViews(OutViews);
}
