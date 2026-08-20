// Content-free Substrate automation: the melee swing's contact walk, as a pure rule.
//
// `Substrate/ElysiumSwingContact.h` owns the whole of the walk's arithmetic — the sub-step count,
// the intervals those sub-steps cover, which authored windows a sub-interval touches, and the
// hit-once bookkeeping that makes a multi-record swing land once and a `2COMBO`'s two disjoint
// groups land twice. Nothing here stands a world: every case is the rule and its arguments, which
// is the only way the boundary conditions can be stated exactly.
//
// The producer half — the sweep, the staged roll and the commit — is
// `Elysium.Substrate.Weapons.Melee`.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Substrate/ElysiumSwingContact.h"

namespace ElysiumSwingContactTests
{
static constexpr EAutomationTestFlags GElysiumTestFlags =
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

namespace
{
	FElysiumSwingRecord Window(float Start, float End)
	{
		FElysiumSwingRecord Record;
		Record.Start = Start;
		Record.End = End;
		Record.Bone = TEXT("Bip01 R Hand");
		Record.BCm = FVector(30.f, 0.f, 0.f);
		return Record;
	}

	FElysiumEntityHandle Handle(int32 Index)
	{
		FElysiumEntityHandle Out;
		Out.Index = Index;
		return Out;
	}
}

// --- The batch: a constant 100 Hz, floored, capped, and guarded ------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSwingSubStepTest,
	"Elysium.Substrate.SwingContact.SubSteps", GElysiumTestFlags)
bool FElysiumSwingSubStepTest::RunTest(const FString&)
{
	TestEqual(TEXT("a 60 Hz span runs one sub-step"), ElysiumSwing::SubStepCount(1.f / 60.f), 1);
	TestEqual(TEXT("a 30 Hz span runs three"), ElysiumSwing::SubStepCount(1.f / 30.f), 3);
	TestEqual(TEXT("a tenth of a second runs ten"), ElysiumSwing::SubStepCount(0.1f), 10);
	TestEqual(TEXT("exactly one sub-step runs one"),
		ElysiumSwing::SubStepCount(ElysiumSwing::SubStepSeconds), 1);

	// **A span below one sub-step is not a batch, and is not a dropped span either.** The caller's
	// contract is that it records nothing at all, so the span accumulates and the batch that does
	// run covers it — which is why a 144 Hz frame answering 0 here still lands its swing.
	// `Elysium.Substrate.Weapons.MeleeBatch` drives that half against the real walk.
	TestEqual(TEXT("a span under one sub-step is no batch"), ElysiumSwing::SubStepCount(0.009f), 0);
	TestEqual(TEXT("a single 144 Hz frame is no batch on its own"),
		ElysiumSwing::SubStepCount(1.f / 144.f), 0);
	TestEqual(TEXT("two of them are"), ElysiumSwing::SubStepCount(2.f / 144.f), 1);
	TestEqual(TEXT("a zero span is no batch"), ElysiumSwing::SubStepCount(0.f), 0);
	TestEqual(TEXT("a negative span is no batch"), ElysiumSwing::SubStepCount(-0.5f), 0);
	TestEqual(TEXT("a non-finite span is no batch rather than an unbounded loop"),
		ElysiumSwing::SubStepCount(std::numeric_limits<float>::infinity()), 0);

	// --- The cap (OURS): accumulation must not turn a hitch into hundreds of sub-steps ---------
	bool bClamped = true;
	TestEqual(TEXT("an ordinary span is walked at its own length"),
		ElysiumSwing::ClampBatchSpan(0.05f, bClamped), 0.05f);
	TestFalse(TEXT("...and is not reported as capped"), bClamped);
	TestEqual(TEXT("a span exactly at the cap is not capped"),
		ElysiumSwing::ClampBatchSpan(ElysiumSwing::MaxBatchSeconds, bClamped),
		ElysiumSwing::MaxBatchSeconds);
	TestFalse(TEXT("...and says so"), bClamped);
	TestEqual(TEXT("a three-second hitch is walked at the cap"),
		ElysiumSwing::ClampBatchSpan(3.0f, bClamped), ElysiumSwing::MaxBatchSeconds);
	TestTrue(TEXT("...and reports that it was"), bClamped);
	TestEqual(TEXT("which is what bounds the sub-steps one batch can ever run"),
		ElysiumSwing::SubStepCount(ElysiumSwing::ClampBatchSpan(3.0f, bClamped)), 25);

	// --- The travel guard (OURS): an engine discontinuity is not motion ------------------------
	TestFalse(TEXT("a limb moving a plausible distance is continuous"),
		ElysiumSwing::ExceedsBatchTravel(FVector::ZeroVector, FVector(60.0, 0.0, 0.0)));
	TestFalse(TEXT("...right up to the stated limit"),
		ElysiumSwing::ExceedsBatchTravel(FVector::ZeroVector,
			FVector(ElysiumSwing::MaxBatchTravelCm - 1.0f, 0.0, 0.0)));
	TestTrue(TEXT("a room-crossing jump is not"),
		ElysiumSwing::ExceedsBatchTravel(FVector::ZeroVector, FVector(5000.0, 0.0, 0.0)));
	TestTrue(TEXT("and a coordinate that is not a number is the discontinuity itself"),
		ElysiumSwing::ExceedsBatchTravel(FVector::ZeroVector,
			FVector(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0)));
	return true;
}

// --- The intervals: contiguous, covering, clamped at 1.0 -------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSwingIntervalTest,
	"Elysium.Substrate.SwingContact.Intervals", GElysiumTestFlags)
bool FElysiumSwingIntervalTest::RunTest(const FString&)
{
	TArray<ElysiumSwing::FInterval> Steps;

	// Contiguity is the property that makes the walk exhaustive: no cycle the clip passed through
	// may fall between two sub-steps.
	ElysiumSwing::SubStepIntervals(0.20f, 0.50f, 3, Steps);
	if (TestEqual(TEXT("three sub-steps yield three intervals"), Steps.Num(), 3))
	{
		TestEqual(TEXT("the first starts where the batch started"), Steps[0].Start, 0.20f, 1e-5f);
		TestEqual(TEXT("the last ends where the batch ended"), Steps[2].End, 0.50f, 1e-5f);
		for (int32 Index = 1; Index < Steps.Num(); ++Index)
		{
			TestEqual(TEXT("each interval starts where the previous ended"),
				Steps[Index].Start, Steps[Index - 1].End, 1e-5f);
		}
	}

	// The clamp at 1.0. A finished one-shot reports exactly 1 and there is nowhere past it to walk.
	ElysiumSwing::SubStepIntervals(0.90f, 1.40f, 2, Steps);
	if (TestEqual(TEXT("an overrun still yields its sub-steps"), Steps.Num(), 2))
	{
		TestEqual(TEXT("...clamped at the end of the clip"), Steps[1].End, 1.0f, 1e-5f);
	}
	ElysiumSwing::SubStepIntervals(-0.30f, 0.20f, 1, Steps);
	if (TestEqual(TEXT("an underrun yields its sub-step"), Steps.Num(), 1))
	{
		TestEqual(TEXT("...clamped at the start of the clip"), Steps[0].Start, 0.0f, 1e-5f);
	}

	// A count below one is the span that never became a batch: no interval, so nothing is tested.
	ElysiumSwing::SubStepIntervals(0.20f, 0.50f, 0, Steps);
	TestTrue(TEXT("no sub-steps means no intervals at all"), Steps.IsEmpty());

	// A cycle that did not advance covers the instant it stands on rather than nothing: that is what
	// the walk's own first batch looks like.
	ElysiumSwing::SubStepIntervals(0.40f, 0.40f, 2, Steps);
	if (TestEqual(TEXT("a stationary cycle still yields its sub-steps"), Steps.Num(), 2))
	{
		TestEqual(TEXT("...all at the instant it stands on"), Steps[1].End, 0.40f, 1e-5f);
	}

	// A backwards cycle is not walked backwards. Nothing proves the clip passed through the span, so
	// only the instant is tested.
	ElysiumSwing::SubStepIntervals(0.60f, 0.20f, 2, Steps);
	if (TestEqual(TEXT("a wrapped cycle still yields its sub-steps"), Steps.Num(), 2))
	{
		TestEqual(TEXT("...anchored at where the walk was, not where it wrapped to"),
			Steps[0].Start, 0.60f, 1e-5f);
		TestEqual(TEXT("...and never running backwards"), Steps[1].End, 0.60f, 1e-5f);
	}
	return true;
}

// --- The overlap test, including what it does with an authored backwards window --------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSwingOverlapTest,
	"Elysium.Substrate.SwingContact.Overlap", GElysiumTestFlags)
bool FElysiumSwingOverlapTest::RunTest(const FString&)
{
	// The closed-interval test: touching at an endpoint IS an overlap, because a window that opens
	// exactly where a sub-step ends is a window the swing reached.
	TestTrue(TEXT("a sub-interval inside the window overlaps"),
		ElysiumSwing::WindowOverlaps(0.20f, 0.60f, 0.30f, 0.40f));
	TestTrue(TEXT("a sub-interval containing the window overlaps"),
		ElysiumSwing::WindowOverlaps(0.30f, 0.40f, 0.20f, 0.60f));
	TestTrue(TEXT("touching the window's start overlaps"),
		ElysiumSwing::WindowOverlaps(0.30f, 0.60f, 0.10f, 0.30f));
	TestTrue(TEXT("touching the window's end overlaps"),
		ElysiumSwing::WindowOverlaps(0.30f, 0.60f, 0.60f, 0.90f));
	TestFalse(TEXT("a sub-interval wholly before the window does not"),
		ElysiumSwing::WindowOverlaps(0.30f, 0.60f, 0.10f, 0.29f));
	TestFalse(TEXT("a sub-interval wholly after it does not"),
		ElysiumSwing::WindowOverlaps(0.30f, 0.60f, 0.61f, 0.90f));

	// **The degenerate window, read exactly as the file states it.** `fists_attack_heavy` authors
	// `start = 0.302, end = 0.0`, and the same comparison then demands a sub-interval that STRADDLES
	// the whole backwards span. It is not repaired and it is not suppressed.
	const FElysiumSwingRecord Backwards = Window(0.302f, 0.0f);
	TestFalse(TEXT("a backwards window is closed inside its own stated span"),
		ElysiumSwing::WindowOverlaps(Backwards.Start, Backwards.End, 0.10f, 0.20f));
	TestFalse(TEXT("...and closed before it"),
		ElysiumSwing::WindowOverlaps(Backwards.Start, Backwards.End, 0.31f, 0.40f));
	TestTrue(TEXT("...but still fires on a sub-interval that straddles it"),
		ElysiumSwing::WindowOverlaps(Backwards.Start, Backwards.End, 0.0f, 0.302f));
	TestTrue(TEXT("...and on any wider straddle"),
		ElysiumSwing::WindowOverlaps(Backwards.Start, Backwards.End, -0.10f, 0.90f));

	// Record-against-record, which is what decides the spread of a landed hit.
	TestTrue(TEXT("two windows that share a span overlap"),
		ElysiumSwing::RecordsOverlap(Window(0.20f, 0.35f), Window(0.25f, 0.40f)));
	TestFalse(TEXT("two disjoint windows do not"),
		ElysiumSwing::RecordsOverlap(Window(0.20f, 0.35f), Window(0.70f, 0.85f)));
	TestTrue(TEXT("a window overlaps itself"),
		ElysiumSwing::RecordsOverlap(Window(0.20f, 0.35f), Window(0.20f, 0.35f)));
	return true;
}

// --- Hit-once: the spread across an overlapping group, and the clear on a closed window ------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FElysiumSwingHitOnceTest,
	"Elysium.Substrate.SwingContact.HitOnce", GElysiumTestFlags)
bool FElysiumSwingHitOnceTest::RunTest(const FString&)
{
	// Records 0 and 1 share a window; record 2 opens later and touches neither. This is the shape a
	// `2COMBO` has, and the whole reason it lands twice rather than three times or once.
	const TArray<FElysiumSwingRecord> Records = {
		Window(0.20f, 0.35f), Window(0.25f, 0.40f), Window(0.70f, 0.85f) };
	const FElysiumEntityHandle Victim = Handle(7);
	const FElysiumEntityHandle Other = Handle(9);

	TArray<TArray<FElysiumEntityHandle>> Hits;
	Hits.SetNum(Records.Num());
	TestFalse(TEXT("nothing is marked before a hit"), ElysiumSwing::IsMarked(Hits[0], Victim));

	// A hit through record 0 marks record 1 as well: the pair is one contact, not two.
	ElysiumSwing::MarkHit(Records, 0, Victim, Hits);
	TestTrue(TEXT("the hitting record is marked"), ElysiumSwing::IsMarked(Hits[0], Victim));
	TestTrue(TEXT("...and so is every record whose window overlaps it"),
		ElysiumSwing::IsMarked(Hits[1], Victim));
	TestFalse(TEXT("...but not one whose window does not"),
		ElysiumSwing::IsMarked(Hits[2], Victim));
	TestFalse(TEXT("and a different victim is untouched by it"),
		ElysiumSwing::IsMarked(Hits[0], Other));

	// Marking twice does not duplicate; the list is a set in behaviour.
	ElysiumSwing::MarkHit(Records, 1, Victim, Hits);
	TestEqual(TEXT("a second mark of the same victim adds nothing"), Hits[0].Num(), 1);

	// A batch covering only the first group leaves it marked and clears nothing it opened.
	ElysiumSwing::ClearClosedRecords(Records, 0.28f, 0.32f, Hits);
	TestTrue(TEXT("an open window keeps its hit list"), ElysiumSwing::IsMarked(Hits[0], Victim));

	// A batch past the first group closes both its records, which is what lets the swing land again
	// if the same victim re-enters a LATER group.
	ElysiumSwing::ClearClosedRecords(Records, 0.50f, 0.55f, Hits);
	TestFalse(TEXT("a closed window forgets whom it hit"), ElysiumSwing::IsMarked(Hits[0], Victim));
	TestFalse(TEXT("...on every record of the closed group"),
		ElysiumSwing::IsMarked(Hits[1], Victim));

	// The second group can now land on the same victim, and marking it does not re-arm the first.
	ElysiumSwing::MarkHit(Records, 2, Victim, Hits);
	TestTrue(TEXT("the disjoint group lands its own contact"),
		ElysiumSwing::IsMarked(Hits[2], Victim));
	TestFalse(TEXT("...without marking the group it does not overlap"),
		ElysiumSwing::IsMarked(Hits[0], Victim));

	// An out-of-range index and an unset handle are refusals, not writes.
	ElysiumSwing::MarkHit(Records, 99, Victim, Hits);
	ElysiumSwing::MarkHit(Records, 0, FElysiumEntityHandle::Invalid(), Hits);
	TestFalse(TEXT("neither a bad index nor an unset victim marks anything"),
		ElysiumSwing::IsMarked(Hits[0], Victim)
		|| ElysiumSwing::IsMarked(Hits[0], FElysiumEntityHandle::Invalid()));

	// A short list is grown rather than indexed past: the record count is the clip's, and the walk
	// meets a clip before it has sized anything.
	TArray<TArray<FElysiumEntityHandle>> Fresh;
	ElysiumSwing::MarkHit(Records, 1, Victim, Fresh);
	TestEqual(TEXT("an unsized hit list grows to the record count"), Fresh.Num(), Records.Num());
	TestTrue(TEXT("...and still spreads across the overlapping group"),
		ElysiumSwing::IsMarked(Fresh[0], Victim) && ElysiumSwing::IsMarked(Fresh[1], Victim));
	return true;
}

}   // namespace ElysiumSwingContactTests

#endif   // WITH_DEV_AUTOMATION_TESTS
