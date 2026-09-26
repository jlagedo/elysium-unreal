#include "Substrate/ElysiumNpcGuard1.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcGuard1::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VGuard1"));
	return Row;
}

// Slot 420: `0x1037e240`.
void FElysiumNpcGuard1::NPCInit()
{
	Guard1NPCInit();
}

// Slot 463: `0x1037d020`, the enemy-is-the-player pre-step, its own copy of the holster/draw
// switch, then a direct call into the Troika body.
void FElysiumNpcGuard1::OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState)
{
	Guard1StateChangePreStep();
	ApplyStateWeaponVisibility(NewState);
	OnStateChangeTroika(OldState, NewState);
}

// Slot 461: `0x1037d290`, chaining the human line's `0x103851e0` directly.
int32 FElysiumNpcGuard1::SelectIdealStateRetail()
{
	return Guard1SelectIdealState();
}

// Slot 453: `0x1037cdf0`. It calls the EMPTY base `CAI_BaseNPC::BuildScheduleTestBits` (`0x10280fb0`,
// nothing to run) rather than the Troika body, then its state ladder; `CacheInterruptConditions`
// (`0x1026a0f0`) adds `NPC_FREEZE` after the virtual.
void FElysiumNpcGuard1::BuildScheduleTestBits(FElysiumNpcConditions& InOutMask)
{
	Guard1BuildScheduleTestBits(InOutMask);
	InOutMask.Set(EElysiumNpcCond::NpcFreeze);
}

// Slot 440: `0x1037d240`.
int32 FElysiumNpcGuard1::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return Guard1TranslateSchedule(ScheduleNumber);
}

// Slot 546: `0x1037c800`, the class's own schedule id space.
const TCHAR* FElysiumNpcGuard1::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VGuard1"), SlotEn);
}
