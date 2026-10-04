# Packets S11 — V4b's three open items, settled or bounded (2026-10-04)

Settles the three items the V4b wave (commit `1442fdc2`) left for the judge: `patrol_monk_loop`'s
red at `pod_1`, `ranged_open_fire`'s first wait, and `FollowerArrivalFloorCm`. Read-only on source and
`Arena/`; the only file written is this one. No build, no bake, no commit.

Marks: **(M)** measured in this sitting (an arena run on the existing binary, or a trace / log read);
**(L)** read off the listing / decompile in this sitting; **(D)** cited from an existing `docs/vtmb`
section or brief; **(P)** the port's source, read; **(I)** inferred.

Evidence runs:

- `uv run elysium arena patrol_monk_loop` with `UE-CmdLineArgs=-LogCmds="LogElysiumNpcEnt Verbose,
  LogPathFollowing Verbose, LogCrowdFollowing Verbose, LogNavigation Verbose"` (the engine inserts the
  variable into the command line; no file changed). Report
  `$ELYSIUM_WORK_ROOT/reports/arena/20261004T183927.648168Z/`, log
  `$ELYSIUM_WORK_ROOT/logs/20261004T183927.466685Z-arena.log` (the unfiltered copy was
  `Saved/Logs/ElysiumUE.log`, which the next boot rotates). 38.6 s wall.
- The V4b suite's own report `reports/arena/20261004T182725.564156Z/` (same binary): the traces of
  `patrol_sentry2_pingpong` and `ranged_open_fire`.
- Not run: `uv run elysium verify nav` (an editor boot; the two runs above answer the navmesh
  question directly, see 1.2).

## 1. `patrol_monk_loop` — the red at `pod_1`

### 1.1 Verdict

**The record's player seat is the obstruction. The navmesh, the path, the velocity script's clamp and
the arrival test are all clear.** The record seats the player at `(-18760, -9481.82)`, which is
**66.6 cm (26 units) from `pod_1` `(-18820.7, -9509.3)`** — closer than the two hulls' radii
(NPC `HUMAN_HULL` 13 units + player 16 units = 73.7 cm). The monk cannot stand on the node while the
player stands there. The `known_red` text's "independent of the player's seat (moved 5.5 m away:
identical)" is contradicted by the suite's own traces (1.2 b) and has no run on disk behind it (the
V4b scratch records `$ELYSIUM_WORK_ROOT/tmp-v4b/tmpv4b_monk_{a..d}.json` all keep the same seat).

Owner: **the record** (`Arena/scenarios/world/patrol_monk_loop.json`), fixed now by a lane, no build.
Not a nav bake item, not V13's.

### 1.2 Evidence

a. **The path to `pod_1` is found, whole, and the node projects (M).** Verbose `MoveTo` line for the
   fifth leg: `dest=X=-18820.714 Y=-9509.277 Z=17897.170 tolerance=0.16 cm (follower radius 1.00)
   … partial-ok=0 snap=(9.3 cm 2-D, dz 2.6) path=found` — not `(partial)`. The other four pods snap
   `0.0 cm`. So `pod_1` sits 9.3 cm outside the eroded mesh edge (the podium), the follower's goal is
   the projection, and that is all the bake contributes.

b. **The same binary, the same map, the same node, the player elsewhere: the monk arrives (M).**
   `patrol_sentry2_pingpong` (player seated at `(-18343.88, -7632.56, 17440)`, another floor) carries
   the monk's own logic_auto patrol in its trace:

   | t (s) | monk event |
   |---|---|
   | 26.033 | `move goal -18821 -9509 17897` |
   | 32.150 | `move arrived` |
   | 35.200 | `task_do_patrol_interest_activity` starts, `OnInterestingPlaceArrived` |
   | 46.217 | task done (11.0 s, inside `RandomFloat(10, 15)`), `OnInterestingPlaceLeft`, `goal -19152 -9307` |

   The whole loop runs twice in that trace with no `fail 12`. That is every expectation of
   `patrol_monk_loop` from `goal_1` on, met — on a run where nobody stands on the podium.

c. **The geometry of the failing run matches a capsule contact with the player (M + I).** The leg
   `pod_2 → pod_1` runs along `(-0.436, -0.900)`. The player's seat is 51.2 cm short of `pod_1` along
   that line and 42.6 cm to its side. Two circles of 33.0 cm and 40.6 cm first touch when the monk is
   `51.2 + sqrt(73.7² − 42.6²) = 111 cm` from the node. The integrator measured "105 cm and 34 cm/s
   at +4.3 s, 87 cm at +5.5 s"; this sitting's log has the first failure at **80.2 cm** left and each
   retry closer (71.3, 69.8, 66.7, 65.2, 61.4, 60.0, 56.1 cm): a body sliding round a circle whose
   centre is 66.6 cm from its goal.

d. **The four hypotheses the brief named, each ruled out:**
   - *Node off the navmesh / path partial*: (a). 9.3 cm of snap, a whole path.
   - *The velocity script's last-waypoint clamp stops it early* (`ScriptedMoveSpeedCm`,
     `ElysiumNpcBody.cpp`): the same clamp lands the same body at the same node in (b); legs 1–4 end
     `0.6–1.0 cm` from their goals in this run.
   - *The arrival test wrong for a goal off the nav surface*: in (b) the follower ends `Success` at
     the projection and `NavSampleStep` (`ElysiumNpcBaseMotor.cpp`) counts it (`bSucceeded`), the
     named modernization already in the source. See item 3 for what that means for the floor.
   - *A nav mark / door cut at the podium (V13)*: nothing in the log; the route is found and walked
     in (b).

### 1.3 What retail does, and whether the port's failure is retail's

Retail's patrol move is `TASK_SET_TOLERANCE_DISTANCE 20` then `TASK_GET_PATH_TO_PATROL_POINT`
(`0x102aa640`: goal type 4, tolerance `-1.0` = keep the path's own) (P, D). The goal tolerance
(`path+0x28`, 20 units here) is **not** the arrival radius: arrival is slot 16 `0x102ef510`, 0.0625
units (item 3). The tolerance is read only by the blocked-step arms: S1 `0x102eefb0` (an obstruction
inside the tolerance of the goal) and `OnMoveBlocked 0x102ef760` (blocked with `dist < path+0x28 +
0.1` → `OnNavComplete`; else the failure tail, `OnNavFailed(0xc)`) (D, `navigation-jump-links.md`
§ "The arrival test and waypoint advance" and the port's cited arms). So in retail a hull the player
blocks more than 20 units from its patrol node also fails `0xc`, runs `SCHED_FAIL` and retries —
which is what the trace shows (`fail 12` at 24.333, `FAIL (0x43)`, re-install at 25.433, every
1.2 s). The behaviour is plausibly retail's for this seat; the seat is not a retail situation the
record meant to state.

A path_corner / patrol node the hull cannot stand on exactly has no special arm in retail: the body
either reaches 0.0625 units or is blocked, and blocked inside `path+0x28 + 0.1` completes.

### 1.4 One sub-item not settled

**Which arm raised the `0xc` at 80.2 cm is not discriminated by the log (I).** The line is
`Move: blocked, failed 0x0c` (the failure tail of `NavigatorMoveStep`), and the follower's own end
for that request is logged only six frames later (`Aborted[UserAbort MovementStop ForcedScript]`, the
`SCHED_FAIL` stop). By the source, a negative result with the request still alive needs
`NavObstructionPreSink` to answer `BlockedEntity`, which needs `remaining < goal tolerance`; with
20 units (50.8 cm) in force, 80.2 cm does not satisfy it. Either the tolerance in force at that
think was not 20 units, or a path this reading missed raised it. It does not change 1.1 (the run
without the player arrives), but it is an unknown about the blocked-step arms. To settle: one
Verbose line at the failure tail naming `Step.Blocker`, `Step.RemainingUnits`,
`Navigator.GetGoalTolerance()` and the arm, then this record once more **before** its seat is moved.
Owner: whoever takes the record fix; cost one build and one 40 s run.

### 1.5 The fix

1. `Arena/scenarios/world/patrol_monk_loop.json`: move `script[0].at` so the seat is more than
   ~120 cm (73.7 cm of hulls plus margin) from every leg of the pentagon and from each pod, still
   inside the hall (the 2048-unit wake of `0x1028d820` and the PVS need only that). The pods (cm):
   `pod_5 (-19152, -9307)`, `pod_4 (-19139, -8504)`, `pod_3 (-18691, -8424)`, `pod_2 (-18557, -8966)`,
   `pod_1 (-18821, -9509)`; their centroid `(-18872, -8942)` is over 250 cm from every leg. **The
   floor at that point is not checked here** — the lane validates the seat with one run (the
   `player_teleport` trace line and the first `SEE_PLAYER`).
2. Rewrite `known_red` (or remove it once green) and the `about`'s "set down 1.6 m from the monk":
   the red was the seat. Strike "independent of the player's seat".
3. Expect the timings to stay V4b's for legs 1–4 (2.6 / 6.5 / 3.9 / 4.6 s); leg 5 took 6.1 s in (b)
   under a stretched think law, so `at_1`'s `within: 10.0` has room.

No judge item for the bake. The 9.3 cm snap at `pod_1` stays as a fact for item 3.

## 2. `ranged_open_fire` — the first wait

### 2.1 What the trace shows (M)

`reports/arena/20261004T182725.564156Z/arena/ranged_open_fire.trace.tsv`, the shooter:

| shot event 3031 | `task_wait_attack_time1` starts | done | held |
|---|---|---|---|
| 0.500 | 1.000 | 2.300 | 1.3 s |
| 2.300 | 2.800 | 2.800 | 0 |
| 2.900 | 5.500 (after `task_step_back`) | 5.500 | 0 |
| 5.500 | 6.000 | 6.000 | 0 |

The two pre-V4b traces on disk (`…172528…`, `…173659…`) have the first wait `1.000 → 1.000`.

### 2.2 Cause (P + L, the arithmetic M)

`StartTask19WeaponNextAttackTime` (`ElysiumNpcStartTask.cpp`) returns `Stamp + Delay`:

- `Stamp` = `Weapon->NextPrimaryAttackTime`, retail `0x10252450` = `FLD [weapon + i*4 + 0x730]` (L).
  **Nothing writes it on an NPC's event shot**, so it is 0 for the whole run (P: the one advancing
  writer, `ElysiumWeaponClasses.cpp:1042`, is the staged-transaction path the event shot does not
  take).
- `Delay` = `ScaleWeaponBurstPause(RandomFloat(NPC_Attack_Rate_Min, _Max), Attack_Rate,
  NPC_Attack_Rate_Base_Range, distance)` = `sqrt(dist / 120) × (v − 0.8)`, `v ∈ [1.0, 2.5]`
  (`0x102c5730` → `0x102c5570`). At the record's ~387 units: `1.796 × [0.2, 1.7]` = **0.36 … 3.05 s**.

So the deadline is an **absolute time between 0.36 and 3.05 s**, whatever the clock reads. A wait
that starts at 1.0 s holds when the draw lands above 1.0 (about three draws in four); the trace's
2.300 is a draw of `v ≈ 2.05`. A wait that starts at 2.8 s holds only for a draw above 2.8 (about one
in eleven), and one that starts past 3.05 s never holds. **The first wait "holding" is the unwritten
stamp read against a small clock, not a fix.** V4b changed no line of the wait
(`git show 1442fdc2` touches neither `StartTask19WeaponNextAttackTime` nor `ScaleWeaponBurstPause`
nor `ShootTargetDelta`); what moved between the two traces is the draw — the `NpcSchedule` stream at
seed 1 is consumed differently since the gather asks slot 562 / slot 197 every pass (I: the draw
itself was not logged).

### 2.3 What O3 must write, and where

Retail's writer is `CWeaponRanged::Shot 0x102387b0`, reached from the event
(`0x10238160` → `0x10238320` → `ModeDispatch(1) 0x102383b0` → slot 373). Listing `0x1023891b …
0x1023895d` (L), for an owner with an NPC pointer (`owner+0x94`):

```
bulletSetsToFire (+0x918) = 0
rate  = this->slot 332()                    // 0x10254410: mode record +0x260 Attack_Rate, through 0x1033d940
floor = curtime − frametime                 // [0x1070b228]+0xc − +0x10
if (weapon[+0x730 + slot*4] <= floor)  weapon[+0x730 + slot*4] = floor      // slot = DAT_1088aee4, 0 for an NPC
while (weapon[+0x730 + slot*4] <= curtime) { weapon[+0x730 + slot*4] += rate; ++bulletSetsToFire; }
```

(`m_iAtkMode +0x86c != 0` re-reads slot 332 each step.) This is `brief-O3-weapon-event-shot.md`
item 1.6 and its "`+0x730[slot]` advanced to `next`" (D) — the brief already states it; this packet
confirms the listing and fixes where it lands in the port:

- **In `FElysiumWeapon::ShotFromAnimEvent`** (O3's new body, `ElysiumWeaponClasses.cpp`):
  `NextPrimaryAttackTime = max(NextPrimaryAttackTime, Now − FrameSeconds)`, then
  `while (NextPrimaryAttackTime <= Now) { NextPrimaryAttackTime += Rate; ++Sets; }`, with `Rate` the
  primary mode's `AttackRate` through the `0x1033d940` seam (Presence doubles it). **Written on every
  commit event that reaches `Shot`, including one that fires zero sets**, and never through
  `FMath::Max(…, Deadline)` (line 1042's form), which cannot express the count.
- `NextSecondaryAttackTime` (`+0x734`) is **not** written by an NPC shot (the global is 0);
  `TASK_WAIT_ATTACK_TIME2` reads a word only the non-shot mode types advance (brief item 1).
- The reader needs no change: `Stamp + Delay` is retail's `0x102a33a2` / `0x102a33b0`.

With that, a shot at `T` leaves the stamp at `T − frametime + 0.8` and every wait's deadline at
`T + 0.78 + [0.36, 3.05]`: **each wait ends between 1.14 s and 3.84 s after its shot**, never in the
think it starts (the wait starts 0.5 s after the shot, when `task_range_attack1` completes).
`TASK_RANGE_ATTACK1`'s burst arm (`ElysiumNpcRunTask.cpp:638`, `NextAttack <= Now`) reads the same
word and comes right with it.

### 2.4 The record's expectations against retail

- `never taskdone task_wait_attack_time1 until 1.05`: true of retail but weak — retail's earliest is
  `0.5 + 1.14 = 1.64 s`. It passes today by the accident of 2.2. **Keep `known_red` until O3 lands**;
  the commit message's "unexpected-pass, kept" is the right call.
- The record pins only the first wait. After O3 it should pin a later one: the harness has no
  relative window (`never` takes absolute `from` / `until`, or `after` a label with no closing
  label — the gap the monk record's `notes` already names). **Judge item**: a `never` window closed
  by `within` seconds after its `after` label (reader + runner, `ElysiumArenaScenario.cpp` /
  `ElysiumArenaScenarioRunner.cpp`; one lane, one build). With it:
  `{ kind: taskdone, match: ^task_wait_attack_time1$, after: <each wait's task label>, within: 0.5 }`.
  Without it, O3 can still raise the absolute bound to `until: 1.6`.
- Everything else in the record (the 3031 inside `task_range_attack1`, no damage before 0.3 s, no
  `BEHIND_ENEMY`, no cover) is retail's as stated in its `about` (D).

## 3. `FollowerArrivalFloorCm` 1.0 cm against retail's 0.0625 units

### 3.1 Retail's arrival test, exactly (L, `0x102ef510`; agrees with D)

Navigator slot 16, called by `MoveNormal` (`0x102efb2f`) before any step is built:

- Distance from the owner's slot 220 (`+0x370`, `GetAbsOrigin`) to the **head waypoint's** position
  (`path+0x24`): `nav+0x18 == 0` (ground) → **2-D** `sqrt(dx² + dy²)`; any other nav type → 3-D.
- Tolerance: the double `0x10451f78` = **0.0625 units**; `0x10449260` = 0.25 when ConVar
  `npc_vphysics` (`DAT_106bbaa4`) is set (shipped 0). **One constant for every goal type and every
  waypoint.** Not `path+0x28` (the goal tolerance), not `path+0x40` (the waypoint tolerance), not a
  hull. Not reached iff `tol < dist`.
- A second refusal (`DAT_10934070`, next waypoint of another type, `dist >= 0.001`) is dead: the
  byte has no writer (D).
- Reached and the goal waypoint (`0x102ee660`) → `OnNavComplete`, `*result = 0`; reached and not the
  goal → `AdvancePath 0x102f0400`, `*result = 1`.

The body lands inside 0.0625 units because `MoveGroundExecute 0x10264680` cuts its step to the
remaining distance (`0x10264916`).

### 3.2 Where the port uses the floor (P)

One place: `AElysiumNpcBody::ResolveFollowerRequest` (`ElysiumNpcBody.cpp`),
`AcceptanceRadiusCm = max(FollowerArrivalFloorCm, ExactToleranceCm)`, handed to the engine follower
by `MoveTo` (`SetAcceptanceRadius`, agent and goal radii excluded). Every kernel leg asks for
0.0625 units (0.159 cm), so every leg's follower radius is the floor, 1.0 cm (the Verbose line:
`tolerance=0.16 cm (follower radius 1.00)`). One test pins it
(`Tests/ElysiumNpcBodyMoveFactsTests.cpp:115`).

The floor decides nothing in the kernel directly. Arrival is read in two places, both of which carry
retail's test **or** the follower's `Success`:

- `FElysiumNpcBase::NavSampleStep` (`ElysiumNpcBaseMotor.cpp`): `DistUnits <= 0.0625 || bSucceeded`.
- `AElysiumNpcBody::Sample`: `Horizontal <= RequestedAcceptanceCm` (0.159 cm) or the follower's
  `Success` on a non-partial path.

Measured this sitting (M): the monk's four clean legs end `Success` with **1.0, 0.6, 1.0, 0.7 cm**
left; sentry2's 0.8 and 0.6. So today the body stops up to 1.0 cm (0.39 units) from the waypoint,
by the follower's radius, one tick before the clamped step would have put it on the point.

### 3.3 Can the port follow retail under K1

**For the radius: yes, in principle — unmeasured.** Since V4b the body commands the clamped step at
the last waypoint (`ScriptedMoveSpeedCm`: `MaxDist / interval`), and Detour's slowdown at the goal
is off, so nothing creeps; the only thing ending the leg at 1.0 cm is the radius handed to the
follower. The fix is the constant:

- `Source/ElysiumUE/Private/Visual/ElysiumNpcBody.h`: `FollowerArrivalFloorCm = 0.0625f *
  ElysiumMove::U` (0.15875 cm) — i.e. `ResolveFollowerRequest` hands the request's own tolerance —
  and its comment; `Tests/ElysiumNpcBodyMoveFactsTests.cpp:115` follows.
- Gate, because it is not measured: `input_clearpatrolpath`, `patrol_sentry2_pingpong`,
  `places_pedestrian_visit`, `script_walk_to_mark` stay green with arrival times within one think of
  V4b's, and the Verbose `move request finished … cm left` line reads `<= 0.16` on straight legs.
  If the crowd's commanded speed lags the body's by a tick (the crowd agent's max speed is read from
  the movement component on the crowd's own tick), the body overshoots instead; the crowd follower
  then ends `Success` on its moved-too-far test, which the port already counts as arrival — no hang,
  but a residual above 0.0625 units. That outcome is the measurement's to show.

**What cannot follow retail under K1, whatever the radius — for the judge to name:**

1. **The goal is the navmesh projection, not the node.** `MoveTo` projects the destination and paths
   to the projection; `pod_1` snaps 9.3 cm (1.2 a). `RemainingDistance2DCm` is measured to the exact
   node, so on a snapped goal retail's test can never pass and the follower's `Success` *is* the
   arrival. Retail walks a straight local move to the node itself. Removing this needs the last leg
   walked off-mesh by the motor — the motor back in the kernel, i.e. not K1.
2. **The overshoot `Success`** (`UCrowdFollowingComponent`'s moved-too-far test) as above.

So "follower `Success` counts as arrival" stays a named modernization in both readers; the floor
itself can go.

### 3.4 What the bytecode can observe of the difference

Position: at most 1.0 cm (0.39 units) at a waypoint — no schedule, condition or script reads a
position that finely. Time: arrival at most one body tick early. Order: none. The ruling is between
two unobservable variants.

## 4. For the judge

| # | Item | Options | Cost |
|---|---|---|---|
| 1 | `patrol_monk_loop` | Not a judge item: the record's seat. A lane moves the seat and rewrites `known_red` / `about`. | no build; one 40 s run per candidate seat |
| 1b | Which arm raised `0xc` at 80.2 cm with 50.8 cm of tolerance (1.4) | one Verbose line at `NavigatorMoveStep`'s failure tail, run the record before its seat moves | one build, one run |
| 2 | `ranged_open_fire` | stays O3's; O3 writes `+0x730` in `ShotFromAnimEvent` as 2.3 | inside O3 |
| 2b | A `never` window closed relative to its label (harness) | add it, then pin every wait; or raise the absolute bound to 1.6 s | one lane, one build |
| 3 | `FollowerArrivalFloorCm` | (a) swap to 0.0625 units and measure (3.3's gate); (b) keep 1.0 cm, named | (a) one constant, one test, one build, four records; (b) nothing |
| 3b | Follower `Success` as arrival on a snapped goal / an overshoot | named modernization under K1; cannot be removed without the motor in the kernel | a name |

## 5. Unrecovered / not done

- The arm of 1.4.
- The floor under the proposed seat (1.5); `verify nav` was not run.
- The draw that moved the first wait (2.2) was not logged; the stream shift is inferred from the
  unchanged wait code and the two traces.
- 3.3's swap is not measured: no build in this sitting.
