#pragma once

#include "Camera/CameraModifier.h"
#include "CoreMinimal.h"

#include "ElysiumCameraModifiers.generated.h"

class UElysiumCameraComponent;

// The post-composition layers.
//
// A base request supplies the viewpoint; a **post layer** is composed over whichever base won.
// They are `UCameraModifier` subclasses rather than a hand-rolled stack because the base class
// already carries ordered priority, alpha in/out, per-modifier enable/disable and a
// `showdebug camera` row — all of which would otherwise be written again, worse.
//
// Two engine facts shape this base class, and both are silent when got wrong:
//
//   * `UCameraModifier::ModifyCamera` does **not** scale by `Alpha`, despite the header comment
//     saying so. Each layer applies its own, and must call `Super` or `Alpha` never advances at all;
//   * `ApplyCameraModifiers` runs **once per view target, not once per frame**. During a view-target
//     blend that is twice in one frame with the same delta, so an unguarded layer blends at double
//     rate. The guard below passes the repeat pass a zero delta rather than skipping it, because the
//     second view target's POV still has to be layered.

UCLASS(Abstract)
class UElysiumCameraModifier : public UCameraModifier
{
	GENERATED_BODY()

public:
	virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;

protected:
	// One layer's work. `DeltaTime` is zero on a repeat pass within the same frame. Returning true
	// stops the rest of the chain, which is the engine's own suppression mechanism.
	virtual bool ApplyElysiumLayer(float DeltaTime, FMinimalViewInfo& InOutPOV)
	{
		return false;
	}

	// The player body's camera behind the manager's current view target, or null.
	UElysiumCameraComponent* ResolveRig() const;

private:
	// Seeded to a frame that cannot be the current one, so the first pass is not the one the guard
	// eats — the same shape `UElysiumCameraComponent`'s two frame phases use.
	uint64 LastAppliedFrame = TNumericLimits<uint64>::Max();
};

// The legacy scripted channel — `SetCamera`, `camera_track`/`camera_keyframe`, and feed cameras —
// composed over the base rig. Dialogue is a separately owned base request in
// `UElysiumCameraService`; it never enters this post-layer stack.
//
// **It is a post layer, not a base request, and that is the faithful arrangement.** Retail's
// `CAM_ApplyToView` adds the boom offset and then blends the scripted pose on top of it, and
// `CAM_IsThirdPerson` is a disjunction that counts a scripted camera *as* third person. Making it
// a competing base request would have the manager's own transition machinery ease an authored
// timeline a second time, which is exactly what a cutscene edit must not have happen.
//
// So the stack stays the single timeline: this layer's `Alpha` is **written** from the shot stack's
// own timed weight each frame with no blend of its own (`AlphaInTime`/`AlphaOutTime` are zero), and
// a zero-duration shot therefore stays a cut rather than becoming a very fast blend.
UCLASS()
class UElysiumCameraModifier_LegacyShot : public UElysiumCameraModifier
{
	GENERATED_BODY()

public:
	UElysiumCameraModifier_LegacyShot();

protected:
	virtual bool ApplyElysiumLayer(float DeltaTime, FMinimalViewInfo& InOutPOV) override;
};
