#include "Substrate/ElysiumNpcScurrying.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	// The player sheet's level-script clan encoding: Brujah 2 … Ventrue 8, so Nosferatu is 5
	// (`ElysiumDlgClan::OffsetFromSheetClan`). `0x103ad0f0` compares the target's character template
	// against `Player_Nosferatu`, and a player's template IS its clan on this sheet.
	constexpr int32 GNosferatuClan = 5;
	// The Scurrying flee march's two scalars: `_DAT_104454d0` = **0.5** (the start raise, the plane
	// deflection and the distance halving) and `_DAT_104454c0` = **1.0** (the give-up floor and the
	// "trace was blocked" fraction).
	constexpr float GFleeRetryScale = 0.5f;
	constexpr float GFleeDistanceFloor = 1.0f;
	// The jitter bands `0x103acba0` picks between: `0..-60` / `0..60` on the dominant axis and
	// `-80..80` on the other, with five attempts.
	constexpr float GFleeJitterMinor = 80.0f;
	constexpr float GFleeJitterMajor = 60.0f;
	constexpr int32 GFleeJitterAttempts = 5;
	// `0x102edae0`'s fourth argument, the constant 30000.0 beside the flee distance.
	constexpr float GFleeNodeSearchUnits = 30000.0f;
}

const FElysiumNpcClass* FElysiumNpcScurrying::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 440: `0x103ac490`.
	// `0x103ac490`
// `0x103ac490`, `CNPC_VScurrying::TranslateSchedule`, the body of `FElysiumNpcScurrying::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpcScurrying::TranslateScheduleRetail(int32 ScheduleNumber)
{
	if (ScheduleNumber == 0x6b) { return 0x161; }
	if (ScheduleNumber == 0x156) { return 0x15e; }
	if (ScheduleNumber == 0x157) { return 0x15f; }
	return TroikaTranslateScheduleRetail(ScheduleNumber);
}

// Slot 337: `0x103ac4e0`.
int32 FElysiumNpcScurrying::GetUsedHullBits()
{
	// The Troika body `0x1029a050` called directly, its 1 ORed with this class's bit.
	// `CNPC_VRat` inherits this body.
	return FElysiumNpc::GetUsedHullBits() | 0x80000;
}

// Slot 546: `0x103abd40`, the class's own schedule id space.
const TCHAR* FElysiumNpcScurrying::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093c4e0`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VScurrying"), TEXT("0x103abd40"), TEXT("0x1093c4e0") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// --- Moved from `ElysiumNpcSenses10_2.cpp` (story 5 step 4) ---

bool FElysiumNpcScurrying::IsNosferatuTemplate(const FElysiumEntity& SeenTarget)
{
	// `0x103ad0f0`: the target's character template compared against `Player_Nosferatu`. On this
	// sheet a player's template IS its clan (`ElysiumDlgClan::OffsetFromSheetClan`), and a
	// non-player target carries no `Player_*` template at all.
	const FElysiumCombatCharacter* Character = SeenTarget.AsCombatCharacter();
	return Character != nullptr && Character->Sheet.Clan() == GNosferatuClan
		&& SeenTarget.AsNpc() == nullptr;
}

bool FElysiumNpcScurrying::ScurryingMustDetectAdmits(const FElysiumEntity& SeenTarget) const
{
	// `0x103ad0a0`: true for any target that is NOT a player, and for a player only while
	// COND `0x5a` (`SEE_PLAYER`) or COND `0x6f` stands.
	if (World == nullptr || SeenTarget.Handle != World->PlayerHandle())
	{
		return true;
	}
	return Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(0x5a))
		|| Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(0x6f));
}

bool FElysiumNpcScurrying::ScurryingShouldDetect(const FElysiumEntity* SeenTarget) const
{
	// `103acac0`: a null target answers false.
	if (SeenTarget == nullptr)
	{
		return false;
	}
	// `103acad3`..`103acb24`: the distance between the two slot-217 origins (the ROOT of the summed
	// squares, `0x10579660`) must be STRICTLY below `m_flDetectionDistance` — `103acb15 FCOMP
	// [ESI+0x6690]` then `TEST AH,0x5 / JP` refuses equal, greater and unordered.
	const float DistanceUnits = static_cast<float>(
		FVector::Dist(Origin, SeenTarget->Origin) / ElysiumMove::U);
	if (!(DistanceUnits < ScurryingDetectionDistanceUnits))
	{
		return false;
	}
	// `103acb26`: `m_fIgnoreNosferatu` (`+0x6694`) rejects a `Player_Nosferatu` target (`0x103ad0f0`).
	if (bScurryingIgnoreNosferatu && IsNosferatuTemplate(*SeenTarget))
	{
		return false;
	}
	// `103acb3d`: `m_fMustDetect` (`+0x6695`) rejects unless `0x103ad0a0` (thunk `0x10007595`) holds.
	if (bScurryingMustDetect && !ScurryingMustDetectAdmits(*SeenTarget))
	{
		return false;
	}
	// `103acb53`: passing all of them answers true.
	return true;
}

bool FElysiumNpcScurrying::ScurryingFindFleeDestination(const FVector& ThreatPosCm, float DistanceUnits,
	FVector* OutDestinationCm)
{
	// `103acbb0`: `0x102edae0(navigator, threat, distance, 30000.0, &node)`, the navigator's node
	// search around the THREAT with this call's flee distance (story 5 step 4r: the port dropped
	// the distance and called it "the nearest node within 30000").
	FVector NodeCm = FVector::ZeroVector;
	if (NearestNavigatorNode(ThreatPosCm, DistanceUnits, GFleeNodeSearchUnits, NodeCm))
	{
		// `103acc3c`: the JITTER. Whichever of the x or y deltas to the threat is LARGER picks the
		// axis; the SIGN of that delta picks a `0..-60` or `0..60` band on it and the other axis
		// gets `-80..80`.
		const double DeltaX = ThreatPosCm.X - NodeCm.X;
		const double DeltaY = ThreatPosCm.Y - NodeCm.Y;
		float MinX = 0.f;
		float MaxX = 0.f;
		float MinY = 0.f;
		float MaxY = 0.f;
		if (FMath::Abs(DeltaX) <= FMath::Abs(DeltaY))
		{
			// The Y delta dominates: Y takes the signed 0..±60 band, X takes -80..80.
			if (DeltaY <= 0.0)
			{
				MinY = 0.f;
				MaxY = -GFleeJitterMajor;
			}
			else
			{
				MinY = 0.f;
				MaxY = GFleeJitterMajor;
			}
			MinX = -GFleeJitterMinor;
			MaxX = GFleeJitterMinor;
		}
		else
		{
			if (DeltaX <= 0.0)
			{
				MinX = 0.f;
				MaxX = -GFleeJitterMajor;
			}
			else
			{
				MinX = 0.f;
				MaxX = GFleeJitterMajor;
			}
			MinY = -GFleeJitterMinor;
			MaxY = GFleeJitterMinor;
		}
		// `103accc8`: z is the node z plus slot 522 `StepHeight` times `_DAT_104454d0` (0.5); up to
		// FIVE random points are tried against `IsAreaClear` (`0x10001f50`).
		FVector CandidateCm = NodeCm;
		bool bClear = false;
		for (int32 Attempt = 0; Attempt < GFleeJitterAttempts; ++Attempt)
		{
			const float ZCm = static_cast<float>(NodeCm.Z
				+ static_cast<double>(StepHeight()) * 0.5 * ElysiumMove::U);
			// Two `RandomFloat` draws per try (`103accc1`, `103accda`): the y offset FIRST, then x.
			CandidateCm.Y = NodeCm.Y
				+ ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(
					FMath::Min(MinY, MaxY), FMath::Max(MinY, MaxY)) * ElysiumMove::U;
			CandidateCm.X = NodeCm.X
				+ ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(
					FMath::Min(MinX, MaxX), FMath::Max(MinX, MaxX)) * ElysiumMove::U;
			CandidateCm.Z = ZCm;
			bClear = IsAreaClear(CandidateCm, 0x202400b);
			if (bClear)
			{
				break;
			}
		}
		if (!bClear)
		{
			// `103acd30`: five refusals answer the BARE node — x from `FLD [ESP+0xc]`, y and z
			// reloaded from the node at `103acd40` — not the last candidate tried.
			CandidateCm = NodeCm;
		}
		// `103acd34`: the point is written out only when one was passed, and the body answers 1
		// either way.
		if (OutDestinationCm != nullptr)
		{
			*OutDestinationCm = CandidateCm;
		}
		return true;
	}

	// `103acd6c`: the MARCH. Start at slot 217 `GetAbsOrigin` raised by slot 522 `StepHeight` times
	// `_DAT_104454d0` (0.5).
	const FVector StartCm = Origin
		+ FVector(0.0, 0.0, static_cast<double>(StepHeight()) * 0.5 * ElysiumMove::U);
	// `103acda6`..`103acddc`: the direction AWAY from the threat, off the UNRAISED origin, normalised
	// in 3-D (`0x1057966c`). Source's normalise answers (0, 0, 1) for a zero vector where this
	// answers zero; the march below reads only x and y, so the endpoint is the same.
	FVector Direction = (Origin - ThreatPosCm).GetSafeNormal();
	float Distance = DistanceUnits;
	for (;;)
	{
		// `103acde4`..`103ace1f`: x and y march along the direction; z is COPIED from the start
		// (`103ace00 MOV ECX,[start.z]` / `103ace05` into `end.z`). The march is horizontal, and a
		// threat above or below shortens it, because the normalise was 3-D.
		const FVector EndCm(StartCm.X + Direction.X * Distance * ElysiumMove::U,
			StartCm.Y + Direction.Y * Distance * ElysiumMove::U, StartCm.Z);
		// `103ace23`..`103ace80`: a hull trace with the collision bounds from `+0x1568` and mask
		// `0x202400b`.
		FKernelHullTrace Trace;
		const bool bTraced = KernelHullTrace(StartCm / ElysiumMove::U, EndCm / ElysiumMove::U,
			HullMinsUnits(false), HullMaxsUnits(false), 0x202400b, Trace);
		// `103aceb9`..`103acee1`: BLOCKED while the fraction is below `_DAT_104454c0` (1.0 — `TEST
		// AH,0x5 / JNP`), or `allsolid` (`+0x36`), or `startsolid` (`+0x37`). The seam reports a
		// clear trace, which is the arm that returns the endpoint.
		const bool bBlocked = bTraced
			&& (Trace.Fraction < GFleeDistanceFloor || Trace.bAllSolid || Trace.bStartSolid);
		if (!bBlocked)
		{
			// `103acf6d`: the endpoint is written only when an out-vector was passed.
			if (OutDestinationCm != nullptr)
			{
				*OutDestinationCm = EndCm;
			}
			return true;
		}
		// `103acee7`..`103acf31`: deflect along the hit plane — the direction plus
		// `_DAT_104454d0` (0.5) times `plane.normal`, renormalised.
		Direction = (Direction + Trace.PlaneNormal * GFleeRetryScale).GetSafeNormal();
		// `103acf39`..`103acf5a`: halve the distance, and give up once it is at or below
		// `_DAT_104454c0` (1.0).
		Distance *= GFleeRetryScale;
		if (Distance <= GFleeDistanceFloor)
		{
			return false;
		}
	}
}
