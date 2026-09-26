#pragma once

#include "Substrate/ElysiumNpcBaseBoss.h"

// `CNPC_VTzimisce` (primary vtable `0x104cb934`), built by `npc_VTzimisce` factory `0x103b6bf0`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcTzimisce : public FElysiumNpcBaseBoss
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual int32 CanPlaySequence(bool bDisregardState, int32 InterruptLevel) override;
	virtual void DeathSound() override;
	virtual void Slot593() override;
	virtual void NPCInit() override;
	virtual void StartNPC() override;
	virtual void Precache() override;
	virtual int32 NPC_EarlyTranslateActivity(int32 Activity) override;
	virtual void OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual bool FVisible(FElysiumEntity* SeenTarget, int32 Mask, FElysiumEntity* Blocker, int32 Arg4) override;
	virtual float ResolveTaskDistance(float Distance) override;
	virtual void TaskFail(int32 Reason) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual float MaxYawSpeed() override;
	virtual bool NavIgnoreCollision(FElysiumEntity* Other) override;
	virtual int32 DrawDebugTextOverlays() override;
	virtual int32 GetUsedHullBits() override;
	virtual bool FValidateHintType(void* Hint) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual void TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm, void* Tolerance, void* SecondTolerance) override;
	virtual void OnScheduleChange(int32 NewSchedule) override;
	virtual void JustMadeSound() override;
	virtual void IdleSound() override;
	virtual void PainSound() override;
};
