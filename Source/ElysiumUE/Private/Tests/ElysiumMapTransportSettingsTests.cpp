// Content-free Substrate automation for R4.6: the explicit per-map cutover flag
// (`docs/architecture/seam_map_map.md` -> "## Import" -> "The explicit per-map cutover flag
// (R4.6)"). Exercises the pure resolver against synthetic `NewObject`-built settings, the same
// shape `UElysiumLightRig::ApplySettings`'s own Substrate coverage uses (R4.3) -- no baked asset or
// scratch content root needed, since the resolver never loads one.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumMapTransportSettings.h"

#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"

static constexpr EAutomationTestFlags GElysiumMapTransportSettingsTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// The rule the whole task rests on: an empty list resolves every map to the legacy path, a listed
// stem resolves case-insensitively (the settings object stores `FName`, whose comparisons the
// resolver deliberately does not rely on -- it compares `ToString()` against the caller's
// `FString`), and a map left off the list stays on the legacy path even while others are listed.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapTransportFlagResolutionTest,
	"Elysium.Substrate.MapTransport.FlagResolution", GElysiumMapTransportSettingsTestFlags)
bool FElysiumMapTransportFlagResolutionTest::RunTest(const FString&)
{
	// NewObject inherits the CDO's config-loaded MapsOnNewTransport (Config/DefaultElysium.ini
	// lists real maps), so an actually-empty list is made explicit here rather than assumed.
	UElysiumMapTransportSettings* Empty = NewObject<UElysiumMapTransportSettings>();
	Empty->MapsOnNewTransport.Empty();
	TestFalse(TEXT("an empty list resolves any map to the legacy path"),
		ElysiumMapTransport::IsMapOnNewTransport(TEXT("sm_pawnshop_1"), *Empty));

	UElysiumMapTransportSettings* Settings = NewObject<UElysiumMapTransportSettings>();
	Settings->MapsOnNewTransport = { FName(TEXT("sm_pawnshop_1")), FName(TEXT("sp_tutorial_1")) };

	TestTrue(TEXT("an exact-case listed map resolves to the new transport"),
		ElysiumMapTransport::IsMapOnNewTransport(TEXT("sm_pawnshop_1"), *Settings));
	TestTrue(TEXT("a listed map resolves case-insensitively"),
		ElysiumMapTransport::IsMapOnNewTransport(TEXT("SM_Pawnshop_1"), *Settings));
	TestTrue(TEXT("the second listed map also resolves"),
		ElysiumMapTransport::IsMapOnNewTransport(TEXT("sp_tutorial_1"), *Settings));
	TestFalse(TEXT("a map left off the list stays on the legacy path"),
		ElysiumMapTransport::IsMapOnNewTransport(TEXT("sm_hub_1"), *Settings));
	TestFalse(TEXT("an unrelated name never matches"),
		ElysiumMapTransport::IsMapOnNewTransport(TEXT("sm_pawnshop_1_annex"), *Settings));

	return true;
}

// The list is a tracked config array, not just an in-memory default -- proves it round-trips
// through `Config/DefaultElysium.ini`'s own mechanism the way every other `Config = Elysium,
// DefaultConfig` page does (`FElysiumOrphanSettingsIniRoundTripTest`'s own shape), on a scratch ini
// so the tracked file is never touched.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapTransportIniRoundTripTest,
	"Elysium.Substrate.MapTransport.IniRoundTrip", GElysiumMapTransportSettingsTestFlags)
bool FElysiumMapTransportIniRoundTripTest::RunTest(const FString&)
{
	UElysiumMapTransportSettings* Settings = GetMutableDefault<UElysiumMapTransportSettings>();
	const TArray<FName> Was = Settings->MapsOnNewTransport;
	Settings->MapsOnNewTransport = { FName(TEXT("sm_pawnshop_1")) };

	const FString ScratchIni = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("ElysiumTests")
		/ (FGuid::NewGuid().ToString() + TEXT(".ini")));

	bool bPassed = true;
	if (!Settings->TryUpdateDefaultConfigFile(ScratchIni))
	{
		AddError(TEXT("TryUpdateDefaultConfigFile(scratch) returned false"));
		bPassed = false;
	}
	else
	{
		TArray<FString> ReadBack;
		const int32 FoundCount = GConfig->GetArray(
			TEXT("/Script/ElysiumUE.ElysiumMapTransportSettings"),
			TEXT("MapsOnNewTransport"), ReadBack, ScratchIni);
		if (FoundCount <= 0)
		{
			AddError(TEXT("MapsOnNewTransport not found in scratch ini"));
			bPassed = false;
		}
		else
		{
			TestEqual(TEXT("scratch ini carries exactly one listed map"), ReadBack.Num(), 1);
			if (ReadBack.Num() == 1)
			{
				TestEqual(TEXT("scratch ini's listed map"), ReadBack[0], FString(TEXT("sm_pawnshop_1")));
			}
		}
	}

	Settings->MapsOnNewTransport = Was;
	GConfig->UnloadFile(ScratchIni);
	IFileManager::Get().Delete(*ScratchIni, /*RequireExists*/ false);

	return bPassed;
}

#endif // WITH_DEV_AUTOMATION_TESTS
