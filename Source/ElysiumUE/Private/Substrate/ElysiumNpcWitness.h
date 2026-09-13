#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntityHandle.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumSchedule.h"

class FElysiumEntityWorld;
class FElysiumNpc;
class FElysiumPlayer;
struct FElysiumSaveArchive;

// NPC law witnessing: the per-NPC half of the player-law transaction whose player half lives in
// `Substrate/ElysiumLaw.h`.
//
// The behaviour is owned by `docs/vtmb/npc-ai/population.md` § "Player-law observation
// transaction" and `docs/vtmb/player-entity.md` § "Law, Masquerade and world response"; the feed
// producer's own two callers are in `docs/vtmb/feeding.md`. The recovered distribution of the four
// authored thresholds is the same RE document's § "Perception and player-reaction distributions".
//
// The recovered transaction, quoted, because everything below is a transcription of it:
//
//   "One NPC retains independent criminal and supernatural processed counts, witnessed levels,
//    locations and offender handles, plus `m_bPLSupernaturalActFleeOnly`. For the direct-player lane
//    it requires a valid `m_hClosestPlayer` and `COND_SEE_PLAYER`, compares the player's
//    monotonically increasing act count with its own processed count, then tests the current player
//    level against each threshold. Passing a threshold records the player as offender and raises the
//    corresponding AI condition. If that channel's observation window is closed, the NPC advances its
//    processed count without producing the condition, so the same act is not replayed when
//    observation resumes."
//
//   "A parallel global-event lane carries expiring criminal or supernatural records with severity,
//    origin and offender. `CAI_BaseNPCTroika` accepts a record only when the origin is inside its
//    view cone, no farther than `m_flSeekDistInspection`, and reached by an unobstructed trace. Among
//    accepted records it retains the strongest criminal and supernatural entries independently, then
//    tests both flee and attack thresholds."
//
//   "Condition gathering never mutates Masquerade or spawns police. Schedule selection/translation
//    requires the retained offender still be the player, submits the retained severity/origin to the
//    player's incident consumer, and copies the player's current act count into the NPC processed
//    count. The supernatural flee-only schedule path instead queues a player-owned scare record."
//
// That split is the load-bearing part of this file: the GATHER half below only reads and remembers,
// and the SELECT half is the only thing that reaches `ElysiumLaw`'s two consumers.
//
// Where the recovered record is open, a **CHOSEN** rule states its reasoning and a **SEAM** names
// what would close it. Neither is ever presented as recovered.

namespace ElysiumNpcWitness
{
	// The two independent law channels. Investigate is deliberately not one of them: it carries no
	// act count, no deadline and no witnessed record — it is a bare level compared against
	// `pl_investigate` on every pass.
	enum class EChannel : uint8
	{
		Criminal = 0,
		Supernatural = 1,
		Count = 2,
	};
	const TCHAR* ChannelName(EChannel Channel);

	// ------------------------------------------------------------------------------------------
	// The authored threshold rule
	// ------------------------------------------------------------------------------------------

	// The player clamps every activity channel to `0..5` (`ElysiumLaw::MaxActivityLevel`), so an
	// authored 6 cannot be reached: "an authored threshold of 6 is therefore an effective disable.
	// This explains the dominant 6 rows above without treating 6 as another attainable crime tier."
	inline constexpr int32 DisabledThreshold = 6;

	// The default an NPC that authors none of the four `pl_*` keys carries.
	//
	// CHOSEN, NOT RECOVERED: no FGD default survives in the recovered material. 6 is picked because
	// it is the dominant AUTHORED value on every one of the four keys (191/260/214/260 of the 424
	// rows) and because it is the disable — an NPC whose author said nothing must not acquire a
	// reaction this runtime invented for it. The 424 rows that DO author all four are unaffected
	// either way; only a hand-built fixture and a hypothetical unauthored map row read this.
	inline constexpr int32 DefaultThreshold = DisabledThreshold;

	/**
	 * Resolve one authored threshold keyvalue into the value the comparison uses.
	 *
	 * CHOSEN, NOT RECOVERED: a NEGATIVE authored threshold reads as UNSET and resolves to
	 * `DefaultThreshold`. The corpus authors `-1` on 45/45/45/48 of the four keys and on 60 rows of
	 * `pl_investigate`, always across the whole block at once. Three things argue for the sentinel
	 * reading and none for the literal one: the same NPC keyfield block already uses `-1.0` as its
	 * recovered "not authored, derive" sentinel on `vision` and `hearing`
	 * (`InitPerceptionDistances`, `0x1028fb70`); a literal `-1` under the recovered `level >=
	 * threshold` test would fire on a level of ZERO, i.e. on no activity at all, which the recovered
	 * `0..5` clamp shows cannot be a crime tier; and the block is authored as a unit, which reads as
	 * "this row was left alone" rather than as five independent maximally-hostile decisions.
	 *
	 * The alternative — `-1` meaning "react to anything" — is settled by decompiling the comparison
	 * site; only this function changes if it does.
	 */
	int32 ResolveThreshold(int32 Authored);

	/**
	 * The recovered comparison: "tests the current player level against each threshold".
	 *
	 * `>=`, not `>`. The direction is fixed by the recovered disable statement: if the test were
	 * `>`, an authored 5 would already be unreachable and the document's "6 is the disable" would
	 * name the wrong number.
	 */
	bool PassesThreshold(int32 Level, int32 ResolvedThreshold);

	// ------------------------------------------------------------------------------------------
	// The three ignore deadlines and what "the window is open" means
	// ------------------------------------------------------------------------------------------
	//
	// THE READING, stated once and implemented exactly once (`IsWindowOpen`). The recovered field is
	// an "ignore deadline" and "NPC spawn initializes the three deadlines to zero"; the three static
	// callers "open" a window for a fixed number of seconds. This runtime reads that as:
	//
	//     the window is OPEN while `Now < deadline`, and CLOSED otherwise — including at the
	//     spawn-zero default.
	//
	// The polarity is taken from the two documents' own directional statements rather than from the
	// field's name. `docs/vtmb/feeding.md`: the feed pulse "opens the victim NPC's criminal and
	// supernatural observation windows for three seconds, ALLOWING that NPC's ordinary
	// condition-gathering pass to compare the new player act counts with its authored `pl_*`
	// thresholds" — an explicit statement that the setter grants the comparison rather than
	// suppressing it. `docs/vtmb/player-entity.md`: "when an ignore deadline closes that channel's
	// observation window it advances the processed count without producing a reaction."
	//
	// It is also the only reading under which the two lanes are not the same lane. The direct-player
	// lane is narrow by construction — it needs the closest player, sight, and one of exactly three
	// windows — while the global-event lane is gated on cone, distance and a trace and on nothing
	// else, which is how an ordinary bystander witnesses a crime it was not the victim of. Under the
	// inverted reading the three setters would be suppressors, the direct lane would fire for every
	// NPC that can see the player, and the feeding document's "allowing" would be false.
	//
	// SEAM: what would settle it beyond argument is decompiling either deadline setter beside its
	// one consumer. Only `IsWindowOpen` changes if it does; every caller below asks it.

	// `Feed()` and the interrupted-feed path both open BOTH channels on the VICTIM for three
	// seconds (`docs/vtmb/feeding.md`).
	inline constexpr double FeedWindowSeconds = 3.0;
	// "entering NPC state 14 opens the criminal window for two seconds".
	inline constexpr double StateChangeWindowSeconds = 2.0;
	// "the closest-player special case opens the Nosferatu window for five seconds".
	inline constexpr double NosferatuWindowSeconds = 5.0;

	// "Seeing a nearby `Player_Nosferatu` can create that severity-2 flee-only record independently
	// of a new supernatural activity count."
	inline constexpr int32 NosferatuSeverity = 2;

	// The priority the attack arm's `D_HT` row carries. `IRelationPriority` (`0x10333700`) "returns
	// the row's raw integer, and otherwise returns 5 for a non-null actor" — so 5 is the value an NPC
	// with no row already arbitrates at, and it is what the corpus's dominant hostile
	// `player_reaction D_HT 5` (96 rows) writes. Choosing anything else would make a law-driven
	// hostility outrank or lose to an authored one for no recovered reason.
	inline constexpr int32 AttackRelationPriority = 5;

	// How long a published global law record stays acceptable.
	//
	// CHOSEN, NOT RECOVERED: the records are "expiring" / "short-lived" with no stated lifetime. 4.0 s
	// is the game-sound bus's own retention window (`FElysiumGameSoundBus::RetentionSeconds`), which
	// is the one measured answer this runtime has for "how long a world stimulus stays available to a
	// consumer that polls on its own think". Using a second, different number for the second polled
	// stimulus window would be an invented value where a measured one already exists.
	inline constexpr double RecordLifetimeSeconds = 4.0;

	// ------------------------------------------------------------------------------------------
	// The global-event lane's record store
	// ------------------------------------------------------------------------------------------

	// One expiring record: "criminal or supernatural records with severity, origin and offender".
	struct FElysiumLawEvent
	{
		EChannel Channel = EChannel::Criminal;
		int32 Severity = 0;
		FVector Origin = FVector::ZeroVector;
		FElysiumEntityHandle Offender;
		double Time = 0.0;
		double ExpiresAt = 0.0;
		uint64 Serial = 0;   // monotonic within one store, 1-based
	};

	/**
	 * The world-owned store, in the game-sound bus's shape and for the same reason: a producer
	 * stamps a record into a bounded window and every consumer polls it during its own think. No
	 * queue entry, no receiver, no scheduler — it is the third event kind, not a fourth transport.
	 *
	 * K8: this is SESSION state and is deliberately not saved. Every record expires within
	 * `RecordLifetimeSeconds`, an NPC's reaction to one is already durable (its retained witnessed
	 * record and processed counts are on the saved NPC leaf), and a save cannot be taken and
	 * restored inside a four-second window without the restore replaying a stimulus the player never
	 * produced twice. The same argument the sound bus's cursor makes: a restored world starts at the
	 * live head.
	 */
	class FElysiumLawEventBus
	{
	public:
		static constexpr int32 MaxRetained = 64;

		// Stamp one record and return it by value. Nothing is delivered.
		FElysiumLawEvent Publish(EChannel Channel, int32 Severity, const FVector& Origin,
			const FElysiumEntityHandle& Offender, double Now);

		// Everything still inside its lifetime at `Now`, oldest first. Expiry is applied here rather
		// than on a tick, because the store has no think of its own.
		void Collect(double Now, TArray<FElysiumLawEvent>& Out) const;

		const TArray<FElysiumLawEvent>& Retained() const { return Events; }
		int32 NumRetained() const { return Events.Num(); }
		uint64 LastSerial() const { return Serial; }
		void Reset();

	private:
		void Evict(double Now);

		TArray<FElysiumLawEvent> Events;
		uint64 Serial = 0;
	};

	// Publish into the world's store. Safe with no world.
	void PublishLawEvent(FElysiumEntityWorld* World, EChannel Channel, int32 Severity,
		const FVector& Origin, const FElysiumEntityHandle& Offender);
}

// --- The per-NPC retained state ---

// One channel's retained half. "independent criminal and supernatural processed counts, witnessed
// levels, locations and offender handles" plus that channel's own ignore deadline.
struct FElysiumNpcWitnessChannel
{
	// How much of the player's monotonic act count this NPC has already accounted for. Never
	// decremented; a closed window advances it without producing a condition, which is what stops an
	// act from being replayed when observation resumes.
	int32 Processed = 0;

	// The retained witnessed record — what schedule selection submits. Severity is the observed
	// level (the player's current activity level for the direct lane, the record's own severity for
	// the global one).
	int32 Level = 0;
	FVector Location = FVector::ZeroVector;
	FElysiumEntityHandle Offender;

	// The ignore deadline. Spawn-zero means "closed" under the reading stated in the header.
	double IgnoreUntil = 0.0;
};

// The whole per-NPC witness block, carried by value on `FElysiumNpc` in the senses/cognition shape.
struct FElysiumNpcWitness
{
	FElysiumNpcWitnessChannel Channels[static_cast<int32>(ElysiumNpcWitness::EChannel::Count)];

	// `m_bPLSupernaturalActFleeOnly`. Set by the Nosferatu special case and cleared by an ordinary
	// supernatural observation, so the flag always describes the record currently retained.
	bool bSupernaturalFleeOnly = false;

	// The third recovered deadline. It has no channel of its own: an open Nosferatu window produces a
	// supernatural observation, it is only the ADMISSION that is separate.
	double NosferatuIgnoreUntil = 0.0;

	// Where this NPC has consumed the world's law-record store up to. Session state, not saved, for
	// the reason stated in `Serialize`; 0 is "nothing consumed yet".
	uint64 GlobalCursor = 0;

	FElysiumNpcWitnessChannel& Channel(ElysiumNpcWitness::EChannel C)
	{
		return Channels[static_cast<int32>(C)];
	}
	const FElysiumNpcWitnessChannel& Channel(ElysiumNpcWitness::EChannel C) const
	{
		return Channels[static_cast<int32>(C)];
	}

	void Reset();
	void Serialize(FElysiumSaveArchive& Ar);
	void Rebase(const FElysiumEntityWorld& World);
};

namespace ElysiumNpcWitness
{
	// ------------------------------------------------------------------------------------------
	// The window rule
	// ------------------------------------------------------------------------------------------

	// The one place the polarity stated in the header is implemented.
	bool IsWindowOpen(double Deadline, double Now);
	bool IsChannelOpen(const FElysiumNpc& Npc, EChannel Channel, double Now);

	// ------------------------------------------------------------------------------------------
	// The three recovered deadline setters
	// ------------------------------------------------------------------------------------------
	// "Those are the complete static callers of the three deadline setters in the pinned DLL", so
	// there are exactly three of them and no fourth door.

	// The feed pulse and the interrupted feed, on the VICTIM. Reached from `ElysiumFeed.cpp`.
	void OpenFeedWindows(FElysiumNpc& Victim, double Now);

	/**
	 * "entering NPC state 14 opens the criminal window for two seconds".
	 *
	 * CHOSEN, NOT RECOVERED — the state mapping. Retail state 14 is unresolved: the decoded base
	 * selector table names 0 none, 1 idle, 2 combat, 3 alert, 4 scripted, 6 prone, 7 dead and 12 as a
	 * "class-specific/fallback idle-like branch; exact semantic label unresolved", and `0xd` is named
	 * only as the state that returns `SCHED_TROIKA_IDLE_STAND`. 14 is named nowhere else in the
	 * recovered material, and this runtime's state set is the six the survey's own requirements list
	 * names ("Current and ideal idle/alert/combat/script/prone/dead states"), so there is no
	 * value-for-value mapping to make.
	 *
	 * ALERT is chosen as the closest analogue, for what the window does rather than for what the
	 * number is. The grant is TWO SECONDS and CRIMINAL-ONLY: it is a transient reaction window opened
	 * by a state change, not a posture. Alert is this runtime's one transient promotion — entered
	 * from idle on damage and on the whole hear family, without a committed enemy — so opening the
	 * criminal window on that edge reproduces the observable behaviour the recovered pieces describe
	 * together: a gunshot promotes bystanders to alert, and the ones that can also SEE the player
	 * inside those two seconds flee or attack according to their authored thresholds.
	 *
	 * The two alternatives and why they lose: Combat is not transient and an NPC already fighting the
	 * player does not need a witness lane to become hostile; Idle is the state every NPC sits in, so
	 * a two-second window opened on entering it would be indistinguishable from no window at all.
	 * Decompiling the state-14 setter's caller settles it; only this function's ONE call site moves.
	 *
	 * ONE ORDERING CONSEQUENCE, stated rather than hidden. The recovered decision pass gathers
	 * conditions and only THEN updates state policy, so the window this opens is open from the NEXT
	 * pass onward — while the law lane of the pass that promoted the NPC has already run against a
	 * closed window and advanced its processed count. An NPC therefore reacts to the first act it
	 * observes with its window already open, not to the act that made it look. The two other setters
	 * do not have this shape: the feed opens the victim's windows inside the same call that raises
	 * the player's activity, and the Nosferatu case runs in `TickSight`, ahead of gathering. Moving
	 * this call earlier would mean re-ordering a recovered pass to suit an unrecovered state number,
	 * which is the larger invention.
	 */
	void OnEnteredAlertState(FElysiumNpc& Npc, double Now);

	/**
	 * "the closest-player special case opens the Nosferatu window for five seconds", reached from
	 * `SetClosestPlayer`'s own body (this runtime: `FElysiumNpcSenses::TickSight`).
	 *
	 * The identity test is the player's clan slot. `Player_Nosferatu` is a clan template
	 * (`docs/vtmb/game_runtime.md` § "The 7 clans"), and the sheet's `Clan` slot is what `pc.clan`
	 * reports, so `FElysiumSheet::ClanFromName(TEXT("Nosferatu"))` is the same identity the scripts
	 * read — not a model or a body-stem guess.
	 *
	 * CHOSEN, NOT RECOVERED — what "nearby"/"sufficiently close" means. No distance is stated. The
	 * senses' own `bPlayerInRange` is used: it is the observer's effective visual radius scaled by
	 * the player's committed stealth vision scalar, i.e. the one proximity term this transaction
	 * already has a recovered rule for. Inventing a second radius would give the same question two
	 * answers.
	 *
	 * SEAM (parsed, read, and deliberately narrow): `nosferatu_tolerrant`. Six exported maps author
	 * it on `worldspawn` and `events_world` carries `SetNosferatuTolerant`, and the name states an
	 * area where a Nosferatu is not a scandal — but NOTHING recovered joins that key to this lane, so
	 * it is read here as a suppression of exactly this special case and of nothing else, and it is
	 * marked rather than presented as recovered. What would close it is one decompiled read of the
	 * world flag inside the closest-player body.
	 */
	void OnClosestPlayerUpdated(FElysiumNpc& Npc, const FElysiumPlayer& Player, double Now);

	// ------------------------------------------------------------------------------------------
	// The gather half — condition gathering, which never mutates Masquerade or spawns police
	// ------------------------------------------------------------------------------------------

	/**
	 * Both lanes plus the investigate test, run as one pass and joined into `ElysiumNpcEnemy::
	 * GatherConditions` step 1.
	 *
	 * It clears and recomputes exactly the five conditions the recovered pass names, writes the
	 * retained witnessed record for whichever channel produced one, and advances a processed count
	 * only on the recovered closed-window arm. It calls nothing on the player.
	 */
	void GatherLawConditions(FElysiumNpc& Npc, double Now, FElysiumNpcConditions& Out);

	// Is the law pass suppressed for this NPC? Both recovered suppressors are SEAMs today; the
	// predicate exists so the two have one named home rather than being absent.
	bool IsLawPassSuppressed(const FElysiumNpc& Npc);

	// ------------------------------------------------------------------------------------------
	// The select half — the only side that submits an incident
	// ------------------------------------------------------------------------------------------

	/**
	 * Schedule selection's law branch. Returns the flee program when one applies and `None`
	 * otherwise, and performs the recovered submission transaction on the way past:
	 *
	 *   * require the retained offender still be the player;
	 *   * submit the retained severity/origin to `ElysiumLaw`'s consumer (or, on the supernatural
	 *     flee-only path, queue the player-owned scare record instead);
	 *   * copy the player's current act count into this NPC's processed count.
	 *
	 * CHOSEN, NOT RECOVERED — the hostility mechanism behind conditions 32/34. The recovered material
	 * states that the attack thresholds are tested and that the corresponding condition is raised,
	 * and names no schedule and no enemy write for the result. What this installs instead is a `D_HT`
	 * entity relationship row toward the player at priority 5, and then nothing: the ordinary enemy
	 * transaction (`ElysiumNpcEnemy`) does the rest on the next pass. The reasoning is that
	 * `BestEnemy`'s eligibility gate is exactly `D_HT`/`D_FR`, so a relationship row is the ONLY
	 * documented way for a non-hostile NPC to become able to select the player at all, and priority 5
	 * is `IRelationPriority`'s own no-row default for a live actor — the value the corpus's dominant
	 * `player_reaction D_HT 5` also writes. Force-setting `m_hEnemy` here would bypass the interrupt
	 * gate, the stickiness test and the arbitration the recovered transaction is built out of, which
	 * is a larger invention than the row.
	 *
	 * CHOSEN, NOT RECOVERED — attack outranks flee when a channel raises both. Nothing states an
	 * order. The corpus barely poses the question (the two non-6 populations are near-disjoint: 103
	 * rows author `pl_criminal_attack 1` while 92 author `pl_criminal_flee 5`), and an NPC that
	 * turned hostile and then ran the retreat program in the same pass would abandon the fight it
	 * just started.
	 *
	 * SEAM (named, not implemented): the recovered `CRIMSUSP` family is six schedules whose contents
	 * the survey does not decode, and the `FLEE`/`COWER` families are eleven and thirteen more in the
	 * same state. `SCHED_TROIKA_RUN_AWAY` (`0xb9`) is the one registered retreat program in this
	 * runtime and is what the flee arm selects; it is a stand-in for a named family, and it is the
	 * program that changes when one of those is decoded, not the branch.
	 */
	EElysiumScheduleId SelectLawSchedule(FElysiumNpc& Npc, double Now);
}
