#pragma once

#include "Substrate/ElysiumNpcBaseBoss.h"

// `CNPC_VMingXiao` (primary vtable `0x104bc704`), built by `npc_VMingXiao` factory `0x10390e70`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcMingXiao : public FElysiumNpcBaseBoss
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual int32 CanPlaySequence(bool bDisregardState, int32 InterruptLevel) override;
	virtual void Precache() override;
	virtual int32 Save(void* Archive) override;
	virtual int32 Restore(void* Archive) override;
	virtual void UpdateOnRemove() override;
	virtual int32 SelectIdealStateRetail() override;
	virtual FVector GetShootEnemyDir(const FVector& ShootPositionCm, int32 A, int32 B) override;
	virtual int32 SelectScheduleMeleeCombat(int32 Unused) override;
	virtual float ResolveTaskDistance(float Distance) override;
	virtual void TaskFail(int32 Reason) override;
	virtual int32 HealthToPercent() override;
	virtual int32 SelectScheduleRangedCombat(int32 Arg) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual float MaxYawSpeed() override;
	virtual bool NavIgnoreCollision(FElysiumEntity* Other) override;
	virtual void DrawDebugGeometryOverlays() override;
	virtual const TCHAR* GetShortConditionName(int32 ConditionId) override;
	virtual void OnChangeActivity(int32 Activity) override;
	virtual int32 GetUsedHullBits() override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual bool HandleAnimEvent(const FElysiumAnimEvent& Event) override;
	virtual void TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm, void* Tolerance, void* SecondTolerance) override;
};
