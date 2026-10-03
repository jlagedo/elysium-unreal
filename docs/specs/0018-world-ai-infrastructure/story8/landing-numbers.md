# 0018/8 — landing numbers (orchestrator's record for brief F)

## Gate (2026-09-30)

| Gate | Result |
|---|---|
| `uv run elysium build` | green |
| `Elysium.Substrate` | 1,722 tests, 0 failed (baseline 1,705; +13 `HintSearch`, +3 in `NpcKernelHints`, +1 `NpcKernelBaseHelpers.CoverNodeFacing`) |
| `Elysium.Content` | 33 / 0 (baseline 27; +6 `Elysium.Content.Hints.*`) |
| `uv run elysium research kernel --check` | 7 / 7 after regen; override census 1,048 → 1,050 (`CoverRadius` on Pedestrian and Tzimisce), `kernel_shape --unported` 371 → 369; `unported.tsv` 393 → 393; `seam-list.md` 390 rows → 390 (the searches and claim primitives had no ledger row) |
| Save schema | `FElysiumSaveVersion::NpcHintClaimOnHint = 40`, `MinSupported` moved to it |

## Live check (`live-check.md`, brief H, three attempts)

Passed live:
- The registry is the live state: hint row 465 reads `hint_rating` 2.5 (the rulebook replacement),
  owner none, next use 0, class mask 1, node 41.
- `DisableHint` sets `m_iDisabled` 1 and hides; a hidden hint swallows `EnableHint` (retail's
  `AcceptInput` gate); `ScriptUnhide` alone unhides AND clears the disabled word (slot 78,
  `0x102d0890`); `StartHidden 1` alone does not disable (`cover_front_11`). Python
  `FindEntityByName('cover_front_10').DisableHint()` works and the hint reads disabled after.
- `sm_hub_1` idle ~3 min and the whole session (136.6k log lines): 0 ensure, 0 assert.
- The cower / melee / shoot-at searches found nothing, as the census predicts (no 10100 / 1030x /
  10400 rows on either witness).

NOT observed live, after three attempts: an NPC's combat-time tactical search claiming a cover
hint and releasing it. Each attempt was stopped by something outside this story:
1. `thug_3` (the census's witness, ranged, `StartHidden 1`): after `ScriptUnhide` it has a body
   but `Body owner: None gen=0`, `on_ground false`, and sits in `FALL_TO_GROUND (0x3e)` for the
   rest of the session; it sees and hates the player but never selects combat. HANDED ON as a
   hidden-NPC lifecycle / motor defect (the on-ground flag never rises for an NPC unhidden after
   spawn).
2. `patrol_cop_north` (hub): entered combat after `SetRelationship` and `EndDialog` on the
   map-load dialogue, but it is a baton cop, so it takes the melee branch (mask 8) and never runs
   the mask-1 cover search; the player also fell through the world when placed off-mesh.
3. `sentry3` / `Hunter1` (tutorial, ranged, unhidden, moved to the warehouse by Python
   `SetOrigin`): "SEEN, in cone" at 6-7 m and `D_HT 5` after `SetRelationship`, yet
   `Conditions (none)` and no combat within 85 s; `Hunter1`'s `hint_groups` is `6` (no group-1
   row admits). HANDED ON to senses: an authored-friendly NPC flipped by `SetRelationship` never
   enters the sighted list.
Also seen: a `map_load` of the same map kept the previous session's entity state (dead thug,
disabled rows) — handed on to world/session.

The claim / release chain is pinned instead by `Elysium.Content.Hints.TutorialTacticalSearch` and
`TutorialClaimAndCooldown` over the baked tutorial's 49 hints (row 465 first from the head, 464
after the cursor moves, the 5.0 s cooldown's strict `<`), and by the 13 `HintSearch` unit tests.

## Census (brief R2, `census.md`)

Tutorial 49 hints (12 mask-1 cover rows, 37 patrol points), hub 274 (234 mask-1, 34 patrol, 6
crosswalk); all 108 maps 3,156 hints, 54 maps with none, 96 `StartHintDisabled` rows on 7 maps;
216 `EnableHint` / `DisableHint` outputs authored, one Python site (`temple.py`). Only class mask 1
has candidates on either witness; the cower (10100), kick (10300 / 10301) and shoot-at (10400)
searches must miss there, which is census-correct.

## Corrections made on the way

- The spec's "no registry" line was stale: the entity, list, node binding, inputs and slot 566
  were already stood (0018/2, 0018/4, 0019/8); what was missing was the query and claim surface.
- `0x102d2980` takes `(npc, flags, mask, radius, origin*, outScore*)`; the port's two callers
  (`0x102b6b50`, `0x102b7110`) passed through a TYPE search with `8` — the flags byte — as the
  type. Now `FindHintByClassMask(8, 0x10, …)` and `(8, SearchType, CoverRadius())`.
- The port's comments on `0x102968f0` (direction of the ray) and `0x102b5de0` (a 3.0 s
  elapsed-occlusion test, not a distance; arm 1 fails only on a world hit) were wrong; both
  bodies are ported from the listing and the oracle corrected (`shape.md` new subsection).
- `m_flHintRating` is REPLACED by a row of `NPC_Cover_Distance_Scalar` (8, 3.5, 3, 2.5, 2, 1.5,
  1), not clamped: the authored 3 becomes 2.5 (seen live on row 465).
- Slot 550 is `CoverRadius`: 1024 base, 4096 for `CNPC_VPedestrian` and `CNPC_VTzimisce`.
- `0x102d1fe0` has no caller (dead); `0x102d0910` is live (the kick-hint hide walk), now on the
  hint as `NpcKicked` and wired from `TASK_KICK_HINT` / `_AT`.
- The NPC-side `bOwnsHint` / `HintReusableAt` stand-ins are deleted; the claim lives on the
  hint's own SAVE words. `ClearHintNode 0x10295ab0` releases only when `0x102d1450` says I own
  it; `UpdateOnRemove` releases with delay 0 and no owner gate; the base `TASK_LOCK_HINTNODE` arm
  calls `0x102d1350` directly (it went through a Troika-only path before).
- The duplicate `FUN_10296c40` / `AttackHintRejectReason` (four defects) is deleted; the cover
  validators `0x10295ed0` / `0x102961a0` now take the facing from `0x102d12e0` (a node-bound
  hint's NODE yaw) in Source axes, normalise 3-D with epsilon and dot X/Y, keep each compare's
  NaN direction, call the real `HintLosCheck` at the tail, and `0x102961a0` casts its own ray
  at mask `0x46804099` (it traced at mask 0 before, always clear).
- Two oracle corrections in `schedule-kernel.md` § validators: "normalize2D" → 3-D normalise
  then X/Y dot; NaN fractions count clear.

## Named divergences / crash guards

- A null anchor in `0x102d24b0` (retail faults): the port answers none, cursor untouched.
- A non-hint index in the claim primitives (retail dereferences NULL): named guards.
- `DefaultEyeOffsetCm()` stands for `m_vecViewOffset` (no stored word).
- `ClassMask` (+0x474, not saved) is re-derived on restore; whether retail's restore re-runs
  `Spawn` is unread — if it does not, retail's mask search finds nothing after a load.
- The idle gate's monsters-only mask falls back to `ECC_Pawn`, so its arm 1 can meet an
  NPC-solid brush retail cannot; the LOS filter's `ShouldCollide` and game-rules group arms are
  not asked (every combat character is refused, as retail).
- The weapon's `+0x8c0` max range is still the 1024 stand-in (family Motor's seam); the player's
  `BodyTarget` is a stub, so the idle gate traces to the world origin when the enemy is the player.
- The kernel draws the random pick on `EElysiumRngStream::NpcSchedule`.

## Handed on

- `0x102d0910`'s output activator / caller order; `m_iszUserData`'s reader; `_DAT_1044e674`.
- Story 9: the cooldown `+1.0` claim write and the selectors over these searches.
- 0002/12b: the kick chooser over `NpcKicked` (no kick hint exists on either witness).
- RE-BACKLOG: whether the engine's restore path re-runs `CAI_Hint::Spawn` (the `+0x474` question).
