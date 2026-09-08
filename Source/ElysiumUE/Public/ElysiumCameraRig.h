#pragma once

#include "CoreMinimal.h"
#include "ElysiumCameraSolve.h"

// The third-person rig's rule set.
//
// It is a separate file from `ElysiumCameraSolve.h`, which holds the recovered VtMB *rules* — the
// weights, the latches, the draw policy, the fade band, the cvar surface. Nothing here reproduces a
// decompiled function: this is the boom, and it is the project's own.
//
// It does, however, read the recovered cvars. Every axis the VtMB console store names is composed
// onto the tuning by `ResolveTuning`, so `cam_idealdist`, `cam_targetangle`, `cam_collide`,
// `cam_trace_radius`, the `c_min*`/`c_max*` clamps and the `cdamp_*` group all keep governing the
// one boom and a user's `config.cfg` still tunes it. What stops a value having two owners is that
// the partition is **by name**: the store owns what it names, this struct owns the rest.
//
// Same split as the faithful half: constants in, values out, no UObject, no world and no trace, so
// the whole rig is asserted with no pawn (`Elysium.Substrate.CameraRig`). The engine half owns the
// sweep and hands the hit back in.
//
// Three things it does differently from the recovered rig, and each is a stated Feel divergence:
//
//   * **the damper is frame-rate independent.** VtMB's is an Euler step scaled by `K * Dt` and
//     clamped, so the same motion settles differently at 60 and 144 Hz. This one decays by a
//     half-life, which is exact at any step — the property `uv run elysium debug move --hz`
//     measures directly;
//   * **collision is asymmetric.** The recovered rig forces a re-seed on contact, so the boom snaps
//     both in *and* out. Retracting instantly is right — a wall must never be inside the near plane
//     — but growing back is rate-limited here, which is what stops the camera popping out of a
//     doorway the moment the sweep clears;
//   * **the body is off-centre.** A shoulder offset in boom space frames the character to one side,
//     the ordinary third-person convention. The recovered rig has no such term.

namespace ElysiumRig
{
	// The rig's whole tuning surface, in cm and degrees.
	//
	// **Every axis the VtMB console store names is owned by the store**, composed onto this struct
	// once per frame by `ResolveTuning`, so a user's `config.cfg` and the Unofficial Patch's aliases
	// keep governing the boom. The defaults below are retail's own values, so a store with nothing in
	// it behaves exactly like a stock install. The remaining fields — the collision floor, the
	// recovery rate and the shoulder offset — are the ones the store does not name, and they are the
	// project's, and they live here as code defaults rather than in the console store.
	struct FElysiumCameraRigTuning
	{
		// The boom at rest, owned by `cam_idealdist` (85 u).
		float BoomLength = 215.9f;
		// The floor a collision retract may reach. **Project-owned, and a different quantity from
		// `c_mindistance`**: that clamps how far the player may dolly the boom, this is how far a wall
		// may push it.
		float MinBoomLength = 40.0f;

		// How far the player's own dolly may drive the rest length, owned by
		// `c_mindistance` / `c_maxdistance`.
		float DollyMin = 76.2f;
		float DollyMax = 508.0f;

		// Degrees above the eye line, owned by `cam_targetangle` (15). Positive raises the camera,
		// which is the opposite sign to Source's down-positive pitch — this rig is in Unreal's
		// convention throughout, and `BoomRotation` subtracts it for that reason.
		float PitchOffset = 15.0f;

		// A constant yaw around the pivot, owned by `cam_yaw`. The patch's `cam_rotateleft` /
		// `cam_rotateright` aliases step it in 15-degree bites.
		float YawOffset = 0.0f;

		// The clamps the player's hand-orbit is held inside, in **Source's** sign convention (pitch
		// positive = down), owned by `c_minpitch`/`c_maxpitch` and `c_minyaw`/`c_maxyaw`.
		float OrbitPitchMin = 0.0f;
		float OrbitPitchMax = 90.0f;
		float OrbitYawMin = -135.0f;
		float OrbitYawMax = 135.0f;

		// Retail's own two bypasses: `cam_collide 0` skips the sweep entirely, `cdamp_on 0` makes the
		// pivot rigid.
		bool bCollide = true;
		bool bDampOn = true;

		// Where the body sits in frame, in boom space: +Y right, +Z up. The X term is a further
		// push back along the boom and is normally left at zero. Project-owned — a recorded
		// divergence, since the recovered rig has no such term.
		FVector ShoulderOffset = FVector(0.0f, 40.0f, 10.0f);

		// The collision probe, owned by `cam_trace_radius` (9 u). A sphere where retail sweeps a box;
		// that substitution is a recorded divergence (`docs/vtmb/camera-view-modes.md` §4).
		float ProbeRadius = 22.86f;
		// How far inside the contact point the camera settles, so the near plane never sits flush.
		// Retail holds this as a **code constant** of 7 u with no ConVar behind it, so it stays here.
		float WallPullIn = 17.78f;
		// How fast the boom grows back after a clip clears. 0 restores instantly. Project-owned: the
		// asymmetry is the recorded divergence.
		float ReturnSpeed = 260.0f;

		// **The two-constant damper.** Retail is stiffer against a wall than in open space — it snaps
		// in and eases out, and that asymmetry is the most characteristic part of the VtMB camera
		// (`docs/vtmb/camera-view-modes.md` §4). The recovered constants are Hooke rates; `ResolveTuning`
		// converts each to the half-life that decays at the same rate, `ln 2 / K`, so the integrator is
		// frame-rate independent while the *feel* is the recovered one. Defaults are `cdamp_hookesconstant`
		// 4.0 and `cdamp_hookesconstantwall` 15.0.
		float PositionHalfLifeFree = 0.1733f;
		float PositionHalfLifeWall = 0.0462f;
		// Below this the pivot is considered arrived, owned by `cdamp_springlength` (0.1 u).
		float DamperDeadBand = 0.254f;
		// The furthest the pivot may trail the body, owned by `cdamp_maxdist` (50 u). It bounds the
		// lag rather than ending the damping: at the bound the pivot keeps pace and still eases in
		// once the body slows.
		float DamperMaxLag = 127.0f;

		// The pitch the boom is allowed to reach, in Unreal's sign convention. Project-owned: this is
		// the composed boom clamp, not the orbit clamp above.
		float PitchMin = -75.0f;
		float PitchMax = 75.0f;

		// How fast the orbit and dolly button pairs move their targets, and how fast the solved values
		// chase them. **No ConVar holds any of these** — `CAM_Think` polls the `kbutton_t`s and steps
		// the values directly, and that step is not recovered — so they are ours and live here rather
		// than in a cvar struct that would imply a retail name.
		float OrbitSpeed = 90.0f;        // deg/s
		float DollySpeed = 254.0f;       // cm/s
	};

	// The project's tuning with every axis the VtMB console store names overwritten from it.
	//
	// `FElysiumCameraCvars` has already converted Source units to centimetres exactly once, at
	// `LoadFrom`, so **nothing here multiplies by 2.54 a second time** — that is the whole of the unit
	// contract, and it is why this takes the converted struct rather than the raw store.
	FElysiumCameraRigTuning ResolveTuning(const FElysiumCameraRigTuning& Project,
		const FElysiumCameraCvars& Cvars);

	// Exponential decay toward a target, expressed as a half-life so it is **exact at any step**:
	// after `HalfLife` seconds the remaining error is halved regardless of how many frames it took
	// to get there. This is the one line that makes the modern damper frame-rate independent where
	// the recovered `Clamp(K * Dt, 0, 1)` is not. A half-life at or below zero snaps.
	FVector DampToward(const FVector& Current, const FVector& Target, float HalfLifeSeconds, float Dt);

	// The same decay on a scalar, for the boom length.
	float DampToward(float Current, float Target, float HalfLifeSeconds, float Dt);

	// The player's hand-orbit (`CAM_Think` step 2, `0x100fc170`).
	//
	// **An axis the player has not touched follows its cvar live.** That is what makes retuning
	// `cam_idealdist` or `cam_targetangle` move the camera at once, and it is why each axis carries a
	// held flag rather than being seeded from the cvar and drifting away from it
	// (`docs/vtmb/camera-view-modes.md` §7).
	struct FElysiumOrbitState
	{
		float YawOffset = 0.0f;      // degrees, added to `cam_yaw`
		float PitchOffset = 0.0f;    // degrees, Source's down-positive convention
		float DollyOffset = 0.0f;    // cm, added to `cam_idealdist`
		bool bYawHeld = false;
		bool bPitchHeld = false;
		bool bDollyHeld = false;

		// **What the cvar was worth when the hold engaged.** A held axis composes onto this frozen
		// value instead of the live one, which is what "stops following the cvar" means in practice:
		// the player's orbit stays where they put it and a later `cam_yaw` retune does not drag it.
		// Meaningless while the matching flag is false, and `RestoreOrbit` clears both together.
		float HeldYawBase = 0.0f;
		float HeldPitchBase = 0.0f;
		float HeldBoomBase = 0.0f;
	};

	// **Whether this frame's mouse belongs to the camera rather than to the view.**
	//
	// `+cammousemove` and `+camdistance` are the two bits that redirect the mouse into the orbit, and
	// redirect is the whole of it: while either is held the delta must not also reach the control
	// rotation, or one movement of the mouse both swings the boom and turns the player. Source draws
	// the same line with `CAM_InterceptingMouse` — `CInput::MouseMove` hands the delta to the camera
	// and never reaches the view angles that frame — and it intercepts **both** axes, including the
	// one `+camdistance` does not itself consume.
	//
	// The rule lives here rather than in the controller because `StepOrbit` is the consumer: one owner
	// decides who the delta belongs to, and the input path asks.
	bool OrbitInterceptsMouse(uint64 Buttons);

	// Poll one frame of the orbit/dolly button pairs and step the targets, clamped against
	// `c_minyaw`/`c_maxyaw`, `c_minpitch`/`c_maxpitch` and `c_mindistance`/`c_maxdistance`.
	// `Buttons` is the frame's `FElysiumUserCmd::Buttons`; `LookDelta` is its mouse delta, which the
	// two mouse-driven camera bits fold in instead of into the view.
	//
	// The step **rates** are ours: no ConVar holds them and the recovered `kbutton_t` step is not
	// decoded, so they live on the tuning rather than pretending to a retail name.
	void StepOrbit(FElysiumOrbitState& InOut, uint64 Buttons, const FVector2D& LookDelta, float Dt,
		const FElysiumCameraRigTuning& Tuning);

	// `snapto` / `cam_restore`: return every hand-orbited axis to its cvar and release the holds.
	void RestoreOrbit(FElysiumOrbitState& InOut);

	// **Where the held flags are spent.** The frame's tuning, per axis: an untouched axis takes the
	// live cvar-owned value, a held one takes the value that cvar had when the hold engaged plus the
	// player's own offset. Yaw and the dolly add; pitch subtracts, because the orbit is in Source's
	// down-positive convention and `PitchOffset` is degrees above the eye line.
	//
	// Composing unconditionally is what made the flags dead: `cam_idealdist` would keep moving a boom
	// the player had already dollied by hand.
	FElysiumCameraRigTuning ComposeOrbit(const FElysiumCameraRigTuning& Base,
		const FElysiumOrbitState& Orbit);

	// The pivot damper, with the recovered spring's two extra terms around it: inside `DeadBand` the
	// pivot has arrived and snaps, and it may never trail further than `MaxLag` — which bounds the
	// lag without interrupting the ease, so a body faster than the spring rides the bound instead of
	// being caught up to. `bClipped` selects the wall half-life over the free one — stiff in, soft out.
	FVector DampPivot(const FVector& Current, const FVector& Target, bool bClipped,
		const FElysiumCameraRigTuning& Tuning, float Dt);

	// The boom's look direction: the player's view, pitched up by `PitchOffset` and clamped. Roll is
	// never taken from the view — a banked view must not rotate the boom.
	FRotator BoomRotation(const FRotator& ViewRot, const FElysiumCameraRigTuning& Tuning);

	// Where the camera wants to be: back along the boom by `Distance`, then the shoulder offset
	// applied in the boom's own frame so it stays put as the player turns.
	FVector BoomTarget(const FVector& Pivot, const FRotator& BoomRot, float Distance,
		const FElysiumCameraRigTuning& Tuning);

	// The asymmetric collision response. `bHit`/`HitDistance` come from the engine half's sweep.
	// Contact retracts immediately to the hit less `WallPullIn`; clearance grows back at
	// `ReturnSpeed`, never past `Desired`. The result is clamped to `MinBoomLength`.
	float SolveBoomDistance(float Current, float Desired, bool bHit, float HitDistance,
		const FElysiumCameraRigTuning& Tuning, float Dt);
}
