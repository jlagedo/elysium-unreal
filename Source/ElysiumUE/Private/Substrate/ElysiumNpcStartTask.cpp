// Story 0019/8 (29e under the strict verdict), family **StartTask19** -- `CAI_BaseNPCTroika`'s
// bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcStartTask.inl` (included inside `class FElysiumNpc`) or
// generated in `ElysiumNpcSlots.inl` for a slot body.
//
// `0x102a1910` is shared with `ElysiumNpcStartTask_2.cpp`: the cut follows the packet's chunk
// boundaries, one `switch`, retail's default arm once. This file carries the dispatch prologue and
// the arms whose start address lies in `[0x102a1943, 0x102a5046)` (packet chunks 1-3, 68 arms,
// lane L01); every other in-range id goes to `StartTaskTroikaTail` (lane L02), whose `default:` is
// the base forward. Walked prose:
// `docs/vtmb/npc-ai/schedule-kernel.md` § "StartTask `0x102a1910` — the dispatch".
//
// Owns (StartTask19's `rule` rows): 0x102a1910 CAI_BaseNPCTroika::StartTask.

#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcFlags.h"
#include "ElysiumPlayer.h"
#include "Substrate/ElysiumRetailActivities.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcEngineRandom.h"
#include "Substrate/ElysiumNpcGait.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcSightTrace.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleCorpus.h"
#include "Substrate/ElysiumScheduleText.h"
#include "Substrate/ElysiumWeaponClasses.h"

// The retail task ids this half of the switch compares against: the global ids the registrar
// `FUN_10316ff0` gives them (330 `AddTask` calls, `0x00..0x149`), which are the ids a step carries
// (`FElysiumScheduleStep::TaskId`). `StartTask19ArmRows` pairs each with its registrar name and the
// test resolves every name through the corpus's task namespace. A named namespace, not an anonymous
// one, so the unity build cannot collide it with L02's half.
namespace StartTask19A
{
	constexpr int32 DieIfPlayerCantSeeMask = 0x4091;              // TASK_DIE_IF_PLAYER_CANT_SEE ray, 0x102a3294..0x102a32d3, no MONSTER
	constexpr int32 TaskSuggestState = 0x06;                      // TASK_SUGGEST_STATE
	constexpr int32 TaskGetPathToEnemy = 0x0f;                    // TASK_GET_PATH_TO_ENEMY
	constexpr int32 TaskGetPathToHintNode = 0x16;                 // TASK_GET_PATH_TO_HINTNODE
	constexpr int32 TaskFaceEnemy = 0x2e;                         // TASK_FACE_ENEMY
	constexpr int32 TaskFaceHintNode = 0x2f;                      // TASK_FACE_HINTNODE
	constexpr int32 TaskRangeAttack1 = 0x34;                      // TASK_RANGE_ATTACK1
	constexpr int32 TaskRangeAttack2 = 0x35;                      // TASK_RANGE_ATTACK2
	constexpr int32 TaskMeleeAttack1 = 0x36;                      // TASK_MELEE_ATTACK1
	constexpr int32 TaskMeleeAttack2 = 0x37;                      // TASK_MELEE_ATTACK2
	constexpr int32 TaskSpecialAttack1 = 0x3e;                    // TASK_SPECIAL_ATTACK1
	constexpr int32 TaskSpecialAttack2 = 0x3f;                    // TASK_SPECIAL_ATTACK2
	constexpr int32 TaskSetActivity = 0x4b;                       // TASK_SET_ACTIVITY
	constexpr int32 TaskSetToleranceDistance = 0x4e;              // TASK_SET_TOLERANCE_DISTANCE
	constexpr int32 TaskSetToleranceDistanceAbs = 0x4f;           // TASK_SET_TOLERANCE_DISTANCE_ABS
	constexpr int32 TaskFindBackawayFromSavePosition = 0x5a;      // TASK_FIND_BACKAWAY_FROM_SAVEPOSITION
	constexpr int32 TaskWaitForMovement = 0x6e;                   // TASK_WAIT_FOR_MOVEMENT
	constexpr int32 TaskGetPathToBestUnknown = 0x79;              // TASK_GET_PATH_TO_BESTUNKNOWN
	constexpr int32 TaskGetPathToPatrolPoint = 0x7a;              // TASK_GET_PATH_TO_PATROL_POINT
	constexpr int32 TaskGetPathToPatrolPointHunt = 0x7b;          // TASK_GET_PATH_TO_PATROL_POINT_HUNT
	constexpr int32 TaskGetFullPatrolPath = 0x7c;                 // TASK_GET_FULL_PATROL_PATH
	constexpr int32 TaskNextPatrolPoint = 0x7d;                   // TASK_NEXT_PATROL_POINT
	constexpr int32 TaskNextPatrolPointHunt = 0x7e;               // TASK_NEXT_PATROL_POINT_HUNT
	constexpr int32 TaskGetDirectedPathToEnemyLkp = 0x7f;         // TASK_GET_DIRECTED_PATH_TO_ENEMY_LKP
	constexpr int32 TaskGetDirectedPathToEnemyLkpRnd = 0x80;      // TASK_GET_DIRECTED_PATH_TO_ENEMY_LKP_RND
	constexpr int32 TaskGetDirectedPathToEnemyLkpLos = 0x81;      // TASK_GET_DIRECTED_PATH_TO_ENEMY_LKP_LOS
	constexpr int32 TaskGetPathToFleeNode = 0x83;                 // TASK_GET_PATH_TO_FLEE_NODE
	constexpr int32 TaskGetPathToCowerNode = 0x84;                // TASK_GET_PATH_TO_COWER_NODE
	constexpr int32 TaskGetPathToCowerNodeSavePos = 0x85;         // TASK_GET_PATH_TO_COWER_NODE_SAVE_POS
	constexpr int32 TaskFindFollowerBackawaySimple = 0x86;        // TASK_FIND_FOLLOWER_BACKAWAY_SIMPLE
	constexpr int32 TaskFindFollowerBackawayNode = 0x87;          // TASK_FIND_FOLLOWER_BACKAWAY_NODE
	constexpr int32 TaskFindFollowerBackawayAStar = 0x88;         // TASK_FIND_FOLLOWER_BACKAWAY_ASTAR
	constexpr int32 TaskMeleeKick = 0x9a;                         // TASK_MELEE_KICK
	constexpr int32 TaskSetMeleeToleranceDistance = 0x9f;         // TASK_SET_MELEE_TOLERANCE_DISTANCE
	constexpr int32 TaskFindFastCoverFromEnemy = 0xa0;            // TASK_FIND_FAST_COVER_FROM_ENEMY
	constexpr int32 TaskFindForwardCoverFromEnemy = 0xa1;         // TASK_FIND_FORWARD_COVER_FROM_ENEMY
	constexpr int32 TaskFindCoverFromSavePosition = 0xa2;         // TASK_FIND_COVER_FROM_SAVEPOSITION
	constexpr int32 TaskFindFlankNodeToEnemy = 0xa3;              // TASK_FIND_FLANK_NODE_TO_ENEMY
	constexpr int32 TaskFindInterestingPlace = 0xa4;              // TASK_FIND_INTERESTING_PLACE
	constexpr int32 TaskGetPathToInterestingPlace = 0xa5;         // TASK_GET_PATH_TO_INTERESTING_PLACE
	constexpr int32 TaskPauseMoving = 0xa6;                       // TASK_PAUSE_MOVING
	constexpr int32 TaskTest1 = 0xa7;                             // TASK_TEST1
	constexpr int32 TaskTest2 = 0xa8;                             // TASK_TEST2
	constexpr int32 TaskCreateHuntPatrolList = 0xae;              // TASK_CREATE_HUNT_PATROL_LIST
	constexpr int32 TaskFindHuntPatrolTarget = 0xaf;              // TASK_FIND_HUNT_PATROL_TARGET
	constexpr int32 TaskWaitAttackTime1 = 0xb0;                   // TASK_WAIT_ATTACK_TIME1
	constexpr int32 TaskWaitAttackTime2 = 0xb1;                   // TASK_WAIT_ATTACK_TIME2
	constexpr int32 TaskRunDialog = 0xb9;                         // TASK_RUN_DIALOG
	constexpr int32 TaskRunDisposition = 0xba;                    // TASK_RUN_DISPOSITION
	constexpr int32 TaskRunDispositionRandom = 0xbb;              // TASK_RUN_DISPOSITION_RANDOM
	constexpr int32 TaskSpecialIdleActivity = 0xbc;               // TASK_SPECIAL_IDLE_ACTIVITY
	constexpr int32 TaskSpecialIdleActivityRandom = 0xbd;         // TASK_SPECIAL_IDLE_ACTIVITY_RANDOM
	constexpr int32 TaskAddEventExpression = 0xbe;                // TASK_ADD_EVENT_EXPRESSION
	constexpr int32 TaskSetPreservePath = 0xc4;                   // TASK_SET_PRESERVE_PATH
	constexpr int32 TaskSetEnemyEluded = 0xc5;                    // TASK_SET_ENEMY_ELUDED
	constexpr int32 TaskSetTargetEluded = 0xc6;                   // TASK_SET_TARGET_ELUDED
	constexpr int32 TaskGetPathToEnemyClosest = 0xc7;             // TASK_GET_PATH_TO_ENEMY_CLOSEST
	constexpr int32 TaskSetInsideInterruptDist = 0xcf;            // TASK_SET_INSIDE_INTERRUPT_DIST
	constexpr int32 TaskSetOutsideInterruptDist = 0xd0;           // TASK_SET_OUTSIDE_INTERRUPT_DIST
	constexpr int32 TaskSetInterruptTime = 0xd3;                  // TASK_SET_INTERRUPT_TIME
	constexpr int32 TaskAddRandomInterruptTime = 0xd4;            // TASK_ADD_RANDOM_INTERRUPT_TIME
	constexpr int32 TaskClearInterruptTime = 0xd5;                // TASK_CLEAR_INTERRUPT_TIME
	constexpr int32 TaskWalkRunPath = 0xd6;                       // TASK_WALK_RUN_PATH
	constexpr int32 TaskWalkRunPathCombatSound = 0xd7;            // TASK_WALK_RUN_PATH_COMBAT_SOUND
	constexpr int32 TaskGetPathToPlayerForDialog = 0xd8;          // TASK_GET_PATH_TO_PLAYER_FOR_DIALOG
	constexpr int32 TaskWalkRunPathForDialog = 0xd9;              // TASK_WALK_RUN_PATH_FOR_DIALOG
	constexpr int32 TaskStartPlayerDialog = 0xda;                 // TASK_START_PLAYER_DIALOG
	constexpr int32 TaskSetToleranceDistDlg = 0xdb;               // TASK_SET_TOLERANCE_DIST_DLG
	constexpr int32 TaskDieIfPlayerCantSee = 0xdc;                // TASK_DIE_IF_PLAYER_CANT_SEE
	constexpr int32 TaskKnockout = 0xdd;                          // TASK_KNOCKOUT
	constexpr int32 TaskUnknockout = 0xde;                        // TASK_UNKNOCKOUT
	constexpr int32 TaskDoJumpActivity = 0xe0;                    // TASK_DO_JUMP_ACTIVITY
	constexpr int32 TaskDoLoopActivity = 0xe1;                    // TASK_DO_LOOP_ACTIVITY
	constexpr int32 TaskDoBlendActivity = 0xe2;                   // TASK_DO_BLEND_ACTIVITY
	constexpr int32 TaskDoBlendLoopActivity = 0xe3;               // TASK_DO_BLEND_LOOP_ACTIVITY

	// The dispatch bounds (`0x102a1925 LEA ECX,[EAX-5]`, `0x102a1928 CMP ECX,0x144`).
	constexpr int32 DispatchFirstId = 5;
	constexpr uint32 DispatchSpan = 0x144;

	// `+0x1b48`'s retail source lines, one per `TaskFail` site of this half (the `+0x1b44` file is
	// `"E:\Vampire\main\dlls\AI_BaseNPCTroika.cpp"` at `0x105da024` on every one).
	constexpr int32 LineSuggestState = 0x2dff;           // the `+0x1b40` selector trace, not a fail
	constexpr int32 LineSuggestStateIdleFrenzy = 0x2e05;
	constexpr int32 LineSuggestStateAlertFrenzy = 0x2e06;
	constexpr int32 LineSuggestStateNoAlert = 0x2e0e;
	constexpr int32 LineBackawayNoNode = 0x2e3f;
	constexpr int32 LineBackawayNoRoute = 0x2e4a;
	constexpr int32 LineInterestingPlaceNoSpot = 0x2eaa;
	constexpr int32 LineInterestingPlaceNone = 0x2eaf;
	constexpr int32 LineInterestingPlaceNoRoute = 0x2ecb;
	constexpr int32 LineInterestingPlaceLost = 0x2ed0;
	constexpr int32 LineFastCoverNone = 0x2ef8;
	constexpr int32 LineSavePositionCoverNone = 0x2f0b;
	constexpr int32 LineForwardCoverNone = 0x2f32;
	constexpr int32 LineFlankNoEnemy = 0x2f3c;
	constexpr int32 LineFlankNoPosition = 0x2f71;
	constexpr int32 LineFleeNoNode = 0x2fe8;
	constexpr int32 LineCowerSaveNoNode = 0x3023;

	constexpr int32 LineFollowerSimpleProbe = 0x304c;
	constexpr int32 LineFollowerSimpleNoBoss = 0x3051;
	constexpr int32 LineFollowerNodeNoNode = 0x306a;
	constexpr int32 LineFollowerNodeNoBoss = 0x306f;
	constexpr int32 LineFollowerAStarNoNode = 0x3087;
	constexpr int32 LineFollowerAStarNoBoss = 0x308c;
	constexpr int32 LinePathToEnemyUnreachable = 0x3101;
	constexpr int32 LinePathToEnemyNoEnemy = 0x3109;
	constexpr int32 LinePathToEnemyNoRoute = 0x311a;
	constexpr int32 LineBestUnknownNone = 0x3127;
	constexpr int32 LineBestUnknownNoRoute = 0x3134;
	constexpr int32 LineHintNodeNone = 0x314a;
	constexpr int32 LineFullPatrolNoPath = 0x3186;
	constexpr int32 LineFullPatrolNoRoute = 0x31a0;
	constexpr int32 LineHuntListFailed = 0x31cd;
	constexpr int32 LineHuntTargetFailed = 0x31e8;
	constexpr int32 LineDirectedNoEnemy = 0x3205;
	constexpr int32 LineDirectedNoRoute = 0x3235;
	constexpr int32 LineDirectedNoLkp = 0x323a;
	constexpr int32 LineDirectedLosNoEnemy = 0x3245;
	constexpr int32 LineDirectedLosNoPoint = 0x3271;
	constexpr int32 LineDirectedLosNoLkp = 0x3276;

	constexpr int32 LineMeleeNoWeapon = 0x3302;
	constexpr int32 LineKickNoWeapon = 0x331d;
	constexpr int32 LineEnemyEludedNoEnemy = 0x3333;
	constexpr int32 LineTargetEludedNoTarget = 0x3341;
	constexpr int32 LinePathToEnemyClosestNoEnemy = 0x334c;
	constexpr int32 LinePathToEnemyClosestNoRoute = 0x335a;

	// `TaskFail` reasons, the `0x106152b0` text table (`ElysiumTaskFailureName`).
	constexpr int32 FailNoTarget = 0x01;
	constexpr int32 FailNoHintNode = 0x04;
	constexpr int32 FailNoEnemy = 0x06;
	constexpr int32 FailNoBackawayNode = 0x07;
	constexpr int32 FailNoCover = 0x08;
	constexpr int32 FailNoShootPosition = 0x0b;
	constexpr int32 FailNoRoute = 0x0c;
	constexpr int32 FailRouteBlocked = 0x0e;
	constexpr int32 FailNoPlayer = 0x17;
	constexpr int32 FailNoReachableNodes = 0x18;
	constexpr int32 FailBadPosition = 0x1a;
	constexpr int32 FailNoPatrolPath = 0x1d;
	constexpr int32 FailNoWeapon = 0x1f;
	constexpr int32 FailHuntList = 0x20;
	constexpr int32 FailNoSeenUnknown = 0x21;
	constexpr int32 FailNoInterestingPlace = 0x22;
	constexpr int32 FailNoFollowerBoss = 0x29;
	constexpr int32 FailBadActivity = 0x15;

	// Retail activity ids (`Activity`) these arms hand to `RestartIdealActivity` /
	// `SetIdealActivity` / slot 310.
	constexpr int32 ActIdle = 0x01;
	constexpr int32 ActWalk = 0x09;
	constexpr int32 ActRun = 0x13;

	// Float literals read out of the image.
	constexpr float SetActivityWatchdogSeconds = 1.0f; // `0x104454c0` = 1.0
	constexpr float Half = 0.5f;                // `0x104454d0`
	constexpr float Quarter = 0.25f;            // `0x1044bef8`
	constexpr float BackawayLimitUnits = 64.0f; // `0x42800000`, pushed at `0x102a1c8b`
	constexpr float HintSearchPadUnits = 8192.0f; // `0x1049ae70`
	constexpr float FastCoverMaxUnits = 150.0f; // `0x43160000`, pushed at `0x102a2137`
	constexpr float SaveCoverMinUnits = 32.0f;  // `0x42000000`, pushed at `0x102a2253`
	constexpr float FlankDefaultMaxUnits = 2000.0f; // `0x44fa0000`, stored at `0x102a250d`
	constexpr float FlankFreshSeconds = 2.0f;   // `0x10452dc4`
	constexpr float TeleportProbeDownUnits = -1024.0f; // `0xc4800000`, pushed at `0x102a1e99`
	constexpr float TestJitterUnits = 200.0f;   // `0x43480000` / `0xc3480000`
	constexpr float TestBoxHalfUnits = 2.0f;    // `0x40000000` / `0xc0000000`
	constexpr uint32 CowerHintType = 0x2774;    // pushed at `0x102a28e7`
	constexpr uint8 CowerHintFlags = 2;         // pushed at `0x102a28e5`
	constexpr float HintClaimReleaseSeconds = 1.0f; // `0x3f800000`, pushed at `0x102a28d0`
	constexpr float HintGoalReleaseSeconds = 5.0f;  // `0x40a00000`, pushed at `0x102a2a52`
	const FVector CowerAttackExtentsUnits(40.0, 40.0, 80.0); // `0x102a2a21..0x102a2a37`
	constexpr uint32 TeleportProbeMask = 0x202400b; // `MASK_NPCSOLID`, pushed at `0x102a1ea3`
	constexpr float FollowerJitterDegrees = 45.0f;     // `0x42340000` / `0xc2340000`, pushed at `0x102a2e68`
	constexpr float FollowerProbeExtentUnits = 100.0f; // `0x42c80000`, pushed at `0x102a2f09`
	constexpr float FollowerWalkPadUnits = 10.0f;      // `0x1044e664`
	constexpr float FollowerSearchMaxUnits = 50000.0f; // `0x47435000`, pushed at `0x102a3001`
	constexpr float HuntSearchUnits = 256.0f;          // `0x43800000`, pushed at `0x102a3c01`
	constexpr float HintLeanDegrees = 45.0f;           // `0x1049949c`
	constexpr float HalfTurnDegrees = 180.0f;          // `0x1044c3a8`
	constexpr int32 HintTypeCoverLean = 0x27d8;        // compared at `0x102a3841`
	constexpr uint32 KnockoutFlags = 0x440a0000u;      // OR'd at `0x102a3344`
	constexpr int32 ActKnockout = 0x1050;              // pushed at `0x102a333f`
	constexpr int32 ActUnknockout = 0x1052;            // pushed at `0x102a3364`
	constexpr int32 ActWaitAttack = 0x05;              // pushed at `0x102a33e3`
	constexpr int32 ActRangeAttack2 = 0x1b;            // pushed at `0x102a457b`
	constexpr int32 ActSpecialAttack1 = 0x5e;          // pushed at `0x102a459a`
	constexpr int32 ActSpecialAttack2 = 0x5f;          // pushed at `0x102a45b0`
	constexpr int32 ActJump = 0x1089;                  // pushed at `0x102a4ec9`
	constexpr uint32 MeleeCapabilityMask = 0x18000u;   // tested at `0x102a45e8`
	constexpr float HalfDouble = 0.5f;                 // `0x10449270` (double 0.5)
	constexpr float DialogToleranceUnits = 160.0f;     // `0x43200000`, stored at `0x102a4c49`
	constexpr int32 ExpressionEventCount = 2;          // `0x102a4a13 CMP EAX,2`
	constexpr uint32 MemoryInCover = 0x2u;             // `m_afMemory &= ~2` (`0x102a98e0(this, 2)`)

	// `UTIL_VecToYaw` (`0x101d2c70`, thunk `0x1000612c`): degrees of the planar direction.
	float VecToYaw(const FVector& V)
	{
		if (V.X == 0.0 && V.Y == 0.0)
		{
			return 0.f;
		}
		return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(V.Y, V.X)));
	}

	// `UTIL_YawToVector` (`0x101d2f40`, thunk `0x1000ecd2`): the planar unit vector of a yaw.
	FVector YawToVector(float YawDegrees)
	{
		const double Radians = FMath::DegreesToRadians(static_cast<double>(YawDegrees));
		return FVector(FMath::Cos(Radians), FMath::Sin(Radians), 0.0);
	}

	double NowOf(const FElysiumNpc& Npc)
	{
		return Npc.World != nullptr ? Npc.World->NowSeconds() : 0.0;
	}

	FRandomStream& Rng()
	{
		return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	}

	// `DAT_1070b244`'s vtable `+4`, `RandomFloat(min, max)`: `min + (max - min) * frac`.
	float RandomFloat(float Min, float Max)
	{
		return Rng().FRandRange(Min, Max);
	}

	// `DAT_1070b244`'s vtable `+8`, `RandomInt(min, max)`, is `ElysiumNpcEngineRandom::RandomInt`.

	FVector UnitsOf(const FVector& Cm)
	{
		return Cm / ElysiumMove::U;
	}

	FVector CmOf(const FVector& Units)
	{
		return Units * ElysiumMove::U;
	}
}

// =================================================================================================
// The shared tails.
// =================================================================================================

int32 FElysiumNpc::StartTask19Complete()
{
	// `0x102a66d7`: `PUSH 0; MOV ECX,ESI; CALL 0x1000ac68` -> `TaskComplete(false)` `0x10273e80`.
	TaskComplete(false);
	return 0;
}

int32 FElysiumNpc::StartTask19Fail(int32 Line, int32 Reason)
{
	// `+0x1b44 = 0x105da024 "E:\Vampire\main\dlls\AI_BaseNPCTroika.cpp"`, `+0x1b48 = Line`, then
	// `CALL [EDX+0x700]` (slot 448). The pair is ABSENT (shape map); the row stands for it.
	if (Line != 0)
	{
		RecordScheduleEvent(FString::Printf(TEXT("StartTask fail trace AI_BaseNPCTroika.cpp:%d"), Line));
	}
	TaskFail(Reason);
	return 0;
}

bool FElysiumNpc::StartTask19SetGoal(const FStartTask19NavGoal& Goal, uint32 SetGoalFlags)
{
	++StartTask19SetGoalCalls;
	StartTask19LastGoal = Goal;
	StartTask19LastGoalFlags = SetGoalFlags;
	// `0x102ecd20`: the literal converted into the one body's record. An entity goal (1 `m_hTargetEnt`,
	// 2 the enemy, 7 `GetBestSeeUnknown`) keeps the default dest triple; the body resolves the entity.
	FStartTaskNavGoal Record;
	Record.Type = Goal.Type;
	Record.bDestSet = Goal.Type != 1 && Goal.Type != 2 && Goal.Type != 7;
	Record.DestCm = StartTask19A::CmOf(Goal.DestUnits);
	Record.DestNode = Goal.DestNode;
	Record.MovementActivity = Goal.Activity;
	Record.ArrivalActivity = Goal.ArrivalActivity;
	Record.ArrivalSequence = Goal.ArrivalSequence;
	Record.ToleranceUnits = Goal.Tolerance;
	Record.GoalFlags = Goal.GoalFlags;
	Record.Target = Goal.Target;
	return StartTaskSetGoal(Record, static_cast<int32>(SetGoalFlags));
}

int32 FElysiumNpc::StartTask19GoalThenMoveWait(const FStartTask19NavGoal& Goal, float TaskData)
{
	(void)StartTask19SetGoal(Goal, 0);                                        // 0x102a76a4
	BaseScheduleHost.MoveWaitFinished =
		StartTask19A::NowOf(*this) + static_cast<double>(TaskData);   // 0x102a76a9..b5
	return 0;
}

int32 FElysiumNpc::StartTask19SharedRunGoal(const FVector& DestUnits, uint32 SetGoalFlags)
{
	// `0x102a4186`: `0x102a9d20(&goal, &dest, 0x13, -2.0, 0, DAT_10923dd8)` then `SetGoal`.
	FStartTask19NavGoal Goal;
	Goal.Type = 4;
	Goal.DestUnits = DestUnits;
	Goal.Activity = StartTask19A::ActRun;
	Goal.Tolerance = StartTask19HullTolerance;
	(void)StartTask19SetGoal(Goal, SetGoalFlags);
	return 0;
}

int32 FElysiumNpc::StartTask19TurnTail(const FVector& PointUnits)
{
	// `0x102a44d1 CALL 0x10009980` -- the motor's stop-turn (`0x102e0b40`).
	++StartTask19MotorStopTurns;
	StartTaskMotorHoldYaw();
	// `0x102a44e9 CALL 0x10001d11` -- `0x102e2020(motor, &point, 0)`, the ideal yaw toward the point.
	++StartTask19MotorFaces;
	StartTask19LastFacePointUnits = PointUnits;
	StartTaskMotorSetIdealYawToTarget(StartTask19A::CmOf(PointUnits));
	SetTurnActivity();                                                        // 0x102a44f2, slot 572
	return 0;
}

// =================================================================================================
// The seams.
// =================================================================================================

bool FElysiumNpc::StartTask19FindCoverPos(const FVector& ThreatOriginUnits,
	const FVector& ThreatEyeUnits, float MinUnits, float MaxUnits, FVector& OutUnits)
{
	// `0x102edc80` is `FElysiumNpcBase::StartTaskFindCoverPos`; recorded here for this half's tests.
	++StartTask19CoverSearches;
	StartTask19LastCoverMinUnits = MinUnits;
	StartTask19LastCoverMaxUnits = MaxUnits;
	FVector CoverCm = FVector::ZeroVector;
	if (!StartTaskFindCoverPos(StartTask19A::CmOf(ThreatOriginUnits), StartTask19A::CmOf(ThreatEyeUnits),
		MinUnits, MaxUnits, CoverCm))
	{
		return false;
	}
	OutUnits = StartTask19A::UnitsOf(CoverCm);
	return true;
}

bool FElysiumNpc::StartTask19NavArrived()
{
	return Motor != nullptr && SampleMotorIntoEntity() == EElysiumNpcMoveStatus::Reached;
}

bool FElysiumNpc::StartTask19TeleportProbe(FVector& InOutUnits, bool& bOutLandedOnNonNpc)
{
	bOutLandedOnNonNpc = false;
	if (!MoveProbeFloorDrop(InOutUnits))
	{
		return false;
	}
	bOutLandedOnNonNpc = true;
	return true;
}

bool FElysiumNpc::StartTask19EnemyLkp(const FElysiumEntity& Enemy, FVector& OutLkpUnits,
	FVector& OutSeenUnits) const
{
	const FElysiumNpcEnemyMemoryRecord* Record = EnemyMemory.Find(Enemy.Handle);
	if (Record == nullptr)
	{
		return false;
	}
	OutLkpUnits = StartTask19A::UnitsOf(Record->Anchor);              // `0x102e032a..33a` record `+0xc`
	// FLAGGED (Q-H3, story V13), not changed: retail's second out is the record's `+0x18`, the last
	// known velocity (`senses.md` § The enemy memory); the port hands the position again.
	OutSeenUnits = OutLkpUnits;
	return true;
}

void FElysiumNpc::StartTask19SetNavTolerances(float GoalToleranceUnits, float ArrivalDistanceUnits)
{
	Navigator.GoalToleranceCm = GoalToleranceUnits * ElysiumMove::U;                 // 0x102ee1c0 path+0x28
	NavPathScalar20 = ArrivalDistanceUnits;                                   // 0x102f2fe0 path+0x20
}

float FElysiumNpc::StartTask19HullWidthUnits(int32 Hull) const
{
	FVector Mins = FVector::ZeroVector;
	FVector Maxs = FVector::ZeroVector;
	RetailHullExtents(Hull, EElysiumHullExtents::Full, Mins, Maxs);
	return static_cast<float>(Maxs.X - Mins.X);
}

void FElysiumNpc::StartTask19AddExpressionForEvent(int32 EventIndex)
{
	// SEAM for `0x101072b0`.
	++StartTask19ExpressionEvents;
	StartTask19LastExpressionEvent = EventIndex;
}

bool FElysiumNpc::StartTask19FindForwardCover(const FVector& ThreatOriginUnits,
	const FVector& ThreatEyeUnits, const FVector& InForward, float MinUnits, float MaxUnits,
	FVector& OutUnits)
{
	// SEAM for `0x102edd50`: no forward-cover search stands here.
	(void)ThreatOriginUnits; (void)ThreatEyeUnits; (void)InForward; (void)MinUnits; (void)MaxUnits;
	(void)OutUnits;
	return false;
}

bool FElysiumNpc::StartTask19FindLosPosition(const FVector& FromUnits, const FVector& ToUnits,
	float MinUnits, float MaxUnits, const FVector* InForward, FVector& OutUnits)
{
	++StartTask19LosSearches;
	FVector OutCm = FVector::ZeroVector;
	bool bFound = false;
	if (InForward == nullptr)
	{
		// `0x102edaa0` is `FElysiumNpcBase::StartTaskFindLosPos`.
		bFound = StartTaskFindLosPos(StartTask19A::CmOf(FromUnits), StartTask19A::CmOf(ToUnits), MinUnits,
			MaxUnits, OutCm);
	}
	// SEAM for `0x102ed9c0` (the forward-constrained sweep): no node graph to sweep; false.
	if (bFound)
	{
		OutUnits = StartTask19A::UnitsOf(OutCm);
	}
	return bFound;
}

void FElysiumNpc::StartTask19SetArrivalDirection(const FVector& DirectionUnits)
{
	// `0x102ee530` is `FElysiumNpcBase::StartTaskSetArrivalDirection` (a direction: unit-free).
	++StartTask19ArrivalDirectionWrites;
	StartTaskSetArrivalDirection(DirectionUnits);
}

bool FElysiumNpc::StartTask19FindBackawayAStar(const FVector& FromUnits, float MinUnits,
	float MaxUnits, FVector& OutUnits)
{
	// SEAM for `0x102edbb0`: no node graph.
	(void)FromUnits; (void)MinUnits; (void)MaxUnits; (void)OutUnits;
	return false;
}

bool FElysiumNpc::StartTask19DirectedPathPoint(const FVector& FromUnits, const FVector& ToUnits,
	float Distance, FVector& OutUnits)
{
	// SEAM for `0x102ee300`: no node graph.
	(void)FromUnits; (void)ToUnits; (void)Distance; (void)OutUnits;
	return false;
}

bool FElysiumNpc::StartTask19BuildHuntPatrolList(const FVector& OriginUnits,
	const FVector* TargetUnits, float RadiusUnits)
{
	// SEAM for `CAI_Pathfinder` `0x10306700`: no pathfinder.
	(void)OriginUnits; (void)TargetUnits; (void)RadiusUnits;
	return false;
}

bool FElysiumNpc::StartTask19FindHuntPatrolTarget(const FVector& OriginUnits,
	const FVector* TargetUnits, float RadiusUnits, FVector& OutUnits)
{
	// SEAM for `CAI_Pathfinder` `0x10306f60`: no pathfinder.
	(void)OriginUnits; (void)TargetUnits; (void)RadiusUnits; (void)OutUnits;
	return false;
}

void FElysiumNpc::StartTask19PatrolPointGoal(bool bHunt)
{
	// `0x102aa640(this, bHunt ? &m_sppPatrolPathHunt (+0x6594) : &m_sppPatrolPath (+0x658c))`, family
	// Script19's body, which owns the task's complete / fail.
	IssuePatrolMoveStart(bHunt ? &PatrolPathHuntCell : &PatrolPathCell);
}

uint32 FElysiumNpc::StartTask19WeaponCapabilityWord() const
{
	if (ActiveWeaponEntity() == nullptr)
	{
		return 0;
	}
	return static_cast<uint32>(ElysiumNpcCond::CapabilityBits(ElysiumNpcCond::WeaponCapability(*this)));
}

int32 FElysiumNpc::StartTask19WeaponMinBurst() const
{
	return 1;   // SEAM for weapon `+0x3a4`.
}

int32 FElysiumNpc::StartTask19WeaponMaxBurst() const
{
	return 1;   // SEAM for weapon `+0x3a8`.
}

float FElysiumNpc::StartTask19WeaponRangeWord(int32 Offset) const
{
	// Weapon `+0x8b8` / `+0x8bc` / `+0x8c0` / `+0x8c4`, the active weapon's class words (0018 story 8,
	// findings R3). Every caller has already tested `GetActiveWeapon()`; with none it reads 0.
	ElysiumWeapons::FRangeWords Words;
	const FElysiumEntity* HeldWeapon = ActiveWeaponEntity();
	if (HeldWeapon == nullptr || !ElysiumWeapons::ItemRangeWords(*HeldWeapon, Words))
	{
		return 0.f;
	}
	switch (Offset)
	{
	case 0x8b8: return Words.MinRange1;
	case 0x8bc: return Words.MinRange2;
	case 0x8c0: return Words.MaxRange1;
	case 0x8c4: return Words.MaxRange2;
	default:    return 0.f;
	}
}

void FElysiumNpc::StartTask19WeaponSwing(bool bSecondary)
{
	++StartTask19WeaponSwings;
	FElysiumItem* Item = Inventory.Active(*this);
	FElysiumWeapon* Weapon = Item != nullptr ? Item->AsWeapon() : nullptr;
	if (Weapon == nullptr)
	{
		return;
	}
	const FElysiumWeapon::EVerdict Verdict = Weapon->AttackIntent(
		bSecondary ? FElysiumWeapon::EIntent::Secondary : FElysiumWeapon::EIntent::Primary);
	RecordScheduleEvent(FString::Printf(TEXT("StartTask weapon slot 0x%x -> %s"),
		bSecondary ? 0x51c : 0x518, FElysiumWeapon::VerdictName(Verdict)));
}

bool FElysiumNpc::StartTask19KickWeaponCast() const
{
	return false;   // SEAM for `__RTDynamicCast(weapon, 0, 0x1055f710, 0x105da324, 0)`.
}

double FElysiumNpc::StartTask19WeaponNextAttackTime(bool bSecondary) const
{
	(void)bSecondary;   // SEAM for `0x10252450` + `0x102c5730`.
	return StartTask19A::NowOf(*this);
}

FElysiumEntity* FElysiumNpc::StartTask19ClosestPlayer() const
{
	return World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;
}

void FElysiumNpc::StartTask19PlayerStartDialog(FElysiumEntity& Player)
{
	// `player->vtable[0x678](this)`: the player starts talking to this NPC.
	(void)OpenConversation(Player.Handle, EElysiumDialogOpenerKind::Forced);
}

TConstArrayView<FElysiumNpc::FStartTask19ArmRow> FElysiumNpc::StartTask19ArmRows()
{
	static const FStartTask19ArmRow Rows[] =
	{
		{ StartTask19A::TaskSuggestState, TEXT("TASK_SUGGEST_STATE"), TEXT("0x102a1b2b") },
		{ StartTask19A::TaskGetPathToEnemy, TEXT("TASK_GET_PATH_TO_ENEMY"), TEXT("0x102a33f9") },
		{ StartTask19A::TaskGetPathToHintNode, TEXT("TASK_GET_PATH_TO_HINTNODE"), TEXT("0x102a371d") },
		{ StartTask19A::TaskFaceEnemy, TEXT("TASK_FACE_ENEMY"), TEXT("0x102a4417") },
		{ StartTask19A::TaskFaceHintNode, TEXT("TASK_FACE_HINTNODE"), TEXT("0x102a382a") },
		{ StartTask19A::TaskRangeAttack1, TEXT("TASK_RANGE_ATTACK1"), TEXT("0x102a4505") },
		{ StartTask19A::TaskRangeAttack2, TEXT("TASK_RANGE_ATTACK2"), TEXT("0x102a4576") },
		{ StartTask19A::TaskMeleeAttack1, TEXT("TASK_MELEE_ATTACK1"), TEXT("0x102a45c6") },
		{ StartTask19A::TaskMeleeAttack2, TEXT("TASK_MELEE_ATTACK2"), TEXT("0x102a45c6") },
		{ StartTask19A::TaskSpecialAttack1, TEXT("TASK_SPECIAL_ATTACK1"), TEXT("0x102a459a") },
		{ StartTask19A::TaskSpecialAttack2, TEXT("TASK_SPECIAL_ATTACK2"), TEXT("0x102a45b0") },
		{ StartTask19A::TaskSetActivity, TEXT("TASK_SET_ACTIVITY"), TEXT("0x102a1c0f") },
		{ StartTask19A::TaskSetToleranceDistance, TEXT("TASK_SET_TOLERANCE_DISTANCE"), TEXT("0x102a4289") },
		{ StartTask19A::TaskSetToleranceDistanceAbs, TEXT("TASK_SET_TOLERANCE_DISTANCE_ABS"), TEXT("0x102a42fa") },
		{ StartTask19A::TaskFindBackawayFromSavePosition, TEXT("TASK_FIND_BACKAWAY_FROM_SAVEPOSITION"), TEXT("0x102a1c6a") },
		{ StartTask19A::TaskWaitForMovement, TEXT("TASK_WAIT_FOR_MOVEMENT"), TEXT("0x102a1dcc") },
		{ StartTask19A::TaskGetPathToBestUnknown, TEXT("TASK_GET_PATH_TO_BESTUNKNOWN"), TEXT("0x102a3599") },
		{ StartTask19A::TaskGetPathToPatrolPoint, TEXT("TASK_GET_PATH_TO_PATROL_POINT"), TEXT("0x102a39be") },
		{ StartTask19A::TaskGetPathToPatrolPointHunt, TEXT("TASK_GET_PATH_TO_PATROL_POINT_HUNT"), TEXT("0x102a39d9") },
		{ StartTask19A::TaskGetFullPatrolPath, TEXT("TASK_GET_FULL_PATROL_PATH"), TEXT("0x102a39f4") },
		{ StartTask19A::TaskNextPatrolPoint, TEXT("TASK_NEXT_PATROL_POINT"), TEXT("0x102a3b91") },
		{ StartTask19A::TaskNextPatrolPointHunt, TEXT("TASK_NEXT_PATROL_POINT_HUNT"), TEXT("0x102a3bac") },
		{ StartTask19A::TaskGetDirectedPathToEnemyLkp, TEXT("TASK_GET_DIRECTED_PATH_TO_ENEMY_LKP"), TEXT("0x102a3d48") },
		{ StartTask19A::TaskGetDirectedPathToEnemyLkpRnd, TEXT("TASK_GET_DIRECTED_PATH_TO_ENEMY_LKP_RND"), TEXT("0x102a3d48") },
		{ StartTask19A::TaskGetDirectedPathToEnemyLkpLos, TEXT("TASK_GET_DIRECTED_PATH_TO_ENEMY_LKP_LOS"), TEXT("0x102a3f84") },
		{ StartTask19A::TaskGetPathToFleeNode, TEXT("TASK_GET_PATH_TO_FLEE_NODE"), TEXT("0x102a2882") },
		{ StartTask19A::TaskGetPathToCowerNode, TEXT("TASK_GET_PATH_TO_COWER_NODE"), TEXT("0x102a2882") },
		{ StartTask19A::TaskGetPathToCowerNodeSavePos, TEXT("TASK_GET_PATH_TO_COWER_NODE_SAVE_POS"), TEXT("0x102a2bd8") },
		{ StartTask19A::TaskFindFollowerBackawaySimple, TEXT("TASK_FIND_FOLLOWER_BACKAWAY_SIMPLE"), TEXT("0x102a2d9b") },
		{ StartTask19A::TaskFindFollowerBackawayNode, TEXT("TASK_FIND_FOLLOWER_BACKAWAY_NODE"), TEXT("0x102a2f94") },
		{ StartTask19A::TaskFindFollowerBackawayAStar, TEXT("TASK_FIND_FOLLOWER_BACKAWAY_ASTAR"), TEXT("0x102a3096") },
		{ StartTask19A::TaskMeleeKick, TEXT("TASK_MELEE_KICK"), TEXT("0x102a464d") },
		{ StartTask19A::TaskSetMeleeToleranceDistance, TEXT("TASK_SET_MELEE_TOLERANCE_DISTANCE"), TEXT("0x102a434a") },
		{ StartTask19A::TaskFindFastCoverFromEnemy, TEXT("TASK_FIND_FAST_COVER_FROM_ENEMY"), TEXT("0x102a20dc") },
		{ StartTask19A::TaskFindForwardCoverFromEnemy, TEXT("TASK_FIND_FORWARD_COVER_FROM_ENEMY"), TEXT("0x102a232e") },
		{ StartTask19A::TaskFindCoverFromSavePosition, TEXT("TASK_FIND_COVER_FROM_SAVEPOSITION"), TEXT("0x102a2231") },
		{ StartTask19A::TaskFindFlankNodeToEnemy, TEXT("TASK_FIND_FLANK_NODE_TO_ENEMY"), TEXT("0x102a24b1") },
		{ StartTask19A::TaskFindInterestingPlace, TEXT("TASK_FIND_INTERESTING_PLACE"), TEXT("0x102a1f23") },
		{ StartTask19A::TaskGetPathToInterestingPlace, TEXT("TASK_GET_PATH_TO_INTERESTING_PLACE"), TEXT("0x102a1fc3") },
		{ StartTask19A::TaskPauseMoving, TEXT("TASK_PAUSE_MOVING"), TEXT("0x102a1f06") },
		{ StartTask19A::TaskTest1, TEXT("TASK_TEST1"), TEXT("0x102a1943") },
		{ StartTask19A::TaskTest2, TEXT("TASK_TEST2"), TEXT("0x102a1a60") },
		{ StartTask19A::TaskCreateHuntPatrolList, TEXT("TASK_CREATE_HUNT_PATROL_LIST"), TEXT("0x102a3bc7") },
		{ StartTask19A::TaskFindHuntPatrolTarget, TEXT("TASK_FIND_HUNT_PATROL_TARGET"), TEXT("0x102a3c5d") },
		{ StartTask19A::TaskWaitAttackTime1, TEXT("TASK_WAIT_ATTACK_TIME1"), TEXT("0x102a337d") },
		{ StartTask19A::TaskWaitAttackTime2, TEXT("TASK_WAIT_ATTACK_TIME2"), TEXT("0x102a337d") },
		{ StartTask19A::TaskRunDialog, TEXT("TASK_RUN_DIALOG"), TEXT("0x102a496b") },
		{ StartTask19A::TaskRunDisposition, TEXT("TASK_RUN_DISPOSITION"), TEXT("0x102a49bc") },
		{ StartTask19A::TaskRunDispositionRandom, TEXT("TASK_RUN_DISPOSITION_RANDOM"), TEXT("0x102a49da") },
		{ StartTask19A::TaskSpecialIdleActivity, TEXT("TASK_SPECIAL_IDLE_ACTIVITY"), TEXT("0x102a49bc") },
		{ StartTask19A::TaskSpecialIdleActivityRandom, TEXT("TASK_SPECIAL_IDLE_ACTIVITY_RANDOM"), TEXT("0x102a49da") },
		{ StartTask19A::TaskAddEventExpression, TEXT("TASK_ADD_EVENT_EXPRESSION"), TEXT("0x102a4a07") },
		{ StartTask19A::TaskSetPreservePath, TEXT("TASK_SET_PRESERVE_PATH"), TEXT("0x102a3cfa") },
		{ StartTask19A::TaskSetEnemyEluded, TEXT("TASK_SET_ENEMY_ELUDED"), TEXT("0x102a46fa") },
		{ StartTask19A::TaskSetTargetEluded, TEXT("TASK_SET_TARGET_ELUDED"), TEXT("0x102a4763") },
		{ StartTask19A::TaskGetPathToEnemyClosest, TEXT("TASK_GET_PATH_TO_ENEMY_CLOSEST"), TEXT("0x102a4812") },
		{ StartTask19A::TaskSetInsideInterruptDist, TEXT("TASK_SET_INSIDE_INTERRUPT_DIST"), TEXT("0x102a4a5b") },
		{ StartTask19A::TaskSetOutsideInterruptDist, TEXT("TASK_SET_OUTSIDE_INTERRUPT_DIST"), TEXT("0x102a4a97") },
		{ StartTask19A::TaskSetInterruptTime, TEXT("TASK_SET_INTERRUPT_TIME"), TEXT("0x102a4ad3") },
		{ StartTask19A::TaskAddRandomInterruptTime, TEXT("TASK_ADD_RANDOM_INTERRUPT_TIME"), TEXT("0x102a4afb") },
		{ StartTask19A::TaskClearInterruptTime, TEXT("TASK_CLEAR_INTERRUPT_TIME"), TEXT("0x102a4b2e") },
		{ StartTask19A::TaskWalkRunPath, TEXT("TASK_WALK_RUN_PATH"), TEXT("0x102a4b4e") },
		{ StartTask19A::TaskWalkRunPathCombatSound, TEXT("TASK_WALK_RUN_PATH_COMBAT_SOUND"), TEXT("0x102a4bd8") },
		{ StartTask19A::TaskGetPathToPlayerForDialog, TEXT("TASK_GET_PATH_TO_PLAYER_FOR_DIALOG"), TEXT("0x102a4c7a") },
		{ StartTask19A::TaskWalkRunPathForDialog, TEXT("TASK_WALK_RUN_PATH_FOR_DIALOG"), TEXT("0x102a4db7") },
		{ StartTask19A::TaskStartPlayerDialog, TEXT("TASK_START_PLAYER_DIALOG"), TEXT("0x102a4e7e") },
		{ StartTask19A::TaskSetToleranceDistDlg, TEXT("TASK_SET_TOLERANCE_DIST_DLG"), TEXT("0x102a4c47") },
		{ StartTask19A::TaskDieIfPlayerCantSee, TEXT("TASK_DIE_IF_PLAYER_CANT_SEE"), TEXT("0x102a3198") },
		{ StartTask19A::TaskKnockout, TEXT("TASK_KNOCKOUT"), TEXT("0x102a3339") },
		{ StartTask19A::TaskUnknockout, TEXT("TASK_UNKNOCKOUT"), TEXT("0x102a3364") },
		{ StartTask19A::TaskDoJumpActivity, TEXT("TASK_DO_JUMP_ACTIVITY"), TEXT("0x102a4ec7") },
		{ StartTask19A::TaskDoLoopActivity, TEXT("TASK_DO_LOOP_ACTIVITY"), TEXT("0x102a4ee3") },
		{ StartTask19A::TaskDoBlendActivity, TEXT("TASK_DO_BLEND_ACTIVITY"), TEXT("0x102a4f38") },
		{ StartTask19A::TaskDoBlendLoopActivity, TEXT("TASK_DO_BLEND_LOOP_ACTIVITY"), TEXT("0x102a4fb1") },
	};
	return Rows;
}

// =================================================================================================
// Slot 442 -- `CAI_BaseNPCTroika::StartTask` `0x102a1910`, 24,295 bytes: the prologue and the arms
// in `[0x102a1943, 0x102a5046)`.
// =================================================================================================

int32 FElysiumNpc::StartTaskSlot442(void* Task)
{
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);  // 0x102a191a
	if (Step == nullptr)
	{
		// Crash guard: retail dereferences `[EDI]` at `0x102a1923` unconditionally.
		return FElysiumNpcBase::StartTaskSlot442(Task);
	}
	// `[EDI]` is retail's `Task_t::iTask`, the CLASS-LOCAL id (the parser `0x1030d850` stores
	// `0x102ea280(space+0x18, global)`). This runtime's step carries the GLOBAL id (stated divergence,
	// `ElysiumScheduleText.h`), so it is translated here through this class's task space -- the body
	// of slot 450 `GetLocalTaskId` (`0x101a6640`), which no class overrides. For the root and Troika
	// spaces the local id is the registrar's number the arms compare.
	const int32 TaskId = GlobalToLocalId(IdSpace(EElysiumIdCategory::Task), Step->TaskId);  // 0x102a1923
	// `0x102a1925 LEA ECX,[EAX-5]; CMP ECX,0x144; JA 0x102a77e2` -- above the table is the base / 0x102a192e
	// forward (`0x102a77e2 PUSH EDI; CALL CAI_BaseNPC::StartTask`).
	if (static_cast<uint32>(TaskId - StartTask19A::DispatchFirstId) > StartTask19A::DispatchSpan)
	{
		return FElysiumNpcBase::StartTaskSlot442(Task);
	}
	const double Now = StartTask19A::NowOf(*this);
	const float TaskData = Step->Data;                                   // `[EDI+4]`
	// Slot 167 (`vtable +0x29c`), `CBaseEntity* GetEnemy() const`: the committed enemy. The non-const
	// `GetEnemy()` on this class is slot 168 (`+0x2a0`), the last-enemy accessor.
	const FElysiumNpcBase* const ConstBase = this;

	// `0x102a1936 MOV DL,[ECX+0x102a7ab8]; JMP [EDX*4+0x102a77f8]`: one byte-table index per id and / 0x102a193c
	// 176 arm addresses; the cases below are this half's arms in ascending task id.
	switch (TaskId)
	{
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskSuggestState:
	{
		// Arm `0x102a1b2b` (index 0x01). The only arm that stamps the `+0x1b3c`/`+0x1b40` selector pair.
		RecordScheduleEvent(FString::Printf(TEXT("SuggestState trace :%d"), StartTask19A::LineSuggestState));  // 0x102a1b30
		const int32 Suggested = static_cast<int32>(TaskData);                 // 0x102a1b43 __ftol
		WriteIdealStateRetail(Suggested);                                     // 0x102a1b4e +0x5cc4
		if ((FrenziedWord & 1u) == 1u)                                        // 0x102a1b54 +0x5b84 & 1 / 0x102a1b5a
		{
			if (Suggested == 1)                                               // 0x102a1b5d
			{
				RecordScheduleEvent(FString::Printf(TEXT("SuggestState trace :%d"), StartTask19A::LineSuggestStateIdleFrenzy));
				WriteIdealStateRetail(0xb);                                   // 0x102a1ba6
				return StartTask19Complete();                                 // 0x102a1bb6
			}
			if (Suggested == 3)                                               // 0x102a1b5f SUB 2; JNZ
			{
				RecordScheduleEvent(FString::Printf(TEXT("SuggestState trace :%d"), StartTask19A::LineSuggestStateAlertFrenzy));
				WriteIdealStateRetail(0xb);                                   // 0x102a1b76
				return StartTask19Complete();                                 // 0x102a1b86
			}
			return StartTask19Complete();                                     // 0x102a1b62 -> 0x102a66d7
		}
		if (!bNoAlertState || Suggested != 3)                                 // 0x102a1bc8 +0x65f6; 0x102a1bd6 / 0x102a1bd0 0x102a1bd9
		{
			return StartTask19Complete();                                     // -> 0x102a66d7
		}
		RecordScheduleEvent(FString::Printf(TEXT("SuggestState trace :%d"), StartTask19A::LineSuggestStateNoAlert));
		WriteIdealStateRetail(1);                                             // 0x102a1bed
		return StartTask19Complete();                                         // 0x102a1bfd
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskGetPathToEnemy:
	{
		// Arm `0x102a33f9` (index 0x02).
		if (IsUnreachable(ConstBase->GetEnemy()))                             // 0x102a33fd slot 167; 0x102a3406 slot 530 / 0x102a3410
		{
			return StartTask19Fail(StartTask19A::LinePathToEnemyUnreachable, StartTask19A::FailNoRoute); // 0x102a3414..42a / 0x102a342a
		}
		if (ConstBase->GetEnemy() == nullptr)                                 // 0x102a343f / 0x102a3447
		{
			return StartTask19Fail(StartTask19A::LinePathToEnemyNoEnemy, StartTask19A::FailNoEnemy);     // 0x102a344b..463 / 0x102a3463
		}
		FStartTask19NavGoal Goal;                                             // 0x102a3476..3525
		Goal.Type = 2;                                                        // the enemy; `+0x04` is the
		Goal.Tolerance = StartTask19DefaultTolerance;                         // `DAT_1093404c` sentinel
		if (StartTask19SetGoal(Goal, 0))                                      // 0x102a352c / 0x102a3533
		{
			return StartTask19Complete();                                     // 0x102a3538
		}
		RecordScheduleEvent(TEXT("GetPathToEnemy failed!!\n"));                 // 0x102a3551 DevWarning(2, ...)
		RememberUnreachable(ConstBase->GetEnemy());                           // 0x102a355e / 0x102a3567 0x10274080
		return StartTask19Fail(StartTask19A::LinePathToEnemyNoRoute, StartTask19A::FailNoRoute);         // 0x102a356e..586 / 0x102a3586
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskGetPathToHintNode:
	{
		// Arm `0x102a371d` (index 0x03).
		if (BaseScheduleHost.HintNode == INDEX_NONE)                          // 0x102a371d +0x5ddc / 0x102a3729
		{
			return StartTask19Fail(StartTask19A::LineHintNodeNone, StartTask19A::FailNoHintNode);        // 0x102a372d..743 / 0x102a3743
		}
		FVector HintUnits = FVector::ZeroVector;
		(void)ApplyHintLeanOffset(HintUnits, false);                          // 0x102a375c 0x102b6120(this, &pos, 0)
		FStartTask19NavGoal Goal;                                             // 0x102a3761..3811
		Goal.Type = 4;
		Goal.DestUnits = HintUnits;
		Goal.Activity = StartTask19A::ActRun;
		Goal.Tolerance = StartTask19DefaultTolerance;
		(void)StartTask19SetGoal(Goal, 0);                                    // 0x102a3818, answer dropped
		return 0;                                                             // no complete
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskFaceEnemy:
	{
		// Arm `0x102a4417` (index 0x04).
		FVector PointUnits = FVector::ZeroVector;
		if (const FElysiumEntity* Override = World != nullptr ? World->Resolve(ShootTargetOverride) : nullptr)
		{
			PointUnits = StartTask19A::UnitsOf(Override->Origin);                           // 0x102a4417..446e +0x5ba8, slot 217 / 0x102a4422 0x102a4442 0x102a4447 0x102a4451
		}
		else
		{
			// Slot 541 `GetEnemies()` then `0x102dfed0(memory, &out, GetEnemy())` (thunk `0x10010613`):
			// the memory record's position, `Conditions19LastKnownPosition` (not the enemy's live origin).
			PointUnits = StartTask19A::UnitsOf(Conditions19LastKnownPosition(ConstBase->GetEnemy()));  // 0x102a4486..44a1 / 0x102a4468 0x102a446e 0x102a447a 0x102a4499 0x102a44a1
		}
		if (FInAimCone(StartTask19A::CmOf(PointUnits)))                                     // 0x102a44c3 slot 364
		{
			return StartTask19Complete();                                     // 0x102a44cb -> 0x102a66d7
		}
		return StartTask19TurnTail(PointUnits);                               // 0x102a44d1
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskFaceHintNode:
	{
		// Arm `0x102a382a` (index 0x05).
		++StartTask19MotorStopTurns;
		StartTaskMotorHoldYaw();                                              // 0x102a3830 0x102e0b40
		// `0x102a3841 CMP [hint+0x5dc],0x27d8` -- retail dereferences `m_pHintNode` unguarded; with / 0x102a384b
		// no resolvable hint the port takes the non-lean arm (crash guard).
		FHintWords Hint;
		const bool bLean = BaseScheduleHost.HintNode != INDEX_NONE
			&& HintWords(BaseScheduleHost.HintNode, Hint) && Hint.HintType == StartTask19A::HintTypeCoverLean;
		// `0x100085f8` -> `0x102d12e0`, the hint's yaw: `StartTaskHintYaw` (node yaw or its own angles).
		float Yaw = 0.f;
		(void)StartTaskHintYaw(BaseScheduleHost.HintNode, Yaw);               // 0x102a385b / 0x102a38cb / 0x102a3938 / 0x102a3911 0x102a391e 0x102a3946 0x102a395d
		if (bLean)
		{
			Yaw = bLeaningLeft ? Yaw + StartTask19A::HintLeanDegrees : Yaw - StartTask19A::HintLeanDegrees;  // 0x102a3851 +0x63fd; 0x102a3860 / 0x102a38d0 / 0x102a3859 0x102a38df 0x102a38f6
		}
		// `0x102a3866..0x102a39a4` is `0x10288670` inlined three times: the `motor+0x28` flip by 180, / 0x102a386f 0x102a3886
		// then `motor+0x1c == 180.0f` -> `motor+0x34 = yaw` (`0x102a38a9`), else `0x102e0a80` / 0x102a38a1
		// (`0x102a399f`) -- `StartTaskMotorSetIdealYaw`, which carries both halves. / 0x102a3978 0x102a3985
		StartTaskMotorSetIdealYaw(Yaw);
		SetTurnActivity();                                                    // 0x102a38ae / 0x102a39ab slot 572
		return 0;                                                             // no complete
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskRangeAttack1:
	{
		// Arm `0x102a4505` (index 0x06). `NOT flags2; TEST AH,AH; JNS` -- bit 15 DISABLE_BURST_FIRE.
		if (ActiveWeaponEntity() != nullptr                                   // 0x102a4507 / 0x102a450e
			&& !NpcFlags.Has(EElysiumNpcFlag2::DISABLE_BURST_FIRE))            // 0x102a4510..451a +0x14bc / 0x102a451a 0x102a451e 0x102a4527 0x102a4534
		{
			// `RandomInt(data->+0x3a4, data->+0x3a8)` over the weapon's data (`0x10003d91`).
			BurstFireCount = ElysiumNpcEngineRandom::RandomInt(StartTask19WeaponMinBurst(), StartTask19WeaponMaxBurst());  // 0x102a4549 -> +0x6490
			return 0;
		}
		BurstFireCount = 1;                                                   // 0x102a455f
		return 0;                                                             // no complete
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskRangeAttack2:
	{
		// Arm `0x102a4576` (index 0x07).
		LastAttackTime = Now;                                                 // 0x102a4580 +0x5d9c
		RestartIdealActivityId(StartTask19A::ActRangeAttack2);                              // 0x102a4588
		return 0;
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskMeleeAttack1:
	case StartTask19A::TaskMeleeAttack2:
	{
		// Arm `0x102a45c6` (index 0x08).
		++StartTask19MotorStopTurns;
		StartTaskMotorHoldYaw();                                          // 0x102a45cc 0x102e0b40
		if (ActiveWeaponEntity() != nullptr                                   // 0x102a45d3 / 0x102a45dc
			&& (StartTask19WeaponCapabilityWord() & StartTask19A::MeleeCapabilityMask) != 0)  // 0x102a45e2 weapon +0x5a0; 0x102a45e8 / 0x102a45ed
		{
			LastAttackTime = Now;                                             // 0x102a45f7 +0x5d9c / 0x102a4604
			StartTask19WeaponSwing(TaskId == StartTask19A::TaskMeleeAttack2);         // 0x102a4608 +0x518 / 0x102a4624 +0x51c
			(void)AutoMovement();                                             // 0x102a4610 / 0x102a462c 0x10280a50
			return 0;
		}
		// `0x102a463e +0x1b48 = 0x3302` -> `0x102a46d0`: `TaskFail(0x1f)`, then `AutoMovement`.
		StartTask19Fail(StartTask19A::LineMeleeNoWeapon, StartTask19A::FailNoWeapon);                     // 0x102a46d2..46e0 / 0x102a46e0
		(void)AutoMovement();                                                 // 0x102a46e8
		return 0;
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskSpecialAttack1:
	{
		RestartIdealActivityId(StartTask19A::ActSpecialAttack1);                            // 0x102a459e
		return 0;
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskSpecialAttack2:
	{
		RestartIdealActivityId(StartTask19A::ActSpecialAttack2);                            // 0x102a45b4
		return 0;
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskSetActivity:
	{
		// Arm `0x102a1c0f` (index 0x0b). `act = (int)flTaskData` (`0x102a1c12 __ftol`). The port's
		// activity operand is the corpus's INTERNED id (`FElysiumSymbolRegistry`), so the operand's
		// NAME is translated to retail's `Activity` value (`ActivityList_IndexForName`, the parse
		// retail does at schedule load). Retail's `act == 0` test is `ACT_RESET` or no name at all.
		// A name retail's shared enum does not carry was a PRIVATE activity in retail (registered at
		// `g_HighestActivity + 1` by the loader): no model authors a sequence for it, which is what
		// -1 resolves to through the ladder.
		const FString* ActivityName =
			FElysiumScheduleCorpus::Get().Activities().NameOf(static_cast<int32>(TaskData));
		if (ActivityName == nullptr || ActivityName->IsEmpty()
			|| ActivityName->Equals(TEXT("ACT_RESET"), ESearchCase::IgnoreCase))   // 0x102a1c19 JZ
		{
			ActivityNumber = 0;                                               // 0x102a1c25 +0xfec = 0
		}
		else
		{
			SetIdealActivity(ElysiumRetailActivities::ValueOf(*ActivityName)); // 0x102a1c1e SetIdealActivity
		}
		BaseScheduleHost.WaitFinished = Now + StartTask19A::SetActivityWatchdogSeconds;                     // 0x102a1c38..41 +0x5db4
		if (NpcStateRetail() == 4)                                            // 0x102a1c47 slot 464, CMP 4 / 0x102a1c50
		{
			AdvanceToIdealActivity();                                         // 0x102a1c58 0x102726a0
		}
		return 0;                                                             // no complete, no fail
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskSetToleranceDistance:
	{
		// Arm `0x102a4289` (index 0x0c). `m_flGoalTolerance (+0x6320) = 0x102d61b0(m_eHull +0x1568)
		// * 0.5` (`0x102a4290..42a2`), `+= ResolveTaskDistance(data)` (`0x102a42ac..42c0`).
		float ToleranceUnits = StartTask19HullWidthUnits(HullKind) * StartTask19A::HalfDouble;
		ToleranceUnits += ResolveTaskDistance(TaskData);
		ScheduleHost.GoalToleranceCm = ToleranceUnits * ElysiumMove::U;       // 0x102a42c0
		StartTask19SetNavTolerances(ToleranceUnits, ToleranceUnits);          // 0x102a42cd / 0x102a42df
		return StartTask19Complete();                                         // 0x102a42e8
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskSetToleranceDistanceAbs:
	{
		// Arm `0x102a42fa` (index 0x0d): the resolved distance alone.
		const float ToleranceUnits = ResolveTaskDistance(TaskData);          // 0x102a4302 slot 418
		ScheduleHost.GoalToleranceCm = ToleranceUnits * ElysiumMove::U;       // 0x102a4316
		StartTask19SetNavTolerances(ToleranceUnits, ToleranceUnits);          // 0x102a431d / 0x102a432f
		return StartTask19Complete();                                         // 0x102a4338
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskFindBackawayFromSavePosition:
	{
		// Arm `0x102a1c6a` (index 0x0e).
		const float Distance = ResolveTaskDistance(TaskData);                // 0x102a1c72 slot 418
		FVector NodeCm;
		if (!NearestNavigatorNode(SavePosition, Distance, StartTask19A::BackawayLimitUnits, NodeCm))  // 0x102a1c98 0x102edae0 / 0x102a1c9f
		{
			return StartTask19Fail(StartTask19A::LineBackawayNoNode, StartTask19A::FailNoBackawayNode);  // 0x102a1ca3..cbb / 0x102a1cbb
		}
		FStartTask19NavGoal Goal;                                             // 0x102a1cce..1d7c
		Goal.Type = 4;
		Goal.DestUnits = StartTask19A::UnitsOf(NodeCm);
		Goal.Activity = StartTask19A::ActRun;
		Goal.Tolerance = StartTask19DefaultTolerance;
		if (StartTask19SetGoal(Goal, 0))                                      // 0x102a1d83 / 0x102a1d8c
		{
			return StartTask19Complete();                                     // 0x102a1d8f
		}
		return StartTask19Fail(StartTask19A::LineBackawayNoRoute, StartTask19A::FailNoRoute);            // 0x102a1da3..db9 / 0x102a1db9
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskWaitForMovement:
	{
		// Arm `0x102a1dcc` (index 0x0f). The four-way on the navigator's path, then the teleport
		// rescue, which runs after EVERY arm (each ends `JMP 0x102a1e6a`). The three reads: `0x102ee2e0`
		// is the PAUSED byte (`NavigatorIsPaused`), `0x102ee620` the goal TYPE (`NavGoalState() == 0`,
		// no goal), `0x102ee6a0` `IsGoalActive` (a head waypoint exists).
		if (NavigatorIsPaused())                                              // 0x102a1dd2 0x102ee2e0 path+0x10 m_bPaused / 0x102a1dd9
		{
			// `0x102a1ddd 0x102bf7e0`, inline: `if (path+0x10) 0x102ee2c0` -- which is `0x1030bea0`,
			// `MOV byte [path+0x10],0` (it clears a path byte, it does NOT stop the move; family
			// Motor's `ResumeScheduledMove` reads it as `NavStopMoving` and is not called here) --
			// then `m_bShouldMove = 1` (`0x102bf7fd`).
			Navigator.bPaused = false;                                        // 0x102ee2c0 -> 0x1030bea0
			BaseScheduleHost.bShouldMove = true;
		}
		if (NavGoalState() == 0)                                              // 0x102a1de8 0x102ee620 path+0x5c / 0x102a1df1
		{
			BaseScheduleHost.bShouldMove = false;                             // 0x102a1df6
			TaskComplete(false);                                              // 0x102a1dfc
			StartTaskClearGoal();                                         // 0x102a1e07 0x102ee270 (the path reset, then slot 7)
		}
		else if (!NavIsGoalActive())                                          // 0x102a1e14 0x102ee6a0 / 0x102a1e1b
		{
			BaseScheduleHost.bShouldMove = false;                             // 0x102a1e1f
			SetIdealActivity(ResolveLinkActivity());                          // 0x102a1e25/2d 0x1027a6c0 / 0x102a1e2d
			TaskComplete(false);                                              // 0x102a1e35
		}
		else if (StartTask19NavArrived())                                          // 0x102a1e42 0x102f2ea0 / 0x102a1e4b
		{
			BaseScheduleHost.bShouldMove = false;                             // 0x102a1e4e
			TaskComplete(false);                                              // 0x102a1e54
		}
		else
		{
			BaseScheduleHost.bShouldMove = true;                              // 0x102a1e5d
			(void)ValidateNavGoal();                                          // 0x102a1e64 slot 528
		}
		// `0x102a1e6a FLD curtime; FCOMP [+0x65dc]; TEST AH,0x41; JP 0x102a77ea`: PARITY-EVEN is / 0x102a1e7d
		// `curtime > m_flTeleportMoveTimer`, which returns. The rescue runs while
		// `curtime <= m_flTeleportMoveTimer`.
		if (Now > static_cast<double>(TeleportMoveTimer))
		{
			return 0;
		}
		// `0x102a1ea8 0x102ee140(nav)` -- the navigator's goal point (`ActualGoalPosition`; `(0,0,0)`
		// after a reset, never 'none').
		FVector ProbeUnits = FVector::ZeroVector;
		(void)NavGoalPosition(ProbeUnits);
		// `0x102a1eb0 0x102e7880(moveProbe, goal, 0x202400b, 1.0, -1024.0, &out, &hit)`. / 0x102a1eb7
		(void)StartTask19A::TeleportProbeDownUnits;       // the probe's -1024 reach, the seam's own
		bool bLandedOnNonNpc = false;
		if (!StartTask19TeleportProbe(ProbeUnits, bLandedOnNonNpc))
		{
			return StartTask19Fail(0, StartTask19A::FailBadPosition);                      // 0x102a1eef, no pair / 0x102a1ef3
		}
		if (!bLandedOnNonNpc)                                                 // 0x102a1ebd hit; 0x102a1ec5 hit+0x94 / 0x102a1ebf 0x102a1ecb
		{
			return StartTask19Fail(0, StartTask19A::FailRouteBlocked);                     // 0x102a6ba5, no pair
		}
		FVector ProbedCm = StartTask19A::CmOf(ProbeUnits);
		++StartTask19SetAbsOriginCalls;
		SetOrigin(ProbedCm);                                               // 0x102a1eda slot 216 (0x100b2300 unparented: m_vecOrigin; slot 216 is an unported stub, slot 62's body is the one origin write)
		return 0;
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskGetPathToBestUnknown:
	{
		// Arm `0x102a3599` (index 0x10).
		FElysiumEntity* Unknown = World != nullptr ? World->Resolve(GetBestSeeUnknown()) : nullptr;  // 0x102a35a5 slot 586
		if (Unknown == nullptr)                                               // 0x102a35b2 / 0x102a35cf / 0x102a35d7
		{
			return StartTask19Fail(StartTask19A::LineBestUnknownNone, StartTask19A::FailNoSeenUnknown);  // 0x102a35db..5f3 / 0x102a35f3
		}
		FStartTask19NavGoal Goal;                                             // 0x102a3606..36b3
		Goal.Type = 7;
		Goal.Tolerance = StartTask19DefaultTolerance;
		if (StartTask19SetGoal(Goal, 0))                                      // 0x102a36ba / 0x102a36c1
		{
			return StartTask19Complete();                                     // 0x102a36c6
		}
		RecordScheduleEvent(TEXT("GetPathToBestUnknown failed!!\n"));           // 0x102a36df DevWarning(2, ...)
		RememberUnreachable(Unknown);                                         // 0x102a36eb 0x10274080
		return StartTask19Fail(StartTask19A::LineBestUnknownNoRoute, StartTask19A::FailNoRoute);         // 0x102a36f2..70a / 0x102a370a
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskGetPathToPatrolPoint:
	{
		// Arm `0x102a39be` (index 0x11): `0x102aa640(this, &m_sppPatrolPath (+0x658c))`, which owns
		// the complete/fail.
		StartTask19PatrolPointGoal(false);                                    // 0x102a39c7
		return 0;
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskGetPathToPatrolPointHunt:
	{
		// Arm `0x102a39d9` (index 0x12): the same with `&m_sppPatrolPathHunt (+0x6594)`.
		StartTask19PatrolPointGoal(true);                                     // 0x102a39e2
		return 0;
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskGetFullPatrolPath:
	{
		// Arm `0x102a39f4` (index 0x13). `p = m_sppPatrolPath`'s pointee (`+0x6590`) -- family
		// Script19's `PatrolPathCell.Path` (the pooled `CAI_PatrolPath`), not the port's own route.
		FPatrolPathRecord* const Path = PatrolPathCell.Path;                  // 0x102a39f4 +0x6590
		if (Path == nullptr)                                                  // 0x102a39fe
		{
			return StartTask19Fail(StartTask19A::LineFullPatrolNoPath, StartTask19A::FailNoPatrolPath);  // 0x102a3a02..a1a / 0x102a3a1a
		}
		// `node = p[p->+0x10 * 4 + 0x14]` (`0x102a3a2d..3a33`); -1 returns with the task RUNNING
		// (`0x102a3a39 JZ 0x102a77ea`). Retail does not bound the index; one outside the record's 64
		// slots reads as -1 here (crash guard).
		const int32 NodeId = Path->Current >= 0 && Path->Current < PatrolPathNodeCapacity
			? Path->Nodes[Path->Current] : -1;
		if (NodeId == -1)
		{
			return 0;
		}
		// The network (`nav+0x2c`) bounds test `0x102a3a4a..3a4e`: out of range bumps `DAT_106c994c` / 0x102a3a4e
		// (`0x102a3a58..3a5e`) and hands a NULL node to `CAI_Node::GetPosition(m_eHull +0x1568)`
		// (`0x102a3a86`), which retail dereferences -- crash guard: the task stays RUNNING.
		FVector NodeCm = FVector::ZeroVector;
		const EPatrolNode Node = PatrolNodePosition(NodeId, HullKind, NodeCm);   // m_eHull +0x1568
		if (Node == EPatrolNode::OutOfRange)
		{
			++PatrolNodeMissCounter();                                            // 0x102a3a5e
		}
		if (Node != EPatrolNode::Found)
		{
			return 0;
		}
		FStartTask19NavGoal Goal;                                             // 0x102a3a8b..3b29
		Goal.Type = 4;
		Goal.DestUnits = StartTask19A::UnitsOf(NodeCm);                       // 0x102a3a86 GetPosition
		Goal.Tolerance = StartTask19DefaultTolerance;
		if (StartTask19SetGoal(Goal, 2))                                      // 0x102a3aea PUSH 2; 0x102a3b30 / 0x102a3b39
		{
			return StartTask19Complete();                                     // 0x102a3b3c
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s can't reach patrol point\n"), *DebugString()));  // 0x102a3b5b / 0x102a3b4e
		return StartTask19Fail(StartTask19A::LineFullPatrolNoRoute, StartTask19A::FailNoRoute);          // 0x102a3b68..b7e / 0x102a3b7e
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskNextPatrolPoint:
	{
		// Arm `0x102a3b91` (index 0x14): `0x102aa9e0(this, &m_sppPatrolPath (+0x658c))`. Retail hands
		// the cell itself (never null); `0x102aa9e0` tests its pointee (`arg+0x4`).
		FUN_102aa9e0(&PatrolPathCell);                        // 0x102a3b91 +0x658c; 0x102a3b9a
		return 0;
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskNextPatrolPointHunt:
	{
		// Arm `0x102a3bac` (index 0x15): the same with `&m_sppPatrolPathHunt (+0x6594)`.
		FUN_102aa9e0(&PatrolPathHuntCell);                    // 0x102a3bac +0x6594; 0x102a3bb5
		return 0;
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskGetDirectedPathToEnemyLkp:
	case StartTask19A::TaskGetDirectedPathToEnemyLkpRnd:
	{
		// Arm `0x102a3d48` (index 0x16).
		FElysiumEntity* Enemy = ConstBase->GetEnemy();                         // 0x102a3d4c slot 167 / 0x102a3d5a
		if (Enemy == nullptr)
		{
			return StartTask19Fail(StartTask19A::LineDirectedNoEnemy, StartTask19A::FailNoEnemy);        // 0x102a3d5e..d76 / 0x102a3d76
		}
		float Distance = TaskData;                                            // 0x102a3d89 RAW, not resolved
		if (TaskId == StartTask19A::TaskGetDirectedPathToEnemyLkpRnd)                 // 0x102a3d92 CMP 0x80 / 0x102a3d97
		{
			Distance = StartTask19A::RandomFloat(Distance * StartTask19A::Half, Distance);                // 0x102a3da7..3db4 / 0x102a3db4
		}
		FVector LkpUnits, SeenUnits;
		if (!StartTask19EnemyLkp(*Enemy, LkpUnits, SeenUnits))                // 0x102a3dca slot 541; 0x102a3dd2 0x102e0290 / 0x102a3dd9
		{
			return StartTask19Fail(StartTask19A::LineDirectedNoLkp, StartTask19A::FailNoEnemy);          // 0x102a3f59..f71 / 0x102a3f71
		}
		FVector PointUnits = FVector::ZeroVector;
		const bool bPoint = StartTask19DirectedPathPoint(LkpUnits, SeenUnits, Distance, PointUnits);  // 0x102a3dfa 0x102ee300
		// The literal (`0x102a3dff..3eac`), then slot 563 `TranslateEnemyChasePosition(enemy,
		// &goal.dest, &goal.tolerance, &tolerance)` whether or not the point was found; `tolerance`
		// is a copy of `m_flGoalTolerance` (`+0x6320`, `0x102a3e35`).
		FStartTask19NavGoal Goal;
		Goal.Type = 4;
		Goal.Tolerance = StartTask19DefaultTolerance;
		FVector ChaseCm = StartTask19A::CmOf(PointUnits);
		float GoalTolerance = Goal.Tolerance;
		float Tolerance = ScheduleHost.GoalToleranceCm / ElysiumMove::U;
		TranslateEnemyChasePosition(Enemy, ChaseCm, &GoalTolerance, &Tolerance);  // 0x102a3eb3 slot 563
		Goal.DestUnits = StartTask19A::UnitsOf(ChaseCm);
		Goal.Tolerance = GoalTolerance;
		if (bPoint && StartTask19SetGoal(Goal, 2))                            // 0x102a3ebb; 0x102a3ec7 PUSH 2; 0x102a3eca / 0x102a3ed1
		{
			StartTask19SetNavTolerances(Tolerance, Tolerance);                // 0x102a3ede 0x102ee1c0; 0x102a3eee 0x102f2fe0
			return StartTask19Complete();                                     // 0x102a3ef6
		}
		RecordScheduleEvent(TEXT("GetDirectedPathToEnemyLKP failed!!\n"));      // 0x102a3f0f DevWarning(2, ...)
		RememberUnreachable(ConstBase->GetEnemy());                           // 0x102a3f1c / 0x102a3f25
		return StartTask19Fail(StartTask19A::LineDirectedNoRoute, StartTask19A::FailNoRoute);            // 0x102a3f2c..f44 / 0x102a3f44
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskGetDirectedPathToEnemyLkpLos:
	{
		// Arm `0x102a3f84` (index 0x17).
		FElysiumEntity* Enemy = ConstBase->GetEnemy();                         // 0x102a3f88 slot 167 / 0x102a3f92
		if (Enemy == nullptr)
		{
			return StartTask19Fail(StartTask19A::LineDirectedLosNoEnemy, StartTask19A::FailNoEnemy);     // 0x102a3f96..fae / 0x102a3fae
		}
		FVector LkpUnits, SeenUnits;
		if (!StartTask19EnemyLkp(*Enemy, LkpUnits, SeenUnits))                // 0x102a3fd3 / 0x102a3fdb 0x102e0290 / 0x102a3fe2
		{
			return StartTask19Fail(StartTask19A::LineDirectedLosNoLkp, StartTask19A::FailNoEnemy);       // 0x102a425e..276 / 0x102a4276
		}
		FVector DirectedUnits;
		if (!StartTask19DirectedPathPoint(LkpUnits, SeenUnits, TaskData, DirectedUnits))  // 0x102a4005 0x102ee300 / 0x102a400e
		{
			return StartTask19Fail(StartTask19A::LineDirectedLosNoPoint, StartTask19A::FailNoShootPosition);  // 0x102a4233..249 / 0x102a421f 0x102a4249
		}
		float MaxRange = StartTask19A::FlankDefaultMaxUnits;                                // 0x102a4014
		float MinRange = 0.f;                                                 // 0x102a401c
		if (ActiveWeaponEntity() != nullptr)                                  // 0x102a4024 / 0x102a402b 0x102a4033
		{
			const float Max0 = StartTask19WeaponRangeWord(0x8c0);             // 0x102a4038 / 0x102a4044
			const float Max1 = StartTask19WeaponRangeWord(0x8c4);             // 0x102a4049
			MaxRange = Max1 < Max0 ? Max0 : Max1;                             // 0x102a405a JP / 0x102a405c 0x102a407a
			const float Min0 = StartTask19WeaponRangeWord(0x8b8);             // 0x102a407f / 0x102a4069 0x102a408b
			const float Min1 = StartTask19WeaponRangeWord(0x8bc);             // 0x102a4090
			MinRange = Min1 <= Min0 ? Min1 : Min0;                            // 0x102a40a3: the SMALLER / 0x102a40a5
		}
		if (!(MaxRange <= DistTooFar))                                        // 0x102a40c5 +0x5de4 / 0x102a40b2 0x102a40d2
		{
			MaxRange = DistTooFar;                                            // 0x102a40d4 / 0x102a40e2
		}
		// The enemy's `m_vecViewOffset` (`+0x184`) over the directed point (`0x102a40e8..412e`).
		const FVector EyeUnits = DirectedUnits + StartTask19A::UnitsOf(Enemy->EyePosition() - Enemy->Origin);
		FVector PositionUnits;
		if (!StartTask19FindLosPosition(DirectedUnits, EyeUnits, MinRange, MaxRange, nullptr,
			PositionUnits))                                                   // 0x102a415a 0x102edaa0
		{
			return 0;                                                         // 0x102a4161 JZ 0x102a77ea: no fail
		}
		return StartTask19SharedRunGoal(PositionUnits, 2);                    // 0x102a4176 PUSH 2 -> 0x102a4186
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskGetPathToFleeNode:
	case StartTask19A::TaskGetPathToCowerNode:
	{
		// Arm `0x102a2882` (index 0x18).
		FElysiumEntity* TargetEntity = ConstBase->GetEnemy();                        // 0x102a2886 slot 167 / 0x102a2894
		if (TargetEntity == nullptr)
		{
			TargetEntity = this;                                                    // 0x102a2896
		}
		const float Distance = ResolveTaskDistance(TaskData);                // 0x102a28a4 slot 418
		const float MaxSearch = Distance + StartTask19A::HintSearchPadUnits;                // 0x102a28ae
		const float MidSearch = (MaxSearch + Distance) * StartTask19A::Half;                // 0x102a28c0..c4 / 0x102a28ce
		if (BaseScheduleHost.HintNode != INDEX_NONE)                          // 0x102a28b4 +0x5ddc
		{
			ClearScheduleHint(StartTask19A::HintClaimReleaseSeconds);                       // 0x102a28d7 0x10295ab0
		}
		BaseScheduleHost.HintNode = FindHintNear(static_cast<int32>(StartTask19A::CowerHintType), StartTask19A::CowerHintFlags,
			MaxSearch);                                                       // 0x102a28ed 0x102d1af0; 0x102a28fa
		if (BaseScheduleHost.HintNode != INDEX_NONE)                          // 0x102a2900
		{
			if (!ClaimHintNode(BaseScheduleHost.HintNode))                    // 0x102a2909 0x102d1350 / 0x102a2910
			{
				BaseScheduleHost.HintNode = INDEX_NONE;                       // 0x102a2912
			}
			else
			{
				// The claim wrote the live hint's `m_hHintOwner`; nothing on the NPC records it.
				FVector HintUnits = FVector::ZeroVector;
				(void)HintStandPosition(BaseScheduleHost.HintNode, HintUnits);  // 0x102a292d 0x102d1180
				FStartTask19NavGoal Goal;                                     // 0x102a2932..29de
				Goal.Type = 4;
				Goal.DestUnits = HintUnits;
				Goal.Activity = StartTask19A::ActRun;
				Goal.Tolerance = StartTask19DefaultTolerance;
				if (StartTask19SetGoal(Goal, 0))                              // 0x102a29e5 / 0x102a29ee
				{
					ScheduleHost.SavedSleepExtents = AttackExtentsCm;         // 0x102a29fa slot 16 -> +0x65d0
					// `0x102a2a42 SetAbsoluteAttackExtents((40,40,80))`, `0x1009b060`: the margin is
					// the absolute box less half this body's collision size.
					FVector CollisionMins = FVector::ZeroVector;
					FVector CollisionMaxs = FVector::ZeroVector;
					RetailCollisionExtents(*this, CollisionMins, CollisionMaxs);
					SetAttackExtents(StartTask19A::CmOf(StartTask19A::CowerAttackExtentsUnits - (CollisionMaxs - CollisionMins) * StartTask19A::Half));
					TaskComplete(false);                                      // 0x102a2a4b
				}
				else
				{
					ClearScheduleHint(StartTask19A::HintGoalReleaseSeconds);                // 0x102a2a57
				}
			}
		}
		if (BaseScheduleHost.HintNode != INDEX_NONE)                          // 0x102a2a60 JNZ 0x102a77ea / 0x102a2a68
		{
			return 0;
		}
		if (TaskId == StartTask19A::TaskGetPathToCowerNode)                           // 0x102a2a6e CMP [EDI],0x84 / 0x102a2a74
		{
			NpcFlags.Set(EElysiumNpcFlag::COWER_PATH);                        // 0x102a2a7c OR AH,2 / 0x102a2aa7
		}
		const FVector OriginUnits = StartTask19A::UnitsOf(TargetEntity->Origin);                  // slot 220
		const FVector EyeUnits = StartTask19A::UnitsOf(TargetEntity->EyePosition());              // slot 193
		FVector CoverUnits;
		if (!StartTask19FindCoverPos(OriginUnits, EyeUnits, MidSearch, MaxSearch, CoverUnits)  // 0x102a2abb / 0x102a2ab2 0x102a2ac2 0x102a2ae6
			&& !StartTask19FindCoverPos(OriginUnits, EyeUnits, Distance, MaxSearch, CoverUnits))  // 0x102a2afa / 0x102a2af1 0x102a2b01
		{
			return StartTask19Fail(StartTask19A::LineFleeNoNode, StartTask19A::FailNoReachableNodes);    // 0x102a2bad..bc5 / 0x102a2bc5
		}
		FStartTask19NavGoal Goal;                                             // 0x102a2b07..2b92
		Goal.Type = 6;
		Goal.DestUnits = CoverUnits;
		Goal.Activity = StartTask19A::ActRun;
		Goal.Tolerance = StartTask19HullTolerance;
		(void)StartTask19SetGoal(Goal, 0);                                    // 0x102a2b99, answer dropped
		return 0;
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskGetPathToCowerNodeSavePos:
	{
		// Arm `0x102a2bd8` (index 0x19).
		const float Distance = ResolveTaskDistance(TaskData);                // 0x102a2be0 slot 418
		const float MaxSearch = Distance + StartTask19A::HintSearchPadUnits;                // 0x102a2bea
		NpcFlags.Set(EElysiumNpcFlag::COWER_PATH);                            // 0x102a2bfc, unconditional
		const FVector FromUnits = StartTask19A::UnitsOf(SavePosition);                      // 0x102a2bf6 +0x5dd0
		const FVector ToUnits = FromUnits + StartTask19A::UnitsOf(EyePosition() - Origin);  // 0x102a2c17.. +0x184 view offset
		FVector CoverUnits;
		if (!StartTask19FindCoverPos(FromUnits, ToUnits, (MaxSearch + Distance) * StartTask19A::Half, MaxSearch,
				CoverUnits)                                                   // 0x102a2c8c / 0x102a2c95
			&& !StartTask19FindCoverPos(FromUnits, ToUnits, Distance, MaxSearch, CoverUnits))  // 0x102a2cb3 / 0x102a2cba
		{
			return StartTask19Fail(StartTask19A::LineCowerSaveNoNode, StartTask19A::FailNoReachableNodes);  // 0x102a2d70..d88 / 0x102a2d88
		}
		FStartTask19NavGoal Goal;                                             // 0x102a2cc0..2d55
		Goal.Type = 6;
		Goal.DestUnits = CoverUnits;
		Goal.Activity = StartTask19A::ActRun;
		Goal.Tolerance = StartTask19HullTolerance;
		(void)StartTask19SetGoal(Goal, 0);                                    // 0x102a2d5c, answer dropped
		return 0;
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskFindFollowerBackawaySimple:
	{
		// Arm `0x102a2d9b` (index 0x1a). `m_hFollowerBoss` (`+0x647c`) through the handle table.
		const FElysiumEntity* Boss = World != nullptr ? World->Resolve(FollowerBoss) : nullptr;
		if (Boss == nullptr)                                                  // 0x102a2da6 / 0x102a2dc7 / 0x102a2dd3
		{
			return StartTask19Fail(StartTask19A::LineFollowerSimpleNoBoss, StartTask19A::FailNoFollowerBoss);  // 0x102a2f69..f81 / 0x102a2f81
		}
		const FVector BossUnits = StartTask19A::UnitsOf(Boss->Origin);                      // 0x102a2ddb slot 217
		const FVector SelfUnits = StartTask19A::UnitsOf(Origin);                            // 0x102a2df9 slot 217
		// Away from the boss: `UTIL_VecToYaw(self - boss)` (`0x102a2e54`) jittered by
		// `RandomFloat(-45, 45)` (`0x102a2e72`), then `self + m_flFollowerDistanceBackAway (+0x6484) *
		// UTIL_YawToVector(yaw)` (`0x102a2e94..2ef0`). The base of the sum is this body's own origin
		// (`[ESP+0x28]`), not the boss's (walk-19-29-pack-13 section 27 corrected).
		float Yaw = StartTask19A::VecToYaw(SelfUnits - BossUnits);
		Yaw += StartTask19A::RandomFloat(-StartTask19A::FollowerJitterDegrees, StartTask19A::FollowerJitterDegrees);
		const FVector CandidateUnits = SelfUnits + StartTask19A::YawToVector(Yaw) * FollowerDistanceBackAway;
		// `0x102a2f1f 0x102e6d70(moveProbe, 0, self, candidate, 0x202400b, 0, 100.0, 0, &trace, 0, 0)`. / 0x102a2f26
		FMotorMoveTrace Trace;
		if (!MotorMoveTraceSweep(0, SelfUnits, CandidateUnits, static_cast<int32>(StartTask19A::TeleportProbeMask),
			StartTask19A::FollowerProbeExtentUnits, nullptr, Trace))
		{
			return StartTask19Fail(StartTask19A::LineFollowerSimpleProbe, StartTask19A::FailNoBackawayNode);  // 0x102a2f3c..f54 / 0x102a2f54
		}
		return StartTask19SharedRunGoal(CandidateUnits, 0);                   // 0x102a2f34 PUSH 0 -> 0x102a4178
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskFindFollowerBackawayNode:
	{
		// Arm `0x102a2f94` (index 0x1b).
		const FElysiumEntity* Boss = World != nullptr ? World->Resolve(FollowerBoss) : nullptr;
		if (Boss == nullptr)                                                  // 0x102a2f9f / 0x102a2fc0 / 0x102a2fcc / 0x102a2fd4
		{
			return StartTask19Fail(StartTask19A::LineFollowerNodeNoBoss, StartTask19A::FailNoFollowerBoss);  // 0x102a306b..083 / 0x102a3083
		}
		FVector NodeCm;
		if (!NearestNavigatorNode(Boss->Origin, FollowerDistanceWalkTo - StartTask19A::FollowerWalkPadUnits,
			StartTask19A::FollowerSearchMaxUnits, NodeCm))                                  // 0x102a2fdc +0x6488; 0x102a3013 0x102edae0 / 0x102a301a
		{
			return StartTask19Fail(StartTask19A::LineFollowerNodeNoNode, StartTask19A::FailNoBackawayNode);  // 0x102a303e..056 / 0x102a3056
		}
		return StartTask19SharedRunGoal(StartTask19A::UnitsOf(NodeCm), 0);                  // 0x102a3036 PUSH 0 -> 0x102a4186
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskFindFollowerBackawayAStar:
	{
		// Arm `0x102a3096` (index 0x1c).
		const FElysiumEntity* Boss = World != nullptr ? World->Resolve(FollowerBoss) : nullptr;
		if (Boss == nullptr)                                                  // 0x102a30a1 / 0x102a30c2 / 0x102a30ce / 0x102a30d6
		{
			return StartTask19Fail(StartTask19A::LineFollowerAStarNoBoss, StartTask19A::FailNoFollowerBoss);  // 0x102a316d..185 / 0x102a3185
		}
		FVector NodeUnits;
		if (!StartTask19FindBackawayAStar(StartTask19A::UnitsOf(Boss->Origin),
			FollowerDistanceWalkTo - StartTask19A::FollowerWalkPadUnits, StartTask19A::FollowerSearchMaxUnits, NodeUnits))  // 0x102a3115 0x102edbb0 / 0x102a311c
		{
			return StartTask19Fail(StartTask19A::LineFollowerAStarNoNode, StartTask19A::FailNoBackawayNode);  // 0x102a3140..158 / 0x102a3158
		}
		return StartTask19SharedRunGoal(NodeUnits, 0);                        // 0x102a3138 PUSH 0 -> 0x102a4186
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskMeleeKick:
	{
		// Arm `0x102a464d` (index 0x26).
		++StartTask19MotorStopTurns;
		StartTaskMotorHoldYaw();                                          // 0x102a4653 0x102e0b40
		const bool bWeapon = ActiveWeaponEntity() != nullptr;                // 0x102a465a
		const bool bCast = StartTask19KickWeaponCast();                       // 0x102a4670 __RTDynamicCast
		if (bWeapon && ((StartTask19WeaponCapabilityWord() >> 30) & 1u) != 0 && bCast)  // 0x102a467c / 0x102a468d / 0x102a4691 / 0x102a4682
		{
			LastAttackTime = Now;                                             // 0x102a46a4
			++StartTask19KickDispatches;                                      // 0x102a46ac cast->+0x5d0(0x53, 0, 0)
			(void)AutoMovement();                                             // 0x102a46b4
			return 0;
		}
		StartTask19Fail(StartTask19A::LineKickNoWeapon, StartTask19A::FailNoWeapon);                      // 0x102a46c6 -> 0x102a46d0
		(void)AutoMovement();                                                 // 0x102a46e8
		return 0;
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskSetMeleeToleranceDistance:
	{
		// Arm `0x102a434a` (index 0x2b). The hull is the ENEMY's (`enemy+0x9c` then `+0x1568`), 0
		// with no enemy (`0x102a434e XOR EBX,EBX`). The port reads it off the enemy's NPC base; a
		// player enemy's `+0x1568` word is unrecovered and reads 0.
		int32 EnemyHull = 0;
		if (FElysiumEntity* Enemy = ConstBase->GetEnemy())                     // 0x102a4350 slot 167 / 0x102a4358
		{
			if (const FElysiumNpcBase* EnemyNpc = Enemy->AsNpcBase())          // 0x102a435a +0x9c / 0x102a4362
			{
				EnemyHull = EnemyNpc->HullKind;                               // 0x102a4364 +0x1568
			}
		}
		float ToleranceUnits = StartTask19HullWidthUnits(EnemyHull) * StartTask19A::HalfDouble;  // 0x102a4376 / 0x102a43e2 / 0x102a438c
		if (ActiveWeaponEntity() != nullptr)                                  // 0x102a436c / 0x102a4374
		{
			// `weapon+0x8c0 * flTaskData` -- the RAW operand, not resolved (`0x102a4391..4397`).
			ToleranceUnits += StartTask19WeaponRangeWord(0x8c0) * TaskData;
		}
		else
		{
			ToleranceUnits += ResolveTaskDistance(TaskData);                 // 0x102a43fe
		}
		ScheduleHost.GoalToleranceCm = ToleranceUnits * ElysiumMove::U;       // 0x102a43ae / 0x102a42c0
		StartTask19SetNavTolerances(ToleranceUnits, ToleranceUnits);          // 0x102a43b5 / 0x102a43c7
		return StartTask19Complete();                                         // 0x102a43d0
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskFindFastCoverFromEnemy:
	{
		// Arm `0x102a20dc` (index 0x2c).
		FElysiumEntity* Threat = ConstBase->GetEnemy();                        // 0x102a20e0 slot 167 / 0x102a20e8 0x102a20f2 (slot 167 re-read)
		if (Threat == nullptr)
		{
			Threat = this;                                                    // 0x102a20ea
		}
		if (StartTaskFindLateralCover(Threat->EyePosition(), Threat))                 // 0x102a2108 slot 193; 0x102a2111 0x102784a0 / 0x102a2118
		{
			BaseScheduleHost.MoveWaitFinished = Now + TaskData;               // 0x102a211a -> 0x102a66ce / 0x102a2148
			return StartTask19Complete();                                     // 0x102a66d7
		}
		FVector CoverUnits;
		if (!StartTask19FindCoverPos(StartTask19A::UnitsOf(Threat->Origin), StartTask19A::UnitsOf(Threat->EyePosition()), 0.f,
			StartTask19A::FastCoverMaxUnits, CoverUnits))                                   // 0x102a215d 0x102edc80 / 0x102a2154 0x102a2164
		{
			return StartTask19Fail(StartTask19A::LineFastCoverNone, StartTask19A::FailNoCover);          // 0x102a2206..21e / 0x102a221e
		}
		FStartTask19NavGoal Goal;                                             // 0x102a216a..21fe
		Goal.Type = 6;
		Goal.DestUnits = CoverUnits;
		Goal.Activity = StartTask19A::ActRun;
		Goal.Tolerance = StartTask19HullTolerance;
		return StartTask19GoalThenMoveWait(Goal, TaskData);                  // 0x102a21ff -> 0x102a76a4
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskFindForwardCoverFromEnemy:
	{
		// Arm `0x102a232e` (index 0x2d).
		FElysiumEntity* Threat = ConstBase->GetEnemy();                        // 0x102a2332 slot 167 / 0x102a233a 0x102a2344 (slot 167 re-read)
		if (Threat == nullptr)
		{
			Threat = this;                                                    // 0x102a233c
		}
		if (StartTaskFindLateralCover(Threat->EyePosition(), Threat))                 // 0x102a235a / 0x102a2363 / 0x102a236c
		{
			BaseScheduleHost.MoveWaitFinished = Now + TaskData;               // 0x102a236e..37c +0x5cf0
			return StartTask19Complete();                                     // 0x102a2382
		}
		FVector CoverUnits;
		if (!StartTask19FindForwardCover(StartTask19A::UnitsOf(Threat->Origin), StartTask19A::UnitsOf(Threat->EyePosition()),
			Forward, 0.f, CoverRadius() * StartTask19A::Quarter, CoverUnits))               // 0x102a23a2..23dd 0x102edd50 / 0x102a23c8
		{
			return StartTask19Fail(StartTask19A::LineForwardCoverNone, StartTask19A::FailNoCover);       // 0x102a2486..49e / 0x102a249e
		}
		FStartTask19NavGoal Goal;                                             // 0x102a23ea..247e / 0x102a23d4 0x102a23dd 0x102a23e4
		Goal.Type = 6;
		Goal.DestUnits = CoverUnits;
		Goal.Activity = StartTask19A::ActRun;
		Goal.Tolerance = StartTask19HullTolerance;
		return StartTask19GoalThenMoveWait(Goal, TaskData);                  // 0x102a247f -> 0x102a76a4
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskFindCoverFromSavePosition:
	{
		// Arm `0x102a2231` (index 0x2e): `m_vSavePosition` (`+0x5dd0`) as both threat points.
		const FVector SaveUnits = StartTask19A::UnitsOf(SavePosition);
		FVector CoverUnits;
		if (!StartTask19FindCoverPos(SaveUnits, SaveUnits, StartTask19A::SaveCoverMinUnits, CoverRadius(),
			CoverUnits))                                                      // 0x102a2247 slot 550; 0x102a225a / 0x102a2261
		{
			return StartTask19Fail(StartTask19A::LineSavePositionCoverNone, StartTask19A::FailNoCover);  // 0x102a2303..31b / 0x102a231b
		}
		FStartTask19NavGoal Goal;                                             // 0x102a2267..22fb
		Goal.Type = 4;
		Goal.DestUnits = CoverUnits;
		Goal.Activity = StartTask19A::ActRun;
		Goal.Tolerance = StartTask19HullTolerance;
		return StartTask19GoalThenMoveWait(Goal, TaskData);                  // 0x102a22fc -> 0x102a76a4
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskFindFlankNodeToEnemy:
	{
		// Arm `0x102a24b1` (index 0x2f).
		FElysiumEntity* Enemy = ConstBase->GetEnemy();                         // 0x102a24b5 slot 167 / 0x102a24bf
		if (Enemy == nullptr)
		{
			return StartTask19Fail(StartTask19A::LineFlankNoEnemy, StartTask19A::FailNoEnemy);           // 0x102a24c3..4d9 / 0x102a24d9
		}
		// Slot 541 then `0x102dfed0` -- the memory record (`Conditions19LastKnownPosition`), not the live origin.
		const FVector LkpUnits = StartTask19A::UnitsOf(Conditions19LastKnownPosition(Enemy));  // 0x102a24fe slot 541; 0x102a2506 0x102dfed0 / 0x102a24ee slot 167 again
		float MaxRange = StartTask19A::FlankDefaultMaxUnits;                                // 0x102a250d
		float MinRange = 0.f;                                                 // 0x102a2515
		if (ActiveWeaponEntity() != nullptr)                                  // 0x102a251d GetActiveWeapon / 0x102a2524 0x102a252c
		{
			const float Max0 = StartTask19WeaponRangeWord(0x8c0);             // 0x102a2531 / 0x102a253d
			const float Max1 = StartTask19WeaponRangeWord(0x8c4);             // 0x102a2542
			MaxRange = Max1 < Max0 ? Max0 : Max1;                             // 0x102a2553 JP / 0x102a2555 0x102a2573
			const float Min0 = StartTask19WeaponRangeWord(0x8b8);             // 0x102a2578 / 0x102a2562 0x102a2584
			const float Min1 = StartTask19WeaponRangeWord(0x8bc);             // 0x102a2589
			MinRange = Min1 <= Min0 ? Min1 : Min0;                            // 0x102a259c JNZ: the SMALLER / 0x102a259e
		}
		if (!(MaxRange <= DistTooFar))                                        // 0x102a25be FCOMP +0x5de4 / 0x102a25ab 0x102a25cb
		{
			MaxRange = DistTooFar;                                            // 0x102a25cd / 0x102a25db 0x102a25e6
		}
		// `enemy + 0x184` is the enemy's `m_vecViewOffset`: the threat's eye above its LKP.
		const FVector EyeUnits = LkpUnits + StartTask19A::UnitsOf(Enemy->EyePosition() - Enemy->Origin);  // 0x102a2645..
		const FElysiumNpcEnemyMemoryRecord* Record = EnemyMemory.Find(BaseMemory.Enemy);   // 0x102a25ee 0x102e0150
		const double LastSeen = Record != nullptr ? Record->LastSeenTime : 0.0;
		FVector PositionUnits;
		bool bFound;
		if (Now - LastSeen >= StartTask19A::FlankFreshSeconds)                              // 0x102a2602 FCOMP 2.0; JP / 0x102a260f 0x102a2615
		{
			bFound = StartTask19FindLosPosition(LkpUnits, EyeUnits, MinRange, MaxRange, nullptr,
				PositionUnits);                                               // 0x102a2739 0x102edaa0 / 0x102a26c0
		}
		else
		{
			// `0x102a2628` slot 219 `GetAbsAngles` -> `0x102a262f 0x10139610` AngleVectors (Source's / 0x102a263b
			// pitch/yaw in degrees, as `FElysiumEntity::Angles` carries them).
			const double Pitch = FMath::DegreesToRadians(static_cast<double>(Enemy->Angles.X));
			const double Yaw = FMath::DegreesToRadians(static_cast<double>(Enemy->Angles.Y));
			const FVector EnemyForward(FMath::Cos(Pitch) * FMath::Cos(Yaw),
				FMath::Cos(Pitch) * FMath::Sin(Yaw), -FMath::Sin(Pitch));
			bFound = StartTask19FindLosPosition(LkpUnits, EyeUnits, MinRange, MaxRange, &EnemyForward,
				PositionUnits);                                               // 0x102a26b9 0x102ed9c0
		}
		if (!bFound)                                                          // 0x102a2740
		{
			return StartTask19Fail(StartTask19A::LineFlankNoPosition, StartTask19A::FailNoShootPosition);  // 0x102a2857..86f / 0x102a2843 0x102a286f
		}
		FStartTask19NavGoal Goal;                                             // 0x102a2746..27e0
		Goal.Type = 4;
		Goal.DestUnits = PositionUnits;
		Goal.Activity = StartTask19A::ActRun;
		Goal.Tolerance = StartTask19HullTolerance;
		(void)StartTask19SetGoal(Goal, 2);                                    // 0x102a279d PUSH 2; 0x102a27eb
		StartTask19SetArrivalDirection(LkpUnits - PositionUnits);            // 0x102a27f0..2843 0x102ee530
		return 0;                                                             // no complete
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskFindInterestingPlace:
	{
		// Arm `0x102a1f23` (index 0x30). `+0x62ec = PickRandomInterestingPlace(this)` (`0x102db590`,
		// each candidate tested by the eligibility `0x102dad60`), then `PickSpotFor(place, this,
		// &m_vecInterestingPlace, 1)` (`0x102da0d0`). `ClaimAmbientSpot` is the port of those three:
		// the place's own visitor claim and the `CurrentSpotIndex` (`+0x62ec`) write, no other hold.
		FElysiumInterestingPlace* Place = ClaimAmbientSpot();                // 0x102a1f24
		if (Place == nullptr)                                                 // 0x102a1f34
		{
			CurrentSpotIndex = INDEX_NONE;                                    // 0x102a1f2c
			return StartTask19Fail(StartTask19A::LineInterestingPlaceNone, StartTask19A::FailNoInterestingPlace);  // 0x102a1f98..fb0 / 0x102a1f83 0x102a1fb0
		}
		// `0x102a1f42 PickSpotFor(place, this, &m_vecInterestingPlace (+0x62f0), 1)`: the claim held, / 0x102a1f4b
		// so the spot is the place's own position. Its refusal arm (`+0x62ec = 0`, line 0x2eaa) is
		// the claim `ClaimAmbientSpot` already refused.
		(void)StartTask19A::LineInterestingPlaceNoSpot;   // the refusal arm's line, unreachable here
		InterestingPlacePosition = StartTask19A::UnitsOf(Place->Origin);
		return StartTask19Complete();                                         // 0x102a1f4f
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskGetPathToInterestingPlace:
	{
		// Arm `0x102a1fc3` (index 0x31).
		if (CurrentSpotIndex == INDEX_NONE)                                   // 0x102a1fc3 +0x62ec / 0x102a1fcd
		{
			return StartTask19Fail(StartTask19A::LineInterestingPlaceLost, StartTask19A::FailNoInterestingPlace);  // 0x102a20b1..c9 / 0x102a20c9
		}
		FStartTask19NavGoal Goal;                                             // 0x102a1fd3..205f
		Goal.Type = 8;
		Goal.DestUnits = InterestingPlacePosition;
		Goal.Tolerance = StartTask19DefaultTolerance;
		if (StartTask19SetGoal(Goal, 0))                                      // 0x102a2066 / 0x102a206f
		{
			return StartTask19Complete();                                     // 0x102a2072
		}
		return StartTask19Fail(StartTask19A::LineInterestingPlaceNoRoute, StartTask19A::FailNoRoute);    // 0x102a2086..9c / 0x102a209c
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskPauseMoving:
	{
		// Arm `0x102a1f06` (index 0x32).
		StopScheduledMove();                                                  // 0x102a1f08 0x102bf770
		return StartTask19Complete();                                         // 0x102a1f11
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskTest1:
	{
		// Arm `0x102a1943` (index 0x33), the developer arm.
		if (ActiveWeaponEntity() != nullptr)                                  // 0x102a1945 GetActiveWeapon / 0x102a194c
		{
			if (StartTask19DebugTestSwitch1 != 0)                             // 0x102a1956 IsCommand; 0x102a1963 +0x2c / 0x102a195b 0x102a1968 0x102a196c
			{
				++StartTask19WeaponUnhideCalls;                               // 0x102a1975 weapon +0x10c / 0x102a197f
			}
			else
			{
				++StartTask19WeaponHideCalls;                                 // 0x102a1988 weapon +0x108
			}
		}
		FVector PointUnits = StartTask19A::UnitsOf(Origin);                                 // 0x102a1992 slot 217
		PointUnits.X += StartTask19A::RandomFloat(-StartTask19A::TestJitterUnits, StartTask19A::TestJitterUnits);       // 0x102a19be
		PointUnits.Y += StartTask19A::RandomFloat(-StartTask19A::TestJitterUnits, StartTask19A::TestJitterUnits);       // 0x102a19db
		// `0x102a1a53 0x10142aa0(&point, &(-2,-2,-2), &(2,2,2), 0xc0, 0xff, 0xc0, 0, 2.0)` -- a
		// debug-overlay box. Debug drawing only; counted.
		++StartTask19DebugBoxes;
		(void)StartTask19A::TestBoxHalfUnits;
		return StartTask19TurnTail(PointUnits);                               // 0x102a1a5b -> 0x102a44d1 / 0x102a44d7
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskTest2:
	{
		// Arm `0x102a1a60` (index 0x34). `value = cv->vtable[4]() ? 0 : cv->m_nValue`, then / 0x102a1a68 0x102a1a6d
		// `DEC; CMP 3; JA` over the four-entry table at `0x102a7c00` (the packet's 22-entry reading of
		// `0x102a1a86` runs into the neighbouring tables).
		int32 Activity = StartTask19A::ActIdle;                                             // 0x102a1b0c default / 0x102a1b10 0x102a1b19
		switch (StartTask19DebugTestSwitch2)                                  // 0x102a1a73 +0x2c / 0x102a1a80
		{
		case 1: Activity = 5; break;                                          // 0x102a1a8d / 0x102a1a91 0x102a1a9a
		case 2: Activity = 0x1118; break;                                     // 0x102a1aac / 0x102a1ab3 0x102a1abc
		case 3: Activity = 0x19; break;                                       // 0x102a1ace / 0x102a1ad2 0x102a1adb
		case 4: Activity = 0x58; break;                                       // 0x102a1aed / 0x102a1af1 0x102a1afa
		default: break;
		}
		RestartIdealActivityId(Activity);                                     // 0x10289ee0
		return StartTask19Complete();                                         // TaskComplete on every arm
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskCreateHuntPatrolList:
	{
		// Arm `0x102a3bc7` (index 0x35). Slot 168 (`+0x2a0`), the last-enemy accessor `0x102b5360`.
		FElysiumEntity* TargetEntity = GetEnemy();                                  // 0x102a3bcb
		FVector TargetUnits = FVector::ZeroVector;
		const FVector* TargetArg = nullptr;
		if (TargetEntity != nullptr)                                                // 0x102a3bd5
		{
			TargetUnits = StartTask19A::UnitsOf(TargetEntity->Origin);                            // 0x102a3bdb slot 217 / 0x102a3c09
			TargetArg = &TargetUnits;
		}
		if (StartTask19BuildHuntPatrolList(StartTask19A::UnitsOf(Origin), TargetArg, StartTask19A::HuntSearchUnits))  // 0x102a3c13 0x10306700 / 0x102a3c1c
		{
			return StartTask19Complete();                                     // 0x102a3c20
		}
		return StartTask19Fail(StartTask19A::LineHuntListFailed, StartTask19A::FailHuntList);            // 0x102a3c34..c4a / 0x102a3c4a
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskFindHuntPatrolTarget:
	{
		// Arm `0x102a3c5d` (index 0x36), into `m_vecHuntPatrolTarget` (`+0x645c`).
		FElysiumEntity* TargetEntity = GetEnemy();                                  // 0x102a3c61 slot 168
		FVector TargetUnits = FVector::ZeroVector;
		const FVector* TargetArg = nullptr;
		if (TargetEntity != nullptr)                                                // 0x102a3c6b
		{
			TargetUnits = StartTask19A::UnitsOf(TargetEntity->Origin);                            // 0x102a3c71
			TargetArg = &TargetUnits;
		}
		if (StartTask19FindHuntPatrolTarget(StartTask19A::UnitsOf(Origin), TargetArg, StartTask19A::HuntSearchUnits,
			HuntPatrolTarget))                                                // 0x102a3cb0 0x10306f60 / 0x102a3ca6 0x102a3cb9
		{
			return StartTask19Complete();                                     // 0x102a3cbd
		}
		return StartTask19Fail(StartTask19A::LineHuntTargetFailed, StartTask19A::FailHuntList);          // 0x102a3cd1..ce7 / 0x102a3ce7
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskWaitAttackTime1:
	case StartTask19A::TaskWaitAttackTime2:
	{
		// Arm `0x102a337d` (index 0x37).
		if (ActiveWeaponEntity() == nullptr)                                  // 0x102a337f
		{
			return StartTask19Complete();                                     // 0x102a3388 -> 0x102a66d7 / 0x102a3398
		}
		// `m_flWaitFinished (+0x5db4) = 0x10252450(weapon, id == 0xb1)` (`0x102a33a2`) `+
		// 0x102c5730(this, weapon)` (`0x102a33b0`).
		BaseScheduleHost.WaitFinished =
			StartTask19WeaponNextAttackTime(TaskId == StartTask19A::TaskWaitAttackTime2);
		if (BaseScheduleHost.WaitFinished <= Now)                             // 0x102a33c7 FCOMP curtime; JNP
		{
			return StartTask19Complete();                                     // 0x102a4e51
		}
		if (BaseScheduleHost.HintNode != INDEX_NONE)                          // 0x102a33d5 +0x5ddc / 0x102a33dd
		{
			return 0;
		}
		RestartIdealActivityId(StartTask19A::ActWaitAttack);                                // 0x102a33e7
		return 0;
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskRunDialog:
	{
		// Arm `0x102a496b` (index 0x3f).
		const int32 DialogActivity = RunDialogActivity();                      // 0x102a496d 0x102c1400
		if (DialogActivity == INDEX_NONE)                                     // 0x102a4977 / 0x102a4979
		{
			return StartTask19Complete();                                     // 0x102a497d
		}
		SetActivity(DialogActivity);                                          // 0x102a4992 slot 310
		++StartTask19MotorStopTurns;
		StartTaskMotorHoldYaw();                                          // 0x102a499e 0x102e0b40
		ReleaseMotorHintYaw();                                                // 0x102a49aa 0x102e1e20(motor, -1)
		return 0;
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskRunDisposition:
	case StartTask19A::TaskSpecialIdleActivity:
	{
		// Arm `0x102a49bc` (index 0x40). The stance machine is `RunTask`'s (`0x102ab351`).
		BaseScheduleHost.WaitFinished = Now + TaskData;                       // 0x102a49c1..49c7 +0x5db4
		return 0;
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskRunDispositionRandom:
	case StartTask19A::TaskSpecialIdleActivityRandom:
	{
		// Arm `0x102a49da` (index 0x41).
		BaseScheduleHost.WaitFinished = StartTask19A::RandomFloat(0.f, TaskData) + Now;     // 0x102a49e8 / 0x102a49f1..49f4
		return 0;
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskAddEventExpression:
	{
		// Arm `0x102a4a07` (index 0x42).
		const int32 EventIndex = static_cast<int32>(TaskData);                // 0x102a4a0a __ftol
		if (EventIndex >= 0 && EventIndex < StartTask19A::ExpressionEventCount)             // 0x102a4a11 JL / 0x102a4a16 JGE
		{
			StartTask19AddExpressionForEvent(EventIndex);                     // 0x102a4a1b 0x101072b0
			return StartTask19Complete();                                     // 0x102a4a24
		}
		RecordScheduleEvent(FString::Printf(TEXT("Invalid event expression: %d\n"), EventIndex));  // 0x102a4a3c print import [0x109f364c]
		return StartTask19Complete();                                         // 0x102a4a49
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskSetPreservePath:
	{
		// Arm `0x102a3cfa` (index 0x43).
		if (static_cast<int32>(TaskData) != 0)                                // 0x102a3cfd __ftol / 0x102a3d0a
		{
			NpcFlags.Set(EElysiumNpcFlag::PRESERVE_PATH);                     // 0x102a3d2a OR AL,8
		}
		else
		{
			NpcFlags.Clear(EElysiumNpcFlag::PRESERVE_PATH);                   // 0x102a3d0c AND AL,0xf7
		}
		return StartTask19Complete();                                         // 0x102a3d18 / 0x102a3d36
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskSetEnemyEluded:
	{
		// Arm `0x102a46fa` (index 0x44).
		FElysiumEntity* Enemy = ConstBase->GetEnemy();                         // 0x102a46fe slot 167
		if (Enemy == nullptr)                                                 // 0x102a470a / 0x102a470c
		{
			return StartTask19Fail(StartTask19A::LineEnemyEludedNoEnemy, StartTask19A::FailNoEnemy);     // 0x102a473a..4750 / 0x102a4750
		}
		EnemyMemory.MarkEluded(Enemy->Handle);                                // 0x102a4717 slot 541; 0x102a471f 0x102dfd90
		return StartTask19Complete();                                         // 0x102a4728
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskSetTargetEluded:
	{
		// Arm `0x102a4763` (index 0x45). `m_hTargetEnt` (`+0x5ce4`).
		FElysiumEntity* TargetEntity = World != nullptr ? World->Resolve(TargetEnt) : nullptr;
		if (TargetEntity == nullptr)                                                // 0x102a476e / 0x102a478e / 0x102a4794 / 0x102a479e 0x102a47b5
		{
			return StartTask19Fail(StartTask19A::LineTargetEludedNoTarget, StartTask19A::FailNoTarget);  // 0x102a47e7..47ff / 0x102a47ff
		}
		EnemyMemory.MarkEluded(TargetEntity->Handle);                               // 0x102a47c2; 0x102a47ca 0x102dfd90
		return StartTask19Complete();                                         // 0x102a47d3
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskGetPathToEnemyClosest:
	{
		// Arm `0x102a4812` (index 0x46).
		FElysiumEntity* Enemy = ConstBase->GetEnemy();                         // 0x102a4816 slot 167 / 0x102a481e
		if (Enemy == nullptr)
		{
			return StartTask19Fail(StartTask19A::LinePathToEnemyClosestNoEnemy, StartTask19A::FailNoEnemy);  // 0x102a4820..483a / 0x102a483a
		}
		FStartTask19NavGoal Goal;                                             // 0x102a484d..4916 / 0x102a4861
		Goal.Type = 4;
		Goal.DestUnits = StartTask19A::UnitsOf(Enemy->Origin);                              // 0x102a486b slot 220
		Goal.Tolerance = StartTask19DefaultTolerance;
		Goal.GoalFlags = 2;                                                   // 0x102a48f2 +0x24 = 2
		if (StartTask19SetGoal(Goal, 0))                                      // 0x102a48d3 PUSH 0; 0x102a4921
		{
			return StartTask19Complete();                                     // 0x102a4928 -> 0x102a4e51
		}
		StartTaskDevMessage(TEXT("GetPathToEnemy failed!!\n"));                 // 0x102a4935 DevWarning(2, ...)
		return StartTask19Fail(StartTask19A::LinePathToEnemyClosestNoRoute, StartTask19A::FailNoRoute);  // 0x102a4942..4958 / 0x102a4958
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskSetInsideInterruptDist:
	{
		// Arm `0x102a4a5b` (index 0x47): `__ftol` THEN an integer square (`0x102a4a72 IMUL`).
		const int32 Distance = static_cast<int32>(ResolveTaskDistance(TaskData));  // 0x102a4a63 / 0x102a4a69
		ScheduleHost.InsideInterruptDistanceSqr = static_cast<float>(Distance * Distance);  // 0x102a4a7b..4a7f +0x6324
		return StartTask19Complete();                                         // 0x102a4a85
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskSetOutsideInterruptDist:
	{
		// Arm `0x102a4a97` (index 0x48).
		const int32 Distance = static_cast<int32>(ResolveTaskDistance(TaskData));  // 0x102a4a9f / 0x102a4aa5
		ScheduleHost.OutsideInterruptDistanceSqr = static_cast<float>(Distance * Distance);  // 0x102a4abb +0x6328
		return StartTask19Complete();                                         // 0x102a4ac1
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskSetInterruptTime:
	{
		ScheduleHost.InterruptTime = Now + TaskData;                          // 0x102a4add..4ae3 +0x632c
		return StartTask19Complete();                                         // 0x102a4ae9
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskAddRandomInterruptTime:
	{
		ScheduleHost.InterruptTime += StartTask19A::RandomFloat(0.f, TaskData);             // 0x102a4b09 / 0x102a4b0c..4b16
		return StartTask19Complete();                                         // 0x102a4b1c
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskClearInterruptTime:
	{
		ScheduleHost.InterruptTime = 0.0;                                     // 0x102a4b32
		return StartTask19Complete();                                         // 0x102a4b3c
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskWalkRunPath:
	{
		// Arm `0x102a4b4e` (index 0x4c).
		const float Distance = ResolveTaskDistance(TaskData);                // 0x102a4b56 slot 418
		// `nav+0x14`, the planar distance squared to the goal the navigator's arrival test
		// (`0x102f2ea0`) last wrote: the port measures it off `MoveGoal`.
		// Retail reads `0x102a4b5c..0x102a4b62 FLD [nav+0x14]` unconditionally; that word's writer, the
		// arrival test `0x102f2ea0` (store at `0x102f2f21`), is not ported (red N12, `stories/v1/triage.md`),
		// so `Navigator.EndpointDistanceSqrUnits` would read 0 here. Until the writer lands this reader
		// stays the port's measurement; `bMoveIssued` is motor bookkeeping standing in for the word.
		const double RemainingSq = bMoveIssued ? (MoveGoal - Origin).SizeSquared2D()
			/ (static_cast<double>(ElysiumMove::U) * ElysiumMove::U) : 0.0;
		int32 WeaponActivity = 0;
		// `d*d <= remaining` runs (`0x102a4b72 JP`); otherwise retail keeps an UNINITIALISED local
		// (`0x102a4b83 MOV EDI,[ESP+0x104]`). The port stands `-1` there (an activity with no
		// sequence), which the probe below replaces with ACT_WALK.
		int32 Activity = INDEX_NONE;
		if (static_cast<double>(Distance) * Distance <= RemainingSq)
		{
			Activity = TranslateActivityNumber(StartTask19A::ActRun, WeaponActivity);        // 0x102a4b7a 0x10271ff0
		}
		if (SelectWeightedSequenceForActivity(Activity) == INDEX_NONE)         // 0x102a4b91 0x10295460 / 0x102a4b98
		{
			Activity = TranslateActivityNumber(StartTask19A::ActWalk, WeaponActivity);       // 0x102a4ba0
		}
		StartTaskSetMovementActivity(Activity);                             // 0x102a4bae 0x102ee250
		BaseScheduleHost.MemoryBits &= ~StartTask19A::MemoryInCover;                        // 0x102a4bb3..4bbe +0x5d8c
		return StartTask19Complete();                                         // 0x102a4bc6
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskWalkRunPathCombatSound:
	{
		// Arm `0x102a4bd8` (index 0x4d). `+0x60b4` is `m_BestSound`'s (`+0x60b0`) type word.
		const uint32 BestSoundType = Senses.Memory.BestSound.TypeMask;
		int32 WeaponActivity = 0;
		int32 Activity = INDEX_NONE;
		bool bRun = false;
		if (BestSoundType == 1u || BestSoundType == 0x10u)                    // 0x102a4be1 / 0x102a4be6
		{
			Activity = TranslateActivityNumber(StartTask19A::ActRun, WeaponActivity);        // 0x102a4bee
			bRun = Activity != INDEX_NONE                                     // 0x102a4bfa
				&& SelectWeightedSequenceForActivity(Activity) != INDEX_NONE;  // 0x102a4c00 / 0x102a4c07
		}
		if (!bRun)
		{
			Activity = TranslateActivityNumber(StartTask19A::ActWalk, WeaponActivity);       // 0x102a4c0f
		}
		StartTaskSetMovementActivity(Activity);                             // 0x102a4c1d
		BaseScheduleHost.MemoryBits &= ~StartTask19A::MemoryInCover;                        // 0x102a4c22..4c2d
		return StartTask19Complete();                                         // 0x102a4c35
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskGetPathToPlayerForDialog:
	{
		// Arm `0x102a4c7a` (index 0x4e).
		FElysiumEntity* Player = StartTask19ClosestPlayer();                  // 0x102a4c7a +0x628c / 0x102a4c87 0x102a4ca4
		ScheduleHost.MoveTarget = Player != nullptr ? Player->Handle          // 0x102a4cb8 GetRefEHandle / 0x102a4cb4
			: FElysiumEntityHandle::Invalid();                                // 0x102a4cc1 -1 -> +0x6240
		// Retail re-resolves `m_hMoveTargetEnt` (`0x102a4d65`) and calls its slot 220 through a null / 0x102a4d4f 0x102a4d6d
		// pointer when the handle is dead (`0x102a4d74..4d78`). Crash guard: the port leaves the task
		// running without a goal.
		FElysiumEntity* MoveTargetEntity = World != nullptr ? World->Resolve(ScheduleHost.MoveTarget) : nullptr;
		if (MoveTargetEntity == nullptr)
		{
			return 0;
		}
		FStartTask19NavGoal Goal;                                             // 0x102a4cc3..4d47
		Goal.Type = 4;
		Goal.DestUnits = StartTask19A::UnitsOf(MoveTargetEntity->Origin);                  // 0x102a4d78 slot 220; 0x102a4d83
		Goal.Tolerance = StartTask19DefaultTolerance;
		Goal.Target = ScheduleHost.MoveTarget;                                // 0x102a4d8a 0x100d1590 -> +0x28
		(void)StartTask19SetGoal(Goal, 0);                                    // 0x102a4d9e GetNavigator; 0x102a4da5
		return 0;                                                             // no complete, no fail
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskWalkRunPathForDialog:
	{
		// Arm `0x102a4db7` (index 0x4f).
		FElysiumEntity* Player = StartTask19ClosestPlayer();                  // 0x102a4dc1 0x102c6460(&handle, 0) / 0x102a4dc8
		if (Player == nullptr)
		{
			return StartTask19Fail(0, StartTask19A::FailNoPlayer);                         // 0x102a4dcc..4dd0, no pair / 0x102a4dd0
		}
		const float Distance = static_cast<float>(FVector::Dist(Origin, Player->Origin) / ElysiumMove::U);  // 0x102a4e01 0x102a9570 / 0x102a4de5 0x102a4dee 0x102a4df9 (the two slot-220 origins)
		int32 Activity = StartTask19A::ActWalk;
		if (Distance >= SpecialDistanceAccum                                  // 0x102a4e06 +0x5bac; 0x102a4e14 JP
			|| SelectWeightedSequenceForActivity(StartTask19A::ActWalk) == INDEX_NONE)      // 0x102a4e1b / 0x102a4e22
		{
			if (SelectWeightedSequenceForActivity(StartTask19A::ActRun) == INDEX_NONE)      // 0x102a4e2f / 0x102a4e38
			{
				return StartTask19Fail(0, StartTask19A::FailBadActivity);                  // 0x102a4e67..4e6b, no pair / 0x102a4e6b
			}
			Activity = StartTask19A::ActRun;                                                // 0x102a4e3a
		}
		StartTaskSetMovementActivity(Activity);                             // 0x102a4e3c GetNavigator; 0x102a4e43
		BaseScheduleHost.MemoryBits &= ~StartTask19A::MemoryInCover;                        // 0x102a4e4c 0x102a98e0(this, 2)
		return StartTask19Complete();                                         // 0x102a4e51 / 0x102a4e55
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskStartPlayerDialog:
	{
		// Arm `0x102a4e7e` (index 0x50). Every miss is the `break` tail: complete, never a fail.
		FElysiumEntity* Player = StartTask19ClosestPlayer();                  // 0x102a4e84 0x102c6420
		if (Player != nullptr && CanTalk(Player))                             // 0x102a4e8d; 0x102a4e98 slot 295 / 0x102a4ea0
		{
			StartTask19PlayerStartDialog(*Player);                            // 0x102a4eab player+0x678(this)
		}
		return StartTask19Complete();                                         // 0x102a4eb5 / 0x102a66d7
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskSetToleranceDistDlg:
	{
		// Arm `0x102a4c47` (index 0x51). `m_flGoalTolerance = 160.0 + ResolveTaskDistance(data)`;
		// the split tail `0x102a42c7`: HALF to `0x102ee1c0`, the full word to `0x102f2fe0`.
		float ToleranceUnits = StartTask19A::DialogToleranceUnits;                          // 0x102a4c49
		ToleranceUnits += ResolveTaskDistance(TaskData);                     // 0x102a4c59
		ScheduleHost.GoalToleranceCm = ToleranceUnits * ElysiumMove::U;       // 0x102a4c66
		StartTask19SetNavTolerances(ToleranceUnits * StartTask19A::HalfDouble, ToleranceUnits);  // 0x102a4c6c; 0x102a42cd / 0x102a42df
		return StartTask19Complete();                                         // 0x102a42e8
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskDieIfPlayerCantSee:
	{
		// Arm `0x102a3198` (index 0x52).
		FElysiumEntity* Player = StartTask19ClosestPlayer();                  // 0x102a3198 +0x628c / 0x102a31a1 0x102a31be
		TaskComplete(false);                                                  // 0x102a31ca -- FIRST, before any test
		if (Player != nullptr)                                                // 0x102a31d1
		{
			// `ResolveTaskDistance(data)` against `m_flPlayerDist` (`+0x6264`): `TEST AH,0x41; JP`
			// returns when the distance is strictly GREATER (the player is inside it).
			const float Distance = ResolveTaskDistance(TaskData);             // 0x102a31df slot 418
			if (Distance > Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U)  // 0x102a31e5 / 0x102a31f0 / 0x102a31fa
			{
				return 0;
			}
			// Own eye (`GetOrigin` slot 220 + `m_vecViewOffset`) to the player's eye, mask `0x4091`
			// (`0x102a3294 0x1004f7a0`, `0x102a32a0 0x101d3190(&filter, this, 0)`, `0x102a32c4` / 0x102a32cc 0x102a32d3
			// enginetrace `+0x10`). The debug line (`0x102a32f5`, gated by `0x10005b87(0x10738960)`)
			// is debug drawing. The filter is `CTraceFilterSimple(this, 0)`: this NPC ignored, the
			// player NOT (mask `0x4091` carries no MONSTER, so no character is listed and only the
			// world can stop the line), which `ElysiumNpcSight::Visible` answers with the player as
			// its target.
			IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr;
			ElysiumNpcSight::FVisibleQuery Query;
			Query.EyeCm = EyePosition();
			Query.TargetCm = Player->EyePosition();
			Query.Mask = StartTask19A::DieIfPlayerCantSeeMask;
			Query.Looker = Handle;
			Query.Target = Player->Handle;
			Query.World = World;
			const bool bClear = Embodiment != nullptr
				&& ElysiumNpcSight::Visible(*Embodiment, Query, nullptr);        // 0x102a3242
			if (bClear)                                                       // 0x102a32fd FCOMP 1.0; JNP / 0x102a330f
			{
				return 0;
			}
		}
		// `0x102a331e 0x102b53d0(this, 0, "Leaving interesting place (TASK_DIE_IF_PLAYER_CANT_SEE)")`.
		RecordScheduleEvent(TEXT("Leaving interesting place (TASK_DIE_IF_PLAYER_CANT_SEE)"));
		LeaveInterestingPlaceOnRemove();
		UtilRemoveSelf();                                                     // 0x102a3324 0x101cd940
		return 0;
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskKnockout:
	{
		// Arm `0x102a3339` (index 0x53): SLEEPING | NO_DIALOG | DONT_INVESTIGATE | ONE_HIT_KILL.
		NpcFlags.AssignAiFlagsWord(NpcFlags.RawWord1() | StartTask19A::KnockoutFlags);      // 0x102a3344..334a +0x14b8
		RestartIdealActivityId(StartTask19A::ActKnockout);                                  // 0x102a3352
		return 0;
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskUnknockout:
	{
		// Arm `0x102a3364` (index 0x54). Clears nothing `0xdd` set; the schedule change does.
		RestartIdealActivityId(StartTask19A::ActUnknockout);                                // 0x102a336b
		return 0;
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskDoJumpActivity:
	{
		SetActivity(StartTask19A::ActJump);                                                 // 0x102a4ed0 slot 310
		return 0;
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskDoLoopActivity:
	{
		// Arm `0x102a4ee3` (index 0x56). The operand is the corpus's interned activity id (see
		// TASK_SET_ACTIVITY), handed on as the number retail would read.
		const int32 Activity = static_cast<int32>(TaskData);                  // 0x102a4ee6 __ftol
		int32 Sequence = 0, Translated = 0, Weapon = 0;
		ResolveActivityToSequence(Activity, Sequence, Translated, Weapon);    // 0x102a4eff 0x10272130
		if (Sequence > 0)                                                     // 0x102a4f0c JG
		{
			SetActivity(Activity);                                            // 0x102a4f25 slot 310
			return 0;
		}
		return StartTask19Complete();                                         // 0x102a4f10
	}
	// ---------------------------------------------------------------------------------------------
	case StartTask19A::TaskDoBlendActivity:
	case StartTask19A::TaskDoBlendLoopActivity:
	{
		// Arms `0x102a4f38` (index 0x57) and `0x102a4fb1` (index 0x58): two byte-identical bodies.
		const int32 Activity = static_cast<int32>(TaskData);                  // 0x102a4f3b / 0x102a4fb4
		float LayerCycle = 0.f;                                               // 0x102a4f51 / 0x102a4fca / 0x102a4f59 0x102a4fd2
		const int32 Layer = FindLayerByOwner(Activity - 1);                   // 0x102a4f48 / 0x102a4fc1 slot 271
		if (Layer != INDEX_NONE)
		{
			if (Layer >= 0 && Layer < ElysiumOverlay::NumSlots)
			{
				LayerCycle = AnimOverlay[Layer].Cycle;                        // 0x102a4f64 / 0x102a4fdf +0x740
			}
			RemoveLayerByOwner(Activity - 1);                                 // 0x102a4f71 / 0x102a4fea slot 274
		}
		int32 Sequence = 0, Translated = 0, Weapon = 0;
		ResolveActivityToSequence(Activity, Sequence, Translated, Weapon);    // 0x102a4f8a / 0x102a5003
		if (Sequence > 0)                                                     // 0x102a4f97 / 0x102a5010 JG
		{
			SetActivity(Activity);                                            // 0x102a5029 slot 310
			SequenceCycle = LayerCycle;                                       // 0x102a5033 +0x6f8 m_flCycle
			return 0;
		}
		return StartTask19Complete();                                         // 0x102a4f9f / 0x102a5014
	}
	// ---------------------------------------------------------------------------------------------
	default:
		break;
	}
	// Every other in-range id is an arm at or past `0x102a5046` (lane L02) or the table's base index
	// `0xaf` (`0x102a77e2`), which `StartTaskTroikaTail`'s `default:` forwards.
	return StartTaskTroikaTail(Task);
}
