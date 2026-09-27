#pragma once

#include "Substrate/ElysiumNpcChangBros.h"

// `CNPC_VChangBrosBlade` (primary vtable `0x104ae68c`), built by `npc_VChangBrosBlade` factory
// `0x1036e990`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcChangBrosBlade : public FElysiumNpcChangBros
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VChangBrosBlade", FElysiumNpcChangBros)

	virtual void NPCInit() override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

};
