#pragma once

#include "Substrate/ElysiumNpcVampire.h"

// `CNPC_VHengeyokai` (primary vtable `0x104b683c`), built by `npc_VHengeyokai` factory
// `0x1037e610`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcHengeyokai : public FElysiumNpcVampire
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void NPCInit() override;
	virtual void Precache() override;
	virtual int32 NPC_EarlyTranslateActivity(int32 Activity) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual void TaskFail(int32 Reason) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual bool NavIgnoreCollision(FElysiumEntity* Other) override;
	virtual int32 DrawDebugTextOverlays() override;
	virtual int32 GetUsedHullBits() override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual bool HandleAnimEvent(const FElysiumAnimEvent& Event) override;
	virtual bool SuppressesDamageFlinch(const FElysiumDmg& Dmg) const override;
	virtual void OnScheduleChange(int32 NewSchedule) override;
};
