// 0018 story 7, the crosswalk lane: the bodies declared in `Substrate/ElysiumNpcCrosswalk.inl` and
// `FElysiumNpc::MovementSinkObstructed` (`0x10298340`, declared in `ElysiumNpc.h`). The walked prose
// is `docs/vtmb/navigation-jump-links.md` § "The crosswalk wait, walked" and
// `docs/vtmb/npc-ai/conditions-and-states.md` (the crosswalk rule); the wait test, its helper and the
// per-think rule are family Dialogue's (`ElysiumNpcDialogueBodies.cpp`).
//
// The retail chain, in the order the bytecode sees it:
//
//   1. A type-8 goal (`SCHED_TROIKA_WALK_TO_INTERESTING_PLACE 0x100`, `GET_PATH_TO_INTERESTING_PLACE`)
//      builds its route with the pedestrian search; both curbs of every crosswalk pair on it are node
//      waypoints flagged `4 | 0x20` (`0x102fcd00`). Here: `NavLayPedestrianLegs`.
//   2. Reaching a curb, the waypoint advance `0x102f0400` runs the wait test `0x102a0bc0`: this node
//      to the next waypoint's node is a red pair -> `AT_CROSSWALK` and the link (`0x102a0b90`). The
//      path pops either way. Here: `NavAdvancePath` -> `NavPedestrianAdvance`.
//   3. The next think's `UpdatePedestrianInfo` (`0x102a0d20`, from `RunAI`) raises
//      `CROSSWALK_DONTWALK`, which interrupts `0x100`; the selector answers `0x102`
//      (`PAUSE_MOVING; FACE_NEXT_NODE; WAIT_INDEFINITE`). `PRESERVE_PATH` (set by `0xff`) keeps the
//      path across the change and `PAUSE_MOVING` (`0x102bf770`) pauses it (`0x102ee2a0`).
//   4. A walker blocked by the waiter queues behind it (`0x10298340`, below) and, through the same
//      wait test, becomes a waiter itself.
//   5. On green the 1 s re-check raises `CROSSWALK_WALK` and drops `AT_CROSSWALK`; `0x102`'s interrupt
//      reselects `0x100`, whose `SetGoal` builds a FRESH route -- step 1 again from the curb the
//      pedestrian stands on. The paused path is not resumed.

#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcFlags.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumPlaceSet.h"

namespace
{
	// The splice's capture distance, SOURCE units, measured flat (2-D): a curb is on the route when
	// the route passes within this of the curb's place. Findings R-C § 1: the nearest two distinct
	// crosswalk places on the hub are the corner pairs, 79.9 and 84.8 units apart, and a body may
	// cross the road gap up to 42.5 units off the pair line; 48 takes the second and not the first.
	constexpr double GXwCaptureUnits = 48.0;

	// The waypoint flag words the pedestrian chain `0x102fcd00` writes: `4` bits_WP_TO_NODE on every
	// node waypoint (its base word), `0x20` bits_WP_DONT_SIMPLIFY on both curbs of a crosswalk pair
	// (`102fcd98 OR AL,0x20`), `8` the goal.
	constexpr int32 GXwWaypointToNode = 0x4;
	constexpr int32 GXwWaypointGoal = 0x8;
	constexpr int32 GXwWaypointDontSimplify = 0x20;

	// `_DAT_1045d650`, 1024.0f: arm (iii) of `0x10298340`, 32 units squared, inclusive (`TEST AH,0x41`).
	constexpr float GXwQueueAdvanceDistSqrUnits = 1024.0f;

	// The start node's place on the route: before every point of it (`0x10304e00` starts the chain at
	// `NearestNodeToNPC`).
	constexpr double GXwStartNodeParam = -1.0;

	struct FXwCurb
	{
		double Param = 0.0;
		int32 Node = INDEX_NONE;
	};

	// The route's closest approach to `At`, flat: the least 2-D distance and the 2-D route length
	// walked to it. False for a route with no point.
	bool XwClosestOnRoute(const TArray<FVector>& Points, const FVector& At, double& OutDistCm,
		double& OutParamCm)
	{
		if (Points.Num() == 0)
		{
			return false;
		}
		const FVector P(At.X, At.Y, 0.0);
		double Best = FVector::Dist(FVector(Points[0].X, Points[0].Y, 0.0), P);
		double BestParam = 0.0;
		double Walked = 0.0;
		for (int32 Index = 0; Index + 1 < Points.Num(); ++Index)
		{
			const FVector A(Points[Index].X, Points[Index].Y, 0.0);
			const FVector B(Points[Index + 1].X, Points[Index + 1].Y, 0.0);
			const FVector AB = B - A;
			const double Length = AB.Size();
			const double T = Length > 0.0
				? FMath::Clamp(FVector::DotProduct(P - A, AB) / (Length * Length), 0.0, 1.0) : 0.0;
			const double Dist = FVector::Dist(A + AB * T, P);
			if (Dist < Best)
			{
				Best = Dist;
				BestParam = Walked + Length * T;
			}
			Walked += Length;
		}
		OutDistCm = Best;
		OutParamCm = BestParam;
		return true;
	}
}

// =================================================================================================
// The splice (decision 2)
// =================================================================================================

bool FElysiumNpc::NavLayPedestrianLegs(const FElysiumNpcMoveRequest& Request)
{
	PedestrianLegs.Reset();
	const FElysiumPlaceSet* Places = World != nullptr ? &World->Places() : nullptr;
	const int32 Hull = RetailPathingHull();
	TArray<int32> Curbs;
	// A map with no walkable pair has no curb to find; its route is the goal alone, and the body is
	// not asked (no query is recorded against a world without crosswalks).
	if (Places != nullptr && Motor != nullptr && Places->NumWalkableCrosswalkPairs() > 0)
	{
		// `BuildNodeRoute 0x10304e00`: the chain runs from `NearestNodeToNPC` (`0x102f3c10`) to the
		// goal's node (`0x102f41b0`); the same node at both ends builds no chain and so no curb.
		const int32 StartNode = NavNearestNodeToNpc(Origin);
		const int32 GoalNode = NavNearestNodeTo(Request.DestinationCm);
		FElysiumNpcRouteQuery Query;
		Query.DestCm = Request.DestinationCm;
		Query.PedestrianCostMultiplier = Request.PedestrianCostMultiplier;   // the multiplier the leg walks
		FElysiumNpcRouteAnswer Answer;
		if (!(StartNode != INDEX_NONE && StartNode == GoalNode)
			&& Motor->QueryRoute(Query, Answer) && Answer.bReachable)
		{
			// Every end of a walkable pair the route passes, in route order. The start node, when it
			// is a curb, is where the chain begins whatever the NavMesh route does near it.
			TArray<FXwCurb> Candidates;
			TSet<int32> Seen;
			const TArray<FIntPoint>& Pairs = Places->CrosswalkPairs();
			for (int32 Pair = 0; Pair < Pairs.Num(); ++Pair)
			{
				if (!Places->IsCrosswalkPairWalkable(Pair))
				{
					continue;
				}
				const int32 Ends[2] = { Pairs[Pair].X, Pairs[Pair].Y };
				for (const int32 Node : Ends)
				{
					if (Seen.Contains(Node))
					{
						continue;
					}
					Seen.Add(Node);
					if (Node == StartNode)
					{
						Candidates.Add(FXwCurb{ GXwStartNodeParam, Node });
						continue;
					}
					double DistCm = 0.0;
					double ParamCm = 0.0;
					if (XwClosestOnRoute(Answer.PointsCm, Places->NetworkNodePositionCm(Node, Hull), DistCm, ParamCm)
						&& DistCm <= GXwCaptureUnits * ElysiumMove::U)
					{
						Candidates.Add(FXwCurb{ ParamCm, Node });
					}
				}
			}
			Candidates.Sort([](const FXwCurb& A, const FXwCurb& B)
			{
				return A.Param != B.Param ? A.Param < B.Param : A.Node < B.Node;
			});
			// `0x102fcd00`: two consecutive route nodes that both pass `0x102f98d0` flag the pair --
			// both curbs are waypoints, entered at one and left at the other. Here: consecutive
			// captured curbs that are a walkable pair.
			for (int32 Index = 0; Index + 1 < Candidates.Num(); ++Index)
			{
				const int32 From = Candidates[Index].Node;
				const int32 To = Candidates[Index + 1].Node;
				const int32 Pair = Places->FindCrosswalkPair(From, To);
				if (Pair == INDEX_NONE || !Places->IsCrosswalkPairWalkable(Pair))
				{
					continue;
				}
				if (Curbs.Num() == 0 || Curbs.Last() != From)
				{
					Curbs.Add(From);
				}
				Curbs.Add(To);
			}
		}
	}
	for (const int32 Node : Curbs)
	{
		PedestrianLegs.Add(FPedestrianLeg{ Places->NetworkNodePositionCm(Node, Hull), Node,
			GXwWaypointToNode | GXwWaypointDontSimplify });
	}
	PedestrianLegs.Add(FPedestrianLeg{ Request.DestinationCm, INDEX_NONE, GXwWaypointGoal });

	FElysiumNpcMoveRequest Head = Request;
	Head.DestinationCm = PedestrianLegs[0].DestCm;
	if (!NavIssueLeg(Head))
	{
		PedestrianLegs.Reset();
		return false;
	}
	return true;
}

bool FElysiumNpc::NavPedestrianAdvance()
{
	// `0x1030ba90`: not the goal (the caller returned on it), a next waypoint stands -> a node
	// waypoint (`+0x28 & 4`) stores its node at `path+0x44`, the head is unlinked and the next one
	// installed (`0x1030b4d0`).
	if (PedestrianLegs.Num() < 2)
	{
		return Navigator.bHasHeadWaypoint;
	}
	const FPedestrianLeg Passed = PedestrianLegs[0];
	PedestrianLegs.RemoveAt(0);
	if ((Passed.Flags & GXwWaypointToNode) != 0)
	{
		Navigator.LastNodePassed = Passed.NodeId;                             // path+0x44 = wp+0x10
	}
	Navigator.bHasHeadWaypoint = true;
	Navigator.bHeadIsGoal = PedestrianLegs.Num() == 1;
	// The leg to the new head: the route's own request, re-aimed. A refused leg leaves the head
	// standing with no request; the next move step reads the refusal as the body giving up.
	bMoveIssued = false;
	if (Navigator.bHeadLegRequestSet)
	{
		FElysiumNpcMoveRequest Next = Navigator.HeadLegRequest;
		Next.DestinationCm = PedestrianLegs[0].DestCm;
		bMoveIssued = NavIssueLeg(Next);
	}
	return true;
}

bool FElysiumNpc::PedestrianHeadWaypoint(FDialogPedWaypoint& Out) const
{
	Out = FDialogPedWaypoint();
	if (!Navigator.bHasHeadWaypoint)                                         // path+0x24 == NULL
	{
		return false;
	}
	if (PedestrianLegs.Num() > 0)
	{
		const FPedestrianLeg& Head = PedestrianLegs[0];
		Out.DestCm = Head.DestCm;
		Out.NodeId = Head.NodeId;
		Out.Flags = Head.Flags;
		Out.bHasNext = PedestrianLegs.Num() > 1;                             // wp+0x30
		Out.NextNodeId = Out.bHasNext ? PedestrianLegs[1].NodeId : INDEX_NONE;
		return true;
	}
	// Any other route: the head is the leg the body walks, a waypoint that is no node.
	Out.DestCm = Navigator.bHeadLegRequestSet ? Navigator.HeadLegRequest.DestinationCm : Navigator.GetGoalPos();
	Out.Flags = Navigator.bHeadIsGoal ? GXwWaypointGoal : 0;
	return true;
}

// =================================================================================================
// The park -- where retail's `Move` does not move the body
// =================================================================================================

void FElysiumNpc::NavParkBody()
{
	if (bNavBodyParked || Motor == nullptr || !Navigator.bHeadLegRequestSet)
	{
		return;
	}
	Motor->Stop();
	bNavBodyParked = true;
	// `Stop` also ends a turn-in-place; the ideal yaw the tasks set this think is handed back, a
	// presentation re-hand of the motor's `+0x34` with no retail word written.
	Motor->Face(-MotorIdealYaw, MotorYawRateDegPerS(MotorYawSpeedWord));
}

void FElysiumNpc::NavUnparkBody()
{
	if (!bNavBodyParked)
	{
		return;
	}
	bNavBodyParked = false;
	if (Navigator.bHasHeadWaypoint)
	{
		NavReissueHeadLeg();
	}
}

// =================================================================================================
// `0x10298340` -- the Troika movement sink's slot 1 (the obstruction callback)
// =================================================================================================

bool FElysiumNpc::MovementSinkObstructed(const FNavStepFacts& Step)
{
	// `CAI_BaseNPCTroika`'s override of `CAI_DefMovementSink` slot 1 (sub-object `npc+0x19b0`),
	// dispatched by S1 `0x102eefb0` when the step is obstructed. Four arms, in the listing's order:
	//
	//   (i)   `goal+0x60` the obstruction, `+0x98` its Troika self, `+0x14b8 >> 2 & 1` ITS
	//         `AT_CROSSWALK` -- I am blocked by an NPC waiting at a crossing. Else (iv).
	//   (ii)  `0x102a0bc0(this, path->CurWaypoint)` on MY head waypoint: true -> `*result = 0`,
	//         handled. The move is swallowed this frame and I queue behind the waiter. The test's
	//         own side effect (`0x102a0b90`) latches MY `AT_CROSSWALK` and link, so a walker queued
	//         behind a waiter is a waiter: its next think raises `CROSSWALK_DONTWALK` and pauses it
	//         under schedule `0x102`. Retail's swallowed step does not move the body this frame; the
	//         port's body would walk on (round the waiter, under crowd avoidance) until that pause
	//         lands, so it is PARKED for the step (`NavParkBody`, the named modernization there): the
	//         next `Move` pass re-issues the head leg unless the path is paused by then, and the body
	//         walks into the waiter again to be re-probed, as retail re-probes every frame.
	//   (iii) `|wp - GetAbsOrigin (slot 217)|^2 <= 1024.0` (3-D, floats, inclusive; NaN falls to (iv))
	//         -> `AdvancePath` (`0x102f0400`), `*result = 0`, handled.
	//   (iv)  the base `0x1027dc10` (the door step).
	//
	// Retail dereferences the head waypoint in (iii) unguarded; with no head the port takes (iv).
	FElysiumEntity* Blocker = World != nullptr && Step.Blocker.IsSet() ? World->Resolve(Step.Blocker) : nullptr;
	const FElysiumNpc* Waiter = Blocker != nullptr ? Blocker->AsNpc() : nullptr;          // +0x98
	if (Waiter != nullptr && Waiter->NpcFlags.Has(EElysiumNpcFlag::AT_CROSSWALK))       // +0x14b8 & 4
	{
		FDialogPedWaypoint Head;
		const bool bHead = PedestrianHeadWaypoint(Head);
		if (ResolvePedestrianPathNode(bHead ? &Head : nullptr))              // (ii) 0x102a0bc0
		{
			NavParkBody();                                                   // this frame's step: none
			MoveSinkResult = ENavMoveResult::Ok;                             // *result = 0
			return true;
		}
		if (bHead)
		{
			const float U = ElysiumMove::U;
			const float Dx = static_cast<float>(Origin.X - Head.DestCm.X) / U;
			const float Dy = static_cast<float>(Origin.Y - Head.DestCm.Y) / U;
			const float Dz = static_cast<float>(Origin.Z - Head.DestCm.Z) / U;
			const float DistSqr = Dx * Dx + Dy * Dy + Dz * Dz;
			if (DistSqr <= GXwQueueAdvanceDistSqrUnits)                      // (iii) _DAT_1045d650
			{
				NavAdvancePath();                                            // 0x102f0400
				MoveSinkResult = ENavMoveResult::Ok;                         // *result = 0
				return true;
			}
		}
	}
	return NavMoveSinkDoorStep(Step);                                        // (iv) 0x1027dc10, result in MoveSinkResult
}

// =================================================================================================
// The pedestrian link's save words -- Troika Save / Restore tails (the archive is the mechanism)
// and the re-find `0x102998c0`
// =================================================================================================

void FElysiumNpc::SerializePedestrianLink(FElysiumSaveArchive& Ar)
{
	// Save: `WriteBool(+0x630c != 0)` (`ISave` slot 12), then, set, `WriteInt(link+4)` and
	// `WriteInt(link+8)` (slot 10): the link's two node ids. Restore: `ReadBool` (slot 17) and, set,
	// `ReadInt` into `+0x6310` / `+0x6314` (slot 15). The signal nibble is not saved.
	if (Ar.IsSaving())
	{
		const FElysiumPlaceSet* Places = World != nullptr ? &World->Places() : nullptr;
		const bool bPairKnown = Places != nullptr && Places->CrosswalkPairs().IsValidIndex(PedestrianPair);
		bool bLinked = bPairKnown;
		Ar << bLinked;
		if (bLinked)
		{
			int32 Node = Places->CrosswalkPairs()[PedestrianPair].X;           // link+4
			int32 DestNode = Places->CrosswalkPairs()[PedestrianPair].Y;       // link+8
			Ar << Node;
			Ar << DestNode;
		}
		return;
	}
	bool bLinked = false;
	Ar << bLinked;
	if (bLinked)
	{
		Ar << RestorePedLinkNode;                                            // +0x6310
		Ar << RestorePedLinkDestNode;                                        // +0x6314
	}
}

void FElysiumNpc::RestorePedestrianLink()
{
	// `0x102998c0`, after `CAI_BaseNPC::OnRestore` and the interesting-place re-find:
	//
	//     if (+0x6310 != -1 && +0x6314 != -1) {
	//         if (+0x6310 < 0 || count <= +0x6310) DAT_106c994c++;
	//         else if (node = nodes[+0x6310]) +0x630c = 0x102f96e0(node, +0x6314);
	//     }
	//
	// A bounds miss leaves `+0x630c` as it stood. The re-found link carries no saved signal: it reads
	// green, so a restored waiter is released on its next check.
	if (RestorePedLinkNode == INDEX_NONE || RestorePedLinkDestNode == INDEX_NONE)
	{
		return;
	}
	const FElysiumPlaceSet* Places = World != nullptr ? &World->Places() : nullptr;
	if (Places == nullptr || !Places->IsValidNode(RestorePedLinkNode))
	{
		++NodeIndexErrorCount();                                             // DAT_106c994c
		return;
	}
	PedestrianPair = Places->FindCrosswalkPair(RestorePedLinkNode, RestorePedLinkDestNode);
}
