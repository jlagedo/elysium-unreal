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
// record and spends damage. Retail enters that second half from a server animation event on both
// sides — ranged event ids 3030-3044 re-enter mode dispatch, and the melee swing's 3047 reaches the
// melee hit applicator, which runs its own contact loop rather than the shared traced-impact
// virtual (`combat-and-damage.md` § RE40 -> Melee Swing Pipeline).
//
// THE CLIP'S OWN TIMELINE NAMES THE INSTANT; `ContactEventCycle` IS THE DEGRADED STAND-IN.
// `OperatorHandleAnimEvent` below is retail's `Operator_HandleAnimEvent` `+0x5c8`: the sequence
// event the playing clip declared arrives there and queues the commit with no delay. An accepted
// transaction schedules the `ContactEventCycle` estimate ONLY where that route cannot run — a
// headless world, a body whose pose layer publishes no phase, or a resolved attack clip whose
// timeline names no commit id. Either way the commit rides the one event queue as a self-input, so
// there is no private timer and no second scheduler (K11).

#include "CoreMinimal.h"

#include "ElysiumAnimEvent.h"
#include "ElysiumEntity.h"
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
	//  * `0x103ea5b0`, the 29 common-melee classes — **3047** invokes the melee virtual, and 3001,
	//    3003 and 3030..3037 are SWALLOWED: accepted and acted on by nothing, so a melee clip carrying
	//    a swish or a ranged id does not fall through to a warning.
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
	inline constexpr int32 MeleeContactEvent    = 3047;

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

	// The id that commits THIS body's transaction. Melee's commit is 3047 alone; ranged's is any of
	// the fifteen shot ids, because the fifteen name shot slots across the weapon classes rather than
	// distinct actions. `None` commits on nothing.
	bool IsCommitEvent(int32 Event, EOperatorBody Body);

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

	// The clip cycle the contact/shot commit enters at when — and only when — the clip's own timeline
	// cannot supply it: a headless world, a body whose pose layer publishes no phase for the polled
	// channel, or a resolved attack clip whose sequence declares no `IsCommitEvent` id. Retail never
	// estimates, so every one of those is a DEGRADED path and the last of them says so once per clip.
	// Mid-clip is the stated stand-in; the recovery names the id, never a time.
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
		// The stem that OWNS that clip — the body's own or the bank the include DAG named. It is the
		// bank a shared sequence actually came out of, which is the half of a missing-column report
		// the body's own stem cannot state; the contact that reports it arrives after the transaction
		// is cleared, so it is staged here rather than re-resolved.
		FString ClipOwnerStem;
		FElysiumEntityHandle Opponent;     // the aimed opponent, or Invalid
		float PlaybackRate = 1.0f;
		float ClipSeconds = 0.0f;
		// The `ContactEventCycle` estimate's instant. Always recorded, because it is what the
		// transaction WOULD have committed at and a diagnostic needs to read it; scheduled only when
		// `bAwaitingAnimEvent` is clear.
		double CommitTime = 0.0;
		double RecoveryDeadline = 0.0;
		// Set when the playing clip's own timeline names this family's commit id and the body
		// publishes a phase for the dispatcher to walk — so the commit will arrive from
		// `OperatorHandleAnimEvent` and no estimate was queued. A transaction whose clip is cut short
		// before its event then commits nothing, which is retail's own shape: the swing simply misses.
		bool bAwaitingAnimEvent = false;
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
	// `SwingClipLabel`/`SwingClipOwnerStem` are the cleared transaction's, captured by the caller
	// before `ClearSwing`: the attacker's blocked reaction is the one the SWING's own sequence
	// descriptor stores, so the contact cannot ask the weapon what it is currently playing.
	void MeleeContact(FElysiumCombatCharacter& Attacker, FElysiumCombatCharacter& Victim,
		int32 ModeIndex, const FString& SwingClipLabel, const FString& SwingClipOwnerStem);
	void RangedImpact(FElysiumCombatCharacter& Attacker, FElysiumCombatCharacter& Victim,
		int32 ModeIndex);

	// `CWeaponRanged::FireOnEmpty` — the mode's dry-fire action, which advances BOTH attack timers.
	void FireOnEmpty(int32 ModeIndex, const FElysiumWeaponMode& Mode);

	// Resolve a logical activity to a concrete clip on the owner's body and start it. Returns the
	// clip's duration; falls back to the mode's `Attack_Rate` (then `FallbackClipSeconds`) with one
	// warning per weapon when no embodiment/body can answer. `OutOwnerStem` receives the bank the
	// include DAG named, which is the other half of the key a timeline is looked up by.
	float ResolveAndPlay(const FString& Activity, const FElysiumWeaponMode& Mode,
		FString& OutClipLabel, FString* OutOwnerStem = nullptr);

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
	// the reach and the 30-degree half-angle cone. SEAM — retail traces forward first and accepts a
	// valid obstruction hit, and the shared query rejects non-targetable/`ScriptHidden` candidates
	// through an engine visibility test; that trace is an engine query and joins with the perception
	// cycle. Selection is distance plus facing until then.
	FElysiumEntityHandle AcquireMeleeOpponent(const FElysiumCombatCharacter& Attacker) const;

	// Schedule one of the two queued halves through the world's event queue.
	void QueueSelfInput(FName Input, int32 Serial, double Delay);

	void ClearSwing();
	// One warning per weapon ENTITY when clip resolution cannot answer, so a headless run reports the
	// fallback once instead of on every swing. Per entity rather than per clip because there is no
	// clip to key on — the fact being reported is that this weapon's owner resolved nothing at all.
	bool bReportedClipFallback = false;

	// Monotonic per weapon. A restore raises them past whatever the restored transactions carry, so
	// a queued commit that survived the save can never collide with a serial minted after it.
	int32 SwingSerialCounter = 0;
	int32 ReloadSerialCounter = 0;
};
