#pragma once

#include "CoreMinimal.h"

#include "ElysiumPlayer.h"

class UPrimitiveComponent;
class FElysiumLockableEntity;
struct FElysiumLootView;

// CItemContainer over CBaseCombatCharacter.

class FElysiumItemContainer final : public FElysiumCombatCharacter
{
public:
	FString EquipSeeds[12];
	FElysiumEntityHandle CurrentUser;
	FElysiumEntityHandle AttachedLock;
	FString DamageModel;

	virtual void Spawn() override;
	virtual void Activate() override;
	virtual void Think() override;
	virtual void Serialize(FElysiumSaveArchive& Ar) override;
	virtual void Use(const FElysiumEntityHandle& Activator) override;
	virtual FElysiumUseBeginResult BeginPlayerUse(const FElysiumUseContext& Context) override;
	virtual void EndPlayerUse(const FElysiumUseContext& Context,
		EElysiumUseEndReason Reason) override;
	virtual bool IsUsable() const override { return true; }
	virtual bool CanPlayerFocus(const FElysiumUseContext& Context) const override;
	virtual const TCHAR* SaveBlockReason() const override;
	virtual void OnRuntimeTransformChanged() override;
	virtual void OnRuntimeModelChanged() override;
	virtual void OnDormancyChanged() override;
	virtual UPrimitiveComponent* GetAttachBody() const override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;
	virtual FElysiumItemContainer* AsItemContainer() override { return this; }

	void InputSpawnItemInContainer(const FElysiumInputArgs& Args);
	void InputAddEntityToContainer(const FElysiumInputArgs& Args);
	void InputDeleteItems(const FElysiumInputArgs& Args);
	bool TakeToPlayer(FElysiumPlayer& Player, int32 Slot);
	bool GiveFromPlayer(FElysiumPlayer& Player, int32 Slot);
	void BuildLootView(FElysiumLootView& Out, const FElysiumPlayer& Player) const;
	void SetSkin(int32 Family);
	bool RegisterLock(FElysiumLockableEntity& Lock);
	void NotifyLockState(const FElysiumEntityHandle& Lock, bool bLocked);
	bool IsLockedByAttachment() const;

private:
	bool SpawnNamedItem(const FString& Classname);
	void DeleteAllItems();
	void BuildWorldBody();
	void DestroyWorldBody();
	void ApplySkin();
	void GateWorldBody();
	void PlayUseAnimation(bool bOpening);

	UPrimitiveComponent* WorldBody = nullptr;
	FString VisualStem;
	FString AnimatedStem;
	bool bSeedsMaterialized = false;
	uint32 LootRevision = 0;
};
