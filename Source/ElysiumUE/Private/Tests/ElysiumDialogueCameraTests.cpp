#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ElysiumCameraService.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumDlg.h"
#include "ElysiumDialogueCamera.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSkeletalBasis.h"
#include "Player/ElysiumCameraShots.h"

#include "Camera/CameraTypes.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
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
		virtual bool EvaluateDialogueCandidate(const FElysiumCameraRequest&,
			FString& OutReason) const override
		{
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
						AttachType Follow
					}
				}
				CameraConstraints
				{
					FieldOfView 40
					DialogPOV 1
					SyncRotateOnMove 1
				}
			}
		}
	)KV");
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
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
