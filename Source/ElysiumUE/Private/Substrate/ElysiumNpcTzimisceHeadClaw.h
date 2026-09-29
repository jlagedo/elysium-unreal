#pragma once

#include "Substrate/ElysiumNpcBaseBoss.h"

// `CNPC_VTzimisceHeadClaw` (primary vtable `0x104cc564`), built by `npc_VTzimisceHeadClaw` factory
// `0x103c11b0`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcTzimisceHeadClaw : public FElysiumNpcBaseBoss
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VTzimisceHeadClaw", FElysiumNpcBaseBoss)

	// The constructor `0x103c1220`: its hull store (`docs/vtmb/data/class_hulls.json`).
	FElysiumNpcTzimisceHeadClaw();

	virtual bool Slot599(int32 Arg) override;
	virtual bool Slot600(FElysiumEntity* Enemy) override;
	virtual void Slot601(FElysiumEntity* Enemy) override;
	virtual bool Slot602() override;
	virtual void NPCInit() override;
	virtual void SetActivity(int32 Activity) override;
	virtual void BuildScheduleTestBits(FElysiumNpcConditions& InOutMask) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual int32 GetUsedHullBits() override;
	virtual void Slot332(FElysiumEntity* SlowTarget) override;
	virtual bool HandleAnimEvent(const FElysiumAnimEvent& Event) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcSpecies.inl`.
	// `CNPC_VTzimisceHeadClaw`'s slow stamp. `+0x6678` is `CNPC_VTzimisceRunner::m_hPotentialEnemy`,
	// `CNPC_VZombie::m_iZombieAIType` and `CNPC_VManBat::m_flFlapTimer` on three other species.
	double HeadClawSlowedExpire = 0.0;   // +0x6678 CNPC_VTzimisceHeadClaw::m_flSlowedExpire (datamap)
	/** `0x103c19e0` / `0x103c1a60` — `CNPC_VTzimisceHeadClaw`'s slots 599 and 600. */
	bool FUN_103c19e0(FElysiumEntity* Enemy);
	/** `0x103c24a0` — `CNPC_VTzimisceHeadClaw`: is the slow window armed? */
	bool FUN_103c24a0() const;

	/** `CNPC_VTzimisceHeadClaw::EndSlow` (`0x103c2230`). Gate `0x103c24a0`: act only while
	 *  `m_flSlowedExpire` is above 0.0, then only when the caller forces it or the timer has reached
	 *  curtime. Zero the expiry; and ONLY when the slowed entity still resolves with a combat view,
	 *  `EndSlowEntity(victim, 500.0)`, clear the handle and emit `…/Sluge_Affected.wav` on channel 3
	 *  from the VICTIM's slot-222 origin at attenuation 0.8. Then `UTIL_Remove` the two owned effects at
	 *  `+0x667c` and `+0x6680`, each only while its handle resolves, setting both to -1. */
	void TzimisceHeadClawEndSlow(bool bForce);
	bool HeadClawSlowRunning() const;   // `0x103c24a0`
	FElysiumEntityHandle HeadClawSlowedEntity;    // +0x6674 `m_hSlowedEntity`
	FElysiumEntityHandle HeadClawPlayerEmitter;   // +0x667c `Tzim2_player_emitter`
	FElysiumEntityHandle HeadClawHudEmitter;      // +0x6680 `HUD_Tzim2_emitter`

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Spawn() override;
	virtual void Event_Killed(void* Arg0) override;
	virtual void RunAI(bool Arg0) override;
	virtual void GatherConditions() override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual int32 StartTaskSlot442(void* Arg0) override;

	// --- 0019/8 L04 (StartTask19 species): private helpers ---
	/** Slot 618 on this class, `0x103c24d0` (retail's `FUN_103c24d0`): the fat guy's exertion sound
	 *  (`TC_FatGuy/Exert_Heavy_1..3`, table `0x1065ca78`, `RandomInt(0, 2)`, channel 4, volume 1.0,
	 *  attenuation 0.8). `StartTask` `0x103c1820` reaches it through `CALL [EAX+0x9a8]`; the class has
	 *  no subclass, so the dispatch lands here. Not a virtual: slot 618 has no port declaration on the
	 *  boss line. */
	void TransformationStartSlot618();
};
