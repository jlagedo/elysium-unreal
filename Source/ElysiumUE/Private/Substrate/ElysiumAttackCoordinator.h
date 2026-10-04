#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"

class FElysiumEntity;
class FElysiumEntityWorld;
class FElysiumNpc;

// Spec 0002 V11-1: retail's attack coordinator (`docs/vtmb/npc-ai/social.md` § "The attack
// coordinator object — `0x1025d880` … `0x1025df40`"). Three plain heap objects of `0x28` bytes --
// no vtable, not entities, never saved -- built by `0x1025d880` as the last statement of the
// `CWorld` constructor (`DAT_1090fbec` "Normal", `DAT_1090fbf0` "Player", `DAT_1090fbf4` "Boss", cap
// 2 each) and freed by `0x1025d940` as the first of its destructor. Layout: `+0x00` cap, `+0x04`
// `EHANDLE*`, `+0x08` allocated, `+0x0c` grow, `+0x10` count, `+0x18` `char name[16]`.
//
// The list is who is IN melee: slot 599 / 600 register, slot 601 is the only remover. An
// unresolvable handle is skipped by every walk and never purged.

/** `0x1025dca0`'s second argument (`char useDist`), as the two values retail passes. */
enum class EElysiumCoordinatorEvict : uint8
{
	/** `useDist == 0` (slot 600): the threshold starts at 0.0, so the farthest member is evicted
	 *  whenever any member's `m_flEnemyDist` is above zero. */
	FarthestMember,
	/** `useDist == 1` (`0x1025db70`'s full arm): the threshold starts at the candidate's own
	 *  `m_flEnemyDist`, so only a member strictly farther than the candidate is evicted. */
	FartherThanCandidate,
};

class FElysiumAttackCoordinator
{
public:
	/** `0x1025d9d0(this, name, cap)`. The world resolves the members' handles; it owns this object. */
	FElysiumAttackCoordinator(const FElysiumEntityWorld& InWorld, const TCHAR* InName, int32 InCap);

	/** `0x1025db50`: `count < cap`. */
	bool HasRoom() const;

	/** `0x1025db70`: already listed -> true, no insert; room -> append, true; full ->
	 *  `AddOrEvict(npc, FartherThanCandidate)`. A null NPC answers false (retail recurses: see the
	 *  body). */
	bool Add(const FElysiumNpc* Npc);

	/** `0x1025dca0`: room -> `Add`. Full: the member with the strictly greatest `m_flEnemyDist`
	 *  (`+0x6268`) above the threshold is released and the candidate added; none -> false, nothing
	 *  changed. Ties and NaN keep the earlier member. */
	bool AddOrEvict(const FElysiumNpc* Npc, EElysiumCoordinatorEvict Threshold);

	/** `0x1025ddd0`: the first entry resolving to the NPC is overwritten by the last, `count--`
	 *  (order not preserved). Absent or null: no-op. */
	void Release(const FElysiumNpc* Npc);

	/** `0x1025de90`: true when the NPC is ABSENT from the list (or null, or the list is empty). */
	bool IsAbsent(const FElysiumNpc* Npc) const;

	/** `0x1025df40`: 0 when either is null or `count < 2`; else, over every OTHER member, the signed
	 *  `UTIL_AngleDiff` between this NPC's bearing from the enemy and the member's, keeping the
	 *  smallest magnitude from 360; `<= 0` -> -1, else 1. */
	int32 CircleSide(const FElysiumNpc* Npc, const FElysiumEntity* Enemy) const;

	/** `0x1025e120`: `this + 0x18`. */
	const FString& Name() const { return NameText; }

	int32 Cap() const { return CapCount; }
	int32 Num() const { return Members.Num(); }
	const TArray<FElysiumEntityHandle>& Handles() const { return Members; }

	/** Empties the list (the port's stand for `0x1025d940` + `0x1025d880` across a map's teardown:
	 *  every map starts with three empty coordinators). */
	void Reset();

private:
	const FElysiumEntity* ResolveMember(const FElysiumEntityHandle& Member) const;

	const FElysiumEntityWorld& World;
	FString NameText;                       // +0x18 char name[16]
	int32 CapCount = 0;                     // +0x00
	TArray<FElysiumEntityHandle> Members;   // +0x04 / +0x10
};
