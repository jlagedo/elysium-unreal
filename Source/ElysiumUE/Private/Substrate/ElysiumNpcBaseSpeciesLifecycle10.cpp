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
	if (World && World->Resolve(ParentHandle)) ++ParentTouchPropagations;
	FElysiumEntity::StartTouch(Other);
}

void FElysiumNpcBase::BaseEntityTouch(FElysiumEntity* Other)
{
	// `CBaseEntity::Touch` `0x100a4af0`. Two steps, and the ORDER is the fact: the touch think
	// function FIRST, the parent forward second.
	//
	// Keep the existing higher-layer diagnostic count; the generic base now owns the actual
	// installed +1ec callback and any-class parent relay instead of a diagnostic substitute.
	++TouchFunctionCalls;
	if (World && World->Resolve(ParentHandle)) ++ParentTouchPropagations;
	FElysiumEntity::Touch(Other);
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
