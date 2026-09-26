#include "Substrate/ElysiumNpcSheriffMan.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcSheriffMan::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VSheriffMan"));
	return Row;
}

// Slot 420: `0x103ae6c0`.
void FElysiumNpcSheriffMan::NPCInit()
{
	SheriffManNPCInit();
}

// Slot 104: `0x103ae540`.
void FElysiumNpcSheriffMan::Precache()
{
	SheriffManPrecache();
}

// Slot 461: `0x103aeac0`, the selector tag 0x21 and then a direct call into the human line's `0x103851e0`.
int32 FElysiumNpcSheriffMan::SelectIdealStateRetail()
{
	SelectIdealStateSelector = 0x21;
	return HumanSelectIdealState();
}

// Slot 604: `0x103af960`, which replaces the Troika body wholesale; its argument is read by no arm.
int32 FElysiumNpcSheriffMan::SelectScheduleMeleeCombat(int32 Unused)
{
	(void)Unused;
	return SelectScheduleMeleeCombatSheriffMan();
}

// Slot 448: `0x103b0290`, its own arm and then a direct call into the Troika body `0x1029adb0`.
void FElysiumNpcSheriffMan::TaskFail(int32 Reason)
{
	SheriffManTaskFail(Reason);
	FElysiumNpc::TaskFail(Reason);
}

// Slot 605: `0x103afdb0`
int32 FElysiumNpcSheriffMan::SelectScheduleRangedCombat(int32 Arg)
{
	return SheriffManSelectScheduleRangedCombat(Arg);
}

// Slot 440: `0x103b0320`.
int32 FElysiumNpcSheriffMan::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return SheriffManTranslateSchedule(ScheduleNumber);
}

// Slot 337: `0x103ae840`.
int32 FElysiumNpcSheriffMan::GetUsedHullBits()
{
	return SpeciesUsedHullBits(TEXT("0x103ae840"));
}

// Slot 566: `0x103af810`, a replacement that does not chain.
bool FElysiumNpcSheriffMan::FValidateHintType(void* Hint)
{
	return SpeciesFValidateHintType(TEXT("CNPC_VSheriffMan"), Hint);
}

// Slot 546: `0x103adcb0`, the class's own schedule id space.
const TCHAR* FElysiumNpcSheriffMan::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VSheriffMan"), SlotEn);
}

// Slot 127: `0x103ae7f0`, whose body is the `CNPC_VVampireBoss` restore (`0x103c5910`, family
// SaveRestore10's `VampireBossRestore`) — the census's mechanism row for this class.
int32 FElysiumNpcSheriffMan::Restore(void* Archive)
{
	return VampireBossRestore(Archive);
}
