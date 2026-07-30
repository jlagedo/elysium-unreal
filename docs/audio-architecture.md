# Audio architecture — VtMB semantics on the Unreal Audio Mixer

This document owns the Unreal design of Elysium's audio system. The engine-neutral facts it
consumes — codecs, SoundSchemes, point entities, dialogue paths, DSP presets, sentences,
surface properties, sound groups and the script surface — live in `audio_pipeline.md`. Status
and implementation order live only in `roadmap.md`.

The goal is not to emulate Miles inside Unreal. It is to reproduce VtMB's authored audio logic
while letting Unreal Engine 5.8 own rendering, routing, spatialization, concurrency and DSP.
Nothing game-sourced becomes a committed asset: the user's install remains the source, offline
exports remain regenerable, and runtime audio continues to enter through loose intermediates.

## 1. System boundary

Every audible path and every gameplay noise event crosses one application-owned service:

```text
map entities / movers / NPCs / dialogue / scenes / combat / UI
                             |
                    FElysiumAudioRequest
                             |
                    UElysiumAudioSubsystem
             +---------------+----------------+
             |               |                |
       asset resolver   voice scheduler   listener-zone resolver
             |               |                |
       async decoders    UAudioComponent   reverb/submix state
             +---------------+----------------+
                             |
                     Unreal Audio Mixer
```

Callers describe intent; they never open files, decode bytes, create attenuation objects, choose
a submix, or apply user volume. `IElysiumAudio` remains the world-facing seam, but its final API
is request/handle based rather than one method per subsystem. Map-owned voices carry the current
map epoch; application-owned music, UI and transition voices do not.

Audio has two outputs which must not be conflated:

1. **Rendered sound** — a request sent to the Unreal Audio Mixer.
2. **Gameplay noise** — a semantic event such as `PLAYER_FOOTSTEP_RUN` or
   `PLAYER_GUNSHOT_PISTOL`, with the authored AI-hearing radius from
   `sound_volume_table.txt`.

A gunshot normally produces both. UI, music and dialogue normally produce only rendered sound.
`flag_no_sfx` or an authored no-noise path can suppress the gameplay event without muting the
rendered voice.

## 2. Offline contract

The offline pipeline resolves patch-first and writes an engine-neutral audio catalog. Raw WAV and
MP3 bytes remain loose and game-derived; the catalog is regenerable metadata, not a cooked asset.
One record per normalized logical path carries:

- actual relative path and codec;
- channel count, sample rate, decoded frame count and duration;
- source provenance and every referring system;
- suggested decode policy (`resident`, `stream`, or `auto`);
- optional dialogue joins (`.lip`, `.vcd`, `.dlg` line id);
- validation state, including case collisions and missing references.

Separate sidecars express authored behavior rather than Unreal objects:

- parsed map SoundSchemes, including all music states, random emitters, `Dry`, `NoPause` and
  `RoomDSP`;
- typed entity sound schemes for `Character`, `Openable`, `Switches`, `Computer` and `Weapons`;
- item/weapon/discipline sound-event lists;
- sentences and surface properties;
- radio/news dependency lists;
- per-map referenced assets and unresolved I/O wires.

`soundgroup` is resolved with a **domain**, never as a bare token. A door's
`standard_door` belongs to `Openable`; an NPC's `Young_Thug` belongs to `Character`. Paths are
case-insensitive and slash-normalized, but the original spelling stays in diagnostics.

The exporter fails content validation for an absent required file, an ambiguous case collision,
an unknown scheme event, or an output that names no target. Optional patch fallbacks are recorded
as optional; the runtime never silently invents a replacement.

## 3. Request and handle model

The public contract is concrete:

- `FElysiumAudioSource` is either a direct logical path or typed `{Domain, EventId}`.
- `FElysiumAudioOwner` is `{Kind, StableId, MapEpoch}`. Kinds are application, map entity,
  scene, dialogue session and gameplay system. Components are never owners.
- `FElysiumAudioRequest` carries source, owner, category, placement, gain/pitch, attenuation,
  loop/start/fade policy, routing flags, priority, concurrency key and an optional
  `FElysiumNoiseEvent`.
- `FElysiumVoiceHandle` is `{Slot, Generation}`. A slot generation changes before reuse.
- `FElysiumVoiceEvent` carries state, resolved path, authoritative scheduled audio-clock start,
  media offset, duration and completion reason.

`Submit`, `Prefetch`, `Stop`/fade, `Pause`, `Seek`, `SetGain`, `SetPitch`, `CancelOwner` and
`RetireMapEpoch` are the mutation surface. Read-only request snapshots expose the ledger to debug
and tests. Every event callback is marshalled to the game thread.

`FElysiumAudioRequest` is the single description passed into the subsystem:

| Field | Meaning |
|---|---|
| `LogicalPath` or `EventId` | direct asset or a typed sound-event lookup |
| `Category` | music, dialogue, radio, ambience, world SFX, foley, UI, player voice |
| `Owner` | entity id, component, scene, dialogue session, or application |
| `MapEpoch` | lifetime token for map-owned work |
| `SpatialMode` | 2D, point, attached, area bed, or listener-relative |
| `Transform` / `AttachTarget` | source location or attachment |
| `Gain` / `Pitch` | authored per-play values before buses and mixes |
| `AttenuationProfile` | faithful radius/custom curve plus optional occlusion |
| `LoopMode` | one-shot, finite loop, persistent loop, or stream |
| `Start` | immediate, game-time deadline, or audio-clock deadline |
| `Fade` | attack/release and transition curve |
| `RoutingFlags` | dry, no-pause, no-duck, no-gameplay-noise |
| `Priority` / `ConcurrencyKey` | voice protection and resolution policy |
| `Completion` | optional callback/event for scenes and dialogue |

The return value is an opaque `FElysiumVoiceHandle` containing a generation, not a raw
`UAudioComponent*`. Stop, pause, seek, fade and volume operations accept the handle; entity inputs
also address the owner's named voice set. Reusing a pooled component cannot make an old handle
control a new sound.

Voice state distinguishes `PendingDecode`, `Scheduled`, `Playing`, `Virtual`, `Fading`,
`Paused`, `Complete` and `Failed`. Completion is published on the game thread with the
**authoritative scheduled audio start**, logical duration and reason. The mixer does not expose a
portable, exact physical DAC-start timestamp; the architecture does not claim one. Dialogue,
choreography, subtitles and lipsync join on the scheduled mixer clock instead of each estimating
when a line began.

## 4. Loose-file decode and streaming

`USoundWaveProcedural` remains the correct bridge for BYOG audio, but it is a transport, not the
policy layer. The final decoder has two paths:

- **Short WAV/SFX:** decode on a worker, retain PCM in a byte-budgeted LRU, then queue to a
  per-play procedural wave. Tiny high-frequency UI/foley clips may be pinned by category.
- **Long MP3/dialogue/music/radio:** decode incrementally on a worker into a bounded ring buffer.
  The audio thread consumes queued PCM; underflow is reported and measured. Whole-file PCM for
  long tracks is forbidden.

The game thread never performs file I/O or codec work. A request may prefetch without playing;
scenes, dialogue and SoundScheme transitions use that path before their deadline. Loop points
are frame-based, and a streamed loop primes its next decoder window before the boundary.

The cache key includes canonical path and decode format, not volume, pitch or routing. Failed
lookups use a negative cache scoped to the current export generation so repeated I/O does not
hammer disk. Budgets and underflow counters are visible in the audio debugger.

## 5. Unreal routing and mix

The Unreal Audio Mixer owns the rendered graph. Game-agnostic routing assets are committed or
regenerated under `Content/`; game-derived sound waves are not.

### Sound Classes — semantic groups

```text
Elysium_Master
|- Music
|- Dialogue
|  |- CharacterVoice
|  `- PlayerVoice
|- Radio
|- Ambience
|- SFX
|  |- World
|  `- Foley
`- UI
```

Every procedural wave receives one class before play. Classes provide stable category identity,
priority/loading defaults and inherited modulation. They are not used to reproduce room DSP.

### Submixes — signal processing

```text
Master
|- Music
|- Dialogue --------> dialogue analysis / optional loudness control
|- Radio -----------> radio source effects
|- Ambience
|- SFX
|- UI
`- Reverb return <--- wet sends from eligible spatial sources
```

`Dry` means no reverb send, not a separate volume category. Dialogue ducking is implemented as
a dynamics/control-bus mix over music, ambience and SFX; `flag_no_voice_duck` excludes a source
from that attenuation. The original attack, release and amount remain the faithful baseline.

The Audio Modulation plugin owns user sliders and transient mixes. Stable control buses cover
master, music, dialogue, ambience, SFX and UI; one user mix persists settings and temporary mixes
cover dialogue ducking, pause/menu and scripted states. Public options may group internal buses,
but internal routing does not lose those distinctions.

The original 128-channel mixer is a fidelity/performance reference, not a forced physical voice
count. Unreal concurrency groups protect dialogue and music, cap random ambience and repeated
foley, and resolve by category priority and audibility. Persistent loops virtualize and resume at
the correct phase; disposable one-shots may stop. A rejected request still completes with an
explicit reason.

## 6. Spatial sound and listener zones

Point/attached sounds use shared attenuation profiles, not a freshly allocated settings object per
play. The faithful `ambient_generic` radius and Source loudness curve are encoded as reusable or
cached custom curves. Area beds use non-spatial or box/area behavior so room tone does not collapse
to an arbitrary point. Occlusion is enabled only for categories where it adds information; its
trace channel, low-pass amount and interpolation are project-owned calibration values.

One `FElysiumAudioZoneResolver` combines every listener-space input:

- map brush volumes such as `trigger_environmental_audio`;
- the active map SoundScheme's `RoomDSP`;
- interior/exterior filtering and reverb sends;
- explicit scripted overrides.

It resolves overlaps by an explicit priority stack, retains the previous zone for hysteresis, and
publishes one interpolated listener environment to the submix graph. VtMB's DSP preset ids map to
committed Unreal reverb/submix presets through a data table; preset `0` is neutral. The exact VtMB
precedence between brush `room_type`, scheme `RoomDSP` and the player's networked room fields is an
RE gate in `audio_pipeline.md`; the resolver has separate inputs so that result does not require an
architecture change.

Audio Gameplay Volumes match this component model and can be the adapter when their UE 5.8 beta
status is acceptable. The resolver remains application-owned so the same semantics can target
stable Audio Volumes or direct submix overrides without changing map entities.

Translating selected VtMB/Miles DSP graphs into Unreal reverb and submix presets is a
**presentation-layer remaster choice**. The authored preset selection and transition behavior are
logic to reproduce; the signal-processing implementation is not a bit-identical Miles emulator.
Calibration differences live beside the generated preset mapping.

## 7. System adapters

### `ambient_generic`

The entity owns one logical voice set. `PlaySound`, `StopSound`, `ToggleSound`, `Volume`,
`FadeIn`, `FadeOut`, hide/unhide, dormancy and `Kill` all manipulate that set through handles.
Source attachment, force-looping, every/no-position mode, ducking exemption, gameplay-noise flags,
authored envelopes and sound-event ownership remain entity semantics; none belongs in the decoder.

### Map SoundSchemes and music

There is one logical active map scheme, with outgoing and incoming transition states. A transition
prefetches the destination, starts its stems on one audio-clock boundary, then crossfades. Explore,
alert and combat are states of that scheme, driven by world combat/safe state and the six
`events_world` music outputs. `NoPause`, `Dry`, `RandomSoundCount` and `RoomDSP` are inputs to that
policy. They are not called faithful until RE30/RE31 settle precedence and scheduling.

Random sounds use the game RNG stream and game clock for deterministic replay. The authored polar
distribution is evaluated around the scheme anchor/listener as specified by the recovered behavior.
Until the remaining frequency curve is recovered, the faithful mode logs the unsupported behavior;
an approximation may exist only as a named, A/B-able divergence.

### Dialogue, choreography and body sound

`FElysiumLineService` is map-owned. It owns story/entity joins, line replacement, subtitles and
later lipsync; `UElysiumAudioSubsystem` remains GameInstance-owned and alone owns resolution,
decode, scheduling and rendering. `PlayDialogFile`, `.dlg` playback, `.vcd` `speak`, `.vcd`
`bodysound`, subtitles and lipsync enter that service. It applies canonical path rules and
MP3-first fallback, attaches the voice to the speaker, prefetches, schedules against the audio
clock, and returns the authoritative scheduled start and duration. A `PlayDialogFile` path is not
assumed to be dialogue content: shipped scripts also use it as a general speaker-attached direct
playback verb.

For an NPC `.dlg` turn, the service derives
`Character/dlg/<dialogue-dir>/<dialogue-stem>/line<ID>_col_e` from the dialogue source. A new NPC
turn cancels the prior line and closing/replacing the conversation cancels its owner. PC choices
do not start a voice.

The audio catalog begins loading with the GameInstance subsystem. Map activation waits for catalog
readiness and required start-enabled point/scheme prefetch work; only then does the initial
entity/audio pass run. A retired epoch cancels queued decode before it can realize components.
Application-owned requests with `PersistAcrossTravel` are outside that retirement set.

Scene timelines use audio mixahead as a scheduling lead, not as an offset applied independently by
each consumer. Quartz is used where sample-accurate stem/scene scheduling materially matters; the
game clock still owns story timing and pause semantics.

### Movers, surfaces and gameplay effects

Doors, buttons, computers and weapons resolve a typed sound-event id through their domain scheme
and entity/item override. The resolver chooses a deterministic or random variant as authored and
returns a normal request. Explicit `locked_sound`, `unlocked_sound`, elevator start/stop sounds and
other direct keys use the same path.

Footsteps and impacts join the current physical/surface material to the exported surface table.
Left/right steps alternate per character; impact/scrape events select their `rndwave`, pitch and
volume range. Weapon and discipline `SoundData`/`SoundFX` emit through the same event layer and may
also publish an AI-hearing event.

### NPC voice sets, whispers, radio and news

Character `soundgroup` resolves sentences, exertions, pain/death and activity-bound voice events.
`SetSoundOverrideEnt` changes the spatial/voice owner used by that resolver; `SetFakeSilence`
suppresses broadcast speech without destroying story state. Player `Whisper` is a protected
player-voice category, not ambient SFX.

Radio evaluates its dependency list at map/save load and streams the selected loop from a radio
source. News evaluates the selected `.vcd` story and uses the same scene/line service as other
choreography. Both have explicit categories and source ownership; neither is disguised as music
or generic dialogue merely because it uses MP3/VCD assets.

## 8. Lifetime, pause, travel and persistence

- Entity destruction, hide/dormancy and map-epoch retirement stop or fade every owned voice and
  cancel pending decodes.
- `NoPause`, UI and explicitly application-owned transition audio continue when appropriate;
  other categories pause on the one application pause state.
- Music/scheme state is logical state. Saves persist the active scheme/state and meaningful phase
  only where retail behavior requires it; they never serialize PCM buffers or Unreal components.
- Loading reconstructs logical audio after entities and the listener are ready. Radio/news
  dependency selection is re-evaluated as authored.
- Device loss/swap rebuilds renderer objects while retaining logical handles where possible.

## 9. Debugging and verification

The audio debugger exposes requests rather than only components:

- logical path/event, owner, map epoch, category, class, submix and zone;
- decode/cache/stream state, buffer lead, underflows and memory by category;
- concurrency group, priority, virtualization and rejection reason;
- active scheme/music state, outgoing transition, random scheduler and RNG seed;
- active room/DSP inputs and the winning zone;
- unresolved assets, case collisions, missing targets and unsupported authored flags;
- AI-hearing events separately from rendered voices.

Headless tests assert the request ledger, ownership, timing deadlines, path resolution, cache
budgets and zone transitions without needing an audio device. Content tests run
`tools/audio_surface_survey.py` and fail new unresolved map wires or missing catalog entries. Live
tests remain mandatory for audible spatialization, reverb transitions, ducking, loop continuity
and device behavior.

## 10. UE feature choices

- **Audio Mixer, Sound Classes, Submixes, Concurrency and attenuation:** core path.
- **Audio Modulation:** control buses and user/transient mixes; preferable to building new policy
  on legacy Sound Mixes.
- **Quartz:** precise music-stem and cinematic scheduling, not every ordinary one-shot.
- **Audio Gameplay Volumes:** optional adapter behind our zone resolver while Epic labels the
  feature beta.
- **MetaSounds:** reserved for committed, game-agnostic remaster-native procedural sound design or
  reusable authored DSP templates. Loose VtMB files do not become one MetaSound asset per sound,
  and MetaSounds do not replace the resolver, voice scheduler or mixer graph.

UE 5.8 grounding: [Audio Mixer](https://dev.epicgames.com/documentation/en-us/unreal-engine/audio-mixer-overview-in-unreal-engine),
[Sound Classes](https://dev.epicgames.com/documentation/en-us/unreal-engine/sound-classes-in-unreal-engine),
[Audio Modulation](https://dev.epicgames.com/documentation/en-us/unreal-engine/audio-modulation-overview-in-unreal-engine),
[Sound Concurrency](https://dev.epicgames.com/documentation/en-us/unreal-engine/sound-concurrency-reference-guide),
[Sound Attenuation](https://dev.epicgames.com/documentation/en-us/unreal-engine/sound-attenuation-in-unreal-engine),
[Quartz](https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-quartz-in-unreal-engine),
[`USoundWaveProcedural`](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/USoundWaveProcedural),
[Audio Gameplay Volumes](https://dev.epicgames.com/documentation/en-us/unreal-engine/audio-gameplay-volumes-overview),
and [MetaSounds](https://dev.epicgames.com/documentation/en-us/unreal-engine/metasounds-the-next-generation-sound-sources-in-unreal-engine).
