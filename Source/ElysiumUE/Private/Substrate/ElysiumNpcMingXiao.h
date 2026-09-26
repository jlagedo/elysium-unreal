#pragma once

#include "Substrate/ElysiumNpcBaseBoss.h"

// `CNPC_VMingXiao` (primary vtable `0x104bc704`), built by `npc_VMingXiao` factory `0x10390e70`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcMingXiao : public FElysiumNpcBaseBoss
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
};
