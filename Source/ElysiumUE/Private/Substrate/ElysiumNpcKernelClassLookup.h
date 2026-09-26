#pragma once

#include "CoreMinimal.h"

#include "Substrate/ElysiumNpcKernelShape.h"

// The census read as a *dispatcher*: which retail family class a spawned `npc_*` classname is, and
// which body fills a given vtable slot for it.
//
// Story 29b landed the census as data and story 29c-1 stood this reader over it, when one C++ leaf
// stood for every classname and retail's override set was a table lookup. Story 5 stands the class
// tree: each species class answers its own row (`FElysiumNpc::OwnRetailClass`), and the census's
// classnames are retail's factories, one class per classname (step 2). The species dispatch through
// this reader shrinks as steps 3-4 turn its arms into overrides.
//
// A species class with no body of its own at a slot inherits its base's, exactly as the vtable does,
// so the resolution walks `Base` upward and stops at the first override row.

namespace ElysiumNpcKernelClass
{
	// The census row for a retail class name (`CNPC_VSabbatLeader`), or null when the family tree
	// holds no such class.
	const FElysiumNpcClass* Find(const TCHAR* RetailClass);

	// The retail class whose factory builds an entity classname (`npc_VSabbatLeader`), or null for
	// a classname no retail factory builds as a family class. Not how an NPC learns its own class
	// (that is its C++ type); a query for surfaces that hold only a classname.
	const FElysiumNpcClass* OfClassname(const FString& Classname);

	// Whether `Cls` is `Ancestor` or derives from it. The chain walk the vtable performs, spelled
	// once: a species body's "is this a vampire boss" question is this and never a name compare.
	bool DerivesFrom(const FElysiumNpcClass* Cls, const TCHAR* Ancestor);

	// The nearest species override of `Slot` walking `Cls`'s base chain upward, or null when no
	// class in the chain replaces the Troika-line body. `CAI_BaseNPC` and `CAI_BaseNPCTroika` are
	// not override rows — their bodies ARE the Troika line — so a null answer means "the base body".
	const FElysiumNpcClassSlot* OverrideOf(const FElysiumNpcClass* Cls, int32 Slot);

	// The retail address of the body that fills `Slot` for `Cls`: the nearest override's, else the
	// Troika-line slot's own. Empty where neither carries one. This is what a ported body cites
	// when it answers per species, so a family's table row can be checked against the ledger.
	const TCHAR* BodyOf(const FElysiumNpcClass* Cls, int32 Slot);

	// The Troika-line census row for a slot, or null for an index outside 0..616.
	const FElysiumNpcSlot* SlotRow(int32 Slot);
}
