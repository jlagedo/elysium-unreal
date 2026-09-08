# 0004 jack-arrival — the Jack cutscene ends with Jack grounded and the first conversation runs on the conversation UI

## Witness
On `sp_tutorial_1`: the second cutscene with Jack ends, Jack stands on the ground (not floating),
and using him opens the conversation UI; the player talks through Jack's tutorial dialogue to its
end. Slice acceptance (live, owner-piloted): the conversation opens by
**use**, posts the refusal hint while a combat timer runs, shows a disabled
`[ PERSUASION 4/7 ]` row beside its enabled failure route, plays the female take for a female PC,
runs Jack's NPC col-5 only after his line finishes or is skipped or cut by a pick, cannot be saved
from, and closes into `DialogPostProcess` as before.

## Scope
- Roadmap rows absorbed: **9.2 Conversation UI**,
  **9.9 NPC disposition & reactions** — talk slice only (dialogue's reaction-score consumer).
- Out of scope: feed/stealth reactions, `React` Character method and broader expression/gesture
  semantics for the feed path (0005); the spoken-line audio service, subtitle publish and the
  retained-NPC-subtitle rule (0002, AUD3 — consumed here as a seam); `StartBarter`/barter UI
  (9.8, 8.6); `9.1`'s `GetStartingLine` fidelity gap and `9.3` level-script fill (tracked
  elsewhere, only cited here where they gate this witness).

## Requirements
1. **Scene-exit ground state (owner-reported defect).** Jack is reported floating after the
   second cutscene ends. Treat as a possibly-missing retail behaviour on scripted-scene exit
   (e.g. a ground snap / animation pose restore that retail's cine-camera or scene-end path
   performs on the actor). The plan text consulted here is silent on this exact defect — no
   `docs/vtmb` pointer is given.
   Recovery required before fix: identify the retail function that runs on scripted-scene/cutscene
   end for an NPC actor (candidate: the `scripted_scene`/`.vcd` completion path referenced in
   dialogue's Voice arm, or the cine-camera teardown in `CDialog::Release`, `0x100e5240`) and what
   it writes to the actor's origin/pose. Until recovered, the port must not guess a fix; it must
   name the seam and cite the retail address once found. Acceptance: Jack's feet are grounded
   immediately on cutscene end, before the player can `+use` him.
2. **Entry by use.** `CBasePlayer::PlayerUse` (`0x10167850`) resolves the use target, tests
   `WillTalk`, clears the NPC schedule, pushes AI schedule `0x6a`, calls the real StartDialog
   (vtable slot 414, `FUN_10178280`). Guards: a live player, no live partner (`player+0xfe8`),
   `IsBusyWithDiscipline(npc)` false, `m_bfAINPCFlags2 & 0x10000000` clear. `Unforced` adds the
   player-side refusal predicate `0x10178170` (combat timers `+0x1d1c`, `+0x1dd0`/`+0x1dd8`, enemy
   count, `+0x1cf8`/`FLT_MAX` sentinel, `0x10175180` while `player+0x1db0` is state 3), cleared by
   `CPlayerEvents::InputClearDialogCombatTimers` (`0x10227250`). On pass: `CDialog::Acquire`,
   `SetDialogPartner`, input lock, remember/holster the drawable weapon (`player+0x1e01`) to
   `item_w_unarmed`, build `camera_cinematic` from `default_camera`.
3. **Dependency evaluation.** `CDialogDependency::Parse` (`0x100e8fc0`) / `TestSimple`
   (`0x100e9760`) / `TestPython` (`0x100e9ff0`) / `ParseDep` (`0x100e9290`) gate each response row:
   trait class, id, inversion (negative threshold ⇒ `<`), threshold, sex gate, Python part,
   compound type/precedence; class 2 (discipline) needs both rating and blood pool, id 6 needs
   Ventrue; class 4 feat `0x16` routes to `FrenzyComparison` (unrecovered, answers false/inverted
   true — excluded from the labelled skill-front set, hides rather than shows disabled).
4. **Turn construction.** `get_pc_responses` (`0x100e82d0`) walks rows to the next `#`/sentinel,
   admits ungated or passing rows; `process_pc_line` (`0x100e8520`) keeps/drops/auto-terminates.
   An empty list without auto-terminate substitutes `"I do not have a valid reply."` plus one
   dummy response. Auto-Link/Auto-End (`0x20`/`0x10`) are hidden control rows: run the automatic
   row's action, then follow its link, never publish the marker as a player-visible response;
   Auto-End requires `LookupSpeechFile` to find audio for the NPC line.
5. **Ordering on pick.** `process_npc_line` (`0x100e8100`) runs the NPC row's col-4 immediately,
   stashes col-5 at `+0x30ea` for `CallPendingNPCEventScript` (`0x100e5c70`), flushed on
   `NPCNotifyDoneTalking` (`0x100e4780`) or `Release`. `CDialog::Pick` (`0x100e4bd0`) is refused
   while the NPC still speaks, maps index → link, echoes history, charges the dependency
   (`pc_charge_dependency` `0x100e8b90`: blood subtract, `AddFakedDisciplineEffect`), runs the
   choice's col-5, then sends or releases (link 0 / auto-terminate). Pick `-1` releases, `-2` is
   hurry (`0x102c0bb0`).
6. **Close.** `CDialog::Release` (`0x100e5240`) flushes pending scripts, fires `OnDialogEnd`
   (`npc+0x5f5c`), clears the partner, destroys the camera, unlocks input, restores the holstered
   weapon (`FUN_10178400`), then continues into `DialogPostProcess`.
7. **Reaction-score consumption (9.9 talk slice).** The RPG reaction-score calculator over
   `reaction.txt`/`reactions000.txt` is landed; the dialogue consumer that reads that score during
   the talk turn is not. Requirement: dialogue must read the calculated score where retail's
   dialogue chain would (unspecified exact call site in the plan text available here — see
   unspecified); the three social domains stay independent (K4): the score feeds
   dialogue only, never combat targeting.
8. **Modernization: M-SAVE — no save inside a conversation.** Retail allows it; the port refuses
   every save entry (menu, quick, auto, console, MCP) while `DialogueSession` is set.
9. **Modernization: M-UI — the panel is the project's own style**, not retail fonts/colours/dot
   glyphs (menu/HUD are not VtMB reproductions).
10. **Modernization: M-REQ — requirement labels replace fonts.** `[ PERSUASION 4/7 ]` (rating /
    required) or `[ DOMINATE 2 ]` + `· 2 BLOOD` for disciplines; only the skill-check front is
    labelled, not a pure Python gate.
11. **Modernization: M-DISABLED — failed skill-front rows stay visible, disabled**, carrying their
    requirement label (feats, disciplines, attributes, abilities, Humanity). A discipline row
    whose rating passes but blood is short is disabled showing cost. Everything else that fails
    (sex gate, clan, `G` flags, Python) stays hidden as retail. Negative-threshold rows never show
    disabled. A disabled row whose text equals an enabled row's text in the same band is dropped.
    Rows number 1..N across enabled and disabled rows; a disabled row's key does nothing.
12. **Modernization: M-REVEAL — choices show immediately** (not withheld during NPC speech); a
    pick cuts the voice, completes the face, flushes the NPC's deferred col-5, then runs the
    pick's col-5, preserving retail's script order.
13. **Modernization: M-SKIP — hurry verb is a skip key** (Space while NPC speaks): ends voice,
    completes face, flushes deferred NPC action as `NPCNotifyDoneTalking` would.
14. **Modernization: M-CAP — no 4-response limit**; a band exceeding four enabled rows logs once
    as an authoring finding for `retail-defects.md`.
15. **Modernization: M-REFUSE — refusal is signalled** via a brief HUD notification ("They won't
    talk right now") instead of retail's silent refusal.
16. **Retail defect reproduced, not repaired** (`docs/vtmb/retail-defects.md` §6): a col-4 with two
    Python halves can never pass (one shipped row).
17. **Named divergence recorded**: `Explain()` evaluates the Python half once even where retail's
    short-circuit would skip it — the single evaluation is what buys the disabled row.

## Design
Presentation (`CHudDialog`/`DialogControl` retail shape) is replaced end-to-end by the project's
own conversation UI panel (M-UI), driven by the same dependency/turn/pick data model
(`FElysiumDlgDependency`, `ElysiumDlg.cpp`, `ElysiumDlgSheet.*`). Response bands render every
passing/disabled row per M-DISABLED/M-REQ, with M-REVEAL/M-SKIP changing only the input-timing
contract, not the script-order guarantees in Requirements 4–6. The reaction-score consumer is a
read-only wire from the 9.9 calculator into the dialogue turn build (Requirement 7); it must not
create a write path from dialogue back into the reaction score.

## Seams
- Consumes: the spoken-line audio service and subtitle publish (0002/AUD3); the reaction-score
  calculator output (9.9, landed); the `.dlg`/dlgexpr parser (9.1, landed); the retail scene-exit
  ground/pose behaviour for Jack (unrecovered — see Requirement 1); barter UI (9.8/8.6, not
  consumed here).
- Provides: the conversation UI panel and turn/pick engine that 0005 (feed/stealth reactions) and
  later dialogue-bearing specs build on; the dialogue-side reaction-score read wire for 9.9.

## Tasks
- [x] 9.1 `.dlg` parser + dlgexpr (landed; `GetStartingLine` fidelity gap tracked outside this spec)
- [~] 9.2 Conversation UI — landed: use-to-talk entry, player refusal predicate, holster, save
  refusal, requirement labels, disabled skill rows, skip, voice take by text column, unit tests
  (2026-09-06). Open:
  - [ ] D8 — player refusal predicate writers (`+0x1d1c`, `+0x1dd0`, `+0x1dd8`, `+0x1cf8` count,
    threat tally `0x1017f770`/`0x1017f8b0`, `player+0x1db0` state-3 enum)
  - [ ] D8 — NPC guards: `IsBusyWithDiscipline`, `m_bfAINPCFlags2 & 0x10000000` writers
  - [ ] D8 — holster: real `player+0x1e01` drawable byte
  - [ ] D8 — dependency: feat `0x16` → `FrenzyComparison`; `AddFakedDisciplineEffect` emitters
  - [ ] D8 — voice: `m_flSpeechVol` authored source; retail's third speech extension
    (`DAT_10562364`)
  - [ ] D8 — corpus: 31 orphan `.lip` files, orphan-`.lip` unit test
  - [ ] Presentation completion: speaker/emotion cues, 8.10 subtitle path (line audio/subtitle
    itself is 0002/AUD3's)
  - [ ] Owner-piloted live acceptance (see Witness)
  - [ ] Requirement 1 — Jack scene-exit ground/pose recovery and fix
- [~] 9.9 NPC disposition & reactions (talk slice) — landed: talk/feed presentation slice, RPG
  reaction-score calculator over `reaction.txt`/`reactions000.txt`. Open:
  - [ ] the dialogue consumer that reads the reaction score during the talk turn

## Open questions
- Exact retail call site where dialogue reads the 9.9 reaction score is unspecified in the plan
  text consulted here.
- The retail function/address responsible for Jack's ground/pose state on scripted-scene exit is
  not yet identified; unspecified. Needs a `vtmb-corpus` pass over the scene-end/cine-camera
  teardown path before a fix can be
  written.
- Whether the 4-response overage (M-CAP) has already produced a logged `retail-defects.md` finding
  is unspecified here.
