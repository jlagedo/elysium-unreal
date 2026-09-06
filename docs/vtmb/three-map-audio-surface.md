# Playable-path maps — audio surface

**Maps:** `sp_tutorial_1`, `sm_pawnshop_1`, `sm_hub_1`
**Document role:** map-join census of every authored sound producer on the three
playable-path maps. Engine behaviour lives in `docs/vtmb/audio_pipeline.md`.
Generic I/O lives in `docs/vtmb/entity_io.md`. Tutorial event graph lives in
`docs/vtmb/sp_tutorial_1-event-surface.md`. Hub rain lives in `docs/vtmb/weather.md`.
**Last recovered:** 2026-09-06

Counts are from the current patch-first JSON `.ents` under `$ELYSIUM_EXPORT_ROOT`.
Native behaviour is pinned to retail `vampire.dll` / `engine.dll` as cited in
`audio_pipeline.md`. No live Unreal listen is evidence for this snapshot.

---

## 1. Scale

| Surface | `sp_tutorial_1` | `sm_pawnshop_1` | `sm_hub_1` |
|---|---:|---:|---:|
| Entities | 1,868 | 468 | 2,597 |
| `ambient_generic` | 76 | 7 | 49 |
| `ambient_soundscheme` | 6 | 1 | 4 |
| `trigger_environmental_audio` | 16 | 0 | 0 |
| Audio I/O wires (Play/Stop/Volume/Fade/Kill/override) | 84 | 8 | 78 |
| Autoplay looping beds (sf bit 0x10 clear, not Not-Looped) | 36 | 6 | 42 |
| True one-shots (sf 0x20, no `flag_force_looping`) | 1 | 1 | 4 |

The pipeline doc's "densest `sp_tutorial_1` (98)" figure is the **sum** of the three
audio classnames (76+6+16), not the `ambient_generic` count.

`trigger_environmental_audio` exists only on the tutorial in this trio, and all
sixteen brushes are inert as authored (`StartDisabled 1`, no `Enable` wire) —
see `audio_pipeline.md` §4.

---

## 2. `sp_tutorial_1`

### 2.1 Schemes

| Targetname | `start_enabled` | File |
|---|---|---|
| `City Soundscheme` | 1 | `sound/Schemes/SP_Tutorial_City.txt` |
| `TutorialUnderground` | 0 | `sound/Schemes/SP_Tutorial_Underground.txt` |
| `SabbatNear` | 0 | `sound/Schemes/SP_Tutorial_Sabbat_Near.txt` |
| `Sabbat Chopshop` | 0 | `sound/Schemes/SP_Tutorial_Sabbat_Chop.txt` |
| `SchemeSabbatAlley` | 0 | `sound/Schemes/SP_Tutorial_Sabbat_Alley.txt` |
| `monestary_scheme` | 0 | `sound/Schemes/SP_Soc_2.txt` |

Activate FadeIns `City Soundscheme` at **2.0 s** (`CSoundScheme::vfunc113` →
`FUN_1022b590(this, 2.0)`). The city file's Ambient bed is `area/Downtown/Downtown_Main.wav`
(cue-looped) **and** the map also autoplays `CityAmbience` (`Environmental/City/City Ambience.wav`,
cue-looped, radius 12000) plus `Restaurant`. Those are three concurrent city beds, authored.

`SP_Tutorial_City.txt` authors **seven** `RandomSound` blocks with `Frequency "0"`
(Wesp-disabled Malkavian whispers: `Whispers\Lying\Asps.wav`, `Tongues`, `Shadow`,
`Hemlock`, …). Retail `Frequency 0` never plays (`audio_pipeline.md` §5).

Scheme I/O is FadeIn/FadeOut pairs on `logic_sound_1`, `SchemeAboveGround` /
`SchemeUnderground`, `trig_popup_shootout`, alley relays, and `teleport_hunter`.
`tutwareportal01.OnFullyClosed → scheme_guns.FadeOut` names **no entity** in this map.
`teleport_hunter.OnBeginFade → City Soundscheme.Disable` is an authored input the
class **does not implement** (only FadeIn/FadeOut exist).

### 2.2 Point-sound spawnflags

Histogram: sf `0` ×36 (autoplay loop-entity), `16` ×27 (start-silent loop-entity),
`17` ×12 (everywhere + start-silent), `48` ×1 (`sound_1a`, start-silent + Not-Looped).

`m_fLooping` is 1 unless Not-Looped (0x20) and not `flag_force_looping`. That bit
is the entity's **active latch**, not the mixer wrap. Mixer wrap needs a WAV `smpl`
or `cue ` chunk, or `flag_force_looping` → emit flag `0x100`. Sampled messages:

| Message | Chunk | Retail wrap? |
|---|---|---|
| `Fire Loop.wav`, `Fire_Roaring.wav`, `floresent light.wav` | `smpl` | yes |
| `City Ambience.wav`, `Downtown_Main.wav`, `Crowd screams.wav` | `cue ` | mixer cue-loop |
| `JackChopWindow.wav`, `gun shot 2.wav`, `Jack V Sabbat SFX.wav`, `Riled_1.wav`, `ExplosionSabbat1.wav` | neither | **plays once** even as a "looping" entity |

So the tutorial's gunshots, window break, and alley cinematic bed are **one-shot
WAVs on looping entities**. Retail plays them once and holds `m_fActive` so a
second `PlaySound` is a no-op until `StopSound`/`Kill`. A port that sets
`bLooping` from the entity bit wraps those files forever.

### 2.3 Event cues that Play and never Stop

Authored Play without Stop/Kill (entity still "active" after the WAV ends):

- `sound_window_break` — `tutchopdoorc.OnOpen`
- `RestrauntScreams` — `logic_sound_1` +2 s (WAV **has** `cue `, so this one *can* wrap)
- `SabbatRaidComp` — Play, later Volume 7 only
- `sound_1h` (`Fire_Roaring2`) — Play, later Volume 6; `smpl` bed, likely intentional fire
- `Sabbat Yell` / `Sabbat Laugh` / `Sheriff_Blow` / `Snd Vampire Death` — alley shots
- `sTalkguy_sound0/1/2` — Plus talkguy sequences

Cues with a real stop path: gunfire 1–4 (Play then Stop at +6/+10/+11/+9 s),
`sound_mac10` (Play/Stop/Play/Stop), wolves/howl (`logic_shot_end.Kill`),
`Jack V Sabbat SFX` (`OnCompletion.StopSound` then `logic_alley_cleanup.Kill`).
The cinematic WAV has no `smpl`/`cue `; if `OnCompletion` never fires the port
still loops it, retail would already have finished the file.

Python: `OnPreRaidSounds()` → `logic_sound_1.Trigger()`. Jack
`PlayDialogFile("Character/dlg/MAIN CHARACTERS/jack_tutorial/line251_col_e.mp3")`.
No `Entity.PlaySound()` on this module.

Unwired leftovers (start-silent, never PlaySound in this `.ents`): `JackChopDoorOpen/Close`,
`sound_jackflash`, `sound_rat_2`, `sound_car_1a/1b`, both `sound_ambient_chant2`.

### 2.4 Movers

| Class | `soundgroup` | n |
|---|---|---:|
| `func_door_rotating` | `standard_door` | 25 |
| `func_door_rotating` | `chainlink_gate` / `light_iron_gate` | 3+1 |
| `func_door` | `sliding_glass_door` / `metal_file_cabinet` | 6+1 |
| `func_button` | `small_metal_switch` | 6 |
| `prop_button` | `elevator_button` | 3 |
| `func_elevator` `tutelev` | none; explicit `startsound` / `stopsound` | 1 |

Six `func_button`s author `locked_sound=deny_beep.wav` and `unlocked_sound=button_beep.wav`.
Retail door emission: `open`+`swing` at go-up, `swing` only at go-down, `close` at
`DoorHitBottom` — no code-owned loop (`animation_and_movers.md` B.4.3).

NPC `soundgroup`s: `Young_Thug` on two combatants and one maker.

---

## 3. `sm_pawnshop_1`

One scheme: `Sm_Apartment_Scheme`, `start_enabled 1`,
`sound/Schemes/SM_Apartment_Interior.txt`. No scheme I/O — it never FadeOuts.

Seven `ambient_generic`: six autoplay beds (heater/fridge/freeway/snore) and one
true one-shot, `muddy_message_sound` (sf 48, `Character/CONVERSATIONS/Answering Machine/Muddy.mp3`).

Eight audio wires: one `PlaySound` (the answering-machine line), five
`SetFakeSilence`, two `SetSoundOverrideEnt`. That override/silence pair is the
NPC voice-ownership surface (`audio_pipeline.md` §9), not a point-sound.

Movers: eight `standard_door`, two unnamed rotating doors, `small_metal_switch`,
`tv`, `old_computer` on `prop_hacking`. `answering_machine_button` carries the
same deny/accept beep pair as the tutorial buttons.

`worldspawn` authors `safearea 1` — combat music should stay on the explore stem
unless something calls `SetSafeArea(0)`.

Level script `santamonica.py` owns `Find("answering_machine_button")`; hub
`buzzer.PlaySound()` is a Santa Monica script call, not this BSP.

---

## 4. `sm_hub_1`

### 4.1 Schemes

| Targetname | `start_enabled` | File |
|---|---|---|
| `Scheme_SM_Streets` | 1 | `sound/Schemes/SM_hub_streets.txt` |
| `Scheme_SM_Parking` | 0 | `sound/Schemes/SM_hub_parking_garage.txt` |
| `Scheme_Tunnel_Pier` | 0 | `sound/Schemes/SM_hub_Tunnel_Pier.txt` |
| `SchemeSewers` | 0 | `sound/Schemes/SM_Sewers_1.txt` |

Street ↔ sewer is the densest scheme graph in the trio: manhole `OnActivate` pairs
and `Sewer Scheme` / `Sewers Scheme` `OnStartTouch`/`OnEndTouch` FadeIn/FadeOut
(34 street wires, 33 sewer). `Scheme_Tunnel_Pier` has **zero** wires in this map.
`sewerB2_street.OnEnterMapHere` FadeIns parking and FadeOuts streets.

Retail FadeIn of B immediately clears every other scheme's playing flag and
replaces the four manager stems, so a delayed FadeOut of A after B's FadeIn is a
no-op (`audio_pipeline.md` §5).

### 4.2 Point sounds

Forty-two autoplay beds: fluorescent ×13, freeway, fire low/mid/high, running
pipes, waterdrips, arcade ambience, `asylum_sounds` (`Music_Dance.wav`), Plus
sewer rain (`plus_drop_sound` ×4, `rainsewers.wav`).

Event cues:

| Name | sf | WAV | Wires |
|---|---|---|---|
| `rain_sounds` | 17 (2D + silent) | `area/Santa_Monica/rain_light_loop.wav` (`smpl`) | Play + Stop from the rain timers (`weather.md`) |
| `plus_sobbing_sound` | 16 | `crying_loop.wav` | Play + Stop |
| `spooky_sewer_close` | 16 | `manhole_cover/off.wav` | Play, no Stop |
| `buzzer` | 48 one-shot | `intercom_buzz.wav` | (script `buzzer.PlaySound()`) |
| `cop_convo` | 48 | `Murder Scene/CopTalk.wav` | Play, no Stop |
| `plus_vomit_sound` / `plus_pain_sound` | 48 | vomit/pain | Play, no Stop |
| `AstrolightRunning` | 0 autoplay | `running_away.wav` | also one PlaySound |

`rain_sounds` authors `fadein=10` / `fadeout=10`. Those are `m_dpv` envelope
KeyValues, not I/O seconds (`audio_pipeline.md` §7). The WAV has `smpl`; the
timers' Play/Stop still start and stop the entity.

### 4.3 Movers and voices

25 `standard_door`, `chainlink_gate`, `light_iron_gate`, `car_trunk`; 13
`prop_switch` `manhole_cover`; `old_computer` on `prop_hacking`. NPC groups:
`officer`, `Unique/Chunk`, `Unique/Smblueblood`, several `citizen_*` / `hooker*`.

Thirteen `logic_choreographed_scene` rows are the prophet `.vcd`s; each
`OnCompletion` enables `prophet_trigger`. Line audio is the VCD `speak` path
(`choreographed_scenes.md`), not `ambient_generic`.

---

## 5. Shared world / script surface

All three maps carry `events_world` rows
`OnCombatMusicStart/End`, `OnAlertMusicStart/End`, `OnNormalMusicStart/End` →
Python `OnCombatMusic*()` in `vamputil`. That is the combat-stem gate, not a
cvar.

Python audio verbs used by this trio's scripts: `PlayDialogFile` (tutorial Jack),
`logic_sound_1.Trigger()` (not `PlaySound` on the entity), Santa Monica
`buzzer.PlaySound()` / answering-machine find, `vamputil` `PlaySound`/`StopSound`
helpers.

---

## 6. What this surface requires of a port

Recorded here so a one-line wrap-the-WAV "fix" is not mistaken for retail:

1. `ambient_generic` `PlaySound`/`StopSound` are edge-only; `ToggleSound` is mode 3
   of the same dispatcher and bypasses the early-out.
2. Entity `m_fLooping` ≠ mixer loop. Wrapping every non-0x20 cue is a divergence;
   `flag_force_looping` / `smpl` / `cue ` are the wrap sources.
3. `Frequency 0` RandomSounds stay silent. XY polar around the **player**, Z around
   the scheme origin. `soundscheme_randomness` default 1000; fire when
   `Frequency > RandomInt(1, max)`.
4. One scheme on the manager. `start_enabled` FadeIns in **2.0 s**. Missing FadeIn
   param is **0 s** (instant); negative clamps to 0.5 s. `Disable` is not an input.
   `Kill` on a scheme does not stop the manager stems.
5. Tutorial env-audio brushes never Enable — implementing Touch must not invent
   begin/end pairs for disabled volumes.
6. Door `close` at arrival; `swing` is one event, not a code loop.
