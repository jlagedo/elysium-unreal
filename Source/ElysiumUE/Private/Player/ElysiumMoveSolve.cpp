#include "ElysiumMoveSolve.h"

namespace ElysiumMove
{

void ApplyFriction(FVector& Velocity, float FrictionCoeff, float InStopSpeed,
	float SurfaceFriction, float Dt)
{
	// The 3D speed, not the horizontal one — the decompile scales all three components.
	const float Speed = Velocity.Size();
	if (Speed < 0.1f)
	{
		return;
	}
	const float Control = FMath::Max(Speed, InStopSpeed);
	const float Drop = Control * FrictionCoeff * SurfaceFriction * Dt;
	Velocity *= FMath::Max(0.0f, Speed - Drop) / Speed;
}

void ApplyAccelerate(FVector& Velocity, const FVector& WishDir, float WishSpeed,
	float AccelCoeff, float SurfaceFriction, float Dt)
{
	const float AddSpeed = WishSpeed - FVector::DotProduct(Velocity, WishDir);
	if (AddSpeed <= 0.0f)
	{
		return;
	}
	const float AccelSpeed = FMath::Min(AccelCoeff * Dt * WishSpeed * SurfaceFriction, AddSpeed);
	Velocity += AccelSpeed * WishDir;
}

void ApplyAirAccelerate(FVector& Velocity, const FVector& WishDir, float WishSpeed,
	float AirAccelCoeff, float InAirSpeedCap, float SurfaceFriction, float Dt)
{
	// The cap applies to the *target* while the uncapped wishspeed still drives accelspeed. That
	// asymmetry is the whole of Source's air-strafing behaviour, so it is reproduced exactly.
	const float Capped = FMath::Min(WishSpeed, InAirSpeedCap);
	const float AddSpeed = Capped - FVector::DotProduct(Velocity, WishDir);
	if (AddSpeed <= 0.0f)
	{
		return;
	}
	const float AccelSpeed = FMath::Min(AirAccelCoeff * WishSpeed * Dt * SurfaceFriction, AddSpeed);
	Velocity += AccelSpeed * WishDir;
}

void ApplyWaterFriction(FVector& Velocity, float FrictionCoeff, float SurfaceFriction, float Dt)
{
	// No stopspeed floor here — water drops straight out of the speed and zeroes below the epsilon.
	const float Speed = Velocity.Size();
	if (Speed == 0.0f)
	{
		return;
	}
	float NewSpeed = Speed - Dt * SurfaceFriction * FrictionCoeff * Speed;
	if (NewSpeed < WaterStopSpeed)
	{
		NewSpeed = 0.0f;
	}
	Velocity *= NewSpeed / Speed;
}

int32 ClipVelocity(const FVector& In, const FVector& Normal, FVector& Out, float Overbounce)
{
	int32 Blocked = 0;
	if (Normal.Z > 0.0f)
	{
		Blocked |= 1;   // floor
	}
	if (Normal.Z == 0.0f)
	{
		Blocked |= 2;   // step / wall
	}

	const float Backoff = FVector::DotProduct(In, Normal) * Overbounce;

	Out = In;
	// Source runs the re-projection *inside* the per-axis loop, so it executes three times. After
	// the first correction `Adjust` is already ~0, which is why the repeat is invisible — but this
	// is the faithful baseline, so it is written the way the decompile runs it rather than folded.
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		Out[Axis] = In[Axis] - Normal[Axis] * Backoff;

		const float Adjust = FVector::DotProduct(Out, Normal);
		if (Adjust < 0.0f)
		{
			Out -= Normal * Adjust;
		}
	}
	return Blocked;
}

void StartGravity(FVector& Velocity, float GravityAccel, float Dt)
{
	Velocity.Z -= GravityAccel * 0.5f * Dt;
}

void FinishGravity(FVector& Velocity, float GravityAccel, float Dt)
{
	Velocity.Z -= GravityAccel * 0.5f * Dt;
}

void CheckVelocity(FVector& Velocity, float MaxVel)
{
	// Per **component**, not per magnitude. The two differ exactly at terminal velocity.
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (!FMath::IsFinite(Velocity[Axis]))
		{
			Velocity[Axis] = 0.0f;
			continue;
		}
		Velocity[Axis] = FMath::Clamp(Velocity[Axis], -MaxVel, MaxVel);
	}
}

float JumpVelocity(float JumpBoostUnits, float GravityAccel)
{
	// v = sqrt(2 * boost * g). The boost is in Source units and the gravity in cm/s^2, so the
	// boost converts here — which is what makes `sv_jump_boost 25` mean "25 units of apex".
	return FMath::Sqrt(2.0f * (JumpBoostUnits * U) * GravityAccel);
}

float ApexHeight(float LaunchZ, float GravityAccel)
{
	if (GravityAccel <= 0.0f)
	{
		return 0.0f;
	}
	// In Source units, so it reads against `sv_jump_boost` directly.
	return (LaunchZ * LaunchZ) / (2.0f * GravityAccel) / U;
}

FVector WishDirection(const FVector2D& Move, float Up, const FRotator& Frame,
	bool bIncludePitch, float& OutScale)
{
	const FRotator Basis = bIncludePitch ? Frame : FRotator(0.0f, Frame.Yaw, 0.0f);
	const FRotationMatrix M(Basis);

	FVector Wish = M.GetUnitAxis(EAxis::X) * Move.X + M.GetUnitAxis(EAxis::Y) * Move.Y;
	if (bIncludePitch)
	{
		Wish += FVector::UpVector * Up;
	}

	// Source scales wishspeed by the length of the input and clamps it to maxspeed; a keyboard
	// diagonal is length sqrt(2), which is why it normalizes rather than summing.
	OutScale = FMath::Min(static_cast<float>(Wish.Size()), 1.0f);
	return Wish.GetSafeNormal();
}

// --------------------------------------------------------------------------------------------

static const FCvarDef GMoveCvars[] =
{
	{ TEXT("sv_gravity"),        TEXT("800"),  TEXT("World gravity, units/s^2.") },
	{ TEXT("sv_friction"),       TEXT("4"),    TEXT("Ground friction coefficient.") },
	{ TEXT("sv_stopspeed"),      TEXT("16"),   TEXT("Friction's control-speed floor, units/s.") },
	{ TEXT("sv_accelerate"),     TEXT("10"),   TEXT("Ground acceleration coefficient.") },
	{ TEXT("sv_airaccelerate"),  TEXT("10"),   TEXT("Air acceleration coefficient.") },
	{ TEXT("sv_stepsize"),       TEXT("18"),   TEXT("Maximum step height, units.") },
	{ TEXT("sv_maxvelocity"),    TEXT("3500"), TEXT("Per-component velocity clamp, units/s.") },
	{ TEXT("sv_jump_boost"),     TEXT("25"),   TEXT("Jump apex height, units.") },
	{ TEXT("sv_jump_maxspeed"),  TEXT("350"),  TEXT("Max speed while airborne, units/s.") },
};

TArrayView<const FCvarDef> CvarDefs()
{
	return MakeArrayView(GMoveCvars);
}

} // namespace ElysiumMove

int32 FElysiumMoveStepper::BeginFrame(float DeltaSeconds, float& OutStepSeconds)
{
	if (DeltaSeconds <= 0.0f)
	{
		OutStepSeconds = 0.0f;
		return 0;
	}

	// The faithful path: one step, at exactly the delta the command carries.
	if (!IsFixed())
	{
		Accumulator = 0.0f;
		OutStepSeconds = DeltaSeconds;
		return 1;
	}

	OutStepSeconds = FixedStep;
	Accumulator += DeltaSeconds;

	int32 Steps = FMath::FloorToInt32(Accumulator / FixedStep);
	if (Steps > MaxSubSteps)
	{
		// Drop the excess rather than carrying it: a carried backlog would spend the next several
		// frames replaying stale input, which reads far worse than losing the time outright.
		Steps = MaxSubSteps;
		Accumulator = 0.0f;
		return Steps;
	}

	// Keep the remainder — it is what makes a 60 Hz stream of 16.7 ms frames land on a 100 Hz step
	// without drifting.
	Accumulator -= Steps * FixedStep;
	return Steps;
}

float FElysiumMoveStepper::Alpha() const
{
	return IsFixed() ? FMath::Clamp(Accumulator / FixedStep, 0.0f, 1.0f) : 0.0f;
}

void FElysiumMoveTuning::LoadFrom(TFunctionRef<FString(const TCHAR*)> Lookup)
{
	// An empty read keeps the default, so a run with no `out/cfg` on disk behaves like a stock
	// install. Every distance converts from Source units to cm here, once.
	auto Read = [&Lookup](const TCHAR* Name, float& Out, float Scale)
	{
		const FString Value = Lookup(Name);
		if (!Value.IsEmpty())
		{
			Out = FCString::Atof(*Value) * Scale;
		}
	};

	Read(TEXT("sv_gravity"),       Gravity,      ElysiumMove::U);
	Read(TEXT("sv_friction"),      Friction,     1.0f);
	Read(TEXT("sv_stopspeed"),     StopSpeed,    ElysiumMove::U);
	Read(TEXT("sv_accelerate"),    Accelerate,   1.0f);
	Read(TEXT("sv_airaccelerate"), AirAccel,     1.0f);
	Read(TEXT("sv_stepsize"),      StepSize,     ElysiumMove::U);
	Read(TEXT("sv_maxvelocity"),   MaxVelocity,  ElysiumMove::U);
	Read(TEXT("sv_jump_maxspeed"), JumpMaxSpeed, ElysiumMove::U);

	// The apex stays in Source units — it is a height, and `JumpSpeed()` converts it.
	Read(TEXT("sv_jump_boost"),    JumpBoost,    1.0f);
}
