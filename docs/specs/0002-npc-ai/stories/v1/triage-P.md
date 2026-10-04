# V1 triage — lane P

One entry per red of the run `$ELYSIUM_WORK_ROOT/reports/arena/20261004T020844.266194Z/`
(traces beside its `index.json`), plus what the earlier runs of the same records showed.

## 1. `sense_cone_enter` — harness fault (the arena block does not block sight)

- **First unmet:** `never[1]` — `cond+ SEE_HATE (0x43)` at t = 0.00.
- **Trace:** at the activation pass (0.00) the guard on `behind_cover` raises `SEE_PLAYER`,
  `SEE_HATE`, `NEW_ENEMY`, `SEE_ENEMY`, `HAVE_ENEMY_LOS` and fires `OnFoundPlayer`, with the player
  at (-176, 0) Source units, 332 units away and directly behind the 96-unit block.
- **Retail:** `CAI_BaseNPCTroika::FVisible 0x102b4630` → `CBaseEntity::FVisible`, mask `0x2804091`;
  a solid brush stops it (senses.md "The sense pass for a hated player").
- **Port / harness:** the sight trace (`ElysiumNpcSight::Visible`,
  `Substrate/ElysiumNpcSightTrace.cpp:45` → `AElysiumMapActor::TraceRetail`) runs on the
  `ElysiumSight` channel, declared `DefaultResponse=ECR_Ignore` (`Config/DefaultEngine.ini:63`);
  the arena's solids are boxes with profile `BlockAll` (`Debug/ElysiumArenaBuilder.cpp:81`) and
  no `ElysiumSight` response, so every arena solid is transparent to NPC sight. On a baked map the
  brushes answer the channel by their contents (0018/3), so this is the stage, not the kernel.
- **Class:** 2, harness fault. Record left as is. Until the arena's walls and block answer
  `ElysiumSight` (and `0x4091`'s recipe, for `SetPlayerLOS`), no arena record can show occlusion,
  and `sense_bodies_transparent`'s pass does not discriminate.

## 2. `memory_occluded_kept` — harness fault (same cause)

- **First unmet:** `expect[2]` `cond+ ENEMY_OCCLUDED (0x48)` by 5.0 (expect[1] met at 0.10).
- **Trace:** the player is teleported behind the block at 1.0; the guard keeps raising
  `SEE_ENEMY` / `HAVE_ENEMY_LOS` (2.8 s, 2.9 s, …) and paths straight to him (`move goal -447 -152
  0` at 2.70). `m_eEnemyOccludedCheck` never counts because `FVisible` never misses.
- **Retail:** `GatherEnemyConditions 0x10270b20` steps 2–4 (ten misses → `ENEMY_OCCLUDED`,
  `OnLostPlayerLOS`, `OnLostEnemyLOS`).
- **Class:** 2, harness fault (entry 1). Also seen, for lane K: the weaponless guard's
  `SCHED_TROIKA_WAIT_FOR_MELEE_ADVANCE` fails `task_choose_best_melee_weapon` with `No weapon to
  choose (0x1f)` at 2.70 and cycles back into the cheer program.

## 3. `unknown_crouched_band` — harness gap (no crouch, no light pin)

- **First unmet:** `expect[0]` `cond+ SEE_UNKNOWN (0x01)` by 4.0.
- **Trace:** `SEE_PLAYER (0x5a)` every gather from 0.00; nothing else.
- **Retail:** the attention path `0x102b3e00` raises `SEE_UNKNOWN` only when `FUN_101671a0`
  admits the player (obfuscated, grappled, or `FL_DUCKING` and `!0x101672d0`); a standing player
  only sets `ATTACK_UNKNOWN`. The crouched player's radius is `StealthVisionScalarTable[light]
  [Sneaking] × 440`.
- **Harness:** the record crouches with `console "+duck"` / `"-duck"` (`GEngine->Exec`,
  `ElysiumArenaScenarioRunner.cpp:815`); nothing confirms the press reaches the player's mover,
  there is no `player_crouch` action, and no light pin (`debug_stealth_light` is an unported seam,
  `Substrate/ElysiumStealth.h:60`). Retail's answer depends on both.
- **Class:** 2, harness gap. Record left.

## 4. `hear_world_investigate` — new red (a heard sound is sometimes never heard)

- **First unmet:** `expect[2]` `cond+ HEAR_WORLD (0x6e)` within 2.5 of `PlaySound` (2.02).
- **Trace:** `arena_noise input PlaySound` at 2.017, then only the idle clip until 4.52. This
  record heard nothing in all three boots it ran in. A byte-identical twin differing only in this
  deadline heard at 2.467 and then ran the whole program green (investigate → walk 768 units,
  arrived 22.9 → `SCHED_TROIKA_ALERT_LOOK_AROUND` → back on `north`, `task_forget`), and
  `fail_route_unreachable_sound` / `interest_mode_never` (the same sound) heard at +0.45 s.
- **Retail:** the sound stays in `CSoundEnt`'s pool until `expire + 4.0` (think `0x101ba890`;
  duration is `max(1, wav length)`), `CanHearSound 0x1030f7b0` has no expiry test, and this
  listener's AI think runs at ≤ 0.29 s at 768 units (`CalcNextAIThink 0x10291230`:
  `(dist − 512) / 896`, 0.1 in LOS). `OnListened 0x1026a5e0` delays the condition by
  `RandomFloat(0.2, 0.9)`. So retail hears it by about 1.5 s after the insert, every time.
- **Port:** `FElysiumNpcSenses::TickHearing` skips any event with `ExpireTime < Now`
  (`Substrate/ElysiumNpcSenses.cpp:713`), so a sound not listened to within its 1 s authored
  life is lost, and in these boots no Listen ran in that second. Which side lags (the gather
  cadence or the bus cursor) is not settled from the trace: no event kind shows a gather.
- **Class:** 4, new red. `known_red: "new: …"` set. Not fixed.

## 5. `sense_enemy_facing_me` — known red 3

- **First unmet:** `never[0]` `cond+ BEHIND_ENEMY (0x57)` at 0.00.
- **Trace:** `BEHIND_ENEMY` on every gather with the player 300 units ahead and facing the guard.
  The same shows in every hostile record (`sense_beyond_vision`, `memory_occluded_kept`).
- **Retail:** `0x10270b20` step 7: the enemy's slot 363 on this NPC (`0x10326750`, player FOV
  0.5) sets `ENEMY_FACING_ME 0x56` and clears `BEHIND_ENEMY`.
- **Class:** 3, known red 3. `known_red` set.

## Not reds, filed for the review

- `hear_world_out_of_range` passes, but with entry 4 open, "not heard" proves less than it says.
- After `FAIL (0x43)` and in `SCHED_TROIKA_ALERT_WAIT` the listener reports `sequence idle01
  rate=0` and `seqfinished idle01` every 0.1 s (`fail_route_unreachable_sound`, from 6.1 s):
  a known-red-1/3 shape, not this lane's.
- The first record of a boot activates its NPC's first schedule at 0.25 s, the others at 0.00
  (`fail_route_unreachable_sound` in the first run, `hear_world_investigate` in the second).
  Scenario time zero differs from the NPC's first think by one phase; harmless to the deadlines
  here.
- Harness gap: `never` has no `from` and no count, so "one `taskfail`, no storm" after the
  failure route cannot be stated; `fail_route_unreachable_sound` asserts the route, not the
  absence of a second one.
