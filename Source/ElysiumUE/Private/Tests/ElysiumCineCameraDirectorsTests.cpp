#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "Player/ElysiumCameraShots.h"

#include "HAL/FileManager.h"

// The `camera_cinematic` director corpus (docs/project/camera_scripted.md §9 "Content tier";
// $ELYSIUM_WORK_ROOT/_camera_recovery/rc_group_a.md RC2). `CBaseCineCam` overrides nine vtable
// slots and none of them is a `KeyValue` handler -- every authored key is a plain `CBaseEntity`
// datamap field, and `spawnflags` is not even in the datamap: it is read by code alone.
// `CBaseCineCam::Spawn` (`vampire.dll` 0x1006d9a0) is four instructions:
//   TEST byte ptr [ECX+0x204],0x2 ; JZ ; MOV byte ptr [ECX+0x640],0x1 ; RET
// i.e. `if (spawnflags & 0x2) m_bDrawPlayer = 1;` -- the only spawnflags bit `CBaseCineCam` itself
// reads. Bit `0x1` is `CCameraAnimated`'s "freeze the player" bit; it is authored on
// `camera_cinematic` too but dead there, because `FUN_10070780`'s `StartShot` freezes
// unconditionally. Bit `0x4` is the runtime "disposable" bit (`FUN_1017cef0`/`FUN_10070990`
// read it, `FUN_10070470` et al. set it) -- also authorable, and read at `UTIL_Remove` time.
// `StartHidden` is an ordinary `CBaseEntity` key, not one of `CBaseCineCam`'s own six datamap
// rows. This test measures the shipped corpus against RC2's pinned bit meanings and census.
namespace
{
	constexpr int32 SpawnflagsFreeze = 0x1;       // CCameraAnimated's freeze bit; dead here
	constexpr int32 SpawnflagsDrawPlayer = 0x2;   // CBaseCineCam::Spawn -> m_bDrawPlayer = 1
	constexpr int32 SpawnflagsDisposable = 0x4;   // UTIL_Remove on EndShot / SetCineCamera
}

static constexpr EAutomationTestFlags GElysiumCineCameraDirectorsFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCineCameraDirectorsTest,
	"Elysium.Content.CineCameraDirectors", GElysiumCineCameraDirectorsFlags)

bool FElysiumCineCameraDirectorsTest::RunTest(const FString&)
{
	TArray<FString> EntsFiles;
	IFileManager::Get().FindFilesRecursive(EntsFiles, *FElysiumContentPaths::Root(), TEXT("*.ents"),
		/*Files*/ true, /*Directories*/ false);
	if (EntsFiles.IsEmpty())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported maps; camera_cinematic census was not sampled"));
		return true;
	}

	int32 Total = 0;
	TMap<FString, int32> KeyCounts;
	TMap<int32, int32> SpawnflagsHistogram;
	int32 ShotnameTxtForm = 0;
	int32 ShotnameBareForm = 0;
	int32 ShotRows = 0;
	int32 ShotResolved = 0;
	TMap<FString, TSet<FString>> NormalizedShotForms;
	bool bFoundDrawplayerShapedKey = false;
	TArray<FString> TutorialEntities;
	bool bFoundTutorialFeedCamera = false;
	FString TutorialShotnameRaw;
	FString TutorialTarget1;
	FString TutorialPointPlayer;
	FString TutorialSpawnflags;

	const TCHAR* CensusKeys[] = { TEXT("spawnflags"), TEXT("target1"), TEXT("shotname"),
		TEXT("origin"), TEXT("endent"), TEXT("startent"), TEXT("target2"), TEXT("point_player"),
		TEXT("StartHidden") };

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
			if (!Def.Classname.Equals(TEXT("camera_cinematic"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			++Total;

			for (const TCHAR* Key : CensusKeys)
			{
				if (Def.Keys.Contains(Key))
				{
					KeyCounts.FindOrAdd(Key)++;
				}
			}

			// RC1: "no shipped map authors a drawplayer-shaped key" -- m_bDrawPlayer lives only in
			// spawnflags bit 0x2, never as its own keyvalue.
			for (const TPair<FString, FString>& Pair : Def.Keys)
			{
				if (Pair.Key.Contains(TEXT("drawplayer"), ESearchCase::IgnoreCase))
				{
					bFoundDrawplayerShapedKey = true;
				}
			}

			if (const FString* SpawnflagsRaw = Def.Keys.Find(TEXT("spawnflags")))
			{
				const int32 Spawnflags = FCString::Atoi(**SpawnflagsRaw);
				SpawnflagsHistogram.FindOrAdd(Spawnflags)++;
			}

			if (const FString* Shotname = Def.Keys.Find(TEXT("shotname")))
			{
				++ShotRows;
				if (Shotname->Contains(TEXT(".txt"), ESearchCase::IgnoreCase))
				{
					++ShotnameTxtForm;
				}
				else
				{
					++ShotnameBareForm;
				}

				const FString NormalizedKey = ElysiumCameraShots::NormalizeKey(*Shotname);
				NormalizedShotForms.FindOrAdd(NormalizedKey).Add(*Shotname);

				const FElysiumCameraShotDef* Shot = ElysiumCameraShots::Load(*Shotname);
				if (Shot && Shot->IsValid())
				{
					++ShotResolved;
				}
				else if (!FElysiumContentPaths::IsIncomplete(TEXT("vdata")))
				{
					AddError(FString::Printf(
						TEXT("%s '%s': shotname '%s' (normalized '%s') did not resolve through "
							"vdata/camerashots/"),
						*EntsPath, *Def.TargetName, **Shotname, *NormalizedKey));
				}
			}

			if (Defs.MapName.Equals(TEXT("sp_tutorial_1"), ESearchCase::IgnoreCase))
			{
				TutorialEntities.Add(Def.TargetName);
				if (Def.TargetName.Equals(TEXT("feedcamera"), ESearchCase::IgnoreCase))
				{
					bFoundTutorialFeedCamera = true;
					TutorialShotnameRaw = Def.Keys.FindRef(TEXT("shotname"));
					TutorialTarget1 = Def.Keys.FindRef(TEXT("target1"));
					TutorialPointPlayer = Def.Keys.FindRef(TEXT("point_player"));
					TutorialSpawnflags = Def.Keys.FindRef(TEXT("spawnflags"));
				}
			}
		}
	}

	if (Total == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: exported maps contain no camera_cinematic entities"));
		return true;
	}

	AddInfo(FString::Printf(TEXT("camera_cinematic census: %d entities"), Total));
	for (const TCHAR* Key : CensusKeys)
	{
		AddInfo(FString::Printf(TEXT("  %s x%d"), Key, KeyCounts.FindRef(Key)));
	}
	AddInfo(FString::Printf(TEXT("sp_tutorial_1 camera_cinematic entities (%d): %s"),
		TutorialEntities.Num(), *FString::Join(TutorialEntities, TEXT(", "))));

	// RC2 "Shipped census" (rc_group_a.md): 51 directors, every one authoring spawnflags/target1/
	// shotname/origin; endent on 49, startent on 46, target2 on 42, point_player on 35, StartHidden
	// on 5. A decoder regression or a corpus revision shows here first.
	TestEqual(TEXT("51 shipped camera_cinematic entities"), Total, 51);
	TestEqual(TEXT("spawnflags authored on every director"), KeyCounts.FindRef(TEXT("spawnflags")), 51);
	TestEqual(TEXT("target1 authored on every director"), KeyCounts.FindRef(TEXT("target1")), 51);
	TestEqual(TEXT("shotname authored on every director"), KeyCounts.FindRef(TEXT("shotname")), 51);
	TestEqual(TEXT("origin authored on every director"), KeyCounts.FindRef(TEXT("origin")), 51);
	TestEqual(TEXT("endent authored on 49 directors"), KeyCounts.FindRef(TEXT("endent")), 49);
	TestEqual(TEXT("startent authored on 46 directors"), KeyCounts.FindRef(TEXT("startent")), 46);
	TestEqual(TEXT("target2 authored on 42 directors"), KeyCounts.FindRef(TEXT("target2")), 42);
	TestEqual(TEXT("point_player authored on 35 directors"), KeyCounts.FindRef(TEXT("point_player")), 35);
	TestEqual(TEXT("StartHidden authored on 5 directors"), KeyCounts.FindRef(TEXT("StartHidden")), 5);

	TestFalse(TEXT("no camera_cinematic authors a drawplayer-shaped key (RC1/RC2: m_bDrawPlayer is a "
		"spawnflags bit read in CBaseCineCam::Spawn, never a keyvalue)"), bFoundDrawplayerShapedKey);

	// RC2.2 "Every spawnflags bit, pinned": values 0, 1, 3, 5, 7 only, counted {1,20,18,3,9}.
	TestEqual(TEXT("five distinct spawnflags values shipped"), SpawnflagsHistogram.Num(), 5);
	TestEqual(TEXT("spawnflags 0 authored on 1 director"), SpawnflagsHistogram.FindRef(0), 1);
	TestEqual(TEXT("spawnflags 1 (freeze) authored on 20 directors"), SpawnflagsHistogram.FindRef(1), 20);
	TestEqual(TEXT("spawnflags 3 (freeze+drawplayer) authored on 18 directors"),
		SpawnflagsHistogram.FindRef(3), 18);
	TestEqual(TEXT("spawnflags 5 (freeze+disposable) authored on 3 directors"),
		SpawnflagsHistogram.FindRef(5), 3);
	TestEqual(TEXT("spawnflags 7 (freeze+drawplayer+disposable) authored on 9 directors"),
		SpawnflagsHistogram.FindRef(7), 9);

	int32 BitFreezeCount = 0;
	int32 BitDrawPlayerCount = 0;
	int32 BitDisposableCount = 0;
	for (const TPair<int32, int32>& Row : SpawnflagsHistogram)
	{
		if (Row.Key & SpawnflagsFreeze)     { BitFreezeCount     += Row.Value; }
		if (Row.Key & SpawnflagsDrawPlayer) { BitDrawPlayerCount += Row.Value; }
		if (Row.Key & SpawnflagsDisposable) { BitDisposableCount += Row.Value; }
	}
	TestEqual(TEXT("spawnflags bit 0x1 (freeze, dead on camera_cinematic) set on 50 of 51"),
		BitFreezeCount, 50);
	TestEqual(TEXT("spawnflags bit 0x2 (m_bDrawPlayer, CBaseCineCam::Spawn 0x1006d9a0) set on 27 of 51"),
		BitDrawPlayerCount, 27);
	TestEqual(TEXT("spawnflags bit 0x4 (disposable, UTIL_Remove on EndShot/SetCineCamera) set on 12 of 51"),
		BitDisposableCount, 12);

	TestEqual(TEXT("43 shotname values carry a vdata/CameraShots/*.txt path"), ShotnameTxtForm, 43);
	TestEqual(TEXT("8 shotname values are authored bare"), ShotnameBareForm, 8);

	// Both authored spellings must fold to the same identity -- `Q_FileBase` in retail,
	// `ElysiumCameraShots::NormalizeKey` here -- or a director naming the bare form and one naming
	// the `.txt` path for the SAME shot would silently load two different caches.
	bool bFoundCollapsedForms = false;
	for (const TPair<FString, TSet<FString>>& Row : NormalizedShotForms)
	{
		if (Row.Value.Num() < 2)
		{
			continue;
		}
		bool bHasTxtForm = false;
		bool bHasBareForm = false;
		for (const FString& RawForm : Row.Value)
		{
			if (RawForm.Contains(TEXT(".txt"), ESearchCase::IgnoreCase))
			{
				bHasTxtForm = true;
			}
			else
			{
				bHasBareForm = true;
			}
		}
		if (bHasTxtForm && bHasBareForm)
		{
			bFoundCollapsedForms = true;
			break;
		}
	}
	TestTrue(TEXT("at least one shot is authored both as a vdata/CameraShots/*.txt path and bare, "
		"and both forms normalize to the same key"), bFoundCollapsedForms);

	if (FElysiumContentPaths::IsIncomplete(TEXT("vdata")))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the vdata export domain is marked incomplete; shotname "
			"resolution against vdata/camerashots/ was not checked"));
	}
	else
	{
		TestEqual(TEXT("every authored shotname resolves through vdata/camerashots/"),
			ShotResolved, ShotRows);
	}

	// sp_tutorial_1's `feedcamera` -- RC2's own worked example, and the one director this test pins
	// exactly rather than by census.
	if (!TestTrue(TEXT("sp_tutorial_1's feedcamera director is in the exported corpus"),
		bFoundTutorialFeedCamera))
	{
		return true;
	}
	TestEqual(TEXT("sp_tutorial_1 feedcamera shotname normalizes to LookAtTarget_Snap"),
		ElysiumCameraShots::NormalizeKey(TutorialShotnameRaw),
		ElysiumCameraShots::NormalizeKey(TEXT("LookAtTarget_Snap")));
	TestEqual(TEXT("sp_tutorial_1 feedcamera target1"), TutorialTarget1, FString(TEXT("tutwareportal03")));
	TestEqual(TEXT("sp_tutorial_1 feedcamera point_player"), TutorialPointPlayer, FString(TEXT("0")));
	TestEqual(TEXT("sp_tutorial_1 feedcamera spawnflags (freeze+drawplayer)"),
		TutorialSpawnflags, FString(TEXT("3")));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
