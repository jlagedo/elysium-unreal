#include "Debug/ElysiumLocomotionTrace.h"

#if !UE_BUILD_SHIPPING

#include "Debug/ElysiumChannelRecorder.h"
#include "ElysiumAnimationIntent.h"
#include "ElysiumLocomotionSample.h"
#include "ElysiumMoveSolve.h"

namespace ElysiumLocomotionTrace
{

namespace
{
	// The shared columns, in the order the movement harness has always written them. A producer with
	// columns of its own appends them; nothing here is reordered to make room, because a baseline is
	// paired by channel name and a column that moves is only noise in a diff.
	const TCHAR* const GTraceChannels[] =
	{
		TEXT("frame"), TEXT("dt"),
		TEXT("px"), TEXT("py"), TEXT("pz"),
		TEXT("vx"), TEXT("vy"), TEXT("vz"), TEXT("speed2d"),
		TEXT("onground"), TEXT("ducked"), TEXT("ducking"), TEXT("water"),
		TEXT("move_yaw_wish"), TEXT("move_yaw_vel"), TEXT("move_yaw"),
		TEXT("act_code"), TEXT("act_route"), TEXT("act_outcome"), TEXT("act_asset"),
		TEXT("act_state"),
		TEXT("air_phase"), TEXT("act_gen"), TEXT("act_stride"), TEXT("act_fade"),
	};

	// Sorted on the way out so two runs of the same course produce the same text.
	FString JoinedTraceIdentities(const TSet<FString>& Values)
	{
		TArray<FString> Sorted = Values.Array();
		Sorted.Sort();
		return FString::Join(Sorted, TEXT(" "));
	}
}

TArrayView<const TCHAR* const> Channels()
{
	return MakeArrayView(GTraceChannels);
}

void Frame(FElysiumChannelRecorder& Recorder, float DeltaSeconds, const FVector& OriginCm,
	const FVector& VelocityCmPerSecond, const FElysiumLocomotionSample& Sample,
	const FElysiumAnimationSelection& Selection)
{
	const double Inv = 1.0 / ElysiumMove::U;

	Recorder.Set(TEXT("frame"), Recorder.FrameCount());
	Recorder.Set(TEXT("dt"), DeltaSeconds);
	Recorder.Set(TEXT("px"), OriginCm.X * Inv);
	Recorder.Set(TEXT("py"), OriginCm.Y * Inv);
	Recorder.Set(TEXT("pz"), OriginCm.Z * Inv);
	Recorder.Set(TEXT("vx"), VelocityCmPerSecond.X * Inv);
	Recorder.Set(TEXT("vy"), VelocityCmPerSecond.Y * Inv);
	Recorder.Set(TEXT("vz"), VelocityCmPerSecond.Z * Inv);
	// From the sample rather than from the velocity above: a yaw rotation preserves length, so the
	// two agree by construction, and reading the published one is what keeps the trace a recording
	// of the record the graph was steered by.
	Recorder.Set(TEXT("speed2d"), Sample.Speed2D() * Inv);
	Recorder.Set(TEXT("onground"), Sample.bOnGround);
	// Source carries the settled hull and the transition as two independent flags and the sample
	// carries the one stance they describe. The two columns are that mapping read back, so a
	// recording keeps the original's own pair rather than inventing a fourth value for the CSV.
	Recorder.Set(TEXT("ducked"),
		Sample.Stance == EElysiumStance::Ducked || Sample.Stance == EElysiumStance::Rising);
	Recorder.Set(TEXT("ducking"),
		Sample.Stance == EElysiumStance::Lowering || Sample.Stance == EElysiumStance::Rising);
	Recorder.Set(TEXT("water"), static_cast<int32>(Sample.Water));

	Recorder.Set(TEXT("move_yaw_wish"), Sample.MoveYawWish);
	Recorder.Set(TEXT("move_yaw_vel"), Sample.MoveYawVelocity);
	// The pose parameter off the **record**, not off the sample: the slew and the hold are a rate the
	// once-a-frame driver owns, and the sample carries its unfiltered input.
	Recorder.Set(TEXT("move_yaw"), Selection.MoveYaw);

	// The classifier's own answer is the pre-translation activity, which is what the record keeps as
	// the logical request; the resolved one is only different once a translation row applied.
	Recorder.Set(TEXT("act_code"),
		static_cast<int32>(ElysiumAnimIntent::ActivityCode(Selection.RequestedActivity)));
	Recorder.Set(TEXT("act_route"), static_cast<int32>(Selection.Route));
	Recorder.Set(TEXT("act_outcome"), static_cast<int32>(Selection.Outcome));
	Recorder.Set(TEXT("act_asset"), static_cast<int32>(Selection.AssetKind));
	// The state the record named, not one this writer projected: the trace and the pose come off the
	// same field, so a run cannot record a state the graph never entered.
	Recorder.Set(TEXT("act_state"), static_cast<int32>(Selection.GraphState));
	Recorder.Set(TEXT("air_phase"), static_cast<int32>(Selection.AirPhase));
	Recorder.Set(TEXT("act_gen"), static_cast<int32>(Selection.Generation));
	Recorder.Set(TEXT("act_stride"), Selection.GroundSpeedCmPerSecond * Inv);
	Recorder.Set(TEXT("act_fade"), Selection.FadeSeconds);
}

void FTotals::Observe(const FElysiumLocomotionSample& Sample,
	const FElysiumAnimationSelection& Selection)
{
	const EElysiumAnimActivityCode Code =
		ElysiumAnimIntent::ActivityCode(Selection.RequestedActivity);
	// `1ull`, not `1u`: the enum reaches past ordinal 31 and a 32-bit shift there is undefined
	// behaviour rather than a value that merely wraps.
	static_assert(static_cast<uint32>(EElysiumAnimActivityCode::Count) <= 64,
		"EElysiumAnimActivityCode has outgrown the CodesSeen bitmask; widen it or stop using one");
	CodesSeen |= (Code != EElysiumAnimActivityCode::Unknown)
		? (1ull << static_cast<uint32>(Code)) : 0ull;

	if (Selection.Outcome == EElysiumAnimOutcome::Resolved)
	{
		++ResolvedFrames;
	}
	else
	{
		++FallbackFrames;
	}

	PeakSpeed2D = FMath::Max(PeakSpeed2D, static_cast<double>(Sample.Speed2D()) / ElysiumMove::U);

	if (!Selection.Stem.IsEmpty())
	{
		Stem = Selection.Stem;
	}
	if (!Selection.OwnerStem.IsEmpty())
	{
		// A course's manifest naming the bank it resolved through IS the bank-ownership claim, in
		// text, in the run's own output.
		Banks.Add(Selection.OwnerStem);
		// The PAIR a fan evaluates, not the floor cell alone: a manifest that named one cell would read
		// as a snap on every frame the body was between two of them, which is most of them. A label
		// that names one animation carries no second half and reads exactly as it did.
		//
		// The FRACTION is deliberately not in the identity — this is a SET of the identities a course
		// visited, and a continuous weight would make every frame its own entry.
		Selections.Add(Selection.NextAnimationName.IsEmpty()
			? FString::Printf(TEXT("%s=%s@%s:%s"), *Selection.ResolvedActivity,
				*Selection.SequenceLabel, *Selection.OwnerStem, *Selection.AnimationName)
			: FString::Printf(TEXT("%s=%s@%s:%s+%s"), *Selection.ResolvedActivity,
				*Selection.SequenceLabel, *Selection.OwnerStem, *Selection.AnimationName,
				*Selection.NextAnimationName));
	}
}

void FTotals::Write(FElysiumChannelRecorder& Recorder) const
{
	Recorder.SetRun(TEXT("peak_speed2d"), PeakSpeed2D);
	Recorder.SetRun(TEXT("act_resolved"), ResolvedFrames);
	Recorder.SetRun(TEXT("act_fallbacks"), FallbackFrames);
	// **One channel, as a double, and it is exact.** The channel's own storage is a double, which
	// represents every integer below 2^53 without loss — and the mask needs one bit per activity
	// code, which `Count` bounds well under that. So the whole set crosses as a single number the
	// comparator can diff.
	//
	// The `int32` this used to be is what would NOT work: bit 31 is a real code now, so the masked
	// value exceeds `INT32_MAX` and would land in the manifest negative.
	static_assert(static_cast<uint32>(EElysiumAnimActivityCode::Count) <= 53,
		"the activity-code mask no longer fits a double exactly; it can no longer ride one channel");
	Recorder.SetRun(TEXT("act_codes"), static_cast<double>(CodesSeen));

	Recorder.SetMeta(TEXT("anim_stem"), Stem);
	Recorder.SetMeta(TEXT("anim_banks"), JoinedTraceIdentities(Banks));
	Recorder.SetMeta(TEXT("anim_selections"), JoinedTraceIdentities(Selections));
}

void FTotals::Reset()
{
	*this = FTotals();
}

} // namespace ElysiumLocomotionTrace

#endif // !UE_BUILD_SHIPPING
