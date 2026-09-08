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
//
// **`Bone: cam_bone` is never sampled.** `SetShotAnchorEntity` `FUN_1006ef50` resolves the block's
// `AttachPos` ONCE at factory time into the camera's `+0x620[0]`, and the only reader of `+0x620` in
// the image is `FUN_1006f080`, the **mode-1** anchor sampler. `CamMode 4`'s own arm `FUN_1006f870`
// reads the anchor's liveness and publishes only `m_flFOV`, which the client's mode-4 arm
// (`client.dll FUN_10002200`, an empty `RET`) then discards. A `camera_animated` shot renders frozen
// at the pose and FOV it had at shot start — a retail defect, already recorded by RG-A as
// "`CamMode 4`'s per-tick FOV publish is dead" in `docs/vtmb/retail-defects.md` §7. The bone lookup
// is kept (the body is stood so the anchor CAN resolve it) and nothing follows it.

class FElysiumCameraAnimated : public FElysiumAnimating
{
public:
	// `+0x7f4` `m_sAnimName`, key `animname` — the sequence played on this entity's own model.
	FString AnimName;

	// `+0x7f0` — the `CamMode 4` camera this entity created, or unset. Retail holds an EHANDLE and
	// removes the entity **directly** at `EndCamera` rather than going through
	// `SetCineCamera(player, NULL)`.
	FElysiumEntityHandle CineCamera;

	// The sequence's deadline, and the port's producer of `m_bSequenceFinished` (`+0x65c`).
	//
	// Retail's think tests **that byte and nothing else** (RC15.3 §3.3): it never reads `m_flCycle`,
	// never compares against 1.0, never calls `IsSequenceFinished()`. `StudioFrameAdvance` is the
	// byte's sole producer and `ResetSequenceInfo` its sole reset, and `StudioFrameAdvance` raises it
	// on the wrap **regardless of `m_bSequenceLoops`** — so a `STUDIO_LOOPING` sequence ends the
	// camera on its first wrap. The port's bodies do not publish a per-tick wrap edge, so the edge is
	// computed instead: the clip's own authored length at `ResetSequenceInfo`'s guaranteed
	// `m_flPlaybackRate = 1.0` **is** the first wrap, for a one-shot and a looping clip alike. Unset
	// (`< 0`) is "no sequence is running", which is also what the strand below leaves behind.
	double SequenceEndTime = -1.0;

	// `+0x65d` `m_bSequenceLoops`, written by `ResetSequenceInfo` from `GetSequenceFlags(seq) & 1`
	// (`STUDIO_LOOPING`). Recovered, carried, and deliberately **not** consulted by the exit: see
	// `SequenceEndTime`. Held so the debug row can show that a looping clip still ends the shot.
	bool bSequenceLoops = false;

	// The `animated_props` stem this entity's `model` resolved to, empty when the body came up as a
	// character or as a static prop. It is what decides which clip vocabulary `animname` is looked
	// up in — the port's stand-in for "`LookupSequence` on the entity's OWN model".
	FString AnimatedStem;

	// The static body the third route stands when the model bakes no skeleton. Registered with the
	// world for teardown exactly as a prop's is; it plays nothing, which is the state retail's
	// `seq < 0` arm leaves the entity in anyway.
	class UStaticMeshComponent* StaticBody = nullptr;

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

	// Retail's `m_bSequenceFinished` read, expressed against the clock the deadline was armed on.
	// False whenever no sequence is running, so the stranded entity never claims to have finished.
	bool IsSequenceFinished(double Now) const
	{
		return SequenceEndTime >= 0.0 && Now >= SequenceEndTime;
	}

private:
	// `CCameraAnimated::vfunc103`'s `SetModel`, built the way `FElysiumLockableEntity::Spawn` builds
	// a body: animated prop, then character, then static. Retail has one model loader and one
	// `LookupSequence` over it; the port's bodies come from three different manifests, so the model
	// picks its own route and `AnimatedStem` records which one answered.
	void BuildCameraVisual();

	// `FUN_10071770` — look the sequence up, play it, fire `OnCameraBegin` and arm the 0.1 s think.
	void PlayCameraAnimation();

	// The `LookupSequence` half of `FUN_10071770`, on whichever vocabulary `BuildCameraVisual` bound.
	// False is retail's `seq < 0`. `OutLoops` is `GetSequenceFlags(seq) & STUDIO_LOOPING`.
	bool PlayCameraSequence(float& OutSeconds, bool& bOutLoops);
};
