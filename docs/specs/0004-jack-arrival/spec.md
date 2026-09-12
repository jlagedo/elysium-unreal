# 0004 jack-arrival — the Jack cutscene ends with Jack grounded and the first conversation runs on the conversation UI

## Witness
On `sp_tutorial_1`: the second cutscene with Jack ends, Jack stands on the ground, and using him
opens the conversation UI; the player talks through Jack's tutorial dialogue to its end. Played
against retail: the conversation opens by **use**, refuses with a hint while a combat timer runs,
shows a disabled `[ PERSUASION 4/7 ]` row beside its enabled failure route, plays the female take
for a female PC, runs Jack's NPC col-5 only after his line finishes or is skipped or cut by a
pick, cannot be saved from, and closes into `DialogPostProcess`.

## Scope
The conversation entry, turn, pick and close (`CDialog`), the dependency evaluator, and the
dialogue's read of the reaction score. Owned elsewhere and consumed here: the beat that ends the
cutscene and grounds Jack — **0003** (its `CineCleanup`); feed and stealth reactions and the
`React` Character method — **0002**; the spoken line, subtitles and the retained-NPC-subtitle
rule — **0011**; barter (`StartBarter`, 9.8/8.6) — unowned; `GetStartingLine`'s fidelity gap and
the level-script fill — **0009** (9.3).

## Sources
- Oracle: `docs/vtmb/game_runtime.md` (`CDialog`, dependencies, the pick order),
  `docs/vtmb/retail-defects.md` §6, `docs/vtmb/entity_io.md` (`StartPlayerDialog*`, `OnDialogEnd`).
- Authored data, V2 seams under `$ELYSIUM_WORK_ROOT/exports_v2/`: `dialogues/` (Jack's `.dlg`),
  `maps/sp_tutorial_1.entities.glb` (`sJack_*`, `trig_dialog_*`), `vdata/` (`reaction.txt`,
  `reactions000.txt`).

## Witness data
**Retail, use to close.**
- Entry: `CBasePlayer::PlayerUse` (`0x10167850`) resolves the use target, tests `WillTalk`,
  clears the NPC schedule, pushes AI schedule `0x6a`, calls StartDialog (slot 414,
  `FUN_10178280`). Guards: a live player, no live partner (`player+0xfe8`),
  `IsBusyWithDiscipline(npc)` false, `m_bfAINPCFlags2 & 0x10000000` clear. `Unforced` adds the
  player-side refusal predicate `0x10178170` (combat timers `+0x1d1c`, `+0x1dd0`/`+0x1dd8`, enemy
  count, `+0x1cf8`/`FLT_MAX` sentinel, `0x10175180` while `player+0x1db0` is state 3), cleared by
  `CPlayerEvents::InputClearDialogCombatTimers` (`0x10227250`). On pass: `CDialog::Acquire`,
  `SetDialogPartner`, input lock, remember/holster the drawable weapon (`player+0x1e01`) to
  `item_w_unarmed`, build `camera_cinematic` from `default_camera`.
- Dependencies: `CDialogDependency::Parse` (`0x100e8fc0`) / `TestSimple` (`0x100e9760`) /
  `TestPython` (`0x100e9ff0`) / `ParseDep` (`0x100e9290`): trait class, id, inversion (negative
  threshold ⇒ `<`), threshold, sex gate, Python part, compound type/precedence; class 2
  (discipline) needs rating and blood pool, id 6 needs Ventrue; class 4 feat `0x16` routes to
  `FrenzyComparison`.
- Turn: `get_pc_responses` (`0x100e82d0`) walks rows to the next `#`/sentinel, admits ungated or
  passing rows; `process_pc_line` (`0x100e8520`) keeps/drops/auto-terminates; an empty list
  without auto-terminate substitutes `"I do not have a valid reply."` plus one dummy response.
  Auto-Link/Auto-End (`0x20`/`0x10`) are hidden control rows: run the row's action, follow its
  link, never publish the marker; Auto-End requires `LookupSpeechFile` to find audio.
- Pick: `process_npc_line` (`0x100e8100`) runs the NPC row's col-4 at once, stashes col-5 at
  `+0x30ea` for `CallPendingNPCEventScript` (`0x100e5c70`), flushed on `NPCNotifyDoneTalking`
  (`0x100e4780`) or `Release`. `CDialog::Pick` (`0x100e4bd0`) is refused while the NPC speaks,
  maps index → link, echoes history, charges the dependency (`pc_charge_dependency`
  `0x100e8b90`: blood subtract, `AddFakedDisciplineEffect`), runs the choice's col-5, then sends
  or releases (link 0 / auto-terminate). Pick `-1` releases, `-2` is hurry (`0x102c0bb0`).
- Close: `CDialog::Release` (`0x100e5240`) flushes pending scripts, fires `OnDialogEnd`
  (`npc+0x5f5c`), clears the partner, destroys the camera, unlocks input, restores the holstered
  weapon (`FUN_10178400`), continues into `DialogPostProcess`.
- Scene exit (recovered 2026-09-12 under 0002/15): the scene's cast setup `0x10081ed0` saves each
  actor's `m_bDisableAI` and sets it on the `position_start == 1` arm; `OnSceneFinished`'s walk
  `0x100847e0` restores it, dispatches slot 614, runs `PhysicsRunThink(0)` and re-arms
  `m_flNextThink`; it writes nothing to origin or pose. The grounding is the beat's:
  `CineCleanup 0x1027d170` snaps the origin to bone 0 (or `Bip01` under spawnflag `0x2000`), adds
  `+1` Z, sets `FL_ONGROUND` and runs the drop-to-floor probe (0003).
- Retail defect reproduced (`retail-defects.md` §6): a col-4 with two Python halves can never pass
  (one shipped row).

**Modernizations (owner-named).** M-SAVE: no save inside a conversation. M-UI: the panel is the
project's own style. M-REQ: `[ PERSUASION 4/7 ]` / `[ DOMINATE 2 ] · 2 BLOOD` labels replace
fonts; only the skill-check front is labelled. M-DISABLED: failed skill-front rows stay visible,
disabled (feats, disciplines, attributes, abilities, Humanity; a passing rating short of blood
shows cost); sex, clan, `G` and Python failures stay hidden; negative-threshold rows never show
disabled; a disabled row whose text equals an enabled row's in the band is dropped; rows number
1..N across both. M-REVEAL: choices show at once; a pick cuts the voice, completes the face,
flushes the deferred col-5, then runs the pick's col-5. M-SKIP: hurry is a skip key. M-CAP: no
4-response limit, an overage logs once as an authoring finding. M-REFUSE: refusal shows a brief
HUD notification. Named divergence: `Explain()` evaluates the Python half once where retail's
short-circuit would skip it.

## Stories
In build order. A story is done when every behaviour it lists is in the substrate and its
recovery is written in the oracle section it names. Numbers are stable ids cited by other
documents; a split keeps the number and adds a letter. Each open story carries the retail
contract the code must match, the job, what it consumes or provides, and a size (XS–XL) with the
model / effort tier recommended for it.

- [x] **1. The `.dlg` parser and dlgexpr** (was 9.1). Oracle: `game_runtime.md`.
- [x] **2. Entry, turn, pick and close** (was 9.2, landed 2026-09-06): use-to-talk entry, the
  player refusal predicate, holster, save refusal, requirement labels, disabled skill rows, skip,
  the voice take by text column; the modernizations above. Oracle: `game_runtime.md`.
- [ ] **3. Jack stands on the ground when the cutscene ends.**
  Retail: the grounding is `CineCleanup`'s bone snap and drop-to-floor at the end of the Jack
  chain's last beat (Witness data); the scene's own exit restores AI and writes no pose.
  Job: nothing of its own — the witness check once 0003/3 lands `CineCleanup`, and the residual
  named if Jack still floats (a `scripted_sequence` on the chain that ends without `CineCleanup`,
  or a scene actor placed by `position_start` with no beat after it).
  Consumes: 0003/3.
  Oracle: entity_io § "Scripted sequences" (`CineCleanup`).
  Size: XS. Effort: Sonnet / medium.
- [ ] **4. The refusal predicate's writers and the NPC guards** (was D8).
  Retail: `+0x1d1c`, `+0x1dd0`/`+0x1dd8`, the `+0x1cf8` count and the threat tally
  (`0x1017f770`/`0x1017f8b0`), the `player+0x1db0` state-3 enum; the NPC side
  `IsBusyWithDiscipline` and the `m_bfAINPCFlags2 & 0x10000000` writers; the real drawable byte
  `player+0x1e01` for the holster.
  Job: each writer recovered and wired so the predicate reads live state, not stubs; the holster
  reads the drawable byte.
  Consumes: 0002 (the enemy count and combat timers), 0005 (the attack transaction's timers).
  Oracle: `game_runtime.md` § "The refusal predicate".
  Size: M. Effort: Sonnet / high.
- [ ] **5. Dependency residue.**
  Retail: feat `0x16` → `FrenzyComparison` (unrecovered; answers false / inverted true today,
  excluded from the labelled front); the `AddFakedDisciplineEffect` emitters behind
  `pc_charge_dependency`.
  Job: `FrenzyComparison` recovered and ported; the emitters wired into 0006's effect kernel.
  Consumes: 0006.
  Oracle: `game_runtime.md` § "Dependencies".
  Size: S. Effort: Sonnet / high.
- [ ] **6. Voice residue.**
  Retail: `m_flSpeechVol`'s authored source; retail's third speech extension (`DAT_10562364`);
  the 31 orphan `.lip` files in the corpus.
  Job: the volume source and the extension recovered; the orphans classified.
  Consumes: 0011 (the line service).
  Oracle: `game_runtime.md` § "Voice".
  Size: S. Effort: Sonnet / medium.
- [ ] **7. The reaction-score consumer** (was 9.9, talk slice).
  Retail: the RPG reaction score over `reaction.txt`/`reactions000.txt` is read by the dialogue
  chain during the talk turn; the exact call site is UNRECOVERED. The three social domains stay
  independent: the score feeds dialogue only, never combat targeting.
  Job: the read wire from the landed calculator into the turn build, at the recovered site; no
  write path from dialogue back into the score.
  Oracle: `game_runtime.md` § "Reactions".
  Size: S. Effort: Sonnet / high; corpus pass on the call site first.
- [ ] **8. Presentation completion.**
  Job: speaker and emotion cues on the panel; the subtitle path reads 0011's publish.
  Consumes: 0011/5.
  Size: S. Effort: Sonnet / medium.

## Seams
- Provides: the conversation panel and turn/pick engine to 0002 (feed and stealth reactions) and
  to every later dialogue-bearing spec; the dialogue-side reaction-score read.
- Consumes: the beat's `CineCleanup` from 0003 (3); the enemy count and timers from 0002 and
  0005 (4); the effect kernel from 0006 (5); the spoken line and subtitles from 0011 (6, 8).
- Open recoveries: `FrenzyComparison` (5); the reaction score's call site (7); whether the
  4-response overage has produced a logged `retail-defects.md` finding.
