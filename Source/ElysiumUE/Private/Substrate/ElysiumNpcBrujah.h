#pragma once

#include "Substrate/ElysiumNpcVampire.h"

// `CNPC_VBrujah` (primary vtable `0x104ab6cc`), built by `npc_VBrujah` factory `0x103677c0`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcBrujah : public FElysiumNpcVampire
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
};
