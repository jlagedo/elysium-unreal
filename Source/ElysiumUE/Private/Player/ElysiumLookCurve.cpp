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

} // namespace ElysiumInput
