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
#include "Visual/ElysiumDecals.h"
#include "ElysiumDlg.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumKeyValues.h"
#include "ElysiumPlayer.h"
#include "Visual/ElysiumNpcClips.h"
#include "Visual/ElysiumObjModel.h"
#include "ElysiumReflections.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumQuestLog.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumSheetMath.h"
#include "Visual/ElysiumRopes.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "Serialization/MemoryWriter.h"

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
		// 8.4 — prop_physics carry the same model_mesh annotation plus a `.phys` collision sidecar;
		// phys_hinge carry the pre-converted hinge_axis. Sample stems confirm on disk.
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
					TEXT("(run: bake.bat sp_tutorial_1 props)"), *Survey.AnyPhysStem));
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
// the shared `<map>.mtl`, so the bake always finds a texture for each ADecalActor it places.
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

// 7.5 — the reflection channel is bound BY NAME from three places (tools/make_world_materials.py
// authors it, tools/bake_map.py binds it onto each baked instance, FElysiumMaterialFactory and
// AElysiumMapActor::ApplyMaterialOverrides bind it at runtime). A rename that misses one of them
// binds nothing and fails silently in the frame, so the contract is asserted here instead: every
// lit master must carry every parameter ElysiumReflections names.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumReflectionParamsTest,
	"Elysium.Content.ReflectionParams", GElysiumContentTestFlags)
bool FElysiumReflectionParamsTest::RunTest(const FString&)
{
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
			TestEqual(*FString::Printf(TEXT("%s EnvMask falls back to white"), Path),
				Mask->GetPathName(),
				FString(TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")));
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
//
// The corpus is discovered, not listed — every `.ents` under tools/out — so it grows with the
// export. A name the install does not ship is counted and warned rather than failed: that is a
// property of Troika's shipped map data, which no re-export can change.
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
	if (!IFileManager::Get().FileExists(*FElysiumContentPaths::NpcIndex()))
	{
		AddInfo(TEXT("skipping: no exported out/npc/npc_index.json (run tools/npc_export.py to enable)"));
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
		AddInfo(TEXT("skipping: no exported map carries a scripted_sequence"));
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
	const FString ClanDoc = FElysiumContentPaths::VdataFile(TEXT("system/clandoc000.txt"));
	if (!IFileManager::Get().FileExists(*ClanDoc) ||
		!IFileManager::Get().FileExists(*FElysiumContentPaths::NpcIndex()))
	{
		AddInfo(TEXT("skipping: no exported out/vdata + out/npc (run tools/export_all.py --npc to enable)"));
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
				"produce (tools/npc_export.py)"), *Model));
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

	return true;
}

// =====================================================================================
// 11.9 — freeze/thaw a real map's `.ents` world (`save-architecture.md` §10, the content
// tier). The substrate tier proves the mechanism on three synthetic entities; this proves
// it against the shapes the shipped data actually holds — 1,000+ records, every registered
// classname, real output tables, the runtime-spawned player. Self-skips with no export.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMapSnapshotTest,
	"Elysium.Content.MapSnapshot", GElysiumContentTestFlags)
bool FElysiumMapSnapshotTest::RunTest(const FString&)
{
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
		AddInfo(TEXT("no exported maps — snapshot round trip skipped"));
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
	if (!IFileManager::Get().FileExists(*FElysiumContentPaths::VdataFile(TEXT("system/stats.txt"))))
	{
		AddInfo(TEXT("skipping: no exported out/vdata (run tools/export_all.py to enable)"));
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
					// `"ID"` is never read (`game_runtime.md` -> "Quests"). Every shipped row
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
	if (!IFileManager::Get().FileExists(*FElysiumContentPaths::VdataFile(TEXT("system/stats.txt"))))
	{
		AddInfo(TEXT("skipping: no exported out/vdata (run tools/export_all.py to enable)"));
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
		// whole of an NPC's health track (`vdata-catalog.md`).
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
	if (!IFileManager::Get().FileExists(*FElysiumContentPaths::VdataFile(TEXT("system/feats.txt"))))
	{
		AddInfo(TEXT("skipping: no exported out/vdata (run tools/export_all.py to enable)"));
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
	if (!IFileManager::Get().FileExists(
			*FElysiumContentPaths::VdataFile(TEXT("system/quests_santamonica.txt"))))
	{
		AddInfo(TEXT("skipping: no exported out/vdata (run tools/export_all.py to enable)"));
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

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
