#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcPositionsShared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumPlaceSet.h"

// Story 29c-1, family **Positions** — the node selectors, the teleport clearance rules and the
// Chang brothers' arena. The rest of the family (the trace bodies, slot 563, the Werewolf teleport
// pair) is `Substrate/ElysiumNpcPositions2.cpp`; the declarations and the family's standing
// facts are `Substrate/ElysiumNpcPositions.inl`; the walked prose is
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
	// `for (node = DAT_10925450; node; node = node->next (+0x5d8))` — the global `CAI_Hint` list,
	// family Motor's `NavAllHintNodes` (the world's live list, head first), each entry's words
	// through family Hints' `HintWords`. The position every selector here scores is the HINT's own
	// `GetAbsOrigin()` (vtable `+0x364` -- `CNPC_VSheriffMan::SelectTeleportNode 0x103b0630`,
	// `CNPC_VSabbatLeader::SelectTeleportArchway 0x103a9540`), not its network node, so
	// `FHintWords::OriginCm` -- the hint entity's origin -- is the one gathered.
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
	// `thunk_FUN_102f41b0(m_pNavigator->GetNetwork() (+0x2c), &pos)` -- the network's nearest node to
	// a POINT (no NPC, no hull):
	//
	//     if (*network == 0) return -1;
	//     cached = 0x102f4520(network, pos); if (cached != -2) return cached;     // not ported
	//     list = ListNodesInBox(10, pos - 2048, pos + 2048, CNodePosFilter(pos));  // 0x102f32f0
	//     for (node : list, nearest first)
	//         if (0x102f39a0(NULL, pos, node->origin (+0x08..+0x10), &flag))      // the line trace
	//             { 0x102f45f0(network, pos, node, 0x17); return node; }
	//     0x102f45f0(network, pos, -1, 0x17); return -1;
	//
	// `CNodePosFilter` (`0x102f44b0` / `0x102f44d0`) admits every node and scores the squared
	// distance from the point to the RAW origin; `_DAT_1046bacc` is 2048.0. The trace ignores no
	// entity and its flag is not read. The 20-entry cache `0x102f4520` / `0x102f45f0` is engine
	// machinery in front of the search and is not ported: the search runs every call.
	const FElysiumPlaceSet* Places = World != nullptr ? &World->Places() : nullptr;
	if (Places == nullptr || Places->NumNodes() == 0)                          // 0x102f41b9 *this != 0
	{
		return -1;
	}
	constexpr float BoxUnits = ElysiumNpcTunables::TwoThousandFortyEight;      // _DAT_1046bacc
	constexpr int32 ListCount = 10;                                            // 0x102f4214 PUSH 10
	const FVector P = PositionCm / ElysiumMove::U;
	const FVector Half(BoxUnits, BoxUnits, BoxUnits);
	const TArray<int32> Order = Places->ListNodesInBox(ListCount, P - Half, P + Half,
		[](int32) { return true; },                                            // CNodePosFilter::vfunc0
		[Places, &P](int32 Node)                                               // CNodePosFilter::vfunc1
		{
			return static_cast<float>(FVector::DistSquared(Places->Row(Node).OriginCm / ElysiumMove::U, P));
		});
	for (const int32 Node : Order)
	{
		if (NavNearestNodeTrace(PositionCm, Places->Row(Node).OriginCm, FElysiumEntityHandle::Invalid())
			!= ENavNodeTrace::Blocked)                                         // 0x102f428c 0x102f39a0(0, ...)
		{
			return Node;
		}
	}
	return -1;
}
