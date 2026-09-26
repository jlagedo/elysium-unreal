#include "Substrate/ElysiumNpcPlaceholder.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcPlaceholder::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VPlaceholder"));
	return Row;
}

// Slot 420: `0x103a4350`.
void FElysiumNpcPlaceholder::NPCInit()
{
	PlaceholderNPCInit();
}

// Slot 437: `0x103a43f0`.
int32 FElysiumNpcPlaceholder::PreSelectSchedule()
{
	return PlaceholderPreSelectSchedule();
}

// Slot 438: `0x103a4410`, which replaces the whole selector (the Troika selector's species hook).
int32 FElysiumNpcPlaceholder::SpeciesSelectSchedule()
{
	return PlaceholderSelectSchedule();
}

// Slot 546: `0x103a3c50`, the class's own schedule id space.
const TCHAR* FElysiumNpcPlaceholder::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VPlaceholder"), SlotEn);
}
