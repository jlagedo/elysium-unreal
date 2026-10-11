# Next bounded packet candidate — P1 velocity contract

Prepared by the planning review, 2026-10-11. **Not dispatched.** The owner has authorized resume
under `goal.md`; use after main accepts the plan and reconciles its sound packet at `8f8aea1b`.
Check current source and
receipts before using this candidate; do not redo it if main has already completed the contract.

## Concrete gap and observer

At the reviewed source snapshot, `FElysiumEntity::GetVelocity` in
`Source/ElysiumUE/Private/Substrate/ElysiumEntitySlots.cpp` only fires the slot diagnostic and
writes neither optional output. The original D02 story identifies retail `0x100ad1a0`: read
linear/angular velocity from a real physics object, otherwise use the entity-field fallback.

This is a gameplay velocity query, not engine storage. Existing port consumers include the
owner velocity query in `ElysiumNpcBaseStartTask.cpp` (retail callsite `0x10282c65`) and the
self query in `ElysiumNpcRunAi.cpp` (`0x103c0626`). These name the semantic observer, not proof
that all their receiver types use the base body: confirm base-versus-animating dispatch before
selecting the witness. The L1 animating override and L4 task logic stay outside this packet.

`uv run elysium research where 0x100ad1a0` found no hand-written implementation citation at the
planning snapshot. Its index points to `secondary_motion.md`, but `research section` found no
matching section and the file did not contain this address. Do not call that a confirmed walk.
Reuse the object/lifetime walk; recover only this getter's exact outputs, fallback, units/frame
and caller contract if no confirmed body walk exists.

## Scope and production inputs

- Source brief: the getter portion of `L0.physics.physics-object-contract-2`; D02 P1 supplies
  velocity service, while P2 retains that brief's filtering/interface closure.
- Use actual body identity and the supported native physics readback service. Do not create a
  second body registry or expose an IVP layout/vtable to satisfy the getter.
- Prove the required read phase and Source/Unreal unit conversion using the existing adapter.
  If an actual producer is missing, name it and repair this packet's prerequisite; do not return
  a fabricated zero or fold target pose/velocity into solved body state.
- Expected files: the base slot implementation, existing
  `Private/Substrate/ElysiumModelPhysics.{h,cpp}` only if a semantic query is missing, and
  focused tests/arena instrumentation. No sound, solver, geometry, higher-layer gameplay or
  broad save/import changes.

## Acceptance

1. A production-created body with deliberately distinct logical and native motion reports the
   authoritative linear and angular values through the real base-entity slot.
2. Linear-only, angular-only and both-output calls preserve optional-output behavior. Recover
   the null-output arm before specifying an assertion; do not infer an unsafe dereference.
3. A nonphysical entity uses the actual retail fallback fields; clearing/replacing/retiring a
   body switches source correctly and never reads a stale native object.
4. Units, angular convention and read phase are explicit. The witness observes the gameplay
   query output, not simply that a Chaos method was called.
5. Register a bounded executable velocity record. The original broad
   `l0_physics_object_contract` scenario was not found under `Arena/scenarios` at review time;
   it still needs later complete group/impulse/swap/interface coverage and must not be marked
   passing or complete by this getter packet.

Existing affected tests include `Elysium.Substrate.ModelPhysics.`. Add only meaningful cases
that distinguish native/fallback/lifetime behavior. Run related mover coverage if its adapter
path changes. Worker runs build and focused records; coordinator independently reviews and
runs build/default/affected arena gates, then makes an exact-path local commit and checkpoint.
No push. Report the P1 contract closed and the remaining P1/P2/P3 requirements, not r045 or L0 done.

## Boundary after this packet

Return to the P1 ownership/lifetime closing list. `GetCollideable` was also still a null-returning
slot at review time, but it belongs to the separate P2 query/interface contract. Do not append
all of P2, ragdoll handoff, impact audio or constraints to this velocity run.
