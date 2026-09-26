#pragma once

// Story 5 step 4: the file-scope locals of `ElysiumNpcKernelPositions.cpp` that its staying bodies share with
// bodies moved to their species classes. Qualified at every use (`NpcKernelPositionsShared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"

namespace NpcKernelPositionsShared
{
	// Retail's `.rdata`, one line per constant, in Source units unless noted.
	inline constexpr float RetailOne = ElysiumNpcTunables::One;
	inline constexpr float RetailZero = ElysiumNpcTunables::Zero;
	// Retail's hint-type words. Named here once so the selectors below read as retail reads.
	inline constexpr int32 HintTeleport17000 = 17000;
	inline constexpr int32 HintJumpbase = 0x4652;
	inline constexpr int32 HintLedge = 0x4653;
	// `UTIL_AngleDiff` `0x1013d580` and `VectorAngles` `0x10139970`, the two angle routines every
	// scored selector here runs. Family Facing (`ElysiumNpcKernelFacing.cpp`) carries an identical
	// pair in its own anonymous namespace and neither is exported; the duplication is stated rather
	// than resolved, because factoring a shared header out of another family's file mid-wave is a
	// bigger edit than the two functions are worth.
	inline float RetailAngleDiff(float A, float B)
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
	// Source `[pitch yaw roll]` for a direction in THIS world's axes; `bsp.source_to_unreal` has
	// negated Y, so a yaw derived from a port-space delta is the negated Unreal one.
	inline FVector RetailVectorAngles(const FVector& PortDir)
	{
		const double Y = -PortDir.Y;
		if (PortDir.X == 0.0 && Y == 0.0)
		{
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
	// The flat (XY) distance retail computes as `sqrt(dx*dx + dy*dy)` through `FUN_101371d0`.
	inline float FlatDistance(const FVector& A, const FVector& B)
	{
		const double Dx = A.X - B.X;
		const double Dy = A.Y - B.Y;
		return static_cast<float>(FMath::Sqrt(Dx * Dx + Dy * Dy));
	}
	// The yaw of the flat delta `From - To`, as `VectorAngles(Vector(dx, dy, 0))` answers it.
	inline float FlatYaw(const FVector& From, const FVector& To)
	{
		const FVector Flat(From.X - To.X, From.Y - To.Y, 0.0);
		return static_cast<float>(RetailVectorAngles(Flat).Y);
	}
	// One Source unit in centimetres, at the point of use so the recovered constant stays visible.
	inline constexpr float U = ElysiumMove::U;
}
