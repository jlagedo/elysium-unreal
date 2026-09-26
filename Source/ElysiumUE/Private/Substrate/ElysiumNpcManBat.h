#pragma once

#include "Substrate/ElysiumNpcVampire.h"

// `CNPC_VManBat` (primary vtable `0x104bbaec`), built by `npc_VManBat` factory `0x10389c50`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcManBat : public FElysiumNpcVampire
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void NPCInit() override;
	virtual void Precache() override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual int32 GetUsedHullBits() override;
	virtual bool FValidateHintType(void* Hint) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
};
