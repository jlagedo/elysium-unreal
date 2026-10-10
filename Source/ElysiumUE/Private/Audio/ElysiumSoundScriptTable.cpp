#include "Audio/ElysiumSoundScriptTable.h"

#include "ElysiumContentPaths.h"
#include "ElysiumRetailSite.h"
#include "ElysiumRng.h"
#include "ElysiumSoundAssets.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumSoundScriptTable, Log, All);

namespace
{
	const TCHAR* const GFindFn = TEXT("Global::FUN_101b2f60");
	const TCHAR* const GIntervalFn = TEXT("Global::FUN_1012f700");
	const TCHAR* const GPickFn = TEXT("Global::FUN_101b3240");
	const TCHAR* const GResolveFn = TEXT("Global::FUN_101b33f0");
	const TCHAR* const GMissingFn = TEXT("Global::FUN_101b4740");

	void Emit(IElysiumRetailSiteSink* Sites, const TCHAR* Tag, const TCHAR* Fn, uint32 Va, const TCHAR* Phase, const FString& Payload)
	{
		if (Sites != nullptr)
		{
			Sites->Site(Tag, Fn, Va, Phase, Payload);
		}
	}

	const TCHAR* NameOrNull(const TCHAR* Name)
	{
		// `DevMsg("%s", NULL)` prints `(null)` through the CRT; the site spells it `null`.
		return Name != nullptr ? Name : TEXT("null");
	}

	// The nine-character sound-char set both `FUN_101b33f0` (0x101b353c-0x101b3567) and `FUN_101b4740`
	// strip: `*` `?` `!` `#` `@` `>` `<` `^` `)` (0x2a 0x3f 0x21 0x23 0x40 0x3e 0x3c 0x5e 0x29). A do-while
	// capped at two: the first character not in the set stops it, so at most two are skipped.
	const TCHAR* StripSoundChars(const TCHAR* Wave)
	{
		const TCHAR* P = Wave;
		int32 Stripped = 0;
		do
		{
			const TCHAR C = *P;
			if (C != TEXT('*') && C != TEXT('?') && C != TEXT('!') && C != TEXT('#') && C != TEXT('@') && C != TEXT('>')
				&& C != TEXT('<') && C != TEXT('^') && C != TEXT(')'))
			{
				break;
			}
			++P;
			++Stripped;
		} while (Stripped < 2);
		return P;
	}

	// `__ftol` (0x10431320): round toward zero, the low dword. A value outside int32 is UNRECOVERED
	// (the FISTP writes 0x80000000; no shipped script reaches it).
	int32 FtolTrunc(double Value)
	{
		return static_cast<int32>(FMath::TruncToDouble(Value));
	}

	// The `strcmpi` LessFunc of the sound-name table (`FUN_1024b230`): CRT `_strcmpi`, ASCII letters
	// folded. The key two spellings share.
	FString FoldName(const TCHAR* Name)
	{
		FString Key(Name);
		for (TCHAR& C : Key)
		{
			if (C >= TEXT('A') && C <= TEXT('Z'))
			{
				C = static_cast<TCHAR>(C + 32);
			}
		}
		return Key;
	}
}

namespace ElysiumSoundScript
{
	// --- VEngineRandom001 -------------------------------------------------------------------------

	uint32 FSessionRandom::Generate()
	{
		// `GenerateRandomNumber` (`vstdlib.dll 0x10002d30`): one 31-bit draw. The named stream stands in
		// for the Park-Miller state (see the header).
		++Calls;
		return ElysiumRng::Stream(EElysiumRngStream::SoundScript).GetUnsignedInt() & 0x7fffffffu;
	}

	double FSessionRandom::RandomFloat(float Lo, float Hi)
	{
		// `CUniformRandomStream::RandomFloat` (`vstdlib.dll 0x10002e00`): `x = (float)(ran * [0x10010568])`
		// with the double 4.656612875245797e-10 (1 / 2147483647); `x > [0x10010560]` (0.99999988) ->
		// `x = 0x3f7ffffe`; `(hi - lo) * x + lo` on the x87 stack, returned unrounded (0x10002e3b-0x10002e47).
		const uint32 Ran = Generate();
		float X = static_cast<float>(static_cast<double>(Ran) * 4.656612875245797e-10);
		if (static_cast<double>(X) > 0.99999988)
		{
			X = FMath::AsFloat(0x3f7ffffeu);
		}
		return (static_cast<double>(Hi) - static_cast<double>(Lo)) * static_cast<double>(X) + static_cast<double>(Lo);
	}

	int32 FSessionRandom::RandomInt(int32 Lo, int32 Hi)
	{
		// `CUniformRandomStream::RandomInt` (`vstdlib.dll 0x10002e60`): `n = hi - lo + 1`; `n > 1` and
		// `(uint)(hi - lo) < 0x80000000` -> draw until `r <= 0x7fffffff - (0x80000000 % n)`, return
		// `r % n + lo`; otherwise `lo` with NO generator call (so `RandomInt(0, 0)` leaves the stream).
		const uint32 N = static_cast<uint32>(Hi - Lo + 1);
		if (N > 1u && static_cast<uint32>(Hi - Lo) < 0x80000000u)
		{
			const uint32 Limit = 0x7fffffffu - (0x80000000u % N);
			uint32 R = 0;
			do
			{
				R = Generate();
			} while (R > Limit);
			return static_cast<int32>(R % N) + Lo;
		}
		return Lo;
	}

	// --- VFileSystem005 slot 9 -------------------------------------------------------------------

	bool FGameSoundFiles::FileExists(const TCHAR* Path)
	{
		// `sound/<rel>`: the bake's key is `<rel>`; the deployed corpus keeps retail's own tree.
		FString Full(Path);
		FString Rel = Full;
		if (Rel.StartsWith(TEXT("sound/"), ESearchCase::IgnoreCase))
		{
			Rel.RightChopInline(6);
		}
		if (ElysiumSoundAssets::Exists(Rel))
		{
			return true;
		}
		return IFileManager::Get().FileExists(*(FElysiumContentPaths::CorpusRoot() / Full));
	}

	// --- FUN_1012f700 ----------------------------------------------------------------------------

	double SampleInterval(const FInterval& Interval, IEngineRandom& Random, IElysiumRetailSiteSink* Sites)
	{
		// Arm 1. `FLD [p+4]; FCOMP [0x104454c4]` (0x1012f707): equal (C3 set, `-0.0` included) -> `p[0]`
		//        (`FLD [ESP+4]` at 0x1012f730). Greater, less and unordered (NaN) fall through.
		if (Interval.Range == 0.f)
		{
			Emit(Sites, TEXT("sndscript.interval"), GIntervalFn, 0x1012f730u, TEXT("return"),
				FString::Printf(TEXT("base=%g span=%g draw=0 value=%g"), Interval.Start, Interval.Range, Interval.Start));
			return static_cast<double>(Interval.Start);
		}
		// Arm 2. `PUSH [p+4]; PUSH 0; CALL [VEngineRandom001+4]` (0x1012f723-0x1012f728): one generator
		//        call, then `FADD [ESP+4]` (0x1012f72b) adds the base at x87 precision.
		const double Sample = Random.RandomFloat(0.f, Interval.Range) + static_cast<double>(Interval.Start);
		Emit(Sites, TEXT("sndscript.interval"), GIntervalFn, 0x1012f72bu, TEXT("return"),
			FString::Printf(TEXT("base=%g span=%g draw=1 value=%g"), Interval.Start, Interval.Range, Sample));
		return Sample;
	}

	// --- FUN_101b3240 ----------------------------------------------------------------------------

	int32 SelectWave(const TArray<FWave>& Waves, uint32 Category, IEngineRandom& Random, IElysiumRetailSiteSink* Sites)
	{
		// Arm 1. `count < 1` -> -1 (0x101b324f-0x101b325e). No draw.
		const int32 Count = Waves.Num();
		if (Count < 1)
		{
			Emit(Sites, TEXT("sndscript.pick"), GPickFn, 0x101b325eu, TEXT("return"),
				FString::Printf(TEXT("arm=empty count=0 category=%u index=-1 draw=0"), Category));
			return -1;
		}
		// Arm 2. `i = 0 .. count-1`: `entry[i].category == category` (`CMP [EDX+EDI*8+4],EAX` 0x101b3289)
		//        appends `i` to the stack vector (`thunk_FUN_10071ae0` grow by 1, `thunk_FUN_10071ba0` shift).
		TArray<int32> Matches;
		for (int32 I = 0; I < Count; ++I)
		{
			if (Waves[I].Gender == Category)
			{
				Matches.Add(I);
			}
		}
		Emit(Sites, TEXT("sndscript.pick"), GPickFn, 0x101b3289u, TEXT("branch"),
			FString::Printf(TEXT("count=%d category=%u matches=%d"), Count, Category, Matches.Num()));
		const int32 CallsBefore = Random.GeneratorCalls();
		// Arm 3. `m >= 1` -> `matches[RandomInt(0, m - 1)]` (slot 2 call 0x101b32d0). `m == 1` draws
		//        nothing. The temp vector's `Plat_Free` (0x101b32f4) has no observable effect.
		if (Matches.Num() >= 1)
		{
			const int32 K = Random.RandomInt(0, Matches.Num() - 1);
			const int32 Index = Matches[K];
			Emit(Sites, TEXT("sndscript.pick"), GPickFn, 0x101b32d0u, TEXT("return"),
				FString::Printf(TEXT("arm=matched m=%d k=%d index=%d draw=%d"), Matches.Num(), K, Index, Random.GeneratorCalls() - CallsBefore));
			return Index;
		}
		// Arm 4. `m == 0` -> `RandomInt(0, count - 1)` (call 0x101b3330): every entry a candidate;
		//        `count == 1` draws nothing. The `Plat_Free` at 0x101b3353 is dead (nothing was allocated).
		const int32 Index = Random.RandomInt(0, Count - 1);
		Emit(Sites, TEXT("sndscript.pick"), GPickFn, 0x101b3330u, TEXT("return"),
			FString::Printf(TEXT("arm=fallback count=%d index=%d draw=%d"), Count, Index, Random.GeneratorCalls() - CallsBefore));
		return Index;
	}

	// --- The table ---------------------------------------------------------------------------------

	uint16 FTable::InternName(const TCHAR* Name, bool* OutNew)
	{
		if (OutNew != nullptr)
		{
			*OutNew = false;
		}
		// `FUN_1024b5e0`: `name == NULL` -> `*out = 0xFFFF` (no table change).
		if (Name == nullptr)
		{
			return InvalidSymbol;
		}
		// `Find` first (`FUN_1024b400`, the `strcmpi` LessFunc of `this + 0x70`): a hit is its symbol.
		const FExactKey Key(FoldName(Name));
		if (const uint16* Found = NameSymbols.Find(Key))
		{
			return *Found;
		}
		// Absent: the string (first spelling) copied into a pool block, a node inserted, the count
		// (`+0x12`) bumped; the new symbol is the node index.
		const uint16 Symbol = static_cast<uint16>(Names.Num());
		Names.Add(Name);
		NameSymbols.Add(Key, Symbol);
		if (OutNew != nullptr)
		{
			*OutNew = true;
		}
		return Symbol;
	}

	uint16 FTable::InternWave(const TCHAR* Wave)
	{
		// The same routine on `this + 0xB0`, whose LessFunc is a byte compare (`FUN_1024b150`): exact.
		if (Wave == nullptr)
		{
			return InvalidSymbol;
		}
		const FExactKey Key(Wave);
		if (const uint16* Found = WaveSymbols.Find(Key))
		{
			return *Found;
		}
		const uint16 Symbol = static_cast<uint16>(Waves.Num());
		Waves.Add(Key.Text);
		WaveSymbols.Add(Key, Symbol);
		return Symbol;
	}

	const FString& FTable::WaveString(uint16 Symbol) const
	{
		// `FUN_1024b830(this + 0xB0, sym)`: 0xFFFF (and, here, a symbol the table never issued) -> the
		// shared empty string `DAT_106b8540`.
		static const FString Empty;
		return Symbol != InvalidSymbol && Waves.IsValidIndex(Symbol) ? Waves[Symbol] : Empty;
	}

	int32 FTable::AddEntry(const TCHAR* Name, FParams&& Record)
	{
		const uint16 Symbol = InternName(Name, nullptr);
		if (Symbol == InvalidSymbol || NodeBySymbol.Contains(Symbol))
		{
			return -1;
		}
		FNode& Node = Nodes.AddDefaulted_GetRef();
		Node.Symbol = Symbol;
		Node.Record = MoveTemp(Record);
		const int32 Index = Nodes.Num() - 1;
		NodeBySymbol.Add(Symbol, Index);
		return Index;
	}

	int32 FTable::FindSound(const TCHAR* Name, IElysiumRetailSiteSink* Sites)
	{
		// Arm 0. A node-shaped 0xE8-byte temp is default-constructed (`FUN_101b30d0` at 0x101b2f77) and
		//        destroyed at the end; never read. Nothing to reproduce.
		// Arm 1. Intern: `AddString(this + 0x70, &key, name)` (call 0x101b2f8c; `MOV DX,[EAX]` 0x101b2f94).
		//        Runs on every call, so an unknown name is added to the table.
		bool bNew = false;
		const uint16 Symbol = InternName(Name, &bNew);
		Emit(Sites, TEXT("sndscript.find"), GFindFn, 0x101b2f8cu, TEXT("intern"),
			FString::Printf(TEXT("name=%s sym=%d new=%d"), NameOrNull(Name), Symbol == InvalidSymbol ? -1 : static_cast<int32>(Symbol), bNew ? 1 : 0));
		// Arm 2. Search from the root `*(this + 0x60)` (0x101b2f97) while `idx != -1`: the LessFunc
		//        `FUN_101b6680` orders u16 symbols numerically (left when `key < elem`, equal returns
		//        `idx` at 0x101b2fe3, else right). An interned-but-absent symbol (and 0xFFFF for a NULL
		//        name) falls off the tree.
		// Arm 3. `idx == -1` at the loop's exit (an empty tree at once, 0x101b2f9f -> 0x101b303a) -> -1.
		const int32* Found = Symbol != InvalidSymbol ? NodeBySymbol.Find(Symbol) : nullptr;
		const int32 Index = Found != nullptr ? *Found : -1;
		Emit(Sites, TEXT("sndscript.find"), GFindFn, 0x101b2f97u, TEXT("return"),
			FString::Printf(TEXT("name=%s index=%d root=%d"), NameOrNull(Name), Index, Nodes.IsEmpty() ? -1 : 0));
		return Index;
	}

	FParams* FTable::Record(int32 Index)
	{
		// `FUN_101b3730`: `-1 < idx && idx < *(this + 0x64)` -> `*(this + 0x54) + 0x1C + idx * 0xE8`; else 0.
		return Index > -1 && Index < Nodes.Num() ? &Nodes[Index].Record : nullptr;
	}

	const FParams* FTable::Record(int32 Index) const
	{
		return Index > -1 && Index < Nodes.Num() ? &Nodes[Index].Record : nullptr;
	}

	int32 FTable::Lookup(const TCHAR* Name) const
	{
		const uint16* Symbol = Name != nullptr ? NameSymbols.Find(FExactKey(FoldName(Name))) : nullptr;
		const int32* Found = Symbol != nullptr ? NodeBySymbol.Find(*Symbol) : nullptr;
		return Found != nullptr ? *Found : -1;
	}

	int32 FTable::MarkMissingWaves(IFileExists& Files, EMissingReport Report, IElysiumRetailSiteSink* Sites)
	{
		// `iVar3 = FUN_101b3220(this)` (the count); `< 1` -> return (EDX garbage in retail; 0 here).
		const int32 SoundCount = Count();
		int32 Missing = 0;
		if (SoundCount < 1)
		{
			return 0;
		}
		for (int32 SoundIndex = 0; SoundIndex < SoundCount; ++SoundIndex)
		{
			// `FUN_101b3730(this, i)`; NULL skipped.
			FParams* Rec = Record(SoundIndex);
			if (Rec == nullptr)
			{
				continue;
			}
			// Every entry of the `+0x20` list (`+0x2C` the count): `FUN_1024b830(this + 0xB0, sym)`; an
			// empty name and a name starting `!` are skipped (a sentence, not a file).
			for (const FWave& Wave : Rec->Waves)
			{
				const FString& Name = WaveString(Wave.Symbol);
				if (Name.IsEmpty() || Name[0] == TEXT('!'))
				{
					continue;
				}
				// The same two-character strip as the resolver's arm 14, then `Q_snprintf(buf, 0x200,
				// "sound/%s", stripped)` (string 0x10548fa8) and slot 9 of `DAT_1070b238`.
				const FString Path = FString(TEXT("sound/")) + StripSoundChars(*Name);
				if (Files.FileExists(*Path))
				{
					continue;
				}
				// A miss: count, `rec + 0x48 = 1`, and verbose `DevMsg("Sound %s references missing file
				// %s\n", FUN_101b31d0(this, i), wave)` (string 0x1059968c) -- the sound's own name.
				++Missing;
				Rec->bHasMissingWave = 1;
				const FString& SoundName = Names.IsValidIndex(Nodes[SoundIndex].Symbol) ? Names[Nodes[SoundIndex].Symbol] : WaveString(InvalidSymbol);
				Emit(Sites, TEXT("sndscript.missing"), GMissingFn, 0x101b4740u, TEXT("write"),
					FString::Printf(TEXT("sound=%s wave=%s path=%s flag=1"), *SoundName, *Name, *Path));
				if (Report == EMissingReport::Verbose)
				{
					UE_LOG(LogElysiumSoundScriptTable, Verbose, TEXT("Sound %s references missing file %s"), *SoundName, *Name);
				}
			}
		}
		Emit(Sites, TEXT("sndscript.missing"), GMissingFn, 0x101b4740u, TEXT("return"),
			FString::Printf(TEXT("sounds=%d missing=%d"), SoundCount, Missing));
		return Missing;
	}

	bool FTable::GetParametersForSound(const TCHAR* Name, FSoundParameters& Out, IEngineRandom& Random,
		IFileExists& Files, IElysiumRetailSiteSink* Sites)
	{
		// Arm 1. `idx = FindSound(name)` (call 0x101b3404). Arm 2. `rec = FUN_101b3730(this, idx)`
		//        (0x101b340f-0x101b3418). -1 or NULL -> `DevMsg("CSoundEmitterSystemBase::
		//        GetParametersForSound:  No such sound %s\n", name)` (string 0x10599440, call 0x101b3422)
		//        and false (`XOR AL,AL` 0x101b342b). `out` untouched.
		const int32 Index = FindSound(Name, Sites);
		const FParams* Rec = Record(Index);
		Emit(Sites, TEXT("sndscript.resolve"), GResolveFn, 0x101b3409u, TEXT("entry"),
			FString::Printf(TEXT("name=%s index=%d"), NameOrNull(Name), Index));
		if (Index == -1 || Rec == nullptr)
		{
			UE_LOG(LogElysiumSoundScriptTable, Verbose, TEXT("CSoundEmitterSystemBase::GetParametersForSound:  No such sound %s"), NameOrNull(Name));
			Emit(Sites, TEXT("sndscript.resolve"), GResolveFn, 0x101b342bu, TEXT("return"),
				FString::Printf(TEXT("result=0 arm=nosuch name=%s"), NameOrNull(Name)));
			return false;
		}
		// Arm 3. `out[0] = rec[0]` (0x101b3447): the channel.
		Out.Channel = Rec->Channel;
		// Arm 4. `out[1] = interval(rec + 4)` (call 0x101b3449, `FSTP float [EDI+4]` 0x101b344e).
		Out.Volume = static_cast<float>(SampleInterval(Rec->Volume, Random, Sites));
		Emit(Sites, TEXT("sndscript.resolve"), GResolveFn, 0x101b3449u, TEXT("write"),
			FString::Printf(TEXT("field=volume value=%g"), Out.Volume));
		// Arm 5. `out[2] = __ftol(interval(rec + 0xC))` (call 0x101b3455, `__ftol` 0x101b345d).
		Out.Pitch = FtolTrunc(SampleInterval(Rec->Pitch, Random, Sites));
		Emit(Sites, TEXT("sndscript.resolve"), GResolveFn, 0x101b3455u, TEXT("write"),
			FString::Printf(TEXT("field=pitch value=%d"), Out.Pitch));
		// Arm 6. `out[3] = __ftol(rec[+0xC])` (`FLD [EBX]` 0x101b3465); `out[4] = __ftol((float)out[3] +
		//        rec[+0x10])` (`FILD`, `FADD [ESI+0x10]`, 0x101b3473-0x101b347f). No draws.
		Out.PitchLow = FtolTrunc(static_cast<double>(Rec->Pitch.Start));
		Out.PitchHigh = FtolTrunc(static_cast<double>(Out.PitchLow) + static_cast<double>(Rec->Pitch.Range));
		// Arm 7. `out[7] = rec[+0x2C]`; `out[0x20] = 0` (0x101b3482-0x101b3497).
		Out.Count = Rec->Waves.Num();
		Out.SoundName.Reset();
		// Arm 8. `w = SelectWave(rec + 0x20, 0)` (call 0x101b349a). `w >= 0`: `sym = list[w].symbol`
		//        (`MOV DX,[ECX+EAX*8]`), `FUN_101b36f0` copies it to the stack, `FUN_1024b830(this + 0xB0,
		//        sym)`, `Q_strncpy(out + 0x20, s, 0x80)` (0x101b34d1). `w == -1`: the name stays "".
		const int32 W = SelectWave(Rec->Waves, 0u, Random, Sites);
		if (W >= 0)
		{
			Out.SoundName = WaveString(Rec->Waves[W].Symbol).Left(0x7f);
		}
		Emit(Sites, TEXT("sndscript.resolve"), GResolveFn, 0x101b349au, TEXT("write"),
			FString::Printf(TEXT("field=soundname w=%d wave=%s"), W, *Out.SoundName));
		// Arm 9. `out[5] = __ftol(interval(rec + 0x14))` (call 0x101b34e4): sampled even with no wave.
		Out.SoundLevel = FtolTrunc(SampleInterval(Rec->SoundLevel, Random, Sites));
		Emit(Sites, TEXT("sndscript.resolve"), GResolveFn, 0x101b34e4u, TEXT("write"),
			FString::Printf(TEXT("field=soundlevel value=%d"), Out.SoundLevel));
		// Arm 10. `*(byte*)(out + 0x18) = *(byte*)(rec + 0x1C)` (0x101b34f6-0x101b34fb): play_to_owner_only.
		Out.bPlayToOwnerOnly = Rec->bPlayToOwnerOnly;
		// Arm 11. `name[0] == 0` (`TEST AL,AL`, 0x101b34fe) -> `DevMsg("...sound %s has no wave or rndwave
		//         key!\n", name)` (string 0x105993d8, 0x101b350d) and false (0x101b3516).
		Emit(Sites, TEXT("sndscript.resolve"), GResolveFn, 0x101b34feu, TEXT("branch"),
			FString::Printf(TEXT("empty=%d"), Out.SoundName.IsEmpty() ? 1 : 0));
		if (Out.SoundName.IsEmpty())
		{
			UE_LOG(LogElysiumSoundScriptTable, Verbose, TEXT("CSoundEmitterSystemBase::GetParametersForSound:  sound %s has no wave or rndwave key!"),
				NameOrNull(Name));
			Emit(Sites, TEXT("sndscript.resolve"), GResolveFn, 0x101b3516u, TEXT("return"),
				FString::Printf(TEXT("result=0 arm=nowave name=%s"), NameOrNull(Name)));
			return false;
		}
		// Arm 12. `rec[+0x48] == 0` (0x101b3528-0x101b352a) -> true (0x101b3640): no validation for a
		//         record the init pass `FUN_101b4740` left unflagged.
		Emit(Sites, TEXT("sndscript.resolve"), GResolveFn, 0x101b3528u, TEXT("branch"),
			FString::Printf(TEXT("missing_flag=%d"), Rec->bHasMissingWave));
		if (Rec->bHasMissingWave == 0)
		{
			Emit(Sites, TEXT("sndscript.resolve"), GResolveFn, 0x101b3643u, TEXT("return"), TEXT("result=1 arm=unflagged"));
			return true;
		}
		// Arm 13. `name[0] == '!'` (0x101b3530-0x101b3532) -> true: a sentence is not a file.
		if (Out.SoundName[0] == TEXT('!'))
		{
			Emit(Sites, TEXT("sndscript.resolve"), GResolveFn, 0x101b3643u, TEXT("return"), TEXT("result=1 arm=bang"));
			return true;
		}
		// Arm 14. Strip up to two sound chars (0x101b353c-0x101b3567), `Q_snprintf(buf, 0x100, "sound/%s",
		//         stripped)` (0x101b3582), `exists = VFileSystem005.slot9(buf, NULL)` (0x101b3599); exists
		//         (`JNZ` 0x101b359e) -> true.
		const FString Path = FString(TEXT("sound/")) + StripSoundChars(*Out.SoundName);
		const bool bExists = Files.FileExists(*Path);
		Emit(Sites, TEXT("sndscript.resolve"), GResolveFn, 0x101b359eu, TEXT("branch"),
			FString::Printf(TEXT("exists=%d path=%s"), bExists ? 1 : 0, *Path));
		if (bExists)
		{
			Emit(Sites, TEXT("sndscript.resolve"), GResolveFn, 0x101b3643u, TEXT("return"), TEXT("result=1 arm=exists"));
			return true;
		}
		// Arm 15. The missing sample. The static cache `DAT_1072c078` (lazily built case-sensitive,
		//         0x101b35c8); `key = Q_snprintf(buf, 0x100, "%s:%s", name, wave)` (string 0x105993d0,
		//         0x101b35ed) with the wave AS STORED (unstripped); `Find` (0x101b3601): a hit (`word [EAX]
		//         != 0xFFFF`, 0x101b3606 -> 0x101b360b) returns false silently; a miss `Add`s the key
		//         (0x101b361c), `DevMsg("...sound '%s' references wave '%s' which doesn't exist on
		//         disk!\n", name, wave)` (string 0x10599348, 0x101b3628) and returns false (0x101b3631).
		const FString CacheKey = FString::Printf(TEXT("%s:%s"), NameOrNull(Name), *Out.SoundName);
		if (MissingSampleCache.Contains(FExactKey(CacheKey)))
		{
			Emit(Sites, TEXT("sndscript.resolve"), GResolveFn, 0x101b360bu, TEXT("branch"),
				FString::Printf(TEXT("cache=hit key=%s"), *CacheKey));
			Emit(Sites, TEXT("sndscript.resolve"), GResolveFn, 0x101b3631u, TEXT("return"), TEXT("result=0 arm=missing reported=0"));
			return false;
		}
		MissingSampleCache.Add(FExactKey(CacheKey));
		Emit(Sites, TEXT("sndscript.resolve"), GResolveFn, 0x101b361cu, TEXT("write"),
			FString::Printf(TEXT("cache=add key=%s"), *CacheKey));
		UE_LOG(LogElysiumSoundScriptTable, Verbose, TEXT("CSoundEmitterSystemBase::GetParametersForSound:  sound '%s' references wave '%s' which doesn't exist on disk!"),
			NameOrNull(Name), *Out.SoundName);
		Emit(Sites, TEXT("sndscript.resolve"), GResolveFn, 0x101b3631u, TEXT("return"), TEXT("result=0 arm=missing reported=1"));
		return false;
	}
}
