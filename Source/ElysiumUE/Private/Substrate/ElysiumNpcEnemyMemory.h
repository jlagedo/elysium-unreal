#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntityHandle.h"

class FElysiumEntityWorld;
class FElysiumNpc;
struct FElysiumSaveArchive;

// CAI_Memory's observed-actor record list. It is deliberately separate from the committed enemy:
// the list is perception's admission store, while `Senses.Memory.Enemy` is the sticky choice made
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
	// UpdateEnemyMemory (slot 544, 0x102709c0): create or refresh the target's one record. The
	// caller supplies an observed actor only; neutral/like relation policy remains BestEnemy's gate.
	void Update(FElysiumNpc& Npc, const FElysiumEntityHandle& Target, double Now);
	void UpdateAtPosition(FElysiumNpc& Npc, const FElysiumEntityHandle& Target,
		const FVector& Position, double Now);
	// UpdateMemory's null-entity arm: retain one position-only record. It is never a BestEnemy
	// candidate, but supplies the current damage/sound investigation position until a later refresh.
	void UpdatePositionOnly(const FVector& Position, double Now);

	// RefreshMemories (0x102df320): only invalid/dead entries leave the list. In particular, time
	// never removes a record, and losing sight only leaves its stored position intact.
	void Refresh(const FElysiumEntityWorld& World, double Now);

	const FElysiumNpcEnemyMemoryRecord* Find(const FElysiumEntityHandle& Target) const;
	FElysiumNpcEnemyMemoryRecord* FindMutable(const FElysiumEntityHandle& Target);
	bool IsEluded(const FElysiumEntityHandle& Target) const;
	void MarkEluded(const FElysiumEntityHandle& Target, bool bEluded = true);

	const TArray<FElysiumNpcEnemyMemoryRecord>& Records() const { return Entries; }
	// Spawn copies Rules.FreeKnowledgeDuration (default/authored V2 value .25) into CAI_Memory.
	double FreeKnowledgeDuration = 0.25;
	int32 Num() const { return Entries.Num(); }

	void Serialize(FElysiumSaveArchive& Ar);
	void Rebase(const FElysiumEntityWorld& World);

private:
	void UpdateObserved(const FElysiumEntityHandle& Target, const FVector& Position,
		const FVector& Velocity, double Now);
	TArray<FElysiumNpcEnemyMemoryRecord> Entries;
};
