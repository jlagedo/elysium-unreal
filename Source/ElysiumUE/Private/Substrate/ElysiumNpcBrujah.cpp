#include "Substrate/ElysiumNpcBrujah.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcBrujah::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VBrujah"));
	return Row;
}

// Slot 546: `0x10367a10`, the class's own schedule id space.
const TCHAR* FElysiumNpcBrujah::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VBrujah"), SlotEn);
}
