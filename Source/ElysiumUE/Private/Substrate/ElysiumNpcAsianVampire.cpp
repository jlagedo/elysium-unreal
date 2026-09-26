#include "Substrate/ElysiumNpcAsianVampire.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcAsianVampire::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VAsianVampire"));
	return Row;
}

// Slot 420: `0x10360ce0`.
void FElysiumNpcAsianVampire::NPCInit()
{
	AsianVampireNPCInit();
}

// Slot 104: `0x10360bc0`.
void FElysiumNpcAsianVampire::Precache()
{
	AsianVampirePrecache();
}

// Slot 461: `0x10361060`, the selector tag 0x6 and then a direct call into the human line's `0x103851e0`.
int32 FElysiumNpcAsianVampire::SelectIdealStateRetail()
{
	SelectIdealStateSelector = 0x6;
	return HumanSelectIdealState();
}

// Slot 604: `0x10361be0`, which replaces the Troika body wholesale; its argument is read by no arm.
int32 FElysiumNpcAsianVampire::SelectScheduleMeleeCombat(int32 Unused)
{
	(void)Unused;
	return SelectScheduleMeleeCombatAsianVampire();
}

// Slot 448: `0x10362390`, its own arm and then a direct call into the Troika body `0x1029adb0`.
void FElysiumNpcAsianVampire::TaskFail(int32 Reason)
{
	AsianVampireTaskFail(Reason);
	FElysiumNpc::TaskFail(Reason);
}

// Slot 605: `0x103620d0`
int32 FElysiumNpcAsianVampire::SelectScheduleRangedCombat(int32 Arg)
{
	return AsianVampireSelectScheduleRangedCombat(Arg);
}

// Slot 440: `0x10362910`.
int32 FElysiumNpcAsianVampire::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return AsianVampireTranslateSchedule(ScheduleNumber);
}

// Slot 566: `0x10361470`, a replacement that does not chain.
bool FElysiumNpcAsianVampire::FValidateHintType(void* Hint)
{
	return SpeciesFValidateHintType(TEXT("CNPC_VAsianVampire"), Hint);
}

// Slot 546: `0x10360610`, the class's own schedule id space.
const TCHAR* FElysiumNpcAsianVampire::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VAsianVampire"), SlotEn);
}

// Slot 127: `0x10360e10`, whose body is the `CNPC_VVampireBoss` restore (`0x103c5910`, family
// SaveRestore10's `VampireBossRestore`) — the census's mechanism row for this class.
int32 FElysiumNpcAsianVampire::Restore(void* Archive)
{
	return VampireBossRestore(Archive);
}
