# Wave H — the integrator's report (2026-10-04, commit `56b1d5ff`)

H1–H5 are integrated. **H stays unticked**: two acceptance records are open questions (Q-H1, Q-H2).

## Runs

One build, 145 s, green with no warnings. The whole suite once, 526 s; the changed records re-run
by name, 31 s. Default tier 176 tests, 0 failed, 21 s.

| run | records | pass | expected-fail | fail | unexpected-pass | error |
|---|---|---|---|---|---|---|
| V2 (before) | 96 | 54 | 35 | 4 | 1 | 2 |
| full suite | 103 | 61 | 35 | 5 | 1 | 1 (designed) |
| after the re-run | 102 | 62 | 36 | 3 | 1 | 0 |

## Verdicts that moved

| record | before | after | why |
|---|---|---|---|
| `rollcall_vcamera`, `rollcall_vcamerasecurity` | error | pass | the camera's model admission (below) |
| `sense_cone_enter` | fail | pass | arena walls block NPC sight (H2) |
| `verbs_stealth_kill` | fail, unclassified | expected-fail, known red 3 | its `stealthkill` events: the query admits the mark (0.917 s), `+use` starts the grapple, death, `OnDeath` and the corpse follow at 5.37 s; only the end `on_ground` probe fails — the corpse that does not land, as in `damage_lethal_death`. V2's failure was the harness's leaked crouch (H4) |
| `cover_reclaim` | pass | fail, open (**Q-H1**) | with walls opaque the gunman first takes `cover_corner_ne`; after the corner hints are killed it fires (`RANGE_ATTACK1`) instead of re-claiming `cover_low_north` |
| `memory_occluded_kept` | fail | fail, open (**Q-H2**) | the lost-sight outputs fire at 2.0 s but `ENEMY_OCCLUDED` appears in the trace only at 2.5 s; retail does both in one pass and the port's code reads the same, so the tap or a second path is at fault — undetermined |

Re-read, unchanged: `cover` (known red 1), `cover_armed` (N1; its new `player_weapon` probe holds),
`sense_bodies_transparent` (pass).

## Decisions taken

- **The camera admission, fixed at the root.** A model-less camera gets `models/null.mdl`
  (`CNPC_VCamera::Precache 0x103689c0`). The character admission now answers Ready for a model the
  catalogues publish with nothing to load, the rule the prop path already used. Coder A's
  harness-side drop was then dead and is removed.
- **`light_pin`.** Retail's behaviour recovered (`docs/vtmb/stealth.md`, listing `0x10351b3e`):
  while the setting's whole-number value is above -1, the light becomes value × 0.1, after the
  torch rule. Ported as the console variable `debug_stealth_light`, default -1, changing nothing.
- **The `stealthkill` tap** is in `ElysiumStealthKillRules.cpp`, emitting when the answer changes.
- **`RebuildStageWorld`** accepts a Failed stage (`ElysiumMapActorLifecycle.cpp:267`).

## Filed

N11 in `stories/v1/triage.md`: the port admits a stealth kill only on an idle or alert victim;
retail's shipping rule is any state but dead (`ElysiumNpc.cpp:2360`,
`debug_allow_non_idle_auto_sk` default 1). Placed on V7, XS.

## Not done

- The roll call's task-failure and churn bounds (`never` with `at_most`): each class's retail
  program must be read first to state the legal count.
- `maker_respawn`'s window cannot close at a label; "nothing after death" needs a read.

## Still red on the harness

Q-H1, Q-H2, `rollcall_vzombie` (H11), H6–H15. No door for the police/law block across a rebuild
or for carried-but-unwielded items on a map host. `_selftest/stage_failed_a` errors by design and
is parked as `.json.parked`.
