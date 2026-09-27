#include "Substrate/ElysiumScriptedScheduleOrder.h"

#include "ElysiumEntity.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumScheduleNumbers.h"

// --- The recovered tables ---

bool ElysiumAiScriptedSchedule::IsKnownMode(int32 Authored)
{
	return Authored >= static_cast<int32>(EMode::MoveToGoalA)
		&& Authored <= static_cast<int32>(EMode::FollowPathB);
}

const TCHAR* ElysiumAiScriptedSchedule::ModeName(int32 Authored)
{
	switch (static_cast<EMode>(Authored))
	{
	case EMode::None:        return TEXT("none");
	case EMode::MoveToGoalA: return TEXT("move-to-goal A");
	case EMode::MoveToGoalB: return TEXT("move-to-goal B");
	case EMode::AssignEnemy: return TEXT("assign-goal-as-enemy");
	case EMode::FollowPathA: return TEXT("follow-path A");
	case EMode::FollowPathB: return TEXT("follow-path B");
	default:                 return TEXT("unknown");
	}
}

bool ElysiumAiScriptedSchedule::IsMoveToGoal(int32 Authored)
{
	return static_cast<EMode>(Authored) == EMode::MoveToGoalA
		|| static_cast<EMode>(Authored) == EMode::MoveToGoalB;
}

bool ElysiumAiScriptedSchedule::IsFollowPath(int32 Authored)
{
	return static_cast<EMode>(Authored) == EMode::FollowPathA
		|| static_cast<EMode>(Authored) == EMode::FollowPathB;
}

bool ElysiumAiScriptedSchedule::IsRunVariant(int32 AuthoredMode)
{
	// 0x101a98c0: lower mode in each pair is ACT_WALK, upper is ACT_RUN.
	return static_cast<EMode>(AuthoredMode) == EMode::MoveToGoalB
		|| static_cast<EMode>(AuthoredMode) == EMode::FollowPathB;
}

bool ElysiumAiScriptedSchedule::ForcedState(int32 Authored, EElysiumNpcState& OutState)
{
	// Recovered verbatim, and the asymmetry is the whole reason this is a table rather than a cast:
	//
	//     authored 0 -> no forced state
	//     authored 1 -> native idle   (1)
	//     authored 2 -> native ALERT  (3)
	//     authored 3 -> native COMBAT (2)
	//
	// "The non-identical numbering is load-bearing. Treating the keyvalue as the native enum would
	// swap combat and alert" — which would put the three warehouse thugs into a fight and the eight
	// combat rows on a lookaround.
	switch (Authored)
	{
	case 1: OutState = EElysiumNpcState::Idle;   return true;
	case 2: OutState = EElysiumNpcState::Alert;  return true;
	case 3: OutState = EElysiumNpcState::Combat; return true;
	default: return false;
	}
}

bool ElysiumAiScriptedSchedule::IsKnownForceState(int32 Authored)
{
	return Authored >= 0 && Authored <= 3;
}

void ElysiumAiScriptedSchedule::BuildRoute(FElysiumEntityWorld& World, const FElysiumEntity& Goal,
	TArray<FVector>& OutRoute)
{
	OutRoute.Reset();
	const FElysiumEntity* Node = &Goal;
	for (int32 Guard = 0; Guard < MaxRouteNodes && Node != nullptr; ++Guard)
	{
		OutRoute.Add(Node->Origin);
		if (Node->Target.IsEmpty())
		{
			break;
		}
		Node = World.FindByName(Node->Target);
	}
}

// 0x101a98c0 passes base IDLE_WALK (2) to both ScheduledMoveToGoalEntity and
// ScheduledFollowPath. The 9/19 arguments are ACT_WALK/ACT_RUN, never program IDs.
int32 ElysiumAiScriptedSchedule::ProgramFor(int32 AuthoredMode)
{
	return IsMoveToGoal(AuthoredMode) || IsFollowPath(AuthoredMode)
		? ElysiumSched::IDLE_WALK : ElysiumScheduleId::None;
}
