#pragma once

#include "Substrate/ElysiumNpcHuman.h"

// `CNPC_VHumanCombatant` (primary vtable `0x104b7ff4`), built by `npc_VHumanCombatant` factory
// `0x10386a30`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcHumanCombatant : public FElysiumNpcHuman
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void NPCInit() override;
	virtual void OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual void BuildScheduleTestBits(FElysiumNpcConditions& InOutMask) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
};
