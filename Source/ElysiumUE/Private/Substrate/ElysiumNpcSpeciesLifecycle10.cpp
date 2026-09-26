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

void FElysiumNpc::BaseEntityStartTouch(FElysiumEntity* Other)
{
	// `CBaseEntity::StartTouch` `0x100a49d0`. The entire body past the scope frame is the parent
	// forward: `if (m_pParent resolves live) parent->vtable[+0x2b8](other)`.
	if (!MoveParent.IsSet() || World == nullptr)
	{
		return;
	}
	FElysiumEntity* Parent = World->Resolve(MoveParent);
	if (Parent == nullptr)
	{
		return;
	}
	++ParentTouchPropagations;
	if (FElysiumNpc* ParentNpc = Parent->AsNpc())
	{
		// Retail dispatches slot 174 virtually, so a parent with a species override takes it.
		ParentNpc->StartTouchSpecies(Other);
	}
	// A parent that is not an NPC carries no slot 174 in this runtime; the dispatch is counted above
	// and nothing runs, which is stated rather than approximated.
}

void FElysiumNpc::BaseEntityTouch(FElysiumEntity* Other)
{
	// `CBaseEntity::Touch` `0x100a4af0`. Two steps, and the ORDER is the fact: the touch think
	// function FIRST, the parent forward second.
	//
	// SEAM: `m_pfnTouch` (`+0x1ac`) has no port member — this runtime has no per-entity touch
	// callback pointer — so the call is counted and nothing runs.
	++TouchFunctionCalls;

	if (!MoveParent.IsSet() || World == nullptr)
	{
		return;
	}
	FElysiumEntity* Parent = World->Resolve(MoveParent);
	if (Parent == nullptr)
	{
		return;
	}
	++ParentTouchPropagations;
	if (FElysiumNpc* ParentNpc = Parent->AsNpc())
	{
		ParentNpc->TouchSpecies(Other);   // the parent's slot 175 (`vtable + 700`)
	}
}

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

// -------------------------------------------------------------------------------------------------
// Slot 463 `OnStateChange` — the `CNPC_VGuard1` and `CNPC_VHunter` pre-steps.
// -------------------------------------------------------------------------------------------------

FElysiumEntity* FElysiumNpc::GetEnemyEntity() const
{
	// `CAI_BaseNPC::FUN_101a67e0` `0x101a67e0`, vtable `+0x29c` (slot 167): `m_hEnemy` resolved
	// through the global entity table, null when the handle is stale.
	if (!BaseMemory.Enemy.IsSet() || World == nullptr)
	{
		return nullptr;
	}
	return const_cast<FElysiumEntityWorld*>(World)->Resolve(BaseMemory.Enemy);
}
