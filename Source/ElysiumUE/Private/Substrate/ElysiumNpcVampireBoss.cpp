#include "Substrate/ElysiumNpcVampireBoss.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcVampireBoss::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VVampireBoss"));
	return Row;
}

// Slot 420: `0x103c5840`.
void FElysiumNpcVampireBoss::NPCInit()
{
	VampireBossNPCInit();
}

// Slot 127: `0x103c5910`.
int32 FElysiumNpcVampireBoss::Restore(void* Archive)
{
	return VampireBossRestore(Archive);
}

// Slot 546: `0x103c5270`, the class's own schedule id space.
const TCHAR* FElysiumNpcVampireBoss::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VVampireBoss"), SlotEn);
}
