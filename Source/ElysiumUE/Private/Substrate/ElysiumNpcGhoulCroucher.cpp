#include "Substrate/ElysiumNpcGhoulCroucher.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcGhoulCroucher::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VGhoulCroucher"));
	return Row;
}

// Slot 420: `0x1037b290`.
void FElysiumNpcGhoulCroucher::NPCInit()
{
	GhoulCroucherNPCInit();
}

// Slot 104: `0x1037b1a0`.
void FElysiumNpcGhoulCroucher::Precache()
{
	GhoulCroucherPrecache();
}

// Slot 105: `0x1037b1f0`, the vocalization-group body shared with the zombie; it calls the Troika
// body `0x10298ce0` directly.
void FElysiumNpcGhoulCroucher::SetModel(TCHAR* ModelName)
{
	ZombieLineSetModel(ModelName, TEXT("0x1037b1f0"));
}

// Slot 24: `0x1037be80`, the burn, then the Troika body `0x1029f8d0` directly.
void FElysiumNpcGhoulCroucher::OnVictimHitByMe(FElysiumEntity* Victim)
{
	GhoulCroucherOnVictimHitByMe(Victim);
}

// Slot 546: `0x1037a950`, the class's own schedule id space.
const TCHAR* FElysiumNpcGhoulCroucher::SquadSlotName(int32 SlotEn)
{
	return SpeciesSquadSlotName(TEXT("CNPC_VGhoulCroucher"), SlotEn);
}

// Slot 615: `0x1037c420`, whose miss calls the Troika body `0x102ad0c0` directly.
bool FElysiumNpcGhoulCroucher::CanBeSetOnFire()
{
	return GhoulCroucherCanBeSetOnFire();
}

// Slot 174: `0x1037bf60`.
void FElysiumNpcGhoulCroucher::StartTouchSpecies(FElysiumEntity* Other)
{
	GhoulCroucherStartTouch(Other);
}
