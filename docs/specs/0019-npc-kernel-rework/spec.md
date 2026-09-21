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

- [ ] **2. The datamap bindings, generated.**
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
  `Elysium.Substrate.NpcKernelBindings.Counts` holds the counts. **Pass B, open:** the
  `CBaseEntity` / `CBaseCombatCharacter` keys (200), which need the census and the shape map
  extended to the chain words the port stores on `FElysiumNpc` first; a binding surface for
  component-struct members; the save walk over `SAVE`-only rows, landing together with the
  deletion of the nine `Serialize*Block` helpers so no field persists twice; and the non-NPC
  classes as 0018 stands them.

- [ ] **3. The schedule seam: texts, id spaces, flag tables.**
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

- [ ] **4. The tunables table.**
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
