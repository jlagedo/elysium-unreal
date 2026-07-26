#include "ElysiumPropSkins.h"

const FElysiumSkinFamily* UElysiumPropSkinSet::Find(FName Stem, int32 Family) const
{
	if (!bIndexed)
	{
		Index.Reset();
		for (int32 I = 0; I < Models.Num(); ++I)
		{
			Index.Add(Models[I].Stem, I);
		}
		bIndexed = true;
	}

	const int32* Slot = Index.Find(Stem);
	if (!Slot || !Models.IsValidIndex(*Slot))
	{
		return nullptr;
	}

	const FElysiumPropSkinModel& Model = Models[*Slot];
	if (!Model.Families.IsValidIndex(Family) || Model.Families[Family].Overrides.Num() == 0)
	{
		return nullptr;   // family 0 (the authored set), an unstored family, or one that repaints nothing
	}
	return &Model.Families[Family];
}
