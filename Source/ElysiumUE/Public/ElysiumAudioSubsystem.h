#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "ElysiumSoundCache.h"
#include "ElysiumAudioSubsystem.generated.h"

class UAudioComponent;
class USceneComponent;
class USoundWaveProcedural;
class IConsoleObject;

// A stable ticket for one running voice (0 = invalid). PlayVoice mints one; the entity/scheme that
// started the voice keeps it to Stop/retarget/fade its own sound, decoupled from the UAudioComponent
// lifetime (a one-shot that already drained is a valid-but-dead handle: calls just no-op).
struct FElysiumAudioVoiceHandle
{
	int32 Id = 0;
	bool IsValid() const { return Id > 0; }
	static FElysiumAudioVoiceHandle Invalid() { return FElysiumAudioVoiceHandle{}; }
	bool operator==(const FElysiumAudioVoiceHandle& O) const { return Id == O.Id; }
};

// How to start a voice. Built by the ambient_generic entity (message/health/radius/pitch/spawnflags →
// these fields) and the SoundScheme manager (ambient bed, music stems, polar random one-shots).
struct FElysiumPlayParams
{
	float Volume = 1.f;          // 0..1 linear multiplier (VtMB health/10 or scheme Volume/100)
	float Pitch = 1.f;           // 1.0 == VtMB pitch 100
	bool  bLooping = false;      // re-queue on underflow (music beds, ambient, looping ambient_generic)
	bool  b3D = true;            // false == non-spatialized 2D (SF "everywhere" / music stems)
	float AttenuationRadiusCm = 0.f;   // sphere falloff distance in cm; 0 == no attenuation
	float FadeInSeconds = 0.f;   // >0 fades the voice up from silence over this long
	// Placement (b3D only): attach to AttachTo if set (SourceEntityName parenting — the sound follows
	// a mover), else play world-static at Location. Ignored when b3D is false.
	USceneComponent* AttachTo = nullptr;
	FVector Location = FVector::ZeroVector;
};

// One live voice — a UAudioComponent + the procedural wave feeding it, held GC-alive here (both are
// hard refs; bAutoDestroy is off so the subsystem owns teardown). Surfaced to the Cog Audio window.
USTRUCT()
struct FElysiumAudioVoice
{
	GENERATED_BODY()

	UPROPERTY() TObjectPtr<UAudioComponent> Comp = nullptr;
	UPROPERTY() TObjectPtr<USoundWaveProcedural> Wave = nullptr;

	int32  Id = 0;
	bool   bLooping = false;
	bool   b3D = false;
	double ExpireWorldTime = -1.0;   // one-shots: reap at/after this world-second (loop = -1, never)
	double DestroyWorldTime = -1.0;  // StopVoice(fade): reap after the fade completes (-1 = not stopping)
	FString Rel;                     // decoded path (debug label)
	float  Volume = 1.f;             // last-requested volume (pre-fade), for the debug table
	float  Pitch = 1.f;
};

// GameInstance-scoped audio (P6). Owns the decode-result registry the Cog Audio window and the
// elysium.* sound verbs share, mints procedural waves from FElysiumSoundCache, and owns the live
// voice pool that entity-driven playback (6.3 ambient_generic) and the SoundScheme manager play
// through. GI scope (like UElysiumGameStateSubsystem) so the decode cache survives map travel; the
// voice pool is per-play and reaped in TickAudio (driven by the map actor's Tick).
UCLASS()
class UElysiumAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Decode (or fetch cached) the WAV/MP3 at SoundDir/Rel and record its metadata in the registry,
	// without playing it. Returns the metadata (Error set on a decode failure), or nullptr if the
	// file is missing.
	const FElysiumSoundInfo* Probe(const FString& Rel);

	// Decode Rel, play it 2D (non-positional), and record the result. Returns the audio component
	// (retained so it can be stopped when the clip's queue drains), or nullptr on decode failure.
	// The Cog Audio window's Play button; a thin wrapper over PlayVoice.
	UAudioComponent* PreviewSound2D(const FString& Rel, float VolumeMultiplier = 1.f, float PitchMultiplier = 1.f);

	// --- Entity/scheme-driven voice pool (6.3) -----------------------------------------------
	// Decode Rel and start a voice per Params. Returns an invalid handle on a decode failure/missing
	// file. A looping voice plays until StopVoice; a one-shot self-reaps when its clip drains.
	FElysiumAudioVoiceHandle PlayVoice(const FString& Rel, const FElysiumPlayParams& Params);
	// Stop a voice (optionally fading out over FadeSeconds, then reap). No-op on a dead handle.
	void StopVoice(FElysiumAudioVoiceHandle Handle, float FadeSeconds = 0.f);
	// Ramp a running voice to Volume over FadeSeconds (0 = immediate). The scheme music state machine
	// crossfades its stems this way. No-op on a dead handle.
	void SetVoiceVolume(FElysiumAudioVoiceHandle Handle, float Volume, float FadeSeconds = 0.f);
	void SetVoicePitch(FElysiumAudioVoiceHandle Handle, float Pitch);
	// Move a world-static voice to a new location (a polar random one-shot is placed once at play;
	// this is for a voice that must track a point without a parent component).
	void SetVoiceLocation(FElysiumAudioVoiceHandle Handle, const FVector& Location);
	bool IsVoicePlaying(FElysiumAudioVoiceHandle Handle) const;

	// --- Global mute ---------------------------------------------------------------------------
	// One gate over every voice this subsystem owns (ambient_generic, scheme bed/music/randoms,
	// mover sounds, Cog previews) — nothing plays audio any other way. Muting scales every voice's
	// gain to zero *without* stopping it, so the scheme beds and music stems keep running in sync
	// and unmuting drops back into the mix mid-stream. Backed by `elysium.Mute`, which defaults to
	// 1: a fresh run is silent until audio is asked for.
	bool IsMuted() const;
	void SetMuted(bool bInMuted);
	// The multiplier every requested volume passes through (0 muted, 1 audible).
	float MasterGain() const { return IsMuted() ? 0.f : 1.f; }

	// Per-frame maintenance: reap drained one-shots and voices whose fade-out has completed. Called
	// from AElysiumMapActor::Tick (the one place the map is ticked); safe to call with no world.
	void TickAudio(float DeltaSeconds);

	// Stop and drop every voice (map unload) without touching the decode cache (that survives travel).
	void StopAllVoices();

	// The shared decode registry the Audio window renders: Rel -> last decode metadata.
	const TMap<FString, FElysiumSoundInfo>& Results() const { return DecodeResults; }
	// The live voice pool (Audio window "Live voices" table).
	const TArray<FElysiumAudioVoice>& ActiveVoices() const { return Voices; }

private:
	// Decode+record helper backing Probe/PreviewSound2D/PlayVoice.
	const FElysiumSoundCache::FDecoded* LoadAndRecord(const FString& Rel);
	// Find a voice by handle id (linear scan — the pool is small, tens at most).
	FElysiumAudioVoice* FindVoice(FElysiumAudioVoiceHandle Handle);
	// Re-apply MasterGain() to every live voice (a mute flip). Voices already fading out toward a
	// reap are skipped — re-setting their volume would make a dying sound audible again.
	void ApplyMasterGain();

	// Rel -> last decode metadata (the browser/verbs read this).
	TMap<FString, FElysiumSoundInfo> DecodeResults;

	// The live voice pool (UPROPERTY so its TObjectPtr members keep the components/waves GC-alive).
	UPROPERTY()
	TArray<FElysiumAudioVoice> Voices;

	int32 NextVoiceId = 1;

	TArray<IConsoleObject*> ConsoleObjects;   // elysium.playsound / elysium.sound_info
};
