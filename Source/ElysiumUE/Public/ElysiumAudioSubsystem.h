#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ElysiumSoundCache.h"
#include "ElysiumAudioSubsystem.generated.h"

class UAudioComponent;
class USceneComponent;
class USoundWaveProcedural;
class IConsoleObject;

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

struct FElysiumAudioRequestSnapshot
{
	FElysiumVoiceHandle Handle;
	FElysiumAudioRequest Request;
	FElysiumVoiceEvent Event;
};

// Compatibility shape for callers while each source integration is migrated to Submit. It is
// deliberately translated at the seam and never enters the ledger as a second request type.
struct FElysiumPlayParams
{
	float Volume = 1.f;
	float Pitch = 1.f;
	bool bLooping = false;
	bool b3D = true;
	float AttenuationRadiusCm = 0.f;
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
	UPROPERTY() TObjectPtr<USoundWaveProcedural> Wave = nullptr;

	FElysiumVoiceHandle Handle;
	FElysiumAudioRequest Request;
	FElysiumVoiceEvent Event;
	double DestroyAudioClock = -1.0;
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
	bool IsCatalogReady() const { return bCatalogReady; }
	bool IsReadyForMapActivation() const { return bCatalogReady && PendingPrefetches == 0; }
	const FString& CatalogError() const { return CatalogLoadError; }

	const TArray<FElysiumAudioRequestSnapshot>& RequestSnapshots() const { return Snapshots; }
	FElysiumVoiceEventDelegate& OnVoiceEvent() { return VoiceEvents; }
	FElysiumNoiseEventDelegate& OnGameplayNoise() { return NoiseEvents; }

	static FString NormalizeSourcePath(const FString& AuthoredPath);
	static FString ResolveSourcePath(const FElysiumAudioSource& Source);

	const FElysiumSoundInfo* Probe(const FString& Rel);
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

	const TMap<FString, FElysiumSoundInfo>& Results() const { return DecodeResults; }
	const TArray<FElysiumAudioVoice>& ActiveVoices() const { return Voices; }

private:
	FElysiumSoundCache::FDecodedPtr LoadAndRecord(const FString& Rel);
	FElysiumAudioVoice* FindVoice(FElysiumVoiceHandle Handle);
	const FElysiumAudioVoice* FindVoice(FElysiumVoiceHandle Handle) const;
	FElysiumVoiceHandle AllocateHandle();
	void ReleaseHandle(FElysiumVoiceHandle Handle);
	void Transition(FElysiumAudioVoice& Voice, EElysiumVoiceState State,
		EElysiumVoiceCompletion Completion = EElysiumVoiceCompletion::None);
	void CompleteAt(int32 VoiceIndex, EElysiumVoiceCompletion Completion);
	void ApplyMasterGain();
	double AudioClock() const;
	void HandleAudioFinished(UAudioComponent* Component);
	void BeginCatalogLoad();
	float OutputGain(const FElysiumAudioRequest& Request) const;
	void RealizeVoice(FElysiumVoiceHandle Handle, FElysiumSoundCache::FDecodedPtr Decoded);
	void EmitGameplayNoise(const FElysiumAudioVoice& Voice);

	TMap<FString, FElysiumSoundInfo> DecodeResults;

	UPROPERTY()
	TArray<FElysiumAudioVoice> Voices;

	TArray<uint32> SlotGenerations;
	TArray<uint32> FreeSlots;
	TArray<FElysiumAudioRequestSnapshot> Snapshots;
	FElysiumVoiceEventDelegate VoiceEvents;
	FElysiumNoiseEventDelegate NoiseEvents;
	TArray<IConsoleObject*> ConsoleObjects;
	bool bCatalogReady = false;
	int32 PendingPrefetches = 0;
	FString CatalogLoadError;
};
