#pragma once

#include "Substrate/ElysiumNpc.h"

// `CNPC_VHuman` (primary vtable `0x104b742c`), built by `npc_VHuman` factory `0x10383fa0`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcHuman : public FElysiumNpc
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual int32 CanPlaySequence(bool bDisregardState, int32 InterruptLevel) override;
	virtual bool Slot599(int32 Arg) override;
	virtual bool Slot600(FElysiumEntity* Enemy) override;
	virtual void Slot601(FElysiumEntity* Enemy) override;
	virtual bool Slot602() override;
	virtual int32 NPC_EarlyTranslateActivity(int32 Activity) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual int32 SelectScheduleMeleeCombat(int32 Unused) override;
	virtual int32 SelectScheduleRangedCombat(int32 Arg) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual void TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm, void* Tolerance, void* SecondTolerance) override;
	virtual bool HandleInteraction(int32 Interaction, void* Data, FElysiumEntity* Other) override;
};
