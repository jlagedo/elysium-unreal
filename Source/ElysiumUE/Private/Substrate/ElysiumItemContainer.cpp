#include "Substrate/ElysiumItemContainer.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumViewState.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumClassFields.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumSkillClasses.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"

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
	TUniquePtr<FElysiumEntity> MakeItemContainer() { return MakeUnique<FElysiumItemContainer>(); }

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
