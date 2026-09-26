#pragma once

#include "Substrate/ElysiumNpcHumanCombatant.h"

// `CNPC_VGhoulCroucher` (primary vtable `0x104b50ac`), built by `npc_VGhoulCroucher` factory
// `0x1037a670`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcGhoulCroucher : public FElysiumNpcHumanCombatant
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void NPCInit() override;
	virtual void Precache() override;
	virtual void SetModel(TCHAR* ModelName) override;
	virtual void OnVictimHitByMe(FElysiumEntity* Victim) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual bool CanBeSetOnFire() override;
	virtual void StartTouchSpecies(FElysiumEntity* Other) override;
};
