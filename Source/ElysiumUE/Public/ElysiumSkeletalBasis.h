#pragma once

#include "CoreMinimal.h"

// Where a skeletal body's authored forward points, so a placement is stated once instead of
// re-derived beside every factory.
//
// There is ONE representation. Every skeletal body — a character, a scene understudy, the
// pawn-attached player surface, an animated prop — comes off the baked mount, built from the
// `.eskm` `UE_mdl_skeletal.py` writes in the repo's canonical Source->Unreal frame. So a body's
// authored forward is its own component +X, and a placement is the reflected Source yaw with
// nothing added.
namespace ElysiumSkeletalBasis
{
	// Source `angles` is [pitch yaw roll]; a standing body needs yaw only, reflected because
	// `bsp.source_to_unreal` negates Y.
	inline FRotator FromSourceAngles(const FVector& SourceAngles)
	{
		return FRotator(0.0f, -SourceAngles.Y, 0.0f);
	}

	inline FRotator FromUnrealYaw(float UnrealYaw)
	{
		return FRotator(0.0f, UnrealYaw, 0.0f);
	}

	// A body parented under an actor that already carries the facing yaw stands at identity: the
	// model's authored forward is its component's own +X, so the parent's yaw is the whole answer.
	// Every `AElysiumNpcBody` body sits here, with the yaw on the character actor, and so does the
	// pawn-attached player surface because `AElysiumPawn` sets `bUseControllerRotationYaw`.
	inline FRotator RelativeToParentFacing()
	{
		return FRotator::ZeroRotator;
	}

	// A placement that is already a full rotation — an entity's exported `model_quat` — is used
	// verbatim, which is why there is no entry point for one here. It carries pitch and roll as
	// well as yaw, and 15 of the corpus's animated-prop placements are leaning palms whose lean a
	// yaw-only rederivation would discard.
}
