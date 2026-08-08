#pragma once

#include "CoreMinimal.h"
#include "ElysiumGymSpec.h"
#include "ElysiumUserCmd.h"

#if !UE_BUILD_SHIPPING

// The movement harness's fixed courses (`-ElysiumMove`), the direct analogue of
// `ElysiumVantages.h` for the shot/profile runs: a run is reproducible and a regression is a diff
// rather than a recollection.
//
// A course is authored as **segments** — hold this intent for this long — and expanded into one
// `FElysiumUserCmd` per frame at run time, because the harness forces a fixed frame rate. That is
// what lets the same course run at 60, 120 and 240 fps and be compared: the *intent* is identical,
// only the integration differs, which is exactly the frame-rate dependence 4.7 had to settle.
//
// There are two hosts and they answer different questions. A **sited** course runs on a real map
// and is the only thing a retail capture could ever be compared against. A **gym** course runs on
// the generated gym, where a riser, roof, ramp or aperture brackets the constant it tests — which
// no real map does, because no real map contains a 17-unit step beside an 18 and a 19.
namespace ElysiumMoveCourses
{
	enum class EHost : uint8
	{
		SitedMap,
		GymStage,
	};

	struct FSegment
	{
		// Zero means exactly one frame, which is how a course says "the frame after the jump" —
		// the airborne duck the crouch-jump needs cannot be expressed in whole tenths of a second.
		float Seconds = 1.0f;
		// Source's forward/side, -1..1. X forward, Y right.
		FVector2D Move = FVector2D::ZeroVector;
		// Held buttons for the whole segment.
		uint64 Buttons = 0;
		// Yaw the view is snapped to at the start of the segment, degrees. NaN = leave it alone.
		float Yaw = NAN;
	};

	struct FCourse
	{
		FName Name;
		EHost Host = EHost::SitedMap;

		// SitedMap only.
		const TCHAR* Map = nullptr;
		// **Feet-anchored**, world cm — the pawn's own origin is its box centre, and
		// `ElysiumGym::SeatOrigin` is the one place the two conventions meet.
		FVector StartFeet = FVector::ZeroVector;

		// GymStage only: the lane whose start and heading this course reads. Resolved at run time
		// rather than copied here, because the gym is derived from tuning that is read live.
		FName GymLane;

		float StartYaw = 0.0f;
		TArray<FSegment> Segments;

		// `rules.txt` jump values this course runs under. The pop brackets need `BaseJumpVelocity`
		// at zero: `sv_jump_boost` is an instant origin displacement, and with the shipped 185 u/s
		// held push also in play the body clears every roof that could bracket the pop, so the
		// bracket would say nothing at all.
		TArray<TPair<const TCHAR*, float>> JumpOverrides;

		// This course's recording is written but not compared. Two reasons reach it: what it
		// measures moves when the speed authority moves, so promoting it before `CCC7` bakes in a
		// number that is about to change; or the course does not yet measure what it is named for.
		// Either way a run records it and no baseline claims it means anything.
		bool bDeferBaseline = false;
	};

	// The courses that need a real map. The five open-floor courses stand on a surveyed stretch of
	// the warehouse and are confirmed against a headless run; `stairs`/`slope`/`doorway` carry
	// surveyed coordinates that the harness cannot yet stand on, and are deferred rather than
	// baselined (see the sites in the .cpp).
	TArrayView<const FCourse> Sited();

	// One course per gym lane, built from the lane's own family — so adding a bracket is a spec
	// edit and never a second hand-written course.
	TArray<FCourse> Gym(const ElysiumGym::FSpec& Spec);

	// Expand a course into one command per frame at the given fixed step.
	FElysiumUserCmdStream Expand(const FCourse& Course, float StepSeconds);
}

#endif // !UE_BUILD_SHIPPING
