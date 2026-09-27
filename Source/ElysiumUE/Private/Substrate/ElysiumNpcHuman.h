#pragma once

#include "Substrate/ElysiumNpc.h"

// `CNPC_VHuman` (primary vtable `0x104b742c`), built by `npc_VHuman` factory `0x10383fa0`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcHuman : public FElysiumNpc
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VHuman", FElysiumNpc)

	virtual int32 CanPlaySequence(bool bDisregardState, int32 InterruptLevel) override;
	virtual bool Slot599(int32 Arg) override;
	virtual bool Slot600(FElysiumEntity* Enemy) override;
	virtual void Slot601(FElysiumEntity* Enemy) override;
	virtual bool Slot602() override;
	virtual int32 NPC_EarlyTranslateActivity(int32 Activity) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual int32 SelectScheduleMeleeCombat(int32 Unused) override;
	virtual int32 SelectScheduleRangedCombat(int32 Arg) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual void TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm, void* Tolerance, void* SecondTolerance) override;
	virtual bool HandleInteraction(int32 Interaction, void* Data, FElysiumEntity* Other) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcAnim10.inl`.
	/** `CNPC_VHuman::NPC_EarlyTranslateActivity` (`0x103854f0`), 565 bytes and 39 census classes — the
	 *  armed/alert decision tree, then the rewrites. See the definition for the arm-by-arm walk; the one
	 *  thing to carry here is that `m_bAggressiveAnims` (`+0x6410`) is written on FOUR different paths
	 *  and every later rewrite reads it. */
	int32 HumanNpcEarlyTranslateActivity(int32 Activity);
	/** The active weapon's `+0x19c` word, whose bit `0x40 NODRAW` makes `0x103854f0` treat an armed NPC
	 *  as unarmed. **SEAM**: no port member carries the weapon's draw flags — the port's weapon state is
	 *  the item's own catalogue row — so this answers 0, the arm in which the weapon DOES draw and the
	 *  decision tree runs. Answering `0x40` instead would clear `m_bAggressiveAnims` for every armed
	 *  body and make the whole tree unreachable. */
	uint32 ActiveWeaponDrawFlags() const;
	/** `CNPC_VHuman::SelectScheduleMeleeCombat` (`0x10385e40`), 1,449 bytes, slot 604 for **34** census
	 *  classes — the single most-inherited melee selector in the game. It REPLACES the Troika body
	 *  `0x102b6c30` wholesale and never chains it. Every return also stamps the selector trace
	 *  (`+0x1b30` the source file, `+0x1b34` the line), which this runtime records through
	 *  `RecordScheduleEvent`. */
	int32 SelectScheduleMeleeCombatHuman();

	// From `ElysiumNpcBosses.inl`.
	/** `0x10385cf0` — slot 601's `CNPC_VAndreiBlood`-line body (38 species). It differs from the
	 *  Troika line's `0x102b5880` in two recovered ways, and both are ported: it fires the global
	 *  melee-left event `DAT_10924edc+4` FIRST, and it forwards to the coordinator WITHOUT the
	 *  `m_pAttackCoordinator != 0` guard the Troika body puts in front of it. */
	void FUN_10385cf0();

	// From `ElysiumNpcCombat10.inl`.
	/** `CNPC_VHuman::SelectScheduleRangedCombat` (`0x10386560`), 802 bytes — the shared human arm that
	 *  fills 36 species `#605` slots and no Troika slot. It REPLACES the Troika body wholesale and never
	 *  chains it. */
	int32 HumanSelectScheduleRangedCombat(int32 Arg);

	// From `ElysiumNpcLifecycle19.inl`.
	int32 HideActiveWeaponCalls = 0;         // slot 66 Hide on the ACTIVE WEAPON
	void HideActiveWeaponIfAny();

	// From `ElysiumNpcSpeciesLifecycle10.inl`.
	/** How many times either relationship write above was made FROM this family's slot-463 pre-step. */
	int32 PlayerHateRelationshipSets = 0;

	// From `ElysiumNpc.h`.
	// The holster/draw switch of the slot-463 bodies that carry one (`CNPC_VHumanCombatant`
	// `0x103871c0` and `CNPC_VGuard1`'s copy `0x1037d020`), taking the NEW state exactly as retail's
	// second argument does. Only those classes' `OnStateChange` overrides call it (story 5 step 3);
	// public so a fixture can state a transition without driving the whole decision pass.
	void ApplyStateWeaponVisibility(EElysiumNpcState NewState);
};
