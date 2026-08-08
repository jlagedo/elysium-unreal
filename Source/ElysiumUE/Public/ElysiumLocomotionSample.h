#pragma once

#include "CoreMinimal.h"

// What a moving body publishes about itself, once per frame (CCC1).
//
// One struct, two producers: the player's mover fills it at its tick tail and the NPC motor fills it
// from its own movement, so the cast's locomotion and the player's cannot become two systems that
// happen to play the same files. Everything downstream — the animation intent, the resolver, the
// graph's parameters — reads this rather than reaching into a mover, a controller or an entity
// (`docs/architecture/animation-architecture.md` §3.2).
//
// It is a POD with no UObject and no engine gameplay type, so it is asserted with no world
// (`Elysium.Substrate.Locomotion`), the same split `ElysiumMoveSolve.h` and `ElysiumCameraSolve.h`
// use. Nothing here traces: a value that needs the world is a value a per-frame sample cannot carry.

// How deep the body is in water (`player+0x3e0`). The move branches on Waist and above. It lives
// here rather than beside the move math because it is body *state* that both producers report, not
// a rule the solve applies.
enum class EElysiumWaterLevel : uint8
{
	None  = 0,
	Feet  = 1,
	Waist = 2,
	Eyes  = 3,
};

// The hull the body is standing in. Source carries the settled state and the transition as two
// independent flags, and **all four combinations are reachable**, so the sample carries four values
// rather than three. Collapsing the two ramps onto one would lose the state a stand-up animation
// keys off — and it is not a transient: a body that released the duck under a low ceiling stays in
// `Rising` indefinitely, because the unduck is gated on headroom as well as on time.
enum class EElysiumStance : uint8
{
	Standing,   // bDucked 0, bDucking 0
	Lowering,   // bDucked 0, bDucking 1 — the duck ramp; the hull is still the standing one
	Ducked,     // bDucked 1, bDucking 0
	Rising,     // bDucked 1, bDucking 1 — the unduck ramp, waiting on time and on headroom
};

enum class EElysiumJumpPhase : uint8
{
	Grounded,
	Ascend,
	Descend,
};

struct FElysiumLocomotionSample
{
	// Velocity in the body's own facing frame, cm/s: X forward, Y right. The frame is a yaw-only
	// rotation, so Z is world vertical unchanged — which is what makes it the jump's own axis.
	FVector LocalVelocity = FVector::ZeroVector;

	// Where the body faces, world degrees. The player's is the controller's yaw; an NPC's is its
	// actor yaw.
	float FacingYaw = 0.0f;

	// The two candidate movement yaws, both facing-relative and both in (-180, 180]. **Both are
	// recorded from the first day on purpose**: `CCC7` recovers the sign of `move_yaw` by comparing
	// the retail selector's own input against these, and that comparison should run against
	// recordings rather than against fresh instrumentation.
	//
	// `MoveYawWish` is the direction the command asked for, `MoveYawVelocity` the direction the body
	// actually went. They differ whenever the body is not yet at speed, is sliding along a wall, or
	// is being decelerated by friction.
	float MoveYawWish = 0.0f;
	float MoveYawVelocity = 0.0f;

	// The wish direction's magnitude, 0..1. Zero means there was no commanded direction at all, and
	// `MoveYawWish` is then a placeholder rather than a measurement — a consumer that cannot tell
	// those apart reads "walk forward" out of a body standing still.
	float WishScale = 0.0f;

	bool bOnGround = false;
	EElysiumWaterLevel Water = EElysiumWaterLevel::None;
	EElysiumStance Stance = EElysiumStance::Standing;

	// How much of the jump's push window is left, seconds. Non-zero means the button is still doing
	// work — VtMB's jump is a held push, not a single impulse — which is what makes the ascend phase
	// derivable without a second flag.
	float JumpHoldRemaining = 0.0f;

	// Horizontal speed, cm/s. Derived rather than stored: a yaw rotation preserves length, so a
	// stored copy would carry no information `LocalVelocity` does not — only the ability to disagree
	// with it.
	float Speed2D() const
	{
		return static_cast<float>(FVector2D(LocalVelocity.X, LocalVelocity.Y).Size());
	}

	EElysiumJumpPhase JumpPhase() const
	{
		if (bOnGround)
		{
			return EElysiumJumpPhase::Grounded;
		}
		return (JumpHoldRemaining > 0.0f || LocalVelocity.Z > 0.0f)
			? EElysiumJumpPhase::Ascend : EElysiumJumpPhase::Descend;
	}

	// **Provisional, and owned by `CCC7`.** Which of the two yaws the graph steers on — and the sign
	// convention it steers in — is read off the retail selector at `0x10164870` rather than chosen,
	// and until it is, this answers with the realized direction because that is what a strafing body
	// visibly does. Both raw yaws stay on the struct so that rung can settle it here and nowhere
	// else.
	float MoveYaw() const { return MoveYawVelocity; }
};

namespace ElysiumLocomotion
{
	// A world yaw expressed against a facing, normalized to (-180, 180]. Positive is Unreal's own
	// yaw direction, so a body strafing right reads positive.
	float RelativeYaw(float WorldYaw, float FacingYaw);

	// Source's two independent duck flags as the one stance they describe. A free function because
	// the mapping is the whole of the rule and both producers have to agree on it.
	inline EElysiumStance StanceFrom(bool bDucked, bool bDucking)
	{
		return bDucked
			? (bDucking ? EElysiumStance::Rising : EElysiumStance::Ducked)
			: (bDucking ? EElysiumStance::Lowering : EElysiumStance::Standing);
	}

	// The sample as stock `UCharacterMovementComponent` can answer it. Both `AElysiumCapsulePawn`
	// (the `elysium.SourceMovement 0` A/B baseline) and `AElysiumNpcBody` are an `ACharacter` over
	// that component, so one function serves the A/B body and the whole cast.
	//
	// `FacingYawDegrees` is the caller's, not the actor's, because the two producers disagree about
	// what facing means: a player body faces where the view points, an NPC where its actor is turned.
	// Asking for it is what keeps `move_yaw` measured against the same reference on both.
	//
	// Two things it cannot fill, and they read as their defaults rather than as guesses: no water
	// level, and no jump hold window — CharacterMovement's jump is an impulse, not VtMB's held push,
	// so its airborne phase is the vertical sign alone. Its crouch is a settled state with no ramp,
	// so the stance it reports is only ever Standing or Ducked.
	FElysiumLocomotionSample FromCharacterMovement(const class ACharacter& Body,
		float FacingYawDegrees);
}
