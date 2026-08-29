#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Debug/ElysiumChannelRecorder.h"
#include "Debug/ElysiumLocomotionTrace.h"
#include "Debug/ElysiumMoveCourses.h"
#include "ElysiumUserCmd.h"

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
// harness exercises the same path a player does, and writes one `FElysiumChannelRecorder` run per
// course under `$ELYSIUM_EXPORT_ROOT/_move/`.
// `pipeline/src/elysium_pipeline/validation/channel_diff.py` is the comparator.
//
// It runs against one of two hosts. `-ElysiumMap=<name>` replays the sited courses on a real map,
// which is the only thing a retail capture can ever be compared against. `-MoveGym` instead builds
// the generated gym in an empty stage world, where the geometry is derived from `ElysiumMove`'s own
// constants and nothing game-derived is in the world at all — which is what makes a gym recording
// something the repository can carry.
class FElysiumMoveRun
{
public:
	static bool IsRequested();
	// True when this run wants the generated gym rather than a map.
	static bool WantsGym();

	// The columns this producer declares: the shared body trace plus what only a driven player body
	// can measure. Exposed so `Elysium.Substrate.LocomotionTrace` can hold the two producers to one
	// schema without standing a world up.
	static TArray<const TCHAR*> DeclaredChannels();

	explicit FElysiumMoveRun(UElysiumMapSubsystem* InSubsystem);
	~FElysiumMoveRun();

	// A leniency course runs twice. The **probe** pass drives the course with no press at all
	// and watches for the ground edge it is timed against; the **record** pass then replays the same
	// course with the press placed at that frame plus the lane's offset, and is the only one that
	// touches the recorder.
	//
	// Two passes rather than a live injection because `Router->StartReplay` takes a finished stream,
	// and a stream built as it runs is not the stream that can be re-run. It is sound because a
	// refused press is a **provable** no-op: `CheckJumpButton` returns on `!bOnGround` without
	// touching velocity, gravity scale, the hold window or `OldButtons`, so nothing before the press
	// can differ between the passes, and the event frame the probe measured is still the event frame.
	enum class ECoursePhase : uint8
	{
		Probe,
		Record,
	};

private:
	bool Tick(float DeltaSeconds);

	// Stand the gym up in the stage world. Once, before the first course.
	bool BuildGym();
	// Seat the body at the course start and arm its command stream.
	bool BeginCourse(int32 Index, ECoursePhase InPhase);
	// One sampled row.
	void Sample();
	// One probe frame: the ground state, and nothing else. No recorder, no output.
	void ProbeSample();
	// Flush the current course to disk.
	void FinishCourse();

	TWeakObjectPtr<UElysiumMapSubsystem> Subsystem;
	FTSTicker::FDelegateHandle TickHandle;

	int32 FrameInPhase = 0;
	int32 CourseIndex = -1;
	bool bDone = false;
	bool bGym = false;
	bool bGymBuilt = false;

	// The fixed step the run is driven at, from -MoveHz (default 60). The harness forces this so a
	// course is reproducible and the 60/120/240 comparison is meaningful.
	float StepSeconds = 1.0f / 60.0f;
	int32 Hz = 60;

	// Only these courses run, when -MoveCourse=<name> is given.
	FString CourseFilter;

	FElysiumChannelRecorder Recorder;

	// The seated body settles for a few frames before its stream is armed, so it holds it.
	FElysiumUserCmdStream PendingStream;
	int32 CourseSettleRemaining = 0;

	// Where the body started, so every run channel is a displacement rather than a world
	// coordinate — which is what lets a gym lane and a sited course be read the same way.
	FVector StartFeet = FVector::ZeroVector;
	FVector StartForward = FVector::ForwardVector;

	// The body trace's own accumulator — the activity totals, the peak speed and the
	// string identities, counted the same way the cast's harness counts them.
	ElysiumLocomotionTrace::FTotals Totals;

	// Summary values accumulated across the course.
	double PeakApexUnits = 0.0;
	double AdvanceMax = 0.0;
	double TopStand = 0.0;
	double ReachMax = 0.0;
	bool bEndedDucked = false;
	// The height the body left the ground at, for the airborne span in flight.
	double TakeoffZ = 0.0;
	// The last sample taken while still on the ground — the takeoff datum.
	double LastGroundedZ = 0.0;
	bool bHaveDatum = false;
	int32 GroundTransitions = 0;
	bool bWasOnGround = false;

	ECoursePhase Phase = ECoursePhase::Record;
	// What the probe pass measured, or INDEX_NONE for "the event never happened" — which the record
	// pass then turns into a course with no press, so the bracket's sentinel rungs disagree with
	// their baselines and the run reddens rather than quietly recording a refusal.
	int32 ResolvedEventFrame = INDEX_NONE;
	int32 ProbeFrame = 0;
	bool bProbeWasOnGround = true;
	bool bProbeSawGroundLoss = false;
	// Which ground edge the probe pass is watching for, read off the course when the pass opens.
	ElysiumMoveCourses::EBodyEvent ProbeEvent = ElysiumMoveCourses::EBodyEvent::GroundLost;
	// Press-edge jumps the record pass's body took. The leniency bracket's whole answer.
	int32 JumpsTaken = 0;
	// The highest third-person weight the course reached. It saturates at 1 on every course, which
	// is the point: it is the cheap catch for a camera that never engaged, and the only camera
	// channel a committed gym baseline can carry.
	double CamThirdMax = 0.0;
};

#endif // !UE_BUILD_SHIPPING
