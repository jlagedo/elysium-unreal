#include "Substrate/ElysiumNpcVampire.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcVampire::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VVampire"));
	return Row;
}

// Slot 546: `0x103c4a80`, the class's own schedule id space.
const TCHAR* FElysiumNpcVampire::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VVampire"), SlotEn);
}
