#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ElysiumAudioLatency.h"
#include "ElysiumAudioSubsystem.generated.h"

class UAudioComponent;
class USceneComponent;
class USoundWave;
class IConsoleObject;
// A request may carry a whole authored falloff instead of a radius (the body-sound path builds one
// from a Source sound level). Held by shared pointer to an INCOMPLETE type on purpose: the settings
// struct drags `Sound/SoundAttenuation.h` and its generated header behind it, and this header is
// reached from `ElysiumWorldServices.h` by most of the runtime.
struct FSoundAttenuationSettings;

enum class EElysiumAudioSourceDomain : uint8
{
	DirectPath,
	Character,
	Openable,
	Switch,
	Computer,
	Weapon,
	Surface,
	Item,
	Discipline,
	Whisper,
	Radio,
	News,
};

enum class EElysiumAudioCategory : uint8
{
	Auto,
	Master,
	Music,
	Dialogue,
	Ambience,
	Sfx,
	Ui,
};

enum class EElysiumAudioOwnerKind : uint8
{
	Application,
	MapEntity,
	Scene,
	DialogueSession,
	GameplaySystem,
};

enum class EElysiumVoiceState : uint8
{
	PendingDecode,
	Scheduled,
	Playing,
	Virtual,
	Paused,
	Fading,
	Completed,
	Failed,
	Canceled,
};

enum class EElysiumVoiceCompletion : uint8
{
	None,
	NaturalEnd,
	Stopped,
	OwnerCanceled,
	MapEpochRetired,
	DecodeFailed,
	MissingSource,
	ConcurrencyRejected,
	PlaybackRejected,
	DeadlineMiss,
	Underflow,
};

enum class EElysiumAudioRouting : uint16
{
	None = 0,
	Dry = 1 << 0,
	NoPause = 1 << 1,
	NoVoiceDuck = 1 << 2,
	NoGameplayNoise = 1 << 3,
	PersistAcrossTravel = 1 << 4,
	Occlusion = 1 << 5,
};
ENUM_CLASS_FLAGS(EElysiumAudioRouting);

struct FElysiumAudioSource
{
	EElysiumAudioSourceDomain Domain = EElysiumAudioSourceDomain::DirectPath;
	FString EventId;
	FString LogicalPath;

	static FElysiumAudioSource Path(const FString& InPath)
	{
		FElysiumAudioSource Out;
		Out.LogicalPath = InPath;
		return Out;
	}

	static FElysiumAudioSource Event(EElysiumAudioSourceDomain InDomain, const FString& InEventId)
	{
		FElysiumAudioSource Out;
		Out.Domain = InDomain;
		Out.EventId = InEventId;
		return Out;
	}
};

struct FElysiumAudioOwner
{
	EElysiumAudioOwnerKind Kind = EElysiumAudioOwnerKind::Application;
	FString StableId;
	uint64 MapEpoch = 0;

	bool operator==(const FElysiumAudioOwner& Other) const
	{
		return Kind == Other.Kind && StableId == Other.StableId && MapEpoch == Other.MapEpoch;
	}
};

struct FElysiumNoiseEvent
{
	FString SemanticEventId;
	FVector Origin = FVector::ZeroVector;
	FString InstigatorId;
	float AuthoredRadiusCm = 0.f;
	uint64 MapEpoch = 0;
};

struct FElysiumAudioPlacement
{
	bool bSpatialized = true;
	FVector Location = FVector::ZeroVector;
	// Components never cross the public request boundary as ownership. This is a weak placement
	// hint resolved only while realizing the request on the game thread.
	TWeakObjectPtr<USceneComponent> AttachTo;
};

struct FElysiumAudioRequest
{
	FElysiumAudioSource Source;
	FElysiumAudioOwner Owner;
	EElysiumAudioCategory Category = EElysiumAudioCategory::Auto;
	FElysiumAudioPlacement Placement;
	float Gain = 1.f;
	float Pitch = 1.f;
	float AttenuationRadiusCm = 0.f;
	// The whole falloff, when the producer knows it. Wins over `AttenuationRadiusCm`: a caller that
	// authored a curve is not asking for the sphere the radius would build. Shared rather than
	// held by value because one attenuation is reused by every step on a surface and the request
	// is copied through the ledger.
	TSharedPtr<const FSoundAttenuationSettings> AttenuationOverride;
	bool bLooping = false;
	float StartOffsetSeconds = 0.f;
	float FadeInSeconds = 0.f;
	float FadeOutSeconds = 0.f;
	double ScheduledAudioClock = -1.0;
	EElysiumAudioRouting Routing = EElysiumAudioRouting::None;
	int32 Priority = 0;
	FName ConcurrencyKey;
	TOptional<FElysiumNoiseEvent> GameplayNoise;
};

// Slot is a reusable pool location; Generation changes before that slot can be handed out again.
// A stale handle therefore cannot address a later voice.
struct FElysiumVoiceHandle
{
	uint32 Slot = MAX_uint32;
	uint32 Generation = 0;

	bool IsValid() const { return Slot != MAX_uint32 && Generation != 0; }
	static FElysiumVoiceHandle Invalid() { return {}; }
	bool operator==(const FElysiumVoiceHandle& Other) const
	{
		return Slot == Other.Slot && Generation == Other.Generation;
	}
	bool operator!=(const FElysiumVoiceHandle& Other) const { return !(*this == Other); }
};

FORCEINLINE uint32 GetTypeHash(const FElysiumVoiceHandle& Handle)
{
	return HashCombine(::GetTypeHash(Handle.Slot), ::GetTypeHash(Handle.Generation));
}

using FElysiumAudioVoiceHandle = FElysiumVoiceHandle;

struct FElysiumVoiceEvent
{
	FElysiumVoiceHandle Handle;
	EElysiumVoiceState State = EElysiumVoiceState::PendingDecode;
	FString ResolvedPath;
	double ScheduledAudioClock = -1.0;
	float MediaOffsetSeconds = 0.f;
	float DurationSeconds = 0.f;
	EElysiumVoiceCompletion Completion = EElysiumVoiceCompletion::None;
};

// Compatibility shape for callers that still submit through PlayVoice. It is
// deliberately translated at the seam and never enters the ledger as a second request type.
struct FElysiumPlayParams
{
	float Volume = 1.f;
	float Pitch = 1.f;
	bool bLooping = false;
	bool b3D = true;
	float AttenuationRadiusCm = 0.f;
	// The `FElysiumAudioRequest` field of the same name, reachable from the compatibility path the
	// body-sound seam is built on.
	TSharedPtr<const FSoundAttenuationSettings> AttenuationOverride;
	float FadeInSeconds = 0.f;
	float StartTimeSeconds = 0.f;
	USceneComponent* AttachTo = nullptr;
	FVector Location = FVector::ZeroVector;
};

USTRUCT()
struct FElysiumAudioVoice
{
	GENERATED_BODY()

	UPROPERTY() TObjectPtr<UAudioComponent> Comp = nullptr;
	UPROPERTY() TObjectPtr<USoundWave> Wave = nullptr;
	// The loop body of a unit the bake split at its `smpl` region: `Wave` is the intro one-shot and
	// this is chained onto the same component when it finishes. Null for every other voice, which
	// is every voice but the three shipped intro+loop units.
	UPROPERTY() TObjectPtr<USoundWave> LoopWave = nullptr;

	FElysiumVoiceHandle Handle;
	FElysiumAudioRequest Request;
	FElysiumVoiceEvent Event;
	double DestroyAudioClock = -1.0;
	double InactiveSinceAudioClock = -1.0;
	// FPlatformTime::Seconds() at Submit, ahead of the async asset load.
	double SubmitSeconds = -1.0;
	// True once the intro half has handed over to the loop body, so the chained finish is not read
	// as the voice's natural end.
	bool bLoopChained = false;
};

// One row of the loaded/pending/retained ledger the audio debugger and `elysium_audio_state` read:
// what the resolver made of a key and what the engine did with the asset. Replaces the decode log
// the hand-rolled decoder wrote (AUD1.2) -- there is no decode to log, because Unreal's stream
// cache is the decoder.
struct FElysiumSoundAssetRow
{
	// The object path the key folded to, whether or not the bake carries it.
	FString ObjectPath;
	// Set when the bake split the unit into an intro one-shot and a looping body.
	FString LoopObjectPath;
	float DurationSeconds = 0.f;
	int32 Channels = 0;
	int32 SampleRate = 0;
	// The asset is in memory (a load completed and something still holds it).
	bool bLoaded = false;
	// An async load is outstanding.
	bool bPending = false;
	// `Prefetch` primed it: its compressed audio is retained for the session.
	bool bRetained = false;
	// The resolver found no asset for this key -- a diagnostic, never a negative cache.
	bool bMissing = false;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FElysiumVoiceEventDelegate, const FElysiumVoiceEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FElysiumNoiseEventDelegate, const FElysiumNoiseEvent&);

UCLASS()
class UElysiumAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	FElysiumVoiceHandle Submit(const FElysiumAudioRequest& Request);
	void Prefetch(const FElysiumAudioSource& Source);
	void Stop(FElysiumVoiceHandle Handle, float FadeSeconds = 0.f);
	void Pause(FElysiumVoiceHandle Handle, bool bPaused);
	void Seek(FElysiumVoiceHandle Handle, float MediaOffsetSeconds);
	void SetGain(FElysiumVoiceHandle Handle, float Gain, float FadeSeconds = 0.f);
	void SetPitch(FElysiumVoiceHandle Handle, float Pitch);
	void CancelOwner(const FElysiumAudioOwner& Owner, float FadeSeconds = 0.f);
	void RetireMapEpoch(uint64 MapEpoch);
	// Map activation waits on the outstanding prefetches only. AUD0.1 deleted the `catalog.json`
	// gate: readiness is the baked family's presence, and that is one asset-registry index built
	// once on first use (AUD1.2 -- it was a directory probe while the corpus was read loose).
	static bool IsSoundCorpusPresent();
	bool IsReadyForMapActivation() const { return PendingPrefetches == 0; }

	FElysiumVoiceEventDelegate& OnVoiceEvent() { return VoiceEvents; }
	FElysiumNoiseEventDelegate& OnGameplayNoise() { return NoiseEvents; }

	static FString NormalizeSourcePath(const FString& AuthoredPath);
	static FString ResolveSourcePath(const FElysiumAudioSource& Source);

	// Resolve one authored spelling and record what the bake carries for it, loading the asset
	// synchronously so the row answers with the wave's own duration/format. The audio debugger's
	// inspect button and `elysium.sound_info`; nothing on a gameplay path calls it.
	const FElysiumSoundAssetRow* Probe(const FString& Rel);
	UAudioComponent* PreviewSound2D(const FString& Rel, float VolumeMultiplier = 1.f, float PitchMultiplier = 1.f);

	FElysiumAudioVoiceHandle PlayVoice(const FString& Rel, const FElysiumPlayParams& Params);
	void StopVoice(FElysiumAudioVoiceHandle Handle, float FadeSeconds = 0.f) { Stop(Handle, FadeSeconds); }
	void SetVoiceVolume(FElysiumAudioVoiceHandle Handle, float Volume, float FadeSeconds = 0.f)
	{
		SetGain(Handle, Volume, FadeSeconds);
	}
	void SetVoicePitch(FElysiumAudioVoiceHandle Handle, float Pitch) { SetPitch(Handle, Pitch); }
	void SetVoiceLocation(FElysiumAudioVoiceHandle Handle, const FVector& Location);
	bool IsVoicePlaying(FElysiumAudioVoiceHandle Handle) const;

	bool IsMuted() const;
	void SetMuted(bool bInMuted);
	float MasterGain() const;

	void TickAudio(float DeltaSeconds);
	void StopAllVoices();

	// Every key this session resolved, with what happened to its asset. The registry the audio
	// debugger's table and `elysium_audio_state` read.
	const TMap<FString, FElysiumSoundAssetRow>& AssetRows() const { return SoundAssets; }
	int32 PendingLoadCount() const { return PendingPrefetches + PendingVoiceLoads; }
	const TArray<FElysiumAudioVoice>& ActiveVoices() const { return Voices; }

	// The audio render clock every voice's ScheduledAudioClock is stamped against, so a reader can
	// place a live voice inside its own media instead of inferring it from game time.
	double AudioClock() const;

	// The output path's latency, term by term, and the lead a cue must be scheduled with so
	// its first sample is heard at the authored instant. The device half is queried once and cached
	// (it only changes on a device swap); the submit → first-pull half is the owner-stamped
	// constant on `UElysiumAudioSettings` (AUD1.3 -- a plain wave has nowhere to hang a render
	// probe, and a primed asset feeds the mixer from memory).
	const FElysiumAudioLatency& OutputLatency() const { return Latency; }
	float OutputLeadSeconds() const { return Latency.Lead(); }
	// Re-ask the device. Called on the first voice of a session and by `elysium.audio_latency`.
	void RefreshOutputLatency();

private:
	// Record what the resolver made of a key, and what the engine now holds for it.
	FElysiumSoundAssetRow& RowFor(const FString& Rel);
	FElysiumAudioVoice* FindVoice(FElysiumVoiceHandle Handle);
	const FElysiumAudioVoice* FindVoice(FElysiumVoiceHandle Handle) const;
	FElysiumVoiceHandle AllocateHandle();
	void ReleaseHandle(FElysiumVoiceHandle Handle);
	void Transition(FElysiumAudioVoice& Voice, EElysiumVoiceState State,
		EElysiumVoiceCompletion Completion = EElysiumVoiceCompletion::None);
	void CompleteAt(int32 VoiceIndex, EElysiumVoiceCompletion Completion);
	void ApplyMasterGain();
	void HandleAudioFinished(UAudioComponent* Component);
	float OutputGain(const FElysiumAudioRequest& Request) const;
	void RealizeVoice(FElysiumVoiceHandle Handle, USoundWave* Wave, USoundWave* LoopWave);
	void EmitGameplayNoise(const FElysiumAudioVoice& Voice);

	TMap<FString, FElysiumSoundAssetRow> SoundAssets;
	// What `Prefetch` primed: the wave is held (so the load is not undone by a GC) and its
	// compressed audio retained (so the stream cache keeps its first chunk resident). Session
	// lifetime, released in Deinitialize -- a prefetch carries no owner or epoch to release on.
	UPROPERTY()
	TMap<FString, TObjectPtr<USoundWave>> RetainedWaves;

	UPROPERTY()
	TArray<FElysiumAudioVoice> Voices;

	TArray<uint32> SlotGenerations;
	TArray<uint32> FreeSlots;
	FDelegateHandle MapEpochRetiredHandle;
	FElysiumVoiceEventDelegate VoiceEvents;
	FElysiumNoiseEventDelegate NoiseEvents;
	TArray<IConsoleObject*> ConsoleObjects;
	int32 PendingPrefetches = 0;
	int32 PendingVoiceLoads = 0;

	FElysiumAudioLatency Latency;
	bool bLatencyQueried = false;
};
