# 0017 save-game — complete save/load and import a VtMB run

## Witness
The player issues `elysium.save <slot>` and `elysium.load <slot>` and resumes the saved run,
including its next observable actions, across processes and map travel; a separate offline
importer converts `E:\elysium-work\Vampire-015.sav` (a three-map tutorial save) into an ordinary
Elysium slot the same command loads. Old Elysium saves stay disposable.

## Scope
Run persistence over every domain the tutorial reaches, the session owner that saves and
travels, the one-way retail importer. Owned elsewhere and consumed here: every domain's own
runtime behaviour (0002–0016); the camera save admission's producers — **0012**; the
choreographed-scene and beat admission — **0010**, **0003**.

## Sources
- Oracle: `docs/vtmb/savegame_format.md` (the binary oracle, the native session save entry),
  `docs/vtmb/level_transitions.md`, `docs/vtmb/entity_io.md` (autosave), `docs/vtmb/game_runtime.md`
  (quests, XP).
- Evidence: `save_example.json` beside this spec — a decoded **retail-engine** save, evidence,
  not an import schema; `E:\elysium-work\research\saves\0011-audit\` for the audit output.

## Witness data
- The audit (2026-09-10): input 320,883 bytes, `JSAV` version 117, SHA-256
  `3d478bdc…f199b38d`; current map / label `sp_tutorial_1` / `safe_beat_3`; decoded JSON
  7,288,381 bytes, byte-identical to `save_example.json`, 0 unparsed regions (name/size type
  fallbacks remain). Provenance: `G.Patch_Plus=1`, `G.PP=1`, history `eldritch prodigy`, a
  patched queued function — Unofficial Patch content, release unknown; pin the matching content
  and hashes before conversion; never strip patch globals. Per map: `sp_genesisdevice_1` 67
  HL1 / 47 HL2 rows, 1 pending I/O, 1 physics owner, HL3 departed 22, 63, 66; `sp_theatre` 519 /
  195, 16 pending, 81 physics owners (26 with bodies), 49 AI memories, 25 decals, departed 512–514;
  `sp_tutorial_1` 1,197 / 378, 1 pending, 195 owners (119 with bodies), 3 squads / 54 memories,
  221 decals, no departures, map-local time 201.48271. Nine empty zero-size server rows per map.
- The retail contract: `ETABLE` and the two-pass restore `0x101a2e40` govern identity (save id,
  client index, source ordinal and native id stay distinct); `CSave::WriteTime 0x101a0a80` and
  sentinel restoration `0x101cf2f0`; ordinary Save tests `ObjectCaps` sign at `0x101a37c0`,
  camera caps `0x1006d8f0` clear only transition bit 2; `GetSaveBlockedReason 0x10174f80`;
  `EventQueue` Save `0x100cfee0` → `0x100cfd00`; `PyDataManager` get/set `0x1019b3d0` /
  `0x1019b570`, dict Save/Restore `0x1019adc0` / `0x1019b130`; the AI block `0x1030bfd0` /
  `0x1030c210`; NPC Save/Restore/OnRestore `0x1027bc60` / `0x1027c160` / `0x1027bf50`, Troika
  OnRestore `0x102998c0`, patrol custom ops `0x1028cc00`, pedestrian resolution `0x102f96e0`;
  physics Save `0x10043c70`; `CSoundEnt` Restore `0x101ba840` with sentinel helper
  `0x101b9860`; player Save `0x1016ea00` → `0x10299c60`, `CAI_CsActList` Save `0x102ca130`;
  police `0x103707e0` over globals `0x1093ac3c` / `0x1093aca8` / `0x1093acac` / `0x1093acb0`
  and `0x1092053c`; the entity Save chain `0x100a9f70`.
- The decided shape: Unreal's custom `USaveGame` + `UGameplayStatics` slot system, the existing
  `UElysiumSaveGame` container and Elysium-owned versioned C++ payload; not VtMB's binary layout,
  not JSON as the shipping format, no live UObject pointers. One run owner
  (`UElysiumSessionSubsystem`) with one state store; `FElysiumSaveStorage` owns the native
  envelope and slot I/O; a named manual save replaces its slot, a bare save takes the next free
  `Elysium-NNN`; slots are logical names (1–64 ASCII letters/digits/`_`/`-`, no separators,
  one case policy, Quick/Auto reserved, no silent overwrite of slot 999); operation states
  `capturing` / `writing` / `written` / `failed` and `preparing` / `loading` / `loaded` /
  `failed`, `loaded` only after the restore barrier; same-slot operations ordered; a failure
  leaves the last committed slot usable. A typed transition request replaces destination /
  placement / fresh-map flags; capture runs once in an explicit retirement phase. The importer
  is a one-way content adapter through the same preflight; the runtime never reads `JSAV`,
  `.HL1`–`.HL3`, datamaps or pickles. Each domain slice carries its native capture/apply, its
  semantic diagnostic projection and its typed import adapter; an adapter cannot report support
  for an unused blob or a silently defaulted field; a breaking layout increments the schema and
  raises the reader floor.

## Stories
In build order. A story is done when every behaviour it lists is in the substrate and its
recovery is written in the oracle section it names. Numbers are stable ids cited by other
documents (the former `SG-NN` ledger ids, kept); a split keeps the number and adds a letter.
Each open story carries the retail contract the code must match, the job, what it consumes or
provides, and a size (XS–XL) with the model / effort tier recommended for it. Each depends on
the stories it names.

- [x] **1. A named native slot through the session owner** (SG-01, landed 2026-09-11, schema
  36). Oracle: `savegame_format.md` § "Native session save entry".
- [ ] **2. A named slot loaded through a validated restore barrier** (SG-02; after 1).
  Retail: restore constructs identities before use and performs class reconstruction
  (`0x101a2e40`); no quest award setters run, no `loaded` on a mere `OpenLevel` request.
  Job: `RequestLoad` and `elysium.load <slot>`; envelope/schema/floor, bounded lengths, type
  tags, checksum and complete payload consumption validated into staging before the run is
  replaced; structured leaf results; one final session result GameFlow observes.
  Size: L. Effort: Opus / high.
- [ ] **3. A map preserved through an explicit departure and return** (SG-03; after 2).
  Retail: the engine departure/capture boundary and class cleanup order; `ObjectCaps`
  `0x101a37c0`, camera caps `0x1006d8f0`; landmarks per `level_transitions.md`.
  Job: `trigger_changelevel`, scripted `ChangeMap` and ordinary navigation through one typed
  transition request; capture exactly once in the retirement phase while bodies are valid.
  Size: L. Effort: Opus / high.
- [ ] **4. New game, reload, fresh map and quit isolated** (SG-04; after 3).
  Retail: authored new-game/landmark sequences kept; no map-state reset on ordinary travel.
  Job: every entry routed through Session with `RunGeneration` / `OperationId` checks on
  retiring worlds and async map callbacks.
  Size: M. Effort: Sonnet / high.
- [ ] **5. Clocks and random streams across save and travel** (SG-05; after 4).
  Retail: `CSave::WriteTime 0x101a0a80`, sentinel restoration `0x101cf2f0`; the engine base
  selection and whether dormant deadlines age are UNRECOVERED.
  Job: typed deadline/sentinel translation with the run clock; named RNG streams moved into an
  explicit run context without changing derivation or draw order.
  Size: M. Effort: Opus / high.
- [ ] **6. Slots committed reliably, operations ordered** (SG-06; after 2, 4).
  Job: staged replace with immutable background compression; per-slot serialization;
  metadata/list/delete; Quick/Auto reservations. Durability is a storage modernization.
  Size: M. Effort: Sonnet / high.
- [ ] **7. The retail save audited by a dry-run command** (SG-07; after 1).
  Retail: the nine sections and the source hash are the fixture; recovered datamap types and
  custom Save operations, not zero-unparsed inference.
  Job: `uv run elysium save import-vtmb <file> --dry-run --report <path>` over the hardened
  decoder; normalized import records with field provenance and per-owner adapter context.
  Size: L. Effort: Opus / high.
- [ ] **8. Saves and imports refused against incompatible content** (SG-08; after 5, 7).
  Job: canonical fingerprints for authored definitions and required content stored in snapshots
  and compared on preflight; the import pins the resolved patch-first corpus; quest ordinals and
  schedule identity validated against loaded content.
  Size: M. Effort: Sonnet / high.
- [ ] **9. A complete entity graph, holes included** (SG-09; after 3, 8).
  Retail: the two-pass handler `0x101a2e40` and the class Save/Restore chains; empty
  engine-only rows are not spawn instructions.
  Job: deterministic authored/runtime/player identity, tombstones and holes, leaf apply,
  cross-reference fixup, class restore hooks; construction-time companions suppressed; every
  reference validated before the world is exposed.
  Size: L. Effort: Opus / high.
- [ ] **10. Entity outputs and delayed deliveries resumed** (SG-10; after 5, 9).
  Retail: `EventQueue` Save `0x100cfee0` → `0x100cfd00`; any non-Entity/Python event arm
  recovered before admission.
  Job: output current values, mutable action records, every queue event type with targets,
  activator, variant parameters, Python source and tie order; the queue replaced without
  dispatch; typed output/PEvent adapters.
  Size: L. Effort: Opus / high.
- [ ] **11. Script collections and the morgue** (SG-11; after 7, 9, 10).
  Retail: `PyDataManager` `0x1019b3d0` / `0x1019b570`, dict Save/Restore `0x1019adc0` /
  `0x1019b130`; witnesses `IsIdling` / `AThingOfSomeKind`, `MarkAsDead` / `IsDead`.
  Job: the run-owned script value representation (exact scalar types, lists, mappings,
  case-sensitive keys, deletion, stored zero, observable aliasing); capture as an isolated
  immutable snapshot.
  Size: L. Effort: Opus / high.
- [ ] **12. The Python environment for the loaded run** (SG-12; after 4, 11).
  Retail: the two-dictionary Restore `0x1019b130` and its callers, module initialization, map
  retirement; `G_tut` and the patched scheduled function as consumers.
  Job: the script-host binding/reset/reimport phases per operation; stale wrappers invalidated
  where retail invalidates them.
  Size: M. Effort: Opus / high.
- [ ] **13. Inventory slots, selection and ammunition** (SG-13; after 9, 11).
  Retail: the witness holds slots 0, 21, 28, 29, 30, 76, 77 and active id 1154, reconciled
  against lookup/add/remove/wield, never a dense layout.
  Job: player, NPC and container ownership, position, active/previous/last selections, armor
  relationships, reserves, magazines and keyring records.
  Size: M. Effort: Opus / high.
- [ ] **14. Items carried between maps without duplicates** (SG-14; after 3, 9, 13).
  Retail: transition selection/capability, ownership and HL3 consumers; eligibility is not
  departure (the old maps list a player and two unarmed entities; the tutorial none).
  Job: the carried set and hand-off at a transition; origin tombstones; same-map Save and
  Reload free of departure side effects.
  Size: M. Effort: Opus / high.
- [ ] **15. Player progression without recalculation** (SG-15; after 8, 11, 13).
  Retail: `idxQuestTable` a flat quest index, `idxState` a zero-based completion ordinal.
  Job: every sheet container, entity HP, effects/history/appearance, money, XP accumulators and
  log, quest store and journal fields assigned silently; persistent effect names vs recalculated
  caches kept distinct.
  Size: M. Effort: Sonnet / high.
- [ ] **16. Player placement and movement before the world is active** (SG-16; after 5, 9, 15).
  Retail: the player datamaps and movement/spawn/restore functions; manual load vs landmark
  travel; Source feet coordinates vs native capsule-center placement.
  Job: feet/body/capsule placement, view rotation, stance, velocity, movement mode, grounding and
  modifiers applied inside the barrier; frame-aware import conversion.
  Size: M. Effort: Opus / high.
- [ ] **17. Gameplay animation and its event cursor** (SG-17; after 5, 9, 16).
  Retail: `CBaseAnimating` Save/Restore/OnRestore, `LastEventCheck`, cycle semantics.
  Job: a semantic animation snapshot (model/clip, cycle, rate, looping/held, event cursor,
  completion deadline) captured and sought without live start events, for props, weapons, NPCs
  and scripted sessions.
  Size: M. Effort: Opus / high.
- [ ] **18. Weapon transactions and ranged accuracy** (SG-18; after 13, 15, 17).
  Retail: the weapon/combat/player Save chains and authored usage; unrelated `m_flNextAttack`
  fields not equated.
  Job: next attack, reload, magazine/reserve commit, draw/holster, accuracy/recoil and cooldowns;
  animation-event ownership integrated so a swing cannot commit twice.
  Size: M. Effort: Sonnet / high.
- [ ] **19. Health, damage and regeneration bookkeeping** (SG-19; after 15, 18).
  Job: damage categories/buffers, regeneration clocks and remaining amounts beyond sheet and
  entity HP.
  Size: S. Effort: Sonnet / high.
- [ ] **20. Doors, elevators and switches in motion** (SG-20; after 10, 13, 15, 17).
  Retail: the mover state machine's Save/Restore arms (endpoint waits, reversals, locks, output
  order); the safe-door/switch chain.
  Job: locks, endpoints, phase/progress, activator, waits and latches restored inside the
  barrier without replaying inputs; per-class adapters.
  Consumes: 0009.
  Size: M. Effort: Sonnet / high.
- [ ] **21. Terminal unlocks, attempts and mail** (SG-21; after 13, 15, 20).
  Retail: `tuthack` → `tutsafelock` / `trig_popup_safe`; global-wins-on-entry /
  local-wins-on-exit; `m_SubDirAttempts` and email flags through the custom writer.
  Job: subdirectory/unlock/attempt and local mail state plus the player-owned global email.
  Size: S. Effort: Sonnet / high.
- [ ] **22. Trigger contacts without duplicate entry** (SG-22; after 10, 16, 21).
  Retail: each trigger's touch/filter/think/save semantics and the engine's restore contact
  handling.
  Job: enabled/rearm/count state and containment reconciled after placement; physical overlap
  reconstruction separated from gameplay `StartTouch` / `EndTouch` delivery.
  Size: M. Effort: Opus / high.
- [ ] **23. Discipline effects and their map-bound cleanup** (SG-23; after 10, 11, 15, 18, 19).
  Retail: each active discipline's Save/Restore and transition path; branch priority, effect
  removal, event ownership.
  Job: snapshots, selection/tier/cast counters, effect groups, tracked targets, owned
  expiry/cooldown events rebound after all entities exist; same-map load vs transition cleanup.
  Consumes: 0006.
  Size: M. Effort: Sonnet / high.
- [ ] **24. Feeding and grapple boundaries** (SG-24; after 9, 16, 17, 18, 19).
  Retail: `GetSaveBlockedReason 0x10174f80`'s predicates against the feeding transaction; a
  datamap field is not proof a phase is saveable.
  Job: resumable states persisted at both ends (phase, pulse/deadline, transferred amount,
  locks, latches); excluded states refused with the precise reason.
  Size: M. Effort: Opus / high.
- [ ] **25. A coherent stealth observation state** (SG-25; after 16, 20, 21, 22, 23).
  Job: sample generation, validity, rotation index, derived scalars, next-update timing and
  trigger modifiers; the first observer read and transition reset wired to that generation.
  Consumes: 0002/1.
  Size: S. Effort: Sonnet / high.
- [ ] **26. The criminal and supernatural act registry** (SG-26; after 5, 9, 15, 18, 19).
  Retail: player Save `0x1016ea00` → `0x10299c60`, `CAI_CsActList` Save `0x102ca130`; the full
  act lifecycle and witness ordering.
  Job: the shared act list (offender/owner/ignore handles, location, level, corpse flag,
  expiry), player counters, per-NPC witness state; producers and consumers wired first.
  Consumes: 0002/21a.
  Size: M. Effort: Opus / high.
- [ ] **27. Police response targets and counters** (SG-27; after 26).
  Retail: `0x103707e0` and the five globals with their writers/readers.
  Job: target/time, officer counts, spawn/alert/grace state and the AI-disable flag with an
  explicit owner; no recount by firing Spawn/Remove.
  Size: M. Effort: Opus / high.
- [ ] **28. Sounds gameplay has not consumed** (SG-28; after 5, 9, 23, 25).
  Retail: `CSoundEnt` Save/Restore (`0x101ba840`, `0x101b9860`) and the producers/listeners.
  Job: the bus records, reserved player stimulus, expiry/order and consumer positions as one
  state; no lost pending stimulus, no replayed consumed one.
  Consumes: 0002/10a.
  Size: M. Effort: Opus / high.
- [ ] **29. NPC knowledge and relationships before sensing resumes** (SG-29; after 9, 23, 25,
  26, 27, 28).
  Retail: the NPC datamap and AI block memory (`0x1030bfd0`), OnRestore `0x1027bf50` /
  `0x102998c0`.
  Job: senses, enemy-memory records, delayed conditions, damage windows, relationship rows and
  witness state applied before the first condition pass.
  Consumes: 0002/5, 6a, 16b.
  Size: M. Effort: Sonnet / high.
- [ ] **30. Squads and tactical ownership** (SG-30; after 9, 29).
  Retail: the AI block `0x1030bfd0` / `0x1030c210`, the `CAI_Squad` datamap.
  Job: the shared owner for named squads, membership, focus/memory and reservations, reconciled
  after all members exist.
  Consumes: 0002/17.
  Size: M. Effort: Opus / high.
- [ ] **31. Schedule tasks and waits without restarting** (SG-31; after 5, 17, 29, 30).
  Retail: Save `0x1027bc60`, Restore `0x1027c160`, OnRestore `0x1027bf50` validate name, CRC and
  references.
  Job: schedule identity/signature, task index/status, start times, waits, failure/memory/
  interrupt flags; the retail validity/resume/fallback branch instead of Clear/Start.
  Consumes: 0002's kernel.
  Size: L. Effort: Opus / high.
- [ ] **32. Navigation, patrol and scripted movement ownership** (SG-32; after 16, 30, 31).
  Retail: OnRestore `0x1027bf50` / `0x102998c0`, patrol custom ops `0x1028cc00`, pedestrian
  resolution `0x102f96e0`; what is rebuilt vs retained stated.
  Job: logical goals/path progress, mode/waits, patrol/hunt data, place claims, follower intent,
  pushed scripted orders rebound through the navigation seam.
  Consumes: 0002/10g, 11, 16a, 24; 0003.
  Size: L. Effort: Opus / high.
- [ ] **33. Moving and sleeping rigid props** (SG-33; after 9, 16, 20, 21, 22).
  Retail: physics Save `0x10043c70`, object headers and vphysics save operations; pointers are
  fixup keys.
  Job: the embodiment read/apply seam for rigid bodies (pose, velocities, sleep, settings);
  captured from the live body; applied while simulation is gated.
  Consumes: 0007.
  Size: M. Effort: Opus / high.
- [ ] **34. Hinges and constraints without re-breaking** (SG-34; after 20, 21, 22, 33).
  Retail: `CPhysConstraint` / `CPhysHinge` Save/Restore and each custom constraint record.
  Job: identity/endpoints/frames/limits and active/disabled/broken state; bodies resolved before
  creation; no gameplay `TurnOn` / `Break` on apply.
  Consumes: 0007/8.
  Size: M. Effort: Opus / high.
- [ ] **35. Corpses and ragdolls at the saved death phase** (SG-35; after 17, 31, 33, 34).
  Retail: the death → animation → physical hand-off and its Save/OnRestore arms; `MarkAsDead` is
  not a mechanical death test.
  Job: poses, velocities/sleep and the logical death state rebuilt without restarting `SCHED_DIE`
  or re-awarding consequences.
  Consumes: 0005/4, 0014.
  Size: M. Effort: Opus / high.
- [ ] **36. Runtime decals and their attachments** (SG-36; after 8, 9, 33, 35).
  Retail: client decal restore; `entityIndex` uses the client `saveentityindex`.
  Job: map-owned decal records (material, placement, flags, attachment, frame) rebuilt after
  their owners; the authored baseline reconciled.
  Size: S. Effort: Sonnet / high.
- [ ] **37. Mutable lighting and environmental transitions** (SG-37; after 5, 9, 20, 21, 22, 36).
  Retail: lightstyle writers/readers and the environment Save/OnRestore with its phase clock.
  Job: the `LIGHTSTYLE` table and entity light/fog/weather/fade/particle transitions restored
  before visual publication.
  Size: S. Effort: Sonnet / high.
- [ ] **38. Ambient audio and music** (SG-38; after 5, 10, 28).
  Retail: whether each state is persisted, restarted or re-derived; `OnNormalMusicStart` / `End`
  consumers; the sample proves no saved playback cursor.
  Job: the recovered policy in the entity/audio managers; voices rebuilt; old-map callbacks
  invalidated.
  Consumes: 0011.
  Size: M. Effort: Opus / high.
- [ ] **39. Choreographed scenes and scripted sequences** (SG-39; after 12, 17, 20, 21, 22, 24,
  32, 38).
  Retail: each controller's Save/Restore/OnRestore and admission; a port-only blanket refusal is
  not a recovered exclusion.
  Job: admitted cast, beat/sequence cursor, pending events and body-claim state rebound without
  `Begin` or replay; forbidden states refused precisely.
  Consumes: 0003, 0010.
  Size: L. Effort: Opus / high.
- [ ] **40. Map-local cameras and camera save admission** (SG-40; after 39).
  Retail: `CBaseCineCam` `ObjectCaps` `0x1006d8f0`; active-camera `GetSaveBlockedReason`
  readers apart from map-local persistence.
  Job: cinematic/animated/track camera state, keyframes, progress and view locks; inactive
  controllers restored, active ones only where retail permits.
  Consumes: 0001, 0012.
  Size: M. Effort: Opus / high.
- [ ] **41. Dialogue, sign and terminal modal boundaries** (SG-41; after 21, 24, 40).
  Retail: the unresolved `GetSaveBlockedReason 0x10174f80` relationships to their writers and
  modal states.
  Job: admitted modal states restored with cursor/partner/focus/lock; forbidden ones refused
  with teardown.
  Consumes: 0004.
  Size: M. Effort: Opus / high.
- [ ] **42. Save admission and deferred autosaves** (SG-42; after 6, 23, 24, 26, 27, 39, 40, 41).
  Retail: `GetSaveBlockedReason 0x10174f80` and the engine autosave/request/unblock handlers
  (`entity_io.md`).
  Job: the ordered admission chain over real substrate inputs; manual/quick/trigger autosave
  through one service; eligibility, pending latch, retry on unblock, one-shot triggers, ring
  publication after results.
  Size: M. Effort: Opus / high.
- [ ] **43. Semantic save and import diagnostics** (SG-43; after 10–42).
  Retail: the next-transition contract is primary; Troika OnRestore's deliberate field changes
  distinguished from missing data.
  Job: canonical typed Describe/diff over every owner's projection, with mapping classification,
  content/schema ids and operation outcomes.
  Size: M. Effort: Sonnet / medium.
- [ ] **44. The retail save's full roster against native maps** (SG-44; after 7, 8, 9, 14, 43).
  Retail: `ETABLE` and the two-pass restore govern identity; the two `rat_2` records, picked-up
  id 213 and the empty viewmodel rows are the ambiguity witnesses.
  Job: per-map source-definition/native-id matching and roster reconstruction for all three maps,
  corroborated by class/model/origin/output signatures.
  Size: L. Effort: Opus / high.
- [ ] **45. The retail save as a native slot** (SG-45; after 6, 12, 42, 43, 44).
  Job: non-dry-run `import-vtmb` building one staged `FElysiumRunSnapshot` through the native
  preflight and `FElysiumSaveStorage` from a commandlet that runs no gameplay; exact binary
  numeric values; no quests derived from flags.
  Size: M. Effort: Sonnet / high.
- [ ] **46. The complete run across processes, travel and import** (SG-46; after 1–45).
  Job: `uv run elysium save verify <case>` over the run/automation/input services driving real
  worlds and separately launched processes, matching next observable events against the
  recovered restore rules; there is no Play tier to reuse (the 0000 play-tier spec was retired
  2026-09-12).
  Size: L. Effort: Opus / high.

## Seams
- Provides: run persistence to every spec's witness; the importer.
- Consumes: every domain's own runtime (0002–0016) as named per story.
- Open recoveries: the engine time-base selection and dormant-deadline ageing (5); the
  non-Entity/Python event arm (10); the module lifecycle (12); each domain's Save/Restore arms
  as named.
