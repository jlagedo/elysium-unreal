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
#include "ElysiumDlg.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumObjModel.h"
#include "ElysiumRopes.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

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
		// 8.3 — prop_dynamic coverage: how many carry the 8.1 model_mesh annotation the runtime
		// renders through, and one sample stem to confirm its decoded OBJ exists on disk.
		int32 PropDynamic = 0;
		int32 PropDynamicWithMesh = 0;
		FString AnyPropStem;
		// 8.4 — prop_physics carry the same model_mesh annotation plus a decomposed `.hulls`
		// sidecar; phys_hinge carry the pre-converted hinge_axis. Sample stems confirm on disk.
		int32 PropPhysics = 0;
		int32 PropPhysicsWithMesh = 0;
		FString AnyPhysStem;
		int32 PhysHinge = 0;
		int32 PhysHingeWithAxis = 0;

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
			if (Def.Classname.Equals(TEXT("prop_dynamic"), ESearchCase::IgnoreCase))
			{
				++Out.PropDynamic;
				if (!Def.ModelMesh.IsEmpty())
				{
					++Out.PropDynamicWithMesh;
					Out.AnyPropStem = Def.ModelMesh;
				}
			}
			if (Def.Classname.Equals(TEXT("prop_physics"), ESearchCase::IgnoreCase))
			{
				++Out.PropPhysics;
				if (!Def.ModelMesh.IsEmpty())
				{
					++Out.PropPhysicsWithMesh;
					Out.AnyPhysStem = Def.ModelMesh;
				}
			}
			if (Def.Classname.Equals(TEXT("phys_hinge"), ESearchCase::IgnoreCase))
			{
				++Out.PhysHinge;
				if (!Def.HingeAxis.IsNearlyZero())
				{
					++Out.PhysHingeWithAxis;
				}
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

	// 8.3 — dynamic props render through the model_mesh annotation. The class registers, and the
	// slice's prop_dynamic records carry a decoded OBJ the runtime can stand (the annotation is 8.1's,
	// so this holds on any current export; model_quat defaults to identity on an older one).
	TestNotNull(TEXT("prop_dynamic registered"), Reg.Find(FName(TEXT("prop_dynamic"))));
	if (Survey.PropDynamic > 0)
	{
		TestTrue(TEXT("prop_dynamic records carry a model_mesh"), Survey.PropDynamicWithMesh > 0);
		if (!Survey.AnyPropStem.IsEmpty())
		{
			const FString Obj =
				FElysiumContentPaths::MapPropsDir(TEXT("sp_tutorial_1")) / (Survey.AnyPropStem + TEXT(".obj"));
			TestTrue(TEXT("a prop_dynamic model_mesh resolves to an OBJ on disk"),
				IFileManager::Get().FileExists(*Obj));
		}
	}
	AddInfo(FString::Printf(TEXT("sp_tutorial_1 prop_dynamic: %d (%d with model_mesh)"),
		Survey.PropDynamic, Survey.PropDynamicWithMesh));

	// 8.4 — physics props/hinges. prop_physics/phys_hinge register; a prop_physics record carries a
	// decoded model_mesh + a `.hulls` collision sidecar (single or decomposed), and phys_hinge carries
	// the pre-converted hinge_axis. Guarded on presence so the tier holds on any current export.
	TestNotNull(TEXT("prop_physics registered"), Reg.Find(FName(TEXT("prop_physics"))));
	TestNotNull(TEXT("phys_hinge registered"), Reg.Find(FName(TEXT("phys_hinge"))));
	if (Survey.PropPhysics > 0)
	{
		TestTrue(TEXT("prop_physics records carry a model_mesh"), Survey.PropPhysicsWithMesh > 0);
		if (!Survey.AnyPhysStem.IsEmpty())
		{
			const FString Dir = FElysiumContentPaths::MapPropsDir(TEXT("sp_tutorial_1"));
			TestTrue(TEXT("a prop_physics model_mesh resolves to an OBJ on disk"),
				IFileManager::Get().FileExists(*(Dir / (Survey.AnyPhysStem + TEXT(".obj")))));
			TestTrue(TEXT("a prop_physics model carries a .hulls collision sidecar"),
				IFileManager::Get().FileExists(*(Dir / (Survey.AnyPhysStem + TEXT(".hulls")))));
		}
	}
	if (Survey.PhysHinge > 0)
	{
		TestTrue(TEXT("phys_hinge records carry a pre-converted hinge_axis"), Survey.PhysHingeWithAxis > 0);
	}
	AddInfo(FString::Printf(TEXT("sp_tutorial_1 prop_physics: %d (%d with model_mesh); phys_hinge: %d (%d with axis)"),
		Survey.PropPhysics, Survey.PropPhysicsWithMesh, Survey.PhysHinge, Survey.PhysHingeWithAxis));

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

// =====================================================================================
// Ropes (8.7) — the `<map>.ropes` cable sidecar. Validates that every segment is a well-formed
// cable (distinct endpoints, positive width, non-negative slack, at least one subdivision) and
// that its decoded RopeMaterial texture exists on disk, so the runtime's BuildRopes always finds
// an albedo for each UCableComponent. The tutorial strings its telephone lines this way.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTutorialRopesTest,
	"Elysium.Content.TutorialRopes", GElysiumContentTestFlags)
bool FElysiumTutorialRopesTest::RunTest(const FString&)
{
	const TCHAR* Map = TEXT("sp_tutorial_1");
	const FString Path = FElysiumContentPaths::MapRopes(Map);
	if (!IFileManager::Get().FileExists(*Path))
	{
		AddInfo(FString::Printf(
			TEXT("skipping %s ropes: no exported .ropes at %s (run the pipeline to enable)"), Map, *Path));
		return true;   // not exported — skip, stay green
	}

	TArray<FElysiumRopeDef> Defs;
	if (!TestTrue(TEXT("ropes sidecar parses"), FElysiumRopes::Parse(Path, Defs)))
	{
		return true;
	}
	TestTrue(TEXT("tutorial carries cable segments"), Defs.Num() > 0);

	const FString Dir = FElysiumContentPaths::MapDir(Map);
	int32 Degenerate = 0, BadWidth = 0, BadSlack = 0, BadSubdiv = 0, MissingTex = 0;
	for (const FElysiumRopeDef& D : Defs)
	{
		if (FVector::DistSquared(D.A, D.B) < 1.0)   // endpoints < 1 cm apart — no cable to draw
		{
			++Degenerate;
		}
		if (D.WidthCm <= 0.f)
		{
			++BadWidth;
		}
		if (D.SlackCm < 0.f)
		{
			++BadSlack;
		}
		if (D.Subdiv < 1)
		{
			++BadSubdiv;
		}
		// A "-" tex is a legitimate decode miss (runtime uses a plain MID); a named tex must exist.
		if (D.Tex != TEXT("-") && !IFileManager::Get().FileExists(*(Dir / D.Tex)))
		{
			++MissingTex;
		}
	}
	TestEqual(TEXT("every cable has distinct endpoints"), Degenerate, 0);
	TestEqual(TEXT("every cable has positive width"), BadWidth, 0);
	TestEqual(TEXT("every cable has non-negative slack"), BadSlack, 0);
	TestEqual(TEXT("every cable has at least one subdivision"), BadSubdiv, 0);
	TestEqual(TEXT("every named rope texture exists on disk"), MissingTex, 0);

	return true;
}

// 7.4 — the world MTL the runtime feeds FElysiumMaterialFactory. Asserts the parse contract holds
// on real exported data: materials resolve albedo, the blend flags stay mutually exclusive (so the
// factory picks exactly one master), and every referenced texture channel exists on disk.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTutorialMaterialsTest,
	"Elysium.Content.TutorialMaterials", GElysiumContentTestFlags)
bool FElysiumTutorialMaterialsTest::RunTest(const FString&)
{
	const TCHAR* Map = TEXT("sp_tutorial_1");
	const FString Dir = FElysiumContentPaths::MapDir(Map);
	const FString Path = Dir / (FString(Map) + TEXT(".mtl"));
	if (!IFileManager::Get().FileExists(*Path))
	{
		AddInfo(FString::Printf(
			TEXT("skipping %s materials: no exported .mtl at %s (run the pipeline to enable)"), Map, *Path));
		return true;   // not exported — skip, stay green
	}

	TMap<FString, FElysiumMaterialDef> Materials;
	FElysiumObjModel::ParseMtl(Path, Materials);
	if (!TestTrue(TEXT("world MTL carries materials"), Materials.Num() > 0))
	{
		return true;
	}

	int32 WithAlbedo = 0, MultiBlend = 0, MissingTex = 0;
	int32 Masked = 0, Translucent = 0, Additive = 0, Reflective = 0, Bumped = 0, Wvt = 0;
	auto TexMissing = [&Dir](const FString& Rel)
	{
		return !Rel.IsEmpty() && !IFileManager::Get().FileExists(*(Dir / Rel));
	};
	for (const TPair<FString, FElysiumMaterialDef>& Pair : Materials)
	{
		const FElysiumMaterialDef& M = Pair.Value;
		if (!M.Albedo.IsEmpty()) { ++WithAlbedo; }
		// At most one blend flag may be set — the factory maps them to disjoint masters.
		if (int32(M.bScissor) + int32(M.bBlend) + int32(M.bAdditive) > 1) { ++MultiBlend; }
		if (M.bScissor) { ++Masked; }
		if (M.bBlend) { ++Translucent; }
		if (M.bAdditive) { ++Additive; }
		if (M.bEnvmap) { ++Reflective; }
		if (!M.Bump.IsEmpty()) { ++Bumped; }
		if (!M.BaseTex2.IsEmpty()) { ++Wvt; }
		// Every named channel must resolve to a real file (the .dds sibling or the .png itself).
		for (const FString& Rel : { M.Albedo, M.Emissive, M.Bump, M.EnvMask, M.BaseTex2 })
		{
			if (TexMissing(Rel) && TexMissing(FPaths::ChangeExtension(Rel, TEXT("dds"))))
			{
				++MissingTex;
			}
		}
	}

	TestTrue(TEXT("most materials resolve an albedo"), WithAlbedo > Materials.Num() / 2);
	TestEqual(TEXT("blend flags are mutually exclusive"), MultiBlend, 0);
	TestEqual(TEXT("every referenced texture channel exists on disk"), MissingTex, 0);
	AddInfo(FString::Printf(
		TEXT("%s materials: %d total, %d albedo | masked %d, translucent %d, additive %d, reflective %d, bump %d, wvt %d"),
		Map, Materials.Num(), WithAlbedo, Masked, Translucent, Additive, Reflective, Bumped, Wvt));

	return true;
}

// =====================================================================================
// 9.1 / B4 — the `.dlg` parser + branch machine against the real jack_tutorial.dlg. Self-skips when
// the dialogue mirror has not been exported (out/dlg). Confirms the physical-format parse holds and
// that the branch machine can drive Jack's beat from entry to the `G.Tut_Jack = 1` action and END.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgJackTutorialTest, "Elysium.Content.DlgJackTutorial",
	GElysiumContentTestFlags)
bool FElysiumDlgJackTutorialTest::RunTest(const FString&)
{
	const FString Path = FElysiumContentPaths::DlgFromDialogname(TEXT("dlg/Main Characters/jack_tutorial.dlg"));
	if (!IFileManager::Get().FileExists(*Path))
	{
		AddInfo(FString::Printf(TEXT("skipping: no exported dialogue at %s (run the pipeline to enable)"), *Path));
		return true;
	}

	TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
	FString Err;
	if (!TestTrue(TEXT("jack_tutorial.dlg parses"), FElysiumDlgFile::LoadFile(Path, File.Get(), &Err)))
	{
		AddError(Err);
		return false;
	}
	// Every row is a 13-field record, so the parse should recover them all (1116 at time of writing;
	// assert a floor rather than an exact count so a patch revision does not brittle-fail the test).
	TestTrue(TEXT("recovered a full conversation"), File->Lines.Num() > 1000);

	const FElysiumDlgLine* Entry = File->FindById(11);
	if (TestNotNull(TEXT("entry line 11 present"), Entry))
	{
		TestTrue(TEXT("11 is an NPC line"), Entry->IsNpcLine());
		TestFalse(TEXT("11 has spoken text"), Entry->Text(true).IsEmpty());
		// The datum that settles NPC col-4 = action (an assignment, not a gate).
		TestTrue(TEXT("11 col-4 is the Story_State assignment"), Entry->Condition.Contains(TEXT("G.Story_State")));
	}

	// col-12 is the Malkavian-PC variant (real datum: id 23's "Okay. I could use the help." / Malkavian
	// "I shall undertake your dark tutelage."). A non-Malkavian must read col-1, a Malkavian col-12.
	const FElysiumDlgLine* Malk = File->FindById(23);
	if (TestNotNull(TEXT("choice 23 present"), Malk))
	{
		TestTrue(TEXT("23 has a col-12 Malkavian variant"), !Malk->TextMalkavian.IsEmpty());
		TestEqual(TEXT("23 non-malk reads col-1"), Malk->RawFor(true, false), Malk->Text(true));
		TestEqual(TEXT("23 malk reads col-12"), Malk->RawFor(true, true), Malk->TextMalkavian);
		TestNotEqual(TEXT("23 col-12 differs from col-1"), Malk->TextMalkavian, Malk->Text(true));
	}

	// Drive the branch machine to the acceptance action. Condition callback: pass every gate (so the
	// full choice set shows regardless of clan/patch state); at each turn prefer a choice that advances
	// Tut_Jack, else the first, so the walk is deterministic and cannot loop.
	TArray<FString> Ran;
	auto Cond = [](const FString&) { return true; };
	auto Act = [&Ran](const FString& A) { Ran.Add(A); };

	FElysiumDlgConversation Conv(File, /*bMale*/ true, /*bMalk*/ false, Cond, Act);
	Conv.Start();
	if (TestNotNull(TEXT("conversation opened"), Conv.CurrentNpcLine()))
	{
		TestEqual(TEXT("opened at entry line 11"), Conv.CurrentNpcLine()->Id, 11);
	}

	bool bSawTutJack1 = false;
	for (int32 Guard = 0; Guard < 64 && !Conv.IsOver(); ++Guard)
	{
		if (Conv.IsTerminalLine())
		{
			Conv.AdvanceTerminal();
			continue;
		}
		int32 Pick = 0;
		for (int32 v = 0; v < Conv.VisibleChoices().Num(); ++v)
		{
			if (Conv.VisibleChoice(v)->Action.Contains(TEXT("Tut_Jack = 1")))
			{
				Pick = v;
				break;
			}
		}
		const int32 Before = Ran.Num();
		Conv.Choose(Pick);
		for (int32 k = Before; k < Ran.Num(); ++k)
		{
			if (Ran[k].Contains(TEXT("Tut_Jack = 1"))) { bSawTutJack1 = true; }
		}
	}

	TestTrue(TEXT("branch walk reached the G.Tut_Jack = 1 action"), bSawTutJack1);
	TestTrue(TEXT("conversation terminated"), Conv.IsOver());
	AddInfo(FString::Printf(TEXT("jack_tutorial.dlg: %d rows, ran %d actions to reach the beat"),
		File->Lines.Num(), Ran.Num()));

	return true;
}

// =====================================================================================
// 9.1 / B4 — the whole NPC dialogue corpus of the exported test-bench maps: every `.dlg` an NPC's
// `dialogname` (or an npc_maker's) points at. Confirms the physical-format parser recovers each file
// and the branch machine drives every one to completion (or a bounded, non-crashing walk) — a broad
// robustness pass over real data, including the malformed-snippet / error-to-false conditions.
// Self-skips when out/dlg / the .ents mirror have not been exported.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgCorpusTest, "Elysium.Content.DlgCorpus", GElysiumContentTestFlags)
bool FElysiumDlgCorpusTest::RunTest(const FString&)
{
	// Discover every exported map's `.ents` and collect the distinct `dialogname` values NPCs reference.
	TArray<FString> EntsFiles;
	IFileManager::Get().FindFilesRecursive(EntsFiles, *FElysiumContentPaths::Root(), TEXT("*.ents"),
		/*Files*/ true, /*Dirs*/ false);
	if (EntsFiles.Num() == 0)
	{
		AddInfo(TEXT("skipping: no exported maps under tools/out (run the pipeline to enable)"));
		return true;
	}

	TSet<FString> DialogNames;
	TMap<FString, FString> FirstMapOf;   // dialogname -> a map that referenced it (for diagnostics)
	for (const FString& EntsPath : EntsFiles)
	{
		FElysiumEntityDefs Defs;
		if (!FElysiumEntityDefs::Parse(EntsPath, Defs))
		{
			continue;
		}
		const FString MapName = FPaths::GetBaseFilename(EntsPath);
		for (const FElysiumEntityDef& Def : Defs.Defs)
		{
			if (const FString* Dn = Def.Keys.Find(TEXT("dialogname")))
			{
				if (!Dn->IsEmpty())
				{
					DialogNames.Add(*Dn);
					if (!FirstMapOf.Contains(*Dn)) { FirstMapOf.Add(*Dn, MapName); }
				}
			}
		}
	}

	if (DialogNames.Num() == 0)
	{
		AddInfo(TEXT("skipping: no NPC dialognames in the exported maps"));
		return true;
	}

	int32 Parsed = 0, ParsedWithNpc = 0, TotalRows = 0, TotalChoices = 0, DanglingLinks = 0, Opened = 0;
	FString WorstDangling;

	for (const FString& Dn : DialogNames)
	{
		const FString Path = FElysiumContentPaths::DlgFromDialogname(Dn);
		if (!IFileManager::Get().FileExists(*Path))
		{
			AddError(FString::Printf(TEXT("%s references '%s' but it is not on disk"), *FirstMapOf[Dn], *Dn));
			continue;
		}

		TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
		FString Err;
		if (!TestTrue(FString::Printf(TEXT("%s parses"), *Dn), FElysiumDlgFile::LoadFile(Path, File.Get(), &Err)))
		{
			AddError(Err);
			continue;
		}
		++Parsed;
		TotalRows += File->Lines.Num();
		const bool bHasNpcLine = File->Lines.ContainsByPredicate([](const FElysiumDlgLine& L) { return L.IsNpcLine(); });
		TestTrue(FString::Printf(TEXT("%s has an NPC line"), *Dn), bHasNpcLine);

		// Every PC choice's link must resolve to an existing NPC line, or be 0 (END). A dangling link is
		// a data quirk, not a parser fault, so it is counted + reported rather than failed.
		for (const FElysiumDlgLine& L : File->Lines)
		{
			if (!L.IsPcChoice()) { continue; }
			const int32 Target = L.LinkTarget();
			if (Target == 0) { continue; }
			const FElysiumDlgLine* To = File->FindById(Target);
			if (!To || !To->IsNpcLine())
			{
				++DanglingLinks;
				if (WorstDangling.IsEmpty()) { WorstDangling = FString::Printf(TEXT("%s id %d -> %d"), *Dn, L.Id, Target); }
			}
		}

		// Drive a bounded, deterministic branch walk (pass every gate; prefer an as-yet-unentered NPC
		// line so the walk explores rather than loops; a hard cap backstops a genuine cycle). The point
		// is that the machine never crashes and always returns a valid current line / choice set on real
		// data — not that every dialogue is exhaustively covered.
		auto Cond = [](const FString&) { return true; };
		auto Act = [](const FString&) {};
		FElysiumDlgConversation Conv(File, /*bMale*/ true, /*bMalk*/ false, Cond, Act);
		Conv.Start();
		if (bHasNpcLine)
		{
			TestNotNull(FString::Printf(TEXT("%s opens on an NPC line"), *Dn), Conv.CurrentNpcLine());
			if (Conv.CurrentNpcLine() != nullptr) { ++Opened; }
			++ParsedWithNpc;
		}

		TSet<int32> Visited;
		for (int32 Guard = 0; Guard < 512 && !Conv.IsOver(); ++Guard)
		{
			if (const FElysiumDlgLine* Cur = Conv.CurrentNpcLine())
			{
				Visited.Add(Cur->Id);
			}
			if (Conv.IsTerminalLine())
			{
				Conv.AdvanceTerminal();
				continue;
			}
			// Prefer a choice leading to an unvisited NPC line (or an END), else the first choice.
			int32 Pick = 0;
			for (int32 v = 0; v < Conv.VisibleChoices().Num(); ++v)
			{
				const int32 Target = Conv.VisibleChoice(v)->LinkTarget();
				if (Target == 0 || !Visited.Contains(Target)) { Pick = v; break; }
			}
			TotalChoices += (Conv.VisibleChoices().Num() > 0) ? 1 : 0;
			Conv.Choose(Pick);
		}
	}

	AddInfo(FString::Printf(
		TEXT("dlg corpus: %d referenced, %d parsed, %d rows, %d opened, %d turns walked, %d dangling links%s"),
		DialogNames.Num(), Parsed, TotalRows, Opened, TotalChoices, DanglingLinks,
		WorstDangling.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (e.g. %s)"), *WorstDangling)));

	TestEqual(TEXT("every referenced dialog parsed"), Parsed, DialogNames.Num());
	TestEqual(TEXT("every dialog with an NPC line opened"), Opened, ParsedWithNpc);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
