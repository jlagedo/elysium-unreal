#pragma once

#include "CoreMinimal.h"
#include "Audio/ElysiumSoundScript.h"
#include "Misc/Crc.h"

struct IElysiumRetailSiteSink;

// The sound-script table and its resolver: the parts of `vampire.dll`'s one `CSoundEmitterSystemBase`
// instance (`DAT_1072be18`, RTTI 0x105986f0, vftable 0x1047b3c0) that answer "what does sound script
// `X` play, at what channel, volume, pitch and level?" -- `GetParametersForSound` `FUN_101b33f0`
// `0x101b33f0` and the three helpers it composes: the name lookup `FindSound` `FUN_101b2f60`
// `0x101b2f60`, the interval sampler `FUN_1012f700` `0x1012f700` and the wave pick `FUN_101b3240`
// `0x101b3240`; plus the missing-wave pass `FUN_101b4740` `0x101b4740` that writes the one flag the
// resolver validates against. The walk is `docs/specs/layers/L0-entity/walks/L0-r008.md`; the prose
// `docs/vtmb/audio_pipeline.md` § 3 "Resolving a sound script".
//
// Layout of the instance, as the constructor `FUN_101b26c0` and the bodies read it:
// - `+0x50` the sound tree (`CUtlRBTree`, node stride 0xE8: `+0x10` the u16 name symbol, the record
//   `CSoundParametersInternal` at `+0x1C`); its LessFunc `FUN_101b6680` compares the two u16 SYMBOLS
//   numerically, so the tree is ordered by symbol id, not by string. Here: `Nodes` plus `NodeBySymbol`.
// - `+0x70` the sound-name symbol table (`FUN_1024b2d0(.., caseInsensitive=1)`, LessFunc `strcmpi`):
//   two spellings that differ only in case are one symbol, the first spelling interned is the stored
//   one. Here: `NameSymbols` keyed by the ASCII-lowered name, `Names` by symbol.
// - `+0xB0` the wave-string table (`caseInsensitive=0`, LessFunc a byte compare): CASE-SENSITIVE.
//   Here: `WaveSymbols` / `Waves`. `FUN_1024b830` maps a symbol back to its string (0xFFFF -> "").
// - The static missing-sample cache `DAT_1072c078` (arm 15 of the resolver): a case-sensitive string
//   table keyed `<sound>:<wave>`. Retail's is a function static (process lifetime); here it lives on the
//   table, which de-duplicates the same developer message over the same span (one table per process in
//   the game; one per fixture in the arena) -- nothing the VM observes.
//
// What is not here: the manifest reader `AddSoundsFromFile` `0x101b4240`, the key parser `FUN_101b3bb0`
// and the wave producer `FUN_101b3830` that FILL the table (the sound-script lane's own story); `AddEntry`
// below is the insert they end in, so a controlled table can be staged.
namespace ElysiumSoundScript
{
	// `VEngineRandom001` (`DAT_1070b244`, engine.dll `CEngineUniformRandomStream`, vtable 0x20173740):
	// slot 1 `RandomFloat(lo, hi)`, slot 2 `RandomInt(lo, hi)`, both forwarding to vstdlib's global
	// `CUniformRandomStream` (`vstdlib.dll 0x10002e00` / `0x10002e60`). The walk recovered both bodies
	// (Shared facts): RandomFloat is `(hi - lo) * x + lo` with `x = ran * 4.656612875245797e-10` capped at
	// `0x3f7ffffe` (0.99999988079), exactly one generator call, the sum left at x87 precision;
	// RandomInt with `n = hi - lo + 1 <= 1` returns `lo` WITHOUT a generator call, else draws `r` until
	// `r <= 0x7fffffff - (0x80000000 % n)` and returns `r % n + lo`.
	struct IEngineRandom
	{
		virtual ~IEngineRandom() = default;
		// Slot 1. Returned as a double: the x87 ST0 result the callers store (`FSTP float`) or truncate
		// (`__ftol`) themselves.
		virtual double RandomFloat(float Lo, float Hi) = 0;
		// Slot 2.
		virtual int32 RandomInt(int32 Lo, int32 Hi) = 0;
		// How many times the generator (`GenerateRandomNumber`, `vstdlib.dll 0x10002d30`) ran: the
		// stream's advance, which is what a later draw anywhere in the game observes.
		virtual int32 GeneratorCalls() const = 0;
	};

	// The port's engine random: vstdlib's two formulas over the session's named `SoundScript` stream
	// (`ElysiumRng.h`). The generator itself is the named modernization every draw in this codebase
	// already carries (a saved, seeded `FRandomStream` in place of vstdlib's Park-Miller `ran1`, whose
	// seeding is UNRECOVERED); the formulas, the cap, the draw count and the no-draw case are retail's.
	struct FSessionRandom final : public IEngineRandom
	{
		virtual double RandomFloat(float Lo, float Hi) override;
		virtual int32 RandomInt(int32 Lo, int32 Hi) override;
		virtual int32 GeneratorCalls() const override { return Calls; }

	private:
		uint32 Generate();   // one generator call: 31 bits, as `ran1` yields
		int32 Calls = 0;
	};

	// `VFileSystem005` (`DAT_1070b238`, written by `CServerGameDLL::vfunc1` `0x1011a0c0`) slot 9
	// (`+0x24`), called `bool(path, NULL)` and read as "the file exists". The filesystem module is not
	// in the corpus (search paths, pack files and case handling UNRECOVERED); the port asks what the
	// running game can read: the baked sound assets and the deployed corpus.
	struct IFileExists
	{
		virtual ~IFileExists() = default;
		virtual bool FileExists(const TCHAR* Path) = 0;
	};

	// The game's answer: `sound/<rel>` is present when the bake carries `<rel>` (`ElysiumSoundAssets::Exists`)
	// or the deployed corpus holds the file under `CorpusRoot()/sound/`.
	struct FGameSoundFiles final : public IFileExists
	{
		virtual bool FileExists(const TCHAR* Path) override;
	};

	// Source's `CSoundParameters`, the `out` of the resolver (>= 0xA0 bytes), by retail offset. Every
	// caller pre-initialises it with these values (the decompiled locals of `101b1040`, `101b40a0`,
	// `101b4170`, `101b1880`); they are what a false return from arms 1-2 leaves visible.
	struct FSoundParameters
	{
		int32 Channel = 0;              // +0x00 <- rec+0x00
		float Volume = 1.0f;            // +0x04 <- the volume sample, stored as float
		int32 Pitch = 100;              // +0x08 <- trunc(pitch sample)
		int32 PitchLow = 100;           // +0x0C <- trunc(pitch base)
		int32 PitchHigh = 100;          // +0x10 <- trunc((float)PitchLow + pitch span)
		int32 SoundLevel = 75;          // +0x14 <- trunc(soundlevel sample)
		uint8 bPlayToOwnerOnly = 0;     // +0x18 (one byte) <- rec+0x1C
		int32 Count = 0;                // +0x1C <- rec+0x2C, the wave count
		FString SoundName;              // +0x20 char[0x80] <- the chosen wave (`Q_strncpy(.., 0x80)`), "" if none
	};

	// `FUN_1012f700` `0x1012f700` (53 B, `__cdecl(float* p)`, result in ST0): `p[1] == 0.0f` (the
	// `FCOMP` against 0x104454c4, 0x1012f707) -> `p[0]`; else `p[0] + VEngineRandom001.RandomFloat(0,
	// p[1])` (call 0x1012f728, `FADD` 0x1012f72b). One generator call iff the span is not zero; a
	// negative span is not rejected (the sample lies in `(base + span, base]`); -0.0 counts as zero;
	// a NaN span reaches RandomFloat unchanged. Returned at double precision for the caller's own
	// store or truncation (the runtime x87 precision control is UNRECOVERED).
	double SampleInterval(const FInterval& Interval, IEngineRandom& Random, IElysiumRetailSiteSink* Sites);

	// `FUN_101b3240` `0x101b3240` (324 B, `__stdcall(list, category)`, `RET 8`): `count < 1` -> -1
	// (0x101b325e); else the indices whose `+4` word equals `category` (0x101b3289) are gathered; one or
	// more -> `matches[RandomInt(0, m - 1)]` (0x101b32d0); none -> `RandomInt(0, count - 1)` (0x101b3330),
	// every entry a candidate. A candidate set of ONE draws nothing (`RandomInt(0, 0)` returns without a
	// generator call). Categories in retail data (producer `FUN_101b3830`): 0 a plain wave, 1 the male
	// and 2 the female expansion of a `$gender` wave; the resolver always asks for 0.
	int32 SelectWave(const TArray<FWave>& Waves, uint32 Category, IEngineRandom& Random, IElysiumRetailSiteSink* Sites);

	// Whether the missing-wave pass prints (`FUN_101b4740`'s `param_1`): `BaseInit` runs it quiet, the
	// console command body `FUN_101b0670` verbose.
	enum class EMissingReport : uint8 { Quiet, Verbose };

	// A string key compared byte for byte. `FString`'s own `==` and hash fold case, which the
	// case-sensitive tables (`+0xB0`, the missing-sample cache, a file set) must not.
	struct FExactKey
	{
		FString Text;
		FExactKey() = default;
		explicit FExactKey(const TCHAR* InText) : Text(InText) {}
		explicit FExactKey(const FString& InText) : Text(InText) {}
		bool operator==(const FExactKey& Other) const { return Text.Equals(Other.Text, ESearchCase::CaseSensitive); }
		friend uint32 GetTypeHash(const FExactKey& Key) { return FCrc::StrCrc32(*Key.Text); }
	};

	class FTable
	{
	public:
		static constexpr uint16 InvalidSymbol = 0xFFFF;

		// `FUN_1024b5e0` on the sound-name table `+0x70` (`AddString`): NULL -> 0xFFFF; else `Find`
		// (`FUN_1024b400`, case-insensitive) and, absent, the string copied and a new symbol. Runs on
		// every `FindSound`, so an unknown name is added. `OutNew` (optional) reports an insert.
		uint16 InternName(const TCHAR* Name, bool* OutNew);
		// The same on the wave-string table `+0xB0` (case-sensitive) -- the store `FUN_101b3830` makes for
		// every `wave` / `rndwave` entry.
		uint16 InternWave(const TCHAR* Wave);
		// `FUN_1024b830(this + 0xB0, sym)`: the wave string, `""` for 0xFFFF (the shared empty string
		// `DAT_106b8540`).
		const FString& WaveString(uint16 Symbol) const;

		// The insert `AddSoundsFromFile` `0x101b4240` ends in: the name interned, a node keyed by its
		// symbol holding the record. A second entry under a name already in the tree is refused (-1): the
		// shipped scripts carry none and `CUtlRBTree`'s duplicate placement was not walked. Returns the
		// node index.
		int32 AddEntry(const TCHAR* Name, FParams&& Record);

		// `FUN_101b2f60` `0x101b2f60` (278 B, `__thiscall(this, name)`, `RET 4`): intern (0x101b2f8c), then
		// walk the tree from the root (0x101b2f97) comparing symbols; the node index when equal, -1 when
		// the walk falls off (an empty tree answers -1 at once).
		int32 FindSound(const TCHAR* Name, IElysiumRetailSiteSink* Sites);

		// `FUN_101b3730` `0x101b3730` (40 B): `0 <= idx < count` -> the record at `node + 0x1C`; else NULL.
		FParams* Record(int32 Index);
		const FParams* Record(int32 Index) const;

		// A read-only lookup for a probe: the node index of a name already in the table, -1 otherwise.
		// Not a retail entry (`FindSound` interns); it changes nothing.
		int32 Lookup(const TCHAR* Name) const;

		// `FUN_101b4740` `0x101b4740` (`__thiscall(this, verbose)`): for every record (`FUN_101b3220` is
		// the count) and every wave of its `+0x20` list, skip an empty name and a name starting `!`,
		// strip up to two leading sound-char prefixes, build `"sound/%s"` (0x200) and ask slot 9; a miss
		// sets `rec + 0x48 = 1`, counts, and (verbose) prints `Sound %s references missing file %s`.
		// Returns the miss count. Run once at the end of `BaseInit` `0x101b2c60` (quiet; the log line
		// `Registered %i sounds ( %i missing .wav files referenced )`) and by the console command body
		// `FUN_101b0670` (verbose).
		int32 MarkMissingWaves(IFileExists& Files, EMissingReport Report, IElysiumRetailSiteSink* Sites);

		// `FUN_101b33f0` `0x101b33f0` (607 B, `__thiscall(this, name, out)`, `RET 8`, result in AL): the
		// fifteen arms in retail order (`walks/L0-r008.md`). Draw order: volume (iff span != 0) ->
		// pitch (iff span != 0) -> wave pick (iff two or more candidates) -> soundlevel (iff span != 0).
		// `Out` is left untouched by arms 1-2 only; every other return comes after it was written.
		bool GetParametersForSound(const TCHAR* Name, FSoundParameters& Out, IEngineRandom& Random,
			IFileExists& Files, IElysiumRetailSiteSink* Sites);

		int32 Count() const { return Nodes.Num(); }           // `FUN_101b3220`: `*(this + 0x64)`
		int32 NameCount() const { return Names.Num(); }       // the symbol table's `+0x12`
		int32 WaveCount() const { return Waves.Num(); }

	private:
		struct FNode
		{
			uint16 Symbol = InvalidSymbol;   // node + 0x10
			FParams Record;                  // node + 0x1C
		};
		TArray<FNode> Nodes;                 // `+0x54`, by node index
		TMap<uint16, int32> NodeBySymbol;    // the tree's order: by symbol, numerically
		TMap<FExactKey, uint16> NameSymbols; // `+0x70`: ASCII-lowered name -> symbol (the `strcmpi` LessFunc)
		TArray<FString> Names;               // by symbol: the first spelling interned
		TMap<FExactKey, uint16> WaveSymbols; // `+0xB0`: exact name -> symbol
		TArray<FString> Waves;               // by symbol
		TSet<FExactKey> MissingSampleCache;  // `DAT_1072c078`: `<sound>:<wave>` keys already reported
	};
}
