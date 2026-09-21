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

- [ ] **0018/4's open question** (spec line 722): `TASK_GET_PATH_TO_RANDOM_NODE` is a random walk
  over AIN links, not a draw from places. Pick **(i)** reduced adjacency in the cooked asset,
  **(ii)** named modernization over the NavMesh, or **(iii)** places only and forfeit the hub
  witness. Needed before row 07.

## A — finish the in-flight group, then the data seams

- [x] **01 · 0018/21-7** — The producer's debt: structural entity read, the five flags, `Use` and `times 0`. M–L · Opus/high.
  First because it changes what every later bake loads; recovery is done, the job is mechanical.
- [x] **02 · 0019/1** — The strict verdict pass → the delete list and the seam list. M · Fable/high.
  Landed 2026-09-21: `rule` 2,205 → 1,354, `dead` 0 → 657, `mechanism` 175 → 380; the lists are
  `docs/vtmb/npc-kernel/delete-list.md` and `seam-list.md`, held by `kernel_lists --check`.
  It found twenty NPC classes nothing can instantiate (`population.md`), which shrinks rows 06,
  10 and 11, and that `ai_goal_standoff` is unauthored, which halves row 21.
- [ ] **03 · 0019/2 pass B** — Bindings: the 200 `CBaseEntity`/`CBaseCombatCharacter` keys, component-struct members, the `SAVE` walk + delete the nine `Serialize*Block`. M · Opus/high.
  The non-NPC classes are not part of this row: each lands with the 0018 story that stands it.
- [ ] **04 · 0019/3** — The schedule seam: 691 texts, id spaces, flag tables, the parser; the 19 hand programs deleted. L · Opus/high.
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
