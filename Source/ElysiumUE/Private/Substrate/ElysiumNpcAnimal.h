#pragma once

#include "Substrate/ElysiumNpc.h"

// `CNPC_VAnimal` (primary vtable `0x104a876c`), built by `npc_VAnimal` factory `0x1035eaa0`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcAnimal : public FElysiumNpc
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
};
