#pragma once

#include "Substrate/ElysiumNpc.h"

// `CNPC_VMingXiaoTentacle` (primary vtable `0x104bdea4`), built by `npc_VMingXiaoTentacle` factory
// `0x1039af30`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcMingXiaoTentacle : public FElysiumNpc
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void Slot21(FElysiumEntity* Attacker) override;
	virtual void Slot22(FElysiumEntity* Attacker) override;
	virtual void Slot23(FElysiumEntity* Attacker) override;
	virtual void OnRestore(bool bFromLoad) override;
	virtual void Precache() override;
	virtual int32 Save(void* Archive) override;
	virtual int32 Restore(void* Archive) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual int32 PreSelectSchedule() override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual bool ShouldIgnoreCollision(FElysiumEntity* Other) override;
	virtual bool NavIgnoreCollision(FElysiumEntity* Other) override;
	virtual bool CanStandOn(FElysiumEntity* Other) override;
	virtual const TCHAR* GetShortConditionName(int32 ConditionId) override;
	virtual int32 GetUsedHullBits() override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
};
