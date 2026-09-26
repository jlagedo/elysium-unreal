#pragma once

#include "Substrate/ElysiumNpcHumanCombatant.h"

// `CNPC_VHumanCombatPatrol` (primary vtable `0x104b8bbc`), built by `npc_VHumanCombatPatrol`
// factory `0x10387660`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcHumanCombatPatrol : public FElysiumNpcHumanCombatant
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
};
