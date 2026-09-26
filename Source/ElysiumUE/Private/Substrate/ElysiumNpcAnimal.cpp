#include "Substrate/ElysiumNpcAnimal.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcAnimal::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VAnimal"));
	return Row;
}

// Slot 482: `0x1035fd40`, a standalone copy that keeps a SCRIPT-state body's answer.
int32 FElysiumNpcAnimal::CanPlaySequence(bool bDisregardState, int32 InterruptLevel)
{
	return FUN_1035fd40(bDisregardState, InterruptLevel);
}

// Slot 461: `0x1035fe80`, chaining the Troika body directly.
int32 FElysiumNpcAnimal::SelectIdealStateRetail()
{
	return AnimalSelectIdealState();
}

// Slot 546: `0x1035edb0`, the class's own schedule id space.
const TCHAR* FElysiumNpcAnimal::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VAnimal"), SlotEn);
}

// Slot 563: `0x1035f5c0`, the `OffsetOnly` shape; a replacement that does not chain.
void FElysiumNpcAnimal::TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm,
	void* Tolerance, void* SecondTolerance)
{
	TranslateEnemyChasePositionShaped(EChaseTranslateShape::OffsetOnly, Enemy, ChasePositionCm, Tolerance,
		SecondTolerance);
}
