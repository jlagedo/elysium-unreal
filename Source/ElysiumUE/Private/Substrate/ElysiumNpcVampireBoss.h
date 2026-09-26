#pragma once

#include "Substrate/ElysiumNpcVampire.h"

// `CNPC_VVampireBoss` (primary vtable `0x104a7b94`), built by `npc_VVampireBoss` factory
// `0x103c4fa0`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcVampireBoss : public FElysiumNpcVampire
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void NPCInit() override;
	virtual int32 Restore(void* Archive) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
};
