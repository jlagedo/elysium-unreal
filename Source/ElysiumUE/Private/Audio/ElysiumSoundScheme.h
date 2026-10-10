#pragma once

#include "CoreMinimal.h"
#include "ElysiumAudioSubsystem.h"   // FElysiumAudioVoiceHandle
#include "ElysiumEntityHandle.h"

class UElysiumAudioSubsystem;
class FElysiumEntity;
class FElysiumEntityWorld;
struct IElysiumRetailSiteSink;

// The VtMB SoundScheme system (`docs/vtmb/audio_pipeline.md` §5/§6; decompiled CSoundScheme parser
// vampire.dll @0x1022a930). A scheme is one per-area unit that bundles: a looping ambient bed, an
// explore/combat/alert music triad, N polar-placed random one-shots, and a DSP room assignment.
// `ambient_soundscheme` entities point at a `sound/Schemes/*.txt` file and are crossfaded by Source
// I/O (FadeIn/FadeOut). This is VtMB's replacement for Source's env_soundscape.
//
// The data structs mirror the parser's fields + retail defaults exactly (the defaults an omitted key
// takes come straight from the decompile, §12). Volumes here are already normalized to 0..1 (the
// scheme files store 0–100). Runtime playback -- the manager's four track records, its retiring
// list and its registered-scheme list (`CSoundSchemeManager`, `DAT_10750d78`) -- plus the random
// scheduler live in FElysiumSoundSchemeManager (a plain-C++ object owned by AElysiumMapActor,
// ticked per frame).

// Which music stem the state machine is crossfading toward. VtMB gates this from combat scoring
// (Python world.SetSafeArea). The live state is debug/cvar-driven (`elysium.MusicState`).
enum class EElysiumMusicState : uint8
{
	Explore,   // safe area — the scheme's `Music` stem (retail state 1)
	Combat,    // the `Combat` stem (retail state 2)
	Alert,     // the `Alert` stem (rare; ~7 schemes carry one; retail state 3)
};

// One Music/Combat/Alert/Ambient block (a scheme record, stride `0x48`: `+0x00` name, `+0x40`
// volume, `+0x44` flags). bValid distinguishes "present in the file" from "absent".
struct FElysiumSchemeSound
{
	// The `Filename` as the file spells it (`Q_strncpy(..., 0x40)` in `FUN_1022a930`): separators and
	// case untouched, because the manager's same-name test (`FUN_10229430`) is `Q_strnicmp` on the
	// raw strings. The audio path resolver folds separators when the name is played.
	FString Filename;
	float   Volume = 0.5f;     // 0..1 (file 0–100; default 50 for these blocks)
	bool    bDry = true;       // route to the dry bus (skip room reverb) — default 1 for music blocks
	bool    bNoPause = false;  // keep playing while the game is paused
	bool    bValid = false;

	bool IsSet() const { return bValid && !Filename.IsEmpty(); }

	// The record's flags word (`+0x44`), as `FUN_1022a930` builds it: `0x800` when `Dry` is non-zero,
	// `0x2000` when `NoPause` is non-zero. What `FUN_10229430` copies into the track's `+0x54`.
	uint32 Flags() const { return (bDry ? 0x800u : 0u) | (bNoPause ? 0x2000u : 0u); }
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
	FElysiumSchemeSound Music;        // explore stem  (`+0x4f0`)
	FElysiumSchemeSound Combat;       // combat stem   (`+0x4a8`)
	FElysiumSchemeSound Alert;        // alert stem    (`+0x538`)
	FElysiumSchemeSound Ambient;      // the looping bed (`+0x460`)
	TArray<FElysiumRandomSound> RandomSounds;

	bool bParsed = false;

	// `FUN_1022a930` (`CSoundScheme::Precache 0x1022a4b0`): load the scheme file named as the entity
	// names it (`sound/Schemes/X.txt`) through the KeyValues file loader `0x102480f0` (cache 1, pathID
	// 0) and read the first root's blocks with the retail defaults. Returns false (Out left
	// bParsed=false) when the file cannot be opened or its first root has no children.
	static bool ParseFile(const FString& SchemeRel, FElysiumSoundScheme& Out);
};

// One manager track record (`CSoundSchemeManager`, stride `0x58`; `walks/L0-r009.md` § Shared facts):
// the four live slots at `+0x14` Ambient, `+0x6c` Music, `+0xc4` Combat, `+0x11c` Alert, and every
// element of the retiring list (`+0x174`). The first `0x40` bytes double as the sample name the engine
// sound server is handed; the port's `Voice` is that engine sound (the name on the channel).
struct FElysiumSchemeTrack
{
	FString Name;          // +0x00 char[64]: the scheme record's Filename (`Q_strncpy(..., 0x40)`)
	float   Target = 0.f;  // +0x40 target volume (zeroed by retire; rewritten each frame by the state gate)
	float   Current = 0.f; // +0x44 ramped volume, the level emitted (written only by `FUN_10229090`)
	float   Authored = 0.f;// +0x48 the authored (full) volume: the ramp's ceiling, the gate's restore value
	float   Rate = 0.f;    // +0x4c ramp rate, units per second (`volume / duration`)
	int32   Channel = 0;   // +0x50 engine channel 1..6 (`FUN_102297a0`)
	uint32  Flags = 0;     // +0x54 the scheme record's flags (`0x800` Dry, `0x2000` NoPause)
	FElysiumAudioVoiceHandle Voice;   // port: the engine sound the record's name and channel address

	bool IsEmpty() const { return Name.IsEmpty(); }   // `track[0] == 0`
};

// The manager's four live slots, in slot-offset order. `FUN_10229270` configures them in the order
// Ambient, Combat, Music, Alert (the scheme-record order), which is not this order.
enum class EElysiumSchemeSlot : uint8
{
	Ambient = 0,   // +0x14
	Music   = 1,   // +0x6c
	Combat  = 2,   // +0xc4
	Alert   = 3,   // +0x11c
};

// `FUN_1022b660` (`0x1022b660`) on one registered `ambient_soundscheme`: `ThinkSet(0)` and `+0x455 = 0`.
// Declared here so the manager's prune loop (`FUN_10229270`) can disable the schemes it still lists
// without knowing the entity class, which is file-local to ElysiumSoundScheme.cpp.
void ElysiumSoundSchemeDisable(FElysiumEntity& Scheme);

// `CSoundSchemeManager` (`DAT_10750d78`, ctor `0x10228450`): the four live track records, the
// retiring list, the registered-scheme EHANDLE list, the music-state word, plus the port's random
// one-shot scheduler. Plain C++ (no UObject), owned by AElysiumMapActor, torn down on map unload
// (which stops its voices). Every method takes the audio subsystem explicitly so the manager holds no
// UObject pointer across frames.
class FElysiumSoundSchemeManager
{
public:
	FElysiumSoundSchemeManager() = default;
	~FElysiumSoundSchemeManager() = default;

	FElysiumSoundSchemeManager(const FElysiumSoundSchemeManager&) = delete;
	FElysiumSoundSchemeManager& operator=(const FElysiumSoundSchemeManager&) = delete;
	void SetMapEpoch(uint64 InMapEpoch) { MapEpoch = InMapEpoch; }

	// `FUN_102289e0` (`0x102289e0`): append the scheme's EHANDLE to the registered list (`+0x19c`
	// base, `+0x1a8` count). No duplicate check: a second registration lists the handle twice.
	int32 RegisterScheme(const FElysiumEntity& Scheme, FElysiumEntityWorld* World);   // returns the new count (`+0x1a8`)
	int32 RegisteredSchemeCount() const { return Schemes.Num(); }

	// `FUN_1022b590` arm 5: `FUN_10229270(&DAT_10750d78, duration, +0x460, +0x4f0, +0x4a8, +0x538)`
	// (`0x10229270`) -- prune the registered list, disable every scheme still listed, then configure
	// the four slots in the order Ambient, Combat, Music, Alert. SchemeRel names the scheme whose four
	// records are the arguments (the cached parse stands for the entity's `+0x460..+0x57c`); Anchor is
	// the `ambient_soundscheme` origin, the centre of polar random placement (the port's scheduler).
	// World resolves the registered handles and carries the `retail_site` events (`CSoundSchemeManager`
	// in the entity column); null World prunes nothing and reports nothing.
	void FadeInScheme(UElysiumAudioSubsystem* Audio, FElysiumEntityWorld* World, const FString& SchemeRel,
		const FVector& Anchor, float Duration);
	void PrimeScheme(UElysiumAudioSubsystem* Audio, const FString& SchemeRel);
	// `FUN_1022b520` arm 3: `FUN_102293f0(&DAT_10750d78, duration)` (`0x102293f0`) -- retire all four
	// slots, in slot order `+0x14`, `+0x6c`, `+0xc4`, `+0x11c`. SchemeRel is the caller's name for the
	// log; retail retires the manager's slots whatever scheme filled them.
	void FadeOutScheme(UElysiumAudioSubsystem* Audio, FElysiumEntityWorld* World, const FString& SchemeRel,
		float Duration);

	// Per-frame: `CSoundSchemeManager::FrameUpdatePostEntityThink` (`0x10228d00`) -- the music-state
	// gate (`FUN_10228e80`), the ramp and emit of the four live records and of every retiring record
	// (`FUN_10229090`), the removal of a retiring record whose level reached `0.01`, then the port's
	// random scheduler. PlayerLoc is the listener (unused for anchor-centred placement). Safe with
	// null Audio. DeltaSeconds is retail's `curtime - [+0x0c]`.
	void Tick(UElysiumAudioSubsystem* Audio, FElysiumEntityWorld* World, const FVector& PlayerLoc, float DeltaSeconds);

	// Stop every voice (map unload). Audio may be null (best-effort). Retail: `LevelShutdownPostEntity`
	// (`0x102287a0`) frees the retiring, EHANDLE and RandomSound lists; the next
	// `LevelInitPreEntity` (`0x10228700`) zeroes the four live records.
	void StopAll(UElysiumAudioSubsystem* Audio);

	// Music state machine.
	void SetMusicState(UElysiumAudioSubsystem* Audio, EElysiumMusicState NewState, float CrossfadeSeconds = -1.f);
	EElysiumMusicState MusicState() const { return CurrentMusicState; }

	// Debug read (Cog Sound Schemes window).
	bool HasActiveScheme() const { return Active.Scheme.bParsed; }
	const FElysiumSoundScheme& ActiveScheme() const { return Active.Scheme; }
	const FString& ActiveSchemeRel() const { return Active.SchemeRel; }
	FVector ActiveAnchor() const { return Active.Anchor; }
	int32 ActiveRandomVoiceCount() const { return Active.RandomVoices.Num(); }
	double SchemeElapsed() const { return Elapsed; }
	const FElysiumSchemeTrack& Track(EElysiumSchemeSlot Slot) const { return Tracks[static_cast<int32>(Slot)]; }
	const TArray<FElysiumSchemeTrack>& RetiringTracks() const { return Retiring; }

private:
	// The scheme the last switch installed: its parse (for the random scheduler), its rel and its
	// anchor. The bed and the three stems are the manager's track records, not this.
	struct FActiveScheme
	{
		FElysiumSoundScheme Scheme;
		FString  SchemeRel;
		FVector  Anchor = FVector::ZeroVector;

		TArray<FElysiumAudioVoiceHandle> RandomVoices;   // in-flight one-shots
		TArray<double> RandomNextTime;                   // per-RandomSound next-attempt time (Elapsed clock)

		bool IsSet() const { return Scheme.bParsed; }
	};

	// Fetch/parse (cached) a scheme by rel path. Returns null if the file is missing/empty.
	const FElysiumSoundScheme* LoadScheme(const FString& SchemeRel);

	// `FUN_10229430` (`0x10229430`): retire an empty/zero-volume record's slot, retarget a same-name
	// slot in place, or retire and replace a different-name slot.
	void ConfigureTrack(UElysiumAudioSubsystem* Audio, IElysiumRetailSiteSink* Sites, const FElysiumSchemeSound& Record,
		float Duration, EElysiumSchemeSlot Slot);
	// `FUN_10229550` (`0x10229550`): zero a populated slot's target, set its outgoing rate, append it to
	// the retiring list and clear the slot.
	void RetireTrack(IElysiumRetailSiteSink* Sites, EElysiumSchemeSlot Slot, float Duration);
	// `FUN_102297a0` (`0x102297a0`): the first free channel of 1..6, else evict the quietest retiring
	// record and take its channel.
	int32 AllocChannel(UElysiumAudioSubsystem* Audio, IElysiumRetailSiteSink* Sites);
	// `FUN_10228e80` (`0x10228e80`): the music-state gate over the Music, Combat and Alert targets.
	void MusicStateGate(int32 State);
	// `FUN_10229090` (`0x10229090`): ramp one record toward its target and emit or stop its sound.
	void RampTrack(UElysiumAudioSubsystem* Audio, FElysiumSchemeTrack& Track, EElysiumSchemeSlot Slot, float DeltaSeconds);
	void StopTrackVoice(UElysiumAudioSubsystem* Audio, FElysiumSchemeTrack& Track);
	void TickRandom(UElysiumAudioSubsystem* Audio, const FVector& PlayerLoc);

	static constexpr int32 SlotOffset(EElysiumSchemeSlot Slot)
	{
		return Slot == EElysiumSchemeSlot::Ambient ? 0x14 : Slot == EElysiumSchemeSlot::Music ? 0x6c
			: Slot == EElysiumSchemeSlot::Combat ? 0xc4 : 0x11c;
	}

	TMap<FString, FElysiumSoundScheme> SchemeCache;
	FActiveScheme Active;
	FElysiumSchemeTrack Tracks[4];              // +0x14, +0x6c, +0xc4, +0x11c
	TArray<FElysiumSchemeTrack> Retiring;       // +0x174 base, +0x180 count
	TArray<FElysiumEntityHandle> Schemes;       // +0x19c base, +0x1a8 count (the registered EHANDLEs)
	int32 MusicStateWord = 0;                   // +0x10: the state the gate last applied (1, 2 or 3)
	EElysiumMusicState CurrentMusicState = EElysiumMusicState::Explore;
	double Elapsed = 0.0;   // manager clock (accumulated Dt), drives the random scheduler
	uint64 MapEpoch = 0;
};
