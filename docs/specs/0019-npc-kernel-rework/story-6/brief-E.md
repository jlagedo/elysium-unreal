# Lane E · traces, EntityChain's engine rows, the filter arms (wave 3)

Read `README.md` first, then `report-S.md`. `ElysiumWorldServices.h` is frozen. You own, and only
you may edit (under `Source/ElysiumUE/Private/`):

- `Substrate/ElysiumEntitySlotBodies.cpp`, `Substrate/ElysiumNpcClosure.cpp`,
  `Substrate/ElysiumNpcSenses10.cpp`, `Substrate/ElysiumNpcSensesBodies.cpp` (its 3 rows),
  `Substrate/ElysiumAnimatingSlotBodies.cpp` (its 4 `mechanism` rows), `Substrate/ElysiumCombatCharacterSlotBodies.cpp`
- `Map/ElysiumRetailMaskRecipe.h`, `Map/ElysiumMapActorGeometry.cpp`
- `Tests/ElysiumNpcKernelEntityChainTests.cpp`, `ClosureTests.cpp`, `Senses*Tests.cpp`,
  `Tests/ElysiumGeometryNavTests.cpp`, `Tests/ElysiumMapActor*Tests.cpp` for what you change

Not yours: the Motor files, `Geometry.cpp` (M), species files (P), `ElysiumNpcBase.h` / `ElysiumNpc.h`
(report), generated files.

## Rows

Every `mechanism` row in `seam-list.md` § "Port bodies owed a seam" whose "Port sites" names your
files (EntitySlotBodies 30, Closure 15, SensesBodies 3, AnimatingSlotBodies 4, CombatCharacter 3).

## What the survey found (verify, then act)

- **Physics and network → nothing.** `Physics_TraceEntity` (`EntitySlotBodies.cpp:167`, refuses:
  no port `trace_t`), `MakeTracer` (`:194`), `VPhysicsDestroyObject` (`:231`), the physics tick
  (`:438`, `0x100b4f30` → `VPhysicsUpdate`), the replication slots (`:147-150`, transmit record
  `+0x1b0`), `GetNetworkable` (`:698`). Body and tests go; targets `Chaos` / `Replication`. If
  the slot is dispatched from a live chain (check `slots.md` / the generated dispatcher), the
  generated `default:` stays and you only delete the hand body; say which in the report.
- **Already forwarding, spell the target:** `SetOrigin` (`:131`), `SetAngles` (`:469`),
  `WorldSpaceCenter` → `SurroundingBounds` (`:217,223`), `SetRefEHandle`, `IsMarkedForDeletion`
  (`:84,728`, AActor pending-kill). Keep the one-line forward; target stays `hand:…`; confirm no
  own math remains in each.
- **Floor rows** `:496`, `:527`, `:772`, `:785` (`CanStandOn`, `GetGroundVelocityToApply`) →
  the floor facts seam from wave 2. Keep retail's `CanStandOn(null)` = standable and the
  MingXiao proxy / tentacle exclusions (they are rules on the answer, `:772` cites them).
- **`StandardFilterRules` arms (g)** listed at `Map/ElysiumRetailMaskRecipe.h:55-66` and built
  nowhere: render mode (without WINDOW), solid `0x20`, the `blocks_traces` / `npc_transparent`
  keyfields. First a corpus read: are those two keyfields authored in any shipped map? `rg -i
  "blocks_traces|npc_transparent" Content/ElysiumCorpus/ $ELYSIUM_WORK_ROOT/exports_v2/`
  (read `.elysium.local.env` for the root; read only). If authored anywhere, the arm is a rule:
  carry it in `ElysiumMapActorGeometry.cpp`'s filter reading the entity's keyfield through the
  entity record the map actor already holds (0018/21-7 landed the structural entity read). If
  authored nowhere, record "unauthored, arm not carried" with the grep scope in the report.
  Render mode and solid `0x20` are entity words the port has (`fields.md`): carry both arms
  in the filter; a test per arm on the tutorial fixture the geometry tests use.
- **The `FVisible` blocker cell (i)**: `Senses10.cpp:83,172` drops the entity that blocked the
  ray. `TraceRetail` returns the hit list; deliver the first blocker into the cell retail writes
  (`0x1027…`, `FVisible`'s fourth argument; cite from `functions.md`) and test it with the
  recording services (`ElysiumTestServices.h:1695`).
- `MoveDone` (`AnimatingSlotBodies.cpp:224`, mover-completion delegate): the NPC rule is
  `0x101c1720`; the helper-entity base only jumps through `m_pfnMoveDone`. Forward to the mover
  seam if one exists (`rg MoveDone Source/ElysiumUE/Public`), else leave the nothing-seam named.
- The closed-room fixture is live-only (`ElysiumGeometryNavTests.cpp:10-11` skips off the
  NavMesh): leave the skip; the orchestrator runs it live at the close. Note the test name.

## Deliver

`report-E.md` (≤300 words): rows closed by service, bodies deleted, the keyfield grep verdict
with scope, arms carried, the blocker cell's retail address, `targets-E.tsv`, "Needs another owner".

## Added after wave 1 (read before starting)

- **Standing `dead` rows in your files:** `residue-dead.md` § "Lane E". Delete body and tests or
  report a live observer for re-verdict; add each closed address to your `targets-E.tsv` with `-`.
- **The seam surface wave 2 landed** is in `report-S.md`: `IElysiumNpcMotor::SampleFloor`
  (`FElysiumNpcFloorFacts`: on-ground, walkable, ground entity, normal, distance, step height, walkable
  Z / angle), `SetMoveIgnore(handle, bool)`, `HasPath()`. Step height and slope are on the FLOOR facts,
  not the move facts. The floor probe reaches ~step height + 2.4 cm; retail's `CheckOnGround` trace
  reaches 4.0 units, so the kernel tests `FloorDistanceCm` against 4.0 itself. The ignore seam is a
  list: a species `NavIgnoreCollision` that decided contact by contact must register its entities
  ahead of the move, and say so in a comment.
- **Lane A already moved** `RetailBonePosition` into `ElysiumNpcGeometry.cpp` (declared in
  `ElysiumNpcKernelBaseHelpers.inl`); the Debug files are gone. `ElysiumNpcKernelShapeMap.cpp` is a
  hand-kept ledger the bindings generator reads: a deleted word needs its row turned to
  `ELYSIUM_NPC_WORD_ABSENT` there (orchestrator does it; report the offset).
