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

	inline FRotator RelativeToPawn()
	{
		return FRotator(0.0f, ModelYawOffset, 0.0f);
	}

	inline FRotator FromUnrealYaw(float UnrealYaw)
	{
		return FRotator(0.0f, UnrealYaw + ModelYawOffset, 0.0f);
	}
}
