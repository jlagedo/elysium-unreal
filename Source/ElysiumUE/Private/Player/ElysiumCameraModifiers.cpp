#include "ElysiumCameraModifiers.h"

#include "ElysiumCameraComponent.h"
#include "ElysiumPlayerCameraManager.h"

// The base layer

bool UElysiumCameraModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	// `ApplyCameraModifiers` runs once per view target. During a view-target blend that is twice in
	// the same frame with the same delta, so the repeat pass is given a zero delta: the second
	// target's POV still needs the layer, but nothing stateful may advance twice.
	const bool bFirstPassThisFrame = (LastAppliedFrame != GFrameCounter);
	LastAppliedFrame = GFrameCounter;
	const float StepSeconds = bFirstPassThisFrame ? DeltaTime : 0.0f;

	// Super advances `Alpha` toward its target and resolves a pending disable. Skipping it is how a
	// layer ends up permanently at whatever alpha it was constructed with.
	Super::ModifyCamera(StepSeconds, InOutPOV);

	return ApplyElysiumLayer(StepSeconds, InOutPOV);
}

UElysiumCameraComponent* UElysiumCameraModifier::ResolveRig() const
{
	if (const APlayerCameraManager* Manager = CameraOwner)
	{
		return AElysiumPlayerCameraManager::ResolveRig(Manager->ViewTarget.Target);
	}
	return nullptr;
}

// The legacy scripted channel

UElysiumCameraModifier_LegacyShot::UElysiumCameraModifier_LegacyShot()
{
	// 0 is the highest priority, so this runs before every other layer — which is what lets an
	// authored shot suppress the rest of the chain below.
	Priority = 0;

	// No blend of its own. The shot stack's timed ramp is the one timeline; easing here would be the
	// second easing over an authored duration that a cutscene must not get.
	AlphaInTime = 0.0f;
	AlphaOutTime = 0.0f;
}

bool UElysiumCameraModifier_LegacyShot::ApplyElysiumLayer(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	UElysiumCameraComponent* Camera = ResolveRig();
	if (!Camera)
	{
		return false;
	}

	const FElysiumScriptedShotView Shot = Camera->ScriptedShotView();

	// The stack's weight *is* this layer's alpha. Written, never blended — so `showdebug camera`
	// reports the real ramp rather than a copy of it drifting alongside.
	Alpha = Shot.Weight;

	Camera->ApplyScriptedShotToView(InOutPOV);

	// The cut is published here rather than with the base, because a temporal history reset has to
	// describe the view that was actually rendered — which is this one, after the shot landed.
	if (Camera->ConsumeTemporalCameraCutRequest())
	{
		InOutPOV.PreviousViewTransform = FTransform(InOutPOV.Rotation, InOutPOV.Location);
		if (CameraOwner)
		{
			CameraOwner->SetGameCameraCutThisFrame();
		}
	}

	// While an authored shot has weight, nothing further composes over it: an edit is exact, and the
	// stock camera-shake modifier sitting at the default priority is the layer this suppresses.
	return Shot.Weight > 0.0f;
}
