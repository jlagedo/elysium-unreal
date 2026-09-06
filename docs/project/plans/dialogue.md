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

## D0 Record the recovery *(first; documentation only)*

Write the eight-arm chain above with its addresses into `docs/vtmb/game_runtime.md` §5 and fix
the column schema there and in `docs/architecture/seam_map_dialogue.md`: cols 6–12 are the seven
clan columns, col 11 is Ventrue, the audio letters `e/f/m/n` are **text-column takes**, not
languages (also the wording in `pipeline/.../dialogue_glb/model.py` `AUDIO_LANGUAGES`). Retire
the stale "no parser exists" note in `rebuild-strategy.md` B9. Move the two dialogue stubs
(`CAI_BaseNPC.StartPlayerDialogUnforcedGate`, `CAI_BaseNPC.DialogOpenerInteger`) to their
recovered state: the gate is `0x10178170`; the integer is stored in `m_flSpecialDistanceAccum`
and its reader is still open. *Acceptance:* docs cite the addresses; no code change.

## DC The dialogue corpus slice *(readers off the legacy tree; owner calls 2026-09-06)*

Every dialogue read is still on the legacy loose export (`FElysiumContentPaths::Root()`):
`DlgFromDialogname` (`.dlg`), `ScenesDir` (per-line `.vcd`), `LipDir` (`.lip`), `SoundDir`
(voice `mp3`/`wav`) and `ScriptsDir`. Only `VdataDir` is on `CorpusRoot()`, and the migration's
legacy ledger still lists `dlg/**`, `scenes/**`, `lip/**`, `sound/**` as unmigrated runtime reads.
The published units already exist under `exports_v2/`: 147 `dialogues/*.glb`, `scenes/**`,
`sounds/**` (BIN payload, with the same-stem `.lip` as a member), `scripts/**`. Expression and
phoneme tables are already imported (`DA_ExpressionTables`, R8.5) and need nothing here.

- **Reader model: capsule bytes + the existing parsers.** `dialogue_glb` and `scene_glb` adopt
  the capsule rule (schema 1.1.0, BIN chunk with the winning source bytes, hash-checked against
  `sourceResolution`) exactly as vdata did; the decoded extension stays the validation record.
  `FElysiumDlgFile`, the `.vcd` scene parser, the `.lip` parser and the audio decoders are
  unchanged. The sound unit already carries its bytes and its `.lip` member.
- **Scope: the dialogue set plus the whole sound family.** `uv run elysium import dialogue`
  deploys `dialogues` → `Content/ElysiumCorpus/dlg/**`, `scenes` → `.../scenes/**`; `uv run
  elysium import sound` deploys every sound unit → `.../sound/**` with its `.lip` beside it, so
  `SoundDir()` has one root. `DlgFromDialogname`, `DlgDir`, `ScenesDir`, `SceneFile`, `LipDir`,
  `LipFile`, `SoundDir`, `SoundFile` flip to `CorpusRoot()`. Scripts stay on the legacy tree
  until their own slice (the ScriptFS mounts are a separate reader).
- Import lanes follow the vdata lane's shape: recipe stamps, per-unit failure isolation, an
  `import_report.json`, byte-equality against the capsule.
- Update the legacy ledger rows in `seam_migration.md` (append a dated entry, do not rewrite)
  and the "Dependencies"/"Audio join" paragraphs of `seam_map_dialogue.md`.

*Tests:* pytest for the two capsule upgrades (BIN present, hash matches, decode unchanged) and
the two lanes (deployed bytes equal the capsule's, `.lip` lands beside its audio);
`Elysium.Content.DlgJackTutorial`, `.DialogueCameraDemand`, `.TheatreLipsync` and the choreo
scene content tests pass against `-ElysiumCorpusRoot=` with the legacy root unset.
*Deps:* D0. Lands before D3 so every later package is built and tested on the corpus paths once.

## D1 The dependency object *(replaces the text rewrite)*

`ElysiumDlgExpr::ConditionToPython` rewrites the skill-check into `pc.CalcFeat(...) >= n` text
and leaves `Humanity -5` unrecognised (`-` is not a relop), so **447 corpus conditions never
pass** — every low-humanity and every `Persuasion -7` failure route. Retail parses a struct.
Build `FElysiumDlgDependency` in `ElysiumDlg.h`:

- Fields: `Trait` (name), `Class` (Attribute/Ability/Discipline/Feat/Unknown, resolved through
  the rulebook tables `stats.txt`/`feats.txt`/disciplines at parse time, `Unknown` fails closed
  with a log), `Threshold`, `bInverted` (negative threshold → `<`), `SexGate`
  (None/Male/Female from `M_`/`F_`), `Python` (the remaining expression, still normalised for the
  host), `Compound` (SkillOnly/PythonOnly/And/Or), `Precedence` (skill-first/python-first from
  token order). `BloodCost` = threshold for a discipline, else 0.
- `Test(const IElysiumDlgSheet&)` reproduces `TestSimple` + `Test`: attribute/ability compare,
  discipline requires rating **and** `BloodPoolValue() >= Threshold` and refuses discipline id 6
  unless Ventrue (id→name join through the rulebook; if it does not resolve, leave the Ventrue
  gate as a named seam), feat via `CalcFeat` with feat `0x16` routed to the frenzy comparison
  seam. Python half through the existing host with the `PyInt` truth rule.
- `Explain()` returns `FElysiumDlgGateResult { bPasses, bSkillFailedOnly, Label{Trait,
  Required, Have, BloodCost} }` — the counterfactual pass (skill forced true) is computed here so
  M-DISABLED needs no second evaluation.
- `FElysiumDlgConversation` gains `VisibleChoices()` entries of `{Index, bEnabled, GateResult}`
  and a `Charge(choice)` step on pick that subtracts blood and fires
  `AddFakedDisciplineEffect(npc, trait, level)` through a new combat-character seam (the effect
  emitters are the effects domain's; the seam fires and logs until they land).

*Tests:* `Elysium.Substrate.DlgExpr` extended over the full skill vocabulary (`Humanity -8`,
`F_Seduction 4`, `Seduction 3 & OneOfSet(1,4)`, `Persuasion 7 & pc.humanity >= 5`, `Intimidate 7
& G.Patch_Plus == 1`, `Seduction 4 & not IsMale(pc)`); a corpus test that parses every col-4 of the
147 files and asserts zero `Unknown` classes and zero unparsed skill fronts; a discipline test
asserting the blood gate and the charge on pick. *Deps:* rulebook tables (9.4, landed).

## D2 Retail turn rules in the branch machine

All pure `FElysiumDlgConversation` changes, unit-tested without a world:

- **Deferred NPC col-5.** `EnterNpcLine` runs col-4 now and parks col-5 as `PendingNpcAction`;
  `FlushPendingNpcAction()` runs it once, called by the world at voice completion, at skip, on a
  pick (before the pick's own col-5, matching `Pick`'s order after `NPCNotifyDoneTalking`), and on
  `Close`. A conversation with no audio flushes at the manual continue.
- **No cap (M-CAP).** Every passing row is admitted; log once per band when more than four
  enabled rows survive (699 bands author more than four rows; they are gate-exclusive by design,
  so an overflow is an authoring finding worth a line in `retail-defects.md`).
- **No valid reply.** Empty enabled list without a pending automatic and without terminal
  intent substitutes the NPC text `I do not have a valid reply.` with one Continue, as retail.
- **Auto-End needs audio.** Keep the existing voice-completion boundary; the manual Continue
  fallback is now the retail rule (forced visible response, value -1), not a port fallback.
  Re-label `bAutomaticFallback` accordingly.
- **Pick while speaking (M-REVEAL).** A pick during the voice is accepted: the world stops the
  voice and lipsync, the machine flushes `PendingNpcAction`, then runs the pick's col-5 and
  follows the link. `bNpcSpeaking` is still published for the skip hint.
- **Pick refusal after the turn changed** stays as the revision guard.

*Tests:* extend `Elysium.Substrate.DlgBranch` and `DlgAutomatic` for the flush order (NPC col-5
before the pick's col-5, once per turn, on close), the cap, the no-reply substitution and the
skip path. *Deps:* D1.

## D3 Use-to-talk entry and the player refusal predicate

`FElysiumNpc` never answers `IsUsable()`, so walking up to an NPC and pressing use does nothing;
only scripted openers work. Reproduce arm 1:

- `FElysiumNpc::IsUsable()` true when `dialogname` is set; `CanPlayerFocus` = `bWillTalk`
  (the `WillTalk` latch already on `FElysiumCombatCharacter`) `&& !bInDialog && !IsInert()
  && !IsBusyWithDiscipline()` (seam on the combat character) and the `AINPCFlags2 0x10000000`
  bit (seam, answers clear). `BeginPlayerUse` → `BeginDialog(EElysiumDialogOpenerKind::Use, 0)`;
  the use session ends immediately (dialogue owns the body through its own token).
- **Player refusal predicate** as a first-class `FElysiumPlayerEntity::DialogRefusalReason()`
  with the retail fields named: `NoDialogueUntil` stamp (`+0x1d1c`), the two auxiliary stamps,
  the threat count, the `FLT_MAX`-sentinel float (`+0x1cf8`) and the state-3 partner check.
  Fed today by the damage path (a hit stamps `NoDialogueUntil`, the retail duration is
  unrecovered → seam with a documented constant) and cleared by
  `InputClearDialogCombatTimers`, which currently only counts. `StartPlayerDialogUnforced`
  consults it (closing the `UnforcedGate` stub); `Use` consults it unless `bForceDialogStart`.
  A refused use posts the M-REFUSE notification; scripted openers refuse silently as retail.
- **On open:** lock player movement input under the Dialogue scope (already the UI-only scope,
  priority 40), holster to unarmed and remember drawability; **on close** restore. Wire to the
  wielded-weapon switch; if the switch verb is not yet exposed, add the seam and log.
- Use-focus glyph: the NPC gets the talk icon from the use-icon atlas (PL3) when focusable.

*Tests:* `Elysium.Substrate.NpcUseStartsDialog` (focus, begin, body token, `OnDialogBegin`,
`OnDialogEnd` on close, `times_talked`), `Elysium.Substrate.DialogRefusalPredicate` (stamped
refusal, cleared by the input, forced start bypass), `Elysium.Content.DlgJackTutorial` extended
to open Jack by use. *Deps:* none; lands first for playability.

## D4 Voice take, sidecars and the seven clan columns

- Parse and store all seven clan columns (`TextClan[7]`, Malkavian = slot 6); `RawFor()` takes
  the player's clan offset and prefers a filled clan column over the gendered text, as
  `get_display_text` (`0x100e1ad0`) does.
- `FElysiumLineService::DialogueLineSource` takes the **chosen column letter**: clan letter when
  that clan column is filled (`m` Ventrue, `n` Malkavian; the other five letters are unpinned →
  probe nothing and log), else `f` when female and col-2 exists, else `e`. The same stem feeds
  `LineScene.Begin` (`.vcd`) and `BeginDialogueLipsync` (`.lip`) so a female take moves the
  female mouth.
- Extension probe order mp3, wav (third unread → seam) through the audio subsystem's resolver;
  a letterless line resolves `character/dlg/ellipses`.
- Speech volume from the NPC `m_flSpeechVol` keyvalue if authored (seam).

*Tests:* `Elysium.Audio.DialogueTakeLetter` (male/female/Malkavian/Ventrue rows → letter),
`Elysium.Content.DialogueTakes` over the export: every `_col_f/_col_n/_col_m` file is reachable
by some (sex, clan) pair; corpus parse keeps the 8 Ventrue rows. *Deps:* D1 for clan offset.

## D5 Presentation: labels, disabled rows, skip, project fonts *(M-UI, M-REQ, M-DISABLED, M-SKIP)*

- `FElysiumDialogueView` grows `Choices[] { Text, Label, bEnabled, Kind
  (Plain/Feat/Discipline/Attribute), Have, Required, BloodCost }`, `bNpcSpeaking`, `bCanSkip`.
  The presentation subsystem fills it from D1's gate results; the UI never evaluates anything.
- `SElysiumDialogueBox`: one row per choice in author order, numbered 1..N across enabled and
  disabled rows so numbering is stable; the label is a separate run before the sentence in the
  accent colour, `[ PERSUASION 4/7 ]` (player rating / required, shown on passing rows too as
  `[ PERSUASION 7/7 ]`); disabled rows dimmed, non-focusable, number key ignored; discipline
  rows append `· N BLOOD`; the band is visible from the first frame (M-REVEAL) with a small
  "Space: skip" hint while `bNpcSpeaking`; Continue for terminal and no-audio automatic turns.
  Fonts through `ElysiumUIStyle` (Spectral for the line, Inter for choices), not `FCoreStyle`.
- `ChoiceForKey` maps a number to the row index and the screen refuses disabled rows; Space
  while speaking is the skip verb → `PresentationSubsystem::DialogueSkip` →
  `World->PlayerDialogSkip()` (stop the voice, complete lipsync, flush D2's pending action).
  Retail's history window is not reproduced (M-UI).

*Tests:* `Elysium.UI.DialogueChoiceLabels` (label text for each dependency kind, disabled
mapping, dedup of same-text pairs), `ElysiumUIScalingAndDialogueInputTest` extended for disabled
keys and skip. Owner-piloted live check on Jack's tutorial and a discipline-gated line
(`prince1.dlg` Dominate rows). *Deps:* D1, D2.

## D6 Side effects the corpus demands *(cross-domain; listed so the seams are named here)*

On live corpus counts: `StartBarter` 108 (9.8), `React` 71 (9.9), `SeductiveFeed` 53 (feeding),
`SetExpression` (12.3), `WorldMap` 15 (travel), `DialogDiscipline`, `IsFollowerOf`. Each stays a
logged stub until its domain lands; this plan owns only the **blood charge and faked discipline
effect on pick** (D1) and the `SetDisposition` reaction consumer wire (9.9). The 111
`dialogParticles` and 106 `preBarter` calls are level-script functions and need no native.

## D7 Save refusal proof *(M-SAVE)*

Enumerate every save entry (pause menu, quick-save key, autosave triggers, `elysium.save`
console, MCP save tool) and assert each consults `ScriptedSessionSaveBlockReason()` and refuses
with the "a conversation is open" reason while `DialogueSession` is set, including the
Auto-Link wait and the no-audio Continue state. *Test:* `Elysium.Session.SaveRefusedInDialogue`.
*Deps:* none.

## Order and acceptance

D0 → DC → D3 → D1 → D2 → D5 → D4 → D7; D6 by its domains. Slice acceptance: `sp_tutorial_1`'s Jack
conversation opens by **use**, posts the refusal hint while a combat timer runs, shows a disabled
`[ PERSUASION 4/7 ]` row beside its enabled failure route, plays the female take for a female
PC, runs Jack's NPC col-5 only after his line finishes or is skipped or cut by a pick, cannot be
saved from, and closes into `DialogPostProcess` as before. Docs to update on landing: `roadmap.md` P9, this file, `seam_migration.md` (DC),
`game_runtime.md` §5, `seam_map_dialogue.md`, `ui-architecture.md` §8,
`gameplay-systems-architecture.md` §2/§5.5, `save-architecture.md`, `audio-architecture.md`.
