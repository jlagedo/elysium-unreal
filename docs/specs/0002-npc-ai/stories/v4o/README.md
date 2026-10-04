# V4o — the NPC overlay layers and the move-and-shoot wire: the design

Planner's design, 2026-10-04, `spec-0002/step-2`. Pulled forward from spec 0015 by the owner's
ruling (work a story cannot be really tested without is pulled into it); it replaces J5's "stub now"
(`stories/v1/triage.md` § "Judge's rulings, V4"). **Runs after V4a and after V4b, before V4c**
*(amended after settling packet S3, 2026-10-04: the shot on the move needs the running body to
turn toward the enemy, which is V4b lane B2's blend `0x102e1a83`; run before V4b,
`cover_move_shoot` may be red on `NOT_FACING_ATTACK 0x61`)*. Not started.
**Amended after the settling packets S2 and S3** (`../v4/packets-S2.md` item 2, `../v4/packets-S3.md`):
the paragraphs they corrected carry that mark, and the four briefs carry every change — a coder
works from its brief alone. **And after `../v4/packets-S4.md` and the judge's second sitting**
(J12, J14.2, J14.3): the NPC's clip is never spent (O3), the record `ranged_sustained_fire`, two
live `0x400` writers, the nine tutorial rows (§7).
Paths are relative to `Source/ElysiumUE/Private/Substrate/` unless they say otherwise. **Line
numbers move: every site is re-located by Grep on the function name.**

Briefs: `brief-O1-layers.md`, `brief-O2-move-and-shoot.md`, `brief-O3-weapon-event-shot.md`,
`brief-O-integrator.md`.

Marks: *(read)* read in the listing or the port this session; *(doc)* walked in `docs/vtmb/` and
cited; *(data)* read in the staged import; *(inferred)*.

## 1. What retail does: the contract

The whole walk is recorded in `docs/vtmb/animation_events.md` § "Overlay layers dispatch their own
timelines…" → "The move-and-shoot overlay, arm by arm" (written this session). In short:

**The layer** *(doc, `animation_and_movers.md` § "The layer table's own bookkeeping"; struct
re-read)*. `m_AnimOverlay[4]` at `+0x734`, stride `0x30`: `m_fFlags +0`, `m_fSequenceFinished +4`,
`m_nSequence +8`, `m_flCycle +0xc`, `m_flPlaybackRate +0x10`, `m_flWeight +0x14` (occupancy is
`!= 0`), `m_flWeightMax +0x18`, `m_flBlendIn +0x1c`, `m_flBlendOut +0x20`, `m_nActivity +0x24`,
`m_bAutoKillWhenFinished +0x28`, `m_flLastEventCheck +0x2c`.

- `AddGesture 0x100991b0` *(read)*: slot 270 `HasLayer(act)` true → return slot 271's index;
  `SelectWeightedSequence(act)`; **`< 1` refuses** (−1, a `DevMsg`); `0x100990f0`: slot 272
  `AllocateLayer` (lowest zero-weight slot, −1 when full) → slot 268 `SetLayer(i, −1, seq,
  autokill)`; then `m_nActivity = act`.
- `SetLayer 0x10099020` *(read)*: activity, `cycle = 0`, `rate = 1.0`, sequence, `blendIn =
  blendOut = 0.2`, `weight = 0.1`, `weightMax = 1.0`, autokill, `finished = 0`,
  `lastEventCheck = 0`; the seqdesc's `flags & 2` zeroes both blends.
- Slot 250 `0x10098bb0` *(read)*: the base `0x1008f120`, then for each layer with `weight != 0`
  `CAnimationLayer::StudioFrameAdvance 0x10098830` **with the base's returned interval**; finished
  and autokill → `weight = 0`, slot 112 `(i, activity)` (empty on the NPC line).
- `0x10098830` *(read, listing)*: cycle advance, the `[0,1)` ends (`finished = 1` at or above 1;
  non-looping clamps to 1.0 / 0, looping drops the integer part), the weight envelope
  (`cycle / blendIn`, `(1 − cycle) / blendOut`, `3w² − 2w³`, clamped to `weightMax`).
  *(amended after S3 — item 1, item 6.1.)* **The envelope's gate**: it is computed only when
  `blendIn < 0.95 || blendOut < 0.95` (float `0x1045001c`, `FCOMP` at `0x100988d6` /
  `0x100988ed`); otherwise the weight stays 1.0 (then the `weightMax` clamp). Inside: `blendIn !=
  0 && cycle < blendIn` → `cycle / blendIn`; `blendOut != 0 && cycle > 1 − blendOut` → `(1 −
  cycle) / blendOut` (the later write wins); then `3w² − 2w³` (double 3.0 at `0x10450010`). A
  snapped layer (both blends 0) keeps weight 1. Below 0 the cycle: looping drops the integer
  part, else 0, no flag.
- **The zero-interval advance** *(S3 item 6.2)*: `CBaseAnimating::StudioFrameAdvance 0x1008f120`
  returns `0.0` when `dt <= 0.001` (double `0x1044f020`); the owner still calls the layer body
  with that 0. The cycle does not move, **but the weight is recomputed**: a layer at cycle 0 with
  `blendIn 0.2` gets `0 / 0.2 = 0` and **frees itself** (occupancy is `weight != 0`). Its dispatch
  still runs, so a 3031 at cycle 0.0 still fires once. For a layer past cycle 0 the recomputed
  weight equals the old one.
- Slot 258 `0x10098c80` → `0x10098cd0` per layer *(read)*: `finished = 0`; window
  `[layer+0x2c, cycle + 0.1 × cycleRate × rate)`, stored at `+0x2c`; ids `< 5000`; the wrap clause
  only with `flags & 1`; no clamp, no weight or "in use" test, no `OnSequenceFinished`;
  `eventtime` from the owner's `m_flAnimTime`; each event to the handler's slot 259.

**The wire** *(read)*. `StartTaskOverlay 0x10288710` arms `CAI_MoveAndShootOverlay` (`+0x5cf4`)
at each task start; `RunTaskOverlay 0x10289c90` runs `0x102e8560` every `RunTask` of a
continuous-move task (slot 529: `0x6e`, `0x0b`, `0x72`). Nine steps, in the doc section: the enemy
refresh; the goal test; `0x102e83e0` (can it aim); `0x102e84a0` (the navigator's movement activity
`9 ↔ 0x11`, `0x13 ↔ 0x15`, and the `curtime + 0.3` floor on the next shot); the `0x47`/`0x48`
gesture arm behind a cvar; the shot — `COND 0x4f` and the clock due → `--m_nMoveShots`, burst
exhausted → `RandomInt` / `RandomFloat` re-arm, else `m_flLastAttackTime = curtime`,
`AddGesture(TranslateActivity(0x1a), 1)`, `Weapon_SetActivity(0x19)`, next shot at `curtime +
fireRate − 0.1`; the tail `AddFacingTarget(enemy, lkp, 1.0, 0.8, 0)`.

**The shot** *(doc + data)*. The layer's 3031 → slot 259 → `0x1032e330` → `CWeaponRanged
0x10238160` → `0x10238320` → `ModeDispatch(1) 0x102383b0` → `Shot 0x102387b0`: no attack is
staged first. *(amended after S2 — `../v4/packets-S2.md` item 2; `brief-O3` has the gates whole.)*
`Shot` fires only for mode type 1 or 2; needs an owner and, for a non-player, the NPC pointer;
**the cooldown is a count, not a refusal** — an event that arrives while `m_flNextPrimaryAttack >
curtime` fires zero bullets and still plays the weapon activity and inserts the sound; the clip
caps the count and is not decremented for an NPC; there is no line-of-fire gate. `smith_attack_layer` (the .38) authors 3031 at cycle 0.0, length 0.4667 s *(data:
`$ELYSIUM_WORK_ROOT/import/characters/character/shared/male/move_and_ranged.clips.json`)*, so the
shot leaves in the `PostRun` of the think that pushed the layer.

**Who reaches it on step-2 records** *(read: the three `.sch`, the two `Spawn` bodies)*. Flags2
`0x400` is set only by `TASK_SET_NPC_FLAG MOVE_FACE_ENEMY` in `SCHED_TROIKA_TAKE_COVER_HINT`,
`_RUN_AWAY_FROM_ENEMY` and `_TAKE_COVER_NO_AMMO`, and cleared after the move. `cover`,
`cover_armed`, `cover_reclaim` run `TAKE_COVER_HINT` with an `npc_VHumanCombatant`
(`CAP_MOVE_SHOOT`, `0x10387110`) holding a .38 (`0x2000`): all three reach it. **`range_bands`
does not** *(inferred: `hint_groups 2`, no cover program, none of the three schedules; J5 and R2 (d)
named it without the flag's writers)*.
*(amended after S3 — item 3: now on a closed writer list.)* The image has **one native writer** of
flags2 `0x400`: `CNPC_VMingXiao::NPCThink` (`0x10394b0b`, `OR [ESI+0x14bc], 0x80000400`); the
generic setter `0x102a9800` has one caller, the task arm. No script names the flag. Nobody carries
it at spawn. So `range_bands` stays out: it would reach the overlay only by selecting
`RUN_AWAY_FROM_ENEMY 0xb9` or `TAKE_COVER_NO_AMMO`; a trace showing either is the thing to read
first. `cover_move_shoot` (the `cover_armed` staging) reaches it as the other three do.

## 2. What the port does

| # | port today | retail |
|---|---|---|
| P1 | The table exists: `FAnimOverlayLayer AnimOverlay[4]` on `FElysiumAnimatingOverlay` (`Public/ElysiumAnimatingOverlaySlotBodies.inl`), with `FindGestureLayerByOwner` / `RemoveLayerByOwner` hand-written (`ElysiumAnimatingOverlaySlotBodies.cpp`) | `+0x734` |
| P2 | Slots 268 `SetLayer`, 269 `RemoveLayer`, 270 `HasLayer`, 272 `AllocateLayer`, 273, 275 are generated empty bodies "closed at 0015" (`ElysiumAnimatingOverlaySlots.cpp`, generated — never hand-edited); nothing in the substrate calls them | `0x10099020`, `0x10099660`, `0x10099540`, `0x10099470` |
| P3 | `FElysiumNpcBase::StudioFrameAdvance` (`ElysiumNpcBaseAnim.cpp`) runs the base only; the comment names the layers as a gap | `0x10098bb0` |
| P4 | After V4a: `FElysiumNpcBase::DispatchAnimEvents` (`ElysiumNpcBaseAnimEvents.cpp`) runs the base and a four-layer seam answering "no layer" | `0x10098c80` / `0x10098cd0` |
| P5 | `RunTaskOverlay` counts (`ElysiumNpcBaseMaintain.cpp`, `++MoveAndShootOverlay.UpdateCalls`); `StartTaskOverlay` and slot 575 are retail (`ElysiumNpcBaseSenses10.cpp`, `ElysiumNpcMotor.cpp` `ShouldMoveAndShoot`); `ArmMoveAndShootOverlay` skips the `0x11` / `0x15` sequence test, the burst words and the `RandomInt` draw | `0x102e8560`, `0x102e8270` |
| P5b | *(added after S3 — item 4.1; it corrects P5's "slot 575 [is] retail")* **Slot 575 never passes.** `FElysiumNpc::ShouldMoveAndShoot` (`ElysiumNpcMotor.cpp`) tests `ActiveWeaponCapabilityWord() & 0x6000`, and that function (`ElysiumNpcBaseMotor.cpp`) is a seam returning 0: `StartTaskOverlay` always takes the disable (`+0x18 = FLT_MAX`) and `0x102e8560` returns at step 1 — **the overlay can never arm**. The real word exists: `FElysiumNpc::SelectActiveWeaponWord()` (`ElysiumNpcSelect.cpp`). Lane O2 fixes the read inside `ShouldMoveAndShoot`; the seam itself is not edited (answering there moves Combat10's reload pre-pass) | slot 575 `0x102bf4a0`: the active weapon's slot 360 `& 0x6000` |
| P6 | The weapon commits an event only into a staged transaction (`FElysiumWeapon::CommitFromAnimEvent`: `!Swing.bActive` → "nothing to commit"); `BeginRangedShot` stages one and plays `ACT_RANGE_ATTACK1_LAYER` on the `UpperBody` channel itself | `0x10238320` → `ModeDispatch(1)`: the event is the shot |
| P7 | The draw already exists and needs no bake: a `FElysiumClipSegment` with `Channel = EElysiumAnimChannel::UpperBody`, `bSnap`, `bLoop = false` through `FElysiumAnimating::PlayAnimSegment` composes a masked layer over the gait (`FElysiumOverlayStack`, 0015 story 1, landed) | the client's layer blend |

**The bake carries everything**: the layer clips, their events, lengths and snap bit are staged and
baked today. **No pipeline change, no import, no judge item.**

## 3. The scope cut

In: the kernel's four layers as retail's record — `AddGesture`, `AllocateLayer`, `SetLayer`,
`HasLayer`, `RemoveLayer`, the layer advance in slot 250, the per-layer dispatch in slot 258; the
move-and-shoot body whole (`0x102e8270`'s missing half, `0x102e83e0`, `0x102e84a0`, `0x102e8560`);
the weapon's event-is-the-shot entry for an NPC; the layer drawn through the existing `UpperBody`
door (visual-only; the kernel's cycle, events and finish are the contract).

Left, named, with its owner:
- 0015: the previous-sequence cross-fade (story 2), the player's layers and their events (story
  3's player half; J3's A4 dispatches them), the stamps and envelope arithmetic of the DRAWN stack
  (4), the instruments (5), the mask source (6). The drawn layer's own cycle is the render stack's;
  it is not re-timed to the kernel's (K2's residue).
- `RestartGesture` slot 273 (**no caller reaches an NPC**: its thunk `0x10008233` is uncalled and
  none of the image's 23 `CALL [reg+0x444]` sites fits it — S3 item 5; dead on step-2 paths),
  `RemoveAllGestures` 275, `AddFlinchGesture` 265,
  `TASK 0xe4` (`0x10099250`, rate 2.45), the discipline applier `0x101de660`, scene gestures
  (`0x10081510`), Ming Xiao: 0010 / 0015 / their species stories. They keep today's bodies.
- `Weapon_SetActivity`'s weapon-model half and `Weapon_FrameUpdate`: V4c lane C1.
- The standing shot's base-sequence 3031 and the `ContactEventCycle` removal: V4c lane C1.
- `GatherAttackConditions` (when `0x4f` holds on the move): ~~V5~~ *(amended after S3 — item
  4)* **already ported and raising `0x4f`** (`ElysiumNpcCond::GatherAttackConditions`, called
  from `FElysiumNpcBase::GatherEnemyConditions`, slot 564 then 561); nothing of V5 is needed for
  it. V5a-1 rewrites that body; its top clear makes `0x4f` per-gather, which the overlay
  tolerates (it re-reads every `RunTask`).
- The burst pause: `UpdateBurstShootPause`'s words are a seam answering 0 / 0
  (`ActiveWeaponBurstPauseWords`, `ElysiumNpcConditions10.cpp`; `../v5a/README.md` §7), so the
  re-arm draws `RandomFloat(0, 0)` and bursts follow each other with no pause. Not a blocker; in
  nobody's lane (V5a-2 adds the three `NPC_Attack_Rate_*` fields it would read).
- The save walk of `m_AnimOverlay` and `m_MoveAndShootOverlay`: V6.

## 4. Lanes (three coders, disjoint files)

- **O1 — the layers.** `ElysiumAnimatingOverlaySlotBodies.cpp`,
  `Public/ElysiumAnimatingOverlaySlotBodies.inl`, `ElysiumNpcBaseAnim.cpp` (`StudioFrameAdvance`
  only), `ElysiumNpcBaseAnimEvents.cpp` (the layer seam only), `ElysiumNpcAnim.cpp` +
  `ElysiumNpcAnim.inl` (the row's snap bit and cycle rate, the layer's draw),
  new `Tests/ElysiumNpcKernelOverlayTests.cpp`.
- **O2 — the wire.** `ElysiumNpcBaseMaintain.cpp` (`RunTaskOverlay` only),
  `ElysiumNpcBaseSenses10.cpp` + `.inl` (`FMoveAndShootOverlay`, `ArmMoveAndShootOverlay`), new
  `ElysiumNpcBaseMoveAndShoot.cpp`, **`ElysiumNpcMotor.cpp` (`FElysiumNpc::ShouldMoveAndShoot`
  only — the slot-575 seam fix, P5b)**, `Tests/ElysiumNpcKernelSenses10Tests.cpp` (the overlay
  block), new `Tests/ElysiumNpcKernelMoveAndShootTests.cpp`. **The tunables row**
  `DebugAllowMfTurn` (`debug_allow_mf_turn` "0", object `0x10923cf0`) is a cross-lane line:
  `ElysiumNpcKernelTunables.h` is generated from `kernel_tunables.tsv`, so O2 writes the row in
  its report and the integrator adds it and regenerates before the build.

**Disjointness, re-checked after S3** (by listing): O1 — the two overlay slot-body files,
`ElysiumNpcBaseAnim.cpp`, `ElysiumNpcBaseAnimEvents.cpp`, `ElysiumNpcAnim.{cpp,inl}`, its test. O2
— `ElysiumNpcBaseMaintain.cpp`, `ElysiumNpcBaseSenses10.{cpp,inl}`,
`ElysiumNpcBaseMoveAndShoot.cpp`, `ElysiumNpcMotor.cpp`, its two tests. O3 —
`ElysiumWeaponClasses.{h,cpp}`, `ElysiumWeaponTests.cpp`. No file is in two lanes;
`ElysiumNpcMotor.cpp` was in none. `ElysiumNpcBaseMotor.cpp` (the seam's home) is in no V4o lane
and is not edited.
- **O3 — the event is the shot.** `ElysiumWeaponClasses.{h,cpp}` (`CommitFromAnimEvent` and a new
  unstaged-shot entry only), `Tests/ElysiumWeaponTests.cpp`.

O2 calls O1's `AddGesture(int32 Activity, bool bAutoKill)` and `HasLayer(int32)`; O1 calls nothing
of O2's. O3 is called by nobody new: the event arrives through the existing slot 259 chain.

**Shared names.** `FElysiumAnimatingOverlay::AddGesture(int32 Activity, bool bAutoKill)` → layer
index or `INDEX_NONE`; `AdvanceOverlayLayers(float Interval)`;
`DispatchOverlayLayerEvents(FElysiumEntity* Handler)`; the virtual hook
`OnOverlayLayerSet(int32 Layer)` (base empty; `FElysiumNpc` draws). On `FElysiumNpc`:
`SequenceSnaps(int32)`, `SequenceCycleRateOf(int32)`. `FMoveAndShootOverlay`: `bMovingAndShooting`
(`+0x10`), `MoveShots` (`+0x14`), `NextShotTime` (`+0x18`, exists), `MinBurst`, `MaxBurst`,
`PauseMin`, `PauseMax` (exist), `InitialDelay` (`+0x2c`). `FElysiumNpcBase::RunMoveAndShootOverlay()`
(`0x102e8560`). `FElysiumWeapon::ShotFromAnimEvent(const FElysiumAnimEvent&)`.

**From V4a, by name** (the integrator checks each exists before the coders start):
`FElysiumNpcBase::DispatchAnimEvents(float, FElysiumEntity*)` in `ElysiumNpcBaseAnimEvents.cpp`
with its layer seam; A1's base-dispatcher entry in `ElysiumAnimEvents.{h,cpp}` and, if J3's
wording was followed, its per-layer body over the words struct (Grep `0x10098cd0`: O1 calls it
rather than writing a second one); `FElysiumNpc::SequenceEvents(int32)` and `SequenceLoops(int32)`;
`FElysiumNpcBase::LastEventCheck`; the `animevent` tap inside the kernel dispatcher; the world poll
deleted (`FElysiumAnimating::AdvanceAnimEvents` gone).

**The generated slots.** `ElysiumAnimatingOverlaySlots.cpp` defines 268, 269, 270, 272 as empty
bodies from `research/tooling/ghidra/driver/kernel_verdicts.tsv` (target `0015`). O1 writes the
four bodies under their slot names in `ElysiumAnimatingOverlaySlotBodies.cpp`; the **integrator**
retargets the four rows (`10099020`, `10099660`, `10099540`, `10099470`) to
`seam:FElysiumAnimatingOverlay::<Name>` — the shape rows `100994c0` / `100995e0` already have — and
regenerates (`uv run elysium research gen_kernel_shape`) before the build.

## 5. Records

**`combat/cover_move_shoot.json`** — written red by A0 (J5); the O integrator makes it exactly
this (creating it if A0's commit lacks it) and removes `known_red`:

```json
{
  "name": "cover_move_shoot",
  "about": "The run-and-gun. cover_armed's staging: a hostile npc_VHumanCombatant (CAP_MOVE_SHOOT, CNPC_VHumanCombatant::Spawn 0x10387110) with a .38 (weapon +0x5a0 & 0x6000) runs SCHED_TROIKA_TAKE_COVER_HINT, whose first task sets NPCFlag MOVE_FACE_ENEMY (flags2 0x400). At TASK_WAIT_FOR_MOVEMENT 0x6e (slot 529) StartTaskOverlay 0x10288710 passes slot 575 0x102bf4a0 and arms CAI_MoveAndShootOverlay +0x5cf4 (0x102e8270). Each RunTask, RunTaskOverlay 0x10289c90 -> 0x102e8560: with COND_CAN_RANGE_ATTACK1 0x4f and the clock due (0.3 s after the run activity is swapped to its aim twin, 0x102e84a0) it pushes AddGesture(TranslateActivity(0x1a)) 0x100991b0 -- smith_attack_layer, 3031 at cycle 0.0 -- and the layer's own dispatch 0x10098cd0 inside slot 258 0x10098c80 hands 3031 to HandleAnimEvent -> CWeaponRanged 0x10238160 -> Shot 0x102387b0, in the same think's PostRun. The base sequence during the move is the run, which authors no 3031: an animevent 3031 before task_wait_for_movement ends is the layer's. docs/vtmb/animation_events.md 'The move-and-shoot overlay, arm by arm'.",
  "stage": "arena",
  "seed": 1,
  "duration": 40.0,
  "player": { "at": "cover_seat", "armed": "item_w_thirtyeight" },
  "cast": [
    {
      "name": "arena_gunman", "classname": "npc_VHumanCombatant", "at": "far_ne", "face": "player", "body": "regular_cop",
      "keys": {
        "stattemplate": "TutorialThug", "additionalequipment": "item_w_thirtyeight", "alternateequipment": "item_w_knife",
        "player_reaction": "D_HT 5",
        "hint_groups": "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32",
        "npc_perception": "3", "stay_entrenched": "0", "allow_kick_hint_use": "1", "squadname": "", "spawnflags": "4",
        "StartHidden": "0", "percent_occluded_wait": "0", "percent_occluded_cover": "100", "percent_occluded_walk": "0",
        "percent_occluded_flank": "0", "percent_occluded_chase": "0"
      }
    }
  ],
  "script": [ { "t": 0.05, "do": "console", "command": "elysium.cmd slot3" } ],
  "expect": [
    { "label": "covers", "who": "arena_gunman", "kind": "schedule", "match": "SCHED_TROIKA_TAKE_COVER_HINT (", "by": 8.0 },
    { "label": "runs", "who": "arena_gunman", "kind": "task", "match": "task_wait_for_movement (", "within": 2.0 },
    { "label": "shot_on_the_move", "who": "arena_gunman", "kind": "animevent", "match": "3031", "within": 3.0 },
    { "label": "arrives", "who": "arena_gunman", "kind": "taskdone", "match": "^task_wait_for_movement$", "regex": true, "within": 25.0 },
    { "label": "at_cover", "who": "arena_gunman", "kind": "taskdone", "match": "^task_snap_to_hint$", "regex": true, "within": 2.0 }
  ],
  "never": [
    { "who": "arena_gunman", "kind": "death" },
    { "who": "arena_gunman", "kind": "task", "match": "task_range_attack1 (", "after": "covers", "until": 8.0 }
  ],
  "probes": [
    { "at": "end", "who": "arena_gunman", "probe": "alive", "equals": true },
    { "at": "end", "who": "player", "probe": "health", "less": 100 }
  ]
}
```

Notes for the integrator. The `runs` → `shot_on_the_move` bound: 0.3 s (`0x1047b868`) after the
first aiming think, `m_initialDelay` 0, the same-think dispatch, plus the time for `0x4f` to rise —
if the shot does not come, read the trace **in this order** *(amended after S3 — item 4; V5 is
not a placement)*: (1) **slot 575** — no overlay arm at all means `ShouldMoveAndShoot` still
reads the seam (O2's fix did not land); (2) **`cond+ 0x61` `NOT_FACING_ATTACK`** instead of
`0x4f` — the running body points more than 60° off the player and does not turn: placed on **V4b
lane B2** (the motor's blend of facing against move direction, `0x102e1a83`); (3) `0x2f` / `0x66`
(the two slot-562 LOS tests) or the range words `0x5f` / `0x60` — the staging (move the player
nearer along the gunman's line with the cited range words). The record stays red **placed**,
never loosened. The second
`never` states that no standing attack runs before the gunman is in cover (the cover program has no
`TASK_RANGE_ATTACK1`); its `until` is tightened to the measured `at_cover` time. No `never 3031`
after cover: the cover programs fire from cover. The `health` probe is the shot's proof beyond the
event (O3); the player's start health is read from a first run, and the probe is dropped with a
note if the arena player is not at 100 or takes no ranged damage by design.

**`combat/ranged_sustained_fire.json`** (J12; written red by A0, `../v4/brief-A0-seam.md` item
7b; the O integrator turns it): the `ranged_open_fire` staging, `expect` eight `animevent 3031` on
the shooter (two more than the .38's `Size 6`), `never` `cond+ NO_PRIMARY_AMMO (0x40)`, `never`
`task_reload`. It needs slot 363 (V4a) and the wait (V5a-2, N2) before O3's fix shows: red on
either is placed there, not on O3.

**The cover records.** `cover`, `cover_armed`, `cover_reclaim`: expectations unchanged; each now
runs with the overlay live, so their timings and the seed-1 random stream move (`0x102e8270`'s
`RandomInt`, the burst re-arm's `RandomInt` / `RandomFloat`, `m_flLastAttackTime`). The integrator
adds to each one `expect`-free line in `about`: "the run to cover fires on the move (V4o,
`cover_move_shoot`)". `cover` must stay green; `cover_armed` stays red on its own cause (the cover
tail's weapon read); a moved verdict is triaged under the bug protocol. `range_bands`: untouched
and expected unmoved (it never sets `0x400`); if its trace shows an overlay shot, read which of
`RUN_AWAY_FROM_ENEMY 0xb9` / `TAKE_COVER_NO_AMMO` it selected — the only native writer of `0x400`
is Ming Xiao's (S3 item 3).

## 6. Arm tests (each assertion names its address)

- `Elysium.Arm.NpcKernelOverlay.AddGesture` — `0x100991b0`, `0x10099470`, `0x10099020`: the seed
  words, the snap bit zeroing both blends, a sequence `< 1` refused, a held activity answered by
  its index, a full table refused.
- `.LayerAdvance` — `0x10098bb0` / `0x10098830`: the base's interval; a zero-weight layer not
  advanced; the envelope at three cycles; **the 0.95 gate** (`0x1045001c`: both blends `>= 0.95`
  → weight 1.0 with no envelope; a snapped layer, both blends 0, keeps weight 1); finished at
  1.0; autokill zeroes the weight and keeps sequence and activity; **a zero-interval advance
  moves no cycle, frees a cycle-0 layer that has a blend-in (weight `0 / 0.2 = 0`), and leaves a
  layer past cycle 0 at its old weight** *(amended after S3 — item 6.2; "a second advance in one
  tick moves nothing" was false for a fresh layer)*.
- `.LayerDispatch` — `0x10098cd0`: the window from `+0x2c`, 3031 at cycle 0.0 fires on the first
  dispatch and once, ids `>= 5000` skipped, `finished` zeroed, no clamp, a zero-weight layer still
  scanned, order base → layers 0..3.
- `Elysium.Arm.NpcKernelMoveAndShoot.Arm` — `0x102e8270`; `.CanAim` — `0x102e83e0`;
  `.MoveActivity` — `0x102e84a0`; `.Run` — `0x102e8560` step by step (the burst countdown, the
  re-arm draws on the `NpcSchedule` stream, `m_flLastAttackTime`, the next-shot clock, the facing
  target).
- `Elysium.Arm.Weapon.ShotFromAnimEvent` — `0x10238160` → `0x10238320` → `0x102387b0`: an NPC's
  3031 with nothing staged commits one shot; **a second 3031 inside the cooldown
  (`m_flNextPrimaryAttack > curtime`) fires nothing** — zero bullets, the activity and the sound
  still played; a second 3031 once the cooldown has passed commits a second *(amended after S2 —
  item 2)*.
- `Elysium.Arm.NpcKernelMoveAndShoot.Slot575` — `0x102bf4a0`: a `0x6000` active weapon read
  through `SelectActiveWeaponWord()` passes; no weapon, or a melee word, does not.
- `.Run`'s gesture arm — `0x102e8626..0x102e86a1`: with `debug_allow_mf_turn` at its shipped "0"
  **no gesture is pushed and no `RandomInt` draw is taken**.

Deleted (they pin a stub): the `UpdateCalls` counter and its assertions; the Senses10 assertion
that the arm "takes the arm" with no sequence test, where it contradicts `0x102e8270`.

## 7. Risks

- **`0x4f` on the move.** ~~Whether the port raises `CAN_RANGE_ATTACK1` while the gunman runs is
  not verified (V5 owns the gather). The record can stay red for that reason; it is then placed
  on V5.~~ *(amended after S3 — item 4.)* The port raises `0x4f` (one retail producer:
  `GatherEnemyConditions 0x10270b20` → slot 564 → slot 561 → weapon slot 365 `0x1024f670`; the
  overlay reads it with plain `HasCondition`). **V5 is not a placement.** What is missing, in
  the order the record meets it: **slot 575** (P5b; O2); `0x102e83e0`'s own gather (the
  `m_bConditionsGathered` test and the slot-481 call; O2, in its body); **the dot** — `0x4f`
  needs `BodyDirection2D · toEnemy >= 0.5` while running, so a body that does not turn shows
  `0x61`: placed on **V4b B2** (inferred from B2's brief, not from a read of the motor), which is
  why V4o runs after V4b; then `0x2f` / `0x66`. The pause pair answering 0 / 0 is not a blocker.
- **A `RunTask` arm that calls `AutoMovement` before the push** would kill a fresh layer (the
  zero-interval advance, §1): in the move-and-shoot path the push is in `RunTask` and the think's
  first advance is `PostRun`'s *(inferred from R2's think order)*, so the layer normally lives.
- **The kernel's sequence number for a layer clip.** The bridge numbers rows on first resolve;
  `AddGesture`'s "`< 1` refuses" is retail's rule over studio indices where 0 is a real sequence.
  The bridge's row 0 is "sequence 0" (J1), so the rule ports as written; the test states it.
- **The cycle rate needs a clip length.** A row's length is known only after a play
  (`FSequenceRow::Seconds`). O1 takes it from the draw's answer; with no body (headless double) the
  fixture supplies it. A body that refuses the draw leaves rate 0: the layer never advances and its
  event at cycle 0.0 never fires (window `[0, 0)`): logged once per (model, clip).
- **Double draw.** `BeginRangedShot` also plays the layer clip on `UpperBody`; V4o's shot never
  goes through it. C1 removes that play for an NPC in V4c.
- **The generator.** If `gen_kernel_shape` rejects `seam:` on those four rows, the integrator
  stops and reports; the fallback (named methods beside the stubs, as `FindGestureLayerByOwner`)
  is the owner's call.
- **The stream moves** for every record that runs `TAKE_COVER_HINT` (§5).
- *Settled by S3 (were "unrecovered"):* the cvar is **`debug_allow_mf_turn`**, default `"0"`,
  flags 0, object `0x10923cf0` (`0x10923cf4` its parent pointer) — **shipped closed: no gesture
  and no `RandomInt` draw**; the gate in order (`0x102e8626..0x102e86a1`): `IsCommand()` false,
  the int value non-zero, the Troika self-cast `+0x98` non-null, `|GetPoseParameter("move_yaw")|
  > 90.0` (double `0x1044e668`, strict), `RandomInt(0, ftol((190.0 − |yaw|) × 0.25)) < 5`
  (doubles `0x1049d910`, `0x10449260`); the side pick compares the dot with float 0.0
  (`0x104454c4`): `<= 0` → `0x48`, else `0x47`. `0x1045001c` is the float 0.95 (§1). Slot 481
  under `+0x5ca4` (`m_bConditionsGathered`) is `CAI_BaseNPC::GatherEnemyConditions 0x10270b20`.
  The native writers of `0x400`: one, Ming Xiao's. Slot 273: no caller reaches an NPC. `Shot
  0x102387b0`'s gates: `../v4/packets-S2.md` item 2, in `brief-O3`.
- *Settled by S4 and the judge's second sitting (J12):* **an NPC's clip is `max(Default_Size, 1)`
  from equip to death** (`Inventory_Insert 0x10334e70`) and `Shot` never lowers it; the port
  spends and refuses on it for every wielder — **an old bug, fixed in lane O3** (no spend, no
  refusal beyond the set cap; the player untouched), record **`ranged_sustained_fire`**; the
  reload finish stays a seam for V5b. `0x1033d940` doubles the rate under one status of the
  owner's `+0xeb4` word — **which status is unrecovered**: O3 ports a seam answering false. No
  file under `Content/ElysiumCorpus/` names `debug_allow_mf_turn`: the default stands.
- *(J14.2, J14.3; `../v4/packets-S4.md` item d.)* **Two live writers of flags2 `0x400` on the
  witness maps, not three**: `SCHED_TROIKA_TAKE_COVER_HINT` and `_RUN_AWAY_FROM_ENEMY`.
  `_TAKE_COVER_NO_AMMO` needs `NO_PRIMARY_AMMO`, which cannot rise from firing, so it is not
  reachable (§1's "three programs" is the image's list; this is what can run). **Who can
  run-and-gun**: the overlay needs `CAP_MOVE_SHOOT` (`npc_VHumanCombatant`, `npc_VVampire`) and
  an active `0x6000` weapon — **nine placed rows on `sp_tutorial_1`**: `thug_3`
  (`item_w_thirtyeight`); `Hunter1`, `sentry3`, `sabbat_redshirt_1`, `_2`, `_5`,
  `sabbat_redshirt_2_proxy` (`item_w_mac_10`); `mercenary_upstairs` (`item_w_ithaca_m_37`);
  `condotierre_upstairs` (`item_w_steyr_aug`). **None on `sm_hub_1` with a ranged primary**
  (`Noir_Cop` and `Chunk` only after a weapon switch); the melee rows and every maker child are
  not candidates. So `cover_move_shoot`'s staging stands for `thug_3` (the same class and
  weapon), the tutorial is the map proof, and V8's hub clause cannot witness the overlay.

## 8. Size

**M.** Three coders and an integrator, one build, no bake. O1 is S–M (the table exists, the draw
exists), O2 S–M (one 989-byte body and three small ones), O3 S. The smallest testable cut is O1 +
O2: `cover_move_shoot` goes green on the event alone; O3 makes the event a shot (the `health`
probe) and is what V4c's C1 builds on.

## Rules for every agent of V4o

`stories/v4/README.md` § "Rules for every agent of V4" applies whole: retail first, cite the
address at every ported line; re-locate by Grep; coders never build, launch the editor or run a
suite; only the brief's files; a line another file needs goes in the report, exact; a divergence is
recorded in the report, not adopted; no test of a port mechanism; no commit by coders; the query
budget (10 s warns, logged to `$ELYSIUM_WORK_ROOT/logs/slow-queries.tsv`; 60 s stops, never
retried as-is); never a file over ~200 KB whole; Grep / Read / Glob, never shell `grep`; report
≤300 words.
