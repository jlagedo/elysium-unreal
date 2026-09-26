#pragma once

#include "Substrate/ElysiumNpcHumanCombatant.h"

// `CNPC_VYukie` (primary vtable `0x104d116c`), built by `npc_VYukie` factory `0x103dcf90`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcYukie : public FElysiumNpcHumanCombatant
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual bool Slot599(int32 Arg) override;
	virtual bool Slot600(FElysiumEntity* Enemy) override;
	virtual void Slot601(FElysiumEntity* Enemy) override;
	virtual bool Slot602() override;
	virtual void NPCInit() override;
	virtual int32 SelectIdealStateRetail() override;
	virtual bool FVisible(FElysiumEntity* SeenTarget, int32 Mask, FElysiumEntity* Blocker, int32 Arg4) override;
	virtual int32 IRelationType(FElysiumEntity* Candidate) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
};
