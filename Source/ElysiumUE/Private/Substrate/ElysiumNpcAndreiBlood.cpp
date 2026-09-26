#include "Substrate/ElysiumNpcAndreiBlood.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcAndreiBlood::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VAndreiBlood"));
	return Row;
}

// Slot 420: `0x1035cec0`.
void FElysiumNpcAndreiBlood::NPCInit()
{
	AndreiBloodNPCInit();
}

// Slot 104: `0x1035cb90`.
void FElysiumNpcAndreiBlood::Precache()
{
	AndreiBloodPrecache();
}

// Slot 127: `0x1035cf80`.
int32 FElysiumNpcAndreiBlood::Restore(void* Archive)
{
	return AndreiBloodRestore(Archive);
}

// Slot 461: `CNPC_VAndreiBlood::vfunc461`, whose typed answer is written back as retail 2 or 1.
int32 FElysiumNpcAndreiBlood::SelectIdealStateRetail()
{
	return AndreiBloodSelectIdealStateRetail();
}

// Slot 438: `0x1035d010`, which replaces the whole selector (the Troika selector's species hook).
int32 FElysiumNpcAndreiBlood::SpeciesSelectSchedule()
{
	return AndreiBloodSelectSchedule();
}

// Slot 566: `0x1035db00`, a replacement that does not chain.
bool FElysiumNpcAndreiBlood::FValidateHintType(void* Hint)
{
	return SpeciesFValidateHintType(TEXT("CNPC_VAndreiBlood"), Hint);
}

// Slot 546: `0x1035c460`, the class's own schedule id space.
const TCHAR* FElysiumNpcAndreiBlood::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VAndreiBlood"), SlotEn);
}
