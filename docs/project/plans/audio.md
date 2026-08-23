# The audio programme (AUD) — open-task specifications

Specifications for **open** AUD tasks only. Status lives solely in `docs/project/roadmap.md`; a
task that lands is deleted here. No status marks in this file.

The programme's goal: **every authored sound heard** — one catalog, one service, one request
ledger, and VtMB's mix, ambience and rooms reproduced on the Unreal Audio Mixer. The governing
design is `docs/architecture/audio-architecture.md`; the engine-neutral facts are
`docs/vtmb/audio_pipeline.md`. This programme owns everything that makes a sound: the offline
catalog, the runtime service, every gameplay producer, the spoken line and its subtitle, the mix
and the listener zone. It absorbs the retired P6 rows, PL17, 12.2/12.2b and the audio halves of
9.2, 9.8, 13.4 and 13.5; landed rows keep their historical IDs, and old IDs resolve through git
history (`git log -S "<id>"`).

Three tiers on one cumulative ladder — a rung consumes the one below it and nothing above it:

- **Tier 0 — it plays** (AUD0–AUD4). Every authored sound reaches the mixer through one path, and
  every I/O input, script verb, scheme event and gameplay producer that names a sound is conformed
  to it. Correctness of *what* plays and *when*, not of how it sits in the mix.
- **Tier 1 — it sounds like VtMB** (AUD5–AUD8). Levels, ducking, ambience behaviour, music state
  and room DSP — parity with what the original does, adjudicated as Feel/Logic by
  `docs/project/remaster-direction.md`.
- **Tier 2 — beyond VtMB** (AUD9). Enhancements the original never had, parked behind the
  presentation freeze.

Lipsync (12.5) stays a facial task in `plans/theatre.md` and reads AUD3's scheduled line clock.
The AI-hearing event is audio's producer and the AI's consumer: AUD2 raises it, 13.5's senses act
on it.

## Tier 0 — it plays

### AUD0 The catalog

The offline half, patch-first, written by `pipeline/`. One record per normalized logical path:
actual relative path and codec, channel count, sample rate, decoded frame count and duration,
source provenance and every referring system, a suggested decode policy (`resident`, `stream`,
`auto`), optional dialogue joins (`.lip`, `.vcd`, `.dlg` line id), and validation state including
case collisions and missing references. Typed sidecars express authored behaviour rather than
Unreal objects: parsed map SoundSchemes (music states, random emitters, `Dry`, `NoPause`,
`RoomDSP`), entity sound schemes for `Character`/`Openable`/`Switches`/`Computer`/`Weapons`,
item/weapon/discipline sound-event lists, sentences and surface properties, radio/news dependency
lists, per-map referenced assets and unresolved I/O wires. `soundgroup` is always recorded with
its domain, never as a bare token. Paths are case-insensitive and slash-normalized while
diagnostics keep the original spelling. Export fails content validation on an absent required
file, an ambiguous case collision, an unknown scheme event or an output naming no target; optional
patch fallbacks are recorded as optional and never silently replaced. Raw game audio stays
gitignored under `$ELYSIUM_EXPORT_ROOT/sound/`. *Acceptance:* every referring system's sound
resolves to a catalog record or carries an explicit optional/missing disposition, and
`research/tooling/probes/audio_surface_survey.py` reports no unclassified reference on the map
priority set. *Deps:* none.

### AUD1 The service

Replace whole-file/game-thread-shaped playback with the request/handle service the ledger already
sketches: one canonical case-insensitive resolver over AUD0's catalog, worker decode, a
byte-budgeted PCM LRU for short sounds, bounded streaming buffers for dialogue/music/radio (no
long track ever exists as whole-file PCM), generation-safe handles, owner and map-epoch
cancellation, and completion carrying the actual audio start and duration. The game thread
performs no file or codec work; prefetch is a first-class path scenes, dialogue and scheme
transitions use before their deadline. Failed lookups use a negative cache scoped to the export
generation. Budgets, buffer lead and underflow counters are visible in the audio debugger.
*Acceptance:* a long MP3 never exists as whole-file PCM; prefetch/decode does no game-thread
file/codec work; map travel cancels every old-map request; forced small buffers exercise underflow
diagnostics without a stale-handle crash. *Deps:* AUD0.

### AUD2 The event surface

Every gameplay producer resolves a **typed** event through its domain and entity/item override —
never a bare `soundgroup` lookup — and returns an ordinary request:

- **Movers, switches and containers.** `Openable`/`Switches` resolution, explicit `locked_sound`,
  `unlocked_sound` and elevator start/stop keys through the same path, the animated-container
  `soundgroup` path and the plain `item_container` lid cue (9.8 owns the lid mover).
- **Terminals.** The four authority-side cues over the exported `old_computer` group plus the
  local keystroke click (13.4 owns the session).
- **Weapons, items and disciplines.** `SoundData`/`SoundFX` through the same event layer.
- **Characters.** NPC voice sets — sentences, exertions, pain/death and activity-bound events —
  `SetSoundOverrideEnt`, `SetFakeSilence`, and player `Whisper` as a protected player-voice
  category rather than ambient SFX.
- **Surfaces.** Footsteps, impacts and scrapes joined to the exported surface table: left/right
  alternation per character, authored `rndwave`, pitch and volume ranges.
- **Radio and news.** Dependency lists evaluated at map/save load, the selected loop streamed from
  a radio source, the selected `.vcd` story through AUD3's service; both keep explicit categories
  and source ownership rather than passing as music or dialogue.
- **The AI-hearing event.** The semantic noise event with its authored radius from
  `sound_volume_table.txt`, published separately from the rendered voice, including the **footstep
  hearing producer** — the one bus category with nothing raising it, the seam marked in
  `Substrate/ElysiumPlayerEntity.cpp`. `flag_no_sfx` and authored no-noise paths suppress the
  event without muting the voice.

*Acceptance:* one door, container lid, computer, NPC voice set, alternating surface footstep,
weapon shot, whisper, radio loop and news story all resolve through the one request ledger with
the correct owner and category, and the footstep producer raises a hearing event a nearby NPC
acts on. *Deps:* AUD1, and each owning gameplay caller.

### AUD3 The spoken line

One map-owned `FElysiumLineService` over the AUD1 subsystem, serving dialogue and choreography
alike. It owns the story/entity join, line replacement, subtitles and later lipsync; the subsystem
alone owns resolution, decode, scheduling and rendering. `PlayDialogFile`'s canonical path rules
and MP3-first fallback, the `Character/dlg/<dialogue-dir>/<dialogue-stem>/line<ID>_col_e`
derivation for an NPC `.dlg` turn, speaker attachment, prefetch, scheduling against the audio
clock, and the authoritative scheduled start and duration returned to every consumer. A new NPC
turn cancels the prior line; closing or replacing the conversation cancels its owner; PC choices
start no voice. A `PlayDialogFile` path is not assumed to be dialogue content — shipped scripts
also use it as a general speaker-attached playback verb. `.vcd` `speak` and `bodysound` enter the
same service, synced to scene time.

**Subtitles** publish on the view state (11.8), including the `(Auto-Link)`/`(Auto-End)` rule:
retain the preceding NPC subtitle through its voice turn and never publish the marker as a player
response.

**The mixahead lead** is calibrated here, and it blocks lipsync precision. The runtime applies
VtMB's `snd_mixahead` 0.100 s to every `speak` event while our own audible latency is ~21–61 ms,
so dialogue is heard 40–80 ms early against every authored cue — lipsync, expressions, gestures
and camera cuts alike. One term is unmeasured: `ScheduledAudioClock` is stamped at submit, ahead
of the async MP3 decode. Scene timelines take the lead as a scheduling lead, not as an offset each
consumer applies independently.

*Acceptance:* a theatre scene's lines and a tutorial conversation are audible and subtitled in
sync, and the scheduled lead matches the measured path with the residual stated. *Deps:* AUD1,
9.1, 11.8, 12.1.

### AUD4 Authored ambience plays

Every authored `ambient_generic` flag, envelope and lifetime path over one logical voice set per
entity: `PlaySound`, `StopSound`, `ToggleSound`, `Volume`, `FadeIn`, `FadeOut`, hide/unhide,
dormancy and `Kill` all manipulate that set through handles; source attachment, force-looping,
every/no-position mode, ducking exemption and gameplay-noise flags stay entity semantics and never
reach the decoder. The active scheme's ambient bed and its point emitters start, stop and retire
with the map epoch, and map activation waits on catalog readiness plus required start-enabled
prefetch. Classify the 15 unresolved wires from `audio_surface_survey.py`; never silently swallow
one. *Acceptance:* every exported map's point and scheme controls resolve or carry an explicit
optional-content disposition, and the tutorial's authored ambience starts, stops and dies with its
entities. *Deps:* AUD1, AUD2.

## Tier 1 — it sounds like VtMB

### AUD5 The mix

Committed or regenerated game-agnostic routing: Sound Classes, Submixes, the reverb return,
shared attenuation profiles, concurrency and virtualization groups, and Audio Modulation control
buses. User control over master/music/dialogue/ambience/SFX/UI persisted across restart; dialogue
ducking as a dynamics/control-bus mix with `flag_no_voice_duck` exempting a source; `Dry` as no
reverb send rather than a volume category; pause and `NoPause`; category priority and audibility
resolution; selective occlusion with project-owned trace channel, low-pass amount and
interpolation. Retire the per-play attenuation allocation. The original 128-channel mixer is a
fidelity reference, not a forced physical voice count; a rejected request still completes with an
explicit reason. *Acceptance:* the Audio debugger names each request's class, submix and buses;
category sliders survive a restart; ducking exempts an authored voice; a persistent loop resumes
from virtualization at the correct phase without restarting; a shipping-config launch is audible.
*Deps:* AUD1.

### AUD6 Ambience, schemes and music

One logical active map scheme with outgoing and incoming transition states: a transition prefetches
the destination, starts its stems on one audio-clock boundary, then crossfades. `Dry`, `NoPause`,
`RandomSoundCount` and `RoomDSP` are policy inputs to that machine. The random-emitter scheduler
runs on the game RNG stream and game clock for deterministic replay, evaluating the authored polar
distribution around the scheme anchor or listener (RE31); until the frequency curve is recovered
the faithful mode logs the unsupported behaviour and any approximation exists only as a divergence
named beside it. Explore, alert and combat are states of that scheme, driven from real world
combat/safe state through the six `events_world` music outputs. *Acceptance:* scheme trigger pairs
in the tutorial and the hubs crossfade without duplicate stems; combat entry and exit move the
music state from real world state; a seeded replay reproduces the same random-emitter sequence.
*Deps:* AUD4, AUD5, RE31.

### AUD7 The listener zone

One `FElysiumAudioZoneResolver` combining every listener-space input — `trigger_environmental_audio`
brush volumes, the active scheme's `RoomDSP`, interior/exterior filtering and reverb sends, and
explicit scripted overrides — resolved by an explicit priority stack, retaining the previous zone
for hysteresis, publishing one interpolated listener environment to the submix graph. VtMB DSP
preset ids map to generated local reverb/submix presets through a data table, preset `0` neutral;
the signal processing is a presentation-layer remaster choice and its calibration differences are
recorded beside the preset mapping, while the authored preset selection and transition behaviour
are logic to reproduce. Settle RE30 — the precedence between brush `room_type`, scheme `RoomDSP`
and the player's networked room fields. *Acceptance:* the tutorial's 16 authored
`trigger_environmental_audio` volumes change and restore DSP as the player crosses them, and the
precedence stack matches RE30's recovered order. *Deps:* AUD5, AUD6, RE30.

### AUD8 Heard — played acceptance

The programme's finish line, played by the owner from real input on a real map (map priority
`sp_theatre` → `sp_tutorial_1` → `sm_pawnshop_1` → `sm_hub_1`), never injected: the theatre act
plays with audible subtitled lines in sync; the tutorial's doors, terminals, weapons, footsteps
and NPC voices sound from their own producers; ambience and music follow the authored schemes
across every trigger pair; rooms change and restore as the player crosses them; the category
sliders behave; and a shipping-config launch is audible with no unresolved-asset diagnostic.
Headless coverage asserts the request ledger, ownership, timing deadlines, path resolution, cache
budgets and zone transitions without a device; live acceptance remains mandatory for
spatialization, reverb transitions, ducking, loop continuity and device behaviour. *Deps:* AUD0–AUD7,
11.10.

## Tier 2 — beyond VtMB

### AUD9 Enhancement — parked

**Parked: presentation polish behind the graphics freeze (playable-path rule 2); revisit at the
thaw.** Each candidate is a separate owner call under the remaster direction's presentation test,
and none lands as a feature flag or A/B toggle without approval by name. Every one is
remaster-native content: it consumes no game-derived bytes into a tracked asset, and the faithful
behaviour stays the default and stays recoverable.

Candidates, none committed:

- **Spatialization** — HRTF/binaural panning and real occlusion plus obstruction, beyond the
  selective low-pass AUD5 calibrates.
- **Zone reverb** — convolution or measured impulse responses per space, beyond the room-type
  preset table AUD7 maps.
- **Loudness** — normalization and an HDR mix across a source corpus with inconsistent levels.
- **Detail layers** — footstep and impact variation, weapon tails and mechanism foley authored as
  game-agnostic MetaSounds beside the faithful event.
- **Adaptive music** — layering and transitions beyond the six authored `events_world` states.
- **Accessibility** — dialogue-boost slider, mono downmix, subtitle presentation options.

## Open owner calls

- **Programme priority.** AUD is unstarted and sits under two playable-path rungs (PP2's scene
  lines, PP3's event surface). Where it enters the unblocked front is the owner's call.
- **Mover sound emission.** Retail emits `close` at arrival (`DoorHitBottom`), not at motion start,
  and starts and stops no loop — `swing` is one event like the others, so any looping is the
  soundgroup's. Elysium plays `close` at motion start and owns the loop in code. Both differ;
  adjudicate against AUD2 when the typed `Openable` path lands. →
  `docs/vtmb/animation_and_movers.md` B.4.3.
