#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

struct IElysiumRetailSiteSink;

// The VSound folder membership index (`CVSoundFileFolder_t`, the name retail's own warning gives the
// type; `docs/specs/layers/L0-entity/walks/L0-r006.md` § 0x101f3ba0 / 0x101f3b00). A tree of folder
// nodes under an owner `T`; each node holds one byte per (category, index) member of `T`'s flat
// address space, and the two rows ported here keep that mask in step with `T`'s per-category counts
// (`AddRange`) and search it (`Find`). The callers that fill and read the index are L2's
// (`FUN_101f3d00`, the sound-name parser, audit `settled:engine_replaced`; `FUN_101f4600`, the wav
// picker, `ported`): what this layer owns is the index itself.
//
// Layout read by the four functions (offsets from the node): `+0x00` key, `+0x04` owner `T`, `+0x08`
// sibling, `+0x14` mask (a byte pointer), `+0x18` mask length, `+0x1c` child count, `+0x20` child
// pointer array. The owner: `*(T + 0xc)` the int32 per-category counts, `*(T + 8)` the category
// table whose `+0x14` is the category count (NULL -> no categories), the root node embedded at
// `T + 0x10` (its `+4` is `T`). The constructor and the owner's concrete type were not read
// (UNRECOVERED): the layout above is the four bodies' own.
namespace ElysiumSoundFolder
{
	struct FOwner;

	struct FNode
	{
		int32 Key = 0;                         // +0x00
		FOwner* Owner = nullptr;               // +0x04, the owner `T`
		FNode* Sibling = nullptr;              // +0x08
		TArray<uint8> Mask;                    // +0x14 / +0x18: one byte per flat member, non-zero = member
		TArray<TUniquePtr<FNode>> Children;    // +0x1c / +0x20
		// A record's name for this node (a site payload names the node it visits by it; retail's
		// identity is the node's address, which no record can spell).
		FString Label;
	};

	// The owner `T`.
	struct FOwner
	{
		virtual ~FOwner() = default;

		TArray<int32> Counts;                  // *(T + 0xc): members per category
		int32 CategoryCount = 0;               // *(*(T + 8) + 0x14); 0 stands for a NULL category table
		FNode Root;                            // T + 0x10

		// HOOK (`hooks.tsv:107`, L0 -> L2): `FUN_101f4530` `0x101f4530`, the flat index `F(cat, idx)`
		// over the owner's category layout -- `sum(counts[0..cat-1]) + idx`, `idx` for `cat <= 0`, as
		// the walk read it. The owner's layout is L2's to complete (the sound-group / character owner
		// that builds `T`), so this layer declares the question and never answers it: pure virtual.
		virtual int32 FlatIndex(int32 Category, int32 Index) const = 0;

		// `FUN_101f4300` `0x101f4300` (`__fastcall owner`): 0 with no category table, else the sum of
		// the first `CategoryCount` counts.
		int32 Total() const;

		// `FUN_101f4330` `0x101f4330` (`__thiscall(T, cat, hi)`): grow category `cat` to hold index `hi`
		// -- if `counts[cat] < hi + 1`: `counts[cat] = hi + 1`, then `FUN_101f3ba0(root, cat, hi, old - 1)`
		// (the count is stored first, so the insert's `Total` already sees it).
		void AddRange(int32 Category, int32 Hi, IElysiumRetailSiteSink* Sites);

		// `FUN_101f42d0` `0x101f42d0` (`__thiscall(T, key, cat, idx)`): `key == -1` -> NULL; else
		// `FUN_101f3b00(root, key, cat, idx)`.
		FNode* Find(int32 Key, int32 Category, int32 Index, IElysiumRetailSiteSink* Sites);
	};

	// `FUN_101f3ba0` `0x101f3ba0` (258 B, `__thiscall(node, cat, hi, lo)`, `RET 0xc`): open the flat
	// range `(F(cat, lo), F(cat, hi)]` in this node's mask -- the bytes before it kept, the new bytes
	// zero, the bytes after it moved up -- then the same on every child. Arms B0-B10 in retail order.
	void InsertRange(FNode& Node, int32 Category, int32 Hi, int32 Lo, IElysiumRetailSiteSink* Sites);

	// `FUN_101f3b00` `0x101f3b00` (113 B, `__thiscall(node, key, cat, idx)`, `RET 0xc`): the first node
	// whose key matches and whose mask holds `F(cat, idx)`: on a key match the node and then every
	// sibling are tested (the loop's key test is a self-compare, `asm 0x101f3b30`), and the children
	// are never searched; on a mismatch each child is searched in turn. Arms F0-F2.
	FNode* FindNode(FNode& Node, int32 Key, int32 Category, int32 Index, IElysiumRetailSiteSink* Sites);

	// The mask as a record spells it: the bytes joined by commas (`1,0,1`), `-` for an empty mask.
	FString MaskText(const FNode& Node);
}
