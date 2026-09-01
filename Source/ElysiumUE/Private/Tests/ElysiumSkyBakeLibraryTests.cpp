// Substrate coverage for `UElysiumSkyBakeLibrary::BakeSkyCubeAsset` (R5.2,
// `docs/architecture/seam_map_map_lighting.md` -> "## Import" -> "Sky baked (R5.2)"): the two
// content-free failure paths, plus (content-gated) the one thing those cannot show — that the
// baked cube is not empty. `ElysiumEnvironment::BuildSkyCubeFrom` already covers the pixel math
// (rotation table, upper-hemisphere mean) against the runtime's own transient cube; what this
// library adds on top is a PERSISTENT asset, and a persistent, uncooked package serializes
// `UTexture::Source`, not `FTexturePlatformData` — a bug there is invisible to any check that
// only looks at the live object inside the baking commandlet, which is why the corpus-gated test
// below inspects `Source` specifically rather than re-deriving the pixels.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumEnvironment.h"
#include "ElysiumSkyBakeLibrary.h"

#include "Engine/TextureCube.h"
#include "UObject/Package.h"

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

// The regression this file exists to catch: a persistent bake with real corpus faces must leave
// the saved asset's `Source` populated (an uncooked package serializes `Source`, never the live
// `FTexturePlatformData` this same call also fills) -- an empty `Source` is exactly what shipped
// silently the first time, because every earlier check here only ever looked at the live object
// inside the same commandlet that built it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSkyBakeLibraryPersistentSourceTest,
	"Elysium.Content.SkyBake.PersistentAssetHasSource", GElysiumSkyBakeLibraryTestFlags)
bool FElysiumSkyBakeLibraryPersistentSourceTest::RunTest(const FString&)
{
	if (!FElysiumContentPaths::IsConfigured())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no export root configured"));
		return true;
	}
	const FString SkyName = TEXT("la");
	const FString Prefix = FElysiumContentPaths::SkyFacePrefix(SkyName);
	if (!ElysiumEnvironment::HasSkyFaces(FElysiumContentPaths::SharedTexDir(), Prefix))
	{
		AddInfo(FString::Printf(
			TEXT("ELYSIUM_TEST_ABSTAIN: no '%s' sky faces under %s"), *Prefix,
			*FElysiumContentPaths::SharedTexDir()));
		return true;
	}

	float UpperMean = -1.f;
	UTextureCube* Cube = UElysiumSkyBakeLibrary::BakeSkyCubeAsset(SkyName,
		TEXT("/Temp/ElysiumSkyBakeLibraryTests/TC_Sky_la_PersistentSourceTest"), UpperMean);
	if (!TestNotNull(TEXT("a real sky's faces bake to a real cube"), Cube))
	{
		return false;
	}
	TestTrue(TEXT("upper-hemisphere mean is a positive radiance"), UpperMean > 0.f);

	// The check the review's own evidence names: `IsSourceValid()` false and `0x0x6` dimensions
	// is precisely what an empty `Source` on a saved package reads as.
	TestTrue(TEXT("the persistent cube's Source is valid"), Cube->Source.IsValid());
	TestEqual(TEXT("Source width matches the decoded face size"),
		int64(Cube->Source.GetSizeX()), int64(Cube->GetSizeX()));
	TestEqual(TEXT("Source height matches the decoded face size"),
		int64(Cube->Source.GetSizeY()), int64(Cube->GetSizeY()));
	TestEqual(TEXT("Source carries all six cube faces as slices"), Cube->Source.GetNumSlices(), 6);
	TestTrue(TEXT("Source format is the BGRA8 the faces were decoded into"),
		Cube->Source.GetFormat() == TSF_BGRA8);
	TestNotNull(TEXT("the baked cube has an owning package"), Cube->GetPackage());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
