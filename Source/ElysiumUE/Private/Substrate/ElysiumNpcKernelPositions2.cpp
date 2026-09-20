#include "Substrate/ElysiumNpc.h"

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
	constexpr float GPositionsTailRetailOne = 1.0f;             // _DAT_104454c0
	constexpr float GPositionsTailRetailZero = 0.0f;            // _DAT_104454c4
	constexpr float RetailHalf = 0.5f;            // _DAT_104454d0
	constexpr double ValidCoverDrop = 0.01;       // _DAT_1044e658, a DOUBLE

	// `CAI_BaseNPC::IsUnreachable` `0x102741e0`'s squared-distance threshold, Source units squared.
	constexpr float UnreachableDistSq = 14400.0f;   // _DAT_10499560 — 120 units, squared

	// `CNPC_VWerewolf::UpdateConditionCanTeleport` `0x103cc0d0`.
	constexpr float WerewolfCloseEnough = 800.0f;   // _DAT_10457ac4, Source units
	constexpr float WerewolfTeleportDistanceFloor = 100.0f;   // _DAT_10450564

	// The three trace masks, as retail spells them.
	constexpr int32 MaskValidCover = 0x202400b;     // `IsValidCover`'s downward hull trace
	constexpr int32 MaskBlockLos = 0x4081;          // `EnemyCouldSeeHull`'s sight trace

	// `CNPC_VWerewolf::TeleportIn`/`TeleportOut`'s effects and solid bits.
	constexpr uint32 GPositionsTailEffectNoDraw = 0x20;           // m_fEffects |= / &= ~
	constexpr uint32 GPositionsTailSolidNotSolid = 0x4;           // m_Collision's +0x2b4 |= / &= ~

	// Retail condition 0x77. This runtime's `EElysiumNpcCond` gained the enumerator with this
	// story; the numeric value is retail's own and is what `SetCondition`/`ClearCondition`
	// (`0x10269a20` / `0x10269b50`) index the 256-bit set with.
	constexpr EElysiumNpcCond CondCanTeleport = EElysiumNpcCond::CanTeleport;

	constexpr float GPositionsTailU = ElysiumMove::U;

	// `AngleVectors` `0x10139550` — forward, right and up for Source `[pitch yaw roll]`, each
	// returned in THIS world's axes (`bsp.source_to_unreal` negates Y). Family Facing carries the
	// forward-only form in its own anonymous namespace; `CNPC_VTzimisce`'s aim override needs all
	// three, so the full routine is written here.
	void RetailAngleVectors(const FVector& SourceAngles, FVector& OutForward, FVector& OutRight,
		FVector& OutUp)
	{
		const float Pitch = FMath::DegreesToRadians(static_cast<float>(SourceAngles.X));
		const float Yaw = FMath::DegreesToRadians(static_cast<float>(SourceAngles.Y));
		const float Roll = FMath::DegreesToRadians(static_cast<float>(SourceAngles.Z));
		const float Sp = FMath::Sin(Pitch);
		const float Cp = FMath::Cos(Pitch);
		const float Sy = FMath::Sin(Yaw);
		const float Cy = FMath::Cos(Yaw);
		const float Sr = FMath::Sin(Roll);
		const float Cr = FMath::Cos(Roll);
		// Source's own three rows, with Y negated on the way out.
		OutForward = FVector(Cp * Cy, -(Cp * Sy), -Sp);
		OutRight = FVector(-Sr * Sp * Cy + Cr * Sy, -(-Sr * Sp * Sy - Cr * Cy), -Sr * Cp);
		OutUp = FVector(Cr * Sp * Cy + Sr * Sy, -(Cr * Sp * Sy - Sr * Cy), Cr * Cp);
	}
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
			if (DistSq <= static_cast<double>(UnreachableDistSq) * GPositionsTailU * GPositionsTailU)
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

bool FElysiumNpc::IsUnreachableChang(FElysiumEntity* Unreachable)
{
	// `CNPC_VChangBros::IsUnreachable` `0x1036e6f0`, slot 530 for the three Chang classes:
	//
	//     CBaseCombatCharacter* c = dynamic_cast<CBaseCombatCharacter*>(pTarget);
	//     if (c) {
	//         int mine = GetSector( GetAbsOrigin() );
	//         int his  = GetSector( c->GetAbsOrigin() );
	//         if (his == 3 && SectorIsInPit(mine)) return true;
	//         if (SectorIsInPit(his) && mine == 3) return true;
	//     }
	//     return CAI_BaseNPC::IsUnreachable( pTarget );
	//
	// The rule is symmetric and it is about the ARENA, not about pathing: one of the two is in the
	// pit (sector 1 or 2) and the other is on the ledge (sector 3), so neither can walk to the
	// other. A target that is not a combat character skips it entirely and takes the base cache.
	const FElysiumCombatCharacter* AsCharacter =
		Unreachable != nullptr ? Unreachable->AsCombatCharacter() : nullptr;
	if (AsCharacter != nullptr)
	{
		const int32 MySector = GetSector(Origin);
		const int32 HisSector = GetSector(Unreachable->Origin);
		if (HisSector == 3 && SectorIsInPit(MySector))
		{
			return true;
		}
		if (SectorIsInPit(HisSector) && MySector == 3)
		{
			return true;
		}
	}
	return IsUnreachable(Unreachable);
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
		CoverCm.Z - HullMins.Z * GPositionsTailU + ValidCoverDrop * GPositionsTailU);

	FVector ObbMins = FVector::ZeroVector;
	FVector ObbMaxs = FVector::ZeroVector;
	RetailCollisionExtents(*this, ObbMins, ObbMaxs);

	FKernelHullTrace Trace;
	// **SEAM**, family Motor's `KernelHullTrace`, in SOURCE units. It carries a fraction and a hit
	// entity and NOT retail's `startsolid`, so the start-solid arm below can never fire; a seam that
	// cannot answer reads as "not in solid", which is the arm that admits the cover.
	KernelHullTrace(CoverCm / GPositionsTailU, EndCm / GPositionsTailU, ObbMins, ObbMaxs, MaskValidCover, Trace);

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
	KernelHullTrace(FromCm / GPositionsTailU, FromCm / GPositionsTailU, ObbMins, ObbMaxs, Mask, Trace);
	bForceNpcCheck = false;

	// The seam answers `Fraction = 1` when it cannot trace and carries neither solid flag, so an
	// unanswered query reads CLEAR — retail's own answer for a trace that hit nothing.
	return Trace.Fraction >= GPositionsTailRetailOne;
}

// --- `EnemyCouldSeeHull`, slot 617's boss branch -------------------------------------------------

FElysiumNpc::FEnemySightCandidates FElysiumNpc::EnemySightCandidatesOf(const FVector& BoxMinCm,
	const FVector& BoxMaxCm, const FVector& ExtentsCm, float RandomZCm)
{
	// The blend, exactly as `0x10366510` computes it:
	//
	//     mins -= extents;  maxs += extents;
	//     mid   = (mins + maxs) * 0.5;                       // _DAT_104454d0
	//     mid.z = RandomFloat( mins.z, maxs.z );             // the ONLY random term
	//
	// and the three candidates the body then tests, in order, are `mid`, `mins` and `maxs`. 29c's
	// walk read them as bottom/mid/top; the listing shows the box CENTRE with a randomized height
	// first, then the two opposite CORNERS of the inflated box.
	FEnemySightCandidates Out;
	const FVector Min = BoxMinCm - ExtentsCm;
	const FVector Max = BoxMaxCm + ExtentsCm;
	Out.MinCm = Min;
	Out.MaxCm = Max;
	Out.MidCm = FVector((Min.X + Max.X) * RetailHalf, (Min.Y + Max.Y) * RetailHalf, RandomZCm);
	return Out;
}

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

bool FElysiumNpc::EnemyInViewCone(const FElysiumEntity& Enemy, const FVector& PointCm)
{
	// The enemy's slot 362 `FInViewCone(const Vector&)`, dispatched on the ENEMY and not on this
	// NPC. **SEAM**: no port body answers a view cone for an arbitrary world point, so this answers
	// false and every candidate is refused unless the caller asked to skip the cone.
	(void)Enemy;
	(void)PointCm;
	return false;
}

bool FElysiumNpc::WerewolfSightConVar()
{
	// `(**(code **)(*DAT_1093d694 + 4))()` / `DAT_1093d694[0xb]` — `ConVar::GetBool()` inlined as
	// `!IsCommand() && m_nValue (+0x2c) != 0`. **SEAM**: the pointer is in uninitialised `.data` and
	// no corpus function constructs it, so its NAME and DEFAULT are **unrecovered**; answering false
	// is what retail answers for a cvar it cannot read, and it CLOSES the Werewolf's gate.
	return false;
}

bool FElysiumNpc::EnemySightPredicate(const FElysiumEntity& Enemy)
{
	// The active enemy's own vtable `+0x278` (slot 158), the second gate of
	// `CNPC_VWerewolf::EnemyCouldSeeHull`. **SEAM**: unreached today, because the cvar above already
	// closed the gate; answering false keeps the refusal honest either way.
	(void)Enemy;
	return false;
}

bool FElysiumNpc::EnemyCouldSeeHull(const FVector& OriginCm, bool bSkipViewCone, bool bUseHitbox,
	const FVector& ExtentsCm)
{
	// `CNPC_VBaseBoss::EnemyCouldSeeHull` `0x10366510`, read from the listing (the decompiler lost
	// every one of the eight stack arguments). Signature from `signatures.tsv` slot 617:
	// `bool EnemyCouldSeeHull(Vector, bool, bool, Vector)`.
	//
	//     CBaseEntity* e = GetEnemy();                     // slot 167, vtable +0x29c
	//     if (!e) return false;
	//     CBaseCombatCharacter* enemy = e->m_pCombatCharacter (+0x9c);
	//     if (!enemy) return false;                        // a non-character enemy cannot look
	//     Vector eye = enemy->EyePosition();               // slot 193, the trace START
	//
	//     if (bUseHitbox && GetSeqDesc(m_nSequence))  ComputeHitboxSurroundingBox(&mins, &maxs);
	//     else { mins = origin + NAI_Hull::Mins(m_eHull);  maxs = origin + NAI_Hull::Maxs(m_eHull); }
	//
	//     mins -= extents;  maxs += extents;
	//     mid = ((mins + maxs) * 0.5) with mid.z = RandomFloat( mins.z, maxs.z );
	//
	//     filter = CTraceFilterSimple(0);  filter.AddIgnore(this);  filter.AddIgnore(enemy);
	//     for (p in { mid, mins, maxs })
	//         if (bSkipViewCone || enemy->FInViewCone(p))                  // slot 362, vtable +0x5a8
	//             if (TraceRay(eye -> p, mask 0x4081) is clear)            // fraction >= 1,
	//                 return true;                                        //  !allsolid, !startsolid
	//     return false;
	//
	// Two details the shape hangs on. The candidates are tested in a LADDER with the view-cone test
	// in FRONT of each trace, and `bSkipViewCone` skips only the cone — the trace still runs. And
	// the mask `0x4081` is `CONTENTS_SOLID | CONTENTS_OPAQUE | CONTENTS_MOVEABLE`: no character bit,
	// so another body standing in the way does not break the line, which is why the filter's two
	// ignores are belt and braces.
	FElysiumEntity* EnemyEntity = World != nullptr ? World->Resolve(Senses.Memory.Enemy) : nullptr;
	if (EnemyEntity == nullptr)
	{
		return false;
	}
	const FElysiumCombatCharacter* Enemy = EnemyEntity->AsCombatCharacter();
	if (Enemy == nullptr)
	{
		return false;
	}
	const FVector EyeCm = EnemyEntity->EyePosition();

	FVector BoxMin = FVector::ZeroVector;
	FVector BoxMax = FVector::ZeroVector;
	if (!bUseHitbox || !ComputeHitboxSurroundingBox(BoxMin, BoxMax))
	{
		FVector HullMins = FVector::ZeroVector;
		FVector HullMaxs = FVector::ZeroVector;
		RetailHullExtents(HullKind, EElysiumHullExtents::Full, HullMins, HullMaxs);   // family Motor's seam: the zero box
		BoxMin = OriginCm + HullMins * GPositionsTailU;
		BoxMax = OriginCm + HullMaxs * GPositionsTailU;
	}

	// `(**(code **)(*DAT_1070b244 + 4))(mins.z, maxs.z)` — `VEngineRandom001::RandomFloat`. Named
	// decision: the draw goes on `EElysiumRngStream::NpcSchedule`, this runtime's NPC decision
	// stream, beside the Andrei teleport coin flip. Retail draws it UNCONDITIONALLY, before any
	// candidate is tested, so the stream advances once per call whatever the answer.
	const float RandomZ = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(
		static_cast<float>((BoxMin - ExtentsCm).Z), static_cast<float>((BoxMax + ExtentsCm).Z));
	const FEnemySightCandidates Candidates =
		EnemySightCandidatesOf(BoxMin, BoxMax, ExtentsCm, RandomZ);

	const FVector Points[3] = { Candidates.MidCm, Candidates.MinCm, Candidates.MaxCm };
	for (const FVector& Point : Points)
	{
		if (!bSkipViewCone && !EnemyInViewCone(*EnemyEntity, Point))
		{
			continue;
		}
		// The engine ray, mask `0x4081`. The port's one solid-world query is the embodiment's
		// `QueryLineOfSight`, which traces the same semantics (world geometry only, characters not
		// occluders) on `ELYSIUM_USE_CHANNEL` — a **named modernization** of the mask, kept because
		// Source content masks are not portable to Unreal's channel set. Its headless answer is
		// "clear", which is the arm that makes a boss believe it is seen.
		const IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr;
		if (Embodiment == nullptr || Embodiment->QueryLineOfSight(EyeCm, Point))
		{
			return true;
		}
	}
	return false;
}

bool FElysiumNpc::EnemyCouldSeeHullWerewolf(const FVector& OriginCm, bool bSkipViewCone,
	bool bUseHitbox, const FVector& ExtentsCm)
{
	// `CNPC_VWerewolf::EnemyCouldSeeHull` `0x103da230`, slot 617 for the Werewolf: two gates in
	// front of `thunk_FUN_10366510`, which is the base body above with every argument forwarded.
	//
	//     if (!ConVar(DAT_1093d694).GetBool()) return false;
	//     if (GetEnemy() && !GetEnemy()->vtable[0x278]()) return false;
	//     return CNPC_VBaseBoss::EnemyCouldSeeHull( ... );
	//
	// Note the asymmetry in the second gate: a Werewolf with NO enemy skips it and reaches the base
	// body, which then refuses for want of an enemy anyway — so the gate only matters when there IS
	// one and it answers false.
	if (!WerewolfSightConVar())
	{
		return false;
	}
	const FElysiumEntity* EnemyEntity =
		World != nullptr ? World->Resolve(Senses.Memory.Enemy) : nullptr;
	if (EnemyEntity != nullptr && !EnemySightPredicate(*EnemyEntity))
	{
		return false;
	}
	return EnemyCouldSeeHull(OriginCm, bSkipViewCone, bUseHitbox, ExtentsCm);
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
	// `ConVar` `DAT_1093d52c`, read as `IsCommand() ? 0.0f : m_fValue (+0x28)`. **SEAM**, name and
	// default **unrecovered** — uninitialised `.data`, no constructor in the corpus. Answering 0 is
	// retail's own arm for a cvar it cannot read, and it makes the Werewolf's tolerance the plain
	// `m_flGoalTolerance`.
	return 0.f;
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

FElysiumNpc::EChaseTranslateShape FElysiumNpc::ChaseTranslateShape() const
{
	// Slot 563's census row for the class this NPC IS, mapped onto the six recovered shapes. The
	// address is the gate as well as the record: an override this family did not recover is not one
	// of these, and it takes the Troika body rather than a guess.
	const FElysiumNpcClassSlot* Row = ElysiumNpcKernelClass::OverrideOf(RetailClass(), 563);
	const TCHAR* BodyAddress = Row != nullptr ? Row->Address : nullptr;
	if (BodyAddress == nullptr)
	{
		return EChaseTranslateShape::Troika;   // `0x10295300`, the line every spawnable species is on
	}
	if (FCString::Strcmp(BodyAddress, TEXT("0x10289f20")) == 0)
	{
		return EChaseTranslateShape::Base;
	}
	if (FCString::Strcmp(BodyAddress, TEXT("0x1035f5c0")) == 0
		|| FCString::Strcmp(BodyAddress, TEXT("0x10384760")) == 0)
	{
		return EChaseTranslateShape::OffsetOnly;
	}
	if (FCString::Strcmp(BodyAddress, TEXT("0x10368ee0")) == 0)
	{
		return EChaseTranslateShape::Empty;
	}
	if (FCString::Strcmp(BodyAddress, TEXT("0x10392c40")) == 0
		|| FCString::Strcmp(BodyAddress, TEXT("0x103ba640")) == 0)
	{
		return EChaseTranslateShape::GoalToleranceLead;
	}
	if (FCString::Strcmp(BodyAddress, TEXT("0x103d9e00")) == 0)
	{
		return EChaseTranslateShape::GoalToleranceWerewolfLead;
	}
	return EChaseTranslateShape::Troika;
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
	// `TranslateEnemyChasePositionSpecies`.
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
		*ToleranceOut = static_cast<float>(HullMaxs.Y - HullMins.Y) * GPositionsTailU;
	}
}

void FElysiumNpc::TranslateEnemyChasePositionSpecies(FElysiumEntity* Enemy,
	FVector& ChasePositionCm, float& Tolerance, float& SecondTolerance)
{
	// The other seven fills of slot 563, dispatched through the census rather than by a name
	// compare. All of them share the nav-type-2 gate and the eye-minus-origin offset; what differs
	// is entirely what happens on the OTHER arm.
	const EChaseTranslateShape Shape = ChaseTranslateShape();
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
			Tolerance = static_cast<float>(HullMaxs.Y - HullMins.Y) * GPositionsTailU;
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

// --- `CNPC_VWerewolf`'s teleport pair -----------------------------------------------------------

bool FElysiumNpc::WerewolfTeleportSoundConVar()
{
	// `(**(code **)(*DAT_1093f73c + 4))()` / `DAT_1093f73c[0xb]` — the same inlined
	// `ConVar::GetBool()` shape. Recorded as UNRECOVERED in `docs/vtmb/npc-ai/lifecycle.md`;
	// **SEAM**, answering false, which is the arm that plays nothing.
	return false;
}

void FElysiumNpc::TeleportOut()
{
	// `CNPC_VWerewolf::TeleportOut` `0x103d4a60`, in retail's order — and the order matters, because
	// the effects bit and the solid flag are raised BEFORE the relink, so the engine sees a
	// non-solid invisible body on the same frame:
	//
	//     m_flTimeTeleportedOut = gpGlobals->curtime;      // +0x66f0
	//     vtable[0x108]();                                 // slot 66 Hide
	//     m_fEffects |= 0x20;                              // EF_NODRAW, +0x019c
	//     m_Collision.AddSolidFlags( 0x4 );                // FSOLID_NOT_SOLID, +0x02b4
	//     Relink();
	//     m_??? (+0x66ac) = 0;
	//     ClearTeleportHint();
	//     m_hintFlags (+0x66e8) = 0;
	//     m_OnTeleportOut.FireOutput( GetEnemy(), this, 0 );
	//     if (ConVar(DAT_1093f73c).GetBool())
	//         EmitSound( CSingleUserRecipientFilter(GetEnemy()->entindex()),
	//                    "dev/ww_tele_out.wav", 1.0, level 100 );
	//
	// The sound's recipient filter is the ENEMY, not the werewolf — a dev cue played at whoever it
	// is fighting. Single-player makes that the player, which is why the wav is audible at all.
	WerewolfTimeTeleportedOut = World != nullptr ? World->NowSeconds() : 0.0;
	Hide();                                     // slot 66, the generated virtual
	EffectsWord |= GPositionsTailEffectNoDraw;
	SolidFlagsWord |= GPositionsTailSolidNotSolid;
	// `CBaseEntity::Relink` — this runtime has no spatial partition to relink into; the visibility
	// and solidity the two words above stand for are the visual layer's, and nothing reads them yet.
	WerewolfWord66ac = 0;
	ClearTeleportHint();                        // family Hints' `0x103d4760`
	WerewolfHintFlags = 0;                      // family Hints' +0x66e8
	FireOutput(FName(TEXT("OnTeleportOut")), Senses.Memory.Enemy);
	if (WerewolfTeleportSoundConVar())
	{
		PlayTeleportSound(TEXT("dev/ww_tele_out.wav"));
	}
}

void FElysiumNpc::TeleportIn()
{
	// `CNPC_VWerewolf::TeleportIn` `0x103d4d60` — the mirror, with the position FIRST:
	//
	//     PositionAtHint( m_pTeleportHint );               // +0x66b0
	//     vtable[0x10c]();                                 // slot 67 Unhide
	//     m_fEffects &= ~0x20;
	//     m_Collision.RemoveSolidFlags( 0x4 );
	//     Relink();
	//     m_flLastSeen (+0x66ec) = gpGlobals->curtime;
	//     m_OnTeleportIn.FireOutput( GetEnemy(), this, 0 );
	//     if (ConVar(DAT_1093f73c).GetBool()) EmitSound( ..., "dev/ww_tele_in.wav", ... );
	//
	// Note the asymmetry with `TeleportOut`: the hint is NOT cleared on the way in and the stamp
	// written is `+0x66ec`, the same word `UpdateConditionCanTeleport` stamps when the enemy could
	// see the hull — so arriving counts as "just been seen" and the teleport cooldown starts over.
	FHintWords Hint;
	if (HintWords(TeleportHintNode, Hint))
	{
		PositionAtHint(Hint);                   // family Hints' `0x103d6280`
	}
	Unhide();                                   // slot 67, the generated virtual
	EffectsWord &= ~GPositionsTailEffectNoDraw;
	SolidFlagsWord &= ~GPositionsTailSolidNotSolid;
	WerewolfLastSeenTime = World != nullptr ? World->NowSeconds() : 0.0;
	FireOutput(FName(TEXT("OnTeleportIn")), Senses.Memory.Enemy);
	if (WerewolfTeleportSoundConVar())
	{
		PlayTeleportSound(TEXT("dev/ww_tele_in.wav"));
	}
}

void FElysiumNpc::PlayTeleportSound(const TCHAR* Rel)
{
	// The shared tail of both halves. The `EmitSound_t` the two bodies fill carries the wav, volume
	// `1.0` (`0x3f800000`) and sound level `0x64` (100 dB); its channel word is `0`, `CHAN_AUTO`.
	// The remaining two words of the record are zero and their FIELDS are unrecovered — the struct
	// is only ever written here, so nothing states which of `m_nFlags` / `m_nPitch` / `m_pOrigin`
	// they are. `IElysiumAudio::PlayBodySound` is this runtime's `EmitSound(..., CHAN_*, ...)` seam.
	IElysiumAudio* Audio = World != nullptr ? World->Audio() : nullptr;
	if (Audio == nullptr)
	{
		return;
	}
	FElysiumBodySound Sound;
	Sound.Rel = Rel;
	Sound.Volume = GPositionsTailRetailOne;
	Sound.SoundLevelDb = 100;
	Sound.Channel = EElysiumSoundChannel::Auto;
	Audio->PlayBodySound(Handle, Sound);
}

// --- `CNPC_VSheriffMan::KillTeleportBats` `0x103b0560` ------------------------------------------

void FElysiumNpc::KillTeleportBats()
{
	//     CBaseEntity* swarm = m_hTeleportSwarm;           // +0x66d0
	//     if (swarm resolves) thunk_FUN_101cd940( swarm ); // UTIL_Remove
	//     m_hTeleportSwarm = INVALID_EHANDLE;
	//
	// The handle is invalidated whether or not it resolved, which is what makes this idempotent and
	// safe to call from a death or a state change as well as from the teleport itself.
	FElysiumEntity* Swarm = World != nullptr ? World->Resolve(SheriffTeleportSwarm) : nullptr;
	if (Swarm != nullptr)
	{
		// `thunk_FUN_101cd940` — `UTIL_Remove`. This runtime's equivalent is the entity's own Kill.
		Swarm->Kill();
	}
	SheriffTeleportSwarm = FElysiumEntityHandle();
}

// --- `CNPC_VWerewolf::UpdateConditionCanTeleport` `0x103cc0d0` ----------------------------------

float FElysiumNpc::WerewolfTeleportDelayConVar()
{
	// `ConVar` `DAT_1093d414`, `IsCommand() ? 0.0f : m_fValue (+0x28)`. Its two readers are this
	// body and its own thunk, and nothing constructs it — **unrecovered**. **SEAM** answering 0,
	// which OPENS the gate for any elapsed time above zero.
	return 0.f;
}

void FElysiumNpc::UpdateConditionCanTeleport()
{
	// The whole body, and note which way round the condition is written — 29c's one-line walk had it
	// backwards. The condition is CLEARED first and SET only at the end:
	//
	//     ClearCondition( 0x77 );                          // thunk_FUN_10269b50
	//     if (!IsViewable()) return;                       // slot 163, vtable +0x28c
	//     bool close = m_flPlayerDist < 800.0;             // _DAT_10457ac4, +0x6264
	//     Vector bone, ang;  GetBonePosition( "Bip01", &bone, &ang );
	//     if (IsViewable() && EnemyCouldSeeHull( bone, close, true, vec3_origin ))   // slot 617
	//         m_flLastSeen (+0x66ec) = gpGlobals->curtime;
	//     float since = gpGlobals->curtime - m_flLastSeen;  if (since < 0) since = 0;
	//     if (ConVar(DAT_1093d414).GetFloat() < since &&
	//         m_fDistA (+0x66d0) + m_fDistB (+0x66cc) + 100.0 < m_flPlayerDist)      // _DAT_10450564
	//     {
	//         ConVar(DAT_10924a6c).GetXXX();               // result discarded — a folded log gate
	//         SetCondition( 0x77 );                        // thunk_FUN_10269a20
	//     }
	//
	// So the werewolf may teleport when it has been out of the enemy's sight for long enough AND the
	// player is further away than the two stored distances plus a hundred units. `IsViewable` is
	// asked TWICE — once as the entry gate and again immediately before the sight test — which is
	// retail's own redundancy and is reproduced.
	//
	// `DAT_1070d1b0/b4/b8`, the extents handed to slot 617, are `vec3_origin`: `staticinit_101370b0`
	// writes three zeros into them and 369 bodies read them. The box is therefore NOT inflated.
	Cognition.Conditions.Clear(CondCanTeleport);
	if (!IsViewable())
	{
		return;
	}
	const float PlayerDist = Senses.Memory.ClosestPlayerDistanceCm;
	const bool bPlayerClose = PlayerDist < WerewolfCloseEnough * GPositionsTailU;

	// `CBaseAnimating::GetBonePosition01("Bip01", &pos, &ang)`. **SEAM**: no bone sampling reaches
	// the kernel here, so the sight point is this body's own origin — the bone's parent transform
	// and retail's own fallback when a model has no such bone.
	const FVector BonePositionCm = Origin;

	if (IsViewable()
		&& EnemyCouldSeeHull(BonePositionCm, bPlayerClose, /*bUseHitbox*/ true,
			/*ExtentsCm*/ FVector::ZeroVector))
	{
		WerewolfLastSeenTime = World != nullptr ? World->NowSeconds() : 0.0;
	}

	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	float Since = static_cast<float>(Now - WerewolfLastSeenTime);
	if (Since < GPositionsTailRetailZero)
	{
		Since = 0.f;
	}
	if (WerewolfTeleportDelayConVar() < Since
		&& WerewolfTeleportDistanceB + WerewolfTeleportDistanceA
			+ WerewolfTeleportDistanceFloor * GPositionsTailU < PlayerDist)
	{
		// `(**(code **)(*DAT_10924a6c + 4))()` with its result discarded — a `ConVar` read whose
		// only consumer the compiler folded away, almost certainly a `DevMsg` gate. Reproduced as
		// the nothing it is, and named so a reader does not go looking for it again.
		Cognition.Conditions.Set(CondCanTeleport);
	}
}

// --- Slot 389's `CNPC_VTzimisce` override `0x103bfd80` ------------------------------------------

float FElysiumNpc::TzimisceAimConVar(int32 Which)
{
	// 0 `DAT_1093cbac` (the UP term), 1 `DAT_1093cbf4` (RIGHT), 2 `DAT_1093cc3c` (FORWARD), each
	// read as `IsCommand() ? 0.0f : m_fValue (+0x28)`. **SEAM**: all three live in uninitialised
	// `.data` with no constructor in the corpus, so their names and defaults are **unrecovered**;
	// 0.0 is retail's own answer for a cvar it cannot read, and with all three at zero both activity
	// arms below answer `SrcCm` exactly as the base body does.
	(void)Which;
	return 0.f;
}

FVector FElysiumNpc::TzimisceAimOffset(const FVector& SrcCm, const FVector& Forward,
	const FVector& Right, const FVector& Up, float ForwardScale, float RightScale, float UpScale,
	bool bAddRight)
{
	// The two arms of `0x103bfd80`, which differ in ONE sign and nothing else:
	//
	//     m_Activity == 0x106:  out = src + forward*F - right*R + up*Up
	//     m_Activity == 0x107:  out = src + forward*F + right*R + up*Up
	//
	// Retail spells the second arm as two statements — the sum without the up term, then
	// `Vector::operator+` (`FUN_1011e060`) with it — and the first as one expression; the result is
	// the same and the split is the compiler's.
	const FVector RightTerm = Right * RightScale;
	return SrcCm + Forward * ForwardScale + (bAddRight ? RightTerm : -RightTerm) + Up * UpScale;
}

bool FElysiumNpc::WeaponShootPositionTzimisce(const FVector& SrcCm, FVector& OutCm) const
{
	// `CNPC_VTzimisce::vfunc389` `0x103bfd80` — the species branch of slot 389. Two activities get
	// an offset aim origin and everything else falls through to
	// `CBaseCombatCharacter::Weapon_ShootPosition`, which is what `false` says here.
	//
	//     AngleVectors( GetAbsAngles(), &forward, &right, &up );   // vtable +0x374, slot 221
	if (ActivityNumber != 0x106 && ActivityNumber != 0x107)
	{
		return false;
	}
	FVector Fwd = FVector::ZeroVector;
	FVector Rgt = FVector::ZeroVector;
	FVector Upv = FVector::ZeroVector;
	RetailAngleVectors(Angles, Fwd, Rgt, Upv);
	OutCm = TzimisceAimOffset(SrcCm, Fwd, Rgt, Upv, TzimisceAimConVar(2), TzimisceAimConVar(1),
		TzimisceAimConVar(0), /*bAddRight*/ ActivityNumber == 0x107);
	return true;
}
