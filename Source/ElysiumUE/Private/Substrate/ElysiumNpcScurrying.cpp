#include "Substrate/ElysiumNpcScurrying.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcScurrying::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VScurrying"));
	return Row;
}

// Slot 440: `0x103ac490`.
int32 FElysiumNpcScurrying::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return ScurryingTranslateSchedule(ScheduleNumber);
}

// Slot 337: `0x103ac4e0`.
int32 FElysiumNpcScurrying::GetUsedHullBits()
{
	return SpeciesUsedHullBits(TEXT("0x103ac4e0"));
}

// Slot 546: `0x103abd40`, the class's own schedule id space.
const TCHAR* FElysiumNpcScurrying::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VScurrying"), SlotEn);
}
