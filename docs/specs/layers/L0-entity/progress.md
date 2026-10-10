# L0 progress — the orchestrator's log

One row per worker run (`runs.md`). Pipeline per run: Haiku CLI reader (API, max effort) walks the retail functions
(`walks/<run>.md`) → a Sonnet confirmer verifies the walk in place → one Opus worker ports the run's briefs (Fable until r017/r029/r030, Opus from r018 on, the owner's instruction)
(one coding lane per checkout, `method.md`) → the coordinator gates (build, default tier, the run's
records) and commits the run alone.

| run | walk | confirmed | worker | gate | commit |
|---|---|---|---|---|---|
| L0-r001 | — | — | done | build ok, default tier ok, 4 selftest records | `27ddd056` |
| L0-r002 | — | — | done | pytest 61, kernel --check ok, build ok, 169/0 | `6475c1b7` |
| L0-r003 | haiku 86 turns $0.51 | sonnet: constants recovered (1/36, 20, 40), 4 corrected | done, 39 min | build ok, 169/0, arena 20261010T041706 3 pass + selftests | `d4a32a29` |
| L0-r004 | haiku 71 turns $0.46 | sonnet: 4 corrected (strtol/atoi, callers of GetInt/GetFloat) | done, 30 min | build ok, 169/0, arena 2 pass + r003 records, selftests | `51e0a88c` | — | — |
| L0-r005 | haiku 82 turns $0.61 | sonnet: 3 corrected (preset block, SetMoveType, UTIL_Remove path) | done, 20 min (B) | B: build ok, 169/0, l0_ambient_init pass; main re-gate ok | `9dde13a1` (cherry-pick of lane-b 3a1e3295) | — | — |
| L0-r006 | haiku 126 turns $0.77 | sonnet: fadein 2560 not 512; slot 113 is Activate (once, ServerActivate); preset table decoded | done, 53 min | build ok, 169/0, kernel --check ok (ledger regenerated), arena 6 new + 9 prior pass | `665bd107` |
| L0-r007 | haiku 79 turns $0.44 | sonnet: lazy getters -2 sentinel, SndScheme_Char registry, RTTI CBaseTerminal/CPropSwitch | done, 42 min | build ok, 169/0, ledger regenerated, 3 new + 10 prior records pass | `5939e7ac` | — | — |
| L0-r008 | haiku 70 turns $0.43 | sonnet: RandomInt(0,0) draws nothing; rec+0x48 = missing-wave flag; case-insensitive interning |
| L0-r009 | haiku 79 turns $0.35 | sonnet: eviction key = quietest retiring; retire rate +0x48/duration; 0x1044fab0 double |
| L0-r010 | haiku 100 turns $0.61 (rerun via stdin) | sonnet: SetParent never touches triggers (bTouch 0), attach always 0, slot 115 no-op; MOVETYPE_FOLLOW |
| L0-r011 | haiku 149 turns $0.91 | sonnet: +0x494 is the host FRAME counter (story goal wrong), decal filter table, CTEBSPDecal |
| L0-r012 | haiku 103 turns $0.48 | sonnet: style 32 gates Spawn/Use only; inputs bypass Use; AcceptInput refuses non-string params |
| L0-r013 | haiku 99 turns $0.56 | sonnet: 0x10449280 is 1.0 (frames > 1); sprite angle arm SetAngles(pitch,0,yaw); blood x5 arm dead in SP |
| L0-r014 | haiku 52 turns $0.31 | sonnet: TE spray emits nothing (slot 14 bare RET; r013 slot 13/14 swapped); IsMultiplayer 0 kills x5 arm; Smoke scale 0.1 |
| L0-r015 | haiku 93 turns $0.48 | sonnet: fresh entity eflags 0x50000; FUN_100dc300 runs twice per entity; untouch queue consumer 0x100f9160 |
| L0-r016 | haiku 132 turns $1.03 (max) | sonnet: ctor eflags 0x54000 (no 0x8000 without edict); ParentName is m_iParent +0x124; lip formulas unclamped |
| L0-r017 | haiku 95 turns $1.01 (max) | sonnet: ParseMapData has 2 more dispatch sites; outputs arrive as type-10 KEY rows; debug gate is ent_debugkeys |
| L0-r018 | haiku 104 turns $0.81 (max) | sonnet: tokenizer 0x10136ce0 read (signed compares); mins/maxs/origin/angles arms set EFL 0x8000; Python __getattr__ bypasses ReadKeyField |
| L0-r019 | haiku 132 turns $0.82 (max) | sonnet: all six CONFIRMED; thunks not extra call sites; 0x450 size stands |
| L0-r020 | haiku 97 turns $0.61 (max) | sonnet: thunks not second bodies; NPC/player override BodyTarget; IsMonster byte 0/1 |
| L0-r021 | haiku 138 turns $0.98 (max) | sonnet: SetModel/UTIL_SetModel never precache (unprecached name fatal); IsViewable via GetModelType; weapon bHidden != script-hidden latch |
| L0-r022 | haiku 128 turns $0.89 (max) | sonnet: SetAbsAngles has NO partition walk; hooks.tsv:9 hook is 0x10092ef0 not 0x10093000; ctor sets EFL 0x10000 |
| L0-r023 | haiku 98 turns $0.94 (max) | sonnet: Teleport has 3 bodies, 12 callers; notify block has physicsRotate byte; base has no m_fEffects storage |
| L0-r024 | haiku 121 turns $0.86 (max) | sonnet: ctor leaves grow size 0 / current ctx -1; slot 15 SetAttackExtents updates the partition; last-think written after the callback |
| L0-r025 | haiku 191 turns $1.25 (max) | sonnet: PostConstructor forces serial -1; empty classname never enqueues (+0x448 = -1); CGameStringPool case-sensitive |
| L0-r026 | haiku 176 turns $1.08 (max) | sonnet: port classname is LAST occurrence, retail FIRST (bake folds last-wins); qsort is VC6 CRT (port verbatim); 0x10352f00 runs before thinks |
| L0-r027 | haiku 150 turns $0.76 (max) | sonnet done |
| L0-r028 | haiku 182 turns $0.85 (max) | sonnet: stat-action callback resolves 3 names (port knows 1); FString == is case-insensitive; TMultiMap returns last landmark first |
| L0-r029 | haiku 93 turns $0.67 (max) | sonnet: cleanup inlined (no callees); 5 blocks Entities/EventQueue/Physics/AI/Python; slot order 17/18/19, restore 20 then 21 | done, 48 min (B) | B: build ok, 169/0, kernel ok, l0_save_restore_blocks + V6 checkpoint records pass | `c5617a18` (cherry-pick of lane-b c8a56c36); follow-up `08ad2527` (bindings keep retail's KEY flag for max_npcs/nextthink — exposed by restore_place_invalid_marker) |
| L0-r034 | haiku 88 turns $0.85 (max) | sonnet: pickers return the node/link in EAX; 4 callers via thunks; GetPosition already ported (FElysiumPlaceSet::GetPositionCm) |
| L0-r030 | haiku 125 turns $0.80 (max) | sonnet: changelevel args are (oldLevel, landmark); m_szMapName +0x598 / m_szLandmarkName +0x5b8 (walk had them swapped); transition mask aliases >= 32 |
| L0-r031 | haiku 166 turns $1.01 (max) | sonnet: 0x101707c0 gate is pOldLevel != NULL (not a float); slot 134 is Think; m_fFlags +0x434 |
| L0-r032 | haiku 130 turns $0.81 (max) | sonnet: IsFloating's this is the ground entity; -9999 sentinel skips the distance test; slot 135 is CBaseEntity's own default |
| L0-r035 | haiku 98 turns $0.98 (max) | sonnet: a BSP/static-prop hit leaves trace.m_pEnt = the WORLD entity (port writes Invalid — unnamed divergence); view-pick runs only on a total miss |
| L0-r036 | haiku 129 turns $0.87 (max) | — (not yet confirmed) |
| L0-r033 | haiku 111 turns $1.01 (max; out of order, a lane-B candidate) | sonnet: acos(+-1) finite; A14/A15 = cand between src/dst; port has no link storage; hooks.tsv:345-346 stale |
| L0-r026 | haiku running | — | — | — | — | — | — | — |

## Where this stopped (2026-10-10 13:50 UTC, the owner's stop)

On `main`: runs r001-r021, r029, r030 (23 of 88). Walks confirmed and waiting for a worker: r022, r023, r024,
r025, r026, r027, r028, r031, r032, r033, r034, r035; walked, not yet confirmed: r036. Next in order:
lane A r022 (parented transforms; after r021), r024 (think contexts / base spawn / removal; after r015, r016);
lane B r033/r034 (view-node links; after r001) or r031 (player map restore). Lane B (`E:/dev/elysium-unreal-b`)
is reset to `main`. Before the layer closes: rebake sprites (r013 `frames`) and sp_giovanni_2b (r018), run the
arm tier (ElysiumNpcKernelPositionsTests.cpp:799 assumes IsViewable false), the full arena and
`contract-checks.md`, settle the owner notes below.

## Lanes

Lane A: this checkout (`main`). Lane B: the clone `E:/dev/elysium-unreal-b` (branch `lane-b`, own binaries,
`Plugins/ElysiumBaked/Content` junctioned to lane A's baked content, `Plugins/External` copied); its runs are
gated in the clone, cherry-picked onto `main`, and re-gated here (`decisions.md` D2).

## Planning faults (reported by workers)

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
