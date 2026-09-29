#include "Substrate/ElysiumNpcNavigator.h"

namespace
{
	// `path+0x2c`'s reset value (`0x1030bb30`; the constructor `0x1030bec0` stores the same): ACT_IDLE.
	constexpr int32 GPathResetMovementActivity = 1;
}

bool FElysiumNpcNavigator::IsGoalSet() const
{
	return GoalType != 0;                                                    // 0x102ee680
}

bool FElysiumNpcNavigator::IsGoalActive() const
{
	return bHasHeadWaypoint;                                                 // 0x102ee6a0 path+0x24
}

int32 FElysiumNpcNavigator::GetGoalType() const
{
	return GoalType;                                                         // 0x102ee620 path+0x5c
}

FVector FElysiumNpcNavigator::GetGoalPos() const
{
	return GoalPosCm - TargetOffsetCm;                                       // 0x102ee140 -> 0x1000f89e
}

int32 FElysiumNpcNavigator::GetMovementActivity() const
{
	return MovementActivity;                                                 // 0x102ee3f0 path+0x2c
}

void FElysiumNpcNavigator::ResetPath()
{
	GoalType = 0;                                                            // 0x1030bb30 path+0x5c
	GoalPosCm = FVector::ZeroVector;                                         // path+0x4c..+0x54 := vec3_origin
	TargetOffsetCm = FVector::ZeroVector;                                    // path+0x34..+0x3c := vec3_origin
	MovementActivity = GPathResetMovementActivity;                           // path+0x2c := 1
	GoalToleranceCm = 0.f;                                                   // path+0x28 := 0
	bPedestrian = false;                                                     // path+0x1 := 0
	bHasHeadWaypoint = false;                                                // the list emptied, path+0x24
	bHeadIsGoal = false;
}

int32 FElysiumNpcNavigator::GetNavType() const
{
	return NavType;                                                          // 0x1027d990 nav+0x18
}

float FElysiumNpcNavigator::GetGoalTolerance() const
{
	return GoalToleranceCm;                                                  // 0x102ee1a0 path+0x28
}

int32 FElysiumNpcNavigator::GetGoalFlags() const
{
	return GoalFlags;                                                        // 0x102ee640 path+0x60
}

bool FElysiumNpcNavigator::IsPaused() const
{
	return bPaused;                                                          // 0x102ee2e0 path+0x10
}

FElysiumEntityHandle FElysiumNpcNavigator::GetTarget() const
{
	return TargetEntity;                                                     // 0x102ee160 path+0x30
}

bool FElysiumNpcNavigator::CurWaypointIsGoal() const
{
	return bHasHeadWaypoint && bHeadIsGoal;                                  // 0x102ee660 -> 0x1030bd50
}
