#pragma once

#include "CoreMinimal.h"

#include "ElysiumNpcMindTypes.h"

#include <initializer_list>

class FElysiumEntity;
class FElysiumEntityWorld;
class FElysiumNpc;
struct FElysiumEntityHandle;
struct FElysiumNpcMemory;

// The AI condition bitset and the producers that fill it.
//
// A condition is one bit of decision input, gathered fresh at the top of every decision pass and
// consumed by ideal-state selection, enemy choice and the schedule interrupt masks. The recovered
// pass (`docs/vtmb/npc-ai-reverse-engineering.md` -> "AI update loop") clears the gathered marker
// when `RunAI` begins and clears transient conditions when it ends, so a condition never outlives
// the pass that produced it.
//
// **Conditions are NOT saved.** No recovered VtMB datamap record carries a condition bitvector, and
// the recovered pass re-derives the whole set from memory (enemy, last-seen, last-heard, last
// damage) every time it runs. Memory is what a save carries (`FElysiumNpcMemory`); the conditions
// over it are rebuilt on the first think after a load. Nothing is lost by that, because every
// producer below reads only saved memory plus the live world.
//
// The VtMB facts: `docs/vtmb/npc-ai-reverse-engineering.md` -> "Enemy acquisition and replacement",
// "Interrupt conditions", "`no_alert_state` does not suppress the alert state", and
// `docs/vtmb/combat-and-damage.md` -> "NPC damage response and stagger boundaries".

// The recovered condition registry. Values ARE retail's registered numbers wherever the survey
// decoded one, so a trace row and the binary agree.
enum class EElysiumNpcCond : uint8
{
	None = 0x00,

	// --- Recovered identities --------------------------------------------------------------------
	// ============================ Cycle 10c — the four law conditions ============================
	// The registry numbers the survey states in decimal (`docs/vtmb/npc-ai-reverse-engineering.md`
	// -> "Player-law observation transaction"): 31, 32, 33, 34. Spelled in hex here so the whole
	// enum reads in one base; the decimal is beside each one because that is how the table names it.
	// Their producer is `Substrate/ElysiumNpcWitness.h`.
	CriminalFleeLevel       = 0x1f,   // 31 — `pl_criminal_flee`
	CriminalAttackLevel     = 0x20,   // 32 — `pl_criminal_attack`
	SupernaturalFleeLevel   = 0x21,   // 33 — `pl_supernatural_flee`
	SupernaturalAttackLevel = 0x22,   // 34 — `pl_supernatural_attack`
	// =============================================================================================
	ShouldDodge           = 0x0c,
	ShouldBlock           = 0x0d,
	ShouldStepback        = 0x0e,
	ShouldKick            = 0x0f,
	Knockback             = 0x28,
	WaitingAttackTime     = 0x2f,
	HitByDoor             = 0x34,
	WeaponThroughWall     = 0x3c,
	NoPrimaryAmmo         = 0x40,
	SeeHate               = 0x43,
	SeeDislike            = 0x45,
	LostEnemy             = 0x47,
	EnemyOccluded         = 0x48,
	HaveEnemyLos          = 0x4a,
	LightDamage           = 0x4c,
	HeavyDamage           = 0x4d,
	RepeatedDamage        = 0x4e,
	CanRangeAttack1       = 0x4f,
	CanRangeAttack2       = 0x50,
	CanMeleeAttack1       = 0x51,
	CanMeleeAttack2       = 0x52,
	NewEnemy              = 0x54,
	EnemyDead             = 0x58,
	EnemyUnreachable      = 0x59,
	SeeNemesis            = 0x5b,
	TooCloseToAttack      = 0x5f,
	TooFarToAttack        = 0x60,
	WeaponBlockedByFriend = 0x63,
	WeaponSightOccluded   = 0x66,

	// --- Named in the recovered material, identity NOT recovered ---------------------------------
	// The survey names these in the interrupt-condition census and the incident chain but decodes no
	// number for them. They are deliberately placed above the recovered 0x00..0x66 band so a value
	// here can never collide with a retail identity that is decoded later; renumber in place when
	// one is.
	SeeEnemy   = 0xe0,
	SeeFear    = 0xe1,
	HearCombat = 0xe2,
	HearPlayer = 0xe3,
	HearWorld  = 0xe4,
	HearDanger = 0xe5,
	// ============================ Cycle 10c — the fifth law condition ============================
	// `COND_INVESTIGATE_LEVEL` is named beside the four above ("clears and recomputes
	// `COND_INVESTIGATE_LEVEL` plus four law conditions") and its registry number is NOT stated, so
	// it lands in this band rather than being guessed at 30 or 35.
	InvestigateLevel = 0xe6,
	// =============================================================================================
};

const TCHAR* ElysiumNpcCondName(EElysiumNpcCond Cond);

// A plain 256-bit set over the identities above. No reflection, no allocation, trivially copyable —
// it is carried by value on the NPC, by the schedule registry as an interrupt mask, and by every
// test that drives a decision pass.
struct FElysiumNpcConditions
{
	static constexpr int32 NumWords = 4;

	void Set(EElysiumNpcCond Cond) { Words[Word(Cond)] |= Bit(Cond); }
	void Clear(EElysiumNpcCond Cond) { Words[Word(Cond)] &= ~Bit(Cond); }
	bool Has(EElysiumNpcCond Cond) const { return (Words[Word(Cond)] & Bit(Cond)) != 0; }

	void Reset()
	{
		for (uint64& W : Words)
		{
			W = 0;
		}
	}

	bool IsEmpty() const
	{
		for (const uint64 W : Words)
		{
			if (W != 0)
			{
				return false;
			}
		}
		return true;
	}

	int32 Num() const
	{
		int32 Count = 0;
		for (const uint64 W : Words)
		{
			Count += static_cast<int32>(FMath::CountBits64(W));
		}
		return Count;
	}

	bool Intersects(const FElysiumNpcConditions& Other) const
	{
		for (int32 i = 0; i < NumWords; ++i)
		{
			if ((Words[i] & Other.Words[i]) != 0)
			{
				return true;
			}
		}
		return false;
	}

	FElysiumNpcConditions Intersection(const FElysiumNpcConditions& Other) const
	{
		FElysiumNpcConditions Out;
		for (int32 i = 0; i < NumWords; ++i)
		{
			Out.Words[i] = Words[i] & Other.Words[i];
		}
		return Out;
	}

	FElysiumNpcConditions& operator|=(const FElysiumNpcConditions& Other)
	{
		for (int32 i = 0; i < NumWords; ++i)
		{
			Words[i] |= Other.Words[i];
		}
		return *this;
	}

	bool operator==(const FElysiumNpcConditions& Other) const
	{
		for (int32 i = 0; i < NumWords; ++i)
		{
			if (Words[i] != Other.Words[i])
			{
				return false;
			}
		}
		return true;
	}

	// The lowest set identity, so an interrupt trace names one condition rather than a bit pattern.
	// Returns `None` for an empty set, which is also identity 0 and therefore never a member.
	EElysiumNpcCond FirstSet() const;

	// "SEE_HATE|NEW_ENEMY" — for a trace row and the inspector.
	FString Describe() const;

	static FElysiumNpcConditions Of(std::initializer_list<EElysiumNpcCond> List)
	{
		FElysiumNpcConditions Out;
		for (const EElysiumNpcCond Cond : List)
		{
			Out.Set(Cond);
		}
		return Out;
	}

private:
	static int32 Word(EElysiumNpcCond Cond) { return static_cast<int32>(Cond) >> 6; }
	static uint64 Bit(EElysiumNpcCond Cond) { return 1ull << (static_cast<uint32>(Cond) & 63u); }

	uint64 Words[NumWords] = { 0, 0, 0, 0 };
};

// The per-NPC decision-pass state. The conditions themselves are session state (see the file
// header); what sits beside them here is the previous pass's timestamp — which is how an
// edge-triggered stimulus (a damage commit, a heard sound) is distinguished from the same memory
// read again on the next pass — plus the once-latch bytes that keep a diagnostic from becoming a
// per-think wall.
struct FElysiumNpcCognition
{
	FElysiumNpcConditions Conditions;

	// When the previous gather ran. Negative means "no pass has run yet"; a restore stamps it with
	// the load time so an hour-old remembered gunshot does not promote a restored NPC to alert.
	double GatheredAt = -1.0;

	// One report each, per NPC.
	bool bWarnedCombatWithoutEnemy = false;
	bool bReportedAlertRefusal = false;
	// The retail-shaped starvation warning is latched per NPC *per schedule*: a different schedule
	// starving selection is a different fact. Retail's registered schedule number, or -1.
	int32 StarvedScheduleNumber = -1;
};

namespace ElysiumNpcCond
{
	/**
	 * Resolve an enemy handle for the cognition layer, which needs the distinction
	 * `FElysiumEntityWorld::Resolve` deliberately collapses.
	 *
	 * `Resolve` answers null for a dead entity as well as for a stale or unbound handle, because
	 * that is the falsy-when-dead contract VtMB scripts rely on. Enemy selection has to tell the
	 * two apart: a dead actor is still an actor and raises `ENEMY_DEAD`, while a handle whose slot
	 * or epoch no longer matches is gone and raises `LOST_ENEMY`. This returns the entity in both
	 * live and dead cases and null only when the handle resolves to nothing at all; the caller
	 * asks `IsInert()`.
	 */
	const FElysiumEntity* ResolveEnemyHandle(const FElysiumEntityWorld& World,
		const FElysiumEntityHandle& Handle);

	// --- Damage ------------------------------------------------------------------------------------
	// CHOSEN, NOT RECOVERED: the heavy-damage threshold. `CAI_BaseNPC::OnTakeDamageAlive` asks "the
	// class light/heavy predicates" to set `LIGHT_DAMAGE` (0x4c) and `HEAVY_DAMAGE` (0x4d)
	// (`docs/vtmb/combat-and-damage.md`), and neither predicate body is recovered — so the fraction
	// of Source max health that makes a hit heavy is ours. A fifth of the pool is picked because it
	// sits below the recovered 15%-in-one-second repeated-damage rule's per-window sum while still
	// requiring a real hit. Decompiling either predicate settles it; replace this constant, not the
	// shape around it.
	inline constexpr float HeavyDamageFraction = 0.20f;

	// Recovered: damage is accumulated for a one-second window and a sum over 15 percent of Source
	// max health sets `REPEATED_DAMAGE` (0x4e); an expired window is reset rather than decayed.
	inline constexpr double RepeatedDamageWindowSeconds = 1.0;
	inline constexpr float RepeatedDamageFraction = 0.15f;

	// The accumulator half of that rule, run from the typed damage commit. Kept here rather than on
	// the leaf so the window arithmetic has one owner and one test.
	void AccumulateDamage(FElysiumNpcMemory& Memory, int32 CommittedDamage, double Now);

	// --- Sound categories ---------------------------------------------------------------------------
	// CHOSEN, NOT RECOVERED (cycle 4, stated once here now that both hearing and the condition
	// producer read it): the recovered material names `HEAR_COMBAT` and `OnHearCombat` and states
	// that combat noise drives them — gunshots, `NPC_TAKE_DAMAGE`, explosions and weapon impacts —
	// but no per-category table survives. The set is therefore matched by name.
	bool IsCombatSoundCategory(const FString& FoldedCategory);

	// The `HEAR_*` family the base ideal-state promotion tests as a group.
	bool IsHearFamily(EElysiumNpcCond Cond);

	// --- The producers ------------------------------------------------------------------------------
	// Each reads saved memory plus the live world and sets bits; none clears. `PreviousGatherTime`
	// is the edge: a stimulus stamped after the previous pass is new to this one.

	// `LIGHT_DAMAGE` / `HEAVY_DAMAGE` / `REPEATED_DAMAGE` from the last committed packet.
	void GatherDamage(const FElysiumNpc& Npc, double PreviousGatherTime, FElysiumNpcConditions& Out);

	// The `HEAR_*` family from the last accepted stimulus, by the same category mapping cycle 4's
	// output selection uses.
	void GatherHearing(const FElysiumNpc& Npc, double PreviousGatherTime, FElysiumNpcConditions& Out);

	// The seen set joined to the relationship table: `SEE_HATE` / `SEE_FEAR`, and the last-seen
	// memory slots those categories own. Writes `Npc.Senses.Memory.LastSeen*`.
	void GatherSight(FElysiumNpc& Npc, double Now, FElysiumNpcConditions& Out);

	// `HAVE_ENEMY_LOS` / `ENEMY_OCCLUDED` / `ENEMY_DEAD` / `SEE_ENEMY` for the committed enemy.
	void GatherCommittedEnemy(const FElysiumNpc& Npc, FElysiumNpcConditions& Out);

	// --- Weapon capability ------------------------------------------------------------------------
	/**
	 * The two capability bits combat selection reads, and nothing else.
	 *
	 * `CNPC_VHuman::SelectSchedule` (`0x10384ee0`) queries the active weapon's capability word: a
	 * weapon reporting `0x18000` enters the melee selector, and every other weapon enters the ranged
	 * one (`docs/vtmb/npc-ai-reverse-engineering.md` -> "Ordinary humanoid combat selection"; the
	 * melee class's full result is `0x40018000` and its `0x18000` portion is also the player's block
	 * gate, `docs/vtmb/combat-and-damage.md`).
	 *
	 * SCOPE, DELIBERATE: this is NOT retail's capability word. The word carries bits for squad
	 * behaviour, doors, jumping and more, none of which is decoded and none of which this selection
	 * reads. Reproducing two named bits as a three-value answer is the whole of what the recovered
	 * split needs; growing this into a bitfield would be inventing a register.
	 */
	enum class ECapability : uint8
	{
		// No active weapon, or one whose record is not a controllable weapon family. It takes the
		// MELEE selector with bare-hands defaults — the reach and cone constants below — and its
		// attack tasks fail by name, because there is no controller to press.
		Unarmed,
		Melee,
		Ranged,
	};
	const TCHAR* CapabilityName(ECapability Capability);
	// Retail's own two values, carried so a trace row and the binary agree.
	int32 CapabilityBits(ECapability Capability);
	inline constexpr int32 MeleeCapabilityBits = 0x18000;
	inline constexpr int32 RangedCapabilityBits = 0x2000;

	// The active weapon's family, resolved through its parsed `vdata/items` record. Never from a
	// classname prefix (`ElysiumItemClasses.h`).
	//
	// CHOSEN, NOT RECOVERED: `WeaponThrown` takes the ranged branch. The survey names only the
	// `0x18000` melee test and "other weapons" for everything else, so a thrown weapon is "other" by
	// the recovered rule — but no decoded body states a thrown weapon's own capability value.
	ECapability WeaponCapability(const FElysiumNpc& Npc);

	// --- Attack conditions ------------------------------------------------------------------------
	// CHOSEN, NOT RECOVERED: the melee reach and cone are cycle 2's, reused rather than restated —
	// `ElysiumWeapons::MeleeReachSourceUnits` (retail's own maximum custom sequence reach stands in
	// at 64 Source units) and `MeleeConeHalfAngleDegrees` (`FindEntityFOV`'s recovered 30-degree
	// half-angle). Using the swing's own numbers is what keeps `CAN_MELEE_ATTACK1` from promising a
	// hit the weapon's acquisition would then refuse.

	/**
	 * The committed enemy's range/facing/readiness conditions, gathered as step 5's tail.
	 *
	 * Melee capability produces `CAN_MELEE_ATTACK1` (0x51), `TOO_FAR_TO_ATTACK` (0x60) and
	 * `WAITING_ATTACK_TIME` (0x2f); ranged capability produces `CAN_RANGE_ATTACK1` (0x4f),
	 * `TOO_CLOSE_TO_ATTACK` (0x5f), `TOO_FAR_TO_ATTACK`, `NO_PRIMARY_AMMO` (0x40),
	 * `WAITING_ATTACK_TIME` and `WEAPON_SIGHT_OCCLUDED` (0x66).
	 *
	 * SEAM (comment only, never set): `WEAPON_THROUGH_WALL` (0x3c) and `WEAPON_BLOCKED_BY_FRIEND`
	 * (0x63). Both are line-of-FIRE terms about the muzzle rather than the eye — one asks whether
	 * the barrel is inside geometry, the other whether a friendly body is on the ray — and this
	 * runtime has neither a muzzle transform nor a squad. Deriving either from the eye's occlusion
	 * latch would raise a condition whose whole point is that it disagrees with that latch.
	 *
	 * SEAM (comment only, never set): `CAN_MELEE_ATTACK2` / `CAN_RANGE_ATTACK2` (0x52 / 0x50). The
	 * secondary attack's own eligibility rule is not decoded, and a mode's authored `Secondary`
	 * record does not by itself say when an NPC should use it.
	 */
	void GatherAttackConditions(const FElysiumNpc& Npc, double Now, FElysiumNpcConditions& Out);

	// --- The incoming-attack notice ---------------------------------------------------------------
	// Recovered (`docs/vtmb/combat-and-damage.md` -> "Target acquisition, sequence commit and
	// recovery"): an accepted melee target receives an incoming-melee notice, and "the NPC notice
	// path accepts the warning in its eligible states when the attacker is within 150 Source units
	// or satisfies its visibility route, remembers the attacker for five seconds and lets the
	// concrete combatant schedule its response". No health or damage changes.
	inline constexpr float MeleeNoticeAcceptanceUnits = 150.0f;
	inline constexpr double DetectedAttackRetentionSeconds = 5.0;

	/**
	 * Deliver one notice to `Victim`. Returns whether it was accepted.
	 *
	 * SCOPE, DELIBERATE: the visibility route is the senses' own player-LOS cache, so an NPC
	 * attacker outside the 150-unit radius is not noticed — the same single-observer scope
	 * `NpcCondBuildSeenSet` carries, and the same place admitting other observers would be one
	 * addition rather than several.
	 *
	 * SEAM (parsed, unread): `ignore_detected_attack` (41 authored occurrences) is the keyfield that
	 * suppresses this in retail. It is not read here because the NPC leaf does not carry it yet and
	 * a suppression with no keyfield behind it would be invented, not authored.
	 */
	bool NoticeMeleeAttack(FElysiumNpc& Victim, const FElysiumEntityHandle& Attacker,
		const FVector& AttackerOrigin, double Now);

	// Is a detected-attack record still inside its five-second retention at `Now`?
	bool HasDetectedAttack(const FElysiumNpc& Npc, double Now);

	// --- Alert lookaround ---------------------------------------------------------------------------
	// `min(30, (m_iEnemySightings + 2) * 5)`, recovered verbatim from step 5 of the idle selector:
	// the percentage chance, out of `RandomInt(0,99)`, that an NPC allowed to look around does. A
	// character that has never acquired the player reads the 10% floor; four acquisitions or more
	// read the 30% cap.
	inline int32 AlertLookaroundChance(int32 EnemySightings)
	{
		return FMath::Min(30, (FMath::Max(0, EnemySightings) + 2) * 5);
	}

	// --- Ideal state --------------------------------------------------------------------------------
	// The decision inputs, spelled out so the rule is drivable with no NPC at all.
	struct FIdealStateInput
	{
		EElysiumNpcState Current = EElysiumNpcState::Idle;
		bool bNoAlertState = false;
		bool bHasEnemy = false;
	};

	/**
	 * `CAI_BaseNPC::SelectIdealState` (`0x1026f660`) — the BASE layer.
	 *
	 * Its `case 1` promotes idle -> alert on `COND_LIGHT_DAMAGE`, `COND_HEAVY_DAMAGE` and the whole
	 * hear family **with no `m_bNoAlertState` test**, and its `case 2` emits `Combat state with no
	 * enemy` and falls back to state 3. `bOutCombatWithoutEnemy` reports that emission to the caller
	 * so the warning is logged where an entity name is in hand.
	 */
	EElysiumNpcState SelectIdealStateBase(const FIdealStateInput& In,
		const FElysiumNpcConditions& Cond, bool& bOutCombatWithoutEnemy);

	/**
	 * `CAI_BaseNPCTroika::SelectIdealState` (`0x102ad660`) — the TROIKA layer, and the whole point
	 * of keeping two.
	 *
	 * `no_alert_state` skips THIS layer's damage and sense promotions, and the body then ends in an
	 * unconditional `return CAI_BaseNPC::SelectIdealState(this)`. The keyvalue is therefore not a
	 * suppression: the base tail promotes anyway. Collapsing the two layers into one gated test is
	 * exactly the bug the recovered shape exists to prevent.
	 */
	EElysiumNpcState SelectIdealState(const FIdealStateInput& In, const FElysiumNpcConditions& Cond,
		bool& bOutCombatWithoutEnemy);
}
