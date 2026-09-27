// `CAI_BaseNPC`'s bodies of the `SpeciesMisc10` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseSpeciesMisc10.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumKeyValues.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Misc/FileHelper.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcSpeciesMisc10Shared.h"

// --- Moved from `ElysiumNpcSpeciesMisc10.cpp` (story 5 step 5) ---

void FElysiumNpcBase::LeaveGrappleState()
{
	// `1026ce30`: the `m_OnGrappleEnd` fire. The activator is the entity `+0x1538` resolves to, or
	// NULL when `+0x153c` is -1 or the handle fails its `0x1fff` index / `>>13` serial check.
	// UNCONDITIONAL — there is no grapple-type gate anywhere in this body.
	FireOutput(TEXT("OnGrappleEnd"), Grapple.Partner);
	// `1026ce78`: `CBaseCombatCharacter::LeaveGrappleState`.
	FElysiumCombatCharacter::LeaveGrappleState();
	// `1026ce82`: slot 416 (`vt+0x680`) `SetForceFrequentThink(false)`.
	SetForceFrequentThink(false);
	// `1026ce8d` -> `0x10007ea0`: `--m_iIsOblivious (+0x5bb4)`, clamped at 0, then the squad
	// reconnect `0x10009601`. Both UNCONDITIONAL, which is the arm the port was missing.
	RemoveGrappleOblivious();
	ReconnectToSquad();
}

FElysiumEntity* FElysiumNpcBase::PlayerInventorySlot0() const
{
	// SEAM for `0x1015d680(player, 0)` — the `+0x2308` handle array's slot 0. This runtime's
	// equivalent standing entity is the character's active weapon; a character with none answers
	// null, which is retail's own refusal for an empty slot.
	if (World == nullptr)
	{
		return nullptr;
	}
	FElysiumPlayer* Player = World->FindPlayer();
	return Player != nullptr ? World->Resolve(Player->Inventory.ActiveWeapon) : nullptr;
}
