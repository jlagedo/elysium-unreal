#include "ElysiumPlayerCameraManager.h"

#include "ElysiumCameraComponent.h"
#include "ElysiumCameraModifiers.h"
#include "ElysiumPlayerBody.h"

#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

// The A/B. **Default 0**: the faithful evaluator stays the shipped feel until `CCC3`'s co-tune
// resolves the modern rig's deltas one owner call at a time. Both rigs evaluate and record their
// channels every frame regardless — this picks only which one supplies the base request, so one
// deterministic run diffs the two booms directly rather than against a recollection.
static TAutoConsoleVariable<int32> CVarModernCamera(
	TEXT("elysium.ModernCamera"), 0,
	TEXT("Which rig supplies the third-person base view: the faithful VtMB evaluator (0) or the ")
	TEXT("remaster boom (1). Both solve and record every frame either way."),
	ECVF_Default);

AElysiumPlayerCameraManager::AElysiumPlayerCameraManager()
{
	// `DefaultModifiers` is instantiated per manager in `PostInitializeComponents`, so appending the
	// class here is the whole registration. The stock camera-shake entry the base constructor added
	// is left in place — `CachedCameraShakeMod` is only bound for a `UCameraModifier_CameraShake`,
	// and removing it would quietly take every shake with it.
	DefaultModifiers.Add(UElysiumCameraModifier_LegacyShot::StaticClass());
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
	const FVector Pivot = Camera.GetComponentLocation();
	const FRotator ViewRot = PCOwner ? PCOwner->GetControlRotation() : Camera.GetComponentRotation();

	ModernAngles = ElysiumRig::BoomRotation(ViewRot, RigTuning);

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

	const FVector Target = ElysiumRig::BoomTarget(Pivot, ModernAngles, ModernDistance, RigTuning);
	ModernPosition = bModernNeedsReseed
		? Target
		: ElysiumRig::DampToward(ModernPosition, Target, RigTuning.PositionHalfLife, Dt);

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

	const FVector ModernOffset = ModernPosition - Camera.GetComponentLocation();
	Sample.ModernBoomLength = static_cast<float>(ModernOffset.Size()) * Weights.ThirdBlend();
	Sample.ModernDamperDistance = static_cast<float>(ModernOffset.Size());
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
