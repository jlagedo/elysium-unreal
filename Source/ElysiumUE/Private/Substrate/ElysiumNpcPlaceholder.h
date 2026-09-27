#pragma once

#include "Substrate/ElysiumNpc.h"

// `CNPC_VPlaceholder` (primary vtable `0x104c198c`), built by `npc_VPlaceholder` factory
// `0x103a3a20`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcPlaceholder : public FElysiumNpc
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VPlaceholder", FElysiumNpc)

	virtual void NPCInit() override;
	virtual int32 PreSelectSchedule() override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
};
