# Brief D1 — V3d: the dialogue hold as retail runs it

Read `README.md` here (§1 "how a dialogue holds an NPC", §2 M12–M13, §4 V3d, §8 Q4/Q7) and
`packets.md` (R1, R2 — **this brief is final only once they are in; where the packet answers a
question below, the packet wins**). Retail walks: `docs/vtmb/npc-ai/conditions-and-states.md`
§ `TASK_RUN_DIALOG` (and R1's new sub-section on `0x102c1400`), `docs/vtmb/game_runtime.md`
§ "Runtime / branching", § "Dialogue close and `DialogPostProcess`", § "Retail conversation chain",
`npc-ai/shape.md` § "The dialogue-release path `0x102c0360`". Runs after V3c. You never build.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcDialogue.h`, `ElysiumNpcDialogue.cpp`
- `Source/ElysiumUE/Private/Substrate/ElysiumEntityWorldDialogue.cpp`, `ElysiumDialogueSession.h`
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcRunTask.cpp` (`RunDialogActivity`, `307-321`)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcStartTask.cpp` (`StartTask19PlayerStartDialog`,
  `586-590`)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcTroikaHelpers2.cpp` (`OnDialogRelease`, `347-`)
- `Source/ElysiumUE/Private/Substrate/ElysiumCombatCharacter.cpp` (`SetDialogPartner`'s body)
- Tests: `ElysiumDialogueCameraTests.cpp`, `ElysiumDialogueTests.cpp`, `ElysiumNpcThinkCadenceTests.cpp`
  (lines 400-403), `ElysiumNpcKernelRunTaskTests.cpp` (`RunDialogAndDisposition`, only if it pins the
  old seam's answer), `ElysiumNpcKernelTroikaHelpersTests.cpp` (`OnDialogRelease`, only if it moves)

## The job

1. **The three inputs install their program** (N8): `StartForced` / `StartRemote` / `StartUnforced`
   (`ElysiumNpcDialogue.cpp:66-110`) keep their guards and `m_bForceDialogStart` in retail's order
   (packet R2), store the integer at `+0x5bac` where retail does, run `FinishTalking` and the think
   reset (slot 614), and **force the schedule** (`0x6d` / `0x6e` per R2) through the NPC's forced
   `SetSchedule` door. They open nothing: no `OnDialogBegin` at the input, no
   `PrepareBodyForDialogue`.
2. **`+use`** (`BeginPlayerUse`, `PlayerUse 0x10167850`): after the eligibility the port already
   has (`WillTalk`, `CanTalk`), clear the NPC's schedule and push `0x6a SCHED_TROIKA_RUN_DIALOG`
   (packet R2 item 3 for the order), then the player's start-dialog (`FUN_10178280`).
3. **The open is the player's start-dialog**, reached from `TASK_START_PLAYER_DIALOG 0xda`
   (`StartTask19PlayerStartDialog`, `ElysiumNpcStartTask.cpp:586-590`, already the task's door) and
   from `+use`: `Begin` (`ElysiumNpcDialogue.cpp:15-64`) loses `PrepareBodyForDialogue`; it is
   `FUN_10178280`'s shape (refusal predicate unless forced, `CDialog::Acquire` = `OpenConversation`,
   `SetDialogPartner` on both sides per R1, `OnDialogBegin` where retail fires it, the tail the world
   runs). `OnDialogBegin` fires from the task, not the input.
4. **`m_hDialogPartner`** (M13): `SetDialogPartner 0x10107050`'s body (the seam left it empty) per R1
   and `npc-ai/social.md:476`. `OpenDialog` (`ElysiumEntityWorldDialogue.cpp:249-294`) loses the
   body-owner token parameter and every `Begin/EndDialogueBodySession` call (`266`, `283`, `1378`);
   the session struct loses `BodyOwner` (`ElysiumDialogueSession.h:322`); the replacement path keeps
   its atomicity by the session itself, not by a claim. `:529`'s "no `m_hDialogPartner` field"
   comment is rewritten. D2 rewrites `HasLiveDialogPartner` to resolve the handle.
5. **`TASK_RUN_DIALOG 0xb9` holds and releases** through `0x102c1400` = `RunDialogActivity`
   (`ElysiumNpcRunTask.cpp:307-321`), ported from packet R1: the four `IsInDialog` terms, the upkeep,
   the disposition answer (slot 611), and on all-clear `OnDialogRelease()` (`0x102c0360`) then −1.
   No fail exit, no facing, no route stop (`conditions-and-states.md:358-367`).
6. **One close, one `OnDialogEnd`** (Q7): `CDialog::Release 0x100e5240`'s port (the world's
   `CloseDialog` / `EndDialogSession`) calls the NPC's `OnDialogRelease()` once, after the flush and
   the clear (`game_runtime.md:1363-1370`); `0x102c0360` fires `m_OnDialogEnd +0x5f5c` and clears the
   partner. `FElysiumNpcDialogue::End` (`112-168`) stops firing `OnDialogEnd` itself: a bare
   `EndDialog` input routes into the world's release, as its own comment says retail's
   `Jack,EndDialog` does; whether `EndDialog` is a retail input at all is V7's question — leave the
   input registered. `TimesTalked`, the holster restore and the think re-arm keep their retail sites
   (R1 says where).
7. **Tests**: delete `Elysium.Arm.DialogueCamera.BodyOwnerLifecycle` (`ElysiumDialogueCameraTests.cpp:358`);
   cut the token / owner assertions in `ElysiumDialogueTests.cpp:1747-1756` and the session lines in
   `ElysiumNpcThinkCadenceTests.cpp:400-403` (the reset-at-dialogue-start site is retail's; the arena
   records pin it). `Elysium.Arm.Session.SaveRefusedInDialogue` stays (Q4).

## Not yours

`ElysiumNpc.{h,cpp}` (D2: it deletes `Begin/EndDialogueBodySession`, `PrepareBodyForDialogue`,
`ThinkInDialog`, `DialogueBodyOwner`, `GetDialogueBodyOwner`), `ElysiumNpcAnim.cpp`, `Public/ElysiumEntity.h`
(D2), the mind and the debug views (D3). The payphone (`ElysiumNpcPayphone.cpp`) unless it calls a
symbol you delete — then report the line.

## Rules

README § "Rules for every agent of V3". The query budget: 10 s warns, 60 s stops; never read a file
over ~200 KB whole. Text through Grep / Read / Glob. No sleep, no polling loop. Do not build, do not
commit.

## Report (≤300 words)

Per change, the retail address; the order of one close from `dialog_choose end` to the NPC's next
selection; every symbol D2 must keep or delete for your files to compile; anything R1/R2 left open
that you stubbed (named, answering "nothing").
