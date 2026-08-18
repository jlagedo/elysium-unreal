#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Debug/ElysiumArenaSpec.h"
#include "Debug/ElysiumChannelRecorder.h"
#include "Debug/ElysiumLocomotionTrace.h"
#include "ElysiumEntityHandle.h"

#if !UE_BUILD_SHIPPING

class AElysiumNpcBody;
class FElysiumEntityWorld;
class UElysiumMapSubsystem;
class UWorld;

// Headless cast-locomotion harness (`-ElysiumCast`), the fifth self-driving run and the **second
// producer of the body trace**.
//
// `FElysiumMoveRun` records the player's selection; this records the cast's, through the same
// `ElysiumLocomotionTrace` writer and the same channel registry, so the claim that the two are one
// system is a diff rather than an assertion. It declares exactly the shared columns and nothing of
// its own: a cast body has no command stream, no headroom test and no camera.
//
// It runs in the **stage world over the combat arena**, for the reason the movement gym runs there:
// the floor is derived from this repository's own values, so what a course measures is the body and
// the resolver rather than a map. The arena rather than the gym because the gym deliberately builds
// no navigation, and a cast body is an `ACharacter` following a Recast path — without a graph every
// travel order fails by name and the cast stands still. Its anchors are deliberately NOT created:
// an `intersting_place` in the room would let an ambient claim take the body off the course.
//
// A course drives the body through an **authored order** (`ElysiumCastCourses.h`) — a patrol route,
// a scripted beat — and never through the motor directly, so the recording covers the whole producer
// chain: the mind's executor, the intent seam, the driver, the selection record.
//
// One `FElysiumChannelRecorder` run per course under `$ELYSIUM_EXPORT_ROOT/_cast/`;
// `pipeline/src/elysium_pipeline/validation/channel_diff.py` is the comparator, the same one that
// judges the movement runs.
class FElysiumCastRun
{
public:
	static bool IsRequested();

	// The columns this producer declares — the shared body trace, verbatim. Exposed for the same
	// reason the movement harness exposes its own: `Elysium.Substrate.LocomotionTrace` holds the two
	// producers to one schema with no world.
	static TArray<const TCHAR*> DeclaredChannels();

	explicit FElysiumCastRun(UElysiumMapSubsystem* InSubsystem);
	~FElysiumCastRun();

private:
	bool Tick(float DeltaSeconds);

	// Stand the arena and ask for its Recast build. Once, before the first course.
	bool StandArena();
	// Kill the previous course's cast, dress the room for this one, and stand the body on its start.
	bool BeginCourse(int32 Index);
	// Deliver the course's travel order. Separate from `BeginCourse` because a freshly spawned body
	// has to reach the ground and finish activating before it can take one.
	void ArmCourse();
	// One recorded row.
	void Sample();
	// Flush the current course to disk.
	void FinishCourse();
	// Open the next course that passes the filter, or end the run.
	void NextCourse();
	void Fail(const TCHAR* Reason);

	UWorld* GetWorld() const;
	FElysiumEntityWorld* GetEntityWorld() const;
	// The engine body the course's character is wearing, or null while it is still being built.
	AElysiumNpcBody* FindBody() const;

	TWeakObjectPtr<UElysiumMapSubsystem> Subsystem;
	FTSTicker::FDelegateHandle TickHandle;

	int32 FrameInPhase = 0;
	int32 CourseIndex = -1;
	bool bDone = false;
	bool bArenaStood = false;
	int32 NavigationWaitFrames = 0;

	// The fixed step the run is driven at, from -CastHz (default 60).
	float StepSeconds = 1.0f / 60.0f;
	int32 Hz = 60;

	// Only these courses run, when -CastCourse=<name> is given.
	FString CourseFilter;
	// The body every course stands, from -CastBody=<stem>. Empty asks for the first baked stem,
	// which is then logged and written into the run metadata — a recording made on a different body
	// is a different recording, and it must not be silent about which one it was.
	FString BodyStem;

	ElysiumArena::FSpec Arena;
	ElysiumArena::FStanding ArenaStanding;

	FElysiumChannelRecorder Recorder;
	ElysiumLocomotionTrace::FTotals Totals;

	// The character this course spawned, and the entities the course dressed the room with.
	FElysiumEntityHandle CastEntity;
	TArray<FElysiumEntityHandle> CourseProps;

	// Frames left before the order is delivered, and frames left to record after it.
	int32 SpawnSettleRemaining = 0;
	int32 RecordRemaining = 0;
	bool bWarnedMissingBody = false;

	// Where the body started, so a run channel is a displacement rather than a world coordinate.
	FVector StartFeet = FVector::ZeroVector;
	FVector StartForward = FVector::ForwardVector;
	double AdvanceMax = 0.0;
};

#endif // !UE_BUILD_SHIPPING
