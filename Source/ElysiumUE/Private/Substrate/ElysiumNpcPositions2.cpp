#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcPositions2Shared.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcKernelShape.h"

// Story 29c-1, family **Positions**, second half — the trace bodies, the unreachable cache, the
// eight fills of slot 563, the Werewolf teleport pair and `CNPC_VTzimisce`'s aim override. The node
// selectors and the teleport clearance rules are `Substrate/ElysiumNpcPositions.cpp`, which
// also states the family's standing facts; the declarations are the matching `.inl`.
//
// Every threshold below was read out of the pinned retail `vampire.dll`'s `.rdata` at its cited
// address. Where a global is quoted with no number, it lives past `.data`'s raw size in that image
// — filled at runtime, unreadable from the file — and is named as a seam.

namespace
{

	constexpr int32 MaskBlockLos = 0x4081;          // `EnemyCouldSeeHull`'s sight trace

}

// --- Slots 548 / 549 `IsValidCover` / `IsValidShootPosition` ------------------------------------

// --- `CAI_BaseNPCTroika::IsAreaClear` `0x102a0fb0` ----------------------------------------------

bool FElysiumNpc::IsAreaClear(const FVector& FromCm, int32 Mask, const FVector* MinsUnits,
	const FVector* MaxsUnits)
{
	// `CAI_BaseNPCTroika::IsAreaClear(pos, mask, mins, maxs)` `0x102a0fb0` (`RET 0x10`, R2 §4), the
	// whole body past the VProf scaffolding:
	//
	//     if (!mins) mins = m_Collision.OBBMins();                  // slot 1
	//     if (!maxs) maxs = m_Collision.OBBMaxs();                  // slot 2
	//     m_bForceNPCCheck = 1;                                     // +0x63da
	//     UTIL_TraceHull( from, from, mins, maxs, mask, CTraceFilterNav(m_pMoveProbe->npc, +0x368),
	//                     &tr, 1 );                                 // 0x1026e940, cylinder flag 1
	//     m_bForceNPCCheck = 0;
	//     return tr.fraction >= 1.0 && !tr.allsolid && !tr.startsolid;   // 102a110a..102a1134
	//
	// Start AND end are the same point, so it is a stationary hull test: "is anything already
	// standing where I want to be". `m_bForceNPCCheck` is raised for exactly the duration of the
	// trace, which is what makes the probe count OTHER NPCS as blockers for this one query and for
	// no other — the flag is the whole reason the body is not just a trace call. The fraction test is
	// `>=` with NaN passing, as the listing's flag test has it; `>= 1.0` on a float is the same set.
	// Masks at retail's callers: `0x202400b` (`PickSpotFor 0x102da0d0`, `0x103acba0`, `0x1039ee20`)
	// and `0x2400b` (`0x10397410`), the latter listing no character at all.
	FVector ObbMins = FVector::ZeroVector;
	FVector ObbMaxs = FVector::ZeroVector;
	RetailCollisionExtents(*this, ObbMins, ObbMaxs);
	const FVector& Mins = MinsUnits != nullptr ? *MinsUnits : ObbMins;
	const FVector& Maxs = MaxsUnits != nullptr ? *MaxsUnits : ObbMaxs;

	bForceNpcCheck = true;
	FKernelHullTrace Trace;
	const FVector AtUnits = FromCm / NpcKernelPositions2Shared::GPositionsTailU;
	KernelHullTrace(AtUnits, AtUnits, Mins, Maxs, Mask, Trace);
	bForceNpcCheck = false;

	// A world with no collision answers `Fraction = 1` and neither solid flag, so an unanswered query
	// reads CLEAR — retail's own answer for a trace that hit nothing.
	return !(Trace.Fraction < NpcKernelPositions2Shared::GPositionsTailRetailOne) && !Trace.bAllSolid
		&& !Trace.bStartSolid;
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
