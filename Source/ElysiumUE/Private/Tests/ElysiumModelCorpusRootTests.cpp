// Content-free Substrate automation for R5.1's model-corpus flip, the per-map
// cutover flag. Two things can silently undo that flip: the second cutover list quietly becoming
// an alias of the R4.6 one, and a path accessor stopping composing from `BakedMeshesFor`. Neither
// shows up as a compile error and neither shows up in a shot -- a map just draws the other corpus's
// props, or none.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumMapTransportSettings.h"

#include "UObject/UObjectGlobals.h"

static constexpr EAutomationTestFlags GElysiumModelCorpusRootTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// The two lists are independent. `sp_theatre` is the live reason: it is on the R4.6 entity/
// collision/environment transport, but the R1 model import is map-scoped and has not staged its
// models, so a resolver that reused `MapsOnNewTransport` would point its props at an empty root.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelCorpusRootFlagIsItsOwnListTest,
	"Elysium.Substrate.ModelCorpusRoot.FlagIsItsOwnList", GElysiumModelCorpusRootTestFlags)
bool FElysiumModelCorpusRootFlagIsItsOwnListTest::RunTest(const FString&)
{
	// NewObject inherits the CDO's config-loaded arrays, so both are made explicit here.
	UElysiumMapTransportSettings* Settings = NewObject<UElysiumMapTransportSettings>();
	Settings->MapsOnNewTransport = { FName(TEXT("sp_theatre")), FName(TEXT("sp_tutorial_1")) };
	Settings->MapsOnV2Models = { FName(TEXT("sp_tutorial_1")) };

	TestTrue(TEXT("a map on both lists resolves to the V2 model corpus"),
		ElysiumMapTransport::IsMapOnV2Models(TEXT("sp_tutorial_1"), *Settings));
	TestTrue(TEXT("a listed map resolves case-insensitively"),
		ElysiumMapTransport::IsMapOnV2Models(TEXT("SP_Tutorial_1"), *Settings));
	TestFalse(TEXT("a map on the entity transport alone stays on the legacy model corpus"),
		ElysiumMapTransport::IsMapOnV2Models(TEXT("sp_theatre"), *Settings));
	TestTrue(TEXT("that same map is still on the entity transport"),
		ElysiumMapTransport::IsMapOnNewTransport(TEXT("sp_theatre"), *Settings));
	TestFalse(TEXT("a map on neither list is on neither"),
		ElysiumMapTransport::IsMapOnV2Models(TEXT("sm_hub_2"), *Settings));

	UElysiumMapTransportSettings* Empty = NewObject<UElysiumMapTransportSettings>();
	Empty->MapsOnV2Models.Empty();
	TestFalse(TEXT("an empty list resolves every map to the legacy model corpus"),
		ElysiumMapTransport::IsMapOnV2Models(TEXT("sp_tutorial_1"), *Empty));

	return true;
}

// Every prop path composes from `BakedMeshesFor`, and the tracked `Config/DefaultElysium.ini` is
// what decides which root that is. This is the only assertion that the settings page actually
// reaches the accessor: the resolution test above runs on a synthetic object, and the paths below
// run on the live default the game boots with.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelCorpusRootPathsFollowTheFlagTest,
	"Elysium.Substrate.ModelCorpusRoot.PathsFollowTheFlag", GElysiumModelCorpusRootTestFlags)
bool FElysiumModelCorpusRootPathsFollowTheFlagTest::RunTest(const FString&)
{
	const FString V2Root = FElysiumContentPaths::BakedMeshes();
	const FString LegacyRoot = FElysiumContentPaths::BakedSharedMeshes();
	TestEqual(TEXT("the V2 model corpus root"), V2Root, FString(TEXT("/ElysiumBaked/Meshes")));
	TestEqual(TEXT("the legacy shared corpus root"), LegacyRoot,
		FString(TEXT("/ElysiumBaked/Shared/Meshes")));
	TestNotEqual(TEXT("the two corpora are siblings, never the same package"), V2Root, LegacyRoot);

	static const FString Stem(TEXT("models_scenery_street_payphone_payphone_pair"));
	const UElysiumMapTransportSettings* Live = GetDefault<UElysiumMapTransportSettings>();
	if (!TestNotNull(TEXT("the settings default object exists"), Live))
	{
		return false;
	}

	// The three maps R5.1 converts, and one that is deliberately not converted. Asserting against
	// the resolver rather than against a hard-coded list keeps this true as more maps are listed.
	for (const TCHAR* Map : { TEXT("sp_tutorial_1"), TEXT("sm_pawnshop_1"), TEXT("sm_hub_1"),
		TEXT("sp_theatre"), TEXT("la_hub_1") })
	{
		const bool bOnV2 = ElysiumMapTransport::IsMapOnV2Models(Map, *Live);
		const FString Expected = bOnV2 ? V2Root : LegacyRoot;
		TestEqual(FString::Printf(TEXT("%s resolves its model corpus root"), Map),
			FElysiumContentPaths::BakedMeshesFor(Map), Expected);
		TestEqual(FString::Printf(TEXT("%s composes its prop mesh path from that root"), Map),
			FElysiumContentPaths::BakedPropMesh(Stem, Map),
			Expected / (TEXT("SM_") + Stem) + TEXT(".SM_") + Stem);
		TestEqual(FString::Printf(TEXT("%s composes its item mesh path from that root"), Map),
			FElysiumContentPaths::BakedItemMesh(Stem, Map),
			FElysiumContentPaths::BakedPropMesh(Stem, Map));
		TestEqual(FString::Printf(TEXT("%s composes its skin table path from that root"), Map),
			FElysiumContentPaths::BakedPropSkins(Map),
			Expected / TEXT("DA_ElysiumPropSkins.DA_ElysiumPropSkins"));
	}

	// The three the tracked ini lists today, so a silent removal is a failure rather than a quiet
	// fallback to the legacy corpus for a map whose level places V2 assets.
	for (const TCHAR* Map : { TEXT("sp_tutorial_1"), TEXT("sm_pawnshop_1"), TEXT("sm_hub_1") })
	{
		TestTrue(FString::Printf(
			TEXT("%s is listed on MapsOnV2Models in Config/DefaultElysium.ini"), Map),
			ElysiumMapTransport::IsMapOnV2Models(Map, *Live));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
