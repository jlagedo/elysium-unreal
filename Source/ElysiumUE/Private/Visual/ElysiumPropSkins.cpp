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

	// "The skin-index clamp is engine behaviour, and the import reproduces it" -- VtMB clamps an
	// out-of-range `skin` keyfield to the last family rather than falling back to 0 or refusing to
	// draw, and the clamp belongs here so every call site keeps asking for the index the placement
	// wrote. `FamilyCount == 0` means this stem carries no clamp table (an asset authored before
	// the field existed), so the lookup falls back to the plain array bound it always used.
	int32 ClampedFamily = Family;
	if (Model.FamilyCount > 0 && Family >= Model.FamilyCount)
	{
		ClampedFamily = Model.FamilyCount - 1;
	}

	if (ClampedFamily <= 0 || !Model.Families.IsValidIndex(ClampedFamily)
		|| Model.Families[ClampedFamily].Overrides.Num() == 0)
	{
		return nullptr;   // family 0 (the authored set), an unstored family, or one that repaints nothing
	}
	return &Model.Families[ClampedFamily];
}
