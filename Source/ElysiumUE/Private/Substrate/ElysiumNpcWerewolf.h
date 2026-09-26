#pragma once

#include "Substrate/ElysiumNpcBaseBoss.h"

// `CNPC_VWerewolf` (primary vtable `0x104cf4d4`), built by `npc_VWerewolf` factory `0x103c8760`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcWerewolf : public FElysiumNpcBaseBoss
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void NPCInit() override;
	virtual void OnRestore(bool bFromLoad) override;
	virtual void Precache() override;
	virtual int32 SelectIdealStateRetail() override;
	virtual bool FVisible(FElysiumEntity* SeenTarget, int32 Mask, FElysiumEntity* Blocker, int32 Arg4) override;
	virtual void TaskFail(int32 Reason) override;
	virtual void GiveBaseFightingItems() override;
	virtual void RemoveBaseFightingItems() override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual float MaxYawSpeed() override;
	virtual bool ShouldIgnoreCollision(FElysiumEntity* Other) override;
	virtual bool NavIgnoreCollision(FElysiumEntity* Other) override;
	// Slot 620 (`0x103d5050`): introduced here; no Troika-line body holds the slot.
	virtual void DrawBBoxOverlay();
	virtual const TCHAR* GetShortConditionName(int32 ConditionId) override;
	virtual void DrawDebugStatOverlays() override;
	virtual void OnChangeActivity(int32 Activity) override;
	virtual int32 GetUsedHullBits() override;
	virtual bool FValidateHintType(void* Hint) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual void TraceAttack(void* InInfo, const FVector& DirUnits, void* InTrace) override;
	virtual void TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm, void* Tolerance, void* SecondTolerance) override;
	virtual void OnScheduleChange(int32 NewSchedule) override;
	virtual void PainSound() override;
	virtual void ExertHvySound() override;
	virtual void GatherAttackConditions(FElysiumEntity* Enemy, float DistanceUnits) override;
};
