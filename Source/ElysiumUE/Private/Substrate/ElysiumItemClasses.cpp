// 9.8 — item entities, the keyring, combat-character inventories, and loot containers.
//
// The contract is `docs/vtmb/inventory.md` §§2-6 and the design is
// `docs/architecture/gameplay-systems-architecture.md` §5.2. Player drop (which PRESERVES a world
// entity) and priced Buy/Sell remain separate operations: `ScriptRemove` must never become the
// shared "delete item" shortcut a drop or transfer path reuses.

#include "Substrate/ElysiumItemClasses.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumWorldServices.h"
#include "ElysiumViewState.h"
#include "Substrate/ElysiumClassFields.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumSkillClasses.h"
#include "Substrate/ElysiumWeaponClasses.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PrimitiveComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumItem, Log, All);

// ============================================================================================
// The installed catalogue
// ============================================================================================

namespace
{
	// The item data in force, owned by whoever installed it (the rulebook subsystem for a session,
	// a test for the length of one case). Game-thread only, like the rest of the substrate.
	const FElysiumItemTable* GItemTable = nullptr;
	// The recovered context-icon table's open hand. Loose items add this modern +use surface while
	// retaining retail DefaultTouch; both ingress paths terminate at AcquireBy.
	constexpr int32 GLooseItemUseIcon = 9;
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

FElysiumItem::FElysiumItem()
{
	UseIcon = GLooseItemUseIcon;
}

const FString& FElysiumItem::ClassName() const
{
	static const FString Empty;
	return Def ? Def->Classname : Empty;
}

const FElysiumItemDef* FElysiumItem::Data() const
{
	return ElysiumItems::Find(ClassName());
}

void FElysiumCombatCharacter::PublishEquippedCameraClass() const
{
	// **Player only.** An NPC drawing a katana does not force the player's camera to third person;
	// the arbitration is a property of the local player's equipped item and of nothing else.
	if (!World || !(World->PlayerHandle() == Handle))
	{
		return;
	}
	IElysiumEmbodiment* Embodiment = World->Embodiment();
	if (!Embodiment)
	{
		return;   // a headless logic world has no camera to arbitrate
	}

	// An empty hand, an item whose record the rulebook has not loaded, and an authored `noswitch` all
	// resolve to 0, which is retail's own early-out in `ApplyWeaponCameraPref`.
	int32 CameraClass = ElysiumCam::CameraClass::None;
	if (FElysiumEntity* Ent = const_cast<FElysiumEntityWorld*>(World)->Resolve(Inventory.ActiveWeapon))
	{
		// `m_hActiveWeapon` is a script-writable handle, so what it names is not guaranteed to be an
		// item. Checked, not assumed: a blind downcast would read a `camera_class` off whatever object
		// a level script assigned and arbitrate the player's camera from it.
		const FElysiumItem* Item = Ent->AsItem();
		if (!Item)
		{
			UE_LOG(LogElysiumItem, Warning,
				TEXT("m_hActiveWeapon on %s names %s, which is not an item; the equipped camera class "
				     "resolves to none"),
				*World->DescribeHandle(Handle), *World->DescribeHandle(Inventory.ActiveWeapon));
		}
		else if (const FElysiumItemDef* Record = Item->Data())
		{
			CameraClass = Record->CameraClass;
		}
	}
	Embodiment->SetEquippedCameraClass(CameraClass);
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
	// The exporter's own decoded stem when the map's prop pass covered this model (an
	// `item_container` states a `model` key like any prop), otherwise the stem the item data's
	// `playermodel` path folds to — which is what the shared item corpus bakes its meshes under.
	const FString Stem = Def->ModelMesh.IsEmpty()
		? FElysiumContentPaths::PropModelStem(Model)
		: Def->ModelMesh;
	if (Stem.IsEmpty())
	{
		return;
	}
	if (Def->ModelMesh.IsEmpty()
		&& Embodiment->ItemGroundModelState(Model) != EElysiumItemGroundModelState::Geometry)
	{
		// Geometryless is an authored no-body result. Unavailable has already produced one owned,
		// catalogue-level warning, so neither state should fall through to repeated package loads.
		return;
	}
	const FQuat Rot = Def->ModelMesh.IsEmpty()
		? FQuat(FRotator(0.0f, -Angles.Y, 0.0f))
		: Def->ModelQuat;
	WorldBody = Embodiment->BuildPropVisual(Stem, Origin, Rot, Embodiment->BodyScaleFor(*Def));
	if (WorldBody)
	{
		World->RegisterPropBody(WorldBody, Handle);
		World->RegisterTouchAnchor(WorldBody, Handle);
		if (IsInert())
		{
			WorldBody->SetVisibility(false);
		}
	}
}

void FElysiumItem::DestroyWorldBody()
{
	if (World)
	{
		World->EndBrushTouches(Handle);
		World->SetUseAnchorEnabled(Handle, false);
		World->SetTouchAnchorEnabled(Handle, false);
	}
	if (WorldBody)
	{
		WorldBody->DestroyComponent();
		WorldBody = nullptr;
	}
}

bool FElysiumItem::CanPlayerFocus(const FElysiumUseContext& Context) const
{
	return FElysiumAnimating::CanPlayerFocus(Context) && World
		&& Context.Activator == World->PlayerHandle();
}

FElysiumUseBeginResult FElysiumItem::BeginPlayerUse(const FElysiumUseContext& Context)
{
	FElysiumEntity* TakerEntity = World ? World->Resolve(Context.Activator) : nullptr;
	FElysiumCombatCharacter* Taker = TakerEntity ? TakerEntity->AsCombatCharacter() : nullptr;
	if (!Taker)
	{
		UE_LOG(LogElysiumItem, Warning, TEXT("%s +use pickup has no combat-character activator %s"),
			*DebugString(), *Context.Activator.ToString());
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Unavailable);
	}
	return AcquireBy(*Taker)
		? FElysiumUseBeginResult::Completed()
		: FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Unavailable);
}

bool FElysiumItem::CanBeginTouch(const FElysiumEntityHandle& Activator) const
{
	return !IsInert() && !IsOwned() && World && Activator == World->PlayerHandle();
}

void FElysiumItem::OnTouchStart(const FElysiumEntityHandle& Activator)
{
	FElysiumEntity* TakerEntity = World ? World->Resolve(Activator) : nullptr;
	FElysiumCombatCharacter* Taker = TakerEntity ? TakerEntity->AsCombatCharacter() : nullptr;
	if (Taker)
	{
		AcquireBy(*Taker);
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
	if (World)
	{
		World->SetUseAnchorEnabled(Handle, !IsInert() && !IsOwned());
		World->SetTouchAnchorEnabled(Handle, !IsInert() && !IsOwned());
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

// ============================================================================================
// FElysiumItemContainer
// ============================================================================================

void FElysiumItemContainer::Spawn()
{
	BuildWorldBody();
}

void FElysiumItemContainer::Activate()
{
	// Runtime creation during the world's range-based Spawn/Activate passes would invalidate the
	// entity array. Retail also services thinks before the event queue, so a due one-shot think
	// materializes the seeds before logic_auto's OnMapLoad Python can call DeleteItems.
	if (!bSeedsMaterialized && World)
	{
		NextThink = static_cast<float>(World->NowSeconds());
	}
}

void FElysiumItemContainer::Think()
{
	if (bSeedsMaterialized)
	{
		return;
	}
	bSeedsMaterialized = true;
	for (const FString& Seed : EquipSeeds)
	{
		if (!Seed.IsEmpty())
		{
			SpawnNamedItem(Seed);
		}
	}
}

void FElysiumItemContainer::Serialize(FElysiumSaveArchive& Ar)
{
	Ar << bSeedsMaterialized;
}

void FElysiumItemContainer::Use(const FElysiumEntityHandle& Activator)
{
	if (!World || !Activator.IsSet() || World->PlayerHandle().Index != Activator.Index)
	{
		UE_LOG(LogElysiumItem, Warning,
			TEXT("loot use refused: %s received invalid/non-player activator %s"),
			*DebugString(), *Activator.ToString());
		return;
	}
	if (CurrentUser == Activator)
	{
		if (!World->EndPlayerUseSession(Handle, EElysiumUseEndReason::Completed))
		{
			FElysiumUseContext Context;
			Context.Activator = Activator;
			Context.Owner = Handle;
			EndPlayerUse(Context, EElysiumUseEndReason::Completed);
		}
		return;
	}
	World->BeginPlayerUseSession(Handle, Activator);
}

FElysiumUseBeginResult FElysiumItemContainer::BeginPlayerUse(const FElysiumUseContext& Context)
{
	if (!World || Context.Activator != World->PlayerHandle())
	{
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Unavailable);
	}
	if (IsLockedByAttachment())
	{
		UE_LOG(LogElysiumItem, Display, TEXT("loot locked: %s"), *DebugString());
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Locked);
	}
	if (CurrentUser.IsSet() && CurrentUser != Context.Activator
		&& World->Resolve(CurrentUser) != nullptr)
	{
		UE_LOG(LogElysiumItem, Display, TEXT("loot busy: %s is already in use"), *DebugString());
		return FElysiumUseBeginResult::Refused(EElysiumUseOutcome::Busy);
	}
	CurrentUser = Context.Activator;
	++LootRevision;
	static const FName OnUseBegin(TEXT("OnUseBegin"));
	FireOutput(OnUseBegin, Context.Activator);
	PlayUseAnimation(/*bOpening*/ true);
	UE_LOG(LogElysiumItem, Display,
		TEXT("loot opened: %s (%d item%s)"),
		*DebugString(), Inventory.Num(), Inventory.Num() == 1 ? TEXT("") : TEXT("s"));
	return FElysiumUseBeginResult::Started(EElysiumUseSessionKind::Explicit);
}

void FElysiumItemContainer::EndPlayerUse(const FElysiumUseContext& Context,
	EElysiumUseEndReason Reason)
{
	if (CurrentUser != Context.Activator)
	{
		UE_LOG(LogElysiumItem, Warning,
			TEXT("loot close ignored: %s is owned by %s, not %s"),
			*DebugString(), *CurrentUser.ToString(), *Context.Activator.ToString());
		return;
	}
	CurrentUser = FElysiumEntityHandle::Invalid();
	PlayUseAnimation(/*bOpening*/ false);
	static const FName OnUseEnd(TEXT("OnUseEnd"));
	FireOutput(OnUseEnd, Context.Activator);
	UE_LOG(LogElysiumItem, Display, TEXT("loot closed: %s reason=%d"),
		*DebugString(), static_cast<int32>(Reason));
}

bool FElysiumItemContainer::CanPlayerFocus(const FElysiumUseContext& Context) const
{
	return FElysiumCombatCharacter::CanPlayerFocus(Context) && !IsLockedByAttachment();
}

const TCHAR* FElysiumItemContainer::SaveBlockReason() const
{
	return CurrentUser.IsSet() ? TEXT("a loot container is open") : nullptr;
}

bool FElysiumItemContainer::RegisterLock(FElysiumLockableEntity& Lock)
{
	if (AttachedLock.IsSet() && AttachedLock != Lock.Handle
		&& World && World->Resolve(AttachedLock) != nullptr)
	{
		UE_LOG(LogElysiumItem, Warning, TEXT("%s already has an attached lock"), *DebugString());
		return false;
	}
	AttachedLock = Lock.Handle;
	NotifyLockState(Lock.Handle, Lock.IsUseLocked());
	return true;
}

void FElysiumItemContainer::NotifyLockState(const FElysiumEntityHandle& Lock, bool bLocked)
{
	if (Lock != AttachedLock)
	{
		UE_LOG(LogElysiumItem, Warning,
			TEXT("%s ignored lock-state update from %s; attached lock is %s"),
			*DebugString(), *Lock.ToString(), *AttachedLock.ToString());
		return;
	}
	if (World)
	{
		World->SetUseAnchorEnabled(Handle, !bLocked && !IsInert());
	}
}

bool FElysiumItemContainer::IsLockedByAttachment() const
{
	if (!AttachedLock.IsSet() || !World)
	{
		return false;
	}
	const FElysiumEntity* LockEntity = World->Resolve(AttachedLock);
	const FElysiumLockableEntity* Lock = LockEntity ? LockEntity->AsLockableEntity() : nullptr;
	return Lock && !Lock->IsDead() && Lock->IsUseLocked();
}

void FElysiumItemContainer::PlayUseAnimation(bool bOpening)
{
	const FString Clip = bOpening ? TEXT("open") : TEXT("close");
	if (!Def || !Def->Classname.Equals(TEXT("item_container_animated"), ESearchCase::IgnoreCase))
	{
		return;
	}
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	// A pure/headless substrate has no placed-model catalogue and intentionally exercises logic
	// without presentation. Once the live catalogue exists, a missing body/stem/clip is content
	// failure and must stay visible in the log.
	if (!Embodiment || !Embodiment->HasPlacedModelCatalogue())
	{
		return;
	}
	USkeletalMeshComponent* Animated = Cast<USkeletalMeshComponent>(WorldBody);
	if (!Animated || AnimatedStem.IsEmpty())
	{
		UE_LOG(LogElysiumItem, Warning,
			TEXT("%s cannot play container '%s': animated body/stem did not resolve"),
			*DebugString(), *Clip);
		return;
	}
	bool bLoops = false;
	if (!Embodiment->FindAnimatedPropClip(AnimatedStem, Clip, bLoops))
	{
		UE_LOG(LogElysiumItem, Warning, TEXT("%s has no container sequence '%s' on %s"),
			*DebugString(), *Clip, *AnimatedStem);
		return;
	}
	if (!Embodiment->PlayAnimatedPropClip(
		Animated, AnimatedStem, Clip, /*bLoop*/ false, nullptr))
	{
		UE_LOG(LogElysiumItem, Warning, TEXT("%s failed to play container sequence '%s' on %s"),
			*DebugString(), *Clip, *AnimatedStem);
	}
}

bool FElysiumItemContainer::SpawnNamedItem(const FString& Classname)
{
	if (Classname.IsEmpty())
	{
		return false;
	}
	const bool bSpawned = Inventory.GiveNamedItem(*this, Classname).IsSet();
	if (!bSpawned)
	{
		UE_LOG(LogElysiumItem, Warning, TEXT("%s could not spawn item '%s'"),
			*DebugString(), *Classname);
	}
	return bSpawned;
}

void FElysiumItemContainer::InputSpawnItemInContainer(const FElysiumInputArgs& Args)
{
	SpawnNamedItem(Args.Param.ToString());
}

void FElysiumItemContainer::InputAddEntityToContainer(const FElysiumInputArgs& Args)
{
	if (!World)
	{
		return;
	}
	TArray<FElysiumEntityHandle> Matches;
	World->ForEachNamed(Args.Param.ToString(), [&Matches](FElysiumEntity& Candidate)
	{
		if (FElysiumItem* Item = Candidate.AsItem(); Item && !Item->IsOwned() && !Item->IsDead())
		{
			Matches.Add(Item->Handle);
		}
	});
	for (const FElysiumEntityHandle& MatchHandle : Matches)
	{
		FElysiumEntity* Candidate = World->Resolve(MatchHandle);
		FElysiumItem* Item = Candidate ? Candidate->AsItem() : nullptr;
		if (Item && !Inventory.Equip(*this, *Item))
		{
			UE_LOG(LogElysiumItem, Warning, TEXT("%s refused %s"),
				*DebugString(), *Item->DebugString());
		}
	}
}

void FElysiumItemContainer::DeleteAllItems()
{
	while (Inventory.Num() > 0)
	{
		FElysiumItem* Item = Inventory.At(*this, Inventory.Num() - 1);
		if (!Item)
		{
			Inventory.Slots.Pop();
			continue;
		}
		Inventory.Detach(*this, *Item);
		Item->Kill();
	}
	Inventory.ActiveWeapon = FElysiumEntityHandle::Invalid();
	PublishEquippedCameraClass();
}

void FElysiumItemContainer::InputDeleteItems(const FElysiumInputArgs&)
{
	DeleteAllItems();
}

bool FElysiumItemContainer::TakeToPlayer(FElysiumPlayer& Player, int32 Slot)
{
	FString Classname;
	int32 Quantity = 0;
	if (!Inventory.TransferSlot(*this, Player, Slot, &Classname, &Quantity))
	{
		return false;
	}
	++LootRevision;
	static const FName OnItemRemove(TEXT("OnItemRemove"));
	FireOutput(OnItemRemove, Player.Handle);
	UE_LOG(LogElysiumItem, Display, TEXT("loot take: %s x%d from %s"),
		*Classname, Quantity, *DebugString());
	return true;
}

bool FElysiumItemContainer::GiveFromPlayer(FElysiumPlayer& Player, int32 Slot)
{
	FString Classname;
	int32 Quantity = 0;
	if (!Player.Inventory.TransferSlot(Player, *this, Slot, &Classname, &Quantity))
	{
		return false;
	}
	++LootRevision;
	static const FName OnItemInsert(TEXT("OnItemInsert"));
	FireOutput(OnItemInsert, Player.Handle);
	UE_LOG(LogElysiumItem, Display, TEXT("loot give: %s x%d to %s"),
		*Classname, Quantity, *DebugString());
	return true;
}

void FElysiumItemContainer::BuildLootView(FElysiumLootView& Out,
	const FElysiumPlayer& Player) const
{
	Out = FElysiumLootView();
	Out.Owner = Handle;
	Out.Revision = LootRevision;
	Out.Title = TargetName.IsEmpty() ? TEXT("Container") : TargetName;

	auto Append = [](const FElysiumCombatCharacter& Owner, const FElysiumInventory& SourceInventory,
		TArray<FElysiumLootEntryView>& Entries)
	{
		for (int32 Slot = 0; Slot < SourceInventory.Num(); ++Slot)
		{
			const FElysiumItem* Item = SourceInventory.At(Owner, Slot);
			if (!Item || Item->IsDead())
			{
				UE_LOG(LogElysiumItem, Warning,
					TEXT("loot projection skipped invalid item: owner=%s slot=%d"),
					*Owner.DebugString(), Slot);
				continue;
			}
			FElysiumLootEntryView& Entry = Entries.AddDefaulted_GetRef();
			Entry.Slot = Slot;
			Entry.Classname = Item->ClassName();
			Entry.Quantity = FMath::Max(1, Item->ItemCount);
			const FElysiumItemDef* Data = Item->Data();
			Entry.Label = Data && !Data->PrintName.IsEmpty() ? Data->PrintName : Entry.Classname;
		}
	};

	Append(*this, Inventory, Out.ContainerItems);
	Append(Player, Player.Inventory, Out.PlayerItems);
}

void FElysiumItemContainer::BuildWorldBody()
{
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (WorldBody || !Embodiment || !Def || Model.IsEmpty())
	{
		return;
	}
	VisualStem = Def->ModelMesh.IsEmpty()
		? FElysiumContentPaths::PropModelStem(Model) : Def->ModelMesh;
	if (VisualStem.IsEmpty())
	{
		return;
	}
	const FQuat StaticRotation = Def->ModelMesh.IsEmpty()
		? FQuat(FRotator(0.0f, -Angles.Y, 0.0f)) : Def->ModelQuat;
	const FQuat SkeletalRotation = Def->ModelMesh.IsEmpty()
		? FQuat(ElysiumSkeletalBasis::FromSourceAngles(Angles)) : Def->ModelQuat;
	FElysiumPlacedModelRequest Request;
	Request.ModelPath = Model;
	Request.StaticStem = VisualStem;
	Request.Location = Origin;
	Request.Rotation = SkeletalRotation;
	Request.UniformScale = Embodiment->BodyScaleFor(*Def);
	Request.PlacementToken = Handle.Index;
	Request.Skin = Skin;
	if (Embodiment->HasPlacedModelCatalogue())
	{
		const FElysiumPlacedModelBody Placed = Embodiment->BuildPlacedModelBody(Request);
		WorldBody = Placed.Visual;
		AnimatedStem = Placed.Stem;
	}
	else
	{
		WorldBody = Embodiment->BuildPropVisual(
			VisualStem, Origin, StaticRotation, Embodiment->BodyScaleFor(*Def));
	}
	if (WorldBody)
	{
		World->RegisterPropBody(WorldBody, Handle);
		ApplySkin();
		GateWorldBody();
	}
}

void FElysiumItemContainer::DestroyWorldBody()
{
	if (WorldBody)
	{
		WorldBody->DestroyComponent();
		WorldBody = nullptr;
	}
	VisualStem.Reset();
	AnimatedStem.Reset();
}

void FElysiumItemContainer::ApplySkin()
{
	if (WorldBody && World && World->Embodiment() && !VisualStem.IsEmpty())
	{
		if (USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(WorldBody))
		{
			World->Embodiment()->ApplyAnimatedPropSkin(Skeletal, VisualStem, Skin);
		}
		else if (UStaticMeshComponent* Static = Cast<UStaticMeshComponent>(WorldBody))
		{
			World->Embodiment()->ApplyPropSkin(Static, VisualStem, Skin);
		}
	}
}

void FElysiumItemContainer::SetSkin(int32 Family)
{
	Skin = Family;
	ApplySkin();
}

void FElysiumItemContainer::GateWorldBody()
{
	const bool bVisible = !IsInert();
	if (WorldBody) { WorldBody->SetVisibility(bVisible); }
	if (World) { World->SetUseAnchorEnabled(Handle, bVisible && !IsLockedByAttachment()); }
}

void FElysiumItemContainer::OnRuntimeTransformChanged()
{
	FElysiumCombatCharacter::OnRuntimeTransformChanged();
	if (WorldBody)
	{
		const FQuat Rotation = Cast<USkeletalMeshComponent>(WorldBody)
			? FQuat(ElysiumSkeletalBasis::FromSourceAngles(Angles))
			: FQuat(FRotator(0.0f, -Angles.Y, 0.0f));
		WorldBody->SetWorldLocationAndRotation(Origin, Rotation);
	}
}

void FElysiumItemContainer::OnRuntimeModelChanged()
{
	DestroyWorldBody();
	BuildWorldBody();
}

void FElysiumItemContainer::OnDormancyChanged()
{
	FElysiumCombatCharacter::OnDormancyChanged();
	GateWorldBody();
	if (IsInert() && CurrentUser.IsSet() && World)
	{
		World->EndPlayerUseSession(Handle, EElysiumUseEndReason::TargetInvalid);
	}
}

UPrimitiveComponent* FElysiumItemContainer::GetAttachBody() const
{
	return WorldBody;
}

void FElysiumItemContainer::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumCombatCharacter::GetDebugState(Out);
	Out.Emplace(TEXT("Contents"), FString::Printf(TEXT("%d / %d"), Inventory.Num(), FElysiumInventory::MaxSlots));
	Out.Emplace(TEXT("Current user"), CurrentUser.IsSet()
		? (World ? World->DescribeHandle(CurrentUser) : CurrentUser.ToString()) : TEXT("(none)"));
	Out.Emplace(TEXT("Attached lock"), AttachedLock.IsSet()
		? (World ? World->DescribeHandle(AttachedLock) : AttachedLock.ToString()) : TEXT("(none)"));
	Out.Emplace(TEXT("Seeds"), bSeedsMaterialized ? TEXT("materialized") : TEXT("pending"));
}

// ============================================================================================
// Registration
// ============================================================================================

namespace
{
	TUniquePtr<FElysiumEntity> MakeItem() { return MakeUnique<FElysiumItem>(); }
	TUniquePtr<FElysiumEntity> MakeKeyring() { return MakeUnique<FElysiumKeyring>(); }
	TUniquePtr<FElysiumEntity> MakeItemContainer() { return MakeUnique<FElysiumItemContainer>(); }

	// CBaseCombatWeapon — the chain node every item classname registers under. It carries the four
	// recovered datamap fields and nothing else: an item's INPUTS are the base chain's, and its
	// policy is the catalogue's.
	struct FElysiumItemChainRegistrar
	{
		FElysiumItemChainRegistrar()
		{
			FElysiumClassDesc& D = FElysiumClassRegistry::Get().Register(
				ElysiumItemClassName(), ElysiumAnimatingClassName(), &MakeItem);

			ElysiumAddClassField(D, TEXT("m_hOwner"), &FElysiumItem::Owner);
			ElysiumAddClassField(D, TEXT("m_iInvenPos"), &FElysiumItem::InvenPos);
			ElysiumAddClassField(D, TEXT("m_iItemCount"), &FElysiumItem::ItemCount);
			ElysiumAddClassField(D, TEXT("m_iAmmoTypes"), &FElysiumItem::AmmoType);
			ElysiumAddClassField(D, TEXT("m_iMagazineCurAmts"), &FElysiumItem::MagazineCount);
		}
	};

	const FElysiumItemChainRegistrar GItemChainRegistrar;

	void AddContainerSeedField(FElysiumClassDesc& D, int32 Index)
	{
		FElysiumFieldAccessor Acc;
		Acc.ApplyFlags(ElysiumFieldDefault);
		Acc.Type = EElysiumVariantType::String;
		Acc.Get = [Index](const FElysiumEntity& E)
		{
			return FElysiumVariant::String(static_cast<const FElysiumItemContainer&>(E).EquipSeeds[Index]);
		};
		Acc.Set = [Index](FElysiumEntity& E, const FElysiumVariant& V)
		{
			static_cast<FElysiumItemContainer&>(E).EquipSeeds[Index] = V.ToString();
		};
		D.Fields.Add(FName(*FString::Printf(TEXT("equip%d"), Index)), MoveTemp(Acc));
	}

	void BuildItemContainerClass(FElysiumClassDesc& D)
	{
		D.Input(TEXT("Use"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FElysiumItemContainer&>(E).Use(A.Activator); });
		D.Input(TEXT("SpawnItemInContainer"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FElysiumItemContainer&>(E).InputSpawnItemInContainer(A); });
		D.Input(TEXT("AddEntityToContainer"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FElysiumItemContainer&>(E).InputAddEntityToContainer(A); });
		D.Input(TEXT("DeleteItems"), [](FElysiumEntity& E, const FElysiumInputArgs& A)
			{ static_cast<FElysiumItemContainer&>(E).InputDeleteItems(A); });

		for (int32 Index = 0; Index < 12; ++Index)
		{
			AddContainerSeedField(D, Index);
		}

		ElysiumAddClassField(D, TEXT("dmgmodel"), &FElysiumItemContainer::DamageModel);

		// Shadow CBaseAnimating.skin so a key/script write repaints the container's prop body.
		FElysiumFieldAccessor Skin;
		Skin.ApplyFlags(ElysiumFieldDefault);
		Skin.Type = EElysiumVariantType::Int;
		Skin.Get = [](const FElysiumEntity& E)
		{
			return FElysiumVariant::Int(static_cast<const FElysiumItemContainer&>(E).Skin);
		};
		Skin.Set = [](FElysiumEntity& E, const FElysiumVariant& V)
		{
			static_cast<FElysiumItemContainer&>(E).SetSkin(V.ToInt());
		};
		D.Fields.Add(FName(TEXT("skin")), MoveTemp(Skin));

		ElysiumAddClassField(D, TEXT("m_BCCUser"), &FElysiumItemContainer::CurrentUser, EElysiumField::Save);
		ElysiumAddClassField(D, TEXT("m_hLockEnt"), &FElysiumItemContainer::AttachedLock, EElysiumField::Save);
	}

	struct FElysiumItemContainerRegistrar
	{
		FElysiumItemContainerRegistrar()
		{
			FElysiumClassRegistry& Reg = FElysiumClassRegistry::Get();
			BuildItemContainerClass(Reg.Register(FName(TEXT("item_container")),
				ElysiumCombatCharacterClassName(), &MakeItemContainer));
			Reg.Register(FName(TEXT("item_container_animated")),
				FName(TEXT("item_container")), &MakeItemContainer);
			Reg.Register(FName(TEXT("item_container_one_item_filtered")),
				FName(TEXT("item_container")), &MakeItemContainer);
		}
	};

	const FElysiumItemContainerRegistrar GItemContainerRegistrar;
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
			// WHICH chain node is decided by the parsed record's item type and by nothing else — a
			// weapon-family definition registers under `CWeapon`, where the controller's scheduling
			// inputs live, and everything else stays on `CBaseCombatWeapon` (K-rule: policy from the
			// record, never from the classname prefix).
			if (ClassName == Keyring)
			{
				Reg.Register(ClassName, ElysiumItemClassName(), &MakeKeyring);
			}
			else if (Item.IsControllableWeapon())
			{
				Reg.Register(ClassName, ElysiumWeaponClassName(), &ElysiumWeapons::MakeWeapon);
			}
			else
			{
				Reg.Register(ClassName, ElysiumItemClassName(), &MakeItem);
			}
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

	bool ExecuteBarter(FElysiumEntityWorld& World, const FString& Args)
	{
		FElysiumPlayer* Player = World.FindPlayer();
		if (!Player)
		{
			UE_LOG(LogElysiumItem, Warning, TEXT("vbarter: no player entity"));
			return false;
		}

		FElysiumItemContainer* Open = nullptr;
		for (const TUniquePtr<FElysiumEntity>& Candidate : World.Entities())
		{
			FElysiumItemContainer* Container = Candidate ? Candidate->AsItemContainer() : nullptr;
			if (Container && !Container->IsDead() && Container->CurrentUser == Player->Handle)
			{
				Open = Container;
				break;
			}
		}
		if (!Open)
		{
			UE_LOG(LogElysiumItem, Display, TEXT("vbarter: no open loot container"));
			return false;
		}

		TArray<FString> Tokens;
		Args.ParseIntoArrayWS(Tokens);
		if (Tokens.Num() != 2)
		{
			UE_LOG(LogElysiumItem, Display, TEXT("usage: vbarter <Take|Give> <slot>"));
			return false;
		}
		int32 Slot = INDEX_NONE;
		if (!LexTryParseString(Slot, *Tokens[1]) || Slot < 0)
		{
			UE_LOG(LogElysiumItem, Display, TEXT("usage: vbarter <Take|Give> <slot>"));
			return false;
		}
		bool bDone = false;
		if (Tokens[0].Equals(TEXT("Take"), ESearchCase::IgnoreCase))
		{
			bDone = Open->TakeToPlayer(*Player, Slot);
		}
		else if (Tokens[0].Equals(TEXT("Give"), ESearchCase::IgnoreCase))
		{
			bDone = Open->GiveFromPlayer(*Player, Slot);
		}
		else if (Tokens[0].Equals(TEXT("Buy"), ESearchCase::IgnoreCase)
			|| Tokens[0].Equals(TEXT("Sell"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogElysiumItem, Display,
				TEXT("vbarter %s: priced vendor transactions remain 9.10"), *Tokens[0]);
			return false;
		}
		else
		{
			UE_LOG(LogElysiumItem, Display, TEXT("usage: vbarter <Take|Give> <slot>"));
			return false;
		}

		if (!bDone)
		{
			UE_LOG(LogElysiumItem, Display, TEXT("vbarter %s %d refused"), *Tokens[0], Slot);
		}
		return bDone;
	}
}
