# Lane R1 report: wave-1 residue sweep

(Saved by the orchestrator from the lane's final answer.)

**Decisions 1–6: all done.**
1. `FElysiumScriptedSequence` `Touch` / `Blocked` overrides, their declarations and the DirectorTests assertions removed. Slots 175/178 now fall to the `FElysiumEntity` stub; no port path dispatches a touch to a director.
2. `GetStateName` body and ClosureTests row 406 removed. Link gap until regeneration: `ElysiumNpcBaseSlots.inl:147` still declares the virtual, and `ElysiumNpcBaseSlots.cpp:1491` takes its address.
3. The four think getters and their declarations removed. The Lifecycle ThinkStamps test now checks that slot 614 moves all four `ScheduleHost` stamps. In EntityChain only the think block was cut; the test stays for its slot-65 `SetAngles` lines.
4. `KillTeleportBats` removed: the call (now an address comment), body, declaration and test. `m_hTeleportSwarm` stays because it is a SAVE field.
5. Removed: the Newscaster debug gate in `NPCThink`, `Think19NewscasterDebugConVar`, `FUN_103a0ff0`, `NewscasterNotPlayingText` and the three listing blocks of the Newscaster test.
6. Search timer removed. Beyond the brief: `ElysiumNpcWerewolf2Species.cpp` also used it (4 start calls, 9 returns), converted the same way as `Misc2Species` (1 start, 3 returns). `LifecycleCycles`, `GSearchTimerCycles`, `SearchTimerElapsedCycles` and their test are gone.

**7. Orphan declarations removed (18):** `BaseHelpers.inl` 10; `BaseMisc.inl` 4 plus `FStandoffAimWords`; `BaseSquad.inl` 2 plus `FSquadSlotSpecies`; `Schedule.inl` 1 (`LoadedSchedulesRows`, with `FScheduleLoadFlag`); `Sounds10.inl` 1 (`TroikaSlot506`). The others named were already clean; remaining script hits are pure virtuals. Stale doc comments fixed in `Werewolf.h`, `Sounds.cpp`, `Sounds10.inl`.

**8. Includes and usings:** nothing names a missing file or namespace.

**rg proof:** outside generated files and comments, no deleted symbol is named anywhere except the two generated `GetStateName` lines.

**`targets-R1.tsv`:** 7 rows, all `-`: 0x101a75a0, 0x101a7580, 0x1027e740, 0x103b0560, 0x103a0ff0, 0x103d1ca0, 0x103d1d60.
