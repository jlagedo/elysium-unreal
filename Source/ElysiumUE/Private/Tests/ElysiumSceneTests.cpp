// Content-free Substrate automation: scene parsing, timeline scheduling, mixahead, and choreography.
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
#include "ElysiumGaitSpeeds.h"               // the animation's per-direction speed (CCC7)
#include "ElysiumGameClock.h"
#include "ElysiumGameFlowSubsystem.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumGymSpec.h"
#include "Visual/ElysiumPoseDeviation.h"
#include "ElysiumHUD.h"
#include "ElysiumInputScope.h"
#include "ElysiumKeyValues.h"
#include "ElysiumLineService.h"
#include "ElysiumLookCurve.h"                // the mouse path's pure rules (CCC3)
#include "Debug/ElysiumMoveCourses.h"        // the event-timed press's pure half (CCC3)
#include "ElysiumMapActor.h"
#include "ElysiumMapEpoch.h"
#include "Map/ElysiumFeedTargeting.h"
#include "Map/ElysiumMapCollision.h"
#include "ElysiumSoundCache.h"
#include "ElysiumMovementComponent.h"
#include "Visual/ElysiumObjModel.h"
#include "Visual/ElysiumNpcClips.h"
#include "ElysiumLocomotionSample.h"         // the body sample's pure rules (CCC1)
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
#include "Substrate/ElysiumRulebook.h"
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
namespace ElysiumSceneTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

// =====================================================================================
// 12.1 — the `.vcd` choreo grammar.
//
// Ported from research/tooling/probes/probe_scenes.py; these cases pin the traps that make such a port wrong.
// The whole-corpus histogram check over all 5,444 shipped files is the Content tier's
// `Elysium.Content.SceneCorpus` — this one is content-free and runs on inline text.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSceneParseTest, "Elysium.Substrate.SceneParse", GElysiumTestFlags)

bool FElysiumSceneParseTest::RunTest(const FString&)
{
	// --- the shape 97% of the corpus has ------------------------------------------------
	{
		const FString Text =
			TEXT("// Choreo version 1\r\n")
			TEXT("actor \"Smiling Jack\"\r\n")
			TEXT("{\r\n")
			TEXT("  channel \"Speech\"\r\n")
			TEXT("  {\r\n")
			TEXT("    event speak \"NPC Line\"\r\n")
			TEXT("    {\r\n")
			TEXT("      time 0.000000 4.870386\r\n")
			TEXT("      param \"character/dlg/main characters/jack_tutorial/line191_col_e.wav\"\r\n")
			TEXT("      param2 \"70dB\"\r\n")
			TEXT("      fixedlength\r\n")
			TEXT("    }\r\n")
			TEXT("  }\r\n")
			TEXT("  bonerename \"Bip02\" \"Bip01\"\r\n")
			TEXT("}\r\n")
			TEXT("fps 60\r\n")
			TEXT("snap off\r\n");

		FElysiumSceneData S;
		ElysiumScene::ParseText(Text, TEXT("test/basic.vcd"), S);

		TestTrue(TEXT("a well-formed scene is valid"), S.bValid);
		TestEqual(TEXT("the version comment is read"), S.Version, 1);
		TestEqual(TEXT("fps is read"), S.Fps, 60.f);
		TestFalse(TEXT("snap off is read"), S.bSnap);
		TestEqual(TEXT("one actor"), S.Actors.Num(), 1);
		// The quoted actor name keeps its space — the whole reason tokens are not split on space.
		TestEqual(TEXT("a quoted name keeps its spaces"), S.Actors[0].Name, FString(TEXT("Smiling Jack")));
		TestEqual(TEXT("bonerename from"), S.Actors[0].BoneFrom, FString(TEXT("Bip02")));
		TestEqual(TEXT("bonerename to"), S.Actors[0].BoneTo, FString(TEXT("Bip01")));
		TestEqual(TEXT("one channel"), S.Channels.Num(), 1);
		TestEqual(TEXT("one event"), S.Events.Num(), 1);

		if (S.Events.Num() == 1)
		{
			const FElysiumSceneEvent& E = S.Events[0];
			TestTrue(TEXT("the event is a speak"), E.Type == EElysiumChoreoEvent::Speak);
			TestEqual(TEXT("its name"), E.Name, FString(TEXT("NPC Line")));
			TestTrue(TEXT("it has an end time"), E.bHasEnd);
			TestEqual(TEXT("its end time"), E.EndTime, 4.870386f);
			TestTrue(TEXT("fixedlength is lifted"), E.bFixedLength);
			TestEqual(TEXT("param2 is lifted"), E.Param2, FString(TEXT("70dB")));
			TestTrue(TEXT("the event is bound to its actor"), E.ActorIndex == 0);
			TestEqual(TEXT("LatestTime is the authored end"), S.LatestTime, 4.870386f);
		}
	}

	// --- the two traps: comments are line-leading only, and braces are matched after tokenizing --
	{
		const FString Text =
			TEXT("actor \"A\"\n")
			TEXT("{\n")
			TEXT("  channel \"C\"\n")
			TEXT("  {\n")
			TEXT("    event speak \"L\"\n")
			TEXT("    {\n")
			// An inline `//` inside a quoted param is part of the path, NOT a comment.
			TEXT("      param \"sound/dlg//odd/line.wav\"\n")
			// A brace inside a quoted string must not open a block.
			TEXT("      param2 \"a{b\"\n")
			TEXT("      time 1.0 2.0\n")
			TEXT("    }\n")
			TEXT("  }\n")
			TEXT("}\n");

		FElysiumSceneData S;
		ElysiumScene::ParseText(Text, TEXT("test/traps.vcd"), S);

		TestEqual(TEXT("the scene survives the traps"), S.Events.Num(), 1);
		if (S.Events.Num() == 1)
		{
			TestEqual(TEXT("an inline // is kept inside a quoted param"),
				S.Events[0].Param, FString(TEXT("sound/dlg//odd/line.wav")));
			TestEqual(TEXT("a { inside a quoted string does not open a block"),
				S.Events[0].Param2, FString(TEXT("a{b")));
		}
	}

	// --- instantaneous events, degenerate ranges, ramps, sequenceduration, unknown blocks -------
	{
		const FString Text =
			TEXT("actor \"A\"\n")
			TEXT("{\n")
			TEXT("  channel \"C\" {\n")                      // trailing-brace form
			TEXT("    event firetrigger \"t\" {\n")
			TEXT("      time 8.600 -1.000000\n")
			TEXT("      param \"1\"\n")
			TEXT("    }\n")
			TEXT("    event expression \"x\" {\n")
			TEXT("      time 5.0 1.0\n")                     // degenerate: end before start
			TEXT("      param \"expressions/jack\"\n")
			TEXT("      param2 \"Snarl\"\n")
			TEXT("      event_ramp {\n")
			TEXT("        0.0 0.0\n")
			TEXT("        1.0 1.0\n")
			TEXT("      }\n")
			TEXT("      tags {\n")                           // recognised by VtMB, used by nothing
			TEXT("        whatever 1\n")
			TEXT("      }\n")
			TEXT("    }\n")
			TEXT("    event gesture \"g\" {\n")
			TEXT("      time 2.0 3.0\n")
			TEXT("      sequenceduration 1.25\n")
			TEXT("    }\n")
			TEXT("  }\n")
			TEXT("}\n");

		FElysiumSceneData S;
		ElysiumScene::ParseText(Text, TEXT("test/edges.vcd"), S);

		TestEqual(TEXT("three events despite the unknown block"), S.Events.Num(), 3);
		TestEqual(TEXT("the degenerate range is counted"), S.NumDegenerate, 1);

		// Events come out sorted by start time: gesture 2.0, expression 5.0, firetrigger 8.6.
		if (S.Events.Num() == 3)
		{
			TestTrue(TEXT("events are sorted by start time"),
				S.Events[0].StartTime <= S.Events[1].StartTime
				&& S.Events[1].StartTime <= S.Events[2].StartTime);
			TestTrue(TEXT("first is the gesture"), S.Events[0].Type == EElysiumChoreoEvent::Gesture);
			TestEqual(TEXT("sequenceduration is lifted"), S.Events[0].SequenceDuration, 1.25f);

			const FElysiumSceneEvent& Expr = S.Events[1];
			TestTrue(TEXT("second is the expression"), Expr.Type == EElysiumChoreoEvent::Expression);
			TestEqual(TEXT("a degenerate end is clamped to its start"), Expr.EndTime, Expr.StartTime);
			TestEqual(TEXT("the ramp has both samples"), Expr.Ramp.Num(), 2);
			TestEqual(TEXT("the ramp interpolates"), Expr.RampAt(0.5f), 0.5f);
			TestEqual(TEXT("  and clamps below"), Expr.RampAt(-1.f), 0.f);
			TestEqual(TEXT("  and clamps above"), Expr.RampAt(99.f), 1.f);

			const FElysiumSceneEvent& Fire = S.Events[2];
			TestTrue(TEXT("third is the firetrigger"), Fire.Type == EElysiumChoreoEvent::FireTrigger);
			TestFalse(TEXT("an end of -1 means instantaneous"), Fire.bHasEnd);
			TestEqual(TEXT("  and its end is held at its start"), Fire.EndTime, Fire.StartTime);
			TestEqual(TEXT("LatestTime uses the start when there is no end"), S.LatestTime, 8.6f);
		}

		// An event with no authored ramp plays at full intensity.
		TestEqual(TEXT("an absent ramp is full intensity"), S.Events[2].RampAt(0.f), 1.f);
	}

	// --- what the reader refuses, and what it tolerates ----------------------------------
	{
		FElysiumSceneData S;
		ElysiumScene::ParseText(TEXT(""), TEXT("test/empty.vcd"), S);
		TestFalse(TEXT("empty text is not a valid scene"), S.bValid);

		ElysiumScene::ParseText(TEXT("}}}\n{{{\nnonsense \"x\"\n"), TEXT("test/garbage.vcd"), S);
		TestFalse(TEXT("garbage names no actor, so it is not valid"), S.bValid);

		// No version line: 10 shipped files are like this and they are still playable.
		ElysiumScene::ParseText(TEXT("actor \"A\"\n{\n}\n"), TEXT("test/nover.vcd"), S);
		TestTrue(TEXT("a scene with no version line is still valid"), S.bValid);
		TestEqual(TEXT("  and reports no version"), S.Version, (int32)INDEX_NONE);
	}

	// --- unknown event types are counted, never dropped silently into a live type --------
	{
		FElysiumSceneData S;
		ElysiumScene::ParseText(
			TEXT("actor \"A\"\n{\n channel \"C\"\n {\n  event notarealtype \"n\"\n  {\n   time 0 1\n  }\n }\n}\n"),
			TEXT("test/unknown.vcd"), S);
		TestEqual(TEXT("an unknown event type still parses"), S.Events.Num(), 1);
		TestTrue(TEXT("  as Unknown"), S.Events[0].Type == EElysiumChoreoEvent::Unknown);
		TestEqual(TEXT("  and is counted as such"), S.CountOf(EElysiumChoreoEvent::Unknown), 1);
	}

	// --- SceneFile -> mirror key ---------------------------------------------------------
	{
		TestEqual(TEXT("the sound/ prefix is stripped and the path lowercased"),
			ElysiumScene::NormalizeSceneRel(TEXT("sound/Character/dlg/MAIN CHARACTERS/x.vcd")),
			FString(TEXT("character/dlg/main characters/x.vcd")));
		TestEqual(TEXT("backslashes fold to forward"),
			ElysiumScene::NormalizeSceneRel(TEXT("sound\\CINEMATIC\\tutorial\\jack_VS_sabbat.vcd")),
			FString(TEXT("cinematic/tutorial/jack_vs_sabbat.vcd")));
		TestEqual(TEXT("a path with no sound/ prefix is left alone but folded"),
			ElysiumScene::NormalizeSceneRel(TEXT("Cinematic/X.vcd")), FString(TEXT("cinematic/x.vcd")));
	}

	// --- the inline cache seam the entity tests drive scenes through ---------------------
	{
		ElysiumScene::ClearCache();
		ElysiumScene::RegisterInline(TEXT("test/inline.vcd"),
			TEXT("actor \"Jack\"\n{\n channel \"C\"\n {\n  event firetrigger \"t\"\n  {\n   time 1.0 -1\n   param \"2\"\n  }\n }\n}\n"));

		TSharedPtr<const FElysiumSceneData> Loaded = ElysiumScene::Load(TEXT("sound/test/inline.vcd"));
		TestTrue(TEXT("an inline scene loads back through the normalized key"), Loaded.IsValid());
		if (Loaded.IsValid())
		{
			TestEqual(TEXT("  with its event"), Loaded->Events.Num(), 1);
			TestEqual(TEXT("  and its trigger number"), Loaded->Events[0].Param, FString(TEXT("2")));
		}
		ElysiumScene::ClearCache();
	}

	return true;
}

// =====================================================================================
// 12.1 — the choreo timeline. Drives FElysiumScenePlayer directly against a recording callback,
// with no entity and no world: start/continue/end classification, the audio mixahead, and the
// two-condition completion test.
// =====================================================================================

namespace
{
	// Records the callback traffic as flat strings so a test can assert on order, not just counts.
	class FElysiumSceneRecorder final : public IElysiumChoreoCallback
	{
	public:
		TArray<FString> Log;

		virtual void StartEvent(const FElysiumSceneData&, const FElysiumSceneEvent& E, float) override
		{
			Log.Add(FString::Printf(TEXT("start:%s"), *E.Name));
		}
		virtual void ProcessEvent(const FElysiumSceneData&, const FElysiumSceneEvent& E, float) override
		{
			Log.Add(FString::Printf(TEXT("cont:%s"), *E.Name));
		}
		virtual void EndEvent(const FElysiumSceneData&, const FElysiumSceneEvent& E, float) override
		{
			Log.Add(FString::Printf(TEXT("end:%s"), *E.Name));
		}
		virtual void RestoreEvent(const FElysiumSceneData&, const FElysiumSceneEvent& E, float) override
		{
			Log.Add(FString::Printf(TEXT("restore:%s"), *E.Name));
		}

		bool Has(const TCHAR* Entry) const { return Log.Contains(Entry); }
		int32 CountOf(const TCHAR* Entry) const
		{
			int32 N = 0;
			for (const FString& S : Log) { if (S == Entry) { ++N; } }
			return N;
		}
		FString Joined() const { return FString::Join(Log, TEXT(",")); }
		void Clear() { Log.Reset(); }
	};

	TSharedPtr<const FElysiumSceneData> MakeTestScene(const FString& Text)
	{
		TSharedPtr<FElysiumSceneData> Data = MakeShared<FElysiumSceneData>();
		ElysiumScene::ParseText(Text, TEXT("test/timeline.vcd"), *Data);
		return Data;
	}

	// Build one `event` block in the shape the shipped corpus actually uses: the brace on its own
	// line. (Neither this reader nor research/tooling/probes/probe_scenes.py accepts a `{ … }` opened and closed on
	// one line — no shipped `.vcd` writes that, and the two grammars are kept identical.)
	FString SceneEvent(const TCHAR* Type, const TCHAR* Name, float Start, float End,
		const TCHAR* Param = nullptr)
	{
		FString S = FString::Printf(TEXT("    event %s \"%s\"\n    {\n      time %f %f\n"),
			Type, Name, Start, End);
		if (Param != nullptr)
		{
			S += FString::Printf(TEXT("      param \"%s\"\n"), Param);
		}
		return S + TEXT("    }\n");
	}

	FString SceneWith(const FString& Events)
	{
		return TEXT("// Choreo version 1\nactor \"A\"\n{\n  channel \"C\"\n  {\n")
			+ Events + TEXT("  }\n}\nfps 60\nsnap off\n");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSceneTimelineTest, "Elysium.Substrate.SceneTimeline", GElysiumTestFlags)

bool FElysiumSceneTimelineTest::RunTest(const FString&)
{
	// A ranged event (2..4), an instantaneous one (3), and a second ranged one (1..5).
	TSharedPtr<const FElysiumSceneData> Scene = MakeTestScene(SceneWith(
		SceneEvent(TEXT("sequence"), TEXT("ranged"), 2.f, 4.f)
		+ SceneEvent(TEXT("firetrigger"), TEXT("inst"), 3.f, -1.f, TEXT("1"))
		+ SceneEvent(TEXT("gesture"), TEXT("wide"), 1.f, 5.f)));

	TestEqual(TEXT("the fixture parsed"), Scene->Events.Num(), 3);

	// --- start / continue / end classification ------------------------------------------
	{
		FElysiumScenePlayer P;
		FElysiumSceneRecorder R;
		P.Begin(Scene, /*Latency=*/0.f, /*MaxDuration=*/600.f);

		TestEqual(TEXT("latest time is the last authored end"), P.GetLatest(), 5.f);
		TestFalse(TEXT("a scene at time 0 is not finished"), P.IsFinished());

		P.AdvanceTo(0.5f, R);
		TestEqual(TEXT("nothing has started yet"), R.Log.Num(), 0);

		P.AdvanceTo(1.5f, R);
		TestTrue(TEXT("the wide event starts"), R.Has(TEXT("start:wide")));
		TestEqual(TEXT("  and only it"), R.Log.Num(), 1);

		R.Clear();
		P.AdvanceTo(2.5f, R);
		TestTrue(TEXT("the ranged event starts"), R.Has(TEXT("start:ranged")));
		TestTrue(TEXT("  and the wide one continues"), R.Has(TEXT("cont:wide")));

		R.Clear();
		P.AdvanceTo(3.5f, R);
		TestTrue(TEXT("an instantaneous event starts"), R.Has(TEXT("start:inst")));
		// One disposition per frame: the frame that starts it does not also end it.
		TestFalse(TEXT("  and does not also end on that frame"), R.Has(TEXT("end:inst")));
		TestEqual(TEXT("  so all three are active"), P.ActiveEvents(), 3);

		R.Clear();
		P.AdvanceTo(4.5f, R);
		TestTrue(TEXT("the instantaneous event ends on the next frame"), R.Has(TEXT("end:inst")));
		TestTrue(TEXT("the ranged event ends when the clock passes its end"), R.Has(TEXT("end:ranged")));
		TestFalse(TEXT("a finished event does not restart"), R.Has(TEXT("start:ranged")));

		R.Clear();
		P.AdvanceTo(5.5f, R);
		TestTrue(TEXT("the wide event ends last"), R.Has(TEXT("end:wide")));
		TestEqual(TEXT("nothing is active"), P.ActiveEvents(), 0);
		TestTrue(TEXT("and the scene is finished"), P.IsFinished());
	}

	// --- completion needs BOTH halves ----------------------------------------------------
	{
		FElysiumScenePlayer P;
		FElysiumSceneRecorder R;
		P.Begin(Scene, 0.f, 600.f);

		// Jump past the last authored end in one advance. Everything starts on that frame; the
		// ends land on the following one, because a frame dispatches one disposition per event.
		P.AdvanceTo(99.f, R);
		TestTrue(TEXT("a long frame still starts every event"), R.Has(TEXT("start:wide"))
			&& R.Has(TEXT("start:ranged")) && R.Has(TEXT("start:inst")));
		TestEqual(TEXT("  each exactly once"), R.CountOf(TEXT("start:wide")), 1);
		TestFalse(TEXT("  and the scene is not finished while they are still active"), P.IsFinished());

		P.AdvanceTo(99.f, R);
		TestEqual(TEXT("the next frame ends them all"), P.ActiveEvents(), 0);
		TestTrue(TEXT("  so the scene is finished"), P.IsFinished());

		// Starts are dispatched in authored start-time order even inside one advance.
		const int32 IdxWide = R.Log.IndexOfByKey(FString(TEXT("start:wide")));
		const int32 IdxRanged = R.Log.IndexOfByKey(FString(TEXT("start:ranged")));
		const int32 IdxInst = R.Log.IndexOfByKey(FString(TEXT("start:inst")));
		TestTrue(TEXT("a long frame dispatches starts in start-time order"),
			IdxWide < IdxRanged && IdxRanged < IdxInst);
	}

	// --- past the end, but something still running: NOT finished -------------------------
	{
		// One event that never ends within the clamp: start 0, end 500.
		TSharedPtr<const FElysiumSceneData> Long = MakeTestScene(SceneWith(
			SceneEvent(TEXT("sequence"), TEXT("long"), 0.f, 500.f)));

		FElysiumScenePlayer P;
		FElysiumSceneRecorder R;
		P.Begin(Long, 0.f, /*MaxDuration=*/10.f);   // clamp the scene end well inside the event

		P.AdvanceTo(11.f, R);
		TestTrue(TEXT("the event is running"), R.Has(TEXT("start:long")));
		TestEqual(TEXT("  and still active"), P.ActiveEvents(), 1);
		// Clock is past the (clamped) latest, but an event is live, so the scene must not end.
		TestFalse(TEXT("past the end but still active is not finished"), P.IsFinished());

		P.AdvanceTo(501.f, R);
		TestTrue(TEXT("once the event ends, so does the scene"), P.IsFinished());
	}

	// --- the max-duration clamp keeps a nonsense range from stalling completion -----------
	{
		// The corpus really contains an event running to ~1.5 million seconds.
		TSharedPtr<const FElysiumSceneData> Absurd = MakeTestScene(SceneWith(
			SceneEvent(TEXT("firetrigger"), TEXT("t"), 0.5f, -1.f, TEXT("1"))
			+ SceneEvent(TEXT("silence"), TEXT("s"), 0.f, 1500000.f)));

		FElysiumScenePlayer P;
		FElysiumSceneRecorder R;
		P.Begin(Absurd, 0.f, /*MaxDuration=*/600.f);
		TestEqual(TEXT("the scene end is clamped"), P.GetLatest(), 600.f);

		P.AdvanceTo(601.f, R);
		TestTrue(TEXT("the real events still fired"), R.Has(TEXT("start:t")));
		// The absurd event is still active, so completion waits on StopActiveEvents — which is what
		// the entity does when the clock passes the clamp. Without the clamp there is no such point.
		P.StopActiveEvents(R);
		TestTrue(TEXT("stopping the stragglers ends the scene"), P.IsFinished());
		TestTrue(TEXT("  and the straggler got its end callback"), R.Has(TEXT("end:s")));
	}

	// --- the audio mixahead pulls speak starts earlier and pushes the scene end later ------
	{
		TSharedPtr<const FElysiumSceneData> Spoken = MakeTestScene(SceneWith(
			SceneEvent(TEXT("speak"), TEXT("line"), 1.f, 2.f)
			+ SceneEvent(TEXT("gesture"), TEXT("gest"), 1.f, 2.f)));

		FElysiumScenePlayer P;
		FElysiumSceneRecorder R;
		P.Begin(Spoken, /*Latency=*/0.25f, 600.f);

		TestEqual(TEXT("the scene end is extended by the mixahead"), P.GetLatest(), 2.25f);

		P.AdvanceTo(0.8f, R);
		TestTrue(TEXT("the speak event starts early by the mixahead"), R.Has(TEXT("start:line")));
		TestFalse(TEXT("  while a non-speak event at the same time does not"), R.Has(TEXT("start:gest")));

		R.Clear();
		P.AdvanceTo(1.1f, R);
		TestTrue(TEXT("the gesture starts at its authored time"), R.Has(TEXT("start:gest")));
	}

	// --- Reset replays, PreLatchTo does not ----------------------------------------------
	{
		FElysiumScenePlayer P;
		FElysiumSceneRecorder R;
		P.Begin(Scene, 0.f, 600.f);
		P.AdvanceTo(99.f, R);

		R.Clear();
		P.Reset();
		TestEqual(TEXT("reset returns the clock to zero"), P.GetTime(), 0.f);
		P.AdvanceTo(99.f, R);
		TestTrue(TEXT("after a reset the scene replays"), R.Has(TEXT("start:wide")));

		// The save-restore path: everything already passed is latched silently.
		R.Clear();
		P.Reset();
		P.PreLatchTo(3.5f);
		TestEqual(TEXT("pre-latching fires nothing"), R.Log.Num(), 0);
		P.AdvanceTo(4.0f, R);
		TestFalse(TEXT("an event whose start already passed does not re-fire"), R.Has(TEXT("start:wide")));
		TestFalse(TEXT("  nor the instantaneous one"), R.Has(TEXT("start:inst")));
		P.AdvanceTo(99.f, R);
		TestTrue(TEXT("a restored scene still reaches completion"), P.IsFinished());
	}

	// --- exact restore rebuilds continuous events without replaying instantaneous outputs -
	{
		FElysiumScenePlayer Original;
		FElysiumSceneRecorder Before;
		Original.Begin(Scene, 0.f, 600.f);
		Original.AdvanceTo(3.5f, Before);
		TArray<uint8> Started;
		TArray<uint8> Active;
		Original.CaptureLatches(Started, Active);

		FElysiumScenePlayer Restored;
		FElysiumSceneRecorder After;
		Restored.Begin(Scene, 0.f, 600.f);
		Restored.RestoreLatches(3.5f, Started, Active, After);
		TestTrue(TEXT("restore reconstitutes the wide ranged event"), After.Has(TEXT("restore:wide")));
		TestTrue(TEXT("restore reconstitutes the ranged event"), After.Has(TEXT("restore:ranged")));
		TestFalse(TEXT("restore does not replay the instantaneous output"), After.Has(TEXT("restore:inst")));
		TestEqual(TEXT("the exact active latch count survives"), Restored.ActiveEvents(), 3);
		After.Clear();
		Restored.AdvanceTo(4.5f, After);
		TestFalse(TEXT("past starts remain exactly-once after restore"), After.Has(TEXT("start:inst")));
		TestTrue(TEXT("the restored instantaneous event still closes"), After.Has(TEXT("end:inst")));
	}

	// --- active 0 actors/channels remain inspectable but never enter the timeline ----------
	{
		const FString DisabledText = TEXT("// Choreo version 1\nactor \"A\"\n{\n  channel \"off\"\n  {\n")
			+ SceneEvent(TEXT("firetrigger"), TEXT("disabled"), 50.f, -1.f, TEXT("1"))
			+ TEXT("    active 0\n  }\n}\nfps 60\n");
		TSharedPtr<const FElysiumSceneData> Disabled = MakeTestScene(DisabledText);
		TestEqual(TEXT("disabled events remain parsed"), Disabled->Events.Num(), 1);
		TestFalse(TEXT("the event inherits its channel's disabled state"), Disabled->Events[0].bActive);
		FElysiumScenePlayer P;
		FElysiumSceneRecorder R;
		P.Begin(Disabled, 0.f, 600.f);
		TestEqual(TEXT("disabled content does not extend scene duration"), P.GetLatest(), 0.f);
		P.AdvanceTo(100.f, R);
		TestEqual(TEXT("disabled content dispatches nothing"), R.Log.Num(), 0);
		TestTrue(TEXT("and cannot stall completion"), P.IsFinished());
	}

	// --- an unbound player is inert -------------------------------------------------------
	{
		FElysiumScenePlayer P;
		FElysiumSceneRecorder R;
		TestFalse(TEXT("an unbound player is not bound"), P.IsBound());
		P.AdvanceTo(5.f, R);          // must not crash
		P.StopActiveEvents(R);
		TestEqual(TEXT("an unbound player dispatches nothing"), R.Log.Num(), 0);
	}

	return true;
}

// =====================================================================================
// 12.2b — the lead a scene schedules its speech with belongs to the audio path we run on.
//
// VtMB hands its scenes `snd_mixahead`, 0.1 s. The *behaviour* that buys is "the sample is heard
// at the authored instant"; the *number* is Source's own mixer's lead. Carrying the number into
// Unreal's mixer reproduces Source's implementation instead of VtMB's behaviour, and the line is
// then heard tens of milliseconds ahead of every cue authored against it — lipsync, expressions,
// gestures and camera cuts alike.
//
// This test holds the derivation in place. It fails if the lead stops tracking what the output
// path reports — which is what reintroducing any constant inherited from a different mixer does.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSceneMixaheadTest, "Elysium.Substrate.SceneMixahead", GElysiumTestFlags)

bool FElysiumSceneMixaheadTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("scene 'test/line.vcd' not found"),
		EAutomationExpectedErrorFlags::Contains, 1);
	IConsoleVariable* MixaheadVar = IConsoleManager::Get().FindConsoleVariable(TEXT("elysium.SceneMixahead"));
	if (!TestNotNull(TEXT("elysium.SceneMixahead is registered"), MixaheadVar))
	{
		return false;
	}
	const float WasMixahead = MixaheadVar->GetFloat();
	ON_SCOPE_EXIT{ MixaheadVar->Set(WasMixahead, ECVF_SetByCode); };

	// --- the default is "derive", not a constant -------------------------------------------
	{
		// A negative default is the whole guard: the moment somebody writes a number here, every
		// scene in the game goes back to being scheduled against a mixer we do not run.
		TestTrue(TEXT("the shipped default derives the lead from the audio path"), WasMixahead < 0.f);
		TestNotEqual(TEXT("  and is not VtMB's snd_mixahead default"), WasMixahead, 0.1f);
	}

	// --- the terms add up, and the fallback is named as one ---------------------------------
	{
		FElysiumAudioLatency L;
		L.MixerQueueSeconds = 0.020f;
		L.EndpointSeconds = 0.030f;
		L.SubmitToRenderSeconds = 0.004f;
		TestEqual(TEXT("the lead is the sum of its terms"), L.Lead(), 0.054f, 1e-6f);
		TestFalse(TEXT("a default-constructed latency has not been near a device"), L.bDeviceQueried);

		// The no-device value is one Unreal mixer callback at 48 kHz. It is a fallback, and the
		// point of asserting it is that it is not, and must not become, 0.1.
		TestEqual(TEXT("the fallback is one 1024-frame callback at 48 kHz"),
			ElysiumAudioLatency::FallbackLeadSeconds, 1024.f / 48000.f, 1e-9f);
		TestNotEqual(TEXT("  and is not Source's mixer's lead"),
			ElysiumAudioLatency::FallbackLeadSeconds, 0.1f);
	}

	// --- a scene leads its speech by whatever the output path reports ------------------------
	//
	// The scene is driven twice with two different reported leads. Comparing the two dispatch
	// instants is what pins the behaviour: a hardcoded constant would produce the same instant
	// both times no matter what the audio path said.
	ElysiumScene::ClearCache();
	ElysiumScene::RegisterInline(TEXT("test/mixahead.vcd"), SceneWith(
		SceneEvent(TEXT("speak"), TEXT("line"), 1.f, 2.f, TEXT("test/line.wav"))));

	// Drive one scene forward in small steps and answer the scene time at which the line was
	// submitted to the audio path, or -1 if it never was.
	auto DispatchTime = [](float ReportedLead) -> double
	{
		FElysiumRecordingServices Services;
		Services.OutputLead = ReportedLead;

		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__test__");
		FElysiumEntityDef S;
		S.Classname = TEXT("logic_choreographed_scene");
		S.TargetName = TEXT("scene1");
		S.Keys.Add(TEXT("SceneFile"), TEXT("test/mixahead.vcd"));
		Defs.Defs.Add(MoveTemp(S));

		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);
		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});

		// 1 ms steps: fine enough that the answer is the lead rather than the step.
		for (int32 Step = 0; Step <= 1500; ++Step)
		{
			const double T = Step * 0.001;
			World.Tick(T);
			if (Services.Saw(TEXT("Submit")))
			{
				return T;
			}
		}
		return -1.0;
	};

	{
		const double Short = DispatchTime(0.020f);
		const double Long = DispatchTime(0.250f);
		TestTrue(TEXT("the line is submitted under a short lead"), Short > 0.0);
		TestTrue(TEXT("the line is submitted under a long lead"), Long > 0.0);
		// The authored start is 1.0, so each dispatch lands one step past `1 - lead`.
		TestEqual(TEXT("a 20 ms lead submits 20 ms before the authored instant"), Short, 0.980, 0.002);
		TestEqual(TEXT("a 250 ms lead submits 250 ms before it"), Long, 0.750, 0.002);
		TestTrue(TEXT("so the lead tracks the audio path rather than a constant"),
			FMath::Abs((Short - Long) - 0.230) < 0.004);
	}

	// --- the retail constant survives as an explicit override --------------------------------
	// Reproducing VtMB's own 0.1 is still one console command away, which is what keeps the
	// derived lead A/B-able against the behaviour the original shipped with.
	{
		MixaheadVar->Set(0.1f, ECVF_SetByCode);
		const double Forced = DispatchTime(0.250f);
		TestEqual(TEXT("a forced mixahead overrides what the audio path reports"), Forced, 0.900, 0.002);
	}

	return true;
}

// =====================================================================================
// 12.1 — `logic_choreographed_scene` end to end, through the real world and event queue.
//
// The scene text is seeded into the parse cache inline, so this stays in the content-free tier.
// Outputs land on a math_counter with distinct Add values, which is what lets one number prove
// both which outputs fired and in what order they did not.
// =====================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumChoreoSceneTest, "Elysium.Substrate.ChoreoScene", GElysiumTestFlags)

bool FElysiumChoreoSceneTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("scene 'test/does_not_exist.vcd' not found"),
		EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("SceneFile 'test/does_not_exist.vcd' did not resolve"),
		EAutomationExpectedErrorFlags::Contains, 1);
	auto Wire = [](FElysiumEntityDef& On, const TCHAR* Output, const TCHAR* Param)
	{
		FElysiumOutputDef W;
		W.Name = Output;
		W.Target = TEXT("counter1");
		W.Input = TEXT("Add");
		W.Param = Param;
		On.Outputs.Add(W);
	};

	auto CounterValue = [](const FElysiumEntity* Entity) -> float
	{
		TArray<TPair<FString, FString>> State;
		Entity->GetDebugState(State);
		for (const TPair<FString, FString>& Row : State)
		{
			if (Row.Key == TEXT("Value")) { return FCString::Atof(*Row.Value); }
		}
		return -1.f;
	};

	// One scene: an actor named "actor1", a 4-second sequence, and triggers 1, 2 and 7 (the last
	// out of range and therefore fired at nothing).
	ElysiumScene::RegisterInline(TEXT("test/scene.vcd"), SceneWith(
		SceneEvent(TEXT("sequence"), TEXT("body"), 0.f, 4.f, TEXT("some_clip"))
		+ SceneEvent(TEXT("firetrigger"), TEXT("t1"), 1.f, -1.f, TEXT("1"))
		+ SceneEvent(TEXT("firetrigger"), TEXT("t2"), 2.f, -1.f, TEXT("2"))
		+ SceneEvent(TEXT("firetrigger"), TEXT("bad"), 2.5f, -1.f, TEXT("7"))));

	auto BuildWorld = [&](FElysiumEntityWorld& World, int32 PositionStart, int32 PositionEnd,
		const TCHAR* SceneFile, const TCHAR* ActorName)
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__test__");

		FElysiumEntityDef S;
		S.Classname = TEXT("logic_choreographed_scene");
		S.TargetName = TEXT("scene1");
		S.Origin = FVector(500.f, 600.f, 700.f);
		S.Keys.Add(TEXT("SceneFile"), SceneFile);
		S.Keys.Add(TEXT("angles"), TEXT("0 90 0"));
		S.Keys.Add(TEXT("position_start"), FString::FromInt(PositionStart));
		S.Keys.Add(TEXT("position_end"), FString::FromInt(PositionEnd));
		Wire(S, TEXT("OnStart"), TEXT("1"));
		Wire(S, TEXT("OnCompletion"), TEXT("10"));
		Wire(S, TEXT("OnCanceled"), TEXT("100"));
		Wire(S, TEXT("OnTrigger1"), TEXT("1000"));
		Wire(S, TEXT("OnTrigger2"), TEXT("10000"));
		Defs.Defs.Add(MoveTemp(S));

		// A point entity the world will place: the scene drives SetRuntimeOrigin, which is on the
		// base, so a logic_relay stands in for an NPC exactly as the scripted_sequence test does.
		FElysiumEntityDef A;
		A.Classname = TEXT("logic_relay");
		A.TargetName = ActorName;
		A.Origin = FVector(10.f, 20.f, 30.f);
		Defs.Defs.Add(MoveTemp(A));

		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = TEXT("counter1");
		Defs.Defs.Add(MoveTemp(Counter));

		World.Load(MoveTemp(Defs));
		World.Activate(0.0);
	};

	// --- bool keyfields read numerically, not as string truthiness -------------------------
	// A map keyvalue is a String, and Source reads a bool key as `atoi(v) != 0`. Reading it with
	// Python truthiness instead makes the literal "0" true, which silently flips every
	// map-authored `hide_ents "0"` on (sp_theatre writes exactly that).
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__test__");

		FElysiumEntityDef S;
		S.Classname = TEXT("logic_choreographed_scene");
		S.TargetName = TEXT("flags1");
		S.Keys.Add(TEXT("SceneFile"), TEXT("test/scene.vcd"));
		S.Keys.Add(TEXT("hide_ents"), TEXT("0"));
		S.Keys.Add(TEXT("full_sound"), TEXT("1"));
		Defs.Defs.Add(MoveTemp(S));
		World.Load(MoveTemp(Defs));
		World.Activate(0.0);

		const FElysiumEntity* Ent = World.FindByName(TEXT("flags1"));
		if (TestNotNull(TEXT("flags1 resolved"), Ent))
		{
			const FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
			const FElysiumFieldAccessor* Hide = Reg.FindField(*Ent->Class, FName(TEXT("hide_ents")));
			const FElysiumFieldAccessor* Full = Reg.FindField(*Ent->Class, FName(TEXT("full_sound")));
			if (TestNotNull(TEXT("hide_ents is a registered field"), Hide)
				&& TestNotNull(TEXT("full_sound is a registered field"), Full))
			{
				TestFalse(TEXT("hide_ents \"0\" reads false"), Hide->Get(*Ent).ToBool());
				TestTrue(TEXT("full_sound \"1\" reads true"), Full->Get(*Ent).ToBool());
			}
		}
	}

	// --- the ordinary run: start, triggers, completion -------------------------------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		BuildWorld(World, /*position_start*/ 0, /*position_end*/ 0, TEXT("test/scene.vcd"), TEXT("A"));

		FElysiumEntity* SceneEnt = World.FindByName(TEXT("scene1"));
		const FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
		if (!TestNotNull(TEXT("logic_choreographed_scene registers a leaf class"), SceneEnt)
			|| !TestNotNull(TEXT("counter1 resolved"), Count))
		{
			return false;
		}
		TestFalse(TEXT("it is not an inert record"), SceneEnt->IsRecordOnly());

		double T = 0.0;
		World.Tick(T);
		TestEqual(TEXT("an untriggered scene fires nothing"), CounterValue(Count), 0.f);

		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
		World.Tick(T);
		TestEqual(TEXT("Start fires OnStart"), CounterValue(Count), 1.f);

		// A second Start while playing is ignored — VtMB's first gate.
		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
		T = 0.5; World.Tick(T);
		TestEqual(TEXT("a second Start while playing does nothing"), CounterValue(Count), 1.f);

		T = 1.2; World.Tick(T);
		TestEqual(TEXT("trigger 1 fires when the clock crosses it"), CounterValue(Count), 1001.f);

		T = 1.5; World.Tick(T);
		TestEqual(TEXT("  and does not fire twice"), CounterValue(Count), 1001.f);

		T = 2.2; World.Tick(T);
		TestEqual(TEXT("trigger 2 fires"), CounterValue(Count), 11001.f);

		T = 2.8; World.Tick(T);
		TestEqual(TEXT("an out-of-range trigger number fires nothing"), CounterValue(Count), 11001.f);

		T = 3.9; World.Tick(T);
		TestEqual(TEXT("completion has not fired before the last event ends"), CounterValue(Count), 11001.f);

		// Past the end: one tick ends the sequence event, the next reports finished.
		T = 4.5; World.Tick(T);
		T = 4.6; World.Tick(T);
		TestEqual(TEXT("OnCompletion fires once the scene ends"), CounterValue(Count), 11011.f);

		T = 6.0; World.Tick(T);
		TestEqual(TEXT("  and only once"), CounterValue(Count), 11011.f);
	}

	// --- Pause keeps the original start base; Resume catches up missed events ---------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		BuildWorld(World, 0, 0, TEXT("test/scene.vcd"), TEXT("A"));
		const FElysiumEntity* Count = World.FindByName(TEXT("counter1"));
		double T = 0.0;
		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
		World.Tick(T);
		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Pause")), FElysiumVariant::Void(), 0.0, {}, {});
		T = 0.5; World.Tick(T);
		T = 2.2; World.Tick(T);
		TestEqual(TEXT("paused scene dispatches no missed triggers"), CounterValue(Count), 1.f);
		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Resume")), FElysiumVariant::Void(), 0.0, {}, {});
		T = 2.3; World.Tick(T);
		T = 2.31; World.Tick(T);
		TestEqual(TEXT("resume catches up both wall-clock triggers exactly once"), CounterValue(Count), 11001.f);
	}

	// --- A mid-scene snapshot restores latches without duplicating past outputs ------------
	{
		FElysiumEntityWorld Before(nullptr, nullptr);
		BuildWorld(Before, 0, 0, TEXT("test/scene.vcd"), TEXT("A"));
		double T = 0.0;
		Before.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
		Before.Tick(T);
		T = 1.2; Before.Tick(T);
		FElysiumMapSnapshot Snapshot;
		Before.Freeze(Snapshot);

		FElysiumEntityWorld After(nullptr, nullptr);
		BuildWorld(After, 0, 0, TEXT("test/scene.vcd"), TEXT("A"));
		After.Tick(T); // match the saved wall clock before the leaf computes its restored base
		After.ApplySnapshot(Snapshot);
		const FElysiumEntity* Count = After.FindByName(TEXT("counter1"));
		TestEqual(TEXT("snapshot carried the first trigger"), CounterValue(Count), 1001.f);
		T = 1.3; After.Tick(T);
		TestEqual(TEXT("the first trigger is not replayed after restore"), CounterValue(Count), 1001.f);
		T = 2.2; After.Tick(T);
		TestEqual(TEXT("the future trigger still dispatches"), CounterValue(Count), 11001.f);
		T = 4.5; After.Tick(T);
		T = 4.6; After.Tick(T);
		TestEqual(TEXT("the restored scene completes exactly once"), CounterValue(Count), 11011.f);
	}

	// --- Cancel suppresses completion, permanently -----------------------------------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		BuildWorld(World, 0, 0, TEXT("test/scene.vcd"), TEXT("A"));
		const FElysiumEntity* Count = World.FindByName(TEXT("counter1"));

		double T = 0.0;
		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
		World.Tick(T);
		T = 1.2; World.Tick(T);
		TestEqual(TEXT("the scene is running"), CounterValue(Count), 1001.f);

		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Cancel")), FElysiumVariant::Void(), 0.0, {}, {});
		T = 1.3; World.Tick(T);
		TestEqual(TEXT("Cancel fires OnCanceled"), CounterValue(Count), 1101.f);

		// Keep ticking well past the scene's end: OnCompletion must never arrive.
		for (int32 i = 0; i < 10; ++i) { T += 1.0; World.Tick(T); }
		TestEqual(TEXT("a cancelled scene never completes"), CounterValue(Count), 1101.f);
	}

	// --- position_start places ONCE and freezes; position_end 2 restores ---------------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		BuildWorld(World, /*position_start*/ 1, /*position_end*/ 2, TEXT("test/scene.vcd"), TEXT("A"));

		FElysiumEntity* Actor = World.FindByName(TEXT("A"));
		const FVector Home(10.f, 20.f, 30.f);
		const FVector Mark(500.f, 600.f, 700.f);
		TestEqual(TEXT("the actor starts at home"), Actor->Origin, Home);

		double T = 0.0;
		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
		World.Tick(T);
		TestEqual(TEXT("position_start 1 places the actor on the scene"), Actor->Origin, Mark);

		// The placement is the scene's only write. VtMB holds its cast by immobilising the body
		// (FUN_10081ed0's MOVETYPE_NONE + SOLID_NONE), not by rewriting the transform every frame,
		// so a mid-scene move stays where it was put — nothing drags it back.
		const FVector Moved(-1.f, -1.f, -1.f);
		Actor->SetRuntimeOrigin(Moved);
		T = 1.0; World.Tick(T);
		TestEqual(TEXT("  and does not re-pin it per frame"), Actor->Origin, Moved);
		Actor->SetRuntimeOrigin(Mark);   // put it back for the position_end assertion below

		T = 4.5; World.Tick(T);
		T = 4.6; World.Tick(T);
		TestEqual(TEXT("position_end 2 restores the transform saved at Start"), Actor->Origin, Home);
	}

	// --- position_end 0 leaves the actor where the scene left it -----------------------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		BuildWorld(World, /*position_start*/ 1, /*position_end*/ 0, TEXT("test/scene.vcd"), TEXT("A"));
		FElysiumEntity* Actor = World.FindByName(TEXT("A"));

		double T = 0.0;
		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
		World.Tick(T);
		T = 4.5; World.Tick(T);
		T = 4.6; World.Tick(T);
		TestEqual(TEXT("position_end 0 leaves the actor on the mark"),
			Actor->Origin, FVector(500.f, 600.f, 700.f));
	}

	// --- an actor the map does not have: its events drop, the scene still completes ----------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		BuildWorld(World, 0, 0, TEXT("test/scene.vcd"), TEXT("someone_else"));
		const FElysiumEntity* Count = World.FindByName(TEXT("counter1"));

		double T = 0.0;
		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
		World.Tick(T);
		for (double Step = 0.5; Step < 6.0; Step += 0.5) { World.Tick(Step); }
		// Every output still fires: an unresolved actor drops its own events, not the scene.
		TestEqual(TEXT("a scene with an unbound actor still runs its triggers and completes"),
			CounterValue(Count), 11011.f);
	}

	// --- a SceneFile that resolves to nothing is a silent no-op ------------------------------
	{
		FElysiumEntityWorld World(nullptr, nullptr);
		BuildWorld(World, 0, 0, TEXT("test/does_not_exist.vcd"), TEXT("A"));
		const FElysiumEntity* Count = World.FindByName(TEXT("counter1"));

		double T = 0.0;
		World.EnqueueInput(TEXT("scene1"), FName(TEXT("Start")), FElysiumVariant::Void(), 0.0, {}, {});
		World.Tick(T);
		for (double Step = 0.5; Step < 6.0; Step += 0.5) { World.Tick(Step); }
		TestEqual(TEXT("an unresolvable SceneFile fires nothing at all"), CounterValue(Count), 0.f);
	}

	// --- the dormant map walk resolves scene, scripted and output animation references --------
	{
		FElysiumRecordingServices Services;
		Services.bCinematicClipsResolve = true;
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__animation_preload__");

		FElysiumEntityDef Actor;
		Actor.Classname = TEXT("npc_VHumanCombatant");
		Actor.TargetName = TEXT("A");
		Actor.Keys.Add(TEXT("model"), TEXT("models/character/npc/common/male_citizen.mdl"));
		Defs.Defs.Add(MoveTemp(Actor));

		FElysiumEntityDef SceneDef;
		SceneDef.Classname = TEXT("logic_choreographed_scene");
		SceneDef.TargetName = TEXT("resident_scene");
		SceneDef.Keys.Add(TEXT("SceneFile"), TEXT("test/scene.vcd"));
		SceneDef.Keys.Add(TEXT("BaseAnim"), TEXT("models/cinematic/test_scene.mdl"));
		Defs.Defs.Add(MoveTemp(SceneDef));

		FElysiumEntityDef Sequence;
		Sequence.Classname = TEXT("scripted_sequence");
		Sequence.TargetName = TEXT("resident_sequence");
		Sequence.Keys.Add(TEXT("m_iszEntity"), TEXT("A"));
		Sequence.Keys.Add(TEXT("m_iszPlay"), TEXT("action_clip"));
		Defs.Defs.Add(MoveTemp(Sequence));

		FElysiumEntityDef Relay;
		Relay.Classname = TEXT("logic_relay");
		FElysiumOutputDef SetAnim;
		SetAnim.Name = TEXT("OnTrigger");
		SetAnim.Target = TEXT("A");
		SetAnim.Input = TEXT("SetAnimation");
		SetAnim.Param = TEXT("wire_clip");
		Relay.Outputs.Add(MoveTemp(SetAnim));
		Defs.Defs.Add(MoveTemp(Relay));

		FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
		World.Load(MoveTemp(Defs));
		Services.Calls.Reset();
		World.PreloadMapAnimations();

		TestFalse(TEXT("the residency walk does not activate the entity world"), World.IsActive());
		TestTrue(TEXT("a choreographed sequence clip is preloaded"),
			Services.Saw(TEXT("PreloadCinematicClip male_citizen")));
		TestTrue(TEXT("a scripted_sequence action clip is preloaded"),
			Services.Saw(TEXT("PreloadNpcClip male_citizen action_clip")));
		TestTrue(TEXT("a SetAnimation output parameter is preloaded"),
			Services.Saw(TEXT("PreloadNpcClip male_citizen wire_clip")));
		TestFalse(TEXT("preloading does not play a cinematic clip"),
			Services.Saw(TEXT("PlayCinematicClip")));

		// Python may recast an actor after the dormant map walk. Start must resolve the VCD again
		// against that replacement skeleton and finish the batch before the scene clock/playback.
		World.Activate(0.0);
		FElysiumEntity* LiveActor = World.FindByName(TEXT("A"));
		Services.Calls.Reset();
		LiveActor->SetRuntimeModel(
			TEXT("models/character/npc/common/female_citizen.mdl"));
		TestTrue(TEXT("SetModel queues the map closure for the replacement skeleton"),
			Services.Saw(TEXT("PreloadCinematicClip female_citizen")));
		Services.Calls.Reset();
		World.EnqueueInput(TEXT("resident_scene"), FName(TEXT("Start")),
			FElysiumVariant::Void(), 0.0, {}, {});
		World.Tick(0.0);

		int32 RecastPreload = INDEX_NONE;
		int32 Finish = INDEX_NONE;
		for (int32 Index = 0; Index < Services.Calls.Num(); ++Index)
		{
			if (RecastPreload == INDEX_NONE
				&& Services.Calls[Index].StartsWith(
					TEXT("PreloadCinematicClip female_citizen")))
			{
				RecastPreload = Index;
			}
			if (Finish == INDEX_NONE
				&& Services.Calls[Index].StartsWith(TEXT("FinishAnimationPreload")))
			{
				Finish = Index;
			}
		}
		TestTrue(TEXT("scene Start re-resolves the Python-recast actor"),
			RecastPreload != INDEX_NONE);
		TestTrue(TEXT("scene Start closes residency after resolving its actual cast"),
			Finish > RecastPreload);
	}

	ElysiumScene::ClearCache();
	return true;
}

} // namespace ElysiumSceneTests

#endif // WITH_DEV_AUTOMATION_TESTS
