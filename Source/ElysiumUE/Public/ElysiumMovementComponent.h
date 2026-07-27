#pragma once

#include "CoreMinimal.h"
#include "ElysiumUserCmd.h"
#include "GameFramework/PawnMovementComponent.h"

#include "ElysiumMovementComponent.generated.h"

// VtMB's own movement tuning, in **cm** (`docs/source_movement.md` § Movement). Source units are
// converted once, here, and never again — the same rule the exporters follow.
namespace ElysiumMove
{
	inline constexpr float U = 2.54f;                 // one Source unit, in cm

	inline constexpr float Gravity      = 800.0f * U; // sv_gravity
	inline constexpr float Friction     = 4.0f;       // sv_friction (dimensionless)
	inline constexpr float StopSpeed    = 16.0f * U;  // sv_stopspeed
	inline constexpr float Accelerate   = 10.0f;      // sv_accelerate
	inline constexpr float AirAccel     = 10.0f;      // sv_airaccelerate
	inline constexpr float AirSpeedCap  = 30.0f * U;  // hardcoded in vampire.dll (0x104492a8)
	inline constexpr float StepSize     = 18.0f * U;  // sv_stepsize, via m_flStepSize
	inline constexpr float MaxVelocity  = 3500.0f * U;// sv_maxvelocity
	inline constexpr float JumpSpeed    = 200.0f * U; // sqrt(2 * sv_jump_boost * sv_gravity)
	inline constexpr float JumpMaxSpeed = 350.0f * U; // sv_jump_maxspeed, while airborne

	// The retail player speed is animation-driven and no ConVar holds it; `speed_walk` /
	// `speed_runbase` are Troika's stated intent and are registered-but-never-read, which makes them
	// what a port with no player animation should use.
	inline constexpr float WalkSpeed    = 100.0f * U; // speed_walk
	inline constexpr float RunSpeed     = 225.0f * U; // speed_runbase (+5 per Athletics at 9.4)

	// The standable-normal test (a double at 0x104492d0) and the epsilon that keeps a down-trace
	// from arriving flush with the floor and reporting no contact.
	inline constexpr float StandableZ   = 0.7f;
	inline constexpr float DistEpsilon  = 0.08f;      // cm

	inline constexpr float NoclipSpeed  = 1200.0f;    // cm/s — a dev speed, no original
	inline constexpr float NoclipBoost  = 3.0f;
}

// The player's mover (roadmap 11.6; the faithful `CGameMovement` port is **4.7**).
//
// It exists because `ACharacter` cannot take a box root and Source's `StepMove` depends on the hull
// being an AABB: a capsule's rounded bottom catches a step's top edge and reports ~0.65 against the
// 0.7 standable test, so *every* climb is rejected (`source_movement.md` § StepMove). So the body is
// an `APawn` with a `UBoxComponent` and this replaces `UCharacterMovementComponent` wholesale.
//
// The structure is Source's and the constants are RE'd; what 4.7 still owns is the line-by-line
// port — `surfaceFriction` from real surface data, the ducked hull (**RE22**), ladders, water, the
// gravity half-step split, and `CategorizePosition`'s full contract. Everything reads one
// `FElysiumUserCmd` and nothing polls a key (S5).
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
	// Source's own names. Each is the shape of the decompiled function; 4.7 finishes them.
	void CategorizePosition();
	void ApplyFriction(float DeltaTime);
	void ApplyAccelerate(const FVector& WishDir, float WishSpeed, float DeltaTime);
	void ApplyAirAccelerate(const FVector& WishDir, float WishSpeed, float DeltaTime);
	// The two-attempt move: flat, then raised, keeping whichever covered more ground. Nothing
	// detects a stair — that is the whole trick.
	void WalkMove(float DeltaTime);
	// One slide-and-clip pass; returns the position reached.
	void TryPlayerMove(const FVector& Delta);
	void NoclipMove(float DeltaTime);

	// Wish direction in the controller's yaw frame (or the view frame while noclipping).
	FVector WishDirection(const FElysiumUserCmd& Cmd, float& OutScale) const;

	FElysiumUserCmd PendingCmd;
	FElysiumUserCmd PrevCmd;

	bool bOnGround = false;
	bool bNoclip = false;
	bool bFrozen = false;

	// 1.0 on every world surface, which is what retail computes: VtMB scales the material's friction
	// by 1.25 and clamps to 1.0, and 1 of the install's 11,624 VMTs carries a `$surfaceprop`, so
	// everything resolves to the `default` prop at 0.8 (`docs/source_movement.md`).
	float SurfaceFriction = 1.0f;
};
