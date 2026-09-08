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
	// `CBasePlayer::PlayerUse`'s maintenance arm `FUN_10167e00` over the lock family's slots: the
	// slot-37 reach test picks between the reposition (slots 40/43, `FUN_10224440`) and the per-tick
	// half of the skill attempt (slot 41, `FUN_102252f0`).
	virtual void TickPlayerUse(const FElysiumUseContext& Context) override;
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
	// --- The `Intrusion` interaction's own constants, all read out of `vampire.dll`'s `.rdata` ----

	// Slot 37, `CPropDoorknob::vfunc37` `FUN_10224eb0` -> `_DAT_104454c8` = **80.0** Source units —
	// the same value `CBaseEntity` holds, so the whole lock family runs on it. It is the
	// **reposition** threshold, not a break-off: `FUN_10167e00` dispatches slot 43 beyond it and
	// slot 41 inside it, and the lock's slot 32 (`FUN_10224ae0`) carries no distance arm at all.
	static constexpr float HoldReachCm = 80.0f * 2.54f;

	// `FUN_10224440`'s two literals. `_DAT_1048c35c` = **31.0** units is how far out from the lock's
	// own abs origin the player is stood, along the flattened `camera_target` -> `camera_position`
	// axis; `_DAT_1045d650` = **1024.0** units is the length of the straight-down probe that has to
	// find floor under that spot before the player is moved at all.
	static constexpr float StandOffCm = 31.0f * 2.54f;
	static constexpr float GroundProbeCm = 1024.0f * 2.54f;

	// Slot 36, `CPropDoorknob::vfunc36` `FUN_10224ca0` — the required item, verbatim. It is the
	// item the `+USE` maintenance arm auto-EQUIPS (the `Intrusion` shot authors `DrawViewmodel 1`
	// precisely because the pick is meant to be visible), never a holster.
	static const TCHAR* RequiredItemClassname() { return TEXT("item_g_lockpick"); }

	// `FUN_10070470("Intrusion", NULL, this, this, NULL)` + `cam->m_bForcePlayerLook = 0` +
	// `FUN_1017cef0(player, cam)` + `FUN_1015ef40(player)`, in that order — the tail of slot 39
	// (`FUN_10225070`). A shot that will not load is retail's NULL camera and must not refuse.
	void OpenIntrusionCamera();
	// `FUN_1017cef0(player, NULL)` + `FUN_1015ef60(player)` — the middle of slot 42
	// (`FUN_10225140`). A same-tick cut: the disposable camera is destroyed, there is no blend.
	void CloseIntrusionCamera();
	// Slots 40 and 43, one body: `FUN_10224440`.
	void PlaceUserAtLock(FElysiumCombatCharacter& User);

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
