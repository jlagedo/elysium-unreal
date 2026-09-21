// Content-free Substrate automation: the export-readiness gate Travel and ExportedMaps share.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumMapSubsystem.h"
#include "Tests/ElysiumScratchContentRoot.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "UObject/UObjectGlobals.h"

static constexpr EAutomationTestFlags GElysiumMapExportGateTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// The marker carries no content the gate reads -- an empty file satisfies it, matching
	// production, where `.ready` is always empty.
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

	// No marker: the refusal Travel logs ("no exported map ... under ...").
	TestFalse(TEXT("a map with no readiness marker is refused"),
		UElysiumMapSubsystem::HasTravelableExport(Map));

	// The marker is the whole gate since 0018 story 21-1 retired the `.obj` arm: the producer
	// writes it once every sidecar `Travel` still depends on is complete on disk.
	TestTrue(TEXT("writing the readiness marker"),
		TouchFile(FElysiumContentPaths::MapExportReady(Map)));
	TestTrue(TEXT("the readiness marker accepts the map"),
		UElysiumMapSubsystem::HasTravelableExport(Map));

	// And removing it refuses again -- the gate reads the marker every time, never a cached answer.
	IFileManager::Get().Delete(*FElysiumContentPaths::MapExportReady(Map));
	TestFalse(TEXT("removing the marker refuses the map again"),
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

#endif // WITH_DEV_AUTOMATION_TESTS
