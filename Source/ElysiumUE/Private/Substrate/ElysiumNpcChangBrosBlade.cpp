#include "Substrate/ElysiumNpcChangBrosBlade.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcChangBrosBlade::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VChangBrosBlade"));
	return Row;
}

// Slot 420: `0x1036f100`.
void FElysiumNpcChangBrosBlade::NPCInit()
{
	ChangBrosBladeNPCInit();
}

// Slot 546: `0x1036ecf0`, the class's own schedule id space.
const TCHAR* FElysiumNpcChangBrosBlade::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VChangBrosBlade"), SlotEn);
}
