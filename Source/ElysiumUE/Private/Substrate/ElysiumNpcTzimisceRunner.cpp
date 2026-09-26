#include "Substrate/ElysiumNpcTzimisceRunner.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumAnimEvent.h"

const FElysiumNpcClass* FElysiumNpcTzimisceRunner::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VTzimisceRunner"));
	return Row;
}

// Slot 588: `0x103c3fd0`, `RestartIdealActivity(1)` with no `IsActivityFinished` gate.
void FElysiumNpcTzimisceRunner::Slot588()
{
	FUN_103c3fd0();
}

// The melee quartet, slots 599-602: `0x103c3960`, `0x103c39e0`, `0x103c3a70`, `0x103c3ab0`. Every recovered dispatch
// site of 599 pushes `GetEnemy()` (family TroikaHelpers' `Slot599`), which the body is handed.
bool FElysiumNpcTzimisceRunner::Slot599(int32 Arg)
{
	(void)Arg;
	return FUN_103c3960(static_cast<const FElysiumNpc*>(this)->GetEnemy());
}

bool FElysiumNpcTzimisceRunner::Slot600(FElysiumEntity* Enemy)
{
	return FUN_103c39e0(Enemy);
}

void FElysiumNpcTzimisceRunner::Slot601(FElysiumEntity* Enemy)
{
	FUN_103c3a70(Enemy);
}

bool FElysiumNpcTzimisceRunner::Slot602()
{
	return FUN_103c3ab0();
}

// Slot 130: `0x103c3c40`, which calls the Troika body first.
void FElysiumNpcTzimisceRunner::OnRestore(bool bFromLoad)
{
	TzimisceRunnerOnRestore(bFromLoad);
}

// Slot 104: `0x103c31e0`.
void FElysiumNpcTzimisceRunner::Precache()
{
	TzimisceRunnerPrecache();
}

// Slot 310: `0x103c3d80`, which calls the Troika body `0x10295750` directly.
void FElysiumNpcTzimisceRunner::SetActivity(int32 Activity)
{
	TzimisceRunnerSetActivity(Activity);
}

// Slot 375: `0x103c3e10`, which calls the Troika body `0x10295590` directly.
int32 FElysiumNpcTzimisceRunner::NPC_EarlyTranslateActivity(int32 Activity)
{
	return TzimisceRunnerNpcEarlyTranslateActivity(Activity);
}

// Slot 604: `0x103c4430`, which replaces the Troika body wholesale; its argument is read by no arm.
int32 FElysiumNpcTzimisceRunner::SelectScheduleMeleeCombat(int32 Unused)
{
	(void)Unused;
	return SelectScheduleMeleeCombatChangLine(false);
}

// Slot 440: `0x103c3560`.
int32 FElysiumNpcTzimisceRunner::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return TzimisceRunnerTranslateSchedule(ScheduleNumber);
}

// Slot 337: `0x103c3cb0`.
int32 FElysiumNpcTzimisceRunner::GetUsedHullBits()
{
	return SpeciesUsedHullBits(TEXT("0x103c3cb0"));
}

// Slot 546: `0x103c2a60`, the class's own schedule id space.
const TCHAR* FElysiumNpcTzimisceRunner::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VTzimisceRunner"), SlotEn);
}

// Slot 259: `0x103c32c0`, the footstep body of `docs/vtmb/footsteps.md` §1.7; an id it does not
// claim is a direct call into the base body.
bool FElysiumNpcTzimisceRunner::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	return SpeciesFootstepAnimEvent(TEXT("npc_VTzimisceRunner"), Event);
}

// `ENpcPredicate::FormBit`: the runner's slot-375 body reads its own form byte `+0x6672`.
bool FElysiumNpcTzimisceRunner::AnimFormBit() const
{
	return bTzimisceRunnerForm;
}

// Slot 400: `0x103c3060`, `CNPC_VTzimisceRunner::vfunc400` — `return 1;`.
bool FElysiumNpcTzimisceRunner::AllowsKnockbackBypass()
{
	return true;
}
