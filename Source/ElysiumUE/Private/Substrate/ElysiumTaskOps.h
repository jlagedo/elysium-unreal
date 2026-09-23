// The interpreter's opcode set, and the map from a corpus task identity onto it.
//
// This is the change the schedule seam forces on the port's task type, and it is worth stating
// plainly because the old type looked like this one. `EElysiumTask` was an IDENTITY set: 23
// enumerators that were simultaneously "the tasks retail has" and "the tasks this runtime runs",
// and a task outside it could not be represented at all. The corpus has 441 task identities. They
// are content, they arrive as names in a global namespace, and they are not the port's to enumerate.
//
// So identity and implementation split. A task's identity is its global task id, minted by the
// registration pass; `EElysiumTaskOp` is the separate, small set of identities this runtime has a
// body for, and `FElysiumTaskOpTable` binds one to the other by name at load. An id with no op
// answers `Unknown` -- which is not an error, it is the measurement: it is exactly the list of
// tasks the port has yet to build, with the reference count that says which to build first.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Map.h"

struct FElysiumIdNamespace;
class FElysiumScheduleManager;

/**
 * The task bodies this runtime carries.
 *
 * Every enumerator but `Unknown` names a retail task the runner implements, under retail's own
 * spelling (`ElysiumTaskOpName`). Adding one is adding a body, not adding a name.
 */
enum class EElysiumTaskOp : uint8
{
	/** No body: the corpus named a task this runtime does not run. The runner fails the step by
	 *  name and the coverage meter counts it. */
	Unknown = 0,

	// The idle and scripted vocabulary.
	SpecialIdleActivity,
	WaitPvs,
	SetActivity,
	Wait,
	WaitRandom,
	FaceSavePosition,
	/** `TASK_MOVE_AWAY_PATH`. A registered task identity that **no shipped text names** -- the
	 *  door family this body was written for actually runs `TASK_STEP_BACK`, which this runtime has
	 *  no body for. The op stays bound to the task it implements rather than being re-pointed at a
	 *  different one: `TASK_STEP_BACK`'s operand is `0.0` where this reads a distance, so what it
	 *  does with that operand is a recovery this port has not made. The coverage meter carries it. */
	MoveAwayFromSavePosition,

	// The combat vocabulary.
	SetFailSchedule,
	StopMoving,
	/** `TASK_FIND_COVER_FROM_ENEMY`. New with the corpus: the witness text
	 *  `SCHED_TROIKA_CHASE_ENEMY_FAILED` is the fifth of its twelve tasks, and the port had no
	 *  body for it because it had no program that named it. The runner verb's engine-neutral
	 *  default refuses, so the task fails by name rather than silently completing. */
	FindCoverFromEnemy,
	SetToleranceDistance,
	GetPathToEnemy,
	RunPath,
	WaitForMovement,
	FaceEnemy,
	AnnounceAttack,
	MeleeAttack1,
	RangeAttack1,
	SetSchedule,
	Remember,
	MakeOblivious,
	SetNpcFlag,
	PlayDeathSequence,
	/** `TASK_SOUND_DIE` (`0x49`), the second task of base `DIE` and of `SCHED_DIE_RAGDOLL`. Troika
	 *  forwards it to the base through its default dispatch arm (`0x102a77e2`); the base arm is
	 *  `0x10286843` and is two calls long -- the death-sound hook, vtable slot 488, and then
	 *  `TaskComplete`. */
	SoundDie,
	/** `TASK_DIE` (`0x5f`), the third and last task of base `DIE`. Unlike every other op here it
	 *  spans both halves of the dispatch: the base `StartTask` arm `0x10286801` (shared with
	 *  `TASK_DIE_IMMEDIATE` `0xdf`) clears the navigator goal and writes `m_lifeState = 1` without
	 *  completing, and Troika's `RunTask` arm `0x102abb90` (shared with `TASK_DIE_GIB` `0xe9` and
	 *  `TASK_DIE_DUE_TO_PLAYER` `0xeb`) waits, then commits -- and never completes either. */
	Die,
	PatrolPath,
};

namespace ElysiumTaskOps
{
	/** Retail's spelling of an op, `TASK_*`. `Unknown` answers `TASK_?`. */
	const TCHAR* Name(EElysiumTaskOp Op);

	/** The op a retail task name binds to, or `Unknown`. Case-insensitive. */
	EElysiumTaskOp FromName(const FString& TaskName);

	/** Every op this runtime carries, `Unknown` excluded. */
	TConstArrayView<EElysiumTaskOp> All();
}

/**
 * Global task id -> op, plus what the binding measured.
 *
 * Built once per corpus load from the task namespace: every registered name is looked up in the
 * op table, and the ones that bind become rows. The rest are the unported set.
 */
class FElysiumTaskOpTable
{
public:
	/** Bind every name in `Tasks`. Replaces any previous binding. */
	void Build(const FElysiumIdNamespace& Tasks);

	/** The op for a global task id, or `Unknown` -- including for an id this table never saw. */
	EElysiumTaskOp Find(int32 GlobalTaskId) const;

	/** The retail name a global task id was registered under, or an empty string. */
	const FString& NameOf(int32 GlobalTaskId) const;

	/** How many of the corpus's task identities have a body here. */
	int32 NumPorted() const { return Ops.Num(); }

	/** How many do not. `NumPorted() + NumUnported()` is the namespace's size. */
	int32 NumUnported() const { return Unported.Num(); }

	/** The unported identities, by name, each with how many STEPS in the corpus reach it.
	 *
	 *  The reference count is what makes this a work queue rather than an inventory: an identity
	 *  reached by 300 steps and one reached by 1 are not the same size of hole. It is filled by
	 *  `Measure`, and is zero everywhere until then. */
	const TMap<FString, int32>& UnportedByName() const { return Unported; }

	/** Walk every step of every program and count the references to each unported identity.
	 *  Answers the number of steps that named one. */
	int32 Measure(const FElysiumScheduleManager& Manager);

	void Reset();

private:
	TMap<int32, EElysiumTaskOp> Ops;
	TMap<int32, FString> Names;
	TMap<FString, int32> Unported;

	/** Global id -> the unported name's key, so `Measure` does not re-fold a string per step. */
	TMap<int32, FString> UnportedById;
};
