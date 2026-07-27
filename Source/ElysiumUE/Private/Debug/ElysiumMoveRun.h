#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

#if !UE_BUILD_SHIPPING

class UElysiumMapSubsystem;

// Headless movement-regression harness (`-ElysiumMove`), the fourth self-driving run beside
// -ElysiumProfile, -ElysiumShots and -ElysiumProbe.
//
// It exists because the movement port's load-bearing claims only exist in a built world: that a
// curb is climbed and a wall is not, that `StepMove`'s two attempts pick the right one, that the
// body re-grounds after a step down with no `StayOnGround`, that a duck fits under a span. A unit
// test asserts the arithmetic; this asserts the arithmetic *against real geometry*, and turns "the
// step still climbs" into a number with a non-zero exit code.
//
// It replays a fixed `FElysiumUserCmdStream` per course through the **real input router**, so the
// harness exercises the same path a player does, and writes one CSV per course plus a JSON summary
// under `tools/out/_move/`. `tools/move_diff.py` is the comparator.
class FElysiumMoveRun
{
public:
	static bool IsRequested();

	explicit FElysiumMoveRun(UElysiumMapSubsystem* InSubsystem);
	~FElysiumMoveRun();

private:
	bool Tick(float DeltaSeconds);

	// Seat the body at the course start and arm its command stream.
	bool BeginCourse(int32 Index);
	// One sampled row.
	void Sample();
	// Flush the current course to disk.
	void FinishCourse();

	TWeakObjectPtr<UElysiumMapSubsystem> Subsystem;
	FTSTicker::FDelegateHandle TickHandle;

	int32 FrameInPhase = 0;
	int32 CourseIndex = -1;
	bool bDone = false;

	// The fixed step the run is driven at, from -MoveHz (default 60). The harness forces this so a
	// course is reproducible and the 60/120/240 comparison is meaningful.
	float StepSeconds = 1.0f / 60.0f;
	int32 Hz = 60;

	// Only these courses run, when -MoveCourse=<name> is given.
	FString CourseFilter;

	// The rows of the course in flight.
	TArray<FString> Rows;
	// Summary values accumulated across the course.
	double PeakSpeed2D = 0.0;
	double PeakApexUnits = 0.0;
	// The height the body left the ground at, for the airborne span in flight.
	double TakeoffZ = 0.0;
	// The last sample taken while still on the ground — the takeoff datum.
	double LastGroundedZ = 0.0;
	bool bHaveDatum = false;
	int32 GroundTransitions = 0;
	bool bWasOnGround = false;
};

#endif // !UE_BUILD_SHIPPING
