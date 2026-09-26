#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelSpeciesMisc10Shared.h"

#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumKeyValues.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Misc/FileHelper.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"

// Story 29d, family **SpeciesMisc10** — the species words, the Newscaster, the Chang brothers, the
// ghoul croucher, the guard and the ManBat. The second half is in
// `ElysiumNpcKernelSpeciesMisc10_2.cpp`; the declarations and this family's five standing facts are
// in `ElysiumNpcKernelSpeciesMisc10.inl`.
//
// Every body below is retail's, arm by arm and in retail's order, with the `0x10……` address of the
// arm in the comment beside it. Each place this family's reading corrected the checklist's one-line
// walk is marked **CORRECTION** at the arm it changes.

namespace
{
	// --- `.rdata` cells, every one read out of the pinned `vampire.dll` at file offset
	// `address - 0x10000000` (`.rdata` is identity-mapped). The mapping is validated by
	// `_DAT_104ce8c0` reading `1e-05` and `_DAT_104454c4` reading `0.0`, both of which the port
	// already records from story 29c-1.

}

// =================================================================================================
// `CAI_BaseNPC::LeaveGrappleState` — `0x1026ce30`, 100 bytes.
// =================================================================================================

void FElysiumNpc::BaseLeaveGrappleState()
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
	NpcFlags.RemoveGrappleOblivious();
	ReconnectToSquad();
}

// =================================================================================================
// `CNPC_VManBat` — `0x1038e9c0` and `0x1038f020`, and the four seams they share.
// =================================================================================================

void FElysiumNpc::BeginSlowEntity(const FElysiumEntityHandle& Victim, float Magnitude)
{
	// SEAM for `CBaseCombatCharacter::BeginSlowEntity`. Recorded, not applied.
	SlowEntityCalls.Add(FSlowEntityCall{ Victim, Magnitude, /*bBegin=*/true });
}

void FElysiumNpc::EndSlowEntity(const FElysiumEntityHandle& Victim, float Magnitude)
{
	// SEAM for `CBaseCombatCharacter::EndSlowEntity`.
	SlowEntityCalls.Add(FSlowEntityCall{ Victim, Magnitude, /*bBegin=*/false });
}

FElysiumEntity* FElysiumNpc::PlayerInventorySlot0() const
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

// =================================================================================================
// `CNPC_VNewscaster` — `0x103a0670` and `0x103a0ab0`.
// =================================================================================================

bool FElysiumNpc::NewscasterPlayerPresent() const
{
	// SEAM for `0x101cd9e0(1)` — `UTIL_PlayerByIndex(1)`: the index must be in `1..maxclients`, the
	// edict must not be free (`+0x4c`) and its `+0x40` unknown must resolve a base entity. This
	// runtime's one player is that entity.
	return World != nullptr && World->FindPlayer() != nullptr;
}

bool FElysiumNpc::EvalNewscasterDependency(const FString& Source) const
{
	// `0x1000134d` -> `thunk_FUN_101d2850`: `PyRun_String(src, Py_eval_input, __main__, __main__)`,
	// whose int value is compared against zero. `EvalCondition` is this runtime's same interpreter
	// and its error-to-false collapse is retail's own `PyErr_Print` + `return 0`.
	if (World == nullptr)
	{
		return false;
	}
	return const_cast<FElysiumEntityWorld*>(World)->EvalCondition(Source, Handle,
		World->PlayerHandle()).ToBool();
}
