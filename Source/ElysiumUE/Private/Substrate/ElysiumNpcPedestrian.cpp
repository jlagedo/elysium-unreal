#include "Substrate/ElysiumNpcPedestrian.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcPedestrian::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VPedestrian"));
	return Row;
}

// Slot 420: `0x103a2570`.
void FElysiumNpcPedestrian::NPCInit()
{
	PedestrianNPCInit();
}

// Slot 130: `0x103a25a0`, which calls the Troika body first.
void FElysiumNpcPedestrian::OnRestore(bool bFromLoad)
{
	PedestrianOnRestore(bFromLoad);
}

// Slot 461: `0x103a2e30`, chaining the human line's `0x103851e0` directly.
int32 FElysiumNpcPedestrian::SelectIdealStateRetail()
{
	return PedestrianSelectIdealState();
}

// Slot 453: `0x103a2980`, a direct call into the Troika body `0x102ad140` first, then its own bits.
void FElysiumNpcPedestrian::BuildScheduleTestBits(FElysiumNpcConditions& InOutMask)
{
	FElysiumNpc::BuildScheduleTestBits(InOutMask);
	PedestrianBuildScheduleTestBits(InOutMask);
}

// Slot 404: `0x103a2930`.
int32 FElysiumNpcPedestrian::IRelationType(FElysiumEntity* Candidate)
{
	return PedestrianIRelationType(Candidate);
}

// Slot 546: `0x103a1fa0`, the class's own schedule id space.
const TCHAR* FElysiumNpcPedestrian::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VPedestrian"), SlotEn);
}
