#pragma once

#include "CoreMinimal.h"

// mdl_gltf.py writes standard right-handed Y-up glTF. glTFRuntime's default Unreal import maps a
// Source model's ground plane into (Source Y, Source X), so a skeletal component needs one fixed
// -90 degree yaw before the reflected Source entity yaw. Keeping that offset here makes ordinary
// NPCs, scene understudies/controllers, and the pawn-attached player surface share one convention.
namespace ElysiumSkeletalBasis
{
	inline constexpr float ModelYawOffset = -90.0f;

	inline FRotator FromSourceAngles(const FVector& SourceAngles)
	{
		return FRotator(0.0f, ModelYawOffset - SourceAngles.Y, 0.0f);
	}

	// A body parented under an actor that already carries the facing yaw takes NO offset: the model's
	// authored forward is its component's own +X, and the offset above belongs to a placement whose
	// component rotation *is* its world rotation. Every `AElysiumNpcBody::CharacterMesh0` sits at
	// identity for this reason, with the yaw on the character actor; the pawn-attached player surface
	// is the same case, because `AElysiumPawn` sets `bUseControllerRotationYaw`.
	inline FRotator RelativeToParentFacing()
	{
		return FRotator::ZeroRotator;
	}

	inline FRotator FromUnrealYaw(float UnrealYaw)
	{
		return FRotator(0.0f, UnrealYaw + ModelYawOffset, 0.0f);
	}

	// The same correction as a model-local quaternion, for a placement that is already a full
	// rotation rather than a Source yaw. Compose it AFTER the placement — World = Placement *
	// ModelFix — so the fix stays in the model's own frame and the placement keeps its pitch and
	// roll. `FromPlacementQuat(source_angles_to_unreal_quat(A))` equals `FromSourceAngles(A)`
	// whenever pitch and roll are zero, and beats it when they are not: 15 of the corpus's
	// animated-prop placements are leaning palms whose lean the yaw-only form would discard.
	inline FQuat ModelFix()
	{
		return FQuat(FRotator(0.0f, ModelYawOffset, 0.0f));
	}

	inline FQuat FromPlacementQuat(const FQuat& Placement)
	{
		return Placement * ModelFix();
	}
}
