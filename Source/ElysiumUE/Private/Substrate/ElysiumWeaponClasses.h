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
// rate, recovery deadline and the commit instant. The commit is a SEPARATE later event that can
// miss: it re-validates the owner, the weapon and the target, and only then stages the opposed
// record and spends damage. Retail enters that second half from a server animation event; nothing
// in this runtime raises one yet (the character bake places no `AnimNotify` and the clip manifest
// carries no `mstudioevent_t` rows), so the commit is scheduled on the one event queue at the
// clip's resolved event instant — the same scheduler-not-listener shape `Substrate/ElysiumFeed.cpp`
// uses, and the seam a real notify path replaces by delivering the same input instead.

#include "CoreMinimal.h"

#include "ElysiumEntity.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumItemClasses.h"

struct FElysiumDiceTables;
struct FElysiumFeatTable;
struct FElysiumRules;
struct FElysiumWeaponMode;

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

	// The two halves of the `rules.txt` classifier over a signed margin. Both return `Unclassified`
	// for an invalid table — the caller warns and skips the reaction rather than guessing a band.
	EElysiumMeleeAttackerReaction ClassifyAttacker(const FElysiumMeleeMargins& Margins, int32 Margin);
	EElysiumMeleeDefenderReaction ClassifyDefender(const FElysiumMeleeMargins& Margins, int32 Margin);

	const TCHAR* AttackerReactionName(EElysiumMeleeAttackerReaction Reaction);
	const TCHAR* DefenderReactionName(EElysiumMeleeDefenderReaction Reaction);

	// The class-registry factory for every weapon-family item classname.
	TUniquePtr<FElysiumEntity> MakeWeapon();

	// --- Stated interim constants -------------------------------------------------------------
	// Each of these stands in for a value the export does not yet carry. They are named, not
	// scattered, so the RE that closes one lands as a single replacement.

	// The clip cycle the contact/shot commit enters at. Retail takes the instant from a server
	// animation event (ranged ids 3030-3044); ordinary melee attack sequences carry
	// `event_count == 0` and their contact window is explicitly unrecovered. Mid-clip is this
	// runtime's stated stand-in until decoded `mstudioevent_t` rows reach the clip manifest.
	inline constexpr float ContactEventCycle = 0.5f;

	// The swing duration used when no embodiment can resolve a clip — a headless run, a bodiless
	// character, or a model whose bank has no sequence for the activity. The mode's authored
	// `Attack_Rate` is preferred over this; the constant only covers a mode that authors none.
	inline constexpr float FallbackClipSeconds = 0.5f;

	// The melee query distance. Retail takes the maximum custom reach float (+0x2D0) over every
	// sequence the translated activity returns; that field is not exported, so this stands in.
	// Source units — the one conversion is at the call site.
	inline constexpr float MeleeReachSourceUnits = 64.0f;

	// `FindEntityFOV`'s 30-degree half-angle (a 60-degree full cone).
	inline constexpr float MeleeConeHalfAngleDegrees = 30.0f;
}

// ================================================================================================
// FElysiumWeapon — the controller
// ================================================================================================

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
		DryFire,         // the magazine could not pay `Ammo_Cost` — the empty-fire action ran
		NotReady,        // the next-attack deadline has not passed
		Reloading,       // a reload is live; the fire-intent interruption latch is set
		ModeToggled,     // `Toggle_Primary_Mode` swapped the primary modes
		ZoomCycled,      // a zoom-loop mode cycled its scope state
		NoMode,          // the record authors no mode for this press
		NoOwner,         // the weapon is loose, or its owner is gone/dead
		Unsupported,     // an authored mode type with no recovered consumer
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
		FString Activity;                  // the LOGICAL activity (`ACT_MELEE_ATTACK_2COMBO`, ...)
		FString ClipLabel;                 // the concrete clip the embodiment resolved, or empty
		FElysiumEntityHandle Opponent;     // the aimed opponent, or Invalid
		float PlaybackRate = 1.0f;
		float ClipSeconds = 0.0f;
		double CommitTime = 0.0;
		double RecoveryDeadline = 0.0;
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

	// The reload request. Starts only with reserve ammunition, reload permitted, and a magazine
	// missing capacity. Returns whether a reload transaction began.
	bool BeginReload();

	// The queued halves. Public because the registered input thunks are free functions.
	void CommitQueuedAttack(int32 Serial);
	void CommitQueuedReload(int32 Serial);

	// Every write to the two deadlines goes through here: a MAXIMUM operation, never a shortening
	// write. Public because a later owner — a discipline, a scripted beat, an AI schedule — holds a
	// weapon the same way, and must not reach the fields directly.
	void HoldAttacksUntil(double Deadline);

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
	EVerdict BeginRangedShot(EIntent Intent, int32 ModeIndex, const FElysiumWeaponMode& Mode,
		const FElysiumEntityHandle& Victim);
	void MeleeContact(FElysiumCombatCharacter& Attacker, FElysiumCombatCharacter& Victim,
		int32 ModeIndex);
	void RangedImpact(FElysiumCombatCharacter& Attacker, FElysiumCombatCharacter& Victim,
		int32 ModeIndex);

	// `CWeaponRanged::FireOnEmpty` — the mode's dry-fire action, which advances BOTH attack timers.
	void FireOnEmpty(int32 ModeIndex, const FElysiumWeaponMode& Mode);

	// Resolve a logical activity to a concrete clip on the owner's body and start it. Returns the
	// clip's duration; falls back to the mode's `Attack_Rate` (then `FallbackClipSeconds`) with one
	// warning per weapon when no embodiment/body can answer.
	float ResolveAndPlay(const FString& Activity, const FElysiumWeaponMode& Mode,
		FString& OutClipLabel);

	// `FindEntityFOV` reduced to what the substrate owns: the nearest live combat character inside
	// the reach and the 30-degree half-angle cone. SEAM — retail traces forward first and accepts a
	// valid obstruction hit, and the shared query rejects non-targetable/`ScriptHidden` candidates
	// through an engine visibility test; that trace is an engine query and joins with the perception
	// cycle. Selection is distance plus facing until then.
	FElysiumEntityHandle AcquireMeleeOpponent(const FElysiumCombatCharacter& Attacker) const;

	// Schedule one of the two queued halves through the world's event queue.
	void QueueSelfInput(FName Input, int32 Serial, double Delay);

	void ClearSwing();
	// One warning per weapon entity when clip resolution cannot answer, so a headless run reports
	// the fallback once instead of on every swing.
	bool bReportedClipFallback = false;

	// Monotonic per weapon. A restore raises them past whatever the restored transactions carry, so
	// a queued commit that survived the save can never collide with a serial minted after it.
	int32 SwingSerialCounter = 0;
	int32 ReloadSerialCounter = 0;
};
