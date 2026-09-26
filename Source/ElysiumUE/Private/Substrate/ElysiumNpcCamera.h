#pragma once

#include "Substrate/ElysiumNpc.h"

// `CNPC_VCamera` (primary vtable `0x104ac294`), built by `npc_VCamera` factory `0x10367ff0`.
//
// Story 5 step 2 stands the class so the classname's factory builds the retail class and the class
// answers its own census row. Its overrides and own datamap words still sit on `FElysiumNpc` and
// move here in steps 3-4 (`docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`).
class FElysiumNpcCamera : public FElysiumNpc
{
public:
	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void Slot497() override;
	virtual void Slot506() override;
	virtual void NPCInit() override;
	virtual void StartNPC() override;
	virtual void Precache() override;
	virtual void OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual int32 PreSelectSchedule() override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual int32 PreSelectIdealStateRetail() override;
	virtual int32 GetUsedHullBits() override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual bool InitSquad() override;
	virtual bool HandleAnimEvent(const FElysiumAnimEvent& Event) override;
	virtual void TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm, void* Tolerance, void* SecondTolerance) override;
	virtual void PrescheduleThink() override;
	virtual void DeathSound() override;
	virtual void AlertSound() override;
	virtual void IdleSound() override;
	virtual void PainSound() override;
	virtual void FearSound() override;
	virtual void LostEnemySound() override;
	virtual void FoundEnemySound() override;
	virtual void SurprisedSound() override;
	virtual void TargetAcquiredSound() override;
	virtual void FleeSound() override;
	virtual void IdleAgitatedSound() override;
	virtual void ExertHvySound() override;
	virtual void ExertLightSound() override;
	virtual void RiledSound() override;
	virtual void ComfortSound() override;
	virtual void UpsetSound() override;
	virtual void TargetGiveUpSound() override;
	virtual void FloatSound() override;
	virtual void SpeakSentence(int32 SentenceIndex) override;
};
