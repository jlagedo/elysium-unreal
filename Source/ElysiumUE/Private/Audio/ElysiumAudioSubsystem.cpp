#include "ElysiumAudioSubsystem.h"

#include "ElysiumContentPaths.h"
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
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
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
	BeginCatalogLoad();

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
}

void UElysiumAudioSubsystem::BeginCatalogLoad()
{
	bCatalogReady = false;
	CatalogLoadError.Reset();
	const FString CatalogPath = FElysiumContentPaths::Root() / TEXT("audio/catalog.json");
	const TWeakObjectPtr<UElysiumAudioSubsystem> WeakThis(this);
	Async(EAsyncExecution::ThreadPool, [WeakThis, CatalogPath]()
	{
		FString Text;
		FString Error;
		if (!FFileHelper::LoadFileToString(Text, *CatalogPath))
		{
			// Older exports remain playable through direct resolution. Absence is a validation
			// warning, not an activation deadlock.
			Error = TEXT("catalog missing; using direct sound mirror resolution");
		}
		else
		{
			TSharedPtr<FJsonObject> Root;
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
			if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
			{
				Error = TEXT("catalog JSON is invalid; using direct sound mirror resolution");
			}
			else if (Root->GetIntegerField(TEXT("version")) != 1)
			{
				Error = TEXT("unsupported audio catalog version; using direct sound mirror resolution");
			}
		}
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Error = MoveTemp(Error)]()
		{
			if (UElysiumAudioSubsystem* Self = WeakThis.Get())
			{
				Self->CatalogLoadError = Error;
				Self->bCatalogReady = true;
				if (!Error.IsEmpty())
				{
					UE_LOG(LogElysiumAudio, Warning, TEXT("%s"), *Error);
				}
			}
		});
	});
}

void UElysiumAudioSubsystem::Deinitialize()
{
	CVarMute->SetOnChangedCallback(FConsoleVariableDelegate());
	for (IConsoleObject* Obj : ConsoleObjects)
	{
		IConsoleManager::Get().UnregisterConsoleObject(Obj);
	}
	ConsoleObjects.Empty();
	StopAllVoices();
	DecodeResults.Empty();
	Snapshots.Empty();
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
	if (FElysiumAudioRequestSnapshot* Snapshot = Snapshots.FindByPredicate(
		[&Voice](const FElysiumAudioRequestSnapshot& Item) { return Item.Handle == Voice.Handle; }))
	{
		Snapshot->Event = Voice.Event;
	}
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

	FElysiumAudioRequestSnapshot& Snapshot = Snapshots.AddDefaulted_GetRef();
	Snapshot.Handle = Voice.Handle;
	Snapshot.Request = Voice.Request;
	Snapshot.Event = Voice.Event;
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
	USoundConcurrency* Concurrency = LoadObject<USoundConcurrency>(nullptr,
		*FString::Printf(TEXT("/Game/VtMB/Audio/Concurrency_%s.Concurrency_%s"),
			CategoryName, CategoryName));
	if (!Request.Placement.bSpatialized)
	{
		Comp = UGameplayStatics::SpawnSound2D(World, Wave, StartGain, Request.Pitch,
			Request.StartOffsetSeconds, Concurrency,
			EnumHasAnyFlags(Request.Routing, EElysiumAudioRouting::PersistAcrossTravel), false);
	}
	else if (Attach)
	{
		Comp = UGameplayStatics::SpawnSoundAttached(Wave, Attach, NAME_None, FVector::ZeroVector,
			FRotator::ZeroRotator, EAttachLocation::SnapToTarget, true, StartGain, Request.Pitch,
			Request.StartOffsetSeconds, nullptr, Concurrency, false);
	}
	else
	{
		Comp = UGameplayStatics::SpawnSoundAtLocation(World, Wave, Request.Placement.Location,
			FRotator::ZeroRotator, StartGain, Request.Pitch, Request.StartOffsetSeconds,
			nullptr, Concurrency, false);
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

	if (Request.Placement.bSpatialized && Request.AttenuationRadiusCm > 0.f)
	{
		Comp->AdjustAttenuation(MakeSphereAttenuation(Request.AttenuationRadiusCm));
	}
	Comp->SoundClassOverride = LoadObject<USoundClass>(nullptr,
		*FString::Printf(TEXT("/Game/VtMB/Audio/SC_%s.SC_%s"), CategoryName, CategoryName));
	if (Request.FadeInSeconds > 0.f)
	{
		Comp->AdjustVolume(Request.FadeInSeconds, OutputGain(Voice.Request));
	}
	if (EnumHasAnyFlags(Request.Routing, EElysiumAudioRouting::NoPause))
	{
		Comp->bIsUISound = true;
	}

	Voice.Comp = Comp;
	Voice.Wave = Wave;
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
		if (IsValid(Voice->Comp))
		{
			Voice->Request.Gain = Gain;
			Voice->Comp->AdjustVolume(FMath::Max(FadeSeconds, 0.f), OutputGain(Voice->Request));
		}
	}
}

void UElysiumAudioSubsystem::SetPitch(FElysiumVoiceHandle Handle, float Pitch)
{
	if (FElysiumAudioVoice* Voice = FindVoice(Handle))
	{
		if (IsValid(Voice->Comp))
		{
			Voice->Request.Pitch = Pitch;
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
	const FElysiumAudioVoice* Voice = FindVoice(Handle);
	return Voice && IsValid(Voice->Comp) && Voice->Comp->IsPlaying();
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
			Voice.Comp->AdjustVolume(0.f, OutputGain(Voice.Request));
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
	UAudioComponent* Comp = Voice.Comp.Get();
	EElysiumVoiceState TerminalState = EElysiumVoiceState::Canceled;
	if (Completion == EElysiumVoiceCompletion::NaturalEnd)
	{
		TerminalState = EElysiumVoiceState::Completed;
	}
	else if (Completion == EElysiumVoiceCompletion::DecodeFailed ||
		Completion == EElysiumVoiceCompletion::MissingSource ||
		Completion == EElysiumVoiceCompletion::ConcurrencyRejected ||
		Completion == EElysiumVoiceCompletion::DeadlineMiss ||
		Completion == EElysiumVoiceCompletion::Underflow)
	{
		TerminalState = EElysiumVoiceState::Failed;
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
	for (int32 Index = Voices.Num() - 1; Index >= 0; --Index)
	{
		const FElysiumAudioVoice& Voice = Voices[Index];
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
		}
		else if (Voice.DestroyAudioClock >= 0.0 && Now >= Voice.DestroyAudioClock)
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
