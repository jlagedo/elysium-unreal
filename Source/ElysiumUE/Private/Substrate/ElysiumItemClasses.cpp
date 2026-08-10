// 9.8 (a) — the item entity, the keyring, and the combat character's inventory container.
//
// The contract is `docs/vtmb/inventory.md` §§2-6 and the design is
// `docs/architecture/gameplay-systems-architecture.md` §5.2. Three things this file will not do,
// because they are distinct operations with distinct entity-lifetime effects and belong to slice
// (b): player drop (which PRESERVES a world entity), container transfer, and barter. They are
// absent rather than approximated — `ScriptRemove` must never become the shared "delete item"
// shortcut a drop path reuses.

#include "Substrate/ElysiumItemClasses.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumRulebook.h"

#include "Components/StaticMeshComponent.h"
#include "Misc/Paths.h"

#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogElysiumItem, Log, All);

// ============================================================================================
// The installed catalogue
// ============================================================================================

namespace
{
	// The item data in force, owned by whoever installed it (the rulebook subsystem for a session,
	// a test for the length of one case). Game-thread only, like the rest of the substrate.
	const FElysiumItemTable* GItemTable = nullptr;
}

namespace ElysiumItems
{
	const FElysiumItemTable* Table() { return GItemTable; }

	const FElysiumItemDef* Find(const FString& Classname)
	{
		return (GItemTable && !Classname.IsEmpty()) ? GItemTable->Find(Classname) : nullptr;
	}
}

// ============================================================================================
// FElysiumItem
// ============================================================================================

const FString& FElysiumItem::ClassName() const
{
	static const FString Empty;
	return Def ? Def->Classname : Empty;
}

const FElysiumItemDef* FElysiumItem::Data() const
{
	return ElysiumItems::Find(ClassName());
}

bool FElysiumItem::IsStackable() const
{
	const FElysiumItemDef* Record = Data();
	return Record && Record->bStackable;
}

bool FElysiumItem::IsDroppable() const
{
	const FElysiumItemDef* Record = Data();
	// No record is not "droppable by default": an item the catalogue does not know has no policy,
	// and the fail-closed answer for a predicate that gates giving something away is no.
	return Record && Record->bDroppable;
}

bool FElysiumItem::IsPermanentInventory() const
{
	const FElysiumItemDef* Record = Data();
	return Record && Record->bPermanentInventory;
}

bool FElysiumItem::IsWieldable() const
{
	const FElysiumItemDef* Record = Data();
	return Record && Record->bWieldable;
}

int32 FElysiumItem::StackLimit() const
{
	const FElysiumItemDef* Record = Data();
	return Record ? Record->StackLimit : 0;
}

bool FElysiumItem::StackHasRoomFor(int32 Count) const
{
	const int32 Limit = StackLimit();
	return Limit <= 0 || Count <= Limit;
}

void FElysiumItem::Spawn()
{
	// The ammunition identity and the magazine a fresh item spawns loaded with are the item data's
	// (`Magazine.Type` / `Default_Size`). A restore overwrites both from the saved fields, which run
	// after Spawn.
	if (const FElysiumItemDef* Record = Data())
	{
		AmmoType = Record->AmmoType;
		if (MagazineCount == 0)
		{
			MagazineCount = Record->DefaultAmmo;
		}
		if (Model.IsEmpty())
		{
			// An item entity authors no `model` key — its world model is the item data's, which is
			// what `CBaseCombatWeapon::Spawn` sets the ground model from.
			Model = Record->PlayerModel;
		}
	}
	else if (!IsRecordOnly())
	{
		UE_LOG(LogElysiumItem, Verbose, TEXT("%s has no vdata/items record — no policy applies"),
			*DebugString());
	}

	if (!IsOwned())
	{
		BuildWorldBody();
	}
}

void FElysiumItem::BuildWorldBody()
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (WorldBody != nullptr || !Embodiment || !Def)
	{
		return;
	}
	// The exporter's own decoded stem when the map's prop pass covered this model, otherwise the
	// item data's `playermodel` basename — the same fallback a runtime `SetModel` takes. Item ground
	// models are not in the prop export corpus today, so this normally resolves to nothing and the
	// loose item stands bodiless; the seam is here so the body appears the moment they are.
	const FString Stem = Def->ModelMesh.IsEmpty()
		? FPaths::GetBaseFilename(Model).ToLower()
		: Def->ModelMesh;
	if (Stem.IsEmpty())
	{
		return;
	}
	const FQuat Rot = Def->ModelMesh.IsEmpty()
		? FQuat(FRotator(0.0f, -Angles.Y, 0.0f))
		: Def->ModelQuat;
	WorldBody = Embodiment->BuildPropVisual(Stem, Origin, Rot, Embodiment->BodyScaleFor(*Def));
	if (WorldBody)
	{
		World->RegisterPropBody(WorldBody);
		if (IsInert())
		{
			WorldBody->SetVisibility(false);
		}
	}
}

void FElysiumItem::DestroyWorldBody()
{
	if (WorldBody)
	{
		WorldBody->DestroyComponent();
		WorldBody = nullptr;
	}
}

bool FElysiumItem::AcquireBy(FElysiumCombatCharacter& Taker)
{
	if (IsOwned())
	{
		return false;   // already carried; a transfer is slice (b)'s container/barter path
	}
	const bool bAccepted = Taker.Inventory.Equip(Taker, *this);
	if (!bAccepted)
	{
		return false;
	}
	if (!IsOwned())
	{
		// Accepted but not carried means it merged into a stack already held: the count went up and
		// this entity has been absorbed, so it must not stay lying in the world.
		DestroyWorldBody();
		Kill();
	}
	// The pickup output the maps wire (`OnPlayerPickup` carries the tutorial's diary, purse and
	// cash-box beats). Only a player pickup fires it, which is what the name says.
	if (World && World->PlayerHandle().IsSet() && Taker.Handle.Index == World->PlayerHandle().Index)
	{
		static const FName OnPlayerPickup(TEXT("OnPlayerPickup"));
		FireOutput(OnPlayerPickup, Taker.Handle);
	}
	return true;
}

void FElysiumItem::OnRuntimeTransformChanged()
{
	FElysiumEntity::OnRuntimeTransformChanged();
	if (WorldBody)
	{
		WorldBody->SetWorldLocationAndRotation(Origin, FQuat(FRotator(0.0f, -Angles.Y, 0.0f)));
	}
}

void FElysiumItem::OnDormancyChanged()
{
	FElysiumEntity::OnDormancyChanged();
	if (WorldBody)
	{
		WorldBody->SetVisibility(!IsInert());
	}
}

UPrimitiveComponent* FElysiumItem::GetAttachBody() const
{
	return WorldBody;   // null while carried — a carried item is nothing to parent to
}

void FElysiumItem::Serialize(FElysiumSaveArchive& Ar)
{
	// Owner, position, stack count, ammo type and magazine are registered Save fields, so the field
	// walk already carries them. What the walk cannot carry is the body, which is presentation and
	// rebuilds — an item that restored owned must not still be standing in the world.
	if (Ar.IsLoading())
	{
		if (IsOwned())
		{
			DestroyWorldBody();
		}
		else
		{
			BuildWorldBody();
		}
	}
}

void FElysiumItem::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	const FElysiumItemDef* Record = Data();
	Out.Emplace(TEXT("Item data"), Record
		? FString::Printf(TEXT("%s%s  worth %d"), ElysiumItemTypeName(Record->Type),
			Record->bHidden ? TEXT(" (hidden)") : TEXT(""), Record->Worth)
		: FString(TEXT("(no vdata/items record)")));
	Out.Emplace(TEXT("Owner"), IsOwned()
		? (World ? World->DescribeHandle(Owner) : Owner.ToString())
		: FString(TEXT("(loose)")));
	Out.Emplace(TEXT("Inven pos"), InvenPos == Unslotted
		? FString(TEXT("255 (unslotted)")) : FString::FromInt(InvenPos));
	Out.Emplace(TEXT("Stack"), FString::Printf(TEXT("%d%s"), ItemCount,
		IsStackable() ? TEXT(" (stackable)") : TEXT("")));
	if (!AmmoType.IsEmpty())
	{
		Out.Emplace(TEXT("Magazine"), FString::Printf(TEXT("%d %s"), MagazineCount, *AmmoType));
	}
	Out.Emplace(TEXT("World body"), WorldBody ? TEXT("standing") : TEXT("(none)"));
}

// ============================================================================================
// FElysiumKeyring
// ============================================================================================

bool FElysiumKeyring::HasKey(const FString& Classname) const
{
	for (const FString& Key : Keys)
	{
		if (Key.Equals(Classname, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}
	return false;
}

bool FElysiumKeyring::AddKey(const FString& Classname)
{
	if (Classname.IsEmpty() || HasKey(Classname))
	{
		return false;
	}
	Keys.Add(Classname);
	return true;
}

int32 FElysiumKeyring::RemoveKey(const FString& Classname)
{
	for (int32 i = 0; i < Keys.Num(); ++i)
	{
		if (Keys[i].Equals(Classname, ESearchCase::IgnoreCase))
		{
			// Retail memmoves the later records over the match and decrements the count, then sends
			// the owning player's client the removed index.
			Keys.RemoveAt(i);
			return i;
		}
	}
	return INDEX_NONE;
}

void FElysiumKeyring::Serialize(FElysiumSaveArchive& Ar)
{
	FElysiumItem::Serialize(Ar);
	// A collected key is no longer a standalone entity, so its membership has no field to ride and
	// has to be leaf state. The byte-level retail codec for the record array was not decoded; the
	// required behaviour is the logical membership, which is what this carries.
	Ar << Keys;
}

void FElysiumKeyring::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumItem::GetDebugState(Out);
	Out.Emplace(TEXT("Keys"), Keys.IsEmpty()
		? FString(TEXT("(none)")) : FString::Join(Keys, TEXT(", ")));
}

// ============================================================================================
// FElysiumInventory
// ============================================================================================

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

bool FElysiumInventory::Add(FElysiumCombatCharacter& Char, FElysiumItem& Item)
{
	if (Item.IsOwned())
	{
		return false;
	}

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
			ActiveWeapon = Carried->Handle;
		}
	}
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

// ============================================================================================
// Registration
// ============================================================================================

namespace
{
	TUniquePtr<FElysiumEntity> MakeItem() { return MakeUnique<FElysiumItem>(); }
	TUniquePtr<FElysiumEntity> MakeKeyring() { return MakeUnique<FElysiumKeyring>(); }

	// Register a field backed by an FElysiumItem member (FElysiumClassDesc::Field only reaches
	// FElysiumEntity members). Mirrors AddCharField / AddPropField — file-unique name so all of
	// them can land in one unity blob.
	template <typename TMember>
	void AddItemField(FElysiumClassDesc& D, const TCHAR* Name, TMember FElysiumItem::* Member,
		EElysiumField Flags = ElysiumFieldDefault)
	{
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(Flags);
		if constexpr (std::is_same_v<TMember, int32>)
		{
			Acc.Type = EElysiumVariantType::Int;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Int(static_cast<const FElysiumItem&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<FElysiumItem&>(E).*Member = V.ToInt(); };
		}
		else if constexpr (std::is_same_v<TMember, FString>)
		{
			Acc.Type = EElysiumVariantType::String;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::String(static_cast<const FElysiumItem&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<FElysiumItem&>(E).*Member = V.ToString(); };
		}
		else if constexpr (std::is_same_v<TMember, FElysiumEntityHandle>)
		{
			// The registry's typed helper has no handle case, and a handle is exactly what an item's
			// owner is. The archive already drops the epoch by design and ApplySnapshot re-stamps it,
			// so a Save-flagged handle field restores as a live reference or as Invalid.
			Acc.Type = EElysiumVariantType::Handle;
			Acc.Get = [Member](const FElysiumEntity& E) { return FElysiumVariant::Handle(static_cast<const FElysiumItem&>(E).*Member); };
			Acc.Set = [Member](FElysiumEntity& E, const FElysiumVariant& V) { static_cast<FElysiumItem&>(E).*Member = V.ToHandle(); };
		}
		else
		{
			static_assert(sizeof(TMember) == 0, "AddItemField: unsupported member type");
		}
		D.Fields.Add(FName(Name), MoveTemp(Acc));
	}

	// CBaseCombatWeapon — the chain node every item classname registers under. It carries the four
	// recovered datamap fields and nothing else: an item's INPUTS are the base chain's, and its
	// policy is the catalogue's.
	struct FElysiumItemChainRegistrar
	{
		FElysiumItemChainRegistrar()
		{
			FElysiumClassDesc& D = FElysiumClassRegistry::Get().Register(
				ElysiumItemClassName(), ElysiumAnimatingClassName(), &MakeItem);

			AddItemField(D, TEXT("m_hOwner"), &FElysiumItem::Owner);
			AddItemField(D, TEXT("m_iInvenPos"), &FElysiumItem::InvenPos);
			AddItemField(D, TEXT("m_iItemCount"), &FElysiumItem::ItemCount);
			AddItemField(D, TEXT("m_iAmmoTypes"), &FElysiumItem::AmmoType);
			AddItemField(D, TEXT("m_iMagazineCurAmts"), &FElysiumItem::MagazineCount);
		}
	};

	const FElysiumItemChainRegistrar GItemChainRegistrar;
}

namespace ElysiumItems
{
	int32 Install(const FElysiumItemTable& InTable)
	{
		GItemTable = &InTable;

		FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
		const FName Keyring = ElysiumKeyringClassName();
		int32 Registered = 0;
		for (const FElysiumItemDef& Item : InTable.Items)
		{
			const FName ClassName(*Item.Classname);
			if (const FElysiumClassDesc* Existing = Reg.Find(ClassName))
			{
				// A name a static registrar already owns keeps it, stub row or not: re-registering
				// would replace a descriptor live entities point at. No shipped stub row names an
				// item classname, so this only reports if one ever does.
				if (Existing->bStub)
				{
					UE_LOG(LogElysiumItem, Warning,
						TEXT("'%s' has a vdata/items definition but a stub class row owns the name"),
						*Item.Classname);
				}
				continue;
			}
			// No own inputs or fields: everything an item answers to is on the chain node above.
			Reg.Register(ClassName, ElysiumItemClassName(),
				ClassName == Keyring ? &MakeKeyring : &MakeItem);
			++Registered;
		}
		UE_LOG(LogElysiumItem, Log, TEXT("item catalogue installed: %d definitions, %d classes registered"),
			InTable.Num(), Registered);
		return Registered;
	}

	void Uninstall(const FElysiumItemTable& InTable)
	{
		if (GItemTable == &InTable)
		{
			GItemTable = nullptr;
		}
	}
}
