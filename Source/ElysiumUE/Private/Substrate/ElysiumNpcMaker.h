#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntity.h"

// ============================================================================================
// FElysiumNpcMaker — npc_maker: retail admission, quotas, timed retries and child ownership.
// ============================================================================================

class FElysiumNpcMaker final : public FElysiumEntity
{
public:
	FString NpcType;
	int32 RemainingTotal = 0;       // MaxNPCCount is the mutable remaining finite total
	float SpawnFrequency = 0.0f;
	int32 LiveChildren = 0;
	int32 MaxLiveChildren = 0;
	float CachedGroundZ = 0.0f;
	FString ChildTargetName;
	bool bDisabled = false;
	bool bNpcClip = false;
	bool bFade = false;
	bool bInfinite = false;
	bool bNoDrop = false;           // base CNPCMaker declares it but does not consume it
	bool bViewCone = false;
	int32 MinPcDistance = 0;        // Source units

	enum class EAttempt : uint8
	{
		Spawned,
		LiveLimit,
		Scene,
		Visible,
		ViewCone,
		Distance,
		Occupied,
		InvalidChild,
	};
	EAttempt LastAttempt = EAttempt::InvalidChild;

	static const TCHAR* AttemptName(EAttempt Attempt);

	bool IsDepleted() const { return !bInfinite && RemainingTotal < 1; }

	virtual void Spawn() override;

	EAttempt CanMakeNpc(bool bBypass) const;

	EAttempt TrySpawn(bool bBypass = false);

	void InputSpawn(const FElysiumInputArgs&) { TrySpawn(/*bBypass=*/false); }

	void InputEnable(const FElysiumInputArgs&);
	void InputDisable(const FElysiumInputArgs&);
	void InputToggle(const FElysiumInputArgs& Args);

	virtual void Think() override;

	virtual void OnOwnedEntityTerminated(FElysiumEntity& Child,
		EElysiumOwnedEntityTermination Reason) override;

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;
};
