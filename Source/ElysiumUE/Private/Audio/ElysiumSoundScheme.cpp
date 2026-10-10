// SoundScheme runtime: the KeyValues scheme parser, the playback manager (ambient bed +
// music state machine + polar random-one-shot scheduler), and the `ambient_soundscheme` entity.
// Reference: `docs/vtmb/audio_pipeline.md` §5/§6 + the decompiled CSoundScheme parser (vampire.dll @0x1022a930,
// `$ELYSIUM_WORK_ROOT/research/ghidra/out/aud_scheme_parser.txt`) — the field set and every retail default below come from
// that decompile. The music-stem crossfade is documented (§6); the live state is cvar/Cog-driven
// (`elysium.MusicState`).

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
#include "ElysiumWorldServices.h"

#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumScheme, Log, All);

// Tunables (debug/calibration). The crossfade time and the random-sound cadence base moved to
// `UElysiumAudioSettings`; these two stay cvars — they are debug A/Bs with a live Cog
// surface (elysium.MusicState is the Cog Sound Schemes window's own echo of combat scoring), not
// orphan taste values.
static TAutoConsoleVariable<int32> CVarMusicState(
	TEXT("elysium.MusicState"), 0,
	TEXT("Force the SoundScheme music state: 0 = Explore (safe), 1 = Combat, 2 = Alert. The real "
	     "driver is combat scoring (P9); this is the debug echo the Cog Sound Schemes window sets."),
	ECVF_Cheat);

static TAutoConsoleVariable<int32> CVarSchemeRandom(
	TEXT("elysium.SchemeRandom"), 1,
	TEXT("Play a scheme's RandomSound polar one-shots (1, default) or suppress them (0)."),
	ECVF_Default);

namespace
{
	constexpr float SchemeInchToCm = 2.54f;

	// The Source KeyValues reader lives in ElysiumKeyValues.h (shared with the sign
	// definitions); pull its names into this file's anonymous namespace unchanged.
	using ElysiumKeyValues::FKvNode;

	// Parse a Music/Combat/Alert/Ambient block, applying the retail defaults. bDryDefault /
	// bNoPauseDefault differ per block (Ambient NoPause defaults 0; the music trio default 1).
	// `FUN_1022a930`'s per-block reads: `GetString("Filename", default)` (`0x10248cd0`), `GetFloat("Volume",
	// 50)` (`0x10248c60`, not in this run: `Flt` stands in), `GetInt("Dry", 1) != 0`, `GetInt("NoPause",
	// 1|0) != 0` (`0x10248bb0`).
	FElysiumSchemeSound ReadSound(FKvNode* Block, bool bDryDefault, bool bNoPauseDefault)
	{
		FElysiumSchemeSound S;
		if (!Block) { return S; }
		S.Filename = ElysiumKeyValues::GetString(*Block, TEXT("Filename"), FString()).Replace(TEXT("\\"), TEXT("/"));
		S.Volume = FMath::Clamp(Block->Flt(TEXT("Volume"), 50.f) / 100.f, 0.f, 1.f);
		S.bDry = ElysiumKeyValues::GetInt(*Block, TEXT("Dry"), bDryDefault ? 1 : 0) != 0;
		S.bNoPause = ElysiumKeyValues::GetInt(*Block, TEXT("NoPause"), bNoPauseDefault ? 1 : 0) != 0;
		S.bValid = true;
		return S;
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
	Out.Music   = ReadSound(ElysiumKeyValues::FindKey(*Root, TEXT("Music"), ElysiumKeyValues::EKvCreate::No),   /*Dry*/ true, /*NoPause*/ true);
	Out.Combat  = ReadSound(ElysiumKeyValues::FindKey(*Root, TEXT("Combat"), ElysiumKeyValues::EKvCreate::No),  /*Dry*/ true, /*NoPause*/ true);
	Out.Alert   = ReadSound(ElysiumKeyValues::FindKey(*Root, TEXT("Alert"), ElysiumKeyValues::EKvCreate::No),   /*Dry*/ true, /*NoPause*/ true);
	Out.Ambient = ReadSound(ElysiumKeyValues::FindKey(*Root, TEXT("Ambient"), ElysiumKeyValues::EKvCreate::No), /*Dry*/ true, /*NoPause*/ false);

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

void FElysiumSoundSchemeManager::FadeInScheme(UElysiumAudioSubsystem* Audio, const FString& SchemeRel,
	const FVector& Anchor, float FadeSeconds)
{
	if (Active.SchemeRel == SchemeRel && Active.IsSet())
	{
		Active.Anchor = Anchor;   // re-anchor but do not restart
		return;
	}

	const FElysiumSoundScheme* Scheme = LoadScheme(SchemeRel);

	// Fade the outgoing scheme's voices out (the subsystem reaps them when the fade completes).
	StopAll(Audio);

	Active = FActiveScheme();
	Active.SchemeRel = SchemeRel;
	Active.Anchor = Anchor;
	if (Scheme)
	{
		Active.Scheme = *Scheme;
	}

	StartActiveVoices(Audio, FadeSeconds);
	UE_LOG(LogElysiumScheme, Log, TEXT("scheme FadeIn '%s' (%s) fade %.1fs"),
		*SchemeRel, Scheme ? TEXT("parsed") : TEXT("MISSING assets"), FadeSeconds);
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

void FElysiumSoundSchemeManager::FadeOutScheme(UElysiumAudioSubsystem* Audio, const FString& SchemeRel, float FadeSeconds)
{
	if (Active.SchemeRel != SchemeRel)
	{
		return;   // not the active scheme — nothing to fade
	}
	// StopAll uses an immediate stop; honour the requested fade on the bed/stems specifically.
	if (Audio)
	{
		Audio->StopVoice(Active.Bed, FadeSeconds);
		Audio->StopVoice(Active.Explore, FadeSeconds);
		Audio->StopVoice(Active.Combat, FadeSeconds);
		Audio->StopVoice(Active.Alert, FadeSeconds);
		for (const FElysiumAudioVoiceHandle& H : Active.RandomVoices) { Audio->StopVoice(H, FadeSeconds); }
	}
	UE_LOG(LogElysiumScheme, Log, TEXT("scheme FadeOut '%s' fade %.1fs"), *SchemeRel, FadeSeconds);
	Active = FActiveScheme();
}

void FElysiumSoundSchemeManager::StartActiveVoices(UElysiumAudioSubsystem* Audio, float FadeSeconds)
{
	if (!Audio || !Active.IsSet())
	{
		return;
	}
	const FElysiumSoundScheme& S = Active.Scheme;

	// Ambient bed — a non-positional looping bed at its own volume.
	if (S.Ambient.IsSet())
	{
		FElysiumAudioRequest Request;
		Request.Source = FElysiumAudioSource::Path(S.Ambient.Filename);
		Request.Owner = { EElysiumAudioOwnerKind::GameplaySystem,
			FString::Printf(TEXT("scheme:%s"), *Active.SchemeRel), MapEpoch };
		Request.Category = EElysiumAudioCategory::Ambience;
		Request.Gain = S.Ambient.Volume;
		Request.bLooping = true;
		Request.Placement.bSpatialized = false;
		Request.FadeInSeconds = FadeSeconds;
		Active.Bed = Audio->Submit(Request);
	}

	// Music stems — all present stems start together (looping, 2D) so they stay phase-locked; only
	// the active state's stem is audible (ApplyMusicVolumes), the rest sit at 0 for an instant crossfade.
	auto StartStem = [&](const FElysiumSchemeSound& Stem) -> FElysiumAudioVoiceHandle
	{
		if (!Stem.IsSet()) { return FElysiumAudioVoiceHandle::Invalid(); }
		FElysiumAudioRequest Request;
		Request.Source = FElysiumAudioSource::Path(Stem.Filename);
		Request.Owner = { EElysiumAudioOwnerKind::GameplaySystem,
			FString::Printf(TEXT("scheme:%s"), *Active.SchemeRel), MapEpoch };
		Request.Category = EElysiumAudioCategory::Music;
		Request.Gain = 0.f;             // ApplyMusicVolumes sets the real level below
		Request.bLooping = true;
		Request.Placement.bSpatialized = false;
		Request.FadeInSeconds = FadeSeconds;
		Request.ConcurrencyKey = TEXT("scheme.music");
		return Audio->Submit(Request);
	};
	Active.Explore = StartStem(S.Music);
	Active.Combat  = StartStem(S.Combat);
	Active.Alert   = StartStem(S.Alert);
	ApplyMusicVolumes(Audio, /*crossfade*/ 0.f);

	// Seed the per-RandomSound scheduler with staggered first-play times.
	Active.RandomNextTime.SetNum(S.RandomSounds.Num());
	for (int32 i = 0; i < S.RandomSounds.Num(); ++i)
	{
		const float Mean = GetDefault<UElysiumAudioSettings>()->SchemeRandomBase * 10.f / FMath::Max(1, S.RandomSounds[i].Frequency);
		Active.RandomNextTime[i] = Elapsed + FMath::FRandRange(0.2f, 1.0f) * Mean;
	}
}

void FElysiumSoundSchemeManager::ApplyMusicVolumes(UElysiumAudioSubsystem* Audio, float CrossfadeSeconds)
{
	if (!Audio || !Active.IsSet())
	{
		return;
	}
	const FElysiumSoundScheme& S = Active.Scheme;
	const float Ex = (CurrentMusicState == EElysiumMusicState::Explore) ? S.Music.Volume : 0.f;
	const float Co = (CurrentMusicState == EElysiumMusicState::Combat)  ? S.Combat.Volume : 0.f;
	const float Al = (CurrentMusicState == EElysiumMusicState::Alert)   ? S.Alert.Volume : 0.f;
	Audio->SetVoiceVolume(Active.Explore, Ex, CrossfadeSeconds);
	Audio->SetVoiceVolume(Active.Combat, Co, CrossfadeSeconds);
	Audio->SetVoiceVolume(Active.Alert, Al, CrossfadeSeconds);
}

void FElysiumSoundSchemeManager::SetMusicState(UElysiumAudioSubsystem* Audio, EElysiumMusicState NewState, float CrossfadeSeconds)
{
	if (NewState == CurrentMusicState)
	{
		return;
	}
	CurrentMusicState = NewState;
	const float Fade = CrossfadeSeconds >= 0.f ? CrossfadeSeconds : GetDefault<UElysiumAudioSettings>()->MusicCrossfade;
	ApplyMusicVolumes(Audio, Fade);
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

void FElysiumSoundSchemeManager::Tick(UElysiumAudioSubsystem* Audio, const FVector& PlayerLoc, float DeltaSeconds)
{
	Elapsed += FMath::Max(0.f, DeltaSeconds);

	// Sample the debug music-state cvar (the Cog window / a console set drives it); apply on change.
	const int32 Cv = FMath::Clamp(CVarMusicState.GetValueOnGameThread(), 0, 2);
	if (Cv != (int32)CurrentMusicState)
	{
		SetMusicState(Audio, (EElysiumMusicState)Cv);
	}

	TickRandom(Audio, PlayerLoc);
}

void FElysiumSoundSchemeManager::StopAll(UElysiumAudioSubsystem* Audio)
{
	if (Audio && Active.IsSet())
	{
		const float Fade = GetDefault<UElysiumAudioSettings>()->MusicCrossfade;
		Audio->StopVoice(Active.Bed, Fade);
		Audio->StopVoice(Active.Explore, Fade);
		Audio->StopVoice(Active.Combat, Fade);
		Audio->StopVoice(Active.Alert, Fade);
		for (const FElysiumAudioVoiceHandle& H : Active.RandomVoices) { Audio->StopVoice(H, Fade); }
	}
	Active.Bed = Active.Explore = Active.Combat = Active.Alert = FElysiumAudioVoiceHandle::Invalid();
	Active.RandomVoices.Reset();
}

// ambient_soundscheme — the scheme anchor entity (FadeIn 205 / FadeOut 179; `docs/vtmb/entity_io.md`).


class FElysiumAmbientSoundscheme final : public FElysiumEntity
{
public:
	virtual void Spawn() override
	{
		SchemeRel = Def ? Def->Keys.FindRef(TEXT("scheme_file")).Replace(TEXT("\\"), TEXT("/")) : FString();
		const FString* Enabled = Def ? Def->Keys.Find(TEXT("start_enabled")) : nullptr;
		bStartEnabled = Enabled && FCString::Atoi(**Enabled) != 0;

		// A start_enabled scheme is active from map load — fade it in instantly. The manager is
		// constructed before the world's spawn pass, so it is reachable here.
		if (bStartEnabled && !SchemeRel.IsEmpty() && !IsInert())
		{
			if (IElysiumAudio* Audio = World ? World->Audio() : nullptr)
			{
				Audio->FadeInScheme(SchemeRel, Def->Origin, /*instant*/ 0.f);
			}
		}
	}

	void InputFadeIn(const FElysiumVariant& Param)
	{
		if (IElysiumAudio* Audio = World ? World->Audio() : nullptr)
		{
			Audio->FadeInScheme(SchemeRel, Def ? Def->Origin : FVector::ZeroVector, FadeTime(Param));
		}
	}
	void InputFadeOut(const FElysiumVariant& Param)
	{
		if (IElysiumAudio* Audio = World ? World->Audio() : nullptr)
		{
			Audio->FadeOutScheme(SchemeRel, FadeTime(Param));
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Scheme file"), SchemeRel.IsEmpty() ? TEXT("(none)") : SchemeRel);
		Out.Emplace(TEXT("Start enabled"), bStartEnabled ? TEXT("yes") : TEXT("no"));
		if (const IElysiumAudio* Audio = World ? World->Audio() : nullptr)
		{
			Out.Emplace(TEXT("Active"), Audio->ActiveSchemeRel() == SchemeRel ? TEXT("yes (this scheme)") : TEXT("no"));
		}
	}

private:
	// FadeIn/FadeOut carry the crossfade time; fall back to the music-crossfade cvar when 0/absent.
	static float FadeTime(const FElysiumVariant& Param)
	{
		const float P = Param.ToFloat();
		return P > 0.f ? P : GetDefault<UElysiumAudioSettings>()->MusicCrossfade;
	}

	FString SchemeRel;
	bool bStartEnabled = false;
};

static TUniquePtr<FElysiumEntity> MakeAmbientSoundscheme() { return MakeUnique<FElysiumAmbientSoundscheme>(); }

static FElysiumClassRegistrar GRegAmbientSoundscheme(
	TEXT("ambient_soundscheme"), ElysiumBaseClassName(), &MakeAmbientSoundscheme,
	[](FElysiumClassDesc& D)
	{
		D.Input(TEXT("FadeIn"),  [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumAmbientSoundscheme&>(E).InputFadeIn(A.Param); });
		D.Input(TEXT("FadeOut"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumAmbientSoundscheme&>(E).InputFadeOut(A.Param); });
		// `Disable` (1 use) is treated as FadeOut with the default crossfade.
		D.Input(TEXT("Disable"), [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumAmbientSoundscheme&>(E).InputFadeOut(FElysiumVariant()); });
	});
