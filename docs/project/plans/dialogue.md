# Dialogue plan — closing the conversation gaps

Specifications for the **open** dialogue tasks (P9 rows 9.1/9.2 and the dialogue halves of 9.9,
AUD3, 12.5). Status lives solely in `docs/project/roadmap.md`; a task that lands is deleted here.
Governing design: `docs/architecture/gameplay-systems-architecture.md` (substrate),
`docs/architecture/ui-architecture.md` §8 (screen), `docs/vtmb/game_runtime.md` §5 (retail).

Scope: the interactive conversation only — player talks to an NPC, the NPC line shows and is
voiced, the face and body perform, the camera frames the pair, the player picks a response.
Choreographed scenes, terminals and signs have their own plans.

## Retail chain this plan reproduces

Recovered 2026-09-06 from `vampire.dll` / `client.dll` (record the addresses in
`docs/vtmb/game_runtime.md` §5 under D0 below). Every arm below is either reproduced or named as a
modernization; nothing is silently dropped.

1. **Entry.** `CBasePlayer::PlayerUse` (`0x10167850`) resolves the use target, tests the
   character's `WillTalk` latch (virtual `+0x49c`, set by `InputWillTalk` `0x103418f0`), clears the
   NPC schedule and pushes AI schedule `0x6a`, then calls player vtable slot 414
   (`FUN_10178280`, the real StartDialog). The `StartPlayerDialog*` inputs land on the same slot
   through `CAI_BaseNPCTroika::StartTask` (`0x102a1910`) with schedules `0x6d`/`0x6e`. Common
   guards: a player exists, the player has no live partner (`player+0xfe8`),
   `IsBusyWithDiscipline(npc)` false, `m_bfAINPCFlags2 & 0x10000000` clear. `Unforced` adds the
   player-side refusal predicate `0x10178170`: a set of combat timers on the player
   (`+0x1d1c` no-dialogue-until stamp, `+0x1dd0`/`+0x1dd8`, an enemy count, `+0x1cf8` with
   `FLT_MAX` as the clear sentinel, and `0x10175180` blocking while the entity at `+0x1db0` is in
   state 3). `CPlayerEvents::InputClearDialogCombatTimers` (`0x10227250`) resets them.
   `FUN_10178280` refuses when `m_bForceDialogStart` (`npc+0x6495`) is clear and the predicate
   holds; otherwise `CDialog::Acquire`, `SetDialogPartner`, input lock, remember whether the active
   weapon was drawable (`player+0x1e01`) and switch to `item_w_unarmed`, then build the
   `camera_cinematic` from `default_camera`.
2. **Dependency.** `CDialogDependency::Parse` (`0x100e8fc0`) turns col-4 into a struct: trait
   class (`+0x04`: 0/1 attribute-or-ability, 2 discipline, 4 feat), trait id (`+0x08`), inversion
   flag (`+0x0c`, a negative threshold selects `<` instead of `>=`), threshold (`+0x14`), sex gate
   (`+0x224`: 0 requires female, 1 requires male), a Python part, `m_CompoundType` (1 one only,
   2 AND, 3 OR) and `m_CompoundPrecedence`. `TestSimple` (`0x100e9760`): classes 0/1 compare the
   stat, class 2 requires **both** the discipline rating and the blood pool (stat `0xc`) to meet
   the threshold and refuses discipline id 6 unless `clan_offset == 5` (Ventrue), class 4 is
   `FeatValue >= threshold` except feat `0x16` which routes to `FrenzyComparison`. `TestPython`
   (`0x100e9ff0`) is true iff the eval result is a non-zero `PyInt`; error is false. `ParseDep`
   (`0x100e9290`) writes the wire flags `0x01` discipline, `0x02` feat, `0x04` python present,
   `0x08` event script present, and records the blood cost (threshold for disciplines, 0 otherwise).
3. **Turn.** `get_pc_responses` (`0x100e82d0`) walks rows after the current NPC line to the next
   `#` or sentinel, admits ungated rows and rows passing the dependency, and **stops at 4**.
   `process_pc_line` (`0x100e8520`) returns 1 keep, 0 drop, -1 drop and set auto-terminate. An
   empty list without auto-terminate replaces the NPC text with `"I do not have a valid reply."`
   and one dummy response. Auto-Link/Auto-End are flags `0x20`/`0x10`, one per turn; Auto-End
   becomes automatic only when `LookupSpeechFile` finds audio for the NPC line, otherwise the
   engine forces one visible response with value -1. Choices are hidden while `IsTalking()`
   (`0x102c0aa0`: live scene or `m_flTalkTime > curtime`) through `ShowPlayerChoices`.
4. **Ordering.** `process_npc_line` (`0x100e8100`) runs the NPC row's col-4 immediately and
   stashes col-5 at `+0x30ea` for `CallPendingNPCEventScript` (`0x100e5c70`), flushed when the
   NPC finishes talking, on `NPCNotifyDoneTalking` (`0x100e4780`), or on `Release`. A pick
   (`CDialog::Pick` `0x100e4bd0`, `dialogpick` ConCommand) is refused while the NPC is still
   speaking, maps the index to its link, echoes subtype 5 for history, **charges the dependency**
   (`pc_charge_dependency` `0x100e8b90`: subtract blood, `AddFakedDisciplineEffect`), runs the
   choice's col-5, then `fill_packet`/`message_send` or `Release` on link 0 / auto-terminate.
   Pick `-1` releases, `-2` is the hurry verb (`0x102c0bb0`).
5. **Presentation.** `CHudDialog` (client.dll, `DialogControl` `0x100553f0`) paints
   `"%d. %s"` bottom-up; `CDialogDependency::ToStr` (`0x100ea5e0`) prefixes discipline choices
   with N dot glyphs (`0x7f`); `GetFontForFlagsDependency` (`0x10053f10`) picks font and colour by
   feat id 6/7/8 and discipline id 6; flags exactly `0x20` draw a pulsing
   `"(press 1 to continue)"`; `Dlg_Malk` font for a Malkavian PC; six bracket pairs stripped
   (`0x100e8060`). `CClientModeDialog` (`0x10042a80`) swallows movement key-downs, so held keys
   are ignored, not cancelled.
6. **Voice.** `generate_speech_filename` (`0x100e1680`) builds
   `sound/character/<dlgpath>/line<id>_col_<C>.<ext>`; `0x100e15c0` picks `C` as the **text
   column used**: the clan letter when that clan column is non-empty (only `m` Ventrue and `n`
   Malkavian are pinned by shipped audio), else `f` for a female PC with a col-2 variant, else `e`.
   `LookupSpeechFile` (`0x100e1880`) probes three extensions in order, mp3 first. Text with no
   letters resolves to `sound/character/dlg/ellipses.<ext>` (`0x100df0b0`). A `.vcd` hit plays
   through a `scripted_scene` (handle `npc+0x6554`, carries `.lip` and gestures); otherwise
   `CHAN_STREAM` at `m_flSpeechVol`. PC lines are never voiced (`message_send` `0x100e58e0`).
7. **Close.** `CDialog::Release` (`0x100e5240`) flushes the pending scripts, fires
   `OnDialogEnd` (`npc+0x5f5c`), clears the partner, destroys the camera, unlocks input and
   restores the holstered weapon (`FUN_10178400`).
8. **Data.** Rows are 13 dwords, stride `0x34`: id, male, female, link, dependency, event script,
   then **seven clan text columns** in `clan_offset` order Brujah, Gangrel, Nosferatu, Toreador,
   Tremere, Ventrue, Malkavian (`0x100e65d0`, `read_line_data` `0x100e61d0`). Shipped data fills
   Ventrue (8 rows, `prince1.dlg`) and Malkavian (9,576 rows). A row is dropped at parse when the
   id is negative or the male text is shorter than 2 characters.

## Named modernizations (owner decisions, 2026-09-06)

- **M-SAVE — no save inside a conversation, no exceptions.** Retail allows it; the port refuses
  every save entry (menu, quick, auto, console, MCP) while `DialogueSession` is set. Already the
  rule in `save-architecture.md`; this plan adds the proof that every entry consults it.
- **M-UI — the panel stays the project's own.** Clean, modern Unreal style; no retail fonts,
  colours or dot glyphs. Menu/HUD are not VtMB reproductions.
- **M-REQ — requirement label instead of fonts.** Where retail encodes a feat or discipline
  dependency as a font/colour or dot prefix, the port writes a label before the text in the
  Baldur's Gate 3 shape: `[ PERSUASION 4/7 ]` (player rating / required), `[ DOMINATE 2 ]` plus
  `· 2 BLOOD` for disciplines. Only the skill-check front of the dependency is labelled; a pure
  Python gate is not.
- **M-DISABLED — failed skill checks stay visible, disabled.** A row whose dependency fails
  **only** on the skill-check front is shown greyed and unpickable, carrying its requirement
  label, so the player learns what they lack. "Skill-check front" is every trait the dlgexpr
  grammar can name: feats, disciplines, attributes, abilities **and Humanity** (owner decision:
  a disabled `[ HUMANITY 6/8 ]` row is acceptable). A discipline row whose rating passes but
  whose blood pool is short is also disabled, showing its cost. Everything else that fails (sex
  gate, clan, `G` flags, any Python part) stays hidden as retail. Negative-threshold rows are
  the authored failure routes and are never shown disabled. Rule, precisely: a row is
  *disabled* iff it has a skill component with a non-negative threshold, that component (rating
  or blood) fails, and the whole gate would pass with that component forced true. A disabled
  row whose display text equals an enabled row's text in the same band is dropped (48 corpus
  pairs of the 855 pass/fail siblings share text; the enabled fail route already carries the
  sentence). Rows are numbered 1..N in author order across enabled and disabled rows; a
  disabled row's key does nothing.
- **M-REVEAL — choices show immediately.** Retail withholds the response band while the NPC
  speaks (`ShowPlayerChoices`) and refuses a pick until `IsTalking()` clears. The port shows the
  band with the line; a pick cuts the voice, completes the face, flushes the NPC's deferred col-5
  and only then runs the pick's col-5, preserving retail's script order.
- **M-SKIP — the hurry verb is a skip key.** Retail's pick `-2` becomes a "skip line" input
  (Space while the NPC speaks) that ends the voice, completes the face and flushes the deferred
  NPC action, exactly as `NPCNotifyDoneTalking` would; the choices stay as they are.
- **M-CAP — no 4-response limit.** Retail's cap is a wire-packet limit (four dependency slots).
  The port shows every passing row; a band exceeding four enabled rows is logged once as an
  authoring finding for `retail-defects.md`.
- **M-REFUSE — refusal is signalled.** Retail's combat-timer refusal is silent; the port shows a
  brief HUD notification ("They won't talk right now") through the existing notification path.

## D6 Side effects the corpus demands *(cross-domain; listed so the seams are named here)*

On live corpus counts: `StartBarter` 108 (9.8), `React` 71 (9.9), `SeductiveFeed` 53 (feeding),
`SetExpression` (12.3), `WorldMap` 15 (travel), `DialogDiscipline`, `IsFollowerOf`. Each stays a
logged stub until its domain lands; this plan owns only the **blood charge and faked discipline
effect on pick** (D1) and the `SetDisposition` reaction consumer wire (9.9). The 111
`dialogParticles` and 106 `preBarter` calls are level-script functions and need no native.

## D8 Deferred seams left by the 2026-09-06 landing

Every package above D6 landed on 2026-09-06 (D0, DC, D1, D2, D3, D4, D5, D7 — the retail chain,
the seven modernizations, the corpus slice and the tests; status in `roadmap.md` P9). What
remains is the set of named seams that answer "nothing" until their retail source is recovered,
each carrying a `TODO(dialogue-plan)` marker in code:

- **Player refusal predicate** (`ElysiumPlayer.h`): the writers of `+0x1d1c` (so
  `ElysiumDialogue::DamageRefusalSeconds = 5.0` is the port's own number), `+0x1dd0`, `+0x1dd8`,
  what `+0x1cf8` counts, the threat tally (`0x1017f770`/`0x1017f8b0`, answers 0 — no player-side
  aggregate exists yet), and `player+0x1db0`'s state-3 partner enum.
- **NPC guards** (`ElysiumNpc.h`): `CBaseCombatCharacter::IsBusyWithDiscipline` and the
  `m_bfAINPCFlags2 & 0x10000000` word and its writers (both answer "not busy").
- **Holster** (`ElysiumPlayerEntity.cpp`): the real `player+0x1e01` drawable byte; the
  `CBasePlayer.DialogHolster` stub fires when no `item_w_unarmed` is carried.
- **Dependency** (`ElysiumDlg.cpp`, `ElysiumDlgSheet.*`): feat `0x16` → `FrenzyComparison`
  (answers false, inverted true) and is excluded from the labelled-skill-front set — the row
  hides rather than showing a greyed `[ FRENZY 0/1 ]` retail never draws
  (`FElysiumDlgDependency::IsUnrecoveredFrenzyFront`); `AddFakedDisciplineEffect` logs until the effects domain lands
  `dialog_domination_emitter`/`dialog_presence_emitter`; the five unpinned clan take letters
  (Brujah…Tremere) probe nothing.
- **Voice** (`ElysiumLineService.*`): `m_flSpeechVol` has no authored source in the corpus (the
  seam answers 1.0); retail's third speech extension (`DAT_10562364`) is unread.
- **Named divergence recorded**: `Explain()` evaluates the Python half once even where retail's
  short-circuit would skip it — that single evaluation is what buys the disabled row.
- **Retail defect reproduced, not repaired** (`docs/vtmb/retail-defects.md` §6): a col-4 with two
  Python halves can never pass (one shipped row).
- **Corpus**: 31 orphan `.lip` files with no audio member do not deploy (a `.lip` miss is
  ordinary at runtime); closing it needs an orphan-`.lip` unit. `scripts/**`, `audio/catalog.json`,
  `sound/Schemes/*`, `sound/usable/soundgroups.json` and `expressions/*` remain legacy reads.
- **Live acceptance** (owner-piloted, out of unit-test scope): Jack's tutorial conversation by
  use, a refused use under the combat timer, a disabled `[ PERSUASION 4/7 ]` row beside its
  failure route, the female take, a Dominate row's blood charge, skip mid-line.

## Order and acceptance

D6 by its domains; D8 as each retail source is recovered. Slice acceptance (live, owner-piloted):
`sp_tutorial_1`'s Jack conversation opens by **use**, posts the refusal hint while a combat timer
runs, shows a disabled `[ PERSUASION 4/7 ]` row beside its enabled failure route, plays the
female take for a female PC, runs Jack's NPC col-5 only after his line finishes or is skipped or
cut by a pick, cannot be saved from, and closes into `DialogPostProcess` as before.
