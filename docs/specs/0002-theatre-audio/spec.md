# 0002 theatre-audio — the intro cutscene plays with every sound starting, stopping and clearing as retail does

## Witness
New Game runs the `sp_theatre` intro cutscene start to finish, then the player is handed to the
first `sp_tutorial_1` scene, with no looping or stuck sound at any transition: every voice line,
scene `bodysound`/`speak` cue and authored `ambient_generic` starts, stops and clears exactly as
retail's dispatcher would. Proven by a played theatre act (New Game → full act → tutorial handoff)
and the tutorial's authored ambience starting/stopping with its entities. Owner-reported defect
driving this spec: sounds loop and are not cleared at the end of the first cutscene.

## Scope
- Roadmap rows absorbed: AUD0 (the catalog), AUD1 (the service), AUD2 (the event surface), AUD3
  (the spoken line), AUD4 (authored ambience plays) — Tier 0, "it plays," of the audio programme.
- Out of scope: AUD5–AUD9 (parked) — the mix (classes/submixes/ducking/occlusion), ambience/scheme
  music-state transitions and the random-emitter scheduler, the listener-zone/room-DSP resolver,
  the owner-played Tier-0-through-1 acceptance pass, and Tier-2 enhancements (spatialization, zone
  reverb, loudness, detail layers, adaptive music, accessibility) — all belong to later specs.
  Mover close/loop semantics adjudication is an open owner call noted below, not this spec's to
  resolve.

## Requirements
1. **Catalog is patch-first and complete.** One record per normalized logical path: relative path,
   codec, channels, sample rate, decoded frame count/duration, source provenance, referring
   systems, a decode policy (`resident`/`stream`/`auto`), optional dialogue joins (`.lip`/`.vcd`/
   `.dlg` line id), and validation state (case collisions, missing references). Typed sidecars
   carry authored behaviour (map SoundSchemes incl. music states/random emitters/`Dry`/`NoPause`/
   `RoomDSP`; entity sound schemes for `Character`/`Openable`/`Switches`/`Computer`/`Weapons`;
   item/weapon/discipline sound-event lists; sentences and surface properties; radio/news
   dependency lists; per-map referenced assets; unresolved I/O wires). `soundgroup` is always
   recorded with its domain. Paths case-insensitive/slash-normalized; diagnostics keep original
   spelling. Export fails on an absent required file, ambiguous case collision, unknown scheme
   event, or an output naming no target; optional patch fallbacks are recorded, never silently
   replaced. Raw audio stays gitignored under `$ELYSIUM_EXPORT_ROOT/sound/`.
2. **Runtime is a request/handle service, not whole-file game-thread playback.** One canonical
   case-insensitive resolver over the catalog, worker decode, a byte-budgeted PCM LRU for short
   sounds, bounded streaming buffers for dialogue/music/radio (no long track ever exists as
   whole-file PCM), generation-safe handles, owner and map-epoch cancellation, completion carrying
   actual audio start and duration. Game thread does no file/codec work; prefetch is first-class.
   Failed lookups use a negative cache scoped to the export generation. Budgets, buffer lead and
   underflow counters are visible in the audio debugger.
3. **Every gameplay producer resolves a typed event through domain + entity/item override**, never
   a bare `soundgroup` lookup: movers/switches/containers (`Openable`/`Switches`, `locked_sound`,
   `unlocked_sound`, elevator start/stop, animated-container `soundgroup`, plain `item_container`
   lid cue owned by 9.8); terminals (four authority-side cues over exported `old_computer` plus
   local keystroke click owned by 13.4); weapons/items/disciplines (`SoundData`/`SoundFX`);
   characters (NPC voice sets — sentences, exertions, pain/death, activity-bound events,
   `SetSoundOverrideEnt`, `SetFakeSilence`, player `Whisper` as protected player-voice category, not
   ambient SFX); surfaces (impacts/scrapes joined to the exported surface table — authored
   `rndwave`, pitch/volume ranges — footsteps have their own spec, 
   architecture.md`, retail in `docs/vtmb/footsteps.md`); radio/news (dependency lists evaluated at
   map/save load, selected loop streamed, selected `.vcd` story through the AUD3 line service, kept
   in explicit categories/ownership, never passed as music or dialogue); the AI-hearing event (the
   semantic noise event with authored radius from `sound_volume_table.txt`, published separately
   from rendered voice; `flag_no_sfx` and authored no-noise paths suppress the event without muting
   the voice; player's reserved locomotion slot still needs its two kill switches wired —
   `FL_NOTARGET` and `m_fNoPlayerSound` via `docs/vtmb/footsteps.md` §2.5, the
   `FElysiumPlayer::bNoPlayerSound` seam; NPC footfalls raise no stimulus by design).
4. **One line service serves dialogue and choreography.** `FElysiumLineService` over the AUD1
   subsystem owns story/entity join, line replacement, subtitles and later lipsync; the subsystem
   alone owns resolution/decode/scheduling/rendering. `PlayDialogFile`'s canonical path rules and
   MP3-first fallback, the `Character/dlg/<dialogue-dir>/<dialogue-stem>/line<ID>_col_e` derivation
   for an NPC `.dlg` turn, speaker attachment, prefetch, scheduling against the audio clock, and
   authoritative scheduled start/duration returned to consumers. A new NPC turn cancels the prior
   line; closing/replacing the conversation cancels its owner; PC choices start no voice.
   `PlayDialogFile` is not assumed to be dialogue content — shipped scripts also use it as a general
   speaker-attached playback verb. `.vcd` `speak` and `bodysound` enter the same service, synced to
   scene time.
5. **Subtitles publish on the view state (11.8)**, including the `(Auto-Link)`/`(Auto-End)` rule:
   retain the preceding NPC subtitle through its voice turn and never publish the marker as a
   player response.
6. **The mixahead lead is calibrated, not skipped.** Apply VtMB's `snd_mixahead` 0.100 s to every
   `speak` event; our audible latency is ~21–61 ms, so dialogue is heard 40–80 ms early against
   every authored cue (lipsync, expressions, gestures, camera cuts). `ScheduledAudioClock` is
   stamped at submit, ahead of async MP3 decode (unmeasured term). Scene timelines take the lead as
   a scheduling lead, not as an offset each consumer applies independently.
7. **Authored `ambient_generic` is a logical voice set per entity, not a bare loop.** `PlaySound`,
   `StopSound`, `ToggleSound` (dispatcher mode 3), `Volume`, hide/unhide, dormancy and `Kill` all
   manipulate that one set through handles. Retail has **no** `FadeIn`/`FadeOut` inputs on this
   class — `fadein`/`fadeout` are `m_dpv` KeyValues. Mixer wrap is `smpl`/`cue ` or
   `flag_force_looping` (`0x100`), not the entity `m_fLooping` bit. `ScriptHide` does not
   `SND_STOP`; `Kill`'s `UpdateOnRemove` does. Source attachment, force-looping, every/no-position
   mode, ducking exemption and gameplay-noise flags stay entity semantics and never reach the
   decoder. The active scheme's ambient bed and its point emitters start, stop and retire with the
   map epoch; map activation waits on catalog readiness plus required `start_enabled` prefetch
   (FadeIns in **2.0 s**). Classify the 15 unresolved wires from `audio_surface_survey.py`; never
   silently swallow one. → `docs/vtmb/audio_pipeline.md` §7 (entity-loop vs mixer-wrap split).

## Design
`pipeline/` writes the offline catalog + typed sidecars (AUD0) that the runtime resolves through
one case-insensitive service (AUD1: resolver, worker decode, PCM LRU, streaming buffers, handles,
owner/epoch cancellation). Every gameplay domain (AUD2) and the dialogue/choreo line service
(`FElysiumLineService`, AUD3) sit on top of AUD1 and issue ordinary requests/handles rather than
touching files or codecs themselves. Authored `ambient_generic` (AUD4) is one logical voice set per
entity built from the same handles, keyed to the map epoch so it starts/stops/dies with its
entities rather than free-running. The governing design is;
engine-neutral retail facts are `docs/vtmb/audio_pipeline.md`.

## Seams
- Consumes: LIFE7 (choreo-scene rewire, theatre staging), 12.1 (theatre scene join), 9.1, 11.8
  (view-state subtitle publish) — spec numbers unknown, still roadmap IDs.
- Provides: the request/handle service and typed event surface that PP3's tutorial conversation
  (9.2, 9.9), the animated-container lid cue and terminal session (9.8, 13.4), and the AI-hearing
  senses consumer (13.5) all read; the scheduled-line clock that 12.3–12.5 lipsync/expression/
  camera consume; the mix/ambience/room specs (AUD5–AUD8, out of scope here) build on this tier.

## Tasks
- [ ] **AUD0 The catalog** — offline patch-first catalog + typed sidecars. *Acceptance:* every
  referring system's sound resolves to a catalog record or an explicit optional/missing
  disposition; `audio_surface_survey.py` reports no unclassified reference on the map priority set.
- [ ] **AUD1 The service** — resolver, worker decode, PCM LRU, streaming buffers, handles,
  owner/epoch cancellation. *Acceptance:* a long MP3 never exists as whole-file PCM; prefetch/
  decode does no game-thread file/codec work; map travel cancels every old-map request; forced
  small buffers exercise underflow diagnostics without a stale-handle crash. *Deps:* AUD0.
- [ ] **AUD2 The event surface** — typed events for movers/switches/containers, terminals,
  weapons/items/disciplines, characters, surfaces, radio/news, the AI-hearing event. *Acceptance:*
  one door, container lid, computer, NPC voice set, alternating surface footstep, weapon shot,
  whisper, radio loop and news story all resolve through the one request ledger with correct
  owner/category, and the footstep producer raises a hearing event a nearby NPC acts on. *Deps:*
  AUD1, and each owning gameplay caller.
- [ ] **AUD3 The spoken line** — `FElysiumLineService`, subtitles, mixahead calibration.
  *Acceptance:* a theatre scene's lines and a tutorial conversation are audible and subtitled in
  sync, and the scheduled lead matches the measured path with the residual stated. *Deps:* AUD1,
  9.1, 11.8, 12.1.
- [ ] **AUD4 Authored ambience plays** — `ambient_generic` flags/envelopes/lifetime over one voice
  set per entity; scheme bed + point emitters tied to map epoch. *Acceptance:* every exported map's
  point and scheme controls resolve or carry an explicit optional-content disposition, and the
  tutorial's authored ambience starts, stops and dies with its entities, matching the entity-loop
  vs mixer-wrap split in `docs/vtmb/audio_pipeline.md` §7. *Deps:* AUD1, AUD2.

## Open questions
- **Programme priority.** AUD is unstarted and sits under two playable-path rungs (PP2's scene
  lines via AUD3, PP3's event surface via AUD2). Where it enters the unblocked front is the
  owner's call.
- **Mover sound emission** (adjudicate against AUD2 when the typed `Openable` path lands). Retail
  emits `close` at arrival (`DoorHitBottom`), not at motion start, and starts/stops no loop —
  `swing` is one event like the others, so any looping is the soundgroup's. Elysium currently plays
  `close` at motion start and owns the loop in code. Both differ. → `docs/vtmb/animation_and_movers.md`
  B.4.3.
