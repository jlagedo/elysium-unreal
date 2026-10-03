# Brief S8 — author the arena's cover nodes from the validator's bands (spike)

Read `CLAUDE.md`. You edit ONLY `Source/ElysiumUE/Private/Debug/ElysiumArenaSpec.cpp` (and `.h`
if a field is needed) and, for the summary text only, the node-print lines in
`Source/ElysiumUE/Private/Debug/ElysiumGreenRoomConsole.cpp` if names change. Do not build.
Report ≤200 words with the computed dot products per node.

Evidence from the live `elysium.gr_hints --validate arena_gunman` (retail's `0x10296c40` reasons,
player seat at Unreal (−300, 683) cm, gunman on pad `far_ne` (683, 683) cm):
- corner hints (type 10200, `IsHintCoverValid` bounds good 0.50 / bad 0.73): refused "Enemy inside
  of bad range" with facing 0.99 / 0.85 — the corner nodes face the middle, i.e. straight at the
  seat. Retail wants the ENEMY DIRECTION 43°–60° OFF the hint's facing (you peek round a corner).
- low hints (type 101, `IsHintCoverValidLoose` bounds good 0.87 / bad 1.10): refused "outside of
  good range" (−0.66, faces away) and "Projection < 0.2" (the hint and the NPC on opposite sides
  of the enemy). Retail wants the hint FACING the enemy within ~30°, with the block between.
- the "projection" test: `dot(normalize(hint − enemy), normalize(npc − enemy)) >= 0.2` — the hint
  must lie roughly on the NPC's side of the enemy.
- facing = `dot(hint facing 2-D, normalize(enemy − hint) 2-D)` in SOURCE axes (Y negated from
  Unreal); the hint's facing comes from the NODE's yaw (`0x102d12e0`), which the spec writes
  through `angles` — check the yaw convention the spec/console already use (Unreal yaw written
  as-is or negated) by reading `StandCoverNetwork` and keep it consistent.

## Author (all in Unreal cm; keep the seat and pad as they are)
1. `cover_low_east`: on the block's EAST face (+X side, `BlockHalfWidth + FaceAnchorSetback`, y 0),
   facing the seat (−X and slightly +Y): compute the yaw so facing·dir(seat) ≈ 0.95 (inside
   0.87–1.10). The block is then between the hint and the seat? NO — the low hint must SEE the
   enemy over the low block: retail's low cover faces the threat with the cover in front. Put the
   node on the face NEAREST the seat's side? Decide by the two tests: facing in band AND
   projection ≥ 0.2 with the NPC at far_ne. Compute both for the east and the west face and pick
   the face that passes both; author the other face's node too (mirrored) for a seat on the
   other side. Write the computed numbers in the comment.
2. `cover_corner_ne` / `cover_corner_nw`: at the corners as now, yaw chosen so that
   facing·dir(seat) ≈ 0.62 (mid-band 0.50–0.73) — rotate the "toward the middle" yaw by the
   angle that gives it; write the number.
3. Keep four nodes, group 1, types as now. Keep the `static_assert`. Keep `Elysium.Substrate.ArenaSpec`
   green (adjust its expectations if it pins yaws).
