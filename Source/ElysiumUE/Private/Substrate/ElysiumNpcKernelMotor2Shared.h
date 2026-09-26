#pragma once

// Story 5 step 4: the file-scope locals of the former `ElysiumNpcKernelMotor2.cpp`, whose bodies all
// moved to their species classes; the classes that share a local read it here. Qualified at every use (`NpcKernelMotor2Shared::`) because a unity
// build concatenates translation units and an anonymous namespace is not file-local there.

#include "Substrate/ElysiumNpc.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"

namespace NpcKernelMotor2Shared
{
	// Retail's `sqrt`, `PTR_thunk_FUN_101371d0`.
	inline float Length2D(const FVector& A)
	{
		return FMath::Sqrt(static_cast<float>(A.X * A.X + A.Y * A.Y));
	}
	inline float Length3D(const FVector& A)
	{
		return FMath::Sqrt(static_cast<float>(A.X * A.X + A.Y * A.Y + A.Z * A.Z));
	}
	// This world's centimetres into retail's Source units, and back. Stated once per file, for the
	// reason given at the top of `ElysiumNpcKernelMotor.cpp`.
	inline FVector MotorTailSourceOf(const FVector& Cm)
	{
		return FVector(Cm.X / ElysiumMove::U, -Cm.Y / ElysiumMove::U, Cm.Z / ElysiumMove::U);
	}
	inline FVector PortOf(const FVector& Units)
	{
		return FVector(Units.X * ElysiumMove::U, -Units.Y * ElysiumMove::U,
			Units.Z * ElysiumMove::U);
	}
}
