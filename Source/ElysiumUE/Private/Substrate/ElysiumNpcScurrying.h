#pragma once

#include "Substrate/ElysiumNpcAnimal.h"

// `CNPC_VScurrying` (primary vtable `0x104c4944`), built by `npc_VScurrying` factory `0x103aba00`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcScurrying : public FElysiumNpcAnimal
{
public:
	// The retail class this C++ class is: `OwnRetailClass`'s row and `FElysiumNpc::AsSpecies`'s key.
	static constexpr const TCHAR* RetailClassName = TEXT("CNPC_VScurrying");

	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual int32 GetUsedHullBits() override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;

	// Own datamap words (`CNPC_VScurrying`): the fright pair. No retail constructor writes them;
	// `StartTask` (`0x103ac740`, TASK 0x14a) reads the distance and `SelectSchedule` (`0x103ac610`)
	// the duration, both story-8 residue.
	float ScurryingFrightDistanceUnits = 0.f;   // +0x6698 m_flFrightDistance, KEY fright_distance
	// +0x669c m_flFrightDurationSeconds, KEY fright_duration: FIELD_TIME in the datamap, authored
	// in seconds.
	float ScurryingFrightDurationSeconds = 0.f;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcSenses10.inl`.
	/** `CNPC_VScurrying::m_flDetectionDistance` (`+0x6690`), `m_fIgnoreNosferatu` (`+0x6694`) and
	 *  `m_fMustDetect` (`+0x6695`) — shape rows with no port producer until now. They are this family's
	 *  words because `ScurryingShouldDetect` is their only reader. SOURCE units on the distance, as the
	 *  authored keyvalue is. */
	float ScurryingDetectionDistanceUnits = 0.f;   // +0x6690
	bool bScurryingIgnoreNosferatu = false;        // +0x6694
	bool bScurryingMustDetect = false;             // +0x6695
	/** `0x103acac0` — the Scurrying detection test. Four arms in retail's order; see the definition. */
	bool ScurryingShouldDetect(const FElysiumEntity* Target) const;
	/** `0x103ad0f0` — "is this target's character template `Player_Nosferatu`". The port's sheet carries
	 *  the clan on the player, so this is the whole recovered rule for a player target and answers false
	 *  for everything else, which is retail's. */
	static bool IsNosferatuTemplate(const FElysiumEntity& Target);
	/** `0x103ad0a0` — the must-detect gate: true for any target that is NOT a player, and for a player
	 *  only while `COND_SEE_PLAYER` (`0x5a`) or COND `0x6f` stands. */
	bool ScurryingMustDetectAdmits(const FElysiumEntity& Target) const;
	/** `0x103acba0` — the Scurrying flee-destination search. Two halves: a node jitter when the
	 *  navigator finds a node within 30000 units, and a hull-traced march away from the threat when it
	 *  does not. **SEAM**: no node graph stands here, so the node search always fails and the MARCH is
	 *  the arm taken — which is retail's own answer for a map with no AI network, and it is the arm
	 *  that still produces a destination. `OutCm` is written only on success, exactly as retail writes
	 *  its out-vector only when one was passed. */
	bool ScurryingFindFleeDestination(const FVector& ThreatPosCm, float DistanceUnits,
		FVector* OutDestinationCm);

};
