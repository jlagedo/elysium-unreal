# V4r — packet S3: the overlay, the move-and-shoot gates, the attack coordinator

Reader S3, 2026-10-04, `spec-0002/step-2`. No code, no build, no run. Every claim carries its
address. **(V)** verified: read in the listing, the decompilation or the image's bytes this session;
**(I)** inferred, with what it rests on; **(D)** taken from `docs/vtmb/` and cited.

Constants were read from `vampire.dll`'s `.rdata` / `.text` bytes (image base `0x10000000`), the
install being read-only. Doc edits made: `animation_events.md` § "The move-and-shoot overlay, arm
by arm" (steps 4 and 7, the layer's envelope, the flag's writers); `animation_and_movers.md` § "The
layer table's own bookkeeping" (`AddGesture`'s refusal, slot 273's callers); `npc-ai/social.md`
§ "The melee entry and exit quartet" (slot 600's other dispatch sites).

**The finding that changes the plan** is in item 4: the port's slot 575 reads a seam that answers
0, so the overlay can never arm. It is not `0x4f` and not V5.

## 1. The `0x47` / `0x48` gesture gate (V)

- **The cvar**: `debug_allow_mf_turn`, default `"0"`, flags `0`, help "If this is on, NPCs will
  turn to look behind them periodically when they run for cover." Static constructor at
  `0x1028c7b0`: `PUSH 0x105d81c8` (help), `PUSH 0`, `PUSH 0x105399a0` ("0"), `PUSH 0x105d81ac`
  (name), `MOV ECX, 0x10923cf0`, call. `0x10923cf4` is the object's parent pointer (the same
  shape as `debug_allow_move_facing`, object `0x10924f70`, read through `*0x10924f74`). No loose
  cfg / txt in the install names it (the packed archives were not searched).
- **The gate, in order** (`0x102e8626..0x102e86a1`): `IsCommand()` false; int value (`+0x2c`)
  non-zero; the Troika self-cast `+0x98` non-null; `|GetPoseParameter("move_yaw")| > 90.0`
  (double `0x1044e668`, strict: `AND 0x4100 / JNZ`); `RandomInt(0, ftol((190.0 − |yaw|) × 0.25))
  < 5` (doubles `0x1049d910`, `0x10449260`; `[0x1070b244]` slot 2).
- **What each decides**: the cvar, whether the arm exists at all; 90, that the body is running
  more away from its facing than toward it; the draw, a chance that rises as the yaw nears 180
  (bound 25 at 90°, 2 at 180°: certain from about 174°).
- **Shipped: closed. No gesture, and no `RandomInt` draw.** The side pick compares the dot with
  float `0.0` (`0x104454c4`): `<= 0` → `0x48`, else `0x47`.
- **`0x1045001c`** is the **float 0.95** (`FCOMP float ptr`, twice, `0x100988d6` / `0x100988ed`).
  It decides whether the envelope is computed at all: `blendIn < 0.95 || blendOut < 0.95`.
  Otherwise the weight stays 1.0 (then the `m_flWeightMax` clamp). Inside: `blendIn != 0 && cycle
  < blendIn` → `cycle / blendIn`; `blendOut != 0 && cycle > 1 − blendOut` → `(1 − cycle) /
  blendOut` (later write wins); `3w² − 2w³` (double 3.0 at `0x10450010`).

## 2. Slot 481 under `+0x5ca4` (V)

`+0x5ca4` is `m_bConditionsGathered` (cleared by `RunAI 0x1026f110`, set by `GatherConditions
0x1026ec30`; four accessors, no other). Slot 481 is **`CAI_BaseNPC::GatherEnemyConditions
0x10270b20`** on all 78 classes that fill it except `CNPC_Crow` (`0x10357680`) and the player
(`0x1034f450`). For a Troika human: the base body, walked in `conditions-and-states.md` :2837 (D).
So `0x102e83e0` gathers the enemy conditions itself — LOS debounce, `SEE_ENEMY`, `ENEMY_TOO_FAR`,
and through slots 564 / 561 the attack conditions — when the think has not gathered yet, before it
reads `0x4f`. It answers nothing; its effect is the condition set.

## 3. Writers of flags2 `0x400`

- **Native (V)**: one. `CNPC_VMingXiao::NPCThink` `0x10394b0b`, `OR [ESI+0x14bc], 0x80000400`.
  Method: the field ledger (`vtmb_readers 0x14bc`: 74 typed, 11 untyped accessors); the
  decompilation's or-masks on the field; a byte scan of `.text` for `OR [reg+0x14bc], imm32`
  (four sites: `0x80004000`, `0x80000008`, `0x82000000`, and this one), for the byte form on
  `+0x14bd`, and for `OR reg, imm` followed by a store to `+0x14bc`. The generic setter
  `0x102a9800` (`flags2 |= arg`) has one caller, `StartTask 0x102a1910` (the task arm).
- **Data (V)**: `TASK_SET_NPC_FLAG NPCFlag:MOVE_FACE_ENEMY` in exactly three programs —
  `SCHED_TROIKA_TAKE_COVER_HINT`, `_RUN_AWAY_FROM_ENEMY`, `_TAKE_COVER_NO_AMMO` (the three
  embedded texts `0x105f4080`, `0x105eec38`, `0x105f46e8`; the deployed `.sch` agree). No file
  under `Content/ElysiumCorpus/scripts/` names the flag or any NPC-flag setter.
- **Who carries it**: nobody at spawn. On `sm_hub_1` and `sp_tutorial_1` an NPC carries it only
  while it runs one of the three programs; the overlay additionally needs `CAP_MOVE_SHOOT`
  (`npc_VHumanCombatant`, `npc_VVampire`) and an active `0x6000` weapon. **(I)** which placed NPCs
  those are was not enumerated: it is a run-time fact of the selector, not a spawn key.
- **`cover`, `cover_armed`, `cover_reclaim`, `cover_move_shoot` reach the overlay (V for the
  chain)**: all four expect `SCHED_TROIKA_TAKE_COVER_HINT` on an `npc_VHumanCombatant` holding
  `item_w_thirtyeight` (alternate `item_w_knife`: the active weapon is the pistol).
- **`range_bands` does not (I, now on a closed list)**: no native writer applies to a human, and
  its expected programs (`RANGE_ATTACK1`, `STEP_BACK_` / `FORCED_RANGE_ATTACK1`) are none of the
  three. It would reach the overlay only by selecting `RUN_AWAY_FROM_ENEMY 0xb9` or
  `TAKE_COVER_NO_AMMO`; a trace showing either is the thing to read first.

## 4. Condition `0x4f`

- **Retail (V + D)**: one producer. `GatherEnemyConditions 0x10270b20` → slot 564
  `FCanCheckAttacks 0x102953a0` (false when caps carry `0x8000`, a weapon is active and
  `m_bInMelee` is clear; else the base: `SEE_ENEMY` and not `ENEMY_TOO_FAR`) → slot 561
  `GatherAttackConditions 0x1026dd10` → weapon slot 365 `0x1024f670(enemy, dot, dist)`: clip `< 1`
  → `0x40`; `d < 100` → `0x08`; `d < m_fMinRange1` → `0x5f`; `d > m_fMaxRange1` → `0x60`; `dot <
  0.5` → `0x61`; else `0x4f` when `0x10252410(weapon, 0)` (the next-attack stamp passed), else 0.
  Then the two slot-562 LOS tests. `SetCondition(…, 0x4f)` appears in no other body
  (`vtmb_grep`). The overlay reads it with plain `HasCondition` (`0x10269b30` is a forwarder), not
  the interrupt-masked `0x10269d30`.
- **The port raises it (V)**: `ElysiumNpcCond::GatherAttackConditions`
  (`ElysiumNpcConditions.cpp`, the ranged band → `FElysiumWeapon::RangeAttack1Conditions`), called
  from `FElysiumNpcBase::GatherEnemyConditions` (`ElysiumNpcBaseConditions2.cpp`, slot 564 then
  561). **Nothing of V5's remaining half is needed, and nothing needs pulling into V5a for
  `0x4f`.** V5a-1 rewrites the same body; its top clear makes `0x4f` per-gather instead of sticky,
  which the overlay tolerates (it re-reads every `RunTask`).
- **What is actually missing, in the order the record will meet it**:
  1. **Slot 575 never passes (V).** `FElysiumNpc::ShouldMoveAndShoot` (`ElysiumNpcMotor.cpp`)
     tests `ActiveWeaponCapabilityWord() & 0x6000`, and that function
     (`ElysiumNpcBaseMotor.cpp`) is a seam returning 0. `StartTaskOverlay` therefore always takes
     the disable (`+0x18 = FLT_MAX`) and `0x102e8560` returns at step 1. V4o's README (§2 P5) and
     brief O2 ("slot 575 … retail already — do not rewrite") are wrong. The real word exists:
     `FElysiumNpc::SelectActiveWeaponWord()` (`ElysiumNpcSelect.cpp`), the read V11 already
     prescribes for `Slot600`. **Owner: V4o lane O2.**
  2. **`0x102e83e0`'s own gather (V)**: the `m_bConditionsGathered` test and the slot-481 call
     (item 2). Owner: O2, already in its body.
  3. **The dot (I)**: `0x4f` needs `BodyDirection2D · toEnemy >= 0.5` while running. Retail turns
     the body through the facing target the overlay's tail adds each `RunTask`
     (`AddFacingTarget(enemy, lkp, 1.0, 0.8, 0)`) and NPCThink's own under
     `debug_allow_move_facing`; the motor's blend of facing against move direction is V4b lane
     B2's (`0x102e1a83`, its brief). If the gunman's run points more than 60° off the player and
     the body does not turn, the trace shows `NOT_FACING_ATTACK 0x61`, not `0x4f`: placed on
     **V4b B2**, not V5. Rests on B2's brief, not on a read of the motor.
  4. **The pause pair (V, not a blocker)**: `UpdateBurstShootPause`'s words are a seam answering
     0 / 0 (`ActiveWeaponBurstPauseWords`, `v5a/README.md` §7), so the overlay's re-arm draws
     `RandomFloat(0, 0)`: bursts follow each other with no pause. The record still goes green.

## 5. Slot 273's callers (V, one arm I)

**None.** `RestartGesture 0x10099570` pops three words (`RET 0xc`); its thunk `0x10008233` has no
caller; the corpus's "23 dispatch sites" are every `CALL [reg+0x444]` in the image and none fits:
eleven are a weapon's own slot 273 (`0x10251de0`, in `Precache` / `Deploy`); `0x102395c0`,
`0x10252ea0`, `0x10258440` dispatch on a weapon's `this`; `CBaseDoor` / `CRotDoor::DoorActivate`
(`0x100f0340`, `0x100f26b0`) and `0x100eef50` pass no argument to a lock or doorknob
(`0x10225b40` / `0x10225ce0`); `0x100dac20` passes two to an `RTDynamicCast` receiver ((I): the
terminal's `AcceptCmd`, by the two-word pop); `0x10189780` passes none and reads a pointer back.
The body itself: slot 271 finds the layer → `m_flCycle = 0`; a miss with the second argument set →
`AddGesture(act, third argument)`.

## 6. The README's walk, line by line (V)

Confirmed as written: `AddGesture 0x100991b0` (slot 270 → slot 271's index; `SelectWeightedSequence`;
`TEST EAX,EAX / JG`, so **`< 1` refuses**; `0x100990f0(seq, autokill)` → slot 272 → slot 268 `(i,
−1, seq, autokill)`; `+0x758 = act`); slot 250 `0x10098bb0` (base, then each layer with `weight !=
0` on the base's returned interval; finished and autokill → `weight = 0`, slot 112 `(i,
activity)`); `0x10098830`'s cycle ends (below 0: looping drops the integer part, else 0, no flag;
at or above 1: `finished = 1` on both, looping drops the integer part, else 1.0); `0x10098cd0`
(`finished = 0`; window `[+0x2c, cycle + 0.1 × cycleRate × rate)`, stored; ids `< 5000`; stride
`0x4c`; the wrap clause `flags & 1 && end >= 1.0 && ev < end − 1.0`; no clamp, no weight test, no
`OnSequenceFinished`; `eventtime = (ev − cycle) / (cycleRate × rate) + owner m_flAnimTime`).

Wrong or missing:

1. **The envelope's gate was unstated**: 0.95 (item 1), and the two `!= 0` tests. A snapped layer
   (both blends 0) keeps weight 1.
2. **§6 `.LayerAdvance`, "a second advance in one tick moves nothing" is false for a fresh
   layer.** `CBaseAnimating::StudioFrameAdvance 0x1008f120` returns `0.0` when `dt <= 0.001`
   (double `0x1044f020`, `0x1008f1e9..0x1008f209`); the owner still calls the layer body with that
   0; the cycle does not move but the weight is recomputed, and a layer at cycle 0 with `blendIn
   0.2` gets `0 / 0.2 = 0`: **it frees itself** (occupancy is `weight != 0`). Its dispatch still
   runs, so the 3031 at cycle 0.0 still fires once. For a layer past cycle 0 the recomputed weight
   equals the old one. In the move-and-shoot path the push is in `RunTask` and the think's first
   advance is `PostRun`'s (I, from R2's think order), so the layer normally lives; a `RunTask` arm
   that calls `AutoMovement` before the push would kill it.
3. **`animation_and_movers.md` said `AddGesture` refuses "1 or less"**; the README's `< 1` is
   right. Doc corrected.
4. R2's two open values: `0x1044f020` = 0.001; the early-out returns 0.0.

## 7. The six coordinator bodies (V)

All six agree with `social.md`'s table and with the V11 README's condensed one: `0x1025db50`
(`count < cap`); `0x1025db70` (listed → 1; room → append; full → `0x1025dca0(npc, 1)`; a NULL NPC
recurses); `0x1025dca0` (room → `db70`; threshold the candidate's `+0x6268` or 0.0; strictly
greatest member; none → 0; else release and add); `0x1025ddd0` (first match overwritten by the
last, `count--`); `0x1025de90` (1 when absent); `0x1025df40` (`AngleDiff(this NPC's bearing, the
member's)`, bearings `atan2` from the enemy to each, best magnitude from 360.0, `<= 0` → −1; a
stale member is dereferenced). Cap 2, three objects (`0x1025d880`).

Precisions for V11:

- **The arena brawler is on the other line.** `npc_VHumanCombatant`'s slots 599..602 are
  `0x10385ab0`, `0x10385c30`, `0x10385cf0`, `0x10385d70` (its vtable), not `0x102b5650..5900`:
  599 calls `0x1025db70` twice, 601 releases unguarded, 602 drops the null-coordinator test.
- **Registers**: slot 599 (`db70`), slot 600 (`dca0`, no distance; dispatched from slot 322
  `0x102a0910` and also `0x1029f8f0`, `OnTakeDamage 0x102beda0` / `0x10385a50`, `0x10374e50`, the
  Werewolf — not walked). **Releases**: slot 601 only.
- **Death**: `Event_Killed` dispatches slot 601 (three bodies, `0x102bf340`, `0x10385a90`, `0x103c4f40`); removal:
  `UpdateOnRemove 0x1028d6e0`. **Schedule change frees nothing**: `OnScheduleChange 0x102a0940` is
  not among the 20 functions holding a `CALL [reg+0x964]`; a slot is freed on a reselect only when
  a melee selector decides to leave (slot 602, or its `0x59` / `9` / `0x1a` arms).
- The must-leave timer is `curtime + RandomFloat(7.5, 15.0)` on both lines (a ledger verdict says
  "4-15 s"; the immediates are `0x40f00000`, `0x41700000`).

## 8. The bat-only chain (V for each body; the first pick (I))

`CNPC_VHumanCombatant::SelectSchedule 0x103872d0`: state 2, active weapon word `& 0x18000` → slot
604 `0x10385e40`. Slot 308 is `HasUsableRangedWeapon 0x10336d70` (an inventory scan for a `0x6000`
weapon with a clip or ammo): **false** for a bat-only NPC.

1. Not in melee → slot 599 `0x10385ab0`: the can-enter timer passed; the "beyond twice the range"
   refusal needs slot 308, so it never applies; height or `0x59`; `0x1025db70` admits →
   `m_bInMelee = 1`.
2. **That same selection answers `0xc7 SCHED_TROIKA_MELEE_IDLE`** (line `0x6c1`) (I): until
   `m_bInMelee` is set, `FCanCheckAttacks 0x102953a0` is false (caps `0x8000`, a weapon), so no
   melee band word has been gathered: `0x51`, `9` and `0x60` are all clear.
3. The next gather sets the band (weapon slot 367 → `0x103ea7e0`, unread) and breaks `MELEE_IDLE`
   (its mask lists `0x51`, `0x5f`, `0x60`, `9`). Reselect, in melee, slot 602 not leaving:
   - **in reach**, `0x51` → slot 308 false → **`0xdd SCHED_TROIKA_MELEE_ATTACK1_NR`** (line
     `0x65d`) → `SET_SCHEDULE MELEE_ATTACK1_SWING 0xde` → `task_melee_attack1`.
   - **out of reach**, `9` or `0x60`: `0x102a11d0` true → `0xe1` (circle, `_NR`); else the cover
     chooser `0x102b7690(0, 1, 0, 1)` if it answers; else `dist <= 100` (the melee-range cvar) and
     the height timer not lapsed → `0xd2` (line `0x6bb`); else **`0xcb
     SCHED_TROIKA_MELEE_ADVANCE_NR`** (line `0x6b0`).
   - neither `0x51` nor `9` / `0x60` (`0x61`, `0x5f`, nothing) → `0xc7` again.

The V11 README's reading (`0xdd` / `0xcb`, the record's "only with a ranged weapon" reversed) is
confirmed. Not in it: the `MELEE_IDLE` step first, and `0xd2` inside 100 units without `0x51`.
`0xcb`'s and `0xd2`'s names are the README's and the `.sch` set's (`…_ADVANCE_NR`,
`…_ADVANCE_SLOW_NR` (I)); the port's number table holds neither.

## Changes to the plan

- **`v4o/README.md`**: §2 P5 — slot 575 is **not** retail in the port (item 4.1); add the row. §1
  "Who reaches it" — `range_bands` stays out, now on a closed writer list. §5 notes and §7 "`0x4f`
  on the move" — replace "the cause is then `GatherAttackConditions` (V5)" with the order of item
  4: slot 575, then `0x61` (V4b B2), then `0x2f` / `0x66`; V5 is not a placement. §6
  `.LayerAdvance` — replace "a second advance in one tick moves nothing" with item 6.2 (a
  zero-interval advance frees a cycle-0 layer with a blend-in; moves no cycle). §7 "Unrecovered" —
  the cvar, the three doubles, `0x1045001c`, slot 481, the native writers and slot 273's callers
  are settled; `Shot 0x102387b0` in mode 1 stays O3's. **Order**: V4o is safest after V4b
  (item 4.3); if it runs before, `cover_move_shoot` may be red on `0x61`.
- **`v4o/README.md`'s record**: expectations unchanged. `about`: nothing wrong.
- **`brief-O1`**: step at :33-37 — the compare is `< 0.95` (float), with the two `!= 0` tests; the
  return path's consequence is item 6.2, and the arm test asserts it instead of "moves no layer".
- **`brief-O2`**: (a) add `ElysiumNpcMotor.cpp` (`FElysiumNpc::ShouldMoveAndShoot` only): read
  `SelectActiveWeaponWord()`; strike "slot 575 … retail already". Do not change the seam
  (`ElysiumNpcBaseMotor.cpp` is B2's; answering there moves Combat10's reload pre-pass). (b) item
  4 is settled: port the gate on a new tunables row `DebugAllowMfTurn` — `debug_allow_mf_turn`
  "0", object `0x10923cf0` — which needs `ElysiumNpcKernelTunables.h` added to its files (or the
  row as a cross-lane line); 90.0 / 190.0 / 0.25 / 0.0; with the default no draw is taken, and
  the arm test says so. (c) `CanAimAtEnemy`'s first test is `m_bConditionsGathered` → slot 481
  (the port binds the byte to `Cognition.GatheredAt`: read it as `ElysiumNpcMaintain.cpp` does).
- **`brief-O3`**: none.
- **`brief-O-integrator`** step 6: the `cover_move_shoot` red placement "V5: 0x4f never holds" is
  removed; read the trace for slot 575 (no overlay arm at all), then `cond+ 0x61` (→ V4b B2). The
  `range_bands` note stands; the only native writer is Ming Xiao's.
- **`v11/README.md`**: §1 "The quartet" — name the `0x10385ab0` line as the brawler's; slot 600's
  other dispatch sites. §6 — add the `MELEE_IDLE (0xc7)` step before `0xdd` / `0xcb` and `0xd2`
  as a legitimate approach program inside 100 units. **`v11/brief-V11-1.md`**: none (its step 1
  and step 2 already carry the two crash arms). **`brief-V11-integrator-notes.md`**: its line :39
  treats "`MELEE_IDLE 0xc7` looping" as a first-unmet; one `MELEE_IDLE` after `START_COMBAT` is
  retail — only a second consecutive one with `0x51` or `0x60` standing is a defect. `advances`
  must also admit `SCHED_TROIKA_MELEE_ADVANCE_SLOW_NR` if the trace prints `0xd2` under that name.
- **`v5a/*`**: none required. Optional, for the owner: V5a-2 adds the three `NPC_Attack_Rate_*`
  fields; wiring `ActiveWeaponBurstPauseWords` to them (item 4.4) would give the overlay its
  pause, and is one function in `ElysiumNpcConditions10.cpp`, in nobody's lane today.
- **The A0 seam's records**: `cover_move_shoot`'s `known_red` text should name slot 575's seam
  (`ActiveWeaponCapabilityWord` answers 0) as the cause, not the missing layers alone.
  `melee_swing` / `chase_melee`: as V11's integrator notes, plus the `MELEE_IDLE` step in the
  `about`. `range_bands`, `cover`, `cover_armed`, `cover_reclaim`: none.

## Unrecovered, and the budget

- `0x103ea7e0` (the melee weapon's band) is still unread: which of `9` / `0x60` a far brawler gets
  is not settled here (both lead to the same arm).
- Slot 600's extra dispatch sites and the cover chooser's answer for a melee NPC were not walked.
- Which placed NPCs on the two maps run the three flag-setting programs: not enumerable from data.
- The packed archives were not searched for `debug_allow_mf_turn`.
- Queries: none passed 10 s. One breach of the file rule, stated: the 2.7 MB
  `move_and_ranged.clips.json` was parsed whole to read one clip row (`smith_attack_layer`: seq
  303, 3031 at 0.0, 5003 at 0.0714, 15 frames at 30 fps); a keyed lookup is the right path. One
  over-wide `vtmb_grep` returned 208 functions where three were wanted.
