#pragma once

#include "CoreMinimal.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumUserCmd.h"
#include "GameFramework/PawnMovementComponent.h"

#include "ElysiumMovementComponent.generated.h"

// The player's mover (roadmap 11.6; the faithful `CGameMovement` port is **4.7**).
//
// It exists because `ACharacter` cannot take a box root and Source's `StepMove` depends on the hull
// being an AABB: a capsule's rounded bottom catches a step's top edge and reports ~0.65 against the
// 0.7 standable test, so *every* climb is rejected (`docs/vtmb/source_movement.md` § StepMove). So the body is
// an `APawn` with a `UBoxComponent` and this replaces `UCharacterMovementComponent` wholesale.
//
// The structure is Source's and the constants are RE'd; the math itself lives in
// `ElysiumMoveSolve.h` so it is asserted without a world. What 4.7 still owns here is the state
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
	// step 5 of the frame, after the substrate's thinks have moved the doors (11.1).
	void SetUserCmd(const FElysiumUserCmd& InCmd) { PendingCmd = InCmd; }

	void SetNoclip(bool bEnable);
	bool IsNoclip() const { return bNoclip; }

	// Drop all carried motion and latches: velocity, the button latch, the duck, the step
	// accumulator. What a teleport wants — a body arriving somewhere new must not still be running.
	void ResetState();

	// Freeze the body where it stands (the spawn hold, while collision cooks).
	void SetFrozen(bool bFrozen);
	bool IsFrozen() const { return bFrozen; }

	bool IsOnGround() const { return bOnGround; }
	// Press-edge jumps taken since the last `ResetState`. The leniency courses' measurement: a jump
	// refused for being airborne never increments it, so this is what the gym's `ledge_*`/`land_*`
	// brackets read (CCC3).
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

	// The frame's settled body state (CCC1), written once at the tick tail. This is the *published*
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

	// Fill `LastSample` from the state the frame just settled on. `bSolved` says whether a move
	// function ran this frame; when it did not, the captured wish belongs to an older frame and is
	// reported as absent rather than as current.
	void PublishLocomotionSample(bool bSolved);

	// The live tuning. Defaults are VtMB's own compiled-in ConVar defaults, so an install with no
	// `cfg/` on disk moves exactly like a stock one.
	FElysiumMoveTuning Tuning;

	// How the frame's delta is chopped. Driven from `elysium.move.FixedStep`, default 0 = faithful.
	FElysiumMoveStepper Stepper;

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

	// The duck. `bDucking` is the transition, `bDucked` means the hull is already the small one —
	// Source carries both, and the pair is what lets a duck be released mid-transition.
	bool bDucking = false;
	bool bDucked = false;
	// **The retained crouch request — the action, not the key and not the pose.** `IN_DUCK`'s press
	// edge flips this and its release does nothing, so the crouch outlives the keypress the way an
	// owner test against retail says it does: CTRL ducks, and a jump taken from a crouch lands still
	// crouched. It is deliberately separate from `bDucked`, which is what the body actually *is*:
	// `CanUnduck` can refuse a stand-up under a low ceiling, and when it does the request is already
	// false while the hull is still small. Collapsing the two would either lose the refusal or make
	// the ceiling re-duck the player.
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

	// The body state this mover publishes (CCC1). Written at the tick tail; read by everything that
	// wants the body rather than the mover.
	FElysiumLocomotionSample LastSample;

	// What the last `WishDirection` answered, captured at the one point every move function asks.
	FVector CapturedWish = FVector::ZeroVector;
	float CapturedWishScale = 0.0f;
};
