#pragma once

#include "CoreMinimal.h"

// The mover's rule set (roadmap 4.7, `docs/source_movement.md`).
//
// VtMB runs an early-Source `CGameMovement`: the *math* is stock Source, the *constants* are
// Troika's, and several differ from Half-Life 2. What lives here is that math as plain C++ with no
// UObject and no engine gameplay type, so every formula is asserted with no pawn, no world and no
// RHI (`Elysium.Substrate.Movement`) — the same split `ElysiumCameraSolve.h` and
// `ElysiumInputScope.h` use.
//
// `UElysiumMovementComponent` is the engine half: it owns the state, traces against real geometry,
// and drives these functions in `CGameMovement::PlayerMove`'s own order.

namespace ElysiumMove
{
	// One Source unit in cm. The cvar surface stays in **Source units** so a user's `config.cfg`
	// transfers unchanged; conversion happens once, at the point of use.
	inline constexpr float U = 2.54f;

	inline constexpr float Gravity      = 800.0f * U; // sv_gravity
	inline constexpr float Friction     = 4.0f;       // sv_friction (dimensionless)
	inline constexpr float StopSpeed    = 16.0f * U;  // sv_stopspeed
	inline constexpr float Accelerate   = 10.0f;      // sv_accelerate
	inline constexpr float AirAccel     = 10.0f;      // sv_airaccelerate
	inline constexpr float AirSpeedCap  = 30.0f * U;  // hardcoded in vampire.dll (0x104492a8)
	inline constexpr float StepSize     = 18.0f * U;  // sv_stepsize, via m_flStepSize
	inline constexpr float MaxVelocity  = 3500.0f * U;// sv_maxvelocity
	inline constexpr float JumpBoost    = 25.0f;      // sv_jump_boost — literally the apex, in units
	inline constexpr float JumpMaxSpeed = 350.0f * U; // sv_jump_maxspeed, while airborne

	// The retail player speed is animation-driven and no ConVar holds it; `speed_walk` /
	// `speed_runbase` are Troika's stated intent and are registered-but-never-read, which makes them
	// what a port with no player animation should use.
	inline constexpr float WalkSpeed    = 100.0f * U; // speed_walk
	inline constexpr float RunSpeed     = 225.0f * U; // speed_runbase (+5 per Athletics at 9.4)

	// The standable-normal test (a **double** at 0x104492d0) and the epsilon that keeps a down-trace
	// from arriving flush with the floor and reporting no contact.
	inline constexpr float StandableZ   = 0.7f;
	inline constexpr float DistEpsilon  = 0.08f;      // cm

	// --- The hulls (RE22, read out of the CGameMovement ctor 0x1011e0d0) --------------------
	// Standing is (-16,-16,0)..(16,16,72) with the eye at 64; ducked keeps the footprint and halves
	// the height, and its eye sits at **30** — not stock Source's VEC_DUCK_VIEW of 28.
	inline constexpr float HullHalfWidth  = 16.0f * U;
	inline constexpr float StandHeight    = 72.0f * U;
	inline constexpr float DuckHeight     = 36.0f * U;
	inline constexpr float StandViewZ     = 64.0f * U;
	inline constexpr float DuckViewZ      = 30.0f * U;

	// --- Duck timing (0x10447ee0 / 0x1044a2bc / 0x10449198) ---------------------------------
	// `m_flDucktime` counts in milliseconds from GameMovementDuckTime; the elapsed fraction is
	// `(1000 - ducktime) * 0.001` and is compared against these two.
	inline constexpr float GameMovementDuckTime = 1000.0f;  // ms
	inline constexpr float TimeToDuck           = 0.4f;     // s
	inline constexpr float TimeToUnduck         = 0.2f;     // s

	// --- Water (`WaterMove` 0x101200c0) ------------------------------------------------------
	// Two constants differ from HL2: the idle sink is **40**, not 60, and vertical placement uses
	// the hull midpoint rather than the eye. The 0.8 speed scale is stock.
	inline constexpr float WaterSpeedScale = 0.8f;          // 0x104491a8, a double
	inline constexpr float WaterSinkSpeed  = 40.0f * U;     // 0x10462950
	inline constexpr float WaterStopSpeed  = 0.1f * U;      // 0x104491b4 — below this, velocity zeroes

	inline constexpr float NoclipSpeed  = 1200.0f;    // cm/s — a dev speed, no original
	inline constexpr float NoclipBoost  = 3.0f;
}

// How deep the body is in water (`player+0x3e0`). The move branches on Waist and above.
enum class EElysiumWaterLevel : uint8
{
	None  = 0,
	Feet  = 1,
	Waist = 2,
	Eyes  = 3,
};

// --------------------------------------------------------------------------------------------
// The math
// --------------------------------------------------------------------------------------------
//
// Every one of these is a value transform on a velocity: no component, no pawn, no UWorld. That is
// what lets `Elysium.Substrate.Movement` assert the numbers `source_movement.md` records rather
// than asserting that a pawn moved.

namespace ElysiumMove
{
	// `Friction` (0x10120ba0) — ground only (the caller gates it), and it scales all **three**
	// components off the 3D speed, not the horizontal one.
	void ApplyFriction(FVector& Velocity, float FrictionCoeff, float InStopSpeed,
		float SurfaceFriction, float Dt);

	// `Accelerate` (0x101212e0).
	void ApplyAccelerate(FVector& Velocity, const FVector& WishDir, float WishSpeed,
		float AccelCoeff, float SurfaceFriction, float Dt);

	// `AirAccelerate` (0x10121000). The cap binds the **target** while the uncapped `WishSpeed`
	// still drives `accelspeed` — that asymmetry is the whole of Source's air-strafing behaviour,
	// and it is why this cannot share a body with ApplyAccelerate.
	void ApplyAirAccelerate(FVector& Velocity, const FVector& WishDir, float WishSpeed,
		float AirAccelCoeff, float InAirSpeedCap, float SurfaceFriction, float Dt);

	// `WaterMove`'s friction step: no stopspeed floor, and it zeroes below WaterStopSpeed.
	void ApplyWaterFriction(FVector& Velocity, float FrictionCoeff, float SurfaceFriction, float Dt);

	// Source's `ClipVelocity`: project onto the plane with an overbounce term, and return the
	// blocked bitfield (1 = floor, 2 = step/wall). `FVector::VectorPlaneProject` is this with
	// overbounce 1.0 and the return value thrown away — the caller needs the bits to run the
	// two-plane crease case a doorway corner hits.
	int32 ClipVelocity(const FVector& In, const FVector& Normal, FVector& Out,
		float Overbounce = 1.0f);

	// The half-step split. `FullWalkMove` applies half the interval's gravity before the move and
	// half after (StartGravity 0x1011fa80 / FinishGravity 0x10120f30), which is what puts the jump
	// apex at a flat JumpBoost rather than `JumpBoost - 100*dt`.
	void StartGravity(FVector& Velocity, float GravityAccel, float Dt);
	void FinishGravity(FVector& Velocity, float GravityAccel, float Dt);

	// `CheckVelocity` clamps each **component** to sv_maxvelocity, and scrubs non-finite values.
	// This is not `GetClampedToMaxSize`, which clamps the magnitude — the two differ exactly at
	// terminal velocity, which is where a long fall lives.
	void CheckVelocity(FVector& Velocity, float MaxVel);

	// The jump, and its inverse. `sv_jump_boost` is literally the apex height in units, since
	// `v^2 / 2g = boost`.
	float JumpVelocity(float JumpBoostUnits, float GravityAccel);
	float ApexHeight(float LaunchZ, float GravityAccel);

	// Wish direction in a given frame, with Source's normalize-and-clamp: a keyboard diagonal is
	// length sqrt(2), so it normalizes rather than summing, and the scale clamps to 1. Takes an
	// FRotator rather than a controller, which is what makes it testable.
	FVector WishDirection(const FVector2D& Move, float Up, const FRotator& Frame,
		bool bIncludePitch, float& OutScale);
}

// --------------------------------------------------------------------------------------------
// The timestep
// --------------------------------------------------------------------------------------------

// How a frame's delta is chopped into integration steps.
//
// **The faithful answer is "it isn't."** `Host_FilterTime` bounds a *variable* frametime and
// returns — there is no accumulator and no fixed interval anywhere in the engine, which is what
// pins VtMB to the pre-tick Source branch (`game_runtime.md` § "Time model"). So retail's movement
// really is frame-rate dependent: `AirAccelerate`'s `addspeed` clamp stops binding above ~117 fps,
// and the full-step gravity puts the jump apex at `JumpBoost - 100*dt` rather than a flat
// `JumpBoost`.
//
// **A fixed step is therefore a divergence, not the baseline** — `elysium.move.FixedStep` defaults
// to 0 (raw delta, faithful) and a non-zero value opts into frame-rate independence. It is worth
// having because it is the only way to A/B the two, and because the difference is measurable
// rather than a matter of taste. Recorded as a divergence in `docs/source_movement.md`.
//
// Either way the move body is written once: this decides only how many times and with what dt.
struct FElysiumMoveStepper
{
	// Seconds per step. 0 = one step at the frame's own delta — the faithful path.
	float FixedStep = 0.0f;

	// A backstop, not a tuning knob. With the frame delta already bounded to 0.1 s, a 100 Hz step
	// needs at most 10, so this can only be reached if FixedStep is set absurdly small.
	int32 MaxSubSteps = 16;

	// Begin a frame. Returns how many steps to run and writes the dt each one takes; the unspent
	// remainder is carried, never dropped.
	int32 BeginFrame(float DeltaSeconds, float& OutStepSeconds);

	// How far through the pending step the accumulator sits, 0..1 — what a view interpolation would
	// need to hide the up-to-one-step lag a fixed step introduces. Nothing consumes it yet.
	float Alpha() const;

	void Reset() { Accumulator = 0.0f; }

	bool IsFixed() const { return FixedStep > 0.0f; }

private:
	float Accumulator = 0.0f;
};

// --------------------------------------------------------------------------------------------
// The cvar surface
// --------------------------------------------------------------------------------------------

// VtMB's movement cvars, reproduced 1:1 by name and default (`source_movement.md` § Movement).
// They are **declared into the VtMB console store**, not registered as `elysium.*` engine cvars, so
// a user's `config.cfg` keeps governing and `elysium.cmd sv_gravity 400` is the same write the game
// itself would make.
//
// Values are held here in **cm**; the table's defaults are in Source units, exactly as they are
// typed, and `LoadFrom` converts once.
struct FElysiumMoveTuning
{
	float Gravity      = ElysiumMove::Gravity;
	float Friction     = ElysiumMove::Friction;
	float StopSpeed    = ElysiumMove::StopSpeed;
	float Accelerate   = ElysiumMove::Accelerate;
	float AirAccel     = ElysiumMove::AirAccel;
	float AirSpeedCap  = ElysiumMove::AirSpeedCap;
	float StepSize     = ElysiumMove::StepSize;
	float MaxVelocity  = ElysiumMove::MaxVelocity;
	float JumpMaxSpeed = ElysiumMove::JumpMaxSpeed;

	// Held in **Source units**, because it is the apex height and reads as one.
	float JumpBoost    = ElysiumMove::JumpBoost;

	// The launch speed the tuning implies.
	float JumpSpeed() const { return ElysiumMove::JumpVelocity(JumpBoost, Gravity); }

	// Re-read the whole surface. `Lookup` returns a cvar's value string or empty for one the store
	// does not carry, which is how the console itself answers; an empty read keeps the default, so a
	// run with no `out/cfg` on disk behaves exactly like a stock install. Taking the reader as a
	// callback is what keeps this header free of the console (and testable with a hand-built store).
	void LoadFrom(TFunctionRef<FString(const TCHAR*)> Lookup);
};

namespace ElysiumMove
{
	// One row of the cvar surface: the VtMB name, its default **as typed** (Source units), and what
	// it does. The table is the declaration; `FElysiumMoveTuning::LoadFrom` is the read.
	struct FCvarDef
	{
		const TCHAR* Name;
		const TCHAR* Default;
		const TCHAR* Help;
	};
	TArrayView<const FCvarDef> CvarDefs();
}
