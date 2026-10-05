#pragma once

#include "CoreMinimal.h"

#include "Substrate/ElysiumNpcConditions.h"   // the interrupt mask a schedule declares
#include "ElysiumNpcFlags.h"        // the flag word `TASK_SET_NPC_FLAG` names
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

// What a task body needs from whoever owns the body. Implemented by the NPC; the recording double
// in the tests implements it too, which is what makes a whole task program assertable with no
// engine.
class IElysiumScheduleRunner
{
public:
	virtual ~IElysiumScheduleRunner() = default;

	// Slot 442 `StartTask(pTask)` and slot 444 `RunTask(pTask)` on the body being run -- the retail
	// dispatch `MaintainSchedule` (`0x102817c0`) makes at `0x10281e10` / `0x1028202c`. The task body
	// writes the task's status itself (`TaskComplete`, slot 448 `TaskFail`, `SetSchedule`). A runner
	// with no body runs no task: the step stays running. `State` is the program state being run --
	// the body's own `Schedule` on an NPC -- and `Now` the kernel's clock; an NPC's bodies read the
	// world's `curtime` themselves, a test runner scripts its status from both.
	// (Story 8 wave 2: these replace the port's `EElysiumTaskOp` switch and its per-op verbs.)
	virtual void StartTaskForMaintenance(FElysiumScheduleState& State, const FElysiumScheduleStep& Step,
		double Now)
	{
		(void)State;
		(void)Step;
		(void)Now;
	}
	virtual void RunTaskForMaintenance(FElysiumScheduleState& State, const FElysiumScheduleStep& Step,
		double Now)
	{
		(void)State;
		(void)Step;
		(void)Now;
	}
	// One trace row, so a decision is readable without a rebuild.
	virtual void RecordScheduleEvent(const FString& Row) {}
	// Read-only observability hook. The gameplay owner may attach a Visual Logger event after the
	// schedule install; the default keeps engine-neutral test runners unchanged.
	virtual void DebugScheduleInstalled(int32 GlobalScheduleId) { (void)GlobalScheduleId; }
	// `IsScheduleValid` (`0x10280ff0`) found an interrupt: the fired bits of the normal mask and of
	// the inverted mask, both in GLOBAL ordinals, for its `npc_task_text` "Break condition" print.
	virtual void DebugScheduleBreak(const FElysiumNpcConditions& Firing,
		const FElysiumNpcConditions& InvertedFiring)
	{
		(void)Firing;
		(void)InvertedFiring;
	}

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
	virtual void TaskStarting() {}

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
struct FElysiumSaveArchive; // 0x1027bc60
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
	// 0x1027bc60: BOOL SAVE; restore does not install a fresh program.
	bool bDidMaintainSchedule = false;

	void Serialize(FElysiumSaveArchive& Ar); // 0x1027bc60 AIScheduleState_t +0x5c40
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
	 * `Interrupts`, virtual slot 453's overlay, empty slot 411, then unconditional NPC_FREEZE.
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
	 * This accessor and HasInterruptCondition use the positive mask only. Authored inverse
	 * interrupts are stored separately and IsScheduleValid evaluates their absence.
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

}
