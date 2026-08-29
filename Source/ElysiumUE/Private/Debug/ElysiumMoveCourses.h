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
// only the integration differs, which is exactly the frame-rate dependence the comparison has to settle.
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

	// A body event a course can time a press against. The ground state's two edges, which is
	// everything the leniency brackets need: one names the moment the feet leave the lip, the other
	// the moment they find the floor again.
	enum class EBodyEvent : uint8
	{
		GroundLost,
		GroundGained,
	};

	// One jump press, placed relative to a body event rather than to the clock.
	//
	// This is the only thing in the course table that cannot be written down in advance: the time
	// it takes to walk to a lip moves with the gait, so a press timed off the clock silently
	// drifts the day the speed authority changes. The *offset from the event* does not move, which
	// is what makes a leniency bracket comparable. The harness resolves the event frame in a probe
	// pass and hands it to `Expand`, which stays pure.
	struct FEventJump
	{
		EBodyEvent Event = EBodyEvent::GroundLost;
		// Frames from the event; negative is before it. The press is exactly **one frame** — a held
		// jump would measure the auto-refire that `OldButtons` already produces on landing (the latch
		// is only cleared by a release), which is not leniency.
		int32 FrameOffset = 0;
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

		// Set on the leniency lanes only. When present, the harness runs the course twice: once to
		// find the event frame, once with the press placed against it.
		TOptional<FEventJump> EventJump;

		// This course's recording is written but not compared. Two reasons reach it: what it
		// measures moves when the speed authority moves, so a committed baseline would bake in a
		// number the gait changes; or the course does not yet measure what it is named for.
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
	//
	// `ResolvedEventFrame` is the frame a course's `EventJump` was measured against, or `INDEX_NONE`
	// for "no press" — which is exactly what the probe pass wants, so one code path serves both and
	// the two streams are guaranteed the same length. Taking it as an argument is what keeps this
	// pure: resolving the event is an engine measurement, and the stream stays a function of the
	// course, the step, and that one number.
	FElysiumUserCmdStream Expand(const FCourse& Course, float StepSeconds,
		int32 ResolvedEventFrame = INDEX_NONE);
}

#endif // !UE_BUILD_SHIPPING
