#include "Substrate/ElysiumNpcBach.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcBach::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VBach"));
	return Row;
}

// Slot 606: `0x10364280`, the arm-then-fire gate that calls the Troika body `0x102b8320` directly.
int32 FElysiumNpcBach::Slot606(int32 Arg)
{
	return FUN_10364280(Arg);
}

// Slot 609: `0x103661f0`, a state gate that tail-calls the Troika hint search `0x102b6b50`
// directly on admission and otherwise zeroes `m_pShootAtHintNode` and answers NULL.
void* FElysiumNpcBach::Slot609(bool bForce)
{
	if (!FUN_103661f0(bForce))
	{
		return nullptr;
	}
	return FElysiumNpc::Slot609(bForce);
}

// Slot 420: `0x10363940`.
void FElysiumNpcBach::NPCInit()
{
	BachNPCInit();
}

// Slot 104: `0x103637b0`.
void FElysiumNpcBach::Precache()
{
	BachPrecache();
}

// Slot 463: `0x103639b0`. While `m_bCanFightYet` is 0, ALERT / COMBAT snap back through
// `SetState(old)` and never reach the Troika body; otherwise a direct call into it.
void FElysiumNpcBach::OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState)
{
	if (BachOnStateChange(LastOnStateChangeOldRetail, LastOnStateChangeNewRetail))
	{
		return;
	}
	OnStateChangeTroika(OldState, NewState);
}

// Slot 461: `0x10363b40`, the selector tag 0xe and then a direct call into the human line's `0x103851e0`.
int32 FElysiumNpcBach::SelectIdealStateRetail()
{
	SelectIdealStateSelector = 0xe;
	return HumanSelectIdealState();
}

// Slot 604: `0x10364080`, which replaces the Troika body wholesale; its argument is read by no arm.
int32 FElysiumNpcBach::SelectScheduleMeleeCombat(int32 Unused)
{
	(void)Unused;
	return SelectScheduleMeleeCombatBach();
}

// Slot 605: `0x103642f0`
int32 FElysiumNpcBach::SelectScheduleRangedCombat(int32 Arg)
{
	return BachSelectScheduleRangedCombat(Arg);
}

// Slot 440: `0x10363a30`.
int32 FElysiumNpcBach::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return BachTranslateSchedule(ScheduleNumber);
}

// Slot 566: `0x10365800`, whose miss falls through into the Troika body `0x10295c20` directly.
bool FElysiumNpcBach::FValidateHintType(void* Hint)
{
	return SpeciesFValidateHintType(TEXT("CNPC_VBach"), Hint);
}

// Slot 546: `0x10362df0`, the class's own schedule id space.
const TCHAR* FElysiumNpcBach::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VBach"), SlotEn);
}

// Slot 561: `0x10363db0` — the shield, teleport and weapon-switch block, then
// `CAI_BaseNPC::GatherAttackConditions` (`0x1026dd10`) directly. The distance argument IS read by the
// Bach block, unlike the base, which takes the port's own committed enemy.
void FElysiumNpcBach::GatherAttackConditions(FElysiumEntity* Enemy, float DistanceUnits)
{
	BachGatherAttackConditions(DistanceUnits);
	FElysiumNpc::GatherAttackConditions(Enemy, DistanceUnits);
}
