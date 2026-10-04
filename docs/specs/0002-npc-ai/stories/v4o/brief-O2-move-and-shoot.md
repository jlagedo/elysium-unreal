# Brief O2 — V4o: the move-and-shoot overlay `0x102e8560` (coder; no build)

Read `README.md` here (§1 "The wire", §2 P5, §4 "Shared names", §6, §7),
`docs/vtmb/animation_events.md` → "The move-and-shoot overlay, arm by arm"
(`uv run elysium research section 0x10098cd0`), `docs/vtmb/npc-ai/senses.md` § "`StartTaskOverlay`
`0x10288710`", `docs/vtmb/npc-ai/shape.md` § "`OverrideMove`, `ShouldMoveAndShoot`…". After V4a's
commit. Re-locate every site by Grep on the function name.

## Files (only these)

- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseMaintain.cpp` (`FElysiumNpcBase::RunTaskOverlay` only)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseSenses10.cpp`, `ElysiumNpcBaseSenses10.inl`
  (`FMoveAndShootOverlay`, `ArmMoveAndShootOverlay`, `DisableMoveAndShootOverlay`, the new
  declarations)
- `Source/ElysiumUE/Private/Substrate/ElysiumNpcBaseMoveAndShoot.cpp` (new: the three bodies)
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelSenses10Tests.cpp` (the slot-445 block only)
- `Source/ElysiumUE/Private/Tests/ElysiumNpcKernelMoveAndShootTests.cpp` (new)

## The job

Read the four bodies in the listing first (`vtmb_code 102e8270`, `102e83e0`, `102e84a0`,
`102e8560`; `vtmb_asm` where a constant is folded). Then:

1. **The words**: `FMoveAndShootOverlay` gains `bMovingAndShooting` (`+0x10`), `MoveShots`
   (`+0x14`), `MinBurst` / `MaxBurst` (`+0x1c` / `+0x20`), `InitialDelay` (`+0x2c`, 0 from
   `0x1027c300`); `UpdateCalls`, `Arms`, `Disables` go with their assertions.
2. **`ArmMoveAndShootOverlay` whole** (`0x102e8270`): state 4, no weapon, or
   `SelectHeaviestSequence(TranslateActivity(0x11))` / `(0x15)` below 0 → the disable; the burst
   pair from the weapon data (`+0x3a4` / `+0x3a8` — Grep the weapon record for the two fields; a
   record with no such field is a seam named for the offset, answering 0, reported);
   `MoveShots = RandomInt(MinBurst, MaxBurst)` on the `NpcSchedule` stream;
   `NextShotTime = curtime + InitialDelay`.
3. **`RunMoveAndShootOverlay()`** = `0x102e8560`, the nine steps of the doc section in their order,
   with `CanAimAtEnemy` = `0x102e83e0` and `UpdateMoveShootActivity(bool)` = `0x102e84a0` as their
   own functions. Every slot it calls exists in the port — find each by address
   (`uv run elysium research where "slot 478" "slot 560" "slot 481" "slot 517" "slot 381"
   0x102ee3f0 0x102ee250 0x102dfed0`): `BestEnemy`, `ClearAttackConditions`, `SetEnemy`,
   `SetState(2)`, `NavIsGoalActive`, the navigator's movement activity getter and setter,
   `TranslateActivity`, `WeaponSetActivity`, `AddFacingTarget`. The layer calls are O1's
   `AddGesture(int32, bool)` and `HasLayer(int32)`. Slots 557 / 558 are `return 1` / empty on every
   class: call the existing slots. The fire rate is the weapon's slot 332 (`0x10254410`: the weapon
   data's `+0x260` through the owner's scale `0x1033d940`); the constants are 0.3 (`0x1047b868`)
   and 0.1 (`0x104493d0`).
4. **The `0x47` / `0x48` gesture arm** (step 7): recover the object at `0x10923cf4` (`vtmb_asm
   102e8560` from `0x102e8600`; `vtmb_globals`), its default, and the doubles `0x1044e668`,
   `0x1049d910`, `0x10449260`. Recovered → port it, the `RandomInt` on the `NpcSchedule` stream,
   and record the values in `docs/vtmb/animation_events.md` by one targeted Edit of that step.
   Not recovered within the budget → a seam answering "gate closed", named for `0x10923cf4`, and
   say so first in your report: it decides whether a draw is taken.
5. **`RunTaskOverlay`** `0x10289c90`: slot 529 true → `RunMoveAndShootOverlay()`.
6. **Tests** `Elysium.Arm.NpcKernelMoveAndShoot.Arm`, `.CanAim`, `.MoveActivity`, `.Run` (README
   §6). In `ElysiumNpcKernelSenses10Tests.cpp` delete only the assertions that pin the counters or
   the arm taken without the sequence test; list them.

## Not yours

The layer table, its advance and dispatch (O1); the weapon's handler (O3); `StartTaskOverlay`,
slot 575 and slot 529 (retail already — do not rewrite); the attack conditions (V5); the
navigator's activity → sequence commit beyond the existing setter.

## Rules

README § "Rules for every agent of V4o". Retail first; cite the address at every line. Never
build, launch or run. Only your files; a line another file needs goes in your report, exact, with
its place. A divergence is recorded, not adopted. Query budget: 10 s warns, 60 s stops. Never read
a file over ~200 KB whole. Grep / Read / Glob. Do not commit. Report ≤300 words: what you ported
(addresses), tests added and deleted, cross-lane lines, what stayed unrecovered.
