# Lane R3 · the wave-3 residue sweep and the seams the lanes could not add (alone)

Read `README.md`, then `report-S.md`, `report-M.md`, `report-E.md`, `report-P.md`, `report-F.md`
(their "Needs another owner", "Against the brief", "Open because a seam is missing" and
"Ownership flags" sections are your list). Lanes M, E, P, F are finished; no one else edits
`Source/` now, and **the header freeze is lifted for you**: you may edit
`Public/ElysiumWorldServices.h`, `Visual/ElysiumNpcBody*`, `Tests/ElysiumTestServices.h`.
You still may not edit generated files (`*Slots.inl`, `*Slots.cpp`, `ElysiumNpcKernelShape*`,
`Tests/ElysiumNpcKernelOverrideCensus.cpp`, `ElysiumNpcKernelTunables.*`, `ElysiumInfraKeyfields.h`,
`Visual/ElysiumNpcActivityTables.cpp`), the ledger, `pipeline/` or `research/`. The orchestrator
has already regenerated the declarations the lanes asked for. Do not build.

## Decisions taken (apply them)

1. **The yaw ladders come back as data through the seam** (M's "against the brief"). Retail's
   `MaxYawSpeed` ladder (`CAI_BaseNPC` `0x102e1c10`-area base, the Troika ladder per activity,
   the species overrides Dog / MingXiao / Tzimisce / Werewolf that lanes M and P deleted) is a
   player-visible timing: how fast an NPC turns. The mechanism is the turning; the rate is
   retail's threshold and stays. Recover the deleted bodies from `git diff` (`git diff -- <file>`
   shows them; `git show HEAD:<path>` for the whole file) and restore them as **rule-shaped
   one-liners / tables returning the yaw speed for the current activity**, in their original
   classes and slot (516 `MaxYawSpeed`), with their constants as `ElysiumNpcTunables::` names
   (list any missing cell in `cells-R3.tsv`: address, name, type, value, evidence). Then make the
   seam carry it: add a yaw-speed field to `FElysiumNpcMoveRequest` and a `Face(target, yawSpeedDegPerS)`
   overload (or a field on the existing request) on `IElysiumNpcMotor`; the live body sets
   `RotationRate.Yaw` from it before turning (`ElysiumNpcBody.cpp:~107,592-601`); the recording
   motor records it. The kernel's callers of the motor pass `MaxYawSpeed()`. Retail reads the
   ladder in `CAI_Motor::UpdateYaw` (`0x102e1e20`-area): pass it where the port issues the turn.
   Tests: the restored ladder rows (from the deleted tests, trimmed to the values) and one
   recording-motor test that the request carries the rate. Report the rows you restored in
   `targets-R3.tsv` with target `hand:<Class>::MaxYawSpeed` so the orchestrator flips their
   verdicts back from the service word.
2. **Slot-584 one-shot** (M's item 4): on the entity world, a flag reset whenever the build stamp
   is set (0018/5's `BuildStamp`, `SetPlaceSetPending`); the first tick with
   `Now >= BuildStamp + 0.8` runs `Slot584(0)` on every NPC, once. Retail: the network manager's
   one-shot think `0x102f6a50` at build stamp + 0.8 s. Test with the recording clock the think
   tests use. File: `ElysiumEntityWorld.cpp/.h` (or the world tick owner the build stamp lives in).
3. **Three seams** (M's "open because a seam is missing"), added to `IElysiumNpcMotor` with live
   and recording implementations, then the listed rows forwarded: `SetHullSize(mins, maxs)` →
   `UCapsuleComponent` size (rows `0x10273070`, `0x10273180`); a facing-while-moving target on
   the move request (`0x10278d20`, `0x10278d90`, `0x102e20f0`, `0x102e2120`, `0x102e2150`,
   `0x102e11f0`: AAIController focus / `bUseControllerDesiredRotation` on the body); `QueryRoute`
   with an explicit start (`WerewolfHasPath 0x103d0db0`). Each forward is one line; each seam's
   comment names the retail word.
4. **`SetMoveIgnore` registration**: the species `NavIgnoreCollision` answers (slot 68/69 bodies
   in Motor.cpp and Werewolf.cpp, `0x10379490`, `0x10380f90`, `0x103bfa00`) are registered through
   `SetMoveIgnore` before each move is issued (where the port calls `MoveTo`), and cleared when
   the move ends. One test on the recording motor.
5. **Prop filter arms** (E): add a filter-kind to the trace request (`FElysiumRetailTrace` or the
   `TraceRetail` signature) so a prop hit can be judged; carry `blocks_traces` (21 authored
   `prop_dynamic`) and `npc_transparent` (8,096 rows) for props: at prop spawn
   (`ElysiumProp.cpp`, `ElysiumPhysProp.cpp`, `ElysiumEntityBodiesProps.cpp`) read the keyfields
   the map actor already holds and choose the prop's mask bit accordingly (`blocks_traces=1` →
   blocks; `npc_transparent` → transparent to the MONSTER filter). Tests on the scripted world E
   used (`Tests/ElysiumMapActorTraceFilterTests.cpp`).
6. **Every "Needs another owner" edit** in the five reports that is a hand-file edit: the
   `VampireBoss.cpp` `Restore` / `VampireBossRestore` deletion (P; the build breaks without it),
   the test blocks that call removed bodies (M's list; P's Precache10Tests werewolf lines; E's
   Lifecycle/Motor test lines), the orphan declarations (F's three `.inl` / `.h` lists, E's
   `Public/ElysiumEntitySlotBodies.inl` list, M's `HintOverlayWords` and `IntervalMovementApplied`),
   `ElysiumNpcBaseEntityChain.cpp` Slot579, `ElysiumNpcBosses.inl` `FPhysicsTraceEntityCall`,
   `LookupFlexController`, F's `EntityChainTests.cpp` edits kept, P's `BaseSaveRestore10.cpp:358`
   `CreateComponents` call and `Rat.cpp:51-52`, MiscTests:275's rat slot 428 (now 0x1027cf60),
   Bach's Precache 0x103637b0 → delete (`Bake`), the base-layer bodies 0x1027bc60 / 0x1027c160 /
   0x1027bf50 / 0x1027bb20 (read their rows in `seam-list.md`; forward or delete per the service).
   `SaveRoundTrip` (`Tests/ElysiumNpcKernelBindingsTests.cpp`): add P's named exceptions
   (boss line +0x6680, +0x6684..0x6690; Asian / Chang / Sheriff +0x64b8) with the reason.
7. **Orphan sweep** as R1 did (declaration without definition in hand `.h` / `.inl`), and an
   include sweep.

## Not yours

`docs/vtmb/npc-ai/senses.md:1156` and the ShapeMap `+0x01b0` row (orchestrator). The tunables
overlay (deliver `cells-R3.tsv`). The `ref` rows.

## Deliver

`report-R3.md` (≤300 words): items 1–7 done / not with reason, the new seam signatures (one
line each with the retail word), rows restored, `targets-R3.tsv`, `cells-R3.tsv`, anything that
still names a deleted symbol.

## 8. Stale citations of closed rows

`cites-R3.txt` in this directory lists every closed row still cited after wave 3 (56 lines from
`kernel_lists --check`). Apply the rules of `brief-R1c.md` to each: a comment that only names
the dead or replaced body goes or is reworded without the address; a surviving declaration or
constant whose only user was the removed body goes; a `Tests/` comment recording the removal
may stay; a live address stays. Finish with `rg` over `Source/` showing each address only in
generated files or in a removal note.
