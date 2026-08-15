#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntity.h"
#include "Substrate/ElysiumInterestingPlaces.h"

// ============================================================================================
// intersting_place — retail's shipped classname is misspelled. The entity owns enable/capacity,
// the authored timing/orientation/type fields, and arrival/leave outputs. NPCs own reservations:
// the logical state remains saveable in the substrate while Unreal only moves the body.
// ============================================================================================

class FElysiumInterestingPlace final : public FElysiumEntity
{
public:
	FString Type;
	bool bEnabled = true;
	int32 MaxNpcs = 1;
	int32 GroupId = 0;
	int32 Rating = 0;
	int32 TestFlags = 0;
	bool bMatchOrientation = false;
	float MinTime = 5.0f;
	float MaxTime = 10.0f;

	bool IsAvailable() const;
	bool Claim(const FElysiumEntityHandle& Npc);
	void Release(const FElysiumEntityHandle& Npc) { Claimants.Remove(Npc.Index); }
	bool IsEnabledFor(const FElysiumEntityHandle& Npc) const;
	void Arrived(const FElysiumEntityHandle& Npc);
	void Left(const FElysiumEntityHandle& Npc);
	void InputEnable(const FElysiumInputArgs&) { bEnabled = true; }
	void InputDisable(const FElysiumInputArgs&) { bEnabled = false; }
	void InputToggle(const FElysiumInputArgs&) { bEnabled = !bEnabled; }

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

private:
	TSet<int32> Claimants;
};

namespace ElysiumInterestingPlaces
{
	// The one-shot `interestingplacetypelist.txt` load, kept beside the entity whose `type` keys
	// into it. Null when the table did not load; the failure is warned once at the first call.
	const FElysiumInterestingPlaceTable* Types();
}
