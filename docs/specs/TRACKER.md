# Tracker — 0019 · 0018 · 0002, one serial sequence

**What's next = the first unticked box.** One story at a time, top to bottom. Tick the box here
when the story's own box is ticked in its spec; the spec stays the source of truth for the text.
Specs: [0019](0019-npc-kernel-rework/spec.md) · [0018](0018-world-ai-infrastructure/spec.md) ·
[0002](0002-npc-ai/spec.md). Built 2026-09-21 from each spec's `## Build order` and `Consumes:` lines.

Already landed: 0018/1, 2, 3, 21-1 … 21-7 · 0019/1 · 0019/2 pass A · 0002/29e 4 of 16 families.
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
  slot 130 was already ported whole (`ElysiumNpcKernelLifecycle19.cpp:1059`) with only tests calling
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
    pre-existing, not fixed: `gen_kernel_shape --check` fails on HEAD (slot 488 `DeathSound` has no
    `SLOT_PORT_MAP` row since `607efd52`).
- [ ] **06 · 0019/8 = 0002/29e** — The 12 remaining families, rules only. XL · Opus or Fable/high.
  Absorbs 0002's 25c, 26, 16b's ideal state, 10d's selector, 21c's loop half. After 04 and 05 so
  no family types a program id or an inline constant by hand.
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
  - [ ] **Pass I, lane A** (the interpreter, in order; gate then full read then commit per family)
    - [ ] Conditions19 (20/23)
    - [ ] RunAi19 (17/34)
    - [ ] StartTask19 (27/28)
    - [ ] RunTask19 (23/24)
    - [ ] Select19 (31/39)
    - [ ] Think19 (15/15)
  - [ ] **Pass I, lane B** (independent of the loop; Boss and Werewolf after lane A's base bodies)
    - [ ] Spawn19 (48/60)
    - [ ] Damage19 (26/26)
    - [ ] Script19 (20/32)
    - [ ] Boss19 (11/19)
    - [ ] Werewolf19 (17/17)
  - [ ] **Pass C** — Misc19 (31/34), the absorbed stories' sentences, the ticks, the rename commit.

## B — the movement base 0019/6 stands on

- [ ] **07 · 0018/4** — The place set. M · Opus/high. **Needs the decision above.**
- [ ] **08 · 0018/5** — The navigator and the movement seam. L · Opus/high.
  21-8 corrected its pedestrian price (an AVOIDANCE, not a preference) and handed it an exact
  acceptance set: the 1,331 links over 40 maps carrying `m_LinkInfo & 0x2000`.
- [ ] **09 · 0018/6** — Geometry services (sight, hull sweep, stand test, reachability). M · Opus/high.

## C — close 0019

- [ ] **10 · 0019/5** — The class tree, one port class per retail class. XL · Opus/high.
  Alone on a branch; witness green before and after; nothing else touches `ElysiumNpc.h`.
- [ ] **11 · 0019/6** — The deletions (dead rows) and the mechanism seams (Motor, Navigator, traces, push-outs). L · Opus/high.
- [ ] **12 · 0019/7** — The reach cut: `kernel_ledger --reach <map>`. S–M · Sonnet/medium.
  **0019 closes here.** Re-read the sizes of rows 26–48 from the tutorial's reach list.

## D — the rest of the world (0018, in its listed order)

- [ ] **13 · 0018/7** — Traversals: jumps, doors, crosswalks. L · Opus/high.
- [ ] **14 · 0018/8** — Hint nodes. M · Opus/high.
- [ ] **15 · 0018/9** — The goal selectors. L · Opus/high.
- [ ] **16 · 0018/10** — Interesting places. M · Opus/high.
- [ ] **17 · 0018/11** — Patrol paths and the patrol-point interest record. S–M · Fable/medium. Corpus pass done 2026-09-21.
- [ ] **18 · 0018/12** — The flying mover. M · Opus/medium.
  21-8 handed it a work list: `la_ventruetower_3`'s 70 hull-20 flight claims (7 ground, 12 jump
  starts, 19 jump ends, 32 bridging), reported by the gate and owed an answer. One open read
  first: hull 20's links are move type GROUND on a flyer that never traverses links.
- [ ] **19 · 0018/13** — The AI sound list and its volume table. M · Opus/high.
- [ ] **20 · 0018/14** — Squads (the object). M · Opus/high.
- [ ] **21 · 0018/15** — The attack coordinator and the standoff goal. S–M · Opus/medium.
- [ ] **22 · 0018/16** — Makers and templates. S–M · Sonnet/medium.
- [ ] **23 · 0018/17** — Relationship defaults and the player-law bus. S.
- [ ] **24 · 0018/18** — The AI logic entities. S · Sonnet/medium.
- [ ] **25 · 0018/19** — The infrastructure debugger view. S · Sonnet/medium. Before the mind, so every row below can be seen.

## E — the mind (0002), cut to `sp_tutorial_1`'s reach

Task bodies and selector arms only; programs come from row 04. Sizes below predate the reach cut.

- [ ] **26 · 0002/25a** — The `ClearSchedule` producers. S · Sonnet/medium.
- [ ] **27 · 0002/25b** — Species `TranslateSchedule` bodies, on their classes. XS, grows per species · Sonnet/low.
- [ ] **28 · 0002/10d** — The alert selectors and the ladder. L · Opus/high.
- [ ] **29 · 0002/10e** — The sound-investigation task arms. M–L · Fable/medium.
- [ ] **30 · 0002/10f** — The unknown-investigation task arms. L · Fable/high.
- [ ] **31 · 0002/12a** — The reaction keyfields: normalization and readers. S–M · Sonnet/medium.
- [ ] **32 · 0002/12b** — The cover and kick chooser. M–L · Fable/medium.
- [ ] **33 · 0002/10k** — The saved-position task arms. M · Fable/medium.
- [ ] **34 · 0002/10g** — The patrol roll sites and task arms. L · Opus/high.
- [ ] **35 · 0002/10h** — The hunt-investigation task arms. M–L · Fable/medium.
- [ ] **36 · 0002/11** — Interesting places: the three selector arms; retires the ambient executor. L · Opus/high.
- [ ] **37 · 0002/27** — Patrol-point interest: the roll and the two task arms. S–M · Fable/medium.
- [ ] **38 · 0002/10j** — `CheckTarget`. S · Sonnet/medium.
- [ ] **39 · 0002/10i** — The comfort program's task arms. M · Fable/medium.
- [ ] **40 · 0002/16a** — Followers. L–XL · Fable/high.
- [ ] **41 · 0002/17** — Squads: the condition producer and the two tasks. L–XL · Opus/high.
- [ ] **42 · 0002/16c** — Possession and frenzy. M · Opus/medium.
- [ ] **43 · 0002/16b** — The composed relationship. M · Opus/medium.
- [ ] **44 · 0002/21a** — The flee state. L · Opus/high.
- [ ] **45 · 0002/21b** — Cower, disoriented, lost. L · Opus/high.
- [ ] **46 · 0002/21c** — The incapacitated victim's consumers. S · Sonnet/medium.
- [ ] **47 · 0002/28** — The player-on-head answer. M · Fable/medium.
- [ ] **48 · 0002/13b** — The leak in the defect catalogue. XS · Haiku/low.

## F — the hub

- [ ] **49 · 0002, the hub's reach** — rows 26–47 again at `sm_hub_1`'s reach list, idle families first.
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
  finding set.
- [ ] **0018/21-10** — The small hulls' cell size. M · Opus/high. Opened by 21-8 and **blocking
  it**: 855 of 1,154 findings are the rat and TinyCentered agents, whose Recast erosion exceeds
  retail's hull by 4.8 and 9.7 cm a side. One experiment settles it (re-cut `hw_hub_1`'s rat mesh
  at 2.54 cm cells and re-ask the 145 links); then a cost decision, a re-bake and a re-judge.

## Where this differs from the specs' stated orders

Each move follows a `Consumes:` line the stated order skipped; revert any by swapping the rows.

- **0018/6 ahead of 0019/6** (row 09): it is the collision service 0019/6's trace and push-out
  seams call; 0019/6 names only 0018/5.
- **0019/8 (29e) after 0019/3 and 0019/4** instead of beside them: serial, so it goes where it
  retypes nothing.
- **0002/12a and 25b given slots**: 0002's build order lists neither. 12a sits ahead of 12b,
  which reads its keys; 25b needs the class tree (row 10).
- **12b → 10k → 10g → 10h** where 0002 lists 10h, 10g, …, 10k, …, 12b: 10k consumes 12b's cover
  search, 10h consumes 10k's `0x84` and 10g's hunt cell.
- **10j ahead of 10i**: 10j provides tasks `0x4b`/`0x49` to 10i.
- **17 → 16c → 16b** where 0002 lists 16b, 16c, 17: 16c consumes 17; 16b consumes 16c's `D_INSANE` producer.
- Two cycles are left as the spec orders them and close with a seam: 10d ↔ 10e, and 10h ↔ 16c (`DoFrenzy`'s entry).
