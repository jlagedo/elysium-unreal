#include "Substrate/ElysiumPlaceSet.h"

#include "ElysiumMoveSolve.h"
#include "Substrate/ElysiumEntityWorldShared.h"   // LogElysiumWorld
#include "Substrate/ElysiumRetailHullTable.h"

namespace
{
	// `bits_CAP_MOVE_GROUND`: the motion-word bit a walking hull's capabilities AND against
	// (`0x102ff960`). A crosswalk pair whose hull-0 word lacks it is jump-only.
	constexpr int32 GCrosswalkMoveGround = 1;

	// `+0x70`, the node type, as `CNodeEnt::Spawn` writes it (`0x102d7b50` climb, `0x102d7b59` ground).
	constexpr int32 GNodeTypeGround = 2;
	constexpr int32 GNodeTypeClimb = 4;

	// `+0x74`, the climb node's info bits, in the order `GetPosition` tests them (`0x102fb0f8`,
	// `0x102fb166`, `0x102fb20b`). The Source SDK calls them `bits_NODE_CLIMB_OFF_FORWARD`,
	// `_OFF_LEFT` and `_OFF_RIGHT`; the arms below are read off the listing, not the SDK.
	constexpr int32 GClimbOffForward = 0x4;
	constexpr int32 GClimbOffLeft = 0x8;
	constexpr int32 GClimbOffRight = 0x10;

	// The climb stand-off, `width(hull) * 0.5 + 8.0` Source units: two `.rdata` DOUBLES
	// (`0x102fb0ec FMUL double [0x10449270]` = 0.5, `0x102fb0fa FADD double [0x1049a148]` = 8.0), the
	// sum stored as a float (`0x102fb108`).
	constexpr double GClimbHalfWidth = 0.5;
	constexpr double GClimbStandOffUnits = 8.0;
	// `_DAT_1044eb08`, the float DEG2RAD the node yaw is scaled by (`0x102fb10f`).
	constexpr float GDegToRad = 0.0174532924f;
	// `_DAT_104454c4`, 0.0f: the right vector's Z is `cos * 0 - sin * 0` (`0x102fb178..0x102fb19c`).
	constexpr float GRightZTerm = 0.0f;

	// `0x102d6180(hull)`: the hull row's `+0x14 - +0x08`, maxs.x - mins.x (the row opens with its
	// bit and its name, so `+0x08` is mins.x -- `staticinit_102d4440`), Source units.
	float HullWidthUnits(int32 Hull)
	{
		const ElysiumRetailHulls::FRow& Row = ElysiumRetailHulls::Table[Hull];
		return static_cast<float>(Row.Maxs.X) - static_cast<float>(Row.Mins.X);
	}
}

void FElysiumPlaceSet::Adopt(const UElysiumMapPlaces& Asset)
{
	AdoptRows(Asset.Rows, Asset.UsedHullBits, Asset.WanderCaps, Asset.CrosswalkPairs,
		Asset.CrosswalkPairMotions);
	AdoptedMapName = Asset.MapName;
	// The payload reader refuses a pair row without its motion word, but an asset authored before
	// the word rode it adopts with every pair's word 0: no pair is walkable and the pedestrian
	// splice is off for the whole map. Said once per map, with the fix.
	if (Asset.CrosswalkPairMotions.Num() != Asset.CrosswalkPairs.Num())
	{
		static TSet<FString> Warned;
		if (!Warned.Contains(Asset.MapName))
		{
			Warned.Add(Asset.MapName);
			UE_LOG(LogElysiumWorld, Warning,
				TEXT("%s: %d crosswalk pairs carry %d hull-0 motion words; the pairs without one read as "
					"jump-only and no pedestrian waits at them (re-bake: uv run elysium bake map --maps %s)"),
				*Asset.MapName, Asset.CrosswalkPairs.Num(), Asset.CrosswalkPairMotions.Num(), *Asset.MapName);
		}
	}
}

void FElysiumPlaceSet::AdoptRows(TArray<FElysiumPlaceRow> InRows, int32 InUsedHullBits,
	TArray<FElysiumPlaceWanderCap> InWanderCaps, TArray<FIntPoint> InCrosswalkPairs,
	TArray<int32> InCrosswalkMotions)
{
	Rows = MoveTemp(InRows);
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		Rows[Index].NetworkIndex = Index;
	}
	HullBits = InUsedHullBits;
	WanderCaps = MoveTemp(InWanderCaps);
	Crosswalks = MoveTemp(InCrosswalkPairs);
	CrosswalkMotions = MoveTemp(InCrosswalkMotions);
	CrosswalkMotions.SetNumZeroed(Crosswalks.Num());
	CrosswalkRed.Init(0, Crosswalks.Num());
	AdoptedMapName.Reset();
	bAdopted = true;
	// The run-time words are sized to the network here and zeroed by `BeginMapSpawn`, so a place
	// set adopted after a spawn pass still answers in range.
	Cooldowns.Init(0.0f, Rows.Num());
	Attached.Init(FElysiumEntityHandle::Invalid(), Rows.Num());
}

int32& ElysiumAiNetwork::NodeMissCounter()
{
	static int32 Count = 0;                                                    // DAT_106c994c
	return Count;
}

void FElysiumPlaceSet::BeginMapSpawn()
{
	// `0x102f6690`: `DAT_10926a3c = 0`, beside a fresh `ai_network` whose nodes the ctor
	// `0x102fc5d0` builds with `+0x9c = 0` and `+0xa0 = 0`. `DAT_106c994c` is not touched: nothing
	// in the image zeroes it.
	Counter = 0;
	Cooldowns.Init(0.0f, Rows.Num());
	Attached.Init(FElysiumEntityHandle::Invalid(), Rows.Num());
	// The links are the loaded AIN's, whose info words carry no signal nibble: every pair green.
	CrosswalkRed.Init(0, Crosswalks.Num());
}

bool FElysiumPlaceSet::GetPositionCm(int32 Node, int32 Hull, FVector& OutCm) const
{
	if (!IsValidNode(Node) || ElysiumRetailHulls::Find(Hull) == nullptr)
	{
		return false;
	}
	const FElysiumPlaceRow& Row = Rows[Node];
	if (Row.Type == GNodeTypeClimb)                                            // 0x102fb0d9 CMP EAX,4
	{
		const float Scale = static_cast<float>(
			static_cast<double>(HullWidthUnits(Hull)) * GClimbHalfWidth + GClimbStandOffUnits);
		// The arms are Source-frame arithmetic; the row is Unreal-native, so the yaw is reflected
		// back and the offset reflected forward (`bsp.source_to_unreal` negates Y).
		const double Radians = static_cast<double>(-Row.YawDeg) * static_cast<double>(GDegToRad);
		const float Cos = static_cast<float>(FMath::Cos(Radians));             // 0x102fb117 FCOS
		const float Sin = static_cast<float>(FMath::Sin(Radians));             // 0x102fb11d FSIN
		const FVector3f Forward(Cos, Sin, 0.0f);
		const FVector3f Right(Sin, -Cos, Cos * GRightZTerm - Sin * GRightZTerm);
		const FVector3f Along = Forward * Scale;                               // 0x10011a40(fwd, s)
		const FVector3f Across = Right * (Scale + Scale);                      // 0x102fb1b1 FADD ST0,ST0
		FVector3f OffsetUnits;
		if ((Row.Flags & GClimbOffForward) != 0)                               // 0x102fb0f8 TEST AL,4
		{
			OffsetUnits = Along;                                               // origin + fwd*s
		}
		else if ((Row.Flags & GClimbOffLeft) != 0)                             // 0x102fb166 TEST AL,8
		{
			OffsetUnits = -Across - Along;                                     // (origin - right*2s) - fwd*s
		}
		else if ((Row.Flags & GClimbOffRight) != 0)                            // 0x102fb20b TEST AL,0x10
		{
			OffsetUnits = Across - Along;                                      // (right*2s + origin) - fwd*s
		}
		else
		{
			OffsetUnits = -Along;                                              // origin - fwd*s
		}
		OutCm = Row.OriginCm
			+ FVector(OffsetUnits.X, -OffsetUnits.Y, OffsetUnits.Z) * static_cast<double>(ElysiumMove::U);
		return true;
	}
	if (Row.Type != GNodeTypeGround)                                           // 0x102fb2f6 CMP EAX,2
	{
		OutCm = Row.OriginCm;                                                  // 0x102fb31f
		return true;
	}
	OutCm = Row.OriginCm + FVector(0.0, 0.0, Row.ZOffsetCm[Hull]);            // 0x102fb303 [ESI+hull*4+0x14]
	return true;
}

FVector FElysiumPlaceSet::NetworkNodePositionCm(int32 NodeId, int32 Hull) const
{
	// `0x102f46d0`: `+4 == 0` -> vec3_origin; `-1 < id && id <= *this` -> GetPosition(node,
	// npc+0x156c); else vec3_origin. The caller has already taken the null-NPC arm. `IsValidNode`
	// is `id < count`: the `id == count` read past the array answers the origin (see the header).
	FVector Out = FVector::ZeroVector;
	if (IsValidNode(NodeId) && GetPositionCm(NodeId, Hull, Out))
	{
		return Out;
	}
	return FVector::ZeroVector;                                                // DAT_1070d1b0..b8
}

float FElysiumPlaceSet::NetworkNodeYawSource(int32 NodeId) const
{
	// `0x102f47b0`: `FLD [node+0x6c]`, else `FLD [0x104454c4]` (0.0f). The row's yaw is the
	// Unreal-native (negated) one; retail's word is the Source yaw.
	return IsValidNode(NodeId) ? -Rows[NodeId].YawDeg : 0.0f;
}

namespace
{
	// `AI_NearNode_t`, the queue's 8-byte element: `+0` the distance, `+4` the node index.
	struct FNearNode
	{
		float Dist = 0.0f;
		int32 Node = INDEX_NONE;
	};

	// `0x102f3770(a, b)`: `*b < *a`. The less-priority test of both queues.
	bool NearNodeLess(const FNearNode& A, const FNearNode& B)
	{
		return B.Dist < A.Dist;
	}

	// `CUtlPriorityQueue::Insert` as `0x102f32f0` inlines it: append (`0x102f90e0`), then sift up
	// while the element is not less than its parent.
	void NearNodeInsert(TArray<FNearNode>& Heap, const FNearNode& Element)
	{
		int32 Index = Heap.Add(Element);
		while (Index != 0)
		{
			const int32 Parent = (Index + 1) / 2 - 1;
			if (NearNodeLess(Heap[Index], Heap[Parent]))
			{
				break;
			}
			Heap.Swap(Parent, Index);                                          // 0x102f9090
			Index = Parent;
		}
	}

	// `CUtlPriorityQueue::RemoveAtHead`: `0x102f9000(heap, 0)` copies the last element over the head
	// and drops the count, then the inline sift-down picks, of the node and its two children, the one
	// the other two are "less" than, and stops when the node wins or it passes `count / 2`.
	void NearNodeRemoveAtHead(TArray<FNearNode>& Heap)
	{
		if (Heap.Num() == 0)
		{
			return;
		}
		Heap[0] = Heap.Last();
		Heap.Pop(EAllowShrinking::No);
		const int32 Count = Heap.Num();
		const int32 Half = Count / 2;
		int32 Index = 0;
		if (Half <= 0)
		{
			return;
		}
		for (;;)
		{
			int32 Best = Index;
			const int32 Left = Index * 2 + 1;
			if (Left < Count && NearNodeLess(Heap[Index], Heap[Left]))
			{
				Best = Left;
			}
			const int32 Right = Index * 2 + 2;
			if (Right < Count && NearNodeLess(Heap[Best], Heap[Right]))
			{
				Best = Right;
			}
			if (Best == Index)
			{
				return;
			}
			Heap.Swap(Index, Best);
			Index = Best;
			if (Index >= Half)
			{
				return;
			}
		}
	}
}

TArray<int32> FElysiumPlaceSet::ListNodesInBox(int32 MaxCount, const FVector& MinsUnits,
	const FVector& MaxsUnits, TFunctionRef<bool(int32)> IsValid, TFunctionRef<float(int32)> DistanceSqr) const
{
	TArray<FNearNode> Result;                                                  // local_18, Less 0x102f3770
	bool bFull = false;
	for (int32 Node = 0; Node < Rows.Num(); ++Node)
	{
		if (!IsValid(Node))                                                    // (*param_5)[0](node)
		{
			continue;
		}
		const FVector O = Rows[Node].OriginCm / static_cast<double>(ElysiumMove::U);   // node +0x08..+0x10
		if (!(MinsUnits.X <= O.X && O.X <= MaxsUnits.X && MinsUnits.Y <= O.Y && O.Y <= MaxsUnits.Y
			&& MinsUnits.Z <= O.Z && O.Z <= MaxsUnits.Z))
		{
			continue;
		}
		const float Dist = DistanceSqr(Node);                                  // (*param_5)[1](node)
		if (bFull)
		{
			if (Result[0].Dist <= Dist)                                        // 0x102f3463 *local_18[0] <= d
			{
				continue;
			}
			NearNodeRemoveAtHead(Result);
		}
		NearNodeInsert(Result, FNearNode{ Dist, Node });
		bFull = Result.Num() == MaxCount;
	}
	// The copy into the caller's queue (same `Less`), popping this one from its head.
	TArray<FNearNode> List;
	while (Result.Num() != 0)
	{
		NearNodeInsert(List, Result[0]);
		NearNodeRemoveAtHead(Result);
	}
	// The order the consumers (`0x102f41b0`, `0x102f3c10`) visit it: head, remove head, repeat.
	TArray<int32> Order;
	Order.Reserve(List.Num());
	while (List.Num() != 0)
	{
		Order.Add(List[0].Node);
		NearNodeRemoveAtHead(List);
	}
	return Order;
}

float FElysiumPlaceSet::NodeCooldown(int32 Node) const
{
	return Cooldowns.IsValidIndex(Node) ? Cooldowns[Node] : 0.0f;
}

void FElysiumPlaceSet::SetNodeCooldown(int32 Node, float Value)
{
	if (Cooldowns.IsValidIndex(Node))
	{
		Cooldowns[Node] = Value;
	}
}

FElysiumEntityHandle FElysiumPlaceSet::AttachedHint(int32 Node) const
{
	return Attached.IsValidIndex(Node) ? Attached[Node] : FElysiumEntityHandle::Invalid();
}

void FElysiumPlaceSet::SetAttachedHint(int32 Node, const FElysiumEntityHandle& Hint)
{
	if (Attached.IsValidIndex(Node))
	{
		Attached[Node] = Hint;
	}
}

int32 FElysiumPlaceSet::ResolveHintNode(int32 NodeId)
{
	// `0x102d3e60`: `+0x5e4 != -1`, then `-1 < id && id < *DAT_1093407c`; the miss arm bumps
	// `DAT_106c994c` and falls to the null answer.
	if (NodeId == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	if (NodeId >= 0 && NodeId < NumNodes())
	{
		return NodeId;
	}
	++ElysiumAiNetwork::NodeMissCounter();
	return INDEX_NONE;
}

int32 FElysiumPlaceSet::SpawnNodeRow(const FElysiumEntityHandle& Hint)
{
	const int32 NodeId = Counter;
	if (Hint.IsSet())                                                          // 0x102d79fa TEST EBX,EBX
	{
		// `0x102d7a04 JL` / `0x102d7a0f JGE` -> `0x102d7a3b INC [0x106c994c]`; else the node
		// pointer, null-tested (`0x102d7a17`), takes the hint at `+0xa0`.
		if (IsValidNode(NodeId))
		{
			Attached[NodeId] = Hint;
		}
		else
		{
			++ElysiumAiNetwork::NodeMissCounter();
		}
	}
	++Counter;                                                                 // 0x102d7a28 INC EDX
	return NodeId;
}

const FElysiumPlaceWanderCap* FElysiumPlaceSet::FindWanderCap(int32 Hull) const
{
	return WanderCaps.FindByPredicate([Hull](const FElysiumPlaceWanderCap& Cap) { return Cap.Hull == Hull; });
}

float FElysiumPlaceSet::WanderCapUnits(int32 Hull) const
{
	const FElysiumPlaceWanderCap* Cap = FindWanderCap(Hull);
	return Cap != nullptr ? Cap->CapUnits : 0.0f;
}

bool FElysiumPlaceSet::WanderCapFromHuman(int32 Hull) const
{
	const FElysiumPlaceWanderCap* Cap = FindWanderCap(Hull);
	return Cap != nullptr && Cap->bFromHuman;
}

int32 FElysiumPlaceSet::CrosswalkPairMotion(int32 Pair) const
{
	return CrosswalkMotions.IsValidIndex(Pair) ? CrosswalkMotions[Pair] : 0;
}

bool FElysiumPlaceSet::IsCrosswalkPairWalkable(int32 Pair) const
{
	return (CrosswalkPairMotion(Pair) & GCrosswalkMoveGround) != 0;
}

int32 FElysiumPlaceSet::NumWalkableCrosswalkPairs() const
{
	int32 Count = 0;
	for (int32 Pair = 0; Pair < Crosswalks.Num(); ++Pair)
	{
		Count += IsCrosswalkPairWalkable(Pair) ? 1 : 0;
	}
	return Count;
}

bool FElysiumPlaceSet::IsWalkableCrosswalkNode(int32 NodeId) const
{
	for (int32 Pair = 0; Pair < Crosswalks.Num(); ++Pair)
	{
		if ((Crosswalks[Pair].X == NodeId || Crosswalks[Pair].Y == NodeId) && IsCrosswalkPairWalkable(Pair))
		{
			return true;
		}
	}
	return false;
}

int32 FElysiumPlaceSet::FindCrosswalkPair(int32 NodeId, int32 NextNodeId) const
{
	// `0x102f96e0`: `for (i < node+0x78) if (0x102dda40(link[i], node+4) == dest) return link[i];`.
	// Each pair is staged once, so the first match is the one link.
	for (int32 Pair = 0; Pair < Crosswalks.Num(); ++Pair)
	{
		const FIntPoint& Ends = Crosswalks[Pair];
		if ((Ends.X == NodeId && Ends.Y == NextNodeId) || (Ends.Y == NodeId && Ends.X == NextNodeId))
		{
			return Pair;
		}
	}
	return INDEX_NONE;
}

void FElysiumPlaceSet::SetCrosswalkWalk(int32 NodeId, bool bWalk)
{
	// `0x102f97c0(node, bWalk)`: each link of the node whose far end passes `0x102f98d0` -- here,
	// each pair holding the node -- `& 0xffffff0f` for Walk (1), `| 0xf0` for DontWalk (0). The
	// far-end bounds miss (`DAT_106c994c++`, a null node `0x102f98d0` refuses) cannot arise: a staged
	// pair names two nodes of the network.
	for (int32 Pair = 0; Pair < Crosswalks.Num(); ++Pair)
	{
		if (Crosswalks[Pair].X == NodeId || Crosswalks[Pair].Y == NodeId)
		{
			CrosswalkRed[Pair] = bWalk ? 0 : 1;
		}
	}
}

bool FElysiumPlaceSet::IsCrosswalkRed(int32 Pair) const
{
	return CrosswalkRed.IsValidIndex(Pair) && CrosswalkRed[Pair] != 0;
}
