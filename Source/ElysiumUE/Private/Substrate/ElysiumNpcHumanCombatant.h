#pragma once

#include "Substrate/ElysiumNpcHuman.h"

// `CNPC_VHumanCombatant` (primary vtable `0x104b7ff4`), built by `npc_VHumanCombatant` factory
// `0x10386a30`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcHumanCombatant : public FElysiumNpcHuman
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VHumanCombatant", FElysiumNpcHuman)

	virtual void NPCInit() override;
	virtual void OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual void BuildScheduleTestBits(FElysiumNpcConditions& InOutMask) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcLifecycle2.inl`.
	void HumanCombatantNPCInit();       // `0x10387140` — helper for Cop / Hunter / Yukie

	// From `ElysiumNpcSenses10.inl`.
	/** The four class-static cells the two arms write and three other bodies read: `DAT_1093ac3c` /
	 *  `_DAT_1093aca8` (the cop's shared provoker handle and its expiry) and `DAT_1093b650` /
	 *  `_DAT_1093b658` (the hunter's). They are STATIC IN RETAIL — every cop in the map shares one
	 *  grudge — so they are file statics here too, reached through these accessors rather than copied
	 *  per NPC. Family **Debug10**'s `CopSuspectIs` and family **Conditions10**'s two slot-404 species
	 *  arms are the readers; this family is the writer. The window is 30 seconds. */
	static constexpr float SpeciesSuspectWindowSeconds = ElysiumNpcTunables::Thirty;

	// From `ElysiumNpcState.inl`.
	/** Map name `gpGlobals->mapname` as the 12-byte compare in `0x10387380` reads it. Tests set
	 *  `World->MapName()`; this override is for a fixture that has no world map name. */
	FString SelectIdealStateMapNameOverride;
	/** `0x103871c0` (shared holster/draw) call count from Cop's OnStateChange tails. */
	int32 CopHolsterDrawCalls = 0;
	int32 HumanCombatPatrolSelectIdealState();
	/** `CNPC_VHumanCombatant::OnStateChange` (`0x103871c0`)'s weapon half, the tail both of Cop's
	 *  arms chain. Unconditional — it carries no census class list. */
	void CopHumanCombatantOnStateChange(int32 NewRetail);

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Spawn() override;
	virtual int32 SpeciesSelectSchedule() override;
};
