// `CAI_BaseNPC`'s declarations of the `Closure` family (story 5 step 5),
// moved from `ElysiumNpcClosure*.inl`. Included inside `class FElysiumNpcBase`
// (`Substrate/ElysiumNpcBase.h`); the definitions are in `ElysiumNpcBaseClosure.cpp`.

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

/** Session-only, never saved; see `FClosureRefusals`. */
FClosureRefusals ClosureRefusals;

/** The clock of the gather pass that is dispatching slot 561 (`ElysiumNpcEnemy::GatherConditions`
 *  sets it around the virtual call and clears it after), so the base body reads the pass's `Now`
 *  rather than the world's. -1 outside a pass. A port mechanism, not a retail word: retail's body
 *  reads `gpGlobals->curtime`, which the pass's `Now` is. */
double GatherPassNow = -1.0;

