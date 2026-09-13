# NPC AI — The schedule kernel

Part of the [NPC AI oracle](./README.md). Sections moved verbatim from
`npc-ai-reverse-engineering.md` on 2026-09-13; their headers are the citation keys.

## Schedules and tasks: the behavior program

The recovered native schedule corpus contains 691 schedules and 4,139 task invocations over 441
distinct task identities. A schedule is an ordered task program with failure and interrupt policy;
it is not an animation clip and it is not merely a state label. The schedule can ask for a path,
wait for movement, face an entity, remember a condition, change an activity, attack, wait, or
switch to another schedule. A class-specific `StartTask` or `RunTask` body performs work that the
declarative schedule cannot express alone.

### Registration and override surface

The DLL registers 330 shared and 184 class-local task identities across 20 owners. The recovery
finds 29 `StartTask` bodies, 24 `RunTask` bodies, and 49 custom handler bodies. Thirty-one of those
custom handlers bear animation policy and 18 do not. The action-policy survey resolves 100 policy
rows and 111 routes. This is the native implementation seam where a generic schedule becomes
police, civilian, vampire, animal, boss, or other class-specific behavior.

The most frequent tasks show the structure of the library:

| Task | Invocations |
|---|---:|
| `SET_FAIL_SCHEDULE` | 328 |
| `SET_NPC_FLAG` | 312 |
| `STOP_MOVING` | 275 |
| `WAIT_FOR_MOVEMENT` | 230 |
| `SET_ACTIVITY` | 216 |
| `WAIT_RANDOM` | 213 |
| `WAIT` | 203 |
| `SET_TOLERANCE_DISTANCE` | 175 |
| `SET_SCHEDULE` | 145 |
| `RUN_PATH` | 133 |
| `FACE_ENEMY` | 69 |
| `REMEMBER` | 41 |
| `GET_PATH_TO_ENEMY` | 37 |

The tail contains path-to-cover, flank, flee, attack, dialogue, scripted, follower, and other
specialized tasks. Movement tasks target the navigator/motor layer. `SET_ACTIVITY` changes an
abstract activity, which then passes through class/weapon translation and weighted model-sequence
selection; the full activity-to-sequence chain is documented in
[animation_and_movers.md](../animation_and_movers.md).

Schedule identifiers are **per-class, not global**. Every class registers its own name table, and
the same number names a different schedule in each one. `0x156` is `SCHED_VTZIMISCE_TEST` in
`CNPC_VTzimisce`'s table, `SCHED_TZIMISCEHEADCLAW_WAIT_FOR_MELEE_ADVANCE` in
`CNPC_VTzimisceHeadClaw`'s, `SCHED_VTZIMISCERUNNER_WAIT_FOR_MELEE_ADVANCE` in
`CNPC_VTzimisceRunner`'s, `SCHED_VMING_XIAO_TRANSFORM` in `CNPC_VMingXiao`'s,
`SCHED_VANIMAL_WALK_TO_INTERESTING_PLACE_SETUP` in `CNPC_VAnimal`'s, and
`SCHED_VWEREWOLF_CONSIDER_SITUATION` in `CNPC_VWerewolf`'s. A bare `return 0x156` from a
`TranslateSchedule` body therefore states nothing until the receiving class's own table is read,
and asking who reaches a schedule by scanning the module for its numeric identifier yields a false
hit for every other class that happens to register at that number.

### The `TASK_TEST*` scaffolding is inert

`CAI_BaseNPC` registers five test tasks in `FUN_10316ff0` — `TASK_TEST1` (`0xa7`) through
`TASK_TEST5` (`0xab`), immediately after `TASK_PAUSE_MOVING` (`0xa6`). Two carry working handlers
in `CAI_BaseNPCTroika::StartTask` (`0x102a1910`), each gated by its own console variable. The
other three reach the default arm, which tail-calls `CAI_BaseNPC::StartTask` (`0x102827f0`).
**No shipped code path runs any of the five.**

`StartTask` dispatches on `pTask->iTask` over the range `5`–`0x149` through a byte index table at
`0x102a7ab8` and a jump table at `0x102a77f8`; `TASK_TEST1` lands at `0x102a1943` and `TASK_TEST2`
at `0x102a1a60`. The Ghidra case labels for this switch are compressed jump-table indices, not
task identifiers, so the two must be resolved through the byte table before a case is named.

The two console variables are ordinary `ConVar`s in `vampire.dll`, both defaulting to `"0"` with
flags `0` — no `FCVAR_CHEAT`, `FCVAR_ARCHIVE` or `FCVAR_REPLICATED` — and both sharing the help
string "Toggles stuff for the test task." There is no `client.dll` counterpart.

| | `debug_test_switch1` | `debug_test_switch2` |
|---|---|---|
| object | `0x10924678` | `0x10924630` |
| initializer | `FUN_1028b5f0` | `FUN_1028b680` |
| gates | `TASK_TEST1` | `TASK_TEST2` |
| read at | `0x102a1950`, `0x102a195f` | `0x102a1a62`, `0x102a1a75` |

Each read is an inlined `ConVar::GetInt()`: `m_pParent` at `+4`, `IsCommand()` through vtable slot
1, and the value from `m_nValue` at `+0x2c`, taken as zero when `IsCommand()` is true. No
absolute-addressed write to either object exists anywhere, because the constructor assigns
`m_pParent = this` through `ECX`; the objects are reachable only as the `this` operand of their
own constructor and destructor.

`TASK_TEST1` hides or unhides the NPC's active weapon. With the variable at its default `0` it
calls `CBaseEntity::Hide` (weapon vtable `+0x108`); nonzero calls `CBaseEntity::Unhide`
(`+0x10c`). Those set and clear `EF_NODRAW` (`0x40`) in `m_fEffects`, or in
`m_fScriptSavedEffects` when `m_bScriptHidden` is set, so a script-hidden weapon stays hidden.
The task then unconditionally jitters two axes of the NPC's origin by `RandomFloat(-200, 200)` and
draws `NDebugOverlay::Box(pos, (-2,-2,-2), (2,2,2), 192, 255, 192, 0, 2.0)`. It does not complete
itself.

`TASK_TEST2` forces an activity through `RestartIdealActivity` and then completes the task. The
arm is selected by a four-entry jump table over the values `1`–`4`:

| value | activity | id |
|---|---|---|
| 1 | `ACT_AIM` | `5` |
| 2 | `ACT_CORNER_COVER_IDLE` | `0x1118` |
| 3 | `ACT_RANGE_ATTACK1` | `0x19` |
| 4 | `ACT_DRYFIRE` | `0x58` |
| 0 or any other | `ACT_IDLE` | `1` |

`RestartIdealActivity` (`0x10289ee0`) zeroes `m_IdealActivity` (`this+0xfec`) when it already
holds the requested activity before setting it, so the clip restarts even when the activity does
not change. The completion helper (`0x10273e80`) writes `TASKSTATUS_COMPLETE` (`4`) to
`this+0x5c44`.

Three schedules contain these tasks, and nothing selects any of them:

| schedule | id | task program |
|---|---|---|
| `SCHED_TASK_TEST1` | `0x152` | `TASK_TEST1 0`, `TASK_TEST2 0`, `TASK_WAIT 3` |
| `SCHED_TASK_TEST2` | `0x153` | `TASK_TEST2 0`, `TASK_WAIT 3` |
| `SCHED_VTZIMISCE_TEST` | `0x156` | `TASK_SET_INTERRUPT_TIME 4`, `TASK_TEST1 0`, interrupts on `COND_INTERRUPT_TIME` |

`SCHED_TASK_TEST3` (`0x154`) and `SCHED_TASK_TEST4` (`0x155`) hold the two handlerless tasks. The
first three identifiers belong to the Troika table, the last to `CNPC_VTzimisce`'s own — see the
per-class identifier note above, which is what makes `0x156` look reachable from several
`TranslateSchedule` bodies that in fact name their own schedules.

Unreachability is established by exhaustion rather than by absence of an obvious caller. Scanning
the whole of `.text` for `0x152`–`0x156` in every immediate form — `PUSH imm32`, `MOV <reg>,imm32`
across seven registers, `CMP EAX,imm32`, `MOV [ESP+d],imm32`, `MOV [EBP-d],imm32` — and resolving
each hit to its containing function returns only other classes' schedule-table builders, the
activity registry (a separate identifier space), and one unrelated comparison in
`CNPC_VWerewolf::StartTask`. A by-name path is ruled out separately: every reference to a schedule
name string in the module is its own registration, and the module holds no name-to-identifier
lookup for schedules.

The trigger this scaffolding was built around is present and dead. `debug_test_schedule` (object
`0x10924db0`, initializer `FUN_1028b550`) is a `ConVar` built through the min/max overload —
default `"0"`, flags `0`, clamped to `[0, 4]`, help "Forces the AI into test schedules." — which
matches `SCHED_TASK_TEST1`–`4` exactly. It has **no readers**: a scan of `.text` for the object and
for its `m_pParent`, `m_flValue` and `m_nValue` fields finds only the constructor and destructor.

Nothing here is a game rule and none of it reaches a frame a player sees, so the rebuild
reproduces none of it. Its remaining value is as an oracle: the `TASK_TEST2` arms state five
activity identifiers unambiguously, and the handler pair states `RestartIdealActivity` and
`CBaseEntity::Hide`/`Unhide` semantics without any surrounding gameplay to disentangle.

### Schedule families

Name-family counts are a useful index, not an exclusive taxonomy: a schedule can participate in
more than one conceptual concern and not every relevant name shares a prefix.

| Family substring | Schedule count | Typical concern |
|---|---:|---|
| `INVESTIGAT` | 32 | Visual/sound anomaly investigation |
| `COVER` | 25 | Find, occupy, or leave cover |
| `COMBAT` | 20 | Combat transitions and positioning |
| `SCRIPT` | 20 | Script-owned entry, wait, and cleanup |
| `FOLLOW` | 16 | Follow target/leader policy |
| `COWER` | 13 | Civilian fear and shelter behavior |
| `FLEE` | 11 | Escape from threat |
| `DODGE` | 10 | Avoid an incoming threat/attack |
| `PATROL` | 10 | Path-based patrol behavior |
| `CRIMSUSP` | 6 | Criminal/suspicion response |
| `DISPOSITION` | 5 | Emotional/disposition-related action |
| `BLOCK` | 4 | Defensive blocking |
| `DIALOG` | 3 | Dialogue-linked control |

The library also contains start-combat, chase, ranged/melee attack, flank, reload/equip, feeding,
prone, animal, follower, boss, and class-specific schedules outside those simple prefixes.

### The incapacitation tasks and the NPC flag word

`CAI_BaseNPCTroika::StartTask` (`0x102a1910`) and `RunTask` (`0x102aacf0`) are two-level MSVC jump
tables Ghidra does not recover. `StartTask`'s dispatch at `0x102a1938` indexes `taskID - 5` through
byte table `0x102a7ab8` into dword table `0x102a77f8`; `RunTask`'s at `0x102aad03` indexes
`taskID - 2` through `0x102ac844` / `0x102ac760`. `[VtMB]`

| Task | ID | `StartTask` arm | `RunTask` arm |
|---|---:|---|---|
| `TASK_SET_NPC_FLAG` | `0x100` | `0x102a585d` | none |
| `TASK_CLEAR_NPC_FLAG` | `0x101` | `0x102a58ae` | none |
| `TASK_SET_MISC_FLAG` | `0x102` | `0x102a5886` | none |
| `TASK_MAKE_OBLIVIOUS` | `0x131` | `0x102a72e3` | none |

All four are `StartTask`-only: they perform their write and call `TaskComplete` (`0x10273e80`) on
the think that begins them. `[VtMB]`

#### `TASK_SET_NPC_FLAG` and the 62-name flag vocabulary

The operand `NPCFlag:<name>` is resolved at schedule-load time by the `strcmpi` chain `0x1030cbd0`,
which returns the bare mask for a word-one name and `0x80000000 | bit` for a word-two name; an
unknown name is an `Error`. The task arm routes on that sign bit into `m_bfAINPCFlags` (`+0x14b8`,
`0x102a97a0`) or `m_bfAINPCFlags2` (`+0x14bc`, `0x102a9800`). Word two's bit 31 is therefore the
routing marker, not a flag. `TASK_CLEAR_NPC_FLAG` is the exact mirror (`0x102a97d0` / `0x102a9830`).
`[VtMB]`

A separate generic flag applier pair, `0x101de6e1`/`0x101def10`, reads its mask from
`[obj+0xa8]`; it has zero recovered callers.

`m_bfAINPCFlags` (`+0x14b8`), bit 0 → 30:

`D_IS_BUSY`, `DO_STARTLED`, `AT_CROSSWALK`, `PRESERVE_PATH`, `FINDING_BODY`, `CARRYING_BODY`,
`NAV_IGNORE_NPC`, `IN_FLEE_SCHED`, `INITIAL_FLEE`, `COWER_PATH`, `COWERING`, `DODGING`,
`MADE_HUNT_PATH`, `AT_COVER_HINT`, `ANIM_MOVEMENT`, `DONE_EXTRAPOLATING`, `FORCE_RELAXED_ANIMS`,
`SLEEPING`, `BOTCHED_ATTACK`, `NO_DIALOG`, `SKIPPED_SOUND`, `LOOKED_AT_UNKNOWN`, `IGNORE_UNKNOWN`,
`ATTACK_UNKNOWN`, `MADE_INITIAL_RESPONSE`, `FINISHED_IGNORE_UNKNOWN`, `DONT_INVESTIGATE`,
`PLAYING_FACE_ANIM`, `FORCED_OCCLUDE`, `INTERESTING_INTO`, `ONE_HIT_KILL`.

`m_bfAINPCFlags2` (`+0x14bc`), bit 0 → 30:

`SLEEP_BOUNDING_BOX`, `FINISH_SPECIAL_NAV`, `SCHEDULE_CHANGED`, `INTERESTING_LOST`,
`TASKS_FACE_ENEMY`, `TASKS_FACE_TARGET`, `IGNORE_SQUAD_SEE_ENEMY`, `NO_UNKNOWN_ATTACK`,
`COVER_VS_MELEE_MODE`, `IGNORE_DOOR_FAILURE`, `MOVE_FACE_ENEMY`, `DISALLOW_TGT_DISCIPLINE`,
`MADE_OBLIVIOUS`, `SQUAD_NEW_ENEMY`, `DONT_FALL_TO_GROUND`, `DISABLE_BURST_FIRE`, `D_CALM`,
`D_INSANE`, `D_POSSESSED`, `D_MILDLY_CRAZY`, `D_FOLLOW`, `D_NIGHTMARE`, `D_AUTO_FEEDABLE`,
`D_DISCONNECT_SQUAD`, `D_WPN_HIDDEN`, `CHOOSE_NEW_SCHEDULE`, `NO_UNKNOWN_VISION`, `NOT_FEEDABLE`,
`NO_DIALOG_PERSISTENT`, `DISAPPEAR`, `ACTIVITY_COPY_PROP_CLEAN`.

Word one's bit order is independently confirmed by the AI debug overlay `0x1028d990`, which walks
bits 0–29 against the legend string `"RSCPFCNFIPCDHVAEFSBDSLIAMFDPOIO"` at `0x105d88b8`.

Every call site of the five generic flag accessors — all 16 — lies inside `StartTask`, so no hidden
writer of these bits exists outside the task vocabulary. `[VtMB]`

Consumers of the three bits the mesmerize program writes, each proved exhaustive against the field
ledger:

| Bit | Readers | Meaning |
|---|---|---|
| `D_IS_BUSY` (0) | **one**: `CBaseCombatCharacter::IsBusyWithDiscipline` (`0x1033e2b0`), whose entire body is that bit test | the bit *is* the predicate. Its 17 callers include `CAI_BaseNPCTroika::SelectSchedule` (`0x102af660`), the dialogue gate (`0x102c21c0`) and all three `StartPlayerDialog` inputs — so a flagged NPC will not be re-targeted by a discipline, will not start dialogue, and is refused an ordinary schedule |
| `NO_DIALOG` (19) | **two**, both the same virtual slot 295: `0x102c21c0` and the `CPayphone` override `0x101aaee0` | one link of the "can the player talk to me" chain, beside `m_iDialog`, `IsUnconscious`, `m_bWillTalk`, `IsBusyWithDiscipline` and `NO_DIALOG_PERSISTENT`. The per-schedule form of dialogue suppression |
| `DONT_INVESTIGATE` (26) | **one**: `0x102b3270`, whose first line rejects on `DONT_INVESTIGATE \| IN_FLEE_SCHED` | the per-candidate interest predicate ("The interest predicate" below), asked by the see-unknown sweep, the sound sweep and the vision producer before they raise `COND_INVESTIGATE_SIGHT`/`_SOUND`. It does NOT gate `SEE_HATE`/`SEE_FEAR`, which the sense pass raises and `m_iIsOblivious` gates |

#### `TASK_MAKE_OBLIVIOUS` and `m_iIsOblivious`

The operand is a float: the schedule compiler writes `TRUE`/`ON` → 1.0 and `FALSE`/`OFF` → 0.0
(`0x1030e65f`), and the arm compares against 0.0 exactly. All 35 shipped operands are `TRUE`; the
clear branch (`0x102a731a`) is dead in shipped data. `[VtMB] [script/data]`

The TRUE arm (`0x102a72f5`) does four things in order: sets `MADE_OBLIVIOUS`; calls `0x1026d130`,
which is `SetEnemy(NULL)` + squad disconnect (`0x1026d050`) + `++m_iIsOblivious`; fires
`OnIncapacitatedStart` (`+0x5fd4`); and completes. The FALSE arm is its exact mirror, ending in
`OnIncapacitatedEnd` (`+0x5fec`). `[VtMB]`

**`CAI_BaseNPC::m_iIsOblivious` (`+0x5bb4`) is an `int` refcount, not a boolean** — retail nests its
sources, so a body oblivious for two reasons stays oblivious when one ends. Its increment sites are
the scripted-scene starts (`CCineNPC`/`CCineAI`/`CCineAISchedule` `vfunc583`), grapple entry
(`0x1026cdc0`), fed-upon begin (`0x1026cec0`) and this task. `MADE_OBLIVIOUS` itself has **zero
readers anywhere in the binary**; it is pure bookkeeping so the schedule-change clear knows a
decrement is owed. All behaviour hangs off the counter, which has exactly four consumers:

| Consumer | Effect |
|---|---|
| `CAI_BaseNPC::PerformSensing` (`0x1026e4f0`) | `if (m_iIsOblivious < 1)` gates the whole sense pass — an oblivious body takes in **no sight, sound or scent at all** |
| `CAI_BaseNPCTroika` slot 587 (`0x1028ef20`) | reject rung in a reaction predicate |
| `CAI_BaseNPCTroika` slot 314 (`0x102bf070`) | clears `m_bAimWeaponAtTarget` and zeroes the aim pose params — the body stops aiming |
| `CStealthKillRules::FindVictim` (`0x101be1f0`) | an oblivious body is **stealth-killable from any angle**, bypassing the behind/deaf-arc test |

#### Nothing in schedule data clears these bits — the schedule *change* does

Across all 38 `TASK_CLEAR_NPC_FLAG` operands in the image, `D_IS_BUSY` and `NO_DIALOG` are never
cleared, and `DONT_INVESTIGATE` only once (`SCHED_TROIKA_SHOT_BY_UNKNOWN`). The release is
structural: `SetSchedule` → `ForceScheduleChange` (`0x102ae490`) → **virtual slot 435**, which for
all 60 classes of the Troika hierarchy is `CAI_BaseNPCTroika::OnScheduleChange` (`0x102a0940`):

```c
m_bfAINPCFlags2 |= SCHEDULE_CHANGED;
if (!(m_bfAINPCFlags & PRESERVE_PATH)) {
    ... navigator / motor / goal reset ...
    uVar1 = m_bfAINPCFlags2;
    m_bfAINPCFlags  &= 0xbbf4b97e;      // ~0xbbf4b97e == 0x440b4681
    m_bfAINPCFlags2 &= 0x77fff14f;
    if (uVar1 & MADE_OBLIVIOUS) { m_bfAINPCFlags2 = uVar1 & 0x77ffe14f; UnOblivious(this); }
}
m_bfAINPCFlags2 &= 0x3fffffff;
m_bfAINPCFlags  &= 0xd7ffffff;
```

`0x440b4681` is `D_IS_BUSY`, `IN_FLEE_SCHED`, `COWER_PATH`, `COWERING`, `ANIM_MOVEMENT`,
`FORCE_RELAXED_ANIMS`, `SLEEPING`, `NO_DIALOG`, `DONT_INVESTIGATE`, `ONE_HIT_KILL`. So an
incapacitating schedule needs no teardown tasks: **the next schedule the NPC is given unwinds it
completely and symmetrically**, and the same virtual ran when the program was installed, which is
why its tasks always write onto a cleared word. `[VtMB]`

A retail refcount leak, **bounded**: virtual slot 448 is **`CAI_BaseNPC::TaskFail(const char*)`**
(`0x10273fc0` — the `"TaskFail -> %s"` DevMsg and `SetCondition(COND_TASK_FAILED)`; Troika override
`0x1029adb0`). The override applies `m_bfAINPCFlags2 &= 0x7fffe24f` (listing `0x1029aeb2`),
clearing `MADE_OBLIVIOUS` without decrementing `m_iIsOblivious`, where its sibling
`OnScheduleChange` uses `0x77fff14f` and decrements on the kept bit. Every writer of `+0x5bb4` is
the inc/dec pair; the field is `FTYPEDESC_SAVE` (`0x105c9cf0`), so a leak survives a save. Verified
by two independent adversarial recoveries. **Reachability is one corner:** of every task following
`TASK_MAKE_OBLIVIOUS` in the 35 shipped schedules, only `TASK_STOP_MOVING` can fail in practice —
`FAIL_STUCK_ONTOP` at `0x10288963`, requiring an active nav goal, `NAV_JUMP`, not on ground and
near-zero velocity — i.e. an NPC wedged mid-air at the moment a discipline forces the schedule
(`CAI_Navigator::OnNavFailed` `0x102eeae0` is a second source). `SCHED_TROIKA_MESMERIZED` carries
no `TASK_STOP_MOVING` and cannot leak. `TASK_SET_ACTIVITY` (`0x102a1c0f`) has no fail arm. A
leaked NPC looks normal (TaskFail's flags1 mask clears `D_IS_BUSY`/`NO_DIALOG`/`DONT_INVESTIGATE`)
but never senses again and is stealth-killable face-on — a silent failure, consistent with the
shipped game. `[VtMB] [script/data]`

#### `TaskFail` and stopped special navigation, walked (2026-09-08)

Slot 448 is `CAI_BaseNPCTroika::TaskFail` `0x1029adb0`, chaining to the base
`0x10273fc0`. The integer reasons occupy the first 42 entries of the pointer-shaped argument:
`FUN_10316fa0` resolves values `< 0x2a` through the pointer table at `0x106152b0`; larger values
are already text pointers. A raw PE read confirms all 42 strings, including the literal holes
`FAIL_CODE_10`, `FAIL_CODE_20`, `FAIL_CODE_30`, and `FAIL_CODE_40`. Selected identities: `0x05`
"Schedule not found", `0x06` "Don't have an enemy", `0x0c` "Don't have a route", `0x17`
"No player", `0x1c` "Stuck on top of something", `0x29` "NPC had no follower boss".

The override's complete transaction, before the base failure condition:

1. `0x102b53d0` releases the interesting-place visit. Keep `PRESERVE_PATH` only when the
   navigator currently reports `NAV_CLIMB (3)` or `NAV_JUMP (1)`; this step never sets the bit.
2. Motor `+0x1c := 180.0` through `0x102e0a60`; desired move yaw zero; all four next-think
   stamps equal current time. Goal tolerance, both squared interrupt distances and interrupt time
   zero; clear the move-target handle.
3. When the kick-prop handle resolves, write its `m_bNpcKickable (+0x788) := false`, then
   invalidate the handle. This consumes an authored kickable prop; it is not a balanced claim.
   An already invalid handle takes no branch.
4. Clear memory bit `0x2000`; apply `flags2 &= 0x7fffe24f`, `memory &= 0x0fffffff`,
   `flags1 &= 0xa3f40178`. `MADE_OBLIVIOUS` is lost without calling `UnOblivious`: neither
   obliviousness nor squad-disconnect refcount is decremented.
5. If the old flags2 word carried `SLEEP_BOUNDING_BOX`, call slot 15, **SetAttackExtents**
   (`0x1009af40`), with `m_vecSavedAttackExtents`; set that saved vector to `(-1,-1,-1)` and
   clear the sleep bit. `SetAttackExtents` updates the attack partition through collision's
   `0x100dc220` and entity `+0x50..58`; it does not set the movement hull. The vector is an
   **additive margin**, not an absolute half-size: engine `CSpatialPartition::vfunc0`
   `0x20040fc0` stores it at each partition record's `+0x28..30`; attack-aware box/ray
   enumeration (`0x200426e0`, `0x20042b70`) tests `[collisionMins-margin, collisionMaxs+margin]`.
   Zero components are valid. `SetAbsoluteAttackExtents` `0x1009b060` explicitly subtracts
   the collision half-size before calling this setter.
6. Clear `m_fSavePositionWalk`; `ClearHintNode(5.0)` (`0x10295ab0`) only acts when a hint exists.
   The hint's owner test (`0x102d1450`) gates its unlock and `nextUse := now+5`
   (`0x102d1420`). The NPC then loses its hint pointer, failed-cover-LOS count and
   `AT_COVER_HINT`, and resets saved attack extents to `(-1,-1,-1)`.
7. Clear motor `+0x28`, NPC `+0x6300`, `+0x659c`, and `m_bPatrolPathUseHint`. The base then
   clears `m_bShouldMove`, writes the supplied reason to `+0x5c50`, and sets
   `COND_TASK_FAILED (0x5c)`. `MaintainSchedule` zeros the reason when the next task starts.

**`TASK_STOP_MOVING` is two distinct arms.** Base StartTask's two-table dispatch maps id `0x69`
to `0x10282d71` (`0x10287138[id-1]`, then `0x10286f8c`). No active goal means clear
`m_bShouldMove` and complete. An active goal calls navigator ClearGoal (`0x102ee270`), resets
the `move_yaw` pose parameter if present, and remains running; ClearGoal does not change the
navigator's type. MaintainSchedule can invoke RunTask immediately in that same iteration.

RunTask's arm at `0x102888d4` reads navigation type independently of the cleared goal. Jump on
ground becomes Ground. Jump in the air with speed `> 0.01` Source units/s keeps running; at
`<= 0.01`, it **sets Ground before TaskFail(0x1c)** (`0x10288940..63`). Climb keeps running.
The remaining path selects the arrival activity, clears `m_bShouldMove`, and calls
TaskComplete(false), which cannot overwrite an already set TASK_FAILED condition. Therefore this
particular stuck-jump failure clears `PRESERVE_PATH`; a navigator-delivered TaskFail while its
type is still Jump/Climb retains the bit. The active-goal requirement is at StartTask admission,
not at each RunTask probe.

`NextScheduledTask` `0x10280f40` raises `COND_SCHEDULE_DONE (0x5d)` when incrementing beyond the
last task. `MaintainSchedule`'s ten-iteration bound exits through `0x102821ae`, retaining task
position and setting `m_bDidMaintainSchedule`; it does not discard a program at the bound.
`SetSchedule` `0x10280e50` invokes the outgoing schedule-change virtual before replacing its
task state, so callbacks can still inspect and complete the outgoing program.

The sibling `OnScheduleChange` `0x102a0940` sets flags2 `|= 0x80000004`, then performs its
conditional movement/mask/refcount/extents cleanup under `!PRESERVE_PATH`. Only afterwards it
tests the surviving `ACTIVITY_COPY_PROP_CLEAN (0x40000000)`, interrupts discipline effects
when `DAT_10739a64` permits, removes `activity_copy_prop` rows whose owner handle at `+0x730`
resolves to this NPC (`0x1018e910`), and clears invincibility. Finally it applies unconditional
`flags2 &= 0x3fffffff`, `flags1 &= 0xd7ffffff`, and clears memory bit `0x2000`.
`UnOblivious` (`0x1026d160`) always calls ReconnectToSquad after its clamped decrement;
Reconnect (`0x1026d0c0`) decrements with a floor at zero and clears D_DISCONNECT_SQUAD.

#### The three cached downcasts

`CAI_BaseNPC`'s constructor (`0x1027c300`) writes `this` at `+0x94`; `CAI_BaseNPCTroika`'s
(`0x1028d230`) at `+0x98`; `CBaseCombatCharacter`'s (`0x10326de0`) at `+0x9c`. None is a datamap
member. So the `+0x98` that `FeedInterrupt`, `IsScheduleValid`, `MaintainSchedule` and
`CStealthKillRules::FindVictim` dereference is the entity's own `CAI_BaseNPCTroika*` — null for
anything that is not a Troika NPC, and `this` for one. `IsScheduleValid`'s two arms through it are
therefore self-writes: during `NAV_CLIMB`/`NAV_JUMP` the interrupt mask is bypassed outright and a
done/failed task sets `PRESERVE_PATH | FINISH_SPECIAL_NAV`; otherwise a set `CHOOSE_NEW_SCHEDULE`
(flags2 `0x02000000`) is consumed and the schedule invalidated — an external "reselect now" request
bit. `[VtMB]`

#### `BuildScheduleTestBits` — the per-NPC interrupt overlay, decoded

`CAI_BaseNPCTroika::BuildScheduleTestBits` (`0x102ad140`), run by `CacheInterruptConditions` every
think on top of the schedule's authored mask:

```c
CAI_BaseNPC::BuildScheduleTestBits();                       // 0x10280fb0, empty
if (!(m_bfAINPCFlags & (DONT_INVESTIGATE | IN_FLEE_SCHED))) {
    if (!IsBusyWithDiscipline() && !(m_bfAINPCFlags2 & D_POSSESSED)) {
        if (!m_pHintNode || m_pHintNode->type != 0x2774) {
            add COND_INVESTIGATE_LEVEL (0x1e), COND_CRIMINAL_FLEE_LEVEL (0x1f),
                COND_SUPERNATURAL_FLEE_LEVEL (0x21);
        }
        if (GetEnemy() == NULL) {
            if (m_bfNPCStateFlags & 0x10) add COND_HEAR_FLINCH (0x72);
            if (m_bfNPCStateFlags & 0x20) add COND_CRIMINAL_ATTACK_LEVEL (0x20),
                                              COND_SUPERNATURAL_ATTACK_LEVEL (0x22);
        }
        if (!(m_bfAINPCFlags & COWERING)) add COND_COMFORT (0x27);
    }
}
if (m_bfAINPCFlags2 & IGNORE_SQUAD_SEE_ENEMY) remove COND_SQUAD_SEE_ENEMY (0x31);
// CacheInterruptConditions itself then always adds COND_NPC_FREEZE (0x75).
```

`m_bfNPCStateFlags` is a per-state capability byte written on every state change by `0x1026e3e0`:
idle `0x31`, alert `0x39` (bits 4 and 5 set), combat `0x8f` (bits 4 and 5 clear), script `0x8`,
the two flee states `0x85`/`0x7f`. So **in idle and alert with no enemy, all four law conditions
plus `HEAR_FLINCH` are interrupts on every schedule; in combat only the two flee levels.** That is
the decoded rule behind the four law conditions the port's idle mask used to carry with a CHOSEN
mark; the overlay is now ported on the runner (`FElysiumNpc::BuildScheduleTestBits`) and the
chosen entries are gone — with one correction the decode brought, that the attack levels are gated
on "no committed enemy". It also shows the overlay is suppressed by the very flags
`SCHED_TROIKA_MESMERIZED` sets, so that program's effective mask is exactly its authored one. `[VtMB]`

Condition ordinals named on the way, from the registrar `0x102c8ce0` (global space via
`thunk_FUN_102ea130`, `CAI_BaseNPC`-local via `thunk_FUN_102beae0`): `COND_INVESTIGATE_LEVEL` 0x1e,
`COND_CRIMINAL_FLEE_LEVEL` 0x1f, `COND_CRIMINAL_ATTACK_LEVEL` 0x20, `COND_SUPERNATURAL_FLEE_LEVEL`
0x21, `COND_SUPERNATURAL_ATTACK_LEVEL` 0x22, `COND_COMFORT` 0x27, `COND_SQUAD_SEE_ENEMY` 0x31,
`COND_TASK_FAILED` 0x5c, `COND_SCHEDULE_DONE` 0x5d, `COND_HEAR_FLINCH` 0x72, `COND_NPC_FREEZE`
0x75. `[VtMB]`

#### `NO_DIALOG_PERSISTENT`'s producers

Two schedules and no code: `SCHED_TROIKA_D_AFRAID` and `SCHED_TROIKA_D_POSSESSION` set it with
`TASK_SET_NPC_FLAG`. `NPCFlag:D_IS_BUSY` is authored by 20 schedules, every one a `D_*` discipline
effect or `SWAT_INSECTS`/`MESMERIZED` — the bit is literally "busy with a discipline effect", which
is what `IsBusyWithDiscipline`'s name says. `[script/data]`

### `MaintainSchedule`, walked

`CAI_BaseNPC::MaintainSchedule` (`0x102817c0`) has three `ret` sites and **one** store of
`m_bDidMaintainSchedule` (`mov byte [esi+0x5bb8], 1` at `0x10282342`, on the common exit
`LAB_102821ae`). The two exits that skip it are harmless: the task-complete early return at
`0x10282269` is gated on the `ai_step` console mode bit (`DAT_1092053c & 2`, set only by
`0x10085830`) and unreachable in a shipping session; the `"ERROR: Missing or invalid schedule!"`
return at `0x10282336` forces `SetActivity(ACT_IDLE)` (slot 310) and is reachable only after the
loop's own `GetNewSchedule`/`SetSchedule` has already re-armed the flag or left no schedule at all.
So the one-think `DELAY_INTERRUPTS` window never silently extends. `[VtMB]`

The loop bound at `0x1028190e` is **10** when the `bool` argument is 0 and **1** when it is set.
The loop continues only while tasks keep completing (`cmp [esi+0x5c44], 4` at `0x1028212e`) — it
is a cap on task completions per think, not a spin guard — and also exits on `TaskIsRunning()`
false, `COND_TASK_FAILED`, or an RDTSC time budget. `IsScheduleValid` is called **inside** the loop
at `0x102819d5`, once per iteration, its argument recomputed as `!m_bDidMaintainSchedule` each time;
its `false`, or `m_NPCState != m_IdealNPCState`, drops into the reselect block. `[VtMB]`

**A reduced-think mode, previously unrecorded.** `CAI_BaseNPC::RunAI(bool)` (`0x1026f110`)
forwards that argument: when set it **skips `GatherConditions` entirely** (slot 433), passes the
bound of 1, and skips the end-of-pass clear of `COND_LIGHT_DAMAGE`, `COND_HEAVY_DAMAGE` and
`COND_WAS_BUMPED`. `CAI_BaseNPC::NPCThink` always passes 0; `CAI_BaseNPCTroika::NPCThink`
(`0x10292de0`) passes `!(m_flNextAIThink − curtime < frametime)` — so a Troika NPC whose
`m_flNextAIThink` (`+0x6250`) is not yet due still thinks, but gathers nothing and maintains one
task. Sibling cadences: `m_flNextUpdateThink` `+0x6244`, `m_flNextNormalThink` `+0x6248`. The port
does not model this mode. `[VtMB]`

### Two Troika virtuals, identified

**Slot 314 (`+0x4e8`) is `UpdatePoseParameters(float flInterval)`** — the authored name is a
ScopeTrace literal at `0x1053ede0` on the base `CBaseCombatCharacter::UpdatePoseParameters`
(`0x10054060`), and `CAI_BaseNPCTroika::0x102bf070` is its override, tail-calling the base. Target =
`m_hShootTargetOverride`, else slot 167; no target **or `m_iIsOblivious > 0`** → the null arm
(`m_bAimWeaponAtTarget = 0`, yaw/pitch zeroed); otherwise the aim offset to the target's eye
position, own angles subtracted, clamped to ±45°, then approached into the pose parameters with the
shaky-hands offset. `[VtMB]`

**Slot 587 (`+0x92c`) has no Source ancestor**: `vftable_CAI_BaseNPC` ends at slot 582, and the
`CAI_BaseNPCTroika` and `CAI_BaseHumanoid` tables extend past it independently, so their slot-587
entries are different virtuals sharing an offset. The Troika body (`0x1028ef20`) has one consumer:
`FUN_10181be0`, a `CBasePlayer` method armed only for the `Player_Nosferatu` template, which
enumerates NPCs in a 512-unit sphere and records the nearest one for which this returns true at
`player+0x1ED0` — networked for the client's Masquerade warning. The body's clauses are the
predicate half of the player-law transaction `0x1028efc0`: reject Kindred, `m_iDialog != 0`,
oblivious, `m_bfNPCFrenziedFlags & 0x10`, `IsBusyWithDiscipline`, then true only when a supernatural
threshold is below 3. Project name `CanWitnessSupernatural()`; the authored spelling is
unrecoverable from this image. TVs, animals, bosses, placeholders and makers all override it to
`return 0`. `[VtMB]`

## The kernel's failure route and the base programs, walked (2026-09-12, story 25)

**Correction: the base programs are text blobs after all.** The claim above that "the base
(`CAI_BaseNPC`) programs are not text blobs in `vampire.dll`" is wrong. `CAI_BaseNPC`'s loader is
`FUN_102cb690`, called through the slot-452 `LoadedSchedules` virtual that `Precache`
(`0x1027bb50`) dispatches (a false return is `"ERROR: Rejecting spawn of %s as error in NPC's
schedules"`). It feeds 63 blobs to the same parser (`0x1030d850`, class name `"CAI_BaseNPC"`)
through a **pointer table** at `0x106034b8..0x106035b4` — which is why the byte scan for
`"\n\tSchedule\n\t\t"` immediately followed by a registrar `call` missed them — and the blob
names carry **no `SCHED_` prefix** (`IDLE_STAND`, `FAIL`, `COWER`, `DIE`…: the 62 "bare names"
the flee section counted). The base name↔id registrar is `FUN_102cadd0` (`AddSymbol` per pair),
and **the debug name table `0x105d1488` is stale**: it lacks `IDLE_PATHCORNER` (id 3) so every
name after `SCHED_IDLE_WALK` sits one slot early (`0x105d1488[0x43]` reads
`SCHED_TROIKA_IDLE_STAND`, `[0x42]` `SCHED_FAIL`). Never number from it. The registrar's own
numbering, verbatim: `1 IDLE_STAND, 2 IDLE_WALK, 3 IDLE_PATHCORNER, 4 IDLE_WANDER, 5 WAKE_ANGRY,
6 ALERT_FACE, 7 ALERT_SMALL_FLINCH, 8 ALERT_SCAN, 9 ALERT_STAND, 10 INVESTIGATE_SOUND, 0xb
COMBAT_FACE, 0xc COMBAT_SWEEP, 0xd FEAR_FACE, 0xe COMBAT_STAND, 0xf CHASE_ENEMY, 0x10
CHASE_ENEMY_FAILED, 0x11 VICTORY_DANCE, 0x12 TARGET_FACE, 0x13 TARGET_CHASE, 0x14 SMALL_FLINCH,
0x15 BACK_AWAY_FROM_ENEMY, 0x16 BACK_AWAY_FROM_SAVE_POSITION, 0x17 TAKE_COVER_FROM_ENEMY, 0x18
TAKE_COVER_FROM_BEST_SOUND, 0x19 TAKE_COVER_FROM_ORIGIN, 0x1a FAIL_TAKE_COVER, 0x1b
RUN_FROM_ENEMY, 0x1c ESTABLISH_LINE_OF_FIRE, 0x1d FAIL_ESTABLISH_LINE_OF_FIRE, 0x1e COWER, 0x1f
MELEE_ATTACK1, 0x20 MELEE_ATTACK2, 0x21 RANGE_ATTACK1, 0x22 RANGE_ATTACK2, 0x23 SPECIAL_ATTACK1,
0x24 SPECIAL_ATTACK2, 0x25 STANDOFF, 0x26 ARM_WEAPON, 0x27 DISARM_WEAPON, 0x28 HIDE_AND_RELOAD,
0x29 RELOAD, 0x2a AMBUSH, 0x2b DIE, 0x2c SCHED_DIE_RAGDOLL, 0x2d WAIT_FOR_SCRIPT, 0x2e AISCRIPT,
0x2f SCRIPTED_WALK, 0x30 SCRIPTED_RUN, 0x31 SCRIPTED_CUSTOM_MOVE, 0x32 SCRIPTED_WAIT, 0x33
SCRIPTED_FACE, 0x34 SCENE_SEQUENCE, 0x35 SCENE_WALK, 0x36 SCENE_FACE_TARGET, 0x37 NEW_WEAPON,
0x38 GIVE_WAY, 0x39 FORCED_GO, 0x3a NPC_FREEZE, 0x3b PATROL_WALK, 0x3c PATROL_RUN, 0x3d
RUN_RANDOM, 0x3e FALL_TO_GROUND, 0x3f/0x40/0x41 DROPSHIP_DEPLOY_FIRST/SECOND/THIRD, 0x42
SCHED_FLINCH_PHYSICS, 0x43 FAIL`. So the base `COWER` (`0x10604f98`) the flee section left
unnumbered is **0x1e**, `NPC_FREEZE` is the `0x3a` and `FALL_TO_GROUND` the `0x3e` that
`GetSchedule` returns, and `SCHED_DIE` is `0x2b` / `SCHED_DIE_RAGDOLL` `0x2c`.

**`0x43` is `FAIL`**, and its list (blob `0x10608238`) is:

    FAIL: TASK_STOP_MOVING 0; TASK_SET_ACTIVITY ACT_IDLE; TASK_WAIT 1; TASK_WAIT_PVS 0
      Interrupts COND_CAN_RANGE_ATTACK1 COND_CAN_RANGE_ATTACK2 COND_CAN_MELEE_ATTACK1
                 COND_CAN_MELEE_ATTACK2 COND_GIVE_WAY

**`Idle_Stand`** (the fail schedule the comfort, calmed, follow, disoriented and interesting-place
programs name) is base **1**, blob `0x106080b0`. Naming it is not running it: the fail route's id
goes through slot 440 before the lookup (below), and Troika's table (`0x102b12f0`, `case 1: case
0x6b:`) sends it to **`0x6b IDLE_DISPOSITION`** — or `0x132 LAUGHING` under `D_MILDLY_CRAZY` — so
on every Troika class those programs fail into the disposition idle, and base `IDLE_STAND` runs
only through `SetSchedule(int)`'s miss arm, whose literal 1 (`0x102cc229`) skips the translation:

    IDLE_STAND: TASK_STOP_MOVING 0; TASK_SET_ACTIVITY ACT_IDLE; TASK_WAIT 5; TASK_WAIT_PVS 0
      Interrupts COND_NEW_ENEMY COND_SEE_FEAR COND_LIGHT_DAMAGE COND_HEAVY_DAMAGE COND_SMELL
                 COND_PROVOKED COND_GIVE_WAY COND_HEAR_PLAYER COND_HEAR_DANGER COND_HEAR_COMBAT
                 COND_HEAR_BULLET_IMPACT

and **`GIVE_WAY`** (base `0x38`, the `COND_GIVE_WAY 0x68` consumer in the base idle selector) is
`SET_ROUTE_SEARCH_TIME 0.5; SET_TOLERANCE_DISTANCE 5; GET_PATH_TO_SAVEPOSITION 2; RUN_PATH_TIMED
2.0; WAIT_FOR_MOVEMENT`, interrupts `GIVE_WAY WAY_CLEAR NEW_ENEMY SEE_FEAR LIGHT_DAMAGE
HEAVY_DAMAGE SMELL PROVOKED`. `NPC_FREEZE` (`0x3a`) is `TASK_FREEZE 0` interrupted by
`COND_NPC_UNFREEZE`; `FALL_TO_GROUND` (`0x3e`) is `TASK_FALL_TO_GROUND 0`, no interrupts.

**The route, in `MaintainSchedule` (`0x102817c0`).** Each iteration first asks `IsScheduleValid`
(`0x10280ff0`, with `!m_bDidMaintainSchedule` as its `DELAY_INTERRUPTS` argument). When it says
no, or `m_NPCState != m_IdealNPCState`: `0x10281430` decides whether the state change clears the
schedule (`0x1026f4d0`); the `HIT_BY_DOOR`-style flags2 bit `0x200` on the Troika pointer is
consumed against `m_hBlockedDoor +0x5d28` (an expired door is dropped, a live one sets a "door
blocked" local); then **`HasCondition(COND_TASK_FAILED 0x5c)` with the state unchanged and no door
block takes the fail route** — `0x10281730`: current local schedule id (slot 447), current task
(`0x1028a150` = `&m_pSchedule->tasks[m_iCurTask]`, 8 bytes each), then **slot 439
`GetFailSchedule(curSched, curTask, m_failSchedule)`** — `0x1028abe0` on all 79 classes, no
override: `m_failSchedule (+0x5c54) ? m_failSchedule : 0x43` — then the selector trace
(`+0x1b2c = 1`, file/line `AI_BaseNPC_Schedule.cpp:682`) and **`SetSchedule(int)` `0x102cc1f0`**:
**slot 440 `TranslateSchedule`** on the answer, then slot 446 `GetScheduleOfType` (`0x102cc260` →
`g_AI_SchedulesManager.GetScheduleFromID` `0x1030f300`, a linked-list walk on `sched+0x1c`); a
miss DevMsgs `"GetScheduleOfType(): No CASE for %d"` and pushes the **literal 1 `IDLE_STAND`**
(`0x102cc229`) straight to slot 446, untranslated. Slot 440 is `CAI_BaseNPCTroika` `0x102b12f0`
on 62 classes (`case 1: case 0x6b:` → `0x6b`, or `0x132` under flags2 `0x80000 D_MILDLY_CRAZY`;
`2 → 0x46`, `3 → 0x47`, `6 → 0x4a`, `0xf → 0xb1`, `0x10 → 0xb7`, `0x15 → 0xb8`, `0x21/0x22 →
0xed/0xee`, `0x25 → 0xc1`, `0x28 → 0xc2`, `0x2f..0x33 → 0xf2/0xf4/0xf6/0xf8/0xf9`, `0x77 → 0x78`
on a `0x2774` hint, `0x94/0x96 → 0x95/0x97` by slot 293; a frenzied pre-table `0x102b11c0` under
`m_bfNPCFrenziedFlags & 0x100`: `0xc7 → 0xc9`, `0xca/0xcb/0xd1/0xd2 → 0xcc`, `0xef → 0xf0`,
`0x87/0x88 → 0x7d/0x7e`; everything else to base `0x102cc080`, which only splits `0x2e AISCRIPT`)
and is overridden by twenty species classes (`vtmb_slot 440`; `CNPC_VWerewolf 0x103d5e00` has an
arm on `0x43` itself) — story 25b. **So a program whose fail schedule is `Idle_Stand` never runs
base `IDLE_STAND` on a Troika NPC; it runs `0x6b`.** Every other invalidity (a state change, a
door block, no `TASK_FAILED`) goes to `SetIdealState` + `GetNewSchedule` (`0x102814d0` →
`0x1028a260`, story 26's pre-selector) instead. The new program installs through `SetSchedule`
(`0x10280e50`: `OnScheduleChange` slot 435 on the OLD schedule, `m_bDidMaintainSchedule = 0`,
`m_pSchedule`, `m_iCurTask = 0`, `m_timeStarted`/`m_timeCurTask = curtime`, `fTaskStatus = 0`,
**`m_failSchedule = 0`**, the 192 condition bits zeroed, the navigator cleared unless
`NAV_CLIMB`/`NAV_JUMP` or `PRESERVE_PATH`) and the same `do…while` keeps running it: the failure
route costs no think beyond the one the failure ended.

**When the route runs, relative to the failure.** A task that fails inside the loop does NOT get
its route on that pass. `TaskFail` (`0x10273fc0`) writes the reason to `+0x5c50`, sets `0x5c` and
leaves the status word `+0x5c44` alone (only `TaskComplete 0x10273e80` = 4 and
`TaskMovementComplete 0x10273ec0` write it), so `0x10273f90` still answers "running"; after
`StartTask` or `RunTask` the loop tests `HasCondition(0x5c)` and jumps to `0x102821ae` — the one
store of `m_bDidMaintainSchedule = 1` — with the failed program still installed. The route runs at
the top of the NEXT `MaintainSchedule`, where `IsScheduleValid` answers no for `0x5c`, and only
if the state is still its ideal and no door blocks; a state change in between reselects instead.
The loop also handles `m_pSchedule == NULL` (a `GetNewSchedule` + install) and an installed
schedule with zero tasks (`"ERROR: Missing or invalid schedule"`, then `SetState(1)` through slot
`0x4d8`) — story 25c.

**`ClearSchedule` (`0x10280d30`)** is the other exit. It zeroes six words, in this order:
`+0x5c48` timeStarted, `+0x5c4c` timeCurTaskStarted, `+0x5c44` fTaskStatus, **`+0x5c3c`
`m_IdealSchedule`** (the datamap name; the earlier reading "the `sched+0x1c` id" is wrong),
`+0x5c38` `m_pSchedule`, `+0x5c40` iCurTask. Through `this+0x98` it clears `m_bfAINPCFlags
+0x14b8` bit `0x8` `PRESERVE_PATH` and dispatches slot 435 with the now-NULL program; both are
skipped when `+0x98` is NULL. `taskFailureCode +0x5c50`, `m_failSchedule +0x5c54`, `+0x5c58` and
every condition word survive. _Recovered 2026-09-13, story 25a._

**What the loop does after a clear.** The next pass never goes straight to the `m_pSchedule ==
NULL` arm (`0x10281c1d`); it asks `IsScheduleValid` (`0x10280ff0`) first, which answers no at once
for a NULL program, so the **invalid arm** runs: the fail route `0x10281730` when `TASK_FAILED`
is still set (the clear kept it) with the state unchanged and no door block — the kept
`m_failSchedule` is honoured — otherwise `SetState(ideal)` `0x1026e340` + `GetNewSchedule`
`0x102814d0` + `SetSchedule`. Three timings:
- **From `StartTask` (slot 442):** status is 0 and `TaskIsRunning` `0x10273f90` counts 0 as
  running, so slot 445 still fires on the cleared program, then MaintainActivity; step 7 is skipped
  on status 0; the pass-cap/budget test at `0x10282179` decides. If another pass is allowed (cap
  10, or 1 under `RunAI(param=1)`; budget `0x10923c68`) the invalid arm and the new program's task
  0 run in the **same think**; otherwise next think.
- **From `RunTask` (slot 444):** slot 529 and `0x10289c90` still run; status 0 != 4 exits at
  `0x102821ae` (`m_bDidMaintainSchedule = 1`); reselect **next think**.
- **Outside the think** (grapple, restore, console): the next `RunAI` `0x1026f110` gathers, then
  the invalid arm runs and task 0 starts in that same think.

**The twelve direct callers** (`thunk 0x10006a8c`; `0x10280d30` has none of its own):

| Caller | Trigger | The clear and what surrounds it |
|---|---|---|
| `NPCInit` base slot 420 `0x10273390` | spawn (slot 103 → `+0x690`) | Unconditional. `m_IdealNPCState = 1` … **clear**, navigator `ClearGoal 0x102ee270`, hint = 0, `m_afMemory = 0`, `SetEnemy(NULL) 0x10279a50`, `m_Conditions +0x5c5c` zeroed (the six words `HasCondition 0x10269aa0` reads), both delayed-condition lists cleared (`0x102cc7e0`), eye offset `0x10274ca0`. Troika `0x1029a0b0` sets `m_NPCState = 0` then calls base; every NPC class reaches it (13 classes hold base directly, ~19 overrides chain to Troika, the rest chain through `0x10387140` / `CNPC_VVampireBoss` / `CNPC_VPlayerController`). `CNPC_VCamera(Security) 0x103692c0` is an inlined copy that does not clear the delayed lists. No NPC class skips the clear. |
| Troika `EnterGrappleState` slot 379 `0x102b5c00` | `StartGrappleAttack 0x10328df0`: attacker site `0x1032928a` (an NPC for mode 8, and mode 2 with an NPC seducer), victim site `0x103292d8` (modes 0/6, 3, 2) | (1) If the queued-burn list `+0x65a8` (count `+0x65b4`, 0x4c-byte `CTakeDamageInfo` copies) is non-empty: each record gets attacker = inflictor = this and hitbox `RandomInt(0,1) ? 4 : 5` (`0x101c2a10` is the hitbox setter), is applied to the partner, and the call **returns false** with the list intact. The only producer is `CreateDamageEffects 0x10330d00` (from `OnTakeDamage_Alive 0x103302e0`): a burn hit (type bit `0x8`, hitbox > 0, `!ON_FIRE 0x30 && curtime > m_flNextBurnTime +0x65bc` through slot 615 `0x102ad0c0`) is copied there, `0x151 TROIKA_ONFIRE` forced (`0x102ae750`), and the hit deferred; `TASK_ON_FIRE_LOOP 0x9d` drains it, then slot 616 `0x102ad110` clears `ON_FIRE` and re-arms `+0x65bc = curtime + 15`. (2) Base `0x1026cdc0`, **not gated on grapple type or role, always true**: `0x1026d130` (`SetEnemy(NULL)`, squad disconnect, `m_iIsOblivious++`), `m_OnGrappleBegin +0x5bd8`, `CBaseCombatCharacter 0x10329760`. (3) `IsInDialog 0x102c1170` (`m_bIsTalking +0x64c0` ∥ `m_szDialogQue +0x64ec` ∥ `m_hDialogPartner +0xfe8` ∥ the speech-scene handle `+0x6554`) → `0x102c0bb0`: stop the voice channel, zero the four overlays (`0x10099630`), `FadeoutExpressions 0x101063a0`, `FinishTalking 0x102c0ca0`, and reset to the `0xf1` idle sequence when a partner is live and `!m_bDisableAI`. (4) `m_hCine` live → `CancelScript 0x101a8c30` (→ `CineCleanup 0x1027d170`), then `0x1026e340(m_IdealNPCState)` if state != ideal — `0x1026e340` is an immediate `SetState` writing both `+0x5cc0` and `+0x5cc4`. (5) **Clear**, return true. |
| `DiscardScheduleState 0x1027be60` | `OnRestore 0x1027bf50` slot 130, from the post-restore loop `0x1011a620` | `ClearGoal`, **clear**, `m_Activity +0xfec = 0`, `m_Conditions` zeroed if no enemy; SCRIPT state (4) with a dead `m_hCine +0x5d74` → `SetState(1)`, `m_IdealNPCState = 1`, DevMsg `"Scripted Sequence stripped on level transition for %s"`. Save slot 126 `0x1027bc60` writes the `AIExtendedSaveHeader_t`: version 1, flags bit0 enemy present, bit1 `m_hTargetEnt +0x5ce4` live, bit2 navigator goal active, `szSchedule[128]`, CRC32 of the task array. `OnRestore` keeps the program only when (state != 4 ∥ cine live) ∧ name ∧ version == 1 ∧ (bit0 → enemy) ∧ (bit1 → target) ∧ CRC matches; it always clamps `taskFailureCode +0x5c50 ≥ 0x2a` to 1; `+0x5c58` (post-restore refind-path) = bit2, and a failed refind (`0x102ee1e0`) discards a second time. |
| `ClearAllSchedules 0x10265820` | `npc_reset` ConCommand `0x10087550`, flags 0 (not cheat-protected) | Every entity that casts to `CAI_BaseNPC`, dead included: **clear**, then navigator `ClearGoal`. The `LoadAllSchedules 0x1030c560` that follows loads only when the list head is empty, and nothing but `CAI_SystemHook` vfunc5 empties it — so the command reloads nothing; it forces every NPC to reselect. |
| `0x102ae8e0` | CopGenerator spawn `0x10310c10` ← cops director `0x1017ee80` ← `0x1017ec40` ← the player rule pass `0x10169960` | The spawner: `SetState(m_bNoAlertState +0x65f6 ? 1 : 3)` via `0x1026e340`, then `m_vSavePosition +0x5dd0 = *pos`, `m_fSavePositionWalk +0x63e0 = 1`, **clear**. `+0x63e0` has one reader: the tail of Troika `GetSchedule 0x102ae920`, which resets it and answers `0x89 TROIKA_RUN_TO_SAVED`; `NPCInit 0x1029a0b0`, `TaskFail 0x1029adb0` and `CNPC_VCamera` are its other clearers. The director arms the incident position through `0x1017ed00` from `PlayerCriminalIncident 0x1017f2a0` / `PlayerSupernaturalIncident 0x1017f4a0`; per cop it calls `0x10370560` (cop target handle `0x1093ac3c`, deadline `0x1093aca8 = curtime + 30`) then the spawn, which picks a node ≥ 512 u from and not visible to the player within 8192 u (`0x10310d70`). |
| `FixScriptNPCSchedule` slot 586: `CCineNPC 0x101a8840`, `CCineAI 0x101a95d0` | `PostIdleDone 0x101a8640`, from RunTask tasks 99 and `0x62` | CCineNPC: `m_IdealNPCState = 1` unless 7 (DEAD), then **clear**. CCineAI: `m_iFinishSchedule +0x5f64` 0 → **clear**; 1 → `SetSchedule(0x2a AMBUSH)` `0x10280de0`, no clear; else DevMsg `"FixScriptNPCSchedule - no case!"` + **clear**. `CCineAISchedule 0x101a98c0` does not clear. Story 0003's. |
| `0x101a81a0` (linked-sequence start) and `RunTask 0x10288780` case `0x60 TASK_WAIT_FOR_SCRIPT` | RunTask | `0x101a81a0` resolves `m_iszLinkedSequence +0x5f5c`, `TaskComplete`s that cine's NPC, recurses along the link chain, `StartSequence` (slot 584) with `m_iszPlay +0x5f48`, **clears if `m_bSequenceFinished +0x65c`**, `m_flPlaybackRate = 1`, fires `m_OnBeginSequence +0x5f9c`. The `0x60` arm does the same on its own cine after `IsTimeToStart 0x101a7540`. Story 0003's. |
| `CSceneEntity::ClearSchedules 0x10084260` | none in the image | Clears only when the running program is `0x34 SCENE_SEQUENCE`. Dead code. |
| `CAI_BehaviorBase::NotifyChangeBehaviorStatus 0x102c6ff0` | standoff `SetActive 0x102c7360` | Gated on slot 455, which is `0x101a66a0 { return 0; }` on every NPC class. Dead. |

Schedule ids `0x2a` and `0x34` are `AMBUSH` and `SCENE_SEQUENCE` in the registrar `0x102cadd0`
(no `SCENE_GENERIC` exists). Unrecovered: hitboxes 4 and 5; which shipped damage sources carry
burn bit `0x8`; what slot 445, slot 529 and `0x10289c90` do on a cleared program; `RunAI(1)`'s
callers; the cop-count table `0x1017ec10`.

**`TASK_WAIT_RANDOM` is task `0x67`** (registrar `0x10316ff0`, the 330-name task table — the
ids the port's tests spell as 0x17/0x1c/0x20… are confirmed against it), base `StartTask` arm
`0x10283dae`: `m_flWaitFinished (+0x5db4) = curtime + RandomFloat(0.1, arg)` through the random
interface at `0x1070b244`. The low bound is `0.1`, not `0`. `TASK_WAIT_RANDOM 0.00` (four programs
author it) passes the operand unclamped, so under Source's `low + (high − low) · frac` (the engine's
`vstdlib` body, not in `vampire.dll`) it waits between 0 and 0.1 s, never nothing; the port's
`FRandRange(0.1, arg)` is the same expression. `TASK_WAIT` (task 2, arm `0x10286505`) is `curtime + arg` with no
floor. Base `RunTask` completes both when `m_flWaitFinished <= curtime`.

**`SetGoal` does not complete tasks.** Navigator `SetGoal` (`0x102ecd20`) returns a `char`; a
third argument of `2` (the patrol arm) or `0` (the base arms) only changes whether it clears the
goal entity / re-paths on failure (`& 4`). The Troika arms that follow it with `TaskComplete` /
`TaskFail(0x0c)` themselves (patrol point, interesting place, kick prop) decide the task; the base
`GET_PATH_TO_*` arms (`0x1c` `GET_PATH_TO_LASTPOSITION`, `0x20` `GET_PATH_TO_BESTSOUND`, `0x1d`
`GET_PATH_TO_BESTUNKNOWN`'s base twin) return without either, so their completion is the base
`RunTask`'s (`0x10288780`). UNRECOVERED: that `RunTask` arm's test (goal active → complete, else
`TaskFail`), and the `TASK_WAIT_PVS` base arm's relation to Troika's (`0x102aad7e`, spawnflag bit
10 or `0x102c2430` → complete at once).

**Port (story 25, closed 2026-09-13; reopened and re-closed 2026-09-13 after review).**
`IDLE_STAND` (1) and `FAIL` (0x43) are registered from their blobs with their decoded masks
(`ElysiumSchedule.cpp`, kernel registry; `COND_PROVOKED 0x53` and `COND_GIVE_WAY 0x68` added to
`EElysiumNpcCond` with no producer yet). A task answering `Failed` inside `ElysiumSchedule::Tick`'s
loop calls `TaskFail`, sets `bDidMaintainSchedule` and returns with the program installed — the
`0x102821ae` exit; the route runs at the top of the next tick, where `TASK_FAILED` in the pass's
conditions (the NPC's `TaskFail` writes it into `Cognition.Conditions`; `ThinkDead` now passes
that set too) asks `FailScheduleFor` (`m_failSchedule`, then the program's declared route, then
`FAIL`) and hands the answer to **`IElysiumScheduleRunner::TranslateSchedule`** (slot 440,
identity by default) before `Start`; the loop then continues on the new program in the same
pass, with the pass's condition snapshot dropped, as `SetSchedule`'s zeroing makes the next
`IsScheduleValid` see none. For that to work the bit has to survive the gather in between:
retail's `GatherConditions` (`0x1026ec30`) zeroes nothing (no six-word loop; only its lanes'
`SetCondition`/`ClearCondition`), where the port's `ElysiumNpcEnemy::GatherConditions` rebuilds
the sensed lanes from a `Reset()` word — a port structure, since its lanes only set — so it now
carries `TASK_FAILED` and `SCHEDULE_DONE`, the two bits written from outside the gather, across
the reset. `FElysiumNpc::TranslateSchedule` is Troika's `0x102b12f0` for the
ids the port registers: `IdleStand`/`IdleDisposition → IdleDisposition`; under `D_MILDLY_CRAZY`
the `0x132` target is a named seam (tallied, answers `IdleDisposition`) until 21a registers
`LAUGHING`; the frenzied pre-table's `0xc7`/`0xca` rows are a named seam (tallied, identity)
until 25b; species overrides are 25b's. The selectors' answers never pass through it — they are
already Troika ids. `ElysiumSchedule::Start`'s miss arm traces `"GetScheduleOfType(): No CASE for
…"`, tallies the unported program under `elysium.stubs` (kind `schedule`, surface
`SetSchedule(<name>)`) and installs `IDLE_STAND` untranslated with no `TaskFail`.
`FElysiumNpc::RandomSeconds` draws `FRandRange(0.1, Max)` and the kernel passes the operand
unclamped. `ElysiumSchedule::ClearSchedule` is `0x10280d30` (the six words cleared,
`FailScheduleOverride` kept, `PRESERVE_PATH` cleared, slot 435, conditions untouched); a body
reaches it through `IElysiumScheduleRunner::TakeClearScheduleRequest`, polled after every task
step and at the top of every tick, and discarded by `Start` — `FElysiumNpc::RequestClearSchedule`
is the seam and no body calls it until 0003's task bodies and 25a's other callers. Consequence the
port now shares with retail: a headless or stance-less body whose idle fails stands one pass on
the failed program, then in `FAIL` (its `SET_ACTIVITY` watchdog, `WAIT 1`, `WAIT_PVS`) instead of
reselecting every think, and `FAIL`'s mask withholds `NEW_ENEMY`, so `ChooseEnemy`'s interest gate
starves for that program's life. Tests: `Elysium.Substrate.Schedule.FailRoute`,
`Elysium.Substrate.Schedule.TroikaTranslate`. Unrecovered after this pass: the base `RunTask`
path-completion arm named above; whether `TASK_SET_ACTIVITY ACT_IDLE` in `FAIL` re-plays an
already-idle body (the Troika `0x102a1c0f` arm answers it, story 14); producers for
`COND_PROVOKED` and `COND_GIVE_WAY`; the door-block gate on the route (25c).

## Species slot-435 overrides all chain (2026-09-08)

`vtmb_slot 435`: 79 classes. `CNPC_VGargoyle` `0x10378fc0`, `CNPC_VHengeyokai` `0x10383090`,
`CNPC_VTzimisce` `0x103bf610`, `CNPC_VWerewolf::OnScheduleChange` `0x103ced10` **all call the
Troika body `0x102a0940` first**, then, gated on `PRESERVE_PATH` clear, decrement a per-species
shun counter (`m_iShunnedFindPillar` / `m_iShunnedFindFish` with `+0x6680 = 0` / `m_iShunnedFindBody`
with `m_ePathMode = 0`); the werewolf keeps a 50-entry schedule history at `+0x668c`. No
classname branch is needed in the port's single `OnScheduleChange`. Classes on the plain base
reset `0x1027a700` (no flag word): `CAI_BaseNPC`, `CAI_BaseHumanoid`, `CAI_ExpressiveNPC`,
`CAI_TestHull`, `CCineNPC`, `CCineAI`, `CCineAISchedule`, `CGenericNPC`, `CGenericSabbat_NPC`,
`CGeneric_NPC_bathack`, `CNPC_Bullseye`, `CNPC_Crow`, `CScriptedTarget`; `CGeneric_NPC`,
`CNPC_ProneDialog`, `CPayphone` and the makers carry the Troika body.
