# Findings D — the landed stories, audited (2026-09-30)

The audit covers 54 landed stories. From 0019: stories 1–8. From 0018: stories 1–7, 21-1 to 21-7, and 8 (row 14, landed with its box still open). From 0002: the 29 series (9 stories) and the 22 mind stories. There is one row per story in `findings-D-landed.tsv`. The live-check column takes five values: *yes*, *partial* (a key half was never observed), *unobservable*, *none* (tests only), and *n/a* (the story changed no runtime behaviour).

## 1. Live checks that were unobservable, deferred or never done

The tally is 11 yes, 6 partial, 2 unobservable, 27 none and 8 n/a. That leaves 29 of the 46 runtime stories with no live check or an unobservable one, and 6 more that were checked only in part.

- **0002's own witness has never been played.** No landing in any of the three specs records thug_1's sneak-past, stealth_victim's kill or the trance. The 22 mind stories (1–25) and 29c / 29c-1 / 29d name tests only. Their bodies reached the live path only when 0019/8 wave 2 wired the loop.
- **Combat and death have never been observed on a retail map.**
  - 0019/8's three smokes were idle runs ("54 baseline surfaces unreached because the smoke has no combat or death").
  - 0018/8 made three attempts to see a cover claim, and something outside the story stopped each one: `thug_3` sat in `FALL_TO_GROUND` with no body owner; a baton cop never runs the mask-1 search; rangers flipped by `SetRelationship` never enter `Sighted()`.
  - The only combat ever observed is the uncommitted Green Room arena run (`story8/arena-run.md`). It found five interpreter defects (§3c).
- **Restore has never been observed live.** 0019/2 and 0019/6 both note that "a live load has no console verb", so `SaveRoundTrip` is the only evidence. 0019/8 later found that dead NPCs revived after a load.
- **Deferred as "closed on tests":** 0018/7's crosswalk wait and locked-door refusal went to row 36. No live NPC can reach either while every hub pedestrian runs the port's ambient executor.
- **Checked only in part:** 0018/4: the wander pick was forced, and nothing issues task `0x1f` at hub idle. 0018/6: the closed room and reachability were never fixture-tested, because the NavMesh spike skips.
- **Tests only, although live behaviour changed:** 0019/3 (the corpus switchover) and 0019/4 (83 constants, among them melee range 100 and health 10).

## 2. Named modernizations: 42 named, 22 of them change state or event order

`AGENTS.md` says "Nothing that changes event order or state is a modernization." Under that rule these 22 must be re-classed as named divergences, each with an owner.

**The worst three:**

1. **The body arbiter** (0019/8 wave 2, `ElysiumNpcAnim.cpp:402-431`). `PlaySequenceClip` plays the kernel's clip only when the body owner is `None` or `Dialogue`. Under any other owner (`Schedule`, which every running program holds, or `Ambient`, `Sequence`, `ScriptedSchedule`, `Follower`) it skips the play and does not write `ScheduleIdealActivity`. If the row has never played, it returns false, so the cycle never advances and `m_bSequenceFinished` never rises. Any task that waits on a clip can hang. In the arena, `play_cover_outof` never completed.
2. **Reach beyond retail's graph** (0018 § Navigation boundary, stories 3 and 7). The human mesh joins all 14 bridging links and 3 of the hub's 6 zone pairs. It also walks every jump-only pair, 117 of 117 on the hub, that retail's ground walk refuses. The boundary section's own door rule says added reach "changes which encounters can reach the player".
3. **Restart instead of resume** (0019/2 pass C; 0019/5's "restart-not-resume on revisit"). Retail resumes a restored or revisited NPC at its task cursor `+0x5c50`; the port restarts the program. The restart runs slot 435, which releases twelve saved rows.

**The other 19 to re-class:** 0019/8: the sequence bridge's primary-clip pick, which replaces the weighted sequence pick. 0018/5: when the NPC hold starts (after Unreal's follower gives up, not at first contact); a `Success` end counted as arrival; the re-issued leg's `Blocked` end judged at re-issue time (~5 s against retail's 3.0 s). 0018/4: the capped point pick. 0018 boundary: tie order and RNG parity not reproduced; a stale link becoming the door's own timer; a door handled on reach (stale text now, because 0018/7 made the look-ahead `0x102f06e0` live). 0018/7: the `CheckStaleRoute` hull sweep, with `Triangulate` answering "blocked"; `QueryRoute` standing for `BuildLocalRoute`. 0019/3: lateral cover by capsule. 0018/3: the 0.7 slope limit. 0019/5: the non-solid controller; same-frame removal; the director's 0.05 s beat; the ground-ray lift; "Non Troika" child removal; StartHidden not replayed onto a child; a self-removing child returning null.

**Allowed (20):** engine work with the retail contract kept (the route's shape; the two crowd-avoidance items; the flag-2 route; the pedestrian area cost; the 1 cm floor; re-issuing a leg on unpause; the disabled jump records); the crosswalk splice; one boolean per crosswalk pair (holds only if the wait test reads red/green alone); visual-only (the yaw→`Face` handoff; the stamps; DevMsg; both debuggers; the skipped pre-idle flash); peripheral (saves refused mid-beat); removed or moot (the disposition copy; 0002/19's flight service).

## 3. Orphaned hand-offs

**(a) Handed to a closed story or a closed owner:** the `copcar` prop obstacle and three crossed `func_brush` windows (5 → 3/row 13 → 7 → "story 3's contents marking", closed 2026-09-20; the copcar cops still stand at the car); the step-rise / step-height pin (3 → 5 → "story 3's harness report"); the mesh walking jump-only pairs ("story 3's step height"; 21-9 only reports it); `UpdateTargetPos 0x10271b10` (0002/10j says 0018/5 owns it; 0018/5 closed without it); `BeginNavigationJump`'s launch direction (to 0002/26, no row); 0018/6's items for row 11 (the closed-room fixture, the Werewolf fake-hull check, the `TestGroundMove` stand chain, nav-filter arm (f), the path-test budget; 0019/6 names none); RE-BACKLOG 39, 45, 46, 47 (queued, owners closed); 0019/5's `handoff-story-8.md` residue (`Weapon_Switch 0x1032dde0`, `MemberSync 0x10337ca0` still stub tallies; `CAI_Hint::ObjectCaps 0x102d2ee0`; slot-166 dispatch; the CCineAI slot-440 translation of `0x2a`/`0x2e`; `m_saved_troika_flags`; hidden makers (`+0xe4`); controller solidity and velocity; the `0x1033f6d0` / `0x10170090` callers); RE-43's per-row moves.

**(b) Handed to an owner that is not a row:** per-level `curtime` and a `map_load` keeping the previous session's state ("world/session"); `SetAttackExtentsForSequence 0x10090c80` ("animation"); a script reading `.classname` ("scripts"); 0018/8's findings (`thug_3`'s hidden-NPC lifecycle; `Sighted()` for NPCs flipped by `SetRelationship`; an open dialogue blinding perception; `TakeDamage` leaving no last-damage record; the weapon `+0x8c0` words and the `CAN_RANGE_ATTACK1` path; the player's `BodyTarget` stub); 0019/7's findings (13 of the 19 unregistered NPC inputs; the VSound table; the `GTaskArms` check); divergences and seams with no owner (flinch order until slot 141 has a producer; the skill-level seam; restart instead of resume; 0002/15's motor contact / gravity / turn-pose arms and its boss registry); loose ends (the `hw_warrens_4` `iris_clip` door; `MapBakeV2` native-reference coverage; the ~280 `unported.tsv` rows outside both reach lists).

**(c) The arena defects** (`story8/arena-run.md`, uncommitted) are in no RE-BACKLOG row and no tracker row: `TASK_RANGE_ATTACK1` never completes; `play_cover_outof` never completes; schedule `0xef` ignores its interrupts; `face_enemy` takes 13.4 s; a dead body stays standing; `COND 0x29` has no name.

**(d) 0002/25c and 26** were "absorbed" by 0019/8 (tracker row 06, pass C). Both are still `[ ]` in 0002 and on no tracker row.

## 4. Overlaps

| 0002 | 0018 | Shared body |
|---|---|---|
| 19 (landed) | 7 | the link predicate `0x102ff960` and `IsJumpLegal 0x10280880`; 0002/19's landed jump flight is dead by 0018/7's capability gate |
| 11 | 10 (and 7) | the trio `0x102a9f40` / `0x102aa210` / `0x102da600`, the walk `0x102daac0`, crosswalk `0x102` / `0x102a0bc0` |
| 10g, 27 | 11 | `CAI_PatrolPath`, `NextPoint 0x10307b80`, the roll `0x1029f650`, the resolves `0x1029f730` / `0x1029f780` |
| 17 | 14 | `DisconnectFromSquad 0x1026d050`, `GetEnemies 0x10273e10`, `SquadNewEnemy 0x103161a0` |
| 12b | 8, 9 | the tactical search `0x102b7110`, the kick walk `0x102d0910`, the cover search `0x10301720` |
| 10h | 9 | the hunt builders `0x10306700` / `0x10306f60` |
| 10a (landed), 10d | 13 | `CommitBestSound 0x102b4090` and `+0x60dc` |
| 16b, 26 | 17 | `AddClassRelationship 0x10332aa0`, `InputSetRelationship 0x10273790`, `ReportCriminalAct 0x1017f2a0` |
| 15 (landed), 10j | 5 | `CalcNextNormalThink 0x10290b60`; `UpdateTargetPos 0x10271b10` |
| 25c | 7 | the door-block gate `m_hBlockedDoor +0x5d28` |

**0019 work that 0002 still lists as a gap:** every open story's `Gap:` line (23) predates rows 06 and 06b and was never struck; 26 says "no pre-selector exists" but Select19 wired `0x1028a260`; 25c says the null-schedule arm answers `TaskFail(0x05)` but Maintain19 ported `0x102817c0` whole (neither has a reach-cut clause); 29e is `[x]` but its text still says the map smoke has not run; row 32 (12b) says the cover search is wired to a stub (rewired 2026-09-30, uncommitted); 0018/11, 0018/14 and 0018/17 carry stale `Gap:` lines.

## 5. One consolidated sequence (the auditor's proposal)

P0 record and tooling; P1 clock and session (per-level `curtime`, `map_load` reset, a live load verb, resume vs restart, StartHidden / ScriptUnhide, flinch slot 141); P2 kernel (the arbiter and the sequence bridge, clip-waiting and attack-task completion, interrupt masks, facing, `DIST:ACCUM`, the `GTaskArms` check, all 19 NPC inputs, the `Weapon_Switch` / `MemberSync` / `ObjectCaps` stubs); P3 senses (weapon range and slot 365, `Sighted()`, the dialogue gate, the last-damage record, the sound list with the VSound table); P4 navigator and world (the reach decision, `copcar` / `func_brush` contents, `UpdateTargetPos`, RE-39/45/46/47, 0018/6's leftovers, the flyer); P5 programs (rows 26, 47, 44, 46, 40, 42, 12b's kick seams); P6 selectors (rows 15+35+32; rows 16+36 retiring the ambient executor with the crosswalk / door acceptance and the `PRESERVE_PATH` release fix); P7 species and social (rows 20+41, 21+22, 23+43, 24, 49); P8 witness (the tutorial played for the first time; row 50; 21-8/9/10 on demand).

**Dead, to be deleted:** 0002/22 and 24 (pointers); row 10; 0002/19's jump-flight path; 25a's `RequestClearSchedule` seam; 26's knockback arms, `0xae`, `TASK_TEST3/4`; 0018/15's standoff half; 0018/18's `info_node_link` as a record only (RE-48).
