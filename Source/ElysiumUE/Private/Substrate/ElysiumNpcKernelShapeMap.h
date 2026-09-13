#pragma once

#include "CoreMinimal.h"

#include "Containers/ArrayView.h"

// Which port member carries which retail word.
//
// The census (`Substrate/ElysiumNpcKernelShape.h`) says what `CAI_BaseNPCTroika` is; this says what
// this runtime made of it. One row per word of the NPC's own layers — `CAI_BaseNPC` and
// `CAI_BaseNPCTroika`, everything past `CBaseCombatCharacter`'s extent — plus the handful of
// entity-chain words a port member already claims by offset.
//
// **Hand-maintained, and compile-checked.** A row written with `ELYSIUM_NPC_WORD` names the port
// member as a type and an identifier, so a rename or a deletion is a build error rather than a
// stale comment; the row also carries `sizeof` that member, which the test compares against the
// datamap's declared width. A private member cannot be named that way and takes
// `ELYSIUM_NPC_WORD_PRIVATE`, which is a string and is checked only by eye.
//
// The entity-chain words below `CAI_BaseNPC` are deliberately NOT rows. They are `CBaseEntity`'s,
// `CBaseAnimating`'s and `CBaseCombatCharacter`'s concerns, the port carries them on its own entity
// chain, and re-homing them is those classes' story rather than the NPC kernel's. The census still
// carries every one of them with its owning layer, so the work is recorded rather than lost.

// What this runtime does with a retail word.
enum class EElysiumNpcWordHome : uint8
{
	// A member of one of the NPC's own structs carries it.
	Member,
	// The port carries the concern on the entity chain below the NPC.
	Chain,
	// The language or an existing mechanism provides it, so there is no member to declare: the
	// vtable pointer, and every `COutputEvent` (the port fires outputs by name through
	// `FElysiumEntity::FireOutput` and keeps no per-output member).
	Implicit,
	// No port member, and this story declares none. The reason is on the row and is never "not got
	// to it": a retail diagnostic this runtime replaces with its own, or an aggregate whose element
	// type has no port counterpart yet.
	Absent,
	Count
};

// One retail word, bound.
struct FElysiumNpcWordBinding
{
	// The offset in `CAI_BaseNPCTroika`, as `layout.tsv` records it.
	int32 Offset = 0;
	// `Struct::Member`, or null for `Implicit` and `Absent`.
	const TCHAR* PortPath = nullptr;
	EElysiumNpcWordHome Home = EElysiumNpcWordHome::Absent;
	// `sizeof` the port member, or 0 where the row could not take it (a private member, or no
	// member at all).
	int32 PortSize = 0;
	// Why, for a row that needs one. Null where the binding speaks for itself.
	const TCHAR* Note = nullptr;
};

// A public member: the type and the identifier are compiled, so the row cannot go stale.
#define ELYSIUM_NPC_WORD(InOffset, InType, InMember) \
	FElysiumNpcWordBinding{ InOffset, TEXT(#InType "::" #InMember), \
		EElysiumNpcWordHome::Member, static_cast<int32>(sizeof(decltype(InType::InMember))), \
		nullptr }

// The same, with a note.
#define ELYSIUM_NPC_WORD_NOTED(InOffset, InType, InMember, InNote) \
	FElysiumNpcWordBinding{ InOffset, TEXT(#InType "::" #InMember), \
		EElysiumNpcWordHome::Member, static_cast<int32>(sizeof(decltype(InType::InMember))), \
		TEXT(InNote) }

// A member the owning struct keeps private. The path is a string and the size is unknown; the
// reason it is private is the note.
#define ELYSIUM_NPC_WORD_PRIVATE(InOffset, InPath, InNote) \
	FElysiumNpcWordBinding{ InOffset, TEXT(InPath), EElysiumNpcWordHome::Member, 0, TEXT(InNote) }

// The port carries this on the entity chain below the NPC.
#define ELYSIUM_NPC_WORD_CHAIN(InOffset, InPath, InNote) \
	FElysiumNpcWordBinding{ InOffset, TEXT(InPath), EElysiumNpcWordHome::Chain, 0, TEXT(InNote) }

#define ELYSIUM_NPC_WORD_IMPLICIT(InOffset, InNote) \
	FElysiumNpcWordBinding{ InOffset, nullptr, EElysiumNpcWordHome::Implicit, 0, TEXT(InNote) }

#define ELYSIUM_NPC_WORD_ABSENT(InOffset, InNote) \
	FElysiumNpcWordBinding{ InOffset, nullptr, EElysiumNpcWordHome::Absent, 0, TEXT(InNote) }

namespace ElysiumNpcKernelShapeMap
{
	TArrayView<const FElysiumNpcWordBinding> Bindings();
}
