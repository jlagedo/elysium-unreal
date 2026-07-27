#include "Debug/ElysiumMoveCourses.h"

#if !UE_BUILD_SHIPPING

#include "ElysiumMoveSolve.h"

namespace ElysiumMoveCourses
{

namespace
{
	using EB = EElysiumButton;

	constexpr float U = ElysiumMove::U;

	// The tutorial's own opening area. See the header: these are placeholders, deliberately marked.
	const FVector TutorialStart(0.0f, 0.0f, 0.0f);

	FCourse MakeFlat()
	{
		FCourse C{ TEXT("flat"), TEXT("sp_tutorial_1"), TutorialStart, 0.0f, {} };
		// Run, then release and let friction stop the body — the stop distance is what this pins,
		// including the `sv_stopspeed` knee that makes the halt crisp rather than asymptotic.
		C.Segments.Add({ 3.0f, FVector2D(1.0f, 0.0f), 0, 0.0f });
		C.Segments.Add({ 2.0f, FVector2D::ZeroVector, 0, NAN });
		return C;
	}

	FCourse MakeWalkRun()
	{
		FCourse C{ TEXT("walkrun"), TEXT("sp_tutorial_1"), TutorialStart, 0.0f, {} };
		// `+speed` selects the SLOW gait: the run is the default and holding the key walks. A gait
		// regression shows up here as the two halves swapping speeds.
		C.Segments.Add({ 2.0f, FVector2D(1.0f, 0.0f), 0, 0.0f });
		C.Segments.Add({ 2.0f, FVector2D(1.0f, 0.0f), static_cast<uint64>(EB::Speed), NAN });
		return C;
	}

	FCourse MakeJump()
	{
		FCourse C{ TEXT("jump"), TEXT("sp_tutorial_1"), TutorialStart, 0.0f, {} };
		// A standing jump, then a running one. The apex is the gravity-split assertion in a built
		// world, and the held-jump segment is what proves the OldButtons latch does not pogo.
		C.Segments.Add({ 0.5f, FVector2D::ZeroVector, 0, 0.0f });
		C.Segments.Add({ 1.5f, FVector2D::ZeroVector, static_cast<uint64>(EB::Jump), NAN });
		C.Segments.Add({ 0.5f, FVector2D::ZeroVector, 0, NAN });
		C.Segments.Add({ 2.0f, FVector2D(1.0f, 0.0f), static_cast<uint64>(EB::Jump), NAN });
		return C;
	}

	FCourse MakeStrafe()
	{
		FCourse C{ TEXT("strafe"), TEXT("sp_tutorial_1"), TutorialStart, 0.0f, {} };
		// The air-strafe pattern: jump, then hold forward+right. This is the headline frame-rate
		// measurement — `AirAccelerate`'s cap stops binding above ~117 fps, so the same intent at
		// 60 / 120 / 240 diverges under the faithful variable step and agrees under a fixed one.
		C.Segments.Add({ 0.3f, FVector2D(1.0f, 0.0f), 0, 0.0f });
		C.Segments.Add({ 2.5f, FVector2D(1.0f, 1.0f), static_cast<uint64>(EB::Jump), NAN });
		return C;
	}

	FCourse MakeDuck()
	{
		FCourse C{ TEXT("duck"), TEXT("sp_tutorial_1"), TutorialStart, 0.0f, {} };
		// Duck in place, walk ducked, release. The hull swap, the eye drop to 30u, the 0.4 s duck
		// and 0.2 s unduck ramps, and the headroom gate all read off this one.
		C.Segments.Add({ 0.5f, FVector2D::ZeroVector, 0, 0.0f });
		C.Segments.Add({ 1.0f, FVector2D::ZeroVector, static_cast<uint64>(EB::Duck), NAN });
		C.Segments.Add({ 1.5f, FVector2D(1.0f, 0.0f), static_cast<uint64>(EB::Duck), NAN });
		C.Segments.Add({ 1.0f, FVector2D::ZeroVector, 0, NAN });
		return C;
	}

	// Placeholder-sited: these need surveyed coordinates before they mean anything (header).
	FCourse MakeSited(const TCHAR* Name, float Yaw)
	{
		FCourse C{ Name, TEXT("sp_tutorial_1"), TutorialStart, Yaw, {} };
		C.Segments.Add({ 4.0f, FVector2D(1.0f, 0.0f), 0, Yaw });
		return C;
	}
}

TArrayView<const FCourse> All()
{
	static const TArray<FCourse> Courses =
	{
		MakeFlat(),
		MakeWalkRun(),
		MakeJump(),
		MakeStrafe(),
		MakeDuck(),
		MakeSited(TEXT("stairs"), 0.0f),
		MakeSited(TEXT("slope"), 90.0f),
		MakeSited(TEXT("doorway"), 180.0f),
	};
	return MakeArrayView(Courses);
}

FElysiumUserCmdStream Expand(const FCourse& Course, float StepSeconds)
{
	FElysiumUserCmdStream Stream;
	if (StepSeconds <= 0.0f)
	{
		return Stream;
	}

	int32 Seq = 0;
	for (const FSegment& Seg : Course.Segments)
	{
		const int32 Frames = FMath::Max(1, FMath::RoundToInt32(Seg.Seconds / StepSeconds));
		for (int32 i = 0; i < Frames; ++i)
		{
			FElysiumUserCmd Cmd;
			Cmd.Seq = Seq++;
			Cmd.DeltaSeconds = StepSeconds;
			Cmd.Move = Seg.Move;
			Cmd.Buttons = Seg.Buttons;
			Stream.Record(Cmd);
		}
	}
	return Stream;
}

} // namespace ElysiumMoveCourses

#endif // !UE_BUILD_SHIPPING
