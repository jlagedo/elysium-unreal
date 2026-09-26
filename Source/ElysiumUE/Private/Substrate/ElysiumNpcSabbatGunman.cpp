#include "Substrate/ElysiumNpcSabbatGunman.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcSabbatGunman::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VSabbatGunman"));
	return Row;
}

// Slot 465: `0x103a56f0`, ending in a direct call into `CAI_BaseNPCTroika::OnChangeActivity` (`0x10295a60`).
void FElysiumNpcSabbatGunman::OnChangeActivity(int32 Activity)
{
	SabbatGunmanOnChangeActivity(Activity);
}

// Slot 546: `0x103a5240`, the class's own schedule id space.
const TCHAR* FElysiumNpcSabbatGunman::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VSabbatGunman"), SlotEn);
}
