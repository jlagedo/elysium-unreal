#pragma once

#include "Substrate/ElysiumNpcVampireBoss.h"

// `CNPC_VSabbatLeader` (primary vtable `0x104c3d64`), built by `npc_VSabbatLeader` factory
// `0x103a5ae0`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcSabbatLeader : public FElysiumNpcVampireBoss
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void NPCInit() override;
	virtual void Precache() override;
	virtual int32 SelectIdealStateRetail() override;
	virtual int32 SelectScheduleMeleeCombat(int32 Unused) override;
	virtual void TaskFail(int32 Reason) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual void OnVictimHitByMe(FElysiumEntity* Victim) override;
	virtual bool OkToInterruptForMelee() override;
	virtual bool FValidateHintType(void* Hint) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	// Slot 620 (`0x103aa5e0`): introduced here.
	virtual void FootstepSound();
	// Slot 621 (`0x103aa7a0`): introduced here.
	virtual void AttackSound();
	virtual int32 Restore(void* Archive) override;
	virtual bool HandleInteraction(int32 Interaction, void* Data, FElysiumEntity* Other) override;
};
