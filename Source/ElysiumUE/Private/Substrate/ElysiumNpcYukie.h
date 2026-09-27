#pragma once

#include "Substrate/ElysiumNpcHumanCombatant.h"

// `CNPC_VYukie` (primary vtable `0x104d116c`), built by `npc_VYukie` factory `0x103dcf90`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcYukie : public FElysiumNpcHumanCombatant
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VYukie", FElysiumNpcHumanCombatant)

	virtual bool Slot599(int32 Arg) override;
	virtual bool Slot600(FElysiumEntity* Enemy) override;
	virtual void Slot601(FElysiumEntity* Enemy) override;
	virtual bool Slot602() override;
	virtual void NPCInit() override;
	virtual int32 SelectIdealStateRetail() override;
	virtual bool FVisible(FElysiumEntity* SeenTarget, int32 Mask, FElysiumEntity* Blocker, int32 Arg4) override;
	virtual int32 IRelationType(FElysiumEntity* Candidate) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	// From `ElysiumNpcMisc.inl`.
	/** `CNPC_VYukie::vfunc599` (`0x103dd8b0`) — the species body at slot 599, `EnterMelee`. Unlike the
	 *  Troika line (family **TroikaHelpers**' `Slot599`) it has NO gates at all: the global melee event
	 *  fires, `m_bInMelee` goes true and `m_flMeleeMustLeaveTimer` takes
	 *  `curtime + RandomFloat(22.5, 45.0)`. 29c's target named the data member `MeleeMustLeaveTimer`;
	 *  renamed here because that member is 29b's and this is the body that writes it. */
	bool YukieEnterMelee();
	/** `0x103dd9a0` — the species body at slot 601 for `CNPC_VYukie`, `LeaveMelee`. The Troika line's
	 *  `0x102b5880` without the attack-coordinator release. Renamed from 29c's `MeleeCanEnterTimer` for
	 *  the same reason as above. */
	void YukieLeaveMelee();

	// From `ElysiumNpcSensesBodies.inl`.
	/** `0x103ddaa0`, `CNPC_VYukie#363` — `FInViewCone(CBaseEntity*)`. The gate and nothing else: Yukie
	 *  has no view cone. */
	bool YukieFInViewCone(const FElysiumEntity* Candidate) const;
	/** `0x103ddaf0`, `CNPC_VYukie#201` — `FVisible`. The gate, and on success the base check through
	 *  vtable `+0x948` (slot 594, `0x102b4760`, story 29d) rather than the werewolf's unconditional
	 *  true. The refusal arm zeroes the blocker only on the `npc_ignore_player` branch and on the
	 *  `npc_ignore_senses` branch, not on a null candidate — retail's own asymmetry. */
	bool YukieFVisible(const FElysiumEntity* Candidate, FElysiumEntityHandle* OutBlocker);

};
