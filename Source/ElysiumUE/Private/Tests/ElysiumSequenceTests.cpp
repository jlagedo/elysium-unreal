// Content-free Substrate automation: sky and fog packing plus scripted-sequence state and locomotion.
#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

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
#include "Substrate/ElysiumNpcThinkCadence.h"   // the spawn clock's first-think delay
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
#include "ElysiumSurfaceParams.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ElysiumEventQueue.h"
#include "ElysiumWireReport.h"
#include "ElysiumExpr.h"
#include "ElysiumGaitSpeeds.h"               // the animation's per-direction speed
#include "ElysiumGameClock.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumSessionSubsystem.h"
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
#include "Tests/ElysiumEntityDebugStateTestHelpers.h"
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
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// One context flag (runs anywhere) + the product filter (this project's own suite bucket).
// EAutomationTestFlags is a strong enum in 5.8, so the constant carries that type (ENUM_CLASS_FLAGS
// makes the `|` yield an EAutomationTestFlags), not int32.
namespace ElysiumSequenceTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// =====================================================================================
// The sky cube face->slice transform (sky-ambience B3). Checks BuildSkyCube's table against
// the two conventions it was derived from, not against itself: for every Unreal cube slice
// and a grid of its texels, the direction UE's own GetCubemapVector assigns that texel must
// be the direction VtMB's draw tables assign the source pixel the transform reads from.
// A wrong face binding, a wrong rotation or a mirror each break it.
// =====================================================================================

namespace
{
	// K1, carried into Unreal space by source_to_unreal (x, -y, z): the direction a Source sky
	// face's image pixel (u, v) looks along, v = 0 the top row.
	// docs/vtmb/sky-ambience.md -> "K1 ... (settled)" / B3.
	FVector ElysiumK1FaceDir(const FString& Face, double U, double V)
	{
		const double S = 2.0 * U - 1.0;
		const double T = 1.0 - 2.0 * V;
		if (Face == TEXT("rt")) { return FVector( 1,  S,  T); }
		if (Face == TEXT("lf")) { return FVector(-1, -S,  T); }
		if (Face == TEXT("bk")) { return FVector( S, -1,  T); }
		if (Face == TEXT("ft")) { return FVector(-S,  1,  T); }
		if (Face == TEXT("up")) { return FVector(-T,  S,  1); }
		return FVector(T, S, -1);   // dn
	}

	// K2, GetCubemapVector (ReflectionEnvironmentShaders.usf): the raw Unreal world direction of
	// slice texel (U, V), U growing right and V growing down over the slice's own texels.
	FVector ElysiumK2SliceDir(int32 Slice, double U, double V)
	{
		const double SX = 2.0 * U - 1.0;
		const double SY = 2.0 * V - 1.0;
		switch (Slice)
		{
		case 0:  return FVector( 1, -SY, -SX);
		case 1:  return FVector(-1, -SY,  SX);
		case 2:  return FVector(SX,   1,  SY);
		case 3:  return FVector(SX,  -1, -SY);
		case 4:  return FVector(SX, -SY,   1);
		default: return FVector(-SX, -SY, -1);
		}
	}
}

#if ELYSIUM_WITH_ARM_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSkyCubeTest, "Elysium.Arm.SkyCube", GElysiumTestFlags)
bool FElysiumSkyCubeTest::RunTest(const FString&)
{
	// Every slice takes a distinct face, and together they are the whole set.
	TSet<FString> Faces;
	for (int32 Slice = 0; Slice < 6; ++Slice)
	{
		Faces.Add(ElysiumEnvironment::SkySliceFace(Slice));
	}
	TestEqual(TEXT("the six slices take six distinct faces"), Faces.Num(), 6);
	for (const TCHAR* Face : { TEXT("rt"), TEXT("lf"), TEXT("ft"), TEXT("bk"), TEXT("up"), TEXT("dn") })
	{
		TestTrue(FString::Printf(TEXT("face %s is bound to a slice"), Face), Faces.Contains(Face));
	}

	// N is odd and > 1 so the sample grid includes the centre and both parities of edge texel;
	// a rotation that happened to be its own inverse on an even grid still has to survive.
	const int32 N = 7;
	for (int32 Slice = 0; Slice < 6; ++Slice)
	{
		const FString Face = ElysiumEnvironment::SkySliceFace(Slice);
		double Worst = 0.0;
		for (int32 Y = 0; Y < N; ++Y)
		{
			for (int32 X = 0; X < N; ++X)
			{
				int32 SX = 0, SY = 0;
				ElysiumEnvironment::SkySliceSource(Slice, X, Y, N, SX, SY);
				TestTrue(TEXT("the source texel is inside the face"),
					SX >= 0 && SX < N && SY >= 0 && SY < N);

				// Texel centres on both sides: the cube samples a slice texel, the transform
				// hands it the pixel of the decoded face that must carry that direction.
				const FVector Want = ElysiumK2SliceDir(Slice, (X + 0.5) / N, (Y + 0.5) / N).GetSafeNormal();
				const FVector Got = ElysiumK1FaceDir(Face, (SX + 0.5) / N, (SY + 0.5) / N).GetSafeNormal();
				Worst = FMath::Max(Worst, 1.0 - FVector::DotProduct(Want, Got));
			}
		}
		TestTrue(FString::Printf(
			TEXT("slice %d (%s): every texel looks where VtMB's tables say (worst 1-dot %g)"),
			Slice, *Face, Worst), Worst < 1e-12);
	}

	return true;
}

// =====================================================================================
// The per-primitive fog packing (sky-ambience B8b). The whole term rests on one property:
// an unwritten custom-primitive-data slot reads as zero, and zero must mean "not fogged" —
// so the material stays neutral on anything nobody stamped, with no branch to get wrong.
// This checks the packing keeps that property, and reproduces Source's own factor.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFogPackTest, "Elysium.Arm.FogPack", GElysiumTestFlags)
bool FElysiumFogPackTest::RunTest(const FString&)
{
	// The slots the material graph reads (pipeline/unreal/mat_fog.py) and the bake writes
	// (pipeline/unreal/bake_map.py FOG_CPD_*). A colour is a float4, so it owns 0..3.
	TestEqual(TEXT("colour is the first slot"), ElysiumFog::SlotColor, 0);
	TestEqual(TEXT("start follows the colour's float4"), ElysiumFog::SlotStart, 4);
	TestEqual(TEXT("inverse range follows start"), ElysiumFog::SlotInvRange, 5);
	TestEqual(TEXT("six floats in all"), ElysiumFog::NumFloats, 6);

	const FLinearColor Color(0.25f, 0.5f, 0.75f, 1.f);
	TArray<float> Data;

	// A map that authored fog: the factor is Source's own saturate((d - start) / (end - start)).
	ElysiumFog::Pack(true, Color, 1270.f, 12700.f, Data);
	TestEqual(TEXT("packed float count"), Data.Num(), ElysiumFog::NumFloats);
	// The authored colour is gamma-encoded, like every VtMB colour, and lands linear.
	for (int32 C = 0; C < 3; ++C)
	{
		TestTrue(FString::Printf(TEXT("colour channel %d decodes to linear"), C),
			FMath::IsNearlyEqual(Data[ElysiumFog::SlotColor + C],
				FMath::Pow(Color.Component(C), 2.2f), 1e-6f));
	}
	TestEqual(TEXT("start passes through"), Data[ElysiumFog::SlotStart], 1270.f);
	TestTrue(TEXT("the inverse range spans start->end"),
		FMath::IsNearlyEqual(Data[ElysiumFog::SlotInvRange], 1.f / (12700.f - 1270.f), 1e-9f));
	// At `end` the surface is gone and only the fog's own colour is left; at `start`, nothing yet.
	TestTrue(TEXT("f is 1 at the authored end"), FMath::IsNearlyEqual(
		(12700.f - Data[ElysiumFog::SlotStart]) * Data[ElysiumFog::SlotInvRange], 1.f, 1e-5f));
	TestEqual(TEXT("f is 0 at the authored start"),
		(1270.f - Data[ElysiumFog::SlotStart]) * Data[ElysiumFog::SlotInvRange], 0.f);

	// The three ways a map says "no fog" must all land on the zero an unwritten slot reads as:
	// the flag is off (23 of the 43 sky_camera maps), or the range is degenerate.
	for (const TTuple<bool, float, float>& Off : {
			MakeTuple(false, 1270.f, 12700.f),   // authored, but fogenable 0
			MakeTuple(true, 12700.f, 12700.f),   // start == end
			MakeTuple(true, 12700.f, 1270.f) })  // end before start
	{
		ElysiumFog::Pack(Off.Get<0>(), Color, Off.Get<1>(), Off.Get<2>(), Data);
		TestEqual(TEXT("an unfogged set packs the same zero an unwritten slot reads"),
			Data[ElysiumFog::SlotInvRange], 0.f);
	}

	return true;
}

// =====================================================================================
// The decal axis: a
// UDecalComponent carries no Custom Primitive Data, so the same fog values the CPD path packs
// for a mesh primitive are applied to an MID's three named instance parameters instead. This
// checks ApplyToDecalMID reproduces Pack's own numbers on those parameters, content-free (the
// engine's own default material needs no project asset).
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFogDecalMIDTest, "Elysium.Arm.FogDecalMID", GElysiumTestFlags)
bool FElysiumFogDecalMIDTest::RunTest(const FString&)
{
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(
		UMaterial::GetDefaultMaterial(MD_Surface), GetTransientPackage());
	if (!TestNotNull(TEXT("a transient MID off the engine default material"), Mid))
	{
		return false;
	}

	const FLinearColor Color(0.25f, 0.5f, 0.75f, 1.f);
	ElysiumFog::ApplyToDecalMID(Mid, /*bEnabled=*/true, Color, 1270.f, 12700.f);

	FLinearColor GotColor = FLinearColor::Black;
	TestTrue(TEXT("FogColor lands on the instance"),
		Mid->GetVectorParameterValue(ElysiumSurfaceParamsDecal::Vectors::FogColor, GotColor));
	for (int32 C = 0; C < 3; ++C)
	{
		TestTrue(FString::Printf(TEXT("FogColor channel %d matches Pack's own decode"), C),
			FMath::IsNearlyEqual(GotColor.Component(C), FMath::Pow(Color.Component(C), 2.2f), 1e-6f));
	}

	float GotStart = 0.f, GotInvRange = 0.f;
	TestTrue(TEXT("FogStart lands on the instance"),
		Mid->GetScalarParameterValue(ElysiumSurfaceParamsDecal::Scalars::FogStart, GotStart));
	TestTrue(TEXT("FogInvRange lands on the instance"),
		Mid->GetScalarParameterValue(ElysiumSurfaceParamsDecal::Scalars::FogInvRange, GotInvRange));
	TestEqual(TEXT("start passes through"), GotStart, 1270.f);
	TestTrue(TEXT("the inverse range spans start->end"),
		FMath::IsNearlyEqual(GotInvRange, 1.f / (12700.f - 1270.f), 1e-9f));

	// A disabled fog set must land the same zero an unwritten CPD slot reads, so a decal with no
	// map fog is untouched -- same "neutral by construction" property ApplyToDecalMID reuses from
	// Pack, now checked on the instance API rather than the packed float array.
	ElysiumFog::ApplyToDecalMID(Mid, /*bEnabled=*/false, Color, 1270.f, 12700.f);
	Mid->GetScalarParameterValue(ElysiumSurfaceParamsDecal::Scalars::FogInvRange, GotInvRange);
	TestEqual(TEXT("a disabled fog set zeroes the inverse range on the instance too"),
		GotInvRange, 0.f);

	return true;
}
#endif // ELYSIUM_WITH_ARM_TESTS

// =====================================================================================
// scripted_sequence — the scene, driven through the real queue and run by the NPC's own program
// (`SelectSchedule` `0x1028a380` case 4 -> `SCHED_AISCRIPT 0x2e` -> `TranslateSchedule 0x102cc080`).
//
// No skeletal body here, so no walk sequence and no action animation: `TASK_WALK_TO_TARGET` completes
// at once (`0x1028405d`) and `TASK_PLANT_ON_SCRIPT` plants the NPC on the mark (`0x10286b7b`). This
// covers the half every map depends on — the mark, OnBeginSequence/OnEndSequence, and the
// m_iszNextScript chain. A beat
// with no `m_iszPlay` is zero-length (59 of the 108 exported sequences are), so the whole chain
// settles within a few ticks once `IsTimeToStart` (`0x101a7540`, `m_startTime = input + 0.05`)
// opens. The target is an NPC: `FindEntity` (`0x101a7600`) admits only an entity with `+0x94`.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScriptedSequenceTest,
	"Elysium.Substrate.ScriptedSequence", GElysiumTestFlags)
bool FElysiumScriptedSequenceTest::RunTest(const FString&)
{
	// Builds one scripted_sequence def aimed at `mover1`, an NPC (retail's `FindEntity` admits only a
	// `CAI_BaseNPC`; a point entity by that name is skipped).
	auto MakeSeq = [](const TCHAR* Name, const TCHAR* MoveTo, const FVector& At, const TCHAR* SpawnFlags)
	{
		FElysiumEntityDef Seq;
		Seq.Classname = TEXT("scripted_sequence");
		Seq.TargetName = Name;
		Seq.Origin = At;
		Seq.Keys.Add(TEXT("m_iszEntity"), TEXT("mover1"));
		Seq.Keys.Add(TEXT("m_fMoveTo"), MoveTo);
		Seq.Keys.Add(TEXT("angles"), TEXT("0 90 0"));
		Seq.Keys.Add(TEXT("spawnflags"), SpawnFlags);
		return Seq;
	};
	auto Wire = [](FElysiumEntityDef& On, const TCHAR* Output, const TCHAR* Input, const TCHAR* Param)
	{
		FElysiumOutputDef W;
		W.Name = Output;
		W.Target = TEXT("counter1");
		W.Input = Input;
		W.Param = Param;
		On.Outputs.Add(W);
	};

	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__test__");

	const FVector Mark(100.f, 200.f, 300.f);
	FElysiumEntityDef Seq1 = MakeSeq(TEXT("seq1"), TEXT("1"), Mark, TEXT("0"));
	Seq1.Keys.Add(TEXT("m_iszNextScript"), TEXT("seq2"));
	Wire(Seq1, TEXT("OnBeginSequence"), TEXT("Add"), TEXT("1"));
	Wire(Seq1, TEXT("OnEndSequence"), TEXT("Add"), TEXT("2"));
	Defs.Defs.Add(MoveTemp(Seq1));

	// The chained beat. m_fMoveTo 0 means "already in place", so it must NOT move the target — which
	// is how the test tells the chain fired without also re-placing.
	FElysiumEntityDef Seq2 = MakeSeq(TEXT("seq2"), TEXT("0"), FVector(-999.f, -999.f, -999.f), TEXT("0"));
	Wire(Seq2, TEXT("OnEndSequence"), TEXT("Add"), TEXT("4"));
	Defs.Defs.Add(MoveTemp(Seq2));

	// The mapper's "don't move the NPC" override (HL1 CCineMonster SF_SCRIPT_NOSCRIPTMOVEMENT).
	FElysiumEntityDef Seq3 = MakeSeq(TEXT("seq3"), TEXT("1"), FVector(-777.f, -777.f, -777.f), TEXT("128"));
	Defs.Defs.Add(MoveTemp(Seq3));

	FElysiumEntityDef Mover;
	Mover.Classname = TEXT("npc_VHumanCombatant");
	Mover.TargetName = TEXT("mover1");
	Mover.Keys.Add(TEXT("stattemplate"), TEXT("Thug"));
	// A model, so the NPC stands a body and the body a motor, as every spawned NPC does.
	Mover.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/jack/Jack.mdl"));
	Defs.Defs.Add(MoveTemp(Mover));

	FElysiumEntityDef Counter;
	Counter.Classname = TEXT("math_counter");
	Counter.TargetName = TEXT("counter1");
	Defs.Defs.Add(MoveTemp(Counter));

	// The scene's program faces the NPC to the mark (`TASK_FACE_SCRIPT 0x66`), whose run arm completes
	// only on `FacingIdeal 0x10278c80` (`0x10288b5b`): the body has to turn, so the NPC stands on a
	// motor that turns it.
	FElysiumRecordingServices Services;
	Services.bProvideNpcMotor = true;
	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr, Services.Bundle());
	ElysiumStandSpawnClock(World, -ElysiumNpcThink::InitThinkDelay);
	World.Load(MoveTemp(Defs));
	World.Activate(-ElysiumNpcThink::InitThinkDelay);
	// The first think — and the mind admission every body claim needs — falls at
	// `curtime + 0.1`: `CAI_BaseNPCTroika::NPCInit` (`0x1029a0b0`) arms `m_flNextThink`
	// there on the map's first second (`_DAT_104493d0`).
	World.Tick(0.0);

	FElysiumEntity* Seq = World.FindByName(TEXT("seq1"));
	FElysiumEntity* Target = World.FindByName(TEXT("mover1"));
	FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
	FElysiumRecordingNpcMotor* Motor = Services.LastNpcMotor();
	if (!TestNotNull(TEXT("seq1 registers a leaf class"), Seq) ||
		!TestNotNull(TEXT("mover1 resolved"), Target) ||
		!TestNotNull(TEXT("counter1 resolved"), Count) ||
		!TestNotNull(TEXT("mover1 stands on a motor"), Motor))
	{
		return false;
	}
	// Every walk arrives (the navigator's goal reached), and the body turns where it is told to.
	Motor->SampleStatus = EElysiumNpcMoveStatus::Reached;
	Motor->bSettlesFacing = true;
	TestFalse(TEXT("scripted_sequence is not an inert record"), Seq->IsRecordOnly());

	auto CounterValue = [](const FElysiumEntity* Entity) -> float
	{
		return ElysiumEntityDebugTest::CounterValue(Entity);
	};

	TestTrue(TEXT("the target starts at the origin"), Target->Origin.IsNearlyZero());

	double Now = 0.0;
	World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), /*Delay*/ 0.0,
		FElysiumEntityHandle::Invalid(), Seq->Handle);
	World.Tick(Now);
	// `OnBeginSequence` fires from `StartScript` (`0x101a81a0`) once the NPC stands on its mark and the
	// start gate opens — NOT at the input.
	TestEqual(TEXT("nothing fires at the input"), CounterValue(Count), 0.f);
	for (int32 i = 0; i < 12; ++i)
	{
		Now += 0.1;
		World.Tick(Now);
	}

	// Both of seq1's outputs fired, then the chain (`Finish` `0x101a8640`: `m_hNextCine` possesses the
	// NPC directly) carried seq2's.
	TestEqual(TEXT("OnBeginSequence + OnEndSequence + the chained beat all fired"),
		CounterValue(Count), 7.f);
	// m_fMoveTo 1 placed the target on seq1's mark; seq2's m_fMoveTo 0 left it there.
	TestTrue(TEXT("the target was placed on the mark"), Target->Origin.Equals(Mark, 0.01));
	TestTrue(TEXT("the mark's facing was applied"),
		FMath::IsNearlyEqual(Target->Angles.Y, 90.f, 0.01f));

	// Spawnflag 128 (HL1's NOSCRIPTMOVEMENT) is NOT a movement gate in VtMB: the scripted schedules
	// split by `m_fMoveTo` alone (`0x102cc080`), and `CineCleanup` (`0x1027d170`) is its one reader
	// (it skips the bone-0 placement). The beat moves its NPC like any other.
	FElysiumEntity* NoMove = World.FindByName(TEXT("seq3"));
	if (TestNotNull(TEXT("seq3 resolved"), NoMove))
	{
		World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), NoMove->Handle);
		for (int32 i = 0; i < 6; ++i)
		{
			Now += 0.1;
			World.Tick(Now);
		}
		TestTrue(TEXT("spawnflag 128 still places the target on the mark"),
			Target->Origin.Equals(FVector(-777.f, -777.f, -777.f), 0.01));
	}

	// A sequence naming `!playercontroller` with no stand-in standing finds nothing, and retail's
	// `BeginSequence` then does nothing at all: no think, no output (`0x101a7412 JZ 0x101a74a9`).
	FElysiumEntityDefs PlayerDefs;
	PlayerDefs.MapName = TEXT("__test2__");
	FElysiumEntityDef PlayerSeq = MakeSeq(TEXT("pseq"), TEXT("1"), Mark, TEXT("0"));
	PlayerSeq.Keys.Add(TEXT("m_iszEntity"), TEXT("!playercontroller"));
	Wire(PlayerSeq, TEXT("OnEndSequence"), TEXT("Add"), TEXT("9"));
	PlayerDefs.Defs.Add(MoveTemp(PlayerSeq));
	FElysiumEntityDef Counter2;
	Counter2.Classname = TEXT("math_counter");
	Counter2.TargetName = TEXT("counter1");
	PlayerDefs.Defs.Add(MoveTemp(Counter2));

	FElysiumEntityWorld World2(nullptr, nullptr);
	ElysiumStandSpawnClock(World2, -ElysiumNpcThink::InitThinkDelay);
	World2.Load(MoveTemp(PlayerDefs));
	World2.Activate(-ElysiumNpcThink::InitThinkDelay);
	// The first think — and the mind admission every body claim needs — falls at
	// `curtime + 0.1`: `CAI_BaseNPCTroika::NPCInit` (`0x1029a0b0`) arms `m_flNextThink`
	// there on the map's first second (`_DAT_104493d0`).
	World2.Tick(0.0);
	FElysiumEntity* PSeq = World2.FindByName(TEXT("pseq"));
	FElysiumEntity* Count2 = World2.FindByName(TEXT("counter1"));
	if (TestNotNull(TEXT("pseq resolved"), PSeq) && TestNotNull(TEXT("counter1 resolved"), Count2))
	{
		World2.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), PSeq->Handle);
		double Now2 = 0.0;
		for (int32 i = 0; i < 6; ++i)
		{
			World2.Tick(Now2);
			Now2 += 0.1;
		}
		TestEqual(TEXT("a beat whose target is not found fires nothing"), CounterValue(Count2), 0.f);
	}

	return true;
}

// =====================================================================================
// The three VtMB spawnflag additions on CCineNPC (`docs/vtmb/entity_io.md`): 256 holds the
// post-idle so the beat never completes (its `OnEndSequence` still fires), 512 keeps a QUEUED beat
// from being kicked out of the queue, and 4096 turns off character collision for the beat's duration. sp_theatre's courtroom
// walk-out authors all three — `0x1260` on the five who walk, `0x360` on the two who stand.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScriptedSequenceFlagsTest,
	"Elysium.Substrate.ScriptedSequenceFlags", GElysiumTestFlags)
bool FElysiumScriptedSequenceFlagsTest::RunTest(const FString&)
{
	// One NPC, and two beats aimed at it so the queue rules have something to arbitrate.
	// `MoveTo` 1 keeps a beat alive in its travel phase for as long as the motor reports Moving,
	// which is the only way to observe state a beat holds *while running*; `MoveTo` 0 with no action
	// animation is the in-place shape Ash and Damsel use, and ends in the pass that starts it.
	auto BuildDefs = [](FElysiumEntityDefs& Defs, int32 FirstFlags, const TCHAR* PostIdle, int32 MoveTo)
	{
		Defs.MapName = TEXT("__flags__");

		FElysiumEntityDef Npc;
		Npc.Classname = TEXT("npc_VVampire");
		Npc.TargetName = TEXT("Damsel");
		Npc.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/downtown/damsel/damsel.mdl"));
		Defs.Defs.Add(MoveTemp(Npc));

		FElysiumEntityDef Seq;
		Seq.Classname = TEXT("scripted_sequence");
		Seq.TargetName = TEXT("beat_a");
		Seq.Origin = FVector(1000.f, 0.f, 0.f);
		Seq.Keys.Add(TEXT("m_iszEntity"), TEXT("Damsel"));
		Seq.Keys.Add(TEXT("m_fMoveTo"), *FString::FromInt(MoveTo));
		Seq.Keys.Add(TEXT("spawnflags"), *FString::FromInt(FirstFlags));
		if (PostIdle && *PostIdle)
		{
			Seq.Keys.Add(TEXT("m_iszPostIdle"), PostIdle);
		}
		FElysiumOutputDef W;
		W.Name = TEXT("OnEndSequence");
		W.Target = TEXT("counter1");
		W.Input = TEXT("Add");
		W.Param = TEXT("5");
		Seq.Outputs.Add(W);
		Defs.Defs.Add(MoveTemp(Seq));

		FElysiumEntityDef Other;
		Other.Classname = TEXT("scripted_sequence");
		Other.TargetName = TEXT("beat_b");
		Other.Keys.Add(TEXT("m_iszEntity"), TEXT("Damsel"));
		Other.Keys.Add(TEXT("m_fMoveTo"), TEXT("0"));
		FElysiumOutputDef W2;
		W2.Name = TEXT("OnBeginSequence");
		W2.Target = TEXT("counter1");
		W2.Input = TEXT("Add");
		W2.Param = TEXT("100");
		Other.Outputs.Add(W2);
		Defs.Defs.Add(MoveTemp(Other));

		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = TEXT("counter1");
		Defs.Defs.Add(MoveTemp(Counter));
	};

	auto CounterValue = [](const FElysiumEntity* Entity) -> float
	{
		return ElysiumEntityDebugTest::CounterValue(Entity);
	};

	auto Begin = [](FElysiumEntityWorld& World, FElysiumEntity* Seq)
	{
		World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), Seq->Handle);
	};

	// --- 4096: the beat borrows the NPC's character collision and gives it back --------------
	// The walk-out's own shape: `0x1260` on a walking beat (NOINTERRUPT | OVERRIDESTATE | priority
	// | ignore-collision), which is what lets five NPCs share one aisle.
	{
		FElysiumEntityDefs Defs;
		BuildDefs(Defs, /*spawnflags*/ 0x1260, nullptr, /*m_fMoveTo*/ 1);

		FElysiumRecordingServices Services;
		Services.bProvideNpcMotor = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		ElysiumStandSpawnClock(World, -ElysiumNpcThink::InitThinkDelay);
		World.Load(MoveTemp(Defs));
		World.Activate(-ElysiumNpcThink::InitThinkDelay);
		// The first think — and the mind admission every body claim needs — falls at
		// `curtime + 0.1`: `CAI_BaseNPCTroika::NPCInit` (`0x1029a0b0`) arms `m_flNextThink`
		// there on the map's first second (`_DAT_104493d0`).
		World.Tick(0.0);

		FElysiumEntity* Seq = World.FindByName(TEXT("beat_a"));
		FElysiumRecordingNpcMotor* Motor = Services.LastNpcMotor();
		if (!TestNotNull(TEXT("beat_a resolved"), Seq)
			|| !TestNotNull(TEXT("Damsel stands on a motor"), Motor))
		{
			return false;
		}
		TestFalse(TEXT("collision is ordinary before the beat"), Motor->bIgnoreCharacterCollision);

		double Now = 0.0;
		Begin(World, Seq);
		World.Tick(Now); Now += 0.1;
		TestTrue(TEXT("4096 turns character collision off for the beat"),
			Motor->bIgnoreCharacterCollision);

		// The body reaches the mark; with no action animation the beat then ends, and must hand
		// the borrowed collision back with it.
		Motor->SampleStatus = EElysiumNpcMoveStatus::Reached;
		for (int32 i = 0; i < 40; ++i) { World.Tick(Now); Now += 0.1; }
		TestFalse(TEXT("and gives it back when the beat ends"), Motor->bIgnoreCharacterCollision);
	}

	// --- 4096: a cancelled beat gives it back too --------------------------------------------
	{
		FElysiumEntityDefs Defs;
		BuildDefs(Defs, /*spawnflags*/ 0x360 | 4096, TEXT("Converse_Normal_Talk_A"), /*m_fMoveTo*/ 0);

		FElysiumRecordingServices Services;
		Services.bProvideNpcMotor = true;
		// Damsel's model authors the post-idle, so `StartSequence`'s `LookupSequence` (`0x101a830d`)
		// finds it and the hold plays it (30 s here, longer than the case runs). Unseeded, the lookup
		// misses and retail would play the model's sequence 0 for its own length, which the sequence
		// bridge cannot yet name (N19, `stories/v1/triage.md`).
		Services.ClipSeconds = 30.f;
		Services.KnownNpcClips.FindOrAdd(TEXT("damsel")).Add(TEXT("Converse_Normal_Talk_A"));
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		ElysiumStandSpawnClock(World, -ElysiumNpcThink::InitThinkDelay);
		World.Load(MoveTemp(Defs));
		World.Activate(-ElysiumNpcThink::InitThinkDelay);
		// The first think — and the mind admission every body claim needs — falls at
		// `curtime + 0.1`: `CAI_BaseNPCTroika::NPCInit` (`0x1029a0b0`) arms `m_flNextThink`
		// there on the map's first second (`_DAT_104493d0`).
		World.Tick(0.0);

		FElysiumEntity* Seq = World.FindByName(TEXT("beat_a"));
		FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
		FElysiumRecordingNpcMotor* Motor = Services.LastNpcMotor();
		if (!TestNotNull(TEXT("beat_a resolved"), Seq) || !TestNotNull(TEXT("counter1 resolved"), Count)
			|| !TestNotNull(TEXT("Damsel stands on a motor"), Motor))
		{
			return false;
		}

		double Now = 0.0;
		Begin(World, Seq);
		for (int32 i = 0; i < 6; ++i) { World.Tick(Now); Now += 0.1; }

		// 256: `OnEndSequence` fires from `SequenceDone` (`0x101a8460`) UNCONDITIONALLY, before the
		// post-idle; `Finish` (`0x101a8640`) then holds the post-idle and the beat never completes.
		TestEqual(TEXT("256 does not suppress OnEndSequence"), CounterValue(Count), 5.f);
		TestTrue(TEXT("a held beat keeps the collision it borrowed"),
			Motor->bIgnoreCharacterCollision);

		World.EnqueueInput(TEXT("beat_a"), FName(TEXT("CancelSequence")), FElysiumVariant::Void(),
			0.0, {}, {});
		for (int32 i = 0; i < 3; ++i) { World.Tick(Now); Now += 0.1; }
		TestEqual(TEXT("cancelling a held beat fires no second OnEndSequence"),
			CounterValue(Count), 5.f);
		TestFalse(TEXT("cancelling releases the borrowed collision"),
			Motor->bIgnoreCharacterCollision);
	}

	// --- 512: a QUEUED priority beat cannot be kicked out of the queue ------------------------
	// `CanOverride` (`0x101a8ac0`) reads spawnflag `0x200` on the cine queued as the holder's
	// `m_hNextCine`, not on the holder: a priority beat waiting its turn refuses a later challenger.
	{
		FElysiumEntityDefs Defs;
		BuildDefs(Defs, /*spawnflags*/ 0x160, TEXT("Converse_Normal_Talk_A"), /*m_fMoveTo*/ 0);
		// beat_b becomes the queued priority beat; beat_c is the late challenger.
		for (FElysiumEntityDef& Def : Defs.Defs)
		{
			if (Def.TargetName == TEXT("beat_b"))
			{
				Def.Keys.Add(TEXT("spawnflags"), TEXT("576"));   // 0x200 | 0x40
			}
		}
		FElysiumEntityDef Late;
		Late.Classname = TEXT("scripted_sequence");
		Late.TargetName = TEXT("beat_c");
		Late.Keys.Add(TEXT("m_iszEntity"), TEXT("Damsel"));
		Late.Keys.Add(TEXT("m_fMoveTo"), TEXT("0"));
		Late.Keys.Add(TEXT("spawnflags"), TEXT("64"));
		FElysiumOutputDef W3;
		W3.Name = TEXT("OnBeginSequence");
		W3.Target = TEXT("counter1");
		W3.Input = TEXT("Add");
		W3.Param = TEXT("1000");
		Late.Outputs.Add(W3);
		Defs.Defs.Add(MoveTemp(Late));

		FElysiumRecordingServices Services;
		Services.bProvideNpcMotor = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		ElysiumStandSpawnClock(World, -ElysiumNpcThink::InitThinkDelay);
		World.Load(MoveTemp(Defs));
		World.Activate(-ElysiumNpcThink::InitThinkDelay);
		World.Tick(0.0);

		FElysiumEntity* First = World.FindByName(TEXT("beat_a"));
		FElysiumEntity* Second = World.FindByName(TEXT("beat_b"));
		FElysiumEntity* Third = World.FindByName(TEXT("beat_c"));
		FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
		if (!TestNotNull(TEXT("beat_a resolved"), First) || !TestNotNull(TEXT("beat_b resolved"), Second)
			|| !TestNotNull(TEXT("beat_c resolved"), Third) || !TestNotNull(TEXT("counter1 resolved"), Count))
		{
			return false;
		}

		double Now = 0.0;
		Begin(World, First);
		for (int32 i = 0; i < 6; ++i) { World.Tick(Now); Now += 0.1; }
		const float AfterFirst = CounterValue(Count);   // beat_a's OnEndSequence (5)

		// Both challengers in one pass: beat_b queues behind the held beat_a, and beat_c is refused
		// because the queued beat_b is a priority script.
		Begin(World, Second);
		Begin(World, Third);
		for (int32 i = 0; i < 10; ++i) { World.Tick(Now); Now += 0.1; }
		TestEqual(TEXT("the queued priority beat runs once the holder yields; the late challenger never"),
			CounterValue(Count) - AfterFirst, 100.f);
	}

	return true;
}

} // namespace ElysiumSequenceTests

#endif // WITH_DEV_AUTOMATION_TESTS
