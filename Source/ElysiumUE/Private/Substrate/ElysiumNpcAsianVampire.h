#pragma once

#include "Substrate/ElysiumNpcVampireBoss.h"

// `CNPC_VAsianVampire` (primary vtable `0x104a935c`), built by `npc_VAsianVampire` factory
// `0x103602f0`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcAsianVampire : public FElysiumNpcVampireBoss
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void NPCInit() override;
	virtual void Precache() override;
	virtual int32 SelectIdealStateRetail() override;
	virtual int32 SelectScheduleMeleeCombat(int32 Unused) override;
	virtual void TaskFail(int32 Reason) override;
	virtual int32 SelectScheduleRangedCombat(int32 Arg) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual bool FValidateHintType(void* Hint) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual int32 Restore(void* Archive) override;
};
