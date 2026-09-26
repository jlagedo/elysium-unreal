#include "Substrate/ElysiumNpcTaxiDriver.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcTaxiDriver::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VTaxiDriver"));
	return Row;
}

// Slot 420: `0x103b35c0`.
void FElysiumNpcTaxiDriver::NPCInit()
{
	TaxiDriverNPCInit();
}

// Slot 546: `0x103b3170`, the class's own schedule id space.
const TCHAR* FElysiumNpcTaxiDriver::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VTaxiDriver"), SlotEn);
}
