# 0019 npc-kernel-rework — The kernel as data plus a class tree: extract what Troika typed, port what the bytecode observes, delete what nothing can see

**Standing rule for every story:** follow retail behaviour; do not take on work already planned in another story of this spec or in another spec.

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
**Closed 2026-09-29** — story 7 landed; every story is ticked.

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
  file; the `nosferatu_tolerrant` key has no reader. (**Corrected in story 5 fold A3:** `m_flRadius`
  on `aiscripted_schedule` HAS a reader — `CCineNPC::FindEntity` `0x101a7600` reads it at
  `0x101a7621` / `0x101a76c7` as the acquisition radius of `CineThink` `0x101a8070`.)
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
  `ElysiumNpcLifecycle2.cpp:126`), and the `.sch` loader `0x1030f220` (`dead`, never
  ported). Of the dead-by-content inputs, the climb is whole rows (`CAI_Motor` slots 3–5 and
  `MoveClimb 0x102eebc0`, `dead`); the rest are ARMS inside bodies that stay `rule`. Two are named
  dead in their row's tag — `COND_KNOCKBACK`'s two arms in `PreSelectSchedule 0x102ae920` and the
  `TASK 0xae` arm in `0x10375f50`; goal type 5 and the hint-type-800 `|| 0x320` are not yet named
  in a tag and ride with the stories that port those bodies (0018/5, 0002/10g); `m_flRadius` and
  `nosferatu_tolerrant` were recorded as keys with no reader, which story 2's bindings left unbound;
  story 5 fold A3 corrected `m_flRadius` (read by `FindEntity` `0x101a7600`) and binds it. Slot 580 (the class's id space) stays `rule` on every
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
  130 was already ported at `ElysiumNpcLifecycle2.cpp:551` — `FElysiumNpc::OnRestore`
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

- [x] **5. The class tree: one port class per live retail class.**
  Retail: 77 classes whose primary vtable spans the NPC range (`classes.md`). 63 sit below
  `CAI_BaseNPCTroika` in ten direct lines, 12 more sit below `CAI_BaseNPC` beside it, and the
  two bases complete the set. 21 are dead (`population.md` § "NPC classes with no instance").
  Each of the 56 live classes carries its own words past its base's end (309 live words over 33
  classes past Troika's `+0x665c`) and its overrides at the slots `signatures.md` lists. A
  species body that calls the body it replaces does so by a **direct** call to the base's own
  function, never through the vtable (`shape.md` § "How a species body reaches the body it
  replaces"). Every classname has exactly one factory, which builds exactly one class
  (`population.md` § "The classname → class map, read from the factories"). The script
  directors are NPC classes too: `scripted_sequence` builds `CCineNPC`, `aiscripted_sequence`
  `CCineAI`, `aiscripted_schedule` `CCineAISchedule`, all below `CAI_BaseNPC`. The three makers
  sit below Troika.
  Gap: `FElysiumNpc` is `final`; species are rows keyed on the retail class name; the vtable
  is rebuilt by hand — `ElysiumNpcKernelClassLookup` (`Find`, `OfClassname`, `DerivesFrom`,
  `OverrideOf`, `BodyOf`), `RetailClass()` / `IsRetailClass` string checks inside bodies, the
  "prologue must decline while the species body runs" re-entry rule, the generated slot
  surface, slot numbers in the API (`RunTaskSlot444`), and every NPC carrying every species'
  words. Measured 2026-09-24, outside tests: 110 `RetailClass()` calls, 50 `IsRetailClass`, 68
  lookup calls, 54 dispatch-scope sites, 21 slot-numbered names, about 174 species words on the
  one class, and 6,013 lines of generated slot surface holding 269 bodies (186 stubs that count
  calls, 83 one-constant bodies; the 268/82 first measured was one default short).
  The dispatcher also answers the wrong class. Its classname column is a proximity guess in
  `npc_translation_survey.py`, and the "most-derived claimant" rule only undoes its
  over-claims. Nine classnames resolve differently from retail's factories, and seven of those
  are live. A placed `npc_VCop` (72 authored, 37 in `sm_hub_1`) runs as the bare Troika line
  with Troika's schedule space. `npc_VGhoulCroucher`, `npc_VZombie`, `npc_VWerewolf`,
  `npc_VSheriffMan`, `npc_VVampireBoss` and `npc_VPlaceholder` resolve to nothing, and four of
  them are not registered at all. Tests pin the wrong answer
  (`TestNull("no census class claims npc_VCop")`) and use the Cop as a stand-in Troika NPC.
  Species datamap rows are unbound: 230 live words, 42 of them `KEY` or `INPUT`. The tutorial's
  three rats author eight such keys that the port drops, and a ported sense body reads three
  of them as zero.
  Decided 2026-09-15 by the owner: **the whole tree, uniformly** — a class whose only
  difference is its sound table is a twenty-line class; two dispatch mechanisms is worse than
  either.
  Job (settled by the owner 2026-09-24; replaces the 2026-09-15 job text, whose "most-derived
  claimant rule kept", "sound-table getter" and "one class per retail class" did not survive
  validation):
  - *The tree.* `FElysiumNpcBase` is `CAI_BaseNPC`, and `FElysiumNpc` is `CAI_BaseNPCTroika`
    with `final` dropped. Below them stands one class per live retail class, named `FElysiumNpc`
    plus the retail name without its prefix: `FElysiumNpcBach`, `FElysiumNpcCop`,
    `FElysiumNpcVampireBoss`, `FElysiumNpcPayphone`. Each class has one header and one cpp.
    Dead classes get no class, only their census row. Own words sit on the owning class,
    overrides are `virtual`, and a direct call to the base's own function (`Super::` or the
    qualified name) stands where retail calls it directly.
  - *The factory.* A classname → constructor map read from retail's factories replaces
    `OfClassname` and the most-derived claimant rule. `_constructor_aliases` is replaced by the
    factory walk and the census is regenerated. Every live retail classname is registered,
    including the seven the census missed.
  - *Class checks.* A check that imitates vtable dispatch becomes an override. A check that
    ports a retail runtime type cast becomes a type test on the tree, for example the Fleshpile
    maker's runner test or `CBasePlayer::StartPlayerDialog`'s `CPayphone` cast.
  - *The generated slot bodies.* Each moves to its retail owning class. The entity chain's go
    onto the port's entity, animating and combat-character classes, and the NPC layers' go onto
    `FElysiumNpcBase` or `FElysiumNpc`. The 38 linker-folded bodies go onto the class that
    first declares the slot. The 8 whose name matches a port concept get audited dispositions,
    not same-name merges (none is a method on the port chain; step 6 recorded the audit):
    `GetAbsOrigin`/`GetOrigin`/`GetAngles` answer the entity's `Origin`/`Angles`, `SetOrigin`
    writes through `SetRuntimeOrigin`, `SetMoveType` is its seam, `AcceptInput` is the world's
    input chokepoint, and `Weapon_Switch` and `GetModelIndex` stay counting stubs. Stubs keep
    counting when fired and name their owner; a stub is deleted only when it is dead, uncalled
    and overridden nowhere; one-constant bodies stay.
  - *Vocalisation* is plain overrides. The live species bodies at 488–508 are one-byte silent
    bodies (Camera, CameraSecurity, Newscaster, FrenzyShadow, WolfMorph), Tzimisce's sentence
    groups and Werewolf's event emits. Every wav-table species is dead. The `FVocalization`
    table goes.
  - *Bindings.* Story 2's generator runs per stood class.
  - *The controller.* `FElysiumPlayerControllerNpc` becomes `FElysiumNpcPlayerController`
    below `FElysiumNpcVampire`, with FrenzyShadow and WolfMorph below it. It runs retail's AI:
    its `NPCThink` `0x103a4700` runs the base think and then slot 614 `ResetThinkTimers`, and
    its `PreSelectSchedule` `0x103a46b0` answers `0x6b` when idle.
  - *The folds.* The three makers go below `FElysiumNpc`. `CCineNPC`, `CCineAI`,
    `CCineAISchedule` and `CAI_TestHull` go below `FElysiumNpcBase`. Today's maker and director
    entity classes are rewritten as these, and the port's one class for both sequence
    classnames splits in two.
  - *Tests.* Fixtures are rewritten to spawn by classname, replacing the 139
    `SetRetailClassForTests` calls in 15 files; the hook is deleted.
  - *The census.* `gen_kernel_shape.py` emits the census only. The census test asserts one port
    class per live retail class with the dead ones listed, one member per own datamap word on
  that class, and one override per ported (class, slot) `rule` row. A new `kernel_shape` flag
    lists the unported `rule` overrides, a number that must only fall. `--residue` already names
    the unsettled layout and slot rows.
  **Execution: [story-5-execution-plan.md](story-5-execution-plan.md)**, on `0019-5-class-tree`.
  Steps 0–6 are accepted (2026-09-24 → 09-27): the dead species deleted, 44 species classes with
  retail factories, dispatch as overrides with 187 qualified direct calls, bodies/words/bindings on
  their classes, `CAI_BaseNPC` separated from Troika, every generated slot body on its retail owner
  and the census asserted in C++ — 46 of the 56 live classes stand. Last gate 1,281 / 14 / 1, zero
  failures. **Revised 2026-09-27** after the owner's review: the remaining work is **two commits**,
  A (the ten deferred classes folded: test hull, controller line, directors, makers) and B (the
  compatibility surface deleted, `gen_kernel_shape` census-only, story-8 handoff). The per-step
  checkers, expectation files and preflight/record ceremony are retired — the product is guarded by
  the generator checks, the C++ census tests and the runtime gate, and commit B deletes the
  `kernel_migration*` / `test_delta` tooling. Expected behaviour changes: the seven corrected
  resolutions and 30 newly active classnames (landed), newly effective bindings (landed), the
  controller running retail's AI and the makers' inherited world participation (commit A), plus any
  evidenced correction found while folding. Story 8 implementation waits for commit B.
  **Order amended 2026-09-23: this story runs before story 8's pass I.** Pass R measured the
  port: 227 of its 295 `rule` rows are species overrides, which on the flat class would each be
  written as a `RetailClass()` prologue and then moved here. The one coupling that put 8 first was
  the census clause above, which reads as "every `rule` override exists" and so needed 8's bodies;
  it is a definition, not a dependency, and is amended: the census asserts an override for every
  (class, slot) `rule` row that the port carries, and lists the `rule` rows not yet ported as
  residue (a new `kernel_shape` flag, since `--residue` is taken; see the job above), a number
  that must only fall as 8's families land. 8's
  retrieval (pass R) stays where it was, since a packet does not depend on the class shape.
  Consumes: 1 (the delete list first, so nothing dead is re-homed), 3 (programs load per
  class in the tree's order). Provides: the tree every species story in 0002 lands on, and the
  tree story 8's pass I ports onto.
  Size: XL. Effort: Opus / high.
  **Landed 2026-09-27** (steps 0–6, commit A, commit B, on `0019-5-class-tree`). All **56 live
  retail classes are C++ classes** at their Appendix A place in the plan; the 21 dead ones stay
  census rows. Commit A folded the last ten (the test hull, the controller line, the three
  directors, the three makers); commit B closed the compatibility surface:
  - *Dispatch.* `ElysiumNpcKernelClassLookup` (`Find`, `OfClassname`, `DerivesFrom`, `OverrideOf`,
    `BodyOf`), `IsRetailClass` and `OwnRetailClassDerivesFrom` are deleted. Retail's
    `__RTDynamicCast` is `AsSpecies<T>()` over the C++ chain (`ELYSIUM_NPC_CLASS` / `IsNpcClass`),
    so the ChangBros brother casts (`0x1036e2f0` and its two siblings) admit the Blade and the Claw
    as retail's do, and the Vampire boss's partner cast (`103c64c4`) is typed. A class's census row
    is identity only (`ElysiumNpcKernelShape::ClassNamed`); its one runtime data use is the schedule
    corpus's per-class spaces. The hull words are constructor stores on the twelve classes whose
    retail constructors store them, not a class-keyed table walk; the pickup attach/release pairs
    are the Hengeyokai's (`0x10382670` / `0x10382400`) and the ManBat's (`0x1038f430` /
    `0x1038f790`) own bodies over a shared helper, with no name test; the Troika-only slot-546
    table, the jump-tunable rows, the slot-117 row table and the name-keyed schedule-space and
    load-flag lookups are gone. Exempt, stated: `FElysiumNpc::ScheduleIdSpaceRows` and
    `LoadedSchedulesRows` stay as records of the recovered slot-580/452 bodies and globals, read
    only by tests (the live answer is the schedule corpus's, per class).
  - *Vocalisation.* `FVocalization` / `GSoundsVocalizations` are gone: each Camera (19),
    SabbatLeader (2) and Tzimisce (2) override is its body.
  - *Hides.* Slots 66, 67, 86, 123, 133, 153 and 158 are real overrides below the NPC line: the
    weapon's `Hide`/`Unhide`, the cinematic camera's `ShouldTransmit` (`CBaseCineCam::vfunc86`
    `0x1006e6a0`, over the single player as recipient, a named seam) and `DrawDebugGeometryOverlays`
    (`0x1006ff40`, reading `camera_showdebug` through a seam answering its default 0), the mover's
    `MoveDone`/`IsMoving`, the player's `IsAlive`. `SLOT_PORT_MAP`'s `accepted` kind is replaced by
    `overridden-below`, which fails generation on a same-name member without `override` (comments
    stripped). The build has zero C4263/C4264.
  - *Census.* `gen_kernel_shape` emits the census, the chain classes' slot bodies (one-constant
    bodies and counting stubs, which stay) and a new compile-checked override census
    (`Tests/ElysiumNpcKernelOverrideCensus.cpp`, 789 rows): `NpcKernelShape.Overrides` holds one
    override per ported (class, slot) live row; `NpcKernelClass.TreeMatchesCensus` holds the C++
    chain of every live classname to the census's; `RegistryMatchesFactories` lists 56 live classes
    and no deferred one; `RetailHull.Constructors` holds the constructors to the hull table.
    Slots 438/440 name the virtuals the species override (`SpeciesSelectSchedule`,
    `TranslateScheduleRetail`). `NpcKernelShape.FieldOwners` covers the base and Troika layers; the
    species words are covered by the generated per-class bindings and their tests.
  - *`kernel_shape --unported`:* **987 → 778** (721 `no-override`, 57 `stub`): 176 rows
    implemented in commit A plus two SabbatLeader 620/621 false positives corrected there; 31
    hook-named overrides (slot 438 ×8, 440 ×23) the tool now sees; +2 corrected evidence when the
    tool learned that an inherited override carries a row only if its class owns the same body
    (`CNPC_VHengeyokai` 599 `0x10381750` / 600 `0x10381780` had been counted on `CNPC_VHuman`'s
    bodies), −2 when those two bodies were ported on the Hengeyokai. Pin:
    `docs/vtmb/npc-kernel/unported.tsv`.
  - *Corrections found.* `CNPC_VMingXiao`'s slot-166 `CanStandOn` (`0x10397000`) stood ported under
    a helper name and was counted ported off its doc comment; it is now the class's override and the
    scan ignores comments. `CNPC_VTzimisce` 491 `0x103b9500` writes its pain expression
    (`0x103b9f90(this, 1, 1.0)`) whether or not the sound gate opens, and both sentence hooks read
    the `tzimisce_voice_*` ConVars (the attenuation is 65, not 75). `kernel_ledger`'s own-body count is a primary-vtable diff (26 plan rows
    corrected; `classes.md` regenerated). `CBasePlayer::m_hControllerNPC` (`+0x1db0`) has one home,
    the world's controller handle; the NPC-side copy and the `GetControllerNPC` / `0x101618e0` /
    `0x10175180` bodies ported onto the NPC over it are deleted.
  - *Named modernizations* (commit A's, standing): the non-solid controller motor, same-frame
    controller removal, the disposition copy (removed by the controller follow-up below), the director's
    0.05 s beat, explicit saves refused mid-beat, restart-not-resume on revisit, the skipped
    pre-idle flash, the ground-ray lift and clamp, the "Non Troika" child removal, StartHidden not
    replayed onto a child, a self-removing child returning null.
  - *Validation* (plan §4, on the B tree `fc3bf3a3`): seven generator `--check`s exit 0; pytest 81;
    build with zero C4263/C4264; `Elysium.Substrate` 1,320 / 0 failed
    (`reports/tests/20260927T164159.752110Z-elysium-substrate`), `Elysium.Content` 14 / 0
    (`…T164528.881045Z-elysium-content`), `Elysium.PlayerWorld` 1 / 0 (`…T164609.730586Z-elysium-playerworld`);
    stub-fired set identical to commit A's gate but one count (`StudioFrameAdvance` on `npc_payphone`
    36 → 37, the new slot-546 payphone test); map smoke on the five maps clean (tutorial porch controller,
    feedcamera and alley camera tracks, fishmarket `logic_takecontrol` cut-scene, giovanni zombie makers
    and a weapon deploy/holster, pawnshop pedestrians and a rotating door). One pre-existing finding
    outside this story: `hw_warrens_4`'s `iris_clip` door pair never reaches AtTop (spec 0018).
    Copies of every report: `$ELYSIUM_WORK_ROOT/research/npc-kernel/story-5/gateA/`, `gateB/`, `smokeA/`.
  - *Corrected after landing A, not a modernization — landed:* which body is drawn during a controller
    scene was recovered on 2026-09-27 — retail draws the pawn and `CBasePlayer::PostThink 0x1016be10`
    copies the controller's pose and transform onto it every frame (`0x1016c510-0x1016c672`). The
    follow-up commit landed it: the stand-in is `EF_NODRAW` and undrawn (`FElysiumNpc::IsTransmitted`,
    `ShouldTransmit 0x100ab020`), `FElysiumEntityWorld::UpdatePlayerFromController` is the per-frame
    copy (the pawn's body draws the stand-in's pose), input is taken by the `0x10351090` usercmd wipe
    (applied to the frame's command, not the `IsMobile` latch), `RemovePlayerControllerEntity` is `0x101618e0` with its (1,1)/(0,0) callers and no
    model/skin/disposition hand-back, and the disposition copy at creation is gone. The
    `docs/vtmb/entity_io.md` claim that a `!player` teleport moves the controller was refuted and
    corrected (`CPointTeleport::InputTeleport 0x1018dc00`). Standing: the non-solid controller motor
    (modernization) and same-frame removal (divergence).
  - *Hand-off to story 8* ([handoff-story-8.md](handoff-story-8.md), the durable list). The class
    map is the plan's Appendix A; the residue is `docs/vtmb/npc-kernel/unported.tsv` (778 rows),
    regenerated by `kernel_shape --unported`, which must only fall. Carried: the think loop — no live code dispatches slots 431/437/433/442, and retail
    `GetNewSchedule` `0x1028a260` runs 437 before 438; Andrei's `StartTask` case 0x154 (the
    fleshpile runner spawner; `SummonRunnerNear` is its seam); `Weapon_Switch` `0x1032dde0` (Ming
    Xiao, Bach); `CAI_Hint`'s `ObjectCaps` `0x102d2ee0`; Camera's `NPCInit` write of
    `DesiredMoveYaw`; `+0xe4 m_pfnScriptSavedThink` and the maker's two think records; the
    `0x101618e0` animation and velocity hand-back onto the player; the VHuman 103/438/442 and
    WolfMorph 451/452/580 bodies; no shipped creator for FrenzyShadow (`0x10161fc0`) or WolfMorph
    (`0x101f8620`/`0x101f8f30`); no navigator or move-probe dispatch of slot 166 (retail's `CAI_`
    thunks `0x102e7e40` / `0x102e2730`, `0x1016a030`); the slot-440 translation of `0x2a`/`0x2e`
    for `CCineAI` and the `m_saved_troika_flags` word (A3's 8.2 / 8.6); `+0x5f44`, `DAT_1093412c`
    and `NAI_Hull::Bits` answering 0; `fields.md`'s sibling-class words at one offset; `m_fEffects`
    `+0x19c` and the `0x10274e30` anim-event arms; `MemberSync` `0x10337ca0` now reached;
    `DAT_10938040`; hidden makers never thinking; the `0x101618e0` copy-back field list (in the
    hand-off file). Owner questions: who is drawn during a controller scene (recovered
    after B, lands as its own commit), `enemy+0x3d4`, `DAT_10924edc`/`DAT_10924a6c`,
    `AddFlag2(0x10)`, the Classify 2/3 names, controller solidity, saving mid-beat (spec 0003
    stories 1–2). Records: the step and fold commits' messages; reports under
    `$ELYSIUM_WORK_ROOT/research/npc-kernel/story-5/` (`gateA/`, `smokeA/`, `B/`).

- [x] **6. The deletions and the mechanism seams.**
  The species `dead` rows are deleted by story 5's step 1 instead: 228 hand-written bodies and
  195 census rows, moved there by the owner on 2026-09-24 so that nothing dead is re-homed.
  Job, dead: every other `dead` row's body and test removed — the Debug, Debug10 and Debug10_2
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
  **Landed 2026-09-29**, orchestrated as five waves of disjoint-file lanes on one checkout
  (briefs, reports and reviews in `docs/specs/0019-npc-kernel-rework/story-6/`, the record until
  the commit). The vtmb-corpus server was down for the whole run, so every retail claim below
  rests on `docs/vtmb/`, the pinned image read directly, and the address comments already in the
  port; what that could not settle is RE-BACKLOG 40–43.
  - *The meter.* `kernel_verdicts.tsv` now spells a closed row: a `dead` row at `-` (body and tests
    gone; a `default:` / `registry:` literal the generator answers stays, because slot 327 is read
    by the rule `MeleeAttack2Conditions` and closing it changed the answer), a `mechanism` row at a
    service word (`CMC`, `UNavigationSystem`, `UPathFollowingComponent`, `TraceRetail`, `Chaos`,
    `Replication`, `FElysiumSaveArchive`, `Bake`, `UReflection`, `UEContainer`, `UAudio`, `Visual`,
    `CppDispatch`, `UEComponent`, or the owning spec `0010` / `0015` / `0018/3` / `0018/16` /
    `0002/28`) or at `seam:` (a hand body that is a one-line forward). `gen_kernel_shape` emits
    nothing for a closed slot nothing reaches and a silent default for one still dispatched; the
    census names the surviving layer. `kernel_lists --check` fails a body-closed row the port
    still cites; `--closed` counts. Generated data tables are not port sites.
  - *Dead: 653 of 653 closed.* The 16 Debug files and both Debug test files are gone; the ring, the
    two stamp words (`+0x1b2c`, `+0x1b38`; ~290 `SelectTrace` sites and 23 species writes), the
    scrambler (Combat10's bound is the literal 2), 39 `SquadSlotName` overrides, the slot-452
    `LoadedSchedules` constants, the undispatched sound and keyvalue slots, the standoff bodies,
    the think-stamp getters, the search timer, the Newscaster lister, the two `CAI_ScriptedSequence`
    empties. Ten dead rows were misverdicts and are `rule` again: the ten species `GetUsedHullBits`
    (each ORs its hull bit onto the Troika word, the listing's `OR AH,<bit>`), `GetLocalTaskId`
    (three rule bodies read it), and `OnRestore 0x1027bf50` (slot 130 runs on every load).
  - *Mechanism: 330 of 372 closed.* The retail ground step, walk execute, navigator move and
    physics tick are gone; `CheckOnGround` measures through the new floor-facts seam and tests
    retail's 4.0-unit reach; the save/restore twins are the generated walk, with the boss line's
    load-side resets moved to `OnPostRestore`; Precache is bake-time; replication and physics are
    nothing; Flex and Overlay bodies with no caller closed to 0010 and 0015; the test hull to
    0018/3. New seams on `IElysiumNpcMotor`: `SampleFloor`, `SetMoveIgnore`, `HasPath`,
    `SetHullSize`, `SetFacingTarget`, `SetYawSpeed`, `Face(yaw, rate)`, a route query with a start;
    the `FVisible` blocker cell is delivered; the render-mode and solid-`0x20` filter arms and the
    prop keyfields `blocks_traces` (21 authored) / `npc_transparent` (8,096) are carried; the
    slot-584 pass is the entity world's one-shot at build stamp + 0.8 s. **Kept, not replaced (42
    `hand:` rows):** the yaw ladders (slot 516) as data through the seam, the facing queue's
    weighted, expiring blend (slot 15), `NavigatorMoveStep` (0018/5's row-for-row `Move`), the six
    boss `OnPostRestore` bodies, and the 35 algorithms C3 lists (`report-C3.md`) whose service
    cannot yet answer them without changing the observable — RE-BACKLOG 43.
  - *Tunables: 438 inline cells → 0.* The overlay grew 114 → 526 rows (401 wave-4 rows plus the
    lanes' cells), every one re-read out of the image; a `ref` type records the 240-odd retail
    globals that are names, not values. Six stand-ins marked UNRECOVERED were plain `.rdata` cells
    and now read retail (among them the melee outer band 256.0 and the ScheduleHost wait default
    1000). The tunables checker learned function-local static ConVars.
  - *Slot 523* is `GetStepDownHeight` (the SDK-order guess `GetMaxJumpSpeed` retired, per 0018/6 R1).
  - *Read once the corpus reconnected (same day):* `UpdateYaw 0x102e1e20` truncates `m_YawSpeed` to an
    int and multiplies by the double cell 10.0 (the port is retail; slot 516 is degrees per tenth of a
    second, `shape.md` corrected); slot 7 `0x102e11f0` is the jump step, not a queue cancel; slot 18
    never rewrites the yaw word; the three facing adders' replace rules (`0x102d8cf0` / `0x102d8e50` /
    `0x102d8f20`) and the (importance, duration, ramp) order are in the port; `IsValidStealthKillTarget`
    ships the relaxed arm (`stealth.md` corrected); the Chang energy ball spawns up 50 / forward 40 /
    right -10 (the port had forward and up swapped; fixed). Second pass, same day: `DAT_10924edc` is
    `ent_trace_melee`'s parent asked `IsCommand()` with the answer dropped (no event); the Tzimisce
    task-distance sentinel answers 150.0; the MingXiao turn speeds 6 / 2 are the loader's immediates;
    `DAT_1070ba3c` is the skill level and autoaim scales by 0.9 at skill 1, blends 0.3 / 0.7 above
    (a `SkillLevel()` seam answering 1); `werewolf_teleport_in_time` is overlay row 527. RE-BACKLOG
    40–43 are banked.
  - *Named divergences:* slot 69 asked per think, not per contact; the ×10 yaw scaling (now read: retail's)
    SDK; catalogue props block only MONSTER traces unless `blocks_traces`; a stub fire on a closed
    slot is the falsifier and none fired in the live check.
  - *Validation:* editor build green; `Elysium.Substrate` 490 + 1,202 with warnings / 0 failed (from 504 +
    1,275: the dead families' tests went with them, 60-odd tests were added for the seams);
    `Elysium.Content` 23 + 2 / 0; `Elysium.Visual.NpcBody` 10 / 0; `Elysium.Map` 4 + 1 / 0;
    `kernel --check` 7 / 7; pytest 141; `unported.tsv` 450 → 393. Live, twice (`sp_tutorial_1` from a
    new game, then `sm_hub_1` for three minutes): 0 ensure / assert, no closed slot fired (the only
    stub fires were six open 29c chain surfaces, three of them — `GetMoveType`, `GetGroundEntity`,
    `SetGroundEntity` — given their one-line bodies over the entity's own words after the first pass,
    because the stub's 0 failed `CheckOnGround`'s `MOVETYPE_STEP` guard on every NPC), the hub's
    pedestrians walk, a save of the hub freezes 331 records with 0 absent and reads back as a
    payload, the two patrol cops fail `0xc` at link 731 as at row 09 (row 13's). A live load has no
    console verb; the restore path is `SaveRoundTrip` and the species round trips.
  - *Handed on:* RE-BACKLOG 42's port (0010 / 0015: the flinch records and the knockback tuples, read
    and banked here) and the per-row moves behind RE-BACKLOG 43's service map (0002). The skill
    level is a seam answering 1 until a difficulty setting exists.

- [x] **7. The reach cut.** (2026-09-29)
  Retail: none — a query over the ledger.
  Job: `kernel_ledger --reach <map>`: seeded from the map's population (classnames → retail
  classes → their `SelectSchedule`, `TranslateSchedule`, `StartTask`, `RunTask` overrides and
  the base arms) and the schedule texts those classes load (the task and condition identities
  their programs name), the query lists the functions, task identities and species rows the map
  reaches; `coverage.md` gains a per-map column. 0002's open stories are then cut to
  `sp_tutorial_1`'s list first and `sm_hub_1`'s second.
  Consumes: 3 (the programs say which tasks a class reaches). Provides: 0002's scope.
  Size: S–M. Effort: Sonnet / medium.
  **Landed 2026-09-29.** `kernel_reach.py` beside the ledger; `kernel_ledger --reach <map>` writes
  `docs/vtmb/npc-kernel/reach/<map>.md` + `.tsv`, and `coverage.md`'s band table carries one
  "Reached by" column per committed map, read from the TSV so the gate needs no exports. The seed is
  the published `<map>.entities.glb` (placed classnames plus every maker's `NPCType`) through
  `kernel_factories.tsv`; the texts come from the deployed corpus's `space.json` chains
  (`parentUnit`), parsed by the pipeline's own parser; the functions from the ledger's walk
  re-run from the map classes' vtables, `walk_from(seeds, classes)` fanning a `this` dispatch out
  to the map's classes only. Two over-approximations stated on every page: a class reaches every
  text its chain registers (selection is not simulated), and a slot-candidate edge is a candidate.
  - *The two cuts.* Tutorial: 6 retail classes (`CNPC_VHumanCombatant`, `CNPC_VVampire`,
    `CNPC_VPedestrian`, `CNPC_VRat`, `CNPCMaker`, `CCineNPC`), 359 of 691 texts, 243 task
    identities, 70 conditions, 3,367 of 5,187 functions (1,062 core), 141 species rows. Hub: 12
    classes, 390 texts, 244 tasks, 3,465 functions, 214 species rows. Both leave **2** task
    identities without a port arm, `TASK_TEST3` and `TASK_TEST4` — retail's test schedules
    `SCHED_TASK_TEST3/4`, which no selector returns (dead by selection). Two more were census
    omissions this cut found and fixed: `GTaskArms` had no row for Troika `0x11f`
    (`TASK_GET_PATH_TO_SAVEPOSITION_LOS_NOATTACK`; the port's constant renamed to the corpus name)
    nor for `CNPC_VScurrying` `0x14a` (`TASK_VSCURRYING_FIND_EVADE_PATH`, the rat's flee arm at
    `StartTaskSlot442`), though both arms exist. The generator behind the table
    (`pass-i/L13-scratch/task_arms.py`, out of repo) missed both; a whole-table check of every
    species arm against its row is owed (row 12's hand-on).
  - *What the cut says.* Reach barely narrows 0002: the twelve human-line classes share the
    `CAI_BaseNPC` / Troika bodies, so the tutorial reaches nearly every address and task the open
    stories cite; the only species narrowing is 25b's (3 of 22 slot-440 bodies). The sizes were
    out of date for a different reason — rows 06 and 06b had already ported most of what the
    stories name — so the cut was applied as an audit of each story against the port (five
    readers over 23 stories, each cited address checked in `Source/`), recorded per story in
    0002's spec as a *Reach cut* clause with the re-read size. Net: 23 stories from ~L median to
    XS–S; four of them (10j, 21b, 25b, 27) are done bar their record; the real remaining work is
    a short list (11's ambient executor still live, 12b's cover search wired to a stub, 16a's
    `DIST:ACCUM` never reading the accumulator, 10h's hunt builders answering false, 28's
    player-side producer, 21a's two inputs and two typed-state reads, 16c's apply-path calls,
    16b's two flat-table consumers, 10d's `+0x60dc` mirror).
  - *Gate.* `kernel --check` 7 of 7 (19.8 s, `coverage.md` with the new column); `kernel_ledger
    --reach sp_tutorial_1 --reach sm_hub_1 --check` 17 of 17; `Elysium.Substrate.Schedule` 32 / 0
    after the census row; pytest `test_kernel_reach.py` 5 new cases; the reach pages are excluded
    from the oracle citation scan (they are the ledger's own reflection).
  - *Handed on.* RE-BACKLOG 44: the reached functions the stories name that carry no verdict.
    Two findings no open story owns, recorded in the tracker under row 12: the NPC input surface
    (19 of retail's 33 NPC inputs are not registered; none fired on the two maps, but the corpus
    fires `SetInvestigateModeCombat` 69×, `StayEntrenched` 67×, `SetInvestigateMode` 66×,
    `FleeAndDie` 27×, `MakeInvincible` 19×, `SetFollowerBoss` 15× across the 108 maps and the
    scripts), and the VSound table (`SpeakVSound` is a seam, so every `PLAY_SOUND` step is silent;
    `0x101f5950` carries no verdict and no spec loads the table). **This closes 0019**: nothing
    remains in this spec.

- [x] **8. 29e closes under the strict verdict.** (2026-09-28)
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
    *Settled after the landing (owner's ask, same day):* the 60 contradiction rows where the first
    judge sided with the packet or could not decide got a second judge (Opus 5.5, three briefs):
    35 packet, 19 checklist, 3 both wrong, 3 both right (a thunk versus its body); the 18 drilled
    rows settled as "neither reading" got the same second judge: 14 upheld, 4 corrected; the 84
    one-sided rows were filled in by the reader that had left them (one remains). The second
    judge's tables sit under each packet's contra list. Cost: 4 Opus subagents, ~0.9M tokens.
    The 38 rows where the packet or both were wrong are closed as recoveries, not decisions: each
    owning verdict row's evidence in `kernel_verdicts.tsv` carries a `CORRECTED 0019/8 pass R`
    clause with the settled statement and the listing lines (32 rows amended, some with two), the
    checklist regenerated, `kernel_ledger --check` and `kernel_lists --check` green. Verdicts
    unchanged.
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
  **Pass I landed 2026-09-28** (wave 1; the method is
  [story-8-execution-plan.md](story-8-execution-plan.md)). Twelve porter lanes, L01–L12, each in its
  own worktree on the shape commit `6497a0ce`. That commit declared every pass-I override and made it
  forwarding, created the 80 family files empty, took `unported.tsv` 778 → 467 and pinned
  `story8-forwarding.tsv` at 210 rows. Each lane was gated by `kernel_gate` (addresses, skeleton arm
  coverage, seam searches, hot-header freeze, residue, tests, twins). Each was then integrated by a
  fresh full read of every body against `vtmb_asm` / `vtmb_code` before its one commit on `main`.
  **Nine families are on `main`: 204 `rule` rows, plus two Damaged19 rows.** Spawn19 is integrated
  but not merged. RunAi19 and Think19 are wave 2. Reports:
  `$ELYSIUM_WORK_ROOT/research/npc-kernel-checklist/pass-i/` (`L<nn>-report.md`, `L<nn>-integration.md`,
  `StartTask19-integration.md`). The walked prose is folded into the oracle, one section per retail
  address, under `schedule-kernel.md`, `conditions-and-states.md`, `lifecycle.md` and
  `authored-control.md` § "Story 8, family …".
  - *Misc19* (L11, `a17ed6a9`): 28 of 31 rows. L12 then ported the two defeat latches `0x10395ce0` /
    `0x1039ea60`, making **30 of 31**. `CCineAISchedule::vfunc586` `0x101a98c0` stays open; it drives
    L10's movers. `SetEnemy` `0x10279a50` and `ChooseEnemy` `0x10279dd0` are now retail's for every
    NPC, base-only included. The review fixed six things:
    - Slot 598 `0x102b4fe0` and Cop `0x10372dd0` forget through `AddEntityRelationship` `0x10332ca0`,
      which overwrites at any priority. The port's `SetEntity` had refused a lower one.
    - `ClearMemory` `0x102dfaa0` now unlinks the record.
    - `ShouldChooseNewEnemy` tests slot 158 `IsAlive`, not dead-or-hidden.
    - `DisconnectFromSquad` `0x1026d050` writes no flag.
    - Anim events 0x3e9–0x3eb reach `AllowInterrupt` and the cine, hint and place outputs.
    - The feed end runs `0x1026d160`. Before, every fed NPC stayed oblivious.
  - *Script19* (L10, `576a06fc`): **20 of 20**. The review found:
    - `ScheduledFollowPath` uses goal type 3.
    - `SetGoal`'s flag 1 clears the path tolerance. The tolerance is the path's `+0x28`, not
      `m_flGoalTolerance +0x6320`.
    - A refused route fails through `OnNavFailed(0xc)`.
    - `CCineNPC` and `CCineAI` save and restore `m_fEffects`, with `EF_NOINTERP` on the teleport.
    - `ExitScriptedSequence` is wired at both SCRIPT arms.
  - *Conditions19* (L07, `617497a1`, rebased as `72b64eca`): **20 of 20**. The review found:
    - **The throw lines ran on the wrong side.** The right vector was the left vector, so the
      Hengeyokai's line started 80 units to its left, and Tzimisce's `-tzimisce_throw_pos_y` was
      mirrored the same way.
    - The wall ray and both throw lines ran on a hull seam that never hits. They now use the live
      world ray.
    - `0x102dfed0` answered the enemy's live origin in three places. Retail answers the memory
      record, else the last position-only record, else `vec3_origin`.
    - The NaN edges of eleven ordered compares were wrong.
    - Sensing is split: `PerformSensing` `0x10310710` stands alone, so slot 481 is the only producer
      of the LOS debounce.
  - *Damage19* (L09, `414545a6`): **26 of 26**, with 279 of 279 arm sites cited. Slots 318 and 319 are
    unified on the margin ladder. **It is not yet on the live path.** Nothing in play reaches slots
    142 or 390, because `CBaseCombatCharacter::OnTakeDamage` `0x1032ef60` and `0x103302e0` are still
    29e stubs. The port's `OnDamageCommitted`, `RememberDamage`, `AccumulateDamage` and `GatherDamage`
    therefore still run, marked `STORY8-TWIN`.
  - *Select19* (L06, `abfae21c`): **31 of 31**. **The selector pair `0x1028a260` is wired live**: it
    clears `+0x1b2c`, runs slot 437, then slot 438. `FElysiumNpc::SelectSchedule` is now
    `SelectNewScheduleRetail`, and slot 438 on the bare Troika line is `0x102af660`. Deleted: the
    port's guessed selector ("CHOSEN, NOT RECOVERED"), `SelectIdle/Alert/CombatSchedule` and
    `SelectLawSchedule`. The review found:
    - Slot 597 `0x102b4fb0` overwrites at any priority.
    - Ming Xiao's `HasCondition(9)` is a read, not a clear, and its tuning is the
      `Ming_Xiao_Info/General` slice of Rules.txt.
    - The Hengeyokai and Tzimisce found arms were stand-ins; both are ported whole.
    - The witnessed-incident consumers are now retail's.
    - The landed tests that pinned port-invented selections are corrected to the listing.
  - *RunTask19* (L05, `8cc9e23a`, rebased as `c9977c23` and `1769381c`): **23 of 23**. Both jump tables
    were decoded from the image (base 28 entries, Troika 57). The review found:
    - About twenty compares had the wrong NaN or equality edge.
    - The last-known-position walk did not handle a null enemy.
    - With no player, the facing falls back to worldspawn.
    - The melee roll was read from this NPC's array; retail reads the enemy's.
    - `0x103498b0` is ported.
    - Two constants read from the image: `_DAT_1049a17c` is 190.0, and the SabbatLeader cell
      `0x1093c33c` is 0.55.
    - The yaw chain runs through slot 515 in retail's frame.
  - *Werewolf19, Boss19 and Damaged19 slot 330* (L12, `2eca094a`): **17 + 11 + 1 rows**. The review
    found:
    - `ShareEnemyWithAlly` and `StartTransformation` overwrite through `AddEntityRelationship` and the
      new `AddClassRelationship` `0x10332aa0`.
    - The tentacle "death sound" `0x1039f310` stops the flop loop (`SND_STOP`). The packet and the
      checklist had it playing.
    - The morph fade floor is 0.0.
    - `HengeyokaiSkin` is folded onto `m_nSkin`.
    - The clear-all `0x102dfc10` runs slot 56 once per record.
    Every stand-in the other lanes left for L12 is now bound to the real body.
  - *StartTask19* (L01–L04, lane commits `cb0ed36a`, `7bb820fb`, `a97ce8f6`, `a059d04c`; integrated as
    `86ceba23`): **27 of 27**, the 26 rows plus `CNPC_VCop` `0x10371b70`. 3,460 of 3,460 skeleton sites
    are cited. Two review findings changed the bodies:
    - **A task-id-kind defect all three giants shared.** `FElysiumScheduleStep::TaskId` is the GLOBAL
      id, but retail's `iTask` is class-local. Base, Troika and Troika-tail switched on the raw id, so
      a real step would have hit `default:` every time; their fixtures built local ids, which hid it.
      Now one `GlobalToLocalId` through slot 450 translates the id, and `CurrentRetailTaskNumber`
      `0x1028a150` answers the class-local id.
    - **The `SetGoal` unification.** Five lane adapters (L01, L02, L03, L04, L10) became one
      `0x102ecd20` body, with `0x102f1dc0`'s route window and `0x102f28a0`'s clear. The tolerance
      goes to `path+0x28`, not `m_flGoalTolerance`.
    The review also found:
    - `RestartIdealActivity` `0x10289ee0` is ported.
    - Slot 216 `SetAbsOrigin`, at seven sites, called an unported stub, so the body never moved.
    - The Ghoul unaware tables are read off the image.
    - The kick goal and the step-back ran in the wrong direction.
    - `m_lifeState` is now one word.
    - The lateral-cover pair is one body.
  - *Spawn19* (L08, replayed from `story8/int-4` onto `main` as `14697e2b`): **48 of 48**, with 373 of
    373 arm sites cited. Troika `Spawn` `0x10298d30` is live as `FElysiumNpc::Spawn`, and `NPCInit`
    runs at spawn, not at `Activate`. The review found:
    - The ScriptHide, `m_lifeState` and `m_fFlags2` twins; each is folded onto one body or word.
    - The runner's `OnRestore` calls `SetAbsoluteAttackExtents` `0x1009b060`.
    - The anim-link removal is `UTIL_RemoveImmediate`.
    - The closest-NPC offer is `0x47c34ff3`.
    - Three `SetClass` sites are `AddClassRelationship`.
    L12's death bodies and TentacleHPInitial are wired to it. The walk is in `lifecycle.md`
    § "Story 8, family Spawn19" and § "The death chain, kill to corpse".
  - *Named divergences*, each named at its line:
    - Crash guards where retail dereferences null or stale data.
    - A zero, NaN or `vec3_origin` where retail reads uninitialised stack.
    - Bounds on the round-robin hint walks and the tentacle spot search.
    - A refused body claim in `SetGoal` counts as "no route attempted".
    - Goal flag 2's node route takes the location arm; there was no node graph. (0018/4 landed
      the place set 2026-09-29; the node route itself stays 0018/5's, so the divergence stands.)
    - The CRT `rand()` draw uses the port's `NpcSchedule` stream.
    - The Tentacle clock is `curtime`.
    - The SabbatLeader prologue measures from the origin.
    - `bDeathCommitted` fires once.
    - The world ray ignores `CONTENTS_MONSTER`.
  - *Modernizations*, visual-only:
    - `MotorUpdateYaw` drives `Motor->Face`.
    - The selector, ideal-state and `TaskFail` stamps are carried as retail-address members, with
      the `__FILE__`/`__LINE__` words absent (the agreed modernization).
    - DevMsg and DevWarning go to the schedule trace.
  - *What stays partial pending wave 2.* The bodies are reached by tests and direct slot calls. The
    running loop still runs the port's twins: **82 `STORY8-TWIN` markers in 13 files**. They are:
    - the `ElysiumSchedule.cpp` `BeginTask` / `ContinueTask` cases (the op list is in
      `StartTask19-integration.md`) and their verbs in `ElysiumNpc.cpp` and `ElysiumNpcBase.cpp`;
    - `ElysiumNpcEnemy::GatherConditions`;
    - `ElysiumNpcSenses` `Tick` and `GatherEnemyLos`;
    - `GatherHearing` and `GatherCommittedEnemy`;
    - the damage twins;
    - the patrol inputs and `BeginScriptedSchedule`;
    - `FElysiumNpc::OnKilled`.
    Also carried to L13:
    - RunTask19's switches still compare the raw global id, the defect fixed on the StartTask side.
    - `ReleaseMotorHintYaw` stays inert until the mover keeps `motor+0x34`.
    - `MotorDeltaIdealYaw` answers 0, so every facing arm completes on its first pass.
  - **Wave 2 landed 2026-09-28.** The porters were L13a and L13b, and the L13 integrator worked on
    `story8/int-3`, merged as `d1d93773` (`e24e5ef9` carries the verification). Reports:
    `L13a-report.md`, `L13b-report.md`, `L13-review.md`, `L13-checks.md`, `L13-suites.md`,
    `L13-postmortem.md`.
    - *RunAi19* (L13a): **17 of 17**. It covers base `RunAI` `0x1026f110`, Troika `0x1028fcc0`,
      `RunAlternateAI` `0x1028fd80` with door modes 2 and 3, and the fourteen species overrides.
    - *Think19* (L13b): **17 rows**, the 15 plus Damaged19's two `NPCThink`s (Werewolf `0x103cb590`
      and PlayerController). It covers Troika `NPCThink` `0x10292de0` in retail phases, base
      `0x1026ca80`, the AI gate `0x1026c3d0`, `UpdateCharacter` `0x10298070` and the species.
    - **The loop is live.** It runs `NPCThink` (431) → `RunAI` (432) → 433 → `MaintainSchedule` →
      437/438 → 442/444.
      - NPC damage enters slots 142 and 390 through the `0x1032ef60` / `0x103302e0` hand bodies.
      - Death runs through slot 144 (`0x1032b9b0`, `CreateCorpse` `0x1032c0e0`, `Die` `0x103392c0`).
      - The patrol is the path object's program.
      - Slot 444 now switches on the class-local task id, closing RunTask19's half of the id defect.
      - Troika `ScriptHide` `0x102c1ce0` is ported, which closes Damaged19.
    - **The twins are deleted: 82 `STORY8-TWIN` markers → 4 named survivors.** Each survivor carries
      its reason at its site:
      - the scripted-beat owner, which waits for spec 0003's `SCHED_AISCRIPT`;
      - the dialogue clip hold;
      - the interesting-place executor;
      - the leg walker for `0x101a98c0` modes 4 and 5. The navigator builds no path-corner goal for
        `ScheduledFollowPath` `0x102801e0`. Modes 1 and 2 now run retail's `ScheduledMoveToGoalEntity`
        `0x102800c0`.
    - **The review** (`L13-review.md`, 49 rows read against the listing) found material defects, and
      each one is fixed:
      - **Dead NPCs revived after a load.** Slot 158 `IsAlive` read `m_lifeState` (`+0x200`), which
        nothing saved. `+0x200` is now one word bound to its datamap `SAVE` row.
      - `CreateCorpse` never took its static-corpse arm. It now takes all three arms: the player
        self-cast `+0xa8` and `MiscFlag 0x80000` (`0x1032c22d`, `0x1032c288`) take
        `SpawnStaticCorpse` `0x1032be80`, and the ragdoll arm is the rest.
      - The stealth kill had no attacker. It now builds `0x10165d90`'s packet with the player as
        inflictor and attacker, then runs slot 144 and slot 403.
      - The `HealthBuffer` arm was invented. It is now `0x103302e0`'s:
        `trunc(DAT_10739a68 × dmg × 0.01)`, `SubBase` or `SetBase` plus the Bloodshield end
        (`0x10330746..0x10330847`).
      - `RunAnimation` `0x1026c540` ran after `PostRun`. It now runs inside `PostRun`
        (`0x1026c8c4`), ahead of the anim events.
      The flinch order (row 8) stays a named event-order divergence until a hit producer dispatches
      slot 141.
    - *Verification:*
      - `research kernel --check` 7/7; `kernel_gate --all` 13/13 families.
      - `Elysium.Substrate` **1,710 / 0 failed** (`reports/tests/20260928T135654.944946Z-elysium-substrate`);
        `Elysium.Substrate.Npc` 1,135 / 0; `Elysium.Content` 14 / 0; `Elysium.PlayerWorld` 1 / 0.
      - pipeline pytest 4,224 passed.
      - Two stale assertions were corrected to retail (`NpcKernelMotor.Probes` `0x1008f120`; slot 158
        `0x100b4dc0`).
    - *Post-mortem* (`L13-postmortem.md`): about an hour of the six-hour session was avoidable under
      the existing rules. The lessons:
      - Batch every hot-header edit into one pass and one rebuild.
      - Before deleting a declaration, grep its callers, `Tests/` included, and fix them in the same
        edit.
      - Run the full suite once, after the last source change, with family filters in between.
      - Regenerate once, at the end.
      The tool fixes are on `main`: `research kernel` runs the seven generators in one process
      (`937d975d`, `kernel_ledger` 470 s → 7 s); `kernel_gate --all` builds one Source index
      (`b87058da`); `elysium test` has an idle watchdog (`9cdf6b5a`).
    - *Smoke* (`pass-i/L13-smoke.md`, two runs on 2026-09-28): `sp_tutorial_1` and `sm_hub_1`, driven
      through the `elysium` MCP. The first run, on the wave-2 tree, found one real divergence: no NPC
      whose `NPCInit` ran after the map's first second ever thought, because `CAI_BaseNPC::NPCInit
      0x10273390`'s late arm (`0x10273a5x`) calls `NPCInitThink 0x10273aa0` inline and the port's else
      arm only counted; `CNPC_VCamera::NPCInit 0x103692c0` has the same arms. Fixed as `2fcc21cb` (the
      think, `0x10273760` and `InputSetRelationship 0x10273790` moved to the base, which owns them;
      test `NpcKernelLifecycle19.LateNPCInitThinksInline`). The second run, on `main` at `2fcc21cb`:
      every maker child and every travelled-to hub NPC is admitted and thinking, hub pedestrians walk,
      `havenbum`'s OnMapLoad greeting opens; the porch controller is created and removed at +5 s, the
      idle `0x6b` loop runs; 0 ensure/assert, 0 `No StartTask entry` / `No RunTask entry`, the only
      TaskFails are three `0xc` "Don't have a route" on `patrol_cop_north`. Stub tally against the
      60-surface baseline: one new surface, `CBaseAnimating::SetAttackExtentsForSequence 0x10090c80`
      (fires at every spawn); 54 baseline surfaces unreached because the smoke has no combat or death.
      The open observations were verdicted against the listing (`pass-i/L13-smoke-verdicts.md`): the
      patrol stalls were one story-8 divergence -- `TASK_NEXT_PATROL_POINT`'s helper `0x102aa9e0` had been
      ported without `NextPoint 0x10307b80`, the path release `0x1029f5d0` and the interest draw
      `0x1029f650`, so no patrol index ever advanced, and the port handed a `use_interesting` body to the
      ambient loop where retail's selector tests the patrol arm `0x102af6b6` before the interest arm
      `0x102af6f3`. Fixed as `5b0a5a40` (three tests). A third run on `main` at `5b0a5a40`
      (`pass-i/L13-smoke-r3.md`): the monk walks 5→1 and wraps (repeat 999→992), `sentry2` walks 1→2→3
      and back under `0x67` and is never handed to the interesting-place loop; the earlier passes hold.
      The think cadence and the "gathered t=-1" display are faithful (`0x102936a1..c0`, `0x1026f1ad`).
      Left with other rows: the monk's wrap leg and the hub cops' legs fail `0xc` "Don't have a route"
      over jump link 390 / unconnected type-0 nodes (navigator row 08, geometry row 09); the mover reports
      success ~83 units short of a node under tolerance 20 (row 08); `NPCInit` compares a per-level
      `curtime` while the port's clock does not restart (world/session); `SetAttackExtentsForSequence
      0x10090c80` is a `CBaseAnimating` stub reached at every spawn (animation); a hub script reads
      `prostitute_1.classname`, which the port's entity wrapper does not expose (scripts); retiring the
      ambient loop's case-1 arms stays with 0002/11.
  - *Tests:* `Elysium.Substrate` **1,326 → 1,613, 0 failed**, at StartTask19
    (`reports/tests/20260928T032303.105515Z-elysium-substrate`). Family suites: Misc19 31, Script19
    23, Conditions19 32, Damage19 28, Select19 32, RunTask19 46, Werewolf19 17, Boss19 12, StartTask19
    66. After wave 2, `Elysium.Substrate` stands at **1,710, 0 failed**. The thirteen story-8
    family suites pass 273/273 + 117/117, and Schedule plus the landed families pass 88/88.
  - *Pins:* `story8-forwarding.tsv` **210 → 71** (RunAi19 16, Think19 13, Spawn19 41, Damaged19 1).
    `kernel_shape --unported` **778 → 467** at the shape commit, since a forwarding override counts
    as an override; it held at 467 through pass I. After wave 2, forwarding is **210 → 0** (71 → 0
    in wave 2), and `unported.tsv` is **778 → 449**: 467 → 451 at Spawn19, where slots 617/618 went
    virtual, then 451 → 449 once `ScriptHide` / `ScriptUnhide` went virtual.
  - **Pass C landed 2026-09-28.** The map smoke above; the rename commit: `ccefc765` (108 pure moves,
    100 % similarity: the `19` suffix dropped, bare-digit parts where a concern file existed, Boss 2,
    Conditions 2, Damage 2/3, Lifecycle 2, Misc 2, Werewolf 2, and the four Tests files) and
    `6376f797` (references: the `.inl` includes, `kernel_gate` / `kernel_story8_shape`, the docs).
    Suite-name strings keep `19`. After the rename: checks 7/7, gates 13/13, the seventeen family
    suites 446/446 (equal to their wave-2 counts), pytest 4,279. Closed on `main` with the final
    joined launch recorded in the tracker.

## Build order
1 → 2 → 3 → 4, with 8's pass R in parallel from 1 on → 5 → 8's passes I and C → 6 → 7 (amended
2026-09-23; the stated order had all of 8 before 5, see story 5). Relevant bindings from 2 precede
completion of 0018 story 2's bake/adoption, while its native class declarations can precede
binding completion. AIN projection is 0018/4's independent format boundary. 5 lands
alone on a branch. When 7 lands this spec closes and 0002's build order takes over.

## Seams
- Provides: the loaded programs and id spaces to 0002; the generated bindings and the tunables
  to 0018 and 0002; the class tree every species story lands on; the reach cut that scopes
  0002; the delete and seam lists.
- Consumes: the ledger and its overlay; the datamap replay; the pinned `vampire.dll` through
  the export lane; 0018 story 5 for the path seam.
