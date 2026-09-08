#pragma once

#include "CoreMinimal.h"

// The look path, from what a device reports to the degrees that reach the view — the whole of it, as
// values and pure functions. Two devices, two
// tunings, one file: `FElysiumLookTuning` is the mouse and `FElysiumStickTuning` is the pad. Plain
// C++, no UObject reflection and no console: the shaping is the entire rule, so it is asserted with
// no world, no local player and no input device — `Elysium.Substrate.LookCurve` and
// `Elysium.Substrate.StickLook`.
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
	// VtMB's pitch clamp (`cl_pitchup` / `cl_pitchdown`, both 89). Carried onto
	// `AElysiumPlayerCameraManager`'s `ViewPitchMin`/`ViewPitchMax`, so the one place a view angle is
	// integrated — the engine's `UpdateRotation`, through `ProcessViewRotation` — applies it.
	inline constexpr float PitchClampDegrees = 89.0f;

	// The mouse scale and the curve over it. The first three are recovered and reproduce retail; the
	// last four are ours and are inert at their defaults.
	struct FElysiumLookTuning
	{
		// Recovered (`docs/vtmb/source_movement.md` § View / camera).
		float Sensitivity = 3.0f;      // `sensitivity`
		float MouseYaw    = 0.022f;    // `m_yaw`
		// `m_pitch`. A negative value is VtMB's invert-Y, so the sign rides through the scale and the
		// curve untouched rather than being read as a setting anywhere.
		float MousePitch  = 0.022f;

		// Ours. At these values the curve is the identity.
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

	// The stick path.
	//
	// A thumbstick is not a mouse and cannot be shaped like one. The mouse reports a *displacement*
	// the hand already made; IA_MouseLook's Enhanced Input Smooth modifier normalizes how those device
	// samples reach frames before the router sees them. A stick reports a *held deflection* that the
	// game integrates into a turn, and the device's noise is integrated with it.
	//
	// Measured on the shipped pad rather than assumed (`elysium.LookProbe`): the axes quantise to
	// 1/127 (8-bit), the resting centre sits about 0.04 off zero, and during a steady hold the Y axis
	// swings ±0.2 around its mean between consecutive frames while X holds to ±0.04. Multiplied by a
	// flat rate that noise *is* the view's motion, which is what an unfiltered stick path renders as
	// judder. Every term below answers one measured property:
	//
	//   * `DeadZone` covers the resting offset — a scaled radial zone, so the shaped value leaves zero
	//     continuously rather than stepping (Josh Sutphin, *Doing Thumbstick Dead Zones Right*);
	//   * `Saturation` retires the top of the travel, where the noise rides on a value that was going
	//     to clamp anyway;
	//   * `Exponent` buys back the fine-control region the dead zone costs, and shrinks the absolute
	//     rate error near centre where aim actually happens;
	//   * `SmoothHalfLife` is the one that answers the frame-to-frame swing directly. A half-life, so
	//     it is exact at any step, the same rule `ElysiumRig::DampToward` uses;
	//   * `AccelScale` lets a sustained hold outrun the precision curve without raising the rate a
	//     small deflection produces.
	//
	// **Gamepad feel has no original to reproduce**: VtMB ships raw joystick cvars, no UI, no default
	// binds and a `joystick.cfg` that does not exist. So unlike `FElysiumLookTuning`, nothing here is
	// recovered and there is no retail
	// identity to stay inert against — these are ours outright, and they sit on the Feel axis.
	struct FElysiumStickTuning
	{
		// Deflection at or below which the stick reads as centred, and the deflection at which it
		// reads as fully pushed. The band between them is remapped to 0..1.
		float DeadZone   = 0.12f;
		float Saturation = 0.92f;

		// The response curve over that band. 1 is linear; 2 is the shipped default.
		float Exponent = 2.0f;

		// Degrees per second at full deflection. Pitch is the slower axis — the shorter travel of a
		// look-up/look-down gesture wants less rate, and it is also the noisier axis on the measured
		// pad, so a lower rate costs less when the noise gets through.
		float YawRate   = 190.0f;
		float PitchRate = 130.0f;

		// The filter's half-life, seconds. 0 is unfiltered. This is the term that trades latency for
		// stability, and it is the first one to reach for when the pad feels noisy rather than slow.
		float SmoothHalfLife = 0.035f;

		// The sustained-turn ramp. A hold at or above `AccelEnter` charges toward `AccelScale` over
		// `AccelTime` seconds and discharges at the same rate when it drops below. `AccelScale` 1 is
		// no ramp at all, which is the shipped default — the curve is doing the work, and a ramp is
		// the delta to reach for after it, one owner call at a time.
		float AccelScale = 1.0f;
		float AccelTime  = 0.4f;
		float AccelEnter = 0.9f;

		// The movement stick's own band. Separate from the look band because the two answer different
		// questions: the look zone is sized to the pad's resting noise, and the move zone is sized to
		// what must not make the player creep — a standing character is a more visible failure than a
		// view that drifts a tenth of a degree, so it is the wider of the two.
		float MoveDeadZone   = 0.14f;
		float MoveSaturation = 0.92f;

		void LoadFrom(TFunctionRef<FString(const TCHAR*)> Lookup);
	};

	// What the filter and the ramp carry between frames. Held by the command builder, because the
	// shaping happens where the clamped frame delta does.
	struct FElysiumStickState
	{
		FVector2D Shaped = FVector2D::ZeroVector;
		float AccelSeconds = 0.0f;

		void Reset() { Shaped = FVector2D::ZeroVector; AccelSeconds = 0.0f; }
	};

	// Shape one frame of raw stick deflection into a **look rate**, degrees per second. The caller
	// multiplies by the frame delta, exactly as it does for the keyboard turn keys — this returns a
	// rate rather than a finished delta so a hitch or a time dilation cannot reach the command stream
	// through the pad alone.
	//
	// The **whole 2D deflection**, never one axis at a time: the dead zone, the saturation and the
	// curve all key on the vector's magnitude, so a diagonal push shapes as one gesture rather than
	// as two independent ones. Per-axis shaping is what makes a stick feel square.
	FVector2D ShapeStickLook(const FVector2D& Deflection, const FElysiumStickTuning& Tuning,
		float DeltaSeconds, FElysiumStickState& State);

	// The movement stick's own shaping: the same scaled radial dead zone and saturation, and
	// deliberately **no curve, no filter and no ramp**. A wish vector is not a rate — the mover
	// already owns acceleration (`ElysiumMove`), so a second ramp here would be two owners of one
	// feel, and a curve would make the walk/run threshold land somewhere other than where the stick
	// says it does. Output is 0..1 in magnitude, in the stick's own (right, up) frame.
	FVector2D ShapeStickMove(const FVector2D& Deflection, const FElysiumStickTuning& Tuning);

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
