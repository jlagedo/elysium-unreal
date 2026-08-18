#pragma once

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

class FElysiumChannelRecorder;
struct FElysiumAnimationSelection;
struct FElysiumLocomotionSample;

// The recorded body trace: one channel schema and one writer, for every harness that records a
// moving body (CCC1/CCC4).
//
// **Two producers, one schema.** `FElysiumLocomotionSample` is what a moving body publishes about
// itself and `FElysiumAnimationSelection` is what its driver resolved from it; this turns that pair
// into channel rows. The player's harness declares these columns plus the ones only it can measure —
// a replayed command's sequence number, the standing hull's headroom, the ground's friction scale,
// the camera's solve — and the cast's harness declares exactly these and nothing else. Two harnesses
// that each wrote their own `act_*` rows would leave the claim that the two producers are one system
// untested; one writer over one pair of records means the day the player's trace needs a column the
// cast's does not is the day it has to be stated here.
//
// The same argument `ElysiumCogLocomotionRow.h` makes for the live panel, made for the file on disk.
//
// Nothing here reaches into a mover, a controller or an entity: every value comes off the two
// published records, so a recording cannot disagree with what actually ran.
namespace ElysiumLocomotionTrace
{
	// The columns, in order. Every one resolves in `ElysiumChannels::Defs()` or the recorder refuses
	// to open — which is what stops a value reaching disk with nothing that knows how to compare it.
	TArrayView<const TCHAR* const> Channels();

	// One row, into a frame the caller has already opened. The caller owns `BeginFrame`/`EndFrame`
	// because a producer with columns of its own writes them into the same frame.
	//
	// The origin and the world velocity are the caller's: a sample is body-relative by design and
	// carries neither. They are emitted in **Source units**, like every other position and velocity
	// in this registry, so a row reads directly against `docs/vtmb/source_movement.md`.
	void Frame(FElysiumChannelRecorder& Recorder, float DeltaSeconds, const FVector& OriginCm,
		const FVector& VelocityCmPerSecond, const FElysiumLocomotionSample& Sample,
		const FElysiumAnimationSelection& Selection);

	// What a course accumulates over its frames, so both producers count the run channels and the
	// string identities the same way rather than each keeping its own tally.
	struct FTotals
	{
		int32 ResolvedFrames = 0;
		int32 FallbackFrames = 0;
		// Which activity codes the course reached, as a bitmask over `EElysiumAnimActivityCode`.
		uint32 CodesSeen = 0;
		// Fastest horizontal speed the course reached, Source units/s.
		double PeakSpeed2D = 0.0;
		// The body the selections were resolved for, and the banks they came out of.
		FString Stem;
		TSet<FString> Banks;
		TSet<FString> Selections;

		void Observe(const FElysiumLocomotionSample& Sample,
			const FElysiumAnimationSelection& Selection);

		// `peak_speed2d`, `act_resolved`, `act_fallbacks` and `act_codes`, plus the identities as run
		// **metadata**: there is no string channel by design, because a value on disk with no
		// comparison rule is what the registry refuses. The Content tier is what asserts an identity
		// against the real corpus.
		void Write(FElysiumChannelRecorder& Recorder) const;

		void Reset();
	};
}

#endif // !UE_BUILD_SHIPPING
