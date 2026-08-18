#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Debug/ElysiumArenaSpec.h"
#include "Debug/ElysiumCastCourses.h"
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
// system is a diff rather than an assertion. It declares the shared columns plus the one value only
// a commanded body has: what its motor was commanding while the cell played.
//
// **Two hosts, and they answer different questions.** Without `-ElysiumMap` it runs in the **stage
// world over the combat arena**, for the reason the movement gym runs there: the floor is derived
// from this repository's own values, so what a course measures is the body and the resolver rather
// than a map. That host is the regression instrument. With a map it dresses no room and builds no
// geometry: it stands one body — the map's own authored classname, model, stat block and loadout —
// on the map's own patrol route, over the map's own floor and navigation graph, which is the
// acceptance. A recording made on generated geometry is not evidence that the game's own content
// walks.
//
// It stands that body rather than adopting the one the level script already drives, and holds the
// rest of the map's cast still while the course runs (`HushOtherBodies`). Both are the same
// requirement: a course has to begin from the same state every time and measure one body. The map's
// own `patrol_cop` walks this very route, and two crowd agents converging on it are steered around
// each other in whichever order they met — which is a recording of Detour, not of the resolver.
//
// A course drives the body through an **authored order** (`ElysiumCastCourses.h`) — a patrol route,
// a scripted beat — and never through the motor directly, so the recording covers the whole producer
// chain: the mind's executor, the intent seam, the driver, the selection record.
//
// **A course either proves its claim or fails the run.** The no-slide predicate is intra-row and is
// evaluated here, frame by frame, against the body's own tables: a course that never moved, whose
// realized speed disagreed with the cell it was playing, or that never reached the gait it is a
// course of, exits non-zero. A baseline diff is a second opinion on a recording that already
// stands on its own.
//
// One `FElysiumChannelRecorder` run per course under `$ELYSIUM_EXPORT_ROOT/_cast/` (arena) or
// `_cast/<map>/` (sited); `pipeline/src/elysium_pipeline/validation/channel_diff.py` is the
// comparator, the same one that judges the movement runs.
class FElysiumCastRun
{
public:
	static bool IsRequested();

	// The columns this producer declares — the shared body trace, plus `act_cmd`. Exposed for the
	// same reason the movement harness exposes its own: `Elysium.Substrate.LocomotionTrace` holds
	// the two producers to one schema with no world.
	static TArray<const TCHAR*> DeclaredChannels();

	explicit FElysiumCastRun(UElysiumMapSubsystem* InSubsystem);
	~FElysiumCastRun();

private:
	bool Tick(float DeltaSeconds);

	// Stand the arena and ask for its Recast build. Once, before the first course, and never on a
	// sited host — a map brings its own floor and its own graph.
	bool StandArena();
	// Kill the previous course's cast, dress the room for this one, and stand the body on its start.
	bool BeginCourse(int32 Index);
	// Freeze every body in the world except this course's own. A sited host is a living map whose
	// script drives its own cast down the same routes; two agents that meet are steered around each
	// other by Detour in whichever order they arrived, which is a recording of the crowd. Arena
	// hosts stand nothing else and never call it.
	void HushOtherBodies();
	// Deliver the course's travel order. Separate from `BeginCourse` because a freshly spawned body
	// has to reach the ground and finish activating before it can take one. False when the order
	// could not be delivered at all.
	bool ArmCourse();
	// Whether the order actually took the body. An input that returns without arming its leaf is the
	// silent failure this exists to close: the body then stands still for the whole course and the
	// recording reads as an idle that resolved.
	bool IsCourseArmed() const;
	// One recorded row. False when the frame could not be recorded.
	bool Sample();
	// Flush the current course to disk and judge it. False when the course did not hold its claim.
	bool FinishCourse();
	// Open the next course that passes the filter, or end the run.
	void NextCourse();
	void Fail(const TCHAR* Reason);

	UWorld* GetWorld() const;
	FElysiumEntityWorld* GetEntityWorld() const;
	// The engine body the course's character is wearing, or null while it is still being built.
	AElysiumNpcBody* FindBody() const;
	// Where the recordings for this host go.
	FString OutputDir() const;
	// Delete this host's previous recordings, leaving its baseline alone. Once, before the first
	// course: a run that fell over half way through must not leave a stale course beside a fresh one
	// where the comparator will read the pair as one recording.
	bool ClearOutputDir();

	TWeakObjectPtr<UElysiumMapSubsystem> Subsystem;
	FTSTicker::FDelegateHandle TickHandle;

	int32 FrameInPhase = 0;
	int32 CourseIndex = -1;
	bool bDone = false;
	bool bArenaStood = false;
	bool bOutputCleared = false;
	int32 NavigationWaitFrames = 0;
	// How many courses the filter actually opened. A filter that matches nothing is a run that
	// records nothing and exits clean, which is the shape of a green run that proved nothing.
	int32 CoursesRun = 0;

	// The fixed step the run is driven at, from -CastHz (default 60).
	float StepSeconds = 1.0f / 60.0f;
	int32 Hz = 60;

	// Only these courses run, when -CastCourse=<name> is given.
	FString CourseFilter;
	// The body every arena course stands, from -CastBody=<stem>. A sited course names its own.
	FString BodyStem;
	// The map this run is sited on, from -ElysiumMap. Empty is the arena host.
	FString MapName;
	// The courses this run drives, resolved once from the host.
	TArray<ElysiumCastCourses::FCourse> Courses;

	ElysiumArena::FSpec Arena;
	ElysiumArena::FStanding ArenaStanding;

	FElysiumChannelRecorder Recorder;
	ElysiumLocomotionTrace::FTotals Totals;

	// The character this course spawned, and the entities the course dressed the room with.
	FElysiumEntityHandle CastEntity;
	TArray<FElysiumEntityHandle> CourseProps;

	// Frames left before the order is delivered, frames left to record after it, and how long the
	// order is given to take the body before the course is declared unarmed.
	int32 SpawnSettleRemaining = 0;
	int32 RecordRemaining = 0;
	int32 ArmWaitRemaining = 0;

	// Where the body started, so a run channel is a displacement rather than a world coordinate.
	FVector StartFeet = FVector::ZeroVector;
	FVector StartForward = FVector::ForwardVector;
	double AdvanceMax = 0.0;

	// The cell the previous recorded frame published, and the direction and state it published it
	// under.
	//
	// **The chain has one frame in it, and the predicate has to be aligned to that or it is
	// measuring the lag.** A row's velocity was realized under the speed the motor was commanding
	// when that row's animation pass began, and that command is the cell the *previous* pass
	// published — the pass that produces a row reads the sample, resolves the cell, and only then
	// re-commands the mover, so this row's own cell steers the next row's travel. On a turning body
	// the two are several u/s apart because the fan's cells are, which is the fan doing its job.
	struct FPublishedCell
	{
		bool bValid = false;
		double Stride = 0.0;
		double MoveYaw = 0.0;
		EElysiumGraphState State = EElysiumGraphState::Idle;
	};
	FPublishedCell Previous;

	// The intra-row no-slide tally, in Source units throughout.
	struct FNoSlide
	{
		int32 MovingFrames = 0;
		// Frames the body was travelling AND was at what its motor commanded. Only these can say
		// anything about the cell: a body still accelerating into its order, or one the path
		// follower is holding under it, is slower than its command for an honest reason, and neither
		// is a claim about what it is playing.
		int32 AtSpeedFrames = 0;
		// Frames whose command was not the cell the previous pass published. This is the defect the
		// rung is named for, and it is a separate count because it is a different failure from the
		// body not travelling at what it was told: the two keys are the motor's requested gait and
		// the record's projected graph state, and they can disagree.
		int32 CommandMismatches = 0;
		double WorstCommand = 0.0;
		int32 SlideFrames = 0;
		// At-speed frames whose published state is one of the three gaits, where the cell that
		// steered the row has to be this body's own fan read at the row's own direction. It is the
		// strongest of the three because it is answerable at every angle.
		int32 CellFrames = 0;
		int32 CellMismatches = 0;
		double WorstCell = 0.0;
		// At-speed frames travelling within the forward window, where the cell being played has to
		// be the body's own authored forward cell rather than one of its strafes.
		int32 ForwardFrames = 0;
		int32 ForwardMismatches = 0;
		double WorstSlide = 0.0;
		double WorstForward = 0.0;
		// The widest bar any forward frame was judged at — the epsilon plus this fan's own blend
		// across the window. Reported so a reader can tell a tight fan from a loose one rather than
		// reading `WorstForward` against a constant that is not the one that applied.
		double WorstForwardAllowed = 0.0;
		int32 FirstBadFrame = INDEX_NONE;
		double FirstBadSpeed = 0.0;
		double FirstBadStride = 0.0;
		double FirstBadCommanded = 0.0;

		void Reset() { *this = FNoSlide(); }
	};
	FNoSlide NoSlide;
	// The generation the log last named, so the selection record reaches the log once per request
	// rather than once per frame.
	uint32 LoggedGeneration = 0;
};

#endif // !UE_BUILD_SHIPPING
