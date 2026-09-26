#include "Substrate/ElysiumNpcChangBros.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcChangBros::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VChangBros"));
	return Row;
}

// Slot 420: `0x1036b050`.
void FElysiumNpcChangBros::NPCInit()
{
	ChangBrosNPCInit();
}

// Slot 104: `0x1036ae60`.
void FElysiumNpcChangBros::Precache()
{
	ChangBrosPrecache();
}

// Slot 127: `0x1036b170`.
int32 FElysiumNpcChangBros::Restore(void* Archive)
{
	return ChangBrosRestore(Archive);
}

// Slot 461: `0x1036b500`, the selector tag 0xa and then a direct call into the human line's `0x103851e0`.
int32 FElysiumNpcChangBros::SelectIdealStateRetail()
{
	SelectIdealStateSelector = 0xa;
	return HumanSelectIdealState();
}

// Slot 604: `0x1036d800`, which replaces the Troika body wholesale; its argument is read by no arm.
int32 FElysiumNpcChangBros::SelectScheduleMeleeCombat(int32 Unused)
{
	(void)Unused;
	return SelectScheduleMeleeCombatChangLine(true);
}

// Slot 448: `0x1036d1d0`, its own arm and then a direct call into the Troika body `0x1029adb0`.
void FElysiumNpcChangBros::TaskFail(int32 Reason)
{
	ChangBrosTaskFail(Reason);
	FElysiumNpc::TaskFail(Reason);
}

// Slot 440: `0x1036b460`.
int32 FElysiumNpcChangBros::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return ChangBrosTranslateSchedule(ScheduleNumber);
}

// Slot 566: `0x1036c6a0`, a replacement that does not chain.
bool FElysiumNpcChangBros::FValidateHintType(void* Hint)
{
	return SpeciesFValidateHintType(TEXT("CNPC_VChangBros"), Hint);
}

// Slot 546: `0x1036a3f0`, the class's own schedule id space.
const TCHAR* FElysiumNpcChangBros::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VChangBros"), SlotEn);
}
