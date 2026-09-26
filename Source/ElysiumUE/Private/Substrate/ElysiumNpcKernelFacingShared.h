#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcKernelFacing.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelFacingShared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumSkeletalBasis.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"

namespace NpcKernelFacingShared
{
	// `UTIL_AngleDiff` `0x1013d580`: `a - b` walked back into `[-180, 180]` by whole turns, with
	// `_DAT_10462948 = -180.0f` and `_DAT_1044c3a8 = 180.0f`. Retail wraps only on the side the
	// `a <= b` test selects, and reproducing that is free.
	inline float FacingRetailAngleDiff(float A, float B)
	{
		float Delta = A - B;
		if (A <= B)
		{
			while (Delta < -180.0f)
			{
				Delta += 360.0f;
			}
		}
		else
		{
			while (Delta > 180.0f)
			{
				Delta -= 360.0f;
			}
		}
		return Delta;
	}
	// `UTIL_VecToYaw` (`0x101d2c70`, reached through `0x1027db80`) over a delta in THIS world's
	// axes. `0x1027db80` answers the body's current angles for a zero vector rather than 0, so the
	// caller's `AngleDiff` against that same yaw lands on 0 — which is the arm reproduced here.
	inline float RetailYawOf(const FVector& PortDelta, float ZeroVectorYaw)
	{
		if (PortDelta.X == 0.0 && PortDelta.Y == 0.0 && PortDelta.Z == 0.0)
		{
			return ZeroVectorYaw;
		}
		return FMath::RadiansToDegrees(
			static_cast<float>(FMath::Atan2(-PortDelta.Y, PortDelta.X)));
	}
	// `VectorAngles` `0x10139970`: Source `[pitch yaw roll]` for a direction in this world's axes.
	inline FVector FacingRetailVectorAngles(const FVector& PortDir)
	{
		const double Y = -PortDir.Y;   // back into Source's Y
		if (PortDir.X == 0.0 && Y == 0.0)
		{
			// Straight up is pitch 270 and straight down 90, which is `VectorAngles`' own
			// degenerate arm and the opposite of what the sign of Z suggests.
			return FVector(PortDir.Z > 0.0 ? 270.0 : 90.0, 0.0, 0.0);
		}
		float Yaw = FMath::RadiansToDegrees(static_cast<float>(FMath::Atan2(Y, PortDir.X)));
		if (Yaw < 0.0f)
		{
			Yaw += 360.0f;
		}
		const double Flat = FMath::Sqrt(PortDir.X * PortDir.X + Y * Y);
		float Pitch = FMath::RadiansToDegrees(static_cast<float>(FMath::Atan2(-PortDir.Z, Flat)));
		if (Pitch < 0.0f)
		{
			Pitch += 360.0f;
		}
		return FVector(Pitch, Yaw, 0.0);
	}
}
