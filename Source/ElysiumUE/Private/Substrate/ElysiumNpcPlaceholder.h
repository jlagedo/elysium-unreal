#pragma once

#include "Substrate/ElysiumNpc.h"

// `CNPC_VPlaceholder` (primary vtable `0x104c198c`), built by `npc_VPlaceholder` factory
// `0x103a3a20`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcPlaceholder : public FElysiumNpc
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void NPCInit() override;
	virtual int32 PreSelectSchedule() override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
};
