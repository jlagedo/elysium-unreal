// Job 6's two marks, asked of the baked levels: a priced roadway and a cut doorway.
//
// Retail's door rule is a mask fact -- the graph-build mask `0x2000b` is the only movement mask
// without `MOVEABLE 0x4000`, so `InitLinks` builds through a standing door while every run-time
// probe finds it solid. A door the designers gave a link is one NPCs use; a door with no link is
// a wall, and Unreal has no such distinction to inherit. These are the witnesses that the bake
// made it.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumNavAreaActor.h"
#include "ElysiumNavAreas.h"
#include "ElysiumNavBakeLibrary.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Map/ElysiumNavQueryFilter_Pedestrian.h"
#include "NavAreas/NavArea_Default.h"
#include "NavMesh/RecastNavMesh.h"
#include "Tests/AutomationCommon.h"

static constexpr EAutomationTestFlags GElysiumNavAreaFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	AElysiumNavAreaActor* FindMarks(UWorld* World)
	{
		for (TActorIterator<AElysiumNavAreaActor> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	int32 ConvexesWearing(AElysiumNavAreaActor* Actor, UClass* Area)
	{
		int32 Count = 0;
		for (UElysiumNavAreaComponent* Component : Actor->Areas)
		{
			if (Component != nullptr && Component->AreaClass == Area)
			{
				Count += Component->Convexes.Num();
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNavAreaHubTest,
	"Elysium.Content.NavArea.Hub", GElysiumNavAreaFlags)
bool FElysiumNavAreaHubTest::RunTest(const FString&)
{
	const FString Package = FElysiumContentPaths::BakedLevel(TEXT("sm_hub_1"));
	UWorld* Baked = LoadObject<UWorld>(nullptr, *(Package + TEXT(".sm_hub_1")));
	if (!TestNotNull(TEXT("the hub's baked level exists"), Baked)) return false;
	AElysiumNavAreaActor* Marks = FindMarks(Baked);
	if (!TestNotNull(TEXT("the hub carries its nav-area marks"), Marks)) return false;
	TestEqual(TEXT("the marks name their map"), Marks->MapName, FString(TEXT("sm_hub_1")));

	// The 9 clip-free `0x2000` brushes -- `---p`, `0x2000` with no clip bit. The 1,881 brushes
	// that also carry a clip bit are NOT priced: a clip brush stops `InitLinks`' own walk test, so
	// no ground link is ever built across it and its `0x2000` never reaches a link.
	TestEqual(TEXT("nine roadway slabs are priced"),
		ConvexesWearing(Marks, UElysiumNavArea_Pedestrian::StaticClass()), 9);

	// 29 doors, 2 of which -- the smoke-shop pair -- carry a link and keep their opening for
	// story 7's smart link. The other 27 are walls, and they are 52 convexes rather than 27: a
	// door entity is several brushes (leaf, frame, a double door's other half), and the mark is
	// made from the same hulls its collision body is cooked from. The door COUNT is pinned where
	// it is derived, over the staged rows, in `test_map_nav_doors.py`.
	TestEqual(TEXT("the 27 cut doors are 52 convexes"),
		ConvexesWearing(Marks, UElysiumNavArea_DoorCut::StaticClass()), 52);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNavAreaTutorialTest,
	"Elysium.Content.NavArea.Tutorial", GElysiumNavAreaFlags)
bool FElysiumNavAreaTutorialTest::RunTest(const FString&)
{
	const FString Package = FElysiumContentPaths::BakedLevel(TEXT("sp_tutorial_1"));
	UWorld* Baked = LoadObject<UWorld>(nullptr, *(Package + TEXT(".sp_tutorial_1")));
	if (!TestNotNull(TEXT("the tutorial's baked level exists"), Baked)) return false;
	AElysiumNavAreaActor* Marks = FindMarks(Baked);
	if (!TestNotNull(TEXT("the tutorial carries its nav-area marks"), Marks)) return false;

	// No roadway indoors: the tutorial has none of the 47 clip-free `0x2000` brushes, and an
	// empty mark would be a filter that prices nothing rather than an absent one.
	TestEqual(TEXT("the tutorial prices no roadway"),
		ConvexesWearing(Marks, UElysiumNavArea_Pedestrian::StaticClass()), 0);

	// 36 doors, 8 of which the graph runs through -- the designers' own choice of which encounters
	// can reach the player, which is why the other 28 become walls rather than all 36 or none.
	// 28 doors, 44 convexes: a door entity is several brushes.
	TestEqual(TEXT("the 28 cut doors are 44 convexes"),
		ConvexesWearing(Marks, UElysiumNavArea_DoorCut::StaticClass()), 44);
	return true;
}

// 0018 story 5: the pedestrian query filter's factory. Retail's A* `FUN_102fe9f0` prices a flagged
// link at `cost * RandomInt(5, 10)` for a pedestrian search only (`FUN_103055b0` passes
// `param_3 == 0` for everyone else), so the factory answers the default filter for multiplier 0 and
// otherwise a per-request copy whose pedestrian-area travel cost is the multiplier.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumNavAreaPedestrianFilterTest,
	"Elysium.Content.NavArea.PedestrianFilter", GElysiumNavAreaFlags)
bool FElysiumNavAreaPedestrianFilterTest::RunTest(const FString&)
{
	FTestWorldWrapper TestWorld;
	if (!TestWorld.CreateTestWorld(EWorldType::Game))
	{
		TestWorld.ForwardErrorMessages(this);
		return false;
	}
	UWorld* World = TestWorld.GetTestWorld();
	ARecastNavMesh* Mesh = World ? World->SpawnActor<ARecastNavMesh>() : nullptr;
	if (!TestNotNull(TEXT("a nav mesh spawns to ask"), Mesh)) return false;

	// A navigation system registers each area with its nav data; a bare test world has none, so do
	// what registration does for the two areas this test reads.
	Mesh->OnNavAreaAdded(UNavArea_Default::StaticClass(), 0);
	Mesh->OnNavAreaAdded(UElysiumNavArea_Pedestrian::StaticClass(), 0);
	const int32 PedestrianId = Mesh->GetAreaID(UElysiumNavArea_Pedestrian::StaticClass());
	const int32 DefaultId = Mesh->GetAreaID(UNavArea_Default::StaticClass());
	if (!TestTrue(TEXT("the mesh carries both areas"), PedestrianId != INDEX_NONE && DefaultId != INDEX_NONE)
		|| !TestNotEqual(TEXT("the two areas have different ids"), PedestrianId, DefaultId))
	{
		return false;
	}

	// Multiplier 0 is a non-pedestrian search: the nav data's own default filter, not a copy.
	const FSharedConstNavQueryFilter Zero =
		ElysiumNavQueryFilterPedestrian::MakeFilter(*Mesh, nullptr, 0);
	TestTrue(TEXT("multiplier 0 answers the default filter"),
		Zero.Get() == Mesh->GetDefaultQueryFilter().Get());

	auto CostsOf = [](const FSharedConstNavQueryFilter& Filter, int32 AreaId, float& OutCost)
	{
		float Costs[256];
		float Fixed[256];
		for (int32 Index = 0; Index < 256; ++Index)
		{
			Costs[Index] = -1.0f;
			Fixed[Index] = -1.0f;
		}
		Filter->GetAllAreaCosts(Costs, Fixed, 256);
		OutCost = Costs[AreaId];
	};

	float DefaultBefore = 0.0f;
	float PedestrianBefore = 0.0f;
	CostsOf(Mesh->GetDefaultQueryFilter(), DefaultId, DefaultBefore);
	CostsOf(Mesh->GetDefaultQueryFilter(), PedestrianId, PedestrianBefore);

	const FSharedConstNavQueryFilter Seven =
		ElysiumNavQueryFilterPedestrian::MakeFilter(*Mesh, nullptr, 7);
	if (!TestTrue(TEXT("multiplier 7 answers a filter"), Seven.IsValid())) return false;
	TestTrue(TEXT("and it is not the default filter"), Seven.Get() != Mesh->GetDefaultQueryFilter().Get());
	float SevenPedestrian = 0.0f;
	float SevenDefault = 0.0f;
	CostsOf(Seven, PedestrianId, SevenPedestrian);
	CostsOf(Seven, DefaultId, SevenDefault);
	TestEqual(TEXT("the pedestrian area costs 7"), SevenPedestrian, 7.0f);
	TestEqual(TEXT("the default area's cost is unchanged"), SevenDefault, DefaultBefore);

	// Independent copies: pricing one never reaches another, the cached class filter, or the
	// nav data's default filter.
	const FSharedConstNavQueryFilter Nine =
		ElysiumNavQueryFilterPedestrian::MakeFilter(*Mesh, nullptr, 9);
	TestTrue(TEXT("two calls are two filters"), Nine.Get() != Seven.Get());
	float NinePedestrian = 0.0f;
	float SevenAgain = 0.0f;
	float PedestrianAfter = 0.0f;
	CostsOf(Nine, PedestrianId, NinePedestrian);
	CostsOf(Seven, PedestrianId, SevenAgain);
	CostsOf(Mesh->GetDefaultQueryFilter(), PedestrianId, PedestrianAfter);
	TestEqual(TEXT("the second copy costs 9"), NinePedestrian, 9.0f);
	TestEqual(TEXT("the first copy still costs 7"), SevenAgain, 7.0f);
	TestEqual(TEXT("the default filter's pedestrian cost is untouched"),
		PedestrianAfter, PedestrianBefore);
	return true;
}

#endif
