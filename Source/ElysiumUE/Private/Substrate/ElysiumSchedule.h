#pragma once

#include "CoreMinimal.h"

#include "Substrate/ElysiumNpcConditions.h"   // the interrupt mask a schedule declares
#include "Substrate/ElysiumNpcFlags.h"        // the flag word `TASK_SET_NPC_FLAG` names

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

	// The combat vocabulary.
	// The 12 additional task identities the registered combat families use, under their recovered
	// names (`docs/vtmb/npc-ai-reverse-engineering.md` -> "Schedules and tasks"). Everything else in
	// the 441-identity library stays absent: an unknown task is a schedule this runtime cannot
	// honestly run, and the runner fails it by name.

	// Redirect this program's failure route (`TASK_SET_FAIL_SCHEDULE`, 328 invocations). Reads
	// `FElysiumTaskStep::Target`.
	SetFailSchedule,
	// TASK_STOP_MOVING 0x69. StartTask clears an active goal; RunTask waits out Jump/Climb
	// and fails a motionless airborne jump with reason 0x1c (0x10288963).
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

	// The death vocabulary.
	// `TASK_PLAY_DEATH_SEQUENCE` (0x149, the last identity in the recovered 441-task library). It
	// walks the recovered ladder — the task's own argument as an activity, then `ACT_DIESIMPLE`, then
	// `ACT_IDLE` — and hands the surviving choice to the body
	// (`docs/vtmb/animation_and_movers.md` -> the `RunTask` activity table). The argument rides in
	// `FElysiumTaskStep::Activity`; empty means the program named none.
	PlayDeathSequence,

	// The incapacitation vocabulary.
	// Both are `StartTask`-only in retail — neither has a `RunTask` arm, so both complete on the
	// think that begins them.

	// `TASK_MAKE_OBLIVIOUS` (0x131, arm `0x102a72e3`). Reads `FElysiumTaskStep::Param`, which the
	// schedule compiler writes as 1.0 for `TRUE` and 0.0 for `FALSE`.
	MakeOblivious,
	// `TASK_SET_NPC_FLAG` (0x100, arm `0x102a585d`). Reads `FElysiumTaskStep::Flag`.
	//
	// `TASK_CLEAR_NPC_FLAG` (0x101) is its exact mirror and is deliberately ABSENT: no registered
	// program clears a flag, and every bit the registered programs set is released by
	// `IElysiumScheduleRunner::OnScheduleChange` instead. Add it with the schedule that needs it.
	SetNpcFlag,

	// The scripted-director vocabulary.
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

	// The combat families (`Substrate/ElysiumNpcCombatSchedules.cpp` registers every one).
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

	// The death family (`Substrate/ElysiumNpcCombatSchedules.cpp` registers it).
	Die,                      // SCHED_DIE (number not decoded)

	// The post-feed trance (`Substrate/ElysiumFeedSchedules.cpp` registers it beside its producer).
	Mesmerized,               // 0xfb SCHED_TROIKA_MESMERIZED

	// The scripted-director family (`Substrate/ElysiumAiScriptedSchedule.cpp` registers both).
	ScriptedMoveToGoal,       // `aiscripted_schedule` modes 1 and 2
	ScriptedFollowPath,       // `aiscripted_schedule` modes 4 and 5
};

// Retail's registered number for a schedule, so a trace row and the binary agree.
int32 ElysiumScheduleNumber(EElysiumScheduleId Id);
const TCHAR* ElysiumScheduleName(EElysiumScheduleId Id);
const TCHAR* ElysiumTaskName(EElysiumTask Task);
const TCHAR* ElysiumTaskFailureName(int32 Reason);

// The name -> id direction, for the two script-facing schedule commands.
//
// `ChangeSchedule` and `StartSchedule` "name native schedules explicitly"
// (`docs/vtmb/npc-ai-reverse-engineering.md` -> "Direct schedule changes"), so schedule identity is
// authored API and needs a lookup rather than a number. Only a REGISTERED program resolves: a name
// this runtime carries no program for has to fail by name, because starting some other schedule
// under an authored name would be a behaviour invented out of a string.
//
// Matching is case-insensitive over `ElysiumScheduleName`.
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
	// The bit `TASK_SET_NPC_FLAG` names; `None` for every other task. Retail spells this operand as
	// `NPCFlag:<name>` and resolves it to a mask at schedule-load time (`0x1030cbd0`), so it is a
	// typed identity here rather than a number in the float column.
	EElysiumNpcFlag Flag = EElysiumNpcFlag::None;
};

struct FElysiumSchedule
{
	EElysiumScheduleId Id = EElysiumScheduleId::None;
	TArray<FElysiumTaskStep> Tasks;
	// Where a failed task goes. `None` ends the schedule and returns the NPC to selection.
	EElysiumScheduleId FailSchedule = EElysiumScheduleId::None;

	// Which newly gathered conditions may abort this task program
	// (`docs/vtmb/npc-ai-reverse-engineering.md` -> "Interrupt conditions").
	//
	// **Empty means interruptible by nothing**, and that is a real recovered posture rather than an
	// unfilled default: `SCHED_TROIKA_MELEE_ATTACK1_SWING` declares no interrupts at all, "so once
	// that terminal attack task owns the NPC it is not reevaluated as a fresh attack choice each
	// tick". The schedule — not the mere existence of a condition — decides whether a new stimulus
	// pre-empts behaviour, which is why a faithful AI cannot be one global priority list.
	//
	// A mask is filled in from a decoded registration site wherever there is one, and is otherwise
	// either left empty or filled from the interrupt census with a CHOSEN mark beside the program —
	// the distinction between those two, and why the idle pair could not stay empty, is stated at
	// the registry and at `MinimalCombatMask` in `Substrate/ElysiumNpcCombatSchedules.cpp`.
	//
	// An interrupt is NOT a task failure: a failed task goes to `FailSchedule`, while an interrupt
	// ends the program and returns the NPC to selection ("until it completes, fails, or an interrupt
	// condition forces reselection"). Routing an interrupt through the fail schedule would send an
	// NPC that just acquired an enemy into a cover or flinch program instead of re-selecting.
	FElysiumNpcConditions Interrupts;

	// `DELAY_INTERRUPTS`, the ONLY schedule flag retail has. Its token table
	// (`vampire.dll 0x1030d7e0`) answers exactly two spellings — `NONE` -> 0 and `DELAY_INTERRUPTS`
	// -> bit 0 — and makes anything else a load-time `Error`, so `bDelayInterrupts` is the whole
	// flag word rather than one bit of a set.
	//
	// It is NOT a property the interrupt check can consult on its own. The sole tester,
	// `CAI_BaseNPC::IsScheduleValid` (`0x10280ff0`, called only from `MaintainSchedule`
	// `0x102817c0`), ANDs it with the NPC's own `m_bDidMaintainSchedule` (`+0x5bb8`):
	//
	//     if (!(!m_bDidMaintainSchedule && (schedule->flags & 1)))  evaluate the interrupt mask
	//
	// so the flag buys a schedule exactly ONE think of immunity, re-armed by every install and
	// bounded by nothing else — no timer, no task boundary, no deferral store. See
	// `FElysiumScheduleState::bDidMaintainSchedule` for the other half and for what "one think"
	// buys, and `ElysiumSchedule::Start` for the condition clear that goes with it.
	//
	// 42 of retail's 691 schedules carry it, and they are one family: the Discipline effects and the
	// externally forced states (`D_MESMERIZE`, `D_DAZE`, `D_BERSERK`, `D_TRANCE`, `FLEE_AND_DIE`,
	// `TROIKA_MESMERIZED`). All of them are installed from OUTSIDE the AI think, which is the case
	// the flag exists for: without it a forced state is re-selected away on the same think that
	// forced it.
	bool bDelayInterrupts = false;

	bool IsValid() const { return Id != EElysiumScheduleId::None && !Tasks.IsEmpty(); }
};

// The registry. Schedules are data, so they are stated once here rather than built per NPC.
const FElysiumSchedule* ElysiumScheduleFor(EElysiumScheduleId Id);

namespace ElysiumSchedule
{
	// The registry's one growth point.
	//
	// A domain file states its own programs and installs them here at static init — the combat
	// families live in `Substrate/ElysiumNpcCombatSchedules.cpp` beside the selectors that choose
	// them, because a program and the policy that picks it are one decision. This kernel carries the
	// task vocabulary and the two idle/door programs it was written around, and nothing else.
	//
	// Registering an id twice replaces the earlier program and says so: a duplicate is a build
	// mistake, not a merge.
	void Register(FElysiumSchedule&& Program);
}

#if WITH_DEV_AUTOMATION_TESTS
namespace ElysiumSchedule
{
	// Test-only: install an interrupt mask on a registered program for the lifetime of the scope,
	// restoring the previous one on destruction.
	//
	// It exists so a kernel case can drive the interrupt path against a mask of its own choosing
	// rather than against whatever the registered program authors: the idle pair here carries a
	// CHOSEN mask, the door programs an empty one, and the combat families in
	// `ElysiumNpcCombatSchedules.cpp` either a decoded mask or the CHOSEN `MinimalCombatMask`, so a
	// case that wants to prove "this
	// condition, and only this condition, ends the program" needs a mask it controls. Nothing
	// outside a test may install one: a mask is authored data, and inventing one at runtime is a
	// behavioural change.
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

	// Test-only: install a task's authored ACTIVITY operand on a registered program for the lifetime
	// of the scope, restoring the previous one on destruction.
	//
	// Same reason as the mask scope beside it, for the same kind of value.
	// `TASK_PLAY_DEATH_SEQUENCE` takes an activity argument and its first ladder rung is that
	// argument, but no registered program authors one — retail spells the operand as an
	// activity-index number and this runtime has no decoded index table — so the rung would
	// otherwise have no content-free driver at all. Nothing outside a test may install an operand:
	// authoring one at runtime is the behavioural change the empty default exists to refuse.
	struct FTaskActivityScope
	{
		FTaskActivityScope(EElysiumScheduleId Id, int32 TaskIndex, const FString& Activity);
		~FTaskActivityScope();

		FTaskActivityScope(const FTaskActivityScope&) = delete;
		FTaskActivityScope& operator=(const FTaskActivityScope&) = delete;

		bool IsInstalled() const { return bInstalled; }

	private:
		EElysiumScheduleId Target;
		int32 Index;
		FString Previous;
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
	// Is the body visible to the player right now? (The renderer's answer; not a task body.)
	virtual bool IsBodyVisible() const = 0;
	// `TASK_WAIT_PVS`'s `RunTask` arm (`0x102aacf0`, task 5). True completes the task. A runner
	// with a Troika clock re-bases it on the way out; the default is the plain visibility answer
	// so a fixture without one keeps its old shape.
	virtual bool WaitPvs() { return IsBodyVisible(); }
	// `TASK_SET_ACTIVITY` -- request a named ACT_*. Returns its length, or negative when
	// unresolvable. Either answer starts the task: Troika RunTask decides completion by whether the
	// body's CURRENT base-channel clip identity reached the resolved IDEAL identity, with a one-second
	// watchdog when it did not.
	virtual float PlayActivity(const FString& Activity) = 0;
	// The current base-channel clip is the resolved ideal clip this task requested. False also covers
	// a resolver/play miss and a body whose channel is held by another producer; neither is a task
	// failure, and the kernel completes on its recovered watchdog instead.
	virtual bool IsIdealActivityCurrent() const { return false; }
	// `TASK_FACE_SAVEPOSITION` / `TASK_MOVE_AWAY_PATH` -- the door-obstruction motor verbs. Both
	// answer false where there is no motor, which fails the task rather than pretending it ran.
	virtual bool FaceSavePosition() { return false; }
	virtual bool StepAwayFromSavePosition(float DistanceCm) { return false; }
	// A uniform draw in [0, Max], from the NPC schedule stream.
	virtual float RandomSeconds(float Max) = 0;
	// One trace row, so a decision is readable without a rebuild.
	virtual void RecordScheduleEvent(const FString& Row) {}
	// Read-only observability hook. The gameplay owner may attach a Visual Logger event after the
	// schedule install; the default keeps engine-neutral test runners unchanged.
	virtual void DebugScheduleInstalled(EElysiumScheduleId) {}

	// The combat verbs.
	// Every one defaults to the answer a runner with no body can honestly give. The movement verbs
	// default to refusing, which fails their task by name. StopMoving is the immediate motor
	// command; the task's Start/Run pair below additionally reads the native traversal state.

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

	// `TASK_PLAY_DEATH_SEQUENCE`'s one rung — try ONE activity on the body and answer with its
	// authored length, or a negative value when this body's vocabulary does not carry it. The kernel
	// owns the ladder that calls this up to three times; the runner owns only "can this body play
	// that, and for how long".
	//
	// Deliberately not `PlayActivity`: a death pose has to REPLACE whatever owns the base channel and
	// hold it, where an idle-schedule activity is an ambient claim any locomotion publish outranks.
	virtual float PlayDeathActivity(const FString& Activity) { return -1.f; }

	// `TASK_GET_PATH_TO_GOAL` — issue the next leg of the scripted order this NPC was pushed, at the
	// order's own gait. False means no order, no body, an exhausted route or a body that would not
	// take the request; all four fail the task, and the runner names which.
	virtual bool GetPathToScriptedGoal() { return false; }

	// `TASK_MAKE_OBLIVIOUS` (0x131, `StartTask` arm `0x102a72e3`). The operand is a float the schedule
	// compiler writes from `TRUE`/`ON` -> 1.0 and `FALSE`/`OFF` -> 0.0 (`0x1030e65f`); all 35 shipped
	// operands are `TRUE`, so `bOblivious=false` is a path no registered program takes.
	virtual void MakeOblivious(bool bOblivious) {}

	// `TASK_SET_NPC_FLAG` (0x100, `StartTask` arm `0x102a585d`). One bit of the NPC flag word.
	virtual void SetNpcFlag(EElysiumNpcFlag Flag) {}

	// Discard every gathered condition, because a schedule was just installed.
	//
	// Retail's `CAI_BaseNPC::SetSchedule` (`0x10280e50`) zeroes all 192 condition bits before it
	// returns, so a forced program starts from a clean slate. This is the half of `DELAY_INTERRUPTS`
	// that is easy to miss: without it the flag would look nearly redundant, and with it the pair
	// produce retail's behaviour — a condition standing at the instant of install is destroyed, and
	// only a stimulus the NEXT pass re-observes can end the program.
	virtual void ClearConditions() {}
	virtual void TaskFail(int32 Reason) {}
	virtual void ScheduleDone() {}
	virtual int32 TaskFailureReason() const { return 0; }
	virtual void TaskStarting() {}
	virtual void SetGoalTolerance(float Units) {}
	virtual EElysiumTaskResult BeginStopMovingTask() { return StopMovingTask(); }
	virtual EElysiumTaskResult StopMovingTask() { StopMoving(); return EElysiumTaskResult::Complete; }

	// Retail's schedule-change virtual, slot 435 — `CAI_BaseNPCTroika::OnScheduleChange`
	// (`0x102a0940`), which `ForceScheduleChange` (`0x102ae490`) dispatches at the tail of every
	// `SetSchedule`. It clears ten bits of the NPC flag word (mask `&= 0xbbf4b97e`), and when
	// `MADE_OBLIVIOUS` was set it clears that too and decrements the oblivious refcount.
	//
	// This is why `SCHED_TROIKA_MESMERIZED` needs no teardown tasks and why the trance unwinds
	// completely: the NEXT schedule this NPC is given is what ends it. It is also why the mesmerize
	// tasks always write onto a cleared word — the same virtual ran when the program was installed.
	virtual void OnScheduleChange() {}

	// `CAI_BaseNPCTroika::BuildScheduleTestBits` (`0x102ad140`), the per-NPC interrupt overlay.
	//
	// The mask an NPC actually runs against is NOT the authored one. Every think,
	// `CacheInterruptConditions` (`0x1026a0f0`) copies the schedule's declared mask onto the NPC and
	// then lets this virtual add and remove conditions by NPC state, flags and enemy. The kernel
	// calls it on a copy of the authored mask immediately before the interrupt test, so a program
	// registered with its decoded mask stays decoded and the overlay is the runner's own recovered
	// rule. A runner with no NPC state leaves the mask alone.
	virtual void BuildScheduleTestBits(FElysiumNpcConditions& InOutMask) {}
};

// The per-NPC runner state. Saved as part of the NPC, so a schedule survives a save.
struct FElysiumScheduleState
{
	EElysiumScheduleId Current = EElysiumScheduleId::None;
	int32 TaskIndex = 0;
	// Set when a timed task starts; the substrate clock decides when it completes.
	double TaskEndsAt = 0.0;
	bool bTaskStarted = false;
	bool bTaskCompletedExternally = false; // TaskComplete(false), consumed by MaintainSchedule

	// What `TASK_SET_FAIL_SCHEDULE` and `TASK_SET_TOLERANCE_DISTANCE` wrote for THIS run of the
	// program. Both are per-run rather than per-program: the same schedule reached from two
	// selectors carries whatever its own tasks set, and `Start` resets them so a previous program's
	// tolerance can never leak into the next one's path request.
	EElysiumScheduleId FailScheduleOverride = EElysiumScheduleId::None;
	// Source units, and negative means "the task never ran, so the motor's own acceptance decides".
	float ToleranceUnits = -1.f;

	// `CAI_BaseNPC::m_bDidMaintainSchedule` (`+0x5bb8`) — the other half of `DELAY_INTERRUPTS`.
	//
	// False from the moment a schedule is installed until the end of the next maintenance pass that
	// runs it, which is the whole window a `DELAY_INTERRUPTS` program is immune in. Retail writes it
	// in exactly three places and this runtime matches all three: false at spawn (the default here),
	// false by every install (`ElysiumSchedule::Start`), true at the end of a pass that ran a task
	// (`ElysiumSchedule::Tick`).
	//
	// NOT serialized, and the default is the reason: a restore re-installs its program through
	// `Start`, so a loaded NPC gets exactly the one think of immunity a fresh install gives. Carrying
	// the saved value would only differ for a payload saved mid-window, and it is not a value the
	// player can observe.
	bool bDidMaintainSchedule = false;

	bool IsRunning() const { return Current != EElysiumScheduleId::None; }
	void Clear()
	{
		Current = EElysiumScheduleId::None;
		TaskIndex = 0;
		TaskEndsAt = 0.0;
		bTaskStarted = false;
		bTaskCompletedExternally = false;
		FailScheduleOverride = EElysiumScheduleId::None;
		ToleranceUnits = -1.f;
		bDidMaintainSchedule = false;
	}
};

namespace ElysiumSchedule
{
	// Begin Id after running the outgoing program's teardown. An unknown program reports
	// TaskFail(5) without installing it, leaving the current program available to failure routing.
	bool Start(FElysiumScheduleState& State, EElysiumScheduleId Id, IElysiumScheduleRunner& Runner);

	// Advance the running schedule by one think. Returns false once the schedule has ended
	// (completed, failed through to nothing, or been interrupted), which is the caller's signal to
	// select again.
	//
	// It proposes no cadence. Retail's `MaintainSchedule` never informs the think clocks -- the four
	// `Calc*` laws read distance, PVS, LOS, `SCHEDULE_CHANGED`, frenzy and `ShouldThinkFrequently`
	// and nothing else -- so a running task is simply re-polled on the next normal think.
	//
	// `Conditions` is this decision pass's gathered set, checked against the active schedule's
	// interrupt mask at the top of the tick and before any task work. Null means "no conditions
	// were gathered for this pass" -- a headless kernel test, or a think that ran with condition
	// gathering suppressed -- and skips the check entirely rather than testing an empty set.
	//
	// `bReduced` is `RunAI`'s own argument: the AI clock declining to think. It bounds task
	// completions at one instead of ten.
	bool Tick(FElysiumScheduleState& State, IElysiumScheduleRunner& Runner, double Now,
		const FElysiumNpcConditions* Conditions = nullptr, bool bReduced = false);

	/**
	 * The mask the NPC actually runs against this think: the installed program's authored
	 * `Interrupts` plus the runner's `BuildScheduleTestBits` overlay.
	 *
	 * This is retail's `CacheInterruptConditions` (`0x1026a0f0`) product, `m_ScheduleTestBits`
	 * `+0x5c74`. With no installed schedule there is no mask at all, and retail's two mask testers
	 * both answer false in that case rather than falling back to an authored default.
	 *
	 * Extracted from `Tick`, which consumes it, so the interrupt test and the condition sweeps that
	 * consult the same mask cannot drift apart.
	 */
	FElysiumNpcConditions EffectiveInterrupts(const FElysiumScheduleState& State,
		IElysiumScheduleRunner& Runner);

	/**
	 * `0x10269c70` -- "does the running program's mask list this condition", and NOTHING else.
	 *
	 * Three testers exist in retail and the differences are load-bearing:
	 *   - `HasCondition` (`0x10269aa0`) reads the condition set `+0x5c5c` only, and needs no
	 *     installed schedule.
	 *   - `HasInterruptCondition` (`0x10269d30`) needs an installed schedule and the bit in BOTH
	 *     the condition set and the mask.
	 *   - this one needs an installed schedule and the bit in the mask, and never reads the
	 *     condition set at all.
	 *
	 * The sound sweep `0x102b1cd0` uses this one for all three of its gates. Inside its six
	 * `HEAR_*` arms the distinction is invisible, because the arm has already tested `HasCondition`
	 * itself; for the `HEAR_FLANK_SOUND` and `SEE_SOUND_SOURCE` gates it is the whole rule -- they
	 * are pure "did the running program ask for this" tests, satisfied with the condition unset.
	 *
	 * Retail reads two mask words here, `+0x5c74` OR `+0x5c8c`, and they are NOT the same kind of
	 * thing. `CacheInterruptConditions` (`0x1026a0f0`) fills `+0x5c74` from `m_pSchedule[10..15]`
	 * (the normal interrupt mask, `CAI_Schedule+0x28`) and `+0x5c8c` from `m_pSchedule[0..5]`
	 * (`CAI_Schedule+0x00`, the INVERTED mask -- the parser `0x1030d850` routes a `!`-prefixed
	 * interrupt token there, and `IsScheduleValid` evaluates
	 * `(conds & +0x5c74) | (~conds & +0x5c8c)`). The per-NPC overlay is not `+0x5c8c`: it ORs into
	 * `+0x5c74` via `SetScheduleTestBits` (`0x10269eb0`).
	 *
	 * This runtime models the normal mask only, which is exact because no shipped schedule uses the
	 * inverted one (see the oracle's interrupt-conditions section). If that is ever modelled it must
	 * be a SEPARATE word: folding it in here would make `HasInterruptCondition` below honour `!COND`
	 * bits that retail's `0x10269d30` — which reads `+0x5c74` alone — deliberately ignores.
	 */
	bool MaskHasCondition(const FElysiumScheduleState& State, IElysiumScheduleRunner& Runner,
		EElysiumNpcCond Cond);

	/**
	 * `CAI_BaseNPC::HasInterruptCondition` (`0x10269d30`): an installed schedule, and the bit in
	 * both the gathered condition set and the effective mask. Retail's selectors mix this with
	 * plain `HasCondition` deliberately -- the driving stimulus is honoured only when the running
	 * program lists it, while refinements read the raw condition.
	 */
	bool HasInterruptCondition(const FElysiumScheduleState& State, IElysiumScheduleRunner& Runner,
		const FElysiumNpcConditions& Conditions, EElysiumNpcCond Cond);

	// `TASK_MOVE_AWAY_PATH`, whole.
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

	// Extrapolate `DistanceCm` directly away from `SavePosition` in the horizontal plane, project
	// that point onto the navigable surface through the motor, RE-TEST the projection against the
	// retreat rule, and issue the move.
	//
	// The re-test is the point of the task: projection answers "where can someone stand", not "is
	// this still away from the door", so a point pulled back through the doorway is a navigable
	// point that must fail the schedule rather than walk the NPC into the swing it was told to
	// leave. `OutDestination` receives the projected point on every result that got that far.
	ERetreat StepAwayFromSavePosition(IElysiumNpcMotor* Motor, const FVector& Origin,
		const FVector& SavePosition, float DistanceCm, FVector& OutDestination);
}
