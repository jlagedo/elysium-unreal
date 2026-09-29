# Report P · persistence twins and species mechanism rows

(Saved by the orchestrator from the lane's final answer. Nothing built or run by the lane; corpus server down.)

**Save/Restore (slots 126/127).** Deleted: the Troika `Save`/`Restore` bodies (0x102993c0, 0x10299700) with their helper codecs, and the species twins that only wrapped time stamps around the archive: MingXiao (0x10395f80/0x10396000), Tentacle (0x1039ed50/0x1039eda0), HeadClaw (0x103c2810/0x103c2860). Moved to `OnPostRestore` (running before the parent's restore chain): VampireBoss (0x103c5910) resets model name, emitter names and classname; Andrei (0x1035cf80) inherits; Asian Vampire (0x10360e10), Chang (0x1036b170), Sabbat Leader (0x103a6e80), Sheriff (0x103ae7f0) run the resets then their own writes. Sabbat Leader's and Sheriff's own writes were missing from the port before. Tests: round trips through `Freeze`/`ApplySnapshot` in Lifecycle2 and SpeciesLifecycle10.

**Precache (slot 104).** Base body 0x1027bb50 emptied (only requested an asset). Troika body 0x10298ad0 keeps only its writes: the error-model fallback, the disposition index `+0x64e8`, the slot-608 bind. 15 species bodies deleted (target `Bake`); Tentacle and Camera keep only their writes.

**Species motor rows.** Deleted: `MaxYawSpeed` for Dog, MingXiao, Tzimisce (`CMC`) and the ManBat trace 0x1038fb20. Kept as `hand:`: collision-ignore rules, `CanStandOn`, the jump constants, `CheckStuck`. Forwarded: 0x102c4380/0x102c43b0/0x102c43f0 → `SetMoveIgnore`; 0x103bf3c0 is an `IndexOfByPredicate`; Chang and Sabbat Leader distance helpers use `FMath`. Dead row 0x1026a910 deleted. `ElysiumNpcStartTaskSpecies.cpp` holds only call sites, unchanged.

**Refused, sent for re-verdict.** The 7 `GetUsedHullBits` rows (0x10368e80, 0x10378680, 0x1037fb20, 0x1038b100, 0x10392a50, 0x1039c480, 0x103ae840): same shape as the three review-1 kept. Live slot-130 load logic: 0x1039f000, 0x103c3c40, 0x102d3ec0.

**Tunables cells to add:** 104a9300 `AsianVampireJumpGravity` 2.0; 104ada44 `ChangBrosJumpGravity` 2.3; 104c6148 `SheriffManJumpGravity` 2.0; 104c3d14 `SabbatStuckDegenerate` 1e-6.

**SaveRoundTrip exceptions needed:** every boss-line class +0x6680 and +0x6684…; Asian Vampire, Chang, Sheriff also +0x64b8.

**Needs another owner.** `ElysiumNpcVampireBoss.cpp`: delete `Restore` (~:64-68) and `VampireBossRestore` (~:277-293). Generated: slot 126/127 declarations on `FElysiumNpc`, species 104/126/127/516/428 rows, `GetHintDelay`; regenerate the override census. Lane M: MotorTests drop MingXiao/Tzimisce/Dog yaw rows and the `LinkFacingCancels` asserts; register NavIgnoreCollision entities through `SetMoveIgnore` ahead of the move (nothing calls slot 69 today). MiscTests:275: the rat's slot 428 now expects 0x1027cf60. `ElysiumNpcBosses.inl`: delete `FPhysicsTraceEntityCall`. Bach's Precache 0x103637b0 is `Bake` (no lane owns Bach.cpp). Base-layer bodies 0x1027bc60, 0x1027c160, 0x1027bf50, 0x1027bb20 are in files lane P does not own. `targets-P.tsv`: 65 rows.
