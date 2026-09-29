# Lane F · flex, overlay, test hull, makers, misc (wave 3)

Read `README.md` first, then `report-S.md`. `ElysiumWorldServices.h` is frozen. You own, and only
you may edit (under `Source/ElysiumUE/Private/`):

- `Substrate/ElysiumFlexSlotBodies.cpp`, `Substrate/ElysiumAnimatingOverlaySlotBodies.cpp`
- `Substrate/ElysiumNpcTestHull.h`, `ElysiumNpcTestHull.cpp`
- `Substrate/ElysiumNpcMaker.cpp/.h`, `ElysiumNpcMakerFleshpile.cpp/.h`, `ElysiumNpcMakerZombie.cpp`,
  every other `Substrate/ElysiumNpcMaker*`
- `Substrate/ElysiumInterestingPlace.cpp`
- `Substrate/ElysiumNpcBaseMisc.cpp`, `ElysiumNpcMisc.cpp`, `ElysiumNpcBaseHelpers2.cpp`,
  `ElysiumNpc.cpp`, `ElysiumNpcBase.cpp`, `ElysiumNpcThink.cpp` (its 3 rows), and their `.inl`
- Their tests under `Tests/` for what you change

Not yours: `ElysiumNpcBase.h`, `ElysiumNpc.h` (report declarations to remove), Motor / Facing (M),
EntitySlotBodies / Closure / Senses (E), species files and Precache (P), generated files.

## The standing rule applies hardest here

Gesture layers belong to **0015 weapon-overlays**; facial flex, scene events and eyes to
**0010 theatre-scene**; the test hull's bake model to **0018/3**; makers and templates to
**0018/16**; the player-on-head push-out to **0002/28**. You build no seam for any of them.
For each such row: if the named spec has already built the seam, forward the body to it in
one line; if the port body is a refusal today (returns nothing / logs / `ElysiumStub`), delete
it and set the target to the owning spec's spelling (`0010`, `0015`, `0018/3`, `0018/16`,
`0002/28`); if the body does real work that neither spec has landed, leave it and report it.

## Rows

Every `mechanism` row in `seam-list.md` § "Port bodies owed a seam" whose "Port sites" names your
files: Flex 16, Overlay 16, TestHull 19, Maker* ~20, InterestingPlace 5, BaseMisc 14, Misc 11,
BaseHelpers2 9, ElysiumNpc.cpp 10, ElysiumNpcBase.cpp 4, Think 3.

## What the survey found (verify, then act)

- **Flex** rows are morph-target weights, choreo scene-event arrays and the eye look-at driver.
  0010's seams that exist: `SetFlexControllers` (`ElysiumWorldServices.h:995`) and the scene
  player (`ElysiumScenePlayer.h`). The slot bodies still refuse (`ElysiumFlexSlotBodies.cpp:31,116,155,182`).
  Forward where the seam matches the retail slot's contract; else delete the refusal, target `0010`.
- **Overlay** rows are gesture layers and the additive flinch layer. 0015 owns the overlay
  stack; `AElysiumNpcBody::SubmitAnimRequest` (`ElysiumNpcBody.cpp:187`) exists but nothing in
  Substrate calls it, and you may not add a Substrate-side seam (the header is frozen and the
  seam is 0015's). Delete refusals, target `0015`; forward only if a Substrate-visible request
  path already exists (`rg -n "SubmitAnimRequest|AnimRequest" Source/ElysiumUE/Public`).
- **TestHull** (19): retail `CAI_TestHull` walked the graph at bake; the pipeline models it
  (0018/3, step 40, `0x102d72b0`). Delete the runtime bodies and the class if nothing else
  names it (`rg ElysiumNpcTestHull Source/ pipeline/`); target `0018/3`. If a test fixture
  spawns it, report the fixture.
- **Makers / InterestingPlace / template** rows whose service is "Unreal asset references
  resolved at bake and load" / "cooked package loading": delete, target `Bake`. Maker
  *behaviour* (spawn cadence, counts, the `Use` input) is 0018/16's and is not in the seam list;
  do not touch it.
- **BaseMisc / Misc / BaseHelpers2 / ElysiumNpc.cpp / ElysiumNpcBase.cpp / Think** rows: read
  each row's "Unreal service" cell; forward to the named existing seam (actor transform,
  `SurroundingBounds`, the motor, the tunables) or delete with the service spelling. "C++
  virtual dispatch" / `UStruct` / `UClass` / `RTTI` rows: no body should exist; if one does,
  delete it; target stays the service word.
- `ResolveStandingOnHead` is in `Geometry.cpp` (M's file) and is 0002/28's: not yours; the
  orchestrator spells its target.

## Deliver

`report-F.md` (≤300 words): rows by owning spec (forwarded / deleted / left with reason),
bodies deleted, fixtures that named the test hull, declarations to remove ("Needs another owner"),
`targets-F.tsv`.

## Added after wave 1 (read before starting)

- **Standing `dead` rows in your files:** `residue-dead.md` § "Lane F". Delete body and tests or
  report a live observer for re-verdict; add each closed address to your `targets-F.tsv` with `-`.
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
