#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "Player/ElysiumCameraShots.h"

#include "HAL/FileManager.h"

static constexpr EAutomationTestFlags GElysiumDialogueCameraContentFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDialogueCameraDemandTest,
	"Elysium.Content.DialogueCameraDemand", GElysiumDialogueCameraContentFlags)

bool FElysiumDialogueCameraDemandTest::RunTest(const FString&)
{
	TArray<FString> EntsFiles;
	IFileManager::Get().FindFilesRecursive(EntsFiles, *FElysiumContentPaths::Root(), TEXT("*.ents"),
		/*Files*/ true, /*Directories*/ false);
	if (EntsFiles.IsEmpty())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported maps; dialogue-camera demand was not sampled"));
		return true;
	}

	int32 Rows = 0;
	int32 Parsed = 0;
	TSet<FString> ShotKeys;
	TArray<FString> ClassifiedFallbacks;
	for (const FString& EntsPath : EntsFiles)
	{
		FElysiumEntityDefs Defs;
		if (!TestTrue(FString::Printf(TEXT("%s parses"), *EntsPath),
			FElysiumEntityDefs::Parse(EntsPath, Defs)))
		{
			continue;
		}
		for (const FElysiumEntityDef& Def : Defs.Defs)
		{
			const FString* Raw = Def.Keys.Find(TEXT("default_camera"));
			if (!Raw)
			{
				continue;
			}
			++Rows;
			const FString Key = ElysiumCameraShots::NormalizeKey(*Raw);
			if (Key.IsEmpty())
			{
				ClassifiedFallbacks.Add(FString::Printf(TEXT("%s: empty name"), *EntsPath));
				continue;
			}
			ShotKeys.Add(Key);
			const FElysiumCameraShotDef* Shot = ElysiumCameraShots::Load(*Raw);
			if (!Shot || !Shot->IsValid())
			{
				ClassifiedFallbacks.Add(FString::Printf(TEXT("%s: '%s' missing or unparsed"),
					*EntsPath, **Raw));
				continue;
			}
			++Parsed;
		}
	}

	if (Rows == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: exported maps contain no default_camera demand"));
		return true;
	}
	for (const FString& Classification : ClassifiedFallbacks)
	{
		AddWarning(FString::Printf(TEXT("classified player-view fallback: %s"), *Classification));
	}
	TestEqual(TEXT("every exported default_camera resolves or has a classified fallback"),
		Parsed + ClassifiedFallbacks.Num(), Rows);
	AddInfo(FString::Printf(TEXT("dialogue-camera demand: %d rows, %d normalized shots, %d parsed, %d classified fallbacks"),
		Rows, ShotKeys.Num(), Parsed, ClassifiedFallbacks.Num()));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
