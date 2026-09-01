// Content-free Substrate coverage for `UElysiumSkyBakeLibrary::BakeSkyCubeAsset` (R5.2,
// `docs/architecture/seam_map_map_lighting.md` -> "## Import" -> "Sky baked (R5.2)"): only the
// two failure paths that need no staged corpus. The success path — six real
// `shared/tex/skybox_<name><face>.png` faces joining into a real cube — is exercised by the
// same `ElysiumEnvironment::BuildSkyCubeFrom` this library calls, and by the pipeline's own
// bake run against the three working maps; nothing here re-tests that pixel math.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumSkyBakeLibrary.h"

#include "Engine/TextureCube.h"

static constexpr EAutomationTestFlags GElysiumSkyBakeLibraryTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// A sky name with no corpus faces on disk fails the same way the runtime's own
// HasSkyFaces/BuildSkyCubeFrom do: null, no crash, OutUpperMean left at 0 — never a divide
// against a cube that was never built.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSkyBakeLibraryMissingFacesTest,
	"Elysium.Substrate.SkyBake.MissingFacesReturnsNull", GElysiumSkyBakeLibraryTestFlags)
bool FElysiumSkyBakeLibraryMissingFacesTest::RunTest(const FString&)
{
	float UpperMean = -1.f;
	UTextureCube* Cube = UElysiumSkyBakeLibrary::BakeSkyCubeAsset(
		TEXT("__elysium_test_no_such_sky__"),
		TEXT("/Temp/ElysiumSkyBakeLibraryTests/TC_Sky___elysium_test_no_such_sky__"), UpperMean);
	TestNull(TEXT("no cube built for a sky with no corpus faces"), Cube);
	TestEqual(TEXT("upper mean reset to zero on failure"), UpperMean, 0.f);
	return true;
}

// A package path with no '/' cannot name a package at all; the function has to fail before it
// ever reads a face off disk rather than crashing on the malformed path.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSkyBakeLibraryBadPackagePathTest,
	"Elysium.Substrate.SkyBake.BadPackagePathReturnsNull", GElysiumSkyBakeLibraryTestFlags)
bool FElysiumSkyBakeLibraryBadPackagePathTest::RunTest(const FString&)
{
	float UpperMean = -1.f;
	UTextureCube* Cube = UElysiumSkyBakeLibrary::BakeSkyCubeAsset(TEXT("la"), TEXT("NoSlashesHere"),
		UpperMean);
	TestNull(TEXT("no cube built for a malformed package path"), Cube);
	TestEqual(TEXT("upper mean reset to zero on failure"), UpperMean, 0.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
