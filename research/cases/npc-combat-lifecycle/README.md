# VtMB NPC combat-lifecycle research case

This offline case closes the retail server joins between perception, enemy selection, combat
state, attack scheduling, damage reactions, authored health, death and player death. It does not
run or modify the game and it does not implement the Unreal combat system. Generated
decompilation and copied Ghidra projects remain below `ELYSIUM_WORK_ROOT`.

## Questions

- Which sensed or remembered actors are eligible to become an enemy, and in what exact order are
  reachability, relationship priority, distance and visibility compared?
- When may the AI keep its current enemy even though a better candidate exists, and which
  conditions and outputs are produced when the enemy changes, dies, eludes the NPC or disappears?
- How do damage, sight and combat sound become memory, alert/combat state and schedule
  interruption rather than an immediate attack animation?
- Which conditions select chase, cover, ranged attack, melee attack, reload, dodge, block, flinch,
  incapacitation and death schedules for ordinary combat-capable humanoids?
- What is the retail distinction between melee reaction bands, generic damage flinch, light/heavy
  damage conditions, knockback and death animation?
- How do `Health`, `Max_Health`, `Health_Aggravated_Dmg`, `HealthBuffer`, Source `m_iHealth`, the
  unkillable flag and life state form one NPC or player life transaction?
- Which native callbacks and entity outputs fire on damage, half health, incapacity and death, in
  what order, and what does an `npc_maker` observe?
- Which class-specific overrides intentionally depart from the ordinary humanoid lifecycle?

Established authored population, state, schedule and task facts are consolidated in
`docs/vtmb/npc-ai-reverse-engineering.md`. Weapon lethality, defense, soak and health commit are in
`docs/vtmb/combat-and-damage.md`; stat-container semantics and literal NPC `Max_Health` values are
in `docs/vtmb/game_runtime.md`. This case owns the joins among them and is the reproducible surface
for RE48.

## Evidence and procedure

1. Use patch-first `vdata/system/npctemplate*.txt`, `stats.txt`, `rules.txt`, exported NPC entity
   definitions and the authored output corpus. Record provenance; do not copy game-derived rows
   into Git.
2. Copy the completed analyzed Ghidra project to a workstream-private directory. Never open or
   mutate the shared project directly.
3. Run the server specification against the hash-pinned `vampire.dll`.
4. Trace `GatherConditions` through memory refresh, `ChooseEnemy`, `BestEnemy`, `SetEnemy`, enemy
   conditions, state selection and schedule selection. Keep candidate discovery, remembered
   eligibility, chosen enemy, high-level state and active schedule as separate facts.
5. Trace the combat-character and NPC damage virtuals through damage conditions, outputs,
   activity/gesture requests, Source health projection, life-state change and `Event_Killed`.
6. Join the ordinary `CAI_BaseNPC`, Troika NPC and `npc_VHumanCombatant` vtables before treating a
   base behavior as the concrete humanoid policy.
7. Use `native_schedule_survey`, `npc_task_override_survey` and `npc_translation_survey` to join
   schedule declarations to task handlers and model activities. A schedule or activity name alone
   is not proof that damage, ammunition or death committed.
8. Use controlled retail capture only for remaining frame timing, random choice and presentation
   validation; preserve captures below `ELYSIUM_WORK_ROOT`.

The driver verifies the binary hash and writes derived output below
`$ELYSIUM_WORK_ROOT/research/ghidra/out/npc-combat-lifecycle/`:

```powershell
uv run elysium research npc-combat-lifecycle research/cases/npc-combat-lifecycle/specs/npc_combat_lifecycle_server.json --binary "<VtMB>/Vampire/dlls/vampire.dll" --project-dir "<work>/research/ghidra/project_npc_combat_lifecycle_<task>" --kinds funcs,asm,xrefs,fields,vtables,grep
```

For a focused rerun, append `--address <address>` and narrow `--kinds`. Use `--dry-run` to
validate the specification and print the planned Ghidra calls without executing them.

The patch-first life-pool census is a separate non-Ghidra probe:

```powershell
uv run elysium research npc_health_census "<export>/vdata/system" --json "<work>/research/npc-combat-lifecycle/npc-health-census.json"
```

## Recovered contract

- The base gather pass refreshes enemy memories before `ChooseEnemy`; the committed enemy exists
  before range, LOS, facing and attack conditions are gathered.
- `BestEnemy` admits living valid remembered `D_HT`/`D_FR` actors and compares reachability,
  relationship priority, distance and visibility. Exact-entity priority precedes class priority;
  the non-null default is 5.
- Enemy replacement is schedule-gated. `NEW_ENEMY`, `LOST_ENEMY` and `ENEMY_DEAD` interrupt
  interest decide whether a better, eluded/null or dead target may pre-empt current work.
- Native state 2 is combat and state 3 is alert. Authored `forcestate` intentionally maps 2 to
  alert (3) and 3 to combat (2).
- Ordinary humanoids route a state-2 active weapon through melee (`0x10385e40`) when capability
  `0x18000` is present and ranged (`0x10386560`) otherwise. Both are ordered schedule selectors,
  not animation or damage committers.
- The alive damage transaction commits `HealthBuffer`, accumulated `Health`, aggravated damage and
  Source-health projection before the NPC raises damage outputs/conditions. One-second damage over
  15 percent of Source max health raises `REPEATED_DAMAGE`.
- Death is admitted when RPG `Health >= Max_Health`, after the alive virtual returns. NPC death is
  schedule/script aware and fires `OnDeath` once; the Troika override composes maker/claim/feed and
  Python `MarkAsDead` consequences. Player death uses its own outer teardown/presentation path.
- The 150 patch-first NPC-template declarations resolve to 114 explicit, 23 inherited and 13
  defaulted pools over 1..1400. `BloodPool` is a separate resource, not life capacity.

No live retail aggression capture was performed. Static code establishes branch ordering,
thresholds and callbacks; it does not close rendered hit-pose blending, ragdoll impulse appearance,
frame-exact output observation, the complete firearm caller chain into generic `DamageFlinch`, or
derived-class incapacitation policy.

## Consumers

- Enemy selection, state, schedules and outputs: `docs/vtmb/npc-ai-reverse-engineering.md`
- Damage, reactions and death commit: `docs/vtmb/combat-and-damage.md`
- Stat and NPC-template semantics: `docs/vtmb/game_runtime.md`
- Rulebook inventory and census: `docs/vtmb/vdata-catalog.md`
- Activity and sequence resolution: `docs/vtmb/animation_and_movers.md`
- Maker child lifecycle: `docs/vtmb/entity_io.md`
- Project/research status: `docs/project/roadmap.md`
