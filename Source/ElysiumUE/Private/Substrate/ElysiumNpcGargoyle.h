#pragma once

#include "Substrate/ElysiumNpcVampire.h"

// `CNPC_VGargoyle` (primary vtable `0x104b44dc`), built by `npc_VGargoyle` factory `0x103779f0`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcGargoyle : public FElysiumNpcVampire
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual bool Slot599(int32 Arg) override;
	virtual bool Slot600(FElysiumEntity* Enemy) override;
	virtual void NPCInit() override;
	virtual void Precache() override;
	virtual int32 SelectIdealStateRetail() override;
	virtual void TaskFail(int32 Reason) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual bool NavIgnoreCollision(FElysiumEntity* Other) override;
	virtual int32 GetUsedHullBits() override;
	virtual void OnVictimHitByMe(FElysiumEntity* Victim) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual bool SuppressesDamageFlinch(const FElysiumDmg& Dmg) const override;
	virtual void OnScheduleChange(int32 NewSchedule) override;
	virtual void TouchSpecies(FElysiumEntity* Other) override;
};
