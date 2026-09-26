#include "Substrate/ElysiumNpcManBat.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcManBat::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VManBat"));
	return Row;
}

// Slot 420: `0x1038b070`.
void FElysiumNpcManBat::NPCInit()
{
	ManBatNPCInit();
}

// Slot 104: `0x1038aec0`.
void FElysiumNpcManBat::Precache()
{
	ManBatPrecache();
}

// Slot 438: `0x1038e340`, which replaces the whole selector (the Troika selector's species hook).
int32 FElysiumNpcManBat::SpeciesSelectSchedule()
{
	return ManBatSelectSchedule();
}

// Slot 337: `0x1038b100`.
int32 FElysiumNpcManBat::GetUsedHullBits()
{
	return SpeciesUsedHullBits(TEXT("0x1038b100"));
}

// Slot 566: `0x1038e480`, a whole replacement body.
bool FElysiumNpcManBat::FValidateHintType(void* Hint)
{
	return ManBatFValidateHintType(Hint);
}

// Slot 546: `0x10389f50`, the class's own schedule id space.
const TCHAR* FElysiumNpcManBat::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VManBat"), SlotEn);
}
