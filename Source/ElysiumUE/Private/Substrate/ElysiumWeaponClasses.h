#pragma once

// 13.3 — the weapon controller: mode dispatch, next-attack scheduling on the substrate clock, the
// two-half attack transaction, reload and dry fire.
//
// The behaviour reproduced here is canonical in `docs/vtmb/combat-and-damage.md`; the design seam
// is `docs/architecture/gameplay-systems-architecture.md` §5.4. This file owns no damage
// arithmetic beyond the weapon's own lethality/margin stage — the descriptor, the shared resolver
// and the one typed health commit are `Substrate/ElysiumDamage.h` and
// `FElysiumCombatCharacter::CommitDamage`, and every hit lands through them.
//
// THE ATTACK IS TWO HALVES, AND THAT IS THE POINT.
// An accepted swing is a transaction — logical activity, concrete clip, aimed opponent, playback
// rate and recovery deadline. The commit is a SEPARATE later event that can miss: it re-validates
// the owner, the weapon and the target, and only then spends damage.
//
// THE TWO FAMILIES REACH THAT SECOND HALF BY DIFFERENT ROUTES, AND THAT IS RETAIL'S OWN SHAPE.
//
//  * RANGED commits from a server animation event. `OperatorHandleAnimEvent` below is retail's
//    `Operator_HandleAnimEvent` `+0x5c8`: ids 3030-3044 re-enter mode dispatch and queue the commit
//    with no delay. Where that route cannot run — a headless world, a body whose pose layer
//    publishes no phase, or a shot clip whose timeline names no id — the transaction schedules the
//    `ContactEventCycle` estimate instead. Either way the commit rides the one event queue as a
//    self-input, so there is no private timer and no second scheduler (K11).
//
//  * MELEE commits from a PER-FRAME SWEPT CONTACT WALK over the clip's own authored swing records
//    (`AdvanceSwingContact`, the rules in `Substrate/ElysiumSwingContact.h`). There is no melee
//    commit event and no estimate: the sequence descriptor states where on the limb the swing
//    sweeps and over which slice of the clip cycle, and the walk tests exactly that. A melee clip
//    declaring no records has no contact at all, which is what retail's own shape gives it. 3047 is
//    retail's NPC swing TRIGGER rather than a commit, and is claimed here without committing
//    anything.

#include "CoreMinimal.h"
#include "Misc/EnumClassFlags.h"

#include "ElysiumAnimEvent.h"
#include "ElysiumAnimationIntent.h"   // EElysiumAnimPriority/EElysiumAnimChannel — the band and the channel
#include "ElysiumEntity.h"
#include "ElysiumSwingRecord.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumItemClasses.h"

struct FElysiumDiceTables;
struct FElysiumFeatTable;
struct FElysiumRules;
struct FElysiumWeaponMode;
// The authored `item_type`, declared in `Substrate/ElysiumItemTable.h`. Forward-declared with its
// underlying type rather than included: this header needs the name to select an operator body and
// nothing else from the table.
enum class EElysiumItemType : uint8;

// The chain node the three wielded weapon families register under — retail's shared `CWeapon`, the
// class that owns the traced-impact virtual both the melee and the ranged vtables fill. Like the
// other chain nodes it never appears in a `.ents` file.
inline FName ElysiumWeaponClassName() { return FName(TEXT("CWeapon")); }

// The two scheduling inputs the controller queues to ITSELF. They are this runtime's own deferred
// work, not recovered datamap names: retail reaches the same two points from a server animation
// event and from the reload frame handler, neither of which is an I/O input. They are registered on
// `CWeapon` so the deferral rides the one event queue (K11) and serializes with it (R8), the same
// way `CLogicRelay`'s refire lockout does.
inline FName ElysiumWeaponCommitInput() { return FName(TEXT("WeaponAttackCommit")); }
inline FName ElysiumWeaponReloadInput() { return FName(TEXT("WeaponReloadCommit")); }

// ================================================================================================
// The `rules.txt` melee reaction margins
// ================================================================================================
//
// `RuleData/Melee_Reactions` (`combat-and-damage.md` § "Opposed record and reaction margin"). K9:
// these are read, never retyped — an absent block leaves the table invalid and the classifier says
// so instead of inventing bands.

struct FElysiumMeleeMargins
{
	int32 AttackerBlockedMajor = 0;   // `SuccessesForAttackerBlockedMajor`
	int32 AttackerBlocked = 0;        // `SuccessesForAttackerBlocked`
	int32 DefenderDodgeAttack = 0;    // `SuccessesForDefenderDodgeAttack`
	int32 DefenderDodge = 0;          // `SuccessesForDefenderDodge`
	int32 DefenderBlock = 0;          // `SuccessesForDefenderBlock`
	int32 DefenderBlockStagger = 0;   // `SuccessesForDefenderBlockStagger`
	bool bValid = false;

	// Read the whole block, or leave the table invalid when any one key is missing. A partial table
	// is not usable: the file's own note says the order is load-bearing.
	static FElysiumMeleeMargins FromRules(const FElysiumRules& Rules);
};

// The attacker half of the classifier. "Everything else registers as a hit for the attacker."
enum class EElysiumMeleeAttackerReaction : uint8
{
	Unclassified = 0,   // no margin table — the fail-safe, never a band
	Hit,
	Blocked,
	BlockedMajor,
};

// The defender half — the five-way classifier at `0x103498B0`. Class 3 (`BlockStagger`) is the one
// that plays `ACT_BLOCK_HEAVY`; there is no separate stagger meter.
enum class EElysiumMeleeDefenderReaction : uint8
{
	Unclassified = 0,
	DodgeAttack,
	Dodge,
	Block,
	BlockStagger,
	HitKnockback,
};

// ================================================================================================
// The rulebook halves the weapon transactions need
// ================================================================================================
//
// Gathered once per transaction so the controller stays free of the subsystem and runs headless.
// Every member may be absent; each consumer then fails safe and says so once.

struct FElysiumWeaponContext
{
	const FElysiumFeatTable*  Feats = nullptr;
	const FElysiumDiceTables* DiceTables = nullptr;

	// `rules.txt` -> `RuleData/Damage_Info`. INDEX_NONE means the rulebook did not answer.
	int32 DefenseDifficultyPc = INDEX_NONE;
	int32 DefenseDifficultyNpc = INDEX_NONE;
	int32 SoakDifficultyPc = INDEX_NONE;
	int32 SoakDifficultyNpc = INDEX_NONE;

	FElysiumMeleeMargins Margins;

	// `rules.txt` -> `RuleData/Knockbacks/KnockbackPreventTime` is deliberately NOT read here. It is
	// the PLAYER's view-kick refractory — its only consumers are the player's own knockback reaction
	// and its ordinary hit reaction — and not a victim re-knockback window. That system is not built
	// in this slice, so nothing in the melee transaction reads the value.

	bool HasDefenseDifficulties() const
	{
		return DefenseDifficultyPc != INDEX_NONE && DefenseDifficultyNpc != INDEX_NONE;
	}
	bool HasSoakDifficulties() const
	{
		return SoakDifficultyPc != INDEX_NONE && SoakDifficultyNpc != INDEX_NONE;
	}

	static FElysiumWeaponContext FromCharacter(const FElysiumCombatCharacter& Char);
};

// ================================================================================================
// The pure rules
// ================================================================================================

namespace ElysiumWeapons
{
	// The recovered `ACT_MELEE_ATTACK_2COMBO` chance table, indexed by the controlling base Ability
	// rank. A rank outside 0..5 clamps to the ends, which is what the table's own bounds mean.
	int32 ComboChancePercent(int32 BaseRank);

	// The melee primary's airborne fork, as a predicate over the player's ideal activity.
	//
	// `CWeaponMelee::PrimaryAttack` asks one helper whether to request `ACT_MELEE_AIR_ATTACK`
	// instead of `ACT_MELEE_ATTACK`, and that helper is a switch on the player's ideal activity —
	// five airborne entry activities plus the air attack itself. It is NOT a ground-flag test, and
	// the difference is observable: a body off the ground for a reason outside this set — riding a
	// lift, mid-landing, a frame mid-teleport — takes the grounded swing.
	//
	// Matched by NAME. The registered activity IDs this runtime carries are the binary's own
	// registration numbers, not the compiled enum slots the recovered switch's cases are, so a
	// numeric join would compare two different vocabularies.
	//
	// An empty activity is no match, which is the grounded answer a body with no published
	// classification gets.
	bool IsAirborneMeleeActivity(const FString& IdealActivity);

	// The player attack sequence's playback rate: `0.70 + 0.03 * evaluated attack-feat rank`. The
	// character's base attack-rate scalar multiplies this in retail; no shipped `stats.txt` trait or
	// `feats.txt` feat carries such a scalar, so it is 1.0 here — see `AttackSpeedScale`.
	float MeleePlaybackRate(int32 AttackFeatRank);

	// SEAM — the owner's attack-speed value, which retail scales `Attack_Rate` and the melee
	// playback rate by. Nothing in the shipped `vdata` authors an attack-speed trait, feat or
	// trait-effect flag, so this is 1.0 until one is recovered; a discipline that changes attack
	// speed joins here rather than at each call site.
	float AttackSpeedScale(const FElysiumCombatCharacter& Owner);

	// The active Potence rank the melee commit floors `DamageInflicted` up to — the
	// `Active_Potence` sheet slot, read through the discipline runtime. 0 when the power is not up.
	int32 ActivePotenceRank(const FElysiumCombatCharacter& Attacker);

	// The ranged per-victim lethality after step 3's clamp and step 4's Kindred-only defence
	// subtraction: `max(max(total, 1) - (kindred ? max(defense, 0) : 0), 0)`. A mortal victim rolls
	// no defence at all — that asymmetry is the recovered behaviour, not an optimisation.
	int32 RangedRemainingLethality(int32 InTotalLethality, bool bKindredVictim, int32 DefenseNet);

	// --- The two post-soak damage formulas (`combat-and-damage.md` § RE40 -> Ranged and Melee
	// Damage Post-Soak Operations) ---------------------------------------------------------------
	// Both truncate toward zero, which is retail's own `__ftol` conversion at the point a float
	// damage value is turned into the integer the health commit spends.

	// `Total = DamageInflicted * (BaseDamage + DamageModifier) * Multiplier`. `DamageModifier` is the
	// attacker's rating for the descriptor's own close-combat attack feat (Brawl or Melee); the
	// Potence floor is applied to `DamageInflicted` before this is called.
	int32 MeleeDamageTotal(int32 DamageInflicted, int32 BaseDamage, int32 DamageModifier,
		float Multiplier);

	// `Total = RemainingLethality * BaseDamage * Multiplier`, the ranged multiplier being
	// `Volley_Fraction * Hitgroup_Scale`. The firearm attack feat scales ACCURACY, not this product,
	// which is why no rating enters here.
	int32 RangedDamageTotal(int32 RemainingLethality, int32 BaseDamage, float Multiplier);

	// The volley hit share: the fraction of a scheduled shot's rays that reached one victim. The
	// volley path groups traces by victim and passes that share into `CTakeDamageInfo`; a shot that
	// placed no rays contributes nothing.
	float VolleyFraction(int32 RaysOnVictim, int32 RaysFired);

	// SEAM — the hitgroup scale is the other half of the ranged multiplier, and the hitgroup comes
	// from the trace the engine owns (K13). With no trace to report one, a hit is scored at the
	// unmodified body scale.
	inline constexpr float DefaultHitgroupScale = 1.0f;

	// SEAM — the melee multiplier is the `CTakeDamageInfo`/trace envelope's, and RE40 decomposes only
	// the ranged one. Melee therefore carries the envelope's identity until its own decomposition is
	// recovered.
	inline constexpr float DefaultMeleeMultiplier = 1.0f;

	// The two halves of the `rules.txt` classifier over a signed margin. Both return `Unclassified`
	// for an invalid table — the caller warns and skips the reaction rather than guessing a band.
	EElysiumMeleeAttackerReaction ClassifyAttacker(const FElysiumMeleeMargins& Margins, int32 Margin);
	EElysiumMeleeDefenderReaction ClassifyDefender(const FElysiumMeleeMargins& Margins, int32 Margin);

	const TCHAR* AttackerReactionName(EElysiumMeleeAttackerReaction Reaction);
	const TCHAR* DefenderReactionName(EElysiumMeleeDefenderReaction Reaction);

	// --- The sequence-event ids the weapon bodies claim ----------------------------------------
	//
	// `Operator_HandleAnimEvent` `+0x5c8` is not one policy. All 169 server weapon subclasses collapse
	// to seven bodies (`docs/vtmb/animation_and_movers.md` → "Sequence events and native dispatch"),
	// and the three wielded families `FElysiumWeapon` controls reach three of them:
	//
	//  * `0x10238160`, the 17 ranged classes — **3030..3044** commit the operator/global-mode-selected
	//    fire-state transition, which re-enters `ModeDispatch` `0x102383b0` in event mode and calls
	//    the weapon's shot virtual `CWeaponRanged::Shot` `0x102387b0`. The event chooses the instant;
	//    the shot body spends ammunition and builds the fire packet.
	//  * `0x103ea5b0`, the 29 common-melee classes — **3047** is the NPC swing trigger, and 3001,
	//    3003 and 3030..3037 are SWALLOWED: accepted and acted on by nothing, so a melee clip carrying
	//    a swish or a ranged id does not fall through to a warning. **3047 commits nothing**: the
	//    melee contact is the per-frame swept walk over the clip's authored swing records, and the
	//    trigger's own consumer — an NPC asking its weapon for a swing from inside a clip — is not
	//    built, so the id is claimed and reported once rather than wired to the damage spine.
	//  * `0x1024f030`, the 17 base/discipline/armor/**thrown**/unarmed classes — **no accepted
	//    route**. A thrown weapon is not a slow firearm: nothing in the band commits it, and this
	//    runtime must not hand it the ranged body's ids merely because it is not melee.
	//
	// The remaining four bodies are unreachable from here. Tzimisce melee `0x103e8be0`, Ming melee
	// `0x103ec460` and Ming tentacle `0x103eca20` are per-class overrides of the common melee body
	// with no shipped item record of their own, and the 103 inventory/non-combat classes
	// `0x103f4470` covers are plain `FElysiumItem`s in this runtime — `IsControllableWeapon` gives
	// them no controller, so no record ever reaches this file. Every body's 4001/4002 bodygroup route
	// is presentation and is claimed by none of them here.
	inline constexpr int32 RangedShotEventFirst = 3030;
	inline constexpr int32 RangedShotEventLast  = 3044;
	// Retail's NPC swing trigger on the common-melee body. Claimed, and deliberately not a commit.
	inline constexpr int32 MeleeSwingTriggerEvent = 3047;

	// Which of the three the record's authored `item_type` selects.
	enum class EOperatorBody : uint8
	{
		None = 0,   // `0x1024f030` — thrown, and any record with no readable type
		Ranged,     // `0x10238160`
		Melee,      // `0x103ea5b0`
	};

	// The assignment, taken from the authored type and from nothing else (K-rule: never the classname
	// prefix). A record that does not parse answers `None`, which accepts nothing — the fail-safe
	// that matches the body retail gives every class it has no route for.
	EOperatorBody OperatorBodyFor(EElysiumItemType Type);
	const TCHAR* OperatorBodyName(EOperatorBody Body);

	// The id that commits THIS body's transaction. Ranged's is any of the fifteen shot ids, because
	// the fifteen name shot slots across the weapon classes rather than distinct actions. `Melee`
	// and `None` commit on NOTHING: a melee contact is the swept walk over the clip's own authored
	// swing records, so no id in the band names its instant.
	bool IsCommitEvent(int32 Event, EOperatorBody Body);

	// Retail's NPC swing trigger on the common-melee body (3047). Claimed here, and reported once
	// rather than wired: its consumer — an NPC asking its weapon for a swing from inside a clip —
	// is not built, and committing on it would put a second melee route beside the contact walk.
	bool IsMeleeSwingTrigger(int32 Event);

	// The common-melee body's swallow set — claimed, and deliberately without effect.
	bool IsSwallowedMeleeEvent(int32 Event);

	// Whether a clip's own timeline names this body's commit instant, which is what decides between
	// the event route and the `ContactEventCycle` estimate below.
	bool TimelineHasCommitEvent(const TArray<FElysiumAnimEvent>& Timeline, EOperatorBody Body);

	// The class-registry factory for every weapon-family item classname.
	TUniquePtr<FElysiumEntity> MakeWeapon();

	// --- Stated interim constants -------------------------------------------------------------
	// Each of these stands in for a value the export does not yet carry. They are named, not
	// scattered, so the RE that closes one lands as a single replacement.

	// The clip cycle the SHOT commit enters at when — and only when — the clip's own timeline cannot
	// supply it: a headless world, a body whose pose layer publishes no phase for the polled channel,
	// or a resolved shot clip whose sequence declares no `IsCommitEvent` id. Retail never estimates,
	// so every one of those is a DEGRADED path and the last of them says so once per clip. Mid-clip
	// is the stated stand-in; the recovery names the id, never a time.
	//
	// **It is the ranged path's alone.** A melee swing estimates no instant at all: its sequence
	// descriptor authors the contact window and `AdvanceSwingContact` walks it.
	inline constexpr float ContactEventCycle = 0.5f;

	// The swing duration used when no embodiment can resolve a clip — a headless run, a bodiless
	// character, or a model whose bank has no sequence for the activity. The mode's authored
	// `Attack_Rate` is preferred over this; the constant only covers a mode that authors none.
	inline constexpr float FallbackClipSeconds = 0.5f;

	// The melee query distance a swing falls back to. Retail takes the maximum custom reach float
	// (+0x2D0) over every sequence the translated activity returns, and the export carries that
	// field as the clip slice's `reach_cm` column — so the acquired swing asks the resolved clip for
	// it and this constant covers only the cases where no answer exists: a headless run, a body with
	// no vocabulary, or a row that authors no reach at all. Querying at a stand-in distance is a
	// DEGRADED path and says so once per weapon.
	// Source units — the one conversion is at the call site.
	inline constexpr float MeleeReachSourceUnits = 64.0f;

	// `FindEntityFOV`'s 30-degree half-angle (a 60-degree full cone), as the dot product the
	// acquisition cone tests against.
	inline constexpr float MeleeConeHalfAngleDegrees = 30.0f;

	// --- The opposed roll's own opponent query -------------------------------------------------
	// `MeleeRollAndSendNoticeCallback` runs its OWN `FindEntityFOV` before any contact test, at a
	// fixed 60 Source units and a half-cone dot of 0.7 — not the acquisition query's authored
	// per-sequence reach and 30-degree cone. The two are separate retail calls asking separate
	// questions: acquisition reserves an opponent for aim assistance when the swing is accepted, and
	// this one selects whom the swing's opposed record is staged against on its first live frame.
	// Collapsing them would make the roll follow the aim reservation, which is not what the bytes do.
	inline constexpr float SwingRollReachSourceUnits = 60.0f;
	inline constexpr float SwingRollConeDot = 0.7f;

	// The ranged query distance a shot falls back to when its mode authors no `Range`. Retail's fire
	// packet carries the mode's own authored range; a record that states none leaves the aim query
	// with no distance to trace at all, which is the same degraded shape as the melee reach above and
	// reports the same way. Source units — the one conversion is at the call site.
	inline constexpr float RangedRangeSourceUnits = 1024.0f;
}

// ================================================================================================
// FElysiumWeapon — the controller
// ================================================================================================

// The three buttons a weapon frame reads, as this file's own vocabulary rather than the user
// command's. `FElysiumEntityWorld::UpdatePlayerWeaponFrame` translates the player's button field
// into a pair of these masks (what is HELD this frame, and what became held THIS frame); an AI
// producer never builds one, because a schedule task is already a decision rather than a button.
enum class EElysiumWeaponButton : uint8
{
	None      = 0,
	Primary   = 1 << 0,   // `+attack`
	Secondary = 1 << 1,   // `+attack2` / `+wpn_secondaryatk`'s composite half
	Reload    = 1 << 2,   // `+reload`
};
ENUM_CLASS_FLAGS(EElysiumWeaponButton);

class FElysiumWeapon : public FElysiumItem
{
public:
	// Which press this is. `+attack2` and `+wpn_secondaryatk` both reach the secondary route; the
	// dedicated block bit is a separate assertion and is not this enum's business.
	enum class EIntent : uint8 { Primary, Secondary };

	// What an attack intent did. Every outcome is reported, because "nothing happened" has several
	// distinct causes and a silent one would be indistinguishable from a broken transaction.
	enum class EVerdict : uint8
	{
		Accepted,        // a swing transaction is staged and its commit is queued
		Chained,         // a press inside the playing attack's hand-off window committed its successor
		Busy,            // the press reached the busy path and the playing attack refused it
		DryFire,         // the magazine could not pay `Ammo_Cost` — the empty-fire action ran
		NotReady,        // the next-attack deadline has not passed
		Reloading,       // a reload is live; the fire-intent interruption latch is set
		ModeToggled,     // `Toggle_Primary_Mode` swapped the primary modes
		ZoomCycled,      // a zoom-loop mode cycled its scope state
		NoMode,          // the record authors no mode for this press
		NoOwner,         // the weapon is loose, or its owner is gone/dead
		Unsupported,     // an authored mode type with no recovered consumer
		Idle,            // no button asked for anything this frame
	};

	static const TCHAR* VerdictName(EVerdict Verdict);

	// --- Fire-mode state ----------------------------------------------------------------------
	// Index into the record's `Modes` of the primary mode in force. `Toggle_Primary_Mode` swaps it
	// between the `Primary` and `PrimaryMode2` records.
	int32 PrimaryModeIndex = INDEX_NONE;
	int32 SecondaryModeIndex = INDEX_NONE;
	// The two authored primary records, in file order. Empty for a weapon that authors none.
	TArray<int32> PrimaryModeSlots;

	// The parsed `Dmg` of every mode, index-aligned with the record's `Modes`. Parsed once when the
	// entity spawns rather than on every swing.
	TArray<FElysiumDmg> ModeDamage;

	// --- Next-attack scheduling (absolute substrate seconds) -----------------------------------
	// `m_flNextPrimaryAttack` / `m_flNextSecondaryAttack`. Every write is a MAXIMUM operation: a
	// pre-existing later deadline is never shortened.
	double NextPrimaryAttackTime = 0.0;
	double NextSecondaryAttackTime = 0.0;

	// --- The accepted-swing transaction --------------------------------------------------------
	struct FSwing
	{
		bool bActive = false;
		// Bumped on every accepted swing. The queued commit carries it, so a commit that arrives
		// after the transaction was replaced or cleared is dropped instead of firing against a
		// stranger.
		int32 Serial = 0;
		int32 ModeIndex = INDEX_NONE;
		bool bMelee = false;
		// --- The attack's own identity across a combo chain -------------------------------------
		// `Serial` is bumped by every LINK, because the queued commit has to be able to tell one
		// link from the next. These two name the ATTACK instead: `ChainRoot` is the serial of the
		// press that opened it and holds still for the whole chain, and `ChainLink` counts 1, 2, 3
		// down the authored successors. Diagnostics only — the transaction reads neither, and neither
		// is saved: a load does not restore a body mid-clip, so a restored swing has no chain to name.
		int32 ChainRoot = 0;
		int32 ChainLink = 0;
		FString Activity;                  // the LOGICAL activity (`ACT_MELEE_ATTACK_2COMBO`, ...)
		FString ClipLabel;                 // the concrete clip the embodiment resolved, or empty
		// The stem that OWNS that clip — the body's own or the bank the include DAG named. It is the
		// bank a shared sequence actually came out of, which is the half of a missing-column report
		// the body's own stem cannot state; the contact that reports it arrives after the transaction
		// is cleared, so it is staged here rather than re-resolved.
		FString ClipOwnerStem;
		FElysiumEntityHandle Opponent;     // the aimed opponent, or Invalid
		float PlaybackRate = 1.0f;
		float ClipSeconds = 0.0f;
		// RANGED ONLY. The `ContactEventCycle` estimate's instant: what the transaction would have
		// committed at, always recorded so a diagnostic can read it, scheduled only when
		// `bAwaitingAnimEvent` is clear. A melee swing leaves it at zero — it estimates nothing.
		double CommitTime = 0.0;
		double RecoveryDeadline = 0.0;
		// RANGED ONLY. Set when the playing shot clip's own timeline names 3030..3044 and the body
		// publishes a phase for the dispatcher to walk — so the commit will arrive from
		// `OperatorHandleAnimEvent` and no estimate was queued. A transaction whose clip is cut short
		// before its event then commits nothing, which is retail's own shape: the shot simply misses.
		bool bAwaitingAnimEvent = false;

		// --- The melee contact walk's state (see `AdvanceSwingContact`) --------------------------
		// All of it is TRANSIENT mid-swing state and none of it is saved, for the same reason
		// `FElysiumCombatCharacter::MeleeRolls` is not: it exists between one frame of a playing clip
		// and the next. A restored swing therefore meets its clip afresh — and in practice never
		// does, because a load does not restore a body mid-clip, so the transaction expires at its
		// recovery deadline having committed nothing. That is the same outcome retail gives any swing
		// whose clip is cut short.

		// Whether this swing's opposed record and incoming-swing notice have been staged. Both are
		// staged ONCE per swing, on its first BATCHED frame, before any contact test — a frame that
		// runs no batch records nothing at all, which is the point of the accumulator below.
		bool bContactStaged = false;
		// Elapsed time the walk has been handed and not yet walked. A frame shorter than one sub-step
		// leaves it here rather than discarding it, so the batch that does run covers everything the
		// clip advanced through since the last one (`ElysiumSwing::SubStepSeconds`).
		float PendingSeconds = 0.0f;
		// The play the walk is following. A clip re-armed is a new play whose window walk starts over,
		// which is the same discriminator the animation-event cursor keys on. Zero is "the walk has
		// not met a play yet".
		uint32 WalkPlayId = 0;
		// The cycle the last batch left off at. Negative is "not yet primed": the first batch has no
		// previous position and covers only the instant it stands on.
		float PrevCycle = -1.0f;
		// Per authored record, the victims it has already landed on. Index-aligned with the playing
		// clip's records; a record's list clears on any batch whose span leaves its window closed,
		// which is what lets a `2COMBO`'s two disjoint groups land twice.
		TArray<TArray<FElysiumEntityHandle>> RecordHits;
		// The contact segments as of the last batch, in the ATTACKER's local frame and index-aligned
		// with the records. The sweep needs where the limb was as well as where it is, and a bone
		// query answers only for now.
		TArray<TPair<FVector, FVector>> PrevSegmentsLocal;
		// The attacker's own frame as of the last batch, so the sub-steps can interpolate the root
		// the segments are carried by.
		FVector PrevOrigin = FVector::ZeroVector;
		FRotator PrevAngles = FRotator::ZeroRotator;

		// Give up the walk's CURSOR — everything that describes where on a clip it was. Called
		// whenever the play it was following changes or the clip stops being live, so a re-armed
		// clip cannot inherit a stale hit list or sweep from where a different play left the limb.
		//
		// `bContactStaged` is deliberately NOT reset: the opposed roll is staged once per SWING, not
		// once per play, and a clip that lost the channel and took it back must not roll twice. The
		// whole transaction ending is what clears it, through `ClearSwing`.
		void ResetWalk()
		{
			WalkPlayId = 0;
			PendingSeconds = 0.0f;
			PrevCycle = -1.0f;
			RecordHits.Reset();
			PrevSegmentsLocal.Reset();
			PrevOrigin = FVector::ZeroVector;
			PrevAngles = FRotator::ZeroRotator;
		}

		// Re-prime the walk's POSITION only, after an engine discontinuity: the next batch starts
		// from where the limb actually is instead of sweeping through wherever it used to be. The
		// hit lists survive, deliberately — a teleport is not a reason for a record whose window is
		// still open to land a second time on a body it already hit.
		void RePrimePosition()
		{
			PrevCycle = -1.0f;
			PrevSegmentsLocal.Reset();
			PrevOrigin = FVector::ZeroVector;
			PrevAngles = FRotator::ZeroRotator;
		}
	};
	FSwing Swing;

	// --- The reload transaction ----------------------------------------------------------------
	bool bReloading = false;
	int32 ReloadSerial = 0;
	double ReloadEndTime = 0.0;
	// Fire intent arriving while a reload is live. `reload_single` reads it to stop re-entering; an
	// ordinary bulk reload finishes regardless, which is what "the reload frame finishes the
	// transaction before normal firing resumes" means.
	bool bFireIntentDuringReload = false;

	// --- The controller surface ----------------------------------------------------------------
	// One attack press. `Victim` is the explicit ranged target: the shot's world trace and spread
	// cone are a producer that joins with the perception and player-crosshair cycles, so the
	// transaction takes the handle rather than inventing a trace. Melee ignores it and acquires its
	// own opponent.
	EVerdict AttackIntent(EIntent Intent,
		const FElysiumEntityHandle& Victim = FElysiumEntityHandle::Invalid());

	// `CWeaponMelee::ItemPostFrame` (`0x103EAEC0`) / `CWeaponRanged`'s shared frame: one frame of
	// button state turned into at most one weapon transaction. `Held` is what is down now; `Pressed`
	// is what became down this frame. `AimTarget` is the ranged victim the caller acquired, and is
	// ignored by melee, which acquires its own opponent.
	//
	// The order below — empty record, primary, secondary, reload — is CHOSEN (RE-A2). Nothing is
	// recovered about the relative order of the four tests, so this runtime states one: primary
	// before secondary because a firearm's secondary is frequently a mode toggle that would otherwise
	// eat the frame, and reload last because it is the only one that cannot produce a swing.
	//
	// Returns the FIRST non-`Idle` verdict, so the caller reads what the frame did rather than what
	// the last test happened to answer. `Idle` means no button asked for anything.
	EVerdict ItemPostFrame(EElysiumWeaponButton Held, EElysiumWeaponButton Pressed,
		const FElysiumEntityHandle& AimTarget = FElysiumEntityHandle::Invalid());

	// The reload request. Starts only with reserve ammunition, reload permitted, and a magazine
	// missing capacity. Returns whether a reload transaction began.
	bool BeginReload();

	// The queued halves. Public because the registered input thunks are free functions.
	void CommitQueuedAttack(int32 Serial);
	void CommitQueuedReload(int32 Serial);

	// **The melee contact.** One frame of the swept walk over the staged swing's own authored
	// records, driven for every character with a live melee transaction — the player's and every
	// NPC's alike, because retail runs it on the CHARACTER rather than on the player's input path.
	//
	// Its whole shape is the clip's: the swing is live for as long as the playing attack clip
	// declares records, the elapsed span is divided into `floor(span * 100)` sub-steps, and each
	// record is swept on the sub-steps its authored window overlaps. Nothing here is scheduled and
	// nothing is estimated. The rules are `Substrate/ElysiumSwingContact.h`; this is their producer
	// half, and the bone transforms and the sweep it needs are engine services (K13).
	//
	// A frame that records nothing at all is the ordinary case: a span below one sub-step is
	// accumulated rather than walked, and the batch that follows covers it. A swing whose clip
	// declares no records is the other, and it never opens a contact window.
	void AdvanceSwingContact(float DeltaSeconds);

	// Retail's virtual `Operator_HandleAnimEvent` `+0x5c8`: one sequence-event record the OPERATOR's
	// clip declared, forwarded here by `FElysiumCombatCharacter::HandleAnimEvent` because it fell in
	// the 3000..3999 weapon band. True means this weapon claimed the id.
	//
	// An id in the band this family does not claim answers FALSE, which puts it on the unclaimed-id
	// census (`Substrate/ElysiumAnimEvents.h`) rather than into a log line per occurrence. Retail's
	// own melee and base bodies warn there instead; the census is this runtime's report for the same
	// fact, and it is a work list rather than a fault.
	bool OperatorHandleAnimEvent(FElysiumCombatCharacter& Operator, const FElysiumAnimEvent& Event);

	// Every write to the two deadlines goes through here: a MAXIMUM operation, never a shortening
	// write. Public because a later owner — a discipline, a scripted beat, an AI schedule — holds a
	// weapon the same way, and must not reach the fields directly.
	void HoldAttacksUntil(double Deadline);

	// How many swing transactions this weapon has ever accepted. `Swing.Serial` is the LIVE
	// transaction's and returns to zero the moment a commit clears it, so a caller counting accepted
	// presses — a diagnostic readout, an assertion — has to read this instead.
	int32 AcceptedSwingCount() const { return SwingSerialCounter; }

	// Whether this frame's buttons would run each attack route — the press edge, `allow_autofire`'s
	// held poll and the secondary's chosen press edge, which is exactly what `ItemPostFrame` branches
	// on below. Public because the player weapon frame has to know THAT a shot is about to leave
	// before it can supply the aim query's answer, and a second copy of the button rule in the caller
	// is how the button route and the aim route come to disagree.
	bool WantsPrimaryPress(EElysiumWeaponButton Held, EElysiumWeaponButton Pressed) const;
	bool WantsSecondaryPress(EElysiumWeaponButton Pressed) const;

	// The aim query's distance for a press about to run on `Intent`, in centimetres — the mode's
	// authored `Range`, or the stated `RangedRangeSourceUnits` stand-in with one report per
	// (classname, mode) when a FIRING record authors none. Zero, silently, when the intent names
	// no mode at all or names one that does not fire (a mode toggle, a zoom loop): neither is a
	// press a shot leaves on, so neither has an aim to trace or a missing `Range` to report.
	float AimQueryRangeCm(EIntent Intent) const;

	// The mode currently in force for a press, or null.
	const FElysiumWeaponMode* ModeFor(EIntent Intent) const;
	const FElysiumWeaponMode* ModeAt(int32 Index) const;
	// The parsed descriptor for a mode index, or a family-less descriptor.
	const FElysiumDmg& DamageForMode(int32 Index) const;

	// The owner as a combat character, or null (a loose weapon, or an owner that is gone).
	FElysiumCombatCharacter* OwnerCharacter() const;
	// Whether this weapon is the owner's active weapon. The commit re-validates it.
	bool IsActiveWeapon() const;

	// `total_lethality = max(BaseLethality + attacker adjustment, 0)`, the adjustment being the
	// descriptor's attack feat applied to the attacker. Rounded; the ranged path clamps to >= 1.
	int32 TotalLethality(int32 ModeIndex, const FElysiumCombatCharacter& Attacker,
		const FElysiumWeaponContext& Context) const;

	virtual void Spawn() override;
	virtual void Serialize(FElysiumSaveArchive& Ar) override;
	virtual void OnEquipped(FElysiumCombatCharacter& Wearer) override;
	virtual void OnHolstered(FElysiumCombatCharacter& Wearer) override;
	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override;
	virtual FElysiumWeapon* AsWeapon() override { return this; }

private:
	// The two halves of an accepted swing.
	EVerdict BeginMeleeSwing(EIntent Intent, int32 ModeIndex, const FElysiumWeaponMode& Mode);

	// --- The busy path: what a melee primary press does DURING an attack ------------------------
	//
	// Retail's melee frame does not simply refuse a press while an attack runs. A press arriving
	// while the weapon is busy — the next-attack deadline has not passed, or the playing attack's own
	// busy predicate still holds — takes this route instead of starting a new swing, and it is where
	// the combo lives (`docs/vtmb/combat-and-damage.md` § "Melee attack, combo, block and damage").
	//
	// **It is the player's alone, and not by a player test.** The route is reached from
	// `ItemPostFrame`'s primary PRESS EDGE and from nowhere else; an AI producer calls `AttackIntent`
	// from a schedule task, which is already a decision rather than a button, so no NPC ever produces
	// the edge this path hangs off — the gate is the edge itself.

	// Whether this frame's primary press meets a busy weapon: a live melee transaction, and either
	// the next-attack deadline still standing or the playing attack's own predicate still holding.
	bool IsMeleePressBusy() const;

	// Whether the playing attack still refuses a new swing on its own terms, apart from the clock.
	// The three arms are `ElysiumCombo::IsBusy`'s; what this adds is reading the LIVE clip's cycle and
	// the value that clip's own descriptor states for the hold.
	bool IsMeleeBusy(double Now) const;

	// One press on the busy path: commit the playing clip's authored successor when the press lands
	// inside its hand-off window, and otherwise ignore it. There is no queue and no restart — a press
	// outside the window, on a terminal attack, or on a chain whose target the body's vocabulary does
	// not name is simply spent.
	EVerdict MeleeBusyPress();

	// The hand-off itself. The successor plays from cycle zero at the SAME playback rate, the logical
	// activity stays `ACT_MELEE_ATTACK`, the next-attack deadline is NOT pushed again, and the swing
	// transaction is restaged exactly as an accepted swing is — a fresh serial, a fresh staged roll
	// and a walk that starts over on the new clip.
	void CommitMeleeChain(FElysiumCombatCharacter& Char, const FString& ChainLabel,
		const FString& ChainOwnerStem);

	EVerdict BeginRangedShot(EIntent Intent, int32 ModeIndex, const FElysiumWeaponMode& Mode,
		const FElysiumEntityHandle& Victim);
	// The accepted swing's own identity, copied out of the transaction before the first contact
	// commits. A contact can retire the transaction, so the walk must not read the live one back
	// through a reference into it — and the values are one thing, not a parameter list.
	struct FSwingContact
	{
		int32 ModeIndex = INDEX_NONE;
		// The accepted-swing serial. It is what scopes the opposed record: a record stamped with any
		// other serial belongs to an earlier swing of this same attacker and is refused.
		int32 Serial = 0;
		// The concrete clip the swing resolved, and the bank that owns it. The attacker's blocked
		// reaction is the one the SWING's own sequence descriptor stores, so the contact cannot ask
		// the weapon what it happens to be playing by then.
		FString ClipLabel;
		FString ClipOwnerStem;
		// WHICH authored swing record landed this contact, indexing the array `NpcClipSwings`
		// answered for `(ClipOwnerStem, ClipLabel)`. Unlike the four values above it is per CONTACT
		// rather than per swing: one swing's records are walked independently and any of them may be
		// the one that reached a body.
		//
		// It is the record's identity and not a copy of it, because the array is owned by the clip
		// vocabulary and outlives the walk; a contact re-reads it rather than carrying the row.
		//
		// The knockback the victim answers with is stated ON that record — four direction buckets of
		// candidate activities, the `+0xB8` rotation byte and the `+0xBA == 2` unconditional marker
		// (`docs/vtmb/combat-and-damage.md` → "The authored table lives in the swing record"). Without
		// this index the contact knows a record landed but not which, so the authored table cannot be
		// read at all.
		//
		// `INDEX_NONE` is a real answer, not an error: a contact that reached a body through no
		// authored record has no candidate table and resolves its cell the way a ranged or discipline
		// entry does.
		int32 RecordIndex = INDEX_NONE;
	};

	// **The opposed record is CONSUMED here, never rolled here.** It was staged on the victim on the
	// swing's first batched frame by `StageSwingOpposedRoll` below, which is where retail rolls it.
	// A victim the sweep reached with no record of THIS swing's serial is an ordinary negative — the
	// roll's own 60-unit query did not select them — and commits nothing.
	void MeleeContact(FElysiumCombatCharacter& Attacker, FElysiumCombatCharacter& Victim,
		const FSwingContact& Contact);

	// The authored swing record `Contact.RecordIndex` names, or null.
	//
	// Re-read through the embodiment rather than carried on the contact: the record array belongs to
	// the clip vocabulary, which is cached whole and immutable for the run, so the index is the
	// durable handle and a copied row would only be a second truth about the same bytes.
	//
	// Null on every ordinary absence — no index stamped, no embodiment, a clip that declares no
	// records, an index the array does not hold. A caller that gets null has a contact with no
	// authored candidate table, which is the same position a ranged or discipline entry is in.
	const FElysiumSwingRecord* ResolveSwingRecord(const FElysiumCombatCharacter& Attacker,
		const FSwingContact& Contact) const;

	// The swing's first batched frame, BEFORE any contact test: select the opponent with the roll's
	// own `FindEntityFOV` (60 Source units, half-cone dot 0.7), stage the opposed record on them
	// under this swing's serial, and send the incoming-swing notice. Retail's
	// `MeleeRollAndSendNoticeCallback`, and the reason the contact above has a record to consume.
	//
	// It stages at most one record per swing. Finding nobody is ordinary and is not retried on later
	// frames: retail rolls once, and a swing that opened on empty air stays opened on empty air.
	void StageSwingOpposedRoll(FElysiumCombatCharacter& Attacker, const FSwingContact& Contact);
	void RangedImpact(FElysiumCombatCharacter& Attacker, FElysiumCombatCharacter& Victim,
		int32 ModeIndex);

	// The grounded knockback branch of an UNBLOCKED melee contact the margin classified into the
	// hit/knockback band: test the victim's eligibility, classify the away direction, snap the
	// victim's facing so the authored cell reads true, and play that cell as a base-channel
	// reaction. The rules are `Substrate/ElysiumReactions.h`; this is their producer half.
	//
	// **No authored per-weapon chance and no refractory window.** The margin band is the whole
	// admission — `knockback_chance` is parsed by retail and never read, and `KnockbackPreventTime`
	// belongs to the player's view kick.
	//
	// Nothing here is a failure: a dead victim, a template that disallows knockbacks, a player victim
	// (whose reaction is the unbuilt view kick) and a body whose vocabulary carries no such cell are
	// all ordinary answers, and the last of them is named on the resolver's own selection record.
	// `Record` is the authored swing record that landed the contact, or null when the entry carries
	// none. It is what the cell is selected out of: four direction buckets of up to four candidate
	// activities, rotated by the record's own `+0xB8` byte
	// (`docs/vtmb/combat-and-damage.md` → "The authored table lives in the swing record").
	void KnockbackContact(FElysiumCombatCharacter& Attacker, FElysiumCombatCharacter& Victim,
		const FElysiumSwingRecord* Record);

	// `CWeaponRanged::FireOnEmpty` — the mode's dry-fire action, which advances BOTH attack timers.
	void FireOnEmpty(int32 ModeIndex, const FElysiumWeaponMode& Mode);

	// Resolve a logical activity to a concrete clip on the owner's body and start it. Returns the
	// clip's duration; falls back to the mode's `Attack_Rate` (then `FallbackClipSeconds`) with one
	// warning per weapon when no embodiment/body can answer. `OutOwnerStem` receives the bank the
	// include DAG named, which is the other half of the key a timeline is looked up by, and
	// `OutMaxReachCm` the acquisition distance the TRANSLATED activity asks for — the maximum over
	// every sequence answering it, which is the query distance and not the played clip's own.
	//
	// **`Band` is stated by the caller and has no default, because the melee and layer families do
	// not mean the same thing by a play.** A melee swing REPLACES the base pose, so it claims the
	// base channel above a travelling body's own locomotion publish; a ranged, dry-fire or reload
	// activity is a layer over an untouched gait ladder, so its stand-in claim has to yield to that
	// publish exactly as retail's untouched ladder does. One default here would silently give one of
	// the two families the other's behaviour (`docs/vtmb/animation_and_movers.md` § "Melee replaces
	// the base; ranged and reload only add a layer").
	//
	// `PlaybackRate` is `m_flPlaybackRate`, written onto the play. It defaults to 1.0 — retail's
	// `ResetSequenceInfo` resets it before `RequestActivity` writes anything, so a family that
	// authors no rate really does play at the authored speed. Only the melee family recovers a rate
	// today, and the returned length is the clip's AUTHORED one either way, so a caller timing its
	// schedule divides by the rate exactly once.
	// `bRequirePlayerStateMask` selects the PLAYER arm of retail's melee sequence selector (vtable
	// slot 331 on the owner): direction-keyed selection only, no weighted fallback, and a miss is a
	// miss. It is the melee attack path's and the player's — see
	// `FElysiumActivityClipRequest::bRequireStateMask` — so every other caller leaves it false and
	// gets the cast arm, which is what an NPC swing takes.
	//
	// `Channel` is stated by every caller and has no default, because the two attack families
	// disagree about what a clip IS: a melee swing is the base pose and a ranged fire or a player
	// reload is retail's `CBaseAnimatingOverlay` slot 0, a masked partial-body layer whose unowned
	// bones decode to a zero quaternion and a zero position. A layer defaulted onto the base channel
	// collapses the body, so the answer is required rather than inherited.
	float ResolveAndPlay(const FString& Activity, EElysiumAnimPriority Band,
		EElysiumAnimChannel Channel, const FElysiumWeaponMode& Mode, FString& OutClipLabel,
		FString* OutOwnerStem = nullptr, float* OutMaxReachCm = nullptr,
		float PlaybackRate = 1.0f, bool bRequirePlayerStateMask = false);

	// The owner's half of an activity request, filled from the CHARACTER rather than from this
	// weapon: one `FElysiumWeapon` is held by a player and by a combatant, and the classname, the
	// equipped weapon and the state that select a translation are all the holder's. One owner for the
	// shape, because the probe below and `ResolveAndPlay` must ask the same question.
	bool BuildActivityClipRequest(FElysiumCombatCharacter& Char, const FString& Activity,
		FElysiumActivityClipRequest& Out) const;

	// What one activity WOULD swing at, committing nothing: the acquisition reach and the name the
	// vocabulary was finally searched for.
	//
	// It exists because `CWeaponMelee::RequestActivity` acquires BEFORE it commits a sequence, and
	// the automatic `2COMBO` promotion can be refused by that acquisition — so the reach has to be
	// answerable without a clip already playing. Resolution is pure and spends no RNG, so probing an
	// activity and then playing it selects the same clip twice rather than two different ones.
	//

	// Which `Operator_HandleAnimEvent` body this weapon's authored record selects.
	ElysiumWeapons::EOperatorBody OperatorBody() const;

	// Whether the accepted transaction's commit will arrive from the playing clip's own timeline,
	// which is what stands the `ContactEventCycle` estimate down. Three things have to hold: this
	// weapon's operator body accepts a commit id at all, the clip's sequence declares that id, and
	// the character publishes a phase for THAT clip on a channel the event pass polls — a timeline
	// nothing walks would silently swallow the whole attack.
	bool CommitArrivesFromAnimEvent(FElysiumCombatCharacter& Char, const FString& OwnerStem,
		const FString& ClipLabel);

	// Queue the staged transaction's commit for this frame's queue service, from the sequence event
	// that named the instant. Claimed even with nothing staged — an idle or aim clip carrying a shot
	// id is retail's own case, and its mode dispatch finds no attack to commit either.
	bool CommitFromAnimEvent(const FElysiumAnimEvent& Event);

	// `FindEntityFOV` reduced to what the substrate owns: the nearest live combat character inside
	// `ReachCm` and the caller's own cone. The reach is the caller's because it is the
	// resolved activity's, not this weapon's — a value of 0 or less takes the stated
	// `MeleeReachSourceUnits` stand-in and reports once. SEAM — retail traces forward first and
	// accepts a valid obstruction hit, and the shared query rejects non-targetable/`ScriptHidden`
	// candidates through an engine visibility test; that trace is an engine query and joins with the
	// perception cycle. Selection is distance plus facing until then.
	//
	// `ConeDot` is the cosine of the half-angle the caller's own query uses: the acquisition call
	// passes `MeleeConeHalfAngleDegrees`' cosine, and the opposed roll's call passes its own
	// `SwingRollConeDot`. Two retail calls, two shapes, one predicate.
	FElysiumEntityHandle AcquireMeleeOpponent(const FElysiumCombatCharacter& Attacker,
		float ReachCm, float ConeDot) const;

	// Schedule one of the two queued halves through the world's event queue.
	void QueueSelfInput(FName Input, int32 Serial, double Delay);

	void ClearSwing();
	// One warning per weapon ENTITY when clip resolution cannot answer, so a headless run reports the
	// fallback once instead of on every swing. Per entity rather than per clip because there is no
	// clip to key on — the fact being reported is that this weapon's owner resolved nothing at all.
	bool bReportedClipFallback = false;

	// One warning per weapon ENTITY that its shots leave with no dispersion (RE-A3). Per entity for
	// the same reason as the clip fallback: the fact being reported is about this weapon's whole
	// firing path, not about one mode or one press.
	bool bReportedNoSpread = false;

	// Monotonic per weapon. A restore raises them past whatever the restored transactions carry, so
	// a queued commit that survived the save can never collide with a serial minted after it.
	int32 SwingSerialCounter = 0;
	int32 ReloadSerialCounter = 0;
};
