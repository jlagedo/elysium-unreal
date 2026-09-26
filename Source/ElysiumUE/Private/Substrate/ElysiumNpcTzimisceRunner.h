#pragma once

#include "Substrate/ElysiumNpcBaseBoss.h"

// `CNPC_VTzimisceRunner` (primary vtable `0x104cd144`), built by `npc_VTzimisceRunner` factory
// `0x103c2f30`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcTzimisceRunner : public FElysiumNpcBaseBoss
{
public:
	// The retail class this C++ class is: `OwnRetailClass`'s row and `FElysiumNpc::AsSpecies`'s key.
	static constexpr const TCHAR* RetailClassName = TEXT("CNPC_VTzimisceRunner");

	virtual const FElysiumNpcClass* OwnRetailClass() const override;
	virtual void Slot588() override;
	virtual bool Slot599(int32 Arg) override;
	virtual bool Slot600(FElysiumEntity* Enemy) override;
	virtual void Slot601(FElysiumEntity* Enemy) override;
	virtual bool Slot602() override;
	virtual void OnRestore(bool bFromLoad) override;
	virtual void Precache() override;
	virtual void SetActivity(int32 Activity) override;
	virtual int32 NPC_EarlyTranslateActivity(int32 Activity) override;
	virtual int32 SelectScheduleMeleeCombat(int32 Unused) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual int32 GetUsedHullBits() override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual bool HandleAnimEvent(const FElysiumAnimEvent& Event) override;
	virtual bool AnimFormBit() const override;
	virtual bool AllowsKnockbackBypass() override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcKernelSpecies.inl`. `+0x6671 m_bDeathNoticeProcessed` (datamap): the once-only
	// latch `CNPCMaker_Fleshpile::DeathNotice` (`0x1034c8e0`) sets on a runner it has counted.
	bool bRunnerDeathNoticeProcessed = false;

	// From `ElysiumNpcKernelLifecycle19.inl`.
	static constexpr float RunnerAttackExtentX = 50.f;
	static constexpr float RunnerAttackExtentY = 50.f;
	static constexpr float RunnerAttackExtentZ = 82.f;
	int32 Flag2Removals = 0;                 // `RemoveFlag2(4)`

	// From `ElysiumNpcKernelSpecies.inl`.
	// `CNPC_VTzimisceRunner`'s own two.
	FElysiumEntityHandle RunnerPotentialEnemy;   // +0x6678 m_hPotentialEnemy (datamap)
	/** `0x103c3960` / `0x103c39e0` / `0x103c3a70` / `0x103c3ab0` / `0x103c3fd0` —
	 *  `CNPC_VTzimisceRunner`'s slots 599, 600, 601, 602 and 588. */
	bool FUN_103c3960(FElysiumEntity* Enemy);

	// From `ElysiumNpcKernelSpeciesMisc10.inl`.
	/** `CNPC_VTzimisceRunner::NotifyChangeSizeSmall` (`0x103c3cd0`), four writes in order:
	 *  `SetHullSizeSmall(force = 1)`; the form byte `+0x6672` (family Anim10's `bTzimisceRunnerForm`);
	 *  `m_bWantsLargeHull` (`+0x5f2c`) = 0; and `+0x6674` = the engine token `DAT_1070b22c`
	 *  vtable `+0x1dc` answers for 0. The base body is a pure no-op, so this arm IS the behaviour. */
	void TzimisceRunnerNotifyChangeSizeSmall();
	/** `CNPC_VTzimisceRunner::NotifyChangeSizeNormal` (`0x103c3d10`), the inverse and its guard: re-read
	 *  the engine token and act ONLY when it differs from the one slot 335 cached, so an unchanged token
	 *  leaves the runner small and the form byte set. Edge-triggered on the engine value, not on a
	 *  request. */
	void TzimisceRunnerNotifyChangeSizeNormal();
	float RunnerHullToken = 0.f;   // +0x6674 CNPC_VTzimisceRunner (walked)
	/** SEAM for `DAT_1070b22c` vtable `+0x1dc` called with 0 — the engine-interface float the runner
	 *  latches and compares. `Tick()` is the only thing in this substrate whose value changes per frame
	 *  in the same way; this answers the world's current time so the token is stable within a frame and
	 *  differs across frames, which is the ONE property both bodies read. Named because the retail
	 *  quantity is unrecovered. */
	float RunnerHullEngineToken() const;

};
