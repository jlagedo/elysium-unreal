#pragma once

#include "Substrate/ElysiumNpcAnimal.h"

// `CNPC_VZombie` (primary vtable `0x104d1d3c`), built by `npc_VZombie` factory `0x103ddd70`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcZombie : public FElysiumNpcAnimal
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void Slot25(FElysiumEntity* Victim) override;
	virtual void Slot26(FElysiumEntity* Victim) override;
	virtual bool ShouldPlayFloatSound() override;
	virtual void NPCInit() override;
	virtual void Precache() override;
	virtual void SetModel(TCHAR* ModelName) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual bool FVisible(FElysiumEntity* SeenTarget, int32 Mask, FElysiumEntity* Blocker, int32 Arg4) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual int32 DrawDebugTextOverlays() override;
	virtual void OnVictimHitByMe(FElysiumEntity* Victim) override;
	virtual bool FValidateHintType(void* Hint) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual bool ShouldPlayIdleSound() override;
	virtual void TraceAttack(void* InInfo, const FVector& DirUnits, void* InTrace) override;
};
