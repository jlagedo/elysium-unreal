#include "Debug/ElysiumArenaScenarioRunner.h"

#if !UE_BUILD_SHIPPING

#include "Debug/ElysiumArenaCast.h"          // `player.armed`: the cast harness's own grants
#include "Debug/ElysiumGreenRoomShared.h"    // ResolveDriveBody: the player's pawn, mover and controller
#include "ElysiumDlg.h"                      // `dialog_choose`: the open turn's response band
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumInputRouter.h"              // `player_walk`: the input replay door `gr_walk` drives
#include "ElysiumMapActor.h"
#include "ElysiumMovementComponent.h"        // `player_crouch`: the duck's heading, read off the mover
#include "ElysiumNpcMindTypes.h"
#include "ElysiumPlayer.h"
#include "ElysiumPlayerBody.h"
#include "ElysiumPlayerController.h"
#include "ElysiumSessionSubsystem.h"
#include "ElysiumUserCmd.h"
#include "ElysiumVariant.h"
#include "ElysiumWorldServices.h"            // `player_crouched`: `IElysiumEmbodiment::IsPlayerDucking`
#include "Substrate/ElysiumWeaponClasses.h"
#include "Substrate/ElysiumItemClasses.h"    // `player_weapon`: the active item's classname
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumSchedule.h"
#include "Visual/ElysiumNpcBody.h"           // `on_ground`: the motor's floor answer

#include "CollisionQueryParams.h"            // `corpse_on_floor`: the floor under the pelvis
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"             // `light_pin`: retail's `debug_stealth_light` knob
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumArenaScenario, Log, All);

// Qualified rather than anonymous: a unity build concatenates translation units.
namespace ElysiumArenaRunnerDetail
{
	// How long a staged world is given to reach its `Activate`, wall seconds. The barrier waits on
	// the stage's own prerequisites (the model residency, the pawn's placement); a run that never sees
	// it is a harness error rather than a scenario that failed.
	constexpr double ActivationWaitSeconds = 60.0;

	// The radius a `player_walk` calls its destination reached, centimetres -- the one
	// `FElysiumGreenRoomRun::TickArenaWalkPlayer` uses for `gr_walk`.
	constexpr float WalkAcceptanceCm = 32.0f;

	// H20 `corpse_on_floor` (spec 0002 V4a seam; `stories/v4/packets-spike.md` findings 4, 5, 7). The
	// bone the probe reads, how far below it the floor is looked for, and the speed under which the
	// body is "at rest": a ragdoll here never sleeps (a residual 0.3-0.7 cm/s on the pelvis), and its
	// last settling sample measured 3.6 cm/s.
	const TCHAR* const CorpsePelvisBone = TEXT("Bip01 Pelvis");
	constexpr double CorpseFloorReachCm = 500.0;
	constexpr double CorpseRestSpeedCmPerSecond = 5.0;

	// H22: the runner's second own kind, one event per entity that left the entity world.
	FName RemovedKind()
	{
		static const FName Kind(TEXT("removed"));
		return Kind;
	}

	// Two numbers a probe reads are equal within this (a health is whole; a distance is not compared
	// for equality by any sensible record).
	constexpr double ProbeEpsilon = 1e-3;

	// Retail's `debug_stealth_light` (`0x109384d8`, default -1, range -1..10; `docs/vtmb/stealth.md`):
	// at 0..10 the normalized body light is replaced by `value * 0.1`, at -1 it is off.
	const TCHAR* const StealthLightKnob = TEXT("debug_stealth_light");
	constexpr float StealthLightOff = -1.0f;
	constexpr float StealthLightScale = 10.0f;   // a record's [0, 1] light as the knob's 0..10

	// The runner's own trace kind: one event per script action it runs.
	FName ScriptKind()
	{
		static const FName Kind(TEXT("script"));
		return Kind;
	}

	FElysiumVariant ToVariant(const FElysiumArenaValue& Value)
	{
		switch (Value.Type)
		{
		case FElysiumArenaValue::EType::Bool:
			return FElysiumVariant::Bool(Value.bBool);
		case FElysiumArenaValue::EType::Number:
			// A whole number marshals as an Int, as the `elysium_entity_fire` tool marshals a JSON one.
			return FMath::IsNearlyEqual(Value.Number, FMath::RoundToDouble(Value.Number))
				? FElysiumVariant::Int(static_cast<int32>(FMath::RoundToDouble(Value.Number)))
				: FElysiumVariant::Float(static_cast<float>(Value.Number));
		case FElysiumArenaValue::EType::String:
			return FElysiumVariant::String(Value.String);
		default:
			return FElysiumVariant::Void();
		}
	}

	// `who`: `player` is the player entity; anything else a targetname, the first live entity of that
	// name, else the first dead one (an `alive` probe asks about a body that may be dead).
	FElysiumEntity* FindEntity(FElysiumEntityWorld& World, const FString& Who)
	{
		if (Who.Equals(TEXT("player"), ESearchCase::IgnoreCase))
		{
			return World.FindPlayer();
		}
		FElysiumEntity* Dead = nullptr;
		for (const TUniquePtr<FElysiumEntity>& Entity : World.Entities())
		{
			if (!Entity.IsValid() || !Entity->TargetName.Equals(Who, ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (!Entity->IsDead())
			{
				return Entity.Get();
			}
			if (Dead == nullptr)
			{
				Dead = Entity.Get();
			}
		}
		return Dead;
	}

	FString OneLine(const FString& Text)
	{
		return Text.Replace(TEXT("\t"), TEXT(" ")).Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" "));
	}

	bool Compare(const FElysiumArenaValue& Answer, const FElysiumArenaProbeSpec& Probe)
	{
		switch (Probe.Compare)
		{
		case EElysiumArenaCompare::Equals:
			switch (Answer.Type)
			{
			case FElysiumArenaValue::EType::Bool:
				return Answer.bBool == Probe.Value.bBool;
			case FElysiumArenaValue::EType::Number:
				return FMath::Abs(Answer.Number - Probe.Value.Number) <= ProbeEpsilon;
			case FElysiumArenaValue::EType::String:
				return Answer.String.Equals(Probe.Value.String, ESearchCase::CaseSensitive);
			default:
				return false;
			}
		case EElysiumArenaCompare::Match:
			return Answer.String.Contains(Probe.Value.String, ESearchCase::CaseSensitive);
		case EElysiumArenaCompare::Less:
			return Answer.Number < Probe.Value.Number;
		case EElysiumArenaCompare::Greater:
			return Answer.Number > Probe.Value.Number;
		default:
			return false;
		}
	}
}

// --- The result ----------------------------------------------------------------------------------

TSharedRef<FJsonObject> FElysiumArenaScenarioResult::ToJson() const
{
	TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("name"), Name);
	Object->SetStringField(TEXT("result"), Result);
	Object->SetStringField(TEXT("known_red"), KnownRed);
	Object->SetStringField(TEXT("error"), Error);
	if (bHasFirstUnmet)
	{
		TSharedRef<FJsonObject> Unmet = MakeShared<FJsonObject>();
		Unmet->SetNumberField(TEXT("index"), UnmetIndex);
		Unmet->SetStringField(TEXT("section"), UnmetSection);
		TSharedRef<FJsonObject> Expect = MakeShared<FJsonObject>();
		Expect->SetStringField(TEXT("who"), UnmetWho);
		Expect->SetStringField(TEXT("kind"), UnmetKind);
		Expect->SetStringField(TEXT("match"), UnmetMatch);
		Unmet->SetObjectField(TEXT("expect"), Expect);
		Unmet->SetNumberField(TEXT("deadline"), UnmetDeadline);
		Unmet->SetStringField(TEXT("reason"), UnmetReason);
		Object->SetObjectField(TEXT("first_unmet"), Unmet);
	}
	else
	{
		const TSharedPtr<FJsonValue> Null = MakeShared<FJsonValueNull>();
		Object->SetField(TEXT("first_unmet"), Null);
	}
	Object->SetNumberField(TEXT("game_seconds"), GameSeconds);
	Object->SetNumberField(TEXT("wall_seconds"), WallSeconds);
	Object->SetNumberField(TEXT("events"), Events);
	Object->SetStringField(TEXT("trace"), Trace);
	if (!Note.IsEmpty())
	{
		Object->SetStringField(TEXT("note"), Note);
	}
	return Object;
}

FString FElysiumArenaScenarioResult::Summary() const
{
	FString Detail;
	if (!Error.IsEmpty())
	{
		Detail = Error;
	}
	else if (bHasFirstUnmet)
	{
		Detail = FString::Printf(TEXT("%s[%d] %s %s \"%s\" (deadline %.2f): %s"), *UnmetSection, UnmetIndex,
			UnmetWho.IsEmpty() ? TEXT("*") : *UnmetWho, *UnmetKind, *UnmetMatch, UnmetDeadline, *UnmetReason);
	}
	if (!KnownRed.IsEmpty())
	{
		Detail += FString::Printf(TEXT("%sknown red %s"), Detail.IsEmpty() ? TEXT("") : TEXT(" | "), *KnownRed);
	}
	if (!Note.IsEmpty())
	{
		Detail += FString::Printf(TEXT("%s%s"), Detail.IsEmpty() ? TEXT("") : TEXT(" | "), *Note);
	}
	return FString::Printf(TEXT("%s: %s (game %.2f s, wall %.2f s, %d event(s))%s%s"), *Name, *Result,
		GameSeconds, WallSeconds, Events, Detail.IsEmpty() ? TEXT("") : TEXT(" -- "), *Detail);
}

bool FElysiumArenaScenarioResult::IsVerdictFailure() const
{
	return Result == TEXT("fail") || Result == TEXT("unexpected-pass") || Result == TEXT("error");
}

// --- The runner ----------------------------------------------------------------------------------

FElysiumArenaScenarioRunner::FElysiumArenaScenarioRunner(const FElysiumArenaScenario& InRecord,
	const ElysiumArenaStage::FHost& InHost)
	: Record(InRecord)
	, Host(InHost)
{
	Result.Name = Record.Name;
	Result.KnownRed = Record.KnownRed;
	Result.Trace = FString::Printf(TEXT("%s.trace.tsv"), *Record.Name);
	MatchTimes.Init(-1.0, Record.Expect.Num());
	NeverScans.Init(0, Record.Never.Num());
	NeverCounts.Init(0, Record.Never.Num());
	ActionFired.Init(false, Record.Script.Num());
	ProbeRead.Init(false, Record.Probes.Num());
	PelvisSamples.SetNum(Record.Probes.Num());
}

FElysiumArenaScenarioRunner::~FElysiumArenaScenarioRunner()
{
	Detach();
}

bool FElysiumArenaScenarioRunner::Start(EZero Zero, FString& OutError)
{
	AElysiumMapActor* Map = Host.GetMap();
	FElysiumEntityWorld* World = Map != nullptr ? Map->GetEntityWorld() : nullptr;
	if (World == nullptr)
	{
		OutError = TEXT("no entity world to record");
		return false;
	}
	if (Zero == EZero::StageActivation && Map->IsRuntimeActive())
	{
		OutError = TEXT("the stage activated before the run could record it");
		return false;
	}

	for (const FElysiumArenaMatch& Spec : Record.Expect)
	{
		FMatcher& Matcher = ExpectMatchers.AddDefaulted_GetRef();
		Matcher.Spec = &Spec;
		if (Spec.bRegex)
		{
			Matcher.Pattern = MakeUnique<FRegexPattern>(Spec.Match);
		}
	}
	for (const FElysiumArenaMatch& Spec : Record.Never)
	{
		FMatcher& Matcher = NeverMatchers.AddDefaulted_GetRef();
		Matcher.Spec = &Spec;
		if (Spec.bRegex)
		{
			Matcher.Pattern = MakeUnique<FRegexPattern>(Spec.Match);
		}
	}

	InstalledWorld = World;
	InstalledEpoch = World->GetEpoch();
	// The sink captures the runner itself. It cannot outlive it: `Detach` (the destructor's) clears
	// it on the world it was installed on, and that world's own teardown clears it first if the world
	// goes before the runner does. It only appends; everything that acts runs on `Tick`.
	World->SetAiTraceSink([this](const FElysiumAiTraceEvent& Event) { RecordEvent(Event); });

	StartWall = FPlatformTime::Seconds();
	if (Zero == EZero::Now)
	{
		ZeroWorld = World->NowSeconds();
		bZeroKnown = true;
	}
	else
	{
		ReadyMap = Map;
		ReadyHandle = Map->OnRuntimeReady().AddRaw(this, &FElysiumArenaScenarioRunner::OnStageActivated);
	}
	bStarted = true;
	UE_LOG(LogElysiumArenaScenario, Log, TEXT("%s: recording (%d expect, %d never, %d probe(s), %d action(s), ")
		TEXT("duration %.1f s)%s"), *Record.Name, Record.Expect.Num(), Record.Never.Num(), Record.Probes.Num(),
		Record.Script.Num(), Record.Duration, bZeroKnown ? TEXT("") : TEXT("; zero is the stage's Activate"));
	return true;
}

void FElysiumArenaScenarioRunner::RecordEvent(const FElysiumAiTraceEvent& Event)
{
	FEvent& Recorded = Events.AddDefaulted_GetRef();
	Recorded.Time = Event.Time;
	Recorded.Entity = Event.Entity;
	Recorded.Name = Event.Name;
	Recorded.Kind = Event.Kind;
	Recorded.Text = Event.Text;
	if (Event.Kind == FName(TEXT("state")))
	{
		// NPCInit writes NONE directly (0x1029a0f5); MaintainSchedule's first SetState
		// (0x10281b63 -> 0x1026e340) establishes the NPC and runs real state-change hooks.
		// Keep that edge observable, but a state ban judges subsequent behaviour.
		Recorded.bEstablishingState = !EstablishedStateEntities.Contains(Event.Entity)
			&& Event.Text.StartsWith(TEXT("None -> "));
		EstablishedStateEntities.Add(Event.Entity);
	}
}

void FElysiumArenaScenarioRunner::OnStageActivated(AElysiumMapActor* Map)
{
	if (bZeroKnown || Map == nullptr || Map != Host.GetMap())
	{
		return;
	}
	// The broadcast closes `ActivateRuntime`, inside the frame whose game time `Activate(Now)` ran
	// at, so the clock still reads that `Now`: every event of the activation pass is at time zero.
	if (FElysiumEntityWorld* World = LiveWorld())
	{
		ZeroWorld = World->NowSeconds();
		bZeroKnown = true;
	}
}

FElysiumEntityWorld* FElysiumArenaScenarioRunner::LiveWorld() const
{
	if (InstalledWorld == nullptr)
	{
		return nullptr;
	}
	const AElysiumMapActor* Map = Host.GetMap();
	FElysiumEntityWorld* Current = Map != nullptr ? Map->GetEntityWorld() : nullptr;
	// The pointer alone could be a new world at a reused address; the epoch is unique per world.
	return (Current != nullptr && Current == InstalledWorld && Current->GetEpoch() == InstalledEpoch)
		? Current : nullptr;
}

void FElysiumArenaScenarioRunner::Detach()
{
	if (FElysiumEntityWorld* World = LiveWorld())
	{
		World->SetAiTraceSink(FElysiumAiTraceSink());
	}
	InstalledWorld = nullptr;
	if (ReadyHandle.IsValid())
	{
		if (AElysiumMapActor* Map = ReadyMap.Get())
		{
			Map->OnRuntimeReady().Remove(ReadyHandle);
		}
		ReadyHandle.Reset();
	}
	if (bLightPinned)
	{
		bLightPinned = false;
		if (IConsoleVariable* Knob = IConsoleManager::Get().FindConsoleVariable(
			ElysiumArenaRunnerDetail::StealthLightKnob))
		{
			Knob->Set(ElysiumArenaRunnerDetail::StealthLightOff, ECVF_SetByCode);
		}
	}
	CrouchStep = ECrouchStep::None;
	if (bWalking || bDrivingInput)
	{
		bWalking = false;
		bDrivingInput = false;
		const ElysiumGreenRoom::FDriveRefs Refs = ElysiumGreenRoom::ResolveDriveBody(Host.GetWorld());
		const AElysiumPlayerController* PC = Refs ? Cast<AElysiumPlayerController>(Refs.PC) : nullptr;
		if (UElysiumInputRouter* Router = PC != nullptr ? PC->GetInputRouter() : nullptr)
		{
			if (Router->IsReplaying())
			{
				Router->StopReplay();
			}
		}
	}
}

bool FElysiumArenaScenarioRunner::Matches(const FMatcher& Matcher, const FEvent& Event) const
{
	const FElysiumArenaMatch& Spec = *Matcher.Spec;
	if (Event.Kind != Spec.Kind)
	{
		return false;
	}
	if (!Spec.Who.IsEmpty() && !Event.Name.Equals(Spec.Who, ESearchCase::IgnoreCase))
	{
		// H21: `who: "player"` is the player ENTITY, whatever targetname it answers to (a tap names an
		// event by targetname and the player's is not `player`); the runner's own `script` events
		// carry that name already and matched above.
		const FElysiumEntityWorld* World = Spec.Who.Equals(TEXT("player"), ESearchCase::IgnoreCase)
			? LiveWorld() : nullptr;
		if (World == nullptr || !Event.Entity.IsSet() || !(Event.Entity == World->PlayerHandle()))
		{
			return false;
		}
	}
	if (Spec.Match.IsEmpty())
	{
		return true;
	}
	if (Matcher.Pattern.IsValid())
	{
		FRegexMatcher Regex(*Matcher.Pattern, Event.Text);
		return Regex.FindNext();
	}
	return Event.Text.Contains(Spec.Match, ESearchCase::CaseSensitive);
}

double FElysiumArenaScenarioRunner::ExpectDeadline(int32 Index) const
{
	const FElysiumArenaMatch& Spec = Record.Expect[Index];
	double Deadline = Record.Duration;
	if (Spec.bBy)
	{
		Deadline = FMath::Min(Deadline, Spec.By);
	}
	if (Spec.bWithin)
	{
		const double Previous = Index > 0 ? MatchTimes[Index - 1] : 0.0;
		Deadline = FMath::Min(Deadline, Previous + Spec.Within);
	}
	return Deadline;
}

void FElysiumArenaScenarioRunner::MatchExpectations()
{
	while (NextExpect < Record.Expect.Num())
	{
		const FMatcher& Matcher = ExpectMatchers[NextExpect];
		const double Deadline = ExpectDeadline(NextExpect);
		int32 Found = INDEX_NONE;
		int32 Scanned = ExpectScan;
		for (; Scanned < Events.Num(); ++Scanned)
		{
			const FEvent& Event = Events[Scanned];
			const double Time = ScenarioTime(Event);
			if (Time < PrevMatchTime || Consumed.Contains(Scanned))
			{
				continue;
			}
			if (Time > Deadline)
			{
				break;   // events arrive in time order: nothing later can make this deadline
			}
			if (Matches(Matcher, Event))
			{
				Found = Scanned;
				break;
			}
		}
		if (Found == INDEX_NONE)
		{
			ExpectScan = Scanned;
			return;
		}

		const double Time = ScenarioTime(Events[Found]);
		MatchTimes[NextExpect] = Time;
		Consumed.Add(Found);
		PrevMatchTime = Time;
		// The next expectation looks from the first event in this instant, not after this one: the
		// same think may have produced what it wants just before this match.
		int32 From = Found;
		while (From > 0 && ScenarioTime(Events[From - 1]) >= PrevMatchTime)
		{
			--From;
		}
		ExpectScan = From;
		if (bLogProgress)
		{
			const FEvent& Event = Events[Found];
			UE_LOG(LogElysiumArenaScenario, Display, TEXT("%s: expect[%d] met at t=%.2f -- %s %s %s"),
				*Record.Name, NextExpect, Time, Event.Name.IsEmpty() ? TEXT("-") : *Event.Name,
				*Event.Kind.ToString(), *Event.Text);
		}
		++NextExpect;
	}
}

bool FElysiumArenaScenarioRunner::NeverWindowStart(const FElysiumArenaMatch& Spec, double& OutStart) const
{
	if (Spec.AfterIndex != INDEX_NONE)
	{
		// A label never met never opens the window; the unmet expectation fails the run on its own.
		const double Met = MatchTimes.IsValidIndex(Spec.AfterIndex) ? MatchTimes[Spec.AfterIndex] : -1.0;
		if (Met < 0.0)
		{
			return false;
		}
		OutStart = Met + Spec.Delay;
		return true;
	}
	OutStart = Spec.bFrom ? Spec.From : 0.0;
	return true;
}

void FElysiumArenaScenarioRunner::FindNeverViolation(FFailure& Out)
{
	// Each `never` judges the events on its own cursor, so one waiting on a label holds its place
	// rather than letting events go by unjudged: once the label is met, the events from its match on
	// are still there to count. Expectations are matched first in the same tick, so a label met by
	// an event already recorded is known before any event after it is judged here.
	for (int32 Index = 0; Index < NeverMatchers.Num(); ++Index)
	{
		const FElysiumArenaMatch& Spec = *NeverMatchers[Index].Spec;
		double Opens = 0.0;
		if (!NeverWindowStart(Spec, Opens))
		{
			continue;
		}
		// `until` is absolute; `within` is relative to where the window opened (its label's match plus
		// `delay`); neither: the run's duration.
		const double Closes = Spec.bUntil ? Spec.Until
			: Spec.bWithin ? FMath::Min(Opens + Spec.Within, Record.Duration) : Record.Duration;
		// Nothing before scenario zero counts, whatever the window says.
		Opens = FMath::Max(Opens, 0.0);
		int32& Scan = NeverScans[Index];
		for (; Scan < Events.Num(); ++Scan)
		{
			const FEvent& Event = Events[Scan];
			const double Time = ScenarioTime(Event);
			if (Event.bEstablishingState || Time < Opens || Time > Closes || !Matches(NeverMatchers[Index], Event))
			{
				continue;
			}
			const int32 Count = ++NeverCounts[Index];
			if (Count <= Spec.AtMost)
			{
				continue;
			}
			if (!Out.bSet || Time < Out.Time)
			{
				Out.bSet = true;
				Out.Time = Time;
				Out.Section = TEXT("never");
				Out.Index = Index;
				Out.Who = Spec.Who;
				Out.Kind = Spec.Kind.ToString();
				Out.Match = Spec.Match;
				Out.Reason = FString::Printf(
					TEXT("appeared at t=%.2f: match %d where at most %d may appear in [%.2f, %.2f]: %s %s"), Time,
					Count, Spec.AtMost, Opens, Closes, Event.Name.IsEmpty() ? TEXT("-") : *Event.Name, *Event.Text);
			}
			++Scan;
			break;
		}
	}
}

void FElysiumArenaScenarioRunner::FindExpectMiss(double Now, FFailure& Out) const
{
	if (NextExpect >= Record.Expect.Num())
	{
		return;
	}
	const double Deadline = ExpectDeadline(NextExpect);
	if (Now <= Deadline && Now < Record.Duration)
	{
		return;
	}
	if (Out.bSet && Out.Time <= Deadline)
	{
		return;   // something earlier already failed the run
	}
	const FElysiumArenaMatch& Spec = Record.Expect[NextExpect];
	Out.bSet = true;
	Out.Time = Deadline;
	Out.Section = TEXT("expect");
	Out.Index = NextExpect;
	Out.Who = Spec.Who;
	Out.Kind = Spec.Kind.ToString();
	Out.Match = Spec.Match;
	Out.Reason = NextExpect > 0
		? FString::Printf(TEXT("unmet at its deadline (expect[%d] was met at t=%.2f)"), NextExpect - 1,
			MatchTimes[NextExpect - 1])
		: FString(TEXT("unmet at its deadline"));
}

void FElysiumArenaScenarioRunner::ReadDueProbes(double Now, FFailure& Out)
{
	for (int32 Index = 0; Index < Record.Probes.Num(); ++Index)
	{
		const FElysiumArenaProbeSpec& Probe = Record.Probes[Index];
		if (ProbeRead[Index] || Probe.bAtEnd || Now < Probe.Time)
		{
			continue;
		}
		ProbeRead[Index] = true;
		FString Read;
		FString Error;
		const bool bHeld = ReadProbe(Probe, Read, Error);
		if (bLogProgress)
		{
			UE_LOG(LogElysiumArenaScenario, Display, TEXT("%s: probe[%d] %s %s at t=%.2f read %s -- %s"),
				*Record.Name, Index, *Probe.Who, ElysiumArenaScenario::ProbeName(Probe.Probe), Now,
				Error.IsEmpty() ? *Read : *Error, bHeld ? TEXT("held") : TEXT("did not hold"));
		}
		if (!bHeld && (!Out.bSet || Probe.Time < Out.Time))
		{
			Out.bSet = true;
			Out.Time = Probe.Time;
			Out.Section = TEXT("probe");
			Out.Index = Index;
			Out.Who = Probe.Who;
			Out.Kind = FString::Printf(TEXT("probe:%s"), ElysiumArenaScenario::ProbeName(Probe.Probe));
			Out.Match = FString::Printf(TEXT("%s %s"), ElysiumArenaScenario::CompareName(Probe.Compare),
				*Probe.Value.Describe());
			Out.Reason = Error.IsEmpty() ? FString::Printf(TEXT("read %s"), *Read) : Error;
		}
	}
}

bool FElysiumArenaScenarioRunner::ReadProbe(const FElysiumArenaProbeSpec& Probe, FString& OutRead,
	FString& OutError) const
{
	FElysiumEntityWorld* World = LiveWorld();
	if (World == nullptr)
	{
		OutError = TEXT("no entity world to read");
		return false;
	}
	FElysiumEntity* Entity = ElysiumArenaRunnerDetail::FindEntity(*World, Probe.Who);
	if (Probe.Probe == EElysiumArenaProbe::Exists)
	{
		// The one probe a missing entity answers: a removed entity (`FElysiumEntity::Kill`, the port's
		// `UTIL_Remove 0x101cd940`) answers no name lookup from then on, reaped or not.
		FElysiumArenaValue Exists;
		Exists.Type = FElysiumArenaValue::EType::Bool;
		Exists.bBool = Entity != nullptr && !Entity->IsDead();
		OutRead = Exists.Describe();
		return ElysiumArenaRunnerDetail::Compare(Exists, Probe);
	}
	if (Entity == nullptr)
	{
		OutError = FString::Printf(TEXT("no entity named '%s'"), *Probe.Who);
		return false;
	}
	FElysiumNpc* Npc = Entity->AsNpc();
	const bool bNeedsNpc = Probe.Probe == EElysiumArenaProbe::Schedule || Probe.Probe == EElysiumArenaProbe::State
		|| Probe.Probe == EElysiumArenaProbe::Hint || Probe.Probe == EElysiumArenaProbe::HasCondition
		|| Probe.Probe == EElysiumArenaProbe::GroundSpeed;
	if (bNeedsNpc && Npc == nullptr)
	{
		OutError = FString::Printf(TEXT("'%s' is not an NPC"), *Probe.Who);
		return false;
	}

	FElysiumArenaValue Answer;
	switch (Probe.Probe)
	{
	case EElysiumArenaProbe::Alive:
		Answer.Type = FElysiumArenaValue::EType::Bool;
		Answer.bBool = !Entity->IsDead() && Entity->LifeState == ElysiumLifeState::Alive;
		break;
	case EElysiumArenaProbe::Health:
		Answer.Type = FElysiumArenaValue::EType::Number;
		Answer.Number = Entity->Health;
		break;
	case EElysiumArenaProbe::Schedule:
		Answer.Type = FElysiumArenaValue::EType::String;
		Answer.String = Npc->Schedule.IsRunning() ? FString(ElysiumScheduleName(Npc->Schedule.Current))
			: FString(TEXT("SCHED_NONE"));
		break;
	case EElysiumArenaProbe::State:
		Answer.Type = FElysiumArenaValue::EType::String;
		Answer.String = LexToString(Npc->GetMind().State());
		break;
	case EElysiumArenaProbe::Enemy:
	{
		Answer.Type = FElysiumArenaValue::EType::String;
		const FElysiumEntity* Enemy = Entity->GetEnemy();
		Answer.String = Enemy == nullptr ? FString(TEXT("none"))
			: Enemy->TargetName.IsEmpty() ? Enemy->Handle.ToString() : Enemy->TargetName;
		break;
	}
	case EElysiumArenaProbe::Hint:
	{
		Answer.Type = FElysiumArenaValue::EType::String;
		const int32 Node = Npc->BaseScheduleHost.HintNode;   // `m_pHintNode`, a hint entity index
		FElysiumNpcBase::FHintWords Words;
		Answer.String = Node == INDEX_NONE ? FString(TEXT("none"))
			: !Npc->HintWords(Node, Words) ? FString::Printf(TEXT("#%d (not a live hint)"), Node)
			: Words.Name.IsEmpty() ? FString::Printf(TEXT("#%d"), Node) : Words.Name;
		break;
	}
	case EElysiumArenaProbe::HasCondition:
		Answer.Type = FElysiumArenaValue::EType::Bool;
		Answer.bBool = Npc->Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(Probe.Condition));
		break;
	case EElysiumArenaProbe::OnGround:
	{
		const USkeletalMeshComponent* Skeletal = Entity->GetSkeletalBody();
		const AElysiumNpcBody* Motor = Skeletal != nullptr
			? Cast<AElysiumNpcBody>(Skeletal->GetAttachParentActor()) : nullptr;
		FElysiumNpcFloorFacts Floor;
		if (Motor == nullptr || !Motor->SampleFloor(Floor))
		{
			OutError = FString::Printf(TEXT("'%s' has no motor reporting a floor"), *Probe.Who);
			return false;
		}
		Answer.Type = FElysiumArenaValue::EType::Bool;
		Answer.bBool = Floor.bOnGround;
		break;
	}
	case EElysiumArenaProbe::Speed2d:
	case EElysiumArenaProbe::MoveYaw:
	{
		// H18. The BODY's own: the motor's velocity and the sample it publishes to its driver (the
		// record `elysium_entity_get`'s `locomotion` prints), never the kernel's words.
		const USkeletalMeshComponent* Skeletal = Entity->GetSkeletalBody();
		const AElysiumNpcBody* Motor = Skeletal != nullptr
			? Cast<AElysiumNpcBody>(Skeletal->GetAttachParentActor()) : nullptr;
		if (Motor == nullptr)
		{
			OutError = FString::Printf(TEXT("'%s' has no motor to read a velocity from"), *Probe.Who);
			return false;
		}
		Answer.Type = FElysiumArenaValue::EType::Number;
		Answer.Number = Probe.Probe == EElysiumArenaProbe::Speed2d
			? Motor->GetVelocity().Size2D()
			: static_cast<double>(Motor->GetAnimSample().MoveYawVelocity);
		break;
	}
	case EElysiumArenaProbe::GroundSpeed:
		// H18. The kernel's `m_flGroundSpeed +0x654` through the accessor its readers use; 0 until
		// V4a lane A2 writes the word.
		Answer.Type = FElysiumArenaValue::EType::Number;
		Answer.Number = Npc->GroundSpeedCm();
		break;
	case EElysiumArenaProbe::CorpseOnFloor:
	{
		// H20. The drawn mesh's pelvis BONE: the component's own location is below the floor once the
		// body lies, and the motor capsule stays at the death spot (`on_ground` is its answer).
		const FName PelvisBone(ElysiumArenaRunnerDetail::CorpsePelvisBone);
		const USkeletalMeshComponent* Skeletal = Entity->GetSkeletalBody();
		const FPelvisSample* Sample = nullptr;
		for (int32 Index = 0; Index < Record.Probes.Num(); ++Index)
		{
			if (&Record.Probes[Index] == &Probe)
			{
				Sample = &PelvisSamples[Index];
				break;
			}
		}
		const UWorld* EngineWorld = Host.GetWorld();
		if (Skeletal == nullptr || EngineWorld == nullptr || Skeletal->GetBoneIndex(PelvisBone) == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("'%s' has no drawn mesh with a `%s` bone"), *Probe.Who,
				ElysiumArenaRunnerDetail::CorpsePelvisBone);
			return false;
		}
		if (Sample == nullptr || !Sample->bSpeedKnown)
		{
			OutError = FString::Printf(TEXT("'%s': the pelvis was not read twice, so it has no speed"), *Probe.Who);
			return false;
		}
		const FVector Pelvis = Skeletal->GetBoneLocation(PelvisBone);
		// The floor is world geometry: an object-type query, so neither the ragdoll's own bodies nor
		// another character's capsule under the pelvis answers for it.
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(ElysiumArenaCorpseFloor), /*bTraceComplex=*/false);
		Params.AddIgnoredComponent(Skeletal);
		FHitResult Hit;
		if (!EngineWorld->LineTraceSingleByObjectType(Hit, Pelvis,
			Pelvis - FVector(0.0, 0.0, ElysiumArenaRunnerDetail::CorpseFloorReachCm), Objects, Params))
		{
			OutError = FString::Printf(TEXT("'%s': no floor within %.0f cm under the pelvis"), *Probe.Who,
				ElysiumArenaRunnerDetail::CorpseFloorReachCm);
			return false;
		}
		const double Height = Pelvis.Z - Hit.ImpactPoint.Z;
		Answer.Type = FElysiumArenaValue::EType::Bool;
		Answer.bBool = Height <= Probe.MaxHeightCm
			&& Sample->SpeedCmPerSecond < ElysiumArenaRunnerDetail::CorpseRestSpeedCmPerSecond;
		OutRead = FString::Printf(TEXT("%s (pelvis %.1f cm over the floor, bound %.1f; %.1f cm/s, at rest under %.1f)"),
			*Answer.Describe(), Height, Probe.MaxHeightCm, Sample->SpeedCmPerSecond,
			ElysiumArenaRunnerDetail::CorpseRestSpeedCmPerSecond);
		return ElysiumArenaRunnerDetail::Compare(Answer, Probe);
	}
 case EElysiumArenaProbe::SameTeam:
 case EElysiumArenaProbe::SwingRecordedHit:
 {
  const FElysiumCombatCharacter* ProbeCharacter = Entity->AsCombatCharacter();
  FElysiumEntity* ProbeVictim = ElysiumArenaRunnerDetail::FindEntity(*World, Probe.To.Name);
  if (!ProbeCharacter || !ProbeVictim) { OutError = TEXT("missing character/target for team/contact probe"); return false; }
  Answer.Type = FElysiumArenaValue::EType::Bool;
  if (Probe.Probe == EElysiumArenaProbe::SameTeam) Answer.bBool = ProbeCharacter->IsSameTeam(ProbeVictim->AsCombatCharacter());
  else
  {
   FElysiumEntity* ProbeWeaponEntity = ProbeCharacter->ActiveWeaponEntity();
   const FElysiumWeapon* ContactWeapon = ProbeWeaponEntity && ProbeWeaponEntity->AsItem() ? ProbeWeaponEntity->AsItem()->AsWeapon() : nullptr;
   Answer.bBool = ContactWeapon != nullptr && ContactWeapon->SwingHasRecordedHit(ProbeVictim->Handle);
  }
  break;
 }
 case EElysiumArenaProbe::TeamSymbol:
 case EElysiumArenaProbe::Wounds:
 case EElysiumArenaProbe::HealthCap:
 {
  const FElysiumCombatCharacter* ProbeCharacter = Entity->AsCombatCharacter();
  if (!ProbeCharacter) { OutError = TEXT("sheet/team probe requires a character"); return false; }
  Answer.Type = FElysiumArenaValue::EType::Number;
  Answer.Number = Probe.Probe == EElysiumArenaProbe::TeamSymbol ? ProbeCharacter->GetTeamSymbol()
   : ProbeCharacter->Sheet.GetCurrent(EElysiumTraitContainer::Attributes,
    Probe.Probe == EElysiumArenaProbe::Wounds ? ElysiumSlot::Health : ElysiumSlot::MaxHealth);
  break;
 }
 case EElysiumArenaProbe::SpawnFlags:
 case EElysiumArenaProbe::RenderMode:
  Answer.Type = FElysiumArenaValue::EType::Number;
  Answer.Number = Probe.Probe == EElysiumArenaProbe::SpawnFlags ? Entity->SpawnFlags : Entity->RenderMode;
  break;
 case EElysiumArenaProbe::OneHitKill:
 case EElysiumArenaProbe::NpcFlags1:
 case EElysiumArenaProbe::RenderAlpha:
 case EElysiumArenaProbe::Activity:
  if (!Npc) { OutError = TEXT("NPC word probe requires an NPC"); return false; }
  if (Probe.Probe == EElysiumArenaProbe::OneHitKill)
  { Answer.Type = FElysiumArenaValue::EType::Bool; Answer.bBool = (Npc->NpcFlags.RawWord1() & 0x40000000u) != 0; }
  else
  { Answer.Type = FElysiumArenaValue::EType::Number;
    Answer.Number = Probe.Probe == EElysiumArenaProbe::NpcFlags1 ? Npc->NpcFlags.RawWord1()
     : Probe.Probe == EElysiumArenaProbe::RenderAlpha ? Npc->RenderAlphaByte : Npc->ActivityNumber; }
  break;
	case EElysiumArenaProbe::DistanceTo:
	{
		FVector Target = FVector::ZeroVector;
		const FElysiumEntity* Other = Probe.To.bCoordinates ? nullptr
			: ElysiumArenaRunnerDetail::FindEntity(*World, Probe.To.Name);
		if (Other != nullptr)
		{
			Target = Other->Origin;
		}
		else
		{
			ElysiumArenaStage::FPlace Place;
			if (!ElysiumArenaStage::ResolveAt(Host, Probe.To, Place, OutError))
			{
				OutError = FString::Printf(TEXT("to: %s"), *OutError);
				return false;
			}
			Target = Place.FeetCm;
		}
		Answer.Type = FElysiumArenaValue::EType::Number;
		Answer.Number = FVector::Dist(Entity->Origin, Target);
		break;
	}
	case EElysiumArenaProbe::PlayerWeapon:
	case EElysiumArenaProbe::PlayerCrouched:
	case EElysiumArenaProbe::PlayerGrappling:
	{
		// Read through what the game's own readers read; the reader refuses any `who` but `player`.
		const FElysiumPlayer* Player = World->FindPlayer();
		if (Player == nullptr)
		{
			OutError = TEXT("no player entity to read");
			return false;
		}
		if (Probe.Probe == EElysiumArenaProbe::PlayerWeapon)
		{
			// `HasWeaponEquipped`'s read: the inventory's active item and its classname.
			const FElysiumItem* Active = Player->Inventory.Active(*Player);
			Answer.Type = FElysiumArenaValue::EType::String;
			Answer.String = Active != nullptr ? Active->ClassName() : FString(TEXT("none"));
		}
		else if (Probe.Probe == EElysiumArenaProbe::PlayerCrouched)
		{
			// `FL_DUCKING` as the stealth eligibility, the grapple admission and the footsteps read it.
			const IElysiumEmbodiment* Embodiment = World->Embodiment();
			if (Embodiment == nullptr)
			{
				OutError = TEXT("no embodiment to read the player's posture from");
				return false;
			}
			Answer.Type = FElysiumArenaValue::EType::Bool;
			Answer.bBool = Embodiment->IsPlayerDucking();
		}
		else
		{
			Answer.Type = FElysiumArenaValue::EType::Bool;
			Answer.bBool = Player->IsGrappling();
		}
		break;
	}
	default:
		OutError = TEXT("unhandled probe");
		return false;
	}
	OutRead = Answer.Describe();
	return ElysiumArenaRunnerDetail::Compare(Answer, Probe);
}

bool FElysiumArenaScenarioRunner::CurrentPlayerFeet(FVector& OutFeet) const
{
	const ElysiumGreenRoom::FDriveRefs Refs = ElysiumGreenRoom::ResolveDriveBody(Host.GetWorld());
	if (!Refs)
	{
		return false;
	}
	OutFeet = Refs.Pawn->GetActorLocation() - FVector(0.0f, 0.0f, Refs.Body->GetBodyHalfHeight());
	return true;
}

bool FElysiumArenaScenarioRunner::ApplyPlayerAtZero(FElysiumEntityWorld& World)
{
	if (Record.Player.bNoTarget)
	{
		// Refused rather than written: `FL_NOTARGET` (0x8000) has no producer in this runtime and its
		// readers are seams answering "targetable" (`ElysiumNpcBaseSenses10.cpp`), so a record that
		// asked for it would assert something nothing reads.
		Abort(TEXT("player.notarget is unsupported in this host: FL_NOTARGET (0x8000) has no reader yet ")
			TEXT("(the senses' notarget gates are seams)"));
		return false;
	}
	if (!Record.Player.bArmed && Record.Player.ArmedItem.IsEmpty())
	{
		return true;
	}
	const UWorld* EngineWorld = Host.GetWorld();
	UGameInstance* GameInstance = EngineWorld != nullptr ? EngineWorld->GetGameInstance() : nullptr;
	UElysiumSessionSubsystem* Session = GameInstance != nullptr
		? GameInstance->GetSubsystem<UElysiumSessionSubsystem>() : nullptr;
	if (!Record.Player.ArmedItem.IsEmpty())
	{
		FString Error;
		if (!ElysiumArenaCast::GivePlayerItem(World, Record.Player.ArmedItem, Error))
		{
			Abort(FString::Printf(TEXT("player.armed: %s"), *Error));
			return false;
		}
		return true;
	}
	const ElysiumArenaCast::FArmResult Armed = ElysiumArenaCast::ArmPlayerWithArsenal(World, Session);
	if (Armed.Melee + Armed.Firearms + Armed.Thrown == 0)
	{
		Abort(FString::Printf(TEXT("player.armed: nothing was granted (%s)"), *Armed.FirstError));
		return false;
	}
	return true;
}

bool FElysiumArenaScenarioRunner::FireDueActions(double Now, FElysiumEntityWorld& World)
{
	for (int32 Index = 0; Index < Record.Script.Num(); ++Index)
	{
		if (ActionFired[Index])
		{
			continue;
		}
		const FElysiumArenaAction& Action = Record.Script[Index];
		bool bDue = false;
		if (Action.bAtTime)
		{
			bDue = Now >= Action.Time;
		}
		else
		{
			const int32 Label = Record.Expect.IndexOfByPredicate([&Action](const FElysiumArenaMatch& Match)
				{ return Match.Label.Equals(Action.After, ESearchCase::CaseSensitive); });
			bDue = Label != INDEX_NONE && MatchTimes[Label] >= 0.0 && Now >= MatchTimes[Label] + Action.Delay;
		}
		if (!bDue)
		{
			continue;
		}
		ActionFired[Index] = true;
		// Traced before it runs, so a trace shows the action an `error` ended on as well.
		RecordAction(Action, World);
		FString Error;
		if (!RunAction(Index, World, Error))
		{
			if (Record.bExpectFail)
			{
				// A harness self-test states an action the host must refuse (`_selftest/dialog_choose_none`):
				// the refusal is the failure it expects, recorded as section `script` and then inverted.
				// Every other record that cannot act ends `error`.
				FFailure Failure;
				Failure.bSet = true;
				Failure.Time = Now;
				Failure.Section = TEXT("script");
				Failure.Index = Index;
				Failure.Kind = ElysiumArenaScenario::ActionName(Action.Do);
				Failure.Reason = Error;
				Finish(Now, Failure);
				return false;
			}
			Abort(FString::Printf(TEXT("script[%d] %s: %s"), Index, ElysiumArenaScenario::ActionName(Action.Do),
				*Error));
			return false;
		}
		if (bLogProgress)
		{
			UE_LOG(LogElysiumArenaScenario, Display, TEXT("%s: script[%d] %s at t=%.2f"), *Record.Name, Index,
				ElysiumArenaScenario::ActionName(Action.Do), Now);
		}
	}
	return true;
}

void FElysiumArenaScenarioRunner::RecordAction(const FElysiumArenaAction& Action, const FElysiumEntityWorld& World)
{
	auto PlaceText = [](const FElysiumArenaAt& At)
	{
		return At.bCoordinates ? At.Coordinates.ToCompactString() : At.Name;
	};
	FString Name;
	FString Text = ElysiumArenaScenario::ActionName(Action.Do);
	switch (Action.Do)
	{
	case EElysiumArenaAction::PlayerTeleport:
	case EElysiumArenaAction::PlayerWalk:
		Name = TEXT("player");
		Text += FString::Printf(TEXT(" %s"), *PlaceText(Action.At));
		break;
	case EElysiumArenaAction::Fire:
		Name = Action.Target;
		Text += FString::Printf(TEXT(" %s %s"), *Action.Input, *Action.Param.Describe());
		if (!Action.Activator.IsEmpty()) Text += FString::Printf(TEXT(" activator=%s"), *Action.Activator);
		break;
	case EElysiumArenaAction::SeedHealth:
	case EElysiumArenaAction::Kill:
		Name = Action.Target;
		break;
	case EElysiumArenaAction::DamagePacket:
		Name = Action.Target;
		Text += FString::Printf(TEXT(" %s from=%s"), *Action.Param.Describe(), *Action.Attacker);
		break;
	case EElysiumArenaAction::Console:
		Text += FString::Printf(TEXT(" %s"), *Action.Command);
		break;
	case EElysiumArenaAction::Spawn:
		Name = Action.Row.Name;
		Text += FString::Printf(TEXT(" %s %s"), *Action.Row.Classname, *PlaceText(Action.Row.At));
		break;
	case EElysiumArenaAction::PlayerCrouch:
		Name = TEXT("player");
		Text += Action.bOn ? TEXT(" on") : TEXT(" off");
		break;
	case EElysiumArenaAction::LightPin:
		Name = TEXT("player");
		Text += Action.bLightRelease ? FString(TEXT(" release")) : FString::Printf(TEXT(" %g"), Action.Light);
		break;
	case EElysiumArenaAction::DialogChoose:
		Name = TEXT("player");
		Text += Action.bDialogEnd ? FString(TEXT(" end")) : FString::Printf(TEXT(" %d"), Action.ChoiceIndex);
		break;
	default:
		break;
	}
	// Into the run's own event list, in time order: the runner ticks between frames, so every event a
	// tap has emitted so far is at or before the world's now.
	FEvent& Recorded = Events.AddDefaulted_GetRef();
	Recorded.Time = World.NowSeconds();
	Recorded.Name = Name;
	Recorded.Kind = ElysiumArenaRunnerDetail::ScriptKind();
	Recorded.Text = Text;
}

bool FElysiumArenaScenarioRunner::RunAction(int32 Index, FElysiumEntityWorld& World, FString& OutError)
{
	const FElysiumArenaAction& Action = Record.Script[Index];
	switch (Action.Do)
	{
	case EElysiumArenaAction::PlayerTeleport:
	{
		ElysiumArenaStage::FPlace Place;
		if (!ElysiumArenaStage::ResolveAt(Host, Action.At, Place, OutError))
		{
			return false;
		}
		FVector PlayerFeet = Place.FeetCm;
		CurrentPlayerFeet(PlayerFeet);
		float Yaw = Place.YawDeg;
		if (!ElysiumArenaStage::ResolveFace(Host, Action.Face, Place, PlayerFeet, Yaw, OutError))
		{
			return false;
		}
		bWalking = false;
		return ElysiumArenaStage::SeatPlayerAt(Host.GetWorld(), Place.FeetCm, Yaw, OutError);
	}
	case EElysiumArenaAction::PlayerWalk:
	{
		ElysiumArenaStage::FPlace Place;
		if (!ElysiumArenaStage::ResolveAt(Host, Action.At, Place, OutError))
		{
			return false;
		}
		const ElysiumGreenRoom::FDriveRefs Refs = ElysiumGreenRoom::ResolveDriveBody(Host.GetWorld());
		const AElysiumPlayerController* PC = Refs ? Cast<AElysiumPlayerController>(Refs.PC) : nullptr;
		if (PC == nullptr || PC->GetInputRouter() == nullptr)
		{
			OutError = TEXT("unsupported in this host: no player controller input router to replay a walk through");
			return false;
		}
		bWalking = true;
		WalkFeet = Place.FeetCm;
		return true;
	}
	case EElysiumArenaAction::SeedHealth:
	{
		FElysiumEntity* FixtureEntity = ElysiumArenaRunnerDetail::FindEntity(World, Action.Target);
		FElysiumCombatCharacter* FixtureCharacter = FixtureEntity ? FixtureEntity->AsCombatCharacter() : nullptr;
		if (!Host.bArena || !FixtureCharacter || !FixtureCharacter->IsAlive())
		{ OutError = TEXT("seed_health requires a live arena fixture character"); return false; }
		// Measurement setup for sheet +0x0f/+0x11 read by 0x1032ef60; never a retail input.
		FixtureCharacter->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::MaxHealth, static_cast<int32>(Action.Param.Number));
		FixtureCharacter->Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health, 0);
		FixtureCharacter->RecomputeSheet(); // effective cap, through the same formula as a damage commit
		return true;
	}
	case EElysiumArenaAction::DamagePacket:
	{
		FElysiumEntity* PacketVictimEntity = ElysiumArenaRunnerDetail::FindEntity(World, Action.Target);
		FElysiumNpc* PacketVictim = PacketVictimEntity ? PacketVictimEntity->AsNpc() : nullptr;
		FElysiumEntity* PacketAttacker = ElysiumArenaRunnerDetail::FindEntity(World, Action.Attacker);
		FElysiumCombatCharacter* PacketAttackerCharacter = PacketAttacker ? PacketAttacker->AsCombatCharacter() : nullptr;
		if (!Host.bArena || !PacketVictim || !PacketAttackerCharacter || !PacketAttackerCharacter->IsAlive())
		{ OutError = TEXT("damage_packet requires a victim and a live named attacker"); return false; }
		FElysiumNpcBase::FElysiumTakeDamageInfo FixturePacket;
		FixturePacket.Attacker = PacketAttacker->Handle;
		FixturePacket.Damage = static_cast<float>(Action.Param.Number);
		PacketVictim->OnTakeDamage(&FixturePacket); // 0x1032ef60 -> 0x10265ed0 -> 0x102bee60
		return true;
	}
	case EElysiumArenaAction::Fire:
	case EElysiumArenaAction::Kill:
	{
		// As `elysium_entity_fire` does it: one queued delivery per live entity of that name, addressed
		// `!self` with that entity as the caller -- the event queue a map's own wire goes through.
		TArray<FElysiumEntityHandle> Targets;
		World.ForEachNamed(Action.Target, [&Targets](FElysiumEntity& Entity)
		{
			if (!Entity.IsDead())
			{
				Targets.Add(Entity.Handle);
			}
		});
		if (Targets.IsEmpty())
		{
			OutError = FString::Printf(TEXT("no live entity named '%s'"), *Action.Target);
			return false;
		}
		const bool bKill = Action.Do == EElysiumArenaAction::Kill;
		const FName Input = bKill ? FName(TEXT("Kill")) : FName(*Action.Input);
		const FElysiumVariant Param = bKill ? FElysiumVariant::Void() : ElysiumArenaRunnerDetail::ToVariant(Action.Param);
		FElysiumEntityHandle InputActivator = FElysiumEntityHandle::Invalid();
		if (!Action.Activator.IsEmpty())
		{
			FElysiumEntity* ActivatingEntity = ElysiumArenaRunnerDetail::FindEntity(World, Action.Activator);
			if (ActivatingEntity == nullptr || ActivatingEntity->IsDead())
			{
				OutError = FString::Printf(TEXT("no live activator named '%s'"), *Action.Activator);
				return false;
			}
			InputActivator = ActivatingEntity->Handle; // 0x102c29c5: inputdata activator reaches damage packet
		}
		for (const FElysiumEntityHandle& Target : Targets)
		{
			World.EnqueueInput(TEXT("!self"), Input, Param, 0.0, InputActivator, Target);
		}
		return true;
	}
	case EElysiumArenaAction::Console:
	{
		UWorld* EngineWorld = Host.GetWorld();
		if (GEngine == nullptr || EngineWorld == nullptr)
		{
			OutError = TEXT("no engine world to run a console command in");
			return false;
		}
		GEngine->Exec(EngineWorld, *Action.Command);
		return true;
	}
	case EElysiumArenaAction::Spawn:
	{
		if (!Host.bArena)
		{
			OutError = TEXT("unsupported in this host: a map host spawns nothing");
			return false;
		}
		FVector PlayerFeet = FVector::ZeroVector;
		CurrentPlayerFeet(PlayerFeet);
		FElysiumEntityDef Def;
		if (!ElysiumArenaStage::BuildRow(Host, Action.Row, PlayerFeet, Def, OutError))
		{
			return false;
		}
		const FString Classname = Def.Classname;
		if (!World.SpawnRuntimeEntity(MoveTemp(Def)).IsSet())
		{
			OutError = FString::Printf(TEXT("'%s' is not a registered classname"), *Classname);
			return false;
		}
		return true;
	}
	case EElysiumArenaAction::PlayerCrouch:
	{
		// The door `player_walk` takes: the router's replay, never a console `+duck` string.
		const ElysiumGreenRoom::FDriveRefs Refs = ElysiumGreenRoom::ResolveDriveBody(Host.GetWorld());
		const AElysiumPlayerController* PC = Refs ? Cast<AElysiumPlayerController>(Refs.PC) : nullptr;
		if (PC == nullptr || PC->GetInputRouter() == nullptr)
		{
			OutError = TEXT("unsupported in this host: no player controller input router to replay a press through");
			return false;
		}
		CrouchStep = ECrouchStep::Press;
		bCrouchWanted = Action.bOn;
		return true;
	}
	case EElysiumArenaAction::LightPin:
	{
		IConsoleVariable* Knob = IConsoleManager::Get().FindConsoleVariable(ElysiumArenaRunnerDetail::StealthLightKnob);
		if (Knob == nullptr)
		{
			OutError = TEXT("unsupported in this host: `debug_stealth_light` is not registered (the override is ")
				TEXT("the seam at Substrate/ElysiumStealth.h:60)");
			return false;
		}
		Knob->Set(Action.bLightRelease ? ElysiumArenaRunnerDetail::StealthLightOff
			: static_cast<float>(Action.Light) * ElysiumArenaRunnerDetail::StealthLightScale, ECVF_SetByCode);
		bLightPinned = !Action.bLightRelease;
		return true;
	}
	case EElysiumArenaAction::DialogChoose:
	{
		// The conversation screen's own doors, never a teardown the player cannot reach: a row goes
		// through `PlayerDialogChoose` (what `UElysiumPresentationSubsystem::DialogueChoose` calls; retail
		// `CDialog::Pick` `0x100e4bd0`), and `end` -- retail's pick -1, which `Release`s
		// (`game_runtime.md` § Retail conversation chain, item 4) -- through `PlayerDialogAdvance`, the
		// Continue (`DialogueAdvance`), whose `AdvanceTerminal` closes the turn through
		// `FElysiumDlgConversation::Close`, the port's `CDialog::Release` (`0x100e5240`).
		const FElysiumDlgConversation* Open = World.GetOpenDialog();
		if (Open == nullptr)
		{
			OutError = TEXT("no open dialogue session to answer");
			return false;
		}
		const uint32 Serial = World.GetOpenDialogSerial();
		const uint32 Revision = Open->Revision();
		if (Action.bDialogEnd)
		{
			World.PlayerDialogAdvance();
		}
		else
		{
			if (!Open->VisibleChoices().IsValidIndex(Action.ChoiceIndex))
			{
				OutError = FString::Printf(TEXT("the open turn lists %d response row(s); there is no row %d"),
					Open->VisibleChoices().Num(), Action.ChoiceIndex);
				return false;
			}
			World.PlayerDialogChoose(Action.ChoiceIndex);
		}
		// `Open` may be gone now (the pick or the release closed the session): re-read it, and only
		// while the same session is still the open one.
		const FElysiumDlgConversation* Still = World.GetOpenDialogSerial() == Serial ? World.GetOpenDialog() : nullptr;
		if (Still != nullptr && Still->Revision() == Revision)
		{
			OutError = Action.bDialogEnd
				? FString(TEXT("the open turn refused the release (an automatic transition is pending)"))
				: FString::Printf(TEXT("the open turn refused row %d (disabled, or an automatic transition is pending)"),
					Action.ChoiceIndex);
			return false;
		}
		return true;
	}
	default:
		OutError = TEXT("unhandled action");
		return false;
	}
}

void FElysiumArenaScenarioRunner::TickPlayerInput()
{
	if (!bWalking && CrouchStep == ECrouchStep::None && !bDrivingInput)
	{
		return;
	}
	const ElysiumGreenRoom::FDriveRefs Refs = ElysiumGreenRoom::ResolveDriveBody(Host.GetWorld());
	const AElysiumPlayerController* PC = Refs ? Cast<AElysiumPlayerController>(Refs.PC) : nullptr;
	UElysiumInputRouter* Router = PC != nullptr ? PC->GetInputRouter() : nullptr;
	if (!Refs || Router == nullptr)
	{
		bWalking = false;
		CrouchStep = ECrouchStep::None;
		bDrivingInput = false;
		return;
	}

	FElysiumUserCmd Cmd;
	bool bSend = false;
	if (bWalking)
	{
		// `FElysiumGreenRoomRun::TickArenaWalkPlayer`, the lab's `gr_walk` player arm, over the same
		// door: one command replayed per frame through the player controller's input router, turning
		// and walking toward the destination read fresh from the live position.
		const FVector Feet = Refs.Pawn->GetActorLocation() - FVector(0.0f, 0.0f, Refs.Body->GetBodyHalfHeight());
		FVector Delta = WalkFeet - Feet;
		Delta.Z = 0.0f;
		if (Delta.Size() <= ElysiumArenaRunnerDetail::WalkAcceptanceCm)
		{
			bWalking = false;
		}
		else
		{
			const float TargetYaw = static_cast<float>(Delta.Rotation().Yaw);
			const float CurrentYaw = static_cast<float>(Refs.PC->GetControlRotation().Yaw);
			Cmd.LookDelta = FVector2D(FMath::FindDeltaAngleDegrees(CurrentYaw, TargetYaw), 0.0f);
			Cmd.Move = FVector2D(1.0f, 0.0f);
			Cmd.Buttons |= static_cast<uint64>(EElysiumButton::Forward);
			bSend = true;
		}
	}

	if (CrouchStep == ECrouchStep::Press)
	{
		// The duck is a toggle keyed on the press edge (`CGameMovement::Duck` `0x10126fd0`,
		// `UElysiumMovementComponent`'s duck): a press while standing starts the lowering, a press
		// while ducked starts the stand-up when there is headroom. Settled ducked or lowering heads
		// for the crouch; standing or rising heads for standing. One press, and only when the body is
		// not already heading where the record wants it: what that press then does (a stand-up with no
		// headroom is swallowed) is the mover's, as it is a player's.
		const bool bHeadingDucked = Refs.Move->IsDucked() != Refs.Move->IsDucking();
		if (bHeadingDucked != bCrouchWanted)
		{
			Cmd.Buttons |= static_cast<uint64>(EElysiumButton::Duck);
			bSend = true;
			CrouchStep = ECrouchStep::Release;
		}
		else
		{
			CrouchStep = ECrouchStep::None;
		}
	}
	else if (CrouchStep == ECrouchStep::Release)
	{
		// This frame's command (the walk's, or the router's own once the replay stops) carries no duck
		// bit: the key is up, so a later press is an edge again.
		CrouchStep = ECrouchStep::None;
	}

	if (bSend)
	{
		FElysiumUserCmdStream OneShot;
		OneShot.Record(Cmd);
		Router->StartReplay(OneShot);
		bDrivingInput = true;
	}
	else if (bDrivingInput)
	{
		bDrivingInput = false;
		if (Router->IsReplaying())
		{
			Router->StopReplay();
		}
	}
}

void FElysiumArenaScenarioRunner::TraceRemovals(FElysiumEntityWorld& World)
{
	// Mark every live entity with this tick, then sweep: one tracked on an earlier tick and not marked
	// now was removed in between (killed, and possibly reaped already). Debug output only: nothing
	// here writes a word the world reads. The runner ticks between frames, so the removal's time is
	// this frame's -- at or after every event a tap has emitted so far.
	++RemovalTick;
	for (const TUniquePtr<FElysiumEntity>& Entity : World.Entities())
	{
		if (!Entity.IsValid() || Entity->IsDead())
		{
			continue;
		}
		FTrackedEntity& Tracked = TrackedEntities.FindOrAdd(Entity->Handle);
		Tracked.Name = Entity->TargetName;
		Tracked.SeenTick = RemovalTick;
	}
	for (TMap<FElysiumEntityHandle, FTrackedEntity>::TIterator It(TrackedEntities); It; ++It)
	{
		if (It->Value.SeenTick == RemovalTick)
		{
			continue;
		}
		FEvent& Recorded = Events.AddDefaulted_GetRef();
		Recorded.Time = World.NowSeconds();
		Recorded.Entity = It->Key;
		Recorded.Name = It->Value.Name;
		Recorded.Kind = ElysiumArenaRunnerDetail::RemovedKind();
		Recorded.Text = It->Key.ToString();
		It.RemoveCurrent();
	}
}

void FElysiumArenaScenarioRunner::SampleCorpsePelvises(FElysiumEntityWorld& World)
{
	const FName PelvisBone(ElysiumArenaRunnerDetail::CorpsePelvisBone);
	for (int32 Index = 0; Index < Record.Probes.Num(); ++Index)
	{
		const FElysiumArenaProbeSpec& Probe = Record.Probes[Index];
		if (Probe.Probe != EElysiumArenaProbe::CorpseOnFloor)
		{
			continue;
		}
		const FElysiumEntity* Entity = ElysiumArenaRunnerDetail::FindEntity(World, Probe.Who);
		const USkeletalMeshComponent* Skeletal = Entity != nullptr ? Entity->GetSkeletalBody() : nullptr;
		if (Skeletal == nullptr || Skeletal->GetBoneIndex(PelvisBone) == INDEX_NONE)
		{
			continue;
		}
		FPelvisSample& Sample = PelvisSamples[Index];
		const FVector Location = Skeletal->GetBoneLocation(PelvisBone);
		const double Now = World.NowSeconds();
		if (Sample.bRead && Now > Sample.Time)
		{
			Sample.SpeedCmPerSecond = FVector::Dist(Location, Sample.LocationCm) / (Now - Sample.Time);
			Sample.bSpeedKnown = true;
		}
		// V4d numeric proof: drawn pelvis, never capsule/component origin or sleep state.
		// Reuse the probe's WorldStatic floor query; this log changes no acceptance or game state.
		if (Entity != nullptr && Entity->LifeState != ElysiumLifeState::Alive && Sample.bSpeedKnown
			&& FMath::FloorToInt(Now * 5.) != FMath::FloorToInt(Sample.Time * 5.))
		{
			const UWorld* SampleWorld = Host.GetWorld();
			FCollisionObjectQueryParams SampleObjects;
			SampleObjects.AddObjectTypesToQuery(ECC_WorldStatic);
			FCollisionQueryParams SampleParams(SCENE_QUERY_STAT(ElysiumArenaCorpseSeries), false);
			SampleParams.AddIgnoredComponent(Skeletal);
			FHitResult SampleHit;
			if (SampleWorld != nullptr && SampleWorld->LineTraceSingleByObjectType(SampleHit, Location,
				Location - FVector(0., 0., ElysiumArenaRunnerDetail::CorpseFloorReachCm), SampleObjects, SampleParams))
			{
				UE_LOG(LogElysiumArenaScenario, Log, TEXT("corpse sample: record=%s who=%s t=%.3f pelvis=(%.3f,%.3f,%.3f) floor=%.3f height=%.3f speed=%.3f sim=%d"),
					*Record.Name, *Probe.Who, Now, Location.X, Location.Y, Location.Z, SampleHit.ImpactPoint.Z,
					Location.Z - SampleHit.ImpactPoint.Z, Sample.SpeedCmPerSecond, Skeletal->IsSimulatingPhysics() ? 1 : 0);
			}
		}
		if (!Sample.bRead || Now > Sample.Time)
		{
			Sample.bRead = true;
			Sample.LocationCm = Location;
			Sample.Time = Now;
		}
	}
}

bool FElysiumArenaScenarioRunner::IsComplete(double Now) const
{
	if (NextExpect < Record.Expect.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < Record.Probes.Num(); ++Index)
	{
		if (!Record.Probes[Index].bAtEnd && !ProbeRead[Index])
		{
			return false;
		}
	}
	// A `never` holds for its window; with no `until`, that is the whole run, so it runs to duration.
	for (const FElysiumArenaMatch& Never : Record.Never)
	{
		if (Never.bWithin)
		{
			// A relative window: closed `within` seconds after its label's match plus `delay`.
			double WindowOpens = 0.0;
			if (!NeverWindowStart(Never, WindowOpens) || Now < WindowOpens + Never.Within)
			{
				return false;
			}
			continue;
		}
		if (!Never.bUntil || Now < Never.Until)
		{
			return false;
		}
	}
	return true;
}

bool FElysiumArenaScenarioRunner::Tick()
{
	if (bDone)
	{
		return false;
	}
	if (!bStarted)
	{
		return true;
	}
	FElysiumEntityWorld* World = LiveWorld();
	if (World == nullptr)
	{
		Abort(TEXT("the entity world the run was recording went away (rebuilt or unloaded) before the run ended"));
		return false;
	}
	if (!bZeroKnown)
	{
		// A stage whose activation failed outright ends the record now rather than at the bound: the
		// record is `error`, and the host stages the next record on a released stage.
		const AElysiumMapActor* FailedMap = Host.GetMap();
		if (FailedMap != nullptr && FailedMap->GetRuntimePhase() == EElysiumMapRuntimePhase::Failed)
		{
			Abort(FString::Printf(TEXT("the stage failed before it activated: %s (waiting on: %s)"),
				*FailedMap->GetRuntimeFailureReason(), *FailedMap->GetMissingRuntimePrerequisites()));
			return false;
		}
		if (FPlatformTime::Seconds() - StartWall > ElysiumArenaRunnerDetail::ActivationWaitSeconds)
		{
			const AElysiumMapActor* Map = Host.GetMap();
			Abort(FString::Printf(TEXT("the stage never activated within %.0f s (runtime %s, waiting on: %s)"),
				ElysiumArenaRunnerDetail::ActivationWaitSeconds,
				Map != nullptr ? ElysiumMapRuntimePhaseName(Map->GetRuntimePhase()) : TEXT("no map"),
				Map != nullptr ? *Map->GetMissingRuntimePrerequisites() : TEXT("-")));
			return false;
		}
		return true;
	}

	const double Now = World->NowSeconds() - ZeroWorld;
	LastNow = Now;
	if (!bZeroApplied)
	{
		bZeroApplied = true;
		if (!ApplyPlayerAtZero(*World))
		{
			return false;
		}
	}

	TraceRemovals(*World);
	SampleCorpsePelvises(*World);

	// What has happened so far, judged in time order: the earliest of a `never` that appeared, an
	// expectation whose deadline passed, and a timed probe that did not hold is the failure.
	MatchExpectations();
	FFailure Failure;
	FindNeverViolation(Failure);
	FindExpectMiss(Now, Failure);
	ReadDueProbes(Now, Failure);
	if (Failure.bSet)
	{
		Finish(Now, Failure);
		return false;
	}

	if (!FireDueActions(Now, *World))
	{
		return false;
	}
	TickPlayerInput();

	if (IsComplete(Now) || Now >= Record.Duration)
	{
		FinishWithEndProbes(Now);
		return false;
	}
	return true;
}

void FElysiumArenaScenarioRunner::FinishWithEndProbes(double Now)
{
	FFailure Failure;
	for (int32 Index = 0; Index < Record.Probes.Num(); ++Index)
	{
		const FElysiumArenaProbeSpec& Probe = Record.Probes[Index];
		if (!Probe.bAtEnd)
		{
			continue;
		}
		ProbeRead[Index] = true;
		FString Read;
		FString Error;
		const bool bHeld = ReadProbe(Probe, Read, Error);
		if (bLogProgress)
		{
			UE_LOG(LogElysiumArenaScenario, Display, TEXT("%s: probe[%d] %s %s at end read %s -- %s"),
				*Record.Name, Index, *Probe.Who, ElysiumArenaScenario::ProbeName(Probe.Probe),
				Error.IsEmpty() ? *Read : *Error, bHeld ? TEXT("held") : TEXT("did not hold"));
		}
		if (!bHeld && !Failure.bSet)
		{
			Failure.bSet = true;
			Failure.Time = Now;
			Failure.Section = TEXT("probe");
			Failure.Index = Index;
			Failure.Who = Probe.Who;
			Failure.Kind = FString::Printf(TEXT("probe:%s"), ElysiumArenaScenario::ProbeName(Probe.Probe));
			Failure.Match = FString::Printf(TEXT("%s %s"), ElysiumArenaScenario::CompareName(Probe.Compare),
				*Probe.Value.Describe());
			Failure.Reason = Error.IsEmpty() ? FString::Printf(TEXT("read %s at the end"), *Read) : Error;
		}
	}
	Finish(Now, Failure);
}

void FElysiumArenaScenarioRunner::Finish(double Now, const FFailure& Failure)
{
	if (bDone)
	{
		return;
	}
	const bool bRawPass = !Failure.bSet;
	if (Failure.bSet)
	{
		Result.bHasFirstUnmet = true;
		Result.UnmetSection = Failure.Section;
		Result.UnmetIndex = Failure.Index;
		Result.UnmetWho = Failure.Who;
		Result.UnmetKind = Failure.Kind;
		Result.UnmetMatch = Failure.Match;
		Result.UnmetDeadline = Failure.Time;
		Result.UnmetReason = Failure.Reason;
		if (bLogProgress && Failure.Section == TEXT("expect"))
		{
			UE_LOG(LogElysiumArenaScenario, Warning, TEXT("%s: expect[%d] missed at t=%.2f -- %s"), *Record.Name,
				Failure.Index, Failure.Time, *ElysiumArenaScenario::DescribeMatch(Record.Expect[Failure.Index]));
		}
	}
	// `expect_fail` inverts pass and fail (the harness's own self-tests); `known_red` then turns a
	// failure into `expected-fail` and a pass into `unexpected-pass`.
	const bool bPass = Record.bExpectFail ? !bRawPass : bRawPass;
	if (Record.bExpectFail && bRawPass)
	{
		Result.Note = TEXT("expect_fail: the record met everything it states, so the harness did not fail it");
	}
	Result.Result = Record.KnownRed.IsEmpty()
		? FString(bPass ? TEXT("pass") : TEXT("fail"))
		: FString(bPass ? TEXT("unexpected-pass") : TEXT("expected-fail"));
	Result.GameSeconds = Now;
	Result.WallSeconds = FPlatformTime::Seconds() - StartWall;
	Result.Events = Events.Num();
	bDone = true;
	Detach();
	UE_LOG(LogElysiumArenaScenario, Display, TEXT("%s"), *Result.Summary());
}

void FElysiumArenaScenarioRunner::Abort(const FString& Error)
{
	if (bDone)
	{
		return;
	}
	Result.Result = TEXT("error");
	Result.Error = Error;
	Result.GameSeconds = bZeroKnown ? LastNow : 0.0;
	Result.WallSeconds = bStarted ? FPlatformTime::Seconds() - StartWall : 0.0;
	Result.Events = Events.Num();
	bDone = true;
	Detach();
	UE_LOG(LogElysiumArenaScenario, Error, TEXT("%s"), *Result.Summary());
}

bool FElysiumArenaScenarioRunner::WriteTrace(const FString& Path, FString& OutError) const
{
	FString Text;
	Text.Reserve(64 + Events.Num() * 96);
	Text += TEXT("time\tname\tkind\ttext\n");
	for (const FEvent& Event : Events)
	{
		// Scenario seconds once zero is known; the raw world clock otherwise (a run that never activated).
		Text += FString::Printf(TEXT("%.3f\t%s\t%s\t%s\n"), bZeroKnown ? ScenarioTime(Event) : Event.Time,
			Event.Name.IsEmpty() ? TEXT("-") : *ElysiumArenaRunnerDetail::OneLine(Event.Name),
			*Event.Kind.ToString(), *ElysiumArenaRunnerDetail::OneLine(Event.Text));
	}
	if (!FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("could not write %s"), *Path);
		return false;
	}
	return true;
}

#endif // !UE_BUILD_SHIPPING
