# Brief C1 — V4c: `Weapon_FrameUpdate`, the NPC attack producers, slot 247 (coder; no build) — PROVISIONAL until R2

Read `README.md` here (§1 "The events", "The attack extents"; §2 M3, M10, M11; §8 Q4, Q6, Q7),
`packets.md` § R2 items 2, 3, 5, the judge's ruling on Q4 if any, `docs/vtmb/combat-and-damage.md`
:1027-1046. After V4b's commit (or V4a's, if V4c runs first). **Items marked TO BE FILLED come from
R2**; if R2 left them unrecovered, the lane does only what is recovered and says so. Re-locate by Grep.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseMotor.cpp` (`PostRun`'s weapon line,
  `++PostRunWeaponUpdates` ~:597, only)
- `Source/ElysiumUE/Private/Substrate/ElysiumWeaponClasses.{h,cpp}` (the `ContactEventCycle`
  estimate ~`.h:303`, `.cpp:1263-1317, 2166`; `OperatorHandleAnimEvent` ~:1349; the swing contact)
- `Source/ElysiumUE/Private/Substrate/ElysiumEntityWorldInteraction.cpp` (`AdvanceMeleeSwings`
  ~:299-326)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcStartTask.cpp` (the attack arms R2 names; the swing
  start ~:557, ~:852-862)
- `Source/ElysiumUE/Private/Substrate/ElysiumAnimatingSlots.cpp` (slot 247 ~:228-232 only)
- `Source/ElysiumUE/Private/Tests/ElysiumWeaponTests.cpp`

## The job

1. **`Weapon_FrameUpdate`** in `PostRun`, after slot 258, with the interval, as R2 item 2 walks it
   (**TO BE FILLED**); the counter goes.
2. **The NPC shot only from its event.** The 3031 event (dispatched by V4a's slot 258 →
   `Operator_HandleAnimEvent`) is the NPC's commit. Remove the `ContactEventCycle` estimate on the
   NPC path unless R2 item 3a finds a retail fallback (**TO BE FILLED**); the player's path is not
   yours. Log once per (model, sequence) an NPC attack clip with no fire event (README §8 Q6).
3. **The NPC melee contact** where R2 item 3b places it (**TO BE FILLED**): if retail sweeps from
   `Weapon_FrameUpdate` inside the NPC's think, the world interaction tick stops sweeping NPC swings
   and `Weapon_FrameUpdate` does; the swing start follows R2's arm.
4. **Slot 247 `0x10090c80`**: if R2 item 5 shows no NPC class sets `Flags2 & 4`, replace the
   counting stub with the body's gate (`Flags2 & 4` false → return), citing the writers R2 found, and
   leave the bbox half a named seam "the bake drops the sequence bbox (`clip_data.py:13-25`)". If an
   NPC class sets it, follow the judge's ruling on Q4.
5. **Tests** (`ElysiumWeaponTests.cpp`): an NPC ranged clip with a 3031 row commits once at the
   event; one without commits nothing; the `PostRun` order with `Weapon_FrameUpdate` last (if A1's
   `.PostRunOrder` does not already cover it).

## Not yours

The dispatcher (V4a), the attack conditions and `TASK_WAIT_ATTACK_TIME1` (V5, N2), the coordinator
(V11, N3), the weighted pick and death (C2).

## Rules

README § "Rules for every agent of V4": no build, no editor, no suite; only your files; cross-lane
lines in the report. The query budget (10 s warns, 60 s stops). Text through Grep / Read / Glob. Do
not commit. Report ≤300 words.
