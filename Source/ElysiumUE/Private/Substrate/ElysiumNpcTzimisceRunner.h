#pragma once

#include "Substrate/ElysiumNpcBaseBoss.h"

// `CNPC_VTzimisceRunner` (primary vtable `0x104cd144`), built by `npc_VTzimisceRunner` factory
// `0x103c2f30`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcTzimisceRunner : public FElysiumNpcBaseBoss
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void Slot588() override;
	virtual bool Slot599(int32 Arg) override;
	virtual bool Slot600(FElysiumEntity* Enemy) override;
	virtual void Slot601(FElysiumEntity* Enemy) override;
	virtual bool Slot602() override;
	virtual void OnRestore(bool bFromLoad) override;
	virtual void Precache() override;
	virtual void SetActivity(int32 Activity) override;
	virtual int32 NPC_EarlyTranslateActivity(int32 Activity) override;
	virtual int32 SelectScheduleMeleeCombat(int32 Unused) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual int32 GetUsedHullBits() override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual bool HandleAnimEvent(const FElysiumAnimEvent& Event) override;
	virtual bool AnimFormBit() const override;
	virtual bool AllowsKnockbackBypass() override;
};
