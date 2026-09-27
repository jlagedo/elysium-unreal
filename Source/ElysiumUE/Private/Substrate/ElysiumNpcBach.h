#pragma once

#include "Substrate/ElysiumNpcVampire.h"

// `CNPC_VBach` (primary vtable `0x104a9f2c`), built by `npc_VBach` factory `0x10362ba0`.
//
// The classname's factory builds this class and it answers its own census row (story 5 step 2). Its
// slot overrides, own bodies, own datamap words and their bindings live here (steps 3-4,
// `docs/specs/0019-npc-kernel-rework/story-5-execution-plan.md`); the words a Troika body still
// reads stay on `FElysiumNpc` until step 11.
class FElysiumNpcBach : public FElysiumNpcVampire
{
public:
	ELYSIUM_NPC_CLASS("CNPC_VBach", FElysiumNpcVampire)

	virtual int32 Slot606(int32 Arg) override;
	virtual void* Slot609(bool bForce) override;
	virtual void NPCInit() override;
	virtual void Precache() override;
	virtual void OnStateChange(EElysiumNpcState OldState, EElysiumNpcState NewState) override;
	virtual int32 SelectIdealStateRetail() override;
	virtual int32 SelectScheduleMeleeCombat(int32 Unused) override;
	virtual int32 SelectScheduleRangedCombat(int32 Arg) override;
	virtual int32 TranslateScheduleRetail(int32 ScheduleNumber) override;
	virtual bool FValidateHintType(void* Hint) override;
	virtual const TCHAR* SquadSlotName(int32 SlotEn) override;
	virtual void GatherAttackConditions(FElysiumEntity* Enemy, float DistanceUnits) override;

	// --- Moved from the kernel families (story 5 step 4) ---------------------------------

	/** `+0x6444` — the word Bach's fall-through clears unless `m_NPCState` is 4 or 0xc. No reader is
	 *  recovered either; same treatment. */
	int32 BachClearWord = 0;

	// From `ElysiumNpcDamage.inl`.
	// `CNPC_VBach`'s grenade cooldown and the byte its throw clears.
	double BachLastGrenadeTime = 0.0;            // +0x6680 m_flLastGrenadeTime, an absolute stamp
	bool bBachCamperFlag = false;                // +0x66a0 m_bCamperFlag (datamap)
	/** `0x10365860` — `CNPC_VBach::ThrowGrenade(const char* targetName, float force)`. Gated on
	 *  `curtime - m_flLastGrenadeTime (+0x6680) >= 5.0` (`_DAT_10454110`). Then: find the named entity
	 *  and, only if it exists, stamp `m_flLastGrenadeTime` with curtime, create an
	 *  `item_w_grenade_frag` at that entity's origin, set `m_takedamage = 2`, `m_iHealth = 1` and clear
	 *  its touch function, initialise its physics if it has none (a failure `Msg`es
	 *  `"No physics data for grenade"` and removes it), take the target's FORWARD vector, apply
	 *  `forward * force` as a velocity with zero angular velocity, arm both its own `+0x7c` word and
	 *  `m_flNextThink` at `curtime + 3.0 (+0.01)` and clear `m_bCamperFlag` (+0x66a0). */
	void ThrowGrenade(const FString& GrenadeTargetName, float Force);

	// From `ElysiumNpcLifecycle19.inl`.
	static constexpr float SwarmDistTooFar = 65535.f;        // `0x477fff00`, Bach's

	// From `ElysiumNpcMisc.inl`.
	/** `CNPC_VBach::vfunc553` (`0x10364500`) and `::vfunc554` (`0x10364550`) — slots 553/554
	 *  `RangeAttack1Conditions` / `RangeAttack2Conditions`. One shape, one differing answer. */
	int32 BachRangeAttack1Conditions(float Dot, float DistUnits) const;
	int32 BachRangeAttack2Conditions(float Dot, float DistUnits) const;

	// From `ElysiumNpcSpecies.inl`.
	// `CNPC_VBach`'s fire gate — the one-shot arm slot 606 keeps around `COND_ENEMY_OCCLUDED`.
	bool bBachFireOccluded = false;   // +0x66a3 CNPC_VBach::m_bFireOccluded (datamap)
	/** `0x103661f0` — `CNPC_VBach`'s slot-609 gate. `CNPC_VBatSwarm` (`0x10367740`) and
	 *  `CNPC_VSheriffSwarm` (`0x103b26f0`) carry byte-identical copies; neither class has an
	 *  instance. */
	bool FUN_103661f0(bool bArg);

	// From `ElysiumNpcSpeciesMisc10.inl`.
	/** `CNPC_VBach::GatherAttackConditions` (`0x10363db0`), slot 561's species arm. It ADDS the shield,
	 *  teleport and weapon-switch block IN FRONT of the base and changes nothing the base gathers, so
	 *  `FElysiumNpc::GatherAttackConditions` runs this first and then the base body unmodified. */
	void BachGatherAttackConditions(float DistanceUnits);
	/** `CNPC_VBach`'s shield block. `m_iBachTeleportState` indexes `DAT_1062d200`, whose four cells read
	 *  **768.0, 768.0, 384.0, 384.0** out of the pinned image at `0x62d200`. */
	static constexpr float BachTeleportDistanceUnits[4] = { 768.f, 768.f, 384.f, 384.f };
	int32 BachTeleportState = 0;             // `m_iBachTeleportState`
	// +0x6690 `m_flNextHolyLightTime`, an absolute curtime stamp. The COND `0x7b` arms of the two
	// selectors `0x10364080` and `0x103642f0` write it (`curtime + _DAT_10463584`, 15.0), and the
	// shield block `0x10363db0` reads it.
	double BachNextHolyLightTime = 0.0;
	double BachShieldTime = 0.0;             // +0x6684 `m_flShieldTime`
	double BachNextShieldTime = 0.0;         // +0x6688 `m_flNextShieldTime`
	double BachNextWeaponSwitchTime = 0.0;   // +0x668c `m_flNextWeaponSwitchTime`
	bool bBachShieldActive = false;          // +0x66a5 `m_bShieldActive`
	bool bBachShieldFlagB = false;           // +0x66a6, cleared on BOTH arms; retail name unrecovered

	// From `ElysiumNpcState19.inl`.
	/** `CNPC_VBach::m_bCanFightYet` (`+0x66a8`). Default 0, so Bach snaps ALERT/COMBAT back. */
	bool bCanFightYet = false;
	/** `CNPC_VBach::OnStateChange` (`0x103639b0`) and `CNPC_VCop::OnStateChange` (`0x10371c20`).
	 *  Bach returns true when it snapped back (the Troika body must not run). */
	bool BachOnStateChange(int32 OldRetail, int32 NewRetail);

	/** `CNPC_VBach::m_bMovementSpot` (`+0x66a7`). */
	bool bBachMovementSpot = false;
};
