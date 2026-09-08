#include "Substrate/ElysiumBareEntityCameraSource.h"

#include "ElysiumEntity.h"
#include "Player/ElysiumCameraShots.h"

FVector FElysiumBareEntityCameraSource::GetCameraViewpointPosition() const
{
	// `10026850`: `WorldSpaceCenter(&tmp)` through vfunc `0x300`, then three floats out.
	return Entity ? ElysiumCameraShots::SurroundingBounds(*Entity).GetCenter() : FVector::ZeroVector;
}

FVector FElysiumBareEntityCameraSource::GetCameraTargetPosition(const FVector& AimFrom) const
{
	// `10026890`: the same centre, and the `from` argument is never read (`RET 8`, writes
	// `[ESP+0x14]`). Carried and discarded because retail computes and passes it.
	(void)AimFrom;
	return GetCameraViewpointPosition();
}

bool FElysiumBareEntityCameraSource::IsCameraSourceAlive() const
{
	return Entity != nullptr && !Entity->IsDead();
}

IElysiumCameraOverrideSource* FElysiumBareEntityCameraSourcePool::Bind(FElysiumEntity& Entity)
{
	for (FElysiumBareEntityCameraSource& Slot : Slots)
	{
		if (Slot.BoundEntity() == &Entity)
		{
			return &Slot;
		}
	}
	FElysiumBareEntityCameraSource& Slot = Slots[NextSlot];
	NextSlot = (NextSlot + 1) % NumSlots;
	Slot.Bind(&Entity);
	return &Slot;
}
