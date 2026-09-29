# Lane P · persistence twins and the species mechanism rows (wave 3)

Read `README.md` first, then `report-S.md`. `ElysiumWorldServices.h` is frozen. You own, and only
you may edit (under `Source/ElysiumUE/Private/Substrate/` unless stated):

- `ElysiumNpcSaveRestore10.cpp`, `ElysiumNpcPrecache10.cpp`, `ElysiumNpcBasePrecache10.cpp`,
  `ElysiumNpcTroikaHelpers2.cpp`, `ElysiumNpcBaseHints.cpp`, `ElysiumNpcStartTaskSpecies.cpp`
  (its 8 `mechanism` rows only), and their `.inl`
- Species files: `ElysiumNpcMingXiao.cpp`, `ElysiumNpcMingXiaoTentacle.cpp`, `ElysiumNpcTzimisce.cpp`,
  `ElysiumNpcTzimisceRunner.cpp`, `ElysiumNpcTzimisceHeadClaw.cpp`, `ElysiumNpcHengeyokai.cpp`,
  `ElysiumNpcSabbatLeader.cpp`, `ElysiumNpcNewscaster.cpp/.h`, `ElysiumNpcChangBros.cpp`,
  `ElysiumNpcAndreiBlood.cpp/.h`, `ElysiumNpcGargoyle.cpp`, `ElysiumNpcCamera.cpp`,
  `ElysiumNpcSheriffMan.cpp`, `ElysiumNpcRat.cpp`, `ElysiumNpcManBat.cpp`, `ElysiumNpcAsianVampire.cpp`,
  `ElysiumNpcDog.cpp`, `ElysiumNpcGhoulCroucher.cpp`, `ElysiumNpcZombie.cpp`, `ElysiumNpcVampireBoss.h`,
  `ElysiumNpcBosses.cpp`, `ElysiumNpcWerewolf2Species.cpp`, and their `.h`
- `Tests/ElysiumNpcKernelSaveRestore10Tests.cpp`, `Lifecycle2Tests.cpp`, `SpeciesLifecycle10Tests.cpp`,
  `Precache*Tests.cpp`, `Hints*Tests.cpp`, and each species' tests for what you change.
  `Tests/ElysiumNpcKernelBindingsTests.cpp` (`SaveRoundTrip`) is NOT yours: report needed exceptions.

Not yours: `ElysiumNpcWerewolf.cpp` (M), Motor files (M), `ElysiumEntitySlotBodies.cpp` (E),
`ElysiumNpcBase.h` / `ElysiumNpc.h` (report), generated files.

## Rows

Every `mechanism` row in `seam-list.md` § "Port bodies owed a seam" whose "Port sites" names your
files. Species counts from the survey: MingXiao 20, Tentacle 18, TzimisceRunner 10, HeadClaw 9,
Tzimisce 9, Hengeyokai 9, SabbatLeader 8, Newscaster 8+4, ChangBros 8, AndreiBlood 8+4,
Gargoyle 7, Camera 7, SheriffMan 6, Rat 6, ManBat 5, AsianVampire 5, Dog 4, GhoulCroucher 4,
Zombie 3, VampireBoss.h 6, Bosses 4; plus SaveRestore10 5, Precache10 4, TroikaHelpers2 5,
BaseHints 5, StartTaskSpecies 8.

## What the survey found (verify, then act)

- **Save/Restore twins (retail slots 126/127).** Nothing live calls the virtual `Save`/`Restore`;
  the port's persistence is the generated SAVE walk (0019/2) plus `FElysiumEntity::OnPostRestore`
  (slot 130, `ElysiumNpcLifecycle2.cpp:551`, dispatched by `ApplyEntityRecord`). The only callers
  of `TroikaSave`/`TroikaRestore` (`SaveRestore10.cpp:136+`) are species overrides: MingXiao
  `:250,265`, Tentacle `:150,161`, HeadClaw `:224,235`, VampireBoss `:298`, and `Restore`
  overrides in AndreiBlood, AsianVampire, ChangBros, SabbatLeader, SheriffMan. For each override
  read the body against its retail twin (`functions.md`; `vtmb_func`): a field write the SAVE
  walk already carries → delete; **load-side logic** (a re-find by name, a handle fix-up, a
  clamp, a timer re-arm) → move it into that class's `OnPostRestore` override in retail's order,
  with a test through `Freeze` / `ApplySnapshot` (`ElysiumRoundTripSnapshot`, the pattern in
  `Lifecycle2Tests.cpp`), then delete the override. Then delete `TroikaSave` / `TroikaRestore`
  and the file's other save twins. **Keep** `UpdateOnRemove` (`:353`, live from
  `ElysiumNpc.cpp:682,705`) and `RunAlternateAiDoorMode4` (`:398`). Target `FElysiumSaveArchive`.
  The generated declarations in `*Slots.inl` are the orchestrator's: report the slots.
- **Precache rows** (`Precache10.cpp`, `BasePrecache10.cpp`, species `Precache` overrides):
  asset references resolve at bake and load in this port ("Unreal asset loading of baked package
  references"). A `Precache` body that only precaches → delete, target `Bake`. A `Precache`
  body that also writes a word a rule reads later (the survey flagged the base one reading slot
  452 `LoadedSchedules`, which lane C removed in wave 1) → keep that write, delete the rest, and
  say so. Read retail for each (`0x1027…`, `functions.md`).
- **Hull bits / collision-ignore / `HasPath` / `MaxYawSpeed` / `CheckStuck` / `GetGroundpoint` /
  walkable-base (slot 166)** species rows: forward to the seams wave 2 added (`report-S.md`), to
  `SurroundingBounds`, or delete the override where the base body is going (lane M deletes base
  `MaxYawSpeed` this wave: species `MaxYawSpeed` overrides go too, their yaw constants become
  tunables cells you list). Retail per-species constants (Ming Xiao hull 50, tentacle 30,
  Tzimisce 56 — 0018/6 R1 §5) are data: if the tunables table lacks a row, list the cell.
- **Templates / makers / interesting places are not yours** (lane F).

## Deliver

`report-P.md` (≤300 words): overrides deleted / moved to `OnPostRestore` (per class, one line
each with the retail address), Precache verdicts, species rows by service, tunables cells to add,
`SaveRoundTrip` exceptions needed, declarations to remove ("Needs another owner"), `targets-P.tsv`.

## Added after wave 1 (read before starting)

- **Standing `dead` rows in your files:** `residue-dead.md` § "Lane P". Delete body and tests or
  report a live observer for re-verdict; add each closed address to your `targets-P.tsv` with `-`.
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
