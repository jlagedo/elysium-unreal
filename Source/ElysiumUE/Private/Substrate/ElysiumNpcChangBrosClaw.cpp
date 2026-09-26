#include "Substrate/ElysiumNpcChangBrosClaw.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcChangBrosClaw::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VChangBrosClaw"));
	return Row;
}

// Slot 420: `0x1036f900`.
void FElysiumNpcChangBrosClaw::NPCInit()
{
	ChangBrosClawNPCInit();
}

// Slot 546: `0x1036f4f0`, the class's own schedule id space.
const TCHAR* FElysiumNpcChangBrosClaw::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VChangBrosClaw"), SlotEn);
}
