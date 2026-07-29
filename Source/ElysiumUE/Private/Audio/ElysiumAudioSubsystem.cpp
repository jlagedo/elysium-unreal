#include "ElysiumAudioSubsystem.h"

#include "ElysiumContentPaths.h"

#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWaveProcedural.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumAudio, Log, All);

// The global mute gate. Defaults to muted: audio is opt-in per session, flipped from the Cog Audio
// window's Mute checkbox (the same state) or from here. A muted voice keeps playing at zero gain, so
// the ambient bed / music stems stay in sync and unmuting rejoins the mix mid-stream.
static TAutoConsoleVariable<int32> CVarMute(
	TEXT("elysium.Mute"), 1,
	TEXT("Global audio mute: 1 = every voice at zero gain (default), 0 = audible."),
	ECVF_Default);

namespace
{
	// Build the sphere attenuation for a 3D voice from a VtMB radius (already in cm). NaturalSound is
	// the dB-based curve closest to Source's SNDLVL distance model (audio_pipeline.md §3/§11); the
	// falloff runs from the source (inner radius 0) out to the radius, where it reaches ~ -60 dB.
	FSoundAttenuationSettings MakeSphereAttenuation(float RadiusCm)
	{
		FSoundAttenuationSettings Att;
		Att.bAttenuate = true;
		Att.bSpatialize = true;
		Att.AttenuationShape = EAttenuationShape::Sphere;
		Att.AttenuationShapeExtents = FVector::ZeroVector;   // point source (inner radius 0)
		Att.FalloffDistance = FMath::Max(RadiusCm, 1.f);
		Att.DistanceAlgorithm = EAttenuationDistanceModel::NaturalSound;
		Att.dBAttenuationAtMax = -60.f;
		return Att;
	}
}

void UElysiumAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	IConsoleManager& CM = IConsoleManager::Get();

	// `elysium.Mute` is the mute state itself, so a console flip has to reach the voices already
	// running. Weak-bound: the callback drops out with the subsystem, and Deinitialize clears it.
	CVarMute->SetOnChangedCallback(FConsoleVariableDelegate::CreateWeakLambda(this,
		[this](IConsoleVariable* Var)
		{
			ApplyMasterGain();
			UE_LOG(LogElysiumAudio, Display, TEXT("audio %s"), Var->GetInt() != 0 ? TEXT("muted") : TEXT("unmuted"));
		}));

	// elysium.playsound <rel> — decode the WAV/MP3 under out/sound/<rel> and preview it 2D. The
	// scriptable echo of the Cog Audio window's Play button (F1-first: the window is primary).
	ConsoleObjects.Add(CM.RegisterConsoleCommand(
		TEXT("elysium.playsound"),
		TEXT("elysium.playsound <relpath> — decode & play a WAV/MP3 under out/sound/ (e.g. Environmental/Fire/Fire_Roaring.wav, radio/radio_loop_1.mp3)"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (Args.Num() == 0)
			{
				UE_LOG(LogElysiumAudio, Display, TEXT("usage: elysium.playsound <relpath under out/sound/>"));
				return;
			}
			const FString Rel = FString::Join(Args, TEXT(" "));   // tolerate spaces in the path
			if (PreviewSound2D(Rel) == nullptr)
			{
				const FElysiumSoundInfo* Info = DecodeResults.Find(Rel);
				UE_LOG(LogElysiumAudio, Warning, TEXT("playsound failed: %s (%s)"), *Rel,
					Info ? *Info->Error : TEXT("file not found"));
			}
		}),
		ECVF_Cheat));

	// elysium.sound_info <rel> — decode-only; print the format/metadata (no playback).
	ConsoleObjects.Add(CM.RegisterConsoleCommand(
		TEXT("elysium.sound_info"),
		TEXT("elysium.sound_info <relpath> — decode a WAV/MP3 under out/sound/ and print its format/metadata"),
		FConsoleCommandWithArgsDelegate::CreateWeakLambda(this, [this](const TArray<FString>& Args)
		{
			if (Args.Num() == 0)
			{
				UE_LOG(LogElysiumAudio, Display, TEXT("usage: elysium.sound_info <relpath under out/sound/>"));
				return;
			}
			const FString Rel = FString::Join(Args, TEXT(" "));
			const FElysiumSoundInfo* Info = Probe(Rel);
			if (Info == nullptr)
			{
				UE_LOG(LogElysiumAudio, Warning, TEXT("sound_info: %s not found under out/sound/"), *Rel);
				return;
			}
			if (!Info->Error.IsEmpty())
			{
				UE_LOG(LogElysiumAudio, Warning, TEXT("sound_info: %s — decode error: %s"), *Rel, *Info->Error);
				return;
			}
			UE_LOG(LogElysiumAudio, Display,
				TEXT("%s: %s (raw 0x%04X) · %d ch · %d Hz · %d-bit · %lld frames · %.3f s · decoded %.2f ms"),
				*Rel, *Info->FormatName(), Info->RawFormatTag, Info->Channels, Info->SampleRate,
				Info->BitsPerSample, Info->FrameCount, Info->DurationSeconds, Info->DecodeMilliseconds);
		}),
		ECVF_Cheat));
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
	FElysiumSoundCache::FlushAll();

	Super::Deinitialize();
}

const FElysiumSoundCache::FDecoded* UElysiumAudioSubsystem::LoadAndRecord(const FString& Rel)
{
	const FElysiumSoundCache::FDecoded* Decoded =
		FElysiumSoundCache::LoadSoundDecoded(FElysiumContentPaths::SoundDir(), Rel);
	if (Decoded != nullptr)
	{
		DecodeResults.Add(Rel, Decoded->Info);
	}
	return Decoded;
}

const FElysiumSoundInfo* UElysiumAudioSubsystem::Probe(const FString& Rel)
{
	const FElysiumSoundCache::FDecoded* Decoded = LoadAndRecord(Rel);
	return Decoded ? &Decoded->Info : nullptr;
}

FElysiumAudioVoice* UElysiumAudioSubsystem::FindVoice(FElysiumAudioVoiceHandle Handle)
{
	if (!Handle.IsValid())
	{
		return nullptr;
	}
	return Voices.FindByPredicate([Handle](const FElysiumAudioVoice& V) { return V.Id == Handle.Id; });
}

FElysiumAudioVoiceHandle UElysiumAudioSubsystem::PlayVoice(const FString& Rel, const FElysiumPlayParams& Params)
{
	const FElysiumSoundCache::FDecoded* Decoded = LoadAndRecord(Rel);
	if (Decoded == nullptr || !Decoded->Info.Error.IsEmpty())
	{
		return FElysiumAudioVoiceHandle::Invalid();
	}

	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return FElysiumAudioVoiceHandle::Invalid();
	}

	USoundWaveProcedural* Wave = FElysiumSoundCache::MakeWave(*Decoded, Params.bLooping);
	if (Wave == nullptr)
	{
		return FElysiumAudioVoiceHandle::Invalid();
	}

	// Start silent when fading in, then ramp to the target volume; otherwise start at volume. Every
	// component-level volume below is the requested volume times the master gain — Voice.Volume keeps
	// the *requested* value, so a mute flip is one re-apply and nothing loses its own level.
	const float Gain = MasterGain();
	const float StartVol = (Params.FadeInSeconds > 0.f ? 0.f : Params.Volume) * Gain;

	UAudioComponent* Comp = nullptr;
	if (!Params.b3D)
	{
		// Non-spatialized bed (music stem / "everywhere" ambient_generic): full volume everywhere.
		Comp = UGameplayStatics::SpawnSound2D(World, Wave, StartVol, Params.Pitch, Params.StartTimeSeconds,
			/*ConcurrencySettings*/ nullptr, /*bPersistAcrossLevelTransition*/ false, /*bAutoDestroy*/ false);
	}
	else if (Params.AttachTo != nullptr)
	{
		// Parent to a mover's component (SourceEntityName) so the sound tracks it.
		Comp = UGameplayStatics::SpawnSoundAttached(Wave, Params.AttachTo, NAME_None, FVector::ZeroVector,
			FRotator::ZeroRotator, EAttachLocation::SnapToTarget, /*bStopWhenAttachedToDestroyed*/ true,
			StartVol, Params.Pitch, Params.StartTimeSeconds, nullptr, nullptr, /*bAutoDestroy*/ false);
	}
	else
	{
		Comp = UGameplayStatics::SpawnSoundAtLocation(World, Wave, Params.Location, FRotator::ZeroRotator,
			StartVol, Params.Pitch, Params.StartTimeSeconds, nullptr, nullptr, /*bAutoDestroy*/ false);
	}

	if (Comp == nullptr)
	{
		return FElysiumAudioVoiceHandle::Invalid();
	}

	if (Params.b3D && Params.AttenuationRadiusCm > 0.f)
	{
		Comp->AdjustAttenuation(MakeSphereAttenuation(Params.AttenuationRadiusCm));
	}
	if (Params.FadeInSeconds > 0.f)
	{
		Comp->AdjustVolume(Params.FadeInSeconds, Params.Volume * Gain);
	}

	const double Now = World->GetTimeSeconds();

	FElysiumAudioVoice Voice;
	Voice.Id = NextVoiceId++;
	Voice.Comp = Comp;
	Voice.Wave = Wave;
	Voice.bLooping = Params.bLooping;
	Voice.b3D = Params.b3D;
	Voice.Rel = Rel;
	Voice.Volume = Params.Volume;
	Voice.Pitch = Params.Pitch;
	// A one-shot self-reaps a hair past its natural length (the procedural voice underflows to
	// silence but never ends itself); a loop never auto-expires.
	const double Remaining = FMath::Max(0.f, Decoded->Info.DurationSeconds - Params.StartTimeSeconds);
	Voice.ExpireWorldTime = Params.bLooping ? -1.0 : (Now + Remaining + 0.25);
	Voices.Add(MoveTemp(Voice));

	return FElysiumAudioVoiceHandle{ NextVoiceId - 1 };
}

void UElysiumAudioSubsystem::StopVoice(FElysiumAudioVoiceHandle Handle, float FadeSeconds)
{
	FElysiumAudioVoice* Voice = FindVoice(Handle);
	if (Voice == nullptr)
	{
		return;
	}
	UAudioComponent* Comp = Voice->Comp.Get();
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	if (FadeSeconds > 0.f && Comp != nullptr)
	{
		Comp->FadeOut(FadeSeconds, 0.f);
		Voice->DestroyWorldTime = Now + FadeSeconds + 0.1;   // reap once the fade has finished
	}
	else
	{
		if (Comp != nullptr)
		{
			Comp->Stop();
		}
		Voice->DestroyWorldTime = Now;   // reaped on the next TickAudio
	}
}

void UElysiumAudioSubsystem::SetVoiceVolume(FElysiumAudioVoiceHandle Handle, float Volume, float FadeSeconds)
{
	FElysiumAudioVoice* Voice = FindVoice(Handle);
	if (Voice == nullptr || !IsValid(Voice->Comp))
	{
		return;
	}
	Voice->Volume = Volume;
	// 0 duration == set instantly.
	Voice->Comp->AdjustVolume(FMath::Max(FadeSeconds, 0.f), Volume * MasterGain());
}

bool UElysiumAudioSubsystem::IsMuted() const
{
	return CVarMute.GetValueOnGameThread() != 0;
}

void UElysiumAudioSubsystem::SetMuted(bool bInMuted)
{
	if (bInMuted == IsMuted())
	{
		return;
	}
	// The cvar is the state; its change callback re-applies the gain to the live pool. Set at console
	// priority — this is a user action, and a lower priority is silently dropped once the cvar has
	// been touched from the console.
	CVarMute->Set(bInMuted ? 1 : 0, ECVF_SetByConsole);
}

void UElysiumAudioSubsystem::ApplyMasterGain()
{
	const float Gain = MasterGain();
	for (FElysiumAudioVoice& V : Voices)
	{
		if (V.DestroyWorldTime >= 0.0 || !IsValid(V.Comp))
		{
			continue;   // already fading out toward a reap — leave it dying
		}
		V.Comp->AdjustVolume(0.f, V.Volume * Gain);
	}
}

void UElysiumAudioSubsystem::SetVoicePitch(FElysiumAudioVoiceHandle Handle, float Pitch)
{
	FElysiumAudioVoice* Voice = FindVoice(Handle);
	if (Voice != nullptr && IsValid(Voice->Comp))
	{
		Voice->Pitch = Pitch;
		Voice->Comp->SetPitchMultiplier(Pitch);
	}
}

void UElysiumAudioSubsystem::SetVoiceLocation(FElysiumAudioVoiceHandle Handle, const FVector& Location)
{
	FElysiumAudioVoice* Voice = FindVoice(Handle);
	if (Voice != nullptr && IsValid(Voice->Comp))
	{
		Voice->Comp->SetWorldLocation(Location);
	}
}

bool UElysiumAudioSubsystem::IsVoicePlaying(FElysiumAudioVoiceHandle Handle) const
{
	const FElysiumAudioVoice* Voice = Voices.FindByPredicate(
		[Handle](const FElysiumAudioVoice& V) { return V.Id == Handle.Id; });
	return Voice != nullptr && IsValid(Voice->Comp) && Voice->Comp->IsPlaying();
}

void UElysiumAudioSubsystem::TickAudio(float /*DeltaSeconds*/)
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();

	for (int32 i = Voices.Num() - 1; i >= 0; --i)
	{
		FElysiumAudioVoice& V = Voices[i];
		UAudioComponent* Comp = V.Comp.Get();

		const bool bGone = (Comp == nullptr);
		const bool bStopFadeDone = (V.DestroyWorldTime >= 0.0 && Now >= V.DestroyWorldTime);
		const bool bOneShotDone = (!V.bLooping && V.ExpireWorldTime >= 0.0 && Now >= V.ExpireWorldTime);

		if (bGone || bStopFadeDone || bOneShotDone)
		{
			if (Comp != nullptr)
			{
				// Spawned with bAutoDestroy=false, so destroy it explicitly (else it lingers registered).
				Comp->Stop();
				Comp->DestroyComponent();
			}
			Voices.RemoveAtSwap(i, EAllowShrinking::No);
		}
	}
}

void UElysiumAudioSubsystem::StopAllVoices()
{
	for (FElysiumAudioVoice& V : Voices)
	{
		if (UAudioComponent* Comp = V.Comp.Get())
		{
			Comp->Stop();
			Comp->DestroyComponent();
		}
	}
	Voices.Reset();
}

UAudioComponent* UElysiumAudioSubsystem::PreviewSound2D(const FString& Rel, float VolumeMultiplier, float PitchMultiplier)
{
	FElysiumPlayParams Params;
	Params.Volume = VolumeMultiplier;
	Params.Pitch = PitchMultiplier;
	Params.bLooping = false;
	Params.b3D = false;
	const FElysiumAudioVoiceHandle Handle = PlayVoice(Rel, Params);
	FElysiumAudioVoice* Voice = FindVoice(Handle);
	return Voice ? Voice->Comp.Get() : nullptr;
}
