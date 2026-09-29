#include "Visual/ElysiumNpcBody.h"

#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationData.h"
#include "NavigationSystem.h"

// 0018 story 6: the NPC body's half of the geometry seam -- the path-length service and the navmesh
// raycast, on this body's own nav agent.

bool AElysiumNpcBody::QueryRoute(const FElysiumNpcRouteQuery& Query, FElysiumNpcRouteAnswer& Out) const
{
	// A route on this body's own mesh, asked synchronously and measured. The agent is this body's
	// own -- the pathing hull's, stated by `ApplyRetailHull` -- so the nav data is the mesh this body
	// walks.
	//
	// Wave 1 keeps the body `RouteLengthTo` had: no filter is passed, so the query runs under the nav
	// data's DEFAULT filter whatever `Query.PedestrianCostMultiplier` says, and a partial path is no
	// route whatever `Query.bAcceptPartial` says. Lane A makes both follow the query (the filter
	// `MoveTo` builds for the same multiplier), so the route measured is the route walked.
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (NavSys == nullptr || Movement == nullptr)
	{
		return false;
	}
	const FNavAgentProperties& Agent = Movement->NavAgentProps;
	const FVector Feet = GetNavAgentLocation();
	const ANavigationData* NavData = NavSys->GetNavDataForProps(Agent, Feet);
	if (NavData == nullptr)
	{
		return false;
	}
	FNavLocation SelfOnMesh;
	FNavLocation GoalOnMesh;
	if (!NavSys->ProjectPointToNavigation(Feet, SelfOnMesh, INVALID_NAVEXTENT, &Agent)
		|| !NavSys->ProjectPointToNavigation(Query.DestCm, GoalOnMesh, INVALID_NAVEXTENT, &Agent))
	{
		return false;
	}
	FPathFindingQuery PathQuery(this, *NavData, SelfOnMesh.Location, GoalOnMesh.Location);
	PathQuery.SetAllowPartialPaths(false);
	const FPathFindingResult Found = NavSys->FindPathSync(Agent, PathQuery);
	if (!Found.IsSuccessful() || !Found.Path.IsValid() || Found.Path->IsPartial())
	{
		return false;
	}
	Out.bReachable = true;
	Out.bPartial = false;
	Out.LengthCm = static_cast<float>(Found.Path->GetLength());
	return true;
}

bool AElysiumNpcBody::NavRaycast(const FElysiumNpcNavRaycast& Query, FElysiumNpcNavRaycastAnswer& Out) const
{
	// Wave 1 stub: no NavMesh answers yet. Lane A casts on this body's own nav data under the
	// query's filter.
	return false;
}
