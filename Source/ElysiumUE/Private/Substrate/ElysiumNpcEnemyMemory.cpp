#include "Substrate/ElysiumNpcEnemyMemory.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "Substrate/ElysiumNpc.h"

void FElysiumNpcEnemyMemory::Update(FElysiumNpc& Npc, const FElysiumEntityHandle& Target,
	double Now)
{
	FElysiumEntityWorld* World = Npc.World;
	const FElysiumEntity* Entity = World ? World->Resolve(Target) : nullptr;
	if (Entity == nullptr || Entity->IsInert() || Target == Npc.Handle)
	{
		return;
	}

	UpdateObserved(Target, Entity->Origin, Entity->Velocity, Now);
}

void FElysiumNpcEnemyMemory::UpdateAtPosition(FElysiumNpc& Npc,
	const FElysiumEntityHandle& Target, const FVector& Position, double Now)
{
	const FElysiumEntity* Entity = Npc.World ? Npc.World->Resolve(Target) : nullptr;
	if (Entity == nullptr || Entity->IsInert() || Target == Npc.Handle)
	{
		return;
	}
	UpdateObserved(Target, Position, FVector::ZeroVector, Now);
}

void FElysiumNpcEnemyMemory::UpdateObserved(const FElysiumEntityHandle& Target,
	const FVector& Position, const FVector& Velocity, double Now)
{
	FElysiumNpcEnemyMemoryRecord* Record = FindMutable(Target);
	const bool bNewRecord = Record == nullptr;
	if (bNewRecord)
	{
		// UpdateMemory prepends a newly observed actor. BestEnemy's equal-score tie is list order,
		// so append order would silently change retail selection.
		Entries.InsertDefaulted(0);
		Record = &Entries[0];
		Record->Handle = Target;
	}
	// The Source record keeps both the current/last-known target origin and the observer's
	// navigation anchor. Elysium has no exposed navigator-node identity yet, so the two node fields
	// carry the honest INDEX_NONE seam rather than fabricated NavMesh ids.
	if (bNewRecord || !Record->Anchor.Equals(Position, 0.0f))
	{
		// UpdateMemory stores the target's origin in both fields initially, then relatches both only
		// when that target has moved from its anchor. It is not the observer/NPC origin.
		Record->LastPosition = Position;
		Record->Anchor = Position;
	}
	Record->Velocity = Velocity;
	Record->LastSeenTime = Now;
	Record->LastNavNode = INDEX_NONE;
	Record->AnchorNavNode = INDEX_NONE;
	Record->bPositionOnly = false;
	Record->bEluded = false;
}

void FElysiumNpcEnemyMemory::Refresh(const FElysiumEntityWorld& World, double Now)
{
	for (FElysiumNpcEnemyMemoryRecord& Record : Entries)
	{
		const FElysiumEntity* Entity = World.Resolve(Record.Handle);
		if (Entity != nullptr && !Entity->IsInert()
			&& Now < Record.LastSeenTime + FreeKnowledgeDuration)
		{
			Record.LastPosition = Entity->Origin;
		}
	}
	Entries.RemoveAll([&World](const FElysiumNpcEnemyMemoryRecord& Record)
	{
		const FElysiumEntity* Entity = World.Resolve(Record.Handle);
		return Entity == nullptr || Entity->IsInert();
	});
}

void FElysiumNpcEnemyMemory::UpdatePositionOnly(const FVector& Position, double Now)
{
	FElysiumNpcEnemyMemoryRecord* Record = Entries.FindByPredicate(
		[](const FElysiumNpcEnemyMemoryRecord& Entry) { return Entry.bPositionOnly; });
	if (Record == nullptr)
	{
		Entries.InsertDefaulted(0);
		Record = &Entries[0];
	}
	Record->Handle = FElysiumEntityHandle::Invalid();
	Record->LastPosition = Position;
	Record->Anchor = Position;
	Record->Velocity = FVector::ZeroVector;
	Record->LastSeenTime = Now;
	Record->LastNavNode = INDEX_NONE;
	Record->AnchorNavNode = INDEX_NONE;
	Record->bPositionOnly = true;
	Record->bEluded = false;
}

const FElysiumNpcEnemyMemoryRecord* FElysiumNpcEnemyMemory::Find(
	const FElysiumEntityHandle& Target) const
{
	return Entries.FindByPredicate([&Target](const FElysiumNpcEnemyMemoryRecord& Record)
	{
		return Record.Handle == Target;
	});
}

FElysiumNpcEnemyMemoryRecord* FElysiumNpcEnemyMemory::FindMutable(
	const FElysiumEntityHandle& Target)
{
	return Entries.FindByPredicate([&Target](const FElysiumNpcEnemyMemoryRecord& Record)
	{
		return Record.Handle == Target;
	});
}

bool FElysiumNpcEnemyMemory::IsEluded(const FElysiumEntityHandle& Target) const
{
	const FElysiumNpcEnemyMemoryRecord* Record = Find(Target);
	return Record != nullptr && Record->bEluded;
}

void FElysiumNpcEnemyMemory::MarkEluded(const FElysiumEntityHandle& Target, bool bEluded)
{
	if (FElysiumNpcEnemyMemoryRecord* Record = FindMutable(Target))
	{
		Record->bEluded = bEluded;
	}
}

void FElysiumNpcEnemyMemory::Serialize(FElysiumSaveArchive& Ar)
{
	int32 Count = Entries.Num();
	Ar << FreeKnowledgeDuration;
	Ar << Count;
	if (Ar.IsLoading())
	{
		if (Count < 0 || Count > 4096)
		{
			Ar.SetError();
			Entries.Reset();
			return;
		}
		Entries.SetNum(Count);
	}
	for (FElysiumNpcEnemyMemoryRecord& Record : Entries)
	{
		Ar << Record.Handle << Record.LastPosition << Record.Anchor << Record.Velocity;
		Ar << Record.LastSeenTime << Record.LastNavNode << Record.AnchorNavNode;
		uint8 PositionOnly = Record.bPositionOnly ? 1 : 0;
		uint8 Eluded = Record.bEluded ? 1 : 0;
		Ar << PositionOnly << Eluded;
		if (Ar.IsLoading())
		{
			Record.bPositionOnly = PositionOnly != 0;
			Record.bEluded = Eluded != 0;
		}
	}
}

void FElysiumNpcEnemyMemory::Rebase(const FElysiumEntityWorld& World)
{
	for (FElysiumNpcEnemyMemoryRecord& Record : Entries)
	{
		Record.Handle = World.RebaseSavedHandle(Record.Handle);
	}
	Entries.RemoveAll([](const FElysiumNpcEnemyMemoryRecord& Record)
	{
		return !Record.Handle.IsSet() && !Record.bPositionOnly;
	});
}
