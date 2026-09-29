#pragma once

#include "CoreMinimal.h"
#include "AI/Navigation/NavQueryFilter.h"   // FSharedConstNavQueryFilter, by value
#include "ElysiumWorldServices.h"           // the retail trace and the route / nav-raycast records

class AActor;
class ANavigationData;
class UWorld;
struct FNavAgentProperties;

// The engine side of the geometry seam (0018 story 6): the free functions the map actor's
// `TraceRetail` and the NPC body's `QueryRoute` / `NavRaycast` forward to, so the Unreal queries live
// in one place and the two owners only resolve their own inputs (the world, the nav data, the
// agent, the filter, the entity behind an actor). Centimetres throughout.
namespace ElysiumWorldGeometry
{
	// One retail trace against `World`, split as `FElysiumRetailTraceResult` states it: the world
	// answer on `ElysiumRetailMask::Recipe(Request.RetailMask).Channel` with characters excluded, and
	// the characters the same query met, listed. `IgnoreSelf` is the pass entity's actor (may be
	// null); `ToHandle` names the entity behind a hit actor (Invalid for the static world).
	// False = no collision world answered; `Out` then keeps its clear defaults.
	bool Trace(UWorld& World, const FElysiumRetailTrace& Request, FElysiumRetailTraceResult& Out,
		const AActor* IgnoreSelf, TFunctionRef<FElysiumEntityHandle(const AActor*)> ToHandle);

	// A synchronous path query on `NavData` for `Agent` from `FromCm` to `Query.DestCm` under
	// `Filter` (null = the nav data's default). False = the mesh could not be asked (an end that will
	// not project); "no complete route" is true with `Out.bReachable == false`. Under
	// `bAcceptPartial` a partial path is reachable with `Out.bPartial` set; without it the search is
	// asked for complete paths only, so a partial never comes back.
	bool Route(const ANavigationData& NavData, const FNavAgentProperties& Agent,
		FSharedConstNavQueryFilter Filter, const FVector& FromCm, const FElysiumNpcRouteQuery& Query,
		FElysiumNpcRouteAnswer& Out);

	// The navmesh raycast on `NavData` under `Filter`. False = the mesh could not answer.
	bool Raycast(const ANavigationData& NavData, FSharedConstNavQueryFilter Filter,
		const FElysiumNpcNavRaycast& Query, FElysiumNpcNavRaycastAnswer& Out);
}

// The per-frame cost of the geometry seam, for the budget the story holds it to. Game thread only.
struct FElysiumGeometryBudget
{
	enum class EKind : uint8
	{
		PathTest,
		Raycast,
		Trace,
	};

	uint64 Frame = 0;
	int32 PathTests = 0;
	int32 Raycasts = 0;
	int32 Traces = 0;
	double Ms = 0.0;

	// Count one query of `Kind` that took `InMs` milliseconds against the current frame.
	static void Count(EKind Kind, double InMs);
	// The costliest frame counted so far (by `Ms`), including the current one.
	static const FElysiumGeometryBudget& Peak();
};
