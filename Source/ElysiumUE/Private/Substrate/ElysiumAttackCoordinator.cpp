#include "Substrate/ElysiumAttackCoordinator.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcFacingShared.h"
#include "Substrate/ElysiumNpcScheduleHost.h"

FElysiumAttackCoordinator::FElysiumAttackCoordinator(const FElysiumEntityWorld& InWorld,
	const TCHAR* InName, int32 InCap)
	: World(InWorld)
	, NameText(InName)   // 0x1025d9d0: the name copied into `this + 0x18`
	, CapCount(InCap)    // 0x1025d9d0: `*this = cap` (2 at all three sites of 0x1025d880)
{
}

const FElysiumEntity* FElysiumAttackCoordinator::ResolveMember(const FElysiumEntityHandle& Member) const
{
	// The EHANDLE resolve every body inlines: `h == 0xffffffff`, or the entity list's serial at
	// `PTR_DAT_10566458 + (h & 0x1fff) * 0xc + 8` differing from `h >> 0xd`, answers null.
	return Member.IsSet() ? World.Resolve(Member) : nullptr;
}

bool FElysiumAttackCoordinator::HasRoom() const
{
	return Members.Num() < CapCount;   // 0x1025db50: `this[4] < this[0]`
}

bool FElysiumAttackCoordinator::Add(const FElysiumNpc* Npc)
{
	// 0x1025db70. A NULL NPC falls to `0x1025dca0(NULL, 1)`, which with room calls this body again
	// (unbounded recursion) and when full reads `NULL + 0x6268`. **Crash arm not reproduced**: false.
	if (Npc == nullptr)
	{
		return false;
	}
	// 0x1025db70: the listed walk -- a member resolving to this NPC answers 1 with no insert.
	for (const FElysiumEntityHandle& Member : Members)
	{
		if (ResolveMember(Member) == Npc)
		{
#if !UE_BUILD_SHIPPING
			if (Npc->World && Npc->World->HasAiTraceSink()) Npc->World->EmitAiTrace(*Npc, FName(TEXT("script")), FString::Printf(TEXT("coordinator phase=idempotent name=%s count=%d cap=%d"), *NameText, Members.Num(), CapCount)); // 0x1025db70 actual existing-member arm
#endif
			return true;
		}
	}
	if (Members.Num() < CapCount)   // 0x1025db70: `this[4] < this[0]`
	{
		Members.Add(Npc->Handle);   // 0x1025d840 (the EHANDLE) appended at `count`, `count++`
#if !UE_BUILD_SHIPPING
		if (Npc->World && Npc->World->HasAiTraceSink()) Npc->World->EmitAiTrace(*Npc, FName(TEXT("script")), FString::Printf(TEXT("coordinator phase=admit name=%s count=%d cap=%d"), *NameText, Members.Num(), CapCount));
#endif
		return true;
	}
	return AddOrEvict(Npc, EElysiumCoordinatorEvict::FartherThanCandidate);   // 0x1025dca0(this, npc, 1)
}

bool FElysiumAttackCoordinator::AddOrEvict(const FElysiumNpc* Npc, EElysiumCoordinatorEvict Threshold)
{
	// 0x1025dca0: room -> 0x1025db70.
	if (Members.Num() < CapCount)
	{
		return Add(Npc);
	}
	// Full with a NULL candidate: retail reads `NULL + 0x6268` when `useDist` is set, and otherwise
	// evicts a member for a candidate `0x1025db70` then recurses on. **Crash arm not reproduced.**
	if (Npc == nullptr)
	{
		return false;
	}
	// 0x1025dca0: `fVar2 = _DAT_104454c4` (0.0), or the candidate's `m_flEnemyDist (+0x6268)` when
	// `useDist != 0`.
	float Best = Threshold == EElysiumCoordinatorEvict::FartherThanCandidate
		? Npc->ScheduleHost.EnemyDistUnits
		: 0.0f;
	int32 Evict = INDEX_NONE;
	for (int32 Index = 0; Index < Members.Num(); ++Index)
	{
		// An invalid or stale handle, or a null slot, is skipped (never purged).
		const FElysiumEntity* const Entity = ResolveMember(Members[Index]);
		const FElysiumNpc* const Member = Entity != nullptr ? Entity->AsNpc() : nullptr;
		if (Member == nullptr)
		{
			continue;
		}
		// `if (fVar2 < fVar3)`: an ORDERED strict less-than, so a tie and a NaN keep the earlier.
		const float Dist = Member->ScheduleHost.EnemyDistUnits;
		if (Best < Dist)
		{
			Best = Dist;
			Evict = Index;
		}
	}
	if (Evict == INDEX_NONE)
	{
#if !UE_BUILD_SHIPPING
		if (Npc->World && Npc->World->HasAiTraceSink()) Npc->World->EmitAiTrace(*Npc, FName(TEXT("script")), FString::Printf(TEXT("coordinator phase=refuse name=%s count=%d cap=%d"), *NameText, Members.Num(), CapCount)); // 0x1025dca0 unchanged full list
#endif
		return false;   // `return uVar5 & 0xffffff00`: nobody farther, nothing changed
	}
	// 0x1025dca0: `0x1025ddd0(this, resolved)` then `0x1025db70(this, npc)`.
	const FElysiumEntity* const Evicted = ResolveMember(Members[Evict]);
	Release(Evicted != nullptr ? Evicted->AsNpc() : nullptr);
	return Add(Npc);
}

void FElysiumAttackCoordinator::Release(const FElysiumNpc* Npc)
{
	// 0x1025ddd0: null -> return.
	if (Npc == nullptr)
	{
		return;
	}
	for (int32 Index = 0; Index < Members.Num(); ++Index)
	{
		if (ResolveMember(Members[Index]) == Npc)
		{
			// `memmove(&list[i], &list[count - 1], 4); count--`: the LAST entry over the found one.
			Members.RemoveAtSwap(Index);
#if !UE_BUILD_SHIPPING
			if (Npc->World && Npc->World->HasAiTraceSink()) Npc->World->EmitAiTrace(*Npc, FName(TEXT("script")), FString::Printf(TEXT("coordinator phase=release name=%s count=%d cap=%d"), *NameText, Members.Num(), CapCount)); // 0x1025ddd0 actual removal
#endif
			return;
		}
	}
}

bool FElysiumAttackCoordinator::IsAbsent(const FElysiumNpc* Npc) const
{
	// 0x1025de90: null, or an empty list, answers 1; a member resolving to the NPC answers 0.
	if (Npc != nullptr)
	{
		for (const FElysiumEntityHandle& Member : Members)
		{
			if (ResolveMember(Member) == Npc)
			{
				return false;
			}
		}
	}
	return true;
}

int32 FElysiumAttackCoordinator::CircleSide(const FElysiumNpc* Npc, const FElysiumEntity* Enemy) const
{
	// 0x1025df40: `npc == NULL || enemy == NULL || count < 2` -> 0.
	if (Npc == nullptr || Enemy == nullptr || Members.Num() < 2)
	{
		return 0;
	}
	// The bearing FROM the enemy TO a body, degrees: `fpatan(pos.y - enemy.y, pos.x - enemy.x) *
	// _DAT_1046a530` (57.29578) over the slot-220 (`+0x370`) origins. Source's Y is this world's -Y;
	// the unit (cm against Source units) cancels in the ratio.
	auto BearingOf = [Enemy](const FElysiumEntity& Body)
	{
		const double SourceDy = -(Body.Origin.Y - Enemy->Origin.Y);
		const double SourceDx = Body.Origin.X - Enemy->Origin.X;
		return FMath::RadiansToDegrees(static_cast<float>(FMath::Atan2(SourceDy, SourceDx)));
	};
	float Best = 360.0f;   // `param_2 = 0x43b40000`
	const float Mine = BearingOf(*Npc);
	for (const FElysiumEntityHandle& Member : Members)
	{
		const FElysiumEntity* const Other = ResolveMember(Member);
		if (Other == Npc)
		{
			continue;
		}
		// Retail dereferences a stale member here (`piVar6 == NULL` is `!= npc`, then
		// `(*piVar6 + 0x370)()`). **Crash arm not reproduced**: the member is skipped.
		if (Other == nullptr)
		{
			continue;
		}
		// `UTIL_AngleDiff 0x1013d580(mine, theirs)`; `if (ABS(diff) < ABS(best)) best = diff`.
		const float Diff = NpcKernelFacingShared::FacingRetailAngleDiff(Mine, BearingOf(*Other));
		if (FMath::Abs(Diff) < FMath::Abs(Best))
		{
			Best = Diff;
		}
	}
	// `if (best <= _DAT_104454c4) return -1; return 1`.
	return Best <= 0.0f ? -1 : 1;
}

void FElysiumAttackCoordinator::Reset()
{
	Members.Reset();
}
