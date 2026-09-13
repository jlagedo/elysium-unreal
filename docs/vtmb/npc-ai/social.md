# NPC AI — Relationships, enemies, squads and reactions

Part of the [NPC AI oracle](./README.md). Sections moved verbatim from
`npc-ai-reverse-engineering.md` on 2026-09-13; their headers are the citation keys.

## Relationship table, exactly decoded

`InputSetRelationship` at `0x10273790` parses one or more triples:

```text
[entity name | entity class | player] [D_* token] [priority]
```

The exact token-to-native relation mapping is:

| Token | Internal value | Meaning |
|---|---:|---|
| `D_HT` | 1 | hate/hostile |
| `D_FR` | 2 | fear |
| `D_LI` | 3 | like |
| `D_NU` | 4 | neutral |

The parser accepts repeated triples in one input. A target ending in `*` wildcard-matches entities
and installs entity relationships. An exact target resolves an entity override. `player` has a
special class mapping when no named entity resolves; another unresolved token can resolve as a
classname and install a class relationship.

`AddClassRelationship` at `0x10332aa0` and `AddEntityRelationship` at `0x10332ca0` update an
existing row or grow their storage in groups of five. The rows retain target handle or class,
disposition, and numeric priority. The tables are save-backed. The diagnostic text describes a
priority range of 1-10, but the parser uses raw integer conversion without clamping; the installed
corpus includes zero and 99. `IRelationPriority` at `0x10333700` uses the exact entity row before
the class row, returns the row's raw integer, and otherwise returns 5 for a non-null actor or 0 for
null. Higher priority wins enemy arbitration; out-of-diagnostic-range values therefore remain
ordered rather than being clamped.

`IRelationType` at `0x10333340` resolves an entity override before a class override and otherwise
returns neutral, with additional special-flag/oblivious branches. Self and null relations resolve
neutral. The proven ordinary-table precedence is therefore:

```text
exact entity relationship
  -> class relationship
  -> neutral default
```

The exact placement and meaning of the additional special branches remain open, but the ordinary
relation-type and priority precedence is closed.

**Story 16b recovery (2026-09-12): the Troika composition `0x10299da0`, re-read.** Slot 404
(`0x650`) on a Troika NPC is `CAI_BaseNPCTroika::IRelationType`, and its arms in order: self →
0; null → 0; the target's cached `CBaseCombatCharacter*` (`target+0x9c`) carries `flags2 &
0x20000 D_INSANE` **and** my `m_hClosestPlayer` is live → if I do not hate that player (slot
`0x650` ≠ 1) and he is not my enemy (slot `0x2a0`) → **D_HT (1)** toward the insane target; the
target's Troika pointer (`target+0x98`) has a live `m_hFollowerBoss +0x647c` that I hate or that
is my enemy → D_HT; my own boss dead → the base table (`CBaseCombatCharacter::IRelationType`
`0x10333340`); my boss is the target → **D_LI (3)**; else `r = boss->IRelationType(target)`,
upgraded to D_HT when the boss hates the target, the boss's enemy is the target, or (the target
being an NPC) the target hates the boss or targets him as enemy; else `r`. The summary in
"`m_hFollowerBoss`" stands. UNRECOVERED: nothing on this path; the player's ally read
(`0x101755d0`) and `CNPC_VHuman::SelectIdealState` (`0x103851e0`) keep their summaries.

The map's `player_reaction` is an initial authored input to this combat relationship domain. A
later `SetRelationship` call can deliberately alter it.

## Enemy acquisition and replacement

The ordinary target-selection transaction is recovered from the pinned retail DLL. In
`CAI_BaseNPC::GatherConditions` (`0x1026ec30`), senses and hostile-category conditions are gathered,
the enemy-memory component refreshes its records, `ChooseEnemy` (`0x10279dd0`) runs, the
new-enemy-condition repair at `0x1026fb40` runs, and only then are conditions for the committed
current enemy gathered. Candidate discovery, memory, enemy choice and attack capability are
separate stages.

**`BestEnemy` enumerates the NPC's own enemy memory, never the world entity list.** It fetches the
memory component through vtable `+0x874` and walks the linked list at component `+0xc`: each entry
holds the remembered actor's handle at `+0x24`, its eluded byte at `+0x35`, and the next entry at
`+0x38`. `0x102e0210` walks the same list to answer the eluded test for one candidate. An actor the
NPC has never sensed has no entry, so it is not a candidate at all — perception is the admission
stage, and the arbitration below only ever ranks what memory already holds. This is why an NPC
authored with `vision 0` and a near-zero `hearing` cannot acquire an enemy, and therefore cannot
enter `COMBAT`, however hostile its `player_reaction` row.

Within that list `BestEnemy` (`0x102743c0`) considers only a handle that resolves to a living actor
other than self, passes the ordinary owner/flag and virtual `IsValidEnemy` gates, has relation
`D_HT` or `D_FR`, and does not carry the eluded marker. It then arbitrates as follows:

1. A reachable candidate beats an unreachable candidate.
2. At the same reachability class, larger `IRelationPriority` wins.
3. At equal priority, smaller integer enemy distance normally wins.
4. Visibility modifies that last comparison: a visible candidate can displace a farther unseen
   incumbent, while a closer unseen candidate displaces only an unseen incumbent. Selection is
   therefore not simply nearest hostile actor.

The current enemy is sticky. `ShouldChooseNewEnemy` (`0x10279d00`) declines a search when an
unnamed internal bit at `+0x14bc` (`0x10000`) is set. Otherwise it searches immediately when there
is no current enemy, when that actor is dead, when its enemy-memory record is marked eluded, or
when `SEE_HATE` (`0x43`), `SEE_DISLIKE` (`0x45`), `SEE_NEMESIS` (`0x5b`) or `ENEMY_DEAD` (`0x58`)
is present. A living, non-eluded current enemy with none of those conditions remains selected.
Notably, this retail body does not test `SEE_FEAR`, although a remembered `D_FR` candidate is
eligible in `BestEnemy`.

Even a better candidate does not automatically pre-empt the behavior in progress. The active
schedule's interrupt mask is consulted first: ordinary replacement needs `NEW_ENEMY` (`0x54`), an
eluded/went-null path needs `LOST_ENEMY` (`0x47`), and a dead target needs `ENEMY_DEAD` (`0x58`). A
schedule uninterested in the relevant condition keeps ownership and enemy choice is skipped; a
null enemy under such a schedule produces a retail warning rather than a plausible fallback.
This schedule gate is the starvation rule a one-frame global target scorer would miss.

When the choice changes, `SetEnemy` (`0x10279a50`) first transfers the old handle through the
last-enemy path and notifies the prior-enemy hook, then writes `m_hEnemy` at `+0x5ce0`; a non-null
enemy is also registered with the response system. `ChooseEnemy` clears stale had-enemy/player
memory, sets `ENEMY_DEAD` for a dead old target, sets or clears `NEW_ENEMY`, vacates an occupied
strategy slot and forgets the previous LOS claim. An eluded target that resolves to null adds
`LOST_ENEMY`, emits the lost-enemy sound hook, and fires `OnLostPlayer` or `OnLostEnemy` according
to the remembered target kind. A non-null replacement records whether it is the player or another
enemy. `OnFoundPlayer`/`OnFoundEnemy` belong to the sensory observation path; they are not aliases
for the `m_hEnemy` write.

This yields the implementation order:

```text
sense / hear / take damage / script relation
  -> update category conditions and enemy-memory records
  -> schedule interrupt-interest gate
  -> ShouldChooseNewEnemy
  -> BestEnemy eligibility and arbitration
  -> SetEnemy plus last-enemy, condition, slot and lost-output effects
  -> gather range, LOS, facing and attack conditions for that committed enemy
  -> state and class schedule selection
```

## Emotional disposition and social reaction are different domains

`SetDisposition` addresses an emotional/dialogue component at character offset `+0x98`, not the
combat relationship rows above. `DispositionTable.txt` defines presentation states including
`Neutral`, `Anger`, `Joy`, `Sad`, `Fear`, `Disgust`, `Apathy`, `Dead`, `Sitting`, `Bartender`,
`ChairDamaged`, `PrinceSitting`, and `BehindBack`. Neutral is the first/fallback inheritance entry.
These states select expression, stance/fidget, eye target, and blink behavior.

Separately, `reaction.txt` divides an RPG/social score into named bands:

| Lower boundary | Label |
|---:|---|
| 0 | Want To Kill |
| 20 | Hatred |
| 40 | Dislike |
| 60 | Neutral Reaction |
| 80 | Admire |
| 100 | Love |
| 9999 | Obsession |

`reactions000.txt` modifies that score based on histories and Disciplines; examples include
Megalomaniac scaling, a Close to Beast penalty against kindred and kine, and Presence bonuses.
Those tables serve social/RPG reaction calculations. A label such as `Hatred` in this scale is not
evidence that the native AI relation table contains the corresponding `D_HT` row.

## `m_hFollowerBoss` — the follower controller

`CAI_BaseNPCTroika+0x647c` is the resolved handle for the `follower_boss` keyfield
(`m_sFollowerBoss`, `+0x6478`; `follower_type` is `+0x6480`). Exactly two writers: the constructor
(`0xffffffff`) and **`SetFollowerBoss(const char*)`** (`0x102c44e0`), which resolves the name through
slot 559 (the `!player`/`!self`/`!enemy`/… resolver), refuses `this`, **`Error`s on a squad member**
("Followers can not be in squads. This functionality not implemented."), and on success sets
`m_bfNPCFrenziedFlags |= 0x3008`. Its arming paths: `Activate` and `OnRestore` from the keyfield,
the `SetFollowerBoss` entity input (`0x102c3350`), `CNPC_VPedestrian::Activate` clearing it, and the
discipline-effect applier's mind-control path (`0x101dfc20` → `0x102c51a0`). No Python setter. `[VtMB]`

**The controller** is virtual slot 607 (`+0x97c`, `0x102b93c0`, Troika-only, 64 tables), with one
dispatch site: `SelectSchedule` (`0x102af660`) case 1, where a non-zero return pre-empts patrol,
interesting places, the alert lookaround and `m_bReturnToInitialPos`. Its body is the boss distance
against three radii `FUN_102c4680` fills from Rules.txt `Npc_Follower_Info` per `follower_type`,
clamped `walkTo ≥ backAway + overlap`, `runTo ≥ walkTo + overlap`:

| Boss distance² | Schedule |
|---|---|
| handle dead | none (fall through) |
| `< FollowerDistanceBackAway²` (`+0x6484`) | `0x10c` `FOLLOWER_BACKAWAY`; writes `m_vSavePosition` = boss origin |
| `> FollowerDistanceRunTo²` (`+0x648c`) | `0x113` `FOLLOWER_FOLLOW_RUN`; `SetTarget(boss)` |
| `> FollowerDistanceWalkTo²` (`+0x6488`) | `0x112` `FOLLOWER_FOLLOW_WALK`; `SetTarget(boss)` |
| otherwise | `0x115` `FOLLOWER_WAIT`; `SetTarget(boss)` |

The `_F` in `COND_INSIDE/OUTSIDE_INTERRUPT_DIST_F` is Follow: those programs re-evaluate the band
continuously. The tasks `0x86`–`0x88` `TASK_FIND_FOLLOWER_BACKAWAY_{SIMPLE,NODE,ASTAR}` read the
handle and fail with `"NPC had no follower boss"` when it is dead. The other readers: `IRelationType`
(D_LI toward the boss, inherit the boss's relations, D_HT toward anyone whose boss I hate),
`GetFollowerBoss()` (slot 293), `CNPC_VHuman::SelectIdealState` (`0x103851e0`) (a follower whose
enemy is gone goes to alert, a non-follower to the hunt state `0xb` when the ConVar at
`DAT_1092447c` is on), the interest predicate (never investigate the boss), and
`CBasePlayer::UpdateClientActionState` (a follower reads as an ally on the target HUD). `[VtMB]`

A second, unrecovered ConVar sits beside it at `DAT_10924f74`.

**Story 16a recovery (2026-09-12).** `DAT_10924f74` is `debug_allow_move_facing` ("Sense and
investigate leftovers"); the `BACKAWAY` arm of the controller forwards `(boss, boss origin, 1.0,
1.0, 0)` to slot 517 (`0x814`) under it before returning `0x10c`. `SetFollowerBoss(name)`
(`0x102c44e0`) resolves through slot 559 (`0x8bc`), writes `+0x647c`, refuses `this`, `Error`s
when `m_iSquadDisconnected < 1 && m_pSquad`, then `0x102b52a0(0, 0)` (the `SetEnemy(NULL)` /
`SetTarget(NULL)` / hint-clear bundle the possession arm also calls) and `frenziedFlags |=
0x3008`. `SetFollowerType` (`0x102c4640`) → `0x102c4680(type)`: `0x101e8c90(&DAT_10739d08,
type, &backAway +0x6484, &walkTo +0x6488, &runTo +0x648c)` reads the `Npc_Follower_Info` row
of `Rules.txt`, then clamps `walkTo ≥ backAway + 10.0` and `runTo ≥ walkTo + 10.0`
(`_DAT_1044e664 = 10.0`, the "overlap"; DevMsgs on each clamp).

The ten programs, byte-read (`0x105e3100..0x105e536c`); every one interrupts on `NEW_ENEMY
SEE_ENEMY SQUAD_SEE_ENEMY SEE_FEAR LIGHT_DAMAGE HEAVY_DAMAGE GIVE_WAY INVESTIGATE_SOUND
INVESTIGATE_SIGHT IGNORE_UNKNOWN DETECTED_ATTACK` plus the distance conditions named:

- `0x115 FOLLOWER_WAIT`: `SET_NPC_FLAG TASKS_FACE_TARGET; SET_ACTIVITY ACT_IDLE;
  SET_INSIDE_INTERRUPT_DIST DIST:FOLLOWER_DISTANCE_BACKAWAY; SET_SPECIAL_DISTANCE_ACCUM
  DIST:FOLLOWER_DISTANCE_WALKTO; ADD_SPECIAL_DISTANCE_ACCUM DIST:FOLLOWER_DISTANCE_OVERLAP;
  SET_OUTSIDE_INTERRUPT_DIST DIST:ACCUM; WAIT 2.0; WAIT_RANDOM 1.0` + `INSIDE_INTERRUPT_DIST_F
  OUTSIDE_INTERRUPT_DIST_F`.
- `0x113 FOLLOWER_FOLLOW_RUN`: `SET_FAIL_SCHEDULE FOLLOWER_FOLLOW_FAILED;
  SET_SPECIAL_DISTANCE_ACCUM WALKTO; ADD OVERLAP; SET_INSIDE_INTERRUPT_DIST ACCUM;
  SET_TOLERANCE_DISTANCE DIST:ACCUM; GET_PATH_TO_TARGET; RUN_PATH; WAIT_FOR_MOVEMENT` +
  `INSIDE_INTERRUPT_DIST_F`.
- `0x112 FOLLOWER_FOLLOW_WALK`: `SET_FAIL_SCHEDULE FOLLOW_FAILED; ACCUM = RUNTO + OVERLAP;
  SET_OUTSIDE_INTERRUPT_DIST ACCUM; ACCUM = BACKAWAY + OVERLAP; SET_INSIDE_INTERRUPT_DIST ACCUM;
  SET_TOLERANCE_DISTANCE ACCUM; GET_PATH_TO_TARGET; WALK_PATH; WAIT_FOR_MOVEMENT` + both
  distance conditions.
- `FOLLOWER_FOLLOW_FAILED`: `SET_NPC_FLAG TASKS_FACE_TARGET; SET_ACTIVITY ACT_IDLE; WAIT 2.0;
  WAIT_RANDOM 1.0`.
- `0x10c FOLLOWER_BACKAWAY`: `SET_FAIL_SCHEDULE BACKAWAY_FAILED; ACCUM = WALKTO − OVERLAP;
  SET_OUTSIDE_INTERRUPT_DIST ACCUM; SET_TOLERANCE_DISTANCE 20; FIND_FOLLOWER_BACKAWAY_SIMPLE 0;
  WALK_PATH; WAIT_FOR_MOVEMENT` + `OUTSIDE_INTERRUPT_DIST_F`. `_BACKAWAY_FAILED`:
  `TASKS_FACE_TARGET; SET_ACTIVITY ACT_IDLE; ACCUM = WALKTO − OVERLAP; SET_OUTSIDE_INTERRUPT_DIST
  ACCUM; WAIT 0.1; WAIT_RANDOM 0.1; SET_SCHEDULE FOLLOWER_BACKAWAY_ASTAR`. `_BACKAWAY_NODE` /
  `_NODE_FAILED` mirror the pair with `FIND_FOLLOWER_BACKAWAY_NODE 0` (fail → `_ASTAR` too);
  `_BACKAWAY_ASTAR` uses `FIND_FOLLOWER_BACKAWAY_ASTAR 64` with fail `_ASTAR_FAILED` (`WAIT
  2.0; WAIT_RANDOM 1.0`, no transfer). So the ladder is simple → (fail) → A*; `_NODE` is reached
  by name only.

The `DIST:` operands are a second operand vocabulary the parser resolves like `NPCFlag:` —
`FOLLOWER_DISTANCE_BACKAWAY/WALKTO/RUNTO` read `+0x6484/88/8c`, `FOLLOWER_DISTANCE_OVERLAP` is
the 10.0 above, `ACCUM` the per-NPC accumulator the three `*_SPECIAL_DISTANCE_ACCUM` tasks
write; `TASK_SET_INSIDE/OUTSIDE_INTERRUPT_DIST` are the producers of `COND_INSIDE/OUTSIDE_
INTERRUPT_DIST_F 0x19/0x18` (tested against the boss each think). UNRECOVERED: their arms and
the accumulator's offset. The three find tasks (Troika `StartTask` idx 26–28): `0x86 SIMPLE`
(`0x102a2d9b`): no boss → `TaskFail(0x29)`; direction = normalise(me − boss) rotated by
`RandomFloat(−45, 45)` about the boss, point = boss + `backAway` × dir, walk-probed
(`0x102e6d70`, mask `0x202400b`, 100.0) → fail `TaskFail(7)`, else `SetGoal{type 4, tolerance
[0x1049a1b0]}`; `0x87 NODE` (`0x102a2f94`): `0x102edae0(boss origin, walkTo − 10, 50000.0)`
picks a node at least that far, fail 7; `0x88 ASTAR` (`0x102a3096`): `0x102edbb0(…, walkTo −
10, 50000.0)`, fail 7. All three complete on the `SetGoal` alone (no explicit `TaskComplete`).
`TASKS_FACE_TARGET` is an NPC flag (bit UNRECOVERED) the motor reads to keep facing
`m_hTargetEnt` while the wait runs.

## Reaction beyond immediate combat

### Dialogue and talk eligibility

`WillTalk` is independently scripted at 79 immediate sites. Damage can disable it, as in Bertram's
encounter, without that Boolean being the hostile relationship itself. Dialogue begin/end outputs
are also common (19 and 89 rows). A conversational NPC therefore combines at least dialogue
identity, talk eligibility, emotional disposition, current AI/script ownership, and combat
relationship.

### Fear, flight, and civilian behavior

Fear is a first-class native relation (`D_FR`), and the schedule corpus contains flee and cower
families plus interrupts such as `SEE_FEAR`. Civilian map outputs begin cower sequences on combat
sound. Scripts can call `FleeAndDie`. These are different tools:

- a fear relationship affects native target categorization;
- a flee/cower schedule decides ongoing behavior;
- a scripted cower sequence guarantees authored presentation; and
- `FleeAndDie` is a high-level scripted encounter verb with its own lifecycle consequence.

Implementing all four as "run away" would lose interruption, quest, and persistence semantics.

### Followers, patrols, and loitering

The script corpus uses `SetFollowerBoss`, `SetupPatrolType`, and `FollowPatrolPath`. The native
schedule library contains follow and patrol families, while map rows provide squad and ambient
place groups. These systems share navigation/motor infrastructure but differ in ownership:

- a patrol follows an authored route policy;
- a follower maintains a relationship/formation to a leader;
- an interesting-place user selects ambient opportunities; and
- a scripted schedule temporarily installs a goal or path.

The navigator must consequently identify its current owner and support clean transfer, failure,
and restoration rather than exposing one unqualified destination vector.

### Incapacitation, feeding, grapple, and death

The output surface distinguishes damage, half health, incapacitation start/end, grapple begin/end,
feeding begin/end, and death. These are lifecycle states with different gameplay and scripting
effects. Damage arithmetic and the health commit boundary are documented in
[combat-and-damage.md](../combat-and-damage.md); AI must consume the committed outcome and select the
appropriate state/schedule/output without reimplementing the damage resolver.

The ordinary NPC damage-to-AI transaction is now recovered. `CAI_BaseNPC::OnTakeDamageAlive`
(`0x10265ed0`) first calls the shared combat-character health commit. On success it fires
`OnDamaged`; when projected Source `m_iHealth` (`+0x210`) is no greater than half
`m_iMaxHealth` (`+0x208`), it also offers `OnHalfHealth`. It records the attack position and
attacker, updates enemy memory, and asks the class's light/heavy classifiers to set
`LIGHT_DAMAGE` (`0x4c`) and `HEAVY_DAMAGE` (`0x4d`). Damage at `+0x5d94` is accumulated for a
one-second window rooted at `+0x5d98`; exceeding 15 percent of Source max health sets
`REPEATED_DAMAGE` (`0x4e`). The Troika alive override (`0x102beda0`) preserves the full incoming
damage packet at `+0x660c`, gives a surviving attacker-memory record a five-second lifetime, and
notifies the active schedule. A special NPC flag can force the death path; its authored semantic
name remains unresolved.

`OnDamaged` and `OnHalfHealth` are normal entity outputs: `FireOutput` enqueues their actions in
`CEventQueue`. They do not recursively run a map-authored `SetRelationship` or Python payload in
the middle of the damage body. Zero-delay actions are serviced in the queue pass after entity
thinks, FIFO behind the equal-time cohort already queued; the native damage memory/conditions are
therefore committed before an authored hostility consequence is observed by a later AI pass.

**Damage condition is not animation.** The base idle/combat selectors may choose `SMALL_FLINCH`
(`0x14`): remember the flinched state, stop, and execute `TASK_SMALL_FLINCH`. The alert selector
chooses `TAKE_COVER_FROM_ORIGIN` (`0x19`) when the attack origin lies within its recovered facing
test, otherwise `ALERT_SMALL_FLINCH` (`0x07`) when a usable flinch sequence exists, or merely
`ALERT_FACE`. Separately, `CBaseCombatCharacter::DamageFlinch` (`0x103229d0`) randomly selects the
head or torso hit activity, derives `hit_yaw` from the incoming vector relative to actor yaw, adds
a random `[-30,+30]` degrees, and starts the gesture/layer with 0.1/0.3 fade values. Its complete
firearm caller chain remains open.

These reactions must not be collapsed:

- melee **block stagger** is the opposed-roll heavy-block band and selects `ACT_BLOCK_HEAVY`;
- a normal **hit/knockback** is the stronger unblocked melee outcome and can add impulse;
- **light/heavy/repeated damage** are AI conditions that may interrupt and select a schedule;
- **generic damage flinch** is a head/torso gesture/activity with hit direction; and
- **incapacitation** is a derived-class lifecycle with its own outputs, not the half-health test or
  a synonym for dying.

After the alive commit and NPC response, `CBaseCombatCharacter::OnTakeDamage` (`0x1032ef60`)
compares RPG `Health` damage against `Max_Health`. `Health < Max_Health` survives; at or above the
pool it calls `Event_Killed`. The shared death body (`0x1032b9b0`) enters life state 1 (dying),
cleans weapon/effect/ownership state, constructs the ragdoll-force envelope, and notifies killer
and game rules. `CAI_BaseNPC::Event_Killed` (`0x10265ad0`) then:

1. refuses the kill outright while the current schedule (`+0x5c38`) is `GetScheduleOfType(0x3a)`
   — schedule type **`NPC_FREEZE`**, not a death schedule. A frozen NPC does not die;
2. defers death while a **started** scripted sequence owns it, otherwise cancels script ownership;
3. cleans navigation, marks current/ideal NPC state 7 (dead), vacates strategy and squad state;
4. fires `OnDeath` once through its `+0x5bd4` guard and notifies the AI death path; and
5. emits the carcass sound or starts the corpse fade, then leaves the death schedule to the
   dead-state selector.

The Troika override (`0x102bf340`) composes game-specific cleanup around that base transaction:
release the hint/claims and feed-related ownership, notify owning/maker systems, run Python
`MarkAsDead('<targetname>')`, and update special partner/owner memory. The player uses a different
outer path (`0x10163af0`): it ends conversations/controllers/grapples and active weapon state,
notifies game rules, enters the player death action/screen (`vdata/Signs/death.txt`), and then uses
the same combat-character death cleanup. NPC and player can therefore kill each other through one
health threshold while retaining different AI, I/O and presentation consequences.

#### The dead-state schedule

`CAI_BaseNPC::SelectSchedule` (`0x1028a380`) is the only producer of either death schedule, and it
decides on the **ragdoll's availability**, not on the damage:

```
case NPC_STATE_DEAD (7):
    BecomeClientRagdoll(vec3_origin, forceBone = -1, 0)
        ? SCHED_DIE_RAGDOLL (0x2c)
        : SCHED_DIE         (0x2b)
```

`BecomeClientRagdoll` returns false when the model carries no ragdoll collide, so a model without
one animates its death instead.

| Schedule | Type id | Tasks | Interrupts |
|---|---|---|---|
| `DIE` | `0x2b` | `TASK_STOP_MOVING 0`, `TASK_SOUND_DIE 0`, `TASK_DIE 0` | none |
| `SCHED_DIE_RAGDOLL` | `0x2c` | `TASK_STOP_MOVING 0`, `TASK_SOUND_DIE 0` | none |

Every argument is zero; neither schedule carries a non-zero task argument, and neither declares an
interrupt condition. Task ids are `TASK_STOP_MOVING` `0x69`, `TASK_SOUND_DIE` `0x49`, `TASK_DIE`
`0x5f`; the death family also registers `TASK_DIE_IF_PLAYER_CANT_SEE` `0xdc`, `TASK_DIE_IMMEDIATE`
`0xdf`, `TASK_DIE_GIB` `0xe9`, `TASK_DIE_EXPLODE_GIB` `0xea` and `TASK_DIE_DUE_TO_PLAYER` `0xeb`.
Named Troika and per-NPC death schedules — `SCHED_TROIKA_D_VISION_OF_DEATH`,
`SCHED_TROIKA_D_SUICIDE`, `SCHED_TROIKA_DO_BLOODBOIL_DEATH_ACTIVITY`,
`SCHED_TROIKA_DO_INTERESTING_PLACE_DEATH`, `SCHED_TROIKA_FLEE_AND_DIE`, `SCHED_VGARGOYLE_DEATH`,
`SCHED_VANDREIBLOOD_DEATH`, the five `SCHED_VMING_XIAO_*_DIE` and `SCHED_VZOMBIE_ANIMATED_DEATH` —
are reached by their own selectors, not by the dead-state branch.

#### Death deferred under a scripted sequence

The deferral test in `CAI_BaseNPC::Event_Killed` reads the owning sequence, not an interruptibility
verb:

```
if (m_NPCState == NPC_STATE_SCRIPT && m_hCine != NULL) {
    if (m_hCine->m_sequenceStarted            // CCineNPC +0x5f91
        && (m_hCine->m_spawnflags & 0x2080) != 0x80)
    {
        copy the 0x4c-byte damage packet to CAI_BaseNPC +0x1a48; return;
    }
    CancelScript(m_hCine);                    // 0x101a8c30
    ...re-enable physics motion...
}
```

**Deferral is the default.** Once a sequence has started, the only combination that lets the kill
proceed immediately is spawnflag bit `0x80` set with bit `0x2000` clear. Bit `0x80` is Source's
`SF_SCRIPT_DONT_TELEPORT_AT_END`; bit `0x2000` is Troika-added, and its only two consumers are this
test and `CineCleanup`'s branch that snaps the entity onto its `Bip01` bone origin and angles. The
authored name of `0x2000` is not present in the image.

**The stashed packet is a dead store.** `CAI_BaseNPC +0x1a48 … +0x1a92` is written only by this
branch, appears in no datamap, and is read nowhere in `vampire.dll` or `client.dll`. The deferred
death resumes instead through `CineCleanup` (`0x1027d170`), whose tail reads
`if (m_iHealth < 1) { SetIdealState(NPC_STATE_DEAD); SetCondition(LIGHT_DAMAGE 0x4c); }`. A kill
that lands mid-sequence is therefore re-derived from the health counter when the script releases
the actor, and every field of the original damage packet — attacker, force, damage type — is lost.

`CineCleanup`'s other branch handles an NPC that did reach life state 1 (dying) inside the script:
health is forced to 0, `FSOLID_NOT_SOLID` is added, state becomes 7 with life state 2 (dead), the
hull collapses to `mins.z + 2.0`, and the corpse fades unless the sequence sets
`SF_SCRIPT_LEAVECORPSE` (`0x8`), in which case use/think/touch are merely cleared.

## Dialogue does not gate bystanders (2026-09-08)

`CBasePlayer::StartPlayerDialog` (`0x10178280`) writes the player's own `m_hDialogPartner`
(`SetDialogPartner` `0x10107050`), `m_bIsImmobilized (+0x19f7) = 1` (a SendProp with **zero
server-side readers**), forces `item_w_unarmed`, opens the UI and rebuilds the transmit list
(`0x100826b0`). The NPC side, `CAI_BaseNPCTroika::StartTalking` (`0x102c0270`), sets its own
partner. **Every one of the 25 readers of `m_hDialogPartner` reads `this->`**; likewise
`m_bInChoreoScene` (`+0x5bc4`, 8 self accesses). The one AI effect is in `CAI_BaseNPC::RunAI`
(`0x1026f110`): `if (!scriptOwner && !m_hDialogPartner) GatherConditions();` — **the dialogue
partner itself stops sensing, remembering and choosing enemies but still maintains its
schedule.** No other NPC can see the player's dialogue state; `IsValidEnemy`, `BestEnemy`,
`ShouldChooseNewEnemy`, `GatherEnemyConditions`, both `SelectIdealState`s and `SelectSchedule`
carry no dialogue term; `COND_NPC_FREEZE` (`0x75`) has one setter (`0x1027c1f0`, a command body
with no recovered caller). A hated NPC inside its effective radius acquires and chases the player
mid-conversation.

## Squads, decoded (2026-09-08)

**`CAI_Squad`** (0x78 bytes; ctor `0x103164c0`, link `0x103165f0`, dtor `0x10316440`; list head
`g_pSquadList 0x10936c68`): `+0x00 next`, `+0x04 name`, **`+0x08 m_memory` — an embedded
`AI_Enemies`**, `+0x1c EHANDLE m_hMembers[16]`, `+0x5c m_nNumMembers`, `+0x60
m_flSquadSoundWaitTime`, `+0x64 m_squadSlotsUsed` (32-bit `CVarBitVec`), `+0x70 m_hFocusEntity`,
`+0x74 m_flFocusExpireTimer`. `datamap_CAI_Squad` (`0x10315610`) holds the last five only; the
member array and name are not saved. NPC side: `m_pSquad +0x5da4` (not in the datamap),
`m_iSquadDisconnected +0x5bb0` (datamap, an int refcount like `m_iIsOblivious`), `m_SquadName
+0x5da8` (key **`squadname`**), `m_pEnemies +0x5d88`.

**The whole coupling is the shared memory.** `CAI_BaseNPC::GetEnemies()` (slot 541,
`0x10273e10`) = `m_iSquadDisconnected < 1 ? m_pEnemies : g_DisconnectedEnemies` (the global
`AI_Enemies` at `0x109203f0`). Joining (`InitSquad` `0x10273d30`: `m_pSquad == NULL &&
CapabilitiesGet() & bits_CAP_SQUAD 0x4000000`, then `FindCreateSquad(name)` `0x10315800` and
`SetSquadEnemies` slot 542 `0x10273dd0` — delete the private memory, point `m_pEnemies` at
`squad+8`; or the `SQUAD` tweak param → `CAI_BaseNPCTroika::SetSquad` `0x1029a930`) swaps the
NPC's enemy memory for the squad's. `FindCreateSquad`: `strcmpi` walk; a 17th recruit DevMsgs
`"Squad %s is too big"` and overwrites the 16th. `GetMember(i)` `0x103160c0` returns NULL for
every index when member 0 is disconnected. `RemoveFromSquad` `0x103158f0` (callers:
`Event_Killed` `0x10265ad0`, `0x1027ca30`, `SetSquad`, `CNPC_VCamera`) compacts the array and
calls slot 578 (UNRECOVERED name) on each survivor. Squads are freed only by `DeleteAllSquads`
`0x103162d0` at level shutdown. `SetSquadFocus` `0x10316660` / `GetSquadFocus` `0x103166b0`
(15 s expiry). No `GetLeader` exists; `CNPC_VChangBros` and `logic_squad_condition`
(`0x10135930`, `squad_name`, `SquadSeesPlayer`) walk `NumMembers`/`GetMember`.
**`LeaveSquad` `0x10316700` is `RET 4` — an empty stub.**

**Disconnect / reconnect.** `DisconnectFromSquad` `0x1026d050`: if not already disconnected and in
a squad, `LeaveSquad` (no-op) then **`g_DisconnectedEnemies->ClearMemory()`** (`0x102dfc10`) —
one global scratch memory shared by every disconnected NPC, wiped on each disconnect; then
`++m_iSquadDisconnected`. `ReconnectToSquad` `0x1026d0c0`: `--count`; at 0 → `squad->
AddSelfToSquadMemory(this)` `0x10316720`; `flags2 &= ~D_DISCONNECT_SQUAD`.
`TASK_DISCONNECT_FROM_SQUAD` is task `0xf5` (the only task of `SCHED_TROIKA_D_BRAINWIPE`);
other callers: `TASK_MAKE_OBLIVIOUS`'s `0x1026d130`, the possession and frenzy arms.

**Conditions.** `COND_SQUAD_SEE_ENEMY 0x31` producer `0x102b2730` (from Troika
`GatherConditions`): `m_iSquadDisconnected < 1 && m_pSquad && enemy && AI_Enemies::LastTimeSeen
(GetEnemies(), enemy) + 0.2 ≥ curtime` → set 0x31, and `COND_SQUAD_LOS_ENEMY 0x32` when
`HAVE_ENEMY_LOS 0x4a` — "someone sharing my memory saw him in the last 0.2 s", no broadcast.
`TASK_SQUAD_NEW_ENEMY` = task `0x138` → `CAI_Squad::SquadNewEnemy` `0x103161a0`: for each
member not disconnected, not already on this enemy, without `SEE_ENEMY`, still sharing the
memory → `SetEnemy`, `m_flLastAttackTime +0x5d9c = 0`, `NEW_ENEMY`. Other callers `0x1026f590`
(slot 460: idle/alert, `NEW_ENEMY`, enemy set → broadcast, or set `flags2 |= SQUAD_NEW_ENEMY` on
the entity at `CAI_BaseNPC+0x98`, UNRECOVERED), `0x102ae920`, `0x10374e50`, `0x10397380`.
**`SQUAD_NEW_ENEMY` (flags2 bit 13) and `IGNORE_SQUAD_SEE_ENEMY` (bit 6) have zero readers in
the image** — write-only bookkeeping; the port's clear of `SquadSeeEnemy` under
`IGNORE_SQUAD_SEE_ENEMY` is a divergence to remove.

**Saving** (`0x1030bfd0`): squad count, each squad's five datamap fields, then the private
`CAI_Memory` of every NPC that is disconnected or squadless. Membership is rebuilt on restore
from each NPC's `squadname` through `InitSquad`; order is not preserved; `m_pSquad` itself is
never serialized.

**Strategy slots ship dead.** The `squadslot` namespace (`0x10920484`) receives exactly two
names, `SQUAD_SLOT_ATTACK1/2` (`0x10316e80`, ids `0x3b9aca00/01`), with no consumer; every class
registers zero squadslots; there is no `OccupyStrategySlot`/`VacateStrategySlot`;
`m_squadSlotsUsed` is touched only by ctor, dtor and save. Do not build them.

UNRECOVERED: `CAI_Squad` `0x10316890/ab0/bc0/ec0/fa0/fd0` (memory-forwarding wrappers).
Closed 2026-09-12 (story 17): `CAI_BaseNPC+0x98` is the Troika self-pointer ("The three cached
downcasts"); slot 578 is an empty virtual and slot 168's Troika body returns `m_hLastEnemy` under
state bit 6 ("The think cadence, decoded", closed items); **`m_iMySquadSlot` is `+0x5dac`**
(`datamap_CAI_BaseNPC`, `FIELD_INTEGER`, between `m_SquadName +0x5da8` and `m_vecLastPosition
+0x5db8`; zero code readers, save-only); `m_bfNPCStateFlags` bit 3 is the PVS/LOS force ("The
think cadence, decoded").

## Story 29c-1, family Squad — the layer 0–9 bodies walked

### Slot 546 `SquadSlotName`, all 57 bodies (`0x101a6c00` + 56 species)

One behaviour written 57 times. The Troika line (`0x101a6c00`) is
`IdToSymbol(&DAT_10936c74, slotEN)`; every species override is
`IdToSymbol(&DAT_10936c74, SquadSlotLocalToGlobal(&DAT_<own id space>, slotEN))` — 29 bytes each,
identical but for the id-space global. The 56 spaces, in census order:
`CGenericSabbat_NPC 0x1093a350`, `CGeneric_NPC 0x1093a234`, `CGeneric_NPC_bathack 0x1093a314`,
`CNPC_Crow 0x1093a070`, `CNPC_VAndreiBlood 0x1093a470`, `CNPC_VAnimal 0x1093a4f4`,
`CNPC_VAsianVampire 0x1093a578`, `CNPC_VBach 0x1093a608`, `CNPC_VBatSwarm 0x1093a6d0`,
`CNPC_VBrujah 0x1093a788`, `CNPC_VCamera 0x1093a7bc` (shared by `CNPC_VCameraSecurity`),
`CNPC_VChangBros 0x1093aa70`, `CNPC_VChangBrosBlade 0x1093a8d8`, `CNPC_VChangBrosClaw 0x1093aa10`,
`CNPC_VCombatman 0x1093abb0`, `CNPC_VCop 0x1093ac40`, `CNPC_VDog 0x1093ad64`,
`CNPC_VFrenzyShadow 0x1093ae28`, `CNPC_VGangrel 0x1093aed8`, `CNPC_VGargoyle 0x1093b038`,
`CNPC_VGhoulCroucher 0x1093b0e0`, `CNPC_VGuard1 0x1093b1b4`, `CNPC_VHengeyokai 0x1093b27c`,
`CNPC_VHuman 0x1093b3c4`, `CNPC_VHumanCombatant 0x1093b47c` (shared by `CNPC_ProneDialog`),
`CNPC_VHumanCombatPatrol 0x1093b580`, `CNPC_VHunter 0x1093b5e8`, `CNPC_VLasombra 0x1093b680`,
`CNPC_VMalkavian 0x1093b6fc`, `CNPC_VManBat 0x1093b89c`, `CNPC_VMingXiao 0x1093bacc`,
`CNPC_VMingXiaoTentacle 0x1093bd80`, `CNPC_VMoleman 0x1093bdb8`, `CNPC_VNosferatu 0x1093bf30`,
`CNPC_VPedestrian 0x1093bffc`, `CNPC_VPlaceholder 0x1093c08c`, `CNPC_VSabbatGunman 0x1093c1d8`,
`CNPC_VSabbatLeader 0x1093c3d4`, `CNPC_VScurrying 0x1093c4e0` (shared by `CNPC_VRat`),
`CNPC_VSheriffMan 0x1093c568`, `CNPC_VSheriffSwarm 0x1093c680`, `CNPC_VStalker 0x1093c738`,
`CNPC_VTaxiDriver 0x1093c7f0`, `CNPC_VTest 0x1093c8bc`, `CNPC_VToreador 0x1093c948`,
`CNPC_VTremere 0x1093c9c8`, `CNPC_VTzimisce 0x1093ccc4`, `CNPC_VTzimisceHeadClaw 0x1093d1ac`,
`CNPC_VTzimisceRunner 0x1093d224`, `CNPC_VVampire 0x1093d2a4` (shared by
`CNPC_VPlayerController`), `CNPC_VVampireBoss 0x1093d30c`, `CNPC_VVentrue 0x1093d394`,
`CNPC_VWerewolf 0x1093d6d4`, `CNPC_VWolfMorph 0x1094028c`, `CNPC_VYukie 0x10940314`,
`CNPC_VZombie 0x109403e0`.

**Every one of those id spaces is empty, so every species answers `<<null>>` for every id.**
`CAI_ClassScheduleIdSpace`'s constructor (`0x102ea090`) takes an `isRoot` flag: true leaves
`{globalBase 0, localBase 0, localTop -1}`, false leaves `{-1, 9999, -1}`. Exactly one space in the
image is constructed true — the root `DAT_10920484` (`staticinit_10265660`); all 56 species
static-inits pass `0`. `Init` (`0x102ea0e0`) rewrites the range only when `+0x0c != -1`, which at
static-init time it is, so it binds the namespace and the parent and leaves the range alone.
`SquadSlotLocalToGlobal` (`0x102ea2d0`) walks the chain testing `localBase != 9999 && localBase <=
id <= localTop`; 9999 refuses at every species level and `localTop == -1` refuses at the root, so
the answer is -1 and `IdToSymbol` (`0x102ea020`) answers the literal `<<null>>` for -1. The global
namespace itself holds two symbols and no more — `SQUAD_SLOT_ATTACK1` = 1000000000 and
`SQUAD_SLOT_ATTACK2` = 1000000001, both from `0x10316e80`, and those two strings are the only
`SQUAD_SLOT` strings in `vampire.dll`. This confirms "Strategy slots ship dead" above from the
other side: not only is there no consumer, there is no species id to consume.

### `0x10273d30` / `0x10369bd0` — `InitSquad`, the Troika line and the camera

`0x10273d30`: with `m_pSquad == NULL` and `CapabilitiesGet() & 0x4000000` (`bits_CAP_SQUAD`, slot
513), an unset `m_SquadName` DevMsgs `"WARNING: Found %s that isn't in a squad but not supposed to
be solo"` and returns; otherwise `m_pSquad = FindCreateSquad(this, m_SquadName)` (`0x10315800`)
then slot 542 `SetSquadEnemies` (`0x10273dd0`). Both exits return `m_pSquad != NULL`, so an NPC
without the capability answers false without warning. `CNPC_VCamera` / `CNPC_VCameraSecurity`
(`0x10369bd0`) replace only the middle: `FindSquad(name)` (`0x10315790`) first, and on a miss
`FindCreateSquad` followed **immediately** by `RemoveFromSquad(squad, this)` (`0x103158f0`) — the
camera wants the squad object for the shared memory and not membership in it. Slot 542 runs on both
arms.

### `0x1029a930` — `CAI_BaseNPCTroika::SetSquad`

The `SQUAD` tweak param's move, in two halves. First the old ownership: already squadded →
`RemoveFromSquad(m_pSquad, this)` and `m_pEnemies = NULL`; not squadded → destroy the private
`AI_Enemies` (`0x102e0730` then `operator delete`). Then the new one: `FindCreateSquad(this,
name[0] ? name : NULL)`; a hit writes `m_pSquad` and points `m_pEnemies` at `squad + 8`, a miss
clears `m_pSquad` and gives the NPC a fresh private `AI_Enemies` (`operator new(0x14)` +
`0x102e06f0`). Finally `m_iSquadDisconnected > 0 && m_pSquad` calls `LeaveSquad` (`0x10316700`),
which is `RET 4` and does nothing. `SetSquad` never writes `m_SquadName` (`+0x5da8`): the keyfield
and the object are separate words.

### `0x1028ae60` — vacate the squad slot

Gates in order: `m_iMySquadSlot != -1`, `m_iSquadDisconnected < 1`, `m_pSquad != 0`. Then it reads
the squad's `m_squadSlotsUsed` word for `slot >> 5` and DevMsgs `"ERROR: Vacating an empty slot!"`
when the bit is already clear, re-tests the disconnect count (a second read that would yield NULL
if it could ever be reached, which the first gate prevents), clears the bit and writes
`m_iMySquadSlot = -1`. -1 is therefore the field's "no slot" sentinel. Retail name unrecovered.

### `0x102781a0` — same-squad test

`if (!other) return false; if (!m_pSquad) return false; return m_pSquad == other->m_pSquad;` — the
two refusals are separate arms, so **two squadless NPCs do not share a squad**. Retail name
unrecovered.

### `0x1036e2f0` — `CNPC_VChangBros::GetOtherBrother`

`m_iSquadDisconnected < 1` and a live `m_pSquad`, then a walk of `0 .. NumMembers()` re-reading
`NumMembers()` (`0x103160a0`) every iteration, `GetMember(i)` (`0x103160c0`), `RTDynamicCast` to
`CNPC_VChangBros` and the first hit that is not `this`. Otherwise 0. The disconnect gate is first,
so a brainwiped brother finds nobody even while the squad still holds him.

### `0x1036e820` — `CNPC_VChangBros::ReadyForUnited`

`GetCurSchedule()` (`0x1028a150`) and its id word (`CAI_Schedule+0x00`) against `0x15a` or `0x15b`;
false when there is no schedule. Nothing else.

### `0x1036d100` — `CNPC_VChangBros::SelectUnitedNode`

Walks the global `CAI_Hint` list from `DAT_10925450` following `+0x5d8`, counting nodes whose
`m_nHintType` (`+0x5dc`) is 18000. `m_ChangType == 0` returns the first match; `m_ChangType == 1`
returns a match only once the counter is already above zero, that is the second. Any other
`m_ChangType` walks the whole list and returns 0, so only the two brothers can ever claim a united
node.

### `0x10399610` — `CNPC_VMingXiao::CoordinateTroops`

One index per call and no loop: `id = m_iCoordinateTentacleID` (`+0x6740`); a live
`m_rhSeveredTentacles[id]` (`+0x66a8`) goes to `0x103998d0` and a live `m_rhProxies[id]`
(`+0x668c`) to `0x103999f0`; then `++id`, wrapping to 0 past 5. Both arrays are six `EHANDLE`s, so
the boss touches one tentacle and one proxy per think and comes back round every six.
`0x103998d0` re-aims a tentacle that is neither running schedule `0x163`/`0x165` nor out of state
2, gated on a distance and a 2-D dot against `+0x6290`/`+0x6294`; `0x103999f0` toggles a proxy pair
through `0x1039aaf0(x, 0|1)` on two distance/dot tests against the enemy. Both are rows of their
own.

### `0x102bf5d0` — alert a nearby ally to an attacker

**Corrects story 29c's one-line walk**, which read `vtable+0x650` as a not-dead test: `0x650 / 4`
is slot 404, `IRelationType`, and the constant compared is `D_LI` (3). The body, arm by arm, with
`this` the ally being told and the argument the attacker: the attacker is non-null; `m_NPCState`
(`+0x5cc0`) is 1 (IDLE), 3 (COMBAT) or the custom `0xb` — **ALERT (2) is not admitted**;
`attacker->m_pCombatCharacter` (`+0x9c`) is non-null; `IRelationType(attacker) != D_LI`; and either
the squared distance between the two `GetAbsOrigin`s is under `_DAT_1049aea0` or `FVisible(attacker,
0x2804091, 0, 0)` (slot 201) **and** `FInViewCone(attacker)` (slot 363) both pass. The effect is
`0x102bf560`: unless `m_bIgnoreDetectedAttack` (`+0x65f5`), store the attacker in
`m_hDetectedAttacker` (`+0x65c0`) and set `m_flDetectedAttackExpireTime` (`+0x65c4`) to curtime
plus `_DAT_10454110`. **Unrecovered:** the literals `_DAT_1049aea0` and `_DAT_10454110`
(`docs/vtmb/combat-and-damage.md` recovers the radius as 150 Source units and the retention as five
seconds from the same chain); retail's `0xb` state has no counterpart in this port's state set.

### `0x103a48b0` — `IRelationType` for `CNPC_VFrenzyShadow` / `CNPC_VPlayerController` / `CNPC_VWolfMorph`

Four answers, in order: a null target is `D_ER` (0); a target equal to the resolved
`m_hFriendPlayer` (`+0x60ac`) is `D_LI` (3); a target whose `m_pCombatCharacter` (`+0x9c`) is
non-null, whose `m_bIsBCCTargetable` (`+0x1480`) is set and whose `m_bScriptHidden` (`+0x00f4`,
read through `0x100b5190`) is clear is `D_HT` (1); anything else is `D_NU` (4). No relationship
table, no disposition and no squad term — these three classes hate everything they can see except
the one player they were told to like.

### `0x102c4470` — name the follower boss

`name = boss->m_pPlayer (+0xa8) ? "!player" : boss->m_iName (+0x26c)`, then
`SetFollowerBoss(name)` (`0x102c44e0`) and `m_sFollowerBoss (+0x6478) = name[0] ? name : NULL`, so
an unnamed boss stores NULL rather than the empty string. The argument is dereferenced without a
null test.

### `0x102c4680` — resolve and clamp the follower distances

`0x101e8c90(&DAT_10739d08, type, &m_flFollowerDistanceBackAway +0x6484, &…WalkTo +0x6488,
&…RunTo +0x648c)` reads the `Npc_Follower_Info` row of `Rules.txt`, then two clamps with four
DevMsgs each: `walkTo < backAway + 10.0` raises `walkTo` to `backAway + 10.0`, and `runTo < walkTo
+ 10.0` raises `runTo` to **the clamped** `walkTo + 10.0`. The overlap constant is
`_DAT_1044e664 = 10.0`. `SetFollowerType` (`0x102c4640`) is this call followed by
`m_sFollowerType (+0x6480) = type[0] ? type : NULL`.

### `0x101a8130` — the named-master lookup

`name = *(char**)(this + 0x5f5c)`; `gEntList.FindEntityByName(NULL, name, 0, 0)` (`0x100f7770` over
`DAT_106eb5d8`); the hit must have a non-null `m_pBaseNPC` (`+0x94`, the `CAI_BaseNPC` self-downcast
cache), that is, be an NPC; then `RTDynamicCast(hit, 0, 0x10538764, 0x105947c8, 0)` and the cast
result is the answer. **Unrecovered:** the RTTI type descriptor `0x105947c8` has exactly one
referrer in the whole image — this body — so the class the cast admits is not named anywhere in the
corpus; the corpus records no call site either, so the receiver class is unconfirmed; and `+0x5f5c`
is the second `COutputEvent` of the NPC's output block in `layout.md`, which does not hold a
`char*`.

## The melee coordinator (story 29c-1, family TroikaHelpers)

### The melee entry and exit quartet `0x102b5650`, `0x102b57c0`, `0x102b5880`, `0x102b5900`

_Recovered 2026-09-13, story 29c-1._

Slots 599–602 are one behaviour written twice. `CAI_BaseNPCTroika` fills all four for 18 classes and
a `CNPC_VAndreiBlood`-line copy fills them for 38–40 more; six species replace each slot outright.
599 and 600 are byte-identical between the two lines, and their copies are `0x10385ab0` and
`0x10385c30`.

Slot 599 decides melee ENTRY from an enemy already committed. `m_bfNPCFrenziedFlags & 2` or a live
`GetFollowerBoss()` (slot 293) enters outright and sets `m_bInMelee`. Otherwise `curtime <
m_flMeleeCanEnterTimer` (`+0x6070`) refuses and clears the latch. Past the timer, three terms must
all hold: the enemy is within twice the melee range (`DAT_10924a1c`) OR this body has no usable
ranged weapon (slot 308); `m_flEnemyHeightDiff` (`+0x626c`) is at or below `_DAT_10451acc` OR
`COND_ENEMY_UNREACHABLE` (0x59) does not stand; and either `m_bfNPCFrenziedFlags & 0x1000` bypasses
the coordinator or `0x1025db70` admits this NPC. On entry it sets `m_bInMelee`, arms
`m_flMeleeMustLeaveTimer` (`+0x6074`) with `curtime + RandomFloat(7.5, 15.0)` — the immediates are
`0x40f00000` and `0x41700000` — and fires the global melee event `(*DAT_10924edc)->vfunc1()`.

Slot 600 is the attacker-side entry, reached from slot 322 (`0x102a0910`) when this NPC swings. It
requires the active weapon's capability word (slot 360, `+0x5a0`) to carry `0x18000` and
`m_bInMelee` to be clear; then `0x1025dca0` admits or refuses. The entry write is the same triple as
599's, with the same 7.5–15.0 draw.

Slot 601 is the exit. It fires the SAME global event FIRST, clears `m_bInMelee`, and — only when
slot 308 answers a usable ranged weapon — arms `m_flMeleeCanEnterTimer` with `curtime +
RandomFloat(5.0, 10.0)`. It then releases the coordinator slot (`0x1025ddd0`). **The Troika body
guards that release on `m_pAttackCoordinator != 0`; the `CNPC_VAndreiBlood` copy `0x10385cf0` does
not.** That is the whole of the difference between the two: both fire the event first.

Slot 602 asks whether to LEAVE. `m_bfNPCFrenziedFlags & 2` refuses, a live `GetFollowerBoss()`
refuses, and **on the Troika line only** a null `m_pAttackCoordinator` refuses. `0x10385d70`, the
`CNPC_VAndreiBlood` copy, drops that third test and nothing else, so the two bodies diverge on
exactly one instruction sequence. Past the gates: with no usable ranged weapon, an enemy at or
beyond twice the melee range plus a full coordinator (`0x1025db50`, `coord[4] < coord[0]`) answers
true; with one, `m_flMeleeMustLeaveTimer <= curtime` answers true. Everything else falls through to
`0x1025de90`, a linear scan of the coordinator's handle array that answers "this NPC is not
registered".

**Unrecovered:** `DAT_10924a1c`'s name and default and `_DAT_10451acc`'s value, both of which live in
uninitialised `.data`; the names of frenzied bits `0x2` and `0x1000`; and what `DAT_10924edc`'s
`vfunc1` does with the event, which is fired on both entry and exit and so is one event and not two.

### Binding an attack coordinator by name `0x102c48b0`

_Recovered 2026-09-13, story 29c-1._

Slot 608 takes a name and walks three global coordinator pointers — `DAT_1090fbec`, `DAT_1090fbf0`,
`DAT_1090fbf4` — in that order, skipping a null. Each candidate's name comes from
`thunk_FUN_1025e120`, and the comparison is an inlined two-bytes-at-a-time `strcmp`. The first match
caches the pointer at `m_pAttackCoordinator` (`+0x65e8`) and the name at
`m_sAttackCoordinatorName` (`+0x65ec`), storing NULL for an empty name, and answers true. A null or
empty argument is refused before the walk.

**Unrecovered:** what a coordinator object is beyond the three fields its five entry points read — a
cap at `+0x00`, a handle array at `+0x04` and a live count at `+0x10`.

### The follower-distance ladder `0x102b93c0`

_Recovered 2026-09-13, story 29c-1._

Slot 607 answers a schedule number for a follower's distance from `m_hFollowerBoss` (`+0x647c`). An
unresolvable handle answers 0. Otherwise it squares the distance between the two abs-origins and
walks three rings **in this order**: inside `m_flFollowerDistanceBackAway` (`+0x6484`) it stamps
`m_vSavePosition` (`+0x5dd0`) with the boss's origin and answers `0x10c`, having first — behind the
cvar `DAT_10924f74` — queued a facing target at the boss through slot 517 with `(1.0, 1.0, 0)`;
beyond `m_flFollowerDistanceRunTo` (`+0x648c`) it calls `SetTarget(boss)` and answers `0x113`;
beyond `m_flFollowerDistanceWalkTo` (`+0x6488`) the same and `0x112`; otherwise the same and `0x115`.
Each miss branch stamps the selector trace at `+0x1b30`/`+0x1b34` with lines `0x6090`, `0x6097`,
`0x609d` and `0x60a2`.

**Unrecovered:** `DAT_10924f74`'s name and default (shared with the whole facing-target family), and
what the four returned numbers name — they are schedule ids the registry does not carry.

### Forgetting a removed enemy `0x102b5120`

_Recovered 2026-09-13, story 29c-1._

Slot 56 takes an entity, two vectors and a string and reads only the entity. A null argument returns.
The entity's `+0x200` must be non-zero; then, if the entity is the one `m_hLastEnemy` (`+0x1a94`)
resolves to, `thunk_FUN_10279b70(this, NULL)` invalidates the handle to `0xffffffff`. The three
trailing arguments are never read.

**Unrecovered:** the word at `entity+0x200`, which no other body in layers 0–9 reads, and therefore
what makes an entity eligible to be forgotten.
