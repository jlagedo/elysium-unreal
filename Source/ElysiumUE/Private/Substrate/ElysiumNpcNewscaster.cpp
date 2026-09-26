#include "Substrate/ElysiumNpcNewscaster.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcNewscaster::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VNewscaster"));
	return Row;
}

// Slot 420: `0x103a0420`.
void FElysiumNpcNewscaster::NPCInit()
{
	NewscasterNPCInit();
}

// Slot 104: `0x103a03e0`.
void FElysiumNpcNewscaster::Precache()
{
	NewscasterPrecache();
}

// Slot 180: `0x103a03a0`, which ends in `TroikaUpdateOnRemove`.
void FElysiumNpcNewscaster::UpdateOnRemove()
{
	NewscasterUpdateOnRemove();
}

// Slot 404: `0x103a01b0`, eight bytes, `return 4;`. It never looks at the candidate, never reaches
// the table and never reaches the Troika body, so a newscaster is `D_NU` toward everything, itself
// and null included.
int32 FElysiumNpcNewscaster::IRelationType(FElysiumEntity* Candidate)
{
	(void)Candidate;
	return 4;   // D_NU
}

// Slot 124: `0x103a1250`
int32 FElysiumNpcNewscaster::DrawDebugTextOverlays()
{
	return NewscasterDrawDebugTextOverlays();
}
