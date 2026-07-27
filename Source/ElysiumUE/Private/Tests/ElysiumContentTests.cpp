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
#include "ElysiumKeyValues.h"
#include "ElysiumNpcClips.h"
#include "ElysiumObjModel.h"
#include "ElysiumReflections.h"
#include "ElysiumRopes.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"

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

#endif // WITH_DEV_AUTOMATION_TESTS
