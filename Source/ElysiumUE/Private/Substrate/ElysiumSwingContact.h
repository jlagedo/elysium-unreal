#pragma once

#include "CoreMinimal.h"

#include "ElysiumEntityHandle.h"
#include "ElysiumSwingRecord.h"

// The melee swing's contact walk, as a pure rule — the `ElysiumAnimEvents` shape beside the
// substrate's other decision leaves. No world, no clock, no engine service, no UObject: this
// frame's cycle interval and the clip's own authored records in, which records are live and which
// victims they may still land on out.
//
// The rule is retail's per-frame swept contact walk over the sequence's swing records
// (`docs/vtmb/combat-and-damage.md` -> the melee swing pipeline). Its shape:
//
//  * A swing is LIVE for as long as the playing attack clip declares at least one record. There is
//    no start event and no stop event — the records ARE the window, and a clip that declares none
//    has no contact at all.
//  * A live swing walks in BATCHES of `floor(span * 100)` sub-steps, at a constant 100 Hz. A span
//    shorter than one sub-step runs no batch: nothing is tested and nothing is recorded, so the
//    span survives to be walked by the batch that does run.
//  * The sub-steps are contiguous cycle intervals covering everything the clip advanced through
//    since the last batch, clamped at 1.0. A record is tested on a sub-step exactly when its
//    authored window overlaps that sub-interval.
//  * Hit-once is per RECORD, with a spread: a landed hit marks the victim in every record whose
//    window overlaps the hitting record's, so a multi-record swing lands once. A record's list
//    clears on any batch whose span leaves its own window closed, which is what lets a `2COMBO`'s
//    two disjoint groups land twice.
//
// **The batch is an ENVIRONMENT RECONCILIATION, and it is worth stating exactly.** Retail's update
// takes the branch below one sub-step straight into its epilogue stores — the stored timestamp,
// position and angles are overwritten and the short span is discarded. That branch is unreachable
// in retail's own environment: the clock is server `curtime`, which advances in fixed ticks, and
// the update's `dt <= 0` exit is the one path that does NOT write the timestamp (which has exactly
// one writer in the binary). So a call either sees `dt == 0` and stores nothing, or sees a whole
// tick and walks. The observable is tick-batched walking, and that is what is reproduced here:
// accumulate, and walk the whole accumulated span when it reaches a sub-step. Reproducing the
// span-dropping branch literally would make melee stop landing above 100 fps — a behaviour retail's
// clock cannot produce and therefore never had.
//
// Where the segment IS at a given sub-step is not this file's business: bone transforms and the
// sweep itself are engine services reached through the embodiment seam (K13). This owns only the
// window arithmetic and the hit bookkeeping.

namespace ElysiumSwing
{
	// The walk's fixed sub-step rate. Retail divides the span by a constant 100 Hz rather than by a
	// per-clip or per-weapon figure.
	inline constexpr float SubStepHz = 100.0f;

	// One sub-step, and therefore the span a batch has to reach before it runs at all.
	inline constexpr float SubStepSeconds = 1.0f / SubStepHz;

	// OURS, and part of the same environment reconciliation rather than a divergence from an
	// observable: the longest span one batch converts into sub-steps. Accumulation means a hitch — a
	// stalled frame, a breakpoint, a level stream — would otherwise arrive as one span worth hundreds
	// of sub-steps, and retail's fixed-tick clock could not hand its update such a `dt` in the first
	// place. What the cap bounds is the sub-step COUNT, and therefore the walk's resolution: the
	// ground a batch covers is the cycle interval, which still reaches back to the last batch
	// whatever the span was. A capped batch is a coarser walk over the same cycle, never a shorter one.
	inline constexpr float MaxBatchSeconds = 0.25f;

	// OURS, and the second half of the same guard: the furthest the attacker's root or a contact
	// segment's endpoint may travel between two batches and still be swept THROUGH. There is no
	// retail equivalent — its server never teleports a body mid-swing without resetting the swing —
	// so this exists to stop an engine discontinuity (a teleport, a travel, a pose-layer hitch)
	// being read as motion: sweeping a metres-wide patch would land the swing on every bystander on
	// the line between where the attacker was and where it now is. A batch that exceeds it re-primes
	// the cursor instead, sweeping nothing and starting again from where the limb actually is.
	//
	// The figure is far above any plausible per-batch limb distance — a fast swing carries a fist
	// tens of centimetres per batch and a sprinting body about ten — and far below a room-crossing
	// teleport, which is the discontinuity it exists to catch.
	inline constexpr float MaxBatchTravelCm = 200.0f;

	// One contiguous slice of the clip cycle the walk tests over.
	struct FInterval
	{
		float Start = 0.0f;
		float End = 0.0f;
	};

	// `floor(span * 100)`. A non-finite or non-positive span answers 0, which is the same "no batch"
	// a span below one sub-step gets — and a caller that answers 0 must record nothing, so the span
	// it was asked about survives into the next call.
	int32 SubStepCount(float SpanSeconds);

	// The span one batch walks, given everything accumulated since the last one: itself, or
	// `MaxBatchSeconds` when a hitch put more than that on the clock. `bOutClamped` reports which,
	// so the caller can say so once rather than discarding a span silently.
	float ClampBatchSpan(float AccumulatedSeconds, bool& bOutClamped);

	// Did this point move further between two batches than a limb or a body plausibly could?
	//
	// The `MaxBatchTravelCm` guard's own predicate, spelled once so the walk asks it the same way
	// about the attacker's root and about every segment endpoint. A non-finite coordinate answers
	// true: a position that is not a position is exactly the discontinuity this catches.
	bool ExceedsBatchTravel(const FVector& From, const FVector& To);

	// The `SubSteps` contiguous intervals covering `[PrevCycle, CurCycle]`, clamped into `[0,1]`.
	//
	// `Out` is RESET first. A count below 1 yields none, which is the batch that never ran. A
	// cycle that did not advance — or that wrapped, which an attack clip does not do but a caller
	// cannot promise — yields zero-length intervals at `PrevCycle` rather than a backwards walk:
	// the clip cannot be proven to have passed through anything, so nothing but the instant it
	// stands on is tested.
	void SubStepIntervals(float PrevCycle, float CurCycle, int32 SubSteps, TArray<FInterval>& Out);

	// Does the authored window `[WindowStart, WindowEnd]` overlap `[IntervalStart, IntervalEnd]`?
	//
	// The closed-interval test, applied to the pair exactly as the file states it. A DEGENERATE
	// window (`End < Start`) is not special-cased and does not have to be: the same comparison then
	// requires the sub-interval to straddle the whole backwards window, which is the only way such
	// a record can fire and is what retail's own test does with it.
	bool WindowOverlaps(float WindowStart, float WindowEnd, float IntervalStart, float IntervalEnd);

	// The same test between two records' windows — what decides which records a landed hit marks.
	bool RecordsOverlap(const FElysiumSwingRecord& A, const FElysiumSwingRecord& B);

	// Has this record already landed on this victim?
	bool IsMarked(const TArray<FElysiumEntityHandle>& Hits, const FElysiumEntityHandle& Victim);

	// Record a landed hit: mark `Victim` in `HitIndex` and in EVERY record whose window overlaps
	// `HitIndex`'s, so a swing whose records share one window lands once rather than once per
	// record. `InOutHits` is index-aligned with `Records` and is grown to fit if it is short.
	void MarkHit(const TArray<FElysiumSwingRecord>& Records, int32 HitIndex,
		const FElysiumEntityHandle& Victim, TArray<TArray<FElysiumEntityHandle>>& InOutHits);

	// Clear the hit list of every record whose window is closed over `[BatchStart, BatchEnd]` — the
	// whole slice of cycle this batch covered. A record that re-opens later therefore starts with an
	// empty list, which is what makes a `2COMBO`'s second group land again.
	void ClearClosedRecords(const TArray<FElysiumSwingRecord>& Records, float BatchStart,
		float BatchEnd, TArray<TArray<FElysiumEntityHandle>>& InOutHits);

	// --- `MeleeSwingStep 0x10343020`: the per-record hit test ------------------------------------

	// `count = min(seqdesc+0x2c4, 20)`: the step walks at most 20 records of a sequence, and zeroes
	// the hit lists of records `count..19` after the record loop.
	inline constexpr int32 MaxRecords = 20;

	// The capacity of the candidate list `0x101cca80` fills (`PUSH 100`).
	inline constexpr int32 MaxBoxEntities = 100;

	// f32 `0x10488874` = 0.1666667: one sample per 6 Source units of segment.
	inline constexpr float SamplesPerUnit = 0.1666667f;

	// `n = ceil(|B - A| * 0.1666667)` over the segment's length in SOURCE units (the endpoints are
	// world centimetres here); `n < 2` answers 1, and the caller then collapses the segment to its
	// midpoint (`A = B = (A + B) / 2`).
	int32 SampleCount(const FVector& ACm, const FVector& BCm);

	// `f = n > 1 ? 1 - i / (n - 1) : 0` (`0x10343fc4..0x10343fe0` in the wall arm, the same
	// expression in the entity arm): sample 0 is the segment's B end, the last its A end.
	float SampleFraction(int32 SampleIndex, int32 Samples);

	// The window test of the step itself: open iff `start <= cycle && end >= prevCycle`, both
	// inclusive. `WindowOverlaps` above is the same comparison; this names retail's operands.
	inline bool StepWindowOpen(const FElysiumSwingRecord& Record, float PrevCycle, float Cycle)
	{
		return WindowOverlaps(Record.Start, Record.End, PrevCycle, Cycle);
	}

	// --- The wall contact (`0x10343f96`), the attacker's slot 328 arm -----------------------------

	// f32 `0x10451ab8`: the plane must be near-vertical, `|normal.z| < 0.3`.
	inline constexpr float WallMaxNormalZ = 0.3f;
	// f32 `0x1049e038`, squared at `0x103442d8`: the flattened forward's 2-D length^2 must exceed it.
	inline constexpr float WallMinForwardLenSq = 1e-12f;
	// f32 `0x1049e03c`: `|dot(normal, flattened forward)| > 0.7071`.
	inline constexpr float WallMinFacingDot = 0.7071f;
	// f32 `0x1049e040`: 20.0 Source units.
	inline constexpr float WallMaxDistanceUnits = 20.0f;

	// The first two tests of the wall arm, which a hit must pass to count at all
	// (`0x10344221..0x1034428a`): the plane normal non-zero and `|normal.z| < 0.3`. A hit failing
	// either goes to the next sample with no reaction AND no impact effect.
	bool WallPlaneQualifies(const FVector& PlaneNormal);

	// Does a world hit of the sample ray send the attacker into its blocked reaction (slot 319)?
	// `0x10344221..0x10344382`, in the listing's order: the plane normal non-zero, `|normal.z| <
	// 0.3`, the attacker's forward (`AngleVectors` of slot 221's angles) flattened with a 2-D
	// length^2 above 1e-12 and normalised, `|dot(normal, forward)| > 0.7071`, and **the 2-D distance
	// from the attacker's origin (slot 217 `GetAbsOrigin`, `0x10344343`) to the trace's hit point
	// (`Q + (P - Q) * fraction`, `0x103441bf..0x103441ec`) below 20.0 units** (`0x1034435d..
	// 0x10344374`: `sqrt(dx*dx + dy*dy)` against `0x1049e040`). The fourth operand was read from the
	// listing 2026-10-04 (spec 0002 V11-2); the packet S5 had it as lost.
	// Everything is in world centimetres; `ForwardWorld` need not be normalised.
	bool WallBlocksSwing(const FVector& PlaneNormal, const FVector& ForwardWorld,
		const FVector& HitPointCm, const FVector& AttackerOriginCm);
}
