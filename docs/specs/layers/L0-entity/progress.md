# L0 progress — the orchestrator's log

One row per worker run (`runs.md`). Current operating policy (2026-10-10): GPT-6.1 Sol Medium
coordinates fresh GPT-6.1 Sol High coding workers, one worker and one run at a time on `main`
in this checkout. Reuse confirmed walks; make targeted retail lookups when facts remain uncertain.
The coordinator reviews, gates (build, default tier, run and touched-story records), stages exact
paths and commits each run alone. Never push. Current AGENTS.md excludes ledger regeneration and
`research kernel --check` from these gates. Retail is the default for unresolved behavior choices.

Historical attribution below remains intact: Haiku CLI readers walked retail, Sonnet confirmed,
and Fable/Opus workers implemented the earlier runs. Their older gate claims are historical
evidence, not new validation or acceptance of the layer closing gate.

## Current checkpoint

- Start: `47b6f780`, clean `main`; r001-r021, r029, r030 landed (23/88).
- r022 deferred before runtime edits: its absolute-origin partition walk needs the unfinished
  bounds/physics/partition lifecycle. Docs-only recovery checkpoint; no code gate or new record.
- Initial dependency repair attempted r022a cached frame/accessor before bounds/physics/partition.
  The integration gate below disproved that split: recover the actual L0 physical publication
  producer first, implement it with the cache it consumes, then bounds/partition and r022b.
  Original r022 remains open.
- r024a gated: independent named-think registration/lookup and context storage, fresh Sol High
  worker reusing the confirmed walk. Only 0x100ac250 / 0x100ac360 close. Original r024 stays open:
  scheduler and stamp producers, plus Spawn/removal's physics/partition inputs, remain deferred.
- r022a deferred after attempted integration: separate cache words exposed missing L0 physical
  publication; the candidate and record are saved externally and all runtime edits reverted.
  Neither transform row closed. Recover the actual physics-object/shadow producer and its
  pose-consuming service/simulation/relink timing before retrying the stored frame packet.
  Query-time bounds/partition remains a separate prerequisite; no eager getter or UE teleport
  surrogate was accepted. See the r022a recovery checkpoint below.
- Deferred recovery checkpoint: `a3aac976` (docs only, no runtime gate); clean checkout at dispatch.
- r024a implementation: `741fe3c3`, gated on main, no push. This coordinator session ends after
  the hash checkpoint; the persistent L0 Goal continues with a fresh Sol Medium coordinator.
- r022a recovery: `632ed8f`, docs only after exact-owned runtime rollback, build/default/16-record
  baseline gates below. Candidate and failed-gate evidence remain in external scratch. This
  coordinator ends after the hash checkpoint with clean main and no push; the persistent Goal
  continues with a fresh coordinator on actual physics producer/service recovery.
- Physics dependency packet: independent slot 237 `0x1003e7a0` is implemented with the real
  Flags word and player self-downcast construction identity. The new producer walk resolves
  actual VPhysics service/construction/controller/lifetime and post-think clocks; it does not
  implement that body/controller environment or resume the rejected cache candidate. Only the
  mask row closes; original runs remain 23/88, r022/r024/r044/r045 and broad query remain open.
  Coordinator reviewed and gated the packet on main; build/default/17-record results below.
  Commit is reported with the coordinator handoff; no push or modernization acceptance.
- Layer closure remains open: 65 unlanded runs, rebakes, default/arm and full arena gate,
  intermittent records in three boot orders, audit/hooks and state-bearing replacement checks.

### Independent physics mask and producer recovery gates (2026-10-10)

Fresh Sol High worker implemented complete independent selector `0x1003e7a0` and the real
player self-downcast class producer; no physics object, controller, query or cache surrogate
was introduced. Production coverage includes registered point/NPC/player construction and Spawn,
flag mutation and precedence, actual slot metadata/virtual routing, and world reset/recreation.
Worker production report:
`E:/elysium-work/reports/tests/20261010T224620.820596Z-elysium-substrate-physicsmaskproduction/index.json`
(1/1 pass, zero errors, four existing NPC diagnostics). Coordinator reviewed source and new
recovery walk, then built:
`E:/elysium-work/logs/20261010T224710.693388Z-build.json` (succeeded, exit 0).
Default report:
`E:/elysium-work/reports/tests/20261010T224716.573636Z-elysium-session-elysium-substrate/index.json`
(170/170 pass, failed 0, notRun 0). Bounded arena report:
`E:/elysium-work/reports/arena/20261010T224740.071234Z/index.json` (17/17 pass, failed 0,
both hosts exit 0): `l0_physics_solid_mask`, `l0_entity_parenting`, `l0_collision_bounds_size`,
`l0_entity_constructor_defaults`, `l0_datamap_keyvalues`, `l0_eflags_word`,
`l0_named_think_context_storage`, `l0_solid_touch_generation`, `l0_save_restore_blocks`,
`l0_transition_state`, `control_sequence`, `corpse_fades`, `restore_place_invalid_marker`,
and four fixture/retail-site/field/call selftests. The mask trace records the actual live
base/NPC/player results `0x200400b` / `0x202400b` / `0x201400b`; the two failure selftests
pass by inversion of their intentional first-unmet conditions. Selected historical passing
records remain passing. Existing engine Python plugin startup errors remain in the arena log;
the scenario JSON and process results were read. No arm/full-arena/mutation or layer closure
gate ran. Original completed runs stay 23/88; only selector row 237 closes.

Next frontier: complete independent trace/filter producers with collision-group/custom/cylinder
and veto-order semantics, then the real owned box/body/controller service boundary (shape and
parameter conversion, independent target/solved pose, environment clock/activity/contact/deletion).
That service can precede its callers once its own inputs are complete. STEP/pusher/think/relink
and cache publication follow. Reuse `walks/L0-physics-producer.md` and existing confirmed walks;
recover the named gaps, not an assumed missing module or the entire engine again. The rejected
r022a candidate stays external and deferred. No modernization was accepted.

### r022a recovery checkpoint and rollback gates (2026-10-10)

One fresh Sol High worker attempted the cache packet. Its build passed, but the touched
Substrate run failed with 57 logged errors, including real getter-observed scripted movement;
five bounded records passed without exposing the physical publication gap. The coordinator
rejected the runtime packet. All Source/test/arena edits were reverted exactly; the proposed
cache record is absent. Candidate source, record, binary patch and failed-gate logs survive in
`E:/elysium-work/codex/l0-r022a-deferred-20261010/`; all 23 saved-file SHA-256 values were
independently verified. No actual mutation run was completed, so no mutation gate is claimed.

Restored-source coordinator build: `E:/elysium-work/logs/20261010T222018.836763Z-build.json`
(succeeded, exit 0). Default report:
`E:/elysium-work/reports/tests/20261010T222256.999406Z-elysium-session-elysium-substrate/index.json`
(169/169, failed 0, notRun 0). Bounded aggregate arena report:
`E:/elysium-work/reports/arena/20261010T222326.335188Z/index.json` (16/16 pass, failed 0):
`l0_entity_parenting`, `l0_particle_attachment`, `l0_angle_motion`, `l0_datamap_keyvalues`,
`l0_eflags_word`, `l0_entity_constructor_defaults`, `l0_named_think_context_storage`,
`l0_save_restore_blocks`, `l0_transition_state`, `control_sequence`, `corpse_fades`,
`restore_place_invalid_marker`, and the four fixture/retail-site/field/call selftests.
The two failure selftests report their intentional first-unmet condition and pass by inversion.
Engine plugin startup errors remain in the arena log; both hosts exited 0 and scenario results
were read. These gates validate the restored baseline, not the rejected candidate.

Recovery and frontier are recorded in `entity_io.md` and `runs.md`: actual L0 object/shadow
producer `0x100a6490` / `0x100a68f0`, slot 46 service/construction/lifetime, pose consumer
`0x100a69e0`, and required relink/simulation/touch timing including `0x100342c0`, `0x1003b190`
and `0x100a53d0`. Exact service and smallest complete subset remain unrecovered. Audit corrects
the two local setters' folded-word/body-publication claims and STEP/touch overclaims; neither
frame row closes. Original runs remain 23/88, r022/r024 open; no modernization accepted.
No arm/full-arena or layer closure gate ran. Fresh coordinator starts with targeted producer
recovery, not another frame-only implementation against the same host coupling.

### r024a gate evidence (2026-10-10)

Coordinator build: `E:/elysium-work/logs/20261010T215457.595573Z-build.json` (pass).
Default tier: `E:/elysium-work/reports/tests/20261010T215501.934183Z-elysium-session-elysium-substrate/index.json`
(169/169, zero failed or abstained). Arena:
`E:/elysium-work/reports/arena/20261010T215628.816258Z/arena/index.json` (12/12 pass):
`l0_named_think_context_storage`, `l0_entity_constructor_defaults`, `selftest_fixture_stages`,
`selftest_retail_site_matches`, `selftest_entity_field_fails`, `selftest_entity_call_refused`,
`l0_entity_parenting`, `l0_datamap_keyvalues`, `l0_eflags_word`, `control_sequence`, `corpse_fades`,
`restore_place_invalid_marker`. Selected historical passing verdicts remain passing; storage is new.
Worker touched-family gate: `E:/elysium-work/reports/tests/20261010T215412.967987Z-elysium-substrate/index.json`
(168/168). No arm or full-arena closure gate was run. Fixtures prove local storage only; no live
dispatch/current/stamp substitute. Original completed-run count stays 23/88. No modernization accepted.

| run | walk | confirmed | worker | gate | commit |
|---|---|---|---|---|---|
| L0-r001 | — | — | done | build ok, default tier ok, 4 selftest records | `27ddd056` |
| L0-r002 | — | — | done | pytest 61, kernel --check ok, build ok, 169/0 | `6475c1b7` |
| L0-r003 | haiku 86 turns $0.51 | sonnet: constants recovered (1/36, 20, 40), 4 corrected | done, 39 min | build ok, 169/0, arena 20261010T041706 3 pass + selftests | `d4a32a29` |
| L0-r004 | haiku 71 turns $0.46 | sonnet: 4 corrected (strtol/atoi, callers of GetInt/GetFloat) | done, 30 min | build ok, 169/0, arena 2 pass + r003 records, selftests | `51e0a88c` | — | — |
| L0-r005 | haiku 82 turns $0.61 | sonnet: 3 corrected (preset block, SetMoveType, UTIL_Remove path) | done, 20 min (B) | B: build ok, 169/0, l0_ambient_init pass; main re-gate ok | `9dde13a1` (cherry-pick of lane-b 3a1e3295) | — | — |
| L0-r006 | haiku 126 turns $0.77 | sonnet: fadein 2560 not 512; slot 113 is Activate (once, ServerActivate); preset table decoded | done, 53 min | build ok, 169/0, kernel --check ok (ledger regenerated), arena 6 new + 9 prior pass | `665bd107` |
| L0-r007 | haiku 79 turns $0.44 | sonnet: lazy getters -2 sentinel, SndScheme_Char registry, RTTI CBaseTerminal/CPropSwitch | done, 42 min | build ok, 169/0, ledger regenerated, 3 new + 10 prior records pass | `5939e7ac` | — | — |
| L0-r008 | haiku 70 turns $0.43 | sonnet: RandomInt(0,0) draws nothing; rec+0x48 = missing-wave flag; case-insensitive interning | done (historical commit) | historical commit reports build ok, default 169/0; l0_sound_script_resolve; 12 prior pass; exact report path not recorded | `09ddb703` |
| L0-r009 | haiku 79 turns $0.35 | sonnet: eviction key = quietest retiring; retire rate +0x48/duration; 0x1044fab0 double | done (historical commit) | historical commit reports build ok, default 169/0; l0_sound_scheme_switch; 7 prior pass; exact report path not recorded | `cfdc35ac` |
| L0-r010 | haiku 100 turns $0.61 (rerun via stdin) | sonnet: SetParent never touches triggers (bTouch 0), attach always 0, slot 115 no-op; MOVETYPE_FOLLOW | done (historical commit) | historical commit reports build ok, default 169/0; l0_entity_parenting, l0_particle_attachment; 8 prior pass, input_changeschedule_reselect expected-fail; exact report path not recorded | `993c5af9` |
| L0-r011 | haiku 149 turns $0.91 | sonnet: +0x494 is the host FRAME counter (story goal wrong), decal filter table, CTEBSPDecal | done (historical commit) | historical commit reports build ok, default 169/0; l0_particle_spawn, l0_particle_ramp_time, l0_world_sweep_query, l0_runtime_decal; 9 prior pass; exact report path not recorded | `a9f47710` |
| L0-r012 | haiku 103 turns $0.48 | sonnet: style 32 gates Spawn/Use only; inputs bypass Use; AcceptInput refuses non-string params | done (historical commit) | historical commit reports build ok, default 169/0; l0_switchable_light; 11 prior pass, two expected-fails; exact report path not recorded | `03a8625d` |
| L0-r013 | haiku 99 turns $0.56 | sonnet: 0x10449280 is 1.0 (frames > 1); sprite angle arm SetAngles(pitch,0,yaw); blood x5 arm dead in SP | done (historical commit) | historical commit reports build ok, default 169/0; l0_sprite_lifecycle, l0_illusionary_spawn, l0_blood_effects; 11 prior pass; exact report path not recorded | `d7fed4d2` |
| L0-r014 | haiku 52 turns $0.31 | sonnet: TE spray emits nothing (slot 14 bare RET; r013 slot 13/14 swapped); IsMultiplayer 0 kills x5 arm; Smoke scale 0.1 | done (historical commit) | historical commit reports build ok, default 169/0; l0_blood_effects; 8 prior pass; exact report path not recorded | `283bec98` |
| L0-r015 | haiku 93 turns $0.48 | sonnet: fresh entity eflags 0x50000; FUN_100dc300 runs twice per entity; untouch queue consumer 0x100f9160 | done (historical commit) | historical commit reports build ok, default 169/0; l0_eflags_word, l0_solid_touch_generation; 13 prior pass; exact report path not recorded | `c78f9398` |
| L0-r016 | haiku 132 turns $1.03 (max) | sonnet: ctor eflags 0x54000 (no 0x8000 without edict); ParentName is m_iParent +0x124; lip formulas unclamped | done (historical commit) | historical commit reports build ok, default 169/0; l0_collision_bounds_size, l0_entity_constructor_defaults; 18 prior pass; exact report path not recorded | `8d5b0296` |
| L0-r017 | haiku 95 turns $1.01 (max) | sonnet: ParseMapData has 2 more dispatch sites; outputs arrive as type-10 KEY rows; debug gate is ent_debugkeys | done (historical commit) | historical commit reports build ok, default 169/0; l0_datamap_keyvalues; 23 prior pass, input_disablethink expected-fail; exact report path not recorded | `e76026a4` |
| L0-r018 | haiku 104 turns $0.81 (max) | sonnet: tokenizer 0x10136ce0 read (signed compares); mins/maxs/origin/angles arms set EFL 0x8000; Python __getattr__ bypasses ReadKeyField | done (historical commit) | historical commit reports build ok, default 169/0; l0_datamap_keyvalues; 11 named regression records pass; exact report path not recorded | `ff43d137` |
| L0-r019 | haiku 132 turns $0.82 (max) | sonnet: all six CONFIRMED; thunks not extra call sites; 0x450 size stands | done (historical commit) | historical commit reports build ok, default 169/0; l0_data_object_registry; 8 prior pass; touched arms 2/2; exact report path not recorded | `d4d8bf10` |
| L0-r020 | haiku 97 turns $0.61 (max) | sonnet: thunks not second bodies; NPC/player override BodyTarget; IsMonster byte 0/1 | done (historical commit) | historical commit reports build ok, default 169/0; l0_target_name_predicates, l0_should_toggle, l0_base_perception_defaults, l0_is_monster_template_flag; 9 prior pass; exact report path not recorded | `c1151ee7` |
| L0-r021 | haiku 138 turns $0.98 (max) | sonnet: SetModel/UTIL_SetModel never precache (unprecached name fatal); IsViewable via GetModelType; weapon bHidden != script-hidden latch | done (historical commit) | historical commit reports build ok, default 169/0; l0_entity_visual_state, l0_angle_motion; 15 prior pass; exact report path not recorded | `00187639` |
| L0-r022 | haiku 128 turns $0.89 (max) | sonnet: SetAbsAngles has NO partition walk; direct hook 0x10092ef0; current Sol recovery adds query-time partition lifecycle | deferred, no runtime edits | not gated: docs-only checkpoint; record still absent | `a3aac976` docs-only; implementation open |
| L0-r023 | haiku 98 turns $0.94 (max) | sonnet: Teleport has 3 bodies, 12 callers; notify block has physicsRotate byte; base has no m_fEffects storage |
| L0-r024 | haiku 121 turns $0.86 (max) | sonnet: ctor leaves grow size 0 / current ctx -1; slot 15 SetAttackExtents updates the partition; last-think written after the callback | r024a storage subset gated; full r024 open | build ok; default 169/169; storage + constructor + 10 regressions 12/12 pass; exact paths above | `741fe3c3` r024a only; original r024 open |
| L0-r025 | haiku 191 turns $1.25 (max) | sonnet: PostConstructor forces serial -1; empty classname never enqueues (+0x448 = -1); CGameStringPool case-sensitive |
| L0-r026 | haiku 176 turns $1.08 (max) | sonnet: port classname is LAST occurrence, retail FIRST (bake folds last-wins); qsort is VC6 CRT (port verbatim); 0x10352f00 runs before thinks |
| L0-r027 | haiku 150 turns $0.76 (max) | sonnet done |
| L0-r028 | haiku 182 turns $0.85 (max) | sonnet: stat-action callback resolves 3 names (port knows 1); FString == is case-insensitive; TMultiMap returns last landmark first |
| L0-r029 | haiku 93 turns $0.67 (max) | sonnet: cleanup inlined (no callees); 5 blocks Entities/EventQueue/Physics/AI/Python; slot order 17/18/19, restore 20 then 21 | done, 48 min (B) | B: build ok, 169/0, kernel ok, l0_save_restore_blocks + V6 checkpoint records pass | `c5617a18` (cherry-pick of lane-b c8a56c36); follow-up `08ad2527` (bindings keep retail's KEY flag for max_npcs/nextthink — exposed by restore_place_invalid_marker) |
| L0-r034 | haiku 88 turns $0.85 (max) | sonnet: pickers return the node/link in EAX; 4 callers via thunks; GetPosition already ported (FElysiumPlaceSet::GetPositionCm) |
| L0-r030 | haiku 125 turns $0.80 (max) | sonnet: changelevel args are (oldLevel, landmark); m_szMapName +0x598 / m_szLandmarkName +0x5b8 (walk had them swapped); transition mask aliases >= 32 | done (historical commit) | historical commit reports build ok, default 169/0; l0_transition_state; l0_save_restore_blocks, V6 checkpoint, 7 prior pass; persistence_world_rebind expected-fail; exact report path not recorded | `71d63e31` |
| L0-r031 | haiku 166 turns $1.01 (max) | sonnet: 0x101707c0 gate is pOldLevel != NULL (not a float); slot 134 is Think; m_fFlags +0x434 |
| L0-r032 | haiku 130 turns $0.81 (max) | sonnet: IsFloating's this is the ground entity; -9999 sentinel skips the distance test; slot 135 is CBaseEntity's own default |
| L0-r035 | haiku 98 turns $0.98 (max) | sonnet: a BSP/static-prop hit leaves trace.m_pEnt = the WORLD entity (port writes Invalid — unnamed divergence); view-pick runs only on a total miss |
| L0-r036 | haiku 129 turns $0.87 (max) | — (not yet confirmed) |
| L0-r033 | haiku 111 turns $1.01 (max; out of order, a lane-B candidate) | sonnet: acos(+-1) finite; A14/A15 = cand between src/dst; port has no link storage; hooks.tsv:345-346 stale |

## Where this stopped (2026-10-10 13:50 UTC, the owner's stop)

On `main`: runs r001-r021, r029, r030 (23 of 88). Walks confirmed and waiting for a worker: r022, r023, r024,
r025, r026, r027, r028, r031, r032, r033, r034, r035; walked, not yet confirmed: r036. Next in order:
lane A r022 (parented transforms; after r021), r024 (think contexts / base spawn / removal; after r015, r016);
lane B r033/r034 (view-node links; after r001) or r031 (player map restore). Lane B (`E:/dev/elysium-unreal-b`)
is reset to `main`. Before the layer closes: rebake sprites (r013 `frames`) and sp_giovanni_2b (r018), run the
arm tier (ElysiumNpcKernelPositionsTests.cpp:799 assumes IsViewable false), the full arena and
`contract-checks.md`, settle the owner notes below.

## Historical lanes (inactive under current policy)

Lane A: this checkout (`main`). Lane B: the clone `E:/dev/elysium-unreal-b` (branch `lane-b`, own binaries,
`Plugins/ElysiumBaked/Content` junctioned to lane A's baked content, `Plugins/External` copied); its runs are
gated in the clone, cherry-picked onto `main`, and re-gated here (`decisions.md` D2).

## Planning faults (reported by workers)

- r022: `SetAbsOrigin` 0x100b2300 -> tree 0x100b52a0 -> dirty append 0x100ddd20 /
  0x100dbc60 -> query callback 0x100dbd70 -> update 0x100ddd90 -> surrounding bounds
  0x100ddae0 / 0x100dd780. Ordinary bounds need 0x100dcd90 and the stored absolute frame;
  physics-selected bounds need the live object at +0x36c, slots 4/49 and collision-service slot 18.
  Those r044/r045 prerequisites were incorrectly scheduled after r022. The initial frame-first
  split was subsequently rejected by the r022a integration gate; physical publication must be
  recovered before a coherent cache packet can be dispatched. Absolute setters remain last.
  Query callbacks run before partition query slots 12-15, including zero-mask queries; a frame-end
  drain would diverge. Detail and remaining recovery: `docs/vtmb/entity_io.md` current r022 section.
  Fresh corpus inventory now includes `vphysics.dll`: object slot 46 is `0x260185b0`, controller
  target writer `0x2601aad0`, simulation `0x2601aa50` / `0x2601a250`, with construction and
  lifetime recovered in `walks/L0-physics-producer.md`. Collision007 geometry is available but
  its complete contract remains open. The body/controller boundary needs real shape/parameter
  conversion, clock/activity/contact/deletion production; STEP additionally needs complete
  trace/filter and named/primary think phases. Slot 237 is an independent completed dependency,
  not proof that those services exist. Use the function boundaries in runs.md for next dispatch.
  Overlap-query equivalence is unproved. This checkpoint downgrades overstated audit closure;
  it accepts no modernization and completes no run. r024 proceeds independently next.

- r024: Spawn `0x100aae50` calls slot15 SetAttackExtents `0x1009af40` before storing extents;
  `0x100dc220` reads cp handle +0x46 and, if invalid, invokes `0x100ddd90` before partition slot0
  margin update. It therefore shares r022's unfinished producer lifecycle. Removal calls slot225
  VPhysicsDestroyObject, currently empty, plus move-tree unlink pose handling. Approved r024a
  closes only independent named-think registration/lookup and context storage; Spawn/removal
  remain deferred, and the original 23/88 completed-run count does not increase for that subset.
  After r024a, the attempted r022a packet exposed missing physical publication. The next packet
  recovers actual physics-object/shadow production and consumer timing before retrying cache work.

- r024 scheduling: retail `PhysicsRunSpecificThink` 0x10033de0 requires deadline >0, clears it to0,
  dispatches at max(deadline,curtime) under a temporary clock and stamps last-think after return.
  Current default dispatcher treats deadline0 as immediate and parks with FLT_MAX. Actual zero
  writers include Elevator.cpp:127, FuncRotating.cpp:141/204, LogicClasses.cpp:512 (brush host
  deferral), Mover.cpp:377/410 and StarterClasses.cpp:40. Correcting all producers requires their
  targeted retail recovery and moving host deferrals out of retail think state. No default-zero
  modernization was accepted. Root bounded r024a to independent storage operations: fixtures can
  prove their local mechanics, never replace live current-context/last-stamp production.
  GetLastThink and named/default scheduling remain open until those producers are implemented.

- r029/r030: `hooks.tsv` has no rows for the AI and Python save-block handlers (0x1011a0c0 -> 0x1030c390 / 0x1019b360);
  they are registered with empty bodies as L4/L5 hooks — the owner adds the rows.

- r010: `0x100fafa0` (env_particle attach bone scan) walks the `.mdl` studio bone table at runtime; the port only
  has the baked reference skeleton (exporter order may differ, absent for static-reduced props). Needs a per-model
  bone-name bake in studio order — pipeline work, not an L0 run.

- r007 (L2): `FElysiumNpc::SeedSheet` re-seeds every stat at Spawn after the authored `gender`/`base_gender_` keys
  land at Load (retail seeds in the ctor 0x10326de0, keys after), and the template loader never maps
  `General/Gender "Female"` — no authored route yields a female combatant; the IsMale-false arm of slot 71 has no witness.

## Substrate divergences found by workers (not fixed in their run)

- r035: the trace result's entity for a BSP / static-prop hit is the world entity in retail (engine TraceRay
  0x2006a5c0 -> 0x200696b0); the port writes an invalid handle. FVisible's blocker, PassesFind's world reject and
  the view-pick guard all read it — fix with the sight-trace story (r035).

- r012: the retail think dispatcher `0x10033de0` fires a think up to one frametime EARLY (next think <= curtime + frametime);
  the port's `RunThinks` fires at or after. A fade step can land a frame late. Substrate-wide: belongs to the think
  story of entity_core, not to CLight.

## Coordinator notes

- Unity build: anonymous-namespace helpers with the same name in two Substrate/*.cpp files collide when the
  module grouping changes (r013's `ModelTypeOfName`/`Ang` vs ElysiumEnvSprite.cpp) — renamed at the cherry-pick;
  the worker template now says so.
- The ledger regeneration after a run re-emits a slot's stub unless the hand body is in `CHAIN_HAND` (r015); the
  worker template now says so. Gate: a build result must be read, not piped (`build FAILED` slipped past a `| tail`).

- r019: on a teleport UE's overlap ends arrive before `ReconcilePlayerTouches`' begins, so the player's data object is
  destroyed and recreated across a teleport (retail orders begins first) — the touch-lifecycle story.

- Lane B drift: its branch lacks lane A's runs between syncs, so shared bodies (ShouldToggle, IsMonster) got rebuilt there;
  from r020 on, lane B is reset to main after each of its runs lands.

- Session limit (claude.ai) hit at ~11:00; both lane workers died mid-run (r017 with 30 files of partial edits, r029 before
  editing). Resumed after the reset with a continuation worker for r017.

- r020/r029 cherry-picks onto main delegated to a merge worker (semantic duplicates of ShouldToggle / IsMonster).

## Notes for the owner (raised by the readers)

- r004: `FUN_102482f0` (hash-keyed file-text cache, pathID 0) needs nothing above L0; `hooks.tsv:325`
  lists it as an L4 hook and `audit.tsv` calls it a string pool — classification looks wrong.
- r004: the typed setters `FUN_10249160/102491a0/102491e0` write the node type onto the container, not
  the child (retail bug; reproduce).
- r003: `0x101ac570` exact multiples of 36 depend on x87 precision (R=36 → 40 or 39); records must avoid them.
- r006: `docs/vtmb/npc-ai/senses.md:442,453` say "owner origin" for the AI-sound insert; the asm says the source entity's origin.
- r007: the sound-scheme seam's layer: `hooks.tsv` says L0, `audit.tsv` says L2 — settle before the layer closes.
- r008: `hooks.tsv:365` hook needs removing or re-pointing; `docs/vtmb/` has no section for 0x1012f700, 0x101b3240, 0x101b33f0, 0x101b2f60.
- r009: `docs/vtmb/audio_pipeline.md:250-254` (`0 is instant`) contradicts the code; Combat/Alert `Filename` default inherits the Music name.
- r011: story `L0.effects_world.particle-rate-ramp` says "store engine time at +0x494"; retail stores the host frame counter (slot 120). The record `l0_particle_ramp_time` samples (0.6/0.9/1.1) are wrong for the same reason — the worker must write the record from the walk.
- r004: `FUN_102482f0` built in L0 (CRC-32 cache); `hooks.tsv:325` still lists it as an L4 hook — drop that row.
- r012: `docs/vtmb/lighting.md:76` calls CLight `client.dll` (it is vampire.dll); `docs/contracts/seam_map_map_lighting.md:544,555-556` carry a false input-gate and a cvar claim (the 0.05 floor is a literal).
- r009: the L0->L3 write 0x1022b590 -> 0x10175360 (player +0x1e04) is ported behind `HookL3PlayerSetSoundScheme`; `hooks.tsv` has no row (decisions.md D6) — add one or reassign.
- r016: port maps `ParentName` to +0x254 (retail m_iParent is +0x124) and `UseActivator` should be `m_hUseActivator`; door/button/movelinear lip formulas `sum|(size-2)*dir|-lip` are unclamped in retail, the port clamps at zero.
- r017: `docs/vtmb/entity_io.md:133` says the keyvalue debug gate is `ent_messages`; it is `ent_debugkeys` (0x106cf420). `hooks.tsv` rows 58, 59, 71, 85, 312 need a docs pass (engine-interface dispatch, not entity calls).
- r018: `docs/vtmb/python_bridge.md:282` and `programs.md:1469` say Python reads go through ReadKeyField; `Entity.__getattr__` goes through FUN_10195940 (case-sensitive, no flags). `ElysiumNpcKernelLifecycleTests.cpp:538` pins a case-insensitive `r` gate retail does not have.
- r021: `docs/vtmb/wielded_weapons.md:184-` says UTIL_SetModel precaches; the engine bodies (slots 20/12 are string-table lookups) contradict it.
- r022: `hooks.tsv:9` carries the wrong hook address (0x10093000; the body is 0x10092ef0); `audit.tsv` rows 407, 683, 705 need the owner's decision.
- r013: `bake_map_v2.py` now writes sprite `frames`; the shared baked content predates it (candle reads 1 frame until a rebake) — rebake before the layer closes.
- r026: `stories/L0.entity_core.gameframe-simulation.md:26` says 0x10352f00 runs after the think pass; it runs before. The bake pops `classname` and folds duplicate keys last-wins; retail's parser takes the first classname — the bake must carry key order / first classname for the map-ordering story.
- r032: `docs/vtmb/activity_enum.md:22` says FUN_10412260 inserts at the vector head; it appends at the end.
- r005: `health`/`radius`/`message` of ambient_generic are code-built datamap keys the ledger missed;
  `docs/vtmb/audio_pipeline.md:354-355` should read `20/x` for fadein/fadeout.
