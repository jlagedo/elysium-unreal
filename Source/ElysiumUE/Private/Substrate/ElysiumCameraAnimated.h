#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"
#include "ElysiumPlayer.h"   // FElysiumAnimating — the animating base the class stands on

// `camera_animated` — VtMB's `CCameraAnimated` (`vampire.dll`, registered by `FUN_10070a10`).
//
// **A second entity class, not a flag on `camera_cinematic`** (RC12). It is a `CBaseAnimating` that
// plays a named sequence on its own model and drives a `CamMode 4` cine camera bolted to the model's
// `cam_bone` for as long as that sequence runs. `special-case.txt` authors the shot it uses —
// `Animated`, `Start { Position Named; AttachPos Bone: cam_bone; AttachType Follow }`, with Troika's
// own comment *"special case shot info used by camera_animated entity. Don't change this."* — so the
// arm is content-facing even though **no shipped map places one** (0 instances across every
// exported `.ents`).
//
// Two clocks: this entity thinks at 0.1 s (`FUN_10071840`) while the cine camera it created thinks
// at 1/24 s. The shot ends when the sequence finishes, which fires `OnCameraComplete`.

class FElysiumCameraAnimated : public FElysiumAnimating
{
public:
	// `+0x7f4` `m_sAnimName`, key `animname` — the sequence played on this entity's own model.
	FString AnimName;

	// `+0x7f0` — the `CamMode 4` camera this entity created, or unset. Retail holds an EHANDLE and
	// removes the entity **directly** at `EndCamera` rather than going through
	// `SetCineCamera(player, NULL)`.
	FElysiumEntityHandle CineCamera;

	// The sequence's deadline. Retail reads `m_bSequenceFinished` off `StudioFrameAdvance`; the port
	// gets the clip's authored length back from `PlayAnimClip` and ends on it, which is the same
	// edge measured from the other side.
	double SequenceEndTime = -1.0;

	// `+0x828` — the cached `camera_showdebug` state the think re-reads every tick. It has no reader
	// in any opened body; carried because the think writes it.
	bool bCachedShowDebug = false;

	// `CCameraAnimated::vfunc103` `0x10071330` — `Spawn`: a model is **required**. With none it
	// warns (`"%s at %.0f %.0f %0.f missing modelname\n"`, itself defective: three conversions, two
	// arguments pushed) and removes itself.
	virtual void Spawn() override;
	virtual void Think() override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

	// `InputStartCamera` `0x10071440` -> `FUN_10071550`.
	void InputStartCamera();
	// `InputEndCamera` `0x10071470` -> `FUN_10071660`.
	void InputEndCamera();

private:
	// `FUN_10071770` — look the sequence up, play it, fire `OnCameraBegin` and arm the 0.1 s think.
	void PlayCameraAnimation();
};
