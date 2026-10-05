#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntityHandle.h"

class FElysiumEntityWorld;
class FElysiumEntity;
class FElysiumNpc;
class FElysiumNpcBase;
struct FElysiumSaveArchive;

// 0x103167f0 / 0x103169a0: named hook for CAI_Memory +4's squad owner, absent in substrate.
// A future squad supplies its AND-of-members/fanout; no squad service is fabricated here.
class IElysiumEnemyMemorySquadOwner
{
public:
	virtual ~IElysiumEnemyMemorySquadOwner() = default;
	virtual bool MayRemove(FElysiumEntity* Target) = 0; // 0x103167f0: member slot54 AND
	virtual void Removed(FElysiumEntity* Target, FVector Lkp, FVector RecordVector,
		const TCHAR* FunctionTag) = 0; // 0x103169a0: member slot56 fanout
};

// CAI_Memory's observed-actor record list. It is deliberately separate from the committed enemy:
// the list is perception's admission store, while `BaseMemory.Enemy` is the sticky choice made
// from it. Until squads land this is per NPC; R17 replaces member ownership with the squad's one
// shared CAI_Memory store, matching retail's `m_pEnemies` redirection, without a second authority.
struct FElysiumNpcEnemyMemoryRecord
{
	FElysiumEntityHandle Handle;
	FVector LastPosition = FVector::ZeroVector;
	FVector Anchor = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	double LastSeenTime = -1.0;
	int32 LastNavNode = INDEX_NONE;
	int32 AnchorNavNode = INDEX_NONE;
	bool bPositionOnly = false;
	bool bEluded = false;
};

class FElysiumNpcEnemyMemory
{
public:
	void BindOwner(FElysiumNpcBase& StoreOwner); // 0x102df320 +0: creation, never victim inference
	void BindSquadOwner(IElysiumEnemyMemorySquadOwner* InSquadOwner,
		FElysiumEntityWorld* StoreWorld = nullptr); // +4 / +8; null refuses permission
	void RedirectTo(FElysiumNpcEnemyMemory* Store) { RedirectedStore = Store; } // 0x10273e10 +0x5d88 seam
	FElysiumNpcEnemyMemory& ConnectedStore() { return RedirectedStore != nullptr ? *RedirectedStore : *this; }
	// UpdateEnemyMemory (slot 544, 0x102709c0): create or refresh the target's one record. The
	// caller supplies an observed actor only; neutral/like relation policy remains BestEnemy's gate.
	void Update(FElysiumNpc& Npc, const FElysiumEntityHandle& Target, double Now);
	void UpdateAtPosition(FElysiumNpcBase& Npc, const FElysiumEntityHandle& Target,
		const FVector& Position, double Now);
	// UpdateMemory's null-entity arm: retain one position-only record. It is never a BestEnemy
	// candidate, but supplies the current damage/sound investigation position until a later refresh.
	void UpdatePositionOnly(const FVector& Position, double Now);

	// 0x102df320: unresolved unconditionally; NPC-state7 only with actual owner/squad permission.
	// No age expiry; tracked +0 alone refreshes inside the strict free-knowledge window.
	void Refresh(const FElysiumEntityWorld& World, double Now);

	const FElysiumNpcEnemyMemoryRecord* Find(const FElysiumEntityHandle& Target) const;
	FElysiumNpcEnemyMemoryRecord* FindMutable(const FElysiumEntityHandle& Target);
	bool IsEluded(const FElysiumEntityHandle& Target) const;
	void MarkEluded(const FElysiumEntityHandle& Target, bool bEluded = true);
	// `CAI_Enemies::ClearMemory` (`0x102dfaa0`): unlink the FIRST record whose handle resolves to
	// the target; a null target or no match removes nothing. Unlike Refresh, slot56/fanout notify
	// precedes unlink (0x102dfaa0); the caller's tag is null until its input is supplied.
	bool ClearMemory(const FElysiumEntityHandle& Target, const TCHAR* FunctionTag = nullptr);

	const TArray<FElysiumNpcEnemyMemoryRecord>& Records() const { return Entries; }
	// Spawn copies Rules.FreeKnowledgeDuration (default/authored V2 value .25) into CAI_Memory.
	double FreeKnowledgeDuration = 0.25;
	int32 Num() const { return Entries.Num(); }

	void Serialize(FElysiumSaveArchive& Ar);
	void Rebase(const FElysiumEntityWorld& World);

private:
	void NotifyRemoved(const FElysiumNpcEnemyMemoryRecord& RemovedRecord, FElysiumEntity* Target,
		const TCHAR* FunctionTag); // 0x102df4bc after unlink; 0x102dfaa0 before unlink
	FElysiumNpcBase* MemoryOwner = nullptr; // 0x102df320 CAI_Memory +0
	FElysiumEntityWorld* MemoryWorld = nullptr; // owner context for handle resolution, never victim inferred
	IElysiumEnemyMemorySquadOwner* SquadOwner = nullptr; // 0x102df320 +4: named null squad hook
	bool bSquadOwned = false; // 0x102df320 +8 selects squad permission and notify
	FElysiumNpcEnemyMemory* RedirectedStore = nullptr; // 0x10273e10 connected shared-store seam
	void UpdateObserved(const FElysiumEntityHandle& Target, const FVector& Position,
		const FVector& Velocity, double Now);
	TArray<FElysiumNpcEnemyMemoryRecord> Entries;
};
