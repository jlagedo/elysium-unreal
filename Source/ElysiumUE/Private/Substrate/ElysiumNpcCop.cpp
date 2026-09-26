#include "Substrate/ElysiumNpcCop.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcCop::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VCop"));
	return Row;
}

// Slot 597: `0x10372cc0`, the `m_hPursuitPlayer` latch and the "Player D_HT 10" relationship in
// front of the Troika body `0x102b4fb0`, which it calls directly.
void FElysiumNpcCop::Slot597(FElysiumEntity* Other, int32 Priority)
{
	CopSlot597Prologue(Other);
	FElysiumNpc::Slot597(Other, Priority);
}

// Slot 420: `0x10372b00`.
void FElysiumNpcCop::NPCInit()
{
	CopNPCInit();
}

// Slot 180: `0x10371a90`, which ends in `TroikaUpdateOnRemove`.
void FElysiumNpcCop::UpdateOnRemove()
{
	CopUpdateOnRemove();
}

// Slot 463: `0x10371c20`, the pursuit latch, census and holster/draw arms (`CopOnStateChange`, whose
// tail `CopHumanCombatantOnStateChange` is `CNPC_VHumanCombatant::OnStateChange`'s weapon half), then
// that body's own direct call into the Troika body.
void FElysiumNpcCop::OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState)
{
	CopOnStateChange(LastOnStateChangeOldRetail, LastOnStateChangeNewRetail);
	OnStateChangeTroika(OldState, NewState);
}

// Slot 461: `0x103726c0`, chaining the combatant's `0x10387380` directly.
int32 FElysiumNpcCop::SelectIdealStateRetail()
{
	return CopSelectIdealState();
}

// Slot 472: `0x10371ae0`
void FElysiumNpcCop::OnSeeEntity(FElysiumEntity* Seen)
{
	CopOnSeeEntity(Seen);
}

// Slot 404: `0x10372b70`.
int32 FElysiumNpcCop::IRelationType(FElysiumEntity* Candidate)
{
	return CopIRelationType(Candidate);
}

// Slot 440: `0x10372150`.
int32 FElysiumNpcCop::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return CopTranslateSchedule(ScheduleNumber);
}

// Slot 123: `0x10372f00`
void FElysiumNpcCop::DrawDebugGeometryOverlays()
{
	VCopDrawDebugGeometryOverlays();
}

// Slot 546: `0x10370ad0`, the class's own schedule id space.
const TCHAR* FElysiumNpcCop::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VCop"), SlotEn);
}
