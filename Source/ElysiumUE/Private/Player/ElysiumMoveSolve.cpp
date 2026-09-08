#include "ElysiumMoveSolve.h"

DEFINE_LOG_CATEGORY(LogElysiumMovement);

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

float ApexHeight(float LaunchZ, float GravityAccel)
{
	if (GravityAccel <= 0.0f)
	{
		return 0.0f;
	}
	// Returned in **Source units** so it reads as a height on the map's own scale.
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

bool ClampCommandSpeed(FVector& CommandCmS, float MaxSpeed)
{
	const double Speed = CommandCmS.Size();
	// An unordered compare is false, so a non-finite command falls out here rather than being scaled
	// by a non-finite ratio.
	if (!(Speed > static_cast<double>(MaxSpeed)))
	{
		return false;
	}
	// Retail scales the three components by one ratio rather than rebuilding the vector from a
	// normalized direction, so the commanded bearing survives the clamp as authored instead of as a
	// re-derivation of itself.
	CommandCmS *= MaxSpeed > 0.0f ? static_cast<double>(MaxSpeed) / Speed : 0.0;
	return true;
}

bool EyeAnglesAdoptCommand(bool bPendingEyeAngleSnap, bool bViewAngleLock)
{
	return !bPendingEyeAngleSnap && !bViewAngleLock;
}

bool BodyYawFollowsEye(const FSetupMoveBodyState& State)
{
	// The tail arm, transcribed:
	//
	//   if (bGrappling) { switch (m_IdealActivity) { the nine: goto ANGLES; default: glue; goto ANGLES; } }
	//   else if (GetVFlags() & 1) { ANGLES: m_vecAngles = GetAngles(); }
	//
	// Every path through a live grapple reaches ANGLES_FROM_ENTITY, so the partner alone suppresses
	// the substitution; the flag is only reachable when there is no partner.
	if (State.bGrapplePartnerLive)
	{
		return false;
	}
	return !State.bMoveAnglesFromEntity;
}

bool GrappleGluesBody(const FSetupMoveBodyState& State)
{
	return State.bGrapplePartnerLive && !State.bGrappleReleaseActivity;
}

FVector GluedBodyFeetOrigin(const FVector& PartnerFeetOrigin, float PlayerCollisionMinZ,
	float PartnerCollisionMinZ)
{
	FVector Out = PartnerFeetOrigin;
	Out.Z -= static_cast<double>(PlayerCollisionMinZ) - static_cast<double>(PartnerCollisionMinZ);
	return Out;
}


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

bool ResolveClipPlanes(TArrayView<const FVector> Planes, const FVector& Original,
	const FVector& Primal, FVector& OutVelocity)
{
	if (Planes.Num() == 0)
	{
		OutVelocity = Original;
		return true;
	}

	// First: is there a single plane whose projection already satisfies all the others? That is the
	// ordinary case, and with one plane it is the only case.
	int32 Accepted = INDEX_NONE;
	for (int32 I = 0; I < Planes.Num(); ++I)
	{
		FVector Candidate;
		ClipVelocity(Original, Planes[I], Candidate);
		bool bIntoAnother = false;
		for (int32 J = 0; J < Planes.Num(); ++J)
		{
			if (J != I && (Candidate | Planes[J]) < 0.0f)
			{
				bIntoAnother = true;
				break;
			}
		}
		if (!bIntoAnother)
		{
			OutVelocity = Candidate;
			Accepted = I;
			break;
		}
	}

	if (Accepted == INDEX_NONE)
	{
		// No projection clears every plane, so the only direction left is the crease the two share.
		// Three planes with no common projection is a wedge, and Source stops dead in one rather than
		// picking an edge that does not exist.
		if (Planes.Num() != 2)
		{
			OutVelocity = FVector::ZeroVector;
			return false;
		}
		const FVector Crease = (Planes[0] ^ Planes[1]).GetSafeNormal();
		OutVelocity = Crease * (Crease | Original);
	}

	// A resolution that points back down the move the body asked for is a wedge answered the long
	// way round: sliding it would walk the body backwards out of the corner it walked into.
	if ((OutVelocity | Primal) <= 0.0f)
	{
		OutVelocity = FVector::ZeroVector;
		return false;
	}
	return true;
}

} // namespace ElysiumMove

void FElysiumMoveTuning::LoadFrom(TFunctionRef<FString(const TCHAR*)> Lookup)
{
	// An empty read keeps the default, so a run with no `out/cfg` on disk behaves like a stock
	// install. Every distance converts from Source units to cm here, once.
	//
	// **A value that is present and unparsable keeps the default too, and says so.** `Atof` answers
	// 0 for anything it cannot read, and this surface is re-read every frame -- so `sv_maxvelocity
	// nonsense` would otherwise clamp every velocity component to zero and freeze the body for the
	// rest of the session with nothing logged. Said once per name because the store does not change
	// between frames and a line here would repeat at the frame rate.
	auto Read = [this, &Lookup](const TCHAR* Name, float& Out, float Scale)
	{
		const FString Value = Lookup(Name);
		if (Value.IsEmpty())
		{
			return;
		}
		float Parsed = 0.0f;
		if (!LexTryParseString(Parsed, *Value) || !FMath::IsFinite(Parsed))
		{
			const FName Key(Name);
			if (!ReportedMalformed.Contains(Key))
			{
				ReportedMalformed.Add(Key);
				UE_LOG(LogElysiumMovement, Warning,
					TEXT("[elysium] %s is set to '%s', which is not a finite number; keeping %g"),
					Name, *Value, Out / Scale);
			}
			return;
		}
		ReportedMalformed.Remove(FName(Name));
		Out = Parsed * Scale;
	};

	Read(TEXT("sv_gravity"),       Gravity,      ElysiumMove::U);
	Read(TEXT("sv_friction"),      Friction,     1.0f);
	Read(TEXT("sv_stopspeed"),     StopSpeed,    ElysiumMove::U);
	Read(TEXT("sv_accelerate"),    Accelerate,   1.0f);
	Read(TEXT("sv_airaccelerate"), AirAccel,     1.0f);
	Read(TEXT("sv_stepsize"),      StepSize,     ElysiumMove::U);
	Read(TEXT("sv_maxvelocity"),   MaxVelocity,  ElysiumMove::U);
	Read(TEXT("sv_jump_maxspeed"), JumpMaxSpeed, ElysiumMove::U);

	// The pop stays in Source units — it is a distance in inches, converted where it is applied.
	Read(TEXT("sv_jump_boost"),    JumpBoost,    1.0f);
}

void FElysiumMoveTuning::LoadJumpFrom(TFunctionRef<bool(const TCHAR*, float&)> Lookup)
{
	float V = 0.0f;
	if (Lookup(TEXT("BaseJumpVelocity"), V))      { BaseJumpVelocity = V * ElysiumMove::U; }
	if (Lookup(TEXT("JumpGravityMultiplier"), V)) { JumpGravityMultiplier = V; }
	if (Lookup(TEXT("JumpHoldTime"), V))          { JumpHoldSeconds = V; }
}

bool FElysiumMoveTuning::SetJumpRule(const TCHAR* Key, float Value)
{
	// The same three keys `LoadJumpFrom` reads, converted the same way, so an override and a
	// rulebook read cannot disagree about what a key means.
	if (FCString::Stricmp(Key, TEXT("BaseJumpVelocity")) == 0)
	{
		BaseJumpVelocity = Value * ElysiumMove::U;
		return true;
	}
	if (FCString::Stricmp(Key, TEXT("JumpGravityMultiplier")) == 0)
	{
		JumpGravityMultiplier = Value;
		return true;
	}
	if (FCString::Stricmp(Key, TEXT("JumpHoldTime")) == 0)
	{
		JumpHoldSeconds = Value;
		return true;
	}
	return false;
}
