#pragma once

#include "Substrate/ElysiumNpcHumanCombatant.h"

// `CNPC_VHumanCombatPatrol` (primary vtable `0x104b8bbc`), built by `npc_VHumanCombatPatrol`
// factory `0x10387660`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcHumanCombatPatrol : public FElysiumNpcHumanCombatant
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VHumanCombatPatrol", FElysiumNpcHumanCombatant)

	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual int32 SpeciesSelectSchedule() override;
};
