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
override: `m_failSchedule (+0x5c54) ? m_failSchedule : 0x43`; by slot order and its 3-word
`RET` the virtual is the SDK's `SelectFailSchedule`, and `0x10281730` is the non-virtual
`GetFailSchedule` around it (story 29a) — then the selector trace
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
| Troika `EnterGrappleState` slot 379 `0x102b5c00` | `StartGrappleAttack 0x10328df0`: attacker site `0x1032928a` (an NPC for mode 8, and mode 2 with an NPC seducer), victim site `0x103292d8` (modes 0/6, 3, 2) | (1) If the queued-burn list `+0x65a8` (count `+0x65b4`, 0x4c-byte `CTakeDamageInfo` copies) is non-empty: each record gets attacker = inflictor = this and hitbox `RandomInt(0,1) ? 4 : 5` (`0x101c2a10` is the hitbox setter), is applied to the partner, and the call **returns false** with the list intact. The only producer is `CreateDamageEffects 0x10330d00` (from `OnTakeDamage_Alive 0x103302e0`): a burn hit (type bit `0x8`, hitbox > 0, `!ON_FIRE 0x30 && curtime > m_flNextBurnTime +0x65bc` through slot 615 `0x102ad0c0`) is copied there, `0x151 TROIKA_ONFIRE` forced (`0x102ae750`), and the hit deferred; `TASK_ON_FIRE_LOOP 0x9d` drains it, then slot 616 `0x102ad110` clears `ON_FIRE` and re-arms `+0x65bc = curtime + 15`. (2) Base `0x1026cdc0`, **not gated on grapple type or role, always true**: `0x1026d130` (`SetEnemy(NULL)`, squad disconnect, `m_iIsOblivious++`), `m_OnGrappleBegin +0x5bd8`, `CBaseCombatCharacter 0x10329760`. (3) `IsInDialog 0x102c1170` (`m_bIsTalking +0x64c0` ∥ `m_szDialogQue +0x64ec` ∥ `m_hDialogPartner +0xfe8` ∥ the speech-scene handle `+0x6554`) → `0x102c0bb0`: stop the voice channel, zero the four overlays (`0x10099630`), `FadeoutExpressions 0x101063a0`, `FinishTalking 0x102c0ca0`, and reset to the `0xf1` idle sequence when a partner is live and `!m_bDisableAI`. (4) `m_hCine` live → `CancelScript 0x101a8c30` (→ `CineCleanup 0x1027d170`), then `0x1026e340(m_IdealNPCState)` if state != ideal — `0x1026e340` is an immediate `SetState` writing both `+0x5cc0` and `+0x5cc4`. (5) **Clear**, return true. **Ported whole by story 29e's review (2026-09-14)** as `FElysiumNpc::EnterGrappleState`: the port's `Grapple.Type == StealthKill` gate on the base half — the twin of the one 29d removed from `LeaveGrappleState` — is deleted, the burn discharge answers the refusal with the list intact, and the hitbox draw is recorded because this runtime's damage packet carries no hit group. |
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

## The schedule host and the task surface, walked (2026-09-13, story 29c-1)

Family **Schedule** of story 29c-1: the 64 `rule` rows of `order.md` layers 0–9 whose behaviour is
the schedule host, the task surface and the melee/cover selectors. The port is
`Substrate/ElysiumNpcKernelSchedule.cpp`, `Substrate/ElysiumNpcScheduleHost.cpp` and the two arms
added to `FElysiumNpc::BuildScheduleTestBits` / `SelectSchedule`; the tests are
`Elysium.Substrate.NpcKernelSchedule.*`.

**The standing fact of the whole family.** Retail gives every NPC class a
`CAI_ClassScheduleIdSpace` and fills it by PARSING that class's schedule text.
`CNPC_VBrujah::InitCustomSchedules` (`0x10367a40`) is the worked example: it calls
`CAI_LocalIdSpace::Init` (`0x102ea0e0`) on `DAT_1093a740` (schedule), `DAT_1093a758` (task) and
`DAT_1093a770` (condition) with the three global namespaces `0x109203cc`/`d4`/`dc` and
`CNPC_VVampire`'s spaces as parents, registers `SCHED_VBRUJAH_WALK` 0x158 and `SCHED_VBRUJAH_WATCH`
0x159 through `0x102ea130`, and then runs the schedule-text parser `0x1030d850` in a loop that seeds
itself from `DAT_1062f278`, breaks on the first failure and stores the answer back. That one byte is
what slot 452 `LoadedSchedules` returns, so **the flag ships `true` and only a malformed schedule
text clears it**. The four spaces are `0x18` apart, which is how family Squad's slot-546 table
reaches the same class's squadslot space at `+0x48`. This port registers its programs by identity
and parses no text, so the id spaces stay at the empty range `0x102ea090(isRoot = false)` left
(`m_localBase = 9999`, `m_localTop = -1`), every translation answers -1, and `LoadedSchedules`
answers the shipped `true`. `[VtMB]`

### `0x101aa790` slot 580 and `0x102b97f0` slot 452 — the per-class spaces and their flag

Fourteen of the slot-580 bodies are one line, `return &DAT_<class schedule id space>;`
(`CNPC_VBrujah` `0x10367870` -> `DAT_1093a740`, `CNPC_VCamera` `0x103683b0` -> `DAT_1093a7d8`
shared with `CNPC_VCameraSecurity`, `CNPC_VChangBros` `0x1036a1b0` -> `DAT_1093a888`, `…Blade`
`0x1036eab0` -> `DAT_1093a8f0`, `…Claw` `0x1036f2b0` -> `DAT_1093a938`, `CNPC_VCombatman`
`0x1036fb10` -> `DAT_1093ab68`, `CNPC_VCop` `0x10370930` -> `DAT_1093ac60`, `CNPC_VDog`
`0x10373530` -> `DAT_1093acd8`, `CNPC_VFrenzyShadow` `0x10375240` -> `DAT_1093ae48`,
`CNPC_VGangrel` `0x10377070` -> `DAT_1093aef8`, `CNPC_VGargoyle` `0x10377b20` -> `DAT_1093aff0`,
`CNPC_VVampire` `0x103750e0` -> `DAT_1093d258` shared with `CNPC_VPlayerController`, and the Troika
line `0x101aa790` -> `DAT_10924248`). The slot-452 bodies are the same shape over the parse flag
(`CAI_BaseNPCTroika` `DAT_105d1058`, `CNPC_VBrujah` `DAT_1062f278`, `CNPC_VCamera` `DAT_1062f668`,
`CNPC_VChangBros` `DAT_1062fe14`, `…Blade` `DAT_1062fe38`, `…Claw` `DAT_1062fe5c`,
`CNPC_VCombatman` `DAT_10631678`, `CNPC_VCop` `DAT_10631ba8`, `CNPC_VDog` `DAT_106368a8`,
`CNPC_VFrenzyShadow` `DAT_10637b44`, `CNPC_VGangrel` `DAT_10639090`, `CNPC_VGargoyle`
`DAT_106395c0`). `CAI_BaseNPC`'s own slot 452 (`0x1027c2e0`) is a literal `true`; the Troika
override replaces it with the global. `CAI_BaseNPC::Precache` (`0x1027bb50`) is the only reader:
a false return is `"ERROR: Rejecting spawn of %s as error in NPC's schedules"`. `[VtMB]`

**A census fact this family's tests had to learn, recorded once.** `CNPC_VCop` is a census class with
its own slot-580 and slot-452 bodies, but **its classname list is empty — no census class claims the
entity classname `npc_VCop`**. `ElysiumNpcClasses.cpp` does register an `npc_VCop` leaf, so one
spawns; `FElysiumNpc::RetailClass()` answers null for it, and every per-species lookup in this
family correctly falls through to the Troika line. Family Squad found the same thing. The converse
also holds and is not the same gap: the census claims `npc_VCamera`, `npc_VMingXiaoTentacle` and
`npc_VPlaceholder`, and this runtime registers no leaf for any of them, so their species arms are
reachable only by retail class name. `[port]`

**Unrecovered:** nothing in the bodies. What the port cannot reproduce is the id-space RANGE, which
only the schedule-text parser writes.

### `0x10280de0` — `SetSchedule(int)`, and the ideal-schedule stamp

`if (id < 0x3b9aca00 || id == -1) id = ScheduleLocalToGlobal(GetClassScheduleIdSpace(), id);`
(slot 580 then `0x102ea2d0`), `m_IdealSchedule (+0x5c3c) = id`, then `SetSchedule(int)`
(`0x102cc1f0`: slot 440 `TranslateSchedule`, slot 446 `GetScheduleOfType`, the
`"GetScheduleOfType(): No CASE for %d"` miss arm installing base 1) and finally
`CAI_BaseNPC::SetSchedule(CAI_Schedule*)` (`0x10280e50`). So an id at or above 1,000,000,000 is
stamped unchanged and everything else — the -1 sentinel included — is translated first. **The
stamp is the only thing this body adds** over the install chain story 25 already ported; nothing in
the port wrote `+0x5c3c` before.

Slot 619's five species overrides (`CNPC_VAndreiBlood` `0x1035dba0`, `CNPC_VAsianVampire`
`0x10361530`, `CNPC_VChangBros` and its two leaves `0x1036c760`, `CNPC_VSabbatLeader` `0x103a9fd0`,
`CNPC_VSheriffMan` `0x103af8d0`) are 100 bytes each and identical: push a literal name onto
`g_ScopeTraceStack`, call `0x10280de0`, pop. They carry no class-specific logic, so the whole of
what they add is the name they push. `[VtMB]`

**Unrecovered:** `0x102b7690`'s `GetScheduleOfType(0x9e)` comparison decompiles as
`thunk_FUN_102cc1f0`; the comparison needs a `CAI_Schedule*`, so it is slot 446 and the thunk label
is wrong there.

### `0x10280f40` `NextScheduledTask` and `0x10280db0`

`fTaskStatus (+0x5c44) = 0`, `m_iScheduleIndex (+0x5c40) += 1`, then `0x10280db0`, whose whole body
is `m_iScheduleIndex == m_pSchedule->[+0x24]` — the schedule record's task COUNT. So the recovered
name reads "is the task index current" and what the body tests is "the program is exhausted". On
true: `m_failedSchedule (+0x5f38) = m_interuptSchedule (+0x5f3c) = 0` (the listing zeroes `EDX` at
`0x10280f43` and never rewrites it, so both stores are literal zero), one call through the global
`DAT_10924a6c`'s slot 1, and `SetCondition(COND_SCHEDULE_DONE 0x5d)`.

`0x10273e80 TaskComplete(bool)` is its neighbour: `if (!ignore && HasCondition(COND_TASK_FAILED))
return;` then `fTaskStatus = 4`. `CAI_Motor` slot 2 (`0x102623c0`) forwards to it through
`m_pOuter (+0x4)` and `CAI_Motor` slot 1 (`0x102623a0`) is `JMP [[m_pOuter] + 0x700]` — the owner's
slot 448 `TaskFail`, unchanged. `[VtMB]`

**Unrecovered:** `DAT_10924a6c` and its slot 1.

### `0x102a18a0` — the `TASK_WAIT` deadline

`if (0.0 < task->flTaskData) m_flWaitFinished (+0x5db4) = curtime + flTaskData; else
m_flWaitFinished = curtime + _DAT_10447ee0;` — an operand at or below zero waits a retail default
rather than not at all. `[VtMB]`

**Unrecovered:** `_DAT_10447ee0`'s value.

### `0x1028a2a0` — `CAI_BaseNPC::PreSelectSchedule`

`field_0x1b2c = 1` (the file/line selector trace), then: `COND_FLOATING_OFF_GROUND` (0x73) sets
`m_flGravity = 1.0` and dispatches slot 208 `SetGroundEntity(NULL)` and **falls through**;
`COND_NPC_FREEZE` (0x75) -> 0x3a; `COND_ON_FIRE` (0x30) -> 0x151; `COND_FLOATING_OFF_GROUND` again
-> 0x3e; else 0. The trace writes are `AI_BaseNPC.cpp` lines 3627 / 3634 / 3639. This is the
slot-437 body for `CAI_BaseNPC`, `CAI_BaseHumanoid`, `CAI_ExpressiveNPC` and ten more — **not** the
Troika line's `0x102ae920`, which is story 29e's.

Three species overrides of the same slot are constants with a trace write: `CNPC_VCamera` /
`CNPC_VCameraSecurity` (`0x10368f20`) writes tag 9 and answers 0x156; `CNPC_VMingXiaoTentacle`
(`0x1039de00`) writes tag 0x1a and answers 0; `CNPC_VPlaceholder` (`0x103a43f0`) writes tag 0x1e and
answers 0x157. `[VtMB]`

### `0x102bf6e0` — `ResolveTaskDistance`, slot 418

A four-entry jump table on `(int)param + 1000008`: -1000008 -> `m_flFollowerDistanceBackAway`
(`+0x6484`), -1000007 -> `+0x6488`, -1000006 -> `+0x648c`, -1000005 -> the fixed `_DAT_1044e664`
(10.0, the follower-distance overlap story 16a recovered). Anything else falls to
`CAI_BaseNPC::ResolveTaskDistance` (`0x102702d0`), which splits -1000003 (through a global's
slot 1), -1000002 and -1000000 and otherwise answers the truncated value.

Two species overrides sit in front of it and only then delegate: `CNPC_VMingXiao` (`0x10392a10`)
answers `m_flIdealRange` (`+0x6748`) for -1000004, `CNPC_VTzimisce` (`0x103b9120`) answers
`_DAT_10457f60` for -1000001. `[VtMB]`

**Unrecovered:** `_DAT_10457f60`; the base body's three sentinel answers.

### `0x102ae840` — the scripted-schedule order push

`if (m_NPCState != 7 && m_IdealNPCState != 7)` (7 is dead) and `if (IsAlive() || force)` (slot 158):
record the order id at `+0x65cc`, `m_bForceStateChange (+0x1b28) = 1`, and
`m_bfAINPCFlags2 (+0x14bc) |= 0x82000000`. That mask is `CHOOSE_NEW_SCHEDULE` **plus bit 31** —
which `ElysiumNpcFlags.h` records as the schedule compiler's routing marker rather than a flag.
`0x102b7690` writes and clears the same bit (`|= 0x80000100`, `&= 0x7ffffeff`), so retail really
does carry a flag there. `[VtMB]`

**Unrecovered:** the name of `m_bfAINPCFlags2` bit 31; `0x1030cbd0` has no entry that resolves to it.

### `0x102b6fe0` — the melee selector's failure gate

`COND_ENEMY_OCCLUDED` (0x48) first, then `COND_ENEMY_BLOCKED` (0x3a), each with the same shape:
`if (HasUsableRangedWeapon())` (slot 308) — with `m_bInMelee` set, dispatch slot 601 with the enemy
first — answer 0xe9; otherwise answer 0xcd for the occluded arm and 0xce for the blocked one. Zero
is "no opinion". Trace lines `AI_BaseNPCTroika.cpp` 23135 / 23145 / 23159 / 23169. `[VtMB]`

### `0x102b6c30` slot 604 and its five species overrides

`CAI_BaseNPCTroika::SelectScheduleMeleeCombat` and `CNPC_VAsianVampire` (`0x10361be0`),
`CNPC_VChangBros` + its two leaves (`0x1036d800`), `CNPC_VSabbatLeader` (`0x103aa060`),
`CNPC_VSheriffMan` (`0x103af960`), `CNPC_VTzimisceRunner` (`0x103c4430`) are ONE shape with six
different arm orders and six different schedule-id sets — they are not a table, and the port keeps
all six arm for arm.

The shape. **Head**: not in melee and slot 599 refusing the enemy, or in melee and slot 602
accepting, is the "should I be fighting at this range at all" branch; the in-melee side dispatches
slot 601 with the enemy before answering. The Troika line answers 0xe4 there (after the gate above),
`CNPC_VSabbatLeader` splits on `m_flEnemyDist <= 2 * range` into 0x15f / 0xe7,
`CNPC_VChangBros` / `CNPC_VTzimisceRunner` on `dist <= range + _DAT_104492b8` (200.0, the same
constant `ElysiumFootsteps.h` names) into 0xe4 / 0xe7, `CNPC_VAsianVampire` and `CNPC_VSheriffMan`
on `HasUsableRangedWeapon()`. **Tail**, in each body's own order: `COND_ENEMY_OCCLUDED` -> 0xcd
(Chang/Tzimisce/Sabbat) or the gate (Troika/AsianVampire/SheriffMan); `COND_SHOULD_DODGE` as an
INTERRUPT condition -> 0xd5 (Chang/Tzimisce only); `COND_CAN_MELEE_ATTACK1` -> 0xdc/0xdd;
`COND_ENEMY_UNREACHABLE` -> 0x17 (Troika, Tzimisce), 0x15d (Chang), 0x15a (SheriffMan), 0x15b
(Sabbat) or `GetJumpSchedule` (AsianVampire); the three-term break `TOO_FAR_FOR_MELEE (9) ||
INTERRUPT_TIME (0x1a) || the height-diff timer` -> 0xe9/0x15c; neither `TOO_FAR_FOR_MELEE` nor
`TOO_FAR_TO_ATTACK` (plus `!ENEMY_OCCLUDED` on AsianVampire and SheriffMan) -> 199; then 0xca/0xcb,
with Chang and Tzimisce inserting a 0xd2 arm gated on `dist <= range && !heightArmed` and a
0x15a/0xe1 arm gated on `0x102a11d0`.

**The height-difference timer is byte-identical in all six**:
`if (m_flEnemyHeightDiff <= _DAT_10451acc) m_flMeleeHeightDiffTimer = -1.0f; else if (timer ==
-1.0f) timer = curtime + RandomFloat(3.0, 4.0); else if (timer <= curtime) armed = true;`.
`CNPC_VSabbatLeader` runs its first two arms and never reads the answer. `[VtMB]`

**Unrecovered:** the melee-range convar `DAT_10924a1c` (its bool selects between `0.0` and its float
at `+0x28`), `_DAT_10451acc`, `_DAT_104c3cd4` (SabbatLeader's `TOO_FAR_TO_ATTACK` bound), and the
semantics of slots 599 / 601 / 602.

### `0x102b7690` — the entrenched cover / kick-prop selector

Completes the UNRECOVERED note in `authored-control.md`. Four `bool` parameters build a four-bit
hint-search mask; their names are not recovered, only their bits.

1. **The kick-prop refresh.** With parameter 4 set, `m_bAllowKickHintUse (+0x6436)` set, no hint
   node (`+0x5ddc`), `DODGING` (flags1 0x800) clear, no live `m_hKickProp (+0x643c)` and
   `m_flKickPhysicsPropSearchTimer (+0x6438) <= curtime`: rearm the timer at
   `curtime + RandomFloat(2.0, 2.0)` — a draw whose bounds are equal, so exactly two seconds, and
   the draw still advances the stream — and run the prop search `0x102b6650`.
2. **A live kick prop returns 0xa9 immediately**, ahead of any hint search.
3. **The hint search.** Under "no hint node, not dodging, no kick prop": store `GetEnemy()` in
   `m_hHintCoverObject (+0x6448)`, build `mask = p1 | (p2 << 1) | (p3 && allowKick) << 2 |
   (p4 && allowKick) << 3` and call `0x102b7110`.
4. **The hint-type table.** `0x283c` -> 0xa7, `0x283d` -> 0xa8, and hint types 100 / 0x65 / 0x27d8 —
   the same three family Hints found on the five activity lookups — enter the cover tail. Anything
   else, and no hint node at all, answers 0.
5. **The cover tail.** `AT_COVER_HINT` (flags1 0x2000) clear -> clear flags2 `0x80000100` and answer
   0x9b. Otherwise compute "does my enemy carry a ranged threat" from its own active weapon's
   capability word `& 0x6000`, and if `m_pShootAtHint (+0x6444)` is empty ask slot 609. On a miss
   there: if `0x102b5de0` refuses, `m_iPeekOutCount (+0x640c) += 1` and answer 0x9d while the count
   is under **5** and `ENEMY_OCCLUDED` is clear, else clear flags2 `0x80000100` and answer 0x9e when
   `m_bStayEntrenched (+0x6435)` is set or `ClearHintNode(60.0)` and no schedule when it is not. If
   `0x102b5de0` passes, the peek-out count decays by two with a floor at zero and the answer is
   0xa3 / 0xa0 / 0xa1 (ranged threat) or 0xa4 / 0xa5 (none), split on "am I already running 0x9e"
   and `COVER_VS_MELEE_MODE` (flags2 0x100), with a `RandomInt(0, 99) > 0x1d` coin on the 0xa0/0xa1
   pair. A hint that already has a shoot-at hint answers 0xa2.

Trace lines `AI_BaseNPCTroika.cpp` 23725, 23743, 23747, 23769, 23792, 23797, 23805, 23812, 23818.
`[VtMB]`

**Unrecovered:** the four parameter names.

### `0x1037cdf0` and `0x10387520` — the slot-453 species overlays

Four classes override `BuildScheduleTestBits`, and **one of them does not compose**.
`CNPC_VHumanCombatant` (`0x10387520`, nine census classes), `CNPC_VPedestrian` (`0x103a2980`) and
`CNPC_VTzimisceHeadClaw` (`0x103c16f0`) all open by calling the Troika line `0x102ad140` and then
add one condition: `SEE_CORPSE_FRIEND` (0x3e) when `IsAlive()` and `GetState() == 1` (idle) and not
(`m_edtDerivedType` bit 7 AND `m_bCameFromSpawner`); `PASS_OUT` (0x24) when not busy with a
discipline; `SHOULD_CHARGE` (0x35) unconditionally. `CNPC_VGuard1` (`0x1037cdf0`) instead calls the
EMPTY base `CAI_BaseNPC::BuildScheduleTestBits` (`0x10280fb0`), so **the Troika overlay does not run
for a Guard1 at all**, and then branches on `m_NPCState`: state 1 adds `COMFORT` (0x27) and falls
into the state-3 tail; state 3 tests the five `pl_*` thresholds against `m_hClosestPlayer`'s current
investigate / criminal / supernatural levels (`0x1017de60`, `0x1017ddd0`, `0x1017dd80`) and on a
pass adds `INVESTIGATE_LEVEL`, `CRIMINAL_FLEE_LEVEL`, `CRIMINAL_ATTACK_LEVEL`,
`SUPERNATURAL_FLEE_LEVEL` and `SUPERNATURAL_ATTACK_LEVEL`, otherwise clears `HEAR_PLAYER` (0x6f)
alone; state 0xb (hunt) runs the same five-threshold test plus `m_fHatesPlayer` and sets or clears
`SEE_PLAYER` (0x5a) and `HEAR_PLAYER` together. Every other state returns untouched. `[VtMB]`

**Unrecovered:** nothing in the bodies. The port cannot reach the hunt arm — `EElysiumNpcState` has
no member for retail state 0xb — and `m_edtDerivedType` (`+0x004c`) has no port member, so bit 7
reads clear.

### `0x1035d010`, `0x1038e340`, `0x1039de20` — three species `SelectSchedule` bodies

`CNPC_VAndreiBlood` (`0x1035d010`) is a strict ladder under trace tag 4: not `m_bActivated` ->
0x15b, `m_bDead` -> 0x15e, else if not `m_bForceTeleport` and `m_iHitCounter < m_iHitMax` then
`0x1035e920` splits 0x15d / 0x15c, else 0x15a. **That last split is the fleshpile's runner budget**
(family Species landed the body): `0x1035e920` is twenty-five bytes of
`return m_iActiveRunnerCount (+0x66b8) < 2.0` — `_DAT_10452dc4` is 2.0f — and the rest of the same
counter is `CNPCMaker_Fleshpile::MakeNPC` (`0x1034c2d0`, refusing at `2 <= count` and adding 1) and
its `DeathNotice` (`0x1034c8e0`, subtracting 1). So Andrei's fleshpile may have at most TWO runners
alive at once, and 0x15d is the arm he takes while there is room for another.

`CNPC_VManBat` (`0x1038e340`): the navigator probe `0x1027d990` answering anything but 2 writes
`m_iMoveGoalNodeID (+0x6674) = 1` and answers 0x158; then an obfuscated equality on `+0x6670`
(`Hash((f & 0x710935 ^ 0x148739) + 0x4094ab & 0x18ef6ca ^ f ^ 0x412a96ec) == Hash(0xfa0b0694)`,
hash `0x1042fbf0`) failing does the same and answers 0x159; then `HasInterruptCondition(0x4d
HEAVY_DAMAGE)` -> 0x15f; then a global's bool and eleventh word gate a `RandomInt(1, 10)` over
0x15c / 0x15d / 0x161 / 0x163 / 0x159.

`CNPC_VMingXiaoTentacle` (`0x1039de20`): a four-phase machine on `m_ePhase` under trace tag 0x1a.
An unset `m_flPhaseExpireTimer` is armed per phase (`_DAT_10450aa0`, `_DAT_10449270`,
`_DAT_1044e664` = 10.0, `_DAT_104bea38`). Phase 0/default splits on the timer into 0x156 / 0x157;
phase 1 into 0x158 / 0x159; phase 3 asks `0x1039ee20` against `GetAbsOrigin()` for 0x160 / 0x161.
Phase 2 is the body: in state 1 (idle) an expired timer plus `0x1039eee0` gives 0x160 / 0x161 and
otherwise 0x15a; in state 2 (combat) it clears `m_bCondTookDamage`, consumes condition 0x78 into
0x165, repeats the expired-timer pair, then on an expired `m_flFailedEvadeTimer` splits
`m_flEnemyDist < _DAT_1044ddb0` -> 0x15b, `m_flHideReadyTimer <= curtime && RandomInt(0, 99) < 50`
-> 0x163, else 0x15f; a live evade timer instead asks slot 604 with the active weapon's capability
word and answers 0x164 on zero. Any other state answers 0x164.

`CNPC_VCamera` / `CNPC_VCameraSecurity` (`0x10368f40`) and `CNPC_VPlaceholder` (`0x103a4410`) are
the same write-and-return constants as their slot-437 bodies (tag 9 -> 0x156, tag 0x1e -> 0x157).
`[VtMB]`

**Unrecovered:** `m_bActivated`, `m_bDead`, `m_bForceTeleport`, `m_iHitCounter`,
`CNPC_VManBat +0x6670`, `m_ePhase` and the three tentacle timers are species words above `+0x665c`
with no port member and no producer (`m_iActiveRunnerCount` and `m_iHitMax` are no longer among
them — family Species declared both); `DAT_1093b814`; the three tentacle phase durations;
condition 0x78, which is above the base registrar's 0x76 and belongs to a derived table that was
not dumped.

### `0x103a9d00` — `CNPC_VSabbatLeader::FlipFailureType`

Inside a scope-trace push/pop, the whole body is `m_FailureType (+0x66c0) = 1 - m_FailureType`.
`[VtMB]`

### `0x1027db30` — retargeted: the navigator node-index guard, not `StartTaskByIndex`

29c's row named this `FElysiumNpc::StartTaskByIndex` over a `‼` row with no recovered callers. The
disassembly reads the NAVIGATOR at `+0x5d34`, takes its node list at `+0x2c` (count at `+0x00`,
array at `+0x04`), bumps the global error counter `0x106c994c` and answers false for an index below
zero or at/past the count, answers false for a null node, and otherwise tail-jumps to
`[[this] + 0x83c]` — slot 527, which `signatures.tsv` names `bool IsUnusableNode(CAI_Node*)`. It is
a node guard; the port carries it as `FElysiumNpc::IsUnusableNodeIndex`. `[VtMB]`

**Unrecovered:** the retail name.

### `0x101a95d0` — `CCineAI::FixScriptNPCSchedule`, slot 586

`m_iFinishSchedule` (`CCineAI +0x5f64`, the director's own word) 0 clears the NPC's schedule
(`0x10280d30`); 1 calls `SetSchedule(0x2a)` (`0x10280de0`) with **no** clear; anything else is
`DevMsg(2, "FixScriptNPCSchedule - no case!")` and then the clear. `[VtMB]`

## The three hint validators — `0x10295ed0`, `0x102961a0`, `0x10296c40` (2026-09-13)

Three bodies that ask "is this hint node still somewhere I can stand", over the same five hint words
— `m_flTargetAngleRangeDot` (`+0x458`), `m_flTargetDistMin` (`+0x45c`), `m_flTargetDistMax`
(`+0x460`), `m_nHintType` (`+0x5dc`) and `m_iDisabled` (`+0x5e8`) — and each with its own tail.

`0x10295ed0` (562 bytes) is the quiet one. A null or disabled hint fails; `m_hHintCoverObject`
(`+0x6448`) must resolve; the 2-D distance from the hint to that cover object must lie in
`[min, max]`, widened by `_DAT_10451acc` (**64** units) on **both** ends when the hint being tested
is already `m_pHintNode` (`+0x5ddc`). The delta is then normalised by `1 / (dist + eps)` and dotted
with the hint's own facing (`0x102d12e0` for the yaw, `0x101d2f40` for its 2-D basis), and the
projection must be **strictly** greater than `m_flTargetAngleRangeDot`. The current hint accepts
there. Any other hint must additionally be within `_DAT_10483aac` (**512** units) of me, and — only
for hint type **0x283d** — the forward projection of `(hint - me)` on the hint's facing must exceed
`_DAT_104454d0`; then `0x102968f0` decides on line of sight.

`0x102961a0` (1326 bytes) is the verbose twin and is **not** the same body. It runs a
`m_strTargetName` (`+0x468`) gate in **front** of everything — an empty name admits everyone, a set
one is compared case-insensitively against my own `m_iName` (`+0x26c`) and a mismatch is
`"Target name mismatch (%s)"` — then the same band (one shared reason,
`"Distance (%d) < %d or > %d"`, for both tolerance policies) and the same projection
(`"Enemy outside of good range (%.2f) <= %.2f"`). Instead of `0x102968f0` it casts its own ray, from
my origin raised by `m_Collision->OBBMaxs().z` to a point on the hint (`0x102d1180`), and requires
`fraction >= 1.0` with neither `allsolid` nor `startsolid`, else `"Failed LOS check (%s)"` naming the
blocker. Every reason string is built only when `DAT_10925444` — the `ai_debug_npc` handle —
resolves to *this* NPC.

`0x10296c40` (1614 bytes) validates an **attack** position against an enemy and an active weapon. Its
first arm is a **pass**, not a fail: my own hint while `m_bStayEntrenched` (`+0x6435`) stands, or a
null enemy, accepts before any test at all. Otherwise: no active weapon fails; a height difference
over `_DAT_1049ae28` fails; the hint-to-enemy 2-D distance below `m_flTargetDistMin` fails; unless
`m_bStayEntrenched`, a distance over **either** the weapon's own maximum range (`weapon +0x8c0`) or
`m_flTargetDistMax` fails; a hint that is not already mine needs
`dot(normalize2D(hint - enemy), normalize2D(me - enemy)) >= _DAT_10451ab4` — and retail's own message
gives that literal away, `"Projection (%.2f) < 0.2"`. The facing projection is then tested against
**two** caller-supplied bounds that are not symmetric: `<= flGoodRange` is
`"Enemy outside of good range"` and `>= flBadRange` is `"Enemy inside of bad range"`. Last, and only
under `m_bForceCoverLOSCheck` (`+0x6408`), `0x102968f0` must pass or the answer is
`"Failed hint LOS"`.

**Unrecovered:** `_DAT_1049ae28` (the height limit), `_DAT_1046a51c` (the normalise epsilon) and
what `_DAT_104454d0` means as `0x10295ed0`'s forward floor; and `0x102968f0` itself is walked only as
"the hint LOS check" here.

## The face-anim turn ladder — `0x10297a20` (2026-09-13)

A **third** turn-in-place ladder beside `CAI_BaseNPC::SetTurnActivity` (`0x10289d10`) and the Troika
line's (`0x10297640`), and it is not a slot. It reads the motor's yaw delta (`CAI_Motor::DeltaIdealYaw`,
`0x102e1f90`) and walks four rungs, each gated on the body actually authoring the activity
(`SelectWeightedSequence(act) != -1`): outside `[_DAT_1049ae3c, _DAT_1049ae38]` (**-140**, **140** —
the same pair the Troika ladder reads) it picks activity `0x10ff`; at or below `_DAT_104704b4` it
picks `0x10fd`; at or above `_DAT_10462950` (**40**) it picks `0x10fa`; otherwise `0x10f8`. Each rung
writes `m_eFaceAnim` (`+0x63e4`) — **8**, **6**, **3**, **1** in that order — and
`m_flFaceYawDiff` (`+0x63e8`), and the three upper rungs write a *drawn* value (`__ftol` of a random,
masked to 16 bits and scaled by `_DAT_1044ffdc`) where `0x10f8` writes the yaw delta itself. A body
authoring none of the four falls to `ACT_IDLE` with `m_eFaceAnim = 0` and `m_flFaceYawDiff` still the
delta.

**Unrecovered:** `_DAT_104704b4` (the second rung's edge) and `_DAT_1044ffdc` (the duration scale);
and the body's retail name — it has one direct caller and no slot.

## The scripted custom move and the patrol interest draw — `0x10289fe0`, `0x1029f650`, `0x1029f730` (2026-09-13)

`0x10289fe0` is `CAI_BaseNPC::GetScriptCustomMoveActivity`, SDK 2013's function arm for arm. It
answers `ACT_WALK` (**9**) unless `m_hCine` (`+0x5d74`) resolves and its `m_iszCustomMove`
(`+0x5f50`) is set; then `LookupActivity(name)`, and on a miss `LookupSequence(name)` — a name that
is a raw sequence answers `ACT_SCRIPT_CUSTOM_MOVE` (**0x18**) and one that is neither falls back to
`ACT_WALK`. Retail re-resolves the cine handle at each of the four reads.

`0x1029f650` and `0x1029f730` are the write and read halves of one draw over a patrol node's
interesting-place record (`0x1029f6c0`). The write half **first** clears `m_bPatrolPathUseHint`
(`+0x65a0`) unconditionally — so a node with no record clears a flag that was standing — then, with
a record, rolls `RandomInt(0, 99)` against its `m_iIPPercent` (`+0x46c`) and sets the flag when the
roll comes in under it, returning the flag. The read half answers 0 unless the flag stands, and
otherwise returns the record cached at `+0x659c`, resolving it on the first ask.

**Unrecovered:** both bodies' retail names, and what `0x1029f6c0` returns beyond its `+0x46c` chance
word.

### The occlusion reaction ladder `0x102b8320`

_Recovered 2026-09-13, story 29c-1._

Slot 606 answers what to do about an occluded enemy, and every arm stamps the selector trace at
`+0x1b30`/`+0x1b34` with its own source line. Without `COND_ENEMY_OCCLUDED` (0x48) it answers 0.
`COND_ENEMY_UNREACHABLE` (0x59) answers `0xaa` (line `0x5e1b`). `m_bfNPCFrenziedFlags & 0x100`
answers `0xb4` (`0x5e20`). `m_bfAINPCFlags2 & D_POSSESSED` (`0x40000`) rolls `RandomInt(0, 99)` and
answers `0xb6` under 0x46, else `0xb4` (`0x5e28` / `0x5e2c`). `m_bfAINPCFlags & FORCED_OCCLUDE`
(`0x10000000`) **clears the bit** and rolls the same draw against 0x50 (`0x5e37` / `0x5e3b`). What
remains is one `RandomInt(0, 99)` against four per-instance thresholds — `m_iPercentOccludedWait`,
`Cover`, `Walk` and `Flank` (`+0x6420`..`+0x642c`) — with `COND_SQUAD_SEE_ENEMY` (0x31) selecting
between two answer tables: `0xab / 0xb0 / 0xb3 / 0xb2 / 0xb2` with the squad condition and
`0xaa / 0xaf / 0xb5 / 0xb6 / 0xb4` without. The squad table's last two buckets answer the same
number. `m_iPercentOccludedChase` (`+0x6430`) is NOT read by this body.

**Unrecovered:** the name of frenzied bit `0x100`, and what the eight returned numbers name.

### The task-argument helper `0x102aa9e0`

_Recovered 2026-09-13, story 29c-1._

One direct caller, no slot. When the argument and its `+0x04` member are both non-null it tests
`thunk_FUN_10307b80(arg->+0x4)` and conditionally clears through `thunk_FUN_1029f5d0(arg)`, forwards
to `thunk_FUN_1029f650(this, arg)`, and calls `TaskComplete(false)` (`0x10273e80`). Otherwise it
stamps the ASSERT file/line pair at `+0x1b44`/`+0x1b48` with line `0x3d9c` and raises through the
entity's own vtable `+0x700` — slot 448, `TaskFail` — with code `0x1d`.

**Unrecovered:** the argument's type, and therefore what `+0x04` is and what the two middle calls do.

## Story 29d, family Motor10 — `CAI_Motor`, `CAI_Navigator`, the standoff goal and the hull probe

_Recovered 2026-09-14, story 29d._

Twelve bodies of layers 10–18: the two `CAI_Motor` step bodies and the helper between them, the
navigator's `MoveNormal` and its gate, the standoff behaviour's activity translation, the standoff
goal entity's two inputs and its `UpdateOnRemove`, the two hull-size bodies, `CAI_TestHull::Spawn`
and the shoot target. Movement is the schedule kernel's concern and they land here together.

Only ONE of the twelve fills a Troika-line slot (`0x102984a0`, slot 531). The rest fill slots on
their own object's vtable — `CAI_Motor`'s 21-slot table, `CAI_Navigator`'s 18-slot table,
`CAI_StandoffBehavior`'s 29-slot behaviour table and `CAI_StandoffGoal`'s 246-slot
`CBaseEntity`-line goal entity — or no slot at all.

**Ten constants, all read out of the pinned image.** `_DAT_10449270` **0.5** (double),
`_DAT_104454c0` **1.0f**, `_DAT_104454c4` **0.0f**, `_DAT_104493d0` **0.1** (double),
`_DAT_1044fab0` **0.0**, `_DAT_1044e658` **0.01** (double — `lifecycle.md` lists it as unrecovered
and this is its value), `_DAT_10451acc` **64.0f**, `_DAT_10449258` **3.0f**, `_DAT_104994e0`
**-30.0f**, and the sweep's inline `0x42c80000` = **100.0f**.

### `CAI_Motor::MoveGroundExecute` `0x102e14a0` and its walk `0x102e1560`

_Recovered 2026-09-14, story 29d._

Slot 19 of `CAI_Motor`'s own table, which no class overrides, and it **returns** the value of
`0x102e1560` — `CALL 0x10012116; POP EDI; POP ESI; POP ECX; RET 0xc` leaves `EAX` alone, so the
decompiler's `void` signature is wrong. Four statements: motor slot 18 `MoveFacing` on the goal
first; `flIdealSpeed = GetCurSpeed()` (`0x102e12c0`, whose whole body is `JMP [[owner]+0x3e0]`, the
owner's slot 248 `GetIdealSpeed`); `step = (|m_vecVelocity| + flIdealSpeed) * m_flMoveInterval *
0.5`, with the 0.5 a **double** the x87 widens; then `0x102e1560(goal, speed, step, arg2, arg3)`.
The last two arguments are slot 19's **own** second and third stack words — the `AIMoveTrace_t` out
param and the trace filter — which the decompiler mis-attributes as `param_1` and `param_2`.

`0x102e1560` is the walk. `step <= goal->maxDist` (`+0x28`) zeroes `m_flMoveInterval` (`+0x30`) and
leaves the step alone; otherwise the goal flag `0x2` (`+0x38`) zeroes the interval outright and
anything else scales it by `1.0 - maxDist/step`, and either way the step is clamped down to
`maxDist`. The polarity reads backwards until one remembers that `m_flMoveInterval` is the time
REMAINING: a step that fits inside the goal consumes the whole interval. Then `m_vecVelocity`
(`+0x3c`) is set per axis to `goal->dir * speed`. A step strictly above `_DAT_1044fab0` (**0.0**)
builds `end = slot 220 GetOrigin() + step * dir` and goes through `0x102e0bd0` with the goal's
expected blocker (`+0x34`); a **zero** answer dispatches motor slot 10 (`+0x28`) and returns 0,
anything else is returned verbatim. A step at or below the epsilon asks the LOCAL NAVIGATOR instead
— `(*(motor+0x10))->slot 6 (+0x18)(goal)` — and folds its answer to `1` / `0`.

**Unrecovered:** what motor slot 10 is, and what `0x102e0bd0`'s five trailing arguments mean past
the two this body supplies.

### `CAI_Motor::MoveGroundStep` `0x102e1760`

_Recovered 2026-09-14, story 29d._

Slot 20, three stack arguments — the `AILocalMoveGoal_t`, an `AIMoveTrace_t` out param and a trace
filter. Motor slot 18 `MoveFacing` first, on the goal. Then `m_vecVelocity = goal->dir *
GetCurSpeed()`, and `step = (sqrt(v dot v) + speed) * m_flMoveInterval * 0.5` over the velocity just
written. The clamp is slot 19's: `FCOMP; FNSTSW AX; AND EAX,0x4100; JNZ` tests C3 and C0, so
`step <= maxDist` zeroes the interval and leaves the step, and anything else scales the interval by
`_DAT_104454c0 (1.0) - maxDist/step` and clamps the step to `maxDist`.

The start point is **slot 220 `GetOrigin`** (`vt+0x370` on the outer at `+0x4`), not slot 217
`GetAbsOrigin`. `VectorMA(origin, step, dir)` builds the endpoint, a 14-dword (0x38-byte) record is
zeroed, and the hull trace `0x102e6d70` on the motor's `+0x68` fills it with kind **2**, mask
**`0x202400b`**, extent **100.0** and this body's third argument as the filter.

**The record is copied into the SECOND argument — the caller's `AIMoveTrace_t` — and the move goal
is never written back.** `EBX` is argument 1 and `EDI` is argument 2 and the listing reads them
separately (`MOV EDI,[ESP+0x78]` at `102e1890`). The copy runs only when that pointer is non-null,
and it writes `+0x00`, `+0x04`, `+0x08`, `+0x0c`, `+0x10`, `+0x14`, `+0x18`, `+0x1c`, `+0x20`,
`+0x24`, the `EHANDLE` at `+0x28` through its assignment operator, and `+0x34`. **`+0x2c` and
`+0x30` are not copied.**

The answer. `|trace->flTotalDist (+0x24) - step| <= 0.1` (`_DAT_104493d0`, a double; `TEST AH,0x41;
JP` makes it `<=`) opens the `4` / `0` pair — and **`return 4` additionally requires the goal's
expected blocker (`+0x34`) to be NON-ZERO** (`MOV EBX,[EBX+0x34]; TEST EBX,EBX; JZ -> return 0`)
before `trace->pObstruction (+0x1c)` is compared against it. A zero blocker or a mismatch answers
0. Otherwise the body calls `0x101cf5c0` on the outer with `trace+0x04` and flag 1, and answers
`1 + 2 * (trace->fStatus (+0x00) < 0)`.

**`0x101cf5c0` is `UTIL_SetOrigin`, not `NDebugOverlay::Line`.** Its body is
`entity->slot 62 (+0xf8) SetLocalOrigin(vec)` and then `PhysicsTouchTriggers()` when the third
argument is non-zero, and `../npc-kernel/layout.md`'s `m_vecGrappleSavedOrigin` row already names it
"the SetAbsOrigin helper". So the last arm of `MoveGroundStep` **moves the body to the trace
endpoint**; it is the step, not a debug draw.

The `AIMoveTrace_t` layout comes from `0x102e6d70`'s own initialiser, which writes by index before
it dispatches on the kind: `[0] fStatus = 0`, `[1..3] vEndPosition = *start`, `[4..6] vHitNormal =
vec3_origin`, `[7] pObstruction = 0`, `[9] flTotalDist = 0`; every arm answers `fStatus >= 0`.

**Unrecovered:** `AIMoveTrace_t`'s `+0x2c` and `+0x30`, and `0x102e6d70`'s fifth and seventh
arguments (both `0` at this call site).

### `CAI_Navigator::MoveNormal` `0x102efaa0` and its gate `0x102efd50`

_Recovered 2026-09-14, story 29d._

Slot 12 of `CAI_Navigator`'s own table, under a `"CAI_Navigator::MoveNormal"` scope-trace frame
that is pushed and popped on every exit. **It returns an `AIMoveResult_t`, not a distance.** The
gate's refusal is `MOV EAX,0xfffffffc` = **-4 `AIMR_ILLEGAL`**, which the decompiler prints as
`-NAN` only because it typed the return `float`; the ideal-speed arm answers `XOR EAX,EAX` = **0
`AIMR_OK`**; and every other exit is the enact's own answer, carried in `EAX` throughout.

In order. The gate `0x102efd50`: it reads the CURRENT route's nav type (`thunk_FUN_1030bc00` over
`nav+0x30`) and the navigator's own (`nav+0x18`); a GROUND route under a non-ground nav type
DevMsgs `"Warning: NPC appears to have wro…"`, runs `m_pMotor`'s slot 8 for nav type **1** or its
slot 5 for nav type **3** (and neither for any other value), and resets the nav type to 0 through
`0x102eeba0`; a route type of **2** under a nav type that is not 2 is the ONE refusal, and it
happens without the `0x102f13d0` call and without clearing `nav+0x51`. Everything else calls
`0x102f13d0(nav, 0)`, clears `nav+0x51` and passes.

Then navigator slot 16 (`+0x40`) is offered the move with a result **seeded to `-4`** before the
call; when it answers true that seeded-or-overwritten value is returned. Otherwise: owner slot 248
`GetIdealSpeed`, then `m_Activity` (`+0xfec`), `m_nSequence` (`+0x6f0`) and slot 217
`GetAbsOrigin()` are saved, in that order; the route's movement activity (`0x102ee3f0` =
`nav->+0x30->+0x2c`) is pushed through owner slot 310 `SetActivity`; and `GetIdealSpeed` is read
again — `speed <= _DAT_104454c4` (**0.0f**, and it is `<=`, not `<`) together with `m_Activity == 2`
answers **0**.

A 14-dword block and then a 31-dword block are zeroed, and **the second covers the first**
(`[E0-0x7c, E0)` contains `[E0-0x38, E0)`) — retail's own redundancy. Navigator slot 17 (`+0x44`)
builds the move info and navigator slot 15 (`+0x3c`) enacts it, with the block pushed LAST so it is
the first parameter and this function's own stack word the second. A non-zero answer is returned.

On `AIMR_OK` the restore runs, and it is **two independent tests**, not one: the first is on the
ideal speed SAVED before `SetActivity` (`FLD [ESP+0x14]` at `102efc11`) against `_DAT_1044e658`
(**the double 0.01**), the second on how far the body actually moved against the same constant.
Both are `TEST AH,5; JP`, a strict `<` with NaN taking the skip. Inside both, `m_nSequence` is
written DIRECTLY (not through a setter) and the saved activity is re-issued through slot 310. The
tail — navigator slot 6 (`+0x18`) when `nav+0x51` is clear — runs whether or not the restore did.

**Unrecovered:** what `m_pMotor`'s slots 5 and 8 are, what `0x102f13d0` does, what `nav+0x51` is
called, and the shape of the 31-dword block past what slot 17 fills.

### `CAI_BaseNPCTroika::OnObstructingDoor` `0x102984a0`

_Recovered 2026-09-14, story 29d._

Slot 531 on the Troika line; the `CAI_BaseNPC` line carries `0x1027dc80` at the same slot and
neither calls the other. `bool OnObstructingDoor(AILocalMoveGoal_t*, CBaseDoor*, float distClear,
AIMoveResult_t*)`.

**`m_hOpeningDoor` is `+0x5d24`.** `LEA EBP,[EDI+0x5d24]` at `102984e7` is the handle this body
reads and writes; `+0x644c` is `m_eAlternateAI` and `+0x6450` is `m_flAlternateAIExpireTimer`.

A NULL door `DevMsg`s `"**WARNING** No door given to OnUpcomingDoor() **WARNING**"` — the retail
NAME in the literal is `OnUpcomingDoor` — and answers false. The gate is
`moveGoal->maxDist (+0x28) >= distClear`, and because it is `FCOMP; TEST AH,5; JNP` an UNORDERED
compare also runs the body. Every exit not named "true" below answers FALSE.

1. The door already in `m_hOpeningDoor` → `*result = 0`, `0x100f0e70(door)`, **true**.
2. The squad's focus door (`GetSquadFocus 0x103166b0` over `+0x5da4`, under
   `m_iSquadDisconnected (+0x5bb0) <= 0 && m_pSquad != 0`) → `*result = -2`,
   `0x100f0e90(door, 1)`, **true**.
3. Slot 464 `GetState() == 4` (`NPC_STATE_SCRIPT`) with slot 513 `CapabilitiesGet()` NOT carrying
   the full `0xd00`: when `distClear < _DAT_10451acc` (**64.0f**, a strict `<`) it runs
   `StopScheduledMove 0x102bf770`, sets `m_eAlternateAI = 4`, `m_flAlternateAIExpireTimer = curtime
   + _DAT_10449258` (**3.0f**) and `m_hOpeningDoor = door->GetRefEHandle()`; and **either way**
   `*result = 0`, `0x100f0e70(door)`, **true**.
4. `0x1027f550(this, door)` refusing → `*result = -2`, `0x100f0e90(door, 4)`, **true**.
5. `TEST AH,0xd` — **ANY** of `0xd00`, not all of it. With none set the body answers FALSE having
   written nothing at all.
6. The door's slot 246 (`+0x3d8`) is asked for a nav position with `(door->+0x4f8 == 2)` as its
   third argument. A `-1` answer writes `0` **through the RESULT pointer** — `MOV EDX,[ESP+0x3c]`
   at `10298725` is argument 4, not the move goal the decompiler names — clears the door and
   answers **true**.
7. Otherwise `CAI_Pathfinder::BuildLocalRoute` (`0x10304130`, the VProf scope at `0x10611514`
   names it) is asked from slot 217 `GetAbsOrigin()` with flags `0x30`, hull `-1`, `1`, `0.0`, `0`.
   FOUND stamps `door->GetRefEHandle()` into `waypoint+0x24` and splices at `nav->+0x30 + 0x24`
   through `0x10319f30`; a FAILED splice falls straight out with **false** and no further write,
   and a successful one sets `m_hOpeningDoor`, sets `m_bOpeningDoorWait (+0x5d30) = (door->+0x4f8
   == 2)` and writes **`moveGoal->maxDist = distClear`** (`MOV [ECX+0x28],EAX` at `10298722`, with
   `ECX` argument 1 and `EAX` argument 3) before falling into the arm-6 exit.
   NOT FOUND gives up with `0x100f0e70` and **false** for `door->+0x4f8` of 0 or 2; for any other
   value it sets `m_hOpeningDoor = door` and tries `0x10298840`, whose FAILURE writes `-2` with
   `0x100f0e90(door, 0x40)` and answers **true** and whose SUCCESS resets `m_hOpeningDoor` to
   `0xffffffff`, calls `0x100f0e70` and answers **FALSE**.

The two door helpers are one word each: `0x100f0e70` is `door->+0x644 = 0` (eleven bytes) and
`0x100f0e90` is `door->+0x644 |= bits`. `0x1027f550` is four arms — a null door answers false with
**no** flag write; `(CapabilitiesGet() & 0xd00) != 0xd00` ORs `0x8`; `door->+0x640 > curtime` ORs
`0x10`; otherwise true.

**Unrecovered:** what door slot 246 is called, what `+0x644`'s bits `0x1`, `0x4`, `0x8`, `0x10` and
`0x40` are NAMED, and the waypoint record `0x10304130` returns past its `+0x24`.

### `CAI_StandoffBehavior::TranslateActivity` `0x102c79e0`

_Recovered 2026-09-14, story 29d._

Slot 22 of the behaviour's own table. `this+4` is the owner NPC and `this+0x1c` is the posture word
family Lifecycle's `FStandoffWords::Posture` carries. Arms in retail's order:

1. The owner's `m_pHintNode` (`+0x5ddc`) existing with `m_nHintType` (`+0x5dc`) == `0x65`: owner
   slot 569 (`+0x8e4`) is read into a local, read **again** to replace an incoming activity of `1`,
   and then `+0x1c = 2` when `+0x1c` was `0` **and the FIRST read was 8**.
2. `+0x1c == 2`: activity `1` answers `8`; activity `9` answers `0x12` when
   `SelectHeaviestSequence(owner, 0x12, -1)` is non-negative.
3. `+0x1c == 1` with activity `8` answers `1` (the listing returns with `EAX` still holding
   `[ESI+0x1c]`).
4. `+0x1c == 3` with activity `8` or `1`: `DAT_10925390` when `weapon_smg1` (`0x10601ef8`) is owned
   and its sequence resolves, else `DAT_10925388` when `weapon_pistol` (`0x10601ee8`) does, else
   `0x1027e590(owner, "NPC in standoff lacks needed low aim activity (%s)", weapon ?
   GetClassname() : "no weapon")` — the literal at `0x10601e9c` is **low aim activity**, not "low
   cover animation", and the `"no weapon"` fallback lives at `0x10601edc` — and answers `1`.
5. Anything left tails to `CAI_Behavior::vfunc22`.

**Unrecovered:** the VALUES of `DAT_10925390` and `DAT_10925388`. Both live past the end of the
file's raw `.data`, so they are registered at runtime and the image does not carry them.

### `CAI_StandoffGoal`'s two inputs `0x102c87a0` / `0x102c8830` and `UpdateOnRemove` `0x102cdc50`

_Recovered 2026-09-14, story 29d._

Slots 241, 243 and 180 of a 246-slot `CBaseEntity`-line goal entity. The two inputs are **one
clamp**, byte for byte, differing only in the backing routine they end on: `m_aggressiveness`
(`+0x484`) outside `[0, 4]` and not the sentinel **5** `DevMsg`s
`"Invalid aggressiveness value %d"` with the PRE-clamp value, a negative clamps to 0 and a value
over 4 clamps to 4, and every path calls the backing routine **exactly once** — the negative arm
returns from inside the warning block after calling it, and the tail calls it on every other path.

`CAI_GoalEntity::InputActivate` (`0x102cd650`) returns with NOTHING done when `m_flags` (`+0x480`)
bit `0x1` already stands; otherwise it inserts into the global goal list `DAT_106eb5d8` through
`0x100f6d80` (intrusive node at `+0x450`), resolves the actors once through `0x102cd4a0` and sets
bit `0x2` or refreshes them through `0x102cd3b0` when `0x2` already stands, sets bit `0x1`, and
dispatches `EnableGoal` (vtable `+0x3d0`) for each of the `+0x468` actor handles, count `+0x474`,
each validated through `PTR_DAT_10566458`. `InputDeactivate` (`0x102cdb70`) is the exact inverse:
nothing unless bit `0x1` stands, the same resolve-or-refresh, CLEAR bit `0x1`, `DisableGoal`
(`+0x3d4`) per actor, and the list removal (`0x100f6e40`) **last**.

`UpdateOnRemove` tests bit `0x1` of `+0x480` and, when it stands, builds a 0x20-byte `inputdata_t`
on the stack and dispatches the goal's **own** slot 243 with it. **Only the `variant_t` inside the
block is initialised**, and it is the inlined `variant_t` constructor: `block+0x08` (the union's
`iVal`) `= 0`, `block+0x14` (`eVal`) `= -1`, `block+0x18` (`fieldType`) `= FIELD_VOID`. The `-1` is
at `+0x14`, not `+0x0c`: the `MOV dword ptr [ESP+0x1c],0xffffffff` at `102cdc72` is issued AFTER
the `PUSH ECX`, which shifts its address by four. `pActivator` (`+0x00`), `pCaller` (`+0x04`) and
`nOutputID` (`+0x1c`) are left **uninitialised** — three words of stack garbage, which ships
because nothing this body reaches reads them. `CBaseEntity::UpdateOnRemove` is the tail either way.

**Unrecovered:** what the actor list's entries are used for past `EnableGoal` / `DisableGoal`, and
what aggressiveness value `5` means.

### `CAI_BaseNPC::SetHullSizeNormal` `0x10273070` and `SetHullSizeSmall` `0x10273180`

_Recovered 2026-09-14, story 29d._

The normal body runs its self-check **unconditionally and before the gate**:
`NAI_Hull::Bits(m_eHull)` (`0x102d6210`, `PTR_DAT_1060a750[hull][0]`) is called **twice**, so the
test is `(bits & NAI_Hull::GetUsedHullBits()) != bits` — "this body's hull was never precached" —
and the six-line ERROR block names the entity through `GetDebugName` and the hull through
`NAI_Hull::Name` (`0x102d6230`, the same record's `+4`). Then, when `m_fIsUsingSmallHull`
(`+0x5f2d`) is SET **or** the force byte is non-zero: `UTIL_SetSize(this, mins 0x102d6100,
maxs 0x102d6120)` — the listing EVALUATES maxs first and mins second and then pushes mins before
maxs — `m_fIsUsingSmallHull = 0` (the `MOV` at `1027312a` sits before the physics `JZ`, so the clear
happens on both arms), and `SetupVPhysicsHull` (`0x10272f40`) only when `+0x36c` is live. `RET 0x4`
with no value: the body is `void`, and its nineteen direct callers make it the widest-called body of
this band.

The small twin has no self-check, its gate is inverted (`m_fIsUsingSmallHull` CLEAR **or** forced),
it sizes to `0x102d6140` / `0x102d6160`, sets `+0x5f2d = 1`, rebuilds the same way — and **returns
1 unconditionally**, including on the path where the gate refused and nothing changed.

**Unrecovered:** the hull table `PTR_DAT_1060a750` itself, and therefore every hull's bits, name and
extents.

### `CAI_TestHull::Spawn` `0x102d72f0`

_Recovered 2026-09-14, story 29d._

Slot 103 on `CAI_TestHull`, read from the listing (the decompiler's jump-table warning is the tail
`JMP` to slot 66). The hull pick: read `NAI_Hull::GetUsedHullBits()` (`0x102f9950`, a one-line
`return DAT_10610be8`), then **`TEST EBX,EBX; JLE`** — a **signed** test, so a zero OR negative mask
short-circuits to hull 0 *without* the fallback call. The shipped `.data` initialiser for that mask
is `0xffffffff`, which is exactly such a value; the mask's two writers are `0x102f9900` (clear it,
and `DAT_1093412c` with it — that is where the runtime `0` comes from, at the start of the
node-graph build) and `0x102f9920` (`|= bits`). A positive mask walks `i = 0 .. 21` and takes the
first `i` whose `NAI_Hull::Bits(i)` intersects it; after 22 misses it calls `AddUsedHullBits(0)` — a
no-op OR, performed anyway — and answers 0.

Then, in order: `m_eHull` (`+0x1568`) `= i`; `SetHullSizeNormal(false)`; `SetSolid(SOLID_BBOX = 2)`
on `m_Collision` (`+0x270`) under a `"CBaseEntity::SetSolid"` scope-trace frame;
`AddSolidFlags(zero-extended word[+0x2b4] | 4)` — retail reads the current 16-bit solid-flag word,
ORs `FSOLID_NOT_SOLID` in and passes the whole thing back to a function that ORs it again — under a
`"CBaseEntity::AddSolidFlags"` frame; slot 93 `SetMoveType(MOVETYPE_FLY = 4, 0)`; `m_iHealth`
(`+0x210`) `= 0x32` = **50**; `AddFlag(0x40000)`; `byte [+0x5f44] = 0`; and a **tail** `JMP` to slot
66 `Hide()`, so `Hide`'s answer is `Spawn`'s and nothing runs after it.

**Unrecovered:** what the byte at `+0x5f44` is — `../npc-kernel/layout.md` binds that offset to an
output block, which a one-byte zero cannot be — and what `DAT_1093412c` holds.

### `CAI_BaseNPC::GetShootTarget` `0x10278650`

_Recovered 2026-09-14, story 29d._

No slot, four direct callers. The checklist's row named this "the standoff anchor"; it is the
**shoot target**, and the reading that settles it is the call at `102786f1`–`10278709`: slot 541
(`vt+0x874`) is `GetEnemies()` and takes **no** arguments, so the two pushes bracketing it belong to
the next call, which is `CAI_Enemies::GetLastKnownPosition(&lkp, enemy)` (`0x102dfed0`, whose own
`DevWarning` reads `"Asking LastKnownPosition for ene…"`). The enemy comes from slot 167
`GetEnemy()` (`+0x29c`), the offset from `enemy->slot 197 BodyTarget(posSrc, bNoisy, bFlag2)`
(`+0x314`), and the cached handle at `+0x5ba8` the body short-circuits on is the one
`../npc-kernel/layout.md` already calls `m_hShootTargetOverride`.

`Vector GetShootTarget(const Vector& posSrc, bool bNoisy, bool bFlag2)`, in retail's order:

1. `m_hShootTargetOverride` resolving answers **that entity's `GetAbsOrigin()`** and ignores all
   three arguments. The handle is validated against `PTR_DAT_10566458` twice — once for the gate and
   once to resolve — which is one redundant read and no behaviour.
2. No enemy: `AngleVectors(GetAngles(), &forward, NULL, NULL)` (slot 221 `+0x374`, which returns a
   `const QAngle&` and so takes only the hidden pointer, then `0x10139610` with
   `forward = (cos(pitch)cos(yaw), cos(pitch)sin(yaw), -sin(pitch))`), and the answer is
   `forward + posSrc`.
3. An enemy: `lkp + (BodyTarget(posSrc, bNoisy, bFlag2) - enemy->GetAbsOrigin())`, with the body
   target's **Z first raised by `_DAT_104994e0` = -30.0f** — a negative offset, so the target moves
   DOWN — when the enemy's player record (`+0x9c`, itself gated on its own `+0xa8`) carries a
   `CVStatList_t` of type **3** in its `+0x13bc` / `+0x13c0` table whose `GetValue(0xb)`
   (`0x102012d0`) answers exactly **5**. An entity with no type-3 list falls back to the lazily
   built EMPTY global `DAT_109f0b40`, whose `GetValue` answers 0.

**Unrecovered:** what stat id `0xb` is, and what the value `5` means. Family Sounds10 records the
same `CVStatList_t` join as unrecovered for `FireBullets`' ranged skill.

## Story 29e, family Translate19 — slot 440 `0x102cc080` / `0x102b12f0` / `0x102b11c0` (2026-09-14)

`CAI_BaseNPC::TranslateSchedule` (`0x102cc080`) is identity for every id except `0x2e SCHED_AISCRIPT`.
On `0x2e` it resolves `m_hCine` (`+0x5d74`); a dead cine `DevWarning`s, runs `CineCleanup`
(`0x1027d170`) and **calls** slot 440 with `1` (a `CALL dword ptr [EAX+0x6e0]`, not a tail jump, so
a species class re-enters its own body). A live cine switches on `m_fMoveTo` (`cine+0x5f60`): 0 and
4 → `slot440(0x32)`, 1 → `0x2f`, 2 → `0x30`, 3 → `0x31`, 5 → `0x33`, `> 5` → identity. The fall-out
path leaves `EAX` as `param_1`.

`CAI_BaseNPCTroika::TranslateSchedule` (`0x102b12f0`) runs the frenzied pre-table (`0x102b11c0`)
only when `m_bfNPCFrenziedFlags & 0x100`. The `1` / `0x6b` arm is
`(-(uint)((m_bfAINPCFlags2 & 0x80000) != 0x80000) & 0xffffff39) + 0x132` — `0x132` when
`D_MILDLY_CRAZY` is set, else `0x6b`. Rows: `2→0x46, 3→0x47, 6→0x4a, 0xf→0xb1, 0x10→0xb7, 0x15→0xb8,
0x21→0xed, 0x22→0xee, 0x25→0xc1, 0x28→0xc2, 0x2f→0xf2, 0x30→0xf4, 0x31→0xf6, 0x32→0xf8, 0x33→0xf9`;
`0x77→0x78` on a live hint whose type is `0x2774`; `0x94→0x95` and `0x96→0x97` each dispatch slot
293 twice.

`0x102b11c0`: `0xc7→0xc9`; `0xca/0xcb/0xd1/0xd2→0xcc`; `0xef→0xf0`; `0x87`/`0x88` test the
**complement** of `MADE_HUNT_PATH` (`+0x14b8` bit `0x1000`): bit set → `0x7e`; bit clear with an
enemy → `0x7c`; bit clear with none → `0x7d`. Default `0`.

The twenty species overrides are 20 distinct bodies (Chang brothers share `0x1036b460`, Rat/Scurrying
share `0x103ac490`). Dispatch keys on the address. Of every target, the one registered program here
is Troika `0xf → 0xb1 SCHED_TROIKA_CHASE_ENEMY`; everything else goes through story 25's miss arm —
retail hands the NUMBER to slot 446 `SetSchedule` (`0x102cc1f0`), whose miss `DevMsg`s "No CASE for
Schedule Type %d!" and installs the literal `1 IDLE_STAND` **untranslated** (`102cc227` / `102cc229`).

Four species bodies are more than a table. `CNPC_VAsianVampire` (`0x10362910`) **calls**
`GetJumpSchedule` (`0x10362430`) for `0xe5..0xe6` at `10362972` and returns its answer — Troika is
never reached. `CNPC_VBach` (`0x10363a30`) is not a table at all: it splits on `param_1 < 0xee`, and
its katana arm (`10363a93`) is four gates in order — a live active weapon, its classname equal to
`"item_w_katana"`, `SelectWeightedSequence(0x10, -1) != 0` (so **sequence index zero refuses**,
unlike every other sequence probe), and `STOP_BACKUP 0x2c` CLEAR — before answering `0x160`.
`CNPC_VHengeyokai` (`0x1037ffa0`) has a side effect: translating any id but `0x16e` with
`m_nSkin +0x670 == 1` runs `0x10383130`, so *merely translating a schedule thaws a hengeyokai*.
`CNPC_VZombie` (`0x103df580`) prints `npc_zombie: encountered schedule:[investigate unknown]
...ignoring!` (`0x10665640`) on its `0x5b` row while translating it anyway. `CNPC_VGuard1`'s crazy
offset is `0x28` where `CNPC_VCop`'s and `CNPC_VHunter`'s is `0x29`; the `0xc7 → 0xc8` rows on
`CNPC_VMingXiaoTentacle`, `CNPC_VTzimisceHeadClaw` and `CNPC_VTzimisceRunner` SHADOW the frenzied
table's `0xc7 → 0xc9`, because a species body is the entry point and answers before Troika is ever
reached; `CNPC_VSheriffMan` (`0x103b0320`) has no translation row at all.

**Unrecovered:** `CCineNPC::m_fMoveTo` (`cine +0x5f60`), the selector `0x102cc080`'s live arm
switches on — this runtime's scripted-sequence record has no such column, so the selector answers 0,
which is retail's own "no move" arm (shared with 4). The cine itself is NOT a seam: `m_hCine`
(`+0x5d74`) is bound to `FElysiumEntity::ScriptOwner` and read through `ScriptOwnerIsLive()`.
`CNPC_VTzimisce` (`0x103bd390`) and `CNPC_VWerewolf` (`0x103d5e00`) stamp their own
`__FILE__`/`__LINE__` into `+0x1b30`/`+0x1b34` before answering and the default arm writes neither —
the pair records "this class decided". The shape map calls it ABSENT; the mind's transition trace
carries the same account.

