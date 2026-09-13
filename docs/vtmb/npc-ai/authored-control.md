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

The corpus contains 13 `aiscripted_schedule` entities. Unlike a scripted sequence, this entity
pushes an AI policy and goal rather than claiming the body for one exact animation.

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
- the move/follow variants use internal schedule IDs 9 or 19, while a special NPC-type branch uses
  `0x22`.

The exact gait or policy label distinguishing 1 from 2 and 4 from 5 is not yet proven. A missing
goal logs and stops. Spawn warns when neither a schedule nor forced state is supplied; spawn flag
`0x800` suppresses the route-failure warning.

### Direct schedule changes

`ChangeSchedule` and `StartSchedule` name native schedules explicitly. They are policy-level
commands: the named schedule still executes normal tasks, failures, interrupts, motor work, and
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
