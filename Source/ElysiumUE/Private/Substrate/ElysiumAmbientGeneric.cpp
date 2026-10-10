// ambient_generic: VtMB's point sound (66 on the tutorial, 1,631 across 95 maps;
// audio_pipeline.md §7). A plain-C++ FElysiumEntity leaf that plays a WAV/MP3 at its origin
// through the GI audio subsystem's voice pool, honouring the Source spawnflags (everywhere / start-
// silent / not-looped) and the I/O input surface the maps wire (PlaySound 903, StopSound 248,
// Volume 36, FadeIn/FadeOut 2 each — entity_io.md). Registration follows ElysiumStarterClasses.cpp.
//
// The initialization sequence is `CAmbientGeneric`'s own, ported arm for arm from
// `docs/specs/layers/L0-entity/walks/L0-r005.md`: the KeyValue override `vfunc110` `0x101ada80`
// (the `m_dpv` modulation keys), `Spawn` `0x101ac310` (sound level, think/use arming, the looping and
// sound-flag words, the empty-sound removal), its tail `Precache` `0x101ac930` (the engine precache,
// the dpv pass, `m_fActive`) and the dpv pass `FUN_101ad0f0` (volume, presets, the running and
// initial words). Field names are the class datamap's (`datamap_records-vampire.dll.json`,
// `CAmbientGeneric` at `0x105969a4`); `m_dpv` is one 100-byte SAVE block there, its interior words
// unnamed in retail, so they are registered as `m_dpv[<byte offset>]`.

#include "ElysiumAudioSubsystem.h"
#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumRetailSite.h"               // the `ambient_level` / `ambient_store` / `kv_key` / ... taps
#include "ElysiumSoundLevel.h"               // `0x101ac570`: radius -> m_iSoundLevel
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumClassFields.h"    // the class datamap rows
#include "Substrate/ElysiumGameSound.h"

DEFINE_LOG_CATEGORY_STATIC(LogElysiumAmbient, Log, All);

namespace
{
	// Source ambient_generic spawnflags (audio_pipeline.md §7 / stock Source SF layout).
	constexpr int32 SF_AMBIENT_EVERYWHERE  = 0x01;   // omni: non-spatialized, no distance falloff
	constexpr int32 SF_AMBIENT_START_SILENT = 0x10;  // do not play on spawn; wait for PlaySound
	constexpr int32 SF_AMBIENT_NOT_LOOPED  = 0x20;   // one-shot (else the sound loops)

	// `m_nSndFlags` `+0x4e0`, the bits `Spawn` `0x101ac310` ORs in (steps 12a-d of the walk).
	constexpr int32 AMBIENT_SND_FLAG_NO_SFX        = 0x800;    // `m_bNoSFX` +0x4dc
	constexpr int32 AMBIENT_SND_FLAG_NO_VOICE_DUCK = 0x1000;   // `m_bNoVoiceDuck` +0x4dd
	constexpr int32 AMBIENT_SND_FLAG_SKIP_COLLIDE  = 0x200;    // `m_bSkipCollide` +0x4de
	constexpr int32 AMBIENT_SND_FLAG_FORCE_LOOPING = 0x100;    // `m_bForceLooping` +0x4df

	// Source keyvalue `radius`/`pitch` are raw Source units; geometry (origins) is already cm (the
	// UE_ convention). Radius converts inches→cm; pitch is a ratio, unitless.
	constexpr float AmbientInchToCm = 2.54f;

	// `m_dpv` `+0x458`, 25 dwords. The retail block has no interior names (one SAVE row of 100
	// `char`); the roles below are the walk's dpv map (`L0-r005.md` § 0x101ad0f0), read from the
	// keys, the dpv pass, Activate `0x101ac9c0`, the think `0x101acb70` and the Use `0x101ad470`.
	// Index = byte offset / 4.
	namespace AmbientDpv
	{
		constexpr int32 Preset      = 0;    // +0x458 key `preset`
		constexpr int32 PitchRun    = 1;    // +0x45c key `pitch`: the pitch ceiling the think ramps to
		constexpr int32 PitchStart  = 2;    // +0x460 key `pitchstart`: the pitch floor
		constexpr int32 SpinUp      = 3;    // +0x464 running per-think pitch increment
		constexpr int32 SpinDown    = 4;    // +0x468 running per-think pitch decrement
		constexpr int32 VolRun      = 5;    // +0x46c volume ceiling 0..100 (health*10)
		constexpr int32 VolStart    = 6;    // +0x470 key `volstart`: the volume floor
		constexpr int32 FadeIn      = 7;    // +0x474 running per-think volume increment
		constexpr int32 FadeOut     = 8;    // +0x478 running per-think volume decrement
		constexpr int32 LfoType     = 9;    // +0x47c
		constexpr int32 LfoRate     = 10;   // +0x480 8.8 phase step
		constexpr int32 LfoModPitch = 11;   // +0x484
		constexpr int32 LfoModVol   = 12;   // +0x488
		constexpr int32 CSpinUp     = 13;   // +0x48c
		constexpr int32 CSpinCount  = 14;   // +0x490 cspinup step counter
		constexpr int32 Pitch       = 15;   // +0x494 current pitch (Activate emits it)
		constexpr int32 SpinUpSav   = 16;   // +0x498 spinup initial
		constexpr int32 SpinDownSav = 17;   // +0x49c spindown initial
		constexpr int32 PitchFrac   = 18;   // +0x4a0 pitch accumulator, 8.8
		constexpr int32 Vol         = 19;   // +0x4a4 current volume (Activate emits it * 0.01)
		constexpr int32 FadeInSav   = 20;   // +0x4a8 fadein initial
		constexpr int32 FadeOutSav  = 21;   // +0x4ac fadeout initial
		constexpr int32 VolFrac     = 22;   // +0x4b0 volume accumulator, 8.8
		constexpr int32 LfoFrac     = 23;   // +0x4b4 LFO phase
		constexpr int32 LfoMult     = 24;   // +0x4b8 LFO output
		constexpr int32 Words       = 25;
	}

	// The preset table `FUN_101ad0f0` copies from: rows 1..27 of 25 dwords at
	// `0x10595eb4 + p * 0x64` (row 1 at `0x10595f18`, row 27 at `0x10596940`; PE read, `L0-r005.md`
	// Q3). Words 14..24 are 0 in every row. Columns are the dpv words 0..13: id, pitch, pitchstart,
	// spinup, spindown, vol-ceiling (x10 by the pass), volstart (x10), fadein, fadeout, lfotype,
	// lforate, lfomodpitch, lfomodvol, cspinup. No shipped map authors a non-zero `preset`.
	constexpr int32 AmbientPresetRows = 27;
	constexpr int32 AmbientPresetColumns = 14;
	constexpr int32 AmbientPresetTable[AmbientPresetRows][AmbientPresetColumns] =
	{
		{  1, 255,  75, 95, 95, 10, 1, 50, 95, 0,   0,   0,   0, 0 },   // 0x10595f18
		{  2, 255,  85, 70, 88, 10, 1, 20, 88, 0,   0,   0,   0, 0 },   // 0x10595f7c
		{  3, 255, 100, 50, 75, 10, 1, 10, 75, 0,   0,   0,   0, 0 },   // 0x10595fe0
		{  4, 100, 100,  0,  0, 10, 1, 90, 90, 0,   0,   0,   0, 0 },   // 0x10596044
		{  5, 100, 100,  0,  0, 10, 1, 80, 80, 0,   0,   0,   0, 0 },   // 0x105960a8
		{  6, 100, 100,  0,  0, 10, 1, 50, 70, 0,   0,   0,   0, 0 },   // 0x1059610c
		{  7, 100, 100,  0,  0,  5, 1, 40, 50, 1,  50,   0,  10, 0 },   // 0x10596170
		{  8, 100, 100,  0,  0,  5, 1, 40, 50, 1, 150,   0,  10, 0 },   // 0x105961d4
		{  9, 100, 100,  0,  0,  5, 1, 40, 50, 1, 750,   0,  10, 0 },   // 0x10596238
		{ 10, 128, 100, 50, 75, 10, 1, 30, 40, 2,   8,  20,   0, 0 },   // 0x1059629c
		{ 11, 128, 100, 50, 75, 10, 1, 30, 40, 2,  25,  20,   0, 0 },   // 0x10596300
		{ 12, 128, 100, 50, 75, 10, 1, 30, 40, 2,  70,  20,   0, 0 },   // 0x10596364
		{ 13,  50,  50,  0,  0, 10, 1, 20, 50, 0,   0,   0,   0, 0 },   // 0x105963c8
		{ 14,  70,  70,  0,  0, 10, 1, 20, 50, 0,   0,   0,   0, 0 },   // 0x1059642c
		{ 15,  90,  90,  0,  0, 10, 1, 20, 50, 0,   0,   0,   0, 0 },   // 0x10596490
		{ 16, 120, 120,  0,  0, 10, 1, 20, 50, 0,   0,   0,   0, 0 },   // 0x105964f4
		{ 17, 180, 180,  0,  0, 10, 1, 20, 50, 0,   0,   0,   0, 0 },   // 0x10596558
		{ 18, 255, 255,  0,  0, 10, 1, 20, 50, 0,   0,   0,   0, 0 },   // 0x105965bc
		{ 19, 200,  75, 90, 90, 10, 1, 50, 90, 2, 100,  20,   0, 0 },   // 0x10596620
		{ 20, 255,  75, 97, 90, 10, 1, 50, 90, 1,  40,  50,   0, 0 },   // 0x10596684
		{ 21, 100, 100,  0,  0, 10, 1, 30, 50, 3,  15,  20,   0, 0 },   // 0x105966e8
		{ 22, 160, 160,  0,  0, 10, 1, 50, 50, 3, 500,  25,   0, 0 },   // 0x1059674c
		{ 23, 255,  75, 88,  0, 10, 1, 40,  0, 0,   0,   0,   0, 5 },   // 0x105967b0
		{ 24, 200,  20, 95, 70, 10, 1, 70, 70, 3,  20,  50,   0, 0 },   // 0x10596814
		{ 25, 180, 100, 50, 60, 10, 1, 40, 60, 2,  90, 100, 100, 0 },   // 0x10596878
		{ 26,  60,  60,  0,  0, 10, 1, 40, 70, 3,  80,  20,  50, 0 },   // 0x105968dc
		{ 27, 128,  90, 10, 10, 10, 1, 20, 40, 1,   5,  10,  20, 0 },   // 0x10596940
	};

	// `(0x65 - v) * 0x40`: the spin/fade rate transform `vfunc110` (arms 4, 5) and the preset arm
	// of `FUN_101ad0f0` (B1, B2, B5, B6) apply to a positive word.
	int32 AmbientSpinRate(int32 V) { return (101 - V) * 64; }

	// `__ftol` `0x10431320` of an x87 value: `FSTCW; OR AH,0xc` (truncate toward zero), `FISTP qword`,
	// EAX = the low dword. A value outside int64 (`+Inf`, NaN) stores the integer indefinite
	// `0x8000000000000000`, whose low dword is 0.
	int32 AmbientFtolLowDword(double V)
	{
		if (!FMath::IsFinite(V) || FMath::Abs(V) >= 9223372036854775808.0)
		{
			return 0;
		}
		return static_cast<int32>(static_cast<uint32>(static_cast<int64>(V) & 0xffffffff));
	}

	// `fadein` / `fadeout` (`vfunc110` arms 7, 8; `0x101adcbf..0x101adcd8`): `_atof(value)`, then
	// `FDIVR double [0x10457188]` (100.0 / x), `FMUL double [0x10449198]` (* 0.2) on the FPU stack,
	// `__ftol`, `SHL EAX,8`. `fadein 10` -> 512, `0.75` -> 6656, `30` -> 0, `0` -> +Inf -> 0 (the
	// integer indefinite's low dword; 635 shipped ambients author `fadein 0`). The two doubles are
	// the stored constants, so an exact quotient (x = 5: 20 * 0.2000000000000000111) lands just
	// above its integer and truncates to it, as retail's does.
	int32 AmbientFadeRate(const FString& Value)
	{
		const double X = FCString::Atod(*Value);
		const double Twenty = 100.0 / X * 0.2;
		return AmbientFtolLowDword(Twenty) * 256;   // `SHL EAX,8` on the (possibly negative) dword
	}
}

// --- ambient_generic ---

class FElysiumAmbientGeneric final : public FElysiumEntity
{
public:
	// --- Inputs (registered below; reach here through the class-chain thunk) ---------------
	// The PlaySound / StopSound inputs reach retail's toggle Use `FUN_101ad470` (a later slice: it
	// re-runs the dpv pass and lets the think `0x101acb70` ramp the volume). Until then they start
	// and stop the voice with the same retail-derived ramp Activate uses (see `InterimRampSeconds`).
	void InputPlaySound()
	{
		bDesiredPlaying = true;
		Volume = static_cast<float>(Dpv[AmbientDpv::VolRun]) * 0.01f;
		Pitch = static_cast<float>(Dpv[AmbientDpv::Pitch]) / 100.f;
		StartVoice(InterimRampSeconds(Dpv[AmbientDpv::VolStart], Dpv[AmbientDpv::VolRun], Dpv[AmbientDpv::FadeInSav]));
	}
	void InputStopSound()
	{
		bDesiredPlaying = false;
		StopActiveVoice(InterimRampSeconds(Dpv[AmbientDpv::VolRun], Dpv[AmbientDpv::VolStart], Dpv[AmbientDpv::FadeOutSav]));
	}
	void InputToggleSound()
	{
		if (bDesiredPlaying) { InputStopSound(); } else { InputPlaySound(); }
	}
	// VtMB `Volume` input param is 0–10 (Source convention); scale to the 0..1 linear multiplier.
	// (`InputVolume 0x101ac7d0` is not this slice's: it keeps the port's prior behaviour.)
	void InputVolume(const FElysiumVariant& Param)
	{
		Volume = FMath::Clamp(Param.ToFloat() / 10.f, 0.f, 1.f);
		if (IElysiumAudio* Audio = World ? World->Audio() : nullptr)
		{
			Audio->SetVoiceVolume(VoiceHandle, Volume);   // no-op if no voice is running
		}
	}
	void InputFadeIn(const FElysiumVariant& Param)  { bDesiredPlaying = true;  StartVoice(FMath::Max(Param.ToFloat(), 0.f)); }
	void InputFadeOut(const FElysiumVariant& Param) { bDesiredPlaying = false; StopActiveVoice(FMath::Max(Param.ToFloat(), 0.f)); }

	// `CAmbientGeneric::vfunc110` `0x101ada80`, the KeyValue override (slot 110). Retail runs it per
	// key from `CBaseEntity::ParseMapData` `0x1009e280` in the lump's key order, before Spawn; the
	// port's keyvalue pass is `Construct`, which applies the datamap rows (`vfunc110`'s arm 14, the
	// delegate `CBaseEntity::KeyValue` `0x1009e430`) and has no per-class override seam, so the
	// thirteen dpv arms run here at Spawn's entry over the same keys in the same order. Nothing reads
	// a dpv word between the two points, so the sequence is retail's. Each key is matched whole with
	// `__strcmpi`, so the arms are mutually exclusive; every handled key returns 1 (`MOV AL,1`).
	bool AmbientKeyValue(const FString& Key, const FString& Value)
	{
		auto Store = [&](int32 Index, int32 V, const TCHAR* KeyName, int32 Mirror = INDEX_NONE)
		{
			Dpv[Index] = V;
			if (Mirror != INDEX_NONE)
			{
				Dpv[Mirror] = V;   // the mirror is written even when V is 0
			}
			if (World != nullptr)
			{
				World->EmitRetailSite(*this, TEXT("kv_key"), TEXT("CAmbientGeneric::vfunc110"), 0x101ada80u, TEXT("write"),
					Mirror == INDEX_NONE ? FString::Printf(TEXT("key=%s value=%d"), KeyName, V)
						: FString::Printf(TEXT("key=%s value=%d mirror=%d"), KeyName, V, V));
			}
			return true;
		};
		// `_atoi` `0x10431447` for every arm but 7 and 8; the clamps are signed compares.
		const int32 I = FCString::Atoi(*Value);
		if (Key.Equals(TEXT("preset"), ESearchCase::IgnoreCase))        // arm 1: no clamp
		{
			return Store(AmbientDpv::Preset, I, TEXT("preset"));
		}
		if (Key.Equals(TEXT("pitch"), ESearchCase::IgnoreCase))         // arm 2: [0, 0xff]
		{
			return Store(AmbientDpv::PitchRun, FMath::Clamp(I, 0, 0xff), TEXT("pitch"));
		}
		if (Key.Equals(TEXT("pitchstart"), ESearchCase::IgnoreCase))    // arm 3: [0, 0xff]
		{
			return Store(AmbientDpv::PitchStart, FMath::Clamp(I, 0, 0xff), TEXT("pitchstart"));
		}
		if (Key.Equals(TEXT("spinup"), ESearchCase::IgnoreCase))        // arm 4: [0, 0x64], >0 -> (101-v)*64, mirror +0x498
		{
			const int32 V = FMath::Clamp(I, 0, 100);
			return Store(AmbientDpv::SpinUp, V > 0 ? AmbientSpinRate(V) : V, TEXT("spinup"), AmbientDpv::SpinUpSav);
		}
		if (Key.Equals(TEXT("spindown"), ESearchCase::IgnoreCase))      // arm 5: as spinup, mirror +0x49c
		{
			const int32 V = FMath::Clamp(I, 0, 100);
			return Store(AmbientDpv::SpinDown, V > 0 ? AmbientSpinRate(V) : V, TEXT("spindown"), AmbientDpv::SpinDownSav);
		}
		if (Key.Equals(TEXT("volstart"), ESearchCase::IgnoreCase))      // arm 6: [0, 0xa], then *10
		{
			return Store(AmbientDpv::VolStart, FMath::Clamp(I, 0, 10) * 10, TEXT("volstart"));
		}
		if (Key.Equals(TEXT("fadein"), ESearchCase::IgnoreCase))        // arm 7: `_atof`, 20/x, ftol, << 8, mirror +0x4a8
		{
			return Store(AmbientDpv::FadeIn, AmbientFadeRate(Value), TEXT("fadein"), AmbientDpv::FadeInSav);
		}
		if (Key.Equals(TEXT("fadeout"), ESearchCase::IgnoreCase))       // arm 8: as fadein, mirror +0x4ac
		{
			return Store(AmbientDpv::FadeOut, AmbientFadeRate(Value), TEXT("fadeout"), AmbientDpv::FadeOutSav);
		}
		if (Key.Equals(TEXT("lfotype"), ESearchCase::IgnoreCase))       // arm 9: raw, no lower clamp; > 4 -> 2
		{
			return Store(AmbientDpv::LfoType, I > 4 ? 2 : I, TEXT("lfotype"));
		}
		if (Key.Equals(TEXT("lforate"), ESearchCase::IgnoreCase))       // arm 10: [0, 0x3e8], then << 8
		{
			return Store(AmbientDpv::LfoRate, FMath::Clamp(I, 0, 1000) * 256, TEXT("lforate"));
		}
		if (Key.Equals(TEXT("lfomodpitch"), ESearchCase::IgnoreCase))   // arm 11: [0, 0x64]
		{
			return Store(AmbientDpv::LfoModPitch, FMath::Clamp(I, 0, 100), TEXT("lfomodpitch"));
		}
		if (Key.Equals(TEXT("lfomodvol"), ESearchCase::IgnoreCase))     // arm 12: [0, 0x64]
		{
			return Store(AmbientDpv::LfoModVol, FMath::Clamp(I, 0, 100), TEXT("lfomodvol"));
		}
		if (Key.Equals(TEXT("cspinup"), ESearchCase::IgnoreCase))       // arm 13: [0, 0x64]
		{
			return Store(AmbientDpv::CSpinUp, FMath::Clamp(I, 0, 100), TEXT("cspinup"));
		}
		// Arm 14: `thunk 0x1000fc86` -> `CBaseEntity::KeyValue` `0x1009e430`, the datamap chain
		// through which `message`, `radius`, `health`, `spawnflags`, `SourceEntityName`,
		// `sound_event*` and `flag_*` reach their rows -- the port's `Construct` has already
		// applied those.
		return false;
	}

	virtual void Spawn() override
	{
		// The KeyValue pass (see `AmbientKeyValue`), in the def's key order.
		if (Def != nullptr)
		{
			for (const TPair<FString, FString>& KV : Def->Keys)
			{
				AmbientKeyValue(KV.Key, KV.Value);
			}
		}

		// `CAmbientGeneric::Spawn` `0x101ac310`, steps in retail order.
		// 1-2. `0x101ac2f0` (`m_spawnflags & 1`) and `FUN_101ac570(m_radius, flag)` at `0x101ac321`;
		//      the result is stored in `m_iSoundLevel` (`+0x454`) at `0x101ac326`. Written before the
		//      empty-sound test, so the removed ambient carries it too.
		FElysiumEntityRetailSites Sites(World, *this);
		SoundLevel = ElysiumSoundLevel::FromAmbientRadius(Radius,
			(SpawnFlags & SF_AMBIENT_EVERYWHERE) != 0 ? ElysiumSoundLevel::EPlacement::Everywhere
				: ElysiumSoundLevel::EPlacement::Positional,
			&Sites);
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("ambient_store"), TEXT("CAmbientGeneric::Spawn"), 0x101ac326u,
				TEXT("write"), FString::Printf(TEXT("field=m_iSoundLevel value=%d"), SoundLevel));
		}

		// 3. `m_iszSound` (`+0x4c0`): a null pointer or `strlen < 1` (`CMP ECX,1; JC`) takes the empty
		//    path (step 9). A one-character name takes the main path.
		if (SoundName.IsEmpty())
		{
			// 9. `0x101ac4a0`: `GetAbsOrigin` (slot 217) three times, `Warning("EMPTY AMBIENT AT: %f, %f,
			//    %f\n", ...)` (format `0x10597804`), then `FUN_101cd940(this)` = `UTIL_Remove`, which sets
			//    the kill-me bit, runs `UpdateOnRemove`, clears the name and queues the deletion;
			//    `DispatchSpawn` `0x101d1280` then answers -1. Nothing else of Spawn runs.
			if (World != nullptr)
			{
				World->EmitRetailSite(*this, TEXT("spawn_empty"), TEXT("CAmbientGeneric::Spawn"), 0x101ac4a0u, TEXT("branch"),
					FString::Printf(TEXT("origin=%g,%g,%g level=%d"), Origin.X, Origin.Y, Origin.Z, SoundLevel));
			}
			UE_LOG(LogElysiumAmbient, Warning, TEXT("EMPTY AMBIENT AT: %f, %f, %f"), Origin.X, Origin.Y, Origin.Z);
			Kill();
			return;
		}

		// 4. `SetSolid(0)`: `FUN_100dc480(&m_Collision, 0)` does nothing when the solid word is already
		//    0, which it is on a freshly built ambient (the constructor `FUN_101ac230` never sets it).
		// 5. `SetMoveType(0, 0)` (slot 93, `0x100aad70`): writes `m_MoveType`/`m_MoveCollide` only when
		//    `m_MoveType != 0`; a fresh ambient's is 0. A bodiless point entity here has neither word
		//    to write, so both steps are the retail no-op.
		// 6. `ThinkSet(this, 0x1000969c -> FUN_101acb70, 0.0, NULL)`: writes `m_pfnThink` (`+0x118`)
		//    only. The ambient think body is a later slice's (`L0-r005.md` Q4); the port's think
		//    identity stays the class's.
		// 7. `m_flNextThink` (`+0x17c`) <- 0: no think is scheduled by Spawn. Activate (`vfunc113`
		//    `0x101ac9c0`) is what arms it, and only for an active ambient (Q1).
		NextThink = ELYSIUM_NEVER_THINK;
		// 8. `m_pfnUse` (`+0x1f0`) <- `0x100131e2` (`FUN_101ad470`, the toggle Use; a later slice).
		// 10. `m_fActive` (`+0x4bc`) <- 0.
		bActive = false;
		// 11. `m_fLooping` (`+0x4bd`) <- 0 iff `m_bForceLooping == 0` and `m_spawnflags & 0x20`, else 1.
		bLooping = !(!bForceLooping && (SpawnFlags & SF_AMBIENT_NOT_LOOPED) != 0);
		// 12. `m_nSndFlags` (`+0x4e0`, constructor-zeroed): four read-modify-writes, in this order.
		if (bNoSFX)        { SndFlags |= AMBIENT_SND_FLAG_NO_SFX; }
		if (bNoVoiceDuck)  { SndFlags |= AMBIENT_SND_FLAG_NO_VOICE_DUCK; }
		if (bSkipCollide)  { SndFlags |= AMBIENT_SND_FLAG_SKIP_COLLIDE; }
		if (bForceLooping) { SndFlags |= AMBIENT_SND_FLAG_FORCE_LOOPING; }
		// 13. The source handle (`+0x4c8`, retail name unrecovered; SDK `m_hSoundSource`) <- -1.
		SoundSource = FElysiumEntityHandle::Invalid();
		if (World != nullptr)
		{
			// 14. `JMP [slot 104]` at `0x101ac49a`: the words Spawn leaves behind as it tail-calls Precache.
			World->EmitRetailSite(*this, TEXT("spawn_arm"), TEXT("CAmbientGeneric::Spawn"), 0x101ac49au, TEXT("branch"),
				FString::Printf(TEXT("m_flNextThink=0 m_pfnThink=0x1000969c m_pfnUse=0x100131e2 m_fActive=%d m_fLooping=%d m_nSndFlags=0x%x m_hSoundSource=0x%08x"),
					bActive ? 1 : 0, bLooping ? 1 : 0, SndFlags, 0xffffffffu));
		}
		Precache();
	}

	// `CAmbientGeneric::Precache` `0x101ac930` (slot 104), Spawn's tail.
	void Precache()
	{
		// `m_iszSound != 0 && strlen > 1 (CMP ECX,1; JBE skips) && name[0] != '!'` ->
		// `IEngineSoundServer003` slot 0, `CEngineSoundServer::PrecacheSound(name, 0)` (engine.dll
		// `0x200018e0`). The port's engine is the audio subsystem's prefetch.
		const bool bPrecache = SoundName.Len() > 1 && SoundName[0] != TEXT('!');
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("precache_arm"), TEXT("CAmbientGeneric::Precache"), 0x101ac930u, TEXT("branch"),
				FString::Printf(TEXT("precache=%d name=%s"), bPrecache ? 1 : 0, *SoundName));
		}
		if (bPrecache)
		{
			if (IElysiumAudio* Audio = World ? World->Audio() : nullptr)
			{
				Audio->Prefetch(FElysiumAudioSource::Path(SoundPath()));
			}
		}
		// `thunk 0x1000da80` -> `FUN_101ad0f0(this)`.
		InitModulationParms();
		// `(m_spawnflags & 0x10) == 0 && m_fLooping` -> `m_fActive` <- 1. `m_flNextThink` untouched.
		if ((SpawnFlags & SF_AMBIENT_START_SILENT) == 0 && bLooping)
		{
			bActive = true;
		}
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("precache_arm"), TEXT("CAmbientGeneric::Precache"), 0x101ac930u, TEXT("write"),
				FString::Printf(TEXT("field=m_fActive value=%d"), bActive ? 1 : 0));
		}
	}

	// `FUN_101ad0f0` `0x101ad0f0` (590 B, `__fastcall this`, no callees): the dpv pass, run by Precache
	// and re-run by the toggle Use `FUN_101ad470` at `0x101ad70a`. Steps A, B, C in retail order.
	void InitModulationParms()
	{
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("dpv_pass"), TEXT("FUN_101ad0f0"), 0x101ad0f0u, TEXT("entry"),
				FString::Printf(TEXT("m_iHealth=%d preset=%d"), Health, Dpv[AmbientDpv::Preset]));
		}
		// A. `+0x46c` <- `m_iHealth` (`+0x210`) * 10, stored first, then > 100 -> 100, then < 0 -> 0.
		Dpv[AmbientDpv::VolRun] = Health * 10;
		if (Dpv[AmbientDpv::VolRun] > 100) { Dpv[AmbientDpv::VolRun] = 100; }
		if (Dpv[AmbientDpv::VolRun] < 0)   { Dpv[AmbientDpv::VolRun] = 0; }

		// B. Preset: `+0x458 != 0 && +0x458 <= 0x1b` (signed `JZ`, `CMP 0x1b; JG`): `MOVSD.REP` x25 from
		//    `0x10595eb4 + p * 0x64` over `+0x458..+0x4bb`, overwriting the id, A's volume and every
		//    keyed word. A negative preset also enters B and reads below the table (row -1 is a
		//    pointer array; UNRECOVERED, authored nowhere): the port leaves the words as they are for
		//    p < 0 and states it.
		const int32 Preset = Dpv[AmbientDpv::Preset];
		if (Preset != 0 && Preset <= 27)
		{
			if (Preset > 0)
			{
				for (int32 I = 0; I < AmbientDpv::Words; ++I)
				{
					Dpv[I] = I < AmbientPresetColumns ? AmbientPresetTable[Preset - 1][I] : 0;
				}
			}
			else
			{
				UE_LOG(LogElysiumAmbient, Warning, TEXT("%s: preset %d reads below the retail table (unrecovered); words left as keyed"),
					*DebugString(), Preset);
			}
			// B1-B2. spindown, then spinup: > 0 -> (101 - v) * 64.
			if (Dpv[AmbientDpv::SpinDown] > 0) { Dpv[AmbientDpv::SpinDown] = AmbientSpinRate(Dpv[AmbientDpv::SpinDown]); }
			if (Dpv[AmbientDpv::SpinUp] > 0)   { Dpv[AmbientDpv::SpinUp] = AmbientSpinRate(Dpv[AmbientDpv::SpinUp]); }
			// B3-B4. volstart, then the row's volume ceiling (not A's): * 10.
			Dpv[AmbientDpv::VolStart] *= 10;
			Dpv[AmbientDpv::VolRun] *= 10;
			// B5-B6. fadein, then fadeout: > 0 -> (101 - v) * 64.
			if (Dpv[AmbientDpv::FadeIn] > 0)  { Dpv[AmbientDpv::FadeIn] = AmbientSpinRate(Dpv[AmbientDpv::FadeIn]); }
			if (Dpv[AmbientDpv::FadeOut] > 0) { Dpv[AmbientDpv::FadeOut] = AmbientSpinRate(Dpv[AmbientDpv::FadeOut]); }
			// B7. lforate << 8.
			Dpv[AmbientDpv::LfoRate] *= 256;
			// B8. The initial mirrors, from the transformed words.
			Dpv[AmbientDpv::FadeInSav] = Dpv[AmbientDpv::FadeIn];
			Dpv[AmbientDpv::FadeOutSav] = Dpv[AmbientDpv::FadeOut];
			Dpv[AmbientDpv::SpinUpSav] = Dpv[AmbientDpv::SpinUp];
			Dpv[AmbientDpv::SpinDownSav] = Dpv[AmbientDpv::SpinDown];
		}

		// C. The running state, every path.
		// C1. fadeout running <- 0; fadein running <- fadein initial.
		Dpv[AmbientDpv::FadeOut] = 0;
		Dpv[AmbientDpv::FadeIn] = Dpv[AmbientDpv::FadeInSav];
		// C2. current volume <- (fadein initial == 0) ? ceiling : floor.
		Dpv[AmbientDpv::Vol] = Dpv[AmbientDpv::FadeInSav] == 0 ? Dpv[AmbientDpv::VolRun] : Dpv[AmbientDpv::VolStart];
		// C3. spindown running <- 0; spinup running <- spinup initial.
		const int32 SpinUpSav = Dpv[AmbientDpv::SpinUpSav];
		Dpv[AmbientDpv::SpinDown] = 0;
		Dpv[AmbientDpv::SpinUp] = SpinUpSav;
		// C4. current pitch <- (spinup initial == 0) ? pitch ceiling : pitch floor; 0 -> 100.
		Dpv[AmbientDpv::Pitch] = SpinUpSav == 0 ? Dpv[AmbientDpv::PitchRun] : Dpv[AmbientDpv::PitchStart];
		if (Dpv[AmbientDpv::Pitch] == 0) { Dpv[AmbientDpv::Pitch] = 100; }
		// C5-C7. The 8.8 accumulators and the LFO phase.
		Dpv[AmbientDpv::VolFrac] = Dpv[AmbientDpv::Vol] * 256;
		Dpv[AmbientDpv::LfoFrac] = 0;
		Dpv[AmbientDpv::PitchFrac] = Dpv[AmbientDpv::Pitch] * 256;
		// C8. lforate <- |lforate| (`CDQ; XOR; SUB`).
		Dpv[AmbientDpv::LfoRate] = FMath::Abs(Dpv[AmbientDpv::LfoRate]);
		// C9. cspinup step counter <- 1.
		Dpv[AmbientDpv::CSpinCount] = 1;
		// C10. cspinup != 0: pitch ceiling <- floor + (255 - floor) / cspinup (signed `IDIV`), > 255 -> 255.
		//      C4 already read the old ceiling, so this call's current pitch is unaffected.
		if (Dpv[AmbientDpv::CSpinUp] != 0)
		{
			Dpv[AmbientDpv::PitchRun] = Dpv[AmbientDpv::PitchStart] + (255 - Dpv[AmbientDpv::PitchStart]) / Dpv[AmbientDpv::CSpinUp];
			if (Dpv[AmbientDpv::PitchRun] > 255) { Dpv[AmbientDpv::PitchRun] = 255; }
		}
		// C11. (spinup initial != 0 || spindown initial != 0 || (lfotype != 0 && lfomodpitch != 0))
		//      && current pitch == 100 -> 101: the engine treats pitch 100 as "no shift", so a
		//      modulated ambient is nudged off it.
		if ((SpinUpSav != 0 || Dpv[AmbientDpv::SpinDownSav] != 0 || (Dpv[AmbientDpv::LfoType] != 0 && Dpv[AmbientDpv::LfoModPitch] != 0))
			&& Dpv[AmbientDpv::Pitch] == 100)
		{
			Dpv[AmbientDpv::Pitch] = 101;
		}
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("dpv_pass"), TEXT("FUN_101ad0f0"), 0x101ad0f0u, TEXT("return"),
				FString::Printf(TEXT("volrun=%d vol=%d pitchrun=%d pitch=%d spinup=%d fadein=%d pitchfrac=%d volfrac=%d cspincount=%d"),
					Dpv[AmbientDpv::VolRun], Dpv[AmbientDpv::Vol], Dpv[AmbientDpv::PitchRun], Dpv[AmbientDpv::Pitch], Dpv[AmbientDpv::SpinUp],
					Dpv[AmbientDpv::FadeIn], Dpv[AmbientDpv::PitchFrac], Dpv[AmbientDpv::VolFrac], Dpv[AmbientDpv::CSpinCount]));
		}
	}

	// `CAmbientGeneric::vfunc113` `0x101ac9c0`, the `CBaseEntity::Activate` override, run once by the
	// level's Activate pass (`CServerGameDLL::vfunc4 0x1011aaf0`; `DispatchSpawn` does not call it).
	// Ported here only as far as this slice's Spawn needs it to be observable: `m_fActive` gates the
	// emission (volume `+0x4a4 * 0.01`, pitch `+0x494`, `m_iSoundLevel`). The source-handle resolution
	// (`+0x4c8` from `m_sSourceEntName`), the emit flags `0x101ac670(this, 8)` and the think arming
	// (`m_flNextThink = curtime + 0.1`, the ramp think `FUN_101acb70`) are `L0.audio.ambient-source-
	// resume`'s. Until that think lands, a non-zero `fadein` would leave the voice at the floor
	// (C2) forever; the named modernization below hands Unreal the ramp retail's think would have run.
	virtual void Activate() override
	{
		bDesiredPlaying = bActive;
		if (!bActive || IsInert())
		{
			return;
		}
		Volume = static_cast<float>(Dpv[AmbientDpv::Vol]) * 0.01f;        // `(float)+0x4a4 * 0.01` (`0x1044e658`)
		Pitch = static_cast<float>(Dpv[AmbientDpv::Pitch]) / 100.f;        // `+0x494`, 100 = unshifted
		const float FadeSeconds = InterimRampSeconds(Dpv[AmbientDpv::Vol], Dpv[AmbientDpv::VolRun], Dpv[AmbientDpv::FadeIn]);
		if (FadeSeconds > 0.f)
		{
			Volume = static_cast<float>(Dpv[AmbientDpv::VolRun]) * 0.01f;   // the ramp's end, see InterimRampSeconds
		}
		StartVoice(FadeSeconds);
	}

	// MODERNIZATION (interim, named): the think `0x101acb70` adds `+0x474` (fade-in) to, or subtracts
	// `+0x478` (fade-out) from, the 8.8 volume accumulator `+0x4b0` every 0.2 s (`0x10449198`) until
	// the volume reaches the ceiling `+0x46c` or the floor `+0x470`. The endpoints and the duration
	// below are retail's (`fadein 10` -> 512 per think -> 2 volume points per 0.2 s: a health-4 bed
	// reaches 40 in 4 s); the per-think staircase is rendered as Unreal's continuous fade until the
	// think slice owns it. A zero rate is no ramp.
	static float InterimRampSeconds(int32 From, int32 To, int32 Rate)
	{
		if (Rate <= 0 || From == To)
		{
			return 0.f;
		}
		const float StepsPerSecond = 5.f;   // the think's 0.2 s cadence
		return static_cast<float>(FMath::Abs(To - From)) * 256.f / static_cast<float>(Rate) / StepsPerSecond;
	}

	// Mirror dormancy onto the voice: a hidden/killed sound goes silent; on un-hide it resumes if it
	// was meant to be playing. Base first (gates the body — though point sounds have none).
	virtual void OnDormancyChanged() override
	{
		FElysiumEntity::OnDormancyChanged();
		if (IsInert())
		{
			StopActiveVoice(0.f);
		}
		else if (bDesiredPlaying)
		{
			StartVoice(0.f);
		}
	}

	virtual void GetDebugState(TArray<TPair<FString, FString>>& Out) const override
	{
		Out.Emplace(TEXT("Sound"), SoundName.IsEmpty() ? TEXT("(none)") : SoundName);
		Out.Emplace(TEXT("Playing"), IsPlaying() ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Active"), bActive ? TEXT("yes") : TEXT("no"));
		Out.Emplace(TEXT("Volume"), FString::Printf(TEXT("%d/100 (health %d, ceiling %d, floor %d)"),
			Dpv[AmbientDpv::Vol], Health, Dpv[AmbientDpv::VolRun], Dpv[AmbientDpv::VolStart]));
		Out.Emplace(TEXT("Pitch"), FString::Printf(TEXT("%d (ceiling %d, floor %d)"),
			Dpv[AmbientDpv::Pitch], Dpv[AmbientDpv::PitchRun], Dpv[AmbientDpv::PitchStart]));
		Out.Emplace(TEXT("Placement"), (SpawnFlags & SF_AMBIENT_EVERYWHERE) != 0 ? TEXT("everywhere (2D)")
			: FString::Printf(TEXT("3D · radius %.0f · level %d%s"), Radius, SoundLevel,
				SourceEntityName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" · parent '%s'"), *SourceEntityName)));
		Out.Emplace(TEXT("Looping"), bLooping ? TEXT("yes") : TEXT("no (one-shot)"));
		Out.Emplace(TEXT("Fade (8.8/think)"), FString::Printf(TEXT("in %d · out %d"), Dpv[AmbientDpv::FadeInSav], Dpv[AmbientDpv::FadeOutSav]));
		Out.Emplace(TEXT("Snd flags"), FString::Printf(TEXT("0x%x"), SndFlags));
	}

private:
	bool IsPlaying() const
	{
		const IElysiumAudio* Audio = World ? World->Audio() : nullptr;
		return Audio && Audio->IsVoicePlaying(VoiceHandle);
	}

	// The relative sound path the audio subsystem resolves (`m_iszSound` with the map's backslashes).
	FString SoundPath() const { return SoundName.Replace(TEXT("\\"), TEXT("/")); }

	// (Re)start the voice — stop any prior one first so a repeated PlaySound restarts cleanly.
	void StartVoice(float FadeInSeconds)
	{
		// Native calls InsertSound immediately after its EmitSound attempt. The substrate's audio
		// backend may be absent in a headless world, but that cannot erase the gameplay producer.
		EmitAiSoundEvent();
		IElysiumAudio* Audio = World ? World->Audio() : nullptr;
		if (!Audio || SoundName.IsEmpty())
		{
			return;
		}
		Audio->StopVoice(VoiceHandle, 0.f);

		FElysiumAudioRequest Request;
		Request.Source = FElysiumAudioSource::Path(SoundPath());
		Request.Owner.Kind = EElysiumAudioOwnerKind::MapEntity;
		Request.Owner.StableId = FString::Printf(TEXT("entity:%u:%d"), Handle.Epoch, Handle.Index);
		Request.Category = EElysiumAudioCategory::Auto;
		Request.Gain = Volume;
		Request.Pitch = Pitch;
		Request.bLooping = bLooping;
		Request.Placement.bSpatialized = (SpawnFlags & SF_AMBIENT_EVERYWHERE) == 0;
		// The Unreal attenuation sphere the voice is submitted with (a visual-only swap of the falloff
		// law; `m_iSoundLevel` is the retail word). Clamped to 1 cm so a zero radius still spatializes.
		Request.AttenuationRadiusCm = FMath::Max(Radius * AmbientInchToCm, 1.f);
		Request.FadeInSeconds = FadeInSeconds;
		Request.Placement.Location = Def ? Def->Origin : FVector::ZeroVector;
		Request.Placement.AttachTo = ResolveParentComponent();
		// `m_nSndFlags` bits `0x1000` / `0x800` (Spawn step 12), routed as the mixer's flags.
		if ((SndFlags & AMBIENT_SND_FLAG_NO_VOICE_DUCK) != 0)
		{
			Request.Routing |= EElysiumAudioRouting::NoVoiceDuck;
		}
		if ((SndFlags & AMBIENT_SND_FLAG_NO_SFX) != 0)
		{
			Request.Routing |= EElysiumAudioRouting::NoGameplayNoise;
		}

		VoiceHandle = Audio->Submit(MoveTemp(Request));
		if (!VoiceHandle.IsValid())
		{
			UE_LOG(LogElysiumAmbient, Verbose, TEXT("%s: PlaySound '%s' failed (missing/undecodable)"),
				*DebugString(), *SoundName);
		}
	}

	void StopActiveVoice(float FadeOutSeconds)
	{
		if (IElysiumAudio* Audio = World ? World->Audio() : nullptr)
		{
			Audio->StopVoice(VoiceHandle, FadeOutSeconds);
		}
		VoiceHandle = FElysiumAudioVoiceHandle::Invalid();
	}

	void EmitAiSoundEvent()
	{
		// `ambient_generic::Use` 0x101ad470 inserts only a nonzero raw event.  `sound_event=0`
		// remains audio-only (notably every tutorial diversion), regardless of sound_event_level.
		if (SoundEventType == 0 || World == nullptr)
		{
			return;
		}
		FElysiumEntityHandle Owner;
		if (!SoundEventOwnerName.IsEmpty())
		{
			if (FElysiumEntity* Resolved = World->FindByName(SoundEventOwnerName))
			{
				Owner = Resolved->Handle;
			}
		}
		const uint32 Type = static_cast<uint32>(SoundEventType);
		if (!Owner.IsSet() && (Type & ~(ElysiumGameSounds::Carcass | ElysiumGameSounds::Flinch)) != 0)
		{
			UE_LOG(LogElysiumAmbient, Warning, TEXT("%s: sound_event %u needs a live sound_event_owner"),
				*DebugString(), Type);
			return;
		}
		const int32 Level = FMath::Clamp(SoundEventLevel, 1, 3);
		if (Level != SoundEventLevel)
		{
			UE_LOG(LogElysiumAmbient, Warning, TEXT("%s: sound_event_level %d clamped to %d"),
				*DebugString(), SoundEventLevel, Level);
		}
		const float EventRadiusCm = (Level == 1 ? 180.f : Level == 2 ? 240.f : 1200.f) * 2.54f;
		FElysiumGameSoundRequest Request;
		Request.Position = Owner.IsSet() && World->Resolve(Owner) ? World->Resolve(Owner)->Origin : Origin;
		Request.Category = FName(TEXT("AMBIENT_GENERIC_AI"));
		Request.TypeMask = Type;
		Request.RadiusCm = EventRadiusCm;
		const IElysiumAudio* Audio = World->Audio();
		Request.DurationSeconds = FMath::Max(1.f, Audio ? Audio->SoundDurationSeconds(SoundPath()) : 0.f);
		Request.Source = Owner;
		Request.bForceNonOccludable = true;
		World->GameSounds().Emit(Request, World->NowSeconds());
	}

	// SourceEntityName parents the sound to a moving entity (§7, 154 uses). Resolve the named entity
	// and attach to its brush body so the sound tracks it. An NPC has no brush body, so a named
	// NPC parent returns null and the sound plays world-static at the def origin.
	USceneComponent* ResolveParentComponent()
	{
		if (SourceEntityName.IsEmpty() || !World)
		{
			return nullptr;
		}
		if (const FElysiumEntity* Src = World->FindByName(SourceEntityName))
		{
			return Src->Body;   // UElysiumBrushComponent : UPrimitiveComponent : USceneComponent
		}
		return nullptr;
	}

public:
	// The class datamap (`CAmbientGeneric`, `0x105969a4`), in retail's layout. Keyed rows carry the
	// map's key name; the others the member name.
	// `m_radius` `+0x450`, the `radius` key (Source units). The value an absent key leaves is
	// UNRECOVERED (no constructor or datamap default in the corpus); 1250 is the port's choice.
	float Radius = 1250.f;
	// `m_iSoundLevel` `+0x454`: `0x101ac570`'s answer, stored at `0x101ac326`.
	int32 SoundLevel = 0;
	// `m_dpv` `+0x458`, 25 dwords (SAVE, 100 bytes). The constructor `FUN_101ac230` does not write it;
	// the zero start assumes the entity allocator zero-fills (UNRECOVERED, `L0-r005.md` Q2).
	int32 Dpv[AmbientDpv::Words] = {};
	// `m_fActive` `+0x4bc`, `m_fLooping` `+0x4bd`.
	bool bActive = false;
	bool bLooping = false;
	// `m_iszSound` `+0x4c0`, key `message` (FIELD_SOUNDNAME; the map's spelling, backslashes kept).
	FString SoundName;
	// `m_sSourceEntName` `+0x4c4`, key `SourceEntityName`.
	FString SourceEntityName;
	// `+0x4c8`, the source handle (constructor and Spawn write -1; Activate resolves it; name UNRECOVERED).
	FElysiumEntityHandle SoundSource;
	// `m_nSoundEvent` `+0x4cc`, `m_nSoundEventLevel` `+0x4d0`, `m_iszSoundEventOwner` `+0x4d4`.
	int32 SoundEventType = 0;
	int32 SoundEventLevel = 0;
	FString SoundEventOwnerName;
	// `m_bNoSFX` `+0x4dc`, `m_bNoVoiceDuck` `+0x4dd`, `m_bSkipCollide` `+0x4de`, `m_bForceLooping`
	// `+0x4df` (constructor-zeroed), the `flag_*` keys.
	bool bNoSFX = false;
	bool bNoVoiceDuck = false;
	bool bSkipCollide = false;
	bool bForceLooping = false;
	// `m_nSndFlags` `+0x4e0` (constructor-zeroed).
	int32 SndFlags = 0;

private:
	// The voice the port submits: gain and pitch are taken from the dpv words at Activate.
	float   Volume = 1.f;        // 0..1 linear
	float   Pitch = 1.f;
	bool    bDesiredPlaying = false;   // the intent (PlaySound/StopSound); the actual voice mirrors it

	FElysiumAudioVoiceHandle VoiceHandle;
};

// --- Registration ---

static TUniquePtr<FElysiumEntity> MakeAmbientGeneric() { return MakeUnique<FElysiumAmbientGeneric>(); }

static FElysiumClassRegistrar GRegAmbientGeneric(
	TEXT("ambient_generic"), ElysiumBaseClassName(), &MakeAmbientGeneric,
	[](FElysiumClassDesc& D)
	{
		// The class datamap rows (`datamap_records-vampire.dll.json` `CAmbientGeneric`; `walks/L0-r003.md`,
		// `walks/L0-r005.md`). A keyed row is registered under its key, the rest under the member name.
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("message"), &FElysiumAmbientGeneric::SoundName,
			EElysiumField::Key | EElysiumField::Save);   // +0x4c0 m_iszSound
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("radius"), &FElysiumAmbientGeneric::Radius,
			EElysiumField::Key | EElysiumField::Save);   // +0x450 m_radius
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("SourceEntityName"), &FElysiumAmbientGeneric::SourceEntityName,
			EElysiumField::Key | EElysiumField::Save);   // +0x4c4 m_sSourceEntName
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("sound_event"), &FElysiumAmbientGeneric::SoundEventType,
			EElysiumField::Key | EElysiumField::Save);   // +0x4cc m_nSoundEvent
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("sound_event_level"), &FElysiumAmbientGeneric::SoundEventLevel,
			EElysiumField::Key | EElysiumField::Save);   // +0x4d0 m_nSoundEventLevel
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("sound_event_owner"), &FElysiumAmbientGeneric::SoundEventOwnerName,
			EElysiumField::Key | EElysiumField::Save);   // +0x4d4 m_iszSoundEventOwner
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("flag_no_sfx"), &FElysiumAmbientGeneric::bNoSFX,
			EElysiumField::Key | EElysiumField::Save);   // +0x4dc m_bNoSFX
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("flag_no_voice_duck"), &FElysiumAmbientGeneric::bNoVoiceDuck,
			EElysiumField::Key | EElysiumField::Save);   // +0x4dd m_bNoVoiceDuck
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("flag_skip_collide"), &FElysiumAmbientGeneric::bSkipCollide,
			EElysiumField::Key | EElysiumField::Save);   // +0x4de m_bSkipCollide
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("flag_force_looping"), &FElysiumAmbientGeneric::bForceLooping,
			EElysiumField::Key | EElysiumField::Save);   // +0x4df m_bForceLooping
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("m_iSoundLevel"), &FElysiumAmbientGeneric::SoundLevel,
			EElysiumField::Save);                        // +0x454 (no key: written by Spawn)
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("m_fActive"), &FElysiumAmbientGeneric::bActive,
			EElysiumField::Save);                        // +0x4bc
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("m_fLooping"), &FElysiumAmbientGeneric::bLooping,
			EElysiumField::Save);                        // +0x4bd
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("m_nSndFlags"), &FElysiumAmbientGeneric::SndFlags,
			EElysiumField::Save);                        // +0x4e0
		// `m_dpv` +0x458: one 100-byte SAVE row in retail with no interior names, so each dword is a
		// row named by its byte offset in the block (`m_dpv[0x3c]` = the current pitch).
		for (int32 Word = 0; Word < AmbientDpv::Words; ++Word)
		{
			ElysiumAddClassFieldVia<FElysiumAmbientGeneric>(D, *FString::Printf(TEXT("m_dpv[0x%02x]"), Word * 4),
				[Word](auto& E) -> auto& { return E.Dpv[Word]; }, EElysiumField::Save);
		}
		D.Input(TEXT("PlaySound"),   [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumAmbientGeneric&>(E).InputPlaySound(); });
		D.Input(TEXT("StopSound"),   [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumAmbientGeneric&>(E).InputStopSound(); });
		D.Input(TEXT("ToggleSound"), [](FElysiumEntity& E, const FElysiumInputArgs&)   { static_cast<FElysiumAmbientGeneric&>(E).InputToggleSound(); });
		D.Input(TEXT("Volume"),      [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumAmbientGeneric&>(E).InputVolume(A.Param); });
		D.Input(TEXT("FadeIn"),      [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumAmbientGeneric&>(E).InputFadeIn(A.Param); });
		D.Input(TEXT("FadeOut"),     [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumAmbientGeneric&>(E).InputFadeOut(A.Param); });
	});
