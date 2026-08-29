// Item entities, the keyring, combat-character inventories, and loot containers.
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
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumClassFields.h"
#include "Substrate/ElysiumItemContainer.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumWeaponClasses.h"

#include "Components/StaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"

DEFINE_LOG_CATEGORY(LogElysiumItem);

// --- The installed catalogue ---

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

// --- Registration ---

namespace
{
	TUniquePtr<FElysiumEntity> MakeItem() { return MakeUnique<FElysiumItem>(); }
	TUniquePtr<FElysiumEntity> MakeKeyring() { return MakeUnique<FElysiumKeyring>(); }

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
