#include "Substrate/ElysiumNpcHunter.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcHunter::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VHunter"));
	return Row;
}

// Slot 420: `0x10388b30`.
void FElysiumNpcHunter::NPCInit()
{
	HunterNPCInit();
}

// Slot 463: `0x10388880`, the pursuit pre-step, then a direct call into
// `CNPC_VHumanCombatant::OnStateChange` (`0x103871c0`, thunk `0x10014a10`).
void FElysiumNpcHunter::OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState)
{
	HunterStateChangePreStep(OldState, NewState);
	FElysiumNpcHumanCombatant::OnStateChange(OldState, NewState);
}

// Slot 461: `0x10388ab0`, the selector tag 0x17 and then a direct call into the combatant's `0x10387380`.
int32 FElysiumNpcHunter::SelectIdealStateRetail()
{
	SelectIdealStateSelector = 0x17;
	return HumanCombatPatrolSelectIdealState();
}

// Slot 472: `0x103887d0`
void FElysiumNpcHunter::OnSeeEntity(FElysiumEntity* Seen)
{
	HunterOnSeeEntity(Seen);
}

// Slot 404: `0x10388bb0`.
int32 FElysiumNpcHunter::IRelationType(FElysiumEntity* Candidate)
{
	return HunterIRelationType(Candidate);
}

// Slot 440: `0x10388a40`.
int32 FElysiumNpcHunter::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return HunterTranslateSchedule(ScheduleNumber);
}

// Slot 546: `0x10388200`, the class's own schedule id space.
const TCHAR* FElysiumNpcHunter::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VHunter"), SlotEn);
}
