# NPC AI — Authored control: outputs, scripts, keyfields

Part of the [NPC AI oracle](./README.md). Sections moved verbatim from
`npc-ai-reverse-engineering.md` on 2026-09-13; their headers are the citation keys.

## What fires when aggression begins

### Native incident chain

The following is the proven general chain. Individual classes and schedules specialize the middle
of it, and not every incident emits every output.

1. **A stimulus exists.** An actor is seen or heard, the NPC takes damage, combat noise reaches its
   senses, a player conduct threshold is crossed, an entity input changes relation, or a script
   assigns behavior/enemy state.
2. **Relationship and eligibility are evaluated.** Exact entity relation precedes class relation;
   capabilities, flags, perception parameters, and class policy decide which observations matter.
3. **Memory and conditions change.** Enemy/last-enemy, last-seen category, last-heard, damage,
   occlusion, range, and attack-capability conditions are updated.
4. **Observation outputs may fire.** The map can receive `OnFoundPlayer`, `OnFoundEnemy`,
   `OnHearCombat`, or `OnDamaged`. `OnFoundPlayer` and `OnFoundEnemy` are distinct surfaces and
   maps use both.
5. **State is reconsidered.** A typical escalation is idle to alert to combat, but fear, scripted
   ownership, prone state, `no_alert_state`, and class policy can produce a different route.
6. **The current schedule may be interrupted.** Interruption occurs only if the new condition is
   in that schedule's mask and is not currently delayed or suppressed.
7. **The class selects a schedule.** Possibilities include investigate, start combat, pursue,
   cover, flank, attack, flee, cower, or a specialized class response.
8. **Tasks execute over time.** The NPC faces or paths, waits for movement, equips, chooses an
   activity, attacks, remembers state, or transfers to another schedule.
9. **Animation is resolved.** Activity passes through NPC and weapon translation before the model
   chooses a weighted sequence.
10. **Consequences propagate.** Combat sounds alert other NPCs; damage, incapacitation, feeding,
    dialogue, and death outputs can drive I/O and Python; scripts may change relationship or
    schedule again.

This chain explains why four tempting shortcuts are wrong:

- `OnDamaged` is an authored output, not the entire native damage response.
- `SetRelationship D_HT` changes a relationship row; it does not directly name the next animation.
- entering combat state does not itself encode pathing, attack choice, or cover behavior.
- a Python quest callback is a consequence/controller layered over the native loop, not proof that
  Python performs sensing and combat selection.

### Losing a target

The base NPC exposes separate outputs for losing line of sight and losing the actor entirely:
`OnLostEnemyLOS`, `OnLostEnemy`, `OnLostPlayerLOS`, and `OnLostPlayer`. The native object also
retains last enemy and last-seen/heard state. Search, investigate, chase, and occlusion schedules
can therefore continue after direct visibility ends. A rebuild must not collapse "not currently
visible" into "forgotten and neutral." The player-specific debounce, including its ten failed
committed-enemy LOS checks, is in [stealth.md](../stealth.md).

## Authored NPC outputs

### Native output surface

The recovered base `CAI_BaseNPC` datamap exposes 16 exact outputs:

```text
OnDamaged          OnDeath             OnHalfHealth
OnFoundEnemy       OnLostEnemyLOS      OnLostEnemy
OnFoundPlayer      OnLostPlayerLOS     OnLostPlayer
OnHearWorld        OnHearPlayer        OnHearCombat
OnGrappleBegin     OnGrappleEnd        OnFedUponBegin
OnFedUponEnd
```

Derived VtMB classes add surfaces used by maps, including dialogue and incapacitation events.

### Output census

NPC and maker rows contain 829 raw output rows. Of those, 242 have a blank exported event name and
are retained as raw evidence but excluded from the named-event behavior census. The remaining 587
named rows are:

| Event | Rows |
|---|---:|
| `OnDeath` | 152 |
| `OnFoundPlayer` | 142 |
| `OnDialogEnd` | 89 |
| `OnSpawnNPC` | 62 |
| `OnDamaged` | 47 |
| `OnHearCombat` | 21 |
| `OnDialogBegin` | 19 |
| `OnFedUponBegin` | 10 |
| `OnIncapacitatedStart` | 10 |
| `OnLostPlayer` | 6 |
| `OnNPCDied` | 6 |
| `OnIncapacitatedEnd` | 6 |
| `OnFoundEnemy` | 5 |
| `OnFedUponEnd` | 5 |
| `OnSellWeapon` | 2 |
| `OnHearPlayer` | 2 |
| `OnLastNPCDied` | 1 |
| `OnUnknownVisionPlayer` | 1 |
| `OnTransformComplete` | 1 |

The largest named-output maps are `sm_warehouse_1` (211), `la_hub_1` (103), `sm_hub_1` (62),
`sp_tutorial_1` (53), `sm_medical_1` (40), `sm_diner_1` (29), and `sm_pier_1` (19). This is not a
measure of AI complexity by itself; it measures how much authored map logic is attached to the
NPC event surface.

### Concrete aggression wiring

The authored examples show that native aggression and story scripting deliberately interleave:

- **Arthur, `sm_bailbonds_1`:** `OnDamaged` sends himself `SetRelationship` with
  `player D_FR 5`; `OnDeath` calls `kilpatrickDeath()`. The injury changes how Arthur relates to
  the player, while death advances authored state.
- **Bertram, `sm_hub_1`:** `OnDamaged` disables talking, sets `player D_HT 5`, and applies scripted
  Potence level 3. Damage simultaneously closes dialogue, establishes hostility, and turns on a
  combat power.
- **Igor, `la_hub_1`:** both `OnDamaged` and `OnHearCombat` trigger `logic_IgorHate`. His death
  updates Python/global-counter state. Either direct injury or nearby combat can enter the
  encounter's hostile path.
- **Diner:** assassins fire `go_attack` on `OnFoundPlayer`; civilians begin a cower sequence on
  `OnHearCombat`; civilian deaths make the Killer hate the player; and damage makes the Killer
  himself hate the player. Sensing, civilian reaction, and blame are separately authored.
- **Warehouse:** `OnFoundPlayer`, `OnHearCombat`, and `OnDamaged` terminate conversations, record
  stealth failure in `G.Warehouse_Spotted`, and drive solidify/unhide relays. Native detection is
  the source event; Python/I/O decides the mission consequence and encounter staging.
- **Theatre:** guards use `OnFoundEnemy`, rather than `OnFoundPlayer`, to fail the Mitnick stealth
  condition; death also calls `setMitnickFail()` and sets `G.Shubs_Botch = 1`.
- **Tutorial:** maker children connect discovery, damage, death, and feeding to tutorial failure,
  progress, UI, and relationship changes. The maker's child I/O is part of the lesson controller.

The examples prove that a faithful implementation needs both layers. Implementing only native
combat produces NPCs that fight but do not advance the authored game. Implementing only outputs
produces quest events without credible sensing, interruption, pursuit, combat, or recovery.

## Python, dialogue, and level orchestration

### Binding model

The map `worldspawn` names the level script. Python resolves entities through `Find`/`Finds` by
targetname or classname. Entity output field six can execute a Python payload, dialogue columns 4
and 5 can mutate state, `ScheduleTask` can defer an action, and shared `G` values coordinate quest
state across callbacks. The bridge exposes datamap inputs and properties through dynamic
`__getattr__`/`__setattr__`; scripts are not limited to a small, formally declared AI API.

This means Python can drive the native AI without implementing its inner loop. It can write a
relationship, start or change a schedule, begin a sequence, change perception policy, or ask an
NPC to flee. The subsequent movement, conditions, task progress, and animation remain native.

### Action-facing corpus

The broad script-API survey resolves 16,814 call sites across 676 callable names. The focused
action survey below then selects the 3,367 calls that can directly change an actor, action,
sequence, schedule, relationship, or presentation. The installed script tree contains 41 `.py`
files and 14 compiled-only `.pyc` files; the focused level-script survey parses 36 applicable
source files, so those figures describe different corpus boundaries rather than a missing-file
claim.

The focused action/animation survey finds 3,367 action-facing calls:

| Action | Calls | Principal source |
|---|---:|---|
| `SetDisposition` | 2,509 | dialogue column 4: 2,467; dialogue column 5: 34; Python: 7; scheduled: 1 |
| `SetRelationship` | 346 | 334 immediate plus 12 scheduled |
| `BeginSequence` | 70 | Python: 67; dialogue: 1; scheduled: 2 |
| `CancelSequence` | 8 | immediate script |
| `SeductiveFeed` | 54 | action/dialogue surface |
| `SetAnimation` | 20 | action/dialogue surface |
| `StartSchedule` | 2 | immediate script |
| `SetGesture` | 2 | action/dialogue surface |
| `SetModel` | 356 | action/dialogue surface |

The immediate `SetRelationship` sites comprise 123 dialogue-column-5 calls and 211 Python calls,
including two survey-tool sites. The prevalence of `SetDisposition` in dialogue is strong evidence
for its emotional presentation role; the smaller but still substantial `SetRelationship` surface
is the deliberate native combat-AI relation mutation seam.

AI-relevant immediate calls in the corpus include:

| Call | Immediate sites |
|---|---:|
| `SetRelationship` | 334 |
| `WillTalk` | 79 |
| `BeginSequence` | 68 |
| `FleeAndDie` | 19 |
| `SetupPatrolType` | 16 |
| `FollowPatrolPath` | 16 |
| `CancelSequence` | 8 |
| `SetFollowerBoss` | 7 |
| `TweakParam` | 6 |
| `ChangeSchedule` | 5 |
| `StartSchedule` | 2 |
| `SetInvestigateMode` | 1 |
| `SetInvestigateModeCombat` | 1 |

Immediate and scheduled counts are intentionally distinguished. A scan that looks only for direct
calls undercounts deferred state changes.

### Script examples

`vamputil.BefriendAnimal` demonstrates how scripts compose several independent AI knobs. It sets a
like relationship, uses `TweakParam` to eliminate hearing and vision, clears the player
investigation/criminal/supernatural thresholds, and sets normal and combat investigation modes to
zero. The dog reaction path first establishes neutral and changes to `SCHED_VDOG_SNARL`; the
friend path changes to `SCHED_VDOG_MADEFRIEND`. The relationship row, perception policy,
thresholds, and schedule are all intentionally separate writes.

Other representative script patterns include:

- `warehouse.fearThugs()` calls `FleeAndDie` across an encounter population;
- `hateSabbat()` installs targeted hostility between groups;
- `santamonica.mercurioDialog()` can make Mercurio hate the player from quest state; and
- `StartSchedule` turns Mercurio around for authored staging.

The scripts use native names such as `SCHED_VDOG_SNARL` because schedule identity is a stable
control seam. They do not encode the per-frame path following, motor updates, or model sequence
choice themselves.

## Scripted control and authority

There are three materially different ways authored content takes control of an NPC. They must not
be collapsed into one generic "play script" operation.

### `scripted_sequence` and `aiscripted_sequence`

A scripted sequence claims the NPC body for a specific animation/cinematic action, with movement
to its mark and interruption/release policy controlled by the entity's inputs and spawn flags. It
is animation/choreography ownership. Generic contracts, including `BeginSequence`, cancellation,
completion, and body release, are documented in [entity_io.md](../entity_io.md).

While script ownership suppresses the ordinary condition-gathering path, the AI does not cease to
exist: state, movement ownership, completion/failure, and restoration still have to be coherent.
Cancel and map teardown must release the claim exactly once.

### `aiscripted_schedule`

The corpus contains **30** `aiscripted_schedule` entities on nine maps (re-counted 2026-09-21 over
all 108 V2 entity units; the 13 below was an earlier, partial export — the table and the counts
that follow it are kept as written and superseded by "The modes, settled" further down). Unlike a
scripted sequence, this entity pushes an AI policy and goal rather than claiming the body for one
exact animation.

| Map/use | Mode | Force state | Goal |
|---|---:|---:|---|
| Apartment, turn Mercurio around | 4 | 0 | named entity |
| Diner, four assassins | 3 | 3 | `!player` |
| Santa Monica hub, blueblood alley | 1 | 0 | named entity |
| Medical, four actions | 2 or 5 | 3 | mixed goals |
| Warehouse, three retreats | 2 | 2 | `!player` |

Across all rows, schedule modes 2, 3, 1, 4, and 5 occur six, four, one, one, and one times
respectively. Force-state values 3, 2, and 0 occur eight, three, and two times. Seven goals are
`!player`; six are named entities.

The spawn validator is at `0x101a9730` and the executor at `0x101a98c0`. Force state has the exact
authored-to-native mapping:

| Authored `forcestate` | Native state |
|---:|---|
| 0 | no forced state |
| 1 | idle (1) |
| 2 | alert (3) |
| 3 | combat (2) |

The non-identical numbering is load-bearing. Treating the keyvalue as the native enum would swap
combat and alert.

Recovered schedule modes are:

- modes 1 and 2 call variants of scheduled move-to-goal-entity;
- mode 3 assigns the goal entity as enemy, copies its target position, and injects native
  condition `0x54`;
- modes 4 and 5 call variants of scheduled follow-path;
- ~~the move/follow variants use internal schedule IDs 9 or 19, while a special NPC-type branch
  uses `0x22`~~ — wrong: 9, `0x13` and `0x22` are ACTIVITIES; see below.

**The modes, settled (2026-09-21).** _A Codex worker's walk
(`$ELYSIUM_WORK_ROOT/codex/re2/wp17-aiscripted-modes`); the executor re-read by the lead._
`CCineAISchedule` (datamap `0x10593c9c`, factory `0x101a96b0`) has five keys of its own —
`m_iszEntity +0x5f54`, `m_flRadius +0x5f68`, `goalent` `m_sGoalEnt +0x608c`, `schedule`
`m_nSchedule +0x6090`, `forcestate` `m_nForceState +0x6094` — over `CCineNPC`'s, and one input,
`StartSchedule` (`0x101a9b30`), which names nothing: it is `ThinkSet(CineThink)` at curtime. There
is NO `interruptability` key and no grab-all. **`m_flRadius` IS read** (corrected 0019/5 fold A3; the
2026-09-21 walk said it had no reader): the target is acquired by `CineThink` (`0x101a8070`), the
think `Spawn` and `StartSchedule` install, through `FindEntity` (`0x101a7600`, reads at
`0x101a7621` / `0x101a76c7`) -> `FindEntityGenericWithin` `0x100f7f70` -> `0x100f7c30` (name) then
`0x100f7e30` (classname fallback): each candidate named `m_iszEntity` (case-insensitive, trailing
`*` allowed, procedural `!` names from the start of the list only) whose origin lies STRICTLY inside
`m_flRadius` of the director (`d² < r²`; a radius of 0 is unbounded), starting after
`m_pLastFoundEntity` (`+0x5f98`, written under spawnflag `0x400`), and among those with `+0x94` the
first answering slot 482 `CanPlaySequence(FCanOverrideState(), 0) == 1`, else the LAST answering 2
(a candidate answering 0 prints "Found %s, but can't play!" unless spawnflag `0x800`). Found:
`m_sequenceStarted = 0`, slot 583 `PossessEntity` (for `CCineAISchedule`, `0x101a9790`: the
NOINTERRUPT oblivious call, then slot 586 on the target), no re-arm. Not found: `CancelScript`
(`0x101a8c30`, `ScriptEntityCancel` on every same-named entity) and a retry `+1.0 s`, forever.
23 of the 30 shipped `aiscripted_schedule` rows author a radius (32–1026). `Activate 0x101a8de0`
resolves the actor only for its precache; `goalent` resolves by name at the executor, `!player`
through the alias table.

The executor `0x101a98c0`, in order: resolve `goalent` — none → log "Can't find goal entity %s /
Can't execute script %s" (`0x1059528c`) and RETURN BEFORE the force state is applied; apply
`forcestate` (`SetState 0x1026e340`); then by `schedule`:

| `schedule` | Call | Installed schedule | Goal | Activity |
|---:|---|---|---|---|
| 1 | `ScheduledMoveToGoalEntity 0x102800c0` | base `2` `IDLE_WALK` | type 4, the goal's position | `ACT_WALK 9` |
| 2 | the same | the same | the same | `ACT_RUN 0x13` |
| 3 | `SetEnemy(goal)`; `UpdateEnemyMemory(goal, goal abs origin)` (slot 544); `SetCondition(NEW_ENEMY 0x54)` | none | — | — |
| 4 | `ScheduledFollowPath 0x102801e0` | base `2` `IDLE_WALK` | type 3, the goal-entity chain | `ACT_WALK 9` |
| 5 | the same | the same | the same | `ACT_RUN 0x13` |

**So 1 vs 2 and 4 vs 5 are walk vs run and nothing else**: the same program, the same goal, the
activity word of the goal record. For an NPC whose `Classify()` (slot `+0x178`) is 5 or 6 the
activity becomes `ACT_FLY 0x22`. The requested base text `0x10607ec8` is `IDLE_WALK` =
`TASK_WALK_PATH 9999; TASK_WAIT_FOR_MOVEMENT 0; TASK_WAIT_PVS 0`, interrupts `NEW_ENEMY
LIGHT_DAMAGE HEAVY_DAMAGE SMELL PROVOKED HEAR_COMBAT HEAR_BULLET_IMPACT`.
**Integration correction, 2026-09-22:** the caller passes local 2 to `0x10280de0`, whose
listing preserves that original argument at `10280e0b` while stamping its global translation,
then calls `0x102cc1f0`. Slot 440 therefore still runs: on Troika, 2 becomes `0x46
SCHED_TROIKA_IDLE_PATROL`. Its loaded text is `TASK_PATROL_PATH 0; TASK_WAIT_FOR_MOVEMENT 0;
TASK_WAIT_PVS 0`, with its OWN interrupts. `TASK_PATROL_PATH 0x105` enters `102a594d`, translates
`ACT_WALK_PATROL 0x1115`, probes the sequence, falls back to translated `ACT_WALK 9`, sets
the navigator activity (`102ee250`), clears `INCOVER` and completes (`102a5904`). Thus a run
goal's initial gait may be replaced by this task on the next think. The old port's two synthetic
programs and uninterruptible masks are deleted. (`SCRIPTED_WALK` / `SCRIPTED_RUN`,
`0x10605848` / `0x10605648`, belong
to the scripted SEQUENCE and are not selected here.) A refused route logs
(`0x10595230` / `0x105951d8`) unless spawnflag `0x800`; spawnflag `0x20` is the inherited
`m_interruptable (+0x5f90)`, and when it is CLEAR the dispatch `0x101a9790` calls `0x1026d130` on
the target — drop its enemy, disconnect it from its squad — before the order. The type-3 chain is
`navigation-jump-links.md` § "The goal types, their issuers and the goal record": `m_pGoalEnt
(+0x5de8)`, then `GetNextTarget` (`0x100a1d20`, the ordinary `target` key, no classname test) up
to `0x80` entities; a null next target ENDS the chain successfully.

Shipped, by mode: **1 × 5** (`hw_hub_1` two, `sm_beachhouse_1` two, `sm_hub_1`
`blue_blood_to_alley`), **2 × 18** (`sm_beachhouse_1` four, `sm_medical_1` three, `sm_warehouse_1`
three at `!player`, `sp_soc_1` seven, `la_bradbury_3` one), **3 × 5** (`sm_diner_1` four at
`!player`, `la_bradbury_3` one whose goal `plank_support_1` does not resolve), **4 × 1**
(`sm_apartment_1` `mercurio_turn_around`, whose goal is the entity ITSELF with no `target`: a
one-node chain to where it stands), **5 × 1** (`sm_medical_1` `guard_to_cs`, whose goal
`cs_target` does not exist — the log-and-stop case, shipped). `sp_soc_1` `h_hunter_2` has a blank
goal; two `sp_soc_1` rows aim at a `trigger_changelevel` and four at an `intersting_place`. No
row authors `interruptability`.

A missing goal logs and stops. Spawn warns when neither a schedule nor forced state is supplied; spawn flag
`0x800` suppresses the route-failure warning.

### Direct schedule changes

**Corrected 2026-09-21: only `ChangeSchedule` names a schedule.** It is the NPC's
`InputChangeSchedule 0x102c33f0` — resolve the name (`0x102c47e0`, `SCHED_` prefixed when absent,
global → local), write `m_iForcedSchedule (+0x65c8)`, set `CHOOSE_NEW_SCHEDULE`. `StartSchedule`
is the `aiscripted_schedule` ENTITY's input above and carries no schedule name. `ChangeSchedule`
is a policy-level command: the named schedule still executes normal tasks, failures, interrupts, motor work, and
activity translation. `BeginSequence`, by contrast, establishes sequence ownership. A rebuild
needs distinct interfaces for these operations so cancellation and save/restore preserve the
correct owner.

## Disciplines that possess or frenzy an NPC; the `AI_NPCFlag` payload (2026-09-08)

**The HitGroup record.** A discipline's `HitGroupList` entry (`0x374` bytes; loader `0x101e0080`)
holds five `0xac`-byte `HitInfo` blocks (instant `+0x08`, `OnEnd +0xb4`, `OnCallback +0x160`,
`OnInterrupt +0x20c`, `OnInterruptSchedule +0x2b8`), `Duration +0x364`, the trait effect `+0x36c`,
and two bytes parsed by `0x101dfa00`: **`+0x370` = key `DoPossession`** (`0x105a2870`), **`+0x371`
= key `DoFrenzy`** (`0x105a2864`), both default 0, copied verbatim by `InheritFrom` (`0x101df340`).
The earlier "make follower vs calm" reading was wrong: `+0x371` is frenzy; `D_CALM` is written only
by `SCHED_TROIKA_CALMED`'s `TASK_SET_NPC_FLAG`. The sole reader is `0x101dfc20` (DevMsg
`"Discipline<%s>: HitGroup: <%s> Hit Triggered on %s (%s)"`): apply the instant `HitInfo`
(`0x101de660`), then on the target's `+0x98` Troika pointer `DoPossession` first, else `DoFrenzy`,
then the trait effect. Chain: cast `0x101e2f50` → per target `0x101e3730` (Affects table picks the
HitGroup) → `0x101e3850` (or `CDisciplineProjectile::vfunc266` `0x101da020` on impact) →
`0x101dfc20`. The HitGroup's `AI_Schedule` is installed by `0x101de660` **before** either arm.

**`DoPossession` arm `0x102c51a0`** (the `SetFollowerBoss` path): `AddMiscFlag(0x800)` when the
enemy or the caster is hated; squad disconnect `0x1026d050`; `0x102b52a0` (`SetEnemy(NULL)`,
`SetTarget(NULL)`, `+0x5d8c &= 0xf7fc7fff`, clear hint); slot 304; `flags2 |= D_POSSESSED |
D_DISCONNECT_SQUAD`; caster a player → `0x10273790(this, "player D_LI 99")`;
**`SetFollowerBoss(ent)` `0x102c4470`** (`"!player"` for a player caster, else the caster's name)
→ `SetFollowerBoss(name)` `0x102c44e0`; **`SetFollowerType("Combat")` `0x102c4640`** → radii from
`Npc_Follower_Info` (`0x102c4680`); ideal state 1; `m_hTargetEnt (+0x5ce4)` and `m_hFriendPlayer
(+0x60ac)` = caster; slot 614; `m_bfNPCFrenziedFlags (+0x5b84) = 0x3b1c`; slot `0x94c` =
`0x102b4cc0` sweeps a 1024×1024×128 box and `SetEnemy`s the nearest hated entity.
**`DoFrenzy` arm `0x102c5310`**: the same first four steps, the hate acquisition, then `flags2 |=
D_INSANE | D_DISCONNECT_SQUAD`, the `D_LI 99` write, ideal state `0xb` (hunt),
`investigate_mode` and `_combat` = 6, `m_hFriendPlayer`, `frenziedFlags = 0x9fbd`; no follower.
Shipped setters (`disciplinetgt_001/002.txt`): `DoFrenzy` — `Dementation_Berserk` and
`Dementation_Bedlam`; `DoPossession` — `Dominate_Possession`. Presence, Animalism, Thaumaturgy,
Dominate 1/2/3/5 set neither.

**The `AI_NPCFlag` payload — a non-task writer of the flag words.** `HitInfo+0xa8` is the key
`"AI_NPCFlag"` (`0x105a26b4`), parsed by `0x101ddfb0` through the same 62-name resolver
`0x1030cbd0` as `TASK_SET_NPC_FLAG` (sign bit = word two). `0x101de6e1` is not a function but the
set arm inside `0x101de660` (`|=` on `+0x14b8`/`+0x14bc` of the target's Troika pointer);
`0x101def10` is the matching clear (`&= ~mask`, plus `RemoveFromComfortList` and a schedule
teardown), reached through the effect-expiry table. The sibling key `"MiscFlag"` → `+0xa4` via
`0x1033cb00`. This corrects the earlier claim that the task vocabulary is the only writer of the
words: a HitGroup sets flags on the target with no task, and `OnScheduleChange`'s masks will
clear them like any other.

**Apply/expiry implementation detail (2026-09-08, spec 0002 story 8).** Re-reading the
whole bodies corrects two shorthand descriptions above. `0x101de660` writes `MiscFlag` first
(`AddMiscFlag`, `0x1033c6b0`, plain OR), then the NPC mask, before health and schedule channels.
`0x101def10` clears the original NPC mask with `&= ~mask`, including the word-two routing bit;
it **does not clear MiscFlag**. The UP `Thaumaturgy` HitGroup
`Hit_Supernatural_BloodGuardian` explicitly calls `Forced_BloodShield` a permanent visual effect.
The misc resolver `0x1033cb00` compares all 22 names case insensitively and returns zero for an
unknown name. Loader `0x101ddfb0` ORs an authored misc mask into inherited `+0xa4`, whereas
`AI_NPCFlag +0xa8` replaces its inherited mask. Multiple live effects that write the same NPC bit
do not reference-count it: the first cleanup clears it even when another effect remains.

`AddToComfortList` (`0x10323630`) appends a handle **without deduplication** and zeroes
`m_iComfortingCount`. `RemoveFromComfortList` (`0x10323770`) removes the **first** matching
handle, preserving the rest of the array's order, and zeroes the count even when no match exists.
The original HitInfo's nonzero `AddToComfort` byte causes one removal during cleanup.
`Event_Killed` (`0x1032b9b0`) and `UpdateOnRemove` (`0x10327790`) each independently call this
same removal after their discipline-visual/presence cleanup; duplicates are not collapsed there.

The `AI_Schedule` cleanup is a **task completion request**, not a wholesale schedule clear:
after flag and comfort cleanup, an originally nonempty schedule channel checks the current
`D_DISCONNECT_SQUAD` bit and reconnects (`0x10009601`: decrement `+0x5bb0`, rejoin the squad's
memory at zero, `flags2 &= 0x7f7fffff`). It then reads the current schedule, and only local IDs
`0xe1`/`0xe3` call `TaskComplete(false)` (`0x10273e80`): if condition `0x5c` is clear, write task
status `+0x5c44 = 4`. It does not invoke `OnScheduleChange`. `0x101dfe80` removes the trait
effect, runs this original-HitInfo cleanup, then directly applies `OnInterrupt +0x20c` or
`OnEnd +0xb4`; these callbacks are HitInfo calls and create no new HitGroup timer. Conversely,
`0x102a0940`'s `ACTIVITY_COPY_PROP_CLEAN` arm interrupts targeted effects before the unconditional
flag tail. `DAT_10739a64` suppresses that interruption while HitInfo installs its own schedule.

The port stores resolved cleanup masks and channel-presence bits on every flag/comfort/schedule
effect, including effects without trait modifiers, and persists them with the shared discipline
block. The common misc word and comforting count persist on both player and NPC; the ordered
comfort registry persists on the map with handle rebasing. Expiry, explicit clear, interruptions,
and exhausted-effect reconciliation use the same cleanup. Possession/frenzy and shared squad
memory remain the separate requirements 16/17; no substitute state is inferred from a flag.
The original caster handle also rebases when an NPC's discipline block loads: the direct
`OnEnd`/`OnInterrupt` HitInfo dispatch from `0x101dfe80` still receives that caster after restore.

**The two `TriggerAISound` producers (2026-09-08, requirement-6 integration).** The parser's
record byte `+0x35` (`0x101e06a0`) controls both. Source activation `0x101e3560`, called once by
`0x101e2f50` before its target loop, checks the same record's active status, adds a 0.1-second
source status if absent, sets misc `Fired_Gun 0x200000`, and inserts **COMBAT `1`** at the caster,
owned by the caster, for **0.2 seconds** using `DAT_1072bc40`/`DAT_1072bcb0` (the gunshot row).
The HitGroup prelude `0x101dfc20`, after target `AddDiscFlag` and before `0x101de660`, inserts
**BULLET_IMPACT `0x10`** at and owned by the target, also for **0.2 seconds**, using
`DAT_1072bc58`/`DAT_1072bcb6` (corrected 2026-09-12: those cells are the `BULLET_IMPACT` row
of `sound_volume_table.txt`, index 14, volume 7 → 250 units, occludable; the gunshot cells are
`PLAYER_GUNSHOT_PISTOL`, index 8, 1200 units; `NPC_DISCIPLINE_ALERT` is index 27 at
`0x1072bc8c`/`0x1072bcc3` and is read by nobody — see the flee section's table layout). These are two real insertions; neither
replaces the other. `0x10` is not DANGER (`0x8`). Direct OnEnd/OnInterrupt HitInfo callbacks do
not repeat the HitGroup prelude. The source status guard is record-wide, so a live targeted status
for the same record also suppresses repeated source activation.

**Derived condition tables, tutorial classes.** Registrar helper `0x102ea130` =
`CAI_ClassScheduleIdSpace::AddSymbol` (63 callers); derived local ids start at `0x78`.
`CNPC_VVampire` (`0x103c4ab0`), `CNPC_VHuman` (`0x10384230`), `CNPC_VHumanCombatant`
(`0x10386cb0`), `CNPC_VPedestrian` (`0x103a1fd0`) register **no** condition above `0x76`;
`CNPC_VRat` has no registrar and inherits `CNPC_VScurrying`'s (`0x103abd70`): **`0x78
COND_VSCURRYING_PLAYER_TOOCLOSE`** (its global id is load-order assigned). The 17 classes that do
add conditions: Crow, VAndreiBlood, VBach, VBatSwarm, VChangBros, VDog, VFrenzyShadow,
VGhoulCroucher, VMingXiao, VMingXiaoTentacle, VSabbatLeader, VScurrying, VSheriffMan,
VSheriffSwarm, VTzimisce, VWerewolf, VZombie.

**`stay_entrenched`** is a `CAI_BaseNPCTroika` datamap keyfield: `m_bStayEntrenched +0x6435`,
`FIELD_BOOLEAN`, builder `0x1028cd70`; plus the input `StayEntrenched` → `0x102c2bd0` (writes 0
on any non-boolean variant). Readers: `0x102ae920` (combat state only: an entrenched NPC that
passes slot 592 and gets a schedule from `0x102b7690` returns it and skips the ordinary tail) and
`NPCThink` `0x10292de0`. UNRECOVERED: `0x102b7690`'s arms, slot 592, the `NPCThink` arm.
UNRECOVERED elsewhere in this section: the `m_bfNPCFrenziedFlags` vocabulary (`0x3008` from
`SetFollowerBoss`, `0x3b1c`/`0x9fbd` from the arms, `& 0x80` in `0x102ae920`), slots 304 and 614 (misc-flag bit 11 is `Was_Hateful`, see below).

## The navigation and reaction keyfields (2026-09-08)

Seven `CAI_BaseNPCTroika` keys the port did not read, each with its readers and a verdict
(ROUTE COST = a term Unreal NavMesh must be given; SELECTION = schedule/task selection, ported
verbatim; ANIMATION). None appears in any vdata pack; they are map-only.

- **`bright_route_penalty`** — `m_iBrightRoutePenalty +0x6344`, int. **Unconsumed in retail**: the
  only access in the image is the copy in `CNPCMaker_Fleshpile::MakeNPC` (`0x1034c2d0`); no
  pathfinder cost reads it (checked `+0x6344`, the index form `[0x18d1]`, and every "Penalty"
  string). Authored `0` on 1802 NPCs and `100000` on the three `npc_VLasombra` in
  `la_bradbury_2`. Verdict: nothing to port; a NavMesh light cost would be a divergence.
- **`percent_occluded_wait/_cover/_walk/_flank/_chase`** — `+0x6420/24/28/2c/30`, int.
  **Spawn normalizes them into a cumulative 0–100 ladder** (`0x10298d30`): `sum = all five; if
  (sum > 0) { chase = 100; wait = wait·100/sum; cover = wait + cover·100/sum; walk = cover +
  walk·100/sum; flank = walk + flank·100/sum }`; if `chase != 100` DevMsg and, if `chase < 1`,
  the fallback `10/40/50/70/100`. `_chase` is forced to 100 and **never compared**. `thug_1`'s
  `10/30/10/20/30` normalizes to exactly the fallback. Consumer: the occluded-enemy selector
  `FUN_102b8320` (vtable `+0x978`, slot 606), reached from `CNPC_VHuman::SelectScheduleRangedCombat`
  (`0x10386560`) and `0x103967d0` — **ranged combat only**, never from the melee selector
  `0x10385e40`. Order: `!COND_ENEMY_OCCLUDED 0x48` → 0; `COND_ENEMY_UNREACHABLE 0x59` → `0xaa
  WAIT_FOR_OCCLUDED_ENEMY`; `frenzied & 0x100` → `0xb4 CHASE_ENEMY_LKP`; `flags2 & 0x40000` →
  `roll < 70 ? 0xb6 LKP_FLANK : 0xb4`; `flags1 & 0x10000000` (cleared; set by `0x102b7cf0`'s
  `COMBAT_DODGE_WAIT` arm) → `roll < 80 ? 0xb6 : 0xb4`; else `roll = RandomInt(0, 99)` against
  the ladder — with `COND_SQUAD_SEE_ENEMY 0x31`: `< wait` `0xab`, `< cover` `0xb0`, `< walk`
  `0xb3`, else `0xb2` (the flank arm also returns `0xb2`: no squad flank variant); without:
  `< wait` `0xaa`, `< cover` `0xaf COVER_FROM_OCCLUDED_ENEMY`, `< walk` `0xb5 CHASE_ENEMY_LKP_WALK`,
  `< flank` `0xb6`, else `0xb4`. `CNPC_VBach` overrides (`0x10364280`, latches `+0x66a3`).
  Verdict: SELECTION, verbatim including the dead `_chase` and the duplicated squad arm.
  UNRECOVERED: producers of `frenzied & 0x100` and `flags2 & 0x40000`; the owners of the two
  code-folded copies `0x103675a0`/`0x103b2550` (slot 606 is past the vtable dump).
- **`hint_groups`** — `m_sHintGroups +0x62e0` → `m_iHintGroups +0x62e4`, parser `FUN_102989e0`
  (from Spawn and `ProcessTweakParam` `0x1029aa10` token `HINTGROUPS`): a **space-separated list
  of 1-based indices**, each `1 << (n−1)` for `1..32`; **empty = `0xFFFFFFFF` (all groups)**. Sole
  reader `CAI_BaseNPCTroika::FValidateHintType` (`0x10295c20`, slot 566): `(hint->m_iGroupID
  +0x470 & m_iHintGroups) == 0` → reject, then a switch on `m_nHintType`. `CAI_Hint::Spawn`
  (`0x102d0b60`) converts the hint's `group_id` `1..32` to `1 << (id−1)`, anything else to
  **Ported (29c)**: `FElysiumNpc::ParseGroupMask` is the parser, `SetHintGroups` the write, and
  `FElysiumNpcScheduleHost::HintGroupMask +0x62e4` the set; `Elysium.Substrate.NpcGroupMask` is the
  test. The reader, `FValidateHintType`, is layer 5–9's and is not ported yet.
  `0xFFFFFFFF`, and assigns a category bit at `+0x474` per type: `100 info_node_cover_med` 1,
  `101 _cover_low` 1, `10000 info_node_hint`/`info_node_patrol_point` none, **`10100 (0x2774)
  info_hint` none**, `10200 _cover_corner` 1, `10300 info_node_kick_over` 4, `10301
  info_node_kick_at` 8, `10400 info_node_shoot_at` 0x10 (with per-type angle/distance/rating
  defaults). The mask search `FUN_102d2980` (global list `DAT_10925450`, cursor `DAT_10925454`;
  flags 1 LOS, 2 nearest, 8 rating-weighted) requires a category bit, so `info_hint` is found
  only by the type-keyed search `0x102d1af0(this, 0x2774, 2, …)` in Troika `StartTask`;
  `FUN_102b7110` stores the result in `m_pHintNode +0x5ddc`. 1716 of ~1830 NPCs author the full
  `"1 … 32"`, identical to leaving it blank. Verdict: SELECTION. UNRECOVERED: the search path for
  type 10000 (no category bit, no validate case).
- **`allow_kick_hint_use`** — `m_bAllowKickHintUse +0x6436`, bool; input `AllowKickHintUse`
  (`0x102c2c10`); copied to children by `CNPCMaker::MakeNPC` (`0x1034b7b0`). Sole consumer
  `FUN_102b7690(bCover, bCorner, bKickOver, bKickAt)`: the kick-prop search (`bKickAt && flag &&
  no hint && !(flags 0x800) && no prop && curtime >= +0x6438` → timer `+2.0`, `m_hKickPhysicsProp
  +0x643c = FUN_102b6650` — needs an enemy, ±512/±512/±64 box, cap 20, prop flag `+0x788`,
  nearest passing `0x102b62e0`); a live prop → **`0xa9 SCHED_TROIKA_KICK_PROP_AT_ENEMY`** before
  any hint search; and the search mask bits `4`/`8` (kick-over 10300 → `0xa7 HINT_KICK_OVER`,
  kick-at 10301 → `0xa8 HINT_KICK_AT_ENEMY`). Callers: `0x102ae920` `(1,0,0,0)` (entrenched
  cover), the melee selector `0x10385e40` `(0,1,0,1)`, `0x102b7cf0` `(1,1,1,1)`. No hint type
  carries category bit 2, so **for a melee NPC the hint search returns only a kick-at node, and
  only under this flag**. Retail maps hold exactly one kick hint (`sm_beachhouse_1` `kick_spot`,
  10300) and no kick-at nodes; the live path is the physics-prop kick. Verdict: SELECTION.
  UNRECOVERED: `0x102b62e0`, `prop+0x788`, the producer of `COND_KICK_PROP_INVALID 0x2a`.
- **`stay_entrenched`** — seven readers, all "do not give up my cover": `NPCThink` (`0x10292de0`,
  suppresses the hint release on `m_hHintCoverObject == enemy && (0x2e || 0x48)`); the cover-hint
  evaluator `0x10296c40` (accept own hint immediately; skip the max-range reject); `SelectSchedule`
  `0x102ae920` (combat + slot 592 → `FUN_102b7690(1,0,0,0)` ahead of the ordinary tail);
  `0x102b7110` (retry the search once); `0x102b7690` (cover LOS failed and `m_iPeekOutCount
  +0x640c >= 5` or occluded: entrenched → `0x9e TAKE_COVER_PEEK_OUT_WAIT`, else `ClearHintNode(60)`);
  `0x102b7cf0` (no dodge-reposition `0x8c`/`0x8d` when entrenched); `ShouldInvestigate`.
  Authored `1` on 65 NPCs. Verdict: SELECTION.
- **`combat_start_activity`** — `m_sCombatStartActivity +0x65e0` → activity id `+0x65e4`
  (`FUN_1029f340` → `ActivityFromName 0x10412520`; absent, `"-1"` and `"ACT_INVALID"` all give
  −1). Gate at the top of `SelectSchedule` `0x102ae920`: `m_iSquadDisconnected < 1 && squad &&
  (flags2 & 0x2000)` → consume the latch; `activity != −1 && !(frenzied & 0x80)` → **`0xeb
  SCHED_TROIKA_START_COMBAT_SQUAD`** (`WAIT_FACE_ENEMY 0.2; PLAY_SOUND Target_Acquired;
  TASK_PLAY_COMBAT_START_SEQUENCE 0x137; TASK_SQUAD_NEW_ENEMY; WAIT_RANDOM 0.2`), else notify the
  squad (`0x103161a0`). `0xea START_COMBAT` (on `NEW_ENEMY && !frenzied`) has no activity task.
  Retail authors a real activity on exactly one NPC (`sec_cam_1_npc`, `la_museum_1`, no squad),
  so the animation is unobservable in shipped content. Verdict: ANIMATION plus the `0xeb` gate.
- **`interesting_place_groups`** — `+0x62d8` → `+0x62dc`, parser `FUN_10298910`: the **same
  1-based index list → mask** as `hint_groups`, but **empty or `"0"` leaves the mask `0`** (no
  place ever matches; 1005 retail NPCs author `"0"`). `CNPC_VCamera::Spawn` zeroes it. This
  corrects the "raw AND of two decimal integers" reading in the interesting-places section for
  the NPC side. UNRECOVERED: whether `CAI_InterestingPlace` converts its own `group_id` to
  `1 << (id−1)` as `CAI_Hint::Spawn` does; if it does, `thug_1`'s pool is `pt1` alone, if the
  place keeps the raw integer the seven-node pool stands — **decided: it converts**
  (`CAI_InterestingPlace::Spawn` `0x102d9c20`), so `thug_1`'s pool is `pt1` alone; the
  interesting-places section above is corrected.
  **Ported (29c, admission closed in 29c-1)**: the same `FElysiumNpc::ParseGroupMask` fills
  `InterestingPlaceGroupMask +0x62dc` on the keyfield write, and `FElysiumNpc::AcceptsAmbientGroup`
  is now `0x102dad60`'s own gate — `place->m_iGroupID +0x574 & npc->m_iInterestingPlaceGroups
  +0x62dc`, against the mask `CAI_InterestingPlace::Spawn` (`0x102d9c20`) folded, so an unset or
  `"0"` list matches NO place. 29c had left the port's "an empty list is every group" rule standing
  as a named divergence; that divergence is closed, and the consequence is retail's: the 1005
  shipped NPCs that author `"0"` do not wander to interesting places at all.
  `Elysium.Substrate.NpcGroupMask` asserts the zero-mask case by name.

## Story 8, family Script19, the script directors — `CCineNPC`, `CCineAI`, `CCineAISchedule` and the Troika's scripted exits (2026-09-27)

_Recovered 2026-09-27, 0019 story 8 pass I: lane L10, integrated as `576a06fc`._

The director half of family Script19's 20 `rule` rows (§ "Scripted control and authority" above
is the authored side). Source of truth: the packet
`$ELYSIUM_WORK_ROOT/research/npc-kernel-checklist/families-19-29/Script19-READING.md` and the
listings (`vtmb_asm`). The family's movement helpers are in `schedule-kernel.md` § "Story 8,
family Script19, the Troika's movement helpers". Port: `ElysiumScriptedSequence.cpp`,
`ElysiumAiScriptedSequence.cpp`, `ElysiumAiScriptedSchedule.cpp`, `ElysiumNpcBaseScript.cpp`,
`ElysiumNpcScript.cpp`, `ElysiumNpcScriptSpecies.cpp`; tests
`Elysium.Substrate.NpcKernelScript19.*`.

`0x101a7140 CCineNPC::UpdateOnRemove` (19 bytes) has no section: base `UpdateOnRemove 0x1027ca30`
direct (`0x101a7143`), then `ScriptEntityCancel 0x101a7170(this)` (`0x101a7149`).

### `0x101a8c30` CancelScript (130 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L10)._

1. `DevMsg(2, "Cancelling script: %s\n", m_iszPlay ?: "")` (`0x101a8c34..0x101a8c4b`).
2. `m_iName` (`+0x26c`, the cine's OWN targetname — not `m_target +0x20c`) null → `ScriptEntityCancel(this)`
   and return (`0x101a8c5c` / `0x101a8c5f` / `0x101a8c69`).
3. Otherwise `FindEntityByName(NULL, m_iName)` and every next match (`0x101a8c76`, `0x101a8ca4`),
   `ScriptEntityCancel` on each (`0x101a8c82`); the name is re-read each pass, the empty string
   substituted when null (`0x101a8c92`).

Reads `+0x5f48`, `+0x26c`. Writes nothing itself (the cancel zeroes each cine's `+0x5f70`).
**Unrecovered:** none.

### `0x101a8640` Finish / PostIdleDone (396 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L10)._

1. Hold arm: `m_iszPostIdle (+0x5f4c)` set (`0x101a864d`) AND spawnflag `0x100` (`0x101a865c`) AND
   `m_hNextCine (+0x5f94)` not live (`0x101a8671..0x101a8690`) → `DevMsg(2, "Post Idle %s finished\n")`
   with the post-idle of the NPC's OWN `m_hCine` re-resolved from `npc+0x5d74` (`0x101a8696..0x101a86d5`;
   a stale handle reads `[NULL+0x5f4c]` and faults — the port prints the empty string), `0x1027f270(2)`
   (empty), `npc->m_scriptState := 2` (`0x101a86e7`), slot 584 `(npc, m_iszPostIdle, 0)` (`0x101a86ff`),
   RETURN (`0x101a8708`) before any cleanup.
2. Spawnflag 4 clear (`0x101a8712`) → `ThinkSet(SUB_Remove)` and `m_flNextThink = curtime + 0.1`
   (`0x101a871f` / `0x101a8733`).
3. `CineCleanup 0x1027d170(npc)` (`0x101a873f`), slot 586 `FixScriptNPCSchedule(npc)` (`0x101a8749`).
4. `m_hNextCine` live (`0x101a8758..0x101a877d`) and either not `this` or spawnflag 4 set
   (`0x101a87a9` / `0x101a87b2`) → `SetTarget(next, npc)` (`0x101a87b7`) and next's slot 583 (`0x101a87c0`).

**Unrecovered:** none.

### `0x1027d0a0` ExitScriptedSequence (153 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L10)._

1. `m_lifeState (+0x200) == 1 LIFE_DYING` (`0x1027d0a0` / `0x1027d0a7`) → `+0x1b3c/+0x1b40 =
   AI_BaseNPC.cpp:0x2a70`, `m_IdealNPCState (+0x5cc4) := 7` (`0x1027d0a9..0x1027d0bd`), answer FALSE
   (`0x1027d0c7`); `m_hCine` stays.
2. `m_hCine (+0x5d74)` -1 / stale / null → answer TRUE with nothing cancelled (`0x1027d0d4` /
   `0x1027d0f6` / `0x1027d0fc` → `0x1027d135`).
3. Live → `CancelScript` on it (`0x1027d125`), TRUE (`0x1027d12a`). The re-read's null-call arm
   (`0x1027d12e` / `0x1027d130`, `CancelScript(NULL)`) is unreachable: nothing runs between the reads.

Port: `m_lifeState` has no word; DYING is the death transaction between `Event_Killed`
(`bDeathReported`) and `TASK_DIE`'s commit (`bDeathCommitted`). **Unrecovered:** none.

### `0x101a7880` CCineNPC PossessEntity (1577 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L10)._

1. `m_hTargetEnt (+0x5ce4)` → entity → `+0x94`; any miss returns (`0x101a7891..0x101a78ca`).
2. NPC `m_bRanAI (+0x1b4c)` clear → the eleven-line `DevMsg` block with the "that has not run it's AI
   yet....." line (`0x101a78da..0x101a794f`).
3. NPC `m_hCine (+0x5d74)` live (`0x101a795d..0x101a7987`) → queue arm: the current cine's live
   `m_hNextCine` is kicked (`SetTarget(kicked, NULL)` `0x101a7af7`, `DevMsg(2, "script \"%s\" kicking
   script \"%s\" out of the queue\n")` `0x101a7b13`), our handle goes into its `+0x5f94`
   (`0x101a7b5a`), return. The `Msg`s under the debug ConVar `DAT_1072bb84` are dead.
4. The take: `0x100b5190` (`+0xf4`) → `0x101a77a0` (`0x101a7c30`); `m_interruptable (+0x5f90)` clear →
   `0x1026d130` (`0x101a7c41`); empty `m_iszNextScript (+0x5f58)` → `m_hNextCine := -1` (`0x101a7c50`);
   `m_pGoalEnt`, `m_hCine`, `SetTarget(npc, this)` (`0x101a7c5a..0x101a7c72`); the save block `+0x5f78`
   movetype, `+0x5f7c` movecollide, `+0x5f80` solid, `+0x5f84` solid flags, `+0x5f88` effects
   (`0x101a7c7b..0x101a7cbd`; the effects word is the NPC's `m_fEffects +0x19c`, which `CineCleanup
   0x1027d170` writes back at `0x1027d271` / `0x1027d279`); NPC `+0x98` → slot 614, `+0x5f8c := npc+0x14b8`, OR `0x40` under spawnflag
   `0x1000` (`0x101a7cc3..0x101a7cf6`); `npc->m_fEffects |= ours` (`0x101a7d0a`).
5. `m_fMoveTo (+0x5f60)`, table `0x101a7eac`: 1/2/3 → state 4/5/6 each with `DelayStart(1)`; 4 →
   teleport (slot 181 to our origin with NULL angles and a zero velocity; `0x102e0b40`; the motor's
   ideal yaw from our yaw with the `+0x28` flip; `SetLocalAngularVelocity(0)`; `EF_NOINTERP`; NPC yaw :=
   ours, pitch/roll kept) and FALLS THROUGH; 0/5 → state 1; above 5 skips.
6. `AI scripted.cpp:0x2c8`, `m_IdealNPCState := 4` (`0x101a7e84..0x101a7e98`).

**Unrecovered:** the director's own `m_fEffects` (no port word; spec 0003), so the OR at `0x101a7d0a` adds
nothing here.

### `0x101a8460` SequenceDone (371 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L10)._

1. Debug-ConVar `Msg` and `0x101a6ec0` (`0x101a8463..0x101a850b`) — dead.
2. `m_iszPostIdle` empty OR `m_hNextCine` live (`0x101a8518..0x101a8545`) → `Finish` (`0x101a857b`);
   else `0x1027f270(2)`, `npc->m_scriptState := 2` (`0x101a8554`), slot 584 `(npc, m_iszPostIdle, 0)`
   (`0x101a856c`).
3. `m_OnEndSequence (+0x5fb4)` through `0x100cd660` LAST and unconditionally, activator `+0x10c` or NULL,
   caller `this` (`0x101a8580..0x101a85c9`).

**Unrecovered:** none.

### `0x101a8890` AllowInterrupt (123 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L10)._

1. Spawnflag `0x20` → return (`0x101a889a`).
2. No target or no `+0x94` → `+0x5f90 := arg` (`0x101a88aa..0x101a88d7` → `0x101a8900`).
3. Latch 1, arg 0 → `0x1026d130`, latch 0, return (`0x101a88e7..0x101a88f4`); latch 0, arg 1 →
   `0x10007ea0` (`0x101a88fb`); then `+0x5f90 := arg` (`0x101a8900`).

**Unrecovered:** its caller's arm (`HandleAnimEvent 0x10274e30`, spec 0003).

### `0x101a9080` CCineAI PossessEntity (899 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L10)._

`0x101a7880`'s order without the queue arm (a standing cine is overwritten), without the `m_hNextCine`
clear and without `DelayStart`; the warning block skips the "has not run" line (`0x101a90d9..0x101a9147`).
Switch (`0x101a9404`): 0/5 → state 1; 1/2/3 → 4/5/6; 4 → the teleport, state 1 and `RemoveFlag(FL_ONGROUND)`
(`0x101a9388..0x101a939b`); above 5 → `DevWarning(2, "aiscript:  invalid Move To Position value!")`
(`0x101a93a9`). Then `DevMsg(2, "\"%s\" found and used\n")` (`0x101a93c1`), read `m_NPCState` BEFORE
writing `scripted.cpp:0x554` and ideal 4 (`0x101a93c7..0x101a93e7`); a state that was already 4 →
`0x10280de0(0x2e)` (`0x101a93f8`). **Unrecovered:** the Troika slot-440 translation of `0x2e` (spec 0003).

### `0x101a9790` CCineAISchedule PossessEntity (233 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L10)._

Target → `+0x94` or return (`0x101a979d..0x101a97d5`); `m_bRanAI` clear → the warning block without the
"has not run" line (`0x101a97e6..0x101a9854`); `m_interruptable` clear → `0x1026d130` (`0x101a9866`); slot
586 `(npc)` (`0x101a9870`). Writes nothing on either object itself. **Unrecovered:** none.

### `0x101a82d0` StartSequence (315 bytes) and `0x101a9510` CCineAI StartSequence (139 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L10)._

`m_sequenceStarted (+0x5f91) := 1` FIRST (`0x101a82da` / `0x101a9517`). A null name with
`completeOnEmpty` → `SequenceDone(npc)` and answer 0 (`0x101a82f0..0x101a82f9`) — `CCineAI` answers 1
(`0x101a9532`); a null name without it continues with `""`. `LookupSequence` into `m_nSequence`; -1 →
`Warning("%s: unknown scripted sequence \"%s\"\n")` / `"...aiscripted..."` and sequence 0; `m_flCycle :=
0`; `ResetSequenceInfo`; the `CCineNPC` body's debug `Msg` tail (`0x101a8358..0x101a83fe`) is dead;
answer 1. **Unrecovered:** none (the port plays the clip by name — no studio header).

### `0x1037c1c0` CNPC_VGhoulCroucher::ScriptHide (232 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L10)._

Scope-trace push keyed on `m_iName` (`0x1037c1c5..0x1037c227`); `CAI_BaseNPCTroika::ScriptHide
0x102c1ce0` direct (`0x1037c229`); `m_hBurningParticle (+0x6670)` live (`0x1037c237..0x1037c25e`) →
slot 77 on the particle (`0x1037c286`); the re-validation's null-receiver arm (`0x1037c295`) is dead; the
handle is not cleared. **Unrecovered:** none.

### `0x102c1ce0` CAI_BaseNPCTroika::ScriptHide (369 bytes, slot 77)

_Recovered 2026-09-28, 0019 story 8 wave 2 (family Damaged19); port `FElysiumNpc::ScriptHide`._

No jump table: the "indirect jump" is the tail `JMP [vtbl+0x134]` (`0x102c1e48`). Arms in order:

1. Warn gate: `m_NPCState (+0x5cc0) == 4` (`0x102c1cf1`) enters the warn arm; otherwise `m_hCine
   (+0x5d74)` resolved (`0x102c1cfc` -1, `0x102c1d1a` serial, `0x102c1d23` null) and only a live cine
   enters; a dead handle goes straight to the base half (`0x102c1e29`).
2. Sequence name: a live cine's `GetDebugName` (`0x102c1d77`), else `"**UNKNOWN**"` (`0x105477a4`,
   `0x102c1d87`). The `GetDebugName(NULL)` re-resolve arm (`0x102c1d7e`) is dead.
3. Two warnings through `[0x109f364c]`: `"Attempting to ScriptHide an NPC (%s) playing a scripted
   sequence (%s).  Satan will eat your babies if you continue to do this.\n"` with `GetDebugName(this)`
   and the name (`0x102c1d9b`), then `"Cancelling script...\n"` (`0x102c1da2`).
4. Live cine → `CancelScript 0x101a8c30` on it (`0x102c1e00`); dead handle → `CineCleanup 0x1027d170`
   on this NPC (`0x102c1e12`). `CancelScript(NULL)` (`0x102c1e09`) is dead.
5. Unless `m_NPCState == 7` (`0x102c1e1e`), `0x102ae7f0(0x6b)`: `m_iForcedSchedule (+0x65c8) :=
   SCHED_TROIKA_IDLE_DISPOSITION` — a store, nothing installed.
6. Every path: `CBaseEntity::ScriptHide 0x100a8710` (`0x102c1e2b`), then `GetActiveWeapon 0x1032e7b0`
   twice (`0x102c1e32` test, `0x102c1e3d` receiver) and slot 77 on the weapon (`0x102c1e48`).

`CNPC_VGhoulCroucher::ScriptHide 0x1037c1c0` reaches this body through thunk `0x10006d11`
(`0x1037c229`). **Unrecovered:** none.

## Story 8, family Boss19, the discipline helpers — `ResetAiState` `0x102b52a0`, `DoPossession` `0x102c51a0`, `DoFrenzy` `0x102c5310` (2026-09-27)

_Recovered 2026-09-27, 0019 story 8 pass I: lane L12, integrated as `2eca094a`._

The Troika helpers the possession and frenzy discipline effects call (§ "Disciplines that
possess or frenzy an NPC; the `AI_NPCFlag` payload" above). Port: `ElysiumNpcBoss.cpp`,
declared in `ElysiumNpcBoss.inl`.

`0x102ae750` is `SetSchedule(id, force)`: translate (slot 440), resolve (slot 446, fallback 1), refuse
in state 7, and — unless `force` — refuse when slot 158 `IsAlive` is false. The `+0x1b30`/`+0x1b34`
and `+0x1b3c`/`+0x1b40` file/line stamps are recorded in the schedule / mind traces.

### `0x102b52a0` `ResetAiState(bool, bool)` (135 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L12)._

`SetEnemy(NULL)` (`0x102b52a5`); `SetLastEnemy(NULL)` (`0x10279b70`, `+0x1a94` = -1, `0x102b52ae`);
`m_afMemory &= 0xf7fc7fff` (`0x102b52c5`); with the first argument, `0x10273760` (`0x102b52cf`); the
enemy store's clear-all `0x102dfc10` on slot 541 `GetEnemies()` with `UTIL_VarArgs("%s(%d) :",
"E:\Vampire\main\dlls\AI_BaseNPCTroika.cpp", 0x5626)` (`0x102b52e3`..`0x102b52f8`); with the second,
the ideal-state stamp at line `0x562a` and `m_IdealNPCState = 1` DIRECTLY (`0x102b5319`) — no
`SetState`, no `OnStateChange`.

**Correction to the checklist:** `0x10273760` is not a hint release. Its whole body is
`InputSetRelationship(this, m_RelationshipString (+0x1584) ?: "", 0)` — the authored relationship
line re-applied. The format string is `"%s(%d) :"` (the packet's correction stands).
`0x102dfc10` pops the list head until empty; for a store that is not squad-shared (`+8 == 0`) with
an owner it calls the owner's slot 56 (`[owner]+0xe0`, `0x102b5120`) with the resolved record entity,
the record's two vectors (`+0xc..+0x20`) and the reason, then frees the record (`0x102df1b0`); a
squad-shared store calls `0x103169a0` instead. The port empties its store and runs slot 56 per popped
record (integration review; the lane dispatched no callback).
**Unrecovered:** which record words the two vectors are (the port hands `LastPosition` / `Anchor`);
the squad arm (no squad-shared store stands).

### `0x102c51a0` `DoPossession(CBaseEntity* caster)` (281 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L12)._

Null caster: nothing (`0x102c51aa`). A current enemy that slot 404 `IRelationType` calls `D_HT`, then
the caster the same: `AddMiscFlag(0x800)` each (`0x102c51dd` / `0x102c51f9`). `DisconnectFromSquad`
(`0x102c5200`); `ResetAiState(1, 1)` (`0x102c520b`); slot 304 `GiveBaseFightingItems` (`0x102c5214`);
`m_bfAINPCFlags2 |= 0x80840000` (`0x102c5226`); a player caster: `InputSetRelationship("player D_LI
99")` (`0x102c523f`); `SetFollowerBossName(caster)` (`0x102c4470`, `0x102c5247`);
`SetFollowerType("Combat")` (`0x102c5253`); the ideal-state stamp at line `0x6e42`, `m_IdealNPCState` =
1, `SetState(1)` (`0x102c525c`..`0x102c527a`); `SetTarget(caster)` (`0x102c5282`); `m_hFriendPlayer`
(`+0x60ac`) = caster (`0x102c5292`); slot 614 `ResetThinkTimers` (`0x102c529a`); `m_bfNPCFrenziedFlags`
= `0x3b1c` (`0x102c52a4`); slot 595 `AcquireNearestHatedTarget` last (`0x102c52ae`).
**Unrecovered:** the discipline effect that calls it is not wired in the port.

### `0x102c5310` `DoFrenzy(CBaseEntity* caster)` (260 bytes)

_Recovered 2026-09-27, 0019 story 8 pass I (lane L12)._

As `DoPossession` up to `ResetAiState(1, 1)` (`0x102c537b`), then slot 595 FIRST (`0x102c5384`) and slot
304 (`0x102c538e`); `m_bfAINPCFlags2 |= 0x80820000` (D_INSANE, `0x102c53a0`); the player line
(`0x102c53b9`); the stamp at line `0x6e7f`, `m_IdealNPCState` = `0xb` HUNT, `SetState(0xb)`
(`0x102c53c2`..`0x102c53e0`); `m_eInvestigateMode` = `m_eInvestigateModeCombat` = 6 (`0x102c53ec` /
`0x102c53f2`); `m_hFriendPlayer` = caster (`0x102c53ff`); `m_bfNPCFrenziedFlags` = `0x9fbd`
(`0x102c5405`). No `SetTarget`, no follower boss, no slot 614.

**Unrecovered:** nothing named by the walk.
