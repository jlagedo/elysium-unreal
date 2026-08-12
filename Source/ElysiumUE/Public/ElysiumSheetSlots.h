#pragma once

#include "CoreMinimal.h"

// The compiled half of VtMB's character sheet: which trait sits in which slot, and what the engine
// datamap calls it.
//
// VtMB splits the sheet the same way. `CBaseCombatCharacter` holds fixed-size int arrays at fixed
// offsets — `m_iVAttributesBase` `+0x10F0` to `m_iVAttributesCurrent` `+0x117C` is `0x8C`, i.e. 35
// ints; Abilities, Disciplines and Active_Disciplines are `0x34` = 13 each — and each slot's
// external name is a hardcoded `typedescription_t` row in `vampire.dll`. Only the *values* are data:
// `vdata/system/stats.txt` supplies which stat occupies each slot (file position IS the trait id),
// its `Min`/`Max`/`Default`/`Costs`, and every by-name cross-reference `CVStatRef` resolves.
//
// So the table below is code and the rulebook (`Private/Substrate/ElysiumRulebook.h`) is data, and
// `Elysium.Content.Sheet` asserts the two still agree slot for slot against the real file.
//
// **Disciplines are 13, not 17.** `stats.txt` authors 17 — the last four are the Numina powers —
// but the compiled array and the save array are both 13, so the Numina rows are file-only: a feat
// or a trait effect can name one, and a vampire-mode character has nowhere to store its value.
//
// The VtMB facts: `docs/vtmb/game_runtime.md` section 3 ("How a trait is addressed"), `docs/vtmb/savegame_format.md`.

// The four containers, in `stats.txt`'s own declaration order — which is also the category tag
// VtMB writes at `CVStatList_t+0x10`.
enum class EElysiumTraitContainer : uint8
{
	Attributes = 0,
	Abilities,
	Disciplines,
	ActiveDisciplines,
	Count,
};

const TCHAR* ElysiumTraitContainerName(EElysiumTraitContainer Container);

// One compiled datamap row.
//
// `Datamap` is the name a Hammer keyvalue, a Python `__getattr__` and the save walk resolve; the
// base array is reached as `base_<Datamap>`. `Internal` is the `stats.txt` `InternalName`, which is
// what `BumpStat` and the `feats.txt` / `traiteffects000.txt` trait references resolve instead —
// two spellings, one slot. They differ on five rows; everything else is the lowercased internal
// name. `Alias` is a second accepted datamap spelling, or null.
struct FElysiumSheetSlot
{
	int32 Index = INDEX_NONE;
	const TCHAR* Datamap = nullptr;
	const TCHAR* Internal = nullptr;
	const TCHAR* Alias = nullptr;
};

// The container's slots, in slot order — `Slots[i].Index == i` holds, and the leading `*_Order`
// block occupies 0 on Attributes and Abilities.
TArrayView<const FElysiumSheetSlot> ElysiumSheetSlots(EElysiumTraitContainer Container);

inline int32 ElysiumSheetSlotCount(EElysiumTraitContainer Container)
{
	return ElysiumSheetSlots(Container).Num();
}

// Resolve a trait by its `stats.txt` InternalName across all four containers, in declaration order
// — `CVStatRef`'s own search. Case-insensitive. Returns false when nothing owns the name.
bool ElysiumFindSheetSlot(const TCHAR* InternalName, EElysiumTraitContainer& OutContainer, int32& OutSlot);

// The slots the runtime addresses by name rather than by walking the table. Attributes only —
// no other container has a slot the C++ needs to know about.
namespace ElysiumSlot
{
	constexpr int32 AttribOrder    = 0;
	constexpr int32 Strength       = 1;
	constexpr int32 Dexterity      = 2;
	constexpr int32 Stamina        = 3;
	constexpr int32 Clan           = 10;
	constexpr int32 Gender         = 11;   // 0 = female, 1 = male
	constexpr int32 BloodPool      = 12;
	constexpr int32 BloodPoolMax   = 13;
	constexpr int32 Health         = 15;   // damage TAKEN, not hit points remaining (RE24)
	constexpr int32 HealthAggDmg   = 16;
	constexpr int32 MaxHealth      = 17;
	constexpr int32 Generation     = 18;
	constexpr int32 HealthBuffer   = 25;
	constexpr int32 Humanity       = 27;
	constexpr int32 Masquerade     = 28;
	constexpr int32 ExpModifier    = 29;
	constexpr int32 Experience     = 34;
}
