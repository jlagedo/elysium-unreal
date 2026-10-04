# V4o — the NPC overlay layers and the move-and-shoot wire: the design

Planner's design, 2026-10-04, `spec-0002/step-2`. Pulled forward from spec 0015 by the owner's
ruling (work a story cannot be really tested without is pulled into it); it replaces J5's "stub now"
(`stories/v1/triage.md` § "Judge's rulings, V4"). **Runs after V4a, before V4c.** Not started.
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
staged first. `smith_attack_layer` (the .38) authors 3031 at cycle 0.0, length 0.4667 s *(data:
`$ELYSIUM_WORK_ROOT/import/characters/character/shared/male/move_and_ranged.clips.json`)*, so the
shot leaves in the `PostRun` of the think that pushed the layer.

**Who reaches it on step-2 records** *(read: the three `.sch`, the two `Spawn` bodies)*. Flags2
`0x400` is set only by `TASK_SET_NPC_FLAG MOVE_FACE_ENEMY` in `SCHED_TROIKA_TAKE_COVER_HINT`,
`_RUN_AWAY_FROM_ENEMY` and `_TAKE_COVER_NO_AMMO`, and cleared after the move. `cover`,
`cover_armed`, `cover_reclaim` run `TAKE_COVER_HINT` with an `npc_VHumanCombatant`
(`CAP_MOVE_SHOOT`, `0x10387110`) holding a .38 (`0x2000`): all three reach it. **`range_bands`
does not** *(inferred: `hint_groups 2`, no cover program, none of the three schedules; J5 and R2 (d)
named it without the flag's writers)*.

## 2. What the port does

| # | port today | retail |
|---|---|---|
| P1 | The table exists: `FAnimOverlayLayer AnimOverlay[4]` on `FElysiumAnimatingOverlay` (`Public/ElysiumAnimatingOverlaySlotBodies.inl`), with `FindGestureLayerByOwner` / `RemoveLayerByOwner` hand-written (`ElysiumAnimatingOverlaySlotBodies.cpp`) | `+0x734` |
| P2 | Slots 268 `SetLayer`, 269 `RemoveLayer`, 270 `HasLayer`, 272 `AllocateLayer`, 273, 275 are generated empty bodies "closed at 0015" (`ElysiumAnimatingOverlaySlots.cpp`, generated — never hand-edited); nothing in the substrate calls them | `0x10099020`, `0x10099660`, `0x10099540`, `0x10099470` |
| P3 | `FElysiumNpcBase::StudioFrameAdvance` (`ElysiumNpcBaseAnim.cpp`) runs the base only; the comment names the layers as a gap | `0x10098bb0` |
| P4 | After V4a: `FElysiumNpcBase::DispatchAnimEvents` (`ElysiumNpcBaseAnimEvents.cpp`) runs the base and a four-layer seam answering "no layer" | `0x10098c80` / `0x10098cd0` |
| P5 | `RunTaskOverlay` counts (`ElysiumNpcBaseMaintain.cpp`, `++MoveAndShootOverlay.UpdateCalls`); `StartTaskOverlay` and slot 575 are retail (`ElysiumNpcBaseSenses10.cpp`, `ElysiumNpcMotor.cpp` `ShouldMoveAndShoot`); `ArmMoveAndShootOverlay` skips the `0x11` / `0x15` sequence test, the burst words and the `RandomInt` draw | `0x102e8560`, `0x102e8270` |
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
- `RestartGesture` slot 273 and its 23 callers, `RemoveAllGestures` 275, `AddFlinchGesture` 265,
  `TASK 0xe4` (`0x10099250`, rate 2.45), the discipline applier `0x101de660`, scene gestures
  (`0x10081510`), Ming Xiao: 0010 / 0015 / their species stories. They keep today's bodies.
- `Weapon_SetActivity`'s weapon-model half and `Weapon_FrameUpdate`: V4c lane C1.
- The standing shot's base-sequence 3031 and the `ContactEventCycle` removal: V4c lane C1.
- `GatherAttackConditions` (when `0x4f` holds on the move): V5.
- The save walk of `m_AnimOverlay` and `m_MoveAndShootOverlay`: V6.

## 4. Lanes (three coders, disjoint files)

- **O1 — the layers.** `ElysiumAnimatingOverlaySlotBodies.cpp`,
  `Public/ElysiumAnimatingOverlaySlotBodies.inl`, `ElysiumNpcBaseAnim.cpp` (`StudioFrameAdvance`
  only), `ElysiumNpcBaseAnimEvents.cpp` (the layer seam only), `ElysiumNpcAnim.cpp` +
  `ElysiumNpcAnim.inl` (the row's snap bit and cycle rate, the layer's draw),
  new `Tests/ElysiumNpcKernelOverlayTests.cpp`.
- **O2 — the wire.** `ElysiumNpcBaseMaintain.cpp` (`RunTaskOverlay` only),
  `ElysiumNpcBaseSenses10.cpp` + `.inl` (`FMoveAndShootOverlay`, `ArmMoveAndShootOverlay`), new
  `ElysiumNpcBaseMoveAndShoot.cpp`, `Tests/ElysiumNpcKernelSenses10Tests.cpp` (the overlay block),
  new `Tests/ElysiumNpcKernelMoveAndShootTests.cpp`.
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
if `0x4f` never holds during the run, read the trace's `cond+` before anything else: the cause is
then `GatherAttackConditions` (V5) or the staging (move the player nearer along the gunman's line
with the cited range words), and the record stays red **placed**, never loosened. The second
`never` states that no standing attack runs before the gunman is in cover (the cover program has no
`TASK_RANGE_ATTACK1`); its `until` is tightened to the measured `at_cover` time. No `never 3031`
after cover: the cover programs fire from cover. The `health` probe is the shot's proof beyond the
event (O3); the player's start health is read from a first run, and the probe is dropped with a
note if the arena player is not at 100 or takes no ranged damage by design.

**The cover records.** `cover`, `cover_armed`, `cover_reclaim`: expectations unchanged; each now
runs with the overlay live, so their timings and the seed-1 random stream move (`0x102e8270`'s
`RandomInt`, the burst re-arm's `RandomInt` / `RandomFloat`, `m_flLastAttackTime`). The integrator
adds to each one `expect`-free line in `about`: "the run to cover fires on the move (V4o,
`cover_move_shoot`)". `cover` must stay green; `cover_armed` stays red on its own cause (the cover
tail's weapon read); a moved verdict is triaged under the bug protocol. `range_bands`: untouched
and expected unmoved (it never sets `0x400`); if its trace shows an overlay shot, a native writer
of `0x400` exists and is read before anything else.

## 6. Arm tests (each assertion names its address)

- `Elysium.Arm.NpcKernelOverlay.AddGesture` — `0x100991b0`, `0x10099470`, `0x10099020`: the seed
  words, the snap bit zeroing both blends, a sequence `< 1` refused, a held activity answered by
  its index, a full table refused.
- `.LayerAdvance` — `0x10098bb0` / `0x10098830`: the base's interval; a zero-weight layer not
  advanced; the envelope at three cycles; finished at 1.0; autokill zeroes the weight and keeps
  sequence and activity; a second advance in one tick moves nothing.
- `.LayerDispatch` — `0x10098cd0`: the window from `+0x2c`, 3031 at cycle 0.0 fires on the first
  dispatch and once, ids `>= 5000` skipped, `finished` zeroed, no clamp, a zero-weight layer still
  scanned, order base → layers 0..3.
- `Elysium.Arm.NpcKernelMoveAndShoot.Arm` — `0x102e8270`; `.CanAim` — `0x102e83e0`;
  `.MoveActivity` — `0x102e84a0`; `.Run` — `0x102e8560` step by step (the burst countdown, the
  re-arm draws on the `NpcSchedule` stream, `m_flLastAttackTime`, the next-shot clock, the facing
  target).
- `Elysium.Arm.Weapon.ShotFromAnimEvent` — `0x10238160` → `0x10238320`: an NPC's 3031 with nothing
  staged commits one shot; a second event commits a second.

Deleted (they pin a stub): the `UpdateCalls` counter and its assertions; the Senses10 assertion
that the arm "takes the arm" with no sequence test, where it contradicts `0x102e8270`.

## 7. Risks

- **`0x4f` on the move.** Whether the port raises `CAN_RANGE_ATTACK1` while the gunman runs is not
  verified (V5 owns the gather). The record can stay red for that reason; it is then placed on V5.
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
- *Unrecovered:* the cvar at `0x10923cf4` (name, default) and the doubles `0x1044e668`,
  `0x1049d910`, `0x10449260` — the `0x47`/`0x48` gesture arm (O2 reads them first; unread, the arm
  is a seam answering "gate closed", stated). The test at `0x1045001c` in the layer envelope (O1
  reads the listing). Slot 481's body under `+0x5ca4`. What `Shot 0x102387b0` gates in mode 1 (O3
  reads it). Native writers of flags2 `0x400` (one pattern searched). Slot 273's 23 callers (R2).

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
