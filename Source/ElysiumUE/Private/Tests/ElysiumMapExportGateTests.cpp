// Content-free Substrate automation: the export-readiness gate Travel and ExportedMaps share.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumMapTransportSettings.h"
#include "Tests/ElysiumScratchContentRoot.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "UObject/UObjectGlobals.h"

static constexpr EAutomationTestFlags GElysiumMapExportGateTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// Neither artifact carries content the gate reads -- an empty file satisfies either one, matching
	// production (`.ready` is always empty; `.obj` is only ever checked for existence here).
	bool TouchFile(const FString& Path)
	{
		return FFileHelper::SaveStringToFile(FString(), *Path);
	}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapExportGateTest,
	"Elysium.Substrate.MapExportGate", GElysiumMapExportGateTestFlags)
bool FElysiumMapExportGateTest::RunTest(const FString&)
{
	FElysiumScratchContentRoot Scratch(TEXT("MapExportGate"));
	if (!TestTrue(TEXT("the scratch content root installed"), Scratch.IsInstalled()))
	{
		return false;
	}

	static const FString Map(TEXT("sm_gatetest_1"));
	IFileManager::Get().MakeDirectory(*FElysiumContentPaths::MapDir(Map), /*Tree*/ true);

	// Neither artifact present: the refusal Travel logs today ("no exported map ... under ...").
	TestFalse(TEXT("neither the legacy .obj nor the readiness marker refuses the map"),
		UElysiumMapSubsystem::HasTravelableExport(Map));

	// The legacy exporter's .obj alone -- today's only artifact, and the one every already-exported
	// map carries.
	TestTrue(TEXT("writing the .obj"), TouchFile(FElysiumContentPaths::MapObj(Map)));
	TestTrue(TEXT("the legacy .obj alone accepts the map"),
		UElysiumMapSubsystem::HasTravelableExport(Map));

	// Remove it and prove the new lane's marker accepts on its own -- the artifact R3.2 will emit,
	// with nothing else on disk for this map yet.
	IFileManager::Get().Delete(*FElysiumContentPaths::MapObj(Map));
	TestFalse(TEXT("removing the .obj refuses again"),
		UElysiumMapSubsystem::HasTravelableExport(Map));

	TestTrue(TEXT("writing the readiness marker"),
		TouchFile(FElysiumContentPaths::MapExportReady(Map)));
	TestTrue(TEXT("the readiness marker alone accepts the map, with no .obj present"),
		UElysiumMapSubsystem::HasTravelableExport(Map));

	// Both present (the state a map sits in mid-cutover, R3.2 through R5.1) still accepts -- the gate
	// is an OR, never a required pair.
	TestTrue(TEXT("re-writing the .obj alongside the marker"), TouchFile(FElysiumContentPaths::MapObj(Map)));
	TestTrue(TEXT("both artifacts together still accept the map"),
		UElysiumMapSubsystem::HasTravelableExport(Map));

	return true;
}

// The marker is a presence-only signal, read for content, only for presence. Two ways that could be gotten
// wrong without either being caught by the first test: reading the bytes at all, and treating a
// same-named directory as if it were the file.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapExportGateMarkerIsPresenceOnlyTest,
	"Elysium.Substrate.MapExportGateMarkerIsPresenceOnly", GElysiumMapExportGateTestFlags)
bool FElysiumMapExportGateMarkerIsPresenceOnlyTest::RunTest(const FString&)
{
	FElysiumScratchContentRoot Scratch(TEXT("MapExportGateArtifactIsFileOnly"));
	if (!TestTrue(TEXT("the scratch content root installed"), Scratch.IsInstalled()))
	{
		return false;
	}

	static const FString Map(TEXT("sm_gatetest_2"));
	IFileManager::Get().MakeDirectory(*FElysiumContentPaths::MapDir(Map), /*Tree*/ true);

	// The marker is a presence-only signal: its bytes are
	// never read, so writing a marker with content still satisfies the gate the same as an empty one.
	TestTrue(TEXT("writing a non-empty readiness marker"),
		FFileHelper::SaveStringToFile(TEXT("not read"), *FElysiumContentPaths::MapExportReady(Map)));
	TestTrue(TEXT("a non-empty marker accepts the map exactly like an empty one"),
		UElysiumMapSubsystem::HasTravelableExport(Map));

	// A same-named directory (not a file) at the marker's path must not satisfy the gate -- the check
	// is FPaths::FileExists, not a bare stat.
	IFileManager::Get().Delete(*FElysiumContentPaths::MapExportReady(Map));
	static const FString OtherMap(TEXT("sm_gatetest_3"));
	IFileManager::Get().MakeDirectory(*FElysiumContentPaths::MapExportReady(OtherMap), /*Tree*/ true);
	TestFalse(TEXT("a directory at the marker's path does not satisfy the gate"),
		UElysiumMapSubsystem::HasTravelableExport(OtherMap));

	return true;
}

// R5.1 narrows the `.obj` half of the gate: once a map is on `MapsOnV2Models` nothing reads its
// `.obj`, so a stale one left on disk from an older export must not vouch for the sidecars beside
// it — Travel's gate follows the same flag. The marker still accepts on either lane -- the branch is about
// which artifact is EVIDENCE, not about which maps may travel.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapExportGateObjOnlyCountsOffTheV2LaneTest,
	"Elysium.Substrate.MapExportGateObjOnlyCountsOffTheV2Lane", GElysiumMapExportGateTestFlags)
bool FElysiumMapExportGateObjOnlyCountsOffTheV2LaneTest::RunTest(const FString&)
{
	FElysiumScratchContentRoot Scratch(TEXT("MapExportGateObjOnlyCountsOffTheV2Lane"));
	if (!TestTrue(TEXT("the scratch content root installed"), Scratch.IsInstalled()))
	{
		return false;
	}

	static const FString Map(TEXT("sm_gatetest_4"));
	IFileManager::Get().MakeDirectory(*FElysiumContentPaths::MapDir(Map), /*Tree*/ true);
	TestTrue(TEXT("writing the .obj"), TouchFile(FElysiumContentPaths::MapObj(Map)));
	TestTrue(TEXT("off the V2 lane, the legacy .obj alone accepts the map"),
		UElysiumMapSubsystem::HasTravelableExport(Map));

	UElysiumMapTransportSettings* Settings = GetMutableDefault<UElysiumMapTransportSettings>();
	const TArray<FName> Was = Settings->MapsOnV2Models;
	Settings->MapsOnV2Models.Add(FName(*Map));

	TestFalse(TEXT("on the V2 lane, a stale .obj alone no longer accepts the map"),
		UElysiumMapSubsystem::HasTravelableExport(Map));
	TestTrue(TEXT("writing the readiness marker"),
		TouchFile(FElysiumContentPaths::MapExportReady(Map)));
	TestTrue(TEXT("on the V2 lane, the readiness marker accepts the map"),
		UElysiumMapSubsystem::HasTravelableExport(Map));

	Settings->MapsOnV2Models = Was;
	TestTrue(TEXT("back off the V2 lane, the map still travels"),
		UElysiumMapSubsystem::HasTravelableExport(Map));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
