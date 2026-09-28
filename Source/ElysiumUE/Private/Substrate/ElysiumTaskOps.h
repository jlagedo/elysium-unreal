// The task-arm census: which of the corpus's task identities has a body in this runtime.
//
// A task identity is its global id, minted by the registration pass; the corpus carries 441 of them
// and they are content, not the port's to enumerate. Since story 8 wave 2 (lane L13) the schedule
// interpreter runs every task through the retail dispatch -- slot 442 `StartTask` and slot 444
// `RunTask` on the body being run -- so "this runtime has a body for task T" means "some class's
// 442/444 switch carries an arm for T". The census is those arms: one row per (retail class,
// class-LOCAL task id, slot), bound at corpus load through that class's task space (slot 450's
// inverse, `LocalToGlobal`). An identity no arm names is the unported set, and the per-step
// reference count `Measure` fills is the work queue. (The port's old `EElysiumTaskOp` opcode set
// -- 26 hand-written bodies under the interpreter -- was deleted with its twin switch.)
//
// The rows are generated from the slot bodies' top-level task switches (and the species bodies'
// single-arm `==` tests) by `pass-i/L13-scratch/task_arms.py`; regenerate after a StartTask/RunTask
// body gains or loses an arm.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Templates/Function.h"

struct FElysiumIdNamespace;
struct FElysiumLocalIdSpace;
class FElysiumScheduleManager;

/** One arm of one class's `StartTask` (442) or `RunTask` (444) switch, by retail class and the
 *  class-LOCAL task id the switch compares. */
struct FElysiumTaskArmRow
{
	const TCHAR* RetailClass;
	int32 LocalTaskId;
	int32 Slot;
};

namespace ElysiumTaskArms
{
	/** Every arm the slot bodies carry. */
	TConstArrayView<FElysiumTaskArmRow> All();
}

/**
 * Global task id -> "has an arm", plus what the binding measured.
 *
 * Built once per corpus load from the task namespace and the class task spaces: every arm row's
 * local id is translated to its global id; every registered name whose id no row reached is an
 * unported identity.
 */
class FElysiumTaskOpTable
{
public:
	/** The task space a retail class runs (its own, else the Troika line's, as slot 580 falls). */
	using FSpaceForClass = TFunctionRef<const FElysiumLocalIdSpace*(const TCHAR* RetailClass)>;

	/** Bind every name in `Tasks` against the arm census. Replaces any previous binding. */
	void Build(const FElysiumIdNamespace& Tasks, FSpaceForClass SpaceForClass);

	/** Bind every name in `Tasks` against an explicit armed set of GLOBAL ids (a test's census). */
	void BuildFromArmed(const FElysiumIdNamespace& Tasks, const TSet<int32>& ArmedGlobalIds);

	/** Whether some class's slot 442/444 switch carries an arm for this global task id. */
	bool HasArm(int32 GlobalTaskId) const { return Armed.Contains(GlobalTaskId); }

	/** The retail name a global task id was registered under, or an empty string. */
	const FString& NameOf(int32 GlobalTaskId) const;

	/** How many of the corpus's task identities have an arm. */
	int32 NumPorted() const { return Armed.Num(); }

	/** How many do not. `NumPorted() + NumUnported()` is the namespace's size. */
	int32 NumUnported() const { return Unported.Num(); }

	/** The unported identities, by name, each with how many STEPS in the corpus reach it. Filled by
	 *  `Measure`, zero everywhere until then. */
	const TMap<FString, int32>& UnportedByName() const { return Unported; }

	/** Walk every step of every program and count the references to each unported identity.
	 *  Answers the number of steps that named one. */
	int32 Measure(const FElysiumScheduleManager& Manager);

	void Reset();

private:
	TSet<int32> Armed;
	TMap<int32, FString> Names;
	TMap<FString, int32> Unported;

	/** Global id -> the unported name's key, so `Measure` does not re-fold a string per step. */
	TMap<int32, FString> UnportedById;
};
