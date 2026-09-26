#pragma once

#include "Substrate/ElysiumNpcBaseBoss.h"

// `CNPC_VTzimisceHeadClaw` (primary vtable `0x104cc564`), built by `npc_VTzimisceHeadClaw` factory
// `0x103c11b0`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcTzimisceHeadClaw : public FElysiumNpcBaseBoss
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
};
