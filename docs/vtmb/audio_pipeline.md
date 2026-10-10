# VtMB audio pipeline — how it works

How *Vampire: The Masquerade – Bloodlines* produces sound: the codecs and driver,
the mixer, the DSP/reverb bank, the bespoke **SoundScheme** ambience/music system
that replaces Source's `env_soundscape`, point sounds (`ambient_generic`), dialogue,
sentences, and footsteps.

Evidence tags: **[VtMB]** = read from the user's own DLLs (Ghidra strings/symbols/
addresses); **[SDK]** = Source SDK / leaked-engine reference at `$ELYSIUM_WORK_ROOT/research/reference-source/source-engine`;
**[data]** = shipped game data (scripts, WAV/MP3 headers, entity lumps), measured;
**[script]** = a level `.py`; **[inferred]** = reasoned, not yet decompiled.
Retail image bases: `engine.dll` `0x20000000`, `vampire.dll` `0x10000000`,
`client.dll` `0x10000000`. All counts are over the **patched** install (patch-first,
see `../CLAUDE.md`).

## 1. The shape of the divergence

VtMB keeps stock early-Source's **low-level audio engine** intact — the Miles
mixer, WAV/ADPCM/MP3 sources, the 128-channel mixer, the `dsp_presets.txt` DSP
processor graph, `CSentence`, and the `*?!#@><^)}` sound-char prefix grammar all
live in `engine.dll` unchanged from Source. What VtMB **replaces wholesale** is the
*high-level ambience layer*: Source's `env_soundscape` + `scripts/soundscapes.txt`
is gone (dead code + dead data — **zero** `env_soundscape`/`env_speaker`/
`env_microphone`/`trigger_soundscape` entities exist in any of the 108 maps
[data]), swapped for a bespoke **SoundScheme** system — an `ambient_soundscheme`
entity pointing at a `sound/schemes/*.txt` file that bundles *exploration + combat +
alert music*, a *looping ambient bed*, *polar-placed random one-shots*, and a *DSP
room assignment* into one per-area unit. A client-side **music state machine**
cross-fades the music stems on combat state.

The map-authored audio surface has three dedicated entity families:
**`ambient_generic`** (point sounds), **`ambient_soundscheme`** →
**`sound/schemes/*.txt`** (ambience + music + DSP), and brush
**`trigger_environmental_audio`** volumes (`room_type`). Audio controls also live on
movers, NPCs, `events_world`, choreography and generic entity lifetime inputs. Above
the map layer, dialogue, sentences, surfaces, radio/news, weapons, disciplines and
character sound schemes all feed the same mixer. The stock HL2 `soundscapes.txt`,
`sounds.txt`, dormant `game_sounds*.txt` entries and `titles.txt` remain leftovers;
§10 separates those from the live non-map systems.

## 2. Asset formats and storage

Two codecs, split by role [data]:

| Role | Format | Storage | Count |
|---|---|---|---|
| SFX, ambience, voice-in-VPK | **WAV, mostly MS-ADPCM** (tag 2, 4-bit) | VPKs | 3,922 `.wav` (VPK); 5,550 merged |
| Dialogue lines | **MP3** | loose `sound/character/dlg/…` | 5,123 loose |
| Music (explore + `_combat`) | **MP3** | loose `sound/music/…` | ~40 retail + 35 patch |
| Radio loops | **MP3** | loose `sound/radio/radio_loop_[1-5].mp3` | 5 |
| Lipsync | `.lip` | VPK | 5,441 (VPK) / 7,136 (merged) |
| Choreography (dialogue scenes) | `.vcd` | VPK | 5,300 (VPK) / 5,444 (merged) |

**WAV encodings** (RIFF/`fmt ` parsed over a random sample) [data]: **~92% Microsoft
ADPCM (`WAVE_FORMAT_ADPCM` = tag 2), 4-bit, mono, 22050 Hz**, the rest 44100 Hz and a
few stereo; only ~2–3% is 16-bit PCM. **Microsoft ADPCM ≠ IMA ADPCM** (§11).

**Driver** [VtMB]: `engine.dll` sound init `FUN_20118cf0` @ `0x20118cf0`
`LoadLibrary`s **`mss32.dll`** (RAD Miles Sound System) + **`vaudio_miles.dll`** and
creates the `"VAudio001"` interface; Miles is the mixer/streamer and the MP3 decoder.
Honours `-nosound`/`-wavonly`; prints `"Sound sampling rate: %i"`. `binkw32.dll`
handles Bink intro-video audio (`media/*.bik`) separately — outside this pipeline.

**Dialogue localization** [data]: line files carry a language/take suffix —
`line901_col_e.mp3` (English, 4,843), `_col_f` (216), `_col_m` (8), `_col_n` (48).

The shipped dialogue take-letter census across the whole corpus is `e`=4975, `f`=229, `n`=50,
`m`=8, and every non-default take is reachable by some (sex, clan) pair.
Played by `Character.PlayDialogFile("Character/dlg/.../lineNNN_col_e.mp3")` [script],
resolved through the datamap method table (see `docs/vtmb/python_bridge.md`), with the paired
`.lip` driving mouth animation and the `.vcd` sequencing the scene.

**The choreo `speak` path resolves `.mp3` first** [VtMB]: `FUN_10081700` in `vampire.dll`
builds `sound/<param>`, swaps the extension for `.mp3`, and plays `*<name>.mp3` when that
file exists, falling back to the authored `*<name>` (`.wav`) when it does not — the `*`
being Source's stream prefix. So a scene authored against a `.wav` normally plays the
shipped MP3. The scene layer itself: `docs/vtmb/choreographed_scenes.md`.

## 3. The engine: mixer, channels, codecs, prefixes (stock Source)

All in `engine.dll`, unchanged from Source [VtMB] cross-checked against
`$ELYSIUM_WORK_ROOT/research/reference-source/source-engine/public/soundflags.h` + `soundchars.h` [SDK].

**Audio sources / mixers** — `CAudioSourceWave` (ctor @ `0x20139d60`),
`CAudioSourceMemWave`, `CAudioSourceStreamWave`, `CAudioSourceMP3`,
`CAudioSourceStreamMP3`, `CAudioSourceVoice`; mixers `CAudioMixerWave`,
`CAudioMixerWaveMP3`, **`CAudioMixerWaveADPCM`**. Decodes **PCM + MS-ADPCM + MP3**.

**Channels** — `S_StartStaticSound` `FUN_2011f620` @ `0x2011f620`:
`MAX_CHANNELS = 128`; the first **24** are the reserved static/looping region,
dynamic sounds scan from 24→128; each channel record is **160 bytes**; volume is
clamped to 255. Refuses allocation at 128 (`"total_channels == MAX_CHANNELS"`).

**Sound-char prefixes** [VtMB, matches soundchars.h verbatim] — parsed from the
first two chars of a wave name in `FUN_2011f620`, with `!` dispatching the sentence
path (`FUN_2013b870`):

`*` stream · `?` uservox · `!` sentence · `#` drymix (bypass DSP) · `@` omni ·
`>` doppler · `<` directional cone · `^` distance-variant · `)` spatialized-stereo ·
`}` fast (non-interpolated) pitch.

**Channels / attenuation enums** [SDK] — `CHAN_AUTO/WEAPON/VOICE/ITEM/BODY/STREAM/
STATIC/VOICE2` (`CHAN_USER_BASE` for game code); `soundlevel_t` in **dB**
(`SNDLVL_20dB`…`SNDLVL_NORM=75`…`SNDLVL_GUNFIRE=140`); dB↔attenuation via
`ATTN_TO_SNDLVL(a)=50+20/a`, `SNDLVL_TO_ATTN(a)=20/(a-50)`. Distance attenuation is
the standard SNDLVL-dB curve. Voice **ducking** cvars present (`snd_duckattacktime`/
`snd_duckreleasetime`/`snd_duckvolume`) — dialogue ducks other channels.

### The sound-script descriptor and the channel parser (`vampire.dll`) [VtMB, recovered 2026-10-10, `docs/specs/layers/L0-entity/walks/L0-r007.md`]

The game DLL's `CSoundEmitterSystem` keeps one `CSoundParametersInternal` per sound entry (0xcc bytes,
with 32-byte name buffers). `FUN_101b30d0` `0x101b30d0` constructs it: both wave vectors (`+0x20`,
`+0x34`) empty; channel `+0x00` 0 with `"CHAN_AUTO"` at `+0x69`; volume `+0x04/+0x08` (1.0, 0) with
`"VOL_NORM"` at `+0x49`; pitch `+0x0c/+0x10` (100.0, 0) -- the engine's pitch percentage -- with
`"PITCH_NORM"` at `+0xa9`; sound level `+0x14/+0x18` (75.0, 0) with `"SNDLVL_NORM"` at `+0x89`;
`play_to_owner_only` `+0x1c` 0; `precache` `+0x1d` 1; `+0x48` 0 (reader UNRECOVERED). Each name is
`Q_strncpy`'d, zero-padded to 32. `AddSoundsFromFile` `0x101b4240` constructs one per entry of every
file `scripts/game_sounds_manifest.txt` names and the key parser `FUN_101b3bb0` overwrites only the
keys the entry carries, so an omitted key keeps the default. The `channel` key goes through
`FUN_101b2490` `0x101b2490`: `desc+0x00 = FUN_101b24d0(text)` and the text itself kept at `+0x69`.
`FUN_101b24d0` `0x101b24d0`: NULL -> 0; `Q_strncasecmp(text, "chan_", 5) != 0` -> `atoi(text)`
(`"2"` -> 2, `""` -> 0, `"chan"` -> 0); else the seven names `CHAN_AUTO` 0, `CHAN_WEAPON` 1,
`CHAN_VOICE` 2, `CHAN_ITEM` 3, `CHAN_BODY` 4, `CHAN_STREAM` 5, `CHAN_STATIC` 6 (table `0x10598c78`,
case-insensitive); else `DevMsg("CSoundEmitterSystem:  Warning, unknown channel type in sounds.txt
(%s)\n")` -- printed only at `developer` 1 -- and 0. The `SoundFX` record loader `FUN_101dbe30`
parses its `Channel` key through the same function; `FUN_101b4c00` wraps it and has no caller. The
shipped `game_sounds_surfaceproperties.txt` authors `"2"`, `"CHAN_BODY"` and `"chan_voice"`.

Port: `Source/ElysiumUE/Private/Audio/ElysiumSoundScript.{h,cpp}` (`FParams`, `Construct`,
`TextToChannel`, `SetChannel`); `EElysiumSoundChannel::Stream` 5 added. The key parser, the manifest
reader and the dictionary are not ported. Proven by `Arena/scenarios/audio/l0_sound_script_defaults.json`
and `l0_sound_channel_parse.json`.

## 4. DSP / reverb bank

**Format is stock Source; the *selection* is not.**

**Parser** [VtMB] — `DSP_LoadPresetFile` @ `0x20131dd0` opens
`scripts/dsp_presets.txt`; `FUN_20131c40` counts presets; `FUN_20131cc0` resolves a
token as a number *or* a processor name (`strcmpi` against a name table). Grammar
(data + parser agree) [data]:

```
{  <presetID>  LINEAR  g0 g1 g2 g3 g4 g5      // 6 global params
     { <PROC>  p0 … p15 }                     // ≤6 processors, ≤16 floats each
     …
}
```

Processor tokens are stock: `RVA` (reverb), `DFR` (diffusor), `FLT_LP/HP/BP`,
`DLY_PLAIN/LOWPASS/ALLPASS`, `QUA_*`, plus LFO/pitch/envelope/mod-delay families —
documented in the file's own header. The shipped file holds **~141 presets indexed
0–140**, all `LINEAR`; presets **0–28** match the named legend echoed atop
`soundscapes.txt` (`0` "Normal (off)", `1` "Generic", `2` "Metal Small" … `28`
"Weirdo 3"). Parse errors are literal (`"DSP PARSE ERROR!!! … too many processors"`).

**Zone selection — the divergence** [VtMB strings + data + partial decompile]: stock
Source picks DSP from `env_soundscape`/`dsp_room` entities; VtMB has no live
`env_soundscape`. A scheme's `SchemeParams { "RoomDSP" "<n>" }` names one preset,
while brush `trigger_environmental_audio` entities author a numeric `room_type`.
The player networks both `m_sndRoomDSP` and `m_sndPlayerDSP` from `vampire.dll` to
`client.dll`; the client carries `RoomDSP`, `dsp_room`, `WET`, `DRY` and `Ducking`
and drives the engine DSP state. Music blocks flagged `"Dry" "1"` bypass the reverb
bus (the `#`/drymix path) so the score is not smeared by room reverb.

**Precedence is recovered** [VtMB] (`CTriggerEnvAudio`, factory `0x101cbb10`, size
`0x59c`; `m_nRoomType` at `+0x598`; `m_bDisabled` is `StartDisabled` at `+0x55c`).
Spawn runs `CTriggerTeleport`'s InitTrigger: `StartDisabled 0` sets `FSOLID_TRIGGER`,
`StartDisabled 1` **clears** it. Enable/Disable (inherited `CBaseTrigger`
`0x101c4bf0` / `0x101c4dd0`) flip that bit and `PhysicsTouchTriggers`.

DSP Touch is **slot 175** `0x101cbbc0`, not `StartTouch`. It does **not** call
`PassesTriggerFilters`. If the toucher has a player object at `+0xa8`,
`FUN_101753a0` writes `player+0x1e08 = room_type` and stamps `player+0x1e0c` with
an engine counter. EndTouch (`0x101cbc10`) clears to `0xffffffff` only if this
brush still owns the value. Spawnflag 1 (clients) gates `OnStartTouch` outputs
only, not this DSP path.

Resolver `FUN_10175290` (from `CBasePlayer::UpdateClientActionState` `0x101755d0`),
the value that tracks with `m_sndRoomDSP`:

1. if the trigger stamp is older than ~5 engine counts, `player+0x1e08 = -1`;
2. if `player+0x1e08 > 0`, **that `room_type` wins**;
3. else the scheme EHANDLE FadeIn stored at `player+0x1e04` → scheme `RoomDSP`
   (`+0x45c`);
4. else **0**.

`m_sndPlayerDSP` is a separate pick (`FUN_10175220`), not scheme RoomDSP. A live
`room_type > 0` therefore beats the active scheme. `StartDisabled 1` with no
Enable wire never sets `FSOLID_TRIGGER`, so Touch never runs: the 16 tutorial
brushes (room types `123`×8, `5`×3, `104`×2, `12`/`108`/`11`×1; all
`StartDisabled 1`, spawnflags `1`, unnamed, no outputs) are **inert in retail**.
`sm_pawnshop_1` and `sm_hub_1` author none.

## 5. The SoundScheme system (VtMB's `env_soundscape` replacement)

The core deviation. Parser + entity in **`vampire.dll`**, playback + music + DSP in
**`client.dll`** [VtMB strings]; the data is 147 `.txt` in the VPKs / **174 merged**
under `sound/schemes/` (a directory that does not exist in stock Source) [data].

**Entity** [data] — `ambient_soundscheme`, **179 instances across 107 of 108 maps**
(149 distinct scheme files); every playable map has ≥1, most have several (a base
area scheme plus per-room overrides):

```
"classname"     "ambient_soundscheme"
"scheme_file"   "sound/Schemes/ch_cloud.txt"   // resolve case-insensitively
"start_enabled" "1"                            // 1 (×103) / 0 (×76)
"origin"        "-21.9 340 0.45"               // anchor for polar RandomSound placement
"targetname"    "SchemeCity"                   // I/O handle
```

Schemes are switched at runtime by **Source I/O crossfade** — a trigger fires
`OnActivate "SchemeSewer,FadeIn,…"` + `"SchemeCity,FadeOut,…"` to swap the active
bed/music (e.g. entering the ch_hub sewers) [data]. `start_enabled 0` schemes wait
for such a `FadeIn`.

**Scheme grammar** [data, full vocabulary surveyed] — KeyValues, root
`SoundScheme { … }`:

```
SoundScheme
{
    SchemeParams { "RandomSoundCount" "0..7"   // max concurrent random one-shots
                   "RoomDSP"          "<presetID>" }   // §4; 0 = neutral
    Music   { "Filename" "music/Dark_Asia.mp3"        "Volume" "50" }          // explore stem
    Combat  { "Filename" "music/Dark_Asia_Combat.mp3" "Volume" "50" "Dry" "1" "NoPause" "1" } // combat stem
    Alert   { "Filename" "music/police_alert.mp3"     "Volume" "50" "Dry" "1" "NoPause" "1" } // alert stem (rare)
    Ambient { "Filename" "environmental/sewers/sewer ambinc.wav" "Volume" "60" } // one looping bed
    RandomSound { "Filename" "Environmental/Sewers/Running_Pipes7.wav"
                  "PitchMin" "95" "PitchMax" "110" "Volume" "30"
                  "Frequency" "10"                    // fire when Frequency > RandomInt(1, soundscheme_randomness); 0 = never
                  "AudibleRadius" "2000"              // attenuation reach
                  "DistMin" "200"  "DistMax" "1000"   // radial distance from the player (XY)
                  "HeightMin" "0"  "HeightMax" "100"  // vertical offset from the scheme origin (Z)
                  "AngleMin" "330" "AngleMax" "30" }  // linear RandomFloat; wrap arcs interpolate the long way
}
```

Block presence across schemes [data]: `Music` ~140–161, `Combat` ~126–149,
`Ambient` ~130–152, `RandomSound` ~1,240–1,410 total (avg ~8/scheme, max 56),
`Alert` ~7, `SchemeParams` in a handful. **Volumes are 0–100 here** (not Source's
0.0–1.0). `Dry` = route to the dry bus (skip reverb); `NoPause` = keep playing while
the game is paused (menus/loading).

**Semantics** [VtMB, decompiled — RE31 closed]: `Ambient` is a constant loop on the
manager's Ambient stem. `RandomSound` is scheduled by `CSoundSchemePlayingThink`
(`FUN_1022b300`, thunk `0x10003a71`), not by a seconds-between-plays mean.

Each think (rescheduled at `curtime + DAT_104491b4`): if the live-random count
(`manager+0x194`) is already `>= RandomSoundCount` (`+0x458`, clamp 0..6), skip;
if this (scheme, cursor) is already live, `cursor++`; else
`roll = IUniformRandomStream::RandomInt(1, soundscheme_randomness.GetInt())` and
**fire when `Frequency > roll`**. The cvar (`FUN_1022b290`, object `0x10750f28`)
defaults to **1000** (same default string as `player_throwforce`). Frequency 10
therefore fires on roll ∈ `[1,9]` (9/1000 per visit). **Frequency 0 never fires**
(`0 > RandomInt(1, max)` is false); an omitted key still defaults to 10 at parse.
Frequency `> 1000` always fires when under the cap. Cursor is round-robin at
`+0x5a8`.

Polar placement (`FUN_102298e0`) uses `DAT_1070b244` (`RandomFloat` / `RandomInt`).
**XY is around the player** (`UTIL_PlayerByIndex(1)` origin + `AngleVectors(0, angle, 0) * dist`).
**Z is around the `ambient_soundscheme` origin** (`[z − HeightMin, z + HeightMax]`).
`AngleMin`/`AngleMax` are a linear `RandomFloat` with **no wrap helper** — an
authored wrap arc `330`..`30` interpolates the long way. Emit ORs flags `0x280`.

A scheme with no music (`cops_outside.txt`) is pure `RandomSound` police barks; a
scheme is `{optional music triad} + {optional ambient loop} + {N random one-shots} + {DSP}`.

### Scheme FadeIn / FadeOut / start_enabled / Kill

One global `CSoundSchemeManager` at `DAT_10750d78` (ctor `FUN_10228450`) holds **one**
Ambient/Music/Combat/Alert stem set (slots `+0x14`, `+0x6c`, `+0xc4`, `+0x11c`,
stride `0x58`). Two full schemes do not overlap.

`InputFadeIn` `0x1022b480` / `InputFadeOut` `0x1022b4d0` read
`inputdata.variant.fieldType == 1` (float) then `flVal`; otherwise **0.0**. Both
bodies then floor the duration: `FUN_1022b590` at `0x1022b594-0x1022b5a4` and
`FUN_1022b520` likewise compare against `0x104454d0` (`0.5f`) with `FCOMP; TEST AH,5;
JP` and store `0.5` when the value is **below 0.5** -- so `0`, a negative, and any
`(0, 0.5)` all fade for **0.5 s**; `0.5` and above are that many seconds; a NaN
passes unchanged. **There is no instant fade** (an earlier revision of this page said
`0 is instant`; the listing says otherwise). There is **no `Disable` / `Enable` input**.

`FUN_1022b590` (FadeIn), in order: the floor above; if `this+0x455` (playing flag)
is already 1, **return**; `UTIL_GetLocalPlayer` (`0x101cda50`) and, with a player,
`FUN_10175360(player, this)` stores this scheme's EHANDLE at `player+0x1e04` (the
RoomDSP fallback; an L0 -> L3 edge); `FUN_10229270(&DAT_10750d78, duration, +0x460,
+0x4f0, +0x4a8, +0x538)` (below); `ThinkSet(0x10003a71)` (PlayingThink `FUN_1022b300`);
`+0x455 = 1`; `+0x17c = curtime + 0.1f` (`0x104491b4`, a float store at `0x1022b60f`).
FadeOut (`FUN_1022b520`): the same floor; if `+0x455 == 0`, **return**; else retire all
four live slots (`FUN_102293f0`, slot order `+0x14`, `+0x6c`, `+0xc4`, `+0x11c`),
`ThinkSet(0)`, `+0x455 = 0`.

**The manager switch `FUN_10229270` `0x10229270`** [walked and confirmed, L0-r009]:
(1) the prune loop over the EHANDLE list (`+0x19c` base, `+0x1a8` count): a handle of
`0xffffffff`, a serial mismatch against `PTR_DAT_10566458[h & 0x1fff].serial`
(`+0x8`) `!= h >> 13`, or a NULL entity pointer (`+0x4`) is removed (memmove, count
`-1`, index held); every other listed scheme gets `FUN_1022b660` (`ThinkSet(0)`,
`+0x455 = 0`) -- the one just fading in included, when it is listed. (2) Four
`FUN_10229430(this, name, volume, flags, duration, slot)` calls in **scheme-record
order**: `+0x14` from `+0x460` (Ambient), `+0xc4` from `+0x4a8` (Combat), `+0x6c`
from `+0x4f0` (Music), `+0x11c` from `+0x538` (Alert).

`FUN_10229430` `0x10229430`: `volume == 0.0` (double `0x1044fab0`, equal only) or an
empty name -> `FUN_10229550` (retire) and return; else, with `0.0f < volume`
(`0x104454c4`), `Q_strnicmp(name, slot, 0x40) == 0` retargets in place (`+0x48 =
+0x40 = volume`, `+0x54 = flags`, `+0x4c = volume / duration`; channel, name and
`+0x44` kept, nothing retired), a different name retires the old record first, then
`Q_strncpy(slot, name, 0x40)`, `+0x48`, `+0x40`, `+0x4c = volume / duration`, `+0x50 =
FUN_102297a0(this)`, `+0x54 = flags`. A negative or NaN volume writes nothing. `+0x44`
is never written here: the new record ramps from 0.

Track record (`0x58`): `+0x00` name `char[64]`, `+0x40` target, `+0x44` ramped level
(the emitted volume; written only by `FUN_10229090`), `+0x48` authored volume (the
ramp's ceiling and what the state gate restores), `+0x4c` rate (units/s), `+0x50`
channel, `+0x54` flags (`0x800` `Dry`, `0x2000` `NoPause`, from the parser).

`FUN_10229550` `0x10229550` (retire): an empty slot (`slot[0] == 0`) returns; else
`+0x40 = 0`, `+0x4c = +0x48 / duration` (the **authored** volume over the **incoming**
duration, so a record at level `L` fades out in `duration * L / authored` seconds),
append the whole record to the retiring list (`+0x174` base, `+0x178` capacity: 1,
then doubling, `Plat_Alloc` / `Plat_Realloc`; `+0x180` count; `+0x184` base copy),
then zero the slot's 22 dwords.

`FUN_102297a0` `0x102297a0` (channel): channels `1..6` in order, the first held by no
retiring record (`+0x50`) and no live slot (`+0x64`, `+0xbc`, `+0x114`, `+0x16c`). All
six held: the **quietest** retiring record -- smallest `+0x44` strictly below `1.1`
(`0x1048dd2c`), first on ties, index 0 when none qualifies -- is stopped through
`IEngineSoundServer003` slot 7 `(0, channel, record)`, removed, and its channel
returned.

`CSoundSchemeManager::FrameUpdatePostEntityThink` `0x10228d00` (every frame): `dt =
curtime - [+0x0c]`; `state = FUN_10228a80()` (1 explore, 2 combat, 3 alert; the
conditions are not walked); `FUN_10228e80(this, state)` sets the Music (`+0xac`),
Combat (`+0x104`) and Alert (`+0x15c`) **targets** -- the state's own to its `+0x48`,
the other two to 0; Ambient is never gated -- records the state at `+0x10` and calls
engine vfn `+0x1ec` (`1.0`; `0.92` for state 2) and, on a change, `FUN_100cd660` on
`FUN_1023dcd0()`'s members (not walked). Then `FUN_10229090(record, dt)` on the four
live slots and on each retiring record: only when `+0x44 != +0x40`, move `+0x44`
toward `+0x40` by `+0x4c * dt` (`FUN_102283a0`), clamp to `[0, +0x48]`; if `+0x44 <=
0.01` and `+0x40 <= 0.01` (`0x10450aa4`) stop the sound (slot 7), else emit through
slot 2 on a reliable single-user filter (channel `+0x50`, the record as the name,
volume `+0x44`, flags `+0x54 | 0x101`). A retiring record is removed when its `+0x44
<= 0.01` (double `0x1044e658`). With `sp_tutorial_city.txt` playing (Ambient ch1,
Combat ch2, Music ch3) a 2 s FadeIn of `sp_tutorial_underground.txt` retires City's
Ambient (rate 0.15) and takes ch4, retargets the shared Combat in place, retires
City's Music (rate 0.1) and leaves the Music slot empty: two retiring records, no
eviction, both gone about 1.9 s later.

Activate (`vfunc113` `0x1022a300`, slot 113; it does **not** chain
`CBaseEntity::Activate`) registers the EHANDLE (`FUN_102289e0` `0x102289e0`: grow
through `FUN_1022d380`, append, **no duplicate check**) then, if `start_enabled`,
FadeIns with a **hardcoded 2.0 s**; else `ThinkSet(0)` (`m_pfnThink = 0` only;
`m_flNextThink` and `+0x455` untouched). `start_enabled 0` waits for `InputFadeIn`.
OnSave (`vfunc129` `0x1022a350`) copies the playing flag into `start_enabled` so
restore Activate FadeIns again.

A typical map pair `A.FadeOut` + `B.FadeIn` on one trigger: FadeIn B first already
cleared A's flag, so A's FadeOut is a no-op; FadeOut A first fades the global stems
toward 0, then B starts. A delayed FadeOut of A **after** B's FadeIn does nothing.

**Kill does not stop the stems.** `CSoundScheme` does not override `UpdateOnRemove`;
the destructor only frees the RandomSound vector. RandomSound think dies with the
entity; beds/music keep going until another FadeIn replaces them or a still-active
scheme FadeOuts. An authored `ambient_soundscheme,Disable` wire has **no handler**.

## 6. Dynamic music and radio

**Music state machine** [VtMB strings + script]: `client.dll` cross-fades between
the active scheme's `Music` (exploration), `Combat`, and `Alert` MP3 stems
(`Combat`, `Alert`, `Crossfade`, `FadeIn`, `FadeOut`, `music/` all present).
Combat/safe state is gated in Python by `world.SetSafeArea(0/1)` [script] — outside
a safe area, combat scoring raises the `Combat` stem; the server signals the client
(`vampire.dll` exposes `CombatMusic`, `Radio`). Tracks are per-hub with pervasive
`_combat` twins (`dark_asia.mp3`/`dark_asia_combat.mp3`,
`chinatown/chinatown_theme.mp3`), plus a `licenced/` folder of the club tracks
(Lacuna Coil, Ministry, Genitorturers, …) and `stems/`. **Radio** streams the five
`sound/radio/radio_loop_[1-5].mp3` loops (the patch adds `.lip` sidecars for a
talking-radio prop).

### Listed UP issue: radio playback resets on load

The Unofficial Patch 11.5 README's **Open Gameplay Issues** says, at line 4690,
"Loading will remove Bach's holy light effect and reset the radio." The radio half has
a narrow static explanation in retail. The installed retail `sm_pawnshop_1.bsp`
(SHA-256 `5e4a518f4428cb2f86ac14988bde09510125daabbcb5f65366bc3b47efb3dcc3`)
authors entity 414, `prop_radio` named `Radio1`, with `spawnflags=1` (starts on),
`radius=800`, `volume=1.0`, and a GehtoBlaster model.

`CPropRadio::ObjectCaps` (`0x1022c660`) returns `8`, admitting the entity to the
ordinary save walk. Its slots 126/127 are inherited `CBaseEntity::Save`/`Restore`
(`0x100a9f70`/`0x100aa140`), and its datamap saves only `m_bActivated` (`+0x734`),
`m_UseIcon` (`+0x730`), `m_flVolume` (`+0x750`), and `m_flRadius` (`+0x754`).
Slot 130 is generic `CBaseAnimating::OnRestore` (`0x1008df10`), with no radio seek.
There is no playback timestamp or MP3 cursor in that record or in the five
`.HL1` save blocks (`savegame_format.md`); the client save has no sound-channel block.
`CPropRadio::Precache` (`0x1022bd60`) reloads `radio_data.txt` and selects the first
dependency-true show; `Activate` (`0x1022c4a0`) emits that show with no seek argument.
The on/off flag is persisted, but the position within the broadcast cannot be
recovered from a save. This is a concrete playback-state gap consistent with the
listed reset; it does not establish the exact audible restart timing.

This establishes the missing restore datum and the map that exercises it. A live
save/load playback capture is still needed to pin the exact audible restart point
and the engine/client channel creation order.

## 7. Point sounds — `ambient_generic`

**1,631 instances across 95 maps** [data]. Stock Source `ambient_generic` with VtMB
additions:

```
"message"      "Environmental/Machines/floresent light.wav"  // the wav (direct path)
"health"       "10"     // VOLUME on a 0–10 scale (Source convention), not hit points
"radius"       "1250"   // falloff distance (units)
"pitch"        "100"    // + pitchstart
"spawnflags"   "16"     // bits below; default (0) is a looping autoplay bed
"targetname"   "Floresent"
// VtMB-specific keys (not in stock ambient_generic):
"SourceEntityName" "tram_mover"   // parents the sound to a moving entity (154 use it)
"sound_event"      "0"            // + sound_event_level "2"
"flag_no_voice_duck" "1"         // exempt from dialogue ducking (§3)
"flag_force_looping" "1"  "flag_no_sfx" "1"  "flag_skip_collide" "1"
"StartHidden"      "1"            // ScriptHide at spawn — visual/collision only, NOT an audio mute
```

Spawnflags (`CAmbientGeneric::vfunc103` `0x101ac310`):

| Bit | Value | Meaning |
|---|---:|---|
| `0x01` | 1 | Everywhere (non-spatial) |
| `0x10` | 16 | Start Silent — do not set `m_fActive` at Precache |
| `0x20` | 32 | Not Looped — `m_fLooping = 0` unless `flag_force_looping` |

`m_fLooping` (`+0x4bd`) is 0 only when `flag_force_looping == 0` **and** bit `0x20` is set;
otherwise 1. Precache (`0x101ac930`) sets `m_fActive = 1` only when **not** Start Silent
**and** `m_fLooping`. Activate emits only if `m_fActive`.

Fired by presence (looping beds), by I/O, or from Python via the datamap-bound
`Entity.PlaySound()` / `StopSound()` (phone rings, sirens, buzzers) [script]. The
stock LFO/spin envelope keys (`preset`/`spinup`/`volstart`/`lfotype`/`lforate`/…)
are parsed in KeyValue override `vfunc110` `0x101ada80` into `m_dpv` (`+0x458`).
**`fadein` / `fadeout` are those envelope keys**, not seconds-I/O: `atof(x)` → `100.0 / x * 0.2`
on the FPU stack (`FDIVR [0x10457188]`, `FMUL [0x10449198]`, i.e. `20 / x`) → `__ftol` → `<< 8`
into `m_dpv+0x1c/+0x20`, mirrored into `+0x50/+0x54` (`fadein 10` → 512, `0.75` → 6656, `30` → 0;
`0` divides to +Inf and `__ftol` stores the integer indefinite, low dword 0). The word is the
per-think (0.2 s) 8.8 volume step, so a health-4 bed with `fadein 10` reaches its ceiling of 40 in
4 s. There are **no** `fadeinsecs` / `fadeoutsecs` strings
in `vampire.dll`; a 108-map scan of current `.ents` finds **zero** non-zero
`fadeinsecs` keys. 78 `ambient_generic` rows do author a non-zero `fadein` LFO value
(including `sm_hub_1`'s `rain_sounds` `fadein=10` / `fadeout=10`).

There are **no** `InputFadeIn` / `InputFadeOut` methods on this class. The only
`InputFadeIn` string in the image is `CSoundScheme`. Authored
`ambient_generic,FadeIn` wires are `AcceptInput` refusals. `entity_io.md`'s
FadeIn(2)/FadeOut(2) counts are those dead rows.

### `radius` → `m_iSoundLevel` [VtMB, recovered 2026-10-10, `docs/specs/layers/L0-entity/walks/L0-r003.md`]

`CAmbientGeneric::Spawn` (`0x101ac310`) calls `FUN_101ac570` at `0x101ac321` with `m_spawnflags & 1`
(`0x101ac2f0`, pushed first) and `m_radius` (`+0x450`, the `radius` key, float), and stores the answer
in `m_iSoundLevel` (`+0x454`, int) at `0x101ac326`. `CSpeaker::Spawn` (`0x101af420`) does the same at
`0x101af447` with `(m_spawnflags >> 1) & 1` and its `m_radius` at `+0x454`, storing at `+0x458`.
`FUN_101ac570` (65 B, pure): `radius > 0.0` strictly (`FCOMP [0x104454c4]`, C3|C0 masked: `<= 0`,
`-0.0` and NaN take the zero arm), then any nonzero `everywhere` byte → 0, else
`trunc(40.0f + 20.0 * log10(radius * 0x3F9C71C71C71C71C))` through `__ftol` (`0x10431320`,
truncate toward zero; `+Inf` stores the integer indefinite, low dword 0). Constants: `0x104454c4`
0.0f, `0x1047aa18` the double nearest 1/36 (`0.027777777777777776`), `0x104704a8` 20.0, `0x10462950`
40.0f. Levels are negative for `0 < radius < 0.36` (0.1 → −11) and are stored as such. `0x10228350`
(footsteps, `footsteps.md` §3.1) is the same chain without the everywhere test. Readers of
`m_iSoundLevel`: `InputPitch 0x101ac690`, `InputVolume 0x101ac7d0`, `vfunc113 0x101ac9c0` (not walked).
UNRECOVERED: the x87 precision control in force (decides 36 → 40 or 39, 360 → 60 or 59) and the value
of `m_radius` when the key is absent (no constructor or datamap default found). Port:
`Source/ElysiumUE/Private/Audio/ElysiumSoundLevel.cpp` `FromAmbientRadius`,
`Substrate/ElysiumAmbientGeneric.cpp` `Spawn`; record `Arena/scenarios/audio/l0_ambient_radius_level.json`.

### Initialization: `vfunc110`, `Spawn`, `Precache`, the dpv pass [VtMB, recovered 2026-10-10, `docs/specs/layers/L0-entity/walks/L0-r005.md`]

Port: `Source/ElysiumUE/Private/Substrate/ElysiumAmbientGeneric.cpp`; record `Arena/scenarios/audio/l0_ambient_init.json`.

- **KeyValue `vfunc110` `0x101ada80`** (slot 110, called per key by `CBaseEntity::ParseMapData`
  `0x1009e280` before Spawn; `__strcmpi` whole-key match, `atoi` unless noted, signed clamps) writes
  `m_dpv` `+0x458` (25 dwords, one unnamed 100-byte SAVE row): `preset` +0x00 unclamped; `pitch` +0x04
  and `pitchstart` +0x08 clamped [0, 255]; `spinup` +0x0c / `spindown` +0x10 clamped [0, 100], then
  `>0 → (101 − v)·64`, mirrored to +0x40 / +0x44 (even when 0); `volstart` +0x18 clamped [0, 10] then ×10;
  `fadein` +0x1c / `fadeout` +0x20 as above; `lfotype` +0x24 raw, `>4 → 2`, no lower clamp; `lforate`
  +0x28 clamped [0, 1000] then `<< 8`; `lfomodpitch` +0x2c, `lfomodvol` +0x30, `cspinup` +0x34 clamped
  [0, 100]. Any other key goes to the base `CBaseEntity::KeyValue` `0x1009e430` (the datamap chain:
  `message`, `radius`, `health`, `spawnflags`, `SourceEntityName`, `sound_event*`, `flag_*`). Every
  handled key returns 1.
- **`Spawn` `0x101ac310`**: `m_iSoundLevel` (§ above); `m_iszSound` null or `strlen < 1` → `Warning("EMPTY
  AMBIENT AT: %f, %f, %f\n")` (`0x10597804`) and `UTIL_Remove` (`0x101cd940`; `DispatchSpawn` returns −1)
  at `0x101ac4a0`; else `SetSolid(0)`, `SetMoveType(0, 0)` (no-ops on the fresh words), `m_pfnThink` ←
  `0x1000969c` (the think `FUN_101acb70`), `m_flNextThink` ← 0 (nothing scheduled: Activate `0x101ac9c0`
  arms it, only when `m_fActive`), `m_pfnUse` ← `0x100131e2` (`FUN_101ad470`), `m_fActive` ← 0,
  `m_fLooping` ← 0 iff `m_bForceLooping == 0 ∧ spawnflags & 0x20` else 1, `m_nSndFlags` |= 0x800
  (`flag_no_sfx`) | 0x1000 (`flag_no_voice_duck`) | 0x200 (`flag_skip_collide`) | 0x100
  (`flag_force_looping`), the source handle `+0x4c8` ← −1, then `JMP [slot 104]` (`0x101ac49a`).
- **`Precache` `0x101ac930`**: `CEngineSoundServer::PrecacheSound(name, 0)` (engine.dll `0x200018e0`)
  only when `strlen > 1` and `name[0] != '!'`; then the dpv pass; then `m_fActive` ← 1 iff
  `(spawnflags & 0x10) == 0 ∧ m_fLooping`.
- **The dpv pass `FUN_101ad0f0`** (also re-run by the toggle Use at `0x101ad70a`): A. `+0x46c` ←
  `clamp(m_iHealth·10, 0, 100)` (store, then >100, then <0). B. `preset ≠ 0 ∧ ≤ 27` (signed; a negative
  preset reads below the table, unrecovered): copy the 25-word row at `0x10595eb4 + p·0x64` (rows 1..27
  at `0x10595f18..0x10596940`; the full table is in the walk, Q3) over `+0x458..+0x4bb`, then spindown,
  spinup `>0 → (101 − v)·64`; volstart, vol-ceiling ×10; fadein, fadeout `>0 → (101 − v)·64`; lforate
  `<< 8`; mirrors +0x50/+0x54/+0x40/+0x44 ← the transformed words. C. every path: fadeout running ← 0,
  fadein running ← initial; current volume `+0x4c` ← fadein-initial ? volstart : ceiling; spindown ← 0,
  spinup ← initial; current pitch `+0x3c` ← spinup-initial ? pitchstart : pitch, 0 → 100; `+0x58` ← vol
  `<< 8`; `+0x5c` ← 0; `+0x48` ← pitch `<< 8`; lforate ← |lforate|; `+0x38` ← 1; `cspinup ≠ 0` → pitch
  ceiling ← pitchstart + (255 − pitchstart) / cspinup (IDIV), capped 255; (spinup-initial ∨
  spindown-initial ∨ (lfotype ∧ lfomodpitch)) ∧ pitch == 100 → 101.
- No shipped map authors a non-zero `preset`, `cspinup`, `lfotype`, `lforate` or `lfomod*`; one authors
  `spinup 10` / `spindown 10`; `pitch 100` is on 1,522 and `pitchstart 100` on 1,623 of the 1,631
  ambients; all 1,631 author `health`. The constructor `FUN_101ac230` never writes `m_iHealth` or `m_dpv`,
  so a keyless default assumes a zero-filled allocation (unrecovered).

### Activate, the modulation think, Use, Pitch / Volume, the AI sound owner [VtMB, recovered 2026-10-10, `docs/specs/layers/L0-entity/walks/L0-r006.md`]

Port: `Source/ElysiumUE/Private/Substrate/ElysiumAmbientGeneric.cpp`; records
`Arena/scenarios/audio/l0_ambient_source_resume.json`, `l0_ambient_pitch.json`, `l0_ai_sound_owner.json`
(and `l0_ambient_init.json` for the activation pass on a runtime spawn).

- **`vfunc113` `0x101ac9c0` is slot 113, `CBaseEntity::Activate`'s override**, run once per level
  activation by `ServerActivate` (`CServerGameDLL::vfunc4` `0x1011aaf0`) — not a per-frame think (an
  earlier reading). H0: the source handle `+0x4c8` is valid iff `≠ −1`, the handle table's serial matches
  (`DAT_10566458`, 12-byte records, `h = index | serial << 13`) and the record holds an entity. H1:
  invalid with `m_sSourceEntName` → `FindEntityByName` `0x100f7770` → the match's `GetRefEHandle`, else
  −1. H2: still invalid → the ambient's own handle (self), unconditionally. H3: `m_fActive` → `Emit(edict,
  source GetAbsOrigin, name, +0x4a4·0.01, +0x454, m_nSndFlags | 0x8, +0x494)` and `m_flNextThink =
  curtime + 0.1`. H4: inactive → nothing emitted, no think. Nothing else re-resolves `+0x4c8`:
  `InputPitch`, `InputVolume`, Use and the think read the cached handle, so a killed source silences them
  until the next Activate pass (a save restore runs the pass again). Neither `+0x4c8` nor `+0x4d8` has a
  datamap record (not saved, not keyed).
- **The think `FUN_101acb70`** (`m_pfnThink` via `0x1000969c`; reschedules `curtime + 0.2`
  (`0x10449198`)). Locals: `vol = +0x4a4`, `pitch = +0x494`, `flags = m_nSndFlags`, `changed = 0`.
  T0: spinup, spindown, fadein, fadeout, lfotype (the running words) all 0 → return, no reschedule.
  T1 (spinup or spindown ≠ 0): `+0x4a0 += spinup` (≥ 1) else `−= spindown` (> 0); `pitch = +0x4a0 >> 8`;
  `> +0x45c` → spinup 0, pitch = ceiling; `< +0x460` → spindown 0, STOP emit `(…, 0.0, 0, 0x4, 0)` on a
  valid handle, return (no reschedule); clamp `[1, 255]`; `changed |= old ≠ pitch`; `+0x494 = pitch`;
  `flags |= 2`. T2 (fadein or fadeout ≠ 0): the same over `+0x4b0` / `+0x46c` / `+0x470`, clamp `[1,
  100]`, `+0x4a4 = vol`, `flags |= 1`. The accumulator is not clamped: the think that crosses the
  ceiling leaves it one step past, so the first down-think reads a change. T3 (`lfotype ≠ 0`): phase
  `+0x4b4 += +0x480` (running signed rate; `> 0x6fffffff` zeroed first); `pos = phase >> 8`; `phase < 0`
  → pos 0, phase 0, rate `|rate|`; `pos > 255` → pos 255, phase `0xff00`, rate `−|rate|` (a triangle);
  output `+0x4b8`: type 1 square (`pos < 0x80 ? 255 : 0`), type 3 `RandomInt(0, 255)` only at
  `pos == 255`, else `pos`; `lfomodpitch` → the EMITTED pitch `+= (out − 0x80)·modpitch / 100`, clamp
  `[1, 255]`, not stored; `lfomodvol` likewise on the emitted volume, clamp `[0, 100]`. T4: `flags ≠ 0 ∧
  changed` and a valid handle → emit `(…, vol·0.01, +0x454, m_nSndFlags | flags, pitch)` with pitch
  100 → 101. T5: `m_flNextThink = curtime + 0.2`.
- **Use `FUN_101ad470`** (`m_pfnUse` via `0x100131e2`): `(activator, caller, int useType, float value)`,
  `useType` 0 OFF / 1 ON / 2 SET / 3 TOGGLE. U0: not TOGGLE → ON while active returns, OFF while
  inactive returns. U1 SET while active: `v > 1.0 → 1.0f; v < 0.0 → 0.01f; +0x494 = ftol(v·255)`; emit
  `(…, 0.0, 0, m_nSndFlags | 2, pitch)`; return (SET has no caller among the inputs). U2 OFF/TOGGLE while
  active: `cspinup ≠ 0` → one more step (`+0x490 + 1`, spinup ← initial, ceiling `+0x45c = (255 −
  floor) / cspinup · step + floor`, cap 255, `m_flNextThink = curtime + 0.1`, return); else `m_fActive =
  0`, **`m_spawnflags |= 0x10`** (the start-silent bit, set at runtime), and with no spindown-initial
  and no fadeout-initial → STOP emit, return; else spindown ← initial, spinup ← 0, fadeout ← initial,
  fadein ← 0, tail. U3 start: `m_fLooping == 0` → STOP emit (a one-shot is never active: every start is
  a STOP then a START); else `m_fActive = 1`; `FUN_101ad0f0`; valid handle → START emit `(…, +0x4a4·0.01,
  +0x454, m_nSndFlags, +0x494)`, then U4. U4: the AI sound (`docs/vtmb/npc-ai/senses.md`, corrected:
  the insert's origin is the SOURCE entity's). Tail: `m_flNextThink = curtime + 0.1`.
- **`InputPitch` `0x101ac690`** (datamap INPUT `Pitch`, FIELD_FLOAT): `v = float variant or 0.0`;
  `> 255 → 255` else `< 0 → 0` (NaN kept); `__ftol` → `+0x494`, with no `m_fActive` gate; cached handle
  valid → emit `(…, +0x4a4·0.01, +0x454, m_nSndFlags | 2, pitch)`. **`InputVolume` `0x101ac7d0`**: clamp
  `[0, 10.0f]`, `× 10.0f`, `__ftol` → `+0x4a4` (no `[1, 100]` clamp, no dpv pass); valid handle → emit with
  `| 1`. The datamap has exactly five INPUT records (`PlaySound`, `StopSound`, `ToggleSound`, `Pitch`,
  `Volume`): `FadeIn` / `FadeOut` wires are `AcceptInput` refusals (entity_io.md).
- **`FUN_101ad9a0`**, the owner cache: `+0x4d8` invalid (−1, serial mismatch, empty) → `FindEntityByName(…,
  name or "", activator = this, caller = NULL)`, `+0x4d8 ← handle or −1`; a valid cache is kept while it
  lives (no re-lookup). Returns the entity or NULL.
- **Emit `FUN_101cdac0`** (cdecl): `!`-prefixed name → sentence index (`VEngineServer014` vslot 53) →
  `"!%d"` or nothing; else `VEngineServer014` vslot 49 with `flags | 0x80`. Flag words passed: `0x8`
  Activate, `0x2` pitch / SET, `0x1` volume, `0x4` STOP (raw), `0x0` start (SDK `SND_*` names; the engine
  side is UNRECOVERED). Port: the audio subsystem's voice pool — start/spawning submit a fresh voice at the
  source entity's origin, attached to its body; change-volume / change-pitch re-level the live voice
  (a change flag with no live voice is dropped, as `S_StartSound` drops it); STOP stops it. The `!`
  sentence table is absent in the port (named gap).

### `PlaySound` and `StopSound` are edge-only, and the wired parameter is inert

`InputPlaySound` `0x101ad3e0` (mode **1**), `InputStopSound` `0x101ad410` (mode **0**),
and `InputToggleSound` `0x101ad440` (mode **3**) share dispatcher `FUN_101ad470`.
Mode 1 on an already-`m_fActive` entity **returns**; mode 0 on an inactive entity
**returns**. Mode 3 **bypasses** that early-out and is a real toggle. A map that
wants a sound retriggered has to Stop it first (or Toggle).

The parameter carried on the wire is **never read**. The dispatcher takes the variant
values as arguments and no path in its body references them; the sound played is always
whatever `message` (`m_iszSound`, `+0x4c0`) already names, resolved at play time, with
a leading `!` treated as a sentence lookup. `sp_tutorial_1` authors `PlaySound` with
the parameter `"3"` on one wire (`logic_shot_7 → sound_maul_wolves`, delay −1) and
empty on every other — they behave identically. Mode 3 is ToggleSound's literal, not
that wire's parameter.

`radius`, `pitch` and `health`/volume are not consulted per Play/Stop either: the
dispatcher branches on `m_fLooping`, and only the looping branch marks the entity
active and runs the ramp and spin bookkeeping out of `m_dpv`. Those keyfields are
resolved once at spawn into `m_dpv`, not re-read on each play. `InputVolume`
`0x101ac7d0` writes `m_dpv+0x4c` and, if a source handle is live, updates the voice
(`FUN_101cdac0` with flags `m_nSndFlags | 1`). It does not start or stop.

Emit flags are `FUN_101ac670` = `m_nSndFlags | param`. Play passes param 0, so the
flags are only the VtMB key bits: `flag_force_looping` → `0x100`, `flag_skip_collide`
→ `0x200`, `flag_no_sfx` → `0x800`, `flag_no_voice_duck` → `0x1000`.

### Entity looping is not mixer wrapping

`m_fLooping` keeps the entity **active** (PlaySound no-op, StopSound required to
`SND_STOP`). The mixer wraps the WAV only when:

- the file has a `smpl` sampler loop or a `cue ` chunk (`CAudioSourceWave` `+0x28`,
  parsed in `FUN_2013a030`), or
- emit flags include **`0x100`** (`flag_force_looping`), which forces
  `CAudioSourceWave::FUN_2013a120(true)` → loop start **0** if `HasLoop()` was false.

`S_StartStaticSound` `0x2011f620` is the wrap site. There is no `SND_LOOPING` string
in the image. A start-silent gunshot with spawnflags 16 and **no** `smpl`/`cue ` and
**no** `flag_force_looping` therefore **plays once** even though the entity stays
"looping". Tutorial samples: `JackChopWindow.wav`, `gun shot 2.wav`,
`Jack V Sabbat SFX.wav`, `Riled_1.wav` have neither chunk; `Fire Loop.wav`,
`floresent light.wav`, `rain_light_loop.wav` have `smpl`; `City Ambience.wav` and
`Crowd screams.wav` have `cue `. Wrapping every non-0x20 entity in a port is a
divergence, not retail.

### Hide, unhide, Kill

**Kill stops the voice.** `UpdateOnRemove` `vfunc180` `0x101ac5e0` emits
`FUN_101cdac0(..., flags 4)` (`SND_STOP`) if the source handle at `+0x4c8` is live.

**ScriptHide does not.** `CBaseEntity::ScriptHide` `0x100a8710` saves think, clears
solid, sets nodraw — no `SND_STOP`. A looping static channel keeps mixing.
ScriptUnhide restores think/solid and does **not** call PlaySound: a still-running
loop continues; a finished one-shot does not restart.

**StartHidden is not Start Silent.** A looping, non-silent, `StartHidden` entity
still sets `m_fActive` at Precache and still emits. Start Silent (`0x10`) is the
audio mute.

### Current exported seam [data]

`research/tooling/probes/audio_surface_survey.py` reads every current `.ents` plus the mirrored Python
without writing game data. The present 23-map snapshot contains 14,663 entities and:

| Entity | Instances |
|---|---:|
| `ambient_generic` | 416 |
| `ambient_soundscheme` | 45 |
| `trigger_environmental_audio` | 16 |

Every exported map has at least one of those. The densest are `sp_tutorial_1` (98),
`la_hub_1` (88), `sm_hub_1` (53), `sm_warehouse_1` (52), and `sm_medical_1` (50).
The `ambient_generic` messages are 410 WAV and 6 MP3. Their authored I/O wires are
173 `PlaySound`, 76 `Kill`, 44 `StopSound`, and 15 `Volume`; SoundSchemes receive 80
`FadeIn` and 69 `FadeOut` wires. Scheme switching is dominated by
`trigger_multiple` enter/exit pairs, with relays, switches and Python checks also
driving it.

Fifteen audio-control wires currently resolve no target in their exported map. They
include patch-added names, `AmbientCrickets` in `sm_warehouse_1`, and `scheme_guns`
in `sp_tutorial_1`. These are content-validation findings: a runtime must diagnose
them rather than silently accept them, but absence in a partial export does not by
itself prove the retail target never exists.

The playable-path trio (`sp_tutorial_1`, `sm_pawnshop_1`, `sm_hub_1`) is joined in
`docs/vtmb/three-map-audio-surface.md`: every `ambient_generic` / scheme / env-audio
row, every Play/Stop/Fade wire, mover `soundgroup`s, and which WAVs actually wrap.

Re-run 2026-09-08 over the full 108-map export (71,096 entities): 50 unresolved
audio-control wires, one of them on `sp_tutorial_1` (`scheme_guns.FadeOut`). The fifteen
above were the 23-map snapshot. The V2 sound corpus (`Content/ElysiumCorpus/sound`,
10,892 files, lower-cased keys) resolves every audio path the tutorial references except
five that no install ships (`three-map-audio-surface.md` §2.5).

2026-09-08 (AUD0.3/0.4): the six scheme `.txt` the tutorial references are no longer legacy-only.
The `sound-scheme` unit publishes its source capsule (schema 1.1.0) and `uv run elysium import
sound-schemes` deploys all 174 schemes to `Content/ElysiumCorpus/sound/schemes/<stem>.txt`,
lower-cased, byte-identical to the UP-first source (152 loose from `Unofficial_Patch`, 22 from the
VPKs). The `export bundle audio` lane and `UE_extract_sounds.py` are deleted with their
`audio/catalog.json`, `audio/schemes.json`, `audio/entity_events.json`, `audio/maps/*.json`,
`sound/usable/soundgroups.json` and `sound/Schemes/*.txt` products.

## 7b. Mover sounds — the `soundgroup` convention

Doors (`func_door`, `func_door_rotating`) and buttons (`func_button`) carry a
**`soundgroup`** keyvalue — a token like `standard_door`, `small_metal_switch`,
`metal_file_cabinet`. **There is no soundgroup *data file*** [VtMB, RE-verified]: an
exhaustive scan of all ~67k install files finds these tokens *only* inside `.bsp`
entity lumps, and the soundscript system that would name them (`game_sounds*.txt`) is
commented out (§9). The token resolves **by directory convention** — the WAVs live
under `sound/usable/<category>/<token>/<subkey>.wav`:

| Category (dir) | Class | Subkeys (files) |
|---|---|---|
| `openable` | doors | `open`, `close`, `swing`, `locked` |
| `switches` | buttons | `on`, `off` |
| `computers` | keypads/terminals (`docs/vtmb/computer-terminals.md`) | `access`, `accept`, `error`, `typing` |

Retail ships **22 openable + 8 switch + 4 computer** groups; a group may omit a subkey
(`standard_door` has no `swing` in retail — the patch adds one). The same token can exist
in two categories (`manhole_cover` is both an openable and a switch); the **class** picks
the subtree.

**Decompile provenance** (`vampire.dll`, base `0x10000000`) [VtMB]:

| Address | What |
|---|---|
| `0x100ef060` | `CBaseDoor::Spawn` sound resolve — strcmpi's the group's child keys for `close`/`open`/`swing`/`locked`, caching each sound index (`DAT_106eb3dc`/`e0`/`e8`/`e4`) |
| `0x100c8810` | `CBaseButton::Spawn` sound resolve — caches `on`/`off` (`DAT_106e6f78`/`f7c`) |
| `0x100ee4e0` | door-sound player — gates on the SILENT spawnflag (`m_spawnflags & 0x1000`) and routes `swing` to the movement channel (4), others to channel 3 |
| `0x100efc90` | `CBaseDoor::Use` — plays the `locked` index on the locked `+use` path |
| `0x101f55a0` | the soundgroup→emitter name resolver (`this+0x20[lang]`; also handles NPC voice-set `soundgroup`s like `Young_Thug`, and the `Female_PC_Override`) |

Buttons also carry explicit **`locked_sound`/`unlocked_sound`** — direct WAV paths (e.g.
`environmental/electronic/button_beep.wav`), the press-success/press-denied feedback,
independent of the soundgroup. `soundgroup` is a **base-`CBaseEntity`** field
(`m_iszVSoundGroup` @`0x0c0`), so NPCs reuse it for a voice actor's line set — those tokens
(`Bertram`, `Young_Thug`, …) resolve through `FUN_101f55a0`'s voice path, not `usable/`.

### How the convention is actually built — the `SndSchemeTables` registry [VtMB, recovered 2026-09-08]

The directory convention above is not hardcoded anywhere as a string: `usable/` never appears in
any shipped binary. It is assembled from two pieces at table-construction time.

`FUN_101f66c0` @`0x101f66c0` registers the five sound-scheme tables, each with its vdata file name
and its category token(s):

| Table (→ `vdata/system/<name>.txt`) | Category token(s) |
|---|---|
| `SndScheme_Char` | `Female`, `Male`, `Monster`, `Animal` |
| `SndScheme_Wpn` | `Melee`, `Ranged`, `Ejection` |
| `SndScheme_Openable` | `Openable` |
| `SndScheme_Switch` | `Switches` |
| `SndScheme_Computer` | `Computers` |

`FUN_101f5210` @`0x101f5210` opens `"%s%s.txt"` (vdata path + table name) and `FUN_101f5390`
@`0x101f5390` parses it: `SoundSchemeTables/SoundScheme` yields **`Name`** (stored at `table+0x0c`)
and `InternalName` (`table+0x08`), and every `SoundList/Sound/Name` becomes one ordinal slot — that
list *is* the subkey vocabulary, in file order.

`FUN_101f41b0` @`0x101f41b0` then builds the directory with `sprintf("%s\%s", table->Name,
categoryToken)`. All three usable vocabulary files author `"Name" "Usable"`, and that is the **only**
source of the `usable/` path component. So:

    sndscheme_openable.txt  "Name" "Usable" + token "Openable"   -> sound\Usable\Openable
    sndscheme_switch.txt    "Name" "Usable" + token "Switches"   -> sound\Usable\Switches
    sndscheme_computer.txt  "Name" "Usable" + token "Computers"  -> sound\Usable\Computers

Note the registered token is `Computers` (plural) while that file's `InternalName` is `Computer`
(singular) — the two are independent strings and only the registered token reaches the path. Note
also that the token is `Openable`, never `doors`: the `sound/usable/doors/` tree the install also
ships (six Unofficial Patch loose WAVs in 4 groups) is **unreachable from any soundgroup table**.
It is not dead content: four of the six are referenced by direct path from `sm_oceanhouse_2` and
`sm_asylum_1` entity keys [data, corpus index `referencedBy`], so they resolve as ordinary sound
paths and must stay in the corpus.

**The groups are the shipped directories, not a list.** `FUN_101f3810` @`0x101f3810` walks
`sound\<dir>\*.*`: every *file* becomes a sound slot (matched to the vocabulary by stem) and every
*subdirectory* recurses as a child table (skipping `.` and `..`, compared at `DAT_105a040c` /
`DAT_105a0410`). Group names are therefore the on-disk directory names **verbatim**, including the
two that carry a space — `squeaky_metal door`, `squeaky_wood door`. `FUN_101f3690` @`0x101f3690`
prepares the base directory and strips a leading `sound\` off it (`_strstr`) plus a trailing `*`.

**Lookup is verbatim and case-insensitive.** `FUN_101f39d0` @`0x101f39d0` copies the token,
`Q_FixSlashes(…, '\')`, and compares each path component with `__strcmpi`, recursing into child
tables. Retail tries **no** space/underscore variance and no other spelling — an authored
`soundgroup` either names a shipped directory or it does not.

**A miss is not silence.** `FUN_101f42a0` @`0x101f42a0` returns the *category root's* own base index
when `FUN_101f39d0` answers `-1`, so an unknown token plays the default sounds sitting directly
under `usable/<category>/` — which is exactly why the install ships loose
`usable/openable/{open,close,locked,swing}.wav`, `usable/switches/{on,off}.wav` and
`usable/computers/{accept,access,error,typing}.wav` beside the group directories. A *subkey* a group
ships no file for is a different case: the directory walk simply never fills that slot, and the cue
is silent (`switches/elevator_button` ships `on.wav` and no `off.wav`).

Port: `Source/ElysiumUE/Private/Substrate/ElysiumMoverSounds.{h,cpp}` (`ElysiumSoundGroups::Resolve`),
proven by `Elysium.Content.SoundGroupResolver`.

### Voice-table category selection: `PrecacheSoundTable` and the group seam [VtMB, recovered 2026-10-10, `docs/specs/layers/L0-entity/walks/L0-r007.md`]

`CBaseEntity::PrecacheSoundTable` `0x1009d460` (vtable slot 71, `+0x11c`; 483 of 501 implementers
run this body, the 18 others -- buttons, doors, terminals, containers, prop switches, the game rules
-- carry nine bodies of their own that call the seam with their own registry) selects the entity's
`SndScheme_Char` category and resolves both group indices. Order: with `+0x9c m_pCombatCharacter` and
`+0xac m_pAnimal` set, `+0xbc m_iVSoundTableIdx = 3`; else slot 70 `IsMonster` true -> 2; else
`CBaseCombatCharacter::IsMale` `0x10336920` (stat slot 0xb, the Gender attribute, `== 1`) -> 1; else
0. With only `+0xa0 m_pCombatWeapon` set -> 0; with neither cache the word is NOT written (it keeps
the constructor's -2, `0x1009d980`). Then, on every path, `+0xb4 m_iVSoundGroup =
FUN_101f55a0(&DAT_1073dc28, this, m_iszVSoundGroup or "", 0)` and `+0xb8 m_iVSoundGroupFemale` =
the same with flag 1. `IsMonster` `0x1009d820` is the char-template record's `General/Monster` byte
(`+0x8e`) when `+0x9c` is set, else false. The getters `GetVSoundTableIdx` `0x1009d5e0` (below 0),
`GetVSoundGroup` `0x1009d6a0` and `GetVSoundGroupFemale` `0x1009d760` (below -1) re-run slot 71
lazily; an entity with neither cache and a loaded table would therefore recurse without bound through
the seam's own `GetVSoundTableIdx` -- no shipped entry point reaches that. Callers: the lazy getters,
`CBaseCombatCharacter::Precache` `0x10340360` (right after the template model precache; reached from
`CAI_BaseNPCTroika::Precache` `0x10298ad0` through `CAI_BaseNPC::Precache`), three
`CBaseCombatWeapon::Precache` bodies and the override classes' `Precache`.

The seam `FUN_101f55a0` `0x101f55a0` (`reg, ent, group, female`): `reg+0x20 == NULL` -> **0** (not
-1). Then `idx = GetVSoundTableIdx(ent)`. A combat character (`+0x9c`) takes **S2b** -- the
`CBaseTerminal` cast (`0x105606d4`) of S2a cannot hold for one, so there is no PC/NPC split here: an
empty group reads the template record's `General/SoundGroup` (`+0x20`, NULL when blank); the female
flag makes `"<group>\Female_PC_Override"` (`"Female_PC_Override"` alone when empty). A weapon (`+0xa0`,
S1a) reads the item record (`FUN_102517b0`): its `+0x2554` cache is -2 for every loaded record
(`FUN_10258b00`), so the arm returns -2 without a lookup; only the dummy record (index 0xffff) ever
caches the lookup of its `sound_group` (`+0x24d4`). Otherwise (S1b): a `CPropSwitch` (`0x105a6404`)
looks the group up; a `func_button` / `func_rot_button` classname falls to the final lookup; any other
non-`CBaseTerminal` answers **-1**, the seam's only explicit miss. The final lookup is `FUN_101f42a0`
on `reg+0x20[idx]`: `FUN_101f39d0`'s component walk (above), and the category ROOT's own key on a
miss -- 0, the per-category counter's first value (`FUN_101f4280`), so an unknown voice group plays
the sounds directly under `Character\Male` etc. `FUN_101f39d0`'s three scratch buffers
(`DAT_1073dad0` 0x104 bytes, `DAT_1074b180`, `DAT_1073dc50`) are process globals: a matching child
that then misses leaves its REMAINDER where its parent's next sibling reads the whole name.

The NPC gender the stat slot answers: retail seeds the stat lists in the `CBaseCombatCharacter`
constructor (`0x10326de0`), applies the datamap keys (`gender` `+0x11a8`, `base_gender_` `+0x111c`)
and then the template at Spawn; where the shipped templates' `General/Gender "Female"` text reaches the
stat (if it does) is UNRECOVERED here.

Port: `Source/ElysiumUE/Private/Substrate/ElysiumEntityVSound.cpp` (slots 70 and 71, the getters, the
words on `FElysiumEntity`), `Substrate/ElysiumVSoundGroup.{h,cpp}` (the seam, every arm;
`IElysiumVSoundRegistry` is the L2 data hook for `reg`, `FElysiumEntityWorld::VSoundCharRegistry`,
null = S0 until L2 builds `SndScheme_Char`), `Audio/ElysiumSoundFolderIndex.cpp` (`FindGroup`,
`GroupIndex`), `ElysiumCombatCharacter.cpp` (`Precache`). Divergence: the arm-6 recursion is refused
once (`bInPrecacheSoundTable`), and an index past the table array answers -1 (`badidx`). Proven by
`Arena/scenarios/audio/l0_voice_table_index.json`. The port's `FElysiumNpc::SeedSheet` re-seeds every
stat at Spawn (after the keys), so an authored `gender` key does not reach `IsMale` -- an L2 gap.

## 8. Sentences and surface sounds (footsteps / impacts)

- **Sentences** [VtMB + data]: `CSentence` in `engine.dll`; the `!`-prefix path
  plays them. `scripts/sentences.txt` is the stock `NAME path.wav {Len n}` format
  (VtMB adds the per-line `{Len …}` duration), used for monster vocalizations
  (`SPI_AGGRO0`, `MX_CHARGE_ROAR0`). Small — most speech is the `.dlg`/`.vcd` layer.
- **Surface properties** [data]: `scripts/surfaceproperties.txt` (63 material
  blocks) carries physics params + `stepleft`/`stepright` WAVs and `impact`/`scrape`
  → a named game-sound; `scripts/game_sounds_surfaceproperties.txt` (48 blocks,
  patch-overridden) holds those named impact sounds (`Metal.Impact` = `soundlevel` +
  volume/pitch range + `rndwave` list). This is the **only** live game-sound script.

## 9. Gameplay definitions and script control

The audio layer above map ambience is data-driven [data]:

- `vdata/system/sndscheme_{char,computer,openable,switch,wpn}.txt` define typed event
  vocabularies and optional animation activities. They are **not** map SoundSchemes.
  They resolve character activity/voice events, usable-object `open`/`close`/`locked`/
  `swing`, switch `on`/`off`, computer `accept`/`access`/`error`/`typing`, and weapon
  events into domain- and entity-specific directory paths.
- Item and weapon definitions carry nested `SoundData` variant lists per pickup,
  attack, deploy, reload, impact and other gameplay events. Discipline definitions
  carry `SoundFX` blocks for activation, loop, hit, interrupt and deactivation.
- `sound_volume_table.txt` maps semantic events such as footsteps, gunshots, impacts,
  feeding, physics and doors to **AI-hearing radii in game units**. Despite its name,
  this is not the audible mixer gain. One action can therefore emit both a rendered
  sound and a separate gameplay-noise event.
- `radio_data.txt` selects the first dependency-true radio loop at map/save load.
  `newscaster_main.txt` / `_side.txt` select dependency-gated `.vcd` stories; these
  reuse the choreography/line-audio path.

`soundgroup` is correspondingly overloaded. In the current map snapshot it appears
on 218 rotating doors, 32 sliding doors, 69 button/switch props and also 124+ NPC/
maker entities. A mover token resolves through the usable domain; an NPC token is a
character voice set. Treating every token as a mover directory is incorrect.

The 36 mirrored game Python files add this executable audio-facing surface (comments
excluded; scheduled string payloads included):

| Method | Calls | Role |
|---|---:|---|
| `PlayDialogFile` | 39 | speaker-attached direct playback; one call intentionally names licensed music |
| `PlaySound` / `StopSound` / `Volume` | 13 / 4 / 2 | point/entity voice control |
| `FadeIn` / `FadeOut` | 10 / 9 | map SoundScheme and music switching |
| `SetSafeArea` | 15 | world combat/music gate, including one scheduled call |
| `SetSoundOverrideEnt` / `SetFakeSilence` | 5 / 1 | NPC/newscaster voice ownership and suppression |
| `Whisper` | 9 | player voice/mental cue |

`PlayDialogFile` paths exercise forward slashes, backslashes, a leading slash,
case differences, WAV and MP3, and dynamic string construction. Resolution is
case-insensitive and slash-normalized; dialogue uses the MP3-first/WAV-fallback rule,
but the verb itself is general direct playback and cannot be hard-wired to a dialogue
directory.

Entity keyvalue sound references get the same tolerance: 41 authored refs across the
maps carry a **leading slash** (`/Area/Santa_Monica/Warehouse/train_bell.wav`) and
resolve to shipped files once it is stripped. Eight refs name files that exist nowhere
in the install (`Music/temp_song.mp3`, six `Music/Stems/Mid_Short cutscene*` stems,
`environmental/electronic/button_beep.wav`) and are silent in retail.

## 10. Dead stock leftovers (ignore)

The `game_sounds` manifest precaches **only** `game_sounds_surfaceproperties.txt`;
every HL2 `game_sounds*.txt` line is commented out [data]. `scripts/soundscapes.txt`
(8 blocks, all Valve `d2_depot`/`cabin` sample data with commented-out waves),
`scripts/sounds.txt` (177 HL2 engine/NPC sounds, unreferenced), and
`scripts/titles.txt` (HL2 placeholder captions — VtMB subtitles come from the
dialogue layer, not here) are all dormant. `CSoundscapeSystem` still exists in the
binaries but no map spawns `env_soundscape`, so it never runs.

## 11. Provenance — key addresses (`engine.dll`, base `0x20000000`) [VtMB]

| Address | Symbol / role |
|---|---|
| `0x20118cf0` | sound init — loads `mss32.dll` + `vaudio_miles.dll` (`VAudio001`); `-nosound`/`-wavonly` |
| `0x2011f620` | `S_StartStaticSound` — channel alloc (`MAX_CHANNELS=128`, base 24, 160-B records), sound-char prefixes, `!`-sentence dispatch, volume clamp 255; flags `0x100` force-loop from sample 0 |
| `0x2013a030` / `0x2013a120` | WAV `smpl`/`cue ` loop-start parse; `SetLooped(true)` writes loop start 0 when `HasLoop()` was false |
| `0x2013b870` | sentence playback path |
| `0x20131dd0` | `DSP_LoadPresetFile` (`scripts/dsp_presets.txt`) |
| `0x20131c40` / `0x20131cc0` | DSP preset counter / processor-name→id table |
| `0x20139d60` | `CAudioSourceWave` ctor |
| `0x201b3c48` | `CAudioMixerWaveADPCM` RTTI (`.?AVCAudioMixerWaveADPCM@@`) — the MS-ADPCM decode mixer, reached via vtable; decode is stock/standard (dr_wav reproduces it) |

**SoundScheme system — `vampire.dll` (base `0x10000000`)** [VtMB, decompiled]. The
scheme parser and `ambient_soundscheme` entity are now pinned (`$ELYSIUM_WORK_ROOT/research/ghidra/$ELYSIUM_EXPORT_ROOT/aud_scheme_*`):

| Address | Symbol / role |
|---|---|
| `0x1022a930` | `CSoundScheme` KeyValues parser — walks `SchemeParams`/`Music`/`Combat`/`Alert`/`Ambient`/`RandomSound` blocks (else `"Unrecognized section '%s' in soundscheme"`); Frequency default 10, stored as-is (0 stays 0) |
| `0x10229ff0` / `0x1022a0c0` | `ambient_soundscheme` factory (size `0x5b0`) / datamap (`scheme_file`, `start_enabled`, FadeIn, FadeOut, think name) |
| `0x1022a300` / `0x1022b480` / `0x1022b4d0` | Activate (`start_enabled` → FadeIn 2.0 s); `InputFadeIn`; `InputFadeOut` |
| `0x1022b590` / `0x1022b520` / `0x1022b660` | FadeIn body; FadeOut body; deactivate (clear think + playing flag) |
| `DAT_10750d78` / `0x10228450` / `0x10229270` / `0x102293f0` | manager singleton; ctor; replace four stems; fade all four slots |
| `0x1022b300` / `0x102298e0` | `CSoundSchemePlayingThink`; polar emit (XY around player, Z around scheme) |
| `0x102282e0` | combat-music timing (`soundscheme_combat_music_time`) |
| `0x1022b290` / `0x1022cf20` | `soundscheme_randomness` (default 1000) / `soundscheme_toggledebug` cvars |
| `0x105b4494` / `0x105b4594` | `CSoundSchemeManager` / `CSoundScheme` RTTI; think name str `0x105b4400` |
| `0x1000e8a9` / `0x1000ef8e` / `0x10010ce9` | `ambient_generic` / `ambient_soundscheme` / `env_soundscape` class registration |
| `0x101ad3e0` / `0x101ad410` / `0x101ad440` / `0x101ad470` | PlaySound / StopSound / ToggleSound / shared dispatcher |
| `0x101ac310` / `0x101ac930` / `0x101ac5e0` / `0x101ada80` / `0x101ac670` | Spawn (loop bit); Precache (`m_fActive`); UpdateOnRemove `SND_STOP`; KeyValue (`fadein` LFO); emit flags |
| `0x101cbb10` / `0x101cbbc0` / `0x101cbc10` / `0x10175290` | `CTriggerEnvAudio` factory size `0x59c`; DSP Touch / EndTouch; room-type vs scheme RoomDSP resolver |

Player DSP transport [VtMB, decompiled]: `vampire.dll` send table builder
`0x1018a730` publishes `m_sndRoomDSP` and `m_sndPlayerDSP`; `client.dll` receive table
builder `0x100a3e00` receives them at client-player offsets `0x144` and `0x148`.

**Retail RandomSound defaults** (from `0x1022a930`) [VtMB, decompiled]: `Volume` 20,
`Frequency` 10, `PitchMin`/`PitchMax` 100/100, `AudibleRadius` 1600, `DistMin`/`DistMax`
800/1400, `HeightMin`/`HeightMax` 20/20, `AngleMin`/`AngleMax` 0/360; `NoPause` sets a
keep-playing flag, `Dry` routes to the dry bus. These are the values an **omitted** key
takes. An authored `Frequency 0` is stored as 0 and never plays (§5).

RE30 (env-audio DSP precedence) and RE31 (RandomSound scheduler) are closed in this
file. Implementation remains AUD7 / AUD6.

Owners: **`engine.dll`** = Miles mixer, codecs (PCM/MS-ADPCM/MP3), DSP graph,
sentences. **`vampire.dll`** = SoundScheme parser + `ambient_soundscheme` +
combat/radio signalling (+ dormant `CSoundscapeSystem`) — **imported and pinned above**.
**`client.dll`** = music-state cross-fader, `RoomDSP` application, wet/dry, ducking —
the music-state addresses there are not yet pinned (importing `client.dll` would pin them).

## 12. Loop regions: what the corpus authors and what retail reads

[VtMB, decompiled 2026-09-08 + corpus census 2026-09-08 over 10,892 `export_v2` sound units]

`CAudioSourceWave`'s RIFF chunk dispatcher `FUN_2013a030` handles exactly four ids and
keeps **one number** out of the two that matter:

- `smpl` (`0x6c706d73`): copies `0x3c` bytes of the chunk, returns early when the dword at
  `+0x28` of that copy is nonzero, and otherwise stores a single dword into `this+0x28`.
- `cue ` (`0x20657563`): copies `0x18` bytes and stores a single dword into `this+0x28`.
- `fact` (`0x74636166`): ignored.
- `VDAT` (`0x54414456`): "Old lipsync data found in %s" DevWarning only.

`this+0x28` is the loop **start** (§7's `FUN_2013a120(true)` writes the same field with 0
when `flag_force_looping` forces a loop on a file that has none). There is no loop-end
field: retail wraps at **end of file** in every case, and a `cue ` point is a loop start
exactly like an `smpl` start.

**Carrying either chunk is what makes a source loop.** Three addresses close it:

- `CAudioSourceWave::CAudioSourceWave` `0x20139d60` initialises `this+0x28` to `-1`.
- The `cue ` arm above stores the point's offset **unconditionally** — 0 is stored like any
  other value — and the `smpl` arm stores the loop start unless it returned early.
- `IsLooped` (vtable slot 8, `FUN_2013a150` `0x2013a150`) is nothing but
  `return -1 < this+0x28`.

So a source loops iff some `smpl` or `cue ` chunk wrote that field, and a `cue ` point at
sample **0** loops the whole file exactly as loudly as one at 6077. Only a member with
neither chunk stays at `-1` and does not loop.

Two details of the `smpl` arm, read off the same `0x3c`-byte copy: the early-return test is
on `loops[0].type` (offset `0x28` in the copy), so a **non-forward** loop — ping-pong or
reverse — is refused and stores nothing, leaving whatever an earlier chunk left; and the
stored dword is `loops[0].start` (offset `0x2c`). Only the first loop of a chunk is
reachable, and since the dispatcher runs once per chunk in file order, **the last chunk to
write wins**.

**The census** (10,892 units). **220 loop**: 57 by `smpl` (58 loops —
`environmental/music/music_rock2 less muffled.wav` ships two `smpl` chunks, both stating a
start of 0, so last-writer-wins cannot change what plays) and **163 by `cue `**, of which
161 sit at offset 0 (`epic/wind.wav`, `area/downtown/downtown_main.wav`, …) and 2 do not
(`environmental/machines/steam2.wav` at 6077, `steam3.wav` at 6475). Only **3** of the 220
start past sample 0 — `area/hollywood/warrens/flow_on.wav` (`smpl` 572416) and those two
`cue ` units — so 217 wrap the whole file. Every `smpl` loop in the corpus is type 0, so the
non-forward refusal above is unexercised here. **12 units carry no samples at all**: 11
whose member the install ships as zero bytes (e.g.
`area/santa_monica/clinic/clinic main bg.wav`) and
`area/santa_monica/clinic/clinic drip flr light loop.wav`, which has a complete MS-ADPCM
header over a zero-length `data` chunk. Retail plays nothing from any of them.

**What the bake does with it** (`importers/sounds_bake.py`, AUD1.1). A start of 0 bakes one
whole-file asset with `bLooping`; a start past 0 bakes `SW_<name>_intro` (`[0, start)`, not
looping) and `SW_<name>_loop` (`[start, last sample]`, looping), which the runtime chains.
**The bake wraps at end of file exactly as retail does**: the loop body always runs to the
last sample and the `smpl` chunk's own `end` field is never cut at, only carried into the
manifest as `authoredEndSample`. Trimming there would have ended the body 159 samples (7 ms)
early on `rain_light_loop.wav` and 1262 samples (29 ms) early on `flow_on.wav`, which is not
what retail plays. A degenerate `start 0 end 0` loop is no special case either — it is a
start of 0, which is a whole-file loop.

Whole-corpus decision counts from a stage over all 10,892 units: **10,660 plain, 217
whole-file loops, 3 intro + 3 loop** = 10,883 assets, 12 units empty. §7's claim that a
`cue ` chunk alone makes the mixer wrap is confirmed here at the source rather than
inferred: it is `IsLooped`'s `>= 0` against the ctor's `-1`.

## 13. The port's scheduling lead, after the asset swap

[Port fact, not a retail one — recorded here because §3's `snd_mixahead` is what it stands in for.]

Retail hands every scene `snd_mixahead` (0.100 s), which is *Source's mixer's* lead: the behaviour
it buys is "the first sample is heard at the authored instant", and the number is a property of
Source's output path. Unreal's path is a different number, so the port composes it instead of
inheriting it — `FElysiumAudioLatency::Lead()` is three terms:

1. **mixer queue** — queried (`FMixerDevice::GetNumOutputBuffers` × `GetNumOutputFrames`);
2. **endpoint** — modelled from the queried callback size and the device period;
3. **submit → the mixer's first pull** — not reported by any engine interface.

Term 3 used to be measured per voice: the hand-rolled decoder minted a `USoundWaveProcedural`
whose generator stamped its own first pull, and the mean over dialogue voices was 37–62 ms per
spoken line, most of it the whole-file MP3 decode. **AUD1.3 (2026-09-08) retired that
measurement.** Rendering moved onto baked `USoundWave` assets, a plain wave has nowhere to
hang a render probe, and a primed asset feeds the mixer out of
Unreal's stream cache rather than paying a decode — so the term stopped being content-dependent
and became a property of the path.

It is now the config constant `UElysiumAudioSettings::SubmitToRenderSeconds`
(Project Settings → Elysium → Audio, `Config/DefaultElysium.ini`). **Shipped at 0.0**: the
pre-asset readings were dominated by a decode that no longer happens, so re-using one would lead
every line by a delay no line pays. The owner re-stamps it from a live `elysium.audio_latency`
reading against asset playback, and the stamped number belongs in this section when it exists.

## 14. What the loose corpus still holds, after the asset swap (2026-09-08)

[Port fact, not a retail one.]

**AUD1.4 retired the loose audio deploy.** `uv run elysium import sound` used to write every
`.wav`/`.mp3` to `Content/ElysiumCorpus/sound/<rel>` and each `.lip` twice — beside its audio and
under `lip/<rel>.lip`. With rendering on baked `USoundWave` assets and
`SoundDir()`/`SoundFile()` deleted, not one of those audio bytes
was read any more, so the lane shrank to the `.lip` mirror alone: one file per `.lip` member, at
`lip/<rel>.lip`, which is the key `FElysiumContentPaths::LipFile` already looks under
(`ElysiumLip::NormalizeLipRel` = the audio key with the extension swapped).

The corpus tree below `Content/ElysiumCorpus` now holds, for audio:

| path | who writes it | what reads it |
|---|---|---|
| `lip/**.lip` | `import sound` (7,105 files, ~31 MB) | `FElysiumContentPaths::LipFile` |
| `sound/schemes/*.txt` | `import sound-schemes` (174 files) | `FElysiumContentPaths::SchemeFile` |
| `sound/**` audio | nobody | nobody — it is `/ElysiumBaked/Sounds/**/SW_<name>` |

`sound/schemes/` is the only thing left below `sound/`. The `sound` lane keeps `sound` in its
`owned_directories` although it writes nothing there, so `corpus_deploy`'s ordinary prune sweeps
the retired audio out of any deployment that ever ran the old recipe (one run: 17,997 files,
1,016 MB → 1.4 MB), and keeps `sound/schemes` in `foreign_directories` so the scheme lane's deploy
is stepped over rather than swept. The whole corpus went 1.1 GB → 62 MB.

`research audio_reference_dispositions` moved with it: the `corpus` disposition no longer asks
whether a loose file exists but whether a published, non-empty V2 unit stands behind the key
(`$ELYSIUM_EXPORT_V2_ROOT/sounds/<key>.glb` with no `empty-member` omission), and every row now
also reports the baked object path it resolves to. Every disposition count is unchanged by the
move — three maps 494 `corpus` / 931 `mp3-first` / 3 `absent-subkey` / 13 `never-voiced` /
2 `silent-in-retail` / 0 `unclassified`, 108 maps 15,800 references and 0 disagreements — which is
the check that the bake's inputs cover exactly what the loose deploy did.
