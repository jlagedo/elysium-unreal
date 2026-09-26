#include "Substrate/ElysiumNpcHuman.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcHuman::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VHuman"));
	return Row;
}

// Slot 482: `0x103850a0` (the ledger indexes it under `CNPC_VAndreiBlood`; every human-line class
// holds it), a standalone copy with the SCRIPT-state tail: family Bosses' `CanPlaySequenceSpecies`.
int32 FElysiumNpcHuman::CanPlaySequence(bool bDisregardState, int32 InterruptLevel)
{
	return CanPlaySequenceSpecies(bDisregardState, InterruptLevel);
}

// The melee quartet on the human line: 599 `0x10385ab0` and 600 `0x10385c30` are byte-identical
// copies of the Troika bodies and 602 `0x10385d70` drops only the coordinator null test (a named
// divergence family TroikaHelpers applies to both lines), so the port runs the one Troika body for
// each; 601 `0x10385cf0` is family Bosses' own body.
bool FElysiumNpcHuman::Slot599(int32 Arg)
{
	return FElysiumNpc::Slot599(Arg);
}

bool FElysiumNpcHuman::Slot600(FElysiumEntity* Enemy)
{
	return FElysiumNpc::Slot600(Enemy);
}

void FElysiumNpcHuman::Slot601(FElysiumEntity* Enemy)
{
	(void)Enemy;
	FUN_10385cf0();
}

bool FElysiumNpcHuman::Slot602()
{
	return FElysiumNpc::Slot602();
}

// Slot 375: `0x103854f0`, which calls the Troika body `0x10295590` directly.
int32 FElysiumNpcHuman::NPC_EarlyTranslateActivity(int32 Activity)
{
	return HumanNpcEarlyTranslateActivity(Activity);
}

// Slot 461: `0x103851e0`, chaining the Troika body directly.
int32 FElysiumNpcHuman::SelectIdealStateRetail()
{
	return HumanSelectIdealState();
}

// Slot 604: `0x10385e40`, which replaces the Troika body wholesale; its argument is read by no arm.
int32 FElysiumNpcHuman::SelectScheduleMeleeCombat(int32 Unused)
{
	(void)Unused;
	return SelectScheduleMeleeCombatHuman();
}

// Slot 605: `0x10386560`
int32 FElysiumNpcHuman::SelectScheduleRangedCombat(int32 Arg)
{
	return HumanSelectScheduleRangedCombat(Arg);
}

// Slot 546: `0x10384200`, the class's own schedule id space.
const TCHAR* FElysiumNpcHuman::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VHuman"), SlotEn);
}

// Slot 563: `0x10384760`, the `OffsetOnly` shape; a replacement that does not chain.
void FElysiumNpcHuman::TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm,
	void* Tolerance, void* SecondTolerance)
{
	TranslateEnemyChasePositionShaped(EChaseTranslateShape::OffsetOnly, Enemy, ChasePositionCm, Tolerance,
		SecondTolerance);
}

// Slot 366: `0x10385a70` (every human-line class holds it; the ledger indexes it under
// `CNPC_VAndreiBlood`) — the whole body is `return 0;`.
bool FElysiumNpcHuman::HandleInteraction(int32 Interaction, void* Data, FElysiumEntity* Other)
{
	return HumanHandleInteraction(Interaction, Data, Other);
}
