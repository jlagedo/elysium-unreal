#pragma once

#include "CoreMinimal.h"

#include "ElysiumNpcMindTypes.h"

#include <initializer_list>

class FElysiumCombatCharacter;
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
	SeeUnknown = 0x01,

	// --- The see-unknown sweep's other six products, `FUN_102b15c0` -------------------------------
	// Story 10b. `LOST_UNKNOWN` is the "stopped seeing it" edge, gated on the running program's mask
	// like the sound sweep's tail; `IGNORE_UNKNOWN` and the three closing-speed conditions are read
	// only while `SEE_UNKNOWN` still stands this pass, and `IGNORE_UNKNOWN`'s own arm can retract
	// that very bit mid-sweep. `INVESTIGATE_SIGHT` is the sight analogue of `INVESTIGATE_SOUND` --
	// the interest predicate's other named caller.
	LostUnknown       = 0x02,
	IgnoreUnknown     = 0x03,
	UnknownRunTimer   = 0x04,
	UnknownAdvancing  = 0x05,
	// Retail quirk: reachable only on exact float equality with the closing-speed threshold, so
	// effectively dead. Reproduced rather than fixed.
	UnknownHolding    = 0x06,
	UnknownRetreating = 0x07,
	InvestigateSight  = 0x26,

	// --- Recovered identities --------------------------------------------------------------------
	// --- The four law conditions ---
	// The registry numbers the survey states in decimal (`docs/vtmb/npc-ai-reverse-engineering.md`
	// -> "Player-law observation transaction"): 31, 32, 33, 34. Spelled in hex here so the whole
	// enum reads in one base; the decimal is beside each one because that is how the table names it.
	// Their producer is `Substrate/ElysiumNpcWitness.h`.
	CriminalFleeLevel       = 0x1f,   // 31 — `pl_criminal_flee`
	CriminalAttackLevel     = 0x20,   // 32 — `pl_criminal_attack`
	SupernaturalFleeLevel   = 0x21,   // 33 — `pl_supernatural_flee`
	SupernaturalAttackLevel = 0x22,   // 34 — `pl_supernatural_attack`
	ShouldDodge           = 0x0c,
	ShouldBlock           = 0x0d,
	ShouldStepback        = 0x0e,
	ShouldKick            = 0x0f,
	Knockback             = 0x28,
	WaitingAttackTime     = 0x2f,
	HitByDoor             = 0x34,
	// `COND_WAS_BUMPED`. `RunAI` (`0x1026f110`) clears it at the end of every NON-reduced pass,
	// beside `LIGHT_DAMAGE` and `HEAVY_DAMAGE`, and `CAI_BaseNPCTroika::GetSchedule`
	// (`0x102ae920`) reads it in combat. Its one producer is the player's own touch handler
	// (`0x10147690`) -- see `FElysiumNpc::OnBumped`.
	WasBumped             = 0x38,
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
	SeePlayer             = 0x5a,
	TaskFailed            = 0x5c,
	ScheduleDone          = 0x5d,
	TooCloseToAttack      = 0x5f,
	TooFarToAttack        = 0x60,
	WeaponBlockedByFriend = 0x63,
	WeaponSightOccluded   = 0x66,

	// --- Decoded from the base condition table, `FUN_102c8ce0` -----------------------------------
	// The `CAI_BaseNPC` registrar is one dense namespace, 0x00..0x76, 119 entries, dumped whole
	// (`docs/vtmb/npc-ai-reverse-engineering.md` -> "The base condition table"). These seven used to
	// sit in a placeholder band above 0x66; every value below is the registered one.
	SeeFear    = 0x44,
	SeeEnemy   = 0x46,
	HearDanger = 0x6a,
	HearThumper = 0x6b,
	HearBugbait = 0x6c,
	HearCombat = 0x6d,
	HearWorld  = 0x6e,
	HearPlayer = 0x6f,
	HearBulletImpact = 0x70,
	HearPhysicsDanger = 0x71,
	Smell = 0x5e,
	// The fifth law condition. `BuildScheduleTestBits` adds it as a custom interrupt to every
	// schedule of a non-busy, non-investigating NPC; `SelectSchedule` state 8 consumes it.
	InvestigateLevel = 0x1e,

	// The four `BuildScheduleTestBits` (`0x102ad140`) names beside the law conditions. Their
	// producers are not built yet -- `COMFORT` is the comfort-list sweep `FUN_102b1a20`,
	// `SQUAD_SEE_ENEMY` the squad layer, `HEAR_FLINCH` and `NPC_FREEZE` unrecovered -- but the
	// overlay names them and a mask that carries an identity the runtime cannot spell would be a
	// mask that silently drops a term.
	Comfort       = 0x27,
	SquadSeeEnemy = 0x31,
	HearFlinch    = 0x72,
	NpcFreeze     = 0x75,

	// --- The sound sweep's three products, `FUN_102b1cd0` -----------------------------------------
	// `INVESTIGATE_SOUND` is the ONLY route by which an alert NPC reaches a heard sound: the hunt
	// state accepts raw `HEAR_*`, alert does not. The raw condition and this one are therefore not
	// redundant -- the raw one says "I heard it", this one says "I heard it AND I take an interest",
	// and only the second reaches `SelectSchedule`'s alert ladder.
	InvestigateSound = 0x25,
	// Raised when the winning sound's owner is my committed enemy and the sound came from BEHIND
	// me. No consumer is recovered; the producer is.
	HearFlankSound   = 0x33,
	// "I can see whatever made the sound I last committed." Read by the sound selector and the hunt
	// ladder, both 10d.
	SeeSoundSource   = 0x2d,
};

// `investigate_mode` / `investigate_mode_combat`, the two authored keyfields the interest predicate
// `0x102b3270` switches on. The seven values are a clean product, `{never} x {players, anything} x
// {hated, non-neutral, any}`; anything else is retail's `DevWarning("Hey FOO!!!  I don't recognize
// your investigate mode!")` and a refusal. The shipped default is 4 on 378 of 426 rows: NPCs
// investigate what they HATE, not the player as such.
enum class EElysiumInvestigateMode : uint8
{
	Never             = 0,
	HatedPlayers      = 1,   // a player, and `IRelationType == D_HT`
	NonNeutralPlayers = 2,   // a player, and `!= D_NU`
	AnyPlayer         = 3,
	Hated             = 4,   // anything `== D_HT`
	NonNeutral        = 5,   // anything `!= D_NU`
	Anything          = 6,
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
	 * The interest predicate, `CAI_BaseNPCTroika::0x102b3270` -- "do I take an interest in this
	 * entity". It is what the see-unknown sweep, the sound sweep and the vision producer ask before
	 * raising `COND_INVESTIGATE_SIGHT` / `COND_INVESTIGATE_SOUND`. Of those three the sound sweep
	 * (`GatherSounds`, below) is built; the see-unknown sweep is story 10b and the vision producer
	 * is not built, so two of its retail callers are still absent.
	 *
	 * Recovered order:
	 *   1. `m_bfAINPCFlags & (DONT_INVESTIGATE | IN_FLEE_SCHED)` -> false.
	 *   2. `stay_entrenched` -> false.        NOT MODELLED: the keyfield is not parsed yet.
	 *   3. no candidate -> false.
	 *   4. candidate is my `m_hFollowerBoss` -> false.   NOT MODELLED: no follower subsystem.
	 *   5. candidate is my committed enemy -> TRUE, unconditionally.
	 *   6. the mode switch, on `investigate_mode` or -- with `bCombatMode` --
	 *      `investigate_mode_combat`.
	 *
	 * `bCombatMode` is retail's third argument. It is NOT sight-versus-sound: the sight sweep, the
	 * world/physics-danger/player sound arms and the vision producer pass 0; the bullet-impact and
	 * combat sound arms pass 1. It also unlocks a third-party-brawl proximity override (a candidate
	 * within 256 units 2-D / 80 vertical whose enemy is someone I hate) that is NOT modelled here
	 * because it reads the candidate's enemy, which this substrate does not expose on an entity.
	 */
	bool ShouldInvestigate(const FElysiumNpc& Npc, const FElysiumEntity& Candidate, bool bCombatMode);

	// `_DAT_1044f02c`, the see-unknown sweep's "stopped seeing it" grace: `+0x6084 = curtime + 1.5`.
	// Armed on the first miss, read as a deadline on every miss after.
	inline constexpr double SeeUnknownGraceSeconds = 1.5;
	// `_DAT_1044eb0c`, the see-unknown sweep's 2-D closing-speed edge, in Source units per second (the
	// sweep converts the player's cm/s velocity before comparing). Strictly below is retreating,
	// strictly above is advancing, and the single point of exact equality is `UNKNOWN_HOLDING` --
	// see its own comment on the enum identity.
	inline constexpr double UnknownClosingSpeedThreshold = 20.0;

	/**
	 * `FUN_102b15c0`, the first of `CAI_BaseNPCTroika::GatherConditions`' three sweeps (before the
	 * comfort sweep `0x102b1a20`, not built: story 10c, and `GatherSounds` below).
	 *
	 * The see-unknown channel only tracks one entity, `Memory.BestSeeUnknown`, and is player-only by
	 * construction: retail reads the target's cached `CBasePlayer*`, which is null for anything
	 * else. Walked:
	 *
	 *  1. Not seeing it this pass (`SEE_UNKNOWN` unset, which `GatherSight` has already gathered
	 *     earlier in the same pass): no tracked entity, or one whose handle no longer resolves,
	 *     answers `LOST_UNKNOWN` (gated on the running program's mask) immediately. A still-live
	 *     handle arms the 1.5 s grace on the first miss and answers nothing until it elapses, then
	 *     drops the handle, remembers its last position, and answers `LOST_UNKNOWN`.
	 *  2. Seeing it: reset the grace sentinel. Not the player, or no player resolved, is a return
	 *     with nothing else touched.
	 *  3. Not in "stealth posture" (`FElysiumPlayer::IsInStealthPosture`, i.e. plainly visible): a
	 *     ONE-SHOT `MADE_INITIAL_RESPONSE` roll sets or clears `ATTACK_UNKNOWN` off a chance
	 *     `min(100, (repeat_sightings + 5) * 20)` -- always 100, but the draw is still taken; `ATTACK_UNKNOWN` standing (fresh or carried from
	 *     an earlier pass) raises `UNKNOWN_RUN_TIMER`; then `ShouldInvestigate` gates
	 *     `INVESTIGATE_SIGHT`.
	 *  4. In stealth posture (hidden, crouched-unseen, or grappled): the same one-shot roll instead
	 *     sets or clears `IGNORE_UNKNOWN` off `max(0, 50 - repeat_sightings * 20)`, skipped entirely
	 *     under `full_investigate`. Then the 2-D closing speed -- the player's velocity dotted with
	 *     the normalized direction from the player toward this NPC -- classifies:
	 *       - `IGNORE_UNKNOWN` standing: closing past the threshold AND `SeeUnknownStartTimer`
	 *         elapsed raises `UNKNOWN_ADVANCING` + `INVESTIGATE_SIGHT`; otherwise this is where
	 *         retail RETRACTS `SEE_UNKNOWN` mid-sweep, and raises `IGNORE_UNKNOWN` unless
	 *         `LOOKED_AT_UNKNOWN` or `FINISHED_IGNORE_UNKNOWN` already stands.
	 *       - Otherwise: `INVESTIGATE_SIGHT`, then `UNKNOWN_RETREATING` / `UNKNOWN_HOLDING` (the
	 *         dead exact-equality case) / `UNKNOWN_ADVANCING` by the same threshold.
	 */
	void GatherSeeUnknown(FElysiumNpc& Npc, double Now, FElysiumNpcConditions& Out);

	/**
	 * `FUN_102b1cd0`, the third of `CAI_BaseNPCTroika::GatherConditions`' three sweeps (after the
	 * see-unknown sweep `0x102b15c0` and the comfort sweep `0x102b1a20`, the latter of which is not
	 * built: story 10c).
	 *
	 * It turns raw `HEAR_*` into the one condition alert selection can act on. Walked:
	 *
	 *  1. UNCONDITIONALLY clear `INVESTIGATE_SOUND` and `HEAR_FLANK_SOUND`. The sweep owns those two
	 *     -- `GatherConditions` itself clears neither. `SEE_SOUND_SOURCE` is cleared by the tail,
	 *     not here, and that asymmetry is real: the tail has two arms that leave it STICKY.
	 *  2. Gate the whole six-arm body on `m_flNextInvestigateSoundTime <= curtime`.
	 *  3. Six arms, in retail's evaluation order, LAST passing arm wins. There is no Flinch arm:
	 *     `+0x6210` is never swept, and reaches a decision only through `CommitBestSound`'s ranking
	 *     and the selector's own `HasInterruptCondition(HEAR_FLINCH)` (10d).
	 *
	 *       world 0x6e (b=0), physics danger 0x71 (b=0), danger 0x6a (predicate SKIPPED),
	 *       player 0x6f (b=0), bullet impact 0x70 (b=1), combat 0x6d (b=1)
	 *
	 *     Each: `HasCondition(HEAR_X) && (mask lists HEAR_X || ShouldInvestigate(owner, b))`.
	 *     `HEAR_DANGER` tests neither -- it sets and claims the winner on `HasCondition` alone.
	 *  4. `HEAR_FLANK_SOUND`: the mask lists it, there IS a winner, I have a committed enemy, the
	 *     enemy owns the winning sound, and the sound is behind me.
	 *  5. The `SEE_SOUND_SOURCE` tail, OUTSIDE the gate, over the previously committed source.
	 *
	 * Takes a mutable NPC because step 5 writes the stranger arm's rate limit. `Out` is the pass's
	 * condition set, already carrying the raw `HEAR_*` bits `GatherHearing` put there.
	 */
	void GatherSounds(FElysiumNpc& Npc, double Now, FElysiumNpcConditions& Out);

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

	// `_DAT_104454d0`, the sound sweep's `SEE_SOUND_SOURCE` stranger-arm re-arm: `+0x6418 =
	// curtime + 0.5`. Written and read only by that arm.
	inline constexpr double SeeSoundSourceCadenceSeconds = 0.5;

	// The accumulator half of that rule, run from the typed damage commit. Kept here rather than on
	// the leaf so the window arithmetic has one owner and one test.
	void AccumulateDamage(FElysiumNpcMemory& Memory, int32 CommittedDamage, double Now);

	// --- Sound categories ---------------------------------------------------------------------------
	// CHOSEN, NOT RECOVERED (stated once here because both hearing and the condition
	// producer read it): the recovered material names `HEAR_COMBAT` and `OnHearCombat` and states
	// that combat noise drives them — gunshots, `NPC_TAKE_DAMAGE`, explosions and weapon impacts —
	// but no per-category table survives. The set is therefore matched by name.
	bool IsCombatSoundCategory(const FString& FoldedCategory);

	// The `HEAR_*` family the base ideal-state promotion tests as a group.
	bool IsHearFamily(EElysiumNpcCond Cond);

	// --- The producers ------------------------------------------------------------------------------
	// Each reads saved memory plus the live world and sets bits. The four base producers below never
	// clear; `GatherSounds` is the one exception and owns its three products outright — it clears
	// `INVESTIGATE_SOUND` and `HEAR_FLANK_SOUND` unconditionally and `SEE_SOUND_SOURCE` in its
	// tail, exactly as `FUN_102b1cd0` does (see its own header). `PreviousGatherTime` is the edge: a
	// stimulus stamped after the previous pass is new to this one.

	// `LIGHT_DAMAGE` / `HEAVY_DAMAGE` / `REPEATED_DAMAGE` from the last committed packet.
	void GatherDamage(const FElysiumNpc& Npc, double PreviousGatherTime, FElysiumNpcConditions& Out);
	// `COND_WAS_BUMPED`, reconstructed the same way and for the same reason as the damage pair:
	// retail sets the bit inside the touch and clears it when a non-reduced `RunAI` ends, so it is
	// decision input for exactly one full pass.
	void GatherBump(const FElysiumNpc& Npc, double PreviousGatherTime, FElysiumNpcConditions& Out);

	// The `HEAR_*` family from the last accepted stimulus, by the same category mapping the
	// senses output selection uses.
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
	ECapability WeaponCapability(const FElysiumCombatCharacter& Char);
	// The NPC spelling, kept because every AI call site reads as one. It is the same answer: the
	// active item and its record live on the combat-character node, and the melee test is retail's
	// `0x18000` mask whichever body is holding the weapon — the player's block predicate asks the
	// same question of the same fields.
	ECapability WeaponCapability(const FElysiumNpc& Npc);

	// --- Attack conditions ------------------------------------------------------------------------
	// CHOSEN, NOT RECOVERED: the melee reach and cone are the weapon controller's, reused rather than restated —
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
