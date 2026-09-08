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
1. **The catalog is the V2 sound family, and the runtime reads nothing else.** One `sound` unit
   per normalized logical path (`exports_v2/sounds/<key>.glb`, schema 1.1.0): codec, channels,
   sample rate, exact decoded duration, VPK/loose provenance with sha256, `identity.referencedBy`
   back-edges from the corpus index, the same-stem `.lip` decoded and capsuled, and the mp3-first
   pairing as data. Typed units carry authored behaviour: `sound-scheme` (music states, random
   emitters, `Dry`/`NoPause`/`RoomDSP`), `vdata/system/sndscheme_{char,computer,openable,switch,wpn}`,
   `vdata/items/*` (`SoundData`/`SoundFX`), `sentences`, `surface-properties` + `sound-scripts`,
   `vdata/system/{radio_data,newscaster_*,sound_volume_table}`, and `map-entities`
   `dependencies[]`/`coverage.unresolved` for per-map references and dead wires. Deployed loose
   under the gitignored `Content/ElysiumCorpus/` (`sound/`, `lip/`, `dlg/`, `scenes/`, `vdata/`,
   and `sound/schemes/` once AUD0.3 lands) with lower-cased keys; every runtime reader folds
   before it looks, diagnostics keep the authored spelling. Optional patch fallbacks are recorded,
   never silently replaced; a missing reference is a per-referrer `resolved:false`, never a swap.
   `soundgroup` resolves at runtime by retail's directory convention (§7b) over the deployed
   `sndscheme_*` vocabularies, not through a pipeline-authored index. No audio consumer opens a
   file under the legacy `-ElysiumContentRoot`; `catalog.json`, `soundgroups.json` and the
   `sound/Schemes/*.txt` mirror are retired with the `export bundle audio` lane. Decode policy is
   derived at load from `codec`; category is a property of the referrer set; case-collision
   roll-ups belong to the corpus index.
2. **Runtime is a request/handle service over baked sound wave assets, not a hand-rolled
   decoder.** (Owner call 2026-09-08, `docs/decisions.md` Audio.) Every `sound` unit is baked to
   a `USoundWave` under `/ElysiumBaked/Sounds/**/SW_<name>`; Unreal's stream cache is the worker
   decode, byte budget and streaming buffer, and audio components are the voices. What stays in
   the port because the bytecode observes it: one canonical case-insensitive resolver that computes
   the asset path from the folded corpus key (mp3 probed before wav), retail's soundgroup walk as
   existence checks by path, generation-safe handles, owner and map-epoch cancellation, completion
   carrying scheduled start and duration, first-class prefetch (async load + prime). A reference
   with no asset is a per-referrer `resolved:false` at bake and a diagnostic at runtime, so no
   negative cache exists. Game thread does no file/codec work
   because there is no file/codec work. Loaded/pending/retained counts are visible in the audio
   debugger; the mixahead lead is re-stamped once against asset playback.
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
one case-insensitive service (AUD1: path resolver over baked `USoundWave` assets, async
load/prime, audio-component voices, handles, owner/epoch cancellation). Every gameplay domain
(AUD2) and the dialogue/choreo line service (`FElysiumLineService`, AUD3) sit on top of AUD1 and
issue ordinary requests/handles rather than touching assets or codecs themselves. Authored
`ambient_generic` (AUD4) is one logical voice set per entity built from the same handles, keyed to
the map epoch so it starts/stops/dies with its entities rather than free-running. The governing
design is; engine-neutral retail facts are `docs/vtmb/audio_pipeline.md`.

## Seams
- Consumes: LIFE7 (choreo-scene rewire, theatre staging), 12.1 (theatre scene join), 9.1, 11.8
  (view-state subtitle publish) — spec numbers unknown, still roadmap IDs.
- Provides: the request/handle service and typed event surface that PP3's tutorial conversation
  (9.2, 9.9), the animated-container lid cue and terminal session (9.8, 13.4), and the AI-hearing
  senses consumer (13.5) all read; the scheduled-line clock that 12.3–12.5 lipsync/expression/
  camera consume; the mix/ambience/room specs (AUD5–AUD8, out of scope here) build on this tier.

## Tasks
- [x] **AUD0 The catalog** — the V2 sound family is the catalog; retire the last legacy reads.
  *State 2026-09-08:* 10,892 sound units exported and deployed 1:1 to `Content/ElysiumCorpus/sound`
  (5,550 wav + 5,342 mp3; 7,105 `.lip` beside the audio and mirrored to `lip/`), import report
  clean; `dlg` (147), `scenes` (5,444) and `vdata` (465, incl. every `sndscheme_*`, `sound_volume_table`,
  `radio_data`, `newscaster_*`, `items/*`) deployed. Every audio byte read in `Source/` goes through
  `SoundDir()` on the corpus; `.lip`, `.vcd`, `.dlg`, `sound_volume_table` likewise. Landed 2026-09-08 (AUD0.1–0.6 below). Before: three files
  were opened from the legacy export root: `audio/catalog.json`
  (`ElysiumAudioSubsystem.cpp:204`, read for existence and `version==1` only, stores nothing),
  `sound/usable/soundgroups.json` (`ElysiumMoverSounds.cpp:35`, consumed by movers, terminals and
  the Cog audio window) and `sound/Schemes/*.txt` (`ElysiumSoundScheme.cpp:158`, raw `Root()`
  concat; the `sound-scheme` unit publishes no capsule and no importer lane exists).
  `sp_tutorial_1` census (`docs/vtmb/three-map-audio-surface.md` §2.5): 268 referenced paths,
  257 resolve in the corpus, 6 are the scheme `.txt` (legacy only), 5 exist nowhere and each has
  a retail disposition. *Acceptance:* no accessor or raw path in `Source/` opens an audio,
  dialogue, scene or scheme file under `Root()`; every reference on the three playable-path maps
  resolves to a corpus file or an explicit disposition; the legacy `audio` bundle is deleted.
  - [x] **AUD0.1** Delete the `catalog.json` read, `AudioCatalogFile()` and the `bCatalogReady`
    map-activation gate (`ElysiumMapActorLifecycle.cpp:69,115`); readiness is corpus presence.
  - [x] **AUD0.2** Soundgroup resolver over the corpus: token → `usable/<category>/<group>/<subkey>.wav`,
    subkey vocabulary from the deployed `sndscheme_{openable,switch,computer}.txt`, category by
    class (door → `openable`, button/switch → `switches`, `prop_hacking` → `computers`), the
    space/underscore token variance reproduced, a missing subkey silent with a diagnostic (retail
    `elevator_button` ships no `off`). Delete `MoverSoundGroupsFile()`, `soundgroups.json` and
    `entity_events.json`. NPC `soundgroup`s (`Young_Thug`) stay on the character voice path.
  - [x] **AUD0.3** Schemes to the corpus: `sound_scheme_glb.py` publishes the source capsule, a new
    `import sound-schemes` lane deploys `Content/ElysiumCorpus/sound/schemes/<name>.txt`, the runtime
    gets a `SchemeFile(Rel)` corpus accessor replacing the `Root()` concat, and scheme `Filename`
    values pass through the sound resolver's lower-case fold.
  - [x] **AUD0.4** Retire `UE_extract_sounds.py`, the `audio` bundle in `profiles.toml`/`export_all.py`,
    `exports/audio/*` and `exports/sound/**`; update `docs/contracts/seam_map_sound*.md`.
  - [x] **AUD0.5** Tests: `Elysium.Content.CorpusPathsFlip` asserts every audio accessor (sound,
    lip, scene, dlg, scheme) resolves under `CorpusRoot()` and none under `Root()`; a decode test
    runs `FElysiumSoundCache::LoadSoundDecoded` on one corpus MS-ADPCM wav and one mp3 (no test
    touches the decoder today).
  - [x] **AUD0.6** Reference validation on the corpus index: the survey's unresolved-wire
    disposition (50 across 108 maps; one on the tutorial, `tutwareportal01.OnFullyClosed →
    scheme_guns.FadeOut`) and the missing-file dispositions reported from `map-entities`
    `dependencies[].resolved` and the runtime negative cache; no unclassified reference on the
    three playable-path maps.
- [ ] **AUD1 The service** — bake the V2 sound family to sound wave assets once, resolve by
  path, delete the hand-rolled decode path. *Owner call 2026-09-08* (`docs/decisions.md` Audio):
  finishing the custom service would reimplement Unreal's stream cache; the swap is peripheral.
  The corpus is 20 years old and never changes, so the lane is a one-shot bake with no index, no
  contract and no incremental machinery beyond what `bake_lib` already gives every lane. *State
  2026-09-08:* the subsystem renders through `UElysiumPcmSoundWave` over `FElysiumSoundCache`'s
  whole-file dr_wav/dr_mp3 decode (`ElysiumSoundCache.cpp:143`); every consumer already goes
  through `IElysiumAudio` (`ElysiumWorldServices.h:1295`) or the request/handle API, so the swap
  is confined to `Private/Audio/`, `ElysiumMoverSounds.cpp`, `ElysiumWaterAudio.cpp` and the Cog
  window. Corpus facts (probe 2026-09-08, 10,892 units): 5,539 wav with int16 PCM already in the
  GLB payload (MS-ADPCM is not an Unreal import format, so the stage writes PCM wavs); 5,342 mp3
  as raw frames with no Python decoder, imported as `.mp3` (UE 5.8 `SoundFactory.cpp:160`); 11
  zero-byte members bake to nothing; 58 `smpl` loops, 3 with a real intro (`warrens/flow_on.wav`,
  `machines/steam2.wav`, `steam3.wav`); keys with spaces/parentheses go through
  `BakedAssetName` with the extension kept so the 13 wav/mp3 stem pairs stay distinct.
  *Acceptance:* every reference on the three playable-path maps resolves to a baked asset or an
  explicit disposition; a 600 s radio loop plays from the stream cache; `Prefetch` primes and
  `IsReadyForMapActivation` waits on it; map travel cancels every old-map voice and a stale
  handle is a no-op; `dr_wav.h`, `dr_mp3.h`, `FElysiumSoundCache`, `UElysiumPcmSoundWave` are
  gone; `test substrate` and `test policy` green; the mixahead lead re-stamped. *Deps:* AUD0.
  *State 2026-09-08:* AUD1.1/1.2/1.4/1.5 landed on a 17-asset sample set (commit a1475467; loop
  rule and sound-only fold in `docs/vtmb/audio_pipeline.md` §12, contract fixture
  `pipeline/tests/fixtures/sound_asset_paths.json`); live smoke: 17 assets indexed in 14 ms, an mp3
  line and the `flow_on` intro→loop chain play. Left: the full-corpus `bake sounds` (10,883
  assets, stage proven at 0 collisions), AUD1.3's owner measurement, the owner tutorial pass.
  - [x] **AUD1.1** `uv run elysium bake sounds` (`importers/sounds_bake.py` staging PCM wavs and
    original mp3s under `$ELYSIUM_WORK_ROOT/_sounds_stage/`; `pipeline/unreal/import_sounds.py`
    importing them in `AssetImportTask` chunks like the texture lane) to
    `/ElysiumBaked/Sounds/<dirs>/SW_<BakedAssetName(key-with-extension)>`. Loop-end units are
    trimmed at the `smpl` end and get `bLooping`; the three intro units bake as `SW_<name>_intro`
    + `SW_<name>_loop`. Compression `ProjectDefined`, everything else engine default. The stamp,
    prune and `import_report.json` come from `bake_lib` unchanged; no verify pass, no index.
  - [x] **AUD1.2** Runtime on assets: `ResolveSourcePath` computes the package path from the
    folded key and probes the mp3 asset then the wav asset (mp3-first, as today with files);
    `Submit` async-loads through `FStreamableManager` and `RealizeVoice` plays the `USoundWave`
    on the audio component (intro then loop chained on `OnAudioFinishedNative`); `Prefetch` =
    async load + `RetainCompressedAudio`, counted by `PendingPrefetches`; duration read from the
    wave. `ElysiumSoundGroups::Resolve` checks subkey assets by path; whisper sets and water pools
    list their folder through the asset registry. Delete `FElysiumSoundCache`,
    `UElysiumPcmSoundWave`, `ElysiumDrWav.cpp`, `ElysiumDrMp3.cpp`, `Private/ThirdParty/dr_*.h`,
    `Results()`, `SoundDir()`/`SoundFile()` (keep `ElysiumScriptFS`'s `sound/` mount only if a
    shipped script opens one — grep first); Cog "Decode log" tab and the MCP rows become
    loaded/pending/retained.
  - [ ] **AUD1.3** Latency: `SubmitToRenderSeconds` becomes a constant on
    `UElysiumAudioSettings`, measured once by the owner with `elysium.audio_latency`; `Lead()`
    keeps its shape. Record the number in `docs/vtmb/audio_pipeline.md`.
  - [x] **AUD1.4** `import sound` shrinks to the `.lip` mirror; loose `sound/**` audio is pruned;
    `sound/schemes` stays. `research audio_reference_dispositions` resolves against the bake's
    `import_report.json`. Update `docs/contracts/seam_map_sound.md` (owner-reviewed).
  - [x] **AUD1.5** Tests without VtMB data (per 44ac84f6): `FElysiumAudioContractsTest` rewritten
    over the path resolver — fold, mp3-first order, `BakedAssetName` twin, handle generation after
    `RetireMapEpoch`. Live: owner-piloted tutorial pass — door soundgroup, one `ambient_generic`,
    one dialogue line with subtitle, scheme music fade.
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
