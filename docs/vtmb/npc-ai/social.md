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

**The six "wrappers", closed 2026-09-19 (0018 story 14).** Two opencode walks and an adjudication
pass; `0x102dfc10` and `0x102b5120` re-read here. Only three are squad code, and a fourth and fifth
sit beside them: each walks `members[0 .. count-1]` (`squad+0x1c`, count `+0x5c`), skips a handle
that does not resolve, applies NO member-0 rule and NO `m_iSquadDisconnected` test, and dispatches
one NPC vtable slot on the member with its arguments forwarded verbatim. They are what a SHARED
`CAI_Memory` calls where a private one (`memory+8 == 0`) calls the same slot on its one outer NPC.

| fan-out | member slot | caller in `CAI_Memory` | Troika body | base body |
|---|---|---|---|---|
| `0x103167f0` | 54 `+0xd8` (enemy) | `RefreshMemories 0x102df320`; AND of the answers, first false ends it | `0x102b50b0`: true unless the argument is my current enemy, a schedule runs and condition `0x47` does not interrupt it | `0x10026910` false |
| `0x10316890` | 55 `+0xdc` (enemy, pos, dir) | `UpdateMemory 0x102df700`, new-record arm only | `0x102b5100` empty | empty |
| `0x103169a0` | 56 `+0xe0` (enemy, record's six dwords, tag) | `0x102df320`, `0x102dfaa0`, `ClearMemory 0x102dfc10` — a record being dropped | `0x102b5120`: when the enemy's `m_lifeState` (`+0x200`) is non-zero and it is my `m_hLastEnemy` (`+0x1a94`), clear `m_hLastEnemy` | empty |
| `0x10316ab0` | 57 `+0xe4` | `0x102dfd90` (then `record+0x35 = 1`) | `0x102b51a0` empty | empty |
| `0x10316bc0` | 58 `+0xe8` (enemy) | `0x102dfed0`, `0x102e0290`, `0x102e0470`, on a lookup miss | `0x102b51c0` empty | empty |

So the whole observable effect of the five is slot 54's veto on dropping a record and slot 56's
`m_hLastEnemy` clear; no class below the Troika line overrides any of them. `0x10316ec0`,
`0x10316fa0` and `0x10316fd0` are not squad code: the `g_ppszTaskFailureText` self-check (table
`0x106152b0`, 42 rows), `TaskFailureToString` (`code < 0x2a`, signed) and a task-name id lookup.

Squad facts the same walks fixed. The object is `0x78` bytes, allocated only by `FindCreateSquad`;
the global list head is `0x10936c68`, linked through `squad+0`, name at `+4`. The overflow test is
`count + 1 <= 16`; the message is `"Error!! Squad %s is too big!!! Replacing last member"`.
`RemoveFromSquad` shifts down from the found index, then `count--` clamped at 0; the per-survivor
slot 578 is `0x101a6cc0`, a bare `RET 4` with NO override in any of 77 classes — dead. A squad is
never freed or unlinked in a level: an empty squad stays listed (`CNPC_VCamera::InitSquad
0x10369bd0` creates one and immediately removes itself), `Event_Killed` and `UpdateOnRemove` leave
`m_pSquad` pointing at it, and `DeleteAllSquads` runs only from
`CAI_SystemHook::LevelShutdownPostEntity` (`0x102cc410`). `GetMember`'s member-0 rule fires only
when member 0's handle RESOLVES and its `m_iSquadDisconnected > 0`; a stale member 0 bypasses it.
`UpdateOnRemove`'s guard `0x10315ec0` skips the removal for a disconnected NPC.

`DisconnectFromSquad 0x1026d050`, listing re-read: `m_iSquadDisconnected > 0` skips to the
increment; otherwise `LeaveSquad` (only when `m_pSquad`) and then — NOT gated on having a squad —
`ClearMemory` on `g_DisconnectedEnemies` with the tag `"…AI_BaseNPC.cpp(3894) :"`; the increment
runs on EVERY call. `ClearMemory` pops each record, runs the slot-56 notify for it, and frees it.
The only three stores to `+0x5bb0` in the image are that increment and `ReconnectToSquad`'s
decrement and zero-floor (`0x1026d0ce`, `0x1026d0e8`), after which it clears `m_bfAINPCFlags2`
bits 31 and 23 unconditionally. Callers: task `0xf5` (`102a536e`: disconnect, `flags2 |=
0x80800000`, complete), `DoPossession`, `DoFrenzy`, the oblivious wrapper `0x1026d130`, and the
ManBat minion sweep `0x1038fc80`; reconnect from its twin `0x1038fd40` and from `0x101def10` when
`flags2 & 0x800000`.

`logic_squad_condition` resolves its squad ONCE (`0x10135bd0` caches `FindSquad(squad_name)` at
`+0x488`, never re-resolved or invalidated); since squads die only at level shutdown the pointer
cannot dangle. `Test 0x10135c10`: invalid condition id → false; no squad → warning and false; no
members → false, silently; the first member with `HasCondition` → true, re-reading the count each
pass. `SquadSeesPlayer` is NOT a condition: the one string (`0x105904fc`) is a Python method
(`0x10196f30`) that scans NPCs within 4096 of the player and compares squad names — and reads
address 4 for a disconnected NPC, a latent fault.

UNRECOVERED: the engine-side dispatcher of `LevelShutdownPostEntity`; the fill path of the
condition-name registry `0x109203dc`.
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

The object itself is recovered in the next section. The binder is the only reader of the three
globals, and its only dispatch site is `CAI_BaseNPCTroika::Precache` (`0x10298ad0`, slot 608 through
`+0x980`) with the literal `"Normal"`, result ignored; so "Player" and "Boss" are built and never
bound by anything in `vampire.dll`. `+0x65e8` is not in the datamap; `+0x65ec` is (SAVE) and has
no reader in the image.

### The attack coordinator object — `0x1025d880` … `0x1025df40` (2026-09-19, 0018 story 15)

_Recovered 2026-09-19 by two independent opencode walks and an adjudication pass; the builder, the
evict path `0x1025dca0`, the angular query `0x1025df40` and the lifetime chain re-read from the
listing._

**Lifetime.** Three plain heap objects of `0x28` bytes — no vtable, no RTTI, no datamap, not an
entity, never saved. `0x1025d880` allocates each and constructs it as `(cap 2, name)` —
`DAT_1090fbec` "Normal" (`0x105c89dc`), `DAT_1090fbf0` "Player" (`0x10547404`), `DAT_1090fbf4`
"Boss" (`0x105a0c78`); an allocation failure stores NULL. It runs as the LAST statement of the
`CWorld` constructor (`0x1023b840 -> thunk 0x10011bcb -> 0x1028d770 -> thunk 0x1000a01f`, reached
from the `worldspawn` factory `0x1023ae40`); `0x1028d770` also loads `InterestingPlaceTypeList` and
precaches the two dialog emitters first. `0x1025d940` frees all three (`0x1025da20` purge, then
delete) and zeroes the globals; it is the FIRST statement of the `CWorld` destructor (`0x1023ba30 ->
thunk 0x100134ad -> 0x1028d800`). So every map starts with three empty coordinators and no handle
survives a level change. The corpus callers index misses this double-thunk chain; `vtmb_grep
thunk_FUN_1028d770` finds it.

**Layout** (constructor `0x1025d9d0`, `RET 8`): `+0x00` cap (int, 2 for all three, written only
here), `+0x04` `EHANDLE*` array, `+0x08` allocated count, `+0x0c` grow step (8), `+0x10` live
count, `+0x14` array mirror, `+0x18` `char name[16]` (`Q_strncpy` 15). `0x1025e120` returns
`this+0x18`. Growth `0x1025e140`: 0 → 8, then +8.

**The entry points**, every handle resolved through `0x10566458` (`h & 0x1fff`, serial `h >> 13`,
entity at cell `+4`, serial at `+8`); an unresolvable handle is skipped, never purged:

| address | answer |
|---|---|
| `0x1025db50` | `count < cap` (signed `SETL`): **"has room"**, not "is full" |
| `0x1025db70` | add: a live entry already resolving to this NPC answers 1 with no insert; `count >= cap` tail-calls `0x1025dca0(npc, 1)`; else append at `array[count]`, `count++`, answer 1. A NULL NPC also tail-calls `0x1025dca0` (unbounded recursion with room; no caller passes NULL) |
| `0x1025dca0` | add-or-evict `(npc, useDist)`: with room, `0x1025db70`. Full: threshold = the candidate's `m_flEnemyDist` (`+0x6268`) when `useDist`, else `0.0f` (`0x104454c4`); scan `0..count-1` for the member with the STRICTLY greatest `m_flEnemyDist` above the running best (`FCOM`/`TEST AH,5`/`JP`: equal and NaN keep the earlier); none → answer 0, nothing changed; else release that member (`0x1025ddd0`) and `0x1025db70` the candidate |
| `0x1025ddd0` | release: first entry resolving to the NPC is overwritten by the LAST entry (`memmove(&a[i], &a[count-1], 4)`), `count--`; order is not preserved; not found or NULL: no-op |
| `0x1025de90` | 1 when the NPC is **absent** (or NULL, or count 0), 0 when present |
| `0x1025df40` | angular separation, `(npc, enemy)`: 0 when either is NULL or `count < 2`; else for every other member the signed `AngleDiff` (`0x1013d580`) between its bearing to the enemy and this NPC's (`atan2` over slot-220 positions, ×57.29578), keeping the smallest magnitude from 360.0; ≤ 0.0 answers `-1`, else 1. A stale member handle resolves to NULL and is then dereferenced — a latent crash. Caller: `CAI_BaseNPCTroika::StartTask 0x102a1910` |

**Callers**, all through thunks: `0x1025db70` from slot 599 (`0x102b5650`, the `CNPC_VAndreiBlood`
copy `0x10385ab0`, `0x103c19e0`, `0x103c3960`); `0x1025dca0` with `useDist = 0` from slot 600
(`0x102b57c0`, `0x10385c30`, `0x103c1a60`, `0x103c39e0`) and with `useDist = 1` only from
`0x1025db70`'s full arm; `0x1025ddd0` from slot 601 (`0x102b5880`, `0x10385cf0`, `0x103c1ad0`,
`0x103c3a70`); `0x1025db50` and `0x1025de90` from slot 602 (`0x102b5900`, `0x10385d70`,
`0x103c1b10`, `0x103c3ab0`). `0x10385ab0` calls `0x1025db70` TWICE: once before the
`m_bfNPCFrenziedFlags & 0x1000` test with the result discarded (`0x10385b5d`), and again only when
the bit is clear, whose result decides (`0x10385b7b`); the duplicate scan makes the second call a
no-op on success.

**Release sites.** Slot 601 is the only remover, dispatched from 23 `CALL [reg+0x964]` sites:
`CAI_BaseNPCTroika::Event_Killed` (unguarded) and `UpdateOnRemove` (guarded on `+0x65e8`), and the
`SelectScheduleMeleeCombat` arms of `CAI_BaseNPCTroika` (`0x102b6c30`), `CNPC_VHuman`,
`CNPC_VAsianVampire`, `CNPC_VMingXiao`, `CNPC_VSabbatLeader` and `CNPC_VSheriffMan`, plus
`0x102b6fe0` and `0x102b8620`, each behind its own `m_bInMelee` / slot 602 / condition guard
(`0x59`, `9`, `0x1a`, `0x3a`, `0x48`).

**Correction.** `population.md`'s surface row named `0x1025db50` "is full, true when the count has
reached the cap"; the listing answers the opposite polarity, and slot 602's "full" arm is the
`0x1025db50() == 0` case. The inequality there is inclusive: `2 * range <= m_flEnemyDist`
(`FCOMP [ESI+0x6268]` / `TEST AH,0x41` / `JP`), NaN not taking it.

**Unrecovered:** any binder for "Player" or "Boss" outside `vampire.dll`; the engine-side order in
which a save-game load re-instantiates `worldspawn` (and so rebuilds the objects) relative to the
restored NPCs' precache.

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

## Story 29d, family Social10 — talking, the tweak file and the dialogue packet

_Recovered 2026-09-14, story 29d._

Ten rows: whether an NPC will talk, what ends a line, what the tweak file may set, and what the
dialogue packet carries. Four are `CAI_BaseNPCTroika` bodies and land in
`Substrate/ElysiumNpcKernelSocial10.{inl,cpp}`; three are `CDialog`'s and land in
`Public/ElysiumDlg.h` / `Private/Scripting/ElysiumDlg.cpp` under `namespace ElysiumDlgRetail`,
because the overlay's target for all three is `FElysiumDlgConversation::EnterNpcLine`; two are
`CBasePlayer`'s and land on `FElysiumPlayer`; and two were already `present`. The suite is
`Elysium.Substrate.NpcKernelSocial10.` and covers all of them.

### `CAI_BaseNPCTroika::CanTalk` `0x102c21c0`

_Recovered 2026-09-14, story 29d._

Slot 295, `vtable +0x49c`, 244 bytes. One nested chain in which every failure falls out to
`102c22ad XOR AL,AL` and only the innermost line reaches `MOV AL,1`. Sixty census classes share it;
`CPayphone::CanTalk` (`0x101aaee0`, seven arms, story 29c-1) is the one override.

The gates, in the listing's order:

| # | Listing | Gate |
|---|---|---|
| 1 | `102c21c8` | the activator is non-null |
| 2 | `102c21d0` | `m_iDialog` (**`+0x128`**) is non-zero — the authored `dialogname` |
| 3 | `102c21de` | slot 158 `IsAlive()` on **this** NPC (`vtable +0x278`) |
| 4 | `102c21ee` | the same slot 158 on the **activator** (`MOV ECX,EDI`) |
| 5 | `102c2200` | `CBaseCombatCharacter::IsUnconscious` is false |
| 6 | `102c220f` | `m_bScriptHidden` (`+0xf4`, through the seven-byte getter `0x100b5190`) is false |
| 7 | `102c221e` | `m_bWillTalk` (**`+0x1088`**) is set |
| 8 | `102c222c` | bit 2 of `m_bfNPCStateFlags` (**`+0x5b64`**) is clear |
| 9 | `102c2239` | `m_bfAINPCFlags` (`+0x14b8`) `& 0x80000` — `NO_DIALOG` — is clear |
| 10 | `102c2245` | `IsInDialog()` (`0x102c1170`) is false |
| 11 | `102c2250` | the **activator's** controller test `0x10175180` is false |
| 12 | `102c225b` | the cross predicate `0x10146b20(activator, this)` is **true** |
| 13 | `102c2267` | `IsBusyWithDiscipline` (`0x1033e2b0`) is false |
| 14 | `102c2272` | `m_bfAINPCFlags2` (`+0x14bc`) `& 0x10000000` — `NO_DIALOG_PERSISTENT` — is clear |
| 15 | `102c227e` | the singleton `0x1023bd00()` is null **or** its `+0x4ac` is zero |
| 16 | `102c2291` | slot 404 `IRelationType` is neither `D_HT` (1) nor `D_FR` (2) |

**Three field names are corrected** against story 29d's checklist walk, from the listing and from
`vtmb_fields CAI_BaseNPCTroika`: the walk gives `m_iDialog +0x5b64`, `m_bWillTalk +0x128` and
`m_bfNPCStateFlags +0x1088`, and all three are rotated one place. The datamap puts `m_iDialog` at
`+0x128` with `key dialogname` and `m_bWillTalk` at `+0x1088`, and `+0x5b64` is the per-state
capability word whose bit 2 the payphone override also reads.

**And gate 16's receiver is corrected.** `102c2291` is `MOV EDX,[ESI] / PUSH EDI / MOV ECX,ESI /
CALL [EDX+0x650]` — the dispatch is on `this` with the activator as the argument, so the question is
"what do *I* think of *you*", not the reverse. Gates 9 and 14 are also two separate tests five gates
apart, which is why the port's `HasDialogSuppressFlag()` (which ORs the two bits) is not what either
one reads.

**Unrecovered:** three retail inputs have no source on this substrate and are seams, each answering
the **admitting** value so nothing is silently refused — the activator's `m_hControllerNPC` state-3
test (`0x10175180`; the port carries that word on the NPC, which
`FElysiumPlayer::CanAttemptStealthKill` already records as a shape gap), the cross predicate
`0x10146b20` (spec 0006 owns its producers), and the menu singleton `0x1023bd00`'s `+0x4ac`.

### `CAI_BaseNPCTroika::FinishTalking` `0x102c0ca0`

_Recovered 2026-09-14, story 29d._

573 bytes. It **latches** `m_bIsTalking` (`+0x64c0`) at `102c0d12`, before anything is written, and
the latched byte is what picks the notification at the very end.

If `m_hDialogScene` (`+0x6554`) resolves to a live entity, the partner's `+0x498` byte — "this
speech scene has finished its line" — splits two arms:

* clear → `UTIL_Remove(partner)` (`0x101cd940`). A partner that has **not** reported done is
  destroyed outright.
* set → the partner's slot `0x3d4` (**245**), then `CBaseEntity::ThinkSet(partner, 0x101c0b10, 0.0)`
  and `partner->m_flNextThink` (`+0x17c`) `= curtime + _DAT_104493d0`. That constant is **0.1**, a
  **double**, read out of the pinned image (`102c0e29 FADD qword ptr [0x104493d0]`).

Then, unconditionally and in this order (`102c0e62`..`102c0e8b`):

```
m_hDialogScene (+0x6554) = -1
m_szDialogQue[0] (+0x64ec) = 0
m_bIsTalking (+0x64c0) = 0
m_flTalkEnd (+0x64cc) = curtime          // a STAMP, not a clear
CBaseEntity::ResetScriptedSoundOverrideEnt(this)
```

The `+0x64cc` write is the fact that mattered for the port: it is a **stamp with the current time**,
not a clear, and `CAI_BaseNPCTroika::IsTalking` (`0x102c0aa0`) compares `curtime < m_flTalkEnd`
**strictly**, so the stamped instant already answers "not talking". Until story 29d the port carried
`+0x64c0` and `+0x64cc` as one `TalkingUntil` stamp; this body can tell them apart, so `bIsTalking`
is now its own member and `ElysiumNpcKernelShapeMap.cpp` binds `+0x64c0` to it. The spoken-line
player `0x102c0520` writes both together (`102c0923 MOV byte [ESI+0x64c0],1` and
`102c092a FSTP float [ESI+0x64cc]`), which is `FElysiumNpc::OnDialogFilePlayed`.

Finally `102c0e92 TEST BL,BL` reads the **latched** byte. Both arms resolve
`UTIL_PlayerByIndex(1)` (`0x101cd9e0`) first and do nothing when there is no player; then
`thunk_FUN_10178120` reaches the conversation object hanging off that player and:

* the latch was **clear** → `CDialog::CallPendingNPCEventScript`;
* the latch was **set** → `CDialog::NPCNotifyDoneTalking`.

`SaveRestore10`'s `StopDialogOnRemove` (`0x102c0bb0`) now **calls** this body instead of standing in
for it with a `TalkingUntil = -1` clear.

**Unrecovered:** the delivery side of the two notifications. This runtime's dialogue continuation is
`FElysiumEntityWorld::UpdateDialogueAutomatic`, which the checklist verdicts `present` against
`0x100e4780` arm for arm and which already runs the pending-script flush first and unconditionally;
re-entering it from here would run the turn's continuation twice, so `FinishTalking` **records which
of the two was asked for** and leaves the delivery to the world. The partner-side scene stop and the
`UTIL_Remove` are counted seams: the scene player carries its own completion and exposes no
per-entity think word to the kernel.

### `CAI_BaseNPCTroika::ProcessTweakParam` `0x1029aa10`

_Recovered 2026-09-14, story 29d._

Slot 585, 735 bytes, the tweak-file key dispatch. Eleven `__strcmpi` compares in this order, and a
twelfth miss reaches the ignore message.

`CAPABILITIES` (`0x105d976c`) and `GOALS` (`0x105d96e8`) are **recognised**, each prints its own
`DevMsg(2, ...)` placeholder — `"Maybe this should be in a func/input SetCapability!\n"`
(`0x105d972c`) and `"Hey foo!  You need to implement some goals!\n"` (`0x105d96b0`) — and then
**falls through** to `1029aa3d`, so both messages print for the same key.

`NPCPERCEPTION` (`0x105d96a0`) stores `atoi(value)` into `m_iNPCPerception` (`+0x63b0`) and then
clamps twice. Each clamp `Error`s with
`"ProcessTweakParam:  %s - You specified an invalid NPCPERCEPTION parameter (%d).  Must be between 1
and %d\n"` (`0x105d9620`) — the trailing `10` is a **pushed literal**, not the field — and the second
clamp **re-reads** `+0x63b0` (`1029aabd`), so a value clamped up to 1 cannot then be clamped down to
10. Both `0x1028fb70` (`InitPerceptionDistances`) and `0x1028fc90` then run.

`VISION` (`0x105d9618`) and `HEARING` (`0x105d957c`) share one shape at `m_flSeekDistBase`
(`+0x63b4`) and `m_flHearingScalarBase` (`+0x63bc`):

```
st0 = atof(value)
FCOM  dword [0x104454c4]        ; the floor
FST   dword [esi + 0x63b4]      ; the STORE happens BEFORE the branch, on every path
JP    -> not-below, or NaN      ; skip to the recomputes
FCOMP dword [0x104492dc]        ; the sentinel
JNP   -> equal                  ; skip to the recomputes
Error(...)                      ; below the floor and not the sentinel
-> the same two recomputes
```

`_DAT_104454c4` is **0.0f** and `_DAT_104492dc` is **-1.0f**, both read out of the pinned image and
both `FCOM`'d as `dword`, so both are floats. A negative value is still stored, `-1` is the
derive sentinel and is exempt from the error, and `0x1028fb70` / `0x1028fc90` run on **both** the
error path and the accepting path.

The rest: `SQUAD` → `0x1029a930` (family Squad's `SetSquad`), `IPGROUPS` → `0x10298910`,
`HINTGROUPS` → `0x102989e0`, `TPMOVETIMER` → `m_flTeleportMoveTimer` (`+0x65dc`) `= curtime +
atof(value)` (an **absolute** time, which is how `ShouldThinkFrequently` reads the word),
`IGNOREATTACK` → `m_bIgnoreDetectedAttack` (`+0x65f5`) `= atoi != 0`, `NOALERTSTATE` →
`m_bNoAlertState` (`+0x65f6`) `= atoi != 0`. Every one of those nine returns from inside its own
block, so only `CAPABILITIES`, `GOALS` and an unrecognised key reach
`DevMsg(2, "ProcessTweakParam(%s, %s) ignored by base class.\n")` (`0x105d96f0`).

**Divergence, named:** retail's `Error()` does not return, so the `NPCPERCEPTION` clamps are dead
code in retail and the process exits. This runtime logs at `Error` level and continues to the clamp
— a crash guard, so an authored tweak file with a bad value cannot take the game down.

**Unrecovered:** nothing in the rule.

### `CDialog::process_npc_line` `0x100e8100` — the two arms the port did not carry

_Recovered 2026-09-14, story 29d._

Two halves of this body were already ported and are unchanged: the col-4/col-5 scheduling
(`FElysiumDlgConversation::EnterNpcLine`, which runs col-4 **now** and parks col-5) and the
gender/clan variant selection (`FElysiumDlgLine::RawFor`, citing `get_display_text 0x100e1ad0`).
The two that were missing:

**1. The row-fetch miss.** `local_34 = this->m_iCurrentLine (+0x2830)`, then `get(this, &local_34)`.
On a **miss** (`100e8130`) the body byte-copies the shared literal at `0x1054ca50` into the caller's
buffer and returns 0, the "used the default" answer that makes `fill_packet` skip the response band
entirely. The pinned image reads that cell as a **single space**.

**2. `0x100e8060` is the ellipsis normaliser, not a truncate/pad.** It copies the caller's buffer
into a local `0x800` one and runs six replace-**until-no-match** passes through `0x100e7f70`, then
copies back. The needle table is at `0x10561a88` and the replacement table at `0x10561aa0`, six
entries each (`iVar2` steps by 4 while `iVar2 < 0x18`):

| pass | needle | `.rdata` | replacement | `.rdata` |
|---|---|---|---|---|
| 1 | `" . . . "` | `0x10561b3c` | `" ... "` | `0x10561b14` |
| 2 | `". . . "` | `0x10561b34` | `"... "` | `0x10561b0c` |
| 3 | `" . . ."` | `0x10561b2c` | **`" ... "`** | `0x10561b14` |
| 4 | `". . ."` | `0x10561b24` | `"..."` | `0x10561b08` |
| 5 | CP1252 `0x85` + space | `0x10561b20` | `"... "` | `0x10561b0c` |
| 6 | bare CP1252 `0x85` | `0x10561b1c` | `"... "` | `0x10561b0c` |

**Pass 3's replacement carries a trailing space**: `0x10561aa8` holds `0x10561b14`, the *same*
pointer pass 1 uses, so a six-character needle is replaced by a five-character string that is not its
prefix. Pass 5 runs before pass 6, so every `0x85` followed by a space is consumed first and only a
bare one reaches pass 6, which **adds** a space. `0x100e7f70` itself replaces the first occurrence
only, bounded by `Q_strncpy`'s size (`0x800` here, so at most `0x7ff` characters survive), and
reports whether it replaced anything — that report is the inner loop's condition.

The normaliser runs on the **NPC subtitle only**. `process_pc_line`'s own `line_copy` at `100e8875`
has no such pass, so a PC choice keeps its authored spacing.

**Unrecovered:** nothing.

### `CDialog::process_pc_line` `0x100e8520` — the packet flag words

_Recovered 2026-09-14, story 29d._

883 bytes, one PC response row of the current NPC line. The gate, the automatic rows and
`DisplayText(bMale, ClanOffset)` were already carried; what was missing is the pair of flag bits the
packet carries and the return code that drives `fill_packet`'s band arithmetic. Ported as
`ElysiumDlgRetail::ClassifyPcRow`.

A row that `get` misses returns 1 outright. `CDialogDependency::Parse` then fills the packet's flag
word at `packet + 0x2804 + i*4` and its value at `packet + 0x2814 + i*4`. Three blocks follow, and
the second is not an `else` of the first — retail tests the two markers separately:

* **Auto-End** (`thunk_FUN_100df120`) with the packet's `0x30` test (`thunk_FUN_100e84e0`) **clear**
  and `CDialogDependency::Test` passing:
  * `LookupSpeechFile(m_iCurrentLine)` **misses** (`100e85e0`) → the band **collapses**:
    `m_iNumChoices (+0x2834) = 1`, slot 0's flags `0` and value `-1`, and the row still answers **1**.
  * it **hits** (`100e8600`) → flag bit **`0x10`**, the row answers **-1**, and the row's text is
    `Q_strncpy`'d into `+0x31ea` over `0x100` bytes.
* **Auto-Link** (`thunk_FUN_100df1b0`) with the same two gates → flag bit **`0x20`** and the row
  answers **1**, so the row **stays** in the band.
* a **starting-condition** row (`thunk_FUN_100df240`) → answers **0**, which `fill_packet` drops
  *without* raising the auto-terminate flag.
* anything else keeps its initial **1**.

**Unrecovered:** the `LookupSpeechFile` arm stays the port's named divergence
(`ElysiumDialogueSession.h`): this runtime resolves a speech file for every line, so the
band-collapse arm is unreachable through the conversation and is driven by its test instead.

### `CDialog::fill_packet` `0x100e7da0` — the band arithmetic, and a divergence closed

_Recovered 2026-09-14, story 29d._

362 bytes. It clears the auto-end flag `+0x30e9`, zeroes the four `0x800` text slots from
`packet + 0x804` and `CDialogDependency::Init`s the four dependencies at `+0x2848` stride `0x228`;
runs `process_npc_line` and **returns when it fails**; then `get_pc_responses` into `+0x2838` with
the count stored at `+0x2834`, and walks the rows.

The index handed to `process_pc_line` is `iVar8 - iVar5` — the loop counter **minus the number
already dropped** — a **compacting** index, so a surviving row lands in the packet slot immediately
after the last survivor rather than at its own ordinal. A `-1` answer raises `+0x30e9` **and**
increments the dropped count; anything below 1 increments it too. The stored count is then reduced by
the dropped rows.

Finally, at `100e7e9a`, when `+0x30e9` is **clear** and that count is **zero**, the packet's NPC text
is overwritten with `"I do not have a valid reply."` (`0x1056368c`), slot 0's flags are set to 0 and
its value to `-1`, and `m_iNumChoices` is forced to 1.

**The port's divergence is closed.** `ElysiumDlg.cpp` gated that fallback on `CandidateRows > 0` —
"a band that authored no PC rows at all is terminal by design and keeps the authored line". Retail
has no such gate: `get_pc_responses` answers 0, the drop loop never runs, the count is zero and the
substitution fires. The gate is removed and `CandidateRows` with it. `ElysiumDlgRetail::FillPacketBand`
is the arithmetic, tested arm by arm.

**Unrecovered:** nothing.

### `CBasePlayer` `0x1017c600` — the barter/loot window opener

_Recovered 2026-09-14, story 29d._

Read off the listing, because the decompiled C folds the target pointer and the loot byte into one
parameter. `RET 0x14` — five stack arguments past `this`: the target NPC, the loot byte and three
ints.

```
1017c617  this->+0x1eb8 = engine->vtbl[+0x8c](target->edict +0x2e0)    ; the entity index
1017c629  this->+0x1ec0 = loot_byte
1017c642  loot == 0 -> CBaseCombatCharacter::SyncVendorInventory(target, a, b, c)
                       and select "showbarter\n"   (0x10587ef4)
1017c650  loot != 0 -> 0x10324080(target)          ; the corpse-loot inventory build, NO extra args
                       and select "showloot\n"     (0x10587ee8)
1017c669  engine->vtbl[+0xf4](engine, this->edict +0x2e0, cmd)         ; ClientCommand
```

The window is opened by a **console command on the player's own edict**, never by a direct call, and
both command strings carry a trailing newline.

**Unrecovered:** this runtime has no barter or loot window (spec 9.8b owns barter and containers) and
no `showbarter` / `showloot` console command. The two branch calls are counted seams and the selected
command is recorded on the player rather than dispatched into a handler that would answer nothing.
The three extra arguments' meaning is unrecovered; they are carried rather than dropped.

### `CBasePlayer` `0x10183120` — `RescaleActiveDisciplineDurations`

_Recovered 2026-09-14, story 29d._

**Retargeted and the walk is corrected.** The checklist calls this `FElysiumNpc::RebaseClassStatArray`
and "a time-rebase of 17 stored per-condition timers … exact semantics unresolved". It is not an NPC
body at all: `vtmb_fields CBasePlayer` names `+0x1adc`
**`m_flClientVActiveDisciplineDurations[17]`**, the client-side mirror of each compiled Discipline's
running duration — seventeen wide because that is `stats.txt`'s Discipline count.

`1018312a CMP EAX,[ESP + 0x28] / JZ` skips the whole body when the first two arguments are
**pointer-equal** — a no-op guard, not a value compare. Otherwise, for each of the seventeen slots:

* find the class-info entry in the list at `+0x13bc` / `+0x13c0` whose type tag (`+0x10`) is **3** —
  the Discipline stat list — or lazily construct the global `CVStatList_t` singleton at
  `DAT_109f0b40` (guarded by the `DAT_109f0b2a` bit-0 latch and registered with `_atexit`);
* `thunk_FUN_102012d0(list, i)` — does the list carry slot `i`;
* build a `CVStat` key `(3, i)` with `0x10230f00`, test membership with
  `thunk_FUN_100ce450(&DAT_106e7050, this, &key)` and read the stored value with
  `thunk_FUN_100ce600(&DAT_106e7050, &value)`;
* then, at double precision (`101831e4 FLD dword / FSTP qword` widens the stored float before the
  subtraction):

```
remaining = value - engine->vtbl[+0x1dc]()        ; 101831fd FSUBR
elapsed   = slot  - remaining                     ; 10183201 FLD / FSUB / FSTP
delta     = remaining * arg2 / arg3               ; 10183209 FMUL / FDIV  (the divide is unguarded)
if (thunk_FUN_100ce630(&DAT_106e7050, delta))     ; 1018321a
    slot = elapsed + delta                        ; 10183223
```

So it is a **duration rescale**: the elapsed part of each running Discipline is kept and the
*remaining* part is scaled by `arg2 / arg3`.

**Unrecovered:** `DAT_106e7050`, the per-(player, stat) record registry. This runtime's active
Discipline state is an absolute `EndTime` per slot on `FElysiumDisciplineState`, not a duration
mirror, so `+0x1adc` is declared at retail's width and written only by this body and by its test; the
record lookup is a seam answering nothing, so no slot is rewritten today. The bounds test
`0x100ce630` is a seam answering **true**, the admitting value.

## Story 29d, family SpeciesMisc10 — the Newscaster's story queue, the cop's pursuit latch and the Sabbat leader's round record

_Recovered 2026-09-14, story 29d._

The relationship, dialogue and player-record half of this family's forty-one rows. The per-species
words and the spawn-side bodies are in [`shape.md`](./shape.md) and [`lifecycle.md`](./lifecycle.md)
under the same family header.

### `CNPC_VNewscaster::LoadNewscasterStories` `0x103a0ab0`

_Recovered 2026-09-14, story 29d._

**The class attribution and the file paths are both corrections.** The reading batch filed
`0x103a0670` and this body under the Ming Xiao family; they are `CNPC_VNewscaster`'s, and the two
words they load (`+0x665c` / `+0x6670`) are the queues `0x103a0d50` tears down and `0x103a0ff0`
prints. And `0x1064aadc` / `0x1064aac0` are **format strings** — `"%sNewscaster_Main.txt"` and
`"%sNewscaster_Side.txt"` — handed to `UTIL_VarArgs` (`0x101d3730`) with the single vararg
`"vdata\system\"` at `0x105a0f80` (`103a0ac2`). The files are `vdata/system/Newscaster_Main.txt` and
`vdata/system/Newscaster_Side.txt`; the earlier walk read the `%s` as a `\s`.

The body, in order:

1. `103a0abd` — tear BOTH queues down through `0x103a0d50` first, so a reload never appends.
2. `103a0ac2` — build the main path and open it as `KeyValues` (`0x101f2e20` against the filesystem
   at `DAT_1070b238`).
3. `103a0aef` — `GetFirstSubKey()` then `GetNextKey()`: every child of the file's one top-level
   block, in authored order. Each key zeroes a ten-word `0x28` scratch row (`103a0b00`), fills it
   with `0x103a07f0`, and is appended **only when that answers true** — growing the array through
   `0x103a1320` when the new count would exceed the allocation at `+0x6660`, republishing the base
   pointer to `+0x666c`, bumping the count `+0x6668`, memmoving the tail in `0x28` strides and
   copying the ten words into the new slot.
4. `103a0ba7` — the `KeyValues` release runs **unconditionally** after the block, so a missing file
   dereferences a null (`MOV ECX,[EDI+4]` with `EDI == 0`). Retail faults there.
5. `103a0bb8` — the identical sequence verbatim for the side file into `+0x6670` (allocation
   `+0x6674`, count `+0x667c`, base mirror `+0x6680`).
6. `103a0cae` — `m_bStoriesLoaded` (`+0x6690`) = 1, last.

**`0x103a07f0`, the per-`Story` parser, and the `+0x24` correction.** `103a0803` requires the key's
name to CONTAIN `"Story"` (`strstr`), warning `"Newscaster: invalid key! (%s)"` otherwise.
`103a081f` reads the story's name as `GetString("Name", "STORY")`, so an unnamed story is literally
`STORY`. `103a0872` finds the first `Version` subkey and walks siblings from it, reading
`dependency` and `filename`; a fifth version warns `"Newscaster: too many versions in %s! skipping
%s"` (`103a08ba`) and the version counter advances **only inside the filename block** (`103a0954`),
so a version with a dependency and no filename occupies no slot. The four slots are
`+0x04`/`+0x08`, `+0x0c`/`+0x10`, `+0x14`/`+0x18` and `+0x1c`/`+0x20`, which is what `0x103a0d50`'s
nine-handle release walk (`i = 0; i < 0x20; i += 8`) is counting.

The tail (`103a098d`..`103a0a0b`) is the fact the play body depends on: the parser walks the stored
versions in order, and the FIRST whose `dependency` is null, empty, or evaluates non-zero through
`0x1000134d` — `PyRun_String(src, Py_eval_input, __main__, __main__)`, the same interpreter every
dlg condition uses — has its **index** stored at `+0x24` and the record answers true. A record whose
every dependency is false frees its strings and answers **false**, and never reaches a queue. So
`+0x24` is not a version count; it is the chosen version, and `record + 8 + selected * 8` is that
version's filename.

The authored data bears it out: `vdata/system/newscaster_main.txt` is a `NewsData` block of `Story`
children whose `Version` rows carry dependencies like `G.Story_State < 15 and not IsPCMalk()`.

**Unrecovered:** nothing in this body. The port refuses a missing file with a named log rather than
faulting, which is a crash guard and the one divergence here.

### `CNPC_VNewscaster::PlayNextNewscasterStory` `0x103a0670`

_Recovered 2026-09-14, story 29d._

No slot; one direct caller. In order:

1. `103a0678` — with `m_bStoriesLoaded` clear, `0x101cd9e0(1)` — `UTIL_PlayerByIndex(1)`, whose own
   body checks the index against `gpGlobals->maxClients`, the edict's free byte `+0x4c` and its
   `+0x40` unknown — must answer an entity. **A missing player returns WITHOUT loading**, so the
   next call tries again.
2. `103a069b` — seed BOTH cursors with `RandomInt(0, count - 1)`.
3. `103a06c8` — `IsInDialog` (`0x102c1170`) refuses the whole rest of the body.
4. `103a06dd` — `count0 + count1` zero does nothing.
5. `103a06ef` — `RandomInt(0, count0 + count1)`, an **inclusive** upper bound, so the roll can equal
   the sum; `m_bPlayMainStory` (`+0x668c`) is 1 below `count0` and 0 otherwise.
6. `103a0719` — a **zero** `+0x668c` or an empty main queue advances the SIDE cursor `+0x6688`
   modulo `+0x667c`, returning early on an empty side queue; anything else advances the MAIN cursor
   `+0x6684` modulo `+0x6668`.
7. `103a0789` — `base + i*0x28 + 8 + record[+0x24]*8` — the selected version's filename — is played
   through `0x102c0520` when it is non-null.

**Note a retail inconsistency, not a port error.** This body reads `+0x668c` non-zero as "the MAIN
queue"; the debug overlay `0x103a0ff0` highlights a MAIN row when `+0x668c == 0`. Both are in the
image and both are reproduced.

**Unrecovered:** nothing.

### `CNPC_VCop::vfunc597` `0x10372cc0`

_Recovered 2026-09-14, story 29d._

Slot 597's species body, and a prologue: it adds two things in front of the Troika base
`0x102b4fb0`, which then runs unchanged.

`10372ce6` gates the whole prologue on the argument being the very entity `m_hClosestPlayer`
(`+0x628c`) resolves to. Inside, when `m_hPursuitPlayer` (`+0x6664`) does **not** resolve to a live
entity **and** `GetState()` (slot 464) is `2` COMBAT, the latch takes the argument's `+0xa8` and
stores that object's handle, writing `0xffffffff` when it is null. **`+0xa8` is `m_pPlayer`, the
player self-downcast cache** — the earlier walk called it "the argument's troika sub-object", so
what is latched is the PLAYER's own handle and nothing else can ever be. Then, unconditionally for
that same argument, `InputSetRelationship` (`0x10273790`) with the literal `"Player D_HT 10"` at
priority argument 0 (`10372d5b`) — capital `P`, unlike `CNPC_VGuard1`'s literal below.

`CNPC_VGuard1`'s hate latch (`0x1037e2d0`, 20 bytes) is the same write from the other side: set the
byte at `+0x6660`, then `InputSetRelationship("player D_HT 10", 0)` — **lower case** `player`. It is
reached from `OnStateChange` (`0x1037d020`) and six times from `vfunc461` (`0x1037d290`), so a
provoked guard permanently reclassifies the player as hated at disposition priority 10 and latches
that it has done so.

**Unrecovered:** nothing.

### `CNPC_VSabbatLeader::RecordPlayerHealth` `0x103aaa80` and `::PlayerDamagedEnoughThisRound` `0x103aabc0`

_Recovered 2026-09-14, story 29d._

A pair with one shared read. Both resolve `m_hClosestPlayer` (`+0x628c`) as a live `EHANDLE` and
walk that player's class-info list (`+0x13bc` count, `+0x13c0` table) for the entry whose type tag
`+0x10` is 0, falling back to the lazily constructed `CVStatList_t` singleton `DAT_109f0b40` (guarded
by bit 0 of `DAT_109f0b2a` and registered with `_atexit`), then read stat `0x0f`.

**Stat `0x0f` is the accumulated WOUND counter, not current health.**
`CBaseCombatCharacter::HealthToPercent` (`0x1032fe60`) computes
`((stat0x11 - stat0x0f) * m_iMaxHealth) / stat0x11`, so `0x11` is the cap and `0x0f` rises with
damage. `RecordPlayerHealth` therefore snapshots the player's damage TOTAL into `m_LastPlayerHealth`
(`+0x66cc`) at the start of a round, and does **nothing at all** without a live closest player — the
mark keeps its previous value rather than resetting.

`PlayerDamagedEnoughThisRound` answers false without that player, and otherwise
`_DAT_104c3ce0 <= (float)(stat0x0f - m_LastPlayerHealth)`. `_DAT_104c3ce0` is **2.0f**, read at file
offset `0x4c3ce0` of the pinned `vampire.dll` (bytes `00 00 00 40`; the same pass re-reads
`_DAT_104ce8c0` as `1e-05` and `_DAT_104454c4` as `0.0`, both matching values the port already
records, which validates the mapping). So the name is literal: the player's wound counter must have
risen by at least two since the mark.

**Unrecovered:** nothing in this family's social half.

## The class-relationship defaults are code, not vdata (2026-09-21, 0018 story 17)

0018 story 17 said the class-relationship table is "loaded once from vdata", naming `Rules.txt`.
**It is not.** The shipped `vdata/system/rules.txt` (540 lines) carries no relationship,
disposition or `D_*` data of any kind; its blocks are `Tables`, `VampFrenzy_Info`, `Damage_Info`,
`VampHeal_Info`, `Occult_Info`, `Discipline_Info`, `Ladder`, `Knockbacks`, `Jumping`,
`Animal_Friendship`, `Physics_Hand`, `Zombie_Grapple_Info`, `Npc_Combat_Info`, `Ming_Xiao_Info`,
`Npc_Follower_Info` and `Melee_Reactions`.

Every default is a hard-coded call to `CBaseCombatCharacter::AddClassRelationship 0x10332aa0`.
The 19 sites in `vampire.dll`, with their literal `(class, disposition, priority)`:

| site | args | when |
|---|---|---|
| `CNPC_Crow::Spawn 0x10357440` | `0xe, 4, 0` | spawn |
| `CNPC_VVampire::Spawn 0x103c4ef0` (and its thunk `0x10014876`) | `1, 1, 0` | spawn |
| `CNPC_VPlayerController::Spawn 0x103a4510` | `1, 3, 0` | spawn |
| `CNPC_VManBat::Spawn 0x1038b030` | `1, 1, 10` | spawn |
| `CNPC_VGhoulCroucher::NPCInit 0x1037b290` | `1, 1, 10` | init |
| `CNPC_VWerewolf::NPCInit 0x103caef0` | `1, 1, 10` | init |
| `CNPC_VZombie::NPCInit 0x103defc0` | `1, 1, 10` | init |
| `CNPC_VSabbatLeader::StartTransformation 0x103aa3b0` (and `0x1000eb3d`) | `1, 1, 10` | transform |
| `CNPC_VHengeyokai::InputStartTransformation 0x10383170` | `1, 1, 10` | input |
| `CNPC_VMingXiao::InputStartTransformation 0x1039a700` | `1, 1, 10` | input |
| `CNPC_VSheriffMan::StartAttacking 0x103b1470` | `1, 1, 10` | on attack |
| `CNPC_VAndreiBlood::InputTriggerCombat 0x1035dd00` | `1, 1, 10` | input |
| `CNPC_VAndreiBlood::vfunc432 0x1035e980` | `1, 4, 10` | combat arm |
| `InputSetRelationship 0x10273790` (and its thunk `0x1000421e`) | authored | map / script |

Only the last is data-driven, and it is the map and script surface, not a default table. **The
defaults are therefore a generated code table, and they are not all applied at spawn** — four of
them arrive on a transformation or an input, so a port that sweeps constructors reproduces the
wrong set. `reaction.txt` (the RPG score) and `SetDisposition` (stance) remain separate systems,
as recorded above.
