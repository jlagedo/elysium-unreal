#pragma once

#include "CoreMinimal.h"
#include "ElysiumLocomotionSample.h"   // EElysiumWaterLevel — body state, which WaterMove branches on

// The mover's rule set (`docs/vtmb/source_movement.md`).
//
// VtMB runs an early-Source `CGameMovement`: the *math* is stock Source, the *constants* are
// Troika's, and several differ from Half-Life 2. What lives here is that math as plain C++ with no
// UObject and no engine gameplay type, so every formula is asserted with no pawn, no world and no
// RHI (`Elysium.Substrate.Movement`) — the same split `ElysiumCameraSolve.h` and
// `ElysiumInputScope.h` use.
//
// `UElysiumMovementComponent` is the engine half: it owns the state, traces against real geometry,
// and drives these functions in `CGameMovement::PlayerMove`'s own order.

// The mover's log category, declared here rather than in the component because the pure half owns
// the cvar surface and has to be able to refuse a value out loud.
ELYSIUMUE_API DECLARE_LOG_CATEGORY_EXTERN(LogElysiumMovement, Log, All);

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

	// The jump, which is Troika's and not Source's.
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
	// `speed_runbase` are Troika's stated intent and are registered-but-never-read. **Neither is on
	// the mover's path**: a gait with no resolved fan commands zero, which is what retail's unwritten
	// table slot holds. What still reads them is the animation classifier's reference gait
	// (`FElysiumGaitReference`), which needs a walk/run split even for a body that publishes no fan.
	inline constexpr float WalkSpeed    = 100.0f * U; // speed_walk
	inline constexpr float RunSpeed     = 225.0f * U; // speed_runbase (+5 per Athletics)

	// What the animation authority scales each gait's authored cells by — function-local statics in
	// `CHL2_Player::PreThink`, and the reason retail's crouch outruns its walk. Dimensionless.
	inline constexpr float WalkScale    = 1.0f;       // sv_walkscale
	inline constexpr float RunScale     = 1.0f;       // sv_runscale
	inline constexpr float SneakScale   = 2.3f;       // sv_sneakscale

	// The standable-normal test (a **double** at 0x104492d0) and the epsilon that keeps a down-trace
	// from arriving flush with the floor and reporting no contact.
	inline constexpr float StandableZ   = 0.7f;
	inline constexpr float DistEpsilon  = 0.08f;      // cm

	// The hulls, read out of the CGameMovement ctor 0x1011e0d0.
	// Standing is (-16,-16,0)..(16,16,72) with the eye at 64; ducked keeps the footprint and halves
	// the height, and its eye sits at **30** — not stock Source's VEC_DUCK_VIEW of 28.
	inline constexpr float HullHalfWidth  = 16.0f * U;
	inline constexpr float StandHeight    = 72.0f * U;
	inline constexpr float DuckHeight     = 36.0f * U;
	inline constexpr float StandViewZ     = 64.0f * U;
	inline constexpr float DuckViewZ      = 30.0f * U;

	// Duck timing (0x10447ee0 / 0x1044a2bc / 0x10449198).
	// `m_flDucktime` counts in milliseconds from GameMovementDuckTime; the elapsed fraction is
	// `(1000 - ducktime) * 0.001` and is compared against these two.
	inline constexpr float GameMovementDuckTime = 1000.0f;  // ms
	inline constexpr float TimeToDuck           = 0.4f;     // s
	inline constexpr float TimeToUnduck         = 0.2f;     // s

	// Water (`WaterMove` 0x101200c0).
	// Two constants differ from HL2: the idle sink is **40**, not 60, and vertical placement uses
	// the hull midpoint rather than the eye. The 0.8 speed scale is stock.
	inline constexpr float WaterSpeedScale = 0.8f;          // 0x104491a8, a double
	inline constexpr float WaterSinkSpeed  = 40.0f * U;     // 0x10462950
	inline constexpr float WaterStopSpeed  = 0.1f * U;      // 0x104491b4 — below this, velocity zeroes

	inline constexpr float NoclipSpeed  = 1200.0f;    // cm/s — a dev speed, no original
	inline constexpr float NoclipBoost  = 3.0f;
}

// The math.
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

	// The most planes one bump sequence accumulates before the move is abandoned. Source's
	// `MAX_CLIP_PLANES`.
	inline constexpr int32 MaxClipPlanes = 5;

	// `TryPlayerMove`'s plane resolution, which is the half of it that needs no world.
	//
	// Clipping against each plane in turn is not enough on its own: in a doorway's interior corner
	// the projection that satisfies wall A drives back into wall B, so the loop below looks for a
	// projection that satisfies EVERY plane at once and, failing that, sends the body along the
	// **crease** the two planes share. Without it the four bumps are spent re-clipping between two
	// walls and the move is abandoned, which is what makes a body stick on a jamb instead of sliding
	// through it.
	//
	// `Original` is the velocity this bump sequence started with (re-baselined whenever the body
	// actually moved) and `Primal` the velocity the whole move started with. Returns false when the
	// move has to be abandoned — more than two planes with no common projection, or a resolution that
	// reverses the original intent, both of which Source answers by zeroing the velocity.
	bool ResolveClipPlanes(TArrayView<const FVector> Planes, const FVector& Original,
		const FVector& Primal, FVector& OutVelocity);

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

	// `CheckParameters`' command clamp (0x1011f140) — the move command's own speed ceiling, and it
	// is the **3-vector** magnitude: `forwardmove`, `sidemove` and `upmove` together, scaled by one
	// ratio so the commanded direction survives untouched. Returns whether it bound.
	//
	// Retail clamps the same ceiling a second time inside `WalkMove`, in the wishvel slots the
	// direct-assignment fork then reads (0x101214cb). That one forces `z` to zero first, so it
	// bounds a 2D magnitude this has already bounded — `sqrt(x^2+y^2) <= sqrt(x^2+y^2+z^2)`. This
	// is the stricter of the pair, and reproducing it reproduces both.
	//
	// A `MaxSpeed` of zero zeroes the command, which is retail's own arithmetic at that value.
	bool ClampCommandSpeed(FVector& CommandCmS, float MaxSpeed);

	// --- `CPlayerMove::SetupMove` `0x10186120`: `CMoveData::m_vecAngles` and the grapple glue ----
	//
	// **What `m_vecAngles` (`+0x58`) is.** Not the frame the mover resolves forward/side against —
	// that is `m_vecViewAngles` (`+0x0c`), which `CGameMovement::PlayerMove` `client.dll 0x100edd80`
	// feeds to `AngleVectors(mv+0x0c, &m_vecForward, &m_vecRight, &m_vecUp)`.
	// `+0x58` is the **body-angle writeback**: `CPlayerMove::FinishMove` `0x10186c10` clamps its
	// pitch to ±90, pushes that pitch into the `body_pitch` pose parameter, and calls
	// `SetLocalAngles(mv->m_vecAngles)` (slot `+0x100`). (This corrects `rc_group_f.md` §RC15.2 and
	// the same sentence in `docs/vtmb/camera-view-modes.md`; the store listing in both is right, the
	// consumer named for it is not.)
	//
	// **What `SetupMove` writes into it.** Every tick, unconditionally
	// (`0x10186458`-`0x1018647d`): all three components from `GetAngles()` (slot 221, the entity's
	// own local angles), then `m_vecAngles.yaw = m_angEyeAngles.y (+0x2070)`. The body's yaw is
	// snapped to the eye's, its pitch and roll are written back unchanged. At the tail the arm
	// below can re-take all three from `GetAngles()`, discarding that substitution.
	//
	// The port's default arm is `AElysiumPawn`'s `bUseControllerRotationYaw = true` with pitch and
	// roll off — the same rotation, by the same rule, one frame later in `FaceRotation`.
	struct FSetupMoveBodyState
	{
		// `EHANDLE_Get(player+0x1538) != NULL && player+0x153c != -1` — a live grapple partner.
		bool bGrapplePartnerLive = false;

		// `m_IdealActivity (+0xff0)` is one of the nine the grapple switch exempts. All nine are
		// the **release** verbs of the feed families, recovered by name from
		// `RegisterGrappleActivity` `0x10412590`'s call list in `0x104126e0`:
		//   0xf88  3976  ACT_FEEDING_FEED_RELEASE
		//   0xf91  3985  ACT_FEEDING_RELEASE
		//   0xf9a  3994  ACT_FEEDING_RELEASE_PC_FLYBACK
		//   0xfb7  4023  ACT_SEDUCTIVE_RELEASE
		//   0xfc0  4032  ACT_SEDUCTIVE_RELEASE_TO_MEZ
		//   0xff7  4087  ACT_ZOMBIE_FEEDING_FEED_RELEASE
		//   0x1000 4096  ACT_ZOMBIE_FEEDING_RELEASE
		//   0x1009 4105  ACT_ZOMBIE_FEEDING_RELEASE_PC_FL…  (symbol truncated at 32 chars)
		//   0x1039 4153  ACT_RAT_FEED_RELEASE
		// So the exemption is not an arbitrary set: **while the release is playing the glue is off
		// and the player carries himself out of the pairing.** The port's `EElysiumFeedPhase`
		// `Release`/`ReleaseTail` are those verbs.
		bool bGrappleReleaseActivity = false;

		// `m_iVFlags & 0x1` — `EElysiumViewFlags::MoveAnglesFromEntity`.
		bool bMoveAnglesFromEntity = false;
	};

	// `CBasePlayer::ProcessUsercmds` `0x1016aaf0` and `CPlayerMove::RunCommand` `0x101874a0`, which
	// carry the same test verbatim:
	//
	//     if (player->+0x207c == 0 && !HasAllVFlags(player, 8)) m_angEyeAngles = cmd->viewangles;
	//
	// `+0x207c` is the one-shot "a forced snap is pending" latch `FUN_10178550` raises and `0x8` is
	// the persistent `EElysiumViewFlags::ViewAngleLock` — two doors onto one refusal, and the
	// one-shot is the higher-priority of the pair because a `point_player` tick or a terminal's
	// near-arm snap has just written the angles the command would overwrite. `SetupMove` then feeds
	// `mv->m_vecViewAngles (+0x0c)` from the frozen `m_angEyeAngles` rather than from the command,
	// so the *move* runs on the frozen view too.
	bool EyeAnglesAdoptCommand(bool bPendingEyeAngleSnap, bool bViewAngleLock);

	// False when the tail arm re-takes all three components from `GetAngles()`, i.e. when the yaw
	// substitution made at the top of `SetupMove` is discarded and the body keeps its own yaw.
	//
	// Note the precedence, which is the arm's whole shape: **with a live partner the flag is never
	// consulted** — both switch arms `goto ANGLES_FROM_ENTITY` — and only without one does
	// `GetVFlags() & 1` decide.
	bool BodyYawFollowsEye(const FSetupMoveBodyState& State);

	// True in the partner arm: a live grapple outside the nine release verbs. That arm re-origins
	// the move onto the partner every tick and zeroes the velocity, and it runs whether or not
	// `m_iVFlags & 0x1` is set. This — not the flag — is what makes a grappled player immobile.
	bool GrappleGluesBody(const FSetupMoveBodyState& State);

	// `mv->m_vecAbsOrigin = partner->GetOrigin(); mv->m_vecAbsOrigin.z -= (playerMins.z -
	// partnerMins.z);` — the correction that lines the two collision hulls' feet up. Both operands
	// are the bottom of an entity's collision bounds **relative to its own origin**; this runtime
	// places every body's origin at its feet (`AElysiumMapActor::TeleportPlayer`), so both are 0 and
	// the correction is identically zero. The parameters exist so the recovered term is present at
	// the point it applies rather than silently folded away, and so a body whose origin ever stops
	// being its feet fails here instead of drifting.
	FVector GluedBodyFeetOrigin(const FVector& PartnerFeetOrigin, float PlayerCollisionMinZ,
		float PartnerCollisionMinZ);
}

// The timestep.

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

// The cvar surface.

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

	// The jump.
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

private:
	// Names whose value could not be parsed, so the refusal is stated once rather than on every
	// frame `LoadFrom` re-reads the store. Cleared with the owning component, so a corrected value
	// that later goes bad again is reported again in the next session rather than swallowed forever.
	TSet<FName> ReportedMalformed;
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
