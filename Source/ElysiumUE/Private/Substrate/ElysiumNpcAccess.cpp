#include "Substrate/ElysiumNpcAccess.h"

#include "Substrate/ElysiumNpc.h"

const FElysiumEntity& ElysiumNpcAccess::AsEntity(const FElysiumNpc& Npc)
{
	return Npc;
}

bool ElysiumNpcAccess::DoorLinkPathfindingAllowed(FElysiumNpcBase& Npc, FElysiumEntity& Door,
	const FVector& StartCm, const FVector& EndCm)
{
	return Npc.DoorLinkPathfindingAllowed(Door, StartCm, EndCm);
}

int32 ElysiumNpcAccess::HullKind(const FElysiumNpc& Npc)
{
	return Npc.HullKind;
}

int32 ElysiumNpcAccess::PathingHullKind(const FElysiumNpc& Npc)
{
	return Npc.PathingHullKind;
}

FBox ElysiumNpcAccess::AttackBounds(const FElysiumNpc& Npc, const FBox& CollisionBounds)
{
	return Npc.AttackBounds(CollisionBounds);
}

bool ElysiumNpcAccess::IsBccTargetable(const FElysiumEntity& Candidate)
{
	return FElysiumNpcBase::IsBccTargetable(Candidate);
}
