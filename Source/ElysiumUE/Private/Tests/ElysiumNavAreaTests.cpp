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

#endif
