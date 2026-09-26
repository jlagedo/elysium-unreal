#pragma once

#include "Substrate/ElysiumNpcHumanCombatant.h"

// `CNPC_VHunter` (primary vtable `0x104b9784`), built by `npc_VHunter` factory `0x10387f60`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcHunter : public FElysiumNpcHumanCombatant
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void NPCInit() override;
	virtual void OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual void OnSeeEntity(FElysiumEntity* Seen) override;
	virtual int32 IRelationType(FElysiumEntity* Candidate) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
};
