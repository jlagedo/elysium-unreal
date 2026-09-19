#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntity.h"

struct FElysiumClassDesc;

// `intersting_place_conversation` — retail's `CAI_InterestingPlaceConverstation` (datamap
// `0x1060c2c0`, base `CBaseEntity`, not the interesting place). This leaf stands the entity: its ten
// keyfields, its three inputs and their writes. Its behaviour — `WaitThink` / `TalkThink`, the
// talker pick over the places `interesting_places` names, the sounds, the six outputs — is 0018
// story 14's, so each input's call into that machinery is a named seam here.
class FElysiumConversationPlace final : public FElysiumEntity
{
public:
	// --- Keyfields (generated bindings, `ElysiumNpcKernelBindings::AddConversationPlaceFields`) ---
	FString InterestingPlaces;       // +0x450 m_iszInterestingPlaces  interesting_places
	FString SoundLoop;               // +0x454 m_iszSoundLoop          sound_loop
	FString SoundOnce;               // +0x458 m_iszSoundOnce          sound_once
	bool bEnabled = false;           // +0x45c m_bEnabled              enabled
	float PlayerDist = 0.0f;         // +0x464 m_flPlayerDist          player_dist
	float AudibleDist = 0.0f;        // +0x468 m_flAudibleDist         audible_dist
	float MinTime = 0.0f;            // +0x50c m_flMinTime             min_time
	float MaxTime = 0.0f;            // +0x510 m_flMaxTime             max_time
	bool bTurnTowardsTalker = false; // +0x514 m_bTurnTowardsTalker    turn_towards_talker
	bool bSoundOccluded = false;     // +0x515 m_bSoundOccluded        sound_occluded

	// --- Save-only word the inputs write -----------------------------------------------------------
	bool bPlayOneOffSound = false;   // +0x45d m_bPlayOneOffSound

	// How many times each input reached its unported half — the observable record of a seam.
	int32 NodesEnabledCalls = 0;     // `FUN_102dc3e0`
	int32 NodesDisabledCalls = 0;    // `FUN_102dc490`

	// `CAI_InterestingPlaceConverstation::InputEnable` (`0x102dccc0`): `m_bEnabled := 1`, then
	// `FUN_102dc3e0` — SEAM (story 14).
	void InputEnable(const FElysiumInputArgs& Args);

	// `InputDisable` (`0x102dcce0`): `m_bEnabled := 0`, `FUN_102dc490` — SEAM (story 14) — then
	// `ThinkSet(NULL)`.
	void InputDisable(const FElysiumInputArgs& Args);

	// `InputPlayOneOffSound` (`0x102dcd10`): `m_bPlayOneOffSound := 1`, which the talk think reads.
	void InputPlayOneOffSound(const FElysiumInputArgs& Args);

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;

	static void BuildClass(FElysiumClassDesc& D);
};
