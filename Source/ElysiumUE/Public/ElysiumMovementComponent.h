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
// 0.7 standable test, so *every* climb is rejected (`source_movement.md` § StepMove). So the body is
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

	// Freeze the body where it stands (the spawn hold, while collision cooks).
	void SetFrozen(bool bFrozen);
	bool IsFrozen() const { return bFrozen; }

	bool IsOnGround() const { return bOnGround; }

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
	// Would the standing hull fit where we are? What stops a stand-up under a low ceiling.
	bool CanUnduck() const;

	// Sweep the current hull between two points. The mover's one world dependency.
	bool TraceHull(const FVector& Start, const FVector& End, FHitResult& OutHit) const;
	// The two-attempt move: flat, then raised, keeping whichever covered more ground. Nothing
	// detects a stair — that is the whole trick.
	void WalkMove(float DeltaTime);
	// One slide-and-clip pass; returns the position reached.
	void TryPlayerMove(const FVector& Delta);
	void NoclipMove(float DeltaTime);

	// Wish direction in the controller's yaw frame (or the view frame while noclipping).
	FVector WishDirection(const FElysiumUserCmd& Cmd, float& OutScale) const;

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

	bool bOnGround = false;
	bool bNoclip = false;
	bool bFrozen = false;

	// The duck. `bDucking` is the transition, `bDucked` means the hull is already the small one —
	// Source carries both, and the pair is what lets a duck be released mid-transition.
	bool bDucking = false;
	bool bDucked = false;
	// Counts **down** in milliseconds from GameMovementDuckTime, exactly as `m_flDucktime` does.
	float DuckTime = 0.0f;

	// How deep the body is in water. Nothing sets this yet — no exported map places a water brush —
	// so the water branch is live in the state machine and unreachable from content.
	EElysiumWaterLevel WaterLevel = EElysiumWaterLevel::None;

	// 1.0 on every world surface, which is what retail computes: VtMB scales the material's friction
	// by 1.25 and clamps to 1.0, and 1 of the install's 11,624 VMTs carries a `$surfaceprop`, so
	// everything resolves to the `default` prop at 0.8 (`docs/source_movement.md`).
	float SurfaceFriction = 1.0f;
};
