#pragma once

#include "Substrate/ElysiumNpc.h"

// `CNPC_VMingXiaoTentacle` (primary vtable `0x104bdea4`), built by `npc_VMingXiaoTentacle` factory
// `0x1039af30`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcMingXiaoTentacle : public FElysiumNpc
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
};
