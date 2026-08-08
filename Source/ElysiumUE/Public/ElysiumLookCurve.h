#pragma once

#include "CoreMinimal.h"

// The mouse look path, from counts to degrees — the whole of it, as one value and one pure function
// (`docs/architecture/input-architecture.md` § Feel). Plain C++, no UObject reflection and no
// console: the shaping is the entire rule, so it is asserted with no world, no local player and no
// input device — `Elysium.Substrate.LookCurve`.
//
// **The shipped tuning is exactly retail.** `sensitivity` 3 x `m_yaw`/`m_pitch` 0.022 is VtMB's
// 0.066 degrees per count, and `Curve` defaults to zero, which makes the gain identically 1.0. A
// non-zero `Curve` is a stated Feel divergence: VtMB's mouse path has no acceleration and no filter
// (`docs/vtmb/source_movement.md` § View / camera), and no `CInput::MouseMove` decompile exists to
// recover one from — the constants are recovered, the code path is not.
//
// It lives here rather than in an Enhanced Input modifier asset so the curve is A/B-able against
// the decompile in a test tier, and so one thing owns the feel. `ElysiumUserCmd.h` includes this;
// nothing here may include it back.

namespace ElysiumInput
{
	// VtMB's pitch clamp (`cl_pitchup` / `cl_pitchdown`, both 89). Applied where the command's look
	// delta reaches the control rotation, which is the one place a view angle is integrated.
	inline constexpr float PitchClampDegrees = 89.0f;

	// The mouse scale and the curve over it. The first three are recovered and reproduce retail; the
	// last four are ours and are inert at their defaults.
	struct FElysiumLookTuning
	{
		// --- Recovered (`docs/vtmb/source_movement.md` § View / camera) ---------------------------
		float Sensitivity = 3.0f;      // `sensitivity`
		float MouseYaw    = 0.022f;    // `m_yaw`
		// `m_pitch`. A negative value is VtMB's invert-Y, so the sign rides through the scale and the
		// curve untouched rather than being read as a setting anywhere.
		float MousePitch  = 0.022f;

		// --- Ours. At these values the curve is the identity ---------------------------------------
		// How much gain the curve may add. **Zero is exactly retail**, and it is the shipped default.
		float Curve     = 0.0f;
		// Shapes how the gain ramps with speed. Inert while `Curve` is zero.
		float Exponent  = 1.0f;
		// The hand speed, degrees per second, the ramp normalizes against.
		float Threshold = 300.0f;
		// Ceiling on the gain, so a hitch cannot multiply a frame's delta without bound.
		float MaxScale  = 4.0f;

		// Degrees per mouse count, per axis. Retail's 0.066 at the shipped values.
		float YawScale() const { return Sensitivity * MouseYaw; }
		float PitchScale() const { return Sensitivity * MousePitch; }

		// True while this tuning reproduces VtMB. The predicate a divergence is named by — the code
		// that engages the curve says so out loud rather than leaving it to a cvar dump.
		bool IsRetailLinear() const { return Curve == 0.0f; }

		// Re-read the whole surface. `Lookup` returns a cvar's value string or empty for one the store
		// does not carry, which is how the console itself answers; an empty read keeps the default, so
		// a run with no `config.cfg` on disk behaves exactly like a stock install. Taking the reader as
		// a callback is what keeps this header free of the console (and testable with a hand-built
		// store) — the same shape as `FElysiumMoveTuning::LoadFrom`.
		void LoadFrom(TFunctionRef<FString(const TCHAR*)> Lookup);
	};

	// Shape one frame's mouse contribution, in degrees, into the degrees that reach the view.
	//
	// The **whole 2D delta**, never one axis at a time: the gain keys on the delta's magnitude, and
	// applying it per-axis would make a diagonal flick curve differently from either of its
	// components. `DeltaSeconds` is what turns a per-frame delta into the hand speed the ramp reads,
	// so the same physical motion shapes the same way at 60 and at 240.
	//
	// At the shipped tuning this returns `MouseDegrees` unchanged — the gain is exactly 1.0, not
	// approximately, so the retail path is a multiply by one rather than a rounding.
	FVector2D ShapeMouseLook(const FVector2D& MouseDegrees, const FElysiumLookTuning& Tuning,
		float DeltaSeconds);

	// One row of the cvar surface: the VtMB name (or ours), its default as typed, and what it does.
	// The table is the declaration; `FElysiumLookTuning::LoadFrom` is the read.
	struct FCvarDef
	{
		const TCHAR* Name;
		const TCHAR* Default;
		const TCHAR* Help;
	};
	TArrayView<const FCvarDef> CvarDefs();
}
