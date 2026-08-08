#pragma once

#include "CoreMinimal.h"

// The **modern** third-person rig's rule set (`docs/architecture/camera-architecture.md`).
//
// This is the remaster half of the camera A/B and it is deliberately a separate file from
// `ElysiumCameraSolve.h`, which holds the *recovered* VtMB rules. Nothing here reproduces a
// decompiled function, and nothing here reads the VtMB console store: the cvar surface tunes the
// faithful evaluator, this rig carries its own tuning, and that partition is what stops a single
// value having two owners while both rigs are live under `elysium.ModernCamera`.
//
// Same split as the faithful half: constants in, values out, no UObject, no world and no trace, so
// the whole rig is asserted with no pawn (`Elysium.Substrate.CameraRig`). The engine half owns the
// sweep and hands the hit back in.
//
// Three things it does differently from the recovered rig, and each is a stated Feel divergence
// (`docs/architecture/camera-architecture.md` -> the divergence table):
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
	// The modern rig's whole tuning surface. Values are in cm and degrees, held in **engine units**
	// rather than Source units — this rig has no `config.cfg` to stay compatible with, so there is
	// no conversion to get wrong. `UElysiumCameraProfile` will carry these as a data asset when the
	// options surface that consumes it lands (`docs/project/roadmap.md` 11.13d); until then the
	// defaults below are the profile.
	struct FElysiumCameraRigTuning
	{
		// The boom at rest, and the floor a collision retract is allowed to reach.
		float BoomLength = 220.0f;
		float MinBoomLength = 40.0f;

		// Degrees above the eye line. Positive raises the camera, which is the opposite sign to
		// Source's down-positive pitch — this rig is in Unreal's convention throughout.
		float PitchOffset = 10.0f;

		// Where the body sits in frame, in boom space: +Y right, +Z up. The X term is a further
		// push back along the boom and is normally left at zero.
		FVector ShoulderOffset = FVector(0.0f, 40.0f, 10.0f);

		// The collision probe. A sphere, like the faithful rig's, so the two are comparable.
		float ProbeRadius = 18.0f;
		// How far inside the contact point the camera settles, so the near plane never sits flush.
		float WallPullIn = 12.0f;
		// How fast the boom grows back after a clip clears. 0 restores instantly.
		float ReturnSpeed = 260.0f;

		// The damper's half-life: the time in which the camera closes half the remaining distance
		// to its target. 0 snaps. Smaller is tighter.
		float PositionHalfLife = 0.07f;

		// The pitch the boom is allowed to reach, in Unreal's sign convention.
		float PitchMin = -75.0f;
		float PitchMax = 75.0f;
	};

	// Exponential decay toward a target, expressed as a half-life so it is **exact at any step**:
	// after `HalfLife` seconds the remaining error is halved regardless of how many frames it took
	// to get there. This is the one line that makes the modern damper frame-rate independent where
	// the recovered `Clamp(K * Dt, 0, 1)` is not. A half-life at or below zero snaps.
	FVector DampToward(const FVector& Current, const FVector& Target, float HalfLifeSeconds, float Dt);

	// The same decay on a scalar, for the boom length.
	float DampToward(float Current, float Target, float HalfLifeSeconds, float Dt);

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
