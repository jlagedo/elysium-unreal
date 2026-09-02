// Coverage for `UElysiumMapBakeLibrary` (R5.5, `docs/architecture/seam_map_map.md` ->
// "## Import -- reflection captures (R5.5)"): the content-free failure paths of the two library
// calls, and (content-gated) the one thing those cannot show -- that every converted map's baked
// level actually carries a rendered cube per placed capture in its `_BuiltData` registry. A
// capture that placed but never built is invisible to an actor census (the image lives in the
// registry, not on the actor), which is why the corpus-gated test below asks the registry.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumBakedTags.h"
#include "ElysiumContentPaths.h"
#include "ElysiumMapBakeLibrary.h"
#include "ElysiumMapTransportSettings.h"

#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

static constexpr EAutomationTestFlags GElysiumMapBakeLibraryTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// No world: the build reports that it could not run (-1, distinct from "ran and built nothing")
// and the count reports nothing found, with no crash on either.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapBakeLibraryNullWorldTest,
	"Elysium.Substrate.MapBake.NullWorldBuildsNothing", GElysiumMapBakeLibraryTestFlags)
bool FElysiumMapBakeLibraryNullWorldTest::RunTest(const FString&)
{
	TestEqual(TEXT("a null world cannot be built"),
		UElysiumMapBakeLibrary::BuildReflectionCaptures(nullptr), -1);
	int32 Components = 7;
	TestEqual(TEXT("a null world counts no built captures"),
		UElysiumMapBakeLibrary::CountBuiltReflectionCaptures(nullptr, Components), 0);
	TestEqual(TEXT("a null world counts no capture components"), Components, 0);
	return true;
}

// A world with no capture at all counts 0 / 0 -- "none placed" -- rather than tripping on the
// absent registry a never-built level has.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapBakeLibraryEmptyWorldTest,
	"Elysium.Substrate.MapBake.EmptyWorldCountsNone", GElysiumMapBakeLibraryTestFlags)
bool FElysiumMapBakeLibraryEmptyWorldTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ElysiumMapBakeLibraryEmpty"));
	if (!TestNotNull(TEXT("scratch world"), World))
	{
		return false;
	}
	int32 Components = 7;
	TestEqual(TEXT("no capture component, nothing built"),
		UElysiumMapBakeLibrary::CountBuiltReflectionCaptures(World, Components), 0);
	TestEqual(TEXT("no capture component found"), Components, 0);
	World->DestroyWorld(false);
	return true;
}

// The bake's own assertion, re-asked of the saved package: for every `MapsOnV2Models` map whose
// level is baked, each `elysium.capture` actor's capture component resolves to a registry entry
// with a rendered cube. Abstains per map that is not baked on this machine; a converted map with
// captures placed and none built is the regression this exists to catch.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapBakeLibraryCapturesBuiltTest,
	"Elysium.Content.MapBake.ReflectionCapturesBuilt", GElysiumMapBakeLibraryTestFlags)
bool FElysiumMapBakeLibraryCapturesBuiltTest::RunTest(const FString&)
{
	const UElysiumMapTransportSettings* Settings = GetDefault<UElysiumMapTransportSettings>();
	if (Settings == nullptr || Settings->MapsOnV2Models.Num() == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no map is on the V2 lane"));
		return true;
	}
	int32 Checked = 0;
	for (const FName& MapName : Settings->MapsOnV2Models)
	{
		const FString PackageName = FElysiumContentPaths::BakedLevel(MapName.ToString());
		if (!FPackageName::DoesPackageExist(PackageName))
		{
			AddInfo(FString::Printf(TEXT("ELYSIUM_TEST_ABSTAIN: %s is not baked"), *PackageName));
			continue;
		}
		UPackage* Package = LoadPackage(nullptr, *PackageName, LOAD_None);
		UWorld* World = Package ? UWorld::FindWorldInPackage(Package) : nullptr;
		if (!TestNotNull(*FString::Printf(TEXT("%s loads as a world"), *PackageName), World))
		{
			continue;
		}
		int32 Placed = 0;
		for (const AActor* Actor : World->PersistentLevel->Actors)
		{
			Placed += (Actor != nullptr && Actor->ActorHasTag(ElysiumBakedTags::Capture)) ? 1 : 0;
		}
		int32 Components = 0;
		const int32 Built = UElysiumMapBakeLibrary::CountBuiltReflectionCaptures(World, Components);
		TestTrue(*FString::Printf(TEXT("%s places at least one capture"), *PackageName), Placed > 0);
		TestEqual(*FString::Printf(TEXT("%s: one capture component per placed actor"), *PackageName),
			Components, Placed);
		TestEqual(*FString::Printf(TEXT("%s: every placed capture carries a rendered cube"),
			*PackageName), Built, Placed);
		++Checked;
	}
	if (Checked == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no V2 map is baked on this machine"));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
