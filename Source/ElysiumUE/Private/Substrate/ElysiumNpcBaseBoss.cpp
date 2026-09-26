#include "Substrate/ElysiumNpcBaseBoss.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcDebugShared.h"
#include "Substrate/ElysiumNpcPositions2Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	constexpr float RetailHalf = ElysiumNpcTunables::Half;
}

const FElysiumNpcClass* FElysiumNpcBaseBoss::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 76: `0x10366290`, which chains `CAI_BaseNPC::DrawDebugStatOverlays` (`0x102775e0`) directly.
void FElysiumNpcBaseBoss::DrawDebugStatOverlays()
{
	BossDrawDebugStatOverlays();
}

// --- Moved from `ElysiumNpcBosses.cpp` (story 5 step 4) ---

bool FElysiumNpcBaseBoss::BossBlacklistHolds(const FElysiumEntity* Candidate) const
{
	// NO LONGER A SEAM. Family **Species** landed `0x10366400` as `FUN_10366400` over the
	// `CNPC_VBaseBoss::m_BlacklistedEntities` array (+0x665c) it also declared, so the walk over
	// the blacklist is real; an empty blacklist still admits every candidate, which is what the
	// permissive arm was standing in for. `const_cast` because the retail body prunes expired rows
	// as it walks, which is a write this query has always made.
	return const_cast<FElysiumNpcBaseBoss*>(this)->FUN_10366400(Candidate);
}

// --- Moved from `ElysiumNpcDebug.cpp` (story 5 step 4) ---

void FElysiumNpcBaseBoss::BossDrawDebugStatOverlays()
{
	// `0x10366290`, thirty-six bytes and all of `CNPC_VBaseBoss#76`:
	//
	//     Msg("Dist to player: %.3f", *(float *)(this + 0x6264));
	//     JMP CAI_BaseNPC::DrawDebugStatOverlays;     // 0x102775e0, a TAIL call
	//
	// The tail call is to the BASE body and not to `CAI_BaseNPCTroika`'s, so a boss never gets the
	// expression/gesture dump even when it has a dialogue. That is the recovered dispatch and it is
	// reproduced.
	//
	// `+0x6264` is `FElysiumNpcMemory::ClosestPlayerDistanceCm` in the shape map; retail's word is
	// SOURCE units, so the print divides.
	EmitDebugMsg(TEXT("Dist to player: %.3f"), FString::Printf(TEXT("Dist to player: %.3f"),
		Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U));
	BaseDrawDebugStatOverlays();
}

// --- Moved from `ElysiumNpcPositions2.cpp` (story 5 step 4) ---

FElysiumNpc::FEnemySightCandidates FElysiumNpcBaseBoss::EnemySightCandidatesOf(const FVector& BoxMinCm,
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

// --- Moved from `ElysiumNpcSpecies.cpp` (story 5 step 4) ---

void FElysiumNpcBaseBoss::FUN_103662d0(const FElysiumEntityHandle& Entity, float Seconds)
{
	// `0x103662d0`, the ADD. Retail is a hand-inlined `CUtlVector<{EHANDLE, float}>::InsertBefore`
	// at the END of the list:
	//
	//     expiry = gpGlobals->curtime + param_2;
	//     n = m_Count (+0x6668);  cap = m_AllocCount (+0x6660);
	//     if (cap < n + 1 && m_GrowSize (+0x6664) != -1) {
	//         while (cap < n + 1) cap = (cap == 0) ? 4 : (grow == 0 ? cap * 2 : cap + grow);
	//         m_AllocCount = cap;
	//         m_Elements = m_Elements ? Plat_Realloc(m_Elements, cap * 8) : Plat_Alloc(cap * 8);
	//     }
	//     m_ElementMirror (+0x666c) = m_Elements;
	//     m_Count = n + 1;
	//     memmove(&e[n+1], &e[n], ((n + 1) - n - 1) * 8);        // always zero bytes on an append
	//     e[n] = { param_1, expiry };
	//
	// The `memmove` moves `(count + 1 - index - 1) * 8` bytes with `index == count`, i.e. **zero**,
	// so the insert is an append and the shift is dead on every call. Family Anim recorded the same
	// dead `0x10430fa0` on its own add. `TArray::Emplace` is that append; the doubling growth and
	// the 4-row first allocation are not observable through any read, so they are not reproduced
	// and this says so rather than carrying a capacity word nothing can see.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	BossBlacklist.Emplace(FBlacklistedEntity{ Entity, Now + static_cast<double>(Seconds) });
}

int32 FElysiumNpcBaseBoss::FUN_10366490(const FElysiumEntity* Candidate) const
{
	// `0x10366490`, the INDEX-OF. Walks `m_Count` rows, resolves each row's `EHANDLE` through the
	// global entity table (`PTR_DAT_10566458`, sentinel `0xffffffff`, serial in the top 19 bits)
	// and answers the index whose RESOLVED POINTER equals the argument, else -1.
	//
	// Two retail details that are kept: a row whose handle is dead resolves to **0**, so a NULL
	// argument would match the first dead row — and this port reproduces that, because the callers
	// below pass `0x10366400`'s argument straight through and one of them can be null.
	for (int32 i = 0; i < BossBlacklist.Num(); ++i)
	{
		const FElysiumEntity* Resolved = World != nullptr
			? World->Resolve(BossBlacklist[i].Entity) : nullptr;
		if (Resolved == Candidate)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

bool FElysiumNpcBaseBoss::FUN_10366400(const FElysiumEntity* Candidate)
{
	// `0x10366400`, the TEST-AND-EXPIRE:
	//
	//     i = IndexOf(param_1);                      // thunk 0x10366490
	//     if (i == -1) return false;                 // <- the low byte of -1 is 0xff, see below
	//     if (curtime < e[i].expiry) return true;    // still blacklisted
	//     if (0 < m_Count) { memmove(&e[i], &e[m_Count - 1], 8); m_Count -= 1; }
	//     return false;
	//
	// **The `-1` arm is a retail quirk and it is reproduced.** The body returns `uVar4 & 0xffffff00`
	// on the miss path, where `uVar4` is the index — so for `i == -1` the returned byte is
	// `0xffffffff & 0xffffff00`'s low byte, i.e. **0**, and the caller reads false. For any other
	// miss (expired) the low byte is likewise cleared. So every non-"still live" path answers false
	// and the only true is the unexpired one; the sign of the index never leaks out.
	//
	// The removal is a SWAP-REMOVE — the LAST row is memmoved over the one being dropped and the
	// count decremented — so the list's order is not preserved and a later index-of can answer a
	// different number for the same entity. `RemoveAtSwap` is exactly that.
	const int32 Index = FUN_10366490(Candidate);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (Now < BossBlacklist[Index].ExpiresAt)
	{
		return true;
	}
	if (BossBlacklist.Num() > 0)
	{
		BossBlacklist.RemoveAtSwap(Index);
	}
	return false;
}

bool FElysiumNpcBaseBoss::FUN_103c1b10()
{
	// `0x103c1b10`, `CNPC_VTzimisceHeadClaw`'s slot 602:
	//
	//     range = MeleeRangeConVar();                    // (*DAT_10924a1c + 4)() ? 0.0 : +0x28
	//     if (range + range < m_flEnemyDist && !CoordinatorHasRoom(m_pAttackCoordinator))  // 0x1025db50
	//         return true;
	//     return CoordinatorDoesNotHoldMe(m_pAttackCoordinator, this);                     // 0x1025de90
	//
	// Eighty-three bytes against the Troika line's far larger body, and it keeps only the FAR arm:
	// the frenzied-bit gate, the follower-boss gate, the null-coordinator gate, the
	// `HasUsableRangedWeapon()` split and the `m_flMeleeMustLeaveTimer` deadline are all gone. A
	// head claw leaves melee when it is out of double melee range and the coordinator is full, or
	// when the coordinator is not holding it — and never because a timer ran out.
	//
	// `range + range` is the DOUBLED melee range, the same doubling the Troika line uses.
	// `MeleeRangeUnits()` (family TroikaHelpers) reads the same ConVar,
	// `debug_melee_advance_combatmove_dist` "100", so the doubled range is 200 units;
	// `MeleeCoordinatorHasRoom()` answers false, so past 200 units that arm returns true.
	//
	// The last line negates `MeleeCoordinatorHoldsMe()` because that seam is spelled "is this NPC IN
	// the array" while `0x1025de90` answers "is it NOT" — family TroikaHelpers' own `Slot602` writes
	// the same `!`, and this body calls the seam the same way so the two cannot disagree. With an
	// absent coordinator the seam answers false, so this answers TRUE: nothing holds this NPC, so
	// nothing stops it leaving melee.
	const float DoubledRange = MeleeRangeUnits() + MeleeRangeUnits();
	if (DoubledRange < ScheduleHost.EnemyDistUnits && !MeleeCoordinatorHasRoom())
	{
		return true;
	}
	return !MeleeCoordinatorHoldsMe();
}

// --- From `FElysiumNpcWerewolf` (the move manifest's corrected owner) ---

bool FElysiumNpcBaseBoss::EnemyCouldSeeHull(const FVector& OriginCm, bool bSkipViewCone, bool bUseHitbox,
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
		BoxMin = OriginCm + HullMins * NpcKernelPositions2Shared::GPositionsTailU;
		BoxMax = OriginCm + HullMaxs * NpcKernelPositions2Shared::GPositionsTailU;
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

// --- From `FElysiumNpcWerewolf` (the move manifest's corrected owner) ---

bool FElysiumNpcBaseBoss::EnemyInViewCone(const FElysiumEntity& Enemy, const FVector& PointCm)
{
	// The enemy's slot 362 `FInViewCone(const Vector&)`, dispatched on the ENEMY and not on this
	// NPC. **SEAM**: no port body answers a view cone for an arbitrary world point, so this answers
	// false and every candidate is refused unless the caller asked to skip the cone.
	(void)Enemy;
	(void)PointCm;
	return false;
}
