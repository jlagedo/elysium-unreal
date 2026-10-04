# Brief D1 — V3d: the dialogue hold as retail runs it

Rewritten 2026-10-04 from `packets.md` (R1, R2 — read from the listing) and the tree after V3b
(`7106f4c5`). **V3c lands before you and moves `ElysiumNpc.cpp` / `ElysiumNpcStartTask.cpp`
again: re-locate every line below by Grep on the symbol before editing.** Read `README.md` here
(the "Corrections" block first — it overrides §1; then §2 M12–M13, §4 V3d, §8 Q4/Q7),
`packets.md` R1 and R2, and the retail walks: `docs/vtmb/npc-ai/conditions-and-states.md`
§ "`0x102c1400` — the dialogue upkeep …" (line ~406), `docs/vtmb/game_runtime.md` § "Runtime /
branching" (line ~1296), § "Retail conversation chain", § "Dialogue close", `npc-ai/shape.md`
§ "The dialogue-release path `0x102c0360`". Runs beside D2 and D3. You never build.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcDialogue.h`, `ElysiumNpcDialogue.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumEntityWorldDialogue.cpp`, `ElysiumDialogueSession.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcRunTask.cpp` (`RunDialogActivity`, `307-321`)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcStartTask.cpp` (`StartTask19PlayerStartDialog`,
  `586-590`, only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcTroikaHelpers2.cpp` (`OnDialogRelease`, `347-377`)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcSocial10.cpp` (`IsInDialog`, `89-94`, only)
- `Source/ElysiumUE/Private/Substrate/ElysiumCombatCharacter.cpp` (`SetDialogPartner`, `452-456`;
  the `TickGaze` dialogue arm's `+0xFE8` stand-in, `843-849`)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcPayphone.h`, `ElysiumNpcPayphone.cpp` (the hiding
  `DialogPartner`, item 9 — no other lane owns the payphone)
- `Source/ElysiumUE/Public/ElysiumPlayer.h` (the `DialogPartner` comment, `994-1000`, only — no other
  V3d lane owns this header)
- `Source/ElysiumUE/Private/Map/ElysiumMapActor.cpp` (`QueryPlayerUse`, `1284-1444`, only — item 10,
  the `dialog_use_hold` triage; **placed here pending the owner's ruling**, see the triage row)
- Tests: `ElysiumDialogueCameraTests.cpp`, `ElysiumDialogueTests.cpp`, `ElysiumDialogueEntryTests.cpp`,
  `ElysiumNpcThinkCadenceTests.cpp` (`395-405`), `ElysiumNpcKernelRunTaskTests.cpp` (only where it
  pins the old seam's answer), `ElysiumNpcKernelTroikaHelpersTests.cpp` (only where `OnDialogRelease`
  moves)

## The job

Retail's shape, which every item serves: the three inputs and `+use` **install a program** on the
NPC through `0x102ae750(id, 0)`; the conversation opens inside the player's start-dialog
(`FUN_10178280`, player slot 414) → `CDialog::Acquire 0x100e05f0` → the NPC's **`StartTalking
0x102c0270`**; `TASK_RUN_DIALOG 0xb9` holds through **`0x102c1400`**; the close is **`CDialog::Release
0x100e5240`** → the NPC's **`0x102c0360`** once. A dialogue writes no `m_NPCState`, takes no body.

1. **The three inputs** (`FElysiumNpcDialogue::StartForced` / `StartRemote` / `StartUnforced`,
   `ElysiumNpcDialogue.cpp:66-110`; dispatched from `ElysiumNpc.h:688-694`). R2 item 1, in this order,
   each guard a silent return: a player exists; the **player's** `GetDialogPartner()` does not resolve
   live; `!IsBusyWithDiscipline`; `!(m_bfAINPCFlags2 +0x14bc & 0x10000000)` (NO_DIALOG_PERSISTENT);
   Unforced only: `!0x10178170(player)` (`EntryRefusalReason`'s player half). Then: the `+0x5bac`
   store — **a float into `FElysiumNpcBase::SpecialDistanceAccum`** (`ElysiumNpcBase.h:195`; read by
   task `0xd9`, `ElysiumNpcStartTask.cpp:1907`): the variant's float when its type is float, else the
   `.rdata` float `0x104454c4` (Forced and Unforced only; **Remote never reads its variant nor writes
   `+0x5bac`**); `FinishTalking` (`ElysiumNpcSocial10.cpp:197`); slot 614 `ResetThinkTimers(Now)`;
   `bForceDialogStart` (`+0x6495`) = 1 / 1 / **0**; then `SetSchedule(0x6d, false)` (Forced,
   Unforced) / `SetSchedule(0x6e, false)` (Remote) — `FElysiumNpc::SetSchedule(int32, bool)`
   (`ElysiumNpcMaintain.cpp:49-83`) **is** `0x102ae750`: translate, `0x102ae780`'s refusals (state or
   ideal 7, `!IsAlive`), `ForceScheduleChange 0x102ae490` (`:85-126`: cancels a live cine, clears
   `PRESERVE_PATH` unless the nav type is climb/jump, slot 435 `OnScheduleChange`), install. Not a
   forced `SetSchedule(id, true)`. **They open nothing**: no `Begin`, no `OnDialogBegin`, no
   `PrepareBodyForDialogue`; the `DialogFlags` integer and its `RE46` stub go (the value is the float
   above). Keep the opener kind on the NPC (`DialogOpener`) for item 3. The debug stamp `+0x1b30/34`
   is not ported (named).
2. **`+use`** (`FElysiumNpcDialogue::BeginPlayerUse`, `ElysiumNpcDialogue.cpp:247-279`). R2 item 5,
   in order: slot 295 `CanTalk(Activator)` (`FElysiumNpc::CanTalk`, `ElysiumNpcSocial10.cpp:96`) —
   false returns with **no** ordinary use and no notification; slot 614 `ResetThinkTimers`;
   `SetSchedule(0x6a, false)` (`0x102ae750(npc, 0x6a, 0)`); then the player's start-dialog (item 3)
   with opener `Use`. **There is no `ClearSchedule`.** The refusal predicate `0x10178170` belongs to
   the start-dialog (item 3), not here: M-REFUSE's notice moves there (it stays the named
   modernization for the `Use` opener only). `CanPlayerFocus` (`:194-218`) stays the reticle's focus
   question; its retail word (the Troika class's slot 35) is not recovered — do not touch it.
3. **The player's start-dialog** (`FUN_10178280`), reached from `TASK_START_PLAYER_DIALOG 0xda`
   (`StartTask19PlayerStartDialog`, `ElysiumNpcStartTask.cpp:586-590`, which passes the NPC's stored
   opener instead of a hard-coded `Forced`) and from item 2. `FElysiumNpcDialogue::Begin` (`:15-64`)
   becomes it: the refusal predicate unless `bForceDialogStart`; `OpenConversation` (`:299-383`) =
   `CDialog::Acquire`; the tail the world already runs (`StartPlayerDialogTail`). It loses
   `PrepareBodyForDialogue`, `OnDialogBegin` and every body-owner argument.
4. **`StartTalking 0x102c0270`** — a new `FElysiumNpcDialogue` door, called once from inside the open
   (`FElysiumEntityWorld::OpenDialog`, `ElysiumEntityWorldDialogue.cpp:249-...`, at `CDialog::Acquire`'s
   call). R1 item 3: it clears `m_bForceDialogStart`, sets `m_bCutsceneForceLOD +0x1590`, bumps
   `m_nTimesTalked` (`TimesTalked`, `ElysiumNpc.h:57` — **counted at the open, no longer at the end**),
   runs `FinishTalking`, cancels a live cine, fires `m_OnDialogBegin +0x5f44` with the player as
   activator, and `SetDialogPartner(player)` on the NPC — **the only writer of the NPC's `+0xfe8`**.
   Take the order of those writes, and the call's place inside `Acquire` (before or after
   `fill_packet` / `process_npc_line` = `Conversation->Start()`, `:327`), from the listing
   (`vtmb_asm 0x102c0270`, `0x100e05f0`): the packet lists the effects, not their order. Set
   `bInDialog` here (D2 deletes `BeginDialogueBodySession`, its old writer, `ElysiumNpc.cpp:2072`).
5. **`m_hDialogPartner`** (M13). `FElysiumCombatCharacter::SetDialogPartner` (`ElysiumCombatCharacter.cpp:452`):
   store the handle; an invalid handle clears (retail stores `-1` for NULL). Writers: the NPC's word
   by `StartTalking` (item 4) and `OnDialogRelease` (item 7) only; the **player's** word by the
   start-dialog tail (`StartPlayerDialogTail`, `ElysiumEntityWorldDialogue.cpp:510`: set; the bark /
   refused arm `:518-545`: clear, and rewrite that block's "the port has no `m_hDialogPartner` field"
   comment, `:529-542`) and `EndPlayerDialog 0x10178400` (`EndPlayerDialogTail`, `~:703`: clear). `TickGaze`'s
   dialogue arm (`ElysiumCombatCharacter.cpp:843-849`) reads `GetDialogPartner()`. `OpenDialog` loses
   the `FElysiumBodyOwnerToken` parameter and both `BeginDialogueBodySession` calls (`:258-292`);
   `FElysiumDialogueSession::BodyOwner` goes (`ElysiumDialogueSession.h:322`); replacement stays atomic
   by the session itself. D2 rewrites `HasLiveDialogPartner` (`ElysiumNpcAnim.cpp:81-88`) to resolve
   the NPC's handle.
6. **`IsInDialog 0x102c1170`** (`ElysiumNpcSocial10.cpp:89-94`, today `bInDialog || IsTalking`): the
   four retail terms in order — `m_bIsTalking +0x64c0` (`bIsTalking`); `Dialogue.DialogQue` non-empty
   (`+0x64ec`); the NPC's `GetDialogPartner()` resolves live; `Dialogue.DialogScene` resolves live
   (`+0x6554`, the handle only). `bInDialog` is port bookkeeping; it never answers `IsInDialog`.
7. **`TASK_RUN_DIALOG`** holds and releases through `RunDialogActivity` = `0x102c1400`
   (`ElysiumNpcRunTask.cpp:307-321`), ported from R1 item 1 / `conditions-and-states.md` ~406: (1)
   `!IsInDialog` → `OnDialogRelease()` (`0x102c0360`, fires nothing — the partner is already gone),
   answer −1; (2) a live `DialogScene` whose `+0x498` byte is 0 → `UTIL_Remove` it, handle = invalid —
   **the byte is a seam** (`DialogSceneReportsDone`, `ElysiumNpcDialogue.cpp:425-439`): while it cannot
   be read, step 2 removes nothing (named), never "byte 0 = finished"; (3) `bIsTalking &&
   !Dialogue.IsTalking(...)` (`0x102c0aa0`) → `FinishTalking`; (4) queued line, `m_bInDispositionFidget
   +0x64e0` clear (no port word: a seam answering 0), not talking → `0x102c0520(this, que, 0, 0)` then
   clear the queue (`OnDialogFilePlayed`, `ElysiumNpc.cpp:1335`, is that player's talking half only —
   name what it lacks); (5) answer `ActivityNumber` (`m_Activity +0xfec`); only when
   `bSequenceFinished` (`+0x65c`): `Slot611()` → ≥0: `CommitForcedSequence(seq)` (`0x10260a50`),
   `m_flCycle +0x6f8 = 0`, answer `0xf1`; <0: the `DevMsg` `"%s could not look up disposition
   sequence!!!"`, answer `1`; (6) live partner → `CDialog::ShowPlayerChoices(dialog, !scene && !talking
   && queue empty)` every tick (a seam into the world's dialogue UI if no door exists; name it).
   The two task arms (`ElysiumNpcStartTask.cpp:1685-1698`, `ElysiumNpcRunTask.cpp:1176-1189`) are
   already retail: −1 completes (run arm also clears `COND_HEAR_PLAYER 0x6f`), else slot 310 + yaw.
   No fail exit, no facing, no route stop.
8. **One close, one `OnDialogEnd`** (Q7, settled by R1 item 4). `CDialog::Release`'s port is
   `EndDialogSession` (`ElysiumEntityWorldDialogue.cpp:1317-1403`). Its order becomes retail's: flush
   (`Conversation->Close()`, `:1340-1343`) → window off (`Presenter()->CloseDialog`, today `:1382-1388`,
   after) → the NPC's `OnDialogRelease()` → `EndPlayerDialogTail` (`:1373`, the camera remove,
   mobilize, holster restore — `0x10178400`) → unload. `OnDialogRelease` (`ElysiumNpcTroikaHelpers2.cpp:347-377`)
   is `0x102c0360` whole: only when the partner resolves live: `bCutsceneForceLOD = false`, fire
   `OnDialogEnd` **with the partner as activator** (`:371-375` uses `Handle` today), then
   `SetDialogPartner(invalid)`; clear `bInDialog`. Delete from the world: `EndDialogueBodySession`
   (`:1374-1379`) and the queued `EndDialog` input (`:1390-1402`). `FElysiumNpcDialogue::End`
   (`ElysiumNpcDialogue.cpp:112-168`) becomes a bare script close only: if this NPC owns the open
   session, `World->CloseDialog(false)`; nothing else — no `TimesTalked`, no `OnDialogEnd`, no holster,
   no `NextThink` re-arm (retail re-bases only at the start). Leave `EndDialog` registered (V7's
   question). The program completes on the first `TASK_RUN_DIALOG` tick on which item 6's four terms
   are clear.
9. **The payphone's hidden word.** `FElysiumNpcPayphone::DialogPartner` (`ElysiumNpcPayphone.h:57`,
   doc `:47-56`) hides the base `+0xfe8`: delete it; `ResolveDialogPartner` (`ElysiumNpcPayphone.cpp:242-251`)
   resolves `GetDialogPartner()`. Its `DialogUpkeepTick` already calls `RunDialogActivity`
   (`:257-259`) and ignores the answer — unchanged; its `:276` comment about
   `BeginDialogueBodySession` is rewritten.
10. **The use focus** (`dialog_use_hold` triage, game red). `AElysiumMapActor::QueryPlayerUse`
    (`ElysiumMapActor.cpp:1284-1444`) treats any blocking hit that is not a registered anchor as an
    occluder (`FindAnchor` `:1312-1318`, the exact ray `:1352-1361`, `VisibleTo` `:1340-1350`, the
    fallback `:1429-1437`), and an NPC's own Pawn capsule (`ElysiumNpcBody.cpp:94-99`; `ElysiumUse`
    defaults to Block, `Config/DefaultEngine.ini:36`) wraps the visual its proxy hangs from. Retail's
    `0x10167470` → `FindEntityFOV 0x10341c30` hit **is** the entity. Accept a hit on the anchor
    owner's own body as that owner: the test `ElysiumFeedTargeting::HitBelongsToCandidate`
    (`Map/ElysiumFeedTargeting.h:38`) already answers it for the feed query (proven by
    `ElysiumPlayerWorldTests.cpp:1894-1927`). Nothing else in the query changes.
11. **The comment** `Public/ElysiumPlayer.h:994-1000` (`DialogPartner`): the one store is
    `SetDialogPartner 0x10107050`; on an NPC it is set by `StartTalking 0x102c0270` and cleared only by
    `0x102c0360`; on the player it is set/cleared by `FUN_10178280` and cleared by `EndPlayerDialog
    0x10178400`. Drop the "SEAM (V3a)" sentence.
12. **Tests.** Delete `Elysium.Arm.DialogueCamera.BodyOwnerLifecycle` (`ElysiumDialogueCameraTests.cpp:357-`).
    `ElysiumDialogueTests.cpp:1747-1756` asserts the dialogue cancels the waveover's scripted owner —
    that cancel is retail (`StartTalking` and `ForceScheduleChange` both cancel a live cine): keep the
    assertion if it reads the cine word, cut only token / owner lines. `ElysiumNpcThinkCadenceTests.cpp:400`
    calls `BeginDialogueBodySession`: re-point it at the input (the reset is retail's at the input,
    slot 614). `ElysiumDialogueEntryTests.cpp:235` (`TimesTalked == 1`) must still hold with the count
    at the open. `Elysium.Arm.Session.SaveRefusedInDialogue` stays (Q4; R2 item 7 found no retail
    save refusal). No new test of a port mechanism.

## Not yours

`ElysiumNpc.{h,cpp}` (D2: `BeginDialogueBodySession` / `EndDialogueBodySession` /
`PrepareBodyForDialogue` — its place release `ElysiumNpc.cpp:2103-2109` is replaced by item 1's
`ForceScheduleChange` → `OnScheduleChange 0x102a0940` path —, `ThinkInDialog`, `DialogueBodyOwner`,
`GetDialogueBodyOwner`, `ReleaseAllBodyOwnership`'s dialogue lines), `ElysiumNpcAnim.cpp`,
`Public/ElysiumEntity.h`, `ElysiumNpcMaintain.cpp` (D2); the mind and the debug views (D3); every
arena record (the integrator).

## Open — stub, name, report (packets leave these)

- `.rdata 0x104454c4`, the `+0x5bac` fallback: 1,331 readers, no writer (`vtmb_globals`); its value is
  not read — almost surely `0.0f` (inferred). Read it once from the image, or name the constant with
  the address.
- The order inside `0x102c0270` and its call site in `0x100e05f0` (item 4).
- `+0xa8` on the partner (R1 item 5, inferred the player) and the `CDialog::ShowPlayerChoices` door.
- Constructor / restore writes of `+0xfe8` (not searched). Whether a silent close (map teardown,
  same-NPC replacement, owner death) runs `CDialog::Release` → `0x102c0360` in retail: keep those
  paths clearing the partner without the output, named at the line.

## Rules

README § "Rules for every agent of V3". The query budget: 10 s warns (log to
`$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s stops, never retried as-is; never read a file over
~200 KB whole (read `ElysiumNpc.cpp` and `ElysiumEntityWorldDialogue.cpp` by ranges). Look an
address up with `uv run elysium research where|section <addr>` before searching `docs/`. Text
through Grep / Read / Glob, never shell `grep`/`cat`/`sed`. No sleep, no polling loop. Touch only
these files; a line another file needs goes in the report, exact. Do not build, do not run the
arena or a suite, do not edit a record, do not commit.

## Report (≤300 words)

Per change, the retail address; the order of one close from `dialog_choose end` to the NPC's next
selection; every symbol D2 must keep or delete for your files to compile (exact file:line); each
open item above and how it is stubbed (named, answering "nothing").
