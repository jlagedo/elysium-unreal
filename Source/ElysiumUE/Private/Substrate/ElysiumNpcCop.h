#pragma once

#include "Substrate/ElysiumNpcHumanCombatant.h"

// `CNPC_VCop` (primary vtable `0x104b09f4`), built by `npc_VCop` factory `0x103704f0`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcCop : public FElysiumNpcHumanCombatant
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
};
