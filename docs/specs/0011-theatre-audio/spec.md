# 0011 theatre-audio — the intro cutscene plays with every sound starting, stopping and clearing as retail does

## Witness
New Game runs the `sp_theatre` intro cutscene start to finish, then the player is handed to the
first `sp_tutorial_1` scene, with no looping or stuck sound at any transition: every voice line,
scene `bodysound` / `speak` cue and authored `ambient_generic` starts, stops and clears exactly as
retail's dispatcher would; the tutorial's authored ambience starts and stops with its entities.
Owner-reported defect: sounds loop and are not cleared at the end of the first cutscene.

## Scope
Tier 0 of the audio programme, "it plays": the catalog, the service, the typed event surface, the
spoken line, authored ambience. Owned elsewhere: the mix (classes, submixes, ducking, occlusion),
scheme/music-state transitions and the random-emitter scheduler, the listener-zone / room-DSP
resolver, Tier-2 enhancements — later specs; the mover sound's emission point — **0009** (7);
the physics impact's AI sound — **0007** (5); footsteps — `docs/vtmb/footsteps.md`.

## Sources
- Oracle: `docs/vtmb/audio_pipeline.md` (§7 entity-loop vs mixer-wrap, §12 the loop rule),
  `docs/vtmb/three-map-audio-surface.md`, `docs/vtmb/footsteps.md`, `docs/vtmb/entity_io.md`
  (`ambient_generic`).
- Authored data, V2 seams under `$ELYSIUM_WORK_ROOT/exports_v2/`: `sounds/<key>.glb` (10,892
  units, schema 1.1.0), `sound-scheme`, `vdata/system/sndscheme_{char,computer,openable,switch,wpn}`,
  `vdata/items/*`, `sentences`, `surface-properties` + `sound-scripts`,
  `vdata/system/{radio_data,newscaster_*,sound_volume_table}`, `map-entities` `dependencies[]` /
  `coverage.unresolved`; `dialogues/`, `scenes/`, `lip/`.

## Witness data
- **The catalog is the V2 sound family and the runtime reads nothing else.** Codec, channels,
  sample rate, exact decoded duration, VPK/loose provenance with sha256, `referencedBy`
  back-edges, the same-stem `.lip`, the mp3-first pairing as data; deployed loose under the
  gitignored `Content/ElysiumCorpus/` with lower-cased keys; every reader folds before it looks;
  a missing reference is a per-referrer `resolved:false`, never a swap; `soundgroup` resolves by
  retail's directory convention (§7b) over the deployed `sndscheme_*` vocabularies. Landed
  2026-09-08 (AUD0.1–0.6): no accessor opens an audio, dialogue, scene or scheme file under the
  legacy root; the `sp_tutorial_1` census (268 referenced paths, 257 resolve, 6 scheme `.txt`, 5
  with a retail disposition).
- **The service is a request/handle service over baked `USoundWave` assets** (owner call
  2026-09-08): `/ElysiumBaked/Sounds/**/SW_<name>`, Unreal's stream cache as decode and budget,
  audio components as voices; what stays because the bytecode observes it: one case-insensitive
  resolver from the folded key (mp3 before wav), the soundgroup walk as existence checks,
  generation-safe handles, owner and map-epoch cancellation, completion carrying scheduled start
  and duration, prefetch (async load + prime). Corpus: 5,539 wav with int16 PCM in the payload
  (MS-ADPCM staged as PCM), 5,342 mp3 as raw frames imported as `.mp3`, 11 zero-byte members, 58
  `smpl` loops, 3 with a real intro (`warrens/flow_on.wav`, `machines/steam2.wav`, `steam3.wav`);
  keys with spaces/parentheses through `BakedAssetName` with the extension kept. Landed on a
  17-asset sample (commit a1475467): the bake lane, the runtime on assets, the `.lip` mirror,
  the contract tests.
- **Typed events.** Movers/switches/containers (`Openable` / `Switches`, `locked_sound`,
  `unlocked_sound`, elevator start/stop, the animated container's `soundgroup`); terminals (four
  authority-side cues over `old_computer` plus the local keystroke click); weapons/items/
  disciplines (`SoundData` / `SoundFX`); characters (sentences, exertions, pain/death,
  activity-bound events, `SetSoundOverrideEnt`, `SetFakeSilence`, the player's `Whisper` as a
  protected voice category); surfaces (impacts/scrapes joined to the surface table, authored
  `rndwave`, pitch/volume ranges); radio/news (dependency lists at map/save load, the selected
  loop streamed, the story through the line service); the AI-hearing event (the semantic noise
  with its `sound_volume_table.txt` radius, published separately from rendered voice;
  `flag_no_sfx` and authored no-noise paths suppress the event without muting the voice; the
  player's reserved locomotion slot's two kill switches `FL_NOTARGET` and `m_fNoPlayerSound`,
  `footsteps.md` §2.5; NPC footfalls raise no stimulus).
- **One line service.** `FElysiumLineService` owns the story/entity join, line replacement,
  subtitles and lipsync; `PlayDialogFile`'s canonical path rules and MP3-first fallback, the
  `Character/dlg/<dialogue-dir>/<dialogue-stem>/line<ID>_col_e` derivation, speaker attachment,
  prefetch, scheduling against the audio clock, authoritative scheduled start/duration. A new NPC
  turn cancels the prior line; closing the conversation cancels its owner; PC choices start no
  voice. `PlayDialogFile` is a general speaker-attached verb, not dialogue-only. `.vcd` `speak`
  and `bodysound` enter the same service on scene time. Subtitles publish on the view state,
  with the `(Auto-Link)` / `(Auto-End)` rule (retain the NPC subtitle through its turn, never
  publish the marker). The mixahead lead: VtMB's `snd_mixahead` 0.100 s on every `speak`; our
  audible latency ~21–61 ms, so dialogue is heard 40–80 ms early against every authored cue;
  `ScheduledAudioClock` stamped at submit; scene timelines take the lead as a scheduling lead.
- **`ambient_generic` is a logical voice set per entity.** `PlaySound`, `StopSound`,
  `ToggleSound` (dispatcher mode 3), `Volume`, hide/unhide, dormancy and `Kill` manipulate that
  set through handles. Retail has **no** `FadeIn` / `FadeOut` inputs — `fadein` / `fadeout` are
  `m_dpv` KeyValues; the mixer wrap is `smpl` / `cue ` or `flag_force_looping` (`0x100`), not the
  entity `m_fLooping` bit; `ScriptHide` does not `SND_STOP`, `Kill`'s `UpdateOnRemove` does. The
  scheme's bed and point emitters retire with the map epoch; activation waits on required
  `start_enabled` prefetch (FadeIns in 2.0 s). The 15 unresolved wires from
  `audio_surface_survey.py` are classified, never swallowed.

## Stories
In build order. A story is done when every behaviour it lists is in the substrate and its
recovery is written in the oracle section it names. Numbers are stable ids cited by other
documents; a split keeps the number and adds a letter. Each open story carries the retail
contract the code must match, the job, what it consumes or provides, and a size (XS–XL) with the
model / effort tier recommended for it.

- [x] **1. The catalog** (was AUD0, landed 2026-09-08): the V2 sound family as the only
  source; the soundgroup resolver over the corpus; schemes to the corpus; the legacy `audio`
  bundle retired; reference validation on the corpus index. Oracle: `audio_pipeline.md`,
  `three-map-audio-surface.md` §2.5.
- [x] **2. The service on assets** (was AUD1.1/1.2/1.4/1.5, landed on the sample set): the
  `bake sounds` lane, the runtime on `USoundWave` assets, the `.lip` mirror, the contract tests
  over the path resolver. Oracle: `audio_pipeline.md` §12.
- [ ] **3. The full corpus and the lead.**
  Job: the full-corpus `bake sounds` (10,883 assets, stage proven at 0 collisions);
  `SubmitToRenderSeconds` a constant on `UElysiumAudioSettings`, measured once with
  `elysium.audio_latency` and recorded; `dr_wav.h`, `dr_mp3.h`, `FElysiumSoundCache`,
  `UElysiumPcmSoundWave` gone.
  Oracle: `audio_pipeline.md` (the measured number).
  Size: S. Effort: Sonnet / medium.
- [ ] **4. The event surface** (was AUD2).
  Retail: the typed events above, each resolved through domain + entity/item override, never a
  bare `soundgroup` lookup.
  Job: movers/switches/containers, terminals, weapons/items/disciplines, characters, surfaces,
  radio/news and the AI-hearing event through the one request ledger with owner and category;
  the footstep producer's two kill switches wired.
  Provides: the typed `Openable` event to 0009/7; the hearing stimulus to 0002.
  Oracle: `audio_pipeline.md`, `footsteps.md` §2.5.
  Size: L. Effort: Opus / high.
- [ ] **5. The spoken line** (was AUD3).
  Retail: the line service, subtitles, the mixahead lead as above.
  Job: `FElysiumLineService`, the subtitle publish with the Auto-Link/Auto-End rule, the
  calibrated lead with the residual stated.
  Provides: the line clock to 0010/13 and 0004/6, 0004/8.
  Oracle: `audio_pipeline.md` § "The spoken line".
  Size: M. Effort: Opus / high.
- [ ] **6. Authored ambience plays** (was AUD4).
  Retail: the voice set per `ambient_generic`, the flags and envelopes, the epoch lifetime.
  Job: the set over handles; the scheme bed and point emitters tied to the map epoch; the 15
  unresolved wires classified.
  Oracle: `audio_pipeline.md` §7.
  Size: M. Effort: Sonnet / high.

## Seams
- Provides: the request/handle service and typed event surface to 0004, 0007, 0008, 0009 and
  0002's hearing; the scheduled-line clock to 0010 and 0004.
- Consumes: 0010's scene join (5); the view-state subtitle publish (11.8, landed).
- Open recoveries: none; the mover emission point is 0009's decision.
