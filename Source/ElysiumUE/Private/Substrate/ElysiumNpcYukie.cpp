#include "Substrate/ElysiumNpcYukie.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcYukie::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VYukie"));
	return Row;
}

// The melee quartet: 599 `0x103dd8b0` (no gates), 600 `0x103dd900` (the one-shot flee), 601
// `0x103dd9a0` (no coordinator release) and 602 `0x103dda10` (distance or clock). Story 5 step 3
// wires all four, which were ported but not dispatched (`decisions-step3.json`).
bool FElysiumNpcYukie::Slot599(int32 Arg)
{
	(void)Arg;
	return YukieEnterMelee();
}

bool FElysiumNpcYukie::Slot600(FElysiumEntity* Enemy)
{
	return FUN_103dd900(Enemy);
}

void FElysiumNpcYukie::Slot601(FElysiumEntity* Enemy)
{
	(void)Enemy;
	YukieLeaveMelee();
}

bool FElysiumNpcYukie::Slot602()
{
	return YukieShouldLeaveMelee();
}

// Slot 420: `0x103dd800`.
void FElysiumNpcYukie::NPCInit()
{
	YukieNPCInit();
}

// Slot 461: `0x103dd780`, the selector tag 0x2a and then a direct call into the combatant's `0x10387380`.
int32 FElysiumNpcYukie::SelectIdealStateRetail()
{
	SelectIdealStateSelector = 0x2a;
	return HumanCombatPatrolSelectIdealState();
}

// Slot 201: `0x103ddaf0`
bool FElysiumNpcYukie::FVisible(FElysiumEntity* SeenTarget, int32 Mask, FElysiumEntity* Blocker, int32 Arg4)
{
	// `CNPC_VYukie#201`, story 29c-1's `YukieFVisible`, which chains slot 594 below.
	FElysiumEntityHandle Unused;
	return YukieFVisible(SeenTarget, &Unused);
}

// Slot 404: `0x103dd880`.
int32 FElysiumNpcYukie::IRelationType(FElysiumEntity* Candidate)
{
	return YukieIRelationType(Candidate);
}

// Slot 546: `0x103dd1f0`, the class's own schedule id space.
const TCHAR* FElysiumNpcYukie::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VYukie"), SlotEn);
}
