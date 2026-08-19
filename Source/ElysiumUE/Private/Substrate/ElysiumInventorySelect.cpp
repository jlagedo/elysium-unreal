#include "ElysiumEntityWorld.h"
#include "ElysiumInventorySections.h"
#include "ElysiumPlayer.h"
#include "ElysiumViewState.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumInvSelect, Log, All);

// Inventory selection — the selector's authority (8.9's selector clause).
//
// One cursor, two commits. `system/items.txt` declares the categories (`InventorySections`) and the
// section each item type files under, so the vocabulary is authored rather than chosen here: the
// `slotN` verbs move the section, `invnext`/`invprev` move the item inside whichever section is
// current, and what a selection MEANS depends on the section. A wielded section commits through
// `FElysiumInventory::SetActiveWeapon`, the same equip funnel every other switch uses; the rest
// park the choice on `SelectedItem`, which is what `inven_drop_curr` calls "the current item".
//
// `Equip`'s automatic switch is gated on `is_wieldable`, which no shipped `item_w_*` record authors,
// so nothing in a played session ever reached the switch from input. `SetActiveWeapon` carries no
// such gate — it asks only that the item is carried — which is why selection funnels through it.

namespace
{
	EElysiumViewWeaponFamily FamilyFor(const FElysiumItemDef& Record)
	{
		switch (Record.Type)
		{
		case EElysiumItemType::WeaponMelee:   return EElysiumViewWeaponFamily::Melee;
		case EElysiumItemType::WeaponFirearm: return EElysiumViewWeaponFamily::Firearm;
		case EElysiumItemType::WeaponThrown:  return EElysiumViewWeaponFamily::Thrown;
		default:                              return EElysiumViewWeaponFamily::None;
		}
	}

	// Bare hands are a melee weapon record like any other, and the two that stand for them author
	// `bucket 0`/`bucket_position 0`. The selector shows them as Unarmed so the readout drops its
	// ammunition row and the category glyph is the fist rather than the melee icon.
	bool IsBareHands(const FString& Classname)
	{
		return Classname.Equals(TEXT("item_w_unarmed"), ESearchCase::IgnoreCase)
			|| Classname.Equals(TEXT("item_w_fists"), ESearchCase::IgnoreCase);
	}

	FElysiumPlayer* PlayerOf(FElysiumEntityWorld& World)
	{
		FElysiumPlayer* Player = World.FindPlayer();
		if (!Player)
		{
			UE_LOG(LogElysiumInvSelect, Warning,
				TEXT("inventory selection: no player entity in this world - the verb reached a world ")
				TEXT("with nothing to select in"));
		}
		return Player;
	}

	// What the cursor currently names inside `Items`, or INDEX_NONE. A wielded section is answered by
	// the active weapon, because that is where its selection actually lives.
	int32 SelectedIndexIn(const FElysiumInventory& Inventory, EElysiumInvSection Section,
		const TArray<FElysiumItem*>& Items)
	{
		const FElysiumEntityHandle Cursor = ElysiumItems::SectionIsWielded(Section)
			? Inventory.ActiveWeapon : Inventory.SelectedItem;
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			if (Items[i]->Handle == Cursor)
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	// Commit a selection. The section decides which of the two homes it lands in.
	bool Commit(FElysiumPlayer& Player, EElysiumInvSection Section, FElysiumItem& Item)
	{
		if (ElysiumItems::SectionIsWielded(Section))
		{
			return Player.Inventory.SetActiveWeapon(Player, Item);
		}
		// A non-wielded selection changes nothing about the world — it moves a cursor. Ownership is
		// still checked, so the cursor cannot name an item the player is not carrying.
		if (!Item.IsOwned() || Item.Owner != Player.Handle)
		{
			return false;
		}
		Player.Inventory.SelectedItem = Item.Handle;
		return true;
	}

	// The section the cursor starts in when nothing has chosen one: whatever the weapon in hand files
	// under, falling back to the first browsable section carrying anything.
	EElysiumInvSection ResolveStartSection(FElysiumPlayer& Player)
	{
		if (FElysiumItem* Active = Player.Inventory.Active(Player))
		{
			if (const FElysiumItemDef* Record = Active->Data())
			{
				const EElysiumInvSection Section = ElysiumSectionForItemType(Record->Type);
				if (ElysiumSectionIsBrowsable(Section))
				{
					return Section;
				}
			}
		}
		for (int32 Index = 1; Index < (int32)EElysiumInvSection::Count; ++Index)
		{
			const EElysiumInvSection Section = (EElysiumInvSection)Index;
			if (!ElysiumSectionIsBrowsable(Section))
			{
				continue;
			}
			TArray<FElysiumItem*> Items;
			ElysiumItems::CollectSection(Player, Section, Items);
			if (Items.Num() > 0)
			{
				return Section;
			}
		}
		return EElysiumInvSection::None;
	}
}

namespace ElysiumItems
{

bool SectionIsWielded(EElysiumInvSection Section)
{
	// `IsWielded` is authored per item type, and the three weapon families are the types carrying it
	// into a browsable section. Asking the section rather than the item keeps an empty section's
	// commit rule defined.
	return Section == EElysiumInvSection::WeaponMelee
		|| Section == EElysiumInvSection::WeaponRanged
		|| Section == EElysiumInvSection::WeaponThrown;
}

void CollectSection(const FElysiumCombatCharacter& Char, EElysiumInvSection Section,
	TArray<FElysiumItem*>& OutItems)
{
	OutItems.Reset();
	if (!ElysiumSectionIsBrowsable(Section))
	{
		return;
	}

	const FElysiumInventory& Inventory = Char.Inventory;
	for (int32 Position = 0; Position < Inventory.Num(); ++Position)
	{
		FElysiumItem* Item = Inventory.At(Char, Position);
		if (!Item)
		{
			// A compact list holding a handle that resolves to nothing is a broken invariant, not an
			// empty slot: `Inventory_Remove` closes gaps and repairs positions.
			UE_LOG(LogElysiumInvSelect, Warning,
				TEXT("inventory selection: position %d of %s resolves to no item - the compact slot ")
				TEXT("list disagrees with the item entities it names"),
				Position, *Char.DebugString());
			continue;
		}

		const FElysiumItemDef* Record = Item->Data();
		if (!Record)
		{
			UE_LOG(LogElysiumInvSelect, Verbose,
				TEXT("inventory selection: '%s' has no `vdata/items` record and cannot be selected"),
				*Item->ClassName());
			continue;
		}
		// The record's own two gates: the section its type files under, and whether the author put it
		// in front of the player at all.
		if (ElysiumSectionForItemType(Record->Type) != Section || !Record->bVisibleInHud)
		{
			continue;
		}
		OutItems.Add(Item);
	}

	// The authored order: the selection column, then the row inside it. Classname breaks the ties the
	// shipped catalogue actually contains, so the order a player learns does not depend on pickup
	// sequence. An item authoring no `bucket` sorts at the front of its section by name.
	OutItems.Sort([](const FElysiumItem& A, const FElysiumItem& B)
	{
		const FElysiumItemDef* RA = A.Data();
		const FElysiumItemDef* RB = B.Data();
		if (RA->Bucket != RB->Bucket)
		{
			return RA->Bucket < RB->Bucket;
		}
		if (RA->BucketPosition != RB->BucketPosition)
		{
			return RA->BucketPosition < RB->BucketPosition;
		}
		return A.ClassName() < B.ClassName();
	});
}

bool CycleSelection(FElysiumEntityWorld& World, int32 Delta)
{
	FElysiumPlayer* Player = PlayerOf(World);
	if (!Player)
	{
		return false;
	}

	FElysiumInventory& Inventory = Player->Inventory;
	if (!ElysiumSectionIsBrowsable(Inventory.CurrentSection))
	{
		// Nothing has chosen a category yet — the first cycle of a session picks the one the hand is
		// already in rather than refusing.
		Inventory.CurrentSection = ResolveStartSection(*Player);
	}

	TArray<FElysiumItem*> Items;
	CollectSection(*Player, Inventory.CurrentSection, Items);
	if (Items.Num() == 0)
	{
		UE_LOG(LogElysiumInvSelect, Verbose,
			TEXT("inventory cycle: the player carries nothing in '%s'"),
			ElysiumInvSectionName(Inventory.CurrentSection));
		return false;
	}

	const int32 Current = SelectedIndexIn(Inventory, Inventory.CurrentSection, Items);
	// Nothing selected starts the cycle at the section's first entry going forward and its last going
	// back, so the first press always lands somewhere.
	int32 Next = 0;
	if (Current != INDEX_NONE)
	{
		Next = (Current + Delta) % Items.Num();
		if (Next < 0)
		{
			Next += Items.Num();
		}
	}
	else if (Delta < 0)
	{
		Next = Items.Num() - 1;
	}

	return Commit(*Player, Inventory.CurrentSection, *Items[Next]);
}

bool SelectSection(FElysiumEntityWorld& World, EElysiumInvSection Section)
{
	FElysiumPlayer* Player = PlayerOf(World);
	if (!Player)
	{
		return false;
	}
	if (!ElysiumSectionIsBrowsable(Section))
	{
		// `None` and `Hidden` are authored as undisplayed. A verb naming one is a no-op the player can
		// see nothing of, so it says so rather than selecting silently.
		UE_LOG(LogElysiumInvSelect, Verbose,
			TEXT("inventory category '%s' is not displayed and cannot be browsed"),
			ElysiumInvSectionName(Section));
		return false;
	}

	TArray<FElysiumItem*> Items;
	CollectSection(*Player, Section, Items);
	if (Items.Num() == 0)
	{
		UE_LOG(LogElysiumInvSelect, Verbose,
			TEXT("inventory category '%s': the player carries nothing in it"),
			ElysiumInvSectionName(Section));
		return false;
	}

	FElysiumInventory& Inventory = Player->Inventory;
	// Pressing the category key while already inside it advances within it — the retail behaviour of
	// the `slotN` keys, and the reason the key is worth pressing twice.
	const bool bAlreadyHere = Inventory.CurrentSection == Section;
	Inventory.CurrentSection = Section;

	const int32 Current = bAlreadyHere ? SelectedIndexIn(Inventory, Section, Items) : INDEX_NONE;
	const int32 Next = Current == INDEX_NONE ? 0 : (Current + 1) % Items.Num();
	return Commit(*Player, Section, *Items[Next]);
}

bool SelectLastWeapon(FElysiumEntityWorld& World)
{
	FElysiumPlayer* Player = PlayerOf(World);
	if (!Player)
	{
		return false;
	}

	FElysiumInventory& Inventory = Player->Inventory;
	if (!Inventory.PreviousWeapon.IsSet())
	{
		UE_LOG(LogElysiumInvSelect, Verbose,
			TEXT("lastinv: no weapon has been switched away from yet"));
		return false;
	}

	// The remembered weapon may have been dropped, sold or destroyed since. That is an ordinary
	// outcome of the verb, not a failure of it.
	FElysiumItem* Previous = nullptr;
	for (int32 Position = 0; Position < Inventory.Num(); ++Position)
	{
		FElysiumItem* Item = Inventory.At(*Player, Position);
		if (Item && Item->Handle == Inventory.PreviousWeapon)
		{
			Previous = Item;
			break;
		}
	}
	if (!Previous)
	{
		UE_LOG(LogElysiumInvSelect, Verbose,
			TEXT("lastinv: the previously held weapon is no longer carried"));
		return false;
	}

	// Returning to a weapon returns to its category too, so the next cycle continues where the hand
	// actually is rather than in whatever was last browsed.
	if (const FElysiumItemDef* Record = Previous->Data())
	{
		const EElysiumInvSection Section = ElysiumSectionForItemType(Record->Type);
		if (ElysiumSectionIsBrowsable(Section))
		{
			Inventory.CurrentSection = Section;
		}
	}
	return Inventory.SetActiveWeapon(*Player, *Previous);
}

void BuildInventoryView(const FElysiumEntityWorld& World, FElysiumEquipmentView& Out)
{
	Out = FElysiumEquipmentView();

	const FElysiumPlayer* Player = World.FindPlayer();
	if (!Player)
	{
		// No player is a state a backdrop and a headless logic world both run in, not a failure.
		return;
	}
	if (Table() == nullptr)
	{
		// A world with a player but no catalogue cannot name an item, and drawing an empty selector
		// would claim the player carries nothing. Invalid says "unknown" instead.
		UE_LOG(LogElysiumInvSelect, Verbose,
			TEXT("inventory view: no `vdata/items` catalogue is installed"));
		return;
	}

	Out.bValid = true;
	const FElysiumInventory& Inventory = Player->Inventory;

	auto Describe = [&Inventory](const FElysiumItem& Item, FElysiumInventoryEntryView& Entry)
	{
		const FElysiumItemDef* Record = Item.Data();
		Entry.Classname = Item.ClassName();
		Entry.Label = Record->PrintName.IsEmpty() ? Item.ClassName() : Record->PrintName;
		Entry.Family = IsBareHands(Item.ClassName())
			? EElysiumViewWeaponFamily::Unarmed
			: FamilyFor(*Record);
		// A stack's count is what the player sees; a non-stackable item is one of a thing and says
		// nothing, rather than saying "1".
		Entry.Quantity = Record->bStackable ? Item.ItemCount : 0;
		// A magazine the record does not author is not an empty magazine: a tire iron shows no
		// ammunition row at all, while an unloaded pistol shows `0`.
		Entry.bHasMagazine = !Record->AmmoType.IsEmpty();
		if (Entry.bHasMagazine)
		{
			Entry.AmmoCurrent = Item.MagazineCount;
			Entry.AmmoReserve = Inventory.Reserve(Record->AmmoType);
		}
	};

	// The weapon in hand — the persistent readout, independent of whichever section is being browsed.
	if (FElysiumItem* Active = Inventory.Active(*Player))
	{
		if (Active->Data() != nullptr)
		{
			Out.bEquippedValid = true;
			Describe(*Active, Out.Equipped);
		}
	}

	// What is worn. `IsWorn` is authored per item type; armour is the slot the HUD draws.
	for (int32 Position = 0; Position < Inventory.Num(); ++Position)
	{
		FElysiumItem* Item = Inventory.At(*Player, Position);
		const FElysiumItemDef* Record = Item ? Item->Data() : nullptr;
		if (Record && Record->Type == EElysiumItemType::Armor && ElysiumItemTypeIsWorn(Record->Type))
		{
			Out.bWornValid = true;
			Describe(*Item, Out.Worn);
			break;
		}
	}

	// The section being browsed, and its rows.
	Out.Section = Inventory.CurrentSection;
	if (!ElysiumSectionIsBrowsable(Out.Section))
	{
		return;
	}

	TArray<FElysiumItem*> Items;
	CollectSection(*Player, Out.Section, Items);
	Out.Entries.Reserve(Items.Num());

	const FElysiumEntityHandle Cursor = SectionIsWielded(Out.Section)
		? Inventory.ActiveWeapon : Inventory.SelectedItem;
	for (FElysiumItem* Item : Items)
	{
		FElysiumInventoryEntryView& Entry = Out.Entries.AddDefaulted_GetRef();
		Describe(*Item, Entry);
		if (Item->Handle == Cursor)
		{
			Out.SelectedIndex = Out.Entries.Num() - 1;
		}
	}
}

} // namespace ElysiumItems
