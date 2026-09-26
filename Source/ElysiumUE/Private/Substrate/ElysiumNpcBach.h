#pragma once

#include "Substrate/ElysiumNpcVampire.h"

// `CNPC_VBach` (primary vtable `0x104a9f2c`), built by `npc_VBach` factory `0x10362ba0`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcBach : public FElysiumNpcVampire
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual int32 Slot606(int32 Arg) override;
	virtual void* Slot609(bool bForce) override;
	virtual void NPCInit() override;
	virtual void Precache() override;
	virtual void OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual int32 SelectScheduleMeleeCombat(int32 Unused) override;
	virtual int32 SelectScheduleRangedCombat(int32 Arg) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual bool FValidateHintType(void* Hint) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual void GatherAttackConditions(FElysiumEntity* Enemy, float DistanceUnits) override;
};
