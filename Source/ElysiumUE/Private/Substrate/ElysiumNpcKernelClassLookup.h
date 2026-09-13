#pragma once

#include "CoreMinimal.h"

#include "Substrate/ElysiumNpcKernelShape.h"

// The census read as a *dispatcher*: which retail family class a spawned `npc_*` classname is, and
// which body fills a given vtable slot for it.
//
// Story 29b landed the census as data and story 29c deliberately did not stand a reader over it —
// "nothing in this runtime dispatches through these virtuals yet, and standing a species dispatcher
// before a caller exists is the seam-by-guess 29b existed to end". Story 29c-1 is that caller: 915
// layer 0–9 bodies land here, and 355 of them are one retail behaviour written once per species.
// This runtime stands ONE leaf for every classname (`FElysiumNpc` is `final`), so retail's override
// set is a table lookup rather than a C++ override, and this is the one reader every family's body
// goes through instead of each inventing its own.
//
// Two facts about the census make the lookup non-trivial, and both are recorded here rather than
// rediscovered per family:
//
//   * A classname appears on EVERY class in the chain that can spawn it, not only on the leaf —
//     `npc_VChangBros` is claimed by `CNPC_VChangBros` and by its base `CNPC_VVampireBoss`. The
//     class a body must dispatch as is the MOST DERIVED claimant, which is the one no other
//     claimant descends from.
//   * A species class with no body of its own at a slot inherits its base's, exactly as the vtable
//     does, so the resolution walks `Base` upward and stops at the first override row.

namespace ElysiumNpcKernelClass
{
	// The census row for a retail class name (`CNPC_VSabbatLeader`), or null when the family tree
	// holds no such class.
	const FElysiumNpcClass* Find(const TCHAR* RetailClass);

	// The retail class a spawned entity classname (`npc_VSabbatLeader`) resolves to — the most
	// derived claimant. Null for a classname no family class claims, which is the ordinary answer
	// for a classname this port registers but retail never stood an NPC class for.
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
