#pragma once

#include "Substrate/ElysiumNpcBaseBoss.h"

// `CNPC_VTzimisceHeadClaw` (primary vtable `0x104cc564`), built by `npc_VTzimisceHeadClaw` factory
// `0x103c11b0`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcTzimisceHeadClaw : public FElysiumNpcBaseBoss
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual bool Slot599(int32 Arg) override;
	virtual bool Slot600(FElysiumEntity* Enemy) override;
	virtual void Slot601(FElysiumEntity* Enemy) override;
	virtual bool Slot602() override;
	virtual void NPCInit() override;
	virtual void Precache() override;
	virtual int32 Save(void* Archive) override;
	virtual int32 Restore(void* Archive) override;
	virtual void SetActivity(int32 Activity) override;
	virtual void BuildScheduleTestBits(FElysiumNpcConditions& InOutMask) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual int32 GetUsedHullBits() override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual void Slot332(FElysiumEntity* SlowTarget) override;
	virtual bool HandleAnimEvent(const FElysiumAnimEvent& Event) override;
};
