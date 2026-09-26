#pragma once

#include "Substrate/ElysiumNpcVampireBoss.h"

// `CNPC_VSheriffMan` (primary vtable `0x104c619c`), built by `npc_VSheriffMan` factory
// `0x103ada30`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcSheriffMan : public FElysiumNpcVampireBoss
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
};
