// Content-free Substrate automation for UElysiumModelSettings: the model-import lane's LOD mapping
// knobs (docs/architecture/seam_map_model.md -> "Import" -> "Geometry").
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumModelSettings.h"

#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"

static constexpr EAutomationTestFlags GElysiumModelSettingsTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelSettingsDefaultsTest,
	"Elysium.Substrate.ModelSettings.Defaults", GElysiumModelSettingsTestFlags)
bool FElysiumModelSettingsDefaultsTest::RunTest(const FString&)
{
	UElysiumModelSettings* Settings = GetMutableDefault<UElysiumModelSettings>();
	if (!Settings)
	{
		AddError(TEXT("GetMutableDefault<UElysiumModelSettings> returned null"));
		return false;
	}

	// "the values are a wiring default, not a tuning judgement" -- pinned exactly as the contract
	// states them (seam_map_model.md -> "Import" -> "Geometry").
	TestEqual(TEXT("LodSwitchConstant default"), Settings->LodSwitchConstant, 1.0f);
	TestEqual(TEXT("LodScreenSizeFloor default"), Settings->LodScreenSizeFloor, 0.001f);
	TestEqual(TEXT("LodScreenSizeCeiling default"), Settings->LodScreenSizeCeiling, 0.9f);
	TestTrue(TEXT("floor is below ceiling"), Settings->LodScreenSizeFloor < Settings->LodScreenSizeCeiling);

	TestEqual(TEXT("CategoryName"), Settings->GetCategoryName(), FName(TEXT("Elysium")));
	TestEqual(TEXT("SectionName"), Settings->GetSectionName(), FName(TEXT("Models")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumModelSettingsIniRoundTripTest,
	"Elysium.Substrate.ModelSettings.IniRoundTrip", GElysiumModelSettingsTestFlags)
bool FElysiumModelSettingsIniRoundTripTest::RunTest(const FString&)
{
	UElysiumModelSettings* Settings = GetMutableDefault<UElysiumModelSettings>();
	if (!Settings)
	{
		AddError(TEXT("GetMutableDefault<UElysiumModelSettings> returned null"));
		return false;
	}

	const float Saved = Settings->LodSwitchConstant;
	const float Sentinel = 2.5f;
	Settings->LodSwitchConstant = Sentinel;

	// A scratch ini under ProjectSavedDir/ElysiumTests, never the tracked Config/DefaultElysium.ini.
	const FString ScratchIni = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("ElysiumTests") / (FGuid::NewGuid().ToString() + TEXT(".ini")));

	const bool bWrote = Settings->TryUpdateDefaultConfigFile(ScratchIni);
	bool bPassed = true;
	if (!bWrote)
	{
		AddError(TEXT("TryUpdateDefaultConfigFile returned false"));
		bPassed = false;
	}
	else
	{
		float ReadBack = 0.0f;
		const bool bFound = GConfig->GetFloat(TEXT("/Script/ElysiumUE.ElysiumModelSettings"),
			TEXT("LodSwitchConstant"), ReadBack, ScratchIni);
		if (!bFound)
		{
			AddError(FString::Printf(TEXT("LodSwitchConstant not found in scratch ini %s"), *ScratchIni));
			bPassed = false;
		}
		else
		{
			TestEqual(TEXT("scratch ini round-trips the sentinel"), ReadBack, Sentinel);
		}
	}

	// Restore, and drop the scratch file plus GConfig's cached copy of it.
	Settings->LodSwitchConstant = Saved;
	GConfig->UnloadFile(ScratchIni);
	IFileManager::Get().Delete(*ScratchIni, /*RequireExists*/ false);

	return bPassed;
}

#endif // WITH_DEV_AUTOMATION_TESTS
