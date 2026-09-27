// `CAI_BaseNPC`'s bodies of the `SpeciesLifecycle10` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseSpeciesLifecycle10.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSpeciesLifecycle10Shared.h"

// --- Moved from `ElysiumNpcSpeciesLifecycle10.cpp` (story 5 step 5) ---

void FElysiumNpcBase::BaseEntityStartTouch(FElysiumEntity* Other)
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

void FElysiumNpcBase::BaseEntityTouch(FElysiumEntity* Other)
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

// -------------------------------------------------------------------------------------------------
// Slot 463 `OnStateChange` — the `CNPC_VGuard1` and `CNPC_VHunter` pre-steps.
// -------------------------------------------------------------------------------------------------

FElysiumEntity* FElysiumNpcBase::GetEnemyEntity() const
{
	// `CAI_BaseNPC::FUN_101a67e0` `0x101a67e0`, vtable `+0x29c` (slot 167): `m_hEnemy` resolved
	// through the global entity table, null when the handle is stale.
	if (!BaseMemory.Enemy.IsSet() || World == nullptr)
	{
		return nullptr;
	}
	return const_cast<FElysiumEntityWorld*>(World)->Resolve(BaseMemory.Enemy);
}
