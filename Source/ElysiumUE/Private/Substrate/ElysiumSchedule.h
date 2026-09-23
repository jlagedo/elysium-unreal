#pragma once

#include "CoreMinimal.h"

#include "Substrate/ElysiumNpcConditions.h"   // the interrupt mask a schedule declares
#include "Substrate/ElysiumNpcFlags.h"        // the flag word `TASK_SET_NPC_FLAG` names
#include "Substrate/ElysiumLocalIdSpace.h"    // the class space an id is translated through
#include "Substrate/ElysiumScheduleId.h"      // local vs global, and the two constants that tell them apart
#include "Substrate/ElysiumScheduleNumbers.h" // the local ids a C++ body still has to name
#include "Substrate/ElysiumScheduleText.h"    // `FElysiumScheduleProgram` / `FElysiumScheduleStep`
#include "Substrate/ElysiumTaskOps.h"         // the opcode set the task bodies below implement

class IElysiumNpcMotor;   // the reachability query `TASK_MOVE_AWAY_PATH` asks the world

// VtMB's schedule/task machinery, as much of it as we have recovered producers for.
//
// A schedule is an ordered task program. The NPC runs one task at a time across thinks; a task
// answers Running, Complete or Failed, a completed task advances the index, and a failed task ends
// the schedule through its fail schedule. That is the whole kernel -- the behaviour lives in the
// task bodies, and the bodies live on the entity, reached through `IElysiumScheduleRunner` so this
// file stays free of the world, the engine and the clip vocabulary.
//
// Recovered facts: `docs/vtmb/npc-ai/conditions-and-states.md` -> "The idle branch, decided" for the
// state-1 selection order, and "Door-obstruction schedule selection" for step 6.
//
// The PROGRAMS are not here and are not typed here. They are VtMB's own 691 schedule texts,
// compiled at load by `FElysiumScheduleCorpus`; this file is only the interpreter that runs them.
// A task identity this runtime has no body for fails by name and is counted, which is the honest
// answer to an open task vocabulary and the reason the coverage meter can exist at all.

// Identity lives in the corpus, not in this header.
//
// This file used to open with two enums. `EElysiumTask` enumerated 23 task identities and
// `int32` 29 schedule ones, and both were the same mistake in two sizes: a closed
// C++ set standing where VtMB has an open table of authored names. The corpus registers 514 task
// identities and 695 schedule names, and a program this runtime has never heard of is not a thing
// the port gets to be unable to spell.
//
// So both are gone, and what replaces them is retail's own answer:
//
//   - a SCHEDULE is an `int32`. Below `ElysiumScheduleId::GlobalBase` it is a class-LOCAL number,
//     the number a selector, a task operand or a script names; at or above it, the GLOBAL id the
//     owning space translated it to, which is what `FElysiumScheduleManager` keys on and what
//     `FElysiumScheduleState::Current` holds. `ElysiumScheduleNumbers.h` carries the handful of
//     local numbers a C++ body has to name, each checked against the corpus at load.
//   - a TASK is a global task id in `FElysiumScheduleStep::TaskId`, and `EElysiumTaskOp`
//     (`Substrate/ElysiumTaskOps.h`) is the separate, small set of identities this runtime has a
//     BODY for. An id with no op is not an error; it is the coverage meter's row.
//
// The programs themselves are `FElysiumScheduleProgram` (`Substrate/ElysiumScheduleText.h`),
// compiled from retail's own schedule texts by `FElysiumScheduleCorpus`.

/** The program a GLOBAL schedule id names, or null. `FElysiumScheduleManager::FindById` over the
 *  loaded corpus, which is the only registry this runtime has. */
const FElysiumScheduleProgram* ElysiumScheduleFor(int32 GlobalId);

/** The GLOBAL id a class-LOCAL retail number names, in the corpus's `CAI_BaseNPCTroika` space --
 *  the space `IElysiumScheduleRunner`'s own default translates through, and the one every class
 *  with no slot-580 body of its own runs. `INDEX_NONE` when no space in the chain holds it.
 *
 *  A body that has an NPC asks the NPC (`IdSpace`); this is for the bodies that do not. */
int32 ElysiumScheduleGlobalId(int32 LocalId);

/** The authored name of a global schedule id, or `SCHED_?`. Retail's `CAI_Schedule+0x40`. */
const TCHAR* ElysiumScheduleName(int32 GlobalId);

/** `name (global/local)` for a trace row. The local half is the number the oracle and the
 *  retail-side prose use, so a row reads like the binary's; it is `(?)` where the runner carries no
 *  space that holds the id. */
FString ElysiumScheduleLabel(int32 GlobalId, const class IElysiumScheduleRunner* Runner = nullptr);

/** How a step's single data word reads, given the op that consumes it -- the activity name, the
 *  schedule the transfer names, the raw flag word, or the float. One body, because the two debug
 *  surfaces that print a task program must not each decide what the word means. */
FString ElysiumTaskOperandLabel(const FElysiumScheduleStep& Step);

const TCHAR* ElysiumTaskFailureName(int32 Reason);

#if WITH_DEV_AUTOMATION_TESTS
namespace ElysiumSchedule
{
	// Test-only: borrow a LOADED program's interrupt mask for the lifetime of the scope, restoring
	// the authored one on destruction. `LocalId` is the class-local number, resolved through the
	// same fallback space `ElysiumSchedule::Start` uses.
	//
	// It exists so a kernel case can drive the interrupt path against a mask of its own choosing
	// rather than against whatever the loaded program declares: a case that wants to prove "this
	// condition, and only this condition, ends the program" needs a mask it controls. Nothing
	// outside a test may install one -- a compiled mask is authored data, and writing one at
	// runtime is a behavioural change.
	struct FInterruptMaskScope
	{
		FInterruptMaskScope(int32 LocalId, const FElysiumNpcConditions& Mask);
		~FInterruptMaskScope();

		FInterruptMaskScope(const FInterruptMaskScope&) = delete;
		FInterruptMaskScope& operator=(const FInterruptMaskScope&) = delete;

	private:
		int32 Target = ElysiumScheduleId::None;
		FElysiumNpcConditions Previous;
		bool bInstalled = false;
	};

	// Test-only: borrow one task's data word, same reason and same rule.
	//
	// `TASK_PLAY_DEATH_SEQUENCE` takes an activity argument and its first ladder rung is that
	// argument; a case that wants to drive that rung needs an operand it controls.
	struct FTaskActivityScope
	{
		FTaskActivityScope(int32 LocalId, int32 TaskIndex, const FString& Activity);
		~FTaskActivityScope();

		FTaskActivityScope(const FTaskActivityScope&) = delete;
		FTaskActivityScope& operator=(const FTaskActivityScope&) = delete;

		bool IsInstalled() const { return bInstalled; }

	private:
		int32 Target = ElysiumScheduleId::None;
		int32 Index = 0;
		float Previous = 0.f;
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

// `AIScheduleState_t::fTaskStatus` (`+0x5c44`). Retail carries five values, and
// `TaskMovementComplete` distinguishes all four non-complete values. The old pair of booleans
// could not represent status 2 versus 3 and therefore could not host that body.
enum class EElysiumTaskStatus : int32
{
	New = 0,
	Running = 1,
	RunningMovement = 2,
	RunningTask = 3,
	Complete = 4,
};

struct FElysiumScheduleState;

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
	// `RandomFloat(0.1, Max)` from the NPC schedule stream: `0.1 + (Max - 0.1) * frac`, the
	// `TASK_WAIT_RANDOM` arm's draw (`0x10283dae`). Not clamped; a `Max` below 0.1 draws in `[Max, 0.1]`.
	virtual float RandomSeconds(float Max) = 0;
	// One trace row, so a decision is readable without a rebuild.
	virtual void RecordScheduleEvent(const FString& Row) {}
	// Read-only observability hook. The gameplay owner may attach a Visual Logger event after the
	// schedule install; the default keeps engine-neutral test runners unchanged.
	virtual void DebugScheduleInstalled(int32 GlobalScheduleId) { (void)GlobalScheduleId; }

	// --- Slot 580's two translations, which is how an id reaches a program -----------------------
	//
	// `GetScheduleOfType` (`0x102cc260`) begins every `SetSchedule(int)` with
	// `if (id < 0x3b9aca00 || id == -1) id = ScheduleLocalToGlobal(GetClassScheduleIdSpace(), id)`,
	// and slot 447 (`0x101a6620`) is the inverse. A runner that carries no class of its own runs the
	// corpus's `CAI_BaseNPCTroika` space, which is the same fallback `ClassScheduleIdSpace()` takes
	// for a class with no slot-580 body of its own -- so a plain test runner still reaches every
	// base and Troika program by its retail number.
	virtual int32 ResolveScheduleId(int32 Id) const;
	virtual int32 LocalScheduleId(int32 GlobalId) const;
	virtual const FElysiumLocalIdSpace* ConditionIdSpace() const;

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
	// `TASK_REMEMBER` (`0x10288e1e`). The operand is a `Memory:` word -- one of the seventeen
	// resolved prefixes, and one of the fourteen that store a signed-converted float rather than a
	// raw one. SEAM, traced and otherwise inert: nothing in this runtime reads the memory word yet.
	virtual void RememberFact(uint32 MemoryMask) { (void)MemoryMask; }

	// `TASK_FIND_COVER_FROM_ENEMY`. New with the corpus -- the witness text
	// `SCHED_TROIKA_CHASE_ENEMY_FAILED` is the fifth of its twelve tasks and the port carried no
	// body for it, because it carried no program that named it. Refusing fails the task by name.
	virtual bool FindCoverFromEnemy(float MoveWait) { return false; }

	// `TASK_PLAY_DEATH_SEQUENCE`'s one rung — try ONE activity on the body and answer with its
	// authored length, or a negative value when this body's vocabulary does not carry it. The kernel
	// owns the ladder that calls this up to three times; the runner owns only "can this body play
	// that, and for how long".
	//
	// Deliberately not `PlayActivity`: a death pose has to REPLACE whatever owns the base channel and
	// hold it, where an idle-schedule activity is an ambient claim any locomotion publish outranks.
	virtual float PlayDeathActivity(const FString& Activity) { return -1.f; }

	// `TASK_SOUND_DIE`'s whole body -- the death-sound hook, vtable slot 488. The base body
	// (`0x101a6880`) is EMPTY; Troika's (`0x10293ec0`) walks the vdata sound table once for the entry
	// named "Death", caches its index in `DAT_10924d64` under the one-shot guard `DAT_10923f0d`, and
	// plays it as sound type 2 at volume 1.0 and pitch 1.25.
	//
	// Retail fires this same hook from `Event_Killed` (`0x10265cb8`) under no life-state guard, so a
	// single death plays it more than once. That is retail's behaviour, not a defect to smooth here.
	virtual void DeathSound() {}

	// `TASK_DIE`'s start half (`0x10286801`): clear the navigator goal through `0x102ee270` and write
	// `m_lifeState = 1`. The arm does NOT complete the task -- it falls off the dispatch without
	// touching `TaskComplete`, which is what leaves the program parked on this task.
	virtual void BeginDying() {}

	// `TASK_DIE`'s Troika `RunTask` gate (`0x102abb90`):
	//   (IsActivityFinished() [slot 251] && m_flCycle >= 1.0) || m_IdealActivity == ACT_IDLE
	// The second arm is load-bearing: a body with no death performance running is already idle, so it
	// commits on the first think instead of waiting for a clip that will never play.
	virtual bool IsDeathPerformanceFinished() const { return true; }

	// `TASK_DIE`'s commit. Retail sets the damage target to the NPC ITSELF, writes `m_lifeState` 1 ->
	// 0, and calls `CBaseCombatCharacter::Die` (`0x103392c0`, reached only through thunk
	// `0x100034f9` -- which is why the decompiler reports it with no callers at all).
	//
	// `Die` guards on `m_lifeState != 2`, builds a synthetic 1.0-damage packet sourced from the NPC,
	// and dispatches `Event_Killed` (slot 144) and `Event_Dying` (slot 403). So the commit RE-ENTERS
	// the kill path; it is the second `Event_Killed` whose `CreateCorpse` makes the corpse.
	virtual void CommitDeath() {}

	// `TASK_GET_PATH_TO_GOAL` — issue the next leg of the scripted order this NPC was pushed, at the
	// order's own gait. False means no order, no body, an exhausted route or a body that would not
	// take the request; all four fail the task, and the runner names which.
	virtual void RunPatrolPathTask() {}

	// `TASK_MAKE_OBLIVIOUS` (0x131, `StartTask` arm `0x102a72e3`). The operand is a float the schedule
	// compiler writes from `TRUE`/`ON` -> 1.0 and `FALSE`/`OFF` -> 0.0 (`0x1030e65f`); all 35 shipped
	// operands are `TRUE`, so `bOblivious=false` is a path no registered program takes.
	virtual void MakeOblivious(bool bOblivious) {}

	// `TASK_SET_NPC_FLAG` (0x100, `StartTask` arm `0x102a585d`). One bit of the NPC flag word.
	virtual void SetNpcFlag(uint32 EncodedFlag) {}

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
	virtual void OnScheduleChange(int32 NewGlobalScheduleId) { (void)NewGlobalScheduleId; }
	// `gpGlobals->curtime`, used by base SetSchedule to stamp both schedule clocks. A plain runner
	// has no world clock and therefore starts at retail's zero-initialised value.
	virtual double ScheduleTime() const { return 0.0; }

	// The non-task arms of `MaintainSchedule` (`0x102817c0`). Defaults preserve the engine-neutral
	// runner's answer; `FElysiumNpc` supplies the retail state, door, selector and animation words.
	virtual bool IsSpecialNavigation() const { return false; }
	virtual void MarkSpecialNavigationScheduleEnd() {}
	virtual bool ConsumeChooseNewSchedule() { return false; }
	virtual bool ScheduleStateDiffersFromIdeal() const { return false; }
	virtual bool HasMaintenanceCondition(EElysiumNpcCond Cond) const
	{
		(void)Cond;
		return false;
	}
	virtual void PrepareScheduleReselect() {}
	virtual bool ConsumeBlockedDoorForSchedule(double Now) { (void)Now; return false; }
	virtual void CommitIdealStateForSchedule() {}
	/** The selector's answer, as the LOCAL retail number it names -- which is what every selector
	 *  in this port already computes and what `SetSchedule(int)` takes. The kernel translates. */
	virtual int32 SelectScheduleForMaintenance(double Now, int32& OutIdealScheduleRetail)
	{
		(void)Now;
		OutIdealScheduleRetail = 0;
		return ElysiumScheduleId::None;
	}
	virtual void SetIdealScheduleForMaintenance(int32 RetailId) { (void)RetailId; }
	virtual void MissingSchedule() {}
	virtual void MaintainActivity() {}
	virtual int32 LocalScheduleIdForStart(int32 GlobalId) { return LocalScheduleId(GlobalId); }
	virtual void MaintenanceOnStartSchedule(int32 LocalScheduleId) { (void)LocalScheduleId; }
	virtual void DebugTaskStart(const FElysiumScheduleStep& Step) { (void)Step; }
	virtual void MaintenanceStartTaskOverlay() {}
	virtual bool MaintenanceIsCurTaskContinuousMove() { return false; }
	virtual void RememberContinuousMove() {}
	virtual void RunTaskOverlay() {}
	virtual bool IsAiStepMode() const { return false; }
	virtual void AdvanceAiStepDebugIndex() {}
	virtual void FreezeForAiStep() {}
	virtual void NextScheduledTaskForMaintenance(FElysiumScheduleState& State);
	// This port represents patrol, ambient use and pushed scripted orders as executors outside the
	// retail schedule table. Their existing handoff consumes the completed program at ScheduleDone.
	virtual bool TakeExternalExecutorReturn() { return false; }

	// `ClearSchedule` (`0x10280d30`) asked for by a body the kernel is running. Retail executes it at
	// the call site; here the body asks and the kernel honours the request at the next point it
	// polls: after the task step that raised it, or at the top of the next `Tick` for a request
	// raised outside one. `Start` discards a pending request, as retail's `ClearSchedule` then
	// `SetSchedule` leaves the new program installed. Consumed by the answer.
	//
	// Twelve direct callers in retail (`thunk 0x10006a8c`); the task-body ones are the scripted
	// family's (0003) and `CAI_BaseNPC::RunTask` `0x10288780`. The non-task callers — the
	// save-position clear `0x102ae8e0`, base slot 420 `0x10273390`, Troika slot 379 `0x102b5c00`,
	// cine slot 586, `CNPC_VCamera` `0x103692c0` — are story 25a's.
	virtual bool TakeClearScheduleRequest() { return false; }
	// `ClearSchedule`'s `PRESERVE_PATH` clear on the Troika pointer, ahead of its slot-435 dispatch.
	virtual void ClearPreservePath() {}

	// Slot 440 `TranslateSchedule` (`CAI_BaseNPCTroika` `0x102b12f0`, base `0x102cc080`): the first
	// step of `SetSchedule(int)` (`0x102cc1f0`), run on every id that reaches it as a NUMBER — the
	// fail route's answer, `TASK_SET_SCHEDULE` / `TASK_SET_FAIL_SCHEDULE` operands, a script's
	// `ChangeSchedule`. The kernel applies it on the fail route; the selectors' answers are already
	// translated Troika ids and never pass through it. Identity for a runner with no class table.
	// The miss arm's literal 1 (`0x102cc229`) does NOT go through it.
	virtual int32 TranslateSchedule(int32 LocalId) { return LocalId; }

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
	/** The GLOBAL id of the installed program -- retail's `m_pSchedule` (`+0x5c38`) by identity
	 *  rather than by pointer. `ElysiumScheduleFor` turns it back into the program. */
	int32 Current = ElysiumScheduleId::None;
	int32 TaskIndex = 0;
	// Set when a timed task starts; the substrate clock decides when it completes.
	double TaskEndsAt = 0.0;
	// `m_ScheduleState.timeStarted +0x5c48` and `timeCurTaskStarted +0x5c4c`. The latter is read by
	// both RunTask spines, so these are distinct retail state rather than derived diagnostics.
	double ScheduleStartedAt = 0.0;
	double TaskStartedAt = 0.0;
	EElysiumTaskStatus TaskStatus = EElysiumTaskStatus::New;

	// What `TASK_SET_FAIL_SCHEDULE` and `TASK_SET_TOLERANCE_DISTANCE` wrote for THIS run of the
	// program. Both are per-run rather than per-program: the same schedule reached from two
	// selectors carries whatever its own tasks set, and `Start` resets them so a previous program's
	// tolerance can never leak into the next one's path request.
	//
	// `m_failSchedule` (`+0x5c54`) holds the operand as authored, which is a class-LOCAL number:
	// `GetFailSchedule` answers it raw and `SetSchedule(int)` is what translates. So this field is
	// LOCAL where `Current` above is GLOBAL, and that asymmetry is retail's, not a slip.
	int32 FailScheduleOverride = ElysiumScheduleId::None;
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

	bool IsRunning() const { return Current != ElysiumScheduleId::None; }
	void Clear()
	{
		Current = ElysiumScheduleId::None;
		TaskIndex = 0;
		TaskEndsAt = 0.0;
		ScheduleStartedAt = 0.0;
		TaskStartedAt = 0.0;
		TaskStatus = EElysiumTaskStatus::New;
		FailScheduleOverride = ElysiumScheduleId::None;
		ToleranceUnits = -1.f;
		bDidMaintainSchedule = false;
	}
};

namespace ElysiumSchedule
{
	// `CAI_BaseNPC::SetSchedule(CAI_Schedule*)` (`0x10280e50`): install a schedule pointer that has
	// already passed the id lookup. `None` is a null pointer and is installed as null; unlike
	// `Start`, it does not take `GetScheduleOfType`'s IDLE_STAND miss arm.
	void Install(FElysiumScheduleState& State, int32 GlobalId,
		IElysiumScheduleRunner& Runner);

	// `SetSchedule(int)` (`0x102cc1f0`): begin Id after running the outgoing program's teardown. A
	// program this runtime does not carry is `GetScheduleOfType`'s miss — a trace row
	// (`"GetScheduleOfType(): No CASE for %d"`) and base `IDLE_STAND` installed in its place. False
	// only if `IDLE_STAND` itself is missing, which is a build defect.
	bool Start(FElysiumScheduleState& State, int32 Id, IElysiumScheduleRunner& Runner);

	// `ClearSchedule` (`0x10280d30`): zero the six schedule words `+0x5c38..+0x5c4c` (program, schedule
	// id, task index, status, both stamps), clear `PRESERVE_PATH`, dispatch slot 435 with no program.
	// `m_failSchedule` (`+0x5c54`) is NOT among them and survives; no condition clear, no install; the
	// NPC selects on its next pass.
	void ClearSchedule(FElysiumScheduleState& State, IElysiumScheduleRunner& Runner);

	// Advance the running schedule by one think. Returns false once the schedule has ended
	// (completed, interrupted, or cleared by `ClearSchedule`), which is the caller's signal to select
	// again. A failure never ends it. A task that fails inside the loop leaves its program installed
	// and the pass ends (`MaintainSchedule`'s `HasCondition(TASK_FAILED)` exit to `0x102821ae`); the
	// NEXT tick's `Conditions` carry `TASK_FAILED` and its top arm installs the failure route and
	// keeps running on it in the same pass.
	//
	// It proposes no cadence. Retail's `MaintainSchedule` never informs the think clocks -- the four
	// `Calc*` laws read distance, PVS, LOS, `SCHEDULE_CHANGED`, frenzy and `ShouldThinkFrequently`
	// and nothing else -- so a running task is simply re-polled on the next normal think.
	//
	// `Conditions` is this decision pass's gathered set, checked against the active schedule's
	// interrupt mask at the top of the tick and before any task work. Null means "no conditions
	// were gathered for this pass" -- a headless kernel test, or a think that ran with condition
	// gathering suppressed -- and skips the check entirely rather than testing an empty set. A
	// runner whose `TaskFail` sets `TASK_FAILED` must pass its set on the tick after a failure, or
	// the failed program is re-run as retail would re-run it with the condition cleared.
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
