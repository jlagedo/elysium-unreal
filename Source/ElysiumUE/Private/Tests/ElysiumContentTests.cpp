// P2.8 — the content-gated tier. These read the offline pipeline's real exported intermediates
// from $ELYSIUM_EXPORT_ROOT and assert the parse contract holds against actual game data. They emit a
// structured abstention when a required export is unavailable, so a fresh checkout stays green without
// being reported as covered; a machine that has run the exporter gets real regression coverage.
//
// The app-context mask (not ClientContext alone) so they run in the editor commandlet uv run elysium test
// drives as well as in a game/client session — the `.ents` are read from disk through
// FElysiumContentPaths, which resolves the same in either. ProductFilter keeps them in this
// project's own suite bucket, out of the per-commit smoke set where a missing export would look
// like noise.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumClassRegistry.h"
#include "ElysiumCameraSolve.h"
#include "ElysiumCommands.h"
#include "ElysiumContentPaths.h"
#include "Visual/ElysiumDecals.h"
#include "ElysiumDlg.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumInputAssets.h"
#include "ElysiumKeyValues.h"
#include "ElysiumPlayer.h"
#include "Visual/ElysiumNpcClips.h"
#include "Visual/ElysiumObjModel.h"
#include "ElysiumReflections.h"
#include "ElysiumRng.h"
#include "Scripting/ElysiumScriptNatives.h"
#include "Substrate/ElysiumChargen.h"
#include "Substrate/ElysiumCameraTrack.h"
#include "Substrate/ElysiumDice.h"
#include "Substrate/ElysiumDisposition.h"
#include "Substrate/ElysiumFeed.h"
#include "Substrate/ElysiumInterestingPlaces.h"
#include "Substrate/ElysiumQuestLog.h"
#include "Substrate/ElysiumQuestView.h"

#include "Algo/AnyOf.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumSheetMath.h"
#include "Visual/ElysiumRopes.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumTestServices.h"
#include "Tests/ElysiumNpcTestHooks.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureCube.h"
#include "EnhancedActionKeyMapping.h"
#include "GameFramework/InputSettings.h"
#include "GameInputDeveloperSettings.h"
#include "HAL/FileManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionRayTracingQualitySwitch.h"
#include "Materials/MaterialExpressionTextureSampleParameterCube.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameterCollection.h"
#include "MaterialShared.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "NiagaraSystem.h"
#include "RHIShaderPlatform.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

static constexpr EAutomationTestFlags GElysiumContentTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	// Abstain only while a domain this test actually reads is missing. Naming the domains is what
	// lets `export bundle npc` un-gate the character tests without a whole-corpus export.
	//
	// The structured event keeps abstention separate from execution in the command summary without
	// misusing Unreal's warning channel for an expected missing-corpus state.
	bool SkipIncompleteCorpus(FAutomationTestBase& Test, std::initializer_list<const TCHAR*> Domains)
	{
		TArray<FString> Missing;
		for (const TCHAR* Domain : Domains)
		{
			if (FElysiumContentPaths::IsIncomplete(Domain))
			{
				Missing.Add(Domain);
			}
		}
		if (Missing.IsEmpty())
		{
			return false;
		}
		Test.AddInfo(FString::Printf(
			TEXT("ELYSIUM_TEST_ABSTAIN: the %s export domain(s) are marked incomplete at %s.*"),
			*FString::Join(Missing, TEXT(", ")), *FElysiumContentPaths::IncompleteMarker()));
		return true;
	}

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
		// 8.4 — prop_physics carry the same model_mesh annotation plus a `.phys` collision sidecar;
		// phys_hinge carry the pre-converted hinge_axis. Sample stems confirm on disk.
		int32 PropPhysics = 0;
		int32 PropPhysicsWithMesh = 0;
		FString AnyPhysStem;
		int32 PhysHinge = 0;
		int32 PhysHingeWithAxis = 0;
		int32 BrushEntities = 0;
		int32 BrushMeshes = 0;
		int32 HiddenBrushMeshes = 0;
		int32 TriggerBrushMeshes = 0;
		int32 ElevatorsWithFloors = 0;
		FString AnyBrushStem;

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
			if (Def.IsBrush())
			{
				++Out.BrushEntities;
				if (!Def.BrushMesh.IsEmpty())
				{
					++Out.BrushMeshes;
					Out.AnyBrushStem = Def.BrushMesh;
					Out.HiddenBrushMeshes += Def.bStartHidden ? 1 : 0;
					Out.TriggerBrushMeshes += Def.Classname.StartsWith(
						TEXT("trigger_"), ESearchCase::IgnoreCase) ? 1 : 0;
				}
			}
			if (Def.Classname.Equals(TEXT("func_elevator"), ESearchCase::IgnoreCase)
				&& Def.ElevatorFloors.Num() == 8)
			{
				++Out.ElevatorsWithFloors;
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
	if (SkipIncompleteCorpus(*this, { TEXT("maps") })) return true;
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
	TestTrue(TEXT("slice has environmental-audio trigger brushes"),
		Survey.Classnames.Contains(TEXT("trigger_environmental_audio")));
	const FElysiumClassDesc* EnvironmentalAudio =
		Reg.Find(FName(TEXT("trigger_environmental_audio")));
	if (TestNotNull(TEXT("trigger_environmental_audio is a registered trigger leaf"),
		EnvironmentalAudio))
	{
		TestNotNull(TEXT("environmental audio inherits StartDisabled"),
			Reg.FindField(*EnvironmentalAudio, FName(TEXT("StartDisabled"))));
		TestNotNull(TEXT("environmental audio inherits Enable"),
			Reg.FindInput(*EnvironmentalAudio, FName(TEXT("Enable"))));
	}

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

	// Renderable BSP submodels are no longer welded into the static world. Each annotation
	// resolves to a local-space OBJ; tools-only trigger brushes remain collision-only, while
	// StartHidden brushes retain their reversible visual.
	TestTrue(TEXT("tutorial carries renderable brush-mesh annotations"), Survey.BrushMeshes > 0);
	TestTrue(TEXT("StartHidden renderable brushes retain their meshes"),
		Survey.HiddenBrushMeshes > 0);
	TestEqual(TEXT("trigger-only brushes remain meshless"), Survey.TriggerBrushMeshes, 0);
	TestTrue(TEXT("func_elevator carries its eight pre-converted floors"),
		Survey.ElevatorsWithFloors > 0);
	if (!Survey.AnyBrushStem.IsEmpty())
	{
		const FString Obj = FPaths::GetPath(FElysiumContentPaths::MapEnts(TEXT("sp_tutorial_1")))
			/ TEXT("brushes") / (Survey.AnyBrushStem + TEXT(".obj"));
		TestTrue(TEXT("a brush_mesh annotation resolves to a local OBJ"),
			IFileManager::Get().FileExists(*Obj));
		UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr,
			*FElysiumContentPaths::BakedBrushMesh(TEXT("sp_tutorial_1"), Survey.AnyBrushStem));
		if (Mesh == nullptr)
		{
			AddInfo(FString::Printf(TEXT("skipping baked brush assertions: no mesh for '%s' ")
				TEXT("(run: uv run elysium export map sp_tutorial_1 --force)"), *Survey.AnyBrushStem));
		}
		else
		{
			TestTrue(TEXT("a baked brush mesh has render triangles"), Mesh->GetNumTriangles(0) > 0);
			TestTrue(TEXT("a baked brush mesh has material slots"), Mesh->GetStaticMaterials().Num() > 0);
			if (UBodySetup* Body = Mesh->GetBodySetup())
			{
				TestEqual(TEXT("a baked brush mesh has no simple collision"),
					Body->AggGeom.GetElementCount(), 0);
			}
		}
	}

	// 8.4 — physics props/hinges. prop_physics/phys_hinge register; a prop_physics record carries a
	// decoded model_mesh + a `.phys` collision sidecar (VtMB's own convex hulls, out of the model's
	// `.phy`), and phys_hinge carries the pre-converted hinge_axis. Guarded on presence so the tier
	// holds on any current export.
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
			TestTrue(TEXT("a prop_physics model carries a .phys collision sidecar"),
				IFileManager::Get().FileExists(*(Dir / (Survey.AnyPhysStem + TEXT(".phys")))));

			// The invariant that actually has to hold at run time: a physics prop's *baked* mesh
			// carries simple collision a Chaos body can simulate against, under a trace flag that
			// cooks it alongside the per-poly shape the debug pick traces. Self-skips when the map
			// has not been baked, the same way the tier self-skips an unexported map.
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr,
				*FElysiumContentPaths::BakedPropMesh(TEXT("sp_tutorial_1"), Survey.AnyPhysStem));
			if (Mesh == nullptr)
			{
				AddInfo(FString::Printf(
					TEXT("skipping the baked-collision assertions: no baked mesh for '%s' ")
					TEXT("(run: uv run elysium export map sp_tutorial_1 --force)"), *Survey.AnyPhysStem));
			}
			else if (UBodySetup* Body = Mesh->GetBodySetup())
			{
				TestTrue(TEXT("a baked physics-prop mesh carries simple collision"),
					Body->AggGeom.GetElementCount() > 0);
				TestEqual(TEXT("a baked physics-prop mesh cooks simple AND complex collision"),
					Body->CollisionTraceFlag, CTF_UseSimpleAndComplex);
				TestTrue(TEXT("a baked physics-prop mesh carries the model's authored mass"),
					Body->DefaultInstance.bOverrideMass && Body->DefaultInstance.GetMassOverride() > 0.0f);
			}
			else
			{
				AddError(TEXT("a baked physics-prop mesh has no body setup at all"));
			}
		}
	}
	if (Survey.PhysHinge > 0)
	{
		TestTrue(TEXT("phys_hinge records carry a pre-converted hinge_axis"), Survey.PhysHingeWithAxis > 0);
	}
	AddInfo(FString::Printf(TEXT("sp_tutorial_1 prop_physics: %d (%d with model_mesh); phys_hinge: %d (%d with axis)"),
		Survey.PropPhysics, Survey.PropPhysicsWithMesh, Survey.PhysHinge, Survey.PhysHingeWithAxis));

	// 8.3/8.4 skin families — the `+use` static-mesh family stands a body and takes a skin, the
	// exporter emits a `props/<stem>.skins` sidecar for every model carrying alternate families,
	// and `.props` carries DStaticPropV4.skin as a tenth field.
	for (const TCHAR* Name : { TEXT("prop_button"), TEXT("prop_sign"), TEXT("prop_doorknob"),
							   TEXT("item_container_animated") })
	{
		TestNotNull(*FString::Printf(TEXT("%s registered"), Name), Reg.Find(FName(Name)));
	}

	const FString PropsDir = FElysiumContentPaths::MapPropsDir(TEXT("sp_tutorial_1"));
	TArray<FString> SkinFiles;
	IFileManager::Get().FindFiles(SkinFiles, *(PropsDir / TEXT("*.skins")), true, false);
	AddInfo(FString::Printf(TEXT("sp_tutorial_1 models with alternate skin families: %d"), SkinFiles.Num()));
	if (SkinFiles.Num() > 0)
	{
		// Every line is `<family> <authored>=<family> ...`: a family number > 0 (family 0 is the
		// authored set and is never written) and at least one remap pair.
		TArray<FString> Lines;
		FFileHelper::LoadFileToStringArray(Lines, *(PropsDir / SkinFiles[0]));
		TestTrue(TEXT("a .skins sidecar carries at least one family"), Lines.Num() > 0);
		for (const FString& Line : Lines)
		{
			TArray<FString> Tok;
			Line.ParseIntoArray(Tok, TEXT(" "), true);
			TestTrue(TEXT(".skins line names a non-zero family and at least one remap"),
				Tok.Num() >= 2 && FCString::Atoi(*Tok[0]) > 0 && Tok[1].Contains(TEXT("=")));
		}
	}

	TArray<FString> PropLines;
	if (FFileHelper::LoadFileToStringArray(PropLines, *FElysiumContentPaths::MapProps(TEXT("sp_tutorial_1")))
		&& PropLines.Num() > 0)
	{
		int32 TenField = 0, Skinned = 0;
		for (const FString& Line : PropLines)
		{
			TArray<FString> Tok;
			Line.ParseIntoArray(Tok, TEXT(" "), true);
			if (Tok.Num() >= 10)
			{
				++TenField;
				Skinned += FCString::Atoi(*Tok[9]) != 0 ? 1 : 0;
			}
		}
		TestEqual(TEXT("every .props line carries the skin field"), TenField, PropLines.Num());
		AddInfo(FString::Printf(TEXT("sp_tutorial_1 static props on an alternate skin: %d of %d"),
			Skinned, PropLines.Num()));
	}

	return true;
}

// =====================================================================================
// Genesis's routing premises. These are deliberately content assertions rather than a hand-built
// duplicate: if the exporter drops the trigger, rewrites its wire, or moves the spawn out of it,
// New Game must fail here before the live route silently stops opening the wizard.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGenesisEntsTest,
	"Elysium.Content.GenesisEnts", GElysiumContentTestFlags)
bool FElysiumGenesisEntsTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("maps") })) return true;
	const TCHAR* Map = TEXT("sp_genesisdevice_1");
	const FString EntsPath = FElysiumContentPaths::MapEnts(Map);
	if (!IFileManager::Get().FileExists(*EntsPath))
	{
		AddInfo(FString::Printf(TEXT("ELYSIUM_TEST_ABSTAIN: %s has no exported .ents at %s"), Map, *EntsPath));
		return true;
	}

	FElysiumEntityDefs Defs;
	if (!TestTrue(TEXT("genesis .ents parses"), FElysiumEntityDefs::Parse(EntsPath, Defs)))
	{
		return false;
	}
	TestEqual(TEXT("genesis carries its 14 records"), Defs.Num(), 14);

	auto Find = [&Defs](const TCHAR* Classname, const TCHAR* Targetname) -> const FElysiumEntityDef*
	{
		for (const FElysiumEntityDef& Def : Defs.Defs)
		{
			if (Def.Classname.Equals(Classname, ESearchCase::IgnoreCase)
				&& Def.TargetName.Equals(Targetname, ESearchCase::IgnoreCase))
			{
				return &Def;
			}
		}
		return nullptr;
	};

	const FElysiumEntityDef* NewPlayer = Find(TEXT("trigger_once"), TEXT("newplayer"));
	const FElysiumEntityDef* Firetrans = Find(TEXT("trigger_multiple"), TEXT("firetrans"));
	const FElysiumEntityDef* Boogieout = Find(TEXT("trigger_changelevel"), TEXT("boogieout"));
	if (!TestNotNull(TEXT("newplayer trigger_once exists"), NewPlayer)
		|| !TestNotNull(TEXT("firetrans trigger_multiple exists"), Firetrans)
		|| !TestNotNull(TEXT("boogieout trigger_changelevel exists"), Boogieout))
	{
		return false;
	}

	const bool bCreatesPlayer = NewPlayer->Outputs.ContainsByPredicate([](const FElysiumOutputDef& W)
	{
		return W.Name.Equals(TEXT("OnTrigger"), ESearchCase::IgnoreCase)
			&& W.Target.Equals(TEXT("newplayer"), ESearchCase::IgnoreCase)
			&& W.Input.Equals(TEXT("Toggle"), ESearchCase::IgnoreCase)
			&& W.Python.Contains(TEXT("ccmd.createplayer"));
	});
	TestTrue(TEXT("newplayer fires ccmd.createplayer and disables itself"), bCreatesPlayer);

	// Read the same two-line sidecar contract as AElysiumMapActor::ReadSpawn. The sidecar value is
	// Source feet origin; capsule-centre compensation belongs to the pawn once its real height exists.
	TArray<FString> SpawnLines;
	FVector Spawn = FVector::ZeroVector;
	bool bFoundOrigin = false;
	if (TestTrue(TEXT("genesis .spawn loads"), FFileHelper::LoadFileToStringArray(
		SpawnLines, *FElysiumContentPaths::MapSpawn(Map))))
	{
		for (const FString& Line : SpawnLines)
		{
			TArray<FString> Tokens;
			Line.ParseIntoArrayWS(Tokens);
			if (Tokens.Num() == 4 && Tokens[0].Equals(TEXT("origin"), ESearchCase::IgnoreCase))
			{
				Spawn = FVector(FCString::Atod(*Tokens[1]), FCString::Atod(*Tokens[2]),
					FCString::Atod(*Tokens[3]));
				bFoundOrigin = true;
				break;
			}
		}
	}
	TestTrue(TEXT("genesis .spawn names an origin"), bFoundOrigin);
	FBox NewPlayerBounds(ForceInit);
	for (const FElysiumConvexHull& Hull : NewPlayer->Hulls)
	{
		for (const FVector& Vertex : Hull.Vertices)
		{
			NewPlayerBounds += NewPlayer->Origin + Vertex;
		}
	}
	TestTrue(TEXT("the authored feet spawn is inside newplayer's volume bounds"),
		bFoundOrigin && NewPlayerBounds.IsValid && NewPlayerBounds.IsInsideOrOn(Spawn));

	const bool bFiresExit = Firetrans->Outputs.ContainsByPredicate([](const FElysiumOutputDef& W)
	{
		return W.Name.Equals(TEXT("OnStartTouch"), ESearchCase::IgnoreCase)
			&& W.Target.Equals(TEXT("boogieout"), ESearchCase::IgnoreCase)
			&& W.Input.Equals(TEXT("ChangeNow"), ESearchCase::IgnoreCase);
	});
	TestTrue(TEXT("firetrans forces boogieout.ChangeNow on touch"), bFiresExit);
	TestEqual(TEXT("boogieout names the authored theatre map"),
		Boogieout->Keys.FindRef(TEXT("map")), FString(TEXT("sp_theatre")));
	TestEqual(TEXT("boogieout names the newgame landmark"),
		Boogieout->Keys.FindRef(TEXT("landmark")), FString(TEXT("newgame")));

	return true;
}

// =====================================================================================
// Every exported ChangeNow output must resolve through the real trigger_changelevel registry.
// The 23-map test bench additionally guards all 88 shipped wires, including the five authored
// wires whose target names are not present in their own map and cannot be classified by target.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumChangeLevelInputCoverageTest,
	"Elysium.Content.ChangeLevelInputs", GElysiumContentTestFlags)
bool FElysiumChangeLevelInputCoverageTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("maps") })) return true;
	TArray<FString> EntsFiles;
	IFileManager::Get().FindFilesRecursive(EntsFiles, *FElysiumContentPaths::Root(), TEXT("*.ents"),
		/*Files*/ true, /*Dirs*/ false);
	if (EntsFiles.Num() == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported maps under $ELYSIUM_EXPORT_ROOT"));
		return true;
	}

	const FElysiumClassDesc* ChangeDesc =
		FElysiumClassRegistry::Get().Find(FName(TEXT("trigger_changelevel")));
	if (!TestNotNull(TEXT("trigger_changelevel is registered"), ChangeDesc))
	{
		return false;
	}

	int32 ChangeNowWires = 0;
	int32 UnknownInputs = 0;
	for (const FString& EntsPath : EntsFiles)
	{
		FElysiumEntityDefs Defs;
		if (!FElysiumEntityDefs::Parse(EntsPath, Defs))
		{
			AddError(FString::Printf(TEXT("could not parse %s"), *EntsPath));
			continue;
		}

		for (const FElysiumEntityDef& Source : Defs.Defs)
		{
			for (const FElysiumOutputDef& Wire : Source.Outputs)
			{
				const bool bIsChangeNow =
					Wire.Input.Equals(TEXT("ChangeNow"), ESearchCase::IgnoreCase);
				ChangeNowWires += bIsChangeNow ? 1 : 0;

				bool bTargetsChangeLevel = false;
				for (const FElysiumEntityDef& Target : Defs.Defs)
				{
					if (Target.Classname.Equals(TEXT("trigger_changelevel"), ESearchCase::IgnoreCase)
						&& FElysiumEntityWorld::NameMatches(Target.TargetName, Wire.Target))
					{
						bTargetsChangeLevel = true;
						break;
					}
				}
				if (!bTargetsChangeLevel && !bIsChangeNow)
				{
					continue;
				}

				if (FElysiumClassRegistry::Get().FindInput(*ChangeDesc, FName(*Wire.Input)) == nullptr)
				{
					++UnknownInputs;
					AddError(FString::Printf(TEXT("%s: %s.%s -> %s.%s is unimplemented"),
						*Defs.MapName, *Source.Classname, *Wire.Name, *Wire.Target, *Wire.Input));
				}
			}
		}
	}

	AddInfo(FString::Printf(TEXT("trigger_changelevel corpus: %d ChangeNow wires, %d unknown inputs"),
		ChangeNowWires, UnknownInputs));
	if (EntsFiles.Num() >= 23)
	{
		TestEqual(TEXT("the complete test bench carries all 88 ChangeNow wires"),
			ChangeNowWires, 88);
	}
	else
	{
		AddInfo(FString::Printf(TEXT("partial corpus: validated %d exported map(s)"),
			EntsFiles.Num()));
	}
	TestEqual(TEXT("every trigger_changelevel input resolves"), UnknownInputs, 0);
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
	if (SkipIncompleteCorpus(*this, { TEXT("maps") })) return true;
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
// the shared `<map>.mtl`, so the bake always finds a texture for each ADecalActor it places.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTutorialDecalsTest,
	"Elysium.Content.TutorialDecals", GElysiumContentTestFlags)
bool FElysiumTutorialDecalsTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("maps") })) return true;
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

	// Materials ride the shared world MTL — the same file the bake's material stage reads.
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
// cable (distinct endpoints, positive width, non-negative slack, a node count inside VtMB's
// [2, 10] ROPE_MAX_SEGMENTS bound) and
// that its decoded RopeMaterial texture exists on disk, so the runtime's BuildRopes always finds
// an albedo for each UCableComponent. The tutorial strings its telephone lines this way.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTutorialRopesTest,
	"Elysium.Content.TutorialRopes", GElysiumContentTestFlags)
bool FElysiumTutorialRopesTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("maps") })) return true;
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
	int32 Degenerate = 0, BadWidth = 0, BadRest = 0, BadNodes = 0, MissingTex = 0, Masked = 0;
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
		// Rest length may legitimately fall below the span (that is a taut cable), but never
		// below zero — `ResetSpringLength` floors the per-segment spring at 0.
		if (D.RestCm < 0.f)
		{
			++BadRest;
		}
		if (D.Nodes < 2 || D.Nodes > 10)
		{
			++BadNodes;
		}
		// A "-" tex is a legitimate decode miss (runtime uses a plain MID); a named tex must exist.
		if (D.Tex != TEXT("-") && !IFileManager::Get().FileExists(*(Dir / D.Tex)))
		{
			++MissingTex;
		}
		if (D.Bump != TEXT("-") && !IFileManager::Get().FileExists(*(Dir / D.Bump)))
		{
			++MissingTex;
		}
		// The chains are the reason matflags exists — assert the tutorial still carries a masked
		// rope, so a regression that flattens every material back to opaque fails here.
		if ((D.MatFlags & FElysiumRopeDef::Masked) != 0)
		{
			++Masked;
		}
	}
	TestTrue(TEXT("tutorial carries at least one $alphatest (chain) rope"), Masked > 0);
	TestEqual(TEXT("every cable has distinct endpoints"), Degenerate, 0);
	TestEqual(TEXT("every cable has positive width"), BadWidth, 0);
	TestEqual(TEXT("every cable has non-negative rest length"), BadRest, 0);
	TestEqual(TEXT("every cable's node count is inside VtMB's [2, 10] bound"), BadNodes, 0);
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
	if (SkipIncompleteCorpus(*this, { TEXT("maps") })) return true;
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
	TestTrue(TEXT("the map has reflective surfaces"), Reflective > 0);
	AddInfo(FString::Printf(
		TEXT("%s materials: %d total, %d albedo | masked %d, translucent %d, additive %d, reflective %d, bump %d, wvt %d"),
		Map, Materials.Num(), WithAlbedo, Masked, Translucent, Additive, Reflective, Bumped, Wvt));

	return true;
}

// 7.5 — the reflection channel is bound BY NAME from three places (pipeline/unreal/make_world_materials.py
// authors it, pipeline/unreal/bake_map.py binds it onto each baked instance, FElysiumMaterialFactory and
// AElysiumMapActor::ApplyMaterialOverrides bind it at runtime). A rename that misses one of them
// binds nothing and fails silently in the frame, so the contract is asserted here instead: every
// lit master must carry every parameter ElysiumReflections names.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumReflectionParamsTest,
	"Elysium.Content.ReflectionParams", GElysiumContentTestFlags)
bool FElysiumReflectionParamsTest::RunTest(const FString&)
{
	// This validates generated Unreal packages only. A corpus-wide export marker must not hide
	// missing parameters or a broken master graph after a focused map/policy bake.
	static const TCHAR* LitMasters[] = {
		TEXT("/Game/VtMB/Materials/M_World_Opaque.M_World_Opaque"),
		TEXT("/Game/VtMB/Materials/M_World_Masked.M_World_Masked"),
		TEXT("/Game/VtMB/Materials/M_World_Translucent.M_World_Translucent"),
	};
	static const FName ScalarParams[] = {
		ElysiumReflections::Params::EnvStrength,
		ElysiumReflections::Params::MetalMask,
		ElysiumReflections::Params::RoughBase,
		ElysiumReflections::Params::RoughReflect,
		ElysiumReflections::Params::SpecBase,
		ElysiumReflections::Params::SpecReflect,
	};

	for (const TCHAR* Path : LitMasters)
	{
		UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr, Path);
		if (!TestNotNull(*FString::Printf(TEXT("master loads: %s"), Path), Master))
		{
			continue;
		}
		for (const FName& Param : ScalarParams)
		{
			float Value = 0.f;
			TestTrue(*FString::Printf(TEXT("%s carries scalar %s"), Path, *Param.ToString()),
				Master->GetScalarParameterValue(Param, Value));
		}
		FLinearColor Tint = FLinearColor::Black;
		TestTrue(*FString::Printf(TEXT("%s carries vector EnvTint"), Path),
			Master->GetVectorParameterValue(ElysiumReflections::Params::EnvTint, Tint));
		// EnvMask must fall back to WHITE, not to the engine placeholder. 228 of the game's
		// reflective materials carry $envmap with no $envmapmask and reflect uniformly, and
		// nothing overwrites the sampler for them -- so this default IS their mask.
		// /Engine/EngineResources/DefaultTexture is 128x128 greenish-grey noise, which would
		// both dim and mottle exactly those surfaces.
		UTexture* Mask = nullptr;
		if (TestTrue(*FString::Printf(TEXT("%s carries texture EnvMask"), Path),
			Master->GetTextureParameterValue(ElysiumReflections::Params::EnvMask, Mask))
			&& TestNotNull(TEXT("EnvMask has a fallback texture"), Mask))
		{
			TestEqual(*FString::Printf(TEXT("%s EnvMask falls back to linear white"), Path),
				Mask->GetPathName(),
				FString(TEXT("/Game/VtMB/Materials/T_LinearWhiteMask.T_LinearWhiteMask")));
			if (const UTexture2D* Mask2D = Cast<UTexture2D>(Mask))
			{
				TestFalse(TEXT("white mask is linear"), Mask2D->SRGB);
				TestEqual(TEXT("white mask uses mask compression"),
					Mask2D->CompressionSettings, TC_Masks);
			}
		}
		UTexture* SourceCube = nullptr;
		TestTrue(*FString::Printf(TEXT("%s carries SourceCube"), Path),
			Master->GetTextureParameterValue(ElysiumReflections::Params::SourceCube, SourceCube));
		TArray<FMaterialParameterInfo> Switches;
		TArray<FGuid> SwitchIds;
		Master->GetAllStaticSwitchParameterInfo(Switches, SwitchIds);
		TestTrue(*FString::Printf(TEXT("%s carries WetnessUsesSourceCube"), Path),
			Switches.ContainsByPredicate([](const FMaterialParameterInfo& Info)
			{
				return Info.Name == ElysiumReflections::Params::WetnessUsesSourceCube;
			}));
		if (UMaterial* Material = Master->GetMaterial())
		{
			TestTrue(TEXT("master contains source cube sample"),
				Algo::AnyOf(Material->GetExpressions(), [](const UMaterialExpression* Expression)
				{
					return Expression && Expression->IsA<UMaterialExpressionTextureSampleParameterCube>();
				}));
			TestTrue(TEXT("master separates primary source contribution from ray and card capture"),
				Algo::AnyOf(Material->GetExpressions(), [](const UMaterialExpression* Expression)
				{
					return Expression && Expression->IsA<UMaterialExpressionRayTracingQualitySwitch>();
				}));
			TArray<FMaterialResource*> Sm6Resources;
			FMaterialResource* Sm6 = FindOrCreateMaterialResource(
				Sm6Resources, Material, nullptr, SP_PCD3D_SM6, EMaterialQualityLevel::High);
			if (TestNotNull(*FString::Printf(TEXT("%s creates a PCD3D_SM6 resource"), Path), Sm6))
			{
				TestTrue(*FString::Printf(TEXT("%s compiles for PCD3D_SM6"), Path),
					Sm6->CacheShaders(EMaterialShaderPrecompileMode::None));
				for (const FString& Error : Sm6->GetCompileErrors())
				{
					AddError(FString::Printf(TEXT("%s PCD3D_SM6: %s"), Path, *Error));
				}
				TestNotNull(*FString::Printf(TEXT("%s has an SM6 shader map"), Path),
					Sm6->GetGameThreadShaderMap());
			}
			FMaterial::DeferredDeleteArray(Sm6Resources);
		}

		// The Lambert base is the shipped default, and it is what makes a non-$envmap surface
		// take no Lumen specular. A master that drifts off it silently re-glosses the world.
		float RoughBase = -1.f, SpecBase = -1.f;
		Master->GetScalarParameterValue(ElysiumReflections::Params::RoughBase, RoughBase);
		Master->GetScalarParameterValue(ElysiumReflections::Params::SpecBase, SpecBase);
		TestEqual(TEXT("master defaults to the Lambert roughness"),
			RoughBase, ElysiumReflections::RoughBase, 1e-4f);
		TestEqual(TEXT("master defaults to the Lambert specular"),
			SpecBase, ElysiumReflections::SpecBase, 1e-4f);
		// EnvStrength must default OFF, or every surface instanced off this master reflects.
		float EnvStrength = -1.f;
		Master->GetScalarParameterValue(ElysiumReflections::Params::EnvStrength, EnvStrength);
		TestEqual(TEXT("reflection is off by default"), EnvStrength, 0.f, 1e-4f);
	}
	return true;
}

// Semantic glass is a separate stable UE5 path: generic $translucent materials must not inherit
// refraction, while real lit/reflective glass compiles as Thin Translucent with a tangent-normal
// Pixel Normal Offset. These properties and parameter names are the bake/runtime contract.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGlassMasterTest,
	"Elysium.Content.GlassMaster", GElysiumContentTestFlags)
bool FElysiumGlassMasterTest::RunTest(const FString&)
{
	UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/VtMB/Materials/M_World_Glass.M_World_Glass"));
	if (!TestNotNull(TEXT("glass master loads"), Master))
	{
		return true;
	}
	UMaterial* Material = Master->GetMaterial();
	if (!TestNotNull(TEXT("glass master resolves its UMaterial"), Material))
	{
		return true;
	}

	TestEqual(TEXT("glass uses translucent blend"), Material->GetBlendMode(), BLEND_Translucent);
	TestTrue(TEXT("glass uses Thin Translucent shading"),
		Material->GetShadingModels().HasShadingModel(MSM_ThinTranslucent));
	TestEqual(TEXT("glass uses Surface ForwardShading"), Material->TranslucencyLightingMode,
		TLM_SurfacePerPixelLighting);
	TestEqual(TEXT("glass uses Pixel Normal Offset"), Material->RefractionMethod,
		RM_PixelNormalOffset);
	TestTrue(TEXT("glass master carries the ISM permutation"),
		Master->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));
	TestTrue(TEXT("glass master carries the Nanite usage contract"),
		Master->GetUsageByFlag(MATUSAGE_Nanite));

	struct FExpectedScalar
	{
		const TCHAR* Name;
		float Value;
	};
	static const FExpectedScalar Scalars[] = {
		{ TEXT("GlassRefraction"), 1.08f },
		{ TEXT("GlassTintStrength"), 0.25f },
		{ TEXT("GlassFrameExponent"), 8.0f },
		{ TEXT("BumpAmount"), 0.0f },
		{ TEXT("EnvStrength"), 0.0f },
	};
	for (const FExpectedScalar& Expected : Scalars)
	{
		float Value = -1.f;
		TestTrue(*FString::Printf(TEXT("glass carries scalar %s"), Expected.Name),
			Master->GetScalarParameterValue(FName(Expected.Name), Value));
		TestEqual(*FString::Printf(TEXT("glass scalar %s default"), Expected.Name),
			Value, Expected.Value, 1e-4f);
	}

	for (const TCHAR* Name : { TEXT("Albedo"), TEXT("BumpMap"), TEXT("EnvMask") })
	{
		UTexture* Texture = nullptr;
		TestTrue(*FString::Printf(TEXT("glass carries texture %s"), Name),
			Master->GetTextureParameterValue(FName(Name), Texture));
		TestNotNull(*FString::Printf(TEXT("glass texture %s has a fallback"), Name), Texture);
	}
	return true;
}

// Source Refract is its own clear distortion overlay. The DUDV texture must never become
// albedo; the dedicated master consumes a linear tangent normal and the original amount through
// Pixel Normal Offset while white Thin Translucent transmission leaves the pane behind visible.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRefractMasterTest,
	"Elysium.Content.RefractMaster", GElysiumContentTestFlags)
bool FElysiumRefractMasterTest::RunTest(const FString&)
{
	UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/VtMB/Materials/M_Refract.M_Refract"));
	if (!TestNotNull(TEXT("refract master loads"), Master))
	{
		return true;
	}
	UMaterial* Material = Master->GetMaterial();
	if (!TestNotNull(TEXT("refract master resolves its UMaterial"), Material))
	{
		return true;
	}

	TestEqual(TEXT("refract uses translucent blend"), Material->GetBlendMode(), BLEND_Translucent);
	TestTrue(TEXT("refract uses Thin Translucent shading"),
		Material->GetShadingModels().HasShadingModel(MSM_ThinTranslucent));
	TestEqual(TEXT("refract uses Surface ForwardShading"), Material->TranslucencyLightingMode,
		TLM_SurfacePerPixelLighting);
	TestEqual(TEXT("refract uses Pixel Normal Offset"), Material->RefractionMethod,
		RM_PixelNormalOffset);
	TestTrue(TEXT("refract master carries the ISM permutation"),
		Master->GetUsageByFlag(MATUSAGE_InstancedStaticMeshes));

	float Amount = -1.f;
	TestTrue(TEXT("refract carries SourceRefractAmount"),
		Master->GetScalarParameterValue(FName(TEXT("SourceRefractAmount")), Amount));
	TestEqual(TEXT("refract amount defaults neutral"), Amount, 0.f, 1e-6f);
	UTexture* Texture = nullptr;
	TestTrue(TEXT("refract carries RefractMap"),
		Master->GetTextureParameterValue(FName(TEXT("RefractMap")), Texture));
	TestNotNull(TEXT("refract map has a flat-normal fallback"), Texture);
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
	if (SkipIncompleteCorpus(*this, { TEXT("scripts") })) return true;
	const FString Path = FElysiumContentPaths::DlgFromDialogname(TEXT("dlg/Main Characters/jack_tutorial.dlg"));
	if (!IFileManager::Get().FileExists(*Path))
	{
		AddInfo(FString::Printf(TEXT("ELYSIUM_TEST_ABSTAIN: no exported dialogue at %s (run the pipeline to enable)"), *Path));
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

	// The retail opener is first passing valid link in physical file order. Pin Jack's three collisions
	// against the real exported file rather than copying the row order into a synthetic fixture.
	auto NoAct = [](const FString&) {};
	{
		auto Cond = [](const FString& C)
		{
			return C == TEXT("G.Tut_Jack == 1 and G.Tut_Patch == 1")
				|| C == TEXT("G.Tut_Jack == 1 and G.Tutorial_Blueblood == 0");
		};
		FElysiumDlgConversation PatchFirst(File, true, false, Cond, NoAct);
		PatchFirst.Start();
		if (TestNotNull(TEXT("patch-first overlap opens"), PatchFirst.CurrentNpcLine()))
		{
			TestEqual(TEXT("patch condition precedes blueblood condition"), PatchFirst.CurrentNpcLine()->Id, 85);
		}
	}
	{
		auto Cond = [](const FString& C)
		{
			return C == TEXT("G.Tut_Jack == 7 and IsClan(pc,\"Nosferatu\") and G.Tut_Ratfeed == 1")
				|| C == TEXT("G.Tut_Jack == 7 and G.Tut_Ratfeed == 1");
		};
		FElysiumDlgConversation NosferatuFirst(File, true, false, Cond, NoAct);
		NosferatuFirst.Start();
		if (TestNotNull(TEXT("Nosferatu overlap opens"), NosferatuFirst.CurrentNpcLine()))
		{
			TestEqual(TEXT("Nosferatu-specific condition precedes generic condition"),
				NosferatuFirst.CurrentNpcLine()->Id, 561);
		}
	}
	{
		auto Cond = [](const FString& C)
		{
			return C == TEXT("G.Tut_Jack == 18 and G.Tut_Ashot == 0");
		};
		FElysiumDlgConversation DuplicateFirst(File, true, false, Cond, NoAct);
		DuplicateFirst.Start();
		if (TestNotNull(TEXT("duplicate-condition overlap opens"), DuplicateFirst.CurrentNpcLine()))
		{
			TestEqual(TEXT("first duplicate shadows the later link"), DuplicateFirst.CurrentNpcLine()->Id, 425);
		}
	}

	// Drive the branch machine to the acceptance action. The condition callback selects the plain
	// Tut_Jack==0 starting sentinel at line 1082 and passes every unrelated gate; at each turn prefer
	// a choice that advances Tut_Jack, else the first, so the walk is deterministic and cannot loop.
	TArray<FString> Ran;
	auto Cond = [](const FString& C)
	{
		return !C.Contains(TEXT("G.Tut_Jack")) || C == TEXT("G.Tut_Jack == 0");
	};
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
//
// The corpus is discovered, not listed — every `.ents` under $ELYSIUM_EXPORT_ROOT — so it grows with the
// export. A name the install does not ship is counted and warned rather than failed: that is a
// property of Troika's shipped map data, which no re-export can change.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDlgCorpusTest, "Elysium.Content.DlgCorpus", GElysiumContentTestFlags)
bool FElysiumDlgCorpusTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("scripts"), TEXT("maps") })) return true;
	// Discover every exported map's `.ents` and collect the distinct `dialogname` values NPCs reference.
	TArray<FString> EntsFiles;
	IFileManager::Get().FindFilesRecursive(EntsFiles, *FElysiumContentPaths::Root(), TEXT("*.ents"),
		/*Files*/ true, /*Dirs*/ false);
	if (EntsFiles.Num() == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported maps under $ELYSIUM_EXPORT_ROOT (run the pipeline to enable)"));
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
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no NPC dialognames in the exported maps"));
		return true;
	}

	int32 Parsed = 0, ParsedWithNpc = 0, TotalRows = 0, TotalChoices = 0, DanglingLinks = 0, Opened = 0;
	int32 Unshipped = 0;
	FString WorstDangling;

	for (const FString& Dn : DialogNames)
	{
		const FString Path = FElysiumContentPaths::DlgFromDialogname(Dn);
		if (!IFileManager::Get().FileExists(*Path))
		{
			// Map data outliving its assets, not a parse fault — the same finding PL9 records for
			// eight unshipped `SceneFile` values. `sm_junkyard_1`'s `Night Watchman` is the one case
			// here: the entity is in retail's own `.bsp` as well as the patch's, but its `.dlg`, its
			// `doppleganger.mdl` and its `NightwatchmenDlg()` script function were all cut before
			// ship, so nothing in the merged install can answer it. Warned and excluded.
			++Unshipped;
			AddWarning(FString::Printf(TEXT("%s references '%s', which the install does not ship"),
				*FirstMapOf[Dn], *Dn));
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
		TEXT("dlg corpus: %d referenced, %d unshipped, %d parsed, %d rows, %d opened, %d turns walked, %d dangling links%s"),
		DialogNames.Num(), Unshipped, Parsed, TotalRows, Opened, TotalChoices, DanglingLinks,
		WorstDangling.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (e.g. %s)"), *WorstDangling)));

	// Every dialogue the install DOES ship parses. A wiped or half-exported `out/dlg` would make
	// that vacuously true, so the corpus size is asserted too rather than left implicit.
	TestEqual(TEXT("every shipped referenced dialog parsed"), Parsed, DialogNames.Num() - Unshipped);
	TestTrue(TEXT("the corpus is not empty"), Parsed > 0);
	TestEqual(TEXT("every dialog with an NPC line opened"), Opened, ParsedWithNpc);

	return true;
}

// =====================================================================================
// scripted_sequence × the NPC clip manifest (8.5) — every animation a cutscene beat names must
// resolve in the vocabulary the offline export gives that NPC. This is the seam that breaks
// silently: a clip lives in a shared animation bank pulled through the studiohdr include DAG, so
// an exporter change that drops a bank turns a beat into a no-op with only a runtime warning.
//
// Measured over the 10 exported maps: 94 animation references across 108 sequences, 90 resolving.
// The 4 that do not all name `!playercontroller`, which has no NPC body and is excluded here.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScriptedSequenceClipsTest,
	"Elysium.Content.ScriptedSequenceClips", GElysiumContentTestFlags)
bool FElysiumScriptedSequenceClipsTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("maps"), TEXT("npc") })) return true;
	if (!IFileManager::Get().FileExists(*FElysiumContentPaths::NpcIndex()))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported npc/npc_index.json (run: uv run elysium export bundle npc)"));
		return true;
	}

	static const TCHAR* const AnimKeys[] = {
		TEXT("m_iszPlay"), TEXT("m_iszIdle"), TEXT("m_iszPreIdle"), TEXT("m_iszPostIdle"), TEXT("m_iszCustomMove") };

	TMap<FString, TSharedPtr<FElysiumNpcClipSet>> ClipCache;   // stem -> vocabulary (null = no such export)
	int32 Sequences = 0, Refs = 0, Resolved = 0, NoBody = 0, Unspawned = 0;
	TArray<FString> Misses;

	for (const TCHAR* Map : { TEXT("sp_tutorial_1"), TEXT("sm_hub_1"), TEXT("sp_giovanni_1"), TEXT("hw_609_1") })
	{
		const FString Path = FElysiumContentPaths::MapEnts(Map);
		if (!IFileManager::Get().FileExists(*Path))
		{
			continue;   // not exported — the other maps still cover the contract
		}
		FElysiumEntityDefs Defs;
		if (!TestTrue(FString::Printf(TEXT("%s parses"), Map), FElysiumEntityDefs::Parse(Path, Defs)))
		{
			continue;
		}

		// An NPC is reachable either by its own targetname or as the child an npc_maker will spawn
		// under `NPCTargetname` — 11 of the exported sequences drive one of those.
		TMap<FString, const FElysiumEntityDef*> ByName;
		for (const FElysiumEntityDef& Def : Defs.Defs)
		{
			if (!Def.TargetName.IsEmpty())
			{
				ByName.FindOrAdd(Def.TargetName.ToLower(), &Def);
			}
			if (const FString* Child = Def.Keys.Find(TEXT("NPCTargetname")))
			{
				if (!Child->IsEmpty())
				{
					ByName.FindOrAdd(Child->ToLower(), &Def);
				}
			}
		}

		for (const FElysiumEntityDef& Def : Defs.Defs)
		{
			if (!Def.Classname.Equals(TEXT("scripted_sequence"), ESearchCase::IgnoreCase) &&
				!Def.Classname.Equals(TEXT("aiscripted_sequence"), ESearchCase::IgnoreCase))
			{
				continue;
			}
			++Sequences;

			TArray<FString> Wanted;
			for (const TCHAR* Key : AnimKeys)
			{
				if (const FString* V = Def.Keys.Find(Key))
				{
					if (!V->IsEmpty())
					{
						Wanted.Add(*V);
					}
				}
			}
			if (Wanted.IsEmpty())
			{
				continue;   // a movement-only beat names no animation
			}

			const FString Target = Def.Keys.FindRef(TEXT("m_iszEntity"));
			if (Target.IsEmpty() || Target.StartsWith(TEXT("!")))
			{
				NoBody += Wanted.Num();   // `!playercontroller` — no NPC skeleton to resolve against
				continue;
			}
			const FElysiumEntityDef* const* Npc = ByName.Find(Target.ToLower());
			const FString Model = Npc ? (*Npc)->Keys.FindRef(TEXT("model")) : FString();
			if (Model.IsEmpty())
			{
				Unspawned += Wanted.Num();
				continue;
			}

			const FString Stem = FPaths::GetBaseFilename(Model).ToLower();
			if (!ClipCache.Contains(Stem))
			{
				TSharedPtr<FElysiumNpcClipSet> Set = MakeShared<FElysiumNpcClipSet>();
				FString Error;
				ClipCache.Add(Stem, Set->Load(Stem, Error) ? Set : nullptr);
			}
			const TSharedPtr<FElysiumNpcClipSet>& Set = ClipCache[Stem];
			for (const FString& Clip : Wanted)
			{
				++Refs;
				if (Set.IsValid() && Set->Find(Clip) != nullptr)
				{
					++Resolved;
				}
				else
				{
					Misses.Add(FString::Printf(TEXT("%s: %s.%s -> '%s' (stem %s)"),
						Map, *Def.TargetName, *Target, *Clip, *Stem));
				}
			}
		}
	}

	if (Sequences == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported map carries a scripted_sequence"));
		return true;
	}

	AddInfo(FString::Printf(
		TEXT("scripted_sequence clips: %d sequences, %d animation refs, %d resolved, %d on the player, %d unspawned"),
		Sequences, Refs, Resolved, NoBody, Unspawned));
	for (const FString& Miss : Misses)
	{
		AddWarning(FString::Printf(TEXT("unresolved sequence clip — %s"), *Miss));
	}

	// Every animation an NPC-targeted beat names resolves. A single miss means the bank that owns it
	// stopped being exported, or its label changed — both silent at runtime.
	TestEqual(TEXT("every NPC-targeted sequence animation resolves in the manifest"), Resolved, Refs);

	return true;
}

// =====================================================================================
// The player bodies × the character export (PL13) — every `.mdl` the clan table names as a PC
// body has a glb on disk. No entity on any map references a player model, so the export seeds
// this half of the set from `vdata/system/clandoc000.txt` itself; this asserts the two have not
// drifted, which is the whole reason the seed is the rulebook and not a hand-written list.
//
// Measured over the merged install: 7 playable clans × 2 sexes × 6 armour slots = 84 slots,
// resolving to 56 distinct models (each clan's top two slots repeat its tier-3 suit).
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerBodiesTest,
	"Elysium.Content.PlayerBodies", GElysiumContentTestFlags)
bool FElysiumPlayerBodiesTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("npc"), TEXT("vdata") })) return true;
	const FString ClanDoc = FElysiumContentPaths::VdataFile(TEXT("system/clandoc000.txt"));
	if (!IFileManager::Get().FileExists(*ClanDoc) ||
		!IFileManager::Get().FileExists(*FElysiumContentPaths::NpcIndex()))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported vdata + npc data (run: uv run elysium export grid)"));
		return true;
	}

	FString Text;
	if (!TestTrue(TEXT("clandoc000.txt loads"), FFileHelper::LoadFileToString(Text, *ClanDoc)))
	{
		return true;
	}
	const TSharedPtr<ElysiumKeyValues::FKvNode> Root = ElysiumKeyValues::ParseText(Text);
	const ElysiumKeyValues::FKvNode* Tables = Root.IsValid() ? Root->Child(TEXT("ClanDataTables")) : nullptr;
	if (!TestNotNull(TEXT("clandoc000.txt has a ClanDataTables block"), Tables))
	{
		return true;
	}

	// Only the *indexed* body keys are the player's. The un-indexed `M_Body`/`F_Body` of the human
	// and Society-of-Leopold templates name NPC models, which the map-driven seed already covers.
	// `Models` spans every template because the export's seed does; `PlayableSlots` counts only the
	// seven `Player_*` ones, since the multiplayer and `unused*` templates repeat their paths.
	int32 PlayableTemplates = 0, PlayableSlots = 0;
	TSet<FString> Models;
	for (const TPair<FString, TSharedPtr<ElysiumKeyValues::FKvNode>>& Clan : Tables->Kids)
	{
		if (Clan.Key != TEXT("clandata") || !Clan.Value.IsValid())
		{
			continue;
		}
		const ElysiumKeyValues::FKvNode* General = Clan.Value->Child(TEXT("General"));
		if (!General)
		{
			continue;
		}
		const ElysiumKeyValues::FKvNode* Names = Clan.Value->Child(TEXT("Text"));
		const bool bPlayable = Names && Names->Str(TEXT("TemplateName"), FString()).StartsWith(TEXT("Player_"));
		PlayableTemplates += bPlayable ? 1 : 0;
		for (const TPair<FString, FString>& Kv : General->Values)
		{
			const bool bBodySlot = (Kv.Key.StartsWith(TEXT("m_body")) || Kv.Key.StartsWith(TEXT("f_body"))) &&
				Kv.Key.Len() > 6 && FChar::IsDigit(Kv.Key[6]);
			if (bBodySlot && Kv.Value.EndsWith(TEXT(".mdl")))
			{
				PlayableSlots += bPlayable ? 1 : 0;
				Models.Add(Kv.Value.ToLower().Replace(TEXT("\\"), TEXT("/")));
			}
		}
	}

	FElysiumNpcIndex Index;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("npc_index.json loads (%s)"), *Error), Index.Load(Error)))
	{
		return true;
	}

	// The index is keyed by stem; the clan table names a path, so match on the entry's own `Model`.
	TMap<FString, FString> StemByModel;
	for (const TPair<FString, FElysiumNpcIndexEntry>& Npc : Index.Npcs)
	{
		StemByModel.Add(Npc.Value.Model.ToLower().Replace(TEXT("\\"), TEXT("/")), Npc.Key);
	}

	int32 Exported = 0;
	for (const FString& Model : Models)
	{
		const FString* Stem = StemByModel.Find(Model);
		if (!Stem)
		{
			AddError(FString::Printf(TEXT("clan table names %s, which the character export did not "
				"produce (pipeline/src/elysium_pipeline/exporters/npc_export.py)"), *Model));
			continue;
		}
		if (IFileManager::Get().FileExists(*FElysiumContentPaths::NpcGlb(*Stem)))
		{
			++Exported;
		}
		else
		{
			AddError(FString::Printf(TEXT("%s exports as stem '%s' but %s is missing"),
				*Model, **Stem, *FElysiumContentPaths::NpcGlb(*Stem)));
		}
	}

	AddInfo(FString::Printf(TEXT("player bodies: %d playable clans × %d slots -> %d distinct models, %d with a glb"),
		PlayableTemplates, PlayableSlots, Models.Num(), Exported));

	// The counts are the drift alarm on the table itself: a patch that adds an armour tier or a
	// clan changes them, and the seed must be re-run rather than silently covering less.
	TestEqual(TEXT("seven Player_* clan templates"), PlayableTemplates, 7);
	TestEqual(TEXT("they fill 84 indexed body slots"), PlayableSlots, 84);
	TestEqual(TEXT("the whole table names 56 distinct body models"), Models.Num(), 56);
	TestEqual(TEXT("every player body is exported"), Exported, Models.Num());

	// The same lookup as the character screen's stage performs, through the shared helper — so the
	// thing this test proves and the thing the UI calls are one path, not two that agree today.
	{
		FElysiumClanTable Clans;
		FString ClanError;
		if (TestTrue(TEXT("the clan table loads"), Clans.Load(ClanError)))
		{
			int32 Resolved = 0;
			for (int32 Clan = 2; Clan <= 8; ++Clan)
			{
				for (bool bFemale : { false, true })
				{
					const FString Stem = Clans.PlayerBodyStem(Clan, bFemale, /*ArmorSlot*/ 0);
					if (Stem.IsEmpty() ||
						!IFileManager::Get().FileExists(*FElysiumContentPaths::NpcGlb(Stem)))
					{
						AddError(FString::Printf(TEXT("no exported body for clan %d %s (stem '%s')"),
							Clan, bFemale ? TEXT("female") : TEXT("male"), *Stem));
						continue;
					}
					++Resolved;
				}
			}
			TestEqual(TEXT("both bodies of all seven clans resolve through PlayerBodyStem"),
				Resolved, 14);
			// The un-indexed key is an NPC model, so the helper must not answer for a slot that has
			// no indexed entry.
			TestTrue(TEXT("an out-of-range armour slot resolves to nothing"),
				Clans.PlayerBodyStem(2, false, /*ArmorSlot*/ 9).IsEmpty());
			TestTrue(TEXT("and a non-playable clan index does too"),
				Clans.PlayerBodyStem(1, false).IsEmpty());
		}
	}

	return true;
}

// =====================================================================================
// 12.1 opening content: the authored camera graphs are closed acyclic chains, the six embrace
// props resolve through npc_index v4 with every clip their wires request, and the player material
// keeps the masked/dithered ModelAlpha contract the scripted-camera body path depends on.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumOpeningCameraContentTest,
	"Elysium.Content.OpeningCameraTracks", GElysiumContentTestFlags)
bool FElysiumOpeningCameraContentTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("maps") })) return true;
	const FString Path = FElysiumContentPaths::MapEnts(TEXT("sp_theatre"));
	if (!IFileManager::Get().FileExists(*Path))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: sp_theatre has not been exported"));
		return true;
	}
	FElysiumEntityDefs Defs;
	if (!TestTrue(TEXT("sp_theatre entities parse"), FElysiumEntityDefs::Parse(Path, Defs)))
	{
		return true;
	}

	TMap<FString, const FElysiumEntityDef*> NamedKeys;
	TArray<const FElysiumEntityDef*> Tracks;
	for (const FElysiumEntityDef& Def : Defs.Defs)
	{
		const bool bTrack = Def.Classname.Equals(TEXT("camera_track"), ESearchCase::IgnoreCase);
		const bool bKey = Def.Classname.Equals(TEXT("camera_keyframe"), ESearchCase::IgnoreCase);
		if (!bTrack && !bKey)
		{
			continue;
		}
		if (!Def.TargetName.IsEmpty())
		{
			const FString Name = Def.TargetName.ToLower();
			if (NamedKeys.Contains(Name))
			{
				AddError(FString::Printf(TEXT("duplicate camera key targetname: %s"), *Def.TargetName));
			}
			NamedKeys.Add(Name, &Def);
		}
		if (bTrack)
		{
			Tracks.Add(&Def);
		}
	}

	TestEqual(TEXT("theatre retains all eight camera track roots"), Tracks.Num(), 8);
	TMap<FString, int32> ChainLengths;
	for (const FElysiumEntityDef* Track : Tracks)
	{
		TSet<FString> Seen;
		const FElysiumEntityDef* Cursor = Track;
		int32 Length = 0;
		while (Cursor)
		{
			const FString Current = Cursor->TargetName.ToLower();
			if (Seen.Contains(Current))
			{
				AddError(FString::Printf(TEXT("camera chain %s cycles at %s"),
					*Track->TargetName, *Cursor->TargetName));
				break;
			}
			Seen.Add(Current);
			++Length;
			const FString Next = Cursor->Keys.FindRef(TEXT("NextKey"));
			if (Next.IsEmpty())
			{
				break;
			}
			const FElysiumEntityDef* const* Found = NamedKeys.Find(Next.ToLower());
			if (!Found)
			{
				AddError(FString::Printf(TEXT("camera chain %s has missing NextKey endpoint %s"),
					*Track->TargetName, *Next));
				break;
			}
			Cursor = *Found;
		}
		ChainLengths.Add(Track->TargetName.ToLower(), Length);
	}
	TestEqual(TEXT("embrace position chain remains complete"),
		ChainLengths.FindRef(TEXT("embrace_camera")), 24);
	TestEqual(TEXT("embrace target chain remains complete"),
		ChainLengths.FindRef(TEXT("embrace_target")), 19);

	// Compile the real chains through the same plain-value path used at runtime. The two independent
	// streams can use different keys, but their cuts, dwells and moves must share one edit clock.
	using namespace ElysiumCameraTrack;
	auto BuildCameraPath = [&NamedKeys](const TCHAR* RootName, FPath& Out, TArray<FString>& OutNames)
	{
		Out = FPath();
		OutNames.Reset();
		const FElysiumEntityDef* const* Root = NamedKeys.Find(FString(RootName).ToLower());
		if (!Root)
		{
			return false;
		}
		auto FloatKey = [](const FElysiumEntityDef& Def, const TCHAR* Name, float Default)
		{
			const FString* Value = Def.Keys.Find(Name);
			return Value ? FCString::Atof(**Value) : Default;
		};
		const FElysiumEntityDef* Cursor = *Root;
		TSet<FString> Seen;
		while (Cursor)
		{
			const FString Current = Cursor->TargetName.ToLower();
			if (Seen.Contains(Current))
			{
				return false;
			}
			Seen.Add(Current);
			FPoint Point;
			Point.Position = Cursor->Origin;
			Point.Roll = FloatKey(*Cursor, TEXT("Roll"), 0.0f);
			Point.FocalLength = FloatKey(*Cursor, TEXT("FocalLength"), 0.0f);
			Point.bTimeControl = Cursor->Keys.FindRef(TEXT("TimeControl")).Equals(TEXT("1"));
			Point.MoveSpeed = FloatKey(*Cursor, TEXT("MoveSpeed"), 64.0f);
			Point.MoveTime = FloatKey(*Cursor, TEXT("MoveTime"), 0.0f);
			Point.Pause = FloatKey(*Cursor, TEXT("Pause"), 0.0f);
			Point.RateIn = FloatKey(*Cursor, TEXT("RateIn"), 1.0f);
			Point.RateOut = FloatKey(*Cursor, TEXT("RateOut"), 1.0f);
			Point.bCorner = Cursor->Keys.FindRef(TEXT("Corner")).Equals(TEXT("1"));
			Out.Points.Add(Point);
			OutNames.Add(Current);
			const FString Next = Cursor->Keys.FindRef(TEXT("NextKey"));
			if (Next.IsEmpty())
			{
				break;
			}
			const FElysiumEntityDef* const* Found = NamedKeys.Find(Next.ToLower());
			if (!Found)
			{
				return false;
			}
			Cursor = *Found;
		}
		Out.RebuildTimes();
		return Out.Points.Num() > 0;
	};
	auto ArrivalAt = [](const FPath& Path, const TArray<FString>& Names, const TCHAR* Name)
	{
		const int32 Index = Names.IndexOfByKey(FString(Name).ToLower());
		return Index != INDEX_NONE && Path.Arrivals.IsValidIndex(Index) ? Path.Arrivals[Index] : -1.0f;
	};

	FPath EmbracePosition;
	FPath EmbraceTarget;
	FPath CourtroomPosition;
	FPath CourtroomTarget;
	FPath WalkBackPosition;
	FPath EscortPosition;
	FPath EscortTarget;
	TArray<FString> EmbracePositionNames;
	TArray<FString> EmbraceTargetNames;
	TArray<FString> CourtroomPositionNames;
	TArray<FString> CourtroomTargetNames;
	TArray<FString> WalkBackPositionNames;
	TArray<FString> EscortPositionNames;
	TArray<FString> EscortTargetNames;
	const bool bPathsBuilt = BuildCameraPath(TEXT("embrace_camera"), EmbracePosition, EmbracePositionNames)
		&& BuildCameraPath(TEXT("embrace_target"), EmbraceTarget, EmbraceTargetNames)
		&& BuildCameraPath(TEXT("courtroom_camera_1"), CourtroomPosition, CourtroomPositionNames)
		&& BuildCameraPath(TEXT("courtroom_target_1"), CourtroomTarget, CourtroomTargetNames)
		&& BuildCameraPath(TEXT("walk_out_back_camera"), WalkBackPosition, WalkBackPositionNames)
		&& BuildCameraPath(TEXT("cinematic_shot_1"), EscortPosition, EscortPositionNames)
		&& BuildCameraPath(TEXT("cinematic_shot_2"), EscortTarget, EscortTargetNames);
	if (!TestTrue(TEXT("the opening camera paths compile through the runtime timing model"), bPathsBuilt))
	{
		return true;
	}
	TestTrue(TEXT("embrace position and target streams end on the same edit"),
		FMath::IsNearlyEqual(EmbracePosition.EndTime, EmbraceTarget.EndTime, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("the recovered embrace edit clock remains 58.87 seconds"),
		FMath::IsNearlyEqual(EmbracePosition.EndTime, 58.87f, 0.001f));
	TestTrue(TEXT("the courtroom streams include their 3.4-second root dwell"),
		FMath::IsNearlyEqual(CourtroomPosition.EndTime, 159.86f, 0.001f)
			&& FMath::IsNearlyEqual(CourtroomTarget.EndTime, 159.86f, 0.001f));
	TestTrue(TEXT("courtroom target 36 reaches the fade clock at 150.19 seconds"),
		FMath::IsNearlyEqual(ArrivalAt(CourtroomTarget, CourtroomTargetNames,
			TEXT("courtroom_target_36")), 150.19f, 0.001f));
	// The retail execution shot is a deliberate compound move, not a held edit: both streams leave
	// key 27 together for 0.87 seconds while the position track widens from a 40 mm to a 35 mm lens.
	const float ExecutionStart = ArrivalAt(CourtroomPosition, CourtroomPositionNames,
		TEXT("courtroom_camera_27"));
	const float ExecutionEnd = ArrivalAt(CourtroomPosition, CourtroomPositionNames,
		TEXT("courtroom_camera_27a"));
	const float ExecutionTargetStart = ArrivalAt(CourtroomTarget, CourtroomTargetNames,
		TEXT("courtroom_target_27"));
	const float ExecutionTargetEnd = ArrivalAt(CourtroomTarget, CourtroomTargetNames,
		TEXT("courtroom_target_27a"));
	TestTrue(TEXT("the execution position and target moves share the 120.45-to-121.32 edit"),
		FMath::IsNearlyEqual(ExecutionStart, 120.45f, 0.001f)
			&& FMath::IsNearlyEqual(ExecutionEnd, 121.32f, 0.001f)
			&& FMath::IsNearlyEqual(ExecutionTargetStart, ExecutionStart, KINDA_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(ExecutionTargetEnd, ExecutionEnd, KINDA_SMALL_NUMBER));
	FSample ExecutionPositionStart;
	FSample ExecutionPositionMiddle;
	FSample ExecutionPositionEnd;
	FSample ExecutionTargetAtStart;
	FSample ExecutionTargetAtMiddle;
	FSample ExecutionTargetAtEnd;
	const float ExecutionMiddle = 0.5f * (ExecutionStart + ExecutionEnd);
	const bool bExecutionSamples = CourtroomPosition.Sample(ExecutionStart, ExecutionPositionStart)
		&& CourtroomPosition.Sample(ExecutionMiddle, ExecutionPositionMiddle)
		&& CourtroomPosition.Sample(ExecutionEnd, ExecutionPositionEnd)
		&& CourtroomTarget.Sample(ExecutionTargetStart, ExecutionTargetAtStart)
		&& CourtroomTarget.Sample(ExecutionMiddle, ExecutionTargetAtMiddle)
		&& CourtroomTarget.Sample(ExecutionTargetEnd, ExecutionTargetAtEnd);
	TestTrue(TEXT("the execution edit samples both authored value streams"), bExecutionSamples);
	if (bExecutionSamples)
	{
		TestTrue(TEXT("the execution camera is moving between its authored endpoints"),
			!ExecutionPositionMiddle.bInPause
				&& !ExecutionPositionMiddle.Position.Equals(ExecutionPositionStart.Position, 0.01f)
				&& !ExecutionPositionMiddle.Position.Equals(ExecutionPositionEnd.Position, 0.01f));
		TestTrue(TEXT("the execution look-at is moving between its authored endpoints"),
			!ExecutionTargetAtMiddle.bInPause
				&& !ExecutionTargetAtMiddle.Position.Equals(ExecutionTargetAtStart.Position, 0.01f)
				&& !ExecutionTargetAtMiddle.Position.Equals(ExecutionTargetAtEnd.Position, 0.01f));
		TestTrue(TEXT("the execution lens widens continuously from 40 mm to 35 mm"),
			FMath::IsNearlyEqual(ExecutionPositionStart.FieldOfView,
				FocalLengthToHorizontalFov(40.0f), 0.001f)
				&& ExecutionPositionMiddle.FieldOfView > ExecutionPositionStart.FieldOfView
				&& ExecutionPositionMiddle.FieldOfView < ExecutionPositionEnd.FieldOfView
				&& FMath::IsNearlyEqual(ExecutionPositionEnd.FieldOfView,
					FocalLengthToHorizontalFov(35.0f), 0.001f));

		// Run the authored entities too: this covers field registration, the substrate clock, the two
		// independently selected roles, and composition onto the embodiment's value-shot seam.
		TSet<FString> CourtroomNames;
		for (const FString& Name : CourtroomPositionNames) { CourtroomNames.Add(Name); }
		for (const FString& Name : CourtroomTargetNames) { CourtroomNames.Add(Name); }
		FElysiumEntityDefs ExecutionDefs;
		ExecutionDefs.MapName = TEXT("__sp_theatre_execution_camera_test__");
		for (const FElysiumEntityDef& Def : Defs.Defs)
		{
			if (CourtroomNames.Contains(Def.TargetName.ToLower()))
			{
				FElysiumEntityDef Copy = Def;
				Copy.Outputs.Reset();
				ExecutionDefs.Defs.Add(MoveTemp(Copy));
			}
		}
		FElysiumRecordingServices ExecutionServices;
		ExecutionServices.bHasPlayer = true;
		FElysiumEntityWorld ExecutionWorld(nullptr, nullptr, ExecutionServices.Bundle());
		ExecutionWorld.Load(MoveTemp(ExecutionDefs));
		ExecutionWorld.Activate(0.0);
		ExecutionWorld.AcceptInput(TEXT("courtroom_target_1"),
			FName(TEXT("PlayAsCameraTarget")), FElysiumVariant::Void(),
			FElysiumEntityHandle(), FElysiumEntityHandle());
		ExecutionWorld.AcceptInput(TEXT("courtroom_camera_1"),
			FName(TEXT("PlayAsCameraPosition")), FElysiumVariant::Void(),
			FElysiumEntityHandle(), FElysiumEntityHandle());
		ExecutionWorld.Tick(ExecutionMiddle);
		TestTrue(TEXT("the live entity pair publishes the execution camera midpoint"),
			ExecutionServices.LastCameraShot.Origin.Equals(ExecutionPositionMiddle.Position, 0.01f));
		TestTrue(TEXT("the live entity pair publishes the execution target midpoint"),
			ExecutionServices.LastCameraShot.bUseLookAt
				&& ExecutionServices.LastCameraShot.LookAt.Equals(
					ExecutionTargetAtMiddle.Position, 0.01f));
		TestTrue(TEXT("the live entity pair publishes the execution lens midpoint"),
			FMath::IsNearlyEqual(ExecutionServices.LastCameraShot.FieldOfView,
				ExecutionPositionMiddle.FieldOfView, 0.001f));
	}
	TestTrue(TEXT("the walk-back edit waits nine seconds on its root"),
		WalkBackPosition.Arrivals.Num() > 1
			&& FMath::IsNearlyEqual(WalkBackPosition.Arrivals[1], 9.0f, 0.001f));
	TestTrue(TEXT("the superseded walk-back clock retains its twenty-second cleanup dwell"),
		FMath::IsNearlyEqual(WalkBackPosition.EndTime, 29.0f, 0.001f));
	TestTrue(TEXT("the escort streams include their 2.5-second root dwell"),
		FMath::IsNearlyEqual(EscortPosition.EndTime, 61.21f, 0.001f)
			&& FMath::IsNearlyEqual(EscortTarget.EndTime, 61.21f, 0.001f));
	TestTrue(TEXT("walk_out_cam_k reaches the fade/travel clock at 40.21 seconds"),
		FMath::IsNearlyEqual(ArrivalAt(EscortPosition, EscortPositionNames,
			TEXT("walk_out_cam_k")), 40.21f, 0.001f));

	// The Embrace transition is deliberately entity I/O rather than camera staging: the final camera
	// key waits four seconds, point_teleport moves !player immediately, and the courtroom volume is
	// admitted on the following movement-touch pass. Keep both the authored wire and that phase seam
	// executable here so a content or runtime regression cannot silently turn the scene into a stall.
	auto FindNamedDef = [&Defs](const TCHAR* Name) -> const FElysiumEntityDef*
	{
		for (const FElysiumEntityDef& Def : Defs.Defs)
		{
			if (Def.TargetName.Equals(Name, ESearchCase::IgnoreCase))
			{
				return &Def;
			}
		}
		return nullptr;
	};
	const FElysiumEntityDef* EmbraceKey = FindNamedDef(TEXT("embrace_camera_22"));
	const FElysiumEntityDef* MoveActors = FindNamedDef(TEXT("move_embrace_actors"));
	const FElysiumEntityDef* StartCourtroom = FindNamedDef(TEXT("start_courtroom"));
	if (!TestNotNull(TEXT("Embrace final camera key exists"), EmbraceKey)
		|| !TestNotNull(TEXT("Embrace actor teleporter exists"), MoveActors)
		|| !TestNotNull(TEXT("courtroom start trigger exists"), StartCourtroom))
	{
		return true;
	}
	const bool bAuthoredTeleportWire = EmbraceKey->Outputs.ContainsByPredicate(
		[](const FElysiumOutputDef& Wire)
		{
			return Wire.Name.Equals(TEXT("OnReachedKeyframe"), ESearchCase::IgnoreCase)
				&& Wire.Target.Equals(TEXT("move_embrace_actors"), ESearchCase::IgnoreCase)
				&& Wire.Input.Equals(TEXT("Teleport"), ESearchCase::IgnoreCase)
				&& FMath::IsNearlyEqual(Wire.Delay, 4.f, KINDA_SMALL_NUMBER);
		});
	TestTrue(TEXT("embrace_camera_22 waits four seconds before teleporting the actors"),
		bAuthoredTeleportWire);
	TestEqual(TEXT("the Embrace teleporter targets the player at input time"),
		MoveActors->Keys.FindRef(TEXT("target")), FString(TEXT("!player")));
	const bool bAuthoredCourtroomWire = StartCourtroom->Outputs.ContainsByPredicate(
		[](const FElysiumOutputDef& Wire)
		{
			return Wire.Name.Equals(TEXT("OnTrigger"), ESearchCase::IgnoreCase)
				&& Wire.Target.Equals(TEXT("courtroom_scene_relay"), ESearchCase::IgnoreCase)
				&& Wire.Input.Equals(TEXT("Trigger"), ESearchCase::IgnoreCase)
				&& FMath::IsNearlyEqual(Wire.Delay, 1.f, KINDA_SMALL_NUMBER);
		});
	TestTrue(TEXT("start_courtroom retains its one-second relay wire"), bAuthoredCourtroomWire);

	FBox CourtroomBounds(ForceInit);
	for (const FElysiumConvexHull& Hull : StartCourtroom->Hulls)
	{
		for (const FVector& Vertex : Hull.Vertices)
		{
			CourtroomBounds += StartCourtroom->Origin + Vertex;
		}
	}
	TestTrue(TEXT("the cached teleporter destination is physically inside start_courtroom"),
		CourtroomBounds.IsValid && CourtroomBounds.IsInsideOrOn(MoveActors->Origin));

	FElysiumEntityDefs TransitionDefs;
	TransitionDefs.MapName = TEXT("__sp_theatre_teleport_touch_test__");
	{
		FElysiumEntityDef Copy = *EmbraceKey;
		Copy.Outputs.RemoveAll([](const FElysiumOutputDef& Wire)
		{
			return !Wire.Target.Equals(TEXT("move_embrace_actors"), ESearchCase::IgnoreCase);
		});
		TransitionDefs.Defs.Add(MoveTemp(Copy));
	}
	TransitionDefs.Defs.Add(*MoveActors);
	{
		FElysiumEntityDef Copy = *StartCourtroom;
		Copy.Outputs.Reset();
		FElysiumOutputDef Observe;
		Observe.Name = TEXT("OnTrigger");
		Observe.Target = TEXT("courtroom_counter");
		Observe.Input = TEXT("Add");
		Observe.Param = TEXT("1");
		Observe.Delay = 1.f; // retain the authored start_courtroom -> relay delay
		Copy.Outputs.Add(MoveTemp(Observe));
		TransitionDefs.Defs.Add(MoveTemp(Copy));
	}
	FElysiumEntityDef CounterDef;
	CounterDef.Classname = TEXT("math_counter");
	CounterDef.TargetName = TEXT("courtroom_counter");
	TransitionDefs.Defs.Add(MoveTemp(CounterDef));

	FElysiumRecordingServices TransitionServices;
	TransitionServices.bHasPlayer = true;
	TransitionServices.PlayerLocation = FVector(-5000.f, -5000.f, 0.f);
	FElysiumEntityWorld TransitionWorld(nullptr, nullptr, TransitionServices.Bundle());
	TransitionWorld.Load(MoveTemp(TransitionDefs));
	const FElysiumEntityHandle TransitionPlayer = TransitionWorld.SpawnPlayer();
	TransitionWorld.Activate(0.0);
	FElysiumEntity* LiveEmbraceKey = TransitionWorld.FindByName(TEXT("embrace_camera_22"));
	FElysiumEntity* LiveStartCourtroom = TransitionWorld.FindByName(TEXT("start_courtroom"));
	FElysiumEntity* CourtroomCounter = TransitionWorld.FindByName(TEXT("courtroom_counter"));
	if (!TestNotNull(TEXT("live Embrace key resolves"), LiveEmbraceKey)
		|| !TestNotNull(TEXT("live courtroom trigger resolves"), LiveStartCourtroom)
		|| !TestNotNull(TEXT("live courtroom observer resolves"), CourtroomCounter))
	{
		return true;
	}
	auto CounterValue = [](const FElysiumEntity* Entity) -> float
	{
		TArray<TPair<FString, FString>> State;
		Entity->GetDebugState(State);
		for (const TPair<FString, FString>& Pair : State)
		{
			if (Pair.Key.Equals(TEXT("Value"), ESearchCase::IgnoreCase))
			{
				return FCString::Atof(*Pair.Value);
			}
		}
		return -1.f;
	};

	LiveEmbraceKey->FireOutput(FName(TEXT("OnReachedKeyframe")), TransitionPlayer);
	TransitionWorld.Tick(3.999);
	TestFalse(TEXT("the player does not move before the authored four-second delay"),
		TransitionServices.PlayerLocation.Equals(MoveActors->Origin));
	TransitionWorld.Tick(4.0);
	TestTrue(TEXT("the point teleport moves the player when the delayed I/O becomes due"),
		TransitionServices.PlayerLocation.Equals(MoveActors->Origin));
	TestEqual(TEXT("teleport alone does not run trigger outputs in the event phase"),
		CounterValue(CourtroomCounter), 0.f);

	const FElysiumEntityHandle StartCourtroomHandle = LiveStartCourtroom->Handle;
	TransitionWorld.ReconcilePlayerTouches(MakeArrayView(&StartCourtroomHandle, 1));
	TestEqual(TEXT("the following movement-touch pass admits the courtroom trigger"),
		CounterValue(CourtroomCounter), 0.f);
	TransitionWorld.Tick(4.999);
	TestEqual(TEXT("the authored one-second courtroom delay remains pending"),
		CounterValue(CourtroomCounter), 0.f);
	TransitionWorld.Tick(5.0);
	TestEqual(TEXT("the courtroom relay edge runs one second after movement touch"),
		CounterValue(CourtroomCounter), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumOpeningAnimatedPropsContentTest,
	"Elysium.Content.OpeningAnimatedProps", GElysiumContentTestFlags)
bool FElysiumOpeningAnimatedPropsContentTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("maps") })) return true;
	const FString Path = FElysiumContentPaths::MapEnts(TEXT("sp_theatre"));
	if (!IFileManager::Get().FileExists(*Path))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: sp_theatre has not been exported"));
		return true;
	}
	FElysiumNpcIndex Index;
	FString Error;
	if (!TestTrue(FString::Printf(TEXT("npc_index loads: %s"), *Error), Index.Load(Error)))
	{
		return true;
	}
	// v6 is what carries declaration order and the selection keys the rest pose is chosen with.
	TestTrue(TEXT("animated-prop manifest schema is at least v6"), Index.ManifestVersion >= 6);

	// The theatre's cinematic props tag their idle `ACT_VM_IDLE`, not `ACT_IDLE`, so every one of
	// them resolves its rest pose through retail's sequence-index-0 fallback rather than the
	// activity lookup. Pinning the exact clip here is what would catch an exporter that went back
	// to sorting the labels.
	{
		const FElysiumAnimatedPropEntry* Sword =
			Index.FindAnimatedProp(TEXT("models/cinematic/santa_monica/courtroom/cin_sheriff_sword.mdl"));
		if (TestNotNull(TEXT("the courtroom sword is indexed"), Sword))
		{
			TestEqual(TEXT("the sword rests on its declared first sequence"),
				Sword->RestSequence(), FString(TEXT("idle01")));
			const FElysiumPropClip* Scene = Sword->FindClip(TEXT("scene"));
			if (TestNotNull(TEXT("the sword carries its scene clip"), Scene))
			{
				TestFalse(TEXT("the scene clip is a one shot"), Scene->IsLooping());
				// The sword is a ghost rig: its component never moves, so the clip's reach is the
				// only thing that can keep it on screen. Its mesh spans ~2 m and its scene clip
				// draws it out past 20, which is the gap the bind-pose bounds cannot see.
				TestTrue(TEXT("the scene clip declares the reach it needs"),
					Scene->BoundsRadiusMeters > 20.f);
			}
		}
		// `drknobantique` is the model whose declaration order and alphabetical order disagree.
		const FElysiumAnimatedPropEntry* Knob =
			Index.FindAnimatedProp(TEXT("models/scenery/structural/doorknoba/drknobantique.mdl"));
		if (TestNotNull(TEXT("the antique doorknob is indexed"), Knob))
		{
			TestEqual(TEXT("declaration order beats alphabetical for the rest pose"),
				Knob->RestSequence(), FString(TEXT("idle")));
		}
		// A model whose only sequence is a single static frame is not an animated prop at all, so
		// the exporter must leave it out and the prop keeps its baked static mesh.
		for (const TCHAR* Motionless : { TEXT("models/scenery/furniture/lampfloor/lampfloor.mdl"),
			TEXT("models/scenery/theater/stage_light.mdl") })
		{
			TestNull(FString::Printf(TEXT("%s is not indexed as animated"), Motionless),
				Index.FindAnimatedProp(Motionless));
		}
	}

	FElysiumEntityDefs Defs;
	if (!TestTrue(TEXT("sp_theatre entities parse"), FElysiumEntityDefs::Parse(Path, Defs)))
	{
		return true;
	}
	TMap<FString, const FElysiumEntityDef*> ByName;
	for (const FElysiumEntityDef& Def : Defs.Defs)
	{
		if (!Def.TargetName.IsEmpty())
		{
			ByName.FindOrAdd(Def.TargetName.ToLower(), &Def);
		}
	}

	struct FExpected
	{
		const TCHAR* Target;
		const TCHAR* ClipA;
		const TCHAR* ClipB;
	};
	const FExpected Expected[] =
	{
		{ TEXT("wineglass_1"), TEXT("wineglass_1"), nullptr },
		{ TEXT("wineglass_2"), TEXT("wineglass_2"), nullptr },
		{ TEXT("pc_pre"),      TEXT("idle01"),     TEXT("pc_pre") },
		{ TEXT("pc_post"),     TEXT("pc_post"),    TEXT("pc_post_female") },
		{ TEXT("sire_pre"),    TEXT("idle01"),     TEXT("sire_pre") },
		{ TEXT("sire_post"),   TEXT("idle01"),     TEXT("sire_post") },
		{ TEXT("sire_fly"),    TEXT("idle01"),     TEXT("sire_fly") },
	};

	int32 Resolved = 0;
	for (const FExpected& Want : Expected)
	{
		const FElysiumEntityDef* const* DefPtr = ByName.Find(FString(Want.Target).ToLower());
		if (!TestNotNull(FString::Printf(TEXT("opening prop %s exists"), Want.Target),
			reinterpret_cast<const void*>(DefPtr)))
		{
			continue;
		}
		const FElysiumEntityDef& Def = **DefPtr;
		TestTrue(FString::Printf(TEXT("%s is prop_dynamic"), Want.Target),
			Def.Classname.Equals(TEXT("prop_dynamic"), ESearchCase::IgnoreCase));
		const FString Model = Def.Keys.FindRef(TEXT("model"));
		const FElysiumAnimatedPropEntry* Entry = Index.FindAnimatedProp(Model);
		if (!TestNotNull(FString::Printf(TEXT("%s model is indexed as animated"), Want.Target), Entry))
		{
			continue;
		}
		// The container the bake reads, not the `.glb` beside it -- nothing the game loads comes
		// off that one any more.
		TestTrue(FString::Printf(TEXT("%s declares its .eskm container"), Want.Target),
			!Entry->Eskm.IsEmpty());
		TestTrue(FString::Printf(TEXT("%s generated container exists"), Want.Target),
			IFileManager::Get().FileExists(
				*(FElysiumContentPaths::NpcDir() / Entry->Eskm)));
		TestTrue(FString::Printf(TEXT("%s clip %s resolves"), Want.Target, Want.ClipA),
			Entry->HasClip(Want.ClipA));
		if (Want.ClipB)
		{
			TestTrue(FString::Printf(TEXT("%s clip %s resolves"), Want.Target, Want.ClipB),
				Entry->HasClip(Want.ClipB));
		}
		++Resolved;
	}
	TestEqual(TEXT("all seven opening animated props resolve"), Resolved, static_cast<int32>(UE_ARRAY_COUNT(Expected)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerBodyMaterialTest,
	"Elysium.Content.PlayerBodyMaterial", GElysiumContentTestFlags)
bool FElysiumPlayerBodyMaterialTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("policy") })) return true;
	UMaterial* Material = LoadObject<UMaterial>(nullptr,
		TEXT("/Game/VtMB/Materials/M_PlayerBody.M_PlayerBody"));
	if (!TestNotNull(TEXT("M_PlayerBody asset loads"), Material))
	{
		return true;
	}
	TestEqual(TEXT("player body material is masked"), Material->GetBlendMode(), BLEND_Masked);
	TestTrue(TEXT("player body uses native dithered opacity masking"), Material->DitherOpacityMask != 0);
	TestTrue(TEXT("player body material is compiled for skeletal meshes"),
		Material->GetUsageByFlag(MATUSAGE_SkeletalMesh));
	// uv run elysium test runs under NullRHI, so the material owns no rendering-platform resource by default.
	// Create and synchronously compile the exact platform the game launches instead of mistaking an
	// absent NullRHI resource for a shader failure.
	TArray<FMaterialResource*> Sm6Resources;
	FMaterialResource* Sm6 = FindOrCreateMaterialResource(Sm6Resources, Material, nullptr,
		SP_PCD3D_SM6, EMaterialQualityLevel::High);
	if (TestNotNull(TEXT("player body creates a PCD3D_SM6 material resource"), Sm6))
	{
		TestTrue(TEXT("player body compiles synchronously for PCD3D_SM6"),
			Sm6->CacheShaders(EMaterialShaderPrecompileMode::None));
		for (const FString& Error : Sm6->GetCompileErrors())
		{
			AddError(FString::Printf(TEXT("PCD3D_SM6: %s"), *Error));
		}
		TestNotNull(TEXT("player body has a ready PCD3D_SM6 shader map"),
			Sm6->GetGameThreadShaderMap());
	}
	FMaterial::DeferredDeleteArray(Sm6Resources);
	TArray<FMaterialParameterInfo> Scalars;
	TArray<FGuid> ScalarIds;
	Material->GetAllScalarParameterInfo(Scalars, ScalarIds);
	TestTrue(TEXT("player material exposes ModelAlpha"),
		Scalars.ContainsByPredicate([](const FMaterialParameterInfo& Info)
		{
			return Info.Name == FName(TEXT("ModelAlpha"));
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSantaMonicaNpcRoutesContentTest,
	"Elysium.Content.SantaMonicaNpcRoutes", GElysiumContentTestFlags)
bool FElysiumSantaMonicaNpcRoutesContentTest::RunTest(const FString&)
{
	const FString EntsPath = FElysiumContentPaths::MapEnts(TEXT("sm_hub_1"));
	if (!IFileManager::Get().FileExists(*EntsPath))
	{
		AddInfo(FString::Printf(TEXT("ELYSIUM_TEST_ABSTAIN: sm_hub_1 entities not exported: %s"), *EntsPath));
		return true;
	}
	FElysiumEntityDefs Defs;
	if (!TestTrue(TEXT("sm_hub_1 entities parse"), FElysiumEntityDefs::Parse(EntsPath, Defs)))
	{
		return false;
	}

	int32 InterestingPlaces = 0;
	int32 InterestingPedestrians = 0;
	TSet<FString> PatrolNodes;
	for (const FElysiumEntityDef& Def : Defs.Defs)
	{
		if (Def.Classname.Equals(TEXT("intersting_place"), ESearchCase::IgnoreCase))
		{
			++InterestingPlaces;
		}
		else if (Def.Classname.Equals(TEXT("info_node_patrol_point"), ESearchCase::IgnoreCase))
		{
			// The name is the `Group` keyvalue, not the targetname: all 34 of Santa Monica's patrol
			// points ship with an empty targetname, and `FollowPatrolPath("s1 s2 ...")` names their
			// groups. Reading targetname here collapses the whole set onto one empty string.
			PatrolNodes.Add(Def.Keys.FindRef(TEXT("Group")).ToLower());
		}
		else if (Def.Classname.Equals(TEXT("npc_VPedestrian"), ESearchCase::IgnoreCase)
			&& Def.Keys.FindRef(TEXT("use_interesting")) == TEXT("1"))
		{
			++InterestingPedestrians;
		}
	}
	TestEqual(TEXT("all authored Santa Monica interesting places survived export"),
		InterestingPlaces, 76);
	TestEqual(TEXT("all authored ambient pedestrians survived export"),
		InterestingPedestrians, 17);
	TestEqual(TEXT("all authored named patrol nodes survived export"), PatrolNodes.Num(), 34);
	for (const TCHAR* Point : { TEXT("s1"), TEXT("s2"), TEXT("s3"), TEXT("s4"), TEXT("s7"),
		TEXT("s8"), TEXT("s9"), TEXT("s10"), TEXT("s11"), TEXT("s12"), TEXT("s13"),
		TEXT("s14"), TEXT("n1"), TEXT("n2"), TEXT("n3"), TEXT("n4"), TEXT("n5"),
		TEXT("n6"), TEXT("n7"), TEXT("n8"), TEXT("n9"), TEXT("n10") })
	{
		TestTrue(FString::Printf(TEXT("retail patrol point %s resolves"), Point),
			PatrolNodes.Contains(Point));
	}

	FElysiumInterestingPlaceTable Types;
	FString Error;
	if (!TestTrue(TEXT("retail interesting-place type table loads"), Types.Load(Error)))
	{
		AddError(Error);
		return false;
	}
	for (const TCHAR* Type : { TEXT("Idle"), TEXT("Citizen_Idle"), TEXT("Doorknock"),
		TEXT("bum_rest"), TEXT("wall_lean"), TEXT("conversation_normal"), TEXT("cigarette"),
		TEXT("piss"), TEXT("cellphone"), TEXT("can_drink"), TEXT("payphone") })
	{
		TestNotNull(FString::Printf(TEXT("interesting-place type %s resolves"), Type),
			Types.Find(Type));
	}

	FString Script;
	const FString ScriptPath = FElysiumContentPaths::ScriptModuleFile(TEXT("santamonica"));
	if (TestTrue(TEXT("Santa Monica level script reads"),
		FFileHelper::LoadFileToString(Script, *ScriptPath)))
	{
		TestTrue(TEXT("retail south cop route remains authored"),
			Script.Contains(TEXT("FollowPatrolPath(\"s1 s2 s3 s4 s7 s8 s9 s10 s11 s12 s13 s14\")")));
		TestTrue(TEXT("retail north cop route remains authored"),
			Script.Contains(TEXT("FollowPatrolPath(\"n1 n2 n3 n4 n1 n2 n3 n4 n5 n6 n7 n8 n9 n10 n1 n2 n3 n4\")")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTutorialJackBootstrapContentTest,
	"Elysium.Content.TutorialJackBootstrap", GElysiumContentTestFlags)

bool FElysiumTutorialJackBootstrapContentTest::RunTest(const FString&)
{
	const FString EntsPath = FElysiumContentPaths::MapEnts(TEXT("sp_tutorial_1"));
	if (!IFileManager::Get().FileExists(*EntsPath))
	{
		AddInfo(FString::Printf(TEXT("ELYSIUM_TEST_ABSTAIN: sp_tutorial_1 entities not exported: %s"),
			*EntsPath));
		return true;
	}
	FElysiumEntityDefs Defs;
	if (!TestTrue(TEXT("sp_tutorial_1 entities parse"), FElysiumEntityDefs::Parse(EntsPath, Defs)))
	{
		return false;
	}

	const FElysiumEntityDef* Jack = nullptr;
	TMap<FString, FString> Group32Places;
	for (const FElysiumEntityDef& Def : Defs.Defs)
	{
		if (Def.TargetName.Equals(TEXT("Jack"), ESearchCase::IgnoreCase))
		{
			Jack = &Def;
		}
		if (Def.Classname.Equals(TEXT("intersting_place"), ESearchCase::IgnoreCase)
			&& Def.Keys.FindRef(TEXT("group_id")) == TEXT("32"))
		{
			Group32Places.Add(Def.TargetName, Def.Keys.FindRef(TEXT("origin")));
		}
	}
	if (!TestNotNull(TEXT("UP Plus Jack is exported"), Jack))
	{
		return false;
	}
	TestEqual(TEXT("Jack authored source origin remains the UP relocation"),
		Jack->Keys.FindRef(TEXT("origin")), FString(TEXT("144 7352 -199")));
	TestEqual(TEXT("Jack authored yaw remains 190"),
		Jack->Keys.FindRef(TEXT("angles")), FString(TEXT("0 190 0")));
	TestEqual(TEXT("Jack retains neutral disposition"),
		Jack->Keys.FindRef(TEXT("default_disposition")), FString(TEXT("Neutral")));
	TestEqual(TEXT("Jack selects the authored source shot"),
		Jack->Keys.FindRef(TEXT("default_camera")), FString(TEXT("Jack")));
	TestEqual(TEXT("Jack retains interesting-place admission"),
		Jack->Keys.FindRef(TEXT("use_interesting")), FString(TEXT("1")));
	TestEqual(TEXT("Jack remains constrained to group 32"),
		Jack->Keys.FindRef(TEXT("interesting_place_groups")), FString(TEXT("32")));

	TestEqual(TEXT("the UP map authors exactly three group-32 places"), Group32Places.Num(), 3);
	TestEqual(TEXT("group-32 window place remains remote"), Group32Places.FindRef(TEXT("ip_by_window")),
		FString(TEXT("85 132 112")));
	TestEqual(TEXT("group-32 disabled place remains remote"), Group32Places.FindRef(TEXT("ip_0b2")),
		FString(TEXT("-208 -16 -40")));
	TestEqual(TEXT("group-32 lean place remains at retail Jack's site"),
		Group32Places.FindRef(TEXT("ip_lean_1")), FString(TEXT("-221 -258 -32")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSantaMonicaRainContentTest,
	"Elysium.Content.SantaMonicaRain", GElysiumContentTestFlags)
bool FElysiumSantaMonicaRainContentTest::RunTest(const FString&)
{
	// This is a focused-map contract: an unrelated grid export may leave the corpus-wide
	// incomplete marker behind, but a present sm_hub_1 weather sidecar must be validated.
	const FString WeatherPath = FElysiumContentPaths::MapDir(TEXT("sm_hub_1"))
		/ TEXT("sm_hub_1.weather.json");
	if (!IFileManager::Get().FileExists(*WeatherPath))
	{
		AddInfo(FString::Printf(TEXT("ELYSIUM_TEST_ABSTAIN: sm_hub_1 weather not exported: %s"),
			*WeatherPath));
		return true;
	}
	FString WeatherText;
	TSharedPtr<FJsonObject> Weather;
	TestTrue(TEXT("weather sidecar reads"), FFileHelper::LoadFileToString(WeatherText, *WeatherPath));
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(WeatherText);
	if (!TestTrue(TEXT("weather sidecar parses"), FJsonSerializer::Deserialize(Reader, Weather)
		&& Weather.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("weather sidecar schema"), Weather->GetStringField(TEXT("schema")),
		FString(TEXT("elysium.map-weather")));
	TestEqual(TEXT("weather sidecar version"),
		static_cast<int32>(Weather->GetNumberField(TEXT("version"))), 1);
	TestEqual(TEXT("weather sidecar has two authored emitters"),
		Weather->GetArrayField(TEXT("emitters")).Num(), 2);
	const TSharedPtr<FJsonObject> HeightJson = Weather->GetObjectField(TEXT("height_texture"));
	TestEqual(TEXT("weather height contract resolution"),
		static_cast<int32>(HeightJson->GetNumberField(TEXT("resolution"))), 2048);
	TestEqual(TEXT("weather height contract format"), HeightJson->GetStringField(TEXT("format")),
		FString(TEXT("R16_UNORM")));

	UMaterialParameterCollection* Collection = LoadObject<UMaterialParameterCollection>(nullptr,
		TEXT("/Game/VtMB/Materials/MPC_ElysiumEnvironment.MPC_ElysiumEnvironment"));
	if (TestNotNull(TEXT("one environment MPC loads"), Collection))
	{
		const TArray<FName> Names = Collection->GetScalarParameterNames();
		for (const FName Name : {FName(TEXT("GlobalWetness")), FName(TEXT("WetnessOutputScale")),
			FName(TEXT("RainEnhancement")),
			FName(TEXT("RainWetDarken")), FName(TEXT("RainWetRoughness")),
			FName(TEXT("RainLightResponse")), FName(TEXT("RainSourceRetain")),
			FName(TEXT("RainWetSpecular")), FName(TEXT("RainReflectionDebug"))})
		{
			TestTrue(FString::Printf(TEXT("MPC exposes %s"), *Name.ToString()), Names.Contains(Name));
		}
	}

	for (const TCHAR* Path : {
		TEXT("/Game/VtMB/Materials/M_World_Opaque.M_World_Opaque"),
		TEXT("/Game/VtMB/Materials/M_World_Masked.M_World_Masked"),
		TEXT("/Game/VtMB/Materials/M_World_Translucent.M_World_Translucent")})
	{
		UMaterial* Master = LoadObject<UMaterial>(nullptr, Path);
		if (!TestNotNull(FString::Printf(TEXT("wet world master loads: %s"), Path), Master))
		{
			continue;
		}
		TArray<FMaterialParameterInfo> Scalars;
		TArray<FGuid> ScalarIds;
		Master->GetAllScalarParameterInfo(Scalars, ScalarIds);
		for (const FName Name : {FName(TEXT("WetnessDriven")), FName(TEXT("WetnessScale")),
			FName(TEXT("RainBoundsMinX")), FName(TEXT("RainBoundsMinY")),
			FName(TEXT("RainBoundsSizeX")), FName(TEXT("RainBoundsSizeY")),
			FName(TEXT("RainHeightMinZ")), FName(TEXT("RainHeightZScale"))})
		{
			TestTrue(FString::Printf(TEXT("%s exposes %s"), Path, *Name.ToString()),
				Scalars.ContainsByPredicate([Name](const FMaterialParameterInfo& Info)
				{
					return Info.Name == Name;
				}));
		}
		TArray<FMaterialParameterInfo> Textures;
		TArray<FGuid> TextureIds;
		Master->GetAllTextureParameterInfo(Textures, TextureIds);
		TestTrue(FString::Printf(TEXT("%s exposes RainHeightTexture"), Path),
			Textures.ContainsByPredicate([](const FMaterialParameterInfo& Info)
			{
				return Info.Name == FName(TEXT("RainHeightTexture"));
			}));
		TestTrue(FString::Printf(TEXT("%s exposes SourceCube"), Path),
			Textures.ContainsByPredicate([](const FMaterialParameterInfo& Info)
			{
				return Info.Name == ElysiumReflections::Params::SourceCube;
			}));
	}

	UNiagaraSystem* RainSystem = LoadObject<UNiagaraSystem>(nullptr,
		TEXT("/Game/VtMB/Particles/NS_ElysiumRain.NS_ElysiumRain"));
	UMaterialInterface* RainMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/VtMB/Particles/M_ElysiumRain.M_ElysiumRain"));
	TestNotNull(TEXT("the single generated Niagara rain system loads"), RainSystem);
	TestNotNull(TEXT("the single generated rain material loads"), RainMaterial);
	for (const TCHAR* Path : {
		TEXT("/Game/VtMB/Particles/T_RainDroplet.T_RainDroplet"),
		TEXT("/Game/VtMB/Particles/T_RainImpact.T_RainImpact"),
		TEXT("/Game/VtMB/Particles/T_RainStain.T_RainStain"),
		TEXT("/Game/VtMB/Particles/T_RainMist.T_RainMist")})
	{
		TestNotNull(FString::Printf(TEXT("rain dependency sprite loads: %s"), Path),
			LoadObject<UTexture2D>(nullptr, Path));
	}

	UTexture2D* Height = LoadObject<UTexture2D>(nullptr,
		TEXT("/ElysiumBaked/sm_hub_1/Weather/T_RainHeight.T_RainHeight"));
	if (TestNotNull(TEXT("per-map rain height texture loads"), Height))
	{
#if WITH_EDITORONLY_DATA
		TestEqual(TEXT("rain height source width"), Height->Source.GetSizeX(), int64(2048));
		TestEqual(TEXT("rain height source height"), Height->Source.GetSizeY(), int64(2048));
		TestEqual(TEXT("rain height source format"), Height->Source.GetFormat(), TSF_G16);
#endif
		TestFalse(TEXT("rain height is linear"), Height->SRGB);
		TestEqual(TEXT("rain height uses displacement compression"),
			Height->CompressionSettings, TC_Displacementmap);
	}

	UTextureCube* SourceCube = LoadObject<UTextureCube>(nullptr,
		TEXT("/ElysiumBaked/sm_hub_1/Textures/Cubes/TC_cubemapdefault.TC_cubemapdefault"));
	if (TestNotNull(TEXT("patch source cubemap loads"), SourceCube))
	{
#if WITH_EDITORONLY_DATA
		TestEqual(TEXT("source cubemap width"), SourceCube->Source.GetSizeX(), int64(32));
		TestEqual(TEXT("source cubemap height"), SourceCube->Source.GetSizeY(), int64(32));
#endif
		TestTrue(TEXT("source cubemap is sRGB colour"), SourceCube->SRGB);
	}
	struct FWetMaterialExpectation
	{
		const TCHAR* Name;
		float Scale;
	};
	static const FWetMaterialExpectation WetMaterials[] = {
		{TEXT("asphalt_asphaltasan_cubemapdefault"), 0.56f},
		{TEXT("concrete_curbredsan_cubemapdefault"), 1.00f},
		{TEXT("concrete_holsidewalkasan_cubemapdefault"), 1.00f},
		{TEXT("concrete_ohcurbasan_cubemapdefault"), 1.00f},
		{TEXT("concrete_ohsidewalkasan_cubemapdefault"), 1.00f},
		{TEXT("grass_grassasan_cubemapdefault"), 1.00f},
		{TEXT("ground_stnstreetasan_cubemapdefault"), 1.00f},
		{TEXT("ground_streetasan_cubemapdefault"), 0.60f},
		{TEXT("ground_streetbsan_cubemapdefault"), 0.60f},
		{TEXT("ground_streetbsantrans_cubemapdefault"), 0.60f},
		{TEXT("ground_streetcsan_cubemapdefault"), 0.60f},
		{TEXT("ground_streetdsan_cubemapdefault"), 0.60f},
		{TEXT("ground_streetesan_cubemapdefault"), 0.60f},
		{TEXT("tile_tilefsan_cubemapdefault"), 1.00f},
	};
	for (const FWetMaterialExpectation& Expected : WetMaterials)
	{
		const FString Path = FString::Printf(
			TEXT("/ElysiumBaked/sm_hub_1/Materials/MI_%s.MI_%s"),
			Expected.Name, Expected.Name);
		UMaterialInstance* Instance = LoadObject<UMaterialInstance>(nullptr, *Path);
		if (!TestNotNull(*FString::Printf(TEXT("wet MIC loads: %s"), *Path), Instance))
		{
			continue;
		}
		float Scale = -1.0f;
		TestTrue(TEXT("wet MIC carries authored scale"),
			Instance->GetScalarParameterValue(TEXT("WetnessScale"), Scale));
		TestEqual(TEXT("wet MIC preserves patch scale"), Scale, Expected.Scale, 1e-4f);
		UTexture* BoundCube = nullptr;
		TestTrue(TEXT("wet MIC binds SourceCube"),
			Instance->GetTextureParameterValue(TEXT("SourceCube"), BoundCube));
		TestTrue(TEXT("all wet MICs bind the same cube"), BoundCube == SourceCube);
		bool bUsesSourceCube = false;
		FGuid SwitchGuid;
		TestTrue(TEXT("wet MIC overrides WetnessUsesSourceCube"),
			Instance->GetStaticSwitchParameterValue(
				FHashedMaterialParameterInfo(TEXT("WetnessUsesSourceCube")),
				bUsesSourceCube, SwitchGuid, true));
		TestTrue(TEXT("wet MIC selects the source-cube permutation"), bUsesSourceCube);
		TArray<FMaterialResource*> Sm6Resources;
		FMaterialResource* Sm6 = FindOrCreateMaterialResource(
			Sm6Resources, Instance->GetMaterial(), Instance,
			SP_PCD3D_SM6, EMaterialQualityLevel::High);
		if (TestNotNull(TEXT("wet MIC creates its PCD3D_SM6 static permutation"), Sm6))
		{
			TestTrue(TEXT("wet MIC compiles its PCD3D_SM6 static permutation"),
				Sm6->CacheShaders(EMaterialShaderPrecompileMode::None));
			for (const FString& Error : Sm6->GetCompileErrors())
			{
				AddError(FString::Printf(TEXT("%s PCD3D_SM6: %s"), *Path, *Error));
			}
			TestNotNull(TEXT("wet MIC has an SM6 shader map"), Sm6->GetGameThreadShaderMap());
		}
		FMaterial::DeferredDeleteArray(Sm6Resources);
	}
	UMaterialInstance* RainMic = LoadObject<UMaterialInstance>(nullptr,
		TEXT("/ElysiumBaked/sm_hub_1/Weather/MI_ElysiumRain.MI_ElysiumRain"));
	if (TestNotNull(TEXT("per-map rain material instance loads"), RainMic))
	{
		TestTrue(TEXT("per-map rain material uses the one master"), RainMic->Parent == RainMaterial);
		UTexture* BoundHeight = nullptr;
		TestTrue(TEXT("per-map rain material binds RainHeightTexture"),
			RainMic->GetTextureParameterValue(
				FHashedMaterialParameterInfo(TEXT("RainHeightTexture")), BoundHeight, true));
		TestTrue(TEXT("per-map rain material binds the selected map height"), BoundHeight == Height);
	}
	return true;
}

// =====================================================================================// 11.9 — freeze/thaw a real map's `.ents` world (`docs/architecture/save-architecture.md` §10, the content
// tier). The substrate tier proves the mechanism on three synthetic entities; this proves
// it against the shapes the shipped data actually holds — 1,000+ records, every registered
// classname, real output tables, the runtime-spawned player. Self-skips with no export.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapSnapshotTest,
	"Elysium.Content.MapSnapshot", GElysiumContentTestFlags)
bool FElysiumMapSnapshotTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("maps") })) return true;
	// Every map that has been exported, so the tier gets wider as the export set does. The maps
	// are read straight off disk; nothing here needs a bake, an RHI or a world.
	static const TCHAR* const Maps[] =
	{
		TEXT("sp_tutorial_1"), TEXT("sm_pawnshop_1"), TEXT("sm_hub_1")
	};

	int32 Checked = 0;
	for (const TCHAR* Map : Maps)
	{
		const FString Path = FElysiumContentPaths::MapEnts(Map);
		if (!IFileManager::Get().FileExists(*Path))
		{
			AddInfo(FString::Printf(TEXT("%s not exported — skipping"), Map));
			continue;
		}

		auto ParseDefs = [&Path, Map](FElysiumEntityDefs& Out) -> bool
		{
			Out = FElysiumEntityDefs();
			const bool bOk = FElysiumEntityDefs::Parse(Path, Out);
			Out.MapName = Map;
			return bOk;
		};

		FElysiumEntityDefs DefsA;
		if (!ParseDefs(DefsA))
		{
			AddError(FString::Printf(TEXT("%s: .ents did not parse"), Map));
			continue;
		}
		const int32 DefCount = DefsA.Num();

		FElysiumEntityWorld A(/*Owner*/ nullptr, /*GameState*/ nullptr);
		A.Load(MoveTemp(DefsA));
		// The player is a runtime entity appended past the def array — the case the snapshot's
		// index alignment turns on, so it has to be in the fixture.
		A.SpawnPlayer();
		A.Activate(0.0);

		// Let the map's own openers run: logic_auto ignition, first thinks, the queue draining at
		// t=0. That is what makes the frozen state a *played* state rather than a spawned one.
		for (int32 i = 0; i < 8; ++i)
		{
			A.Tick(0.0);
		}

		FElysiumMapSnapshot Snapshot;
		A.Freeze(Snapshot);
		TestEqual(*FString::Printf(TEXT("%s: the snapshot records the def count"), Map),
			Snapshot.DefCount, DefCount);
		// The omission rule has to actually omit: a whole map's worth of untouched records would
		// mean the diff-against-a-fresh-build baseline is not doing its job.
		TestTrue(*FString::Printf(TEXT("%s: fewer records than entities (%d of %d)"),
			Map, Snapshot.Entities.Num(), A.NumEntities()),
			Snapshot.Entities.Num() < A.NumEntities());

		// Round-trip the snapshot through the real payload container, so what is applied below is
		// what came off disk, not what stayed in memory.
		FElysiumSavePayload Payload;
		Payload.World.CurrentMap = Map;
		Payload.Maps.Add(Snapshot.MapName, Snapshot);
		TArray<uint8> Bytes;
		FString Error;
		if (!TestTrue(*FString::Printf(TEXT("%s: the payload writes"), Map),
			ElysiumSave::Write(Payload, Bytes, Error)))
		{
			AddError(Error);
			continue;
		}
		FElysiumSavePayload Read;
		if (!TestTrue(*FString::Printf(TEXT("%s: and reads back"), Map),
			ElysiumSave::Read(Bytes, Read, Error)))
		{
			AddError(Error);
			continue;
		}
		const FElysiumMapSnapshot* Thawed = Read.Maps.Find(Snapshot.MapName);
		if (!TestNotNull(*FString::Printf(TEXT("%s: the map came back"), Map), Thawed))
		{
			continue;
		}

		// Rebuild and apply. Entity counts, the name index and the queue all have to survive.
		FElysiumEntityDefs DefsB;
		ParseDefs(DefsB);
		FElysiumEntityWorld B(/*Owner*/ nullptr, /*GameState*/ nullptr);
		B.Load(MoveTemp(DefsB));
		B.SpawnPlayer();
		B.ApplySnapshot(*Thawed);
		B.Activate(0.0);

		TestEqual(*FString::Printf(TEXT("%s: the same entity count"), Map),
			B.NumEntities(), A.NumEntities());
		TestEqual(*FString::Printf(TEXT("%s: the same pending queue"), Map),
			B.Queue().Num(), A.Queue().Num());
		TestNotNull(*FString::Printf(TEXT("%s: `!player` still resolves"), Map),
			(const void*)B.FindPlayer());

		// The name index: every targetname the frozen world answered to, the restored one answers
		// to as well. This is what `Entity.SetName` and the runtime-spawn append would break.
		int32 NameMisses = 0;
		for (const TUniquePtr<FElysiumEntity>& Ent : A.Entities())
		{
			if (Ent && !Ent->TargetName.IsEmpty() && !Ent->IsDead()
				&& B.FindByName(Ent->TargetName) == nullptr)
			{
				++NameMisses;
			}
		}
		TestEqual(*FString::Printf(TEXT("%s: no targetname lost across the round trip"), Map),
			NameMisses, 0);

		// And the digest: freeze -> write -> read -> apply -> freeze is the same bytes (§8/§10).
		FElysiumMapSnapshot Second;
		B.Freeze(Second);
		TArray<uint8> DigestA;
		TArray<uint8> DigestB;
		{
			FMemoryWriter W(DigestA, /*bIsPersistent*/ true);
			FElysiumSaveArchive Ar(W, FElysiumSaveVersion::Latest);
			Ar << Snapshot;
		}
		{
			FMemoryWriter W(DigestB, /*bIsPersistent*/ true);
			FElysiumSaveArchive Ar(W, FElysiumSaveVersion::Latest);
			Ar << Second;
		}
		if (DigestB != DigestA)
		{
			// The failure has to name the field, not just the mismatch — §10's whole point is that a
			// persistence bug is a text diff. Same set diff `elysium.save.diff` runs.
			AddError(FString::Printf(TEXT("%s: the round trip is NOT byte-identical"), Map));
			FElysiumSavePayload Before;
			FElysiumSavePayload After;
			Before.Maps.Add(Snapshot.MapName, Snapshot);
			After.Maps.Add(Second.MapName, Second);
			TArray<FString> LinesA;
			TArray<FString> LinesB;
			ElysiumSave::Describe(Before, LinesA);
			ElysiumSave::Describe(After, LinesB);
			const TSet<FString> SetA(LinesA);
			const TSet<FString> SetB(LinesB);
			int32 Shown = 0;
			for (const FString& Line : LinesA)
			{
				if (!SetB.Contains(Line) && Shown++ < 8) { AddError(TEXT("  - ") + Line); }
			}
			Shown = 0;
			for (const FString& Line : LinesB)
			{
				if (!SetA.Contains(Line) && Shown++ < 8) { AddError(TEXT("  + ") + Line); }
			}
		}

		AddInfo(FString::Printf(
			TEXT("%s: %d entities, %d records (%d bytes compressed), %d queued"),
			Map, A.NumEntities(), Snapshot.Entities.Num(), Bytes.Num(), Snapshot.Queue.Num()));
		++Checked;
	}

	if (Checked == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported maps for snapshot round trip"));
	}
	return true;
}

// =====================================================================================
// The rulebook (9.4a) — every `vdata/system/` table the RPG layer reads, against the real
// exported files. Two kinds of assertion, and the second is the point of the test:
//
//   * **shape** — the row counts, so a re-export that drops or duplicates rows is a failure
//     rather than a quietly smaller table;
//   * **coherence** — the cross-table references actually resolve. A quest's `AwardXP` names an
//     `experience_table` key, a feat's `Base%d` names a stat, a clan's `ClanEffect` names a
//     trait-effect group, an NPC template's parent names another template. Each of those is a
//     string that resolves at runtime and fails silently when it does not.
//
// Counts are exact where the number is a documented invariant of the shipped data and a floor
// where a data revision is plausible.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRulebookContentTest,
	"Elysium.Content.Rulebook", GElysiumContentTestFlags)
bool FElysiumRulebookContentTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("vdata") })) return true;
	if (!IFileManager::Get().FileExists(*FElysiumContentPaths::VdataFile(TEXT("system/stats.txt"))))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported vdata (run: uv run elysium export bundle vdata)"));
		return true;
	}

	FString Error;

	// --- stats.txt ------------------------------------------------------------------------------
	FElysiumStatTable Stats;
	if (!TestTrue(TEXT("stats.txt loads"), Stats.Load(Error)))
	{
		AddError(Error);
		return true;
	}
	// The container sizes ARE the engine's trait-id space: a trait is (container, position).
	// Attributes is not the nine attributes — it is a flat list carrying every derived and
	// bookkeeping stat through Experience.
	TestEqual(TEXT("Attributes container is 35 slots"),
		Stats.Container(EElysiumTraitContainer::Attributes).Num(), 35);
	TestEqual(TEXT("Abilities container is 13 slots"),
		Stats.Container(EElysiumTraitContainer::Abilities).Num(), 13);
	// 17, not 13: the last four are the Numina/hunter powers, which ship in stats.txt proper.
	TestEqual(TEXT("Disciplines container is 17 slots"),
		Stats.Container(EElysiumTraitContainer::Disciplines).Num(), 17);
	TestEqual(TEXT("Active_Disciplines container is 17 slots"),
		Stats.Container(EElysiumTraitContainer::ActiveDisciplines).Num(), 17);
	// The block name and the engine key diverge on exactly one container.
	TestEqual(TEXT("the fourth container's engine key is Active_Disciplines"),
		Stats.Container(EElysiumTraitContainer::ActiveDisciplines).InternalName,
		FString(TEXT("Active_Disciplines")));

	// Index 0 is the ordering enum, not a trait — which is what shifts every following id by one.
	if (const FElysiumStat* Order = Stats.Container(EElysiumTraitContainer::Attributes).At(0))
	{
		TestEqual(TEXT("Attributes[0] is Attrib_Order"), Order->InternalName, FString(TEXT("Attrib_Order")));
	}
	if (const FElysiumStat* Order = Stats.Container(EElysiumTraitContainer::Abilities).At(0))
	{
		TestEqual(TEXT("Abilities[0] is Ability_Order"), Order->InternalName, FString(TEXT("Ability_Order")));
	}
	TestEqual(TEXT("Disciplines carry no order slot, so Animalism is 0"),
		Stats.Container(EElysiumTraitContainer::Disciplines).IndexOf(TEXT("Animalism")), 0);

	// The chargen priority-tier tables are nested Table blocks on the `*_Order` stats — NOT rows of
	// rules_tables.txt, which holds only the clan-keyed half. 9.4f spends the sum of the two, so
	// the nested-table load is what keeps that a lookup rather than a hardcoded 2/1/0.
	if (const FElysiumStat* AttribOrder = Stats.Container(EElysiumTraitContainer::Attributes).At(0))
	{
		const FElysiumRuleTable* Tier =
			AttribOrder->Tables.Find(TEXT("subpool_attribute_primary_secondary_tertiary"));
		if (TestNotNull(TEXT("Attrib_Order carries the attribute tier table"), Tier))
		{
			TestEqual(TEXT("  primary tier is 2"), Tier->Lookup(0), 2.f);
			TestEqual(TEXT("  secondary 1"), Tier->Lookup(1), 1.f);
			TestEqual(TEXT("  tertiary 0"), Tier->Lookup(2), 0.f);
		}
		// The non-vampire variant, used when a template's `Kindred` key is 0.
		TestNotNull(TEXT("and its _Kine variant"),
			AttribOrder->Tables.Find(TEXT("subpool_attribute_primary_secondary_tertiary_kine")));
	}
	if (const FElysiumStat* AbilityOrder = Stats.Container(EElysiumTraitContainer::Abilities).At(0))
	{
		const FElysiumRuleTable* Tier =
			AbilityOrder->Tables.Find(TEXT("subpool_ability_primary_secondary_tertiary"));
		if (TestNotNull(TEXT("Ability_Order carries the ability tier table"), Tier))
		{
			TestEqual(TEXT("  ability tiers are 3/2/1"), Tier->Lookup(0), 3.f);
			TestEqual(TEXT("  secondary 2"), Tier->Lookup(1), 2.f);
			TestEqual(TEXT("  tertiary 1"), Tier->Lookup(2), 1.f);
		}
	}

	// Health is the finding RE24 corrected: Max_Health is an authored stat with no formula, and
	// Health counts damage TAKEN against it.
	if (const FElysiumStat* MaxHealth = Stats.Find(TEXT("Max_Health")))
	{
		TestEqual(TEXT("Max_Health defaults to 100"), MaxHealth->Default, 100);
		// Priced out of reach rather than flagged: a derived stat carries a flat 10000, which no
		// screen offers and no XP total reaches. The 30000 cannot-buy sentinel is used by no
		// shipped stat at all — it is a runtime verdict, not authored data.
		const FElysiumStatCost& Raise =
			Stats.Container(EElysiumTraitContainer::Attributes).CostsFor(*MaxHealth).Raise;
		TestEqual(TEXT("Max_Health's raise price is a flat 10000"), Raise.At(0), 10000);
		TestEqual(TEXT("  and it is a flat price, not a per-rating one"),
			(int32)Raise.Kind, (int32)FElysiumStatCost::EKind::Flat);
	}
	else
	{
		AddError(TEXT("Max_Health missing from stats.txt"));
	}
	if (const FElysiumStat* Health = Stats.Find(TEXT("Health")))
	{
		TestEqual(TEXT("Health starts at 0 — it counts damage taken"), Health->Default, 0);
		TestEqual(TEXT("and its ceiling is a stat NAME, not a number"),
			Health->MaxExpr, FString(TEXT("Max_Health")));
	}

	// A repeated leaf key survived the parse — the FKvNode::Pairs half.
	{
		int32 MultiGate = 0;
		for (const FElysiumStat& S : Stats.Container(EElysiumTraitContainer::ActiveDisciplines).Stats)
		{
			MultiGate += (S.IncPredependency.Num() > 1) ? 1 : 0;
		}
		TestEqual(TEXT("17 active disciplines carry two IncPredependency gates each"), MultiGate, 17);
	}

	// --- feats.txt ------------------------------------------------------------------------------
	FElysiumFeatTable Feats;
	if (TestTrue(TEXT("feats.txt loads"), Feats.Load(Error)))
	{
		TestEqual(TEXT("23 feats"), Feats.Num(), 23);
		if (const FElysiumFeat* Soak = Feats.Find(TEXT("Soak_vs_Lethal_Falling")))
		{
			// The one feat whose base carries a divisor — the reason a base is parsed, not read.
			TestTrue(TEXT("Soak_vs_Lethal_Falling has bases"), Soak->Bases.Num() >= 2);
			TestEqual(TEXT("  its first base halves Armor_Rating"), Soak->Bases[0].Apply(8), 4);
		}
		if (const FElysiumFeat* Damage = Feats.Find(TEXT("Damage")))
		{
			TestEqual(TEXT("`Damage` is a pure-code feat with no bases"), Damage->Bases.Num(), 0);
		}
		int32 Normal = 0;
		for (const FElysiumFeat& F : Feats.Feats)
		{
			Normal += (F.PcWeighting == TEXT("Normal") && F.NpcWeighting == TEXT("Normal")) ? 1 : 0;
		}
		TestEqual(TEXT("all 23 roll on the Normal dice table"), Normal, 23);
	}

	// --- rules.txt + rules_tables.txt -----------------------------------------------------------
	FElysiumRules Rules;
	if (TestTrue(TEXT("rules.txt + rules_tables.txt load"), Rules.Load(Error)))
	{
		TestEqual(TEXT("17 rule blocks"), Rules.BlockOrder.Num(), 17);
		TestEqual(TEXT("20 shared tables"), Rules.Tables.Num(), 20);
		// One value from each file, so a silently-empty half is caught.
		TestEqual(TEXT("the frenzy damage threshold is the patch's 20"),
			Rules.Int(TEXT("VampFrenzy_Info"), TEXT("Dmg_Amount")), 20);
		// Blocks nest, and a nested one is keyed by its dotted path — without that the blood/health
		// ratio reads as absent and `BloodHeal` silently falls back to its compiled default.
		TestEqual(TEXT("a nested block's key reads through its path"),
			Rules.Int(TEXT("VampHeal_Info.VampFeedingHeal_Info"), TEXT("BloodToHealthRatio")), 10);
		// The CLAN-keyed half of the chargen pools (the tier half is nested in stats.txt, above).
		// Indexed by the clandoc000 template index — their own per-row comments name Brujah(2)
		// through Mercenary(11) — and the clan term is **0 on every shipped clan** bar disciplines,
		// which is what leaves the priority tiers carrying the whole spend.
		const FElysiumRuleTable* Physical = Rules.Table(TEXT("Subpool_Physical"));
		const FElysiumRuleTable* Disciplines = Rules.Table(TEXT("Subpool_Disciplines"));
		if (TestNotNull(TEXT("Subpool_Physical is present"), Physical) &&
			TestNotNull(TEXT("Subpool_Disciplines is present"), Disciplines))
		{
			int32 Lo = 0, Hi = 0;
			Physical->KeyRange(Lo, Hi);
			TestEqual(TEXT("the pools are keyed over the clan index space, 0..11"), Hi, 11);
			TestEqual(TEXT("Brujah's attribute subpool is the dead 0"), Physical->Lookup(2), 0.f);
			TestEqual(TEXT("but its discipline subpool is 1"), Disciplines->Lookup(2), 1.f);
			TestEqual(TEXT("and the two non-clan rows are 0"), Disciplines->Lookup(0), 0.f);
		}
	}

	// --- traiteffect.txt + traiteffects000.txt ---------------------------------------------------
	FElysiumTraitEffects Effects;
	if (TestTrue(TEXT("traiteffects load"), Effects.Load(Error)))
	{
		TestEqual(TEXT("11 operators"), Effects.Operators.Names.Num(), (int32)EElysiumTraitOp::Count);
		// The compiled enum mirrors a vocabulary that ships as data — assert they still agree,
		// because a shifted index silently re-reads every modifier in the file as another operator.
		for (int32 i = 0; i < Effects.Operators.Names.Num(); ++i)
		{
			TestEqual(FString::Printf(TEXT("operator %d is %s"), i,
				ElysiumTraitOpName((EElysiumTraitOp)i)),
				Effects.Operators.Names[i], FString(ElysiumTraitOpName((EElysiumTraitOp)i)));
		}
		TestEqual(TEXT("5 effect categories"), Effects.Categories.Num(), 5);
		TestEqual(TEXT("169 effect groups"), Effects.Num(), 169);
		TestEqual(TEXT("457 effects"), Effects.NumEffects(), 457);

		// A group legitimately names one trait twice — which is why a group holds an array.
		if (const FElysiumTraitEffectGroup* Brujah = Effects.Find(TEXT("Clan (Brujah)")))
		{
			int32 Animalism = 0;
			for (const FElysiumTraitEffect& Fx : Brujah->Effects)
			{
				Animalism += Fx.Trait.Equals(TEXT("Animalism"), ESearchCase::IgnoreCase) ? 1 : 0;
			}
			TestTrue(TEXT("Clan (Brujah) names Animalism more than once"), Animalism > 1);
		}
		else
		{
			AddError(TEXT("no `Clan (Brujah)` trait-effect group"));
		}
	}

	// --- clandoc000.txt + npctemplate*.txt --------------------------------------------------------
	FElysiumClanTable Clans;
	if (TestTrue(TEXT("clan + npc templates load"), Clans.Load(Error)))
	{
		TestEqual(TEXT("25 clan templates"), Clans.Clans.Num(), 25);
		TestEqual(TEXT("36 npctemplate files"), Clans.NpcFiles.Num(), 36);
		TestEqual(TEXT("150 npc templates"), Clans.NpcTemplates.Num(), 150);

		// The engine's clan index is the file's own order, and `pc.clan` reports it. The seven
		// playable clans sit at 2..8; re-ordering the file would re-point every save.
		int32 Playable = 0;
		for (const FElysiumClanTemplate& C : Clans.Clans) { Playable += C.IsPlayable() ? 1 : 0; }
		TestEqual(TEXT("seven Player_* clans"), Playable, 7);
		if (const FElysiumClanTemplate* Brujah = Clans.Clan(2))
		{
			TestEqual(TEXT("clan index 2 is Player_Brujah"), Brujah->TemplateName,
				FString(TEXT("Player_Brujah")));
		}
		if (const FElysiumClanTemplate* Ventrue = Clans.Clan(8))
		{
			TestEqual(TEXT("clan index 8 is Player_Ventrue"), Ventrue->TemplateName,
				FString(TEXT("Player_Ventrue")));
		}

		// The parent chain resolves across files, and a partial override inherits the rest. This
		// is the whole reason an absent trait key means "inherit" rather than zero.
		int32 Parented = 0, Broken = 0, Inherited = 0;
		for (const FElysiumClanTemplate& C : Clans.NpcTemplates)
		{
			if (C.ParentTemplateName.IsEmpty())
			{
				continue;
			}
			++Parented;
			if (Clans.Find(C.ParentTemplateName) == nullptr)
			{
				++Broken;
				AddWarning(FString::Printf(TEXT("%s: parent '%s' resolves to nothing (%s)"),
					*C.TemplateName, *C.ParentTemplateName, *C.SourceFile));
				continue;
			}
			FElysiumClanTemplate Resolved;
			if (Clans.Resolve(C.TemplateName, Resolved) &&
				Resolved.Attributes.Num() > C.Attributes.Num())
			{
				++Inherited;
			}
		}
		AddInfo(FString::Printf(TEXT("npc templates: %d parented, %d inheriting attributes"),
			Parented, Inherited));
		TestEqual(TEXT("every ParentTemplateName resolves"), Broken, 0);
		TestTrue(TEXT("at least one template inherits its parent's attributes"), Inherited > 0);
	}

	// --- histories000.txt --------------------------------------------------------------------------
	FElysiumHistoryTable Histories;
	if (TestTrue(TEXT("histories000.txt loads"), Histories.Load(Error)))
	{
		TestEqual(TEXT("93 histories"), Histories.Num(), 93);
	}

	// --- experience_table.txt ----------------------------------------------------------------------
	FElysiumExperienceTable Experience;
	if (TestTrue(TEXT("experience_table.txt loads"), Experience.Load(Error)))
	{
		TestEqual(TEXT("190 experience rows"), Experience.Num(), 190);
		// The engine's lookup is a prefix compare over the STORED key's length. An exact match
		// reproduces it only while no key prefixes another — assert the premise, not the outcome.
		TArray<TPair<FString, FString>> Collisions;
		Experience.FindPrefixCollisions(Collisions);
		for (const TPair<FString, FString>& C : Collisions)
		{
			AddWarning(FString::Printf(TEXT("'%s' prefixes '%s' — the engine's Q_strnicmp lookup ")
				TEXT("would swallow it, so an exact match no longer reproduces it"), *C.Key, *C.Value));
		}
		TestEqual(TEXT("no experience key prefixes another"), Collisions.Num(), 0);
	}

	// --- the five quests_*.txt ---------------------------------------------------------------------
	FElysiumQuestTables Quests;
	if (TestTrue(TEXT("quests_*.txt load"), Quests.Load(Error)))
	{
		TestTrue(TEXT("at least 70 quests"), Quests.NumQuests() >= 70);
		TestTrue(TEXT("at least 400 completion states"), Quests.NumStates() >= 400);
		// quests_main.txt is comment-only and must load clean rather than as a failure.
		TestEqual(TEXT("quests_main.txt is empty and clean"), Quests.Quests[3].Num(), 0);

		int32 Awards = 0, Unresolved = 0, Money = 0, Events = 0;
		int32 IdMismatches = 0, OverCap = 0;
		TSet<FString> Types;
		for (int32 t = 0; t < FElysiumQuestTables::NumTables; ++t)
		{
			for (const FElysiumQuest& Q : Quests.Quests[t])
			{
				// VtMB's loader stops at 20 states per quest — a 21st would silently not exist.
				if (Q.States.Num() > FElysiumQuest::MaxStates) { ++OverCap; }
				for (int32 i = 0; i < Q.States.Num(); ++i)
				{
					const FElysiumQuestState& S = Q.States[i];
					// `SetQuest(title, N)` addresses the N-th state in FILE ORDER; the authored
					// `"ID"` is never read (`docs/vtmb/game_runtime.md` -> "Quests"). Every shipped row
					// happens to author the two equal, which is what makes `elysium.rules quest`'s
					// by-ID listing readable — assert it so a patched rulebook that breaks the
					// coincidence surfaces here rather than as a mis-addressed award.
					if (S.Id != i + 1) { ++IdMismatches; }
					Types.Add(S.Type.ToLower());
					Money += (S.AwardMoney != 0) ? 1 : 0;
					Events += S.Event.IsEmpty() ? 0 : 1;
					if (S.AwardXp.IsEmpty())
					{
						continue;
					}
					++Awards;
					// THE named acceptance: an AwardXP value is a key into experience_table, not a
					// number, and one that stops resolving is a silently unpaid quest reward.
					if (Experience.Find(S.AwardXp) == nullptr)
					{
						++Unresolved;
						AddError(FString::Printf(TEXT("quest \"%s\" state %d awards '%s', ")
							TEXT("which is not an experience_table key"), *Q.Title, S.Id, *S.AwardXp));
					}
				}
			}
		}
		AddInfo(FString::Printf(TEXT("quests: %d, states: %d, AwardXP: %d, AwardMoney: %d, Event: %d"),
			Quests.NumQuests(), Quests.NumStates(), Awards, Money, Events));
		TestTrue(TEXT("at least 150 AwardXP references"), Awards >= 150);
		TestEqual(TEXT("every AwardXP key resolves in experience_table"), Unresolved, 0);
		// The schema documents both, and the shipped data authors neither — recorded so 9.4d knows
		// its money/event award path has no shipped case to test against.
		TestEqual(TEXT("AwardMoney is authored nowhere"), Money, 0);
		TestEqual(TEXT("Event is authored nowhere"), Events, 0);
		// Three, not four: `botch` is a real Type in the engine's parser and no row authors it, so
		// the refuse-to-leave-a-botched-quest branch is unreachable on retail data.
		TestEqual(TEXT("Type is exactly three values"), Types.Num(), 3);
		TestEqual(TEXT("every authored ID equals its 1-based file position"), IdMismatches, 0);
		TestEqual(TEXT("no quest exceeds VtMB's 20-state cap"), OverCap, 0);

		if (const FElysiumQuest* Knox = Quests.Find(TEXT("Arthur Knox")))
		{
			if (const FElysiumQuestState* State2 = Knox->StateById(2))
			{
				TestEqual(TEXT("Arthur Knox state 2 awards Carson01"), State2->AwardXp,
					FString(TEXT("Carson01")));
			}
		}
		else
		{
			AddError(TEXT("no quest titled 'Arthur Knox'"));
		}
	}

	// --- levelingtemplate_000.txt -------------------------------------------------------------------
	FElysiumLevelingTemplates Leveling;
	if (TestTrue(TEXT("levelingtemplate_000.txt loads"), Leveling.Load(Error)))
	{
		TestEqual(TEXT("16 leveling templates"), Leveling.Num(), 16);
		TestTrue(TEXT("at least 1200 buy steps"), Leveling.NumSteps() >= 1200);
		int32 CharGen = 0;
		for (const FElysiumLevelingTemplate& T : Leveling.Templates) { CharGen += T.IsCharGen() ? 1 : 0; }
		// The eight chargen templates are what 9.4f runs after `giftxp 9000`.
		TestEqual(TEXT("eight *_CharGen templates"), CharGen, 8);
		if (const FElysiumLevelingTemplate* Tremere = Leveling.Find(TEXT("Tremere_CharGen")))
		{
			TestTrue(TEXT("Tremere_CharGen buys something"), Tremere->NumSteps() > 0);
		}
		else
		{
			AddError(TEXT("no Tremere_CharGen leveling template"));
		}
	}

	// --- charcreatewizard.txt --------------------------------------------------------------------
	FElysiumWizard Wizard;
	if (TestTrue(TEXT("charcreatewizard.txt loads"), Wizard.Load(Error)))
	{
		TestEqual(TEXT("8 abstract traits"), Wizard.Traits.Num(), 8);
		TestEqual(TEXT("3 trait combinations"), Wizard.Combinations.Num(), 3);
		TestEqual(TEXT("10 popup groups"), Wizard.Groups.Num(), 10);
		// 78 author a bare `Popup` line; 7 more carry a trailing `// restored by wesp` /
		// `// added by wesp`, so counting by line shape undercounts exactly the patch's restorations.
		TestEqual(TEXT("85 popups"), Wizard.NumPopups(), 85);
		TestEqual(TEXT("7 clan nodes"), Wizard.ClanNodes.Num(), 7);

		// The 3x3 payoff matrix, read out of the file rather than compiled in: the player's n-th
		// choice meeting a clan that ranks it n-th is the diagonal, and it is the maximum.
		TestEqual(TEXT("1st choice / clan Primary scores 3"), Wizard.ConnectionScores[0][0], 3);
		TestEqual(TEXT("1st choice / clan Tertiary scores 0"), Wizard.ConnectionScores[0][2], 0);
		TestEqual(TEXT("2nd choice / clan Secondary scores 3"), Wizard.ConnectionScores[1][1], 3);
		TestEqual(TEXT("3rd choice / clan Tertiary scores 3"), Wizard.ConnectionScores[2][2], 3);

		// A rank REPEATS: Gangrel authors two Primaries. A reader keeping only the last would score
		// it against one trait and silently mis-suggest the clan.
		if (const FElysiumWizClanNode* Gangrel = Wizard.ClanNode(TEXT("Player_Gangrel")))
		{
			TestEqual(TEXT("Gangrel has two Primary traits"), Gangrel->Primary.Num(), 2);
			TestEqual(TEXT("Armed is Primary for Gangrel"), Gangrel->RankOf(TEXT("Armed")), 0);
			TestEqual(TEXT("Unarmed is Primary for Gangrel too"), Gangrel->RankOf(TEXT("Unarmed")), 0);
			TestEqual(TEXT("Stealth is Secondary"), Gangrel->RankOf(TEXT("Stealth")), 1);
			TestEqual(TEXT("a trait it does not rank is INDEX_NONE"),
				Gangrel->RankOf(TEXT("Social")), (int32)INDEX_NONE);
		}
		else
		{
			AddError(TEXT("no Player_Gangrel ClanNode"));
		}

		// Popups share an InternalName on purpose — the wizard picks among them at random, so the
		// gender question being one-of-N would silently become one-of-1 if the reader deduped.
		TArray<const FElysiumWizPopup*> Combat;
		Wizard.PopupsNamed(TEXT("Trait_Combat_vs_Non-Combat"), Combat);
		TestTrue(TEXT("the Combat/Non-Combat question has several phrasings"), Combat.Num() > 1);

		// `Defaults` inheritance, the part a reader most easily drops: this popup authors no
		// Bkg_Image and no TextRegion, and must take its group's.
		TArray<const FElysiumWizPopup*> Gender;
		Wizard.PopupsNamed(TEXT("Trait_Male_vs_Female"), Gender);
		if (TestEqual(TEXT("one gender popup"), Gender.Num(), 1))
		{
			TestEqual(TEXT("it inherited the group's backdrop"),
				Gender[0]->BkgImage, FString(TEXT("Interface/Pop_Ups/Pop_Up_1")));
			TestTrue(TEXT("it inherited the group's TextRegion"), Gender[0]->TextRegion.bAuthored);
			TestEqual(TEXT("the inherited TextRegion is the authored one"),
				Gender[0]->TextRegion.X, 183);
			TestEqual(TEXT("two answers"), Gender[0]->Actions.Num(), 2);
			if (Gender[0]->Actions.Num() == 2)
			{
				TestTrue(TEXT("answer 1 sets male"), Gender[0]->Actions[0].bSetGenderMale);
				TestTrue(TEXT("answer 2 sets female"), Gender[0]->Actions[1].bSetGenderFemale);
			}
		}

		// The group hand-off that ends the quiz section, on the answered-count.
		if (const FElysiumWizGroup* G = Wizard.Group(TEXT("Trait_Popups")))
		{
			TestEqual(TEXT("Trait_Popups hands off after 6..8 answers"), G->NextSection.MinCount, 6);
			TestEqual(TEXT("and at most 8"), G->NextSection.MaxCount, 8);
			TestFalse(TEXT("to a named popup"), G->NextSection.Next.IsEmpty());
		}
		else
		{
			AddError(TEXT("no Trait_Popups group"));
		}

		// An absent bound must stay unbounded. A popup authoring only MaxVal has to admit a trait
		// the player has never picked, which is exactly the zero tally a 0-default would exclude.
		bool bSawOpenMin = false;
		for (const FElysiumWizGroup& G : Wizard.Groups)
		{
			for (const FElysiumWizPopup& P : G.Popups)
			{
				for (const FElysiumWizPrereq& Q : P.Prereqs)
				{
					if (Q.MinVal == MIN_int32) { bSawOpenMin = true; TestTrue(
						TEXT("an unauthored MinVal admits a zero tally"), Q.Admits(0)); }
				}
			}
		}
		TestTrue(TEXT("some popup authors MaxVal alone"), bSawOpenMin);

		// Every wire resolves, or the quiz dead-ends at runtime instead of here.
		int32 BadNext = 0, BadTrait = 0, BadTemplate = 0;
		TSet<FString> TraitNames;
		for (const FString& T : Wizard.Traits) { TraitNames.Add(ElysiumFold(T)); }
		auto CheckNext = [&](const FString& Owner, const FString& Next)
		{
			if (Next.IsEmpty()) { return; }
			TArray<const FElysiumWizPopup*> Hits;
			Wizard.PopupsNamed(Next, Hits);
			if (Hits.IsEmpty())
			{
				++BadNext;
				AddError(FString::Printf(TEXT("%s: Next '%s' names no popup"), *Owner, *Next));
			}
		};
		for (const FElysiumWizGroup& G : Wizard.Groups)
		{
			CheckNext(G.InternalName, G.NextSection.Next);
			for (const FElysiumWizPopup& P : G.Popups)
			{
				for (const FElysiumWizAction& A : P.Actions)
				{
					CheckNext(P.InternalName, A.Next);
					if (!A.Trait.IsEmpty() && !TraitNames.Contains(ElysiumFold(A.Trait)))
					{
						++BadTrait;
						AddError(FString::Printf(TEXT("popup %s: answer increments unknown trait '%s'"),
							*P.InternalName, *A.Trait));
					}
					if (!A.CharTemplate.IsEmpty() && Clans.Find(A.CharTemplate) == nullptr)
					{
						++BadTemplate;
						AddError(FString::Printf(TEXT("popup %s: answer names unknown template '%s'"),
							*P.InternalName, *A.CharTemplate));
					}
				}
			}
		}
		TestEqual(TEXT("every Next resolves to a popup"), BadNext, 0);
		TestEqual(TEXT("every answer's Trait is one of the 8"), BadTrait, 0);
		TestEqual(TEXT("every answer's CharTemplate resolves in clandoc"), BadTemplate, 0);

		// The scoring table and the clan table have to agree on names, or a clan scores 0 forever.
		int32 BadClan = 0;
		for (const FElysiumWizClanNode& C : Wizard.ClanNodes)
		{
			if (Clans.Find(C.CharTemplate) == nullptr)
			{
				++BadClan;
				AddError(FString::Printf(TEXT("ClanNode '%s' resolves to no clan template"),
					*C.CharTemplate));
			}
		}
		TestEqual(TEXT("every ClanNode names a real clan"), BadClan, 0);
	}

	// --- cross-table coherence -----------------------------------------------------------------------
	// Every trait name a feat sums, and every effect group a clan or history names, has to resolve
	// or the system that reads it fails silently at runtime rather than here.
	{
		int32 BadBase = 0;
		for (const FElysiumFeat& F : Feats.Feats)
		{
			for (const FElysiumTraitRef& Base : F.Bases)
			{
				if (Stats.Find(Base.Trait) == nullptr && Feats.Find(Base.Trait) == nullptr)
				{
					++BadBase;
					AddError(FString::Printf(TEXT("feat %s: base '%s' names no stat or feat"),
						*F.InternalName, *Base.Trait));
				}
			}
		}
		TestEqual(TEXT("every feat Base%d resolves to a stat or feat"), BadBase, 0);

		int32 BadEffect = 0;
		auto CheckGroup = [&](const FString& Owner, const FString& Group)
		{
			if (Group.IsEmpty() || Effects.Find(Group) != nullptr)
			{
				return;
			}
			++BadEffect;
			AddError(FString::Printf(TEXT("%s names trait-effect group '%s', which does not exist"),
				*Owner, *Group));
		};
		for (const FElysiumClanTemplate& C : Clans.Clans)
		{
			CheckGroup(C.TemplateName, C.GeneralStr(TEXT("ClanEffect")));
			CheckGroup(C.TemplateName, C.GeneralStr(TEXT("FrenzyEffect")));
		}
		for (const FElysiumHistory& H : Histories.Rows)
		{
			CheckGroup(H.InternalName, H.Effect);
		}
		TestEqual(TEXT("every ClanEffect/FrenzyEffect/history Effect resolves"), BadEffect, 0);
	}

	return true;
}

// =================================================================================================
// 9.6 — `dicerolls.txt` against the real file, and the resolver over what it loaded.
//
// The claim under test is that the shipped weighting tables are a plain uniform d10, which is what
// makes the resolver's fail-open fallback faithful rather than an approximation. A patched install
// that reweights a die is not a failure — it is the mechanism working — so a non-uniform table is
// reported rather than failed, and the structural assertions still hold.
//
// The algorithm itself is pinned content-free in `Elysium.Substrate.Dice`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDiceContentTest,
	"Elysium.Content.Dice", GElysiumContentTestFlags)
bool FElysiumDiceContentTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("vdata") })) return true;
	if (!IFileManager::Get().FileExists(*FElysiumContentPaths::VdataFile(TEXT("system/dicerolls.txt"))))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported vdata (run: uv run elysium export bundle vdata)"));
		return true;
	}

	FString Error;
	FElysiumDiceTables Dice;
	if (!TestTrue(TEXT("dicerolls.txt loads"), Dice.Load(Error)))
	{
		AddError(Error);
		return true;
	}

	// --- TableWeightings --------------------------------------------------------------------------
	TestEqual(TEXT("three weighting tables"), Dice.Num(), 3);
	for (const TCHAR* Named : { TEXT("Normal"), TEXT("Heavy"), TEXT("Light") })
	{
		TestEqual(FString::Printf(TEXT("`%s` is authored"), Named),
			Dice.Find(Named).InternalName.ToLower(), FString(Named).ToLower());
	}
	// Index 0 is the engine's fallback for an unmatched name, and it is the table every feat asks
	// for — so a re-ordered file would silently re-point every miss.
	TestEqual(TEXT("index 0 is Normal"), Dice.At(0).InternalName.ToLower(), FString(TEXT("normal")));

	int32 Weighted = 0;
	for (const FElysiumDiceTable& Table : Dice.Tables)
	{
		// Structural, whatever the distribution: 100 entries covering the raw draw's whole domain.
		int32 OutOfRange = 0;
		for (int32 i = 0; i < FElysiumDiceTable::NumEntries; ++i)
		{
			OutOfRange += (Table.Face(i) < 0 || Table.Face(i) >= FElysiumDiceTable::NumFaces) ? 1 : 0;
		}
		TestEqual(FString::Printf(TEXT("`%s` maps every draw to a d10 face"), *Table.InternalName),
			OutOfRange, 0);

		if (!Table.IsUniform())
		{
			++Weighted;
			// Report the shape rather than failing: a data mod is entitled to bias a die.
			FString Histogram;
			int32 Counts[FElysiumDiceTable::NumFaces] = {};
			for (int32 i = 0; i < FElysiumDiceTable::NumEntries; ++i)
			{
				const int32 Face = Table.Face(i);
				if (Face >= 0 && Face < FElysiumDiceTable::NumFaces) { ++Counts[Face]; }
			}
			for (int32 f = 0; f < FElysiumDiceTable::NumFaces; ++f)
			{
				Histogram += FString::Printf(TEXT("%s%d:%d"), f ? TEXT(" ") : TEXT(""),
					f + 1, Counts[f]);
			}
			AddInfo(FString::Printf(
				TEXT("weighting table `%s` is NOT a uniform d10 — face counts out of 100: %s"),
				*Table.InternalName, *Histogram));
		}
	}
	// The shipped claim. Stated as an equality so a patched install reports the delta in one line.
	TestEqual(TEXT("every shipped weighting table is a uniform d10"), Weighted, 0);

	// --- HealthModifiers --------------------------------------------------------------------------
	TestEqual(TEXT("eight health levels"), Dice.HealthModifiers.Num(), 8);
	int32 NonZeroPenalty = 0;
	for (const int32 Penalty : Dice.HealthModifiers) { NonZeroPenalty += Penalty != 0 ? 1 : 0; }
	if (NonZeroPenalty > 0)
	{
		AddInfo(FString::Printf(TEXT("%d health level(s) author a non-zero pool penalty"),
			NonZeroPenalty));
	}
	TestEqual(TEXT("the shipped wound penalty is a no-op"), NonZeroPenalty, 0);

	// --- the feat -> weighting join ---------------------------------------------------------------
	FElysiumFeatTable Feats;
	if (TestTrue(TEXT("feats.txt loads"), Feats.Load(Error)))
	{
		// Every feat names a table that exists, so no feat is silently rolling on the fallback.
		int32 Unresolved = 0;
		for (const FElysiumFeat& F : Feats.Feats)
		{
			for (const FString& Name : { F.PcWeighting, F.NpcWeighting })
			{
				if (Dice.Find(Name).InternalName.ToLower() != Name.ToLower())
				{
					++Unresolved;
					AddError(FString::Printf(TEXT("feat %s names weighting '%s', which does not exist"),
						*F.InternalName, *Name));
				}
			}
		}
		TestEqual(TEXT("every feat's PC/NPC weighting resolves to a real table"), Unresolved, 0);

		if (const FElysiumFeat* Intrusion = Feats.Find(TEXT("Intrusion")))
		{
			TestEqual(TEXT("Intrusion rolls on Normal"),
				Dice.ForFeat(*Intrusion, /*bNpc*/ false).InternalName.ToLower(),
				FString(TEXT("normal")));
		}
	}

	// --- the resolver over the real table ---------------------------------------------------------
	// A seeded smoke over loaded data: the counters have to be internally consistent and the tier
	// has to be the one the counters imply, whatever the draws were.
	ElysiumRng::SeedAll(9600);
	const FElysiumDiceTable& Normal = Dice.Find(TEXT("Normal"));
	for (int32 Trial = 0; Trial < 32; ++Trial)
	{
		const FElysiumRollResult Result = ElysiumDice::Roll(6, 6, Normal);
		TestEqual(TEXT("net is successes minus botches"), Result.Net,
			Result.Successes - Result.Botches);
		TestEqual(TEXT("the tier is the one the counters imply"), (int32)Result.Tier,
			(int32)ElysiumDice::TierFor(Result.Successes, Result.Botches));
		TestTrue(TEXT("no more dice were resolved than the roll cap allows"),
			Result.Successes + Result.Botches <= ElysiumDice::MaxRolls);
		TestTrue(TEXT("every ten counted as a success"), Result.Tens <= Result.Successes);
	}

	return true;
}

// =================================================================================================
// The character sheet against the real rulebook — the audit that keeps the compiled slot table
// honest.
//
// `ElysiumSheetSlots.h` freezes the layout in C++ because VtMB freezes it in `vampire.dll`; the
// values come from `stats.txt`. That split only holds while the two agree, so this walks every
// compiled slot and asserts the file names the same trait at the same index.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSheetContentTest,
	"Elysium.Content.Sheet", GElysiumContentTestFlags)
bool FElysiumSheetContentTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("vdata") })) return true;
	if (!IFileManager::Get().FileExists(*FElysiumContentPaths::VdataFile(TEXT("system/stats.txt"))))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported vdata (run: uv run elysium export bundle vdata)"));
		return true;
	}

	using EC = EElysiumTraitContainer;
	FString Error;

	FElysiumStatTable Stats;
	if (!TestTrue(TEXT("stats.txt loads"), Stats.Load(Error)))
	{
		AddError(Error);
		return true;
	}

	// --- Slot for slot ------------------------------------------------------------------------
	int32 Mismatched = 0;
	for (uint8 i = 0; i < (uint8)EC::Count; ++i)
	{
		const EC Container = (EC)i;
		const FElysiumStatContainer& File = Stats.Container(Container);
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(Container))
		{
			const FElysiumStat* Stat = File.At(Slot.Index);
			if (!Stat)
			{
				++Mismatched;
				AddError(FString::Printf(TEXT("%s slot %d (%s) has no row in stats.txt"),
					ElysiumTraitContainerName(Container), Slot.Index, Slot.Internal));
			}
			else if (!Stat->InternalName.Equals(Slot.Internal, ESearchCase::IgnoreCase))
			{
				++Mismatched;
				AddError(FString::Printf(TEXT("%s slot %d: the table says '%s', stats.txt says '%s'"),
					ElysiumTraitContainerName(Container), Slot.Index, Slot.Internal, *Stat->InternalName));
			}
		}
	}
	TestEqual(TEXT("every compiled slot names the trait stats.txt puts at that index"), Mismatched, 0);

	// The file's Disciplines container is WIDER than the compiled array: 17 rows, the last four
	// being the Numina powers. If that ever stops being true the 13 above needs re-deriving.
	TestEqual(TEXT("stats.txt authors 17 disciplines to the compiled 13"),
		Stats.Container(EC::Disciplines).Num(), 17);
	if (const FElysiumStat* Numina = Stats.Container(EC::Disciplines).At(13))
	{
		TestEqual(TEXT("the first file-only slot is Shield_of_Faith"),
			Numina->InternalName, FString(TEXT("Shield_of_Faith")));
	}

	// --- The seed -----------------------------------------------------------------------------
	FElysiumSheet Sheet;
	Sheet.SeedFrom(Stats);

	// `Max_Health` is an ordinary stat with `Default 100` and no formula anywhere in `vdata` —
	// nothing derives health from Stamina (RE24). This read is what retired the interim constant.
	TestEqual(TEXT("Max_Health seeds to its authored 100"),
		Sheet.GetCurrent(EC::Attributes, ElysiumSlot::MaxHealth), 100);
	TestEqual(TEXT("Health starts at zero damage taken"),
		Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Health), 0);
	TestEqual(TEXT("Humanity seeds to 7"), Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Humanity), 7);
	TestEqual(TEXT("BloodPool seeds to 10"), Sheet.GetCurrent(EC::Attributes, ElysiumSlot::BloodPool), 10);
	TestEqual(TEXT("Masquerade starts clean"), Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Masquerade), 0);
	TestEqual(TEXT("an attribute seeds to 1"), Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Strength), 1);
	TestEqual(TEXT("an ability seeds to 0"), Sheet.GetCurrent(EC::Abilities, /*Brawl*/ 1), 0);
	// -1 is the sentinel for a discipline the clan cannot take; it is the authored Default, so a
	// freshly seeded sheet reads it on all 13 until a clan template overlays.
	TestEqual(TEXT("a discipline seeds to the -1 sentinel"),
		Sheet.GetCurrent(EC::Disciplines, /*Animalism*/ 0), -1);

	// `Health`'s authored Max is the NAME `Max_Health`, not a number — so the clamp resolves it
	// against the sheet, which is why the recompute is two passes rather than one.
	Sheet.SetBase(EC::Attributes, ElysiumSlot::Health, 999);
	Sheet.RecomputeCurrent(&Stats);
	TestEqual(TEXT("damage clamps to the Max_Health the sheet holds"),
		Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Health), 100);
	// And the numeric bounds, which is where the counter inputs get their range.
	Sheet.SetBase(EC::Attributes, ElysiumSlot::Humanity, 99);
	Sheet.SetBase(EC::Attributes, ElysiumSlot::Masquerade, 99);
	Sheet.RecomputeCurrent(&Stats);
	TestEqual(TEXT("Humanity clamps to its authored 0..10"),
		Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Humanity), 10);
	TestEqual(TEXT("Masquerade clamps to 0..5 — the game-over ceiling"),
		Sheet.GetCurrent(EC::Attributes, ElysiumSlot::Masquerade), 5);

	// --- A template overlays it ----------------------------------------------------------------
	FElysiumClanTable Clans;
	if (TestTrue(TEXT("the clan/NPC templates load"), Clans.Load(Error)))
	{
		// A playable clan: the overlay is what makes a discipline the clan CAN take read 0 or more
		// instead of the -1 sentinel.
		FElysiumClanTemplate Brujah;
		if (TestTrue(TEXT("Player_Brujah resolves"), Clans.Resolve(TEXT("Player_Brujah"), Brujah)))
		{
			FElysiumSheet Pc;
			Pc.SeedFrom(Stats);
			Pc.ApplyTemplate(Brujah, &Stats);
			TestNotEqual(TEXT("its clan disciplines are no longer the -1 sentinel"),
				Pc.GetCurrent(EC::Disciplines, /*Celerity*/ 3), -1);
		}

		// An NPC template: `Max_Health` is authored as a literal there, and that overlay IS the
		// whole of an NPC's health track (`docs/vtmb/vdata-catalog.md`).
		int32 Checked = 0;
		for (const FElysiumClanTemplate& Row : Clans.NpcTemplates)
		{
			const int32* Authored = Row.Trait(TEXT("Max_Health"));
			if (!Authored || *Authored <= 0)
			{
				continue;
			}
			FElysiumClanTemplate Resolved;
			if (!Clans.Resolve(Row.TemplateName, Resolved))
			{
				continue;
			}
			FElysiumSheet Npc;
			Npc.SeedFrom(Stats);
			Npc.ApplyTemplate(Resolved, &Stats);
			TestEqual(*FString::Printf(TEXT("%s takes its authored Max_Health"), *Row.TemplateName),
				Npc.GetCurrent(EC::Attributes, ElysiumSlot::MaxHealth), *Authored);
			if (++Checked >= 5)
			{
				break;   // five is enough to prove the path; the slot audit above covers the rest
			}
		}
		TestTrue(TEXT("at least one NPC template authors a health ceiling"), Checked > 0);
	}
	else
	{
		AddError(Error);
	}

	return true;
}

// =================================================================================================
// The sheet's arithmetic against the real rulebook (9.4c) — the halves the content-free tier
// cannot reach: the trait-effect layer resolved out of `traiteffects000.txt`, the feat evaluator
// over the shipped 23 feats, and the award values in `experience_table.txt`.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSheetMathContentTest,
	"Elysium.Content.SheetMath", GElysiumContentTestFlags)
bool FElysiumSheetMathContentTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("vdata") })) return true;
	if (!IFileManager::Get().FileExists(*FElysiumContentPaths::VdataFile(TEXT("system/feats.txt"))))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported vdata (run: uv run elysium export bundle vdata)"));
		return true;
	}

	using EC = EElysiumTraitContainer;
	FString Error;

	FElysiumStatTable Stats;
	FElysiumFeatTable Feats;
	FElysiumClanTable Clans;
	FElysiumTraitEffects Effects;
	FElysiumExperienceTable Experience;
	const bool bLoaded = TestTrue(TEXT("stats.txt loads"), Stats.Load(Error))
		&& TestTrue(TEXT("feats.txt loads"), Feats.Load(Error))
		&& TestTrue(TEXT("clandoc000.txt loads"), Clans.Load(Error))
		&& TestTrue(TEXT("traiteffects000.txt loads"), Effects.Load(Error))
		&& TestTrue(TEXT("experience_table.txt loads"), Experience.Load(Error));
	if (!bLoaded)
	{
		AddError(Error);
		return true;
	}

	// A real playable character: `stats.txt`'s defaults, the clan's authored ratings on top.
	auto MakePc = [&](const TCHAR* Template, FElysiumSheet& OutSheet, FElysiumSheetEffects& OutLayer)
	{
		FElysiumClanTemplate Resolved;
		if (!Clans.Resolve(Template, Resolved))
		{
			AddError(FString::Printf(TEXT("%s does not resolve"), Template));
			return;
		}
		const FString Group = Resolved.GeneralStr(TEXT("ClanEffect"));
		TArray<FString> Groups;
		if (!Group.IsEmpty()) { Groups.Add(Group); }
		OutLayer.Build(Effects, Groups, &Feats);
		OutSheet.SeedFrom(Stats);
		OutSheet.ApplyTemplate(Resolved, &Stats, &OutLayer);
	};

	// --- Every shipped feat evaluates ------------------------------------------------------------
	{
		FElysiumSheet Pc;
		FElysiumSheetEffects Layer;
		MakePc(TEXT("Player_Brujah"), Pc, Layer);

		int32 Evaluated = 0;
		for (int32 i = 0; i < Feats.Num(); ++i)
		{
			const FElysiumFeat* Feat = Feats.At(i);
			if (!Feat) { continue; }
			const int32 Value = ElysiumFeats::FeatValue(*Feat, Pc, &Layer);
			TestTrue(*FString::Printf(TEXT("%s evaluates within [0, %d]"), *Feat->InternalName, Feat->MaxValue),
				Value >= 0 && Value <= Feat->MaxValue);
			++Evaluated;
		}
		TestEqual(TEXT("all 23 shipped feats evaluate"), Evaluated, 23);

		// The two-name shape, read off the real file: `Persuasion` = Charisma + Academics, each at
		// its seeded rating (1 and 0), the attribute floored at 1.
		bool bResolved = false, bIsFeat = false;
		TestEqual(TEXT("CalcFeat(\"Persuasion\") sums the authored bases"),
			ElysiumFeats::Calc(Feats, Pc, &Layer, TEXT("Persuasion"), bResolved, bIsFeat),
			Pc.GetCurrent(EC::Attributes, /*Charisma*/ 4) + Pc.GetCurrent(EC::Abilities, /*Academics*/ 12));
		TestTrue(TEXT("...as a feat"), bResolved && bIsFeat);
		// The dlgexpr normalizer routes stat checks through the same name (272 `Humanity` checks in
		// the corpus), so the fallback has to answer — a marked divergence from VtMB, which raises.
		TestEqual(TEXT("CalcFeat(\"Humanity\") falls back to the trait"),
			ElysiumFeats::Calc(Feats, Pc, &Layer, TEXT("Humanity"), bResolved, bIsFeat),
			Pc.GetCurrent(EC::Attributes, ElysiumSlot::Humanity));
		TestTrue(TEXT("...resolved, but not as a feat"), bResolved && !bIsFeat);
		ElysiumFeats::Calc(Feats, Pc, &Layer, TEXT("D_HT"), bResolved, bIsFeat);
		TestFalse(TEXT("a name that is neither reads 0 and says so"), bResolved);
	}

	// --- The clan banes, enforced by the generic effect layer -------------------------------------
	{
		// Tremere: no Physical attribute above 4, authored as `Max 4` trait effects.
		FElysiumSheet Tremere;
		FElysiumSheetEffects Layer;
		MakePc(TEXT("Player_Tremere"), Tremere, Layer);
		TestTrue(TEXT("the Tremere clan group resolves"), Layer.ResolvedGroups().Num() > 0);

		Tremere.SetBase(EC::Attributes, ElysiumSlot::Strength, 5);
		Tremere.RecomputeCurrent(&Stats, &Layer);
		TestEqual(TEXT("Tremere physicals cap at the bane's Max 4"),
			Tremere.GetCurrent(EC::Attributes, ElysiumSlot::Strength), 4);
		TestEqual(TEXT("...on the current only — the base is what was bought"),
			Tremere.GetBase(EC::Attributes, ElysiumSlot::Strength), 5);
		TestFalse(TEXT("...and a dot cannot be raised past it"),
			Tremere.IncBase(EC::Attributes, ElysiumSlot::Strength, &Stats, &Layer));
		// The bound itself goes through the effect walk (RE26), so the bane lowers the CEILING —
		// which is what `IncBase` and the chargen buy path both read, not just the clamp.
		int32 BaneMin = 0, BaneMax = 0;
		Tremere.BoundsFor(EC::Attributes, ElysiumSlot::Strength, &Stats, &Layer, BaneMin, BaneMax);
		TestEqual(TEXT("the effective Strength ceiling is the bane's 4, not stats.txt's 10"), BaneMax, 4);
		int32 FreeMin = 0, FreeMax = 0;
		Tremere.BoundsFor(EC::Attributes, /*Charisma*/ 4, &Stats, &Layer, FreeMin, FreeMax);
		TestEqual(TEXT("...and an unaffected attribute keeps its authored ceiling"), FreeMax, 10);

		// Toreador: humanity moves are doubled — the gift and the bane are one flag.
		FElysiumSheet Toreador;
		FElysiumSheetEffects ToreadorLayer;
		MakePc(TEXT("Player_Toreador"), Toreador, ToreadorLayer);
		TestTrue(TEXT("Toreador carries Fx_Humanity_Mods_Doubled"),
			ToreadorLayer.Flag(TEXT("Fx_Humanity_Mods_Doubled")) > 0);
		TestEqual(TEXT("no other clan does — Brujah"), Layer.Flag(TEXT("Fx_Humanity_Mods_Doubled")), 0);

		// A frenzy penalty is a real stat, not code: `Frenzy_Check_Mod`.
		FElysiumSheet Gangrel;
		FElysiumSheetEffects GangrelLayer;
		MakePc(TEXT("Player_Gangrel"), Gangrel, GangrelLayer);
		TestTrue(TEXT("Gangrel's frenzy bane lands on Frenzy_Check_Mod"),
			Gangrel.GetCurrent(EC::Attributes, /*Frenzy_Check_Mod*/ 21) < 0);
	}

	// --- The award values --------------------------------------------------------------------------
	{
		// A real row, banked the way `AddExperience` banks it.
		const FElysiumExperienceEntry* Row = Experience.Rows.IsEmpty() ? nullptr : &Experience.Rows[0];
		if (TestNotNull(TEXT("the experience table has rows"), Row))
		{
			float Remainder = 0.f, Lifetime = 0.f;
			const int32 Banked = ElysiumXp::Bank(ElysiumXp::WithModifier(Row->Value, 0), Remainder, Lifetime);
			TestEqual(*FString::Printf(TEXT("'%s' (%d raw) banks floor(v/100)"), *Row->Key, Row->Value),
				Banked, Row->Value / 100);
		}
		// Every shipped value is in hundredths — `N01` — bar the file's own "Junk for testing"
		// block. That is what makes the kept remainder worth one bonus point per 100 awards.
		int32 Hundredths = 0;
		for (const FElysiumExperienceEntry& Entry : Experience.Rows)
		{
			if (Entry.Value % 100 == 1) { ++Hundredths; }
		}
		TestTrue(TEXT("nearly every award is authored as N01"), Hundredths >= Experience.Num() - 8);
	}

	return true;
}

// =================================================================================================
// Quests against the real catalogue (9.4d) — what the content-free tier's hand-built fixture
// cannot answer: that the shipped 161 `AwardXP` keys are actually REACHABLE through the state
// change that owes them, not merely present in the file. The entity-side award walk itself
// (give-once, Experience_Modifier, the remainder-keeping /100) is `Elysium.Content.SheetMath`'s.
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumQuestContentTest,
	"Elysium.Content.Quests", GElysiumContentTestFlags)

bool FElysiumQuestContentTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("vdata") })) return true;
	if (!IFileManager::Get().FileExists(
			*FElysiumContentPaths::VdataFile(TEXT("system/quests_santamonica.txt"))))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported vdata (run: uv run elysium export bundle vdata)"));
		return true;
	}

	FString Error;
	FElysiumQuestTables Quests;
	FElysiumExperienceTable Experience;
	if (!TestTrue(TEXT("quests_*.txt load"), Quests.Load(Error)) ||
		!TestTrue(TEXT("experience_table.txt loads"), Experience.Load(Error)))
	{
		AddError(Error);
		return true;
	}

	// --- Every authored award is reachable, and every reached award resolves --------------------
	// Walk every quest to every one of its states through the real decision function, exactly as
	// `SetQuestState` does, and check the key it hands back against `experience_table`.
	int32 Reached = 0, Unresolved = 0, Repeats = 0;
	for (int32 t = 0; t < FElysiumQuestTables::NumTables; ++t)
	{
		for (const FElysiumQuest& Q : Quests.Quests[t])
		{
			TArray<FElysiumAssignedQuest> Journal;
			for (int32 i = 1; i <= Q.States.Num(); ++i)
			{
				const ElysiumQuestLog::FOutcome Out =
					ElysiumQuestLog::Apply(Quests, Journal, Q.Title, i);
				if (!Out.bChanged)
				{
					AddError(FString::Printf(TEXT("quest \"%s\" state %d did not change"), *Q.Title, i));
					continue;
				}
				if (Out.AwardXpKey.IsEmpty())
				{
					continue;
				}
				++Reached;
				if (Experience.Find(Out.AwardXpKey) == nullptr) { ++Unresolved; }

				// Immediately re-setting the same state owes nothing — the property that keeps a
				// re-fired dialogue line from paying twice.
				const ElysiumQuestLog::FOutcome Again =
					ElysiumQuestLog::Apply(Quests, Journal, Q.Title, i);
				if (Again.bChanged || !Again.AwardXpKey.IsEmpty()) { ++Repeats; }
			}
			// One quest, one row, however many states it walked through.
			TestEqual(TEXT("a walked quest holds exactly one journal row"), Journal.Num(), 1);
		}
	}
	AddInfo(FString::Printf(TEXT("AwardXP reached through a state change: %d"), Reached));
	TestTrue(TEXT("at least 150 awards are reachable"), Reached >= 150);
	TestEqual(TEXT("every reached award resolves in experience_table"), Unresolved, 0);
	TestEqual(TEXT("no repeat set owes anything"), Repeats, 0);

	// --- The worked case, end to end -----------------------------------------------------------
	TArray<FElysiumAssignedQuest> Journal;
	ElysiumQuestLog::FOutcome Out = ElysiumQuestLog::Apply(Quests, Journal, TEXT("Arthur Knox"), 1);
	TestTrue(TEXT("Arthur Knox assigns"), Out.bChanged);
	TestEqual(TEXT("as the first quest of the run, order 1"), Out.Order, 1);
	TestEqual(TEXT("with the shipped display name"), Out.DisplayName,
		FString(TEXT("A Bounty For The Hunter")));

	Out = ElysiumQuestLog::Apply(Quests, Journal, TEXT("Arthur Knox"), 2);
	TestEqual(TEXT("state 2 owes Carson01"), Out.AwardXpKey, FString(TEXT("Carson01")));
	if (const FElysiumExperienceEntry* Row = Experience.Find(Out.AwardXpKey))
	{
		// Raw 101 -> one whole XP point, with 0.01 banked as residue (`ElysiumXp::Bank`).
		TestEqual(TEXT("Carson01 is worth 101 raw"), Row->Value, 101);
		float Remainder = 0.f, Lifetime = 0.f;
		TestEqual(TEXT("which banks one whole XP point"),
			ElysiumXp::Bank(Row->Value, Remainder, Lifetime), 1);
		TestEqual(TEXT("keeping the sub-100 residue"), Remainder, 1.0f);
	}
	TestEqual(TEXT("and the journal still holds one row"), Journal.Num(), 1);
	TestEqual(TEXT("on the santamonica table"), Journal[0].Table, 4);

	// A quest assigned second gets order 2, whichever hub it came from.
	Out = ElysiumQuestLog::Apply(Quests, Journal, TEXT("Tutorial"), 1);
	TestTrue(TEXT("Tutorial assigns"), Out.bChanged);
	TestEqual(TEXT("as the second quest, order 2"), Out.Order, 2);

	// --- The screen's read of the same data (9.4e) ----------------------------------------------
	// Assign every shipped quest at its first state, then check the view places each one exactly
	// once across the four hub tabs. A row that lands in no tab is a quest the player can never
	// read, which is the failure this catches.
	{
		TArray<FElysiumAssignedQuest> All;
		int32 Assigned = 0;
		for (int32 t = 0; t < FElysiumQuestTables::NumTables; ++t)
		{
			for (const FElysiumQuest& Q : Quests.Quests[t])
			{
				if (Q.States.Num() > 0 && ElysiumQuestLog::Apply(Quests, All, Q.Title, 1).bChanged)
				{
					++Assigned;
				}
			}
		}
		TestEqual(TEXT("every shipped quest assigns"), All.Num(), Assigned);

		int32 Placements = 0, Unresolvable = 0;
		for (int32 Hub : FElysiumQuestTables::HubTabOrder)
		{
			const ElysiumQuestView::FView View = ElysiumQuestView::Build(Quests, All, Hub);
			Placements += View.Num();
			for (const ElysiumQuestView::FEntry& E : View.Active)
			{
				if (!E.bResolved) { ++Unresolvable; }
			}
		}
		TestEqual(TEXT("every row the view shows resolves against the catalogue"), Unresolvable, 0);

		// A `main` row is shown under all four tabs by design, so the total placement count is the
		// journal size plus three extra copies of each cross-hub quest.
		const int32 MainRows = All.FilterByPredicate([](const FElysiumAssignedQuest& R)
			{ return R.Table == FElysiumQuestTables::MainTable; }).Num();
		TestEqual(TEXT("each quest is placed once per tab it belongs to"),
			Placements, All.Num() + MainRows * 3);
		AddInfo(FString::Printf(TEXT("journal %d rows, %d cross-hub, %d tab placements"),
			All.Num(), MainRows, Placements));

		// **`quests_main.txt` ships with every quest commented out** — the file is the format's own
		// documentation template and authors none. So the cross-hub rule is schema with no data
		// behind it on retail content, the same standing as `AwardMoney` and `Event`, and all four
		// hub tabs together cover every shipped quest. Asserted rather than assumed, because the
		// day a row appears there the fold-in stops being theoretical.
		TestEqual(TEXT("no shipped quest lives on the main table"),
			Quests.Quests[FElysiumQuestTables::MainTable].Num(), 0);
		TestEqual(TEXT("so every placement is a single tab"), Placements, All.Num());

		// The opening hub for a character who has never opened the screen is a real hub, not a
		// sentinel the tab row cannot draw.
		const int32 Opening = ElysiumQuestView::DefaultHub(Quests, All);
		TestTrue(TEXT("the default hub is one of the four tabs"),
			Algo::AnyOf(FElysiumQuestTables::HubTabOrder,
				[Opening](int32 H) { return H == Opening; }));
	}

	return true;
}


// ================================================================================================
// Chargen over the real rulebook — the pools a clan produces and the baseline it stands on (9.4f)
// ================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumChargenContentTest,
	"Elysium.Content.Chargen", GElysiumContentTestFlags)
bool FElysiumChargenContentTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("vdata") })) return true;
	if (!IFileManager::Get().FileExists(*FElysiumContentPaths::VdataFile(TEXT("system/stats.txt"))))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported vdata (run: uv run elysium export bundle vdata)"));
		return true;
	}

	FString Error;
	FElysiumStatTable Stats;
	FElysiumRules RuleData;
	FElysiumClanTable Clans;
	FElysiumHistoryTable Histories;
	FElysiumLevelingTemplates Leveling;
	FElysiumTraitEffects Effects;
	FElysiumFeatTable Feats;
	FElysiumStrings Strings;

	const bool bLoaded =
		TestTrue(TEXT("stats.txt loads"), Stats.Load(Error))
		& TestTrue(TEXT("rules load"), RuleData.Load(Error))
		& TestTrue(TEXT("clandoc000.txt loads"), Clans.Load(Error))
		& TestTrue(TEXT("histories000.txt loads"), Histories.Load(Error))
		& TestTrue(TEXT("levelingtemplate_000.txt loads"), Leveling.Load(Error))
		& TestTrue(TEXT("traiteffects load"), Effects.Load(Error))
		& TestTrue(TEXT("feats.txt loads"), Feats.Load(Error))
		& TestTrue(TEXT("strings load"), Strings.Load(Error));
	if (!bLoaded)
	{
		AddError(Error);
		return true;
	}

	FElysiumChargenRules Rules;
	Rules.Stats = &Stats;
	Rules.Rules = &RuleData;
	Rules.Clans = &Clans;
	Rules.Histories = &Histories;
	Rules.Leveling = &Leveling;
	Rules.TraitEffects = &Effects;
	Rules.Feats = &Feats;
	Rules.Strings = &Strings;

	// --- the string groups a symbolic trait value resolves through -------------------------------
	{
		TestEqual(TEXT("AttributeOrder names all seven orderings"),
			Strings.Group(TEXT("AttributeOrder")) ? Strings.Group(TEXT("AttributeOrder"))->Num() : 0, 7);
		TestEqual(TEXT("  Physical_Mental_Social is ordering 1"),
			Strings.IndexOf(TEXT("AttributeOrder"), TEXT("Physical_Mental_Social")), 1);
		TestEqual(TEXT("AbilityOrder names all seven"),
			Strings.Group(TEXT("AbilityOrder")) ? Strings.Group(TEXT("AbilityOrder"))->Num() : 0, 7);
		TestEqual(TEXT("  Talents_Skills_Knowledges is ordering 0"),
			Strings.IndexOf(TEXT("AbilityOrder"), TEXT("Talents_Skills_Knowledges")), 0);

		// The two files merge: `AttributeOrder` is internal-only and `AttributeGroup` localized, and
		// both have to be reachable through one table.
		TestEqual(TEXT("the localized file's groups survive the merge"),
			Strings.At(TEXT("AttributeGroup"), 0), FString(TEXT("Physical")));
		TestEqual(TEXT("  including the ability headings"),
			Strings.At(TEXT("AbilityGroup"), 2), FString(TEXT("Knowledges")));
		TestEqual(TEXT("  and the sheet's category titles"),
			Strings.At(TEXT("StatCategoryTitles"), 6), FString(TEXT("Disciplines")));

		// A stat's `NameMapping` names a group that must exist, or its value has no display and no
		// symbolic reading.
		int32 Missing = 0;
		for (int32 c = 0; c < (int32)EElysiumTraitContainer::Count; ++c)
		{
			for (const FElysiumStat& Stat : Stats.Container((EElysiumTraitContainer)c).Stats)
			{
				if (!Stat.NameMapping.IsEmpty() && Strings.Group(Stat.NameMapping) == nullptr)
				{
					++Missing;
					AddError(FString::Printf(TEXT("stat '%s' maps to unknown string group '%s'"),
						*Stat.InternalName, *Stat.NameMapping));
				}
			}
		}
		TestEqual(TEXT("every NameMapping resolves to a string group"), Missing, 0);
	}

	// --- a clan template's symbolic trait values survive the int maps ----------------------------
	{
		FElysiumClanTemplate Brujah;
		if (TestTrue(TEXT("Player_Brujah resolves"),
			ElysiumChargen::ResolveClanTemplate(Rules, 2, Brujah)))
		{
			// The trait maps hold ints, so an unrecorded symbolic value would read as ordering 0 —
			// a different point split entirely.
			TestEqual(TEXT("Brujah's Attrib_Order is authored as a name"),
				Brujah.TraitStr(TEXT("Attrib_Order")), FString(TEXT("Physical_Mental_Social")));
			TestEqual(TEXT("  which resolves to ordering 1"),
				ElysiumChargen::ResolveOrder(Rules, Brujah, EElysiumTraitContainer::Attributes,
					ElysiumSlot::AttribOrder), 1);
			TestEqual(TEXT("  and its Ability_Order to 0"),
				ElysiumChargen::ResolveOrder(Rules, Brujah, EElysiumTraitContainer::Abilities, 0), 0);
			// The chargen leveling template is named in the Attributes block and is not a stat slot,
			// so it exists only as authored text.
			TestEqual(TEXT("the CharGen template is named on the clan"),
				Brujah.TraitStr(TEXT("CharGen_AutoLevel_Template")), FString(TEXT("Brujah_CharGen")));
			TestNotNull(TEXT("  and it is a real template"),
				Leveling.Find(Brujah.TraitStr(TEXT("CharGen_AutoLevel_Template"))));
		}
	}

	// --- every playable clan produces the same total, split by its own ordering -------------------
	for (int32 Clan = 2; Clan <= 8; ++Clan)
	{
		const TCHAR* Name = FElysiumSheet::ClanName(Clan);
		FElysiumClanTemplate Template;
		if (!TestTrue(*FString::Printf(TEXT("Player_%s resolves"), Name),
			ElysiumChargen::ResolveClanTemplate(Rules, Clan, Template)))
		{
			continue;
		}
		const int32 AttribOrder = ElysiumChargen::ResolveOrder(Rules, Template,
			EElysiumTraitContainer::Attributes, ElysiumSlot::AttribOrder);
		const int32 AbilityOrder = ElysiumChargen::ResolveOrder(Rules, Template,
			EElysiumTraitContainer::Abilities, 0);
		TestTrue(*FString::Printf(TEXT("%s names a real attribute ordering"), Name),
			AttribOrder >= 0 && AttribOrder < 6);
		TestTrue(*FString::Printf(TEXT("%s names a real ability ordering"), Name),
			AbilityOrder >= 0 && AbilityOrder < 6);

		const FElysiumChargenPools P = ElysiumChargen::BuildPools(Rules, Clan, AttribOrder,
			AbilityOrder, true);
		TestEqual(*FString::Printf(TEXT("%s gets 3 attribute points"), Name),
			P[EElysiumChargenPool::Physical] + P[EElysiumChargenPool::Social]
			+ P[EElysiumChargenPool::Mental], 3);
		TestEqual(*FString::Printf(TEXT("%s gets 6 ability points"), Name),
			P[EElysiumChargenPool::Talents] + P[EElysiumChargenPool::Skills]
			+ P[EElysiumChargenPool::Knowledges], 6);
		// `Subpool_Disciplines` is the one subpool table Troika left non-zero — the file's own note
		// says so — and it ships 1 for every clan.
		TestEqual(*FString::Printf(TEXT("%s gets 1 discipline point"), Name),
			P[EElysiumChargenPool::Disciplines], 1);
	}

	// --- the baseline: seeded, templated, then bought through the CharGen template ----------------
	{
		FElysiumChargenState State;
		State.Clan = 2;                  // Brujah
		State.bMale = false;
		State.HistoryId = 0;             // "None" — no effect group
		ElysiumChargen::ApplyBaseline(State, Rules);

		TestEqual(TEXT("the clan slot survives the baseline"), State.Sheet.Clan(), 2);
		TestFalse(TEXT("and so does the sex"), State.Sheet.IsMale());
		TestEqual(TEXT("the resolved order lands on the sheet"),
			State.Sheet.GetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::AttribOrder), 1);

		// Ordering 1 is `Physical_Mental_Social`, which is what the shipped screen draws as
		// PHYSICAL(2) / SOCIAL / MENTAL(1) — the zero pool showing no parenthetical at all.
		TestEqual(TEXT("Brujah's Physical pool is 2"), State.Pools[EElysiumChargenPool::Physical], 2);
		TestEqual(TEXT("  Mental 1"), State.Pools[EElysiumChargenPool::Mental], 1);
		TestEqual(TEXT("  Social 0"), State.Pools[EElysiumChargenPool::Social], 0);
		TestEqual(TEXT("  Talents 3"), State.Pools[EElysiumChargenPool::Talents], 3);
		TestEqual(TEXT("  Skills 2"), State.Pools[EElysiumChargenPool::Skills], 2);
		TestEqual(TEXT("  Knowledges 1"), State.Pools[EElysiumChargenPool::Knowledges], 1);
		TestFalse(TEXT("and nothing is spent yet"), State.IsSpentOut());

		// `Brujah_CharGen`'s Physical_Mental_Social group buys Strength 2, Dexterity 1, Stamina 2 and
		// Wits 2 — the dots VtMB grants through `giftxp 9000` + `vautolvl`, not a written block.
		TestEqual(TEXT("the baseline bought Strength to 2"),
			State.Sheet.GetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Strength), 2);
		TestEqual(TEXT("  Stamina to 2"),
			State.Sheet.GetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Stamina), 2);
		TestEqual(TEXT("  and Wits to 2"),
			State.Sheet.GetBase(EElysiumTraitContainer::Attributes, 9), 2);
		TestEqual(TEXT("the baseline is snapshotted as the sell floor"),
			State.Baseline.GetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Strength), 2);

		// The clan effect layer is live while the player is still spending, so the Brujah gift is
		// already on the sheet's feat reads.
		TestTrue(TEXT("the clan effect group resolved"),
			State.Effects.ResolvedGroups().Contains(TEXT("Clan (Brujah)")));

		// Brujah has Celerity, Potence, Presence, Blood_Healing and Corpus_Vampirus; every other
		// discipline stays at the -1 default and draws no row.
		int32 Visible = 0;
		for (const FElysiumSheetSlot& Slot : ElysiumSheetSlots(EElysiumTraitContainer::Disciplines))
		{
			Visible += ElysiumChargen::IsRowVisible(Rules, State.Sheet,
				EElysiumTraitContainer::Disciplines, Slot.Index) ? 1 : 0;
		}
		TestEqual(TEXT("only the clan's own disciplines draw a row"), Visible, 5);

		int32 Cost = 0;
		TestFalse(TEXT("a non-clan discipline cannot be bought"),
			ElysiumChargen::CanBuy(State, Rules, EElysiumTraitContainer::Disciplines, 0, Cost));
		TestTrue(TEXT("a clan discipline can"), ElysiumChargen::CanBuy(State, Rules,
			EElysiumTraitContainer::Disciplines, 3, Cost));   // Celerity

		// Spending the discipline point out empties that pool and nothing else.
		TestTrue(TEXT("the discipline point spends"), ElysiumChargen::Buy(State, Rules,
			EElysiumTraitContainer::Disciplines, 3));
		TestEqual(TEXT("  leaving the discipline pool empty"),
			State.Remaining(EElysiumChargenPool::Disciplines), 0);
		TestFalse(TEXT("  and refusing a second"), ElysiumChargen::CanBuy(State, Rules,
			EElysiumTraitContainer::Disciplines, 4, Cost));
		TestTrue(TEXT("  while it sells back for exactly what it cost"),
			ElysiumChargen::Sell(State, Rules, EElysiumTraitContainer::Disciplines, 3));
		TestEqual(TEXT("  restoring the pool"), State.Remaining(EElysiumChargenPool::Disciplines), 1);

		// A clan change re-runs the baseline, which must replace the old clan rather than layer on
		// it — a Nosferatu who kept Brujah's disciplines would be the failure.
		State.Clan = 5;
		ElysiumChargen::ApplyBaseline(State, Rules);
		TestEqual(TEXT("a clan change re-derives the sheet"), State.Sheet.Clan(), 5);
		TestFalse(TEXT("  dropping the old clan's Celerity"), ElysiumChargen::IsRowVisible(
			Rules, State.Sheet, EElysiumTraitContainer::Disciplines, 3));
		TestEqual(TEXT("  and nothing carries over as spent"), State.Spent.Total(), 0);
	}


	// --- the quiz runner over the real wizard -----------------------------------------------------
	{
		FElysiumWizard Wizard;
		FString WizError;
		if (TestTrue(TEXT("charcreatewizard.txt loads"), Wizard.Load(WizError)))
		{
			// The entry popup is the wizard's own, and route 3 is filtered out: the Society of
			// Leopold campaign is a marked omission, so its action must not be offered.
			FElysiumChargenState Quiz;
			Quiz.Clan = 2;
			FElysiumWizRun Run;
			ElysiumChargen::WizBegin(Run, Wizard, Quiz, ElysiumChargen::WizEntryPopup);
			if (TestTrue(TEXT("the entry popup opens"), Run.IsActive()))
			{
				TestEqual(TEXT("  offering two routes, not three"), Run.Choices.Num(), 2);
				TestTrue(TEXT("  route 1 walks the quiz"),
					Run.Choices[0]->Next.Equals(TEXT("Trait_Male_vs_Female")));
				TestTrue(TEXT("  route 2 ends it"), Run.Choices[1]->Next.IsEmpty());

				// Route 2: the chain ends immediately and nothing was tallied.
				FElysiumChargenState Direct = Quiz;
				FElysiumWizRun Straight;
				ElysiumChargen::WizBegin(Straight, Wizard, Direct, ElysiumChargen::WizEntryPopup);
				TestFalse(TEXT("route 2 finishes the run"),
					ElysiumChargen::WizChoose(Straight, Direct, 1));
				TestTrue(TEXT("  with nothing tallied"), Direct.Tally.IsEmpty());
			}

			// Route 1, walked to the end from a seeded stream. The run must terminate, and it must
			// terminate the same way twice — the phrasing of each question is a random pick, so a
			// replay that diverged would mean the stream is not the only source of it.
			auto Walk = [&Wizard](int32 Seed, FElysiumChargenState& Out) -> int32
			{
				ElysiumRng::SeedAll(Seed);
				Out = FElysiumChargenState();
				Out.Clan = 2;
				FElysiumWizRun Run;
				ElysiumChargen::WizBegin(Run, Wizard, Out, ElysiumChargen::WizEntryPopup);
				ElysiumChargen::WizChoose(Run, Out, 0);   // route 1

				int32 Steps = 0;
				while (Run.IsActive() && Steps < 64)
				{
					// Always the first surviving answer, so the only variation left is the stream's.
					ElysiumChargen::WizChoose(Run, Out, 0);
					++Steps;
				}
				return Steps;
			};

			FElysiumChargenState First, Second;
			const int32 StepsA = Walk(1234, First);
			const int32 StepsB = Walk(1234, Second);
			TestTrue(TEXT("the quiz terminates"), StepsA > 0 && StepsA < 64);
			TestEqual(TEXT("  and replays identically from the same seed"), StepsB, StepsA);
			TestEqual(TEXT("  tallying the same traits"), Second.Tally.Num(), First.Tally.Num());
			for (const TPair<FString, int32>& Pair : First.Tally)
			{
				const int32* Other = Second.Tally.Find(Pair.Key);
				TestEqual(*FString::Printf(TEXT("  '%s' tallies the same"), *Pair.Key),
					Other ? *Other : -1, Pair.Value);
			}
			TestTrue(TEXT("  and it tallied something"), First.Tally.Num() > 0);

			// The ordering is the top three by tally, and it is what the clan suggestion scores.
			TArray<FString> Ordering;
			ElysiumChargen::WizOrdering(First, Wizard, Ordering);
			TestTrue(TEXT("the ordering names at most three traits"), Ordering.Num() <= 3);
			TestTrue(TEXT("  each of them a wizard trait"),
				Ordering.Num() == 0 || Wizard.Traits.Contains(Ordering[0]));
		}
	}

	// --- the quiz's clan suggestion ---------------------------------------------------------------
	{
		FElysiumWizard Wizard;
		if (TestTrue(TEXT("charcreatewizard.txt loads"), Wizard.Load(Error)))
		{
			// A player whose three picks are all a clan's Primaries must be suggested that clan, and
			// the answer must be a clan index the sheet can actually hold.
			for (const FElysiumWizClanNode& Node : Wizard.ClanNodes)
			{
				if (Node.Primary.IsEmpty())
				{
					continue;
				}
				const TArray<FString> Ordering = { Node.Primary[0] };
				const int32 Suggested = ElysiumChargen::SuggestClan(Wizard, Clans, Ordering);
				TestTrue(*FString::Printf(TEXT("a pick of '%s' suggests a playable clan"),
					*Node.Primary[0]), FElysiumSheet::IsValidClan(Suggested));
			}
			TestEqual(TEXT("no picks suggests nothing"),
				ElysiumChargen::SuggestClan(Wizard, Clans, {}), 0);
		}
	}

	return true;
}

// =====================================================================================
// 12.1 — the whole choreographed-scene corpus: every `.vcd` the pipeline mirrored (PL9).
//
// This is what turns `docs/vtmb/choreographed_scenes.md`'s survey numbers into a regression test. The
// doc's histograms were produced by research/tooling/probes/probe_scenes.py over the install; this reads the mirror
// with the runtime's own parser and asserts the two agree. A drift here means either the exporter
// changed what it mirrors or this reader diverged from the reference grammar — both worth failing.
//
// Self-skips when out/scenes has not been exported.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSceneCorpusTest, "Elysium.Content.SceneCorpus", GElysiumContentTestFlags)
bool FElysiumSceneCorpusTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("scenes"), TEXT("maps") })) return true;
	const FString ScenesDir = FElysiumContentPaths::ScenesDir();

	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *ScenesDir, TEXT("*.vcd"), /*Files*/ true, /*Dirs*/ false);
	if (Files.Num() == 0)
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no scenes under $ELYSIUM_EXPORT_ROOT/scenes (run the pipeline to enable)"));
		return true;
	}

	int32 TypeTotals[static_cast<int32>(EElysiumChoreoEvent::Count)] = {};
	int32 NumValid = 0;
	int32 NumUnreadable = 0;
	int32 NumVersion1 = 0;
	int32 NumNoVersion = 0;
	int32 NumFps60 = 0;
	int32 NumSnapOn = 0;
	int32 TotalDegenerate = 0;
	int32 TotalEvents = 0;

	for (const FString& Path : Files)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			++NumUnreadable;
			continue;
		}

		FElysiumSceneData S;
		ElysiumScene::ParseText(Text, Path, S);

		if (S.bValid) { ++NumValid; }
		if (S.Version == 1) { ++NumVersion1; }
		else if (S.Version == INDEX_NONE) { ++NumNoVersion; }
		if (FMath::IsNearlyEqual(S.Fps, 60.f)) { ++NumFps60; }
		if (S.bSnap) { ++NumSnapOn; }
		TotalDegenerate += S.NumDegenerate;
		TotalEvents += S.Events.Num();

		for (int32 i = 0; i < static_cast<int32>(EElysiumChoreoEvent::Count); ++i)
		{
			TypeTotals[i] += S.TypeCounts[i];
		}
	}

	AddInfo(FString::Printf(TEXT("%d scene files, %d events, %d valid, %d degenerate ranges"),
		Files.Num(), TotalEvents, NumValid, TotalDegenerate));

	// The mirror the exporter writes (PL9). A change here is an export change, not a parser one.
	TestEqual(TEXT("the corpus is the 5,444 scenes PL9 mirrors"), Files.Num(), 5444);
	TestEqual(TEXT("every scene file is readable"), NumUnreadable, 0);
	TestEqual(TEXT("every scene names at least one actor"), NumValid, Files.Num());

	// `// Choreo version 1` on 5,434; 10 files carry no version line at all.
	TestEqual(TEXT("version 1 count"), NumVersion1, 5434);
	TestEqual(TEXT("no-version count"), NumNoVersion, 10);
	// `fps 60` on all 5,443 that have an fps line — the one without falls back to the same default,
	// so this counts 5,444. `snap on` appears exactly once.
	TestEqual(TEXT("fps is 60 everywhere"), NumFps60, Files.Num());
	TestEqual(TEXT("snap on appears once"), NumSnapOn, 1);

	// The nine live event types, straight out of `docs/vtmb/choreographed_scenes.md`.
	const TPair<EElysiumChoreoEvent, int32> Expected[] = {
		{ EElysiumChoreoEvent::Silence,     12089 },
		{ EElysiumChoreoEvent::Loud,         9809 },
		{ EElysiumChoreoEvent::Speak,        5541 },
		{ EElysiumChoreoEvent::Expression,   1431 },
		{ EElysiumChoreoEvent::Gesture,       609 },
		{ EElysiumChoreoEvent::Sequence,      242 },
		{ EElysiumChoreoEvent::FireTrigger,    24 },
		{ EElysiumChoreoEvent::Python,          2 },
		{ EElysiumChoreoEvent::BodySound,       1 },
	};
	for (const TPair<EElysiumChoreoEvent, int32>& E : Expected)
	{
		TestEqual(*FString::Printf(TEXT("event count: %s"), ElysiumScene::EventTypeName(E.Key)),
			TypeTotals[static_cast<int32>(E.Key)], E.Value);
	}

	// The ten types VtMB's parser recognises and no shipped scene authors. If one of these ever
	// goes non-zero, the runtime is dropping an event it has no handler for — a real finding.
	const EElysiumChoreoEvent Dead[] = {
		EElysiumChoreoEvent::Section, EElysiumChoreoEvent::LookAt, EElysiumChoreoEvent::MoveTo,
		EElysiumChoreoEvent::Face, EElysiumChoreoEvent::FlexAnimation, EElysiumChoreoEvent::Subscene,
		EElysiumChoreoEvent::Loop, EElysiumChoreoEvent::CameraMove, EElysiumChoreoEvent::CameraShot,
		EElysiumChoreoEvent::CameraRestore,
	};
	for (EElysiumChoreoEvent T : Dead)
	{
		TestEqual(*FString::Printf(TEXT("dead event type stays unauthored: %s"),
			ElysiumScene::EventTypeName(T)), TypeTotals[static_cast<int32>(T)], 0);
	}
	TestEqual(TEXT("no event carries an unrecognised type token"),
		TypeTotals[static_cast<int32>(EElysiumChoreoEvent::Unknown)], 0);

	// --- every SceneFile an exported map names must resolve through the same path rule ---------
	TArray<FString> EntsFiles;
	IFileManager::Get().FindFilesRecursive(EntsFiles, *FElysiumContentPaths::Root(), TEXT("*.ents"),
		/*Files*/ true, /*Dirs*/ false);

	int32 NumReferenced = 0;
	int32 NumResolved = 0;
	TArray<FString> Missing;
	for (const FString& EntsPath : EntsFiles)
	{
		FElysiumEntityDefs Defs;
		if (!FElysiumEntityDefs::Parse(EntsPath, Defs))
		{
			continue;
		}
		for (const FElysiumEntityDef& Def : Defs.Defs)
		{
			const FString* SceneFile = Def.Keys.Find(TEXT("SceneFile"));
			if (SceneFile == nullptr || SceneFile->IsEmpty())
			{
				continue;
			}
			++NumReferenced;
			const FString Full = FElysiumContentPaths::SceneFile(ElysiumScene::NormalizeSceneRel(*SceneFile));
			if (IFileManager::Get().FileExists(*Full))
			{
				++NumResolved;
			}
			else
			{
				Missing.AddUnique(*SceneFile);
			}
		}
	}

	AddInfo(FString::Printf(TEXT("%d SceneFile references across the exported maps, %d resolve"),
		NumReferenced, NumResolved));
	for (const FString& M : Missing)
	{
		AddWarning(FString::Printf(TEXT("SceneFile does not resolve: %s"), *M));
	}
	// Eight scenes referenced by three maps are absent from the install itself (map data outliving
	// its assets) — none of them on an exported map today, so this is currently exact. It is a
	// warning list plus a hard check, so a broken path rule fails while Troika's own gaps do not.
	TestEqual(TEXT("every SceneFile an exported map names resolves"), NumResolved, NumReferenced);

	return true;
}

// =====================================================================================
// 12.1 / PL16 — the cinematic anim sets a scene's actors animate out of.
//
// A whole-cast performance lives in a `models/cinematic/**.mdl` that no NPC's include tree names,
// so it reaches the runtime only through this seed. The join asserted here is the one the runtime
// makes at `sequence "entire_scene"`: (the scene's anim-set model, the actor's `bonerename` source)
// -> a bank stem that exists. A non-zero miss count means the export no longer covers the content.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSceneAnimSetsTest, "Elysium.Content.SceneAnimSets",
	GElysiumContentTestFlags)
bool FElysiumSceneAnimSetsTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("scenes"), TEXT("maps") })) return true;
	FElysiumNpcIndex Index;
	FString Error;
	if (!Index.Load(Error))
	{
		AddInfo(FString::Printf(TEXT("ELYSIUM_TEST_ABSTAIN: no NPC index (%s)"), *Error));
		return true;
	}
	if (Index.Cinematics.IsEmpty())
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the NPC export carries no cinematic anim sets (re-run npc_export)"));
		return true;
	}

	TArray<FString> EntsFiles;
	IFileManager::Get().FindFilesRecursive(EntsFiles, *FElysiumContentPaths::Root(), TEXT("*.ents"),
		/*Files*/ true, /*Dirs*/ false);

	int32 NumSets = 0, NumSetsResolved = 0;
	int32 NumActorBinds = 0, NumActorBindsResolved = 0;
	TArray<FString> MissingSets;

	for (const FString& EntsPath : EntsFiles)
	{
		FElysiumEntityDefs Defs;
		if (!FElysiumEntityDefs::Parse(EntsPath, Defs))
		{
			continue;
		}
		for (const FElysiumEntityDef& Def : Defs.Defs)
		{
			if (Def.Classname != TEXT("logic_choreographed_scene"))
			{
				continue;
			}
			// Whichever anim set this scene would use — check every key it carries.
			for (const TCHAR* Key : { TEXT("BaseAnim"), TEXT("MaleAnim"), TEXT("FemaleAnim") })
			{
				const FString* Model = Def.Keys.Find(Key);
				if (Model == nullptr || Model->IsEmpty())
				{
					continue;
				}
				++NumSets;
				// The set itself proves the model was exported. Validate every bank it names rather
				// than asking an empty actor root to choose arbitrarily from a multi-actor set.
				const FElysiumCinematicSet* Set = Index.FindCinematic(*Model);
				bool bAllBanksExist = Set != nullptr && !Set->Roots.IsEmpty();
				if (Set != nullptr)
				{
					for (const TPair<FString, FString>& RootBank : Set->Roots)
					{
						bAllBanksExist &= Index.Banks.Contains(RootBank.Value);
					}
				}
				if (bAllBanksExist)
				{
					++NumSetsResolved;
				}
				else
				{
					MissingSets.AddUnique(*Model);
				}
			}

			// And the per-actor root each scene actually asks for.
			const FString* SceneFile = Def.Keys.Find(TEXT("SceneFile"));
			const FString* BaseAnim = Def.Keys.Find(TEXT("BaseAnim"));
			if (SceneFile == nullptr || BaseAnim == nullptr || BaseAnim->IsEmpty())
			{
				continue;
			}
			TSharedPtr<const FElysiumSceneData> Scene = ElysiumScene::Load(*SceneFile);
			if (!Scene.IsValid())
			{
				continue;
			}
			for (const FElysiumSceneActor& Actor : Scene->Actors)
			{
				++NumActorBinds;
				const FString Bank = Index.CinematicBank(*BaseAnim, Actor.BoneFrom);
				if (!Bank.IsEmpty() && Index.Banks.Contains(Bank))
				{
					++NumActorBindsResolved;
				}
			}
		}
	}

	AddInfo(FString::Printf(TEXT("%d anim-set references (%d resolve), %d actor bone-roots (%d resolve)"),
		NumSets, NumSetsResolved, NumActorBinds, NumActorBindsResolved));
	for (const FString& M : MissingSets)
	{
		AddWarning(FString::Printf(TEXT("anim set not exported: %s"), *M));
	}

	TestEqual(TEXT("every anim set an exported scene names was exported and split"),
		NumSetsResolved, NumSets);
	TestEqual(TEXT("every scene actor's bonerename root resolves to a bank"),
		NumActorBindsResolved, NumActorBinds);

	return true;
}

// The blink cadence the eye pass schedules from (roadmap 12.4). The table authors the key under two
// spellings — `"Min Blink Interval"` on four rows and `"MinBlinkInterval"` on six — and reading only
// one is a silent fallback to the defaults rather than a parse error, which is exactly the shape of
// bug this tier exists to catch: the two rows that carry a cadence other than 2.5/6.0 are both
// spelled the second way.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDispositionBlinkContentTest,
	"Elysium.Content.DispositionBlink", GElysiumContentTestFlags)

bool FElysiumDispositionBlinkContentTest::RunTest(const FString&)
{
	if (!IFileManager::Get().FileExists(
		*FElysiumContentPaths::VdataFile(TEXT("system/dispositiontable.txt"))))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: disposition table not exported"));
		return true;
	}
	FElysiumDispositionTable Table;
	FString Error;
	if (!TestTrue(TEXT("the disposition table loads"), Table.Load(Error)))
	{
		AddError(Error);
		return false;
	}

	int32 Cadences = 0;
	TSet<FString> Distinct;
	for (const TPair<FString, FElysiumDisposition>& Pair : Table.Rows)
	{
		const FElysiumDisposition& Row = Pair.Value;
		TestTrue(FString::Printf(TEXT("'%s' blinks at a positive interval"), *Pair.Key),
			Row.MinBlinkInterval > 0.f);
		TestTrue(FString::Printf(TEXT("'%s' has a usable blink range"), *Pair.Key),
			Row.MaxBlinkInterval >= Row.MinBlinkInterval);
		// Long enough for the 0.3 s envelope to finish before the next toggle is scheduled.
		TestTrue(FString::Printf(TEXT("'%s' blinks slower than the envelope"), *Pair.Key),
			Row.MinBlinkInterval > 0.3f);
		++Cadences;
		Distinct.Add(FString::Printf(TEXT("%.2f/%.2f"), Row.MinBlinkInterval, Row.MaxBlinkInterval));
	}
	TestTrue(TEXT("every disposition carries a cadence"), Cadences > 0);
	// More than one cadence reaches the rows. A single distinct value means the unspaced spelling
	// stopped being read and every row collapsed onto the default.
	//
	// The file authors three; the reader keys rows by name alone, so where a disposition is authored
	// several times over — `Anger` four times, once per `DispositionLevel` — only the last block
	// survives and `Enraged`'s 4.5/7.0 is not among them.
	TestTrue(FString::Printf(TEXT("the authored cadences survive the parse (%d distinct)"),
		Distinct.Num()), Distinct.Num() >= 2);

	// `Error` is the row a character falls to when its own disposition does not resolve, and it is
	// authored to blink fast enough to read as a tell. It is also spelled the unspaced way.
	if (const FElysiumDisposition* Err = Table.Rows.Find(TEXT("error")))
	{
		TestEqual(TEXT("Error blinks fast"), Err->MinBlinkInterval, 1.5f, 1e-3f);
		TestEqual(TEXT("and closes its range"), Err->MaxBlinkInterval, 2.f, 1e-3f);
	}
	else
	{
		AddError(TEXT("the disposition table carries no `Error` row"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAudioCatalogContentTest,
	"Elysium.Content.AudioCatalog", GElysiumContentTestFlags)

bool FElysiumAudioCatalogContentTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("audio") })) return true;
	const FString Path = FElysiumContentPaths::Root() / TEXT("audio/catalog.json");
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: audio catalog not exported"));
		return true;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	TestTrue(TEXT("audio catalog JSON parses"), FJsonSerializer::Deserialize(Reader, Root));
	if (!Root)
	{
		return false;
	}
	TestEqual(TEXT("audio catalog contract version"), Root->GetIntegerField(TEXT("version")), 1);
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	TestTrue(TEXT("audio catalog has entries"), Root->TryGetArrayField(TEXT("entries"), Entries));
	if (!Entries)
	{
		return false;
	}
	TSet<FString> Canonical;
	int32 ExplicitMissing = 0;
	for (const TSharedPtr<FJsonValue>& Value : *Entries)
	{
		const TSharedPtr<FJsonObject> Entry = Value->AsObject();
		if (!Entry)
		{
			AddError(TEXT("audio catalog contains a non-object entry"));
			continue;
		}
		const FString Rel = Entry->GetStringField(TEXT("canonical_path"));
		TestFalse(FString::Printf(TEXT("duplicate canonical audio path: %s"), *Rel),
			Canonical.Contains(Rel));
		Canonical.Add(Rel);
		const FString Disposition = Entry->GetStringField(TEXT("disposition"));
		const bool bKnown = Disposition == TEXT("present") ||
			Disposition == TEXT("optional") || Disposition == TEXT("missing_content");
		TestTrue(FString::Printf(TEXT("%s has an explicit validation disposition"), *Rel), bKnown);
		if (Disposition == TEXT("missing_content"))
		{
			++ExplicitMissing;
		}
	}
	AddInfo(FString::Printf(TEXT("%d canonical audio entries; %d explicit missing-content records"),
		Canonical.Num(), ExplicitMissing));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumAudioRoutingAssetsContentTest,
	"Elysium.Content.AudioRoutingAssets", GElysiumContentTestFlags)

bool FElysiumAudioRoutingAssetsContentTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("policy") })) return true;
	static const TCHAR* Packages[] = {
		TEXT("/Game/VtMB/Audio/SC_Master"),
		TEXT("/Game/VtMB/Audio/SC_Music"),
		TEXT("/Game/VtMB/Audio/SC_Dialogue"),
		TEXT("/Game/VtMB/Audio/SC_Ambience"),
		TEXT("/Game/VtMB/Audio/SC_SFX"),
		TEXT("/Game/VtMB/Audio/SC_UI"),
		TEXT("/Game/VtMB/Audio/SM_Master"),
		TEXT("/Game/VtMB/Audio/SM_Music"),
		TEXT("/Game/VtMB/Audio/SM_Dialogue"),
		TEXT("/Game/VtMB/Audio/SM_Ambience"),
		TEXT("/Game/VtMB/Audio/SM_SFX"),
		TEXT("/Game/VtMB/Audio/SM_UI"),
		TEXT("/Game/VtMB/Audio/SM_ReverbReturn"),
		TEXT("/Game/VtMB/Audio/CB_Master"),
		TEXT("/Game/VtMB/Audio/CB_Music"),
		TEXT("/Game/VtMB/Audio/CB_Dialogue"),
		TEXT("/Game/VtMB/Audio/CB_Ambience"),
		TEXT("/Game/VtMB/Audio/CB_SFX"),
		TEXT("/Game/VtMB/Audio/CB_UI"),
		TEXT("/Game/VtMB/Audio/CBM_User"),
		TEXT("/Game/VtMB/Audio/Concurrency_Music"),
		TEXT("/Game/VtMB/Audio/Concurrency_Dialogue"),
		TEXT("/Game/VtMB/Audio/Concurrency_Ambience"),
		TEXT("/Game/VtMB/Audio/Concurrency_SFX"),
		TEXT("/Game/VtMB/Audio/Concurrency_UI"),
		TEXT("/Game/VtMB/Audio/Attenuation_Point"),
		TEXT("/Game/VtMB/Audio/Attenuation_Dialogue"),
		TEXT("/Game/VtMB/Audio/Attenuation_Mover"),
		TEXT("/Game/VtMB/Audio/Attenuation_Ambient"),
	};
	for (const TCHAR* Package : Packages)
	{
		TestTrue(FString::Printf(TEXT("generated routing asset exists: %s"), Package),
			FPackageName::DoesPackageExist(Package));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMouseInputAssetsContentTest,
	"Elysium.Content.MouseInputAssets", GElysiumContentTestFlags)

bool FElysiumMouseInputAssetsContentTest::RunTest(const FString&)
{
	const UInputSettings* Settings = GetDefault<UInputSettings>();
	if (!TestNotNull(TEXT("engine input settings"), Settings))
	{
		return false;
	}
	TestFalse(TEXT("legacy mouse smoothing is disabled"), Settings->bEnableMouseSmoothing);
	const FInputAxisConfigEntry* Mouse2DConfig = Settings->AxisConfig.FindByPredicate(
		[](const FInputAxisConfigEntry& Entry) { return Entry.AxisKeyName == EKeys::Mouse2D.GetFName(); });
	if (TestNotNull(TEXT("Mouse2D axis config"), Mouse2DConfig))
	{
		TestEqual(TEXT("Mouse2D has no legacy sensitivity multiplier"),
			Mouse2DConfig->AxisProperties.Sensitivity, 1.0f);
	}

	UElysiumInputActionSet* ActionSet = LoadObject<UElysiumInputActionSet>(
		nullptr, ElysiumInputAssets::ActionSetPath);
	UInputMappingContext* Context = LoadObject<UInputMappingContext>(
		nullptr, ElysiumInputAssets::KeyboardMouseContextPath);
	if (!TestNotNull(TEXT("generated input action set"), ActionSet) ||
		!TestNotNull(TEXT("generated keyboard/mouse mapping context"), Context))
	{
		return false;
	}
	const FElysiumInputActionDefinition* MouseLook = ActionSet->Find(TEXT("MouseLook"));
	if (!TestTrue(TEXT("MouseLook definition exists"), MouseLook && MouseLook->Action))
	{
		return false;
	}
	TestTrue(TEXT("MouseLook is Axis2D"), MouseLook->Action->ValueType == EInputActionValueType::Axis2D);

	const TArray<FEnhancedActionKeyMapping>& Mappings = Context->GetMappings();
	TestEqual(TEXT("the keyboard/mouse slice maps only mouse look"), Mappings.Num(), 1);
	if (Mappings.Num() == 1)
	{
		const FEnhancedActionKeyMapping& Mapping = Mappings[0];
		TestEqual(TEXT("MouseLook maps the paired mouse axis"), Mapping.Key, EKeys::Mouse2D);
		TestTrue(TEXT("MouseLook mapping references IA_MouseLook"), Mapping.Action == MouseLook->Action);
		// The **absence** is the assertion. Mouse2D is a displacement the hand already made, so the
		// only multiplier between the device and the view is `sensitivity x m_yaw/m_pitch`.
		// `UInputModifierSmooth` in particular must not come back: it averages against a hardcoded
		// 0.0083s window whatever the real frame rate is, and discards its residual when input
		// returns to zero, so a flick lands short where a slow drag does not.
		TestEqual(TEXT("MouseLook carries no modifier"), Mapping.Modifiers.Num(), 0);
		for (const UInputModifier* Modifier : Mapping.Modifiers)
		{
			TestNull(TEXT("MouseLook carries no Smooth modifier"), Cast<UInputModifierSmooth>(Modifier));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGamepadInputAssetsContentTest,
	"Elysium.Content.GamepadInputAssets", GElysiumContentTestFlags)

bool FElysiumGamepadInputAssetsContentTest::RunTest(const FString&)
{
	UElysiumInputActionSet* ActionSet = LoadObject<UElysiumInputActionSet>(
		nullptr, ElysiumInputAssets::ActionSetPath);
	UInputMappingContext* Context = LoadObject<UInputMappingContext>(
		nullptr, ElysiumInputAssets::GamepadContextPath);
	if (!TestNotNull(TEXT("generated input action set"), ActionSet) ||
		!TestNotNull(TEXT("generated gamepad mapping context"), Context))
	{
		return false;
	}

	TestEqual(TEXT("the input slice defines exactly eight actions"), ActionSet->Actions.Num(), 8);
	const FElysiumInputActionDefinition* Move = ActionSet->Find(TEXT("Move"));
	const FElysiumInputActionDefinition* Look = ActionSet->Find(TEXT("Look"));
	const FElysiumInputActionDefinition* Jump = ActionSet->Find(TEXT("Jump"));
	const FElysiumInputActionDefinition* Use = ActionSet->Find(TEXT("Use"));
	const FElysiumInputActionDefinition* Feed = ActionSet->Find(TEXT("Feed"));
	const FElysiumInputActionDefinition* Duck = ActionSet->Find(TEXT("Duck"));
	const FElysiumInputActionDefinition* Camera = ActionSet->Find(TEXT("Camera"));
	if (!TestTrue(TEXT("Move definition exists"), Move && Move->Action) ||
		!TestTrue(TEXT("Look definition exists"), Look && Look->Action) ||
		!TestTrue(TEXT("Jump definition exists"), Jump && Jump->Action) ||
		!TestTrue(TEXT("Use definition exists"), Use && Use->Action) ||
		!TestTrue(TEXT("Feed definition exists"), Feed && Feed->Action) ||
		!TestTrue(TEXT("Duck definition exists"), Duck && Duck->Action) ||
		!TestTrue(TEXT("Camera definition exists"), Camera && Camera->Action))
	{
		return false;
	}
	TestTrue(TEXT("Move is Axis2D"), Move->Action->ValueType == EInputActionValueType::Axis2D);
	TestTrue(TEXT("Look is Axis2D"), Look->Action->ValueType == EInputActionValueType::Axis2D);
	TestTrue(TEXT("Jump is Boolean"), Jump->Action->ValueType == EInputActionValueType::Boolean);
	TestTrue(TEXT("Use is Boolean"), Use->Action->ValueType == EInputActionValueType::Boolean);
	TestTrue(TEXT("Feed is Boolean"), Feed->Action->ValueType == EInputActionValueType::Boolean);
	TestTrue(TEXT("Duck is Boolean"), Duck->Action->ValueType == EInputActionValueType::Boolean);
	TestTrue(TEXT("Camera is Boolean"), Camera->Action->ValueType == EInputActionValueType::Boolean);
	TestEqual(TEXT("Jump preserves its command identity"), Jump->Command, FString(TEXT("+jump")));
	TestTrue(TEXT("Jump is a press/release pair"), Jump->bButtonPair);
	TestEqual(TEXT("RB preserves the ordinary use command identity"), Use->Command, FString(TEXT("+use")));
	TestTrue(TEXT("Use is a press/release pair"), Use->bButtonPair);
	TestEqual(TEXT("Y/Triangle preserves the ordinary feed command identity"),
		Feed->Command, FString(TEXT("+feed")));
	TestTrue(TEXT("Feed keeps the low-level press/release command pair"), Feed->bButtonPair);

	// **L3 fires the ordinary `+duck` pair, and the crouch is a toggle anyway.** The retention is the
	// mover's — `IN_DUCK`'s press edge flips `bDuckRequested` — so there is no gamepad-only crouch
	// verb to keep in step with the keyboard's, and a stick click that cannot be held costs nothing.
	// The pair is what makes the press edge exist at all, which is the thing the toggle reads.
	TestEqual(TEXT("L3 fires the ordinary crouch verb"), Duck->Command, FString(TEXT("+duck")));
	TestTrue(TEXT("crouch is a press/release pair like the keyboard's"), Duck->bButtonPair);
	// R3 is a genuine one-shot: `togglecamera` has no release half, so binding a Completed edge
	// would fire `-togglecamera`, which is not a verb.
	TestEqual(TEXT("R3 fires the view toggle"), Camera->Command, FString(TEXT("togglecamera")));
	TestFalse(TEXT("view toggle is not a press/release pair"), Camera->bButtonPair);
	// Every mapped command names a declared verb, or the button is a no-op that logs nothing. The
	// leading `+` is stripped first, because a pair's press edge is declared under its bare name.
	for (const FElysiumInputActionDefinition* Definition : { Jump, Use, Feed, Duck, Camera })
	{
		const FString Bare = Definition->Command.StartsWith(TEXT("+"))
			? Definition->Command.Mid(1) : Definition->Command;
		TestTrue(FString::Printf(TEXT("'%s' is a declared verb"), *Definition->Command),
			FElysiumCommands::Get().IsDeclared(ElysiumCommands::Canonical(Bare)));
	}

	const TArray<FEnhancedActionKeyMapping>& Mappings = Context->GetMappings();
	TestEqual(TEXT("seven actions are gameplay-mapped"), Mappings.Num(), 7);
	auto FindMapping = [&Mappings](const UInputAction* Action) -> const FEnhancedActionKeyMapping*
	{
		return Mappings.FindByPredicate(
			[Action](const FEnhancedActionKeyMapping& Mapping) { return Mapping.Action == Action; });
	};
	const FEnhancedActionKeyMapping* MoveMapping = FindMapping(Move->Action);
	const FEnhancedActionKeyMapping* LookMapping = FindMapping(Look->Action);
	const FEnhancedActionKeyMapping* JumpMapping = FindMapping(Jump->Action);
	const FEnhancedActionKeyMapping* UseMapping = FindMapping(Use->Action);
	const FEnhancedActionKeyMapping* FeedMapping = FindMapping(Feed->Action);
	const FEnhancedActionKeyMapping* DuckMapping = FindMapping(Duck->Action);
	const FEnhancedActionKeyMapping* CameraMapping = FindMapping(Camera->Action);
	if (!TestNotNull(TEXT("Move mapping"), MoveMapping) ||
		!TestNotNull(TEXT("Look mapping"), LookMapping) ||
		!TestNotNull(TEXT("Jump mapping"), JumpMapping) ||
		!TestNotNull(TEXT("Use mapping"), UseMapping) ||
		!TestNotNull(TEXT("Feed mapping"), FeedMapping) ||
		!TestNotNull(TEXT("Duck mapping"), DuckMapping) ||
		!TestNotNull(TEXT("Camera mapping"), CameraMapping))
	{
		return false;
	}
	TestEqual(TEXT("Move uses the left stick"), MoveMapping->Key, EKeys::Gamepad_Left2D);
	TestEqual(TEXT("Look uses the right stick"), LookMapping->Key, EKeys::Gamepad_Right2D);
	TestEqual(TEXT("Jump uses A/Cross"), JumpMapping->Key, EKeys::Gamepad_FaceButton_Bottom);
	TestEqual(TEXT("Use is on RB"), UseMapping->Key, EKeys::Gamepad_RightShoulder);
	TestEqual(TEXT("Feed is on Y/Triangle"), FeedMapping->Key, EKeys::Gamepad_FaceButton_Top);
	// L3 and R3 are the stick *clicks*, and each sits on the stick whose job it serves: crouch is a
	// movement verb on the movement stick, the view toggle is a camera verb on the camera stick.
	TestEqual(TEXT("crouch is on L3"), DuckMapping->Key, EKeys::Gamepad_LeftThumbstick);
	TestEqual(TEXT("the view toggle is on R3"), CameraMapping->Key, EKeys::Gamepad_RightThumbstick);
	TestEqual(TEXT("L3 carries no modifier stack"), DuckMapping->Modifiers.Num(), 0);
	TestEqual(TEXT("R3 carries no modifier stack"), CameraMapping->Modifiers.Num(), 0);
	// **The mapping carries device-frame corrections only.** Every feel term — dead zone,
	// saturation, response curve, rate, filter, turn ramp — is `ElysiumInput::ShapeStickLook` /
	// `ShapeStickMove` at the command seam (`Elysium.Substrate.StickLook`), because the filter is a
	// half-life and the ramp is a charge and both need the frame's *clamped, dilated* delta, which
	// no Enhanced Input modifier ever sees. So the modifiers that are NOT here are asserted at
	// least as firmly as the one that is: a dead zone reappearing in the asset is a second owner of
	// the same number, and only one of the two can be measured.
	TestEqual(TEXT("Move carries no modifier stack at all"), MoveMapping->Modifiers.Num(), 0);
	TestEqual(TEXT("Look carries only its device-frame correction"), LookMapping->Modifiers.Num(), 1);
	TestEqual(TEXT("Jump has no modifier stack"), JumpMapping->Modifiers.Num(), 0);

	for (const FEnhancedActionKeyMapping* Mapping : { MoveMapping, LookMapping })
	{
		for (const TObjectPtr<UInputModifier>& Modifier : Mapping->Modifiers)
		{
			TestFalse(TEXT("the dead zone is not a modifier asset"),
				Modifier != nullptr && Modifier->IsA<UInputModifierDeadZone>());
			TestFalse(TEXT("the rate is not a modifier asset"),
				Modifier != nullptr && Modifier->IsA<UInputModifierScalar>());
			TestFalse(TEXT("the stick is not delta-time scaled in the asset"),
				Modifier != nullptr && Modifier->IsA<UInputModifierScaleByDeltaTime>());
			TestFalse(TEXT("the stick is not FOV scaled"),
				Modifier != nullptr && Modifier->IsA<UInputModifierFOVScaling>());
			TestFalse(TEXT("the response curve is not a modifier asset"),
				Modifier != nullptr && Modifier->IsA<UInputModifierResponseCurveExponential>());
			TestFalse(TEXT("the look filter is not a modifier asset"),
				Modifier != nullptr && Modifier->IsA<UInputModifierSmooth>());
		}
	}

	if (LookMapping->Modifiers.Num() == 1)
	{
		const UInputModifierNegate* NativeY = Cast<UInputModifierNegate>(LookMapping->Modifiers[0]);
		TestNotNull(TEXT("native right-stick Y correction"), NativeY);
		if (NativeY)
		{
			TestFalse(TEXT("native Y correction preserves yaw"), NativeY->bX);
			TestTrue(TEXT("native Y correction inverts pitch"), NativeY->bY);
			TestFalse(TEXT("native Y correction preserves Z"), NativeY->bZ);
			const FVector2D PhysicalUp = NativeY->ModifyRaw(
				nullptr, FInputActionValue(FVector2D(0.0, -1.0)), 0.0f).Get<FVector2D>();
			const FVector2D PhysicalDown = NativeY->ModifyRaw(
				nullptr, FInputActionValue(FVector2D(0.0, 1.0)), 0.0f).Get<FVector2D>();
			TestEqual(TEXT("right-stick up becomes positive look pitch"),
				(float)PhysicalUp.Y, 1.0f);
			TestEqual(TEXT("right-stick down becomes negative look pitch"),
				(float)PhysicalDown.Y, -1.0f);
		}
	}

	for (const TCHAR* Name : {
		ElysiumInputAssets::DualSenseCreateKey,
		ElysiumInputAssets::DualSensePSKey,
		ElysiumInputAssets::DualSenseMuteKey })
	{
		const TSharedPtr<FKeyDetails> Details = EKeys::GetKeyDetails(FKey(Name));
		TestTrue(FString::Printf(TEXT("%s is registered as a gamepad key"), Name),
			Details.IsValid() && Details->IsGamepadKey());
	}

#if PLATFORM_WINDOWS && GAME_INPUT_SUPPORT
	bool bPreferredAPIEnabled = false;
	FString PreferredAPIs;
	TestTrue(TEXT("preferred controller API setting is present"), GConfig->GetBool(
		TEXT("/Script/Engine.InputSettings"), TEXT("bEnablePreferredInputAPIPreferences"),
		bPreferredAPIEnabled, GInputIni));
	TestTrue(TEXT("preferred controller API selection is enabled"), bPreferredAPIEnabled);
	TestTrue(TEXT("preferred controller API list is present"), GConfig->GetString(
		TEXT("/Script/Engine.InputSettings"), TEXT("DefaultPreferredInputAPIList"),
		PreferredAPIs, GInputIni));
	TestEqual(TEXT("GameInput is the only preferred controller API"),
		PreferredAPIs, FString(TEXT("GameInput")));

	const UGameInputPlatformSettings* Platform = UGameInputPlatformSettings::Get();
	TestNotNull(TEXT("Windows GameInput platform settings"), Platform);
	if (Platform)
	{
		TestTrue(TEXT("Xbox Gamepad capability is enabled"), Platform->bProcessGamepad);
		TestTrue(TEXT("configured HID Controller capability is enabled"), Platform->bProcessController);
		TestFalse(TEXT("GameInput does not duplicate keyboard"), Platform->bProcessKeyboard);
		TestFalse(TEXT("GameInput does not duplicate mouse"), Platform->bProcessMouse);
		TestFalse(TEXT("raw reports remain outside this slice"), Platform->bProcessRawInput);
		TestTrue(TEXT("unconfigured HID controllers are rejected"),
			Platform->bSpecialDevicesRequireExplicitDeviceConfiguration);
	}
	bool bProcessSensors = true;
	TestTrue(TEXT("GameInput sensor setting is present"), GConfig->GetBool(
		TEXT("GameInputPlatformSettings_Windows GameInputPlatformSettings"),
		TEXT("bProcessSensors"), bProcessSensors, GInputIni));
	TestFalse(TEXT("sensors remain outside this slice"), bProcessSensors);

	const FGameInputDeviceConfiguration* DualSense =
		GetDefault<UGameInputDeveloperSettings>()->FindDeviceConfiguration(
			FGameInputDeviceIdentifier(0x054c, 0x0ce6));
	if (!TestNotNull(TEXT("DualSense 054C:0CE6 configuration"), DualSense))
	{
		return false;
	}
	TestEqual(TEXT("DualSense hardware id"), DualSense->OverriddenHardwareDeviceId,
		FString(TEXT("DualSense")));
	TestTrue(TEXT("DualSense hardware id override is active"),
		DualSense->bOverrideHardwareDeviceIdString);
	TestTrue(TEXT("DualSense extra buttons are processed"), DualSense->bProcessControllerButtons);
	TestFalse(TEXT("DualSense D-pad has only the native Gamepad publisher"),
		DualSense->bProcessControllerSwitchState);
	TestFalse(TEXT("DualSense axes have only the native Gamepad publisher"),
		DualSense->bProcessControllerAxis);
	TestFalse(TEXT("DualSense raw reports remain outside this slice"),
		DualSense->bProcessRawReportData);
	TestEqual(TEXT("only four DualSense-only buttons use the Controller processor"),
		DualSense->ControllerButtonMappingData.Num(), 4);
	TestEqual(TEXT("the Controller processor republishes no DualSense axes"),
		DualSense->ControllerAxisMappingData.Num(), 0);

	static const TPair<uint32, const TCHAR*> ExpectedButtons[] = {
		{256, ElysiumInputAssets::DualSenseCreateKey},
		{4096, ElysiumInputAssets::DualSensePSKey}, {8192, TEXT("Gamepad_Special_Left")},
		{16384, ElysiumInputAssets::DualSenseMuteKey},
	};
	for (const TPair<uint32, const TCHAR*>& Expected : ExpectedButtons)
	{
		const FName* Actual = DualSense->ControllerButtonMappingData.Find(Expected.Key);
		TestTrue(FString::Printf(TEXT("DualSense button mask %u is configured"), Expected.Key),
			Actual && *Actual == FName(Expected.Value));
	}

	static const uint32 NativeGamepadButtonMasks[] = {
		1, 2, 4, 8, 16, 32, 64, 128, 512, 1024, 2048,
	};
	for (const uint32 Mask : NativeGamepadButtonMasks)
	{
		TestFalse(FString::Printf(TEXT("native Gamepad button mask %u is not republished"), Mask),
			DualSense->ControllerButtonMappingData.Contains(Mask));
	}
#endif

	return true;
}

// =====================================================================================
// The script/entity action surface — every name the shipped content calls has to resolve.
//
// `docs/vtmb/script_api.md` is the recovered inventory: the `vampire` module's 11 globals, the 24
// Character methods, and the datamap inputs on the character chain, each with its owning class and
// the record's fieldType. This test asserts the runtime *offers* every one of those names — backed
// for real or registered pending — so a gap is a build failure here rather than a
// `[no input]` line discovered while playing. It asserts nothing about behaviour: a pending input
// logs and no-ops, and that counts as resolved.
//
// **Source of truth: the transcribed manifest below, not the survey JSON.** The survey
// (`uv run elysium research script_api_survey`) classifies a name as `entity-input` by scanning
// this repository's own `D.Input(TEXT("..."))` calls, so its verdict on inputs is circular, and it
// records no owning classname for them. Its module-global and Character-method tables are
// independent of our source, so those halves are cross-checked against it when the export carries
// it. The manifest is committed reverse-engineering fact about VtMB's binding tables, not game
// data, which is why it can live in the checkout at all.
// =====================================================================================

namespace
{
	// One datamap input, and the classname whose chain has to resolve it. `Type` is the record's
	// fieldType (VtMB's shifted enum) — the marshalling contract, reported so a failure line says
	// what the missing registration owes.
	struct FElysiumApiInput
	{
		const TCHAR* ClassName;
		const TCHAR* Input;
		const TCHAR* Type;
	};

	// Module globals — table 0x1058f7a8, 11 entries.
	const TCHAR* const GScriptApiGlobals[] = {
		TEXT("FindEntityByName"), TEXT("FindPlayer"), TEXT("OneOfSet"), TEXT("ScheduleTask"),
		TEXT("FindEntitiesByName"), TEXT("ChangeMap"), TEXT("FindEntitiesByClass"),
		TEXT("CreateEntityNoSpawn"), TEXT("CallEntitySpawn"), TEXT("IsPCMalk"),
		TEXT("SquadSeesPlayer"),
	};

	// Character methods — table 0x1058f868, 24 entries.
	const TCHAR* const GScriptApiCharacterMethods[] = {
		TEXT("SetDisposition"), TEXT("SetQuest"), TEXT("GetQuestState"), TEXT("HasItem"),
		TEXT("IsMale"), TEXT("RemoveItem"), TEXT("GiveItem"), TEXT("SetCamera"),
		TEXT("StartBarter"), TEXT("CurrentMoney"), TEXT("SeductiveFeed"), TEXT("CalcFeat"),
		TEXT("HasWeaponEquipped"), TEXT("AmmoCount"), TEXT("GiveAmmo"), TEXT("BumpStat"),
		TEXT("WorldMap"), TEXT("IsFollowerOf"), TEXT("SewerMap"), TEXT("SetGesture"),
		TEXT("GetMasqueradeLevel"), TEXT("DialogDiscipline"), TEXT("SetExpression"),
		TEXT("React"),
	};

	// The receiver-split pair. `vamputil.py` defines both as script helpers AND both are datamap
	// input names, so the binding is a property of the call site rather than of the name: a bare
	// `Whisper(...)` must reach the script's own function while `pc.Whisper(...)` reaches the input.
	// A row in the shared native table would collapse that split, so their absence from it is the
	// assertion. (`Elysium.Substrate.OneOfSet` guards the same rule from the other side.)
	const TCHAR* const GScriptApiReceiverSplit[] = { TEXT("Whisper"), TEXT("FrenzyTrigger") };

	const FElysiumApiInput GScriptApiInputs[] = {
		// CBaseCombatCharacter — datamap 0x1061664c, 25 inputs.
		{ TEXT("CBaseCombatCharacter"), TEXT("MoneyAdd"),               TEXT("INTEGER") },
		{ TEXT("CBaseCombatCharacter"), TEXT("MoneyRemove"),            TEXT("INTEGER") },
		{ TEXT("CBaseCombatCharacter"), TEXT("WillTalk"),               TEXT("INTEGER") },
		{ TEXT("CBaseCombatCharacter"), TEXT("HumanityAdd"),            TEXT("INTEGER") },
		{ TEXT("CBaseCombatCharacter"), TEXT("ChangeMasqueradeLevel"),  TEXT("INTEGER") },
		{ TEXT("CBaseCombatCharacter"), TEXT("Bloodloss"),              TEXT("INTEGER") },
		{ TEXT("CBaseCombatCharacter"), TEXT("Bloodgain"),              TEXT("INTEGER") },
		{ TEXT("CBaseCombatCharacter"), TEXT("BloodHeal"),              TEXT("INTEGER") },
		{ TEXT("CBaseCombatCharacter"), TEXT("FrenzyTrigger"),          TEXT("VOID") },
		{ TEXT("CBaseCombatCharacter"), TEXT("FrenzyCheck"),            TEXT("INTEGER") },
		{ TEXT("CBaseCombatCharacter"), TEXT("HungerCheck"),            TEXT("INTEGER") },
		{ TEXT("CBaseCombatCharacter"), TEXT("FrenzyUpdate"),           TEXT("INTEGER") },
		{ TEXT("CBaseCombatCharacter"), TEXT("ClearActiveDisciplines"), TEXT("VOID") },
		{ TEXT("CBaseCombatCharacter"), TEXT("Inventory_Remove"),       TEXT("CLASSPTR") },
		{ TEXT("CBaseCombatCharacter"), TEXT("BarterBegin"),            TEXT("VOID") },
		{ TEXT("CBaseCombatCharacter"), TEXT("BarterEnd"),              TEXT("VOID") },
		{ TEXT("CBaseCombatCharacter"), TEXT("PlayFloat"),              TEXT("VOID") },
		{ TEXT("CBaseCombatCharacter"), TEXT("SetHeadAsCameraTarget"),  TEXT("VOID") },
		{ TEXT("CBaseCombatCharacter"), TEXT("SetBodyAsCameraTarget"),  TEXT("VOID") },
		{ TEXT("CBaseCombatCharacter"), TEXT("FadeHeadAsCameraTarget"), TEXT("FLOAT") },
		{ TEXT("CBaseCombatCharacter"), TEXT("FadeBodyAsCameraTarget"), TEXT("FLOAT") },
		{ TEXT("CBaseCombatCharacter"), TEXT("LookAtEntityEye"),        TEXT("STRING") },
		{ TEXT("CBaseCombatCharacter"), TEXT("LookAtEntityCenter"),     TEXT("STRING") },
		{ TEXT("CBaseCombatCharacter"), TEXT("LookAtEntityOrigin"),     TEXT("STRING") },
		{ TEXT("CBaseCombatCharacter"), TEXT("LookAtEntityDefault"),    TEXT("VOID") },

		// The player class — datamap 0x10580edc. Its header states 11 inputs; 10 were recovered
		// from the builder dump and the eleventh is still unidentified, so it cannot be listed.
		{ TEXT("player"), TEXT("GiveItem"),             TEXT("STRING") },
		{ TEXT("player"), TEXT("AwardExperience"),      TEXT("STRING") },
		{ TEXT("player"), TEXT("Whisper"),              TEXT("STRING") },
		{ TEXT("player"), TEXT("SetCriminalLevel"),     TEXT("INTEGER") },
		{ TEXT("player"), TEXT("RemoveCamera"),         TEXT("VOID") },
		{ TEXT("player"), TEXT("PlayHUDParticle"),      TEXT("STRING") },
		{ TEXT("player"), TEXT("StopHUDParticle"),      TEXT("FLOAT") },
		{ TEXT("player"), TEXT("SetInvestigateLevel"),  TEXT("INTEGER") },
		{ TEXT("player"), TEXT("SetSupernaturalLevel"), TEXT("INTEGER") },
		{ TEXT("player"), TEXT("Holster"),              TEXT("VOID") },
		// And the chain: everything CBaseCombatCharacter declares reaches the player unshadowed.
		{ TEXT("player"), TEXT("MoneyAdd"),             TEXT("INTEGER") },
		{ TEXT("player"), TEXT("FrenzyTrigger"),        TEXT("VOID") },

		// CAI_BaseNPC — datamap 0x105c9814. `SetRelationship` is the recovered scripted input; the
		// other three are map-fired at `npc_*` receivers across the exported maps
		// (`docs/vtmb/sp_tutorial_1-event-surface.md` §7) with no datamap record recovered yet.
		{ TEXT("npc_VVampire"),        TEXT("SetRelationship"),        TEXT("STRING") },
		{ TEXT("npc_VVampire"),        TEXT("TeleportToEntity"),       TEXT("(unrecovered)") },
		{ TEXT("npc_VVampire"),        TEXT("SetScriptedDiscipline"),  TEXT("(unrecovered)") },
		{ TEXT("npc_VVampire"),        TEXT("TakeDamage"),             TEXT("(unrecovered)") },
		{ TEXT("npc_VHumanCombatant"), TEXT("SetRelationship"),        TEXT("STRING") },
		{ TEXT("npc_VHumanCombatant"), TEXT("TeleportToEntity"),       TEXT("(unrecovered)") },
		// And the chain again, from the other leaf of it.
		{ TEXT("npc_VVampire"),        TEXT("WillTalk"),               TEXT("INTEGER") },
		{ TEXT("npc_VVampire"),        TEXT("SetBodyAsCameraTarget"),  TEXT("VOID") },
	};

	// Report a group of missing names as one work-list line rather than N unordered errors.
	void ReportMissing(FAutomationTestBase& Test, const TCHAR* Kind, const TArray<FString>& Missing)
	{
		if (Missing.IsEmpty())
		{
			return;
		}
		Test.AddError(FString::Printf(TEXT("%d %s do not resolve: %s"),
			Missing.Num(), Kind, *FString::Join(Missing, TEXT(", "))));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScriptApiCoverageTest,
	"Elysium.Content.ScriptApiCoverage", GElysiumContentTestFlags)
bool FElysiumScriptApiCoverageTest::RunTest(const FString&)
{
	// --- The module globals and the Character methods, against the one shared native table ------
	TArray<FString> MissingGlobals;
	for (const TCHAR* Name : GScriptApiGlobals)
	{
		if (!ElysiumScriptNatives::IsNativeGlobal(FString(Name)))
		{
			MissingGlobals.Add(Name);
		}
	}
	ReportMissing(*this, TEXT("module globals"), MissingGlobals);

	TArray<FString> MissingMethods;
	for (const TCHAR* Name : GScriptApiCharacterMethods)
	{
		if (!ElysiumScriptNatives::IsCharacterMethod(FString(Name)))
		{
			MissingMethods.Add(Name);
		}
	}
	ReportMissing(*this, TEXT("Character methods"), MissingMethods);

	for (const TCHAR* Name : GScriptApiReceiverSplit)
	{
		const FString AsString(Name);
		TestFalse(FString::Printf(
			TEXT("%s stays out of the native table (the receiver decides its binding)"), Name),
			ElysiumScriptNatives::IsNativeGlobal(AsString)
				|| ElysiumScriptNatives::IsCharacterMethod(AsString));
	}

	// --- The datamap inputs, through the registry's own chain walk ------------------------------
	const FElysiumClassRegistry& Registry = FElysiumClassRegistry::Get();
	TArray<FString> MissingInputs;
	TSet<FString> MissingClasses;
	for (const FElysiumApiInput& Row : GScriptApiInputs)
	{
		const FElysiumClassDesc* Desc = Registry.Find(FName(Row.ClassName));
		if (Desc == nullptr)
		{
			MissingClasses.Add(Row.ClassName);
			continue;
		}
		if (Registry.FindInput(*Desc, FName(Row.Input)) == nullptr)
		{
			MissingInputs.Add(FString::Printf(TEXT("%s.%s [%s]"),
				Row.ClassName, Row.Input, Row.Type));
		}
	}
	if (!MissingClasses.IsEmpty())
	{
		ReportMissing(*this, TEXT("owning classnames are unregistered"), MissingClasses.Array());
	}
	ReportMissing(*this, TEXT("datamap inputs"), MissingInputs);

	AddInfo(FString::Printf(
		TEXT("script API surface: %d module globals, %d Character methods, %d datamap inputs asserted"),
		UE_ARRAY_COUNT(GScriptApiGlobals), UE_ARRAY_COUNT(GScriptApiCharacterMethods),
		UE_ARRAY_COUNT(GScriptApiInputs)));

	// --- The corpus survey, when the export carries it -------------------------------------------
	// Cross-check only the two halves the survey derives independently of this repository. Its
	// `entity-input` verdict is read back out of our own registrations, so it can neither confirm
	// nor deny the manifest above.
	const FString SurveyPath = FElysiumContentPaths::Root() / TEXT("_mcp/script_api.json");
	FString SurveyJson;
	if (!FElysiumContentPaths::IsConfigured() || !FFileHelper::LoadFileToString(SurveyJson, *SurveyPath))
	{
		// Not an abstention: the manifest above is the assertion and it ran. Only the optional
		// cross-check is skipped, so the test stays counted as executed.
		AddInfo(FString::Printf(
			TEXT("no corpus survey at %s — the manifest ran without it; regenerate the survey with ")
			TEXT("`uv run elysium research script_api_survey --json $ELYSIUM_EXPORT_ROOT/_mcp/script_api.json`"),
			*SurveyPath));
		return true;
	}

	TSharedPtr<FJsonObject> Survey;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SurveyJson);
	if (!TestTrue(TEXT("the corpus survey parses"), FJsonSerializer::Deserialize(Reader, Survey)
		&& Survey.IsValid()))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Ledger = nullptr;
	if (!TestTrue(TEXT("the corpus survey carries a ledger"), Survey->TryGetArrayField(TEXT("ledger"), Ledger)
		&& Ledger != nullptr))
	{
		return false;
	}

	TArray<FString> SurveyMissing;
	int32 CalledGlobals = 0;
	int32 CalledMethods = 0;
	int32 CalledInputs = 0;
	for (const TSharedPtr<FJsonValue>& Value : *Ledger)
	{
		const TSharedPtr<FJsonObject> Row = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!Row.IsValid())
		{
			continue;
		}
		const FString Name = Row->GetStringField(TEXT("name"));
		const FString Kind = Row->GetStringField(TEXT("kind"));
		if (Kind == TEXT("engine-global"))
		{
			++CalledGlobals;
			if (!ElysiumScriptNatives::IsNativeGlobal(Name)) { SurveyMissing.Add(Name + TEXT(" (global)")); }
		}
		else if (Kind == TEXT("character"))
		{
			++CalledMethods;
			if (!ElysiumScriptNatives::IsCharacterMethod(Name)) { SurveyMissing.Add(Name + TEXT(" (method)")); }
		}
		else if (Kind == TEXT("entity-input"))
		{
			++CalledInputs;
		}
		// script / stdlib / entity-base / G-method / UNRESOLVED are not this runtime's obligation:
		// the corpus defines them, CPython owns them, or nothing in the recovered tables explains
		// them yet.
	}
	ReportMissing(*this, TEXT("corpus-called engine names"), SurveyMissing);
	AddInfo(FString::Printf(
		TEXT("corpus survey: %d ledger rows — %d module globals, %d Character methods, %d entity inputs called"),
		Ledger->Num(), CalledGlobals, CalledMethods, CalledInputs));
	return true;
}


// =====================================================================================
// `vdata/items` (9.8a) — the item catalogue against the real exported corpus.
//
// The recovered figures in `docs/vtmb/inventory.md` §4 are PATCH-FIRST facts about the
// content this project consumes, not stock-retail authoring facts, so a local corpus that
// differs is reported rather than failed: each block asserts structural validity first and
// reports the actuals through AddInfo when they disagree with the recorded numbers.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumItemsContentTest,
	"Elysium.Content.Items", GElysiumContentTestFlags)
bool FElysiumItemsContentTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("vdata") })) return true;
	if (!IFileManager::Get().FileExists(*FElysiumContentPaths::VdataFile(TEXT("system/items.txt"))))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no exported vdata (run: uv run elysium export bundle vdata)"));
		return true;
	}

	FString Error;
	FElysiumItemTable Items;
	if (!TestTrue(TEXT("vdata/items parses"), Items.Load(Error)))
	{
		AddError(Error);
		return true;
	}

	// --- The eleven semantic types, and the compile-time mirror of them --------------------
	// `system/items.txt`'s ItemTypes block is the vocabulary `EElysiumItemType` mirrors; the file
	// order is the engine's enum, so a reordering there silently reinterprets every item.
	{
		FString Raw;
		TSharedPtr<ElysiumKeyValues::FKvNode> Root;
		if (FFileHelper::LoadFileToString(Raw, *FElysiumContentPaths::VdataFile(TEXT("system/items.txt"))))
		{
			Root = ElysiumKeyValues::ParseText(Raw);
		}
		const ElysiumKeyValues::FKvNode* Data = Root.IsValid() ? Root->Child(TEXT("ItemTypeData")) : nullptr;
		const ElysiumKeyValues::FKvNode* Types = Data ? Data->Child(TEXT("ItemTypes")) : nullptr;
		if (TestNotNull(TEXT("system/items.txt carries an ItemTypes block"), Types))
		{
			TArray<FString> Declared;
			for (const TPair<FString, TSharedPtr<ElysiumKeyValues::FKvNode>>& Kid : Types->Kids)
			{
				if (Kid.Key == TEXT("itemtype") && Kid.Value.IsValid())
				{
					Declared.Add(Kid.Value->Str(TEXT("InternalName"), FString()));
				}
			}
			TestEqual(TEXT("it declares eleven item types"), Declared.Num(),
				(int32)EElysiumItemType::Count);
			for (int32 i = 0; i < Declared.Num() && i < (int32)EElysiumItemType::Count; ++i)
			{
				TestEqual(*FString::Printf(TEXT("type %d is %s"), i,
					ElysiumItemTypeName((EElysiumItemType)i)),
					Declared[i], FString(ElysiumItemTypeName((EElysiumItemType)i)));
			}
		}
		// The eight authored inventory sections: hidden `None`, six displayed, hidden `Hidden`.
		if (const ElysiumKeyValues::FKvNode* Sections = Data ? Data->Child(TEXT("InventorySections")) : nullptr)
		{
			int32 Total = 0;
			int32 Displayed = 0;
			for (const TPair<FString, TSharedPtr<ElysiumKeyValues::FKvNode>>& Kid : Sections->Kids)
			{
				if (Kid.Key == TEXT("inventorysection") && Kid.Value.IsValid())
				{
					++Total;
					Displayed += Kid.Value->Int(TEXT("IsDisplayed"), 0) != 0 ? 1 : 0;
				}
			}
			TestEqual(TEXT("eight inventory sections are authored"), Total, 8);
			TestEqual(TEXT("...six of them displayed"), Displayed, 6);
		}
	}

	// --- The catalogue's shape -------------------------------------------------------------
	// 244 definitions, and the type histogram, are the recorded patch-first figures.
	constexpr int32 RecordedCount = 244;
	if (Items.Num() != RecordedCount)
	{
		AddInfo(FString::Printf(
			TEXT("vdata/items holds %d definitions; inventory.md records %d for the patch-first corpus"),
			Items.Num(), RecordedCount));
	}
	TestTrue(TEXT("the catalogue is not empty"), Items.Num() > 0);

	// The recorded histogram, visible + hidden per type (`inventory.md` §4).
	struct FTypeExpectation { EElysiumItemType Type; int32 Total; };
	const FTypeExpectation Recorded[] = {
		{ EElysiumItemType::Generic,       106 },
		{ EElysiumItemType::Powerup,        63 },
		{ EElysiumItemType::WeaponFirearm,  17 },   // 16 visible + 1 hidden
		{ EElysiumItemType::WeaponMelee,    18 },   // 15 visible + 3 hidden
		{ EElysiumItemType::Armor,          13 },
		{ EElysiumItemType::Hidden,         19 },   // 10 unmarked + 9 explicitly hidden
		{ EElysiumItemType::WeaponThrown,    5 },
		{ EElysiumItemType::Bloodpack,       3 },
	};
	int32 Histogrammed = 0;
	for (const FTypeExpectation& Row : Recorded)
	{
		const int32 Actual = Items.CountOfType(Row.Type);
		Histogrammed += Actual;
		if (Actual != Row.Total)
		{
			AddInfo(FString::Printf(TEXT("item type %s: %d files (inventory.md records %d)"),
				ElysiumItemTypeName(Row.Type), Actual, Row.Total));
		}
		// Structural, whatever the count: every recorded type is represented.
		TestTrue(*FString::Printf(TEXT("%s items exist"), ElysiumItemTypeName(Row.Type)), Actual > 0);
	}
	TestEqual(TEXT("every definition resolved to one of the recorded types"),
		Histogrammed, Items.Num());
	// The two hidden-marked families are what makes `item_type` a SET rather than one word.
	TestTrue(TEXT("some definitions carry the trailing `hidden` flag beside their type"),
		Items.Items.ContainsByPredicate([](const FElysiumItemDef& D) { return D.bHidden; }));

	// --- Policy: three distinct keys --------------------------------------------------------
	int32 Stackable = 0;
	int32 NonDroppable = 0;
	int32 Permanent = 0;
	int32 WithMagazine = 0;
	for (const FElysiumItemDef& Def : Items.Items)
	{
		Stackable += Def.bStackable ? 1 : 0;
		NonDroppable += Def.bDroppable ? 0 : 1;
		Permanent += Def.bPermanentInventory ? 1 : 0;
		WithMagazine += Def.AmmoType.IsEmpty() ? 0 : 1;
		TestFalse(TEXT("every definition names its classname"), Def.Classname.IsEmpty());
	}
	const int32 RecordedStackable = 22;
	const int32 RecordedNonDroppable = 39;
	const int32 RecordedPermanent = 29;
	if (Stackable != RecordedStackable || NonDroppable != RecordedNonDroppable
		|| Permanent != RecordedPermanent)
	{
		AddInfo(FString::Printf(
			TEXT("policy keys: is_stackable=1 on %d (recorded %d), is_droppable=0 on %d (recorded %d), ")
			TEXT("permanent_inventory=1 on %d (recorded %d)"),
			Stackable, RecordedStackable, NonDroppable, RecordedNonDroppable,
			Permanent, RecordedPermanent));
	}
	// The three keys are distinct, which is the fact the runtime depends on: no two of the sets
	// coincide, so none of them can be derived from another.
	TestTrue(TEXT("is_stackable, is_droppable and permanent_inventory are three different sets"),
		Stackable != NonDroppable && NonDroppable != Permanent && Stackable != Permanent);
	TestTrue(TEXT("some items carry a magazine"), WithMagazine > 0);

	// --- The keyring is an ordinary carried item with its own policy -------------------------
	const FElysiumItemDef* Keyring = Items.Find(TEXT("item_g_keyring"));
	if (TestNotNull(TEXT("item_g_keyring has an item definition"), Keyring))
	{
		TestFalse(TEXT("...and is not droppable"), Keyring->bDroppable);
		TestTrue(TEXT("...and is permanent inventory"), Keyring->bPermanentInventory);
		TestEqual(TEXT("...found case-insensitively"),
			Items.Find(TEXT("ITEM_G_KeyRing")), Keyring);
	}

	// --- The tutorial's `.38`: the values its ammo beat rides on ------------------------------
	if (const FElysiumItemDef* Gun = Items.Find(TEXT("item_w_thirtyeight")))
	{
		TestEqual(TEXT("the .38 is a firearm"), (int32)Gun->Type, (int32)EElysiumItemType::WeaponFirearm);
		TestEqual(TEXT("...naming ammo type ThirtyeightRound"), Gun->AmmoType,
			FString(TEXT("ThirtyeightRound")));
		TestEqual(TEXT("...with a magazine of six"), Gun->MagazineSize, 6);
		TestEqual(TEXT("...a default load of six"), Gun->DefaultAmmo, 6);
		TestFalse(TEXT("...and it does not stack"), Gun->bStackable);
	}
	else
	{
		AddInfo(TEXT("item_w_thirtyeight is absent from this corpus"));
	}

	// The patch-authored stackable the doc warns not to read as stock behaviour.
	if (const FElysiumItemDef* TireIron = Items.Find(TEXT("item_w_tire_iron")))
	{
		if (!TireIron->bStackable || TireIron->StackLimit != 10)
		{
			AddInfo(FString::Printf(
				TEXT("item_w_tire_iron: stackable=%d limit=%d (the patch-first corpus records 1 / 10)"),
				TireIron->bStackable ? 1 : 0, TireIron->StackLimit));
		}
	}

	// --- Every item classname a map places has a definition ----------------------------------
	// The three `item_container*` classnames are containers, not items, and deliberately have none.
	{
		TArray<FString> EntsFiles;
		IFileManager::Get().FindFilesRecursive(EntsFiles, *FElysiumContentPaths::Root(), TEXT("*.ents"),
			/*Files*/ true, /*Dirs*/ false);
		TArray<FString> Missing;
		int32 Placed = 0;
		for (const FString& EntsPath : EntsFiles)
		{
			FElysiumEntityDefs MapDefs;
			if (!FElysiumEntityDefs::Parse(EntsPath, MapDefs))
			{
				continue;
			}
			for (const FElysiumEntityDef& Def : MapDefs.Defs)
			{
				if (!Def.Classname.StartsWith(TEXT("item_"), ESearchCase::IgnoreCase)
					|| Def.Classname.StartsWith(TEXT("item_container"), ESearchCase::IgnoreCase))
				{
					continue;
				}
				++Placed;
				if (Items.Find(Def.Classname) == nullptr)
				{
					Missing.AddUnique(Def.Classname);
				}
			}
		}
		AddInfo(FString::Printf(TEXT("the exported maps place %d item entities"), Placed));
		ReportMissing(*this, TEXT("placed item classnames with no vdata/items record"), Missing);
	}

	AddInfo(FString::Printf(TEXT("vdata/items: %d definitions, %d with a magazine"),
		Items.Num(), WithMagazine));
	return true;
}


// =====================================================================================
// Item ground models — the shared corpus a loose item's body resolves against.
//
// A placed `item_*` states no `model` key, so the map prop pass never saw these meshes;
// UE_extract_items.py decodes them from the same `vdata/items` table the runtime loads, and the
// item bake scope stands them on /ElysiumBaked/items. This asserts the two halves agree: that the
// stem the runtime derives from a `playermodel` is the stem the offline decode wrote, and that
// every model carrying geometry has a baked mesh to load.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumItemGroundModelsContentTest,
	"Elysium.Content.ItemGroundModels", GElysiumContentTestFlags)
bool FElysiumItemGroundModelsContentTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("vdata"), TEXT("items") })) return true;

	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *FElysiumContentPaths::ItemGroundModels()))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: no item ground models "
			"(run: uv run elysium export bundle items)"));
		return true;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!TestTrue(TEXT("the item ground-model manifest parses"),
		FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid()))
	{
		return true;
	}
	TestEqual(TEXT("...under the expected schema"), Root->GetStringField(TEXT("schema")),
		FString(TEXT("elysium.item-ground-models")));

	const TSharedPtr<FJsonObject>* Models = nullptr;
	if (!TestTrue(TEXT("...carrying a models table"), Root->TryGetObjectField(TEXT("models"), Models)))
	{
		return true;
	}

	// The catalogue is the enumeration's source of truth, so the manifest must cover every
	// `playermodel` it names — a definition whose model is neither decoded nor listed as skipped
	// would be an item the export silently forgot.
	FString Error;
	FElysiumItemTable Items;
	TSet<FString> Referenced;
	if (Items.Load(Error))
	{
		for (const FElysiumItemDef& Def : Items.Items)
		{
			FString Model = Def.PlayerModel.Replace(TEXT("\\"), TEXT("/")).ToLower().TrimStartAndEnd();
			if (Model.IsEmpty())
			{
				continue;   // armour and the disciplines author no ground body at all
			}
			if (!Model.EndsWith(TEXT(".mdl")))
			{
				Model += TEXT(".mdl");   // two definitions write the path without its extension
			}
			Referenced.Add(Model);
		}
	}

	TSet<FString> Covered;
	TArray<FString> Unresolved;
	TArray<FString> Geometryless;
	int32 Resolved = 0;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Models)->Values)
	{
		const TSharedPtr<FJsonObject>* Row = nullptr;
		if (!Entry.Value.IsValid() || !Entry.Value->TryGetObject(Row))
		{
			continue;
		}
		Covered.Add(Entry.Key);
		const FString Stem = (*Row)->GetStringField(TEXT("stem"));
		// The load-bearing agreement: the runtime derives its stem from the `playermodel` path
		// alone, so if its fold and the decoder's ever diverge every item body silently vanishes.
		TestEqual(*FString::Printf(TEXT("%s folds to the stem the decode wrote"), *Entry.Key),
			FElysiumContentPaths::PropModelStem(Entry.Key), Stem);
		int32 Faces = 0;
		if (!(*Row)->TryGetNumberField(TEXT("faces"), Faces))
		{
			AddError(FString::Printf(TEXT("%s: the manifest states no face count"), *Entry.Key));
			continue;
		}
		if (Faces <= 0)
		{
			Geometryless.Add(Entry.Key);   // `w_null.mdl` and its kin: no triangles, so no asset
			continue;
		}
		if (LoadObject<UStaticMesh>(nullptr, *FElysiumContentPaths::BakedItemMesh(Stem)) != nullptr)
		{
			++Resolved;
		}
		else
		{
			Unresolved.Add(Entry.Key);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* Skipped = nullptr;
	TArray<FString> SkipNotes;
	if (Root->TryGetArrayField(TEXT("skipped"), Skipped))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Skipped)
		{
			const TSharedPtr<FJsonObject>* Row = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(Row))
			{
				continue;
			}
			const FString Model = (*Row)->GetStringField(TEXT("model"));
			Covered.Add(Model);
			SkipNotes.Add(FString::Printf(TEXT("%s (%s)"), *Model,
				*(*Row)->GetStringField(TEXT("reason"))));
		}
	}

	TArray<FString> Uncovered;
	for (const FString& Model : Referenced)
	{
		if (!Covered.Contains(Model))
		{
			Uncovered.Add(Model);
		}
	}
	ReportMissing(*this, TEXT("vdata/items playermodels the item export did not account for"),
		Uncovered);

	// Every decoded model that carries triangles must have a mesh to stand. A model the install
	// does not contain is reported, never failed — that is the export's own contract.
	TestEqual(TEXT("every decoded item ground model with geometry has a baked mesh"),
		Unresolved.Num(), 0);
	if (Unresolved.Num() > 0)
	{
		AddInfo(FString::Printf(TEXT("no baked mesh for: %s"),
			*FString::Join(Unresolved, TEXT(", "))));
	}
	AddInfo(FString::Printf(
		TEXT("item ground models: %d referenced, %d decoded, %d baked, %d without geometry, %d skipped"),
		Referenced.Num(), (*Models)->Values.Num(), Resolved, Geometryless.Num(), SkipNotes.Num()));
	if (Geometryless.Num() > 0)
	{
		AddInfo(FString::Printf(TEXT("no geometry (no asset expected): %s"),
			*FString::Join(Geometryless, TEXT(", "))));
	}
	for (const FString& Note : SkipNotes)
	{
		AddInfo(FString::Printf(TEXT("not exported: %s"), *Note));
	}
	return true;
}

// =====================================================================================
// B6 — the tutorial's authored feeding wires, against the real `sp_tutorial_1.ents`.
//
// `blueblood_maker.Spawn` creates the child; feeding the child has to reach the MAKER-authored
// `OnFedUponBegin` / `OnFedUponEnd` rows (`docs/vtmb/sp_tutorial_1-event-surface.md` §11.2), which
// on this map are `G.Tutorial_Blueblood = 1` (a Python-only wire) and
// `trig_dialog_outside_chopshop.Enable`.
// =====================================================================================

namespace
{
	// Records what entered the event queue, with its provenance. The Python arm of a wire needs a
	// script host to *evaluate*, which a headless world has none of — so the assertion that the
	// authored `G.Tutorial_Blueblood = 1` payload was raised by the right entity is made here, at
	// chokepoint 2, rather than on the `G` store.
	class FElysiumQueuedWireSink final : public IElysiumIOSink
	{
	public:
		struct FRow
		{
			FString Target;
			FName Input;
			FString Python;
			int32 CallerIndex = INDEX_NONE;
			int32 ActivatorIndex = INDEX_NONE;
		};
		TArray<FRow> Rows;

		virtual void OnQueued(double, const FElysiumIOEvent& Event) override
		{
			FRow Row;
			Row.Target = Event.Target;
			Row.Input = Event.Input;
			Row.Python = Event.PythonSrc;
			Row.CallerIndex = Event.Caller.IsSet() ? Event.Caller.Index : INDEX_NONE;
			Row.ActivatorIndex = Event.Activator.IsSet() ? Event.Activator.Index : INDEX_NONE;
			Rows.Add(MoveTemp(Row));
		}
	};

	int32 TriggerDisabledFlag(const FElysiumEntity& Ent)
	{
		const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
		if (!Ent.Class)
		{
			return INDEX_NONE;
		}
		const FElysiumFieldAccessor* Acc = Reg.FindField(*Ent.Class, FName(TEXT("StartDisabled")));
		return (Acc && Acc->Get) ? Acc->Get(Ent).ToInt() : INDEX_NONE;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTutorialFeedingContentTest,
	"Elysium.Content.TutorialFeeding", GElysiumContentTestFlags)
bool FElysiumTutorialFeedingContentTest::RunTest(const FString&)
{
	if (SkipIncompleteCorpus(*this, { TEXT("maps") })) return true;
	const FString Path = FElysiumContentPaths::MapEnts(TEXT("sp_tutorial_1"));
	if (!IFileManager::Get().FileExists(*Path))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: sp_tutorial_1 has not been exported"));
		return true;
	}
	FElysiumEntityDefs Defs;
	if (!TestTrue(TEXT("sp_tutorial_1.ents parses"), FElysiumEntityDefs::Parse(Path, Defs)))
	{
		return true;
	}
	Defs.MapName = TEXT("sp_tutorial_1");
	FElysiumClanTable Clans;
	FElysiumClanTemplate BluebloodTemplate;
	FString TemplateError;
	if (!TestTrue(TEXT("the NPC template table loads"), Clans.Load(TemplateError))
		|| !TestTrue(TEXT("BluebloodFastfood resolves through its real parent chain"),
			Clans.Resolve(TEXT("BluebloodFastfood"), BluebloodTemplate)))
	{
		AddError(TemplateError);
		return false;
	}
	TestEqual(TEXT("the resolved tutorial victim authors FastFood"),
		BluebloodTemplate.GeneralInt(TEXT("FastFood")), 1);
	TestFalse(TEXT("FastFood takes the non-resisting acceptance route"),
		ElysiumFeed::ResistsByAuthoredPolicy(/*bFastFood*/ true, /*bNoResistEffect*/ false));

	FElysiumRecordingServices Services;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.SpawnPlayer();

	TUniquePtr<FElysiumQueuedWireSink> OwnedSink = MakeUnique<FElysiumQueuedWireSink>();
	FElysiumQueuedWireSink* Sink = OwnedSink.Get();
	World.AddSink(MoveTemp(OwnedSink));

	World.Activate(0.0);
	for (int32 i = 0; i < 8; ++i)
	{
		World.Tick(0.0);   // let the map's own openers run
	}

	FElysiumEntity* Maker = World.FindByName(TEXT("blueblood_maker"));
	FElysiumEntity* Chopshop = World.FindByName(TEXT("trig_dialog_outside_chopshop"));
	if (!TestNotNull(TEXT("blueblood_maker is a live entity"), Maker)
		|| !TestNotNull(TEXT("trig_dialog_outside_chopshop is a live entity"), Chopshop))
	{
		return false;
	}
	TestEqual(TEXT("the chopshop dialogue trigger starts disabled, as authored"),
		TriggerDisabledFlag(*Chopshop), 1);

	// The authored rows this test is about.
	int32 AuthoredFedUponEnd = 0;
	for (const FElysiumOutputDef& Row : Maker->Def->Outputs)
	{
		AuthoredFedUponEnd += (Row.Name == TEXT("OnFedUponEnd")) ? 1 : 0;
	}
	TestTrue(TEXT("the maker carries its OnFedUponEnd wiring"), AuthoredFedUponEnd >= 2);

	World.EnqueueInput(TEXT("!self"), FName(TEXT("Spawn")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), Maker->Handle);
	World.Tick(0.0);

	FElysiumEntity* ChildEnt = World.FindByName(TEXT("blueblood"));
	if (!TestNotNull(TEXT("blueblood_maker.Spawn created the child"), ChildEnt))
	{
		return false;
	}
	TestEqual(TEXT("the child def carries every one of the maker's authored output rows"),
		ChildEnt->Def->Outputs.Num(), Maker->Def->Outputs.Num());
	const int32 EntitiesAfterFirstSpawn = World.NumEntities();
	const FElysiumEntityHandle FirstChildHandle = ChildEnt->Handle;
	World.EnqueueInput(TEXT("!self"), FName(TEXT("Spawn")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), Maker->Handle);
	World.Tick(0.0);
	TestEqual(TEXT("the tutorial maker's MaxLiveChildren rejects the second porch-equivalent Spawn"),
		World.NumEntities(), EntitiesAfterFirstSpawn);
	TestEqual(TEXT("the first Blueblood remains the only named live child"),
		World.FindByName(TEXT("blueblood"))->Handle.Index, FirstChildHandle.Index);

	FElysiumPlayer* Player = World.FindPlayer();
	FElysiumCombatCharacter* Child = ChildEnt->AsCombatCharacter();
	if (!TestNotNull(TEXT("the player entity exists"), Player)
		|| !TestNotNull(TEXT("the child is a combat character"), Child))
	{
		return false;
	}
	TestEqual(TEXT("the spawned child retains its authored stattemplate"),
		ChildEnt->Def->Keys.FindRef(TEXT("stattemplate")), FString(TEXT("BluebloodFastfood")));
	if (!TestTrue(TEXT("the resolved template applies to the real spawned NPC"),
		ElysiumNpcTestHooks::ApplyResolvedTemplate(*ChildEnt, BluebloodTemplate)))
	{
		return false;
	}

	// The bare world has no persistent player record, so seed only the feeder half. The victim's
	// BloodPool and FastFood flag came from the real resolved template above; its neutral disposition
	// is left untouched, and the request enters through QueuePlayerFeedEdge + the embodiment service.
	Player->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth, 100);
	Player->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::BloodPool, 0);
	Player->RecomputeSheet();
	TestEqual(TEXT("the real template supplies the blueblood's blood pool"), Child->BloodPoolValue(), 9);
	TestTrue(TEXT("the neutral FastFood blueblood does not resist"), !Child->ResistsFeeding());
	Services.FeedTarget = Child->Handle;
	const int32 RowsBefore = Sink->Rows.Num();
	World.QueuePlayerFeedEdge(EElysiumUseEdge::Pressed);
	World.UpdatePlayerFeed();
	World.QueuePlayerFeedEdge(EElysiumUseEdge::Released);
	World.UpdatePlayerFeed();
	if (!TestTrue(TEXT("the first feed-button tap accepts the spawned blueblood"),
		Player->IsFeedPaired()))
	{
		return false;
	}
	for (int32 Step = 1; Step <= 70; ++Step)
	{
		World.RunPlayerThink(Step * 0.05);
		World.Tick(Step * 0.05);
	}
	TestEqual(TEXT("the first tap remains active through one transfer"),
		Player->FeedState.BloodStolen, 1);
	TestEqual(TEXT("the blueblood loses that transferred point"), Child->BloodPoolValue(), 8);
	World.QueuePlayerFeedEdge(EElysiumUseEdge::Pressed);
	World.UpdatePlayerFeed();
	World.QueuePlayerFeedEdge(EElysiumUseEdge::Released);
	World.UpdatePlayerFeed();
	for (int32 Step = 71; Step <= 120; ++Step)
	{
		World.RunPlayerThink(Step * 0.05);
		World.Tick(Step * 0.05);
	}
	TestFalse(TEXT("the second feed-button tap releases the pair"), Player->IsFeedPaired());
	TestFalse(TEXT("stopping after one point leaves the blueblood alive"), Child->IsDead());

	// THE ACCEPTANCE. `OnFedUponEnd` is a maker-authored row, fired by the CHILD because the child
	// carries the copied rows (`docs/vtmb/entity_io.md`), so both halves of the tutorial's authored
	// consequence land: the trigger is enabled, and the Python payload that sets the flag is raised
	// with the child as caller and the feeder as activator.
	TestEqual(TEXT("trig_dialog_outside_chopshop is enabled by the feed"),
		TriggerDisabledFlag(*Chopshop), 0);

	bool bSawFlagWire = false;
	bool bSawEnableWire = false;
	bool bSawDeathFlagWire = false;
	for (int32 i = RowsBefore; i < Sink->Rows.Num(); ++i)
	{
		const FElysiumQueuedWireSink::FRow& Row = Sink->Rows[i];
		// The maker authors `Tutorial_Blueblood` twice, so match the successful feed payload exactly;
		// this toggle release leaves the victim alive and therefore must not take the OnDeath branch.
		if (Row.Python.Contains(TEXT("Tutorial_Blueblood = 1")))
		{
			bSawFlagWire = true;
			TestEqual(TEXT("the flag wire is fired BY the child, not by the maker"),
				Row.CallerIndex, ChildEnt->Handle.Index);
			TestEqual(TEXT("...with the feeder as activator"),
				Row.ActivatorIndex, Player->Handle.Index);
		}
		bSawDeathFlagWire |= Row.Python.Contains(TEXT("Tutorial_Blueblood = 2"));
		if (Row.Target == TEXT("trig_dialog_outside_chopshop")
			&& Row.Input == FName(TEXT("Enable")))
		{
			bSawEnableWire = true;
			TestEqual(TEXT("the Enable wire is fired BY the child too"),
				Row.CallerIndex, ChildEnt->Handle.Index);
		}
	}
	TestTrue(TEXT("the authored `G.Tutorial_Blueblood = 1` payload was raised"), bSawFlagWire);
	TestTrue(TEXT("the authored chopshop Enable was raised"), bSawEnableWire);
	TestFalse(TEXT("toggle release does not raise the blueblood death payload"), bSawDeathFlagWire);
	// `G` itself is not asserted here: evaluating a field-6 payload needs a script host, which needs
	// the game-state subsystem a headless entity world does not have. The wire reaching the queue
	// with the right provenance is the whole of what this tier can prove; the flag write is the
	// script host's, and is covered where that host is up.

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
