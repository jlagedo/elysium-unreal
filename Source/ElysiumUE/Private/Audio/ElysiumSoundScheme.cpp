// SoundScheme runtime: the KeyValues scheme parser, the manager (`CSoundSchemeManager`: four track
// records, the retiring list, the registered-scheme list, the per-frame ramp) and the
// `ambient_soundscheme` entity (`CSoundScheme`: Activate, FadeIn, FadeOut, the playing flag and think).
// Reference: `docs/vtmb/audio_pipeline.md` §5/§6, the decompiled CSoundScheme parser (vampire.dll
// @0x1022a930) and the confirmed walk `docs/specs/layers/L0-entity/walks/L0-r009.md` -- every arm, order
// and constant below is cited at its line. The music STATE (retail `FUN_10228a80`, the local player's
// conditions, L3/L4) is the one input this layer does not produce: the cvar `elysium.MusicState`
// stands for it (a named modernization, the Cog window's echo of combat scoring).

#include "Audio/ElysiumSoundScheme.h"

#include "ElysiumAudioSettings.h"
#include "ElysiumAudioSubsystem.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumContentPaths.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumKeyValues.h"
#include "ElysiumKeyValuesLoader.h"
#include "ElysiumPlayer.h"
#include "ElysiumRetailSite.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumClassFields.h"

#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumScheme, Log, All);

// Tunables (debug/calibration). The random-sound cadence base lives in `UElysiumAudioSettings`; these
// two stay cvars -- they are debug A/Bs with a live Cog surface (elysium.MusicState is the Cog Sound
// Schemes window's own echo of combat scoring), not orphan taste values.
static TAutoConsoleVariable<int32> CVarMusicState(
	TEXT("elysium.MusicState"), 0,
	TEXT("Force the SoundScheme music state: 0 = Explore (safe), 1 = Combat, 2 = Alert. The real "
	     "driver is combat scoring (retail FUN_10228a80, L3/L4); this is the debug echo the Cog Sound Schemes window sets."),
	ECVF_Cheat);

static TAutoConsoleVariable<int32> CVarSchemeRandom(
	TEXT("elysium.SchemeRandom"), 1,
	TEXT("Play a scheme's RandomSound polar one-shots (1, default) or suppress them (0)."),
	ECVF_Default);

namespace
{
	constexpr float SchemeInchToCm = 2.54f;

	// Retail constants (`walks/L0-r009.md` § Shared facts, decoded from the PE).
	constexpr float  SchemeFadeFloor      = 0.5f;    // `0x104454d0`: the FadeIn/FadeOut duration floor
	constexpr float  SchemeThinkCadence   = 0.1f;    // `0x104491b4`: `curtime + 0.1` (think re-arm)
	constexpr float  SchemeStopThreshold  = 0.01f;   // `0x10450aa4`: the emit/stop threshold in `FUN_10229090`
	constexpr double SchemeRemoveThreshold = 0.01;   // `0x1044e658` (double): retiring-record removal in `0x10228d00`
	constexpr float  SchemeEvictStart     = 1.1f;    // `0x1048dd2c`: the eviction scan's initial minimum
	constexpr float  SchemeStartFade      = 2.0f;    // the code literal `vfunc113` hands `FUN_1022b590`
	constexpr uint32 SchemePlayingThink   = 0x10003a71u;   // `m_pfnThink` after FadeIn: `jmp FUN_1022b300`
	constexpr int32  SchemeNameCapacity   = 0x40;    // the record's `char[64]` name
	constexpr uint32 SchemeFlagDry        = 0x800u;  // the parser's `Dry` bit
	constexpr uint32 SchemeFlagNoPause    = 0x2000u; // the parser's `NoPause` bit

	const TCHAR* const ManagerSiteName = TEXT("CSoundSchemeManager");   // `DAT_10750d78`, a global with no entity

	// The Source KeyValues reader lives in ElysiumKeyValues.h (shared with the sign
	// definitions); pull its names into this file's anonymous namespace unchanged.
	using ElysiumKeyValues::FKvNode;

	// Parse a Music/Combat/Alert/Ambient block, applying the retail defaults. bDryDefault /
	// bNoPauseDefault differ per block (Ambient NoPause defaults 0; the music trio default 1).
	// `FUN_1022a930`'s per-block reads: `GetString("Filename", default)` (`0x10248cd0`), `GetFloat("Volume",
	// 50)` (`0x10248c60`, not in this run: `Flt` stands in), `GetInt("Dry", 1) != 0`, `GetInt("NoPause",
	// 1|0) != 0` (`0x10248bb0`). The `Filename` default is the empty string for Ambient and Music and
	// the MUSIC record's name for Combat and Alert (`ebp+0x4f0` is the default argument at `0x1022abef`
	// and `0x1022acc7`). The name is stored as the file spells it (`Q_strncpy(..., 0x40)`); an absent
	// block leaves the record as constructed.
	FElysiumSchemeSound ReadSound(FKvNode* Block, const FString& FilenameDefault, bool bDryDefault, bool bNoPauseDefault)
	{
		FElysiumSchemeSound S;
		if (!Block)
		{
			// As constructed: Source zero-fills entity memory and the constructor's own initial volume
			// is UNRECOVERED (`walks/L0-r009.md` Open 12), so the record reads zero. Only its empty name
			// is observable: `FUN_10229430` takes the retire arm on either test.
			S.Volume = 0.f;
			return S;
		}
		S.Filename = ElysiumKeyValues::GetString(*Block, TEXT("Filename"), FilenameDefault).Left(SchemeNameCapacity - 1);
		S.Volume = FMath::Clamp(Block->Flt(TEXT("Volume"), 50.f) / 100.f, 0.f, 1.f);
		S.bDry = ElysiumKeyValues::GetInt(*Block, TEXT("Dry"), bDryDefault ? 1 : 0) != 0;
		S.bNoPause = ElysiumKeyValues::GetInt(*Block, TEXT("NoPause"), bNoPauseDefault ? 1 : 0) != 0;
		S.bValid = true;
		return S;
	}

	// `FUN_102283a0` (`0x102283a0`): move `Current` toward `Target` by `Rate * Dt`, never past it.
	float MoveToward(float Current, float Target, float Rate, float Dt)
	{
		if (Current <= Target)
		{
			if (Target <= Current) { return Target; }
			const float Next = Rate * Dt + Current;
			return Target < Next ? Target : Next;
		}
		const float Next = Current - Rate * Dt;
		return Next < Target ? Target : Next;
	}

	const TCHAR* SlotName(EElysiumSchemeSlot Slot)
	{
		switch (Slot)
		{
		case EElysiumSchemeSlot::Ambient: return TEXT("Ambient");
		case EElysiumSchemeSlot::Music:   return TEXT("Music");
		case EElysiumSchemeSlot::Combat:  return TEXT("Combat");
		default:                          return TEXT("Alert");
		}
	}
}

// FElysiumSoundScheme::ParseFile.

bool FElysiumSoundScheme::ParseFile(const FString& SchemeRel, FElysiumSoundScheme& Out)
{
	// `CSoundScheme::Precache` (`0x1022a4b0`) -> `FUN_1022a930(this, m_sSchemeFile)`: a node named
	// `SoundScheme` (`0x105a63f4`), then the KeyValues file loader `0x102480f0(node, file, DAT_1070b238,
	// pathID 0, cache 1)` -- the `.txt` name takes the text cache `0x102482f0` (pathID 0 open, interned
	// for the process; a second load of the name is a hit). The engine file system resolves the retail
	// name against the deployed corpus (`FElysiumContentPaths::SchemeFile`: `sound/` stripped, lower
	// case). A failed load returns 0 and the scheme stays unparsed.
	ElysiumKeyValuesLoader::FKvDiskFileSystem Fs([](const FString& Name) { return FElysiumContentPaths::SchemeFile(Name); });
	ElysiumKeyValuesLoader::FKvLoadArgs Args;
	Args.FileName = SchemeRel;
	Args.Fs = &Fs;
	Args.PathID = 0;
	Args.Cache = 1;
	const TSharedPtr<FKvNode> Node = MakeShared<FKvNode>();
	Node->Name = TEXT("SoundScheme");
	TArray<TSharedPtr<FKvNode>> Roots;
	if (!ElysiumKeyValuesLoader::LoadFile(Node, Args, Roots))
	{
		return false;
	}
	// `FUN_1022a930` then walks the FIRST root's children (`0x10248a10`, then `0x10248a30`), matching each
	// name with `strstr` against `SchemeParams`, `Ambient`, `Music`, `Combat`, `Alert`, `RandomSound`;
	// no children (an empty file, or a first token with no brace) reads nothing and the call returns 0.
	FKvNode* Root = Node.Get();
	if (Root->Children.IsEmpty())
	{
		return false;
	}

	Out = FElysiumSoundScheme();
	Out.SourcePath = FElysiumContentPaths::SchemeFile(SchemeRel);

	// `SchemeParams`: `GetInt("RandomSoundCount", 2)` clamped 0..6, `GetInt("RoomDSP", 0)` clamped 0..255
	// (`0x10248bb0` with the retail defaults).
	if (FKvNode* Params = ElysiumKeyValues::FindKey(*Root, TEXT("SchemeParams"), ElysiumKeyValues::EKvCreate::No))
	{
		Out.RandomSoundCount = FMath::Clamp(ElysiumKeyValues::GetInt(*Params, TEXT("RandomSoundCount"), 2), 0, 6);
		Out.RoomDSP = FMath::Clamp(ElysiumKeyValues::GetInt(*Params, TEXT("RoomDSP"), 0), 0, 255);
	}

	// The music trio + ambient bed default Dry=1; only Ambient defaults NoPause=0 (the trio default 1).
	// Music first: its name is the Combat and Alert `Filename` default (the shipped files author Music
	// before Combat; a file that authors Combat first would see the empty Music name in retail).
	Out.Music   = ReadSound(ElysiumKeyValues::FindKey(*Root, TEXT("Music"), ElysiumKeyValues::EKvCreate::No),   FString(), /*Dry*/ true, /*NoPause*/ true);
	Out.Combat  = ReadSound(ElysiumKeyValues::FindKey(*Root, TEXT("Combat"), ElysiumKeyValues::EKvCreate::No),  Out.Music.Filename, /*Dry*/ true, /*NoPause*/ true);
	Out.Alert   = ReadSound(ElysiumKeyValues::FindKey(*Root, TEXT("Alert"), ElysiumKeyValues::EKvCreate::No),   Out.Music.Filename, /*Dry*/ true, /*NoPause*/ true);
	Out.Ambient = ReadSound(ElysiumKeyValues::FindKey(*Root, TEXT("Ambient"), ElysiumKeyValues::EKvCreate::No), FString(), /*Dry*/ true, /*NoPause*/ false);

	for (const TSharedPtr<FKvNode>& Kid : Root->Children)
	{
		if (!Kid.IsValid() || Kid->Type != ElysiumKeyValues::EKvType::Block
			|| !Kid->Name.Equals(TEXT("RandomSound"), ESearchCase::IgnoreCase))
		{
			continue;
		}
		FKvNode& B = *Kid;
		FElysiumRandomSound R;
		R.Filename = ElysiumKeyValues::GetString(B, TEXT("Filename"), FString()).Replace(TEXT("\\"), TEXT("/"));
		R.Volume = FMath::Clamp(B.Flt(TEXT("Volume"), 20.f) / 100.f, 0.f, 1.f);
		R.Frequency = ElysiumKeyValues::GetInt(B, TEXT("Frequency"), 10);
		R.PitchMin = ElysiumKeyValues::GetInt(B, TEXT("PitchMin"), 100);
		R.PitchMax = ElysiumKeyValues::GetInt(B, TEXT("PitchMax"), 100);
		R.AudibleRadius = B.Flt(TEXT("AudibleRadius"), 1600.f);
		R.DistMin = B.Flt(TEXT("DistMin"), 800.f);
		R.DistMax = B.Flt(TEXT("DistMax"), 1400.f);
		R.HeightMin = B.Flt(TEXT("HeightMin"), 20.f);
		R.HeightMax = B.Flt(TEXT("HeightMax"), 20.f);
		R.AngleMin = B.Flt(TEXT("AngleMin"), 0.f);
		R.AngleMax = B.Flt(TEXT("AngleMax"), 360.f);
		R.bDry = ElysiumKeyValues::GetInt(B, TEXT("Dry"), 0) != 0;
		R.bNoPause = ElysiumKeyValues::GetInt(B, TEXT("NoPause"), 0) != 0;
		if (!R.Filename.IsEmpty())
		{
			Out.RandomSounds.Add(MoveTemp(R));
		}
	}

	Out.bParsed = true;
	return true;
}

// FElysiumSoundSchemeManager.

const FElysiumSoundScheme* FElysiumSoundSchemeManager::LoadScheme(const FString& SchemeRel)
{
	if (const FElysiumSoundScheme* Cached = SchemeCache.Find(SchemeRel))
	{
		return Cached->bParsed ? Cached : nullptr;
	}
	// AUD0.3: the scheme tables read from the corpus like every other audio byte. SchemeRel is the
	// raw `scheme_file` keyvalue ("sound/Schemes/SP_Tutorial_City.txt"); SchemeFile() strips the
	// leading "sound/" the way retail's own table builder does (FUN_101f3690 @0x101f3690) and folds
	// the rest to the deployed lower-case spelling.
	FElysiumSoundScheme Parsed;
	const bool bOk = FElysiumSoundScheme::ParseFile(SchemeRel, Parsed);
	if (!bOk)
	{
		UE_LOG(LogElysiumScheme, Warning,
			TEXT("scheme '%s' not in the corpus at %s (run `uv run elysium import sound-schemes`)"),
			*SchemeRel, *FElysiumContentPaths::SchemeFile(SchemeRel));
	}
	const FElysiumSoundScheme& Stored = SchemeCache.Add(SchemeRel, MoveTemp(Parsed));
	return Stored.bParsed ? &Stored : nullptr;
}

int32 FElysiumSoundSchemeManager::RegisterScheme(const FElysiumEntity& Scheme, FElysiumEntityWorld* World)
{
	// `FUN_102289e0` (`0x102289e0`): `h = *scheme->vfn[1]()` (slot 1, `GetRefEHandle`); grow the list
	// through `FUN_1022d380` when `+0x1a0 < +0x1a8 + 1` (first capacity 8, then doubling); `+0x1ac =
	// base`; `+0x1a8 = count + 1`; a zero-length memmove; `base[count] = h`. No duplicate check: a
	// second Activate lists the handle twice (each copy is disabled by `FUN_10229270`, harmlessly).
	Schemes.Add(Scheme.Handle);
	(void)World;
	return Schemes.Num();
}

void FElysiumSoundSchemeManager::FadeInScheme(UElysiumAudioSubsystem* Audio, FElysiumEntityWorld* World,
	const FString& SchemeRel, const FVector& Anchor, float Duration)
{
	// `FUN_10229270(&DAT_10750d78, duration, scheme+0x460, +0x4f0, +0x4a8, +0x538)` (`0x10229270`),
	// `FUN_1022b590`'s arm 5. The scheme's four records are its parse; a scheme whose file did not load
	// has every record as constructed (empty name, volume 0), so all four slots take the retire arm.
	const FElysiumSoundScheme* Scheme = LoadScheme(SchemeRel);
	static const FElysiumSoundScheme EmptyScheme;
	const FElysiumSoundScheme& S = Scheme ? *Scheme : EmptyScheme;

	TOptional<FElysiumNamedRetailSites> Named;
	if (World != nullptr) { Named.Emplace(*World, ManagerSiteName); }
	IElysiumRetailSiteSink* Sites = Named.IsSet() ? &Named.GetValue() : nullptr;

	// Arm 1, the prune loop over the EHANDLE list (`+0x19c` base, `+0x1a8` count), a `do/while`
	// entered only when the count is positive. `h == 0xffffffff`, a serial mismatch at
	// `PTR_DAT_10566458[h & 0x1fff].serial != h >> 13`, or a NULL entity pointer: remove the handle
	// (`memmove` the tail down, `+0x1a8 -= 1`, index not advanced). Otherwise `FUN_1022b660(entity)`
	// and advance. The `else` branch's second `0xffffffff || serial` test (which would call
	// `FUN_1022b660(NULL)`) is unreachable: the same tests already took the prune branch. The port's
	// handle carries the same two words (index, epoch) and `Resolve` answers null for a dead entity.
	if (World != nullptr)
	{
		int32 Index = 0;
		while (Index < Schemes.Num())
		{
			FElysiumEntity* Listed = World->Resolve(Schemes[Index]);
			if (Listed == nullptr)
			{
				const FElysiumEntityHandle Pruned = Schemes[Index];
				Schemes.RemoveAt(Index);
				if (Sites)
				{
					Sites->Site(TEXT("mgr_prune"), TEXT("FUN_10229270"), 0x10229270u, TEXT("prune"),
						FString::Printf(TEXT("handle=#%d count=%d"), Pruned.Index, Schemes.Num()));
				}
				continue;
			}
			if (Sites)
			{
				Sites->Site(TEXT("mgr_prune"), TEXT("FUN_10229270"), 0x10229270u, TEXT("disable"),
					FString::Printf(TEXT("handle=#%d name=%s count=%d"), Listed->Handle.Index, *Listed->TargetName, Schemes.Num()));
			}
			ElysiumSoundSchemeDisable(*Listed);   // `thunk_FUN_1022b660`
			++Index;
		}
	}

	// Arm 2, the four `FUN_10229430` calls in retail's order: `+0x14` from `+0x460` (Ambient), `+0xc4`
	// from `+0x4a8` (Combat), `+0x6c` from `+0x4f0` (Music), `+0x11c` from `+0x538` (Alert) -- the
	// scheme-record order, not the slot order. The order matters: the channel allocator is order-
	// dependent and a slot retired earlier holds its channel against a slot configured later.
	ConfigureTrack(Audio, Sites, S.Ambient, Duration, EElysiumSchemeSlot::Ambient);
	ConfigureTrack(Audio, Sites, S.Combat,  Duration, EElysiumSchemeSlot::Combat);
	ConfigureTrack(Audio, Sites, S.Music,   Duration, EElysiumSchemeSlot::Music);
	ConfigureTrack(Audio, Sites, S.Alert,   Duration, EElysiumSchemeSlot::Alert);

	// The port's random scheduler takes the new scheme's RandomSound blocks and anchor. In-flight
	// one-shots are the manager's live list (`+0x188`, counted against RandomSoundCount) and carry over;
	// retail never stops a one-shot on a switch.
	TArray<FElysiumAudioVoiceHandle> Carried = MoveTemp(Active.RandomVoices);
	Active = FActiveScheme();
	Active.SchemeRel = SchemeRel;
	Active.Anchor = Anchor;
	Active.RandomVoices = MoveTemp(Carried);
	if (Scheme)
	{
		Active.Scheme = *Scheme;
		Active.RandomNextTime.SetNum(Scheme->RandomSounds.Num());
		for (int32 i = 0; i < Scheme->RandomSounds.Num(); ++i)
		{
			const float Mean = GetDefault<UElysiumAudioSettings>()->SchemeRandomBase * 10.f / FMath::Max(1, Scheme->RandomSounds[i].Frequency);
			Active.RandomNextTime[i] = Elapsed + FMath::FRandRange(0.2f, 1.0f) * Mean;
		}
	}
	UE_LOG(LogElysiumScheme, Log, TEXT("scheme switch '%s' (%s) duration %.2fs: Ambient ch%d Music ch%d Combat ch%d Alert ch%d, retiring %d"),
		*SchemeRel, Scheme ? TEXT("parsed") : TEXT("MISSING assets"), Duration,
		Tracks[0].Channel, Tracks[1].Channel, Tracks[2].Channel, Tracks[3].Channel, Retiring.Num());
}

void FElysiumSoundSchemeManager::PrimeScheme(
	UElysiumAudioSubsystem* Audio, const FString& SchemeRel)
{
	const FElysiumSoundScheme* Scheme = Audio ? LoadScheme(SchemeRel) : nullptr;
	if (!Scheme)
	{
		return;
	}
	auto Prime = [Audio](const FElysiumSchemeSound& Sound)
	{
		if (Sound.IsSet())
		{
			Audio->Prefetch(FElysiumAudioSource::Path(Sound.Filename));
		}
	};
	Prime(Scheme->Ambient);
	Prime(Scheme->Music);
	Prime(Scheme->Combat);
	Prime(Scheme->Alert);
	for (const FElysiumRandomSound& Random : Scheme->RandomSounds)
	{
		if (!Random.Filename.IsEmpty())
		{
			Audio->Prefetch(FElysiumAudioSource::Path(Random.Filename));
		}
	}
}

void FElysiumSoundSchemeManager::FadeOutScheme(UElysiumAudioSubsystem* Audio, FElysiumEntityWorld* World,
	const FString& SchemeRel, float Duration)
{
	// `FUN_102293f0(&DAT_10750d78, duration)` (`0x102293f0`), `FUN_1022b520`'s one manager call: retire
	// the four slots in slot order, `+0x14`, `+0x6c`, `+0xc4`, `+0x11c`.
	(void)Audio;
	TOptional<FElysiumNamedRetailSites> Named;
	if (World != nullptr) { Named.Emplace(*World, ManagerSiteName); }
	IElysiumRetailSiteSink* Sites = Named.IsSet() ? &Named.GetValue() : nullptr;
	RetireTrack(Sites, EElysiumSchemeSlot::Ambient, Duration);
	RetireTrack(Sites, EElysiumSchemeSlot::Music,   Duration);
	RetireTrack(Sites, EElysiumSchemeSlot::Combat,  Duration);
	RetireTrack(Sites, EElysiumSchemeSlot::Alert,   Duration);
	// The caller cleared its PlayingThink (`ThinkSet(0)`), so no scheme schedules random one-shots
	// until the next FadeIn: the port's scheduler drops its scheme (in-flight one-shots finish).
	Active.Scheme = FElysiumSoundScheme();
	Active.SchemeRel.Reset();
	Active.RandomNextTime.Reset();
	UE_LOG(LogElysiumScheme, Log, TEXT("scheme FadeOut '%s' duration %.2fs: retiring %d"), *SchemeRel, Duration, Retiring.Num());
}

void FElysiumSoundSchemeManager::ConfigureTrack(UElysiumAudioSubsystem* Audio, IElysiumRetailSiteSink* Sites,
	const FElysiumSchemeSound& Record, float Duration, EElysiumSchemeSlot Slot)
{
	// `FUN_10229430(this, name, volume, flags, duration, track)` (`0x10229430`). `name` is the scheme
	// record's `+0x00`, `volume` its `+0x40`, `flags` its `+0x44`.
	FElysiumSchemeTrack& Track = Tracks[static_cast<int32>(Slot)];
	const float Volume = Record.Volume;
	const uint32 Flags = Record.Flags();
	const FString& Name = Record.Filename;
	const int32 Offset = SlotOffset(Slot);

	// Arm 1: `volume == 0.0` (`FCOMP qword [0x1044fab0]`, a double zero; equal only, a NaN does not
	// match) `|| name == NULL || *name == 0` -> `FUN_10229550(this, track, duration)` and return.
	if (static_cast<double>(Volume) == 0.0 || Name.IsEmpty())
	{
		if (Sites)
		{
			Sites->Site(TEXT("track_set"), TEXT("FUN_10229430"), 0x10229430u, TEXT("retire_sentinel"),
				FString::Printf(TEXT("slot=0x%x name=%s volume=%g duration=%g"), Offset, *Name, Volume, Duration));
		}
		RetireTrack(Sites, Slot, Duration);
		return;
	}
	// Arm 2: `0.0f < volume` (`0x104454c4`; `AND EAX,0x4100; JNZ` returns on `<=` or NaN).
	if (0.0f < Volume)
	{
		// 2a: `Q_strnicmp(name, track, 0x40) == 0` -- the same file on the slot: retarget in place.
		// `+0x48 = volume`, `+0x40 = volume`, `+0x54 = flags`, `+0x4c = volume / duration`; the channel,
		// the name and the ramped level `+0x44` are kept; nothing is retired. The stored name is at most
		// 63 characters, so a 64-or-longer name never matches its own truncation, as in retail.
		if (!Track.IsEmpty() && Track.Name.Equals(Name.Left(SchemeNameCapacity), ESearchCase::IgnoreCase))
		{
			Track.Authored = Volume;
			Track.Target = Volume;
			Track.Flags = Flags;
			Track.Rate = Volume / Duration;
			if (Sites)
			{
				Sites->Site(TEXT("track_set"), TEXT("FUN_10229430"), 0x10229430u, TEXT("same"),
					FString::Printf(TEXT("slot=0x%x name=%s volume=%g duration=%g rate=%g channel=%d current=%g flags=0x%x"),
						Offset, *Name, Volume, Duration, Track.Rate, Track.Channel, Track.Current, Flags));
			}
			return;
		}
		// 2b: a different file. Retire the old contents first (the retired record keeps its channel,
		// so the allocator below excludes it), `Q_strncpy(track, name, 0x40)`, `+0x48`, `+0x40`,
		// `+0x4c = volume / duration`, `+0x50 = FUN_102297a0(this)`, `+0x54 = flags`. `+0x44` is not
		// written: a retired slot was zeroed, an empty one was already zero, so the new track ramps
		// from 0 toward `volume` at `volume / duration` per second through `FUN_10229090`.
		RetireTrack(Sites, Slot, Duration);
		Track.Name = Name.Left(SchemeNameCapacity - 1);
		Track.Authored = Volume;
		Track.Target = Volume;
		Track.Rate = Volume / Duration;
		Track.Channel = AllocChannel(Audio, Sites);
		Track.Flags = Flags;
		if (Sites)
		{
			Sites->Site(TEXT("track_set"), TEXT("FUN_10229430"), 0x10229430u, TEXT("replace"),
				FString::Printf(TEXT("slot=0x%x name=%s volume=%g duration=%g rate=%g channel=%d flags=0x%x"),
					Offset, *Name, Volume, Duration, Track.Rate, Track.Channel, Flags));
		}
		return;
	}
	// Arm 3: a negative or NaN volume writes nothing.
	if (Sites)
	{
		Sites->Site(TEXT("track_set"), TEXT("FUN_10229430"), 0x10229430u, TEXT("skip"),
			FString::Printf(TEXT("slot=0x%x name=%s volume=%g"), Offset, *Name, Volume));
	}
}

void FElysiumSoundSchemeManager::RetireTrack(IElysiumRetailSiteSink* Sites, EElysiumSchemeSlot Slot, float Duration)
{
	// `FUN_10229550(this, track, duration)` (`0x10229550`).
	FElysiumSchemeTrack& Track = Tracks[static_cast<int32>(Slot)];
	const int32 Offset = SlotOffset(Slot);
	// Arm 1: `track[0] == 0` -> return. An empty track is never retired.
	if (Track.IsEmpty())
	{
		if (Sites)
		{
			Sites->Site(TEXT("track_retire"), TEXT("FUN_10229550"), 0x10229550u, TEXT("empty"),
				FString::Printf(TEXT("slot=0x%x retiring=%d"), Offset, Retiring.Num()));
		}
		return;
	}
	// Arm 2: `fVar1 = +0x48` (the authored volume, read first); `+0x40 = 0.0f`; `+0x4c = fVar1 /
	// duration` (the outgoing rate: full-scale volume over the INCOMING duration, so the fade from the
	// level actually reached takes `duration * (+0x44 / +0x48)` seconds); append the 0x58-byte record
	// to the retiring list (`+0x174` base, `+0x178` capacity doubling from 1 through `Plat_Alloc` /
	// `Plat_Realloc`, `+0x180 = count + 1`, `+0x184 = base`, a zero-length memmove: a pure append);
	// then zero the track's 22 dwords -- the name, target, `+0x44`, `+0x48`, rate, channel and flags.
	// The copy carries the ramped `+0x44`, the old channel and the old flags (and, here, the voice).
	const float Authored = Track.Authored;
	Track.Target = 0.0f;
	Track.Rate = Authored / Duration;
	Retiring.Add(Track);
	if (Sites)
	{
		Sites->Site(TEXT("track_retire"), TEXT("FUN_10229550"), 0x10229550u, TEXT("append"),
			FString::Printf(TEXT("slot=0x%x name=%s current=%g authored=%g rate=%g channel=%d retiring=%d"),
				Offset, *Track.Name, Track.Current, Authored, Track.Rate, Track.Channel, Retiring.Num()));
	}
	Track = FElysiumSchemeTrack();
	if (Sites)
	{
		Sites->Site(TEXT("track_retire"), TEXT("FUN_10229550"), 0x10229550u, TEXT("clear"),
			FString::Printf(TEXT("slot=0x%x"), Offset));
	}
}

int32 FElysiumSoundSchemeManager::AllocChannel(UElysiumAudioSubsystem* Audio, IElysiumRetailSiteSink* Sites)
{
	// `FUN_102297a0(this)` (`0x102297a0`, `__fastcall`). Channels 1..6 in order: the first one no
	// retiring record (`+0x174` list, element `+0x50`) and no live track (`+0x64`, `+0xbc`, `+0x114`,
	// `+0x16c`) holds is returned.
	for (int32 Channel = 1; Channel < 7; ++Channel)
	{
		bool bHeld = false;
		for (const FElysiumSchemeTrack& R : Retiring) { if (R.Channel == Channel) { bHeld = true; break; } }
		for (int32 i = 0; !bHeld && i < 4; ++i) { if (Tracks[i].Channel == Channel) { bHeld = true; } }
		if (!bHeld)
		{
			if (Sites)
			{
				Sites->Site(TEXT("chan_alloc"), TEXT("FUN_102297a0"), 0x102297a0u, TEXT("free"),
					FString::Printf(TEXT("channel=%d retiring=%d"), Channel, Retiring.Num()));
			}
			return Channel;
		}
	}
	// All six held: scan the retiring list for the record with the smallest `+0x44` strictly below 1.1
	// (`0x1048dd2c`; the first on ties, index 0 when none is below it), stop its sound through
	// `IEngineSoundServer003` slot 7 `(0, channel, record)`, remove it (memmove, `+0x180 -= 1`) and
	// return its channel. The eviction key is the QUIETEST retiring record, not index 0. Four live
	// slots can hold at most four channels, so the list is never empty here; the guard is the port's.
	if (Retiring.IsEmpty())
	{
		return 0;
	}
	int32 Best = 0;
	float Quietest = SchemeEvictStart;
	for (int32 i = 0; i < Retiring.Num(); ++i)
	{
		if (Retiring[i].Current < Quietest)
		{
			Quietest = Retiring[i].Current;
			Best = i;
		}
	}
	const int32 Channel = Retiring[Best].Channel;
	StopTrackVoice(Audio, Retiring[Best]);
	if (Sites)
	{
		Sites->Site(TEXT("chan_alloc"), TEXT("FUN_102297a0"), 0x102297a0u, TEXT("evict"),
			FString::Printf(TEXT("channel=%d index=%d name=%s current=%g retiring=%d"),
				Channel, Best, *Retiring[Best].Name, Retiring[Best].Current, Retiring.Num() - 1));
	}
	Retiring.RemoveAt(Best);
	return Channel;
}

void FElysiumSoundSchemeManager::MusicStateGate(int32 State)
{
	// `FUN_10228e80(this, state)` (`0x10228e80`), run every frame by `0x10228d00` before the ramps.
	// State 1: Music target (`+0xac`) <- its authored `+0xb4`, Alert (`+0x15c`) and Combat (`+0x104`)
	// targets <- 0. State 2: Combat <- `+0x10c`, Music and Alert <- 0. State 3: Alert <- `+0x164`,
	// Music and Combat <- 0. Ambient (`+0x14`) is never gated. The engine call through
	// `DAT_1070b22c` vfn `+0x1ec` (`1.0` for states 1 and 3, `0.92` for state 2) and, on a state change
	// with a live player, the `FUN_100cd660` calls on members `+0x5b8..+0x630` of `FUN_1023dcd0()`'s
	// object are not walked (`walks/L0-r009.md` Open 12): named seams, nothing emitted for them.
	FElysiumSchemeTrack& Music  = Tracks[static_cast<int32>(EElysiumSchemeSlot::Music)];
	FElysiumSchemeTrack& Combat = Tracks[static_cast<int32>(EElysiumSchemeSlot::Combat)];
	FElysiumSchemeTrack& Alert  = Tracks[static_cast<int32>(EElysiumSchemeSlot::Alert)];
	if (State == 1)
	{
		Music.Target = Music.Authored;
		Alert.Target = 0.f;
		Combat.Target = 0.f;
	}
	else if (State == 2)
	{
		Combat.Target = Combat.Authored;
		Music.Target = 0.f;
		Alert.Target = 0.f;
	}
	else if (State == 3)
	{
		Alert.Target = Alert.Authored;
		Music.Target = 0.f;
		Combat.Target = 0.f;
	}
	MusicStateWord = State;   // `+0x10`
}

void FElysiumSoundSchemeManager::StopTrackVoice(UElysiumAudioSubsystem* Audio, FElysiumSchemeTrack& Track)
{
	// `IEngineSoundServer003` slot 7 `(0, channel +0x50, record)` (`engine.dll 0x20001f20`): slot 2 with
	// volume 0 and the stop flag `4` -- the sound on that channel stops now.
	if (Audio && Track.Voice.IsValid())
	{
		Audio->StopVoice(Track.Voice, 0.f);
	}
	Track.Voice = FElysiumAudioVoiceHandle::Invalid();
}

void FElysiumSoundSchemeManager::RampTrack(UElysiumAudioSubsystem* Audio, FElysiumSchemeTrack& Track,
	EElysiumSchemeSlot Slot, float DeltaSeconds)
{
	// `FUN_10229090(track, dt)` (`0x10229090`). The whole body is inside `if (+0x44 != +0x40)`: a
	// record at its target neither emits nor stops that frame.
	if (Track.Current == Track.Target)
	{
		return;
	}
	float Level = MoveToward(Track.Current, Track.Target, Track.Rate, DeltaSeconds);   // `FUN_102283a0`
	// Clamp to `[0.0, +0x48]`: `if (v <= authored) { if (v < 0.0) v = 0.0 } else v = authored`.
	Level = (Level <= Track.Authored) ? FMath::Max(Level, 0.0f) : Track.Authored;
	Track.Current = Level;
	// `+0x44 <= 0.01 && +0x40 <= 0.01` (`0x10450aa4`): the stop (slot 7) and return.
	if (Level <= SchemeStopThreshold && Track.Target <= SchemeStopThreshold)
	{
		StopTrackVoice(Audio, Track);
		return;
	}
	// Otherwise, with a local player (`FUN_101cd9e0(1)`), emit through slot 2 (`engine.dll
	// 0x20001b90`) on a reliable single-user filter: channel `+0x50`, the record as the name, volume
	// `+0x44`, flags `+0x54 | 0x101` (the parameter struct is not walked). The port's engine sound is
	// the voice: started at the level on the first emit, re-levelled on each later one. `Dry` (`0x800`)
	// and `NoPause` (`0x2000`) ride as the mixer's routing bits.
	if (Audio == nullptr)
	{
		return;
	}
	if (Track.Voice.IsValid() && Audio->IsVoicePlaying(Track.Voice))
	{
		Audio->SetVoiceVolume(Track.Voice, Level, 0.f);
		return;
	}
	FElysiumAudioRequest Request;
	Request.Source = FElysiumAudioSource::Path(Track.Name);
	Request.Owner = { EElysiumAudioOwnerKind::GameplaySystem,
		FString::Printf(TEXT("scheme:%s"), SlotName(Slot)), MapEpoch };
	Request.Category = Slot == EElysiumSchemeSlot::Ambient ? EElysiumAudioCategory::Ambience : EElysiumAudioCategory::Music;
	Request.Gain = Level;
	Request.bLooping = true;
	Request.Placement.bSpatialized = false;
	if ((Track.Flags & SchemeFlagDry) != 0)     { Request.Routing |= EElysiumAudioRouting::Dry; }
	if ((Track.Flags & SchemeFlagNoPause) != 0) { Request.Routing |= EElysiumAudioRouting::NoPause; }
	Track.Voice = Audio->Submit(Request);
}

void FElysiumSoundSchemeManager::SetMusicState(UElysiumAudioSubsystem* Audio, EElysiumMusicState NewState, float CrossfadeSeconds)
{
	// The state word the gate reads next frame. The crossfade argument is the old port's; retail's
	// ramp rate is each record's own `+0x4c`, so the argument is accepted and not read.
	(void)Audio;
	(void)CrossfadeSeconds;
	CurrentMusicState = NewState;
}

void FElysiumSoundSchemeManager::TickRandom(UElysiumAudioSubsystem* Audio, const FVector& /*PlayerLoc*/)
{
	if (!Audio || !Active.IsSet() || CVarSchemeRandom.GetValueOnGameThread() == 0)
	{
		return;
	}
	const FElysiumSoundScheme& S = Active.Scheme;

	// Prune finished one-shots.
	Active.RandomVoices.RemoveAll([Audio](const FElysiumAudioVoiceHandle& H) { return !Audio->IsVoicePlaying(H); });

	const int32 Cap = FMath::Max(0, S.RandomSoundCount);
	const float Base = GetDefault<UElysiumAudioSettings>()->SchemeRandomBase;

	for (int32 i = 0; i < S.RandomSounds.Num(); ++i)
	{
		if (Elapsed < Active.RandomNextTime[i])
		{
			continue;
		}
		const FElysiumRandomSound& R = S.RandomSounds[i];
		const float Mean = Base * 10.f / FMath::Max(1, R.Frequency);

		if (Active.RandomVoices.Num() < Cap)
		{
			// Polar placement around the anchor (Source units → cm). Angle arc wraps when Max < Min.
			const float Dist = FMath::FRandRange(R.DistMin, R.DistMax) * SchemeInchToCm;
			const float Height = FMath::FRandRange(R.HeightMin, R.HeightMax) * SchemeInchToCm;
			float AMin = R.AngleMin, AMax = R.AngleMax;
			if (AMax < AMin) { AMax += 360.f; }
			const float Ang = FMath::DegreesToRadians(FMath::FRandRange(AMin, AMax));
			const FVector Loc = Active.Anchor + FVector(FMath::Cos(Ang) * Dist, FMath::Sin(Ang) * Dist, Height);

			FElysiumAudioRequest Request;
			Request.Source = FElysiumAudioSource::Path(R.Filename);
			Request.Owner = { EElysiumAudioOwnerKind::GameplaySystem,
				FString::Printf(TEXT("scheme:%s"), *Active.SchemeRel), MapEpoch };
			Request.Category = EElysiumAudioCategory::Ambience;
			Request.Gain = R.Volume;
			Request.Pitch = FMath::FRandRange((float)R.PitchMin, (float)R.PitchMax) / 100.f;
			Request.Placement.bSpatialized = true;
			Request.AttenuationRadiusCm = R.AudibleRadius * SchemeInchToCm;
			Request.Placement.Location = Loc;
			Request.ConcurrencyKey = TEXT("scheme.random");
			const FElysiumAudioVoiceHandle H = Audio->Submit(Request);
			if (H.IsValid())
			{
				Active.RandomVoices.Add(H);
			}
			Active.RandomNextTime[i] = Elapsed + FMath::FRandRange(0.5f, 1.5f) * Mean;
		}
		else
		{
			// At the concurrency cap — retry soon rather than skipping this sound's whole interval.
			Active.RandomNextTime[i] = Elapsed + 0.5f * Mean;
		}
	}
}

void FElysiumSoundSchemeManager::Tick(UElysiumAudioSubsystem* Audio, FElysiumEntityWorld* World, const FVector& PlayerLoc, float DeltaSeconds)
{
	// `CSoundSchemeManager::FrameUpdatePostEntityThink` (`0x10228d00`): `dt = curtime - [+0x0c]`,
	// `[+0x0c] = curtime` (the frame's delta, handed in here); `state = FUN_10228a80()`;
	// `FUN_10228e80(this, state)`; `FUN_10229090(track, dt)` on the four live records in slot order,
	// then on each retiring record, removing it when its `+0x44 <= 0.01` (double `0x1044e658`; the
	// index is not advanced); the RandomSound expiry sweep (`+0x188`, the port's scheduler prunes its
	// finished one-shots); `FUN_10229c50(this)` closes the frame (not read, `walks/L0-r009.md` Open 12).
	Elapsed += FMath::Max(0.f, DeltaSeconds);

	// MODERNIZATION (named): `FUN_10228a80` reads the local player's conditions (L3/L4) for 1, 2 or
	// 3; the cvar `elysium.MusicState` (the Cog window / a console set) is that state here.
	const int32 Cv = FMath::Clamp(CVarMusicState.GetValueOnGameThread(), 0, 2);
	if (Cv != static_cast<int32>(CurrentMusicState))
	{
		SetMusicState(Audio, static_cast<EElysiumMusicState>(Cv));
	}
	MusicStateGate(static_cast<int32>(CurrentMusicState) + 1);

	for (int32 i = 0; i < 4; ++i)
	{
		RampTrack(Audio, Tracks[i], static_cast<EElysiumSchemeSlot>(i), DeltaSeconds);
	}
	for (int32 i = 0; i < Retiring.Num();)
	{
		RampTrack(Audio, Retiring[i], EElysiumSchemeSlot::Ambient, DeltaSeconds);
		if (static_cast<double>(Retiring[i].Current) <= SchemeRemoveThreshold)
		{
			// Retail only drops the record (its sound stopped in the ramp the frame the level crossed
			// 0.01 with a zero target); releasing a voice handle that may still be live is the port's own
			// housekeeping of an engine sound at or below the threshold.
			StopTrackVoice(Audio, Retiring[i]);
			if (World != nullptr)
			{
				World->EmitRetailSite(FString(ManagerSiteName), TEXT("mgr_frame"), TEXT("CSoundSchemeManager::FrameUpdatePostEntityThink"),
					0x10228d00u, TEXT("remove"), FString::Printf(TEXT("name=%s channel=%d current=%g retiring=%d"),
						*Retiring[i].Name, Retiring[i].Channel, Retiring[i].Current, Retiring.Num() - 1));
			}
			Retiring.RemoveAt(i);
			continue;
		}
		++i;
	}

	TickRandom(Audio, PlayerLoc);
}

void FElysiumSoundSchemeManager::StopAll(UElysiumAudioSubsystem* Audio)
{
	// Map unload: `LevelShutdownPostEntity` (`0x102287a0`) frees the retiring, EHANDLE and RandomSound
	// lists; `LevelInitPreEntity` (`0x10228700`) zeroes the four live records on the next map. The
	// port stops the engine sounds those records addressed as it drops them.
	if (Audio)
	{
		for (FElysiumSchemeTrack& T : Tracks) { StopTrackVoice(Audio, T); }
		for (FElysiumSchemeTrack& T : Retiring) { StopTrackVoice(Audio, T); }
		for (const FElysiumAudioVoiceHandle& H : Active.RandomVoices) { Audio->StopVoice(H, 0.f); }
	}
	for (FElysiumSchemeTrack& T : Tracks) { T = FElysiumSchemeTrack(); }
	Retiring.Reset();
	Schemes.Reset();
	Active = FActiveScheme();
}

// ambient_soundscheme -- `CSoundScheme` (`docs/vtmb/entity_io.md`: FadeIn 205 / FadeOut 179).
//
// Fields: `+0x450` `m_sSchemeFile` (key `scheme_file`), `+0x454` `m_bStartEnabled` (datamap, key
// `start_enabled`), `+0x455` the playing flag (no ledger or datamap name; `vfunc129 0x1022a350` copies it
// into `+0x454` on save), `+0x118` `m_pfnThink` (0, or `0x10003a71` = `jmp FUN_1022b300`, PlayingThink),
// `+0x17c` `m_flNextThink` (the base `NextThink`). The scheme's four records (`+0x460..+0x57c`) are the
// manager's cached parse of `m_sSchemeFile`.
class FElysiumAmbientSoundscheme final : public FElysiumEntity
{
public:
	FString SchemeFile;          // +0x450 m_sSchemeFile
	bool    bStartEnabled = false;   // +0x454 m_bStartEnabled
	bool    bPlaying = false;        // +0x455
	uint32  ThinkFn = 0;             // +0x118 m_pfnThink: 0 or 0x10003a71

	// `CSoundScheme::vfunc113` (`0x1022a300`), slot 113 = `CBaseEntity::Activate`, run once by the level's
	// Activate pass (and immediately after PostSpawn for a runtime spawn in an active world). The
	// override does NOT chain `CBaseEntity::Activate` (`0x100a0bc0`). There is no guard: a second call
	// registers a second copy of the handle.
	virtual void Activate() override
	{
		IElysiumAudio* Audio = World ? World->Audio() : nullptr;
		// Arm 1, always: `thunk_FUN_102289e0(&DAT_10750d78, this)` -- the EHANDLE append.
		const int32 Registered = Audio ? Audio->RegisterScheme(*this) : 0;
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("scheme_activate"), TEXT("CSoundScheme::vfunc113"), 0x1022a300u, TEXT("register"),
				FString::Printf(TEXT("count=%d m_bStartEnabled=%d"), Registered, bStartEnabled ? 1 : 0));
		}
		// Arm 2: `if (m_bStartEnabled != 0) { FUN_1022b590(this, 2.0f); return; }` -- the 2.0 is a code literal.
		if (bStartEnabled)
		{
			if (World != nullptr)
			{
				World->EmitRetailSite(*this, TEXT("scheme_activate"), TEXT("CSoundScheme::vfunc113"), 0x1022a300u, TEXT("start"),
					FString::Printf(TEXT("duration=%g"), SchemeStartFade));
			}
			FadeIn(SchemeStartFade);
			return;
		}
		// Arm 3: `CBaseEntity::ThinkSet(this, 0, 0.0, NULL)` (`0x100ac4e0`): with a NULL context this is
		// `m_pfnThink = 0; return` -- the time argument is ignored, `m_flNextThink` and `+0x455` are left
		// alone (an Activate re-run with `start_enabled 0` on a playing scheme removes its PlayingThink
		// while `+0x455` stays 1).
		ThinkFn = 0;
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("scheme_activate"), TEXT("CSoundScheme::vfunc113"), 0x1022a300u, TEXT("clear_think"),
				FString::Printf(TEXT("m_pfnThink=0 +0x455=%d"), bPlaying ? 1 : 0));
		}
	}

	// `FUN_1022b590(this, duration)` (`0x1022b590`), reached from `InputFadeIn` and from `vfunc113`.
	void FadeIn(float Duration)
	{
		// Arm 1: `if (duration < 0.5f) duration = 0.5f` (`FCOMP dword [0x104454d0]; TEST AH,5; JP`): a
		// NaN passes unchanged, 0.5 stays, and 0.0, negatives and (0, 0.5) all become 0.5. The clamp
		// runs before the guard, so it is also computed and discarded on the guard path.
		if (Duration < SchemeFadeFloor)
		{
			Duration = SchemeFadeFloor;
		}
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("scheme_fadein"), TEXT("FUN_1022b590"), 0x1022b590u, TEXT("entry"),
				FString::Printf(TEXT("duration=%g +0x455=%d"), Duration, bPlaying ? 1 : 0));
		}
		// Arm 2: `if (+0x455 != 0) return` -- the repeat-activation guard, per entity. Writes nothing.
		if (bPlaying)
		{
			if (World != nullptr)
			{
				World->EmitRetailSite(*this, TEXT("scheme_fadein"), TEXT("FUN_1022b590"), 0x1022b590u, TEXT("guard"), TEXT("+0x455=1"));
			}
			return;
		}
		// Arms 3-4: `this_00 = UTIL_GetLocalPlayer()` (`0x101cda50`: the player only when `gpGlobals->
		// maxClients < 2`, the game rules exist and their slot 21 answers 0 -- `0x101abcc0`, `return 0`
		// in `CHalfLife2` -- and `FUN_101cd9e0(1)` yields the entity); if non-null,
		// `FUN_10175360(player, this)`. The write is the L3 player's: see the hook below.
		HookL3PlayerSetSoundScheme();
		// Arm 5: `FUN_10229270(&DAT_10750d78, duration, +0x460, +0x4f0, +0x4a8, +0x538)` -- the manager
		// switch. It disables every registered scheme, this one included when it is listed, and
		// configures the four tracks. Arms 6-8 then re-arm this scheme.
		if (IElysiumAudio* Audio = World ? World->Audio() : nullptr)
		{
			Audio->FadeInScheme(SchemeFile, Origin, Duration);
		}
		// Arm 6: `ThinkSet(this, &LAB_10003a71, 0.0, NULL)` -> `m_pfnThink = 0x10003a71` (`jmp FUN_1022b300`,
		// PlayingThink). Arm 7: `+0x455 = 1`. Arm 8: `+0x17c = curtime + 0.1f` (`FLD [gpGlobals+0xc];
		// FADD dword [0x104491b4]; FSTP dword [ESI+0x17c]`, a float store at `0x1022b60f`).
		ThinkFn = SchemePlayingThink;
		bPlaying = true;
		const double CurTime = World ? World->NowSeconds() : 0.0;   // `gpGlobals->curtime`
		NextThink = static_cast<float>(CurTime) + SchemeThinkCadence;
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("scheme_fadein"), TEXT("FUN_1022b590"), 0x1022b590u, TEXT("arm"),
				FString::Printf(TEXT("m_pfnThink=0x%08x +0x455=1 m_flNextThink=%g"), ThinkFn, NextThink));
		}
	}

	// `FUN_1022b520(duration)` (`0x1022b520`), reached from `InputFadeOut` (`0x1022b4d0`): the same 0.5
	// floor (`0x104454d0`), return if `+0x455 == 0`, else `FUN_102293f0(&DAT_10750d78, duration)` (retire
	// all four slots), `ThinkSet(0)`, `+0x455 = 0`.
	void FadeOut(float Duration)
	{
		if (Duration < SchemeFadeFloor)
		{
			Duration = SchemeFadeFloor;
		}
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("scheme_fadeout"), TEXT("FUN_1022b520"), 0x1022b520u, TEXT("entry"),
				FString::Printf(TEXT("duration=%g +0x455=%d"), Duration, bPlaying ? 1 : 0));
		}
		if (!bPlaying)
		{
			return;
		}
		if (IElysiumAudio* Audio = World ? World->Audio() : nullptr)
		{
			Audio->FadeOutScheme(SchemeFile, Duration);
		}
		ThinkFn = 0;
		bPlaying = false;
	}

	// `FUN_1022b660(scheme)` (`0x1022b660`, `__fastcall`), the manager's disable of every registered
	// scheme during a switch: `ThinkSet(0)` (`m_pfnThink = 0`) and `+0x455 = 0`. `m_flNextThink` is left
	// as it was; the next due think dispatches to a NULL callback and does nothing (`PhysicsRunSpecificThink
	// 0x10033de0` clears the deadline, `CBaseEntity::Think 0x10026c70` runs `m_pfnThink` only when set).
	void Disable()
	{
		ThinkFn = 0;
		bPlaying = false;
	}

	// The think dispatch: `m_pfnThink == 0` is a no-op (the gate still consumed the deadline).
	// `FUN_1022b300` (PlayingThink) is the RandomSound scheduler, owned by the RandomSound story; its
	// tail re-arms `+0x17c = curtime + 0.1f` (`0x1022b41d`), which is the one line of it kept here so the
	// cadence a record observes is retail's. The port's scheduler runs in the manager's frame update.
	virtual void ThinkAt(double Now) override
	{
		if (ThinkFn == 0)
		{
			return;
		}
		NextThink = static_cast<float>(Now) + SchemeThinkCadence;
	}

	// `CSoundScheme::InputFadeIn` (`0x1022b480`): `inputdata.fieldType == 1` (FIELD_FLOAT) ->
	// `FUN_1022b590(this, flVal)`, else `FUN_1022b590(this, 0.0)`. `CBaseEntity::AcceptInput`
	// (`0x100abc90`) has already converted a string or integer parameter to the input's declared
	// FIELD_FLOAT, so the 0.0 arm is a parameterless wire; the variant's `ToFloat` is that conversion
	// (Int, Float and String answer their value, Void answers 0).
	void InputFadeIn(const FElysiumVariant& Param)  { FadeIn(Param.ToFloat()); }
	// `CSoundScheme::InputFadeOut` (`0x1022b4d0`): the same read, into `FUN_1022b520`.
	void InputFadeOut(const FElysiumVariant& Param) { FadeOut(Param.ToFloat()); }

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Scheme file"), SchemeFile.IsEmpty() ? TEXT("(none)") : SchemeFile);
		Out.Emplace(TEXT("Start enabled"), bStartEnabled ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Playing (+0x455)"), bPlaying ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("m_pfnThink"), FString::Printf(TEXT("0x%08x"), ThinkFn));
		if (const IElysiumAudio* Audio = World ? World->Audio() : nullptr)
		{
			Out.Emplace(TEXT("Manager's last switch"), Audio->ActiveSchemeRel() == SchemeFile ? TEXT("this scheme") : TEXT("another"));
		}
	}

private:
	// UPWARD HOOK L0 -> L3 (`FUN_1022b590` -> `FUN_10175360` `0x10175360`): the write of this scheme's
	// EHANDLE to `player+0x1e04`, the RoomDSP fallback's input (`audio_pipeline.md` § 4). Not listed in
	// `docs/specs/layers/hooks.tsv` (`decisions.md` D6, `L0-audio`): ported through the player's named
	// setter pending the owner's classification. `UTIL_GetLocalPlayer` is the world's single player;
	// with none (a map built without one, or an Activate pass before the player exists -- UNRECOVERED
	// whether retail's pass has the player yet) the arm is skipped, as retail's null test skips it.
	void HookL3PlayerSetSoundScheme()
	{
		FElysiumPlayer* LocalPlayer = World ? World->FindPlayer() : nullptr;
		if (LocalPlayer == nullptr)
		{
			return;
		}
		LocalPlayer->SetSoundScheme(this);
		World->EmitRetailSite(*this, TEXT("scheme_fadein"), TEXT("FUN_1022b590"), 0x1022b590u, TEXT("player_write"),
			FString::Printf(TEXT("fn=FUN_10175360 va=0x10175360 field=+0x1e04 handle=#%d"), Handle.Index));
	}
};

void ElysiumSoundSchemeDisable(FElysiumEntity& Scheme)
{
	// The manager's list only ever holds schemes (`vfunc113` is the sole writer), as retail's does.
	if (Scheme.Def != nullptr && Scheme.Def->Classname.Equals(TEXT("ambient_soundscheme"), ESearchCase::IgnoreCase))
	{
		static_cast<FElysiumAmbientSoundscheme&>(Scheme).Disable();
	}
}

static TUniquePtr<FElysiumEntity> MakeAmbientSoundscheme() { return MakeUnique<FElysiumAmbientSoundscheme>(); }

static FElysiumClassRegistrar GRegAmbientSoundscheme(
	TEXT("ambient_soundscheme"), ElysiumBaseClassName(), &MakeAmbientSoundscheme,
	[](FElysiumClassDesc& D)
	{
		// The class datamap rows (`walks/L0-r009.md` § Shared facts, CSoundScheme). A keyed row is
		// registered under its key; the unnamed playing flag under its offset, as `m_dpv[..]` is.
		ElysiumAddClassField<FElysiumAmbientSoundscheme>(D, TEXT("scheme_file"), &FElysiumAmbientSoundscheme::SchemeFile,
			EElysiumField::Key | EElysiumField::Save);   // +0x450 m_sSchemeFile
		ElysiumAddClassField<FElysiumAmbientSoundscheme>(D, TEXT("start_enabled"), &FElysiumAmbientSoundscheme::bStartEnabled,
			EElysiumField::Key | EElysiumField::Save);   // +0x454 m_bStartEnabled
		ElysiumAddClassField<FElysiumAmbientSoundscheme>(D, TEXT("+0x455"), &FElysiumAmbientSoundscheme::bPlaying,
			EElysiumField::Save);                        // +0x455 the playing flag (no retail name)
		ElysiumAddClassField<FElysiumAmbientSoundscheme>(D, TEXT("m_pfnThink"), &FElysiumAmbientSoundscheme::ThinkFn,
			EElysiumField::Save);                        // +0x118 (CBaseEntity; 0 or 0x10003a71 here)
		// The two inputs retail declares (`FadeIn` 205 uses, `FadeOut` 179). There is no `Disable`
		// handler: the one authored `ambient_soundscheme,Disable` wire is unhandled in retail
		// (`audio_pipeline.md` § 5, "Kill does not stop the stems").
		D.Input(TEXT("FadeIn"),  [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumAmbientSoundscheme&>(E).InputFadeIn(A.Param); });
		D.Input(TEXT("FadeOut"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumAmbientSoundscheme&>(E).InputFadeOut(A.Param); });
	});
