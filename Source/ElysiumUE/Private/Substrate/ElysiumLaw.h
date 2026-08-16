#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntityHandle.h"

// Cycle 10b — the player law channels: activity levels and their deadlines, the two witnessed
// incident consumers, the delayed police response, the Masquerade rate limiter and the
// pursuit/alert state machine.
//
// `docs/vtmb/player-entity.md` § "Law, Masquerade and world response" owns the behaviour, with the
// world-area half in `docs/vtmb/game_runtime.md` § "Zone legality" and
// `docs/vtmb/disciplines.md` § "World-area eligibility and transition teardown". The storage is
// `FElysiumLawState` + `FElysiumPoliceState` on `FElysiumPlayer` (`Public/ElysiumPlayer.h`); this is
// the third piece, in the `ElysiumStealth` shape — every rule is a PURE function over explicit
// inputs (no world, no services, no clock) so the whole matrix is assertable with nothing built,
// and the thin wiring half below is what a producer and the player think actually call.
//
// Two things this file deliberately does NOT do:
//
//   * it adds no console variable. Retail's `pl_min_act_timer` and the `debug_*` family are code
//     constants of the retail build, not `vdata` rows, so they land here as named constants with
//     their provenance — and this project's debug surface is Cog, never a cvar
//     (`Source/ElysiumUE/CLAUDE.md`);
//   * it owns no clock and no scheduler. Every deadline is an absolute substrate-clock time read
//     by the player's own 0.1 s think (K11).
//
// Where the recovered record is open, a **CHOSEN** rule states its reasoning and a **SEAM** names
// what would close it. Neither is ever presented as recovered.

class FElysiumCombatCharacter;
class FElysiumEntityWorld;
class FElysiumPlayer;
struct FElysiumLawState;
struct FElysiumPoliceState;
struct FElysiumVariant;

namespace ElysiumLaw
{
	// ------------------------------------------------------------------------------------------
	// Retail constants
	// ------------------------------------------------------------------------------------------
	// Every value below is a retail console-variable default. The RE names each variable and the
	// arithmetic it feeds, but NOT the number: no recovered document states a default, and the
	// shipped `cfg/*.cfg` in the export corpus carry none of them (they are `debug_*` developer
	// variables and `pl_min_act_timer`, none of which the shipped configs write). Each is therefore
	// **CHOSEN, NOT RECOVERED**, with the reasoning that picked it. Replace the number when the
	// default is recovered; the arithmetic around it is the recovered part and does not move.

	// `pl_min_act_timer` — the floor an activity write falls back to when no positive explicit
	// duration was passed: `max(previously retained level, pl_min_act_timer)`.
	//
	// CHOSEN 2.0 s. The recovered `max` compares a duration in seconds against a level in `0..5`,
	// so the floor has to sit inside that range or the level term can never win and the recovered
	// `max` would be dead arithmetic. 2.0 is also the one duration the recovered producer set
	// states outright (the feed pulse's explicit two seconds), so an unauthored write and the
	// authored one agree at the bottom of the range while levels 3-5 still lengthen their own.
	inline constexpr float MinActTimerSeconds = 2.0f;

	// `debug_masquerade_timer` — the window an admitted supernatural incident must clear before it
	// may increment Masquerade again.
	//
	// CHOSEN 30.0 s. The counter is a five-step loss track, so the window has to be long enough
	// that one witnessed cast cannot walk it to five in a single fight and short enough that a
	// deliberately repeated violation still costs. Half a minute is the smallest value with both
	// properties at VtMB's combat pacing.
	inline constexpr double MasqueradeTimerSeconds = 30.0;

	// `debug_response_timer_min` / `_max` — the random delay between an admitted incident and the
	// police-response update that consumes it.
	//
	// CHOSEN 5.0 .. 15.0 s. `sm_diner_1`'s `post_robbery_cop_call` volume authors its own siren
	// wires at delays of 9 and 12 seconds after the same criminal activity write, which is the only
	// authored evidence in the corpus of how long "the cops are coming" is meant to feel; the range
	// is centred on it.
	inline constexpr double ResponseTimerMinSeconds = 5.0;
	inline constexpr double ResponseTimerMaxSeconds = 15.0;

	// `debug_cop_grace_time` — how long a consumed response's spawned count stays the baseline that
	// the next response's desired count is measured against, so a duplicate incident inside the
	// window adds nothing and a more severe one adds only the difference.
	//
	// CHOSEN 30.0 s, the same window as the Masquerade limiter: both exist to stop one incident
	// stream from compounding, and giving them different windows would be a second invented number
	// rather than a second recovered one.
	inline constexpr double CopGraceSeconds = 30.0;

	// `debug_heightened_alert_expire_time` — how long heightened alert survives the last cop
	// leaving pursuit.
	//
	// CHOSEN 60.0 s. Heightened alert is the "they are still looking for you" state dialogue
	// predicates read, so it has to outlast the pursuit that produced it by long enough to cross a
	// street; a minute is the shortest value that does.
	inline constexpr double HeightenedAlertExpireSeconds = 60.0;

	// `debug_supernatural_cop_spawn` — whether an admitted supernatural incident ALSO enters the
	// police-response admission a criminal incident enters.
	//
	// CHOSEN false. The variable is a `debug_` developer switch and the recovered text reads "when
	// enabled", which is how a retail source describes a default-off branch; defaulting it on would
	// put cops on the street for a Discipline cast with nothing recovered saying retail does.
	inline constexpr bool bSupernaturalCopSpawn = false;

	// The recovered producer durations (`docs/vtmb/feeding.md` § the pulse transaction): a feed
	// pulse and an interrupted feed both pass an explicit two seconds.
	inline constexpr float FeedActivitySeconds = 2.0f;

	// The clamp both timed setters apply, and the sentinel a zero write installs.
	inline constexpr int32 MaxActivityLevel = 5;
	inline constexpr double NoDeadline = -1.0;

	// The duration sentinel every recovered non-native caller passes: `trigger_player_activity_level`
	// and the targeted-Discipline commit both hand the setters `-1`, which is "no explicit duration,
	// derive the finite one" and NOT "indefinite".
	inline constexpr float DeriveDuration = -1.0f;

	// The three world area types (`m_nAreaType`, clamped 0..2).
	enum class EArea : int32
	{
		Combat = 0,     // terminal criminal and supernatural incidents are suppressed
		Safe = 1,       // entering ends Celerity and Protean
		Elysium = 2,    // unarmed + full Discipline teardown; verbs refused before commit
	};

	// ------------------------------------------------------------------------------------------
	// The pure channel rule
	// ------------------------------------------------------------------------------------------

	// One timed channel's whole retained state, detached from the player so the write and expiry
	// rules are assertable on their own.
	struct FChannel
	{
		int32  Level = 0;
		double Expiry = NoDeadline;
		int32  Count = 0;
	};

	// What one write did, for the caller's log line and for the tests.
	struct FWriteResult
	{
		bool bCleared = false;     // an explicit zero: level and deadline both dropped
		bool bRaised = false;      // the retained level actually moved up
		bool bRefreshed = false;   // the deadline was pushed out (true for a lower non-zero too)
		bool bCounted = false;     // the incident count was incremented
		double Duration = 0.0;     // the resolved duration, for the log
	};

	// The variant-type policy. Retail's setters take an integer entity-input variant, clamp it to
	// `0..5`, and treat a wrong type or a negative value as zero.
	//
	// DIVERGENCE (marshalling, not behaviour): a Hammer wire's parameter reaches this substrate as
	// a String variant, because our `FElysiumVariant` marshals the authored text rather than the
	// engine's already-parsed keyvalue integer. A String that is a well-formed integer literal is
	// therefore accepted as one; every other type (Void, Float, Vector, Handle, a non-numeric
	// String) is the recovered "wrong type" and reads as zero.
	int32 SanitizeLevel(const FElysiumVariant& Value);
	int32 SanitizeLevel(bool bIntegerVariant, int32 Raw);

	// The criminal / supernatural write. `Level` is already sanitized; `ExplicitDuration` is the
	// caller's own seconds, or `DeriveDuration` (-1) for "derive it".
	//
	//   * zero explicitly CLEARS: level 0 and expiry -1, no count;
	//   * a non-zero write never lowers the retained level, but a lower one still refreshes the
	//     deadline and still increments the count;
	//   * a positive explicit duration wins; otherwise the duration is
	//     `max(previously retained level, pl_min_act_timer)` — measured against the level the
	//     channel held BEFORE this write.
	FWriteResult WriteTimedChannel(FChannel& Channel, int32 Level, float ExplicitDuration, double Now);

	// The investigate rule: direct replacement, no deadline and no count.
	int32 WriteDirectChannel(int32 Level);

	// One expiry pass over one timed channel. True only on the transition that cleared it, so a
	// caller fires its consequence once.
	bool ExpireTimedChannel(FChannel& Channel, double Now);

	// ------------------------------------------------------------------------------------------
	// The pure police-response rule
	// ------------------------------------------------------------------------------------------

	enum class EQueueVerdict : uint8
	{
		Queued,           // nothing was pending: the record and its random deadline are installed
		ReplacedHigher,   // a strictly higher severity replaced the record, WITHOUT rescheduling
		RejectedLower,    // a pending record of equal or higher severity kept the street
		RejectedHunters,  // m_iHuntersInPursuitCount > 0 refuses new admission outright
	};

	// `Delay` is the caller's own draw from `debug_response_timer_min..max`, passed in rather than
	// drawn here so the rule stays pure and a seeded test can name the deadline.
	EQueueVerdict QueueResponse(FElysiumPoliceState& Police, int32 Severity,
		const FElysiumEntityHandle& Witness, const FVector& Position, double Now, double Delay);

	// `desired cops = max(1, severity - 1)`.
	int32 DesiredCops(int32 Severity);

	// What the response update produced.
	struct FConsumeResult
	{
		bool  bConsumed = false;     // the deadline was due and the record was taken
		bool  bWitnessLost = false;  // taken, but the saved witness no longer resolves: no spawn
		int32 Severity = 0;
		int32 Desired = 0;           // max(1, severity - 1)
		int32 Delta = 0;             // what the grace policy leaves to actually spawn
		FVector Position = FVector::ZeroVector;
	};

	// The `PlayerRuleUpdate` half: consume a due record, apply the `debug_cop_grace_time`
	// duplicate/higher-severity delta policy and report the delta the caller must spawn. The caller
	// supplies whether the witness still resolves, because that is a world question.
	FConsumeResult ConsumeResponse(FElysiumPoliceState& Police, bool bWitnessLive, double Now);

	// The Masquerade rate limiter. `bIncrement` is false while the window is still closed; when it
	// is true the caller applies `ChangeMasqueradeLevel(+1)` and stores `NextDeadline`.
	struct FMasqueradeVerdict
	{
		bool   bIncrement = false;
		double NextDeadline = 0.0;
	};
	FMasqueradeVerdict DecideMasquerade(const FElysiumPoliceState& Police, double Now);

	// ------------------------------------------------------------------------------------------
	// The pure pursuit / alert edges
	// ------------------------------------------------------------------------------------------

	enum class EEdge : uint8
	{
		None,
		Start,   // the zero-to-one edge
		End,     // the one-to-zero edge
	};

	// Cop pursuit. The zero-to-one edge ZEROES the retained alert deadline (so the next rule pass
	// expires the alert); the one-to-zero edge raises heightened alert and schedules its expiry.
	// The caller fires the outputs the returned edge names.
	EEdge SetCopsInPursuit(FElysiumPoliceState& Police, int32 NewCount, double Now);
	// Hunters are the same counter shape with their own outputs and no alert consequence.
	EEdge SetHuntersInPursuit(FElysiumPoliceState& Police, int32 NewCount);

	// True only on the pass that cleared it.
	bool ExpireHeightenedAlert(FElysiumPoliceState& Police, double Now);

	// ------------------------------------------------------------------------------------------
	// The wiring half — producers
	// ------------------------------------------------------------------------------------------

	// The three player setters, as every recovered producer calls them. Safe on a player with no
	// world. `Duration` is seconds, or `DeriveDuration`.
	void SetCriminalLevel(FElysiumPlayer& Player, int32 Level, float Duration = DeriveDuration);
	void SetSupernaturalLevel(FElysiumPlayer& Player, int32 Level, float Duration = DeriveDuration);
	void SetInvestigateLevel(FElysiumPlayer& Player, int32 Level);

	// ------------------------------------------------------------------------------------------
	// The wiring half — the two witnessed-incident consumers
	// ------------------------------------------------------------------------------------------

	enum class EAdmission : uint8
	{
		Accepted,
		RefusedCombatArea,   // the terminal thunk's world `m_nAreaType != 0` guard
		RefusedHunters,      // the response admission alone; a Masquerade increment is unaffected
		RefusedRateLimited,  // supernatural only: the Masquerade window is still closed
	};
	const TCHAR* AdmissionName(EAdmission Admission);

	// `PlayerCriminalIncident` — publish the changed player state and enter the police-response
	// admission. It NEVER touches Masquerade.
	EAdmission PlayerCriminalIncident(FElysiumPlayer& Player, int32 Severity,
		const FElysiumEntityHandle& Witness, const FVector& Position);

	// `PlayerSupernaturalIncident` — the Masquerade rate limiter, and (only when
	// `debug_supernatural_cop_spawn` is enabled) the same police-response admission.
	EAdmission PlayerSupernaturalIncident(FElysiumPlayer& Player, int32 Severity,
		const FElysiumEntityHandle& Witness, const FVector& Position);

	// ============================ Cycle 10c hunk 1/3 — the scare queue ===========================
	// "A supernatural flee-only branch instead inserts a 16-byte player-owned scare record keyed by
	// NPC identity: a repeat keeps the greater severity and refreshes its timestamp.
	// `PlayerRuleUpdate` selects from that queue, submits the supernatural incident and removes
	// consumed/expired records." (`docs/vtmb/player-entity.md`.)
	//
	// The record carries no position, which is why `FElysiumScareRecord` does not: 16 bytes hold the
	// NPC handle, a severity and a timestamp with one 4-byte field unidentified, and a world
	// position would not fit beside them. The submission therefore resolves the queued NPC's own
	// origin at consumption time, which is also the only position that is still true by then.
	//
	// CHOSEN, NOT RECOVERED — the record lifetime and the selection order. Neither is stated. The
	// lifetime is the Nosferatu window's own five seconds, the one duration the recovered material
	// attaches to the same flee-only record; the selection is the strongest record, ties going to
	// the oldest, because a queue whose whole purpose is "how bad was the worst thing seen" cannot
	// sensibly submit the weakest and because oldest-first is what stops one NPC starving another.
	inline constexpr double ScareRecordLifetimeSeconds = 5.0;

	// Queue (or refresh) one NPC's scare record. Reached only from the NPC's flee-only schedule
	// branch (`Substrate/ElysiumNpcWitness.h`).
	void QueueScareRecord(FElysiumPlayer& Player, const FElysiumEntityHandle& Npc, int32 Severity);

	// The queue's own pass, run from `TickPlayerLaw`: drop expired records, take the strongest
	// remaining one and submit it as a supernatural incident. Returns whether one was consumed.
	bool ConsumeScareQueue(FElysiumPlayer& Player, double Now);

	// ============================ Cycle 10c hunk 2/3 — the witness record ========================
	// The world-event lane's producer is the criminal/supernatural activity WRITE itself: an act
	// that counted an incident publishes an expiring record (severity, origin, offender) into the
	// world's law-record store, which is how an NPC that never saw the offender still witnesses the
	// crime. The store and its per-NPC acceptance are `Substrate/ElysiumNpcWitness.h`; the choice of
	// producer, and why the witnessed incident cannot be one, is marked at the publish site in
	// `ApplyTimedWrite`.
	// =============================================================================================

	// ------------------------------------------------------------------------------------------
	// The wiring half — the world area
	// ------------------------------------------------------------------------------------------

	// The world's clamped `m_nAreaType`. Read from `events_world.safearea` — the registered field
	// the ordinary R2 walk reaches — and, when the map carries no `events_world`, from the
	// `worldspawn` key that establishes the same baseline. `Combat` when neither exists.
	int32 WorldAreaType(const FElysiumEntityWorld& World);

	// `CWorldEvents::SetSafeArea`'s world-to-player transaction: apply the new area's policy to the
	// connected player BEFORE the value is published. Elysium runs the ordinary all-Discipline
	// teardown; safe/Masquerade ends only Celerity and Protean; combat has no immediate teardown.
	// Called by the `events_world` input, which owns the value itself.
	void ApplyWorldAreaTransition(FElysiumEntityWorld& World, int32 NewArea);

	// ------------------------------------------------------------------------------------------
	// The wiring half — the expiry pass and the world outputs
	// ------------------------------------------------------------------------------------------

	// One player think's worth of law work: the supernatural and criminal expiries, the
	// response-timer consume, and the heightened-alert expiry. Reached only from
	// `FElysiumPlayer::Think`, so every deadline is measured on the clock the save restores.
	void TickPlayerLaw(FElysiumPlayer& Player, double Now);

	// The `events_world` outputs this domain fires. Corpus evidence (23 exported maps): every one
	// of these is an authored output row on `events_world`, which is why they are fired there and
	// not on the player.
	namespace Outputs
	{
		const FName& StartCopPursuit();      // OnStartCopPursuitMode      — 31 rows
		const FName& EndCopPursuit();        // OnEndCopPursuitMode        — 31 rows
		const FName& StartCopAlert();        // OnStartCopAlertMode        — 23 rows
		const FName& EndCopAlert();          // OnEndCopAlertMode          — 24 rows
		const FName& StartHunterPursuit();   // OnStartHunterPursuitMode   — 23 rows
		const FName& EndHunterPursuit();     // OnEndHunterPursuitMode     — 23 rows
		const FName& CopsComing();           // OnCopsComing               — 23 rows
		const FName& CopsOutside();          // OnCopsOutside              — 41 rows
		const FName& MasqueradeLevel1();     // OnMasqueradeLevel1         — 23 rows
		const FName& MasqueradeLevel2();     // OnMasqueradeLevel2         — 23 rows
		const FName& MasqueradeLevel3();     // OnMasqueradeLevel3         — 23 rows
		const FName& MasqueradeLevel4();     // OnMasqueradeLevel4         — 23 rows
		const FName& MasqueradeLevel5();     // OnMasqueradeLevel5         — 23 rows
		const FName& MasqueradeLevelChanged(); // OnMasqueradeLevelChanged — 25 rows (la_hub_1 x3)
	}

	// Fire one of the above at the map's `events_world`. A map with no `world` entity fires
	// nothing; a wired output with no authored row is an ordinary no-op inside `FireOutput`.
	void FireWorldEvent(FElysiumEntityWorld& World, const FName& Output,
		const FElysiumEntityHandle& Activator);

	// The two pursuit counters, as the systems that own cop and hunter bodies will drive them.
	// They are the door for the edge outputs; nothing in this cycle produces a cop, so the only
	// callers today are the tests and whatever lands the spawn seam.
	void SetCopPursuitCount(FElysiumPlayer& Player, int32 NewCount);
	void SetHunterPursuitCount(FElysiumPlayer& Player, int32 NewCount);
}
