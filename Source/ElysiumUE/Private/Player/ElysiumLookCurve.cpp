#include "ElysiumLookCurve.h"

namespace ElysiumInput
{

// Three recovered names and four of ours. They are **declared into the VtMB console store**, not
// registered as `elysium.*` engine cvars, so a user's `config.cfg` keeps governing the three that
// VtMB itself had and `elysium.cmd sensitivity 5` is the same write the game would make.
//
// `cl_yawspeed`, `cl_pitchspeed`, `cl_pitchup` and `cl_pitchdown` are deliberately absent: nothing
// reads them from the store — they are compiled-in constants at the point they apply — and
// declaring a cvar the code never reads is a lie the console would tell.
static const FCvarDef GLookCvars[] =
{
	{ TEXT("sensitivity"),    TEXT("3"),     TEXT("Mouse sensitivity, multiplies m_yaw and m_pitch.") },
	{ TEXT("m_yaw"),          TEXT("0.022"), TEXT("Degrees of yaw per mouse count.") },
	{ TEXT("m_pitch"),        TEXT("0.022"), TEXT("Degrees of pitch per mouse count; negative inverts.") },
	{ TEXT("look_curve"),     TEXT("0"),     TEXT("Elysium: mouse acceleration gain. 0 is VtMB's linear path.") },
	{ TEXT("look_exponent"),  TEXT("1"),     TEXT("Elysium: how the acceleration gain ramps with hand speed.") },
	{ TEXT("look_threshold"), TEXT("300"),   TEXT("Elysium: hand speed the gain ramp normalizes against, deg/s.") },
	{ TEXT("look_maxscale"),  TEXT("4"),     TEXT("Elysium: ceiling on the acceleration gain.") },

	// The pad. Every one of these is ours — VtMB has no gamepad feel to reproduce — and they are
	// declared into the same store so `elysium.cmd joy_yawsensitivity 220` tunes a live run and a
	// `config.cfg` keeps the answer. The `joy_` prefix is Source's family name for the group, not a
	// claim that any of these is a recovered value.
	{ TEXT("joy_deadzone"),        TEXT("0.12"),  TEXT("Elysium: stick deflection that reads as centred.") },
	{ TEXT("joy_saturation"),      TEXT("0.92"),  TEXT("Elysium: stick deflection that reads as fully pushed.") },
	{ TEXT("joy_response_look"),   TEXT("2"),     TEXT("Elysium: look response curve exponent. 1 is linear.") },
	{ TEXT("joy_yawsensitivity"),  TEXT("190"),   TEXT("Elysium: degrees of yaw per second at full deflection.") },
	{ TEXT("joy_pitchsensitivity"),TEXT("130"),   TEXT("Elysium: degrees of pitch per second at full deflection.") },
	{ TEXT("joy_smoothing"),       TEXT("0.035"), TEXT("Elysium: look filter half-life, seconds. 0 is unfiltered.") },
	{ TEXT("joy_accelscale"),      TEXT("1"),     TEXT("Elysium: ceiling on the sustained-turn ramp. 1 is no ramp.") },
	{ TEXT("joy_acceltime"),       TEXT("0.4"),   TEXT("Elysium: seconds of held turn to reach joy_accelscale.") },
	{ TEXT("joy_accelenter"),      TEXT("0.9"),   TEXT("Elysium: shaped deflection at which the ramp charges.") },
	{ TEXT("joy_move_deadzone"),   TEXT("0.14"),  TEXT("Elysium: move stick deflection that reads as centred.") },
	{ TEXT("joy_move_saturation"), TEXT("0.92"),  TEXT("Elysium: move stick deflection that reads as fully pushed.") },
};

TArrayView<const FCvarDef> CvarDefs()
{
	return MakeArrayView(GLookCvars);
}

void FElysiumLookTuning::LoadFrom(TFunctionRef<FString(const TCHAR*)> Lookup)
{
	// An empty read keeps the default, so a run with no `config.cfg` on disk behaves like a stock
	// install. Nothing converts: every value here is already in the unit it applies in.
	auto Read = [&Lookup](const TCHAR* Name, float& Out)
	{
		const FString Value = Lookup(Name);
		if (!Value.IsEmpty())
		{
			Out = FCString::Atof(*Value);
		}
	};

	Read(TEXT("sensitivity"),    Sensitivity);
	Read(TEXT("m_yaw"),          MouseYaw);
	Read(TEXT("m_pitch"),        MousePitch);
	Read(TEXT("look_curve"),     Curve);
	Read(TEXT("look_exponent"),  Exponent);
	Read(TEXT("look_threshold"), Threshold);
	Read(TEXT("look_maxscale"),  MaxScale);
}

FVector2D ShapeMouseLook(const FVector2D& MouseDegrees, const FElysiumLookTuning& Tuning,
	float DeltaSeconds)
{
	// The retail path, and the overwhelmingly common one: no work, and no floating-point round trip
	// that could move a delta by an ulp. `Curve` is compared against zero exactly because that is the
	// value the default is written as and the value a cvar read of "0" produces.
	if (Tuning.IsRetailLinear())
	{
		return MouseDegrees;
	}

	// A frame with no time, a tuning with no ramp, or a delta that cannot produce a speed leaves the
	// input alone rather than dividing by it. `IsNearlyZero` also covers the frame where the mouse
	// did not move, which is most of them.
	if (DeltaSeconds <= 0.0f || Tuning.Threshold <= 0.0f
		|| MouseDegrees.IsNearlyZero() || MouseDegrees.ContainsNaN())
	{
		return MouseDegrees;
	}

	const double Magnitude = MouseDegrees.Size();
	const double Speed = Magnitude / static_cast<double>(DeltaSeconds);
	const double Ramp = FMath::Pow(Speed / static_cast<double>(Tuning.Threshold),
		static_cast<double>(Tuning.Exponent));
	const double Gain = FMath::Min(1.0 + static_cast<double>(Tuning.Curve) * Ramp,
		static_cast<double>(Tuning.MaxScale));

	return MouseDegrees * Gain;
}

// =====================================================================================
// The stick path
// =====================================================================================

void FElysiumStickTuning::LoadFrom(TFunctionRef<FString(const TCHAR*)> Lookup)
{
	auto Read = [&Lookup](const TCHAR* Name, float& Out)
	{
		const FString Value = Lookup(Name);
		if (!Value.IsEmpty())
		{
			Out = FCString::Atof(*Value);
		}
	};

	Read(TEXT("joy_deadzone"),         DeadZone);
	Read(TEXT("joy_saturation"),       Saturation);
	Read(TEXT("joy_response_look"),    Exponent);
	Read(TEXT("joy_yawsensitivity"),   YawRate);
	Read(TEXT("joy_pitchsensitivity"), PitchRate);
	Read(TEXT("joy_smoothing"),        SmoothHalfLife);
	Read(TEXT("joy_accelscale"),       AccelScale);
	Read(TEXT("joy_acceltime"),        AccelTime);
	Read(TEXT("joy_accelenter"),       AccelEnter);
	Read(TEXT("joy_move_deadzone"),    MoveDeadZone);
	Read(TEXT("joy_move_saturation"),  MoveSaturation);
}

namespace
{
	// The scaled radial band, as one number: 0 inside the dead zone, 1 at or past saturation, and a
	// continuous ramp between. Returning the *magnitude* rather than the vector is what keeps the
	// shaping radial — the direction is restored by the caller from the unshaped deflection, so a
	// diagonal keeps its angle exactly.
	float RadialBand(float Magnitude, float DeadZone, float Saturation)
	{
		// A band with no width would divide by zero. Treating it as a switch at the dead zone is the
		// honest reading of "everything past here is full deflection", and it keeps a hand-typed
		// `joy_saturation 0.12` from producing NaNs on the view.
		const float Lower = FMath::Clamp(DeadZone, 0.0f, 0.999f);
		const float Upper = FMath::Max(Saturation, Lower + UE_KINDA_SMALL_NUMBER);
		return FMath::Clamp((Magnitude - Lower) / (Upper - Lower), 0.0f, 1.0f);
	}

	// Exponential decay toward a target expressed as a half-life, so the filter settles the same
	// amount per second of real time regardless of how many frames that took. The same rule as
	// `ElysiumRig::DampToward`, duplicated rather than shared because this header may not depend on
	// the camera rig's — a look filter and a boom damper are not one value with two callers.
	FVector2D DampToward(const FVector2D& Current, const FVector2D& Target, float HalfLife, float Dt)
	{
		if (HalfLife <= 0.0f || Dt <= 0.0f)
		{
			return Target;
		}
		const float Alpha = 1.0f - FMath::Pow(2.0f, -Dt / HalfLife);
		return Current + (Target - Current) * Alpha;
	}
}

FVector2D ShapeStickLook(const FVector2D& Deflection, const FElysiumStickTuning& Tuning,
	float DeltaSeconds, FElysiumStickState& State)
{
	if (Deflection.ContainsNaN())
	{
		State.Reset();
		return FVector2D::ZeroVector;
	}

	// The magnitude can exceed 1: the measured pad reports 1.0095 on a hard diagonal, because the
	// axes saturate independently and the corner of the square is outside the circle. Clamping it
	// here is what makes "full deflection" one value rather than a direction-dependent one.
	const double Magnitude = FMath::Min(Deflection.Size(), 1.0);
	const float Band = RadialBand(static_cast<float>(Magnitude), Tuning.DeadZone, Tuning.Saturation);
	const float Curved = Band > 0.0f
		? FMath::Pow(Band, FMath::Max(Tuning.Exponent, UE_KINDA_SMALL_NUMBER))
		: 0.0f;

	// Direction from the *unshaped* deflection, so the curve changes how fast the view turns and
	// never which way. A magnitude at or below zero has no direction to restore.
	const FVector2D Direction = Magnitude > UE_DOUBLE_SMALL_NUMBER
		? FVector2D(Deflection / Magnitude)
		: FVector2D::ZeroVector;

	// A centred stick is a **decision, not a sample**, so it is taken rather than filtered toward:
	// the dead zone has already said this is no input, and letting the filter approach zero over its
	// half-life would coast the view `YawRate * HalfLife / ln2` degrees past where the thumb let go —
	// about ten at the shipped tuning, which reads as the view sliding out from under a stop.
	//
	// Snapping costs nothing precisely because the band is scaled: `Curved` reaches zero
	// continuously at the dead zone rather than stepping off a threshold, so the value being
	// discarded here is always infinitesimal. And it stays symmetric for every deflection that is
	// not zero, which an attack/release split would not — a fast-fall, slow-rise filter biases a
	// steady hold's mean downward, turning noise into a turn that is quietly slower than the stick.
	const FVector2D Target = Direction * Curved;
	State.Shaped = Target.IsNearlyZero()
		? FVector2D::ZeroVector
		: DampToward(State.Shaped, Target, Tuning.SmoothHalfLife, DeltaSeconds);

	// The ramp charges on the *unfiltered* band, so releasing the stick discharges it immediately
	// rather than waiting out the filter's tail — an accelerated turn that keeps accelerating after
	// the thumb has left is the failure this ordering avoids.
	const float Dt = FMath::Max(DeltaSeconds, 0.0f);
	State.AccelSeconds = FMath::Clamp(
		State.AccelSeconds + (Band >= Tuning.AccelEnter ? Dt : -Dt), 0.0f, FMath::Max(Tuning.AccelTime, 0.0f));
	const float Charge = Tuning.AccelTime > 0.0f ? State.AccelSeconds / Tuning.AccelTime : 0.0f;
	const float Boost = FMath::Lerp(1.0f, FMath::Max(Tuning.AccelScale, 1.0f), Charge);

	return FVector2D(State.Shaped.X * Tuning.YawRate, State.Shaped.Y * Tuning.PitchRate) * Boost;
}

FVector2D ShapeStickMove(const FVector2D& Deflection, const FElysiumStickTuning& Tuning)
{
	if (Deflection.ContainsNaN())
	{
		return FVector2D::ZeroVector;
	}
	const double Magnitude = FMath::Min(Deflection.Size(), 1.0);
	if (Magnitude <= UE_DOUBLE_SMALL_NUMBER)
	{
		return FVector2D::ZeroVector;
	}
	const float Band = RadialBand(static_cast<float>(Magnitude), Tuning.MoveDeadZone, Tuning.MoveSaturation);
	return FVector2D(Deflection / Magnitude) * Band;
}

} // namespace ElysiumInput
