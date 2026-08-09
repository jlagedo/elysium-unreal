#include "ElysiumPlayerCameraManager.h"

#include "ElysiumCameraComponent.h"
#include "ElysiumCameraModifiers.h"
#include "ElysiumLookCurve.h"
#include "ElysiumPlayerBody.h"

#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

// The A/B. **Default 1**: the modern rig supplies the shipped base view, and `0` reverts to the
// faithful evaluator. Both rigs evaluate and record their channels every frame regardless — this
// picks only which one supplies the base request, so one deterministic run diffs the two booms
// directly rather than against a recollection, and `CCC3`'s co-tune still resolves the rig's
// remaining deltas one owner call at a time.
static TAutoConsoleVariable<int32> CVarModernCamera(
	TEXT("elysium.ModernCamera"), 1,
	TEXT("Which rig supplies the third-person base view: the faithful VtMB evaluator (0) or the ")
	TEXT("remaster boom (1). Both solve and record every frame either way."),
	ECVF_Default);

// The modern boom's translation damper, as a live override on `FElysiumCameraRigTuning`. Negative
// takes the tuning's own value, which keeps the shipped number in one place; zero makes the rig
// fully rigid, which is the direct read on how much of a given motion the damper owns.
static TAutoConsoleVariable<float> CVarModernPositionHalfLife(
	TEXT("elysium.cam.PositionHalfLife"), -1.0f,
	TEXT("Half-life in seconds of the modern boom's pivot damper. Negative uses the rig tuning's ")
	TEXT("own value; 0 snaps."),
	ECVF_Default);

AElysiumPlayerCameraManager::AElysiumPlayerCameraManager()
{
	// `DefaultModifiers` is instantiated per manager in `PostInitializeComponents`, so appending the
	// class here is the whole registration. The stock camera-shake entry the base constructor added
	// is left in place — `CachedCameraShakeMod` is only bound for a `UCameraModifier_CameraShake`,
	// and removing it would quietly take every shake with it.
	DefaultModifiers.Add(UElysiumCameraModifier_LegacyShot::StaticClass());

	// VtMB's pitch clamp (`cl_pitchup` / `cl_pitchdown`, both 89). It lives here because
	// `UpdateRotation` now owns the integration and runs these limits through
	// `ProcessViewRotation`; the engine's own default is ±89.9, which would quietly widen the clamp
	// the router used to apply by hand.
	ViewPitchMin = -ElysiumInput::PitchClampDegrees;
	ViewPitchMax = ElysiumInput::PitchClampDegrees;
}

// =====================================================================================
// The view
// =====================================================================================

UElysiumCameraComponent* AElysiumPlayerCameraManager::ResolveRig(AActor* Target)
{
	// Resolved per frame from whatever is currently being viewed. Both movement bodies answer the
	// same interface, which is what keeps the `elysium.SourceMovement` A/B comparing the movers
	// rather than two camera paths.
	if (IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(Target))
	{
		return Body->GetCameraComponent();
	}
	return nullptr;
}

void AElysiumPlayerCameraManager::UpdateViewTargetInternal(FTViewTarget& OutVT, float DeltaTime)
{
	UElysiumCameraComponent* Camera = ResolveRig(OutVT.Target);

	// `SolveFrameFor` answers false without touching the view when there is no active rig, so the
	// stock dispatch below still gets an untouched POV — which is what keeps a spectator, a scene
	// capture and `UGameplayStatics::CalculateViewProjectionMatricesFromViewTarget` correct.
	if (!Camera || !UElysiumCameraComponent::SolveFrameFor(Camera, DeltaTime, OutVT.POV))
	{
		Super::UpdateViewTargetInternal(OutVT, DeltaTime);
		return;
	}

	// **Both rigs solve, every frame.** Only one of them supplies the base, but a channel recording
	// that carried whichever rig happened to be selected could not diff them against each other.
	SolveModernRig(*Camera, DeltaTime);

	// The **base** only. The scripted channel and the temporal cut are the legacy post layer's,
	// because both have to happen after the base request has been chosen — a shot composes over
	// whichever rig won, and a history reset must describe the view that was actually rendered.
	if (CVarModernCamera.GetValueOnGameThread() != 0)
	{
		ApplyModernBaseToView(*Camera, OutVT.POV);
	}
	else
	{
		Camera->ApplyBaseToView(OutVT.POV);
	}

	// The body-visibility ramp travels with the view. It used to ride on the pawn's own
	// `CalcCamera`, which the manager no longer goes through on the production path, and a player
	// mesh that never fades reports nothing — no log line, no failed check.
	if (IElysiumPlayerBody* Body = Cast<IElysiumPlayerBody>(OutVT.Target))
	{
		Body->ApplyPlayerModelAlpha(Camera->ModelAlpha());
	}

	PublishSample(*Camera);
}

// =====================================================================================
// The modern rig (CCC2 stage two)
// =====================================================================================

void AElysiumPlayerCameraManager::SolveModernRig(const UElysiumCameraComponent& Camera,
	float DeltaSeconds)
{
	// Once per frame, for the same reason the faithful solve is guarded: `ApplyCameraModifiers` and
	// a second view-target pass would otherwise integrate the damper twice at the same delta.
	if (ModernSolvedFrame == GFrameCounter)
	{
		return;
	}
	ModernSolvedFrame = GFrameCounter;

	const UWorld* World = GetWorld();
	// A held world holds the camera, exactly as the faithful rig does.
	const float Dt = (World && World->IsPaused()) ? 0.0f : FMath::Max(0.0f, DeltaSeconds);

	// The eye the boom hangs off, and the view it derives from. Both are read the same way the
	// faithful rig reads them, so the two booms answer the same question and the diff between them
	// is the rig rather than the input.
	const FVector BodyPivot = Camera.GetComponentLocation();
	const FRotator ViewRot = PCOwner ? PCOwner->GetControlRotation() : Camera.GetComponentRotation();

	ModernAngles = ElysiumRig::BoomRotation(ViewRot, RigTuning);

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
	const float HalfLife = CVarModernPositionHalfLife.GetValueOnGameThread() >= 0.0f
		? CVarModernPositionHalfLife.GetValueOnGameThread()
		: RigTuning.PositionHalfLife;
	ModernPivot = bModernNeedsReseed
		? BodyPivot
		: ElysiumRig::DampToward(ModernPivot, BodyPivot, HalfLife, Dt);

	// Everything downstream — the collision sweep included — hangs off the damped pivot, so the
	// probe traces from where the camera actually is rather than from where the body got to.
	const FVector Pivot = ModernPivot;

	// The sweep. A sphere, like the faithful rig's, so the two clip on the same geometry — the
	// difference between them is the response, not the probe.
	const float Desired = RigTuning.BoomLength;
	const FVector Reach = ElysiumRig::BoomTarget(Pivot, ModernAngles, Desired, RigTuning);
	bModernClipped = false;
	float HitDistance = Desired;
	if (World)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ElysiumModernBoom), /*bTraceComplex*/ false,
			Camera.GetOwner());
		Params.AddIgnoredActor(Camera.GetOwner());
		FHitResult Hit;
		if (World->SweepSingleByChannel(Hit, Pivot, Reach, FQuat::Identity, ECC_Camera,
				FCollisionShape::MakeSphere(RigTuning.ProbeRadius), Params))
		{
			bModernClipped = true;
			HitDistance = static_cast<float>(FVector::Dist(Pivot, Hit.Location));
		}
	}

	ModernDistance = bModernNeedsReseed
		? (bModernClipped ? FMath::Max(RigTuning.MinBoomLength, HitDistance - RigTuning.WallPullIn)
						  : Desired)
		: ElysiumRig::SolveBoomDistance(ModernDistance, Desired, bModernClipped, HitDistance,
			RigTuning, Dt);

	// Rigid: the pivot and the length are already damped, so a second damper here would be the one
	// that puts rotation back into the lag.
	ModernPosition = ElysiumRig::BoomTarget(Pivot, ModernAngles, ModernDistance, RigTuning);

	// A first-person frame parks the rig on its target rather than letting it drift, so re-entering
	// third person does not swing in from wherever the camera was left — the same rule the faithful
	// re-seed flag encodes.
	bModernNeedsReseed = Camera.GetWeights().Third <= 0.0f;
}

void AElysiumPlayerCameraManager::ApplyModernBaseToView(const UElysiumCameraComponent& Camera,
	FMinimalViewInfo& View) const
{
	// The same composition shape the faithful base uses: an offset and an angle lerp, both scaled
	// by the third-person weight, so weight 0 is exactly the first-person view and there is no
	// second camera anywhere. Keeping the shape identical is what makes the two channel families
	// comparable row for row.
	const float E = Camera.GetWeights().ThirdBlend();
	if (E > 0.0f)
	{
		View.Location += (ModernPosition - Camera.GetComponentLocation()) * E;
		View.Rotation = FMath::Lerp(View.Rotation, ModernAngles, E);
	}
}

void AElysiumPlayerCameraManager::PublishSample(const UElysiumCameraComponent& Camera)
{
	const FElysiumCameraWeights& Weights = Camera.GetWeights();
	const FRotator& Angles = Camera.SolvedBoomAngles();

	Sample.Frame = GFrameCounter;
	Sample.BoomLength = Camera.BoomLength();
	Sample.DamperDistance = static_cast<float>(Camera.SolvedBoomOffset().Size());
	Sample.BoomPitch = static_cast<float>(Angles.Pitch);
	Sample.BoomYaw = static_cast<float>(Angles.Yaw);
	Sample.bClipped = Camera.IsBoomClipped();
	Sample.ThirdWeight = Weights.Third;
	Sample.ScriptedWeight = Weights.Scripted;
	Sample.ModelAlpha = Camera.ModelAlpha();

	// Reported off the solved boom rather than off `ModernPosition - eye`. The two agreed while the
	// camera hung rigidly off the live eye; now that the pivot is damped, that difference carries
	// the pivot's lag as well as the boom, and a channel named for the boom would be reporting both.
	Sample.ModernBoomLength = ModernDistance * Weights.ThirdBlend();
	Sample.ModernDamperDistance = ModernDistance;
	Sample.ModernBoomPitch = static_cast<float>(ModernAngles.Pitch);
	Sample.ModernBoomYaw = static_cast<float>(ModernAngles.Yaw);
	Sample.bModernClipped = bModernClipped;
}

// =====================================================================================
// showdebug camera
// =====================================================================================

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
