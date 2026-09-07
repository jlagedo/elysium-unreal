#include "ElysiumCameraComponent.h"
#include "ElysiumCameraService.h"
#include "ElysiumContentPaths.h"

#include "Player/ElysiumCommandBus.h"
#include "Debug/ElysiumConsole.h"
#include "ElysiumPlayerBody.h"
#include "ElysiumStub.h"
#include "ElysiumUserSettings.h"
#include "UI/ElysiumUiArt.h"

#include "Camera/PlayerCameraManager.h"
#include "CollisionQueryParams.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumCamera, Log, All);

namespace
{
	void MarkTemporalCameraCut(const UActorComponent* Component)
	{
		const APawn* Pawn = Component ? Cast<APawn>(Component->GetOwner()) : nullptr;
		const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
		if (PC && PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->SetGameCameraCutThisFrame();
		}
	}
}

UElysiumCameraComponent::UElysiumCameraComponent()
{
	// The solve runs from CalcCamera, not from a tick (header): a component tick would run before the
	// pawn has moved and leave the boom a frame behind the body it hangs off.
	PrimaryComponentTick.bCanEverTick = false;

	// The view angles are the player's the whole time — the camera derives from them, it does not
	// take them over. That is true in third person too: `CAM_ApplyToView` overrides the *view's*
	// angles with the solved ones, it does not reparent anything.
	bUsePawnControlRotation = true;
}

void UElysiumCameraComponent::BeginPlay()
{
	Super::BeginPlay();
	RestoreCameraPrefs();
	RegisterCommands();
}

void UElysiumCameraComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	UnregisterCommands();
	Super::EndPlay(Reason);
}

// The frame

FRotator UElysiumCameraComponent::ViewRotation() const
{
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (const AController* Controller = Pawn->GetController())
		{
			return Controller->GetControlRotation();
		}
		return Pawn->GetActorRotation();
	}
	return GetComponentRotation();
}

void UElysiumCameraComponent::AdvanceFrame(float DeltaSeconds)
{
	// Once per frame: a second pass in the same frame (a scene capture, a spectator, the second
	// target of a view-target blend) must read the solved view, not blend again.
	if (LastAdvancedFrame == GFrameCounter)
	{
		return;
	}
	LastAdvancedFrame = GFrameCounter;

	const UWorld* World = GetWorld();
	// A held world holds the camera. The delta CalcCamera is handed is real time while paused, since
	// the controller keeps ticking; and while running it has already been scaled by the world's
	// dilation, which is why the weight driver is passed a time scale of 1 (the game clock's rule,
	// applied to the same problem).
	const float Dt = (World && World->IsPaused()) ? 0.0f : FMath::Max(0.0f, DeltaSeconds);

	Cvars.LoadFrom([](const TCHAR* Name) { return ElysiumCommandBus::Console().GetCvar(Name); });

	ConsumeCamCommand();
	const bool bJustEnteredFeed = Weights.bFeed && !bFeedPoseLive;
	if (bJustEnteredFeed)
	{
		bFeedPoseLive = true;
		FeedElapsedSeconds = 0.0f;
		FeedEntryYaw = ViewRotation().Yaw;
		EnsureFeedVisionMask();
	}
	else if (bFeedPoseLive)
	{
		FeedElapsedSeconds += Dt;
	}

	// The scripted channel's weight is written before the third-person driver advances, because
	// `CAM_IsThirdPerson` reads it — a dialogue camera counts as third person, which is what gets the
	// player model drawn under it for free.
	Shots.Advance(Dt);
	Weights.Scripted = Shots.GetWeight();
	Weights.Advance(Dt, /*TimeScale*/ 1.0f);
	if (!Weights.bFeed && Weights.Feed <= 0.0f)
	{
		bFeedPoseLive = false;
		FeedElapsedSeconds = 0.0f;
	}

	// The first-person case needs no zeroing here. `SolvedOffset` reaches the view and the fade band
	// only through `Weights.ThirdBlend()`, which is exactly 0 in first person, so the boom the rig
	// pushes is already inert; and the rig re-arms its own re-seed every first-person frame
	// (`AElysiumPlayerCameraManager::SolveModernRig`, VtMB's `+0x4` flag). Zeroing here as well made
	// this a second writer of a value the rig owns, and it wrote the frame *before* the rig did.
	SolveShot(Dt);
}

void UElysiumCameraComponent::FinalizeFrame()
{
	// Phase two is guarded separately from phase one: the manager calls them around its boom solve,
	// the fallback door calls them back to back, and either may be reached twice in one frame.
	//
	// **The guard is the manager's, and only the manager may take it.** `SolveDrawPolicy` is a pure
	// function of the boom the rig last wrote, so whoever runs it last in the frame wins — and the
	// answer is only right after `SolveModernRig`. A door reached *before* the manager (a scene
	// capture, a spectator, a bare `CalcCamera` on the pawn) must therefore not consume this, or the
	// manager's own call becomes a no-op and the body's alpha describes the previous frame's boom
	// while `Sample.BoomLength` describes this one. Those callers go through `FinalizeFrameUnguarded`.
	if (LastFinalizedFrame == GFrameCounter)
	{
		return;
	}
	LastFinalizedFrame = GFrameCounter;

	SolveDrawPolicy();
}

void UElysiumCameraComponent::FinalizeFrameUnguarded()
{
	// No stamp. Running twice costs one evaluation of a pure function; taking the guard costs the
	// frame's correctness.
	SolveDrawPolicy();
}

void UElysiumCameraComponent::ConsumeCamCommand()
{
	// `cam_command` is a one-shot mode request, and it is both a cvar and a bindable verb — reading it
	// here is what makes the two the same write.
	const FString Value = ElysiumCommandBus::Console().GetCvar(TEXT("cam_command"));
	const int32 Command = Value.IsEmpty() ? 0 : FCString::Atoi(*Value);
	if (Command == 1)
	{
		SetThirdPerson(true);
	}
	else if (Command == 2)
	{
		SetThirdPerson(false);
	}
}

void UElysiumCameraComponent::SolveShot(float Dt)
{
	// **The tracker follows the cine channel first.** An adopted `C_BaseCineCamera` is the shot whose
	// pose is solved (`CamMode == 1`, `FUN_10001fa0`); the `camera_track` override underneath it is a
	// value the `CInput` path re-derives every frame and needs no tracker state at all. With no cine
	// shot adopted the top track shot takes the tracker, which is what the port has always done.
	const FElysiumCameraShot* Top = Shots.TopCine();
	int32 TopId = Shots.TopCineId();
	if (!Top)
	{
		Top = Shots.TopTrack();
		TopId = Shots.TopTrackId();
	}
	if (!Top)
	{
		bShotSeeded = false;
		LastTopShotId = 0;
		return;
	}
	if (TopId != LastTopShotId)
	{
		// The deciding shot changed — a push, or a pop that revealed the one underneath. Either way it
		// cuts: the weight ramp is what blends, the shot itself does not chase in from its predecessor.
		LastTopShotId = TopId;
		bShotSeeded = false;
	}

	// A shot arriving is a *blend*, not a chase: the weight ramp is what carries the view from the
	// player's camera to the shot, so the shot itself starts where it was authored (retail's own
	// shot-start arm for a shot that carries a `Start` anchor, `FUN_10002210`). `MoveSpeed`,
	// `MoveAccel`, `MaxTurnRate`, `TurnAccel` and the two tolerances are the file's limits on the
	// shot **tracking** a moving subject afterwards, and the tracker is the one place they are read.
	if (!bShotSeeded)
	{
		ShotTracker.Start(*Top);
		bShotSeeded = true;
	}
	else
	{
		ShotTracker.Advance(*Top, Dt, Cvars.CameraFov);
	}
	ShotPosition = ShotTracker.Location;
	ShotRotation = ShotTracker.Rotation;
}

FElysiumShotPresentation UElysiumCameraComponent::ShotPresentation() const
{
	// The deciding shot is the one the stack composes, so its keys are the ones in force. An empty
	// stack answers the default, which is "not a named shot" — HUD up, viewmodel governed by the
	// mode predicate alone.
	const FElysiumCameraShot* Top = Shots.Top();
	return Top ? Top->Presentation : FElysiumShotPresentation();
}

void UElysiumCameraComponent::SolveDrawPolicy()
{
	DrawPolicy = ElysiumCam::SolveDrawPolicy(Weights, SolvedOffset, Cvars, ShotPresentation());
}

void UElysiumCameraComponent::EnsureFeedVisionMask()
{
	if (bFeedVisionMaskAttempted)
	{
		return;
	}
	bFeedVisionMaskAttempted = true;
	// `effects/spotlight`, the exact 128x128 radial mask `DrawFeedingView` samples, as the
	// texture lane imported it (R6.6).
	FeedVisionMask = ElysiumUI::ArtTexture(TEXT("effects/spotlight"));
	if (!FeedVisionMask)
	{
		UE_LOG(LogElysiumCamera, Warning,
			TEXT("ordinary feed vision cannot load the imported spotlight mask (effects/spotlight); using the renderer's oval fallback"));
	}
}
// The apply point (`CAM_ApplyToView`, 0x100ffb00)

void UElysiumCameraComponent::ApplyToView(FMinimalViewInfo& View) const
{
	ApplyBaseToView(View);
	ApplyScriptedShotToView(View);
}

void UElysiumCameraComponent::ApplyBaseToView(FMinimalViewInfo& View) const
{
	// **The lens is `default_fov`, not Unreal's component default.** Retail registers `default_fov`
	// at 75 and it is a 4:3-referenced horizontal angle under Source's Hor+ rule, so the window's own
	// aspect widens it — ~91.3 degrees at 16:9 (`docs/vtmb/source_movement.md` -> "View / camera",
	// `vfov = 2*atan(tan(hfov/2)/(4/3))`). Nothing set this before, which left the player view on
	// `UCameraComponent`'s 90 with the engine's horizontal-held constraint: a fixed horizontal angle
	// that CROPS vertically as the window widens, the opposite of Hor+.
	//
	// One rule for both channels: the scripted shot's own `FieldOfView` goes through the same
	// `WidenSourceFov` in `ApplyScriptedShotToView`, so the shot's weight lerps two angles that are
	// in the same space rather than a Source angle against an engine constant.
	View.FOV = ElysiumCam::WidenSourceFov(Cvars.DefaultFov,
		ElysiumCameraView::RenderAspectRatio(View.AspectRatio));

	// The strafe bank goes on FIRST, so the third-person blend below lerps it away along with
	// everything else. VtMB's own gate is binary — it adds a literal 0.0 in third person — but the
	// mode here is a weight rather than a flag, so the bank is scaled by the first-person share.
	// At E = 0 and E = 1 that is exactly the original; in between it fades instead of popping.
	if (const AActor* Owner = GetOwner())
	{
		const float Roll = ElysiumCam::SolveViewRoll(Owner->GetVelocity(), View.Rotation,
			Cvars.RollAngle, Cvars.RollSpeed);
		View.Rotation.Roll += Roll * (1.0f - Weights.ThirdBlend());
	}

	// **The boom is the `CInput` slot-31 branch, and an adopted cine camera skips it outright.**
	// `ClientModeShared::OverrideView` (`0x100d4040`) tests the adopted camera and calls slot 33
	// (`FUN_100ffb90`, the track override) *directly* when one is live, so `m_vecCameraOffset` and
	// `m_angCamera` never reach the view. That is not cosmetic: it is what the track override lerps
	// **from**, so leaving the boom in would compose an authored edit out of a third-person pose.
	const float E = Weights.ThirdBlend();
	if (E > 0.0f && !Shots.TopCine())
	{
		View.Location += SolvedOffset * E;
		View.Rotation = FMath::Lerp(View.Rotation, SolvedAngles, E);
	}

	// The water clearance (`GetWaterOffset`, R7.1), applied to whatever the boom left behind and
	// OUTSIDE the block above: the case it exists for — a treading or swimming body — is first
	// person, where `E` is 0 and that block never runs. A Z-only nudge, so the solved angles stand.
	View.Location.Z += ElysiumCam::SolveWaterOffset(WaterLevel,
		static_cast<float>(View.Location.Z), WaterSurfaceZCm, Cvars.WaterDist);

	// `DrawFeedingView` runs after the ordinary base camera. Its full-strength pose ignores the boom
	// and applies no collision trace; the linear feed weight is eased only here, at point of use.
	const float FeedE = Weights.FeedBlend();
	if (FeedE > 0.0f && bFeedPoseLive)
	{
		const FElysiumFeedCameraPose Feed = ElysiumCam::SolveOrdinaryFeedCamera(
			FeedElapsedSeconds, FeedEntryYaw, Cvars);
		View.Location = FMath::Lerp(View.Location, EyeLocation() + Feed.Offset, FeedE);
		View.Rotation = FMath::Lerp(View.Rotation, Feed.Rotation, FeedE);

		// Renderer-owned isolation: grayscale inside the recovered spotlight mask, pure black outside
		// at weight one. This is camera post-process state, not a HUD panel, so it covers the world and
		// every rendered body consistently at any viewport resolution.
		FPostProcessSettings& PP = View.PostProcessSettings;
		PP.bOverride_ColorSaturation = true;
		PP.ColorSaturation = FMath::Lerp(PP.ColorSaturation,
			FVector4(0.0, 0.0, 0.0, 1.0), FeedE);
		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = FeedE;
		PP.bOverride_VignetteType = true;
		PP.VignetteType = FeedVisionMask ? EVignetteType::Texture : EVignetteType::Oval;
		PP.bOverride_VignetteCenter = true;
		PP.VignetteCenter = FVector2f(0.5f, 0.5f);
		PP.bOverride_VignetteColor = true;
		PP.VignetteColor = FLinearColor::Black;
		PP.bOverride_VignetteSize = true;
		PP.VignetteSize = FVector2f(1.0f, 1.0f);
		PP.bOverride_VignetteSoftness = true;
		PP.VignetteSoftness = 1.0f;
		PP.bOverride_VignetteTexture = FeedVisionMask != nullptr;
		PP.VignetteTexture = FeedVisionMask;
	}

	// The spectator replace, `CViewRender::CalcView`'s last arm (`0x1019158e`-`0x101915f2`, RC10):
	// origin and angles hard-replaced by the entity the engine's view is on, FOV untouched. It is
	// last inside `CalcView`, so it beats the bob, the shake, the water offset and `scr_ofs*` — and
	// it is still ahead of the cine hard write, which is why a cutscene camera beats a death view.
	//
	// **Nothing sets this yet**: the port has no observer, death-cam or spectator producer. See
	// `SetSpectatedView`.
	if (SpectatedView.IsSet())
	{
		View.Location = SpectatedView->Origin;
		View.Rotation = SpectatedView->Angles;
	}

	// `camortho`: Source's orthographic debug view (`ClientModeShared::OverrideView` `100d40b6`, the
	// block the client report mis-read as an off-centre projection — M11, re-scoped by RC9). Retail
	// raises `CViewSetup::m_bOrtho` and writes the rect `(-w*0.5, -h*0.5, w*0.5, h*0.5)`; Unreal
	// already owns that projection, so the port hands it the width and lets the window's aspect give
	// the height. `c_orthoheight` is loaded and stands for retail's vertical pair.
	//
	// Retail runs the block after the `CInput` override rather than here; the position is immaterial
	// because it writes projection state and never the pose, and here it is on the path every caller
	// reaches rather than only the ones that run the legacy post layer.
	if (bOrthographic)
	{
		View.ProjectionMode = ECameraProjectionMode::Orthographic;
		View.OrthoWidth = Cvars.OrthoWidth;
	}
}

float ElysiumCameraView::RenderAspectRatio(float Fallback)
{
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const FIntPoint Size = GEngine->GameViewport->Viewport->GetSizeXY();
		if (Size.X > 0 && Size.Y > 0)
		{
			return static_cast<float>(Size.X) / static_cast<float>(Size.Y);
		}
	}
	return Fallback;
}

void UElysiumCameraComponent::ApplyScriptedShotToView(FMinimalViewInfo& View) const
{
	const FElysiumScriptedShotView Shot = ScriptedShotView();

	if (Shot.ChannelWeight() > 0.0f)
	{
		// VtMB camera tracks author exact edits and deliberate dollies, but no camera-motion blur.
		// UE's default blur turns even the small post-cut dollies into a radial smear and makes a
		// zero-time edit read as a scroll. Keep gameplay post-processing intact and suppress only the
		// scripted camera channel while it has visible weight.
		View.PostProcessSettings.bOverride_MotionBlurAmount = true;
		View.PostProcessSettings.MotionBlurAmount = 0.0f;

		// The pusher's exposure ask, for the handle's lifetime (`FElysiumShotPresentation`, the named
		// Presentation modernization in `docs/architecture/computer-terminal-architecture.md` §6.4).
		// A terminal shot frames one bright emissive panel at close range and the eye would otherwise
		// ramp the rest of the room into black around it.
		float ExposureMin = 0.0f;
		float ExposureMax = 0.0f;
		if (ElysiumCam::SolveExposureClamp(Shot.Presentation, ExposureMin, ExposureMax))
		{
			View.PostProcessSettings.bOverride_AutoExposureMinBrightness = true;
			View.PostProcessSettings.AutoExposureMinBrightness = ExposureMin;
			View.PostProcessSettings.bOverride_AutoExposureMaxBrightness = true;
			View.PostProcessSettings.AutoExposureMaxBrightness = ExposureMax;
		}
	}
	const float Aspect = ElysiumCameraView::RenderAspectRatio(View.AspectRatio);

	// **Branch one — the adopted cine camera.** `C_BaseCineCamera::CalcView` (`FUN_10001b50`) writes
	// all three outright: `v->origin = m_vecCurOrigin`, `v->angles = m_angCurAngles`,
	// `v->fov = m_flCurFov`. No blend, no lerp, no weight, and no arm anywhere on the path takes one.
	//
	// **M10 — the ordering fact, kept as a comment because the code is dead.** Retail's
	// `C_BasePlayer::CalcView` (`0x100a7770`) runs a vehicle arm *before* this one, so an adopted cine
	// camera beats a vehicle view. VtMB ships no drivable vehicle and `m_bInVehicle` /
	// `field_0x19c4` have no writer in the image, so there is no transition to reproduce — only the
	// precedence, which this ordering already has.
	if (Shot.Cine.bLive && Shot.bSeeded)
	{
		View.Location = Shot.Cine.Location;
		View.Rotation = Shot.Cine.Rotation;
		// The shot's `FieldOfView` is a **4:3-referenced** Source angle; the widening to the window's
		// own aspect happens here, at apply time, so the parsed shot keeps the authored number (M15).
		// A shot with no `FieldOfView` keeps the player's, which `WidenSourceFov` passes through.
		if (Shot.Cine.FieldOfView > 0.0f)
		{
			View.FOV = ElysiumCam::WidenSourceFov(Shot.Cine.FieldOfView, Aspect);
		}
	}

	// **Branch two — the `camera_track` override, composed over whichever base won.** Both of retail's
	// branches end in `FUN_100ffb90`; only the base differs, which is what makes the two channels one
	// camera in series rather than two rival viewpoints. At weight 1 the track wins outright; at
	// weight 0 the cine pose (or the rig's) stands.
	if (Shot.Track.bLive && Shot.Weight > 0.0f && Shot.bSeeded)
	{
		float Fov = View.FOV;
		ElysiumCam::ComposeScriptedShot(View.Location, View.Rotation, Fov,
			Shot.Track.Location, Shot.Track.Target, Shot.Track.Roll,
			ElysiumCam::WidenSourceFov(Shot.Track.FieldOfView, Aspect), Shot.Weight);
		View.FOV = Fov;
	}
}

FElysiumScriptedShotView UElysiumCameraComponent::ScriptedShotView() const
{
	FElysiumScriptedShotView Out;
	Out.Weight = Shots.GetTrackWeight();
	Out.bSeeded = bShotSeeded;
	if (const FElysiumCameraShot* Top = Shots.Top())
	{
		Out.Presentation = Top->Presentation;
	}

	// The cine channel takes the tracker's pose, because the tracker is following it (`SolveShot`).
	const FElysiumCameraShot* Cine = Shots.TopCine();
	if (Cine)
	{
		Out.Cine.bLive = true;
		Out.Cine.Location = ShotPosition;
		Out.Cine.Rotation = ShotRotation;
		Out.Cine.Roll = Cine->Roll;
		Out.Cine.FieldOfView = Cine->FieldOfView;
		Out.Cine.Target = Cine->bUseLookAt
			? Cine->LookAt
			: ElysiumCam::ScriptedShotTargetPoint(ShotPosition, ShotRotation);
	}

	if (const FElysiumCameraShot* Track = Shots.TopTrack())
	{
		Out.Track.bLive = true;
		Out.Track.Roll = Track->Roll;
		Out.Track.FieldOfView = Track->FieldOfView;
		// With no cine camera adopted the tracker is on this shot, so its solved pose is the answer.
		// With one adopted the track channel reads the shot's own values straight through — retail's
		// `CInput` override does exactly that, from the replicated fields, every frame.
		Out.Track.Location = Cine ? Track->Origin : ShotPosition;
		Out.Track.Rotation = Cine
			? (Track->bUseLookAt ? (Track->LookAt - Track->Origin).Rotation() : Track->Rotation)
			: ShotRotation;
		Out.Track.Target = Track->bUseLookAt
			? Track->LookAt
			: ElysiumCam::ScriptedShotTargetPoint(Out.Track.Location, Out.Track.Rotation);
	}
	return Out;
}

bool UElysiumCameraComponent::ConsumeTemporalCameraCutRequest()
{
	const bool bPending = bTemporalCameraCutPending;
	bTemporalCameraCutPending = false;
	return bPending;
}

bool UElysiumCameraComponent::SolveFrameFor(UElysiumCameraComponent* Camera, float DeltaSeconds,
	FMinimalViewInfo& Out)
{
	if (!Camera || !Camera->IsActive())
	{
		return false;
	}
	// `GetCameraView` FIRST: it fills FOV, the post-process settings and the first-person-rendering
	// fields, and skipping it is the documented cause of first-person rendering silently not applying.
	// It is also the only thing that advances this component's world rotation, since it does not tick.
	Camera->GetCameraView(DeltaSeconds, Out);
	Camera->AdvanceFrame(DeltaSeconds);
	return true;
}

void UElysiumCameraComponent::FinalizeFrameFor(UElysiumCameraComponent* Camera)
{
	if (Camera)
	{
		Camera->FinalizeFrame();
	}
}

bool UElysiumCameraComponent::CalcCameraFor(UElysiumCameraComponent* Camera, float DeltaSeconds,
	FMinimalViewInfo& Out)
{
	if (!SolveFrameFor(Camera, DeltaSeconds, Out))
	{
		return false;
	}
	// No rig runs between the phases on this path — a scene capture has no boom of its own — so the
	// band reads whatever boom the manager last solved. That is the right answer for a second view
	// of the same frame and the only available one for a caller outside the player's frame.
	//
	// Unguarded deliberately: this door can be reached *earlier* in the frame than the camera manager,
	// and taking the once-per-frame stamp there would leave the manager's post-rig call a no-op and the
	// draw policy a frame behind the boom it is supposed to describe.
	Camera->FinalizeFrameUnguarded();
	Camera->ApplyBaseToView(Out);
	Camera->ApplyScriptedShotToView(Out);
	if (Camera->ConsumeTemporalCameraCutRequest())
	{
		// A camera cut resets temporal histories and has zero camera velocity by definition. The base
		// camera component can still supply its pre-cut transform through PreviousViewTransform, so
		// override it with the newly applied authored view before the viewport builds this frame.
		Out.PreviousViewTransform = FTransform(Out.Rotation, Out.Location);
		MarkTemporalCameraCut(Camera);
		UE_LOG(LogElysiumCamera, Log, TEXT("applied temporal camera cut at %s"),
			*Out.Location.ToCompactString());
	}
	return true;
}

// The mode

void UElysiumCameraComponent::SetThirdPerson(bool bThird)
{
	// The minimal pair: set the latch, clear the one-shot request. No weapon arbitration and no
	// holster check — only `togglecamera` runs those, and both need weapons.
	Weights.bUserThird = bThird;
	ElysiumCommandBus::Console().SetCvar(TEXT("cam_command"), TEXT("0"));
}

void UElysiumCameraComponent::SetEquippedCameraClass(int32 CameraClass)
{
	if (EquippedCameraClass == CameraClass)
	{
		return;
	}
	EquippedCameraClass = CameraClass;
	ApplyEquippedCameraPref();
}

void UElysiumCameraComponent::ApplyEquippedCameraPref()
{
	FElysiumConsole& Console = ElysiumCommandBus::Console();
	const int32 Prefs = FCString::Atoi(*Console.GetCvar(TEXT("camera_prefs")));
	const bool bWeaponSwitch = FCString::Atoi(*Console.GetCvar(TEXT("camera_weaponswitch"))) != 0;

	switch (ElysiumCam::ApplyWeaponCameraPref(EquippedCameraClass, Prefs, bWeaponSwitch))
	{
	case ElysiumCam::EWeaponCameraAction::ToFirstPerson:
		// `CAM_ToFirstPerson` writes the **user** latch, not the forced-first one. The forced-first
		// latch's producer is unrecovered, which is also why it is correctly absent from
		// `CAM_IsThirdPerson`'s disjunction.
		Weights.bForcedThird = false;
		SetThirdPerson(false);
		break;
	case ElysiumCam::EWeaponCameraAction::ToThirdPerson:
		Weights.bForcedThird = false;
		SetThirdPerson(true);
		break;
	case ElysiumCam::EWeaponCameraAction::ForceThirdOn:
		Weights.bForcedThird = true;
		break;
	case ElysiumCam::EWeaponCameraAction::None:
		// **Class 0 does not clear a forced hold.** Switching from a katana to `item_w_unarmed`
		// (`noswitch`) leaves the forced latch set, and only `togglecamera`'s holster branch clears
		// it. That stickiness is retail's, not an oversight here.
		break;
	}
}

void UElysiumCameraComponent::ToggleCamera()
{
	// `CAM_ToggleCamera` (`0x100ff800`), in its recovered order.
	FElysiumConsole& Console = ElysiumCommandBus::Console();
	Console.SetCvar(TEXT("cam_command"), TEXT("0"));
	Weights.bUserThird = !Weights.bUserThird;

	// `LocalPlayer_CanUseThirdPerson` (player vtable `+0x250`) is **not recovered** — what makes third
	// person unavailable is unknown, so it is not guessed. The predicate answers true and reports
	// once, which is what puts it on the work list rather than silently inventing a refusal.
	ElysiumStub::Fired(TEXT("camera"), TEXT("LocalPlayer_CanUseThirdPerson"), FString(), FString(),
		TEXT("player vtable +0x250 gate is not recovered; third person is always available"));

	const int32 Prefs = FCString::Atoi(*Console.GetCvar(TEXT("camera_prefs")));
	WriteCameraPrefs(ElysiumCam::SaveWeaponCameraPref(EquippedCameraClass, Prefs, Weights.bUserThird));
	ApplyEquippedCameraPref();

	// The sniper branch is gated on item-record `+0x4fe84`, also unrecovered. The call site exists
	// and reports rather than firing `force_sniper_third_person` blind.
	ElysiumStub::Fired(TEXT("camera"), TEXT("force_sniper_third_person"), FString(), FString(),
		TEXT("item record +0x4fe84 gate is not recovered; the sniper forced-third path is not armed"));

	if (Weights.bForcedThird && EquippedCameraClass == ElysiumCam::CameraClass::ForceThird)
	{
		// Retail gates this on item-record `+0x24d0`, which is unrecovered. **Reconstruction: the
		// class is the gate** — `docs/vtmb/camera-view-modes.md` establishes behaviourally that a
		// melee weapon cannot be brought to first person at all, so toggling out of one holsters it.
		//
		// `holster` is the registered verb (`FElysiumCommands`), bound to the player controller's
		// `Holster` handler, which reaches the player class-chain's own `Holster` input — the same
		// switch-to-`item_w_unarmed` the inventory selector's `H` key drives. Going through the verb
		// rather than the inventory directly is what keeps the key bind and this branch the same
		// operation.
		const int32 ClassBeforeHolster = EquippedCameraClass;
		Console.Execute(TEXT("holster"));

		// **The camera may not commit the consequences of a holster that did not happen.** Retail's
		// body drops to first person here because `inven_holster` has put the weapon away, which
		// clears the forced class. `holster` refuses — logging a warning and leaving the draw
		// unchanged — when the character carries no `item_w_unarmed` to fall back to, and committing
		// anyway in that case would produce a state the game cannot be in — first person while
		// holding a melee weapon, the exact thing the `0x10` force exists to prevent — and it would be
		// self-perpetuating, because the class is still ForceThird on the next toggle, so this branch
		// would fire again and the camera could never leave first person at all.
		//
		// `FElysiumCombatCharacter::PublishEquippedCameraClass` is the only writer of
		// `EquippedCameraClass`, so an unchanged value here IS "the weapon is still equipped".
		if (EquippedCameraClass == ClassBeforeHolster)
		{
			// The hold stands, which is the documented behaviour for a melee weapon carried with no
			// `item_w_unarmed` fallback. `bForcedThird` is deliberately left set.
			Weights.bUserThird = true;
			ElysiumStub::Fired(TEXT("camera"), TEXT("inven_holster"), FString(), FString(),
				TEXT("the forced-third holster branch asked for a holster and the weapon stayed ")
				TEXT("equipped (no carried item_w_unarmed to fall back to); the camera keeps third ")
				TEXT("person rather than entering a first-person state a melee class forbids."));
		}
		else
		{
			Weights.bUserThird = false;
			Weights.bForcedThird = false;
		}
	}

	UE_LOG(LogElysiumCamera, Verbose, TEXT("togglecamera -> %s (class 0x%02x)"),
		Weights.bUserThird ? TEXT("third") : TEXT("first"), EquippedCameraClass);
}

UElysiumUserSettings* UElysiumCameraComponent::CameraSettings()
{
	UGameUserSettings* Base = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (!Base)
	{
		UE_LOG(LogElysiumCamera, Warning,
			TEXT("no game user settings object; the camera preference cannot be persisted or restored "
			     "and `camera_prefs` will fall back to the registered default for this session"));
		return nullptr;
	}
	UElysiumUserSettings* Settings = Cast<UElysiumUserSettings>(Base);
	if (!Settings)
	{
		UE_LOG(LogElysiumCamera, Warning,
			TEXT("game user settings is a %s, not UElysiumUserSettings; the camera preference cannot be "
			     "persisted or restored (check GameUserSettingsClassName in DefaultEngine.ini)"),
			*Base->GetClass()->GetName());
		return nullptr;
	}
	return Settings;
}

void UElysiumCameraComponent::RestoreCameraPrefs()
{
	// **The read half of the archive.** The store has already been seeded from the install's cfg by the
	// time a pawn exists, so this runs after it and deliberately wins: `bCameraPrefsStored` means the
	// player has toggled at least once in *this* build, and from then on our durable copy is the
	// authority. Until that first toggle nothing is written here and the install's `config.cfg` value
	// (or the registered default) stands, which is what makes a first run inherit the retail history.
	const UElysiumUserSettings* Settings = CameraSettings();
	if (!Settings || !Settings->bCameraPrefsStored)
	{
		return;
	}
	FElysiumConsole& Console = ElysiumCommandBus::Console();
	Console.SetCvar(TEXT("camera_prefs"), *FString::FromInt(Settings->CameraPrefs));
	Console.SetCvar(TEXT("camera_weaponswitch"), Settings->bCameraWeaponSwitch ? TEXT("1") : TEXT("0"));
	UE_LOG(LogElysiumCamera, Verbose, TEXT("restored camera_prefs %d / camera_weaponswitch %d"),
		Settings->CameraPrefs, Settings->bCameraWeaponSwitch ? 1 : 0);
}

void UElysiumCameraComponent::WriteCameraPrefs(int32 Prefs)
{
	// The live value is the console store's, so `config.cfg` and the patch aliases keep reading and
	// writing the name they always did. It is mirrored into the project's own settings because
	// retail archives it to `config.cfg` and **we must not write into the user's VtMB install**
	// (repo-root `CLAUDE.md`, Bring-your-own-game). `RestoreCameraPrefs` reads it back at `BeginPlay`.
	FElysiumConsole& Console = ElysiumCommandBus::Console();
	Console.SetCvar(TEXT("camera_prefs"), *FString::FromInt(Prefs));
	if (UElysiumUserSettings* Settings = CameraSettings())
	{
		Settings->CameraPrefs = Prefs;
		// Retail archives both names together. `camera_weaponswitch` has no verb of its own — it is set
		// from the console or a cfg — so the toggle is the point at which the live value becomes durable.
		Settings->bCameraWeaponSwitch =
			FCString::Atoi(*Console.GetCvar(TEXT("camera_weaponswitch"))) != 0;
		Settings->bCameraPrefsStored = true;
		Settings->SaveSettings();
	}
}

// The scripted-shot channel

int32 UElysiumCameraComponent::PushShot(const FElysiumCameraShot& Shot)
{
	FElysiumCameraShot StableShot = Shot;
	StableShot.bCameraCut = false; // an instruction for this frame, never persistent shot state
	const int32 Id = Shots.Push(StableShot);
	// A cine adoption is a cut by construction — it is live at full weight the frame it lands, with
	// no ramp to smear the previous view across (M1).
	if (Shot.bCine || Shot.bCameraCut || Shot.BlendSeconds <= KINDA_SMALL_NUMBER)
	{
		bTemporalCameraCutPending = true;
	}
	UE_LOG(LogElysiumCamera, Verbose, TEXT("camera shot #%d '%s' pushed"), Id, *Shot.DebugName);
	return Id;
}

bool UElysiumCameraComponent::UpdateShot(int32 Id, const FElysiumCameraShot& Shot)
{
	FElysiumCameraShot StableShot = Shot;
	StableShot.bCameraCut = false;
	if (!Shots.Update(Id, StableShot))
	{
		return false;
	}
	if (Shot.bCameraCut)
	{
		bTemporalCameraCutPending = true;
		UE_LOG(LogElysiumCamera, Log, TEXT("camera shot #%d queued temporal cut"), Id);
	}
	return true;
}

bool UElysiumCameraComponent::PopShot(int32 Id, float BlendOutSeconds)
{
	const bool bWasTop = Shots.TopId() == Id;
	const FElysiumCameraShot* Existing = Shots.Find(Id);
	// A cine release is a cut by construction (M1), so it always resets the temporal history; a track
	// release only does so when its own blend-out is instantaneous.
	const bool bWasCine = Existing && Existing->bCine;
	const float EffectiveBlend = BlendOutSeconds >= 0.0f
		? BlendOutSeconds
		: (Existing ? Existing->BlendSeconds : 0.0f);
	if (!Shots.Pop(Id, BlendOutSeconds))
	{
		return false;
	}
	if (bWasTop && (bWasCine || EffectiveBlend <= KINDA_SMALL_NUMBER))
	{
		bTemporalCameraCutPending = true;
	}
	UE_LOG(LogElysiumCamera, Verbose, TEXT("camera shot #%d popped"), Id);
	return true;
}

// The verbs

void UElysiumCameraComponent::RegisterCommands()
{
	FElysiumCommands& Registry = FElysiumCommands::Get();

	Bindings.Add(Registry.Bind(TEXT("togglecamera"), [this](const FElysiumCommandCall&) { ToggleCamera(); }));
	Bindings.Add(Registry.Bind(TEXT("thirdperson"), [this](const FElysiumCommandCall&) { SetThirdPerson(true); }));
	Bindings.Add(Registry.Bind(TEXT("firstperson"), [this](const FElysiumCommandCall&) { SetThirdPerson(false); }));

	// `cam_command` as a verb writes the same cvar the console does; `CAM_Think` consumes it either
	// way, which is the point of it being a channel rather than a call.
	Bindings.Add(Registry.Bind(TEXT("cam_command"), [](const FElysiumCommandCall& Call)
	{
		ElysiumCommandBus::Console().SetCvar(TEXT("cam_command"),
			Call.Args.IsEmpty() ? TEXT("0") : *Call.Args);
	}));

	// `snapto` / `cam_restore` return every hand-orbited axis to its cvar and re-seed the smoothing,
	// so the camera arrives at its ideal instead of easing there. One lambda for both, because they
	// are the same operation under two authored names — the patch binds `cam_restore` to `KP_5` —
	// and two implementations would drift. (`cam_snapto` is registered but its role is unverified.)
	const auto RestoreOrbit = [this](const FElysiumCommandCall&)
	{
		RequestOrbitRestore();
		RequestReseed();
	};
	Bindings.Add(Registry.Bind(TEXT("snapto"), RestoreOrbit));
	Bindings.Add(Registry.Bind(TEXT("cam_restore"), RestoreOrbit));

	// `cam_rotateleft` / `cam_rotateright` are the patch's own aliases and they step **`cam_yaw`**,
	// not the held orbit — the aliases move it in 15-degree bites, which is why they are a cvar write
	// rather than a button. The rig reads `cam_yaw` live, so the boom follows on the next frame.
	const auto StepCamYaw = [](float Degrees)
	{
		return [Degrees](const FElysiumCommandCall&)
		{
			FElysiumConsole& Console = ElysiumCommandBus::Console();
			const FString Current = Console.GetCvar(TEXT("cam_yaw"));
			const float Next = (Current.IsEmpty() ? 0.0f : FCString::Atof(*Current)) + Degrees;
			Console.SetCvar(TEXT("cam_yaw"), *FString::SanitizeFloat(Next));
		};
	};
	Bindings.Add(Registry.Bind(TEXT("cam_rotateleft"), StepCamYaw(-15.0f)));
	Bindings.Add(Registry.Bind(TEXT("cam_rotateright"), StepCamYaw(15.0f)));

	// `camortho` (`client.dll` `FUN_101001e0`) toggles `CInput+0x1b8`, the enable
	// `CAM_IsOrthographic` (slot 48) answers; `ClientModeShared::OverrideView` then raises
	// `CViewSetup::m_bOrtho` and writes the rect from `c_orthowidth` / `c_orthoheight`. A dev view —
	// no shipped content sets it — but it is a real arm, recovered by RC9, and it is ported rather
	// than left as a logging stub. An argument sets the state outright; no argument toggles, which is
	// the shape the retail command has.
	Bindings.Add(Registry.Bind(TEXT("camortho"), [this](const FElysiumCommandCall& Call)
	{
		SetOrthographic(Call.Args.IsEmpty()
			? !IsOrthographic()
			: FCString::Atoi(*Call.Args) != 0);
		UE_LOG(LogElysiumCamera, Verbose, TEXT("camortho %d (width %.1f cm)"),
			IsOrthographic() ? 1 : 0, Cvars.OrthoWidth);
	}));

	// `centerview` / `force_centerview` recentre pitch. The view belongs to the controller, so this
	// writes there — the camera derives from it and follows on its own.
	const auto CenterView = [this](const FElysiumCommandCall&)
	{
		const APawn* Pawn = Cast<APawn>(GetOwner());
		if (AController* Controller = Pawn ? Pawn->GetController() : nullptr)
		{
			FRotator Rot = Controller->GetControlRotation();
			Rot.Pitch = 0.0f;
			Rot.Roll = 0.0f;
			Controller->SetControlRotation(Rot);
		}
	};
	Bindings.Add(Registry.Bind(TEXT("centerview"), CenterView));
	Bindings.Add(Registry.Bind(TEXT("force_centerview"), CenterView));

	Bindings.RemoveAll([](const FElysiumCommandBinding& B) { return !B.IsValid(); });
}

void UElysiumCameraComponent::UnregisterCommands()
{
	FElysiumCommands& Registry = FElysiumCommands::Get();
	for (FElysiumCommandBinding& Binding : Bindings)
	{
		Registry.Unbind(Binding);
	}
	Bindings.Reset();
}

FString UElysiumCameraComponent::Describe() const
{
	return FString::Printf(
		TEXT("camera: %s (driver %s)\n")
		TEXT("  weights   third %.3f (eased %.3f)  scripted %.3f  feed %.3f  secondary %.3f\n")
		TEXT("  latches   user %d  forced-third %d  forced-first %d  feed %d\n")
		TEXT("  boom      offset %s  length %.1f cm  pitch %.1f  yaw %.1f%s\n")
		TEXT("  applied   model alpha %.2f\n")
		TEXT("  shots     %s"),
		Weights.IsThirdPerson() ? TEXT("third person") : TEXT("first person"),
		Weights.Driver(),
		Weights.Third, Weights.ThirdBlend(), Weights.Scripted, Weights.Feed, Weights.Secondary,
		Weights.bUserThird ? 1 : 0, Weights.bForcedThird ? 1 : 0,
		Weights.bForcedFirst ? 1 : 0, Weights.bFeed ? 1 : 0,
		*SolvedOffset.ToCompactString(), BoomLength(),
		SolvedAngles.Pitch, SolvedAngles.Yaw, bClipped ? TEXT("  [wall-clipped]") : TEXT(""),
		DrawPolicy.BodyAlpha,
		*Shots.Describe());
}

// Dev verb

static FAutoConsoleCommandWithWorld GElysiumCameraDump(
	TEXT("elysium.camera"),
	TEXT("Dump the player camera: the four VtMB weights, which latch is driving, the solved boom and "
	     "the scripted-shot stack."),
	FConsoleCommandWithWorldDelegate::CreateStatic([](UWorld* World)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		const UElysiumCameraComponent* Camera = Pawn
			? Pawn->FindComponentByClass<UElysiumCameraComponent>() : nullptr;
		if (!Camera)
		{
			UE_LOG(LogElysiumCamera, Warning, TEXT("no player camera in this world"));
			return;
		}
		UE_LOG(LogElysiumCamera, Display, TEXT("%s"), *Camera->Describe());
		if (const ULocalPlayer* LocalPlayer = PC ? PC->GetLocalPlayer() : nullptr)
		{
			if (const UElysiumCameraService* Service =
				LocalPlayer->GetSubsystem<UElysiumCameraService>())
			{
				TArray<FString> Requests;
				Service->DescribeRequests(Requests);
				const FElysiumResolvedCameraState& Resolved = Service->ResolvedCamera();
				UE_LOG(LogElysiumCamera, Display,
					TEXT("director: epoch=%llu requests=%d winner=%s weight=%.3f source=%s profile=%s fallback=%s pose=%s %s fov=%.1f"),
					Service->CurrentEpoch(), Requests.Num(), *Resolved.Request.DebugName,
					Resolved.Weight, *Resolved.Request.SourceShot,
					*Resolved.Request.SelectedProfile, *Resolved.Request.FallbackReason,
					*Resolved.Location.ToCompactString(), *Resolved.Rotation.ToCompactString(),
					Resolved.FieldOfView);
				for (const FString& Request : Requests)
				{
					UE_LOG(LogElysiumCamera, Display, TEXT("  request %s"), *Request);
				}
			}
		}
	}));
