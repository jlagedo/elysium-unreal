#include "Substrate/ElysiumNpcHumanCombatant.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcHumanCombatant::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VHumanCombatant"));
	return Row;
}

// Slot 420: `0x10387140`.
void FElysiumNpcHumanCombatant::NPCInit()
{
	HumanCombatantNPCInit();
}

// Slot 463: `0x103871c0`, hide on IDLE and unhide on ALERT / COMBAT / 11, then a direct call into
// the Troika body `0x102ae140`. Inherited by the ghoul croucher, the combat patrol, the Sabbat
// gunman, Yukie and `CNPC_ProneDialog`; `CNPC_VCop` and `CNPC_VHunter` call it directly.
void FElysiumNpcHumanCombatant::OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState)
{
	ApplyStateWeaponVisibility(NewState);
	OnStateChangeTroika(OldState, NewState);
}

// Slot 461: `0x10387380`, chaining the human line's `0x103851e0` directly (the port names the body after the combat patrol).
int32 FElysiumNpcHumanCombatant::SelectIdealStateRetail()
{
	return HumanCombatPatrolSelectIdealState();
}

// Slot 453: `0x10387520`, a direct call into the Troika body `0x102ad140` first, then its own bits.
void FElysiumNpcHumanCombatant::BuildScheduleTestBits(FElysiumNpcConditions& InOutMask)
{
	FElysiumNpc::BuildScheduleTestBits(InOutMask);
	HumanCombatantBuildScheduleTestBits(InOutMask);
}

// Slot 546: `0x10386c80`, the class's own schedule id space.
const TCHAR* FElysiumNpcHumanCombatant::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VHumanCombatant"), SlotEn);
}
