# Story 8 — family Werewolf19, walked prose (lane L12)

Spec 0019 story 8, pass I. Each section is one `rule` row of `families-19-29/Werewolf19-READING.md`
over 64 bytes, walked arm by arm off the decompiled C and the listing (`vtmb_code` / `vtmb_asm`).
Pass C folds these into `schedule-kernel.md` / `conditions-and-states.md` / `lifecycle.md`.
Port: `Source/ElysiumUE/Private/Substrate/ElysiumNpcWerewolf19Species.cpp` (species),
`ElysiumNpcWerewolf19.cpp` (Troika line), `ElysiumNpcBaseWerewolf19.cpp` (`CAI_BaseNPC`),
`ElysiumNpcWerewolf.cpp` (`0x103cac20`).

Shared words, as the listings use them (the port keeps older names landed suites assert through):
`+0x66a1` the enemy-unreachable latch (`bWerewolfTaskFailed`); `+0x66a4` its once-per-frame stamp, an
engine frame number (`WerewolfMorphTimerA`); `+0x66a8` the previous pass's `HasCondition(0x59)`
(`WerewolfSnapWordA`); `+0x66a9` `m_bPlayFrustration`; `+0x66ac` the teleport search cursor
(`WerewolfWord66ac`); `+0x66b8` the move search cursor (`WerewolfMoveHintSearchStart`); `+0x66d4` /
`+0x66d8` the searches' frame stamp and curtime stamp (`WerewolfMorphTimerB` / `C`); `+0x66e8` the
hint-gate bit word (`WerewolfHintFlags`); `+0x6708` / `+0x670c` the move / random-move node zones.
The engine frame count (`(*DAT_1070b22c)->vfunc 0x1e0`) is a seam answering `INDEX_NONE`, read as
"never the same frame" — retail's answer at retail's one-call-per-frame rate.

The global hint list is `DAT_10925450` with the `+0x5d8` next link (the world's `HintList`, head =
last authored). `HasPath` (`0x103d0db0`) forwards to the navigator's `0x102fdcc0`, a seam answering
false (no path object). Slot 617 is `CNPC_VWerewolf::EnemyCouldSeeHull` (`0x103da230`).

## `0x103cc450` `CNPC_VWerewolf::UpdateConditionShouldBreakHint` (281 bytes)

1. `ClearCondition(0x7b)` unconditionally (`0x103cc4c2`).
2. `+0x66e8 & 2` clear: `m_pBreakHint` (`+0x66c4`) := NULL (`0x103cc4d2` / `0x103cc4d4`).
3. No break hint: return (`0x103cc4e6`).
4. `IsValidBreakHint` (`0x103d8550`) false: return (`0x103cc4f2`).
5. `HasPath(GetAbsOrigin, GetHintGroundpoint(hint))` false: return (`0x103cc546`).
6. Slot 1 on `DAT_10924a6c` (`ent_trace_conditions`, object `0x10924a68`), result discarded
   (`0x103cc550`); `SetCondition(0x7b)` (`0x103cc557`).

Reads `+0x66e8`, `+0x66c4`. Writes `+0x66c4`, condition `0x7b`.
**Unrecovered:** nothing in the body; the meaning of `+0x66e8` bit 2 ("a break hint is held") is the
zone logic's.

## `0x103d0ec0` `CNPC_VWerewolf::FindBreakHint` (644 bytes)

1. A held `m_pBreakHint` that `IsValidBreakHint` accepts and `HasPath(origin, groundpoint)` reaches:
   return true (`0x103d0f37` / `0x103d0f43` / `0x103d0f97` -> `0x103d0fab`), no stamp.
2. Stamp `+0x66d4` = frame (`0x103d0fba`), `+0x66d8` = curtime (`0x103d0fc8`).
3. Empty hint list: false (`0x103d0fd6`). `m_hClosestPlayer` (`+0x628c`) not resolving: false
   (`0x103d0ff4` / `0x103d100f` / `0x103d1014`).
4. Best distance `FLT_MAX` (`0x103d102d`), origin (`0x103d1035`), reference = the chase point
   `0x103d9c90` (`0x103d1056`).
5. Walk the whole list (`0x103d105f` / `0x103d1121`): `IsValidBreakHint` (`0x103d106f`); groundpoint
   (`0x103d107d`); distance reference->groundpoint (`0x103d10b4`); keep only a strictly smaller one
   (`0x103d10d0`) that `HasPath(origin, groundpoint)` reaches (`0x103d110d`). Distance is tested
   first, path second.
6. `m_pBreakHint` := winner or NULL (`0x103d1127`); answer whether one was found.

The early-false exits (3) leave `m_pBreakHint` untouched. **Unrecovered:** none.

## `0x103d1200` `CNPC_VWerewolf::FindEgressHint` (581 bytes)

1. With a move hint (`+0x66bc`, `0x103d1277`) and an enemy (slot 167, `0x103d1289`): true only if
   `HasPath(origin, groundpoint)` (`0x103d12e1`) AND `HasPath(GetHintEndpoint(hint), enemy origin)`
   (`0x103d133f`). Any failure, or no enemy: `ClearMoveHint` (`0x103d1356`).
2. Walk the list (`0x103d1363` / `0x103d142c`) for type `0x3aa8` (`0x103d1373`): `HasPath(origin,
   groundpoint)` must pass (`0x103d13c5`); then slot 617 at the hint's ENDPOINT with `(0, 0,
   vec3_origin)` (`0x103d1410`): false BREAKS the walk (`0x103d1418`), true runs `SetMoveHint(hint, 0)`
   (`0x103d141f`) and the walk goes on — the last seen hint stays held.
3. The walk always answers false (`0x103d1444`).

**Unrecovered:** none.

## `0x103d2070` `CNPC_VWerewolf::IsImperativeMoveHint(CAI_Hint*)` (1547 bytes)

Four gates over `+0x66e8` in retail order.

- A (`0x103d20f7` bit 0x100 set, `0x103d2101` bit 4 clear): with bit 0x80 (`0x103d210a`) and
  `0x103d1e50` true (`0x103d2115`), type `0x3aaa` answers true (`0x103d2121`) and any other type skips to
  B WITHOUT the next test; otherwise type `0x3aa6` answers true (`0x103d2142`), no path asked.
- B (bit 0x200, `0x103d2158`): `m_iDisabled` (`+0x5e8`) set answers false (`0x103d2166`); else the
  eight `(type, m_iName)` pairs through `0x102d1220` (`0x103d217f`..`0x103d2216`) and, on a match,
  `HasPath(GetOrigin, groundpoint)` true answers true (`0x103d2265`).
- C (bit 0x400, `0x103d227d`): same shape with the five tramdoor pairs (`0x103d22a4`..`0x103d2347`).
- D: bit 4 SET and bit 0x100 CLEAR, else false (`0x103d235b` / `0x103d236b`). `0x3aa7` goes straight
  to the path test (`0x103d237e`). Else with `0x103d1e50` true (`0x103d23aa`): `0x3aa5` with a path is
  true (`0x103d23b5` / `0x103d2404`); then only `0x3aa4` named `jump_to_platform_hint_1`/`_2` passes
  (`0x103d2414`..`0x103d2600`). With it false: only `0x3a9c` named `archway_a_5_squeeze_front` /
  `archway_b_6_squeeze_front` (`0x103d250e`..`0x103d2600`). The last `HasPath` decides (`0x103d264f`).

`0x102d1220` compares the hint's TYPE and its `m_iName` (`+0x26c`) — the pointer-equal fast path, then
`_strnicmp` over `len-1` for a literal ending in `*`, `_stricmp` otherwise. **Correction to the
checklist:** it names `m_strGroup`; the listing reads `+0x26c`. **Unrecovered:** what each `+0x66e8` bit
means in the zone logic.

## `0x103d3c20` `CNPC_VWerewolf::FindTeleportHint` (1791 bytes)

1. `StartSearchTimer` (`0x103d3c94`). Same frame as `+0x66d4`: false (`0x103d3cad`). curtime below
   `+0x66d8 + 0.15` (`_DAT_104aaac4`): false (`0x103d3ce2`). A held teleport hint: true (`0x103d3d01`).
2. Stamp `+0x66d4` / `+0x66d8` (`0x103d3d28` / `0x103d3d36`). Empty list, or no closest player: false.
3. Resume at `+0x66ac`'s successor, else the head (`0x103d3d92` / `0x103d3d9c`). Best NULL, priority 0,
   distance `FLT_MAX`; reference = chase point (`0x103d3db7`), or the break hint's groundpoint with
   `+0x66e8 & 2` and a break hint (`0x103d3e2e` / `0x103d3e38`).
4. Per hint: `IsImperativeTeleportHint` wins at once (`0x103d3e65`). An invalid hint
   (`IsValidTeleportHint`, `0x103d3e75`) appends the STALE target point to the reject list unless the
   last valid candidate left the accept flag set (retail defect, reproduced). A valid hint past 9
   tries (`0x103d3e82`) ends the walk: with no best yet it stores `+0x66ac` = this hint, `DevWarning`s
   "Werewolf giving up after %d teleport searches!" and answers `ReportSearchTimer(0)` (`0x103d42a7`..).
   Otherwise: groundpoint, `GetHintEndEntity` (discarded), target groundpoint; distance target->reference
   (`0x103d3f04`); priority (`0x103d3f1a`); a target already rejected is rejected AGAIN (`0x103d3f6a`);
   `dist < best && bestPriority < priority` (`0x103d3f7f` / `0x103d3f8f`) else reject. Accept flag: an
   already-accepted target (`0x103d3fe1`), or `werewolf_force_teleport_in_time` ("25.0") +
   `m_flTimeTeleportedOut` (`+0x66f0`) < curtime (`0x103d401a`); else `werewolf_teleport_full_path_check`
   ("1") counts a try and asks `HasPath(target, reference)` (`0x103d4052` / `0x103d4087`) — the other
   arm is `0x103d0e70`, both nodes' `+0x94` zones — and a failure rejects; success records the target
   as accepted. Then one more try (`0x103d40f4`) and slot 617 at the groundpoint with
   `werewolf_teleport_ignore_viewcone` ("1") (`0x103d4152`): only FALSE installs the hint as best.
5. Advance on `+0x5d8`, wrapping to the head when `+0x66ac` is set (`0x103d41dc` / `0x103d41e4`); stop
   on returning to `+0x66ac` (`0x103d41f2`).
6. `+0x66ac` := best (`0x103d4203`); `SetTeleportHint(best)` when non-null (`0x103d4213`); answer
   `ReportSearchTimer(best != NULL)` (`0x103d4224`).

**Correction to the packet:** its walk of `0x103d401a` labels the not-taken arm "threshold has not
passed"; `TEST AH,5 / JP` is taken on `>=` or unordered, so the fall-through that sets the flag is
`stamp + allowance < curtime`, as the decompiled C says. **Unrecovered:** the three cvars are not in
the 0019/4 tunables table (names and defaults read from their constructors `0x103c8500`,
`0x103d3bb0`, `0x103d3b20`).

## `0x103da0a0` `CNPC_VWerewolf::IsEnemyUnreachable` (306 bytes)

1. `+0x66a4` equal to the frame: answer `+0x66a1` (`0x103da122`).
2. `+0x66a4` := frame (`0x103da136`).
3. No enemy (slot 167) or slot 530 `IsUnreachable(enemy)`: `+0x66a1` := 1 (`0x103da148` / `0x103da157`
   -> `0x103da159`). Never written 0 here.
4. With the latch set (`0x103da168`): `HasPath(origin, chase point)` true clears it (`0x103da1b5` ->
   `0x103da1b7`).
5. Answer `+0x66a1`.

**Unrecovered:** none.

## `0x103cac20` `WerewolfResetHuntState` (351 bytes)

No branch. `SetEnemy(NULL)`; `m_hClosestPlayer` = -1; `+0x66a4` = 0; bytes `+0x66a1`, `+0x66a8`,
`+0x66a9` = 0; `+0x66d4`, `+0x66d8` = 0; `+0x66dc..e4` = `vec3_origin`; `+0x66ec`, `+0x66f4` = curtime;
`+0x66b8`, `+0x66ac` = 0; `+0x6708`, `+0x670c` = -1; `+0x66e8`, `+0x66f8`, `+0x66fc`, `+0x6700`,
`+0x6704` = 0 (`0x103cac2a`..`0x103cacd5`). Then `+0x66cc += sqrt(dx² + dy²)` over the hull `+0x1568`'s
half-extents (`0x103cad59`) and `+0x66d0 += sqrt(512.0)` (`_DAT_104704c0`, a double, `0x103cad6d`).
Both are `+=` and the body runs from `NPCInit` and `OnRestore`: the floor grows with every load
(retail defect, reproduced). **Unrecovered:** the retail name; `+0x66f8`/`+0x66fc` meaning.

## `0x103d2810` `CNPC_VWerewolf::IsImperativeRandomMoveHint(CAI_Hint*)` (388 bytes)

Type exactly `0x3aa8` (`0x103d288e`), `m_flPlayerDist` (`+0x6264`) above `werewolf_pursuit_distance`
(`0x103d28bf`), slot 617 at the hint's ENDPOINT with `(1, 0, vec3_origin)` answering FALSE
(`0x103d2916`), and `HasPath(GetOrigin, groundpoint)` (`0x103d2964`): true (`0x103d2978`). Anything
else answers `IsImperativeMoveHint(hint)` (`0x103d297e`). **Unrecovered:** none.

## `0x103d2a10` `CNPC_VWerewolf::FindMoveHint` (1643 bytes)

1. Same frame: false (`0x103d2a99`). curtime below `+0x66d8 + 0.5` (`_DAT_104454d0`): false
   (`0x103d2ace`). Held move hint: true (`0x103d2aed`). `ShouldPursueEnemy` false: false (`0x103d2b0f`).
2. `StartSearchTimer`; stamps (`0x103d2b3d` / `0x103d2b4b`); empty list / no closest player: false.
3. Resume at `+0x66b8`'s successor, else head. Best NULL, distance seeded 2500.0 (`0x103d2bc6`);
   origin; reference = chase point, or the break hint's groundpoint under `+0x66e8 & 2`.
4. Per hint: `IsImperativeMoveHint` wins (`0x103d2c8b`); `IsValidMoveHint` gates (`0x103d2c9b`) —
   an invalid hint appends NOTHING. Past 14 tries (`0x103d2ca6`): with no best, `+0x66b8` := this hint
   and `ReportSearchTimer(0)` (`0x103d3023`), else stop with the best. Distance origin->groundpoint
   (`0x103d2d31`); admitted when below the best OR the type is `0x3aa5` (`0x103d2d47` / `0x103d2d4f`); a
   rejected target is rejected again (`0x103d2d9c`); an accepted target WINS with no path test
   (`0x103d2de9` -> `0x103d2ebf`); else a try and `HasPath(target, reference)` (`0x103d2e36`), a try
   and `HasPath(origin, groundpoint)` (`0x103d2e80`) — both pass: accept and win; a loser's target is
   appended to the reject list (`0x103d2ed1`).
5. At the list's end with a cursor: `0x103d0ad0`'s cached nearest node, when present, writes its `+0x94`
   into `+0x6708` (`0x103d2f43`); restart at the head. Stop on returning to `+0x66b8`.
6. `+0x66b8` := winner (`0x103d2f61`); `SetMoveHint(winner, false)` (`0x103d2f72`);
   `ReportSearchTimer(found)` (`0x103d2f83`).

**Unrecovered:** the node graph `0x103d0ad0` reads (seam `CachedNearestNodeZone`, no node).

## `0x10397380` `ShareEnemyWithAlly(CBaseEntity* ally)` (98 bytes)

No enemy (slot 167, `0x10397390`): nothing. Else, on the ALLY: `AddEntityRelationship(enemy, D_HT, 5)`
(`0x1039739e`), slot 596 with the enemy (`0x103973a8`), `SetCondition(0x54 NEW_ENEMY)` (`0x103973bd`);
then only when THIS NPC is in a connected squad (`+0x5bb0 < 1`, `+0x5da4`), `SquadNewEnemy`
(`0x103973d8`). **Unrecovered:** no squad object stands in the port.

## `0x103cc320` `CNPC_VWerewolf::UpdateConditionEnemyUnreachable` (239 bytes)

`IsEnemyUnreachable` false: clear `0x59`, set `0x79` (`0x103cc3b5` / `0x103cc3c9`); true: set `0x59`,
clear `0x79` (`0x103cc3a1` / `0x103cc3aa`). Then with `+0x66a8` clear (`0x103cc3d6`) and `0x59` now set
(`0x103cc3e3`), `CheckAllMoveHints` (`0x103cc3e7`, lane L11's row) finding none sets `+0x66a9`
(`0x103cc3f0`). Finally `+0x66a8` := `HasCondition(0x59)` (`0x103cc400`) — the search runs once per
unreachable episode. **Unrecovered:** none.

## `0x103cf770` `CNPC_VWerewolf::CheckAllRandomMoveHints` (986 bytes)

1. Held move hint: `HasPath(origin, target groundpoint)` true answers true (`0x103cf840`), else
   `ClearMoveHint` (`0x103cf858`).
2. `StartSearchTimer`; `+0x66d4` = frame (`0x103cf874`), `+0x66b8` = NULL (`0x103cf885`), `+0x66d8` =
   curtime (`0x103cf88b`); origin.
3. The WHOLE list from the head: an `IsImperativeRandomMoveHint` hint wins (`ClearMoveHint`,
   `SetMoveHint(hint, 1)`, `ReportSearchTimer(1)`, `0x103cfa9c`..). An `IsValidRandomMoveHint` hint whose
   target point is not yet visited (`0x103cf957`), whose OWN origin is within 1500.0 (`_DAT_10462b70`,
   `0x103cf9a9`) and reachable (`HasPath(origin, groundpoint)`, `0x103cf9ef`) wins the same way.
4. Every non-winner appends the target point — for an invalid hint, the stale one (retail defect,
   reproduced). Off the end: `ReportSearchTimer(0)` (`0x103cfa48`).

**Unrecovered:** retail's stale point before any valid hint is an uninitialised stack read; the port
seeds `vec3_origin` (named divergence).

## `0x103d14f0` `CNPC_VWerewolf::FindRandomMoveHint` (1554 bytes)

As `FindMoveHint` with: retry 0.25 (`_DAT_1044bef8`, `0x103d15ae`); no pursue gate; budget 5 valid
candidates (`0x103d176f`); best distance seeded `FLT_MAX` and measured target->reference
(`0x103d17f4`); an accepted target wins without a path (`0x103d1936`); otherwise one try and
`HasPath(origin, groundpoint)` (`0x103d18f7`); an invalid hint appends the stale point unless the last
candidate won (`0x103d194f`); at the list end with a cursor, `+0x670c` takes the cached node's `+0x94`
(`0x103d19b1`), `DevMsg("FindRandomMoveHint() looped through the entire list")` (`0x103d19bc`) and the
walk restarts; the winner is installed with `SetMoveHint(winner, 1)` (`0x103d19ef`).
**Unrecovered:** as `FindMoveHint`.

## `0x10271d10` `CAI_BaseNPC::CheckTarget(CBaseEntity*)` (418 bytes)

Clear `0x4b` then `0x49` (`0x10271d6e` / `0x10271d77`); slot 201 `FVisible(target, 0x2804091, NULL, 0)`
(`0x10271d8e`); set `0x4b` on a clear line, `0x49` otherwise (`0x10271db0`); `UpdateTargetPos`
(`0x10271db7`) unconditionally. No debounce. The VProf scope is profiler bookkeeping.
**Unrecovered:** `UpdateTargetPos` (`0x10271b10`) needs the navigator goal object (type `+0x18`,
`GetGoalType`, goal target, goal flags); it stands as a seam taking the "no target goal" arm.

## `0x103cc5c0` `CNPC_VWerewolf::UpdateConditionCanSpecialMove` (568 bytes)

1. `m_pSchedule` equal to `0x102cc1f0`'s program for `0x15f`, `0x15b`, `0x15a` or `0x160` (a null
   schedule stops the compares): do nothing (`0x103cc636`..`0x103cc69e`).
2. `m_fEffects & 0x40`: do nothing (`0x103cc6af`).
3. A `0x3aa5` move hint `0x103d1e50` refuses is cleared (`0x103cc6bd`..`0x103cc6d8`).
4. A held move hint: `IsImperativeMoveHint` sets `0x78` and returns (`0x103cc706`); else
   `HasPath(target groundpoint, chase point)` sets `0x78` and returns (`0x103cc783`); else
   `ClearMoveHint` (`0x103cc797`).
5. `0x59` absent: `ClearMoveHint`, return (`0x103cc7e6`). A hint still held: return. Else
   `ShouldPursueEnemy` ? `FindMoveHint` : `FindRandomMoveHint` (`0x103cc7c0` / `0x103cc7d2`).

`0x78` is only ever SET here. **Unrecovered:** the corpus names of the four programs.

## Retail defects reproduced

- `0x103cac20`: the teleport floor `+0x66cc`/`+0x66d0` accumulates on every `NPCInit`/`OnRestore`.
- `0x103d3c20`, `0x103d14f0`, `0x103cf770`: an invalid hint appends a stale point to the reject list.
- `0x103d3c20`, `0x103d2a10`, `0x103d14f0`: a rejected point is appended again on each revisit.
- `0x103d2a10`: an accepted point wins without re-testing its paths.
