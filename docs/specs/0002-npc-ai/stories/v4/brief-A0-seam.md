# Brief A0 — V4a's seam commit (one agent: writes the seam, builds once, commits once)

Read `README.md` here (all of it; §3 is your job), `packets.md` (R1, R2) and the judge's rulings
(`stories/v1/triage.md` § "Judge's rulings, V4"). You are the only V4a agent that writes and builds
before the wave (method step 1). **The seam changes no behaviour**: after your commit every record
that passed still passes and every V4 record is still red (`anim_footsteps_walk` may pass: it is a
guard). Re-locate every site by Grep on its name: V3c / V3d / T6b moved lines.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBase.h` (the words; the slot-258 override's
  declaration)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseAnimEvents.cpp` (new: the override's body,
  forwarding to the base stub)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpc.h` (the bridge row accessors' declarations only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcAnim.cpp` (the accessors' bodies answering nothing)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcPositions2.cpp` (`GroundSpeedCm()` reads the word)
- `Source/ElysiumUE/Public/ElysiumPlayer.h` (`FElysiumCombatCharacter`), and the NPC FOV word's
  current home (`ElysiumNpcLifecycle2.inl:~118`, `ElysiumNpcSpawn.inl:~92`) if the word moves
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcKernelShapeMap.cpp`
- `Source/ElysiumUE/Private/Debug/ElysiumArenaScenario.{h,cpp}`,
  `Source/ElysiumUE/Private/Debug/ElysiumArenaScenarioRunner.{h,cpp}`, the MCP tool file holding
  `elysium_entity_get` (`ElysiumMcpTools.cpp`), `Arena/README.md`
- `Arena/scenarios/world/anim_footsteps_walk.json` (new), `Arena/scenarios/combat/face_enemy_turn.json`
  (new), `damage_lethal_death.json`, `verbs_stealth_kill.json`, and the `known_red` field (only) of
  the records README §3 lists.

## The job

1. **The words** on `FElysiumNpcBase`, beside the sequence words: `float LastEventCheck`
   (`m_flLastEventCheck +0x658`), `float YawSpeed` (`m_flYawSpeed +0x560`), `float GroundSpeed`
   (`m_flGroundSpeed +0x654`), `bool SequencePastHalf` (`m_fSequencePastHalf +0x568`, unless a word
   already stands for it — Grep `PastHalf`). Each with a comment: retail writers (`0x1008f120`,
   `0x10090950`, `0x10091880`, `0x10264841`), readers (`0x100916a0`, `0x10091740`), "not written
   yet: V4a lane A1/A2". `GroundSpeedCm()` returns `GroundSpeed` (0, as today).
2. **`m_flFieldOfView +0x1574` on `FElysiumCombatCharacter`** (`FieldOfView`). If the NPC holds it
   today, move it; every NPC writer keeps writing its value (0.2 Troika, species' own). The player's
   value is written by A3, not you. Shape-map rows for all five words in the macro form their
   neighbours use; generated bindings are not touched (they regenerate at V4's close).
3. **The slot-258 override** `FElysiumNpcBase::DispatchAnimEvents(float Interval, FElysiumEntity*
   Handler)` declared in `ElysiumNpcBase.h`, defined in the new `ElysiumNpcBaseAnimEvents.cpp` as a
   call to today's base (the overlay stub). Comment: "`CBaseAnimatingOverlay::DispatchAnimEvents
   0x10098c80` on the NPC chain; filled by V4a lane A1".
4. **The bridge row accessors** (README § "Shared names"): `SequenceEvents(int32)`,
   `SequenceTurnYaw(int32)`, `SequenceGroundSpeedAt(int32, poseParams)` answering empty / 0 / 0;
   `SequenceLoops(int32)` returning today's row `bLoops`. Comment each with the descriptor field it
   stands for; "filled by lane A2 from the baked clip data".
5. **Harness H18–H20** (README §3, §9): probes `speed2d`, `move_yaw`, `ground_speed`,
   `corpse_on_floor` in the scenario schema and runner (a probe that cannot be read fails, as the
   others); `elysium_entity_get` gains the H19 fields (they may read 0 where the kernel word is
   unwritten yet). `Arena/README.md`'s probe table: each new probe, and `on_ground` re-described as
   the motor capsule's floor, not meaningful after death.
6. **The records** (README §3): the two new records; `damage_lethal_death` and `verbs_stealth_kill`
   corrected as the judge ruled (by the ruling: either the end probe → `corpse_on_floor` with
   `known_red` naming 0014, or the transaction assertions with `corpse_on_floor` removed — state
   the retail source in `about`); `known_red` retargets. `face_enemy_turn`'s deadline is R1's bound,
   cited in `about`. Change no other expectation.
7. **Build once** (`uv run elysium build`; wait on it, generous timeout or its completion
   notification). A second build only for your own compile break. Then run `uv run elysium arena`
   with the records you touched plus `control_sequence` and `cover` (by name), and `uv run elysium
   test` (the default tier) once. Acceptance: every record that passed before passes; the new and
   corrected records parse and are `expected-fail` (or pass, for the guard); the default tier is
   unchanged.
8. **Commit once**: `feat(npc,arena): V4a seam -- the sequence speed and event words, slot 258's NPC
   body, the FOV on the combat character, H18-H20 probes, the V4 records`. Do not push.

## Not yours

No behaviour: no writer of the new words, no dispatch, no cone, no pick. No `ElysiumNpc.cpp`.

## Rules

README § "Rules for every agent of V4" (you are the one agent allowed a build here). The query
budget: 10 s warns, 60 s stops; never read a file over ~200 KB whole. Text through Grep / Read /
Glob. Wait on a build or a run by its completion notification, never a sleep or polling loop.

## Report (≤300 words)

The commit hash, the build's wall time, the verdicts of the records you ran, the default tier's
totals, where the FOV word lived and where it went, and anything you could not do.
