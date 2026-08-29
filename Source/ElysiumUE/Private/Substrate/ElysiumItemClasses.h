#pragma once

// Items are entities (`docs/vtmb/inventory.md`).
//
// VtMB's carried inventory is an array of handles to real server entities, so an item has to BE
// one: `FElysiumItem` sits on the character chain at CBaseCombatWeapon's place, under
// CBaseAnimating, and carries the four recovered datamap fields — the owner, `m_iInvenPos`,
// `m_iItemCount` and the primary loaded magazine. A loose world item is one of these with no
// owner and position 255; picking it up changes its ownership rather than replacing it with a
// classname token.
//
// The class list is DATA. `ElysiumItems::Install` registers one entity class per `vdata/items`
// definition, so a classname is a live item class exactly when the catalogue holds a record for
// it — there is no code-side list of item names to keep in step, and no classname prefix decides
// anything.

#include "CoreMinimal.h"

#include "ElysiumPlayer.h"

class UPrimitiveComponent;
class UStaticMeshComponent;

struct FElysiumItemDef;
struct FElysiumItemTable;
class FElysiumEntityWorld;

// The item family's shared log category; defined in `ElysiumItemClasses.cpp`.
DECLARE_LOG_CATEGORY_EXTERN(LogElysiumItem, Log, All);

// The one classname the item family does NOT share a leaf with: collected keys are stored as
// logical records inside a single carried keyring entity rather than one entity each, so retail
// links this name to its own class and so do we.
inline FName ElysiumKeyringClassName() { return FName(TEXT("item_g_keyring")); }
// The chain node every item classname registers under — CBaseCombatWeapon's place in VtMB's own
// datamap chain. Like the other chain nodes it never appears in a `.ents` file.
inline FName ElysiumItemClassName() { return FName(TEXT("CBaseCombatWeapon")); }

class FElysiumItem : public FElysiumAnimating
{
public:
	FElysiumItem();

	// `m_iInvenPos` (+0x73c). 255 means "not assigned to a carried slot" — a loose world item, or
	// one that `Inventory_Remove` has just detached.
	static constexpr int32 Unslotted = 255;

	// The combat character carrying this item, or Invalid for a loose world item. Registered as a
	// Save-flagged handle field, so ownership restores with the entity and the character's handle
	// list is rebuilt from it.
	FElysiumEntityHandle Owner;
	int32 InvenPos = Unslotted;
	// `m_iItemCount` (+0x8d0) — the stack quantity. 1 for a non-stackable item; only an item whose
	// data says `is_stackable` ever leaves 1.
	int32 ItemCount = 1;
	// `m_iAmmoTypes[0]` (+0x744) — the primary ammunition type. Retail stores the resolved index;
	// the recoverable form is the name the item data states, the same treatment `m_hEyeLookTarget`
	// takes for a handle.
	FString AmmoType;
	// `m_iMagazineCurAmts[0]` (+0x74c) — the primary LOADED magazine. This is what `AmmoCount`
	// reports for a non-stackable item; reserve rounds live on the owning character.
	int32 MagazineCount = 0;

	// This item's `vdata/items` record, or null when the catalogue is not installed / has no row
	// for the classname. Every policy question goes through it.
	const FElysiumItemDef* Data() const;

	bool IsStackable() const;
	bool IsDroppable() const;
	bool IsPermanentInventory() const;
	bool IsWieldable() const;
	// `stack_limit`, or 0 when the file authored none (which is not a limit of zero).
	int32 StackLimit() const;
	// Whether a stack of `Count` of this item is within its authored limit.
	bool StackHasRoomFor(int32 Count) const;

	bool IsOwned() const { return Owner.IsSet(); }
	// The classname scripts, triggers and transfers identify this item by (+0x11c).
	const FString& ClassName() const;

	// The acquisition terminus: hand this loose entity to a combat character. It detaches the world
	// body, adds through the inventory's equip/add route, and fires `OnPlayerPickup` when the taker
	// is the player. Returns whether the item was accepted.
	//
	// Loose world bodies register a player-overlap anchor which reaches this same terminus through
	// OnTouchStart, reproducing CBaseCombatWeaponDefaultTouch without giving the component identity.
	bool AcquireBy(FElysiumCombatCharacter& Taker);

	virtual void Spawn() override;
	virtual void Serialize(FElysiumSaveArchive& Ar) override;
	virtual void OnRuntimeTransformChanged() override;
	virtual void OnDormancyChanged() override;
	virtual bool IsUsable() const override { return !IsOwned(); }
	virtual bool CanPlayerFocus(const FElysiumUseContext& Context) const override;
	virtual FElysiumUseBeginResult BeginPlayerUse(const FElysiumUseContext& Context) override;
	virtual bool CanBeginTouch(const FElysiumEntityHandle& Activator) const override;
	virtual void OnTouchStart(const FElysiumEntityHandle& Activator) override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;
	virtual UPrimitiveComponent* GetAttachBody() const override;

	// `Weapon_Equip`'s equip callback and its inverse. Ordinary items have no equip effects; the
	// weapon leaf resets its fire mode and its in-flight transactions here. Called by
	// `FElysiumInventory::Equip` on the item that becomes active, and on the one it replaces.
	virtual void OnEquipped(FElysiumCombatCharacter& /*Owner*/) {}
	virtual void OnHolstered(FElysiumCombatCharacter& /*Owner*/) {}

	// No-RTTI downcast, the reason `AsCombatCharacter`/`AsDoorBase` exist.
	virtual FElysiumItem* AsItem() { return this; }
	virtual FElysiumKeyring* AsKeyring() { return nullptr; }
	virtual class FElysiumWeapon* AsWeapon() { return nullptr; }

	// Stand / tear down the loose world body. Ownership is what decides: an owned item is carried,
	// so it has no presence in the world.
	void BuildWorldBody();
	void DestroyWorldBody();

protected:
	// The loose item's ground model, built from the item data's `playermodel`. Null while carried.
	UStaticMeshComponent* WorldBody = nullptr;
};

// The one carried entity that owns logical records.
//
// Collected keys are the deliberate exception to one-entity-per-item storage (`inventory.md` §3):
// the player carries one ordinary `item_g_keyring` entity, and that entity owns a dynamic array of
// key records. Lookup and removal compare the record's classname case-insensitively, so `HasItem`
// and `RemoveItem` fall through to here after the ordinary slots.
//
// Only the record's classname is recovered (the 0xc0-byte record begins with a 0x60-byte classname
// field and the rest was not decoded), and the required behaviour is the logical membership rather
// than a byte-for-byte save port — so the records ride the leaf `Serialize`.

class FElysiumKeyring final : public FElysiumItem
{
public:
	TArray<FString> Keys;

	bool HasKey(const FString& Classname) const;
	bool AddKey(const FString& Classname);
	// Removes the first case-insensitive match, closing the gap after it. Returns the removed
	// index, or INDEX_NONE — retail sends that index to the owning player's client.
	int32 RemoveKey(const FString& Classname);

	virtual void Serialize(FElysiumSaveArchive& Ar) override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;
	virtual FElysiumKeyring* AsKeyring() override { return this; }
};

// The loot container leaf lives in its own file on the same chain; the item family's includers
// reach it through here.
#include "Substrate/ElysiumItemContainer.h"

// --- The catalogue -> class-registry install ---

struct FElysiumEquipmentView;

namespace ElysiumItems
{
	// Make `Table` the item data in force and register one entity class per definition. Idempotent
	// per classname: a name a static registrar already owns is left alone. `Table` must outlive the
	// install — the rulebook subsystem owns it for the session and uninstalls on teardown.
	// Returns the number of classes newly registered.
	int32 Install(const FElysiumItemTable& Table);

	// Drop `Table` if it is the installed one. Registered classes stay (the registry has no
	// unregister and a stale class simply finds no definition), which is why every policy read
	// handles a null record.
	void Uninstall(const FElysiumItemTable& Table);

	// The installed catalogue, or null. Policy comes from here and nowhere else.
	const FElysiumItemTable* Table();
	const FElysiumItemDef* Find(const FString& Classname);

	// Diagnostic command adapter over the same narrow intents CommonUI uses. The player must have
	// opened one container through +use; only `Take <slot>` and `Give <slot>` mutate state. Buy/Sell
	// remain the economy slice and fail closed.
	bool ExecuteBarter(FElysiumEntityWorld& World, const FString& Args);

	// --- Inventory selection ---
	//
	// Whether a section's selection is held in the hand. The three weapon families are the types
	// `items.txt` marks `IsWielded` inside a browsable section, so a wielded section commits through
	// `SetActiveWeapon` and every other section moves `SelectedItem` instead.
	bool SectionIsWielded(EElysiumInvSection Section);

	// The carried items of one section, in authored selection order — `bucket`, then
	// `bucket_position`, then classname so records sharing a position stay stable. A record the
	// catalogue has no row for, and one whose `is_visible_in_hud` is off, are both left out: the
	// selector shows what the author put in front of the player. An undisplayed section is empty.
	void CollectSection(const FElysiumCombatCharacter& Char, EElysiumInvSection Section,
		TArray<FElysiumItem*>& OutItems);

	// `invnext`/`invprev` — step `Delta` places inside the CURRENT section and commit the result.
	// The list wraps. With no section chosen yet the cursor starts in the one the hand is already
	// in. Returns whether anything was committed.
	bool CycleSelection(FElysiumEntityWorld& World, int32 Delta);

	// `slotN` — move the cursor to a section and select inside it. Repeating the verb while already
	// in that section advances within it, which is what the retail category keys do.
	bool SelectSection(FElysiumEntityWorld& World, EElysiumInvSection Section);

	// `lastinv` — return to the weapon held before the current one, and to its section with it. No
	// remembered weapon, or one no longer carried, leaves the hand alone.
	bool SelectLastWeapon(FElysiumEntityWorld& World);

	// Project the player's inventory for presentation — the weapon in hand, what is worn, and the
	// browsed section's rows. Fills `bValid` false when there is no player or no catalogue;
	// `PeekAlpha` is the publisher's and is not written here.
	void BuildInventoryView(const FElysiumEntityWorld& World, FElysiumEquipmentView& Out);
}
