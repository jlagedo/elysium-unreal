// `CAI_BaseNPC`'s bodies of the `Geometry` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseGeometry.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcGeometryShared.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	// `_DAT_1044bef8` = 0.25f. `BodyTarget`'s anchor drop: the fraction of the centre-to-origin
	// delta subtracted from the bounds centre to get the point the blend starts at.
	constexpr float GBodyTargetAnchorFraction = 0.25f;
	// `BodyTarget`'s two noise draws, `RandomFloat(0, 0.5)` twice (`PUSH 0x3f000000; PUSH 0x0`). The
	// arm adds BOTH, so the blend parameter spans 0..1 with a triangular distribution rather than
	// the uniform 0..0.5 a single draw would give.
	constexpr float GBodyTargetNoiseMin = 0.f;
	constexpr float GBodyTargetNoiseMax = 0.5f;
}

// --- Moved from `ElysiumNpcGeometry.cpp` (story 5 step 5) ---

FVector FElysiumNpcBase::EyePosition() const
{
	// `CAISound::FUN_100b4b40` `0x100b4b40`, 85 bytes, the Troika line's own and the body 21 classes
	// in this family and 24 call sites reach: `GetAbsOrigin()` (slot 217) plus `m_vecViewOffset`
	// (`+0x0184`), component by component. `FElysiumCombatCharacter::EyePosition()` is that sum with
	// the standing view offset as the port's `m_vecViewOffset`. `CPayphone` (`0x101aae60`) overrides
	// this method on its C++ class (story 5 step 3). `CAI_BaseHumanoid` (`0x1025e8e0`) has no
	// instance (its arm was deleted by story 5 step 1); `CBaseCineCam`, `CBasePlayer` and
	// `CItemContainerLock` are not NPC classes.
	return FElysiumCombatCharacter::EyePosition();
}

FVector FElysiumNpcBase::BodyTargetAnchor(const FVector& CentreCm, const FVector& OriginCm)
{
	// `0x102789c0`'s head, from the listing (`102789c6`..`10278a23`): slot 192 `WorldSpaceCenter()`,
	// slot 217 `GetAbsOrigin()`, the delta between them scaled by `_DAT_1044bef8` (0.25), and then
	// slot 192 dispatched a SECOND time and the scaled delta subtracted from THAT.
	//
	// Both dispatches answer the same vector, so the anchor is the bounds centre pulled a quarter of
	// the way back down toward the feet. The second dispatch is not redundant in retail — a class
	// whose `WorldSpaceCenter` reads an animated bound could answer differently between the two —
	// but on every body in this family it is the same point, and it is written once here.
	const FVector Delta = (CentreCm - OriginCm) * GBodyTargetAnchorFraction;
	return CentreCm - Delta;
}

FVector FElysiumNpcBase::BodyTargetBlend(const FVector& AnchorCm, const FVector& EyeCm, bool bNoisy,
	bool bAimAtEyeExactly, float Noise1, float Noise2)
{
	// The three arms, in the listing's order (`10278a84` tests the SECOND bool first).
	const FVector Span = EyeCm - AnchorCm;

	if (bNoisy)
	{
		// `10278a8a`..`10278b32`: TWO independent `RandomFloat(0, 0.5)` draws, and the span is added
		// once scaled by each. `Anchor + Span*(r1 + r2)` — a triangular 0..1 blend whose mode is the
		// midpoint, NOT one uniform 0..0.5 draw. Aim spread on this engine's NPCs is that sum.
		return AnchorCm + Span * Noise1 + Span * Noise2;
	}

	if (bAimAtEyeExactly)
	{
		// `10278b3c`: three word copies straight out of the slot-193 result. The anchor is not
		// consulted at all on this arm.
		return EyeCm;
	}

	// `10278b56`: `Anchor + Span * _DAT_104454d0` — the plain midpoint between the lowered centre
	// and the eye.
	return AnchorCm + Span * NpcKernelGeometryShared::GRetailHalf;
}

FVector FElysiumNpcBase::BodyTarget(const FVector& /*PosSrc*/, bool bNoisy, bool bAimAtEyeExactly)
{
	// `0x102789c0`, 518 bytes. **`posSrc` is never read.** The retail signature takes a
	// `const Vector&` (the caller's own eye point, in Valve's SDK the thing the spread cone is
	// measured from) and the body's 0x10 bytes of stack arguments are the return buffer, that
	// reference, and the two bools — of which only the two bools and `this` reach an instruction.
	// A shipped program was tuned against that, so the parameter stays and stays unread.
	//
	// Slot 192 `WorldSpaceCenter()` is another story's row and is still a generated stub here, so
	// the anchor it feeds is the zero vector until that lands. The wiring is the deliverable; the
	// formula is asserted through `BodyTargetAnchor` / `BodyTargetBlend`.
	const FVector CentreCm = WorldSpaceCenter();
	const FVector AnchorCm = BodyTargetAnchor(CentreCm, Origin);
	const FVector EyeCm = EyePosition();

	float Noise1 = 0.f;
	float Noise2 = 0.f;
	if (bNoisy)
	{
		// `(**(code **)(*DAT_1070b244 + 4))(0, 0x3f000000)` twice. Drawn here rather than inside
		// `BodyTargetBlend` so the blend stays measurable, and drawn BOTH times even though the two
		// products are added, because retail draws twice and a stream's position is observable.
		FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
		Noise1 = Stream.FRandRange(GBodyTargetNoiseMin, GBodyTargetNoiseMax);
		Noise2 = Stream.FRandRange(GBodyTargetNoiseMin, GBodyTargetNoiseMax);
	}
	return BodyTargetBlend(AnchorCm, EyeCm, bNoisy, bAimAtEyeExactly, Noise1, Noise2);
}

uint32 FElysiumNpcBase::DebugOverlayBits() const
{
	// SEAM for slot 513 (vtable `+0x804`). No per-NPC overlay word here; answers 0.
	return 0u;
}
