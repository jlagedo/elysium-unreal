# VtMB audio pipeline — how it works, and how it maps to Godot

How *Vampire: The Masquerade – Bloodlines* produces sound: the codecs and driver,
the mixer, the DSP/reverb bank, the bespoke **SoundScheme** ambience/music system
that replaces Source's `env_soundscape`, point sounds (`ambient_generic`), dialogue,
sentences, and footsteps — then what a Godot 4 port needs from each layer and how it
maps onto Godot's audio nodes.

Evidence tags: **[VtMB]** = read from the user's own DLLs (Ghidra strings/symbols/
addresses); **[SDK]** = Source SDK / leaked-engine reference at `tools/re/source-engine`;
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

So the live audio surface is exactly three things: **`ambient_generic`** (point
sounds), **`ambient_soundscheme`** → **`sound/schemes/*.txt`** (ambience + music +
DSP), and **direct dialogue playback** (`PlayDialogFile`, from Python and the
dialogue layer). Everything else in `scripts/` (`soundscapes.txt`, `sounds.txt`,
`game_sounds*.txt`, `titles.txt`) is dormant HL2 leftover.

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
few stereo; only ~2–3% is 16-bit PCM. **Microsoft ADPCM ≠ IMA ADPCM.** This is the
single most important asset fact for the port (see §11).

**Driver** [VtMB]: `engine.dll` sound init `FUN_20118cf0` @ `0x20118cf0`
`LoadLibrary`s **`mss32.dll`** (RAD Miles Sound System) + **`vaudio_miles.dll`** and
creates the `"VAudio001"` interface; Miles is the mixer/streamer and the MP3 decoder.
Honours `-nosound`/`-wavonly`; prints `"Sound sampling rate: %i"`. `binkw32.dll`
handles Bink intro-video audio (`media/*.bik`) separately — outside this pipeline.

**Dialogue localization** [data]: line files carry a language/take suffix —
`line901_col_e.mp3` (English, 4,843), `_col_f` (216), `_col_m` (8), `_col_n` (48).
Played by `Character.PlayDialogFile("Character/dlg/.../lineNNN_col_e.mp3")` [script],
resolved through the datamap method table (see `python_bridge.md`), with the paired
`.lip` driving mouth animation and the `.vcd` sequencing the scene.

## 3. The engine: mixer, channels, codecs, prefixes (stock Source)

All in `engine.dll`, unchanged from Source [VtMB] cross-checked against
`tools/re/source-engine/public/soundflags.h` + `soundchars.h` [SDK].

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

**Zone selection — the divergence** [VtMB strings + inferred]: stock Source picks
DSP from `env_soundscape`/`dsp_room` entities; VtMB has none. Instead a scheme's
`SchemeParams { "RoomDSP" "<n>" }` names the preset, and `client.dll` (which carries
`RoomDSP`, `dsp_room`, `WET`, `DRY`, `Ducking`) drives the engine `dsp_room` cvar
from the active scheme. Music blocks flagged `"Dry" "1"` bypass the reverb bus (the
`#`/drymix path) so the score isn't smeared by room reverb.

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
                  "Frequency" "10"                    // spawn rate/likelihood
                  "AudibleRadius" "2000"              // attenuation reach
                  "DistMin" "200"  "DistMax" "1000"   // radial distance from anchor/player
                  "HeightMin" "0"  "HeightMax" "100"  // vertical offset from the entity
                  "AngleMin" "330" "AngleMax" "30" }  // azimuth arc (wraps); many one-shots
}
```

Block presence across schemes [data]: `Music` ~140–161, `Combat` ~126–149,
`Ambient` ~130–152, `RandomSound` ~1,240–1,410 total (avg ~8/scheme, max 56),
`Alert` ~7, `SchemeParams` in a handful. **Volumes are 0–100 here** (not Source's
0.0–1.0). `Dry` = route to the dry bus (skip reverb); `NoPause` = keep playing while
the game is paused (menus/loading).

**Semantics** [data comments + inferred]: `Ambient` is a constant loop.
`RandomSound` picks a random point on a ring `[DistMin,DistMax]` at a random
height/azimuth around the entity `origin`, plays at random pitch/volume, attenuated
to `AudibleRadius`, up to `RandomSoundCount` at once — VtMB's positional-ambience
answer to Source `playrandom`/`recPositions`, authored in polar coordinates around
the player instead of from BSP-baked recording points. A scheme with no music
(`cops_outside.txt`) is pure `RandomSound` police barks; a scheme is
`{optional music triad} + {optional ambient loop} + {N random one-shots} + {DSP}`.

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
"spawnflags"   "16"     // 0, 16=Start Silent, 48=Start Silent+Not Looped, 1=Everywhere
"targetname"   "Floresent"
// VtMB-specific keys (not in stock ambient_generic):
"SourceEntityName" "tram_mover"   // parents the sound to a moving entity (154 use it)
"sound_event"      "0"            // + sound_event_level "2"
"flag_no_voice_duck" "1"         // exempt from dialogue ducking (§3)
"flag_force_looping" "1"  "flag_no_sfx" "1"  "flag_skip_collide" "1"
"StartHidden"      "1"            // spawn-muted; revealed via ScriptUnhide (entity_io.md)
```

Fired by presence (looping beds), by I/O, or from Python via the datamap-bound
`Entity.PlaySound()` / `StopSound()` (phone rings, sirens, buzzers) [script]. The
stock LFO/spin envelope block (`lfotype`/`lforate`/`spinup`…) is present but zeroed.

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

## 9. Dead stock leftovers (ignore)

The `game_sounds` manifest precaches **only** `game_sounds_surfaceproperties.txt`;
every HL2 `game_sounds*.txt` line is commented out [data]. `scripts/soundscapes.txt`
(8 blocks, all Valve `d2_depot`/`cabin` sample data with commented-out waves),
`scripts/sounds.txt` (177 HL2 engine/NPC sounds, unreferenced), and
`scripts/titles.txt` (HL2 placeholder captions — VtMB subtitles come from the
dialogue layer, not here) are all dormant. `CSoundscapeSystem` still exists in the
binaries but no map spawns `env_soundscape`, so it never runs.

## 10. What Elysium needs (scope)

The map viewer's job is *atmosphere first*, story audio later. Priority order:

1. **`ambient_soundscheme` + scheme reader** — the biggest perceptual win: per-area
   ambient bed + music + random one-shots. Small, self-contained (174 files, one
   KeyValues grammar). Requires the music/ambient MP3+WAV assets.
2. **`ambient_generic`** — point-source atmosphere (flickering lights, fire, drips).
   Direct wav paths, trivial mapping.
3. **DSP rooms** — optional polish; map `RoomDSP` presets to reverb buses.
4. **Footstep/impact sounds** — needs the player/physics-material layer first.
5. **Dialogue + sentences + `.vcd`/`.lip`** — deferred with the story/NPC port;
   large (5k+ lines, choreography + lipsync unhandled by any current tool).

## 11. Mapping to Godot 4

Verified against Godot 4 docs (this project runs 4.7.1).

**Codec / import.** Godot decodes **MP3** (`AudioStreamMP3`) and **Ogg Vorbis**
natively; `AudioStreamWAV.Format` is `FORMAT_8_BITS`, `FORMAT_16_BITS`,
**`FORMAT_IMA_ADPCM`**, `FORMAT_QOA` — **there is no Microsoft-ADPCM path.** So the
converter must **transcode VtMB's MS-ADPCM WAVs offline** (decode to PCM16, emit
`.wav` PCM or `.ogg`); MP3s (music/dialogue) pass through as bytes into
`AudioStreamMP3`. A `tools/` audio decoder alongside `tex_to_png.py` is the natural
home — Python's `audioop`/`wave` won't decode MS-ADPCM directly, but the codec is
simple (a documented 4-bit predictor with per-block coefficients) or `ffmpeg` can be
shelled if available.

**Point sounds → `AudioStreamPlayer3D`.** `message` → stream; `health/10` →
`volume_db` (20·log10); `radius` → `max_distance` (× 0.0254 m); loop from
`spawnflags`; `SourceEntityName` → parent the player node under that entity's node;
`StartHidden`/`ScriptHide` → `playing=false`. `attenuation_model` =
`ATTENUATION_INVERSE_DISTANCE` approximates the SNDLVL curve closely enough; tune
`unit_size`. `flag_no_voice_duck` → assign to a non-ducked bus.

**SoundScheme → a scheme node + buses.** Export each `sound/schemes/*.txt` to a
per-map sidecar (JSON, like `.ents`/`.water`), and build a runtime `SoundScheme`
node driven by the exported `ambient_soundscheme` entities:
- `Ambient` → one looping `AudioStreamPlayer` (2D/non-positional bed) at `Volume/100`.
- `RandomSound` → an `AudioStreamRandomizer` (random pitch via `PitchMin/Max`,
  random volume) feeding an `AudioStreamPlayer3D` that a small scheduler repositions
  each shot on the polar ring (`DistMin/Max`, `HeightMin/Max`, `AngleMin/Max`) around
  the entity `origin`, at up to `RandomSoundCount` concurrent voices (`max_polyphony`
  / `AudioStreamPlaybackPolyphonic`), gated by `Frequency`, ranged by `AudibleRadius`.
- Scheme swap (`FadeIn`/`FadeOut` I/O) → tween the bed/music `volume_db` between the
  outgoing and incoming scheme nodes.

**Dynamic music → `AudioStreamInteractive`.** Godot 4's `AudioStreamInteractive`
(clips + a transition table, exposed as a stream parameter on an `AudioStreamPlayer`)
is the direct fit: register the scheme's `Music`/`Combat`/`Alert` MP3s as clips, wire
transitions, and drive the active clip from combat state (the port's equivalent of
`SetSafeArea`). `Dry`/`NoPause` map to bus routing (dry bus) and
`AudioStreamPlayer.process_mode = ALWAYS` (play through pause). A hand-rolled
two-player cross-fade is the fallback if `AudioStreamInteractive` is over-heavy.

**DSP rooms → reverb buses + `Area3D`.** Build a bus per distinct `RoomDSP` preset
with an `AudioEffectReverb` (approximate the preset's reverb size/damping/wet from
its `RVA` processor row — no need to reproduce the full DSP graph). Godot's
`Area3D` **reverb bus** (dry/wet send + `uniformity`) is the built-in room-reverb
mechanism; a scheme's active `RoomDSP` selects which reverb bus its 3D sources send
to. `Dry` music routes to a bus with no reverb send.

**Bus layout** (proposed): `Master → {Music, Ambient, SFX(→ per-room reverb buses),
Voice(ducks SFX/Ambient)}`, matching VtMB's ducking and dry/wet split.

## 12. Provenance — key addresses (`engine.dll`, base `0x20000000`) [VtMB]

| Address | Symbol / role |
|---|---|
| `0x20118cf0` | sound init — loads `mss32.dll` + `vaudio_miles.dll` (`VAudio001`); `-nosound`/`-wavonly` |
| `0x2011f620` | `S_StartStaticSound` — channel alloc (`MAX_CHANNELS=128`, base 24, 160-B records), sound-char prefixes, `!`-sentence dispatch, volume clamp 255 |
| `0x2013b870` | sentence playback path |
| `0x20131dd0` | `DSP_LoadPresetFile` (`scripts/dsp_presets.txt`) |
| `0x20131c40` / `0x20131cc0` | DSP preset counter / processor-name→id table |
| `0x20139d60` | `CAudioSourceWave` ctor |

Owners: **`engine.dll`** = Miles mixer, codecs (PCM/MS-ADPCM/MP3), DSP graph,
sentences. **`vampire.dll`** = SoundScheme parser + `ambient_soundscheme` +
combat/radio signalling (+ dormant `CSoundscapeSystem`). **`client.dll`** =
music-state cross-fader, `RoomDSP` application, wet/dry, ducking. The scheme-parser
and music-state addresses in `vampire.dll`/`client.dll` are not yet pinned (only
`engine.dll` was imported into Ghidra) — importing those two would pin them.
