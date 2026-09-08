#include "ElysiumCameraRig.h"

#include "ElysiumUserCmd.h"

// The damper

namespace
{
	// The decay factor for one step. Expressed as a half-life so the result is independent of how
	// the elapsed time was subdivided: two steps of `Dt` compose to exactly one step of `2 * Dt`,
	// which is what `Pow` gives and a `K * Dt` Euler step does not.
	float DecayFactor(float HalfLifeSeconds, float Dt)
	{
		if (HalfLifeSeconds <= 0.0f || Dt <= 0.0f)
		{
			return HalfLifeSeconds <= 0.0f ? 0.0f : 1.0f;
		}
		return FMath::Pow(0.5f, Dt / HalfLifeSeconds);
	}
}

FVector ElysiumRig::DampToward(const FVector& Current, const FVector& Target, float HalfLifeSeconds,
	float Dt)
{
	const float Retained = DecayFactor(HalfLifeSeconds, Dt);
	return Target + (Current - Target) * Retained;
}

FVector ElysiumRig::DampPivot(const FVector& Current, const FVector& Target, bool bClipped,
	const FElysiumCameraRigTuning& Tuning, float Dt)
{
	if (!Tuning.bDampOn)
	{
		return Target;   // `cdamp_on 0`, retail's own bypass
	}

	// `cdamp_springlength`: inside it the spring is at rest, so chasing further only produces a
	// permanent sub-millimetre crawl.
	const FVector Trail = Current - Target;
	const float Lag = static_cast<float>(Trail.Size());
	if (Lag <= Tuning.DamperDeadBand)
	{
		return Target;
	}

	// **`cdamp_maxdist` bounds how far the pivot may trail; it does not end the damping.** The
	// distinction is the whole feel of a run: a mover fast enough to outpace the spring rides the
	// bound and eases in from it the moment it slows, where releasing the pivot onto the body would
	// reset the lag to zero every time the bound was crossed — a sawtooth, since the spring's own
	// settling lag at any speed above `DamperMaxLag * ln2 / PositionHalfLifeFree` is past the bound
	// by construction. A body that jumped rather than moved is `bModernNeedsReseed`'s to snap, and
	// it bypasses this function entirely.
	const FVector From = (Lag > Tuning.DamperMaxLag)
		? Target + Trail * (Tuning.DamperMaxLag / Lag)
		: Current;

	// **Stiff against a wall, soft in open space.** The two recovered Hooke constants differ by
	// nearly four times, and that asymmetry — snap in, ease out — is what the VtMB camera feels like.
	const float HalfLife = bClipped ? Tuning.PositionHalfLifeWall : Tuning.PositionHalfLifeFree;
	return DampToward(From, Target, HalfLife, Dt);
}

float ElysiumRig::DampToward(float Current, float Target, float HalfLifeSeconds, float Dt)
{
	const float Retained = DecayFactor(HalfLifeSeconds, Dt);
	return Target + (Current - Target) * Retained;
}

// The boom

FRotator ElysiumRig::BoomRotation(const FRotator& ViewRot, const FElysiumCameraRigTuning& Tuning)
{
	// The camera sits at `Pivot - Forward * Distance`, so lifting it above the eye line means
	// pitching the boom *down*: `Forward.Z < 0` puts `-Forward.Z` above the pivot. Hence the
	// subtraction, and it is the reason `PitchOffset` is documented as "degrees above the eye line"
	// rather than as a rotation — the sign of the rotation is the opposite of the sign of the lift.
	// **Normalize before clamping.** A control rotation arrives in Unreal's canonical [0, 360)
	// — `APlayerCameraManager::LimitViewPitch` ends with `FRotator::ClampAxis` — so looking down is
	// 271..359, not -89..0. Clamping that raw pins the boom at `PitchMax` for every downward view
	// and releases it only when the angle wraps back past 360, which reads as the camera snapping to
	// maximum-up and then recentring. Yaw has always been normalized here for the same reason.
	FRotator BoomRot;
	BoomRot.Pitch = FMath::Clamp(FRotator::NormalizeAxis(ViewRot.Pitch) - Tuning.PitchOffset,
		Tuning.PitchMin, Tuning.PitchMax);
	// `cam_yaw` swings the whole boom around the pivot without touching where the player is looking,
	// which is what the patch's `cam_rotateleft` / `cam_rotateright` aliases drive.
	BoomRot.Yaw = FRotator::NormalizeAxis(ViewRot.Yaw + Tuning.YawOffset);
	// A banked view must not roll the boom: the strafe bank is a view effect and rotating the arm
	// by it would swing the character across the frame.
	BoomRot.Roll = 0.0f;
	return BoomRot;
}

FVector ElysiumRig::BoomTarget(const FVector& Pivot, const FRotator& BoomRot, float Distance,
	const FElysiumCameraRigTuning& Tuning)
{
	const FRotationMatrix Basis(BoomRot);
	const FVector Forward = Basis.GetScaledAxis(EAxis::X);
	const FVector Right = Basis.GetScaledAxis(EAxis::Y);
	const FVector Up = Basis.GetScaledAxis(EAxis::Z);

	// The shoulder offset is applied in the boom's own frame rather than in world space, so it
	// stays on the same side of the character as the player turns.
	return Pivot
		- Forward * (Distance + Tuning.ShoulderOffset.X)
		+ Right * Tuning.ShoulderOffset.Y
		+ Up * Tuning.ShoulderOffset.Z;
}

float ElysiumRig::SolveBoomDistance(float Current, float Desired, bool bHit, float HitDistance,
	const FElysiumCameraRigTuning& Tuning, float Dt)
{
	const float Floor = FMath::Min(Tuning.MinBoomLength, Desired);

	if (bHit)
	{
		// Retract immediately. Any easing here puts geometry inside the near plane for the duration
		// of the ease, which reads as the wall clipping through frame rather than as smoothing.
		const float Allowed = FMath::Max(Floor, HitDistance - Tuning.WallPullIn);
		return FMath::Min(Current, Allowed);
	}

	// Clear: grow back toward the rest length, rate-limited. This is the half that is *not*
	// symmetric with the retract, and it is why the camera does not pop out of a doorway on the
	// first frame the sweep misses.
	if (Tuning.ReturnSpeed <= 0.0f || Dt <= 0.0f)
	{
		return Desired;
	}
	return FMath::Clamp(Current + Tuning.ReturnSpeed * Dt, Floor, Desired);
}

bool ElysiumRig::OrbitInterceptsMouse(uint64 Buttons)
{
	constexpr uint64 MouseBits = static_cast<uint64>(EElysiumButton::CamMouseMove)
		| static_cast<uint64>(EElysiumButton::CamDistance);
	return (Buttons & MouseBits) != 0;
}

void ElysiumRig::StepOrbit(FElysiumOrbitState& InOut, uint64 Buttons, const FVector2D& LookDelta,
	float Dt, const FElysiumCameraRigTuning& Tuning)
{
	const auto Held = [Buttons](EElysiumButton Bit)
	{
		return (Buttons & static_cast<uint64>(Bit)) != 0;
	};

	const float AngleStep = Tuning.OrbitSpeed * FMath::Max(0.0f, Dt);
	const float DollyStep = Tuning.DollySpeed * FMath::Max(0.0f, Dt);

	// The moment an axis first becomes the player's, the cvar it was following is frozen at its
	// current value. Everything after that composes onto the frozen one, so a live retune no longer
	// drags an axis the player has taken over; `RestoreOrbit` is what hands it back.
	const auto Engage = [](bool& bHeld, float& Frozen, float Live)
	{
		if (!bHeld)
		{
			bHeld = true;
			Frozen = Live;
		}
	};

	// Yaw. `cam_rotateleft` / `cam_rotateright` are cfg aliases that step the cvar instead; these are
	// the held button pair, which is a different input and moves the player's own offset.
	float Yaw = InOut.YawOffset;
	if (Held(EElysiumButton::CamYawLeft))  { Yaw -= AngleStep; Engage(InOut.bYawHeld, InOut.HeldYawBase, Tuning.YawOffset); }
	if (Held(EElysiumButton::CamYawRight)) { Yaw += AngleStep; Engage(InOut.bYawHeld, InOut.HeldYawBase, Tuning.YawOffset); }

	float Pitch = InOut.PitchOffset;
	if (Held(EElysiumButton::CamPitchUp))   { Pitch -= AngleStep; Engage(InOut.bPitchHeld, InOut.HeldPitchBase, Tuning.PitchOffset); }
	if (Held(EElysiumButton::CamPitchDown)) { Pitch += AngleStep; Engage(InOut.bPitchHeld, InOut.HeldPitchBase, Tuning.PitchOffset); }

	float Dolly = InOut.DollyOffset;
	if (Held(EElysiumButton::CamIn))  { Dolly -= DollyStep; Engage(InOut.bDollyHeld, InOut.HeldBoomBase, Tuning.BoomLength); }
	if (Held(EElysiumButton::CamOut)) { Dolly += DollyStep; Engage(InOut.bDollyHeld, InOut.HeldBoomBase, Tuning.BoomLength); }

	// The two mouse-driven camera bits fold the frame's look delta into the orbit rather than into
	// the view, which is what `+campitchup`-style held orbit and `+camdistance` do in retail.
	if (Held(EElysiumButton::CamMouseMove))
	{
		Yaw += static_cast<float>(LookDelta.X);
		Pitch += static_cast<float>(LookDelta.Y);
		Engage(InOut.bYawHeld, InOut.HeldYawBase, Tuning.YawOffset);
		Engage(InOut.bPitchHeld, InOut.HeldPitchBase, Tuning.PitchOffset);
	}
	if (Held(EElysiumButton::CamDistance))
	{
		Dolly += static_cast<float>(LookDelta.Y);
		Engage(InOut.bDollyHeld, InOut.HeldBoomBase, Tuning.BoomLength);
	}

	// Clamped against the recovered orbit limits. The dolly clamp is expressed on the composed rest
	// length rather than on the offset, because `c_mindistance` bounds the distance, not the delta —
	// and it composes against the same base the frame's tuning will, so a held dolly is bounded by the
	// length it was frozen at rather than by one the cvar has since moved.
	const float DollyBase = InOut.bDollyHeld ? InOut.HeldBoomBase : Tuning.BoomLength;
	InOut.YawOffset = FMath::Clamp(Yaw, Tuning.OrbitYawMin, Tuning.OrbitYawMax);
	InOut.PitchOffset = FMath::Clamp(Pitch, Tuning.OrbitPitchMin, Tuning.OrbitPitchMax);
	InOut.DollyOffset = FMath::Clamp(DollyBase + Dolly, Tuning.DollyMin, Tuning.DollyMax) - DollyBase;
}

void ElysiumRig::RestoreOrbit(FElysiumOrbitState& InOut)
{
	InOut = FElysiumOrbitState();
}

ElysiumRig::FElysiumCameraRigTuning ElysiumRig::ComposeOrbit(const FElysiumCameraRigTuning& Base,
	const FElysiumOrbitState& Orbit)
{
	FElysiumCameraRigTuning Out = Base;
	Out.YawOffset = (Orbit.bYawHeld ? Orbit.HeldYawBase : Base.YawOffset) + Orbit.YawOffset;
	Out.PitchOffset = (Orbit.bPitchHeld ? Orbit.HeldPitchBase : Base.PitchOffset) - Orbit.PitchOffset;
	Out.BoomLength = (Orbit.bDollyHeld ? Orbit.HeldBoomBase : Base.BoomLength) + Orbit.DollyOffset;
	return Out;
}

ElysiumRig::FElysiumCameraRigTuning ElysiumRig::ResolveTuning(
	const FElysiumCameraRigTuning& Project, const FElysiumCameraCvars& Cvars)
{
	// `FElysiumCameraCvars` is already in centimetres — `LoadFrom` is the single conversion point, and
	// `ElysiumCam::U` deliberately does not appear anywhere in this file so a second multiply cannot
	// be written by accident.
	FElysiumCameraRigTuning Out = Project;

	Out.BoomLength = Cvars.IdealDist;
	Out.DollyMin = Cvars.MinDistance;
	Out.DollyMax = Cvars.MaxDistance;
	Out.PitchOffset = Cvars.TargetAngle;
	Out.YawOffset = Cvars.Yaw;
	Out.OrbitPitchMin = Cvars.MinPitch;
	Out.OrbitPitchMax = Cvars.MaxPitch;
	Out.OrbitYawMin = Cvars.MinYaw;
	Out.OrbitYawMax = Cvars.MaxYaw;
	Out.bCollide = Cvars.bCollide;
	Out.ProbeRadius = Cvars.TraceRadius;
	Out.bDampOn = Cvars.bDampOn;
	Out.DamperDeadBand = Cvars.SpringLength;
	Out.DamperMaxLag = Cvars.DampMaxDist;

	// A Hooke rate expressed as the half-life that decays at the same speed. `x' = -Kx` halves in
	// `ln 2 / K` seconds, so the recovered stiffness survives while the integrator becomes exact at
	// any subdivision of a step — which is the one part of the damper that is deliberately not
	// retail's.
	auto HalfLifeFor = [](float HookeConstant, float Fallback)
	{
		return HookeConstant > 0.0f ? (UE_LN2 / HookeConstant) : Fallback;
	};
	Out.PositionHalfLifeFree = HalfLifeFor(Cvars.HookesConstant, Project.PositionHalfLifeFree);
	Out.PositionHalfLifeWall = HalfLifeFor(Cvars.HookesConstantWall, Project.PositionHalfLifeWall);

	return Out;
}
