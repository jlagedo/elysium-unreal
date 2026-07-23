#pragma once

#include "CoreMinimal.h"
#include "ElysiumAudioSubsystem.h"   // FElysiumAudioVoiceHandle

class UElysiumAudioSubsystem;

// P6.3 — the VtMB SoundScheme system (audio_pipeline.md §5/§6; decompiled CSoundScheme parser
// vampire.dll @0x1022a930). A scheme is one per-area unit that bundles: a looping ambient bed, an
// explore/combat/alert music triad, N polar-placed random one-shots, and a DSP room assignment.
// `ambient_soundscheme` entities point at a `sound/Schemes/*.txt` file and are crossfaded by Source
// I/O (FadeIn/FadeOut). This is VtMB's replacement for Source's env_soundscape.
//
// The data structs mirror the parser's fields + retail defaults exactly (the defaults an omitted key
// takes come straight from the decompile, §12). Volumes here are already normalized to 0..1 (the
// scheme files store 0–100). Runtime playback + the music state machine + the random scheduler live
// in FElysiumSoundSchemeManager (a plain-C++ object owned by AElysiumMapActor, ticked per frame).

// Which music stem the state machine is crossfading toward. VtMB gates this from combat scoring
// (Python world.SetSafeArea, P9); until that lands the state is debug/cvar-driven (elysium.MusicState).
enum class EElysiumMusicState : uint8
{
	Explore,   // safe area — the scheme's `Music` stem
	Combat,    // the `Combat` stem
	Alert,     // the `Alert` stem (rare; ~7 schemes carry one)
};

// One Music/Combat/Alert/Ambient block. bValid distinguishes "present in the file" from "absent".
struct FElysiumSchemeSound
{
	FString Filename;          // engine-relative, e.g. "music/Dark_Asia.mp3" / "environmental/.../x.wav"
	float   Volume = 0.5f;     // 0..1 (file 0–100; default 50 for these blocks)
	bool    bDry = true;       // route to the dry bus (skip room reverb) — default 1 for music blocks
	bool    bNoPause = false;  // keep playing while the game is paused
	bool    bValid = false;

	bool IsSet() const { return bValid && !Filename.IsEmpty(); }
};

// One RandomSound block: a positional one-shot placed on a polar ring around the scheme anchor.
struct FElysiumRandomSound
{
	FString Filename;
	float   Volume = 0.20f;    // default 20/100
	int32   Frequency = 10;    // spawn likelihood/rate weight (default 10)
	int32   PitchMin = 100;
	int32   PitchMax = 100;
	float   AudibleRadius = 1600.f;   // Source units (falloff reach)
	float   DistMin = 800.f;   // Source units, radial from the anchor
	float   DistMax = 1400.f;
	float   HeightMin = 20.f;  // Source units, vertical offset
	float   HeightMax = 20.f;
	float   AngleMin = 0.f;    // azimuth arc in degrees (wraps when Min > Max)
	float   AngleMax = 360.f;
	bool    bDry = false;
	bool    bNoPause = false;
};

// A whole parsed scheme.
struct FElysiumSoundScheme
{
	FString SourcePath;               // the .txt this was parsed from (debug)
	int32   RandomSoundCount = 2;     // max concurrent random one-shots (clamped 0..6)
	int32   RoomDSP = 0;              // DSP preset id (0 = neutral; not applied yet — reverb is P-later)
	FElysiumSchemeSound Music;        // explore stem
	FElysiumSchemeSound Combat;       // combat stem
	FElysiumSchemeSound Alert;        // alert stem
	FElysiumSchemeSound Ambient;      // the looping bed
	TArray<FElysiumRandomSound> RandomSounds;

	bool bParsed = false;

	// Parse a scheme .txt (Source KeyValues) from disk under out/sound/. Returns false (Out left
	// bParsed=false) if the file is missing or has no SoundScheme block. Applies the retail defaults.
	static bool ParseFile(const FString& AbsPath, FElysiumSoundScheme& Out);
};

// Owns the live scheme playback for one map: the active scheme's ambient bed + music-stem trio + the
// polar random-one-shot scheduler + the music state machine. Plain C++ (no UObject), owned by
// AElysiumMapActor, torn down on map unload (which stops its voices). Every method takes the audio
// subsystem explicitly so the manager holds no UObject pointer across frames.
class FElysiumSoundSchemeManager
{
public:
	FElysiumSoundSchemeManager() = default;
	~FElysiumSoundSchemeManager() = default;

	FElysiumSoundSchemeManager(const FElysiumSoundSchemeManager&) = delete;
	FElysiumSoundSchemeManager& operator=(const FElysiumSoundSchemeManager&) = delete;

	// Crossfade a scheme in as the active one (fading out whatever was active). SchemeRel is engine-
	// relative ("sound/Schemes/x.txt"); Anchor is the ambient_soundscheme entity origin (Unreal cm),
	// the centre of polar random placement. FadeSeconds 0 = instant (start_enabled at map load).
	void FadeInScheme(UElysiumAudioSubsystem* Audio, const FString& SchemeRel, const FVector& Anchor, float FadeSeconds);
	// Fade the named scheme out if it is the active one (else no-op).
	void FadeOutScheme(UElysiumAudioSubsystem* Audio, const FString& SchemeRel, float FadeSeconds);

	// Per-frame: advance the random scheduler + music crossfade. PlayerLoc is the listener (unused for
	// anchor-centred placement today, kept for a future player-relative mode). Safe with null Audio.
	void Tick(UElysiumAudioSubsystem* Audio, const FVector& PlayerLoc, float DeltaSeconds);

	// Stop every voice (map unload). Audio may be null (best-effort).
	void StopAll(UElysiumAudioSubsystem* Audio);

	// --- Music state machine ------------------------------------------------------------
	void SetMusicState(UElysiumAudioSubsystem* Audio, EElysiumMusicState NewState, float CrossfadeSeconds = -1.f);
	EElysiumMusicState MusicState() const { return CurrentMusicState; }

	// --- Debug read (Cog Sound Schemes window) ------------------------------------------
	bool HasActiveScheme() const { return Active.Scheme.bParsed; }
	const FElysiumSoundScheme& ActiveScheme() const { return Active.Scheme; }
	const FString& ActiveSchemeRel() const { return Active.SchemeRel; }
	FVector ActiveAnchor() const { return Active.Anchor; }
	int32 ActiveRandomVoiceCount() const { return Active.RandomVoices.Num(); }
	double SchemeElapsed() const { return Elapsed; }

private:
	// The live state for the (single) active scheme. A crossfade fades the previous scheme's voices
	// out (StopVoice with fade → the subsystem reaps them); only one scheme is "active" at a time,
	// which covers VtMB's mutually-exclusive area schemes (City/Underground/Sabbat on the tutorial).
	struct FActiveScheme
	{
		FElysiumSoundScheme Scheme;
		FString  SchemeRel;
		FVector  Anchor = FVector::ZeroVector;

		FElysiumAudioVoiceHandle Bed;      // ambient loop
		FElysiumAudioVoiceHandle Explore;  // music stems (all started together to stay phase-locked)
		FElysiumAudioVoiceHandle Combat;
		FElysiumAudioVoiceHandle Alert;

		TArray<FElysiumAudioVoiceHandle> RandomVoices;   // in-flight one-shots
		TArray<double> RandomNextTime;                   // per-RandomSound next-attempt time (Elapsed clock)

		bool IsSet() const { return Scheme.bParsed; }
	};

	// Fetch/parse (cached) a scheme by rel path. Returns null if the file is missing/empty.
	const FElysiumSoundScheme* LoadScheme(const FString& SchemeRel);

	void StartActiveVoices(UElysiumAudioSubsystem* Audio, float FadeSeconds);
	void ApplyMusicVolumes(UElysiumAudioSubsystem* Audio, float CrossfadeSeconds);
	void TickRandom(UElysiumAudioSubsystem* Audio, const FVector& PlayerLoc);

	TMap<FString, FElysiumSoundScheme> SchemeCache;
	FActiveScheme Active;
	EElysiumMusicState CurrentMusicState = EElysiumMusicState::Explore;
	double Elapsed = 0.0;   // manager clock (accumulated Dt), drives the random scheduler
};
