// `CAI_BaseNPC`'s bodies of the `Positions2` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBasePositions2.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcPositions2Shared.h"

// --- File-scope helpers moved with the base bodies (story 5 step 5) ---

namespace
{
	constexpr double ValidCoverDrop = ElysiumNpcTunables::HundredthDouble;
	// `CAI_BaseNPC::IsUnreachable` `0x102741e0`'s squared-distance threshold, Source units squared.
	constexpr float UnreachableDistSq = ElysiumNpcTunables::UnreachableDistanceSquared;   // 120 units, squared
	// The three trace masks, as retail spells them.
	constexpr int32 MaskValidCover = 0x202400b;     // `IsValidCover`'s hull probe (MASK_NPCSOLID)
}

// --- Moved from `ElysiumNpcPositions2.cpp` (story 5 step 5) ---

// --- Slot 530 `IsUnreachable` `0x102741e0` ------------------------------------------------------

bool FElysiumNpcBase::IsUnreachable(FElysiumEntity* Unreachable)
{
	// The retail body, walking `m_UnreachableEnts` (+0x5d48) BACKWARDS from `count - 1` (+0x5d54):
	//
	//   * a record whose handle no longer resolves is removed and the walk CONTINUES;
	//   * a record naming `Target` answers true only when `curtime <= record.expiry` AND the target
	//     has not moved more than `_DAT_10499560 = 14400` (120 units) squared from where it was
	//     recorded, and is REMOVED and answers false otherwise;
	//   * falling off the end answers false.
	//
	// The removal is a memmove of the LAST record over the one being dropped (`FUN_10430fa0`, 0x14
	// bytes) followed by `--count`, and the loop index then decrements PAST the record that was
	// just moved in — so a record shifted down from the tail is never examined on this pass. That is
	// retail's own behaviour and it is reproduced rather than corrected: a shipped program was tuned
	// against which stale entries survive a sweep.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	for (int32 Index = UnreachableEnts.Num() - 1; Index >= 0; --Index)
	{
		const FElysiumEntity* Stored =
			World != nullptr ? World->Resolve(UnreachableEnts[Index].Entity) : nullptr;
		if (Stored == nullptr)
		{
			UnreachableEnts[Index] = UnreachableEnts.Last();
			UnreachableEnts.Pop();
			continue;
		}
		if (Stored != Unreachable)
		{
			continue;
		}
		if (Now <= UnreachableEnts[Index].ExpiresAt && Unreachable != nullptr)
		{
			const double DistSq =
				(Unreachable->Origin - UnreachableEnts[Index].PositionCm).SizeSquared();
			if (DistSq <= static_cast<double>(UnreachableDistSq) * NpcKernelPositions2Shared::GPositionsTailU * NpcKernelPositions2Shared::GPositionsTailU)
			{
				return true;
			}
		}
		UnreachableEnts[Index] = UnreachableEnts.Last();
		UnreachableEnts.Pop();
		return false;
	}
	return false;
}

bool FElysiumNpcBase::IsValidCover(const FVector& CoverCm, void* Hint)
{
	// `0x1028af20`, read from the listing because the decompiler mis-assigned both stack arguments:
	//
	//     Vector end( cover.x, cover.y, cover.z - NAI_Hull::Mins(m_eHull).z + 0.01 );
	//     Ray_t ray;  ray.Init( cover, end, m_Collision.OBBMins(), m_Collision.OBBMaxs() );
	//     trace_t tr; enginetrace->TraceRay( ray, 0x202400b, CTraceFilterSimple(this, 0), &tr );
	//     if (tr.startsolid)  return false;                      // trace_t +0x37
	//     if (m_strHintGroup != NULL_STRING &&
	//         (pHint == NULL || pHint->m_strGroup (+0x5f0) != m_strHintGroup)) return false;
	//     return true;
	//
	// What it actually asks is small and worth stating plainly: the cover spot must not be inside
	// solid, and — only when this NPC has been given a hint group — the hint offered with it must
	// belong to the same group. The trace's END is `0.01 - mins.z` ABOVE its start (R2 §3): HUMAN_HULL's
	// mins.z is 0 (static init `0x102d4440`), so for every hull whose floor is at the origin the end
	// sits a hundredth of a unit UP. It is a near-zero-length STANDING hull probe at the spot, not a
	// drop. The hull enum (`m_eHull +0x1568`) feeds only that z; the box is `m_Collision`'s OBB
	// (`1028af87` / `1028af90`), and `Ray_t::Init(.., 1, 0)` asks the engine's cylinder test.
	FVector HullMins = FVector::ZeroVector;
	FVector HullMaxs = FVector::ZeroVector;
	RetailHullExtents(HullKind, EElysiumHullExtents::Full, HullMins, HullMaxs);
	const FVector EndCm(CoverCm.X, CoverCm.Y,
		CoverCm.Z - HullMins.Z * NpcKernelPositions2Shared::GPositionsTailU + ValidCoverDrop * NpcKernelPositions2Shared::GPositionsTailU);

	FVector ObbMins = FVector::ZeroVector;
	FVector ObbMaxs = FVector::ZeroVector;
	RetailCollisionExtents(*this, ObbMins, ObbMaxs);

	// Family Motor's `KernelHullTrace` (SOURCE units, port axes), mask `0x202400b`, filter
	// `CTraceFilterSimple(this, 0)`: its character rule is the kernel trace's (BCC / hidden / slot 68
	// either way; no force bracket here). The ONLY `trace_t` word read is `startsolid` (`+0x37`,
	// `1028b00c`): the fraction is not, so a blocked-but-not-embedded hull passes. A world with no
	// collision answers clear, which admits.
	FKernelHullTrace Trace;
	KernelHullTrace(CoverCm / NpcKernelPositions2Shared::GPositionsTailU, EndCm / NpcKernelPositions2Shared::GPositionsTailU, ObbMins, ObbMaxs, MaskValidCover, Trace);
	if (Trace.bStartSolid)
	{
		return false;
	}

	const FHintWords* HintNode = static_cast<const FHintWords*>(Hint);
	if (!BaseScheduleHost.HintGroup.IsEmpty()
		&& (HintNode == nullptr || !HintNode->bValid || HintNode->Group != BaseScheduleHost.HintGroup))
	{
		return false;
	}
	return true;
}
