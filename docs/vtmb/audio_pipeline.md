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
`inputdata.variant.fieldType == 1` (float) then `flVal`; otherwise **0.0**. Values
**strictly below 0** clamp to **0.5 s**; **0 is instant**; a positive float is that
many seconds. There is **no `Disable` / `Enable` input**.

`FUN_1022b590` (FadeIn): if `this+0x455` (playing flag) is already 1, **return**.
Else walk every registered scheme and `FUN_1022b660` (clear think, `+0x455 = 0`),
replace the four stems (`FUN_10229270` → `FUN_10229430`, same filename retargets
volume, different filename fades the old slot into `+0x174` then starts the new),
install PlayingThink, set `+0x455 = 1`, and store this scheme's EHANDLE at
`player+0x1e04` (RoomDSP fallback). FadeOut (`FUN_1022b520`): if `+0x455 == 0`,
**return**; else fade all four live slots (`FUN_102293f0`) and clear think/flag.

Activate (`vfunc113` `0x1022a300`) registers the EHANDLE (`FUN_102289e0`) then, if
`start_enabled`, FadeIns with a **hardcoded 2.0 s**. `start_enabled 0` waits for
`InputFadeIn`. OnSave (`vfunc129` `0x1022a350`) copies the playing flag into
`start_enabled` so restore Activate FadeIns again.

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
**`fadein` / `fadeout` are those envelope keys**, not seconds-I/O: `atof` → `ftol` →
`<< 8` into `m_dpv+0x1c/+0x20`. There are **no** `fadeinsecs` / `fadeoutsecs` strings
in `vampire.dll`; a 108-map scan of current `.ents` finds **zero** non-zero
`fadeinsecs` keys. 78 `ambient_generic` rows do author a non-zero `fadein` LFO value
(including `sm_hub_1`'s `rain_sounds` `fadein=10` / `fadeout=10`).

There are **no** `InputFadeIn` / `InputFadeOut` methods on this class. The only
`InputFadeIn` string in the image is `CSoundScheme`. Authored
`ambient_generic,FadeIn` wires are `AcceptInput` refusals. `entity_io.md`'s
FadeIn(2)/FadeOut(2) counts are those dead rows.

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
measurement.** Rendering moved onto baked `USoundWave` assets (owner call in `docs/decisions.md`
§Audio), a plain wave has nowhere to hang a render probe, and a primed asset feeds the mixer out of
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
under `lip/<rel>.lip`. With rendering on baked `USoundWave` assets (owner call in
`docs/decisions.md` §Audio) and `SoundDir()`/`SoundFile()` deleted, not one of those audio bytes
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
