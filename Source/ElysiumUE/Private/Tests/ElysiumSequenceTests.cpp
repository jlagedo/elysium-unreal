// Content-free Substrate automation: sky and fog packing plus scripted-sequence state and locomotion.
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
#include "ElysiumSurfaceParams.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
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
#include "Sound/SoundGenerator.h"
#include "Sound/SoundWaveProcedural.h"
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSkyCubeTest, "Elysium.Substrate.SkyCube", GElysiumTestFlags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFogPackTest, "Elysium.Substrate.FogPack", GElysiumTestFlags)
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
// The decal axis of R5.3 (seam_map_material.md -> "Decal fog and wetness homes"): a
// UDecalComponent carries no Custom Primitive Data, so the same fog values the CPD path packs
// for a mesh primitive are applied to an MID's three named instance parameters instead. This
// checks ApplyToDecalMID reproduces Pack's own numbers on those parameters, content-free (the
// engine's own default material needs no project asset).
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumFogDecalMIDTest, "Elysium.Substrate.FogDecalMID", GElysiumTestFlags)
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

// =====================================================================================
// scripted_sequence — the cutscene beat's state machine, driven through the real queue.
//
// No skeletal body here, so no action animation: this covers the half every map depends on —
// placement on the mark, OnBeginSequence/OnEndSequence, and the m_iszNextScript chain. A beat
// with no `m_iszPlay` is zero-length (59 of the 108 exported sequences are), so the whole chain
// settles within a few ticks of the null clock.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScriptedSequenceTest,
	"Elysium.Substrate.ScriptedSequence", GElysiumTestFlags)
bool FElysiumScriptedSequenceTest::RunTest(const FString&)
{
	// Builds one scripted_sequence def. The target is a logic_relay purely because it is a point
	// entity the world will place — the sequence drives FElysiumEntity::SetRuntimeOrigin, which is
	// on the base, not on the NPC leaf.
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
	Mover.Classname = TEXT("logic_relay");
	Mover.TargetName = TEXT("mover1");
	Defs.Defs.Add(MoveTemp(Mover));

	FElysiumEntityDef Counter;
	Counter.Classname = TEXT("math_counter");
	Counter.TargetName = TEXT("counter1");
	Defs.Defs.Add(MoveTemp(Counter));

	FElysiumEntityWorld World(/*Owner*/ nullptr, /*GameState*/ nullptr);
	World.Load(MoveTemp(Defs));
	World.Activate(0.0);

	FElysiumEntity* Seq = World.FindByName(TEXT("seq1"));
	FElysiumEntity* Target = World.FindByName(TEXT("mover1"));
	FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
	if (!TestNotNull(TEXT("seq1 registers a leaf class"), Seq) ||
		!TestNotNull(TEXT("mover1 resolved"), Target) ||
		!TestNotNull(TEXT("counter1 resolved"), Count))
	{
		return false;
	}
	TestFalse(TEXT("scripted_sequence is not an inert record"), Seq->IsRecordOnly());

	auto CounterValue = [](const FElysiumEntity* Entity) -> float
	{
		return ElysiumEntityDebugTest::CounterValue(Entity);
	};

	TestTrue(TEXT("the target starts at the origin"), Target->Origin.IsNearlyZero());

	World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), /*Delay*/ 0.0,
		FElysiumEntityHandle::Invalid(), Seq->Handle);
	for (int32 i = 0; i < 8; ++i)
	{
		World.Tick(0.0);
	}

	// Both of seq1's outputs fired, then the chain carried seq2's.
	TestEqual(TEXT("OnBeginSequence + OnEndSequence + the chained beat all fired"),
		CounterValue(Count), 7.f);
	// m_fMoveTo 1 placed the target on seq1's mark; seq2's m_fMoveTo 0 left it there.
	TestTrue(TEXT("the target was placed on the mark"), Target->Origin.Equals(Mark, 0.01));
	TestTrue(TEXT("the mark's facing was applied"),
		FMath::IsNearlyEqual(Target->Angles.Y, 90.f, 0.01f));

	// NOSCRIPTMOVEMENT: the beat still runs, but the target stays where it is.
	FElysiumEntity* NoMove = World.FindByName(TEXT("seq3"));
	if (TestNotNull(TEXT("seq3 resolved"), NoMove))
	{
		World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), NoMove->Handle);
		for (int32 i = 0; i < 4; ++i)
		{
			World.Tick(0.0);
		}
		TestTrue(TEXT("SF_SCRIPT_NOSCRIPTMOVEMENT left the target on its previous mark"),
			Target->Origin.Equals(Mark, 0.01));
	}

	// A sequence naming the player has no body to drive; it must still run as a timing shell so the
	// map's flow continues rather than dead-ending (10 of the 108 target `!playercontroller`).
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
	World2.Load(MoveTemp(PlayerDefs));
	World2.Activate(0.0);
	FElysiumEntity* PSeq = World2.FindByName(TEXT("pseq"));
	FElysiumEntity* Count2 = World2.FindByName(TEXT("counter1"));
	if (TestNotNull(TEXT("pseq resolved"), PSeq) && TestNotNull(TEXT("counter1 resolved"), Count2))
	{
		World2.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), PSeq->Handle);
		for (int32 i = 0; i < 4; ++i)
		{
			World2.Tick(0.0);
		}
		TestEqual(TEXT("a player-targeted beat still fires OnEndSequence"), CounterValue(Count2), 9.f);
	}

	return true;
}

// =====================================================================================
// scripted_sequence locomotion — the travel phase, over the recording motor.
//
// The claim: a beat whose `m_fMoveTo` says walk sends its NPC to the mark under its own power
// and holds `OnEndSequence` until it gets there. 132 of the 188 exported sequences travel, and
// the ones that carry no `m_iszPlay` — sp_theatre's five-beat courtroom walk-out among them —
// have nothing BUT the transit to time their outputs off, so a beat that ends at the input
// collapses the shot to a blink. The mirror claim is that no failure can hang the beat: a mark
// with no path falls back to the placement and ends in the same pass.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScriptedSequenceLocomotionTest,
	"Elysium.Substrate.ScriptedSequenceLocomotion", GElysiumTestFlags)
bool FElysiumScriptedSequenceLocomotionTest::RunTest(const FString&)
{
	const FVector Spawn(0.f, 0.f, 0.f);
	const FVector Mark(1000.f, 0.f, 0.f);

	auto BuildDefs = [&Mark](FElysiumEntityDefs& Defs)
	{
		Defs.MapName = TEXT("__walkout__");

		FElysiumEntityDef Npc;
		Npc.Classname = TEXT("npc_VVampire");
		Npc.TargetName = TEXT("Isaac");
		Npc.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/isaac/isaac.mdl"));
		Defs.Defs.Add(MoveTemp(Npc));

		// walk_out_people_walk_3's shape: walk, no action animation, one OnEndSequence wire.
		FElysiumEntityDef Seq;
		Seq.Classname = TEXT("scripted_sequence");
		Seq.TargetName = TEXT("walk_out");
		Seq.Origin = Mark;
		Seq.Keys.Add(TEXT("m_iszEntity"), TEXT("Isaac"));
		Seq.Keys.Add(TEXT("m_fMoveTo"), TEXT("1"));
		Seq.Keys.Add(TEXT("angles"), TEXT("0 270 0"));
		FElysiumOutputDef W;
		W.Name = TEXT("OnEndSequence");
		W.Target = TEXT("counter1");
		W.Input = TEXT("Add");
		W.Param = TEXT("5");
		Seq.Outputs.Add(W);
		Defs.Defs.Add(MoveTemp(Seq));

		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = TEXT("counter1");
		Defs.Defs.Add(MoveTemp(Counter));
	};

	auto CounterValue = [](const FElysiumEntity* Entity) -> float
	{
		return ElysiumEntityDebugTest::CounterValue(Entity);
	};

	// --- The beat walks, and holds its output until the mark is reached ---------------------
	{
		FElysiumEntityDefs Defs;
		BuildDefs(Defs);

		FElysiumRecordingServices Services;
		Services.bProvideNpcMotor = true;
		Services.bNpcActivitiesResolve = true;
		Services.ResolvedNpcActivityClip = TEXT("walk_0");
		Services.ResolvedNpcGroundSpeedCmPerSecond = 136.7f;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumEntity* Seq = World.FindByName(TEXT("walk_out"));
		FElysiumEntity* Npc = World.FindByName(TEXT("Isaac"));
		FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
		FElysiumRecordingNpcMotor* Motor = Services.LastNpcMotor();
		if (!TestNotNull(TEXT("walk_out resolved"), Seq) || !TestNotNull(TEXT("Isaac resolved"), Npc)
			|| !TestNotNull(TEXT("counter1 resolved"), Count)
			|| !TestNotNull(TEXT("Isaac stands on a motor"), Motor))
		{
			return false;
		}

		World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), Seq->Handle);

		double Now = 0.0;
		for (int32 i = 0; i < 10; ++i) { World.Tick(Now); Now += 0.1; }

		TestTrue(TEXT("the beat asked the motor for the mark"),
			Motor->RequestedFeet.Equals(Mark, 0.01));
		TestTrue(TEXT("the scripted walk uses the selected clip's authored ground speed"),
			FMath::IsNearlyEqual(Motor->RequestedSpeedCmPerSecond, 136.7f, 0.01f));
		TestTrue(TEXT("the speed came from the concrete forward walk cell"),
			Services.Saw(TEXT("ResolveNpcActivityClip isaac ACT_WALK")));
		TestTrue(TEXT("the activity label entered the global bank resolver after the move"),
			Services.Saw(TEXT("PlayNpcClip isaac walk loop=1")));
		TestFalse(TEXT("the concrete bank cell was not mistaken for a vocabulary label"),
			Services.Saw(TEXT("PlayNpcClip isaac walk_0")));
		// The beat resolves the gait ONCE. Its travel-cycle fallback reaches the same seam, so a
		// second line here would be the beat re-selecting a clip it already chose.
		TestEqual(TEXT("the resolved scripted walk was not selected a second time"),
			Services.Count(TEXT("ResolveNpcActivityClip isaac ACT_WALK")), 1);
		TestFalse(TEXT("the NPC was not teleported onto the mark"), Npc->Origin.Equals(Mark, 0.01));
		TestEqual(TEXT("OnEndSequence is held while the NPC is still walking"),
			CounterValue(Count), 0.f);

		// The body reaches the mark; the beat then turns it onto the mark's angles before ending.
		Motor->SampleStatus = EElysiumNpcMoveStatus::Reached;
		World.Tick(Now); Now += 0.1;
		TestTrue(TEXT("arriving starts the turn onto the mark"),
			Services.Calls.ContainsByPredicate([](const FString& C) { return C.StartsWith(TEXT("NpcMotor Face")); }));
		TestEqual(TEXT("OnEndSequence is still held through the turn"), CounterValue(Count), 0.f);

		// The turn is bounded, so the beat completes even against a motor that never reports level.
		for (int32 i = 0; i < 40; ++i) { World.Tick(Now); Now += 0.1; }
		TestEqual(TEXT("OnEndSequence fires once the beat's travel is over"), CounterValue(Count), 5.f);
	}

	// --- A mark with no path still ends the beat, on the placement fallback -----------------
	{
		FElysiumEntityDefs Defs;
		BuildDefs(Defs);

		FElysiumRecordingServices Services;
		Services.bProvideNpcMotor = true;
		Services.bNpcActivitiesResolve = true;
		Services.ResolvedNpcActivityClip = TEXT("walk_0");
		Services.ResolvedNpcGroundSpeedCmPerSecond = 0.f;   // older sidecar: clip, no motion block
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumEntity* Seq = World.FindByName(TEXT("walk_out"));
		FElysiumEntity* Npc = World.FindByName(TEXT("Isaac"));
		FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
		if (!TestNotNull(TEXT("walk_out resolved"), Seq) || !TestNotNull(TEXT("Isaac resolved"), Npc)
			|| !TestNotNull(TEXT("counter1 resolved"), Count))
		{
			return false;
		}
		FElysiumRecordingNpcMotor* Motor = Services.LastNpcMotor();
		if (Motor)
		{
			Motor->bAcceptMoves = false;   // the navigation graph has nothing to offer this mark
		}

		World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), Seq->Handle);
		for (int32 i = 0; i < 4; ++i) { World.Tick(0.0); }

		TestTrue(TEXT("an unreachable mark places the NPC on it instead"), Npc->Origin.Equals(Mark, 0.01));
		TestTrue(TEXT("and applies the mark's facing"),
			FMath::IsNearlyEqual(Npc->Angles.Y, 270.f, 0.01f));
		if (Motor)
		{
			TestTrue(TEXT("an older sidecar without motion retains the scripted-walk fallback speed"),
				FMath::IsNearlyEqual(Motor->RequestedSpeedCmPerSecond, 254.f, 0.01f));
		}
		TestEqual(TEXT("and the beat still fires OnEndSequence"), CounterValue(Count), 5.f);
	}

	// --- `m_fMoveTo 3` (Custom) resolves ITS named clip's own authored ground speed -----------
	// B3: `m_iszCustomMove` already names an exact clip, one lookup away from the same authored
	// number the Walk branch above resolves through ACT_WALK -- it must not fall back to the
	// hardcoded gait constant while that lookup is answerable.
	auto BuildCustomDefs = [&Mark](FElysiumEntityDefs& Defs, const TCHAR* CustomMove)
	{
		Defs.MapName = TEXT("__walkout_custom__");

		FElysiumEntityDef Npc;
		Npc.Classname = TEXT("npc_VVampire");
		Npc.TargetName = TEXT("Isaac");
		Npc.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/isaac/isaac.mdl"));
		Defs.Defs.Add(MoveTemp(Npc));

		FElysiumEntityDef Seq;
		Seq.Classname = TEXT("scripted_sequence");
		Seq.TargetName = TEXT("walk_out");
		Seq.Origin = Mark;
		Seq.Keys.Add(TEXT("m_iszEntity"), TEXT("Isaac"));
		Seq.Keys.Add(TEXT("m_fMoveTo"), TEXT("3"));
		Seq.Keys.Add(TEXT("m_iszCustomMove"), CustomMove);
		Seq.Keys.Add(TEXT("angles"), TEXT("0 270 0"));
		Defs.Defs.Add(MoveTemp(Seq));
	};

	// The stub reports an authored speed for the named clip: the beat commands it, not the
	// hardcoded ElysiumNpcGait::WalkSpeed.
	{
		FElysiumEntityDefs Defs;
		BuildCustomDefs(Defs, TEXT("sneak_0"));

		FElysiumRecordingServices Services;
		Services.bProvideNpcMotor = true;
		Services.bNpcSequenceClipsResolve = true;
		Services.ResolvedNpcSequenceAnimName = TEXT("sneak_0");
		Services.ResolvedNpcSequenceGroundSpeedCmPerSecond = 72.2f;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumEntity* Seq = World.FindByName(TEXT("walk_out"));
		FElysiumRecordingNpcMotor* Motor = Services.LastNpcMotor();
		if (!TestNotNull(TEXT("walk_out resolved"), Seq)
			|| !TestNotNull(TEXT("Isaac stands on a motor"), Motor))
		{
			return false;
		}

		World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), Seq->Handle);
		for (int32 i = 0; i < 4; ++i) { World.Tick(0.0); }

		TestTrue(TEXT("the custom gait commands the named clip's authored ground speed"),
			FMath::IsNearlyEqual(Motor->RequestedSpeedCmPerSecond, 72.2f, 0.01f));
		TestTrue(TEXT("the speed came from the named clip, not a weighted activity"),
			Services.Saw(TEXT("ResolveNpcSequenceClip isaac sneak_0")));
		TestFalse(TEXT("the custom gait never asks the activity resolver"),
			Services.Saw(TEXT("ResolveNpcActivityClip")));
	}

	// The stub reports no motion record for the named clip: the beat falls back to
	// ElysiumNpcGait::WalkSpeed exactly as an unresolved Walk does, rather than commanding a zero or
	// negative speed. This covers the fallback only — it is not the ordinary path. A travel cycle is
	// normally a blend grid in its owning shared bank (`claws_aggressive_run` is a nine-cell
	// `move_yaw` fan in `character_shared_male_move_and_ranged`), and retail resolves its speed from
	// that grid, so a real export reaching this branch means the motion record went missing.
	{
		FElysiumEntityDefs Defs;
		BuildCustomDefs(Defs, TEXT("claws_run_0"));

		FElysiumRecordingServices Services;
		Services.bProvideNpcMotor = true;
		Services.bNpcSequenceClipsResolve = false;   // the stub carries no motion for this clip
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumEntity* Seq = World.FindByName(TEXT("walk_out"));
		FElysiumRecordingNpcMotor* Motor = Services.LastNpcMotor();
		if (!TestNotNull(TEXT("walk_out resolved"), Seq)
			|| !TestNotNull(TEXT("Isaac stands on a motor"), Motor))
		{
			return false;
		}

		World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), Seq->Handle);
		for (int32 i = 0; i < 4; ++i) { World.Tick(0.0); }

		TestTrue(TEXT("an unresolved custom clip falls back to the scripted-gait constant"),
			FMath::IsNearlyEqual(Motor->RequestedSpeedCmPerSecond, 254.f, 0.01f));
	}

	return true;
}

// The Sheriff scene temporarily replaces the player with `npc_VPlayerController`; its two MoveTo1
// beats must traverse under the same motor contract as an NPC and transfer the final mark back to
// the real player when the stand-in is removed.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumPlayerControllerSequenceLocomotionTest,
	"Elysium.Substrate.PlayerControllerSequenceLocomotion", GElysiumTestFlags)
bool FElysiumPlayerControllerSequenceLocomotionTest::RunTest(const FString&)
{
	const FVector Mark(750.f, 125.f, 20.f);
	FElysiumEntityDefs Defs;
	Defs.MapName = TEXT("__player_controller_sequence__");

	FElysiumEntityDef Seq;
	Seq.Classname = TEXT("scripted_sequence");
	Seq.TargetName = TEXT("player_move");
	Seq.Origin = Mark;
	Seq.Keys.Add(TEXT("m_iszEntity"), TEXT("!playercontroller"));
	Seq.Keys.Add(TEXT("m_fMoveTo"), TEXT("1"));
	Seq.Keys.Add(TEXT("angles"), TEXT("0 210 0"));
	FElysiumOutputDef End;
	End.Name = TEXT("OnEndSequence");
	End.Target = TEXT("counter1");
	End.Input = TEXT("Add");
	End.Param = TEXT("1");
	Seq.Outputs.Add(MoveTemp(End));
	Defs.Defs.Add(MoveTemp(Seq));

	FElysiumEntityDef Counter;
	Counter.Classname = TEXT("math_counter");
	Counter.TargetName = TEXT("counter1");
	Defs.Defs.Add(MoveTemp(Counter));

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	Services.bProvideNpcMotor = true;
	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	World.Load(MoveTemp(Defs));
	World.SpawnPlayer();
	World.Activate(0.0);
	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("player exists"), Player))
	{
		return false;
	}
	Player->SetRuntimeModel(TEXT("models/character/pc/male/tremere_armor_0.mdl"));
	World.CreatePlayerControllerEntity();

	FElysiumEntity* Controller = World.FindPlayerController();
	FElysiumEntity* Sequence = World.FindByName(TEXT("player_move"));
	FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
	FElysiumRecordingNpcMotor* Motor = Services.LastNpcMotor();
	if (!TestNotNull(TEXT("controller exists"), Controller)
		|| !TestNotNull(TEXT("player sequence exists"), Sequence)
		|| !TestNotNull(TEXT("counter exists"), Count)
		|| !TestNotNull(TEXT("controller owns a motor"), Motor))
	{
		return false;
	}

	auto CounterValue = [](const FElysiumEntity* Entity) -> float
	{
		return ElysiumEntityDebugTest::CounterValue(Entity);
	};

	World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
		FElysiumEntityHandle::Invalid(), Sequence->Handle);
	double Now = 0.0;
	for (int32 Tick = 0; Tick < 5; ++Tick) { World.Tick(Now); Now += 0.1; }
	TestTrue(TEXT("MoveTo1 sends the controller motor to the authored mark"),
		Motor->RequestedFeet.Equals(Mark, 0.01));
	TestTrue(TEXT("the controller motor ignores other character capsules"),
		Motor->bIgnoreCharacterCollision);
	TestFalse(TEXT("the controller is not snapped to the mark while its motor is travelling"),
		Controller->Origin.Equals(Mark, 0.01));
	TestEqual(TEXT("OnEndSequence waits for controller travel"), CounterValue(Count), 0.f);

	Motor->Feet = Mark;
	Motor->SampleStatus = EElysiumNpcMoveStatus::Reached;
	for (int32 Tick = 0; Tick < 5; ++Tick) { World.Tick(Now); Now += 0.1; }
	TestEqual(TEXT("OnEndSequence fires after controller arrival and facing"),
		CounterValue(Count), 1.f);
	TestTrue(TEXT("controller samples the motor's final mark"),
		Controller->Origin.Equals(Mark, 0.01));

	TestTrue(TEXT("controller removal succeeds"), World.RemovePlayerControllerEntity());
	TestTrue(TEXT("the final scene mark transfers back to the player"),
		Player->Origin.Equals(Mark, 0.01));
	TestTrue(TEXT("controller teardown destroys its motor before its visual"),
		Services.Saw(TEXT("DestroyNpcMotor")));
	return true;
}

// =====================================================================================
// The three VtMB spawnflag additions on CCineNPC (`docs/vtmb/entity_io.md`): 256 holds the
// post-idle so the beat never completes, 512 makes the beat's claim on its NPC unbreakable,
// and 4096 turns off character collision for the beat's duration. sp_theatre's courtroom
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
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

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
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

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

		// 256: the post-idle is held, so the beat never completes and its wire never fires.
		TestEqual(TEXT("256 suppresses OnEndSequence"), CounterValue(Count), 0.f);
		TestTrue(TEXT("a held beat keeps the collision it borrowed"),
			Motor->bIgnoreCharacterCollision);

		World.EnqueueInput(TEXT("beat_a"), FName(TEXT("CancelSequence")), FElysiumVariant::Void(),
			0.0, {}, {});
		for (int32 i = 0; i < 3; ++i) { World.Tick(Now); Now += 0.1; }
		TestEqual(TEXT("cancelling a held beat still fires no OnEndSequence"),
			CounterValue(Count), 0.f);
		TestFalse(TEXT("cancelling releases the borrowed collision"),
			Motor->bIgnoreCharacterCollision);
	}

	// --- 512: a priority beat cannot be kicked out of the queue ------------------------------
	{
		FElysiumEntityDefs Defs;
		BuildDefs(Defs, /*spawnflags*/ 0x360, TEXT("Converse_Normal_Talk_A"), /*m_fMoveTo*/ 0);

		FElysiumRecordingServices Services;
		Services.bProvideNpcMotor = true;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumEntity* First = World.FindByName(TEXT("beat_a"));
		FElysiumEntity* Second = World.FindByName(TEXT("beat_b"));
		FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
		if (!TestNotNull(TEXT("beat_a resolved"), First) || !TestNotNull(TEXT("beat_b resolved"), Second)
			|| !TestNotNull(TEXT("counter1 resolved"), Count))
		{
			return false;
		}

		double Now = 0.0;
		Begin(World, First);
		for (int32 i = 0; i < 6; ++i) { World.Tick(Now); Now += 0.1; }

		Begin(World, Second);
		for (int32 i = 0; i < 6; ++i) { World.Tick(Now); Now += 0.1; }
		TestEqual(TEXT("512 refuses the challenger outright — not even OnBeginSequence"),
			CounterValue(Count), 0.f);

		// Releasing the claim reopens the queue: the same challenger now gets the NPC.
		World.EnqueueInput(TEXT("beat_a"), FName(TEXT("CancelSequence")), FElysiumVariant::Void(),
			0.0, {}, {});
		for (int32 i = 0; i < 3; ++i) { World.Tick(Now); Now += 0.1; }
		Begin(World, Second);
		for (int32 i = 0; i < 6; ++i) { World.Tick(Now); Now += 0.1; }
		TestEqual(TEXT("and admits it once the claim is released"), CounterValue(Count), 100.f);
	}

	return true;
}

// =====================================================================================
// The beat's claim on its NPC's body (K7). A beat drives the pose from BeginSequence to
// OnEndSequence, so the NPC's own disposition-stance schedule must not select an idle over
// `m_iszPlay` while it runs — an idle is an infinite loop on the same slot, so the NPC would hold a
// standing pose for the whole shot and the action animation would never be seen.
//
// The exposed shape is the beat that never travels (`m_fMoveTo` 0 / 4 / 5 — 24 of sp_tutorial_1's
// 51), because only the travel phase ever reached the arbiter. The travelling beat's ACTION phase
// is the same exposure: its arrival hands the motor back, and the claim must outlive that.
// =====================================================================================

namespace
{
	// The shipped Neutral disposition row, as `Elysium.Substrate.Stance.*` states it.
	FElysiumDisposition SeqNeutralTuning()
	{
		FElysiumDisposition Row;
		Row.Name = TEXT("Neutral");
		Row.AnimName = TEXT("Neutral");
		Row.TalkingStanceChangeThreshold = 0.15f;
		Row.TalkingStanceChangeChance = 65;
		Row.StandingFidgetChance = 50;
		Row.StandingStanceChangeThreshold = 3.0f;
		Row.StandingStanceChangeChance = 80;
		return Row;
	}

	// A body that authored the three stance idles, so the stance machine has something to play and
	// the stomp is observable as a `Stance_*` clip on the recording embodiment.
	FElysiumStanceClips SeqStanceClips()
	{
		FElysiumStanceClips Clips;
		Clips.Idle[0] = TEXT("Stance_Neutral_Idle_1");
		Clips.Idle[1] = TEXT("Stance_Neutral_Idle_2");
		Clips.Idle[2] = TEXT("Stance_Neutral_Idle_3");
		ElysiumStance::ApplyPrecacheFallbacks(Clips);
		return Clips;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScriptedSequenceBodyClaimTest,
	"Elysium.Substrate.ScriptedSequenceBodyClaim", GElysiumTestFlags)
bool FElysiumScriptedSequenceBodyClaimTest::RunTest(const FString&)
{
	// One NPC with a stance set, and one beat aimed at it carrying an action animation.
	auto BuildDefs = [](FElysiumEntityDefs& Defs, const TCHAR* MoveTo)
	{
		Defs.MapName = TEXT("__body_claim__");

		FElysiumEntityDef Npc;
		Npc.Classname = TEXT("npc_VVampire");
		Npc.TargetName = TEXT("Damsel");
		Npc.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/downtown/damsel/damsel.mdl"));
		Npc.Keys.Add(TEXT("default_disposition"), TEXT("Neutral"));
		Defs.Defs.Add(MoveTemp(Npc));

		FElysiumEntityDef Seq;
		Seq.Classname = TEXT("scripted_sequence");
		Seq.TargetName = TEXT("beat");
		Seq.Origin = FVector(1000.f, 0.f, 0.f);
		Seq.Keys.Add(TEXT("m_iszEntity"), TEXT("Damsel"));
		Seq.Keys.Add(TEXT("m_fMoveTo"), MoveTo);
		Seq.Keys.Add(TEXT("m_iszPlay"), TEXT("Sheriff_Talk_A"));
		FElysiumOutputDef W;
		W.Name = TEXT("OnEndSequence");
		W.Target = TEXT("counter1");
		W.Input = TEXT("Add");
		W.Param = TEXT("5");
		Seq.Outputs.Add(MoveTemp(W));
		Defs.Defs.Add(MoveTemp(Seq));

		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = TEXT("counter1");
		Defs.Defs.Add(MoveTemp(Counter));
	};

	auto CounterValue = [](const FElysiumEntity* Entity) -> float
	{
		return ElysiumEntityDebugTest::CounterValue(Entity);
	};

	// The arbiter, read off the NPC's own debug state ("Sequence gen=1 parked=None").
	auto OwnsBody = [](const FElysiumEntity* Entity) -> bool
	{
		return ElysiumEntityDebugTest::Row(Entity, TEXT("Body owner")).StartsWith(TEXT("Sequence"));
	};

	const FString StancePrefix(TEXT("PlayNpcClip damsel Stance_"));

	// --- A beat that never travels still owns the body for its whole duration ----------------
	// No motor is provided, so `m_fMoveTo` 5 takes the placement path with 0 and 4: all three reach
	// the action phase without ever entering BeginScriptMove.
	for (const TCHAR* MoveTo : { TEXT("0"), TEXT("4"), TEXT("5") })
	{
		const FString Case = FString::Printf(TEXT("m_fMoveTo %s"), MoveTo);

		FElysiumEntityDefs Defs;
		BuildDefs(Defs, MoveTo);

		FElysiumRecordingServices Services;
		Services.StanceClips = SeqStanceClips();
		Services.DispositionRow = SeqNeutralTuning();
		Services.ClipSeconds = 2.0f;   // every clip, the beat's `m_iszPlay` included
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumEntity* Seq = World.FindByName(TEXT("beat"));
		FElysiumEntity* Npc = World.FindByName(TEXT("Damsel"));
		FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
		if (!TestNotNull(TEXT("beat resolved"), Seq) || !TestNotNull(TEXT("Damsel resolved"), Npc)
			|| !TestNotNull(TEXT("counter1 resolved"), Count))
		{
			return false;
		}

		// The NPC settles onto its own stance idle first: that is the schedule the beat displaces.
		// A stance machine that keeps writing after the beat starts overwrites `m_iszPlay` mid-beat.
		double Now = 0.0;
		for (int32 i = 0; i < 4; ++i) { World.Tick(Now); Now += 0.1; }
		const int32 StanceBefore = Services.Count(StancePrefix);
		TestTrue(*(Case + TEXT(": the NPC's own stance machine is running before the beat")),
			StanceBefore > 0);

		World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), Seq->Handle);

		int32 StanceDuringBeat = 0;
		bool bHeldBody = false;
		for (int32 i = 0; i < 60 && CounterValue(Count) == 0.f; ++i)
		{
			World.Tick(Now);
			Now += 0.1;
			if (CounterValue(Count) == 0.f)
			{
				StanceDuringBeat = FMath::Max(StanceDuringBeat,
					Services.Count(StancePrefix) - StanceBefore);
				bHeldBody = bHeldBody || OwnsBody(Npc);
			}
		}

		TestTrue(*(Case + TEXT(": the beat played its action animation")),
			Services.Saw(TEXT("PlayNpcClip damsel Sheriff_Talk_A loop=0")));
		TestTrue(*(Case + TEXT(": the beat holds the body arbiter while it runs")), bHeldBody);
		TestEqual(*(Case + TEXT(": no disposition stance is played over m_iszPlay")),
			StanceDuringBeat, 0);
		TestEqual(*(Case + TEXT(": the beat still reaches OnEndSequence")), CounterValue(Count), 5.f);
		TestFalse(*(Case + TEXT(": and the claim leaves with the beat")), OwnsBody(Npc));

		// Released, not abandoned: the NPC's own idle selection runs again once the beat is done.
		const int32 StanceAtEnd = Services.Count(StancePrefix);
		for (int32 i = 0; i < 40; ++i) { World.Tick(Now); Now += 0.1; }
		TestTrue(*(Case + TEXT(": the stance machine resumes once the beat lets go")),
			Services.Count(StancePrefix) > StanceAtEnd);
	}

	// --- A travelling beat keeps the claim across its arrival -------------------------------
	// `EndScriptMove` gives the motor back at the mark; the action phase that follows is animating
	// the same body, so the arbiter claim must not go back with the motor.
	{
		FElysiumEntityDefs Defs;
		BuildDefs(Defs, TEXT("1"));

		FElysiumRecordingServices Services;
		Services.bProvideNpcMotor = true;
		Services.StanceClips = SeqStanceClips();
		Services.DispositionRow = SeqNeutralTuning();
		Services.ClipSeconds = 2.0f;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumEntity* Seq = World.FindByName(TEXT("beat"));
		FElysiumEntity* Npc = World.FindByName(TEXT("Damsel"));
		FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
		FElysiumRecordingNpcMotor* Motor = Services.LastNpcMotor();
		if (!TestNotNull(TEXT("beat resolved"), Seq) || !TestNotNull(TEXT("Damsel resolved"), Npc)
			|| !TestNotNull(TEXT("counter1 resolved"), Count)
			|| !TestNotNull(TEXT("Damsel stands on a motor"), Motor))
		{
			return false;
		}

		double Now = 0.0;
		for (int32 i = 0; i < 4; ++i) { World.Tick(Now); Now += 0.1; }
		const int32 StanceBefore = Services.Count(StancePrefix);

		World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), Seq->Handle);
		for (int32 i = 0; i < 5; ++i) { World.Tick(Now); Now += 0.1; }
		TestTrue(TEXT("the travelling beat holds the body"), OwnsBody(Npc));

		// Arrive: the beat turns onto the mark, then plays `m_iszPlay`.
		Motor->Feet = FVector(1000.f, 0.f, 0.f);
		Motor->SampleStatus = EElysiumNpcMoveStatus::Reached;

		int32 StanceDuringBeat = 0;
		bool bHeldThroughAction = false;
		for (int32 i = 0; i < 80 && CounterValue(Count) == 0.f; ++i)
		{
			World.Tick(Now);
			Now += 0.1;
			if (CounterValue(Count) == 0.f)
			{
				StanceDuringBeat = FMath::Max(StanceDuringBeat,
					Services.Count(StancePrefix) - StanceBefore);
				bHeldThroughAction = bHeldThroughAction
					|| (OwnsBody(Npc) && Services.Saw(TEXT("PlayNpcClip damsel Sheriff_Talk_A loop=0")));
			}
		}

		TestTrue(TEXT("the claim survives the arrival that hands the motor back"), bHeldThroughAction);
		TestEqual(TEXT("no disposition stance is played over the action phase"), StanceDuringBeat, 0);
		TestEqual(TEXT("the travelling beat still reaches OnEndSequence"), CounterValue(Count), 5.f);
		TestFalse(TEXT("and the claim leaves with it"), OwnsBody(Npc));
	}

	// --- A beat triggered in the same pass as its NPC's admission ---------------------------
	// `FElysiumNpcMind::Acquire` answers false until admission has run, so a beat that arrives that
	// early may be refused the arbiter. The beat-queue lock is stamped either way, and that is what
	// keeps the ordinary idle path off the body until the claim lands.
	{
		FElysiumEntityDefs Defs;
		BuildDefs(Defs, TEXT("0"));

		FElysiumRecordingServices Services;
		Services.StanceClips = SeqStanceClips();
		Services.DispositionRow = SeqNeutralTuning();
		Services.ClipSeconds = 2.0f;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumEntity* Seq = World.FindByName(TEXT("beat"));
		FElysiumEntity* Npc = World.FindByName(TEXT("Damsel"));
		FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
		if (!TestNotNull(TEXT("beat resolved"), Seq) || !TestNotNull(TEXT("Damsel resolved"), Npc)
			|| !TestNotNull(TEXT("counter1 resolved"), Count))
		{
			return false;
		}

		World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), Seq->Handle);

		double Now = 0.0;
		int32 StanceDuringBeat = 0;
		bool bHeldBody = false;
		for (int32 i = 0; i < 60 && CounterValue(Count) == 0.f; ++i)
		{
			World.Tick(Now);
			Now += 0.1;
			if (CounterValue(Count) == 0.f)
			{
				StanceDuringBeat = FMath::Max(StanceDuringBeat, Services.Count(StancePrefix));
				bHeldBody = bHeldBody || OwnsBody(Npc);
			}
		}

		TestEqual(TEXT("a beat that races admission still keeps the idle off the body"),
			StanceDuringBeat, 0);
		TestTrue(TEXT("and the claim lands"), bHeldBody);
		TestEqual(TEXT("and the beat runs to OnEndSequence"), CounterValue(Count), 5.f);
		TestFalse(TEXT("and releases afterwards"), OwnsBody(Npc));
	}

	return true;
}

// ================================================================================================
// A beat that names ITSELF in m_iszNextScript
//
// The authored "stand here doing this forever" idiom: `m_iszIdle`, `m_iszPlay` and `m_iszPostIdle`
// all name one clip and the chain points back at the beat's own targetname. Eight sequences across
// three maps do it -- sm_hub_1's `hooker_workin` (prostitute_1, `praying_idle`) is the one that
// flashed a frame of bind pose once a second.
//
// Two rules are held here, both off `FUN_101a8640`:
//   - the action clip is played HELD, so it stands on its last frame instead of retiring into
//     whatever the state machine holds while the beat's think is still one frame away;
//   - the chain is `(next != this) || REPEATABLE`, so a self-naming beat without spawnflag 4 stops
//     rather than re-entering a beat the engine has already retired.
// ================================================================================================
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumScriptedSequenceSelfChainTest,
	"Elysium.Substrate.ScriptedSequenceSelfChain", GElysiumTestFlags)
bool FElysiumScriptedSequenceSelfChainTest::RunTest(const FString&)
{
	auto BuildDefs = [](FElysiumEntityDefs& Defs, int32 SpawnFlags)
	{
		Defs.MapName = TEXT("__selfchain__");

		FElysiumEntityDef Npc;
		Npc.Classname = TEXT("npc_VVampire");
		Npc.TargetName = TEXT("Damsel");
		Npc.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/downtown/damsel/damsel.mdl"));
		Defs.Defs.Add(MoveTemp(Npc));

		FElysiumEntityDef Seq;
		Seq.Classname = TEXT("scripted_sequence");
		Seq.TargetName = TEXT("hold_beat");
		Seq.Keys.Add(TEXT("m_iszEntity"), TEXT("Damsel"));
		Seq.Keys.Add(TEXT("m_fMoveTo"), TEXT("0"));
		Seq.Keys.Add(TEXT("m_iszPlay"), TEXT("praying_idle"));
		Seq.Keys.Add(TEXT("m_iszNextScript"), TEXT("hold_beat"));   // itself
		Seq.Keys.Add(TEXT("spawnflags"), *FString::FromInt(SpawnFlags));
		FElysiumOutputDef W;
		W.Name = TEXT("OnBeginSequence");
		W.Target = TEXT("counter1");
		W.Input = TEXT("Add");
		W.Param = TEXT("1");
		Seq.Outputs.Add(W);
		Defs.Defs.Add(MoveTemp(Seq));

		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = TEXT("counter1");
		Defs.Defs.Add(MoveTemp(Counter));
	};

	auto CounterValue = [](const FElysiumEntity* Entity) -> float
	{
		return ElysiumEntityDebugTest::CounterValue(Entity);
	};

	// --- REPEATABLE: the beat re-enters itself, and its action holds its final frame ---------
	{
		FElysiumEntityDefs Defs;
		BuildDefs(Defs, /*spawnflags*/ 4);

		FElysiumRecordingServices Services;
		Services.ClipSeconds = 0.5f;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumEntity* Seq = World.FindByName(TEXT("hold_beat"));
		FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
		if (!TestNotNull(TEXT("hold_beat resolved"), Seq)
			|| !TestNotNull(TEXT("counter1 resolved"), Count))
		{
			return false;
		}

		World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), Seq->Handle);

		double Now = 0.0;
		for (int32 i = 0; i < 40; ++i) { World.Tick(Now); Now += 0.1; }

		TestTrue(TEXT("the beat plays its action clip"),
			Services.Saw(TEXT("PlayNpcClip damsel praying_idle loop=0")));
		TestTrue(TEXT("a REPEATABLE self-naming beat keeps re-entering itself"),
			CounterValue(Count) > 1.f);
	}

	// --- not REPEATABLE: the same authoring stops after one pass -----------------------------
	{
		FElysiumEntityDefs Defs;
		BuildDefs(Defs, /*spawnflags*/ 0);

		FElysiumRecordingServices Services;
		Services.ClipSeconds = 0.5f;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumEntity* Seq = World.FindByName(TEXT("hold_beat"));
		FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
		if (!TestNotNull(TEXT("hold_beat resolved"), Seq)
			|| !TestNotNull(TEXT("counter1 resolved"), Count))
		{
			return false;
		}

		World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), Seq->Handle);

		double Now = 0.0;
		for (int32 i = 0; i < 40; ++i) { World.Tick(Now); Now += 0.1; }

		TestEqual(TEXT("a self-naming beat that is not REPEATABLE runs once and stops"),
			CounterValue(Count), 1.f);
	}

	return true;
}

// The montage-slot RUN: one mechanism, the producer's band, and a claim every exit gives back.
//
// `scripted_sequence`'s `m_iszIdle` -> travel cycle -> `m_iszPlay` -> `m_iszPostIdle` and an
// interesting place's enter/hold/leave are the same shape and reach the frame through the same funnel
// (`UElysiumEntityBodies::PlayNpcClip` -> the body's `DefaultSlot` montage). What differs is the BAND
// and where the run's claim is given back, and both are what this asserts — on the beat family,
// because it is the one of the two a content-free world can drive end to end: the ambient family's
// enter/hold/leave activities are named by `interestingplacetypelist.txt`, which is export corpus.
//
// The band is not decoration. `Scripted` outranks the travelling body's own locomotion publish and
// `Ambient` deliberately does not (`EElysiumAnimPriority`), so a beat whose segments claimed `Ambient`
// would have `m_iszCustomMove` and `m_iszPlay` consumed by the travel publish on exactly the beats
// that move — and a beat whose WAITING pose claimed `Scripted` would freeze the NPC's own patrol in a
// pre-idle the beat has not started yet.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumMontageSlotRunTest,
	"Elysium.Substrate.MontageSlotRun", GElysiumTestFlags)
bool FElysiumMontageSlotRunTest::RunTest(const FString&)
{
	auto BuildDefs = [](FElysiumEntityDefs& Defs, int32 SpawnFlags, const TCHAR* PostIdle)
	{
		Defs.MapName = TEXT("__slotrun__");

		FElysiumEntityDef Npc;
		Npc.Classname = TEXT("npc_VVampire");
		Npc.TargetName = TEXT("Damsel");
		Npc.Keys.Add(TEXT("model"), TEXT("models/character/npc/unique/downtown/damsel/damsel.mdl"));
		Defs.Defs.Add(MoveTemp(Npc));

		FElysiumEntityDef Seq;
		Seq.Classname = TEXT("scripted_sequence");
		Seq.TargetName = TEXT("beat");
		Seq.Keys.Add(TEXT("m_iszEntity"), TEXT("Damsel"));
		Seq.Keys.Add(TEXT("m_fMoveTo"), TEXT("0"));
		Seq.Keys.Add(TEXT("m_iszIdle"), TEXT("wait_idle"));
		Seq.Keys.Add(TEXT("m_iszPlay"), TEXT("praying_idle"));
		if (PostIdle != nullptr)
		{
			Seq.Keys.Add(TEXT("m_iszPostIdle"), PostIdle);
		}
		Seq.Keys.Add(TEXT("spawnflags"), *FString::FromInt(SpawnFlags));
		Defs.Defs.Add(MoveTemp(Seq));
	};

	// --- the ordinary beat: ambient wait, scripted run, and the claim back at the end -------------
	{
		FElysiumEntityDefs Defs;
		BuildDefs(Defs, /*spawnflags*/ 0, TEXT("rest_idle"));

		FElysiumRecordingServices Services;
		Services.ClipSeconds = 0.5f;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumEntity* Seq = World.FindByName(TEXT("beat"));
		if (!TestNotNull(TEXT("the beat resolved"), Seq))
		{
			return false;
		}

		// The waiting pose, taken at map load. It is the AMBIENT band and holds nothing: the beat has
		// claimed no NPC yet, so the body's own patrol or interesting-place travel still owns it and a
		// `Scripted` pre-idle would refuse the travel clip and slide the NPC to its next mark praying.
		TestTrue(TEXT("the waiting pose plays on the ambient band"),
			Services.Saw(TEXT("PlayNpcClip damsel wait_idle loop=1 band=ambient")));
		TestFalse(TEXT("...and takes no run claim, because the beat owns nothing yet"),
			Services.bNpcSegmentHeld);

		World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), Seq->Handle);
		World.Tick(0.0);

		// The action is the run proper: the beat's own band, and a claim with no duration so the gap
		// between two of its segments is never a frame the channel goes back to locomotion.
		TestTrue(TEXT("the action plays on the scripted band, holding the channel"),
			Services.Saw(TEXT("PlayNpcClip damsel praying_idle loop=0 band=scripted held=1")));
		TestTrue(TEXT("...and the body is holding the run's claim"), Services.bNpcSegmentHeld);

		// The action runs out. A post-idle the beat does NOT hold is what it LEAVES the NPC standing
		// in after handing the body back, so it drops to the ambient band with the claim given back —
		// a released beat still holding `Scripted` would park the channel on a beat that has ended.
		double Now = 0.1;
		for (int32 i = 0; i < 12; ++i) { World.Tick(Now); Now += 0.1; }
		TestTrue(TEXT("the run's claim is given back when the beat ends"),
			Services.Saw(TEXT("ReleaseNpcSegment body=1")));
		TestFalse(TEXT("...and nothing is left holding it"), Services.bNpcSegmentHeld);
		TestTrue(TEXT("the released post-idle is a resting pose on the ambient band"),
			Services.Saw(TEXT("PlayNpcClip damsel rest_idle loop=1 band=ambient")));
	}

	// --- spawnflag 256: the beat goes on owning its NPC, so the post-idle stays part of the run ----
	{
		FElysiumEntityDefs Defs;
		BuildDefs(Defs, /*spawnflags*/ 256, TEXT("rest_idle"));

		FElysiumRecordingServices Services;
		Services.ClipSeconds = 0.5f;
		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		FElysiumEntity* Seq = World.FindByName(TEXT("beat"));
		if (!TestNotNull(TEXT("the held beat resolved"), Seq))
		{
			return false;
		}
		World.EnqueueInput(TEXT("!self"), FName(TEXT("BeginSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), Seq->Handle);
		double Now = 0.0;
		for (int32 i = 0; i < 12; ++i) { World.Tick(Now); Now += 0.1; }

		TestTrue(TEXT("a held post-idle is the run's last segment, at the beat's own band"),
			Services.Saw(TEXT("PlayNpcClip damsel rest_idle loop=1 band=scripted held=1")));
		TestTrue(TEXT("...and the claim is still standing, because the beat never finished"),
			Services.bNpcSegmentHeld);

		// Cancelling is the way out of a held post-idle, and it is a stop path like any other: the
		// claim goes back or the channel is parked on a beat nothing will ever end.
		World.EnqueueInput(TEXT("!self"), FName(TEXT("CancelSequence")), FElysiumVariant::Void(), 0.0,
			FElysiumEntityHandle::Invalid(), Seq->Handle);
		World.Tick(Now);
		TestTrue(TEXT("cancelling a held beat gives the run's claim back"),
			Services.Saw(TEXT("ReleaseNpcSegment body=1")));
		TestFalse(TEXT("...and leaves nothing holding the base channel"), Services.bNpcSegmentHeld);
	}

	return true;
}

} // namespace ElysiumSequenceTests

#endif // WITH_DEV_AUTOMATION_TESTS
