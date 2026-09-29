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
	// `0x1030bb30`, in the listing's order. `path+0x20` (`1030bb82`) is the NPC's `NavPathScalar20`,
	// zeroed by the caller; the guard bytes `+0x48` / `+0x58`, `+0x0`, `+0x4..+0xc`, `+0x11` and the
	// vector `+0x14..+0x1c` have no port word.
	bHasHeadWaypoint = false;                                                // 1030bb36 the list emptied, path+0x24
	bHeadIsGoal = false;
	HeadLegRequest = FElysiumNpcMoveRequest();                               // port: the head leg's request
	bHeadLegRequestSet = false;
	GoalType = 0;                                                            // 1030bb3d path+0x5c
	GoalPosCm = FVector::ZeroVector;                                         // 1030bb46 path+0x4c..+0x54 := vec3_origin
	GoalFlags = 0;                                                           // 1030bb61 path+0x60
	TargetOffsetCm = FVector::ZeroVector;                                    // 1030bb6a path+0x34..+0x3c := vec3_origin
	GoalToleranceCm = 0.f;                                                   // 1030bb7f path+0x28 := 0
	MovementActivity = GPathResetMovementActivity;                           // 1030bb85 path+0x2c := 1
	TargetEntity = FElysiumEntityHandle::Invalid();                          // 1030bb8c path+0x30 := -1
	bPedestrian = false;                                                     // 1030bbb3 path+0x1 := 0
	bPaused = false;                                                         // 1030bbbf path+0x10 := 0
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
