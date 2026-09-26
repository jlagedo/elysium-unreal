#pragma once

#include "Substrate/ElysiumNpcAnimal.h"

// `CNPC_VDog` (primary vtable `0x104b15bc`), built by `npc_VDog` factory `0x10373480`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcDog : public FElysiumNpcAnimal
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
};
