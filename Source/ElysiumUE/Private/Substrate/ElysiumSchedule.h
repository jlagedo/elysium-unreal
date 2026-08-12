#pragma once

#include "CoreMinimal.h"

// VtMB's schedule/task machinery, as much of it as we have recovered producers for.
//
// A schedule is an ordered task program. The NPC runs one task at a time across thinks; a task
// answers Running, Complete or Failed, a completed task advances the index, and a failed task ends
// the schedule through its fail schedule. That is the whole kernel -- the behaviour lives in the
// task bodies, and the bodies live on the entity, reached through `IElysiumScheduleRunner` so this
// file stays free of the world, the engine and the clip vocabulary.
//
// Recovered facts: `docs/vtmb/npc-ai-reverse-engineering.md` -> "The idle branch, decided" for the
// state-1 selection order, and "Door-obstruction schedule selection" for step 6. The IDs below are
// retail's own registered numbers, kept so a trace row reads like the binary's.
//
// Only the tasks the implemented schedules need are enumerated. Every other task ID is deliberately
// absent rather than stubbed: an unknown task is a schedule this runtime cannot honestly run, and
// the runner fails it by name instead of quietly skipping a step.

enum class EElysiumTask : uint8
{
	// The disposition stance machine's per-completion selection (`TASK_SPECIAL_IDLE_ACTIVITY`).
	SpecialIdleActivity,
	// Hold until the body is in the player's PVS (`TASK_WAIT_PVS`). Our oracle is a render-time
	// query; the divergence is stated on `IElysiumEmbodiment::IsNpcBodyVisible`.
	WaitPvs,
	// Play a named ACT_* activity on the body (`TASK_SET_ACTIVITY`).
	SetActivity,
	// Hold for `Param` seconds (`TASK_WAIT`).
	Wait,
	// Hold for a uniform random 0..`Param` seconds (`TASK_WAIT_RANDOM`).
	WaitRandom,
	// Turn to face the position saved by the door-obstruction selector (`TASK_FACE_SAVEPOSITION`).
	FaceSavePosition,
	// Step back from the saved position (`TASK_MOVE_AWAY_PATH` and its follow-ups).
	MoveAwayFromSavePosition,
};

enum class EElysiumScheduleId : uint8
{
	None,
	IdleDisposition,          // 0x6b SCHED_TROIKA_IDLE_DISPOSITION
	AlertLookAroundNi,        // 0x4f SCHED_TROIKA_ALERT_LOOK_AROUND_NI
	BackAwayFromDoorNe,       // 0x91 SCHED_TROIKA_BACK_AWAY_FROM_DOOR_NE
	BackAwayFromDoorWaitNe,   // 0x96 SCHED_TROIKA_BACK_AWAY_FROM_DOOR_WAIT_NE
	TakeCoverHintDoor,        // 0x9c SCHED_TROIKA_TAKE_COVER_HINT_DOOR
};

// Retail's registered number for a schedule, so a trace row and the binary agree.
int32 ElysiumScheduleNumber(EElysiumScheduleId Id);
const TCHAR* ElysiumScheduleName(EElysiumScheduleId Id);
const TCHAR* ElysiumTaskName(EElysiumTask Task);

struct FElysiumTaskStep
{
	EElysiumTask Task;
	// The task's single authored operand. Retail spells these as floats in the schedule tables.
	float Param = 0.f;
	// The activity name for `SetActivity`; empty for every other task.
	FString Activity;
};

struct FElysiumSchedule
{
	EElysiumScheduleId Id = EElysiumScheduleId::None;
	TArray<FElysiumTaskStep> Tasks;
	// Where a failed task goes. `None` ends the schedule and returns the NPC to selection.
	EElysiumScheduleId FailSchedule = EElysiumScheduleId::None;

	bool IsValid() const { return Id != EElysiumScheduleId::None && !Tasks.IsEmpty(); }
};

// The registry. Schedules are data, so they are stated once here rather than built per NPC
// (`gameplay-systems-architecture.md` K9).
const FElysiumSchedule* ElysiumScheduleFor(EElysiumScheduleId Id);

enum class EElysiumTaskResult : uint8
{
	Running,     // still working; ask again on the next think
	Complete,    // advance to the next task
	Failed,      // end the schedule through its fail schedule
};

// What a task body needs from whoever owns the body. Implemented by the NPC; the recording double
// in the tests implements it too, which is what makes a whole task program assertable with no
// engine.
class IElysiumScheduleRunner
{
public:
	virtual ~IElysiumScheduleRunner() = default;

	// `TASK_SPECIAL_IDLE_ACTIVITY` -- run one disposition stance selection. Returns the chosen
	// clip's length in seconds, or a negative value when this body has no stance machine.
	virtual float RunSpecialIdleActivity(double Now) = 0;
	// `TASK_WAIT_PVS` -- is the body visible to the player right now?
	virtual bool IsBodyVisible() const = 0;
	// `TASK_SET_ACTIVITY` -- play a named ACT_*. Returns its length, or negative when unresolvable.
	virtual float PlayActivity(const FString& Activity) = 0;
	// `TASK_FACE_SAVEPOSITION` / `TASK_MOVE_AWAY_PATH` -- the door-obstruction motor verbs. Both
	// answer false where there is no motor, which fails the task rather than pretending it ran.
	virtual bool FaceSavePosition() { return false; }
	virtual bool StepAwayFromSavePosition(float DistanceCm) { return false; }
	// A uniform draw in [0, Max], from the NPC schedule stream.
	virtual float RandomSeconds(float Max) = 0;
	// One trace row, so a decision is readable without a rebuild.
	virtual void RecordScheduleEvent(const FString& Row) {}
};

// The per-NPC runner state. Saved as part of the NPC, so a schedule survives a save.
struct FElysiumScheduleState
{
	EElysiumScheduleId Current = EElysiumScheduleId::None;
	int32 TaskIndex = 0;
	// Set when a timed task starts; the substrate clock decides when it completes.
	double TaskEndsAt = 0.0;
	bool bTaskStarted = false;

	bool IsRunning() const { return Current != EElysiumScheduleId::None; }
	void Clear()
	{
		Current = EElysiumScheduleId::None;
		TaskIndex = 0;
		TaskEndsAt = 0.0;
		bTaskStarted = false;
	}
};

namespace ElysiumSchedule
{
	// Begin `Id`, discarding whatever was running. Returns false for an unknown or empty schedule,
	// which leaves the state cleared rather than half-started.
	bool Start(FElysiumScheduleState& State, EElysiumScheduleId Id, IElysiumScheduleRunner& Runner);

	/**
	 * Advance the running schedule by one think.
	 *
	 * `OutNextThinkDelay` receives how long the caller should wait before asking again -- a timed
	 * task hands back its own remainder, so a five-second wait costs one think rather than fifty.
	 * Returns false once the schedule has ended (completed or failed through to nothing), which is
	 * the caller's signal to select again.
	 */
	bool Tick(FElysiumScheduleState& State, IElysiumScheduleRunner& Runner, double Now,
		double& OutNextThinkDelay);
}
