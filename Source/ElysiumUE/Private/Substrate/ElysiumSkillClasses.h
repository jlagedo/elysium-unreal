#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntity.h"

class FElysiumDoorBase;
class UStaticMeshComponent;

inline FName ElysiumSkillEntityClassName()
{
	return FName(TEXT("CBaseVampireSkillEntity"));
}

inline FName ElysiumLockableEntityClassName()
{
	return FName(TEXT("CBaseLockableEnt"));
}

// The shared timed-threshold state. One accepted use resolves one deterministic feat check; the
// five outputs always use the ordinary entity queue and the think only produces them. Presentation
// is deliberately diagnostic until the Intrusion view publishes this state.
class FElysiumSkillEntity : public FElysiumEntity
{
public:
	int32 Difficulty = 0;
	int32 SkillType = 0;
	int32 LastRoll = 3;
	float LastAttemptSeconds = 0.0f;
	int32 SkillAttempts = 0;
	int32 LastSkillLevel = 0;
	FElysiumEntityHandle AttemptUser;

	void InputResetDifficulty(int32 NewDifficulty);
	virtual void Think() override;
	virtual void Serialize(FElysiumSaveArchive& Ar) override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;
	virtual const TCHAR* SaveBlockReason() const override;

protected:
	bool StartAttempt(FElysiumCombatCharacter& User);
	void StopAttempt();
	virtual void OnSkillSucceeded(FElysiumCombatCharacter& User) {}
	virtual void OnAttemptStopped(FElysiumCombatCharacter& User, EElysiumUseEndReason Reason) {}

private:
	void ResolveAttempt(FElysiumCombatCharacter& User);
	float AttemptIntervalSeconds(int32 Rating) const;
};

// CBaseLockableEnt. Lock state is LastRoll (<3 locked), exactly as retail; key and lockpick paths
// meet at Unlock and then forward to the attached leaf.
class FElysiumLockableEntity : public FElysiumSkillEntity
{
public:
	FString KeyName;
	bool bDeleteKey = false;
	bool bRequiresKey = false;

	virtual void Spawn() override;
	virtual void PostSpawn() override;
	virtual bool IsUsable() const override { return true; }
	virtual bool IsUseLocked() const override { return LastRoll < 3; }
	virtual FElysiumUseBeginResult BeginPlayerUse(const FElysiumUseContext& Context) override;
	virtual void EndPlayerUse(const FElysiumUseContext& Context, EElysiumUseEndReason Reason) override;
	virtual void Use(const FElysiumEntityHandle& Activator) override;
	virtual void OnDormancyChanged() override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;
	virtual UPrimitiveComponent* GetAttachBody() const override;
	virtual FElysiumLockableEntity* AsLockableEntity() override { return this; }

	void InputLock();
	void InputUnlock(const FElysiumEntityHandle& Activator);
	void ApplyDoorLockState(bool bLocked);

protected:
	virtual void OnSkillSucceeded(FElysiumCombatCharacter& User) override;
	virtual void OnAttemptStopped(FElysiumCombatCharacter& User, EElysiumUseEndReason Reason) override;
	virtual void ForwardUse(const FElysiumEntityHandle& Activator);
	void FinishUseOutputs(const FElysiumEntityHandle& Activator);

	FElysiumEntityHandle AttachedDoor;
	UStaticMeshComponent* WorldBody = nullptr;
	bool bUseOutputsOpen = false;
};

class FElysiumPropDoorknob final : public FElysiumLockableEntity
{
protected:
	virtual void ForwardUse(const FElysiumEntityHandle& Activator) override;
};
