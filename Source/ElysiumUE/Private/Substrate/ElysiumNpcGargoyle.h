#pragma once

#include "Substrate/ElysiumNpcVampire.h"

// `CNPC_VGargoyle` (primary vtable `0x104b44dc`), built by `npc_VGargoyle` factory `0x103779f0`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcGargoyle : public FElysiumNpcVampire
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VGargoyle", FElysiumNpcVampire)

	// --- Select19 (story 0019/8 lane L06): the helpers `SelectSchedule` `0x103788d0` calls --------
	/** `0x10378f80` — the find-pillar gate: `FINDING_BODY` and `0x15a` `FIND_PILLAR` when a pillar is
	 *  found, else `+0x6680 = 2` and 0. */
	int32 Select19GargoyleFindPillarSchedule();
	/** SEAM for `0x10378ec0`'s `0x100f7b20("pillar", enemy origin)` nearest-entity search, which writes
	 *  `+0x667c`. No nearest-by-name search stands on the world; the miss arm writes `-1` and answers
	 *  false. */
	bool Select19GargoyleFindPillar();
	/** SEAM for `0x102ee380(m_pNavigator, from, to)`, the path test the combat arm asks. The same
	 *  navigator seam `FElysiumNpcWerewolf::WerewolfHasPath` records: no path object, answers false. */
	bool Select19GargoyleHasPath(const FVector& FromUnits, const FVector& ToUnits) const;

	// The constructor `0x10377a60`: its hull store (`docs/vtmb/data/class_hulls.json`).
	FElysiumNpcGargoyle();

	virtual bool Slot599(int32 Arg) override;
	virtual bool Slot600(FElysiumEntity* Enemy) override;
	virtual void NPCInit() override;
	virtual void TaskFail(int32 Reason) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual bool NavIgnoreCollision(FElysiumEntity* Other) override;
	virtual int32 GetUsedHullBits() override;
	virtual void OnVictimHitByMe(FElysiumEntity* Victim) override;
	virtual bool SuppressesDamageFlinch(const FElysiumDmg& Dmg) const override;
	virtual void OnScheduleChange(int32 NewSchedule) override;
	virtual void TouchSpecies(FElysiumEntity* Other) override;

	// +0x6680 m_iShunnedFindPillar (`CNPC_VGargoyle`): its own shunned-find counter, written by
	// `NPCInit` (`0x103785f0`) and `TaskFail` (`0x10379060`).
	int32 GargoyleShunnedFindPillar = 0;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcLifecycle2.inl`.
	static constexpr int32 HullIndexGargoyle = 0x0e;     // `0x103785f0`
	/** `CNPC_VGargoyle` species words at `NPCInit`. */
	FElysiumEntityHandle GargoylePillarTarget;   // +0x667c
	int32 GargoyleDoingGibDeath = 0;             // +0x6684
	int32 GargoyleCanKnockback = 0;              // +0x6688

	/** `CNPC_VGargoyle::OnVictimHitByMe` (`0x1037a450`)'s classname filter, as a pure function so the
	 *  two names it matches are assertable without an entity. `FClassnameIs` semantics: case-insensitive
	 *  and a trailing `*` is a prefix match — neither of these two carries one. */
	static bool GargoyleHitsPillar(const FString& Classname);

	// From `ElysiumNpcMotor.inl`.
	/** `CNPC_VGargoyle::NavIgnoreCollision` `0x10379490`'s classname filter, as a pure function so the
	 *  three names it matches are assertable without an entity. */
	static bool GargoyleIgnoresClassname(const FString& Classname);

	// From `ElysiumNpcSpecies.inl`.
	/** `0x10379ef0` / `0x10379f20` — `CNPC_VGargoyle`'s slots 599 and 600. */
	bool FUN_10379ef0(FElysiumEntity* Enemy);

	TArray<FGargoylePillarHit> GargoylePillarHits;
	/** `1037a38c PUSH 0x1` / `1037a385 PUSH 0x80` / `1037a383 PUSH 0xa` and `1037a3ab MOV [..],0x1`. */
	static constexpr int32 GargoylePillarDamageFamily = 1;        // EElysiumDmgFamily::Lethal
	static constexpr uint32 GargoylePillarDamageTypes = 0x80u;    // DMG_CLUB
	static constexpr int32 GargoylePillarDiceAmount = 10;
	static constexpr int32 GargoylePillarToHitSuccesses = 1;
	/** `1037a3a0 PUSH 0x3f800000` — the packet's damage scalar, **1.0**. */
	static constexpr float GargoylePillarDamageScale = 1.0f;

	// --- 0019/8 shape: forwarding overrides (replace the body, keep the declaration) ---
	virtual void Event_Killed(void* Arg0) override;
	virtual bool HandleAnimEvent(const FElysiumAnimEvent& Event) override;
	virtual void UpdatePresenceEffect() override;
	virtual bool PlayerKnockbackReaction(FElysiumEntity* Arg0, int32 Arg1) override;
	virtual int32 OnTakeDamage_Alive(void* Arg0) override;
	virtual void RunAI(bool Arg0) override;

	// --- RunAi19 (story 0019/8 lane L13a): what slot 432 `0x10378b80` calls ---------------------
	/** The reaction half of `0x10378b80` (`0x10378b90`..`0x10378bba`): a null blocker does nothing;
	 *  kind 1 takes the step-up `0x10379e80`, any other kind the obstacle handler `0x10379b40`. */
	void RunAi19GargoyleReact(FElysiumEntity* Blocker, int32 Kind);
	/** `0x10379b40` -- the obstacle handler: a `prop_dynamic` (case-insensitive `FClassnameIs`) is
	 *  BROKEN by the gargoyle (slot 266 on the prop, `CBreakableProp::Break(this)` `0x1018fb90`) and
	 *  nothing else happens; anything else takes the shared physics push with the gargoyle ConVars. */
	void RunAi19GargoyleObstacle(FElysiumEntity* Blocker);
	/** SEAM for slot 266 on a `prop_dynamic` blocker, `CBreakableProp::Break(breaker)`. The port's
	 *  break is `FElysiumProp::InputBreak` (the `OnBreak` output and the visual); the prop's own
	 *  damage, gibs and explosion are not stood here. Counted. */
	void RunAi19GargoyleBreakProp(FElysiumEntity* Prop);
	int32 RunAi19GargoylePropBreaks = 0;
	/** SEAMS for `gargoyle_obstruction_lookahead` (`0x1093af60`, "24"), `gargoyle_obstruction_scalar`
	 *  (`0x1093afa8`, "5") and `gargoyle_obstruction_z` (`0x1093b050`, "75"). */
	static float& GargoyleObstructionLookaheadConVar();
	static float& GargoyleObstructionScalarConVar();
	static float& GargoyleObstructionZConVar();
	virtual void GatherConditions() override;
	virtual int32 SpeciesSelectSchedule() override;
	virtual int32 StartTaskSlot442(void* Arg0) override;
	virtual int32 RunTaskSlot444(void* Arg0) override;

	// --- 0019/8 L04 (StartTask19 species): private helpers ---
	/** Slot 618 on this class, `0x1037a100`: the gargoyle's exertion sound (`exert_heavy_1..3`, table
	 *  `0x10639490`). `StartTask` `0x103790d0` tasks `0x36`/`0x37` reach it through `CALL [EAX+0x9a8]`. */
	void TransformationStartSlot618();
	/** SEAM for `m_pPhysicsObject` (`+0x36c`) vtable `+0xa4` (`IPhysicsObject` slot 41), the gib
	 *  impulse `StartTask` tasks `0xe9`/`0xea` apply. No port physics object; receives nothing. */
	void GargoyleGibImpulse(const FVector& VelocityUnits, const FVector& AngularUnits);
};
