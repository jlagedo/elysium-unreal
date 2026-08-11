#pragma once

#include "CoreMinimal.h"

// Where a skeletal body's authored forward points, per representation, so a placement is stated
// once instead of re-derived beside every factory. There are two representations and they do NOT
// share a basis:
//
// - A CHARACTER comes off the baked mount. `UE_mdl_skeletal.py` writes the `.eskm` the bake reads
//   in the repo's canonical Source->Unreal frame, so the body's authored forward is its own
//   component +X and a placement is the reflected Source yaw with nothing added.
// - An ANIMATED PROP is loaded at runtime from `mdl_gltf.py`'s standard right-handed Y-up glTF,
//   which glTFRuntime reorients into its own basis on import: that lands the model's ground plane
//   on (Source Y, Source X), so its forward is component +Y and every placement takes one fixed
//   -90 degree model-local yaw. Those are the `Glb` entry points below.
//
// Handing a character placement to the `Glb` half — or the reverse — yaws the body a quarter turn.
namespace ElysiumSkeletalBasis
{
	// --- Baked characters -----------------------------------------------------------------------

	// Source `angles` is [pitch yaw roll]; a standing character needs yaw only, reflected because
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

	// --- Runtime glTF bodies (animated props) ---------------------------------------------------

	inline constexpr float GlbModelYawOffset = -90.0f;

	inline FRotator GlbFromSourceAngles(const FVector& SourceAngles)
	{
		return FRotator(0.0f, GlbModelYawOffset - SourceAngles.Y, 0.0f);
	}

	// The same correction as a model-local quaternion, for a placement that is already a full
	// rotation rather than a Source yaw. Compose it AFTER the placement — World = Placement *
	// ModelFix — so the fix stays in the model's own frame and the placement keeps its pitch and
	// roll. `GlbFromPlacementQuat(source_angles_to_unreal_quat(A))` equals `GlbFromSourceAngles(A)`
	// whenever pitch and roll are zero, and beats it when they are not: 15 of the corpus's
	// animated-prop placements are leaning palms whose lean the yaw-only form would discard.
	inline FQuat GlbModelFix()
	{
		return FQuat(FRotator(0.0f, GlbModelYawOffset, 0.0f));
	}

	inline FQuat GlbFromPlacementQuat(const FQuat& Placement)
	{
		return Placement * GlbModelFix();
	}
}
