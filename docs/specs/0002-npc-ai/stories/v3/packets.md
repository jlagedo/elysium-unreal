# V3r — reading packets R1 and R2

Reader's record, 2026-10-04, from the `vampire.dll` listing (`vtmb-corpus`) and the installed
schedule corpus. *(read)* = read in the listing this session; *(inferred)* = concluded, not read.
No query passed 10 s.

## R1 — the dialogue hold's upkeep and its release

Written into `docs/vtmb/npc-ai/conditions-and-states.md` § The bump and interrupt keys,
`TASK_RUN_DIALOG` … (the `TASK_RUN_DIALOG` paragraph corrected; new sub-section "`0x102c1400` —
the dialogue upkeep …").

1. *(read)* `0x102c1400`: `!IsInDialog` → `0x102c0360`, answer −1. Else: (a) a live `+0x6554`
   scene whose `+0x498` byte is 0 is `UTIL_Remove`d and `+0x6554 = -1`; (b) `m_bIsTalking` and
   `!0x102c0aa0` → `FinishTalking 0x102c0ca0`; (c) queued line, no fidget `+0x64e0`, not talking →
   `0x102c0520(this, m_szDialogQue, 0, 0)`, queue cleared; (d) answer = `m_Activity +0xfec`; only
   when `m_bSequenceFinished +0x65c`: slot 611 → a sequence; ≥0 → `ResetSequence 0x10260a50`,
   `m_flCycle = 0`, answer `0xf1`; <0 → DevMsg, answer `1`; (e) live partner with non-null
   `+0xa8` → `CDialog::ShowPlayerChoices(dialog, !scene && !talking && queue empty)` every tick.
2. *(read)* The task arms: −1 → `TaskComplete` (run arm also clears cond `0x6f`); else slot 310
   `SetActivity(answer)` + the motor yaw reset.
3. *(read)* NPC `+0xfe8` is set only by `StartTalking 0x102c0270` (from `CDialog::Acquire`), which
   also clears `m_bForceDialogStart`, sets `+0x1590`, bumps `m_nTimesTalked`, runs `FinishTalking`,
   cancels a live cine and fires `m_OnDialogBegin +0x5f44`. Cleared only by `0x102c0360`.
   `FUN_10178280` writes the player's word only.
4. *(read)* `OnDialogEnd` fires once: `0x102c0360` fires only for a live partner and clears it;
   `0x102c1400` calls it only when the partner is gone, so that call fires nothing. Release order:
   flush → window off → `0x102c0360` → player slot 415 `EndPlayerDialog` → `+0x30e8 = 0` → unload;
   the program completes on the next tick on which all four terms are clear.
5. **Unrecovered:** the name of `+0xa8` (inferred: it resolves to the player); the value of
   `.rdata` `0x104454c4`; constructor/restore writes of `+0xfe8` (not searched).

**Correction:** `conditions-and-states.md:358-367` was wrong: slot 611 is not the answer during a
dialogue, and the helper's `0x102c0360` call does not fire `OnDialogEnd`.

## R2 — the inputs, `+use`, `UseInteresting`

Written into `docs/vtmb/game_runtime.md` § Runtime / branching (table and new walked paragraph),
§ Retail conversation chain items 1 and 7, § Dialogue close; `docs/vtmb/npc-ai/lifecycle.md`
entity-inputs item.

1. *(read)* Guards, in this order: player 1 exists; the player's `+0xfe8` is not live;
   `!IsBusyWithDiscipline`; `!(+0x14bc & 0x10000000)`. Unforced also checks `!0x10178170(player)`.
   Then the `+0x5bac` **float** store (Forced and Unforced only), `FinishTalking`, slot 614, the
   `+0x6495` byte (1 / 1 / 0), the debug stamp, then `0x102ae750(id, 0)`.
2. *(read)* **Forced → `0x6d`, Remote → `0x6e` (`PUSH 0x6e`, `0x1029f0c7`), Unforced → `0x6d`.**
   `lifecycle.md:232-234` was wrong (it said all three set the byte and install `0x6d`); fixed.
   `game_runtime.md` was right on `0x6e` but wrong on "integer" and "schedule/activity"; fixed.
   The ids are pinned by the registration table `0x102b9810`.
3. *(read)* The install is `bForce = 0`: `0x102cc1f0` → `0x102ae780` (refuses on state/ideal 7 or
   `!IsAlive`) → `ForceScheduleChange 0x102ae490` (cancels a live cine, clears `PRESERVE_PATH`
   unless nav type 1/3, slot 435 `OnScheduleChange`) → `SetSchedule 0x10280e50`.
4. *(read)* Programs: `0x6d` = fail→self, stop, store pos, tolerance `DIST:DIALOG`, path to player,
   `FORCE_RELAXED_ANIMS`, walk/run (threshold `+0x5bac`), wait, `START_PLAYER_DIALOG`,
   `RUN_DIALOG`; no interrupts. `0x6e` = fail→self, `START_PLAYER_DIALOG`, `RUN_DIALOG`; no
   interrupts. `0x6a` = `RUN_DIALOG`; interrupt `COND_PROVOKED`.
5. *(read)* `PlayerUse` NPC arm: slot 295 `CanTalk(player)` (false → return, no ordinary use) →
   slot 614 → `0x102ae750(npc, 0x6a, 0)` → player slot 414 `FUN_10178280`. **There is no
   `ClearSchedule`.** `game_runtime.md` item 1 was wrong ("clears the NPC schedule", "WillTalk
   +0x49c"); fixed.
6. *(read)* `InputUseInteresting 0x102c2a70` is nine instructions: `+0x63d9` = the variant's byte
   when its type is 5 (bool), else 0. Nothing else. `programs.md` is unchanged.
7. Save refusal: not found in 4 queries. The engine's save gate `CSaveRestore::vfunc15
   0x200957f0` has no dialogue term; its `+0x54` reason code's setter was not traced. Q4 stays.
