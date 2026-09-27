#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcSpeciesLifecycle10Shared.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 29d, family **SpeciesLifecycle10** — `CPayphone`'s slot 431, the slot 174/175 base bodies
// and their two species arms, the `CNPC_VGuard1` / `CNPC_VHunter` slot-463 pre-steps, the two
// `CNPC_VVampireBoss`-line `Restore` arms and the two destructors. The three maker `Spawn` bodies
// land on `FElysiumNpcMaker` and are in `Substrate/ElysiumNpcMaker.cpp`, beside that class's other
// slot bodies.
//
// `ElysiumNpcSpeciesLifecycle10.inl` carries the family's reading notes, the corrections it
// made to the checklist's walks and the four `.rdata` cells it read out of the pinned image. The
// walked prose is `docs/vtmb/npc-ai/lifecycle.md`.
int32& FElysiumNpc::WerewolfShowDebug()
{
	return NpcKernelSpeciesLifecycle10Shared::GWerewolfShowDebug;
}

// -------------------------------------------------------------------------------------------------
// Slots 174 `StartTouch` and 175 `Touch`.
// -------------------------------------------------------------------------------------------------

void FElysiumNpc::StartTouchSpecies(FElysiumEntity* Other)
{
	// Slot 174. `CNPC_VGhoulCroucher` (`0x1037bf60`), the census's only override of the slot on the
	// NPC line, overrides this method on its C++ class (story 5 step 3).
	BaseEntityStartTouch(Other);
}

void FElysiumNpc::TouchSpecies(FElysiumEntity* Other)
{
	// Slot 175. `CNPC_VGargoyle` (`0x1037a270`), the census's only override on the NPC line,
	// overrides this method on its C++ class (story 5 step 3).
	BaseEntityTouch(Other);
}

void FElysiumNpc::OnTouchStart(const FElysiumEntityHandle& Activator)
{
	// This runtime's touch-begin notification IS retail's slot 174, so the whole slot-174 chain hangs
	// off it. `FElysiumEntity::OnTouchStart` is empty, so there is no base behaviour to keep.
	FElysiumEntity* Other = World != nullptr ? World->Resolve(Activator) : nullptr;
	StartTouchSpecies(Other);
}

