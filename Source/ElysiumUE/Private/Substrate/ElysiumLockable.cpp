#include "Substrate/ElysiumLockable.h"

#include "ElysiumContentPaths.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumMover.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"

namespace
{
	const FName GOnUnlocked(TEXT("OnUnlocked"));
	const FName GOnUseBegin(TEXT("OnUseBegin"));
	const FName GOnUseEnd(TEXT("OnUseEnd"));
}

void FElysiumLockableEntity::Spawn()
{
	LastRoll = Difficulty != 0 ? 1 : 3;
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Embodiment || !Def)
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
	if (Embodiment->HasPlacedModelCatalogue())
	{
		const FElysiumPlacedModelBody Placed = Embodiment->BuildPlacedModelBody(Request);
		WorldBody = Placed.Visual;
		AnimatedStem = Placed.Stem;
	}
	else
	{
		AnimatedStem = Embodiment->AnimatedPropStemForModel(Model);
		if (!AnimatedStem.IsEmpty())
		{
			WorldBody = Embodiment->BuildAnimatedPropVisual(AnimatedStem, Origin, SkeletalRotation,
				Embodiment->BodyScaleFor(*Def), Handle.Index);
		}
		else
		{
			WorldBody = Embodiment->BuildPropVisual(
				VisualStem, Origin, StaticRotation, Embodiment->BodyScaleFor(*Def));
		}
	}
	if (WorldBody)
	{
		World->RegisterPropBody(WorldBody, Handle);
	}
	OnLockPresentationChanged();
}

void FElysiumLockableEntity::PostSpawn()
{
	FElysiumEntity::PostSpawn();
	FElysiumEntity* Parent = World && !ParentName.IsEmpty() ? World->FindByName(ParentName) : nullptr;
	if (!Parent || !AttachToParent(*Parent))
	{
		UE_LOG(LogElysiumSkill, Warning, TEXT("%s attached to invalid owner: %s"),
			*DebugString(), *ParentName);
		Kill();
	}
}

void FElysiumLockableEntity::Serialize(FElysiumSaveArchive& Ar)
{
	FElysiumSkillEntity::Serialize(Ar);
	if (!Ar.IsLoading())
	{
		return;
	}
	OnLockPresentationChanged();
	if (FElysiumEntity* Parent = World ? World->Resolve(AttachedOwner) : nullptr)
	{
		if (FElysiumItemContainer* Container = Parent->AsItemContainer())
		{
			Container->NotifyLockState(Handle, IsUseLocked());
		}
	}
}

bool FElysiumLockableEntity::AttachToParent(FElysiumEntity& Parent)
{
	FElysiumDoorBase* Door = Parent.AsDoorBase();
	if (!Door)
	{
		return false;
	}
	AttachedOwner = Door->Handle;
	Door->RegisterDoorknob(*this);
	return !IsDead();
}

void FElysiumLockableEntity::InputLock()
{
	if (FElysiumEntity* Parent = World ? World->Resolve(AttachedOwner) : nullptr)
	{
		if (FElysiumDoorBase* Door = Parent->AsDoorBase())
		{
			Door->InputLock();
			return;
		}
		if (FElysiumItemContainer* Container = Parent->AsItemContainer())
		{
			ApplyDoorLockState(true);
			Container->NotifyLockState(Handle, true);
			return;
		}
	}
	ApplyDoorLockState(true);
}

void FElysiumLockableEntity::InputUnlock(const FElysiumEntityHandle& Activator)
{
	FireOutput(GOnUnlocked, Activator);
	if (FElysiumEntity* Parent = World ? World->Resolve(AttachedOwner) : nullptr)
	{
		if (FElysiumDoorBase* Door = Parent->AsDoorBase())
		{
			Door->InputUnlock();
		}
		else if (FElysiumItemContainer* Container = Parent->AsItemContainer())
		{
			ApplyDoorLockState(false);
			Container->NotifyLockState(Handle, false);
		}
		else
		{
			ApplyDoorLockState(false);
		}
	}
	else
	{
		ApplyDoorLockState(false);
	}
	OnUnlocked(Activator);
}

void FElysiumLockableEntity::ApplyDoorLockState(bool bLocked)
{
	LastRoll = bLocked ? 1 : 3;
	OnLockPresentationChanged();
}

int32 FElysiumLockableEntity::ResolveUseIcon(const FElysiumEntityHandle& Activator) const
{
	if (IsUseLocked() && KeyIcon != 0 && !KeyName.IsEmpty() && World)
	{
		const FElysiumEntity* Entity = World->Resolve(Activator);
		const FElysiumCombatCharacter* User = Entity ? Entity->AsCombatCharacter() : nullptr;
		if (User && User->Inventory.Has(*User, KeyName))
		{
			return KeyIcon;
		}
	}
	return GetUseIcon();
}

FElysiumUseBeginResult FElysiumLockableEntity::BeginPlayerUse(const FElysiumUseContext& Context)
{
	FElysiumEntity* UserEntity = World ? World->Resolve(Context.Activator) : nullptr;
	FElysiumCombatCharacter* User = UserEntity ? UserEntity->AsCombatCharacter() : nullptr;
	if (!User)
	{
		return FElysiumUseBeginResult::Completed();
	}
	if (!IsUseLocked())
	{
		Use(Context.Activator);
		return FElysiumUseBeginResult::Completed();
	}
	if (!KeyName.IsEmpty() && User->Inventory.Has(*User, KeyName))
	{
		if (bDeleteKey)
		{
			User->Inventory.ScriptRemove(*User, KeyName);
		}
		InputUnlock(Context.Activator);
		Use(Context.Activator);
		return FElysiumUseBeginResult::Completed();
	}
	if (bRequiresKey || !User->Inventory.Has(*User, TEXT("item_g_lockpick")))
	{
		UE_LOG(LogElysiumSkill, Display, TEXT("%s: locked use refused — %s"), *DebugString(),
			bRequiresKey ? TEXT("requires key") : TEXT("no item_g_lockpick"));
		return FElysiumUseBeginResult::Completed();
	}
	FireOutput(GOnUseBegin, Context.Activator);
	bUseOutputsOpen = true;
	if (StartAttempt(*User))
	{
		return FElysiumUseBeginResult::Started(EElysiumUseSessionKind::Explicit);
	}
	FinishUseOutputs(Context.Activator);
	return FElysiumUseBeginResult::Completed();
}

void FElysiumLockableEntity::EndPlayerUse(const FElysiumUseContext& Context,
	EElysiumUseEndReason Reason)
{
	FElysiumEntity* UserEntity = World ? World->Resolve(Context.Activator) : nullptr;
	FElysiumCombatCharacter* User = UserEntity ? UserEntity->AsCombatCharacter() : nullptr;
	if (User)
	{
		if (const TCHAR* Feat = FeatForSkillType(SkillType))
		{
			LastSkillLevel = User->CalcFeat(Feat);
		}
		OnAttemptStopped(*User, Reason);
	}
	else
	{
		StopAttempt();
	}
	FinishUseOutputs(Context.Activator);
}

void FElysiumLockableEntity::Use(const FElysiumEntityHandle& Activator)
{
	FireOutput(GOnUseBegin, Activator);
	ForwardUse(Activator);
	FireOutput(GOnUseEnd, Activator);
}

void FElysiumLockableEntity::OnSkillSucceeded(FElysiumCombatCharacter& User)
{
	InputUnlock(User.Handle);
	if (World && !World->EndPlayerUseSession(Handle, EElysiumUseEndReason::Completed))
	{
		UE_LOG(LogElysiumSkill, Warning,
			TEXT("%s skill success could not close its captured +use session"), *DebugString());
	}
	ForwardUse(User.Handle);
}

void FElysiumLockableEntity::OnAttemptStopped(FElysiumCombatCharacter&, EElysiumUseEndReason)
{
	StopAttempt();
}

void FElysiumLockableEntity::FinishUseOutputs(const FElysiumEntityHandle& Activator)
{
	if (bUseOutputsOpen)
	{
		bUseOutputsOpen = false;
		FireOutput(GOnUseEnd, Activator);
	}
}

void FElysiumLockableEntity::ForwardUse(const FElysiumEntityHandle&)
{
	UE_LOG(LogElysiumSkill, Warning,
		TEXT("%s accepted lockable use but has no specialized attachment forwarder"), *DebugString());
}

void FElysiumLockableEntity::OnLockPresentationChanged() {}

void FElysiumLockableEntity::OnDormancyChanged()
{
	FElysiumEntity::OnDormancyChanged();
	if (WorldBody)
	{
		WorldBody->SetVisibility(!IsInert());
	}
	if (World)
	{
		World->SetUseAnchorEnabled(Handle, !IsInert());
	}
}

void FElysiumLockableEntity::OnRuntimeTransformChanged()
{
	FElysiumEntity::OnRuntimeTransformChanged();
	if (!WorldBody)
	{
		return;
	}
	const FQuat Rotation = Cast<USkeletalMeshComponent>(WorldBody)
		? FQuat(ElysiumSkeletalBasis::FromSourceAngles(Angles))
		: FQuat(FRotator(0.0f, -Angles.Y, 0.0f));
	WorldBody->SetWorldLocationAndRotation(Origin, Rotation);
}

UPrimitiveComponent* FElysiumLockableEntity::GetAttachBody() const
{
	return WorldBody;
}

void FElysiumLockableEntity::GetDebugState(TArray<TPair<FString, FString>>& Out) const
{
	FElysiumSkillEntity::GetDebugState(Out);
	Out.Emplace(TEXT("Locked"), IsUseLocked() ? TEXT("yes") : TEXT("no"));
	Out.Emplace(TEXT("Attached owner"), AttachedOwner.IsSet() ? AttachedOwner.ToString() : TEXT("(none)"));
}

FElysiumPropDoorknob::FElysiumPropDoorknob()
{
	UseIcon = 10;
	LockedIcon = 3;
	KeyIcon = 4;
}

void FElysiumPropDoorknob::ForwardUse(const FElysiumEntityHandle& Activator)
{
	FElysiumEntity* Parent = World ? World->Resolve(AttachedOwner) : nullptr;
	if (FElysiumDoorBase* Door = Parent ? Parent->AsDoorBase() : nullptr)
	{
		Door->DoorUse(Activator);
		return;
	}
	UE_LOG(LogElysiumSkill, Warning, TEXT("%s cannot forward +use: attached door %s is invalid"),
		*DebugString(), *AttachedOwner.ToString());
}

void FElysiumPropDoorknob::OnLockPresentationChanged()
{
	USkeletalMeshComponent* Animated = Cast<USkeletalMeshComponent>(WorldBody);
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!Animated || !Embodiment || AnimatedStem.IsEmpty())
	{
		if (Embodiment && Embodiment->HasPlacedModelCatalogue() && WorldBody)
		{
			UE_LOG(LogElysiumSkill, Warning,
				TEXT("%s cannot present doorknob lock state: animated body/stem did not resolve"),
				*DebugString());
		}
		return;
	}
	const FString Clip = IsUseLocked() ? TEXT("handle_locked") : TEXT("handle_unlocked");
	bool bLoops = false;
	if (Embodiment->FindAnimatedPropClip(AnimatedStem, Clip, bLoops))
	{
		if (!Embodiment->PlayAnimatedPropClip(
			Animated, AnimatedStem, Clip, /*bLoop*/ false, nullptr))
		{
			UE_LOG(LogElysiumSkill, Warning, TEXT("%s failed to play doorknob sequence '%s' on %s"),
				*DebugString(), *Clip, *AnimatedStem);
		}
	}
	else
	{
		UE_LOG(LogElysiumSkill, Warning, TEXT("%s has no doorknob sequence '%s' on %s"),
			*DebugString(), *Clip, *AnimatedStem);
	}
}

FElysiumElectronicDoorknob::FElysiumElectronicDoorknob()
{
	UseIcon = 53;
	LockedIcon = 54;
	KeyIcon = 5;
}

void FElysiumElectronicDoorknob::Spawn()
{
	bRequiresKey = true;
	FElysiumPropDoorknob::Spawn();
}

void FElysiumElectronicDoorknob::OnLockPresentationChanged()
{
	FElysiumPropDoorknob::OnLockPresentationChanged();
	IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
	if (!WorldBody || !Embodiment || VisualStem.IsEmpty())
	{
		return;
	}
	const int32 Family = IsUseLocked() ? 0 : 1;
	if (USkeletalMeshComponent* Skeletal = Cast<USkeletalMeshComponent>(WorldBody))
	{
		Embodiment->ApplyAnimatedPropSkin(Skeletal, VisualStem, Family);
	}
	else if (UStaticMeshComponent* Static = Cast<UStaticMeshComponent>(WorldBody))
	{
		Embodiment->ApplyPropSkin(Static, VisualStem, Family);
	}
}

FElysiumContainerLock::FElysiumContainerLock()
{
	UseIcon = 10;
	LockedIcon = 3;
}

bool FElysiumContainerLock::AttachToParent(FElysiumEntity& Parent)
{
	FElysiumItemContainer* Container = Parent.AsItemContainer();
	if (!Container || !Container->RegisterLock(*this))
	{
		return false;
	}
	AttachedOwner = Container->Handle;
	return true;
}

void FElysiumContainerLock::ForwardUse(const FElysiumEntityHandle& Activator)
{
	FElysiumEntity* Parent = World ? World->Resolve(AttachedOwner) : nullptr;
	if (FElysiumItemContainer* Container = Parent ? Parent->AsItemContainer() : nullptr)
	{
		const FElysiumUseBeginResult Result = World->BeginPlayerUseSession(
			Container->Handle, Activator);
		if (Result.Outcome != EElysiumUseOutcome::SessionStarted)
		{
			UE_LOG(LogElysiumSkill, Warning,
				TEXT("%s unlocked but failed to forward +use to %s (outcome=%d)"),
				*DebugString(), *Container->DebugString(), static_cast<int32>(Result.Outcome));
		}
		return;
	}
	UE_LOG(LogElysiumSkill, Warning, TEXT("%s cannot forward +use: attached container %s is invalid"),
		*DebugString(), *AttachedOwner.ToString());
}

FElysiumPadlock::FElysiumPadlock()
{
	UseIcon = 10;
	LockedIcon = 3;
	KeyIcon = 4;
}

void FElysiumPadlock::ForwardUse(const FElysiumEntityHandle& Activator)
{
	FElysiumEntity* Parent = World ? World->Resolve(AttachedOwner) : nullptr;
	if (FElysiumDoorBase* Door = Parent ? Parent->AsDoorBase() : nullptr)
	{
		Door->DoorUse(Activator);
		return;
	}
	UE_LOG(LogElysiumSkill, Warning, TEXT("%s cannot forward +use: attached door %s is invalid"),
		*DebugString(), *AttachedOwner.ToString());
}

void FElysiumPadlock::OnUnlocked(const FElysiumEntityHandle&)
{
	FElysiumEntity* Parent = World ? World->Resolve(AttachedOwner) : nullptr;
	if (FElysiumDoorBase* Door = Parent ? Parent->AsDoorBase() : nullptr)
	{
		Door->UnregisterDoorknob(Handle);
	}
	else
	{
		UE_LOG(LogElysiumSkill, Warning,
			TEXT("%s unlocked without a live attached door %s"),
			*DebugString(), *AttachedOwner.ToString());
	}
	Kill();
}
