// The NavMesh spike for the geometry seam (0018 story 6, lane D).
//
// `QueryRoute` and `NavRaycast` forward to `ElysiumWorldGeometry::Route` / `Raycast`, which need a
// baked mesh with tiles and a navigation system to run `FindPathSync` on. This asks the one question
// that decides how those two can be witnessed: does the SHIPPED, baked tutorial level, loaded the
// way the other baked-level tests load it (`LoadObject<UWorld>`, no game running), carry tiles on
// its human mesh, and can a path be found on it?
//
// When it cannot -- no navigation system on a world nobody initialised for play, or a mesh whose
// tiles attach only when the level is added to a running world -- the test reports a SKIP with the
// reason (an info line, not a failure) and returns. Reachability and the closed room then become
// live-only checks (`elysium.geom.*` in a running game).

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/Navigation/NavigationTypes.h"
#include "ElysiumContentPaths.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Map/ElysiumWorldGeometry.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationSystem.h"
#include "Tests/AutomationCommon.h"

static constexpr EAutomationTestFlags GElysiumGeometryNavFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The mesh cut for the human hull. `DefaultEngine.ini`'s generated agent list names it "Human"
	// (HUMAN_HULL, 33.02 cm radius); a map builds only the agents its graph uses, so the mesh is found
	// by that name and never by index.
	ARecastNavMesh* FindHumanMesh(UWorld* World)
	{
		for (TActorIterator<ARecastNavMesh> It(World); It; ++It)
		{
			if (It->GetConfig().Name == FName(TEXT("Human")))
			{
				return *It;
			}
		}
		return nullptr;
	}

	FString NavVectorText(const FVector& Cm)
	{
		return FString::Printf(TEXT("(%.1f %.1f %.1f)"), Cm.X, Cm.Y, Cm.Z);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGeometryNavSpikeTest,
	"Elysium.Content.Geometry.NavSpike", GElysiumGeometryNavFlags)
bool FElysiumGeometryNavSpikeTest::RunTest(const FString&)
{
	const FString Package = FElysiumContentPaths::BakedLevel(TEXT("sp_tutorial_1"));
	UWorld* Baked = LoadObject<UWorld>(nullptr, *(Package + TEXT(".sp_tutorial_1")));
	if (!TestNotNull(TEXT("the tutorial's baked level exists"), Baked))
	{
		return false;
	}
	ARecastNavMesh* Mesh = FindHumanMesh(Baked);
	if (!TestNotNull(TEXT("the baked tutorial carries a RecastNavMesh whose agent is the human"), Mesh))
	{
		return false;
	}

	const int32 Tiles = Mesh->GetNumActiveTiles();
	AddInfo(FString::Printf(TEXT("nav spike: human mesh '%s' holds %d active tiles"),
		*Mesh->GetName(), Tiles));
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Baked);
	if (Tiles == 0 || Nav == nullptr)
	{
		AddInfo(FString::Printf(TEXT("SKIP: reachability and the nav raycast are not witnessed outside a "
			"running game -- %s. Route and Raycast stay live-only checks."),
			Nav == nullptr
				? TEXT("the loaded level has no navigation system (nobody initialised it for play), "
					"so FindPathSync has nothing to run on")
				: TEXT("the loaded mesh has no active tiles until its level joins a running world")));
		return true;
	}

	const FSharedConstNavQueryFilter Filter = Mesh->GetDefaultQueryFilter();
	const FNavLocation From = Mesh->GetRandomPoint(Filter);
	if (!TestTrue(TEXT("the mesh yields a point on itself"), From.HasNodeRef()))
	{
		return false;
	}
	// A destination reachable by construction (a flood from `From`), so a failed route is the query's.
	FNavLocation To;
	if (!TestTrue(TEXT("the mesh yields a reachable point 20 m away"),
		Mesh->GetRandomReachablePointInRadius(From.Location, 2000.0f, To, Filter)))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("nav spike: route %s -> %s"), *NavVectorText(From.Location),
		*NavVectorText(To.Location)));

	FElysiumNpcRouteQuery RouteQuery;
	RouteQuery.DestCm = To.Location;
	FElysiumNpcRouteAnswer Route;
	const bool bRouted =
		ElysiumWorldGeometry::Route(*Mesh, Mesh->GetConfig(), Filter, From.Location, RouteQuery, Route);
	// False here with tiles and a navigation system present is a real failure -- or lane A's `Route`
	// body still a stub, which answers false.
	TestTrue(TEXT("Route answers a reachable pair"), bRouted && Route.bReachable);
	TestTrue(*FString::Printf(TEXT("the route is at least as long as the straight line (%.1f cm)"),
		Route.LengthCm), Route.LengthCm + 1.0f >= FVector::Dist(From.Location, To.Location));

	// The corridor: to a point 1.5 m away on the same island. A zero-length ray never leaves the mesh.
	FNavLocation Near;
	if (TestTrue(TEXT("the mesh yields a point 1.5 m from the start"),
		Mesh->GetRandomReachablePointInRadius(From.Location, 150.0f, Near, Filter)))
	{
		FElysiumNpcNavRaycast Ray;
		Ray.FromCm = From.Location;
		Ray.ToCm = Near.Location;
		FElysiumNpcNavRaycastAnswer Hit;
		TestTrue(TEXT("Raycast answers along a corridor"),
			ElysiumWorldGeometry::Raycast(*Mesh, Filter, Ray, Hit));
		AddInfo(FString::Printf(TEXT("nav spike: raycast to %s hit=%s"), *NavVectorText(Near.Location),
			Hit.bHit ? *NavVectorText(Hit.HitCm) : TEXT("no")));

		Ray.ToCm = From.Location;
		FElysiumNpcNavRaycastAnswer Self;
		TestTrue(TEXT("Raycast answers a zero-length ray"),
			ElysiumWorldGeometry::Raycast(*Mesh, Filter, Ray, Self));
		TestFalse(TEXT("a zero-length ray never leaves the mesh"), Self.bHit);
	}
	return true;
}

#endif
