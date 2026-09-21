#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ElysiumContentPaths.h"
#include "ElysiumMapSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Misc/PackageName.h"

// 0018 story 21-3: the travel gate and the map list ask the project.
//
// `HasBakedMap` is a predicate over four packages -- the level and the three `DA_<map>_*` assets --
// so it can only be asked of real content; the scratch content root the retired
// `ElysiumMapExportGateTests` drove the `.ready` marker with cannot fabricate a package. This test
// therefore runs against the mount itself: every map the gate accepts carries all four, and a level
// that carries only its `.umap` is refused. The mount holds ~100 such stale bare levels, baked
// before the three assets existed, so the second half is not hypothetical.
//
// Abstains with no baked maps on the mount, like the other Content tests.

static constexpr EAutomationTestFlags GElysiumMapListFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace ElysiumMapListTests
{
	// Every `<map>/<map>` level package under `/ElysiumBaked`, whether or not it is baked whole.
	TArray<FString> LevelsOnMount()
	{
		TArray<FString> Maps;
		IAssetRegistry* Registry = IAssetRegistry::Get();
		if (Registry == nullptr)
		{
			return Maps;
		}
		const FString Mount = FElysiumContentPaths::BakedMount();
		Registry->ScanPathsSynchronous({ Mount }, /*bForceRescan*/ false);
		FARFilter Filter;
		Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("World")));
		Filter.PackagePaths.Add(FName(*Mount));
		Filter.bRecursivePaths = true;
		TArray<FAssetData> Assets;
		Registry->GetAssets(Filter, Assets);
		for (const FAssetData& Asset : Assets)
		{
			const FString Name = Asset.AssetName.ToString();
			// A map's level is named for its own folder; anything else under the mount is not one.
			if (Asset.PackagePath.ToString() == Mount / Name)
			{
				Maps.AddUnique(Name);
			}
		}
		Maps.Sort();
		return Maps;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapListTest, "Elysium.Content.MapList",
	GElysiumMapListFlags)
bool FElysiumMapListTest::RunTest(const FString&)
{
	const TArray<FString> Levels = ElysiumMapListTests::LevelsOnMount();
	TArray<FString> Whole;
	TArray<FString> Bare;
	for (const FString& Map : Levels)
	{
		(UElysiumMapSubsystem::HasBakedMap(Map) ? Whole : Bare).Add(Map);
	}

	if (Whole.Num() == 0)
	{
		AddInfo(FString::Printf(
			TEXT("skipping: no map under %s is baked whole (run: uv run elysium bake map --maps <map>)"),
			*FElysiumContentPaths::BakedMount()));
		return true;
	}
	AddInfo(FString::Printf(TEXT("%d level(s) on the mount: %d baked whole, %d bare"),
		Levels.Num(), Whole.Num(), Bare.Num()));

	// The gate's own claim, asked of each half directly: a map it accepts really does carry all
	// four packages, and one it refuses is missing at least one.
	for (const FString& Map : Whole)
	{
		TestTrue(*FString::Printf(TEXT("%s: the level package exists"), *Map),
			FPackageName::DoesPackageExist(FElysiumContentPaths::BakedLevel(Map)));
		for (const FString& ObjectPath : {
				FElysiumContentPaths::BakedMapEntities(Map),
				FElysiumContentPaths::BakedMapEnvironment(Map),
				FElysiumContentPaths::BakedMapCollision(Map) })
		{
			TestTrue(*FString::Printf(TEXT("%s: %s exists"), *Map, *ObjectPath),
				FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(ObjectPath)));
		}
	}
	for (const FString& Map : Bare)
	{
		const bool bAllThree =
			FPackageName::DoesPackageExist(
				FPackageName::ObjectPathToPackageName(FElysiumContentPaths::BakedMapEntities(Map)))
			&& FPackageName::DoesPackageExist(
				FPackageName::ObjectPathToPackageName(FElysiumContentPaths::BakedMapEnvironment(Map)))
			&& FPackageName::DoesPackageExist(
				FPackageName::ObjectPathToPackageName(FElysiumContentPaths::BakedMapCollision(Map)));
		TestFalse(*FString::Printf(TEXT("%s is refused because an asset is missing"), *Map), bAllThree);
	}

	// A name that is no map at all folds to no level path and must not be accepted.
	TestFalse(TEXT("an unbaked name is refused"),
		UElysiumMapSubsystem::HasBakedMap(TEXT("sm_no_such_map_1")));
	TestFalse(TEXT("an empty name is refused"), UElysiumMapSubsystem::HasBakedMap(FString()));
	TestFalse(TEXT("a path traversal is refused"),
		UElysiumMapSubsystem::HasBakedMap(TEXT("../Textures")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapListMatchesGateTest,
	"Elysium.Content.MapListMatchesGate", GElysiumMapListFlags)
bool FElysiumMapListMatchesGateTest::RunTest(const FString&)
{
	// The list and the gate must never disagree, which is the whole reason `BakedMaps` routes
	// through `HasBakedMap`. Build the expected answer straight off the mount and compare.
	TArray<FString> Expected;
	for (const FString& Map : ElysiumMapListTests::LevelsOnMount())
	{
		if (UElysiumMapSubsystem::HasBakedMap(Map))
		{
			Expected.AddUnique(Map);
		}
	}
	if (Expected.Num() == 0)
	{
		AddInfo(TEXT("skipping: no map on the mount is baked whole"));
		return true;
	}
	Expected.Sort();

	// `BakedMaps` is the subsystem's, so it needs a game instance. An editor run without one
	// abstains rather than passing on a list it never asked for.
	UElysiumMapSubsystem* Maps = nullptr;
	if (GEngine != nullptr)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.OwningGameInstance != nullptr)
			{
				Maps = Context.OwningGameInstance->GetSubsystem<UElysiumMapSubsystem>();
				if (Maps != nullptr)
				{
					break;
				}
			}
		}
	}
	if (Maps == nullptr)
	{
		AddInfo(TEXT("skipping the list half: no game instance in this run"));
		return true;
	}
	UElysiumMapSubsystem::InvalidateBakedMaps();
	const TArray<FString> Listed = Maps->BakedMaps();
	TestEqual(TEXT("the baked map list is exactly the set the gate accepts"),
		Listed.Num(), Expected.Num());
	for (int32 Index = 0; Index < FMath::Min(Listed.Num(), Expected.Num()); ++Index)
	{
		TestEqual(TEXT("in sorted order"), Listed[Index], Expected[Index]);
	}
	return true;
}

#endif
