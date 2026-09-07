#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumGameStateSubsystem.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSheetMath.h"
#include "Substrate/ElysiumWieldRules.h"

namespace
{
	// The item an inventory handle points at, or null when it went stale.
	FElysiumItem* ResolveItem(const FElysiumCombatCharacter& Char, const FElysiumEntityHandle& Handle)
	{
		FElysiumEntityWorld* World = Char.World;
		FElysiumEntity* E = World ? World->Resolve(Handle) : nullptr;
		return E ? E->AsItem() : nullptr;
	}
}

FElysiumItem* FElysiumInventory::At(const FElysiumCombatCharacter& Char, int32 Position) const
{
	return Slots.IsValidIndex(Position) ? ResolveItem(Char, Slots[Position]) : nullptr;
}

FElysiumItem* FElysiumInventory::Active(const FElysiumCombatCharacter& Char) const
{
	return ResolveItem(Char, ActiveWeapon);
}

FElysiumItem* FElysiumInventory::FindOrdinary(const FElysiumCombatCharacter& Char,
	const FString& Classname) const
{
	if (Classname.IsEmpty())
	{
		return nullptr;
	}
	// Retail iterates all 224 handles; the compact list is the same walk without the empty slots.
	for (const FElysiumEntityHandle& Handle : Slots)
	{
		FElysiumItem* Item = ResolveItem(Char, Handle);
		if (Item && Item->ClassName().Equals(Classname, ESearchCase::IgnoreCase))
		{
			return Item;
		}
	}
	return nullptr;
}

FElysiumKeyring* FElysiumInventory::FindKeyring(const FElysiumCombatCharacter& Char) const
{
	for (const FElysiumEntityHandle& Handle : Slots)
	{
		FElysiumItem* Item = ResolveItem(Char, Handle);
		if (FElysiumKeyring* Ring = Item ? Item->AsKeyring() : nullptr)
		{
			return Ring;
		}
	}
	return nullptr;
}

bool FElysiumInventory::Has(const FElysiumCombatCharacter& Char, const FString& Classname) const
{
	// The ordinary slots first, then the keyring's records — `HasItem`'s own order, both halves
	// case-insensitive.
	if (FindOrdinary(Char, Classname) != nullptr)
	{
		return true;
	}
	const FElysiumKeyring* Ring = FindKeyring(Char);
	return Ring != nullptr && Ring->HasKey(Classname);
}

namespace
{
	void NotifyPlayerItemReceived(FElysiumCombatCharacter& Char, FElysiumItem& Item,
		int32 ReceivedQuantity)
	{
		FElysiumEntityWorld* World = Char.World;
		if (!World || !World->PlayerHandle().IsSet()
			|| Char.Handle.Index != World->PlayerHandle().Index)
		{
			return;
		}

		UE_LOG(LogElysiumItem, Display, TEXT("INFO - Item received: %s x%d"),
			*Item.ClassName(), ReceivedQuantity);

		IElysiumPresenter* Presenter = World->Presenter();
		if (!Presenter)
		{
			return; // a headless substrate world: the stable gameplay log remains the observable surface
		}

		const FElysiumItemDef* Record = Item.Data();
		FString Subject = Record ? Record->PrintName.TrimStartAndEnd() : FString();
		if (Subject.IsEmpty())
		{
			Subject = Item.ClassName();
			UE_LOG(LogElysiumItem, Warning,
				TEXT("player item '%s' has no printname; notification uses the classname"),
				*Item.ClassName());
		}

		FElysiumNotification Notification;
		Notification.Kind = EElysiumNotificationKind::ItemAcquired;
		Notification.Subject = MoveTemp(Subject);
		Notification.Quantity = FMath::Max(1, ReceivedQuantity);
		Presenter->PostNotification(Notification);
	}
}

bool FElysiumInventory::Add(FElysiumCombatCharacter& Char, FElysiumItem& Item)
{
	if (Item.IsOwned())
	{
		return false;
	}
	const int32 ReceivedQuantity = FMath::Max(1, Item.ItemCount);

	// Stacking is the item data's call, never the classname's. A stackable classname already
	// carried merges into that stack; the incoming entity is left unowned for the caller to
	// dispose of, because a merge produces no second carried entity.
	if (Item.IsStackable())
	{
		if (FElysiumItem* Existing = FindOrdinary(Char, Item.ClassName()))
		{
			const int32 Merged = Existing->ItemCount + FMath::Max(1, Item.ItemCount);
			if (!Existing->StackHasRoomFor(Merged))
			{
				return false;
			}
			Existing->ItemCount = Merged;
			NotifyPlayerItemReceived(Char, Item, ReceivedQuantity);
			return true;
		}
	}

	if (IsFull())
	{
		UE_LOG(LogElysiumItem, Log, TEXT("%s cannot take %s — all %d slots are full"),
			*Char.DebugString(), *Item.DebugString(), MaxSlots);
		return false;
	}

	Item.DestroyWorldBody();   // carried, so it has no presence in the world
	Item.Owner = Char.Handle;
	Item.InvenPos = Slots.Num();
	Item.ItemCount = FMath::Max(1, Item.ItemCount);
	Slots.Add(Item.Handle);
	NotifyPlayerItemReceived(Char, Item, ReceivedQuantity);
	return true;
}

bool FElysiumInventory::Equip(FElysiumCombatCharacter& Char, FElysiumItem& Item)
{
	// `Weapon_Equip` calls `Inventory_Add`, performs the active-weapon switch for a wieldable item,
	// and invokes the item's equip callback (whose effects are the weapon layer's, not this slice's).
	const bool bWieldable = Item.IsWieldable();
	const FString Classname = Item.ClassName();
	if (!Add(Char, Item))
	{
		return false;
	}
	if (bWieldable)
	{
		// A merge leaves the incoming entity unowned and spare, so the carried stack is what the
		// switch has to name.
		if (FElysiumItem* Carried = Item.IsOwned() ? &Item : FindOrdinary(Char, Classname))
		{
			SetActiveWeapon(Char, *Carried);
		}
	}
	return true;
}

bool FElysiumInventory::SetActiveWeapon(FElysiumCombatCharacter& Char, FElysiumItem& Item)
{
	if (!Item.IsOwned() || Item.Owner != Char.Handle)
	{
		return false;
	}
	if (Item.Handle != ActiveWeapon)
	{
		// The outgoing item's equip effects end before the incoming one's begin, so a swing or a
		// reload in flight on the holstered weapon cannot commit against the new one.
		if (FElysiumItem* Previous = Active(Char))
		{
			Previous->OnHolstered(Char);
		}
		PreviousWeapon = ActiveWeapon;
		ActiveWeapon = Item.Handle;
		Item.OnEquipped(Char);
	}
	Char.PublishEquippedCameraClass();
	return true;
}

FElysiumEntityHandle FElysiumInventory::GiveNamedItem(FElysiumCombatCharacter& Char,
	const FString& Classname)
{
	FElysiumEntityWorld* World = Char.World;
	if (!World || Classname.IsEmpty() || ElysiumItems::Find(Classname) == nullptr)
	{
		return FElysiumEntityHandle::Invalid();
	}

	// Create and spawn the named entity, then run it through the same item/weapon equip path a
	// pickup takes — `GiveNamedItem`'s own shape.
	FElysiumEntityDef Def;
	Def.Classname = Classname;
	Def.Origin = Char.Origin;
	const FElysiumEntityHandle Handle = World->SpawnRuntimeEntity(MoveTemp(Def));

	FElysiumEntity* Created = World->Resolve(Handle);
	FElysiumItem* Item = Created ? Created->AsItem() : nullptr;
	if (!Item)
	{
		return FElysiumEntityHandle::Invalid();
	}
	if (!Equip(Char, *Item))
	{
		// Refused (full, or a stack at its limit). Nothing is carried, so the entity it created is
		// not left standing in the world either.
		Item->Kill();
		return FElysiumEntityHandle::Invalid();
	}
	if (!Item->IsOwned())
	{
		// It merged into an existing stack: the carried count went up and this entity is spare. The
		// grant still landed, so the handle is still the answer.
		Item->DestroyWorldBody();
		Item->Kill();
	}
	return Handle;
}

bool FElysiumInventory::Detach(FElysiumCombatCharacter& Char, FElysiumItem& Item)
{
	const int32 Position = Slots.IndexOfByPredicate(
		[&Item](const FElysiumEntityHandle& H) { return H == Item.Handle; });
	if (Position == INDEX_NONE)
	{
		return false;
	}

	Slots.RemoveAt(Position);
	// Compact the remaining ordering and repair the following items' positions — the whole of what
	// `Inventory_Remove` does beyond clearing the handle. It never destroys the entity.
	for (int32 i = Position; i < Slots.Num(); ++i)
	{
		if (FElysiumItem* Later = ResolveItem(Char, Slots[i]))
		{
			Later->InvenPos = i;
		}
	}
	if (ActiveWeapon == Item.Handle)
	{
		ActiveWeapon = FElysiumEntityHandle::Invalid();
		Char.PublishEquippedCameraClass();
	}
	Item.Owner = FElysiumEntityHandle::Invalid();
	Item.InvenPos = FElysiumItem::Unslotted;
	return true;
}

bool FElysiumInventory::ScriptRemove(FElysiumCombatCharacter& Char, const FString& Classname)
{
	if (FElysiumItem* Item = FindOrdinary(Char, Classname))
	{
		// A stackable item with two or more decrements; a non-stackable item, or the last of a
		// stack, is detached/reindexed and the entity removed. This destructive path is deliberately
		// NOT shared with player drop, which preserves a world entity (`inventory.md` §5.3).
		if (Item->IsStackable() && Item->ItemCount >= 2)
		{
			--Item->ItemCount;
			return true;
		}
		Detach(Char, *Item);
		Item->Kill();
		return true;
	}

	// No ordinary classname matched, so try a keyring record.
	FElysiumKeyring* Ring = FindKeyring(Char);
	return Ring != nullptr && Ring->RemoveKey(Classname) != INDEX_NONE;
}

void FElysiumInventory::RebuildFrom(FElysiumCombatCharacter& Char)
{
	FElysiumEntityWorld* World = Char.World;
	if (!World)
	{
		return;
	}
	TArray<FElysiumItem*> Owned;
	for (const TUniquePtr<FElysiumEntity>& Candidate : World->Entities())
	{
		FElysiumItem* Item = Candidate ? Candidate->AsItem() : nullptr;
		if (Item && Item->Owner == Char.Handle && !Item->IsDead())
		{
			Owned.Add(Item);
		}
	}
	// The restored `m_iInvenPos` is the ordering; a tie (or an unslotted owned item, which a
	// half-written payload can produce) falls back to handle order so the result is deterministic.
	Owned.Sort([](const FElysiumItem& A, const FElysiumItem& B)
	{
		return A.InvenPos != B.InvenPos ? A.InvenPos < B.InvenPos : A.Handle.Index < B.Handle.Index;
	});

	Slots.Reset(Owned.Num());
	for (int32 i = 0; i < Owned.Num(); ++i)
	{
		Owned[i]->InvenPos = i;   // re-derive the compact position from the restored ordering
		Slots.Add(Owned[i]->Handle);
	}
	if (ActiveWeapon.IsSet() && ResolveItem(Char, ActiveWeapon) == nullptr)
	{
		ActiveWeapon = FElysiumEntityHandle::Invalid();
	}
	// **The restore publishes too.** A loaded game that skipped this would arbitrate the camera
	// against whatever class the previous run's weapon carried.
	Char.PublishEquippedCameraClass();
}

int32 FElysiumInventory::Reserve(const FString& InAmmoType) const
{
	const int32* Held = AmmoReserve.Find(ElysiumFold(InAmmoType));
	return Held ? *Held : 0;
}

void FElysiumInventory::AddReserve(const FString& InAmmoType, int32 Amount)
{
	if (InAmmoType.IsEmpty() || Amount == 0)
	{
		return;
	}
	// No clamp of its own — the wrapper has none, and any ceiling is a class/data service's.
	int32& Held = AmmoReserve.FindOrAdd(ElysiumFold(InAmmoType));
	Held += Amount;
}

bool FElysiumInventory::TransferSlot(FElysiumCombatCharacter& From, FElysiumCombatCharacter& To,
	int32 Position, FString* OutClassname, int32* OutQuantity)
{
	FElysiumItem* Item = From.Inventory.At(From, Position);
	if (!Item || Item->IsDead() || Item->Owner != From.Handle)
	{
		UE_LOG(LogElysiumItem, Warning,
			TEXT("inventory transfer refused: source=%s slot=%d has no live owned item"),
			*From.DebugString(), Position);
		return false;
	}

	const FString Classname = Item->ClassName();
	const int32 SourceQuantity = FMath::Max(1, Item->ItemCount);
	const int32 TransferQuantity = Item->IsStackable() ? 1 : SourceQuantity;
	if (Item->IsStackable())
	{
		if (FElysiumItem* Existing = To.Inventory.FindOrdinary(To, Classname))
		{
			if (!Existing->StackHasRoomFor(Existing->ItemCount + TransferQuantity))
			{
				UE_LOG(LogElysiumItem, Warning,
					TEXT("inventory transfer refused: destination stack is full for %s (%s -> %s)"),
					*Classname, *From.DebugString(), *To.DebugString());
				return false;
			}
		}
		else if (To.Inventory.IsFull())
		{
			UE_LOG(LogElysiumItem, Warning,
				TEXT("inventory transfer refused: destination inventory is full (%s -> %s, %s)"),
				*From.DebugString(), *To.DebugString(), *Classname);
			return false;
		}
	}
	else if (To.Inventory.IsFull())
	{
		UE_LOG(LogElysiumItem, Warning,
			TEXT("inventory transfer refused: destination inventory is full (%s -> %s, %s)"),
			*From.DebugString(), *To.DebugString(), *Classname);
		return false;
	}

	// Barter moves one unit at a time out of a multi-count stack. GiveNamedItem creates the real
	// destination entity or merges into its carried stack through the ordinary add/equip door; only
	// after that succeeds does the source count commit.
	if (Item->IsStackable() && SourceQuantity > 1)
	{
		if (!To.Inventory.GiveNamedItem(To, Classname).IsSet())
		{
			UE_LOG(LogElysiumItem, Warning,
				TEXT("inventory transfer failed to materialize %s in %s"),
				*Classname, *To.DebugString());
			return false;
		}
		--Item->ItemCount;
		if (OutClassname) { *OutClassname = Classname; }
		if (OutQuantity) { *OutQuantity = TransferQuantity; }
		return true;
	}

	// Admission is now guaranteed on this single-threaded substrate. Detach first so Add sees the
	// item as unowned; it either carries this entity or merges its count into the existing stack.
	if (!From.Inventory.Detach(From, *Item) || !To.Inventory.Equip(To, *Item))
	{
		// This is an invariant failure after the preflight, not an ordinary refusal. Restore ownership
		// through the same add door so the item cannot disappear from both inventories.
		if (!Item->IsOwned())
		{
			From.Inventory.Equip(From, *Item);
		}
		UE_LOG(LogElysiumItem, Error, TEXT("inventory transfer invariant failed for %s"),
			*Item->DebugString());
		return false;
	}
	if (!Item->IsOwned())
	{
		Item->DestroyWorldBody();
		Item->Kill();
	}
	if (OutClassname) { *OutClassname = Classname; }
	if (OutQuantity) { *OutQuantity = TransferQuantity; }
	return true;
}


// --- The wield rule -----------------------------------------------------------------------------
//
// `CBaseCombatCharacter::Inventory_Can_Wield` (0x10335a70) and the sweep that enforces it,
// `Inventory_Wield_Update` (0x10335b80). The recovery is `docs/vtmb/wielded_weapons.md`
// § "Who may wield what: ExcludedEquipTables and equip_mask".
//
// What is deliberately NOT gated, because retail does not gate it: `Weapon_Equip` (0x1032d380, the
// spawn equip), `Weapon_Switch` (0x1032dde0) and `GetBestMeleeWeapon` (0x10336f20). An NPC is
// spawn-equipped with a weapon it may not wield, and it is the wield update that takes it away
// again — not the grant.

namespace
{
	// The `ExcludedEquipTables` rows in force for this character, or null.
	//
	// The subsystem always wins and the bound table is the headless fallback — the same shape
	// `FElysiumCombatCharacter::SheetRules` uses, for the same reason: a Substrate-tier world has no
	// GameInstance and therefore no rulebook at all.
	const FElysiumExcludedEquipTable* WieldRulesFor(const FElysiumCombatCharacter& Char)
	{
		UElysiumGameStateSubsystem* GameState = Char.World ? Char.World->GetGameState() : nullptr;
		if (UElysiumRulebookSubsystem* Rules = GameState ? GameState->Rulebook() : nullptr)
		{
			return &Rules->ExcludedEquip();
		}
		return ElysiumSheetRules::BoundTables().ExcludedEquip;
	}

	// `CBaseCombatWeapon::CanDeploy` (0x10253a70 / 0x10253ab0) — a weapon may be drawn when it has
	// ammunition or needs none. A record with no `Magazine` block needs none, which is every melee
	// weapon and the fists.
	bool CanDeploy(const FElysiumCombatCharacter& Char, const FElysiumItem& Item)
	{
		const FElysiumItemDef* Record = Item.Data();
		if (Record == nullptr || Record->AmmoType.IsEmpty())
		{
			return true;
		}
		return Item.MagazineCount > 0 || Char.Inventory.Reserve(Record->AmmoType) > 0;
	}
}

bool FElysiumInventory::CanWield(const FElysiumCombatCharacter& Char, const FElysiumItem& Item) const
{
	const FElysiumItemDef* Record = Item.Data();
	// A carried item the catalogue has no row for authors no `equip_mask`, which is mask 0 — the
	// same answer an absent key gives, and no arm of the rule refuses it.
	const uint32 Mask = Record ? Record->EquipMask : 0;

	const FElysiumExcludedEquipTable* Table = WieldRulesFor(Char);
	if (Table == nullptr)
	{
		// No rules in reach. Only the row-independent arm can still speak, and it is the one that
		// matters: `never` refuses everything, everywhere.
		return (Mask & (uint32)ElysiumEquipFlags::Never) == 0;
	}
	// Slot 31 is read CURRENT, not base: the Protean groups
	// (`Discipline (Protean-Feral_Claws)`) write the `Clawed_Form` row onto it as a
	// `"Value Clawed_Form"` trait effect, and a base read would never see the form the character is
	// actually in.
	const int32 Row = Char.Sheet.GetCurrent(EElysiumTraitContainer::Attributes,
		ElysiumSlot::ExcludedEquipment);
	return Table->CanWield(Row, Mask);
}

FElysiumItem* FElysiumInventory::NextBestWeapon(const FElysiumCombatCharacter& Char,
	const FElysiumItem* Exclude) const
{
	FElysiumItem* Best = nullptr;
	int32 BestWeight = TNumericLimits<int32>::Lowest();
	for (const FElysiumEntityHandle& Handle : Slots)
	{
		FElysiumItem* Item = ResolveItem(Char, Handle);
		if (Item == nullptr || Item == Exclude)
		{
			continue;
		}
		const FElysiumItemDef* Record = Item->Data();
		if (Record == nullptr || !Record->IsControllableWeapon())
		{
			continue;
		}
		if (!CanDeploy(Char, *Item) || !CanWield(Char, *Item))
		{
			continue;
		}
		// The authored `weight` (accessor 0x10251ef0) is the whole ordering; ties keep the first
		// carried, which is inventory-position order and so is the compact list's own walk.
		if (Record->Weight > BestWeight)
		{
			BestWeight = Record->Weight;
			Best = Item;
		}
	}
	return Best;
}

void FElysiumInventory::Holster(FElysiumCombatCharacter& Char)
{
	FElysiumItem* Current = Active(Char);
	if (Current == nullptr)
	{
		ActiveWeapon = FElysiumEntityHandle::Invalid();
		return;
	}
	Current->OnHolstered(Char);
	ActiveWeapon = FElysiumEntityHandle::Invalid();
	// `PreviousWeapon` is untouched: `Weapon_Switch(NULL)` puts the weapon away, it does not forget
	// what was held.
	Char.PublishEquippedCameraClass();
}

void FElysiumInventory::WieldUpdate(FElysiumCombatCharacter& Char)
{
	FElysiumItem* Current = Active(Char);
	if (Current != nullptr && CanWield(Char, *Current))
	{
		return;   // what is held is still legal; retail's early-out
	}

	// Arm 1 — `m_hLastWeapon`, if it is still carried and passes the rule.
	FElysiumItem* Last = ResolveItem(Char, PreviousWeapon);
	if (Last != nullptr && Last != Current && Last->IsOwned() && Last->Owner == Char.Handle
		&& CanWield(Char, *Last))
	{
		SetActiveWeapon(Char, *Last);
		return;
	}

	// `Weapon_Switch(NULL)` — the illegal weapon goes away BEFORE a replacement is looked for, so a
	// character that finds none ends up holding nothing rather than holding what it may not wield.
	if (Current != nullptr)
	{
		Holster(Char);
	}

	// Arm 2 — the gamerules picker.
	if (FElysiumItem* Next = NextBestWeapon(Char, Current))
	{
		SetActiveWeapon(Char, *Next);
		return;
	}

	// Arm 3 — the carried `item_w_unarmed`. It is named as a CLASSNAME by retail, and it is not the
	// fists record: `item_w_unarmed` is `item_type hidden` and authors no `Activation` block, which
	// is what makes it the "holding nothing" answer rather than a weapon.
	if (FElysiumItem* Unarmed = FindOrdinary(Char, TEXT("item_w_unarmed")))
	{
		if (Unarmed != Current && CanWield(Char, *Unarmed))
		{
			SetActiveWeapon(Char, *Unarmed);
		}
	}
}
