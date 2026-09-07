// `Elysium.Content.TerminalCamera` — the Play-tier terminal witness, run headless.
//
// `docs/project/camera_scripted.md` §9 rows this beat as **M8** (the terminal on the shared adoption
// slot with retail's destroy rule) plus `special-case.txt`'s `Hacking` (`ShowHud 1`,
// `DrawViewmodel 0`) and `Intrusion` (`ShowHud 1`, `DrawViewmodel 1`) driving SC5's HUD edge and the
// **speed-gated** viewmodel — "the hands appear only after the dolly parks" — with both blocks on
// `AttachType Follow`, so SC6's latch never touches the shipped terminal path.
//
// The fixture is `FElysiumTerminalGym`: the movement gym's empty stage, the faithful pawn and its
// `UElysiumCameraComponent`, the `sp_tutorial_1` terminal slice with its real baked bodies, and the
// production embodiment behind them. No RHI is required and the case abstains — never fails — when
// the export, the baked plugin or the `vdata/camerashots/` corpus is not mounted.
//
// Two things the fixture cannot give, said out loud rather than worked around:
//
//  * **The port has no `Intrusion` pusher yet.** `PushCameraShotNamed` has exactly one shipped
//    caller (`FElysiumTerminal::BeginPlayerUse`, `special-case:Hacking`); the lockpick path pushes
//    no camera at all, and the gym stands no prop carrying `camera_position`/`camera_target`
//    attachments. So the `Intrusion` arm below is the **authored** `Intrusion` block — its own
//    `CameraConstraints`, its own `AttachType Follow` anchors — with only its two attachment NAMES
//    retargeted onto the monitor's `screen_axis`/`screen`, installed through
//    `ElysiumCameraShots::InstallNamed` so it stands on this fixture. Every key the case asserts on
//    is read off the shipped file before the retarget.
//  * **`FElysiumCameraDirector::Tick` cannot be driven here.** `AElysiumMapActor::CameraDirector` is
//    private with no accessor and `PostMoveTick` early-outs unless `RuntimePhase == Active`, which a
//    deferred-spawned gym map actor never reaches. The per-tick `Follow` re-resolve is therefore
//    asserted through the director's own public static `Resolve` on the `Think` pass, which is the
//    function that tick calls.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreGlobals.h"
#include "ElysiumCameraComponent.h"
#include "ElysiumCameraSolve.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumInteraction.h"
#include "ElysiumMapActor.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPawn.h"
#include "ElysiumPlayer.h"
#include "ElysiumPlayerBody.h"
#include "Components/SkeletalMeshComponent.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumTerminal.h"
#include "Tests/ElysiumTerminalGym.h"

namespace ElysiumTerminalCameraTests
{
// The flags convention every camera suite declares (`ElysiumCameraTests.cpp:120-135`).
static constexpr EAutomationTestFlags GTerminalCameraFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

static const TCHAR* const GTerminalCameraMap = TEXT("sp_tutorial_1");

// One rendered frame. `AdvanceFrame` is guarded to once per `GFrameCounter`, so the counter is
// bumped alongside it exactly as `ElysiumCameraComposeTests.cpp` does.
static constexpr float GTerminalCameraStep = 1.0f / 60.0f;

// The camera's own frame, in the two phases the manager runs it in: phase one advances the shot
// stack and the tracker, phase two solves the draw policy against the boom the rig last wrote.
static void TerminalCameraFrame(UElysiumCameraComponent& Camera)
{
	++GFrameCounter;
	Camera.AdvanceFrame(GTerminalCameraStep);
	Camera.FinalizeFrameUnguarded();
}

// `UElysiumCameraComponent::SolveShot`'s HUD line, mirrored so the case can count the edges the
// component's own (private) `FElysiumShotHudGate` issues. The component exposes only the LATCHED
// result (`GetDrawPolicy().bShowHud`), not the gate, so both are asserted: the mirror for the count,
// the component for the published value.
static bool PumpTerminalHudGate(FElysiumShotHudGate& Gate, const UElysiumCameraComponent& Camera)
{
	const FElysiumCameraShot* Cine = Camera.GetShots().TopCine();
	return Gate.OnDataChanged(Cine != nullptr, Cine ? Cine->ShotIndex : INDEX_NONE,
		!Cine || !Cine->Presentation.bNamed || Cine->Presentation.bShowHud);
}
}

using namespace ElysiumTerminalCameraTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumTerminalCameraTest, "Elysium.Content.TerminalCamera",
	GTerminalCameraFlags)
bool FElysiumTerminalCameraTest::RunTest(const FString&)
{
	if (!FElysiumTerminalGym::Available(GTerminalCameraMap))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the sp_tutorial_1 export or /ElysiumBaked is absent"));
		return true;
	}
	// The shot corpus is a separate read-only root from the map export: a run can have one and not
	// the other, and the whole case is about two authored blocks of one file.
	if (!FPaths::FileExists(FElysiumContentPaths::VdataFile(
		FString(TEXT("camerashots")) / TEXT("special-case.txt"))))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: vdata/camerashots/special-case.txt is not mounted"));
		return true;
	}

	// ------------------------------------------------------------------------------------------
	// The two authored blocks, read before anything is installed over them.
	// ------------------------------------------------------------------------------------------
	const FElysiumCameraShotDef* AuthoredHacking =
		ElysiumCameraShots::LoadNamed(TEXT("special-case"), TEXT("Hacking"));
	const FElysiumCameraShotDef* AuthoredIntrusion =
		ElysiumCameraShots::LoadNamed(TEXT("special-case"), TEXT("Intrusion"));
	if (!TestNotNull(TEXT("special-case.txt carries a Hacking block"), AuthoredHacking)
		|| !TestNotNull(TEXT("and an Intrusion block"), AuthoredIntrusion))
	{
		return false;
	}
	const FElysiumCameraShotDef HackingDef = *AuthoredHacking;
	const FElysiumCameraShotDef IntrusionDef = *AuthoredIntrusion;

	// (b) The two authored HUD/viewmodel keys, which are what SC5's gates read.
	TestTrue(TEXT("Hacking authors ShowHud 1"), HackingDef.Constraints.bShowHud);
	TestFalse(TEXT("and DrawViewmodel 0"), HackingDef.Constraints.bDrawViewmodel);
	TestTrue(TEXT("Intrusion authors ShowHud 1"), IntrusionDef.Constraints.bShowHud);
	TestTrue(TEXT("and DrawViewmodel 1"), IntrusionDef.Constraints.bDrawViewmodel);

	// (d) Both blocks take `AttachType Follow` on both anchors, so nothing about them latches:
	// retail's `FUN_1006f010` latch test reads anchor 0's flags and neither block authors a `Start`
	// at all, which is the other half of the same answer.
	TestEqual(TEXT("Hacking's End anchor follows"), HackingDef.End.Attach,
		EElysiumShotAttach::Follow);
	TestEqual(TEXT("Hacking's Target Point1 follows"), HackingDef.Target1.Attach,
		EElysiumShotAttach::Follow);
	TestEqual(TEXT("Intrusion's End anchor follows"), IntrusionDef.End.Attach,
		EElysiumShotAttach::Follow);
	TestEqual(TEXT("Intrusion's Target Point1 follows"), IntrusionDef.Target1.Attach,
		EElysiumShotAttach::Follow);
	TestFalse(TEXT("so the Hacking shot never latches its anchors"),
		ElysiumCameraShots::LatchesAnchors(HackingDef));
	TestFalse(TEXT("nor does Intrusion"), ElysiumCameraShots::LatchesAnchors(IntrusionDef));
	TestEqual(TEXT("Hacking sits on the screen_axis attachment"), HackingDef.End.AttachPoint,
		EElysiumShotAttachPos::Attachment);
	TestEqual(TEXT("named verbatim"), HackingDef.End.AttachPointName, FString(TEXT("screen_axis")));
	TestEqual(TEXT("and looks at the screen attachment"), HackingDef.Target1.AttachPointName,
		FString(TEXT("screen")));
	TestEqual(TEXT("Intrusion sits on the lockpick prop's camera_position attachment"),
		IntrusionDef.End.AttachPointName, FString(TEXT("camera_position")));
	TestEqual(TEXT("and looks at its camera_target"), IntrusionDef.Target1.AttachPointName,
		FString(TEXT("camera_target")));

	// ------------------------------------------------------------------------------------------
	// (b) The HUD edge, on the gate itself, with the two shipped blocks' real keys.
	//
	// M14: the edge is a LATCH. `HideHud(0xa06d)` / `ShowHud(0xa06d)` are issued on the shot-index
	// change and on going inactive after having been active, and the port's HUD seam is a published
	// boolean, so nothing is issued for a HUD that is already in the asked-for state. Both shipped
	// terminal blocks set `ShowHud`, so **the shipped path issues no edge at all** — which is the
	// claim, and the contrast below is what makes it an assertion rather than a tautology.
	// `m_ShotIndex` is only ever compared for inequality, so the indices here stand for records.
	// ------------------------------------------------------------------------------------------
	{
		FElysiumShotHudGate Gate;
		TestTrue(TEXT("a fresh gate has the HUD up"), Gate.bHudVisible);
		TestFalse(TEXT("adopting the Hacking shot issues no HUD edge - it authors ShowHud 1"),
			Gate.OnDataChanged(true, 0, HackingDef.Constraints.bShowHud));
		TestFalse(TEXT("re-shotting Hacking to Intrusion issues none either, because both set it"),
			Gate.OnDataChanged(true, 1, IntrusionDef.Constraints.bShowHud));
		TestFalse(TEXT("and dropping the slot issues none, the HUD never having gone down"),
			Gate.OnDataChanged(false, INDEX_NONE, true));
		TestEqual(TEXT("so the whole shipped terminal session issues zero HUD calls"),
			Gate.Issued, 0);
		TestTrue(TEXT("with the HUD up throughout"), Gate.bHudVisible);

		// The contrast: a shot whose `CameraConstraints` authors no `ShowHud` (the parse default is
		// 0, which is every story cinematic) takes the HUD down on adoption and gives it back on
		// release — exactly one edge each way, and nothing in between.
		FElysiumShotHudGate Hiding;
		TestTrue(TEXT("a shot with ShowHud clear issues one hide on adoption"),
			Hiding.OnDataChanged(true, 7, false));
		TestFalse(TEXT("the HUD is down"), Hiding.bHudVisible);
		TestFalse(TEXT("a second HUD-hiding shot replacing it re-issues nothing (M14)"),
			Hiding.OnDataChanged(true, 8, false));
		TestTrue(TEXT("going inactive issues exactly one show"),
			Hiding.OnDataChanged(false, INDEX_NONE, true));
		TestEqual(TEXT("two calls over the whole shot, which is retail's count"), Hiding.Issued, 2);
		TestTrue(TEXT("and the HUD is back up"), Hiding.bHudVisible);
	}

	// ------------------------------------------------------------------------------------------
	// The gym.
	// ------------------------------------------------------------------------------------------
	FElysiumTerminalGym Gym;
	const TArray<FString> Roots = { TEXT("tuthack") };
	if (!Gym.Build(*this, GTerminalCameraMap, Roots, FVector(300.0f, -324.0f, 0.0f), 0.0f))
	{
		return false;
	}
	AddInfo(Gym.Report());

	FElysiumPropHacking* Terminal = Gym.Terminal(TEXT("tuthack"));
	FElysiumEntityWorld* World = Gym.World();
	if (!TestNotNull(TEXT("tuthack resolves"), Terminal) || !TestNotNull(TEXT("the world"), World))
	{
		return false;
	}
	if (!TestTrue(TEXT("the monitor's screen attachments resolved off its baked model"),
		Terminal->bScreenAttachmentsResolved))
	{
		AddInfo(TEXT("ELYSIUM_TEST_ABSTAIN: the baked monitor carries no screen/screen_axis "
			"attachments; neither shot can anchor"));
		return true;
	}

	// Install the two blocks the fixture drives: `Hacking` exactly as shipped, and `Intrusion` with
	// only its two attachment names moved onto the monitor, because the gym stands no lockpick prop
	// carrying `camera_position` / `camera_target`.
	FElysiumCameraShotDef IntrusionOnGlass = IntrusionDef;
	IntrusionOnGlass.End.AttachPos = TEXT("Attachment: screen_axis");
	IntrusionOnGlass.End.AttachPointName = TEXT("screen_axis");
	IntrusionOnGlass.Target1.AttachPos = TEXT("Attachment: screen");
	IntrusionOnGlass.Target1.AttachPointName = TEXT("screen");
	const FElysiumCameraShotDef Installed[] = { HackingDef, IntrusionOnGlass };
	ElysiumCameraShots::InstallNamed(TEXT("special-case"), MakeArrayView(Installed, 2));
	ON_SCOPE_EXIT { ElysiumCameraShots::FlushCache(); };
	AddInfo(TEXT("fixture seam: the port has no Intrusion pusher and the gym stands no lockpick "
		"prop, so the authored Intrusion block is re-anchored onto the monitor's screen_axis/screen "
		"attachments; its CameraConstraints are the shipped file's, untouched"));

	// Stand the pawn square in front of the real glass, as the gym's cone case does.
	const FVector Screen = Terminal->ScreenPointCm;
	const FVector ScreenAxis = Terminal->ScreenAxisPointCm;
	const FVector Forward = (ScreenAxis - Screen).GetSafeNormal2D();
	const FVector Stand = Screen + Forward * (60.0f * ElysiumMove::U);
	Gym.PlacePawnFeet(FVector(Stand.X, Stand.Y, Terminal->Origin.Z - 60.0f),
		(-Forward).Rotation().Yaw);

	IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(Gym.Host.Pawn);
	UElysiumCameraComponent* Camera = Body ? Body->GetCameraComponent() : nullptr;
	if (!TestNotNull(TEXT("the gym's pawn carries an Elysium camera component"), Camera))
	{
		return false;
	}
	const int32 ShotsBefore = Camera->GetShots().Num();
	TestEqual(TEXT("nothing is adopted before the session"), World->CineCameraShotId(), 0);

	// The live mirror of the component's own HUD gate, pumped on every frame the component solves.
	FElysiumShotHudGate LiveGate;
	PumpTerminalHudGate(LiveGate, *Camera);

	// ------------------------------------------------------------------------------------------
	// (a) M8 — opening adopts through the shared cine slot.
	// ------------------------------------------------------------------------------------------
	const FElysiumUseBeginResult Opened =
		World->BeginPlayerUseSession(Terminal->Handle, World->PlayerHandle());
	if (!TestEqual(TEXT("+use opens the terminal session on the real body"), Opened.Outcome,
		EElysiumUseOutcome::SessionStarted))
	{
		return false;
	}
	const int32 HackingSlot = World->CineCameraShotId();
	TestTrue(TEXT("the opener adopted its camera into the world's ONE cine slot (M8)"),
		HackingSlot != 0);
	TestTrue(TEXT("which the world reports as a live scripted camera"),
		World->HasScriptedCamera());
	TestEqual(TEXT("naming the file it pushed"), World->ScriptedCameraName(),
		FString(TEXT("special-case")));
	TestFalse(TEXT("with no entity behind it - the terminal keeps no camera of its own"),
		World->CineCameraEntity().IsSet());
	TestTrue(TEXT("and the slot's occupant is disposable, so the next adoption releases it"),
		World->IsCineCameraDisposable());
	TestEqual(TEXT("the camera stack grew by exactly one"), Camera->GetShots().Num(),
		ShotsBefore + 1);

	const FElysiumCameraShot* HackingShot = Camera->GetShots().TopCine();
	int32 HackingShotIndex = INDEX_NONE;
	if (TestNotNull(TEXT("the adopted shot is on the cine channel, not the track one"), HackingShot))
	{
		HackingShotIndex = HackingShot->ShotIndex;
		TestEqual(TEXT("it is the Hacking block"), HackingShot->DebugName, FString(TEXT("Hacking")));
		TestTrue(TEXT("carrying the file's ShowHud"), HackingShot->Presentation.bShowHud);
		TestFalse(TEXT("and its cleared DrawViewmodel"), HackingShot->Presentation.bDrawViewmodel);
		TestTrue(TEXT("marked as a named shot, which is what carries either key at all"),
			HackingShot->Presentation.bNamed);
		TestTrue(TEXT("it sits on the screen_axis attachment"),
			HackingShot->Origin.Equals(ScreenAxis, 0.1f));
		TestTrue(TEXT("and looks at the screen attachment"),
			HackingShot->bUseLookAt && HackingShot->LookAt.Equals(Screen, 0.1f));
	}

	// ------------------------------------------------------------------------------------------
	// (c) With `Hacking` the hands never come back: `DrawViewmodel 0` fails SC5's gate at every
	// speed, parked or dollying.
	// ------------------------------------------------------------------------------------------
	int32 HackingHudDown = 0;
	int32 HackingViewmodelFrames = 0;
	int32 HackingFrames = 0;
	for (int32 Frame = 0; Frame < 180; ++Frame)
	{
		TerminalCameraFrame(*Camera);
		PumpTerminalHudGate(LiveGate, *Camera);
		++HackingFrames;
		HackingHudDown += Camera->GetDrawPolicy().bShowHud ? 0 : 1;
		HackingViewmodelFrames += Camera->GetDrawPolicy().bViewmodelEligible ? 1 : 0;
	}
	AddInfo(FString::Printf(
		TEXT("Hacking: %d frames, HUD down on %d of them, viewmodel eligible on %d"),
		HackingFrames, HackingHudDown, HackingViewmodelFrames));
	TestEqual(TEXT("a ShowHud 1 shot never takes the HUD down"), HackingHudDown, 0);
	TestEqual(TEXT("and DrawViewmodel 0 keeps the hands off for the whole session"),
		HackingViewmodelFrames, 0);
	// The park is the tracker's own 1-unit settle distance, not an exact landing: `FUN_10001fe0`
	// stops the moment the goal is inside `SettleDistance` and zeroes the speed there.
	TestTrue(TEXT("the Hacking dolly parks on the screen_axis attachment"),
		Camera->ScriptedShotView().Cine.bLive
			&& Camera->ScriptedShotView().Cine.Location.Equals(ScreenAxis,
				FElysiumScriptedShotTracker::SettleDistance + 0.5f));
	TestEqual(TEXT("and no HUD edge has been issued yet"), LiveGate.Issued, 0);

	// ------------------------------------------------------------------------------------------
	// The re-shot: `Intrusion` replaces `Hacking` in the same slot, exactly as `FUN_1017cef0` does.
	// ------------------------------------------------------------------------------------------
	const int32 IntrusionSlot = Gym.Host.MapActor->PushCameraShotNamed(TEXT("special-case"),
		TEXT("Intrusion"), Terminal->Handle, EElysiumShotExposure::Clamped);
	if (!TestTrue(TEXT("the Intrusion block resolves on the monitor"), IntrusionSlot != 0))
	{
		return false;
	}
	World->SetCineCamera(FElysiumEntityHandle::Invalid(), IntrusionSlot, /*bDisposable*/ true,
		TEXT("special-case"));
	TestEqual(TEXT("the shared slot now holds the Intrusion shot"), World->CineCameraShotId(),
		IntrusionSlot);
	TestTrue(TEXT("and the slot is still exactly one shot deep - the old one was released"),
		Camera->GetShots().Num() == ShotsBefore + 1);

	const FElysiumCameraShot* IntrusionShot = Camera->GetShots().TopCine();
	if (TestNotNull(TEXT("the Intrusion shot is the adopted one"), IntrusionShot))
	{
		TestTrue(TEXT("it is a different shot record than Hacking - a new m_ShotIndex"),
			IntrusionShot->ShotIndex != HackingShotIndex && IntrusionShot->ShotIndex != INDEX_NONE);
		TestTrue(TEXT("carrying ShowHud"), IntrusionShot->Presentation.bShowHud);
		TestTrue(TEXT("and DrawViewmodel"), IntrusionShot->Presentation.bDrawViewmodel);
	}
	// (b), live: the re-shot changes `m_ShotIndex` and still issues nothing, because both blocks
	// ask for the same HUD state.
	TestFalse(TEXT("the Hacking -> Intrusion re-shot issues no HUD edge"),
		PumpTerminalHudGate(LiveGate, *Camera));
	TestEqual(TEXT("the count is still zero"), LiveGate.Issued, 0);

	// ------------------------------------------------------------------------------------------
	// (c) The speed gate. `ShouldHideViewModel` is `cine && !(m_flSpeed <= 1.0 && DrawViewmodel)`,
	// so an `Intrusion` shot suppresses the hands for as long as its dolly is running and pops them
	// back the frame the tracker parks — and never before.
	// ------------------------------------------------------------------------------------------
	TArray<bool> Viewmodel;
	int32 IntrusionHudDown = 0;
	for (int32 Frame = 0; Frame < 240; ++Frame)
	{
		TerminalCameraFrame(*Camera);
		PumpTerminalHudGate(LiveGate, *Camera);
		Viewmodel.Add(Camera->GetDrawPolicy().bViewmodelEligible);
		IntrusionHudDown += Camera->GetDrawPolicy().bShowHud ? 0 : 1;
	}
	int32 LastSuppressed = INDEX_NONE;
	int32 SuppressedFrames = 0;
	for (int32 Frame = 0; Frame < Viewmodel.Num(); ++Frame)
	{
		if (!Viewmodel[Frame])
		{
			LastSuppressed = Frame;
			++SuppressedFrames;
		}
	}
	AddInfo(FString::Printf(
		TEXT("Intrusion: %d frames, hands suppressed on %d of them, last suppressed frame %d"),
		Viewmodel.Num(), SuppressedFrames, LastSuppressed));
	TestTrue(TEXT("the hands are suppressed while the Intrusion dolly is running"),
		SuppressedFrames > 1);
	TestTrue(TEXT("the suppression is the dolly's, so it ends rather than lasting the session"),
		LastSuppressed != INDEX_NONE && LastSuppressed + 1 < Viewmodel.Num());
	bool bStaysOn = true;
	for (int32 Frame = LastSuppressed + 1; Frame < Viewmodel.Num(); ++Frame)
	{
		bStaysOn &= Viewmodel[Frame];
	}
	TestTrue(TEXT("and once the dolly parks the hands stay on"), bStaysOn);
	TestTrue(TEXT("which is the frame the shot has reached its anchor"),
		Camera->ScriptedShotView().Cine.Location.Equals(ScreenAxis,
			FElysiumScriptedShotTracker::SettleDistance + 0.5f));
	TestEqual(TEXT("an Intrusion shot never takes the HUD down either"), IntrusionHudDown, 0);
	TestEqual(TEXT("and the whole Hacking -> Intrusion session issued no HUD call at all"),
		LiveGate.Issued, 0);

	// ------------------------------------------------------------------------------------------
	// (d) `AttachType Follow` means the anchors are re-read every tick, never latched at shot start.
	//
	// The per-frame re-resolve is `FElysiumCameraDirector::Tick`, which cannot be driven on this
	// fixture (see the file header), so the same work is done through the static `Resolve` the tick
	// calls, on the `Think` pass, with the monitor's attachment source moved between the two reads.
	// ------------------------------------------------------------------------------------------
	{
		USkeletalMeshComponent* const* AttachSource = Gym.Attachments.Find(TEXT("tuthack"));
		if (TestTrue(TEXT("the gym registered the monitor's attachment source"),
			AttachSource != nullptr && *AttachSource != nullptr))
		{
			auto ResolveOnce = [&](const FElysiumCameraShotDef& Def, FElysiumCameraShot& Out)
			{
				FElysiumShotBindings Bindings;
				FElysiumCameraDirector::BindAnchors(World, Def, Terminal->Handle, Bindings);
				return FElysiumCameraDirector::Resolve(World, Def, Terminal->Handle, Out, &Bindings,
					EElysiumShotResolvePass::Think);
			};

			FElysiumCameraShot BeforeHacking;
			FElysiumCameraShot BeforeIntrusion;
			const bool bResolvedBefore = ResolveOnce(HackingDef, BeforeHacking)
				&& ResolveOnce(IntrusionOnGlass, BeforeIntrusion);
			TestTrue(TEXT("both Follow shots resolve against the standing body"), bResolvedBefore);

			const FVector Nudge(0.0f, 0.0f, 25.0f);
			const FVector Held = (*AttachSource)->GetComponentLocation();
			(*AttachSource)->SetWorldLocation(Held + Nudge);
			Terminal->ResolveScreenAttachments();

			FElysiumCameraShot AfterHacking;
			FElysiumCameraShot AfterIntrusion;
			const bool bResolvedAfter = ResolveOnce(HackingDef, AfterHacking)
				&& ResolveOnce(IntrusionOnGlass, AfterIntrusion);
			TestTrue(TEXT("and again after the body moved"), bResolvedAfter);
			if (bResolvedBefore && bResolvedAfter)
			{
				TestTrue(TEXT("the Hacking origin follows the attachment rather than latching"),
					AfterHacking.Origin.Equals(BeforeHacking.Origin + Nudge, 0.1f));
				TestTrue(TEXT("and so does its look-at"),
					AfterHacking.LookAt.Equals(BeforeHacking.LookAt + Nudge, 0.1f));
				TestTrue(TEXT("the Intrusion origin follows it too"),
					AfterIntrusion.Origin.Equals(BeforeIntrusion.Origin + Nudge, 0.1f));
				TestTrue(TEXT("and its look-at"),
					AfterIntrusion.LookAt.Equals(BeforeIntrusion.LookAt + Nudge, 0.1f));
			}

			(*AttachSource)->SetWorldLocation(Held);
			Terminal->ResolveScreenAttachments();
		}
	}

	// ------------------------------------------------------------------------------------------
	// (a) The close: the slot drops to the PLAYER VIEW on the same frame, with no residual weight.
	// M1 — a cine release is a cut; `BlendOutSeconds` is not consulted anywhere on that path.
	// ------------------------------------------------------------------------------------------
	FElysiumPlayer* PlayerEntity = World->FindPlayer();
	TestTrue(TEXT("the session held the player"), PlayerEntity && !PlayerEntity->IsMobile());

	TestTrue(TEXT("the terminal session closes"),
		World->EndPlayerUseSession(Terminal->Handle, EElysiumUseEndReason::Completed));

	// No camera frame between the close and these: this is the same frame.
	TestFalse(TEXT("the cine slot is empty on the closing frame"), World->HasScriptedCamera());
	TestEqual(TEXT("carrying no shot id at all"), World->CineCameraShotId(), 0);
	TestEqual(TEXT("the camera stack is back where it started"), Camera->GetShots().Num(),
		ShotsBefore);
	TestNull(TEXT("with nothing adopted on the cine channel"), Camera->GetShots().TopCine());
	TestEqual(TEXT("and no residual scripted weight - the release is a cut, not a fade"),
		Camera->GetShots().GetWeight(), 0.0f);
	TestTrue(TEXT("the close mobilizes the player on that same frame"),
		PlayerEntity && PlayerEntity->IsMobile());

	// The next solved frame is the player's own view: no cine camera, so the mode predicate decides
	// the viewmodel again and the HUD latch is still up.
	TerminalCameraFrame(*Camera);
	TestFalse(TEXT("going inactive after a ShowHud shot issues no edge, the HUD being up already"),
		PumpTerminalHudGate(LiveGate, *Camera));
	TestEqual(TEXT("so the whole beat issued zero HUD calls, open and close alike"),
		LiveGate.Issued, 0);
	TestTrue(TEXT("the HUD is up on the frame after the close"), Camera->GetDrawPolicy().bShowHud);
	TestFalse(TEXT("the frame after the close is not third person"),
		Camera->GetDrawPolicy().bThirdPerson);
	TestTrue(TEXT("and the first-person hands are back, on the mode predicate alone"),
		Camera->GetDrawPolicy().bViewmodelEligible);
	TestFalse(TEXT("with the cine pose no longer live"), Camera->ScriptedShotView().Cine.bLive);

	return true;
}

#endif   // WITH_DEV_AUTOMATION_TESTS
