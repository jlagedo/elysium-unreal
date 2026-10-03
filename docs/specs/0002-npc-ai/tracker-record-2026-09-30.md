# Tracker — 0019 · 0018 · 0002, one serial sequence

**What's next = the first unticked box** (row 13 closed 2026-09-30; row 14 is next). One story at a time, top to bottom. Tick the box here
when the story's own box is ticked in its spec; the spec stays the source of truth for the text.
Specs: [0019](0019-npc-kernel-rework/spec.md) · [0018](0018-world-ai-infrastructure/spec.md) ·
[0002](0002-npc-ai/spec.md). Built 2026-09-21 from each spec's `## Build order` and `Consumes:` lines.

Already landed: 0018/1, 2, 3, 4, 21-1 … 21-7 · 0019/1 · 0019/2 pass A · 0019/8 = 0002/29e (row 07 closed 2026-09-29) · 0018/5 (row 08 closed 2026-09-29) · 0018/6 (row 09 closed 2026-09-29) · 0019/6 (row 11 closed 2026-09-29) · 0019/7 (row 12 closed 2026-09-29; **0019 is closed**) · 0018/7 (row 13 closed 2026-09-30).
Stopped incomplete: 0018/21-8 (the whole-corpus map pass, 2026-09-21) — 37 of 108 maps green.
It fed work to rows 08 and 18 and opened 21-9 and 21-10; 21-10 blocks its close.
What the three specs still owe a READ (not a build) is tracked in [RE-BACKLOG.md](RE-BACKLOG.md).

## Decision owed (does not block 01–06)

- [x] **0018/4's open question**: `TASK_GET_PATH_TO_RANDOM_NODE` is a random walk over AIN links,
  not a draw from places. **Decided by the owner 2026-09-21: (ii), as the capped point pick** — a
  retail place at the order's distance, the route Unreal's, the walk's guard `0x14` one baked
  number per map and agent (`20 x` the median hop). The asset stays places and no links; the rule,
  what it gives up and the upgrade held in reserve are in story 4's decided block. Row 07 is
  unblocked.

## A — finish the in-flight group, then the data seams

- [x] **01 · 0018/21-7** — The producer's debt: structural entity read, the five flags, `Use` and `times 0`. M–L · Opus/high.
  First because it changes what every later bake loads; recovery is done, the job is mechanical.
- [x] **02 · 0019/1** — The strict verdict pass → the delete list and the seam list. M · Fable/high.
  Landed 2026-09-21: `rule` 2,205 → 1,354, `dead` 0 → 657, `mechanism` 175 → 380; the lists are
  `docs/vtmb/npc-kernel/delete-list.md` and `seam-list.md`, held by `kernel_lists --check`.
  It found twenty NPC classes nothing can instantiate (`population.md`), which shrinks rows 06,
  10 and 11, and that `ai_goal_standoff` is unauthored, which halves row 21.
- [x] **03 · 0019/2 pass B** — Bindings: the 200 `CBaseEntity`/`CBaseCombatCharacter` keys, component-struct members, the `SAVE` walk + delete the nine `Serialize*Block`. M · Opus/high.
  The non-NPC classes are not part of this row: each lands with the 0018 story that stands it.
  Landed 2026-09-21: the whole NPC entity chain is generated (four new binding classes, 215 keyed
  rows bound, 37 recorded gaps), the sheet with it; `ElysiumAddClassFieldVia` is the
  component-struct surface; `AddNpcSaveFields` carries 199 of the 216 `SAVE`-only rows. **The nine
  blocks did not go** — measured, they duplicate only three words, and those were hand rows, now
  deleted. The real double-persistence is in the seven component `Serialize` methods below them,
  which is **0019/2 pass C** (row 03b). Two retail spellings corrected: `gender` / `base_gender_`
  and `base_active_active_active_dominate`.
- [x] **03b · 0019/2 pass C** — Split the component serializers against the generated SAVE walk and
  move their load-side validation to a post-restore hook. M–L · Opus/high.
  Landed 2026-09-21. The overlap measured **62 words in three components**, not ~104 in seven — the
  other four answer retail `FIELD_EMBEDDED` rows and stay; `save_walk_overlap` holds it at 0.
  `FElysiumNpcWitness::Serialize` is gone entirely. **The hook did not have to be built:** retail's
  slot 130 was already ported whole (`ElysiumNpcLifecycle2.cpp:551`) with only tests calling
  it, so pass C wired it — `FElysiumEntity::OnPostRestore`, dispatched by `ApplyEntityRecord` — and
  the schedule re-find by name and task CRC is live for the first time. The nine blocks are four,
  every dead version gate is deleted (schema 39, floor raised), and retail's `AIExtendedSaveHeader_t`
  is the leaf's one hand block, as it is retail's.
  It found and fixed a defect: `OnDormancyChanged` re-armed the four `m_flNext*Think` rows on every
  load, so retail's saved cadence was discarded and every restored NPC thought immediately. It named
  one it did not fix: the port restarts the restored program where retail resumes it, and the
  restart's own slot 435 releases twelve of the walk's per-run rows.
  New net: `Elysium.Substrate.NpcKernelBindings.SaveRoundTrip` (218 rows, 23 named exceptions); six
  component suites moved off `Npc->Serialize(Ar)` onto the real `Freeze`/`ApplySnapshot` path.
- [x] **04 · 0019/3** — The schedule seam: 691 texts, id spaces, flag tables, the parser; the hand
  programs deleted. L · Opus/high. **Complete, delivered in six passes** (owner's decision
  2026-09-22): P the pipeline seam, then A id spaces, B parser + manager, C corpus loader + the
  witness, D the switchover (indivisible), E the deletions and the coverage meter.
  **Pass P landed 2026-09-22**: 57 units publish, `uv run elysium import ai-schedules` deploys 748
  files, the parser reads all 691 texts and refuses none, and `schedule_owner_survey --check` holds
  the oracle's per-owner table against the image. Three of the story's own numbers were wrong and
  are corrected in the spec — the base registers 68 names not 57, the texts are in `.data` not
  `.rdata`, and there are 28 hand programs not 19 with 295 non-test `EElysiumScheduleId` sites
  across 35 files, not 20 users.
  **Pass A landed 2026-09-22**: `FElysiumIdNamespace` and `FElysiumLocalIdSpace`, the four retail
  id-space bodies ported arm for arm, nothing wired and nothing deleted. It carries
  `m_translatedTop`, the sixth word `FScheduleIdSpace` never had — `GlobalToLocal` bounds a global
  id against it, and the old body bounds against the local top, which is a live range bug the
  moment real ranges land. `Elysium.Substrate.ScheduleIdSpace` 5 of 5; `Elysium.Substrate`
  1249 of 1249.
  **Pass B landed 2026-09-22**: the parser `0x1030d850` and the engine tokenizer it reads through,
  ported arm for arm — `FElysiumScheduleProgram`/`FElysiumScheduleStep` (`CAI_Schedule` and
  `CAI_Task` field for field), seventeen prefixes, every row of the failure table, the two things
  that do NOT fail, `FElysiumScheduleManager`, `FElysiumSymbolRegistry` (the three interning
  run-time tables), `ElysiumMiscFlags::ParseScheduleIndex` and
  `FElysiumNpcConditions::SetOrdinal`/`HasOrdinal`/`ClearOrdinal`/`Difference`. The eleven
  compiled-in operand tables were diffed row for row against the deployed `vocabulary.json` and
  agree. `EElysiumTaskOp` splits a task's IDENTITY (the corpus's global id, 441 of them) from its
  BODY (the 23 this runtime runs); `FElysiumTaskOpTable::Measure` is the coverage meter's engine.
  Two stated divergences: a task record stores the global task id (an operand still stores the
  local one — the data word is a float), and `Activity:`/`Model:`/`SOUND:` intern instead of
  Erroring. `Elysium.Substrate.ScheduleText` 9 of 9; `Elysium.Substrate` 1258 of 1258.
  **Pass P repaired 2026-09-22**: the census read `Init` only out of init bodies, and neither root
  makes one there — `CAI_BaseNPC` initialises in `0x1030c4e0` (not an owner, it calls no parser)
  and `CAI_BaseNPCTroika` through the helper `0x102be9f0`, whose three arguments arrive as the
  return values of one-line getters. Twelve of the fifty-six units had no parent at all. Both are
  read now, and `_prove_parents` refuses the seam if a SCHEDULE space ever parents on a space no
  unit initialises. `_attach_shared_spaces` also does what its docstring promised: an MSVC RTTI
  walk (`rtti.py`) reads slot 580 off every vtable whose base chain names `CAI_BaseNPC`. That is
  **77 classes over 58 spaces against 56 init bodies** — not four pairs: 21 classes have no body
  and run the vocabulary of the class above them (`CPayphone` runs Troika's, `CGenericNPC` the
  base's). Every count unchanged; the graph now has one root and a maximum depth of six.
  **Pass C landed 2026-09-22**: `FElysiumScheduleCorpus` loads the deployed corpus —
  `schedules 691 in 56 spaces (0 skipped), 0 parse failures; tasks 4138 steps over 514 identities,
  24 ported (5%), 490 unported reached by 1611 steps`. That last clause is the story's real
  deliverable: the first honest measurement of how much of VtMB's task vocabulary this runtime
  runs, with the per-identity reference count that makes it a work queue. Also landed:
  `ElysiumScheduleNumbers.h` (nineteen ids, each checked against the corpus by `VerifyNumbers`,
  which Errors with BOTH numbers), `TASK_FIND_COVER_FROM_ENEMY`, and the witness as a test —
  `SCHED_TROIKA_CHASE_ENEMY_FAILED` runs retail's own twelve-task text with its operands asserted.
  One recovery banked: **retail SORTS its (name, id) pairs by local id before registering**, and
  the oracle said so in one word that the first port skipped. A space takes its local base from its
  first registration and refuses every later id below it, so append order silently loses names —
  three owners (`CNPC_VZombie`, `CNPC_VAndreiBlood`, `CNPC_VWerewolf`) and then every remaining
  text those classes own. `Elysium.Substrate` 1264 of 1264.
  **Passes D–E complete 2026-09-22.** The runner now
  reads the corpus, its task records are the two retail words, and the closed schedule enum and
  28 hand programs are gone. The last two were based on a misreading: the director's 9 / 19 are
  activities, not schedule IDs; its local 2 goes through slot 440 to Troika's loaded IDLE_PATROL.
  Review connected the NPC's own schedule space, removed the old translation whitelist, routed
  NPCFlag operands into both words and joined local live conditions to global parsed masks.
  The chase-failure witness now executes through the real NPC and recording geometry/motor,
  including its successful twelve-task route and its failure to translated STANDOFF.
  `elysium.schedules [filter]` reports every unported identity and its reference count.
  Validation: editor build green, **1,267 / 1,267 `Elysium.Substrate` tests**, and
  `kernel_ledger --check` matches all 13 generated tables (coverage refreshed). Live census:
  **691 programs, 4,138 steps, 26 bound task bodies, 488 unported identities / 1,602 steps**.
  Test report: `$ELYSIUM_WORK_ROOT/reports/tests/20260923T003145.743425Z-elysium-substrate/index.json`.
  **World seams still owed by later rows:** cover-node positions/cooldowns/hint claims (0018/4–5),
  character sight occluders and the flying mover. Lateral cover uses native capsule probes,
  a named geometry modernization; candidate order, 48-unit steps and task timing stay retail's.

- [x] **05 · 0019/4** — The tunables table. S–M · Sonnet/medium. **Landed 2026-09-22, grown to M**
  by the owner's call to wire every seam, not just the store.
  - *The table:* `kernel_tunables.tsv` has 99 rows (51 cells, 48 ConVars). `gen_kernel_tunables
    --check` re-reads every row out of the image at its width; ConVars are checked through their
    static initialisers. It renders `ElysiumNpcKernelTunables.h/.cpp`.
  - *The oracle:* the check corrected `werewolf_draw_hints` (`"0"`, not `"40"`) and named six
    debug ConVars, one of which, `ent_trace_conditions`, ships `"1"`.
  - *The migration:* 83 inline constants now read the table, the two ConVar stores and about a
    score of per-family ConVar seams are deleted, and about twenty 0.0 stand-ins are retired.
    Among the values that now ship are the melee range 100, move-facing 1, the view-cone apex 40,
    the hint height 64, `CAI_Hint::Spawn`'s stored ×0.5 / +43, and `NPCInit`'s health 10.
  - *Misreadings fixed at the listing:* five, among them the crow's scale clamp (a clamp AT 1.0,
    not a flatten) and `CAI_Motor#4`'s missing normalise.
  - *Validation:* 1,268 / 1,268 `Elysium.Substrate`, `Elysium.Content` 14 / 14.
  - *Review corrections (2026-09-23):* convert the claw-origin ConVars to centimetres at their
    consumer and count ConVar reader aliases as covered; both have regression coverage.
  - *Hand-offs:* 344 inline cells over 91 files stay as row 11's queue (`--report`). Found
    pre-existing: `kernel_shape --check` failed on HEAD. Fixed 2026-09-23 before story 5: the cause
    was `83b8402b` hand-editing the generated `layout.md` (the two hull words) instead of the
    `kernel_fields.tsv` overlay; the recovery now lives in the overlay, a `doc`-tier row may
    annotate a datamap word, and the tables are regenerated.
- [x] **06 · 0019/8 = 0002/29e** — The 12 remaining families, rules only. XL · Opus or Fable/high.
  Absorbs 0002's 25c, 26, 16b's ideal state, 10d's selector, 21c's loop half. After 04 and 05 so
  no family types a program id or an inline constant by hand.
  **Status 2026-09-23: pass R complete (below); passes I and C wait for row 06b, the class tree,
  and port onto it.** The reasons and the review of the earlier order are under "Where this
  differs" at the end of this file.
  Row 02 re-verdicted band 19–29: 380 `rule`, 4 `present`, 13 `mechanism`, 71 `dead` of 468.
  Joined to the family files 2026-09-23: **286 `rule` rows of 351** in the twelve families, and
  that is the port (counts below are rule / rows). Nothing of Conditions19 exists in the tree.
  **Three passes (owner's decision 2026-09-23), text in the spec:** R retrieval, I implementation
  in two lanes, C close.
  - [x] **Pass R** (2026-09-23) — the skeleton script, then luna 6 `high` Fast + Sol 6 `high` on the
    skeleton brief, diffed, conflicts drilled by luna `max`; **13 packets covering 295 rows** (the
    twelve families' 286 + Damaged19's 9), `check-packet --all` green; 115 packet-versus-checklist
    contradictions listed per family for the owner. Numbers and the routing as measured in the spec.
    - [x] Tier 0, `kernel_skeleton` (2026-09-23): all 468 band rows, `--check` green. Found the
      eleven ‼ rows outside every family; bucketed as `Damaged19.tsv`, 9 of them `rule`.
    - [x] The two readers on the skeleton brief, the diff, the drills, the packets, the contra lists
      (`kernel_packet.py`, 231 codex runs + 10 Opus subagents, 58 min wall).
    - [x] Second judge (Opus) on the 60 open contradictions and the 18 "neither" drills; the 84
      one-sided rows filled in by the other reader. The 38 rows where the packet corrects the
      checklist are written back as `CORRECTED` clauses on the verdict evidence (32 rows), the
      checklist regenerated, both ledger checks green.
  - [x] **Pass I** — after row 06b. Each family ports onto its retail classes as overrides
    (`Super::` where retail called the base directly, per the packet's skeleton); the Damaged19
    rows land on their own classes, so the "which family" question dissolves. Lanes as below.
  - [x] **Pass I, lane A** (the interpreter, in order; gate then full read then commit per family)
    - [x] Conditions19 (20/23) — `617497a1`, `72b64eca`
    - [x] RunAi19 (17/34) — wave 2, L13a, merged as `d1d93773`
    - [x] StartTask19 (27/28) — `86ceba23`
    - [x] RunTask19 (23/24) — `8cc9e23a`
    - [x] Select19 (31/39) — `abfae21c`
    - [x] Think19 (15/15) — wave 2, L13b (+ Damaged19's two `NPCThink`s), merged as `d1d93773`
  - [x] **Pass I, lane B** (independent of the loop; Boss and Werewolf after lane A's base bodies)
    - [x] Spawn19 (48/60) — `14697e2b` (replayed from `story8/int-4`)
    - [x] Damage19 (26/26) — `414545a6`
    - [x] Script19 (20/32) — `576a06fc`
    - [x] Boss19 (11/19) — `2eca094a`
    - [x] Werewolf19 (17/17) — `2eca094a`
  **2026-09-28: pass I wave 1 landed** (twelve lanes in worktrees, not the two lanes above). Nine
  families are on `main`, 204 `rule` rows plus two Damaged19 rows. Misc19 was ported in pass I too,
  30 of 31 (`a17ed6a9`); `0x101a98c0` is open. Tests `Elysium.Substrate` 1,326 → 1,613.
  `story8-forwarding.tsv` 210 → 71. The walked prose is folded into the oracle. Open: Spawn19's
  merge, wave 2 (RunAi19, Think19, the loop wiring, the 82 `STORY8-TWIN` twins), then pass C. The
  record is the spec's § "Pass I landed 2026-09-28".
  **2026-09-28: wave 2 landed** (`d1d93773`). RunAi19 17 and Think19 17 (with the Werewolf and
  PlayerController `NPCThink`s) are ported, and Spawn19 is on `main` (`14697e2b`). The retail loop
  is live: `NPCThink → RunAI → 433 → MaintainSchedule → 437/438 → 442/444`, with damage through
  142/390 and death through 144. 82 `STORY8-TWIN` markers are down to 4 named survivors.
  Forwarding is 210 → 0 and unported 778 → 449. `Elysium.Substrate` is 1,710 / 0; pytest 4,224
  passed. The walked prose is folded, and `docs/vtmb/npc-ai/story8/` is gone.
  - [x] **Pass C** (2026-09-28) — Misc19 (31/34), the absorbed stories' sentences, the ticks, the
    rename commit (`ccefc765` moves, `6376f797` references) and the map smoke, twice: the first run
    found late `NPCInit 0x10273390` never starting its NPC (maker children and every travelled-to
    hub NPC stood on FLT_MAX), fixed as `2fcc21cb`; the second run on `main` confirms it, hub
    pedestrians walk, 0 ensure/assert. The remaining observations were verdicted against the
    listing: the patrol stalls were one more divergence, `TASK_NEXT_PATROL_POINT`'s helper
    `0x102aa9e0` ported without `NextPoint 0x10307b80`, fixed as `5b0a5a40` and confirmed by a third
    run (the monk loops its five nodes, `sentry2` patrols under `0x67`); think cadence and the
    "gathered" display are faithful. Routed on: route failures over jump link 390 and unconnected
    type-0 nodes (rows 08/09), the per-level clock (world/session), `SetAttackExtentsForSequence
    0x10090c80` (animation), a script reading `.classname` (scripts). Detail: the spec's smoke paragraph.
    Content note: the four gitignored trees were deleted twice by `git worktree remove` recursing
    through junctions; restored from `exports_v2`, 102 of 108 maps baked (six refused by the pipeline
    on retail data: five zero-width ropes, one missing `particles/flare3` sprite).
- [x] **06b · 0019/5** — The class tree, one port class per live retail class. XL · Opus/high. **Landed 2026-09-27.**
  Alone on a branch; witness green before and after; nothing else touches `ElysiumNpc.h`.
  Moved up from row 10 on 2026-09-23 (decision recorded under "Where this differs"). Its inputs
  are landed (rows 02 and 04); its census clause is amended in the spec so unported `rule`
  overrides are residue, not failures, until row 06's pass I closes. Baseline is green:
  `kernel_shape --check` fixed the same day (row 05's hand-off).
  **Validated and settled 2026-09-24; step 0 accepted, step 1 not started.** The story's claims were checked against
  the image, the ledger and the code. Three did not survive and are replaced in the spec's job
  text: the "most-derived claimant" rule, the vocalisation sound-table getter, and "one class
  per retail class".
  - *Defect found.* The census's classname column is a proximity guess. Retail's factories map
    each classname to exactly one class, and nine resolve differently from the census, seven of
    them live. A placed `npc_VCop` runs as the bare Troika line with Troika's schedules; the hub
    has 37. The map is in `population.md` § "The classname → class map, read from the
    factories".
  - *Owner's decisions.*
    - The base splits into `FElysiumNpcBase` (`CAI_BaseNPC`) and `FElysiumNpc` (Troika).
    - One class per live retail class, named `FElysiumNpcBach` and so on, one header and one
      cpp each. Dead classes get no class.
    - Species `dead` bodies are deleted first; that part moves from row 11.
    - The resolution fix lands with the tree.
    - Every live classname is registered.
    - Generated slot bodies move to their retail owning classes.
    - Fixtures are rewritten to spawn by classname.
    - Bodies, words and bindings land in one commit.
    - The controller folds in with retail AI.
    - Makers, script directors and the test hull fold in as the last pass.
  - *Reviewed execution plan:*
    [story-5-execution-plan.md](0019-npc-kernel-rework/story-5-execution-plan.md). Steps 0–6
    accepted (2026-09-27, last gate 1,281 / 14 / 1, zero failures); revised the same day to two
    remaining commits, A (the ten deferred classes folded) and B (closure). The step records,
    checkers and expectation files were retired the same day; the commits are the record.
  - *Landed 2026-09-27* (commit A the ten deferred classes, commit B the closure): all 56 live retail
    classes are C++ classes; the string-keyed class lookup, `IsRetailClass`, the vocalisation table
    and the C4263/C4264 hides are gone; `gen_kernel_shape` emits the census, the chain slot bodies
    and a compile-checked override census. `kernel_shape --unported` 987 → 778. Gate on this tree:
    see the B landing commit. Details and the story-8 hand-off: the spec's story-5 landing paragraph
    and [handoff-story-8.md](0019-npc-kernel-rework/handoff-story-8.md).
  - *Hand-off to row 06 (pass I):* the residue pin `docs/vtmb/npc-kernel/unported.tsv` (778 rows, must only
    fall) and the carried items listed in the spec (the 431/437/433/442 think loop, Andrei's task
    0x154, `Weapon_Switch`, `CAI_Hint` `ObjectCaps`, Camera `NPCInit`, `+0xe4`).

## B — the movement base 0019/6 stands on

- [x] **07 · 0018/4** — The place set. M · Opus/high. **Landed 2026-09-29.**
  - *The asset.* One `DA_<map>_Places` per map holds a row per AIN node with all 22 hull offsets,
    the positional hint pairing, the crosswalk pairs and one wander cap per hull. All 108 maps stage;
    the tutorial has 203 places / 49 bound hints, the hub 578 / 274, 0 out of range.
  - *The runtime.* `FElysiumPlaceSet` carries the cooldown `+0x9c` and the attached hint `+0xa0`.
    The `CNodeEnt::Spawn` counter binds `m_nNodeID`, and every node seam answers from the set:
    patrol, the hint node arms, the nearest node, slot 527 and the live hint list.
  - *The wander.* `TASK_GET_PATH_TO_RANDOM_NODE` runs the decided capped point pick through two
    narrow seams, `InstallPathNoGoal` and `RouteLengthTo`, which rows 08 and 09 absorb (row 09: now `QueryRoute`).
  - *The reports.* The gate carries three observation reports: places off mesh 0, uncovered 0,
    and zone pairs joined on the mesh (tutorial 2, hub 3).
  - *Handed on.* The smoke found the hub patrol cops' stall pre-existing: Unreal routes a leg over
    jump link 731, which retail's `IsJumpLegal` (`0x10280880`) refuses. Row 13 is to carry retail's
    jump legality per direction; row 08 the jump seam, which also refuses legal jumps (hub 88,
    tutorial 390). Detail: the spec's story 4 "Landed 2026-09-29" paragraph.
- [x] **08 · 0018/5** — The navigator and the movement seam. L · Opus/high. **Landed 2026-09-29.**
  - *The seam.* `FElysiumNpcNavigator` holds the path and goal words by their retail offsets;
    `FElysiumNpcMoveRequest` / `FElysiumNpcMoveFacts` on `IElysiumNpcMotor` carry the body's facts
    and never a verdict. Wave 0 corrected the story's text: goal flag `0x2` has one issuer,
    `nav+0x44` is never written, arrival is the constant 0.0625, `Move` never re-paths, door `0x0e`
    comes from `SimplifyPath`.
  - *The outcomes.* `NavigatorMoveStep` is `CAI_Navigator::Move 0x102eff40` row for row (`0x0d`,
    `0x0c`, the stale mark, the NPC hold and its re-issue, both completions), with
    `TaskMovementComplete`'s `AdvancePath`, the path-corner chain (`OnPass`, `speed`) and the
    goal-flag-2 node route. `ChangeSchedule` now translates first (`0x102cc1f0`). An unblocked cop
    arrives (1.0 cm left over 45-50 m, three of three).
  - *The filter.* Pedestrian requests price ×5–9 through `ElysiumNavQueryFilter_Pedestrian`, plain
    walks ×0; `pedestrian_link_census.py` gates the corrected census: 1,277 flagged links over 32
    maps, hub 461 (the spec's 1,331 / 40 mixed the base graphs with the patch hub count).
  - *The gate.* The 0.8 s think gate is one per-world stamp (`BuildStamp`), re-stamped on restore;
    Troika gates only on the loader byte (`SetPlaceSetPending`). 0 ensure / assert / `0x0d`.
  - *Handed on.* The NPC hold's cadence defect (a held cop re-armed every ~14.8 s and never
    resumed) was fixed the same day (`414ac129`); the 14.8 s is retail's out-of-PVS think law,
    kept. The `copcar` cops' spawn stall is the police car prop, which the
    NavMesh does not carry (3's contents / row 13). `RouteLengthTo` went to row 09 (landed as `QueryRoute`), the jump
    refusals, door policy and two 0.8 s tails to row 13, the slot-584 tail to row 11, the step-rise
    pin to story 3's harness report. Detail: the spec's story 5 "Landed" paragraph.
- [x] **09 · 0018/6** — Geometry services (sight, hull sweep, stand test, reachability). M · Opus/high. **Landed 2026-09-29.**
  - *The seam.* `TraceRetail` (retail's mask, characters returned as a list, each caller applying its
    own filter), `QueryRoute` in place of `RouteLengthTo`, `NavRaycast`, the mask recipe. Wave 0
    corrected the story's text: sight was already on its own channel, `RetailCollisionExtents` was
    the zero box, `0x102a0ed0` is `CanStandAt` (the stand test is `0x102e7270`).
  - *The bodies.* The stand test, `IsValidCover`, `IsAreaClear`, `CanFitAtNode`, the floor drop, the
    wander probe, `ValidateNavGoal`'s floor probe and enemy eye, and `FVisible`'s four arguments
    (every NPC transparent, the player and solid props block).
  - *Fixtures and live check.* Five tutorial fixtures pass; the NavMesh spike skips in a test, so
    reachability is live: the stealth lane attributed (0 of 3 flipped), a player occludes and an
    NPC does not, a sealed pocket refused, `sm_hub_1` 0 ensure / assert. Gate: 504 / 0 and 23 / 0.
  - *Handed on.* Rows 11, 13 and 19 (below); the target's own hull under a MONSTER ray is exempt, an
    inference (RE-BACKLOG 39). Detail: the spec's story 6 "Landed" paragraph.

## C — close 0019

- **10 · 0019/5** — moved to row 06b (2026-09-23).
- [x] **11 · 0019/6** — The deletions (dead rows) and the mechanism seams (Motor, Navigator, traces, push-outs). L · Opus/high. **Landed 2026-09-29.**
  - *Dead 653 / 653 closed; mechanism 330 / 372* (42 kept `hand:` bodies, RE-BACKLOG 43); inline cells 438 → 0
    (the tunables overlay 114 → 526 rows, every one re-read from the image); `unported.tsv` 450 → 393.
  - *The seams:* floor facts, move-ignore, has-path, hull size, facing target, yaw speed, a route query with a
    start; the `FVisible` blocker cell; the render-mode / solid-`0x20` filter arms and the prop keyfields
    `blocks_traces` / `npc_transparent`; the slot-584 one-shot. Slot 523 is `GetStepDownHeight`.
  - *Re-verdicts:* ten species `GetUsedHullBits`, `GetLocalTaskId`, `OnRestore 0x1027bf50` are `rule`.
  - *Live check:* tutorial and `sm_hub_1` idle, 0 ensure / assert, no closed slot fired, pedestrians walk, the
    patrol cops fail 0xc at link 731 as at row 09 (row 13). Detail: the spec's story 6 "Landed" paragraph.
  - *Handed on:* RE-BACKLOG 40–43 are read and banked (the motor's points, wave 4's reads incl. the skill-level
    autoaim arm and the 150 Tzimisce sentinel, 0010 / 0015's flinch and knockback tuples, the kept bodies'
    service map in `seam-list.md`); the ports behind 42 and 43 stay with 0010 / 0015 and 0002. Row 12 is next.
  Row 09 handed it: `MoveLimit`'s jump / fly / climb arms and `TestGroundMove`'s per-step stand
  chain (a NavMesh raycast stands in for the ground arm, named); the nav filter arms
  (`CNavPropertyDatabase`, slot 91, gamerules groups); the `StandardFilterRules` arms not carried
  (render mode without WINDOW, solid `0x20`, the `blocks_traces` / `npc_transparent` keyfields);
  the `FVisible` blocker cell; the trace cost (~0.5 ms each on the hub, measured, not capped); the
  closed-room fixture, live-only; the Werewolf fake-hull check (local box, not world bounds) and the
  slot-523 accessor's `GetMaxJumpSpeed` name (generated from `signatures.tsv`).
- [x] **12 · 0019/7** — The reach cut: `kernel_ledger --reach <map>`. S–M · Sonnet/medium. **Landed 2026-09-29; 0019 is closed.**
  - *The tool:* `kernel_reach.py`; `reach/<map>.md` + `.tsv` per map, a "Reached by" column per map in
    `coverage.md` (read from the committed TSV, so `kernel --check` needs no exports). Tutorial: 6 classes,
    359 texts, 243 tasks, 3,367 functions, 141 species rows; hub: 12 / 390 / 244 / 3,465 / 214. Two task
    identities lack an arm on both maps (`TASK_TEST3/4`, retail's test schedules, selected by nothing); two
    more were `GTaskArms` census omissions (Troika `0x11f`, the rat's `0x14a`), fixed.
  - *The cut:* reach barely narrows section E — the human line shares the base/Troika bodies — and the
    sizes were stale because rows 06/06b had already ported most of what the stories name. Applied as an
    audit of all 23 stories against the port (five readers, every cited address checked); each story in
    0002's spec now carries a *Reach cut* clause and a re-read `Size:`. Rows 26–48 below are re-sized from
    those clauses; four are done bar their record.
  - *Gate:* `kernel --check` 7/7; `kernel_ledger --reach … --check` 17/17; `Elysium.Substrate.Schedule`
    32/0; `test_kernel_reach.py` 5 new. *Handed on:* RE-BACKLOG 44 (the reached no-verdict functions).
  - *Found, owned by no open story* (needs a home before section E starts):
    - **The NPC input surface.** Retail registers 33 NPC inputs (`GNpcInputFuncs`); the port registers 14
      (`ElysiumNpcClasses.cpp`). The 19 missing: `AllowAlertLookaround`, `AllowKickHintUse`,
      `AllowOpenDoors`, `Faint`, `FleeAndDie`, `MakeInvincible`, `SetBloodShieldDiscipline`,
      `SetBossMonster`, `SetDefaultDialogCamera`, `SetDontFacePlayerInDialog`, `SetFallToGround`,
      `SetFollowerBoss`, `SetFollowerType`, `SetInvestigateMode`, `SetInvestigateModeCombat`,
      `SetMovementMultiplier`, `SetSpeechVolume`, `StayEntrenched`, `WalkToNode`. None is fired on
      `sp_tutorial_1` or `sm_hub_1`; over the 108 maps' I/O: `SetInvestigateModeCombat` 69, `StayEntrenched`
      67, `SetInvestigateMode` 66, `FleeAndDie` 27, `MakeInvincible` 19, `SetFollowerBoss` 15, and the
      scripts fire six of them. Five are named inside stories (10g, 12a, 16a, 21a); the other fourteen — the
      investigate-mode pair above all — are nobody's. One story: the retail handler behind each (the datamap
      replay names them), its verdict, the registration.
    - **The VSound table.** `SpeakVSound` is a seam (`ElysiumNpcSounds10.cpp:115-140`); every `PLAY_SOUND`
      step in the corpus is silent and the flee / death vocalisations with it. `0x101f5950` (the play) and
      `CVSoundActivityTable_t` carry no verdict, and no spec loads the table. Not 0018/13 (that is
      `CSoundEnt`, the AI sound list).
    - **`GTaskArms` is hand-checked, not generated in-repo.** Two arms that exist had no row; a check that
      every `case` / `== 0x..` local id in the species 442/444 bodies has a row belongs beside the meter.
    - **`DIST:ACCUM` is a live combat defect today**, not a follower one: 18 programs, the melee-advance
      family among them, run it on every tutorial combatant (16a's clause). Row 40 is far down; pull the
      `0x102702d0` arm forward or give it to 0005.

## D — the rest of the world (0018, in its listed order)

- [x] **13 · 0018/7** — Traversals: jumps, doors, crosswalks. L · Opus/high. **Landed 2026-09-30.**
  - *Jumps, faithful (the owner's decision).* No shipped NPC holds `bits_CAP_MOVE_JUMP`: slot 513 has
    no override, `m_afCapability +0x5cec` is SAVE-only, none of the 63 `CapabilitiesAdd` literals
    carries bit 2. So `0x102ff960` refuses every jump-only link at step 2, and `IsJumpLegal` is never
    reached. The 25 / 117 jump links are baked as disabled records with per-hull verdicts. The hub
    cops walk retail's ground route `452 → 151 → 453`, and link 731's stall is gone live.
  - *Doors.* The door keeps `+0x644` / `+0x640` (SAVE), `GetNPCOpenData`, `IsCloseBlocked`, and HitTop →
    `OnDoorFullyOpen 0x1027dd10`. Slot 531 is live from both dispatchers: the look-ahead `0x102f06e0`
    fails `0x0e`, the move-step sink `0x1027dc10` fails `0x0c`. The alternate-AI modes 1/2/3 run.
    `AElysiumNavDoorLink` carries the `0x102fce80` predicate, and every door is cut (tutorial 8
    links / 8 doors, hub the smoke-shop pair).
  - *Crosswalks, the splice (the owner's decision).* A NavMesh route is laid as [curb A, curb B, goal]
    with retail's waypoint words (named modernization for `0x102fcd00`). There is one red/green
    boolean per pair, the wait test `0x102a0bc0` in retail's gate order, and the queue arm
    `0x10298340`. `PAUSE_MOVING` / `FACE_NEXT_NODE` run, so `0x102` runs. Save / restore uses −1.
  - *Gate.* Pytest 1,154 / 8 skipped; Content 27 / 0; Substrate 1,705 / 0; `verify nav` clean. Both
    maps idle live with 0 ensure / assert / jump refusals.
  - *Closed on tests (the owner, 2026-09-30).* The live crosswalk wait and the live locked-door
    refusal go to row 36. Every hub pedestrian runs the ambient executor, never `0xff` / `0x100`, so
    no live NPC reaches either. `NpcCrosswalk.ThinkWaitsAtRed` and the eight `NpcDoorLink` tests
    carry them until then.
  - *Handed on.* Row 36: the two live acceptances and the `ThinkSchedulePolicy` release defect.
    Story 3 / 21-9: the mesh walks every jump-only pair (25/25, 36/36; 117/117, 103/103), and
    bridging keeps ground+jump. 21-10: rat link 721's pin is retired. Row 25 (0018/19): the rat-only
    jump pairs, which have no record. Story 3's contents: `copcar` and the three crossed
    `func_brush`. 0002/26: `BeginNavigationJump`'s launch direction. RE-BACKLOG 45–48. Detail: the
    spec's story 7 "Landed" paragraph.
  Row 07 handed it the jump links' legality. The bake places every motion-2 link both ways without
  `IsJumpLegal` (`0x10280880`); 95 of the hub's 117 and 17 of the tutorial's 25 fail it both ways.
  Unreal then routes the hub patrol cop over link 731, which retail never plans.
  Row 08 added: why `BeginNavigationJump` refuses LEGAL jumps (hub link 88, tutorial 390), the
  door policy behind slot 531 and `SimplifyPath`'s `0x0e`, the `copcar` prop obstacle (a
  `prop_dynamic` the NavMesh does not carry; shared with 3's contents marking), and two unwired
  0.8 s-think tails: the `ai_node_graph_built` event and the dynamic-link initialisation
  `0x102cc900`.
  Row 09 added: the jump refusals stall both hub patrol cops (link 731 and friends, 28 refusals in
  a 3-minute idle on `sm_hub_1`).
- [ ] **14 · 0018/8** — Hint nodes. M · Opus/high. **Landed 2026-09-30; box left open: the owner decides the tick on the live check.**
  - *The searches.* `0x102d1af0`, `0x102d24b0`, `0x102d2980` / `0x102d2940` and the random pick
    `0x102d1760` run over the world's list, cursor and count; the class word `+0x474`; claim, release,
    owner test and available-to-me (`0x102d1350`, `0x102d1420`, `0x102d1450`, `0x102d1540`) on the
    hint's own SAVE words. The attack validator `0x10296c40`, the hint LOS check `0x102968f0`, the
    idle gate `0x102b5de0`, the kick-hide walk `0x102d0910` (`NpcKicked`), slot 550 `CoverRadius`.
    `hint_rating` is replaced by a row of `NPC_Cover_Distance_Scalar` (authored 3 reads 2.5).
  - *Corrected.* The spec's "no registry" line was stale. The two mask-search callers passed a type
    where retail passes a class mask. The port's comments on the LOS check and the idle gate were
    backwards. The NPC-side `bOwnsHint` / `HintReusableAt` stand-ins and the duplicate `FUN_10296c40`
    are deleted. The cover validators take the node's yaw as facing.
  - *Gate.* Build green; Substrate 1,722 / 0 (baseline 1,705); Content 33 / 0 (baseline 27); ledger
    regenerated, `uv run elysium research kernel --check` 7 / 7; save schema 40.
  - *Live check, NOT fully observed (why the box is open).* Observed: `hint_rating` 2.5 on row 465,
    `DisableHint` / `ScriptUnhide` / `EnableHint` semantics, Python `DisableHint`, 0 ensure / assert
    over the hub idle and the whole session. **NOT observed: an NPC's combat-time tactical search
    claiming a cover hint and releasing it**, after three attempts, each stopped outside this story:
    `thug_3` stuck in `FALL_TO_GROUND` after `ScriptUnhide` (no body owner, never enters combat);
    the hub's `patrol_cop_north` is a baton cop (melee branch, mask 8, never the mask-1 search);
    `sentry3` / `Hunter1` never enter combat (`Conditions (none)` though seen; `Hunter1`'s
    `hint_groups` admits no group-1 row). The chain is pinned by 13 `HintSearch` unit tests and
    `Elysium.Content.Hints.TutorialTacticalSearch` / `TutorialClaimAndCooldown`. Detail:
    `story8/live-check.md`, `landing-numbers.md` § Live check.
  - *Handed on.* Row 15: the cooldown `+1.0` claim write and the selectors. Row 32: the kick chooser
    over `NpcKicked`. The weapon `+0x8c0` range and the player's `BodyTarget` stub. `0x102d0910`'s
    activator order, `m_iszUserData`'s reader. The hidden-NPC lifecycle defect (`thug_3`) and the
    senses `Sighted()` gate for a `SetRelationship`-flipped NPC, both outside this story.
    RE-BACKLOG 49. Detail: the spec's story 8 "Landed" paragraph.
- [ ] **15 · 0018/9** — The goal selectors. L · Opus/high.
- [ ] **16 · 0018/10** — Interesting places. M · Opus/high.
- [ ] **17 · 0018/11** — Patrol paths and the patrol-point interest record. S–M · Fable/medium. Corpus pass done 2026-09-21.
  From row 07's smoke run, unattributed: after a cold MCP `map_load` of `sp_tutorial_1`,
  `monk_upstairs_podium` ran `0x67` between `SetupPatrolType` and `FollowPatrolPath`, 0.1 s apart,
  and failed `0x1d`. It needs a check on a normal boot.
- [ ] **18 · 0018/12** — The flying mover. M · Opus/medium.
  21-8 handed it a work list: `la_ventruetower_3`'s 70 hull-20 flight claims (7 ground, 12 jump
  starts, 19 jump ends, 32 bridging), reported by the gate and owed an answer. One open read
  first: hull 20's links are move type GROUND on a flyer that never traverses links.
- [ ] **19 · 0018/13** — The AI sound list and its volume table. M · Opus/high.
  Row 09 handed it the weapon line-of-sight mask `0x46004003`: it has no exact channel and falls
  back to `ECC_Pawn`, so NPC clips block shots.
- [ ] **20 · 0018/14** — Squads (the object). M · Opus/high.
- [ ] **21 · 0018/15** — The attack coordinator and the standoff goal. S–M · Opus/medium.
- [ ] **22 · 0018/16** — Makers and templates. S–M · Sonnet/medium.
- [ ] **23 · 0018/17** — Relationship defaults and the player-law bus. S.
- [ ] **24 · 0018/18** — The AI logic entities. S · Sonnet/medium.
- [ ] **25 · 0018/19** — The infrastructure debugger view. S · Sonnet/medium. Before the mind, so every row below can be seen.

## E — the mind (0002), cut to `sp_tutorial_1`'s reach

Task bodies and selector arms only; programs come from row 04. **Sizes re-read 2026-09-29 by row 12** (the
reach cut applied as an audit against the port; each story's *Reach cut* clause in 0002's spec is the
evidence, `was` is the pre-cut size). "record" = done, needs only its tick and record.

- [ ] **26 · 0002/25a** — The `ClearSchedule` producers. XS (was S) · Sonnet/medium.
  delete the dead `RequestClearSchedule` seam, one cine-clear test; out of tutorial reach otherwise.
- [ ] **27 · 0002/25b** — Species `TranslateSchedule` bodies, on their classes. XS, record (was XS, grows per species) · Sonnet/low.
  all twenty bodies landed with 06b; three named constants left.
- [ ] **28 · 0002/10d** — The alert selectors and the ladder. XS–S (was L) · Opus/high.
  one divergence: `CommitBestSound`'s `+0x60dc` mirror; one program test.
- [ ] **29 · 0002/10e** — The sound-investigation task arms. XS (was M–L) · Fable/medium.
  done; `PLAY_SOUND` silent behind the VSound seam (no story owns the table — row 12's findings).
- [ ] **30 · 0002/10f** — The unknown-investigation task arms. XS (was L) · Fable/high.
  two arm tests, one program test.
- [ ] **31 · 0002/12a** — The reaction keyfields: normalization and readers. XS (was S–M) · Sonnet/medium.
  the predicate's `stay_entrenched` arm; register `StayEntrenched` / `AllowKickHintUse`.
- [ ] **32 · 0002/12b** — The cover and kick chooser. S–M (was M–L) · Fable/medium.
  the chooser's cover search is wired to a stub; the prop finder / predicate and the impulse are seams.
- [ ] **33 · 0002/10k** — The saved-position task arms. XS (was M) · Fable/medium.
  done (the census row landed with row 12); one interpreter test.
- [ ] **34 · 0002/10g** — The patrol roll sites and task arms. XS (was L) · Opus/high.
  register `WalkToNode`.
- [ ] **35 · 0002/10h** — The hunt-investigation task arms. S–M (was M–L) · Fable/medium.
  the `0xaf` builder over 0018/9; the cvar wire; the typed Hunt state.
- [ ] **36 · 0002/11** — Interesting places: the three selector arms; retires the ambient executor. M (was L) · Opus/high.
  arms and bodies landed; the executor is still the live path; `0x102daac0` unported.
  Added 2026-09-30 by row 13 (the owner's decision: 0018/7 closed on tests). *Acceptance:* live on
  `sm_hub_1`, a pedestrian on `0xff` → `0x100` waits at a red curb through `0x102` and crosses on
  green, a second queues behind it, and an NPC routed over the smoke-shop link is refused by the
  locked `basic_smoke_door` and avoids it for its timer. *Defect claim (not fixed):*
  `ElysiumNpc.cpp:985-988` releases the ambient place under any running program. Retail's only
  schedule-change release, `OnScheduleChange 0x102a0940` → `0x102b53d0`, runs only without
  `PRESERVE_PATH`, which `0xff` sets. Detail: 0002's story 11 block.
- [ ] **37 · 0002/27** — Patrol-point interest: the roll and the two task arms. XS, record (was S–M) · Fable/medium.
  one record-arm test.
- [ ] **38 · 0002/10j** — `CheckTarget`. XS, record (was S) · Sonnet/medium.
  done; `UpdateTargetPos`'s body is 0018/5's mechanism seam.
- [ ] **39 · 0002/10i** — The comfort program's task arms. XS (was M) · Fable/medium.
  the stale `-1` test and comment; one integration test.
- [ ] **40 · 0002/16a** — Followers. S–M (was L–XL) · Fable/high.
  `DIST:ACCUM` never reads the accumulator (18 programs, melee-advance included); the rulebook row; the input; 6b's arm 4.
- [ ] **41 · 0002/17** — Squads: the condition producer and the two tasks. XS (was L–XL) · Opus/high.
  drop the overlay clear now; the set arm, `SquadNewEnemy` and `SquadSeesPlayer` after 0018/14.
- [ ] **42 · 0002/16c** — Possession and frenzy. XS (was M) · Opus/medium.
  two apply-path calls in `ElysiumDisciplines.cpp`; out of the population cut (a player cast).
- [ ] **43 · 0002/16b** — The composed relationship. S (was M) · Opus/medium.
  the feed guard and `GatherSight` still read the flat table (three more sites to check).
- [ ] **44 · 0002/21a** — The flee state. S (was L) · Opus/high.
  `FleeAndDie` / `Faint` inputs; the scream's `NPC_FLEE` row; two typed-state reads that miss raw state 8.
- [ ] **45 · 0002/21b** — Cower, disoriented, lost. XS, record (was L) · Opus/high.
  done; the spec's `0xe1`/`0xe3` line was stale.
- [ ] **46 · 0002/21c** — The incapacitated victim's consumers. XS (was S) · Sonnet/medium.
  `IsFeedAutoAcceptState` over the ideal activity ids.
- [ ] **47 · 0002/28** — The player-on-head answer. S (was M) · Fable/medium.
  the player-side producer into the existing ground-entity seam.
- [ ] **48 · 0002/13b** — The leak in the defect catalogue. XS · Haiku/low.
  unchanged.

## F — the hub

- [ ] **49 · 0002, the hub's reach** — rows 26–47 again at `sm_hub_1`'s reach list, idle families first.
  Row 12 cut the hub too (`reach/sm_hub_1.md`): it adds `CNPC_VCop`, `CNPC_VHuman`, `CNPC_VHunter`,
  `CNPC_VTaxiDriver`, `CCineAI` and `CCineAISchedule`, 31 more texts and 65 unported `rule` rows (the
  tutorial's 47 plus the four classes' `Classify`, slot 434/473/580 bodies and the taxi driver's damage
  chain); no story's undone piece differs between the two maps, so this row is those species rows.
- [ ] **50 · 0018/20** — The hub at idle, the second witness: scene tests per map, the cook (four
  files need editor guards first), then played. M · Sonnet/medium. Its scene tests and the cook
  guards need nothing past row 25 and can be pulled up to there.

## On demand, not in the sequence

- [ ] **0018/21-8** — The other 102 maps. Owner's approval only, when a witness needs a map outside
  the six. Needs row 01. **Approved 2026-09-21, run, and STOPPED INCOMPLETE the same day**: the
  legacy export tree is deleted and all 108 nav-graph units are published, but of 63 maps
  attempted only 37 are green. It cannot close until **21-10** settles the small-hull cell size,
  which is what 26 of its failures are. Resume by re-running `scratch/21-8/bake_all.py`, which
  skips the green maps.
- [ ] **0018/21-9** — The gate's judgement machinery: pins for bridging and jump-projection
  findings, which take none today. S · Sonnet/medium. Opened by 21-8; shape it against 21-8's
  finding set. Row 08 added one finding to shape against: `hw_hub_1`'s 124 of 513 UNFLAGGED
  ground links cross a `0x2000` box (reported by the census, not gated, cause uninvestigated).
- [ ] **0018/21-10** — The small hulls' cell size. M · Opus/high. Opened by 21-8 and **blocking
  it**: 855 of 1,154 findings are the rat and TinyCentered agents, whose Recast erosion exceeds
  retail's hull by 4.8 and 9.7 cm a side. One experiment settles it (re-cut `hw_hub_1`'s rat mesh
  at 2.54 cm cells and re-ask the 145 links); then a cost decision, a re-bake and a re-judge.
  Row 08 handed on the hub's three rat links (406 / 721 / 1472), which path the long way round.

## Where this differs from the specs' stated orders

Each move follows a `Consumes:` line the stated order skipped; revert any by swapping the rows.

- **0018/6 ahead of 0019/6** (row 09): it is the collision service 0019/6's trace and push-out
  seams call; 0019/6 names only 0018/5.
- **0019/8 (29e) after 0019/3 and 0019/4** instead of beside them: serial, so it goes where it
  retypes nothing.
- **0019/5 (the class tree) before 0019/8's passes I and C, as row 06b** (decided 2026-09-23,
  owner and Fable). The spec's build order had all of 8 before 5, and this tracker had gone
  further and put 5 behind 0018/4–6 in section B. Review of that earlier ordering, by the one who
  made it:
  - *What was right:* 8's pass R first. A reading packet does not depend on the port's class
    shape, and its measurement is what settled this question.
  - *What was wrong, twice.* (1) Placing 5 in section C after 0018/4–6: nothing in B feeds 5 —
    5 consumes 1 and 3, both landed by 2026-09-22 — so that placement followed section headings,
    not `Consumes:` lines, which is the one rule this file claims to follow. (2) Keeping the
    spec's "all of 8 before 5" after pass R had measured 227 of 8's 295 `rule` rows as species
    overrides. On the flat `final` class each lands as a `RetailClass()` prologue that 5 then
    re-homes: 227 bodies and their tests written twice, added to the 155 prologue sites 5 already
    has to move. The only thing that made 8-before-5 look necessary was 5's census clause
    ("one override per `rule` (class, slot)"), which wanted 8's bodies to exist; that is a
    definition and is amended in the spec, not a dependency.
  - *What it costs:* 5 is XL and exclusive on `ElysiumNpc.h`, so 8's two lanes wait for it. That
    wait exists in either order; it is shorter with 5 first because 5 then moves what exists today
    and not 295 more bodies.
  - *What stays after 5:* 8's pass I, then 6 and 7 as before. 0018/4–6 (rows 07–09) keep their
    place; they are 0019/6's base, not 5's.
- **0019/6's species `dead` rows moved into 0019/5's step 1** (owner, 2026-09-24): 228
  hand-written bodies and 195 census rows. Moving them onto the class tree only for row 11 to delete them would do
  the work twice, and story 5 already consumes story 1's delete list for exactly that reason.
- **0002/12a and 25b given slots**: 0002's build order lists neither. 12a sits ahead of 12b,
  which reads its keys; 25b needs the class tree (row 10).
- **12b → 10k → 10g → 10h** where 0002 lists 10h, 10g, …, 10k, …, 12b: 10k consumes 12b's cover
  search, 10h consumes 10k's `0x84` and 10g's hunt cell.
- **10j ahead of 10i**: 10j provides tasks `0x4b`/`0x49` to 10i.
- **17 → 16c → 16b** where 0002 lists 16b, 16c, 17: 16c consumes 17; 16b consumes 16c's `D_INSANE` producer.
- Two cycles are left as the spec orders them and close with a seam: 10d ↔ 10e, and 10h ↔ 16c (`DoFrenzy`'s entry).
