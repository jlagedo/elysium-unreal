#pragma once

#include "CoreMinimal.h"

struct IElysiumRetailSiteSink;

// The sound-script descriptor (`vampire.dll`'s `CSoundParametersInternal`, 0xcc bytes with name
// buffers) and the two parsers that fill its channel: `FUN_101b30d0` `0x101b30d0` constructs it with
// the retail defaults, `FUN_101b24d0` `0x101b24d0` turns a `channel` key's text into a number and
// `FUN_101b2490` `0x101b2490` stores both. The walk is `docs/specs/layers/L0-entity/walks/L0-r007.md`;
// the prose `docs/vtmb/audio_pipeline.md` § 3.
//
// Where retail builds one: `CSoundEmitterSystem::AddSoundsFromFile` `0x101b4240` constructs a
// descriptor per sound entry of every file `scripts/game_sounds_manifest.txt` names (call
// 0x101b4318), the key parser `FUN_101b3bb0` overwrites only the keys the entry carries, and the
// entry is inserted into the sound dictionary (whose node allocator `FUN_101b6bd0` constructs one in
// place per node, 0x101b6c9e). `AddSound` `0x101b4d30` (0x101b4eb4) and the dictionary lookup
// `FUN_101b2f60` (0x101b2f77, a temp) are the other two callers. The shipped manifest precaches
// `game_sounds_surfaceproperties.txt` alone (`audio_pipeline.md` § 10). The key parser and the
// manifest reader are not this story's; the descriptor and its channel are.
namespace ElysiumSoundScript
{
	// One wave entry of the descriptor's two `CUtlVector`s (8 bytes: `u16` filename symbol, `u32`
	// gender tag 0 none / 1 male / 2 female). Filled by the `wave` / `rndwave` keys through
	// `FUN_101b3830` (a `<gender>` token expands to a male and a female entry) -- not this story's.
	struct FWave
	{
		uint16 Symbol = 0;
		uint32 Gender = 0;
	};

	// A float pair `interval_t` (start, range), the shape `volume`, `pitch` and `soundlevel` take.
	struct FInterval
	{
		float Start = 0.f;
		float Range = 0.f;
	};

	// The 32-byte name buffers `Q_strncpy(dst, name, 0x20)` fills: the name zero-padded to 32 bytes,
	// so 31 characters at most.
	constexpr int32 NameBufferSize = 0x20;

	// `CSoundParametersInternal`, by retail offset.
	struct FParams
	{
		int32 Channel = 0;                        // +0x00: `FUN_101b2490` (default 0 = CHAN_AUTO)
		FInterval Volume;                         // +0x04 / +0x08: `volume` (`FUN_101b2420`; `VOL_NORM` -> 1.0, 0)
		FInterval Pitch;                          // +0x0c / +0x10: `pitch` (`FUN_101b2570`; `PITCH_NORM` 100, `PITCH_LOW` 95, `PITCH_HIGH` 120)
		FInterval SoundLevel;                     // +0x14 / +0x18: `soundlevel` (`FUN_101b2630`) / `attenuation`
		uint8 bPlayToOwnerOnly = 0;               // +0x1c: `play_to_owner_only`
		uint8 bPrecache = 0;                      // +0x1d: `precache`
		TArray<FWave> Waves;                      // +0x20..+0x30: the `wave` / `rndwave` entries
		TArray<FWave> SecondList;                 // +0x34..+0x44: a second vector of the same shape; its content is UNRECOVERED
		uint8 bHasMissingWave = 0;                // +0x48: "has a missing wave file"; written by `FUN_101b4740` at BaseInit, read by `FUN_101b33f0` arm 12 (`walks/L0-r008.md`)
		TCHAR VolumeText[NameBufferSize] = {};    // +0x49: the `volume` key's text ("VOL_NORM")
		TCHAR ChannelText[NameBufferSize] = {};   // +0x69: the `channel` key's text ("CHAN_AUTO")
		TCHAR SoundLevelText[NameBufferSize] = {}; // +0x89: the `soundlevel` key's text ("SNDLVL_NORM")
		TCHAR PitchText[NameBufferSize] = {};     // +0xa9: the `pitch` key's text ("PITCH_NORM")
	};

	// `FUN_101b30d0` (152 B, `__fastcall(desc)`, returns `desc`): the sixteen writes in retail order --
	// the two vectors' ten dwords to 0; channel 0 and "CHAN_AUTO"; volume (1.0, 0) and "VOL_NORM";
	// pitch (100.0, 0) and "PITCH_NORM"; sound level (75.0, 0) and "SNDLVL_NORM"; `+0x1c` 0, `+0x1d`
	// 1, `+0x48` 0. Site: `sndscript.defaults fn=Global::FUN_101b30d0 va=0x101b30d0 phase=return
	// channel=0 volume=1.0 pitch=100 level=75 flags=<+1c>,<+1d>,<+48>` at the return (0x101b3162).
	FParams& Construct(FParams& Params, IElysiumRetailSiteSink* Sites);

	// `FUN_101b24d0` (123 B, `__cdecl(name)`): the channel a `channel` key names. Arms in retail
	// order: NULL -> 0 (no warning); `Q_strncasecmp(name, "chan_", 5) != 0` -> `atoi(name)`; one of
	// the seven `CHAN_*` names (table 0x10598c78, case-insensitive) -> its value 0..6; else
	// `DevMsg("CSoundEmitterSystem:  Warning, unknown channel type in sounds.txt (%s)\n", name)` and 0.
	// Site: `sndchan.parse fn=Global::FUN_101b24d0 va=0x101b24d0 phase=return text=<name> ret=<n>
	// warn=<0|1>`. Its live callers: `FUN_101b2490` below (the script key) and, through the thunk
	// 0x101b6100, the `SoundFX` record loader `FUN_101dbe30` (L2); `FUN_101b4c00` is a `__stdcall`
	// wrapper with no caller in the image.
	int32 TextToChannel(const TCHAR* Name, IElysiumRetailSiteSink* Sites);

	// `FUN_101b2490` (`__thiscall(desc, name)`, `RET 4`): `desc+0x00 = FUN_101b24d0(name)`, then
	// `Q_strncpy(desc+0x69, name, 0x20)` -- the text as typed, not canonicalized. The script key
	// parser `FUN_101b3bb0`'s one caller (0x101b3bfe, the `channel` key).
	void SetChannel(FParams& Params, const TCHAR* Name, IElysiumRetailSiteSink* Sites);

	// `Q_strncpy(dst, src, 0x20)` into one of the four name buffers: CRT `strncpy` then `dst[31] = 0`.
	void CopyName(TCHAR (&Dst)[NameBufferSize], const TCHAR* Src);
}
