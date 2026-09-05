#pragma once

#include "CoreMinimal.h"
#include "ElysiumEntityHandle.h"

enum class EElysiumCharacterModelAdmission : uint8 { Ready, Pending, Rejected };

/** Presentation request identity; independent of entity identity and Source event scheduling. */
struct FElysiumCharacterModelTicket
{
	FElysiumEntityHandle Entity;
	FString ModelId;
	uint64 Generation = 0;
};

class FElysiumCharacterModelRequests
{
public:
	FElysiumCharacterModelTicket Begin(const FElysiumEntityHandle& Entity, const FString& ModelId, uint64 Generation)
	{
		FElysiumCharacterModelTicket Ticket{Entity, ModelId, Generation};
		Current.Add(Entity, Ticket); return Ticket;
	}
	bool IsCurrent(const FElysiumCharacterModelTicket& Ticket, uint32 WorldEpoch,
		const FElysiumEntityHandle& LiveHandle, const FString& LiveModelId, bool bAlive) const
	{
		const auto* Found = Current.Find(Ticket.Entity);
		return bAlive && Ticket.Entity.IsSet() && Found && Ticket.Entity == LiveHandle
			&& Ticket.Entity.Epoch == WorldEpoch && Ticket.ModelId == LiveModelId
			&& Found->Generation == Ticket.Generation && Found->ModelId == Ticket.ModelId;
	}
	bool Remove(const FElysiumCharacterModelTicket& Ticket)
	{
		const auto* Found = Current.Find(Ticket.Entity);
		if (!Found || Found->Generation != Ticket.Generation || Found->ModelId != Ticket.ModelId) return false;
		Current.Remove(Ticket.Entity); return true;
	}
	void Cancel(const FElysiumEntityHandle& Entity) { Current.Remove(Entity); }
	void Reset() { Current.Reset(); }
	int32 Num() const { return Current.Num(); }
private:
	TMap<FElysiumEntityHandle, FElysiumCharacterModelTicket> Current;
};
