# Brief R — V3r, the reading packets (one reader, no code, no build)

Read `README.md` here first (§1 and §5). You recover retail bodies the V3 coders will port, and you
write them into `docs/vtmb/`. You write no C++, no Python, no record.

## Your files (only these)

- `docs/vtmb/npc-ai/conditions-and-states.md` — § "The bump and interrupt keys, `TASK_RUN_DIALOG`,
  `TASK_MELEE_KNOCKBACK`" (around line 358): add a walked sub-section for `0x102c1400`.
- `docs/vtmb/game_runtime.md` — § "Runtime / branching" (around line 1296) and § "Retail
  conversation chain" (around line 1490): correct the input table and the entry paragraph.
- `docs/vtmb/npc-ai/lifecycle.md` — line 232-234 (the three inputs' schedule): correct it if R2 shows
  it wrong; nothing else in that file.
- `docs/vtmb/npc-ai/programs.md` § "Interesting places" — only if `0x102c2a70` is not a bare byte write.
- Your report: `docs/specs/0002-npc-ai/stories/v3/packets.md` (new; the one file you create).

## Packet R1 — the dialogue hold's upkeep and its release

1. `0x102c1400` (506 bytes; `lifecycle.md:2095` calls it unrecovered). Walk it arm by arm with
   addresses: the four `IsInDialog 0x102c1170` terms as it reads them (`m_bIsTalking +0x64c0`,
   `m_szDialogQue +0x64ec`, `m_hDialogPartner +0xfe8`, `+0x6554`); the `+0x6554` handle release;
   `FinishTalking 0x102c0ca0`; `0x102c0520`; the slot-611 disposition lookup and its two answers
   (0xf1 or 1); `CDialog::ShowPlayerChoices`; the exact condition under which it calls `0x102c0360`
   and answers −1; what it answers otherwise (`m_Activity +0xfec`?). Say which words it WRITES.
   The port's seam and its guess are at `Source/ElysiumUE/Private/Substrate/ElysiumNpcRunTask.cpp:307-321`;
   the taxi override `0x103b36d0` / `0x103b38a0` needs no walk.
2. `m_hDialogPartner +0xfe8` on the NPC: every writer and clearer. Start from `SetDialogPartner
   0x10107050` (`research where 0x10107050`; `npc-ai/social.md:476`), then its callers: is it written
   on BOTH the player and the NPC by `FUN_10178280` (player slot 414)? Who clears the NPC's: only
   `0x102c0360` (walked, `npc-ai/shape.md:2917`), or also `CDialog::Release 0x100e5240`?
3. `CDialog::Release 0x100e5240` → `0x102c0360` (`game_runtime.md:1363-1370`) and `0x102c1400` →
   `0x102c0360`: can both run for one conversation, and does `0x102c0360` guard against firing
   `m_OnDialogEnd +0x5f5c` twice (its "no live partner → skip" block, `ElysiumNpcTroikaHelpers2.cpp:347`
   ports it)? State the order: Release's flush, the clear, the output, the program's completion.

## Packet R2 — the inputs that open a dialogue, and `UseInteresting`

1. `InputStartPlayerDialog 0x1029ef80`, `InputStartPlayerDialogRemote 0x1029f060`,
   `InputStartPlayerDialogUnforced 0x1029f120`: guards in order (player present, the player's
   `+0xfe8` empty, `IsBusyWithDiscipline`, `m_bfAINPCFlags2 & 0x10000000`, `Unforced`'s
   `0x10178170`), `FinishTalking`, the think reset (slot 614), the `+0x5bac` store, `m_bForceDialogStart
   +0x6495`, and **the schedule each installs and how** (`SetSchedule` forced? through `0x102ae750` /
   `0x102ae780`?). `lifecycle.md:232-234` says all three install `0x6d`; `game_runtime.md:1308-1310`
   says Remote installs `0x6e`. Settle it from the listing and correct the wrong doc.
2. The program texts of `0x6d` and `0x6e` (`cai_basenpctroika/sched_troika_start_player_dialog*.sch`
   in the schedule corpus; `script_dialog_hold.json`'s `about` quotes `0x6d`'s): copy them verbatim
   with their interrupts.
3. `CBasePlayer::PlayerUse 0x10167850`'s NPC arm: the order of the `WillTalk` test, `CanTalk`, the
   schedule clear (`ClearSchedule 0x10280d30`?), `SetSchedule(0x6a)` (forced?), and the slot-414 call.
4. `InputUseInteresting 0x102c2a70`: is it a byte write to `+0x63d9` and nothing else
   (`world/input_useinteresting.json` says so)? If it releases a place or touches a schedule, walk it.
5. Only if it costs less than five queries: does anything in the save path (`CSaveRestore`, the
   game's save command, `CDialog`) refuse a save while a conversation is open? A "not found in N
   queries" answer is a fine answer.

## How to read

- Look every address up first: `uv run elysium research where <addr>`, `research section <addr>`;
  then the `vtmb-corpus` MCP (`vtmb_func`, `vtmb_asm`, `vtmb_callers`, `vtmb_fields`, `vtmb_string`)
  for what the docs do not hold. Prefer `vtmb_asm` slices to whole decompiles.
- The query budget: 10 s warns (log the query to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`), 60 s
  stops; a stopped query is not retried as-is or widened. Never read a file over ~200 KB whole.
- Text through the built-in Grep / Read / Glob tools. Wait on a background command by its completion
  notification, never a sleep or polling loop.
- Every claim carries its address. What you could not settle is written as **Unrecovered:** with
  what you tried.

## Report (≤300 words, plus `packets.md`)

`packets.md`: R1 and R2 as numbered findings, each with addresses and the doc section you wrote it
into. The report: what you settled, what stays unrecovered, the doc lines you corrected, every query
over 10 s.
