#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumCameraService.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumDlg.h"
#include "ElysiumDialogueCamera.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumSkeletalBasis.h"
#include "Player/ElysiumCameraShots.h"

#include "Camera/CameraTypes.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Misc/ScopeExit.h"
#include "UObject/UObjectGlobals.h"

static constexpr EAutomationTestFlags GElysiumDialogueCameraTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	class FDialogueCameraRecordingService final : public IElysiumCameraService
	{
	public:
		int32 AcquireCount = 0;
		int32 UpdateCount = 0;
		int32 ReleaseCount = 0;
		int32 NextSlot = 1;
		bool bEnabled = true;
		bool bAcceptCandidates = true;
		// Reject only the retail source shot, so the director walks on to the authored grammar with
		// the same conversation's `default_camera` still standing behind it. That is the one arm the
		// port has that retail does not, and the arm the DialogPOV carry-over is about.
		bool bAcceptSourceShots = true;
		TSet<int32> LiveSlots;
		FElysiumCameraRequest LastRequest;
		FElysiumResolvedCameraState Resolved;

		virtual FElysiumCameraHandle AcquireCamera(const FElysiumCameraRequest& Request) override
		{
			FElysiumCameraHandle Handle;
			Handle.Slot = NextSlot++;
			Handle.Generation = 1;
			Handle.Epoch = 77;
			LiveSlots.Add(Handle.Slot);
			LastRequest = Request;
			++AcquireCount;
			return Handle;
		}
		virtual bool UpdateCamera(FElysiumCameraHandle Handle,
			const FElysiumCameraRequest& Request) override
		{
			if (!IsCameraLive(Handle))
			{
				return false;
			}
			LastRequest = Request;
			++UpdateCount;
			return true;
		}
		virtual bool ReleaseCamera(FElysiumCameraHandle Handle) override
		{
			if (Handle.Epoch != 77 || LiveSlots.Remove(Handle.Slot) == 0)
			{
				return false;
			}
			++ReleaseCount;
			return true;
		}
		virtual bool IsCameraLive(FElysiumCameraHandle Handle) const override
		{
			return Handle.Epoch == 77 && LiveSlots.Contains(Handle.Slot);
		}
		virtual bool EvaluateDialogueCandidate(const FElysiumCameraRequest& Request,
			FString& OutReason) const override
		{
			if (!bAcceptSourceShots && Request.Fallback == EElysiumCameraFallback::SourceShot)
			{
				OutReason = TEXT("source shot refused");
				return false;
			}
			OutReason = bAcceptCandidates ? FString() : FString(TEXT("test rejection"));
			return bAcceptCandidates;
		}
		virtual bool DialogueCamerasEnabled() const override { return bEnabled; }
		virtual void GetDialogueProfiles(TArray<FElysiumDialogueCameraProfile>& Out) const override
		{
			Out = ElysiumDialogueCamera::DefaultProfiles();
		}
		virtual const FElysiumResolvedCameraState& ResolvedCamera() const override { return Resolved; }
	};

	TSharedRef<FElysiumDlgConversation> MakeOneLineConversation()
	{
		TSharedRef<FElysiumDlgFile> File = MakeShared<FElysiumDlgFile>();
		FElysiumDlgLine& Line = File->Lines.AddDefaulted_GetRef();
		Line.Id = 1;
		Line.TextMale = TEXT("Test line");
		Line.Link = TEXT("#");
		Line.Role = EElysiumDlgRole::NpcLine;
		File->IndexById.Add(1, 0);
		TSharedRef<FElysiumDlgConversation> Conversation = MakeShared<FElysiumDlgConversation>(
			File, true, false, [](const FString&) { return true; }, [](const FString&) {});
		Conversation->Start();
		return Conversation;
	}

	FElysiumEntityDefs MakeDialogueWorldDefs()
	{
		FElysiumEntityDefs Defs;
		Defs.MapName = TEXT("__dialogue_camera_test__");
		FElysiumEntityDef Npc;
		Npc.Classname = TEXT("npc_VPedestrian");
		Npc.TargetName = TEXT("speaker");
		Npc.Origin = FVector(200.0f, 0.0f, 0.0f);
		Defs.Defs.Add(MoveTemp(Npc));
		FElysiumEntityDef SecondNpc;
		SecondNpc.Classname = TEXT("npc_VPedestrian");
		SecondNpc.TargetName = TEXT("speaker2");
		SecondNpc.Origin = FVector(250.0f, 100.0f, 0.0f);
		Defs.Defs.Add(MoveTemp(SecondNpc));
		return Defs;
	}

	UElysiumCameraService* MakeHeadlessCameraService()
	{
		// ULocalPlayerSubsystem has ClassWithin=ULocalPlayer. A transient local player gives the
		// subsystem its real lifetime owner without constructing a world, viewport, controller or RHI.
		ULocalPlayer* LocalPlayer = GEngine ? NewObject<ULocalPlayer>(GEngine) : nullptr;
		if (!LocalPlayer)
		{
			return nullptr;
		}
		return NewObject<UElysiumCameraService>(LocalPlayer);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDialogueCameraSessionTest,
	"Elysium.Substrate.DialogueCamera.Session", GElysiumDialogueCameraTestFlags)

bool FElysiumDialogueCameraSessionTest::RunTest(const FString&)
{
	const FElysiumClassRegistry& Registry = FElysiumClassRegistry::Get();
	const FElysiumClassDesc* NpcClass = Registry.Find(TEXT("npc_VPedestrian"));
	if (!TestNotNull(TEXT("NPC Tier-1 class"), NpcClass))
	{
		return false;
	}
	const FElysiumInputThunk Forced = Registry.FindInput(*NpcClass, TEXT("StartPlayerDialog"));
	const FElysiumInputThunk Remote = Registry.FindInput(*NpcClass, TEXT("StartPlayerDialogRemote"));
	const FElysiumInputThunk Unforced = Registry.FindInput(*NpcClass, TEXT("StartPlayerDialogUnforced"));
	TestTrue(TEXT("all three opener names have Tier-1 bindings"), Forced && Remote && Unforced);
	TestTrue(TEXT("forced and remote enter distinct handlers"), Forced != Remote);
	TestTrue(TEXT("remote and unforced enter distinct handlers"), Remote != Unforced);
	const FElysiumFieldAccessor* DefaultCamera =
		Registry.FindField(*NpcClass, TEXT("default_camera"));
	TestTrue(TEXT("default_camera is definition-derived non-save state"),
		DefaultCamera && DefaultCamera->bKeyable && !DefaultCamera->bSave);
	auto ProbeOpener = [](FName Input, int32 Argument)
	{
		FElysiumEntityWorld Probe(nullptr, nullptr);
		Probe.Load(MakeDialogueWorldDefs());
		Probe.SpawnPlayer();
		Probe.Activate(0.0);
		Probe.Tick(0.0);
		FElysiumEntity* ProbeSpeaker = Probe.FindByName(TEXT("speaker"));
		if (!ProbeSpeaker)
		{
			return FString();
		}
		Probe.AcceptInput(ProbeSpeaker->Handle, Input, FElysiumVariant::Int(Argument),
			FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid());
		TArray<TPair<FString, FString>> Rows;
		ProbeSpeaker->GetDebugState(Rows);
		for (const TPair<FString, FString>& Row : Rows)
		{
			if (Row.Key == TEXT("In dialog"))
			{
				return Row.Value;
			}
		}
		return FString();
	};
	TestTrue(TEXT("Remote discards the authored 256 variant before session decode"),
		ProbeOpener(TEXT("StartPlayerDialogRemote"), 256).Contains(TEXT("raw=0")));
	TestTrue(TEXT("ordinary opener preserves its unresolved integer without decoding bits"),
		ProbeOpener(TEXT("StartPlayerDialog"), 256).Contains(TEXT("raw=256 decoded=0")));

	FDialogueCameraRecordingService Camera;
	FElysiumWorldServices Services;
	Services.Camera = &Camera;
	FElysiumEntityWorld World(nullptr, nullptr, Services);
	World.Load(MakeDialogueWorldDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0); // deterministic NPC admission, no executor action
	FElysiumEntity* Speaker = World.FindByName(TEXT("speaker"));
	if (!TestNotNull(TEXT("dialogue speaker"), Speaker))
	{
		return false;
	}

	World.OpenDialog(Speaker->Handle, MakeOneLineConversation(),
		EElysiumDialogOpenerKind::Remote, 256, FString());
	TestEqual(TEXT("one request is acquired before presentation"), Camera.AcquireCount, 1);
	TestEqual(TEXT("one conversation owns one live request"), Camera.LiveSlots.Num(), 1);
	TestTrue(TEXT("missing source shot establishes from the authored grammar"),
		Camera.LastRequest.Fallback == EElysiumCameraFallback::AuthoredProfile);
	TestEqual(TEXT("dialogue blocks saving with a stable reason"),
		World.ScriptedSessionSaveBlockReason(), FString(TEXT("a conversation is open")));

	World.RefreshDialogueCamera();
	TestEqual(TEXT("anchor tracking updates the existing request"), Camera.UpdateCount, 1);
	TestEqual(TEXT("tracking never acquires another request"), Camera.AcquireCount, 1);

	World.OpenDialog(Speaker->Handle, MakeOneLineConversation(),
		EElysiumDialogOpenerKind::Remote, 0, FString());
	TestEqual(TEXT("silent replacement releases the prior request"), Camera.ReleaseCount, 1);
	TestEqual(TEXT("replacement acquires exactly one fresh request"), Camera.AcquireCount, 2);
	TestEqual(TEXT("replacement still leaves one live request"), Camera.LiveSlots.Num(), 1);

	World.CloseDialog(/*bSilent*/ true);
	TestEqual(TEXT("normal abort releases the scoped request"), Camera.ReleaseCount, 2);
	TestEqual(TEXT("close leaves no request live"), Camera.LiveSlots.Num(), 0);
	TestTrue(TEXT("ordinary state is saveable after dialogue closes"),
		World.ScriptedSessionSaveBlockReason().IsEmpty());

	Camera.bEnabled = false;
	World.OpenDialog(Speaker->Handle, MakeOneLineConversation(),
		EElysiumDialogOpenerKind::Remote, 0, TEXT("missing-shot"));
	TestEqual(TEXT("disabled preference still owns one scoped request"), Camera.AcquireCount, 3);
	TestFalse(TEXT("disabled preference retains the current player pose"),
		Camera.LastRequest.bOverridePose);
	TestTrue(TEXT("disabled preference is classified as player-view fallback"),
		Camera.LastRequest.Fallback == EElysiumCameraFallback::PlayerView
			&& Camera.LastRequest.FallbackReason.Contains(TEXT("disabled")));
	World.CloseDialog(/*bSilent*/ true);
	TestEqual(TEXT("disabled fallback request releases normally"), Camera.ReleaseCount, 3);
	Camera.bEnabled = true;
	Camera.bAcceptCandidates = false;
	World.OpenDialog(Speaker->Handle, MakeOneLineConversation(),
		EElysiumDialogOpenerKind::Remote, 0, TEXT("missing-shot"));
	TestEqual(TEXT("unsafe grammar still owns one scoped fallback"), Camera.AcquireCount, 4);
	TestTrue(TEXT("candidate rejection is retained on player-view fallback"),
		Camera.LastRequest.Fallback == EElysiumCameraFallback::PlayerView
			&& Camera.LastRequest.FallbackReason.Contains(TEXT("test rejection")));
	World.CloseDialog(/*bSilent*/ true);
	TestEqual(TEXT("unsafe fallback request releases normally"), Camera.ReleaseCount, 4);
	Camera.bAcceptCandidates = true;

	World.OpenDialog(Speaker->Handle, MakeOneLineConversation(),
		EElysiumDialogOpenerKind::Remote, 0, FString());
	Speaker->Kill();
	World.RefreshDialogueCamera();
	TestEqual(TEXT("owner loss releases the request"), Camera.ReleaseCount, 5);
	TestEqual(TEXT("owner loss closes the dialogue"), Camera.LiveSlots.Num(), 0);

	// A null-camera world retains the dialogue and event seam without manufacturing camera state.
	FElysiumEntityWorld HeadlessWorld(nullptr, nullptr);
	HeadlessWorld.Load(MakeDialogueWorldDefs());
	HeadlessWorld.SpawnPlayer();
	HeadlessWorld.Activate(0.0);
	HeadlessWorld.Tick(0.0);
	FElysiumEntity* HeadlessSpeaker = HeadlessWorld.FindByName(TEXT("speaker"));
	if (TestNotNull(TEXT("null-camera speaker"), HeadlessSpeaker))
	{
		HeadlessWorld.OpenDialog(HeadlessSpeaker->Handle, MakeOneLineConversation());
		TestNotNull(TEXT("null camera does not prevent dialogue"), HeadlessWorld.GetOpenDialog());
	}

	FDialogueCameraRecordingService TeardownCamera;
	{
		FElysiumWorldServices TeardownServices;
		TeardownServices.Camera = &TeardownCamera;
		FElysiumEntityWorld TeardownWorld(nullptr, nullptr, TeardownServices);
		TeardownWorld.Load(MakeDialogueWorldDefs());
		TeardownWorld.SpawnPlayer();
		TeardownWorld.Activate(0.0);
		TeardownWorld.Tick(0.0);
		if (FElysiumEntity* TeardownSpeaker = TeardownWorld.FindByName(TEXT("speaker")))
		{
			TeardownWorld.OpenDialog(TeardownSpeaker->Handle, MakeOneLineConversation());
		}
	}
	TestEqual(TEXT("map teardown releases dialogue ownership"),
		TeardownCamera.ReleaseCount, 1);
	TestEqual(TEXT("map teardown leaves no request live"),
		TeardownCamera.LiveSlots.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDialogueCameraRegistryTest,
	"Elysium.Substrate.DialogueCamera.Registry", GElysiumDialogueCameraTestFlags)

bool FElysiumDialogueCameraRegistryTest::RunTest(const FString&)
{
	UElysiumCameraService* Service = MakeHeadlessCameraService();
	if (!TestNotNull(TEXT("headless camera service"), Service))
	{
		return false;
	}

	FElysiumCameraRequest Dialogue;
	Dialogue.Kind = EElysiumCameraRequestKind::Dialogue;
	Dialogue.Owner = TEXT("dialogue:test");
	Dialogue.Priority = 500;
	TestFalse(TEXT("no request is acquired outside a map epoch"),
		Service->AcquireCamera(Dialogue).IsSet());

	Service->BeginMapEpoch(41);
	const FElysiumCameraHandle First = Service->AcquireCamera(Dialogue);
	FElysiumCameraRequest Sequence = Dialogue;
	Sequence.Kind = EElysiumCameraRequestKind::Sequence;
	Sequence.Owner = TEXT("sequence:test");
	Sequence.Priority = 700;
	const FElysiumCameraHandle Second = Service->AcquireCamera(Sequence);
	TestTrue(TEXT("first request is live"), Service->IsCameraLive(First));
	TestTrue(TEXT("second request is live"), Service->IsCameraLive(Second));
	TestTrue(TEXT("out-of-order release removes only its own request"), Service->ReleaseCamera(First));
	TestFalse(TEXT("released request is no longer live"), Service->IsCameraLive(First));
	TestTrue(TEXT("later request survives older release"), Service->IsCameraLive(Second));
	TestFalse(TEXT("double release is rejected"), Service->ReleaseCamera(First));
	const FElysiumCameraHandle Reused = Service->AcquireCamera(Dialogue);
	TestEqual(TEXT("released slots are reused"), Reused.Slot, First.Slot);
	TestTrue(TEXT("slot reuse advances generation"), Reused.Generation > First.Generation);
	TestFalse(TEXT("a stale generation cannot release its replacement"),
		Service->ReleaseCamera(First));
	TestTrue(TEXT("replacement remains live after stale release"), Service->IsCameraLive(Reused));

	FElysiumCameraRequest Updated = Sequence;
	Updated.DebugName = TEXT("updated-in-place");
	TestTrue(TEXT("live handle updates in place"), Service->UpdateCamera(Second, Updated));
	Service->RetireMapEpoch(40);
	TestTrue(TEXT("a stale teardown cannot retire the current epoch"), Service->IsCameraLive(Second));
	Service->RetireMapEpoch(41);
	TestFalse(TEXT("map teardown retires all epoch handles"), Service->IsCameraLive(Second));
	TestFalse(TEXT("a stale epoch handle cannot update"), Service->UpdateCamera(Second, Sequence));
	TestFalse(TEXT("a stale epoch handle cannot release"), Service->ReleaseCamera(Second));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDialogueBodyOwnerLifecycleTest,
	"Elysium.Substrate.DialogueCamera.BodyOwnerLifecycle", GElysiumDialogueCameraTestFlags)

bool FElysiumDialogueBodyOwnerLifecycleTest::RunTest(const FString&)
{
	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MakeDialogueWorldDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);
	FElysiumEntity* First = World.FindByName(TEXT("speaker"));
	FElysiumEntity* Second = World.FindByName(TEXT("speaker2"));
	if (!TestNotNull(TEXT("first speaker"), First)
		|| !TestNotNull(TEXT("replacement speaker"), Second))
	{
		return false;
	}
	auto DebugValue = [](const FElysiumEntity& Entity, const TCHAR* Key)
	{
		TArray<TPair<FString, FString>> Rows;
		Entity.GetDebugState(Rows);
		for (const TPair<FString, FString>& Row : Rows)
		{
			if (Row.Key == Key)
			{
				return Row.Value;
			}
		}
		return FString();
	};

	World.OpenDialog(First->Handle, MakeOneLineConversation());
	TestTrue(TEXT("open session owns the first NPC body"),
		DebugValue(*First, TEXT("Body owner")).StartsWith(TEXT("Dialogue")));
	World.OpenDialog(Second->Handle, MakeOneLineConversation());
	TestEqual(TEXT("silent replacement clears displaced NPC latch"),
		DebugValue(*First, TEXT("In dialog")), FString(TEXT("no")));
	TestEqual(TEXT("silent replacement does not count a completed conversation"),
		DebugValue(*First, TEXT("Times talked")), FString(TEXT("0")));
	TestTrue(TEXT("replacement owns the second NPC body"),
		DebugValue(*Second, TEXT("Body owner")).StartsWith(TEXT("Dialogue")));

	World.CloseDialog(/*bSilent=*/false);
	TestTrue(TEXT("normal close leaves latch until queued EndDialog"),
		DebugValue(*Second, TEXT("In dialog")).StartsWith(TEXT("YES")));
	World.Tick(0.0);
	TestEqual(TEXT("queued EndDialog clears the latch"),
		DebugValue(*Second, TEXT("In dialog")), FString(TEXT("no")));
	TestEqual(TEXT("normal close counts exactly once"),
		DebugValue(*Second, TEXT("Times talked")), FString(TEXT("1")));
	TestTrue(TEXT("normal close restores no autonomous owner"),
		DebugValue(*Second, TEXT("Body owner")).StartsWith(TEXT("None")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDialogueCameraGrammarTest,
	"Elysium.Substrate.DialogueCamera.Grammar", GElysiumDialogueCameraTestFlags)

bool FElysiumDialogueCameraGrammarTest::RunTest(const FString&)
{
	const TCHAR* Forms[] = {
		TEXT("Jack"),
		TEXT("jack"),
		TEXT("JACK"),
		TEXT("Jack.txt"),
		TEXT("vdata/camerashots/Jack"),
		TEXT("vdata/camerashots/Jack.txt"),
		TEXT("vdata/CameraShots/Jack.TXT"),
		TEXT("VData/CameraShots/JACK.TXT"),
		TEXT("vdata\\camerashots\\Jack.txt"),
		TEXT("  vdata/camerashots/Jack.txt  "),
		TEXT("vdata/camerashots/Jack.txt/"),
	};
	for (const TCHAR* Form : Forms)
	{
		TestEqual(FString::Printf(TEXT("normalizes '%s'"), Form),
			ElysiumCameraShots::NormalizeKey(Form), FString(TEXT("jack")));
	}

	const TArray<FElysiumDialogueCameraProfile> Profiles =
		ElysiumDialogueCamera::DefaultProfiles();
	TestEqual(TEXT("tracked grammar has seven deterministic profiles"), Profiles.Num(), 7);
	if (Profiles.Num() != 7)
	{
		return false;
	}

	FElysiumDialogueCameraContext Context;
	Context.SpeakerEye = FVector(200.0f, 0.0f, 170.0f);
	Context.ListenerEye = FVector::UpVector * 170.0f;
	Context.ScreenSide = 1.0f;
	Context.Owner = TEXT("dialogue:test");
	const FElysiumCameraRequest Establishing =
		ElysiumDialogueCamera::BuildRequest(Profiles[0], Context);
	TestTrue(TEXT("the establishing profile publishes a dialogue pose"),
		Establishing.Kind == EElysiumCameraRequestKind::Dialogue && Establishing.bOverridePose);
	TestTrue(TEXT("the establishing profile declares subtitle-safe framing"),
		Establishing.bRequireSubtitleSafe);
	TestFalse(TEXT("dialogue grammar does not lock controls"),
		Establishing.Control == EElysiumCameraControlPolicy::Locked);

	FElysiumDialogueCameraContext Opposite = Context;
	Opposite.ScreenSide = -1.0f;
	const FElysiumCameraRequest SameSide =
		ElysiumDialogueCamera::BuildRequest(Profiles[1], Context);
	const FElysiumCameraRequest Crossed =
		ElysiumDialogueCamera::BuildRequest(Profiles[1], Opposite);
	TestTrue(TEXT("two profiles on one side preserve the axis"),
		ElysiumDialogueCamera::PreservesScreenSide(Establishing, SameSide));
	TestFalse(TEXT("a candidate across the line is rejected"),
		ElysiumDialogueCamera::PreservesScreenSide(SameSide, Crossed));

	const FElysiumCameraRequest CloseDenied =
		ElysiumDialogueCamera::BuildRequest(Profiles[5], Context);
	TestFalse(TEXT("close-up requires explicit policy"), CloseDenied.bOverridePose);
	TestTrue(TEXT("denied close-up keeps a player-view request"),
		CloseDenied.Fallback == EElysiumCameraFallback::PlayerView);
	Context.bAllowCloseUp = true;
	const FElysiumCameraRequest CloseAllowed =
		ElysiumDialogueCamera::BuildRequest(Profiles[5], Context);
	TestTrue(TEXT("explicit policy permits close-up"), CloseAllowed.bOverridePose);

	const FElysiumCameraRequest Fallback =
		ElysiumDialogueCamera::BuildRequest(Profiles[6], Context);
	TestFalse(TEXT("fallback never overrides the prior player pose"), Fallback.bOverridePose);
	TestTrue(TEXT("fallback remains a scoped Dialogue request"),
		Fallback.Kind == EElysiumCameraRequestKind::Dialogue
			&& Fallback.Fallback == EElysiumCameraFallback::PlayerView);

	UElysiumCameraService* Headless = MakeHeadlessCameraService();
	if (!TestNotNull(TEXT("headless candidate evaluator"), Headless))
	{
		return false;
	}
	FString Reason;
	TestTrue(TEXT("valid authored geometry passes headless safety"),
		Headless->EvaluateDialogueCandidate(Establishing, Reason));
	FElysiumCameraRequest NearPlane = Establishing;
	NearPlane.Shot.Origin = NearPlane.Shot.LookAt;
	TestFalse(TEXT("near-plane candidate is rejected"),
		Headless->EvaluateDialogueCandidate(NearPlane, Reason));
	TestEqual(TEXT("near-plane rejection is classified"), Reason,
		FString(TEXT("near-plane distance")));
	return true;
}

// `DialogPOV` reaches the gaze cascade from EVERY dialogue camera path, not only from the retail
// source shot.
//
// Retail has one path: `CAI_BaseNPC::MaintainAutonomousEyeDirection` (`vampire.dll` 0x1026B810)
// asks the player's ACTIVE camera entity (`GetActiveCameraEntity` 0x1017CF90, off `player+0x19B4` /
// `+0x1EC4`) for its current shot's flags (`FUN_1006EDB0`, shot-table stride 0x104, flags dword at
// `+0x20`, bit 0x10; parser 0x100721E0) and, on a set bit, aims at that camera entity's own
// position — the lens — however the shot was selected. The port has three paths, so the flag is
// resolved once off the conversation's `default_camera` and stamped on whichever request is
// published; with no source shot at all it defaults SET, which is the named modernization recorded
// in `docs/architecture/camera-architecture.md` (51 of the 66 shipped shot files set it).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumDialogueCameraPovTest,
	"Elysium.Substrate.DialogueCamera.DialogPOV", GElysiumDialogueCameraTestFlags)

bool FElysiumDialogueCameraPovTest::RunTest(const FString&)
{
	// One shot file's worth of text, with the flag as the only thing that varies between the two
	// fixtures. `DialogTarget`/`Follow` is what the shipped conversation shots use, so the resolve
	// exercises the same anchor path a real `default_camera` does.
	auto ShotText = [](const TCHAR* Flag)
	{
		return FString::Printf(TEXT(R"KV(
			CameraShotTable
			{
				Fixture
				{
					End
					{
						Position DialogTarget
						AttachPos Origin
						AttachType Follow
						OffsetOrigin "[50, 0, 65]"
					}
					Target
					{
						Point1 { Position DialogTarget AttachPos Origin AttachType Follow }
					}
					CameraConstraints { FieldOfView 40 DialogPOV %s }
				}
			}
		)KV"), Flag);
	};

	FElysiumCameraShotDef PovDef;
	FElysiumCameraShotDef PlainDef;
	if (!TestTrue(TEXT("the DialogPOV fixture shot parses"),
			ElysiumCameraShots::ParseText(ShotText(TEXT("1")), PovDef))
		|| !TestTrue(TEXT("the plain fixture shot parses"),
			ElysiumCameraShots::ParseText(ShotText(TEXT("0")), PlainDef)))
	{
		return false;
	}
	TestTrue(TEXT("the fixture's flag is the only difference"),
		PovDef.Constraints.bDialogPOV && !PlainDef.Constraints.bDialogPOV);

	// An automation run has no export mounted, so the shot table is seeded rather than loaded.
	ElysiumCameraShots::FlushCache();
	ElysiumCameraShots::Install(TEXT("pov-fixture"), PovDef);
	ElysiumCameraShots::Install(TEXT("plain-fixture"), PlainDef);
	ON_SCOPE_EXIT { ElysiumCameraShots::FlushCache(); };

	FDialogueCameraRecordingService Camera;
	FElysiumWorldServices Services;
	Services.Camera = &Camera;
	FElysiumEntityWorld World(nullptr, nullptr, Services);
	World.Load(MakeDialogueWorldDefs());
	World.SpawnPlayer();
	World.Activate(0.0);
	World.Tick(0.0);
	FElysiumEntity* Speaker = World.FindByName(TEXT("speaker"));
	FElysiumPlayer* Player = World.FindPlayer();
	if (!TestNotNull(TEXT("dialogue speaker"), Speaker) || !TestNotNull(TEXT("player"), Player))
	{
		return false;
	}

	// --- the retail path: the source shot resolved, and it sets the flag ------------------------
	World.OpenDialog(Speaker->Handle, MakeOneLineConversation(),
		EElysiumDialogOpenerKind::Remote, 0, TEXT("pov-fixture"));
	TestTrue(TEXT("the source shot is what the director selected"),
		Camera.LastRequest.Fallback == EElysiumCameraFallback::SourceShot);
	TestTrue(TEXT("the source shot's DialogPOV rides the request"), Camera.LastRequest.bDialogPOV);
	FVector Lens = FVector::ZeroVector;
	TestTrue(TEXT("a DialogPOV source shot redirects the gaze to the shot's own lens"),
		World.GetDialogueCameraGaze(Lens) == EElysiumDialogueGazeLens::ShotOrigin);
	TestTrue(TEXT("...and the lens IS the camera position, not the look-at"),
		Lens.Equals(Camera.LastRequest.Shot.Origin, 0.01f));
	TestFalse(TEXT("the lens is not the speaker's eye"), Lens.Equals(Speaker->EyePosition(), 1.0f));
	World.CloseDialog(/*bSilent*/ true);

	// --- the authored profile, standing in for the same conversation's shot ----------------------
	// The profile REPLACES the shot, so it has no flags of its own. The authored intent for this
	// conversation is still the NPC's `default_camera`, so the flag carries across.
	Camera.bAcceptSourceShots = false;
	World.OpenDialog(Speaker->Handle, MakeOneLineConversation(),
		EElysiumDialogOpenerKind::Remote, 0, TEXT("pov-fixture"));
	TestTrue(TEXT("refusing the source shot falls through to the authored grammar"),
		Camera.LastRequest.Fallback == EElysiumCameraFallback::AuthoredProfile
			&& Camera.LastRequest.bOverridePose);
	TestTrue(TEXT("the profile carries the source shot's DialogPOV"), Camera.LastRequest.bDialogPOV);
	Lens = FVector::ZeroVector;
	TestTrue(TEXT("a profile shot redirects the gaze to the profile camera's lens"),
		World.GetDialogueCameraGaze(Lens) == EElysiumDialogueGazeLens::ShotOrigin);
	TestTrue(TEXT("...and that lens is the profile's own camera origin"),
		Lens.Equals(Camera.LastRequest.Shot.Origin, 0.01f));

	// The per-frame anchor refresh rebuilds the profile request; the flag must survive it.
	World.RefreshDialogueCamera();
	TestTrue(TEXT("the anchor refresh does not drop the flag"), Camera.LastRequest.bDialogPOV);
	TestTrue(TEXT("...so the redirect survives the frame after selection"),
		World.GetDialogueCameraGaze(Lens) == EElysiumDialogueGazeLens::ShotOrigin);
	World.CloseDialog(/*bSilent*/ true);
	Camera.bAcceptSourceShots = true;

	// --- the player-view fallback ----------------------------------------------------------------
	// No candidate survives, so the request publishes no pose and the player's own view stays up.
	// That view IS the lens retail would aim at, so the redirect still answers — with the player.
	Camera.bAcceptCandidates = false;
	Player->Origin = FVector(120.f, -40.f, 0.f);
	World.OpenDialog(Speaker->Handle, MakeOneLineConversation(),
		EElysiumDialogOpenerKind::Remote, 0, FString());
	TestTrue(TEXT("the fallback keeps the player's own pose"),
		Camera.LastRequest.Fallback == EElysiumCameraFallback::PlayerView
			&& !Camera.LastRequest.bOverridePose);
	TestTrue(TEXT("a conversation with no source shot defaults DialogPOV set"),
		Camera.LastRequest.bDialogPOV);
	Lens = FVector::ZeroVector;
	TestTrue(TEXT("the fallback names the player view as the lens"),
		World.GetDialogueCameraGaze(Lens) == EElysiumDialogueGazeLens::PlayerView);
	TestTrue(TEXT("...and answers with the player's camera location"),
		Lens.Equals(Player->EyePosition(), 0.01f));
	World.CloseDialog(/*bSilent*/ true);
	Camera.bAcceptCandidates = true;

	// --- a source shot that does NOT set the flag --------------------------------------------------
	// The 15 shipped shot files that leave DialogPOV out want the NPC on the player's eye, and the
	// default must not leak past them on any path.
	World.OpenDialog(Speaker->Handle, MakeOneLineConversation(),
		EElysiumDialogOpenerKind::Remote, 0, TEXT("plain-fixture"));
	TestFalse(TEXT("a source shot without the flag does not set it"), Camera.LastRequest.bDialogPOV);
	TestTrue(TEXT("...and asks for no gaze redirect at all"),
		World.GetDialogueCameraGaze(Lens) == EElysiumDialogueGazeLens::None);
	Camera.bAcceptSourceShots = false;
	World.OpenDialog(Speaker->Handle, MakeOneLineConversation(),
		EElysiumDialogOpenerKind::Remote, 0, TEXT("plain-fixture"));
	TestTrue(TEXT("the unflagged shot still falls through to the grammar"),
		Camera.LastRequest.Fallback == EElysiumCameraFallback::AuthoredProfile);
	TestFalse(TEXT("and the profile inherits its cleared flag too"), Camera.LastRequest.bDialogPOV);
	TestTrue(TEXT("so no path redirects the gaze"),
		World.GetDialogueCameraGaze(Lens) == EElysiumDialogueGazeLens::None);
	World.CloseDialog(/*bSilent*/ true);

	// A closed conversation supplies no gaze point at all — the producer the eye pass's own
	// view-target reset stands behind.
	TestTrue(TEXT("a closed conversation supplies no gaze"),
		World.GetDialogueCameraGaze(Lens) == EElysiumDialogueGazeLens::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumJackCameraBasisTest,
	"Elysium.Substrate.DialogueCamera.JackBasis", GElysiumDialogueCameraTestFlags)

bool FElysiumJackCameraBasisTest::RunTest(const FString&)
{
	const FString JackText = TEXT(R"KV(
		CameraShotTable
		{
			Jack
			{
				End
				{
					Position DialogTarget
					AttachPos Origin
					AttachType Follow
					OffsetOrigin "[50, 0, 65]"
				}
				Target
				{
					Point1
					{
						Position DialogTarget
						AttachPos "Bone: Bip01 Head"
						AttachType None
					}
				}
				CameraConstraints
				{
					"MoveSpeed"		"500"
					"MoveAccel"		"250"
					"TurnAccel"		"30"
					"MaxTurnRate"		"[60, 60, 60]"
					"DistanceTolerance"	"5"
					"AngularTolerance"	"[10, 10, 10]"
					"FieldOfView"		"40"
					"DialogPOV"		"1"
					"SyncRotateOnMove"	"1"
				}
			}
		}
	)KV");
	// The fixture is the shipped `vdata/camerashots/jack.txt` verbatim, `AttachType None` on Point1
	// included: `None` is the offset frame, not a latch, and retail re-resolves the head bone every
	// tick regardless (`vampire.dll` `FUN_1006e8e0`).
	FElysiumCameraShotDef Def;
	if (!TestTrue(TEXT("Jack source shot parses"), ElysiumCameraShots::ParseText(JackText, Def)))
	{
		return false;
	}

	FElysiumEntityWorld World(nullptr, nullptr);
	World.Load(MakeDialogueWorldDefs());
	FElysiumEntity* Jack = World.FindByName(TEXT("speaker"));
	if (!TestNotNull(TEXT("Jack fixture"), Jack))
	{
		return false;
	}
	Jack->Origin = FVector(144.0f, -7352.0f, -199.0f) * ElysiumCam::U;
	Jack->Angles = FVector(0.0f, 190.0f, 0.0f);

	FElysiumCameraShot Shot;
	if (!TestTrue(TEXT("Jack source shot resolves"),
		FElysiumCameraDirector::Resolve(&World, Def, Jack->Handle, Shot)))
	{
		return false;
	}
	const FVector RenderedForward = ElysiumSkeletalBasis::FromSourceAngles(Jack->Angles).Vector();
	const FVector CameraOffset = Shot.Origin - Jack->Origin;
	TestTrue(TEXT("camera lies on Jack rendered forward axis"),
		FVector::DotProduct(CameraOffset.GetSafeNormal2D(), RenderedForward.GetSafeNormal2D()) > 0.9999f);
	TestTrue(TEXT("authored forward distance is preserved"),
		FMath::IsNearlyEqual(CameraOffset.Size2D(), 50.0f * ElysiumCam::U, 0.01f));
	TestTrue(TEXT("authored camera height is preserved"),
		FMath::IsNearlyEqual(CameraOffset.Z, 65.0f * ElysiumCam::U, 0.01f));
	TestTrue(TEXT("Jack shot tracks its head/eye target"), Shot.bUseLookAt);
	TestTrue(TEXT("Jack shot carries FOV 40"), FMath::IsNearlyEqual(Shot.FieldOfView, 40.0f));
	TestTrue(TEXT("Jack shot carries DialogPOV"), Def.Constraints.bDialogPOV);
	TestTrue(TEXT("Jack shot carries SyncRotateOnMove"), Def.Constraints.bSyncRotateOnMove);

	// **The whole constraints block has to reach the tracker.** `DistanceTolerance`,
	// `AngularTolerance`, `MoveAccel`, `TurnAccel` and `SyncRotateOnMove` were parsed and never read,
	// which is what left the camera panning with the head bone every frame of the conversation.
	TestTrue(TEXT("MoveSpeed reaches the resolved shot"),
		FMath::IsNearlyEqual(Shot.MoveSpeed, 500.0f * ElysiumCam::U, 0.01f));
	TestTrue(TEXT("MoveAccel reaches the resolved shot"),
		FMath::IsNearlyEqual(Shot.MoveAccel, 250.0f * ElysiumCam::U, 0.01f));
	TestTrue(TEXT("TurnAccel reaches the resolved shot"),
		FMath::IsNearlyEqual(Shot.TurnAccel, 30.0f, 0.01f));
	TestTrue(TEXT("MaxTurnRate reaches the resolved shot"),
		Shot.MaxTurnRate.Equals(FVector(60.0f, 60.0f, 60.0f), 0.01f));
	TestTrue(TEXT("DistanceTolerance reaches the resolved shot"),
		FMath::IsNearlyEqual(Shot.DistanceTolerance, 5.0f * ElysiumCam::U, 0.01f));
	TestTrue(TEXT("the 10-degree angular deadband reaches the resolved shot"),
		Shot.AngularTolerance.Equals(FVector(10.0f, 10.0f, 10.0f), 0.01f));
	TestTrue(TEXT("SyncRotateOnMove reaches the resolved shot"), Shot.bSyncRotateOnMove);

	// Jack's shot renders ~51.9 degrees horizontal at 16:9, not the authored 40. The authored number
	// is 4:3-referenced and Source is Hor+; handing it straight to Unreal was the "closer than
	// retail" divergence.
	TestTrue(TEXT("Jack's 40-degree shot renders about 51.8 degrees at 16:9"),
		FMath::IsNearlyEqual(ElysiumCam::WidenSourceFov(Shot.FieldOfView, 16.0f / 9.0f), 51.78f, 0.05f));
	TestTrue(TEXT("and exactly 40 at the 4:3 reference it was authored against"),
		FMath::IsNearlyEqual(ElysiumCam::WidenSourceFov(Shot.FieldOfView, 4.0f / 3.0f), 40.0f, 0.01f));

	// `AttachPos EyePosition` is `CBaseCombatCharacter::CalcLookData` — a FIXED offset from the
	// origin (`CBaseEntity::EyePosition`), not an animated bounds fraction and not a head bone.
	FElysiumCameraShotDef EyeDef;
	TestTrue(TEXT("an EyePosition anchor parses"), ElysiumCameraShots::ParseText(TEXT(R"KV(
		CameraShotTable { EyeShot { End { Position DialogTarget AttachPos Origin }
			Target { Point1 { Position DialogTarget AttachPos EyePosition } } } }
	)KV"), EyeDef));
	FElysiumCameraShot EyeShot;
	if (TestTrue(TEXT("the EyePosition shot resolves"),
		FElysiumCameraDirector::Resolve(&World, EyeDef, Jack->Handle, EyeShot)))
	{
		TestTrue(TEXT("an EyePosition anchor is the entity's own fixed eye"),
			EyeShot.LookAt.Equals(Jack->EyePosition(), 0.01f));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
