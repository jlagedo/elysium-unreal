#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumMapPlaces.h"

// Retail's AI network (`DAT_1093407c`: `+0 count`, `+4 CAI_Node**`) as this runtime stands it --
// 0018 story 4's place set. One per entity world (`FElysiumEntityWorld::Places`).
//
// The rows are the map's baked nodes (`UElysiumMapPlaces`), copied in at map activation and never
// changed after. Beside them the place set owns the node's two run-time words and the global
// `CNodeEnt::Spawn` (`0x102d78d0`) numbers node rows with:
//
//   node `+0x9c`   the node cooldown (float, 0 from the ctor `0x102fc5d0`). Story 9 writes it;
//                  this holds the storage and the read.
//   node `+0xa0`   the `CAI_Hint*` attached to the node (null from the ctor). Held as the hint's
//                  entity handle.
//   `DAT_10926a3c` the node-row spawn counter, zeroed by `0x102f6690` at `CWorld::Precache` before
//                  any BSP entity spawns (`BeginMapSpawn`).
//
// The other global `CNodeEnt::Spawn` touches, `DAT_106c994c` (a node id outside the network), is
// not the place set's: it is ONE DLL-wide word every node reader bumps and nothing ever zeroes
// (`0x102f6690` zeroes only `DAT_10926a3c`), so it lives outside any world -- `NodeMissCounter`.
//
// Nothing here is saved. Retail has no datamap for `CAI_Node` or the network (the `ai_network`
// entity's `CAI_NetworkManager` map is two function-table rows), so a restored map starts every node
// at its ctor words and the hints relink themselves (`CAI_Hint::OnRestore` `0x102d3ec0`).
//
// Retail's other `CNodeEnt::Spawn` arm -- no network loaded, so each node row ADDS a node
// (`0x102f47f0`) for the rebuild -- is never taken here: every shipped map carries its AIN and the
// bake is its only producer. A world with no adopted asset (a headless test world, or a map the
// travel gate would have refused) is a loaded network of zero nodes, whose hints all count out.

namespace ElysiumAiNetwork
{
	// `DAT_106c994c`: the count of node ids met outside the network -- by `CNodeEnt::Spawn`'s loaded
	// arm, the hint node lookup `0x102d3e60`, the patrol readers (`0x102aa640`, `0x102aa860`,
	// `0x102a3a4e`, `PatrolNodeInterestRecord`, `0x10307ac0`) and `0x1027db30`. A process static, as retail's is a
	// DLL global: never reset by a map load or a new network. Nothing reads it for behaviour.
	int32& NodeMissCounter();
}

class FElysiumPlaceSet
{
public:
	// --- Adoption ------------------------------------------------------------------------------

	// Copy the map's baked asset in. Replaces any rows held before; does not touch the run-time
	// words (`BeginMapSpawn` does).
	void Adopt(const UElysiumMapPlaces& Asset);
	// The same from bare rows (tests, and `Adopt`'s own body). A row's network index is its position.
	// `InCrosswalkMotions` is parallel to `InCrosswalkPairs` (each pair's hull-0 motion word); a pair
	// past its end takes 0, a word no walking hull can use.
	void AdoptRows(TArray<FElysiumPlaceRow> InRows, int32 InUsedHullBits = 0,
		TArray<FElysiumPlaceWanderCap> InWanderCaps = {}, TArray<FIntPoint> InCrosswalkPairs = {},
		TArray<int32> InCrosswalkMotions = {});
	// True once a baked asset (or a test's rows) has been adopted.
	bool IsAdopted() const { return bAdopted; }
	const FString& MapName() const { return AdoptedMapName; }

	// `0x102f6690` at `CWorld::Precache`: a fresh network for this map load. The counter back to 0,
	// every node's run-time words to their ctor values and every crosswalk pair green
	// (`NodeMissCounter` is left alone).
	void BeginMapSpawn();

	// --- The nodes ------------------------------------------------------------------------------

	// `*DAT_1093407c`.
	int32 NumNodes() const { return Rows.Num(); }
	// `0 <= Node < NumNodes`, the bounds test every retail reader of the node array makes first.
	bool IsValidNode(int32 Node) const { return Rows.IsValidIndex(Node); }
	// The node's baked words. `IsValidNode(Node)` must hold.
	const FElysiumPlaceRow& Row(int32 Node) const { return Rows[Node]; }

	// `CAI_Node::GetPosition` (`0x102fb0d0`, this = node, out, hull), every arm, in centimetres:
	//   type 2 (ground): the origin with `zoffset[hull]` (`+0x14 + 4*hull`) added to Z;
	//   type 4 (climb):  the origin moved along the node yaw by `width(hull) * 0.5 + 8.0` units
	//                    (`0x102d6180`, `_DAT_10449270`, `_DAT_1049a148`), the arm chosen by the
	//                    info bits `4` / `8` / `0x10`;
	//   any other type:  the raw origin.
	// False, `Out` untouched, for a node or hull out of range (retail indexes blind; no caller asks).
	bool GetPositionCm(int32 Node, int32 Hull, FVector& OutCm) const;

	// `0x102f46d0(network, &out, npc, id)`, the network half of `CAI_Hint::GetPosition`
	// (`0x102d1180`): `vec3_origin` (`DAT_1070d1b0`) for a network with no node array (`+4 == 0`) or
	// an id outside `-1 < id <= count`, else `GetPosition(node, npc+0x156c)`. Retail's bound is `<=`,
	// so an id EQUAL to the count reads the slot past the last node of `m_pAInode` (a `new[MAX_NODES]`
	// whose unwritten tail is garbage); that one arm answers `vec3_origin` here (named divergence: a
	// read past the array is not a behaviour to reproduce). A hull outside the table answers the
	// origin too (retail indexes the offsets blind; every caller passes a table hull).
	FVector NetworkNodePositionCm(int32 NodeId, int32 Hull) const;

	// `0x102f47b0(network, id)`, the network half of the hint yaw `0x102d12e0`: the node's `+0x6c`
	// yaw, SOURCE degrees, or `_DAT_104454c4` (0.0f) for no node array or an id outside
	// `-1 < id <= count` -- the same `<=` as above, the same named divergence at `id == count`.
	float NetworkNodeYawSource(int32 NodeId) const;

	// `CAI_Network::ListNodesInBox` (`0x102f32f0`, VPROF "CAI_Network_ListNodesInBox"), verbatim in
	// its selection AND its bug. Every node in index order: `IsValid(node)` (the filter's slot 0)
	// first, then the raw origin (`+0x08..+0x10`) inside `[Mins, Maxs]` on all three axes, both ends
	// inclusive; then its `DistanceSqr(node)` (the filter's slot 1) enters a `CUtlPriorityQueue`
	// capped at `MaxCount`. Both queues order with `0x102f3770` -- `Less(a, b) = b.dist < a.dist` --
	// so the HEAD is the NEAREST, and once the queue is full a candidate enters only when it is
	// strictly nearer than that head, which it then EVICTS. (The SDK's twin gives the result queue
	// `RevIsLowerPriority`, keeping the farthest at the head; VtMB's image passes `0x102f3770` to both.)
	// So the list always carries the nearest in-box node, beside the first-admitted others. The
	// answer is the order retail's consumers pop the second queue in (`0x102f9000` + the inline
	// sift-down), i.e. ascending distance, ties in heap order. `Mins` / `Maxs` / the distance are in
	// whatever frame the caller measures in (the box is a symmetric test, so the port's Y reflection
	// does not change it); this takes them in SOURCE units against `OriginCm / U`.
	TArray<int32> ListNodesInBox(int32 MaxCount, const FVector& MinsUnits, const FVector& MaxsUnits,
		TFunctionRef<bool(int32)> IsValid, TFunctionRef<float(int32)> DistanceSqr) const;

	// Node `+0x9c`. 0 for a node out of range; a write to one is dropped.
	float NodeCooldown(int32 Node) const;
	void SetNodeCooldown(int32 Node, float Value);

	// Node `+0xa0`, the attached hint. Invalid for a node out of range or with none attached.
	FElysiumEntityHandle AttachedHint(int32 Node) const;
	void SetAttachedHint(int32 Node, const FElysiumEntityHandle& Hint);

	// `0x102d3e60`, a hint's node lookup over its `m_nNodeID`: -1 answers no node and counts
	// nothing; an id inside the network answers it; any other id bumps `DAT_106c994c` and answers
	// no node. Answers the node index or `INDEX_NONE`.
	int32 ResolveHintNode(int32 NodeId);

	// --- `CNodeEnt::Spawn`'s counter ------------------------------------------------------------

	// `DAT_10926a3c`: the node id the next non-standalone node row takes.
	int32 SpawnCounter() const { return Counter; }
	// The loaded arm of `CNodeEnt::Spawn` for one non-standalone node row, `0x102d79e2..0x102d7a2f`:
	// when the row made a hint (`Hint` set), attach it to `node[counter] + 0xa0` if the counter is
	// inside the network, else count it out (`DAT_106c994c++`); then advance the counter, whether or
	// not a hint was made. Answers the counter the row took -- the hint's `m_nNodeID`.
	int32 SpawnNodeRow(const FElysiumEntityHandle& Hint);

	// --- Map-wide words -------------------------------------------------------------------------

	// The OR of `1 << hull` over every hull a link of this map's graph declares. 0 when unadopted.
	int32 UsedHullBits() const { return HullBits; }
	// The per-hull wander cap, SOURCE units, or 0 when the map declares no cap for the hull.
	float WanderCapUnits(int32 Hull) const;
	// Whether that cap is the human hull's figure standing in for a hull with no link of its own.
	bool WanderCapFromHuman(int32 Hull) const;

	// --- The crosswalk pairs (0018 story 7) -----------------------------------------------------
	//
	// Retail's crosswalk state is the signal nibble `0xf0` of a `CAI_Link`'s info word (`link+0x64`).
	// Its only writer is `0x102f97c0` (from `CAI_Hint::InputWalk` / `InputDontWalk`), which sets or
	// clears the whole nibble on every link of the hint's node whose far end is a crosswalk node
	// (`0x102f98d0`: `node+0xa0 -> hint+0x5dc == 11000`). In shipped content only crosswalk hints
	// take `Walk` / `DontWalk`, so the links written are exactly the pairs below, and one boolean
	// per pair carries the whole state (decision 3). The readers test
	// `link+0x64 & (0x10 << (((int)curtime >> 4) & 3))` -- a four-phase 64-second clock -- but with
	// the nibble written whole the phase never changes an answer: all four set is red in every
	// phase, all four clear green in every phase. The rotation is recorded, not modelled
	// (`navigation-jump-links.md` § "The crosswalk wait, walked"). No AIN link carries `0xf0` and no
	// link word is saved, so every pair starts green at every map load.

	// Node index pairs whose ends both pair to a crosswalk hint (type 11000), lower index first.
	const TArray<FIntPoint>& CrosswalkPairs() const { return Crosswalks; }
	// The pair's hull-0 motion word (`link+0x0c`), 0 for a pair out of range.
	int32 CrosswalkPairMotion(int32 Pair) const;
	// A walking hull can take the pair: its hull-0 word carries `bits_CAP_MOVE_GROUND` (1). The two
	// jump-only hub pairs (260-263, 259-261, word 2) are on no pedestrian route (`0x102ff960`), so
	// the splice reads only walkable pairs.
	bool IsCrosswalkPairWalkable(int32 Pair) const;
	int32 NumWalkableCrosswalkPairs() const;
	// Whether `NodeId` is an end of any walkable pair.
	bool IsWalkableCrosswalkNode(int32 NodeId) const;
	// `0x102f96e0(node, dest)`: the first link of `NodeId` whose far end (`0x102dda40`) is
	// `NextNodeId`, symmetric. Answered as the crosswalk pair it is, or `INDEX_NONE`: a link that
	// is no pair carries no signal bit, which is how every reader treats `NONE`.
	int32 FindCrosswalkPair(int32 NodeId, int32 NextNodeId) const;
	// `0x102f97c0(node, bWalk)`: every pair containing `NodeId` goes green (`Walk`, `& 0xffffff0f`)
	// or red (`DontWalk`, `| 0xf0`). Walkable or not: retail's write tests no motion word.
	void SetCrosswalkWalk(int32 NodeId, bool bWalk);
	// `link+0x64 & phase` for the pair: red, the pedestrian waits. False for `INDEX_NONE`.
	bool IsCrosswalkRed(int32 Pair) const;

private:
	TArray<FElysiumPlaceRow> Rows;
	TArray<float> Cooldowns;                         // node +0x9c, one per row
	TArray<FElysiumEntityHandle> Attached;           // node +0xa0, one per row
	TArray<FElysiumPlaceWanderCap> WanderCaps;
	TArray<FIntPoint> Crosswalks;
	TArray<int32> CrosswalkMotions;                  // link +0x0c (hull 0), one per pair
	TArray<uint8> CrosswalkRed;                      // link +0x64 & 0xf0 != 0, one per pair
	FString AdoptedMapName;
	int32 HullBits = 0;
	int32 Counter = 0;                               // DAT_10926a3c
	bool bAdopted = false;

	const FElysiumPlaceWanderCap* FindWanderCap(int32 Hull) const;
};
