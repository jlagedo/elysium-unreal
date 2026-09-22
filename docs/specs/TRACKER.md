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
- [ ] **04 · 0019/3** — The schedule seam: 691 texts, id spaces, flag tables, the parser; the hand
  programs deleted. L · Opus/high. **In flight, landing as six passes** (owner's decision
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
  **Resume at pass B**, on branch `0019-3-schedule-seam` (5 commits, tree clean, corpus deployed:
  748 files under `Content/ElysiumCorpus/ai/schedules/`). Nothing in the runtime is wired to any of
  it yet — passes P and A are purely additive, which is why stopping here costs nothing.
  - **B** ports the parser into C++. It has a fixture that did not exist before: the Python
    `formats/ai_schedule_glb/parser.py` is the same body from the same oracle section, and it reads
    all 691 shipped texts, so the C++ port is written against a reference that already agrees with
    retail. Also in B: `ElysiumScheduleTokenizer`, `ElysiumScheduleOperands` (the 17 prefixes; the
    13 tables are already recovered and deployed in `vocabulary.json`), `ElysiumScheduleManager`,
    `ElysiumActivityRegistry`, `ElysiumTaskOps`, `ElysiumMiscFlags::ParseScheduleIndex`, and
    `FElysiumNpcConditions::SetOrdinal`/`HasOrdinal`/`Difference`. Driven by inline test text; the
    hand registry and all 295 sites stay untouched, so B is green by construction.
  - **C** stands `FElysiumScheduleCorpus` on the deployed corpus and runs the witness as a test.
    The sidecar already carries what the loader needs: `classNames`, the four spaces with
    `parentUnit`, the registrations and the texts in feed order. Load order is the sidecar's
    `parent_space` graph, NOT the C++ base chain — they genuinely differ (`CNPC_VTzimisce`'s census
    base is `CNPC_VBaseBoss`, its space parent is Troika).
  - **D is indivisible and wants a fresh session with room**: the 295-site type change, the save CRC
    re-key, the inverted mask, the id-space stubs re-pointed. The moment `Tick` reads the manager,
    `Step.Target` has no meaning, so it cannot be split.
  - **E** deletes the 28 hand programs and lands the coverage meter.
  The witness text is read and known: `SCHED_TROIKA_CHASE_ENEMY_FAILED` at `0x105ef068` is **twelve**
  tasks, not the eleven the spec's prose lists (it omits `TASK_WAIT_FOR_MOVEMENT`), with twelve
  interrupts; of its task tokens the port has ten, `TASK_FIND_COVER_FROM_ENEMY` is new, and
  `TASK_REMEMBER` re-types to a memory mask.
- [ ] **05 · 0019/4** — The tunables table. S–M · Sonnet/medium.
- [ ] **06 · 0019/8 = 0002/29e** — The 12 remaining families, rules only. XL · Opus or Fable/high.
  Absorbs 0002's 25c, 26, 16b's ideal state, 10d's selector, 21c's loop half. After 04 and 05 so
  no family types a program id or an inline constant by hand.
  Row 02 re-verdicted band 19–29: 380 `rule`, 4 `present`, 13 `mechanism`, 71 `dead` of 468 — the
  family counts below predate it; port only the `rule` rows (`checklist-19-29.md`).
  - [ ] Conditions19 (23 rows) — in flight; review against row 02 before it merges
  - [ ] Spawn19 (60)
  - [ ] RunAi19 (34)
  - [ ] StartTask19 (28)
  - [ ] RunTask19 (24)
  - [ ] Select19 (39)
  - [ ] Damage19 (26)
  - [ ] Script19 (32)
  - [ ] Think19 (15)
  - [ ] Boss19 (19)
  - [ ] Werewolf19 (17)
  - [ ] Misc19 (34)

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
