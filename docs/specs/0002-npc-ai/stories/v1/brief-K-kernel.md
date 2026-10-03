# Brief K — the loop in combat, movement, cover, damage and death

Read `README.md` here first. Your records go under `Arena/scenarios/combat/`; your files are those
records, `stories/v1/inventory-K.md` and `stories/v1/triage-K.md`. `Arena/scenarios/cover.json` is
yours to extend or split.

## The landed stories you prove

| Story | What it claims |
|---|---|
| 0019/8 = 0002/29e the loop | `NPCThink → RunAI → GatherConditions → MaintainSchedule → 437/438 → StartTask/RunTask` over 380 `rule` rows; damage through slots 142 / 390, death through 144. Combat and death were never observed on a map |
| 0019/3 the schedule corpus | 691 texts run from the deployed corpus; the witness `SCHED_TROIKA_CHASE_ENEMY_FAILED` (stop, wait 0.2 s, fail to `STANDOFF`, tolerance 24, find cover from the enemy, relaxed anims, run the path, remember `INCOVER`, face the enemy, idle, wait 1 s) |
| 0018/5 the navigator | `Move 0x102eff40` row for row: the outcomes `0x0d`, `0x0c`, the stale mark, the NPC hold and its re-issue, both completions; arrival |
| 0018/6 geometry services (movement half) | the stand test, `IsValidCover`, reachability, a sealed pocket refused |
| 0018/8 hint nodes | the four searches, claim / release / owner / available, the cooldown; the tactical cover search `0x102b7110` |
| 0018/7 traversals | doors (slot 531 from both dispatchers, the NPC-open data, the locked-door refusal and its timer), the crosswalk wait `0x102a0bc0` and the queue arm, jump links refused |
| 0002/3a–c the stealth kill | the rules and the arc, the victim selection `0x101be1f0`, the commitment and the synchronized death |
| 0002/20 the trance | `FeedInterrupt → SCHED_TROIKA_MESMERIZED`, `DELAY_INTERRUPTS` |
| 0019/2, 0019/6 save and restore | the generated walk, `OnPostRestore` |
| old rows "done, record only" | 25b the species `TranslateSchedule` bodies; 25c the null-schedule arm; 26 the pre-selector |

## The families, each at least one record

- **Chase.** A hostile melee NPC with the player in sight across the room: the chase program,
  the path, arrival inside melee range. With the player unreachable (teleported onto a spot the
  mesh does not reach, if the arena has one; else say so): the chase-failed witness above, task
  for task.
- **Cover.** `cover.json` as it stands, plus: the claim released and the next hint claimed when
  the first cannot be reached or is invalidated (`hint-`, the `+5.0` path-failure delay, `hint+`
  on the next after the cursor — read `docs/harness/green-room-arena.md` and the hint searches'
  prose first); a variant with the player armed (the integrator saw the port select
  `SCHED_TROIKA_TAKE_COVER_HINT_VS_MELEE` against an unarmed player: read slot 606 `0x102b8320`
  and the selector to state which program retail selects in each case, and write both records).
- **Ranged attack.** From the open, in range: the range-attack program, the weapon's anim event,
  the task completing, the next attack after retail's delay.
- **Melee.** The NPC reaches the player and swings: the melee program, the hit's anim event,
  damage on the player.
- **Range bands.** The player teleported to 96 cm of a ranged NPC, then to 10 m: exactly one
  attack condition at a time, the step-back program ending, the re-face, the attack.
- **Damage and death.** A live idle NPC takes non-lethal damage: the reaction retail selects
  (the coder of wave 2 saw `SCHED_TROIKA_SHOT_BY_UNKNOWN`); lethal damage: `death`, `corpse`, the
  body on the ground (`on_ground`), no schedule after it, `OnDeath` fired.
- **Lifecycle.** `StartHidden 1`, then `ScriptUnhide` by `script`: the NPC lands, sees, fights.
  A neutral NPC, then `SetRelationship` `player D_HT 5`: it enters combat. Dialogue is lane W's.
- **Doors and the crosswalk** are map-stage records: a tutorial NPC routed through a door it may
  open, and one refused by a locked door (`sm_hub_1`'s `basic_smoke_door`); a hub pedestrian
  waiting at a red curb. Find the entities in `docs/vtmb/sp_tutorial_1-event-surface.md`, the
  hub's reach list `docs/vtmb/npc-kernel/reach/sm_hub_1.md` and 0018's story 7 text.
- **The player's verbs.** The stealth kill and the feed need the player to act. Find the door a
  script can use (a console verb, an entity input, the input router's replay): if one exists,
  write the records (victim selection from behind inside the arc, the commit, the synchronized
  death; the feed and `SCHED_TROIKA_MESMERIZED`); if none does, that is a harness gap: name the
  action the runner needs.
- **Save and restore.** There is no load verb yet (V6): name the two actions the runner needs
  (`save`, `load`) and write the record against them, parked.

Known reds you should expect to meet: 1 (every walk-then-animate program: the sequence at
`rate=0`), 2 (attack conditions stack), 3 (anim events, the corpse, the 13 s face,
`BEHIND_ENEMY`), 4 (`0xef` never breaks), 5 (hidden and flipped NPCs), 6 (the place release under
any program), 8 (restart, not resume).
