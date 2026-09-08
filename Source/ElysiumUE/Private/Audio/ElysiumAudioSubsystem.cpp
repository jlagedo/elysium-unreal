#include "ElysiumAudioSubsystem.h"

#include "Audio/ElysiumPcmSoundWave.h"
#include "ElysiumContentPaths.h"
#include "ElysiumMapSubsystem.h"
#include "ElysiumUserSettings.h"

#include "AudioDevice.h"
#include "Async/Async.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Paths.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundWaveProcedural.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumAudio, Log, All);

// This is a debug override, not a saved setting. A fresh launch is audible.
static TAutoConsoleVariable<int32> CVarMute(
	TEXT("elysium.Mute"), 0,
	TEXT("Non-persistent audio debug override: 1 = mute every Elysium voice, 0 = audible."),
	ECVF_Default);

namespace
{
	FSoundAttenuationSettings MakeSphereAttenuation(float RadiusCm)
	{
		FSoundAttenuationSettings Att;
		Att.bAttenuate = true;
		Att.bSpatialize = true;
		Att.AttenuationShape = EAttenuationShape::Sphere;
		Att.AttenuationShapeExtents = FVector::ZeroVector;
		Att.FalloffDistance = FMath::Max(RadiusCm, 1.f);
		Att.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
		Att.dBAttenuationAtMax = -60.f;
		return Att;
	}

	const TCHAR* DomainFolder(EElysiumAudioSourceDomain Domain)
	{
		switch (Domain)
		{
		case EElysiumAudioSourceDomain::Character:  return TEXT("character");
		case EElysiumAudioSourceDomain::Openable:   return TEXT("events/openable");
		case EElysiumAudioSourceDomain::Switch:     return TEXT("events/switches");
		case EElysiumAudioSourceDomain::Computer:   return TEXT("events/computer");
		case EElysiumAudioSourceDomain::Weapon:     return TEXT("events/weapons");
		case EElysiumAudioSourceDomain::Surface:    return TEXT("events/surfaces");
		case EElysiumAudioSourceDomain::Item:       return TEXT("events/items");
		case EElysiumAudioSourceDomain::Discipline: return TEXT("events/disciplines");
		case EElysiumAudioSourceDomain::Whisper:    return TEXT("whispers");
		case EElysiumAudioSourceDomain::Radio:      return TEXT("radio");
		case EElysiumAudioSourceDomain::News:       return TEXT("radio/news");
		default:                                    return TEXT("");
		}
	}

	EElysiumAudioCategory ResolveCategory(EElysiumAudioCategory Requested, const FString& Rel)
	{
		if (Requested != EElysiumAudioCategory::Auto)
		{
			return Requested;
		}
		if (Rel.StartsWith(TEXT("character/dlg/"))) { return EElysiumAudioCategory::Dialogue; }
		if (Rel.StartsWith(TEXT("music/")) || Rel.StartsWith(TEXT("radio/")) ||
			Rel.StartsWith(TEXT("licensed/"))) { return EElysiumAudioCategory::Music; }
		if (Rel.StartsWith(TEXT("ui/")) || Rel.StartsWith(TEXT("interface/")))
		{
			return EElysiumAudioCategory::Ui;
		}
		if (Rel.StartsWith(TEXT("environmental/")) || Rel.StartsWith(TEXT("ambient/")))
		{
			return EElysiumAudioCategory::Ambience;
		}
		return EElysiumAudioCategory::Sfx;
	}

	const TCHAR* CategoryAssetName(EElysiumAudioCategory Category)
	{
		switch (Category)
		{
		case EElysiumAudioCategory::Music:    return TEXT("Music");
		case EElysiumAudioCategory::Dialogue: return TEXT("Dialogue");
		case EElysiumAudioCategory::Ambience: return TEXT("Ambience");
		case EElysiumAudioCategory::Ui:       return TEXT("UI");
		case EElysiumAudioCategory::Sfx:      return TEXT("SFX");
		default:                              return TEXT("Master");
		}
	}
}

void UElysiumAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	IsSoundCorpusPresent();   // one directory probe + one warning if the corpus was never deployed

	// Every voice carries its owner's map epoch, so unloading a map is what stops the ambient bed
	// and the dialogue line it was holding.
	if (UElysiumMapSubsystem* Maps = Collection.InitializeDependency<UElysiumMapSubsystem>())
	{
		MapEpochRetiredHandle = Maps->OnMapEpochRetired().AddUObject(
			this, &UElysiumAudioSubsystem::RetireMapEpoch);
	}

	IConsoleManager& CM = IConsoleManager::Get();
	CVarMute->SetOnChangedCallback(FConsoleVariableDelegate::CreateWeakLambda(this,
		[this](IConsoleVariable* Var)
		{
			ApplyMasterGain();
			UE_LOG(LogElysiumAudio, Display, TEXT("audio %s"),
				Var->GetInt() != 0 ? TEXT("muted") : TEXT("unmuted"));
		}));

	ConsoleObjects.Add(CM.RegisterConsoleCommand(
		TEXT("elysium.playsound"),
		TEXT("elysium.playsound <logical path under sound/>"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (Args.IsEmpty())
			{
				UE_LOG(LogElysiumAudio, Display, TEXT("usage: elysium.playsound <logical path>"));
				return;
			}
			PreviewSound2D(FString::Join(Args, TEXT(" ")));
		}), ECVF_Cheat));

	ConsoleObjects.Add(CM.RegisterConsoleCommand(
		TEXT("elysium.sound_info"),
		TEXT("elysium.sound_info <logical path under sound/>"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (Args.IsEmpty())
			{
				UE_LOG(LogElysiumAudio, Display, TEXT("usage: elysium.sound_info <logical path>"));
				return;
			}
			const FString Rel = ResolveSourcePath(FElysiumAudioSource::Path(FString::Join(Args, TEXT(" "))));
			const FElysiumSoundInfo* Info = Probe(Rel);
			if (!Info)
			{
				UE_LOG(LogElysiumAudio, Warning, TEXT("sound_info: unresolved %s"), *Rel);
				return;
			}
			UE_LOG(LogElysiumAudio, Display,
				TEXT("%s: %s %dch %dHz %lld frames %.3fs decode=%.2fms%s"),
				*Rel, *Info->FormatName(), Info->Channels, Info->SampleRate, Info->FrameCount,
				Info->DurationSeconds, Info->DecodeMilliseconds,
				Info->Error.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" error=%s"), *Info->Error));
		}), ECVF_Cheat));

	// The lead a cue is scheduled with, term by term and labelled by where each came from,
	// so the number is explicable instead of trusted.
	ConsoleObjects.Add(CM.RegisterConsoleCommand(
		TEXT("elysium.audio_latency"),
		TEXT("Report the output path's latency: what the device says, what the render path measured."),
		FConsoleCommandDelegate::CreateWeakLambda(this, [this]()
		{
			RefreshOutputLatency();
			const FElysiumAudioLatency& L = Latency;
			if (!L.bDeviceQueried)
			{
				UE_LOG(LogElysiumAudio, Display,
					TEXT("audio latency: no device — fallback lead %.1f ms"), L.Lead() * 1000.f);
				return;
			}
			UE_LOG(LogElysiumAudio, Display,
				TEXT("audio latency on %s (%s): %d Hz, %d-frame callback x%d, device period %d, endpoint %d"),
				*L.DeviceName, *L.PlatformApi, L.SampleRate, L.CallbackFrames, L.OutputBuffers,
				L.DevicePeriodFrames, L.EndpointFrames);
			UE_LOG(LogElysiumAudio, Display,
				TEXT("  mixer queue %.1f ms (queried) + endpoint %.1f ms (modelled) + submit->render %.1f ms "
					 "(measured over %d dialogue lines, peak %.1f, of which decode %.1f) = lead %.1f ms"),
				L.MixerQueueSeconds * 1000.f, L.EndpointSeconds * 1000.f,
				L.SubmitToRenderSeconds * 1000.f, L.SubmitToRenderSamples,
				L.SubmitToRenderPeakSeconds * 1000.f, L.DecodeSeconds * 1000.f,
				L.Lead() * 1000.f);
			for (const FElysiumAudioVoice& Voice : Voices)
			{
				const double Head = Voice.RenderHeadSeconds();
				if (Head >= 0.0)
				{
					UE_LOG(LogElysiumAudio, Display,
						TEXT("  voice %s: render head %.3fs of %.3fs, submit->render %.1f ms"),
						*Voice.Event.ResolvedPath, Head, Voice.Event.DurationSeconds,
						Voice.Render->SubmitToRenderSeconds() * 1000.0);
				}
			}
		}), ECVF_Cheat));
}

// AUD0.1 (2026-09-08): the `exports/audio/catalog.json` read is gone. It was an offline validation
// record, not a VtMB artifact — it was opened for existence and `version == 1` only, stored nothing,
// and set `bCatalogReady` either way, so the map-activation gate it fed could only ever fail on a
// watchdog timeout. Readiness is corpus presence: the deployed `Content/ElysiumCorpus/sound` tree,
// checked once as a directory.
bool UElysiumAudioSubsystem::IsSoundCorpusPresent()
{
	static const bool bPresent = []()
	{
		const FString Dir = FElysiumContentPaths::SoundDir();
		const bool bOk = IFileManager::Get().DirectoryExists(*Dir);
		if (!bOk)
		{
			UE_LOG(LogElysiumAudio, Warning,
				TEXT("no deployed sound corpus at %s — every request will miss (run `uv run elysium import sound`)"),
				*Dir);
		}
		return bOk;
	}();
	return bPresent;
}

void UElysiumAudioSubsystem::Deinitialize()
{
	if (MapEpochRetiredHandle.IsValid())
	{
		if (UElysiumMapSubsystem* Maps = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UElysiumMapSubsystem>() : nullptr)
		{
			Maps->OnMapEpochRetired().Remove(MapEpochRetiredHandle);
		}
		MapEpochRetiredHandle.Reset();
	}
	CVarMute->SetOnChangedCallback(FConsoleVariableDelegate());
	for (IConsoleObject* Obj : ConsoleObjects)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Obj);
	}
	ConsoleObjects.Empty();
	StopAllVoices();
	DecodeResults.Empty();
	FElysiumSoundCache::FlushAll();
	Super::Deinitialize();
}

FString UElysiumAudioSubsystem::NormalizeSourcePath(const FString& AuthoredPath)
{
	FString Rel = AuthoredPath;
	Rel.TrimStartAndEndInline();
	Rel.ReplaceInline(TEXT("\\"), TEXT("/"));
	while (Rel.StartsWith(TEXT("/")))
	{
		Rel.RightChopInline(1);
	}
	if (Rel.StartsWith(TEXT("sound/"), ESearchCase::IgnoreCase))
	{
		Rel.RightChopInline(6);
	}
	FPaths::CollapseRelativeDirectories(Rel);
	while (Rel.StartsWith(TEXT("./")))
	{
		Rel.RightChopInline(2);
	}
	return Rel.ToLower();
}

FString UElysiumAudioSubsystem::ResolveSourcePath(const FElysiumAudioSource& Source)
{
	FString Rel;
	if (Source.Domain == EElysiumAudioSourceDomain::DirectPath)
	{
		Rel = NormalizeSourcePath(Source.LogicalPath);
	}
	else
	{
		Rel = NormalizeSourcePath(FPaths::Combine(DomainFolder(Source.Domain), Source.EventId));
	}

	if (Source.Domain == EElysiumAudioSourceDomain::Whisper &&
		FPaths::GetExtension(Rel).IsEmpty())
	{
		TArray<FString> Candidates;
		const FString Directory = FPaths::Combine(FElysiumContentPaths::SoundDir(), Rel);
		IFileManager::Get().FindFiles(Candidates, *(Directory / TEXT("*.wav")), true, false);
		Candidates.Sort();
		if (!Candidates.IsEmpty())
		{
			// Typed whisper events are sets. Selection is stable for a given semantic id until the
			// game RNG adapter takes ownership of variation.
			const int32 Pick = static_cast<int32>(GetTypeHash(Source.EventId.ToLower()) % Candidates.Num());
			return NormalizeSourcePath(Rel / Candidates[Pick]);
		}
	}

	const FString Ext = FPaths::GetExtension(Rel, true);
	if (!Ext.IsEmpty())
	{
		return Rel;
	}

	// VtMB lines and dynamic script paths frequently omit a suffix. One policy owns the authored
	// MP3-first, WAV-fallback rule instead of each caller probing the filesystem.
	const FString Mp3 = Rel + TEXT(".mp3");
	if (FPaths::FileExists(FPaths::Combine(FElysiumContentPaths::SoundDir(), Mp3)))
	{
		return Mp3;
	}
	const FString Wav = Rel + TEXT(".wav");
	if (FPaths::FileExists(FPaths::Combine(FElysiumContentPaths::SoundDir(), Wav)))
	{
		return Wav;
	}
	return Mp3;
}

FElysiumSoundCache::FDecodedPtr UElysiumAudioSubsystem::LoadAndRecord(const FString& Rel)
{
	const FElysiumSoundCache::FDecodedPtr Decoded =
		FElysiumSoundCache::LoadSoundDecoded(FElysiumContentPaths::SoundDir(), Rel);
	if (Decoded)
	{
		DecodeResults.Add(Rel, Decoded->Info);
	}
	return Decoded;
}

const FElysiumSoundInfo* UElysiumAudioSubsystem::Probe(const FString& Rel)
{
	const FString Resolved = ResolveSourcePath(FElysiumAudioSource::Path(Rel));
	const FElysiumSoundCache::FDecodedPtr Decoded = LoadAndRecord(Resolved);
	const FElysiumSoundInfo* Recorded = DecodeResults.Find(Resolved);
	return Decoded ? Recorded : nullptr;
}

FElysiumVoiceHandle UElysiumAudioSubsystem::AllocateHandle()
{
	uint32 Slot = MAX_uint32;
	if (!FreeSlots.IsEmpty())
	{
		Slot = FreeSlots.Pop(EAllowShrinking::No);
	}
	else
	{
		Slot = static_cast<uint32>(SlotGenerations.Add(1));
	}
	uint32& Generation = SlotGenerations[Slot];
	if (Generation == 0)
	{
		Generation = 1;
	}
	return { Slot, Generation };
}

void UElysiumAudioSubsystem::ReleaseHandle(FElysiumVoiceHandle Handle)
{
	if (Handle.Slot >= static_cast<uint32>(SlotGenerations.Num()))
	{
		return;
	}
	uint32& Generation = SlotGenerations[Handle.Slot];
	++Generation;
	if (Generation == 0)
	{
		Generation = 1;
	}
	FreeSlots.Add(Handle.Slot);
}

FElysiumAudioVoice* UElysiumAudioSubsystem::FindVoice(FElysiumVoiceHandle Handle)
{
	return Handle.IsValid()
		? Voices.FindByPredicate([Handle](const FElysiumAudioVoice& Voice) { return Voice.Handle == Handle; })
		: nullptr;
}

const FElysiumAudioVoice* UElysiumAudioSubsystem::FindVoice(FElysiumVoiceHandle Handle) const
{
	return Handle.IsValid()
		? Voices.FindByPredicate([Handle](const FElysiumAudioVoice& Voice) { return Voice.Handle == Handle; })
		: nullptr;
}

double FElysiumAudioVoice::RenderHeadSeconds() const
{
	if (!Render || !Render->HasRendered())
	{
		return -1.0;
	}
	const double Head = Render->RenderHeadSeconds();
	// A loop's generator wraps its cursor, so the head keeps climbing past the file. Folding it
	// back is what makes the reading a *position* rather than a total.
	return (Request.bLooping && Event.DurationSeconds > 0.f)
		? FMath::Fmod(Head, static_cast<double>(Event.DurationSeconds))
		: Head;
}

void UElysiumAudioSubsystem::RefreshOutputLatency()
{
	const FElysiumAudioLatency Queried = ElysiumAudioLatency::QueryDevice(GetWorld());
	// The measured half belongs to the session, not to the device query, so it survives a refresh.
	const float Mean = Latency.SubmitToRenderSeconds;
	const float Peak = Latency.SubmitToRenderPeakSeconds;
	const float Decode = Latency.DecodeSeconds;
	const int32 Samples = Latency.SubmitToRenderSamples;
	Latency = Queried;
	Latency.SubmitToRenderSeconds = Mean;
	Latency.SubmitToRenderPeakSeconds = Peak;
	Latency.DecodeSeconds = Decode;
	Latency.SubmitToRenderSamples = Samples;
	bLatencyQueried = true;
}

void UElysiumAudioSubsystem::ObserveRenderLatency(FElysiumAudioVoice& Voice)
{
	if (Voice.bRenderLatencyObserved || !Voice.Render || !Voice.Render->HasRendered())
	{
		return;
	}
	// Dialogue only. The lead this feeds is the one a scene schedules speech with, and a 209-second
	// ambience bed or a 223-second music stream costs an order of magnitude more to decode than a
	// spoken line — folding those in would lead every line by a delay no line ever pays.
	if (Voice.Request.Category != EElysiumAudioCategory::Dialogue)
	{
		return;
	}
	const double Observed = Voice.Render->SubmitToRenderSeconds();
	if (Observed < 0.0)
	{
		return;
	}
	Voice.bRenderLatencyObserved = true;
	SubmitToRenderTotal += Observed;
	if (const FElysiumSoundInfo* Info = DecodeResults.Find(Voice.Event.ResolvedPath))
	{
		DecodeTotal += Info->DecodeMilliseconds * 0.001;
	}
	++Latency.SubmitToRenderSamples;
	Latency.SubmitToRenderSeconds =
		static_cast<float>(SubmitToRenderTotal / Latency.SubmitToRenderSamples);
	Latency.DecodeSeconds = static_cast<float>(DecodeTotal / Latency.SubmitToRenderSamples);
	Latency.SubmitToRenderPeakSeconds =
		FMath::Max(Latency.SubmitToRenderPeakSeconds, static_cast<float>(Observed));
}

double UElysiumAudioSubsystem::AudioClock() const
{
	if (const UWorld* World = GetWorld())
	{
		if (const FAudioDevice* Device = World->GetAudioDeviceRaw())
		{
			return Device->GetInterpolatedAudioClock();
		}
	}
	return 0.0;
}

void UElysiumAudioSubsystem::Transition(FElysiumAudioVoice& Voice, EElysiumVoiceState State,
	EElysiumVoiceCompletion Completion)
{
	Voice.Event.State = State;
	Voice.Event.Completion = Completion;
	VoiceEvents.Broadcast(Voice.Event);
}

FElysiumVoiceHandle UElysiumAudioSubsystem::Submit(const FElysiumAudioRequest& Request)
{
	check(IsInGameThread());

	FElysiumAudioVoice Voice;
	Voice.Handle = AllocateHandle();
	Voice.Request = Request;
	Voice.Event.Handle = Voice.Handle;
	Voice.Event.State = EElysiumVoiceState::PendingDecode;
	Voice.Event.ResolvedPath = ResolveSourcePath(Request.Source);
	Voice.Request.Category = ResolveCategory(Request.Category, Voice.Event.ResolvedPath);
	Voice.Event.MediaOffsetSeconds = FMath::Max(Request.StartOffsetSeconds, 0.f);
	Voice.Event.ScheduledAudioClock =
		Request.ScheduledAudioClock >= 0.0 ? Request.ScheduledAudioClock : AudioClock();
	// The submit instant on the wall clock, stamped here because everything that follows
	// (the file read, the mp3 decode, the game-thread realization) happens after this line and is
	// invisible to ScheduledAudioClock. The probe itself is minted by the wave, so the stamp rides
	// on the voice until there is one to hand it to.
	Voice.SubmitSeconds = FPlatformTime::Seconds();

	VoiceEvents.Broadcast(Voice.Event);
	const FElysiumVoiceHandle Handle = Voice.Handle;
	const FString ResolvedPath = Voice.Event.ResolvedPath;
	Voices.Add(MoveTemp(Voice));

	const TWeakObjectPtr<UElysiumAudioSubsystem> WeakThis(this);
	Async(EAsyncExecution::ThreadPool, [WeakThis, Handle, ResolvedPath]()
	{
		FElysiumSoundCache::FDecodedPtr Decoded =
			FElysiumSoundCache::LoadSoundDecoded(FElysiumContentPaths::SoundDir(), ResolvedPath);
		AsyncTask(ENamedThreads::GameThread,
			[WeakThis, Handle, Decoded = MoveTemp(Decoded)]() mutable
			{
				if (UElysiumAudioSubsystem* Self = WeakThis.Get())
				{
					Self->RealizeVoice(Handle, MoveTemp(Decoded));
				}
			});
	});
	return Handle;
}

void UElysiumAudioSubsystem::RealizeVoice(
	FElysiumVoiceHandle Handle, FElysiumSoundCache::FDecodedPtr Decoded)
{
	check(IsInGameThread());
	FElysiumAudioVoice* VoicePtr = FindVoice(Handle);
	if (!VoicePtr)
	{
		return; // owner/epoch retired while decode was queued
	}
	if (!Decoded || !Decoded->Info.Error.IsEmpty())
	{
		const int32 Index = Voices.IndexOfByPredicate(
			[Handle](const FElysiumAudioVoice& Item) { return Item.Handle == Handle; });
		if (Index != INDEX_NONE)
		{
			CompleteAt(Index, Decoded
				? EElysiumVoiceCompletion::DecodeFailed
				: EElysiumVoiceCompletion::MissingSource);
		}
		return;
	}
	DecodeResults.Add(VoicePtr->Event.ResolvedPath, Decoded->Info);

	// Component realization is deliberately game-thread-only. The worker only touched bytes and
	// decoder state, so an obsolete epoch can be canceled without ever creating a UObject.
	FElysiumAudioVoice& Voice = *VoicePtr;
	const FElysiumAudioRequest& Request = Voice.Request;
	UWorld* World = GetWorld();
	USoundWaveProcedural* Wave = World
		? FElysiumSoundCache::MakeWave(Decoded, Request.bLooping)
		: nullptr;
	if (!World || !Wave)
	{
		const int32 Index = Voices.IndexOfByPredicate(
			[Handle](const FElysiumAudioVoice& Item) { return Item.Handle == Handle; });
		if (Index != INDEX_NONE)
		{
			CompleteAt(Index, EElysiumVoiceCompletion::DecodeFailed);
		}
		return;
	}

	const float StartGain = Request.FadeInSeconds > 0.f ? 0.f : OutputGain(Voice.Request);
	UAudioComponent* Comp = nullptr;
	USceneComponent* Attach = Request.Placement.AttachTo.Get();
	const TCHAR* CategoryName = CategoryAssetName(Voice.Request.Category);
	USoundConcurrency* Concurrency = nullptr;
	if (!Request.ConcurrencyKey.IsNone())
	{
		Concurrency = LoadObject<USoundConcurrency>(nullptr,
			*FElysiumContentPaths::AudioConcurrency(CategoryName));
	}
	if (!Request.Placement.bSpatialized)
	{
		Comp = UGameplayStatics::SpawnSound2D(World, Wave, StartGain, Request.Pitch,
			Request.StartOffsetSeconds, Concurrency,
			EnumHasAnyFlags(Request.Routing, EElysiumAudioRouting::PersistAcrossTravel), false);
	}
	else
	{
		// Create without a location first so UE's short-sound distance optimization cannot
		// discard the component before the request ledger can observe a completion. Apply all
		// spatial policy before Play so virtualization sees the authored attenuation from frame 0.
		Comp = UGameplayStatics::CreateSound2D(World, Wave, StartGain, Request.Pitch,
			Request.StartOffsetSeconds, Concurrency, false, false);
		if (Comp)
		{
			Comp->bIsUISound = false;
			Comp->bAllowSpatialization = true;
			if (Request.AttenuationOverride.IsValid())
			{
				// An authored falloff (the body-sound path's Source sound-level curve) wins over
				// the radius: the caller already knows the whole shape.
				Comp->bOverrideAttenuation = true;
				Comp->AttenuationOverrides = *Request.AttenuationOverride;
			}
			else if (Request.AttenuationRadiusCm > 0.f)
			{
				Comp->bOverrideAttenuation = true;
				Comp->AttenuationOverrides = MakeSphereAttenuation(
					Request.AttenuationRadiusCm);
			}
			if (Attach)
			{
				Comp->AttachToComponent(Attach,
					FAttachmentTransformRules::SnapToTargetNotIncludingScale);
				Comp->SetRelativeLocation(FVector::ZeroVector);
				Comp->bStopWhenOwnerDestroyed = true;
			}
			else
			{
				Comp->SetWorldLocation(Request.Placement.Location);
			}
			Comp->Play(Request.StartOffsetSeconds);
		}
	}

	if (!Comp)
	{
		const int32 Index = Voices.IndexOfByPredicate(
			[Handle](const FElysiumAudioVoice& Item) { return Item.Handle == Handle; });
		if (Index != INDEX_NONE)
		{
			CompleteAt(Index, EElysiumVoiceCompletion::DecodeFailed);
		}
		return;
	}

	Comp->SoundClassOverride = LoadObject<USoundClass>(nullptr,
		*FElysiumContentPaths::AudioSoundClass(CategoryName));
	if (Request.FadeInSeconds > 0.f)
	{
		const float TargetGain = OutputGain(Voice.Request);
		// UAudioComponent treats an AdjustVolume target of exactly zero as FadeOut and destroys
		// the active sound. Keep silent phase-locked stems alive at an inaudible floor instead.
		Comp->AdjustVolume(Request.FadeInSeconds,
			FMath::IsNearlyZero(TargetGain) ? 0.0001f : TargetGain);
	}
	if (EnumHasAnyFlags(Request.Routing, EElysiumAudioRouting::NoPause))
	{
		Comp->bIsUISound = true;
	}

	Voice.Comp = Comp;
	Voice.Wave = Wave;
	// The probe is created with the wave and written by its generator on the render thread. Handing
	// it the submit stamp here closes the one gap ScheduledAudioClock cannot see: how long the file
	// read, the decode and this realization actually took before a single sample was pulled.
	if (const UElysiumPcmSoundWave* PcmWave = Cast<UElysiumPcmSoundWave>(Wave))
	{
		Voice.Render = PcmWave->RenderProbe();
		if (Voice.Render)
		{
			Voice.Render->SubmitSeconds.store(Voice.SubmitSeconds, std::memory_order_relaxed);
		}
	}
	Voice.Event.DurationSeconds = Decoded->Info.DurationSeconds;
	Comp->OnAudioFinishedNative.AddUObject(this, &UElysiumAudioSubsystem::HandleAudioFinished);
	const double Now = AudioClock();
	if (Request.ScheduledAudioClock > Now)
	{
		Comp->SetPaused(true);
		Transition(Voice, EElysiumVoiceState::Scheduled);
	}
	else
	{
		Voice.Event.ScheduledAudioClock = Now;
		Transition(Voice, EElysiumVoiceState::Playing);
		EmitGameplayNoise(Voice);
	}
}

void UElysiumAudioSubsystem::EmitGameplayNoise(const FElysiumAudioVoice& Voice)
{
	if (Voice.Request.GameplayNoise.IsSet() &&
		!EnumHasAnyFlags(Voice.Request.Routing, EElysiumAudioRouting::NoGameplayNoise))
	{
		FElysiumNoiseEvent Noise = Voice.Request.GameplayNoise.GetValue();
		Noise.MapEpoch = Voice.Request.Owner.MapEpoch;
		NoiseEvents.Broadcast(Noise);
	}
}

void UElysiumAudioSubsystem::Prefetch(const FElysiumAudioSource& Source)
{
	const FString Resolved = ResolveSourcePath(Source);
	++PendingPrefetches;
	const TWeakObjectPtr<UElysiumAudioSubsystem> WeakThis(this);
	Async(EAsyncExecution::ThreadPool, [WeakThis, Resolved]()
	{
		const FElysiumSoundCache::FDecodedPtr Decoded =
			FElysiumSoundCache::LoadSoundDecoded(FElysiumContentPaths::SoundDir(), Resolved);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Resolved, Decoded]()
		{
			if (UElysiumAudioSubsystem* Self = WeakThis.Get(); Self && Decoded)
			{
				Self->DecodeResults.Add(Resolved, Decoded->Info);
			}
			if (UElysiumAudioSubsystem* Self = WeakThis.Get())
			{
				Self->PendingPrefetches = FMath::Max(0, Self->PendingPrefetches - 1);
			}
		});
	});
}

void UElysiumAudioSubsystem::Stop(FElysiumVoiceHandle Handle, float FadeSeconds)
{
	FElysiumAudioVoice* Voice = FindVoice(Handle);
	if (!Voice)
	{
		return;
	}
	if (FadeSeconds > 0.f && IsValid(Voice->Comp))
	{
		Voice->Comp->FadeOut(FadeSeconds, 0.f);
		Voice->DestroyAudioClock = AudioClock() + FadeSeconds;
		Transition(*Voice, EElysiumVoiceState::Fading, EElysiumVoiceCompletion::Stopped);
		return;
	}
	const int32 Index = Voices.IndexOfByPredicate(
		[Handle](const FElysiumAudioVoice& Item) { return Item.Handle == Handle; });
	if (Index != INDEX_NONE)
	{
		CompleteAt(Index, EElysiumVoiceCompletion::Stopped);
	}
}

void UElysiumAudioSubsystem::Pause(FElysiumVoiceHandle Handle, bool bPaused)
{
	if (FElysiumAudioVoice* Voice = FindVoice(Handle))
	{
		if (IsValid(Voice->Comp))
		{
			Voice->Comp->SetPaused(bPaused);
			Transition(*Voice, bPaused ? EElysiumVoiceState::Paused : EElysiumVoiceState::Playing);
		}
	}
}

void UElysiumAudioSubsystem::Seek(FElysiumVoiceHandle Handle, float MediaOffsetSeconds)
{
	if (FElysiumAudioVoice* Voice = FindVoice(Handle))
	{
		if (IsValid(Voice->Comp))
		{
			Voice->Event.MediaOffsetSeconds = FMath::Max(MediaOffsetSeconds, 0.f);
			Voice->Comp->Play(Voice->Event.MediaOffsetSeconds);
			Transition(*Voice, EElysiumVoiceState::Playing);
		}
	}
}

void UElysiumAudioSubsystem::SetGain(FElysiumVoiceHandle Handle, float Gain, float FadeSeconds)
{
	if (FElysiumAudioVoice* Voice = FindVoice(Handle))
	{
		// The request ledger is authoritative even while decode is pending. Scheme stems are
		// submitted at zero and immediately assigned their state volume, often before realization.
		Voice->Request.Gain = Gain;
		if (IsValid(Voice->Comp))
		{
			const float TargetGain = OutputGain(Voice->Request);
			if (FadeSeconds <= 0.f)
			{
				// SetVolumeMultiplier accepts exact zero without changing play state.
				Voice->Comp->SetVolumeMultiplier(TargetGain);
			}
			else
			{
				// AdjustVolume(…, 0) means FadeOut in UE and would destroy a phase-locked stem.
				Voice->Comp->AdjustVolume(FadeSeconds,
					FMath::IsNearlyZero(TargetGain) ? 0.0001f : TargetGain);
			}
		}
	}
}

void UElysiumAudioSubsystem::SetPitch(FElysiumVoiceHandle Handle, float Pitch)
{
	if (FElysiumAudioVoice* Voice = FindVoice(Handle))
	{
		Voice->Request.Pitch = Pitch;
		if (IsValid(Voice->Comp))
		{
			Voice->Comp->SetPitchMultiplier(Pitch);
		}
	}
}

void UElysiumAudioSubsystem::SetVoiceLocation(FElysiumAudioVoiceHandle Handle, const FVector& Location)
{
	if (FElysiumAudioVoice* Voice = FindVoice(Handle))
	{
		Voice->Request.Placement.Location = Location;
		if (IsValid(Voice->Comp))
		{
			Voice->Comp->SetWorldLocation(Location);
		}
	}
}

void UElysiumAudioSubsystem::CancelOwner(const FElysiumAudioOwner& Owner, float FadeSeconds)
{
	TArray<FElysiumVoiceHandle> Matches;
	for (const FElysiumAudioVoice& Voice : Voices)
	{
		if (Voice.Request.Owner == Owner)
		{
			Matches.Add(Voice.Handle);
		}
	}
	for (FElysiumVoiceHandle Handle : Matches)
	{
		if (FadeSeconds > 0.f)
		{
			Stop(Handle, FadeSeconds);
		}
		else if (const int32 Index = Voices.IndexOfByPredicate(
			[Handle](const FElysiumAudioVoice& Item) { return Item.Handle == Handle; }); Index != INDEX_NONE)
		{
			CompleteAt(Index, EElysiumVoiceCompletion::OwnerCanceled);
		}
	}
}

void UElysiumAudioSubsystem::RetireMapEpoch(uint64 MapEpoch)
{
	for (int32 Index = Voices.Num() - 1; Index >= 0; --Index)
	{
		const FElysiumAudioVoice& Voice = Voices[Index];
		if (Voice.Request.Owner.MapEpoch == MapEpoch &&
			!EnumHasAnyFlags(Voice.Request.Routing, EElysiumAudioRouting::PersistAcrossTravel))
		{
			CompleteAt(Index, EElysiumVoiceCompletion::MapEpochRetired);
		}
	}
}

bool UElysiumAudioSubsystem::IsVoicePlaying(FElysiumAudioVoiceHandle Handle) const
{
	// This compatibility query is intentionally ledger-authoritative. Pending decode,
	// scheduled, virtual, and paused requests still occupy their owner's concurrency slot.
	return FindVoice(Handle) != nullptr;
}

bool UElysiumAudioSubsystem::IsMuted() const
{
	return CVarMute.GetValueOnGameThread() != 0;
}

float UElysiumAudioSubsystem::MasterGain() const
{
	if (IsMuted())
	{
		return 0.f;
	}
	if (GEngine)
	{
		if (const UElysiumUserSettings* Settings =
			Cast<UElysiumUserSettings>(GEngine->GetGameUserSettings()))
		{
			return Settings->MasterVolume;
		}
	}
	return 1.f;
}

float UElysiumAudioSubsystem::OutputGain(const FElysiumAudioRequest& Request) const
{
	float CategoryGain = 1.f;
	if (GEngine)
	{
		if (const UElysiumUserSettings* Settings =
			Cast<UElysiumUserSettings>(GEngine->GetGameUserSettings()))
		{
			CategoryGain = Settings->AudioVolume(Request.Category);
		}
	}
	return Request.Gain * MasterGain() * CategoryGain;
}

void UElysiumAudioSubsystem::SetMuted(bool bInMuted)
{
	if (bInMuted != IsMuted())
	{
		CVarMute->Set(bInMuted ? 1 : 0, ECVF_SetByConsole);
	}
}

void UElysiumAudioSubsystem::ApplyMasterGain()
{
	for (FElysiumAudioVoice& Voice : Voices)
	{
		if (Voice.Event.State != EElysiumVoiceState::Fading && IsValid(Voice.Comp))
		{
			// Exact-zero debug/user muting must not stop or complete the underlying request.
			Voice.Comp->SetVolumeMultiplier(OutputGain(Voice.Request));
		}
	}
}

void UElysiumAudioSubsystem::CompleteAt(int32 VoiceIndex, EElysiumVoiceCompletion Completion)
{
	if (!Voices.IsValidIndex(VoiceIndex))
	{
		return;
	}
	FElysiumAudioVoice& Voice = Voices[VoiceIndex];
	// A short line can be submitted, rendered and finished between two game frames, so the reading
	// is taken here too rather than only on the tick that sees the voice alive.
	ObserveRenderLatency(Voice);
	UAudioComponent* Comp = Voice.Comp.Get();
	EElysiumVoiceState TerminalState = EElysiumVoiceState::Canceled;
	if (Completion == EElysiumVoiceCompletion::NaturalEnd)
	{
		TerminalState = EElysiumVoiceState::Completed;
	}
	else if (Completion == EElysiumVoiceCompletion::DecodeFailed ||
		Completion == EElysiumVoiceCompletion::MissingSource ||
		Completion == EElysiumVoiceCompletion::ConcurrencyRejected ||
		Completion == EElysiumVoiceCompletion::PlaybackRejected ||
		Completion == EElysiumVoiceCompletion::DeadlineMiss ||
		Completion == EElysiumVoiceCompletion::Underflow)
	{
		TerminalState = EElysiumVoiceState::Failed;
	}
	if (TerminalState == EElysiumVoiceState::Failed)
	{
		UE_LOG(LogElysiumAudio, Warning,
			TEXT("voice failed completion=%d source='%s' owner='%s' epoch=%llu"),
			static_cast<int32>(Completion), *Voice.Event.ResolvedPath,
			*Voice.Request.Owner.StableId, Voice.Request.Owner.MapEpoch);
	}
	Transition(Voice, TerminalState, Completion);
	if (Comp)
	{
		Comp->OnAudioFinishedNative.RemoveAll(this);
		Comp->Stop();
		Comp->DestroyComponent();
	}
	const FElysiumVoiceHandle Handle = Voice.Handle;
	Voices.RemoveAtSwap(VoiceIndex, EAllowShrinking::No);
	ReleaseHandle(Handle);
}

void UElysiumAudioSubsystem::HandleAudioFinished(UAudioComponent* Component)
{
	const int32 Index = Voices.IndexOfByPredicate(
		[Component](const FElysiumAudioVoice& Voice) { return Voice.Comp.Get() == Component; });
	if (Index != INDEX_NONE)
	{
		const EElysiumVoiceCompletion Completion =
			Voices[Index].Event.State == EElysiumVoiceState::Fading
				? Voices[Index].Event.Completion
				: EElysiumVoiceCompletion::NaturalEnd;
		CompleteAt(Index, Completion);
	}
}

void UElysiumAudioSubsystem::TickAudio(float /*DeltaSeconds*/)
{
	const double Now = AudioClock();
	if (!bLatencyQueried && GetWorld() != nullptr)
	{
		// Deferred to the first tick rather than done in Initialize: a GameInstance subsystem comes
		// up before there is a world, and the device is reached through the world.
		RefreshOutputLatency();
	}
	for (int32 Index = Voices.Num() - 1; Index >= 0; --Index)
	{
		FElysiumAudioVoice& Voice = Voices[Index];
		ObserveRenderLatency(Voice);
		if (Voice.Event.State == EElysiumVoiceState::PendingDecode ||
			Voice.Event.State == EElysiumVoiceState::Scheduled)
		{
			if (Voice.Event.State == EElysiumVoiceState::Scheduled &&
				Now >= Voice.Event.ScheduledAudioClock && IsValid(Voice.Comp))
			{
				FElysiumAudioVoice& MutableVoice = Voices[Index];
				MutableVoice.Comp->SetPaused(false);
				Transition(MutableVoice, EElysiumVoiceState::Playing);
				EmitGameplayNoise(MutableVoice);
			}
			continue;
		}
		if (!IsValid(Voice.Comp))
		{
			CompleteAt(Index, EElysiumVoiceCompletion::NaturalEnd);
			continue;
		}
		else if (!Voice.Request.bLooping &&
			Voice.Event.State == EElysiumVoiceState::Playing &&
			!Voice.Comp->IsPlaying())
		{
			// UE suppresses OnAudioFinished when an active sound fails to start (for example
			// due to an engine-level playback rejection). Give a normal completion callback
			// one game-thread grace window, then retire the otherwise orphaned request.
			if (Voice.InactiveSinceAudioClock < 0.0)
			{
				Voice.InactiveSinceAudioClock = Now;
			}
			else if (Now - Voice.InactiveSinceAudioClock >= 0.1)
			{
				CompleteAt(Index, EElysiumVoiceCompletion::PlaybackRejected);
				continue;
			}
		}
		else
		{
			Voice.InactiveSinceAudioClock = -1.0;
		}
		if (Voice.DestroyAudioClock >= 0.0 && Now >= Voice.DestroyAudioClock)
		{
			CompleteAt(Index, Voice.Event.Completion);
		}
	}
}

void UElysiumAudioSubsystem::StopAllVoices()
{
	for (int32 Index = Voices.Num() - 1; Index >= 0; --Index)
	{
		CompleteAt(Index, EElysiumVoiceCompletion::Stopped);
	}
}

FElysiumAudioVoiceHandle UElysiumAudioSubsystem::PlayVoice(
	const FString& Rel, const FElysiumPlayParams& Params)
{
	FElysiumAudioRequest Request;
	Request.Source = FElysiumAudioSource::Path(Rel);
	Request.Gain = Params.Volume;
	Request.Pitch = Params.Pitch;
	Request.bLooping = Params.bLooping;
	Request.Placement.bSpatialized = Params.b3D;
	Request.Placement.Location = Params.Location;
	Request.Placement.AttachTo = Params.AttachTo;
	Request.AttenuationRadiusCm = Params.AttenuationRadiusCm;
	Request.AttenuationOverride = Params.AttenuationOverride;
	Request.FadeInSeconds = Params.FadeInSeconds;
	Request.StartOffsetSeconds = Params.StartTimeSeconds;
	return Submit(Request);
}

UAudioComponent* UElysiumAudioSubsystem::PreviewSound2D(
	const FString& Rel, float VolumeMultiplier, float PitchMultiplier)
{
	FElysiumAudioRequest Request;
	Request.Source = FElysiumAudioSource::Path(Rel);
	Request.Owner.Kind = EElysiumAudioOwnerKind::Application;
	Request.Owner.StableId = TEXT("debug.preview");
	Request.Category = EElysiumAudioCategory::Ui;
	Request.Placement.bSpatialized = false;
	Request.Gain = VolumeMultiplier;
	Request.Pitch = PitchMultiplier;
	const FElysiumVoiceHandle Handle = Submit(Request);
	if (FElysiumAudioVoice* Voice = FindVoice(Handle))
	{
		return Voice->Comp.Get();
	}
	return nullptr;
}
