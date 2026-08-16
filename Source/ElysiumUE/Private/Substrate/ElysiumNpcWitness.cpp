#include "Substrate/ElysiumNpcWitness.h"

#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumPlayer.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSaveTypes.h"
#include "ElysiumSheetSlots.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRelationships.h"

namespace
{
	using EChannel = ElysiumNpcWitness::EChannel;

	constexpr int32 ChannelCount = static_cast<int32>(EChannel::Count);

	// The one world term the engine answers (K13), reached exactly as the senses reach it: a
	// headless world has no embodiment and the service's own default is CLEAR, so a `-nullrhi`
	// Substrate run can still prove the cone and distance halves of the acceptance.
	bool SegmentClear(const FElysiumEntityWorld* World, const FVector& FromCm, const FVector& ToCm)
	{
		const IElysiumEmbodiment* Embodiment = World ? World->Embodiment() : nullptr;
		return Embodiment == nullptr || Embodiment->QueryLineOfSight(FromCm, ToCm);
	}

	// `nosferatu_tolerrant`, off the world's own policy leaf. The SEAM this reads under is stated at
	// `OnClosestPlayerUpdated`; it is a registered `events_world` field, so this is the ordinary R2
	// walk and not a second store.
	bool WorldToleratesNosferatu(const FElysiumEntityWorld* World)
	{
		if (World == nullptr)
		{
			return false;
		}
		static const FString WorldEventsClass(TEXT("events_world"));
		static const FName TolerantField(TEXT("nosferatu_tolerrant"));
		for (const TUniquePtr<FElysiumEntity>& Ent : World->Entities())
		{
			if (!Ent || !Ent->Def || Ent->IsRecordOnly() || Ent->Class == nullptr
				|| !Ent->Def->Classname.Equals(WorldEventsClass, ESearchCase::IgnoreCase))
			{
				continue;
			}
			const FElysiumFieldAccessor* Acc =
				FElysiumClassRegistry::Get().FindField(*Ent->Class, TolerantField);
			return Acc != nullptr && Acc->Get && Acc->Get(*Ent).ToInt() != 0;
		}
		return false;
	}

	// One channel's authored threshold pair, resolved. Kept in one place so the two lanes and the
	// consumer cannot disagree about which keyfield feeds which condition.
	struct FThresholds
	{
		int32 Flee = ElysiumNpcWitness::DefaultThreshold;
		int32 Attack = ElysiumNpcWitness::DefaultThreshold;
		EElysiumNpcCond FleeCondition = EElysiumNpcCond::CriminalFleeLevel;
		EElysiumNpcCond AttackCondition = EElysiumNpcCond::CriminalAttackLevel;
	};

	FThresholds ThresholdsFor(const FElysiumNpc& Npc, EChannel Channel)
	{
		FThresholds Out;
		if (Channel == EChannel::Criminal)
		{
			Out.Flee = ElysiumNpcWitness::ResolveThreshold(Npc.PlCriminalFlee);
			Out.Attack = ElysiumNpcWitness::ResolveThreshold(Npc.PlCriminalAttack);
			Out.FleeCondition = EElysiumNpcCond::CriminalFleeLevel;
			Out.AttackCondition = EElysiumNpcCond::CriminalAttackLevel;
			return Out;
		}
		Out.Flee = ElysiumNpcWitness::ResolveThreshold(Npc.PlSupernaturalFlee);
		Out.Attack = ElysiumNpcWitness::ResolveThreshold(Npc.PlSupernaturalAttack);
		Out.FleeCondition = EElysiumNpcCond::SupernaturalFleeLevel;
		Out.AttackCondition = EElysiumNpcCond::SupernaturalAttackLevel;
		return Out;
	}

	// What one pass observed, before anything is written to the NPC. The retained record is
	// "the strongest criminal and supernatural entries independently", so the strongest observation
	// of the pass — from either lane — is what survives into the leaf.
	struct FPassObservation
	{
		bool bFresh = false;
		int32 Severity = 0;
		FVector Origin = FVector::ZeroVector;
		FElysiumEntityHandle Offender;
		bool bFleeOnly = false;

		void Offer(int32 InSeverity, const FVector& InOrigin, const FElysiumEntityHandle& InOffender,
			bool bInFleeOnly)
		{
			if (bFresh && InSeverity <= Severity)
			{
				return;
			}
			bFresh = true;
			Severity = InSeverity;
			Origin = InOrigin;
			Offender = InOffender;
			bFleeOnly = bInFleeOnly;
		}
	};
}

namespace ElysiumNpcWitness
{

const TCHAR* ChannelName(EChannel Channel)
{
	switch (Channel)
	{
	case EChannel::Criminal:     return TEXT("criminal");
	case EChannel::Supernatural: return TEXT("supernatural");
	default:                     return TEXT("?");
	}
}

// ================================================================================================
// The authored threshold rule
// ================================================================================================

int32 ResolveThreshold(int32 Authored)
{
	// The reasoning for the negative arm is on the declaration.
	return Authored < 0 ? DefaultThreshold : Authored;
}

bool PassesThreshold(int32 Level, int32 ResolvedThreshold)
{
	// An authored 6 (or anything above the clamp) can never be reached by a `0..5` activity level.
	// The comparison alone already says so; the explicit test is what makes the disable readable at
	// the call site rather than an emergent property of two clamps.
	if (ResolvedThreshold > ElysiumLaw::MaxActivityLevel)
	{
		return false;
	}
	return Level >= ResolvedThreshold;
}

// ================================================================================================
// The window rule
// ================================================================================================

bool IsWindowOpen(double Deadline, double Now)
{
	// THE READING, implemented once. See the header for the two documents it is taken from and for
	// the seam that would settle it.
	return Now < Deadline;
}

bool IsChannelOpen(const FElysiumNpc& Npc, EChannel Channel, double Now)
{
	return IsWindowOpen(Npc.Witness.Channel(Channel).IgnoreUntil, Now);
}

// ================================================================================================
// The global-event lane's record store
// ================================================================================================

FElysiumLawEvent FElysiumLawEventBus::Publish(EChannel Channel, int32 Severity,
	const FVector& Origin, const FElysiumEntityHandle& Offender, double Now)
{
	Evict(Now);
	FElysiumLawEvent Event;
	Event.Channel = Channel;
	Event.Severity = Severity;
	Event.Origin = Origin;
	Event.Offender = Offender;
	Event.Time = Now;
	Event.ExpiresAt = Now + RecordLifetimeSeconds;
	Event.Serial = ++Serial;
	Events.Add(Event);
	return Event;
}

void FElysiumLawEventBus::Collect(double Now, TArray<FElysiumLawEvent>& Out) const
{
	Out.Reset();
	for (const FElysiumLawEvent& Event : Events)
	{
		if (Now < Event.ExpiresAt)
		{
			Out.Add(Event);
		}
	}
}

void FElysiumLawEventBus::Evict(double Now)
{
	Events.RemoveAll([Now](const FElysiumLawEvent& Event) { return Now >= Event.ExpiresAt; });
	// The count cap is a ceiling and not a budget: nothing depends on a record surviving to it, and
	// a firefight must not grow the store without bound. The oldest go first.
	while (Events.Num() >= MaxRetained)
	{
		Events.RemoveAt(0);
	}
}

void FElysiumLawEventBus::Reset()
{
	Events.Reset();
	Serial = 0;
}

void PublishLawEvent(FElysiumEntityWorld* World, EChannel Channel, int32 Severity,
	const FVector& Origin, const FElysiumEntityHandle& Offender)
{
	if (World == nullptr)
	{
		return;   // an ordinary absence: a detached player has no world to publish into
	}
	World->LawEvents().Publish(Channel, Severity, Origin, Offender, World->NowSeconds());
}

}   // namespace ElysiumNpcWitness

// ================================================================================================
// FElysiumNpcWitness — the per-NPC retained state
// ================================================================================================

void FElysiumNpcWitness::Reset()
{
	*this = FElysiumNpcWitness();
}

void FElysiumNpcWitness::Serialize(FElysiumSaveArchive& Ar)
{
	for (int32 i = 0; i < ChannelCount; ++i)
	{
		Ar << Channels[i].Processed;
		Ar << Channels[i].Level;
		Ar << Channels[i].Location;
		Ar << Channels[i].Offender;
		Ar << Channels[i].IgnoreUntil;
	}
	uint8 FleeOnly = bSupernaturalFleeOnly ? 1 : 0;
	Ar << FleeOnly;
	Ar << NosferatuIgnoreUntil;
	if (Ar.IsLoading())
	{
		bSupernaturalFleeOnly = FleeOnly != 0;
		for (int32 i = 0; i < ChannelCount; ++i)
		{
			// A payload written by another build must not be able to make an act count run backwards:
			// the processed count is monotonic by construction, and a negative one would replay every
			// act the player has ever committed.
			Channels[i].Processed = FMath::Max(0, Channels[i].Processed);
			Channels[i].Level = FMath::Clamp(Channels[i].Level, 0, ElysiumLaw::MaxActivityLevel);
		}
	}
	// The global lane's consumed-serial cursor is deliberately NOT here. The store it indexes is
	// session state (K8, the reasoning is on `FElysiumLawEventBus`), so a saved serial would name a
	// record that no longer exists; `Rebase` starts it at the live head instead, which is exactly
	// what the sound cursor does for the same reason.
}

void FElysiumNpcWitness::Rebase(const FElysiumEntityWorld& World)
{
	for (int32 i = 0; i < ChannelCount; ++i)
	{
		Channels[i].Offender = World.RebaseSavedHandle(Channels[i].Offender);
		if (!Channels[i].Offender.IsSet())
		{
			// The retained record names one offender; with no offender there is nothing schedule
			// selection could submit, and the level/location would describe a witness to nothing.
			Channels[i].Level = 0;
			Channels[i].Location = FVector::ZeroVector;
		}
	}
	if (!Channels[static_cast<int32>(ElysiumNpcWitness::EChannel::Supernatural)].Offender.IsSet())
	{
		bSupernaturalFleeOnly = false;
	}
	GlobalCursor = World.LawEvents().LastSerial();
}

namespace ElysiumNpcWitness
{

// ================================================================================================
// The three recovered deadline setters
// ================================================================================================

void OpenFeedWindows(FElysiumNpc& Victim, double Now)
{
	Victim.Witness.Channel(EChannel::Criminal).IgnoreUntil = Now + FeedWindowSeconds;
	Victim.Witness.Channel(EChannel::Supernatural).IgnoreUntil = Now + FeedWindowSeconds;
	UE_LOG(LogElysiumNpcEnt, Verbose,
		TEXT("%s law windows opened by a feed for %.1fs"), *Victim.DebugString(), FeedWindowSeconds);
}

void OnEnteredAlertState(FElysiumNpc& Npc, double Now)
{
	Npc.Witness.Channel(EChannel::Criminal).IgnoreUntil = Now + StateChangeWindowSeconds;
	UE_LOG(LogElysiumNpcEnt, Verbose,
		TEXT("%s criminal law window opened by a state change for %.1fs"),
		*Npc.DebugString(), StateChangeWindowSeconds);
}

void OnClosestPlayerUpdated(FElysiumNpc& Npc, const FElysiumPlayer& Player, double Now)
{
	if (!Npc.Senses.Memory.bPlayerInRange)
	{
		return;   // an ordinary negative proximity result, not a failure
	}
	static const int32 NosferatuClan = FElysiumSheet::ClanFromName(TEXT("Nosferatu"));
	if (NosferatuClan == 0)
	{
		// `ClanFromName` answers 0 for a name it does not recognise, and 0 is also the value an
		// unseeded sheet carries — so a failed lookup would make EVERY player read as Nosferatu.
		// Reported once and refused rather than allowed to become that.
		static bool bReported = false;
		if (!bReported)
		{
			bReported = true;
			UE_LOG(LogElysiumNpcEnt, Error,
				TEXT("law: the clan encoding does not resolve 'Nosferatu' — the closest-player "
					"special case cannot identify one and is disabled"));
		}
		return;
	}
	if (Player.Sheet.Clan() != NosferatuClan)
	{
		return;
	}
	if (WorldToleratesNosferatu(Npc.World))
	{
		return;   // the marked `nosferatu_tolerrant` read; the SEAM is on the declaration
	}
	Npc.Witness.NosferatuIgnoreUntil = Now + NosferatuWindowSeconds;
}

// ================================================================================================
// The gather half
// ================================================================================================

bool IsLawPassSuppressed(const FElysiumNpc&)
{
	// SEAM (comment only, never true): "The pass is suppressed while the NPC has frenzy flag `0x10`
	// or is busy with a dynamic interaction." Neither system exists in this runtime — there is no
	// frenzy state on a character and no dynamic-interaction ownership beyond the body arbiter's own
	// K7 tokens, and the arbiter's owners are patrol, ambient, sequence, scripted schedule, follower
	// and dialogue, none of which is the recovered "dynamic interaction". Suppressing on a token
	// that merely looks similar would be a behaviour invented out of a name.
	//
	// What would close it: the frenzy flag word (one bit test) and whatever entity owns a dynamic
	// interaction. Both are one line here when they land.
	return false;
}

namespace
{
	// The direct-player lane, for one channel. What it observes is offered to the pass record.
	void RunDirectChannel(FElysiumNpc& Npc, const FElysiumPlayer& Player, EChannel Channel,
		double Now, FPassObservation& Out)
	{
		FElysiumNpcWitnessChannel& State = Npc.Witness.Channel(Channel);
		const int32 ActCount = Channel == EChannel::Criminal
			? Player.CriminalActCount() : Player.SupernaturalActCount();
		if (ActCount <= State.Processed)
		{
			return;   // nothing new to compare: this NPC has already accounted for every act
		}
		if (!IsWindowOpen(State.IgnoreUntil, Now))
		{
			// The recovered closed-window arm, verbatim: "the NPC advances its processed count
			// without producing the condition, so the same act is not replayed when observation
			// resumes."
			State.Processed = ActCount;
			Npc.RecordScheduleEvent(FString::Printf(
				TEXT("law: %s act %d passed unobserved (window closed)"),
				ChannelName(Channel), ActCount));
			return;
		}
		const int32 Level = Channel == EChannel::Criminal
			? Player.Law.Criminal : Player.Law.Supernatural;
		// The processed count is deliberately NOT advanced here. Schedule selection copies it when it
		// submits, which is the recovered split — an act the NPC saw but whose schedule never
		// selected the reaction is still unaccounted for on the next pass.
		Out.Offer(Level, Player.Origin, Player.Handle, /*bFleeOnly*/ false);
	}

	// The Nosferatu special case: a supernatural observation with no act-count comparison at all.
	void RunNosferatuCase(FElysiumNpc& Npc, const FElysiumPlayer& Player, double Now,
		FPassObservation& Out)
	{
		if (!IsWindowOpen(Npc.Witness.NosferatuIgnoreUntil, Now))
		{
			return;
		}
		// "Seeing a nearby `Player_Nosferatu` can create that severity-2 flee-only record
		// independently of a new supernatural activity count."
		Out.Offer(NosferatuSeverity, Player.Origin, Player.Handle, /*bFleeOnly*/ true);
	}

	// The global-event lane. One scan of the world's retained window, with the recovered three-term
	// acceptance and the consumed-serial cursor described at its declaration.
	void RunGlobalLane(FElysiumNpc& Npc, double Now, FPassObservation (&Out)[ChannelCount])
	{
		FElysiumEntityWorld* World = Npc.World;
		if (World == nullptr)
		{
			return;
		}
		TArray<FElysiumLawEvent> Records;
		World->LawEvents().Collect(Now, Records);
		if (Records.IsEmpty())
		{
			return;
		}
		// `m_flSeekDistInspection`. The datamap places `m_flSeekDistBase` at `+0x63b4` (the authored
		// `vision` keyvalue) and `InitPerceptionDistances` writes the DERIVED effective distance at
		// `+0x63b8`, one slot above it, with the hearing pair in the same base/derived arrangement at
		// `+0x63bc`/`+0x63c0` — so the inspection distance is the resolved visual radius this
		// runtime already carries as `Perception.VisionDistanceCm`, not a fourth authored number.
		const float SeekDistCm = Npc.Senses.Perception.VisionDistanceCm;
		uint64 HighestAccepted = Npc.Witness.GlobalCursor;
		for (const FElysiumLawEvent& Record : Records)
		{
			if (Record.Serial <= Npc.Witness.GlobalCursor)
			{
				continue;   // already consumed by this NPC
			}
			if (Record.Offender.IsSet() && Record.Offender == Npc.Handle)
			{
				continue;   // an NPC does not witness itself, the same rule hearing applies
			}
			// The recovered three-term acceptance: "the origin is inside its view cone, no farther
			// than `m_flSeekDistInspection`, and reached by an unobstructed trace".
			if (!FElysiumNpcSenses::IsInViewCone(Npc, Record.Origin))
			{
				continue;
			}
			if (SeekDistCm <= 0.f
				|| FVector::DistSquared(Npc.Origin, Record.Origin)
					> static_cast<double>(SeekDistCm) * SeekDistCm)
			{
				continue;
			}
			if (!SegmentClear(World, Npc.EyePosition(), Record.Origin))
			{
				continue;
			}
			// Accepted. "Among accepted records it retains the strongest criminal and supernatural
			// entries independently" — which is exactly what `Offer` does, per channel.
			Out[static_cast<int32>(Record.Channel)].Offer(Record.Severity, Record.Origin,
				Record.Offender, /*bFleeOnly*/ false);
			HighestAccepted = FMath::Max(HighestAccepted, Record.Serial);
		}
		// CHOSEN, NOT RECOVERED: the cursor advances over ACCEPTED records only, so a record this NPC
		// could not see stays available until it expires and is witnessed the moment the NPC turns
		// toward it — which is what the per-pass cone/trace acceptance is for. Something has to stop
		// an accepted record from re-raising its condition on every pass for its whole lifetime: the
		// direct lane has the processed count for exactly that job and the recovered material gives
		// the global lane no equivalent, so the sound bus's serial cursor is reused rather than a
		// second device invented. Without it one accepted record would resubmit its incident several
		// times a second.
		Npc.Witness.GlobalCursor = HighestAccepted;
	}
}

void GatherLawConditions(FElysiumNpc& Npc, double Now, FElysiumNpcConditions& Out)
{
	// "The native condition-gathering pass clears and recomputes `COND_INVESTIGATE_LEVEL` plus four
	// law conditions." The whole set is cleared here rather than relying on the caller's `Reset`, so
	// the recovered clear-and-recompute is a property of this function and survives a re-order.
	Out.Clear(EElysiumNpcCond::CriminalFleeLevel);
	Out.Clear(EElysiumNpcCond::CriminalAttackLevel);
	Out.Clear(EElysiumNpcCond::SupernaturalFleeLevel);
	Out.Clear(EElysiumNpcCond::SupernaturalAttackLevel);
	Out.Clear(EElysiumNpcCond::InvestigateLevel);

	if (IsLawPassSuppressed(Npc) || Npc.World == nullptr)
	{
		return;
	}

	FPassObservation Observed[ChannelCount];

	// --- The direct-player lane ------------------------------------------------------------------
	// "it requires a valid `m_hClosestPlayer` and `COND_SEE_PLAYER`". Both gate the WHOLE lane: an
	// unseen player is not compared and no processed count advances, which is the difference between
	// this gate and the closed-window arm inside it.
	const FElysiumNpcMemory& Memory = Npc.Senses.Memory;
	FElysiumPlayer* Player = Memory.ClosestPlayer.IsSet() ? Npc.World->FindPlayer() : nullptr;
	const bool bClosestPlayerValid = Player != nullptr && !Player->IsInert()
		&& Player->Handle == Memory.ClosestPlayer;
	// `COND_SEE_PLAYER`'s equivalent is the senses' committed player-LOS latch. It is already false
	// unless the player is in range AND in cone (`FElysiumNpcSenses::TickSight`), so it is the whole
	// of the recovered condition rather than one term of it.
	if (bClosestPlayerValid && Memory.bPlayerLos)
	{
		RunDirectChannel(Npc, *Player, EChannel::Criminal, Now, Observed[0]);
		RunDirectChannel(Npc, *Player, EChannel::Supernatural, Now, Observed[1]);
		RunNosferatuCase(Npc, *Player, Now, Observed[1]);

		// The investigate channel. It carries no act count, no deadline and no witnessed record —
		// "direct replacement with no companion timer or incident count in the player setter" — so it
		// is a bare level-versus-threshold test on every pass, under the same closest-player/sight
		// gate the rest of the lane runs under.
		if (PassesThreshold(Player->Law.Investigate, ResolveThreshold(Npc.PlInvestigate)))
		{
			Out.Set(EElysiumNpcCond::InvestigateLevel);
		}
	}

	// --- The global-event lane -------------------------------------------------------------------
	// Parallel, and deliberately NOT under the closest-player/sight gate: its own acceptance is cone,
	// distance and trace against the record's ORIGIN, which is how an NPC witnesses a crime whose
	// offender it cannot see.
	RunGlobalLane(Npc, Now, Observed);

	// --- Retain, then test both thresholds -------------------------------------------------------
	for (int32 i = 0; i < ChannelCount; ++i)
	{
		if (!Observed[i].bFresh)
		{
			continue;
		}
		const EChannel Channel = static_cast<EChannel>(i);
		FElysiumNpcWitnessChannel& State = Npc.Witness.Channel(Channel);
		// "Passing a threshold records the player as offender" — the retained record is written for
		// the strongest observation of the pass whether or not a threshold passes, because it is also
		// what a diagnostic reads to say what this NPC saw.
		State.Level = Observed[i].Severity;
		State.Location = Observed[i].Origin;
		State.Offender = Observed[i].Offender;
		if (Channel == EChannel::Supernatural)
		{
			Npc.Witness.bSupernaturalFleeOnly = Observed[i].bFleeOnly;
		}

		const FThresholds Thresholds = ThresholdsFor(Npc, Channel);
		const bool bFlee = PassesThreshold(State.Level, Thresholds.Flee);
		// The flee-only policy suppresses the ATTACK arm and nothing else: a Nosferatu on sight can
		// scare a bystander into running, and cannot make it start a fight.
		const bool bAttack = !(Channel == EChannel::Supernatural && Npc.Witness.bSupernaturalFleeOnly)
			&& PassesThreshold(State.Level, Thresholds.Attack);
		if (bFlee)
		{
			Out.Set(Thresholds.FleeCondition);
		}
		if (bAttack)
		{
			Out.Set(Thresholds.AttackCondition);
		}
		if (bFlee || bAttack)
		{
			Npc.RecordScheduleEvent(FString::Printf(
				TEXT("law: witnessed %s severity %d%s -> %s%s"),
				ChannelName(Channel), State.Level,
				Npc.Witness.bSupernaturalFleeOnly && Channel == EChannel::Supernatural
					? TEXT(" (flee only)") : TEXT(""),
				bFlee ? TEXT("flee ") : TEXT(""), bAttack ? TEXT("attack") : TEXT("")));
		}
	}
}

// ================================================================================================
// The select half
// ================================================================================================

namespace
{
	// The recovered submission transaction for one channel: submit (or queue the scare record), then
	// copy the act count.
	void SubmitChannel(FElysiumNpc& Npc, FElysiumPlayer& Player, EChannel Channel, bool bFleeOnly)
	{
		FElysiumNpcWitnessChannel& State = Npc.Witness.Channel(Channel);
		if (Channel == EChannel::Supernatural && bFleeOnly)
		{
			// "The supernatural flee-only schedule path instead queues a player-owned scare record."
			ElysiumLaw::QueueScareRecord(Player, Npc.Handle, State.Level);
		}
		else if (Channel == EChannel::Criminal)
		{
			const ElysiumLaw::EAdmission Admission =
				ElysiumLaw::PlayerCriminalIncident(Player, State.Level, Npc.Handle, State.Location);
			Npc.RecordScheduleEvent(FString::Printf(TEXT("law: submitted criminal %d -> %s"),
				State.Level, ElysiumLaw::AdmissionName(Admission)));
		}
		else
		{
			const ElysiumLaw::EAdmission Admission =
				ElysiumLaw::PlayerSupernaturalIncident(Player, State.Level, Npc.Handle, State.Location);
			Npc.RecordScheduleEvent(FString::Printf(TEXT("law: submitted supernatural %d -> %s"),
				State.Level, ElysiumLaw::AdmissionName(Admission)));
		}
		// "and copies the player's current act count into the NPC processed count". The copy runs on
		// the scare path too: it is the same sentence's transaction, and without it a flee-only NPC
		// would re-queue the same act on every pass — which is the exact replay the count exists to
		// prevent.
		State.Processed = Channel == EChannel::Criminal
			? Player.CriminalActCount() : Player.SupernaturalActCount();
	}
}

EElysiumScheduleId SelectLawSchedule(FElysiumNpc& Npc, double Now)
{
	(void)Now;
	const FElysiumNpcConditions& Cond = Npc.Cognition.Conditions;
	const bool bChannelRaised[ChannelCount] = {
		Cond.Has(EElysiumNpcCond::CriminalFleeLevel) || Cond.Has(EElysiumNpcCond::CriminalAttackLevel),
		Cond.Has(EElysiumNpcCond::SupernaturalFleeLevel)
			|| Cond.Has(EElysiumNpcCond::SupernaturalAttackLevel),
	};
	if (!bChannelRaised[0] && !bChannelRaised[1])
	{
		// SEAM (comment only): `COND_INVESTIGATE_LEVEL`'s own consumer. The `INVESTIGAT` family is 32
		// schedules whose contents the survey does not decode, and the numeric meanings of
		// `investigate_mode` / `investigate_mode_combat` / `full_investigate` are unrecovered — so
		// there is no program to select and no policy to select it with. The condition is gathered and
		// the three keyfields are carried; nothing reads either until one of those is decoded.
		return EElysiumScheduleId::None;
	}

	FElysiumPlayer* Player = Npc.World ? Npc.World->FindPlayer() : nullptr;
	if (Player == nullptr)
	{
		return EElysiumScheduleId::None;
	}

	bool bAttack = false;
	bool bFlee = false;
	FVector FleeFrom = FVector::ZeroVector;
	for (int32 i = 0; i < ChannelCount; ++i)
	{
		if (!bChannelRaised[i])
		{
			continue;
		}
		const EChannel Channel = static_cast<EChannel>(i);
		FElysiumNpcWitnessChannel& State = Npc.Witness.Channel(Channel);
		// "requires the retained offender still be the player". A record whose offender has since
		// stopped resolving to the player is dropped rather than submitted against whoever is there
		// now.
		if (!State.Offender.IsSet() || State.Offender != Player->Handle)
		{
			continue;
		}
		const bool bChannelFleeOnly =
			Channel == EChannel::Supernatural && Npc.Witness.bSupernaturalFleeOnly;
		const bool bChannelAttack = !bChannelFleeOnly
			&& (Channel == EChannel::Criminal
				? Cond.Has(EElysiumNpcCond::CriminalAttackLevel)
				: Cond.Has(EElysiumNpcCond::SupernaturalAttackLevel));
		bAttack |= bChannelAttack;
		if (Channel == EChannel::Criminal
			? Cond.Has(EElysiumNpcCond::CriminalFleeLevel)
			: Cond.Has(EElysiumNpcCond::SupernaturalFleeLevel))
		{
			bFlee = true;
			FleeFrom = State.Location;
		}
		SubmitChannel(Npc, *Player, Channel, bChannelFleeOnly);
	}

	if (bAttack)
	{
		// The marked hostility mechanism (the reasoning is on the declaration): a `D_HT` row toward
		// the player at `IRelationPriority`'s own no-row default, and then nothing. The ordinary
		// enemy transaction's gate, stickiness test and arbitration do the rest on the next pass, and
		// the composed selector picks the fight's schedule.
		if (Npc.Relationships.Resolve(Player->Handle, TEXT("player")) != EElysiumRelationship::Hate)
		{
			// The row can be REFUSED: `SetEntity` replaces an existing target only at an equal-or-higher
			// priority, so an authored `player_reaction` written above this one keeps the street. That is
			// an authored decision beating a derived one and not an error, but it is also the difference
			// between "the NPC turned hostile" and "nothing happened", so it is reported either way.
			const bool bInstalled = Npc.Relationships.SetEntity(Player->Handle,
				EElysiumRelationship::Hate, ElysiumNpcWitness::AttackRelationPriority);
			Npc.RecordScheduleEvent(bInstalled
				? FString(TEXT("law: attack threshold passed -> D_HT toward the player"))
				: FString::Printf(
					TEXT("law: attack threshold passed but the D_HT row was refused — an authored "
						"relationship at priority %d outranks it"),
					Npc.Relationships.ResolvePriority(Player->Handle, TEXT("player"))));
		}
		// Attack outranks flee (the reasoning is on the declaration): the ordinary selection below
		// this branch is what runs, so the NPC fights rather than retreating from the fight.
		return EElysiumScheduleId::None;
	}
	if (bFlee)
	{
		// The retreat is away from where the crime was witnessed, which is the position the retained
		// record carries and the same `SavePosition` stamp the ranged selector's own run-away route
		// uses.
		Npc.SavePosition = FleeFrom;
		Npc.RecordScheduleEvent(TEXT("law: flee threshold passed -> SCHED_TROIKA_RUN_AWAY"));
		return EElysiumScheduleId::RunAway;
	}
	return EElysiumScheduleId::None;
}

}   // namespace ElysiumNpcWitness
