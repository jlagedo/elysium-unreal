# Report E · traces, EntityChain engine rows, filter arms

(Saved by the orchestrator from the lane's final answer. Nothing compiled or run by the lane. Corpus server down; addresses from docs and code comments.)

**Removed (body and tests).**
- `ElysiumEntitySlotBodies.cpp`, 15 bodies: `Replication` Slot88, Slot89, GetNetworkable; `TraceRetail` Physics_TraceEntity; `Chaos` VPhysicsUpdate (+ VPhysicsUpdatePusher, PhysicsObjectPosition), VPhysicsDestroyObject; `CMC` CanStandOn(edict), GetGroundVelocityToApply; MakeTracer; dead residue Slot38, ReflectGauss, DebugGetClassName.
- `ElysiumNpcClosure.cpp`: GetServerClass → `Replication`; GetDataDescMap → `FElysiumSaveArchive`; 5 empty banners.
- `ElysiumCombatCharacterSlotBodies.cpp`: GetPredDescMap → `Replication` (the client-prediction descriptor, not the save walk).
- `ElysiumNpcSensesBodies.cpp`: ShouldTransmit → `Replication`.
- Tests: the closure table 35 → 28 rows; Refusals, NetworkChangeState, VPhysicsUpdate tests gone.

**Kept as one-line forwards (`hand:`).** No own math left. IsMarkedForDeletion is `return IsDead()`. SetPoseParameter(index) loses its counter. CanStandOn keeps retail's "null is standable"; its always-false `RetailIsStandable` seam is replaced by the candidate's own slot 164 (`+0x290`); both answer false today because GetSolid is a generated stub (its stub now fires in the ledger).

**Blocker cell.** The write is in `CBaseEntity::FVisible 0x100a6fa0` at `100a71ab`: `*ppBlocker = tr.m_pEnt`, the THIRD argument (the brief said fourth). The blocker reaches `LastFVisibleBlockerTarget` through `Visible`'s out-blocker; a static-world block writes Invalid. Test `Elysium.Substrate.NpcKernelSenses10.FVisibleBlockerCell`.

**Keyfield grep.** All 108 `exports_v2/_sidecars/*/*.ents` (needs `rg -a`). Both authored: `blocks_traces=1` on 21 `prop_dynamic` rows (ch_temple_1, sm_beachhouse_1, sm_hub_1, sm_pier_1); `npc_transparent=1` on 8,096 rows. Neither arm carried for props: a prop hit names no entity and `FElysiumRetailTrace` carries no filter kind (recorded in `ElysiumRetailMaskRecipe.h`). → orchestrator: a header addition after the wave-3 gate (filter kind on the trace request) and a no-`PropMaskBit` choice at prop spawn.

**Arms carried.** Solid flag `0x20` (`101d30b6`) and render mode without WINDOW (`101d3112`) per brush-entity hit in `AElysiumMapActor::TraceRetail`; a refused entity is ignored and the line re-traced, capped at 8 passes. Tests `Elysium.Map.TraceFilter.*` in `Tests/ElysiumMapActorTraceFilterTests.cpp` (scripted world).

**Re-verdict.** `0x101a6640` GetLocalTaskId: rule code at `0x1028878a`, `0x102aad03`, `0x10375f7f` reads its value → `rule`.

**Needs another owner.** `ElysiumNpcBaseEntityChain.cpp:101` delete Slot579 (`0x101a6ce0`); `LifecycleTests.cpp:109-110` (GetNetworkable) and `MotorTests.cpp:539-545` (GetGroundVelocityToApply) delete; `Public/ElysiumEntitySlotBodies.inl` stale `RetailIsStandable`, `PhysicsObjectPosition`, `VPhysicsUpdatePusher`, `PhysicsUpdateCalls` declarations and dead `ClosureRefusals` fields; ShapeMap `+0x01b0` (`m_NetworkChangeState`) now unwritten; `docs/vtmb/npc-ai/senses.md:1156` stale.

**Untouched, not mine:** `0x10271900`, `0x10271b10`, `0x102eff40`, `0x10273e40`, `0x1039ebd0`, `0x1026c120` (PerformMovement's seam counters read by other lanes' tests). `targets-E.tsv`: 32 rows.
