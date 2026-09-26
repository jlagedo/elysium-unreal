#include "Substrate/ElysiumNpcHumanCombatPatrol.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcHumanCombatPatrol::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VHumanCombatPatrol"));
	return Row;
}

// Slot 546: `0x103878b0`, the class's own schedule id space.
const TCHAR* FElysiumNpcHumanCombatPatrol::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VHumanCombatPatrol"), SlotEn);
}
