#pragma once

#include "CoreMinimal.h"

#include "Substrate/ElysiumSkillClasses.h"

class UPrimitiveComponent;

inline FName ElysiumLockableEntityClassName()
{
	return FName(TEXT("CBaseLockableEnt"));
}

// CBaseLockableEnt. Lock state is LastRoll (<3 locked), exactly as retail; key and lockpick paths
// meet at Unlock and then forward to the attached leaf.
class FElysiumLockableEntity : public FElysiumSkillEntity
{
public:
	FString KeyName;
	bool bDeleteKey = false;
	bool bRequiresKey = false;
	int32 KeyIcon = 0;

	virtual void Spawn() override;
	virtual void PostSpawn() override;
	virtual void Serialize(FElysiumSaveArchive& Ar) override;
	virtual bool IsUsable() const override { return true; }
	virtual bool IsUseLocked() const override { return LastRoll < 3; }
	virtual int32 ResolveUseIcon(const FElysiumEntityHandle& Activator) const override;
	virtual FElysiumUseBeginResult BeginPlayerUse(const FElysiumUseContext& Context) override;
	virtual void EndPlayerUse(const FElysiumUseContext& Context, EElysiumUseEndReason Reason) override;
	virtual void Use(const FElysiumEntityHandle& Activator) override;
	virtual void OnDormancyChanged() override;
	virtual void OnRuntimeTransformChanged() override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;
	virtual UPrimitiveComponent* GetAttachBody() const override;
	virtual FElysiumLockableEntity* AsLockableEntity() override { return this; }

	void InputLock();
	void InputUnlock(const FElysiumEntityHandle& Activator);
	// The lock this entity owns. A door never writes it: retail's CBaseDoor consults the knob
	// (CBaseDoor::IsUseRefused FUN_100eec70) and pushes only the handle pose back down.
	void SetLockState(bool bLocked);
	// Retail vtable +0x444 — re-pose the handle from this entity's own lock state. This is the
	// whole of what a door pushes to its knobs.
	void RefreshHandlePose() { OnLockPresentationChanged(); }

protected:
	virtual void OnSkillSucceeded(FElysiumCombatCharacter& User) override;
	virtual void OnAttemptStopped(FElysiumCombatCharacter& User, EElysiumUseEndReason Reason) override;
	virtual void ForwardUse(const FElysiumEntityHandle& Activator);
	virtual bool AttachToParent(FElysiumEntity& Parent);
	virtual void OnLockPresentationChanged();
	virtual void OnUnlocked(const FElysiumEntityHandle& Activator) {}
	// Whether the built body is ever rendered. A leaf whose retail Spawn stamps EF_NODRAW keeps
	// the body — its registration is the use anchor, the same bounds an undrawn retail entity
	// keeps — but never shows it, and no later un-hide may either.
	virtual bool DrawsWorldBody() const { return true; }
	void FinishUseOutputs(const FElysiumEntityHandle& Activator);

	FElysiumEntityHandle AttachedOwner;
	UPrimitiveComponent* WorldBody = nullptr;
	FString VisualStem;
	FString AnimatedStem;
	bool bUseOutputsOpen = false;
};

class FElysiumPropDoorknob : public FElysiumLockableEntity
{
public:
	FElysiumPropDoorknob();

protected:
	virtual void ForwardUse(const FElysiumEntityHandle& Activator) override;
	virtual void OnLockPresentationChanged() override;
};

class FElysiumElectronicDoorknob final : public FElysiumPropDoorknob
{
public:
	FElysiumElectronicDoorknob();
	virtual void Spawn() override;

protected:
	virtual void OnLockPresentationChanged() override;
};

class FElysiumContainerLock final : public FElysiumLockableEntity
{
public:
	FElysiumContainerLock();

protected:
	virtual bool AttachToParent(FElysiumEntity& Parent) override;
	virtual void ForwardUse(const FElysiumEntityHandle& Activator) override;
	// CItemContainerLock::Spawn (vampire.dll 102264f0) sets EF_NODRAW (`m_fEffects |= 0x40`,
	// docs/vtmb/entity_visuals.md) unconditionally: retail spawns the lock with its authored
	// model — lock state, sounds and use bounds — but never renders it. The tutorial safe's
	// `tutsafelock` carries `padlock1.mdl` and shows nothing.
	virtual bool DrawsWorldBody() const override { return false; }
};

class FElysiumPadlock final : public FElysiumLockableEntity
{
public:
	FElysiumPadlock();

protected:
	virtual void ForwardUse(const FElysiumEntityHandle& Activator) override;
	virtual void OnUnlocked(const FElysiumEntityHandle& Activator) override;
};
