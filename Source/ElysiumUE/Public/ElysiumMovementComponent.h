#pragma once

#include "CoreMinimal.h"
#include "ElysiumClipMovement.h"
#include "ElysiumGaitSpeeds.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumUserCmd.h"
#include "GameFramework/PawnMovementComponent.h"

#include "ElysiumMovementComponent.generated.h"

// The player's mover.
//
// It exists because `ACharacter` cannot take a box root and Source's `StepMove` depends on the hull
// being an AABB: a capsule's rounded bottom catches a step's top edge and reports ~0.65 against the
// 0.7 standable test, so *every* climb is rejected (`docs/vtmb/source_movement.md` § StepMove). So the body is
// an `APawn` with a `UBoxComponent` and this replaces `UCharacterMovementComponent` wholesale.
//
// The structure is Source's and the constants are RE'd; the math itself lives in
// `ElysiumMoveSolve.h` so it is asserted without a world. What remains on the component is the state
// machine around it — the gravity half-step split, ducking, water, and `CategorizePosition`'s full
// contract. Everything reads one `FElysiumUserCmd` and nothing polls a key (S5).
UCLASS()
class UElysiumMovementComponent : public UPawnMovementComponent
{
	GENERATED_BODY()

public:
	UElysiumMovementComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	virtual float GetMaxSpeed() const override;
	virtual bool IsMovingOnGround() const override { return bOnGround && !bNoclip; }
	virtual bool IsFalling() const override { return !bOnGround && !bNoclip; }

	// The frame's intent, from the router. Held rather than acted on immediately: movement runs at
	// step 5 of the frame, after the substrate's thinks have moved the doors.
	void SetUserCmd(const FElysiumUserCmd& InCmd) { PendingCmd = InCmd; }

	void SetNoclip(bool bEnable);
	bool IsNoclip() const { return bNoclip; }

	// The speed authority.
	// The animation's own per-direction speeds, pushed by whoever built the body's visual rather
	// than pulled per frame: the tables depend on the body, not on the frame, and the mover runs
	// ahead of the resolver that could answer for one.
	//
	// A gait whose fan did not resolve commands **zero**, which is what retail's unwritten table slot
	// holds, and the resolver has already named the body and the gait that failed. A mover nothing
	// ever pushed tables onto is the different case — a state retail has no equivalent of, because a
	// player there always wears a model — so it is latched and reported once by `WishSpeed` when a
	// real command is erased by it, rather than being silently indistinguishable from an authored
	// absence.
	void SetGaitSpeeds(const FElysiumGaitSpeeds& InSpeeds)
	{
		GaitSpeeds = InSpeeds;
		bGaitSpeedsPublished = true;
	}
	void ClearGaitSpeeds()
	{
		GaitSpeeds = FElysiumGaitSpeeds();
		bGaitSpeedsPublished = false;
	}
	const FElysiumGaitSpeeds& GetGaitSpeeds() const { return GaitSpeeds; }

	// Animation-driven movement (`CPlayerMove::SetupMove`).
	// The swing that owns this body's command, pushed by the animation pass exactly as the gait
	// tables above are and for the same reason: the mover runs before the driver that knows the
	// answer, so it is told one frame later — which is where retail reads the cycle from too.
	//
	// While the pushed lock is active the command's three movement axes are DISCARDED and refilled
	// from the playing sequence's authored displacement, and `WalkMove` assigns the resulting wish to
	// the velocity instead of accelerating toward it. The move is still swept: nothing teleports.
	//
	// The refill is bounded by `GetMaxSpeed()` — `CheckParameters`' clamp — so a clip whose authored
	// blocks outrun the body's own peak carries it a shorter distance than the animation states.
	void SetAnimMovementLock(const FElysiumAnimMovementLock& InLock) { AnimLock = InLock; }
	void ClearAnimMovementLock() { AnimLock = FElysiumAnimMovementLock(); }
	const FElysiumAnimMovementLock& GetAnimMovementLock() const { return AnimLock; }
	// Whether this frame's command was actually refilled from a sequence. False while the lock holds
	// over a clip that authors no movement records at all — the command is still discarded, so the
	// body is held where it stands by ordinary friction rather than by an assignment.
	bool IsMoveAnimationDriven() const { return bSubstitutedMove; }

	// What the body is commanding this frame, cm/s — the wish speed the last solved substep used.
	// Published so the animation classifier can take retail's `cmdMoveMag` term without recomputing
	// the direction, and so a readout and the solve cannot disagree.
	float GetCommandedSpeed() const { return CommandedSpeed; }

	// Drop all carried motion and latches: velocity, the button latch, the duck, the step
	// accumulator. What a teleport wants — a body arriving somewhere new must not still be running.
	void ResetState();

	// Discard the body's carried motion, and nothing else: `CBasePlayer::PostThink`'s melee
	// stop, driven from the substrate on every frame of a swing's tail — from the playing sequence's
	// own `w_hold` to the end of its clip — with no direction key held
	// (`ElysiumClipMovement::StopsMeleeTailMotion`).
	//
	// It is not `ResetState` and not `SetFrozen`. Retail zeroes two velocities and leaves every
	// latch, the duck, the jump window and the pushed lock exactly where they were — the body is
	// stopped, not reset and not immobilised, and the very next command moves it again.
	//
	// The published sample goes with the component's velocity because it is retail's second call:
	// `SetLocalVelocity(vec3_origin)`. Every animation reader takes the body's motion from that
	// record, and the reader that matters runs later in the SAME frame, before the mover ticks
	// again — so a record left carrying the lunge is the whole defect this exists to fix.
	void StopBody();

	// Freeze the body where it stands (the spawn hold, while collision cooks).
	void SetFrozen(bool bFrozen);
	bool IsFrozen() const { return bFrozen; }

	bool IsOnGround() const { return bOnGround; }
	// Press-edge jumps taken since the last `ResetState`. The leniency courses' measurement: a jump
	// refused for being airborne never increments it, so this is what the gym's `ledge_*`/`land_*`
	// brackets read.
	int32 GetJumpsTaken() const { return JumpsTaken; }
	bool IsDucked() const { return bDucked; }
	bool IsDucking() const { return bDucking; }
	EElysiumWaterLevel GetWaterLevel() const { return WaterLevel; }
	// What the substrate calls when a water brush reports the body's depth. Nothing calls it yet —
	// no exported map places one (`docs/vtmb/source_movement.md` → "Water").
	void SetWaterLevel(EElysiumWaterLevel InLevel) { WaterLevel = InLevel; }
	float GetSurfaceFriction() const { return SurfaceFriction; }

	// Would the standing hull fit where we are? What stops a stand-up under a low ceiling, and what
	// `FinishUnDuck` gates on. Public because the gym's unduck-refusal brackets **measure** it: read
	// off `ducked` plus the command stream it would be an inference, and an inference cannot tell a
	// refused unduck from one that was never asked for.
	bool CanUnduck() const;

	// Override one `rules.txt` jump value for the run in flight, in the unit the key is authored
	// in. The gym's pop brackets need it: `sv_jump_boost` is an origin displacement, and with the
	// shipped 185 u/s held push also in play the body clears every roof that would bracket it, so
	// the pop can only be isolated by suppressing the push. Latches the rulebook read, which is
	// otherwise deferred to the first frame and would overwrite this.
	//
	// Cleared by `ResetState`, so an override belongs to one course and never leaks into the next.
	bool SetJumpRuleOverride(const TCHAR* Key, float Value);

	// The live tuning, for a harness that has to record what a run was made under — the gym is
	// derived from these values, so a recording that does not say which ones it used cannot say
	// what changed when it disagrees with its baseline.
	const FElysiumMoveTuning& GetTuning() const { return Tuning; }

	// The frame's settled body state, written once at the tick tail. This is the *published*
	// half of the mover: every scalar getter above answers about the mover's own bookkeeping, while
	// this answers about the body, in the vocabulary an animation graph steers on.
	//
	// It is stored rather than derived on demand because it carries the wish direction, which is a
	// property of the command that was *integrated*. Derived on demand it would change the instant
	// `SetUserCmd` lands for the next frame, reporting a body that has not moved yet.
	const FElysiumLocomotionSample& GetLocomotionSample() const { return LastSample; }

private:
	// One integration step of the pending command. `TickComponent` is a thin driver over this: it
	// bounds the delta, asks the stepper how to chop it, and calls this N times. Both timestep
	// modes run this same body.
	void PlayerMove(float DeltaTime);

	// Source's own names. Each is the shape of the decompiled function; the arithmetic each one
	// runs is `ElysiumMove`'s, so what is left here is the world half — the traces and the state.
	void CategorizePosition();
	void CheckJumpButton();

	// `CPlayerMove::SetupMove`'s one recovered behaviour: while the body is animation-driven, zero
	// the command's `forwardmove`/`sidemove`/`upmove` and refill them from the window of the playing
	// sequence this frame will advance through. Runs once per step, ahead of every move function, so
	// the walk, the air move and the water move all read the same substituted command retail's do.
	void SetupMove(float DeltaTime);

	// `FullWalkMove` — the gravity half-step split lives here, not in the caller.
	void FullWalkMove(float DeltaTime);
	void AirMove(float DeltaTime);
	void WaterMove(float DeltaTime);

	// `ReduceTimers` + `Duck`. The duck runs **before** CategorizePosition so the ground trace uses
	// the hull the rest of the step will use.
	void ReduceTimers(float DeltaTime);
	void Duck();
	void FinishDuck();
	void FinishUnDuck();

	// `sv_jump_boost` — the instant origin pop on the jump's first frame, swept so a ceiling caps it.
	void ApplyJumpBoost();
	// Close the hold window and restore full gravity. Called on release, on expiry and on landing.
	void EndJumpHold();

	// The two-attempt move: flat, then raised, keeping whichever covered more ground. Nothing
	// detects a stair — that is the whole trick.
	void WalkMove(float DeltaTime);
	// One slide-and-clip pass; returns the position reached.
	void TryPlayerMove(const FVector& Delta);
	void NoclipMove(float DeltaTime);

	// The frame the wish is built in and the body is measured against: the controller's control
	// rotation, falling back to the actor's when there is no controller. One expression, because a
	// sample whose facing came from one source and whose `move_yaw` came from another is two frames
	// wearing one name.
	FRotator ViewFrame() const;

	// Wish direction in the controller's yaw frame (or the view frame while noclipping or swimming).
	// **Every wish the solve uses comes through here**, and it captures what it answered — so the
	// published sample carries the wish the last substep actually moved on rather than a
	// reconstruction of it, which is what stops the water branch (pitch included) from disagreeing
	// with a planar re-derivation.
	FVector WishDirection(const FElysiumUserCmd& Cmd, float& OutScale, bool bForcePitch = false);

	// **The speed authority's one seam.** What the body commands in `WishDir`, cm/s, already
	// scaled by the command's own deflection.
	//
	// Retail has no scalar gait speed to read: it publishes a per-direction table and the client
	// writes one cell's absolute speed into the move command, so "how fast is a walk" is only
	// answerable with a direction (`docs/vtmb/source_movement.md` → "Player speed is
	// animation-driven"). `GetMaxSpeed()` is the ceiling over those cells — retail's `m_flMaxspeed` —
	// and nothing inside the solve reads it.
	//
	// Airborne it answers the **last grounded** wish speed rather than `sv_jump_maxspeed`, because
	// retail stops refreshing its tables for the whole jump while the client keeps writing the last
	// grounded cell; 350 is a clamp that never fires at default settings.
	float WishSpeed(const FVector& WishDir, float Scale) const;

	// The body half of the speed input — everything true of the body rather than of the command.
	// `WishSpeed` completes it with a direction and a deflection to pick a cell; `GetMaxSpeed` asks
	// for the ceiling over those cells and needs no more than this. One builder, so the cell and the
	// ceiling can never be answered about two different bodies.
	FElysiumWishSpeedInput BodySpeedInput() const;

	// Which gait the command selects, for `WishSpeed` and for a readout. `+speed` picks the *slow*
	// gait and a ducked body is always sneaking, which is retail's ladder minus the speed test the
	// animation classifier owns.
	const FElysiumGaitSpeedTable& GaitTableForCommand() const;

	// Fill `LastSample` from the state the frame just settled on. `bSolved` says whether a move
	// function ran this frame; when it did not, the captured wish belongs to an older frame and is
	// reported as absent rather than as current.
	void PublishLocomotionSample(bool bSolved);

	// The live tuning. Defaults are VtMB's own compiled-in ConVar defaults, so an install with no
	// `cfg/` on disk moves exactly like a stock one.
	FElysiumMoveTuning Tuning;

	FElysiumUserCmd PendingCmd;
	FElysiumUserCmd PrevCmd;

	// Source's `m_nOldButtons`: the latch a press is consumed into, so a held button does not
	// re-fire — and so one press cannot fire once per sub-step under a fixed timestep.
	uint64 OldButtons = 0;

	// How many times a press edge became a jump. Counted at the edge itself rather than derived
	// from the trace, because a refused jump and a body that was already falling look identical.
	int32 JumpsTaken = 0;

	bool bOnGround = false;
	bool bNoclip = false;
	bool bFrozen = false;

	// The animation's per-direction speeds for this body, or invalid tables when nothing pushed any.
	FElysiumGaitSpeeds GaitSpeeds;
	// Whether any body ever published the tables above, which is what separates "this body authored
	// no fan for that gait" (the resolver reported it, and zero is retail's own answer) from "no body
	// ever answered at all" (ours, and nobody else's to report).
	bool bGaitSpeedsPublished = false;
	// The one-shot latch for that report. `WishSpeed` is const because asking a table a question
	// changes nothing; the latch is what keeps the answer from being a log line per frame.
	mutable bool bReportedNoGaitAuthority = false;
	// The substituted command's own two refusals, said once per body for the same reason: a swing
	// reaches `SetupMove` on every step it owns, so an unguarded line runs at the frame rate.
	bool bReportedNoClampCeiling = false;
	bool bReportedClampedLunge = false;

	// The pushed swing, and what `SetupMove` made of it this step. `SubstitutedWish` is a WORLD
	// velocity in cm/s — the authored displacement over the step, divided by the step — so the
	// direction and the magnitude the move functions read come from one expression rather than two.
	FElysiumAnimMovementLock AnimLock;
	bool bSubstitutedMove = false;
	FVector SubstitutedWish = FVector::ZeroVector;

	// The wish speed the last grounded substep commanded, cm/s. Retail's tables stop refreshing for
	// the duration of a jump, so this is what an airborne body keeps commanding — the held value is
	// the behaviour, not a cache.
	float LastGroundedWishSpeed = 0.0f;

	// This frame's commanded speed, published for the animation classifier's `cmdMoveMag` term.
	float CommandedSpeed = 0.0f;

	// The duck. `bDucking` is the transition, `bDucked` means the hull is already the small one —
	// Source carries both, and the pair is what lets a duck be released mid-transition.
	bool bDucking = false;
	bool bDucked = false;
	// **The retained crouch request — the action, not the key and not the pose.** `IN_DUCK`'s press
	// edge decides it and its release does nothing, so the crouch outlives the keypress the way an
	// owner test against retail says it does: CTRL ducks, and a jump taken from a crouch lands still
	// crouched.
	//
	// **Both edges are keyed on the hull rather than on this value**, which is what retail does with
	// `FL_DUCKING` (`CGameMovement::Duck`, `vampire.dll 0x10126fd0`): a press raises it only when the
	// hull is standing, and lowers it only when the hull is ducked *and* `CanUnduck` answers. So a
	// press under a low ceiling is **swallowed, not queued** — the body leaves the vent still
	// crouched and the player presses again. That also means this cannot fall while a lowering ramp
	// is in flight, so a release mid-lower runs the ramp to completion instead of entering a
	// stand-up with a standing hull.
	bool bDuckRequested = false;
	// Counts **down** in milliseconds from GameMovementDuckTime, exactly as `m_flDucktime` does.
	float DuckTime = 0.0f;

	// How deep the body is in water. Nothing sets this yet — no exported map places a water brush —
	// so the water branch is live in the state machine and unreachable from content.
	EElysiumWaterLevel WaterLevel = EElysiumWaterLevel::None;

	// How much of the jump's push window is left, seconds. Non-zero means the button is still
	// doing work — VtMB's jump is a held push, not a single impulse.
	float JumpHoldRemaining = 0.0f;

	// The player's own gravity scale (`m_flGravity`, `player+0x3ec`), which `Start`/`FinishGravity`
	// multiply by. `JumpGravityMultiplier` for the duration of a jump, 1.0 otherwise.
	float GravityScale = 1.0f;

	// The rulebook is read once; `rules.txt` is not hot-reloaded.
	bool bJumpTuningLoaded = false;

	// 1.0 on every world surface, which is what retail computes: VtMB scales the material's friction
	// by 1.25 and clamps to 1.0, and 1 of the install's 11,624 VMTs carries a `$surfaceprop`, so
	// everything resolves to the `default` prop at 0.8 (`docs/vtmb/source_movement.md`).
	float SurfaceFriction = 1.0f;

	// The body state this mover publishes. Written at the tick tail; read by everything that
	// wants the body rather than the mover.
	FElysiumLocomotionSample LastSample;

	// What the last `WishDirection` answered, captured at the one point every move function asks.
	FVector CapturedWish = FVector::ZeroVector;
	float CapturedWishScale = 0.0f;
};
