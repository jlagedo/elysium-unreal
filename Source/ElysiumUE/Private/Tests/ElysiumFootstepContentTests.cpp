// The footstep subsystem's CONTENT proof — what the shipped corpus actually authors, asserted
// against the recovery in `docs/vtmb/footsteps.md` and `docs/vtmb/animation_events.md`.
//
// The seam cases (`Elysium.Substrate.Footsteps.*` in `ElysiumFootstepSeamTests.cpp`) prove the
// rules against fabricated data. These four prove the DATA those rules will meet:
//
//   * `Elysium.Content.FootstepTemplates`     — every `npctemplate*.txt` record resolves the four
//                                               footfall keys through `ParentTemplateName`, and the
//                                               shipped distribution of the values.
//   * `Elysium.Content.FootstepTuningDefaults`— the seven cvars' retail defaults, struct and
//                                               declaration, as `vampire.dll` constructs them.
//   * `Elysium.Content.FootstepSurfacesForSteps` — the four surfaceprops the PLAYER's step clock
//                                               names by string (`ladder`, `water`, `wade`, and the
//                                               dry fallback `default`) resolve with real pools.
//   * `Elysium.Content.FootstepEventCensus`   — the 2050–2053 records the runtime's own clip tables
//                                               carry for the shared male and female banks.
//
// Nothing here plays a step: the producers are wave 2's (`Elysium.Substrate.Footsteps.Npc*` /
// `Player*`). What these assert is that the numbers those producers read exist, in the quantities
// the recovery states, so a re-export that dropped a key or an alternate fails HERE rather than as
// a silent character.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumAnimEvent.h"
#include "ElysiumBodyData.h"
#include "ElysiumCastData.h"
#include "ElysiumClipData.h"
#include "ElysiumContentPaths.h"
#include "ElysiumFootstepTuning.h"
#include "ElysiumMapActor.h"
#include "ElysiumSoundLevel.h"
#include "ElysiumSurfaceSounds.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumFootsteps.h"
#include "Substrate/ElysiumRulebook.h"
#include "Tests/ElysiumNativeCharacterTestData.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumCharacterAssets.h"

#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"    // GetDefault

namespace ElysiumFootstepContentTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// --- Elysium.Content.FootstepTemplates ----------------------------------------------------------
//
// `0x1026d460` step 2 reads `NormalFootfallVol/Dist` (`template+0x24`/`+0x28`) or
// `HeavyFootfallVol/Dist` (`+0x2c`/`+0x30`) off the NPC's resolved character template, with
// `0x101d3f10`'s defaults 0.45/256/0.85/512 where the block does not author them. This case walks
// the shipped `npctemplate*.txt` corpus through `FElysiumClanTable::Resolve` +
// `FElysiumClanTemplate::GeneralFloat` — the port's spelling of that read — and pins what comes
// back.

namespace
{
	// The four authored profiles the shipped corpus contains, and how many of the 150 NPC templates
	// resolve to each. Counted over the real files; a rulebook edit that moved a template between
	// profiles has to move a number here with it, which is the point.
	struct FFootfallProfile
	{
		const TCHAR* Name;
		float NormalVol;
		float NormalDist;
		float HeavyVol;
		float HeavyDist;
		int32 Expected;
	};

	// Ordered loudest-authored first so the report reads as a ladder.
	const FFootfallProfile GShippedProfiles[] =
	{
		// `0x101d3f10`'s own defaults — the template authors nothing anywhere in its parent chain.
		// **The largest group by far**: most of the cast steps on the same numbers the cvar
		// fallback would have given.
		{ TEXT("loader defaults"),   0.45f,  256.0f, 0.85f,  512.0f, 94 },
		// The base human authoring, `NPCGeneric` in `npctemplate000.txt` and everything under it.
		{ TEXT("base human"),        0.45f,  300.0f, 0.85f,  600.0f, 31 },
		// **The inverted profile.** `npctemplate007/009/011/013/014/015/016` and the tutorial's own
		// two thug templates author the NORMAL footfall louder and farther than the heavy one.
		{ TEXT("inverted thug"),     0.85f, 1000.0f, 0.45f,  800.0f, 24 },
		// `MuseumGuard` alone (`npctemplate002.txt`): both modes at full volume, and the longest
		// authored distances in the game.
		{ TEXT("museum guard"),      1.00f, 1400.0f, 1.00f, 1700.0f,  1 },
	};

	bool MatchesProfile(const FFootfallProfile& Profile, float NormalVol, float NormalDist,
		float HeavyVol, float HeavyDist)
	{
		return FMath::IsNearlyEqual(NormalVol, Profile.NormalVol, 1e-4f)
			&& FMath::IsNearlyEqual(NormalDist, Profile.NormalDist, 1e-3f)
			&& FMath::IsNearlyEqual(HeavyVol, Profile.HeavyVol, 1e-4f)
			&& FMath::IsNearlyEqual(HeavyDist, Profile.HeavyDist, 1e-3f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFootstepTemplatesTest,
	"Elysium.Content.FootstepTemplates", GElysiumTestFlags)
bool FElysiumFootstepTemplatesTest::RunTest(const FString&)
{
	using namespace ElysiumFootstep;

	if (!IFileManager::Get().FileExists(
		*FElysiumContentPaths::VdataFile(TEXT("system/npctemplate000.txt"))))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported vdata "
			"(run: uv run elysium export_v2 vdatas-glb, then uv run elysium import vdata)"));
		return true;
	}

	FString Error;
	FElysiumClanTable Clans;
	if (!TestTrue(TEXT("clandoc000.txt + the npctemplate corpus load"), Clans.Load(Error)))
	{
		AddError(Error);
		return true;
	}
	TestEqual(TEXT("36 npctemplate files"), Clans.NpcFiles.Num(), 36);
	TestEqual(TEXT("150 npc templates"), Clans.NpcTemplates.Num(), 150);

	// --- Every template answers all four keys ---------------------------------------------------
	// `0x1026d460` reads the four unconditionally once `footstep_npc_use_templates` is on, so a
	// template that answered nothing would step at volume 0 (silent) or distance 0 (SNDLVL_NONE,
	// audible across the whole map). Neither is a state the corpus is allowed to reach.
	const int32 ProfileCount = static_cast<int32>(UE_ARRAY_COUNT(GShippedProfiles));
	int32 Resolved = 0;
	int32 Unclassified = 0;
	int32 ProfileHits[UE_ARRAY_COUNT(GShippedProfiles)] = {};
	for (const FElysiumClanTemplate& Row : Clans.NpcTemplates)
	{
		FElysiumClanTemplate Template;
		if (!Clans.Resolve(Row.TemplateName, Template))
		{
			AddError(FString::Printf(TEXT("%s (%s) does not resolve"),
				*Row.TemplateName, *Row.SourceFile));
			continue;
		}
		++Resolved;

		const float NormalVol  = Template.GeneralFloat(KeyNormalVol,  TemplateNormalVolDefault);
		const float NormalDist = Template.GeneralFloat(KeyNormalDist, TemplateNormalDistDefault);
		const float HeavyVol   = Template.GeneralFloat(KeyHeavyVol,   TemplateHeavyVolDefault);
		const float HeavyDist  = Template.GeneralFloat(KeyHeavyDist,  TemplateHeavyDistDefault);

		if (NormalVol <= 0.f || NormalDist <= 0.f || HeavyVol <= 0.f || HeavyDist <= 0.f)
		{
			AddError(FString::Printf(
				TEXT("%s resolves a non-positive footfall value: %g/%g %g/%g"),
				*Row.TemplateName, NormalVol, NormalDist, HeavyVol, HeavyDist));
			continue;
		}

		int32 Matched = INDEX_NONE;
		for (int32 i = 0; i < ProfileCount; ++i)
		{
			if (MatchesProfile(GShippedProfiles[i], NormalVol, NormalDist, HeavyVol, HeavyDist))
			{
				Matched = i;
				break;
			}
		}
		if (Matched == INDEX_NONE)
		{
			++Unclassified;
			AddError(FString::Printf(
				TEXT("%s (%s) authors a footfall profile the recovery does not name: %g/%g %g/%g"),
				*Row.TemplateName, *Row.SourceFile, NormalVol, NormalDist, HeavyVol, HeavyDist));
			continue;
		}
		++ProfileHits[Matched];
	}
	TestEqual(TEXT("every npc template resolves"), Resolved, Clans.NpcTemplates.Num());
	TestEqual(TEXT("no template authors an unrecovered footfall profile"), Unclassified, 0);

	// --- The shipped distribution ----------------------------------------------------------------
	int32 Total = 0;
	for (int32 i = 0; i < ProfileCount; ++i)
	{
		const FFootfallProfile& Profile = GShippedProfiles[i];
		AddInfo(FString::Printf(TEXT("footfall profile '%s' (%g/%g %g/%g): %d templates"),
			Profile.Name, Profile.NormalVol, Profile.NormalDist, Profile.HeavyVol,
			Profile.HeavyDist, ProfileHits[i]));
		TestEqual(FString::Printf(TEXT("%d templates take the '%s' footfall profile"),
			Profile.Expected, Profile.Name), ProfileHits[i], Profile.Expected);
		Total += Profile.Expected;
	}
	TestEqual(TEXT("the four profiles account for every npc template"),
		Total, Clans.NpcTemplates.Num());

	// **The corpus does not agree with the cvar fallback.** 56 of the 150 author the four keys
	// somewhere in their chain and 94 do not, so `footstep_npc_use_templates 0` silently changes the
	// volume of a third of the cast (0.45 -> 0.5 normal) while leaving the rest identical.
	TestEqual(TEXT("56 templates author a footfall value somewhere in their parent chain"),
		Clans.NpcTemplates.Num() - ProfileHits[0], 56);

	// --- `NPCGeneric` — the base human template, authored in npctemplate000.txt -------------------
	{
		FElysiumClanTemplate Base;
		if (TestTrue(TEXT("NPCGeneric resolves"), Clans.Resolve(TEXT("NPCGeneric"), Base)))
		{
			TestEqual(TEXT("NPCGeneric NormalFootfallVol 0.45"),
				Base.GeneralFloat(KeyNormalVol, TemplateNormalVolDefault), 0.45f);
			TestEqual(TEXT("NPCGeneric NormalFootfallDist 300"),
				Base.GeneralFloat(KeyNormalDist, TemplateNormalDistDefault), 300.f);
			TestEqual(TEXT("NPCGeneric HeavyFootfallVol 0.85"),
				Base.GeneralFloat(KeyHeavyVol, TemplateHeavyVolDefault), 0.85f);
			TestEqual(TEXT("NPCGeneric HeavyFootfallDist 600"),
				Base.GeneralFloat(KeyHeavyDist, TemplateHeavyDistDefault), 600.f);
			TestEqual(TEXT("...and it authors all four itself, not through a parent"),
				Base.ParentTemplateName, FString());
		}
	}

	// --- Inheritance, one level, two levels, and across files -------------------------------------
	// The four keys are `General` entries, and `Resolve` folds `General` parent-first, so a child
	// that states none of them takes its parent's — which is the whole reason `0x1026d460` reading a
	// RESOLVED template rather than the raw block matters.
	{
		// `CivilianGeneric` (npctemplate003) is `NPCGeneric`'s child and authors no footfall key.
		if (const FElysiumClanTemplate* Raw = Clans.Find(TEXT("CivilianGeneric")))
		{
			TestEqual(TEXT("CivilianGeneric's parent is NPCGeneric"),
				Raw->ParentTemplateName, FString(TEXT("NPCGeneric")));
			TestFalse(TEXT("...and its own block states no NormalFootfallDist"),
				Raw->HasGeneral(KeyNormalDist));
			TestFalse(TEXT("...nor a HeavyFootfallDist"), Raw->HasGeneral(KeyHeavyDist));
		}
		else
		{
			AddError(TEXT("no CivilianGeneric template"));
		}

		// One level, two levels (`CivilianTest1` -> `CivilianGeneric` -> `NPCGeneric`) and across
		// files (`Cabbie` lives in `npctemplate_cabbie.txt` and its parent in `npctemplate003.txt`).
		for (const TCHAR* Name : { TEXT("CivilianGeneric"), TEXT("CivilianTest1"),
			TEXT("Zhao"), TEXT("Test_Mle1_Def1_Sok1"), TEXT("Cabbie") })
		{
			FElysiumClanTemplate Child;
			if (!TestTrue(FString::Printf(TEXT("%s resolves"), Name), Clans.Resolve(Name, Child)))
			{
				continue;
			}
			TestEqual(FString::Printf(TEXT("%s inherits NormalFootfallDist 300"), Name),
				Child.GeneralFloat(KeyNormalDist, TemplateNormalDistDefault), 300.f);
			TestEqual(FString::Printf(TEXT("%s inherits HeavyFootfallDist 600"), Name),
				Child.GeneralFloat(KeyHeavyDist, TemplateHeavyDistDefault), 600.f);
			TestEqual(FString::Printf(TEXT("%s inherits NormalFootfallVol 0.45"), Name),
				Child.GeneralFloat(KeyNormalVol, TemplateNormalVolDefault), 0.45f);
			TestEqual(FString::Printf(TEXT("%s inherits HeavyFootfallVol 0.85"), Name),
				Child.GeneralFloat(KeyHeavyVol, TemplateHeavyVolDefault), 0.85f);
		}
	}

	// --- A template with none of the four takes the loader's own defaults -------------------------
	// `VampireGeneric` heads the largest branch of the cast and authors no footfall key anywhere.
	{
		FElysiumClanTemplate Bare;
		if (TestTrue(TEXT("VampireGeneric resolves"), Clans.Resolve(TEXT("VampireGeneric"), Bare)))
		{
			TestFalse(TEXT("VampireGeneric authors no NormalFootfallVol in its whole chain"),
				Bare.HasGeneral(KeyNormalVol));
			TestEqual(TEXT("so NormalFootfallVol is 0x101d3f10's 0.45"),
				Bare.GeneralFloat(KeyNormalVol, TemplateNormalVolDefault), 0.45f);
			TestEqual(TEXT("NormalFootfallDist is 256"),
				Bare.GeneralFloat(KeyNormalDist, TemplateNormalDistDefault), 256.f);
			TestEqual(TEXT("HeavyFootfallVol is 0.85"),
				Bare.GeneralFloat(KeyHeavyVol, TemplateHeavyVolDefault), 0.85f);
			TestEqual(TEXT("HeavyFootfallDist is 512"),
				Bare.GeneralFloat(KeyHeavyDist, TemplateHeavyDistDefault), 512.f);
		}
	}

	// --- The inverted profile, stated as a recovery rather than left as a surprise -----------------
	// `TutorialThug` and `TutorialShovelhead` are the bodies the owner's live check walks past, and
	// they author `NormalFootfallDist 1000` / `HeavyFootfallDist 800` and volumes 0.85 / 0.45. The
	// *walk* footfall is therefore the louder and further-carrying one on these NPCs, which reads
	// backwards and is what the file says. Any port that assumed heavy >= normal breaks here.
	{
		FElysiumClanTemplate Thug;
		if (TestTrue(TEXT("TutorialThug resolves"), Clans.Resolve(TEXT("TutorialThug"), Thug)))
		{
			const float NormalVol  = Thug.GeneralFloat(KeyNormalVol,  TemplateNormalVolDefault);
			const float NormalDist = Thug.GeneralFloat(KeyNormalDist, TemplateNormalDistDefault);
			const float HeavyVol   = Thug.GeneralFloat(KeyHeavyVol,   TemplateHeavyVolDefault);
			const float HeavyDist  = Thug.GeneralFloat(KeyHeavyDist,  TemplateHeavyDistDefault);
			TestEqual(TEXT("TutorialThug NormalFootfallVol 0.85"), NormalVol, 0.85f);
			TestEqual(TEXT("TutorialThug NormalFootfallDist 1000"), NormalDist, 1000.f);
			TestEqual(TEXT("TutorialThug HeavyFootfallVol 0.45"), HeavyVol, 0.45f);
			TestEqual(TEXT("TutorialThug HeavyFootfallDist 800"), HeavyDist, 800.f);
			TestTrue(TEXT("its walk footfall is LOUDER than its run footfall"), NormalVol > HeavyVol);
			TestTrue(TEXT("...and carries FARTHER"), NormalDist > HeavyDist);
		}
	}

	// --- No clan template authors a footfall key ---------------------------------------------------
	// `clandoc000.txt` is the player's own table. Nothing in it states a footfall value, so the
	// player's clan can never reach `0x1026d460`'s template read — which matches retail, where the
	// player's steps come from the movement clock and never from this path at all.
	{
		int32 ClanAuthors = 0;
		for (const FElysiumClanTemplate& Clan : Clans.Clans)
		{
			ClanAuthors += (Clan.HasGeneral(KeyNormalVol) || Clan.HasGeneral(KeyNormalDist)
				|| Clan.HasGeneral(KeyHeavyVol) || Clan.HasGeneral(KeyHeavyDist)) ? 1 : 0;
		}
		TestEqual(TEXT("25 clan templates"), Clans.Clans.Num(), 25);
		TestEqual(TEXT("no clan template authors a footfall key"), ClanAuthors, 0);
	}

	// --- What the authored distances buy, through `0x10228350` -------------------------------------
	// The whole point of the numbers above: each one is an argument to
	// `int(20*log10(d/36) + 40)`. These are every distance the shipped corpus authors, plus the two
	// cvar defaults the template path falls back from.
	TestEqual(TEXT("256 units is level 57"), ElysiumSoundLevel::FromDistanceUnits(256.f), 57);
	TestEqual(TEXT("300 units is level 58"), ElysiumSoundLevel::FromDistanceUnits(300.f), 58);
	TestEqual(TEXT("512 units is level 63"), ElysiumSoundLevel::FromDistanceUnits(512.f), 63);
	TestEqual(TEXT("600 units is level 64"), ElysiumSoundLevel::FromDistanceUnits(600.f), 64);
	TestEqual(TEXT("800 units is level 66"), ElysiumSoundLevel::FromDistanceUnits(800.f), 66);
	TestEqual(TEXT("1000 units is level 68"), ElysiumSoundLevel::FromDistanceUnits(1000.f), 68);
	TestEqual(TEXT("1400 units is level 71"), ElysiumSoundLevel::FromDistanceUnits(1400.f), 71);
	TestEqual(TEXT("1700 units is level 73"), ElysiumSoundLevel::FromDistanceUnits(1700.f), 73);
	// The museum guard's own run step is only two dB under the player's fixed 75 — the loudest
	// authored footfall in the game.
	TestTrue(TEXT("the loudest authored NPC step stays under the player's level 75"),
		ElysiumSoundLevel::FromDistanceUnits(1700.f) < ElysiumSoundLevel::PlayerStepLevelDb);

	return true;
}

// --- Elysium.Content.FootstepTuningDefaults -----------------------------------------------------
//
// The seven cvars as `vampire.dll` constructs them. This is the CONTENT-tier restatement of the
// values: `Elysium.Substrate.Footsteps.Tuning` proves the store round-trip and the parse refusal;
// what is pinned here is the retail number itself, beside the corpus that overrides it above.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFootstepTuningDefaultsTest,
	"Elysium.Content.FootstepTuningDefaults", GElysiumTestFlags)
bool FElysiumFootstepTuningDefaultsTest::RunTest(const FString&)
{
	// Not `const`: the type carries a member whose default comes from its own constructor.
	FElysiumFootstepTuning Defaults;
	TestTrue(TEXT("footstep_npc_use_templates defaults to 1"), Defaults.bNpcUseTemplates);
	TestEqual(TEXT("footstep_normal_vol defaults to 0.5"), Defaults.NormalVolume, 0.5f);
	TestEqual(TEXT("footstep_normal_dist defaults to 256"), Defaults.NormalDistanceUnits, 256.f);
	TestEqual(TEXT("footstep_heavy_vol defaults to 0.85"), Defaults.HeavyVolume, 0.85f);
	TestEqual(TEXT("footstep_heavy_dist defaults to 512"), Defaults.HeavyDistanceUnits, 512.f);
	TestEqual(TEXT("footstep_pc_vol defaults to 0.5"), Defaults.PlayerVolume, 0.5f);
	TestTrue(TEXT("sv_footsteps defaults to 1"), Defaults.bServerFootsteps);

	// --- The declaration names exactly these seven, with exactly these defaults -------------------
	// A cvar the console does not declare cannot be typed, and a default that disagreed with the
	// struct would make the same name read two values depending on whether a cfg had been loaded.
	struct FExpectedCvar
	{
		const TCHAR* Name;
		const TCHAR* Default;
	};
	const FExpectedCvar Expected[] =
	{
		{ TEXT("footstep_npc_use_templates"), TEXT("1")    },   // cvar `0x109203f8`
		{ TEXT("footstep_normal_vol"),        TEXT("0.5")  },   // cvar `0x10920440`
		{ TEXT("footstep_normal_dist"),       TEXT("256")  },   // cvar `0x10920280`
		{ TEXT("footstep_heavy_vol"),         TEXT("0.85") },   // cvar `0x10920160`
		{ TEXT("footstep_heavy_dist"),        TEXT("512")  },   // cvar `0x109204a8`
		{ TEXT("footstep_pc_vol"),            TEXT("0.5")  },   // cvar `0x1070b2d0`
		{ TEXT("sv_footsteps"),               TEXT("1")    },   // cvar `0x109ef090`
	};

	TArrayView<const ElysiumFootstep::FCvarDef> Defs = ElysiumFootstep::CvarDefs();
	TestEqual(TEXT("seven footstep cvars are declared"),
		Defs.Num(), static_cast<int32>(UE_ARRAY_COUNT(Expected)));

	TMap<FString, FString> Declared;
	for (const ElysiumFootstep::FCvarDef& Def : Defs)
	{
		TestTrue(FString::Printf(TEXT("%s carries help text"), Def.Name),
			Def.Help != nullptr && *Def.Help != TEXT('\0'));
		Declared.Add(Def.Name, Def.Default != nullptr ? Def.Default : TEXT(""));
	}
	for (const FExpectedCvar& Row : Expected)
	{
		const FString* Found = Declared.Find(Row.Name);
		if (!TestNotNull(FString::Printf(TEXT("%s is declared"), Row.Name), Found))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s defaults to '%s'"), Row.Name, Row.Default),
			*Found, FString(Row.Default));
	}
	// Nothing EXTRA: the surface is the seven VtMB names and no `elysium.*` invention beside them.
	for (const ElysiumFootstep::FCvarDef& Def : Defs)
	{
		bool bNamed = false;
		for (const FExpectedCvar& Row : Expected)
		{
			bNamed = bNamed || FCString::Strcmp(Def.Name, Row.Name) == 0;
		}
		TestTrue(FString::Printf(TEXT("%s is one of the seven recovered names"), Def.Name), bNamed);
	}

	// The mode selection `0x1026d460` makes when `footstep_npc_use_templates` is zero.
	TestEqual(TEXT("normal mode reads the normal cvar pair"), Defaults.VolumeFor(false), 0.5f);
	TestEqual(TEXT("...and its distance"), Defaults.DistanceUnitsFor(false), 256.f);
	TestEqual(TEXT("heavy mode reads the heavy cvar pair"), Defaults.VolumeFor(true), 0.85f);
	TestEqual(TEXT("...and its distance"), Defaults.DistanceUnitsFor(true), 512.f);

	return true;
}

// --- Elysium.Content.FootstepSurfacesForSteps ---------------------------------------------------
//
// `CGameMovement::UpdateStepSound` (`0x1011e940`) reaches three surfaceprops **by name** rather than
// through the ground trace — `"ladder"` (`0x10572554`), `"water"` (`0x10572544`) and `"wade"`
// (`0x1057254c`) — and the dry arm falls back to index 0, which is `default`. If any one of the four
// resolves without a step pool, that whole arm of the player's clock is silent, and nothing else in
// the suite would say so.
//
// The pool sizes are read out of `$ELYSIUM_WORK_ROOT/exports_v2/surface-properties/<name>.glb`,
// glTF JSON chunk, `extensions.ELYSIUM_vtmb_surface_property.footsteps.left` / `.right`. Same
// provenance and same shape as `Elysium.Content.SurfaceSoundTable`'s declared-pool table in
// `ElysiumFootstepSeamTests.cpp`; these four are its subset that the PLAYER's clock needs.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFootstepSurfacesForStepsTest,
	"Elysium.Content.FootstepSurfacesForSteps", GElysiumTestFlags)
bool FElysiumFootstepSurfacesForStepsTest::RunTest(const FString&)
{
	// Git-tracked under the ElysiumBaked plugin, so a fresh clone has them and needs no exporter run.
	const FString SurfaceDir = FPaths::ProjectPluginsDir()
		/ TEXT("ElysiumBaked/Content/SurfaceProperties");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(SurfaceDir / TEXT("PM_*.uasset")), true, false);
	if (Files.IsEmpty())
	{
		AddInfo(FString::Printf(
			TEXT("ELYSIUM_TEST_ABSTAIN: no baked surfaceprop assets at %s"), *SurfaceDir));
		return true;
	}

	// The real `IElysiumEmbodiment::ResolveSurfaceSounds`, on the class default object:
	// `AElysiumMapActor::ResolveSurfaceSounds` reads no actor state, so a spawned actor and a world
	// would prove nothing extra. Same shape `Elysium.Content.SurfaceSoundTable` uses.
	const AElysiumMapActor* Map = GetDefault<AElysiumMapActor>();
	if (!TestNotNull(TEXT("a map actor to answer the surface query"), Map))
	{
		return false;
	}
	const IElysiumEmbodiment& Embodiment = *Map;

	struct FClockSurface
	{
		const TCHAR* Surface;
		int32 Left;
		int32 Right;
		const TCHAR* Arm;
	};
	const FClockSurface ClockSurfaces[] =
	{
		{ TEXT("ladder"),  2, 2, TEXT("the ladder arm, volume 0.35, 350 ms") },
		{ TEXT("water"),   2, 2, TEXT("water level 1, volume 1.0, 400/300 ms") },
		{ TEXT("wade"),    2, 2, TEXT("water level >= 2, volume 1.0, 600 ms, one step in four silent") },
		{ TEXT("default"), 4, 4, TEXT("the dry arm's surfaceprop index 0") },
	};

	for (const FClockSurface& Row : ClockSurfaces)
	{
		FElysiumSurfaceSounds Sounds;
		if (!TestTrue(FString::Printf(TEXT("'%s' resolves (%s)"), Row.Surface, Row.Arm),
			Embodiment.ResolveSurfaceSounds(FName(Row.Surface), Sounds)))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("'%s' carries a step pool"), Row.Surface), Sounds.HasSteps());
		TestEqual(FString::Printf(TEXT("'%s' stepleft pool size"), Row.Surface),
			Sounds.StepLeft.Num(), Row.Left);
		TestEqual(FString::Printf(TEXT("'%s' stepright pool size"), Row.Surface),
			Sounds.StepRight.Num(), Row.Right);
		// Both feet, because `m_nStepside` (`player+0x1ee4`) alternates between them every emit and a
		// one-sided surface would stutter rather than alternate.
		TestFalse(FString::Printf(TEXT("'%s' has a left foot"), Row.Surface),
			Sounds.StepLeft.IsEmpty());
		TestFalse(FString::Printf(TEXT("'%s' has a right foot"), Row.Surface),
			Sounds.StepRight.IsEmpty());
		for (const FString& Wav : Sounds.StepLeft)
		{
			TestFalse(FString::Printf(TEXT("'%s' left steps carry no asset-id prefix"), Row.Surface),
				Wav.StartsWith(TEXT("vtmb:")));
		}
		AddInfo(FString::Printf(TEXT("%s -> %d/%d steps, gamematerial '%s' (%s)"),
			Row.Surface, Sounds.StepLeft.Num(), Sounds.StepRight.Num(),
			*Sounds.GameMaterial, Row.Arm));
	}

	// --- The dry arm's own volume selector ---------------------------------------------------------
	// `CGameMovement::m_chTextureType` is `surfacedata_t+0x70`, the gamematerial letter, and the dry
	// arm switches on it: `'D'` 0.25/0.55, `'V'` 0.40/0.70, everything else 0.20/0.50. `default`
	// declaring `C` is therefore what makes an unmaterialed floor take the DEFAULT pair — the
	// letter has to survive the bake for that arm to be reachable at all.
	{
		FElysiumSurfaceSounds DryFallback;
		if (TestTrue(TEXT("'default' resolves"),
			Embodiment.ResolveSurfaceSounds(FName(TEXT("default")), DryFallback)))
		{
			TestEqual(TEXT("'default' declares gamematerial C"),
				DryFallback.GameMaterial, FString(TEXT("C")));
			// The first alternate, spelled the way `PlayVoice`/`PlayBodySound` take it.
			TestEqual(TEXT("'default' first left step"),
				DryFallback.StepLeft.IsEmpty() ? FString() : DryFallback.StepLeft[0],
				FString(TEXT("surfaces/stepleft1.wav")));
		}

		// `wade` bases on `water` and declares its own step pool, so the flattening must NOT hand it
		// water's: the two are different wavs and the silent-phase arm plays only the wade ones.
		FElysiumSurfaceSounds Wade;
		FElysiumSurfaceSounds Water;
		if (Embodiment.ResolveSurfaceSounds(FName(TEXT("wade")), Wade)
			&& Embodiment.ResolveSurfaceSounds(FName(TEXT("water")), Water))
		{
			TestTrue(TEXT("wade's own pool is not water's"),
				Wade.StepLeft.IsEmpty() || Water.StepLeft.IsEmpty()
					|| Wade.StepLeft[0] != Water.StepLeft[0]);
			TestEqual(TEXT("wade's first left step"),
				Wade.StepLeft.IsEmpty() ? FString() : Wade.StepLeft[0],
				FString(TEXT("surfaces/wade/stepleft1.wav")));
			TestEqual(TEXT("water's first left step"),
				Water.StepLeft.IsEmpty() ? FString() : Water.StepLeft[0],
				FString(TEXT("surfaces/water/stepleft1.wav")));
		}
	}

	return true;
}

// --- Elysium.Content.FootstepEventCensus --------------------------------------------------------
//
// The other half of the NPC producer: retail's `2050`–`2053` records, counted in the runtime's own
// clip metadata rather than in the export. Every link between `mstudioevent_t` and
// `UElysiumClipData::Events` is silent when it breaks, so "the handler claims 2050" proves nothing
// about whether any clip this runtime plays ever fires one.
//
// **Scope.** The shipped corpus carries 2050: 228, 2051: 227, 2052: 77, 2053: 77 over all 4,445
// models (`docs/vtmb/animation_events.md`). This case walks the SHARED MALE AND FEMALE BANKS only —
// every `vtmb:model:character/shared/{male,female}/*` model in `DA_Cast`, 77 of them — which is
// where every ordinary humanoid's locomotion comes from. That subset is 2050: 72, 2051: 72,
// 2052: **77**, 2053: **77** — all of the heavy (run) records in the game are here. The remaining
// 156 `2050` and 155 `2051` records live on the nine monster bodies (`gargoyle` 58/76, `werewolf`
// and `werewolf_damaged` 26/16 each, tzimisce `creation1_full` 33/35, `creation2_full` 2/2,
// `tzim3` 4/4, `hengeyokai` 4/6, `mingxiao` 2/0, `andrei` 1/0), whose species overrides are out of
// scope (`footsteps.md` §1.7) and none of which carries a run footfall at all.

namespace
{
	struct FBankFootfall
	{
		const TCHAR* Bank;
		int32 WalkLeft;    // 2050
		int32 WalkRight;   // 2051
		int32 RunLeft;     // 2052
		int32 RunRight;    // 2053
	};

	// The nine shared banks that carry a footfall record at all, and what each carries. Every other
	// shared bank carries none, which the union check below is what proves.
	const FBankFootfall GBankFootfalls[] =
	{
		{ TEXT("character_shared_female_frenzy"),                        0,  0,  3,  3 },
		{ TEXT("character_shared_female_misc"),                          4,  4,  0,  0 },
		{ TEXT("character_shared_female_move_and_ranged"),              32, 32, 34, 34 },
		{ TEXT("character_shared_male_frenzy"),                          0,  0,  3,  3 },
		{ TEXT("character_shared_male_misc"),                            4,  4,  0,  0 },
		{ TEXT("character_shared_male_move_and_ranged"),                32, 32, 34, 34 },
		{ TEXT("character_shared_male_runbrujah_pcidles_allsequences"),  0,  0,  1,  1 },
		{ TEXT("character_shared_male_runmalknos_pcidles_allsequences"), 0,  0,  1,  1 },
		{ TEXT("character_shared_male_runotherspc_pcidles_allsequences"),0,  0,  1,  1 },
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFootstepEventCensusTest,
	"Elysium.Content.FootstepEventCensus", GElysiumTestFlags)
bool FElysiumFootstepEventCensusTest::RunTest(const FString&)
{
	// `EventWalkLeft`/`EventWalkRight`/`EventRunLeft`/`EventRunRight`, `IsFootstepEvent` and
	// `IsHeavyFootstep` — the port's own vocabulary for the band, not a second copy of it.
	using namespace ElysiumFootsteps;

	if (!ElysiumNativeTest::HasCast())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: native DA_Cast is absent "
			"(run: uv run elysium import characters)"));
		return true;
	}
	const UElysiumCastData* CastData = ElysiumCharacterAssets::Cast();
	if (!TestNotNull(TEXT("DA_Cast resolves"), CastData))
	{
		return false;
	}

	// Every shared male/female bank, addressed by ASSET ID rather than by stem: three of them
	// (`runbrujah_pcidles_allsequences` and its two siblings) carry a short stem that does not
	// name the family, and they are exactly the banks the PC's run clip lives in.
	TArray<FString> BankIds;
	for (const TPair<FString, FElysiumCastModel>& Pair : CastData->Models)
	{
		if (Pair.Key.StartsWith(TEXT("vtmb:model:character/shared/male/"))
			|| Pair.Key.StartsWith(TEXT("vtmb:model:character/shared/female/")))
		{
			BankIds.Add(Pair.Key);
		}
	}
	BankIds.Sort();
	if (!TestTrue(TEXT("the shared male/female banks are in the cast"), BankIds.Num() > 0))
	{
		return false;
	}
	AddInfo(FString::Printf(TEXT("shared male/female bank models in DA_Cast: %d"), BankIds.Num()));

	// The union, and the per-bank tallies keyed by the same owner name the bank table above uses.
	TMap<int32, int32> Totals;
	TMap<FString, TMap<int32, int32>> PerBank;
	// Distinct SEQUENCE LABELS, over all banks: retail's own corroboration is "all 37 clips carrying
	// 2052/2053 are `*_run`", which is a count of labels and not of records.
	TSet<FString> NormalLabels;
	TSet<FString> HeavyLabels;
	// Every heavy record's cycle, and every clip label that carries one — the two facts
	// `docs/vtmb/animation_events.md` states about the run band.
	TArray<float> RunLeftCycles;
	TArray<float> RunRightCycles;
	TArray<FString> HeavyOnNonRunClip;
	int32 BodiesRead = 0;
	int32 BodiesMissing = 0;
	// A label whose timeline is reachable ONLY as a blend space. `FElysiumBlendTable::Events` is
	// built from `NativeSequences` alone, so such a label would be invisible to every consumer that
	// reads the table rather than `UElysiumNativeAnimationData::ClipData`.
	int32 GridOnlyLabels = 0;

	for (const FString& BankId : BankIds)
	{
		const FString Owner = ElysiumNativeTest::CaptureOwner(BankId);
		FString Error;
		const UElysiumBodyData* Body = ElysiumCharacterAssets::Body(Owner, Error);
		if (Body == nullptr)
		{
			// A cast row whose bank was not baked — the ten `*allsequences` / `*npcsequences`
			// umbrellas own no sequence of their own and are legitimately absent.
			++BodiesMissing;
			continue;
		}
		// **The runtime's own clip table**, built exactly as `UElysiumNativeAnimationData::BlendTable`
		// builds it: `Events` keyed by SOURCE LABEL, so the plain `A_<label>` sequence the bake emits
		// beside every `BS_<label>` grid collapses onto one entry rather than doubling the count.
		FElysiumBlendTable Table;
		if (!ElysiumNativeTest::Load(Table, Owner, Error))
		{
			return false;   // `Load` already raised the error
		}
		++BodiesRead;

		TMap<int32, int32>& Bank = PerBank.Add(Owner);
		for (const TPair<FString, TArray<FElysiumAnimEvent>>& Pair : Table.Events)
		{
			for (const FElysiumAnimEvent& Record : Pair.Value)
			{
				if (!IsFootstepEvent(Record.Event))
				{
					continue;
				}
				++Bank.FindOrAdd(Record.Event);
				++Totals.FindOrAdd(Record.Event);
				if (!IsHeavyFootstep(Record.Event))
				{
					NormalLabels.Add(Pair.Key);
					continue;
				}
				HeavyLabels.Add(Pair.Key);
				(Record.Event == EventRunLeft ? RunLeftCycles : RunRightCycles)
					.Add(Record.Cycle);
				if (!Pair.Key.Contains(TEXT("run"), ESearchCase::IgnoreCase))
				{
					HeavyOnNonRunClip.Add(FString::Printf(TEXT("%s/%s"), *Owner, *Pair.Key));
				}
			}
		}

		// --- The canary --------------------------------------------------------------------------
		// `FElysiumBlendTable::Events` is built from `NativeSequences` alone, and 141 of the 149
		// footfall-carrying labels in these banks are 9-blend GRIDS. They are only visible above
		// because the bake also emits a plain sequence per grid and stamps the same timeline on it.
		// If that ever stops being true, every locomotion footfall vanishes from this table — and
		// from every consumer that reads it rather than `UElysiumNativeAnimationData::ClipData`'s
		// blend-space fallback — with no other symptom than silent walking NPCs.
		for (const TPair<FString, TSoftObjectPtr<UBlendSpace>>& Pair : Body->NativeBlendSpaces)
		{
			const UBlendSpace* Blend = Pair.Value.LoadSynchronous();
			const UElysiumClipData* Meta =
				Blend ? Blend->FindMetaDataByClass<UElysiumClipData>() : nullptr;
			if (Meta == nullptr || Table.Events.Contains(Meta->SourceLabel))
			{
				continue;
			}
			for (const FElysiumAnimEvent& Record : Meta->Events)
			{
				if (IsFootstepEvent(Record.Event))
				{
					++GridOnlyLabels;
					AddWarning(FString::Printf(
						TEXT("%s: '%s' carries footfall records only as a blend space — "
						     "FElysiumBlendTable::Events cannot see it"),
						*Owner, *Meta->SourceLabel));
					break;
				}
			}
		}
	}

	AddInfo(FString::Printf(TEXT("shared bank bodies read: %d (%d had no body data)"),
		BodiesRead, BodiesMissing));
	if (!TestTrue(TEXT("the shared bank bodies load"), BodiesRead > 0))
	{
		return false;
	}

	// --- The nine banks that carry records, each at its exact count -------------------------------
	for (const FBankFootfall& Expected : GBankFootfalls)
	{
		const TMap<int32, int32>* Bank = PerBank.Find(Expected.Bank);
		if (!TestNotNull(FString::Printf(TEXT("%s is in the cast"), Expected.Bank), Bank))
		{
			continue;
		}
		auto Count = [Bank](int32 Event)
		{
			const int32* Found = Bank->Find(Event);
			return Found ? *Found : 0;
		};
		AddInfo(FString::Printf(TEXT("%s: 2050=%d 2051=%d 2052=%d 2053=%d"),
			Expected.Bank, Count(EventWalkLeft), Count(EventWalkRight),
			Count(EventRunLeft), Count(EventRunRight)));
		TestEqual(FString::Printf(TEXT("%s carries %d 2050 records"),
			Expected.Bank, Expected.WalkLeft), Count(EventWalkLeft), Expected.WalkLeft);
		TestEqual(FString::Printf(TEXT("%s carries %d 2051 records"),
			Expected.Bank, Expected.WalkRight), Count(EventWalkRight), Expected.WalkRight);
		TestEqual(FString::Printf(TEXT("%s carries %d 2052 records"),
			Expected.Bank, Expected.RunLeft), Count(EventRunLeft), Expected.RunLeft);
		TestEqual(FString::Printf(TEXT("%s carries %d 2053 records"),
			Expected.Bank, Expected.RunRight), Count(EventRunRight), Expected.RunRight);
	}

	// --- The union over EVERY shared bank ----------------------------------------------------------
	// This is what makes the nine above the complete list: any tenth bank with a footfall record
	// pushes these totals up.
	auto Total = [&Totals](int32 Event)
	{
		const int32* Found = Totals.Find(Event);
		return Found ? *Found : 0;
	};
	TestEqual(TEXT("the shared banks carry 72 2050 records"), Total(EventWalkLeft), 72);
	TestEqual(TEXT("...72 2051 records"), Total(EventWalkRight), 72);
	// The whole game's run band is here.
	TestEqual(TEXT("...and all 77 of the corpus's 2052 records"), Total(EventRunLeft), 77);
	TestEqual(TEXT("...and all 77 of its 2053"), Total(EventRunRight), 77);
	TestEqual(TEXT("2050 and 2051 come in matched pairs"),
		Total(EventWalkLeft), Total(EventWalkRight));
	TestEqual(TEXT("...and so do 2052 and 2053"), Total(EventRunLeft), Total(EventRunRight));

	// --- Every heavy record is on a run clip, at one of the four authored cycles --------------------
	// `docs/vtmb/animation_events.md`: "all 37 clips carrying 2052/2053 are `*_run`, at cycles 1/3,
	// 5/9 and 7/9, 8/9". Both halves, over the runtime's own tables.
	TestEqual(TEXT("37 distinct clip labels carry the run band"), HeavyLabels.Num(), 37);
	TestEqual(TEXT("36 distinct clip labels carry the walk band"), NormalLabels.Num(), 36);
	if (!HeavyOnNonRunClip.IsEmpty())
	{
		AddError(FString::Printf(TEXT("2052/2053 on a clip that is not a run: %s"),
			*FString::Join(HeavyOnNonRunClip, TEXT(", "))));
	}
	TestEqual(TEXT("no heavy footfall sits on a clip that is not a run"),
		HeavyOnNonRunClip.Num(), 0);

	const float OneThird = 1.f / 3.f;
	const float FiveNinths = 5.f / 9.f;
	const float SevenNinths = 7.f / 9.f;
	const float EightNinths = 8.f / 9.f;
	int32 BadCycles = 0;
	for (const float Cycle : RunLeftCycles)
	{
		// 2052 is the LEFT run footfall, and the two clip shapes place it at 1/3 (the plain
		// locomotion grids) or 7/9 (the `*_alt` claw and frenzy runs).
		const bool bKnown = FMath::IsNearlyEqual(Cycle, OneThird, 1e-3f)
			|| FMath::IsNearlyEqual(Cycle, SevenNinths, 1e-3f);
		BadCycles += bKnown ? 0 : 1;
		if (!bKnown)
		{
			AddError(FString::Printf(TEXT("a 2052 record sits at cycle %.6f"), Cycle));
		}
	}
	for (const float Cycle : RunRightCycles)
	{
		const bool bKnown = FMath::IsNearlyEqual(Cycle, FiveNinths, 1e-3f)
			|| FMath::IsNearlyEqual(Cycle, EightNinths, 1e-3f);
		BadCycles += bKnown ? 0 : 1;
		if (!bKnown)
		{
			AddError(FString::Printf(TEXT("a 2053 record sits at cycle %.6f"), Cycle));
		}
	}
	TestEqual(TEXT("every run footfall sits at 1/3, 5/9, 7/9 or 8/9"), BadCycles, 0);

	// --- What the census exists to catch -----------------------------------------------------------
	// Locomotion footfalls live on 9-blend grids (`walk`, `run`, and every weapon's own pair). The
	// bake emits a plain sequence beside each grid and stamps the same timeline on it, which is the
	// only reason `FElysiumBlendTable::Events` can see them at all. If that ever stops being true
	// the warning above fires and this fails, long before it shows up as a silent walking NPC.
	TestEqual(TEXT("no footfall timeline is reachable only as a blend space"), GridOnlyLabels, 0);

	// **A recovery worth stating.** `panic_run*` in the two `misc` banks carries 2050/2051 — the
	// NORMAL footfall — on a clip that is a sprint. The walk/run split is the AUTHORED mode and not
	// the gait, so a panicking civilian's sprint plays the quiet step. Four labels, both banks.
	{
		int32 PanicRuns = 0;
		for (const FString& Label : NormalLabels)
		{
			PanicRuns += Label.StartsWith(TEXT("panic_run"), ESearchCase::IgnoreCase) ? 1 : 0;
		}
		TestEqual(TEXT("the four panic_run clips carry the WALK band, not the run band"),
			PanicRuns, 4);
		for (const FString& Label : HeavyLabels)
		{
			TestFalse(TEXT("no panic_run clip carries the run band"),
				Label.StartsWith(TEXT("panic_run"), ESearchCase::IgnoreCase));
		}
	}

	return true;
}

} // namespace ElysiumFootstepContentTests

#endif // WITH_DEV_AUTOMATION_TESTS
