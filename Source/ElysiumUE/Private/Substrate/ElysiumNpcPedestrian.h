#pragma once

#include "Substrate/ElysiumNpcHuman.h"

// `CNPC_VPedestrian` (primary vtable `0x104c0dc4`), built by `npc_VDialogPedestrian` factory
// `0x103a1d20`; `npc_VPedestrian` factory `0x103a1cb0`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcPedestrian : public FElysiumNpcHuman
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void NPCInit() override;
	virtual void OnRestore(bool bFromLoad) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual void BuildScheduleTestBits(FElysiumNpcConditions& InOutMask) override;
	virtual int32 IRelationType(FElysiumEntity* Candidate) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
};
