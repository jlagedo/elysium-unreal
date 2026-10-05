#include "Substrate/ElysiumNpcEnemyMemory.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcBase.h"

void FElysiumNpcEnemyMemory::BindOwner(FElysiumNpcBase& StoreOwner)
{
	if (MemoryOwner == nullptr && !bSquadOwned) // 0x102df320 +0
	{
		MemoryOwner = &StoreOwner; // 0x102df320: creator's identity, never changed by an observer
	}
	if (MemoryOwner == &StoreOwner) { MemoryWorld = StoreOwner.World; } // same owner attaches to its world after construction
}

void FElysiumNpcEnemyMemory::BindSquadOwner(IElysiumEnemyMemorySquadOwner* InSquadOwner,
	FElysiumEntityWorld* StoreWorld)
{
	MemoryOwner = nullptr; SquadOwner = InSquadOwner; bSquadOwned = true; // 0x102df320 +0/+4/+8
	MemoryWorld = StoreWorld; // actual squad-store owner context, never arbitrary caller
}

void FElysiumNpcEnemyMemory::NotifyRemoved(const FElysiumNpcEnemyMemoryRecord& RemovedRecord,
	FElysiumEntity* Target, const TCHAR* FunctionTag)
{
	if (bSquadOwned) // 0x102df470
	{
		if (SquadOwner != nullptr) // 0x102df477: absent squad receives nothing
		{
			SquadOwner->Removed(Target, RemovedRecord.Anchor, RemovedRecord.Velocity, FunctionTag); // 0x103169a0
		}
	}
	else if (MemoryOwner != nullptr) // 0x102df4bc
	{
		MemoryOwner->Slot56(Target, RemovedRecord.Anchor, RemovedRecord.Velocity, FunctionTag); // 0x102df4fa
	}
}

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

void FElysiumNpcEnemyMemory::UpdateAtPosition(FElysiumNpcBase& Npc,
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
	int32 EntryIndex = 0; // 0x102df3ce: head, then ordered +0x38 links
	int32 PreviousIndex = INDEX_NONE; // 0x102df3d1: address of head or last kept +0x38 link
	while (Entries.IsValidIndex(EntryIndex)) // 0x102df3da
	{
		FElysiumNpcEnemyMemoryRecord& Entry = Entries[EntryIndex]; // 0x102df3e0
		FElysiumEntity* const Target = const_cast<FElysiumEntity*>(World.Resolve(Entry.Handle)); // 0x102df3ec..0x102df409
		bool bRemoveEntry = Target == nullptr; // 0x102df40d: unresolved unconditionally
		if (Target != nullptr) // 0x102df40f: NPC self-cast +0x94, not IsAlive
		{
			FElysiumNpcBase* const TargetNpc = Target->AsNpcBase(); // 0x102df40f
			if (TargetNpc != nullptr && TargetNpc->NpcStateRetail() == 7) // 0x102df41f slot464
			{
				bRemoveEntry = bSquadOwned // 0x102df42e
					? SquadOwner != nullptr && SquadOwner->MayRemove(Target) // 0x103167f0, null hook refuses
					: MemoryOwner != nullptr && MemoryOwner->Slot54(Target); // 0x102df455
			}
		}
		if (bRemoveEntry) // 0x102df467: unlink before notify
		{
			const FElysiumNpcEnemyMemoryRecord RemovedRecord = Entry; // 0x102df47f..0x102df4f7
			const int32 NextLinkIndex = PreviousIndex + 1; // 0x102df46a: *prev = removed->next
			// 0x102df518 leaves prev unchanged when skipping the successor. A later removal can
			// consequently detach that skipped node too, without a notification or free for it.
			Entries.RemoveAt(NextLinkIndex, EntryIndex - PreviousIndex); // 0x102df46a reachable-list splice
			NotifyRemoved(RemovedRecord, Target, TEXT("CAI_Memory::RefreshMemories")); // 0x102df4fa
			EntryIndex = NextLinkIndex + 1; // 0x102df50e..0x102df518: successor->next, prev stays
			continue;
		}
		if (Now < Entry.LastSeenTime + FreeKnowledgeDuration) // 0x102df51d..0x102df533: strict boundary
		{
			Entry.LastPosition = Target->GetAbsOrigin(); // 0x102df539: +0 only, never LKP +0x0c
		}
		PreviousIndex = EntryIndex; // 0x102df54f: prev advances only after a kept entry
		++EntryIndex; // 0x102df556
	}
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

bool FElysiumNpcEnemyMemory::ClearMemory(const FElysiumEntityHandle& Target, const TCHAR* FunctionTag)
{
	// `0x102dfaa0`: `param_1 != 0` guards the walk; the first record whose `+0x24` handle resolves
	// to the entity is unlinked (`*prev = rec->next`) and freed (`0x102df1b0`).
	if (!Target.IsSet())
	{
		return false;
	}
	const int32 Index = Entries.IndexOfByPredicate(
		[&Target](const FElysiumNpcEnemyMemoryRecord& Record) { return Record.Handle == Target; });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	const FElysiumNpcEnemyMemoryRecord RemovedRecord = Entries[Index]; // 0x102dfaa0
	FElysiumEntity* const RemovedTarget = MemoryWorld != nullptr
		? MemoryWorld->Resolve(Target) : nullptr; // 0x102dfaa0: resolve from store owner
	if (RemovedTarget == nullptr) { return false; } // 0x102dfaa0: null target cannot match a live pointer
	NotifyRemoved(RemovedRecord, RemovedTarget, FunctionTag); // 0x102dfaa0: unlike Refresh, before unlink
	Entries.RemoveAt(Index); // 0x102dfaa0 unlink after notify, original caller tag (null named seam)
	return true;
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
