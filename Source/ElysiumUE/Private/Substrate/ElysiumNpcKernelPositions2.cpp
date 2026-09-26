#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcKernelPositions2Shared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "Substrate/ElysiumNpcKernelShape.h"

// Story 29c-1, family **Positions**, second half — the trace bodies, the unreachable cache, the
// eight fills of slot 563, the Werewolf teleport pair and `CNPC_VTzimisce`'s aim override. The node
// selectors and the teleport clearance rules are `Substrate/ElysiumNpcKernelPositions.cpp`, which
// also states the family's standing facts; the declarations are the matching `.inl`.
//
// Every threshold below was read out of the pinned retail `vampire.dll`'s `.rdata` at its cited
// address. Where a global is quoted with no number, it lives past `.data`'s raw size in that image
// — filled at runtime, unreadable from the file — and is named as a seam.

namespace
{
	constexpr double ValidCoverDrop = ElysiumNpcTunables::HundredthDouble;

	// `CAI_BaseNPC::IsUnreachable` `0x102741e0`'s squared-distance threshold, Source units squared.
	constexpr float UnreachableDistSq = 14400.0f;   // _DAT_10499560 — 120 units, squared

	// The three trace masks, as retail spells them.
	constexpr int32 MaskValidCover = 0x202400b;     // `IsValidCover`'s downward hull trace
	constexpr int32 MaskBlockLos = 0x4081;          // `EnemyCouldSeeHull`'s sight trace

}

// --- Slot 530 `IsUnreachable` `0x102741e0` ------------------------------------------------------

bool FElysiumNpc::IsUnreachable(FElysiumEntity* Unreachable)
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

// --- Slots 548 / 549 `IsValidCover` / `IsValidShootPosition` ------------------------------------

bool FElysiumNpc::IsValidCover(const FVector& CoverCm, void* Hint)
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
	// belong to the same group. The trace's END is barely below its start (the hull's own mins.z
	// plus a hundredth of a unit), so it is a STANDING hull test at the spot, not a drop test.
	FVector HullMins = FVector::ZeroVector;
	FVector HullMaxs = FVector::ZeroVector;
	RetailHullExtents(HullKind, EElysiumHullExtents::Full, HullMins, HullMaxs);   // family Motor's seam: the zero box
	const FVector EndCm(CoverCm.X, CoverCm.Y,
		CoverCm.Z - HullMins.Z * NpcKernelPositions2Shared::GPositionsTailU + ValidCoverDrop * NpcKernelPositions2Shared::GPositionsTailU);

	FVector ObbMins = FVector::ZeroVector;
	FVector ObbMaxs = FVector::ZeroVector;
	RetailCollisionExtents(*this, ObbMins, ObbMaxs);

	FKernelHullTrace Trace;
	// **SEAM**, family Motor's `KernelHullTrace`, in SOURCE units. It carries a fraction and a hit
	// entity and NOT retail's `startsolid`, so the start-solid arm below can never fire; a seam that
	// cannot answer reads as "not in solid", which is the arm that admits the cover.
	KernelHullTrace(CoverCm / NpcKernelPositions2Shared::GPositionsTailU, EndCm / NpcKernelPositions2Shared::GPositionsTailU, ObbMins, ObbMaxs, MaskValidCover, Trace);

	const FHintWords* HintNode = static_cast<const FHintWords*>(Hint);
	if (!ScheduleHost.HintGroup.IsEmpty()
		&& (HintNode == nullptr || !HintNode->bValid || HintNode->Group != ScheduleHost.HintGroup))
	{
		return false;
	}
	return true;
}

bool FElysiumNpc::IsValidShootPosition(const FVector& PositionCm, void* Hint)
{
	// `0x1028b0b0`, 36 bytes and the whole body — the hint-group half of `IsValidCover` with no
	// trace at all, and the position argument is never read:
	//     if (m_strHintGroup != NULL_STRING &&
	//         (pHint == NULL || pHint->m_strGroup != m_strHintGroup)) return false;
	//     return true;
	// An NPC with no hint group accepts every shoot position, which is what makes the base body a
	// no-op for all but the hint-grouped cast.
	(void)PositionCm;
	const FHintWords* HintNode = static_cast<const FHintWords*>(Hint);
	if (!ScheduleHost.HintGroup.IsEmpty()
		&& (HintNode == nullptr || !HintNode->bValid || HintNode->Group != ScheduleHost.HintGroup))
	{
		return false;
	}
	return true;
}

// --- `CAI_BaseNPCTroika::IsAreaClear` `0x102a0fb0` ----------------------------------------------

bool FElysiumNpc::IsAreaClear(const FVector& FromCm, int32 Mask)
{
	// The whole body past the VProf scaffolding:
	//
	//     if (!mins) mins = m_Collision.OBBMins();
	//     if (!maxs) maxs = m_Collision.OBBMaxs();
	//     m_bForceNPCCheck = 1;                                     // +0x63da
	//     CAI_MoveProbe::TraceHull( from, from, mins, maxs, mask, m_pMoveProbe, &tr, true );
	//     m_bForceNPCCheck = 0;
	//     return tr.fraction >= 1.0 && !tr.allsolid && !tr.startsolid;
	//
	// Start AND end are the same point, so it is a stationary hull test: "is anything already
	// standing where I want to be". `m_bForceNPCCheck` is raised for exactly the duration of the
	// trace, which is what makes the probe count OTHER NPCS as blockers for this one query and for
	// no other — the flag is the whole reason the body is not just a trace call.
	FVector ObbMins = FVector::ZeroVector;
	FVector ObbMaxs = FVector::ZeroVector;
	RetailCollisionExtents(*this, ObbMins, ObbMaxs);

	bForceNpcCheck = true;
	FKernelHullTrace Trace;
	KernelHullTrace(FromCm / NpcKernelPositions2Shared::GPositionsTailU, FromCm / NpcKernelPositions2Shared::GPositionsTailU, ObbMins, ObbMaxs, Mask, Trace);
	bForceNpcCheck = false;

	// The seam answers `Fraction = 1` when it cannot trace and carries neither solid flag, so an
	// unanswered query reads CLEAR — retail's own answer for a trace that hit nothing.
	return Trace.Fraction >= NpcKernelPositions2Shared::GPositionsTailRetailOne;
}

// --- Slot 563 `TranslateEnemyChasePosition`, eight bodies ---------------------------------------

FVector FElysiumNpc::EnemyChaseAnchor(const FElysiumEntity& Enemy)
{
	// `pEnemy->vtable[0x304]()` — slot 193 `EyePosition`. The chase position is nudged by
	// `EyePosition() - GetAbsOrigin()` on the ENEMY, so a chaser aims for the enemy's eye height
	// rather than its feet.
	return Enemy.EyePosition();
}

float FElysiumNpc::GroundSpeedCm() const
{
	// `m_flGroundSpeed` (+0x0654). **SEAM**: `IElysiumNpcMotor` publishes no realized ground speed
	// to the kernel, so the lead helpers below are handed zero.
	return 0.f;
}

FVector FElysiumNpc::LocalVelocityCm() const
{
	// `GetLocalVelocity()` — slot 220 through the vtable at `+0x370`. **SEAM**: same reason.
	return FVector::ZeroVector;
}

float FElysiumNpc::WerewolfChaseToleranceConVar()
{
	// `ConVar` `DAT_1093d52c`, read as `IsCommand() ? 0.0f : m_fValue (+0x28)`:
	// `werewolf_translated_enemy_position_tolerance`, shipped "0", so as shipped the Werewolf's
	// tolerance is the plain `m_flGoalTolerance`.
	return ElysiumNpcTunables::ConVarFloat(
		ElysiumNpcTunables::EConVar::WerewolfTranslatedEnemyPositionTolerance);
}

void FElysiumNpc::ChaseLeadTolerance(FElysiumEntity* Enemy, const FVector& ChasePositionCm,
	float& InOutTolerance) const
{
	// `thunk_FUN_102c3b50(this, pEnemy, chasePos (by value), &chasePos, &tolerance)` — the first of
	// the two target-lead helpers. Neither body is in this family's rows and neither has a port
	// counterpart. **SEAM**: the tolerance is left exactly as the caller set it.
	(void)Enemy;
	(void)ChasePositionCm;
	(void)InOutTolerance;
}

void FElysiumNpc::ChaseLeadPosition(FElysiumEntity* Enemy, const FVector& VelocityCm,
	float GroundSpeed, const FVector& ChasePositionCm, FVector& OutPositionCm) const
{
	// `thunk_FUN_102c36d0(this, GetLocalVelocity() (by value), pEnemy, m_flGroundSpeed, &chasePos,
	// &out)` — the second helper, which is what actually moves the chase point ahead of a running
	// enemy. **SEAM**: answers the position unchanged.
	(void)Enemy;
	(void)VelocityCm;
	(void)GroundSpeed;
	OutPositionCm = ChasePositionCm;
}

void FElysiumNpc::TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm,
	void* Tolerance, void* SecondTolerance)
{
	// `CAI_BaseNPCTroika::TranslateEnemyChasePosition` `0x10295300`, the body slot 563 carries for
	// every spawnable species. 125 bytes, and the whole of it:
	//
	//     if (GetNavigator()->GetNavType() == 2) {                 // thunk_FUN_1027d990, NAV_FLY
	//         chasePos += pEnemy->EyePosition() - pEnemy->GetAbsOrigin();
	//         tolerance = NAI_Hull::Width( m_eHull );              // maxs.y - mins.y
	//     }
	//
	// Nav type 2 is FLY. A flying chaser aims at the enemy's eye and widens its tolerance to its own
	// hull width; a walking one is left untouched. The BASE body (`0x10289f20`) is byte-identical
	// but for an else arm that writes `tolerance = 0`, and the difference is observable — see
	// `TranslateEnemyChasePositionAs`. Six species classes override this method on their C++
	// classes (story 5 step 3).
	float* ToleranceOut = static_cast<float*>(Tolerance);
	(void)SecondTolerance;
	if (NavGetType() != 2 || Enemy == nullptr)
	{
		return;
	}
	ChasePositionCm += EnemyChaseAnchor(*Enemy) - Enemy->Origin;
	if (ToleranceOut != nullptr)
	{
		FVector HullMins = FVector::ZeroVector;
		FVector HullMaxs = FVector::ZeroVector;
		RetailHullExtents(HullKind, EElysiumHullExtents::Full, HullMins, HullMaxs);
		// `NAI_Hull::Width` is `FUN_102d61b0` — `maxs.y - mins.y`, the Y span, not a radius.
		*ToleranceOut = static_cast<float>(HullMaxs.Y - HullMins.Y) * NpcKernelPositions2Shared::GPositionsTailU;
	}
}

void FElysiumNpc::TranslateEnemyChasePositionShaped(EChaseTranslateShape Shape,
	FElysiumEntity* Enemy, FVector& ChasePositionCm, void* InTolerance, void* InSecondTolerance)
{
	// The body of a species class's slot-563 override (story 5 step 3), in the shape its retail
	// body takes. The generated slot's `float&`/`float*` pair arrives as `void*`; a null one reads and
	// writes a scratch, which no retail caller passes.
	float ScratchTolerance = 0.f;
	float ScratchSecond = 0.f;
	float& Tolerance = InTolerance != nullptr ? *static_cast<float*>(InTolerance) : ScratchTolerance;
	float& SecondTolerance =
		InSecondTolerance != nullptr ? *static_cast<float*>(InSecondTolerance) : ScratchSecond;
	TranslateEnemyChasePositionAs(Shape, Enemy, ChasePositionCm, Tolerance, SecondTolerance);
}

void FElysiumNpc::TranslateEnemyChasePositionAs(EChaseTranslateShape Shape, FElysiumEntity* Enemy,
	FVector& ChasePositionCm, float& Tolerance, float& SecondTolerance)
{
	// The seven non-Troika fills of slot 563. All of them share the nav-type-2 gate and the
	// eye-minus-origin offset; what differs is entirely what happens on the OTHER arm.
	if (Shape == EChaseTranslateShape::Empty)
	{
		// `CNPC_VCamera::TranslateEnemyChasePosition` `0x10368ee0` — three bytes, all four
		// arguments ignored. A security camera never translates a chase position, and recording that
		// is the point: a reader looking for the camera's rule stops at this line.
		return;
	}

	const bool bNavigating = NavGetType() == 2;
	if (bNavigating && Enemy != nullptr)
	{
		ChasePositionCm += EnemyChaseAnchor(*Enemy) - Enemy->Origin;
		if (Shape == EChaseTranslateShape::Base || Shape == EChaseTranslateShape::Troika)
		{
			FVector HullMins = FVector::ZeroVector;
			FVector HullMaxs = FVector::ZeroVector;
			RetailHullExtents(HullKind, EElysiumHullExtents::Full, HullMins, HullMaxs);
			Tolerance = static_cast<float>(HullMaxs.Y - HullMins.Y) * NpcKernelPositions2Shared::GPositionsTailU;
		}
		// `CNPC_VAnimal` (`0x1035f5c0`, with `CNPC_VDog` and `CNPC_VRat` beside it) and
		// `CNPC_VHuman` (`0x10384760`, covering 42 classes) stop at the offset: neither writes the
		// tolerance at all, so a flying dog keeps whatever tolerance the task set.
		return;
	}

	switch (Shape)
	{
	case EChaseTranslateShape::Base:
		// `CAI_BaseNPC::TranslateEnemyChasePosition` `0x10289f20` — the ONE body of the eight whose
		// else arm writes: `*tolerance = 0`, an integer zero stored over the float. A grounded
		// chaser on the base line is given a zero tolerance and must reach the position exactly.
		Tolerance = 0.f;
		break;

	case EChaseTranslateShape::GoalToleranceLead:
	{
		// `CNPC_VMingXiao` `0x10392c40` and `CNPC_VTzimisce` `0x103ba640`, byte-identical:
		//
		//     *arg3      = m_flGoalTolerance;                      // +0x6320
		//     thunk_FUN_102c3b50( pEnemy, chasePos, &chasePos, arg3 );
		//     *tolerance = *arg3;
		//     Vector v   = GetLocalVelocity();                     // slot 220
		//     thunk_FUN_102c36d0( v, pEnemy, m_flGroundSpeed, &chasePos, &out );
		//     chasePos   = out;
		//
		// The fourth argument is the scratch the two helpers pass the tolerance through; the third
		// receives a COPY of it after the first helper has had its say.
		SecondTolerance = ScheduleHost.GoalToleranceCm;
		ChaseLeadTolerance(Enemy, ChasePositionCm, SecondTolerance);
		Tolerance = SecondTolerance;
		FVector Led = ChasePositionCm;
		ChaseLeadPosition(Enemy, LocalVelocityCm(), GroundSpeedCm(), ChasePositionCm, Led);
		ChasePositionCm = Led;
		break;
	}

	case EChaseTranslateShape::GoalToleranceWerewolfLead:
		// `CNPC_VWerewolf` `0x103d9e00`:
		//
		//     *arg3      = m_flGoalTolerance;
		//     *arg3     += ConVar(DAT_1093d52c).GetFloat();        // 0.0 when the cvar is unreadable
		//     thunk_FUN_102c3b50( pEnemy, chasePos, &chasePos, arg3 );
		//     *tolerance = *arg3;
		//
		// and nothing else — the Werewolf takes the tolerance lead but NOT the position lead, which
		// is the one line that separates it from MingXiao and Tzimisce.
		SecondTolerance = ScheduleHost.GoalToleranceCm + WerewolfChaseToleranceConVar();
		ChaseLeadTolerance(Enemy, ChasePositionCm, SecondTolerance);
		Tolerance = SecondTolerance;
		break;

	case EChaseTranslateShape::Troika:
	case EChaseTranslateShape::OffsetOnly:
	case EChaseTranslateShape::Empty:
	default:
		// `CAI_BaseNPCTroika` `0x10295300`, `CNPC_VAnimal` `0x1035f5c0` and `CNPC_VHuman`
		// `0x10384760` all fall off the end of the gate and write nothing.
		break;
	}
}

// --- Slot 389's `CNPC_VTzimisce` override `0x103bfd80` ------------------------------------------

bool FElysiumNpc::ComputeHitboxSurroundingBox(FVector& OutMinsCm, FVector& OutMaxsCm) const
{
	// `CBaseAnimating::GetSeqDesc(m_nSequence (+0x6f0))` then
	// `CBaseAnimating::ComputeHitboxSurroundingBox(&mins, &maxs)`. **SEAM**: the animating tier
	// exposes no hitbox set to the kernel, so this answers false — which is retail's own
	// "no sequence description" arm, and that arm falls through to the hull box.
	(void)OutMinsCm;
	(void)OutMaxsCm;
	return false;
}
