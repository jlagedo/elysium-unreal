# 0019 npc-kernel-rework — The kernel as data plus a class tree: extract what Troika typed, port what the bytecode observes, delete what nothing can see

## Witness
The tutorial's stealth lessons, unchanged: the same `sp_tutorial_1` witness tests green on both
sides of every story here. And one new witness: `SCHED_TROIKA_CHASE_ENEMY_FAILED` runs from
retail's own text — stop, wait 0.2 s, fail to `STANDOFF`, tolerance 24, find cover from the
enemy, relaxed anims, run the path, remember `INCOVER`, face the enemy, idle, wait 1 s — where
today the port runs an invented two-task program (`ElysiumNpcCombatSchedules.cpp:293`).

## Scope
No new behaviour. Every story either loads what the port retyped, or removes what nothing can
observe. Five pieces: the strict verdict pass that separates rule from mechanism and dead; the
data seams (datamap bindings, schedule texts with their id spaces and flag tables, the tunables
table); the class tree; the deletions and the mechanism seams; the reach cut that scopes 0002.

Owned elsewhere: the world's AI objects — **0018**; the mind and its programs' task bodies —
**0002**. This spec finishes and then closes: once story 7 lands, nothing remains here.

The rule every story applies, from `docs/vision.md` § "The three adjudication tests", restated
for the kernel on 2026-09-15 after the session that found the DRM:

> A retail function is ported only if something can observe it: an authored keyfield, a
> schedule text, a script-visible name, an entity output, a save field, a player-visible timing,
> or the witness. What Troika typed as a string or a table is **data** — extracted from the
> install, loaded, never retyped. What an observable names is a **rule** — ported verbatim with
> retail's constants and order. What the world merely needs is a **mechanism** — Unreal's
> service behind a seam, retail thresholds kept as tunables. What nothing observes is **dead** —
> one verdict row, no body, no test.

What the 29-series method got wrong, so it is not repeated: "port everything in the closure,
bottom-up by call layer" has no term for that question, so an automated port answered yes to
every function it reached — the `CAI_Motor` ground step, the physics tick, twenty-one debug
overlay bodies with no output device, and Macrovision's `CSecureType` integer scrambler, all
verdicted `rule` and all ported with tests. Bottom-up by layer stays the right *order*; the
closure was the wrong *scope*.

## Sources
- Ledger: `docs/vtmb/npc-kernel/` — `order.md` (the 30 layers), `functions.md`, `fields.md`,
  `classes.md`, `layout.md`, `signatures.md`, `coverage.md`, `checklist-*.md`;
  `research/tooling/ghidra/driver/kernel_verdicts.tsv` (the overlay), `merge_verdicts.py`,
  `kernel_ledger.py`, `kernel_shape.py`; `research/tooling/gen_kernel_shape.py`.
- Oracle: `docs/vtmb/npc-ai/shape.md` (§ "The tables", § "The layout the datamaps do not
  save", § "How a species body reaches the body it replaces"); `schedule-kernel.md`
  (§ "Schedules and tasks: the behavior program", § "The schedule host and the task surface,
  walked" — the Brujah id-space walk); `conditions-and-states.md` (§ "`DELAY_INTERRUPTS`,
  decoded" — the parser and the `CAI_Schedule` layout); `senses.md` (the `NPCFlag:` and
  `MiscFlag:` parsers and the name↔mask tables).
- Replays and probes: `$ELYSIUM_WORK_ROOT/research/ghidra/types/datamap_records-vampire.dll.json`
  (every class's `typedescription_t` rows); `research/tooling/probes/native_schedule_survey.py`
  (the schedule-text extractor).
- Contracts: `docs/contracts/seam_map_unit_contract.md`, `seam_map_vdata.md` (the precedent
  for a text table as a GLB unit).

## Witness data
Read on 2026-09-15 from the tree and the pinned `vampire.dll`.

| Measure | Value |
|---|---:|
| Verdict rows: `rule` / `mechanism` / `present` / `dead` | 2,205 / 175 / 161 / 0 |
| Kernel source (`ElysiumNpcKernel*.cpp/.inl`) | 87,532 lines |
| …of which generated census / slot stubs | 9,008 / 6,009 |
| …Debug families / Motor families | ≈3,500 / ≈4,000 |
| `CHOSEN, NOT RECOVERED` admissions in the port | 59 (23 in the schedule files) |
| Retail schedules / task invocations / task identities | 691 / 4,139 / 441 |
| Port programs / task identities, hand-typed | 19 / 58 |
| NPC-chain datamap rows: `KEY` / `INPUT` / `OUTPUT` / `SAVE` | 275 / 80 / 28 / 727 |
| Port bindings, hand-typed: class fields / inputs / serialize blocks | 48 / 22 / 9 |

Misverdicted `rule` rows that anchor story 1, with their port targets: the physics tick
`0x100b4f30` → `VPhysicsUpdate`; the ground step `0x102e1760` → `MotorMoveGroundStep`; the
navigator move `0x102efaa0` → `NavigatorMoveNormal`; the debug ring appender `0x1027ef20` →
`AppendDebugLogLine`; the criminal-witness writer `0x1028ea60` → `RecordCriminalWitness`, whose
`CSecureType` half (`0x1042fde0` / `0x1042fe90`, twelve constants copied into
`ElysiumNpcKernelSenses.cpp:97` and `ElysiumNpcKernelCombat10.cpp:82` with a test that the two
copies agree) is SafeDisc copy protection over a plain integer. The Bosses family already carries
the ManBat and Hengeyokai secure ints as plain integers (`ElysiumNpcKernelBosses.inl:29`); the
Senses and Combat10 families did not.

Retail's own text for the chase and its failure route, read at `0x5f0732` and `0x5ef06a` of the
image, is the witness above; the port's two programs at `ElysiumNpcCombatSchedules.cpp:255-300`
drop `FORCE_RELAXED_ANIMS`, two interrupts, and the whole failure program.

## Stories
In build order. A story is done when the code it names is gone or generated, the witness is
green, and `coverage.md` shows the change.

- [x] **1. The strict verdict pass.**
  Retail: none — a judgment over the 2,545 overlay rows, bands 0–29.
  Rule: a `rule` row must name its observable in `evidence` — the keyfield, schedule text,
  script name, output, save field, timing or witness test that would notice its absence — or
  it becomes `mechanism` (the Unreal service or seam named in `target`) or `dead` (`target -`).
  `present` rows are re-read the same way. The pass is judgment per row, not a re-reading of
  bodies: the evidence column already says what each body does.
  Job: the pass, folded through `merge_verdicts --band … --from` so a corrected row lands on
  top of the batch it corrects; `coverage.md` re-rendered; the outputs are **the delete list**
  (every `dead` row's port target and its tests) and **the seam list** (every `mechanism` row's
  port target and the Unreal service that replaces it). Expected `dead`: the 21 Debug bodies
  and the ring (`0x1027ef20`, `0x1027efb0`), the three debug stamps, the salt bytes, the
  scrambler pair and the secure ints. Expected `mechanism`: the `CAI_Motor` / `CAI_Navigator`
  bodies (`0x102e14a0`, `0x102e1560`, `0x102e1760`, `0x102efaa0`, `0x102efd50`), the physics
  tick, network change-state, the trace and push-out bodies, the sector partition.
  Provides: the lists every story below consumes; the re-verdicted 19–29 checklist to 8.
  **Inputs the 2026-09-21 RE pass adds (`docs/specs/RE-BACKLOG.md`).** Rows that are `dead` by
  CONTENT, each with its proof in the oracle: `COND_KNOCKBACK 0x28` has no producer anywhere, so
  `GetSchedule`'s two knockback arms and the idle programs' listing of it are unreachable; goal
  type 5 has no issuer; hint type 800 is authored by nothing, so the patrol lookup's `|| 0x320`
  never matches; `TASK_CREATE_HUNT_PATROL_LIST 0xae` has no issuer; `FINISH_CLIMB 0xfc` cannot
  run (no shipped link climbs); the generic `.sch` file loader `0x1030f220` has no caller and no
  file; `m_flRadius` on `aiscripted_schedule` and the `nosferatu_tolerrant` key have no reader.
  And one correction to the ledger's vocabulary: in the goal record the `0x13` several rows call a
  "flag" is the ACTIVITY word `+0x14`.
  Size: M. Effort: Fable / high.
  **Landed 2026-09-21.** All 2,544 overlay rows re-judged (the three `unsettled` rows stand as
  they were): fifteen judges over address-ordered packs, then a lead pass that normalised every
  slot the judges split on and ran the two checks below. Before → after: `rule` 2,205 → 1,354,
  `present` 161 → 150, `mechanism` 175 → 380, `dead` 0 → 657. Band 19–29, which story 8 lands
  under: 380 `rule`, 4 `present`, 13 `mechanism`, 71 `dead` of 468.
  *The record:* every row's evidence now leads with `[0019/1 obs=<kind>: …]`, `[0019/1 seam=…]` or
  `[0019/1 dead=…]` (`; was <verdict>` where the word changed), and
  `uv run elysium research kernel_lists --check` holds every row to it — a `rule` with no named
  observable fails, and so does a `via:` chain that leans on a row which is not itself a rule. Of
  the 1,504 `rule` / `present` rows 716 are `via` a rule that uses them, 342 name a schedule text,
  284 a player-visible timing or state, 64 a keyfield, 32 an output, 22 a dlg gate, 20 a rulebook
  row, 12 an input, 11 a script name, 1 a save field.
  *The outputs:* `docs/vtmb/npc-kernel/delete-list.md` (657 rows; 567 still name a port target, 300
  of those with a hand-written port site, the rest generated stubs and registry values) and
  `seam-list.md` (380 rows; 253 with a port body, 127 service-only), both rendered by
  `kernel_lists` from the overlay. The target column KEEPS the port target through the pass —
  a `dead` or `mechanism` row that still names a port body is one story 6 owes, and 6 writes `-`
  when the body is gone — and `gen_kernel_shape` emits a `dead` row's `hand:` / `default:` spelling
  as before, so the pass changes no runtime behaviour: the census C++ differs in verdict words
  only, the build is green and `Elysium.Substrate.NpcKernelS*` runs 188 of 188.
  *Where the 657 came from:* 252 on **classes nothing can instantiate** — a finding the pass made,
  now in `population.md` § "NPC classes with no instance": twenty classnames (`npc_crow`,
  `npc_generic*`, `npc_sabbat`, `npc_bullseye`, `monster_generic`, `npc_VTest`,
  `npc_VSheriffSwarm`, `npc_VBatSwarm`, `npc_VStalker`, `npc_VCombatman`, `npc_VMoleman`, six clan
  classes, `npc_TestBaseHumanoid` with the whole `CAI_BaseHumanoid` line, `scripted_target`) are in
  no map, maker, template or script of the install and `vampire.dll` names each only at its factory;
  172 debug and diagnostics (the overlays, the ring, the trace formatters, the stamps, slots 451 /
  546 / 409 name lookups); 105 slots with no dispatch site (slots 23, 496, 497, 501, 502, 505, 506,
  554, 556, the think-stamp getters); 44 empty or constant bodies nothing reads; 42 slot 452
  `LoadedSchedules` (story 3's load error stands in for it); 19 `CAI_StandoffBehavior` /
  `CAI_StandoffGoal` bodies — `ai_goal_standoff` is authored by nothing and slot 455 answers 0 on
  every class, which halves 0018/15; 12 overrides whose only content is a debug stamp around the
  body the class would inherit anyway. The `CSecureType` half of `RecordCriminalWitness` and of
  the man-bat `OverrideMove` is named dead inside rows that stay `rule`.
  *Expected and found:* every anchor above landed where the story said — `0x1027ef20`,
  `0x1027efb0`, `0x1028d990`, `0x10292500`, `0x102779a0`, `0x10277d90` `dead`; `0x102e14a0`,
  `0x102e1760`, `0x102efaa0`, `0x100b4f30` `mechanism` with their result codes and step factors
  named as kept. **Five anchors have no overlay row** because the ledger does not count them core
  (no family slot, no NPC-range offset) although the port cites them, so story 6 takes them from
  here rather than from the lists: `0x102e1560` and `0x102efd50` (`mechanism`,
  `ElysiumNpcKernelMotor10.cpp`), the scrambler pair `0x1042fde0` / `0x1042fe90` (`dead`,
  `ElysiumNpcKernelSenses.cpp:97`, `ElysiumNpcKernelCombat10.cpp:82`,
  `ElysiumNpcKernelLifecycle19.cpp:251`), and the `.sch` loader `0x1030f220` (`dead`, never
  ported). Of the dead-by-content inputs, the climb is whole rows (`CAI_Motor` slots 3–5 and
  `MoveClimb 0x102eebc0`, `dead`); the rest are ARMS inside bodies that stay `rule`. Two are named
  dead in their row's tag — `COND_KNOCKBACK`'s two arms in `PreSelectSchedule 0x102ae920` and the
  `TASK 0xae` arm in `0x10375f50`; goal type 5 and the hint-type-800 `|| 0x320` are not yet named
  in a tag and ride with the stories that port those bodies (0018/5, 0002/10g); `m_flRadius` and
  `nosferatu_tolerrant` are keys with no reader, which story 2's bindings leave unbound. Slot 580 (the class's id space) stays `rule` on every
  class: story 3 keys its units by it. The six rows that called the goal record's `0x13` a flag
  carry the correction in their tag.

- [x] **2. The datamap bindings, generated.**
  Retail: every class's `DATADESC` — `DEFINE_KEYFIELD` (name, member, type: fully data),
  `DEFINE_INPUT` (a field-setting input: fully data), `DEFINE_INPUTFUNC` (name and argument
  type data, the handler code), `DEFINE_OUTPUT` (fully data), `DEFINE_FIELD` with `SAVE`, and
  `FUNCTIONTABLE` — replayed as `datamap_records-vampire.dll.json`. `ReadKeyField`
  (`0x100acab0`) and `AcceptInput` (`0x100abc90`) walk that table; nothing knows a field by name.
  Gap: `ElysiumNpcClasses.cpp` hand-types 48 `ElysiumAddClassField` rows and 22 `D.Input`
  rows against 275 keys and 80 inputs; nine `Serialize*Block` helpers walk a save format the
  project calls disposable; 0002's 12a lists seven keys never parsed.
  Job: extend `gen_kernel_shape.py` (or a sibling under `research/tooling/`) to emit, for every
  entity class the port stands, the `KEY` → member bindings through the shape census's
  retail-name → port-member map, the `OUTPUT` declarations, the `INPUT` registry (field-form
  inputs bound; `INPUTFUNC` rows emitted as declarations the port fills by hand — 80 on the
  chain, most field-form), and the `SAVE` walk over bound members; the hand rows and the nine
  blocks deleted; a generator check that every retail `KEY` on a stood class has a bound member
  and every bound `SAVE` word is walked. Member names stay the port's: the census is the
  bridge, so no member is renamed to `m_` anything, and no offset, `FIELD_*` type or datamap
  walker is reproduced. The strings cross the seam; the reflection does not.
  **Actor boundary (2026-09-17):** 0018's placed infrastructure uses native `AActor` classes
  and normal `UCLASS` / `UPROPERTY` declarations, with `USTRUCT` for serialized records.
  This story supplies the generated mapping from retail external names/types to those typed
  members; it does not create a second reflection system. The existing generated bindings
  target plain substrate objects, so binding actor properties still requires an explicit
  adapter and coverage check. Actor declarations may be built first; generated bindings and
  runtime adoption complete together for each stood class. AIN nodes/edges are decoded graph
  data, not datamap keyfields: their bake into places and native nav links is owned by 0018/4 and 0018/7,
  and do not require generated actor classes for every graph record.
  **Shared runtime access:** preserve the existing entity lookup/input/field bridge so map I/O,
  Python and NPC queries read and mutate one live state initialized from the baked actor.
  Unreal actor labels/tags, retail `targetname`, exact-case patrol `Group` and network indices
  are separate namespaces. Preserve field write permissions, synchronous Python input calls
  and queued output ordering; `UPROPERTY(EditAnywhere)` alone grants no retail script API.
  Coverage must include an input/field mutation observed by the actual registry query, not
  just successful deserialization. Inputs with side effects still need their recovered handlers.
  Provides: keyfields to 0018 story 2 and 16; the parse half of 0002's 12a. Consumes: 1.
  Size: M. Effort: Opus / high.
  **Pass A landed (2026-09-16, GLM-5.3-Flash from a brief, Fable review):**
  `research/tooling/gen_kernel_bindings.py` reads the replay's `CAI_BaseNPC` and
  `CAI_BaseNPCTroika` rows, joins them to the shape map by offset and emits
  `Substrate/ElysiumNpcKernelBindings.h/.cpp` — 35 bound keyfields, 24 output names, 34
  handler-form inputs listed, 5 unbound with a reason (three `FElysiumNpcScheduleHost` words
  the entity-only binding API cannot reach, `default_disposition` on a chain row,
  `interesting_place_groups` hand-owned by its parse-on-write accessor). `BuildNpcClass` calls
  `AddNpcFields` first; 21 hand rows retired, 4 kept that pass A cannot reach: `stattemplate`,
  `floatfreq`, `cantdropweapons` are `CBaseCombatCharacter` keys the shape map does not cover,
  and `m_iEnemySightings` is a `SAVE`-only row, a save-walk binding under its retail name. Seventeen retail keys
  are bound for the first time, among them 12a's `stay_entrenched`, `percent_occluded_*`,
  `squadname`, `follower_*`. `--check` verifies the emission; the test
  `Elysium.Substrate.NpcKernelBindings.Counts` holds the counts.
  **Pass B landed (2026-09-21).** The whole NPC entity chain is generated. `ReadKeyField`
  (`0x100acab0`) walks `CAI_BaseNPCTroika` up to `CBaseEntity` and the port's registry carries the
  same walk through `FElysiumClassDesc::BaseName`, so `CBaseEntity`, `CBaseToggle`,
  `CBaseAnimating` and `CBaseCombatCharacter` each gained an `Add…Fields` registered on the
  matching chain node (`CBaseFlex` and `CBaseAnimatingOverlay` name no external and got none):
  **27 + 0 + 1 + 149 + 38 keyed rows bound, 37 recorded gaps**, each naming the retail word and
  what this port does instead. *The "200 `CBaseEntity`/`CBaseCombatCharacter` keys" this story
  asked for were 200 replay ROWS, not 200 gaps:* 148 are the character sheet, which
  `AddSheetFields` already registered at runtime, and 25 more were the base node's hand rows, so
  the real gap was **42**, of which 5 now bind and 37 carry a reason.
  *Three new `FElysiumEntity` words:* `DialogName` (`m_iDialog +0x128`, replacing a live read of
  `Def->Keys` that no runtime write could reach), `AuthoredSpeed` (`m_flSpeed +0x164`) and
  `bStartHidden` (`m_bStartHidden +0xe0`, now the one word both the datamap row and the born-hidden
  spawn rule read).
  *The sheet is generated too*, as `ElysiumAddSheetField` rows against the port's compiled slot
  table, which put retail's own spellings in charge and corrected two: `gender` / `base_gender_`
  (the underscore is on the base row alone — the pair was carried inverted) and
  `base_active_active_active_dominate` (Troika's typo, and the only spelling that addresses
  Active_Disciplines slot 6's base). Both now sit in `FElysiumSheetSlot::BaseDatamap`, and
  `docs/vtmb/game_runtime.md` § "How a trait is addressed" records them.
  *The component-struct binding surface* is `ElysiumAddClassFieldVia<TClass>(D, name, resolver)` —
  a captureless generic lambda returning a reference, so one form serves a component member, a
  struct nested in one and an array element alike, and the path is compiled. `ElysiumAddClassField`
  delegates to it, and the marshalling chain widened to every integral type and every enum, because
  retail's `FIELD_INTEGER` lands on the port's `uint32` bit words and `uint8` enums.
  *The SAVE walk* is `AddNpcSaveFields`: **199 of the 216** `SAVE`-only rows that have a shape-map
  member, registered under their RETAIL MEMBER NAME with `EElysiumField::Save` alone. That is
  retail's own reading — `ReadKeyField` gates on `KEY|OUTPUT`, `AcceptInput` on `INPUT`, and a row
  with no external answers neither, so it is persistence and nothing else
  (`docs/vtmb/python_bridge.md` § "The three gates, read together"). The 17 that do not generate
  each say why: 13 are `FIELD_EMBEDDED` rows whose port member carries its own typed `Serialize`
  (which is the same shape as retail's nested `datamap_t`), 2 are `FIELD_CLASSPTR`, and 2 are the
  blink pair, a word of the RESOLVED disposition row this port re-derives.
  **The nine `Serialize*Block` helpers did NOT go, and the reason is a finding.** Measured against
  the 239 port words the generated walk now reaches, the nine blocks duplicate **three** of them —
  `m_iEnemySightings`, `m_CurrStance`, `m_flStanceTime`, and those were hand rows in
  `BuildNpcClass` rather than block writes. They are deleted. What the blocks actually carry is
  three things the walk cannot: port state with no retail datamap row at all (the patrol/ambient
  bookkeeping 0018's places need), `PRIVATE` members the shape map records as strings because
  `sizeof` cannot reach them (`m_NPCState`, `m_IdealNPCState`, `m_bDisableAI` and 11 more), and
  restore LOGIC that is not persistence (re-`Start`ing the saved program, re-claiming an ambient
  spot, rebasing handles, the witness clamps). **The real double-persistence is one layer down:**
  `FElysiumNpcWitness::Serialize`, `FElysiumNpcScheduleHost::Serialize` and their kin write ~104
  of the words the registry now carries. Splitting those seven component serializers against the
  generated walk — and moving their load-side validation to a post-restore hook — is the remaining
  work, and it is a different job from "delete the nine blocks": **pass C.**
  **Pass C landed (2026-09-21): the split, and the hook that was already there.**
  *The measurement first, and it corrects pass B's estimate twice.* The overlap is **62 words, not
  ~104, and it is in three components rather than seven** — `FElysiumNpcMemory` 27,
  `FElysiumNpcScheduleHost` 23, `FElysiumNpcWitness` 12, `FElysiumNpcSenses` 0. The 104 pass B
  quoted is the count of all component-member rows in `AddNpcSaveFields`, not of words a
  serializer also wrote. `FElysiumNpcFlags`, `FElysiumRelationships`, `FElysiumNpcEnemyMemory` and
  `FElysiumDisciplineState` overlap the walk by **nothing**: they answer retail `FIELD_EMBEDDED`
  rows, where retail's own datamap points at a second `datamap_t` and recurses, so a nested
  serializer there is the port's spelling of the recursion rather than a duplicate — and they stay.
  The measurement is reproducible: `uv run elysium research save_walk_overlap` crosses the
  generated walk's resolver paths against each serializer's `Ar <<` statements and now answers 0.
  `FElysiumNpcWitness::Serialize` had 12 duplicated words and **no** port-only word, so it is gone
  entirely; the other three keep only what the walk cannot reach.
  *The post-restore hook did not have to be built — it had to be WIRED.* The whole of retail's slot
  130 was already ported at `ElysiumNpcKernelLifecycle19.cpp:1059-1233` — `FElysiumNpc::OnRestore`
  → `SpeciesOnRestore` → `TroikaOnRestore` → `BaseOnRestore`, walked arm by arm against
  `0x102998c0` and `0x1027bf50`, its unrecoverable inputs already marked `SEAM` — and **every
  caller was a test**. The port's persistence had never once run it. Pass C adds
  `FElysiumEntity::OnPostRestore(World)`, called by `ApplyEntityRecord` after the leaf blob and
  after `OnDormancyChanged` but before the saved think cadence is restamped, and `FElysiumNpc`'s
  override runs `OnRestore(true)` and then the port-only re-derivations (the four `Rebase`s, the
  senses tuning, the discipline re-install, the patrol/ambient resolve, the mind validation,
  `RestoreDeathBodyState`). The schedule re-find by NAME and task-array CRC32 is therefore live for
  the first time.
  *Retail's one hand block is now the leaf's one hand block.* `CAI_BaseNPC::Save 0x1027bc60` writes
  `AIExtendedSaveHeader_t` — version, the three-bit liveness word, a 128-byte schedule name, the
  task-array CRC32 — and defers everything else to the datamap walk. `BuildExtendedSaveHeader` is
  split out of `BaseSave` so the real save path writes the same block, and `BaseOnRestore` reads it.
  The nine `Serialize*Block` helpers are four: the header, patrol/ambient, the maker relationship
  and the mind's two raw words. Every one of the leaf's version gates is deleted — all sat below
  `MinSupported`, and `ElysiumSaveTests.cpp` already asserted it — and the schema is
  `NpcSaveWalkSplit` (39), with the floor raised to it.
  *Two defects, one found and fixed, one named.* **Fixed:** `ApplyEntityRecord` calls
  `OnDormancyChanged()` after the field walk, and the NPC's hook took its "waking, not going
  dormant" arm and ran `ResetThinkTimers` — so retail's four `m_flNext*Think` rows (`+0x6244`..)
  were thrown away on *every* load and every restored NPC thought immediately, which retail's
  `OnRestore` never does. A restore is not a `ScriptUnhide`; the arm now stands down while
  `FElysiumEntityWorld::IsApplyingSnapshot()` is true. **Named, not fixed:** this port restarts the
  restored program where retail resumes it (retail's datamap also carries the task cursor `+0x5c50`;
  this port does not save it, because a task holds a clip, a pending move or a deadline). A restart
  runs retail's own slot 435 `TroikaOnScheduleChange 0x102a0940`, which releases twelve of the
  walk's rows on the way in — `m_flGoalTolerance`, the two interrupt distances, `m_flInterruptTime`,
  `m_hMoveTargetEnt`, `m_flDesiredMoveYaw`, `m_bWaitFinishedSet`, `m_flMoveWaitFinished`,
  `m_bShouldMove`, the opening-door pair and `m_bDidMaintainSchedule`. They are per-run task state
  the restarted program's own first tasks set again; the divergence is stated at
  `FElysiumNpc::RestartRestoredSchedule` and each row is listed with its retail address in the test.
  *One generated row corrected:* `m_bConditionsGathered` (+0x5ca4, retail `bool`) was bound to
  `FElysiumNpcCognition::GatheredAt`, a `double`, so it marshalled a timestamp under a bool's name
  and was then stomped on load. It is a recorded `SAVE_UNBOUND` gap now — the port carries that fact
  as the pass EDGE, which the hook re-stamps — and the walk is 198 rows.
  *The net:* `Elysium.Substrate.NpcKernelBindings.SaveRoundTrip`, which nothing equivalent existed
  for. It stamps all 218 registered `m_*` rows with values no default produces, freezes, applies
  onto a world that knows nothing, and requires each to read back — with 23 named exceptions, each
  carrying the reason and the retail address that overwrites it. Six component save suites were
  driving `Npc->Serialize(Ar)` directly and so were testing the leaf blob alone; they now go through
  `Freeze`/`ApplySnapshot` via `ElysiumRoundTripSnapshot`, which is the only call that exercises
  what a real save does. `Elysium.Substrate` runs 458 of 458.
  **Still open:** the non-NPC classes as 0018 stands them. The 19 `mechanism` rows whose bodies are
  in `ElysiumNpcKernelSaveRestore10.cpp` — retail's slot 126/127 `Save`/`Restore` twins, which this
  port's persistence still does not call — stay with story 6, as story 1 assigned them; pass C only
  shares their CRC helpers and their header struct.

- [x] **3. The schedule seam: texts, id spaces, flag tables.**
  Retail: the 691 schedule descriptions are null-terminated ASCII in `.rdata`, one per
  schedule — `Schedule <name> Tasks <TASK_* arg>… Interrupts <COND_*>… [Flags …]`. Each class's
  `InitCustomSchedules` (the worked example `CNPC_VBrujah` `0x10367a40`) calls
  `CAI_LocalIdSpace::Init` (`0x102ea0e0`) on its schedule, task and condition spaces with the
  global namespaces and the base class's spaces as parents, registers its names against its
  numbers through `0x102ea130`, then runs the parser `0x1030d850` over its texts, breaking on
  the first failure into the byte slot 452 `LoadedSchedules` returns. The parser resolves task
  names and `Schedule:` / `Task:` operands through the global spaces and then the class's local
  ones; **`COND_*` through the GLOBAL condition space only — the masks are in global ordinals**;
  and an operand through one of **seventeen prefixes** (`Activity`, `Task`, `Schedule`, `State`,
  `Memory`, `Path`, `Goal`, `HintFlags`, `NPCFlag`, `MiscFlag`, `Model`, `SOUND`, `EXPRESSION`,
  `STO`, `DIST`, `MXTPHASE`, `TOMODE`), the words `TRUE` / `ON` / `FALSE` / `OFF`, or `_atof`;
  `Flags` through the two-token table `0x1030d7e0` (`NONE`, `DELAY_INTERRUPTS`). Walked whole
  2026-09-21: `schedule-kernel.md` § "The schedule-text parser `0x1030d850`, walked" — the
  grammar, every resolver table, the failure table. (`0x1033cb00` / `0x1033cb50` are NOT the
  parser's: `Memory:` is `0x1030c800`, `MiscFlag:` is `0x1030d390` and stores an index.) It fills a `CAI_Schedule`: inverted interrupt mask
  `+0x00`, flags `+0x18`, id `+0x1c`, task array and count `+0x20` / `+0x24` (cap 64), interrupt
  mask `+0x28`, name `+0x40`. Ids are per class: `0x156` names six different schedules in six
  tables. The fourth space, squad slots, sits `0x18` past the third.
  Gap: 19 programs hand-typed from prose in `ElysiumNpcCombatSchedules.cpp`,
  `ElysiumSchedule.cpp`, `ElysiumFeedSchedules.cpp`, `ElysiumAiScriptedSchedule.cpp`, with
  `FScheduleMeta` numbers decoded by hand (two at 0), `EElysiumScheduleId` (29 entries) and
  invented masks; the id spaces stubbed at the empty range so every translation answers -1.
  Job, pipeline: a V2 unit per owning class, `vtmb:ai-schedule:<class>` →
  `ai-schedules/<class>.glb`, under the unit contract like `seam_map_vdata.md`, carrying the
  class's verbatim texts as rows, its schedule / task / condition / squad-slot registrations
  (name, number) recovered from its init body's call sites, and — once, on the Troika unit —
  the `NPCFlag:`, `MiscFlag:` and `MEMORY:` name tables; the extractor lifted from
  `native_schedule_survey.py`; the owner of each text recovered at export from which init body
  references it; a contract `docs/contracts/seam_map_ai_schedule.md`; and an import lane,
  `uv run elysium import ai-schedules`, deploying each unit's texts and tables as loose files
  under `Content/ElysiumCorpus/ai/schedules/<class>/` the way the vdata lane deploys
  `vdata/**` — the runtime reads the deployed corpus, never the GLB (0018 § Scope). Decided
  2026-09-15 by the owner: GLB unit as the export-stage product, deployed to the corpus.
  Job, runtime: a port of the parser and its argument grammar (the seventeen prefixes, the four
  boolean words, the bare number, `!`-inverted interrupts, `Flags` — the oracle section is the
  contract), `CAI_LocalIdSpace` with parents,
  loading from the deployed corpus (`CorpusRoot()`) at class init in the tree's order, malformed text a load
  error as retail's is;
  `EElysiumTask` becomes a name table the parser resolves; the runner keeps "unknown task fails
  by name" — that failure count over the loaded programs is the port's task-coverage meter.
  Job, deletions: the 19 programs, `FScheduleMeta`, `EElysiumScheduleId` and its 20 non-test
  users (they ask the class's space by name), the program-content tests
  (`ElysiumScheduleTests.cpp` and the schedule halves of `ElysiumNpcKernelScheduleTests.cpp`,
  `ElysiumNpcCombatTests.cpp`); the interpreter (`Register`, `Start`, `Install`, `Tick`,
  `MaintainSchedule`) stays.
  Provides: every program to 0002, loaded; the coverage meter. Consumes: 1, 2.
  Oracle: `schedule-kernel.md` § "The schedule host and the task surface, walked",
  `conditions-and-states.md` § "`DELAY_INTERRUPTS`, decoded". Unrecovered: nothing. Both reads
  this story owed landed 2026-09-21 in `schedule-kernel.md`: § "The schedule-text parser
  `0x1030d850`, walked" (the grammar) and § "The schedule owners and their registrations" (the
  call-site recipe and the per-owner table). **Three numbers in this story's text change with
  them.** There are not 20 owners but **56 init bodies, 48 of them feeding texts** (20 is the
  count of classes registering class-local tasks) — so "a V2 unit per owning class" is 48 units,
  or 56 if the empty eight are kept for their parent links; the base class feeds **64** texts
  from a static pointer table and Troika **274**, leaving 353 to the species; and four class
  pairs share one space through a common slot-580 getter, so the unit key is the SPACE, not the
  classname. Names and ids are literal immediates ahead of each append call: the extractor reads
  operands and never executes a body.
  Size: L. Effort: Opus / high.
  **Pass P landed (2026-09-22): the pipeline half, no C++.** `uv run elysium export_v2
  ai-schedules-glb` publishes **57 units** — 56 id spaces plus a root — in four seconds, each
  validating standalone with no install and no writer, and a re-export is byte-identical.
  `uv run elysium import ai-schedules` deploys **748 files**: 691 `.sch` texts, 56 `space.json`
  sidecars and one `vocabulary.json`, under `Content/ElysiumCorpus/ai/schedules/`.
  *Three decisions the owner took.* The unit key is the SPACE and all **57** units publish (the
  eight text-less owners included, because they carry the parent links translation walks). The
  parser's vocabulary rides on a **root `vocabulary` unit**, not on the Troika unit as this text
  said — those thirteen tables are the parser's and a unit never carries data another owns. And the
  source-capsule question is settled by an **executable-image rule** appended to
  `seam_map_unit_contract.md`: the root carries the image without a capsule and its ledger
  partitions it, while each space unit capsules its texts' exact bytes as spans. The contract also
  gains a **derived-products** rule, because a registration table recovered from call sites is a
  fact no byte of the source spells and can never be a capsule.
  *What the image said that this text did not.* `CAI_BaseNPC` registers **68** schedule names, ids
  `0x00`–`0x43` dense, not 57 — through TWO entry points, the second being the same shared helper
  Troika uses, which is why Troika's range starts at 68. The "seven unregistered names" were a
  missing call target, and exactly four registered ids carry no text (`NONE`, `TARGET_FACE`,
  `TARGET_CHASE`, `AISCRIPT`). The texts live in **`.data`**, not `.rdata`. The cut region needs
  BOTH halves of retail's acceptance test — first token `Schedule` AND third token `Tasks` —
  because the parser's own diagnostics (`"Schedule has invalid state ID '%s'"` and eleven more)
  live in the same pool: the first half alone admits 704 runs, the full rule admits exactly the
  **691** that are fed, and the partition proves. MSVC emitted **three** bodies for one
  pair-append template, and anchoring on fewer leaves one name (`0xe2`) unregistered.
  *The parser is ported and run.* All 691 texts parse and none is refused, and four counts it never
  saw fall out matching the oracle: **441** distinct task identities, **42** `DELAY_INTERRUPTS`
  schedules, **0** inverted `!COND` interrupts and **35** boolean operands. Two facts for story 6:
  three of the seventeen prefixes (`Path:`, `Goal:`, `HintFlags:`) are used by no shipped text, and
  no bare operand fails `_atof`. Task statements measure **4,138**, not the witness table's 4,139.
  *The check:* `uv run elysium research schedule_owner_survey --check` re-derives the per-owner
  table from the image and diffs it against the committed one in `schedule-kernel.md`; it agrees on
  all 56 owners. The runtime passes follow below.

  **D–E integration complete, 2026-09-22.** All 691 texts now run through the
  corpus manager. The 28 hand programs, closed ID enum, metadata registry and C++ program-provider
  API are deleted. The last two programs had misread `aiscripted_schedule`'s activity arguments
  9/19 as program IDs; the recovered local-2 install and slot-440 translation now select the
  corpus's actual program, with its interrupts. `TASK_PATROL_PATH` supplies the movement activity;
  the director submits the goal before the next think and route exhaustion succeeds.
  The review repaired class-space dispatch, second-word NPCFlag routing, the remaining enum
  whitelist in slot 440, and local/global condition-mask integration. Schedule corpus loading is
  guaranteed even when a named input or restore arrives before the first schedule think.
  The new `ScheduleIntegration` suite executes the chase-failure witness through the real NPC,
  covering the lateral-cover route, all twelve task entries, final wait and translated fail route.
  `elysium.schedules [filter]` exposes the census and per-task reference counts.
  Cover-node selection still awaits the world navigation inputs assigned to 0018/4–5; its hook
  answers nothing. Character sight occluders and the flying mover remain explicit world seams.
  The lateral geometry uses Unreal capsule overlap/sweep as a named modernization, with retail
  candidate order, distances and timing. Recovery: `schedule-kernel.md`'s integration section and
  `authored-control.md`'s corrected director call chain.
  **Validation:** editor build green; all **1,267 `Elysium.Substrate` tests executed and passed**,
  including `ScheduleIntegration.ChaseFailureWitness`, both ID-space/condition regressions and
  the two-word flag execution test. `kernel_ledger --check` matches all 13 generated tables;
  the refreshed ledger cites 2,089 closure functions in the port and 2,324 in the oracle.
  Current task census: 691 programs, 4,138 steps, 26 bound bodies, 488 unported identities reached
  by 1,602 steps. The unported work queue is visible through `elysium.schedules`.

- [x] **4. The tunables table.**
  Retail: the `.rdata` cells every kernel file cites — "every threshold below was read out of
  the pinned `vampire.dll` at its cited address".
  Gap: the constants are inline, per file, with a paragraph of provenance each; the DRM
  constants were carried the same way.
  Job: one overlay, `research/tooling/ghidra/driver/kernel_tunables.tsv` (address, name, type,
  value, evidence), rendered by the ledger into a generated `ElysiumNpcKernelTunables.cpp` and
  verified against the image by `--check`; rule bodies read a named tunable; the inline
  constants and their paragraphs deleted as each family is touched by 6. Committed as generated
  C++, as the ledger's addresses and values already are — decided 2026-09-15 by the owner over
  a corpus file, because these are recovered numbers, not retail content.
  Provides: the thresholds 6's mechanism seams keep. Consumes: 1.
  **Two oracle pages feed the overlay directly (2026-09-21):** `docs/vtmb/npc-ai/rdata-cells.md`
  — 32 cells the oracle had marked unread, each TYPED by its reading instruction (eight are
  doubles that read as `0.0` when taken for floats) — and `docs/vtmb/npc-ai/convars.md` — 42
  ConVars with name, default and reader member; no shipped file overrides any, so the default is
  the retail value. `--check` should read a cell at the width the row states.
  Size: S–M. Effort: Sonnet / medium.
  **Landed 2026-09-22.** `research/tooling/ghidra/driver/kernel_tunables.tsv` holds **99 rows**:
  39 `f32` and 12 `f64` cells, and 22 `convar_f32` / 26 `convar_i32` ConVars. Every one is re-read
  out of the pinned image by `uv run elysium research gen_kernel_tunables --check`, at the width
  the row states, compared bit for bit. A ConVar row is checked by finding its object's
  `MOV ECX, object` static initialiser in `.text` and reading the console name and the default
  string that initialiser pushes. The generator emits `ElysiumNpcKernelTunables.h`, one
  `inline constexpr` per cell under `namespace ElysiumNpcTunables`, and `.cpp`, which holds the
  ConVar rows and their live store. It is a header plus a `.cpp` rather than the `.cpp` this text
  named, because the values must stay constant expressions.
  - *What it reads:* `ConVarFloat` is retail's `+0x28` read and `ConVarInt` its `+0x2c` read. A
    default parses as `ConVar::Create` does it (`atof` / `atoi`), and `SetConVar` behaves as
    `SetValue(float)`. `ElysiumNpc.h` includes the header, so every kernel body sees the names.
  - *How cells are named:* a cell MSVC pooled (1.0f, 0.5f, 100.0f…) is named by its value
    (`One`, `Half`, `HalfDouble`). A cell that one body owns is named by what it bounds.
  - *The ledger:* `kernel_ledger` counts the table into `coverage.md` § "Tunables". `--report`
    lists the NPC substrate's remaining inline `DAT_` cells: **344 over 91 files**, which is story
    6's migration queue. ConVar coverage includes both the object and its reader pointer at +4;
    the review correction removed 37 already-covered reader aliases from the original count.
  - *The seeds:* `rdata-cells.md` (31 of its 32 rows; the CRC-32 table stays out), `convars.md`
    (all 42), and the movement cells the 0019/1 seam rows keep. The check then named **seven
    more**: the six debug ConVars below, and `0x1044ffe0` (65536/360).

  *What the check corrected in the oracle.* `werewolf_draw_hints` ships `"0"`, not `"40"`. It
  named six debug ConVars the Debug families read that `convars.md` had not listed; one of them,
  `ent_trace_conditions`, ships **`"1"`**. `DAT_10920534` / `DAT_10920535` are plain bytes, not
  ConVars. The code's "taunt gate" `DAT_109248f4` is `debug_allow_dodge` (`0x102b7cf0`).

  *The owner's decision: wire every seam, not just the store (2026-09-22).* **83 inline
  constants** already held the image's value and now read the table. The rest were divergences,
  each a single one against a body already ported, now corrected to retail:
  - **The ConVar seams.** The kernel's two string-keyed stores (`DebugConVar`,
    `Anim10FloatConVar`) are deleted. So is about a score of per-family ConVar seams, every one
    of which answered 0 as "unrecovered". What now ships as retail defaults:
    - the melee range, `debug_melee_advance_combatmove_dist` 100, read by every melee selector;
      `MeleeRangeUnits()` is its one reader;
    - `debug_allow_move_facing` 1, which arms facing requests and the combat-aggression arm;
    - `debug_hunting_aggressive` 1;
    - `npc_hit_buildup_amount` 2, with its 0-fallback deleted;
    - flex 5 / 7;
    - the view-cone apex, `debug_viewcone_back_dist` 40 units. `ViewConeBodyOffsetCm` is now an
      accessor over the table;
    - the turn scalars .15 and turn speed 90;
    - `ming_xiao_pickup` 1 and `manbat_delta` 600;
    - werewolf pursuit 3.0 s / 800, teleport-out 4.0 and fastbreak 40;
    - Tzimisce claw 40 / 25 / 0, voice 100 / 65 / 1 and throw .007 / .0008;
    - sabbat gunman .1 / 3 / 3.0;
    - `sk_basenpctroika_health` 10, written by `NPCInit` exactly as retail does. A pedestrian's
      restore, which re-runs `NPCInit`, therefore comes back at 10.
  - **The 0.0 stand-ins for cells `rdata-cells.md` had read.** There are about twenty:
    - the hint validator's height arm (64, double), which is no longer disarmed;
    - the face-turn rung −40, the angle quantum and the normalise epsilon;
    - `CAI_Hint::Spawn`'s ×0.5 and +43. These are STORED back over the angle range, the authored
      one included (`102d0da5 FST`);
    - the cine delays 1.0 / 10⁶ and the standoff 0.01 / −0.001;
    - the werewolf hint yaw 90 and the whisper 0.5 / 0.6;
    - the fire-immune window 15, the detected-attack window 5 and the melee height 64;
    - the cover-lean 45 / 1.4 / 1.2;
    - the Sabbat leader's too-far bound 120 and the Chang jump-path clearance 100;
    - the OUTOF wait 0.5 and the segment floor 1e-5.
  - **Misreadings corrected while wiring.** Each was read at the listing:
    - `CNPC_Crow` `0x10357be0` took `(float)_DAT_10449280` as 0.0 and flattened every positive
      scale. It is `FCOMP double ptr`, a clamp AT 1.0.
    - `CAI_Motor#4` scaled the raw delta. The listing normalises in place first.
    - `CAI_Motor#18` dropped the 65536/360 prescale.
    - `CheckJumpPathToHintNode` fed centimetres into a SOURCE-unit test.
    - Look-target arm 2 lacked its 96-unit goal floor.

  *Review corrections (2026-09-23).* The Tzimisce claw-origin helper (`0x103bfd80`) converts all
  three live ConVar distances from Source units to centimetres before adding them to the source
  point; the two default offsets are 63.5 cm sideways and 101.6 cm upward. Regression coverage
  exercises both claw activities, a nonzero forward value and the fallback. The migration queue
  now recognizes ConVar reader aliases as covered, with a regression protecting adjacent ordinary
  cells from being incorrectly excluded.

  *Validation:* editor build green; **1,268 / 1,268 `Elysium.Substrate`**, including the new
  `NpcKernelTunables.ConVars` suite and `ScheduleIntegration.ChaseFailureWitness`, plus
  `Elysium.Content` 14 / 14 (the `sp_tutorial_1` content set) and `Elysium.PlayerWorld` 1 / 1. `gen_kernel_tunables`, `kernel_ledger` and `kernel_lists`
  `--check` are all green. **Pre-existing and not this story's:** `gen_kernel_shape --check` (and
  so `gen_kernel_bindings --check`) fails on HEAD, because `607efd52` added
  `virtual void DeathSound()` to `ElysiumSchedule.h` without a `SLOT_PORT_MAP` row for slot 488.
  *Still open, for story 6:* the queue above, and inputs that are still seams, so their cells are
  named but their arms cannot run: `_DAT_104994e0` (−30) behind the stat-type join,
  `_DAT_104492e0` (1e-6) behind the hull sweep, and `_DAT_10449154` (0.45) behind the navigator
  re-probe.

- [ ] **5. The class tree: one port class per retail class.**
  Retail: 77 classes in six lines under `CAI_BaseNPCTroika` (`classes.md`), each with its own
  words past `+0x665c` and its overrides at the slots `signatures.md` lists; a species body
  that calls the body it replaces does so by a **direct** call to the base's own function,
  never through the vtable (`shape.md` § "How a species body reaches the body it replaces").
  Gap: `FElysiumNpc` is `final`; species are rows keyed on the retail class name; the vtable
  is rebuilt by hand — `ElysiumNpcKernelClassLookup` (`Find`, `OfClassname`, `DerivesFrom`,
  `OverrideOf`, `BodyOf`), `RetailClass()` / `IsRetailClass` string checks inside bodies, the
  "prologue must decline while the species body runs" re-entry rule, 6,009 lines of numbered
  slot stubs, slot numbers in the API (`RunTaskSlot444`), and every NPC carrying every
  species' words. Decided 2026-09-15 by the owner: **the whole tree, uniformly** — a class
  whose only difference is its sound table is a twenty-line class; two dispatch mechanisms is
  worse than either.
  Job: `FElysiumNpc` becomes the Troika base (`final` dropped); one subclass per retail class
  in the retail tree, own words on the owning class, overrides `virtual`, `Super::` where retail
  called the base directly; a classname → constructor factory replacing `OfClassname` (the
  most-derived claimant rule kept); `FElysiumPlayerControllerNpc` folded in as
  `CNPC_VPlayerController`'s port; the vocalisation slots 488–508 as one base body over a
  virtual sound-table getter each species overrides; the dispatcher, the string checks, the
  re-entry rule, the slot stubs and the species `if` prologues deleted; `gen_kernel_shape.py`
  emits the census only, and the census test asserts one port class per retail class, one
  member per own word on that class, and one override per retail (class, slot) row verdicted
  `rule`. Lands alone on a branch with the witness green before and after; no other story
  touches `ElysiumNpc.h` while it is open.
  Consumes: 1 (the delete list first, so nothing dead is re-homed), 3 (programs load per
  class in the tree's order). Provides: the tree every species story in 0002 lands on.
  Size: XL. Effort: Opus / high.

- [ ] **6. The deletions and the mechanism seams.**
  Job, dead: every `dead` row's body and test removed — the Debug, Debug10 and Debug10_2
  families, the ring and stamps and their words, the scrambler in Senses and Combat10 (the
  criminal level stored plain, the retail field noted as a `CSecureType` so the ledger still
  lines up), the secure ints.
  Job, mechanism: every `mechanism` row's body replaced by a seam call into Unreal that keeps
  the row's retail thresholds as tunables (4) — locomotion stepping and yaw to the character
  movement component, pathfinding/following to Unreal navigation through 0018 story 5's
  movement seam (goal types, tolerance rules and failure codes kept), traces
  and push-out to the collision service, the physics tick and network state to nothing; the
  Motor, Motor10, Motor2 families, the Positions trace bodies, Geometry's push-outs and
  EntityChain's engine rows.
  Destination choice is not pathfinding: hunt, cover, retreat and flank choose among places by
  query-specific tests, ownership and cooldowns. Those tests are behaviour and stay (0018 story
  9); the graph traversal under them is engine and goes with the pathfinder (0018 § Navigation
  boundary, 2026-09-20).
  Consumes: 1, 4, 0018 story 5. Provides: the smaller kernel 0002 continues on.
  Size: L. Effort: Opus / high.

- [ ] **7. The reach cut.**
  Retail: none — a query over the ledger.
  Job: `kernel_ledger --reach <map>`: seeded from the map's population (classnames → retail
  classes → their `SelectSchedule`, `TranslateSchedule`, `StartTask`, `RunTask` overrides and
  the base arms) and the schedule texts those classes load (the task and condition identities
  their programs name), the query lists the functions, task identities and species rows the map
  reaches; `coverage.md` gains a per-map column. 0002's open stories are then cut to
  `sp_tutorial_1`'s list first and `sm_hub_1`'s second.
  Consumes: 3 (the programs say which tasks a class reaches). Provides: 0002's scope.
  Size: S–M. Effort: Sonnet / medium.

- [ ] **8. 29e closes under the strict verdict.**
  Job: the 19–29 checklist re-verdicted by 1 before the twelve remaining families land; each
  family ported as rules only, its `dead` and `mechanism` arms skipped with the verdict as the
  record; the in-flight Conditions19 reviewed against the pass before it merges. 29e keeps its
  number and its text in 0002; this story is the rule it finishes under.
  Consumes: 1. Size: what remains of 29e. Effort: as 29e.
  **Scope after 1 (measured 2026-09-23):** the twelve families hold 351 rows, of which **286 are
  `rule`** and are the port; 55 `dead`, 8 `mechanism` and 2 `present` get no body. Per family
  (rule / rows): Conditions19 20/23, Spawn19 48/60, RunAi19 17/34, StartTask19 27/28, RunTask19
  23/24, Select19 31/39, Damage19 26/26, Script19 20/32, Think19 15/15, Boss19 11/19, Werewolf19
  17/17, Misc19 31/34. Nothing of Conditions19 is in the tree: the tracker's "in flight" predates
  the stop after Maintain19, and the `Conditions19` git branch is the 0019/2 bindings work, already
  an ancestor of `main`.
  **Delivered in three passes (owner's decision 2026-09-23): R the retrieval, I the
  implementation, C the close.** The split separates reading the corpus, which is parallel and
  needs no build, from porting, which is serial on the class, the overlay and the build.
  - *Pass R, retrieval.* One reading packet per family beside the briefs
    (`$ELYSIUM_WORK_ROOT/research/npc-kernel-checklist/families-19-29/<Family>19-READING.md`), in
    the shape of the cancelled wave-1 `READING.md` the four landed families were ported from. Per
    `rule` row: the arm inventory (every conditional branch with both targets), every memory
    operand with its shape-map name, every global with its value at the instruction's width, every
    call with thunks followed and virtual calls as slot numbers, the argument mapping settled by
    the callee's `RET n`, the stack locals a callee may rewrite through a pointer, the datamap name
    of every offset read, the banked wave-1 corrections that apply, and the verdict line verbatim.
    Verdicts do not change here; a packet claim that contradicts its checklist row is listed for
    the owner, because it changes what the porter was told. Done when 12 packets cover 286 rows and
    a check refuses a packet missing any address of its family's row file.
    *The method, piloted on `UpdateEnemyPos 0x10271900`* (artifacts `$ELYSIUM_WORK_ROOT/bench/pair-read/`):
    (1) a **script** writes the skeleton — arms, operands, globals, calls, slots — from the listing
    database in seconds, complete by construction, so no model transcribes structure; (2) **luna 6
    at `high` on the Fast service tier** (`-c service_tier='"priority"'`) fills the walk on the
    skeleton — branch meaning, argument mapping, locals, object identity — in about four minutes
    and cents per function; (3) **GLM 5.3** reads the same skeleton brief as the second reader on
    the flat plan; (4) the two walks are **diffed row by row on the skeleton's addresses**, agreed
    rows accepted, disagreements sent to luna `max` with both readings and the listing excerpt;
    (5) the packet is diffed against the checklist row. What the pilot measured: on the full
    (skeleton-less) brief luna mis-attributed the pushed arguments at every effort and tier, and the
    skeleton brief fixed it at `high`; `max` bought one more row (a swapped branch consequence) at
    four times the time; the Fast tier cut `max` from 24 to 14 minutes for the same answer; every
    error any luna run made was a row where GLM disagreed; the one error no pair catches — the
    checklist's `+0x20` "yaw", which the datamap names `CAI_Path::m_moveTolerance` — is caught by
    the checklist diff. GLM-5.3-Flash answers a bare prompt but hung twice on the turn after corpus
    tool results, and the 2026-09-20 bench had already disqualified it on accuracy; it is not a
    reader.
    **Tier 0 landed 2026-09-23:** `uv run elysium research kernel_skeleton` (`kernel_skeleton.py`
    beside the ledger; `<addr>`, `--family <name>`, `--all`, `--check`). It reads the listing
    database and the image and writes `skeletons-19-29/<Family>.md` + `.json` beside the family
    files: every conditional jump with both targets, every `RET`, every jump table read out of the
    image while its entries fall inside the body (base `StartTask` 107 entries at `0x10286f8c`,
    Troika 176 at `0x102a77f8`, as the reading banked them), every memory operand with its base
    resolved to `this` or to a word loaded off `this` and joined to the datamap through the
    function's own class and base chain, to `layout.tsv` and to the shape map, every global at the
    instruction's width, every call with thunks followed through their own `JMP` (the edge table
    has no edge out of a thunk), virtual calls as slot numbers named from the port's slot table
    when the vtable came off `this`, and a `JMP dword ptr [reg+0xNNN]` in an epilogue as the
    virtual tail call it is (`CNPC_VHunter::NPCInit 0x10388b4f` → slot 66 on the weapon). `this`
    survives an early-return epilogue's `POP`s. All 468 band rows in about two seconds;
    `--check` holds every family row to a section. `pipeline/tests/test_kernel_skeleton.py`
    exercises the walk on the pinned pilot listing and the real corpus when present.
    *Scope correction the tool found:* the band's **eleven ‼ rows are in no family file** — they
    were verdicted one at a time and the family split never took them back — and nine are `rule`
    (`ScriptHide 0x102c1ce0`, `CNPC_VGuard1` / `CNPC_VHumanCombatant` / `CNPC_VYukie::NPCInit`,
    `CNPC_VTzimisceRunner::vfunc330` and `::HandleAnimEvent`, `CNPC_VPlayerController` and
    `CNPC_VWerewolf::NPCThink`, `CNPC_VCop::StartTask`). They are collected as
    `families-19-29/Damaged19.tsv` for pass R; which port family lands each, and which of the
    three `NPCInit`s Lifecycle19 already carried, is the owner's call at pass I.
    **Pass R landed 2026-09-23** (log with every measurement: `$ELYSIUM_WORK_ROOT/bench/pair-read/LOG.md`).
    Thirteen packets, `families-19-29/<Family>19-READING.md`, the twelve families plus Damaged19:
    **295 `rule` rows** (286 + 9), every row a section holding the checklist verdict line verbatim,
    the skeleton, the merged walk with a provenance cell per row, both readers' Effect and
    Unrecovered, the banked wave-1 corrections that cite the address, and the family's
    packet-versus-checklist table. Rows verdicted `dead` / `mechanism` / `present` are listed with
    the verdict and get no walk. The merged walk holds **10,113 branch and argument rows**: 4,472
    agreed, 5,428 where one reader names more (kept, the deeper cell first), **128 conflicts, all
    drilled** with both readings and the judge's settlement beside the listing line, 85 one-sided
    (a reader's omission the other covered), none unsettled. **115 packet-versus-checklist
    contradictions** for the owner (StartTask19 32, Conditions19 14, Damage19 11, RunTask19 10,
    Select19 10, Think19 8, Werewolf19 8, Misc19 7, Spawn19 5, Boss19 4, Script19 3, Damaged19 2,
    RunAi19 1), each with the listing line that decides it; verdicts unchanged.
    *Runs and cost:* 231 codex runs, 78M input / 3.3M output tokens (luna 6 high Fast: 102 first
    reads and bounces, 18.6M; Sol 6 high: 91 second reads, 34.5M; luna 6 max: 18 drills, 10.4M; luna
    high: 18 contra reads, 14.0M), ten Opus 5.5 subagents (~1.9M Claude tokens: nine 2.5–4 KB first
    walks before measurement retired them, one drill as the judge control). Reader waves ran 03:35
    → 04:33, 58 minutes wall at 8–9 concurrent codex runs; run-time sum 610 minutes.
    *Routing as measured, changed from the pilot's plan:* GLM-5.3-Flash is not a reader (hung on
    every batch brief, one 9-function batch in 744 s by file attachment; Sol did it in 120 s) —
    **Sol high is the second reader on every band**. The diff signal was tightened on the first
    pairs (`->` is not a sense, `0xa` = `10`, `RET n` and address citations are not arguments, a
    different *order* of the shared literals is the only argument conflict): the tiny pair fell
    from 10 flagged rows to 0 real ones. On the 2.8 KB pilot with that signal luna high Fast
    disagreed with Opus on 0 of 174 rows (Sol 3), at 2 min versus 10, so **luna reads every band
    first**, the four giants as weight-balanced chunks plus a chunk 0 for the dispatch prologue,
    and **Opus is not used**: the same Misc19 drill to luna max and to Opus settled all 12 rows
    the same way (luna max 530 s, Opus 212 s and 120k tokens), so **luna max is the judge**. luna
    high is short by one or two rows on a fifth of its batches and is bounced per function or
    chunk (23 bounces, 41–164 s); six calls in the giants it omits on a second read stay as
    Sol-only rows. Sol was complete on every batch and every giant chunk on the first run.
    *Tool changes:* the reading side of the tool landed as `kernel_packet.py` behind
    `kernel_skeleton` subcommands — `brief` (rule rows only, bands, batches, chunks with case ids,
    shared continuations and chunk 0), `check-walk`, `diff`, `drill-brief`, `packet`,
    `contra-brief`, `check-packet` — with `pipeline/tests/test_kernel_packet.py` on pinned walks;
    `check-packet --all` refuses a packet missing a `rule` row, a skeleton arm without a branch
    row or a call without an argument row, and is green on the thirteen.
  - *Pass I, implementation.* One family at a time per lane, two lanes in two worktrees, each
    family ported from its packet: every `rule` arm in retail order with its instruction address,
    one test per arm, the slot rows flipped to `hand:`, the generators re-run, build, the family
    suite plus `Elysium.Substrate.Npc` and `Elysium.Substrate.Schedule`. Lane A, the interpreter:
    Conditions19 → RunAi19 → StartTask19 → RunTask19 → Select19 → Think19. Lane B, independent of
    the loop: Spawn19 → Damage19 → Script19 → Boss19 → Werewolf19 (the last two after lane A's base
    bodies exist). Before any review, a script gates the family: every packet address appears in
    the family files, the coverage table matches the arm inventory, and the three seam searches are
    run and pasted. A family that fails the gate goes back to its porter. A green gate reaches the
    full read by a fresh reviewer on a different model than the porter, then one commit per
    family. Merges at family boundaries; the known conflicts are the include list in
    `ElysiumNpc.h`, disjoint overlay rows and the generated slot stubs.
  - *Pass C, close.* After both lanes merge: Misc19's own 31 rows, the absorbed stories' sentences
    (25b, 25c, 26, 16b, 10d), the `[x]` on 29e and here, the build-order line, and the file rename
    dropping the `19` suffix as a separate pure-move commit, allocating numbered parts where a
    concern file already exists (Lifecycle, Conditions) and keeping every file.

## Build order
1 → 2 → 3 → 4, with 8 in parallel from 1 on → 5 → 6 → 7. Relevant bindings from 2 precede
completion of 0018 story 2's bake/adoption, while its native class declarations can precede
binding completion. AIN projection is 0018/4's independent format boundary. 5 lands
alone on a branch. When 7 lands this spec closes and 0002's build order takes over.

## Seams
- Provides: the loaded programs and id spaces to 0002; the generated bindings and the tunables
  to 0018 and 0002; the class tree every species story lands on; the reach cut that scopes
  0002; the delete and seam lists.
- Consumes: the ledger and its overlay; the datamap replay; the pinned `vampire.dll` through
  the export lane; 0018 story 5 for the path seam.
