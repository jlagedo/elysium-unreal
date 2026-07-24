// P2.8 — the content-gated tier. These read the offline pipeline's real exported intermediates
// from tools/out and assert the parse contract holds against actual game data. They SELF-SKIP (log
// + pass) when the map has not been exported, so a fresh checkout with an empty tools/out stays
// green; a machine that has run the exporter gets real regression coverage of the `.ents` decode.
//
// The app-context mask (not ClientContext alone) so they run in the editor commandlet test.bat
// drives as well as in a game/client session — the `.ents` are read from disk through
// FElysiumContentPaths, which resolves the same in either. ProductFilter keeps them in this
// project's own suite bucket, out of the per-commit smoke set where a missing export would look
// like noise.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumClassRegistry.h"
#include "ElysiumContentPaths.h"
#include "ElysiumDecals.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumObjModel.h"
#include "HAL/FileManager.h"

static constexpr EAutomationTestFlags GElysiumContentTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// Count entities of a classname, and collect info_landmark targetnames — the two things the
	// integration assertions turn on (class coverage, and the anchors the P4.6 travel path needs).
	struct FEntsSurvey
	{
		int32 Total = 0;
		int32 WithOutputs = 0;
		TSet<FString> Classnames;
		TSet<FString> LandmarkNames;

		int32 CountClass(const TCHAR* Class) const
		{
			return Classnames.Contains(FString(Class)) ? 1 : 0;   // presence, not multiplicity
		}
	};

	// Parse a map's `.ents` if it was exported. Returns false (and logs a skip) when absent.
	bool SurveyMap(FAutomationTestBase& Test, const TCHAR* Map, FEntsSurvey& Out)
	{
		const FString Path = FElysiumContentPaths::MapEnts(Map);
		if (!IFileManager::Get().FileExists(*Path))
		{
			Test.AddInfo(FString::Printf(
				TEXT("skipping %s: no exported .ents at %s (run the pipeline to enable this test)"), Map, *Path));
			return false;
		}

		FElysiumEntityDefs Defs;
		if (!Test.TestTrue(FString::Printf(TEXT("%s parses"), Map), FElysiumEntityDefs::Parse(Path, Defs)))
		{
			return false;
		}

		Out.Total = Defs.Num();
		for (const FElysiumEntityDef& Def : Defs.Defs)
		{
			Out.Classnames.Add(Def.Classname);
			if (Def.Outputs.Num() > 0)
			{
				++Out.WithOutputs;
			}
			if (Def.Classname.Equals(TEXT("info_landmark"), ESearchCase::IgnoreCase) && !Def.TargetName.IsEmpty())
			{
				Out.LandmarkNames.Add(Def.TargetName);
			}
		}
		return true;
	}
}

// =====================================================================================
// sp_tutorial_1 — the canonical vertical slice. Its shape is the roadmap baseline.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTutorialEntsTest,
	"Elysium.Content.TutorialEnts", GElysiumContentTestFlags)
bool FElysiumTutorialEntsTest::RunTest(const FString&)
{
	FEntsSurvey Survey;
	if (!SurveyMap(*this, TEXT("sp_tutorial_1"), Survey))
	{
		return true;   // not exported — skip, stay green
	}

	// The exported `.ents` holds 1,868 records across 80 classnames. Assert a generous band so a
	// real regression (a decoder dropping entities) trips, but a data revision does not.
	TestTrue(TEXT("entity count in the expected band"), Survey.Total > 1600 && Survey.Total < 2100);
	TestTrue(TEXT("classname variety in the expected band"),
		Survey.Classnames.Num() > 60 && Survey.Classnames.Num() < 110);

	// The slice's load-bearing classes must be present.
	TestTrue(TEXT("has logic_relay"), Survey.Classnames.Contains(TEXT("logic_relay")));
	TestTrue(TEXT("has math_counter"), Survey.Classnames.Contains(TEXT("math_counter")));
	TestTrue(TEXT("has info_landmark"), Survey.Classnames.Contains(TEXT("info_landmark")));

	// Some entities wire outputs (the I/O graph is non-empty).
	TestTrue(TEXT("has entities with outputs"), Survey.WithOutputs > 0);

	// The story-entry landmark the New Game / travel path enters at must exist.
	TestTrue(TEXT("carries the 'tutorial' info_landmark"),
		Survey.LandmarkNames.Contains(TEXT("tutorial")));

	// B3 — the beat's NPCs are in the slice and their classes now register (Jack + the maker resolve
	// to real leaves, not inert records, so trig_off_porch's wires deliver instead of `[no input]`).
	TestTrue(TEXT("slice has npc_VVampire (Jack)"), Survey.Classnames.Contains(TEXT("npc_VVampire")));
	TestTrue(TEXT("slice has npc_maker (blueblood_maker)"), Survey.Classnames.Contains(TEXT("npc_maker")));
	const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
	TestNotNull(TEXT("npc_VVampire registered"), Reg.Find(FName(TEXT("npc_VVampire"))));
	TestNotNull(TEXT("npc_maker registered"), Reg.Find(FName(TEXT("npc_maker"))));

	return true;
}

// =====================================================================================
// sm_pawnshop_1 — the cross-map travel destination. Confirms it parses and offers a
// landmark to travel to (the P4.6 precondition), without standing up a live travel.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPawnshopEntsTest,
	"Elysium.Content.PawnshopEnts", GElysiumContentTestFlags)
bool FElysiumPawnshopEntsTest::RunTest(const FString&)
{
	FEntsSurvey Survey;
	if (!SurveyMap(*this, TEXT("sm_pawnshop_1"), Survey))
	{
		return true;   // not exported — skip
	}

	TestTrue(TEXT("has entities"), Survey.Total > 0);
	// The travel target needs at least one info_landmark to seat the incoming player on.
	TestTrue(TEXT("offers a landmark to travel to"), Survey.LandmarkNames.Num() > 0);

	return true;
}

// =====================================================================================
// Decals (7.2) — the `<map>.decals` projector sidecar. Validates that every line is a
// well-formed projector (unit normal, positive extents) and that its material resolves in
// the shared `<map>.mtl`, so the runtime always finds a texture for each UDecalComponent.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTutorialDecalsTest,
	"Elysium.Content.TutorialDecals", GElysiumContentTestFlags)
bool FElysiumTutorialDecalsTest::RunTest(const FString&)
{
	const TCHAR* Map = TEXT("sp_tutorial_1");
	const FString Path = FElysiumContentPaths::MapDecals(Map);
	if (!IFileManager::Get().FileExists(*Path))
	{
		AddInfo(FString::Printf(
			TEXT("skipping %s decals: no exported .decals at %s (run the pipeline to enable)"), Map, *Path));
		return true;   // not exported — skip, stay green
	}

	TArray<FElysiumDecalDef> Defs;
	if (!TestTrue(TEXT("decals sidecar parses"), FElysiumDecals::Parse(Path, Defs)))
	{
		return true;
	}
	TestTrue(TEXT("tutorial carries decals"), Defs.Num() > 0);

	// Materials ride the shared world MTL — the same file the runtime's BuildDecals reads.
	TMap<FString, FElysiumMaterialDef> Materials;
	FElysiumObjModel::ParseMtl(
		FElysiumContentPaths::MapDir(Map) / (FString(Map) + TEXT(".mtl")), Materials);

	int32 BadNormal = 0, BadExtent = 0, Unresolved = 0;
	for (const FElysiumDecalDef& D : Defs)
	{
		if (!FMath::IsNearlyEqual(static_cast<float>(D.Normal.Size()), 1.0f, 1.e-2f))
		{
			++BadNormal;
		}
		if (D.HalfW <= 0.f || D.HalfH <= 0.f)
		{
			++BadExtent;
		}
		if (!Materials.Contains(D.Mat))
		{
			++Unresolved;
		}
	}
	TestEqual(TEXT("every decal normal is unit length"), BadNormal, 0);
	TestEqual(TEXT("every decal has positive extents"), BadExtent, 0);
	TestEqual(TEXT("every decal material resolves in the shared MTL"), Unresolved, 0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
