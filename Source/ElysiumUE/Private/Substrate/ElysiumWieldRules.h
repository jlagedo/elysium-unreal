#pragma once

#include "CoreMinimal.h"

// Who may WIELD what — `CBaseCombatCharacter::Inventory_Can_Wield` (`vampire.dll` 0x10335a70) and
// the two authored halves it joins (`docs/vtmb/wielded_weapons.md` § "Who may wield what").
//
// The join is a bitmask on each side:
//
//   * the WEAPON side is the item record's `equip_mask` (item parser 0x10259f80, stored at
//     record +0x5eb1c), parsed by `ParseEquipFlag` (0x1025b740);
//   * the CHARACTER side is one row of the `ExcludedEquipTables` block of
//     `vdata/system/items.txt` (block loader 0x101ecde0, row parser 0x101ecb90), selected by the
//     character's `Excluded_Equipment` stat — sheet slot 31, `excluded_equipment`. A template
//     authors it by NAME (`"Excluded_Equipment" "Default"`) and the row id is the block order
//     starting at 0, so `Default` is row 0.
//
// This is a rulebook table like the twelve in `Substrate/ElysiumRulebook.h`; it lives in its own
// pair because the flag vocabulary, the row set and the evaluator are one tightly coupled cluster
// and because the item side (`FElysiumItemDef::EquipMask`) parses through the same helper.

// `ParseEquipFlag` (0x1025b740) — the name table, index = bit, recovered from the string table at
// 0x105c7638 (strings 0x105c76f8..0x105c775c). The compare is `strcmpi`, so a name matches in any
// casing. `normal` is NOT in the table: the function answers 0x50 for it (bits 4|6,
// `no_wolfform | no_clawedform`), which is why it is spelled out as its own constant below.
namespace ElysiumEquipFlags
{
	enum : uint32
	{
		Never        = 1u << 0,   // "never" — nothing may ever wield this
		BlueBlood    = 1u << 1,   // "blueblood"
		NoBlueBlood  = 1u << 2,   // "no_blueblood"
		WolfForm     = 1u << 3,   // "wolfform"
		NoWolfForm   = 1u << 4,   // "no_wolfform"
		ClawedForm   = 1u << 5,   // "clawedform"
		NoClawedForm = 1u << 6,   // "no_clawedform"
		NoNpc        = 1u << 7,   // "no_npc"
		// Bit 8 is the UNRECOVERED ninth name. The string table carries nine entries and the ninth
		// could not be read back; no shipped item record and no shipped `ExcludedEquip` row uses it,
		// so nothing observable depends on its spelling. The bit is reserved so the indices below it
		// stay the engine's own.
		Unrecovered8 = 1u << 8,
	};

	// The literal `normal` — `no_wolfform | no_clawedform`. 19 of the 226 shipped item files author
	// it, which makes it the commonest authored value by a wide margin.
	inline constexpr uint32 Normal = NoWolfForm | NoClawedForm;

	// One token's value, `strcmpi` against the name table. 0 for a name the table does not carry —
	// which is `ParseEquipFlag`'s own answer for an unknown token, not an error code.
	uint32 Bit(const FString& Token);

	// The whole authored value: one `ParseEquipFlag` call per whitespace-separated token,
	// accumulated by the function's own rule — a value that is neither 0 nor 1 CLEARS bit 0 of the
	// accumulator before it is OR'd in, so `Never` beside any other flag stops meaning "never".
	// An absent/empty key is mask 0, which no arm of the evaluator refuses.
	uint32 Parse(const FString& Authored);

	// The mask read back as the names that built it, for the inspector and the test messages.
	FString Describe(uint32 Mask);
}

// One `ExcludedEquip` block. `InternalName` is the name a stat template authors; the two masks are
// the accumulated `ExcludedFlag` and `RequiredFlag` keys, each of which REPEATS inside a row.
struct FElysiumExcludedEquipRow
{
	FString InternalName;
	FString Name;
	uint32 Excluded = 0;
	uint32 Required = 0;
	int32 Index = INDEX_NONE;   // block order — the value `Excluded_Equipment` holds
};

// The `ExcludedEquipTables` block, in file order.
struct FElysiumExcludedEquipTable
{
	TArray<FElysiumExcludedEquipRow> Rows;

	// Reads `vdata/system/items.txt`. The per-item files are `FElysiumItemTable`'s; this is the one
	// block of the SYSTEM file the wield rule needs, and it is loaded on its own so a broken item
	// directory and a broken rule table are two separate failures.
	bool Load(FString& OutError);
	bool IsValid() const { return !Rows.IsEmpty(); }
	int32 Num() const { return Rows.Num(); }

	// Rebuild the name index and re-stamp each row's `Index` to its position — block order IS the
	// row id, so the two cannot disagree. `Load` calls it; a hand-built table (the tests') needs it
	// because the lookup IS the index, not a scan (the rule `FElysiumQuestTables` set).
	void Reindex();

	const FElysiumExcludedEquipRow* At(int32 Index) const;
	const FElysiumExcludedEquipRow* Find(const FString& InternalName) const;
	// The row id a template's authored NAME selects, or INDEX_NONE. This is what stands in for
	// retail's `ExcludedEquipFunc` (the `NameFunc` `stats.txt` gives the `Excluded_Equipment` stat).
	int32 RowIndexByName(const FString& InternalName) const;

	// `Inventory_Can_Wield`'s callback body (0x10220460), verbatim:
	//
	//   * `mask & Never`                                  -> no
	//   * `Row.Excluded & mask`                           -> no
	//   * `Row.Required != 0 && (mask & Row.Required)==0` -> no
	//   * otherwise                                       -> yes
	//
	// A `RowIndex` the table does not carry answers YES: retail selects the row by index and a
	// character whose stat names no row is unrestricted, which is also what a world with no rulebook
	// loaded has to answer so a headless run is not silently disarmed.
	//
	// (The thunk edge from 0x10335a70 to this body is not resolvable in the corpus — the callback is
	// reached through an indirect call the decompilation does not bind. The body, its two callers'
	// register setup and the shipped data agree, which is the evidence this reproduces.)
	bool CanWield(int32 RowIndex, uint32 EquipMask) const;

private:
	TMap<FString, int32> ByName;
};
