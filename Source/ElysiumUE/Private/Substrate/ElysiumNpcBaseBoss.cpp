#include "Substrate/ElysiumNpcBaseBoss.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"

const FElysiumNpcClass* FElysiumNpcBaseBoss::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(TEXT("CNPC_VBaseBoss"));
	return Row;
}

// Slot 76: `0x10366290`, which chains `CAI_BaseNPC::DrawDebugStatOverlays` (`0x102775e0`) directly.
void FElysiumNpcBaseBoss::DrawDebugStatOverlays()
{
	BossDrawDebugStatOverlays();
}
