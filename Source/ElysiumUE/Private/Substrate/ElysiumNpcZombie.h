#pragma once

#include "Substrate/ElysiumNpcAnimal.h"

// `CNPC_VZombie` (primary vtable `0x104d1d3c`), built by `npc_VZombie` factory `0x103ddd70`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcZombie : public FElysiumNpcAnimal
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
};
