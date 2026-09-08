// Content-free Substrate automation for R4.5: the orphan taste values that used to live only as
// hardcoded cvar/literal defaults now have `UDeveloperSettings` homes.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumAudioSettings.h"
#include "ElysiumChoreoSettings.h"
#include "ElysiumSessionSettings.h"
#include "ElysiumUISettings.h"
#include "ElysiumWeatherSettings.h"

#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"

static constexpr EAutomationTestFlags GElysiumOrphanSettingsTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// Every value here is a straight relocation, not a re-tune: the roadmap line's own text is "values
// = today's defaults exactly." This is the assertion that landed true — each field's default is
// the exact number the retired cvar/literal carried.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumOrphanSettingsDefaultsTest,
	"Elysium.Substrate.OrphanSettings.Defaults", GElysiumOrphanSettingsTestFlags)
bool FElysiumOrphanSettingsDefaultsTest::RunTest(const FString&)
{
	const UElysiumUISettings* UI = GetDefault<UElysiumUISettings>();
	TestEqual(TEXT("UI.MenuScrim was elysium.MenuScrim's default"), UI->MenuScrim, 0.22f);

	const UElysiumChoreoSettings* Choreo = GetDefault<UElysiumChoreoSettings>();
	TestEqual(TEXT("Choreo.JawSpeechLevel was elysium.JawSpeechLevel's default"),
		Choreo->JawSpeechLevel, 0.49f);
	TestEqual(TEXT("Choreo.JawSmoothing was elysium.JawSmoothing's default"),
		Choreo->JawSmoothing, 0.05f);
	TestEqual(TEXT("Choreo.CameraCutSeconds was elysium.CameraCutSeconds's default"),
		Choreo->CameraCutSeconds, 0.05f);

	const UElysiumAudioSettings* Audio = GetDefault<UElysiumAudioSettings>();
	TestEqual(TEXT("Audio.MusicCrossfade was elysium.MusicCrossfade's default"),
		Audio->MusicCrossfade, 2.0f);
	TestEqual(TEXT("Audio.SchemeRandomBase was elysium.SchemeRandomBase's default"),
		Audio->SchemeRandomBase, 8.0f);

	const UElysiumSessionSettings* Session = GetDefault<UElysiumSessionSettings>();
	TestEqual(TEXT("Session.LoadingScreenMinTime was elysium.LoadingScreenMinTime's default"),
		Session->LoadingScreenMinTime, 0.75f);

	// The seven values the Cog "Enhanced defaults" button used to hardcode, verbatim.
	const UElysiumWeatherSettings* Weather = GetDefault<UElysiumWeatherSettings>();
	TestEqual(TEXT("Weather.RainEnhancement matches the button's preset"), Weather->RainEnhancement, 1.0f);
	TestEqual(TEXT("Weather.RainWetDarken matches the button's preset"), Weather->RainWetDarken, 0.06f);
	TestEqual(TEXT("Weather.RainWetRoughness matches the button's preset"), Weather->RainWetRoughness, 0.10f);
	TestEqual(TEXT("Weather.RainLightResponse matches the button's preset"), Weather->RainLightResponse, 0.25f);
	TestEqual(TEXT("Weather.RainSourceRetain matches the button's preset"), Weather->RainSourceRetain, 1.0f);
	TestEqual(TEXT("Weather.RainWetSpecular matches the button's preset"), Weather->RainWetSpecular, 0.50f);
	TestEqual(TEXT("Weather.EnvironmentWetnessScale matches the button's preset"),
		Weather->EnvironmentWetnessScale, 1.0f);

	return true;
}

// One field per page, round-tripped through a scratch ini exactly as `UElysiumSurfaceSettings`'s
// own `IniRoundTrip` test does (`ElysiumSurfaceKnobTests.cpp`): every one of these five pages is a
// `Config = Elysium, DefaultConfig` object, so this is the mechanism -- not the numbers -- that
// makes "editor home" true. A page that failed to round-trip would edit live but never persist.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumOrphanSettingsIniRoundTripTest,
	"Elysium.Substrate.OrphanSettings.IniRoundTrip", GElysiumOrphanSettingsTestFlags)
bool FElysiumOrphanSettingsIniRoundTripTest::RunTest(const FString&)
{
	// One scratch file per object, not one shared by all five: GConfig caches a loaded ini by
	// filename, so a second `GetFloat` against a filename it already cached from the first object's
	// read would answer from that stale in-memory copy rather than re-parsing what the second
	// object's `TryUpdateDefaultConfigFile` just wrote to disk.
	const FString ScratchDir = FPaths::ProjectSavedDir() / TEXT("ElysiumTests");
	auto NewScratchIni = [&ScratchDir]()
	{
		return FPaths::ConvertRelativePathToFull(
			ScratchDir / (FGuid::NewGuid().ToString() + TEXT(".ini")));
	};

	UElysiumUISettings* UI = GetMutableDefault<UElysiumUISettings>();
	UElysiumChoreoSettings* Choreo = GetMutableDefault<UElysiumChoreoSettings>();
	UElysiumAudioSettings* Audio = GetMutableDefault<UElysiumAudioSettings>();
	UElysiumSessionSettings* Session = GetMutableDefault<UElysiumSessionSettings>();
	UElysiumWeatherSettings* Weather = GetMutableDefault<UElysiumWeatherSettings>();

	const float WasScrim = UI->MenuScrim;
	const float WasSmoothing = Choreo->JawSmoothing;
	const float WasCrossfade = Audio->MusicCrossfade;
	const float WasMinTime = Session->LoadingScreenMinTime;
	const float WasWetDarken = Weather->RainWetDarken;

	UI->MenuScrim = 0.111f;
	Choreo->JawSmoothing = 0.222f;
	Audio->MusicCrossfade = 0.333f;
	Session->LoadingScreenMinTime = 0.444f;
	Weather->RainWetDarken = 0.055f;

	bool bPassed = true;
	TArray<FString> ScratchFiles;

	// Every one of these five objects is a separate `Config = Elysium, DefaultConfig` UCLASS, each
	// with its own `/Script/ElysiumUE.<Class>` ini section -- so, exactly like
	// `FElysiumSurfaceSettingsIniRoundTripTest`, writing to a scratch ini path never touches the
	// tracked `Config/DefaultElysium.ini`, and the CDO mutation above is restored below regardless.
	auto RoundTrip = [this, &bPassed, &ScratchFiles, &NewScratchIni](UDeveloperSettings* Settings,
		const TCHAR* Section, const TCHAR* Field, float Expected, const TCHAR* Label)
	{
		const FString ScratchIni = NewScratchIni();
		ScratchFiles.Add(ScratchIni);
		if (!Settings->TryUpdateDefaultConfigFile(ScratchIni))
		{
			AddError(FString::Printf(TEXT("%s: TryUpdateDefaultConfigFile(scratch) returned false"), Label));
			bPassed = false;
			return;
		}
		float ReadBack = 0.f;
		if (!GConfig->GetFloat(Section, Field, ReadBack, ScratchIni))
		{
			AddError(FString::Printf(TEXT("%s: %s not found in scratch ini"), Label, Field));
			bPassed = false;
			return;
		}
		TestEqual(Label, ReadBack, Expected);
	};

	RoundTrip(UI, TEXT("/Script/ElysiumUE.ElysiumUISettings"), TEXT("MenuScrim"), 0.111f,
		TEXT("UI.MenuScrim round-trips"));
	RoundTrip(Choreo, TEXT("/Script/ElysiumUE.ElysiumChoreoSettings"), TEXT("JawSmoothing"), 0.222f,
		TEXT("Choreo.JawSmoothing round-trips"));
	RoundTrip(Audio, TEXT("/Script/ElysiumUE.ElysiumAudioSettings"), TEXT("MusicCrossfade"), 0.333f,
		TEXT("Audio.MusicCrossfade round-trips"));
	RoundTrip(Session, TEXT("/Script/ElysiumUE.ElysiumSessionSettings"), TEXT("LoadingScreenMinTime"), 0.444f,
		TEXT("Session.LoadingScreenMinTime round-trips"));
	RoundTrip(Weather, TEXT("/Script/ElysiumUE.ElysiumWeatherSettings"), TEXT("RainWetDarken"), 0.055f,
		TEXT("Weather.RainWetDarken round-trips"));

	// Restore every mutated CDO and drop every scratch file plus GConfig's cached copy of it.
	UI->MenuScrim = WasScrim;
	Choreo->JawSmoothing = WasSmoothing;
	Audio->MusicCrossfade = WasCrossfade;
	Session->LoadingScreenMinTime = WasMinTime;
	Weather->RainWetDarken = WasWetDarken;
	for (const FString& ScratchIni : ScratchFiles)
	{
		GConfig->UnloadFile(ScratchIni);
		IFileManager::Get().Delete(*ScratchIni, /*RequireExists*/ false);
	}

	return bPassed;
}

#endif // WITH_DEV_AUTOMATION_TESTS
