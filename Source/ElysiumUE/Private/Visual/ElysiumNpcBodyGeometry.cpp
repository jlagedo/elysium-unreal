#include "Visual/ElysiumNpcBody.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "Map/ElysiumNavQueryFilter_Pedestrian.h"
#include "Map/ElysiumWorldGeometry.h"
#include "NavigationData.h"
#include "NavigationSystem.h"

// 0018 story 6: the NPC body's half of the geometry seam -- the path-length service and the navmesh
// raycast, on this body's own nav agent. The body resolves the nav data, the agent and the filter;
// `ElysiumWorldGeometry` asks the mesh.

namespace ElysiumNpcBodyGeometry
{
	// The nav data this body walks: its own agent (the pathing hull's, stated by `ApplyRetailHull`)
	// at its feet. Null = no NavMesh behind this body.
	const ANavigationData* ResolveNavData(const AElysiumNpcBody& Body, const FNavAgentProperties*& OutAgent)
	{
		OutAgent = nullptr;
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Body.GetWorld());
		const UCharacterMovementComponent* Movement = Body.GetCharacterMovement();
		if (NavSys == nullptr || Movement == nullptr)
		{
			return nullptr;
		}
		OutAgent = &Movement->NavAgentProps;
		return NavSys->GetNavDataForProps(*OutAgent, Body.GetNavAgentLocation());
	}
}

bool AElysiumNpcBody::QueryRoute(const FElysiumNpcRouteQuery& Query, FElysiumNpcRouteAnswer& Out) const
{
	// A route on this body's own mesh, asked synchronously and measured, under the filter `MoveTo`
	// builds for the same multiplier (`ElysiumNavQueryFilterPedestrian::MakeFilter`: 0 = the nav
	// data's default, 5..10 = the priced pedestrian copy), so the route measured is the route walked.
	const FNavAgentProperties* Agent = nullptr;
	const ANavigationData* NavData = ElysiumNpcBodyGeometry::ResolveNavData(*this, Agent);
	if (NavData == nullptr || Agent == nullptr)
	{
		return false;
	}
	const FSharedConstNavQueryFilter Filter = ElysiumNavQueryFilterPedestrian::MakeFilter(
		*NavData, this, FMath::Max(0, Query.PedestrianCostMultiplier));
	return ElysiumWorldGeometry::Route(*NavData, *Agent, Filter, GetNavAgentLocation(), Query, Out);
}

bool AElysiumNpcBody::NavRaycast(const FElysiumNpcNavRaycast& Query, FElysiumNpcNavRaycastAnswer& Out) const
{
	// The Detour raycast on the same nav data, under the same filter a route would use.
	const FNavAgentProperties* Agent = nullptr;
	const ANavigationData* NavData = ElysiumNpcBodyGeometry::ResolveNavData(*this, Agent);
	if (NavData == nullptr)
	{
		return false;
	}
	const FSharedConstNavQueryFilter Filter = ElysiumNavQueryFilterPedestrian::MakeFilter(
		*NavData, this, FMath::Max(0, Query.PedestrianCostMultiplier));
	return ElysiumWorldGeometry::Raycast(*NavData, Filter, Query, Out);
}
