#pragma once

#include "CoreMinimal.h"
#include "ElysiumLocomotionSample.h"   // EElysiumWaterLevel — body state, which WaterMove branches on

// The mover's rule set (roadmap 4.7, `docs/vtmb/source_movement.md`).
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
	inline constexpr float JumpMaxSpeed = 350.0f * U; // sv_jump_maxspeed, while airborne

	// --- The jump, which is Troika's and not Source's ------------------------------------------
	// `sv_jump_boost` is an instant **origin** displacement on the press frame, in inches — its own
	// help string says so — not an apex height. The rest of the jump is authored in
	// `vdata/system/rules.txt` → `RuleData/Jumping`.
	inline constexpr float JumpBoost    = 25.0f;      // sv_jump_boost, Source units (inches)
	// The pop is scaled by 0.99 and by the hull trace's fraction, so it can never seat the body in
	// a ceiling (`0x10462968`, read as a double).
	inline constexpr float JumpBoostScale = 0.99f;

	inline constexpr float BaseJumpVelocity      = 185.0f * U;   // rules.txt BaseJumpVelocity
	inline constexpr float JumpGravityMultiplier = 0.75f;        // rules.txt JumpGravityMultiplier
	inline constexpr float JumpHoldSeconds       = 0.2f;         // rules.txt JumpHoldTime

	// The retail player speed is animation-driven and no ConVar holds it; `speed_walk` /
	// `speed_runbase` are Troika's stated intent and are registered-but-never-read, which makes them
	// what a body with no resolved gait fan falls back to.
	inline constexpr float WalkSpeed    = 100.0f * U; // speed_walk
	inline constexpr float RunSpeed     = 225.0f * U; // speed_runbase (+5 per Athletics at 9.4)

	// What the animation authority scales each gait's authored cells by — function-local statics in
	// `CHL2_Player::PreThink`, and the reason retail's crouch outruns its walk. Dimensionless.
	inline constexpr float WalkScale    = 1.0f;       // sv_walkscale
	inline constexpr float RunScale     = 1.0f;       // sv_runscale
	inline constexpr float SneakScale   = 2.3f;       // sv_sneakscale

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

// --------------------------------------------------------------------------------------------
// The math
// --------------------------------------------------------------------------------------------
//
// Every one of these is a value transform on a velocity: no component, no pawn, no UWorld. That is
// what lets `Elysium.Substrate.Movement` assert the numbers `docs/vtmb/source_movement.md` records rather
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

	// Apex of a ballistic launch — still the right tool for reading a fall or the tail of a jump,
	// but NOT for predicting VtMB's jump height, which is a held push and not one impulse.
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
// pins VtMB to the pre-tick Source branch (`docs/vtmb/game_runtime.md` § "Time model"). So retail's movement
// really is frame-rate dependent: `AirAccelerate`'s `addspeed` clamp stops binding above ~117 fps,
// and the full-step gravity puts the jump apex at `JumpBoost - 100*dt` rather than a flat
// `JumpBoost`.
//
// The mover therefore integrates the command's own delta once per frame, with no accumulator and
// no substepping — the same shape retail has.

// --------------------------------------------------------------------------------------------
// The cvar surface
// --------------------------------------------------------------------------------------------

// VtMB's movement cvars, reproduced 1:1 by name and default (`docs/vtmb/source_movement.md` § Movement).
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

	// --- The jump -----------------------------------------------------------------------------
	// VtMB does **not** use Source's jump. Its own model is a *constant upward push held for a
	// window*, under reduced gravity, preceded by an instant origin pop — all four numbers below
	// come from `vdata/system/rules.txt` → `RuleData/Jumping`, not from a `CGameMovement` constant
	// (`docs/vtmb/source_movement.md` → "The jump is not stock Source's").

	// `BaseJumpVelocity`, cm/s. The velocity ADDED on the press frame and re-asserted every frame
	// the button stays down inside the hold window.
	float BaseJumpVelocity = ElysiumMove::BaseJumpVelocity;

	// `JumpGravityMultiplier` — gravity runs at this scale for the whole jump.
	float JumpGravityMultiplier = ElysiumMove::JumpGravityMultiplier;

	// `JumpHoldTime`, seconds — how long the push keeps being applied while held.
	float JumpHoldSeconds = ElysiumMove::JumpHoldSeconds;

	// `sv_jump_boost`, held in **Source units** because the cvar's own help calls them inches:
	// "How many extra inches to add to the player's origin on the first frame of the jump".
	float JumpBoost    = ElysiumMove::JumpBoost;

	// Seed the four jump values from the rulebook. Keys absent from `rules.txt` keep their
	// defaults, so a run without the vdata mirror behaves like the shipped tuning.
	void LoadJumpFrom(TFunctionRef<bool(const TCHAR*, float&)> Lookup);

	// Write one field by its rulebook key (`BaseJumpVelocity`, `JumpGravityMultiplier`,
	// `JumpHoldTime`), in the unit that key is authored in. Returns false for a key this does not
	// carry, which is what keeps a caller from believing an override it never made.
	//
	// The `sv_*` half is deliberately not writable here: `LoadFrom` re-reads it from the console
	// store every frame, so the console *is* the way to set one and a second door would silently
	// lose to it on the next tick.
	bool SetJumpRule(const TCHAR* Key, float Value);

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
