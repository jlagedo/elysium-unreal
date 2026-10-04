# NPC AI — The schedule kernel

Part of the [NPC AI oracle](./README.md). Sections moved verbatim from
`npc-ai-reverse-engineering.md` on 2026-09-13; their headers are the citation keys.

## Schedules and tasks: the behavior program

### Schedule seam integration (0019/3 D–E, 2026-09-22)

All 691 running programs now come from the corpus; the C++ program-provider API is deleted.
The four registered base names without texts remain without texts. `elysium.schedules [filter]`
reports the census and each unported task's reference count, largest first.

`GetScheduleOfType 0x102cc260` converts through the RECEIVING NPC's slot-580 space, and slot 447
uses the same space in reverse (`0x102ea2d0` / `0x102ea280`). The runner now does both. The old
enum whitelist is gone: slot 440's answer is preserved, and only the manager lookup can miss.
In particular the chase-failure text's `STANDOFF 0x25` translates through `0x102b12f0` to the
loaded Troika `0xc1`, not `IDLE_STAND`.

Re-read `SetCondition 0x10269a20`, `HasCondition 0x10269aa0` and `ClearCondition 0x10269b50`:
local IDs go through slot 580's condition space at `+0x30`, then `global - 1000000000` selects
the bit. The port retains its class-local live cognition representation and converts at the
schedule boundary: positive masks to local for overlays/intersection, live conditions to global
for the inverted test. This is an internal representation difference, not a gameplay change.
An inverted foreign-class bit still fires on absence. Base IDs coincide; species IDs do not:
werewolf teleport is local `0x77`, currently global ordinal `0x8f`. The regression executes
`SCHED_VWEREWOLF_FAIL`'s interrupt against the local producer.

`TASK_SET_NPC_FLAG 0x100`, arm `0x102a585d`, routes the raw operand via its sign bit to
`0x102a97a0` (word one) or `0x102a9800` (word two, marker stripped). The regression executes
`SCHED_TROIKA_COMBAT_WAIT`: `TASKS_FACE_ENEMY` must not write `FINDING_BODY` in word one.

**Executable witness:** task `0x58` enters base `0x10283558`; Troika forwards it at `0x102a77e2`.
Threat is enemy or self. `FindLateralCover 0x102784a0` tests origin, then five left/right pairs,
48 Source units per step (`0x10447ee8`), with Z unchanged. `TestLateralCover 0x10278220` orders
blocked sight, slot 548 with NULL hint, clear MoveLimit, then a type-4 run goal with tolerance
-1. Success stamps `m_flMoveWaitFinished = curtime + operand` and completes. The node fallback
`0x102edc80 -> 0x10301720` gets threat origin/eye, minimum zero and slot-550 radius. No node
fails with 8; a node submits a type-6 run goal with tolerance -2 (hull width), keeps its hint's
arrival activity/direction and stamps the deadline. `SetGoal -> FindPath 0x102f1dc0` owns route
submission's task completion/failure, as documented below.

The lateral arm is wired and the witness executes its twelve tasks, including movement and the
final wait; a second run exercises no-cover failure and translated standoff. **Unimplemented
world input:** `IElysiumNpcMotor::FindNodeCover` answers nothing until 0018/9 ports the cover
search over 0018/4's place set (places and the cooldown storage landed 2026-09-29) and 0018/8's
hint claims. No invented point replaces a node. Task-body coverage does
not claim every world input is implemented.

**Named geometry modernization:** lateral stand/movement probes use Unreal's current NPC
capsule overlap/sweep, while retail uses its box and MoveProbe. Candidate order, 48-unit steps,
hint-group rejection and deadlines stay retail's. The shared sight service keeps its existing
documented gap: brush sight signatures are live, character occluders are not yet wired.

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
passes this mode into the retail loop: it skips the full gather and caps completions at one.
`[VtMB]`

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
schedules"`). It feeds 64 blobs (corrected 2026-09-21 from 63: the pointer table `0x106034b8` has 64 cells, § "The schedule owners and their registrations") to the same parser (`0x1030d850`, class name `"CAI_BaseNPC"`)
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
schedule with zero tasks (`"ERROR: Missing or invalid schedule"`, then **`SetActivity(ACT_IDLE)`**
through slot 310 / vtable `+0x4d8`, listing `0x1028227e..0x10282280`) — story 25c. The older
`SetState(1)` wording was a slot-name error; this exit does not touch either state word.

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

**`SetGoal` DOES complete the task — corrected 2026-09-21.** This paragraph used to say the
opposite and sent three stories looking for a base `RunTask` path arm that does not exist.
Navigator `SetGoal` (`0x102ecd20`) returns a `char`; a third argument of `2` (the patrol arm) or
`0` (the base arms) only changes whether it clears the goal entity / re-paths on failure (`& 4`).
But `SetGoal` calls the find wrapper `0x102f1dc0`, and that body decides the task:

1. **Find succeeded** (`DoFindPath 0x102f2330` true): clear the retry bit (`m_afMemory +0x5d8c &=
   ~0x20`), ask NPC slot 529 (`+0x844`, `0x10280330`) whether the CURRENT task is one of `0x6e`
   `WAIT_FOR_MOVEMENT`, `0x0b` or `0x72` (by the SDK's `IsCurTaskContinuousMove` these are
   `MOVE_TO_TARGET_RANGE` and `WEAPON_RUN_PATH` — names inferred, numbers read); if it is not, call
   the navigator's slot 2 (`+0x8`, `0x102623c0`), which is `TaskComplete(outer)` (`0x10273e80`).
   So every bare-`SetGoal` path task **completes synchronously, inside `SetGoal`, at route
   submission** — not at arrival, and not in any `RunTask` arm.
2. **Find failed, no retry word** (`nav+0x40 == 0`): navigator slot 10 `OnNavFailed(0x0c, 1)`
   (`0x102eeae0`) → NPC slot 448 `TaskFail(0x0c)`, movement stopped.
3. **Find failed, retry word set**: `m_afMemory |= 0x20`, deadline `nav+0x48 = curtime +
   nav+0x40`, next try `nav+0x4c = curtime + nav+0x44`; return false with the task neither complete
   nor failed — it sits RUNNING. Re-entered with the bit set: past the deadline → `OnNavFailed(0x0c,
   1)`; past the next-try time → find again, and a success completes the task unless it is `0x6e`.

The Troika arms that follow `SetGoal` with their own `TaskComplete` / `TaskFail(0x0c)` (patrol
point, interesting place, kick prop, `GET_PATH_TO_ENEMY_LKP 0x10` at `0x10284470`, the cower
task's hint branch at `0x102a2a4b`) are therefore completing a task that is usually already
complete; the base `GET_PATH_TO_*` arms (`0x1c` `GET_PATH_TO_LASTPOSITION` `0x10285bb0`, `0x20`
`GET_PATH_TO_BESTSOUND` `0x10285df8`, `0x15` `GET_PATH_TO_TARGET` `0x10285949`, `0x16`
`GET_PATH_TO_HINTNODE` `0x10285a9e`) rely on it alone. The Troika `RunTask` byte table `0x102ac840`
maps every one of those ids to its forward-to-base entry, and the base `RunTask` (`0x10288780`)
has no case for any of them. Their own fail codes, before `SetGoal` is reached: no target
`TaskFail(1)` (`0x10285980`), no hint `TaskFail(4)` (`0x10285aaa`), no best sound `TaskFail(0x12)`
(`0x10285e08`), no cover for the cower node `TaskFail(0x18)` (`0x102a2bad`).

**The move is a separate task.** `RUN_PATH 0x22` (`0x102863f1`: activity `0x13`, else `9`) and
`WALK_PATH 0x23` (`0x10286438`: `0x22`, else `9`, else `0x13`) set the movement activity, clear
`MEMORY:INCOVER` and complete at once. `WAIT_FOR_MOVEMENT 0x6e` (start `0x10286749`, run
`0x10288f43`) is the only one that waits: goal type (`path+0x5c`, `0x102ee620`) zero → clear
`m_bShouldMove`, `TaskComplete`, clear the goal; a goal with a current waypoint (`path+0x24`,
`0x102ee6a0`) → keep moving and `ValidateNavGoal` (slot 528, `0x10280360`); a goal with no
waypoint → stop the activity and wait. There is no "arrived" flag; a failed move leaves through
`OnNavFailed 0x102eeae0`, never through the completing branch. UNRECOVERED: the `TASK_WAIT_PVS`
base arm's relation to Troika's (`0x102aad7e`, spawnflag bit 10 or `0x102c2430` → complete at
once).

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
`COND_PROVOKED` and `COND_GIVE_WAY`.

### Maintain19 completion (2026-09-14)

The two Troika schedule-change entries compose in a surprising but explicit order. `0x102ae750`
translates the raw number through slot 440 and resolves it through `GetScheduleOfType`, then passes
the schedule pointer and force byte to `0x102ae780`. That body refuses without writes when either
`m_NPCState` or `m_IdealNPCState` is 7, then requires `IsAlive()` or a non-zero force byte. On
admission it calls `ForceScheduleChange 0x102ae490` first and base
`SetSchedule(CAI_Schedule*) 0x10280e50` second. Both bodies dispatch slot 435, so this entry makes
**two ordered `OnScheduleChange(newSchedule)` calls** before the new task state stands. `[VtMB]`
The base install then writes both retail clocks to `curtime`: `timeStarted +0x5c48` at
`0x10280e69` and `timeCurTaskStarted +0x5c4c` at `0x10280e73`. `MaintainSchedule` rewrites only
the latter when it starts each new task (`0x10281da2`). Both words now live independently in the
schedule state; clearing or replacing a schedule zeroes them before a new install stamps them.

`ForceScheduleChange`'s three warning blocks do not refuse the change: `!0x1028a190`,
`!IsAlive && !force`, and a live cine whose `m_interruptable +0x5f90` is clear each print and
continue. A live cine is then cancelled. If current and ideal state differ, the ideal selector
refresh runs unless ideal is DEAD, followed by `SetState(ideal)` in either case. Finally,
navigation other than `NAV_CLIMB` and `NAV_JUMP` clears `PRESERVE_PATH`, and slot 435 runs. The
port reads the scripted sequence's existing `bInterruptable` word through an entity-chain accessor;
it does not infer it from the separate queue-lock spawnflag. `[VtMB]`

The Troika slot-435 body now includes the previously omitted live-opening-door arm:
`0x102a09eb..0x102a0a07` resolves `m_hOpeningDoor +0x5d24` and calls slot 532 with reason 8 before
either flag mask. Slot 532 clears the opening-door handle and wait byte on every path, and its
Troika arm additionally ends the alternate door transaction where the reason permits. Deferring
this call leaves stale ownership visible after the replacement program is installed. `[VtMB]`
Its base body `0x1027a700` first calls navigator slot 4, whose retail body `0x102eea30` is an
empty `RET 4`, clears `m_flMoveWaitFinished +0x5db4`, and calls the already-ported
`VacateSquadSlot 0x1028ae60`. The last call currently takes its own no-squad refusal because the
substrate has no squad object, but it remains in the chain so that source can become live without
rewiring this body.

`TaskMovementComplete 0x10273ec0` first clears `m_bShouldMove`. Status 0 or 1 becomes 3; status 2
calls `TaskComplete(false)` and therefore becomes 4 unless `TASK_FAILED` stands; status 3 warns
`"Movement completed twice!"` without changing it; status 4 falls out unchanged. Script states
4, 5 and 6 retain their ideal activity, while every other state installs `GetStoppedActivity()`.
An active navigator goal receives `StopMoving` and every path then receives `ClearGoal`. The port
therefore carries the full five-value `fTaskStatus +0x5c44` in place of the former pair of
started/completed booleans. `[VtMB]`

SabbatLeader `TaskFail 0x103a9400` treats reasons 12 through 15 specially. Each increments
`m_RouteFailCount +0x66bc`; the pinned image stores **6.0f** at `_DAT_104c3cc0`
(`00 00 c0 40`), and retail converts the integer count to float for the comparison. Below six,
and for all other reasons, it chains Troika `TaskFail 0x1029adb0`. At six or above it flips
`m_FailureType`, installs `0x161` when the flipped value is non-zero or `0x160` when zero through
`0x102ae750`, and returns without the Troika chain. Consequently the flip arm neither raises
`COND_TASK_FAILED` nor runs the base failure cleanup. The two source-file/line stamps remain
ABSENT shape words. `[VtMB]`

`ElysiumSchedule::Tick` is now the single `MaintainSchedule 0x102817c0` loop. It re-runs validity
inside every iteration; handles special-navigation completion, `CHOOSE_NEW_SCHEDULE`, delayed
interrupts, state changes and the blocked-door retry; installs a selector's null answer as a real
null pointer before the one retry; calls `OnStartSchedule`, both task overlays and
`MaintainActivity` in listing order; writes memory bit `0x40000` on continuous movement; and
observes the 10/1 completion limit plus the recovered **8 ms** cycle budget (`_DAT_1049a148 =
8.0`). The door comparison follows the x87 flags: expired or equal drops the handle, future or
unordered latches it, and the consumed flags2 mask clears both `0x200` and bit 31
(`0x80000200`). The common exit alone writes `m_bDidMaintainSchedule = 1`; the `ai_step` return
and missing/zero-task error return do not.

The `GetNewSchedule 0x102814d0` adapter retains its own internal order. It first refreshes
`CacheInterruptConditions 0x1026a0f0` only while `m_flCacheInterruptTime +0x1b24 < curtime`, then
calls slot 433 only while the gathered marker is clear, calls the still separately owned slot-438
selector, converts that original answer through slot 580 / `0x102ea2d0` into the raw int32
`m_IdealSchedule +0x5c3c`, and finally sends the original answer through slot 440 and
`GetScheduleOfType`. The port now carries that ideal word as int32, so local ids, `-1` and global
ids at or above 1,000,000,000 are representable. The current slot-438 API still returns the typed
set of registered programs; Select19 owns widening its result to supply those raw forms. Maintain19
preserves the raw retail number of every currently supported selector answer and keeps the install
translation separate from that ideal stamp. The shape map previously bound **both** `+0x1b24` and Troika
`m_flInterruptTime +0x632c` to one `ScheduleHost.InterruptTime`; Maintain19 splits the first into
`CacheInterruptTime`, because the schedule-change reset of `+0x632c` must not silently force or
suppress the cache gate. The current program format has no authored inverse-interrupt column, so
the cached inverse mask remains empty; the positive authored mask plus slot 453 is complete.

`FElysiumNpc::MaintainSchedule` supplies the entity words to that loop. The former pre-loop
`UpdateIdealState` runtime call and the select/start/second-tick body in `ThinkStanceOrIdle` no
longer stand as separate execution paths. Program blobs remain outside this family; every absent
translated id still takes story 25's registry-miss path.

A completed program, a failed one and a state change all reselect inside the same `MaintainSchedule`
pass (`0x10281be5`, `0x10281c46`): `NextScheduledTask 0x10280f40` writes `COND_SCHEDULE_DONE`
and the loop falls into `IsScheduleValid` and the reselect block. Patrols and interesting places are
programs `SelectSchedule` case 1 answers (the patrol path's program, `0xff` / `0x100` / `0x102` /
`0x105` / `0x106`), so no family returns to an executor outside the interpreter. *(2026-10-04, V3b:
the port's external-executor return — a `SCHEDULE_DONE` latch and a slot-438 null adapter for a
`use_interesting` body that installed a null program and handed the body back to the ambient
executor — is deleted with the executor.)*

This port's `ThinkDead` reuses the same task interpreter only to finish its death clip, then performs
the ragdoll/final-frame handoff outside `RunAI`. It supplies a task-only runner which has no schedule
selector; completion therefore returns to that handoff without adding a DEAD-only exception to the
retail Maintain/reselection body. `FElysiumNpc::SelectScheduleForMaintenance` preserves the
ordinary live-selector zero as `GetScheduleOfType`'s literal-1 fallback.
The caller boundary follows the recovered death order: `CAI_BaseNPC::Event_Killed 0x10265ad0`
calls `CBaseCombatCharacter::Event_Killed 0x1032b9b0` at `0x10265cea`, whose first state write is
`m_lifeState = LIFE_DYING`; only afterwards does the NPC body stamp ideal DEAD at `0x10265d06`,
run its death-sound/physics decision, stamp DEAD again at `0x10265db0`, and call
`SetState(7)` at `0x10265dba`. The port's pre-existing `ThinkDead` owns that post-kill visual and
physics/final-frame boundary. The task-only adapter changes none of its outputs or polling cadence;
it only prevents that caller from pretending to be the live `RunAI` selector once the death clip's
single task completes. Live `RunAI` selection sends ordinary zero answers to the literal-1
registry fallback.

**Unrecovered:** the identity and producer of the profiler's process-wide cycle records; only the
observable 8 ms stop budget is needed by the port. The current program format has no input column
for retail's inverse interrupt mask, and the current typed slot-438 surface cannot yet return an
unregistered local/global numeric selector result; those inputs belong to the program and Select19
families respectively. Later task families still own task bodies absent from the current registry.

## Species slot-435 overrides all chain (2026-09-08)

`vtmb_slot 435`: 79 classes. `CNPC_VGargoyle` `0x10378fc0`, `CNPC_VHengeyokai` `0x10383090`,
`CNPC_VTzimisce` `0x103bf610`, `CNPC_VWerewolf::OnScheduleChange` `0x103ced10` **all call the
Troika body `0x102a0940` first**, then, gated on `PRESERVE_PATH` clear, decrement a per-species
shun counter (`m_iShunnedFindPillar` / `m_iShunnedFindFish` with `+0x6680 = 0` / `m_iShunnedFindBody`
with `m_ePathMode = 0`); the werewolf drops the oldest row only when its count already exceeds 50,
then appends, so the history settles at **51** entries at `+0x668c`. The port's final NPC leaf
dispatches these tails by `BodyOf(RetailClass(), 435)` and uses `FSpeciesDispatchScope` around the
direct Troika thunk. Retail stores the incoming **`CAI_Schedule*`**, not its numeric id. Its only
recovered consumer is Werewolf `DrawDebugStatOverlays 0x103d5130` at `0x103d5450`, which
dereferences the pointer only for its schedule name and prints `INVALID SCHEDULE` for null; the
port's existing `TArray<FString>` therefore carries the whole consumed representation. No history
reader compares schedule identity or inspects a task. Classes on the plain base
reset `0x1027a700` (no flag word): `CAI_BaseNPC`, `CAI_BaseHumanoid`, `CAI_ExpressiveNPC`,
`CAI_TestHull`, `CCineNPC`, `CCineAI`, `CCineAISchedule`, `CGenericNPC`, `CGenericSabbat_NPC`,
`CGeneric_NPC_bathack`, `CNPC_Bullseye`, `CNPC_Crow`, `CScriptedTarget`; `CGeneric_NPC`,
`CNPC_ProneDialog`, `CPayphone` and the makers carry the Troika body.

## The schedule host and the task surface, walked (2026-09-13, story 29c-1)

Family **Schedule** of story 29c-1: the 64 `rule` rows of `order.md` layers 0–9 whose behaviour is
the schedule host, the task surface and the melee/cover selectors. The port is
`Substrate/ElysiumNpcSchedule.cpp`, `Substrate/ElysiumNpcScheduleHost.cpp` and the two arms
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

`DAT_10924a6c` is the ConVar `ent_trace_conditions` — the static object at `0x10924a68` (registrar `0x1028bde0`: name `0x105d7ad0`, default `"1"`, help "When ent_trace is on, this will dump info about conditions also."), whose `+4` word is the pointer every condition setter reads a value through (slot 1) and discards: a debug-trace read, no game state (closed 2026-09-19).

### `0x102a18a0` — the `TASK_WAIT` deadline

`if (0.0 < task->flTaskData) m_flWaitFinished (+0x5db4) = curtime + flTaskData; else
m_flWaitFinished = curtime + _DAT_10447ee0;` — an operand at or below zero waits a retail default
rather than not at all. `[VtMB]`

`_DAT_10447ee0` = **1000.0** (a `.rdata` float, no writer; read 2026-09-20).

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

A four-entry jump table on `(int)param + 1000008` (`0x102bf738`). **Corrected 2026-09-21 — this
paragraph had the four rows reversed**; the table's dwords, read from the image, are index 0 →
`0x102bf71b`, 1 → `0x102bf711`, 2 → `0x102bf707`, 3 → `0x102bf6fd`, so: -1000008 -> the fixed
`_DAT_1044e664` (10.0, `FOLLOWER_DISTANCE_OVERLAP`), -1000007 -> `+0x648c` (`…_RUNTO`), -1000006 ->
`+0x6488` (`…_WALKTO`), -1000005 -> `m_flFollowerDistanceBackAway` (`+0x6484`, `…_BACKAWAY`) —
which is the order the parser's `DIST:` name table gives them (§ "The schedule-text parser
`0x1030d850`, walked"). Anything else falls to
`CAI_BaseNPC::ResolveTaskDistance` (`0x102702d0`), which tests the `__ftol` of its argument:
-1000003 (`0x1027030d`, `DIST:COMBATMOVE`) -> the melee-range ConVar `DAT_10924a1c` (0.0 when its
slot 1 answers true, else its `+0x28`, shipped 100); -1000002 (`0x10270303`, `DIALOG`) -> the fixed
`0x1047a3ac` (160.0); -1000000 (`0x102702f9`, `ACCUM`) -> `m_flSpecialDistanceAccum` (`+0x5bac`);
anything else -> **the argument as passed** (`0x102702f1 FLD [ESP+0x8]`, the float itself: only the
comparison uses the truncation). **Corrected 2026-10-04 (spec 0002 V11)**: this paragraph said the
pass-through answered "the truncated value".

Two species overrides sit in front of it and only then delegate: `CNPC_VMingXiao` (`0x10392a10`)
answers `m_flIdealRange` (`+0x6748`) for -1000004, `CNPC_VTzimisce` (`0x103b9120`) answers
`_DAT_10457f60` for -1000001. `[VtMB]`

`_DAT_10457f60` = **150.0** (a `.rdata` float, no writer; read 2026-09-20).
The base body's three answers, closed 2026-09-21 (`0x102702d0`): -1000000 `ACCUM` ->
`m_flSpecialDistanceAccum` (`+0x5bac`, `0x102702f9`); -1000002 `DIALOG` -> **160.0**
(`0x1047a3ac`, `0x10270303`); -1000003 `COMBATMOVE` -> the melee-range singleton `DAT_10924a1c`'s
slot 1 answer, zero or its `+0x28` (`0x1027030d`–`0x1027032d`). **Unrecovered:** nothing.

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
at `+0x28`), `_DAT_10451acc` (= **64.0f**, float32; read 2026-09-21, `rdata-cells.md`), `_DAT_104c3cd4` (= **120.0f**, float32; read 2026-09-21, `rdata-cells.md`) (SabbatLeader's `TOO_FAR_TO_ATTACK` bound), and the
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

## The schedule-text parser `0x1030d850`, walked (2026-09-21, 0019 story 3)

_A Codex worker's walk (`$ELYSIUM_WORK_ROOT/codex/re2/wp01-sched-parser`); the compare chain, the
`DIST:` and `MiscFlag:` resolvers, the flag table and the condition arm re-read by the lead against
the corpus and the image._ This is the body 0019 story 3 ports. The spec knew six operand forms;
there are **seventeen prefixes, four boolean words and a bare number**.

**Tokens.** The reader is the engine's own (`VEngineServer` slot 98, `+0x188`; engine side
`0x2003c800`): bytes `<= 0x20` separate tokens, `//` runs to end of line, double quotes group a
token with no escape processing, and each of `{ } ( ) ' :` is a one-character token — so
`NPCFlag:FORCE_RELAXED_ANIMS` arrives as three tokens and the parser tests the middle one against
`":"` (`0x106142ec`). Every keyword, prefix and value compare is `strcmpi` / `strnicmp`:
case-insensitive throughout.

```ebnf
text      ::= { record }
record    ::= "Schedule" name "Tasks" { task } [ "Interrupts" { [ "!" ] cond } ] [ "Flags" { flag } ]
task      ::= task_name operand
operand   ::= prefix ":" value | "TRUE" | "ON" | "FALSE" | "OFF" | number
```

`Tasks` is required; `Interrupts` and `Flags` are optional and in that order — a `Flags` met inside
the task loop is read as a task name and fails. A text whose first token is not `Schedule`
(including an empty text) returns SUCCESS and loads nothing. Every task takes exactly one operand:
a task followed directly by `Interrupts` or by another `TASK_…` token is the "Bad syntax at task
#%d" failure. A `!` before a condition writes the INVERTED mask instead of the ordinary one.

**The operand chain, in the order the body tests it** (`0x1030d9ce` … `0x1030e6b7`). The task
record is two 32-bit words, id then data. Resolvers returning an int are stored as that number
converted to float — except `NPCFlag:`, `MiscFlag:` and `Model:`, whose raw 32-bit word is stored
unconverted.

| # | Prefix | Resolver | Data word |
|---:|---|---|---|
| 1 | `Activity:` | `0x1025d760` over the activity registry `DAT_1090fbe0` | activity id |
| 2 | `Task:` | `0x10316fd0` (global task space) then local translation | class-local task id |
| 3 | `Schedule:` | `0x102cadb0` (global schedule space) then local translation | class-local schedule id |
| 4 | `State:` | `0x1030c600` | state id |
| 5 | `Memory:` | `0x1030c800` | memory mask |
| 6 | `Path:` | `0x1030ca70` | `TRAVEL 0`, `LOS 1`, `COVER 2` |
| 7 | `Goal:` | `0x1030cb00` | `ENEMY 0`, `TARGET 1`, `ENEMY_LKP 2`, `TARGET_LKP 3`, `SAVED_POSITION 4` |
| 8 | `HintFlags:` | `0x102d3f50` | substring search of the lowercased token: `none 0`, `visible 1`, `nearest 2`, `random 4`; nearest + random warns and reads as nearest |
| 9 | `NPCFlag:` | `0x1030cbd0` | raw flag word; the sign bit selects `m_bfAINPCFlags2` |
| 10 | `MiscFlag:` | `0x1030d390` over the 22 names at `0x10619ec8` | raw **index** 0–21, not a mask; an unknown name silently reads as 0 (`Unconscious`) |
| 11 | `Model:` | `0x1030d3d0` (symbol table `DAT_10936b74`) | raw 16-bit symbol id; the model is also precached |
| 12 | `SOUND:` | `0x1030d400` over the run-time `{id, name}` array `DAT_1073dc3c` / `DAT_1073dc40` | sound id |
| 13 | `EXPRESSION:` | `0x1030f5f0` | `FLINCH 0`, `KNOCKBACK 1` |
| 14 | `STO:` | `0x1030d480` | `DEFAULT 0`, `SHOOT_AT_HINT 1` |
| 15 | `DIST:` | `0x1030d4f0` | a negative sentinel slot 418 resolves at run time (below) |
| 16 | `MXTPHASE:` | `0x1030d650` | `TENTACLE 0`, `TENTACLE_TO_GRUB 1`, `GRUB 2`, `GRUB_TO_PROXY 3` |
| 17 | `TOMODE:` | `0x1030d710` | `NONE 0`, `PATHING 1`, `GRABBING 2`, `CARRYING 3`, `THROWING 4` |
| — | `TRUE` / `ON` | — | `1.0` |
| — | `FALSE` / `OFF` | — | `0.0` |
| — | anything else | `_atof` | the number; a non-number reads as `0.0` |

`State:` — `NONE 0`, `IDLE 1`, `COMBAT 2`, `ALERT 3`, `SCRIPT 4`, `PLAYDEAD 5`, `PRONE 6`, `DEAD 7`,
`FLEEING 8`, `RETREATING 9`, `COWERING 10`, `HUNTING 11`, `OBLIVIOUS 13`, `CRIMINAL_SUSPICION 14`.
**That table names the two states the oracle had as unnamed: `0xd` is `OBLIVIOUS` and `0xe` is
`CRIMINAL_SUSPICION`.** 12 has no name.

`Memory:` — `PROVOKED 0x1`, `INCOVER 0x2`, `SUSPICIOUS 0x4`, `PATH_FAILED 0x20`, `FLINCHED 0x40`,
`TOURGUIDE 0x100`, `LOCKED_HINT 0x400`, `TURNING 0x2000`, `TURNHACK 0x4000`, `HAD_ENEMY 0x8000`,
`HAD_PLAYER 0x10000`, `HAD_LOS 0x20000`, `INVESTIGATING 0x08000000`, `CUSTOM4 0x10000000`,
`CUSTOM3 0x20000000`, `CUSTOM2 0x40000000`, `CUSTOM1 0x80000000`. (`0x1033cb00` / `0x1033cb50`, which
0019's text credited with `MEMORY:` and `MiscFlag:`, are the separate misc name↔mask API and are
NOT called by the parser.)

`DIST:` (`0x1030d4f0`, a `strcmpi` chain, an unknown name is an `Error`) — `ACCUM −1000000`,
`TZIMISCE_CLAW −1000001`, `DIALOG −1000002`, `COMBATMOVE −1000003`, `MINGXIAO_IDEAL_RANGE −1000004`,
`FOLLOWER_DISTANCE_BACKAWAY −1000005`, `…_WALKTO −1000006`, `…_RUNTO −1000007`, `…_OVERLAP
−1000008`. These are exactly the sentinels slot 418 `ResolveTaskDistance` turns into distances
(§ "`0x102bf6e0` — `ResolveTaskDistance`, slot 418" above; `navigation-jump-links.md`): the name
table and the run-time enum are one vocabulary, and `social.md`'s "resolves like `NPCFlag:`" was
wrong about the mechanism.

`NPCFlag:`'s 62 names are `0x1030cbd0`'s chain, already tabled in § "The incapacitation tasks and the
NPC flag word"; the walk re-read it and agrees, `TASKS_FACE_TARGET` = word two bit `0x20`.

**Name resolution.** Schedule, task and condition names are looked up in the GLOBAL spaces first
(`0x102ea050` over `DAT_109203cc` schedules, `DAT_109203d4` tasks, `DAT_109203dc` conditions; the
global id is the ordinal plus 1,000,000,000). A task id and a `Schedule:` / `Task:` operand are then
translated to the class's local id by `0x102ea280` (the class's space for schedules, `+0x18` past
it for tasks), which walks the parent chain at `+0x10` — so a text may name a base-class schedule.
Because every init body registers ALL its names before it parses its first text, forward
references inside a class resolve. **Conditions are not translated**: the interrupt arm takes the
global ordinal, subtracts 1,000,000,000 and sets bit `ordinal & 31` of word `ordinal >> 5` in the
six-word mask at `+0x28` (or, after `!`, at `+0x00`). The masks are in GLOBAL condition ordinals.

**Failure.** Each of these returns false, and the calling init body stops loading that class's
remaining texts (`0x10367a40`, `0x102cb690`): a duplicate schedule name; an unknown schedule name;
a missing `Tasks`; a 65th task (64 are allowed); an unknown task; a missing operand; a missing `:`
after a prefix or a stray `:` after an operand; an unknown `Activity:` / `Schedule:` / `State:` /
`Memory:` / `Path:` / `Goal:` / `NPCFlag:` / `SOUND:` / `EXPRESSION:` / `STO:` / `DIST:` /
`MXTPHASE:` / `TOMODE:` / `Model:` value. They report through the import `Error` (tier0's fatal
spew); only the missing operand is a `Warning`. Two things do NOT fail the text: an unknown
condition (`DevMsg`, the bit is skipped) and the `Flags` section's diagnostics — `0x1030d7e0`
answers `DELAY_INTERRUPTS` → 1, `NONE` → 0 silently, and anything else → `Error` and 0; the parser
then `DevMsg`s "Unknown schedule flag" for every 0, so an authored `Flags NONE` prints that
diagnostic harmlessly. `MiscFlag:`, `HintFlags:` and a bare non-number never fail; they read as 0.
The schedule node is linked into the manager BEFORE its tasks are parsed and no error path unlinks
it, so a failed text leaves a task-less node behind (inferred from `0x1030c5b0` and the direct
returns).

**The record.** `0x1030c5b0` allocates `0x48` bytes and links the node at the head of the manager
list `DAT_10936b68`; constructor `0x1030f680`. `+0x00..+0x17` the inverted mask (six words),
`+0x18` flags, `+0x1c` the GLOBAL schedule id, `+0x20` the heap task array (`count × 8`), `+0x24`
the count, `+0x28..+0x3f` the interrupt mask, `+0x40` a heap COPY of the name, `+0x44` next. Lookup
by name is `0x1030f350` (`strcmpi` on `+0x40`), by id `0x1030f300` (on `+0x1c`, reached from
`0x102cc260`).

**Unrecovered:** the run-time contents of the `SOUND:` array (filled at load from the VSound table,
not static) and `Model:`'s symbol numbers (interned at run time); neither is a parser fact.

## The schedule owners and their registrations (2026-09-21, 0019 story 3)

_A Codex worker's walk (`$ELYSIUM_WORK_ROOT/codex/re2/wp02-sched-registration`); the caller census,
the base text table and the 691 sum re-checked by the lead._ This is what 0019 story 3's extractor
reads. The spec expected "20 owners"; the image has **57 callers of the parser (through its thunk
`0x10006398`): 56 class init bodies, 48 of which feed at least one text, plus one ownerless file
loader**. 20 is a true number for something else — the classes that register class-local TASKS
(184 task names between them).

**The two calls.** `CAI_LocalIdSpace::Init` (`0x102ea0e0`): `this` = the space, arg 1 = the global
namespace, arg 2 = the PARENT local space; it stores the namespace at `+0x14`, the parent at
`+0x10`, the global base at `+0x00`, the local base / top / translated top at `+0x04` / `+0x08` /
`+0x0c`. `Register` (`0x102ea130`, thunk `0x1000a948`): `this` = the space, then `(name, localId,
category, className)` with category one of `"schedule"`, `"condition"`, `"squadslot"`; it extends
the local range, translates the id (`0x102ea2d0`: `space[0] - space[4] + localId`, falling through
the parent chain) and inserts name → global id into the namespace (`0x102e9fe0`). Both answer
success in `AL`. A class's four spaces sit `0x18` apart: schedule, task (`+0x18`), condition
(`+0x30`), and the squad-slot space elsewhere in the class's statics.

**The call-site recipe.** The name and the local id are LITERAL IMMEDIATES in the init body — no
counter, no enum table. A species body builds stack-local pair vectors (`MOV [pair.name], imm32
string` / `MOV [pair.id], imm32` / `CALL 0x102c6a50` to append), sorts them, and only then loops
`PUSH className / PUSH category / PUSH localId / PUSH name / MOV ECX, space / CALL 0x1000a948`.
Texts are appended the same way (`MOV [local], imm32 text` / `CALL 0x102c6920`) and fed in a loop
`PUSH classScheduleSpace / PUSH text / PUSH className / MOV ECX, 0x10936b68 / CALL 0x10006398`,
breaking on the first false. So an extractor reads, per owner, the `imm32` pairs ahead of each
append call; it never needs to execute the body. Brujah, concretely: `0x10367ac2` name
`0x1062f4f0` "SCHED_VBRUJAH_WALK", id `0x158`; init `0x10367b3c`–`0x10367b8f`; registration
`0x10367c13`–`0x10367cfc`; parse `0x10367d1e`–`0x10367d53`.

**Order, inside one owner**: build the vectors; `Init` all four spaces; sort; register schedules,
then tasks, then conditions, then squad slots; run the activity-registration callbacks; parse. ALL
names are registered before the first text is parsed.

**The sort is by LOCAL ID and it is load-bearing** (measured 2026-09-22, porting this order). A
space takes its local base from its FIRST registration (`SetFirstLocal 0x102ea240`) and `Register`
refuses every later id below that base, so the order names arrive in decides how many of them
register at all. The append order is not ascending, in exactly **three** of the fifty-six owners:
`CNPC_VZombie` appends schedule `0x161` before `0x15e` and would lose `0x15e`–`0x160`,
`CNPC_VAndreiBlood` appends `0x15b` before `0x15a` and would lose one, and `CNPC_VWerewolf` opens
its conditions at `0x78` with `0x77` behind it. The cost is not those five names: the first text
that names a lost schedule is an `unknown schedule name` failure, and the owner then stops, so
those three classes lose every remaining text as well. The extractor reads the APPEND order,
because that is what the image states; a consumer replaying the recipe sorts before it registers,
as the body does.

**Order, across owners.** The base is rooted at `CWorld::Precache 0x1023c020` → `0x1030c560` →
`0x1030c4e0` (init the base spaces; schedules `0x102cadd0`; conditions `0x102c8ce0`; tasks
`0x10316ff0`; activities `0x1025d790`; the two global squad slots `0x10316e80`) and then
`0x102cb690`, the 64 base parses. Troika is once-guarded (`0x102b97a0` compares the manager
pointer it last loaded against, then `0x102b9810`). The species bodies are first-touch. A parent's
space is always initialised before a child parses, because the child's `Init` names it and
translation walks the chain; a SIBLING's private space is never searched.

**Three deviations from the Brujah shape, and only three.**
1. `CAI_BaseNPC` registers in three separate bodies (above) and feeds its texts from the one
   static pointer table in the image, **64 cells at `0x106034b8`..`0x106035b4`** (`FAIL`,
   `IDLE_STAND`, `WAIT_FOR_SCRIPT` … `DROPSHIP_DEPLOY_THIRD`, `SCHED_FLINCH_PHYSICS`), 64 calls
   from `0x102cb6a5` to `0x102cbe70` — not 63. `0x102cadd0` registers **68 schedule names, local
   ids `0x00`–`0x43` dense**, against those 64 texts, and it does so through **two** entry points:
   57 (`0x00`–`0x38`) through the five-argument `thunk_FUN_102ea130` (`0x1000a948`), then 11
   (`0x39`, `0x3b`, `0x3c`, `0x3d`, `0x43`, `0x3e`, `0x3f`, `0x40`, `0x41`, `0x3a`, `0x42`, in
   that order) through the four-argument `thunk_FUN_102bea80` (`0x10007068`), which supplies the
   `"schedule"` category itself. The second entry point is the SAME shared helper Troika uses
   (deviation 2 below), which is why Troika's range starts at 68 and not at 57 — an earlier read
   that counted only the five-argument thunk reported "57 names, where the other seven are
   registered was not walked"; there are no unregistered names. Four registered ids have no text.
   Three names the decompiler leaves unnamed are `NONE` = `0x00` (`0x1059a350`), `DIE` = `0x2b`
   (`0x10608528`) and **`FAIL` = `0x43`** (`0x10608410`). Base names carry NO `SCHED_` prefix
   (`IDLE_STAND`, `CHASE_ENEMY`, `FAIL`) except `SCHED_DIE_RAGDOLL` `0x2c` and
   `SCHED_FLINCH_PHYSICS` `0x42`; Troika's all do. **The base programs ARE text blobs**:
   `programs.md`'s "UNRECOVERED: `0x43`'s task list — the base programs are not text blobs" is
   wrong; `FAIL` is cell 0 (`0x10608238`), read whole 2026-09-21.
2. `CAI_BaseNPCTroika` does everything through helpers — spaces `0x102be9f0`, schedules
   `0x102bea80`, tasks `0x102beab0` (`space+0x18`), conditions `0x102beae0` (`space+0x30`), texts
   in two blocks (`158 × 0x102c6920 + 116 × 0x102c65d0`) — and registers 274 schedules (local
   68–341) and NOTHING else: every Troika task and condition is in the base spaces.
3. Classes SHARE spaces through a common slot-580 getter, and more of them than four pairs. The
   four pairs are real: `CNPC_VCamera` / `CNPC_VCameraSecurity` (`0x103683b0`), `CNPC_VVampire` /
   `CNPC_VPlayerController` (`0x103750e0`), `CNPC_VHumanCombatant` / `CNPC_ProneDialog`
   (`0x10386ae0`), `CNPC_VScurrying` / `CNPC_VRat` (`0x103abba0`), and the second of each has no
   init body of its own. **So do two larger groups** (read 2026-09-22, by walking the vtable of
   every class whose RTTI base chain names `CAI_BaseNPC` and reading slot 580 off each):
   `CGenericNPC`, `CCineNPC`, `CCineAI`, `CCineAISchedule`, `CAI_BaseHumanoid`,
   `CAI_ExpressiveNPC`, `CAI_TestHull`, `CNPC_Bullseye` and `CScriptedTarget` inherit
   **`CAI_BaseNPC`'s** getter (`0x101a6d00` / `0x102beb30` → `0x1090ff08`); `CNPC_VBaseBoss`,
   `CNPC_VNewscaster`, `CPayphone`, `CNPCMaker`, `CNPCMaker_Fleshpile` and `CNPCMaker_Zombie`
   inherit **Troika's** (`0x101aa790` / `0x102bec90` → `0x10924248`). **77 classes over 58
   distinct spaces**, against 56 init bodies: 21 classes have no body and run the vocabulary of the
   class above them. Three have a space of their own that no body registers into and so an empty
   vocabulary — `CGeneric_NPC` (`0x1093a1a8`), `CGeneric_NPC_bathack` (`0x1093a2c8`) and
   `CGenericSabbat_NPC` (`0x1093a3b0`).

**The two roots' own spaces.** Neither root initialises its spaces the way a species does, which is
why a walk that reads only init bodies leaves both — and the ten species that parent on Troika —
with no parent at all.
- `CAI_BaseNPC`: `0x1030c4e0` calls `Init` three times with literal operands,
  `0x1090ff08` / `0x1090ff20` / `0x1090ff38` against namespaces `0x109203cc` / `0x109203d4` /
  `0x109203dc`, **parent 0** — it is the root of the whole graph. It initialises no squad-slot
  space.
- `CAI_BaseNPCTroika`: one call at `0x102bd799`, inside its own init body, to the helper
  `0x102be9f0`, which initialises all three from `(space, namespaceBase, parentBase)` and derives
  `+0x18` / `+0x30` and `+8` / `+0x10` itself. The three arguments arrive as the return values of
  one-line getters — `0x102bec90` → `0x10924248` (the space), `0x102beb10` → `0x109203cc` (the
  namespace base) and `0x102beb30` → `0x1090ff08` (**the parent: the base**). So Troika's spaces are
  `0x10924248` / `0x10924260` / `0x10924278`, and it too has no squad-slot space.
- The squad-slot spaces of every class that has one parent on `0x10920484`, a `CAI_LocalIdSpace`
  constructed alone in `0x10265660` with the root flag set. Nothing registers into it — the two
  global squad slots go straight into the namespace (`0x10316e80`) — so falling through to it finds
  nothing, by construction.
Eight bodies feed zero texts and register nothing (`CAI_StandoffBehavior`, `CNPC_VChangBrosBlade`,
`…Claw`, `CNPC_VLasombra`, `CNPC_VSabbatGunman`, `CNPC_VStalker`, `CNPC_VTaxiDriver`,
`CNPC_VYukie`): they exist so the class has spaces with the right parents.

**The 691, attributed**: 64 base + 274 Troika + 353 across the species bodies (the column below
sums to it). Matched by `.rdata` address, not by name: no text is fed by two owners and none is
left unfed. The 57th caller, `0x1030f220`, is a generic loader that opens
`vdata/schedules/<name>.sch` and parses it with caller-supplied class and space; it has no caller
in the image and no shipped `.sch` is known — a dead door, not an owner.

**Squad slots.** No class registers a local squad-slot name (every `Q` below is 0). The namespace
itself is seeded globally with two names, `SQUAD_SLOT_ATTACK1` = 1,000,000,000 and
`SQUAD_SLOT_ATTACK2` = 1,000,000,001 (`0x10316e80`, straight through `0x102e9fe0`).

Parent codes (the tuple is schedule / task / condition / squad-slot space):

| Code | Parent spaces |
|---|---|
| `B` | `0x1090ff08/0x1090ff20/0x1090ff38`, `CAI_BaseNPC`; squad parent `0x10920484` |
| `T` | `0x10924248/0x10924260/0x10924278`, `CAI_BaseNPCTroika`; squad parent `0x10920484` |
| `V` | `0x1093d258/0x1093d270/0x1093d288/0x1093d2a4`, `CNPC_VVampire` |
| `VB` | `0x1093d330/0x1093d348/0x1093d360/0x1093d30c`, `CNPC_VVampireBoss` |
| `H` | `0x1093b3e0/0x1093b3f8/0x1093b410/0x1093b3c4`, `CNPC_VHuman` |
| `HC` | `0x1093b498/0x1093b4b0/0x1093b4c8/0x1093b47c`, `CNPC_VHumanCombatant` |
| `A` | `0x1093a4a8/0x1093a4c0/0x1093a4d8/0x1093a4f4`, `CNPC_VAnimal` |
| `CB` | `0x1093a888/0x1093a8a0/0x1093a8b8/0x1093aa70`, `CNPC_VChangBros` |

`R[n;a-b]` is `n` registrations whose local ids run from `a` to `b` (not necessarily densely).
`Txt` is the number of texts fed; `V=local_xx` is the body's stack vector of text pointers.

| Owner | Init body; parser caller | Own spaces S/T/C/Q | P | Txt | Registrations S / T / C / Q | Shape/deviation |
|---|---|---|---|---:|---|---|
| `CAI_BaseNPC` | init `0x1030c4e0`; feed `0x102cb690` | `0x1090ff08/0x1090ff20/0x1090ff38/—` | root | 64; static table `0x106034b8..0x106035b4` | `R[68;0-67] / R[330;0-329] / R[119;0-118] / 0` | Registration split across `0x102cadd0`, `0x10316ff0`, `0x102c8ce0`; `0x102cadd0` itself uses two entry points (`0x1000a948` × 57, `0x10007068` × 11); 4 registered ids carry no text |
| `CAI_BaseNPCTroika` | guard `0x102b97a0`; body/feed `0x102b9810` | `0x10924248/0x10924260/0x10924278/—` | `B` | 274; `158×0x102c6920 + 116×0x102c65d0` | `R[274;68-341] / 0 / 0 / 0` | Helper-based init/registration; no local task, condition, or squad-slot pairs |
| `CAI_StandoffBehavior` | `0x102c7f10` | `0x10925398/0x109253b0/0x109253c8/—` | `B` | 0 | `0 / 0 / 0 / 0` | Three-space empty body; parser loop has zero iterations |
| `CNPC_Crow` | `0x10359270` | `0x1093a0d0/0x1093a0e8/0x1093a100/0x1093a070` | `B` | 8; `V=local_98` | `R[8;68-75] / R[9;330-338] / R[3;119-121] / 0` | Exact Brujah shape |
| `CNPC_VAndreiBlood` | `0x1035c490` | `0x1093a420/0x1093a438/0x1093a450/0x1093a470` | `VB` | 5; `V=local_8c` | `R[5;346-350] / R[7;336-342] / R[1;121] / 0` | Exact |
| `CNPC_VAnimal` | `0x1035ede0` | `0x1093a4a8/0x1093a4c0/0x1093a4d8/0x1093a4f4` | `T` | 8; `V=local_98` | `R[8;342-349] / 0 / 0 / 0` | Exact |
| `CNPC_VAsianVampire` | `0x10360640` | `0x1093a530/0x1093a548/0x1093a560/0x1093a578` | `VB` | 3; `V=local_84` | `R[3;346-348] / R[3;336-338] / 0 / 0` | Exact |
| `CNPC_VBach` | `0x10362e20` | `0x1093a620/0x1093a638/0x1093a650/0x1093a608` | `V` | 9; `V=local_9c` | `R[9;344-352] / R[8;330-337] / R[3;121-123] / 0` | Exact |
| `CNPC_VBatSwarm` | `0x10366e40` | `0x1093a688/0x1093a6a0/0x1093a6b8/0x1093a6d0` | `V` | 1; `V=local_7c` | `R[1;344] / R[1;330] / R[1;121] / 0` | Exact |
| `CNPC_VBrujah` | `0x10367a40` | `0x1093a740/0x1093a758/0x1093a770/0x1093a788` | `V` | 2; `V=local_88` | `R[2;344-345] / 0 / 0 / 0` | Worked exact shape |
| `CNPC_VCamera` | `0x10368580` | `0x1093a7d8/0x1093a7f0/0x1093a808/0x1093a7bc` | `T` | 1; `V=local_84` | `R[1;342] / 0 / 0 / 0` | Exact; slot 580 is shared with `CNPC_VCameraSecurity` by `0x103683b0` |
| `CNPC_VChangBros` | `0x1036a420` | `0x1093a888/0x1093a8a0/0x1093a8b8/0x1093aa70` | `VB` | 5; `V=local_78` | `R[5;346-350] / R[14;336-349] / R[4;121-124] / 0` | Exact |
| `CNPC_VChangBrosBlade` | `0x1036ed20` | `0x1093a8f0/0x1093a908/0x1093a920/0x1093a8d8` | `CB` | 0 | `0 / 0 / 0 / 0` | Exact empty leaf |
| `CNPC_VChangBrosClaw` | `0x1036f520` | `0x1093a938/0x1093a950/0x1093a968/0x1093aa10` | `CB` | 0 | `0 / 0 / 0 / 0` | Exact empty leaf |
| `CNPC_VCombatman` | `0x1036fce0` | `0x1093ab68/0x1093ab80/0x1093ab98/0x1093abb0` | `H` | 2; `V=local_88` | `R[2;343-344] / 0 / 0 / 0` | Exact |
| `CNPC_VCop` | `0x10370b00` | `0x1093ac60/0x1093ac78/0x1093ac90/0x1093ac40` | `HC` | 27; `V=local_e4` | `R[27;344-370] / R[1;330] / 0 / 0` | Exact |
| `CNPC_VDog` | `0x10373700` | `0x1093acd8/0x1093acf0/0x1093ad08/0x1093ad64` | `A` | 10; `V=local_a0` | `R[10;350-359] / 0 / R[7;120-126] / 0` | Exact |
| `CNPC_VFrenzyShadow` | `0x10375470` | `0x1093ae48/0x1093ae60/0x1093ae78/0x1093ae28` | `V` | 9; `V=local_9c` | `R[9;344-353] / R[1;330] / R[1;121] / 0` | Exact |
| `CNPC_VGangrel` | `0x10377240` | `0x1093aef8/0x1093af10/0x1093af28/0x1093aed8` | `V` | 2; `V=local_88` | `R[2;344-345] / 0 / 0 / 0` | Exact |
| `CNPC_VGargoyle` | `0x10377d00` | `0x1093aff0/0x1093b008/0x1093b020/0x1093b038` | `V` | 9; `V=local_9c` | `R[9;344-352] / 0 / 0 / 0` | Exact |
| `CNPC_VGhoulCroucher` | `0x1037a980` | `0x1093b0f8/0x1093b110/0x1093b128/0x1093b0e0` | `HC` | 2; `V=local_80` | `R[2;344-345] / R[2;330-331] / R[1;121] / 0` | Exact |
| `CNPC_VGuard1` | `0x1037c830` | `0x1093b168/0x1093b180/0x1093b198/0x1093b1b4` | `H` | 4; `V=local_88` | `R[4;343-346] / 0 / 0 / 0` | Exact |
| `CNPC_VHengeyokai` | `0x1037ea90` | `0x1093b328/0x1093b340/0x1093b358/0x1093b27c` | `V` | 25; `V=local_dc` | `R[25;344-368] / R[5;330-334] / 0 / 0` | Exact |
| `CNPC_VHuman` | `0x10384230` | `0x1093b3e0/0x1093b3f8/0x1093b410/0x1093b3c4` | `T` | 1; `V=local_84` | `R[1;342] / 0 / 0 / 0` | Exact |
| `CNPC_VHumanCombatant` | `0x10386cb0` | `0x1093b498/0x1093b4b0/0x1093b4c8/0x1093b47c` | `H` | 1; `V=local_84` | `R[1;343] / 0 / 0 / 0` | Exact; slot 580 shared with `CNPC_ProneDialog` by `0x10386ae0` |
| `CNPC_VHumanCombatPatrol` | `0x103878e0` | `0x1093b538/0x1093b550/0x1093b568/0x1093b580` | `HC` | 1; `V=local_84` | `R[1;344] / 0 / 0 / 0` | Exact |
| `CNPC_VHunter` | `0x10388230` | `0x1093b608/0x1093b620/0x1093b638/0x1093b5e8` | `HC` | 4; `V=local_88` | `R[4;344-347] / 0 / 0 / 0` | Exact |
| `CNPC_VLasombra` | `0x10388fb0` | `0x1093b698/0x1093b6b0/0x1093b6c8/0x1093b680` | `V` | 0 | `0 / 0 / 0 / 0` | Exact empty leaf |
| `CNPC_VMalkavian` | `0x10389730` | `0x1093b718/0x1093b730/0x1093b748/0x1093b6fc` | `V` | 2; `V=local_88` | `R[2;344-345] / 0 / 0 / 0` | Exact |
| `CNPC_VManBat` | `0x10389f80` | `0x1093b778/0x1093b790/0x1093b7a8/0x1093b89c` | `V` | 13; `V=local_98` | `R[13;344-356] / R[27;330-356] / 0 / 0` | Exact |
| `CNPC_VMingXiao` | `0x103913c0` | `0x1093b918/0x1093b930/0x1093b948/0x1093bacc` | `T` | 26; `V=local_e0` | `R[26;342-367] / R[22;330-351] / R[8;119-126] / 0` | Exact |
| `CNPC_VMingXiaoTentacle` | `0x1039b260` | `0x1093bd38/0x1093bd50/0x1093bd68/0x1093bd80` | `T` | 24; `V=local_d8` | `R[24;342-365] / R[13;330-342] / R[3;119-121] / 0` | Exact |
| `CNPC_VMoleman` | `0x1039f7d0` | `0x1093bdd8/0x1093bdf0/0x1093be08/0x1093bdb8` | `H` | 2; `V=local_88` | `R[2;343-344] / 0 / 0 / 0` | Exact |
| `CNPC_VNosferatu` | `0x103a1670` | `0x1093bee8/0x1093bf00/0x1093bf18/0x1093bf30` | `V` | 2; `V=local_88` | `R[2;344-345] / 0 / 0 / 0` | Exact |
| `CNPC_VPedestrian` | `0x103a1fd0` | `0x1093bfb0/0x1093bfc8/0x1093bfe0/0x1093bffc` | `H` | 4; `V=local_88` | `R[4;343-346] / 0 / 0 / 0` | Exact |
| `CNPC_VPlaceholder` | `0x103a3c80` | `0x1093c040/0x1093c058/0x1093c070/0x1093c08c` | `T` | 1; `V=local_84` | `R[1;343] / 0 / 0 / 0` | Exact |
| `CNPC_VSabbatGunman` | `0x103a5270` | `0x1093c190/0x1093c1a8/0x1093c1c0/0x1093c1d8` | `HC` | 0 | `0 / 0 / 0 / 0` | Exact empty leaf |
| `CNPC_VSabbatLeader` | `0x103a5e80` | `0x1093c3f0/0x1093c408/0x1093c420/0x1093c3d4` | `VB` | 13; `V=local_98` | `R[13;346-358] / R[20;336-355] / R[1;121] / 0` | Exact |
| `CNPC_VScurrying` | `0x103abd70` | `0x1093c490/0x1093c4a8/0x1093c4c0/0x1093c4e0` | `A` | 6; `V=local_90` | `R[6;350-355] / R[2;330-331] / R[1;120] / 0` | Exact; slot 580 shared with `CNPC_VRat` by `0x103abba0` |
| `CNPC_VSheriffMan` | `0x103adce0` | `0x1093c520/0x1093c538/0x1093c550/0x1093c568` | `VB` | 4; `V=local_74` | `R[4;346-349] / R[9;336-344] / R[1;121] / 0` | Exact |
| `CNPC_VSheriffSwarm` | `0x103b1df0` | `0x1093c638/0x1093c650/0x1093c668/0x1093c680` | `V` | 1; `V=local_7c` | `R[1;344] / R[1;330] / R[1;121] / 0` | Exact |
| `CNPC_VStalker` | `0x103b2a80` | `0x1093c6f0/0x1093c708/0x1093c720/0x1093c738` | `HC` | 0 | `0 / 0 / 0 / 0` | Exact empty leaf |
| `CNPC_VTaxiDriver` | `0x103b31a0` | `0x1093c7a8/0x1093c7c0/0x1093c7d8/0x1093c7f0` | `H` | 0 | `0 / 0 / 0 / 0` | Exact empty leaf |
| `CNPC_VTest` | `0x103b3cf0` | `0x1093c830/0x1093c848/0x1093c860/0x1093c8bc` | `V` | 3; `V=local_8c` | `R[3;344-346] / 0 / 0 / 0` | Exact |
| `CNPC_VToreador` | `0x103b5500` | `0x1093c900/0x1093c918/0x1093c930/0x1093c948` | `V` | 2; `V=local_88` | `R[2;344-345] / 0 / 0 / 0` | Exact |
| `CNPC_VTremere` | `0x103b5ca0` | `0x1093c980/0x1093c998/0x1093c9b0/0x1093c9c8` | `V` | 2; `V=local_88` | `R[2;344-345] / 0 / 0 / 0` | Exact |
| `CNPC_VTzimisce` | `0x103b7120` | `0x1093ce70/0x1093ce88/0x1093cea0/0x1093ccc4` | `T` | 70; `V=local_190` | `R[70;342-411] / 0 / R[2;119-120] / 0` | Exact |
| `CNPC_VTzimisceHeadClaw` | `0x103c0cc0` | `0x1093d160/0x1093d178/0x1093d190/0x1093d1ac` | `T` | 3; `V=local_8c` | `R[3;342-344] / 0 / 0 / 0` | Exact |
| `CNPC_VTzimisceRunner` | `0x103c2a90` | `0x1093d1d8/0x1093d1f0/0x1093d208/0x1093d224` | `T` | 2; `V=local_88` | `R[2;342-343] / 0 / 0 / 0` | Exact |
| `CNPC_VVampire` | `0x103c4ab0` | `0x1093d258/0x1093d270/0x1093d288/0x1093d2a4` | `H` | 1; `V=local_84` | `R[1;343] / 0 / 0 / 0` | Exact; slot 580 shared with `CNPC_VPlayerController` by `0x103750e0` |
| `CNPC_VVampireBoss` | `0x103c52a0` | `0x1093d330/0x1093d348/0x1093d360/0x1093d30c` | `V` | 2; `V=local_6c` | `R[2;344-345] / R[6;330-335] / 0 / 0` | Exact |
| `CNPC_VVentrue` | `0x103c7980` | `0x1093d3b0/0x1093d3c8/0x1093d3e0/0x1093d394` | `V` | 2; `V=local_88` | `R[2;344-345] / 0 / 0 / 0` | Exact |
| `CNPC_VWerewolf` | `0x103c8f00` | `0x1093f9e8/0x1093fa00/0x1093fa18/0x1093d6d4` | `T` | 15; `V=local_a0` | `R[15;342-356] / R[24;330-353] / R[5;119-123] / 0` | Exact |
| `CNPC_VWolfMorph` | `0x103dc980` | `0x109402a8/0x109402c0/0x109402d8/0x1094028c` | `V` | 1; `V=local_84` | `R[1;344] / 0 / 0 / 0` | Exact |
| `CNPC_VYukie` | `0x103dd220` | `0x10940330/0x10940348/0x10940360/0x10940314` | `HC` | 0 | `0 / 0 / 0 / 0` | Exact empty leaf |
| `CNPC_VZombie` | `0x103de500` | `0x10940398/0x109403b0/0x109403c8/0x109403e0` | `A` | 13; `V=local_ac` | `R[13;350-363] / R[9;330-339] / R[2;120-121] / 0` | Exact |

**Unrecovered:** the run-time order in which the first-touch species bodies fire (it depends on
which class is precached first, and nothing observable depends on it, since no space searches a
sibling); a caller for `0x1030f220`.

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
null enemy, accepts before any test at all. A null or disabled hint fails before that (re-read
2026-09-30, 0018 story 8). Otherwise: no active weapon fails; a height difference
over `_DAT_1049ae28` fails; the hint-to-enemy 2-D distance below `m_flTargetDistMin` fails; unless
`m_bStayEntrenched`, a distance over **either** the weapon's own maximum range (`weapon +0x8c0`) or
`m_flTargetDistMax` fails; a hint that is not already mine needs
`dot(normalize(hint - enemy), normalize(me - enemy)) >= _DAT_10451ab4` — and retail's own message
gives that literal away, `"Projection (%.2f) < 0.2"`. The normalise is `0x10137220`, **3-D with an
epsilon**, and only then are the X and Y components dotted (the section's earlier "normalize2D"
was a shorthand; corrected 2026-09-30). Every ordered compare in this body and in `0x102968f0`
takes its edge from the flag test, so an unordered (NaN) fraction counts as CLEAR in both traces. The facing projection is then tested against
**two** caller-supplied bounds that are not symmetric: `<= flGoodRange` is
`"Enemy outside of good range"` and `>= flBadRange` is `"Enemy inside of bad range"`. Last, and only
under `m_bForceCoverLOSCheck` (`+0x6408`), `0x102968f0` must pass or the answer is
`"Failed hint LOS"`.

**Unrecovered:** `_DAT_1049ae28` (= **64.0**, float64; read 2026-09-21, `rdata-cells.md`) (the height limit), `_DAT_1046a51c` (= **1.1920928955e-7f**, float32; read 2026-09-21, `rdata-cells.md`) (the normalise epsilon) and
what `_DAT_104454d0` (= **0.5f**, float32; read 2026-09-21, `rdata-cells.md`) means as `0x10295ed0`'s forward floor; and `0x102968f0` itself is walked only as
"the hint LOS check" here.

### The debug NPC's prints — every reader of ai_debug_npc and ent_trace_conditions (2026-09-30)

Recovered for 0018 story 8, brief S7, from the corpus (`vtmb_globals`, `vtmb_asm`) and the image's
own strings. Retail has **four** per-NPC debug switches, not one, and the port's verb
(`elysium.ai_debug_npc` = `elysium.npc_trace`) now sets the three that gate text.

**The switches.**

| Switch | Object / word | Set by | Default |
|---|---|---|---|
| `ai_hint_focus_npc` (the port's `ai_debug_npc`) | EHANDLE `DAT_10925444` | ConCommand `0x10085480` (registered `0x10085410`, name `0x1054981c`), DevMsg `"Changing hint ent to %s from %s\n"` / `"Clearing hint ent to *UNKNOWN* from %s\n"`; `-1` at static init `0x102d0840` | `-1` |
| `npc_task_text` | `m_debugOverlays` (`+0x224`) bit `0x8000000` | `0x10087b70` → `0x100d1ff0` (XOR-toggle on every entity the name matches) | clear |
| `ent_trace` | `m_debugOverlays` bit `0x80000000` | `0x100b0ad0` → `0x100d1ff0` | clear |
| `ent_trace_conditions` | ConVar `0x10924a68` (parent word `DAT_10924a6c`, value `+0x2c`) | ConVar, default `DAT_10539978` = `"1"` | `1` |

`ent_trace_buffer` (`0x100b0c00`, bytes `DAT_10920534`/`DAT_10920535` = on / verbose, DevMsg
`"Trace Buffer On%s\n"` / `"Trace Buffer Off\n"`) and `ent_trace_dump_buffer` (`0x100b0df0` →
`0x1027efb0`) route the trace into a 16 KB per-NPC ring (`+0x1b4e`, cursor `+0x5b50`, wrapped flag
`+0x5b54`; appender `0x1027ef20`) and dump it between `"** BEGIN BUFFER DUMP FOR %s\n"` and
`"** END BUFFER DUMP FOR %s\n"`. The other `ent_trace_*` sub-switches (`_doors`, `_hints`, `_melee`,
`_sound`, `_frenzy`, `_scripted`, `_moveshoot`, `_status`, `_idealact`, `_nav`, `_patrolpath`,
statics `0x1028bd50`..`0x1028c380`) have **no reader** in the image.

**Readers of `DAT_10925444`** — eight, all hint validators: `0x10295c20` (FValidateHintType, thunk
`0x1001077b`), `0x102961a0`, `0x102968f0`, `0x10296c40` (and their thunks). Their reason strings are
the section above; brief S5 stood them.

**Readers of `ent_trace_conditions`** — 130 functions. **129 are dead gates**: the release build
kept the virtual `IsCommand()` call (`MOV ECX,[0x10924a6c]; CALL [EAX+4]`) and discarded its answer,
immediately before a `SetCondition` (`0x100041b0`) or `ClearCondition` call — e.g. `TaskFail`
`0x10274034` before `SetCondition(0x5c)`, `0x1029f81a` before `SetCondition(0xa)`,
`CAI_BaseNPC::GatherConditions` `0x1026ee2c` before `SetCondition(0x67)`. Whatever those sites
printed was compiled out; no text survives. **One is live**: the NPC trace formatter `0x1028d990`.

**The trace line.** `CBaseEntity::TraceMessage` is vtable slot 18 (`0x1009b2c0`, base format
`"%-20s  %6.2f : %*s %s\n"` at `0x10554dbc`, `TraceMessageBase` `0x1009b1d0`); slots 17, 19, 20 are
its siblings (`TraceMessageBare`). `CAI_BaseNPCTroika` overrides 17..20 (`0x1028de90`,
`0x1028de10`, `0x1028dfb0`, `0x1028df30`); 17/18 format through `0x1028d990(msg, indent, buf,
0x200)` and then either DevMsg or, under `ent_trace_buffer`, append to the ring. `0x1028d990`,
unbuffered: `"%-20s  %6.2f : %*s %s\n%s%s %s%s %s\n\n"` (`0x105d8828`) with `GetDebugName`,
`curtime`, `indent` spaces, the message, then

- only while `ent_trace_conditions` answers `!IsCommand() && m_nValue > 0` (`0x1028d9fd`..`0x1028da17`):
  `"CONDS:"`, `" %s"` (`0x105a3060`) per condition `i < slot 0x664` that `HasCondition(i)`, named by
  slot `0x660`, then `"\n"`;
- `m_afMemory` (`+0x5d8c`) as 32 letters of `"PIS__PF_T_L__TTEPLM________ICCCC"` (`0x105d88e0`), `.`
  for a clear bit;
- `m_bfAINPCFlags` (`+0x14b8`) as 30 letters of `"RSCPFCNFIPCDHVAEFSBDSLIAMFDPOIO_"` (`0x105d88b8`);
- an always-empty `%s`, then `"NAV %s %s"` only when `GetNavType()` (`0x1027d990`) is 3 (`"CLIMB"`)
  or 1 (`"JUMP"`).

Buffered: `"%6.2f : %*s %s\n"` (`0x105d8854`), or with verbose `"%6.2f : %*s %s\n%s%s %s%s %s\n\n"`
(`0x105d8868`). **No reachable caller** of slots 17..20 on an NPC exists in the image — the call
sites were compiled out with the gates. `ent_trace`'s bit has three readers, none of them text:
`CBaseAnimating::DispatchAnimEvents` (`0x10091880`), the stealth-vision probe `0x102b4760` (debug
boxes) and `0x10260a50`.

**The live schedule-chain prints** are `npc_task_text`'s, plain DevMsg, no NPC name:

| Group | Site | Gate | Text | Operand |
|---|---|---|---|---|
| schedule change | `SetSchedule` `0x10280e50`, last statement | `+0x224 & 0x8000000` | `"Schedule: %s\n"` (`0x105cde18`) | `CAI_Schedule+0x40` name |
| task start | `MaintainSchedule` `0x102817c0`, `0x10281d68`..`0x10281d83` (before slot 442) | `& 0x8000000` | `"Task: %s\n"` (`0x105cdf20`) | slot `0x704` task name |
| interrupt | `IsScheduleValid` `0x10280ff0`, `0x1028121f`..`0x10281334` | `developer != 0` (`DAT_1070af4c`) and `& 0x8000000` | `"   Break condition -> %s\n"` / `"   Break condition -> !%s\n"` (`0x105cde28` / `0x105cde48`) | lowest fired ordinal `0..0xbf`, `!` when in the inverted set (`+0x5c8c` and not held); name via slot `0x728`, miss = `"ERROR: Unknown condition!"` |
| task fail | `CAI_BaseNPC::TaskFail` `0x10273fc0`, `0x10273fc3`..`0x10274018` | `developer != 0` and `& 0x8000000` | `"   TaskFail -> %s\n"` (`0x105cc5e0`) | `0x10316fa0(code)` failure text |

Under `developer` alone the interrupt and fail arms also write an overlay record (`+0x5f30` failure
text, `+0x5f34` break-condition name, `+0x5f38`/`+0x5f3c`). Unconditional prints in the same chain,
gated on no switch: `"ERROR: Missing or invalid schedule!\n"` (`0x1028226c`), `"Invalid State for
SelectSchedule!\n"` and `"No suitable combat schedule!\n"` (`SelectSchedule` `0x1028a380`),
`"Calling ForceScheduleChange on NPC '%s'\n"` (`0x102ae490`), and `CNPC_VWerewolf::TaskFail`'s
DevWarning (`0x103ce750`). The selectors print **nothing**: every arm stamps `+0x1b2c` (selector
id), `+0x1b30` (`__FILE__`) and `+0x1b34` (`__LINE__`), e.g. the occlusion ladder `0x102b8320`.
Navigation / movement failures have no gated print (`ent_trace_nav` has no reader).

**The port** (`Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseTrace.cpp`):
`FElysiumNpcBase::IsAiDebugNpc` (the `DAT_10925444` test; `FElysiumNpc::IsHintDebugNpc` forwards to
it); `NpcTraceMessage` = `0x1028d990`'s unbuffered line, prefixed with `DebugString()` instead of
`GetDebugName`, on `LogElysiumNpcTrace`; the four `npc_task_text` prints at their port sites
(`FElysiumNpcBase::DebugScheduleInstalled` from `ElysiumSchedule::Install`,
`FElysiumNpc::DebugTaskStart`, `DebugScheduleBreak` from `ElysiumSchedule::Tick`'s interrupt test,
`FElysiumNpcBase::TaskFail`), names printed `name (id)`. The verb sets `DAT_10925444` and both
overlay bits on the chosen NPC. Named divergences, all debug output only: (1) the four prints go
through the trace formatter (name, time, CONDS) rather than bare DevMsg; (2) the `developer` half of
the interrupt/fail gate is dropped and the `+0x5f30..+0x5f3c` record is not kept; (3) for the 129
dead gates the port prints, under `ent_trace` and `ent_trace_conditions > 0`, one
`SetCondition` / `ClearCondition name (id)` line per condition `GatherConditions` changed
(`FElysiumNpcBase::RunAI`); (4) the port's schedule rows (`RecordScheduleEvent`, which carries the
selector stamps) join the trace under `npc_task_text`; (5) one ring of whole entries for the debug
NPC, always on, read by `elysium.npc_trace_tail [n]` (default 60), in place of `ent_trace_buffer`'s
per-NPC 16 KB rings. **Unrecovered:** the text of the 129 compiled-out condition traces and of every
compiled-out `TraceMessage` call.

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

**Unrecovered:** `_DAT_104704b4` (= **-40.0f**, float32; read 2026-09-21, `rdata-cells.md`) (the second rung's edge) and `_DAT_1044ffdc` (= **0.0054931640625f**, float32; read 2026-09-21, `rdata-cells.md`) (the duration scale);
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

**Recovered 2026-09-28 (story 8 pass C, L13 smoke).** The argument is the patrol cell
(`m_sppPatrolPath` `+0x658c` from arm `0x102a3b91`, `m_sppPatrolPathHunt` `+0x6594` from `0x102a3bac`),
`+0x04` its pooled `CAI_PatrolPath`. `0x10307b80` is `CAI_PatrolPath::NextPoint`: `cur += step[type]`
(`DAT_1049df2c`, the table row stride 0x14: +1, -1, +1, -1 for types 0..3); in `0..count-1` it answers
false; off either end `repeat < 1` answers true (exhausted), else `--repeat`, `type = next[type]`
(`DAT_1049df30`: 0, 1, 3, 2 -- 0/1 wrap, 2/3 reverse), `cur = 0x10307c20` for the new type, false. The
untyped row -1 reads the dwords before the table (`0x1049df18` step 0x3ee4f766, `0x1049df1c` next 0).
A true answer releases the cell (`0x1029f5d0`, `0x102aa9ff`); `0x1029f650` then redraws the interest on
the same cell (`0x102aaa07`), which on a released cell only clears `+0x65a0`. Port:
`FElysiumNpc::FUN_102aa9e0` (`ElysiumNpcTroikaHelpers2.cpp`), `PatrolPathNextPoint`
(`ElysiumNpcScript.cpp`).

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

**Ten constants, all read out of the pinned image.** `_DAT_10449270` (= **0.5**, float64; read 2026-09-21, `rdata-cells.md`) **0.5** (double),
`_DAT_104454c0` **1.0f**, `_DAT_104454c4` **0.0f**, `_DAT_104493d0` **0.1** (double),
`_DAT_1044fab0` (= **0.0**, float64; read 2026-09-21, `rdata-cells.md`) **0.0**, `_DAT_1044e658` (= **0.01**, float64; read 2026-09-21, `rdata-cells.md`) **0.01** (double — `lifecycle.md` lists it as unrecovered
and this is its value), `_DAT_10451acc` (= **64.0f**, float32; read 2026-09-21, `rdata-cells.md`) **64.0f**, `_DAT_10449258` **3.0f**, `_DAT_104994e0` (= **-30.0f**, float32; read 2026-09-21, `rdata-cells.md`)
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

**Settled 2026-10-04 (spec 0002 `stories/v4/packets-S5.md` items 1 and 2).**

- **Motor slot 10 is `0x102e1440`**: `m_vecVelocity = 0`, then `0x102ddc40(owner+0x5d38)` (not
  walked). A refused step zeroes the motor's velocity.
- **`0x102e0bd0` is `CAI_Motor::MoveGroundStep(newPos, pMoveTarget, newYaw, bAsFarAsCan, bTestZ,
  pTraceResult, bNoTrace)`** (`RET 0x1c`). `flags = bTestZ ? 1 : 5`;
  `CAI_MoveProbe::TestGroundMove 0x102e4f50(probe, GetOrigin(), newPos, 0x202400b, 100.0, flags,
  &trace, bNoTrace, 0)` — flag 4 skips the final height test (status −2), and `bNoTrace` makes the
  probe answer `newPos` untested. `bHitTarget = trace.pObstruction != 0 && == pMoveTarget`;
  `bBlocked = trace.fStatus < 0`; `bBlocked && !bAsFarAsCan && !bHitTarget` → 0, nothing moved.
  Else the origin is set to the trace's end, a step-up above 0.1 (`0x104491b4`) is handed to the
  physics shadow controller clamped to the step height, `newYaw != −1` sets the local yaw; answer
  4 (hit the target) / 1 (not blocked) / 2 (`fStatus == −3`) / 3.
  Callers: slot 19 `0x10264680` `(…, move+0x34, −1, 1, 1, its 2nd arg, its 3rd arg)`; the walk
  `0x102e1560` `(…, move+0x34, −1, 1, "the step was clamped to maxDist", its 4th, its 5th)`;
  `AutoMovement 0x10280a50` `(delta, 0x102729d0(this), yaw, 0, 0, 0, 0)`.
- **`CAI_Motor::MoveFacing 0x102e19e0`'s owner test, slot 526 (`+0x838`), is
  `CAI_BaseNPC::OverrideMoveFacing 0x1027d9f0` on all 77 classes that fill the slot; it returns
  false.** No class overrides it.
- **The turn script's insert `0x10262ea0(i, t)`** (array `+0x6038`, count `+0x6044`): the first
  entry `k >= i` with `t <= flTime[k]`: `a = t / flTime[k]`; `flTime[k] −= t`; a new entry at `k+1`
  with `flTime = t`, `flElapsed (+4)` and `vecLocation (+0x2c)` lerped by `a` between `k` and the
  old `k+1`; returns `k+1`, or 0 off the end. The durations are swapped against the SDK's
  `BuildInsertNode`; the lerp reads `k+1` before the shift (one past the count on the last entry).
  Its caller `0x10262c20` writes the new entry's yaw.

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
   names it) is asked from slot 217 `GetAbsOrigin()` to arm 6's **`StandPos`** (the open data's
   `+0`; `(pathfinder, AbsOrigin, &stand, 0, 0x30, -1, 1, 0.0, 0)` at `1029864c`, added
   2026-09-30), flags `0x30`, hull `-1`, `1`, `0.0`, `0`.
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

**Unrecovered:** what `+0x644`'s bits `0x1`, `0x4`, `0x8`, `0x10` and `0x40` are NAMED, and the
waypoint record `0x10304130` returns past its `+0x24`. (Slot 246 is `GetNPCOpenData`, closed
2026-09-30 below.)

### The door's NPC-open data and the alternate-AI door modes (2026-09-30, 0018 story 7)

_Wave 0 read R-D; the constants were read from the image._

**`GetNPCOpenData` (door slot 246, `+0x3d8`).** Only `CBaseDoor` (`0x100f0ef0`) and `CRotDoor`
(`0x100f2bd0`) fill the slot. The base body is `out+0x18 = -1` and nothing else. A sliding
`func_door` therefore always takes slot 531's arm-6 exit (`1029862f` / `10298725`: `*result = 0`,
`0x100f0e70`, true) and never reaches arm 7. The out struct is `0x1c` bytes: `+0 Vec StandPos`,
`+0xc Vec FaceDir`, `+0x18 int Activity` (−1 = no data). The `CRotDoor` body:
- `npc == NULL`, or `spawnflags & 0xc0` (`100f2c31`) → −1.
- `rest`, `fwd` and `back` are yaw vectors built from `+0x6f8`, `+0x6fc` and `+0x700` through
  `0x101d2f40`: `(cos, sin, 0)` of the angle × 0.017453292, degrees.
- `d = npc.AbsOrigin − door.AbsOrigin`, 2-D (slot 217 on both).
- `fwd·d >= 0` → open = `fwd`; else `back·d >= 0` → open = `back`; else −1 (`100f2e90`). A NaN
  answers −1.
- `bOpening` true: `Stand = origin + rest·24 + open·100`, `z −= 54`; `Face = −open`;
  `Activity = 1` (`ACT_IDLE`). `bOpening` false: the same with 50 in place of 100, and `Activity
  = 0xc84` (`ACT_KICK`).
- Constants: `_DAT_10450564` = 100.0, `_DAT_1044ffe8` = 50.0, `_DAT_1045504c` = 54.0,
  `_DAT_1044dba8` = 24.0.
- `bOpening` is `toggle_state == 2` (`TS_GOING_UP`) at slot 531. In the two alternate-AI callers
  it is `m_bOpeningDoorWait` (`+0x5d30`).

The three readers: slot 531 arm 7 routes to `StandPos` (above). Mode 1 `0x10290040` turns
`FaceDir` into a yaw (`VecToYaw`, `102900ca`) and sets the motor's ideal yaw with
`0x102e1c10(yaw, -1.0)`; a −1 answer returns false and keeps the mode. `0x10298840` hands
`Activity` to `RestartIdealActivity` with no −1 test (`10298882`), then runs the lock test and
`AcceptInput("Open", npc, npc)`: TRUE (refused) → `0x1027de00`, FALSE → Open.

**`RunAlternateAI` (`0x1028fd80`) modes 1, 2 and 3 and their timers.**
- **Mode 1** (`0x10290040`, face the door; walked in `conditions-and-states.md` § "The alternate-AI
  door transaction"). The entry `0x10298800` sets `m_bShouldMove = 0`, pauses the path (`path+0x10
  = 1`) and sets the mode to 1. Its only caller is `AdvancePath 0x102f0400`, when a `WP_TO_DOOR
  0x10` waypoint's door passes `0x1027f550` and has toggle state 1 (closed). A dead door → mode 0,
  false. Open data −1 → false, mode kept. Once `FacingIdeal`: a door already opening → mode 2,
  expiry `now + 1.0`; otherwise, if `0x10298840` succeeds → mode 2, expiry `now + 5.0`.
- **Mode 2** (`0x10290200`, above in § "`RunAlternateAI`"). `MaintainActivity`, then wait until
  `curtime >= +0x6450`. At expiry with the door handle still live → `TaskFail(0x0e)`, clear
  `+0x5d24` and `+0x5d30`, mode 0. Otherwise, if the path is paused (`0x102ee2e0` reads the
  byte `path+0x10`), `0x102bf7e0`; then slot 528; mode 0.
- **Mode 3** (`0x102902e0`, door blocked). Entered from `OnDoorBlocked` with expiry `now + 1.0`;
  at expiry `TaskFail(0x0e)` and the same clears.
- **Exits from modes 1 and 2:** `OnDoorFullyOpen` (below) → 0; `OnDoorBlocked` → mode 3; slot 532
  `0x10290570` cases 1 and 8; mode 2's own expiry.

**`OnDoorFullyOpen 0x1027dd10`** (called by `DoorHitTop 0x100f0860` on the NPC behind the door's
`m_hActivator +0x53c`). It needs slot 158 (`+0x278`, alive) and a door. If the door is
`m_hOpeningDoor (+0x5d24)` → slot 532(1). If `0x102ee2e0` answers true → `0x102ee2c0`, the
navigator's unpause, called ONLY when paused. `m_bShouldMove (+0x1a40) = 1`. **Slot 528
(`+0x840`)**. Then, through `+0x98`, a mode of 1 or 2 → 0. Port: `FElysiumNpcBase::OnDoorFullyOpen`
(`ElysiumNpcBaseConditions.cpp`), called from the door's HitTop.

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

**The actor list and aggressiveness 5, closed 2026-09-19 (0018 story 15).** Two opencode walks and
an adjudication pass; the resolver, the refresher, `vfunc244` and the preset table re-read here.
`m_actors` is a `CUtlVector<CHandle<CAI_BaseNPC>>`: array `+0x468`, allocated `+0x46c`, grow
`+0x470`, count `+0x474`, mirror `+0x478`. Its readers are exactly `0x102cd4a0`, `0x102cd3b0`,
`0x102cd650`, `0x102cdb70`, `CAI_StandoffGoal::InputSetAggressiveness 0x102c8280`, `vfunc242
0x102cd740` and the deleting destructor `0x102c88c0` — nothing else consumes an actor.

- The resolver `0x102cd4a0` zeroes the count and purges, then iterates every entity matching
  `m_iszActor` (`+0x454`): `m_SearchType` (`+0x460`) 0 by targetname (`0x100f7770`), 1 by classname
  (`0x100f7380`), any other value none. Each match contributes the NPC at `entity+0x94`, skipped
  when null or when `GetState()` (slot 464, `+0x740`) answers 7 (`NPC_STATE_DEAD`); the handle is
  appended. `m_iszGoal` (`+0x458`) then resolves by TARGETNAME, first match, into `m_hGoalEntity`
  (`+0x47c`), or `0xFFFFFFFF`.
- The refresher `0x102cd3b0` walks `count-1` down to 0 and swap-removes (last entry into the slot,
  `count--`) an entry that no longer resolves or whose NPC is in state 7.
- `vfunc242 0x102cd740` (the `UpdateActors` input `0x101cc7c0` jumps straight to it, and the
  spawn think `0x102cd310` takes it when `m_fStartActive` is clear) snapshots the list, refreshes,
  re-resolves, and dispatches slot 244 for each actor that is new and slot 245 for each that left.
  `Spawn 0x102cd2d0` arms that think at `curtime + 0.01` (`0x1044e658` is a **double**).
- Slot 244 `0x102c8570` / slot 245 `0x102c8690` find the actor's `CAI_StandoffBehavior` by
  `__RTDynamicCast` over its behaviour array (slots 456 / 457), set `behavior+0x19` to 1 / 0
  (`0x102c7360`), and on 244 `behavior+0x1a = spawnflags & 1` (`0x102c7390`). BOTH then apply a
  seven-dword parameter block through `0x102c73b0` (copied to `behavior+0x20..+0x38`; `+0x3c =
  -1.0f`, `+0x40 = block[2]`, `+0x44 = block[3]`), its first dword overwritten with
  `m_HintChangeReaction` (`+0x4a4`) after the copy (`102c8631`).
- **Aggressiveness 5 selects the entity's own block** `m_customParams` at `+0x488` (keys
  `CustomCoverOnReload` `+0x48c`, `CustomMinTimeShots` `+0x490`, `CustomMaxTimeShots` `+0x494`,
  `CustomMinShots` `+0x498`, `CustomMaxShots` `+0x49c`, `CustomOddsCover` `+0x4a0`); 0..4 index the
  preset table `0x10601880`, stride `0x1c` (`102c8600 CMP EAX,5`). Read from the DLL, as
  `{coverOnReload, minTimeShots, maxTimeShots, minShots, maxShots, oddsCover}`:
  0 `{1, 10.0, 15.0, 0, 1, 90}`, 1 `{1, 4.0, 8.0, 1, 2, 50}`, 2 `{1, 2.0, 4.0, 1, 4, 25}`,
  3 `{1, 1.0, 3.0, 2, 4, 10}`, 4 `{0, 0.0, 0.0, 100, 100, 0}`. `InputSetAggressiveness` reads an
  int payload only when the variant's field type is 4 (else 0), clamps as the two inputs do, and
  re-applies the block to every actor inline.

**`vfunc242`'s order, read from the listing (closed 2026-09-19).** It is the SDK's
`CAI_GoalEntity::UpdateActors`: (1) the refresher prunes; (2) every current actor's resolved
POINTER (`0x10009c8c`) is inserted into a stack `CUtlRBTree` whose comparator is the unsigned
pointer compare `0x102ce100` (`CMP ECX,EAX / SBB / NEG`); (3) the resolver rebuilds the list;
(4) the NEW list is walked in index order 0 … n−1 — an actor absent from the tree gets slot 244
(`+0x3d0`, `102cd8c1`) at once, one present is removed from the tree (`102cd913`…); (5) what is
left in the tree gets slot 245 (`+0x3d4`, `102cd9a2`) in the tree's IN-ORDER walk (leftmost
first, `102cd94d`…, then successor steps). So every enable precedes every disable, enables
follow the resolver's entity-list order, and disables go in ASCENDING OBJECT ADDRESS — an
allocator order, not an authored one. `entity+0x94` has no datamap name: it is the
`CAI_BaseNPC*` self-downcast the NPC constructor caches (`1027c64b MOV [ESI+0x94],ESI`), which
`shape.md` calls `m_pBaseNPC`.

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

The hull table `PTR_DAT_1060a750` itself — 22 rows, bits `1 << index`, names and both extent
pairs — is tabulated in `../navigation-jump-links.md` § "What the shipped graphs and maps
actually use" (closed 2026-09-19).

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

## Story 8, family StartTask19 — slot 442 `StartTask`: base `0x102827f0`, Troika `0x102a1910`, the species overrides (2026-09-28)

_Recovered 2026-09-28, 0019 story 8 pass I: lanes L03 (`cb0ed36a`), L01 (`7bb820fb`), L02 (`a97ce8f6`), L04 (`a059d04c`), integrated as `86ceba23`._

The 26 `rule` rows of family StartTask19 and `CNPC_VCop::StartTask` `0x10371b70` (family
Damaged19), walked from the pass-R packet
(`$ELYSIUM_WORK_ROOT/research/npc-kernel-checklist/families-19-29/StartTask19-READING.md`) and the
listings (`vtmb_asm`) by four porter lanes, then integrated as one commit. The first three sections
below are what the integration recovered across the lanes; the four after them are the lane parts.

| part | rows | port | tests |
|---|---|---|---|
| `CAI_BaseNPC::StartTask` `0x102827f0` (lane L03) | 107 arm-table entries | `ElysiumNpcBaseStartTask.cpp` | `NpcKernelStartTask19.Base.*` |
| `CAI_BaseNPCTroika::StartTask` `0x102a1910`, first part (lane L01) | the prologue, arms `[0x102a1943, 0x102a5046)` | `ElysiumNpcStartTask.cpp` | `NpcKernelStartTask19.*` |
| `0x102a1910`, second part (lane L02) | arms `[0x102a5046, 0x102a77f7]`, the two shared tails, the base forward | `ElysiumNpcStartTask_2.cpp` | `NpcKernelStartTask19.TroikaTail.*` |
| the species overrides (lane L04) | the 25 species bodies, `CNPC_VCop` `0x10371b70`, FrenzyShadow `0x10375f50` | `ElysiumNpcStartTaskSpecies.cpp`, `ElysiumNpcFrenzyShadow.cpp` | `NpcKernelStartTask19.Species.*` |

### The dispatch, checked against the image — `0x102827f0`, `0x102a1910`

_Recovered 2026-09-28, 0019 story 8 pass I (integration of lanes L01–L04)._

- Base `0x102827f0`: `iTask - 1` into the byte table `0x10287138` (0x120 entries), then the arm
  table `0x10286f8c` (107 entries). Arms `0x00..0x69` take 153 ids; entry `0x6a` (`0x10286f63`,
  `DevMsg("No StartTask entry for %s\n")`, the task left running) takes the other 166 and every id
  past the table (`0x1028280e JA`).
- Troika `0x102a1910`: `id - 5 > 0x144` goes to the base (`0x102a77e2`); otherwise the byte table
  `0x102a7ab8` (index `id - 5`) into the arm table `0x102a77f8` (176 entries). 74 ids reach the 68
  arms below `0x102a5046` (the first switch); 115 reach arms at or past it (`StartTaskTroikaTail`),
  among them the shared tails `0x102a66d7` (ids `0x8b`, `0x125`: complete) and `0x102a77ea` (ids
  `0x05`, `0xea`, `0xeb`: left running); the remaining 136 in-range ids take `0x102a77e2`, the base
  forward, which is `StartTaskTroikaTail`'s `default:`. The two switches are disjoint and together
  complete.

**Unrecovered:** nothing named by the walk.

### `CAI_Navigator::SetGoal` `0x102ecd20` (one body), with `0x102f1dc0` and `0x102f28a0`

_Recovered 2026-09-28, 0019 story 8 pass I (integration of lanes L01–L04)._

Every StartTask arm that routes, and family Script19's builders, reach one port body
(`FElysiumNpcBase::StartTaskSetGoal`); the Troika halves and Script19 only convert their goal
literals into its record.

1. Nav `+0x8` := the owner's PATHING hull `+0x156c`, `+0xc` := curtime (`0x102ecd2c`), then navigator
   slot 7 (`0x102eea70`, the reset).
2. SetGoal flag 1 → `0x102f28a0` (below). Else flag 2 → the path's target handle and dest words
   reset (`path+0x30..+0x3c`).
3. Goal `[5]` (the MOVEMENT activity) `!= -1` → `0x102ee250` (`path+0x2c`).
4. The tolerance, `path+0x28`: `[8] == -2.0` (`_DAT_1049d980`) the pathing hull's width
   (`0x102d61b0`: `row+0x18 - row+0xc`); `[8] != -1.0` the literal; `[8] == -1.0` keeps the path's
   own unless it is `0.0`, when the hull width is written and, for an entity goal (type 1
   `m_hTargetEnt`, 2 `GetEnemy`, 7 `GetBestSeeUnknown`) whose entity is an NPC, averaged with that
   NPC's `m_eHull` width (`* 0.5`, `0x102ecebd`).
5. Path `+0x40` := hull * 0.5; the arrival-direction triple `[11..13]`; path `+0x8`/`+0x4` := `[14]`/`[15]`.
6. Goal flag 2 → a node route (`0x102f3c10` / `0x102f41b0` / `0x102fd240`) and return, bypassing
   `0x102f1dc0`.
7. Path goal type := `[0]`, `path+0x60` := `[9]`; the target entity by type; the dest words when
   `[1..3]` is not the default triple `0x1093404c..54`, else the node `[4]` (`0x102ee9c0`).
8. `0x102f1dc0` (the route build, below). Refused: flag 4 → `0x102f28a0`; return false. Built: goal
   flag 1 → `0x102e0b40` + `0x102e2020` toward the path goal; `0x102f13d0(this, 1)`; arrival
   activity `[6]`, else arrival sequence `[7]`, else arrival activity 1.

**`0x102f1dc0`** — with `m_afMemory` bit `0x20` clear: a built route (`0x102f2330`) clears the bit and,
unless slot 529 `IsCurTaskContinuousMove` answers true, completes the task through the navigator's
slot 2 (`0x102623c0` → `TaskComplete(false)`), answering true; a refused one fails the task
`OnNavFailed(0xc, 1)` (`0x102f1f00`) when nav `+0x40` is `0.0`, else sets bit `0x20`, `+0x4c :=
curtime + +0x44`, `+0x48 := curtime + +0x40`, and answers false. With the bit set: past `+0x48`
→ `OnNavFailed(0xc, 1)`; past `+0x4c` → rebuild, whose success clears the bit and completes the
task unless the current task is `0x6e` (`0x1028a150`); else false.

**`0x102f28a0`** — nav `+0x40`, `+0x44`, `+0x48`, `+0x4c` := 0, `m_afMemory &= ~0x20`, then the path
reset `0x1030bb30` (its tolerance `+0x28` among it). **`0x102886f0`** (`TASK_SET_ROUTE_SEARCH_TIME`)
is the only writer of `+0x40`; nothing but `0x102f28a0` writes `+0x44`.

`path+0x28` is written also by `0x102ee1c0` (base `TASK_SET_TOLERANCE_DISTANCE` `0x10286c84`,
`TASK_SET_MELEE_TOLERANCE_DISTANCE` `0x10286cd4`, the Troika tolerance tails, the species
`0x9f` arms); it is not `m_flGoalTolerance` (`+0x6320`), which only the Troika/species arms write
themselves. `path+0x20` is written by `0x102f2fe0` (the tolerance tails, the LKP chase arms).

**Unrecovered:** `0x102f13d0`; the arrival activity / sequence bodies `0x1030b550` / `0x1030b5b0`; the path byte `+0x10` (`0x102ee2c0` clears it, `0x102ee2e0` reads it); the BSS goal defaults `0x1093404c..54`; goal flag 2's node route (`0x102f3c10`, `0x102f41b0`, `0x102fd240`) and the node position `0x102ee9c0` — the port takes the location arm.

### Helpers the lanes shared — `0x10289ee0`, `0x1028a150`, `0x102dfed0`, `0x102d1180`, `0x10278220`, `0x102784a0`

_Recovered 2026-09-28, 0019 story 8 pass I (integration of lanes L01–L04)._

- `0x10289ee0` RestartIdealActivity: `m_Activity (+0xfec) == act` → `m_Activity = 0`
  (`0x10289eee`); then `SetIdealActivity(act)` (`0x10289efc` → `0x10272650`).
- `0x1028a150` GetCurTask: the running schedule's task at `m_iScheduleIndex`; NULL with no schedule.
  The port's steps carry retail's global task ids, so the running step answers it.
- `0x102dfed0` CAI_Enemies::GetLastKnownPosition (one port body, family Conditions19's
  `Conditions19LastKnownPosition`): the entity's record `+0xc`; else the LAST record
  flagged `+0x34` with `DevWarning(2, "Asking LastKnownPosition for enemy (%s) that's not in my memory
  (using danger pos)!!\n")` (`0x1060e248`); else `vec3_origin` (`DAT_1070d1b0`, zeroed by
  `0x101370b0`) with `DevWarning(2, "Asking LastKnownPosition for enemy (%s) that's not in my
  memory!!\n")` (`0x1060e1f8`). Before either warning it notifies `this+0` vtable `+0xe8` or
  `0x10316bc0(this+4)` by `this+8` (unrecovered target).
- `0x102d1180` (the hint's LOS endpoint): `m_nNodeID (+0x5e4) == -1` → the hint's `GetAbsOrigin`;
  else the network node's position `0x102f46d0(DAT_1093407c, ...)`.
- `0x10278220` TestLateralCover and `0x102784a0` FindLateralCover are `CAI_BaseNPC`'s, one port body
  each (`StartTaskTestLateralCover` / `StartTaskFindLateralCover`), reached by the base arm `0x48`,
  the Troika arms `0x83..0x85`, and `FindCoverFromEnemy`.

**Unrecovered:** `0x102f13d0`; `0x1030b550` / `0x1030b5b0` (arrival activity / sequence); the path
byte `+0x10` (`0x102ee2c0` clears it, `0x102ee2e0` reads it); the BSS goal defaults `0x1093404c..54`,
`0x10923a30`, `0x10934060..68`; `0x102e0290`'s second vector (read by the target-lead query as a
velocity, answered as a position by the Troika arms); the `0x102dfed0` miss notification target;
the node network (`0x102f3c10`, `0x102f41b0`, `0x102fd240`, `0x102f46d0`, `0x102ee9c0`).

### `CAI_BaseNPC::StartTask` `0x102827f0`

_Recovered 2026-09-28, 0019 story 8 pass I (lane L03)._

Slot 442 on the base line; port body `FElysiumNpcBase::StartTaskSlot442`
(`Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseStartTask.cpp`), helpers in
`ElysiumNpcBaseStartTask.inl`.

18330 bytes, 4992 instructions. Callers: `MaintainSchedule` (`0x102817c0`) through slot 442, and
`CAI_BaseNPCTroika::StartTask` (`0x102a1910`) from its own default arm.

#### Dispatch

`0x10282806`: `(iTask - 1) > 0x11f` (unsigned) jumps to the default arm `0x10286f63`; otherwise the
byte table `0x10287138[iTask - 1]` selects one of 107 entries of the arm table `0x10286f8c`
(`0x10282821`). The byte table was re-read from the image for this walk: 166 ids of `1..0x120` land on
the default (`0x45`, `0x4f`, and `0x78..0x11f` minus `0x9f`/`0xac`/`0xad`). `EDI` is preloaded with the
source-file string `0x105cde88` (`AI_BaseNPC_Schedule.cpp`) for every failure site.

#### Shared services

- **Failure** — every site stores `+0x1b44 = file`, `+0x1b48 = line`, then calls slot 448
  `TaskFail(code)`. Three sites pass a string's address as the code: `"No Sound!"` `0x105ce038`,
  `"No sound in list"` `0x105ce024`, `"gah"` `0x105cdfe4`.
- **Completion** — `TaskComplete(0)` (`0x10273e80`) everywhere; a failure raised earlier on the pass
  blocks it.
- **The goal record** (`AI_NavGoal_t`, 16 dwords): `[0]` type, `[1..3]` dest, `[4]` destNode, `[5]`
  **movement** activity, `[6]` arrival activity, `[7]` arrival sequence, `[8]` tolerance, `[9]`
  flags, `[10]` pTarget, `[11..13]` defaults `0x10934060/64/68`, `[14..15]` zero. Read off
  `SetGoal` `0x102ecd20`: `[5]` goes to `SetMovementActivity` `0x102ee250`, `[6]` to `0x1030b550`,
  `[7]` to `0x1030b5b0`, the default arrival activity is 1. Tolerance `-2.0` (`0x1049a164`) is the
  hull width (`0x102d61b0`: hull row `+0x18 - +0xc`, maxs.y − mins.y); anything but `-1.0`
  (`0x1049a160`) is taken as given; `-1.0` keeps the path's tolerance (`path+0x28`) unless it is 0,
  then the hull width, averaged with the goal entity's hull for entity goals.
- **SetGoal completes and fails tasks itself.** The route builder `0x102f1dc0`: a built route
  clears `m_afMemory` bit `0x20` and, unless slot 529 `IsCurTaskContinuousMove` answers true, calls
  the navigator's slot 2 (`TaskComplete(0)` on the owner); a refused route with route search time
  (`nav+0x40`) `0.0` calls `OnNavFailed(0xc)` (`0x102eeae0` → `TaskFail(0xc)`), otherwise sets bit
  `0x20` and defers. So the arms below that "return without completing" after a `SetGoal` still
  complete through the navigator when the route is built.
- **The weapon range clamp** (arms `0x0a` mode 1, `0x0d`, `0x10`, `0x1a`): unarmed `0.0 .. 2000.0`;
  armed `max = max(+0x8c0, +0x8c4)` (ties to `+0x8c4`, `TEST AH,5 / JP`), `min = min(+0x8b8, +0x8bc)`
  (ties to `+0x8bc`, `AND 0x4100 / JNZ`); then `max = min(max, m_flDistTooFar +0x5de4)`.
- **The motor yaw store** (`0x10288670`, and the tail of `0x102e2020`): flip by 180 when
  `motor+0x28` is set (`+180` below 180, `−180` otherwise), then `motor+0x1c == 180.0f` stores
  straight into `motor+0x34`, else through `0x102e0a80`. Every facing arm opens with `0x102e0b40`
  (`motor+0x2c = −1.0`). `UTIL_AngleMod` is `0x10288590` (`((int)(y·65536/360) & 0xffff)·360/65536`).

#### Arms, in arm-table order

| arm | address | task ids | body | exit |
|---|---|---|---|---|
| 0x00 | 0x10282828 | 1 RESET_ACTIVITY | `m_Activity +0xfec = 0` | complete |
| 0x01 | 0x10286505 | 2 WAIT, 4 WAIT_FACE_ENEMY | `m_flWaitFinished +0x5db4 = curtime + data`, no floor | running |
| 0x02 | 0x10286cd9 | 3 ANNOUNCE_ATTACK | — | complete |
| 0x03 | 0x10286f7d | 5, 0x68, 0x74 | the epilogue | running |
| 0x04 | 0x10286c0d | 6 SUGGEST_STATE | `+0x1b3c/40 = file:0xa9b`; `m_IdealNPCState +0x5cc4 = (int)data` | complete |
| 0x05 | 0x10283ed5 | 7 TARGET_PLAYER | `FindEntityByName("!player")`; null → fail 0x17 @0x67a; `SetTarget` | complete |
| 0x06 | 0x10283f36 | 8, 9, 0xa | target null → fail 1 @0x686; `dist < 1.0` → complete; activity 9 / 0x13 / `GetScriptCustomMoveActivity`; no sequence (except 0x18) → complete; target re-tested → fail 1 @0x6a7; goal type 1, [5] = activity, tol −1; in SCRIPT state `+0x5d7c != −1` → [6] = it, else `+0x5d80` set → [7] = `LookupSequence`; `SetGoal(..,4)` false → fail 0xc @0x6be, true → `SetArrivalDirection(target->GetAbsAngles())`; **every exit** runs the tail `0x102841f2`: `+0x5d7c = −1`, `+0x5d80 = 0` | complete (tail) |
| 0x07 | 0x10283dde | 0xb MOVE_TO_TARGET_RANGE | target null → fail 1 @0x669; `dist < 1.0` → complete | running |
| 0x08 | 0x1028611a | 0xc MOVE_AWAY_PATH | angles with yaw `motor+0x34 + 180`; dest = origin + fwd·`ResolveTaskDistance`; goal type 4, [5] 9, tol −1; `SetGoal(..,0)` → complete; else `FindCoverPos(origin, eye, 0, CoverRadius)` miss → fail 8 @0x94b; hit → type 4, [5] 0x13, `SetGoal` discarded; `m_flMoveWaitFinished = curtime + 2.0` | running |
| 0x09 | 0x102847a3 | 0xd SET_GOAL | `(int)data` through table `0x10287258`: 0 enemy (fail 6 @0x75f) type 2, target = enemy; 1 target (fail 1 @0x77f) type 1; 2 enemy LKP (fail 6 @0x76f) type 4; 3 target LKP in the enemy memory (fail 1 @0x78f) type 4; 4 save position type 4; `> 4` → complete; the five write `+0x5df4..+0x5e08`, then `SetMovementActivity(0x13)` | complete |
| 0x0a | 0x10284ae8 | 0xe GET_PATH_TO_GOAL | goal = stored type, target `+0x5df4`, tol −2; mode 0 stored point; 1 clamp + `FindLosPos(stored, aim)` (fail 0xb @0x7de); 2 lateral cover → move-wait + complete, else `FindCoverPos` hit → `SetGoal` (dest **not** copied) + move-wait, miss → fail 8 @0x807 **then** fail 0xc @0x816; other → fail 0xc @0x816; tail: `SetGoal` true → complete, false → fail 0xc @0x816 | per mode |
| 0x0b | 0x1028509b | 0xf GET_PATH_TO_ENEMY | `IsUnreachable(GetEnemy())` first → fail 0xc @0x83a; null → fail 6 @0x842; goal type 2 tol −1; false → `DevWarning "GetPathToEnemy failed!!"`, `RememberUnreachable`, fail 0xc @0x84f | complete |
| 0x0c | 0x10284349 | 0x10 ENEMY_LKP | `IsUnreachable` → fail 0xc @0x712; type 4 at LKP tol −1; `TranslateEnemyChasePosition(enemy, &dest, &tol, &scalar)` with `path+0x20`; `SetGoal(..,2)` true → `path+0x20 = scalar`, complete; false → DevWarning, unreachable mark, fail 0xc @0x724 | complete |
| 0x0d | 0x102844d1 | 0x11 ENEMY_LKP_LOS | null → fail 6 @0x72d; clamp; aim = LKP + enemy view offset; `FindLosPos(lkp, aim)` miss → fail 0xb @0x74e; type 4, [5] 0x13, tol −2, `SetGoal(..,2)`; `SetArrivalDirection(lkp − dest)` | running |
| 0x0e | 0x1028523a | 0x12 ENEMY_CORPSE | dest = LKP − fwd·64; type 4 tol −1; `SetGoal(..,2)` discarded | running |
| 0x0f | 0x10285387 | 0x13 GET_PATH_TO_PLAYER | `!player` unchecked; type 4 at `WorldSpaceCenter`, pTarget = player; `SetGoal` discarded | running |
| 0x10 | 0x1028545a | 0x14 ENEMY_LOS | null → fail 6 @0x86f; clamp; `FindLosPos(enemy origin, enemy eye)` miss → fail 0xb @0x891; type 4, [5] 0x13, tol −2; hint → `SetArrivalActivity(GetCoverActivity)`; `SetArrivalDirection(enemy origin − dest)` | running |
| 0x11 | 0x10285949 | 0x15 GET_PATH_TO_TARGET | null → fail 1 @0x8bf; type 4 at the target, pTarget = target, tol −1 | running |
| 0x12 | 0x10285a9e | 0x16 GET_PATH_TO_HINTNODE | no hint → fail 4 @0x8cf; type 4 at `0x102d1180(hint)`, [5] 0x13 | running |
| 0x13 | 0x10282afd | 0x17 | `+0x5db8 = GetOrigin`, `+0x5dc4 = GetAngles` | complete |
| 0x14 | 0x10282b5b | 0x18 | both from the zero vectors `0x1070d1b0` / `0x1070d9d0` | complete |
| 0x15 | 0x10282bb7 | 0x19 | `m_vSavePosition +0x5dd0 = GetOrigin` | complete |
| 0x16 | 0x10282bf1 | 0x1a | `GetBestSound` null → fail "No Sound!" @0x492; savepos = sound+0x20, plus twice the owner's slot 199 `GetVelocity` | complete |
| 0x17 | 0x10282cc7 | 0x1b | null enemy → fail 6 @0x4a7; savepos = enemy origin | complete |
| 0x18 | 0x10285bb0 | 0x1c | type 4 at `m_vecLastPosition`; false → fail 0xc @0x8df; true → `SetArrivalDirection(m_qaLastFacing)` | running |
| 0x19 | 0x10285cb9 | 0x1d | type 4 at save position; discarded | running |
| 0x1a | 0x1028570b | 0x1e | clamp; aim = savepos + OWN view offset; `FindLosPos` miss → fail 0xb @0x8b6; type 4, [5] 0x13, tol −2 | running |
| 0x1b | 0x10285d7f | 0x1f | `SetRandomGoal(ResolveTaskDistance, BodyDirection2D)` false → fail 0x18 @0x8f7 | complete |
| 0x1c / 0x1d | 0x10285df8 / 0x10285ef8 | 0x20 / 0x21 | best sound (fail 0x12 @0x905) / best scent (fail 0x13 @0x915); type 4 at sound+0x20 tol −1 | running |
| 0x1e | 0x102863f1 | 0x22 RUN_PATH | 0x13 if the model has it else 9; `m_afMemory &= ~2` | complete |
| 0x1f | 0x10286438 | 0x23 WALK_PATH | 0x22 on FLY/FLYGRAVITY when present, else 9, else 0x13; `&= ~2` | complete |
| 0x20..0x23 | 0x102864f1 / af / d0 / 0x10286523 | 0x24..0x27 | `m_bShouldMove = 1`; 9 / 9 / 0x13 / 0x13; the TIMED pair also stamp `m_flWaitFinished` | running |
| 0x24 | 0x10286556 | 0x28 STRAFE_PATH | `m_bShouldMove = 1`; 2-D right · (waypoint − origin), both normalised; `<= 0` → 0x37 else 0x38 | complete |
| 0x25 | 0x10284218 | 0x29 | `m_flMoveWaitFinished = curtime` | complete |
| 0x26 | 0x102867e5 | 0x2a SMALL_FLINCH | `SetIdealActivity(GetFlinchActivity 0x10265970)` | running |
| 0x27 | 0x10283cd5 | 0x2b FACE_IDEAL | hold; `SetTurnActivity` | running |
| 0x28 | 0x10283cf7 | 0x2c FACE_PATH | no goal → `DevWarning "No route to face!"`, fail 0xc @0x63e; hold; ideal yaw to the waypoint; `|DeltaIdealYaw| > 15.0` (double `0x1049a170`) → turn, else complete | either |
| 0x29 | 0x10286537 | 0x2d FACE_PLAYER | `m_flWaitFinished = curtime + data` | running |
| 0x2a | 0x10283c66 | 0x2e FACE_ENEMY | `FInAimCone(LKP)` → complete; else hold, yaw to LKP, turn | either |
| 0x2b | 0x10283a36 | 0x2f FACE_HINTNODE | hold; yaw `0x102d12e0(hint)` into the store; turn | running |
| 0x2c | 0x10282dfb | 0x30 | `SetIdealActivity(GetHintActivity(hint+0x5dc))` | running |
| 0x2d | 0x10283b9e | 0x31 FACE_TARGET | null → fail **1** @0x61c; hold; yaw to target; turn | running |
| 0x2e | 0x10283aad | 0x32 | hold; yaw to `m_vecLastPosition`; turn | running |
| 0x2f | 0x10283ae3 | 0x33 | hold; `AngleMod(GetAngles().y)` into the store | complete |
| 0x30..0x36 | 0x10284286 … 0x102842fb | 0x34..0x3f | `m_flLastAttackTime = curtime` (not RELOAD / SPECIAL); `RestartIdealActivity` 0x19, 0x1b, 0x4b, 0x4e, 0x54, 0x5e, 0x5f | running |
| 0x37 | 0x10282a17 | 0x40, 0x41 | hint held → complete; else `0x102d1af0(this, 0, type, 2000)`, null → fail 4 @0x45a; 0x40 returns, 0x41 falls into 0x39 | — |
| 0x38 | 0x10282d44 | 0x42 | `0x102d1420(hint, 0.0)`; hint = 0 | complete |
| 0x39 | 0x10282a79 | 0x43 | no hint → fail 4 @0x465; `0x102d1350` false → fail 0x11 @0x46d **and** hint = 0 | complete |
| 0x3a..0x3e | 0x102868a3 … | 0x44, 0x46..0x49 | `DevMsg "SOUND"` / slots 490, 489, 491, 488 | complete |
| 0x3f | 0x102868c9 | 0x4a | slot 508 `SpeakSentence((int)data)` | complete |
| 0x40 | 0x10284311 | 0x4b SET_ACTIVITY | nonzero → `SetIdealActivity`, zero → `m_Activity = 0` | running |
| 0x41 | 0x10282e27 | 0x4c SET_SCHEDULE | `0x102cc1f0` (slot 440 then 446, miss DevMsgs and takes schedule 1); null → fail 5 @0x507; `+0x1b2c = 1`, `+0x1b34 = 0x4fc`; `m_IdealSchedule = (int)data` untranslated; `SetSchedule 0x10280e50` | running |
| 0x42 / 0x45 | 0x10286c45 / 0x10286d58 | 0x4d / 0x51 | `m_failSchedule = (int)data` / `= 0` | complete |
| 0x43 | 0x10286c69 | 0x4e | `0x102ee1c0`: path `+0x28` = `ResolveTaskDistance(data)` | complete |
| 0x44 | 0x10286d1a | 0x50 | `nav+0x40 = (float)(int)data` | complete |
| 0x46 | 0x10282dde | 0x52..0x56 | `SetIdealActivity((int)data)` | running |
| 0x47 | 0x102838e7 | 0x57 | no sound → fail "No sound in list" @0x5eb; `FindCoverPos(sound, sound, (float)m_iVolume, CoverRadius)` miss → fail 8 @0x5fb; type 4, [5] 0x13, tol −2; move-wait | running |
| 0x48 | 0x10283558 | 0x58 | threat = enemy or self; lateral cover → move-wait + complete; `FindCoverPos(threat, threat eye, 0, CoverRadius)` miss → fail 8 @0x5a6; type 6, [5] 0x13, tol −2; hint arrival; move-wait | running |
| 0x49 | 0x102836fa | 0x59 | threat = `m_hEnemy` raw or self; lateral cover only; false → fail 8 @0x5c2; move-wait | complete |
| 0x4a | 0x10282eab | 0x5a | no enemy → fail 6 @0x510; `0x102edae0(&savepos, 0, 30000)` false → fail 7 @0x519; type 4 [5] 0x13 tol −1; false → fail 0xc @0x524 | complete |
| 0x4b..0x4d | 0x102833ac / 0x10283035 / 0x102831ea | 0x5b / 0x5c / 0x5d | fail 6 @0x566/0x52d/0x549; radii (0, CoverRadius) / (0, ResolveTaskDistance) / (ResolveTaskDistance, CoverRadius); fail 8 @0x57a/0x541/0x55e; type 6 [5] 0x13, tol −1/−1/−2; hint arrival | running |
| 0x4e | 0x102837c9 | 0x5e | `FindCoverPos(origin, eye, 0, CoverRadius)` miss → fail 8 @0x5d5; type 4 [5] 0x13 tol −2; move-wait | running |
| 0x4f | 0x10286801 | 0x5f, 0xdf | `ClearGoal`; `m_lifeState = 1` | running |
| 0x50 | 0x102868f2 | 0x60 | cine pre-idle set → slot 584 `StartSequence`, `strcmp(play, idle) == 0` → `m_flPlaybackRate = 0`; else `m_scriptState != 6` → `SetIdealActivity(1)` | running |
| 0x51 | 0x102869e0 | 0x61 | pick `m_iszPlay`, else `m_iszPostIdle`; `+0x5d7c = −1`, `+0x5d80 = 0`; `+0x5d7c = ActivityList_IndexForName`, −1 parks the name in `+0x5d80` | complete |
| 0x52 / 0x53 | 0x10286adb / 0x10286b0a | 0x62 / 0x63 | `HasMovement(GetSequence())` discarded, empty `0x1027f270`; `m_scriptState = 0` / `2` | running |
| 0x54 | 0x10286b2a | 0x64 | `DelayStart(cine, 0)` | complete |
| 0x55 | 0x10286b54 | 0x65 | target → slot 62 `SetOrigin(target->GetAbsOrigin())` | complete |
| 0x56 | 0x10286b96 | 0x66 | target → hold, `AngleMod(target->GetAngles().y)` into the store; `m_scriptState != 6` → turn; `ClearGoal` | running |
| 0x57 | 0x10283dae | 0x67 | `m_flWaitFinished = curtime + RandomFloat(0.1, data)` | running |
| 0x58 | 0x10282d71 | 0x69 STOP_MOVING | no goal → `m_bShouldMove = 0`, complete; else `ClearGoal`, `move_yaw` present → `SetPoseParameter(move_yaw, 0)` | either |
| 0x59 / 0x5a | 0x10282926 / 0x10282848 | 0x6a / 0x6b | hold; `AngleMod(AngleMod(yaw) ± data)` into the store; turn | running |
| 0x5b / 0x5c | 0x102829bd / 0x102829e9 | 0x6c / 0x6d | `m_afMemory |= / &= ~(int)data` | complete |
| 0x5d | 0x10286749 | 0x6e | `path+0x10` cleared; no waypoint → `m_bShouldMove = 0`, complete, `ClearGoal`; goal active → `m_bShouldMove = 1`, slot 528; else `m_bShouldMove = 0`, `SetIdealActivity(GetStoppedActivity)` | either |
| 0x5e | 0x102866bf | 0x6f | refresh; no goal or slot 251 finished → `m_bShouldMove = 0`, complete; else `= 1`, slot 528 | either |
| 0x5f | 0x10286d78 | 0x70 | `Weapon_FindUsable(1000³)`; `SetTarget`; null → fail 3 @0xaca | complete |
| 0x60 / 0x61 | 0x10286df5 / 0x102864d7 | 0x71 / 0x72 | `SetIdealActivity(0x5c)` / `SetMovementActivity(0x13)` | running |
| 0x62 | 0x10286e0b | 0x73 | `SetHullSizeSmall(0)` | complete |
| 0x63 | 0x10284e4a | 0x75 | ray origin+fwd·256 → −500 z, mask `0x46004003`; type 4 at endpos tol −1; `SetGoal(..,2)` true → complete ×2; false → fail "gah" @0x82f, then complete | complete |
| 0x64 | 0x10286ec2 | 0x76 WANDER | `SetWanderGoal(n/10000, n%10000)` false → fail 0x18 @0xb05 | complete |
| 0x65 | 0x102869a6 | 0x77 FREEZE | `m_flPlaybackRate = 0` | running |
| 0x66 | 0x10286c9f | 0x9f | no weapon → fail 3 @0xab3; `0x102ee1c0`: path `+0x28` = `(float)(int)(weapon+0x8c0 · data)` | complete |
| 0x67 / 0x68 | 0x10286e2a / 0x10286e76 | 0xac / 0xad | `ChooseBest{Melee,Ranged}Weapon` false → fail 0x1f @0xae6/0xaf1 | complete |
| 0x69 | 0x10285ff8 | 0x120 PATHCORNER | `m_target` empty → fail **0x13** @0x921; type 3 at `m_pGoalEnt->GetOrigin` (unchecked), [5] = fly ? 0x22 : 9, tol −1, flags 1; false → `DevWarning "Can't Create Route!"` | running |
| 0x6a | 0x10286f63 | the rest | `DevMsg "No StartTask entry for %s"` (slot 449) | running |

#### Reads and writes (base words)

Reads `m_hTargetEnt +0x5ce4`, `m_hEnemy +0x5ce0` (arm 0x49 only; elsewhere slot 167), `m_hCine +0x5d74`,
`m_scriptState +0x5d70`, `+0x5d7c`/`+0x5d80`, `m_pHintNode +0x5ddc`, `m_vecLastPosition`,
`m_qaLastFacing`, `m_vSavePosition`, `+0x5df4..+0x5e04`, `m_flDistTooFar +0x5de4`, `m_target +0x20c`,
`m_pGoalEnt +0x5de8`, `m_LastHitGroup +0x1594`, `motor+0x28/+0x34/+0x1c`. Writes the fields listed in the
table plus `+0x1b2c..+0x1b48` (the trace words, ABSENT in the port).

#### Retail defects reproduced

- `TASK_GET_PATH_TO_GOAL` mode 2 hands `SetGoal` a record whose dest was never filled (the cover
  point stays on the stack at `+0x7c`).
- `TASK_GET_PATH_TO_GOAL` mode 2's cover miss fails twice (8 then 0xc); the second reason stands.
- `TASK_GET_PATH_TO_PATHCORNER` fails an empty `m_target` with `FAIL_NO_SCENT`.
- `TASK_GET_DROPSHIP_DEPLOY_PATH` completes after failing (blocked) and twice on success.
- `TASK_LOCK_HINTNODE`'s claim failure also drops the hint.

#### Port

Named divergences: `FindCoverPos` honours the maximum radius only (`IElysiumNpcMotor::FindNodeCover`);
the arrival activity / direction and the route-search deferral are recorded and not executed (the
mover has neither); `m_flPlaybackRate` / `path+0x2c` live on the Troika record
(`FElysiumNpc::SequencePlaybackRate`, `ScheduleHost.NavigationActivity`), so a base-only NPC has
neither; `path+0x28` is the base's `NavPathToleranceCm` (arms `0x43` / `0x66` write it through
`0x102ee1c0`, not `m_flGoalTolerance`); `SetGoal` claims the Troika's schedule body before commanding the mover.
Crash guards replace the unguarded reads of arms 0x0f, 0x2b, 0x2c, 0x50, 0x51, 0x54, 0x69 and the
null `Task_t`.

**Unrecovered:** the BSS default dest / pTarget words (`0x1093404c..54`, `0x10923a30`); the fourth
goal word's meaning beyond "arrival sequence"; `0x1030b550`/`0x1030b5b0`/`0x102f13d0` (arrival
activity / sequence setters, the navigator's post-route call); the deferred-route retry
(`0x102f1dc0`'s bit-`0x20` arm, `nav+0x44/+0x48/+0x4c`); `path+0x10`'s meaning (cleared by
`0x102ee2c0`); `path+0x20`'s meaning; the `+0x1c`-not-180 arm `0x102e0a80`; `CAI_Enemies`'s slot
`+0xe8` call on a missed `GetLastKnownPosition`; the retail activity numbering of an `Activity:`
operand (the port interns names); `motor+0x28`'s retail name.

### `CAI_BaseNPCTroika::StartTask` `0x102a1910` — the dispatch and the arms in `[0x102a1943, 0x102a5046)`

_Recovered 2026-09-28, 0019 story 8 pass I (lane L01)._

Slot 442 on the Troika line (`FElysiumNpc::StartTaskSlot442`,
`Source/ElysiumUE/Private/Substrate/ElysiumNpcStartTask.cpp`). The arms from `0x102a5046` on are
the next section's (`StartTaskTroikaTail`, `ElysiumNpcStartTask_2.cpp`). Every statement cites the
listing; the per-arm long form is `walk-19-29-pack-13.md` in the research tree, corrected where
listed at the end.

#### The contract

`param_1` is the compiled `Task_t` (`[0]` task id, `[1]` the operand word). Every arm leaves by one of:
`TaskComplete(false)` (`0x10273e80`, the shared `break` tail `0x102a66d7`, or `0x102a4e51` inside
this range); `TaskFail(reason)` (slot 448, `vtable +0x700`), preceded at every site but three by
`+0x1b44 = "E:\Vampire\main\dlls\AI_BaseNPCTroika.cpp"` (`0x105da024`) and `+0x1b48 = line`; a plain
return to `0x102a77ea` that leaves the task RUNNING for `RunTask` (`0x102aacf0`); or the base forward.
The three fail sites with no line pair are `0x102a1eef` (`TaskFail(0x1a)`), `0x102a6ba5`
(`TaskFail(0xe)`) and the dialog pair `0x102a4dcc` (`0x17`) / `0x102a4e67` (`0x15`).

#### The dispatch

`EDI = task` (`0x102a191a`); `ECX = [EDI] - 5` (`0x102a1925`); `CMP ECX,0x144; JA 0x102a77e2`
(`0x102a1928`): an id past `0x149` goes to `CAI_BaseNPC::StartTask 0x102827f0` with the task
pointer. Otherwise `MOV DL,[ECX+0x102a7ab8]` (the 325-byte index table) and
`JMP [EDX*4+0x102a77f8]` (176 arm addresses, ascending id). Ghidra prints the index as the case
label.

#### The shared tails this range uses

| tail | body |
|---|---|
| `0x102a66d7` | `TaskComplete(false)`, return |
| `0x102a4e51` | the same, reached from `0x102a33cf`, `0x102a4928`, `0x102a4e51` |
| `0x102a44d1` | motor stop-turn `0x102e0b40`; `0x102e2020(motor, &point, 0)`; slot 572 `SetTurnActivity`; return RUNNING |
| `0x102a76a4` | `SetGoal(goal, 0)` (answer dropped), then `0x102a76a9` `m_flMoveWaitFinished (+0x5cf0) = curtime + data`; return RUNNING |
| `0x102a4186` | goal literal type 4, activity `0x13`, tolerance `-2.0` (`0x1049a1b0`), then `SetGoal(goal, flags)` with the caller's pushed flags; answer dropped; RUNNING. `0x102a4178` stores the two literal words `+0x24`/`+0x3c` first |
| `0x102a42c0` / `0x102a42c7` / `0x102a42df` | write `m_flGoalTolerance (+0x6320)`, `0x102ee1c0(nav, tol)` (the path's goal tolerance), `0x102f2fe0(nav, +0x6320)` (the arrival distance), `TaskComplete`. `0x102a42c7` is entered with a HALVED value for `0x102ee1c0` only |
| `0x102a46d0` | `+0x1b44` file, `TaskFail(0x1f)`, then `CAI_BaseNPC::AutoMovement 0x10280a50` |
| `0x102a6ba5` | `TaskFail(0xe)` |

The goal literal (`AI_NavGoal_t`, 0x40 bytes) is read by `SetGoal 0x102ecd20` as: `+0x00` type
(1 `m_hTargetEnt`, 2 the enemy, 7 slot 586 `GetBestSeeUnknown`, anything else the destination),
`+0x04` destination, `+0x10` destNode, `+0x14` movement activity (`-1` keeps it, else
`0x102ee250`), `+0x18`/`+0x1c` arrival activity/sequence, `+0x20` tolerance (`-2` hull width
`0x102d61b0(+0x156c)`; `-1` keeps the path's `+0x28`, or the hull width when that is zero), `+0x24`
goal flags (bit 1 routes node to node through `0x102f3c10`/`0x102f41b0`/`0x102fd240`), `+0x28` the
target entity (`DAT_10923dd8` in every literal here but `0x102a4d8f`), `+0x2c..+0x34` the arrival
direction (`DAT_10934060..68`). The second `SetGoal` argument's bit 2 calls `0x102f28a0` on a refused
route.

#### The arms, ascending task id

**`0x06` TASK_SUGGEST_STATE — `0x102a1b2b`.** Stamps the selector pair `+0x1b3c/+0x1b40 = 0x2dff`;
`m_IdealNPCState (+0x5cc4) = (int)data` (`0x102a1b43`/`0x102a1b4e`). `m_bfNPCFrenziedFlags (+0x5b84)
& 1` (`0x102a1b54`): ideal 1 -> line `0x2e05`, ideal `0xb` (`0x102a1ba6`); ideal 3 -> line `0x2e06`,
ideal `0xb` (`0x102a1b76`); any other -> complete. Not frenzied: `m_bNoAlertState (+0x65f6)` and
ideal 3 -> line `0x2e0e`, ideal 1 (`0x102a1bed`). Every arm completes.

**`0x0f` TASK_GET_PATH_TO_ENEMY — `0x102a33f9`.** Slot 530 `IsUnreachable(GetEnemy())` -> line
`0x3101`, `TaskFail(0xc)`. No enemy -> line `0x3109`, `TaskFail(6)`. Goal type 2, destination the
`DAT_1093404c` sentinel, tolerance -1, `SetGoal(goal, 0)`: true completes; false ->
`DevWarning(2, "GetPathToEnemy failed!!\n")`, `RememberUnreachable(GetEnemy())` (`0x10274080`), line
`0x311a`, `TaskFail(0xc)`.

**`0x16` TASK_GET_PATH_TO_HINTNODE — `0x102a371d`.** `m_pHintNode (+0x5ddc) == 0` -> line `0x314a`,
`TaskFail(4)`. `0x102b6120(this, &pos, 0)` (the lean offset), goal type 4, activity `0x13`,
tolerance -1, `SetGoal(goal, 0)`, RUNNING, answer dropped.

**`0x2e` TASK_FACE_ENEMY — `0x102a4417`.** `m_hShootTargetOverride (+0x5ba8)` resolved -> its
`GetAbsOrigin`; else the enemy's LKP (`GetEnemies()->0x102dfed0`). Slot 364 `FInAimCone(point)`:
true completes (`0x102a44cb`), false is the turn tail at that point.

**`0x2f` TASK_FACE_HINTNODE — `0x102a382a`.** Motor stop-turn. `m_pHintNode` is dereferenced
UNGUARDED (`0x102a3841 CMP [hint+0x5dc],0x27d8`). Type `0x27d8`: yaw = `0x102d12e0(hint)` `+45.0`
(`0x1049949c`) when `m_bLeaningLeft (+0x63fd)`, `-45.0` otherwise; other types: the hint yaw as is.
`motor+0x28` set flips it: `< 180 ? +180 : -180` (`0x1044c3a8`). `motor+0x1c == 180.0f` stores it
straight to `motor+0x34` (`0x102a38a9`), else `motor+0x34 = 0x102e0a80(motor, yaw)` (`0x102a399f`).
Slot 572, RUNNING.

**`0x34` TASK_RANGE_ATTACK1 — `0x102a4505`.** A weapon AND `m_bfAINPCFlags2 (+0x14bc)` bit 15
(`DISABLE_BURST_FIRE`) clear (`NOT; TEST AH,AH; JNS`) -> `m_iBurstFireCount (+0x6490) =
RandomInt(data+0x3a4, data+0x3a8)` over the weapon's data (`0x10003d91`); otherwise 1. RUNNING.

**`0x35` TASK_RANGE_ATTACK2 — `0x102a4576`.** `m_flLastAttackTime (+0x5d9c) = curtime`,
`RestartIdealActivity(0x1b)`, RUNNING.

**`0x36`/`0x37` TASK_MELEE_ATTACK1/2 — `0x102a45c6`.** Motor stop-turn. A weapon whose slot `+0x5a0`
carries `0x18000` -> `m_flLastAttackTime = curtime`, weapon slot `+0x51c` for `0x37` else `+0x518`,
`AutoMovement`, RUNNING. Otherwise line `0x3302` -> `0x102a46d0`.

**`0x3e`/`0x3f` TASK_SPECIAL_ATTACK1/2 — `0x102a459a` / `0x102a45b0`.** `RestartIdealActivity(0x5e)` /
`(0x5f)`, RUNNING.

**`0x4b` TASK_SET_ACTIVITY — `0x102a1c0f`.** `act = (int)data`; 0 writes `m_Activity (+0xfec) = 0`,
else `SetIdealActivity(act)` (`0x10272650`). `m_flWaitFinished (+0x5db4) = curtime + 1.0`. Slot 464
`GetState() == 4` -> `AdvanceToIdealActivity 0x102726a0`. No exit: RUNNING either way.

**`0x4e` TASK_SET_TOLERANCE_DISTANCE — `0x102a4289`.** `m_flGoalTolerance = 0x102d61b0(m_eHull
+0x1568) * 0.5` (`0x10449270`, double), `+= ResolveTaskDistance(data)`, tail `0x102a42c0`.
`0x102d61b0(hull)` is `hullTable[hull]+0x18 - +0xc`, the hull's X width.

**`0x4f` TASK_SET_TOLERANCE_DISTANCE_ABS — `0x102a42fa`.** `m_flGoalTolerance =
ResolveTaskDistance(data)`, the same two writes, complete.

**`0x5a` TASK_FIND_BACKAWAY_FROM_SAVEPOSITION — `0x102a1c6a`.** `d = ResolveTaskDistance(data)`;
`0x102edae0(nav, &m_vSavePosition (+0x5dd0), d, 64.0, &out)`; false -> line `0x2e3f`,
`TaskFail(7)`. Goal type 4, activity `0x13`, tolerance -1, `SetGoal(goal, 0)`: true completes, false
-> line `0x2e4a`, `TaskFail(0xc)`.

**`0x6e` TASK_WAIT_FOR_MOVEMENT — `0x102a1dcc`.** (1) `0x102ee2e0(nav)` reads `CAI_Path+0x10`, a byte;
set -> `0x102bf7e0`, which clears that byte (`0x102ee2c0` -> `0x1030bea0 MOV byte [path+0x10],0`)
and sets `m_bShouldMove`. (2) `0x102ee620(nav)` = `path+0x5c`, the goal TYPE `SetGoal` stores through
`0x1030ba50` (with `path+0x58 = 1`): zero -> `m_bShouldMove = 0`, complete, `0x102ee270` (clear the
goal). (3) else `0x102ee6a0` (path and its `+0x24` current waypoint both live) false ->
`m_bShouldMove = 0`, `SetIdealActivity(0x1027a6c0())`, complete. (4) else `0x102f2ea0` (planar
distance² to the goal, stored at `nav+0x14`, under the path tolerance and the height gap under slot
522, then the navigator's `+0x20`) true -> `m_bShouldMove = 0`, complete; false -> `m_bShouldMove =
1`, slot 528 `ValidateNavGoal`. Then, after every arm: `FCOMP curtime` against
`m_flTeleportMoveTimer (+0x65dc)`, `TEST AH,0x41; JP` — returns when `curtime > timer`, so the
rescue runs INSIDE the window: `0x102e7880(moveProbe, 0x102ee140(nav), 0x202400b, 1.0, -1024.0,
&out, &hit)` false -> `TaskFail(0x1a)`; no hit entity, or a hit whose `+0x94` (`m_pBaseNPC`) is set
-> `TaskFail(0xe)`; otherwise slot 216 `SetAbsOrigin(out)`.

**`0x79` TASK_GET_PATH_TO_BESTUNKNOWN — `0x102a3599`.** Slot 586 handle dead -> line `0x3127`,
`TaskFail(0x21)`. Goal type 7, tolerance -1, `SetGoal(goal, 0)`: true completes; false ->
`DevWarning(2, "GetPathToBestUnknown failed!!\n")`, `0x10274080(this, u)`, line `0x3134`,
`TaskFail(0xc)`.

**`0x7a`/`0x7b` TASK_GET_PATH_TO_PATROL_POINT(_HUNT) — `0x102a39be` / `0x102a39d9`.**
`0x102aa640(this, &m_sppPatrolPath)` with the smart pointer's address, `+0x658c` / `+0x6594`; the
helper owns the exit.

**`0x7c` TASK_GET_FULL_PATROL_PATH — `0x102a39f4`.** `p = [+0x6590]` (the pointee); null -> line
`0x3186`, `TaskFail(0x1d)`. `node = p[p[+0x10]*4 + 0x14]`; -1 -> RUNNING. The navigator's node array
(`nav+0x2c`, count `[0]`, entries `[1]`) bounds-checks it; out of range increments
`DAT_106c994c` and yields null — and `0x102fb0d0 CAI_Node::GetPosition(m_eHull)` is then called on
null (a crash). Goal type 4, destination the node position, destNode -1, activity -1, tolerance -1,
flags 0, `SetGoal(goal, 2)`: true completes; false -> `DevWarning(2, "%s can't reach patrol
point\n", GetDebugName())`, line `0x31a0`, `TaskFail(0xc)`.

**`0x7d`/`0x7e` TASK_NEXT_PATROL_POINT(_HUNT) — `0x102a3b91` / `0x102a3bac`.**
`0x102aa9e0(this, &m_sppPatrolPath(+0x658c) / Hunt(+0x6594))`.

**`0x7f`/`0x80` TASK_GET_DIRECTED_PATH_TO_ENEMY_LKP(_RND) — `0x102a3d48`.** No enemy -> line
`0x3205`, `TaskFail(6)`. `d` = the RAW operand; `0x80` only: `d = RandomFloat(d*0.5, d)`.
`GetEnemies()->0x102e0290(enemy, &lkp, &seen)` false -> line `0x323a`, `TaskFail(6)`.
`bOk = 0x102ee300(nav, this, &lkp, &seen, d, &point)`. The literal (type 4, activity -1, tolerance
-1), then slot 563 `TranslateEnemyChasePosition(enemy, &goal.dest, &goal.tolerance, &tol)` with `tol`
a copy of `m_flGoalTolerance` — unconditionally. `bOk && SetGoal(goal, 2)` -> `0x102ee1c0(tol)`,
`0x102f2fe0(tol)`, complete; otherwise `DevWarning(2, "GetDirectedPathToEnemyLKP failed!!\n")`,
`0x10274080(this, GetEnemy())`, line `0x3235`, `TaskFail(0xc)`.

**`0x81` TASK_GET_DIRECTED_PATH_TO_ENEMY_LKP_LOS — `0x102a3f84`.** No enemy -> `0x3245`,
`TaskFail(6)`; no LKP -> `0x3276`, `TaskFail(6)`; `0x102ee300(nav, this, &lkp, &seen, data, &dir)`
false -> `0x3271`, `TaskFail(0xb)`. Range: max 2000.0 (`0x44fa0000`), min 0; armed: max =
`max(w+0x8c0, w+0x8c4)`, min = `min(w+0x8b8, w+0x8bc)` (the SMALLER, `0x102a40a3 AND 0x4100; JNZ`);
max clamped down to `m_flDistTooFar (+0x5de4)`. `0x102edaa0(nav, &dir, &dir+enemy view offset
(+0x184), min, max, 1.0, 0, &out)`: false -> RUNNING with no fail; true -> `0x102a4186` flags 2.

**`0x83`/`0x84` TASK_GET_PATH_TO_FLEE_NODE / TASK_GET_PATH_TO_COWER_NODE — `0x102a2882`.** Target =
enemy or this. `d = ResolveTaskDistance(data)`, `max = d + 8192.0` (`0x1049ae70`), `mid = (max +
d) * 0.5`. A held hint -> `ClearHintNode(1.0)`. `m_pHintNode = 0x102d1af0(this, 0x2774, 2, max, 0,
0)`; found: `0x102d1350(hint, this)` refused -> `m_pHintNode = 0`; claimed -> `0x102d1180(hint,
this, &pos)`, goal type 4, activity `0x13`, tolerance -1, `SetGoal(goal, 0)`: true -> slot 16's
extents into `m_vecSavedSleepExtents (+0x65d0)`, `SetAbsoluteAttackExtents((40,40,80))`
(`0x1009b060`: `SetAttackExtents(abs - (maxs-mins)*0.5)`), complete; false -> `ClearHintNode(5.0)`.
Still holding a hint -> return (`0x102a2a68`). `0x84` only: `m_bfAINPCFlags |= 0x200`
(`COWER_PATH`). `0x102edc80(nav, target->GetOrigin(), target->EyePosition(), mid, max, &out,
target)`, retried with `(d, max)`; both false -> line `0x2fe8`, `TaskFail(0x18)`. Success: goal type
6, activity `0x13`, tolerance -2, `SetGoal(goal, 0)`, RUNNING.

**`0x85` TASK_GET_PATH_TO_COWER_NODE_SAVE_POS — `0x102a2bd8`.** `COWER_PATH` set unconditionally
(`0x102a2bfc`). From = `m_vSavePosition`, to = it + this body's view offset (`+0x184..+0x18c`).
`0x102edc80(from, to, (max+d)*0.5, max, &out, this)` then `(from, to, d, max, &out, this)`; both
false -> line `0x3023`, `TaskFail(0x18)`. Goal type 6, activity `0x13`, tolerance -2, flags 0,
RUNNING.

**`0x86` TASK_FIND_FOLLOWER_BACKAWAY_SIMPLE — `0x102a2d9b`.** `m_hFollowerBoss (+0x647c)` dead ->
line `0x3051`, `TaskFail(0x29)`. `yaw = UTIL_VecToYaw(self - boss)` (`0x1000612c`) +
`RandomFloat(-45, 45)`; candidate = `SELF + m_flFollowerDistanceBackAway (+0x6484) *
UTIL_YawToVector(yaw)` (`0x1000ecd2`; the sum's base is `[ESP+0x28]`, this body's origin). A 14-dword
trace is zeroed, `0x102e6d70(moveProbe, 0, self, candidate, 0x202400b, 0, 100.0, 0, &trace, 0, 0)`:
false -> line `0x304c`, `TaskFail(7)`; true -> `0x102a4178` with flags 0.

**`0x87` TASK_FIND_FOLLOWER_BACKAWAY_NODE — `0x102a2f94`.** Boss dead -> `0x306f`, `TaskFail(0x29)`.
`0x102edae0(nav, bossOrigin, m_flFollowerDistanceWalkTo (+0x6488) - 10.0, 50000.0, &out)`: false ->
`0x306a`, `TaskFail(7)`; true -> `0x102a4186` flags 0.

**`0x88` TASK_FIND_FOLLOWER_BACKAWAY_ASTAR — `0x102a3096`.** The same through `0x102edbb0`; lines
`0x308c` / `0x3087`.

**`0x9a` TASK_MELEE_KICK — `0x102a464d`.** Motor stop-turn; `cast = __RTDynamicCast(weapon, 0,
0x1055f710, 0x105da324, 0)`. Weapon AND `slot(+0x5a0) >> 30 & 1` AND cast -> `m_flLastAttackTime`,
`cast->+0x5d0(0x53, 0, 0)`, `AutoMovement`, RUNNING; otherwise line `0x331d` -> `0x102a46d0`.

**`0x9f` TASK_SET_MELEE_TOLERANCE_DISTANCE — `0x102a434a`.** `hull = enemy ? enemy+0x9c ->
+0x1568 : 0`. Armed: `m_flGoalTolerance = 0x102d61b0(hull) * 0.5 + weapon+0x8c0 * data` (RAW
operand); unarmed: `+ ResolveTaskDistance(data)` into `0x102a42c0`. Complete.

**`0xa0` TASK_FIND_FAST_COVER_FROM_ENEMY — `0x102a20dc`.** Threat = enemy or this.
`0x102784a0(threat->EyePosition(), threat)` true -> `m_flMoveWaitFinished = curtime + data`,
complete. Else `0x102edc80(nav, threat->GetOrigin(), threat->EyePosition(), 0, 150.0, &out,
threat)`: false -> `0x2ef8`, `TaskFail(8)`; true -> goal type 6, activity `0x13`, tolerance -2,
`0x102a76a4`.

**`0xa1` TASK_FIND_FORWARD_COVER_FROM_ENEMY — `0x102a232e`.** The same lateral test, then
`0x102edd50(nav, origin, eye, &m_vecForward (+0x6290), 0.0, CoverRadius() * 0.25, &out, threat)`:
false -> `0x2f32`, `TaskFail(8)`; true -> goal type 6, `0x102a76a4`.

**`0xa2` TASK_FIND_COVER_FROM_SAVEPOSITION — `0x102a2231`.** `0x102edc80(nav, &m_vSavePosition,
&m_vSavePosition, 32.0, CoverRadius(), &out, this)`: false -> `0x2f0b`, `TaskFail(8)`; true -> goal
type 4, activity `0x13`, tolerance -2, `0x102a76a4`.

**`0xa3` TASK_FIND_FLANK_NODE_TO_ENEMY — `0x102a24b1`.** No enemy -> `0x2f3c`, `TaskFail(6)`. LKP
(`0x102dfed0`); range as `0x81` (max 2000 / `max(0x8c0,0x8c4)`, min `min(0x8b8,0x8bc)`, clamp to
`m_flDistTooFar`). `curtime - 0x102e0150(enemy) >= 2.0` (`0x10452dc4`) -> `0x102edaa0(nav, &lkp,
&lkp+enemy view offset, min, max, 1.0, 0, &out)`; fresher -> the enemy's forward
(`0x10139610(enemy->GetAbsAngles())`) and `0x102ed9c0(..., min, max, 1.0, &fwd, 0, &out)`. False ->
`0x2f71`, `TaskFail(0xb)`. True -> goal type 4, activity `0x13`, tolerance -2, `SetGoal(goal, 2)`,
then `0x102ee530(nav, lkp - out)`; RUNNING.

**`0xa4` TASK_FIND_INTERESTING_PLACE — `0x102a1f23`.** `m_pInterestingPlace (+0x62ec) =
PickRandomInterestingPlace(this)` (`0x102db590`); null -> `0x2eaf`, `TaskFail(0x22)`.
`PickSpotFor(place, this, &m_vecInterestingPlace (+0x62f0), 1)` (`0x102da0d0`): false -> `+0x62ec =
0`, `0x2eaa`, `TaskFail(0x22)`; true completes.

**`0xa5` TASK_GET_PATH_TO_INTERESTING_PLACE — `0x102a1fc3`.** No place -> `0x2ed0`,
`TaskFail(0x22)`. Goal type 8, destination `m_vecInterestingPlace`, activity -1, tolerance -1,
`SetGoal(goal, 0)`: complete / `0x2ecb`, `TaskFail(0xc)`.

**`0xa6` TASK_PAUSE_MOVING — `0x102a1f06`.** `0x102bf770(this)`, complete.

**`0xa7` TASK_TEST1 — `0x102a1943`.** With a weapon: `debug_test_switch1` (`DAT_1092467c`,
`!IsCommand() && m_nValue`) -> weapon `+0x10c`, else `+0x108`. `origin.x/y += RandomFloat(-200,
200)`; `0x10142aa0` draws a ±2 box; then the turn tail at that point.

**`0xa8` TASK_TEST2 — `0x102a1a60`.** `v = debug_test_switch2` (`DAT_10924634`); `DEC; CMP 3; JA`,
four-entry table `0x102a7c00`: 1 -> 5, 2 -> `0x1118`, 3 -> `0x19`, 4 -> `0x58`, else 1;
`RestartIdealActivity(v)`, complete.

**`0xae`/`0xaf` TASK_CREATE_HUNT_PATROL_LIST / TASK_FIND_HUNT_PATROL_TARGET — `0x102a3bc7` /
`0x102a3c5d`.** Slot 168 (`+0x2a0`) target's origin or null; `m_pPathfinder (+0x5d3c)->0x10306700
(this, origin, target, 256.0)` / `0x10306f60(..., &m_vecHuntPatrolTarget (+0x645c))`: complete /
`0x31cd` / `0x31e8`, `TaskFail(0x20)`.

**`0xb0`/`0xb1` TASK_WAIT_ATTACK_TIME1/2 — `0x102a337d`.** No weapon -> complete.
`m_flWaitFinished = 0x10252450(weapon, id == 0xb1) + 0x102c5730(this, weapon)`; `<= curtime` ->
complete (`0x102a4e51`); a held hint -> RUNNING; else `RestartIdealActivity(5)`, RUNNING.

**`0xb9` TASK_RUN_DIALOG — `0x102a496b`.** `a = 0x102c1400(this)`; -1 completes; else slot 310
`SetActivity(a)`, motor stop-turn, `0x102e1e20(motor, -1)`, RUNNING.

**`0xba`/`0xbc` TASK_RUN_DISPOSITION / TASK_SPECIAL_IDLE_ACTIVITY — `0x102a49bc`.**
`m_flWaitFinished = curtime + data`, RUNNING. **`0xbb`/`0xbd` — `0x102a49da`.** `= RandomFloat(0,
data) + curtime`, RUNNING.

**`0xbe` TASK_ADD_EVENT_EXPRESSION — `0x102a4a07`.** `i = (int)data`; `0 <= i < 2` ->
`AddExpressionForEvent(i)` (`0x101072b0`), complete; else `"Invalid event expression: %d\n"` with `i`
(through the print import `[0x109f364c]`, not the DevWarning import `[0x109f3658]`), complete.

**`0xc4` TASK_SET_PRESERVE_PATH — `0x102a3cfa`.** `(int)data` non-zero -> `m_bfAINPCFlags |= 8`, else
`&= ~8`; complete.

**`0xc5` TASK_SET_ENEMY_ELUDED — `0x102a46fa`.** No enemy -> `0x3333`, `TaskFail(6)`;
`GetEnemies()->0x102dfd90(enemy)`, complete. **`0xc6` TASK_SET_TARGET_ELUDED — `0x102a4763`.**
`m_hTargetEnt (+0x5ce4)` dead -> `0x3341`, `TaskFail(1)`; the same write, complete.

**`0xc7` TASK_GET_PATH_TO_ENEMY_CLOSEST — `0x102a4812`.** No enemy -> `0x334c`, `TaskFail(6)`. Goal
type 4, destination `enemy->GetOrigin()` (slot 220), activity -1, tolerance -1, goal flags 2,
`SetGoal(goal, 0)`: complete (`0x102a4e51`) / `DevWarning(2, "GetPathToEnemy failed!!\n")`,
`0x335a`, `TaskFail(0xc)`.

**`0xcf`/`0xd0` TASK_SET_INSIDE/OUTSIDE_INTERRUPT_DIST — `0x102a4a5b` / `0x102a4a97`.**
`i = (int)ResolveTaskDistance(data)`; `+0x6324` / `+0x6328` = `(float)(i*i)` (`IMUL`); complete.

**`0xd3`/`0xd4`/`0xd5` interrupt time — `0x102a4ad3` / `0x102a4afb` / `0x102a4b2e`.**
`m_flInterruptTime (+0x632c) = curtime + data` / `+= RandomFloat(0, data)` / `= 0`; complete.

**`0xd6` TASK_WALK_RUN_PATH — `0x102a4b4e`.** `d = ResolveTaskDistance(data)`; `d*d <= nav+0x14`
-> `act = TranslateActivity(0x13)`; otherwise `act` is an UNINITIALISED stack word
(`0x102a4b83 MOV EDI,[ESP+0x104]`). `0x10295460(act, -1) == -1` -> `TranslateActivity(9)`.
`0x102ee250(nav, act)`, `m_afMemory (+0x5d8c) &= ~2`, complete.

**`0xd7` TASK_WALK_RUN_PATH_COMBAT_SOUND — `0x102a4bd8`.** `+0x60b4` (the `m_BestSound` record's
second word) is 1 or `0x10` AND `TranslateActivity(0x13) != -1` AND its sequence exists -> run; else
`TranslateActivity(9)`. The same two writes, complete.

**`0xd8` TASK_GET_PATH_TO_PLAYER_FOR_DIALOG — `0x102a4c7a`.** `m_hMoveTargetEnt (+0x6240) =
closest player ? its handle : -1`. Goal type 4, tolerance -1, target = `0x100d1590(&m_hMoveTargetEnt)`,
destination = the move target's `GetOrigin` — called through a NULL pointer when the handle is dead
(`0x102a4d74`). `GetNavigator()->SetGoal(goal, 0)`, RUNNING.

**`0xd9` TASK_WALK_RUN_PATH_FOR_DIALOG — `0x102a4db7`.** `0x102c6460(&m_hClosestPlayer, 0)` (the
handle is null) -> `TaskFail(0x17)` with no line pair. `dist = 0x102a9570(selfOrigin,
playerOrigin)`; `dist >= m_flSpecialDistanceAccum (+0x5bac)` or no ACT_WALK sequence -> ACT_RUN, or
`TaskFail(0x15)` (no pair) when it has none; else ACT_WALK. `0x102ee250(nav, act)`, `0x102a98e0(this,
2)` (`m_afMemory &= ~2`), complete.

**`0xda` TASK_START_PLAYER_DIALOG — `0x102a4e7e`.** The closest player and slot 295 `CanTalk(p)` ->
`p->+0x678(this)`, complete; any miss is the break tail — complete, never a fail.

**`0xdb` TASK_SET_TOLERANCE_DIST_DLG — `0x102a4c47`.** `m_flGoalTolerance = 160.0 +
ResolveTaskDistance(data)`; `0x102a42c7` with HALF of it for `0x102ee1c0`, the whole for
`0x102f2fe0`; complete.

**`0xdc` TASK_DIE_IF_PLAYER_CANT_SEE — `0x102a3198`.** `TaskComplete(false)` FIRST (`0x102a31ca`). No
closest player -> the removal. Else `ResolveTaskDistance(data)` against `m_flPlayerDist (+0x6264)`:
`TEST AH,0x41; JP` returns when the distance is GREATER. Else trace own eye (slot 220 + `+0x184`) to
the player's, mask `0x4091` (`0x1004f7a0`, `0x101d3190`, enginetrace `+0x10`; a debug line when
`0x10005b87(0x10738960)`); fraction `== 1.0` -> return. Removal: `0x102b53d0(this, 0, "Leaving
interesting place (TASK_DIE_IF_PLAYER_CANT_SEE)")` then `UTIL_Remove(this)` (`0x101cd940`).

**`0xdd` TASK_KNOCKOUT — `0x102a3339`.** `m_bfAINPCFlags |= 0x440a0000`, `RestartIdealActivity(0x1050)`,
RUNNING. **`0xde` TASK_UNKNOCKOUT — `0x102a3364`.** `RestartIdealActivity(0x1052)`, RUNNING.

**`0xe0` TASK_DO_JUMP_ACTIVITY — `0x102a4ec7`.** Slot 310 `SetActivity(0x1089)`, RUNNING.

**`0xe1` TASK_DO_LOOP_ACTIVITY — `0x102a4ee3`.** `a = (int)data`; `0x10272130(this, a, &seq, …)`;
`seq > 0` (`JG`) -> slot 310 `SetActivity(a)`, RUNNING; else complete.

**`0xe2`/`0xe3` TASK_DO_BLEND(_LOOP)_ACTIVITY — `0x102a4f38` / `0x102a4fb1`.** Two byte-identical
bodies. `cycle = 0`; slot 271 `FindLayerByOwner(a - 1) != -1` -> `cycle = m_AnimOverlay[layer].m_flCycle`
(`+0x740 + layer*0x30`), slot 274 `RemoveLayerByOwner(a - 1)`. `0x10272130(a)`; `seq > 0` -> slot 310
`SetActivity(a)`, `m_flCycle (+0x6f8) = cycle`, RUNNING; else complete.

#### Corrections to the older record

- `0x102a1b4e`: the ideal state is `+0x5cc4`, not `+0x200`; `0x102a1c41`: `m_flWaitFinished` is
  `+0x5db4`, not `+0x5cc4`.
- `0x102a1e7d`: the teleport rescue runs while `curtime <= m_flTeleportMoveTimer`, not after it.
- `0x102a1dd2`: `0x102ee2e0` tests `CAI_Path+0x10` and `0x102ee2c0` CLEARS it (`0x1030bea0`); it is
  not `IsGoalSet`/`StopMoving`. `0x102ee620` is the goal type `path+0x5c` (`0x1030ba50`).
- `0x102a1a86`: TASK_TEST2's table is `0x102a7c00`, four entries; the 22-entry reading ran into the
  neighbouring table.
- `0x102a259c` / `0x102a40a3`: the minimum range is the SMALLER of `+0x8b8`/`+0x8bc`.
- `0x102a2ebb`: the follower backaway candidate is built from this body's origin, not the boss's.
- `0x102a3a2d..3b29`: TASK_GET_FULL_PATROL_PATH's goal activity is -1, not 0.
- `0x102a39be` / `0x102a3b91`: the patrol arms pass `&m_sppPatrolPath` at `+0x658c` (the smart
  pointer), whose pointee is `+0x6590`.
- `0x102a4bb3`: `m_afMemory` is `+0x5d8c`; `0x102a98e0(this, 2)` is `m_afMemory &= ~2`.
- `0x102a454c`: `m_iBurstFireCount` is `+0x6490`; `+0x6320` is `m_flGoalTolerance`.
- `0x102a4f0c`: TASK_DO_LOOP_ACTIVITY tests the resolved SEQUENCE (`> 0`), not a count.
- `0x102a4a3c`: the invalid-expression line goes through the import `[0x109f364c]`, not `DevWarning`'s `[0x109f3658]`.
- `0x102a3551` / `0x102a492e`: the string is `"GetPathToEnemy failed!!\n"` (two `!`).

**Unrecovered:** the `CAI_Path+0x10` byte's meaning (`0x102ee2e0`); what `0x102edae0` /
`0x102edbb0` / `0x102edd50` / `0x102edaa0` / `0x102ed9c0` / `0x102ee300` search internally; the
weapon words `+0x3a4`/`+0x3a8`/`+0x8b8..+0x8c4` and slots `+0x518`/`+0x51c`/`+0x5d0` by name; the
kick cast's target class `0x105da324`; `0x10252450` and `0x102c5730`; `0x102c1400`; the second
vector `0x102e0290` copies (`record+0x18`, read as the last SEEN position); the `+0x60b4` values 1 and
`0x10` by name; `debug_test_switch1/2`'s default string `0x105399a0`; `0x10142aa0`'s colour
arguments' meaning beyond a debug box.

### `CAI_BaseNPCTroika::StartTask` `0x102a1910` — the second part (arms `0x102a5046`..`0x102a77f7`)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L02)._

Port: `FElysiumNpc::StartTaskTroikaTail`
(`Source/ElysiumUE/Private/Substrate/ElysiumNpcStartTask_2.cpp`), reached from the first part's
switch default. Helpers and seams: `ElysiumNpcStartTask_2.inl`. Tests:
`Elysium.Substrate.NpcKernelStartTask19.TroikaTail.*`.

The dispatch (`id - 5 > 0x144` → base; byte table `0x102a7ab8` → dword table `0x102a77f8`) is the
first part's. This part carries 108 arm entries of the 176: every arm whose start lies in
`[0x102a5046, 0x102a77f7]`, including the two shared tails and the base forward. Arms below are in
retail table order (ascending task id). "Complete" is `TaskComplete(false)` (`0x10273e80`, directly
or through the break tail `0x102a66d7`); "fail(line, r)" is `+0x1b44 = "AI_BaseNPCTroika.cpp"`,
`+0x1b48 = line`, slot 448 `TaskFail(r)`; "running" is a bare return.

#### The tails

- `0x102a77ea` — ids `0x05`, `0xea`, `0xeb`: running, no write.
- `0x102a66d7` — ids `0x8b`, `0x125` (index `0x1d`, the arm Ghidra drops): complete.
- `0x102a77e2` — the base forward, 133 in-range ids plus every out-of-range id.
- `0x102a5904` — the movement-activity tail: `0x102ee250(nav, act)` (path `+0x2c`),
  `0x102a98e0(this, 2)` (`m_afMemory &= ~2`), complete.
- `0x102a7744` — the look tail: `0x10297940(pos)` then `0x102a18a0(task)` (`m_flWaitFinished =
  curtime + operand`, or `+ _DAT_10447ee0` for a non-positive operand).
- `0x102a63fc` — the facing tail: `0x10288670(motor, yaw)`, running.
- `0x102a76a9` — `m_flMoveWaitFinished (+0x5cf0) = curtime + operand`, running.

`0x10297940` (face a point with a turn animation): `0x102e0b40` (motor `+0x2c = -1`), `0x102e2020`
(ideal yaw toward the point), `0x10297a20` (the face-anim pick, writing `m_eFaceAnim` and
`m_flFaceYawDiff`), `m_bfAINPCFlags |= 0x8000000` (`PLAYING_FACE_ANIM`), then ideal yaw =
`GetAbsAngles().y + m_flFaceYawDiff`, flipped by 180 under `motor+0x28`, stored directly when
`motor+0x1c == 180` else through `0x102e0a80`.

#### The arms

- `0x102a733f` `0x8c` MELEE_DODGE_ATTACK: `RestartIdealActivity(0x1155)`, running.
- `0x102a6de4` `0x92` MELEE_KNOCKBACK: slot 266, `RestartIdealActivity(m_knockbackType +0x6068)`.
- `0x102a6e09` `0x93` FLYING_KNOCKBACK_INTO: slot 266; `m_flGravity (+0x3ec) = m_fJumpGravity`;
  `SetAbsVelocity(m_KnockbackVelocity)` (`0x102a96b0`; no separate `m_vecVelocity` store); slot 208
  `SetGroundEntity(0)`; nav type 1 (`0x1027d9b0`); `m_bJumping = 1`; restart `m_knockbackType`.
- `0x102a6e70` `0x94` FLYING_KNOCKBACK_IDLE: `n = 0x10345480()`; `n < 0` → restart `0x8f`, else slot
  311 `ForcePreTranslatedSequenceAndActivity(0x8f, 0x8f, n)`.
- `0x102a6ef7` `0x95`/`0x98`: `0x102c41b0(this, "impact_dust_emitter", 0, 0, 0)`.
- `0x102a6eb4` `0x96`: `m_fKnockbackWallHitFallTime (+0x6014) = curtime + 0.01`.
- `0x102a6ed5` `0x97`: `0x102c4e80` (`SetMoveType(4, 0)`, `m_flGravity = m_fJumpGravity`), then
  `SetAbsVelocity(m_KnockbackVelocity)`.
- `0x102a6f16` `0x99`: `GetBonePosition01("Bip01 Spine2")`; `+0x6018 = curtime`; `+0x601c = pos`.
- `0x102a6fc5` `0x9b`: `m_flLastMeleeStepbackTime (+0x606c) = curtime`; complete.
- `0x102a6f70` / `0x102a6f93` / `0x102a6fac` `0x9c..0x9e`: (`FearSound` first on `0x9c`)
  `SetIdealActivity(0x1082 / 0x1083 / 0x1084)`.
- `0x102a634a` `0xb2` FACE_INTEREST: no `m_pInterestingPlace (+0x62ec)` → fail(0x3848, 0x22);
  `0x102ae310` (weapon holster policy); `m_bMatchOrientation (+0x570)` clear → complete; set →
  stop-turn, yaw = `AngleMod(place GetAbsAngles().y)`, facing tail.
- `0x102a63bd` `0xb3`: hint `0x1029f730(&patrol)`, place `0x1029f780(&patrol)`; no place →
  complete; match-orientation clear → `+0x6300 = +0x659c = 0`, complete; set → stop-turn, yaw =
  `0x102d12e0(hint)`, facing tail.
- `0x102a644e` `0xb4`: `+0x6308 = -1` first; no place → fail(0x3874, 0x22); else
  `0x102a9f40(place, 0, 1)`.
- `0x102a64a6` `0xb5`: no patrol place → complete; else `0x102a9f40(place, hint, 0)` — the third
  argument (the marker byte) is 0 (`0x102a64c2 PUSH 0`); `0xb4` passes 1.
- `0x102a64de` / `0x102a64e4` `0xb6`/`0xb7`: `TranslateActivity(1 / 9)` into `0x102ee250`, running.
- `0x102a650b` `0xb8`: `+0x6308 == -1` → clear `INTERESTING_INTO` (`0x20000000`), complete; else
  restart it.
- `0x102a5046` `0xe4` ADD_GESTURE: `SelectWeightedSequence((int)op) < 0` → break tail; else
  `0x10099250(act, 2.45, 0)`, complete.
- `0x102a5087` `0xe5` DO_DAMAGE: `n = (int)op`, negative → `m_iLastStoredDmg (+0xfdc)`; `CVDmg_t`
  `SetSrc(this)`, `Set(1, 0x80, n)`, `+0xc = 1`; `0x101c26d0(this, this, 1.0, 0, 0, dmg, -1)`; slot
  142 on itself; complete.
- `0x102a5125` / `0x102a516c` `0xe6`/`0xe7`: `act == 0x1097` → `m_iCowerAnimOffset (+0x6414) =
  RandomInt(0,2)*3`; Set / Restart `act + offset`; running.
- `0x102a778e` `0xe8` SET_DYING: `m_lifeState = 1`; complete.
- `0x102a51b3` `0xec`: `act = RandomInt(0,1)*3 + 0x106a`, `SelectWeightedSequence` discarded,
  `SetIdealActivity(act)`.
- `0x102a51e8` `0xed`/`0xee`: `m_Activity + 1 == 0` → `m_Activity = 0`; else
  `SetIdealActivity(m_Activity + 1)`.
- `0x102a521d` / `0x102a5283` `0xef`/`0xf0`: on `m_iLastDisciplineHitBy (+0xfd4)`: 6 → `0x108e` /
  `0x1091`, 12 → `0x108f` / `0x1092`, 5 and anything else → `0x108d` / `0x1090`.
- `0x102a52e9` `0xf1`: `m_knockbackType = (int)op`; complete.
- `0x102a530d` `0xf2`: slot 387 `Weapon_Drop_All`; complete. `0x102a532d` `0xf3`: slot 304; complete.
- `0x102a534d` `0xf4` CLEAR_HATRED: `0x102b52a0(this, 1, 1)`; complete.
- `0x102a536e` `0xf5`: `DisconnectFromSquad` (`0x1026d050`), `+0x14bc |= 0x80800000` (raw, bit 31
  included); complete.
- `0x102a5397` `0xf6`: `0x102ca2a0(registry, 2, this, pos, 2, op-bits, 0, this, 0)`;
  `InsertSound(8, pos, table[0x1b], 10.0, flag[0x1b], this)`; complete.
- `0x102a5426` `0xf7` FACE_NEXT_NODE: no route → `DevWarning(2, "No route to face!\n")`,
  fail(0x3599, 0xc); no waypoint → fail(0x35b9, 0xc); else the waypoint (or its successor),
  stop-turn, `0x102e2020`, `FacingIdeal()` → complete, else running.
- `0x102a5515` `0xf8`/`0xf9`: slot 474 null → fail(0x35cb, 0x12) (unreachable on the Troika line:
  slot 474 answers `&m_BestSound`); else `0x101b99d0` (owner origin for types `0x10`/`0x400` with a
  live owner, else the sound's origin), look tail.
- `0x102a555c` `0xfa`: `0x1010e530(&v, -100, 100)` (CRT `rand()` per axis) — around the WORLD
  origin — look tail.
- `0x102a5599` `0xfb`: `m_hClosestPlayer (+0x628c)` dead → fail(0x35e2, 0x17); else look.
- `0x102a55e3` `0xfc`: slot 586 live → look; else `m_hLastSeeUnknown (+0x608c)` live → look at
  `+0x6090`; else fail(0x35f7, 0x21).
- `0x102a5662` `0xfd`: `+0x65c0` dead → fail(0x3607, 0x21); else look.
- `0x102a56a2` `0xfe` / `0x102a5754` `0xff`: `m_eFaceAnim` 1..8 → `0x1100..0x1107`, else ACT_IDLE;
  `SetIdealActivity`; yaw = `GetAbsAngles().y`; `PLAYING_FACE_ANIM`; stop-turn; ideal yaw = yaw −
  `m_flFaceYawDiff` (`0xfe`) or yaw + {0, 0, 45, 90, 135, −45, −90, −135, 180}[face] (`0xff`);
  `0x102a18a0`; running.
- `0x102a585d` `0x100`: raw mask `>= 0` → `+0x14b8 |= mask`, else `+0x14bc |= mask`; complete.
- `0x102a58ae` `0x101`: the mirror (`&= ~mask`); complete.
- `0x102a5886` `0x102`: `AddMiscFlag(1 << (raw & 31))`; complete.
- `0x102a58d7` `0x103`: `TranslateActivity(0x1093)`, no sequence → `TranslateActivity(0x13)`; tail.
- `0x102a5932` `0x104`: `0x10295460(0x1115)` (untranslated) `== -1` → 9; tail.
- `0x102a594d` `0x105` / `0x102a5956` `0x126`: `TranslateActivity(0x1115 / 0x1121)`, no sequence →
  `TranslateActivity(9)`; tail.
- `0x102a59eb` `0x106`: `motor+0x28 = (op != 0)`; complete.
- `0x102a5a45` `0x107` ATTEMPT_DIVE: `d = navGoal − GetAbsOrigin()`, 2-D `len2`: `< 4096` →
  complete; `> 12100` → complete (`TEST AH,0x41; JP` at `0x102a5ac4` is taken when neither C0 nor
  C3 is set); in the band `dot2D(norm(d.xy), m_vecRight)`: `> 0.98` →
  `ANIM_MOVEMENT` + `SetIdealActivity(0x110b)`; `< −0.98` → `0x110a`; else complete.
- `0x102a5b32` `0x108`: from = origin + (0, 0, StepHeight*0.5); right/left = from ± `m_vecRight*60`;
  `0x110b` with a sequence and a clear move probe (kind 0, mask `0x202400b`, 100.0) →
  `ANIM_MOVEMENT` + `0x110b`; else `0x110a` likewise; else fail(0x3736, 0xe).
- `0x102a5cd4` `0x109`: forward `m_vecForward*96`, activity `0xf1d`; else fail(0x3759, 0xe).
- `0x102a5dda` `0x10a` / `0x102a5e1c` `0x10c`: the cover-anim restart (`0x102a1560` / `0x102a15c0`)
  true → running; false → complete. `0x102a5dff` `0x10b`: `0x102a1590`, complete.
- `0x102a5e41` `0x10d` PLAY_COVER_AIM: `0x102a15f0`; point = `m_hShootTargetOverride` origin if
  live; else if `m_hHintCoverObject (+0x6448) == GetEnemy()` (null == null included) → the enemy's
  LKP (`0x102dfed0`); else complete and fall through with the zero point; `FInAimCone(point)` →
  complete; else stop-turn + `0x102e2020(point)`, running.
- `0x102a5f16` `0x10e`: no `m_pHintNode` → fail(0x37aa, 4); else `0x102b6120(&pos, 0)`, slot 62
  `SetOrigin(pos)`, complete.
- `0x102a5f86` `0x10f`: no hint → fail(0x37bd, 4); `RestartIdealActivity(0xc84)`; hint output
  `0x102d0910`; `ClearHintNode(60.0)`; running.
- `0x102a5fcc` `0x110`: no hint → fail(0x37f4, 4); restart `0xc84`; a hint target name → find it,
  store in `m_hKickPhysicsProp (+0x643c)`, `DevMsg` (no arguments pushed) if not found; a live prop
  within 128 units → `0x102b6890(prop)`, else `DevMsg`; clear the handle; hint output; clear hint 60.
- `0x102a6128` `0x111`: dead prop → fail(0x3812, 0x25); no enemy → fail(0x380d, 6); goal
  `0x102a9c80(10, -1, -1.0, 0, …)` whose type word is then OVERWRITTEN to 4 (`0x102a61b4`), dest =
  prop − norm(enemy − prop) * 64 (`0x102a61f1` → `0x1001395d` = `0x10146190`, FSUB: the far side
  of the prop), target 0; `SetGoal(goal, 0)`; running.
- `0x102a6289` `0x112`: dead prop → fail(0x381f, 0x25); else complete.
- `0x102a62db` `0x113`: dead prop → fail(0x3832, 0x25); restart `0xc84`; kick; clear; running.
- `0x102a654b` `0x114`: slot 168 null or slot 530 unreachable → fail(0x38c3, 0xc); goal
  `0x102a9d20(lkp, -1, -1.0, 0)`, t = nav arrival (`0x102f2fc0`); slot 563(t, &dest, &goal.tol, &t);
  `SetGoal(goal, 2)` false → `DevWarning("GetPathToLastEnemyLKP failed!!\n")`,
  `RememberUnreachable`, fail(0x38d8, 0xc); true → `0x102f2fe0(nav, t)`, complete.
- `0x102a6694` `0x115`: t = slot 168 or this; `FindLateralCover(t eye, t)` (`0x102784a0`) → move
  wait + complete; else `0x102edc80(origin, eye, 0.0, CoverRadius, &out, t)` false →
  fail(0x3907, 8); goal `0x102a9dc0(6, out, 0x13, -2.0)`, `SetGoal(goal, 0)`; with a hint: slot 569
  → `0x102ee410`, `0x102d11f0` → `0x102ee530`; move wait; running.
- `0x102a6801..0x102a6861` `0x116..0x118`: `0x102a9770(0x40000)` answers 1 when `BOTCHED_ATTACK` is
  CLEAR, and that arm completes. Set: `0x116` restarts `0x55`; `0x117` `0x102a1620`; `0x118`
  `0x102a1560` true → running, else `0x102a1620`.
- `0x102a68a8` `0x119`: `+0x6334 = 1`, `+0x6330 = op`; complete. `0x102a68bd` `0x11a`: `+0x6330 +=
  RandomFloat(0, op)`; complete.
- `0x102a68df` `0x11b`/`0x11c`: act `0x10`/`0x14`; no sequence → turn act `9`/`0x13`,
  `0x102a1650(act, 180, delta)` false → fail(0x396f, 0x15); true → `+0x63ec = 180`,
  `m_flWaitFinished = curtime + delta`, restart. With a sequence: hull trace from origin +
  half step height to `− m_vecForward*50*delta` (`0x102a69ff`, the same subtract helper; mask
  `0x202400b`, collision mins/maxs); fraction
  `!= 1.0` → fail(0x3997, 0xe); else wait + restart.
- `0x102a6fd8` `0x11d`: stop-turn; yaw = `AngleMod(m_qaLastFacing.y)`; facing tail.
- `0x102a7000` `0x11e`: `0x101f5950(&DAT_1073dc28, this, (int)op, 2, 1.0, 1.25)`; complete.
- `0x102a7025` `0x11f`: from `m_vSavePosition`, to = from + `m_vecViewOffset`;
  `0x102edaa0(from, to, 0, 4096, 1.0, 1, &out)` false → fail(0x3ac1, 0xb); goal
  `0x102a9d20(out, 0x13, -2.0)`, `SetGoal(0)`; running.
- `0x102a70e6` `0x121`: op != 0 → `+0x14bc |= 0x80000001`, `+0x65d0 = GetAttackExtents()`,
  `SetAttackExtents(60, 60, 80)`; op == 0 → `SetAttackExtents(+0x65d0)`, `+0x65d0 = (-1,-1,-1)`,
  `+0x14bc &= ~0x80000001`; complete.
- `0x102a6ab7` `0x122`: no `0x1121` sequence → fail(0x39a7, 0x15) AND tries = 1000 (the arm goes
  on); side = `0x1025df40(coordinator, this, enemy)`, 0 → `RandomInt(0,1) ? -1 : 1`; while tries <
  2: yaw = `RandomFloat(side*80, side*120)`, `0x102a1650(0x1121, yaw, delta)` true → `+0x63ec = yaw`,
  wait, restart `0x1121`; false → side = −side. tries == 1000 → running; else fail(0x39d5, 0xe).
- `0x102a6bf7` `0x123`/`0x124`: radius = slot 418(op); t = slot 168: dist computed, or
  fail(0x39ec, 6) + tries = 1000; `debug_circle_dist_override > 0` replaces radius; radius == −1
  with a weapon → `w[0x8c0]*0.75 + w[0x8b8]*0.25`; act `0x1121`, else 9, else fail(0x3a06, 0x15)
  (returns); up to two slot-603 yaws through `0x102a1650`; tries == 1000 → running; else
  fail(0x3a30, 0xe).
- `0x102a597f` `0x127`: `+0x6320 = debug_melee_advance_combatmove_dist + RandomFloat(0,
  slot418(op))`; then the tolerance tail: `0x102ee1c0` (path `+0x28`) and `0x102f2fe0` (path
  `+0x20`) with the same value (`0x102a59d1`, `0x102a42df..0x102a42e8`); complete.
- `0x102a7185` `0x128`: `m_flWaitFinished = delta + curtime`; `RandomInt(0,1)` 0 → ACT 1, 1 → 3;
  `RestartIdealActivity(TranslateActivity(act))`.
- `0x102a71e4..0x102a721c` `0x129..0x12d`: `m_flSpecialDistanceAccum (+0x5bac)` set / += / += rand /
  −= / −= rand of slot 418(op); complete.
- `0x102a72a7` `0x12e`: stop-turn, `0x102e2020(m_vSavePosition)`, slot 572; running.
- `0x102a72e3` `0x131`: op != 0 → `+0x14bc |= 0x80001000`, `0x1026d130` (`SetEnemy(NULL)`,
  `DisconnectFromSquad`, `++m_iIsOblivious`), fire `+0x5fd4`; op == 0 → `0x1026d160`
  (`--m_iIsOblivious` floored, `ReconnectToSquad`), `&= ~0x80001000`, fire `+0x5fec`; complete.
- `0x102a7358` `0x137`: `0x10295460(+0x65e4, 1) == 0` → `TaskFail(0x15)` with no line write; else
  restart `+0x65e4`. (A -1 "no sequence" answer is NOT refused — retail defect.)
- `0x102a73a0` `0x138`: squad and enemy → `SquadNewEnemy`; complete.
- `0x102a73da` / `0x102a7468` / `0x102a748c` `0x139`/`0x13b`/`0x13c`: stop-turn, restart
  `0x2b`/`0x30`/`0x32`.
- `0x102a73fe` `0x13a`: `m_flGravity = m_fJumpGravity`; `0x102c4c50` solves the arc; motor `+0x18`
  applies it; nav type 1; `m_bJumping = 1`; running.
- `0x102a74b0` `0x13d`: `m_bInvincible (+0x63d8) = (op != 0)`; running (no complete).
- `0x102a74ed..0x102a7586` `0x13e..0x144`: the activity copy-prop calls; all complete except
  `0x142`, which keeps running when `0x1018eb50` answers 0.
- `0x102a7598` `0x145`: `BurnModel(GetSkeletonModelName(), false)`; complete.
- `0x102a75b2` `0x146`: `+0x5db8 = +0x62a8`, `+0x5dc4 = +0x62b4`; complete.
- `0x102a75db` `0x147`: `m_hLastDamageEnt (+0x5b7c)` dead → fail(0x3be0, 0x21); `0x102edc80(origin,
  eye, 32.0, CoverRadius, &out, this)` false → fail(0x3bdb, 8); goal `0x102a9d20(out, 0x13, -2.0)`,
  `SetGoal(0)` (answer unread), move wait; running.
- `0x102a7722` `0x148`: dead → fail(0x3bee, 0x21); else look at it.
- `0x102a779d` `0x149`: `(int)op`, no sequence → `0x1d`, none → 1; `SetIdealActivity`; ACT_IDLE →
  complete, else running.

#### Reads and writes

Reads: the task's two words; `+0xfd4`, `+0xfdc`, `+0xfec`, `+0x14b8`, `+0x5b7c`, `+0x5ba8`,
`+0x5d8c`, `+0x5dc4`, `+0x5dd0`, `+0x5ddc`, `+0x6004`, `+0x6068`, `+0x608c`, `+0x6090`, `+0x60b0`,
`+0x628c`, `+0x6290`, `+0x629c`, `+0x62a8`, `+0x62b4`, `+0x62ec`, `+0x6300`, `+0x6308`, `+0x6330`,
`+0x63e4`, `+0x63e8`, `+0x643c`, `+0x6448`, `+0x649c..+0x64b8`, `+0x658c`, `+0x65c0`, `+0x65d0`,
`+0x65e4`, `+0x65e8`, motor `+0x1c`/`+0x28`, navigator path `+0x20`/`+0x24`.
Writes: `+0x200`, `+0x3d4`, `+0x3ec`, `+0xfec`, `+0x14b8`, `+0x14bc`, `+0x1b44/+0x1b48` (debug),
`+0x5bac`, `+0x5bb4`, `+0x5cf0`, `+0x5db4`, `+0x5db8`, `+0x5dc4`, `+0x6014`, `+0x6018`, `+0x601c`,
`+0x6068`, `+0x606c`, `+0x6300`, `+0x6308`, `+0x6320`, `+0x6330`, `+0x6334`, `+0x6414`, `+0x63d8`,
`+0x63ec`, `+0x643c`, `+0x6498`, `+0x659c`, `+0x65d0`, motor `+0x28`/`+0x2c`/`+0x34`, navigator
path `+0x20`/`+0x28`/`+0x2c`.

**Unrecovered:** the category name of sound-table row `0x1b` (`DAT_1072bc20`, `0x102a9ea0` /
`0x102a9ec0`); the `+0xb0` word `0x1029f780` caches from a place; `0x102b6890`'s kick impulse;
`0x102c4c50`'s arc solve; the `activity_copy_prop` bodies `0x1018e790..0x1018ecf0`; the default
string of `cvar_debug_circle_dist_override` (`0x105399a0`); `0x10345480`'s studio-header read; the
eluded-record fallback inside `0x102dfed0`; `0x102ae310` (weapon holster policy) in full.

### The species `StartTask` overrides (slot 442) — `0x103c1820` … `0x10375f50`

_Recovered 2026-09-28, 0019 story 8 pass I (lane L04)._

This walks every species `StartTask` override the retail
`vampire.dll` ships, plus `CNPC_VCop::StartTask` (family Damaged19). It also covers the
verification of `CNPC_VFrenzyShadow::StartTask`.

- **Port:** `Source/ElysiumUE/Private/Substrate/ElysiumNpcStartTaskSpecies.cpp`. FrenzyShadow is
  in `ElysiumNpcFrenzyShadow.cpp`.
- **Tests:** `Tests/ElysiumNpcKernelStartTaskTests_4.cpp`, suite
  `Elysium.Substrate.NpcKernelStartTask19.Species.*`.
- **Packet:** `$ELYSIUM_WORK_ROOT/research/npc-kernel-checklist/families-19-29/StartTask19-READING.md`.
  VCop is in `Damaged19-READING.md`.

Every body below has instruction addresses in the port's trailing comments. This page states the
behaviour; the addresses stay in the code.

#### Shared facts

**Task number.**
- Retail's `Task_t::iTask` is the class-LOCAL task id, and every species switch compares local
  numbers.
- The port stores the GLOBAL id. Each body therefore translates the id back through slot 450
  `GetLocalTaskId` (`0x101a6640`) before it switches.
- A task a body builds on its own stack is translated forward before it is handed on. The head
  claw's `{0x4b, 1.0}` is the one case.

**Parent chains.** Every chain in these bodies is a DIRECT thunk, so the port makes an explicit
base call:

| Thunk | Target |
|---|---|
| `0x10010695` | `CAI_BaseNPCTroika::StartTask` `0x102a1910` |
| `0x10013336`, `0x1000a0c9` | `CNPC_VHuman::StartTask` `0x103847f0` |
| `0x10009b06` | `CNPC_VAnimal::StartTask` `0x1035f650` |
| `0x1000de7c` | `CNPC_VVampireBoss::StartTask` `0x103c5ac0` |

**Failure trace.**
- A traced failure writes `+0x1b44` (the source file) and `+0x1b48` (the line), then calls slot 448
  `TaskFail`. The port records `StartTask fail trace <file>:<line>` in the mind trace.
- A bare failure calls slot 448 alone.

**Motor facing idiom.**
- `0x102e0b40(m_pMotor)` resets the motor yaw-speed override (`motor+0x2c = -1`). At integration
  the species sites, like the base and Troika ones, call the one base helper
  `StartTaskMotorHoldYaw`, which is family RunTask19's `MotorMoveStop`.
- `0x102e2020(m_pMotor, target)` sets the ideal yaw at a point. `0x102e20b0` does the same with a
  speed. Both land in `SetMotorHintYaw`.

**`RestartIdealActivity` (`0x10289ee0`).**
- Retail: when `m_Activity == act`, it sets `m_Activity = 0`, then calls `SetIdealActivity(act)`.
- Ported at integration as `RestartIdealActivityId` (`ElysiumNpcBaseHints.cpp`); it had been a
  no-op seam of family Hints, so the arms that test `m_IdealActivity` afterwards now read retail's
  word.

**`SetGoal` (`0x102ecd20`).**
- Arms build an `AI_NavGoal_t` on the stack and submit it to `SetGoal 0x102ecd20`, the family's one
  port body (§ "`CAI_Navigator::SetGoal` `0x102ecd20` (one body)" above).
- A tolerance of -1.0 keeps the PATH's tolerance (`path+0x28`, not `m_flGoalTolerance +0x6320`), or
  the pathing hull's width when that is 0.0.

#### `CNPC_VTzimisceHeadClaw::StartTask` `0x103c1820`

- **Tasks 0x122, 0x123, 0x124:** the task is REPLACED. A stack `Task_t {0x4b, 1.0}` goes to the
  Troika body, and the original id is never passed on.
- **Tasks 0x36, 0x37:** slot 618 plays the exertion: `EmitSound` on channel 4, volume 1.0,
  attenuation 0.8, pitch 100, one of three `TC_FatGuy/Exert_Heavy_N.wav` picked by
  `RandomInt(0, 2)`. The task then falls into the Troika body.
- **Everything else:** the Troika body.

#### `CNPC_VTzimisceRunner::StartTask` `0x103c35d0`

- **Tasks 0x122–0x124:** `RestartIdealActivity(1)`, then
  `m_flWaitFinished = m_flWaitFinishedDelta + curtime`. The body returns WITHOUT calling the base.
- **Task 0x130:**
  - A live `m_hPotentialEnemy` (+0x6678): its slot 220 origin goes into `m_vSavePosition`, then
    `TaskComplete`, and the base STILL runs.
  - A dead handle goes straight to the base.
- **Tasks 0x36, 0x37:** the `TC_Runner` exertion, then the base.

#### `CNPC_VTaxiDriver::StartTask` `0x103b36d0`

- **Tasks completed at once, without the base:** 0x2b, 0x2e, 0x2f, 0x31, 0xb2, 0xba–0xbd, 0xf8,
  0xf9, 0xfb–0xfd, 0x11b and 0x11d.
- **Task 0xb9:** runs the dialogue upkeep `0x102c1400`.
  - An answer of -1 (not in dialogue) completes the task.
  - Any other answer calls slot 310 `SetActivity(0x114e)` and leaves the task running.

#### `CNPC_VSabbatGunman::StartTask` `0x103a5650`

- **Tasks 0x11b, 0x11c, 0x122–0x124:**
  `m_flWaitFinishedDelta *= 1.0 / sabbat_gunman_speed_scalar`, with the ConVar read as
  `IsCommand ? 0.0 : m_fValue`. The IsCommand arm divides by zero; that is the retail defect.
  Then the VHuman body.

#### `CNPC_VCop::StartTask` `0x10371b70`

- **Task 0x107:** `TaskComplete` (tail jump).
- **Task 0x14a:** `m_hClosestPlayer` (null when stale) is handed to slot 598 by tail jump. No
  completion.
- **Everything else:** VHuman.

#### `CNPC_VDog::StartTask` `0x10374940`

- **Task 0x36:**
  1. `AutoMovement`.
  2. The motor facing at the enemy's last-known position, at speed -2.0.
  3. `RestartIdealActivity(0x4b)`.
  4. `TaskComplete` only if slot 251 `IsActivityFinished` answers true.
- **Task 0xbc:** `SetActivity(3)`, then the Animal body.
- **Everything else:** Animal.

#### `CNPC_VGhoulCroucher::StartTask` `0x1037b8b0`

- **Task 0x14a:** `RestartIdealActivity(table A)` (`0x1037b870`). If `m_IdealActivity` did not
  take, `TaskFail(0x15)`.
- **Task 0x14b:** the same over table B (`0x1037b890`). A refusal also sets `m_bUnawareExited`
  (+0x6667).
- Neither task completes.
- **Everything else:** VHuman.

#### `CNPC_VHuman::StartTask` `0x103847f0`

- **Tasks 0x89, 0x8a:** `m_flLastAttackTime = curtime`, then `RestartIdealActivity(0x4b)`.
- **Task 0x8b** (a downed enemy):
  - With an enemy that has a combat character, face its last-known position.
  - Play the enemy sequence's paired activity (descriptor `+0x2d8`) when it is non-negative, else
    0x51.
- **Tasks 0x8d, 0x8e, 0x8f, 0x90, 0x91:** activities 0x1154, 0x52, 0x1156, 0x1152 and 0x1153.
- **Task 0x9f:**
  - No weapon: line 0x138, `TaskFail(3)`.
  - Otherwise the tolerance is `weapon+0x8c0 * data + 2 * NAI_Hull::Width(m_eHull)`, then
    `TaskComplete`.
- **Everything else:** Troika.

#### `CNPC_VAnimal::StartTask` `0x1035f650`

- **Tasks 0x89, 0x8a, 0x8b, 0x8e, 0x9f:** as VHuman, with activities 0x4b / 0x4b / 0x50 / 0x52.
  The 0x9f failure is at line 0x165.
- **Task 0xa5:**
  - No `m_pInterestingPlace`: line 0x19e, `TaskFail(0x22)`.
  - Otherwise a goal of type 9 at `m_vecInterestingPlace`, with activity words -1 and tolerance
    -1.0. Accepted: complete. Refused: line 0x199, `TaskFail(0xc)`.
- **Everything else:** Troika.

#### `CNPC_VAndreiBlood::StartTask` `0x1035d1b0`

- **Task 0x14b** (unhide at the hint):
  - No hint: line 0x130, `TaskFail(4)`.
  - Otherwise, in order: `SetAbsOrigin(hint)`, slot 181 `Teleport`, slot 62, `SetHullSizeNormal(1)`,
    `m_bTriggerUnhide = 1`, `m_fEffects &= ~0x20`, `RemoveSolidFlags(4)`, `ForceTransmit`, `Relink`,
    `m_fEffects |= 0x10`, complete.
- **Task 0x150:** nothing.
- **Tasks 0x151, 0x152:** the teleport-out / teleport-in wav on channel 2 (0.8), the matching blood
  emitter, then activity 0x113c / 0x113b. No completion.
- **Task 0x153:** `m_pHintNode = SelectTeleportNode()`, complete.
- **Task 0x154:** the summon wav, activity 0x113a, the summon emitter. Then the nearest
  `npc_maker_fleshpile` within 1024 of the origin makes a runner. No completion.
- **Task 0x155:** `m_OnDeath` fires with the closest player as activator, then `UTIL_Remove(this)`.
- **Task 0x156:** `+0x66d0 = curtime`.
- **Everything else:** VampireBoss.

#### `CNPC_VAsianVampire::StartTask` `0x103611a0`

- **Task 0x150:** consumed.
- **Tasks 0x151, 0x152:** `m_pHintNode` from `SelectLedgeNode` / `SelectJumpbaseNode`.
  - A null result stamps line 0x110 / 0x118 and calls `TaskFail(1)`.
  - Both paths then reach the ONE shared `TaskComplete(0)`. After a failure `COND_TASK_FAILED`
    refuses it.
- **Everything else:** VampireBoss.

#### `CNPC_VBach::StartTask` `0x103645a0`

The dispatch is three nested compares: above 0x14c, equal to 0x14c, and the low range.

- **Tasks 0x34, 0x35:**
  - Holding a non-rifle: the VHuman body runs, then `+0x66a4` is cleared.
  - Holding the rifle (`item_w_rem_m_700_bach`, compared with `__strcmpi`) or nothing:
    - `+0x66a4` clear: complete. Bach does not turn while aimed.
    - `+0x66a4` set: the VHuman body, then clear.
- **Tasks 0xb0, 0xb1** (the sniper wait):
  - No weapon: complete.
  - A non-rifle: `+0x66a4 = 1`, the VHuman body, then `m_flWaitFinished -= 0.35`.
  - The rifle, with the camper flag `+0x66a0` set:
    - Occluded (`+0x6674` non-zero): wait = curtime + 1e9, and the warning time equals it.
    - Not occluded: wait = curtime + 0.15, clear the flag and `+0x6678`, warning = wait + 1e9.
  - The rifle, with the camper flag clear:
    1. Without skip-to-warning: wait = curtime + 0.2 + (enemy feat 0xc, 1 with no enemy) * 0.08.
    2. With it: wait = curtime + `+0x6698`, then clear both skip words.
    3. Both paths then add max(enemy feat 10, feat 9) * 0.1.
    4. warning = wait, then wait += 1.5.
  - With no hint, `RestartIdealActivity(5)`.
- **Tasks 0xba–0xbd:** complete.
- **Task 0x14a:** the rifle via `Inventory_Find` and slot 388, `+0x66a4 = 0`,
  `+0x668c = curtime + 0.5`, complete.
- **Task 0x14b:** the katana, `+0x668c = curtime + 0.5`, complete.
- **Task 0x14c:** the holy-light equip `0x103656a0`, its wav, `+0x66a1 = 0`, complete.
- **Task 0x14d:**
  - With a weapon: type-3 stat 0xf base 1, then weapon `+0x518` and `+0x4f0(0)`.
  - Always completes.
- **Task 0x14e:** the teleport-ring hint for `m_iBachTeleportState` 0..3 (types 0x4268..0x426b),
  searched with flags 2. Any other state: line 0x350, `TaskFail(4)`.
- **Task 0x14f** (the teleport):
  - No hint: line 0x355, fail 4.
  - Otherwise, in order:
    1. Capability 1 off, `+0x66a7 = 0`.
    2. `SetAbsOrigin`, `Teleport`, slot 62.
    3. The state advances, wrapping above 3.
    4. Stat 0xd = 5.
    5. `+0x6690 += 6`, the shield up, `+0x6680 = curtime + 3`.
    6. The shield wav, complete.
- **Task 0x150:**

  | State | Effect |
  |---|---|
  | 1 | capability 1, hint 0x426c, movement spot |
  | 0 | capability 1, hint 0x426d |
  | 3 | capability 1, movement spot, then as state 2 |
  | 2 | skip-to-warning at `0x1062d210[state]` = {1.3, 1.0, 1.2, 1.2}, line 0x387, fail 4 |

- **Task 0x151:** without a movement spot, capability 1 off. Then the rifle, `+0x66a4 = 0`, skip
  at the state's time, complete.

#### `CNPC_VChangBros::StartTask` `0x1036b750`

Also the body of the Blade and Claw classes.

- **Task 0x13b:** in sector 3, `+0x66cc = curtime`. Then the base.
- **Task 0x150:** motor reset, `AddSolidFlags(4)`, activity 0x1148, then FALLS THROUGH into 0x151.
- **Task 0x151:** motor reset, the teleport-in emitter, `RecordHealthPercent`,
  `+0x66d0 = curtime`.
- **Task 0x152:** teleport node, complete.
- **Task 0x153:** ground nav, `m_bJumping = 0`, `flags2 &= 0x7ffffffd`, `CommitSetupJump`, the
  teleport-out emitter, activity 0x1149.
- **Task 0x154:** ledge node. A null result stamps line 0x1aa and fails 1. Both paths complete; a
  failure refuses the completion.
- **Task 0x155:** `SetupSuperJump(hint pointer)` (the pointer is tested as a float), complete.
- **Task 0x156:**
  1. Face the closest player.
  2. Activity 0x114c.
  3. `+0x66f0 = curtime + 1.5`.
  4. The charge emitters.
- **Task 0x157:** `+0x66d8 = 0`, activity 0x114a, `m_flLastAttackTime`.
- **Task 0x158:** `CheckJumpPathToHintNode`: complete, or line 0x1ce and fail 1.
- **Task 0x159:** the united node, complete.
- **Task 0x15a:** nothing.
- **Task 0x15b:** `AddSolidFlags(4)`, activity 0x114b, face the stored centre,
  `+0x66d4 = curtime + 4`.
- **Task 0x15c:** activity 0x114c.
- **Task 0x15d:**
  1. `+0x66ec = curtime`, activity 0x114d, `RemoveSolidFlags(4)`.
  2. Only when `m_ChangType == 0`: the blast emitter 50 above the arena centre.
  3. Then, unless the closest player is in sector 4 or the template is negative:
     `CausePlayerAOEDamage(point, 1000, template+0xcc)`.
- **Everything else:** VampireBoss.

#### `CNPC_VGargoyle::StartTask` `0x103790d0`

- **Task 0x12f:** activity 0x5e.
- **Task 0x130:** a live pillar's origin into `m_vSavePosition`, complete. With no pillar, nothing
  happens and the task keeps running.
- **Tasks 0xe9, 0xea:**
  1. `m_iDoingGibDeath = 1`.
  2. With a physics object: the impulse `velocity + forward * 400 + (0, 0, 100)` and
     `m_OnGibDeath`.
  3. The VHuman body.
- **Task 0x31:** with a pillar, face it and call `SetTurnActivity`. Complete only if
  `FacingIdeal`.
- **Tasks 0x36, 0x37:** the gargoyle exertion, then VHuman.

#### `CNPC_VHengeyokai::StartTask` `0x103805d0`

- **Task 0x135:** drop the carried body (`0x103828a0`), complete.
- **Task 0x136:** the thaw (`0x10383130`), complete.
- **Task 0x14a:** the shark form (`0x103831c0`), complete.
- **Task 0x14b:** nothing.
- **Task 0x14c:** the hengeyokai model, `m_fEffects |= 0x10`, render words 0, hull 0x12,
  `SetHullSizeNormal`, complete.
- **Task 0x14d:** `SUB_Remove` think at curtime + 0.01, complete.
- **Task 0x14e:** `m_bfAINPCFlags2 |= 0x80000800`, complete. The packet row claimed `+0x19c`; the
  word is `+0x14bc`.
- **Task 0x134:** activity 0x127.
- **Tasks 0x36, 0x37:** the exertion, then VHuman.
- **Task 0xc8:** face the pickup target. Complete only once `FacingIdeal`.
- **Task 0xc9:**
  - `COND 0x1b` and `0x103822a0` both answer: complete.
  - Otherwise: face, `SetTurnActivity`, `+0x6674 = curtime + 1`.
- **Task 0xca:** the same over slot 168, with no condition gate and no timer.
- **Task 0x130:**
  1. `+0x6680 = 2`.
  2. A live target: its origin into `m_vSavePosition`, the tolerance pushed, complete.
  3. A dead target: line 0x3b3, fail 1.
- **Tasks 0x132, 0x133:** the carry facing `0x10382bb0`, activity 0x126.

#### `CNPC_VManBat::StartTask` `0x1038c390`

**Mode word.** The mode is a secure word:
- `+0x6670` holds `0x103908c0(0x1042fb50(mode))`.
- The port also keeps the decoded mode (`ManBatMoveGoalNodeMode`).

**Fly-node searches.** Each is `0x102d1af0(this, 20000, type, 5000.0, 0, 0)`, and the result goes
into `m_pFlyNode` (+0x6688).

**Tasks:**
- **0x14a:** ideal 0x28, plus `0x1038c250(node)` when there is a fly node.
- **0x14b:** the first flap (ideal 0x22, flap timer 2.3), then the steer `0x1038b370` and the flap
  selector `0x1038e720`.
- **0x14c:** in order:
  1. Release the carried object.
  2. Ground switch 2.
  3. Mode 5.
  4. Ideal 0x2f.
  5. A ballistic velocity to the fly node.
- **0x14d:** yaw `RandomInt(-180, 180)`, quantised to the 16-bit angle; speed 500; climb
  `RandomFloat(0.1, 0.5) * 500`. Then `SetAbsVelocity` and the flap selector.
- **0x14e:** mode 1, search type 2. Found: complete. Not found: fail 4.
- **0x14f:**
  - With no node: mode 0, search, else node id 1 and search again. Still none: fail 4.
  - Otherwise: node id + 1, complete.
- **0x150:** ideal 0x30, velocity zero.
- **0x151:** mode 2, node id `RandomInt(1, 3)`, search. Complete, or fail 4.
- **0x152:** mode 3, search. Steer and flap, or fail 4.
- **0x153:** the throw row `RandomInt(1, 4)` of four overlapping tables, via `0x1038f2c0`.
  Complete, or fail 1.
- **0x154:** a node id `RandomInt(1, 3)` different from the current one. With a node, set the
  origin to it, clear the node, complete. Otherwise fail 4.
- **0x155:** mode 4, search, then ideal 0x116f, or fail 4.
- **0x156:** face the closest player (pitch 0, roll kept), ideal 0xb0.
- **0x157:** mode 6, clear the node.
- **0x158:** ideal 0x1170, the screech cone.
- **0x159 / 0x162:** mode 7, fly-by target = closest player.
- **0x15a / 0x161:** ideal 0x4b.
- **0x15b:** ideal 0x1054.
- **0x15c:** mode 8, node id 1, search, then steer or fail 4, then FALL THROUGH into 0x15d.
- **0x15d:** origin z + 10, the first flap, complete.
- **0x15e / 0x15f:** `0x1038fc80` / `0x1038fd40`, complete.
- **0x160:** the nearest entity named "Cop" becomes the fly-by target. Live: mode 7. Otherwise
  fail 1.
- **0x163:** mode 9, coast timer curtime + 1.
- **0x164:** clear the fly-by sound flag, complete.
- **Everything else:** VHuman.

#### `CNPC_VMingXiao::StartTask` `0x10392d80`

- **Tasks 0x89, 0x8a, 0x8b, 0x8e, 0x9f:** as VHuman. The 0x9f failure is at line 0x296.
- **Task 0x14a:** the transform `0x1039a750`, complete.
- **Task 0x14b:** nothing.
- **Task 0x14c:** `MingXiao.mdl`, 0x10, hull 0xf, `+0x6678 = 1`, complete.
- **Task 0x14d:** `SUB_Remove`, complete.
- **Task 0x14e:** `BeginDefeatSequenceOnce` (Misc19), complete.
- **Tasks 0x14f–0x152:** the throw attack for tentacles 0..3, with activities 0x112a..0x112d.
- **Tasks 0x153, 0x154:** the motor clamp around the current yaw (range 20), activity 0x1131 /
  0x1130.
- **Task 0x155:**
  1. Switch to `m_hRangedWeapon`.
  2. Activity 0x19.
  3. `+0x66c0 = 0x10397f70() + curtime`.
- **Task 0x156:** the throwable-object mode from the data word (0..4, anything else 0), complete.
- **Task 0x157:** a goal of type 4 at `+0x6720`, running. The answer is the return value.
- **Task 0x158:** activity 0x112f for tentacle 4, else 0x112e.
- **Task 0x159:** modes 3/4 run the throw clean-up; always completes.
- **Task 0x15a:** clamp, then 0x1131 / 0x1130 by tentacle.
- **Task 0x15b:** `hit_yaw = RandomFloat(-90, 90)`, activity 0x73.
- **Task 0x15c:** `hit_yaw = -VecToYaw(origin - bone) + RandomFloat(-15, 15)`, activity 0x74. The
  FCHS negation is retail's.
- **Tasks 0x15d, 0x15e, 0x15f:** the damage / death / proxy-death emitters, complete.

#### `CNPC_VMingXiaoTentacle::StartTask` `0x1039c4c0`

- **Tasks 0x8b, 0x8e:** as MingXiao.
- **Task 0x9f:**
  - No weapon: line 0x181, fail 3.
  - Otherwise the tolerance is `self hull mins.x + enemy hull mins.x + range * data`.
    `0x102d6100` answers `&row.mins`.
- **Task 0x14a:** when `(int)data` differs from `m_ePhase`:
  1. The teardown `0x1039f310`.
  2. Phase 1 is invincible, phase 2 is vulnerable, phase 3 and anything else (stored as 0) are
     invincible. Each clears `+0x6674`.
  3. Always completes.
- **Task 0x14b:** the form swap on `(int)data`:

  | Data | Model | Model index | Hull | Attack extents | Emitter |
  |---|---|---|---|---|---|
  | 2 | grub | `+0x6668` | 0x11 | 0 | none |
  | 3 | transformation | `+0x666c` | 0xf | 64/64/32 | baby-transform |
  | other | grub | `+0x6664` | 0x11 | 0 | tentacle-transform |

  Then `SetHullSizeNormal`, complete.
- **Task 0x14c:** `SetForceFrequentThink(1)`.
- **Task 0x14d:** complete.
- **Task 0x14e:** `0x1039ef10`, complete.
- **Task 0x14f** (the evade):
  1. No enemy: line 0x200, fail 6.
  2. The navigator node search around the enemy (512, 30000). None: line 0x20b, fail 7.
  3. Jitter bands on the dominant axis: 0..60 away from the enemy, and ±80 across it.
  4. Up to 5 candidates. Z is lifted by half the step height; Y is drawn before X; each is tested
     with `IsAreaClear` against `0x202400b`. After the fifth refusal the original node is kept.
  5. A run goal there. Refused: line 0x26b, fail 0xc.
  6. Accepted: `+0x667c = RandomFloat(1, 2) + curtime`, complete.
- **Task 0x150:** nothing.
- **Task 0x151** (hide behind the companion):
  1. No companion or enemy: line 0x2a0, fail 1.
  2. Draw `RandomFloat(-20, 20)` three times: z, then y, then x.
  3. The point is `companion + normalize(companion - enemy) * 200 + jitter`.
  4. Not clear: line 0x29a, fail 0x1a.
  5. A run goal. Refused: line 0x294, fail 0xc.
  6. Accepted: `+0x667c = curtime + 600`, `+0x6680 = RandomFloat(5, 15) + curtime`, complete.
- **Task 0x152** (scatter):
  1. An enemy within 512 of this body: search at `(enemy * 3 + scatter centre) / 4`.
  2. Otherwise, or when that search fails: search at the scatter centre. Failure: line 0x2c6,
     fail 7.
  3. A goal. Refused: line 0x2d4, fail 0xc.
  4. Accepted: `+0x667c = curtime + 600`, complete.
- **Task 0x153:** notify the owner, complete.
- **Task 0x154:** activity 0x49.
- **Task 0x155:** the baby death emitter, complete.
- **Task 0x156:** `0x1039ea60` (Misc19), complete.

#### `CNPC_VSabbatLeader::StartTask` `0x103a78c0`

**Prologue** (every task): a spawned nova particle (`+0x66e4`) is killed unless the task is 0x161
or 0x162.

**Tasks:**
- **0x36, 0x37:** slot 621 `AttackSound`, then the base.
- **0x6e, 0x92, 0x93, 0xe5:** `CommitSetupJump`. A body still on the jump nav type is stopped
  (motor slot 8), put back on ground nav, and `m_bJumping` is cleared. Then the base.
- **0x14e:** `+0x66b8 = 1`, `m_bIsBossMonster = 1`, `m_flLastAttackTime`, then the base.
- **0x150–0x153, 0x159:** `SelectHintNode` with (0x3e80, 2), (0x3e80, 4), (0x3e81, 2), (0x3e82, 2)
  and (0x3e83, 2).
- **0x154:** the dive-in point. None: line 0x25a, fail 1. Found: complete.
- **0x155:** the teleport archway, complete.
- **0x156:** the dive-out point, complete.
- **0x157:** the ambient run loop, complete.
- **0x158:** `StopSound(2, ambient_run)`, complete.
- **0x15a:** jump origin/target to the hint, height 200, track and dive cleared, complete.
- **0x15b:** needs a live closest player outside the no-jump zone, else line 0x28d, fail 1. Then
  `SetJumpOriginAndTarget(player, 35, 25)`, the leap wav, track the player, complete.
- **0x15c** (dive in):
  1. Gravity 0.1, dive on, activity 0x113f.
  2. `EF_NODRAW`, not solid.
  3. Velocity = delta / 1.1, with Z replaced by `gravity * sv_gravity * 1.1 / 2`.
  4. Jump nav, `m_bJumping`.
- **0x15d** (dive out):
  1. Dive off, `Unhide`, 0x10.
  2. Face the player at speed 50.
  3. The splash wav, the blood pool at the hint.
  4. Activity 0x1140.
- **0x15e:** teleport onto the hint, the warning wav, `+0x66dc = curtime + 1`.
- **0x15f:** `flags2 |= 0x80000800`, complete.
- **0x160:** roar, activity 0x1141.
- **0x161:** nova on, body emitters, particle spawned, activity 0x1142.
- **0x162:** activity 0x1143.
- **0x163:** the blast emitter 80 above the origin, the AOE over 350 when the template resolves,
  activity 0x1144, `m_flLastAttackTime`.
- **Everything else:** VampireBoss.

#### `CNPC_VScurrying::StartTask` `0x103ac740`

Also the body of `CNPC_VRat`.

- **Task 0xbc:** `SetActivity(3)`.
- **Task 0x14b:** complete.
- **Task 0x14a** (flee):
  1. A scarer that does not resolve AND a spent scare stamp: line 0xe9, fail 6.
  2. Flee from the scarer at 2 * detection distance, or from the stored scare point at the fright
     distance.
  3. The destination search `0x103acba0`. None: line 0xfa, fail 7.
  4. A run goal. Refused: line 0x107, fail 0xc. Accepted: complete.
- **Everything else:** Animal.

#### `CNPC_VSheriffMan::StartTask` `0x103aec70`

- **Task 0x13b:** the land-blast emitter and the AOE over 300 at the origin, then the base.
- **Task 0x150** (teleport out):
  1. `m_bTeleporting`.
  2. The weapon hidden and made non-solid.
  3. The teleport emitter.
  4. `Hide`, `EF_NODRAW`, not solid.
- **Tasks 0x151, 0x155, 0x156, 0x157:** the teleport, centre, ledge(0) and ledge(1) nodes. None
  stamps lines 0x192, 0x1ac, 0x1b4 and 0x1bc and fails 1. Both paths reach the completion.
- **Task 0x152:** only with no hint held: the selection, or line 0x19c and fail 1. No completion.
- **Task 0x153** (teleport in, retail's order):
  1. Onto the hint.
  2. The weapon shown and made solid.
  3. `Unhide`, visible, solid.
  4. The best melee weapon, `KillTeleportBats`, `MatchOriginAnglesToAnimation("bip01", 1, 1)`.
  5. `m_bTeleporting = 0`, 0x10, `RecordHealthPercent`.
  6. The emitter, `m_flLastAttackTime`.
- **Task 0x154:** `OnFinishTransformation`.
- **Task 0x158:** a start-solid stand trace stamps line 0x1c8 and fails 1. The body STILL sets up
  the jump, stamps the attack time and completes; the failure refuses the completion.

#### `CNPC_VTzimisce::StartTask` `0x103ba7c0`

- **Task 0x3:** complete.
- **Task 0xf:** `m_ePathMode = 1`, then the base.
- **Tasks 0x89, 0x8a, 0x8b, 0x8e:** as Animal.
- **Task 0xa7:** face the enemy's last-known position, `SetTurnActivity`.
- **Tasks 0xbf, 0xc0:** the carry facing `0x103bf440`, activity 0xf2 / 0xf4.
- **Task 0xc1:** 0xf6 for a heavy body, else 0xf8.
- **Task 0xc2:** the gib clean-up, complete.
- **Task 0xc3:**
  1. `m_ePathMode = 2`.
  2. A live target: `m_vSavePosition = +0x6674`, then the lead helper `0x102c3b50`, the tolerance
     pushed twice, complete.
  3. Otherwise: line 0x722, fail 1.
- **Tasks 0xc8, 0xc9, 0xca:** as Hengeyokai, with timer `+0x66a8` and predicate `0x103be8e0`.
- **Task 0xcb:** `AutoMovement`, face at speed -2, stamp, activity 0x102. Complete when finished.
- **Task 0xcc:**
  1. No enemy: line 0x78c, fail 6.
  2. The pounce check `0x103bf660`. Refused: line 0x7ab, fail 0x1a.
  3. Face, stamp, activity 0x103.
  4. When finished, complete only if the translated activity (slots 375 → 381 → 376) equals
     `m_Activity`.
- **Task 0xcd:** as 0xcc, without the pounce check (no enemy: line 0x7b5), activity 0x104.
- **Task 0xce:** activity 0x105, the same completion.
- **Task 0xd1:** `+0x6324 = (ResolveTaskDistance(data) + 150)^2`, complete.
- **Task 0xd2:** `SetIdealActivity((int)data)`.

#### `CNPC_VVampireBoss::StartTask` `0x103c5ac0`

**Prologue:** `+0x669c = curtime` on EVERY task.

**Tasks:**
- **0x2f:** face the hint, `SetTurnActivity`.
- **0x14a:**
  - No hint: line 0xee, fail 4.
  - Otherwise: `SetHullSizeSmall(1)`, `Hide`, `EF_NODRAW`, complete.
- **0x14b:**
  - No hint: line 0xfd, fail 4.
  - Otherwise, in order: onto the hint, `SetHullSizeNormal`, `Unhide`, visible, solid,
    `MatchOriginAnglesToAnimation("bip01")`, 0x10, complete.
- **0x14c:** slot 618 (virtual), complete.
- **0x14d:** nothing.
- **0x14e:**
  - No monster model: line 0x131, fail 4.
  - Otherwise: the model, 0x10, render words 0, hull 0 in both words, `SetHullSizeNormal`,
    complete.
  - The packet named `m_fEffects` as `+0x168`; it is `+0x19c`.
- **0x14f:** `SUB_Remove`, complete.
- **Everything else:** VHuman.

#### `CNPC_VWerewolf::StartTask` `0x103ccda0`

The hint machine. The dispatch splits at 0x154, 0x14d and 0x14a, then uses two jump tables:
0x14e..0x153 and 0x155..0x161.

**Tasks 2, 0x4e, 0x100, 0x14a–0x14d:**
- **Task 2:** the Werewolf's own only while `m_pSchedule` is the schedule
  `0x102cc1f0(0x158)` resolves to. It then sets `m_flWaitFinished = curtime +
  werewolf_teleport_in_time` ("2.0", object `0x1093d6f0`, read `IsCommand ? 0 : m_fValue`) and
  does NOT complete. Otherwise the base runs.
- **Task 0x4e:** `m_flGoalTolerance = ResolveTaskDistance(data) + (+0x66d0) + (+0x66cc) + 5.0`,
  pushed twice, complete.
- **Task 0x100:** the Troika body FIRST, then `m_bfAINPCFlags &= ~0x10000`.
- **Task 0x14a:** with no enemy, or an empty `+0x6720`:
  1. `SetClosestPlayer`.
  2. For a live player: `+0x6710` = (template == `Player_Malkavian`),
     `AddEntityRelationship(player, D_HT, 10)`, `SetEnemy`, `SetTarget`, slot 600.
  3. Still no enemy: `DevWarning "%s could not find an enemy... oh well!"`.
  4. Always completes.
- **Task 0x14b:**
  1. The activity: zone bit 0 SET gives a random roar. Otherwise the enemy inside the view cone
     gives 0x100, and outside it the random roar.
  2. The random roar is `RandomInt(0, 1)`: non-zero gives 0x124, zero gives 0x125.
  3. `RestartIdealActivity`, then face the enemy. No completion.
  4. The packet's verdict line had the bit-0 arm backwards.
- **Task 0x14c** (path out of sight): the `TASK_WAIT_FOR_MOVEMENT` start shape.
  1. `COND 0x77`: complete.
  2. A set goal is stopped.
  3. Goal type 0: `m_bShouldMove = 0`, `TaskFail("Did not path out of player's sight")` (the code
     is the string's address `0x10661dac`), then clear the goal.
  4. Goal not active: `m_bShouldMove = 0`, `SetIdealActivity(GetStoppedActivity)`.
  5. Not at the goal (`0x102f2ea0`): `m_bShouldMove = 1`, `ValidateNavGoal`.
  6. At the goal: `m_bShouldMove = 0` and the string failure.
- **Task 0x14d:** nothing.

**Tasks 0x14e–0x153** (teleport and move hints):
- **0x14e:** `TeleportIn`, complete.
- **0x14f:** `dt = max(curtime - +0x66ec, 0)`. With `dt >= 1.0` (or unordered), `TeleportOut`,
  complete. Otherwise fail 0x1a.
- **0x150:**
  - No teleport hint: fail 4.
  - Type 0x3aa9: complete.
  - Otherwise `SetHintActivity`: running, or fail 0x15.
- **0x151:**
  1. No teleport hint: complete.
  2. Blacklist it for 5 s, remember it (`+0x66b4`), release it, clear it.
  3. Resolve `lastused.m_strTargetName` with `FindEntityByName` + `RTDynamicCast<CAI_Hint>` and set
     that hint.
  4. None: complete. Otherwise `SnapToAnimationPoint`, then `SetHintActivity`: running, or
     complete.
- **0x152:** zone 0, blacklist for 5 s unless listed (`0x10366400` also evicts an expired row),
  clear, `SetHullSizeSmall(1)`, `+0x66ec = curtime`, complete.
- **0x153:** needs a move hint, else fail 4. Then the hint debug string (discarded), then the
  hint's groundpoint into `m_vSavePosition`, complete.

**Tasks 0x154–0x161** (move, break and leap hints):
- **0x154:** needs a move hint, else fail 4. The hint's yaw (slot 219, `+4`), flipped by 180 when
  motor `+0x28` is set, stored into motor `+0x34`, then `0x102e1e20(-1)`. No completion.
- **0x155:**
  1. `SetHintActivity(move)`, else fail 0x15.
  2. For type 0x3aa0 only: add a 15 s blacklist row when `0x10366490` answers exactly 0. An
     absent hint (-1) is NOT added. This is the retail bug, reproduced.
  3. Always keeps running.
- **0x156:**
  1. No move hint: complete.
  2. Release it, remember it (`+0x66c0`), add a 3 s row when its index is 0, clear it.
  3. Chain by name: `SetMoveHint(next, m_bRandomHint)`.
  4. None: complete. Otherwise `SetHintActivity`, or `TaskFail(0x15)`, which then FALLS INTO
     `TaskComplete` (the retail defect).
- **0x157:** blacklist for 3 s unless listed, zone 0, clear, `SetHullSizeSmall(1)`, complete.
- **0x158:** `FindBreakHint`, else fail 4. The break hint's groundpoint into `m_vSavePosition`,
  complete.
- **0x159:** needs a break hint and `SetHintActivity`, else fail 0x15.
- **0x15a** (the leap):
  1. Needs a move hint, else fail 4.
  2. Jump origin = the origin; jump target = the hint's endpoint; gravity 2.
  3. Height = `atof(m_iszUserData)` plus a rise term:
     - `dz > 0`: `dz + 100`.
     - Otherwise (including `dz == 0` and unordered): `(|dy| + |dx|) * 0.25`.
  4. Complete.
- **0x15b** (the landing):
  1. `CheckStuck(0)`.
  2. Zone 0, gravity 1.
  3. A 15 s blacklist row UNCONDITIONALLY.
  4. Clear the move hint, `+0x66a8 = 0`, complete.
- **0x15c–0x161:** `RestartIdealActivity` of 0x11c, 0x11d, 0x11f, 0x11e, 0x120 and 0x121, then
  fail 0x15 unless `m_IdealActivity` took. 0x15c first runs `PositionAtHint`. 0x15f, when the
  activity took, also runs `AddSolidFlags(4)` and fires `m_OnBeginCrushAnimation` with the enemy.

**Everything else:** Troika.

#### `CNPC_VZombie::StartTask` `0x103dfd80`

Walked in the body's own compare order.

- **Task 0x151:** `m_bShouldMove = 1`, navigator movement activity 0x1014, and
  `m_flWaitFinished = RandomFloat(min, max) + curtime`. The bounds come from the lunge-distance
  fields `+0x250` / `+0x254`; the max getter runs first. A wait drawn from DISTANCE fields is what
  retail does.
- **Task 0x152:** a closest player with a player record: its slot 425 `BeFedOnByZombie(this)`.
  Always completes.
- **Task 0x153:** variant 1/2/3 plays 0x1098 / 0x109b / 0x109e; any other variant keeps
  `m_Activity`. Fail 0x15 unless it took.
- **Task 0x150:** the nearest node. None: fail 0x18. Found: a type-4 goal there, whose answer is
  ignored.
- **Tasks 0x14f, 0x14e:** `SetIdealActivity` 0x4a / 0x1081.
- **Task 0x36:** as Dog.
- **Task 0x14c:** clear the crawl-out flag, `Unhide`, activity 0x1053.
- **Everything else:** Animal.

#### `CNPC_VFrenzyShadow::StartTask` `0x10375f50` (verified)

The landed body was checked against the packet and the listing. Three corrections were made:

1. **Local ids.** The switch compared raw global ids; it now switches on `GetLocalTaskId`. The
   PlayerController tests that set raw 0x14a were corrected.
2. **Attack arguments.** `(0xf18, 1, 1)` is pushed BEFORE the task-0x37 compare (pass R), so every
   attack task makes the same `+0x5d0` call. The port no longer passes a task-0x37 flag.
3. **Tolerance.** `0x102d61b0(0)` is `NAI_Hull::Width` of hull 0 (26.0), multiplied by the DOUBLE
   0.2. The interim store to `+0x6320` comes before slot 418. The landed body used 0.

The other arms matched the listing:
- the prologue's owner hunger copy;
- the 0xae/0xaf hunt;
- the 0x123/0x124 turn with its no-enemy sentinel 1000 and the single clearance try;
- the grapple.

The packet's verdict line named `+0x6320` for `m_vecHuntPatrolTarget`; the listing writes
`+0x645c`.

#### What stays unrecovered across the species bodies

At integration `0x103d0ec0` (`FindBreakHint`), `0x10395ce0` and `0x1039ea60` (the two defeat
latches) and `0x10289ee0` stopped being seams here: family Werewolf19 / Boss19's bodies and the
base `RestartIdealActivityId` answer them.

**Unrecovered:**
- The weapon's maximum range (`+0x8c0`) has no port carrier; the 0x9f tolerances drop that term.
- Motor words `+0x18`, `+0x1c`, `+0x28`, `+0x2c` and `+0x38` (clamp centre, clamp range, facing
  flip, yaw-speed override, yaw speed) have no port carrier.
- The navigator:
  - `0x102f2ea0` (at-goal) is a seam that answers false.
  - `0x102edae0` (the node search) answers false.
  - `0x102ee620` / `0x102ee6a0` both read the mover's single goal latch.
- The player's character template (`GetCharTemplate` against `Player_Malkavian`) is a seam that
  answers not-Malkavian.
- The template AOE's third argument (`template+0xcc`) is dropped. The port's
  `CausePlayerAOEDamage` takes two arguments.
- These calls are counted seams: MingXiao `0x1039a750` and `0x10397f70`; the Tentacle helpers
  `0x1039f310` and `0x1039ef10`; the ManBat helpers; the Hengeyokai helpers; the Bach
  holy light and weapon `+0x518` / `+0x4f0`.
- The ManBat throw's bone and float cells: `ThrowModel` takes only (model, parent).

## Story 8, family RunTask19 — slot 444 `RunTask`: base `0x10288780`, Troika `0x102aacf0`, the species overrides (2026-09-27)

_Recovered 2026-09-27, 0019 story 8 pass I: lane L05, integrated as `8cc9e23a` (rebased onto Damage19 `c9977c23` and Select19 `1769381c`)._

Walked from the listings (`vtmb_asm`) with the pass-R packet
(`families-19-29/RunTask19-READING.md`) and its chunk walks. Every body answers nothing useful;
the task's status is what it writes (`TaskComplete` `0x10273e80` = `fTaskStatus 4`, `TaskFail`
slot 448 = `m_bShouldMove 0`, `+0x5c50`, `COND_TASK_FAILED`). A body that returns without either
leaves the task **running**. Only `TaskFail` sites stamp the `+0x1b44`/`+0x1b48` file/line trace;
completions never do. Port: `ElysiumNpcBaseRunTask.cpp`, `ElysiumNpcRunTask.cpp`,
`ElysiumNpcRunTaskSpecies.cpp`.

Shared vocabulary: `0x102e0b40` writes `motor+0x2c = -1.0` (the yaw clock); `0x102e1c10(yaw, speed)`
stores the ideal yaw `motor+0x34` (flipped 180 under the `+0x28` latch; direct store when
`+0x1c == 180.0`), stores `speed` at `+0x38` unless it is -1.0 or -2.0, and ends in `UpdateYaw(-1)`
`0x102e1e20`; `0x102e20b0(pos, speed)` is `0x102e1c10(yaw-to(pos), speed)`. `0x102ee620` is the
path's goal type and `0x102ee680` is exactly its non-zero test; `0x102ee6a0` is "the path has a
waypoint". `0x102dfed0` answers the enemy memory's last-known position, `vec3_origin` on a miss.

### `CAI_BaseNPC::RunTask` `0x10288780`

_Recovered 2026-09-27, 0019 story 8 pass I (lane L05)._

Dispatch `id - 2 <= 0xaf` through byte table `0x10289794` into `0x10289724`; anything else, and
every in-range id the byte table sends there, is the default `0x102896f5`.

1. **2 / 0x67** (`0x10288bb6`): complete when `!(curtime < m_flWaitFinished)`.
2. **4 / 0xb0 / 0xb1** (`0x10288bc1`): `0x102e0b40`; LKP of slot 167 `GetEnemy`; when slot 364
   `FInAimCone(lkp)` refuses, `0x102e20b0(lkp, -2.0)`; then the wait test.
3. **5** (`0x10288b7b`): spawnflag 0x400 completes; else `UTIL_FindClientInPVS(edict)` `0x101d1800`
   completes on non-zero, runs otherwise.
4. **0xb** (`0x10288c43`): no live `m_hTargetEnt` → `TaskFail(1)` line 0xc09. `range = slot 418(data)`;
   `d = |goal - GetOrigin()|2D`. When `d < range`, or when `|goal - target|3D > range * 0.5`,
   `d` becomes the 2-D distance to the target and the goal is re-aimed at it (`0x102ee220`). Then
   `d < range` → complete + `ClearGoal` `0x102ee270`; else `act = slot 571(d)`, navigator movement
   activity `0x102ee250(act)`, `SetIdealActivity(act)`.
5. **0x1f / 0x68 / 0x76 / 0x77**: the epilogue `0x10289718` — running.
6. **0x24 / 0x27** (`0x10289614`): running while `curtime <= m_flWaitFinished` and the goal type is
   set; else `m_bShouldMove (+0x1a40) = 0`, complete, `ClearGoal`.
7. **0x25 / 0x26** (`0x10289599`): running while `slot 418(data) < |GetOrigin() - goal|3D`; else the
   same stop/complete/clear.
8. **0x2a** (`0x102891cf`): complete on slot 251 `IsActivityFinished`.
9. **0x2b / 0x2c / 0x2f / 0x31 / 0x32 / 0x66** (`0x10288b4c`) and **0x6a / 0x6b** (`0x102887ad`):
   `UpdateYaw(-1)`; complete on `FacingIdeal` `0x10278c80`.
10. **0x2d** (`0x10288a18`): player = engine `PEntityOfEntIndex(1)` else `(0)`, `CBaseEntity::Instance`;
    none → `TaskFail(0x17)` line 0xbc7. Else `0x102e0b40`, `0x102e20b0(player, -2.0)`, slot 572
    `SetTurnActivity`; complete only when `curtime > m_flWaitFinished` (strict) and
    `DeltaIdealYaw` `0x102e1f90` `< 10.0` (`_DAT_1044e664`).
11. **0x2e** (`0x102889b5`): `0x102e0b40`; `0x102e20b0(lkp, -1.0)`; complete on `FacingIdeal`.
12. **0x30** (`0x1028886f`): no `m_pHintNode` (`+0x5ddc`) → `TaskFail(4)` line 0xb5c and retail then
    faults reading the null hint's `m_hHintOwner`. A hint owned by someone else →
    `DevMsg("Hint node (%s) being used by non-owner!\n")`. Complete on `IsActivityFinished`.
13. **0x34..0x37 / 0x3e / 0x3f** (`0x102891f4`): `AutoMovement`, `0x102e0b40`, LKP; for id 0x34 or 0x38
    (0x38 never reaches this arm) with capability bit 0x20000000 and `FInAimCone(lkp)`, hold the
    current motor yaw `0x102e1c10(motor+0x34, -2.0)`; else `0x102e20b0(lkp, -2.0)`. Complete on
    `IsActivityFinished`.
14. **0x38 / 0x3d** (`0x102890f3`): `AutoMovement`; for 0x38 also stop and aim at `m_hEnemy`'s LKP
    (-2.0). On `IsActivityFinished`: with an active weapon `m_bInReload (+0x898) = 1`, weapon slot 322,
    `ClearCondition(0x40)`, `ClearCondition(0x41)`, complete; with none, complete.
15. **0x39..0x3c / 0x52 / 0x53** (`0x102891c8`): `AutoMovement`, then 0x2a's test.
16. **0x4b** (`0x102889a2`): complete when `m_nSequence (+0x6f0) == m_nIdealSequence (+0x5ccc)`.
17. **0x54 / 0x55 / 0x56** (`0x102887c6`): the face target is `m_hTargetEnt` for 0x56 and slot 167
    otherwise; when it exists `0x102e0b40` and `0x102e1c10(VecToYaw(target - GetOrigin()), -2.0)`.
    `AutoMovement`; complete on `IsActivityFinished`.
18. **0x5f** (`0x10288fc4`): on `IsActivityFinished` with `m_flCycle >= 1.0`: `m_lifeState = 2`,
    `ThinkSet(NULL)`, `m_flPlaybackRate = 0`, `UTIL_SetSize` to `(-4,-4,0)/(4,4,1)` or, when
    `0x10279420` answers true, to the collision mins and `(maxs.x, maxs.y, mins.z + 1)`; then
    `SUB_StartFadeOut` `0x102695d0` when slot 552 `ShouldFadeOnDeath`, else
    `CSoundEnt::InsertSound(0x20, GetOrigin(), 0x180, 30.0)`. Never completes.
19. **0x60** (`0x102892aa`): a live cine whose `IsTimeToStart` `0x101a7540` answers yes: complete,
    `StartScript` `0x101a81a0`, the (re-resolved) director's slot 584 `StartSequence(this, m_iszPlay,
    true)`, `ClearSchedule` when `m_bSequenceFinished`, `m_flPlaybackRate = 1.0`. A live cine not yet
    due runs. A dead handle → `DevMsg("Cine died!\n")`, complete.
20. **0x62** (`0x10289439`): `AutoMovement`; on `m_bSequenceFinished (+0x65c)`: `SequenceDone`
    `0x101a8460` on a live cine and complete; with none, complete.
21. **0x63** (`0x102894e2`): when the sequence finished, or the cine's `m_hNextCine (+0x5f94)` is live,
    `Finish` `0x101a8640` (null director on a dead handle). Retail reads `+0x5f94` of a null director.
22. **0x69** (`0x102888d4`): in `NAV_JUMP`: on the ground → `NAV_GROUND`; airborne with speed
    `> 0.01` → running; else `NAV_GROUND` and `TaskFail(0x1c)` line 0xb81 — and the arm goes on.
    Unless `NAV_CLIMB`: `SetIdealActivity(0x1027a6c0())`, `m_bShouldMove = 0`, `TaskComplete` (which
    a raised failure refuses).
23. **0x6e / 0x6f** (`0x10288f43`): while `(data == 0 || curtime - timeCurTaskStarted <= data)` and the
    goal type is set: a waypoint → slot 528 `ValidateNavGoal`; none → `m_bShouldMove = 0`,
    `SetIdealActivity(GetStoppedActivity)`. Otherwise stop, complete, `ClearGoal`.
24. **0x71** (`0x1028964b`): on `IsActivityFinished`, `m_hTargetEnt` cast to `CBaseCombatWeapon` and
    `0x102521f0` (its owner) non-null → `TaskFail(2)` line 0xd35; else complete.
25. **0x72** (`0x10288e80`): no target → `TaskFail(3)` line 0xc3a; target slot 97 owner set →
    `TaskFail(2)` line 0xc30; goal type set → running; else complete + `ClearGoal`.
26. **0x74** (`0x102896d7`): complete on `FL_ONGROUND`.
27. **default** (`0x102896f5`): `DevMsg("No RunTask entry for %s\n", slot 449 TaskName(id))`,
    **complete** (`0x10289713`).

**Integration notes (L05, pass I).**
- `_DAT_1049a17c` is the `.rdata` float **190.0** (`0x433e0000`, read from the image; no writer).
  Slot 571 (`0x10289ce0`) answers ACT_WALK only on an ordered `distance < 190.0`
  (`10289cec TEST AH,0x5` / `10289cf4 JNP`); at or beyond it, and on NaN, ACT_RUN.
- The wait gates (`0x10288c1d` / `0x10288c25 AND 0x100`), the timed walk (`0x10289625 AND 0x4100`),
  the within-distance walk (`0x102895fd AND 0x100`), the death cycle (`0x10288fe4 AND 0x100`) and
  the 0x6e timer (`0x10288f66 AND 0x4100`) all keep the task RUNNING on an unordered compare.
- `0x102dfed0` is called with a NULL enemy too (arms 1, 10, 12, 13): the record walk answers the
  last position-only ("danger") record before `vec3_origin`.
- 0x2d with no player faces worldspawn (`PEntityOfEntIndex(0)`, `0x10288a7e..0x10288a8e`); the
  `TaskFail(0x17)` needs the world edict to have no entity.
- 0x72's slot 97 (`+0x184`) answers the RESOLVED owner (`0x10288eb4`).
- `0x102e20b0` -> `0x102e2750` is `JMP [outer vtbl+0x80c]`, slot 515 `CalcIdealYaw` (`0x10274b30`),
  whose answer is `VecToYaw` (`0x101d2c70`) in `[0, 360)`. `0x102e2020` is `0x102e2750` then the
  `+0x28` flip and the `+0x1c == 180` direct store. `0x102e1cf0` stores `(motor+0x10)->vfunc0()` into
  `motor+0x38` (the recalculated yaw speed).
- `m_lifeState` (`+0x200`) is one port word, `AnimEventLifeStateWord` (family Misc19's).

**Unrecovered:** `0x10279420`'s predicate (four corner traces, mask `0x2400b`, not walked);
`0x102ee220`'s re-aim (no port navigator goal); the vfunc behind `0x102e1cf0`.

### `CAI_BaseNPCTroika::RunTask` `0x102aacf0`

_Recovered 2026-09-27, 0019 story 8 pass I (lane L05)._

Dispatch `id - 2 <= 0x147` through byte table `0x102ac844` into the 57-entry table `0x102ac760`;
index 0x38 and every out-of-range id tail-call `CAI_BaseNPC::RunTask`. Arms by index:

- **0x00** `0x102aad61` (2, 0x67, 0x68): `0x102aab70` (with `m_bfAINPCFlags2 & 0x10` aim at the
  enemy's LKP, else with `& 0x20` at `m_hTargetEnt`, each only outside the aim cone, -2.0), then the base.
- **0x01** `0x102ab659` (4, 0xb0, 0xb1): aim at the live shoot-target override, else the enemy's LKP,
  else `TaskFail(6)` line 0x407c **and still aim at the uninitialised point**; steer unless in the aim
  cone (-2.0); the wait test.
- **0x02** `0x102aad7e` (5 `TASK_WAIT_PVS`): spawnflag 0x400 or `ShouldThinkFrequently` completes;
  else `0x101d1a90(m_hClosestPlayer, this)`: false → running; true → slot 614, `m_flLastThink` and
  the four `+0x6254..+0x6260` stamps = curtime, complete.
- **0x03** `0x102aae43` (0x2b, 0x31): `SetTurnActivity` unless `m_afMemory & 0x2000`, then the base.
- **0x04** `0x102aae61` (0x2e): the turn test; aim at the override or the enemy's LKP (-1.0); complete
  on `FacingIdeal`. _Walked 2026-10-04 (0002 V4 packet S1):_ `m_afMemory & 0x2000` clear → slot 572
  `SetTurnActivity` every call; `0x102e20b0(point, -1.0)` = `SetIdealYawAndUpdate`, whose `-1.0`
  re-reads `MaxYawSpeed` (`0x102e1cf0`) before `UpdateYaw(-1)`. **Unlike the base arm `0x102889b5`
  it does not call `0x102e0b40`**, so the yaw clock (`motor+0x2c`) is reset only by `StartTask`'s
  turn tail `0x102a44d1` and `UpdateYaw` `0x102e1e20` integrates the real time since the last call
  (0.1 s, double `0x104493d0`, on the first). `AI_ClampYaw` is `0x102e1d10(rate = speed × 10.0,
  current, target, dt)`: `step = rate × dt`; `move = target − current` wrapped into ±180
  (`>= 180` → `− 360` when `target > current`, `<= −180` → `+ 360` otherwise); clamp to `± step`;
  the sum quantised to 16 bits.
- **0x05** `0x102ab0a9` (0x34): `AutoMovement`, aim (-2.0). With `m_iBurstFireCount > 0`: no weapon →
  complete (and retail then reads the null weapon); next attack time `0x10252450` still ahead →
  running; else decrement, `0x102aaa60` (the hint idle re-arm; stamps `m_flLastAttackTime`) true →
  running, false → complete. With no burst, complete on the activity.
- **0x06** `0x102ab1e5` (0x35, 0x3e, 0x3f): `AutoMovement`, aim (-2.0), complete on the activity.
- **0x07** `0x102ab2b2` (0x36, 0x37, 0x8c, 0x9a): `AutoMovement`; with an enemy and slot 591, aim at
  its origin (-2.0); complete on the activity.
- **0x08** `0x102aad1f` (0x4b): complete on the ideal sequence, else the wait test.
- **0x09** `0x102ab4c5` (0x54..0x56): face `m_hTargetEnt` (0x56; a stale handle is a null deref) or
  the override or the enemy's LKP with `0x102e1c10(VecToYaw, -2.0)`; `AutoMovement`; the activity.
- **0x0a** `0x102abb90` (0x5f, 0xe9, 0xeb) and **0x28** `0x102abb89` (0xea, `BloodExplode` first):
  gate `(IsActivityFinished && m_flCycle >= 1.0) || m_IdealActivity == 1`; credit
  `m_hClosestPlayer` (self for 0xe9/0x5f); `m_lifeState` 1 → 0; `Die(credit, 0, 0)`; 0xe9/0xea slot
  402; 0xeb with non-zero data slot 77. Never completes.
- **0x0b** `0x102aaf2e` (0x6e): timeout (`data != 0 && data < elapsed`) or no goal type → stop,
  complete, `ClearGoal`; no waypoint → stop, stopped activity, **complete**; `0x102f2ea0` not arrived
  → slot 528; arrived → stop, complete.
- **0x0c / 0x0d** (0x7a / 0x7b): `0x102aa860` on `m_sppPatrolPath` / `m_sppPatrolPathHunt`.
- **0x0e** `0x102ab83c` (0x92, 0x95, 0x98, 0xe6, 0xec, 0xee, 0xef, 0xf0, 0x10f, 0x110, 0x113):
  `AutoMovement`, the activity. **0x0f** `0x102ab2a3` (0x93, 0xe0): the activity.
- **0x10** `0x102ac4ef` (0x94): unless `NAV_JUMP` on the ground: `0x102a0870`; the wall probe
  `0x102a0490` → `ACT 0x91` (linked sequence or restart), yaw from the wall normal, `0x102c4e30`,
  `m_KnockbackVelocity = normal * 100`, `SetSchedule(0x14e)`; motor `+0x30 = 0`; running while
  `vz >= 0` or `0x102c4eb0` refuses. Landing: linked sequence `0x10345480`, motor slot 8,
  `NAV_GROUND`, `m_bJumping = 0`, complete, `ACT 0x90`.
- **0x11** `0x102abea5` (0x96): `SetSchedule(0x14f)` once `m_fKnockbackWallHitFallTime` passes.
- **0x12** `0x102abedb` (0x97): the playing sequence's activity 0x91 finished → `ACT 0x92`; the same
  airborne test; landing → `ACT 0x93` then complete.
- **0x13** `0x102ac2ed` (0x99): `AutoMovement`, `Bip01 Spine2`; unfinished → stamp the bone time and
  position; finished → `0x102c4e80`, a `CVDmg_t` (dice 1, to-hit 1, source this), damage info
  `(this, this, 1.0, 0, 0, dmg, -1)` with force `normalize(last - bone) * (last time - curtime) *
  50000`, stat list 0 `SetBaseToStatValue(0xf, 0x11)`, slot 144, slot 403, complete.
- **0x14 / 0x16** (0x9c / 0x9e): complete on the activity, else `AutoMovement`. **0x15** (0x9d):
  finished → drain `m_QueuedBurnDamage` (take record 0, swap the last in), when alive stop and fade
  (2.0) every live `m_hBodyFireParticles[18]` whose `+0x484` is set, slot 616, complete.
- **0x17** `0x102ab900` (0xa7, 0x11d, 0x12e), **0x2a** `0x102ab63b` (0xf7): the turn test,
  `UpdateYaw(-1)`, `FacingIdeal`. **0x18** (0xb2): the same after `TaskFail(0x22)` line 0x4105 with
  no interesting place.
- **0x19** (0xb3): no patrol interest place → complete; else turn, `FacingIdeal` → `+0x6300 = +0x659c
  = 0`, complete. **0x1a** (0xb4): no place → `TaskFail(0x23)` line 0x4141; `0x102aa210` done → the
  holster (`place+0x571`), `LeaveInterestingPlace(1, "…(RunTask-WaitFinished)")`, complete. **0x1b**
  (0xb5): the patrol twin, firing `m_OnInterestingPlaceLeft` when arrived, `0x102da600`, clearing the
  place words.
- **0x1c** (0xb6, 0xb7): finished → navigator movement activity `TranslateActivity(9)`, complete.
  **0x1d** (0xb8): finished → clear flag 0x20000000, `MoveToBoneOriginAngles("Bip01")`, complete.
- **0x1e** (0xb9): `0x102c1400` activity → `SetActivity` + `UpdateYaw(-1)`; -1 → complete and
  `ClearCondition(0x6f)`. **0x1f** (0xba, 0xbb): slot 588, the wait test. **0x20** (0xbc, 0xbd): with
  a live closest player within 128 and `COND 0x5a`, on a finished activity restart `0x10f7` **when no
  weighted sequence exists** else slot 588; otherwise slot 588; the wait test.
- **0x21** (0xdd), **0x25** (0xe2, 0x149, after `AutoMovement`): the activity. **0x22** (0xde):
  finished and ideal 0x1068 → complete, flags `&= 0xbbf5ffff`; finished otherwise → restart 0x1068.
  **0x23** (0xdf): `Die(0,0,0)`. **0x24 / 0x26** (0xe1 / 0xe3): `SetActivity(ftol(data))` unless
  `m_bLastDisciplineResist` and finished → complete. **0x27** (0xe7): the ideal sequence.
  **0x29** (0xed): the epilogue — running.
- **0x2b** (0xf8..0xff, 0x148): `UpdateYaw(-1)`; when the wait passed or the activity finished and
  `FacingIdeal`, clear 0x08000000, complete. **0x2c** (0x107..0x109): finished → ideal 0xf1d →
  `SetIdealActivity(0x1052)`; else clear 0x4000, complete. **0x2d** (0x10a..0x10c): `0x102aab70`,
  `AutoMovement`, the activity. **0x2e** (0x10d): `AutoMovement`; override → aim; cover object is
  the enemy → LKP; else **complete and aim at an uninitialised point**; `FacingIdeal` → complete.
- **0x2f / 0x30** (0x116 / 0x117): finished → complete, clear 0x40000. **0x31** (0x118): ideal is the
  hint's `0x102a13d0` → `0x102a1620`; is `0x102a1510` with non-zero data → `0x102a15c0`; else
  complete, clear 0x40000.
- **0x32** (0x11b, 0x11c, 0x123): `AutoMovement`, aim at the enemy (-1.0), past the wait →
  `m_flDesiredMoveYaw = 0`, complete. **0x33** (0x122, 0x124): the same aim; finished and inside the
  wait without `COND 0xe`: `ACT 0x1121`, else 9, else `TaskFail(0x15)` line 0x4255, restart it;
  otherwise the yaw clear and complete.
- **0x34** (0x128): the 0x01 aim (`TaskFail(6)` line 0x4304); past the wait, the activity.
  **0x35** (0x137): `AutoMovement`, aim at the enemy (-1.0), the activity. **0x36** (0x139, 0x13b,
  0x13c): `UpdateYaw(-1)`, the activity. **0x37** (0x13a): unless landed: yaw from the local velocity
  (-1.0), motor `+0x30 = 0`, running while `vz >= 0` or `0x102c4eb0` refuses; landing: motor slot 8,
  `NAV_GROUND`, `m_bJumping = 0`, complete.

**Integration notes (L05, pass I).**
- 0x7a / 0x7b call lane Script19's `0x102aa860` port (`IssuePatrolMoveRun`) on `+0x658c` / `+0x6594`.
- 0xb4: inside `0x102b53d0(this, 1, ...)`, with `+0x62e8` arrived, `0x102b54f1..0x102b5500` fire the
  NPC's `m_OnInterestingPlaceLeft` (`+0x5f8c`, activator the place) before `0x102da600(place, this,
  1, fired)`.
- 0xb5: `0x102da600(place, this, 0, arrived)` (`0x102abad3..0x102abae2`, EBX = 0): with its second
  argument 0 it only fires the place's `OnNPCLeft` (`+0x468`) when arrived; no claimant removal.
- The unordered compares keep running at `0x102aad3c` (the shared wait), `0x102abead` (0x96),
  `0x102abbb1` (the death cycle: NaN is not finished) and `0x102ab19c TEST AH,0x41` (the burst's
  next-attack gate); 0xbc/0xbd's player distance (`0x102ab3a0..0x102ab3ab`) takes the slot-588 path
  on greater OR unordered.
- `0x102c1400` answers -1 only when `IsInDialog()` (`0x102c1170`) is false; inside a dialogue it runs
  the dialogue upkeep and answers `m_Activity` or 0xf1 / 1 (the dialogue family's body).

**Unrecovered:** `0x102f2ea0`'s tolerance source; `0x102a0870`, `0x102a0490`'s traces;
`0x103454c0`/`0x10345480`'s studio link words; `0x102c4e80`; the rest of `0x102b53d0` (its two
sounds, the `+0x14b8`/`+0x14bc` bit clears, `0x102ae310`); `place+0x571`'s holster slot 315.

### The species `RunTask` overrides (slot 444) — `0x1035f940` … `0x103cdfb0`

_Recovered 2026-09-27, 0019 story 8 pass I (lane L05)._

- **`0x1035f940` CNPC_VAnimal** (Rat, Scurrying): 0x36/0x37 `AutoMovement`, face LKP (-2.0),
  activity; 0x89/0x8a the same, completing also when `curtime - m_flLastAttackTime > data`;
  0x8b/0x8e `AutoMovement`, activity; else Troika.
- **`0x10374a20` CNPC_VDog**: tasks 2/0x67 in `m_NPCState == 3` aim at `UTIL_GetLocalPlayer`'s origin
  (-2.0); always `CNPC_VAnimal::RunTask`.
- **`0x103e01d0` CNPC_VZombie**: 0x14c/0x14f/0x150 activity; 0x14e activity → complete,
  `AddMiscFlag(0x80000)`, non-virtual `CreateCorpse(&m_vecDeathForceVector, this+0x668c)`; 0x151
  wait passed or no goal → stop, complete, `ClearGoal`; 0x153 activity, else (AI type 7) aim the motor
  at the closest player (`0x102e2020` + `UpdateYaw(-1)`); else `CNPC_VAnimal`.
- **`0x10384ab0` CNPC_VHuman** (28 human classes): 0x89/0x8a as Animal; 0x8b the melee swing (enemy
  and its `+0x9c` combat view, `GetMeleeDiceRolls`; ideal 0x1157 → `IsMeleeSwingOver`, else
  `m_flCycle` against 0.5 with a roll (band `0x103498b0 == 0` arms completion) or 1.0 without, then
  the activity; finish → 0x1155 has a sequence and armed → complete, else `TaskFail(0x21)` line 0x1da);
  0x8d no enemy → complete, else wait for every event of the enemy's sequence; 0x8e..0x91 face when
  the enemy has a combat view, complete on the activity or `m_flNextAttack` passed; else Troika.
- **`0x103793e0` CNPC_VGargoyle**: 0x31 turn, `UpdateYaw(-1)`, `FacingIdeal`; 0x12f activity; else Human.
- **`0x1037b9f0` CNPC_VGhoulCroucher**: 0x14a activity; 0x14b activity → `m_bUnawareExited = 1`; else Human.
- **`0x103b38a0` CNPC_VTaxiDriver**: 0xb9 `0x102c1400 == -1` → complete, `ClearCondition(0x6f)`,
  `m_bFirstThink = 0`, `SetActivity(1)`; else Human.
- **`0x103c5f40` CNPC_VVampireBoss**: 0x14d `WaitForTransformation`; else Human.
- **`0x103af780` CNPC_VSheriffMan**: 0x154 swallowed (running); else VampireBoss.
- **`0x1035d8b0` CNPC_VAndreiBlood**: 0x150 `FacePlayerAdvance`; 0x151 `m_takedamage = 0` at cycle
  0.5, activity; 0x152 the unhide (slot 67, `m_takedamage 2`, counters reset), face, activity; 0x154
  activity → `m_bForceTeleport`, complete; 0x156 force, hit cap or 5 s since the wait start → complete;
  else VampireBoss.
- **`0x103612e0` CNPC_VAsianVampire**: 0x13a VampireBoss then restart 0x2d when not in it and
  `vz <= 250`; 0x150 `SetupJump(m_pHintNode)`, `m_bPathBlocked = 0`, complete; 0x151/0x152 running.
- **`0x1036bfc0` CNPC_VChangBros**: `StoreArenaCenter` first; 0x8b the Human swing variant (line
  0x295); 0x13a as AsianVampire at 500; 0x150/0x151/0x153/0x15d `UpdateYaw`, `AutoMovement`,
  activity; 0x156 until `m_fEnergyChargeTime`; 0x157 the energy ball at cycle 0.591; 0x15a the other
  brother's `ReadyForUnited` (none → `TaskFail(1)` line 0x255); 0x15b the emitters and the centre
  emitter at arena centre + 50; 0x15c the united time (none → `TaskFail(1)` line 0x26b).
- **`0x103a8990` CNPC_VSabbatLeader**: prologue `andrei_force_awaken` → `StartTransformation` with a
  player within 500 (2-D); 0x15c the dive-in (yaw from velocity, splash emitters and wav at
  `_DAT_1093c33c`, landing gravity 1, `NAV_GROUND`, slot 66, complete); 0x139 the retreat wav then
  VampireBoss; 0x13a landing stamps and the jump steer, then VampireBoss; 0x13c the normalized
  100-unit velocity toward the player (face speed 50), then VampireBoss; 0xbc/0xbd running in
  dialogue; 0x15d the emerge (slot 67, `m_fEffects &= ~0x20`, not-solid cleared, complete); 0x15e
  the warning time; 0x36/0x37/0x9a/0x160 face the enemy at speed 10, activity; 0x161/0x163
  activity; 0x162 one second after the task start.
- **`0x1038d130` CNPC_VManBat** (falls to `CAI_BaseNPC::RunTask` DIRECT): 0x14a aims at the SUM of
  its origin and the fly node's, activity → complete + flap; 0x14b arrived → teleport onto the node
  when the descrambled mode equals `fold(0xfa0b0695)`, clear the node, complete; 0x14c on the
  ground → stop, leave flight, complete, `ACT 0x1171`, `fall.wav`; 0x14d aim along velocity; 0x150
  land; 0x152/0x155/0x157 on arrival; 0x156 release the carried body; 0x158 flap; 0x159/0x160 the
  fly-by landing onto a live target (`TaskFail(1)` without the file trace otherwise); 0x15a the
  melee weapon attack (`TaskFail(0x1f)` line 0x4e1); 0x15b the spotlight kill; 0x15c the next
  script node (`TaskFail(4)` on a missing hint); 0x161 the throw (`ThrowModel`, target `Kill`); 0x162
  the fly-by sound; 0x163 the coast timer.
- **`0x10393930` CNPC_VMingXiao** / **`0x1039d750` CNPC_VMingXiaoTentacle**: the melee/throw arms
  (weapon switch back, throw release, attack timers from `0x103983d0`), the 0x8e event walk, the
  tentacle's flex blend `F%02d` over `[0, 4]` s and its type-19000 shoot-hint walk from the shared
  cursor `DAT_1093bd34`.
- **`0x103bb1e0` CNPC_VTzimisce** / **`0x10380cb0` CNPC_VHengeyokai**: the claw/grab arms
  (`0x103be8e0` / `0x103822a0`, `COND 0x1b`, `m_flTaskFailTimer`), the translated-activity
  completions (slots 375 → 381 → 376), the hint-usable test `TaskFail(0x1a)` line 0x911.
- **`0x103c3870` CNPC_VTzimisceRunner**: 0x122..0x124 `AutoMovement`, stop + face the enemy (-1.0),
  past the wait `m_flDesiredMoveYaw = 0`, complete.
- **`0x103cdfb0` CNPC_VWerewolf**: the hint movers (0x14b..0x14d), the anim-point snaps, the fake hull,
  `TeleportOut` + `SetSchedule(0x158)` on activity 0x10b, `m_lifeState 2` + `OnFinishCrushAnimation`
  (0x15f); its `TaskFail("Did not path out of player's sight")` is the SDK's text fail code (the
  string's address as the reason), the port's `TaskFailText`. `DAT_1093f9a4` is ConVar
  `werewolf_force_teleport` (`0x1093f9a0`).

**Integration notes (L05, pass I).**
- `0x103498b0`: margin = roll `+4 - +0xc - +8`, then four `FCOMP` / `TEST AH,0x41` / `JP` rungs
  against `_DAT_10739fa0..fac` -- the `rules.txt` defender thresholds (`ClassifyDefender`'s ladder,
  one lower): an ordered `margin <= T` answers 0..3, else 4.
- `GetMeleeDiceRolls` (`0x10345980`) searches the ATTACKER's array (`+0xa88`, filled by
  `CalcAndStoreMeleeDiceRolls 0x10346380` keyed by the defender); Human and ChangBros 0x8b ask the
  enemy for its swing at this NPC.
- `_DAT_1093c33c` = 33.0 / 60.0 = 0.55f (static initialiser `0x103a5870`, `0x104c3cf4` / `0x104c3cf8`).
- `0x1039aa20` / `0x10383470`: when `+0x1560 + 2.0 < curtime` (ordered), the type-0 stat list
  `Set(0xf, 0)`, Hengeyokai also `+0x6694 = 1`, then `TaskComplete(0)`. `0x10398db0` is the tentacle
  GRAB (`phys_animlink`, mode 3). `0x1038e720` is lane Script19's wing selector.
- `0x1039ee20` is `IsAreaClear(pos, 0x202400b, mins, maxs)` over hull 15 with X and Y doubled
  (Z not); Tentacle 0x150's goal is type 4, activity 0x13, tolerance -1.0, `SetGoal(goal, 0)`.
- ChangBros 0x15b creates `chang_center_emitter` through `0x100fbc90`, the named-emitter create.
- SabbatLeader 0x15e / 0x162 complete only on an ordered `>` (`0x103a906b AND 0x4100`).

**Unrecovered:** `IsMeleeSwingOver` / the event walk inputs (no studio data at the kernel tier);
`0x1038c170`, `0x1038fe30`; the goal target word `DAT_1093bd30`; `m_iWasOccluded`'s producer beyond
Bach's camper pass.

## Story 8, family Select19 — slots 437 / 438: `0x1028a260`, `0x1028a380`, `0x102af660`, the species selectors (2026-09-27)

_Recovered 2026-09-27, 0019 story 8 pass I: lane L06, integrated as `abfae21c`._

The schedule selectors: slot 437 `PreSelectSchedule` and slot 438 `SelectSchedule`, the base
`CAI_BaseNPC` body, the `CAI_BaseNPCTroika` pair, and the species overrides. Read off the listing
(`vtmb_asm`) and the decompile; schedule names are each class's own corpus registrations
(`Content/ElysiumCorpus/ai/schedules/<unit>/space.json`).

Conventions below: "trace L" is the `+0x1b30`/`+0x1b34` `__FILE__`/`__LINE__` stamp (ABSENT in the
port; recorded through `RecordScheduleEvent`); "sel N" is the `+0x1b2c` selector id
(`SelectScheduleSelector`, carried as a word — the agreed visual-only modernization). `Has(c)` is
`HasCondition 0x10269aa0`, `Int(c)` is `HasInterruptCondition 0x10269d30` (needs the bit in the
running schedule's mask). States are retail's `m_NPCState` numbers.

Slot 437's Troika body `0x102ae920` is walked where the oracle already held it: `conditions-and-states.md`
§ "`GetSchedule` `0x102ae920` runs ahead of `SelectSchedule`".

### `GetNewSchedule` `0x1028a260` — the selector pair (no verdict row)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L06)._

`+0x1b2c = 0`; slot 437; when it answers 0, slot 438 as a tail jump. `GetNewSchedule 0x102814d0`
calls it, and three Select19 bodies re-enter through it (base case 2, Troika case 2, Troika
PreSelect state 2 / 0xe). Port: `FElysiumNpcBase::SelectNewScheduleRetail`.

**Unrecovered:** nothing named by the walk.

### `0x1028a380` `CAI_BaseNPC::SelectSchedule`

_Recovered 2026-09-27, 0019 story 8 pass I (lane L06)._

sel 1, then `switch (m_NPCState)`, table `0x1028a9f4`.

- **1 IDLE**: any of HEAR_DANGER/COMBAT/WORLD/BULLET_IMPACT/PLAYER → 6 `ALERT_FACE` (0xe5b);
  GIVE_WAY → `0x38` (0xe60); navigator path type (`0x102ee620`) 0 → 1 `IDLE_STAND` (0xe65);
  LIGHT_DAMAGE with a sequence for ACT 0x49 → clear `m_bCondTookDamage`, `0x14 SMALL_FLINCH`
  (0xe6b); else 2 `IDLE_WALK` (0xe70).
- **2 COMBAT**: NEW_ENEMY → 5 `WAKE_ANGRY`; ENEMY_DEAD → `SetEnemy(NULL)`, `ChooseEnemy`: chosen →
  clear 0x58 and re-enter `0x1028a260`; not → `SetState(3)` and re-enter; damage (0x4c/0x4d) with
  `m_afMemory & 0x40` clear and an ACT 0x49 sequence → `0x14`; `IRelationType(GetEnemy()) == D_FR`
  → `0xd FEAR_FACE` unless SEE_ENEMY/0x4c/0x4d, then `FearSound` and `0x1b RUN_FROM_ENEMY`; no
  SEE_ENEMY → `0xb COMBAT_FACE` or, occluded, `0xf CHASE_ENEMY`; then 0x5f → `0x15`, 0x4f → `0x21`,
  0x50 → `0x22`, 0x51 → `0x1f`, 0x52 → `0x20`, 0x61 → `0xb`, and neither 0x4f nor 0x51 → `0xf`
  (always, the ladder already answered both); otherwise DevWarning "No suitable combat schedule!".
- **3 ALERT**: ENEMY_DEAD with ACT 0x61 → 8 `ALERT_SCAN`; no damage → hear family 6, else 9
  `ALERT_STAND`; damage → clear `m_bCondTookDamage`; `|DeltaIdealYaw| < (1.0 - m_flFieldOfView) *
  60.0` → `0x19 TAKE_COVER_FROM_ORIGIN`, else an ACT 0x49 sequence → 7, else 6.
- **4 SCRIPT**: live `m_hCine` → `0x2e AISCRIPT`; else DevWarning "Script failed for %s",
  `CineCleanup`, 1.
- **6** → 1; **7 DEAD** → `BecomeClientRagdoll` ? `0x2c` : `0x2b`; **0xc** → 1; **0, 5, 8..0xb, >0xc**
  → DevWarning (`NPC_STATE_IS_NONE!` / `Invalid State for SelectSchedule!`) and `0x43 FAIL` (0xf21).

**Unrecovered:** nothing in the body. The port answers `BecomeClientRagdoll` false (no client
ragdoll forms; the death handoff is `CompleteDeathHandoff`) and the navigator path type `-1`.

### `0x102af660` `CAI_BaseNPCTroika::SelectSchedule`

_Recovered 2026-09-27, 0019 story 8 pass I (lane L06)._

sel 2; `switch (m_NPCState - 1)` over 1..0xe, table `0x102b0bf0`; 0, 4..7, 9, 0xa, >0xe → base.

- **1 IDLE**: busy with a discipline or in a choreo scene → `0x6b` (0x49d2). Slot 607 (follower
  ladder) non-zero is **returned** (`0x102af6b0 JNZ 0x102b0af5`). Patrol path `+0x6590`: its
  `m_iSchedule` non-zero → `0x1029f650`, returned (0x498d); zero → DevMsg "WARNING:  Patrol path
  for '%s' has no schedule." and `0x1029f5d0`. `m_bUseInteresting`: path type != 8 and no place →
  `0xff` (0x49a9); else Int(0x13) → `0x102`, Int(0x10) → `0x106`, Int(0x11) → `0x105`, else
  `0x100`. `m_bAllowAlertLookaround`: `RandomInt(0,99) < min(0x1e, (sightings+2)*5)` → `0x4f`.
  A live `m_hBlockedDoor` or `m_hCondHitByDoor` → `SelectDoorObstructionSchedule` non-zero
  returns. `m_bReturnToInitialPos` → consumed, `0x45`. Else `0x6b`.
- **2 COMBAT**: clear `m_bCondTookDamage`. ENEMY_DEAD as the base (traces 0x4abf / 0x4acb). Damage
  flinch `0x14` (0x4ad5). D_FR → `0xd` / `FearSound` + `0x1b`. NO_PRIMARY_AMMO with a weapon whose
  reserve (`0x103346c0(+0x744)`) > 0 → `0x28 HIDE_AND_RELOAD`. The gate `+0x6444` (unnamed dword)
  zero or WAITING_ATTACK_TIME → the ladder; else a `0x6000` weapon → `0xec`, otherwise the base.
  Ladder: no SEE_ENEMY → `0xb` / occluded `0xb1`; 0x08 or 0x5f → a `0x6000` weapon with neither
  0x2f nor 0x63 → `0x102b7f40` ? `0xef` : `0xf0`, else `0xb8`; 0x4f → `0x101e3f50` → `0xef`, or
  `RandomInt < 0x14` with no hint node and `m_flEnemyDist < 800.0` → `0xef`, else `0xed`; 0x50 →
  `0xee`; 0x51 → `0xdc`; 0x52 → `0xdf`; 0x61 → `0xb`; 0x60 → `0xb1`; 0x63 or 0x2f → `0xbc`; 0x59
  → `0x17`; 0x66 with a `0x6000` weapon → `0xf0`; 0x4a with a weapon → `0x6000` → `0xf0`, `0x18000`
  → `0xca`; else `0xbf`.
- **3 ALERT**: `0x102b8a60`, `0x102b8c40`, DETECTED_ATTACK → `0x56`, `0x102b7370`, `0x102b9060`,
  else `m_bGoToIdleState = m_bForceStateChange = 1` and **`0x4b ALERT_WAIT`** (0x4a08).
- **8 FLEE**: COVER_FAILURE `0x39` → `0x73`. None of SEE_ENEMY, SEE_FEAR, 0x4c, 0x4d, 0x4e, 0x21,
  0x1f → DETECTED_ATTACK `0x56`, INVESTIGATE_SOUND (clear INITIAL_FLEE, investigate clock +2.0,
  `CommitBestSound`) `0x48`, else `0x77`. Otherwise clear `m_bCondTookDamage`; no INITIAL_FLEE →
  `0x73`; else clear it, flee-sound clock `RandomFloat(10,20)`, `FleeSound`; damage → `0x72`;
  supernatural flee `0x21` → offender to slot 596; player != offender → live offender `0x73`, else
  save the location `0x76`; player == offender → `InsertSound(8,…)`, then the incident (or, flee
  only with SEE_PLAYER, `0x1017fd60`) and the processed count; `m_flPlayerDist <= 512` and
  `RandomInt < 0x50` → `0x71`, else `0x70`. Criminal `0x1f` mirrors it. SEE_FEAR → slot 596 on
  `m_hLastSeenFearEnt`; `0x72`.
- **0xb HUNT**: Int(1)/Int(0x26) → `0x81`; `0x102b8c40`; Int(2) → `0x82`; Int(sound family) →
  investigate clock, `CommitBestSound`, (0x6d or 0x70) and `RandomInt < 100` → `0x7f`, else
  `0x80`; Int(0x72) → `CommitBestSound`, `0x50`; SEE_SOUND_SOURCE → `0x102b8d20(0x84, 0x73)`; no
  hunt path `+0x6598` → clear `MADE_HUNT_PATH`; bit clear → curtime at/past `m_flHuntExpireTimer`
  → `0x85`, a live slot-168 enemy → `0x7c`, else `0x7d`; bit set → `0x7e`.
- **0xc** → `0x6b`; **0xd** → `0x44`; **0xe** → the `m_iSubState` walk (default → 1 `0x116`, 1 → 2
  `0x117`, 2 → 3 `0x118`, 3 → 4 `0x119`, 4 → 5 SEE_ENEMY ? `0x11b` : `0x11a`, 5 → 2 `0x117`).

**Unrecovered:** the patrol-path object (no port `CAI_PatrolPath`), `InsertSound`'s two globals.

### Helpers `0x102b8a60`, `0x102b9060`, `0x102b8d20` (no verdict rows)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L06)._

- `0x102b8a60`: Int(IGNORE_UNKNOWN) while not investigating (`m_afMemory & 0x8000000`) → `0x60`;
  Int(SEE_UNKNOWN) / Int(UNKNOWN_ADVANCING) / Has(INVESTIGATE_SIGHT) → alert level 3, then not
  investigating `0x59`, LOOKED_AT_UNKNOWN → UNKNOWN_RUN_TIMER ? `0x5e` : `0x5c`, else `0x5a`;
  Int(LOST_UNKNOWN) → investigating with the run timer `0x5f`, else `0x5d`; LOOKED_AT_UNKNOWN →
  `0x5c`; else 0.
- `0x102b9060`: Int(INVESTIGATE_SOUND): combat or bullet → clock, `CommitBestSound`, level 3,
  investigating → `0x52`, not frenzied `0x10000` and `RandomInt < 100` → `0x50`, else `0x51`;
  HEAR_WORLD → `m_BestSound = m_LastSoundWorld`, `m_InvestigateSound = m_BestSound`, `0x51`;
  player or danger → `CommitBestSound`, then TAIL-JUMPS into `0x102b8980`, whose answer IS the
  schedule (`0x4c`/`0x4d`/`0x51`/`0x52`). Then SEE_SOUND_SOURCE → `0x102b8d20(0x89, 0x73)`; not
  frenzied `0x10000` and Int(HEAR_FLINCH) → `CommitBestSound`, `0x50`; else 0.
- `0x102b8d20(hated, feared)` (`RET 0x8`; the answers are the two stack arguments): the best-sound
  source's enemy; `D_HT` → the source's memory of the enemy (when the source made the last combat
  or bullet sound) or its forward × 128 + origin into `m_vSavePosition`, `hated`; `D_FR` →
  `feared`; else investigate clock `curtime + 20.0`, 0. A dead source answers 0 with no re-arm.

**Unrecovered:** nothing named by the walk.

### The species selectors (slots 437 / 438) — `0x1035fb50` … `0x10375d90`

_Recovered 2026-09-27, 0019 story 8 pass I (lane L06)._

- `0x1035fb50` **CNPC_VAnimal** (sel 5): DO_STARTLED → cleared, `0x15d`; idle: patrol node schedule
  (no stamp); use_interesting: path type != 9 and no place → `0x156`, else `0x157`; alert:
  `0x102b8a60`, `0x102b9060`; then the Troika body.
- `0x103742d0` **CNPC_VDog** (sel 0xd): SHOULD_SNARL `0x7b` → cleared, `0x166`; idle: patrol node,
  no use_interesting → `0x164`; combat clears `m_bCondTookDamage` and skips; idle/alert:
  Int(PLAYER_BEFRIENDED `0x7d`) → `0x167`; the animal's `0x6b` becomes `0x164`.
- `0x103ac610` **CNPC_VScurrying** (sel 0x20): inside `m_flFrightEndTime` → `0x162`; PLAYER_TOOCLOSE
  `0x78` → `0x162`; HEAR_BULLET_IMPACT → fright origin from the bullet sound, end time = now +
  duration, `0x162`; the animal's `0x6b` becomes `0x161`.
- `0x103df2e0` **CNPC_VZombie** (sel 0x2b): crawl-out → `0x161`; EF_NODRAW → slot 67 `Unhide`; not
  alive → `CreateCorpse(death force, death info)`; AI type 7/8: provoked (0x4c, 0x4c again, 0x4d,
  `0x79`) → type 1, else without GIVE_WAY → `0x16b`; idle: type 1 → `SetState(2)`, SEE_ENEMY ?
  `0x165` : `0x169`; type 5 → `0x169`; patrol node; combat/alert: a player enemy seen and not
  obfuscated (`0x10146a80`) → `0x165`, else `0x169`; then the animal.
- `0x10384ee0` **CNPC_VHuman** (sel 0x14): combat only — clear `m_bCondTookDamage`; DETECTED_ATTACK:
  no memory of the attacker → `0x56` (0x269), last seen at least 1.0 s ago → `0x56` (0x261); then
  the weapon word `& 0x18000` → slot 604, else slot 605, both given the word; non-zero returns;
  else the Troika body.
- `0x103872d0` **CNPC_VHumanCombatant** (sel 0x15), `0x103dd6b0` **CNPC_VYukie** (sel 0x2a): combat →
  clear the flag, the weapon split; else the parent.
- `0x10387d20` **CNPC_VHumanCombatPatrol** (sel 0x16): combat, not busy, not seeing (or occluded),
  a patrol node → its schedule as it stands (0x121); then the weapon split; alert, not busy, a
  node → its schedule (0x116); else the combatant.
- `0x1037bd60` **CNPC_VGhoulCroucher** (no sel): not disturbed → `0x158`; not exited → `0x159`;
  else the combatant.
- `0x1037d130` **CNPC_VGuard1** (sel 0x12): DO_STARTLED → `0xf1` (no stamp); state 0xc: Int(0x1e)
  and a live closest player → `0x1017e6f0(player, 0)`, `0x6d`; else `SetState(1)`; the human.
- `0x10371ee0` **CNPC_VCop** (sel 0xc): idle and from a spawner: with no patrol node, or a player
  that is not in heightened alert and has no cops in pursuit → the budget: the `+0x6672` claim
  byte clear and `[0x1093acac] - [0x1093acb0] <= 3` → `0x170`; else claim it, `++[0x1093acb0]`,
  `0x16e`; state 0xc as Guard1; else the combatant.
- `0x103a29f0` **CNPC_VPedestrian** (sel 0x1d): first think → `0xfe`; PASS_OUT not busy → `0xfa`;
  DO_STARTLED → `0xf1`; idle/alert INVESTIGATE_SOUND → clock, `CommitBestSound`; the SQUARED
  distance to `m_BestSound`'s origin at least 256.0, `SKIPPED_SOUND` clear and `RandomInt < 0x19`
  → set it, `0x158`; else clear it, set INITIAL_FLEE, `0x157`; else the human.
- `0x10360eb0` **CNPC_VAsianVampire** (sel 6): a closest player not hated → the human;
  `asianvamp_force_jump_up` → `0x15a`; path not blocked: not stationary too long and not standing
  on the player → the human; blocked in melee → slot 601 on the enemy, `0x15c`; else
  `GetJumpSchedule`.
- `0x1036b250` **CNPC_VChangBros** (sel 10): a closest player not hated → the human; the three force
  ConVars `0x15e`/`0x15a`/`0x15c`; conditions `0x7c` → `0x15e`, `0x79` → `0x15c`, `0x7a` →
  `0x15a`, ENEMY_UNREACHABLE → `0x15d`; else the human.
- `0x103788d0` **CNPC_VGargoyle** (sel 0x11): the stat-list dead test → `0x15c`; combat: a
  reachable enemy with a path → the human; else `0x10378f80` (pillar found → FINDING_BODY,
  `0x15a`; else `+0x6680 = 2`); else the human.
- `0x1037fca0` **CNPC_VHengeyokai** (sel 0x13): state 5 → `0x16f`; combat: ENEMY_DEAD while
  carrying → `0x15e`; finding a body: clear it; Int(0x51) or Int(0x5f)/Int(8) → release the pickup
  target, re-arm the collision ignore at 0, `0x10382f60`; else `0x10382d40` or `0x15f`; carrying:
  timer expired and reachable → `0x161`, occluded or unseen → `0x166`, too close → `0x15e`, no throw
  LOS → `0x166`, the one fake throw (`RandomInt < 5`) → `0x164`/`0x162`, else `0x163`/`0x160`;
  otherwise clear PRESERVE_PATH, `0x10382d40`; unreachable → `0x167`; occluded → `0x165`;
  TOO_FAR_FOR_MELEE → SEE_ENEMY ? `0x169` : `0x165`; else the human.
- `0x10394120` **CNPC_VMingXiao::PreSelectSchedule** (sel 0x19): state 5 → `0x157`; else
  `Weapon_Switch(ranged weapon or NULL, 0)`, 0.
- `0x103941e0` **CNPC_VMingXiao::SelectSchedule** (sel 0x19): idle/alert → `0x44`; combat: the
  grabbed-object arm; tentacle conditions `0x77+i` → `0x158+i`; spit → `0x15e`; occluded →
  `0x160`; `0x10396bc0`; a proxy → `0x162`; `ming_xiao_charge` and enemy nearer than 120: ready →
  re-arm from the tuning record, `0x15f`; MELEE_HELPLESS → now, then the tentacle gate 2/3/0/1 →
  `0x15a`/`0x15b`/`0x158`/`0x159`; else `0x165`; other states the Troika body.
- `0x103aa510` **CNPC_VSabbatLeader::PreSelectSchedule** (sel 0x1f): combat with no enemy → the two
  state words written 1 directly (not `SetState`); the Troika pre-selector.
- `0x103a70c0` **CNPC_VSabbatLeader::SelectSchedule** (sel 0x1f): state 5 → `0x158`;
  `andrei_force_player_collision` → reset to 0, SetAbsOrigin(player + 20 x); `CheckStuck`; no roars
  → refill 3, `0x165`; activated: force jump → `0x15b`, force charge → `0x166`, TIME_TO_JUMP → not
  after a nova `RandomInt(0,2)` 0 → `0x164`, 1 → `0x15b`, 2 → `0x166`; after a nova
  `RandomInt(0,1)` 0 → `0x164`, else `0x15b`; else the human.
- `0x103ae8c0` **CNPC_VSheriffMan** (sel 0x21): activated: `CategorizeHeights`; state 5 → `0x158`;
  alive: `sheriff_force_teleport > 0` → reset, `0x15a`; player low and self high → `0x15c`; player
  high and self low → `0x15d`; health lost since the record `< 0.05` and not idle 3.0 s → the human,
  else `0x15a`; dead → `KillSheriff`; the human.
- `0x103bb7c0` **CNPC_VTzimisce** (sel 0x26): DO_STARTLED → `0x187`; `RandomInt(0,99) < 0x32` →
  slot 627; combat: drop/throw arms `0x198`/`0x195` while carrying, first NEW_ENEMY → 5; not
  finding a body: carrying ladder (`0x193`, `0x169`, `0x198`, `0x169`, fake throw
  `0x196`/`0x194`, `0x195`/`0x192`); else clear PRESERVE_PATH, `0x103bc4e0`, unreachable → the hint
  pick (`0x17c`/`0x17e`/`0x17f`/`0x180`/`0x16a`), occluded `0x167`, reachable: pounce `0x186`,
  TOO_FAR `0x168`/`0x167`; the running-program continuations `0x103bca20`/`0x103bcaf0`/
  `0x103bcb60`/`0x103bcc00`; else `0x170`; finding a body: clear it, the melee-attack pick on
  Int(0x51) or Int(0x5f)/Int(8), else `0x103bc4e0` or `0x170`; alert: the two Troika helpers; hunt
  (0xb): carrying `0x198`, SEE_UNKNOWN `0x15d`, sound `0x15c`, the roll (`0x15f`/`0x160`/`0x161`),
  MADE_HUNT_PATH `0x15b`, no enemy `0x15a`, the hint pick, `0x159`/`0x15a`; else the Troika body.
- `0x103c1610` **CNPC_VTzimisceHeadClaw** (no sel): combat: SHOULD_CHARGE → `0x158`; ranged word:
  slow running or `RandomInt(0,100) < 0x28` → `0xca`, else slot 605; melee word: slot 604.
- `0x103c3310` **CNPC_VTzimisceRunner** (no sel): combat: slot 604; a live potential enemy discards
  the answer unless it is 199/200/`0xe4`/`0xe7`/`0x156` and the enemy is beyond 256 in 2-D (then
  `0x157`); else the Troika body.
- `0x103cee70` **CNPC_VWerewolf** (sel 0x29): slot 461 first; `werewolf_force_teleport` →
  `ClearMoveHint`, `0x157`; not viewable → `0x158`; states 0/1 → enemy ? `0x157` : `0x156`; 2, 3,
  8, 10, 0xe → no enemy `0x156`, dead enemy `0x157`, CAN_TELEPORT `TeleportOut` `0x158`,
  SHOULD_BREAKHINT without hint flag `0x100` → `0x15e`, TOO_CLOSE → `0xd3`, frustration → `0x15d`,
  special move with no melee and unreachable → the move hint's schedule, unreachable → `0x157`; 7
  → not alive `0x162`, DEATH_TRIGGERED `0x161`; 9 → teleport, random move hints, `0x157`; 0xb →
  death, teleport, break-hint, then special move / random hint; else the Troika body.
- `0x10375d90` **CNPC_VFrenzyShadow**, `0x103dceb0` **CNPC_VWolfMorph**, `0x103a46b0`
  **CNPC_VPlayerController::PreSelectSchedule** landed in story 5 fold A2 (their class files);
  their tail into `CNPC_VHuman` is an integrator redirect.

**Unrecovered:** the Tzimisce body search (`0x103be180`) and pounce hull probe (`0x103bf660`), the
Hengeyokai fish search (`0x10381cd0`), the Gargoyle pillar search (`0x100f7b20`) and navigator path
test (`0x102ee380`), slot 627's fidget voice, `m_fSequencePastHalf`, and the Werewolf
random-move-hint pair (family Werewolf19's rows) — each a named seam answering retail's "nothing".

### The selector pair wired live, and the integration's corrections — `0x1028a260`, `0x102b4fb0`, `0x10396bc0`

_Recovered 2026-09-27, 0019 story 8 pass I (lane L06)._

Wiring, read off the listing:

- `FElysiumNpc::SelectSchedule` is `0x1028a260` (`SelectNewScheduleRetail`): the maintenance pass now
  asks slot 437 before slot 438, so every species pre-selector runs. The port's guessed selector
  (dead guard, species-first composition, the law branch at the outermost entry) and its
  `SelectIdleSchedule` / `SelectAlertSchedule` / `SelectCombatSchedule` and
  `ElysiumNpcWitness::SelectLawSchedule` are deleted.
- Slot 438 on the bare Troika line (`FElysiumNpc::SpeciesSelectSchedule`) is `0x102af660`.
- `0x10375d90` FrenzyShadow and `0x103dceb0` WolfMorph tail-jump `0x10015ad2` = `JMP 0x10384ee0`
  (`CNPC_VHuman`), not the base. FrenzyShadow writes sel 0xf first (`0x10375d99`).
- `0x103a46b0` writes sel 2 at `0x103a46b6`, before the state test: both arms.
- The player-side bump no longer runs the NPC's `0x101e3df0` sweep; the NPC's own
  `0x102ae920` `WAS_BUMPED` arm (`0x102ae9ec` / `0x102ae9f4`) does, at its next selection.
- The witnessed-incident consumers are retail's: idle + COND 0x1f/0x21 → state 8 through
  `0x102ad660`, then `0x102af660` case 8 submits and answers FLEE_AND_COWER_TURN_TO_PLAYER(_NEAR)
  `0x70`/`0x71`; COND 0x20/0x22 are consumed only by `0x102ae920` in COMBAT (mask-admitted), with
  slot 596 then slot 597 `(offender, 5)`.

Corrections made while reading bodies against the listing:

- `0x102b4fb0` slot 597 is `AddEntityRelationship 0x10332ca0` (overwrite at any priority), not the
  refusing `SetEntity`.
- `0x10396bc0` (Ming Xiao throw search): `0x10396c4d CALL 0x10014df8` is `HasCondition(9)` with the
  answer discarded, not a clear.
- `0x101e8da0(0x10739d08)` is `LEA EAX,[ECX+0x2bc]`, the `Ming_Xiao_Info/General` slice of the
  Rules.txt feat list: `+0x8` ThrowChance (60), `+0xc` / `+0x10` ChargeResetTimeNormal /
  ChargeResetTimeDesperate (10.0, 10.0).
- `0x102b8980` answers a schedule id (`0x4c`, `0x4d`, `0x51`/`0x52`), not a grade letter.
- `0x10382d40` (Hengeyokai fish) and `0x103bc4e0` (Tzimisce body): the found arm is now ported
  whole. The enemy's last known position (`0x102dfed0`) is compared against the target. When it is
  nearer, the pickup target is released, `0x102c43b0(0)` is called and the helper answers 0.
- `0x103bc820`: the near test is `AND 0x4100` on the x87-extended distance (NaN takes the near
  answer).
- `0x10384ee0`: at or past `seen + 1.0` → `0x56`; unordered keeps selecting.
- `0x10378ec0`: with no enemy it answers false without writing `+0x667c`.
- `0x103df2e0`: `CreateCorpse` is called directly (`0x103df349 CALL 0x10007c7a`).

**Unrecovered (integration):** `SelectWeightedSequence` still has no sequence index at the kernel
tier (`ElysiumAnimatingOverlaySlotBodies.cpp`), so every `SMALL_FLINCH`/`ALERT_SMALL_FLINCH`/
`ALERT_SCAN` arm gated on it (`0x1028a48a`, `0x1028a6d1`, `0x1028a4ff`, `0x1028a615`, `0x102afcdb`)
is unreachable. The `+0x6590` patrol path object is unstood, so the idle patrol arm never answers.
An idle `use_interesting` or patrolling NPC still runs the port's ambient/patrol executor ahead of
selection (`FElysiumNpc::Think` -> `ThinkAmbient`), so case 1's interesting-place answers
(`0xff`..`0x106`) are reached only when selection is — L13's loop wiring.

## Story 8, family Script19, the Troika's movement helpers — `0x1029f460`, `0x1038b1a0`, `0x10278220`, `0x102800c0`, `0x102aa640` (2026-09-27)

_Recovered 2026-09-27, 0019 story 8 pass I: lane L10, integrated as `576a06fc`._

The movement half of family Script19: the patrol-path builder and its moves, the hidden-position
probe, the two scheduled moves the `aiscripted_schedule` director issues, and the ManBat's flight
override. Every one reaches `SetGoal` `0x102ecd20` (§ "`CAI_Navigator::SetGoal` `0x102ecd20` (one body), with `0x102f1dc0` and `0x102f28a0`" above). The
director bodies of the same family are in `authored-control.md` § "Story 8, family Script19, the
script directors". Port: `ElysiumNpcBaseScript.cpp`, `ElysiumNpcScript.cpp`,
`ElysiumNpcScriptSpecies.cpp`; tests `Elysium.Substrate.NpcKernelScript19.*`.

### `0x1029f460` BuildPatrolPath (277 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L10)._

Args `(cell, repeat, type, schedule, nodeIds, bReplace)`; the cell is `m_sppPatrolPath +0x658c` or
`m_sppPatrolPathHunt +0x6594` (owned byte `+0`, `CAI_PatrolPath*` `+4`).

1. `m_NPCState == 7` → return (`0x1029f46b`); null cell → return (`0x1029f477`).
2. `bReplace` clear (`0x1029f486`): an existing path skips straight to step 5 (`0x1029f4d1`); an empty
   cell allocates (`0x10307d30`, `0x1029f4d3`), writes the owned byte (`0x1029f4dd`), resets
   (`0x10307aa0`: `type := -1, schedule := 0, count := 0`), FORCES `type := 0` and `repeat := 0`
   (`0x1029f4ea`). A dry pool leaves a null path that the reset then faults on.
3. `bReplace` set: allocate when empty (`0x1029f48c`, owned byte `0x1029f494`); still null →
   `DevMsg("Failed to create patrol path for %s\n")` and return (`0x1029f49c..0x1029f4b6`); else reset
   and `repeat := arg` (`0x1029f4c9`).
4. `type := arg` (`0x10307b40`, `0x1029f4f4`).
5. Append every id to the `-1` terminator (`0x10307bf0`, `0x1029f500..0x1029f51b`).
6. `current := min(count - 1, DAT_1049df28[type])` (`0x10307b60` / `0x10307c20`); the type table
   `DAT_1049df20` rows are `{id, name, start, step, next}` = `{0,"0",0,1,0}`, `{1,"1",0x7fff,-1,1}`,
   `{2,"2",0,1,3}`, `{3,"3",0x7fff,-1,2}` — types 1 and 3 start at the last node.
7. Non-zero `schedule` → `path+4 := schedule` (`0x1029f531`).
8. `path+4` non-zero → `0x1029f650(this, this+0x658c)` (always the PATROL cell, `0x1029f547`), stamp
   `AI_BaseNPCTroika.cpp:0x27a1` (`0x1029f54c` / `0x1029f556`), `SetSchedule 0x102ae750(path+4, 0)`
   (`0x1029f56b`).

The pool (`0x10307d30`): 32 slots of `0x114` bytes at `0x10934158`, in-use bytes `DAT_109363d8`,
cursor `DAT_109363f8`; allocation scans from the cursor and bumps the CURSOR by one; a cursor above
`0x1f` is `Error("Patrol path pool is dry.  It will store up to %d paths.  Change
PATROL_PATH_POOL_MAX_PATHS to increase this amount.\n", 0x20)`. `0x10307db0` frees a slot and lowers the
cursor to it; `0x10307d00` resets it. **Unrecovered:** the network node ids the inputs pass
(`0x102d2900`) — the port stands a node id as the patrol hint's entity index.

### `0x1038b1a0` ManBatOverrideMoveFly (366 bytes) and `0x1038b120` OverrideMove (96 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L10)._

`0x1038b120`: navigator type (`0x1027d990`) 2 → `0x1038b1a0(interval)`, TRUE (`0x1038b12b..0x1038b13c`);
otherwise `0x1042fbf0(ladder(+0x6670)) == 0x1042fbf0(0xfa0b0699)` (`0x1038b13f..0x1038b179`).

`0x1038b1a0`:
1. `cvar_manbat_stun` (`0x1093b858`, "manbat_stun", default "0"): not a command and `m_nValue` set
   (`0x1038b1a6..0x1038b1c1`) → stamp `NPC_VManBat.cpp:0x1ad`, `0x102ae750(0x15b, 0)`, `ConVar::SetValue(0)`,
   return (`0x1038b1cc..0x1038b1f6`).
2. Interval above 1.0 → 1.0 (`0x1038b1f9..0x1038b20c`; NaN is kept).
3. `0x1038b370(this, &vel, interval)` (`0x1038b220`), `SetAbsVelocity(vel)` (`0x1038b240`).
4. `m_Activity (+0xfec)` in `{0x30, 0xb0, 0x4b, 0x1171}` → return (`0x1038b24e..0x1038b26d`).
5. `0x102e1c10(motor, VecToYaw(vel), -1.0)` (`0x1038b283` / `0x1038b28d`): the `+0x28` flip, the
   `+0x1c == 180` direct write else `0x102e0a80`; the rate `-1.0` equals `_DAT_104492dc` and takes
   `0x102e1cf0` (motor `+0x38 := MaxYawSpeed()`) — it does NOT leave the rate alone — then `0x102e1e20(-1)`.
   `0x102e1cf0` is the same call on the same `m_pMotor (+0x5d44)` that `SetActivityAndSequence 0x10272490`
   ends in (`0x10272569` / `0x10272575`); the port counts both on one seam (`NavigatorActivityNotices`).
6. `GetAngles` with pitch `:= VecToPitch(vel)` (`0x101d2ce0`) through `SetAngles` (`0x1038b296..0x1038b2ca`).
7. `m_flFlapTimer (+0x6678) <= curtime` (`0x1038b2d6..0x1038b2e4`) → `0x1038e720(vel)` (`0x1038b301`).

`0x1038e720` (the selector): refuses activities `0x28, 0x30, 0xb0, 0x4b, 0x1171`; `vel.z >= 30.0`
(`_DAT_104492a8` f32 — the VELOCITY's z, not the interval) → `0x1038e640` (act `0x22`, 2.3 s); else
`turn = VecToYaw(vel) - angles.yaw` (+360 when negative); `!(turn < 30) && !(turn > 330)` — an unordered
turn stays in the band (`0x1038e7cb` leaves on C0 alone, `0x1038e7da` on C0=C3=0) — (f64 cells `0x1044dcf0`,
`0x104bc6a0`) → `0x1038e6a0` (act `0x116d`, 0.2 s) below 180 (`0x10452918`) else `0x1038e6e0` (`0x116e`,
0.2 s); otherwise activity `0x24` → `0x1038e640`, anything else `0x1038e670` (`0x24`, 4.0 s).
`UTIL_VecToYaw 0x101d2c70` answers 0 for a vertical vector and wraps into `[0, 360)`; `UTIL_VecToPitch
0x101d2ce0` answers 180 (z < 0) / -180 (else) for a vertical vector, else `atan2(-z, len2d)`.
**Unrecovered:** the motor `+0x1c` word (the port's `MotorIdealYaw` takes the direct write).

### `0x10278220` TryMoveToHiddenPosition (505 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L10)._

1. Trace `threat → candidate + m_vecViewOffset (+0x184..0x18c)`, mask `0x2804091`, filter
   `CTraceFilterSimpleTwoEnt(this, param_3)` (`0x10278239..0x102782b8`); the `0x10738960` overlay is a
   developer line.
2. Fraction == 1.0 → FALSE (`0x102782f1..0x10278303`: `TEST AH,0x44 / JNP` jumps on C3 alone).
3. Slot 548 `IsValidCover(candidate, NULL)` false → FALSE (`0x10278310` / `0x10278318`).
4. `0x102e6d70` on `m_pMoveProbe` from slot 220 to the candidate, mask `0x202400b`, 100.0; non-zero
   `fStatus` → FALSE (`0x10278359` / `0x10278365`).
5. Goal `{type 4, candidate, activity 0x13, tolerance -1.0 (_DAT_104994a0), flags 0, target
   DAT_1090fdb4}` through `SetGoal(goal, 1)`; its `AL` is the answer (`0x102783ca..0x102783fd`). Flag 1
   (`0x102ecd74`) runs `0x102f28a0` first, which zeroes navigator `+0x40..+0x4c` and, through `0x1030bb30`,
   the path's tolerance `+0x28`: the -1.0 "keep" word therefore always resolves to the hull width here.

`SetGoal 0x102ecd20` writes the path's tolerance (`CAI_Path +0x28`, `0x102ecec7`), never
`m_flGoalTolerance (+0x6320)`. A refused route (`0x102f1dc0`) with navigator `+0x40
m_timePathRebuildMax == 0.0` (`0x102f1ee8`) calls `OnNavFailed(0xc)` (`0x102f1f00` → `0x102eeae0`):
`TaskFail(0xc)` inside `SetGoal`, whatever the caller then does with the FALSE. `+0x40` is written only by
`TASK_SET_ROUTE_SEARCH_TIME` (`0x102886f0`, from `StartTask 0x102827f0`) and zeroed by `0x102f28a0`; the
navigator constructor `0x102eca50` does not write it.

**Unrecovered:** `DAT_1090fdb4`'s identity (a null handle in practice).

### `0x102800c0` ScheduledMoveToGoalEntity / `0x102801e0` ScheduledFollowPath (213 bytes each)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L10)._

`0x10280de0(schedule)`; `m_pGoalEnt (+0x5de8) := goal`; goal `{type 4 (0x102800ec; 0x102801e0: type 3
GOALTYPE_PATHCORNER, 0x10280205), goal->slot 217 origin (0x102801e0: slot 220), [4..7] -1 except [5] := param_3 = the MOVEMENT ACTIVITY (9 / 0x13 / 0x22 from
0x101a98c0) — not a goal type, tolerance 128.0 (0x102801e0: -1.0), flags 1, target DAT_10923a2c}`; slot 563
`(goal, &dest, &tolerance, &param_1)`; `SetGoal(goal, 0)`, whose `AL` is returned (the caller
`0x101a98c0` tests it). **Unrecovered:** `DAT_10923a2c` (zero-initialised; a null handle).

### `0x102aa640` / `0x102aa860` IssuePatrolMove (432 / 299 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L10)._

`0x102aa640` (StartTask `0x7a..0x7e` on `+0x658c` / `+0x6594`): no cell or no path → `+0x1b48 = 0x3d39`,
`TaskFail(0x1d)`; node `nodes[current]` == -1 → `0x3d65`, `0x1d`; outside `network (+0x5d34)->+0x2c`'s
count → `++DAT_106c994c`; out of range or a null node → `0x3d60`, `0x1d`; else goal `{4, GetPosition(node,
m_eHull), activity -1, tolerance -1.0, flags 0}` through `SetGoal(goal, 2)`: TRUE → `TaskComplete(0)`, FALSE
→ `DevWarning(2, "%s can't reach patrol point\n")`, `0x3d55`, `TaskFail(0xc)`.

`0x102aa860` (RunTask `0x7a` / `0x7b`): no path → `0x3d73`, `TaskFail(0x1d)`; node -1 → return silently;
out of range → `++DAT_106c994c` and a NULL node into `GetPosition` (a retail fault; the port returns);
goal `{4, position, -1, NAI_Hull::Width(m_eHull), 0}` through `SetGoal(goal, 0)`, answer ignored — but a
refused route still fails the task inside `SetGoal` (`OnNavFailed(0xc)`, above).
**Unrecovered:** nothing on this path since 0018 story 4 (2026-09-29): the node id is the hint's
`m_nNodeID` from the node-row counter and the position is `GetPosition 0x102fb0d0` over the
place set, where the port had stood a patrol hint's entity index.

## Story 8, family RunAi19 — slot 432 `RunAI`: base `0x1026f110`, Troika `0x1028fcc0`, `RunAlternateAI` `0x1028fd80`, the species overrides (2026-09-28)

_Recovered 2026-09-28, 0019 story 8 pass I: lane L13a, integrated with the wave-2 rewire (lane L13)._

Each section is one `rule` row of `families-19-29/RunAi19-READING.md`, walked arm by arm off the
listing (`vtmb_asm`) with the decompiled C as a second reader. Port: `ElysiumNpcBaseRunAi.cpp`
(`CAI_BaseNPC`), `ElysiumNpcRunAi.cpp` (`CAI_BaseNPCTroika`), `ElysiumNpcRunAiSpecies.cpp` (the
species slot-432 overrides). Every scope-trace push/pop (`g_ScopeTraceStack`, the `"NULL ENTITY"` name
pick) and every VProf scope / RDTSC bracket in these bodies is absent by convention: none has an
observable.

Retail's think chain around this family: `NPCThink 0x10292de0` asks `RunAlternateAI(bReduced)` and runs
slot 432 `RunAI(bReduced)` only when it answers false; `bReduced` is `!IsThinkDue` of the AI clock.
The thirteen-byte species slot-432 bodies (`CGeneric_NPC`, `CNPC_VAnimal` `0x10360160`,
`CNPC_VChangBros` `0x10385a10`, `CNPC_VGhoulCroucher` `0x10387500`, …, verdict `dead`) are bare
forwards to `CAI_BaseNPCTroika::RunAI 0x1028fcc0` and are not separate port bodies: the species classes
inherit `FElysiumNpc::RunAI`. Since the wave-2 rewire (story 8, lane L13) this is the live loop: the
entity think is slot 431 `NPCThink`, which reaches slot 432 and through it slot 433 `GatherConditions`
and `MaintainSchedule 0x102817c0`; the port's former per-tick router is gone.

### `0x1026f110` `CAI_BaseNPC::RunAI(bool bReduced)` (756 bytes, slot 432)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13a)._

1. `m_bConditionsGathered (+0x5ca4) = 0` (`0x1026f1ad`). The port's form of the byte is
   `Cognition.GatheredAt` (non-negative = gathered this pass, L07); the clear is the negative.
2. The debug overlay: `DAT_1070af4c` is the `developer` ConVar — `[+4]` its parent, slot 1
   `IsCommand` false and `m_nValue (+0x2c) != 0` (`0x1026f1be`..`0x1026f1cb`) — AND the navigator's
   `m_bNotOnNetwork` (`m_pNavigator +0x34`, `0x1026f1d3`) → `AddTimedOverlay("NPC w/no reachable
   nodes!", 5)` (`0x1026f1d8`..`0x1026f1e1`). Shipped `developer` is 0: dead in retail.
3. Only when `bReduced` is false (`0x1026f1ea`) AND `m_hDialogPartner (+0x0fe8)` fails the three-part
   EHANDLE test (`0x1026f1f9` / `0x1026f215` / `0x1026f219`): slot 433 `GatherConditions`
   (`0x1026f237`), then `if (!m_bConditionsGathered) m_bConditionsGathered = 1` (`0x1026f243` /
   `0x1026f245`) for an override that did not call the base. Both refusals are a plain skip: the
   standing condition word is not touched.
4. `0x1026ab50`, the shrunk-hull head probe, on every pass (`0x1026f29a`).
5. Slot 434 `PrescheduleThink` (`0x1026f2b6`), bracketed by RDTSC into `0x1090fe68/6c`.
6. `MaintainSchedule 0x102817c0(this, bReduced)` (`0x1026f302`).
7. Full passes only (`0x1026f30b`): `ClearCondition 0x10269b50` of `LIGHT_DAMAGE 0x4c`
   (`0x1026f311`), `HEAVY_DAMAGE 0x4d` (`0x1026f31a`), `WAS_BUMPED 0x38` (`0x1026f323`), in that
   order. This is the one-pass life of the damage and bump bits: raised by the damage transaction
   and the touch handler between passes, visible to exactly one full `MaintainSchedule`.
8. `m_bRanAI (+0x1b4c) = 1` (`0x1026f32c`), on every pass.

Reads `+0x5ca4`, `+0x5d34`→`+0x34`, `+0x0fe8`, the `developer` ConVar; writes `+0x5ca4`, `+0x1b4c`
and (through `0x10269b50`) the condition word.

**Unrecovered:** the navigator's `m_bNotOnNetwork` has no producer in the port
(`Conditions19NavNotOnNetwork` answers false), so the overlay arm cannot open; `AddTimedOverlay` is a
recording seam (no debug-overlay service).

### `0x1028fcc0` `CAI_BaseNPCTroika::RunAI(bool bReduced)` (141 bytes, slot 432)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13a)._

Three calls whose order is the whole body: `0x1028fc90` (`0x1028fd2c`), `UpdatePedestrianInfo
0x102a0d20` (`0x1028fd31`; ECX is still `this` — `0x1028fc90` does not touch it), then
`CAI_BaseNPC::RunAI 0x1026f110(bReduced)` (`0x1028fd3d`). Filled for `CAI_BaseNPCTroika`, `CNPCMaker`
and its two subclasses, `CNPC_VBaseBoss`, `CNPC_VNewscaster`, `CNPC_VPlaceholder`,
`CNPC_VTzimisceRunner`, `CNPC_VWerewolf` and `CPayphone`.

`0x1028fc90` (11 instructions, no verdict row of its own): `m_flStealthHearingDist (+0x63cc) = 0`,
`m_flStealthVisionScalar (+0x63c4) = 1.0`, `m_flStealthVisionCone (+0x63c8) = 1.0`
(`0x1028fc95`..`0x1028fca5`). A discipline's stealth modifier therefore lives exactly one AI pass.
`UpdatePedestrianInfo` runs before the gather because it clears `SHOULD_INTERACT 0x10`,
`CROSSWALK_WALK 0x12` and `CROSSWALK_DONTWALK 0x13` on every crosswalk-path pass.

Note: `CNPCMaker::Spawn` (`0x1034b9d0`) calls `0x1028fb70` then `0x1028fc90` on the MAKER; the port's
`RecomputePerceptionDistances` seam (`ElysiumNpcSocial10.cpp`) names `0x1028fc90` as a perception
recompute, which it is not — it is this stealth reset.

**Unrecovered:** none.

### `0x1028fd80` `CAI_BaseNPCTroika::RunAlternateAI(bool bReduced)` (337 bytes, `RET 4`)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13a)._

The argument is `NPCThink`'s `bReduced` byte; each arm pushes it on and none reads it.

1. Head arm: `m_GrapplePartner (+0x1538)` live by the three-part EHANDLE test (`0x1028fdf2` /
   `0x1028fe0f` / `0x1028fe14`) AND `m_GrappleRole (+0x153c) == 1` — the victim — (`0x1028fe1f`):
   `m_IdealActivity (+0x0ff0) - 0xf88` unsigned `<= 0xb1` (`0x1028fe31`) indexes the byte table
   `0x1028fedc`; entries `0xf88 0xf91 0xf9a 0xfb7 0xfc0 0xff7 0x1000 0x1009 0x1039` call
   `CAI_BaseNPC::AutoMovement 0x10280a50` (`0x1028fe42`, plain `RET`, no argument). TRUE either way
   (`0x1028fe52`).
2. Otherwise `m_eAlternateAI (+0x644c)` unsigned `> 4` answers FALSE (`0x1028fe60`); the table at
   `0x1028ff90`: 0 → FALSE (`0x1028fec1`); 1 → `0x10290040` (the door approach, `0x1028fe6e`); 2 →
   `0x10290200` (`0x1028fe84`); 3 → `0x102902e0` (`0x1028fe9a`); 4 → `0x10290350` (`0x1028feb0`);
   each arm's answer is returned.

`0x10290200` (mode 2, the door opened; no verdict row of its own): `MaintainActivity 0x102727d0`
(`0x10290203`); `curtime < m_flAlternateAIExpireTimer (+0x6450)` → TRUE (`0x1029021d`); else a live
`m_hOpeningDoor (+0x5d24)` (`0x1029022c`..`0x1029024e`) → slot 448 `TaskFail(0xe)` (`0x10290256`),
`+0x5d24 = -1`, `m_bOpeningDoorWait (+0x5d30) = 0`, `+0x644c = 0`, TRUE; a gone door → when the
path is PAUSED (`0x102ee2e0` answers the byte `path+0x10`, `0x10290283`; corrected 2026-09-30, it
was read as "the goal is set") `0x102bf7e0` (`0x1029028e`), then slot 528
`ValidateNavGoal` (`0x10290297`), `+0x644c = 0`, TRUE — the wait flag is left as it was.

`0x102902e0` (mode 3, the door blocked): `MaintainActivity` (`0x102902e3`); at or after the expiry
(`0x102902fd`) `TaskFail(0xe)` and the same three clears (`0x10290305`..`0x1029031d`); TRUE.

**Unrecovered:** none in the body. `AutoMovement` has no observable in a headless world (the
interval-movement seam answers no delta), so the nine-activity split is carried but not witnessed.

### `0x1039e3d0` `CNPC_VMingXiaoTentacle::RunAI` (820 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13a)._

Three blocks before `CAI_BaseNPCTroika::RunAI` (`0x1039e633`), inside a scope trace and the VProf node
`CNPC_VMingXiaoTentacle_RunAI` (both absent).

1. The evade re-plan. `FLD curtime; FCOMP m_flUpdateEvadeTimer (+0x667c); AND 0x4100; JNZ`: only a
   curtime STRICTLY past the timer runs it (`0x1039e4a6`). The timer is re-armed FIRST, to curtime +
   `RandomFloat(1.0, 2.0)` (`0x1039e4be` / `0x1039e4cf`), then: the navigator's goal active
   (`0x102ee6a0`, `0x1039e4d5` / `0x1039e4dc`) and its goal type (`0x102ee620`) == 4
   (`GOALTYPE_LOCATION`, `0x1039e4f0`). A null slot 167 `GetEnemy` (`0x1039e4fa`) fails the task:
   `+0x1b48 = 0x4ba`, reason 6. With an enemy, `FLD m_flEnemyDist (+0x6268); FCOMP 256.0; TEST AH,0x41;
   JP` continues only AT OR BELOW 256 (`0x1039e523`); `GetEnemy` is asked again (`0x1039e529`) and
   `0x102edae0(navigator, &enemy->GetAbsOrigin(), 512.0, 30000.0, &out)` (`0x1039e53c`..`0x1039e551`;
   slot 217 takes no argument, the pushes are `0x102edae0`'s) — a refusal fails with line `0x4c6`,
   reason 7; a node then `0x102ee220(navigator, &out)` (`0x1039e573`), whose false answer fails with
   line `0x4d0`, reason `0xc`; true falls through. Every failing arm stamps `+0x1b44` =
   `E:\Vampire\main\dlls\hl2_dll\NPC_VMingXiaoTentacle.cpp` (`0x1039e58c`) and calls slot 448.
2. The collision release: `m_bIgnoreCollision (+0x6688)` set (`0x1039e5a4`) and NOT `curtime <
   m_flIgnoreCollisionTimer (+0x6684)` (`0x1039e5bc`, an equal stamp has expired): the flag is CLEARED
   first (`0x1039e5cb`), then `IsAreaClear(GetAbsOrigin(), 0x202400b, 0, 0)` (`0x1039e5db`); a blocked
   area sets the flag again and the timer to curtime + 1.0 (`0x1039e5f2` / `0x1039e5f9`).
3. The landing sounds: `m_bHitGroundSound (+0x6698)` clear and slot 209 `GetGroundEntity` non-null
   (`0x1039e607` / `0x1039e615`): `0x1039f030` (`tentacle_hit_ground.wav`, `CHAN_BODY`), `0x1039f1a0`
   (`tentacle_flopping_loop.wav`, `CHAN_VOICE`; the loop `0x1039f310` stops at death), then the flag.

`0x102ee220` is `m_pPath->SetGoalPos` (`0x1030b950`) then the route build `0x102f1dc0(this, 0, 0)`,
returning its byte; on a refused build `0x102f1dc0` itself fails the task (`OnNavFailed(0xc, 1)`), so
retail fails twice on that arm.

**Unrecovered:** the port's navigator keeps no goal type (`NavGoalState` answers -1), so arm 1 stops
after the re-arm; the node search `0x102edae0` is a seam answering false; `0x102ee220`'s answer is a
seam answering true. The species shape map lists `+0x667c`, `+0x6684`, `+0x6698` ABSENT.

### `0x103bdef0` `CNPC_VTzimisce` slot 432 (80 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13a)._

The decompiled `this == 1` is the stack word `PUSH ECX` reserves; `0x103c0160(this, &kind)`
(`0x103bdef9`) answers a blocker or null. Null → the base (`0x103bdf2f`). Otherwise kind 1 →
`0x103c0860(blocker)` (`0x103bdf11`), any other → `0x103c05e0(blocker)` (`0x103bdf2a`); both then
`CAI_BaseNPCTroika::RunAI` (`0x103bdf1d` / `0x103bdf36`).

`0x103c0160` (909 bytes, `RET 4`): start = slot 220 `GetOrigin` + (0, 0, 4.0 `_DAT_10450aa0`)
(`0x103c0189`); end = start + `AngleVectors(slot 221)` forward × `tzimisce_obstruction_lookahead`
(ConVar `0x1093cd28`, "24"); `m_Collision` mins/maxs; `CTraceFilterSimpleTwoEnt(this,
GetIgnoreCollisionEntity(), m_CollisionGroup +0x368)`; `TraceRay` mask `0x202400b` (`0x103c0425`).
`fraction == 1.0` → null (`0x103c0475`); the world entity (`0x1023bd00`) → null (`0x103c049e`); slot 69
`NavIgnoreCollision(hit)` false → null (`0x103c04ba`); else `*kind = 0`, answer the hit
(`0x103c04d2`). **It only ever writes 0: the kind-1 arm `0x103c0860` is dead in retail.**

`0x103c0860` (74 bytes, blocker unused): slot 62 `SetOrigin(GetOrigin() + (0, 0, slot 522
StepHeight()))`. `0x103c05e0` (501 bytes): null blocker or no `m_pPhysicsObject (+0x36c)` → return
(`0x103c05f1` / `0x103c05ff`); v = this body's slot 199 velocity rotated −90° `(v.y, −v.x, v.z)`; a
world-only line from the blocker's origin along v (`0x103c06d5`) that hits flips v on all three
components (`0x103c0708`..`0x103c0717`); v × `tzimisce_obstruction_scalar` ("5"); v.z +=
`tzimisce_obstruction_z` ("75"); `IPhysicsObject` slot 41 `AddVelocity(&v, &zero)` (`0x103c07c4`).

**Unrecovered:** the kernel hull trace is a seam answering clear, so no blocker reaches the port;
`m_CollisionGroup`, the world entity and the blocker's physics object are seams; slot 41's name is
inferred (`CBaseCombatWeapon` drop `0x10252a90`).

### `0x103c1d20` `CNPC_VTzimisceHeadClaw` slot 432 (26 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13a)._

`0x103c2230(this, false)` (`0x103c1d25`) — the slowed-grab teardown, which acts only while
`m_flSlowedExpire (+0x6678)` is above 0.0 and has reached curtime — then `CAI_BaseNPCTroika::RunAI`
directly (`0x103c1d31`, thunk `0x1000704f`, no ChangBros forwarder).

**Unrecovered:** none.

### `0x1035e980` `CNPC_VAndreiBlood` slot 432 (171 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13a)._

`m_bActivated (+0x66cc)` clear (`0x1035e98b`): `m_NPCState (+0x5cc0) = 1`, `m_IdealNPCState (+0x5cc4)
= 1` written directly — no `SetState`, no `OnStateChange` (`0x1035e98d` / `0x1035e997`); a live
`m_hClosestPlayer (+0x628c)` (`0x1035e9aa`..`0x1035e9cd`) whose slot 404 `IRelationType` is not 4
(`0x1035e9dd`) gets `AddClassRelationship(1, 4, 10)` (`0x1035e9e7`); `m_bfNPCStateFlags (+0x5b64) &=
~4` (`0x1035e9ec`..`0x1035e9fb`); `0x10385a10` (`0x1035ea01`). Set: both words 2 (`0x1035ea16` /
`0x1035ea1c`), `0x10385a10` (`0x1035ea22`).

**Unrecovered:** the port derives `+0x5b64` from the state; the clear of bit 2 is carried by IDLE's
derived byte (`0x31`), retail's other stale bits of the previous byte are not.

### `0x10361110` `CNPC_VAsianVampire::RunAI` (111 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13a)._

`UpdateMovedTimeStamp 0x10362540` (`0x10361163`, bare `RET`: no argument) then `0x10385a10`
(`0x1036116f`). The stamp is what `StationaryForTooLong 0x10362670` in `SelectSchedule 0x10360eb0`
reads.

**Unrecovered:** none.

### `0x10363b60` `CNPC_VBach` slot 432 (186 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13a)._

`m_bCanFightYet (+0x66a8)` clear → `SetEnemy(NULL)` (`0x10363b6b` / `0x10363b6f`), every pass. Then
`m_bShieldActive (+0x66a5)` set and `m_flShieldTime (+0x6684)` STRICTLY before curtime (`0x10363b97`,
`TEST AH,0x41`): the stat list whose `+0x10` tag is 3 (`0x10363ba5`..`0x10363bbd`), else the lazily
constructed global `0x109f0b40` (`0x10363bc9`..`0x10363be4`), `CVStatList_t::SetBase(0xd, 0)`
(`0x10363bf7`), `m_bShieldActive = 0` (`0x10363bfd`). Then `0x10385a10` (`0x10363c0c`). The raise is
`GatherAttackConditions 0x10363db0` (`SetBase(0xd, 5)`, 6.0 s).

**Unrecovered:** none.

### `0x103747e0` `CNPC_VDog` slot 432 (164 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13a)._

`m_Activity (+0x0fec)` 1 and `m_NPCState` 1 (`0x103747ec` / `0x103747f4`): `RandomInt(0, 0xff)` through
the import `[0x109f3868]` (`0x103747fd`) EQUAL to 0x80 → slot 310 `SetActivity(3)` (`0x10374813`);
`0x10360160` (`0x10374820`). `0x6e` (ACT_SNARL) with `m_bSequenceFinished (+0x065c)` →
`SetActivity(1)` and `0x10360160` (`0x10374843` / `0x10374850`). 3 (ACT_FIDGET) or `0x40` (ACT_SIT)
with the byte → `SetActivity(1)` (`0x1037486e`). Every path ends in `0x10360160`.

**Unrecovered:** none.

### `0x10378b80` `CNPC_VGargoyle` slot 432 (80 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13a)._

The Tzimisce shape with `0x103796a0` / `0x10379e80` / `0x10379b40` and the `gargoyle_obstruction_*`
ConVars (`0x1093af60` "24", `0x1093afa8` "5", `0x1093b050` "75"). The sweep adds a re-trace with both
ends raised by slot 522 `StepHeight` (18.0): clear, the world, or a hit slot 69 refuses → kind 1
(`0x103799fe`..`0x10379a0e`, `0x10379a40`), else kind 0 (`0x10379a24`); the FIRST trace's entity is
answered, so kind 1 steps up even when the raised trace hits world geometry. `0x10379b40`: null →
return (`0x10379b54`); `FClassnameIs(blocker, "prop_dynamic")` (inline, case-insensitive,
`0x10379b5a`..`0x10379bcd`) → the prop's slot 266 with the gargoyle (`CBreakableProp::Break`
`0x1018fb90`, `0x10379bd4`) and return; else the physics push (`0x10379be6`..`0x10379db5`). Then
`0x10385a10`.

**Unrecovered:** as Tzimisce; the prop break is `FElysiumProp::InputBreak` (the prop's damage, gibs
and explosion are not stood).

### `0x10380120` `CNPC_VHengeyokai` slot 432 (111 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13a)._

The Gargoyle twin (`0x10380fc0` / `0x103816e0` / `0x10381460`, `hengeyokai_obstruction_*` at
`0x1093b238` / `0x1093b298` / `0x1093b1f0`), then `0x10385a10` (`0x10380157`), then the one-shot console
trigger: `hengeyokai_stun` (`0x1093b2e0`, "0") `GetInt() != 0` (`0x10380164` slot 1 IsCommand,
`0x10380175` `m_nValue`) → `0x103830e0` (the morph: schedule `0x16e`, skin fade 0.0, `FadeToSkin(1)`,
`0x10380179`) then `ConVar::SetValue(0)` (`0x10246160`, `0x10380185`).

**Unrecovered:** none.

### `0x1038e990` `CNPC_VManBat` slot 432 (26 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13a)._

`0x1038f020(this, false)` (`0x1038e995`), then `0x10385a10` (`0x1038e9a1`). `Event_Killed 0x1038e8c0`
is the force-true caller.

**Unrecovered:** none.

### `0x103a3670` `CNPC_VPedestrian` slot 432 (257 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13a)._

`0x10385a10` FIRST (`0x103a367b`). Then `m_bfAINPCFlags (+0x14b8) & 0x80` (`IN_FLEE_SCHED`,
`0x103a368f`) and curtime NOT below `m_flNextFleeSoundTime (+0x641c)` (`0x103a36ab`): `+0x641c =
curtime + 10.0` (`0x103a36c7`); `AngleVectors(slot 221)` forward (`0x103a36d4`); point = slot 220
origin − forward × 256.0 (`0x103a36dd`..`0x103a3729`); `CSoundEnt::InsertSound(8 SOUND_DANGER, &point,
DAT_1072bc8c, 10.0, DAT_1072bcc3, this)` (`0x103a3762`). The two cells are `sound_volume_table` row 27
`NPC_DISCIPLINE_ALERT` (radius, occlusion byte).

**Unrecovered:** none.

### `0x103a75c0` `CNPC_VSabbatLeader::RunAI` (111 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13a)._

`0x10385a10` (`0x103a7618`) then `UpdateBloodSplash 0x103aa960` (`0x103a761f`, no argument).

**Unrecovered:** none.

### `0x103aebd0` `CNPC_VSheriffMan::RunAI` (121 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13a)._

`m_bFloorHeightsCached (+0x66e7)` clear (`0x103aec29`) → `CacheFloorHeights 0x103b1510`
(`0x103aec2d`); then `0x10385a10` (`0x103aec39`).

**Unrecovered:** none.

### `0x103df850` `CNPC_VZombie` slot 432 (364 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13a)._

1. `m_iZombieAIType (+0x6678) == 1` (`0x103df85a`) and `m_flPlayerDist (+0x6264)` STRICTLY above
   `m_flRemoveDist (+0x66dc)` (`0x103df86f`): `DevMsg("zombie %s: player too far away, killing self
   (%.1f > %.1f)...\n", GetDebugName(), dist, removeDist)` (`0x103df892`), slot 119 `Kill`
   (`0x103df89f`). Nothing returns: the pass continues.
2. `0x10360160` (`0x103df8ac`).
3. Type 1 or 0 (`0x103df8ba` / `0x103df8be`), `m_Activity (+0x0fec) != 0x4b` (`0x103df8cb`),
   `HasCondition(0x46 SEE_ENEMY)` (`0x103df8dc`), `LungeDistanceMin (+0x250, 98.0) <= dist`
   (`0x103df8f7`, `TEST AH,0x41; JP`), NOT `LungeDistanceMax (+0x254, 160.0) < dist` (`0x103df914`),
   NOT `curtime < m_flGrappleReadyTimer (+0x66d8)` (`0x103df930`), `m_hClosestPlayer` live
   (`0x103df93b`..`0x103df95d`) with a player record (`+0xa8`, `0x103df967`), and `Percent (+0x264, 2)
   > RandomInt(0, 99)` (`0x103df985` / `0x103df988`): `0x102ae750(0x16a, 0)` (`0x103df993`) and
   `m_flGrappleReadyTimer = curtime + DelayBetween (+0x280, 20.0)` (`0x103df9ab`).
4. Every exit: `m_bGroundSpeedFromIntervalMovement (+0x05ac) = 1` (`0x103df9b1`).

The packet's verdict line put `m_Activity` at `+0x628c`; the listing reads `+0x0fec`.

**Unrecovered:** none.

## Story 8, family Think19 — slot 431 `NPCThink`: base `0x1026ca80`, Troika `0x10292de0`, slot 312 `UpdateCharacter` `0x10298070`, the species overrides (2026-09-28)

_Recovered 2026-09-28, 0019 story 8 pass I: lane L13b (with Damaged19's `0x103cb590` and the review of
`0x103a4700`), integrated with the wave-2 rewire (lane L13)._

Each section is one `rule` row of `families-19-29/Think19-READING.md`, walked arm by arm off the
listing. Port: `ElysiumNpcBaseThink.cpp` (`CAI_BaseNPC`), `ElysiumNpcThink.cpp`
(`CAI_BaseNPCTroika`), `ElysiumNpcThinkSpecies.cpp`, `ElysiumNpcDamagedSpecies.cpp` (Werewolf),
`ElysiumNpcFrenzyShadow.cpp` / `ElysiumNpcPlayerController.cpp` (landed earlier, reviewed here). The
think cadence these bodies sit in is `lifecycle.md` § "The think cadence".

Every body below opens with the scope-trace push (`g_ScopeTraceStack`, `m_iName` or `"NULL ENTITY"`)
and closes with its pop, and most carry a VProf node; both are debugger bookkeeping the port does
not carry (story 29b: the ring and the trace stack are absent words). They are not repeated per
section.

Port notes:

- **Where the bodies live.** `FElysiumNpc::NPCThink` calls `Think19NormalSet1` / `Think19NormalSet2` /
  `Think19Tail` in retail order. Slot 312 is `UpdateCharacterRetail(float)`, and the AI console gate
  `0x1026c3d0` is `FElysiumNpcBase::Think19AiConsoleGate`. Since the wave-2 rewire (lane L13) slot 431
  IS the entity think: `FElysiumNpc::Think` dispatches it, and the movement step takes
  `RunAnimation 0x1026c540`'s interval (`PerformMovement(RunAnimation(), …)`).
- **Seams** (each answers the admitting value or counts):
  - `DAT_1093408c` / `g_pAINetworkManager+0x658`: both answer true.
  - `CacheInterruptConditions` on a base-only body.
  - `0x102bfe10` under `debug_track_under_ground` "0", and `0x1029bd40`: debug-only.
  - `CBaseCombatCharacter::UpdateCharacter 0x103246d0`: its pieces run on their own owners.
  - `0x100fb980`, the emitter's direct rate setter.
  - The werewolf's `EnableDebugStuff 0x103dbad0` and hint overlay `0x103cb4b0`.
  - The engine tick.
- **Named divergences:**
  - The werewolf round robin divides the world clock's whole-frame count, not an engine tick.
  - The boss registry's slot-1 over-read writes the unbound handle.
  - Retail's raw 0 handle is the unbound handle.
  - A NULL obfuscation candidate in `0x103e0a00` answers false.
  - A negative round-robin remainder dispatches nothing.
- **Retail defects reproduced:**
  - A hint with no cover object and no enemy reaches the 0x2e/0x48 tests.
  - `UTIL_Remove` does not end the think.
  - The zombie resets its clocks on every think whose `m_flNextThink` truncates to `<= 0`.
  - `Scream_Death`'s `-1` is cached for the process.

### `0x10292de0` `CAI_BaseNPCTroika::NPCThink()` (2552 bytes, slot 431)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13b)._

44 classes fill slot 431 with this body. Order:

1. `NPCThinkDebugPre` `0x10292500` (`0x10292e4d`), before anything else.
2. `m_bfAINPCFlags2 &= 0x7ffffffb` (`0x10292e5e`/`0x10292e66`): **both** bit `0x4`
   (`SCHEDULE_CHANGED`) and bit `0x80000000` are cleared every think, before the disable test.
3. `m_bDisableAI` (`+0x6080`) set: return (`0x10292e6c`). `m_flNextThink` is not written.
4. `bNormal = IsThinkDue(m_flNextNormalThink)` `0x102906c0` (`0x10292e8e`). With it, **Set1**:
   - `m_flPlayerDist = SetClosestPlayer()` `0x10293a80` (`0x10292ef4`/`0x10292efd`).
   - slot 167 `GetEnemy()` (`0x10292f03`); null or `enemy->m_lifeState (+0x200) != 0`: the three
     words `+0x6268/+0x626c/+0x6270` = `0x469c4000` = **20000.0** (`0x10293059`..`0x1029306a`).
     Otherwise `m_flEnemyDist = |myOrigin - enemyOrigin|` (`0x10292fb0`), `m_flEnemyHeightDiff =
     |my.z - enemy.z|` (`0x10292fbf` `FABS`, `0x10292fc1`), and `m_flEnemyLastKnownDist = |myOrigin -
     lastKnown|` where `lastKnown` is slot 541 `GetEnemies()` → `0x102dfed0` (`0x10293003`); then,
     **inside the enemy arm only**, when the ConVar `*0x10924f74` is not a command (slot 1 false) and
     its int `+0x2c` is non-zero and `flags2 & 0x400`: slot 517 `AddFacingTarget(enemy, &lastKnown,
     1.0, 0.8, 0.0)` (`0x10293051`).
   - slot 221 `GetAngles()` → `AngleVectors` `0x10139610` into `m_vecForward +0x6290` and
     `m_vecRight +0x629c`, up NULL (`0x10293084`/`0x1029308b`).
   - `SetPlayerLOS` `0x10291610` (`0x10293095`); `CacheInterruptConditions` `0x1026a0f0` (`0x1029309c`).
   - `m_bfAINPCFlags & 0x4000`: `AutoMovement` `0x10280a50` (`0x102930b7`).
   - `m_pHintNode` (`+0x5ddc`) set: `m_flOccludedDelay = m_flOccludedDelayCover` (`0x102930d1`), then
     slot 566 `FValidateHintType(m_pHintNode)` (`0x102930db`); false → clear arm. True and
     `m_bStayEntrenched` (`+0x6435`) → keep. Else resolve `m_hHintCoverObject` (`+0x6448`, `-1` or
     stale → NULL) and compare with slot 167 `GetEnemy()` (`0x1029312b`): different → keep; **equal —
     which includes "no cover object and no enemy"** → `HasCondition(0x2e)` (`0x10293133`) → clear
     arm, else `HasCondition(0x48)` (`0x10293140`) → clear arm, else keep. The clear arm is
     `ClearHintNode(5.0)` `0x10295ab0` (`0x10293150`), the `*0x10924a6c` slot-1 read with its answer
     discarded (`0x1029315d`), `SetCondition(0x29)` (`0x10293164`). No hint node:
     `m_flOccludedDelay = m_flOccludedDelayNormal` (`0x10293171`).
   - `m_pShootAtHint` (`+0x6444`) set: slot 566 on it (`0x10293186`); false →
     `m_hShootTargetOverride = -1`, `m_pShootAtHint = NULL` (`0x102931b5`/`0x102931bf`); true →
     `m_hShootTargetOverride = *hint->slot1()` (the hint's own EHANDLE, `0x102931a1`).
   - `m_bfNPCFrenziedFlags & 0x8000` and `RandomInt(0, 99) < 1` (`0x102931ed`/`0x102931f3`): the
     `"Scream_Death"` sound index, searched once for the whole process in the VSound concept table
     (`__strcmpi`, `0x10293231`) and cached in `0x109246c0` behind guard bit `0x10923dd7 & 1`, `-1`
     cached forever when absent; then the VSound play `0x101f5950(this, index, 2, 1.0, 1.25)`
     (`0x1029326c`).
5. `updateInterval = curtime - m_flLastUpdateThink` (`0x10293288`), `bUpdate =
   IsThinkDue(m_flNextUpdateThink)` `0x102906a0` (`0x10293292`), `normalInterval = curtime -
   m_flLastNormalThink` (`0x1029329a`) — all three on EVERY think.
6. With `bNormal`, **Set2**: `ResolveStandingOnHead(normalInterval)` `0x102bf820` (`0x1029330c`);
   `flags2 & 0x4000` clear → `0x102bfdf0(normalInterval)`, an empty `RET 4` (`0x10293321`); ConVar
   `*0x10924d24` gate → `0x102bfe10` the ground check (`0x10293344`); `0x102bf310` the `move_yaw`
   pose (`0x1029334c`); `flags2 & 0x20000000` (`DISAPPEAR`): closest player (`+0x628c`, NULL when
   stale) fails the PVS test `0x101d1a90` or `FVisible(player, 0x2804091)` → `UTIL_Remove(this)`
   `0x101cd940` (`0x102933f1`) — **and the think continues**: `UTIL_Remove` is deferred.
   Then the AI gate `0x1026c3d0` (`0x102933fb`). Refused: `m_flNextThink = curtime + 0.1f`
   (`0x104491b4`) unless `DAT_1093408c` (`0x1029341f`), and **return without the tail** (`0x10293447`
   → `0x102937c6`). Accepted: `bMove = 0x102906e0()` (constant 1), `bAI = IsThinkDue(m_flNextAIThink)`
   `0x10290700`; `RunAlternateAI(!bAI)` (`0x1029356e`) and, when it answers false, slot 432
   `RunAI(!bAI)` (`0x1029357c`); `PostRun()` (`0x10293584`); `PerformMovement(interval, !bMove = 0)`
   (`0x1029359e`); `CalcNextMoveThink` (`0x10293632`); `CalcNextAIThink` (`0x1029363e`).
7. Tail: `bUpdate` → slot 312 `UpdateCharacter(updateInterval)` (`0x1029365b`), and within that arm
   `m_bIsTalking` (`+0x64c0`) with `0x102c0aa0` false → `FinishTalking` `0x102c0ca0` (`0x10293678`).
   `CalcNextUpdateThink` (`0x10293684`), `CalcNextNormalThink` (`0x10293690`), `m_flNextThink =
   (NextUpdate <= NextNormal or unordered) ? NextUpdate : NextNormal` (`0x102936a1`..`0x102936c0`),
   overwritten with `curtime + 0.01f` (`0x10450aa4`) while `m_bJumping` (`+0x6498`, `0x102936d8`);
   then the debug overlay `0x1029bd40` (`0x102936e0`).

**Unrecovered:** the names of flags2 `0x80000000`, conditions `0x2e`/`0x48`/`0x29`; the three ConVars
`0x10924f74`, `0x10924d24`, `0x10924a6c`; `0x102bfdf0`'s purpose (empty); what `0x1093408c` is (the
node-graph flag by its other readers).

### `0x1026ca80` `CAI_BaseNPC::NPCThink()` (643 bytes, slot 431)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13b)._

Filled by `CAI_BaseNPC`, `CAI_BaseHumanoid`, `CAI_ExpressiveNPC`, `CAI_TestHull`, the four Cine
classes, `CGenericNPC`, `CGenericSabbat_NPC`. `m_bDumpDebugBuffer` (`+0x5b55`) set → clear it and dump
the ring `0x1027efb0` (`0x1026cafd`/`0x1026cb03`). `CacheInterruptConditions` (`0x1026cb0a`).
`m_flNextThink = curtime + 0.1` (the DOUBLE at `0x104493d0`, `0x1026cb1d`) unconditionally, before
every gate. `g_pAINetworkManager` (`0x10934088`) null or its `+0x658` ready byte clear → return
(`0x1026cb2a`/`0x1026cb36`). The AI gate `0x1026c3d0` refused → return (`0x1026cb92`). Accepted:
`m_hGrapplePartner` (`+0x1538`) resolving AND `m_GrappleRole` (`+0x153c`) `== 1` skips slot 432
(`0x1026cc23`); otherwise `RunAI(false)` (`0x1026cc2a`). `PostRun()` (`0x1026cc32`) and
`PerformMovement(interval, false)` (`0x1026cc43`) run on both.

**Unrecovered:** the network manager's `+0x658` name (ready/built).

### `0x10298070` `CAI_BaseNPCTroika::UpdateCharacter(float)` (574 bytes, slot 312)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13b)._

`m_bIsBossMonster` (`+0x6496`): when not yet registered (`+0x6497 == 0`), the global count
`DAT_10924fb8` (a signed BYTE) `< 2` and slot 464 `GetState() == 2` → the table
`DAT_109247e0[count++] = GetRefEHandle()`, `+0x6497 = 1` (`0x1029812b`/`0x1029812d`); a body still a
boss then skips to the tail (`0x1029813c`). Not a boss but registered (`0x1029814a`): both slots are
walked (`0x1029817a`..`0x1029821f`); an entry whose entity resolves (`0x100290c0`) and is not `this`
is appended to a temporary vector as that entity's own handle (`-1` when it no longer resolves on the
second lookup); `DAT_109247e0 = DAT_109247e4 = 0`, `count = survivors` (`0x1029822b`..`0x10298239`);
when `count != 0` BOTH slots are copied back from the vector (`0x1029824d`..`0x1029825a`) — slot 1
reads past the vector's count when one survived (a retail over-read); `+0x6497 = 0`
(`0x10298260`). Tail, every path: `CBaseCombatCharacter::UpdateCharacter(dt)` `0x103246d0`
(`0x1029829a`). `CWorld::vfunc113` `0x1023bc20` resets the table to `-1, -1` and the count to 0 on
world activate; the one reader is `CBasePlayer::UpdateClientActionState` `0x101755d0`.

**Unrecovered:** what the player reads the registry for (the boss bar); the vector growth policy
behind `0x102c6c30` (whether the slot-1 over-read sees 0 or heap).

### `0x10369120` `CNPC_VCamera::NPCThink()` (210 bytes; also `CNPC_VCameraSecurity`)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13b)._

Replaces the Troika body outright. `+0x5b55` → clear and dump (`0x1036912d`/`0x10369134`);
`0x1029f2e0` (`m_bDisableAI`) true → return with no stamp (`0x10369142`); `CacheInterruptConditions`
(`0x1036914a`); the AI gate `0x1026c3d0` refused → `DAT_1093408c` set: return; clear:
`m_flNextThink = curtime + 0.1f` (`0x10369175`), return. Accepted: slot 432 `RunAI(false)`
(`0x10369183`); the four `Next*` stamps copied into the four `Last*` (`0x1036919b`..`0x103691b3`);
`m_flNextThink` and all four `Next*` = `curtime + 0.2` (the DOUBLE at `0x10449198`,
`0x103691c8`..`0x103691ea`). No `SCHEDULE_CHANGED` clear, no senses pass, no `PostRun`/movement.

**Unrecovered:** none.

### `0x1037b3f0` `CNPC_VGhoulCroucher::NPCThink()` (202 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13b)._

The Troika body first (`0x1037b3f4`). Then `+0x6665` (the SECOND spawn byte, which `Spawn` names
disturbed) and `m_hBurningParticle` (`+0x6670`) resolving to a live object: `rate = 1.0 -
0x101beed0(m_flPlayerDist, 200.0, 600.0)` where `0x101beed0` is the clamped fraction (`> max → 1`,
`< min → 0`), and `rate < 0.1` → `0.1f` (`0x3dcccccd`, `0x1037b46c`; equality keeps, the packet's
"at most" is loose); `0x100fb980(rate)` on the particle (`0x1037b4a3`): a negative rate warns and
becomes 0, the emitter's `+0x48c` = rate, times the cvar `DAT_107083dc` when its `+0x4a1`, and `+0x494`
= the engine tick. The second handle test (`0x1037b47d`) cannot fail after the first passed (one
thread); its NULL-receiver arm (`0x1037b4b2`) is dead.

**Unrecovered:** none.

### `0x10394990` `CNPC_VMingXiao::NPCThink()` (926 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13b)._

All species work runs BEFORE the Troika body. `switch (m_eThrowableObjectMode +0x673c)` through the
table `0x10394d30` (mode `> 4` unsigned → default): modes 1 and 2 → `flags2 &= 0x7ffffbff`
(`0x10394aa2`) and slot 518 `AddFacingTarget(GetAbsOrigin() + m_vecPickupSavedForward (+0x672c),
1.0, 0.5, 0.0)` (`0x10394b03`); every other mode → `flags2 |= 0x80000400` (`0x10394b0b`).
`RandomInt(0, 99) < 10` (`0x10394b21`/`0x10394b27`): slots 217 and 219 read and discarded, then
`0x102c42a0` seven times — `"Ming_xiao_slimetrail_emitter"` at `"Bip01 TailRoot"` and
`"Ming_xiao_slimetrail_emitter2"` at `"Bip01 Tail1"`..`"Bip01 Tail6"` (`0x10394b50`..`0x10394bc8`).
`0x10398870` false (not possessing): the six regrow timers `m_rflRegrowTimers[i]` at `+0x66f4`,
index order; a timer with `m_flPrevAnimTime (+0x170) > timer` (ordered, strict: `0x10394bf0`) clears
bit `i` of `m_iSeveredTentacleMask +0x6710` (`0x10394c05`), becomes `FLT_MAX` (`0x10394c0b`),
increments `m_iConnectedTentacleCount +0x670c` (`0x10394c1b`), runs `0x10398800` (`0x10394c21`) and
stores `0x103986b0()` in `m_flIdealRange +0x6748` (`0x10394c2d`). `0x10398870` false again →
`CoordinateTroops` (`0x10394c49`). `PushPhysicsObjects` (`0x10394c50`), `0x102c43f0`
(`0x10394c57`), then the Troika body (`0x10394c5e`).

**Correction to the verdict:** the timers are at `+0x66f4`, the count at `+0x670c`, the mask at
`+0x6710` and the pickup forward at `+0x672c` (listing `0x10394bda`, `0x10394c12`, `0x10394bfb`,
`0x10394ab0`); the verdict's `+0x670c` timers / `+0x6748` count are wrong.

**Unrecovered:** none.

### `0x103a05b0` `CNPC_VNewscaster::NPCThink()` (136 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13b)._

`m_flNextMoveThink = m_flNextAIThink = curtime + 1.0` (`0x103a05c1`/`0x103a05d6`), `m_flLastMoveThink
= m_flLastAIThink = curtime` (`0x103a05e5`/`0x103a05f6`); the Troika body (`0x103a05fc`); the story
advance `0x103a0670` (`0x103a0603`); the ConVar `*0x1093be8c` not a command, `+0x2c` non-zero, and
`m_debugOverlays (+0x224) & 1` CLEAR → the text dump `0x103a0ff0(0)` (`0x103a0631`).

**Unrecovered:** none.

### `0x103b9040` `CNPC_VTzimisce::NPCThink()` (16 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13b)._

`0x102c43f0` (`0x103b9043`), then a tail jump to the Troika body (`0x103b904b`).

**Unrecovered:** none.

### `0x103c6000` `CNPC_VVampireBoss::NPCThink()` (114 bytes; also `CNPC_VSabbatLeader`)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13b)._

The Troika body (`0x103c6050`), then `m_flNextThink = curtime + 0.1f` (`0x104ce8b8`, `0x103c6063`).

**Unrecovered:** none.

### `0x103dfa20` `CNPC_VZombie::NPCThink()` (197 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13b)._

`0x1029f2e0` (`m_bDisableAI`) true → return (`0x103dfa90`) — no base body, no slot 614.
`m_iZombieAIType (+0x6678) == 1` → `0x103e0a00` (`0x103dfa9d`). `m_flSeekDistInspection (+0x63b8) <=
1.0` (ordered; `JP` skips NaN, `0x103dfab3`) → `InitPerceptionDistances` `0x1028fb70`
(`0x103dfab7`). The Troika body (`0x103dfabe`). `__ftol(m_flNextThink) <= 0` → slot 614
(`0x103dfad0`/`0x103dfad6`).

**Unrecovered:** none.

### `0x1035db20` `CNPC_VAndreiBlood::NPCThink()` (90 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13b)._

`CNPC_VVampireBoss::NPCThink` (`0x1035db6c`) and nothing else: Andrei keeps the boss's 0.1 s.

**Unrecovered:** none.

### `0x10361490` `CNPC_VAsianVampire::NPCThink()` (114 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13b)._

`CNPC_VVampireBoss::NPCThink` (`0x103614e0`), then `m_flNextThink = curtime + 0.1f` (`0x104a9304`,
`0x103614f3`).

**Unrecovered:** none.

### `0x1036c6c0` `CNPC_VChangBros::NPCThink()` (114 bytes; also Blade and Claw)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13b)._

`CNPC_VVampireBoss::NPCThink` (`0x1036c710`), then `m_flNextThink = curtime + 0.1f` (`0x104ad9f0`,
`0x1036c723`).

**Unrecovered:** none.

### `0x103af830` `CNPC_VSheriffMan::NPCThink()` (114 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13b)._

`CNPC_VVampireBoss::NPCThink` (`0x103af880`), then `m_flNextThink = curtime + 0.1f` (`0x104c6120`,
`0x103af893`).

**Unrecovered:** none.

### `0x10375e50` `CNPC_VFrenzyShadow::NPCThink()` (83 bytes) — reviewed, landed earlier

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13b)._

`CNPC_VPlayerController::NPCThink` `0x103a4700` DIRECT (`0x10375e54`); slot 167 (`0x10375e5d`);
`m_NPCState == 2` (`0x10375e6e`), enemy non-null (`0x10375e72`), `HasCondition(0x46)` false
(`0x10375e7f`) → slot 544 `UpdateEnemyMemory(enemy, enemy->GetAbsOrigin(), enemy + 0x3d4)`
(`0x10375e99`). The landed body matches arm for arm.

**Unrecovered:** none.

### `0x103a4700` `CNPC_VPlayerController::NPCThink()` (19 bytes; also `CNPC_VWolfMorph`) — reviewed

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13b)._

`CALL 0x10002a7c` (the Troika body, `0x103a4703`), then `JMP [EAX+0x998]` slot 614
`ResetThinkTimers` as a tail call (`0x103a470d`). The landed body matches.

**Unrecovered:** none.

### `0x103cb590` `CNPC_VWerewolf::NPCThink()` (404 bytes)

_Recovered 2026-09-28, 0019 story 8 pass I (lane L13b)._

1. ConVar `*0x1093f73c` not a command and `+0x2c` non-zero → `EnableDebugStuff` `0x103dbad0(this)`
   (cdecl, `0x103cb616`); falls through.
2. ConVar `*0x1093f95c` the same → the hint overlay `0x103cb4b0` (`0x103cb63b`), slot 614
   (`0x103cb644`), RETURN (`0x103cb652`).
3. `+0x66a0 == 0` or `+0x6720 == 0` → `InitializeHintData` `0x103d7710` (`0x103cb669`), `0x103cade0`
   (`0x103cb670`), `+0x66a0 = 1` (`0x103cb675`).
4. `g_AIDisabled` (`0x1092053c`) bit 0 SET → straight to slot 614 (`0x103cb683` → `0x103cb718`):
   **the Troika body does not run at all** while AI is disabled.
5. `(+0x66e8 & 0x20) == 0x20` → `TeleportOut` `0x103d4a60` (`0x103cb698`).
6. slot 167 non-null (`0x103cb6a9`) → `engine->slot 120 (tick) % 5` (signed `IDIV`, `0x103cb6bf`)
   through the table `0x103cb754`: 0 `UpdateConditionCanTeleport` (`0x103cb6c8`), 1
   `UpdateConditionEnemyUnreachable` (`0x103cb72b`), 2 `UpdateConditionDeathTriggered`
   (`0x103cb734`), 3 `UpdateConditionCanSpecialMove` (`0x103cb73d`), 4 `CheckStuck(1)`
   (`0x103cb746`); every arm rejoins `0x103cb6cf`.
7. slot 167 non-null again (`0x103cb6db`) → slot 600 with it (`0x103cb6eb`).
8. The Troika body (`0x103cb6f4`).
9. `m_fIsUsingSmallHull (+0x5f2d)` and slot 163 `IsViewable()` → `UpdateFakeHull` `0x103d93b0`
   (`0x103cb713`).
10. slot 614 (`0x103cb71c`).

A negative tick would index before the table (retail hazard; not reachable with a monotonic tick).

**Unrecovered:** the two ConVar names (`0x1093f73c`, `0x1093f95c`).
