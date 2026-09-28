// `CAI_BaseNPC`'s bodies of the `Maintain19` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseMaintain19.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumStub.h"
#include "Substrate/ElysiumDisciplines.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcLog.h"

// --- Moved from `ElysiumNpcMaintain19.cpp` (story 5 step 5) ---

void FElysiumNpcBase::TaskMovementComplete()
{
	BaseScheduleHost.bShouldMove = false; // 0x10273ec9
	bMoveIssued = false;
	switch (Schedule.TaskStatus)
	{
		case EElysiumTaskStatus::New:
		case EElysiumTaskStatus::Running:
			Schedule.TaskStatus = EElysiumTaskStatus::RunningTask; // 0x10273edc
			break;
		case EElysiumTaskStatus::RunningMovement:
			TaskComplete(false); // 0x10273eec
			break;
		case EElysiumTaskStatus::RunningTask:
			UE_LOG(LogElysiumNpcEnt, Warning, TEXT("Movement completed twice!")); // 0x10273ef3
			break;
		case EElysiumTaskStatus::Complete:
			break;
	}
	if (!IsScriptDriven()) // 0x10273f01..0x10273f14
	{
		SetIdealActivity(ResolveLinkActivity()); // 0x10273f20
	}
	if (NavIsGoalActive()) // 0x10273f2b
	{
		NavStopMoving(); // 0x10273f3a
	}
	if (Motor != nullptr)
	{
		Motor->ClearNavigationGoal(); // 0x10273f46
	}
}

bool FElysiumNpcBase::MaintainSchedule(double Now, bool bReduced)
{
	// `0x102817c0`, as `RunAI` (`0x1026f302`) calls it. A Troika body goes through the port's owner
	// routing first (`FElysiumNpc::RouteScheduleMaintenance`, its STORY8-TWIN survivors named there),
	// which reaches the interpreter below for every body the schedule owns.
	if (FElysiumNpc* const Troika = AsNpc())
	{
		return Troika->RouteScheduleMaintenance(Now, bReduced);
	}
	return MaintainScheduleRetail(Now, bReduced);
}

void FElysiumNpcBase::StartTaskForMaintenance(FElysiumScheduleState& State, const FElysiumScheduleStep& Step, double Now)
{
	(void)State;   // this body's own `Schedule`, which the task body writes through `TaskComplete`
	(void)Now;     // the bodies read `curtime` themselves
	// `MaintainSchedule` `0x10281e10`: `(this->*vtable[442])(pTask)`. The slot takes the task by
	// pointer and reads it only; the step belongs to the loaded program.
	StartTaskSlot442(const_cast<FElysiumScheduleStep*>(&Step));
}

void FElysiumNpcBase::RunTaskForMaintenance(FElysiumScheduleState& State, const FElysiumScheduleStep& Step, double Now)
{
	(void)State;
	(void)Now;
	// `MaintainSchedule` `0x1028202c`: `(this->*vtable[444])(pTask)`.
	RunTaskSlot444(const_cast<FElysiumScheduleStep*>(&Step));
}

bool FElysiumNpcBase::MaintainScheduleRetail(double Now, bool bReduced)
{
	return ElysiumSchedule::Tick(Schedule, *this, Now, &Cognition.Conditions, bReduced); // 0x102817c0
}

void FElysiumNpcBase::RefreshIdealStateForMaintenance()
{
	// `0x1026f4d0` clears only the five debug/source words. They are ABSENT in the shape map; no
	// member, source path or line stamp is introduced here.
	int32 Selected = PreSelectIdealStateRetail(); // 0x1026f4ec
	if (Selected == 0)
	{
		Selected = SelectIdealStateRetail(); // 0x1026f4f8
	}
	LastSelectIdealStateRetail = Selected;
	if (NpcFlags.Has(EElysiumNpcFlag2::D_INSANE)
		&& (Selected == 1 || Selected == 3)) // 0x1026f50b
	{
		Mind.WriteIdealStateRetail(0x0b); // 0x1026f55d / 0x1026f586
	}
}

void FElysiumNpcBase::RunTaskOverlay()
{
	// `RunTaskOverlay 0x10289c90` re-tests slot 529 before entering the already stood
	// `CAI_MoveAndShootOverlay` seam. The overlay's full weapon/pose controller is outside this
	// family; its existing state records the live call instead of silently dropping it.
	if (IsCurTaskContinuousMove()) // 0x10289c90
	{
		++MoveAndShootOverlay.UpdateCalls; // 0x10289c9e -> 0x102e8560
	}
}
