#include "Substrate/ElysiumNpcTzimisceHeadClaw.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumAnimEvent.h"

const FElysiumNpcClass* FElysiumNpcTzimisceHeadClaw::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VTzimisceHeadClaw"));
	return Row;
}

// The melee quartet, slots 599-602: `0x103c19e0`, `0x103c1a60`, `0x103c1ad0`, `0x103c1b10`. Every recovered dispatch
// site of 599 pushes `GetEnemy()` (family TroikaHelpers' `Slot599`), which the body is handed.
bool FElysiumNpcTzimisceHeadClaw::Slot599(int32 Arg)
{
	(void)Arg;
	return FUN_103c19e0(static_cast<const FElysiumNpc*>(this)->GetEnemy());
}

bool FElysiumNpcTzimisceHeadClaw::Slot600(FElysiumEntity* Enemy)
{
	return FUN_103c1a60(Enemy);
}

void FElysiumNpcTzimisceHeadClaw::Slot601(FElysiumEntity* Enemy)
{
	FUN_103c1ad0(Enemy);
}

bool FElysiumNpcTzimisceHeadClaw::Slot602()
{
	return FUN_103c1b10();
}

// Slot 420: `0x103c1c80`.
void FElysiumNpcTzimisceHeadClaw::NPCInit()
{
	TzimisceHeadClawNPCInit();
}

// Slot 104: `0x103c1400`.
void FElysiumNpcTzimisceHeadClaw::Precache()
{
	TzimisceHeadClawPrecache();
}

// Slot 126: `0x103c2810`.
int32 FElysiumNpcTzimisceHeadClaw::Save(void* Archive)
{
	return TzimisceHeadClawSave(Archive);
}

// Slot 127: `0x103c2860`.
int32 FElysiumNpcTzimisceHeadClaw::Restore(void* Archive)
{
	return TzimisceHeadClawRestore(Archive);
}

// Slot 310: `0x103c1cd0`, which calls the Troika body `0x10295750` directly.
void FElysiumNpcTzimisceHeadClaw::SetActivity(int32 Activity)
{
	TzimisceHeadClawSetActivity(Activity);
}

// Slot 453: `0x103c16f0`, a direct call into the Troika body `0x102ad140` first, then its own bits.
void FElysiumNpcTzimisceHeadClaw::BuildScheduleTestBits(FElysiumNpcConditions& InOutMask)
{
	FElysiumNpc::BuildScheduleTestBits(InOutMask);
	TzimisceHeadClawBuildScheduleTestBits(InOutMask);
}

// Slot 440: `0x103c1720`.
int32 FElysiumNpcTzimisceHeadClaw::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return TzimisceHeadClawTranslateSchedule(ScheduleNumber);
}

// Slot 337: `0x103c1cb0`.
int32 FElysiumNpcTzimisceHeadClaw::GetUsedHullBits()
{
	return SpeciesUsedHullBits(TEXT("0x103c1cb0"));
}

// Slot 546: `0x103c0c90`, the class's own schedule id space.
const TCHAR* FElysiumNpcTzimisceHeadClaw::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VTzimisceHeadClaw"), SlotEn);
}

// Slot 332: `0x103c1d80`, a replacement that does not chain.
void FElysiumNpcTzimisceHeadClaw::Slot332(FElysiumEntity* SlowTarget)
{
	HeadClawSlot332(SlowTarget);
}

// Slot 259: `0x103c1540`, the footstep body of `docs/vtmb/footsteps.md` §1.7; an id it does not
// claim is a direct call into the base body.
bool FElysiumNpcTzimisceHeadClaw::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	return SpeciesFootstepAnimEvent(TEXT("npc_VTzimisceHeadClaw"), Event);
}
