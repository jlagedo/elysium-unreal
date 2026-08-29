#include "ElysiumPlayerCameraManager.h"

#include "ElysiumCameraComponent.h"
#include "ElysiumCameraModifiers.h"
#include "ElysiumCameraService.h"
#include "ElysiumLookCurve.h"
#include "ElysiumPlayerBody.h"

#include "Engine/Canvas.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

// The boom's damper has no `elysium.*` control: `cdamp_on`, `cdamp_hookesconstant` and
// `cdamp_hookesconstantwall` own it, live, through the VtMB console store. A second name for the
// same value is exactly the two-owner problem the tuning partition exists to prevent.

AElysiumPlayerCameraManager::AElysiumPlayerCameraManager()
{
	// `DefaultModifiers` is instantiated per manager in `PostInitializeComponents`, so appending the
	// class here is the whole registration. The stock camera-shake entry the base constructor added
	// is left in place — `CachedCameraShakeMod` is only bound for a `UCameraModifier_CameraShake`,
	// and removing it would quietly take every shake with it.
	DefaultModifiers.Add(UElysiumCameraModifier_LegacyShot::StaticClass());

	// VtMB's pitch clamp (`cl_pitchup` / `cl_pitchdown`, both 89). It lives here because
	// `UpdateRotation` now owns the integration and runs these limits through
	// `ProcessViewRotation`; the engine's own default is ±89.9, which would quietly widen it.
	ViewPitchMin = -ElysiumInput::PitchClampDegrees;
	ViewPitchMax = ElysiumInput::PitchClampDegrees;
}

// The view

UElysiumCameraComponent* AElysiumPlayerCameraManager::ResolveRig(AActor* Target)
{
	// Resolved per frame from whatever is currently being viewed rather than cached at `BeginPlay`:
	// the view target changes for character generation, a cutscene and a spectator.
	if (IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(Target))
	{
		return Body->GetCameraComponent();
	}
	return nullptr;
}

void AElysiumPlayerCameraManager::UpdateViewTargetInternal(FTViewTarget& OutVT, float DeltaTime)
{
	UElysiumCameraComponent* Camera = ResolveRig(OutVT.Target);
	UElysiumCameraService* ScopedCamera = nullptr;
	if (ULocalPlayer* LocalPlayer = PCOwner ? PCOwner->GetLocalPlayer() : nullptr)
	{
		ScopedCamera = LocalPlayer->GetSubsystem<UElysiumCameraService>();
	}
	if (ScopedCamera)
	{
		ScopedCamera->Advance(DeltaTime);
	}
	if (Camera)
	{
		// Advance semantic requests before the base rig. A winning Feed request drives the recovered
		// one-second weight; the camera component applies the pinned ordinary orbit after third-person.
		if (ScopedCamera)
		{
			const FElysiumResolvedCameraState& Resolved = ScopedCamera->ResolvedCamera();
			Camera->SetFeedCamera(Resolved.bActive
				&& Resolved.Request.Kind == EElysiumCameraRequestKind::Feed);
		}
		else
		{
			Camera->SetFeedCamera(false);
		}
	}

	// `SolveFrameFor` answers false without touching the view when there is no active rig, so the
	// stock dispatch below still gets an untouched POV — which is what keeps a spectator, a scene
	// capture and `UGameplayStatics::CalculateViewProjectionMatricesFromViewTarget` correct.
	if (!Camera || !UElysiumCameraComponent::SolveFrameFor(Camera, DeltaTime, OutVT.POV))
	{
		Super::UpdateViewTargetInternal(OutVT, DeltaTime);
		return;
	}

	// The rig solves first and hands its boom to the component, so the apply below reads one boom.
	SolveModernRig(*Camera, DeltaTime);

	// Phase two, now that the boom exists. The fade band is a function of how far the camera ended
	// up from the eye, so it has to run after the sweep and the damper rather than before them —
	// evaluating it inside phase one is what made the body's alpha describe the previous frame, and
	// what made `Sample.ModelAlpha` and `Sample.BoomLength` disagree in the same record.
	Camera->FinalizeFrame();

	// The **base** only — the strafe bank and the boom, at the third-person weight. The scripted
	// channel and the temporal cut are the legacy post layer's, because both have to happen after
	// the base request has been chosen: a shot composes over the rig, and a history reset must
	// describe the view that was actually rendered.
	Camera->ApplyBaseToView(OutVT.POV);

	// Scoped base requests compose between the selected player rig and the legacy post layers. The
	// service publishes policy only; this manager remains the single writer of the final POV.
	if (ScopedCamera)
	{
		ScopedCamera->ApplyToView(OutVT.POV);
	}

	// **The single write of the frame's draw policy**, from the view that is actually rendered and
	// after every layer that could change the predicate has been applied. The body applies it and
	// decides nothing; nothing else in the frame writes the surface's flags.
	if (IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(OutVT.Target))
	{
		Body->ApplyDrawPolicy(Camera->GetDrawPolicy());
	}

	PublishSample(*Camera);
}

// The modern rig.

void AElysiumPlayerCameraManager::SolveModernRig(UElysiumCameraComponent& Camera,
	float DeltaSeconds)
{
	// Once per frame: `ApplyCameraModifiers` and a second view-target pass would otherwise integrate
	// the damper twice at the same delta.
	if (ModernSolvedFrame == GFrameCounter)
	{
		return;
	}
	ModernSolvedFrame = GFrameCounter;

	if (Camera.ConsumeReseedRequest())
	{
		bModernNeedsReseed = true;
	}

	const UWorld* World = GetWorld();
	// A held world holds the camera.
	const float Dt = (World && World->IsPaused()) ? 0.0f : FMath::Max(0.0f, DeltaSeconds);

	// **The frame's tuning: the project's, with every axis the VtMB store names taken from it.** The
	// store is re-read each frame by the camera component, so `elysium.cmd cam_idealdist 50` moves the
	// boom on the next frame exactly as it does in retail.
	const ElysiumRig::FElysiumCameraRigTuning Base =
		ElysiumRig::ResolveTuning(RigTuning, Camera.GetCvars());

	// The player's own orbit, stepped from the frame's command. `snapto` / `cam_restore` hand every
	// axis back to its cvar, which is why an axis the player has not touched follows a live retune.
	if (Camera.ConsumeOrbitRestoreRequest())
	{
		ElysiumRig::RestoreOrbit(ModernOrbit);
	}
	const FElysiumUserCmd& Cmd = Camera.GetUserCmd();
	ElysiumRig::StepOrbit(ModernOrbit, Cmd.Buttons, Cmd.LookDelta, Dt, Base);

	// The orbit composes onto the cvar-owned tuning rather than replacing it, so `cam_yaw` and a
	// hand-orbited yaw add — but only until the player takes that axis over, after which it composes
	// onto the value the cvar was worth at that moment and a live retune no longer drags it. That
	// per-axis decision is the rig's, not this function's.
	const ElysiumRig::FElysiumCameraRigTuning Tuning = ElysiumRig::ComposeOrbit(Base, ModernOrbit);

	// The eye the boom hangs off, and the view it derives from. **The orbit never reaches the control
	// rotation**: the camera swings around the player, the player does not turn
	// (`docs/vtmb/camera-view-modes.md` §5 — the mode changes no movement steering).
	const FVector BodyPivot = Camera.GetComponentLocation();
	const FRotator ViewRot = PCOwner ? PCOwner->GetControlRotation() : Camera.GetComponentRotation();

	ModernAngles = ElysiumRig::BoomRotation(ViewRot, Tuning);

	// **The damper's domain is translation, never rotation.** A camera position damped in world
	// space conflates two motions that want opposite treatment: the pivot moving — stairs, crouch,
	// gait bob — wants weight, while the view turning wants none at all. Damping their sum makes the
	// camera slide around the boom's arc after every turn, because a turn moves the target the
	// length of that arc instantly. On a stick the slide is invisible: the rate is capped and
	// pre-filtered, so the target only ever steps a few centimetres per frame and the damper keeps
	// up. A mouse has no such bound — a flick can carry most of a metre of arc inside one frame, and
	// the damper then spends a quarter of a second catching up to a rotation that already finished.
	//
	// So the damper runs on the **pivot** and on the boom **length**, and the camera hangs off both
	// rigidly. Everything that should feel weighted still does; nothing that should be instant lags.
	//
	// Which of the two recovered stiffnesses applies is decided by last frame's clip state, because
	// this frame's sweep has not run yet — it needs the damped pivot to trace from. That is retail's
	// own ordering: `0x100fd0b0` selects on the flag the previous solve left behind.
	ModernPivot = bModernNeedsReseed
		? BodyPivot
		: ElysiumRig::DampPivot(ModernPivot, BodyPivot, bModernClipped, Tuning, Dt);

	// Everything downstream — the collision sweep included — hangs off the damped pivot, so the
	// probe traces from where the camera actually is rather than from where the body got to.
	const FVector Pivot = ModernPivot;

	// The sweep. A sphere against the camera channel, at the authored `cam_trace_radius`.
	// `cam_collide 0` skips it entirely, which is retail's own switch and not a debug shortcut.
	const float Desired = FMath::Clamp(Tuning.BoomLength, Tuning.DollyMin, Tuning.DollyMax);
	const FVector Reach = ElysiumRig::BoomTarget(Pivot, ModernAngles, Desired, Tuning);
	bModernClipped = false;
	float HitDistance = Desired;
	if (World && Tuning.bCollide)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ElysiumModernBoom), /*bTraceComplex*/ false,
			Camera.GetOwner());
		Params.AddIgnoredActor(Camera.GetOwner());
		FHitResult Hit;
		if (World->SweepSingleByChannel(Hit, Pivot, Reach, FQuat::Identity, ECC_Camera,
				FCollisionShape::MakeSphere(Tuning.ProbeRadius), Params))
		{
			bModernClipped = true;
			HitDistance = static_cast<float>(FVector::Dist(Pivot, Hit.Location));
		}
	}

	ModernDistance = bModernNeedsReseed
		? (bModernClipped ? FMath::Max(Tuning.MinBoomLength, HitDistance - Tuning.WallPullIn)
						  : Desired)
		: ElysiumRig::SolveBoomDistance(ModernDistance, Desired, bModernClipped, HitDistance,
			Tuning, Dt);

	// Rigid: the pivot and the length are already damped, so a second damper here would be the one
	// that puts rotation back into the lag.
	ModernPosition = ElysiumRig::BoomTarget(Pivot, ModernAngles, ModernDistance, Tuning);

	// The boom the rest of the frame reads: the fade band, the readouts and the base apply all take
	// it from here, so there is exactly one boom in the frame.
	Camera.SetSolvedBoom(ModernPosition - Camera.GetComponentLocation(), ModernAngles, bModernClipped);

	// A first-person frame parks the rig on its target rather than letting it drift, so re-entering
	// third person does not swing in from wherever the camera was left.
	bModernNeedsReseed = Camera.GetWeights().Third <= 0.0f;
}

void AElysiumPlayerCameraManager::PublishSample(const UElysiumCameraComponent& Camera)
{
	const FElysiumCameraWeights& Weights = Camera.GetWeights();
	const FRotator& Angles = Camera.SolvedBoomAngles();

	Sample.Frame = GFrameCounter;
	Sample.BoomLength = Camera.BoomLength();
	Sample.BoomPitch = static_cast<float>(Angles.Pitch);
	Sample.BoomYaw = static_cast<float>(Angles.Yaw);
	Sample.bClipped = Camera.IsBoomClipped();
	Sample.ThirdWeight = Weights.Third;
	Sample.ScriptedWeight = Weights.Scripted;
	Sample.Draw = Camera.GetDrawPolicy();

	// The damper's own reach, reported off the rig rather than off `ModernPosition - eye`: the pivot
	// is damped too, so that difference would carry the pivot's lag as well as the boom.
	Sample.DamperDistance = ModernDistance;
}

// showdebug camera

void AElysiumPlayerCameraManager::DisplayDebug(UCanvas* Canvas, const FDebugDisplayInfo& DebugDisplay,
	float& YL, float& YPos)
{
	if (!Canvas)
	{
		return;
	}

	FDisplayDebugManager& Display = Canvas->DisplayDebugManager;

	// The stock implementation dereferences the view target unguarded, so `showdebug camera` during
	// a map transition crashes instead of reporting that there is nothing to view.
	if (!ViewTarget.Target)
	{
		Display.SetDrawColor(FColor::Yellow);
		Display.DrawString(TEXT("   Elysium camera: no view target"));
		return;
	}

	Super::DisplayDebug(Canvas, DebugDisplay, YL, YPos);

	Display.SetDrawColor(FColor::White);
	if (const UElysiumCameraComponent* Camera = ResolveRig(ViewTarget.Target))
	{
		Display.DrawString(FString::Printf(TEXT("   Elysium: %s"), *Camera->Describe()));
	}
	else
	{
		Display.DrawString(TEXT("   Elysium: the view target is not a player body"));
	}
}
