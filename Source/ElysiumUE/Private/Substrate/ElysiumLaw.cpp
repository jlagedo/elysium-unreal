#include "Substrate/ElysiumLaw.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumVariant.h"
#include "Substrate/ElysiumDisciplines.h"
#include "Substrate/ElysiumNpcWitness.h"   // Cycle 10c — the world-event lane's record store
#include "Substrate/ElysiumPlayerLog.h"

namespace
{
	// One report per distinct reason per module load. The unbuilt halves of this domain sit on paths
	// a run can reach many times a second, and a warning per pass is spam rather than observability;
	// the first one still says exactly what did not happen and why.
	void ReportOnce(const FString& Key, const FString& Message)
	{
		static TSet<FString> Reported;
		if (Reported.Contains(Key))
		{
			return;
		}
		Reported.Add(Key);
		UE_LOG(LogElysiumPlayer, Warning, TEXT("law: %s"), *Message);
	}

	// The world's own policy leaf, or null. Retail's `m_nAreaType` lives on the world singleton and
	// `CWorldEvents` mutates it; this substrate keeps the value on the `events_world` leaf, which is
	// that same class, so this is the singleton lookup rather than a second store.
	FElysiumEntity* FindWorldEvents(const FElysiumEntityWorld& World)
	{
		static const FString WorldEventsClass(TEXT("events_world"));
		for (const TUniquePtr<FElysiumEntity>& Ent : World.Entities())
		{
			if (Ent && Ent->Def && !Ent->IsRecordOnly()
				&& Ent->Def->Classname.Equals(WorldEventsClass, ESearchCase::IgnoreCase))
			{
				return Ent.Get();
			}
		}
		return nullptr;
	}

	// The authored `worldspawn` value for one policy key. False when the map does not carry it.
	// This is the map's baseline, which `events_world`'s inputs then override.
	bool WorldSpawnKey(const FElysiumEntityWorld& World, const TCHAR* Key, FString& Out)
	{
		static const FString WorldSpawnClass(TEXT("worldspawn"));
		for (const TUniquePtr<FElysiumEntity>& Ent : World.Entities())
		{
			if (Ent && Ent->Def && Ent->Def->Classname.Equals(WorldSpawnClass, ESearchCase::IgnoreCase))
			{
				if (const FString* Value = Ent->Def->Keys.Find(Key))
				{
					Out = *Value;
					return true;
				}
				return false;
			}
		}
		return false;
	}

	// Read one registered field off an entity through the ordinary R2 walk.
	bool ReadRegisteredInt(const FElysiumEntity& Ent, const FName& Field, int32& Out)
	{
		if (Ent.Class == nullptr)
		{
			return false;
		}
		const FElysiumFieldAccessor* Acc = FElysiumClassRegistry::Get().FindField(*Ent.Class, Field);
		if (Acc == nullptr || !Acc->Get)
		{
			return false;
		}
		Out = Acc->Get(Ent).ToInt();
		return true;
	}
}

namespace ElysiumLaw
{

// ================================================================================================
// The pure channel rule
// ================================================================================================

int32 SanitizeLevel(bool bIntegerVariant, int32 Raw)
{
	// A wrong type or a negative value is zero — the recovered rule, and the reason a `-1` authored
	// channel on `trigger_player_activity_level` is "unset" rather than "clear".
	if (!bIntegerVariant || Raw < 0)
	{
		return 0;
	}
	return FMath::Min(Raw, MaxActivityLevel);
}

int32 SanitizeLevel(const FElysiumVariant& Value)
{
	switch (Value.Type)
	{
	case EElysiumVariantType::Int:
	case EElysiumVariantType::Bool:
		return SanitizeLevel(/*bIntegerVariant*/ true, Value.ToInt());
	case EElysiumVariantType::String:
	{
		// The marshalling divergence stated in the header: an authored wire's parameter is a String
		// here where retail's keyvalue parser had already produced an integer. Only a well-formed
		// integer literal is treated as one.
		const FString Text = Value.AsString.TrimStartAndEnd();
		if (Text.IsEmpty() || !Text.IsNumeric())
		{
			return 0;
		}
		return SanitizeLevel(/*bIntegerVariant*/ true, FCString::Atoi(*Text));
	}
	default:
		return 0;   // Void / Float / Vector / Handle — the recovered "wrong type" arm
	}
}

FWriteResult WriteTimedChannel(FChannel& Channel, int32 Level, float ExplicitDuration, double Now)
{
	FWriteResult Result;
	Level = FMath::Clamp(Level, 0, MaxActivityLevel);

	if (Level == 0)
	{
		// The explicit clear. The deadline goes to the sentinel rather than to `Now`, so the expiry
		// pass has nothing left to age out and cannot fire a second consequence.
		Channel.Level = 0;
		Channel.Expiry = NoDeadline;
		Result.bCleared = true;
		return Result;
	}

	// The duration is derived against the level the channel held BEFORE this write — retail's
	// "previously retained level", not the one being installed.
	const int32 Previous = Channel.Level;
	Result.Duration = (ExplicitDuration > 0.f)
		? static_cast<double>(ExplicitDuration)
		: static_cast<double>(FMath::Max(static_cast<float>(Previous), MinActTimerSeconds));

	// Raise, never lower. A lower non-zero request still refreshes and still counts.
	if (Level > Channel.Level)
	{
		Channel.Level = Level;
		Result.bRaised = true;
	}
	Channel.Expiry = Now + Result.Duration;
	Result.bRefreshed = true;
	++Channel.Count;
	Result.bCounted = true;
	return Result;
}

int32 WriteDirectChannel(int32 Level)
{
	// Direct replacement, both ways: the investigate setter has no timer and no incident count, so
	// a lower value really does lower it.
	return FMath::Clamp(Level, 0, MaxActivityLevel);
}

bool ExpireTimedChannel(FChannel& Channel, double Now)
{
	if (Channel.Level == 0 || Channel.Expiry < 0.0 || Now < Channel.Expiry)
	{
		return false;
	}
	Channel.Level = 0;
	Channel.Expiry = NoDeadline;
	return true;
}

// ================================================================================================
// The pure police-response rule
// ================================================================================================

int32 DesiredCops(int32 Severity)
{
	return FMath::Max(1, Severity - 1);
}

EQueueVerdict QueueResponse(FElysiumPoliceState& Police, int32 Severity,
	const FElysiumEntityHandle& Witness, const FVector& Position, double Now, double Delay)
{
	if (Police.HuntersInPursuit > 0)
	{
		// Hunters own the street: no new police response is admitted while they pursue.
		return EQueueVerdict::RejectedHunters;
	}
	if (Police.bResponsePending)
	{
		if (Severity <= Police.ResponseSeverity)
		{
			return EQueueVerdict::RejectedLower;
		}
		// A strictly higher severity REPLACES the record and deliberately does not reschedule: the
		// cops were already called, and calling again does not buy the player more time.
		Police.ResponseSeverity = Severity;
		Police.ResponseWitness = Witness;
		Police.ResponsePosition = Position;
		return EQueueVerdict::ReplacedHigher;
	}
	Police.bResponsePending = true;
	Police.ResponseSeverity = Severity;
	Police.ResponseWitness = Witness;
	Police.ResponsePosition = Position;
	Police.ResponseDeadline = Now + Delay;
	return EQueueVerdict::Queued;
}

FConsumeResult ConsumeResponse(FElysiumPoliceState& Police, bool bWitnessLive, double Now)
{
	FConsumeResult Result;
	if (!Police.bResponsePending || Now < Police.ResponseDeadline)
	{
		return Result;
	}
	Result.bConsumed = true;
	Result.Severity = Police.ResponseSeverity;
	Result.Position = Police.ResponsePosition;
	Police.bResponsePending = false;

	if (!bWitnessLive)
	{
		// "continue only if the saved witness handle still resolves" — the record is consumed either
		// way, so a dead witness costs the response rather than deferring it.
		Result.bWitnessLost = true;
		return Result;
	}

	Result.Desired = DesiredCops(Result.Severity);
	const bool bInGrace = Now < Police.GraceUntil;
	const int32 AlreadyOnTheStreet = bInGrace ? Police.GraceSpawned : 0;
	Result.Delta = FMath::Max(0, Result.Desired - AlreadyOnTheStreet);

	// CHOSEN: the grace window is refreshed only when the response actually put something new on
	// the street. Refreshing it on a duplicate that spawned nothing would let a stream of identical
	// incidents hold the window open forever, which is the opposite of what a grace time is for.
	if (Result.Delta > 0)
	{
		Police.GraceSpawned = Result.Desired;
		Police.GraceUntil = Now + CopGraceSeconds;
	}
	return Result;
}

FMasqueradeVerdict DecideMasquerade(const FElysiumPoliceState& Police, double Now)
{
	FMasqueradeVerdict Verdict;
	if (Now < Police.MasqueradeTimerNext)
	{
		return Verdict;
	}
	Verdict.bIncrement = true;
	Verdict.NextDeadline = Now + MasqueradeTimerSeconds;
	return Verdict;
}

// ================================================================================================
// The pure pursuit / alert edges
// ================================================================================================

EEdge SetCopsInPursuit(FElysiumPoliceState& Police, int32 NewCount, double Now)
{
	NewCount = FMath::Max(0, NewCount);
	const int32 Previous = Police.CopsInPursuit;
	Police.CopsInPursuit = NewCount;
	if (Previous == 0 && NewCount > 0)
	{
		// The zero-to-one edge ZEROES the retained alert deadline rather than clearing the byte, so
		// the next rule pass is what expires the alert and fires its end output. Reproduced exactly:
		// collapsing it into an immediate clear here would lose the output.
		Police.HeightenedAlertExpiry = 0.0;
		return EEdge::Start;
	}
	if (Previous > 0 && NewCount == 0)
	{
		Police.bHeightenedAlert = true;
		Police.HeightenedAlertExpiry = Now + HeightenedAlertExpireSeconds;
		return EEdge::End;
	}
	return EEdge::None;
}

EEdge SetHuntersInPursuit(FElysiumPoliceState& Police, int32 NewCount)
{
	NewCount = FMath::Max(0, NewCount);
	const int32 Previous = Police.HuntersInPursuit;
	Police.HuntersInPursuit = NewCount;
	if (Previous == 0 && NewCount > 0) { return EEdge::Start; }
	if (Previous > 0 && NewCount == 0) { return EEdge::End; }
	return EEdge::None;
}

bool ExpireHeightenedAlert(FElysiumPoliceState& Police, double Now)
{
	if (!Police.bHeightenedAlert || Now < Police.HeightenedAlertExpiry)
	{
		return false;
	}
	Police.bHeightenedAlert = false;
	return true;
}

// ================================================================================================
// The wiring half — producers
// ================================================================================================

namespace
{
	void ApplyTimedWrite(FElysiumPlayer& Player, const TCHAR* ChannelName,
		ElysiumNpcWitness::EChannel Channel, int32& Level, double& Expiry, int32& Count,
		int32 Requested, float Duration)
	{
		FChannel ChannelState;
		ChannelState.Level = Level;
		ChannelState.Expiry = Expiry;
		ChannelState.Count = Count;

		const double Now = Player.World ? Player.World->NowSeconds() : 0.0;
		const FWriteResult Result = WriteTimedChannel(ChannelState, Requested, Duration, Now);

		Level = ChannelState.Level;
		Expiry = ChannelState.Expiry;
		Count = ChannelState.Count;

		if (Result.bCleared)
		{
			UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s %s activity cleared"),
				*Player.DebugString(), ChannelName);
			return;
		}
		// ============ Cycle 10c hunk 2/3 — the world-event lane's one producer ==================
		// An act that counted an incident is also published as an expiring world law record, which
		// is what an NPC's global witness lane accepts on cone, `m_flSeekDistInspection` and a
		// trace. The record's severity is the level written and its origin is where the player was
		// when it happened, which is exactly the "severity, origin and offender" the recovered
		// record carries.
		//
		// CHOSEN, NOT RECOVERED — that the ACT publishes rather than the witnessed incident. Two
		// things settle it. The record describes a crime, not a police call: a witnessed incident
		// already has a consumer of its own, and the whole point of the parallel lane is that an NPC
		// which never saw the offender still saw the event. And publishing from the incident
		// consumers is not merely different but unstable: an NPC's own submission would publish a
		// record that the same NPC (and every other) then accepts, submits again and republishes,
		// with nothing in the recovered material to stop it — the global lane has no processed count
		// of its own. Publishing here gives the lane exactly one record per act.
		if (Result.bCounted)
		{
			ElysiumNpcWitness::PublishLawEvent(Player.World, Channel, Level, Player.Origin,
				Player.Handle);
		}
		// =======================================================================================
		UE_LOG(LogElysiumPlayer, Verbose,
			TEXT("%s %s activity %d for %.2fs (%s, act count %d)"),
			*Player.DebugString(), ChannelName, Level, Result.Duration,
			Result.bRaised ? TEXT("raised") : TEXT("refreshed"), Count);
	}
}

void SetCriminalLevel(FElysiumPlayer& Player, int32 Level, float Duration)
{
	ApplyTimedWrite(Player, TEXT("criminal"), ElysiumNpcWitness::EChannel::Criminal,
		Player.Law.Criminal, Player.Law.CriminalExpiry, Player.Law.CriminalCount, Level, Duration);
}

void SetSupernaturalLevel(FElysiumPlayer& Player, int32 Level, float Duration)
{
	ApplyTimedWrite(Player, TEXT("supernatural"), ElysiumNpcWitness::EChannel::Supernatural,
		Player.Law.Supernatural, Player.Law.SupernaturalExpiry, Player.Law.SupernaturalCount,
		Level, Duration);
}

void SetInvestigateLevel(FElysiumPlayer& Player, int32 Level)
{
	Player.Law.Investigate = WriteDirectChannel(Level);
	UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s investigate activity %d"),
		*Player.DebugString(), Player.Law.Investigate);
}

// SEAM (comment, nothing failed): the fourth recovered producer is the compact player action 300,
// `LockPick`, which REASSERTS criminal level 1 for as long as the player is picking. This runtime
// has no player-action classifier and no compact action table — the same absence the Discipline
// world-area gate reports for the Bloodbuff/LockPick Elysium exception, and the same one the
// targeted-Discipline aim path reports for the retained action-target handle. When that classifier
// lands, its criminal reassertion is one `SetCriminalLevel(Player, 1)` per classified frame; the
// channel it writes into needs nothing further.

// ================================================================================================
// The wiring half — the world outputs
// ================================================================================================

namespace Outputs
{
	#define ELYSIUM_LAW_OUTPUT(Fn, Name) \
		const FName& Fn() { static const FName N(TEXT(Name)); return N; }

	ELYSIUM_LAW_OUTPUT(StartCopPursuit,    "OnStartCopPursuitMode")
	ELYSIUM_LAW_OUTPUT(EndCopPursuit,      "OnEndCopPursuitMode")
	ELYSIUM_LAW_OUTPUT(StartCopAlert,      "OnStartCopAlertMode")
	ELYSIUM_LAW_OUTPUT(EndCopAlert,        "OnEndCopAlertMode")
	ELYSIUM_LAW_OUTPUT(StartHunterPursuit, "OnStartHunterPursuitMode")
	ELYSIUM_LAW_OUTPUT(EndHunterPursuit,   "OnEndHunterPursuitMode")
	ELYSIUM_LAW_OUTPUT(CopsComing,         "OnCopsComing")
	ELYSIUM_LAW_OUTPUT(CopsOutside,        "OnCopsOutside")

	#undef ELYSIUM_LAW_OUTPUT
}

void FireWorldEvent(FElysiumEntityWorld& World, const FName& Output,
	const FElysiumEntityHandle& Activator)
{
	// Corpus provenance: all eight names are authored output rows on `events_world` across the 23
	// exported maps (`OnCopsOutside` 41, `OnStartCopPursuitMode`/`OnEndCopPursuitMode` 31 each,
	// `OnEndCopAlertMode` 24, and 23 each for the rest), so this is the recovered owner and not a
	// convenient one. The cop-CAR outputs (`OnSpawnOneCopCar`, `OnSpawnTwoCopCars`,
	// `OnSpawnNoCopCars`, `OnDelaySpawn*`, `OnCopsInPursuit`) are the other owner — `info_landmark`,
	// per the `CBaseLandmark` datamap — and belong to whatever lands the spawn seam below.
	FElysiumEntity* WorldEvents = FindWorldEvents(World);
	if (WorldEvents == nullptr)
	{
		// An ordinary absence: a headless test world or a map with no `events_world` has no bus to
		// fire on, and no authored consumer to disappoint.
		return;
	}
	WorldEvents->FireOutput(Output, Activator);
}

// ================================================================================================
// The wiring half — the world area
// ================================================================================================

int32 WorldAreaType(const FElysiumEntityWorld& World)
{
	static const FName SafeAreaField(TEXT("safearea"));
	if (const FElysiumEntity* WorldEvents = FindWorldEvents(World))   // read-only use
	{
		int32 Area = 0;
		if (ReadRegisteredInt(*WorldEvents, SafeAreaField, Area))
		{
			return FMath::Clamp(Area, 0, 2);
		}
		ReportOnce(TEXT("worldarea.field"),
			TEXT("`events_world` carries no registered `safearea` field — the world-area gate and "
				"the incident guards cannot read the area type"));
	}
	// No `events_world` in this map: the authored `worldspawn` key is the same baseline the entity
	// would have been seeded from, so reading it directly is the same answer and not a fallback
	// invention.
	FString Authored;
	if (WorldSpawnKey(World, TEXT("safearea"), Authored))
	{
		return FMath::Clamp(FCString::Atoi(*Authored), 0, 2);
	}
	return static_cast<int32>(EArea::Combat);
}

void ApplyWorldAreaTransition(FElysiumEntityWorld& World, int32 NewArea)
{
	FElysiumPlayer* Player = World.FindPlayer();
	if (Player == nullptr)
	{
		return;   // no connected player to apply the policy to
	}
	switch (static_cast<EArea>(FMath::Clamp(NewArea, 0, 2)))
	{
	case EArea::Elysium:
		// Value 2 runs the ordinary all-Discipline teardown, including owned timed events and
		// targeted effects — the one teardown `vdiscipline_endall` and the map boundary already use.
		ElysiumDisciplines::ClearAll(*Player);
		// SEAM: retail ALSO equips `item_w_unarmed` on this transition, and
		// `CBaseCombatCharacter::Weapon_CanSwitchTo` then admits only that item while Elysium is
		// active. This runtime has no world-area consumer inside weapon switching — the admission
		// predicate `Weapon_CanSwitchTo` does not exist as a seam an area policy can join — so a
		// player who walks into Elysium keeps whatever is drawn. Closing it is that one predicate
		// plus the forced equip here; nothing else about the transition is missing.
		ReportOnce(TEXT("elysium.unarmed"),
			TEXT("the Elysium unarmed enforcement is unbuilt — entering area type 2 tears down "
				"every Discipline but does not force `item_w_unarmed`, because this runtime has no "
				"`Weapon_CanSwitchTo` admission predicate for a world-area policy to join"));
		break;

	case EArea::Safe:
		// Value 1 ends ONLY compiled indices 3 and 11 — Celerity and Protean. Everything else the
		// player has running survives entering a safe area.
		ElysiumDisciplines::EndNative(*Player, ElysiumDisciplines::Celerity);
		ElysiumDisciplines::EndNative(*Player, ElysiumDisciplines::Protean);
		break;

	case EArea::Combat:
	default:
		// Value 0 has no immediate player teardown; it only stops suppressing incidents.
		break;
	}
}

// ================================================================================================
// The wiring half — the two witnessed-incident consumers
// ================================================================================================

// The two consumers below are reached from an NPC's schedule branch, after condition gathering set
// one of the four law conditions (`COND_CRIMINAL_FLEE_LEVEL` 31, `COND_CRIMINAL_ATTACK_LEVEL` 32,
// `COND_SUPERNATURAL_FLEE_LEVEL` 33, `COND_SUPERNATURAL_ATTACK_LEVEL` 34) by comparing the player's
// act counts against that NPC's own processed counts. That producer is
// `ElysiumNpcWitness::SelectLawSchedule`; the act counts it reads are public through
// `FElysiumPlayer::CriminalActCount()` / `SupernaturalActCount()`, and the entry points below take
// exactly the severity, witness and position the NPC's retained incident record carries.

const TCHAR* AdmissionName(EAdmission Admission)
{
	switch (Admission)
	{
	case EAdmission::Accepted:          return TEXT("accepted");
	case EAdmission::RefusedCombatArea: return TEXT("refused (combat area)");
	case EAdmission::RefusedHunters:    return TEXT("refused (hunters in pursuit)");
	case EAdmission::RefusedRateLimited:return TEXT("refused (masquerade timer)");
	default:                            return TEXT("?");
	}
}

namespace
{
	// The shared police-response admission both incident consumers enter.
	EQueueVerdict EnterResponseAdmission(FElysiumPlayer& Player, int32 Severity,
		const FElysiumEntityHandle& Witness, const FVector& Position)
	{
		const double Now = Player.World ? Player.World->NowSeconds() : 0.0;
		// The delay is drawn HERE, off the owned Dice stream (S8), and handed to the pure rule — so a
		// save carries the draw across a load and a seeded test can name the deadline.
		const double Delay = static_cast<double>(ElysiumRng::Stream(EElysiumRngStream::Dice)
			.FRandRange(static_cast<float>(ResponseTimerMinSeconds),
				static_cast<float>(ResponseTimerMaxSeconds)));
		return QueueResponse(Player.Police, Severity, Witness, Position, Now, Delay);
	}

	// The terminal thunks' shared guard: world `m_nAreaType != 0`. Combat suppresses an otherwise
	// witnessed incident at admission — it does NOT suppress the activity write that produced it.
	bool AreaAdmitsIncident(const FElysiumPlayer& Player)
	{
		return Player.World == nullptr
			|| WorldAreaType(*Player.World) != static_cast<int32>(EArea::Combat);
	}
}

EAdmission PlayerCriminalIncident(FElysiumPlayer& Player, int32 Severity,
	const FElysiumEntityHandle& Witness, const FVector& Position)
{
	if (!AreaAdmitsIncident(Player))
	{
		UE_LOG(LogElysiumPlayer, Verbose,
			TEXT("%s criminal incident (severity %d) suppressed: world area type 0"),
			*Player.DebugString(), Severity);
		return EAdmission::RefusedCombatArea;
	}
	// This consumer does not touch Masquerade, ever. Breaking human law is not a Masquerade
	// violation (`docs/vtmb/game_runtime.md` § "Zone legality"), and the only route to the counter
	// is the supernatural consumer below.
	const EQueueVerdict Verdict = EnterResponseAdmission(Player, Severity, Witness, Position);
	UE_LOG(LogElysiumPlayer, Log, TEXT("%s criminal incident (severity %d): police response %s"),
		*Player.DebugString(), Severity,
		Verdict == EQueueVerdict::RejectedHunters ? TEXT("refused — hunters in pursuit")
			: Verdict == EQueueVerdict::RejectedLower ? TEXT("already pending at this severity")
			: Verdict == EQueueVerdict::ReplacedHigher ? TEXT("replaced at a higher severity")
			: TEXT("queued"));
	return (Verdict == EQueueVerdict::RejectedHunters) ? EAdmission::RefusedHunters
		: EAdmission::Accepted;
}

EAdmission PlayerSupernaturalIncident(FElysiumPlayer& Player, int32 Severity,
	const FElysiumEntityHandle& Witness, const FVector& Position)
{
	if (!AreaAdmitsIncident(Player))
	{
		UE_LOG(LogElysiumPlayer, Verbose,
			TEXT("%s supernatural incident (severity %d) suppressed: world area type 0"),
			*Player.DebugString(), Severity);
		return EAdmission::RefusedCombatArea;
	}

	// 1. The rate-limited Masquerade increment. The two consumers below are independent: the timer
	//    closing does not stop the police branch, and vice versa.
	const double Now = Player.World ? Player.World->NowSeconds() : 0.0;
	const FMasqueradeVerdict Verdict = DecideMasquerade(Player.Police, Now);
	EAdmission Result = EAdmission::RefusedRateLimited;
	if (Verdict.bIncrement)
	{
		Player.Police.MasqueradeTimerNext = Verdict.NextDeadline;
		Player.ChangeMasqueradeLevel(+1);
		Result = EAdmission::Accepted;
		UE_LOG(LogElysiumPlayer, Log,
			TEXT("%s supernatural incident (severity %d): masquerade now %d, next window at %.2f"),
			*Player.DebugString(), Severity, Player.GetMasqueradeLevel(), Verdict.NextDeadline);
	}
	else
	{
		UE_LOG(LogElysiumPlayer, Verbose,
			TEXT("%s supernatural incident (severity %d): masquerade rate-limited until %.2f"),
			*Player.DebugString(), Severity, Player.Police.MasqueradeTimerNext);
	}

	// SEAM (comment, nothing failed): retail's `ChangeMasqueradeLevel` itself fires the
	// `events_world` output for the resulting level (`OnMasqueradeLevel1..5`, 23 authored rows
	// each) plus the generic `OnMasqueradeLevelChanged` (25 rows). This runtime's
	// `FElysiumCombatCharacter::ChangeMasqueradeLevel` mutates the sheet slot and reports the
	// breach but fires neither, so those 140-odd authored rows have no producer. Closing it belongs
	// beside the mutation, not here — firing from this call site would leave the datamap input
	// `ChangeMasqueradeLevel` silent and put the rule in two places.

	// 2. The independent police branch. Held in a local so the switch reads as the runtime policy it
	//    is rather than as a folded literal.
	const bool bCopSpawnEnabled = bSupernaturalCopSpawn;
	if (bCopSpawnEnabled)
	{
		const EQueueVerdict Queue = EnterResponseAdmission(Player, Severity, Witness, Position);
		if (Queue == EQueueVerdict::RejectedHunters && Result != EAdmission::Accepted)
		{
			Result = EAdmission::RefusedHunters;
		}
	}
	return Result;
}

// ================================================================================================
// Cycle 10c hunk 1/3 — the player-owned scare queue
// ================================================================================================

void QueueScareRecord(FElysiumPlayer& Player, const FElysiumEntityHandle& Npc, int32 Severity)
{
	if (!Npc.IsSet())
	{
		UE_LOG(LogElysiumPlayer, Warning,
			TEXT("law: a scare record was queued with no NPC identity — the queue is keyed by it, "
				"so the record has nowhere to go"));
		return;
	}
	const double Now = Player.World ? Player.World->NowSeconds() : 0.0;
	for (FElysiumScareRecord& Record : Player.ScareQueue)
	{
		if (Record.Npc == Npc)
		{
			// "a repeat keeps the greater severity and refreshes its timestamp" — both halves, in
			// that order, so a weaker repeat cannot lower what the queue already holds.
			Record.Severity = FMath::Max(Record.Severity, Severity);
			Record.Time = Now;
			return;
		}
	}
	FElysiumScareRecord Record;
	Record.Npc = Npc;
	Record.Severity = Severity;
	Record.Time = Now;
	Player.ScareQueue.Add(Record);
}

bool ConsumeScareQueue(FElysiumPlayer& Player, double Now)
{
	// "removes consumed/expired records". Expiry first, so a record that aged out this pass cannot
	// be the one selected.
	Player.ScareQueue.RemoveAll([Now](const FElysiumScareRecord& Record)
		{ return Now - Record.Time >= ScareRecordLifetimeSeconds; });
	if (Player.ScareQueue.IsEmpty())
	{
		return false;
	}
	int32 Best = 0;
	for (int32 i = 1; i < Player.ScareQueue.Num(); ++i)
	{
		const FElysiumScareRecord& Candidate = Player.ScareQueue[i];
		const FElysiumScareRecord& Incumbent = Player.ScareQueue[Best];
		if (Candidate.Severity > Incumbent.Severity
			|| (Candidate.Severity == Incumbent.Severity && Candidate.Time < Incumbent.Time))
		{
			Best = i;
		}
	}
	const FElysiumScareRecord Selected = Player.ScareQueue[Best];
	Player.ScareQueue.RemoveAt(Best);

	// The record carries no position (16 bytes, stated in the header), so the scared NPC's own
	// origin is resolved here. An NPC that no longer resolves takes the player's own position: the
	// incident still happened, and the response's witness check is what handles a witness that has
	// since gone.
	const FElysiumEntity* Npc = Player.World ? Player.World->Resolve(Selected.Npc) : nullptr;
	const FVector Position = Npc != nullptr ? Npc->Origin : Player.Origin;
	const EAdmission Admission =
		PlayerSupernaturalIncident(Player, Selected.Severity, Selected.Npc, Position);
	UE_LOG(LogElysiumPlayer, Log, TEXT("%s scare record (severity %d, from %s): %s"),
		*Player.DebugString(), Selected.Severity, *Selected.Npc.ToString(),
		AdmissionName(Admission));
	return true;
}

// ================================================================================================
// The wiring half — the pursuit counters and the expiry pass
// ================================================================================================

namespace
{
	void FirePursuitEdge(FElysiumPlayer& Player, EEdge Edge, const FName& StartOutput,
		const FName& EndOutput)
	{
		if (Edge == EEdge::None || Player.World == nullptr)
		{
			return;
		}
		FireWorldEvent(*Player.World, Edge == EEdge::Start ? StartOutput : EndOutput, Player.Handle);
	}
}

void SetCopPursuitCount(FElysiumPlayer& Player, int32 NewCount)
{
	const double Now = Player.World ? Player.World->NowSeconds() : 0.0;
	const EEdge Edge = SetCopsInPursuit(Player.Police, NewCount, Now);
	FirePursuitEdge(Player, Edge, Outputs::StartCopPursuit(), Outputs::EndCopPursuit());
	if (Edge == EEdge::End && Player.World)
	{
		// The one-to-zero edge raises heightened alert and announces it in the same transaction.
		FireWorldEvent(*Player.World, Outputs::StartCopAlert(), Player.Handle);
	}
}

void SetHunterPursuitCount(FElysiumPlayer& Player, int32 NewCount)
{
	const EEdge Edge = SetHuntersInPursuit(Player.Police, NewCount);
	FirePursuitEdge(Player, Edge, Outputs::StartHunterPursuit(), Outputs::EndHunterPursuit());
}

void TickPlayerLaw(FElysiumPlayer& Player, double Now)
{
	// --- 1. The two activity expiries ------------------------------------------------------------
	// DIVERGENCE (stated, not hidden): retail splits these. `PlayerRuleUpdate` (the first call of
	// `CHL2_Player::PreThink`) clears an expired SUPERNATURAL level, while the CRIMINAL level is
	// cleared by a law helper reached from `SetAnimation`'s path in `PostThink`. Both run inside the
	// same command transaction, one before movement and one after, so nothing an outside observer
	// can time separates them — and this runtime has no `SetAnimation` law helper to hang the second
	// half on. They are therefore done together in this one pass. If a producer ever needs the
	// half-frame ordering, it is this comment that says where the seam was.
	{
		FChannel Supernatural{ Player.Law.Supernatural, Player.Law.SupernaturalExpiry,
			Player.Law.SupernaturalCount };
		if (ExpireTimedChannel(Supernatural, Now))
		{
			Player.Law.Supernatural = Supernatural.Level;
			Player.Law.SupernaturalExpiry = Supernatural.Expiry;
			UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s supernatural activity expired"),
				*Player.DebugString());
		}
	}
	{
		FChannel Criminal{ Player.Law.Criminal, Player.Law.CriminalExpiry,
			Player.Law.CriminalCount };
		if (ExpireTimedChannel(Criminal, Now))
		{
			Player.Law.Criminal = Criminal.Level;
			Player.Law.CriminalExpiry = Criminal.Expiry;
			UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s criminal activity expired"),
				*Player.DebugString());
		}
	}

	// --- 1b. The scare queue ---------------------------------------------------------------------
	// Cycle 10c hunk 3/3. `PlayerRuleUpdate` "selects from that queue, submits the supernatural
	// incident and removes consumed/expired records", and `PlayerRuleUpdate` is the first call of
	// `PreThink` — which is this pass. It runs ahead of the response consume below so a scare
	// record's own incident can be the one that queues this pass's response, rather than always
	// waiting a think.
	ConsumeScareQueue(Player, Now);

	// --- 2. The delayed response-cop deadline ----------------------------------------------------
	{
		const bool bWitnessLive = Player.World != nullptr
			&& Player.World->Resolve(Player.Police.ResponseWitness) != nullptr;
		const FConsumeResult Response = ConsumeResponse(Player.Police, bWitnessLive, Now);
		if (Response.bConsumed)
		{
			if (Response.bWitnessLost)
			{
				UE_LOG(LogElysiumPlayer, Log,
					TEXT("%s police response (severity %d) dropped: its witness no longer resolves"),
					*Player.DebugString(), Response.Severity);
			}
			else if (Response.Delta <= 0)
			{
				UE_LOG(LogElysiumPlayer, Log,
					TEXT("%s police response (severity %d): %d cop(s) already on the street inside "
						"the grace window — nothing to add"),
					*Player.DebugString(), Response.Severity, Player.Police.GraceSpawned);
			}
			else
			{
				// The wait-area path is authored world policy, and the one half of the recovered
				// no-wait/wait split this runtime can perform faithfully: it fires the game-rules
				// waiting/coming outputs for a later release instead of constructing anything.
				int32 CopWaitArea = 0;
				static const FName CopWaitField(TEXT("copwaitarea"));
				const FElysiumEntity* WorldEvents = Player.World ? FindWorldEvents(*Player.World) : nullptr;
				const bool bWaitArea = WorldEvents != nullptr
					&& ReadRegisteredInt(*WorldEvents, CopWaitField, CopWaitArea) && CopWaitArea != 0;
				if (bWaitArea)
				{
					FireWorldEvent(*Player.World, Outputs::CopsComing(), Player.Handle);
					FireWorldEvent(*Player.World, Outputs::CopsOutside(), Player.Handle);
					UE_LOG(LogElysiumPlayer, Log,
						TEXT("%s police response (severity %d): cop wait area, %d cop(s) held for "
							"release"),
						*Player.DebugString(), Response.Severity, Response.Delta);
				}
				else
				{
					// **SEAM** — the no-wait path. Retail marks the player as the response target and
					// invokes the CONFIGURED GLOBAL NPC MAKER once per required delta, reporting
					// `Failed to spawn with NPCMaker` when one attempt fails. This runtime has no such
					// concept: `FElysiumNpcMaker` is a map-authored `npc_maker` entity reached by
					// targetname, and nothing registers one as the game-wide police maker, so there is
					// no receiver to invoke and no cop body to place. Everything up to this line — the
					// admission, the random deadline, the severity replacement, the witness check, the
					// desired-cop arithmetic and the grace delta — is built and tested; only the
					// invocation is missing.
					ReportOnce(TEXT("response.spawn"),
						FString::Printf(TEXT("the police-response spawn is unbuilt — a severity-%d "
							"incident resolved to %d cop(s) with nothing to spawn them: this runtime "
							"registers no global NPC maker for the response to invoke"),
							Response.Severity, Response.Delta));
				}
			}
		}
	}

	// --- 3. The heightened-alert expiry ----------------------------------------------------------
	if (ExpireHeightenedAlert(Player.Police, Now))
	{
		if (Player.World)
		{
			FireWorldEvent(*Player.World, Outputs::EndCopAlert(), Player.Handle);
		}
		UE_LOG(LogElysiumPlayer, Verbose, TEXT("%s heightened alert expired"),
			*Player.DebugString());
	}
}

}   // namespace ElysiumLaw
