#pragma once

#include "CoreMinimal.h"
#include "ElysiumUserCmd.h"

#if !UE_BUILD_SHIPPING

// The movement harness's fixed courses (`-ElysiumMove`), the direct analogue of
// `ElysiumVantages.h` for the shot/profile runs: a static table, one row per course, so a run is
// reproducible and a regression is a diff rather than a recollection.
//
// A course is authored as **segments** — hold this intent for this long — and expanded into one
// `FElysiumUserCmd` per frame at run time, because the harness forces a fixed frame rate. That is
// what lets the same course run at 60, 120 and 240 fps and be compared: the *intent* is identical,
// only the integration differs, which is exactly the frame-rate dependence 4.7 had to settle.
namespace ElysiumMoveCourses
{
	struct FSegment
	{
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
		const TCHAR* Name;
		const TCHAR* Map;
		// Where the body is seated before the first segment, world cm, and the yaw it faces.
		FVector Start;
		float StartYaw;
		TArray<FSegment> Segments;
	};

	// Every course runs on `sp_tutorial_1` — it is the map the whole project already exports, bakes
	// and plays, and it carries the geometry the port actually needs proving against.
	//
	// **The start positions are placeholders.** They are the map's own player start plus an offset,
	// not surveyed vantages: picking real stair/slope/doorway coordinates needs a session with
	// `elysium.campos`, the same way `ElysiumVantages.h`'s were gathered. Until then `flat`, `jump`,
	// `strafe` and `duck` are meaningful anywhere with floor, and `stairs`/`slope`/`doorway` will
	// report whatever is at the placeholder — so they are marked and must not be baselined yet.
	TArrayView<const FCourse> All();

	// Expand a course into one command per frame at the given fixed step.
	FElysiumUserCmdStream Expand(const FCourse& Course, float StepSeconds);
}

#endif // !UE_BUILD_SHIPPING
