#pragma once

#include "Substrate/ElysiumNpcBaseBoss.h"

// `CNPC_VTzimisceRunner` (primary vtable `0x104cd144`), built by `npc_VTzimisceRunner` factory
// `0x103c2f30`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcTzimisceRunner : public FElysiumNpcBaseBoss
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
};
