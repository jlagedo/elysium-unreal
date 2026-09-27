#pragma once

#include "Substrate/ElysiumNpcHuman.h"

// `CNPC_VTaxiDriver` (primary vtable `0x104c8a14`), built by `npc_VTaxiDriver` factory
// `0x103b2ed0`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcTaxiDriver : public FElysiumNpcHuman
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VTaxiDriver", FElysiumNpcHuman)

	virtual void NPCInit() override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcLifecycle19.inl`.
	bool bTaxiFirstThink = false;

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual int32 StartTaskSlot442(void* Arg0) override;
	virtual int32 RunTaskSlot444(void* Arg0) override;

	// --- 0019/8 L04 (StartTask19 species): private helpers ---
	/** SEAM for `FUN_102c1400` (`0x102c1400`), the Troika dialogue upkeep `StartTask` `0x103b36d0` task
	 *  `0xb9` tests against -1. Its not-in-dialog arm (-1) is evaluated; the in-dialog upkeep is
	 *  unported and answers `m_Activity` (`+0xfec`). */
	int32 TaxiDialogUpkeep();
};
