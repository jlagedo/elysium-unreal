#pragma once

#include "Substrate/ElysiumNpc.h"

// `CNPC_VCamera` (primary vtable `0x104ac294`), built by `npc_VCamera` factory `0x10367ff0`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcCamera : public FElysiumNpc
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VCamera", FElysiumNpc)

	// The constructor `0x10368060`: its hull store (`docs/vtmb/data/class_hulls.json`).
	FElysiumNpcCamera();

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

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcLifecycle.inl`.
	/** `CNPC_VCamera::Precache` (`0x103689c0`), shared with `CNPC_VCameraSecurity` — fall the model key
	 *  back to `models/null.mdl` when it is unset or empty, precache it, then run the link-table
	 *  integrity check. Answers the model that was precached; `OutLinkWarning` is retail's
	 *  "spawned after links have been..." arm. */
	static FString CameraPrecacheModel(const FString& AuthoredModel);

	// From `ElysiumNpcLifecycle19.inl`.
	static constexpr float CameraOccludedDelayNormal = 3.4f;
	static constexpr float CameraOccludedDelayCover = 10.f;
	static constexpr float CameraEnemyStoreInterval = 0.5f;
	/** Camera engine-query seam: slot 74 of `DAT_1070ba0c`. Default admits. Tests may refuse. */
	bool bCameraEngineQueryAnswer = true;
	int32 CameraEngineQueries = 0;
	int32 CameraSelfRemovals = 0;

};
