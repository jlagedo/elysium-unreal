// Story 0019/8 (29e under the strict verdict), family **StartTask19** -- `CAI_BaseNPCTroika`'s
// bodies, second part.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// The cut follows the packet's chunk boundaries: one dispatch, retail's default arm once, no case
// body shared across the cut. Unity-build names here are prefixed `StartTask19_2`.
//
// Owns (StartTask19's `rule` rows): 0x102a1910 CAI_BaseNPCTroika::StartTask (the arms past the cut;
// the first part is `ElysiumNpcStartTask19.cpp`).
//
// Lane L02: every arm whose start lies in `[0x102a5046, 0x102a77f7]` (packet chunks 4-6, 108 arms,
// the two shared tails `0x102a66d7` / `0x102a77ea` and the base forward `0x102a77e2` included), read
// off the listing of `0x102a1910`. The switch is in retail's table order (ascending task id); each
// arm carries the address of the instruction it came from. The walked prose is
// `docs/vtmb/npc-ai/story8/StartTask19-TroikaB.md`.
//
// The four exits are retail's: `TaskComplete(false)` (`0x10273e80`, directly or through the shared
// break tail `0x102a66d7` = `StartTask19Complete()`), `TaskFail(reason)` behind the `+0x1b44` /
// `+0x1b48` debug pair (`StartTask19Fail(line, reason)`), a bare return that leaves the task RUNNING
// for `RunTask` (`0x102aacf0`), and the base forward (`default:`).
//
// Positions: retail's are SOURCE units; the port's entity origins are centimetres
// (`ElysiumMove::U`). Every helper here takes and answers SOURCE units unless it says `Cm`.

#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumNpcFlags.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "Substrate/ElysiumNpcGait.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumScheduleText.h"
#include "Substrate/ElysiumScriptedSequence.h"

namespace StartTask19_2Ids
{
	// The task ids of this lane's arms, from the registrar `0x10316ff0` (`walk-19-29-pack-13.md`,
	// section 0). Global and Troika-local ids coincide for the base + Troika spaces, which is what
	// `FElysiumScheduleStep::TaskId` carries (the landed idiom, `ElysiumNpcFrenzyShadow.cpp`).
	inline constexpr int32 TASK_WAIT_PVS = 0x05;
	inline constexpr int32 TASK_MELEE_DODGE = 0x8b;
	inline constexpr int32 TASK_MELEE_DODGE_ATTACK = 0x8c;
	inline constexpr int32 TASK_MELEE_KNOCKBACK = 0x92;
	inline constexpr int32 TASK_MELEE_FLYING_KNOCKBACK_INTO = 0x93;
	inline constexpr int32 TASK_MELEE_FLYING_KNOCKBACK_IDLE = 0x94;
	inline constexpr int32 TASK_MELEE_FLYING_KNOCKBACK_LAND = 0x95;
	inline constexpr int32 TASK_MELEE_FLYING_KNOCKBACK_WALL_0x96 = 0x96;
	inline constexpr int32 TASK_MELEE_FLYING_KNOCKBACK_WALL_0x97 = 0x97;
	inline constexpr int32 TASK_MELEE_FLYING_KNOCKBACK_WALL_0x98 = 0x98;
	inline constexpr int32 TASK_MELEE_HIT_BY_FINISHING_MOVE = 0x99;
	inline constexpr int32 TASK_RESET_MELEE_STEPBACK_TIME = 0x9b;
	inline constexpr int32 TASK_ON_FIRE_INTO = 0x9c;
	inline constexpr int32 TASK_ON_FIRE_LOOP = 0x9d;
	inline constexpr int32 TASK_ON_FIRE_OUTOF = 0x9e;
	inline constexpr int32 TASK_FACE_INTEREST = 0xb2;
	inline constexpr int32 TASK_FACE_PATROL_INTEREST = 0xb3;
	inline constexpr int32 TASK_DO_INTEREST_ACTIVITY = 0xb4;
	inline constexpr int32 TASK_DO_PATROL_INTEREST_ACTIVITY = 0xb5;
	inline constexpr int32 TASK_DO_LOITER_ACTIVITY = 0xb6;
	inline constexpr int32 TASK_DO_INTERACT_ACTIVITY = 0xb7;
	inline constexpr int32 TASK_DO_INTEREST_DEATH_ACTIVITY = 0xb8;
	inline constexpr int32 TASK_ADD_GESTURE = 0xe4;
	inline constexpr int32 TASK_DO_DAMAGE = 0xe5;
	inline constexpr int32 TASK_PLAY_COWER = 0xe6;
	inline constexpr int32 TASK_SET_COWER = 0xe7;
	inline constexpr int32 TASK_SET_DYING = 0xe8;
	inline constexpr int32 TASK_DIE_EXPLODE_GIB = 0xea;
	inline constexpr int32 TASK_DIE_DUE_TO_PLAYER = 0xeb;
	inline constexpr int32 TASK_PLAY_COMFORT_INTO = 0xec;
	inline constexpr int32 TASK_DO_COMFORT_LOOP = 0xed;
	inline constexpr int32 TASK_PLAY_COMFORT_OUTOF = 0xee;
	inline constexpr int32 TASK_PLAY_PARTIAL_RESIST_ACTIVITY = 0xef;
	inline constexpr int32 TASK_PLAY_FULL_RESIST_ACTIVITY = 0xf0;
	inline constexpr int32 TASK_SET_KNOCKBACK_ACTIVITY = 0xf1;
	inline constexpr int32 TASK_DROP_ALL_WEAPONS = 0xf2;
	inline constexpr int32 TASK_GIVE_FISTS = 0xf3;
	inline constexpr int32 TASK_CLEAR_HATRED = 0xf4;
	inline constexpr int32 TASK_DISCONNECT_FROM_SQUAD = 0xf5;
	inline constexpr int32 TASK_SUPERNATURAL_ACT = 0xf6;
	inline constexpr int32 TASK_FACE_NEXT_NODE = 0xf7;
	inline constexpr int32 TASK_LOOK_AT_BEST_SOUND = 0xf8;
	inline constexpr int32 TASK_ALERT_LOOK_AT_BEST_SOUND = 0xf9;
	inline constexpr int32 TASK_ALERT_LOOK_AT_RANDOM_LOC = 0xfa;
	inline constexpr int32 TASK_LOOK_AT_PLAYER = 0xfb;
	inline constexpr int32 TASK_LOOK_AT_BEST_UNKNOWN = 0xfc;
	inline constexpr int32 TASK_ALERT_LOOK_AT_DETECTED_ATTACK = 0xfd;
	inline constexpr int32 TASK_UNLOOK_AT = 0xfe;
	inline constexpr int32 TASK_UNLOOK_AT_FACE = 0xff;
	inline constexpr int32 TASK_SET_NPC_FLAG = 0x100;
	inline constexpr int32 TASK_CLEAR_NPC_FLAG = 0x101;
	inline constexpr int32 TASK_SET_MISC_FLAG = 0x102;
	inline constexpr int32 TASK_RUN_PATH_FLEE = 0x103;
	inline constexpr int32 TASK_WALK_PATH_HUNT = 0x104;
	inline constexpr int32 TASK_PATROL_PATH = 0x105;
	inline constexpr int32 TASK_FLIP_NEXT_IDEAL_YAW = 0x106;
	inline constexpr int32 TASK_ATTEMPT_DIVE = 0x107;
	inline constexpr int32 TASK_ATTEMPT_DIVE_SIDE = 0x108;
	inline constexpr int32 TASK_ATTEMPT_DIVE_FORWARD = 0x109;
	inline constexpr int32 TASK_PLAY_COVER_INTO = 0x10a;
	inline constexpr int32 TASK_PLAY_COVER_IDLE = 0x10b;
	inline constexpr int32 TASK_PLAY_COVER_OUTOF = 0x10c;
	inline constexpr int32 TASK_PLAY_COVER_AIM = 0x10d;
	inline constexpr int32 TASK_SNAP_TO_HINT = 0x10e;
	inline constexpr int32 TASK_KICK_HINT = 0x10f;
	inline constexpr int32 TASK_KICK_HINT_AT = 0x110;
	inline constexpr int32 TASK_GET_PATH_TO_KICK_PROP = 0x111;
	inline constexpr int32 TASK_SNAP_TO_KICK_PROP = 0x112;
	inline constexpr int32 TASK_KICK_PROP = 0x113;
	inline constexpr int32 TASK_GET_PATH_TO_LASTENEMY_LKP = 0x114;
	inline constexpr int32 TASK_FIND_COVER_FROM_LASTENEMY_LKP = 0x115;
	inline constexpr int32 TASK_RESOLVE_BOTCH = 0x116;
	inline constexpr int32 TASK_RESOLVE_BOTCH_IN_COVER = 0x117;
	inline constexpr int32 TASK_RESOLVE_BOTCH_OUT_OF_COVER = 0x118;
	inline constexpr int32 TASK_WAIT_FINISH_SET = 0x119;
	inline constexpr int32 TASK_WAIT_FINISH_ADD_RANDOM = 0x11a;
	inline constexpr int32 TASK_STEP_BACK = 0x11b;
	inline constexpr int32 TASK_STEP_BACK_RUN = 0x11c;
	inline constexpr int32 TASK_FACE_LASTANGLE = 0x11d;
	inline constexpr int32 TASK_PLAY_SOUND = 0x11e;
	inline constexpr int32 TASK_GET_PATH_TO_SAVEPOSITION_LOS = 0x11f;
	inline constexpr int32 TASK_SLEEP_BOUNDING_BOX = 0x121;
	inline constexpr int32 TASK_MELEE_CIRCLE_ENEMY = 0x122;
	inline constexpr int32 TASK_CIRCLE_ENEMY = 0x123;
	inline constexpr int32 TASK_CIRCLE_ENEMY_FULLCYCLE = 0x124;
	inline constexpr int32 TASK_SET_SHOOT_TARGET_OVERRIDE = 0x125;
	inline constexpr int32 TASK_COMBATMOVE_PATH = 0x126;
	inline constexpr int32 TASK_SET_TOLERANCE_DIST_COMBATMOVE = 0x127;
	inline constexpr int32 TASK_MELEE_CHEER = 0x128;
	inline constexpr int32 TASK_SET_SPECIAL_DISTANCE_ACCUM = 0x129;
	inline constexpr int32 TASK_ADD_SPECIAL_DISTANCE_ACCUM = 0x12a;
	inline constexpr int32 TASK_ADD_SPECIAL_DISTANCE_ACCUM_RANDOM = 0x12b;
	inline constexpr int32 TASK_SUB_SPECIAL_DISTANCE_ACCUM = 0x12c;
	inline constexpr int32 TASK_SUB_SPECIAL_DISTANCE_ACCUM_RANDOM = 0x12d;
	inline constexpr int32 TASK_FACE_SAVEPOSITION = 0x12e;
	inline constexpr int32 TASK_MAKE_OBLIVIOUS = 0x131;
	inline constexpr int32 TASK_PLAY_COMBAT_START_SEQUENCE = 0x137;
	inline constexpr int32 TASK_SQUAD_NEW_ENEMY = 0x138;
	inline constexpr int32 TASK_PRE_JUMP = 0x139;
	inline constexpr int32 TASK_JUMP = 0x13a;
	inline constexpr int32 TASK_LAND = 0x13b;
	inline constexpr int32 TASK_LAND_HARD = 0x13c;
	inline constexpr int32 TASK_SET_INVINCIBLE = 0x13d;
	inline constexpr int32 TASK_ACTIVITY_COPY_PROP_SPAWN = 0x13e;
	inline constexpr int32 TASK_ACTIVITY_COPY_PROP_CLEAR = 0x13f;
	inline constexpr int32 TASK_ACTIVITY_COPY_PROP_SET_COUNT = 0x140;
	inline constexpr int32 TASK_ACTIVITY_COPY_PROP_SET_RANDOM = 0x141;
	inline constexpr int32 TASK_ACTIVITY_COPY_PROP_FADEOUT = 0x142;
	inline constexpr int32 TASK_ACTIVITY_COPY_PROP_FORWARD = 0x143;
	inline constexpr int32 TASK_ACTIVITY_COPY_PROP_NIVBED = 0x144;
	inline constexpr int32 TASK_BURN_MODEL = 0x145;
	inline constexpr int32 TASK_SET_LASTPOSITION_TO_INITIAL = 0x146;
	inline constexpr int32 TASK_FIND_COVER_FROM_UNKNOWN_ATTACKER = 0x147;
	inline constexpr int32 TASK_ALERT_LOOK_AT_UNKNOWN_ATTACKER = 0x148;
	inline constexpr int32 TASK_PLAY_DEATH_SEQUENCE = 0x149;
}

namespace StartTask19_2Consts
{
	// Retail `Activity` ids the arms push as immediates (`ActivityList` names in comments).
	inline constexpr int32 ActIdle = 0x1;                // ACT_IDLE
	inline constexpr int32 ActRangeAttack2Alt = 0x3;     // the cheer's second pick (`0x102a71b0`)
	inline constexpr int32 ActWalk = 0x9;                // ACT_WALK
	inline constexpr int32 ActStepBackWalk = 0x10;       // TASK_STEP_BACK's activity
	inline constexpr int32 ActRun = 0x13;                // ACT_RUN
	inline constexpr int32 ActStepBackRun = 0x14;        // TASK_STEP_BACK_RUN's activity
	inline constexpr int32 ActDieSimple = 0x1d;          // ACT_DIESIMPLE
	inline constexpr int32 ActPreJump = 0x2b;
	inline constexpr int32 ActLand = 0x30;
	inline constexpr int32 ActLandHard = 0x32;
	inline constexpr int32 ActResolveBotch = 0x55;
	inline constexpr int32 ActFlyingKnockbackIdle = 0x8f;
	inline constexpr int32 ActKick = 0xc84;              // ACT_KICK
	inline constexpr int32 ActDiveForward = 0xf1d;
	inline constexpr int32 ActComfortInto = 0x106a;      // ACT_COMFORT_INTO
	inline constexpr int32 ActOnFireInto = 0x1082;
	inline constexpr int32 ActOnFireLoop = 0x1083;
	inline constexpr int32 ActOnFireOutof = 0x1084;
	inline constexpr int32 ActPartialResistDem = 0x108d;
	inline constexpr int32 ActPartialResistDom = 0x108e;
	inline constexpr int32 ActPartialResistThaum = 0x108f;
	inline constexpr int32 ActFullResistDem = 0x1090;
	inline constexpr int32 ActFullResistDom = 0x1091;
	inline constexpr int32 ActFullResistThaum = 0x1092;
	inline constexpr int32 ActRunFlee = 0x1093;
	inline constexpr int32 ActCowerInto = 0x1097;        // ACT_COWER_INTO
	inline constexpr int32 ActAlertFrontOutof = 0x1100;  // 0x1100..0x1107, `m_eFaceAnim` 1..8
	inline constexpr int32 ActDiveLeft = 0x110a;
	inline constexpr int32 ActDiveRight = 0x110b;
	inline constexpr int32 ActWalkPatrol = 0x1115;
	inline constexpr int32 ActCombatMove = 0x1121;
	inline constexpr int32 ActMeleeDodgeAttack = 0x1155;

	// `m_iLastDisciplineHitBy` values the resist arms switch on (`0x102a5228`..`0x102a5230`).
	inline constexpr int32 DisciplineDementation = 5;
	inline constexpr int32 DisciplineDominate = 6;
	inline constexpr int32 DisciplineThaumaturgy = 12;

	// `m_bfAINPCFlags2` masks as the schedule compiler encodes them (bit 31 is the routing marker
	// and IS written into the word by retail's raw OR, `0x102a9800`).
	inline constexpr uint32 Flag2DisconnectSquad = 0x80800000u;   // `0x102a5375`
	inline constexpr uint32 Flag2SleepBoundingBox = 0x80000001u;  // `0x102a70f8` / `0x102a7174`
	inline constexpr uint32 Flag2MadeOblivious = 0x80001000u;     // `0x102a72f5` / `0x102a731f`

	// Literal floats, read off the listing (cells named where retail pools them).
	inline constexpr float GestureDuration = 2.45f;               // `0x102a5064 PUSH 0x401ccccd`
	inline constexpr float DangerSoundDuration = 10.0f;           // `0x102a53f8 PUSH 0x41200000`
	inline constexpr int32 SupernaturalActLevel = 2;              // `0x102a53b1 PUSH 0x2`
	inline constexpr int32 DangerSoundRow = 0x1b;                 // `0x102a53d4 PUSH 0x1b`
	inline constexpr uint32 SoundTypeDanger = 0x8;                // `0x102a5406 PUSH 0x8`
	inline constexpr float RandomLookHalfExtent = 100.0f;         // `0x102a5565`/`0x102a556a`
	inline constexpr float DiveMinDistSqr = 4096.0f;              // `_DAT_104563b0`, 64 squared
	inline constexpr float DiveMaxDistSqr = 12100.0f;             // `_DAT_1049ae6c`, 110 squared
	inline constexpr double DiveRightDot = 0.98;                  // `_DAT_1049ae60`
	inline constexpr double DiveLeftDot = -0.98;                  // `_DAT_1049ae50`
	inline constexpr float DiveSideReach = 60.0f;                 // `0x102a5b6d PUSH 0x42700000`
	inline constexpr float DiveForwardReach = 96.0f;              // `0x102a5d09 PUSH 0x42c00000`
	inline constexpr float DiveProbeExtent = 100.0f;              // `0x102a5be0 PUSH 0x42c80000`
	inline constexpr int32 MaskNpcSolid = 0x202400b;              // `0x102a5bee PUSH 0x202400b`
	inline constexpr float StepBackReach = 50.0f;                 // `0x102a69d6 PUSH 0x42480000`
	inline constexpr float KickPropMaxDistSqr = 16384.0f;         // `_DAT_10450ab0`, 128 squared
	inline constexpr float KickHintReuseDelay = 60.0f;            // `0x102a60e4 PUSH 0x42700000`
	inline constexpr float KickPropGoalReach = 64.0f;             // `0x102a61cb PUSH 0x42800000`
	inline constexpr int32 GoalTypeLocation = 4;                  // `0x102a61b4` / `0x102a9d27`
	inline constexpr int32 GoalTypeKickProp = 10;                 // `0x102a61a2 PUSH 0xa`
	inline constexpr int32 GoalTypeCover = 6;                     // `0x102a675f PUSH 0x6`
	inline constexpr float GoalToleranceDefault = -1.0f;          // `DAT_1049a1ac`, AIN_DEF_TOLERANCE
	inline constexpr float GoalToleranceHull = -2.0f;             // `DAT_1049a1b0`, AIN_HULL_TOLERANCE
	inline constexpr uint32 SetGoalFlagsLkp = 2;                  // `0x102a65e4 PUSH 0x2`
	inline constexpr float LosMaxDist = 4096.0f;                  // `0x102a705d PUSH 0x45800000`
	inline constexpr float UnknownCoverMinDist = 32.0f;           // `0x102a7656 PUSH 0x42000000`
	inline constexpr float CircleFirstYaw = 80.0f;                // `_DAT_104454c8`
	inline constexpr float CircleSecondYaw = ElysiumNpcTunables::OneTwenty;   // `_DAT_1044f00c`
	inline constexpr int32 CircleTries = 2;                       // `0x102a6b2d CMP EBP,0x2`
	inline constexpr int32 CircleFailSuppressed = 1000;           // `0x102a6aeb MOV EBP,0x3e8`
	inline constexpr double CircleMaxRangeWeight = 0.75;          // `_DAT_10462958`
	inline constexpr float CircleMinRangeWeight = 0.25f;          // `_DAT_1044bef8`
	inline constexpr float CircleRadiusSentinel = -1.0f;          // `_DAT_104492dc`
	inline constexpr float SleepExtentsXY = 60.0f;                // `0x102a7124 PUSH 0x42700000`
	inline constexpr float SleepExtentsZ = 80.0f;                 // `0x102a711f PUSH 0x42a00000`
	inline constexpr float SleepExtentsCleared = -1.0f;           // `0x102a7151 PUSH 0xbf800000`
	inline constexpr int32 SoundChannelVoice = 2;                 // `0x102a700d PUSH 0x2`
	inline constexpr float SoundVolume = 1.0f;                    // `0x102a7008 PUSH 0x3f800000`
	inline constexpr float SoundAttenuation = 1.25f;              // `0x102a7003 PUSH 0x3fa00000`
	inline constexpr int32 SoundTypeBulletImpact = 0x10;          // `0x101b99d4`
	inline constexpr int32 SoundTypePhysicsDanger = 0x400;        // `0x101b99d9`
	inline constexpr float UnlookYawDelta[9] =                    // `0x102a5780`..`0x102a581a`
		{ 0.f, 0.f, 45.f, 90.f, 135.f, -45.f, -90.f, -135.f, 180.f };
	inline constexpr float MotorYawHalfTurn = ElysiumNpcTunables::OneEighty;  // `_DAT_1044c3a8`
	inline constexpr int32 DamageFamilyLethal = 1;                // `0x102a50ba PUSH 0x1`
	inline constexpr uint32 DamageTypeClub = 0x80;                // `0x102a50b5 PUSH 0x80`
	inline constexpr int32 DamageToHitSuccesses = 1;              // `0x102a50c8 PUSH 0x1`
	inline constexpr float DamageInfoScale = 1.0f;                // `0x102a50e4 PUSH 0x3f800000`

	const TCHAR* const ImpactDust = TEXT("impact_dust_emitter");  // `0x105da1e8`
	const TCHAR* const FinishingMoveBone = TEXT("Bip01 Spine2");  // `0x1053e6ec`
}

namespace
{
	// The two kick-hint `DevMsg` formats (`0x105da298`, `0x105da228`), verbatim.
	const TCHAR* const GStartTask19_2KickNotFound =
		TEXT("Warning: Kick hint (%s) is trying to find a physics object (%s) that is not found.\n ");
	const TCHAR* const GStartTask19_2KickTooFar =
		TEXT("Warning: Kick hint (%s) is trying to kick a physics object (%s) that is too far away.\n ");

	// `(int)flTaskData` -- `__ftol` (`0x10431320`), truncation toward zero.
	int32 StartTask19_2Ftol(float Value)
	{
		return static_cast<int32>(Value);
	}

	// `vstdlib` `RandomInt(lo, hi)` / `RandomFloat(lo, hi)` through `DAT_1070b244` (`+8` / `+4`).
	int32 StartTask19_2RandomInt(int32 Lo, int32 Hi)
	{
		return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).RandRange(Lo, Hi);
	}
	float StartTask19_2RandomFloat(float Lo, float Hi)
	{
		return ElysiumRng::Stream(EElysiumRngStream::NpcSchedule).FRandRange(Lo, Hi);
	}

	// `0x102e2750`'s yaw toward a delta, degrees.
	float StartTask19_2VecToYaw(const FVector& Delta)
	{
		return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)));
	}

	FVector StartTask19_2Units(const FVector& Cm)
	{
		return Cm / ElysiumMove::U;
	}

}

// =================================================================================================
// The helpers (`ElysiumNpcStartTask19_2.inl`).
// =================================================================================================

bool FElysiumNpc::TaskTailNavSetGoal(const FTaskTailNavGoal& Goal, uint32 SetGoalFlags)
{
	// `0x102ecd20`. Recorded first (the goal literal is the recovered half), then converted into the
	// one body's record: SOURCE units to centimetres, an entity goal (1, 2, 7) keeping the default
	// dest triple, no arrival activity / sequence (`0x102a9c80` / `0x102a9d20` / `0x102a9dc0` write
	// -1 into `[6]` / `[7]`).
	TaskTailLastNavGoal = Goal;
	TaskTailLastNavGoalFlags = SetGoalFlags;
	++TaskTailNavGoalCalls;
	FStartTaskNavGoal Record;
	Record.Type = Goal.Type;
	Record.bDestSet = Goal.Type != 1 && Goal.Type != 2 && Goal.Type != 7;
	Record.DestCm = Goal.DestUnits * ElysiumMove::U;
	Record.DestNode = Goal.DestNode;
	Record.MovementActivity = Goal.Activity;
	Record.ToleranceUnits = Goal.Tolerance;
	Record.GoalFlags = static_cast<int32>(Goal.GoalFlags);
	Record.Target = Goal.Target;
	return StartTaskSetGoal(Record, static_cast<int32>(SetGoalFlags));
}

bool FElysiumNpc::TaskTailNavFacingWaypoint(FVector& OutPositionUnits) const
{
	// SEAM for `0x102a9c60` / `0x102a9f20` / `0x102a9ee0` / `0x102a9f00`.
	(void)OutPositionUnits;
	return false;
}

float FElysiumNpc::TaskTailAbsYaw() const
{
	// `0x102885f0` (vtable `+0x374`) then `0x102a9600(1)`: `GetAbsAngles()[1]`.
	return static_cast<float>(Angles.Y);
}

void FElysiumNpc::TaskTailFaceWithAnim(const FVector& PositionUnits)
{
	// `0x10297940`.
	StartTaskMotorHoldYaw();                               // 0x1029794a
	StartTaskMotorSetIdealYawToTarget(PositionUnits * ElysiumMove::U);              // 0x1029795c
	FUN_10297a20();                                        // 0x10297963
	const float Yaw = TaskTailAbsYaw() + FaceYawDiff;      // 0x1029796c..0x10297981
	NpcFlags.Set(EElysiumNpcFlag::PLAYING_FACE_ANIM);      // 0x10297987
	StartTaskMotorSetIdealYaw(Yaw);                         // 0x10297993..0x102979e8
}

bool FElysiumNpc::TaskTailRestartHintActivity(EElysiumHintActivityQuery Query)
{
	// `0x102a1560`..`0x102a1620`: `act = 0x102a13d0..0x102a1510(); if (act == -1) return false;
	// RestartIdealActivity(act); return true;`.
	FHintWords Words;
	const int32 HintType = BaseScheduleHost.HintNode != INDEX_NONE
		&& HintWords(BaseScheduleHost.HintNode, Words) ? Words.HintType : INDEX_NONE;
	const int32 Activity = FElysiumNpcScheduleHost::HintNodeActivity(Query, HintType, bLeaningLeft);
	if (Activity == INDEX_NONE)
	{
		return false;
	}
	RestartIdealActivityId(Activity);
	return true;
}

int32 FElysiumNpc::TaskTailFlyingIdleSequence() const
{
	// SEAM for `0x10345480`.
	return INDEX_NONE;
}

FElysiumEntity* FElysiumNpc::TaskTailEnemy167() const
{
	// Slot 167, `0x101a67e0`, through the base pointer.
	return static_cast<const FElysiumNpcBase*>(this)->GetEnemy();
}

int32 FElysiumNpc::TaskTailCoordinatorCircleSide(const FElysiumEntity* Enemy) const
{
	// SEAM for `0x1025df40`.
	(void)Enemy;
	return 0;
}

bool FElysiumNpc::TaskTailWeaponMinRangeUnits(float& OutRangeUnits) const
{
	// SEAM for `GetActiveWeapon()->+0x8b8`.
	(void)OutRangeUnits;
	return false;
}

float FElysiumNpc::TaskTailCircleDistOverride()
{
	// SEAM for `cvar_debug_circle_dist_override` (`0x10924890`).
	return 0.f;
}

FElysiumEntityHandle FElysiumNpc::TaskTailFindEntityByName(const FString& Name) const
{
	// `gEntList.FindEntityByName(NULL, name)` -- the world's by-name lookup.
	if (World != nullptr && !Name.IsEmpty())
	{
		if (FElysiumEntity* Found = World->FindByName(Name))
		{
			return Found->Handle;
		}
	}
	return FElysiumEntityHandle::Invalid();
}

FString FElysiumNpc::TaskTailHintTargetName() const
{
	// `0x102a9c40`: the hint's `m_strTargetName` (`+0x468`).
	FHintWords Words;
	if (BaseScheduleHost.HintNode != INDEX_NONE && HintWords(BaseScheduleHost.HintNode, Words))
	{
		return Words.TargetName;
	}
	return FString();
}

int32 FElysiumNpc::TaskTailCopyPropFadeout(uint32 RawOperand)
{
	// SEAM for `0x1018eb50`: no `activity_copy_prop` entity; answers 0, the arm that keeps running.
	++TaskTailCopyPropCalls;
	TaskTailLastCopyPropCall = 0x1018eb50u;
	(void)RawOperand;
	return 0;
}

bool FElysiumNpc::TaskTailSolveJump(FVector& OutVelocityUnits) const
{
	// SEAM for `0x102c4c50`.
	(void)OutVelocityUnits;
	return false;
}

FElysiumInterestingPlace* FElysiumNpc::TaskTailPlaceAt(int32 EntityIndex) const
{
	if (World == nullptr || EntityIndex <= 0)
	{
		return nullptr;
	}
	FElysiumEntity* Entity = World->Resolve(FElysiumEntityHandle(EntityIndex, World->GetEpoch()));
	return Entity != nullptr && Entity->Def != nullptr
		&& Entity->Def->Classname.Equals(TEXT("intersting_place"), ESearchCase::IgnoreCase)
		? static_cast<FElysiumInterestingPlace*>(Entity) : nullptr;
}

void FElysiumNpc::TaskTailMovementActivity(int32 Activity)
{
	// `0x102a5904`.
	StartTaskSetMovementActivity(Activity);                       // 0x102a590e 0x102ee250(nav, act) / 0x102a5907 GetNavigator
	BaseScheduleHost.MemoryBits &= ~0x2u;                         // 0x102a5917 0x102a98e0(this, 2)
	TaskComplete(false);                                          // 0x102a5920
}

void FElysiumNpc::TaskTailLookAt(const FVector& PositionUnits, float TaskSeconds, double Now)
{
	// `0x102a7744`.
	TaskTailFaceWithAnim(PositionUnits);                          // 0x102a7747 0x10297940
	BaseScheduleHost.SetWaitFinished(TaskSeconds, Now);           // 0x102a774f 0x102a18a0
}

// =================================================================================================
// `CAI_BaseNPCTroika::StartTask` `0x102a1910`, the arms in `[0x102a5046, 0x102a77f7]`.
// =================================================================================================

int32 FElysiumNpc::StartTaskTroikaTail(void* Task)
{
	// `0x1029f730` / `0x1029f780` are handed `&m_sppPatrolPath` (`+0x658c`); their reader `0x1029f6c0`
	// (a seam, `PatrolNodeInterestRecord`) takes the path's current node. The port's own patrol route
	// (`PatrolIndex`) is a STORY8-TWIN and not what retail reads.
	auto TaskTailPatrolNode = [this]() -> int32
	{
		const FPatrolPathRecord* Path = PatrolPathCell.Path;
		return Path != nullptr && Path->Current >= 0 && Path->Current < PatrolPathNodeCapacity
			? Path->Nodes[Path->Current] : INDEX_NONE;
	};
	using namespace StartTask19_2Ids;
	using namespace StartTask19_2Consts;

	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	if (Step == nullptr)
	{
		// Retail dereferences the task unconditionally; a null step is the base's to refuse.
		return FElysiumNpcBase::StartTaskSlot442(Task);
	}
	// `[EDI]` is retail's `Task_t::iTask`, the CLASS-LOCAL id (the parser `0x1030d850` stores
	// `0x102ea280(space+0x18, global)`). This runtime's step carries the GLOBAL id (stated divergence,
	// `ElysiumScheduleText.h`), so it is translated here through this class's task space -- the body
	// of slot 450 `GetLocalTaskId` (`0x101a6640`), which no class overrides. For the root and Troika
	// spaces the local id is the registrar's number the arms compare.
	const int32 TaskId = GlobalToLocalId(IdSpace(EElysiumIdCategory::Task), Step->TaskId);
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const float Operand = Step->Data;                             // EDI + 0x4, the float word
	const uint32 RawOperand = Step->RawWord();                    // EDI + 0x4, the raw word

	switch (TaskId)
	{
	// --- index 0x00: `0x102a77ea`, the common epilogue -------------------------------------------
	case TASK_WAIT_PVS:
	case TASK_DIE_EXPLODE_GIB:
	case TASK_DIE_DUE_TO_PLAYER:
		// No write: the task stays RUNNING and `RunTask` owns all three.        // 0x102a77ea
		return 0;

	// --- index 0x1d: `0x102a66d7` (the arm Ghidra dropped) -------------------------------------- / 0x102a66db TaskComplete
	case TASK_MELEE_DODGE:
	case TASK_SET_SHOOT_TARGET_OVERRIDE:
		StartTask19Complete();                                  // 0x102a66d7
		return 0;

	// --- index 0x1e: TASK_MELEE_DODGE_ATTACK -----------------------------------------------------
	case TASK_MELEE_DODGE_ATTACK:
		RestartIdealActivityId(ActMeleeDodgeAttack);              // 0x102a7346
		return 0;

	// --- index 0x1f: TASK_MELEE_KNOCKBACK --------------------------------------------------------
	case TASK_MELEE_KNOCKBACK:
		Slot266();                                                // 0x102a6de8 vtable +0x428
		RestartIdealActivityId(KnockbackType);                    // 0x102a6df7
		return 0;

	// --- index 0x20: TASK_MELEE_FLYING_KNOCKBACK_INTO --------------------------------------------
	case TASK_MELEE_FLYING_KNOCKBACK_INTO:
	{
		Slot266();                                                // 0x102a6e0d
		Gravity = JumpGravity;                                    // 0x102a6e1c 0x102a9750 -> +0x3ec
		Velocity = KnockbackVelocity * ElysiumMove::U;            // 0x102a6e2a 0x102a96b0 -> +0x3d4
		if (Motor != nullptr)
		{
			// `CBaseEntity::SetAbsVelocity(m_KnockbackVelocity)` -- the motor's ballistic
			// assignment (`IElysiumNpcMotor::Launch`).
			Motor->Launch(KnockbackVelocity * ElysiumMove::U);    // 0x102a6e32
		}
		SetGroundEntity(nullptr);                                 // 0x102a6e3d slot 208
		NavSetType(1);                                            // 0x102a6e47 0x1027d9b0(this, 1)
		bJumping = true;                                          // 0x102a6e50 0x102a99a0(this, 1)
		RestartIdealActivityId(KnockbackType);                    // 0x102a6e5e
		return 0;
	}

	// --- index 0x21: TASK_MELEE_FLYING_KNOCKBACK_IDLE --------------------------------------------
	case TASK_MELEE_FLYING_KNOCKBACK_IDLE:
	{
		const int32 Sequence = TaskTailFlyingIdleSequence();      // 0x102a6e72 0x10345480
		if (Sequence < 0)                                         // 0x102a6e7b
		{
			RestartIdealActivityId(ActFlyingKnockbackIdle);       // 0x102a6ea2
			return 0;
		}
		ForcePreTranslatedSequenceAndActivity(ActFlyingKnockbackIdle, ActFlyingKnockbackIdle,
			Sequence);                                            // 0x102a6e8a slot 311
		return 0;
	}

	// --- index 0x22: TASK_MELEE_FLYING_KNOCKBACK_LAND / ..._WALL 0x98 ----------------------------
	case TASK_MELEE_FLYING_KNOCKBACK_LAND:
	case TASK_MELEE_FLYING_KNOCKBACK_WALL_0x98:
		TaskTailParticleDispatches.Add(ImpactDust);               // 0x102a6f04 0x102c41b0
		return 0;

	// --- index 0x23: TASK_MELEE_FLYING_KNOCKBACK_WALL 0x96 ---------------------------------------
	case TASK_MELEE_FLYING_KNOCKBACK_WALL_0x96:
		KnockbackWallHitFallTime = Now + ElysiumNpcTunables::Hundredth;   // 0x102a6ebc `_DAT_10450aa4`
		return 0;

	// --- index 0x24: TASK_MELEE_FLYING_KNOCKBACK_WALL 0x97 ---------------------------------------
	case TASK_MELEE_FLYING_KNOCKBACK_WALL_0x97:
		// `0x102c4e80`: `SetMoveType(4, 0)` (slot 93) then `m_flGravity = m_fJumpGravity`.
		SetMoveType(4, 0);                                        // 0x102a6ed7 -> 0x102c4e89
		Gravity = JumpGravity;                                    // 0x102c4e95
		// No `0x102a96b0` (`m_vecVelocity`) write here, unlike 0x93: `0x102a6edc..0x102a6ee5` is
		// `SetAbsVelocity` alone.
		if (Motor != nullptr)
		{
			Motor->Launch(KnockbackVelocity * ElysiumMove::U);    // 0x102a6ee5 SetAbsVelocity
		}
		return 0;

	// --- index 0x25: TASK_MELEE_HIT_BY_FINISHING_MOVE --------------------------------------------
	case TASK_MELEE_HIT_BY_FINISHING_MOVE:
	{
		FVector BoneUnits = FVector::ZeroVector;                  // 0x102a6f1a
		FVector BoneAngles = FVector::ZeroVector;                 // 0x102a6f26
		(void)RetailBonePosition(FinishingMoveBone, BoneUnits, BoneAngles);   // 0x102a6f3f
		FinishingMoveBoneTrackLastTime = Now;                     // 0x102a6f58
		FinishingMoveBoneTrackLastPos = BoneUnits;                // 0x102a6f5e
		return 0;
	}

	// --- index 0x27: TASK_RESET_MELEE_STEPBACK_TIME ----------------------------------------------
	case TASK_RESET_MELEE_STEPBACK_TIME:
		LastMeleeStepbackTime = Now;                              // 0x102a6fcd
		StartTask19Complete();                                  // 0x102a6fd3 -> 0x102a66d7
		return 0;

	// --- index 0x28..0x2a: the on-fire trio ------------------------------------------------------
	case TASK_ON_FIRE_INTO:
		FearSound();                                              // 0x102a6f74 slot 492
		SetIdealActivity(ActOnFireInto);                          // 0x102a6f81
		return 0;
	case TASK_ON_FIRE_LOOP:
		SetIdealActivity(ActOnFireLoop);                          // 0x102a6f9a
		return 0;
	case TASK_ON_FIRE_OUTOF:
		SetIdealActivity(ActOnFireOutof);                         // 0x102a6fb3
		return 0;

	// --- index 0x38: TASK_FACE_INTEREST ----------------------------------------------------------
	case TASK_FACE_INTEREST:
	{
		FElysiumInterestingPlace* Place = CurrentAmbientSpot();   // 0x102a634a +0x62ec
		if (Place == nullptr)                                     // 0x102a6354
		{
			StartTask19Fail(0x3848, 0x22);                        // 0x102a6396 / 0x102a63aa slot 448
			return 0;
		}
		++TaskTailHolsterChecks;                                  // 0x102a6356 0x102ae310
		if (!Place->bMatchOrientation)                            // 0x102a6361 0x102a9e80 (+0x570)
		{
			TaskComplete(false);                                  // 0x102a636a -> 0x102a6873
			return 0;
		}
		StartTaskMotorHoldYaw();                                  // 0x102a6377 / 0x102a6370 GetMotor
		// `0x102a9e60(place)` = the place's `GetAbsAngles().y`, through `0x10288590` (AngleMod).
		const float Yaw = StartTaskAngleMod(static_cast<float>(Place->Angles.Y));   // 0x102a638b / 0x102a6382 0x102a9e60
		StartTaskMotorSetIdealYaw(Yaw);                                     // 0x102a6390 -> 0x102a63fc
		return 0;
	}

	// --- index 0x39: TASK_FACE_PATROL_INTEREST ---------------------------------------------------
	case TASK_FACE_PATROL_INTEREST:
	{
		const int32 Hint = FUN_1029f730(TaskTailPatrolNode());   // 0x102a63c6 (&m_sppPatrolPath)
		FElysiumInterestingPlace* Place =
			TaskTailPlaceAt(ResolvePatrolInterestPlace(TaskTailPatrolNode()));   // 0x102a63d0 0x1029f780
		if (Place == nullptr)                                     // 0x102a63d9
		{
			TaskComplete(false);                                  // 0x102a643c
			return 0;
		}
		if (!Place->bMatchOrientation)                            // 0x102a63dd / 0x102a63e6
		{
			ScheduleHost.Unknown6300 = 0;                         // 0x102a641b
			ScheduleHost.Unknown659c = 0;                         // 0x102a6421
			TaskComplete(false);                                  // 0x102a6427
			return 0;
		}
		StartTaskMotorHoldYaw();                                  // 0x102a63ef / 0x102a63e8 GetMotor
		float HintYawDegrees = 0.f;
		(void)StartTaskHintYaw(Hint, HintYawDegrees);             // 0x102a63f6 0x102d12e0 (node or own yaw)
		StartTaskMotorSetIdealYaw(HintYawDegrees);                          // -> 0x102a63fc / 0x102a6408 / 0x102a6401 GetMotor
		return 0;
	}

	// --- index 0x3a: TASK_DO_INTEREST_ACTIVITY ---------------------------------------------------
	case TASK_DO_INTEREST_ACTIVITY:
	{
		InterestingDeathActivity = INDEX_NONE;                    // 0x102a6454 +0x6308 = -1
		FElysiumInterestingPlace* Place = CurrentAmbientSpot();   // 0x102a644e
		if (Place == nullptr)                                     // 0x102a6462
		{
			StartTask19Fail(0x3874, 0x22);                        // 0x102a647f / 0x102a6493 slot 448
			return 0;
		}
		// `0x102a9f40(this, place, 0, 1)`.
		ClaimInterestingPlace(Place, /*bClaimSecondary=*/true, Now);   // 0x102a6469
		return 0;
	}

	// --- index 0x3b: TASK_DO_PATROL_INTEREST_ACTIVITY --------------------------------------------
	case TASK_DO_PATROL_INTEREST_ACTIVITY:
	{
		const int32 Hint = FUN_1029f730(TaskTailPatrolNode());   // 0x102a64af
		FElysiumInterestingPlace* Place =
			TaskTailPlaceAt(ResolvePatrolInterestPlace(TaskTailPatrolNode()));   // 0x102a64b9
		if (Place == nullptr)                                     // 0x102a64c4
		{
			TaskComplete(false);                                  // 0x102a64c4 -> 0x102a6875
			return 0;
		}
		// `0x102a9f40(this, place, hint, 0)` (`0x102a64c2 PUSH 0` / `0x102a64ca PUSH EBX`): the
		// marker byte (`param_3`, handed to `ClaimMarker 0x102da7c0`) is 0 here, 1 in 0xb4; the hint
		// is `param_2`, the facing source `0x102d12e0` reads when the place matches orientation --
		// family Hints' claim carries no hint argument (unrecovered there), so it is dropped.
		(void)Hint;
		ClaimInterestingPlace(Place, /*bClaimSecondary=*/false, Now);   // 0x102a64cc
		return 0;
	}

	// --- index 0x3c / 0x3d: the loiter and interact picks ----------------------------------------
	case TASK_DO_LOITER_ACTIVITY:
	case TASK_DO_INTERACT_ACTIVITY:
	{
		const int32 Base = TaskId == TASK_DO_LOITER_ACTIVITY ? ActIdle : ActWalk;   // 0x102a64e0/e6
		int32 WeaponActivity = 0;
		const int32 Activity = TranslateActivityNumber(Base, WeaponActivity);   // 0x102a64ea
		StartTaskSetMovementActivity(Activity);                  // 0x102a64f9 0x102ee250 / 0x102a64f2 GetNavigator
		return 0;
	}

	// --- index 0x3e: TASK_DO_INTEREST_DEATH_ACTIVITY ---------------------------------------------
	case TASK_DO_INTEREST_DEATH_ACTIVITY:
		if (InterestingDeathActivity != INDEX_NONE)              // 0x102a6516
		{
			RestartIdealActivityId(InterestingDeathActivity);    // 0x102a6519
			return 0;
		}
		NpcFlags.Clear(EElysiumNpcFlag::INTERESTING_INTO);       // 0x102a6530 0x102a97d0(0x20000000)
		TaskComplete(false);                                     // 0x102a6539
		return 0;

	// --- index 0x59: TASK_ADD_GESTURE ------------------------------------------------------------
	case TASK_ADD_GESTURE:
	{
		const int32 Activity = StartTask19_2Ftol(Operand);        // 0x102a5049
		if (SelectWeightedSequenceForActivity(Activity) < 0)      // 0x102a5055 / 0x102a505c JL
		{
			StartTask19Complete();                              // 0x102a66d7
			return 0;
		}
		++TaskTailGestureCalls;                                   // 0x102a506c 0x10099250(act, 2.45, 0)
		TaskTailLastGestureActivity = Activity;
		TaskTailLastGestureDuration = GestureDuration;
		TaskComplete(false);                                      // 0x102a5075
		return 0;
	}

	// --- index 0x5a: TASK_DO_DAMAGE --------------------------------------------------------------
	case TASK_DO_DAMAGE:
	{
		int32 Amount = StartTask19_2Ftol(Operand);                // 0x102a508a
		if (Amount < 0)                                           // 0x102a5093 JGE
		{
			Amount = LastStoredDamage;                            // 0x102a5095 +0xfdc
		}
		// `CVDmg_t`: `SetSrc(this)`, `Set(1, 0x80, n)`, `+0xc = 1` (`0x102a9690`).
		FElysiumDmg Dmg;                                          // 0x102a50a2
		Dmg.Source = Handle;                                      // 0x102a50af
		Dmg.Family = static_cast<EElysiumDmgFamily>(DamageFamilyLethal);   // 0x102a50c3
		Dmg.DmgMask = DamageTypeClub;
		Dmg.BaseDamage = Amount;
		Dmg.ExtraInput = DamageToHitSuccesses;                    // 0x102a50d1
		// `0x101c26d0(&info, this, this, 1.0, 0, 0, &dmg, -1)` -- inflictor and attacker both this.
		FElysiumTakeDamageInfo Info;                              // 0x102a50ef
		Info.Dmg = &Dmg;
		Info.Attacker = Handle;
		Info.Damage = DamageInfoScale;
		Info.DamageBits = 0;
		Info.AmmoType = INDEX_NONE;
		OnTakeDamage(&Info);                                      // 0x102a50fd slot 142 / 0x102a510a the info's destructor
		TaskComplete(false);                                      // 0x102a5113
		return 0;
	}

	// --- index 0x5b / 0x5c: TASK_PLAY_COWER / TASK_SET_COWER -------------------------------------
	case TASK_PLAY_COWER:
	case TASK_SET_COWER:
	{
		const int32 Activity = StartTask19_2Ftol(Operand);        // 0x102a5128 / 0x102a516f
		if (Activity == ActCowerInto)                             // 0x102a512f / 0x102a5176 / 0x102a5135 / 0x102a517c JNZ
		{
			CowerAnimOffset = StartTask19_2RandomInt(0, 2) * 3;   // 0x102a5143 / 0x102a5149 / 0x102a518a RandomInt
		}
		if (TaskId == TASK_PLAY_COWER)
		{
			SetIdealActivity(CowerAnimOffset + Activity);         // 0x102a515a
		}
		else
		{
			RestartIdealActivityId(CowerAnimOffset + Activity);   // 0x102a51a1
		}
		return 0;
	}

	// --- index 0x5d: TASK_SET_DYING --------------------------------------------------------------
	case TASK_SET_DYING:
		AnimEventLifeStateWord = 1;                                      // 0x102a778e +0x200 = LIFE_DYING
		StartTask19Complete();                                  // 0x102a7798 -> 0x102a66d7
		return 0;

	// --- index 0x5e: TASK_PLAY_COMFORT_INTO ------------------------------------------------------
	case TASK_PLAY_COMFORT_INTO:
	{
		const int32 Activity = StartTask19_2RandomInt(0, 1) * 3 + ActComfortInto;   // 0x102a51bf/c2
		(void)SelectWeightedSequenceForActivity(Activity);        // 0x102a51ce, result discarded
		SetIdealActivity(Activity);                               // 0x102a51d6
		return 0;
	}

	// --- index 0x5f: TASK_DO_COMFORT_LOOP / TASK_PLAY_COMFORT_OUTOF ------------------------------
	case TASK_DO_COMFORT_LOOP:
	case TASK_PLAY_COMFORT_OUTOF:
		if (ActivityNumber + 1 == 0)                              // 0x102a51ee INC / 0x102a51ef JZ
		{
			ActivityNumber = 0;                                   // 0x102a5206
			return 0;
		}
		SetIdealActivity(ActivityNumber + 1);                     // 0x102a51f4
		return 0;

	// --- index 0x60 / 0x61: the resist activities -----------------------------------------------
	case TASK_PLAY_PARTIAL_RESIST_ACTIVITY:
	{
		int32 Activity = ActPartialResistDem;                     // 0x102a5223 / 0x102a5269 / 0x102a522b JZ
		if (LastDisciplineHitBy == DisciplineDominate)            // 0x102a522e
		{
			Activity = ActPartialResistDom;                       // 0x102a524f
		}
		else if (LastDisciplineHitBy == DisciplineThaumaturgy)    // 0x102a5233
		{
			Activity = ActPartialResistThaum;                     // 0x102a5235
		}
		SetIdealActivity(Activity);                               // 0x102a523d / 0x102a5257 / 0x102a5271
		return 0;
	}
	case TASK_PLAY_FULL_RESIST_ACTIVITY:
	{
		int32 Activity = ActFullResistDem;                        // 0x102a5289 / 0x102a52cf / 0x102a5291 JZ
		if (LastDisciplineHitBy == DisciplineDominate)            // 0x102a5294
		{
			Activity = ActFullResistDom;                          // 0x102a52b5
		}
		else if (LastDisciplineHitBy == DisciplineThaumaturgy)    // 0x102a5299
		{
			Activity = ActFullResistThaum;                        // 0x102a529b
		}
		SetIdealActivity(Activity);                               // 0x102a52a3 / 0x102a52bd / 0x102a52d7
		return 0;
	}

	// --- index 0x62: TASK_SET_KNOCKBACK_ACTIVITY -------------------------------------------------
	case TASK_SET_KNOCKBACK_ACTIVITY:
		KnockbackType = StartTask19_2Ftol(Operand);               // 0x102a52ec / 0x102a52f5
		TaskComplete(false);                                      // 0x102a52fb
		return 0;

	// --- index 0x63: TASK_DROP_ALL_WEAPONS -------------------------------------------------------
	case TASK_DROP_ALL_WEAPONS:
		Weapon_Drop_All();                                        // 0x102a5311 slot 387
		TaskComplete(false);                                      // 0x102a531b
		return 0;

	// --- index 0x64: TASK_GIVE_FISTS -------------------------------------------------------------
	case TASK_GIVE_FISTS:
		GiveBaseFightingItems();                                  // 0x102a5331 slot 304
		TaskComplete(false);                                      // 0x102a533b
		return 0;

	// --- index 0x65: TASK_CLEAR_HATRED -----------------------------------------------------------
	case TASK_CLEAR_HATRED:
		// `0x102b52a0(this, 1, 1)` -- Boss19's `ResetAiState`, lane L12's body. Reached through the
		// seam below until that body lands (the integrator redirects it).
		ResetAiState(true, true);                                 // 0x102a5353
		TaskComplete(false);                                      // 0x102a535c
		return 0;

	// --- index 0x66: TASK_DISCONNECT_FROM_SQUAD --------------------------------------------------
	case TASK_DISCONNECT_FROM_SQUAD:
		DisconnectFromSquad();                                    // 0x102a5370 0x1026d050
		NpcFlags.SetRawWord2Bits(Flag2DisconnectSquad);           // 0x102a537c 0x102a9800
		TaskComplete(false);                                      // 0x102a5385
		return 0;

	// --- index 0x67: TASK_SUPERNATURAL_ACT -------------------------------------------------------
	case TASK_SUPERNATURAL_ACT:
		// `0x102ca2a0(DAT_109253f8, 2, this, pos, 2, <operand bits>, 0, this, 0)`: the act's
		// duration is the operand's raw word handed on unchanged.
		++TaskTailSupernaturalActs;                               // 0x102a53cf / 0x102a53a0 GetAbsOrigin
		TaskTailLastSupernaturalActDuration = Operand;
		// `CSoundEnt::InsertSound(8, pos, table[0x1b], 10.0, flag[0x1b], this)`.
		++TaskTailDangerSounds;                                   // 0x102a5408 / 0x102a53db / 0x102a53e9 row 0x1b; 0x102a5400 GetAbsOrigin
		TaskComplete(false);                                      // 0x102a5414
		return 0;

	// --- index 0x68: TASK_FACE_NEXT_NODE ---------------------------------------------------------
	case TASK_FACE_NEXT_NODE:
	{
		if (!NavigatorGoalIsActive())                             // 0x102a542f 0x102ee6a0 / 0x102a5436 / 0x102a5428 GetNavigator
		{
			EmitDevMsg(TEXT("No route to face!\n"), TEXT("No route to face!\n"));   // 0x102a543f DevWarning(2, ...)
			StartTask19Fail(0x3599, 0xc);                         // 0x102a5462
			return 0;
		}
		FVector WaypointUnits = FVector::ZeroVector;
		if (!TaskTailNavFacingWaypoint(WaypointUnits))            // 0x102a548e / 0x102a5477 / 0x102a547e / 0x102a5485 / 0x102a5492 / 0x102a5499
		{
			StartTask19Fail(0x35b9, 0xc);                         // 0x102a5502
			return 0;
		}
		StartTaskMotorHoldYaw();                                  // 0x102a54a6 / 0x102a549f GetMotor
		StartTaskMotorSetIdealYawToTarget(WaypointUnits * ElysiumMove::U);                 // 0x102a54be 0x102e2020 / 0x102a54af / 0x102a54b7
		if (FacingIdeal())                                        // 0x102a54c5 / 0x102a54cc
		{
			TaskComplete(false);                                  // 0x102a54d6
		}
		return 0;
	}

	// --- index 0x69: the best-sound looks --------------------------------------------------------
	case TASK_LOOK_AT_BEST_SOUND:
	case TASK_ALERT_LOOK_AT_BEST_SOUND:
	{
		const FElysiumGameSoundEvent* Sound =
			static_cast<const FElysiumGameSoundEvent*>(GetBestSound());   // 0x102a5519 slot 474
		if (Sound == nullptr)                                     // 0x102a5521
		{
			StartTask19Fail(0x35cb, 0x12);                        // 0x102a5549
			return 0;
		}
		// `0x101b99d0`: the owner's `GetAbsOrigin` for a BULLET_IMPACT / PHYSICS_DANGER sound whose
		// owner is live, else the sound's own origin (`+0x20`).
		FVector SoundUnits = StartTask19_2Units(Sound->Position);
		if ((Sound->TypeMask == static_cast<uint32>(SoundTypeBulletImpact)
				|| Sound->TypeMask == static_cast<uint32>(SoundTypePhysicsDanger))
			&& World != nullptr)
		{
			if (const FElysiumEntity* Owner = World->Resolve(Sound->Source))
			{
				SoundUnits = StartTask19_2Units(Owner->Origin);
			}
		}
		TaskTailLookAt(SoundUnits, Operand, Now);                 // 0x102a552a -> 0x102a7744 / 0x102a5525 0x101b99d0 the sound's position
		return 0;
	}

	// --- index 0x6a: TASK_ALERT_LOOK_AT_RANDOM_LOC -----------------------------------------------
	case TASK_ALERT_LOOK_AT_RANDOM_LOC:
	{
		// `0x1010e530(&v, -100, 100)`: each axis drawn in [-100, 100] -- around the WORLD origin, not
		// around this body (retail's own reading, reproduced).
		FVector PointUnits = FVector::ZeroVector;                 // 0x102a5560
		PointUnits.X = StartTask19_2RandomFloat(-RandomLookHalfExtent, RandomLookHalfExtent);   // 0x102a5573
		PointUnits.Y = StartTask19_2RandomFloat(-RandomLookHalfExtent, RandomLookHalfExtent);
		PointUnits.Z = StartTask19_2RandomFloat(-RandomLookHalfExtent, RandomLookHalfExtent);
		TaskTailLookAt(PointUnits, Operand, Now);                 // 0x102a557f / 0x102a5587
		return 0;
	}

	// --- index 0x6b: TASK_LOOK_AT_PLAYER ---------------------------------------------------------
	case TASK_LOOK_AT_PLAYER:
	{
		const FElysiumEntity* Player =
			World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr;   // 0x102a55a1 +0x628c
		if (Player == nullptr)                                    // 0x102a55a8
		{
			StartTask19Fail(0x35e2, 0x17);                        // 0x102a55d0
			return 0;
		}
		TaskTailLookAt(StartTask19_2Units(Player->Origin), Operand, Now);   // 0x102a55b1 -> 0x102a773a / 0x102a55ac GetAbsOrigin
		return 0;
	}

	// --- index 0x6c: TASK_LOOK_AT_BEST_UNKNOWN ---------------------------------------------------
	case TASK_LOOK_AT_BEST_UNKNOWN:
	{
		const FElysiumEntity* Best =
			World != nullptr ? World->Resolve(GetBestSeeUnknown()) : nullptr;   // 0x102a55ef slot 586 / 0x102a55f7 the handle resolve
		if (Best != nullptr)                                      // 0x102a55fe
		{
			TaskTailLookAt(StartTask19_2Units(Best->Origin), Operand, Now);   // -> 0x102a773a
			return 0;
		}
		const bool bLastLive = World != nullptr
			&& World->Resolve(Senses.Memory.LastSeeUnknown) != nullptr;   // 0x102a560b 0x1028ac10(h, 0)
		if (bLastLive)                                            // 0x102a5614
		{
			TaskTailLookAt(StartTask19_2Units(Senses.Memory.LastSeeUnknownPosition), Operand, Now);   // 0x102a561d +0x6090 / 0x102a5625
			return 0;
		}
		StartTask19Fail(0x35f7, 0x21);                            // 0x102a564f
		return 0;
	}

	// --- index 0x6d: TASK_ALERT_LOOK_AT_DETECTED_ATTACK ------------------------------------------
	case TASK_ALERT_LOOK_AT_DETECTED_ATTACK:
	{
		const FElysiumEntity* Attacker = World != nullptr
			? World->Resolve(Senses.Memory.DetectedAttackAttacker) : nullptr;   // 0x102a5668 +0x65c0
		if (Attacker == nullptr)                                  // 0x102a566f
		{
			StartTask19Fail(0x3607, 0x21);                        // 0x102a568f
			return 0;
		}
		TaskTailLookAt(StartTask19_2Units(Attacker->Origin), Operand, Now);   // -> 0x102a773a
		return 0;
	}

	// --- index 0x6e / 0x6f: the alert turn-outs -------------------------------------------------
	case TASK_UNLOOK_AT:
	case TASK_UNLOOK_AT_FACE:
	{
		// `m_eFaceAnim` (`+0x63e4`) 1..8 -> 0x1100..0x1107, anything else (unsigned `> 8`, or 0)
		// -> ACT_IDLE. `0x102a56ab` / `0x102a575d` `JA`, tables `0x102a7c10` / `0x102a7c34`.
		const uint32 Face = static_cast<uint32>(FaceAnim);
		const bool bTurn = Face >= 1u && Face <= 8u;
		SetIdealActivity(bTurn ? ActAlertFrontOutof + static_cast<int32>(Face) - 1 : ActIdle);   // 0x102a56f0 / 0x102a5815 / 0x102a56ad / 0x102a5763 table jumps / 0x102a577b / 0x102a5794 / 0x102a57aa / 0x102a57c0 / 0x102a57d6 / 0x102a57ec / 0x102a5802 the per-face SetIdealActivity
		const float Yaw = TaskTailAbsYaw();                       // 0x102a56f9 / 0x102a5826 / 0x102a5700 / 0x102a582d
		NpcFlags.Set(EElysiumNpcFlag::PLAYING_FACE_ANIM);         // 0x102a5710 / 0x102a583d
		StartTaskMotorHoldYaw();                                  // 0x102a571e / 0x102a584b / 0x102a5717 GetMotor / 0x102a5844 GetMotor
		if (TaskId == TASK_UNLOOK_AT)
		{
			StartTaskMotorSetIdealYaw(Yaw - FaceYawDiff);          // 0x102a5727 FSUB +0x63e8 / 0x102a573a / 0x102a5733 GetMotor
		}
		else
		{
			StartTaskMotorSetIdealYaw(Yaw + UnlookYawDelta[bTurn ? Face : 0u]);   // 0x102a5854 FADD
		}
		BaseScheduleHost.SetWaitFinished(Operand, Now);           // 0x102a5742
		return 0;
	}

	// --- index 0x70: TASK_SET_NPC_FLAG -----------------------------------------------------------
	case TASK_SET_NPC_FLAG:
	{
		const int32 Mask = static_cast<int32>(RawOperand);        // 0x102a585d
		if (Mask < 0)                                             // 0x102a5865 JS
		{
			NpcFlags.SetRawWord2Bits(RawOperand);                 // 0x102a537c 0x102a9800
		}
		else
		{
			NpcFlags.Set(static_cast<EElysiumNpcFlag>(RawOperand));   // 0x102a586b 0x102a97a0
		}
		TaskComplete(false);                                      // 0x102a5874 / 0x102a5385
		return 0;
	}

	// --- index 0x71: TASK_CLEAR_NPC_FLAG ---------------------------------------------------------
	case TASK_CLEAR_NPC_FLAG:
	{
		const int32 Mask = static_cast<int32>(RawOperand);        // 0x102a58ae
		if (Mask < 0)                                             // 0x102a58b6 JS
		{
			NpcFlags.ClearRawWord2Bits(RawOperand);               // 0x102a717b 0x102a9830
			StartTask19Complete();                              // 0x102a7180 -> 0x102a66d7
			return 0;
		}
		NpcFlags.Clear(static_cast<EElysiumNpcFlag>(RawOperand));   // 0x102a58bc 0x102a97d0
		TaskComplete(false);                                      // 0x102a58c5
		return 0;
	}

	// --- index 0x72: TASK_SET_MISC_FLAG ----------------------------------------------------------
	case TASK_SET_MISC_FLAG:
		// `1 << CL` -- the shift count is the low five bits of the raw word (x86 `SHL`).
		ElysiumMiscFlags::Set(MiscFlags, 1u << (RawOperand & 0x1fu));   // 0x102a588e / 0x102a5893
		TaskComplete(false);                                      // 0x102a589c
		return 0;

	// --- index 0x73..0x75, 0x92: the movement-activity picks ------------------------------------
	case TASK_RUN_PATH_FLEE:
	{
		int32 WeaponActivity = 0;
		int32 Activity = TranslateActivityNumber(ActRunFlee, WeaponActivity);   // 0x102a58e0
		if (SelectWeightedSequenceForActivity(Activity) == INDEX_NONE)          // 0x102a58ee / 0x102a58f5
		{
			Activity = TranslateActivityNumber(ActRun, WeaponActivity);          // 0x102a58fd
		}
		TaskTailMovementActivity(Activity);                                      // 0x102a5904
		return 0;
	}
	case TASK_WALK_PATH_HUNT:
	{
		// `0x10295460(this, 0x1115, 0)` (`0x102a593c`): `TranslateActivity` then
		// `SelectWeightedSequence` of the translated weapon activity, no raw fallback (arg 0); the
		// movement activity handed on stays the RAW 0x1115. The one sequence seam stands for both.
		int32 Activity = ActWalkPatrol;                                          // 0x102a5932
		if (SelectWeightedSequenceForActivity(Activity) == INDEX_NONE)          // 0x102a5944
		{
			Activity = ActWalk;                                                  // 0x102a5946
		}
		TaskTailMovementActivity(Activity);                                      // -> 0x102a5904
		return 0;
	}
	case TASK_PATROL_PATH:
	case TASK_COMBATMOVE_PATH:
	{
		int32 WeaponActivity = 0;
		const int32 Wanted = TaskId == TASK_PATROL_PATH ? ActWalkPatrol : ActCombatMove;   // 0x102a594f / 0x102a5958
		int32 Activity = TranslateActivityNumber(Wanted, WeaponActivity);                      // 0x102a595f
		if (SelectWeightedSequenceForActivity(Activity) == INDEX_NONE)                         // 0x102a596d / 0x102a5974
		{
			Activity = TranslateActivityNumber(ActWalk, WeaponActivity);                        // 0x102a5978 -> 0x102a58fb
		}
		TaskTailMovementActivity(Activity);                                                     // -> 0x102a5904
		return 0;
	}

	// --- index 0x76: TASK_FLIP_NEXT_IDEAL_YAW ----------------------------------------------------
	case TASK_FLIP_NEXT_IDEAL_YAW:
		// `0x102a99c0(GetMotor(), operand != 0.0)` -- `motor+0x28`, the port's
		// `bMotorAnimationMovement`.
		BaseScheduleHost.bMotorAnimationMovement = Operand != 0.f;   // 0x102a59fb / 0x102a5a06 / 0x102a5a2a / 0x102a59ff GetMotor / 0x102a5a23 GetMotor
		TaskComplete(false);                                      // 0x102a5a0f / 0x102a5a33
		return 0;

	// --- index 0x77: TASK_ATTEMPT_DIVE -----------------------------------------------------------
	case TASK_ATTEMPT_DIVE:
	{
		FVector GoalUnits = FVector::ZeroVector;
		(void)NavGoalPosition(GoalUnits);                         // 0x102a5a4e 0x102ee140 / 0x102a5a47 GetNavigator
		const FVector Delta = GoalUnits - StartTask19_2Units(Origin);   // 0x102a5a6b / 0x102a5a93
		const float Len2 = static_cast<float>(Delta.X * Delta.X + Delta.Y * Delta.Y);   // 0x102a5a98..a8
		if (Len2 < DiveMinDistSqr)                                // 0x102a5aaa / 0x102a5ab7
		{
			TaskComplete(false);                                  // 0x102a5b20
			return 0;
		}
		// `FCOMP 12100; TEST AH,0x41; JP` is taken when NEITHER C0 nor C3 is set -- past 110 units.
		// The dive is decided only in the band [64, 110]; `walk-19-29-pack-13.md` section 120 reads
		// this test inverted.
		if (Len2 > DiveMaxDistSqr)                                // 0x102a5ab9 / 0x102a5ac4 JP
		{
			StartTask19Complete();                              // 0x102a66d7
			return 0;
		}
		// `Delta.z = 0`, normalise (`0x1057966c`), `DotProduct2D(delta, m_vecRight)` (`0x102a9540`).
		FVector Flat(Delta.X, Delta.Y, 0.0);                      // 0x102a5ace
		Flat.Normalize();                                         // 0x102a5ad6
		const double Dot = Flat.X * Right.X + Flat.Y * Right.Y;   // 0x102a5ae9
		if (Dot > DiveRightDot)                                   // 0x102a5aee / 0x102a5afb
		{
			NpcFlags.Set(EElysiumNpcFlag::ANIM_MOVEMENT);         // 0x102a5c0d
			SetIdealActivity(ActDiveRight);                       // 0x102a5c19
			return 0;
		}
		if (Dot < DiveLeftDot)                                    // 0x102a5b04 / 0x102a5b0f
		{
			NpcFlags.Set(EElysiumNpcFlag::ANIM_MOVEMENT);         // 0x102a5c82
			SetIdealActivity(ActDiveLeft);                        // 0x102a5c8e
			return 0;
		}
		StartTask19Complete();                                  // 0x102a5b0f JP -> 0x102a66d7
		return 0;
	}

	// --- index 0x78: TASK_ATTEMPT_DIVE_SIDE ------------------------------------------------------
	case TASK_ATTEMPT_DIVE_SIDE:
	{
		FVector FromUnits = StartTask19_2Units(Origin);           // 0x102a5b36
		FromUnits.Z += StepHeight() * ElysiumNpcTunables::Half;   // 0x102a5b54 / 0x102a5b5a
		const FVector RightEnd = FromUnits + Right * DiveSideReach;    // 0x102a5b7d / 0x102a5b8f
		const FVector LeftEnd = FromUnits + Right * -DiveSideReach;    // 0x102a5ba3 / 0x102a5bb2
		FMotorMoveTrace Trace;
		if (SelectWeightedSequenceForActivity(ActDiveRight) != INDEX_NONE   // 0x102a5bc0 / 0x102a5bca
			&& MotorMoveTraceSweep(0, FromUnits, RightEnd, MaskNpcSolid, DiveProbeExtent, nullptr,
				Trace))                                           // 0x102a5c04 / 0x102a5c0b / 0x102a5bd0 the trace record's ctor; 0x102a5bfd GetMoveProbe
		{
			NpcFlags.Set(EElysiumNpcFlag::ANIM_MOVEMENT);         // 0x102a5c14
			SetIdealActivity(ActDiveRight);                       // 0x102a5c20
			return 0;
		}
		if (SelectWeightedSequenceForActivity(ActDiveLeft) != INDEX_NONE    // 0x102a5c3b / 0x102a5c42
			&& MotorMoveTraceSweep(0, FromUnits, LeftEnd, MaskNpcSolid, DiveProbeExtent, nullptr,
				Trace))                                           // 0x102a5c79 / 0x102a5c80 / 0x102a5c48 the trace record's ctor; 0x102a5c72 GetMoveProbe
		{
			NpcFlags.Set(EElysiumNpcFlag::ANIM_MOVEMENT);         // 0x102a5c89
			SetIdealActivity(ActDiveLeft);                        // 0x102a5c95
			return 0;
		}
		StartTask19Fail(0x3736, 0xe);                             // 0x102a5cc1
		return 0;
	}

	// --- index 0x79: TASK_ATTEMPT_DIVE_FORWARD ---------------------------------------------------
	case TASK_ATTEMPT_DIVE_FORWARD:
	{
		FVector FromUnits = StartTask19_2Units(Origin);           // 0x102a5cd8
		FromUnits.Z += StepHeight() * ElysiumNpcTunables::Half;   // 0x102a5cf6 / 0x102a5cfc
		const FVector End = FromUnits + Forward * DiveForwardReach;    // 0x102a5d1d / 0x102a5d2f
		FMotorMoveTrace Trace;
		if (SelectWeightedSequenceForActivity(ActDiveForward) != INDEX_NONE   // 0x102a5d3d / 0x102a5d45
			&& MotorMoveTraceSweep(0, FromUnits, End, MaskNpcSolid, DiveProbeExtent, nullptr,
				Trace))                                           // 0x102a5d7f / 0x102a5d86 / 0x102a5d4b the trace record's ctor; 0x102a5d78 GetMoveProbe
		{
			NpcFlags.Set(EElysiumNpcFlag::ANIM_MOVEMENT);         // 0x102a5d8f
			SetIdealActivity(ActDiveForward);                     // 0x102a5d9b
			return 0;
		}
		StartTask19Fail(0x3759, 0xe);                             // 0x102a5dc7
		return 0;
	}

	// --- index 0x7a..0x7c: the cover animations --------------------------------------------------
	case TASK_PLAY_COVER_INTO:
		if (TaskTailRestartHintActivity(EElysiumHintActivityQuery::Query102a13d0))   // 0x102a5ddc 0x102a1560
		{
			return 0;                                             // 0x102a5de3 -> 0x102a77ea
		}
		TaskComplete(false);                                      // 0x102a5ded
		return 0;
	case TASK_PLAY_COVER_IDLE:
		(void)TaskTailRestartHintActivity(EElysiumHintActivityQuery::Query102a1420);   // 0x102a5e01 0x102a1590
		TaskComplete(false);                                      // 0x102a5e0a
		return 0;
	case TASK_PLAY_COVER_OUTOF:
		if (TaskTailRestartHintActivity(EElysiumHintActivityQuery::Query102a1470))   // 0x102a5e1e 0x102a15c0
		{
			return 0;                                             // 0x102a5e25 -> 0x102a77ea
		}
		TaskComplete(false);                                      // 0x102a5e2f
		return 0;

	// --- index 0x7d: TASK_PLAY_COVER_AIM ---------------------------------------------------------
	case TASK_PLAY_COVER_AIM:
	{
		(void)TaskTailRestartHintActivity(EElysiumHintActivityQuery::Query102a14c0);   // 0x102a5e43 0x102a15f0
		FVector AimUnits = FVector::ZeroVector;                   // 0x102a5e4c
		const FElysiumEntity* Override =
			World != nullptr ? World->Resolve(ShootTargetOverride) : nullptr;   // 0x102a5e5b +0x5ba8
		if (Override != nullptr)                                  // 0x102a5e62
		{
			AimUnits = StartTask19_2Units(Override->Origin);      // 0x102a5e6f / 0x102a5e7a / 0x102a5e66
		}
		else
		{
			FElysiumEntity* Enemy = TaskTailEnemy167();           // 0x102a5e85
			const FElysiumEntity* CoverObject =
				World != nullptr ? World->Resolve(ScheduleHost.HintCoverObject) : nullptr;   // 0x102a5e8f 0x102a9960 +0x6448
			if (CoverObject == Enemy)                             // 0x102a5e94 / 0x102a5e98
			{
				// Both null compares EQUAL -- retail then asks the memory for a null enemy's LKP and
				// aims at the origin. Reproduced.
				AimUnits = StartTask19_2Units(Conditions19LastKnownPosition(TaskTailEnemy167()));   // 0x102a5e9c / 0x102a5eb7 / 0x102a5eaf slot 541 / 0x102a5ec1 the copy
			}
			else
			{
				TaskComplete(false);                              // 0x102a5eca, falls through
			}
		}
		if (FInAimCone(AimUnits * ElysiumMove::U))                // 0x102a5ed8 slot 364 / 0x102a5ee2
		{
			TaskComplete(false);                                  // 0x102a6873
			return 0;
		}
		StartTaskMotorHoldYaw();                                  // 0x102a5eef / 0x102a5ee8 GetMotor
		StartTaskMotorSetIdealYawToTarget(AimUnits * ElysiumMove::U);                      // 0x102a5f04 / 0x102a5efd GetMotor
		return 0;
	}

	// --- index 0x7e: TASK_SNAP_TO_HINT -----------------------------------------------------------
	case TASK_SNAP_TO_HINT:
	{
		if (BaseScheduleHost.HintNode == INDEX_NONE)              // 0x102a5f1e
		{
			StartTask19Fail(0x37aa, 4);                           // 0x102a5f73
			return 0;
		}
		FVector PointUnits = FVector::ZeroVector;                 // 0x102a5f24
		// `0x102b6120(&pos, 0)` always yields a position in retail (the hint is live here). The
		// port's answer is false while its hint store is a seam; then there is no point to snap to
		// and the snap is skipped rather than teleporting the body to the origin -- a named guard.
		if (ApplyHintLeanOffset(PointUnits, /*bStanding=*/false))   // 0x102a5f32
		{
			SetOrigin(PointUnits * ElysiumMove::U);               // 0x102a5f3e 0x102885d0 -> slot 62
		}
		TaskComplete(false);                                      // 0x102a5f47
		return 0;
	}

	// --- index 0x7f: TASK_KICK_HINT --------------------------------------------------------------
	case TASK_KICK_HINT:
		if (BaseScheduleHost.HintNode == INDEX_NONE)              // 0x102a5f90
		{
			StartTask19Fail(0x37bd, 4);                           // 0x102a5fb9
			return 0;
		}
		RestartIdealActivityId(ActKick);                          // 0x102a5f97
		++TaskTailHintFires;                                      // 0x102a60df 0x102d0910(hint, this)
		ClearScheduleHint(KickHintReuseDelay);                    // 0x102a60eb 0x10295ab0(60.0)
		return 0;

	// --- index 0x80: TASK_KICK_HINT_AT -----------------------------------------------------------
	case TASK_KICK_HINT_AT:
	{
		if (BaseScheduleHost.HintNode == INDEX_NONE)              // 0x102a5fd6
		{
			StartTask19Fail(0x37f4, 4);                           // 0x102a6115
			return 0;
		}
		RestartIdealActivityId(ActKick);                          // 0x102a5fe1
		const FString HintTargetName = TaskTailHintTargetName();      // 0x102a5fec 0x102a9c40
		if (!HintTargetName.IsEmpty())                                // 0x102a5ff1 / 0x102a5ff4
		{
			ScheduleHost.KickProp = TaskTailFindEntityByName(HintTargetName);   // 0x102a6013 / 0x102a601b / 0x102a6006
			if (World == nullptr || World->Resolve(ScheduleHost.KickProp) == nullptr)   // 0x102a6024 0x102c63b0(h, 0) / 0x102a602b JZ
			{
				// RETAIL DEFECT, reproduced: `0x102a602d` pushes the format and NO arguments, so the
				// two `%s` read whatever the stack holds. Transcribed as the bare format.
				EmitDevMsg(GStartTask19_2KickNotFound, GStartTask19_2KickNotFound);   // 0x102a6032
			}
		}
		if (FElysiumEntity* Prop =
				World != nullptr ? World->Resolve(ScheduleHost.KickProp) : nullptr)   // 0x102a6045 / 0x102a604c
		{
			const FVector PropUnits = StartTask19_2Units(Prop->Origin);   // 0x102a605d / 0x102a6054 the handle's entity
			const FVector MeUnits = StartTask19_2Units(Origin);          // 0x102a6069
			const FVector D = MeUnits - PropUnits;                   // 0x102a607d..0x102a608a / 0x102a6078
			const float Dist2 = static_cast<float>(D.X * D.X + D.Y * D.Y + D.Z * D.Z);
			if (Dist2 <= KickPropMaxDistSqr)                         // 0x102a609d / 0x102a60ae JP only past 128
			{
				++TaskTailKicks;                                     // 0x102a60ba 0x102b6890(this, prop) / 0x102a60b2 the handle's entity
				TaskTailLastKicked = Prop->Handle;
			}
			else
			{
				EmitDevMsg(GStartTask19_2KickTooFar, GStartTask19_2KickTooFar);   // 0x102a60c1, no arguments either / 0x102a60c6
			}
			ScheduleHost.KickProp = FElysiumEntityHandle::Invalid();   // 0x102a60d3 0x1028ac80(h, 0)
		}
		++TaskTailHintFires;                                      // 0x102a60df 0x102d0910(hint, this)
		ClearScheduleHint(KickHintReuseDelay);                    // 0x102a60eb
		return 0;
	}

	// --- index 0x81: TASK_GET_PATH_TO_KICK_PROP --------------------------------------------------
	case TASK_GET_PATH_TO_KICK_PROP:
	{
		FElysiumEntity* Prop = World != nullptr ? World->Resolve(ScheduleHost.KickProp) : nullptr;   // 0x102a6132
		if (Prop == nullptr)                                      // 0x102a613d
		{
			StartTask19Fail(0x3812, 0x25);                        // 0x102a6276
			return 0;
		}
		FElysiumEntity* Enemy = TaskTailEnemy167();               // 0x102a6143 / 0x102a615e slot 167
		if (Enemy == nullptr)                                     // 0x102a614b
		{
			StartTask19Fail(0x380d, 6);                           // 0x102a624d
			return 0;
		}
		const FVector PropUnits = StartTask19_2Units(Prop->Origin);   // 0x102a616a / 0x102a6153 the handle's entity
		FVector Dir = StartTask19_2Units(Enemy->Origin) - PropUnits;  // 0x102a617a / 0x102a6182
		Dir.Normalize();                                          // 0x102a618b
		// `0x102a9c80(&goal, 10, -1, -1.0, 0, DAT_10923dd8)`, then the type word OVERWRITTEN to 4
		// (`0x102a61b4 MOV [ESP+0xa4],4`) and the target word zeroed (`0x102a620f`).
		FTaskTailNavGoal Goal;
		Goal.Type = GoalTypeKickProp;                             // 0x102a61a2 / 0x102a61ad 0x102a9c80
		Goal.Activity = INDEX_NONE;                               // 0x102a61a0
		Goal.Tolerance = GoalToleranceDefault;                    // 0x102a6196
		Goal.GoalFlags = 0;
		Goal.Type = GoalTypeLocation;                             // 0x102a61b4 / 0x102a61bf the handle's entity
		// `0x102a61d7` `dir * 64`, then `0x102a61f1` `0x1001395d` = prop - (dir * 64): the kick spot is
		// on the far side of the prop from the enemy (the same subtract helper `0x102a5a93` uses for
		// goal - origin).
		Goal.DestUnits = PropUnits - Dir * KickPropGoalReach;     // 0x102a61d7 / 0x102a61f1 / 0x102a61fe / 0x102a61e9 GetAbsOrigin
		Goal.Target = FElysiumEntityHandle::Invalid();            // 0x102a620f
		(void)TaskTailNavSetGoal(Goal, 0);                        // 0x102a6221, answer unread / 0x102a621a GetNavigator
		return 0;
	}

	// --- index 0x82: TASK_SNAP_TO_KICK_PROP ------------------------------------------------------
	case TASK_SNAP_TO_KICK_PROP:
		if (World != nullptr && World->Resolve(ScheduleHost.KickProp) != nullptr)   // 0x102a6291 / 0x102a629a
		{
			TaskComplete(false);                                  // 0x102a629e
			return 0;
		}
		StartTask19Fail(0x381f, 0x25);                            // 0x102a62c8
		return 0;

	// --- index 0x83: TASK_KICK_PROP --------------------------------------------------------------
	case TASK_KICK_PROP:
	{
		FElysiumEntity* Prop = World != nullptr ? World->Resolve(ScheduleHost.KickProp) : nullptr;   // 0x102a62e5
		if (Prop == nullptr)                                      // 0x102a62ee
		{
			StartTask19Fail(0x3832, 0x25);                        // 0x102a6337
			return 0;
		}
		RestartIdealActivityId(ActKick);                          // 0x102a62f5
		++TaskTailKicks;                                          // 0x102a6304 0x102b6890(this, prop) / 0x102a62fc the handle's entity
		TaskTailLastKicked = Prop->Handle;
		ScheduleHost.KickProp = FElysiumEntityHandle::Invalid();  // 0x102a630d
		return 0;
	}

	// --- index 0x84: TASK_GET_PATH_TO_LASTENEMY_LKP ----------------------------------------------
	case TASK_GET_PATH_TO_LASTENEMY_LKP:
	{
		FElysiumEntity* EnemyEntity = GetEnemy();                      // 0x102a654f slot 168
		if (EnemyEntity == nullptr || IsUnreachable(EnemyEntity))           // 0x102a6559 / 0x102a6564 slot 530 / 0x102a656c
		{
			StartTask19Fail(0x38c3, 0xc);                         // 0x102a6681
			return 0;
		}
		FTaskTailNavGoal Goal;                                    // 0x102a65a6 0x102a9d20(&goal, lkp, -1, -1.0, 0, ...) / 0x102a65ad GetNavigator
		Goal.Type = GoalTypeLocation;
		Goal.DestUnits = StartTask19_2Units(Conditions19LastKnownPosition(EnemyEntity));           // 0x102a6591 / 0x102a6599
		Goal.Activity = INDEX_NONE;
		Goal.Tolerance = GoalToleranceDefault;
		// The fourth argument is seeded from `0x102f2fc0` (`0x102a65b4`, path `+0x20`,
		// `NavPathScalar20`). The port's slot-563 bodies read and write both words in CENTIMETRES
		// (`ElysiumNpcPositions2.cpp`); the goal's word is SOURCE units with the -1.0 "keep"
		// sentinel, which a body that writes nothing leaves standing (the base arm `0x1028443e`'s idiom).
		float ToleranceScratchCm = Goal.Tolerance;
		float ScalarCm = NavPathScalar20 * ElysiumMove::U;
		FVector ChaseCm = Goal.DestUnits * ElysiumMove::U;
		// Slot 563 `(t, &goal.dest, &goal.tolerance, &scalar)`.
		TranslateEnemyChasePosition(EnemyEntity, ChaseCm, &ToleranceScratchCm, &ScalarCm);   // 0x102a65d7
		if (ToleranceScratchCm != Goal.Tolerance)
		{
			Goal.Tolerance = ToleranceScratchCm / ElysiumMove::U;
		}
		Goal.DestUnits = StartTask19_2Units(ChaseCm);
		// A built route is completed by `SetGoal`'s own route build (navigator slot 2) and then again
		// here; a refused one is failed by `OnNavFailed(0xc)` and then again here -- both retail.
		if (!TaskTailNavSetGoal(Goal, SetGoalFlagsLkp))           // 0x102a65f0 / 0x102a65f7 / 0x102a65e9 GetNavigator
		{
			EmitDevMsg(TEXT("GetPathToLastEnemyLKP failed!!\n"), TEXT("GetPathToLastEnemyLKP failed!!\n"));   // 0x102a6629 DevWarning(2, ...)
			RememberUnreachable(EnemyEntity);                          // 0x102a6635 0x10274080
			StartTask19Fail(0x38d8, 0xc);                         // 0x102a6654
			return 0;
		}
		NavPathScalar20 = ScalarCm / ElysiumMove::U;              // 0x102a6607 0x102f2fe0 / 0x102a6600 GetNavigator
		TaskComplete(false);                                      // 0x102a6610
		return 0;
	}

	// --- index 0x85: TASK_FIND_COVER_FROM_LASTENEMY_LKP ------------------------------------------
	case TASK_FIND_COVER_FROM_LASTENEMY_LKP:
	{
		FElysiumEntity* Threat = GetEnemy();                      // 0x102a6698 slot 168
		if (Threat == nullptr)                                    // 0x102a66a2
		{
			Threat = this;                                        // 0x102a66a4
		}
		const FVector ThreatEyeCm = Threat->EyePosition();        // 0x102a66b3 slot 193
		if (StartTaskFindLateralCover(ThreatEyeCm, Threat))       // 0x102a66bc 0x102784a0 / 0x102a66c3
		{
			BaseScheduleHost.MoveWaitFinished = Now + Operand;    // 0x102a66cb..0x102a66d1 +0x5cf0
			StartTask19Complete();                              // 0x102a66d7
			return 0;
		}
		FVector CoverCm = FVector::ZeroVector;                    // 0x102a66f1
		const float RadiusUnits = CoverRadius();                  // 0x102a6700 slot 550
		// `0x102edc80(nav, threat->GetAbsOrigin(), threat->EyePosition(), 0.0, radius, &out,
		// threat)` -- `CAI_Navigator::FindCoverPos`, family StartTask19's one port body.
		const bool bFound = StartTaskFindCoverPos(Threat->Origin, ThreatEyeCm, 0.f, RadiusUnits,
			CoverCm);                                             // 0x102a6733 / 0x102a6718 slot 193 / 0x102a6723 GetAbsOrigin / 0x102a672c GetNavigator
		if (!bFound)                                              // 0x102a673a
		{
			StartTask19Fail(0x3907, 8);                           // 0x102a67ee
			return 0;
		}
		FTaskTailNavGoal Goal;                                    // 0x102a6768 0x102a9dc0(&goal, 6, &out, 0x13, -2.0, 0, ...)
		Goal.Type = GoalTypeCover;
		Goal.DestUnits = StartTask19_2Units(CoverCm);
		Goal.Activity = ActRun;
		Goal.Tolerance = GoalToleranceHull;
		(void)TaskTailNavSetGoal(Goal, 0);                        // 0x102a6780, answer unread / 0x102a6779 GetNavigator
		if (BaseScheduleHost.HintNode != INDEX_NONE)              // 0x102a6785 / 0x102a678d
		{
			StartTaskSetArrivalActivity(GetCoverActivity(&BaseScheduleHost.HintNode)); // 0x102a6798 slot 569 / 0x102a67a8 0x102ee410 / 0x102a67a1 GetNavigator
			FVector Direction = FVector::ZeroVector;
			StartTaskHintFacing(BaseScheduleHost.HintNode, Direction);   // 0x102a67bb 0x102d11f0
			StartTaskSetArrivalDirection(Direction);              // 0x102a67ca 0x102ee530 / 0x102a67c3 GetNavigator
		}
		BaseScheduleHost.MoveWaitFinished = Now + Operand;        // 0x102a76a9..0x102a76b5
		return 0;
	}

	// --- index 0x86..0x88: the botch resolutions ------------------------------------------------
	// `0x102a9770(this, 0x40000)` answers 1 when BOTCHED_ATTACK is CLEAR (`NEG/SBB/INC`).
	case TASK_RESOLVE_BOTCH:
		if (!NpcFlags.Has(EElysiumNpcFlag::BOTCHED_ATTACK))       // 0x102a6808 / 0x102a6811
		{
			TaskComplete(false);                                  // 0x102a6815
			return 0;
		}
		RestartIdealActivityId(ActResolveBotch);                  // 0x102a6829
		return 0;
	case TASK_RESOLVE_BOTCH_IN_COVER:
		if (!NpcFlags.Has(EElysiumNpcFlag::BOTCHED_ATTACK))       // 0x102a6842 / 0x102a684b
		{
			TaskComplete(false);                                  // 0x102a684f
			return 0;
		}
		(void)TaskTailRestartHintActivity(EElysiumHintActivityQuery::Query102a1510);   // 0x102a6896 0x102a1620
		return 0;
	case TASK_RESOLVE_BOTCH_OUT_OF_COVER:
		if (!NpcFlags.Has(EElysiumNpcFlag::BOTCHED_ATTACK))       // 0x102a6868 / 0x102a6871
		{
			TaskComplete(false);                                  // 0x102a6875
			return 0;
		}
		if (TaskTailRestartHintActivity(EElysiumHintActivityQuery::Query102a13d0))   // 0x102a6887 0x102a1560
		{
			return 0;                                             // 0x102a688e -> 0x102a77ea
		}
		(void)TaskTailRestartHintActivity(EElysiumHintActivityQuery::Query102a1510);   // 0x102a6896 0x102a1620
		return 0;

	// --- index 0x89 / 0x8a: the wait-finished delta ----------------------------------------------
	case TASK_WAIT_FINISH_SET:
		ScheduleHost.bWaitFinishedSet = true;                     // 0x102a68ab +0x6334
		ScheduleHost.WaitFinishedDelta = Operand;                 // 0x102a68b2 +0x6330
		StartTask19Complete();                                  // 0x102a68b8 -> 0x102a66d7
		return 0;
	case TASK_WAIT_FINISH_ADD_RANDOM:
		ScheduleHost.WaitFinishedDelta += StartTask19_2RandomFloat(0.f, Operand);   // 0x102a68cb / 0x102a68ce
		StartTask19Complete();                                  // 0x102a68da -> 0x102a66d7
		return 0;

	// --- index 0x8b: TASK_STEP_BACK / TASK_STEP_BACK_RUN -----------------------------------------
	case TASK_STEP_BACK:
	case TASK_STEP_BACK_RUN:
	{
		const bool bRun = TaskId == TASK_STEP_BACK_RUN;     // 0x102a68e1 / 0x102a68e6
		const int32 Activity = bRun ? ActStepBackRun : ActStepBackWalk;   // 0x102a68ed
		const float Delta = ScheduleHost.WaitFinishedDelta;
		if (SelectWeightedSequenceForActivity(Activity) == INDEX_NONE)    // 0x102a68f5 / 0x102a68fd
		{
			const int32 TurnActivity = bRun ? ActRun : ActWalk;   // 0x102a6903..0x102a6920
			if (!TraceMoveClearanceAtYaw(TurnActivity, ElysiumNpcTunables::OneEighty, Delta))   // 0x102a6924 0x102a1650 / 0x102a692d JZ
			{
				StartTask19Fail(0x396f, 0x15);                    // 0x102a697b
				return 0;
			}
			ScheduleHost.DesiredMoveYaw = ElysiumNpcTunables::OneEighty;   // 0x102a6934 0x102a9940
			BaseScheduleHost.WaitFinished = Now + Delta;          // 0x102a6942..0x102a694b
			RestartIdealActivityId(TurnActivity);                 // 0x102a6951
			return 0;
		}
		FVector FromUnits = StartTask19_2Units(Origin);           // 0x102a6992
		FromUnits.Z += StepHeight() * ElysiumNpcTunables::Half;   // 0x102a69b0 / 0x102a69b6
		// `0x102a69e6` / `0x102a69ed` `m_vecForward * 50 * delta`, then `0x102a69ff` `0x1001395d`
		// (the subtract helper): the probe runs BACKWARD from the body.
		const FVector ToUnits = FromUnits - Forward * StepBackReach * Delta;   // 0x102a69e6 / 0x102a69ed / 0x102a69ff
		FVector HullMins = FVector::ZeroVector;
		FVector HullMaxs = FVector::ZeroVector;
		(void)RetailCollisionExtents(*this, HullMins, HullMaxs);  // 0x102a6a23 / 0x102a6a2b (+0x270)
		FKernelHullTrace Trace;                                   // 0x102a6a0b
		(void)KernelHullTrace(FromUnits, ToUnits, HullMins, HullMaxs, MaskNpcSolid, Trace);   // 0x102a6a47 0x102a99e0 / 0x102a6a40 GetMoveProbe
		if (Trace.Fraction != 1.f)                                // 0x102a6a4c / 0x102a6a5e
		{
			StartTask19Fail(0x3997, 0xe);                         // 0x102a6aa4
			return 0;
		}
		BaseScheduleHost.WaitFinished = Now + Delta;              // 0x102a6a67..0x102a6a72
		RestartIdealActivityId(Activity);                         // 0x102a6a78
		return 0;
	}

	// --- index 0x8c: TASK_FACE_LASTANGLE ---------------------------------------------------------
	case TASK_FACE_LASTANGLE:
		StartTaskMotorHoldYaw();                                  // 0x102a6fe1 / 0x102a6fda GetMotor
		StartTaskMotorSetIdealYaw(StartTaskAngleMod(static_cast<float>(LastFacing.Y)));   // 0x102a6fee 0x102a95e0(1) / 0x102a6ff6 AngleMod -> 0x102a63fc
		return 0;

	// --- index 0x8d: TASK_PLAY_SOUND -------------------------------------------------------------
	case TASK_PLAY_SOUND:
		// `0x101f5950(&DAT_1073dc28, this, (int)operand, 2, 1.0, 1.25)` -- the operand IS the id.
		SpeakVSound(nullptr, StartTask19_2Ftol(Operand), SoundChannelVoice, SoundVolume,
			SoundAttenuation);                                    // 0x102a700f / 0x102a701b
		StartTask19Complete();                                  // 0x102a7020 -> 0x102a66d7
		return 0;

	// --- index 0x8e: TASK_GET_PATH_TO_SAVEPOSITION_LOS -------------------------------------------
	case TASK_GET_PATH_TO_SAVEPOSITION_LOS:
	{
		const FVector FromCm = SavePosition;                      // 0x102a703b +0x5dd0
		const FVector ToCm = FromCm + (EyePosition() - Origin);   // 0x102a7045 +0x184
		FVector OutCm = FVector::ZeroVector;                      // 0x102a7029
		// `0x102edaa0(nav, from, to, 0.0, 4096.0, 1.0, 1, &out)` -- `CAI_Navigator::FindLosPos`.
		if (!StartTaskFindLosPos(FromCm, ToCm, 0.f, 4096.f, OutCm))   // 0x102a706f 0x102edaa0 / 0x102a7076 / 0x102a7068 GetNavigator
		{
			StartTask19Fail(0x3ac1, 0xb);                         // 0x102a70d3
			return 0;
		}
		FTaskTailNavGoal Goal;                                    // 0x102a7098 0x102a9d20(&goal, &out, 0x13, -2.0, 0, ...)
		Goal.Type = GoalTypeLocation;
		Goal.DestUnits = StartTask19_2Units(OutCm);
		Goal.Activity = ActRun;
		Goal.Tolerance = GoalToleranceHull;
		(void)TaskTailNavSetGoal(Goal, 0);                        // 0x102a70a7, answer unread / 0x102a70a0 GetNavigator
		return 0;
	}

	// --- index 0x8f: TASK_SLEEP_BOUNDING_BOX -----------------------------------------------------
	case TASK_SLEEP_BOUNDING_BOX:
		if (Operand != 0.f)                                       // 0x102a70e6..0x102a70f6 JNP
		{
			NpcFlags.SetRawWord2Bits(Flag2SleepBoundingBox);      // 0x102a70fd 0x102a9800
			ScheduleHost.SavedSleepExtents = GetAttackExtents();  // 0x102a710e slot 16 / 0x102a7118 +0x65d0
			SetAttackExtents(FVector(SleepExtentsXY, SleepExtentsXY, SleepExtentsZ) * ElysiumMove::U);   // 0x102a713d slot 15 / 0x102a7135 the Vector
			StartTask19Complete();                              // 0x102a7140 -> 0x102a66d7
			return 0;
		}
		SetAttackExtents(ScheduleHost.SavedSleepExtents);         // 0x102a714e slot 15
		ScheduleHost.SavedSleepExtents = FVector(SleepExtentsCleared);   // 0x102a716f / 0x102a7167 the Vector
		NpcFlags.ClearRawWord2Bits(Flag2SleepBoundingBox);        // 0x102a717b 0x102a9830
		StartTask19Complete();                                  // 0x102a7180 -> 0x102a66d7
		return 0;

	// --- index 0x90: TASK_MELEE_CIRCLE_ENEMY -----------------------------------------------------
	case TASK_MELEE_CIRCLE_ENEMY:
	{
		int32 Tries = 0;                                          // 0x102a6ab7
		if (SelectWeightedSequenceForActivity(ActCombatMove) == INDEX_NONE)   // 0x102a6ac1 / 0x102a6ac9
		{
			StartTask19Fail(0x39a7, 0x15);                        // 0x102a6ae5
			Tries = CircleFailSuppressed;                         // 0x102a6aeb, and the arm goes on
		}
		int32 Side = TaskTailCoordinatorCircleSide(TaskTailEnemy167());   // 0x102a6af4 / 0x102a6b02
		if (Side == 0)                                            // 0x102a6b0b / 0x102a6b0f JNZ
		{
			Side = StartTask19_2RandomInt(0, 1) != 0 ? -1 : 1;    // 0x102a6b1c..0x102a6b28
		}
		const float Delta = ScheduleHost.WaitFinishedDelta;
		while (Tries < CircleTries)                               // 0x102a6b2d / 0x102a6b83 / 0x102a6b30 JGE
		{
			const float Yaw = StartTask19_2RandomFloat(Side * CircleFirstYaw,
				Side * CircleSecondYaw);                          // 0x102a6b32..0x102a6b56
			if (TraceMoveClearanceAtYaw(ActCombatMove, Yaw, Delta))   // 0x102a6b70 / 0x102a6b77
			{
				ScheduleHost.DesiredMoveYaw = Yaw;                // 0x102a6bc5
				BaseScheduleHost.WaitFinished = Now + Delta;      // 0x102a6bd6..0x102a6bdf
				RestartIdealActivityId(ActCombatMove);            // 0x102a6be5
				return 0;
			}
			Side = -Side;                                         // 0x102a6b79
			++Tries;                                              // 0x102a6b7b
		}
		if (Tries == CircleFailSuppressed)                        // 0x102a6b85 / 0x102a6b8b
		{
			return 0;                                             // -> 0x102a77ea
		}
		StartTask19Fail(0x39d5, 0xe);                             // 0x102a6b91 -> 0x102a6ba5 / 0x102a6bab slot 448
		return 0;
	}

	// --- index 0x91: TASK_CIRCLE_ENEMY / TASK_CIRCLE_ENEMY_FULLCYCLE -----------------------------
	case TASK_CIRCLE_ENEMY:
	case TASK_CIRCLE_ENEMY_FULLCYCLE:
	{
		float Radius = ResolveTaskDistance(Operand);              // 0x102a6c09 slot 418
		int32 Tries = 0;                                          // 0x102a6bff
		float Distance = 0.f;                                     // 0x102a6c1f
		if (FElysiumEntity* EnemyEntity = GetEnemy())                  // 0x102a6c17 slot 168 / 0x102a6c23
		{
			Distance = static_cast<float>(FVector::Dist(StartTask19_2Units(EnemyEntity->Origin),
				StartTask19_2Units(Origin)));                     // 0x102a6c3c 0x102a9570 / 0x102a6c29 / 0x102a6c34 GetAbsOrigin
		}
		else
		{
			StartTask19Fail(0x39ec, 6);                           // 0x102a6c61
			Tries = CircleFailSuppressed;                         // 0x102a6c67
		}
		if (TaskTailCircleDistOverride() > 0.f)                   // 0x102a6c71 / 0x102a6c83
		{
			Radius = TaskTailCircleDistOverride();                // 0x102a6c8a
		}
		if (Radius == CircleRadiusSentinel && ActiveWeaponEntity() != nullptr)   // 0x102a6c97 / 0x102a6cad / 0x102a6ca2 JP / 0x102a6ca6 GetActiveWeapon
		{
			float MaxRange = 0.f;
			float MinRange = 0.f;
			(void)ActiveWeaponMaxRangeUnits(MaxRange);            // 0x102a6cb6 +0x8c0 / 0x102a6cb1 GetActiveWeapon
			(void)TaskTailWeaponMinRangeUnits(MinRange);          // 0x102a6ccd +0x8b8 / 0x102a6cc8 GetActiveWeapon
			Radius = static_cast<float>(MaxRange * CircleMaxRangeWeight
				+ MinRange * CircleMinRangeWeight);               // 0x102a6cbc..0x102a6cdd
		}
		int32 Activity = ActCombatMove;                           // 0x102a6c01 / 0x102a6d36
		if (SelectWeightedSequenceForActivity(Activity) == INDEX_NONE)   // 0x102a6cea / 0x102a6cf4
		{
			Activity = ActWalk;                                   // 0x102a6cf6
			if (SelectWeightedSequenceForActivity(Activity) == INDEX_NONE)   // 0x102a6d00 / 0x102a6d07
			{
				StartTask19Fail(0x3a06, 0x15);                    // 0x102a6d23
				return 0;
			}
		}
		const float Delta = ScheduleHost.WaitFinishedDelta;
		while (Tries < CircleTries)                               // 0x102a6d3a / 0x102a6d73 / 0x102a6d3d JGE
		{
			const float Yaw = Slot603(&Distance, &Radius);        // 0x102a6d4d slot 603
			if (TraceMoveClearanceAtYaw(Activity, Yaw, Delta))    // 0x102a6d66 / 0x102a6d6d
			{
				ScheduleHost.DesiredMoveYaw = Yaw;                // 0x102a6db5
				BaseScheduleHost.WaitFinished = Now + Delta;      // 0x102a6dc3..0x102a6dcc
				RestartIdealActivityId(Activity);                 // 0x102a6dd2
				return 0;
			}
			++Tries;                                              // 0x102a6d6f
		}
		if (Tries == CircleFailSuppressed)                        // 0x102a6d75 / 0x102a6d7b
		{
			return 0;                                             // -> 0x102a77ea
		}
		StartTask19Fail(0x3a30, 0xe);                             // 0x102a6d9b
		return 0;
	}

	// --- index 0x93: TASK_SET_TOLERANCE_DIST_COMBATMOVE ------------------------------------------
	case TASK_SET_TOLERANCE_DIST_COMBATMOVE:
	{
		// `m_flGoalTolerance (+0x6320) = debug_melee_advance_combatmove_dist`, then
		// `+= RandomFloat(0, ResolveTaskDistance(operand))`, then the tolerance tail `0x102a42df`.
		float ToleranceUnits = ElysiumNpcTunables::ConVarFloat(
			ElysiumNpcTunables::EConVar::DebugMeleeAdvanceCombatmoveDist);   // 0x102a5984 0x102a9640(0x10924a18)
		ToleranceUnits += StartTask19_2RandomFloat(0.f, ResolveTaskDistance(Operand));   // 0x102a599e / 0x102a59b0
		ScheduleHost.GoalToleranceCm = ToleranceUnits * ElysiumMove::U;   // 0x102a59c1 +0x6320
		// `0x102ee1c0(nav, tol)` (path `+0x28`, `0x102a59d1`) and, at the shared tail `0x102a42df`,
		// `0x102f2fe0(nav, m_flGoalTolerance)` (path `+0x20`) -- family StartTask19's tolerance tail.
		StartTask19SetNavTolerances(ToleranceUnits, ToleranceUnits);   // 0x102a59d1 / 0x102a42df / 0x102a59ca / 0x102a59df GetNavigator
		TaskComplete(false);                                      // 0x102a42e8
		return 0;
	}

	// --- index 0x94: TASK_MELEE_CHEER ------------------------------------------------------------
	case TASK_MELEE_CHEER:
	{
		BaseScheduleHost.WaitFinished = Now + ScheduleHost.WaitFinishedDelta;   // 0x102a718c..0x102a7197
		const int32 Draw = StartTask19_2RandomInt(0, 1);          // 0x102a71a5
		// 0 -> ACT_IDLE, 1 -> 3; any other draw reads an uninitialised local (`0x102a71be`), which
		// `RandomInt(0, 1)` never answers.
		const int32 Base = Draw == 0 ? ActIdle : ActRangeAttack2Alt;   // 0x102a71ab / 0x102a71b0 / 0x102a71b7 / 0x102a71ae JNZ
		int32 WeaponActivity = 0;
		RestartIdealActivityId(TranslateActivityNumber(Base, WeaponActivity));   // 0x102a71ca / 0x102a71d2
		return 0;
	}

	// --- index 0x95..0x99: the special-distance accumulator ------------------------------------
	case TASK_SET_SPECIAL_DISTANCE_ACCUM:
		SpecialDistanceAccum = ResolveTaskDistance(Operand);      // 0x102a71ec / 0x102a71f2
		StartTask19Complete();                                  // 0x102a71f8 -> 0x102a66d7
		return 0;
	case TASK_ADD_SPECIAL_DISTANCE_ACCUM:
		SpecialDistanceAccum = ResolveTaskDistance(Operand) + SpecialDistanceAccum;   // 0x102a7205 / 0x102a720b
		StartTask19Complete();                                  // 0x102a7217
		return 0;
	case TASK_ADD_SPECIAL_DISTANCE_ACCUM_RANDOM:
	{
		const float Resolved = ResolveTaskDistance(Operand);      // 0x102a7281
		SpecialDistanceAccum = StartTask19_2RandomFloat(0.f, Resolved) + SpecialDistanceAccum;   // 0x102a7293 / 0x102a7296
		StartTask19Complete();                                  // 0x102a72a2
		return 0;
	}
	case TASK_SUB_SPECIAL_DISTANCE_ACCUM:
		SpecialDistanceAccum = SpecialDistanceAccum - ResolveTaskDistance(Operand);   // 0x102a725a / 0x102a7260 FSUBR
		StartTask19Complete();                                  // 0x102a726c
		return 0;
	case TASK_SUB_SPECIAL_DISTANCE_ACCUM_RANDOM:
	{
		const float Resolved = ResolveTaskDistance(Operand);      // 0x102a722c
		SpecialDistanceAccum = SpecialDistanceAccum - StartTask19_2RandomFloat(0.f, Resolved);   // 0x102a723e / 0x102a7241 FSUBR
		StartTask19Complete();                                  // 0x102a724d
		return 0;
	}

	// --- index 0x9a: TASK_FACE_SAVEPOSITION ------------------------------------------------------
	case TASK_FACE_SAVEPOSITION:
		StartTaskMotorHoldYaw();                                  // 0x102a72b0 / 0x102a72a9 GetMotor
		StartTaskMotorSetIdealYawToTarget(SavePosition);   // 0x102a72c7 0x102e2020(motor, +0x5dd0, 0) / 0x102a72c0 GetMotor
		SetTurnActivity();                                        // 0x102a72d0 slot 572
		return 0;

	// --- index 0x9b: TASK_MAKE_OBLIVIOUS ---------------------------------------------------------
	case TASK_MAKE_OBLIVIOUS:
		if (Operand != 0.f)                                       // 0x102a72e3..0x102a72f3 JNP
		{
			NpcFlags.SetRawWord2Bits(Flag2MadeOblivious);         // 0x102a72fa 0x102a9800
			// `0x1026d130`: `SetEnemy(NULL)`, `DisconnectFromSquad`, `++m_iIsOblivious` -- the port's
			// one body of it is the director's `MakeNpcOblivious`.
			FElysiumScriptedSequence::MakeNpcOblivious(*this);    // 0x102a7301 0x100050d3 -> 0x1026d130
			FireOutput(FName(TEXT("OnIncapacitatedStart")), Handle);   // 0x102a7310 +0x5fd4
			StartTask19Complete();                              // 0x102a7315 -> 0x102a66d7
			return 0;
		}
		// `0x10007ea0` -> `0x1026d160`: `--m_iIsOblivious` floored at 0, then `ReconnectToSquad` --
		// the director's `ReleaseNpcOblivious`.
		FElysiumScriptedSequence::ReleaseNpcOblivious(*this);     // 0x102a731a 0x10007ea0
		NpcFlags.ClearRawWord2Bits(Flag2MadeOblivious);           // 0x102a7326 0x102a9830
		FireOutput(FName(TEXT("OnIncapacitatedEnd")), Handle);    // 0x102a7335 +0x5fec
		StartTask19Complete();                                  // 0x102a733a -> 0x102a66d7
		return 0;

	// --- index 0x9c: TASK_PLAY_COMBAT_START_SEQUENCE ---------------------------------------------
	case TASK_PLAY_COMBAT_START_SEQUENCE:
		// RETAIL DEFECT, reproduced: `0x10295460` answers -1 for "no sequence", but the arm tests
		// `== 0` (`0x102a7368 TEST EAX,EAX`), so it refuses only a model whose FIRST sequence is the
		// pick and plays on for a model that has none.
		if (SelectWeightedSequenceForActivity(CombatStartActivityId) == 0)   // 0x102a7363 / 0x102a736a
		{
			TaskFail(0x15);                                       // 0x102a738d, no +0x1b44/+0x1b48 write
			return 0;
		}
		RestartIdealActivityId(CombatStartActivityId);            // 0x102a7375
		return 0;

	// --- index 0x9d: TASK_SQUAD_NEW_ENEMY --------------------------------------------------------
	case TASK_SQUAD_NEW_ENEMY:
		if (ConnectedSquad() != nullptr && TaskTailEnemy167() != nullptr)   // 0x102a73a2 0x102a9910 / 0x102a73b5
		{
			// `SquadNewEnemy(squad, GetEnemy())` (`0x103161a0`, 0x102a73d0) -- no squad object / 0x102a73c7 slot 167
			// stands here, so `ConnectedSquad` answers null and this arm is unreachable.
		}
		StartTask19Complete();                                  // 0x102a73ab / 0x102a73bd / 0x102a73d5
		return 0;

	// --- index 0x9e..0xa1: the jump set ----------------------------------------------------------
	case TASK_PRE_JUMP:
		StartTaskMotorHoldYaw();                                  // 0x102a73e3 / 0x102a73dc GetMotor
		RestartIdealActivityId(ActPreJump);                       // 0x102a73ec
		return 0;
	case TASK_JUMP:
	{
		FVector VelocityUnits = FVector::ZeroVector;              // 0x102a7402
		Gravity = JumpGravity;                                    // 0x102a7410 0x102a9750
		(void)TaskTailSolveJump(VelocityUnits);                   // 0x102a7431 0x102c4c50
		++TaskTailJumpApplies;                                    // 0x102a7446 motor vtable +0x18 / 0x102a7438 GetMotor
		if (Motor != nullptr)
		{
			Motor->Launch(VelocityUnits * ElysiumMove::U);
		}
		NavSetType(1);                                            // 0x102a744d 0x1027d9b0(this, 1)
		bJumping = true;                                          // 0x102a7456 0x102a99a0(this, 1)
		return 0;
	}
	case TASK_LAND:
		StartTaskMotorHoldYaw();                                  // 0x102a7471 / 0x102a746a GetMotor
		RestartIdealActivityId(ActLand);                          // 0x102a747a
		return 0;
	case TASK_LAND_HARD:
		StartTaskMotorHoldYaw();                                  // 0x102a7495 / 0x102a748e GetMotor
		RestartIdealActivityId(ActLandHard);                      // 0x102a749e
		return 0;

	// --- index 0xa2: TASK_SET_INVINCIBLE ---------------------------------------------------------
	case TASK_SET_INVINCIBLE:
		// No complete and no fail: the task is left RUNNING (`0x102a74cb` / `0x102a74e0`).
		bInvincible = Operand != 0.f;                             // 0x102a74c5 / 0x102a74da +0x63d8 / 0x102a74be JNP
		return 0;

	// --- index 0xa3..0xa9: the activity copy-prop set -------------------------------------------
	case TASK_ACTIVITY_COPY_PROP_SPAWN:
		// `0x1018e790(this, 0x102a9860(task))` -- the operand's low word through the symbol table
		// `0x10936b74`, as a model name.
		++TaskTailCopyPropCalls;                                  // 0x102a74f6 / 0x102a74ef the handle's entity
		TaskTailLastCopyPropCall = 0x1018e790u;
		StartTask19Complete();                                  // 0x102a74fe
		return 0;
	case TASK_ACTIVITY_COPY_PROP_CLEAR:
		ClearOwnedActivityCopyProps();                            // 0x102a7504 0x1018e910
		StartTask19Complete();                                  // 0x102a750c
		return 0;
	case TASK_ACTIVITY_COPY_PROP_SET_COUNT:
		(void)StartTask19_2Ftol(Operand);                         // 0x102a7514
		++TaskTailCopyPropCalls;                                  // 0x102a751b 0x1018e9d0
		TaskTailLastCopyPropCall = 0x1018e9d0u;
		StartTask19Complete();                                  // 0x102a7523
		return 0;
	case TASK_ACTIVITY_COPY_PROP_SET_RANDOM:
		++TaskTailCopyPropCalls;                                  // 0x102a753c / 0x102a754d 0x1018eab0 / 0x102a7536 JNP
		TaskTailLastCopyPropCall = 0x1018eab0u;
		StartTask19Complete();                                  // 0x102a7544 / 0x102a7555
		return 0;
	case TASK_ACTIVITY_COPY_PROP_FADEOUT:
		if (TaskTailCopyPropFadeout(RawOperand) == 0)             // 0x102a755f / 0x102a7569
		{
			return 0;                                             // -> 0x102a77ea, still running
		}
		StartTask19Complete();                                  // 0x102a756f
		return 0;
	case TASK_ACTIVITY_COPY_PROP_FORWARD:
		++TaskTailCopyPropCalls;                                  // 0x102a7579 0x1018ecf0
		TaskTailLastCopyPropCall = 0x1018ecf0u;
		StartTask19Complete();                                  // 0x102a7581
		return 0;
	case TASK_ACTIVITY_COPY_PROP_NIVBED:
		++TaskTailCopyPropCalls;                                  // 0x102a758b 0x1018ec20
		TaskTailLastCopyPropCall = 0x1018ec20u;
		StartTask19Complete();                                  // 0x102a7593
		return 0;

	// --- index 0xaa: TASK_BURN_MODEL -------------------------------------------------------------
	case TASK_BURN_MODEL:
		BurnModel(GetSkeletonModelName(), false);                 // 0x102a759e slot 244 / 0x102a75a7 slot 243
		StartTask19Complete();                                  // 0x102a75ad
		return 0;

	// --- index 0xab: TASK_SET_LASTPOSITION_TO_INITIAL --------------------------------------------
	case TASK_SET_LASTPOSITION_TO_INITIAL:
		LastPosition = InitialPosition;                           // 0x102a75bf +0x5db8 = +0x62a8
		LastFacing = InitialAngles;                               // 0x102a75d1 +0x5dc4 = +0x62b4
		StartTask19Complete();                                  // 0x102a75d6
		return 0;

	// --- index 0xac: TASK_FIND_COVER_FROM_UNKNOWN_ATTACKER ---------------------------------------
	case TASK_FIND_COVER_FROM_UNKNOWN_ATTACKER:
	{
		const FElysiumEntity* Attacker = World != nullptr
			? World->Resolve(BaseMemory.LastDamageAttacker) : nullptr;   // 0x102a75ec +0x5b7c / 0x102a75df the Vector
		if (Attacker == nullptr)                                  // 0x102a75f3
		{
			StartTask19Fail(0x3be0, 0x21);                        // 0x102a770f
			return 0;
		}
		const FVector AttackerCm = Attacker->Origin;              // 0x102a7604 / 0x102a75fb / 0x102a7620 the handle's entity
		const FVector AttackerEyeCm = Attacker->EyePosition();    // 0x102a7631 slot 193
		const float RadiusUnits = CoverRadius();                  // 0x102a7641 slot 550
		// `0x102edc80(nav, origin, eye, 32.0, radius, &out, this)` -- `CAI_Navigator::FindCoverPos`,
		// family StartTask19's one port body (the threat handed is THIS NPC, `0x102a763d PUSH ESI`).
		FVector CoverCm = FVector::ZeroVector;
		const bool bFound = StartTaskFindCoverPos(AttackerCm, AttackerEyeCm, UnknownCoverMinDist,
			RadiusUnits, CoverCm);                                // 0x102a7666 / 0x102a765f GetNavigator
		if (!bFound)                                              // 0x102a766d
		{
			StartTask19Fail(0x3bdb, 8);                           // 0x102a76e2
			return 0;
		}
		FTaskTailNavGoal Goal;                                    // 0x102a768c 0x102a9d20(&goal, &out, 0x13, -2.0, 0, ...)
		Goal.Type = GoalTypeLocation;
		Goal.DestUnits = StartTask19_2Units(CoverCm);
		Goal.Activity = ActRun;
		Goal.Tolerance = GoalToleranceHull;
		(void)TaskTailNavSetGoal(Goal, 0);                        // 0x102a76a4, answer unread / 0x102a769d GetNavigator
		BaseScheduleHost.MoveWaitFinished = Now + Operand;        // 0x102a76a9..0x102a76b5
		return 0;
	}

	// --- index 0xad: TASK_ALERT_LOOK_AT_UNKNOWN_ATTACKER -----------------------------------------
	case TASK_ALERT_LOOK_AT_UNKNOWN_ATTACKER:
	{
		const FElysiumEntity* Attacker = World != nullptr
			? World->Resolve(BaseMemory.LastDamageAttacker) : nullptr;   // 0x102a772a
		if (Attacker == nullptr)                                  // 0x102a7731
		{
			StartTask19Fail(0x3bee, 0x21);                        // 0x102a777b
			return 0;
		}
		TaskTailLookAt(StartTask19_2Units(Attacker->Origin), Operand, Now);   // 0x102a773a..0x102a774f / 0x102a7735 the handle's entity / 0x102a773e GetAbsOrigin
		return 0;
	}

	// --- index 0xae: TASK_PLAY_DEATH_SEQUENCE ----------------------------------------------------
	case TASK_PLAY_DEATH_SEQUENCE:
	{
		int32 Activity = StartTask19_2Ftol(Operand);              // 0x102a77a0
		if (SelectWeightedSequenceForActivity(Activity) == INDEX_NONE)   // 0x102a77ac / 0x102a77b6
		{
			Activity = ActDieSimple;                              // 0x102a77b8
			if (SelectWeightedSequenceForActivity(Activity) == INDEX_NONE)   // 0x102a77c2 / 0x102a77c9
			{
				Activity = ActIdle;                               // 0x102a77cb
			}
		}
		SetIdealActivity(Activity);                               // 0x102a77d3
		if (Activity == ActIdle)                                  // 0x102a77d8 / 0x102a77db
		{
			StartTask19Complete();                              // 0x102a77dd -> 0x102a66d7
		}
		return 0;                                                 // 0x102a77ea, the sequence owns the task
	}

	default:
		break;
	}
	// Index `0xaf`, `0x102a77e2`: `CAI_BaseNPC::StartTask(this, task)` -- the 136 in-range ids Troika
	// (byte table `0x102a7ab8` decoded 2026-09-27)
	// does not override, and every id outside `5..0x149`.
	return FElysiumNpcBase::StartTaskSlot442(Task);               // 0x102a77e5
}
