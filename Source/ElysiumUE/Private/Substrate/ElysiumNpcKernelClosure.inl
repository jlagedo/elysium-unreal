// Story 29c-1, family **Closure** — the declarations this family needs beyond the generated ones.
//
// Included inside `class FElysiumNpc` by `Substrate/ElysiumNpc.h`, beside the generated
// `ElysiumNpcKernelSlots.inl`. The definitions are in `Substrate/ElysiumNpcKernelClosure.cpp` and
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

/** Where slot 215's answer LIVES, so the slot can hand out an address at all.
 *
 *  `0x100b4c30` and slot 192's `0x10027160` are the SAME body compiled twice: identical arms,
 *  identical arithmetic, differing only in how the answer leaves — 192 writes through the hidden
 *  struct-return pointer, 215 returns a pointer into the image's rotating temp-vector ring
 *  (`DAT_109f0cc0`, 128 entries, index `DAT_106b856c` advanced with `& 0x7f`). The ring is retail's
 *  way of returning a `const Vector&` from a value computation; nothing in the port has one.
 *
 *  **Named modernization.** This is a ONE-DEEP PER-ENTITY cache rather than a 128-deep global ring,
 *  so the reference slot 215 hands out stays valid for exactly as long as this NPC does and is
 *  invalidated by the next slot-215 call on the SAME body instead of by the 128th call on any body.
 *  The retail aliasing that difference removes — a caller holding the reference across 128
 *  intervening `WorldSpaceCenter()` calls and silently reading someone else's centre — has no
 *  reachable instance in layers 0–9: every one of the 49 virtual call sites reads the answer
 *  before it calls anything. `mutable` because retail's slot is `const` and still writes the ring.
 *  CENTIMETRES, like every other length on this struct. */
mutable FVector WorldSpaceCentreCacheCm = FVector::ZeroVector;

// --- The refusal record -------------------------------------------------------------------------

/** What this family's `mechanism` slots were asked for and refused.
 *
 *  Seven of the 41 rows are Source engine plumbing with no counterpart in this substrate. The brief
 *  requires such a body to answer nothing and to NAME the retail call it stands for; the counters
 *  here are what lets `Elysium.Substrate.NpcKernelClosure.*` assert the second half — that the slot
 *  reached the seam and that the refusal is the recovered one — instead of asserting only that the
 *  call returned. Same posture as family Facing's `PoseParameterWrites`: **read by the test and by
 *  nothing else**, written by no rule, and never saved.
 *
 *  A counter here is NOT a stub tally. `ElysiumStub` is for a surface with no implementation; these
 *  slots have their implementation, and it is a refusal with a recovered reason. */
struct FClosureRefusals
{
	int32 PredDescMap = 0;             // slot 79  `0x10321670` -> `&datamap_CBaseCombatCharacter`
	int32 ServerClass = 0;             // slot 80  `0x102c5870` -> `&DAT_109248d4`
	int32 DataDescMap = 0;             // slot 82  `0x1028cd10` -> `&datamap_CAI_BaseNPCTroika`
	int32 ChangeTracker = 0;           // slot 88  `0x10026b50` -> `0x10146700` on `this+0x1b0`
	int32 PhysicsTraceEntity = 0;      // slot 102 `0x100ab450` -> `0x101cd110`
	int32 MakeTracer = 0;              // slot 184 `0x10267260` -> the `TRACER_LINE` temp entity
	int32 VPhysicsDestroyObject = 0;   // slot 225 `0x100b5040` -> `m_pPhysicsObject +0x36c`
	int32 LoopingPoseParameter = 0;    // slot 346 `0x1032fc50` -> the model's pose-param bounds
	int32 BlinkCadence = 0;            // slot 333 `0x102bff20` arm 1 -> `FElysiumBlinkSchedule`
	int32 EyeFidgetDriver = 0;         // slot 333 `0x102c0010` -> the saccade layer
	int32 BaseEyeMaintainer = 0;       // slot 333 tail `0x1026b810` -> `TickGaze`

	// The endpoints the last `Physics_TraceEntity` was asked for, and the last `MakeTracer` start
	// and tracer type. Kept so the refusal cases can assert the slot was reached WITH retail's own
	// arguments rather than merely reached.
	FVector TraceStartCm = FVector::ZeroVector;
	FVector TraceEndCm = FVector::ZeroVector;
	uint32 TraceMask = 0;
	FVector TracerStartCm = FVector::ZeroVector;
	int32 TracerType = 0;
};

/** Session-only, never saved; see `FClosureRefusals`. */
FClosureRefusals ClosureRefusals;
