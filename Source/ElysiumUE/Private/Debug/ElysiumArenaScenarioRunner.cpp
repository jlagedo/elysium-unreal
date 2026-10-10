#include "Debug/ElysiumArenaScenarioRunner.h"

#if !UE_BUILD_SHIPPING

#include "Debug/ElysiumArenaCast.h"          // `player.armed`: the cast harness's own grants
#include "Debug/ElysiumGreenRoomShared.h"    // ResolveDriveBody: the player's pawn, mover and controller
#include "ElysiumClassRegistry.h"            // `entity_field`: a class datamap row by its name
#include "ElysiumDlg.h"                      // `dialog_choose`: the open turn's response band
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumContentPaths.h"             // `KeyValues_LoadFile`: the corpus behind the fixture file system
#include "ElysiumKeyValues.h"                // `entity_call KeyValues_Lex` / `KeyValues_Parse` / the accessors
#include "ElysiumKeyValuesLoader.h"          // `entity_call KeyValues_LoadFile`
#include "ElysiumRetailSite.h"               // the named sink those calls report through
#include "Audio/ElysiumSoundScript.h"        // `entity_call SoundScript_New` / `SoundScript_SetChannel`
#include "Substrate/ElysiumBloodEffects.h"   // `entity_call Blood_Spawn`: `FUN_102699e0` on a utility target
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
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumAttackCoordinator.h"
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

	// The engine file system a `KeyValues_LoadFile` hands `0x102480f0` (and its text cache `0x102482f0`):
	// one file name bound to a `text` fixture's bytes for this call, every other name the deployed
	// corpus through the scheme resolver (`FElysiumContentPaths::SchemeFile`) -- so a record loads
	// the controlled texts it stages and the witness `sound/Schemes/SP_Tutorial_City.txt` itself.
	struct FKvFixtureFileSystem final : public ElysiumKeyValuesLoader::IKvFileSystem
	{
		FKvFixtureFileSystem()
			: Disk([](const FString& Name) { return FElysiumContentPaths::SchemeFile(Name); }) {}

		struct FOpenText
		{
			TArray<uint8> Bytes;
		};

		virtual ElysiumKeyValuesLoader::FKvFileHandle Open(const FString& Name, const ANSICHAR* Mode, int32 PathID) override
		{
			if (const FString* Text = Texts.Find(Name))
			{
				// The fixture's text as the file's bytes: each TCHAR narrowed to the byte it spells (the
				// inverse of the loader's widening).
				TUniquePtr<FOpenText> File = MakeUnique<FOpenText>();
				File->Bytes.Reserve(Text->Len());
				for (const TCHAR C : *Text)
				{
					File->Bytes.Add(static_cast<uint8>(C));
				}
				FOpenText* Raw = File.Get();
				OpenTexts.Add(Raw, MoveTemp(File));
				return reinterpret_cast<ElysiumKeyValuesLoader::FKvFileHandle>(Raw);
			}
			return Disk.Open(Name, Mode, PathID);
		}
		virtual void Close(ElysiumKeyValuesLoader::FKvFileHandle Handle) override
		{
			if (OpenTexts.Remove(reinterpret_cast<FOpenText*>(Handle)) == 0)
			{
				Disk.Close(Handle);
			}
		}
		virtual int32 Read(uint8* Out, int32 Count, ElysiumKeyValuesLoader::FKvFileHandle Handle) override
		{
			if (const TUniquePtr<FOpenText>* File = OpenTexts.Find(reinterpret_cast<FOpenText*>(Handle)))
			{
				const int32 N = FMath::Min(Count, (*File)->Bytes.Num());
				FMemory::Memcpy(Out, (*File)->Bytes.GetData(), N);
				return N;
			}
			return Disk.Read(Out, Count, Handle);
		}
		virtual int32 Size(ElysiumKeyValuesLoader::FKvFileHandle Handle) override
		{
			if (const TUniquePtr<FOpenText>* File = OpenTexts.Find(reinterpret_cast<FOpenText*>(Handle)))
			{
				return (*File)->Bytes.Num();
			}
			return Disk.Size(Handle);
		}

		TMap<FString, FString> Texts;
		TMap<FOpenText*, TUniquePtr<FOpenText>> OpenTexts;
		ElysiumKeyValuesLoader::FKvDiskFileSystem Disk;
	};

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
		// Stable index selection distinguishes a retained corpse from its same-name successor.
		// Maker enumeration 0x101cc9e0 does not filter life state.
		if (Who.StartsWith(TEXT("#")) && Who.Mid(1).IsNumeric())
		{
			const int32 StableIndex = FCString::Atoi(*Who.Mid(1));
			const auto& Entities = World.Entities();
			return Entities.IsValidIndex(StableIndex) ? Entities[StableIndex].Get() : nullptr;
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
		SegmentWorld = ZeroWorld; // scenario zero; engine 0x200f5bc4
		bZeroKnown = true;
		StageFixtures(*World);
		if (!Record.InitialWeaponState.IsEmpty() && !ApplyInitialWeaponState(*World)) return false;
	}
	else
	{
		ReadyMap = Map;
		ReadyHandle = Map->OnRuntimeReady().AddRaw(this, &FElysiumArenaScenarioRunner::OnStageActivated);
	}
	ElysiumArenaStage::ConfigureHost(Host); // both lab and GI hosts share adapter/transport, 0x1011a620
	bStarted = true;
	if (Host.bArena && Host.Transport && Host.Transport->StageInitialTime >= 0.0)
	{
		FEvent& InitEvent = Events.AddDefaulted_GetRef();
		StampEvent(InitEvent, Host.Transport->StageInitialTime);
		InitEvent.Kind = FName(TEXT("script"));
		InitEvent.Text = FString::Printf(TEXT("stage_pre_init npc_draw=%d"), Host.Transport->StageInitialDraw);
		for (int32 ProbeIndex = 0; ProbeIndex < Record.Probes.Num(); ++ProbeIndex)
		{
			const FElysiumArenaProbeSpec& Probe = Record.Probes[ProbeIndex];
			if (Probe.Fence != TEXT("pre_init")) continue;
			// Only the clock was sampled before this initial Load; other fields cannot be read late.
			if (Probe.Probe != EElysiumArenaProbe::Witness || Probe.Field != TEXT("clock") || !Probe.Checkpoint.IsEmpty())
			{ OutError = TEXT("initial stage pre_init supports its captured clock only"); Abort(OutError); return false; }
			FElysiumArenaValue InitialClock;
			InitialClock.Type = FElysiumArenaValue::EType::Number; InitialClock.Number = Host.Transport->StageInitialTime;
			ProbeRead[ProbeIndex] = true;
			if (!ElysiumArenaRunnerDetail::Compare(InitialClock, Probe))
			{ OutError = TEXT("initial pre-entity clock assertion failed"); Abort(OutError); return false; }
		}
	}
	UE_LOG(LogElysiumArenaScenario, Log, TEXT("%s: recording (%d expect, %d never, %d probe(s), %d action(s), ")
		TEXT("duration %.1f s)%s"), *Record.Name, Record.Expect.Num(), Record.Never.Num(), Record.Probes.Num(),
		Record.Script.Num(), Record.Duration, bZeroKnown ? TEXT("") : TEXT("; zero is the stage's Activate"));
	return true;
}

void FElysiumArenaScenarioRunner::RecordEvent(const FElysiumAiTraceEvent& Event)
{
	if (bDone) return; // retired operation observer cannot append after detach, 0x1011a620
	FEvent& Recorded = Events.AddDefaulted_GetRef();
	StampEvent(Recorded, Event.Time);
	Recorded.Entity = Event.Entity;
	if (FElysiumEntityWorld* EventWorld = LiveWorld()) Recorded.bPlayer = Event.Entity == EventWorld->PlayerHandle();
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
		SegmentWorld = ZeroWorld;
		// Activation's events belong to zero; later segments are already stamped (0x1011a620).
		for (FEvent& Recorded : Events) Recorded.Time = Recorded.WorldTime - ZeroWorld;
		bZeroKnown = true;
		StageFixtures(*World);
		if (!Record.InitialWeaponState.IsEmpty() && !ApplyInitialWeaponState(*World)) return;
		FEvent& ReadyEvent = Events.AddDefaulted_GetRef();
		StampEvent(ReadyEvent, World->NowSeconds());
		ReadyEvent.Kind = FName(TEXT("script"));
		ReadyEvent.Text = FString::Printf(TEXT("stage_ready npc_draw=%d"), ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).GetCurrentSeed()); // 0x200f5170 ready caveat
		for (int32 ProbeIndex = 0; ProbeIndex < Record.Probes.Num(); ++ProbeIndex)
		{
			const FElysiumArenaProbeSpec& Probe = Record.Probes[ProbeIndex];
			if (Probe.Fence != TEXT("ready") || ProbeRead[ProbeIndex]) continue;
			ProbeRead[ProbeIndex] = true;
			FString Read, Error;
			if (!ReadProbe(Probe, Read, Error))
			{
				FFailure Failure;
				Failure.bSet = true; Failure.Section = TEXT("probe"); Failure.Index = ProbeIndex;
				Failure.Reason = Error.IsEmpty() ? FString::Printf(TEXT("initial ready probe read %s"), *Read) : Error;
				Finish(0.0, Failure);
				return;
			}
		}

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
	// A `vsound_registry` fixture installed on the live world comes off with the run; the world it was
	// installed on is the one checked, so a world that went first is not touched.
	if (RegistryWorld != nullptr && RegistryWorld == LiveWorld() && StagedRegistry.IsValid()
		&& RegistryWorld->VSoundCharRegistry == StagedRegistry.Get())
	{
		RegistryWorld->VSoundCharRegistry = nullptr;
	}
	RegistryWorld = nullptr;
	if (Host.Transport) Host.Transport->Cancel(); // observer must not outlive runner, 0x1011a620
	bTransactionPending = false;
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
	// A `retail_site` matcher names the site tag: the event text opens `tag=<tag> ` (`EmitRetailSite`).
	if (!Spec.Site.IsEmpty()
		&& !Event.Text.StartsWith(FString::Printf(TEXT("tag=%s "), *Spec.Site), ESearchCase::CaseSensitive))
	{
		return false;
	}
	const bool bStableWho = Spec.Who.StartsWith(TEXT("#")) && Spec.Who.Mid(1).IsNumeric();
	if (bStableWho && (!Event.Entity.IsSet() || Event.Entity.Index != FCString::Atoi(*Spec.Who.Mid(1)))) return false;
	if (!Spec.Who.IsEmpty() && !bStableWho && !Event.Name.Equals(Spec.Who, ESearchCase::IgnoreCase))
	{
		// H21: `who: "player"` is the player ENTITY, whatever targetname it answers to (a tap names an
		// event by targetname and the player's is not `player`); the runner's own `script` events
		// carry that name already and matched above.
		// Judge identity when the event was emitted, not against a later restored epoch (0x101a2e40).
		if (!Spec.Who.Equals(TEXT("player"), ESearchCase::IgnoreCase) || !Event.bPlayer)
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
		if (ProbeRead[Index] || Probe.bAtEnd || !Probe.Fence.IsEmpty() || Now < Probe.Time)
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

namespace ElysiumArenaRunnerDetail
{
	// A `sound_folder` fixture's owner `T`: the retail-shaped answer to the L2 hook `FUN_101f4530`
	// (`0x101f4530`), `sum(counts[0..cat-1]) + idx` (`idx` for `cat <= 0`), as the walk read the body.
	// The hook stays L2's in the port (`FOwner::FlatIndex` is pure virtual); the harness supplies the
	// owner the two L0 rows are tested against, under the record's control.
	struct FRetailShapedFolderOwner final : public ElysiumSoundFolder::FOwner
	{
		virtual int32 FlatIndex(int32 Category, int32 Index) const override
		{
			int32 Sum = Index;
			for (int32 Cat = 0; Cat < Category; ++Cat)
			{
				Sum += Counts.IsValidIndex(Cat) ? Counts[Cat] : 0;
			}
			return Sum;
		}
	};

	// One node of a `sound_folder` fixture's `root`: `{"label", "key", "mask": [...], "children": [...],
	// "siblings": [...]}`. Siblings are owned by the parent's child list beside the node they follow.
	bool BuildFolderNode(const FJsonObject& Json, ElysiumSoundFolder::FOwner& Owner, ElysiumSoundFolder::FNode& Node,
		TArray<TUniquePtr<ElysiumSoundFolder::FNode>>* SiblingHome, FString& OutError)
	{
		Node.Owner = &Owner;
		Json.TryGetStringField(TEXT("label"), Node.Label);
		// `name` (`+0x10`): the folder name `FUN_101f39d0` compares a path component with; absent, NULL
		// (read as ""), which is how a category root with no name answers.
		Json.TryGetStringField(TEXT("name"), Node.Name);
		double Key = 0.0;
		if (!Json.TryGetNumberField(TEXT("key"), Key))
		{
			OutError = FString::Printf(TEXT("node '%s' needs a `key`"), *Node.Label);
			return false;
		}
		Node.Key = static_cast<int32>(Key);
		Node.Mask.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Mask = nullptr;
		if (Json.TryGetArrayField(TEXT("mask"), Mask))
		{
			for (const TSharedPtr<FJsonValue>& Byte : *Mask)
			{
				Node.Mask.Add(static_cast<uint8>(Byte.IsValid() && Byte->Type == EJson::Number ? Byte->AsNumber() : 0.0));
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
		if (Json.TryGetArrayField(TEXT("children"), Children))
		{
			for (const TSharedPtr<FJsonValue>& Child : *Children)
			{
				if (!Child.IsValid() || Child->Type != EJson::Object)
				{
					OutError = FString::Printf(TEXT("node '%s': a child must be an object"), *Node.Label);
					return false;
				}
				TUniquePtr<ElysiumSoundFolder::FNode> Built = MakeUnique<ElysiumSoundFolder::FNode>();
				ElysiumSoundFolder::FNode& Ref = *Built;
				Node.Children.Add(MoveTemp(Built));
				if (!BuildFolderNode(*Child->AsObject(), Owner, Ref, &Node.Children, OutError))
				{
					return false;
				}
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Siblings = nullptr;
		if (Json.TryGetArrayField(TEXT("siblings"), Siblings))
		{
			if (SiblingHome == nullptr)
			{
				OutError = TEXT("the root node takes no `siblings`");
				return false;
			}
			ElysiumSoundFolder::FNode* Previous = &Node;
			for (const TSharedPtr<FJsonValue>& Sibling : *Siblings)
			{
				if (!Sibling.IsValid() || Sibling->Type != EJson::Object)
				{
					OutError = FString::Printf(TEXT("node '%s': a sibling must be an object"), *Node.Label);
					return false;
				}
				TUniquePtr<ElysiumSoundFolder::FNode> Built = MakeUnique<ElysiumSoundFolder::FNode>();
				ElysiumSoundFolder::FNode& Ref = *Built;
				// The next child of the same parent (`+0x08` is the link `FUN_101f3b00` walks on a key
				// match), owned by that parent's child list right after the node it follows.
				SiblingHome->Add(MoveTemp(Built));
				if (!BuildFolderNode(*Sibling->AsObject(), Owner, Ref, nullptr, OutError))
				{
					return false;
				}
				Previous->Sibling = &Ref;
				Previous = &Ref;
			}
		}
		return true;
	}
}

void FElysiumArenaScenarioRunner::StageFixtures(FElysiumEntityWorld& World)
{
	for (const FElysiumArenaFixture& Fixture : Record.Fixtures)
	{
		StagedFixtures.Add(Fixture.Id, Fixture);
		if (Fixture.Kind == TEXT("sprite_model"))
		{
			// L0-r013: one row of the engine's model table (`VEngineServer014` slot 26's frame count),
			// staged into the world so a sprite the record creates reads it in `CSprite::Spawn`.
			World.StageSpriteModelFrames(Fixture.Model, Fixture.Frames);
		}
		FEvent& Staged = Events.AddDefaulted_GetRef();
		StampEvent(Staged, World.NowSeconds());
		Staged.Kind = ElysiumArenaRunnerDetail::ScriptKind();
		if (Fixture.Kind == TEXT("sound_folder"))
		{
			// The owner `T` of `Audio/ElysiumSoundFolderIndex.h`, retail-shaped: `categories` is the
			// category table's count (`*(T + 8) + 0x14`), `counts` the per-category array (`*(T + 0xc)`),
			// `root` the node at `T + 0x10`. A malformed configuration stages nothing and says why.
			TUniquePtr<ElysiumArenaRunnerDetail::FRetailShapedFolderOwner> Owner = MakeUnique<ElysiumArenaRunnerDetail::FRetailShapedFolderOwner>();
			FString Error;
			double Categories = 0.0;
			const TArray<TSharedPtr<FJsonValue>>* Counts = nullptr;
			const TSharedPtr<FJsonObject>* RootJson = nullptr;
			if (!Fixture.Config.IsValid() || !Fixture.Config->TryGetNumberField(TEXT("categories"), Categories)
				|| !Fixture.Config->TryGetArrayField(TEXT("counts"), Counts) || !Fixture.Config->TryGetObjectField(TEXT("root"), RootJson))
			{
				Error = TEXT("config needs `categories` (number), `counts` (array) and `root` (object)");
			}
			else
			{
				Owner->CategoryCount = static_cast<int32>(Categories);
				for (const TSharedPtr<FJsonValue>& Count : *Counts)
				{
					Owner->Counts.Add(static_cast<int32>(Count.IsValid() && Count->Type == EJson::Number ? Count->AsNumber() : 0.0));
				}
				ElysiumArenaRunnerDetail::BuildFolderNode(**RootJson, *Owner, Owner->Root, nullptr, Error);
			}
			if (Error.IsEmpty())
			{
				Staged.Text = FString::Printf(TEXT("fixture %s %s staged categories=%d counts=%d total=%d root=%s"), *Fixture.Id, *Fixture.Kind,
					Owner->CategoryCount, Owner->Counts.Num(), Owner->Total(), *ElysiumSoundFolder::MaskText(Owner->Root));
				StagedFolders.Add(Fixture.Id, MoveTemp(Owner));
			}
			else
			{
				Staged.Text = FString::Printf(TEXT("fixture %s %s failed: %s"), *Fixture.Id, *Fixture.Kind, *Error);
				StagedFixtures.Remove(Fixture.Id);
			}
			continue;
		}
		if (Fixture.Kind == TEXT("vsound_registry"))
		{
			// The SndScheme table object `reg` (`Substrate/ElysiumVSoundGroup.h`): `tables` names staged
			// `sound_folder` fixtures in category order (`reg+0x20[i]`), each already built above (a
			// fixture list is staged in record order, so the tables come first). Installed as the world's
			// `SndScheme_Char` -- `DAT_1073dc28`, the registry the base `PrecacheSoundTable` reads -- for
			// the run. One per record.
			FString Error;
			const TArray<TSharedPtr<FJsonValue>>* Tables = nullptr;
			TUniquePtr<FStagedVSoundRegistry> Registry = MakeUnique<FStagedVSoundRegistry>();
			if (!Fixture.Config.IsValid() || !Fixture.Config->TryGetArrayField(TEXT("tables"), Tables))
			{
				Error = TEXT("config needs `tables` (an array of sound_folder fixture ids)");
			}
			else if (StagedRegistry.IsValid())
			{
				Error = TEXT("a record stages one vsound_registry");
			}
			else
			{
				for (const TSharedPtr<FJsonValue>& Id : *Tables)
				{
					const FString Name = Id.IsValid() && Id->Type == EJson::String ? Id->AsString() : FString();
					const TUniquePtr<ElysiumSoundFolder::FOwner>* Owner = StagedFolders.Find(Name);
					if (Owner == nullptr || !Owner->IsValid())
					{
						Error = FString::Printf(TEXT("`tables` names '%s', which is not a staged sound_folder fixture"), *Name);
						break;
					}
					Registry->Tables.Add(Owner->Get());
				}
			}
			if (Error.IsEmpty())
			{
				Staged.Text = FString::Printf(TEXT("fixture %s %s staged tables=%d"), *Fixture.Id, *Fixture.Kind, Registry->Tables.Num());
				StagedRegistry = MoveTemp(Registry);
				World.VSoundCharRegistry = StagedRegistry.Get();
				RegistryWorld = &World;
			}
			else
			{
				Staged.Text = FString::Printf(TEXT("fixture %s %s failed: %s"), *Fixture.Id, *Fixture.Kind, *Error);
				StagedFixtures.Remove(Fixture.Id);
			}
			continue;
		}
		if (Fixture.Kind == TEXT("sound_script"))
		{
			// A controlled `CSoundEmitterSystemBase` table (`Audio/ElysiumSoundScriptTable.h`), built the
			// way `AddSoundsFromFile` 0x101b4240 ends: per entry a descriptor constructed by `FUN_101b30d0`,
			// the keys the entry carries set on it (`channel` through the setter `FUN_101b2490`; the
			// interval pairs stated as (base, span), their parsers not being this run's), each wave
			// interned in the wave-string table with its category (0 plain, 1 male, 2 female -- what
			// `FUN_101b3830` assigns), then the node inserted. `files` is the set of `sound/<wave>` paths
			// the fixture's `VFileSystem005` answers true for. `mark_missing` (default true) runs the
			// BaseInit pass `FUN_101b4740` quiet, as `0x101b2c60` does, so `rec+0x48` holds what retail's
			// would; false models the records before that pass (flag 0 whatever the files).
			TUniquePtr<FStagedSoundScript> Scripted = MakeUnique<FStagedSoundScript>();
			// The pass below reports through the sink, which appends to `Events`: `Staged` may move, so
			// the staged line is written through its index once the pass is done.
			const int32 StagedIndex = Events.Num() - 1;
			FString Error;
			const TArray<TSharedPtr<FJsonValue>>* Sounds = nullptr;
			if (!Fixture.Config.IsValid() || !Fixture.Config->TryGetArrayField(TEXT("sounds"), Sounds))
			{
				Error = TEXT("config needs `sounds` (an array of sound entries)");
			}
			else
			{
				const TArray<TSharedPtr<FJsonValue>>* Files = nullptr;
				if (Fixture.Config->TryGetArrayField(TEXT("files"), Files))
				{
					for (const TSharedPtr<FJsonValue>& File : *Files)
					{
						if (File.IsValid() && File->Type == EJson::String)
						{
							Scripted->Files.Add(ElysiumSoundScript::FExactKey(FString(TEXT("sound/")) + File->AsString()));
						}
					}
				}
				for (const TSharedPtr<FJsonValue>& Sound : *Sounds)
				{
					const TSharedPtr<FJsonObject>* Entry = nullptr;
					FString Name;
					if (!Sound.IsValid() || !Sound->TryGetObject(Entry) || !(*Entry)->TryGetStringField(TEXT("name"), Name))
					{
						Error = TEXT("each of `sounds` is an object with a `name`");
						break;
					}
					ElysiumSoundScript::FParams Params;
					ElysiumSoundScript::Construct(Params, nullptr);
					if (const TSharedPtr<FJsonValue> Channel = (*Entry)->TryGetField(TEXT("channel")); Channel.IsValid())
					{
						const FString ChannelText = Channel->Type == EJson::Number
							? FString::FromInt(static_cast<int32>(Channel->AsNumber())) : Channel->AsString();
						ElysiumSoundScript::SetChannel(Params, *ChannelText, nullptr);
					}
					auto ReadPair = [&Entry, &Error](const TCHAR* Key, ElysiumSoundScript::FInterval& Interval)
					{
						const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
						if (!(*Entry)->TryGetArrayField(Key, Pair))
						{
							return;
						}
						if (Pair->Num() != 2 || !(*Pair)[0].IsValid() || !(*Pair)[1].IsValid()
							|| (*Pair)[0]->Type != EJson::Number || (*Pair)[1]->Type != EJson::Number)
						{
							Error = FString::Printf(TEXT("`%s` is [base, span]"), Key);
							return;
						}
						Interval.Start = static_cast<float>((*Pair)[0]->AsNumber());
						Interval.Range = static_cast<float>((*Pair)[1]->AsNumber());
					};
					ReadPair(TEXT("volume"), Params.Volume);
					ReadPair(TEXT("pitch"), Params.Pitch);
					ReadPair(TEXT("soundlevel"), Params.SoundLevel);
					if (const TSharedPtr<FJsonValue> OwnerOnly = (*Entry)->TryGetField(TEXT("play_to_owner_only")); OwnerOnly.IsValid())
					{
						Params.bPlayToOwnerOnly = OwnerOnly->Type == EJson::Boolean ? (OwnerOnly->AsBool() ? 1 : 0)
							: static_cast<uint8>(OwnerOnly->AsNumber());
					}
					const TArray<TSharedPtr<FJsonValue>>* Waves = nullptr;
					if ((*Entry)->TryGetArrayField(TEXT("waves"), Waves))
					{
						for (const TSharedPtr<FJsonValue>& Wave : *Waves)
						{
							ElysiumSoundScript::FWave Built;
							FString WaveName;
							if (Wave.IsValid() && Wave->Type == EJson::String)
							{
								WaveName = Wave->AsString();
							}
							else if (const TSharedPtr<FJsonObject>* WaveObject = nullptr; Wave.IsValid() && Wave->TryGetObject(WaveObject)
								&& (*WaveObject)->TryGetStringField(TEXT("name"), WaveName))
							{
								double Category = 0.0;
								(*WaveObject)->TryGetNumberField(TEXT("category"), Category);
								Built.Gender = static_cast<uint32>(Category);
							}
							else
							{
								Error = FString::Printf(TEXT("sound '%s': a wave is a string or {\"name\", \"category\"}"), *Name);
								break;
							}
							Built.Symbol = Scripted->Table.InternWave(*WaveName);
							Params.Waves.Add(Built);
						}
					}
					if (!Error.IsEmpty())
					{
						break;
					}
					if (Scripted->Table.AddEntry(*Name, MoveTemp(Params)) == -1)
					{
						Error = FString::Printf(TEXT("sound '%s' is already in the table"), *Name);
						break;
					}
				}
			}
			int32 Missing = 0;
			if (Error.IsEmpty())
			{
				bool bMarkMissing = true;
				Fixture.Config->TryGetBoolField(TEXT("mark_missing"), bMarkMissing);
				if (bMarkMissing)
				{
					FElysiumNamedRetailSites Sites(World, Fixture.Id);
					Missing = Scripted->Table.MarkMissingWaves(*Scripted, ElysiumSoundScript::EMissingReport::Quiet, &Sites);
				}
				Events[StagedIndex].Text = FString::Printf(TEXT("fixture %s %s staged sounds=%d names=%d waves=%d files=%d missing=%d"), *Fixture.Id, *Fixture.Kind,
					Scripted->Table.Count(), Scripted->Table.NameCount(), Scripted->Table.WaveCount(), Scripted->Files.Num(), Missing);
				StagedSoundScripts.Add(Fixture.Id, MoveTemp(Scripted));
			}
			else
			{
				Events[StagedIndex].Text = FString::Printf(TEXT("fixture %s %s failed: %s"), *Fixture.Id, *Fixture.Kind, *Error);
				StagedFixtures.Remove(Fixture.Id);
			}
			continue;
		}
		Staged.Text = Fixture.Kind == TEXT("text")
			? FString::Printf(TEXT("fixture %s %s staged chars=%d"), *Fixture.Id, *Fixture.Kind, Fixture.Text.Len())
			: Fixture.Kind == TEXT("sprite_model")
			? FString::Printf(TEXT("fixture %s %s staged model=%s frames=%d"), *Fixture.Id, *Fixture.Kind, *Fixture.Model, Fixture.Frames)
			: FString::Printf(TEXT("fixture %s %s staged keys=%d"), *Fixture.Id, *Fixture.Kind, Fixture.Values.Num());
	}
}

bool FElysiumArenaScenarioRunner::ReadEntityField(const FElysiumArenaProbeSpec& Probe, FElysiumEntityWorld& World,
	FElysiumArenaValue& OutAnswer, FString& OutError) const
{
	OutAnswer = FElysiumArenaValue();
	// A handle word (`m_pParent`, `m_pMoveParent`, `m_pMoveChild`, `m_pMovePeer`, `m_hAimEnt`) alone
	// takes `to`: the probe then answers whether the handle resolves to the entity `to` names.
	const bool bHandleField = Probe.Field == TEXT("m_pParent") || Probe.Field == TEXT("m_pMoveParent")
		|| Probe.Field == TEXT("m_pMoveChild") || Probe.Field == TEXT("m_pMovePeer") || Probe.Field == TEXT("m_hAimEnt");
	if (Probe.To.bSet && !bHandleField)
	{
		OutError = FString::Printf(TEXT("field '%s' takes no `to`"), *Probe.Field);
		return false;
	}
	if (Probe.Who.StartsWith(TEXT("fixture:")))
	{
		// A `keyvalues` fixture: the field is the key, the answer the text a map row would spell.
		const FElysiumArenaFixture* Fixture = StagedFixtures.Find(Probe.Who.Mid(8));
		if (Fixture == nullptr)
		{
			OutError = FString::Printf(TEXT("fixture '%s' is not staged"), *Probe.Who.Mid(8));
			return false;
		}
		// A `sound_folder` fixture answers `mask` (`index`: a node's label; the bytes joined by commas)
		// and `count` (`index`: the category; a number) off its staged owner.
		if (Fixture->Kind == TEXT("sound_folder"))
		{
			const TUniquePtr<ElysiumSoundFolder::FOwner>* Owner = StagedFolders.Find(Fixture->Id);
			if (Owner == nullptr || !Owner->IsValid() || !Probe.Member.IsEmpty() || Probe.Index.IsEmpty())
			{
				OutError = FString::Printf(TEXT("fixture '%s': `mask` / `count` take `index` (a node label / a category) and no `member`"), *Fixture->Id);
				return false;
			}
			if (Probe.Field == TEXT("count"))
			{
				const int32 Category = FCString::Atoi(*Probe.Index);
				OutAnswer.Type = FElysiumArenaValue::EType::Number;
				OutAnswer.Number = (*Owner)->Counts.IsValidIndex(Category) ? (*Owner)->Counts[Category] : 0;
				return true;
			}
			if (Probe.Field != TEXT("mask"))
			{
				OutError = FString::Printf(TEXT("fixture '%s' has no field '%s' (mask, count)"), *Fixture->Id, *Probe.Field);
				return false;
			}
			TArray<const ElysiumSoundFolder::FNode*> Pending;
			Pending.Add(&(*Owner)->Root);
			while (!Pending.IsEmpty())
			{
				const ElysiumSoundFolder::FNode* Node = Pending.Pop();
				if (Node == nullptr) continue;
				if (Node->Label == Probe.Index)
				{
					OutAnswer.Type = FElysiumArenaValue::EType::String;
					OutAnswer.String = ElysiumSoundFolder::MaskText(*Node);
					return true;
				}
				for (const TUniquePtr<ElysiumSoundFolder::FNode>& Child : Node->Children)
				{
					Pending.Add(Child.Get());
				}
			}
			OutError = FString::Printf(TEXT("fixture '%s' has no node labelled '%s'"), *Fixture->Id, *Probe.Index);
			return false;
		}
		// A `sound_script` fixture answers `names` (the sound-name symbol table's count, `+0x12`: an
		// unknown name `FindSound` interned shows here) and `missing_flag` (`index`: a sound name; its
		// record's `+0x48` byte), both read without a retail call.
		if (Fixture->Kind == TEXT("sound_script"))
		{
			const TUniquePtr<FStagedSoundScript>* Scripted = StagedSoundScripts.Find(Fixture->Id);
			if (Scripted == nullptr || !Scripted->IsValid() || !Probe.Member.IsEmpty())
			{
				OutError = FString::Printf(TEXT("fixture '%s': `names` / `missing_flag` take no `member`"), *Fixture->Id);
				return false;
			}
			if (Probe.Field == TEXT("names") && Probe.Index.IsEmpty())
			{
				OutAnswer.Type = FElysiumArenaValue::EType::Number;
				OutAnswer.Number = (*Scripted)->Table.NameCount();
				return true;
			}
			if (Probe.Field == TEXT("missing_flag") && !Probe.Index.IsEmpty())
			{
				const ElysiumSoundScript::FParams* Rec = (*Scripted)->Table.Record((*Scripted)->Table.Lookup(*Probe.Index));
				if (Rec == nullptr)
				{
					OutError = FString::Printf(TEXT("fixture '%s' has no sound '%s'"), *Fixture->Id, *Probe.Index);
					return false;
				}
				OutAnswer.Type = FElysiumArenaValue::EType::Number;
				OutAnswer.Number = Rec->bHasMissingWave;
				return true;
			}
			OutError = FString::Printf(TEXT("fixture '%s' has no field '%s' (names; missing_flag with `index`)"), *Fixture->Id, *Probe.Field);
			return false;
		}
		// A `text` fixture answers its one field, `text`.
		const FString* Value = Fixture->Kind == TEXT("text")
			? (Probe.Field == TEXT("text") ? &Fixture->Text : nullptr)
			: Fixture->Values.Find(Probe.Field);
		if (Value == nullptr || !Probe.Index.IsEmpty() || !Probe.Member.IsEmpty())
		{
			OutError = Value == nullptr ? FString::Printf(TEXT("fixture '%s' has no key '%s'"), *Fixture->Id, *Probe.Field)
				: FString(TEXT("a fixture key takes no selector"));
			return false;
		}
		OutAnswer.Type = FElysiumArenaValue::EType::String;
		OutAnswer.String = *Value;
	}
	else
	{
		const FElysiumEntity* Entity = ElysiumArenaRunnerDetail::FindEntity(World, Probe.Who);
		if (Entity == nullptr)
		{
			OutError = FString::Printf(TEXT("no entity named '%s'"), *Probe.Who);
			return false;
		}
		// The retail field names (datamap / ledger), not the port's members, so a probe survives a rename.
		// A story adds an adapter here only for a retail value the current witnesses do not expose.
		// L0-r010's: the move hierarchy's handles, `m_iParentAttachment`, `m_iEFlags` (`member`: a bit
		// mask in hex, answered as a bool), `m_MoveType` / `m_MoveCollide`, `m_NetworkChangeState.m_bChanged`,
		// the local pose words `m_vecOrigin` / `m_angRotation` and the absolute ones `m_vecAbsOrigin` /
		// `m_angAbsRotation` (read through slots 217 / 219, which recompute under EFL 0x800 as retail's do).
		const bool bVector = Probe.Field == TEXT("m_vecOrigin") || Probe.Field == TEXT("m_angRotation")
			|| Probe.Field == TEXT("m_vecAbsOrigin") || Probe.Field == TEXT("m_angAbsRotation");
		const bool bFlagWord = Probe.Field == TEXT("m_iEFlags");
		if (!bVector && !bFlagWord && (!Probe.Index.IsEmpty() || !Probe.Member.IsEmpty()))
		{
			OutError = FString::Printf(TEXT("field '%s' takes no index or member"), *Probe.Field);
			return false;
		}
		if (bHandleField)
		{
			const FElysiumEntityHandle Word = Probe.Field == TEXT("m_pParent") ? Entity->ParentHandle
				: Probe.Field == TEXT("m_pMoveParent") ? Entity->MoveParent
				: Probe.Field == TEXT("m_pMoveChild") ? Entity->MoveChild
				: Probe.Field == TEXT("m_pMovePeer") ? Entity->MovePeer : Entity->AimEnt;
			if (Probe.To.bSet)
			{
				const FElysiumEntity* Other = Probe.To.bCoordinates ? nullptr : ElysiumArenaRunnerDetail::FindEntity(World, Probe.To.Name);
				if (Other == nullptr)
				{
					OutError = FString::Printf(TEXT("`to` names no live entity ('%s')"), *Probe.To.Name);
					return false;
				}
				OutAnswer.Type = FElysiumArenaValue::EType::Bool;
				OutAnswer.bBool = Word.IsSet() && World.Resolve(Word) == Other;
			}
			else
			{
				// The retail word: `#<index>` for a set handle, `-1` (0xFFFFFFFF) for the invalid one.
				OutAnswer.Type = FElysiumArenaValue::EType::String;
				OutAnswer.String = Word.IsSet() ? Word.ToString() : FString(TEXT("-1"));
			}
		}
		else if (Probe.Field == TEXT("m_iParentAttachment") || Probe.Field == TEXT("m_MoveType")
			|| Probe.Field == TEXT("m_MoveCollide"))
		{
			OutAnswer.Type = FElysiumArenaValue::EType::Number;
			OutAnswer.Number = Probe.Field == TEXT("m_iParentAttachment") ? Entity->ParentAttachment
				: Probe.Field == TEXT("m_MoveType") ? Entity->GetMoveType() : Entity->GetMoveCollide();
		}
		else if (bFlagWord)
		{
			if (Probe.Member.IsEmpty())
			{
				OutAnswer.Type = FElysiumArenaValue::EType::Number;
				OutAnswer.Number = static_cast<double>(Entity->EFlagsWord());
			}
			else
			{
				const uint32 Mask = FParse::HexNumber(*Probe.Member);
				if (Mask == 0)
				{
					OutError = TEXT("m_iEFlags `member` is a non-zero hex bit mask (`0x800`)");
					return false;
				}
				OutAnswer.Type = FElysiumArenaValue::EType::Bool;
				OutAnswer.bBool = (Entity->EFlagsWord() & Mask) == Mask;
			}
		}
		else if (Probe.Field == TEXT("m_NetworkChangeState.m_bChanged"))
		{
			OutAnswer.Type = FElysiumArenaValue::EType::Bool;
			OutAnswer.bBool = Entity->bNetworkChanged;
		}
		else if (Probe.Field == TEXT("m_touchStamp") || Probe.Field == TEXT("m_Solid") || Probe.Field == TEXT("m_usSolidFlags"))
		{
			// L0-r015's: `m_touchStamp` (+0x1ac, the untouch generation), and the collision property's
			// `m_Solid` (coll+0x40) / `m_usSolidFlags` (coll+0x44) datamap rows, read raw.
			OutAnswer.Type = FElysiumArenaValue::EType::Number;
			OutAnswer.Number = Probe.Field == TEXT("m_touchStamp") ? Entity->TouchStamp
				: Probe.Field == TEXT("m_Solid") ? Entity->RetailSolidType
				: static_cast<double>(Entity->RetailSolidFlags & 0xffffu);
		}
		else if (Probe.Field == TEXT("m_iName"))
		{
			OutAnswer.Type = FElysiumArenaValue::EType::String;
			OutAnswer.String = Entity->TargetName;
		}
		else if (Probe.Field == TEXT("m_iClassname"))
		{
			OutAnswer.Type = FElysiumArenaValue::EType::String;
			OutAnswer.String = Entity->Def != nullptr ? Entity->Def->Classname : FString();
		}
		else if (Probe.Field == TEXT("m_iHealth") || Probe.Field == TEXT("m_spawnflags")
			|| Probe.Field == TEXT("m_nRenderMode") || Probe.Field == TEXT("m_lifeState"))
		{
			OutAnswer.Type = FElysiumArenaValue::EType::Number;
			OutAnswer.Number = Probe.Field == TEXT("m_iHealth") ? Entity->Health
				: Probe.Field == TEXT("m_spawnflags") ? Entity->SpawnFlags
				: Probe.Field == TEXT("m_nRenderMode") ? Entity->RenderMode : Entity->LifeState;
		}
		else if (Probe.Field == TEXT("m_iVSoundGroup") || Probe.Field == TEXT("m_iVSoundGroupFemale")
			|| Probe.Field == TEXT("m_iVSoundTableIdx"))
		{
			// The ledger's names for `CBaseEntity` `+0xb4` / `+0xb8` / `+0xbc` (`layout.tsv:20-22`; no
			// datamap row), the words `PrecacheSoundTable` 0x1009d460 writes (L0.audio.voice-table-index).
			// Read raw: a probe reads the stored word, never the lazy getter that would re-run slot 71.
			OutAnswer.Type = FElysiumArenaValue::EType::Number;
			OutAnswer.Number = Probe.Field == TEXT("m_iVSoundGroup") ? Entity->VSoundGroup
				: Probe.Field == TEXT("m_iVSoundGroupFemale") ? Entity->VSoundGroupFemale : Entity->VSoundTableIdx;
		}
		else if (bVector)
		{
			if (Probe.Member != TEXT("x") && Probe.Member != TEXT("y") && Probe.Member != TEXT("z"))
			{
				OutError = FString::Printf(TEXT("%s needs `member`: x, y or z"), *Probe.Field);
				return false;
			}
			// `m_vecOrigin` / `m_angRotation` are the LOCAL words (slots 220 / 221); `m_vecAbsOrigin` /
			// `m_angAbsRotation` the absolute ones through slots 217 / 219 (a retail read: it recomputes
			// and clears EFL 0x800 exactly as any retail reader of the pose does). Port units: cm on the
			// Unreal axes for the origins, Source degrees (pitch, yaw, roll) for the angles.
			const FVector Word = Probe.Field == TEXT("m_vecOrigin") ? Entity->LocalOriginWord()
				: Probe.Field == TEXT("m_angRotation") ? Entity->LocalAnglesWord()
				: Probe.Field == TEXT("m_vecAbsOrigin") ? Entity->GetAbsOrigin() : Entity->GetAbsAngles();
			OutAnswer.Type = FElysiumArenaValue::EType::Number;
			OutAnswer.Number = Probe.Member == TEXT("x") ? Word.X : Probe.Member == TEXT("y") ? Word.Y : Word.Z;
		}
		else
		{
			// A row of the entity's class datamap, by the name the class registers it under: the
			// retail key (`radius`) or the retail member (`m_iSoundLevel`), as `ElysiumAddClassField`
			// names them. The same table the spawn pass, the save walk and Python read.
			const FElysiumFieldAccessor* Row = Entity->Class != nullptr
				? FElysiumClassRegistry::Get().FindField(*Entity->Class, FName(*Probe.Field)) : nullptr;
			if (Row == nullptr || !Row->Get)
			{
				OutError = FString::Printf(TEXT("no retail field adapter named '%s' (m_iName, m_iClassname, m_iHealth, ")
					TEXT("m_spawnflags, m_nRenderMode, m_lifeState, m_vecOrigin, m_angRotation, m_vecAbsOrigin, m_angAbsRotation, ")
					TEXT("m_pParent, m_pMoveParent, m_pMoveChild, m_pMovePeer, m_hAimEnt, m_iParentAttachment, m_iEFlags, ")
					TEXT("m_MoveType, m_MoveCollide, m_NetworkChangeState.m_bChanged, m_iVSoundGroup, m_iVSoundGroupFemale, ")
					TEXT("m_iVSoundTableIdx, m_touchStamp, m_Solid, m_usSolidFlags, or a datamap row the entity's class registers)"),
					*Probe.Field);
				return false;
			}
			const FElysiumVariant Word = Row->Get(*Entity);
			switch (Row->Type)
			{
			case EElysiumVariantType::Int:
			case EElysiumVariantType::Float:
				OutAnswer.Type = FElysiumArenaValue::EType::Number;
				OutAnswer.Number = Row->Type == EElysiumVariantType::Int ? static_cast<double>(Word.ToInt())
					: static_cast<double>(Word.ToFloat());
				break;
			case EElysiumVariantType::Bool:
				OutAnswer.Type = FElysiumArenaValue::EType::Bool;
				OutAnswer.bBool = Word.ToInt() != 0;
				break;
			case EElysiumVariantType::String:
				OutAnswer.Type = FElysiumArenaValue::EType::String;
				OutAnswer.String = Word.ToString();
				break;
			case EElysiumVariantType::Handle:
				// The retail word: `#<index>` for a set handle, `-1` (0xFFFFFFFF) for the invalid one, as
				// the base handle adapters above spell it.
				OutAnswer.Type = FElysiumArenaValue::EType::String;
				OutAnswer.String = Word.ToHandle().IsSet() ? Word.ToHandle().ToString() : FString(TEXT("-1"));
				break;
			default:
				OutError = FString::Printf(TEXT("field '%s' is a %s row; read its members"), *Probe.Field,
					Row->Type == EElysiumVariantType::Vector ? TEXT("vector") : TEXT("void"));
				return false;
			}
		}
	}
	// The field's own type must be the comparison's: a number is never compared with a string.
	if (OutAnswer.Type != Probe.Value.Type)
	{
		OutError = FString::Printf(TEXT("field '%s' answers %s; the comparison is %s"), *Probe.Field,
			*OutAnswer.Describe(), *Probe.Value.Describe());
		return false;
	}
	return true;
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
	if (Probe.Probe == EElysiumArenaProbe::Witness)
	{
		FElysiumArenaValue Word;
		if (!Probe.Checkpoint.IsEmpty())
		{
			const FCheckpoint* Saved = Checkpoints.Find(Probe.Checkpoint);
			const FString Key = Probe.Who + TEXT("\n") + Probe.Field;
			const FElysiumArenaValue* Before = Saved ? Saved->Captured.Find(Key) : nullptr;
			const FElysiumArenaValue* After = Saved ? Saved->Applied.Find(Key) : nullptr;
			if (!Before || !After || !Saved->bWritten || !Saved->bApplied)
			{ OutError = TEXT("checkpoint has no successful capture/write/apply fence"); return false; }
			OutRead = After->Describe();
			return ElysiumArenaScenario::WitnessEqual(ElysiumArenaScenario::RebaseWitness(Probe.Field, *Before, Saved->SaveBase, Saved->RestoreBase), *After, Probe.Tolerance);
		}
		if (!ReadWitness(Probe.Who, Probe.Field, Word, OutError)) return false;
		OutRead = Word.Describe();
		if (Probe.Compare == EElysiumArenaCompare::Equals)
			return ElysiumArenaScenario::WitnessEqual(Probe.Value, Word, Probe.Tolerance);
		return ElysiumArenaRunnerDetail::Compare(Word, Probe);
	}
	if (Probe.Probe == EElysiumArenaProbe::EntityField)
	{
		FElysiumArenaValue FieldAnswer;
		if (!ReadEntityField(Probe, *World, FieldAnswer, OutError)) return false;
		OutRead = FieldAnswer.Describe();
		return ElysiumArenaRunnerDetail::Compare(FieldAnswer, Probe);
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
	// Required save witnesses are admitted immediately before capture, after any lawful maker
	// spawn/unhide, not prematurely at scenario zero. Reload's explicit initial fixture stays at ready.
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

bool FElysiumArenaScenarioRunner::ApplyInitialWeaponState(FElysiumEntityWorld& World)
{
	if (bInitialWeaponApplied) return true;
	if (!Host.bArena) { Abort(TEXT("initial weapon fixture requires Green Room")); return false; }
	FString AdmissionError;
	if (!Host.ValidateWitnessAdmission || !Host.ValidateWitnessAdmission(World, Record, AdmissionError))
	{ Abort(AdmissionError.IsEmpty() ? TEXT("weapon fixture admission unavailable") : AdmissionError); return false; }
	// Validate every entry before any writes. The sink proves no owner NPCThink has run.
	for (const auto& Initial : Record.InitialWeaponState)
	{
		FElysiumEntity* Entity = ElysiumArenaRunnerDetail::FindEntity(World, Initial.Who);
		FElysiumNpc* Npc = Entity ? Entity->AsNpc() : nullptr;
		// The ordinary port equip is deferred to ResolveLoadout on the first think (retail spawn
		// 0x10273200/0x1032d380 equips before think). Stage that real prerequisite at ready,
		// before touching the explicit clip/reserve fixture; no invented item or condition.
		if (Npc && !Npc->bLoadoutResolved) Npc->StageResolveLoadout();
		FElysiumEntity* Held = Npc ? World.Resolve(Npc->Inventory.ActiveWeapon) : nullptr;
		FElysiumWeapon* Weapon = Held && Held->AsItem() ? Held->AsItem()->AsWeapon() : nullptr;
		const FElysiumItemDef* Data = Weapon ? Weapon->Data() : nullptr;
		if (!Npc || !Npc->Visual || !Weapon || Weapon->ClassName() != Initial.Weapon || !Data || Data->AmmoType.IsEmpty() || Data->MagazineSize <= 0 || Data->bReloadSingle
			|| Weapon->Owner != Npc->Handle || Events.ContainsByPredicate([&](const FEvent& Event) { return Event.Entity == Npc->Handle && Event.Kind == FName(TEXT("thinkfence")); }))
		{ Abort(TEXT("initial weapon fixture missing/mismatched NPC/item/ammo/bulk body, or after first think")); return false; }
	}
	for (const auto& Initial : Record.InitialWeaponState)
	{
		FElysiumNpc* Npc = ElysiumArenaRunnerDetail::FindEntity(World, Initial.Who)->AsNpc();
		FElysiumWeapon* Weapon = World.Resolve(Npc->Inventory.ActiveWeapon)->AsItem()->AsWeapon();
		const int32 EquippedClip = Weapon->MagazineCount;
		Weapon->MagazineCount = Initial.Magazine;
		Npc->Inventory.AddReserve(Weapon->Data()->AmmoType, Initial.Reserve - Npc->Inventory.Reserve(Weapon->Data()->AmmoType));
		Npc->FakeReloadCount = Initial.FakeReloadCount;
		World.EmitAiTrace(*Npc, FName(TEXT("reload")), FString::Printf(TEXT("initial item=%s clip=%d reserve=%d fake=%d post_equip_clip=%d handle=%s now=%.6f"),
			*Weapon->ClassName(), Weapon->MagazineCount, Npc->Inventory.Reserve(Weapon->Data()->AmmoType), Npc->FakeReloadCount, EquippedClip, *Weapon->Handle.ToString(), World.NowSeconds()));
	}
	bInitialWeaponApplied = true;
	return true;
}

bool FElysiumArenaScenarioRunner::ObserveFinalWeaponState(FElysiumEntityWorld& World)
{
	if (bFinalWeaponObserved) return true;
	for (const auto& Initial : Record.InitialWeaponState)
	{
		FElysiumEntity* Entity = ElysiumArenaRunnerDetail::FindEntity(World, Initial.Who);
		FElysiumNpc* Npc = Entity ? Entity->AsNpc() : nullptr;
		FElysiumEntity* Held = Npc ? World.Resolve(Npc->Inventory.ActiveWeapon) : nullptr;
		FElysiumWeapon* Weapon = Held && Held->AsItem() ? Held->AsItem()->AsWeapon() : nullptr;
		if (!Weapon || !Weapon->Data()) { Abort(TEXT("final reload observation lost real NPC/weapon")); return false; }
		World.EmitAiTrace(*Npc, FName(TEXT("reload")), FString::Printf(TEXT("final item=%s clip=%d reserve=%d flags=%d handle=%s now=%.6f"),
			*Weapon->ClassName(), Weapon->MagazineCount, Npc->Inventory.Reserve(Weapon->Data()->AmmoType),
			(Weapon->bInReload ? 1 : 0) | (Weapon->bInterruptReload ? 2 : 0) | (Weapon->bIsJammed ? 4 : 0), *Weapon->Handle.ToString(), World.NowSeconds()));
	}
	bFinalWeaponObserved = true;
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
		if (bDone) return false; // capture observer may have refused; 0x1027bc60
		// A transaction may replace World synchronously; never reuse this reference afterward.
		// 0x200975f0/0x1011a620: even inline Ready ends this action batch.
		if (Action.Do == EElysiumArenaAction::Save || Action.Do == EElysiumArenaAction::Load
			|| Action.Do == EElysiumArenaAction::FreshMap || Action.Do == EElysiumArenaAction::Travel) return true;
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
	case EElysiumArenaAction::Save:
	case EElysiumArenaAction::Load:
	case EElysiumArenaAction::FreshMap:
	case EElysiumArenaAction::Travel:
	case EElysiumArenaAction::RestoreCompare:
	case EElysiumArenaAction::NpcSingleRoundFinishReload:
	case EElysiumArenaAction::CorruptCheckpoint:
	case EElysiumArenaAction::InvalidMarker:
	case EElysiumArenaAction::RestoreBase:
	case EElysiumArenaAction::NoRagdollDeath:
	case EElysiumArenaAction::DamageMemory:
	case EElysiumArenaAction::ReserveSpot:
	case EElysiumArenaAction::StartNpcGroundGate:
		Name = Action.Target;
		Text += FString::Printf(TEXT(" slot=%s map=%s landmark=%s checkpoint=%s control=%s param=%s"),
			*Action.Slot, *Action.Map, *Action.Landmark, *Action.Checkpoint, *Action.Control, *Action.Param.Describe());
		break;
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
	case EElysiumArenaAction::EntityCall:
	{
		Name = Action.Target;
		TArray<FString> ArgText;
		for (const FElysiumArenaCallArg& Arg : Action.Args)
		{
			ArgText.Add(Arg.Fixture.IsEmpty() ? Arg.Value.Describe() : FString::Printf(TEXT("fixture:%s"), *Arg.Fixture));
		}
		Text += FString::Printf(TEXT(" %s(%s)"), *Action.Function, *FString::Join(ArgText, TEXT(", ")));
		break;
	}
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
	StampEvent(Recorded, World.NowSeconds());
	Recorded.Name = Name;
	Recorded.bPlayer = Name == TEXT("player");
	Recorded.Kind = ElysiumArenaRunnerDetail::ScriptKind();
	Recorded.Text = Text;
}

bool FElysiumArenaScenarioRunner::RunAction(int32 Index, FElysiumEntityWorld& World, FString& OutError)
{
	const FElysiumArenaAction& Action = Record.Script[Index];
	// Completion-aware harness transactions, never a generic console trace (0x20096010).
	if (Action.Do == EElysiumArenaAction::Save || Action.Do == EElysiumArenaAction::Load
		|| Action.Do == EElysiumArenaAction::FreshMap || Action.Do == EElysiumArenaAction::Travel)
	{
		if (!Host.Transport) { OutError = TEXT("no common save transport installed"); return false; }
		if (Action.Do == EElysiumArenaAction::Save && !Action.Fields.IsEmpty())
		{
			FElysiumArenaScenario Admission; Admission.Script.Add(Action);
			if (!Host.ValidateWitnessAdmission || !Host.ValidateWitnessAdmission(World, Admission, OutError))
			{
				if (OutError.Contains(TEXT("event-free body seek")) && !Record.KnownRed.IsEmpty())
				{
					FFailure Failure; Failure.bSet = true; Failure.Section = TEXT("admission"); Failure.Index = Index; Failure.Reason = OutError;
					Finish(LastNow, Failure); // measured named refusal, never a successful default
				}
				return false;
			}
		}
		if (Action.Do == EElysiumArenaAction::Load && !Action.Checkpoint.IsEmpty())
		{
			const FCheckpoint* Saved = Checkpoints.Find(Action.Checkpoint);
			if (!Saved || !Saved->bWritten || Saved->Slot != Action.Slot)
			{ OutError = TEXT("load checkpoint has no successful write in this slot"); return false; }
		}
		bTransactionPending = true;
		bTransactionCaptured = bTransactionApplied = false;
		TransactionAction = Index;
		TransactionId = 0;
		TransactionWall = FPlatformTime::Seconds();
		return Host.Transport->Begin(Host, Action,
			[this](const ElysiumArenaStage::FTransactionFence& Fence) { OnTransactionFence(Fence); }, OutError);
	}
	if (Action.Do == EElysiumArenaAction::RestoreCompare) return CompareCheckpoint(Action, OutError);
	if (Action.Do == EElysiumArenaAction::CorruptCheckpoint || Action.Do == EElysiumArenaAction::RestoreBase)
	{
		const FCheckpoint* Saved = Checkpoints.Find(Action.Checkpoint);
		if (!Host.bArena || !Host.Transport || !Saved || !Saved->bWritten)
		{ OutError = TEXT("fixture requires written Green Room checkpoint"); return false; }
		if (Action.Do == EElysiumArenaAction::CorruptCheckpoint)
			Host.Transport->Corruptions.Add(Saved->Slot, Action.Control); // normal header consumer follows, 0x1027bf50
		else { Host.Transport->bHasRestoreBase = true; Host.Transport->RestoreBase = Action.Param.Number; Host.Transport->RestoreBaseSlot = Saved->Slot; } // 0x20097d00 tagged base
		return true;
	}
	if (Action.Do >= EElysiumArenaAction::NpcSingleRoundFinishReload)
	{
		if (!Host.bArena || !Host.RunFixture) { OutError = TEXT("unavailable Green Room runtime fixture adapter"); return false; }
		return Host.RunFixture(World, Action, OutError); // setup then real consumer; 0x10255077/0x102da0d0/0x10273ad0
	}
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
		FElysiumEntity* PacketAttacker = Action.Attacker == TEXT("none") ? nullptr : ElysiumArenaRunnerDetail::FindEntity(World, Action.Attacker);
		FElysiumEntity* PacketInflictor = Action.Inflictor.IsEmpty() || Action.Inflictor == TEXT("none") ? nullptr : ElysiumArenaRunnerDetail::FindEntity(World, Action.Inflictor);
		if (!Action.Inflictor.IsEmpty() && Action.Inflictor != TEXT("none") && !PacketInflictor)
		{ OutError = TEXT("damage_packet inflictor is unavailable"); return false; }
		if (!PacketVictim || (Action.Attacker != TEXT("none") && (!PacketAttacker || !PacketAttacker->IsAlive())))
		{ OutError = TEXT("damage_packet requires a victim and a live named attacker"); return false; }
		// The current scalar packet has no +0x28 inflictor seam; integration supplies that
		// runtime consumer through the typed fixture adapter, never a fake CVDmg_t scalar.
		if (PacketInflictor)
		{
			if (!Host.RunFixture) { OutError = TEXT("unavailable scalar damage inflictor seam (+0x28, 0x10265ed0)"); return false; }
			return Host.RunFixture(World, Action, OutError);
		}
		FElysiumNpcBase::FElysiumTakeDamageInfo FixturePacket;
		FixturePacket.Attacker = PacketAttacker ? PacketAttacker->Handle : FElysiumEntityHandle::Invalid();

		FixturePacket.Damage = static_cast<float>(Action.Param.Number);
		PacketVictim->OnTakeDamage(&FixturePacket); // 0x1032ef60 -> 0x10265ed0 -> 0x102bee60
		return true;
	}
	case EElysiumArenaAction::EntityCall:
	{
		// Only what retail itself exposes to the world: an input, Use, a touch, a think, spawn and
		// activate, damage through the damage entry. Never an internal helper (`harness.md`). Each
		// story adds its entry points to `EntityCallAllowlist` together with their dispatch here.
		const TArray<FString>& Allowed = ElysiumArenaScenario::EntityCallAllowlist();
		if (!Allowed.Contains(Action.Function))
		{
			OutError = FString::Printf(TEXT("entity_call '%s' is not an allowlisted retail entry point (allowed: %s)"),
				*Action.Function, Allowed.IsEmpty() ? TEXT("none") : *FString::Join(Allowed, TEXT(", ")));
			return false;
		}
		for (const FElysiumArenaCallArg& Arg : Action.Args)
		{
			if (!Arg.Fixture.IsEmpty() && !StagedFixtures.Contains(Arg.Fixture))
			{
				OutError = FString::Printf(TEXT("fixture '%s' is not staged"), *Arg.Fixture);
				return false;
			}
		}
		if (Action.Function == TEXT("Use"))
		{
			// Slot 173 with its USE_TYPE (`CSprite::Use` 0x1042f030 reads it; `ShouldToggle` 0x100a98f0
			// judges it): `[useType, value?]`, both numbers. The activator and caller are the player when
			// the stage seats one (the +use press's provenance), else invalid, as a hand-fired input's.
			FElysiumEntity* UseTarget = ElysiumArenaRunnerDetail::FindEntity(World, Action.Target);
			if (UseTarget == nullptr || UseTarget->IsDead())
			{
				OutError = FString::Printf(TEXT("no live entity named '%s'"), *Action.Target);
				return false;
			}
			if (Action.Args.Num() < 1 || Action.Args.Num() > 2 || Action.Args[0].Value.Type != FElysiumArenaValue::EType::Number
				|| (Action.Args.Num() == 2 && Action.Args[1].Value.Type != FElysiumArenaValue::EType::Number))
			{
				OutError = TEXT("entity_call 'Use' takes [<useType 0..3>, <value>?], numbers");
				return false;
			}
			const int32 UseType = static_cast<int32>(Action.Args[0].Value.Number);
			const float UseValue = Action.Args.Num() == 2 ? static_cast<float>(Action.Args[1].Value.Number) : 0.0f;
			const FElysiumEntity* UsePlayer = World.FindPlayer();
			const FElysiumEntityHandle UseActivator = UsePlayer ? UsePlayer->Handle : FElysiumEntityHandle::Invalid();
			UseTarget->UseTyped(UseActivator, UseActivator, UseType, UseValue);
			FEvent& Done = Events.AddDefaulted_GetRef();
			StampEvent(Done, World.NowSeconds());
			Done.Kind = ElysiumArenaRunnerDetail::ScriptKind();
			Done.Name = Action.Target;
			Done.Text = FString::Printf(TEXT("entity_call Use done usetype=%d value=%g"), UseType, UseValue);
			return true;
		}
		if (Action.Function == TEXT("Blood_Spawn"))
		{
			// `FUN_102699e0` 0x102699e0 (`Substrate/ElysiumBloodEffects.h`): `[x, y, z, color, damage]`,
			// Source units; the colour as the `int` `BloodColor()` answers (-1 is DONT_BLEED).
			if (Action.Args.Num() != 5)
			{
				OutError = TEXT("entity_call 'Blood_Spawn' takes [x, y, z, color, damage], numbers");
				return false;
			}
			for (int32 I = 0; I < 5; ++I)
			{
				if (Action.Args[I].Value.Type != FElysiumArenaValue::EType::Number)
				{
					OutError = FString::Printf(TEXT("entity_call 'Blood_Spawn' argument %d is not a number"), I);
					return false;
				}
			}
			const FVector BloodPos(Action.Args[0].Value.Number, Action.Args[1].Value.Number, Action.Args[2].Value.Number);
			const uint32 BloodColor = static_cast<uint32>(static_cast<int32>(Action.Args[3].Value.Number));
			const float BloodDamage = static_cast<float>(Action.Args[4].Value.Number);
			FElysiumNamedRetailSites BloodSites(World, Action.Target);
			ElysiumBlood::SpawnBlood(BloodPos, BloodColor, BloodDamage, &World, &BloodSites);
			FEvent& Done = Events.AddDefaulted_GetRef();
			StampEvent(Done, World.NowSeconds());
			Done.Kind = ElysiumArenaRunnerDetail::ScriptKind();
			Done.Name = Action.Target;
			Done.Text = FString::Printf(TEXT("entity_call Blood_Spawn done color=%d damage=%g"), static_cast<int32>(BloodColor), BloodDamage);
			return true;
		}
		if (Action.Function == TEXT("Sweep_HullPrelude"))
		{
			// `0x10241620`'s hull path up to its separating-axis clip (`ElysiumRetailSweep::HullClipPrelude`):
			// 19 numbers, in the order the allowlist states. Sites `world_clear` / `segment_sphere` in the
			// target's column; the result line states the early-out verdict.
			double V[19];
			if (Action.Args.Num() != 19)
			{
				OutError = TEXT("entity_call 'Sweep_HullPrelude' takes 19 numbers: ray start, start offset, delta, extents; box centre, half size; tolerance");
				return false;
			}
			for (int32 I = 0; I < 19; ++I)
			{
				if (Action.Args[I].Value.Type != FElysiumArenaValue::EType::Number)
				{
					OutError = FString::Printf(TEXT("entity_call 'Sweep_HullPrelude' argument %d is not a number"), I);
					return false;
				}
				V[I] = Action.Args[I].Value.Number;
			}
			FElysiumNamedRetailSites Sites(World, Action.Target);
			FElysiumRetailTraceResult Trace;
			const bool bPass = ElysiumRetailSweep::HullClipPrelude(FVector(V[0], V[1], V[2]), FVector(V[3], V[4], V[5]),
				FVector(V[6], V[7], V[8]), FVector(V[9], V[10], V[11]), FVector(V[12], V[13], V[14]), FVector(V[15], V[16], V[17]),
				V[18], Trace, &Sites);
			FEvent& Done = Events.AddDefaulted_GetRef();
			StampEvent(Done, World.NowSeconds());
			Done.Kind = ElysiumArenaRunnerDetail::ScriptKind();
			Done.Name = Action.Target;
			Done.Text = FString::Printf(TEXT("entity_call Sweep_HullPrelude done prelude=%s fraction=%g end=%g,%g,%g"),
				bPass ? TEXT("pass") : TEXT("miss"), Trace.Fraction, Trace.EndPosCm.X, Trace.EndPosCm.Y, Trace.EndPosCm.Z);
			return true;
		}
		if (Action.Function == TEXT("KeyValues_Lex") || Action.Function == TEXT("KeyValues_Parse"))
		{
			// Argument 0: a `text` fixture, the buffer `0x101f2180` R3..R6 would have read from the file.
			const FElysiumArenaFixture* Source = Action.Args.Num() >= 1 && !Action.Args[0].Fixture.IsEmpty()
				? StagedFixtures.Find(Action.Args[0].Fixture) : nullptr;
			if (Source == nullptr || Source->Kind != TEXT("text"))
			{
				OutError = FString::Printf(TEXT("entity_call '%s' takes a `text` fixture as its first argument"), *Action.Function);
				return false;
			}
			FElysiumNamedRetailSites Sites(World, Action.Target);
			ElysiumKeyValues::FKvReader Reader(Source->Text, &Sites);
			// The call's result, written after its sites (the sink appends to `Events` while the reader
			// runs, so the event is built last and added once).
			auto RecordDone = [this, &World, &Action](const FString& Text)
			{
				FEvent& Done = Events.AddDefaulted_GetRef();
				StampEvent(Done, World.NowSeconds());
				Done.Kind = ElysiumArenaRunnerDetail::ScriptKind();
				Done.Name = Action.Target;
				Done.Text = Text;
			};
			if (Action.Function == TEXT("KeyValues_Lex"))
			{
				// The wrapper `0x101f2f30` called until the cursor it writes back is NULL -- how
				// `0x101f2360` and `0x101f2180` consume a buffer. `calls` counts the wrapper calls.
				int32 Calls = 0;
				while (Reader.Pos != INDEX_NONE)
				{
					uint8 Quoted = 0;
					ElysiumKeyValues::ReadToken(Reader, &Quoted);
					++Calls;
				}
				RecordDone(FString::Printf(TEXT("entity_call KeyValues_Lex done calls=%d"), Calls));
				return true;
			}
			// Argument 1 (optional): the target node's name -- `0x101f2e20` names its cache node after the
			// file and hands it to `0x101f2180` in ECX. Absent or empty: no target (ECX NULL).
			TSharedPtr<ElysiumKeyValues::FKvNode> Target;
			FString FileName = TEXT("(text)");
			if (Action.Args.Num() >= 2 && Action.Args[1].Value.Type == FElysiumArenaValue::EType::String
				&& !Action.Args[1].Value.String.IsEmpty())
			{
				FileName = Action.Args[1].Value.String;
				Target = MakeShared<ElysiumKeyValues::FKvNode>();
				ElysiumKeyValues::SetName(*Target, FileName);
			}
			TArray<TSharedPtr<ElysiumKeyValues::FKvNode>> Roots;
			ElysiumKeyValues::ParseRoots(Reader, FileName, Target, Roots);
			TArray<FString> RootNames;
			for (const TSharedPtr<ElysiumKeyValues::FKvNode>& Root : Roots)
			{
				RootNames.Add(FString::Printf(TEXT("%s(%d)"), *Root->Name, Root->Children.Num()));
			}
			RecordDone(FString::Printf(TEXT("entity_call KeyValues_Parse done roots=%d [%s] target=%s"), Roots.Num(),
				*FString::Join(RootNames, TEXT(" ")),
				Target.IsValid() ? *FString::Printf(TEXT("%s(%d)"), *Target->Name, Target->Children.Num()) : TEXT("null")));
			return true;
		}
		if (Action.Function == TEXT("KeyValues_LoadFile") || Action.Function == TEXT("KeyValues_GetInt")
			|| Action.Function == TEXT("KeyValues_GetString") || Action.Function == TEXT("KeyValues_SetString")
			|| Action.Function == TEXT("KeyValues_Chain"))
		{
			using ElysiumKeyValues::FKvNode;
			FElysiumNamedRetailSites Sites(World, Action.Target);
			auto RecordDone = [this, &World, &Action](const FString& Text)
			{
				FEvent& Done = Events.AddDefaulted_GetRef();
				StampEvent(Done, World.NowSeconds());
				Done.Kind = ElysiumArenaRunnerDetail::ScriptKind();
				Done.Name = Action.Target;
				Done.Text = Text;
			};
			auto StringArg = [&Action](int32 Index, FString& Out) -> bool
			{
				if (Action.Args.Num() <= Index || Action.Args[Index].Value.Type != FElysiumArenaValue::EType::String) return false;
				Out = Action.Args[Index].Value.String;
				return true;
			};
			auto NumberArg = [&Action](int32 Index, double& Out) -> bool
			{
				if (Action.Args.Num() <= Index) return false;
				const FElysiumArenaValue& V = Action.Args[Index].Value;
				if (V.Type == FElysiumArenaValue::EType::Number) { Out = V.Number; return true; }
				if (V.Type == FElysiumArenaValue::EType::Bool) { Out = V.bBool ? 1.0 : 0.0; return true; }
				return false;
			};
			// The caller's node (`0x102480f0`'s ECX): named by `target`, made on first use (`0x10247ba0`).
			TSharedPtr<FKvNode>& Tree = StagedTrees.FindOrAdd(Action.Target);
			if (!Tree.IsValid())
			{
				Tree = MakeShared<FKvNode>();
				Tree->Name = Action.Target;
			}
			if (Action.Function == TEXT("KeyValues_LoadFile"))
			{
				// `["<file name>", <cache>, {"fixture": <text id>}?]`: the name `0x102480f0` receives, its
				// fifth argument, and the bytes the engine file system answers for that name (absent: the
				// deployed corpus, or no such file).
				FString FileName;
				double Cache = 0.0;
				if (!StringArg(0, FileName) || !NumberArg(1, Cache))
				{
					OutError = TEXT("entity_call 'KeyValues_LoadFile' takes [\"<file name>\", <cache 0|1>, {\"fixture\": id}?]");
					return false;
				}
				ElysiumArenaRunnerDetail::FKvFixtureFileSystem Fs;
				if (Action.Args.Num() >= 3 && !Action.Args[2].Fixture.IsEmpty())
				{
					const FElysiumArenaFixture* Source = StagedFixtures.Find(Action.Args[2].Fixture);
					if (Source == nullptr || Source->Kind != TEXT("text"))
					{
						OutError = TEXT("entity_call 'KeyValues_LoadFile' takes a `text` fixture as its third argument");
						return false;
					}
					Fs.Texts.Add(FileName, Source->Text);
				}
				ElysiumKeyValuesLoader::FKvLoadArgs Args;
				Args.FileName = FileName;
				Args.Fs = &Fs;
				Args.PathID = 0;
				Args.Cache = Cache != 0.0 ? 1 : 0;
				Args.Sites = &Sites;
				TArray<TSharedPtr<FKvNode>> Roots;
				const bool bResult = ElysiumKeyValuesLoader::LoadFile(Tree, Args, Roots);
				TArray<FString> RootNames;
				for (const TSharedPtr<FKvNode>& Root : Roots)
				{
					RootNames.Add(FString::Printf(TEXT("%s(%d)"), *ElysiumKeyValues::Shown(Root->Name), Root->Children.Num()));
				}
				RecordDone(FString::Printf(TEXT("entity_call KeyValues_LoadFile done result=%d roots=%d [%s] target=%s(%d)"),
					bResult ? 1 : 0, Roots.Num(), *FString::Join(RootNames, TEXT(" ")), *ElysiumKeyValues::Shown(Tree->Name),
					Tree->Children.Num()));
				return true;
			}
			if (Action.Function == TEXT("KeyValues_Chain"))
			{
				// `["<other target>"]`: `target->+0x18 = other` (the vgui2 `ChainKeyValue`; no retail writer in
				// the corpus, walk L0-r004 open question 6 -- the harness builds the chain `0x10248900` searches).
				FString Other;
				if (!StringArg(0, Other) || !StagedTrees.Contains(Other) || !StagedTrees[Other].IsValid())
				{
					OutError = TEXT("entity_call 'KeyValues_Chain' takes [\"<other target>\"], a node an earlier call made");
					return false;
				}
				Tree->Chain = StagedTrees[Other];
				RecordDone(FString::Printf(TEXT("entity_call KeyValues_Chain done chain=%s"), *Other));
				return true;
			}
			FString Key;
			if (!StringArg(0, Key))
			{
				OutError = FString::Printf(TEXT("entity_call '%s' takes [\"<key>\", <default or value>]"), *Action.Function);
				return false;
			}
			if (Action.Function == TEXT("KeyValues_GetInt"))
			{
				double Default = 0.0;
				if (!NumberArg(1, Default))
				{
					OutError = TEXT("entity_call 'KeyValues_GetInt' takes [\"<key>\", <default>]");
					return false;
				}
				const int32 IntResult = ElysiumKeyValues::GetInt(*Tree, *Key, static_cast<int32>(Default), &Sites);
				RecordDone(FString::Printf(TEXT("entity_call KeyValues_GetInt done key=%s result=%d"), *Key, IntResult));
				return true;
			}
			FString Second;
			if (!StringArg(1, Second))
			{
				OutError = FString::Printf(TEXT("entity_call '%s' takes [\"<key>\", \"<default or value>\"]"), *Action.Function);
				return false;
			}
			if (Action.Function == TEXT("KeyValues_GetString"))
			{
				const FString TextResult = ElysiumKeyValues::GetString(*Tree, *Key, Second, &Sites);
				RecordDone(FString::Printf(TEXT("entity_call KeyValues_GetString done key=%s result=%s"), *Key,
					*ElysiumKeyValues::Shown(TextResult)));
				return true;
			}
			ElysiumKeyValues::SetString(*Tree, *Key, Second, &Sites);
			RecordDone(FString::Printf(TEXT("entity_call KeyValues_SetString done key=%s value=%s"), *Key,
				*ElysiumKeyValues::Shown(Second)));
			return true;
		}
		if (Action.Function == TEXT("Activate"))
		{
			// `ServerActivate` `0x1011aaf0`'s per-entity call: vslot 113 (`+0x1c4`) on one live entity,
			// as the level's activation pass (and a save restore's second barrier) runs it. No arguments.
			// Idempotence is the entity's own: the pass calls the slot on an already-activated entity too.
			FElysiumEntity* Target = ElysiumArenaRunnerDetail::FindEntity(World, Action.Target);
			if (Target == nullptr || Target->IsDead())
			{
				OutError = FString::Printf(TEXT("entity_call 'Activate': no live entity named '%s'"), *Action.Target);
				return false;
			}
			if (!Action.Args.IsEmpty())
			{
				OutError = TEXT("entity_call 'Activate' takes no arguments");
				return false;
			}
			Target->Activate();
			FEvent& Done = Events.AddDefaulted_GetRef();
			StampEvent(Done, World.NowSeconds());
			Done.Kind = ElysiumArenaRunnerDetail::ScriptKind();
			Done.Name = Action.Target;
			Done.Text = TEXT("entity_call Activate done");
			return true;
		}
		if (Action.Function == TEXT("Use"))
		{
			// Slot 173 (`+0x2b4`) `Use(activator, caller, useType, value)` on one live entity, with the
			// use type a record states: `[<useType 0..3>, <value>?]` (`ElysiumUseType`: 0 OFF, 1 ON,
			// 2 SET, 3 TOGGLE). The `Use` INPUT is `fire` (`CBaseEntity::InputUse` `0x100ac9f0`, always
			// 3 / 0); this is the slot itself, as the player's +use and the ambient's PlaySound /
			// StopSound (1 / 0) reach it. No entity stands behind the call: activator and caller are
			// invalid handles.
			FElysiumEntity* Target = ElysiumArenaRunnerDetail::FindEntity(World, Action.Target);
			if (Target == nullptr || Target->IsDead())
			{
				OutError = FString::Printf(TEXT("entity_call 'Use': no live entity named '%s'"), *Action.Target);
				return false;
			}
			if (Action.Args.IsEmpty() || Action.Args.Num() > 2 || Action.Args[0].Value.Type != FElysiumArenaValue::EType::Number
				|| (Action.Args.Num() == 2 && Action.Args[1].Value.Type != FElysiumArenaValue::EType::Number))
			{
				OutError = TEXT("entity_call 'Use' takes [<useType 0..3>, <value>?]");
				return false;
			}
			const int32 UseType = static_cast<int32>(Action.Args[0].Value.Number);
			const float UseValue = Action.Args.Num() == 2 ? static_cast<float>(Action.Args[1].Value.Number) : 0.f;
			Target->UseByType(FElysiumEntityHandle::Invalid(), FElysiumEntityHandle::Invalid(), UseType, UseValue);
			FEvent& Done = Events.AddDefaulted_GetRef();
			StampEvent(Done, World.NowSeconds());
			Done.Kind = ElysiumArenaRunnerDetail::ScriptKind();
			Done.Name = Action.Target;
			Done.Text = FString::Printf(TEXT("entity_call Use done use_type=%d value=%g"), UseType, UseValue);
			return true;
		}
		if (Action.Function == TEXT("GetEFlags") || Action.Function == TEXT("SetEFlags"))
		{
			// Slots 83 / 84 on one live entity, as `CServerNetworkProperty::vfunc2` / `vfunc3` forward the
			// engine's call (`walks/L0-r015.md`). `GetEFlags` takes no argument and reports the word;
			// `SetEFlags` takes the whole word as one number.
			FElysiumEntity* Target = ElysiumArenaRunnerDetail::FindEntity(World, Action.Target);
			if (Target == nullptr || Target->IsDead())
			{
				OutError = FString::Printf(TEXT("entity_call '%s': no live entity named '%s'"), *Action.Function, *Action.Target);
				return false;
			}
			const bool bSet = Action.Function == TEXT("SetEFlags");
			if (bSet ? (Action.Args.Num() != 1 || Action.Args[0].Value.Type != FElysiumArenaValue::EType::Number)
				: !Action.Args.IsEmpty())
			{
				OutError = bSet ? TEXT("entity_call 'SetEFlags' takes [<word>]") : TEXT("entity_call 'GetEFlags' takes no arguments");
				return false;
			}
			FString Text;
			if (bSet)
			{
				const uint32 Word = static_cast<uint32>(static_cast<int64>(Action.Args[0].Value.Number));
				Target->SetEFlags(static_cast<int32>(Word));
				Text = FString::Printf(TEXT("entity_call SetEFlags done m_iEFlags=0x%x"), Word);
			}
			else
			{
				const uint32 Word = static_cast<uint32>(Target->GetEFlags());
				Text = FString::Printf(TEXT("entity_call GetEFlags done m_iEFlags=0x%x"), Word);
			}
			FEvent& Done = Events.AddDefaulted_GetRef();
			StampEvent(Done, World.NowSeconds());
			Done.Kind = ElysiumArenaRunnerDetail::ScriptKind();
			Done.Name = Action.Target;
			Done.Text = Text;
			return true;
		}
		if (Action.Function == TEXT("MarkEntitiesAsTouching"))
		{
			// `CServerGameEnts::MarkEntitiesAsTouching` `0x1011be20` -> `PhysicsMarkEntitiesAsTouching`
			// `0x1003e2e0` -> `PhysicsMarkEntityAsTouched` `0x1003dc70` once in each direction: the pair
			// the engine's move found, handed to the server. The port's terminus is `RouteEntityTouch`
			// (the touched entity, the toucher, begin), which makes the pair's two stamped links and
			// fires the touched side's StartTouch (`entity_io.md` § The touch dispatch path).
			FElysiumEntity* Target = ElysiumArenaRunnerDetail::FindEntity(World, Action.Target);
			if (Target == nullptr || Target->IsDead())
			{
				OutError = FString::Printf(TEXT("entity_call 'MarkEntitiesAsTouching': no live entity named '%s'"), *Action.Target);
				return false;
			}
			if (Action.Args.Num() != 1 || Action.Args[0].Value.Type != FElysiumArenaValue::EType::String)
			{
				OutError = TEXT("entity_call 'MarkEntitiesAsTouching' takes [\"<toucher targetname>\"]");
				return false;
			}
			FElysiumEntity* Toucher = ElysiumArenaRunnerDetail::FindEntity(World, Action.Args[0].Value.String);
			if (Toucher == nullptr || Toucher->IsDead())
			{
				OutError = FString::Printf(TEXT("entity_call 'MarkEntitiesAsTouching': no live entity named '%s'"), *Action.Args[0].Value.String);
				return false;
			}
			World.RouteEntityTouch(Target->Handle, Toucher->Handle, /*bBegin*/ true);
			FEvent& Done = Events.AddDefaulted_GetRef();
			StampEvent(Done, World.NowSeconds());
			Done.Kind = ElysiumArenaRunnerDetail::ScriptKind();
			Done.Name = Action.Target;
			Done.Text = FString::Printf(TEXT("entity_call MarkEntitiesAsTouching done toucher=%s touching=%d"),
				*Action.Args[0].Value.String, Target->IsCurrentlyTouching() ? 1 : 0);
			return true;
		}
		if (Action.Function == TEXT("VSoundFolder_AddRange") || Action.Function == TEXT("VSoundFolder_Find"))
		{
			// The VSound folder index's two `T`-methods (`Audio/ElysiumSoundFolderIndex.h`): `AddRange`
			// `0x101f4330` -> `FUN_101f3ba0`, `Find` `0x101f42d0` -> `FUN_101f3b00`. Argument 0 names a
			// staged `sound_folder` fixture (the owner `T`); the rest are the retail integers.
			const FElysiumArenaFixture* Source = Action.Args.Num() >= 1 && !Action.Args[0].Fixture.IsEmpty()
				? StagedFixtures.Find(Action.Args[0].Fixture) : nullptr;
			TUniquePtr<ElysiumSoundFolder::FOwner>* Owner = Source != nullptr ? StagedFolders.Find(Source->Id) : nullptr;
			if (Source == nullptr || Source->Kind != TEXT("sound_folder") || Owner == nullptr || !Owner->IsValid())
			{
				OutError = FString::Printf(TEXT("entity_call '%s' takes a staged `sound_folder` fixture as its first argument"), *Action.Function);
				return false;
			}
			auto IntArg = [&Action](int32 Index, int32& Out) -> bool
			{
				if (Action.Args.Num() <= Index || Action.Args[Index].Value.Type != FElysiumArenaValue::EType::Number) return false;
				Out = static_cast<int32>(Action.Args[Index].Value.Number);
				return true;
			};
			FElysiumNamedRetailSites Sites(World, Action.Target);
			// The call's result is written after its sites (the sink appends to `Events` while the method
			// runs, so the event is built last and added once).
			auto RecordDone = [this, &World, &Action](const FString& Text)
			{
				FEvent& Done = Events.AddDefaulted_GetRef();
				StampEvent(Done, World.NowSeconds());
				Done.Kind = ElysiumArenaRunnerDetail::ScriptKind();
				Done.Name = Action.Target;
				Done.Text = Text;
			};
			if (Action.Function == TEXT("VSoundFolder_AddRange"))
			{
				int32 Category = 0, Hi = 0;
				if (!IntArg(1, Category) || !IntArg(2, Hi) || Action.Args.Num() != 3)
				{
					OutError = TEXT("entity_call 'VSoundFolder_AddRange' takes [{\"fixture\": id}, <category>, <hi>]");
					return false;
				}
				(*Owner)->AddRange(Category, Hi, &Sites);
				RecordDone(FString::Printf(TEXT("entity_call VSoundFolder_AddRange done cat=%d hi=%d count=%d total=%d"), Category, Hi,
					(*Owner)->Counts.IsValidIndex(Category) ? (*Owner)->Counts[Category] : 0, (*Owner)->Total()));
				return true;
			}
			int32 Key = 0, Category = 0, MemberIndex = 0;
			if (!IntArg(1, Key) || !IntArg(2, Category) || !IntArg(3, MemberIndex) || Action.Args.Num() != 4)
			{
				OutError = TEXT("entity_call 'VSoundFolder_Find' takes [{\"fixture\": id}, <key>, <category>, <index>]");
				return false;
			}
			const ElysiumSoundFolder::FNode* Found = (*Owner)->Find(Key, Category, MemberIndex, &Sites);
			RecordDone(FString::Printf(TEXT("entity_call VSoundFolder_Find done key=%d cat=%d idx=%d node=%s"), Key, Category, MemberIndex,
				Found != nullptr ? (Found->Label.IsEmpty() ? TEXT("?") : *Found->Label) : TEXT("null")));
			return true;
		}
		if (Action.Function == TEXT("SoundScript_New") || Action.Function == TEXT("SoundScript_SetChannel"))
		{
			// The sound-script descriptor as `AddSoundsFromFile` 0x101b4240 builds one per entry
			// (`Audio/ElysiumSoundScript.h`): `FUN_101b30d0` on a fresh record (0x101b4318), then for
			// `SoundScript_SetChannel` the `channel` key's setter `FUN_101b2490` with the one argument (a
			// string, or `null` for the parser's NULL arm) -- the key parser `FUN_101b3bb0`'s call at
			// 0x101b3bfe. The result line spells the record's words so a record can read them back.
			FElysiumNamedRetailSites Sites(World, Action.Target);
			ElysiumSoundScript::FParams Params;
			ElysiumSoundScript::Construct(Params, &Sites);
			FString Verb = TEXT("New");
			if (Action.Function == TEXT("SoundScript_SetChannel"))
			{
				if (Action.Args.Num() != 1 || !Action.Args[0].Fixture.IsEmpty()
					|| (Action.Args[0].Value.Type != FElysiumArenaValue::EType::String
						&& Action.Args[0].Value.Type != FElysiumArenaValue::EType::None))
				{
					OutError = TEXT("entity_call 'SoundScript_SetChannel' takes [\"<channel text>\" | null]");
					return false;
				}
				const bool bNull = Action.Args[0].Value.Type == FElysiumArenaValue::EType::None;
				ElysiumSoundScript::SetChannel(Params, bNull ? nullptr : *Action.Args[0].Value.String, &Sites);
				Verb = TEXT("SetChannel");
			}
			else if (!Action.Args.IsEmpty())
			{
				OutError = TEXT("entity_call 'SoundScript_New' takes no arguments");
				return false;
			}
			FEvent& Done = Events.AddDefaulted_GetRef();
			StampEvent(Done, World.NowSeconds());
			Done.Kind = ElysiumArenaRunnerDetail::ScriptKind();
			Done.Name = Action.Target;
			Done.Text = FString::Printf(TEXT("entity_call SoundScript_%s done channel=%d channel_text=%s volume=%.1f,%.1f volume_text=%s ")
				TEXT("pitch=%.0f,%.0f pitch_text=%s level=%.0f,%.0f level_text=%s owner_only=%d precache=%d flag48=%d waves=%d second=%d"),
				*Verb, Params.Channel, Params.ChannelText, Params.Volume.Start, Params.Volume.Range, Params.VolumeText,
				Params.Pitch.Start, Params.Pitch.Range, Params.PitchText, Params.SoundLevel.Start, Params.SoundLevel.Range,
				Params.SoundLevelText, Params.bPlayToOwnerOnly, Params.bPrecache, Params.bHasMissingWave, Params.Waves.Num(),
				Params.SecondList.Num());
			return true;
		}
		if (Action.Function == TEXT("SoundScript_GetParameters"))
		{
			// `GetParametersForSound` `FUN_101b33f0` `0x101b33f0` on a staged `sound_script` table
			// (`Audio/ElysiumSoundScriptTable.h`), as the EmitSound / StopSound workers call it: argument
			// 0 names the fixture (the instance `DAT_1072be18` and its `VFileSystem005`), argument 1 the
			// sound-script name (a string, or `null` for `EmitAmbientSound`'s NULL). `out` is the callers'
			// preset `CSoundParameters`; the draws go through the session's `VEngineRandom001`
			// (`FSessionRandom`), whose generator calls the result line counts.
			const FElysiumArenaFixture* Source = Action.Args.Num() >= 1 && !Action.Args[0].Fixture.IsEmpty()
				? StagedFixtures.Find(Action.Args[0].Fixture) : nullptr;
			TUniquePtr<FStagedSoundScript>* Scripted = Source != nullptr ? StagedSoundScripts.Find(Source->Id) : nullptr;
			const bool bNameOk = Action.Args.Num() == 2 && Action.Args[1].Fixture.IsEmpty()
				&& (Action.Args[1].Value.Type == FElysiumArenaValue::EType::String
					|| Action.Args[1].Value.Type == FElysiumArenaValue::EType::None);
			if (Source == nullptr || Source->Kind != TEXT("sound_script") || Scripted == nullptr || !Scripted->IsValid() || !bNameOk)
			{
				OutError = TEXT("entity_call 'SoundScript_GetParameters' takes [{\"fixture\": <sound_script id>}, \"<sound name>\" | null]");
				return false;
			}
			FElysiumNamedRetailSites Sites(World, Action.Target);
			ElysiumSoundScript::FSoundParameters Out;
			ElysiumSoundScript::FSessionRandom Random;
			const bool bNull = Action.Args[1].Value.Type == FElysiumArenaValue::EType::None;
			const bool bResult = (*Scripted)->Table.GetParametersForSound(bNull ? nullptr : *Action.Args[1].Value.String, Out, Random,
				**Scripted, &Sites);
			FEvent& Done = Events.AddDefaulted_GetRef();
			StampEvent(Done, World.NowSeconds());
			Done.Kind = ElysiumArenaRunnerDetail::ScriptKind();
			Done.Name = Action.Target;
			Done.Text = FString::Printf(TEXT("entity_call SoundScript_GetParameters done result=%d channel=%d volume=%.3f pitch=%d pitch_low=%d ")
				TEXT("pitch_high=%d level=%d owner_only=%d count=%d wave=%s draws=%d"),
				bResult ? 1 : 0, Out.Channel, Out.Volume, Out.Pitch, Out.PitchLow, Out.PitchHigh, Out.SoundLevel, Out.bPlayToOwnerOnly,
				Out.Count, *Out.SoundName, Random.GeneratorCalls());
			return true;
		}
		OutError = FString::Printf(TEXT("entity_call '%s' is allowlisted but has no dispatch"), *Action.Function);
		return false;
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
		StampEvent(Recorded, World.NowSeconds());
		Recorded.Entity = It->Key;
		Recorded.bPlayer = It->Key == World.PlayerHandle();
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
	if (bTransactionPending || ActionFired.Contains(false)) return false; // 0x200975f0: completion is a fence
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
	if (Host.Transport) Host.Transport->Poll(); // readiness fence can finish a replacement, 0x1011a620
	if (bDone) return false;
	if (bTransactionPending)
	{
		const FElysiumArenaAction& Pending = Record.Script[TransactionAction];
		if (FPlatformTime::Seconds() - TransactionWall > Pending.Timeout) FailTransaction(TEXT("transaction wall timeout"));
		if (Pending.Do == EElysiumArenaAction::Save && !LiveWorld())
		{ FailTransaction(TEXT("unexpected world loss during save")); return !bDone; }
		// No actions/probes/player replay in a no-world gap; no fabricated simulation elapsed.
		if (FElysiumEntityWorld* PendingWorld = LiveWorld())
			LastNow = SegmentElapsed + FMath::Max(0.0, PendingWorld->NowSeconds() - SegmentWorld);
		return !bDone;
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

	const double Now = SegmentElapsed + FMath::Max(0.0, World->NowSeconds() - SegmentWorld); // 0x200975f0 monotonic segments
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
	if (Now >= Record.Duration && !Record.InitialWeaponState.IsEmpty() && !ObserveFinalWeaponState(*World)) return false;

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
	if (bDone) return false;
	// FireDueActions may have retired World, even with inline completion (0x200975f0).
	if (bTransactionPending || LiveWorld() != World) return true;
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
	for (int32 ActionIndex = 0; ActionIndex < Record.Script.Num(); ++ActionIndex)
	{
		if (Record.Script[ActionIndex].Do >= EElysiumArenaAction::Save && !ActionFired[ActionIndex])
		{
			Failure.bSet = true; Failure.Time = Now; Failure.Section = TEXT("script"); Failure.Index = ActionIndex;
			Failure.Reason = TEXT("required transaction/fixture action never fired");
			break;
		}
	}
	for (int32 Index = 0; Index < Record.Probes.Num(); ++Index)
	{
		const FElysiumArenaProbeSpec& Probe = Record.Probes[Index];
		if (!Probe.Fence.IsEmpty() && !ProbeRead[Index])
		{
			Failure.bSet = true; Failure.Time = Now; Failure.Section = TEXT("probe"); Failure.Index = Index;
			Failure.Reason = TEXT("required transaction fence never occurred");
			break;
		}
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
	if (!bPass && Record.KnownRed.StartsWith(TEXT("0017/35:")) && !Failure.Reason.Contains(TEXT("event-free body seek")))
		Result.Result = TEXT("fail"); // named source filing cannot cover another missed stage or behaviour
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


// Harness coordinates and typed reads; engine 0x200975f0 restores a different clock epoch.
void FElysiumArenaScenarioRunner::StampEvent(FEvent& Event, double WorldTime) const
{
	Event.WorldTime = WorldTime;
	Event.Time = bZeroKnown ? SegmentElapsed + FMath::Max(0.0, WorldTime - SegmentWorld) : WorldTime;
	Event.Epoch = InstalledEpoch;
	Event.Map = Host.bArena ? TEXT("arena") : Host.GetMap() ? Host.GetMap()->MapName : TEXT("gap");
}

void FElysiumArenaScenarioRunner::Rebind(AElysiumMapActor* Map)
{
	// Never dereference the retired pointer. Keep labels/actions/counts; discard removal baseline.
	// Retail identities are reconstructed at 0x101a2e40 before 0x1011a620 post-restore.
	if (FElysiumEntityWorld* Previous = LiveWorld())
	{
		LastNow = SegmentElapsed + FMath::Max(0.0, Previous->NowSeconds() - SegmentWorld);
		Previous->SetAiTraceSink(FElysiumAiTraceSink());
	}
	SegmentElapsed = LastNow;
	Host.Map = Map;
	Host.World = Map ? Map->GetWorld() : nullptr;
	InstalledWorld = Map ? Map->GetEntityWorld() : nullptr;
	InstalledEpoch = InstalledWorld ? InstalledWorld->GetEpoch() : 0;
	SegmentWorld = InstalledWorld ? InstalledWorld->NowSeconds() : 0.0;
	TrackedEntities.Reset();
	PelvisSamples.Reset();
	PelvisSamples.SetNum(Record.Probes.Num());
	if (InstalledWorld) InstalledWorld->SetAiTraceSink([this](const FElysiumAiTraceEvent& Event) { RecordEvent(Event); });
}

bool FElysiumArenaScenarioRunner::ReadWitness(const FString& Who, const FString& Field,
	FElysiumArenaValue& Out, FString& OutError) const
{
	FElysiumEntityWorld* World = LiveWorld();
	FElysiumArenaValue::EType RequiredType;
	if (!World || !ElysiumArenaScenario::WitnessType(Field, RequiredType))
	{ OutError = TEXT("no live world or unknown witness"); return false; }
	Out.Type = RequiredType;
	if (Field == TEXT("clock")) Out.Number = World->NowSeconds();
	else if (Field == TEXT("world_generation")) Out.Number = World->GetEpoch();
	else if (Field == TEXT("map")) Out.String = Host.bArena ? TEXT("arena") : Host.GetMap()->MapName;
	else if (Field.StartsWith(TEXT("coordinator.")))
	{
		const int32 CoordinatorIndex = Field.StartsWith(TEXT("coordinator.normal.")) ? 1 : Field.StartsWith(TEXT("coordinator.player.")) ? 2 : 3;
		const FElysiumAttackCoordinator* Coordinator = World->AttackCoordinator(CoordinatorIndex);
		if (!Coordinator) { OutError = TEXT("coordinator unavailable"); return false; }
		if (Field.EndsWith(TEXT(".count"))) Out.Number = Coordinator->Num();
		else if (Field.EndsWith(TEXT(".cap"))) Out.Number = Coordinator->Cap();
		else
		{
			TArray<int32> Members;
			for (const FElysiumEntityHandle& Member : Coordinator->Handles()) Members.Add(Member.Index);
			Members.Sort();
			TArray<FString> Identities;
			for (int32 MemberIndex : Members) Identities.Add(FString::Printf(TEXT("#%d"), MemberIndex));
			Out.String = FString::Join(Identities, TEXT(","));
		}
	}
	else
	{
		FElysiumEntity* Entity = ElysiumArenaRunnerDetail::FindEntity(*World, Who);
		FElysiumNpc* Npc = Entity ? Entity->AsNpc() : nullptr;
		if (Field == TEXT("identity") && Entity) Out.String = Entity->Handle.ToString();
		else if (Field == TEXT("hidden") && Entity) Out.bBool = Entity->IsHidden();
		else if (Field == TEXT("callback") && Entity) Out.String = Entity->ThinkCallback.ToString();
		else if (Field == TEXT("callback.saved") && Entity) Out.String = Entity->SavedThinkCallback.ToString();
		else if (Field == TEXT("think.next") && Entity) Out.Number = Entity->NextThink;
		else if (Field == TEXT("task.index") && Npc) Out.Number = Npc->Schedule.TaskIndex;
		else if (Field == TEXT("task.status") && Npc) Out.Number = static_cast<int32>(Npc->Schedule.TaskStatus);
		else if (Field == TEXT("task.started") && Npc) Out.Number = Npc->Schedule.ScheduleStartedAt;
		else if (Field == TEXT("task.task_started") && Npc) Out.Number = Npc->Schedule.TaskStartedAt;
		else if (Field == TEXT("task.failure") && Npc) Out.Number = Npc->BaseScheduleHost.FailureReason;
		else if (Field == TEXT("task.wait") && Npc) Out.Number = Npc->BaseScheduleHost.WaitFinished;
		else if (Field == TEXT("task.move_wait") && Npc) Out.Number = Npc->BaseScheduleHost.MoveWaitFinished;
		else if (Field == TEXT("senses.gathered") && Npc) Out.bBool = Npc->Cognition.GatheredAt >= 0.0;
		else if (Field == TEXT("script.owner") && Entity) Out.String = Entity->ScriptOwner.ToString();
		else if (Field == TEXT("anim.sequence") && Npc) Out.Number = Npc->SequenceNumber;
		else if (Field == TEXT("anim.cycle") && Npc) Out.Number = Npc->SequenceCycle;
		else if (Field == TEXT("anim.rate") && Npc) Out.Number = Npc->SequencePlaybackRate;
		else if (Field == TEXT("anim.time") && Npc) Out.Number = Npc->AnimTime;
		else if (Field == TEXT("anim.previous_time") && Npc) Out.Number = Npc->PrevAnimTime;
		else if (Field == TEXT("anim.last_event") && Npc) Out.Number = Npc->LastEventCheck;
		else if (Field == TEXT("anim.ground_speed") && Npc) Out.Number = Npc->GroundSpeed;
		else if (Field == TEXT("anim.yaw_speed") && Npc) Out.Number = Npc->YawSpeed;
		else if (Field == TEXT("anim.finished") && Npc) Out.bBool = Npc->bSequenceFinished;
		else if (Field == TEXT("anim.past_half") && Npc) Out.bBool = Npc->SequencePastHalf;
		else if (Field == TEXT("move_shoot.active") && Npc) Out.bBool = Npc->MoveAndShootOverlay.bMovingAndShooting;
		else if (Field == TEXT("move_shoot.next") && Npc) Out.Number = Npc->MoveAndShootOverlay.NextShotTime;
		else if (Field == TEXT("move_shoot.burst") && Npc) Out.Number = Npc->MoveAndShootOverlay.MoveShots;
		else if (Field == TEXT("move_shoot.min_burst") && Npc) Out.Number = Npc->MoveAndShootOverlay.MinBurst;
		else if (Field == TEXT("move_shoot.max_burst") && Npc) Out.Number = Npc->MoveAndShootOverlay.MaxBurst;
		else if (Field == TEXT("move_shoot.pause_min") && Npc) Out.Number = Npc->MoveAndShootOverlay.PauseMin;
		else if (Field == TEXT("move_shoot.pause_max") && Npc) Out.Number = Npc->MoveAndShootOverlay.PauseMax;
		else if (Field == TEXT("move_shoot.initial_delay") && Npc) Out.Number = Npc->MoveAndShootOverlay.InitialDelay;
		else if (Field == TEXT("damage.attacker") && Npc) Out.String = Npc->BaseMemory.LastDamageAttacker.ToString();
		else if (Field == TEXT("damage.sum") && Npc) Out.Number = Npc->BaseMemory.RepeatedDamageAccumulated;
		else if (Field == TEXT("damage.time") && Npc) Out.Number = Npc->BaseMemory.RepeatedDamageWindowStart;
		else if (Field.StartsWith(TEXT("damage.position.")) && Npc)
		{
			const FVector& Position = Npc->BaseMemory.LastDamageAttackPosition;
			Out.Number = Field.EndsWith(TEXT(".x")) ? Position.X : Field.EndsWith(TEXT(".y")) ? Position.Y : Position.Z;
		}
		else if (Field == TEXT("senses.can_sense") && Npc) Out.bBool = Npc->Senses.bCanPerformSenses;
		else if (Field == TEXT("senses.sighted") && Npc)
		{
			TArray<FString> Identities;
			for (const FElysiumEntityHandle& Sighted : Npc->Senses.Sighted()) Identities.Add(Sighted.ToString());
			Out.String = FString::Join(Identities, TEXT(","));
		}
		else if (Field == TEXT("los.player") && Npc) Out.bBool = Npc->Senses.Memory.bPlayerLos;
		else if (Field == TEXT("los.pvs") && Npc) Out.bBool = Npc->Senses.Memory.bPlayerInPvs;
		else if (Field == TEXT("los.cache") && Npc) Out.Number = Npc->Senses.Memory.PlayerLosNextUpdateTime;
		else if (Field == TEXT("los.last_clear") && Npc) Out.Number = Npc->Senses.Memory.PlayerLosLastClearTime;
		else if (Field == TEXT("dialog.partner") && Entity && Entity->AsCombatCharacter()) Out.String = Entity->AsCombatCharacter()->GetDialogPartner().ToString();
		else if (Field == TEXT("dialog.partner_live") && Entity && Entity->AsCombatCharacter()) Out.bBool = World->Resolve(Entity->AsCombatCharacter()->GetDialogPartner()) != nullptr;
		else if (Field.StartsWith(TEXT("memory.")) && Npc)
		{
			const FElysiumEntity* Enemy = Npc->GetEnemy();
			const FElysiumNpcEnemyMemoryRecord* Memory = Enemy ? Npc->EnemyMemory.Find(Enemy->Handle) : nullptr;
			if (Field == TEXT("memory.enemy")) Out.String = Enemy ? Enemy->Handle.ToString() : TEXT("#<null>");
			else if (!Memory) { OutError = TEXT("committed enemy has no memory record"); return false; }
			else if (Field == TEXT("memory.last_seen")) Out.Number = Memory->LastSeenTime;
			else Out.Number = Field.EndsWith(TEXT(".x")) ? Memory->LastPosition.X : Field.EndsWith(TEXT(".y")) ? Memory->LastPosition.Y : Memory->LastPosition.Z;
		}
		else if (Field.StartsWith(TEXT("layer.")) && Npc)
		{
			TArray<FString> Parts; Field.ParseIntoArray(Parts, TEXT("."));
			const int32 LayerIndex = FCString::Atoi(*Parts[1]);
			const FElysiumAnimatingOverlay::FAnimOverlayLayer& Layer = Npc->AnimOverlay[LayerIndex]; // four native records, 0x10098c80
			const FString& Word = Parts[2];
			if (Word == TEXT("flags")) Out.Number = Layer.Flags;
			else if (Word == TEXT("finished")) Out.Number = Layer.SequenceFinished;
			else if (Word == TEXT("sequence")) Out.Number = Layer.Sequence;
			else if (Word == TEXT("cycle")) Out.Number = Layer.Cycle;
			else if (Word == TEXT("rate")) Out.Number = Layer.PlaybackRate;
			else if (Word == TEXT("weight")) Out.Number = Layer.Weight;
			else if (Word == TEXT("weight_max")) Out.Number = Layer.WeightMax;
			else if (Word == TEXT("blend_in")) Out.Number = Layer.BlendIn;
			else if (Word == TEXT("blend_out")) Out.Number = Layer.BlendOut;
			else if (Word == TEXT("activity")) Out.Number = Layer.Activity;
			else if (Word == TEXT("auto_kill")) Out.bBool = Layer.bAutoKillWhenFinished;
			else if (Word == TEXT("last_event")) Out.Number = Layer.LastEventCheck;
		}
		else if (Field == TEXT("dialog.open")) Out.bBool = World->GetOpenDialog() != nullptr;
		else if (!Host.ReadWitness || !Host.ReadWitness(*World, Who, Field, Out, OutError))
		{ if (OutError.IsEmpty()) OutError = FString::Printf(TEXT("unavailable retail witness %s.%s"), *Who, *Field); return false; }
	}
	if (Out.Type != RequiredType || (Out.Type == FElysiumArenaValue::EType::Number && !FMath::IsFinite(Out.Number)))
	{ OutError = TEXT("witness adapter returned an incompatible type/value"); return false; }
	return true;
}

void FElysiumArenaScenarioRunner::FailTransaction(const FString& Reason)
{
	if (bDone) return;
	FEvent& FailureEvent = Events.AddDefaulted_GetRef();
	StampEvent(FailureEvent, LiveWorld() ? LiveWorld()->NowSeconds() : SegmentWorld);
	FailureEvent.Kind = FName(TEXT("script"));
	FailureEvent.Text = FString::Printf(TEXT("transaction id=%llu failed reason=%s"), TransactionId, *Reason); // exact fence reason, 0x200975f0

	if (Record.bExpectFail)
	{
		FFailure Failure;
		Failure.bSet = true;
		Failure.Time = LastNow;
		Failure.Section = TEXT("script");
		Failure.Index = TransactionAction;
		Failure.Reason = Reason;
		Finish(LastNow, Failure);
	}
	else Abort(FString::Printf(TEXT("script[%d] transaction: %s"), TransactionAction, *Reason));
}

void FElysiumArenaScenarioRunner::OnTransactionFence(const ElysiumArenaStage::FTransactionFence& Fence)
{
	using ElysiumArenaStage::EFence;
	if (!bTransactionPending || bDone) return;
	if (TransactionId == 0) TransactionId = Fence.OperationId;
	if (Fence.OperationId != TransactionId) { FailTransaction(TEXT("wrong operation id")); return; }
	const FElysiumArenaAction& Action = Record.Script[TransactionAction];
	if (Fence.Phase == EFence::Failed) { FailTransaction(Fence.Reason); return; }
	if (Fence.Phase == EFence::Rebinding)
	{
		if (!Action.Map.IsEmpty() && Action.Map != Fence.MapIdentity) { FailTransaction(TEXT("wrong map at reconstruction fence")); return; }
		Rebind(Fence.Map.Get());
	}
	FEvent& Event = Events.AddDefaulted_GetRef();
	StampEvent(Event, LiveWorld() ? LiveWorld()->NowSeconds() : SegmentWorld);
	Event.Kind = FName(TEXT("script"));
	Event.Text = FString::Printf(TEXT("transaction id=%llu fence=%d npc_draw=%d"), Fence.OperationId,
		static_cast<int32>(Fence.Phase), ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).GetCurrentSeed());
	if (Fence.Phase == EFence::Captured)
	{
		bTransactionCaptured = true;
		if (!Action.Checkpoint.IsEmpty())
		{
			FCheckpoint& Saved = Checkpoints.FindOrAdd(Action.Checkpoint);
			Saved.Slot = Action.Slot;
			Saved.SaveBase = LiveWorld() ? LiveWorld()->NowSeconds() : 0.0;
			for (const FElysiumArenaWitness& Word : Action.Fields)
			{
				FElysiumArenaValue Value;
				FString Error;
				if (!ReadWitness(Word.Who, Word.Field, Value, Error)) { FailTransaction(Error); return; }
				Saved.Captured.Add(Word.Who + TEXT("\n") + Word.Field, MoveTemp(Value));
			}
		}
	}
	if (Fence.Phase == EFence::Written)
	{
		if (!bTransactionCaptured) { FailTransaction(TEXT("write completed without capture fence")); return; }
		if (FCheckpoint* Saved = Checkpoints.Find(Action.Checkpoint)) Saved->bWritten = true;
		bTransactionPending = false;
	}
	if (Fence.Phase == EFence::Applied)
	{
		if (!LiveWorld()) { FailTransaction(TEXT("apply without pre-restore world rebind")); return; }
		bTransactionApplied = true;
		if (FCheckpoint* Saved = Checkpoints.Find(Action.Checkpoint))
		{
			Saved->RestoreBase = LiveWorld()->NowSeconds();
			for (const TPair<FString, FElysiumArenaValue>& Pair : Saved->Captured)
			{
				FString Who, Field;
				Pair.Key.Split(TEXT("\n"), &Who, &Field);
				FElysiumArenaValue Value;
				FString Error;
				if (!ReadWitness(Who, Field, Value, Error)) { FailTransaction(Error); return; }
				Saved->Applied.Add(Pair.Key, MoveTemp(Value));
			}
			Saved->bApplied = true;
		}
	}
	const FString ProbeFence = Fence.Phase == EFence::Rebinding ? TEXT("pre_init")
		: Fence.Phase == EFence::Captured ? TEXT("captured") : Fence.Phase == EFence::Applied ? TEXT("applied")
		: Fence.Phase == EFence::Ready ? TEXT("ready") : TEXT("");
	for (int32 ProbeIndex = 0; ProbeIndex < Record.Probes.Num(); ++ProbeIndex)
	{
		const FElysiumArenaProbeSpec& Probe = Record.Probes[ProbeIndex];
		if (ProbeFence.IsEmpty() || Probe.Fence != ProbeFence || ProbeRead[ProbeIndex]) continue;
		if ((Probe.Fence == TEXT("captured") && Action.Do != EElysiumArenaAction::Save)
			|| (Probe.Fence == TEXT("applied") && Action.Do != EElysiumArenaAction::Load)) continue;
		if (!Probe.Checkpoint.IsEmpty() && Action.Checkpoint != Probe.Checkpoint) continue;
		ProbeRead[ProbeIndex] = true;
		FString Read, Error;
		if (!ReadProbe(Probe, Read, Error))
		{ FailTransaction(Error.IsEmpty() ? FString::Printf(TEXT("fence probe[%d] failed: %s"), ProbeIndex, *Read) : Error); return; }
	}
	if (Fence.Phase == EFence::Ready)
	{
		if (!bTransactionApplied) { FailTransaction(TEXT("ready without apply fence")); return; }
		if (Action.Do == EElysiumArenaAction::FreshMap)
		{
			for (const FElysiumArenaRow& Row : Record.Cast)
				if (!LiveWorld() || !ElysiumArenaRunnerDetail::FindEntity(*LiveWorld(), Row.Name))
				{ FailTransaction(TEXT("fresh-map cast missing ") + Row.Name); return; }
		}
		bTransactionPending = false;
	}
}

bool FElysiumArenaScenarioRunner::CompareCheckpoint(const FElysiumArenaAction& Action, FString& OutError) const
{
	const FCheckpoint* Saved = Checkpoints.Find(Action.Checkpoint);
	if (!Saved || !Saved->bWritten || !Saved->bApplied)
	{ OutError = TEXT("checkpoint has no successful capture/write/apply fence"); return false; }
	for (const FElysiumArenaWitness& Word : Action.Fields)
	{
		const FString Key = Word.Who + TEXT("\n") + Word.Field;
		const FElysiumArenaValue* Before = Saved->Captured.Find(Key);
		const FElysiumArenaValue* After = Saved->Applied.Find(Key);
		if (!Before || !After || !ElysiumArenaScenario::WitnessEqual(ElysiumArenaScenario::RebaseWitness(Word.Field, *Before, Saved->SaveBase, Saved->RestoreBase), *After, Word.Tolerance))
		{ OutError = FString::Printf(TEXT("restore equality failed %s.%s: saved=%s applied=%s"), *Word.Who, *Word.Field,
			Before ? *Before->Describe() : TEXT("unavailable"), After ? *After->Describe() : TEXT("unavailable")); return false; }
	}
	return true;
}

bool FElysiumArenaScenarioRunner::WriteTrace(const FString& Path, FString& OutError) const
{
	FString Text;
	Text.Reserve(64 + Events.Num() * 96);
	Text += TEXT("time\tname\tkind\ttext\tworld_time\tepoch\tmap\n");
	for (const FEvent& Event : Events)
	{
		// Scenario seconds once zero is known; the raw world clock otherwise (a run that never activated).
		Text += FString::Printf(TEXT("%.3f\t%s\t%s\t%s\t%.6f\t%u\t%s\n"), ScenarioTime(Event),
			Event.Name.IsEmpty() ? TEXT("-") : *ElysiumArenaRunnerDetail::OneLine(Event.Name),
			*Event.Kind.ToString(), *ElysiumArenaRunnerDetail::OneLine(Event.Text), Event.WorldTime, Event.Epoch, *ElysiumArenaRunnerDetail::OneLine(Event.Map));
	}
	if (!FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		OutError = FString::Printf(TEXT("could not write %s"), *Path);
		return false;
	}
	return true;
}

#endif // !UE_BUILD_SHIPPING
