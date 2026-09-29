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

	virtual void NPCInit() override;
	virtual void StartNPC() override;
	virtual void Precache() override;
	virtual void OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual int32 PreSelectSchedule() override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual int32 PreSelectIdealStateRetail() override;
	virtual int32 GetUsedHullBits() override;
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
	virtual void FleeSound() override;
	virtual void IdleAgitatedSound() override;
	virtual void ComfortSound() override;
	virtual void UpsetSound() override;
	virtual void FloatSound() override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcLifecycle.inl`.
	/** `CNPC_VCamera::Precache` (`0x103689c0`)'s model fallback, shared with `CNPC_VCameraSecurity`:
	 *  the model key falls back to `models/null.mdl` when it is unset or empty. */
	static FString CameraPrecacheModel(const FString& AuthoredModel);

	// From `ElysiumNpcLifecycle2.inl`.
	static constexpr float CameraOccludedDelayNormal = 3.4f;
	static constexpr float CameraOccludedDelayCover = 10.f;
	static constexpr float CameraEnemyStoreInterval = 0.5f;
	/** Slot 74 of `DAT_1070ba0c` (`g_pGameRules->FAllowNPCs()`, `0x103692f8`) is the base's
	 *  `Spawn19GameRulesAllowNpcs` seam (`ElysiumNpcBaseSpawn.inl`), the same call `0x10273272`
	 *  makes; the camera's own answer word was folded into it. Counted here. */
	int32 CameraEngineQueries = 0;
	int32 CameraSelfRemovals = 0;

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Spawn() override;
	virtual void NPCThink() override;
};
