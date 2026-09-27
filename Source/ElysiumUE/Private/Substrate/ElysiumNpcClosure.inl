// Story 29c-1, family **Closure** — the declarations this family needs beyond the generated ones.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. The definitions are in `Substrate/ElysiumNpcClosure.cpp` and
// the tests in `Tests/ElysiumNpcKernelClosureTests.cpp`.
//
// **This family is not the story's `rule` half.** Its 41 rows are the layer 0–9 Troika-line slots
// whose 29c verdict is `present` (the port already runs the body somewhere else) or `mechanism`
// (Unreal, or a service this substrate stands, supplies it). They were generated STUBS, so the slot
// the kernel dispatches through answered a tally instead of the port's own answer. Every one of
// them is now defined in the `.cpp` and each definition does exactly one of three things:
//
//   * **forwards** to the port function 29c named, citing the retail address and that function;
//   * goes **through the port service** the row names (`ElysiumCameraShots::SurroundingBounds`,
//     `ElysiumActionTables`, `ElysiumSchedule`, `ElysiumNpcCond`, `ElysiumNpcEnemy`, …);
//   * **refuses**, where the row's mechanism is Source engine plumbing this substrate does not
//     have (the datamap/server-class RTTI descriptors, the physics trace, the temp-entity tracer,
//     the VPhysics object, the model's looping pose-parameter table, the network change tracker).
//     A refusal answers retail's own answer for the zero state where there is one, says which
//     retail call it stands for, and is COUNTED on `ClosureRefusals` below so a test can assert the
//     seam was asked rather than only that nothing crashed.
//
// So there is almost nothing to declare here: two members, and both exist for a slot whose retail
// ABI the port cannot reproduce any other way.

// --- Slot 215 `const Vector& WorldSpaceCenter() const` — `0x100b4c30` ----------------------------

// --- The refusal record -------------------------------------------------------------------------

