#pragma once

#include "Substrate/ElysiumNpcVampireBoss.h"

// `CNPC_VChangBros` (primary vtable `0x104adabc`), built by `npc_VChangBros` factory `0x1036a0a0`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcChangBros : public FElysiumNpcVampireBoss
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
};
