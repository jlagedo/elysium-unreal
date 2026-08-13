# Audio plan — open-task specifications

Specifications for **open** audio tasks. Status lives solely in `docs/project/roadmap.md`; a
task that lands is deleted here. No status marks in this file. Facts:
`docs/vtmb/audio_pipeline.md`; design: `docs/architecture/audio-architecture.md`.

### 6.5 Final loose-audio core + catalog

Replace whole-file/game-thread-shaped playback with the request/handle service: one canonical
case-insensitive resolver, PL17's duration/codec/reference catalog, worker decode,
byte-budgeted PCM LRU for short sounds, bounded streaming buffers for dialogue/music/radio,
generation-safe handles, owner + map-epoch cancellation, completion carrying actual audio
start/duration. *Acceptance:* a long MP3 never exists as whole-file PCM; prefetch/decode does
no game-thread file/codec work; map travel cancels every old-map request; forced small buffers
exercise underflow diagnostics without a stale-handle crash. *Deps:* PL17.

### 6.6 UE mixer graph + mix policy

Committed/regenerated game-agnostic Sound Classes, Submixes, reverb return, attenuation
profiles, concurrency/virtualization and Audio Modulation buses; user
master/music/dialogue/ambience/SFX/UI control, dialogue ducking + `flag_no_voice_duck`, `Dry`,
pause/`NoPause`, category priority, selective occlusion. Retire the per-play attenuation
allocation; make `elysium.Mute` a debug override rather than the default-on gate.
*Acceptance:* the Audio debugger names the request's class/submix/buses, category sliders
survive restart, dialogue ducking exempts an authored voice, loops resume from virtualization
without restarting, a shipping-config launch is audible. *Deps:* 6.5.

### 6.7 Map ambience, music + DSP closure

Finish every authored `ambient_generic` flag/envelope/lifetime path; deterministic SoundScheme
transitions honoring `Dry`, `NoPause`, `RandomSoundCount`, `RoomDSP`; drive `events_world`'s
six music outputs from real explore/alert/combat state; one listener-zone resolver combining
scheme DSP with `trigger_environmental_audio`. Settle RE30/RE31; classify the 15 unresolved
wires from `audio_surface_survey.py`; never silently swallow one. *Acceptance:* scheme trigger
pairs in tutorial + hubs crossfade without duplicate stems, the tutorial's authored `room_type`
volumes change and restore DSP, every exported map's point/scheme controls resolve or carry an
explicit optional-content disposition. *Deps:* 6.5, 6.6, RE30, RE31.

### 6.8 Gameplay audio adapters

Typed `Character`/`Openable`/`Switches`/`Computer`/`Weapons` event resolution (never a bare
`soundgroup` lookup); NPC sentences, whispers, `SetSoundOverrideEnt`/`SetFakeSilence`; surface
footsteps/impacts/scrapes; item/weapon/discipline `SoundData`/`SoundFX`; radio/news; the
separate AI-hearing event from `sound_volume_table.txt`. Dialogue and scene line presentation
remain 9.2/12.2, consuming the same line service. *Acceptance:* one door, computer, NPC voice
set, alternating surface footstep, weapon shot + AI stimulus, whisper, radio loop and news
story all resolve through the one request ledger with the correct owner/category. *Deps:* 6.5,
6.6, each owning gameplay caller.
