#pragma once

#include "CoreMinimal.h"

#include "Containers/ArrayView.h"

#include <type_traits>

// The NPC kernel's shape, as project source: what `CAI_BaseNPCTroika` *is*, so the port's own
// shape can be asserted against it rather than described in comments.
//
// The data is generated from the kernel ledger (`docs/vtmb/npc-kernel/`) by
// `research/tooling/gen_kernel_shape.py`; `ElysiumNpcKernelShape.cpp` is its output and is never
// hand-edited. This header declares the shape of that output and nothing else — the census carries
// no behaviour, no rule and no threshold, only the recorded identity of every word, slot and class.
//
// Consumers:
//
//   * `ElysiumNpcKernelShapeMap.cpp` binds every census word to the port member that carries it,
//     with the member's existence checked by the compiler.
//   * the generated `…Slots.inl` / `.cpp` of each chain class declare its slots and hold the
//     one-constant bodies and the counting stubs; the census row says which story owns each.
//   * each class of the NPC tree takes its own row as its identity (`ELYSIUM_NPC_CLASS`,
//     `ClassNamed`) -- identity only: behaviour is the class's overrides.
//   * `Tests/ElysiumNpcKernelShapeTests.cpp` and the class-tree tests walk all of it and require
//     the recovered counts back, which is what catches a bad regeneration or a hand-edit.
//
// A species' own words and slot bodies are census rows here and C++ members and overrides on its
// port class (0019 story 5): the rows are what the classes are asserted against.

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

class FElysiumEntity;
class FElysiumAnimating;
class FElysiumAnimatingOverlay;
class FElysiumFlex;
class FElysiumCombatCharacter;
class FElysiumNpcBase;
class FElysiumNpc;

// What a generated slot row's port body is.
enum class EElysiumNpcSlotBody : uint8
{
	// The counting stub: no port implementation yet. It tallies under `Retail::PortMethod`.
	Stub,
	// Retail's whole body is one literal (`Default`), and the port answers it.
	Default,
	// Declared by the generator, defined by hand in the substrate.
	Hand,
};

// One generated slot row of one port class (0019 story 5 step 6): a slot the class introduces, or
// one it overrides because its retail table holds a body of its own there.
//
// One table per port class, typed to it (`ElysiumNpcKernelShape::EntitySlotRows()` ...
// `NpcSlotRows()`). The census suite reads the table to hold the tree to the chain it models; the
// defaults suite calls every `Default` row's `Invoke` on a receiver of exactly this class, so the
// generated body and the recovered literal stay joined. Without that the emission would be a
// comment that compiles.
template <typename TReceiver>
struct TElysiumNpcSlotRow
{
	int32 Slot = 0;
	// The retail body, `0x10……`.
	const TCHAR* Address = nullptr;
	// The retail class that owns the body: the most-base table holding the same pointer. A stub
	// tallies under `Retail::PortMethod`.
	const TCHAR* Retail = nullptr;
	// The port's virtual, as the class declares it.
	const TCHAR* PortMethod = nullptr;
	EElysiumNpcSlotBody Body = EElysiumNpcSlotBody::Stub;
	// For a `Default` row, the retail literal exactly as the decompiled C spells it, or `void`.
	const TCHAR* Default = nullptr;
	// That literal in the port's return type, as an integer. 0 for a `void` or non-default row.
	int64 Value = 0;
	// True when retail's body is `return;` and there is no answer to compare.
	bool bVoid = false;
	// True when a more-base port class declares the slot and this row overrides it.
	bool bOverride = false;
	// Whether the port method is declared on `TReceiver` itself, answered by the compiler
	// (`TDeclaredOn`) rather than by the generator.
	bool bDeclaredHere = false;
	// A `Default` row's probe: calls the virtual with value-initialised arguments and renders the
	// answer as an integer. Null on every other row.
	int64 (*Invoke)(TReceiver&) = nullptr;
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

// One species slot body: the class, the slot it fills and the body that fills it, where that body is
// not the Troika line's. Census only: the port's answer is the class's override (0019 story 5).
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
	// where the body is not a constant.
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

	// The census row of a retail class by its name (`CNPC_VCop`), or null. A linear walk of
	// `Classes()`: the class tree takes its own row once (`ELYSIUM_NPC_CLASS`), and the census and
	// factory tests read rows by name. Never a dispatch key.
	const FElysiumNpcClass* ClassNamed(const TCHAR* Name);

	// Each port class's generated slot rows, defined at the end of its generated `…Slots.cpp`.
	TArrayView<const TElysiumNpcSlotRow<FElysiumEntity>> EntitySlotRows();
	TArrayView<const TElysiumNpcSlotRow<FElysiumAnimating>> AnimatingSlotRows();
	TArrayView<const TElysiumNpcSlotRow<FElysiumAnimatingOverlay>> AnimatingOverlaySlotRows();
	TArrayView<const TElysiumNpcSlotRow<FElysiumFlex>> FlexSlotRows();
	TArrayView<const TElysiumNpcSlotRow<FElysiumCombatCharacter>> CombatCharacterSlotRows();
	TArrayView<const TElysiumNpcSlotRow<FElysiumNpcBase>> NpcBaseSlotRows();
	TArrayView<const TElysiumNpcSlotRow<FElysiumNpc>> NpcSlotRows();

	// Whether `&TClass::Method` names a method declared on `TClass` itself, for one signature.
	// Deduction picks the one overload with that signature out of the name's set, and the member
	// pointer's class is the class that declares it: a method only a base declares deduces the base.
	template <typename TClass, typename TSignature>
	struct TDeclaredOn;
	template <typename TClass, typename TRet, typename... TArgs>
	struct TDeclaredOn<TClass, TRet(TArgs...)>
	{
		template <typename TOwner>
		static constexpr bool Test(TRet (TOwner::*)(TArgs...)) { return std::is_same_v<TOwner, TClass>; }
	};
	template <typename TClass, typename TRet, typename... TArgs>
	struct TDeclaredOn<TClass, TRet(TArgs...) const>
	{
		template <typename TOwner>
		static constexpr bool Test(TRet (TOwner::*)(TArgs...) const) { return std::is_same_v<TOwner, TClass>; }
	};

	// The ledger's own spelling of a tier (`datamap`, `sdk-order`, `walked`…), which is what the
	// digest folds and what a diagnostic prints.
	const TCHAR* TierName(EElysiumNpcShapeTier Tier);

	// The digest the test recomputes from the committed rows: FNV-1a 64 over the same row stream
	// the generator hashed, so the two agree only if nothing was dropped or reordered.
	uint64 DigestOfRows();
}
