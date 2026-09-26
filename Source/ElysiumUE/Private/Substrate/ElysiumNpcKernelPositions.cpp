#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelPositionsShared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

// Story 29c-1, family **Positions** — the node selectors, the teleport clearance rules and the
// Chang brothers' arena. The rest of the family (the trace bodies, slot 563, the Werewolf teleport
// pair) is `Substrate/ElysiumNpcKernelPositions2.cpp`; the declarations and the family's standing
// facts are `Substrate/ElysiumNpcKernelPositions.inl`; the walked prose is
// `docs/vtmb/npc-ai/shape.md`.
//
// Every threshold below was read out of the pinned retail `vampire.dll`'s `.rdata` at its cited
// address (image base `0x10000000`; `.rdata`'s raw offset equals its RVA in this image, so the
// address IS the file offset), so the numbers are recovered facts and not estimates. Where an
// address is quoted with no number beside it, the datum lives past `.data`'s raw size and is filled
// at runtime — those are named as seams, never guessed.

namespace
{

	// `_DAT_104ce8c0`, `DistToSegment`'s degenerate-length floor (the float 1e-5), is not here: the
	// body collapsed onto family Hints' `FElysiumNpcChangBros::DistToSegment`, which owns it.

}

// --- The candidate list -------------------------------------------------------------------------

void FElysiumNpc::GatherHintNodes(TArray<FHintWords>& OutNodes, TArray<int32>& OutNodeIds) const
{
	// `for (node = DAT_10925450; node; node = node->next (+0x5d8))` — the global `CAI_Hint` list.
	// **SEAM**, twice over: family Motor's `NavAllHintNodes` is the list (empty) and family Hints'
	// `HintWords` is one node's words (never valid). Nine selectors walk this and every one of them
	// therefore answers "no node", which is retail's own answer on a map that authors none.
	OutNodes.Reset();
	OutNodeIds.Reset();
	TArray<int32> NodeIds;
	if (!NavAllHintNodes(NodeIds))
	{
		return;
	}
	for (const int32 NodeId : NodeIds)
	{
		FHintWords Words;
		if (HintWords(NodeId, Words) && Words.bValid)
		{
			OutNodes.Add(Words);
			OutNodeIds.Add(NodeId);
		}
	}
}

// --- `PositionClearForTeleport`, four species ---------------------------------------------------
//
// Retail stands NO base `PositionClearForTeleport` and no vtable slot for it: the name exists on
// exactly these four classes as non-virtual methods, each called only by its own class's selector.
// So there is no dispatcher (story 5 step 3 retired the port's test-only one).

bool FElysiumNpc::SquadPositionTaken(const FVector& PositionCm, float ClearanceCm) const
{
	// `thunk_FUN_10315a80(m_pSquad, pos, clearance)`. **SEAM**: this substrate has no squad object,
	// so `ConnectedSquad()` answers null and the caller never gets here. Answering false is the arm
	// that lets the candidate through, which is what an empty squad answers in retail.
	(void)PositionCm;
	(void)ClearanceCm;
	return false;
}

void FElysiumNpc::SquadMembers(TArray<FElysiumNpc*>& OutMembers) const
{
	// `thunk_FUN_103160a0(squad)` (the count) and `thunk_FUN_103160c0(squad, i)` (the Nth member).
	// **SEAM**: no squad object, so the list is empty.
	OutMembers.Reset();
}

// --- What is left of `FUN_102c5570` -------------------------------------------------------------

bool FElysiumNpc::EnemyLastKnownPosition(FVector& OutPositionCm) const
{
	// `GetEnemies()` (slot 541) then `thunk_FUN_102dfed0(memory, &out, pEnemy)`. This runtime's
	// `FElysiumNpcEnemyMemory` is the same store, so the fact is carried; the record's position is
	// what retail's helper copies out.
	const FElysiumEntity* Enemy =
		World != nullptr ? World->Resolve(Senses.Memory.Enemy) : nullptr;
	if (Enemy == nullptr)
	{
		return false;
	}
	OutPositionCm = Enemy->Origin;
	return true;
}

bool FElysiumNpc::ShootTargetDelta(FVector& OutDeltaCm) const
{
	// The middle of `FUN_102c5570`, which is what the listing settles:
	//
	//     if (m_hShootTargetOverride (+0x5ba8) resolves)
	//         delta = override->GetAbsOrigin() - GetAbsOrigin();
	//     else if (GetEnemy() (slot 167) != NULL)
	//         delta = GetEnemies()->GetLastKnownPosition(GetEnemy()) - GetAbsOrigin();
	//     else
	//         <no target: the caller skips the divide entirely>
	//
	// The override wins outright — the enemy is not even asked for — which is why a schedule that
	// pins a shoot target keeps aiming at it after the enemy moves.
	const FElysiumEntity* Override =
		World != nullptr ? World->Resolve(ShootTargetOverride) : nullptr;
	if (Override != nullptr)
	{
		OutDeltaCm = Override->Origin - Origin;
		return true;
	}
	FVector LastKnown;
	if (!EnemyLastKnownPosition(LastKnown))
	{
		return false;
	}
	OutDeltaCm = LastKnown - Origin;
	return true;
}

float FElysiumNpc::ShootTargetFalloff(float RangeBase, float RangeDivisor, float Value) const
{
	// `FUN_102c5570`'s whole numeric shape, read from the listing (`0x102c5570`..`0x102c56bf`):
	//
	//     float scale = 1.0f;                                 // _DAT_104454c0
	//     if (RangeDivisor > 0.0f) {                          // _DAT_104454c4
	//         Vector d;
	//         if (ShootTargetDelta(d)) {
	//             float dist = d.Length();
	//             scale = (dist > 0.0f) ? sqrt(dist / RangeDivisor) : dist;
	//         } else {
	//             scale = sqrt(1.0f / RangeDivisor);
	//         }
	//     }
	//     return (Value - RangeBase) * scale;
	//
	// `RangeBase` is the pointer argument's `+0x260` and `RangeDivisor` its `+0x26c`.
	//
	// **UNRECOVERED, and stated rather than papered over**: the class of that pointer. Those two
	// offsets are `m_pMoveChild` (an `EHANDLE`) and `m_iName` (a `string_t`) on a `CBaseEntity`, so
	// it is NOT an entity, and the function has ZERO callers anywhere in the image — nothing states
	// its type. 29c's overlay names this row `FElysiumNpc::IsNearShootTarget`; that name is 29c's
	// own inference and the body is not a predicate at all, so it is ported under a name that
	// describes what it computes. See `docs/vtmb/npc-ai/shape.md`.
	float Scale = NpcKernelPositionsShared::RetailOne;
	if (RangeDivisor > NpcKernelPositionsShared::RetailZero)
	{
		FVector Delta;
		if (ShootTargetDelta(Delta))
		{
			const float Distance = static_cast<float>(Delta.Size());
			Scale = Distance > NpcKernelPositionsShared::RetailZero ? FMath::Sqrt(Distance / RangeDivisor) : Distance;
		}
		else
		{
			Scale = FMath::Sqrt(NpcKernelPositionsShared::RetailOne / RangeDivisor);
		}
	}
	return (Value - RangeBase) * Scale;
}

int32 FElysiumNpc::NavNearestNodeTo(const FVector& PositionCm) const
{
	// `thunk_FUN_102f41b0(m_pNavigator->GetNetwork() (+0x2c), &pos)`. **SEAM**: family Motor's
	// standing fact — there is no `CAI_Node` array here — so this answers retail's own miss value.
	(void)PositionCm;
	return -1;
}
