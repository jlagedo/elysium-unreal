# Brief S4 — scenario geometry inside vision range; readout additions (spike)

Read `CLAUDE.md`. You edit ONLY `Source/ElysiumUE/Private/Debug/ElysiumGreenRoomConsole.cpp`,
`ElysiumArenaSpec.{h,cpp}` (the seat constants) and `Source/ElysiumUE/Private/Debug/ElysiumEntityDebugSubsystem.cpp`
(`elysium.npc_brief` only). Do not build. Report ≤200 words.

Finding from the live run and the code trace: `gr_scenario cover` seats the gunman on pad
`north` (Unreal X=+975 cm) and the player at the start (X=−975 cm): 1951 cm apart with the
COVER BLOCK on the line between them. `npc_perception 3` gives a vision distance of ~1118 cm
(`ElysiumNpcSenses.cpp:~202`, 440 units), so the range test at `TickSight` (`:~585`) fails
before any trace, and the block would occlude anyway. Fix the geometry, not the NPC:

1. Gunman on pad `far_ne` (P·0.7, P·0.7 ≈ (683, 683) cm), facing the player seat.
2. Player seat for `cover`: (−300, 683) cm, yaw facing +X (toward the gunman): ~983 cm apart,
   inside vision, and the line y=683 clears the block (block half width `BlockHalfWidth` ≪ 683 —
   assert that in code with a comment). Keep the yaw convention the file already uses.
3. `gr_los on`: the player on the far side of the block from the gunman: the point on the line
   gunman→block-centre, `BlockHalfWidth + 150 cm` past the centre, i.e. (−(B+150)·0.707,
   −(B+150)·0.707) — behind the block as seen from (683, 683). `gr_los off` back to the seat.
4. `elysium.npc_brief`: on the `player:` line add `vision=<VisionDistanceCm> in_range=<0|1>`
   (the same words `TickSight` compares; grep `bPlayerInOuterBand` / the range test and print the
   distance the NPC actually uses) and on the `enemy:` line add `state_flags=<hex>` if the
   state-flag word is reachable (the sense gate reads bit 1 of it, `ElysiumNpcBaseConditions2.cpp:~199`).
5. `elysium.gr_hints`: also print `rating=<hint_rating>`.
Print the new seat and pad in `gr_scenario`'s summary line.
