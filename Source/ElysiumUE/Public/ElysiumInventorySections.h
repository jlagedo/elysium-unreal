#pragma once

#include "CoreMinimal.h"

// `system/items.txt`'s `InventorySections` block — the inventory's category vocabulary.
//
// Plain C++ and no reflection: the substrate carries the cursor, the view state projects it and the
// HUD renders its name, so the enum is shared rather than mirrored in each of the three. The
// type-to-section join lives with the rulebook, because it reads `EElysiumItemType`.

// The block's authored order. The value IS the section's index there, which is what the `slotN`
// verbs select. `None` and `Hidden` author `IsDisplayed 0`; the six between them are the categories
// the player browses.
enum class EElysiumInvSection : uint8
{
	None = 0,
	WeaponMelee,
	WeaponRanged,
	WeaponThrown,
	Armor,
	Generic,
	Powerups,
	Hidden,
	Count,
};

// A section the player can browse — one the block marks displayed.
inline bool ElysiumSectionIsBrowsable(EElysiumInvSection Section)
{
	return Section != EElysiumInvSection::None
		&& Section != EElysiumInvSection::Hidden
		&& Section != EElysiumInvSection::Count;
}

// The section a `slotN` verb selects. Retail's `kb_act.lst` numbers the keys from the same block,
// offset by one because `slot1` addressed a `Disciplines` section this runtime does not browse —
// so `slot2` is `Weapon_Melee`, the first remaining section. A number outside the block
// resolves to `None`, which is not browsable and therefore selects nothing.
inline EElysiumInvSection ElysiumSectionForSlot(int32 SlotNumber)
{
	const int32 Index = SlotNumber - 1;
	return (Index > 0 && Index < (int32)EElysiumInvSection::Count)
		? (EElysiumInvSection)Index : EElysiumInvSection::None;
}

// The block's own `Name` field, for the selector heading.
inline const TCHAR* ElysiumInvSectionName(EElysiumInvSection Section)
{
	switch (Section)
	{
	case EElysiumInvSection::WeaponMelee:  return TEXT("Weapon (Melee)");
	case EElysiumInvSection::WeaponRanged: return TEXT("Weapon (Ranged)");
	case EElysiumInvSection::WeaponThrown: return TEXT("Weapon (Thrown)");
	case EElysiumInvSection::Armor:        return TEXT("Armor");
	case EElysiumInvSection::Generic:      return TEXT("General");
	case EElysiumInvSection::Powerups:     return TEXT("Powerups");
	case EElysiumInvSection::Hidden:       return TEXT("Hidden");
	default:                               return TEXT("None");
	}
}
