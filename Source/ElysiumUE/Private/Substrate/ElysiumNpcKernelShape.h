#pragma once

#include "CoreMinimal.h"

#include "Containers/ArrayView.h"

// The NPC kernel's shape, as project source: what `CAI_BaseNPCTroika` *is*, so the port's own
// shape can be asserted against it rather than described in comments.
//
// The data is generated from the kernel ledger (`docs/vtmb/npc-kernel/`) by
// `research/tooling/gen_kernel_shape.py`; `ElysiumNpcKernelShape.cpp` is its output and is never
// hand-edited. This header declares the shape of that output and nothing else — the census carries
// no behaviour, no rule and no threshold, only the recorded identity of every word, slot and class.
//
// Three consumers:
//
//   * `ElysiumNpcKernelShapeMap.cpp` binds every census word to the port member that carries it,
//     with the member's existence checked by the compiler.
//   * `ElysiumNpcKernelSlots.inl` / `.cpp` declare and stub the Troika-line slots the port has no
//     body for yet; the census row is what says which story owns each.
//   * `Tests/ElysiumNpcKernelShapeTests.cpp` walks all of it and requires the recovered counts
//     back, which is what catches a bad regeneration or a hand-edit.
//
// Species are data here as they are everywhere else in this runtime: a species' own words and its
// own slot bodies are rows keyed on the retail class name, not a C++ subclass.

// Where a row's answer came from, in the ledger's own vocabulary (`npc-kernel/README.md`,
// "What is a fact and what is a heuristic"). `Unsettled` is a reading that did not settle and
// carries its reason in the ledger, not here.
enum class EElysiumNpcShapeTier : uint8
{
	Datamap,
	Interior,
	Sdk,
	SdkOrder,
	Doc,
	Walked,
	Evidence,
	Open,
	Unsettled,
	Count
};

// One top-level word of a retail layout. An interior — a `COutputEvent`, a `CUtlVector`, a
// `Vector`'s components, an array's elements — is not a row of its own: the port declares one
// member per retail aggregate, so the interiors are counted on the word that owns them.
struct FElysiumNpcWord
{
	// Byte offset into the owning table's instance.
	int32 Offset = 0;
	// `CAI_BaseNPCTroika` for the flattened base layout, else the species class that adds the word.
	const TCHAR* Table = nullptr;
	// The retail member name.
	const TCHAR* Member = nullptr;
	// The retail C type, verbatim.
	const TCHAR* Type = nullptr;
	// The class in the chain that owns the word — `CBaseEntity` through `CAI_BaseNPCTroika`. This
	// is what decides which port type should carry it.
	const TCHAR* Layer = nullptr;
	EElysiumNpcShapeTier Tier = EElysiumNpcShapeTier::Open;
	// The datamap's declared width, or 0 where no record states one.
	int32 Size = 0;
	// The array count the datamap declares; 1 for a scalar.
	int32 Count = 1;
	// How many interior rows collapsed onto this word.
	int32 Interiors = 0;
};

// One primary-vtable slot, with the declaration every body at that slot overrides.
struct FElysiumNpcSlot
{
	int32 Slot = 0;
	// Empty for a virtual of the Troika line. Past a base's table each branch declares unrelated
	// virtuals at the same index, and this names the class that introduces it.
	const TCHAR* Class = nullptr;
	// The image's name for the slot; empty where no evidence names it.
	const TCHAR* Method = nullptr;
	// The retail declaration, verbatim.
	const TCHAR* Declaration = nullptr;
	// The port's callable for this slot: an existing method where the port already implements the
	// concern, else the generated virtual on `FElysiumNpc`.
	const TCHAR* PortMethod = nullptr;
	EElysiumNpcShapeTier Tier = EElysiumNpcShapeTier::Open;
	// The body the holder fills, `0x10……`; empty where the slot has no body in the closure.
	const TCHAR* Address = nullptr;
	// That body's layer in `npc-kernel/order.md`, or -1.
	int32 Layer = -1;
	// The spec story whose layer band owns the port of that body (`29c`/`29d`/`29e`).
	const TCHAR* Story = nullptr;
	// True when the port already implements the slot; false when it is a declared stub.
	bool bPorted = false;
	// What `research/tooling/ghidra/driver/kernel_verdicts.tsv` records for the body at this slot
	// — `rule`, `mechanism`, `present`, `dead`, `unsettled` — or empty where no story has read it.
	const TCHAR* Verdict = nullptr;
	// The retail literal the whole body returns (`0`, `0x17`, `-1`), or `void` where the whole body
	// is `return;`. Empty when the body is not a constant. This is the one place the census carries
	// a value rather than an identity, and it is a recovered fact: a virtual whose entire retail
	// implementation is one literal is data, not behaviour, exactly as a species override is.
	const TCHAR* Default = nullptr;
};

// One slot whose retail body is a constant, with a probe that calls the port's virtual for it.
//
// The port's body for these is generated from `Default` rather than written, so something has to
// keep the generated body and the recovered literal joined: that is
// `Elysium.Substrate.NpcKernelSlots.Defaults`, which walks this table, calls `Invoke` on a real
// NPC and requires `Value` back. Without it the emission would be a comment that compiles.
struct FElysiumNpcSlotDefault
{
	int32 Slot = 0;
	// The retail body, `0x10……`.
	const TCHAR* Address = nullptr;
	// The port's virtual, as `FElysiumNpc` declares it.
	const TCHAR* PortMethod = nullptr;
	// The retail literal exactly as the decompiled C spells it, or `void`.
	const TCHAR* Retail = nullptr;
	// That literal in the port's return type, as an integer. 0 for a `void` slot.
	int64 Value = 0;
	// True when retail's body is `return;` and there is no answer to compare — the assertion is
	// then only that the call tallies no stub.
	bool bVoid = false;
	// Calls the port's virtual with value-initialised arguments and renders the answer as an
	// integer. Never null.
	int64 (*Invoke)(class FElysiumNpc&) = nullptr;
};

// One class of the family: the 77 whose primary vtable spans the NPC slot range.
struct FElysiumNpcClass
{
	const TCHAR* Name = nullptr;
	const TCHAR* Base = nullptr;
	// The primary vtable's address, `0x10……`.
	const TCHAR* Vtable = nullptr;
	int32 Slots = 0;
	// How many slots this class fills with a body of its own.
	int32 OwnBodies = 0;
	const TCHAR* const* Classnames = nullptr;
	int32 ClassnameCount = 0;
};

// One species override: the class, the slot it replaces, and the body that replaces it. Rows, not
// subclasses — this runtime stands one leaf for every `npc_V*` classname.
struct FElysiumNpcClassSlot
{
	const TCHAR* Class = nullptr;
	int32 Slot = 0;
	const TCHAR* Address = nullptr;
	const TCHAR* Method = nullptr;
	// What `kernel_verdicts.tsv` records for the body, or empty where no story has read it.
	const TCHAR* Verdict = nullptr;
	// The literal this species answers at this slot, where the verdict says the body is a class →
	// slot → value row and the whole retail body is one `return`. `void` for `return;`, empty
	// where the body is not a constant. This is the value half of the registry decision 29c took:
	// a species override of a constant-returning virtual is data, not a C++ type.
	const TCHAR* Default = nullptr;
};

// The stored provenance: every count the generator measured, plus a digest of the row stream. A
// regeneration that silently dropped rows disagrees with one of these.
struct FElysiumNpcShapeCensus
{
	int32 Words = 0;
	int32 TroikaWords = 0;
	int32 SpeciesWords = 0;
	// Of `TroikaWords`, the ones whose owning layer is `CAI_BaseNPC` or `CAI_BaseNPCTroika` — the
	// words the NPC itself carries, as opposed to the entity chain under it.
	int32 NpcWords = 0;
	int32 Interiors = 0;
	int32 UnsettledWords = 0;
	int32 Slots = 0;
	int32 TroikaSlots = 0;
	int32 BranchSlots = 0;
	int32 PortedSlots = 0;
	int32 UnsettledSlots = 0;
	int32 Classes = 0;
	int32 Classnames = 0;
	int32 Overrides = 0;
	// Of the Troika line, the slots whose retail body a verdict has read (story 29c and after),
	// and the subset of those whose whole body is a constant the port now answers.
	int32 VerdictedSlots = 0;
	int32 DefaultSlots = 0;
	// The same two over the species override rows: how many bodies a verdict has read, and how
	// many of those answer a literal the registry row carries.
	int32 VerdictedOverrides = 0;
	int32 RegistryValues = 0;
	uint64 RowDigest = 0;
};

namespace ElysiumNpcKernelShape
{
	TArrayView<const FElysiumNpcWord> Words();
	TArrayView<const FElysiumNpcSlot> Slots();
	TArrayView<const FElysiumNpcClass> Classes();
	TArrayView<const FElysiumNpcClassSlot> Overrides();
	const FElysiumNpcShapeCensus& Census();

	// The slots whose body the port generates from the recovered literal, each with the probe the
	// defaults suite calls it through. Defined beside those bodies in `ElysiumNpcKernelSlots.cpp`.
	TArrayView<const FElysiumNpcSlotDefault> SlotDefaults();

	// The ledger's own spelling of a tier (`datamap`, `sdk-order`, `walked`…), which is what the
	// digest folds and what a diagnostic prints.
	const TCHAR* TierName(EElysiumNpcShapeTier Tier);

	// The digest the test recomputes from the committed rows: FNV-1a 64 over the same row stream
	// the generator hashed, so the two agree only if nothing was dropped or reordered.
	uint64 DigestOfRows();
}
