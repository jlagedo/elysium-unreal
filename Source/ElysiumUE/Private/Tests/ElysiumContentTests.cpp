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
#include "Tests/ElysiumNativeCharacterTestData.h"
#include "ElysiumContentPaths.h"
#include "Visual/ElysiumDecals.h"
#include "ElysiumDlg.h"
#include "ElysiumBrushComponent.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumMapActor.h"
#include "ElysiumMoveSolve.h"                 // ElysiumMove::StandHeight / U — the +use reach's units
#include "ElysiumPawn.h"
#include "ElysiumUseIcons.h"                  // ELYSIUM_USE_CHANNEL
#include "UI/ElysiumUiArt.h"
#include "Substrate/ElysiumSignData.h"
#include "ElysiumHUDTypes.h"
#include "GameFramework/PlayerController.h"
#include "Tests/AutomationCommon.h"           // FTestWorldWrapper
#include "ElysiumInputAssets.h"
#include "UI/ElysiumInputGlyphControllerData.h"
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
#include "Substrate/ElysiumChargenWizard.h"
#include "Substrate/ElysiumDiceTables.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumWieldRules.h"
#include "Substrate/ElysiumQuestTables.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumSoundVolumeTable.h"
#include "Substrate/ElysiumSheetMath.h"
#include "Tests/ElysiumRulebookTestFixture.h"
#include "Visual/ElysiumRopes.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumTestServices.h"
#include "Tests/ElysiumNpcTestHooks.h"
#include "Animation/AnimSequence.h"
#include "CommonInputBaseTypes.h"
#include "Engine/SkeletalMesh.h"
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
#include "InputTriggers.h"
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

// `Elysium.Policy.*` is the third tier: cases that need a GENERATED `/Game` package -- a real
// material graph, a declared input asset, an audio routing asset -- and read nothing from
// `$ELYSIUM_EXPORT_ROOT`. They cannot be Substrate, because a recording stub cannot answer "the
// master carries an EnvStrength scalar defaulting to 0"; they are not Content either, because
// nothing about the user's own corpus is being regressed. Keeping them under Content made the
// corpus tier slower and told a reader something untrue about what the run needed.
static constexpr EAutomationTestFlags GElysiumPolicyTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// The one case in this file that needs neither the corpus nor a generated package.
static constexpr EAutomationTestFlags GElysiumSubstrateTestFlags =
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


// sp_tutorial_1 — the canonical vertical slice.


bool // Genesis's routing premises. These are deliberately content assertions rather than a hand-built
// duplicate: if the exporter drops the trigger, rewrites its wire, or moves the spawn out of it,
// New Game must fail here before the live route silently stops opening the wizard.


bool // Every exported ChangeNow output must resolve through the real trigger_changelevel registry.
// The 88-wire count was measured on the 23-map bench of docs/vtmb/exported-map-event-surface.md,
// so it is counted over exactly those maps by name: the export root now reaches all 108 maps of
// the patch-first install and a corpus-wide literal would go stale on every widening. The bench
// guard is what catches a regression in the five authored wires whose target names are absent
// from their own map and so cannot be classified by target.


bool // sm_pawnshop_1 — the cross-map travel destination. Confirms it parses and offers a
// landmark to travel to (the P4.6 precondition), without standing up a live travel.


bool // Decals — the `<map>.decals` projector sidecar. Validates that every line is a
// well-formed projector (unit normal, positive extents) and that its material resolves in
// the shared `<map>.mtl`, so the bake always finds a texture for each ADecalActor it places.


bool // Ropes — the `<map>.ropes` cable sidecar. Validates that every segment is a well-formed
// cable (distinct endpoints, positive width, non-negative slack, a node count inside VtMB's
// [2, 10] ROPE_MAX_SEGMENTS bound) and that its `vtmb:material:` id resolves to an imported
// `MI_` package under /ElysiumBaked/Materials (R6.5), so the runtime's BuildRopes always finds
// the instance for each UCableComponent. The tutorial strings its telephone lines this way.


bool bool bool // 7.5 — the reflection channel is bound BY NAME from two places (pipeline/unreal/make_world_materials.py
// authors it, pipeline/unreal/bake_map.py binds it onto each baked instance on an unconverted map;
// the runtime builder that used to be the third retired at R6.5). A rename that misses one of them
// binds nothing and fails silently in the frame, so the contract is asserted here instead: every
// lit master must carry every parameter ElysiumReflections names.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumReflectionParamsTest,
	"Elysium.Policy.ReflectionParams", GElysiumPolicyTestFlags)
bool FElysiumReflectionParamsTest::RunTest(const FString&)
{
	// This validates generated Unreal packages only. A corpus-wide export marker must not hide
	// missing parameters or a broken master graph after a focused map/policy bake.
	static const TCHAR* LitMasters[] = {
		TEXT("/Game/ElysiumGenerated/Materials/M_World_Opaque.M_World_Opaque"),
		TEXT("/Game/ElysiumGenerated/Materials/M_World_Masked.M_World_Masked"),
		TEXT("/Game/ElysiumGenerated/Materials/M_World_Translucent.M_World_Translucent"),
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
				FString(TEXT("/Game/ElysiumGenerated/Materials/T_LinearWhiteMask.T_LinearWhiteMask")));
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
	"Elysium.Policy.GlassMaster", GElysiumPolicyTestFlags)
bool FElysiumGlassMasterTest::RunTest(const FString&)
{
	UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/ElysiumGenerated/Materials/M_World_Glass.M_World_Glass"));
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
	"Elysium.Policy.RefractMaster", GElysiumPolicyTestFlags)
bool FElysiumRefractMasterTest::RunTest(const FString&)
{
	UMaterialInterface* Master = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/ElysiumGenerated/Materials/M_Refract.M_Refract"));
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


// 9.1 / B4 — the `.dlg` parser + branch machine against the real jack_tutorial.dlg. Self-skips when
// the dialogue mirror has not been exported (out/dlg). Confirms the physical-format parse holds and
// that the branch machine can drive Jack's beat from entry to the `G.Tut_Jack = 1` action and END.


bool // The captured Sheriff transaction depends on a small authored join spanning touch, controller
// locomotion, camera tracks, and three particle attachment shapes. Pin that join against the user's
// exported map so a patch/corpus drift cannot silently turn the focused runtime tests into fiction.


bool // scripted_sequence × the NPC clip manifest — every animation a cutscene beat names must
// resolve in the vocabulary the offline export gives that NPC. This is the seam that breaks
// silently: a clip lives in a shared animation bank pulled through the studiohdr include DAG, so
// an exporter change that drops a bank turns a beat into a no-op with only a runtime warning.
//
// Measured over the 10 exported maps: 94 animation references across 108 sequences, 90 resolving.
// The 4 that do not all name the embodied `!playercontroller`; its PC clip vocabulary is separate
// from the NPC manifest this test audits and is excluded here.


bool // The player bodies × the character export — every `.mdl` the clan table names as a PC
// body has a glb on disk. No entity on any map references a player model, so the export seeds
// this half of the set from `vdata/system/clandoc000.txt` itself; this asserts the two have not
// drifted, which is the whole reason the seed is the rulebook and not a hand-written list.
//
// Measured over the merged install: 7 playable clans × 2 sexes × 6 armour slots = 84 slots,
// resolving to 56 distinct models (each clan's top two slots repeat its tier-3 suit).


bool // 12.1 opening content: the authored camera graphs are closed acyclic chains, the six embrace
// props resolve through npc_index v4 with every clip their wires request, and the player material
// keeps the masked/dithered ModelAlpha contract the scripted-camera body path depends on.


bool bool bool IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlacedPropMaterialUsageTest,
	"Elysium.Policy.PlacedPropMaterialUsage", GElysiumPolicyTestFlags)
bool FElysiumPlacedPropMaterialUsageTest::RunTest(const FString&)
{
	// Every branch bake_map._master_for can select for a prop is also applied to the skeletal
	// representation of an authored non-static rest pose. A missing cooked permutation renders
	// Default Material in a packaged build even though the editor compiles it on demand.
	static const TCHAR* Masters[] = {
		TEXT("/Game/ElysiumGenerated/Materials/M_World_Opaque.M_World_Opaque"),
		TEXT("/Game/ElysiumGenerated/Materials/M_World_Masked.M_World_Masked"),
		TEXT("/Game/ElysiumGenerated/Materials/M_World_Translucent.M_World_Translucent"),
		TEXT("/Game/ElysiumGenerated/Materials/M_World_Glass.M_World_Glass"),
		TEXT("/Game/ElysiumGenerated/Materials/M_Refract.M_Refract"),
		TEXT("/Game/ElysiumGenerated/Materials/M_Additive.M_Additive"),
	};
	for (const TCHAR* Path : Masters)
	{
		UMaterial* Material = LoadObject<UMaterial>(nullptr, Path);
		if (TestNotNull(*FString::Printf(TEXT("placed-prop master loads: %s"), Path), Material))
		{
			TestTrue(*FString::Printf(TEXT("%s has its skeletal permutation authored"), Path),
				Material->GetUsageByFlag(MATUSAGE_SkeletalMesh));
		}
	}
	return true;
}

bool bool bool bool bool // Freeze/thaw a real map's `.ents` world — the content
// tier. The substrate tier proves the mechanism on three synthetic entities; this proves
// it against the shapes the shipped data actually holds — 1,000+ records, every registered
// classname, real output tables, the runtime-spawned player. Self-skips with no export.

bool // The rulebook — every `vdata/system/` table the RPG layer reads, against the real
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


bool // 9.6 — `dicerolls.txt` against the real file, and the resolver over what it loaded.
//
// The claim under test is that the shipped weighting tables are a plain uniform d10, which is what
// makes the resolver's fail-open fallback faithful rather than an approximation. A patched install
// that reweights a die is not a failure — it is the mechanism working — so a non-uniform table is
// reported rather than failed, and the structural assertions still hold.
//
// The algorithm itself is pinned content-free in `Elysium.Substrate.Dice`.


bool // `sound_volume_table.txt` against the real file — the authored half of NPC hearing.
//
// Two claims: the four documented levels still carry the radii and occlusion policy
// `docs/vtmb/npc-ai-reverse-engineering.md` records, and every category this runtime's producers
// name is actually in the file. The second is the one that matters: a category the table does not
// name resolves to the normal level and warns, so a re-export that renamed a row would quietly
// change how far a gunshot carries.
//
// The bus's own resolution rules are pinned content-free in `Elysium.Substrate.GameSound.Resolve`.


bool // The character sheet against the real rulebook — the audit that keeps the compiled slot table
// honest.
//
// `ElysiumSheetSlots.h` freezes the layout in C++ because VtMB freezes it in `vampire.dll`; the
// values come from `stats.txt`. That split only holds while the two agree, so this walks every
// compiled slot and asserts the file names the same trait at the same index.


bool // The sheet's arithmetic against the real rulebook — the halves the content-free tier
// cannot reach: the trait-effect layer resolved out of `traiteffects000.txt`, the feat evaluator
// over the shipped 23 feats, and the award values in `experience_table.txt`.


bool // Quests against the real catalogue — what the content-free tier's hand-built fixture
// cannot answer: that the shipped 161 `AwardXP` keys are actually REACHABLE through the state
// change that owes them, not merely present in the file. The entity-side award walk itself
// (give-once, Experience_Modifier, the remainder-keeping /100) is `Elysium.Content.SheetMath`'s.


bool // Chargen over the real rulebook — the pools a clan produces and the baseline it stands on.


bool // The blink cadence the eye pass schedules from. The table authors the key under two
// spellings — `"Min Blink Interval"` on four rows and `"MinBlinkInterval"` on six — and reading only
// one is a silent fallback to the defaults rather than a parse error, which is exactly the shape of
// bug this tier exists to catch: the two rows that carry a cadence other than 2.5/6.0 are both
// spelled the second way.

bool bool IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMouseInputAssetsContentTest,
	"Elysium.Policy.MouseInputAssets", GElysiumPolicyTestFlags)

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumInputGlyphAssetsContentTest,
	"Elysium.Substrate.InputGlyphs", GElysiumSubstrateTestFlags)

bool FElysiumInputGlyphAssetsContentTest::RunTest(const FString&)
{
	UCommonInputPlatformSettings* Platform = UCommonInputPlatformSettings::Get();
	if (!TestNotNull(TEXT("CommonInput Windows platform policy"), Platform))
	{
		return false;
	}
	TestEqual(TEXT("Windows starts with Xbox glyphs until hardware identifies another pad"),
		Platform->GetDefaultGamepadName(), FName(TEXT("Xbox")));
	TestEqual(TEXT("GameInput DualSense hardware selects PlayStation glyphs"),
		Platform->GetBestGamepadNameForHardware(
			TEXT("Xbox"), TEXT("GameInput"), TEXT("DualSense")), FName(TEXT("DualSense")));
	TestEqual(TEXT("GameInput Xbox One hardware selects Xbox glyphs"),
		Platform->GetBestGamepadNameForHardware(
			TEXT("DualSense"), TEXT("GameInput"), TEXT("XboxOne")), FName(TEXT("Xbox")));

	const UElysiumKeyboardControllerData* Keyboard = GetDefault<UElysiumKeyboardControllerData>();
	const UElysiumXboxControllerData* Xbox = GetDefault<UElysiumXboxControllerData>();
	const UElysiumDualSenseControllerData* DualSense =
		GetDefault<UElysiumDualSenseControllerData>();
	if (!TestNotNull(TEXT("keyboard glyph policy"), Keyboard) ||
		!TestNotNull(TEXT("Xbox glyph policy"), Xbox) ||
		!TestNotNull(TEXT("DualSense glyph policy"), DualSense))
	{
		return false;
	}

	const TArray<const UCommonInputBaseControllerData*> KeyboardPolicies =
		Platform->GetControllerDataForInputType(ECommonInputType::MouseAndKeyboard, NAME_None);
	const TArray<const UCommonInputBaseControllerData*> XboxPolicies =
		Platform->GetControllerDataForInputType(ECommonInputType::Gamepad, TEXT("Xbox"));
	const TArray<const UCommonInputBaseControllerData*> DualSensePolicies =
		Platform->GetControllerDataForInputType(ECommonInputType::Gamepad, TEXT("DualSense"));
	TestTrue(TEXT("Windows policy registers keyboard glyph data"), KeyboardPolicies.Contains(Keyboard));
	TestTrue(TEXT("Windows policy registers Xbox glyph data"), XboxPolicies.Contains(Xbox));
	TestTrue(TEXT("Windows policy registers DualSense glyph data"),
		DualSensePolicies.Contains(DualSense));

	auto TestGlyphs = [this](const UElysiumInputGlyphControllerData* Policy,
		const TArray<FKey>& Keys, const TCHAR* Family)
	{
		for (const FKey& Key : Keys)
		{
			FSlateBrush Brush;
			const FString Label = FString::Printf(TEXT("%s has a glyph for %s"), Family, *Key.ToString());
			if (!TestTrue(Label, Policy->TryGetInputBrush(Brush, Key)))
			{
				continue;
			}
			UTexture2D* Texture = Cast<UTexture2D>(Brush.GetResourceObject());
			if (TestNotNull(*FString::Printf(TEXT("%s resolves a texture"), *Label), Texture))
			{
				TestEqual(*FString::Printf(TEXT("%s source width"), *Label),
					Texture->Source.GetSizeX(), int64(128));
				TestEqual(*FString::Printf(TEXT("%s source height"), *Label),
					Texture->Source.GetSizeY(), int64(128));
				TestTrue(*FString::Printf(TEXT("%s comes from Kenney policy content"), *Label),
					Texture->GetPathName().Contains(
						FString::Printf(TEXT("/Game/ElysiumGenerated/Input/Glyphs/Kenney/%s/"), Family)));
			}
		}
	};

	TestGlyphs(Keyboard, {EKeys::E, EKeys::Enter, EKeys::Escape}, TEXT("Keyboard"));
	const TArray<FKey> StandardGamepadKeys = {
		EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right,
		EKeys::Gamepad_FaceButton_Left, EKeys::Gamepad_FaceButton_Top,
		EKeys::Gamepad_LeftShoulder, EKeys::Gamepad_RightShoulder,
		EKeys::Gamepad_LeftTriggerAxis, EKeys::Gamepad_RightTriggerAxis,
		EKeys::Gamepad_LeftThumbstick, EKeys::Gamepad_RightThumbstick,
		EKeys::Gamepad_Left2D, EKeys::Gamepad_Right2D,
		EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Down,
		EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Right,
		EKeys::Gamepad_Special_Left, EKeys::Gamepad_Special_Right,
	};
	TestGlyphs(Xbox, StandardGamepadKeys, TEXT("Xbox"));
	TArray<FKey> DualSenseKeys = StandardGamepadKeys;
	DualSenseKeys.Add(FKey(ElysiumInputAssets::DualSenseCreateKey));
	DualSenseKeys.Add(FKey(ElysiumInputAssets::DualSenseMuteKey));
	TestGlyphs(DualSense, DualSenseKeys, TEXT("PlayStation"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumGamepadInputAssetsContentTest,
	"Elysium.Policy.GamepadInputAssets", GElysiumPolicyTestFlags)

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

	TestEqual(TEXT("the input policy defines exactly nineteen actions"), ActionSet->Actions.Num(), 19);
	const FElysiumInputActionDefinition* Move = ActionSet->Find(TEXT("Move"));
	const FElysiumInputActionDefinition* Look = ActionSet->Find(TEXT("Look"));
	const FElysiumInputActionDefinition* Jump = ActionSet->Find(TEXT("Jump"));
	const FElysiumInputActionDefinition* Use = ActionSet->Find(TEXT("Use"));
	const FElysiumInputActionDefinition* Feed = ActionSet->Find(TEXT("Feed"));
	const FElysiumInputActionDefinition* Duck = ActionSet->Find(TEXT("Duck"));
	const FElysiumInputActionDefinition* Camera = ActionSet->Find(TEXT("Camera"));
	const FElysiumInputActionDefinition* WalkRun = ActionSet->Find(TEXT("WalkRun"));
	const FElysiumInputActionDefinition* Attack = ActionSet->Find(TEXT("Attack"));
	const FElysiumInputActionDefinition* SecondaryAttack = ActionSet->Find(TEXT("SecondaryAttack"));
	const FElysiumInputActionDefinition* Reload = ActionSet->Find(TEXT("Reload"));
	const FElysiumInputActionDefinition* DisciplineCast = ActionSet->Find(TEXT("DisciplineCast"));
	const FElysiumInputActionDefinition* WeaponRanged = ActionSet->Find(TEXT("WeaponRanged"));
	const FElysiumInputActionDefinition* WeaponMelee = ActionSet->Find(TEXT("WeaponMelee"));
	const FElysiumInputActionDefinition* WeaponLast = ActionSet->Find(TEXT("WeaponLast"));
	const FElysiumInputActionDefinition* Holster = ActionSet->Find(TEXT("Holster"));
	const FElysiumInputActionDefinition* Character = ActionSet->Find(TEXT("Character"));
	const FElysiumInputActionDefinition* Pause = ActionSet->Find(TEXT("Pause"));
	if (!TestTrue(TEXT("Attack definition exists"), Attack && Attack->Action) ||
		!TestTrue(TEXT("SecondaryAttack definition exists"), SecondaryAttack && SecondaryAttack->Action) ||
		!TestTrue(TEXT("Reload definition exists"), Reload && Reload->Action) ||
		!TestTrue(TEXT("DisciplineCast definition exists"), DisciplineCast && DisciplineCast->Action) ||
		!TestTrue(TEXT("WeaponRanged definition exists"), WeaponRanged && WeaponRanged->Action) ||
		!TestTrue(TEXT("WeaponMelee definition exists"), WeaponMelee && WeaponMelee->Action) ||
		!TestTrue(TEXT("WeaponLast definition exists"), WeaponLast && WeaponLast->Action) ||
		!TestTrue(TEXT("Holster definition exists"), Holster && Holster->Action) ||
		!TestTrue(TEXT("Character definition exists"), Character && Character->Action) ||
		!TestTrue(TEXT("Pause definition exists"), Pause && Pause->Action) ||
		!TestTrue(TEXT("Move definition exists"), Move && Move->Action) ||
		!TestTrue(TEXT("Look definition exists"), Look && Look->Action) ||
		!TestTrue(TEXT("Jump definition exists"), Jump && Jump->Action) ||
		!TestTrue(TEXT("Use definition exists"), Use && Use->Action) ||
		!TestTrue(TEXT("Feed definition exists"), Feed && Feed->Action) ||
		!TestTrue(TEXT("Duck definition exists"), Duck && Duck->Action) ||
		!TestTrue(TEXT("Camera definition exists"), Camera && Camera->Action) ||
		!TestTrue(TEXT("WalkRun definition exists"), WalkRun && WalkRun->Action))
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
	TestTrue(TEXT("Attack is Boolean"), Attack->Action->ValueType == EInputActionValueType::Boolean);
	TestTrue(TEXT("SecondaryAttack is Boolean"),
		SecondaryAttack->Action->ValueType == EInputActionValueType::Boolean);
	TestEqual(TEXT("Jump preserves its command identity"), Jump->Command, FString(TEXT("+jump")));
	TestTrue(TEXT("Jump is a press/release pair"), Jump->bButtonPair);
	TestEqual(TEXT("RT preserves the ordinary use command identity"), Use->Command, FString(TEXT("+use")));
	TestTrue(TEXT("Use is a press/release pair"), Use->bButtonPair);
	TestEqual(TEXT("Y/Triangle preserves the ordinary feed command identity"),
		Feed->Command, FString(TEXT("+feed")));
	TestTrue(TEXT("Feed keeps the low-level press/release command pair"), Feed->bButtonPair);
	TestEqual(TEXT("B/Circle fires the ordinary crouch verb"), Duck->Command, FString(TEXT("+duck")));
	TestTrue(TEXT("crouch is a press/release pair like the keyboard's"), Duck->bButtonPair);
	// R3 is a genuine one-shot: `togglecamera` has no release half, so binding a Completed edge
	// would fire `-togglecamera`, which is not a verb.
	TestEqual(TEXT("R3 fires the view toggle"), Camera->Command, FString(TEXT("togglecamera")));
	TestFalse(TEXT("view toggle is not a press/release pair"), Camera->bButtonPair);
	TestEqual(TEXT("RB preserves primary attack"), Attack->Command, FString(TEXT("+attack")));
	TestTrue(TEXT("primary attack is a press/release pair"), Attack->bButtonPair);
	TestEqual(TEXT("LT preserves the composite secondary command"),
		SecondaryAttack->Command, FString(TEXT("+wpn_secondaryatk")));
	TestTrue(TEXT("the composite secondary is a press/release pair"), SecondaryAttack->bButtonPair);
	TestEqual(TEXT("X/Square preserves reload"), Reload->Command, FString(TEXT("+reload")));
	TestTrue(TEXT("reload is a press/release pair"), Reload->bButtonPair);
	TestEqual(TEXT("LB casts the selected discipline"),
		DisciplineCast->Command, FString(TEXT("vdiscipline_last")));
	TestFalse(TEXT("the discipline cast is a one-shot"), DisciplineCast->bButtonPair);

	// The D-pad is one weapon cluster. Left and right enter the melee/ranged categories, and the
	// selector advances inside a category when its verb repeats. All four are one-shots.
	TestEqual(TEXT("D-pad right selects ranged weapons"),
		WeaponRanged->Command, FString(TEXT("slot3")));
	TestEqual(TEXT("D-pad left selects melee weapons"),
		WeaponMelee->Command, FString(TEXT("slot2")));
	TestEqual(TEXT("D-pad up recalls the last weapon"), WeaponLast->Command, FString(TEXT("lastinv")));
	TestEqual(TEXT("D-pad down holsters"), Holster->Command, FString(TEXT("holster")));
	TestFalse(TEXT("ranged selection is not a press/release pair"), WeaponRanged->bButtonPair);
	TestFalse(TEXT("melee selection is not a press/release pair"), WeaponMelee->bButtonPair);
	TestFalse(TEXT("last weapon is not a press/release pair"), WeaponLast->bButtonPair);
	TestFalse(TEXT("holster is not a press/release pair"), Holster->bButtonPair);
	TestEqual(TEXT("View opens the character screen"), Character->Command, FString(TEXT("+chareditor")));
	TestTrue(TEXT("character screen preserves its command pair"), Character->bButtonPair);
	TestEqual(TEXT("Menu preserves Escape's pause/cancel verb"),
		Pause->Command, FString(TEXT("cancelselect")));
	TestFalse(TEXT("pause/cancel is a one-shot"), Pause->bButtonPair);
	TestEqual(TEXT("L3 preserves the patch walk/run alias"),
		WalkRun->Command, FString(TEXT("autospeed")));
	TestFalse(TEXT("walk/run is a one-shot alias"), WalkRun->bButtonPair);
	// Every mapped command names a declared verb, or the button is a no-op that logs nothing. The
	// leading `+` is stripped first, because a pair's press edge is declared under its bare name.
	// `autospeed` is the patch's cfg alias, so it deliberately resolves in the console's alias tier
	// rather than this compiled inventory.
	for (const FElysiumInputActionDefinition* Definition : {
		Jump, Use, Feed, Duck, Camera, Attack, SecondaryAttack, Reload, DisciplineCast,
		WeaponRanged, WeaponMelee, WeaponLast, Holster, Character, Pause })
	{
		const FString Bare = Definition->Command.StartsWith(TEXT("+"))
			? Definition->Command.Mid(1) : Definition->Command;
		TestTrue(FString::Printf(TEXT("'%s' is a declared verb"), *Definition->Command),
			FElysiumCommands::Get().IsDeclared(ElysiumCommands::Canonical(Bare)));
	}

	const TArray<FEnhancedActionKeyMapping>& Mappings = Context->GetMappings();
	TestEqual(TEXT("eighteen actions are gamepad-mapped"), Mappings.Num(), 18);
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
	const FEnhancedActionKeyMapping* WalkRunMapping = FindMapping(WalkRun->Action);
	const FEnhancedActionKeyMapping* AttackMapping = FindMapping(Attack->Action);
	const FEnhancedActionKeyMapping* SecondaryAttackMapping = FindMapping(SecondaryAttack->Action);
	const FEnhancedActionKeyMapping* ReloadMapping = FindMapping(Reload->Action);
	const FEnhancedActionKeyMapping* DisciplineCastMapping = FindMapping(DisciplineCast->Action);
	const FEnhancedActionKeyMapping* WeaponRangedMapping = FindMapping(WeaponRanged->Action);
	const FEnhancedActionKeyMapping* WeaponMeleeMapping = FindMapping(WeaponMelee->Action);
	const FEnhancedActionKeyMapping* WeaponLastMapping = FindMapping(WeaponLast->Action);
	const FEnhancedActionKeyMapping* HolsterMapping = FindMapping(Holster->Action);
	const FEnhancedActionKeyMapping* CharacterMapping = FindMapping(Character->Action);
	const FEnhancedActionKeyMapping* PauseMapping = FindMapping(Pause->Action);
	if (!TestNotNull(TEXT("Attack mapping"), AttackMapping) ||
		!TestNotNull(TEXT("SecondaryAttack mapping"), SecondaryAttackMapping) ||
		!TestNotNull(TEXT("Reload mapping"), ReloadMapping) ||
		!TestNotNull(TEXT("DisciplineCast mapping"), DisciplineCastMapping) ||
		!TestNotNull(TEXT("WeaponRanged mapping"), WeaponRangedMapping) ||
		!TestNotNull(TEXT("WeaponMelee mapping"), WeaponMeleeMapping) ||
		!TestNotNull(TEXT("WeaponLast mapping"), WeaponLastMapping) ||
		!TestNotNull(TEXT("Holster mapping"), HolsterMapping) ||
		!TestNotNull(TEXT("Character mapping"), CharacterMapping) ||
		!TestNotNull(TEXT("Pause mapping"), PauseMapping) ||
		!TestNotNull(TEXT("Move mapping"), MoveMapping) ||
		!TestNotNull(TEXT("Look mapping"), LookMapping) ||
		!TestNotNull(TEXT("Jump mapping"), JumpMapping) ||
		!TestNotNull(TEXT("Use mapping"), UseMapping) ||
		!TestNotNull(TEXT("Feed mapping"), FeedMapping) ||
		!TestNotNull(TEXT("Duck mapping"), DuckMapping) ||
		!TestNotNull(TEXT("Camera mapping"), CameraMapping) ||
		!TestNotNull(TEXT("WalkRun mapping"), WalkRunMapping))
	{
		return false;
	}
	TestEqual(TEXT("Move uses the left stick"), MoveMapping->Key, EKeys::Gamepad_Left2D);
	TestEqual(TEXT("Look uses the right stick"), LookMapping->Key, EKeys::Gamepad_Right2D);
	TestEqual(TEXT("Jump uses A/Cross"), JumpMapping->Key, EKeys::Gamepad_FaceButton_Bottom);
	TestEqual(TEXT("Use is on RT"), UseMapping->Key, EKeys::Gamepad_RightTriggerAxis);
	TestEqual(TEXT("Feed is on Y/Triangle"), FeedMapping->Key, EKeys::Gamepad_FaceButton_Top);
	TestEqual(TEXT("crouch is on B/Circle"), DuckMapping->Key, EKeys::Gamepad_FaceButton_Right);
	TestEqual(TEXT("walk/run is on L3"), WalkRunMapping->Key, EKeys::Gamepad_LeftThumbstick);
	TestEqual(TEXT("the view toggle is on R3"), CameraMapping->Key, EKeys::Gamepad_RightThumbstick);
	TestEqual(TEXT("primary attack is on RB"), AttackMapping->Key, EKeys::Gamepad_RightShoulder);
	TestEqual(TEXT("block/weapon-secondary is on LT"),
		SecondaryAttackMapping->Key, EKeys::Gamepad_LeftTriggerAxis);
	TestEqual(TEXT("reload is on X/Square"), ReloadMapping->Key, EKeys::Gamepad_FaceButton_Left);
	TestEqual(TEXT("discipline cast is on LB"),
		DisciplineCastMapping->Key, EKeys::Gamepad_LeftShoulder);
	TestEqual(TEXT("ranged weapon selection is on D-pad right"),
		WeaponRangedMapping->Key, EKeys::Gamepad_DPad_Right);
	TestEqual(TEXT("melee weapon selection is on D-pad left"),
		WeaponMeleeMapping->Key, EKeys::Gamepad_DPad_Left);
	TestEqual(TEXT("last weapon is on D-pad up"), WeaponLastMapping->Key, EKeys::Gamepad_DPad_Up);
	TestEqual(TEXT("holster is on D-pad down"), HolsterMapping->Key, EKeys::Gamepad_DPad_Down);
	TestEqual(TEXT("character screen is on View/touchpad"),
		CharacterMapping->Key, EKeys::Gamepad_Special_Left);
	TestEqual(TEXT("pause/cancel is on Menu/Options"),
		PauseMapping->Key, EKeys::Gamepad_Special_Right);
	TestEqual(TEXT("ranged selection carries no modifier stack"),
		WeaponRangedMapping->Modifiers.Num(), 0);
	TestEqual(TEXT("melee selection carries no modifier stack"),
		WeaponMeleeMapping->Modifiers.Num(), 0);
	TestEqual(TEXT("L3 carries no modifier stack"), WalkRunMapping->Modifiers.Num(), 0);
	TestEqual(TEXT("R3 carries no modifier stack"), CameraMapping->Modifiers.Num(), 0);
	TestEqual(TEXT("RT carries one explicit down trigger"), UseMapping->Triggers.Num(), 1);
	TestEqual(TEXT("LT carries one explicit down trigger"), SecondaryAttackMapping->Triggers.Num(), 1);
	for (const FEnhancedActionKeyMapping* Mapping : { UseMapping, SecondaryAttackMapping })
	{
		if (Mapping->Triggers.Num() == 1)
		{
			const UInputTriggerDown* Down = Cast<UInputTriggerDown>(Mapping->Triggers[0]);
			TestNotNull(TEXT("an analog trigger uses UInputTriggerDown"), Down);
			if (Down)
			{
				TestEqual(TEXT("the analog trigger crosses at half pull"),
					Down->ActuationThreshold, 0.5f);
			}
		}
	}
	TestEqual(TEXT("RB attack needs no trigger object"), AttackMapping->Triggers.Num(), 0);
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

		// NPC-chain inputs. `TeleportToEntity` is FIELD_EHANDLE on CAI_BaseNPCTroika; the two
		// remaining map-fired names have no recovered datamap record yet.
		{ TEXT("npc_VVampire"),        TEXT("SetRelationship"),        TEXT("STRING") },
		{ TEXT("npc_VVampire"),        TEXT("TeleportToEntity"),       TEXT("EHANDLE") },
		{ TEXT("npc_VVampire"),        TEXT("SetScriptedDiscipline"),  TEXT("(unrecovered)") },
		{ TEXT("npc_VVampire"),        TEXT("TakeDamage"),             TEXT("(unrecovered)") },
		{ TEXT("npc_VHumanCombatant"), TEXT("SetRelationship"),        TEXT("STRING") },
		{ TEXT("npc_VHumanCombatant"), TEXT("TeleportToEntity"),       TEXT("EHANDLE") },
		// And the chain again, from the other leaf of it.
		{ TEXT("npc_VVampire"),        TEXT("WillTalk"),               TEXT("INTEGER") },
		{ TEXT("npc_VVampire"),        TEXT("SetBodyAsCameraTarget"),  TEXT("VOID") },

		// CItemContainer — Python receiver calls used by patch refill helpers. The animated leaf is
		// asserted because that is the concrete class of `container_hunter` and `tutsafe`.
		{ TEXT("item_container_animated"), TEXT("SpawnItemInContainer"), TEXT("STRING") },
		{ TEXT("item_container_animated"), TEXT("AddEntityToContainer"), TEXT("STRING") },
		{ TEXT("item_container_animated"), TEXT("DeleteItems"),          TEXT("VOID") },
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


// `vdata/items` — the item catalogue against the real exported corpus.
//
// The recovered figures in `docs/vtmb/inventory.md` §4 are PATCH-FIRST facts about the
// content this project consumes, not stock-retail authoring facts, so a local corpus that
// differs is reported rather than failed: each block asserts structural validity first and
// reports the actuals through AddInfo when they disagree with the recorded numbers.


bool // B6 — the tutorial's authored feeding wires, against the real `sp_tutorial_1.ents`.
//
// `blueblood_maker.Spawn` creates the child; feeding the child has to reach the MAKER-authored
// `OnFedUponBegin` / `OnFedUponEnd` rows (`docs/vtmb/sp_tutorial_1-event-surface.md` §11.2), which
// on this map are `G.Tutorial_Blueblood = 1` (a Python-only wire) and
// `trig_dialog_outside_chopshop.Enable`.


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

bool // A hub door stays selectable through the transition volume it stands in.
//
// sm_hub_1's exits are wired `OnFullyOpen -> <Teleport>.ChangeNow`, and the `trigger_changelevel`
// is authored across the doorway approach — so the player's eye is inside that volume whenever
// they are close enough to +use the door. A trigger body carries no use anchor, so if it answers
// the ElysiumUse channel at all it blocks the look-ray where it starts and nothing behind it can
// ever be selected. Both bodies are built from the map's own exported hulls at their own relative
// placement, carried rigidly in front of the test pawn.

bool #endif // WITH_DEV_AUTOMATION_TESTS
