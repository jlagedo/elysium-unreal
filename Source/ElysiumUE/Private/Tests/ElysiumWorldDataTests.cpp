// Content-free Substrate automation: decal, rope, and world-material sidecar contracts.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "ElysiumAppState.h"
#include "ElysiumAudioLatency.h"
#include "ElysiumBinds.h"
#include "ElysiumBrushComponent.h"
#include "Player/ElysiumCameraShots.h"
#include "ElysiumCameraComponent.h"
#include "ElysiumCameraRig.h"
#include "ElysiumCameraSolve.h"
#include "Substrate/ElysiumCameraTrack.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumCommands.h"
#include "ElysiumContentPaths.h"
#include "Debug/ElysiumChannelRecorder.h"
#include "Debug/ElysiumConsole.h"
#include "Debug/ElysiumLogTap.h"
#include "Visual/ElysiumBlendGrids.h"
#include "Visual/ElysiumDecals.h"
#include "Visual/ElysiumEntityBodies.h"
#include "Visual/ElysiumBipedAnimInstance.h"
#include "Visual/ElysiumNpcBody.h"
#include "Visual/ElysiumLightRig.h"
#include "ElysiumDlg.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEnvironment.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumStub.h"
#include "ElysiumWeatherState.h"
#include "ElysiumFog.h"
#include "ElysiumEventQueue.h"
#include "ElysiumWireReport.h"
#include "ElysiumExpr.h"
#include "ElysiumGaitSpeeds.h"               // the animation's per-direction speed
#include "ElysiumGameClock.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumGymSpec.h"
#include "Visual/ElysiumPoseDeviation.h"
#include "ElysiumHUD.h"
#include "ElysiumInputScope.h"
#include "ElysiumKeyValues.h"
#include "ElysiumLineService.h"
#include "ElysiumLookCurve.h"                // the mouse path's pure rules
#include "Debug/ElysiumMoveCourses.h"        // the event-timed press's pure half
#include "ElysiumMapActor.h"
#include "ElysiumMapEpoch.h"
#include "Map/ElysiumFeedTargeting.h"
#include "Map/ElysiumMapCollision.h"
#include "ElysiumSoundCache.h"
#include "ElysiumMovementComponent.h"
#include "Visual/ElysiumObjModel.h"
#include "Visual/ElysiumNpcClips.h"
#include "ElysiumLocomotionSample.h"         // the body sample's pure rules
#include "ElysiumMoveSolve.h"                // ElysiumMove::StandViewZ / U — the gaze test's units
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumDisposition.h"    // FElysiumEyeTargetTuning
#include "ElysiumPawn.h"
#include "ElysiumPresentationSubsystem.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumChargen.h"
#include "Substrate/ElysiumDice.h"
#include "Substrate/ElysiumFeed.h"
#include "Substrate/ElysiumInterestingPlaces.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMover.h"
#include "Substrate/ElysiumQuestLog.h"
#include "Substrate/ElysiumQuestView.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumSkillClasses.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumScenePlayer.h"
#include "Substrate/ElysiumSheetMath.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "Scripting/ElysiumPythonVM.h"
#include "ElysiumViewState.h"
#include "Visual/ElysiumRopes.h"
#include "Scripting/ElysiumScriptFS.h"
#include "ElysiumScriptHost.h"
#include "Scripting/ElysiumScriptNatives.h"
#include "Tests/ElysiumOverlapTestProbe.h"
#include "Tests/ElysiumTestServices.h"
#include "ElysiumTimeControl.h"
#include "ElysiumUseIcons.h"
#include "ElysiumUserCmd.h"
#include "ElysiumVariant.h"

#include "Math/RotationMatrix.h"
#include "Animation/AnimSequence.h"
#include "Serialization/MemoryWriter.h"
#include "Tests/AutomationCommon.h"

#include "Components/SceneComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Sound/SoundGenerator.h"
#include "Sound/SoundWaveProcedural.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// One context flag (runs anywhere) + the product filter (this project's own suite bucket).
// EAutomationTestFlags is a strong enum in 5.8, so the constant carries that type (ENUM_CLASS_FLAGS
// makes the `|` yield an EAutomationTestFlags), not int32.
namespace ElysiumWorldDataTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// =====================================================================================
// FElysiumDecals — the `.decals` projector sidecar parser + the orientation contract the bake
// places each ADecalActor by. Pure data + math, no RHI.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDecalsTest, "Elysium.Substrate.Decals", GElysiumTestFlags)
bool FElysiumDecalsTest::RunTest(const FString&)
{
	// --- parse: 15 tokens -> one def with fields in order; malformed lines dropped ---
	TArray<FString> Lines;
	Lines.Add(TEXT("decals/blood1 10.0 20.0 30.0 1 0 0 0 1 0 0 0 1 12.5 7.5"));
	Lines.Add(TEXT("# too few tokens -> skipped"));
	Lines.Add(TEXT("decals/blood2 0 0 0 0 0 1 1 0 0 0 -1 0 4 4"));
	Lines.Add(FString());   // blank -> skipped

	TArray<FElysiumDecalDef> Defs;
	FElysiumDecals::ParseLines(Lines, Defs);
	TestEqual(TEXT("two valid decals parsed (two junk lines dropped)"), Defs.Num(), 2);

	if (Defs.Num() >= 1)
	{
		const FElysiumDecalDef& D = Defs[0];
		TestEqual(TEXT("material name"), D.Mat, FString(TEXT("decals/blood1")));
		TestTrue(TEXT("loc parsed"), D.Loc.Equals(FVector(10, 20, 30)));
		TestTrue(TEXT("normal parsed"), D.Normal.Equals(FVector(1, 0, 0)));
		TestTrue(TEXT("s_dir parsed"), D.SDir.Equals(FVector(0, 1, 0)));
		TestTrue(TEXT("t_dir parsed"), D.TDir.Equals(FVector(0, 0, 1)));
		TestEqual(TEXT("half-width"), D.HalfW, 12.5f);
		TestEqual(TEXT("half-height"), D.HalfH, 7.5f);
	}

	// --- orientation: the bake rotates each decal by MakeRotFromXZ(Normal, SDir). A deferred decal
	// maps texture U -> local Z and V -> local Y, so the surface horizontal (SDir, the U axis) goes
	// on local Z; local +X stays the room normal, so the component's -X (its projection axis) fires
	// into the wall. ---
	const FVector Normal(1, 0, 0), SDir(0, -1, 0);   // wall decal facing +X, U axis along -Y
	const FMatrix R = FRotationMatrix::MakeFromXZ(Normal, SDir);
	TestTrue(TEXT("local +X aligns with the room normal (projection is -X into the wall)"),
		R.GetUnitAxis(EAxis::X).Equals(Normal));
	TestTrue(TEXT("local +Z aligns with the surface horizontal / texture U (SDir)"),
		R.GetUnitAxis(EAxis::Z).Equals(SDir));
	// The three axes stay orthonormal (a valid rotation, not the exporter's reflected frame).
	TestTrue(TEXT("Y is orthonormal to X and Z"),
		FMath::IsNearlyZero(FVector::DotProduct(R.GetUnitAxis(EAxis::Y), Normal)) &&
		FMath::IsNearlyZero(FVector::DotProduct(R.GetUnitAxis(EAxis::Y), SDir)));

	return true;
}

// =====================================================================================
// FElysiumRopes — the `.ropes` cable sidecar parser + the rest-length contract BuildRopes builds
// each UCableComponent from. Pure data + math, no RHI.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumRopesTest, "Elysium.Substrate.Ropes", GElysiumTestFlags)
bool FElysiumRopesTest::RunTest(const FString&)
{
	// --- parse: 14 tokens -> one def with fields in order; malformed lines dropped ---
	TArray<FString> Lines;
	Lines.Add(TEXT("tex/rope_cable_cable.png 0 0 300 400 0 300 2.54 203.2 10 0.2 0 tex/rope_cable_cable_n.png 0"));
	Lines.Add(TEXT("# too few tokens -> skipped"));
	// "-" = no decoded texture; Type-2 + Dangling; $alphatest + $envmap (the cable/chain case)
	Lines.Add(TEXT("- 0 0 0 0 0 100 5 0 2 1 1 - 5"));
	Lines.Add(TEXT("- 0 0 0 0 0 100 5 0 99 1 0 - 0"));  // out-of-range node count -> clamped to 10
	Lines.Add(FString());   // blank -> skipped

	TArray<FElysiumRopeDef> Defs;
	FElysiumRopes::ParseLines(Lines, Defs);
	TestEqual(TEXT("three valid ropes parsed (two junk lines dropped)"), Defs.Num(), 3);

	if (Defs.Num() >= 1)
	{
		const FElysiumRopeDef& D = Defs[0];
		TestEqual(TEXT("texture path"), D.Tex, FString(TEXT("tex/rope_cable_cable.png")));
		TestTrue(TEXT("endpoint A parsed"), D.A.Equals(FVector(0, 0, 300)));
		TestTrue(TEXT("endpoint B parsed"), D.B.Equals(FVector(400, 0, 300)));
		TestEqual(TEXT("width cm"), D.WidthCm, 2.54f);
		TestEqual(TEXT("rest cm"), D.RestCm, 203.2f);
		TestEqual(TEXT("nodes"), D.Nodes, 10);
		TestEqual(TEXT("texscale"), D.TexScale, 0.2f);
		TestEqual(TEXT("flags"), static_cast<int32>(D.Flags), 0);
		TestEqual(TEXT("bump path"), D.Bump, FString(TEXT("tex/rope_cable_cable_n.png")));
		TestEqual(TEXT("matflags"), static_cast<int32>(D.MatFlags), 0);

		// --- rest-length contract: BuildRopes feeds RestCm straight into CableLength, and the
		// exporter has already resolved VtMB's own arithmetic into it. Rest *below* the straight
		// span is the normal case, not a bug: `RecomputeSprings` subtracts a flat 100 units, so a
		// 4 m span at 2.032 m rest is a taut cable the solver draws along the chord. ---
		TestTrue(TEXT("rest length below the span -> taut, no sag"),
			D.RestCm < static_cast<float>(FVector::Dist(D.A, D.B)));
	}

	if (Defs.Num() >= 2)
	{
		const FElysiumRopeDef& D = Defs[1];
		TestEqual(TEXT("dashed texture kept verbatim (runtime falls back to a plain MID)"),
			D.Tex, FString(TEXT("-")));
		// A Type-2 rope has two nodes, so BuildRopes gives it one span — a straight line that
		// cannot sag, which is the whole point of the type.
		TestEqual(TEXT("Type-2 rope keeps two nodes"), D.Nodes, 2);
		TestEqual(TEXT("Type-2 rope is one cable span"), FMath::Max(1, D.Nodes - 1), 1);
		TestTrue(TEXT("Dangling flag parsed"), (D.Flags & FElysiumRopeDef::Dangling) != 0);
		// $alphatest must survive to the runtime or BuildRopes instances the opaque master and
		// fills in the ~47% of the chain texture that is cut out between the links.
		TestTrue(TEXT("Masked matflag parsed"), (D.MatFlags & FElysiumRopeDef::Masked) != 0);
		TestTrue(TEXT("Envmap matflag parsed"), (D.MatFlags & FElysiumRopeDef::Envmap) != 0);
		TestFalse(TEXT("Translucent matflag not set"),
			(D.MatFlags & FElysiumRopeDef::Translucent) != 0);
		TestEqual(TEXT("dashed bump kept verbatim"), D.Bump, FString(TEXT("-")));
	}

	if (Defs.Num() >= 3)
	{
		// Activate() clamps m_nSegments to [2, 10]; the parser holds the same bound so a bad
		// sidecar cannot ask for an unbounded Verlet chain.
		TestEqual(TEXT("node count clamped to VtMB's ROPE_MAX_SEGMENTS"), Defs[2].Nodes, 10);
	}

	return true;
}

// =====================================================================================
// 7.4 world material set — ParseMtlLines reads every channel/flag the master-selection and
// param-binding depend on, and the blend flags stay mutually exclusive (the exporter writes at
// most one). No RHI, no assets: the factory's master choice is a pure function of these fields.
// =====================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumWorldMaterialsTest, "Elysium.Substrate.WorldMaterials", GElysiumTestFlags)
bool FElysiumWorldMaterialsTest::RunTest(const FString&)
{
	TArray<FString> Lines;
	// opaque with every optional feature channel present at once
	Lines.Add(TEXT("newmtl brick"));
	Lines.Add(TEXT("map_Kd tex/brick.png"));
	Lines.Add(TEXT("map_Ke tex/brick_ke.png"));
	Lines.Add(TEXT("bumpmap tex/brick_n.png"));
	Lines.Add(TEXT("envmap c0_0_0"));
	Lines.Add(TEXT("envmapmask tex/brick_envmask.png"));
	Lines.Add(TEXT("basetex2 tex/grass.png"));
	// masked (illum 4)
	Lines.Add(TEXT("newmtl fence"));
	Lines.Add(TEXT("map_Kd tex/fence.png"));
	Lines.Add(TEXT("illum 4"));
	// translucent (blend 1)
	Lines.Add(TEXT("newmtl glass"));
	Lines.Add(TEXT("map_Kd tex/glass.png"));
	Lines.Add(TEXT("blend 1"));
	Lines.Add(TEXT("glass 1"));
	Lines.Add(TEXT("bumpmap tex/glass_glass_n.png"));
	// additive
	Lines.Add(TEXT("newmtl neon"));
	Lines.Add(TEXT("map_Kd tex/neon.png"));
	Lines.Add(TEXT("additive 1"));
	// reflective, no explicit mask (uniform reflectivity)
	Lines.Add(TEXT("newmtl marble"));
	Lines.Add(TEXT("map_Kd tex/marble.png"));
	Lines.Add(TEXT("envmap cubemapdefault"));
	// 7.5 — a grey $envmaptint: a reflection-strength dim-down, not a metal
	Lines.Add(TEXT("newmtl dimtile"));
	Lines.Add(TEXT("map_Kd tex/dimtile.png"));
	Lines.Add(TEXT("envmap cubemapdefault"));
	Lines.Add(TEXT("envtint 0.5000 0.5000 0.5000"));
	// 7.5 — a chromatic $envmaptint: VtMB naming a metal (brass)
	Lines.Add(TEXT("newmtl brassrail"));
	Lines.Add(TEXT("map_Kd tex/brassrail.png"));
	Lines.Add(TEXT("envmap env_cubemap"));
	Lines.Add(TEXT("envmapmask tex/brassrail_envmask.png"));
	Lines.Add(TEXT("envtint 0.6500 0.5000 0.0000"));
	// 7.5 — a chromatic tint on a TRANSLUCENT surface is coloured glass, which stays dielectric
	Lines.Add(TEXT("newmtl bluepane"));
	Lines.Add(TEXT("map_Kd tex/bluepane.png"));
	Lines.Add(TEXT("blend 1"));
	Lines.Add(TEXT("envmap env_cubemap"));
	Lines.Add(TEXT("envtint 0.5000 0.6000 0.9000"));
	// Source Refract overlay: no albedo, only the converted DUDV normal + authored amount.
	Lines.Add(TEXT("newmtl rain_refract"));
	Lines.Add(TEXT("refract 0.010000"));
	Lines.Add(TEXT("refractmap tex/rain_refract_n.png"));

	TMap<FString, FElysiumMaterialDef> Mats;
	FElysiumObjModel::ParseMtlLines(Lines, Mats);
	TestEqual(TEXT("nine materials parsed"), Mats.Num(), 9);

	if (const FElysiumMaterialDef* B = Mats.Find(TEXT("brick")))
	{
		TestEqual(TEXT("brick albedo"), B->Albedo, FString(TEXT("tex/brick.png")));
		TestEqual(TEXT("brick emissive"), B->Emissive, FString(TEXT("tex/brick_ke.png")));
		TestEqual(TEXT("brick bump"), B->Bump, FString(TEXT("tex/brick_n.png")));
		TestEqual(TEXT("brick envmask"), B->EnvMask, FString(TEXT("tex/brick_envmask.png")));
		TestEqual(TEXT("brick basetex2"), B->BaseTex2, FString(TEXT("tex/grass.png")));
		TestTrue(TEXT("brick is reflective"), B->bEnvmap);
		TestTrue(TEXT("brick is opaque (no blend flag)"), !B->bBlend && !B->bScissor && !B->bAdditive);
	}
	if (const FElysiumMaterialDef* F = Mats.Find(TEXT("fence")))
	{
		TestTrue(TEXT("fence is masked"), F->bScissor && !F->bBlend && !F->bAdditive);
	}
	if (const FElysiumMaterialDef* G = Mats.Find(TEXT("glass")))
	{
		TestTrue(TEXT("glass is translucent"), G->bBlend && !G->bScissor && !G->bAdditive);
		TestTrue(TEXT("glass semantic parsed"), G->bGlass);
		TestEqual(TEXT("glass normal parsed"), G->Bump, FString(TEXT("tex/glass_glass_n.png")));
	}
	if (const FElysiumMaterialDef* N = Mats.Find(TEXT("neon")))
	{
		TestTrue(TEXT("neon is additive"), N->bAdditive && !N->bBlend && !N->bScissor);
	}
	if (const FElysiumMaterialDef* M = Mats.Find(TEXT("marble")))
	{
		TestTrue(TEXT("marble is reflective with no explicit mask"), M->bEnvmap && M->EnvMask.IsEmpty());
		// An unauthored $envmaptint is white, so the grey path is a multiply by 1.
		TestEqual(TEXT("marble tint defaults to white"), M->EnvTint, FLinearColor::White);
		TestEqual(TEXT("marble tint luma is unity"), M->TintLuma(), 1.f, 1e-4f);
		TestFalse(TEXT("marble is not a metal"), M->IsChromatic());
	}
	// 7.5 — $envmaptint splits two ways, and the split is what decides Metallic. The population
	// is bimodal (docs/vtmb/reflections.md), so these are the two sides plus the glass exclusion.
	if (const FElysiumMaterialDef* D = Mats.Find(TEXT("dimtile")))
	{
		TestFalse(TEXT("a grey tint is not a metal"), D->IsChromatic());
		TestEqual(TEXT("grey tint luma dims the reflection"), D->TintLuma(), 0.5f, 1e-3f);
	}
	if (const FElysiumMaterialDef* Br = Mats.Find(TEXT("brassrail")))
	{
		TestTrue(TEXT("a chromatic tint on an opaque surface is a metal"), Br->IsChromatic());
		TestEqual(TEXT("brass tint parsed"), Br->EnvTint, FLinearColor(0.65f, 0.5f, 0.f));
	}
	if (const FElysiumMaterialDef* Bp = Mats.Find(TEXT("bluepane")))
	{
		// Chromatic, but translucent: coloured glass, not brass. Metalness comes off VtMB's own
		// authoring, and a $translucent surface is never it.
		TestTrue(TEXT("bluepane tint is chromatic"),
			(Bp->EnvTint.B - Bp->EnvTint.R) >= FElysiumMaterialDef::ChromaticSpread);
		TestFalse(TEXT("tinted glass stays dielectric"), Bp->IsChromatic());
	}
	if (const FElysiumMaterialDef* R = Mats.Find(TEXT("rain_refract")))
	{
		TestTrue(TEXT("Source Refract semantic parsed"), R->bRefract);
		TestEqual(TEXT("Source Refract amount parsed"), R->RefractAmount, 0.01f, 1e-6f);
		TestEqual(TEXT("Source Refract map parsed"), R->RefractMap,
			FString(TEXT("tex/rain_refract_n.png")));
		TestTrue(TEXT("Source Refract is not a generic blend flag"),
			!R->bBlend && !R->bScissor && !R->bAdditive && !R->bGlass);
	}
	return true;
}

} // namespace ElysiumWorldDataTests

#endif // WITH_DEV_AUTOMATION_TESTS
