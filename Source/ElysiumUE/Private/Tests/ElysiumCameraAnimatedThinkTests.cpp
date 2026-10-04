// `CCameraAnimated`'s think `0x10071840` (spec 0002 V4a, lane A4; ruling J4): slot 250 (the
// advance) -> slot 258 `DispatchAnimEvents` (the base dispatcher `0x10091880`, no layers) -> the
// finish test on `m_bSequenceFinished` (`+0x65c`). Hand-built: no shipped map places a
// `camera_animated`.
#include "Misc/AutomationTest.h"

#include "Tests/ElysiumArmTier.h"

#if WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS

#include "ElysiumAnimEvent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "Misc/ScopeExit.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumAnimEvents.h"
#include "Substrate/ElysiumCameraAnimated.h"
#include "Tests/ElysiumTestServices.h"

namespace ElysiumCameraAnimatedThinkTests
{
static constexpr EAutomationTestFlags GFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

static const TCHAR* const RigModel = TEXT("models/props/camrig.mdl");
static const TCHAR* const RigStem = TEXT("camrig");
static const TCHAR* const RigClip = TEXT("swoop");

// `special-case.txt`'s `Animated` block, the shot `FUN_10070690` names for the `CamMode 4` camera.
static void InstallAnimatedShot()
{
	TArray<FElysiumCameraShotDef> Blocks;
	FElysiumCameraShotDef Animated;
	Animated.Name = TEXT("Animated");
	Animated.Start.bPresent = true;
	Animated.Start.Position = EElysiumShotPosition::Named;
	Animated.Start.AttachPos = TEXT("Bone: cam_bone");
	Animated.Start.AttachPoint = EElysiumShotAttachPos::Bone;
	Animated.Start.AttachPointName = TEXT("cam_bone");
	Animated.Start.Attach = EElysiumShotAttach::Follow;
	Animated.Constraints.FieldOfView = 42.0f;
	Blocks.Add(Animated);
	ElysiumCameraShots::InstallNamed(TEXT("special-case"), Blocks);
}

static float CounterValue(const FElysiumEntity* Entity)
{
	if (Entity == nullptr)
	{
		return -1.0f;
	}
	TArray<TPair<FString, FString>> State;
	Entity->GetDebugState(State);
	for (const TPair<FString, FString>& Row : State)
	{
		if (Row.Key == TEXT("Value")) { return FCString::Atof(*Row.Value); }
	}
	return -1.0f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraAnimatedThinkOrderTest,
	"Elysium.Arm.CameraAnimated.ThinkOrder", GFlags)
bool FElysiumCameraAnimatedThinkOrderTest::RunTest(const FString&)
{
	ElysiumCameraShots::FlushCache();
	ON_SCOPE_EXIT { ElysiumCameraShots::FlushCache(); };
	InstallAnimatedShot();

	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	Services.ClipSeconds = 2.0f;   // a one-shot: cycle rate 0.5 per second at playback rate 1.0
	Services.AnimatedPropModels.Add(RigModel, RigStem);
	// The rig's sequence authors one 2070 at cycle 0.25, and the pose layer names that clip.
	FElysiumAnimEvent Script;
	Script.Cycle = 0.25f;
	Script.Event = 2070;
	Services.NpcEventTimelines.Add(FElysiumRecordingServices::EventTimelineKey(RigStem, RigClip))
		.Add(Script);
	Services.bBodyClipPhaseSet = true;
	Services.BodyClipPhase.OwnerStem = RigStem;
	Services.BodyClipPhase.Label = RigClip;
	Services.BodyClipPhase.Length = 2.0f;
	Services.BodyClipPhase.PlayId = 1;

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__camera_animated_think__");
		FElysiumEntityDef Camera;
		Camera.Classname = TEXT("camera_animated");
		Camera.TargetName = TEXT("animcam");
		Camera.Keys.Add(TEXT("model"), RigModel);
		Camera.Keys.Add(TEXT("animname"), RigClip);
		Camera.Keys.Add(TEXT("spawnflags"), TEXT("0"));
		FElysiumOutputDef Complete;
		Complete.Name = TEXT("OnCameraComplete");
		Complete.Target = TEXT("counter1");
		Complete.Input = TEXT("Add");
		Complete.Param = TEXT("100");
		Camera.Outputs.Add(Complete);
		Defs.Defs.Add(MoveTemp(Camera));
		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = TEXT("counter1");
		Defs.Defs.Add(MoveTemp(Counter));
		World.Load(MoveTemp(Defs));
		World.SpawnPlayer();
		World.Activate(0.0);
	}

	FElysiumEntity* Ent = World.FindByName(TEXT("animcam"));
	if (!TestTrue(TEXT("camera_animated resolves as its own leaf"), Ent != nullptr
		&& Ent->Class != nullptr && !Ent->Class->bStub
		&& Ent->Class->ClassName == FName(TEXT("camera_animated"))))
	{
		return false;
	}
	FElysiumCameraAnimated* Cam = static_cast<FElysiumCameraAnimated*>(Ent);
	const FElysiumEntity* Count = World.FindByName(TEXT("counter1"));

	// Every `animevent` the dispatcher emits for the camera, with the words as they stood when the
	// event was handed to slot 259.
	struct FSeen
	{
		FString Text;
		float CycleAtEvent = -1.0f;
		bool bAdoptedAtEvent = false;
	};
	TArray<FSeen> Seen;
	World.SetAiTraceSink([&Seen, &World, Cam](const FElysiumAiTraceEvent& Event)
	{
		if (Event.Kind == FName(TEXT("animevent")) && Event.Entity == Cam->Handle)
		{
			FSeen Row;
			Row.Text = Event.Text;
			Row.CycleAtEvent = Cam->SequenceWords.Cycle;
			Row.bAdoptedAtEvent = World.HasScriptedCamera();
			Seen.Add(MoveTemp(Row));
		}
	});
	ON_SCOPE_EXIT { World.SetAiTraceSink(FElysiumAiTraceSink()); };

	World.Tick(1.0);
	World.AcceptInput(TEXT("animcam"), FName(TEXT("StartCamera")), FElysiumVariant::Void(),
		FElysiumEntityHandle(), FElysiumEntityHandle());
	TestTrue(TEXT("0x10071550: StartCamera adopted the camera"), World.HasScriptedCamera());
	TestEqual(TEXT("0x10090950 ResetSequenceInfo: m_flLastEventCheck (+0x658) starts at 0"),
		Cam->SequenceWords.LastEventCheck, 0.0f);
	TestFalse(TEXT("0x10090950 ResetSequenceInfo: m_bSequenceFinished (+0x65c) starts clear"),
		Cam->SequenceWords.bSequenceFinished);

	// The first think: 0.15 s in, cycle 0.075; the window ends 0.1 s of clip time ahead, at 0.125.
	World.Tick(1.15);
	TestEqual(TEXT("0x10071840 slot 250: the advance wrote m_flCycle from the sequence's own clock"),
		Cam->SequenceWords.Cycle, 0.075f, 1.e-3f);
	TestEqual(TEXT("0x10091880: m_flLastEventCheck (+0x658) = m_flCycle + 0.1 x cycle rate"),
		Cam->SequenceWords.LastEventCheck, 0.125f, 1.e-3f);
	TestEqual(TEXT("0x10091880: nothing authored inside [0, 0.125)"), Seen.Num(), 0);

	// 0.45 s in: cycle 0.225, look-ahead end 0.275. The 2070 at 0.25 is inside the window although
	// the pose has not reached it.
	World.Tick(1.45);
	if (TestEqual(TEXT("0x10091880: the 2070 inside the look-ahead window is dispatched once"),
		Seen.Num(), 1))
	{
		TestTrue(TEXT("0x10091da0: 2070 is handed to the camera's slot 259"),
			Seen[0].Text.StartsWith(TEXT("2070")));
		TestEqual(TEXT("0x10071840: the advance ran before the dispatch"),
			Seen[0].CycleAtEvent, 0.225f, 1.e-3f);
		TestTrue(TEXT("0x10071840: the dispatch ran before the finish test"), Seen[0].bAdoptedAtEvent);
	}
	TestTrue(TEXT("0x10071840: m_bSequenceFinished clear, the think re-arms"),
		World.HasScriptedCamera());

	// 1.85 s in: cycle 0.925, look-ahead end 0.975 — short of 1.0.
	World.Tick(2.85);
	TestFalse(TEXT("0x10091880: a look-ahead end below 1.0 leaves m_bSequenceFinished clear"),
		Cam->SequenceWords.bSequenceFinished);
	TestTrue(TEXT("0x10071840: and the camera runs on"), World.HasScriptedCamera());
	TestEqual(TEXT("0x10091880: the window's start is the stored end, so 2070 does not fire twice"),
		Seen.Num(), 1);

	// 1.96 s in: cycle 0.98, look-ahead end 1.03 -> finished, 0.04 s before the pose's own end at
	// 3.0. A time compare against the clip's length would still say "running" here.
	World.Tick(2.96);
	TestFalse(TEXT("0x10071840: the dispatcher's finish flag ends the camera one look-ahead early"),
		World.HasScriptedCamera());
	TestEqual(TEXT("0x10071660: a finished camera_animated stops thinking"), Cam->NextThink,
		ELYSIUM_NEVER_THINK);
	World.Tick(2.98);
	TestEqual(TEXT("0x10071660: OnCameraComplete fired, before the pose's end"),
		CounterValue(Count), 100.0f);
	return true;
}

// `CCameraAnimated::HandleAnimEvent` `0x10071900`, the camera's own slot 259 (packet S9 item 5):
// event 1003 with `atoi(options)` in 1..8 fires `OnScriptEvent01..08`, activator and caller the
// camera; every other id falls to `CBaseAnimating::HandleAnimEvent` `0x10091da0`.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumCameraAnimatedScriptEventTest,
	"Elysium.Arm.CameraAnimated.ScriptEvent", GFlags)
bool FElysiumCameraAnimatedScriptEventTest::RunTest(const FString&)
{
	FElysiumRecordingServices Services;
	Services.bHasPlayer = true;
	Services.ClipSeconds = 2.0f;
	Services.AnimatedPropModels.Add(RigModel, RigStem);

	FElysiumEntityWorld World(nullptr, nullptr, Services.Bundle());
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__camera_animated_script_event__");
		FElysiumEntityDef Camera;
		Camera.Classname = TEXT("camera_animated");
		Camera.TargetName = TEXT("animcam");
		Camera.Keys.Add(TEXT("model"), RigModel);
		Camera.Keys.Add(TEXT("animname"), RigClip);
		Camera.Keys.Add(TEXT("spawnflags"), TEXT("0"));
		// All eight outputs are wired, so an index that strayed outside 1..8 onto a neighbour shows.
		for (int32 N = 1; N <= 8; ++N)
		{
			FElysiumOutputDef Wire;
			Wire.Name = FString::Printf(TEXT("OnScriptEvent%02d"), N);
			Wire.Target = TEXT("counter1");
			Wire.Input = TEXT("Add");
			Wire.Param = FString::FromInt(N);
			Camera.Outputs.Add(Wire);
		}
		Defs.Defs.Add(MoveTemp(Camera));
		FElysiumEntityDef Counter;
		Counter.Classname = TEXT("math_counter");
		Counter.TargetName = TEXT("counter1");
		Defs.Defs.Add(MoveTemp(Counter));
		World.Load(MoveTemp(Defs));
		World.SpawnPlayer();
		World.Activate(0.0);
	}

	FElysiumEntity* Ent = World.FindByName(TEXT("animcam"));
	if (!TestTrue(TEXT("camera_animated resolves as its own leaf"), Ent != nullptr
		&& Ent->Class != nullptr && !Ent->Class->bStub
		&& Ent->Class->ClassName == FName(TEXT("camera_animated"))))
	{
		return false;
	}
	FElysiumCameraAnimated* Cam = static_cast<FElysiumCameraAnimated*>(Ent);

	TUniquePtr<FElysiumOrderedIOSink> OwnedSink = MakeUnique<FElysiumOrderedIOSink>();
	FElysiumOrderedIOSink* Sink = OwnedSink.Get();
	World.AddSink(MoveTemp(OwnedSink));
	World.Tick(1.0);
	Sink->Reset();

	auto Script = [](int32 Id, const TCHAR* Options)
	{
		FElysiumAnimEvent Event;
		Event.Event = Id;
		Event.Options = Options;
		return Event;
	};

	// Options "3": `atoi` 3, index 2, the output at `+0x730 + 2 * 0x18`.
	TestTrue(TEXT("0x10071907: 1003 is the camera's own arm"), Cam->HandleAnimEvent(Script(1003, TEXT("3"))));
	TestEqual(TEXT("0x1007193d: one FireOutput"), Sink->OfKind(TEXT("fire")).Num(), 1);
	TestEqual(TEXT("0x10071936: the output is OnScriptEvent03"),
		Sink->CountOf(TEXT("fire"), TEXT("animcam.OnScriptEvent03 ")), 1);
	const FString Provenance = FString::Printf(TEXT("act=%s cal=%s"),
		*Cam->Handle.ToString(), *Cam->Handle.ToString());
	TestTrue(TEXT("0x10071932: PUSH ESI twice -- the activator and the caller are the camera"),
		Sink->FirstLine(TEXT("queue"), TEXT("counter1.Add(3)")).Contains(Provenance));

	// `atoi` 0 (index -1), 9 (index 8) and the empty string (0) fire nothing.
	Sink->Reset();
	Cam->HandleAnimEvent(Script(1003, TEXT("0")));
	TestEqual(TEXT("0x10071928 JS: options \"0\" fires nothing"), Sink->OfKind(TEXT("fire")).Num(), 0);
	Cam->HandleAnimEvent(Script(1003, TEXT("9")));
	TestEqual(TEXT("0x1007192d JGE: options \"9\" fires nothing"), Sink->OfKind(TEXT("fire")).Num(), 0);
	Cam->HandleAnimEvent(Script(1003, TEXT("")));
	TestEqual(TEXT("0x1007191f atoi: empty options fire nothing"), Sink->OfKind(TEXT("fire")).Num(), 0);

	// The bounds themselves: 1 and 8 are inside.
	Cam->HandleAnimEvent(Script(1003, TEXT("1")));
	Cam->HandleAnimEvent(Script(1003, TEXT("8")));
	TestEqual(TEXT("0x10071927 DEC: options \"1\" is index 0, OnScriptEvent01"),
		Sink->CountOf(TEXT("fire"), TEXT("animcam.OnScriptEvent01 ")), 1);
	TestEqual(TEXT("0x1007192a CMP EAX,8: options \"8\" is index 7, OnScriptEvent08"),
		Sink->CountOf(TEXT("fire"), TEXT("animcam.OnScriptEvent08 ")), 1);

	// Any other id is the base's: `0x10071912` -> `0x10091da0`.
	Sink->Reset();
	const FElysiumAnimEvent Other = Script(1004, TEXT("3"));
	TestEqual(TEXT("0x10071912: id 1004 gets the base handler's answer"),
		Cam->HandleAnimEvent(Other), Cam->FElysiumAnimating::HandleAnimEvent(Other));
	TestEqual(TEXT("0x10071912: and fires no script output"), Sink->OfKind(TEXT("fire")).Num(), 0);
	return true;
}

}   // namespace ElysiumCameraAnimatedThinkTests

#endif   // WITH_DEV_AUTOMATION_TESTS && ELYSIUM_WITH_ARM_TESTS
