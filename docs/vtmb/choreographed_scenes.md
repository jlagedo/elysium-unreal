# Choreographed scenes — the `.vcd` format and `logic_choreographed_scene`

VtMB plays every cinematic, and every spoken line, out of a **Faceposer choreo scene**: a
plain-text `.vcd` file that lays events on a timeline against named actors. The map entity
`logic_choreographed_scene` is the map-authored player of one; the engine spawns a second,
scriptless player for dialogue lines.

Everything here is `vampire.dll`. `engine.dll` contains **no** choreo code at all — no
`.vcd` string, no `Choreo` symbol, no parser. Addresses are `vampire.dll` at its
imagebase `0x10000000`. The data half is surveyed by **`research/tooling/probes/probe_scenes.py`**.

Related: `docs/vtmb/animation_and_movers.md` (the `.mdl` animation the scenes drive),
`docs/vtmb/audio_pipeline.md` (the sound the `speak` events play), `docs/vtmb/entity_io.md` (the output wires
the scenes fire), `docs/vtmb/game_runtime.md` (where the opening cinematic sits in the story flow).
Facial flex, eyeballs and the `.lip` phoneme files are **not** here — they are
`docs/vtmb/facial_animation.md`.

## Inventory

| | Count |
|---|---:|
| `.vcd` on disk (patch-first merge of VPK + `Unofficial_Patch`) | **5,444** (5,300 VPK, 307 loose patch) |
| `logic_choreographed_scene` entities across the 108 maps | **122**, on 30 maps |
| distinct `SceneFile` values they name | 113 — **105 resolve**, 8 do not |
| `.lip` phoneme files beside the audio | 7,136 (`docs/vtmb/facial_animation.md`) |
| `expressions/*.txt` + `*.vfe` flex tables | 249 pairs (`docs/vtmb/facial_animation.md`) |

So **~5,300 of the 5,444 scenes are never named by a map**: they are per-line dialogue
scenes, one `…/line431_col_e.vcd` beside each `line431_col_e.wav`. The `.vcd` — not the
`.wav` — is the unit of a spoken line, and the class that plays one without a map entity is
`CInstancedSceneEntity` (below).

Eight referenced scenes are missing from the install (map data outliving its assets), on
three maps: `la_dane_1` (3), `hw_tawni_1` (3 news-TV scenes), `sp_endsequences_b` (2). A
missing `SceneFile` is map-data breakage, not a format question.

The busiest maps: `sm_hub_1` 13, `sp_endsequences_b` 13, `hw_vesuvius_1` 12, **`sp_theatre`
12**, `la_dane_1` 9, `sp_giovanni_3/4` 7 each, `sp_ninesintro` 5, `sp_tutorial_1` 3.

## The file format

Plain ASCII, CRLF, opening with a version comment. The grammar is uniform: **every line is
a whitespace-separated word list, optionally followed by a `{ … }` block**; quoted words
keep spaces. That one rule parses the whole file — actors, channels, events and every
sub-block alike.

```
// Choreo version 1
actor "Jack"
{
  channel "Speech"
  {
    event speak "NPC Line"
    {
      time 0.000000 4.870386
      param "character/dlg/main characters/jack_tutorial/line191_col_e.wav"
      param2 "70dB"
      fixedlength
    }
  }
  channel "Scripts"
  {
    event python "Sabbat"
    {
      time 0.680000 -1.000000
      param "OnPreRaidSounds()"
      param2 "OnPreRaidSounds()"
    }
  }
  channel "snarl 1"
  {
    event expression "Snarl 01"
    {
      time 1.286666 3.693333
      param "smiling_jack_expressions"
      param2 "Snarl"
      event_ramp
      {
        0.6862 0.4192
        1.1448 1.0000
      }
    }
  }
  bonerename "Bip01" "Bip01"
}

fps 60
snap off
```

- **`// Choreo version 1`** on 5,434 files; 10 files carry no version line at all. Version 1
  is the only version shipped, and the writer (`FUN_1007bfc0`) emits only that string.
- **`fps <n>`** and **`snap on|off`** close the file. `fps 60` on all 5,443 that have it;
  `snap off` on 5,442, `snap on` once. `fps` is Faceposer's editing grid — the runtime
  reads seconds, not frames.
- **`actor "<name>"`** blocks hold `channel "<name>"` blocks hold `event <type> "<name>"`
  blocks. Actors also carry **`bonerename "<from>" "<to>"`** (5,032 uses) and, once,
  `faceposermodel`. `active 0` appears 3 times (a disabled actor/channel).
- **Channel names are free text and carry no semantics** — 85 distinct spellings across the
  corpus (`Speech Triggers` 21,975, `Speech` 5,374, `Gestures` 675, `Expressions` 338, then a
  long tail of `brows up`, `snarl 1`, `laugh_nodeform`, `tzimisce_wait`…). The engine never
  switches on a channel name; it walks every event of every actor. Channels are Faceposer's
  editing lanes.
- **`time <start> <end>`** is on every event, in **seconds from scene start**. `end` is
  `-1.000000` for an instantaneous event (39 events corpus-wide use it). A few authored
  ranges are degenerate — 21 `silence`/`loud` events disagree with their own `param`, one
  event ends 86 s before it starts and one runs to 1.5 million seconds — so a parser must
  tolerate garbage rather than assert on it.
- **`param` / `param2`** are the two payload strings. There is no `param3`.
- **`fixedlength`** (5,743 uses) marks an event whose length is the asset's, not the
  authored range.
- **`event_ramp { <time> <value> … }`** (1,401 uses in 264 files) is a per-event intensity
  envelope, used by `expression` (1,393) and `gesture` (8).
- **`sequenceduration <s>`** appears on 56 `gesture` events.

### The parser accepts far more than the content uses

`FUN_1007ab40` (event), `FUN_1007b7b0` (actor) and their siblings recognise a token set
inherited from Valve's Faceposer. Scanning all 5,444 files, these are recognised and
**never used**: `tags`, `absolutetags`, `relativetag`, `flextimingtags`, `flexanimations`,
`resumecondition`, `loopcount`, `yaw`, `targethead`, `mapname`, `ramp`, and the flex-track
modifiers `range`, `combo`, `disabled`, `samples_use_time`. A reimplementation only needs
the live subset: `actor`, `channel`, `event`, `time`, `param`, `param2`, `fixedlength`,
`sequenceduration`, `event_ramp`, `bonerename`, `faceposermodel`, `active`, `fps`, `snap`.

## Event types

`CChoreoEvent`'s type enum, read out of the name mapping at `FUN_10074bf0` and the
lowercase parse-token table at `0x10547984`–`0x10547a64`. Nineteen types, `**UNKNOWN**`
for anything else:

| # | Name | Token | In the shipped corpus | Dispatch (`CSceneEntity::DispatchStartEvent`, `0x10082ee0`) |
|---:|---|---|---:|---|
| 1 | `SECTION` | `section` | 0 | scene section marker → `+0x428` |
| 2 | `EXPRESSION` | `expression` | **1,431** | actor-side facial expression → `+0x3f8`; `param` = `expressions/<file>`, `param2` = the expression name in it |
| 3 | `LOOKAT` | `lookat` | 0 | resolve `param` as an entity, then `+0x408` with the event duration |
| 4 | `MOVETO` | `moveto` | 0 | → `+0x40c` |
| 5 | `SPEAK` | `speak` | **5,541** | play `param` on the actor; `param2` is a dB level (`"70dB"`, parsed as a float) |
| 6 | `GESTURE` | `gesture` | **609** | → `+0x400`; `param` = an animation sequence label |
| 7 | `SEQUENCE` | `sequence` | **242** | → `+0x418`; `param` = an animation sequence label |
| 8 | `FACE` | `face` | 0 | resolve `param` as an entity, then `+0x414` |
| 9 | `FIRETRIGGER` | `firetrigger` | **24** | `atoi(param)` ∈ 1…4 → fire `OnTrigger1`…`OnTrigger4` |
| 10 | `FLEXANIMATION` | `flexanimation` | 0 | same handler as `EXPRESSION` (`+0x3f8`) |
| 11 | `SUBSCENE` | `subscene` | 0 | → `+0x420`; nested scenes are walked by the actor/anim passes |
| 12 | `LOOP` | `loop` | 0 | → `+0x424` |
| 13 | `SILENCE` | `silence` | **12,089** | handed to the actor (see below) |
| 14 | `LOUD` | `loud` | **9,809** | handed to the actor |
| 15 | `PYTHON` | `python` | **2** | handed to the actor |
| 16 | `CAMERAMOVE` | `cameramove` | 0 | resolve `param`/`param2` as entities and push both at every client |
| 17 | `CAMERASHOT` | `camerashot` | 0 | **no handler** — the jump table sends it to the unknown-type branch |
| 18 | `CAMERARESTORE` | `camerarestore` | 0 | return the camera to player control, at every client |
| 19 | `BODYSOUND` | `bodysound` | **1** | emit `param` at the actor on sound channel 4; `param2` = dB, default 80, floored at 75 |

Types 13–15 (`SILENCE`, `LOUD`, `PYTHON`) share one dispatch arm at `0x1008320e`: gated on a
flag bit (`0x40` at actor `+0x4C`), the event is handed to the actor's own AI object
(`actor+0x98`, virtual `+0x478`) together with the scene. The scene entity itself does
nothing with them — the `python` event's call string and the mouth-driving envelope are
both the actor's business.

`SILENCE` and `LOUD` always sit on a `Speech Triggers` channel beside the line's `speak`
event, and their `param` is the event's own duration as a string (`"0.590"` for
`time 1.110000 1.700000`) — exact on 21,877 of 21,898. They are the **amplitude envelope of
the line**, cut from the wav at author time: 4,927 of 5,444 scenes carry one. This is
VtMB's jaw-flap track and is independent of the `.lip` phoneme files
(`docs/vtmb/facial_animation.md`) — as is `mstudiomouth_t`, the model's own amplitude-driven jaw.

`CAMERASHOT` is registered, named, parsed — and unhandled. `SECTION`, `LOOKAT`, `MOVETO`,
`FACE`, `FLEXANIMATION`, `SUBSCENE`, `LOOP`, `CAMERAMOVE` and `CAMERARESTORE` all have
handlers but zero authored uses; VtMB's cinematics move the camera with
`camera_keyframe` + `PlayAsCameraPosition`/`PlayAsCameraTarget` entities instead
(`docs/vtmb/game_runtime.md`).

### `speak` — audio resolution is `.mp3`-first

`FUN_10081700` builds `sound/<param>`, swaps the extension for **`.mp3`**, asks the
filesystem whether that exists, and plays `*<name>.mp3` if it does, `*<name>` (the
authored `.wav`) if it does not. The leading `*` is Source's stream-this-sound prefix.
So a `param "…/line191_col_e.wav"` normally plays the shipped `.mp3` — which is what
`docs/vtmb/audio_pipeline.md` records for `PlayDialogFile` as well.

### `gesture` / `sequence` — the animation payload

`param` is a **sequence label**, resolved against the actor's model. In cinematics the
label is usually `entire_scene` (175 of 851 gesture+sequence params) — the single
whole-cast animation inside the entity's `BaseAnim` model. The rest are per-character
dialogue clips (`Smiling_Jack_line191_col_E`, `Damsel_Line231_col_E`). Against today's
exported NPC set (45 NPCs / 62 shared banks) 216 of 851 resolve; the remainder belong to
characters not yet exported, not to a naming mismatch.

## `logic_choreographed_scene` — the entity

C++ class **`CSceneEntity`**; factory `FUN_100802e0`, constructor `FUN_10080830`, object
size `0x580`, vftable `0x1044f05c`, datamap `0x10548180` (records `0x105481c4`, 34, base
`CBaseEntity` `0x10552e18`; the builder is `FUN_10080420`, which writes the six trailing
output records at static-init — a raw image read reports `numFields 0`).

### Keyvalues

| Key | Field | Offset | Type | Uses in map data |
|---|---|---|---|---:|
| `SceneFile` | `m_iszSceneFile` | `0x450` | string | 122 |
| `target1` | `m_iszTarget1` | `0x458` | string | 107 |
| `target2` | `m_iszTarget2` | `0x45c` | string | 66 |
| `target3` | `m_iszTarget3` | `0x460` | string | 32 |
| `target4` | `m_iszTarget4` | `0x464` | string | 23 |
| `override_speech_target` | `m_iszOverrideSpeechTarget` | `0x468` | string | 5 |
| `BaseAnim` | `m_iszBaseAnimSet` | `0x480` | modelname | 73 |
| `MaleAnim` | `m_iszAnimSetForMalePlayer` | `0x484` | modelname | 25 |
| `FemaleAnim` | `m_iszAnimSetForFemalePlayer` | `0x488` | modelname | 25 |
| `position_start` | `m_ActorPosStart` | `0x574` | integer | 105 (70 non-zero) |
| `position_end` | `m_ActorPosEnd` | `0x578` | integer | 106 (46 non-zero) |
| `hide_ents` | `m_bHideEnts` | `0x57e` | bool | 116 (22 on) |
| `force_lod_2` | `m_bForceLOD` | `0x57d` | bool | 8 (7 on) |
| `full_sound` | `m_bFullSound` | `0x57f` | bool | 40 (39 on) |

Non-keyable runtime state in the same map: `m_bIsPlayingBack` `0x498`, `m_bPaused` `0x499`,
`m_flFrameTime` `0x49c`, `m_flCurrentTime` `0x4a0`, `m_flLastUpdateTime` `0x4a4`, plus the
automation block (`m_bAutomated` `0x4ac`, `m_nAutomatedAction` `0x4b0`,
`m_flAutomationDelay` `0x4b4`, `m_flAutomationTime` `0x4b8`), the parsed scene pointer
`0x4bc`, and four cached actor `EHANDLE`s at `0x46c`–`0x478`.

**`force_lod` is a dead keyvalue.** 61 entities set it and no such string exists in
`vampire.dll` — only `force_lod_2`, which 8 entities set. The keyvalue lookup drops the
wire, exactly like `OnEnterMapHere` on a `point_teleport` (`docs/vtmb/game_runtime.md`). `StartHidden`
(12 uses) is real but comes from `CBaseEntity`, not this class.

### Inputs

| Input | Handler | Effect |
|---|---|---|
| `Start` | `InputStartPlayback` → `0x10081e00` → virtual `+0x3c8` = `0x100829e0` | begin playback |
| `Pause` | `InputPausePlayback` → `+0x3cc` = `0x10082ad0` | set `m_bPaused` (only while playing) |
| `Resume` | `InputResumePlayback` → `+0x3d0` = `0x10082b00` | clear `m_bPaused`, re-apply the anim set; the unchanged start clock catches up next frame |
| `Cancel` | `InputCancelPlayback` → `+0x3d4` = `0x10082b80` | stop and fire `OnCanceled` |

Map data only ever sends **`Start` (131 wires)** and **`Cancel` (18)** — `Pause`/`Resume`
are never used. Scenes also receive the generic `Kill` (20), `ScriptHide` (11) and
`ScriptUnhide` (6). The senders are ordinary logic: `logic_relay` 76, `logic_case` 31,
another `logic_choreographed_scene` 18, `npc_VHumanCombatant` 12, `logic_pythoncheck` 11,
`trigger_multiple` 11, `trigger_once` 10, `scripted_sequence` 8. Level scripts start scenes
the same way, through the datamap: `Find("vv_dance_4").Start()` in `hollywood.py`
(`docs/vtmb/python_bridge.md` — an input name *is* a Python attribute).

`InputStartPlayback` carries a one-shot divergence: a global byte at `0x106e7e91`, if set,
makes the **first** `Start` in the process skip the scene entirely — it clears the flag,
evaluates `__main__.G.Story_State=-3` and changes level to `sp_tutorial_1`. That is an
intro-skip path; what sets the flag is not identified.

### Outputs

Seven, all `CBaseEntity`-standard output objects at stride `0x18`:

| Output | Field | Offset | Fires when |
|---|---|---|---|
| `OnStart` | `m_OnStart` | `0x4cc` | `Start` accepted (scene parsed, not already playing) |
| `OnCompletion` | `m_OnCompletion` | `0x4e4` | the scene reaches its end (`OnSceneFinished`, `0x10081b60`) |
| `OnCanceled` | `m_OnCanceled` | `0x4fc` | `Cancel` while playing |
| `OnTrigger1` | `m_OnTrigger1` | `0x514` | a `firetrigger` event with `param "1"` |
| `OnTrigger2` | `m_OnTrigger2` | `0x52c` | `param "2"` |
| `OnTrigger3` | `m_OnTrigger3` | `0x544` | `param "3"` |
| `OnTrigger4` | `m_OnTrigger4` | `0x55c` | `param "4"` |

`OnCompletion` and `OnCanceled` are mutually exclusive: cancelling never fires completion.
Map data wires `OnCompletion` 69×, `OnTrigger1` 13×, `OnTrigger2` 7×, `OnTrigger3` 4×;
`OnStart`, `OnCanceled` and `OnTrigger4` are never wired, though `firetrigger "4"` is
authored (`jack_VS_sabbat.vcd` fires triggers 1–4, and `sp_tutorial_1` wires only 1 and 2 —
3 and 4 fire into nothing).

### Binding actors to entities

`CSceneEntity::FindNamedEntity` (`0x10083cd0`, virtual `+0x430`) resolves a scene's
`actor "<name>"` to a live entity:

- `Player` / `!player` → `UTIL_PlayerByIndex(1)`
- `!playercontroller` → the player's controller entity
- `!dialogpartner` → the scene's speech-target string (seeded by `override_speech_target`)
  if set, else the player's current dialogue partner
- `!target1` … `!target4` → the `targetN` keyvalues, resolved once and cached in the four
  `EHANDLE`s
- anything else → `gEntList.FindEntityByName(name)`

**The shipped scenes bind almost entirely by plain name.** Across 5,444 files the only
`!` actors used are `!playercontroller` (25) and `!dialogpartner` (1); `!target1`…`!target4`
appear **zero** times. Comparing each entity's `targetN` list against its scene's actor
list over the 114 resolvable pairs: 86 match exactly, 28 differ — by ordering
(`hw_vesuvius_1`'s lap dances, `sm_beachhouse_1`), by a stray control character
(`sp_endsequences_b`), or because the entity lists no targets at all while the scene names
an actor (`sp_giovanni_3`'s seven Nadia scenes). So `targetN` is **not** the actor binding;
it is an author-side manifest of the cast, live only through the `!targetN` aliases nothing
uses. A runtime that resolves actors by name alone reproduces every shipped scene.

An actor that resolves to nothing logs `CSceneEntity unable find actor "%s"` and the event
is dropped — the rest of the scene continues.

### The animation set

`BaseAnim` (and `MaleAnim`/`FemaleAnim`) name a **cinematic `.mdl` that holds the whole
multi-actor performance**, e.g. `models/cinematic/tutorial/jack_VS_sabbat.mdl` for
`sp_tutorial_1`'s alley fight.

**One model, N co-located skeletons, one clip.** `jack_vs_sabbat.mdl` is **271 bones** — four
complete 67-bone skeletons rooted `Bip01`, `Bip02`, `Bip03`, `Bip04`, plus three `DummyNN` nodes —
carrying a **single** sequence, `entire_scene`, 461 frames at 30 fps (15.37 s, against the `.vcd`'s
authored `0 → 15.300`). Every actor plays that same clip; `bonerename "BipNN" "Bip01"` is what
selects which skeleton inside it is *this* actor's. That is the whole purpose of the key.

Some cinematics instead ship one file per actor (`Courtroom_bip1.mdl` … `Courtroom_bip7.mdl` for
`sp_theatre`'s seven concurrent courtroom scenes, `snuff_bip1`/`snuff_bip2` in Hollywood), and
those files still carry all four bip skeletons. 106 such models are in the merged install; the
exported maps name **20** distinct ones, and `sp_theatre` alone accounts for 15.

At `Start`, `FUN_100843d0` picks the male or female model by
the local player's `IsMale()`, then for each scene actor resolves the entity, casts it to
`CBaseAnimating`, and hands it the scene, the actor's **`bonerename` pair**, the chosen
anim-set model and the `BaseAnim` model (virtual `+0x3d4` on the actor). That is what
`bonerename "Bip02" "Bip01"` is for: each actor takes a different bone-prefix root out of
the one shared animation. Nested `SUBSCENE` events are walked recursively by the same pass.

### `position_start` / `position_end`

`position_start == 1` (70 of the 105 entities that set the key — all twelve of `sp_theatre`'s
and `sp_tutorial_1`'s alley fight) makes the scene **own the actors' transforms**: at `Start`,
`FUN_10081ed0` saves every actor's origin, angles, solidity and flags into the scene's actor
record, then teleports the actor to the scene entity's own origin/angles so the shared
cinematic animation lines up; every frame `FUN_100846c0` re-pins them; and at
`OnSceneFinished` the saved transform, solid type and solid flags are restored.

`position_end` decides where the actors are left, applied over every actor at completion
(`FUN_100821f0`):

| Value | Effect | Uses |
|---:|---|---:|
| 0 | leave the actors where the animation ended | 60 |
| 1 | move each actor to the scene entity's origin and angles | 1 |
| 2 | restore the origin/angles saved at `Start` | 2 |
| 3 | snap each actor's origin onto its **`bip01`** bone (settle the entity under the pose the animation finished in) | 43 |

The corpus is effectively 0-or-3. Either way the actor is flagged and re-activated for
normal simulation afterwards.

`hide_ents` is set on 116 entities but **enabled on only 22** (including the tutorial's two
Jack lines); when set it hides the surrounding entities for the scene's duration and
unhides them at completion **or** at `Cancel`.

### What `hide_ents` actually selects

`FUN_100826b0` walks the global **NPC list** and hides everything its predicate `FUN_10082520`
accepts; `FUN_100828d0` reverses it. The predicate, in order:

1. the candidate must exist and not be dead or dying (`FUN_100a52a0`);
2. its flags (`FUN_100b39c0`) must not carry bit **`0x20`**;
3. it must not be one of **this scene's own actors** — the scene walks its actor array and compares;
4. its character object (entity `+0x98`) must exist, and that object's EHANDLE at **`+0xfe8` must
   not resolve**. That field is the **current dialogue partner**: `FindNamedEntity`'s
   `!dialogpartner` branch reads exactly `UTIL_PlayerByIndex(1) + 0xfe8`. So an NPC already in a
   conversation is never hidden;
5. finally a class/relationship gate — **not decoded**: a virtual at `+0x740` on that character
   object returning a small enum (value `4` is rejected outright; `2`, `3`, `0x0b` and `0x0e` pass
   when `+0x29c` — which returns an entity — equals the player), plus `+0x650(player)` returning 1
   as an alternative accept.

Steps 1–4 are settled. Step 5's two virtuals are unidentified: they live on the object at entity
`+0x98`, whose vftable is large enough to be entity-sized, and naming them needs that vftable
located and walked. Until then the *set* of NPCs a shipped scene hides is not reproducible, only
the mechanism.

## The timing model

`CSceneEntity`'s playback think is virtual slot **`+0x218`** = `0x100819b0`, and it re-arms
itself to `gpGlobals->curtime` every call — **the scene thinks every frame** while playing.
Per call:

1. bail if there is no parsed scene or `m_bIsPlayingBack` is false;
2. if `m_bPaused`, run the paused think (`+0x3c4` = `0x10081020`) and return — the clock
   does not advance;
3. **`m_flCurrentTime = curtime − m_flStartTime`**, where `m_flStartTime` (`0x4a8`) was
   stamped at `Start`. Scene time is absolute elapsed wall-clock, **not** an accumulator, so
   it cannot drift and cannot be scaled;
4. hand the scene the sound-system latency — the `snd_mixahead` convar, cached in the
   constructor at `0x4c8` — so `speak` events are scheduled against the mixer's lead;
5. `CChoreoScene::Process(m_flCurrentTime)` (`FUN_1007d7f0`), which classifies **every** event
   against the clock and calls back through `IChoreoEventCallback` — `vampire.dll` carries both
   `.?AVIChoreoEventCallback@@` and `.?AVCChoreoEventCallback@@`, so the interface is Valve's
   four-method one, not a single start dispatch. Per event, per frame, exactly one of:
   **START** (its start has been crossed and it was not running) → `DispatchStartEvent`
   (virtual `+0x3e0`); **CONTINUE** (still inside its range); **STOP** (the clock left its
   range); **IGNORE**. An event with no end time (`time t -1`) is treated as
   `end = start`, so it starts on the frame that crosses it and stops on the next one.
   Pending events are dispatched in start-time order;
6. if the scene reports its simulation finished, call `OnSceneFinished` (`+0x3dc`) — which
   restores the actors, fires **`OnCompletion`**, clears the events and unhides;
7. otherwise, if `position_start == 1`, re-pin the actors.

### The completion test

`SimulationFinished` (`FUN_1007d1e0`) is **two** conditions, not one: the scene's current time
(`+0x7c`) must be past its latest time (`+0x90`) **and** its active-event count (`+0x94`) must be
zero. `latestTime` is the maximum over events of the end time — or the start time for an event
that has none — with **SPEAK events extended by the sound-system latency** (`+0x98`, written by
`FUN_1007e5a0` from the scene entity each think). The same latency pulls each speak event's start
*earlier*, so a line is queued ahead of its cue by exactly the mixer's lead and the scene's own end
is pushed out by the same amount.

So an instantaneous event authored at the very end of a scene still gets dispatched before the
scene may finish, and a scene whose last event never stops never completes.

### `Start` and `OnSceneFinished` in order

`FUN_100829e0` gates on an already-parsed scene (`+0x4bc`) and on not already playing, then runs:
reset the simulation → bind the actors → apply the anim set (`FUN_100843d0`) → `position_start`
save-and-teleport (`FUN_10081ed0`) → arm the think → `hide_ents` → and fires **`OnStart` last**.

`FUN_10081b60` is the mirror: stop the live events → reset → clear the playing/paused flags and
zero the clock → apply **`position_end`** → restore the actors → fire **`OnCompletion`** → unhide.

**`Resume` does not re-base the clock.** `FUN_10082b00` re-applies the anim set and writes
`m_flLastUpdateTime` (`+0x4a4`) and the next-think (`+0x17c`) — **not** `m_flStartTime` (`+0x4a8`),
which is what `m_flCurrentTime = curtime − m_flStartTime` reads. Paused time is therefore *counted*:
a resumed map scene snaps forward to wall-clock and dispatches everything that fell inside the pause
in one burst. Unobservable in shipped content — no map wires `Pause` or `Resume` — but it is what
the code does. `Cancel` stops the scene's events, unhides, fires `OnCanceled` with the stored
activator, and clears both flags — without touching `position_end`.

### A map sequences scenes with triggers and camera keyframes, not with scene outputs

None of the shipped `logic_choreographed_scene` entities on `sp_theatre` wires an output — the
only one in the map that does is `embrace_o_matic`, whose `OnTrigger1` opens a door. Scenes do
not chain to each other. The clock that advances a cutscene is the **camera keyframe track**:
each `camera_keyframe` carries `MoveTime` and `Pause`, and its `OnReachedKeyframe` at a chosen
index fires the next stage.

`sp_theatre` is the worked example, and it is a cutscene end to end — one touch starts it and it
finishes by loading the next map, with nothing to decide in between:

1. An **unnamed** `trigger_once` at `(-4169.72, 1232, -309.85)` runs `chooseSire()`,
   `castUnderstudy()`, `controller CreateControllerNPC`, the `embrace_camera`/`embrace_target`
   tracks, `embrace_o_matic` + `_2 Start`, five prop `SetAnimation`s, and `G.Story_State = -4`.
   Arriving from `sp_genesisdevice_1` spawns the player at `info_landmark newgame`, which lies
   inside it, so the cutscene starts on arrival. `info_player_start` — where a console `map`
   load spawns — is 77 units short of it, and the player has to cross that gap.
2. `embrace_camera_22 OnReachedKeyframe` → +4.0 → `move_embrace_actors Teleport` moves `!player`
   into the `start_courtroom` volume.
3. `start_courtroom OnTrigger` → +1.0 → `courtroom_scene_relay`, which starts seven
   `courtroom_scene_bip*` scenes at once, the courtroom camera pair, three prop animations, and
   `fillSeats()`.
4. `courtroom_target_36 OnReachedKeyframe` → `scene_over_fade` +1.0 and `scene_over_relay` +4.0,
   which kills the courtroom cast and triggers `walk_out_relay` — four `scripted_sequence`
   `BeginSequence` walk-outs behind a second camera pair.
5. `walk_out_cam_k OnReachedKeyframe` → +21.0 → `tutorial_change ScriptUnhide`, revealing the
   `trigger_changelevel` that ends the map.
6. `walk_out_fade` → +2.0 → `walk_out_fade_relay` → +3.5 → `male_or_female Test` →
   `Prince_Escort_Male`/`Female`, whose escort walks the player through that changelevel.

Two consequences for a rebuild. **Scene playback is driven by an external clock**, so a scene
that finishes early or late does not shift what follows it — the keyframe track does. And
`theatre.py::tutorialLoad()`, which calls `ChangeMap(2.5, "tutorial", "tutorial_change")`, is
dead: no entity output references it, and step 5 is what actually ends the map.

## `CInstancedSceneEntity` — the dialogue path

RTTI `.?AVCInstancedSceneEntity@@`, vftable `0x1044f584`. A `CSceneEntity` subclass the
engine constructs at runtime (never from map data — no map in the install spawns a
`scripted_scene` or `instanced_scripted_scene`). It inherits `StartPlayback`,
`OnSceneFinished` and the whole dispatch, and **overrides only the think** (`+0x218` →
`0x10084c80`). That override differs in one way that matters:

> it advances `m_flCurrentTime` by an accumulated `curtime − GetLastThink()` delta,
> **clamped to 0.1 s** (the double at `0x104493d0`), instead of taking absolute elapsed
> time. A frame longer than 100 ms therefore makes an instanced scene run late by the
> excess — permanently, since the clamp is never repaid — while a map scene's clock stays
> locked to wall time no matter what the frame did.

It also does not call `OnSceneFinished`: on completion it resets the simulation and clears
its own flags in place. This is the path the ~5,300 per-line `.vcd`s run on.

## What this settles for the rebuild

- The scene file is a **trivially parseable** uniform word-list/brace grammar with a
  14-token live vocabulary. No binary decode, no `.vfe` dependency, no coordinate space.
- The runtime needs **nine** event types to play every shipped scene: `speak`, `silence`,
  `loud`, `expression`, `gesture`, `sequence`, `firetrigger`, `python`, `bodysound`. The
  other ten are dead content-side (and `CAMERASHOT` is dead engine-side too).
- Actors bind **by name**, with `!playercontroller` and `!dialogpartner` as the only live
  aliases; `targetN` can be ignored.
- The completion contract is: `Start` → `OnStart`; scene end → actors restored per
  `position_end`, then `OnCompletion`; `Cancel` → `OnCanceled`, never `OnCompletion`;
  `firetrigger "N"` → `OnTriggerN`, N ∈ 1…4.
- Scene time is absolute seconds since `Start`, ticked every frame, offset by the audio
  mixahead. That maps directly onto a timeline on the game clock.
- Actor/channel/event blocks marked `active 0` remain parseable for inspection but do not enter the
  live timeline or extend its completion time.
- `sp_theatre`'s trial runs **seven** concurrent scenes (`courtroom_scene_bip2`…`bip7`
  started directly, `bip1` through a `logic_pythoncheck` gender branch), plus the two
  embrace scenes and the two Prince-escort scenes.

### Runtime lifecycle and persistence contract

Cinematic clips are driven from absolute scene/event time. Start, resume, and load explicitly seek
the clip; event end, cancellation, restart, and teardown explicitly stop it. Before the last clip is
stopped the actor's final `bip01` world transform is cached, so `position_end == 3` settles the entity
under the actual final pose rather than its component origin. Pause suppresses processing without
moving the scene's start time; resume seeks to wall-clock elapsed time and dispatches missed events.

Every accepted `Start` clears the missing-actor, missing-clip, and missing-speech one-shot diagnostics.
`!dialogpartner` uses `override_speech_target` first and the player's active conversation partner only
when no override is present. Both `speak` and `bodysound` own voice handles. Cancel, restart, teardown,
and load stop or resume them, and an active restored voice starts at its saved elapsed offset instead
of replaying from zero.

Ordering is explicit. Completion stops live events, caches/applies final positions, restores actors,
fires `OnCompletion`, and only then unhides the scene-hidden set. Cancellation stops live events,
unhides, then fires `OnCanceled`; it never applies completion positioning. A map snapshot carries the
activator, actor bindings, original transforms, hidden actors, event dispatched/active latches,
elapsed and pause state, the player-controller relationship, and live voice offsets. Restore seeks
the current pose and recreates active ranged events without replaying past instantaneous outputs.

## Where the rebuild diverges

The runtime class is `FElysiumChoreoScene`
(`Source/ElysiumUE/Private/Substrate/ElysiumChoreoScene.cpp`), over the reader and timeline in
`ElysiumSceneData.{h,cpp}` / `ElysiumScenePlayer.{h,cpp}`. It reproduces the four inputs, the seven
outputs, actor binding by name, the timing model above, and `position_start`/`position_end`. It
diverges as follows, each an explicit call:

- **`gesture` and `sequence` collapse onto one clip player.** Source layers a gesture additively
  over a base sequence; this runtime has a single clip slot per body, so both play through it. Same
  class of simplification as `scripted_sequence`'s missing locomotion. `sequenceduration` is parsed
  and surfaced, not acted on.
- **`position_start` round-trips origin and angles only.** VtMB also saves and restores each actor's
  solidity and solid flags; there is no per-entity solid state here to save.
- **A scene's end is clamped** by `elysium.SceneMaxDuration` (600 s). The corpus contains authored
  ranges that are plainly wrong — one event runs to ~1.5 million seconds — and unbounded, such a
  scene never satisfies the completion test, so `OnCompletion` never fires. On `sp_tutorial_1` that
  is a soft-lock, because the alley fight's completion gates `logic_alley_cleanup`.
- **An event whose start was overrun by a long frame still fires**, rather than being dropped. The
  classifier latches on "has this start passed" instead of "did it fall inside this frame's
  window"; on a monotonic clock the two agree, and the latch additionally survives a hitch and a
  save restored mid-scene.
- **`python` events go through the field-6 event queue** (`EnqueuePython`) rather than the actor's
  own AI object. Both corpus uses are module-level calls, so the receiver is observably the same,
  and this way the call single-steps in the queue window and serializes into a save.
- **`hide_ents` is off by default** (`elysium.SceneHideEnts`), because step 5 of its predicate is
  undecoded (above). The mechanism is implemented; the selection is not trusted.
- **Not reproduced at all:** `force_lod_2` (LOD is not reproduced), the `m_bAutomated` pause
  automation block, and the intro-skip global at `0x106e7e91` — nothing in any map or script
  reaches any of them.

Open, and owned elsewhere: what `full_sound` and `force_lod_2` change (both read once each,
in the speak path and the actor pass), and what sets the intro-skip flag. The facial layer
`expression` and the `.lip` files feed is settled in **`docs/vtmb/facial_animation.md`** — flex
controllers, the flex-rule RPN, both vertex-animation encodings and the phoneme tables. It
also settles what is *not* there: **no shipped model carries eyeball data**, so a scene's
faces are eyelid flexes, not eye poses.

## Provenance

Data half: `research/tooling/probes/probe_scenes.py` (whole-install `.vcd` survey — grammar, channel/event
histograms, per-type key and value shapes, map cross-reference). Binary half: the Ghidra
workspace (`research/tooling/ghidra/driver/README.md`), `vampire.dll`, imagebase `0x10000000`.

The runtime reads the scenes out of the offline mirror `pipeline/src/elysium_pipeline/exporters/UE_extract_scenes.py` writes
(roadmap PL9): every `.vcd` under `$ELYSIUM_EXPORT_ROOT/scenes/` and every `.lip` under `$ELYSIUM_EXPORT_ROOT/lip/`, verbatim and
patch-first, with the `sound/` prefix stripped — so a `SceneFile` is that path minus `sound/`.

| What | Address |
|---|---|
| event-type name table | `FUN_10074bf0`; strings `0x105477a4`–`0x10547888` |
| lowercase parse tokens | `0x10547984`–`0x10547a64`; grammar tokens `0x10547ad4`–`0x10547e58` |
| event parser / actor parser / writer | `FUN_1007ab40` / `FUN_1007b7b0` / `FUN_1007bfc0` |
| `logic_choreographed_scene` factory / ctor | `FUN_100802e0` / `FUN_10080830` |
| datamap / records / builder | `0x10548180` / `0x105481c4` (34) / `FUN_10080420` |
| vftable | `0x1044f05c` (`+0x218` think, `+0x3c4`–`+0x3d4` paused/start/pause/resume/cancel, `+0x3dc` finished, `+0x3e0` dispatch, `+0x430` FindNamedEntity) |
| `DispatchStartEvent` + its jump table | `FUN_10082ee0`, table `0x10083500` |
| `OnSceneFinished` | `0x10081b60` |
| speak audio resolution | `FUN_10081700` |
| actor save / re-pin / anim-set / `position_end` | `FUN_10081ed0` / `FUN_100846c0` / `FUN_100843d0` / `FUN_100821f0` |
| `CInstancedSceneEntity` | vftable `0x1044f584`, think `0x10084c80` |
