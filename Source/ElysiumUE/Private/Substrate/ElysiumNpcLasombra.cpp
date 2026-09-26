#include "Substrate/ElysiumNpcLasombra.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcLasombra::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VLasombra"));
	return Row;
}

// Slot 592: `0x103893c0`, whose miss calls the Troika body `0x102953e0` directly.
bool FElysiumNpcLasombra::CanSeekCover()
{
	return LasombraCanSeekCover();
}

// Slot 546: `0x10388f80`, the class's own schedule id space.
const TCHAR* FElysiumNpcLasombra::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VLasombra"), SlotEn);
}
