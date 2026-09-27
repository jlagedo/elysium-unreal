// `CAI_BaseNPC`'s bodies of the `Species` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseSpecies.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// The pooled zeroes (`_DAT_104454c4`, and the double `_DAT_1044fab0`) and one.
	constexpr float SpeciesZero = ElysiumNpcTunables::Zero;
	// Slot 323 (`0x10344dd0`). The direction's 2-D length must clear this before an angle is taken
	// at all — a hair over zero, so the too-slow arm is only a genuinely stationary direction.
	constexpr float MoveDirectionMinLength2D = 1.0e-07f;    // _DAT_1049e028
	// Slot 323's `UTIL_AngleMod`: `(360/65536) * (ftol(a * 65536/360) & 0xffff)`.
	constexpr float AngleModScale = ElysiumNpcTunables::AngleQuantum;   // 360 / 65536
	constexpr float GSpeciesDegreesPerTurn = 360.0f;                // _DAT_10450568
	// Slot 323's four band boundaries. **316, not 315** — read out of the image; the bands are 89,
	// 90, 90 and 91 degrees wide and that asymmetry is retail's.
	constexpr float MoveDirectionAheadBand = ElysiumNpcTunables::FortyFive;
	constexpr float MoveDirectionLeftBand = 135.0f;         // _DAT_1049e8a0
	constexpr float MoveDirectionBehindBand = 225.0f;       // _DAT_1049e89c
	constexpr float MoveDirectionRightBand = 316.0f;        // _DAT_1049e8a4
	// `UTIL_VecToYaw` `0x101d2c70` over a delta in THIS world's axes, whose Y is the negated Source
	// one (`bsp.source_to_unreal`). Families Bosses, Facing and Positions each keep an identical
	// private copy; this is the fourth, for the same file-ownership reason.
	float SpeciesVecToYaw(const FVector& PortDelta)
	{
		if (PortDelta.X == 0.0 && PortDelta.Y == 0.0)
		{
			return SpeciesZero;
		}
		float Yaw = FMath::RadiansToDegrees(
			static_cast<float>(FMath::Atan2(-PortDelta.Y, PortDelta.X)));
		if (Yaw < SpeciesZero)
		{
			Yaw += GSpeciesDegreesPerTurn;
		}
		return Yaw;
	}
	// `UTIL_AngleMod` — the `ftol`/mask/scale triple slot 323 folds its relative yaw through.
	// Reproduced as the INTEGER pipeline retail runs, not as an `fmod`: the truncation toward zero
	// and the 16-bit mask are what make 360.0 come back as 0.0 and a negative angle come back
	// positive, and an `fmod` would answer differently at the boundaries this body compares on.
	float SpeciesAngleMod(float Degrees)
	{
		const int32 Fixed = static_cast<int32>(Degrees * (1.0f / AngleModScale));
		float Modded = static_cast<float>(static_cast<uint32>(Fixed) & 0xffffu) * AngleModScale;
		// `if (fVar1 < _DAT_104454c4) fVar1 = fVar1 + _DAT_10450568;` — dead after the mask, which
		// cannot produce a negative, and reproduced because retail carries it.
		if (Modded < SpeciesZero)
		{
			Modded += GSpeciesDegreesPerTurn;
		}
		return Modded;
	}
}

// --- Moved from `ElysiumNpcSpecies.cpp` (story 5 step 5) ---

// -------------------------------------------------------------------------------------------------
// Slot 323 — `CAI_BaseNPC::FUN_10344dd0` `0x10344dd0`. 78 classes share the one body.
// -------------------------------------------------------------------------------------------------

int32 FElysiumNpcBase::Slot323(const FVector& DirectionCm)
{
	// `0x10344dd0`, arm by arm:
	//
	//     len2d = sqrt(dir.x*dir.x + dir.y*dir.y);          // PTR_thunk_FUN_101371d0
	//     if (_DAT_1049e028 (1e-07) <= len2d) {
	//         dir.z = 0;
	//         a = UTIL_AngleMod( VecToYaw(dir) - GetAbsAngles().y );   // 0x101d2c70, slot 219
	//         if (316.0 < a || a <= 45.0)   return 2;
	//         if (a <= 135.0)               return 3;
	//         if (135.0 < a && a <= 225.0)  return 0;
	//         if (225.0 < a)                return 1;
	//     }
	//     return 0;
	//
	// The decompiler lost the subtraction between `VecToYaw` and `GetAbsAngles` — both calls are
	// there with their results dropped on the FPU stack and a bare `__ftol()` after them, which is
	// the `UTIL_AngleMod` prologue. The reading is settled by the arithmetic that DID survive: the
	// `& 0xffff` and the `* 0.0054931640625` are `UTIL_AngleMod`'s, and a body that bucketed an
	// ABSOLUTE yaw into four fixed bands would answer the same code for the same world direction no
	// matter which way the NPC faced, which is not a direction code. **Unrecovered:** nothing else;
	// the operand order (`yaw - facing`, not `facing - yaw`) is the one that makes band 2 the body's
	// own heading, and band 2 is the band the recovered `45.0` centres on zero.
	//
	// `DirectionCm` is this runtime's axes; `SpeciesVecToYaw` negates Y for Source's, exactly as the
	// three other copies of `UTIL_VecToYaw` in the kernel do. The Z flatten is retail's and is done
	// before the yaw rather than after, which changes nothing and is kept in retail's order.
	const double Length2D = FMath::Sqrt(
		DirectionCm.X * DirectionCm.X + DirectionCm.Y * DirectionCm.Y);
	if (static_cast<float>(Length2D) < MoveDirectionMinLength2D)
	{
		return static_cast<int32>(EMoveDirectionCode::Behind);   // retail's `0`
	}
	const FVector Flat(DirectionCm.X, DirectionCm.Y, 0.0);
	const float Relative = SpeciesAngleMod(
		SpeciesVecToYaw(Flat) - static_cast<float>(Angles.Y));

	if (MoveDirectionRightBand < Relative || Relative <= MoveDirectionAheadBand)
	{
		return static_cast<int32>(EMoveDirectionCode::Ahead);
	}
	if (Relative <= MoveDirectionLeftBand)
	{
		return static_cast<int32>(EMoveDirectionCode::Left);
	}
	if (MoveDirectionLeftBand < Relative && Relative <= MoveDirectionBehindBand)
	{
		return static_cast<int32>(EMoveDirectionCode::Behind);
	}
	if (MoveDirectionBehindBand < Relative)
	{
		return static_cast<int32>(EMoveDirectionCode::Right);
	}
	// Unreachable — the four bands cover [0, 360). Retail carries the fall-through and so does this.
	return static_cast<int32>(EMoveDirectionCode::Behind);
}
