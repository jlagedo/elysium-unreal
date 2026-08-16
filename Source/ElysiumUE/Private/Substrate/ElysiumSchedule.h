#pragma once

#include "CoreMinimal.h"

#include "Substrate/ElysiumNpcConditions.h"   // the interrupt mask a schedule declares

class IElysiumNpcMotor;   // the reachability query `TASK_MOVE_AWAY_PATH` asks the world

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

	// --- The combat vocabulary ------------------------------------------------------------------
	// The 12 additional task identities the registered combat families use, under their recovered
	// names (`docs/vtmb/npc-ai-reverse-engineering.md` -> "Schedules and tasks"). Everything else in
	// the 441-identity library stays absent: an unknown task is a schedule this runtime cannot
	// honestly run, and the runner fails it by name.

	// Redirect this program's failure route (`TASK_SET_FAIL_SCHEDULE`, 328 invocations). Reads
	// `FElysiumTaskStep::Target`.
	SetFailSchedule,
	// Cancel the outstanding movement request (`TASK_STOP_MOVING`, 275). A body that was not moving
	// is not a failure, so this always completes.
	StopMoving,
	// How close to the goal counts as arrived, in SOURCE UNITS (`TASK_SET_TOLERANCE_DISTANCE`, 175).
	// The one conversion to centimetres happens at the motor call, like every other recovered
	// distance in this runtime.
	SetToleranceDistance,
	// Path to the committed enemy at the tolerance in force (`TASK_GET_PATH_TO_ENEMY`, 37).
	GetPathToEnemy,
	// Take the path at running locomotion (`TASK_RUN_PATH`, 133).
	RunPath,
	// Hold until the outstanding request arrives or fails (`TASK_WAIT_FOR_MOVEMENT`, 230).
	WaitForMovement,
	// Turn in place toward the committed enemy (`TASK_FACE_ENEMY`, 69).
	FaceEnemy,
	// The incoming-attack notice the aimed opponent receives (`TASK_ANNOUNCE_ATTACK`). No health or
	// damage changes: it is opponent reservation (`docs/vtmb/combat-and-damage.md`).
	AnnounceAttack,
	// Drive the active weapon's primary attack (`TASK_MELEE_ATTACK1` / `TASK_RANGE_ATTACK1`). The
	// weapon controller owns the transaction; the task only presses.
	MeleeAttack1,
	RangeAttack1,
	// Transfer to another program (`TASK_SET_SCHEDULE`, 145). Reads `FElysiumTaskStep::Target`.
	SetSchedule,
	// Record a fact for later selection (`TASK_REMEMBER`, 41).
	//
	// SEAM (traced, no consumer): the operand names one of retail's memory bits and the bit table is
	// not decoded, so what is remembered is carried as a number and read by nobody. The task is here
	// because `SCHED_SMALL_FLINCH` opens with it and dropping a step would misreport the program.
	Remember,

	// --- The scripted-director vocabulary --------------------------------------------------------
	// Path to the goal an `aiscripted_schedule` pushed (`TASK_GET_PATH_TO_GOAL`). It reads no
	// operand: the goal, the route and the gait are the pushed order's, exactly as
	// `TASK_GET_PATH_TO_ENEMY` reads the committed enemy off memory rather than off a task column.
	GetPathToGoal,
};

enum class EElysiumScheduleId : uint8
{
	None,
	IdleDisposition,          // 0x6b SCHED_TROIKA_IDLE_DISPOSITION
	AlertLookAroundNi,        // 0x4f SCHED_TROIKA_ALERT_LOOK_AROUND_NI
	BackAwayFromDoorNe,       // 0x91 SCHED_TROIKA_BACK_AWAY_FROM_DOOR_NE
	BackAwayFromDoorWaitNe,   // 0x96 SCHED_TROIKA_BACK_AWAY_FROM_DOOR_WAIT_NE
	TakeCoverHintDoor,        // 0x9c SCHED_TROIKA_TAKE_COVER_HINT_DOOR

	// --- The combat families (`Substrate/ElysiumNpcCombatSchedules.cpp` registers every one) -----
	MeleeAttack1,             // 0xdc SCHED_TROIKA_MELEE_ATTACK1
	MeleeAttack1Nr,           // 0xdd SCHED_TROIKA_MELEE_ATTACK1_NR
	MeleeAttack1Swing,        //      SCHED_TROIKA_MELEE_ATTACK1_SWING (number not decoded)
	MeleeDodge,               // 0xd5 SCHED_TROIKA_MELEE_DODGE
	MeleePreblock,            // 0xd6 SCHED_TROIKA_MELEE_PREBLOCK
	MeleeKick,                // 0xdb SCHED_TROIKA_MELEE_KICK
	MeleeStepback,            // 0xd3 SCHED_TROIKA_MELEE_STEPBACK
	MeleeIdle,                // 0xc7 SCHED_TROIKA_MELEE_IDLE
	MeleeAdvance,             // 0xca SCHED_TROIKA_MELEE_ADVANCE
	MeleeCircle,              // 0xe0 SCHED_TROIKA_MELEE_CIRCLE
	ChaseEnemy,               // 0xb1 SCHED_TROIKA_CHASE_ENEMY
	ChaseEnemyFailed,         //      SCHED_TROIKA_CHASE_ENEMY_FAILED (number not decoded)
	RangeAttack1,             // 0xec SCHED_TROIKA_RANGE_ATTACK1
	RunAway,                  // 0xb9 SCHED_TROIKA_RUN_AWAY
	SmallFlinch,              // 0x14 SCHED_SMALL_FLINCH
	AlertSmallFlinch,         // 0x07 SCHED_ALERT_SMALL_FLINCH
	TakeCoverFromOrigin,      // 0x19 SCHED_TAKE_COVER_FROM_ORIGIN

	// --- The scripted-director family (`Substrate/ElysiumAiScriptedSchedule.cpp` registers both) --
	ScriptedMoveToGoal,       // `aiscripted_schedule` modes 1 and 2
	ScriptedFollowPath,       // `aiscripted_schedule` modes 4 and 5
};

// Retail's registered number for a schedule, so a trace row and the binary agree.
int32 ElysiumScheduleNumber(EElysiumScheduleId Id);
const TCHAR* ElysiumScheduleName(EElysiumScheduleId Id);
const TCHAR* ElysiumTaskName(EElysiumTask Task);

/**
 * The name -> id direction, for the two script-facing schedule commands.
 *
 * `ChangeSchedule` and `StartSchedule` "name native schedules explicitly"
 * (`docs/vtmb/npc-ai-reverse-engineering.md` -> "Direct schedule changes"), so schedule identity is
 * authored API and needs a lookup rather than a number. Only a REGISTERED program resolves: a name
 * this runtime carries no program for has to fail by name, because starting some other schedule
 * under an authored name would be a behaviour invented out of a string.
 *
 * Matching is case-insensitive over `ElysiumScheduleName`.
 */
bool ElysiumScheduleIdFromName(const FString& Name, EElysiumScheduleId& OutId);

struct FElysiumTaskStep
{
	EElysiumTask Task;
	// The task's single authored operand. Retail spells these as floats in the schedule tables.
	float Param = 0.f;
	// The activity name for `SetActivity`; empty for every other task.
	FString Activity;
	// The program `SetFailSchedule` / `SetSchedule` names. Retail spells a schedule operand as a
	// registered number in the same float column; it is kept as the identity here so a program reads
	// as the schedule it transfers to rather than as a magic constant.
	EElysiumScheduleId Target = EElysiumScheduleId::None;
};

struct FElysiumSchedule
{
	EElysiumScheduleId Id = EElysiumScheduleId::None;
	TArray<FElysiumTaskStep> Tasks;
	// Where a failed task goes. `None` ends the schedule and returns the NPC to selection.
	EElysiumScheduleId FailSchedule = EElysiumScheduleId::None;

	/**
	 * Which newly gathered conditions may abort this task program
	 * (`docs/vtmb/npc-ai-reverse-engineering.md` -> "Interrupt conditions").
	 *
	 * **Empty means interruptible by nothing**, and that is a real recovered posture rather than an
	 * unfilled default: `SCHED_TROIKA_MELEE_ATTACK1_SWING` declares no interrupts at all, "so once
	 * that terminal attack task owns the NPC it is not reevaluated as a fresh attack choice each
	 * tick". The schedule — not the mere existence of a condition — decides whether a new stimulus
	 * pre-empts behaviour, which is why a faithful AI cannot be one global priority list.
	 *
	 * A mask is filled in from a decoded registration site wherever there is one, and is otherwise
	 * either left empty or filled from the interrupt census with a CHOSEN mark beside the program —
	 * the distinction between those two, and why the idle pair could not stay empty, is stated at
	 * the registry and at `MinimalCombatMask` in `Substrate/ElysiumNpcCombatSchedules.cpp`.
	 *
	 * An interrupt is NOT a task failure: a failed task goes to `FailSchedule`, while an interrupt
	 * ends the program and returns the NPC to selection ("until it completes, fails, or an interrupt
	 * condition forces reselection"). Routing an interrupt through the fail schedule would send an
	 * NPC that just acquired an enemy into a cover or flinch program instead of re-selecting.
	 */
	FElysiumNpcConditions Interrupts;

	// SEAM (named, unimplemented): 42 schedules carry a `DELAY_INTERRUPTS` flag. What "delayed"
	// means — a deferral window, a task boundary, a one-shot suppression — is not recovered, and
	// none of the schedules this runtime registers is among the 42. The flag is named here so a
	// recovered schedule that carries it has somewhere to land; nothing reads it, and nothing
	// should until the semantics are decoded.
	bool bDelayInterrupts = false;

	bool IsValid() const { return Id != EElysiumScheduleId::None && !Tasks.IsEmpty(); }
};

// The registry. Schedules are data, so they are stated once here rather than built per NPC
// (`gameplay-systems-architecture.md` K9).
const FElysiumSchedule* ElysiumScheduleFor(EElysiumScheduleId Id);

namespace ElysiumSchedule
{
	/**
	 * The registry's one growth point.
	 *
	 * A domain file states its own programs and installs them here at static init — the combat
	 * families live in `Substrate/ElysiumNpcCombatSchedules.cpp` beside the selectors that choose
	 * them, because a program and the policy that picks it are one decision. This kernel carries the
	 * task vocabulary and the two idle/door programs it was written around, and nothing else.
	 *
	 * Registering an id twice replaces the earlier program and says so: a duplicate is a build
	 * mistake, not a merge.
	 */
	void Register(FElysiumSchedule&& Program);
}

#if WITH_DEV_AUTOMATION_TESTS
namespace ElysiumSchedule
{
	// Test-only: install an interrupt mask on a registered program for the lifetime of the scope,
	// restoring the previous one on destruction.
	//
	// It exists because every registered program's recovered mask is empty (nothing in the survey
	// names one), so the kernel's interrupt path would otherwise have no content-free driver at
	// all — and "the code is unreachable" is not the same claim as "the code is right". Nothing
	// outside a test may install a mask: a mask is authored data, and inventing one at runtime is
	// the behavioural change the empty defaults exist to refuse.
	struct FInterruptMaskScope
	{
		FInterruptMaskScope(EElysiumScheduleId Id, const FElysiumNpcConditions& Mask);
		~FInterruptMaskScope();

		FInterruptMaskScope(const FInterruptMaskScope&) = delete;
		FInterruptMaskScope& operator=(const FInterruptMaskScope&) = delete;

	private:
		EElysiumScheduleId Target;
		FElysiumNpcConditions Previous;
		bool bInstalled = false;
	};
}
#endif

enum class EElysiumTaskResult : uint8
{
	Running,     // still working; ask again on the next think
	Complete,    // advance to the next task
	Failed,      // end the schedule through its fail schedule
};

// What `TASK_WAIT_FOR_MOVEMENT` sees when it samples the outstanding request. Three answers rather
// than a bool, because "still travelling" and "the body gave up" take different routes out of the
// schedule: one holds the task, the other fails it into the fail schedule.
enum class EElysiumMoveWatch : uint8
{
	Moving,
	Arrived,
	Failed,
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

	// --- The combat verbs -----------------------------------------------------------------------
	// Every one defaults to the answer a runner with no body can honestly give. The movement verbs
	// default to refusing, which fails their task by name; `StopMoving` and `RememberFact` cannot
	// fail, because neither asserts anything about the world.

	virtual void StopMoving() {}
	// Issue the path to the committed enemy. `ToleranceUnits` is the schedule's own operand, in
	// Source units. False means no enemy, no body, or a body that would not take the request — all
	// three fail the task, and the runner names which.
	virtual bool GetPathToEnemy(float ToleranceUnits) { return false; }
	// Put running locomotion on the body. It cannot fail the schedule: a model whose bank carries no
	// run clip still travels, and refusing the chase over a missing animation would be a
	// presentation defect deciding a behaviour. The runner records the miss.
	virtual void RunPath() {}
	virtual EElysiumMoveWatch WaitForMovement() { return EElysiumMoveWatch::Failed; }
	virtual bool FaceEnemy() { return false; }
	// `TASK_ANNOUNCE_ATTACK` — the incoming-attack notice the aimed enemy receives. False only when
	// there is no enemy to announce to.
	virtual bool AnnounceAttack(float Param) { return false; }
	// Press the active weapon's primary attack at the committed enemy. False for a missing or
	// ineligible weapon, which is what fails the swing into its melee-idle fail schedule.
	virtual bool MeleeAttack1() { return false; }
	virtual bool RangeAttack1() { return false; }
	// `TASK_REMEMBER`, traced and otherwise inert (see `EElysiumTask::Remember`).
	virtual void RememberFact(float What) {}

	// `TASK_GET_PATH_TO_GOAL` — issue the next leg of the scripted order this NPC was pushed, at the
	// order's own gait. False means no order, no body, an exhausted route or a body that would not
	// take the request; all four fail the task, and the runner names which.
	virtual bool GetPathToScriptedGoal() { return false; }
};

// The per-NPC runner state. Saved as part of the NPC, so a schedule survives a save.
struct FElysiumScheduleState
{
	EElysiumScheduleId Current = EElysiumScheduleId::None;
	int32 TaskIndex = 0;
	// Set when a timed task starts; the substrate clock decides when it completes.
	double TaskEndsAt = 0.0;
	bool bTaskStarted = false;

	// What `TASK_SET_FAIL_SCHEDULE` and `TASK_SET_TOLERANCE_DISTANCE` wrote for THIS run of the
	// program. Both are per-run rather than per-program: the same schedule reached from two
	// selectors carries whatever its own tasks set, and `Start` resets them so a previous program's
	// tolerance can never leak into the next one's path request.
	EElysiumScheduleId FailScheduleOverride = EElysiumScheduleId::None;
	// Source units, and negative means "the task never ran, so the motor's own acceptance decides".
	float ToleranceUnits = -1.f;

	bool IsRunning() const { return Current != EElysiumScheduleId::None; }
	void Clear()
	{
		Current = EElysiumScheduleId::None;
		TaskIndex = 0;
		TaskEndsAt = 0.0;
		bTaskStarted = false;
		FailScheduleOverride = EElysiumScheduleId::None;
		ToleranceUnits = -1.f;
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
	 * Returns false once the schedule has ended (completed, failed through to nothing, or been
	 * interrupted), which is the caller's signal to select again.
	 *
	 * `Conditions` is this decision pass's gathered set, checked against the active schedule's
	 * interrupt mask at the top of the tick and before any task work. Null means "no conditions
	 * were gathered for this pass" -- a headless kernel test, or a think that ran with condition
	 * gathering suppressed -- and skips the check entirely rather than testing an empty set.
	 */
	bool Tick(FElysiumScheduleState& State, IElysiumScheduleRunner& Runner, double Now,
		double& OutNextThinkDelay, const FElysiumNpcConditions* Conditions = nullptr);

	// --- `TASK_MOVE_AWAY_PATH`, whole (11.14) --------------------------------------------------
	// Where the step back wants to land, whether the world will have it, and whether what the world
	// handed back is still a retreat. It lives here rather than inside the NPC leaf for the reason
	// the rest of this file does: the leaf class is file-local, so a rule spelled out there is a
	// rule no Substrate test can drive. Every branch below is one this kernel's fail path depends
	// on being distinguishable.
	enum class ERetreat : uint8
	{
		Moving,         // navigable, still a retreat, and the body took the request
		NoMotor,        // no body to ask -- the supported headless/backdrop case
		Degenerate,     // the NPC is standing ON the save position; there is no direction to leave in
		Unprojectable,  // the world carries no navigable surface at the extrapolated point
		NotARetreat,    // navigable, but the projection put it no further from what it was leaving
		MotorRefused,   // a good destination the body would not path to
	};
	const TCHAR* RetreatResultName(ERetreat Result);

	// A retreat that has gained less than this much ground has gained none: the margin is what a
	// projection sliding the point along a wall costs, and a step that only slid sideways is not a
	// step back.
	inline constexpr double RetreatMarginCm = 8.0;

	/**
	 * Extrapolate `DistanceCm` directly away from `SavePosition` in the horizontal plane, project
	 * that point onto the navigable surface through the motor, RE-TEST the projection against the
	 * retreat rule, and issue the move.
	 *
	 * The re-test is the point of the task: projection answers "where can someone stand", not "is
	 * this still away from the door", so a point pulled back through the doorway is a navigable
	 * point that must fail the schedule rather than walk the NPC into the swing it was told to
	 * leave. `OutDestination` receives the projected point on every result that got that far.
	 */
	ERetreat StepAwayFromSavePosition(IElysiumNpcMotor* Motor, const FVector& Origin,
		const FVector& SavePosition, float DistanceCm, FVector& OutDestination);
}
