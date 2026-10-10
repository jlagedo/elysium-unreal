// ambient_generic: VtMB's point sound (66 on the tutorial, 1,631 across 95 maps;
// audio_pipeline.md §7). A plain-C++ FElysiumEntity leaf that plays a WAV/MP3 at its source entity's
// origin through the GI audio subsystem's voice pool, honouring the Source spawnflags (everywhere /
// start-silent / not-looped) and the I/O input surface the maps wire (PlaySound 903, StopSound 248,
// Volume 36 -- entity_io.md; `FadeIn`/`FadeOut` have no retail datamap record and are `AcceptInput`
// refusals, entity_io.md:2758). Registration follows ElysiumStarterClasses.cpp.
//
// The whole of `CAmbientGeneric` is ported arm for arm from `docs/specs/layers/L0-entity/walks/
// L0-r005.md` and `L0-r006.md`: the KeyValue override `vfunc110` `0x101ada80` (the `m_dpv` modulation
// keys), `Spawn` `0x101ac310`, its tail `Precache` `0x101ac930`, the dpv pass `FUN_101ad0f0`, the
// `Activate` override `vfunc113` `0x101ac9c0` (the source handle, the activation emission, the first
// think), the modulation think `FUN_101acb70` (`m_pfnThink`, 0.2 s), the toggle `Use` `FUN_101ad470`
// (`m_pfnUse`; PlaySound / StopSound / ToggleSound call it with useType 1 / 0 / 3), `InputPitch`
// `0x101ac690`, `InputVolume` `0x101ac7d0` and the AI sound-event owner cache `FUN_101ad9a0`. Field
// names are the class datamap's (`datamap_records-vampire.dll.json`, `CAmbientGeneric` at `0x105969a4`);
// `m_dpv` is one 100-byte SAVE block there, its interior words unnamed in retail, so they are
// registered as `m_dpv[<byte offset>]`. The two handles `+0x4c8` / `+0x4d8` have no datamap record at
// all (not saved, not keyed; the constructor and Spawn write -1, Activate / Use resolve them).
//
// The one sound output of the class is `FUN_101cdac0` (`IEngineSound` through `VEngineServer014`
// slot 49; audit row `settled:engine_replaced`): the port renders it on the audio subsystem's voice
// pool -- start, stop, change-volume, change-pitch -- with the retail arguments stated at every call
// as a `retail_site` (`ambient_emit`).

#include "ElysiumAudioSubsystem.h"
#include "ElysiumBrushComponent.h"
#include "ElysiumClassRegistry.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumRetailSite.h"               // the `ambient_level` / `ambient_store` / `kv_key` / ... taps
#include "ElysiumRng.h"                      // the LFO's `RandomInt(0, 255)` (type 3)
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

	// The flag words the class hands `FUN_101cdac0` (`L0-r006.md` § 0x101cdac0): the low bits match
	// the SDK's `SND_CHANGE_VOL` / `SND_CHANGE_PITCH` / `SND_STOP` / `SND_SPAWNING`; the engine side is
	// UNRECOVERED (Emit itself ORs `0x80` before the engine call). Every non-STOP emit ORs `m_nSndFlags`
	// in through `0x101ac670`; STOP is passed raw.
	constexpr int32 AMBIENT_EMIT_CHANGE_VOL   = 0x1;
	constexpr int32 AMBIENT_EMIT_CHANGE_PITCH = 0x2;
	constexpr int32 AMBIENT_EMIT_STOP         = 0x4;
	constexpr int32 AMBIENT_EMIT_SPAWNING     = 0x8;

	// The `useType` `FUN_101ad470` switches on (`CMP ECX,3/1/0/2`): the SDK's USE_OFF / ON / SET / TOGGLE.
	constexpr int32 AMBIENT_USE_OFF    = 0;
	constexpr int32 AMBIENT_USE_ON     = 1;
	constexpr int32 AMBIENT_USE_SET    = 2;
	constexpr int32 AMBIENT_USE_TOGGLE = 3;

	// The think cadence and the first-think delay: `0x10449198` = 0.2 (double), `0x104493d0` = 0.1
	// (double), added to `curtime` (`[0x1070b228] + 0xc`) as a float.
	constexpr float AmbientThinkPeriodSeconds = 0.2f;
	constexpr float AmbientFirstThinkSeconds = 0.1f;
	// `0x1044e658` = 0.01 (double): the current volume word is emitted `* 0.01`.
	constexpr float AmbientVolumeScale = 0.01f;

	// Source keyvalue `radius` is raw Source units; geometry (origins) is already cm (the UE_
	// convention). Radius converts inches->cm for the Unreal attenuation sphere.
	constexpr float AmbientInchToCm = 2.54f;

	// `VolumeLevels[1..3]` of `vdata/System/sound_volume_table.txt` (`SoundVolumeTable 0x1072bc20 +
	// 0xAC`, `.bss`, loaded by `0x101af9f0` -- its walk is UNRECOVERED; the values are the shipped
	// file's): the AI-sound reach `Use` U4 inserts, in Source units.
	constexpr float AmbientSoundEventLevels[4] = { 0.f, 180.f, 240.f, 1200.f };

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
		constexpr int32 LfoRate     = 10;   // +0x480 8.8 phase step (running, signed: flips at the bounce)
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
	// `__ftol`, `SHL EAX,8`. `fadein 10` -> 512, `2` -> 2560, `3` -> 1536, `0.75` -> 6656, `30` -> 0,
	// `0` -> +Inf -> 0 (the integer indefinite's low dword; 635 shipped ambients author `fadein 0`).
	// The two doubles are the stored constants, so an exact quotient (x = 5: 20 * 0.2000000000000000111)
	// lands just above its integer and truncates to it, as retail's does.
	int32 AmbientFadeRate(const FString& Value)
	{
		const double X = FCString::Atod(*Value);
		const double Twenty = 100.0 / X * 0.2;
		return AmbientFtolLowDword(Twenty) * 256;   // `SHL EAX,8` on the (possibly negative) dword
	}

	// `x >> 8` on a signed dword (`SAR EAX,8`): the 8.8 accumulators are read with an arithmetic shift,
	// so a negative accumulator (a fade-out past the floor) reads as a negative word. Spelled out
	// rather than relying on `>>` of a negative `int32`.
	int32 AmbientSar8(int32 V) { return V >= 0 ? (V >> 8) : -((-V + 255) >> 8); }

	// The entity column of a site payload: the targetname, else the handle (`#<index>`).
	FString AmbientEntityLabel(const FElysiumEntity* Entity)
	{
		if (Entity == nullptr) { return TEXT("null"); }
		return Entity->TargetName.IsEmpty() ? Entity->Handle.ToString() : Entity->TargetName;
	}
}

// --- ambient_generic ---

class FElysiumAmbientGeneric final : public FElysiumEntity
{
public:
	// --- Inputs (registered below; reach here through the class-chain thunk) ---------------
	// The three sound inputs forward `inputdata_t` to `m_pfnUse` (`FUN_101ad470`): `InputPlaySound`
	// `0x101ad3e0` -> `Use(activator, caller, 1, 0)`, `InputStopSound` `0x101ad410` -> `(…, 0, 0)`,
	// `InputToggleSound` `0x101ad440` -> `(…, 3, 0)`. `value` is 0 on all three; SET (2) has no caller.
	void InputPlaySound(const FElysiumInputArgs& A)   { AmbientUse(A.Activator, A.Caller, AMBIENT_USE_ON, 0.f); }
	void InputStopSound(const FElysiumInputArgs& A)   { AmbientUse(A.Activator, A.Caller, AMBIENT_USE_OFF, 0.f); }
	void InputToggleSound(const FElysiumInputArgs& A) { AmbientUse(A.Activator, A.Caller, AMBIENT_USE_TOGGLE, 0.f); }

	// `CAmbientGeneric::InputPitch` `0x101ac690` (245 B, `RET 4`), the datamap INPUT record `Pitch`
	// (FIELD_FLOAT). P0-P3 in retail order.
	void InputPitch(const FElysiumVariant& Param)
	{
		// P0. `variant +0x18 == 1` (float) -> `+0x08`, else `0.0f` (`0x104454c4`). `AcceptInput` has
		//     converted the parameter to the record's FIELD_FLOAT before this body runs (an int, a
		//     numeric string), which is what `ToFloat` does here; a void parameter is 0.0 on both sides.
		float V = Param.IsVoid() ? 0.f : Param.ToFloat();
		// P1. `> 255.0f` (`0x1044fffc`) -> 255; else `< 0.0f` -> 0 (a NaN keeps its value: `FCOMP;
		//     TEST AH,5; JP`). Then `__ftol`, truncation toward zero.
		if (V > 255.f) { V = 255.f; }
		else if (V < 0.f) { V = 0.f; }
		const int32 Pitch = AmbientFtolLowDword(V);
		// P2. `+0x494` <- pitch. Written whether or not a source is live; `m_fActive` is not read.
		Dpv[AmbientDpv::Pitch] = Pitch;
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("ambient_pitch"), TEXT("CAmbientGeneric::InputPitch"), 0x101ac690u, TEXT("write"),
				FString::Printf(TEXT("field=m_dpv[0x3c] value=%d param=%s"), Pitch, *Param.Describe()));
		}
		// P3. The cached handle only (`+0x4c8`, never re-resolved here): if valid, emit
		//     `(edict, origin, name, +0x4a4 * 0.01, +0x454, m_nSndFlags | 2, pitch)`.
		if (FElysiumEntity* Src = ResolvedSource())
		{
			EmitAmbient(TEXT("CAmbientGeneric::InputPitch"), 0x101ac690u, *Src,
				static_cast<float>(Dpv[AmbientDpv::Vol]) * AmbientVolumeScale, SoundLevel,
				SndFlagsOr(AMBIENT_EMIT_CHANGE_PITCH), Pitch);
		}
	}

	// `CAmbientGeneric::InputVolume` `0x101ac7d0` (257 B), the datamap INPUT record `Volume`
	// (FIELD_FLOAT): the variant as above, `> 10.0f` (`0x1044e664`) -> 10, `< 0.0f` -> 0, `* 10.0f`,
	// `__ftol` -> `+0x4a4` (no `[1, 100]` clamp, no dpv pass); if the cached source is valid, emit
	// `(edict, origin, name, vol * 0.01, +0x454, m_nSndFlags | 1, +0x494)`.
	void InputVolume(const FElysiumVariant& Param)
	{
		float V = Param.IsVoid() ? 0.f : Param.ToFloat();
		if (V > 10.f) { V = 10.f; }
		else if (V < 0.f) { V = 0.f; }
		const int32 Vol = AmbientFtolLowDword(static_cast<double>(V * 10.f));
		Dpv[AmbientDpv::Vol] = Vol;
		if (World != nullptr)
		{
			World->EmitRetailSite(*this, TEXT("ambient_volume"), TEXT("CAmbientGeneric::InputVolume"), 0x101ac7d0u, TEXT("write"),
				FString::Printf(TEXT("field=m_dpv[0x4c] value=%d param=%s"), Vol, *Param.Describe()));
		}
		if (FElysiumEntity* Src = ResolvedSource())
		{
			EmitAmbient(TEXT("CAmbientGeneric::InputVolume"), 0x101ac7d0u, *Src,
				static_cast<float>(Vol) * AmbientVolumeScale, SoundLevel,
				SndFlagsOr(AMBIENT_EMIT_CHANGE_VOL), Dpv[AmbientDpv::Pitch]);
		}
	}

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
		// 1-2. `0x101ac2f0` (`m_spawnflags & 1`) and `FUN_101ac570(m_radius, flag)` at `0x101ac321` (a
		//      cdecl call, `ADD ESP,8`); the result is stored in `m_iSoundLevel` (`+0x454`) at
		//      `0x101ac326`. Written before the empty-sound test, so the removed ambient carries it too.
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
		//    only. The port's think identity is the class's (`ThinkAt`, the modulation think below).
		// 7. `m_flNextThink` (`+0x17c`) <- 0: no think is scheduled by Spawn. Activate (`vfunc113`
		//    `0x101ac9c0`) arms it, and only for an active ambient.
		NextThink = ELYSIUM_NEVER_THINK;
		// 8. `m_pfnUse` (`+0x1f0`) <- `0x100131e2` (`FUN_101ad470`, `AmbientUse` below).
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

	// `CAmbientGeneric::Precache` `0x101ac930`: the class's override of slot 104 (`CBaseEntity::Precache`
	// `0x10026b90`, a bare `ret`), reached as Spawn's tail by the virtual `JMP [vtable + 0x1a0]`.
	virtual void Precache() override
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

	// `CAmbientGeneric::vfunc113` `0x101ac9c0` (335 B): slot 113, the `CBaseEntity::Activate`
	// override (the base `0x100a0bc0` is not called). Run once per level activation by `ServerActivate`
	// `CServerGameDLL::vfunc4 0x1011aaf0` (the port's `FElysiumEntityWorld::Activate` pass, and its
	// restore barrier) -- not per frame. Arms H0-H4 in retail order (`L0-r006.md`).
	virtual void Activate() override
	{
		if (World == nullptr)
		{
			return;
		}
		// H0. `h = +0x4c8`. Valid = `h != -1 && table[h & 0x1fff].serial == h >> 13 && .entity != 0`
		//     (`PTR_DAT_10566458`, 12-byte records). The port's handle is (index, epoch) and `Resolve`
		//     is the same three tests: unset, a stale epoch, or a slot whose entity is gone.
		const TCHAR* Via = TEXT("cached");
		if (World->Resolve(SoundSource) == nullptr)
		{
			// H1. `m_sSourceEntName != NULL` -> `FindEntityByName(0x106eb5d8, NULL, name, 0, 0)`
			//     (`0x100f7770`): found -> its `GetRefEHandle()` (vslot 1), else -1. An empty name reaches
			//     the lookup and answers NULL there -- the same -1 -- so the port skips it as one case.
			if (!SourceEntityName.IsEmpty())
			{
				FElysiumEntity* Found = World->FindByName(SourceEntityName);
				SoundSource = Found != nullptr ? Found->Handle : FElysiumEntityHandle::Invalid();
				Via = TEXT("lookup");
			}
			// H2. Still invalid -> `this->GetRefEHandle()`: the self fallback, written without a test.
			if (World->Resolve(SoundSource) == nullptr)
			{
				SoundSource = Handle;
				Via = TEXT("self");
			}
		}
		World->EmitRetailSite(*this, TEXT("ambient_activate"), TEXT("CAmbientGeneric::vfunc113"), 0x101ac9c0u, TEXT("write"),
			FString::Printf(TEXT("field=m_hSoundSource value=%s via=%s m_fActive=%d"),
				SoundSource == Handle ? TEXT("self") : *AmbientEntityLabel(World->Resolve(SoundSource)), Via, bActive ? 1 : 0));
		// H3. `m_fActive != 0`:
		if (bActive)
		{
			// H3a. A valid handle -> `Emit(e + 0x2e0, e->GetAbsOrigin(), name or "", (float)+0x4a4 * 0.01,
			//      +0x454, m_nSndFlags | 0x8, +0x494)`: the activation ("spawning") emission.
			if (FElysiumEntity* Src = World->Resolve(SoundSource))
			{
				EmitAmbient(TEXT("CAmbientGeneric::vfunc113"), 0x101ac9c0u, *Src,
					static_cast<float>(Dpv[AmbientDpv::Vol]) * AmbientVolumeScale, SoundLevel,
					SndFlagsOr(AMBIENT_EMIT_SPAWNING), Dpv[AmbientDpv::Pitch]);
			}
			// H3b. `m_flNextThink = curtime + 0.1` (`0x104493d0`), whether or not H3a emitted: the first
			//      run of the modulation think, which reschedules itself at +0.2.
			NextThink = static_cast<float>(World->NowSeconds()) + AmbientFirstThinkSeconds;
			World->EmitRetailSite(*this, TEXT("ambient_activate"), TEXT("CAmbientGeneric::vfunc113"), 0x101ac9c0u, TEXT("write"),
				FString(TEXT("field=m_flNextThink value=curtime+0.1")));
		}
		// H4. Inactive: nothing, no reschedule.
	}

	// `FUN_101acb70` `0x101acb70` (`m_pfnThink`, via `0x1000969c`): the modulation think. Arms T0-T5 in
	// retail order. Reads the RUNNING words (`+0x464` spinup, `+0x468` spindown, `+0x474` fadein,
	// `+0x478` fadeout, `+0x47c` lfotype); writes the 8.8 accumulators, the current pitch and volume,
	// and emits once per think when something changed. `Now` is `curtime` (`[0x1070b228] + 0xc`).
	virtual void ThinkAt(double Now) override
	{
		if (World == nullptr)
		{
			return;
		}
		// Locals: `vol = +0x4a4`, `pitch = +0x494`, `flags = 0x101ac670(this, 0)` = `m_nSndFlags` (so a
		// non-zero `m_nSndFlags` leaves T4's gate to `changed` alone), `changed = 0`.
		int32 Vol = Dpv[AmbientDpv::Vol];
		int32 Pitch = Dpv[AmbientDpv::Pitch];
		int32 EmitFlags = SndFlagsOr(0);
		bool bChanged = false;
		const int32 SpinUp = Dpv[AmbientDpv::SpinUp];
		// T0. Idle: all five running words zero -> return, no reschedule.
		if (SpinUp == 0 && Dpv[AmbientDpv::SpinDown] == 0 && Dpv[AmbientDpv::FadeIn] == 0
			&& Dpv[AmbientDpv::FadeOut] == 0 && Dpv[AmbientDpv::LfoType] == 0)
		{
			World->EmitRetailSite(*this, TEXT("ambient_think"), TEXT("FUN_101acb70"), 0x101acb70u, TEXT("branch"), TEXT("idle=1"));
			return;
		}
		// T1. Pitch, when spinup or spindown is non-zero.
		if (SpinUp != 0 || Dpv[AmbientDpv::SpinDown] != 0)
		{
			const int32 Old = AmbientSar8(Dpv[AmbientDpv::PitchFrac]);
			if (SpinUp >= 1)                             { Dpv[AmbientDpv::PitchFrac] += SpinUp; }
			else if (Dpv[AmbientDpv::SpinDown] > 0)      { Dpv[AmbientDpv::PitchFrac] -= Dpv[AmbientDpv::SpinDown]; }
			Pitch = AmbientSar8(Dpv[AmbientDpv::PitchFrac]);
			if (Pitch > Dpv[AmbientDpv::PitchRun])       // past the ceiling: spinup stops, pitch = ceiling
			{
				Dpv[AmbientDpv::SpinUp] = 0;
				Pitch = Dpv[AmbientDpv::PitchRun];
			}
			if (Pitch < Dpv[AmbientDpv::PitchStart])     // under the floor: spindown stops, STOP, no reschedule
			{
				Dpv[AmbientDpv::SpinDown] = 0;
				World->EmitRetailSite(*this, TEXT("ambient_think"), TEXT("FUN_101acb70"), 0x101acb70u, TEXT("branch"),
					FString::Printf(TEXT("stop=pitch pitch=%d floor=%d"), Pitch, Dpv[AmbientDpv::PitchStart]));
				EmitStopIfSourceValid(TEXT("FUN_101acb70"), 0x101acb70u);
				return;
			}
			Pitch = Pitch < 0x100 ? FMath::Max(Pitch, 1) : 0xff;
			bChanged |= (Old != Pitch);
			Dpv[AmbientDpv::Pitch] = Pitch;
			EmitFlags |= AMBIENT_EMIT_CHANGE_PITCH;
		}
		// T2. Fade, when fadein or fadeout is non-zero.
		const int32 FadeIn = Dpv[AmbientDpv::FadeIn];
		if (FadeIn != 0 || Dpv[AmbientDpv::FadeOut] != 0)
		{
			const int32 Old = AmbientSar8(Dpv[AmbientDpv::VolFrac]);
			if (FadeIn >= 1)                             { Dpv[AmbientDpv::VolFrac] += FadeIn; }
			else if (Dpv[AmbientDpv::FadeOut] > 0)       { Dpv[AmbientDpv::VolFrac] -= Dpv[AmbientDpv::FadeOut]; }
			Vol = AmbientSar8(Dpv[AmbientDpv::VolFrac]);
			if (Vol > Dpv[AmbientDpv::VolRun])           // past the ceiling: fadein stops, vol = ceiling
			{
				Dpv[AmbientDpv::FadeIn] = 0;
				Vol = Dpv[AmbientDpv::VolRun];
			}
			if (Vol < Dpv[AmbientDpv::VolStart])         // under the floor: fadeout stops, STOP, no reschedule
			{
				Dpv[AmbientDpv::FadeOut] = 0;
				World->EmitRetailSite(*this, TEXT("ambient_think"), TEXT("FUN_101acb70"), 0x101acb70u, TEXT("branch"),
					FString::Printf(TEXT("stop=volume vol=%d floor=%d"), Vol, Dpv[AmbientDpv::VolStart]));
				EmitStopIfSourceValid(TEXT("FUN_101acb70"), 0x101acb70u);
				return;
			}
			Vol = Vol < 0x65 ? FMath::Max(Vol, 1) : 100;
			bChanged |= (Old != Vol);
			EmitFlags |= AMBIENT_EMIT_CHANGE_VOL;
			Dpv[AmbientDpv::Vol] = Vol;
		}
		// T3. LFO (`lfotype != 0`). The emitted pitch/volume carry the modulation; the stored words do not.
		int32 EmitPitch = Pitch;
		const int32 LfoType = Dpv[AmbientDpv::LfoType];
		if (LfoType != 0)
		{
			if (Dpv[AmbientDpv::LfoFrac] > 0x6fffffff) { Dpv[AmbientDpv::LfoFrac] = 0; }
			const int32 Rate = Dpv[AmbientDpv::LfoRate];          // running, SIGNED (|rate| after the dpv pass)
			const int32 AbsRate = FMath::Abs(Rate);
			Dpv[AmbientDpv::LfoFrac] += Rate;
			int32 Pos = AmbientSar8(Dpv[AmbientDpv::LfoFrac]);
			if (Dpv[AmbientDpv::LfoFrac] < 0)                      // the triangle's lower bounce
			{
				Pos = 0;
				Dpv[AmbientDpv::LfoFrac] = 0;
				Dpv[AmbientDpv::LfoRate] = AbsRate;
			}
			else if (Pos > 0xff)                                   // the upper bounce
			{
				Pos = 0xff;
				Dpv[AmbientDpv::LfoFrac] = 0xff00;
				Dpv[AmbientDpv::LfoRate] = -AbsRate;
			}
			if (LfoType == 1)                                      // square
			{
				Dpv[AmbientDpv::LfoMult] = Pos < 0x80 ? 0xff : 0;
			}
			else if (LfoType == 3)                                 // random: `VEngineRandom001` vslot 2 `(0, 0xff)`, drawn at the top only
			{
				if (Pos == 0xff)
				{
					Dpv[AmbientDpv::LfoMult] = ElysiumRng::Stream(EElysiumRngStream::Ambient).RandRange(0, 0xff);
				}
			}
			else                                                   // 2 and 4 (and any other): triangle
			{
				Dpv[AmbientDpv::LfoMult] = Pos;
			}
			if (Dpv[AmbientDpv::LfoModPitch] != 0)
			{
				EmitPitch = Pitch + ((Dpv[AmbientDpv::LfoMult] - 0x80) * Dpv[AmbientDpv::LfoModPitch]) / 100;   // IDIV: toward zero
				EmitPitch = EmitPitch < 0x100 ? FMath::Max(EmitPitch, 1) : 0xff;
				bChanged |= (Pitch != EmitPitch);
				EmitFlags |= AMBIENT_EMIT_CHANGE_PITCH;
			}
			if (Dpv[AmbientDpv::LfoModVol] != 0)
			{
				int32 ModVol = Vol + ((Dpv[AmbientDpv::LfoMult] - 0x80) * Dpv[AmbientDpv::LfoModVol]) / 100;
				ModVol = ModVol < 0x65 ? FMath::Max(ModVol, 0) : 100;
				bChanged |= (Vol != ModVol);
				EmitFlags |= AMBIENT_EMIT_CHANGE_VOL;
				Vol = ModVol;
			}
		}
		// T4. `flags != 0 && changed`, source valid -> emit `(edict, origin, name, vol * 0.01, +0x454,
		//     m_nSndFlags | flags, pitch)` with pitch 100 -> 101.
		World->EmitRetailSite(*this, TEXT("ambient_think"), TEXT("FUN_101acb70"), 0x101acb70u, TEXT("return"),
			FString::Printf(TEXT("vol=%d pitch=%d flags=0x%x changed=%d pitchfrac=%d volfrac=%d"),
				Vol, EmitPitch, EmitFlags, bChanged ? 1 : 0, Dpv[AmbientDpv::PitchFrac], Dpv[AmbientDpv::VolFrac]));
		if (EmitFlags != 0 && bChanged)
		{
			if (EmitPitch == 100) { EmitPitch = 0x65; }
			if (FElysiumEntity* Src = ResolvedSource())
			{
				EmitAmbient(TEXT("FUN_101acb70"), 0x101acb70u, *Src, static_cast<float>(Vol) * AmbientVolumeScale,
					SoundLevel, SndFlagsOr(EmitFlags), EmitPitch);
			}
		}
		// T5. `m_flNextThink = curtime + 0.2` (`0x10449198`).
		NextThink = static_cast<float>(Now) + AmbientThinkPeriodSeconds;
	}

	// `FUN_101ad470` `0x101ad470` (1054 B; `m_pfnUse` via `0x100131e2`): `Use(activator, caller, int
	// useType, float value)`. Arms U0-U4 and the tail in retail order. `useType` is an int (`CMP
	// ECX,3/1/0/2`); `value` is the fourth stack argument, read only by SET.
	void AmbientUse(const FElysiumEntityHandle& Activator, const FElysiumEntityHandle& Caller, int32 UseType, float Value)
	{
		(void)Caller;   // read only by FindEntityByName's `!caller` arm, which U4 never reaches (caller NULL there)
		(void)Activator;
		if (World == nullptr)
		{
			return;
		}
		World->EmitRetailSite(*this, TEXT("ambient_use"), TEXT("CAmbientGeneric::Use"), 0x101ad470u, TEXT("entry"),
			FString::Printf(TEXT("useType=%d value=%g m_fActive=%d m_fLooping=%d"), UseType, Value, bActive ? 1 : 0, bLooping ? 1 : 0));
		// U0. Not TOGGLE: ON while active returns; OFF while inactive returns.
		if (UseType != AMBIENT_USE_TOGGLE)
		{
			if (bActive && UseType == AMBIENT_USE_ON) { return; }
			if (!bActive && UseType == AMBIENT_USE_OFF) { return; }
		}
		if (UseType == AMBIENT_USE_SET)
		{
			// U1. SET while active: `v > 1.0 (0x10449280) -> 1.0f; v < 0.0 (0x1044fab0) -> 0.01f
			//     (0x10450aa4)`; `+0x494 = ftol(v * 255.0f)`; source valid -> emit `(edict, origin, name,
			//     0.0f, 0, m_nSndFlags | 2, pitch)`; return, no reschedule. SET while inactive falls to U3.
			if (bActive)
			{
				float V = Value;
				if (V > 1.f) { V = 1.f; }
				else if (V < 0.f) { V = 0.01f; }
				const int32 Pitch = AmbientFtolLowDword(static_cast<double>(V * 255.f));
				Dpv[AmbientDpv::Pitch] = Pitch;
				World->EmitRetailSite(*this, TEXT("ambient_use"), TEXT("CAmbientGeneric::Use"), 0x101ad470u, TEXT("write"),
					FString::Printf(TEXT("arm=set field=m_dpv[0x3c] value=%d"), Pitch));
				if (FElysiumEntity* Src = ResolvedSource())
				{
					EmitAmbient(TEXT("CAmbientGeneric::Use"), 0x101ad470u, *Src, 0.f, 0, SndFlagsOr(AMBIENT_EMIT_CHANGE_PITCH), Pitch);
				}
				return;
			}
		}
		else if (bActive)
		{
			// U2. OFF / TOGGLE while active.
			const int32 CSpinUp = Dpv[AmbientDpv::CSpinUp];
			if (CSpinUp != 0)
			{
				// One more cspinup step: raise the ceiling and let the think spin up to it.
				if (CSpinUp < Dpv[AmbientDpv::CSpinCount]) { return; }
				const int32 Step = Dpv[AmbientDpv::CSpinCount] + 1;
				Dpv[AmbientDpv::SpinDown] = 0;
				Dpv[AmbientDpv::SpinUp] = Dpv[AmbientDpv::SpinUpSav];
				Dpv[AmbientDpv::CSpinCount] = Step;
				Dpv[AmbientDpv::PitchRun] = ((255 - Dpv[AmbientDpv::PitchStart]) / CSpinUp) * Step + Dpv[AmbientDpv::PitchStart];
				if (Dpv[AmbientDpv::PitchRun] > 255) { Dpv[AmbientDpv::PitchRun] = 255; }
				World->EmitRetailSite(*this, TEXT("ambient_use"), TEXT("CAmbientGeneric::Use"), 0x101ad470u, TEXT("branch"),
					FString::Printf(TEXT("arm=cspinup step=%d pitchrun=%d"), Step, Dpv[AmbientDpv::PitchRun]));
				NextThink = static_cast<float>(World->NowSeconds()) + AmbientFirstThinkSeconds;
				return;
			}
			// `m_fActive = 0`; `m_spawnflags |= 0x10` (the start-silent bit, set at runtime).
			bActive = false;
			SpawnFlags |= SF_AMBIENT_START_SILENT;
			if (Dpv[AmbientDpv::SpinDownSav] == 0 && Dpv[AmbientDpv::FadeOutSav] == 0)
			{
				// No spindown and no fadeout: STOP now (source valid), return with no reschedule.
				World->EmitRetailSite(*this, TEXT("ambient_use"), TEXT("CAmbientGeneric::Use"), 0x101ad470u, TEXT("branch"),
					TEXT("arm=off stop=1 m_fActive=0"));
				EmitStopIfSourceValid(TEXT("CAmbientGeneric::Use"), 0x101ad470u);
				return;
			}
			// Else arm the ramps down: spindown <- initial, spinup <- 0, fadeout <- initial, fadein <- 0;
			// the think takes it from here. Tail.
			Dpv[AmbientDpv::SpinDown] = Dpv[AmbientDpv::SpinDownSav];
			Dpv[AmbientDpv::SpinUp] = 0;
			Dpv[AmbientDpv::FadeOut] = Dpv[AmbientDpv::FadeOutSav];
			Dpv[AmbientDpv::FadeIn] = 0;
			World->EmitRetailSite(*this, TEXT("ambient_use"), TEXT("CAmbientGeneric::Use"), 0x101ad470u, TEXT("branch"),
				FString::Printf(TEXT("arm=off stop=0 m_fActive=0 spindown=%d fadeout=%d"), Dpv[AmbientDpv::SpinDown], Dpv[AmbientDpv::FadeOut]));
			NextThink = static_cast<float>(World->NowSeconds()) + AmbientFirstThinkSeconds;
			return;
		}
		// U3. Start. `m_fLooping == 0` -> STOP (source valid) and stay inactive: a one-shot is never
		//     active, every start is a STOP then a fresh START. Else `m_fActive = 1`.
		if (!bLooping)
		{
			EmitStopIfSourceValid(TEXT("CAmbientGeneric::Use"), 0x101ad470u);
		}
		else
		{
			bActive = true;
		}
		// `FUN_101ad0f0(this)` at `0x101ad70a`: the dpv pass re-seeds the running words.
		InitModulationParms();
		World->EmitRetailSite(*this, TEXT("ambient_use"), TEXT("CAmbientGeneric::Use"), 0x101ad470u, TEXT("branch"),
			FString::Printf(TEXT("arm=start m_fActive=%d vol=%d pitch=%d"), bActive ? 1 : 0, Dpv[AmbientDpv::Vol], Dpv[AmbientDpv::Pitch]));
		if (FElysiumEntity* Src = ResolvedSource())
		{
			// START: `(edict, origin, name, +0x4a4 * 0.01, +0x454, m_nSndFlags | 0, +0x494)`.
			EmitAmbient(TEXT("CAmbientGeneric::Use"), 0x101ad470u, *Src,
				static_cast<float>(Dpv[AmbientDpv::Vol]) * AmbientVolumeScale, SoundLevel, SndFlagsOr(0), Dpv[AmbientDpv::Pitch]);
			// U4. The AI sound event, inside the source-valid gate, after the START.
			InsertAiSound(*Src);
		}
		// Tail. `m_flNextThink = curtime + 0.1`.
		NextThink = static_cast<float>(World->NowSeconds()) + AmbientFirstThinkSeconds;
	}

	// `FUN_101ad9a0` `0x101ad9a0` (174 B, `__fastcall this`): the AI sound-event owner, lazily resolved
	// and cached in `+0x4d8` (no datamap record), serial-validated on reuse. O1-O2.
	FElysiumEntity* ResolveSoundEventOwner()
	{
		if (World == nullptr)
		{
			return nullptr;
		}
		// O1. `h == -1`, a serial mismatch or a null entity -> `FindEntityByName(0x106eb5d8, NULL, name
		//     or "", activator = this, caller = NULL)`; `+0x4d8 <- found ? GetRefEHandle() : -1`. A valid
		//     cached handle skips the lookup (the first match is kept while it lives).
		const TCHAR* Via = TEXT("cached");
		if (World->Resolve(SoundEventOwner) == nullptr)
		{
			FElysiumEntity* Found = FindEntityByNameAsActivator(SoundEventOwnerName);
			SoundEventOwner = Found != nullptr ? Found->Handle : FElysiumEntityHandle::Invalid();
			Via = TEXT("lookup");
		}
		// O2. `h != -1` and the serial matches -> the record's entity (no null test; the port's
		//     `Resolve` answers null for an emptied slot). Else NULL.
		FElysiumEntity* Owner = World->Resolve(SoundEventOwner);
		World->EmitRetailSite(*this, TEXT("ai_sound_owner"), TEXT("FUN_101ad9a0"), 0x101ad9a0u, TEXT("return"),
			FString::Printf(TEXT("owner=%s via=%s name=%s"), *AmbientEntityLabel(Owner), Via, *SoundEventOwnerName));
		return Owner;
	}

	// Dormancy (ScriptHide / Kill) onto the voice: a hidden or killed ambient goes silent; on un-hide an
	// active one is re-started with its current words. Port behaviour kept from before this slice: the
	// retail hide/unhide path for `CAmbientGeneric` is not in the walks (named gap).
	virtual void OnDormancyChanged() override
	{
		FElysiumEntity::OnDormancyChanged();
		if (IsInert())
		{
			StopVoice();
		}
		else if (bActive)
		{
			if (FElysiumEntity* Src = ResolvedSource())
			{
				EmitAmbient(TEXT("CAmbientGeneric::vfunc113"), 0x101ac9c0u, *Src,
					static_cast<float>(Dpv[AmbientDpv::Vol]) * AmbientVolumeScale, SoundLevel,
					SndFlagsOr(AMBIENT_EMIT_SPAWNING), Dpv[AmbientDpv::Pitch]);
			}
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
				SourceEntityName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" · source '%s'"), *SourceEntityName)));
		Out.Emplace(TEXT("Source handle"), SoundSource.IsSet() ? (SoundSource == Handle ? FString(TEXT("self")) : SoundSource.ToString()) : FString(TEXT("-1")));
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

	// `0x101ac670` (`__thiscall`, `RET 4`): `m_nSndFlags | param`.
	int32 SndFlagsOr(int32 Extra) const { return SndFlags | Extra; }

	// The cached source handle's three-part validity test (`+0x4c8`; see Activate H0). Never resolves
	// the name: only Activate writes the handle.
	FElysiumEntity* ResolvedSource() { return World != nullptr ? World->Resolve(SoundSource) : nullptr; }

	// `FindEntityByName(list, NULL, name, activator = this, caller = NULL)` (`0x100f7770`), as U4's
	// owner lookup calls it: `!activator` is this ambient, `!caller` is NULL (no caller), `!player` the
	// player; any other `!name` warns `"Invalid entity search name %s"` and answers NULL (`FUN_100f7460`);
	// a plain name is the first live match (`*` tail allowed). Empty -> NULL.
	FElysiumEntity* FindEntityByNameAsActivator(const FString& Name)
	{
		if (Name.IsEmpty())
		{
			return nullptr;
		}
		if (Name[0] == TEXT('!'))
		{
			if (Name.Equals(TEXT("!activator"), ESearchCase::IgnoreCase)) { return this; }
			if (Name.Equals(TEXT("!caller"), ESearchCase::IgnoreCase))    { return nullptr; }
			if (Name.Equals(TEXT("!player"), ESearchCase::IgnoreCase))    { return World->Resolve(World->PlayerHandle()); }
			if (Name.Equals(TEXT("!playercontroller"), ESearchCase::IgnoreCase)) { return World->FindByName(Name); }
			UE_LOG(LogElysiumAmbient, Warning, TEXT("Invalid entity search name %s"), *Name);
			return nullptr;
		}
		return World->FindByName(Name);
	}

	// The sound output `FUN_101cdac0(p1 = edict, origin, name, vol, level, flags, pitch)` (`__cdecl`;
	// `VEngineServer014` vslot 49 with `flags | 0x80`; a `!`-prefixed name goes through the sentence
	// index, vslot 53, and emits `"!%d"` or nothing). The engine side is replaced by the audio
	// subsystem's voice pool (audit row `0x101cdac0` `settled:engine_replaced`):
	//   - `0x4` STOP: the voice stops;
	//   - `0x1` / `0x2` change-volume / change-pitch: the LIVE voice is re-levelled / re-pitched
	//     (Source's `S_StartSound` drops a change flag that finds no channel: a stopped sound stays
	//     stopped);
	//   - otherwise (`0x0` start, `0x8` spawning): the prior voice is dropped and a fresh one submitted
	//     at the source entity's origin, attached to its body so it follows the entity (the engine binds
	//     an ambient to its edict).
	// Named modernizations: the Unreal attenuation sphere stands for the falloff law (`m_iSoundLevel`
	// is the retail word); pitch 100 is the mixer's 1.0 and a pitch word of 0 is floored to 0.01 for
	// the mixer only. The `!` sentence table is absent in the port: nothing plays (named gap).
	void EmitAmbient(const TCHAR* RetailFn, uint32 RetailVa, FElysiumEntity& Source, float Vol, int32 Level, int32 EmitFlags, int32 Pitch)
	{
		World->EmitRetailSite(*this, TEXT("ambient_emit"), RetailFn, RetailVa, TEXT("emit"),
			FString::Printf(TEXT("flags=0x%x vol=%.2f level=%d pitch=%d origin=%g,%g,%g source=%s name=%s"),
				EmitFlags, Vol, Level, Pitch, Source.Origin.X, Source.Origin.Y, Source.Origin.Z,
				&Source == this ? TEXT("self") : *AmbientEntityLabel(&Source), *SoundName));
		IElysiumAudio* Audio = World->Audio();
		if (Audio == nullptr)
		{
			return;
		}
		if ((EmitFlags & AMBIENT_EMIT_STOP) != 0)
		{
			StopVoice();
			return;
		}
		if ((EmitFlags & (AMBIENT_EMIT_CHANGE_VOL | AMBIENT_EMIT_CHANGE_PITCH)) != 0)
		{
			if (Audio->IsVoicePlaying(VoiceHandle))
			{
				if ((EmitFlags & AMBIENT_EMIT_CHANGE_VOL) != 0)   { Audio->SetVoiceVolume(VoiceHandle, Vol); }
				if ((EmitFlags & AMBIENT_EMIT_CHANGE_PITCH) != 0) { Audio->SetVoicePitch(VoiceHandle, FMath::Max(static_cast<float>(Pitch) * 0.01f, 0.01f)); }
			}
			return;
		}
		if (SoundName.IsEmpty() || SoundName[0] == TEXT('!'))
		{
			return;
		}
		Audio->StopVoice(VoiceHandle, 0.f);
		FElysiumAudioRequest Request;
		Request.Source = FElysiumAudioSource::Path(SoundPath());
		Request.Owner.Kind = EElysiumAudioOwnerKind::MapEntity;
		Request.Owner.StableId = FString::Printf(TEXT("entity:%u:%d"), Handle.Epoch, Handle.Index);
		Request.Category = EElysiumAudioCategory::Auto;
		Request.Gain = Vol;
		Request.Pitch = FMath::Max(static_cast<float>(Pitch) * 0.01f, 0.01f);
		Request.bLooping = bLooping;
		Request.Placement.bSpatialized = (SpawnFlags & SF_AMBIENT_EVERYWHERE) == 0;
		Request.AttenuationRadiusCm = FMath::Max(Radius * AmbientInchToCm, 1.f);
		Request.Placement.Location = Source.Origin;
		Request.Placement.AttachTo = Source.GetAttachBody();
		// `m_nSndFlags` bits `0x1000` / `0x800` (Spawn step 12), routed as the mixer's flags; `0x200` /
		// `0x100` have no engine meaning recovered (UNRECOVERED).
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
			UE_LOG(LogElysiumAmbient, Verbose, TEXT("%s: start of '%s' failed (missing/undecodable)"), *DebugString(), *SoundName);
		}
	}

	// The STOP emit every arm shares: `Emit(e + 0x2e0, e->GetAbsOrigin(), name, 0.0f, 0, 0x4, 0)` --
	// flags raw (no `m_nSndFlags`), only when the cached source is valid; no emit otherwise.
	void EmitStopIfSourceValid(const TCHAR* RetailFn, uint32 RetailVa)
	{
		if (FElysiumEntity* Src = ResolvedSource())
		{
			EmitAmbient(RetailFn, RetailVa, *Src, 0.f, 0, AMBIENT_EMIT_STOP, 0);
		}
	}

	void StopVoice()
	{
		if (IElysiumAudio* Audio = World ? World->Audio() : nullptr)
		{
			Audio->StopVoice(VoiceHandle, 0.f);
		}
		VoiceHandle = FElysiumAudioVoiceHandle::Invalid();
	}

	// `Use` U4 (`0x101ad7a9..0x101ad872`), inside the source-valid gate after the START emit.
	void InsertAiSound(FElysiumEntity& Source)
	{
		// `+0x4cc == 0`: no AI sound (every tutorial ambient).
		if (SoundEventType == 0)
		{
			return;
		}
		FElysiumEntity* Owner = ResolveSoundEventOwner();
		const uint32 Type = static_cast<uint32>(SoundEventType);
		if (Owner == nullptr && Type != ElysiumGameSounds::Carcass && Type != ElysiumGameSounds::Flinch)
		{
			// `Warning("%s has invalid NPC sound event parameters\n", GetDebugName())` (`0x1059785c`); no insert.
			UE_LOG(LogElysiumAmbient, Warning, TEXT("%s has invalid NPC sound event parameters"), *DebugString());
			World->EmitRetailSite(*this, TEXT("ai_sound_insert"), TEXT("CAmbientGeneric::Use"), 0x101ad470u, TEXT("branch"),
				FString::Printf(TEXT("refused=owner type=0x%x"), Type));
			return;
		}
		// `+0x4d0` outside `[1, 3]`: `Warning("%s has an invalid sound event level: %d\n")` (`0x10597828`),
		// then `< 4 ? max(1) : 3`, written back.
		if (SoundEventLevel < 1 || SoundEventLevel > 3)
		{
			UE_LOG(LogElysiumAmbient, Warning, TEXT("%s has an invalid sound event level: %d"), *DebugString(), SoundEventLevel);
			SoundEventLevel = SoundEventLevel < 4 ? FMath::Max(SoundEventLevel, 1) : 3;
		}
		// `dur = IEngineSoundServer003 vslot 12 GetSoundDuration(name)`, floored at 1.0 (`0x10449280`).
		const IElysiumAudio* Audio = World->Audio();
		float Duration = Audio != nullptr ? Audio->SoundDurationSeconds(SoundPath()) : 0.f;
		if (Duration < 1.f) { Duration = 1.f; }
		// `iVolume = [0x1072bccc + level * 4]` = `VolumeLevels[level]`; `CSoundEnt::InsertSound(+0x4cc,
		// SOURCE->GetAbsOrigin() (EDI, `m_hSource`; asm 0x101ad85a), iVolume, dur, 0, owner)`.
		const float ReachUnits = AmbientSoundEventLevels[SoundEventLevel];
		FElysiumGameSoundRequest Request;
		Request.Position = Source.Origin;
		Request.Category = FName(TEXT("AMBIENT_GENERIC_AI"));
		Request.TypeMask = Type;
		Request.RadiusCm = ReachUnits * AmbientInchToCm;
		Request.DurationSeconds = Duration;
		Request.Source = Owner != nullptr ? Owner->Handle : FElysiumEntityHandle::Invalid();
		Request.bForceNonOccludable = true;
		World->EmitRetailSite(*this, TEXT("ai_sound_insert"), TEXT("CAmbientGeneric::Use"), 0x101ad470u, TEXT("insert"),
			FString::Printf(TEXT("type=0x%x origin=%g,%g,%g source=%s volume=%g level=%d duration=%g owner=%s"),
				Type, Source.Origin.X, Source.Origin.Y, Source.Origin.Z, &Source == this ? TEXT("self") : *AmbientEntityLabel(&Source),
				ReachUnits, SoundEventLevel, Duration, *AmbientEntityLabel(Owner)));
		World->GameSounds().Emit(Request, World->NowSeconds());
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
	// `+0x4c8`, the source handle (no datamap record: not saved, not keyed; the constructor and Spawn
	// write -1, Activate resolves it; retail name UNRECOVERED, SDK `m_hSoundSource`).
	FElysiumEntityHandle SoundSource;
	// `m_nSoundEvent` `+0x4cc`, `m_nSoundEventLevel` `+0x4d0`, `m_iszSoundEventOwner` `+0x4d4`.
	int32 SoundEventType = 0;
	int32 SoundEventLevel = 0;
	FString SoundEventOwnerName;
	// `+0x4d8`, the sound-event owner cache (no datamap record; the constructor writes -1,
	// `FUN_101ad9a0` resolves it; retail name UNRECOVERED).
	FElysiumEntityHandle SoundEventOwner;
	// `m_bNoSFX` `+0x4dc`, `m_bNoVoiceDuck` `+0x4dd`, `m_bSkipCollide` `+0x4de`, `m_bForceLooping`
	// `+0x4df` (constructor-zeroed), the `flag_*` keys.
	bool bNoSFX = false;
	bool bNoVoiceDuck = false;
	bool bSkipCollide = false;
	bool bForceLooping = false;
	// `m_nSndFlags` `+0x4e0` (constructor-zeroed).
	int32 SndFlags = 0;

private:
	// The voice the port's emit submits (the engine's channel for this edict).
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
			EElysiumField::Key | EElysiumField::Save | EElysiumField::MapKey);   // +0x4c0 m_iszSound
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("radius"), &FElysiumAmbientGeneric::Radius,
			EElysiumField::Key | EElysiumField::Save | EElysiumField::MapKey);   // +0x450 m_radius
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("SourceEntityName"), &FElysiumAmbientGeneric::SourceEntityName,
			EElysiumField::Key | EElysiumField::Save | EElysiumField::MapKey);   // +0x4c4 m_sSourceEntName
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("sound_event"), &FElysiumAmbientGeneric::SoundEventType,
			EElysiumField::Key | EElysiumField::Save | EElysiumField::MapKey);   // +0x4cc m_nSoundEvent
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("sound_event_level"), &FElysiumAmbientGeneric::SoundEventLevel,
			EElysiumField::Key | EElysiumField::Save | EElysiumField::MapKey);   // +0x4d0 m_nSoundEventLevel
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("sound_event_owner"), &FElysiumAmbientGeneric::SoundEventOwnerName,
			EElysiumField::Key | EElysiumField::Save | EElysiumField::MapKey);   // +0x4d4 m_iszSoundEventOwner
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("flag_no_sfx"), &FElysiumAmbientGeneric::bNoSFX,
			EElysiumField::Key | EElysiumField::Save | EElysiumField::MapKey);   // +0x4dc m_bNoSFX
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("flag_no_voice_duck"), &FElysiumAmbientGeneric::bNoVoiceDuck,
			EElysiumField::Key | EElysiumField::Save | EElysiumField::MapKey);   // +0x4dd m_bNoVoiceDuck
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("flag_skip_collide"), &FElysiumAmbientGeneric::bSkipCollide,
			EElysiumField::Key | EElysiumField::Save | EElysiumField::MapKey);   // +0x4de m_bSkipCollide
		ElysiumAddClassField<FElysiumAmbientGeneric>(D, TEXT("flag_force_looping"), &FElysiumAmbientGeneric::bForceLooping,
			EElysiumField::Key | EElysiumField::Save | EElysiumField::MapKey);   // +0x4df m_bForceLooping
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
		// The five INPUT records of the class datamap (`L0-r006.md` § 0x101ada80): `PlaySound`
		// `0x101ad3e0`, `StopSound` `0x101ad410`, `ToggleSound` `0x101ad440` (void, each a `Use` call),
		// `Pitch` `0x101ac690` and `Volume` `0x101ac7d0` (FIELD_FLOAT). There is no `FadeIn`/`FadeOut`
		// record: those wires are `AcceptInput` refusals in retail (entity_io.md:2758), so none is
		// registered here.
		D.Input(TEXT("PlaySound"),   [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumAmbientGeneric&>(E).InputPlaySound(A); });
		D.Input(TEXT("StopSound"),   [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumAmbientGeneric&>(E).InputStopSound(A); });
		D.Input(TEXT("ToggleSound"), [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumAmbientGeneric&>(E).InputToggleSound(A); });
		D.Input(TEXT("Pitch"),       [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumAmbientGeneric&>(E).InputPitch(A.Param); });
		D.Input(TEXT("Volume"),      [](FElysiumEntity& E, const FElysiumInputArgs& A) { static_cast<FElysiumAmbientGeneric&>(E).InputVolume(A.Param); });
	});
