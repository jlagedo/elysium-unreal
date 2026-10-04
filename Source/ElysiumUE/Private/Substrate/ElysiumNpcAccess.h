#pragma once

#include "CoreMinimal.h"

class FElysiumEntity;
class FElysiumNpc;
class FElysiumNpcBase;

// The NPC kernel members a few callers in other layers name, declared without the kernel's class
// (spec 0002 T6b).
//
// `ElysiumNpcBase.h` and `ElysiumNpc.h` are the module's largest class headers and change with
// most stories; a file that includes one is recompiled, with every file of its unity blob, on each
// such edit. A caller outside the kernel that needs one member of an NPC calls it through here and
// keeps its blob out of that rebuild. Each function IS the member it names, called as is -- nothing
// is added to or taken from what the member does. Definitions in `ElysiumNpcAccess.cpp`.
namespace ElysiumNpcAccess
{
	// The NPC as the entity it is: what a caller holding `AsNpc()`'s answer reads the entity's own
	// words through (`DebugString`, `Origin`, ...).
	const FElysiumEntity& AsEntity(const FElysiumNpc& Npc);

	// `FElysiumNpcBase::DoorLinkPathfindingAllowed` (`0x102ff960`'s stale tail over `0x102fce80`),
	// for the door smart link's per-query predicate (`AElysiumNavDoorLink::IsUsableFor`).
	bool DoorLinkPathfindingAllowed(FElysiumNpcBase& Npc, FElysiumEntity& Door, const FVector& StartCm,
		const FVector& EndCm);

	// `FElysiumNpcBase::HullKind` (`m_eHull`) and `FElysiumNpc::PathingHullKind` (`+0x156c`), for the
	// body's capsule and nav agent (`AElysiumNpcBody::ApplyRetailHull`).
	int32 HullKind(const FElysiumNpc& Npc);
	int32 PathingHullKind(const FElysiumNpc& Npc);

	// `FElysiumNpc::AttackBounds`: the collision bounds grown by the stored attack margin (the feed
	// probe's candidate box, `AElysiumMapActor`).
	FBox AttackBounds(const FElysiumNpc& Npc, const FBox& CollisionBounds);

	// `FElysiumNpcBase::IsBccTargetable` (`m_bIsBCCTargetable` `+0x1480` off any `+0x94` NPC), for the
	// feed probe's `EntityUnselectable` test (`AElysiumMapActor`).
	bool IsBccTargetable(const FElysiumEntity& Candidate);
}
