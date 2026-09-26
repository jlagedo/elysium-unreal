#pragma once

#include "Substrate/ElysiumNpcChangBros.h"

// `CNPC_VChangBrosBlade` (primary vtable `0x104ae68c`), built by `npc_VChangBrosBlade` factory
// `0x1036e990`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcChangBrosBlade : public FElysiumNpcChangBros
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
};
