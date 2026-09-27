// Story 0019/8 (29e under the strict verdict), family **StartTask19** -- `CAI_BaseNPC`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcBaseStartTask19.inl` (included inside `class FElysiumNpcBase`) or
// generated in `ElysiumNpcBaseSlots.inl` for a slot body.
//
// Owns (StartTask19's `rule` rows): 0x102827f0 CAI_BaseNPC::StartTask.
//
// Lane L03 (pass I): the body is `CAI_BaseNPC::StartTask` `0x102827f0`, 18330 bytes, read off the
// listing (`uv run elysium --verbose research corpus asm 0x102827f0`) arm by arm in the order of its
// arm table `0x10286f8c` (107 entries, reached through the byte table `0x10287138` indexed `iTask-1`).
// The walked account is `docs/vtmb/npc-ai/story8/StartTask19-Base.md`.
//
// Every `TaskFail` site first writes the source file (`AI_BaseNPC_Schedule.cpp`, `0x105cde88`) into
// `+0x1b44` and its line into `+0x1b48`; those words are ABSENT in this runtime (shape map), so the
// landed convention (`ElysiumNpcFrenzyShadow.cpp` slot 442) records the line on the schedule trace
// and then calls slot 448. `TaskComplete` (`0x10273e80`) is always called with `bOverride = 0`, so a
// failure raised earlier on the same pass blocks it.

#include "Substrate/ElysiumNpcBase.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumScheduleCorpus.h"
#include "Substrate/ElysiumScriptedSequence.h"

namespace ElysiumStartTask19Base
{
	// The retail source file every trace in this body names (`0x105cde88`, pre-loaded into EDI at
	// `0x10282816`).
	const TCHAR* const File = TEXT("AI_BaseNPC_Schedule.cpp");

	// --- Task ids: the registrar `FUN_10316ff0`'s numbers (the global task id `Step->TaskId`) -----
	constexpr int32 TASK_RESET_ACTIVITY = 0x01;
	constexpr int32 TASK_WAIT = 0x02;
	constexpr int32 TASK_ANNOUNCE_ATTACK = 0x03;
	constexpr int32 TASK_WAIT_FACE_ENEMY = 0x04;
	constexpr int32 TASK_WAIT_PVS = 0x05;
	constexpr int32 TASK_SUGGEST_STATE = 0x06;
	constexpr int32 TASK_TARGET_PLAYER = 0x07;
	constexpr int32 TASK_WALK_TO_TARGET = 0x08;
	constexpr int32 TASK_RUN_TO_TARGET = 0x09;
	constexpr int32 TASK_SCRIPT_CUSTOM_MOVE_TO_TARGET = 0x0a;
	constexpr int32 TASK_MOVE_TO_TARGET_RANGE = 0x0b;
	constexpr int32 TASK_MOVE_AWAY_PATH = 0x0c;
	constexpr int32 TASK_SET_GOAL = 0x0d;
	constexpr int32 TASK_GET_PATH_TO_GOAL = 0x0e;
	constexpr int32 TASK_GET_PATH_TO_ENEMY = 0x0f;
	constexpr int32 TASK_GET_PATH_TO_ENEMY_LKP = 0x10;
	constexpr int32 TASK_GET_PATH_TO_ENEMY_LKP_LOS = 0x11;
	constexpr int32 TASK_GET_PATH_TO_ENEMY_CORPSE = 0x12;
	constexpr int32 TASK_GET_PATH_TO_PLAYER = 0x13;
	constexpr int32 TASK_GET_PATH_TO_ENEMY_LOS = 0x14;
	constexpr int32 TASK_GET_PATH_TO_TARGET = 0x15;
	constexpr int32 TASK_GET_PATH_TO_HINTNODE = 0x16;
	constexpr int32 TASK_STORE_LASTPOSITION = 0x17;
	constexpr int32 TASK_CLEAR_LASTPOSITION = 0x18;
	constexpr int32 TASK_STORE_POSITION_IN_SAVEPOSITION = 0x19;
	constexpr int32 TASK_STORE_BESTSOUND_IN_SAVEPOSITION = 0x1a;
	constexpr int32 TASK_STORE_ENEMY_POSITION_IN_SAVEPOSITION = 0x1b;
	constexpr int32 TASK_GET_PATH_TO_LASTPOSITION = 0x1c;
	constexpr int32 TASK_GET_PATH_TO_SAVEPOSITION = 0x1d;
	constexpr int32 TASK_GET_PATH_TO_SAVEPOSITION_LOS = 0x1e;
	constexpr int32 TASK_GET_PATH_TO_RANDOM_NODE = 0x1f;
	constexpr int32 TASK_GET_PATH_TO_BESTSOUND = 0x20;
	constexpr int32 TASK_GET_PATH_TO_BESTSCENT = 0x21;
	constexpr int32 TASK_RUN_PATH = 0x22;
	constexpr int32 TASK_WALK_PATH = 0x23;
	constexpr int32 TASK_WALK_PATH_TIMED = 0x24;
	constexpr int32 TASK_WALK_PATH_WITHIN_DIST = 0x25;
	constexpr int32 TASK_RUN_PATH_WITHIN_DIST = 0x26;
	constexpr int32 TASK_RUN_PATH_TIMED = 0x27;
	constexpr int32 TASK_STRAFE_PATH = 0x28;
	constexpr int32 TASK_CLEAR_MOVE_WAIT = 0x29;
	constexpr int32 TASK_SMALL_FLINCH = 0x2a;
	constexpr int32 TASK_FACE_IDEAL = 0x2b;
	constexpr int32 TASK_FACE_PATH = 0x2c;
	constexpr int32 TASK_FACE_PLAYER = 0x2d;
	constexpr int32 TASK_FACE_ENEMY = 0x2e;
	constexpr int32 TASK_FACE_HINTNODE = 0x2f;
	constexpr int32 TASK_PLAY_HINT_ACTIVITY = 0x30;
	constexpr int32 TASK_FACE_TARGET = 0x31;
	constexpr int32 TASK_FACE_LASTPOSITION = 0x32;
	constexpr int32 TASK_SET_IDEAL_YAW_TO_CURRENT = 0x33;
	constexpr int32 TASK_RANGE_ATTACK1 = 0x34;
	constexpr int32 TASK_RANGE_ATTACK2 = 0x35;
	constexpr int32 TASK_MELEE_ATTACK1 = 0x36;
	constexpr int32 TASK_MELEE_ATTACK2 = 0x37;
	constexpr int32 TASK_RELOAD = 0x38;
	constexpr int32 TASK_RANGE_ATTACK1_NOTURN = 0x39;
	constexpr int32 TASK_RANGE_ATTACK2_NOTURN = 0x3a;
	constexpr int32 TASK_MELEE_ATTACK1_NOTURN = 0x3b;
	constexpr int32 TASK_MELEE_ATTACK2_NOTURN = 0x3c;
	constexpr int32 TASK_RELOAD_NOTURN = 0x3d;
	constexpr int32 TASK_SPECIAL_ATTACK1 = 0x3e;
	constexpr int32 TASK_SPECIAL_ATTACK2 = 0x3f;
	constexpr int32 TASK_FIND_HINTNODE = 0x40;
	constexpr int32 TASK_FIND_LOCK_HINTNODE = 0x41;
	constexpr int32 TASK_CLEAR_HINTNODE = 0x42;
	constexpr int32 TASK_LOCK_HINTNODE = 0x43;
	constexpr int32 TASK_SOUND_ANGRY = 0x44;
	constexpr int32 TASK_SOUND_IDLE = 0x46;
	constexpr int32 TASK_SOUND_WAKE = 0x47;
	constexpr int32 TASK_SOUND_PAIN = 0x48;
	constexpr int32 TASK_SOUND_DIE = 0x49;
	constexpr int32 TASK_SPEAK_SENTENCE = 0x4a;
	constexpr int32 TASK_SET_ACTIVITY = 0x4b;
	constexpr int32 TASK_SET_SCHEDULE = 0x4c;
	constexpr int32 TASK_SET_FAIL_SCHEDULE = 0x4d;
	constexpr int32 TASK_SET_TOLERANCE_DISTANCE = 0x4e;
	constexpr int32 TASK_SET_ROUTE_SEARCH_TIME = 0x50;
	constexpr int32 TASK_CLEAR_FAIL_SCHEDULE = 0x51;
	constexpr int32 TASK_PLAY_SEQUENCE = 0x52;
	constexpr int32 TASK_PLAY_PRIVATE_SEQUENCE = 0x53;
	constexpr int32 TASK_PLAY_PRIVATE_SEQUENCE_FACE_ENEMY = 0x54;
	constexpr int32 TASK_PLAY_SEQUENCE_FACE_ENEMY = 0x55;
	constexpr int32 TASK_PLAY_SEQUENCE_FACE_TARGET = 0x56;
	constexpr int32 TASK_FIND_COVER_FROM_BEST_SOUND = 0x57;
	constexpr int32 TASK_FIND_COVER_FROM_ENEMY = 0x58;
	constexpr int32 TASK_FIND_LATERAL_COVER_FROM_ENEMY = 0x59;
	constexpr int32 TASK_FIND_BACKAWAY_FROM_SAVEPOSITION = 0x5a;
	constexpr int32 TASK_FIND_NODE_COVER_FROM_ENEMY = 0x5b;
	constexpr int32 TASK_FIND_NEAR_NODE_COVER_FROM_ENEMY = 0x5c;
	constexpr int32 TASK_FIND_FAR_NODE_COVER_FROM_ENEMY = 0x5d;
	constexpr int32 TASK_FIND_COVER_FROM_ORIGIN = 0x5e;
	constexpr int32 TASK_DIE = 0x5f;
	constexpr int32 TASK_WAIT_FOR_SCRIPT = 0x60;
	constexpr int32 TASK_PUSH_SCRIPT_ARRIVAL_ACTIVITY = 0x61;
	constexpr int32 TASK_PLAY_SCRIPT = 0x62;
	constexpr int32 TASK_PLAY_SCRIPT_POST_IDLE = 0x63;
	constexpr int32 TASK_ENABLE_SCRIPT = 0x64;
	constexpr int32 TASK_PLANT_ON_SCRIPT = 0x65;
	constexpr int32 TASK_FACE_SCRIPT = 0x66;
	constexpr int32 TASK_WAIT_RANDOM = 0x67;
	constexpr int32 TASK_WAIT_INDEFINITE = 0x68;
	constexpr int32 TASK_STOP_MOVING = 0x69;
	constexpr int32 TASK_TURN_LEFT = 0x6a;
	constexpr int32 TASK_TURN_RIGHT = 0x6b;
	constexpr int32 TASK_REMEMBER = 0x6c;
	constexpr int32 TASK_FORGET = 0x6d;
	constexpr int32 TASK_WAIT_FOR_MOVEMENT = 0x6e;
	constexpr int32 TASK_WAIT_FOR_MOVEMENT_STEP = 0x6f;
	constexpr int32 TASK_WEAPON_FIND = 0x70;
	constexpr int32 TASK_WEAPON_PICKUP = 0x71;
	constexpr int32 TASK_WEAPON_RUN_PATH = 0x72;
	constexpr int32 TASK_USE_SMALL_HULL = 0x73;
	constexpr int32 TASK_FALL_TO_GROUND = 0x74;
	constexpr int32 TASK_GET_DROPSHIP_DEPLOY_PATH = 0x75;
	constexpr int32 TASK_WANDER = 0x76;
	constexpr int32 TASK_FREEZE = 0x77;
	constexpr int32 TASK_SET_MELEE_TOLERANCE_DISTANCE = 0x9f;
	constexpr int32 TASK_CHOOSE_BEST_MELEE_WEAPON = 0xac;
	constexpr int32 TASK_CHOOSE_BEST_RANGED_WEAPON = 0xad;
	constexpr int32 TASK_DIE_IMMEDIATE = 0xdf;
	constexpr int32 TASK_GET_PATH_TO_PATHCORNER = 0x120;

	// --- Retail activity numbers the arms name ---------------------------------------------------
	constexpr int32 ACT_IDLE = 0x01;
	constexpr int32 ACT_WALK = 0x09;
	constexpr int32 ACT_RUN = 0x13;
	constexpr int32 ACT_SCRIPT_CUSTOM_MOVE = 0x18;
	constexpr int32 ACT_RANGE_ATTACK1 = 0x19;
	constexpr int32 ACT_RANGE_ATTACK2 = 0x1b;
	constexpr int32 ACT_FLY = 0x22;
	constexpr int32 ACT_STRAFE_LEFT = 0x37;
	constexpr int32 ACT_STRAFE_RIGHT = 0x38;
	constexpr int32 ACT_SMALL_FLINCH = 0x49;
	constexpr int32 ACT_MELEE_ATTACK1 = 0x4b;
	constexpr int32 ACT_MELEE_ATTACK2 = 0x4e;
	constexpr int32 ACT_RELOAD = 0x54;
	constexpr int32 ACT_PICKUP_GROUND = 0x5c;
	constexpr int32 ACT_SPECIAL_ATTACK1 = 0x5e;
	constexpr int32 ACT_SPECIAL_ATTACK2 = 0x5f;

	// --- Goal types, goal-record sentinels and failure codes -------------------------------------
	constexpr int32 GOALTYPE_TARGETENT = 1;
	constexpr int32 GOALTYPE_ENEMY = 2;
	constexpr int32 GOALTYPE_PATHCORNER = 3;
	constexpr int32 GOALTYPE_LOCATION = 4;
	constexpr int32 GOALTYPE_LOCATION_NEAREST_NODE = 6;
	// `0x1049a160` (-1.0, read 2026-09-27 off the image) — keep the path's tolerance.
	constexpr float NavToleranceKeep = -1.0f;
	// `0x1049a164` (-2.0) — the hull's tolerance.
	constexpr float NavToleranceHull = -2.0f;

	constexpr int32 FAIL_NO_TARGET = 0x01;
	constexpr int32 FAIL_NO_WEAPON = 0x03;
	constexpr int32 FAIL_NO_HINT_NODE = 0x04;
	constexpr int32 FAIL_SCHEDULE_NOT_FOUND = 0x05;
	constexpr int32 FAIL_NO_ENEMY = 0x06;
	constexpr int32 FAIL_NO_BACKAWAY_NODE = 0x07;
	constexpr int32 FAIL_NO_COVER = 0x08;
	constexpr int32 FAIL_NO_SHOOT = 0x0b;
	constexpr int32 FAIL_NO_ROUTE = 0x0c;
	constexpr int32 FAIL_ALREADY_LOCKED = 0x11;
	constexpr int32 FAIL_NO_SOUND = 0x12;
	constexpr int32 FAIL_NO_SCENT = 0x13;
	constexpr int32 FAIL_NO_PLAYER = 0x17;
	constexpr int32 FAIL_NO_REACHABLE_NODE = 0x18;
	constexpr int32 FAIL_NO_WEAPON_TO_CHOOSE = 0x1f;

	// The three literal-string failure codes: retail hands `TaskFail` the string's own address,
	// which `0x10316fa0` passes through as text because it is above the code table (`>= 0x2a`).
	constexpr int32 FAIL_TEXT_NO_SOUND = 0x105ce038;            // "No Sound!"
	constexpr int32 FAIL_TEXT_NO_SOUND_IN_LIST = 0x105ce024;    // "No sound in list"
	constexpr int32 FAIL_TEXT_GAH = 0x105cdfe4;                 // "gah"

	// --- The constants the arms read, each read off the image ------------------------------------
	constexpr float HintSearchRadiusUnits = 2000.0f;     // `0x44fa0000` pushed at `0x10282a28`
	constexpr float WeaponMaxRangeUnarmed = 2000.0f;     // `0x44fa0000` at `0x10284c89` / `0x1028452d`
	constexpr float BackawaySearchUnits = 30000.0f;      // `0x46ea6000` pushed at `0x10282ef3`
	constexpr double FacePathYawTolerance = 15.0;        // `0x1049a170`, a DOUBLE (`FCOMP qword` 0x10283d76)
	constexpr double MoveAwayCoverWait = 2.0;            // `0x10452dc4`
	constexpr float DropshipForwardUnits = 256.0f;       // `0x1044ddb0`
	constexpr float DropshipDropUnits = 500.0f;          // `0x10457f5c`
	constexpr int32 DropshipTraceMask = 0x46004003;      // pushed at `0x10284f2f`
	constexpr float WeaponFindExtentUnits = 1000.0f;     // `0x447a0000` x3 at `0x10286d78`
	constexpr int32 WanderSplit = 10000;                 // the `0x68db8bad` / `SAR 12` divide at `0x10286ecc`
	constexpr float LateralCoverStepUnits = 48.0f;       // `0x10447ee8`
	constexpr int32 LateralCoverSteps = 5;               // `0x102784a0`'s `4 < i` exit
	constexpr int32 MemoryInCover = 0x2;                 // `m_afMemory &= 0xfffffffd` (`0x1028641b`)
	constexpr uint32 MemoryPathFailed = 0x20;            // `0x102f1dc0`'s deferred-route bit
	constexpr int32 ScriptStateCustomMove = 6;           // `m_scriptState == 6` (`0x102869bd`, `0x10286bdf`)
	constexpr int32 ScriptStatePlaying = 0;              // `0x10286af3`
	constexpr int32 ScriptStatePostIdle = 2;             // `0x10286b13`
	constexpr int32 MoveTypeFly = 5;                     // `GetMoveType() == 5` (`0x10286445`)
	constexpr int32 MoveTypeFlyGravity = 6;              // `GetMoveType() == 6` (`0x10286454`)
	constexpr int32 LifeStateDying = 1;                  // `0x1028680c`
}

// =================================================================================================
// Slot 442: `CAI_BaseNPC::StartTask` `0x102827f0`
// =================================================================================================

int32 FElysiumNpcBase::StartTaskSlot442(void* Task)
{
	using namespace ElysiumStartTask19Base;

	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	if (Step == nullptr)
	{
		// Retail reads `pTask->iTask` unconditionally (`0x10282803`). Named crash guard.
		++StartTaskNav.CrashGuards;
		return 0;
	}
	const int32 TaskId = Step->TaskId;                                   // 0x10282803
	const float Data = Step->Data;                                       // pTask->flTaskData (+0x4)
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;     // gpGlobals->curtime (+0xc)
	const float U = ElysiumMove::U;

	// `+0x1b44 = file, +0x1b48 = line`, then slot 448 `TaskFail(reason)` (`+0x700`).
	auto Fail = [this](int32 Line, int32 Reason)
	{
		RecordScheduleEvent(FString::Printf(TEXT("StartTask fail trace %s:%d"),
			ElysiumStartTask19Base::File, Line));
		TaskFail(Reason);
	};
	// The three literal-string sites: the string's address is the "reason" retail stores.
	auto FailText = [this](int32 Line, const TCHAR* Text, int32 TextAddress)
	{
		RecordScheduleEvent(FString::Printf(TEXT("StartTask fail trace %s:%d \"%s\""),
			ElysiumStartTask19Base::File, Line, Text));
		TaskFail(TextAddress);
	};
	auto Enemy = [this]() -> FElysiumEntity*
	{
		// Slot 167 `GetEnemy()` (`+0x29c`), the const overload retail's vtable holds.
		return static_cast<const FElysiumNpcBase*>(this)->GetEnemy();
	};
	auto ResolveTargetEnt = [this]() -> FElysiumEntity*
	{
		// `m_hTargetEnt` (`+0x5ce4`) through the handle table (index `& 0x1fff`, serial `>> 13`).
		return World != nullptr ? World->Resolve(TargetEnt) : nullptr;
	};
	auto MakeGoal = [](int32 GoalType, float GoalToleranceUnits)
	{
		FStartTaskNavGoal Goal;
		Goal.Type = GoalType;
		Goal.ToleranceUnits = GoalToleranceUnits;
		return Goal;
	};
	// The hint follow-up the cover arms share (`0x10283168..0x102831ab`): when `m_pHintNode` is
	// held, `SetArrivalActivity(GetCoverActivity(hint))` (slot 569) and
	// `SetArrivalDirection(0x102d11f0(hint))`.
	auto HintArrival = [this]()
	{
		if (BaseScheduleHost.HintNode == INDEX_NONE)
		{
			return;
		}
		StartTaskSetArrivalActivity(GetCoverActivity(&BaseScheduleHost.HintNode));
		FVector Facing = FVector::ZeroVector;
		StartTaskHintFacing(BaseScheduleHost.HintNode, Facing);
		StartTaskSetArrivalDirection(Facing);
	};

	// `0x10282806`: `(iTask - 1) > 0x11f` unsigned -> the default arm; otherwise the byte table.
	switch (TaskId)
	{
	// ---------------------------------------------------------------------------------------------
	case TASK_RESET_ACTIVITY:                                            // arm 0x00, 0x10282828
		ActivityNumber = 0;                                              // 0x1028282c  m_Activity (+0xfec) = 0
		TaskComplete(false);                                             // 0x10282836
		return 0;

	case TASK_WAIT:                                                      // arm 0x01, 0x10286505
	case TASK_WAIT_FACE_ENEMY:
		BaseScheduleHost.WaitFinished = Now + Data;                      // 0x10286511  +0x5db4, no floor, no complete
		return 0;

	case TASK_ANNOUNCE_ATTACK:                                           // arm 0x02, 0x10286cd9
		TaskComplete(false);                                             // 0x10286cdd
		return 0;

	case TASK_WAIT_PVS:                                                  // arm 0x03, 0x10286f7d
	case TASK_WAIT_INDEFINITE:
	case TASK_FALL_TO_GROUND:
		// The arm IS the function epilogue: no start work at all.
		return 0;

	case TASK_SUGGEST_STATE:                                             // arm 0x04, 0x10286c0d
		// `+0x1b3c/+0x1b40 = file, 0xa9b` — the ideal-state call record, not a failure.
		RecordScheduleEvent(FString::Printf(TEXT("SetIdealState trace %s:%d"), File, 0xa9b)); // 0x10286c17
		WriteIdealStateRetail(static_cast<int32>(Data));                 // 0x10286c2d  m_IdealNPCState (+0x5cc4)
		TaskComplete(false);                                             // 0x10286c33
		return 0;

	case TASK_TARGET_PLAYER:                                             // arm 0x05, 0x10283ed5
	{
		// `FindEntityByName(gEntList, NULL, "!player", this, 0)` (`0x100f7770`).
		FElysiumPlayer* Player = World != nullptr ? World->FindPlayer() : nullptr; // 0x10283ee4
		if (Player == nullptr)
		{
			Fail(0x67a, FAIL_NO_PLAYER);                                 // 0x10283f23
			return 0;
		}
		SetTarget(Player->Handle);                                       // 0x10283ef0  SetTarget 0x10279cc0
		TaskComplete(false);                                             // 0x10283ef9
		return 0;
	}

	case TASK_WALK_TO_TARGET:                                            // arm 0x06, 0x10283f36
	case TASK_RUN_TO_TARGET:
	case TASK_SCRIPT_CUSTOM_MOVE_TO_TARGET:
	{
		// Every exit of this arm goes through the shared tail `0x102841f2`.
		auto ArrivalTail = [this]()
		{
			ScriptArrivalActivity = INDEX_NONE;                          // 0x102841f6  +0x5d7c = -1
			ScriptArrivalSequence.Empty();                               // 0x102841fc  +0x5d80 = 0
			TaskComplete(false);                                         // 0x10284206
		};
		FElysiumEntity* Tgt = ResolveTargetEnt();
		if (Tgt == nullptr)
		{
			Fail(0x686, FAIL_NO_TARGET);                                 // 0x10283f85
			ArrivalTail();
			return 0;
		}
		// `|target->GetAbsOrigin() - GetOrigin()| < 1.0` (`0x104454c0`), Source units.
		if ((Tgt->Origin - Origin).Size() < ElysiumNpcTunables::One * U) // 0x10284005
		{
			TaskComplete(false);                                         // 0x10284019
			ArrivalTail();
			return 0;
		}
		int32 Activity = ACT_WALK;                                       // 0x1028402b  task 8
		if (TaskId == TASK_RUN_TO_TARGET)
		{
			Activity = ACT_RUN;                                          // 0x10284037
		}
		else if (TaskId == TASK_SCRIPT_CUSTOM_MOVE_TO_TARGET)
		{
			Activity = GetScriptCustomMoveActivity();                    // 0x10284040  0x10289fe0
		}
		// `SelectWeightedSequence(act, -1)` (`0x1008dc40`), skipped only for the custom-move answer.
		if (Activity != ACT_SCRIPT_CUSTOM_MOVE && SelectWeightedSequenceForActivity(Activity) == INDEX_NONE) // 0x10284050
		{
			TaskComplete(false);                                         // 0x1028405d
			ArrivalTail();
			return 0;
		}
		if (ResolveTargetEnt() == nullptr)                                         // 0x10284067  re-tested
		{
			Fail(0x6a7, FAIL_NO_TARGET);                                 // 0x102840b0
			ArrivalTail();
			return 0;
		}
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_TARGETENT, NavToleranceKeep); // 0x10284109
		Goal.MovementActivity = Activity;                                // 0x10284115  [5]
		// Only in `NPC_STATE_SCRIPT` (slot 464 == 4): a pushed arrival activity goes to [6]; failing
		// that, a pushed sequence NAME goes through `LookupSequence` into [7].
		if (GetState() == EElysiumNpcState::Scripted)                    // 0x10284135
		{
			if (ScriptArrivalActivity != INDEX_NONE)                     // 0x10284148
			{
				Goal.ArrivalActivity = ScriptArrivalActivity;            // 0x10284156
			}
			else if (!ScriptArrivalSequence.IsEmpty())                   // 0x10284150
			{
				Goal.ArrivalSequence = LookupSequenceByName(*ScriptArrivalSequence); // 0x1028416e
			}
		}
		if (!StartTaskSetGoal(Goal, 4))                                  // 0x10284184  SetGoal(.., 4)
		{
			Fail(0x6be, FAIL_NO_ROUTE);                                  // 0x102841a7
			ArrivalTail();
			return 0;
		}
		// `SetArrivalDirection(target->GetAbsAngles())` (`0x102ee550`). Retail re-resolves the handle
		// and dereferences it without a test (`0x102841da`); a target lost inside `SetGoal` is the
		// named crash guard.
		if (FElysiumEntity* Again = ResolveTargetEnt())
		{
			StartTaskSetArrivalDirectionAngles(Again->Angles); // 0x102841ed
		}
		else
		{
			++StartTaskNav.CrashGuards;
		}
		ArrivalTail();
		return 0;
	}

	case TASK_MOVE_TO_TARGET_RANGE:                                      // arm 0x07, 0x10283dde
	{
		FElysiumEntity* Tgt = ResolveTargetEnt();
		if (Tgt == nullptr)
		{
			Fail(0x669, FAIL_NO_TARGET);                                 // 0x10283e2b
			return 0;
		}
		if ((Tgt->Origin - Origin).Size() < ElysiumNpcTunables::One * U) // 0x10283eab
		{
			TaskComplete(false);                                         // 0x10283ec3
		}
		// Otherwise the task is left running with no goal set (`JP 0x10286f7d`).
		return 0;
	}

	case TASK_MOVE_AWAY_PATH:                                            // arm 0x08, 0x1028611a
	{
		// `GetAngles()` with its yaw replaced by `m_pMotor+0x34 + 180.0`, then `AngleVectors`.
		FVector Away = Angles;                                           // 0x1028611e
		Away.Y = MotorIdealYaw + ElysiumNpcTunables::OneEighty;          // 0x1028614b
		FVector Forward = FVector::ZeroVector;
		StartTaskAngleVectors(Away, &Forward, nullptr);                  // 0x1028615f
		const float DistanceUnits = ResolveTaskDistance(Data);           // 0x1028616f  slot 418
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceKeep);
		Goal.DestCm = Origin + Forward * (DistanceUnits * U);            // 0x102861a5
		Goal.bDestSet = true;
		Goal.MovementActivity = ACT_WALK;                                // 0x102861ce
		if (StartTaskSetGoal(Goal, 0))                                   // 0x1028627e
		{
			TaskComplete(false);                                         // 0x1028628a
			return 0;
		}
		// `FindCoverPos(GetOrigin(), EyePosition(), 0.0, CoverRadius(), &out)`.
		FVector Cover = FVector::ZeroVector;
		if (!StartTaskFindCoverPos(Origin, EyePosition(), 0.f, CoverRadius(), Cover)) // 0x102862dd
		{
			Fail(0x94b, FAIL_NO_COVER);                                  // 0x102863de
			return 0;
		}
		FStartTaskNavGoal CoverGoal = MakeGoal(GOALTYPE_LOCATION, NavToleranceKeep);
		CoverGoal.DestCm = Cover;
		CoverGoal.bDestSet = true;
		CoverGoal.MovementActivity = ACT_RUN;                            // 0x10286365
		StartTaskSetGoal(CoverGoal, 0);                                  // 0x1028639e  result DISCARDED
		BaseScheduleHost.MoveWaitFinished = Now + MoveAwayCoverWait;     // 0x102863b2  +0x5cf0
		return 0;
	}

	case TASK_SET_GOAL:                                                  // arm 0x09, 0x102847a3
	{
		// `switch ((int)flTaskData)` through the five-entry table `0x10287258`; anything above 4
		// jumps straight to the completion (`0x102847b2 JA 0x10284ad2`).
		const int32 Mode = static_cast<int32>(Data);                     // 0x102847a6
		switch (Mode)
		{
		case 0:                                                          // 0x102847bf  the enemy
		{
			FElysiumEntity* E = Enemy();
			if (E == nullptr)
			{
				Fail(0x75f, FAIL_NO_ENEMY);                              // 0x102847e7
				return 0;
			}
			BaseScheduleHost.StoredPathGoal = E->Origin;                 // 0x1028480e  +0x5df8
			BaseScheduleHost.StoredPathType = GOALTYPE_ENEMY;            // 0x1028482a  +0x5e04
			BaseScheduleHost.StoredPathFlags = 0;                        // 0x10284834  +0x5e08
			FElysiumEntity* Again = Enemy();                             // 0x1028483e
			BaseScheduleHost.StoredPathTarget = Again != nullptr         // 0x10284abf  +0x5df4
				? Again->Handle : FElysiumEntityHandle::Invalid();
			break;
		}
		case 1:                                                          // 0x102848bf  the target
		{
			FElysiumEntity* Tgt = ResolveTargetEnt();
			if (Tgt == nullptr)
			{
				Fail(0x77f, FAIL_NO_TARGET);                             // 0x1028490b
				return 0;
			}
			BaseScheduleHost.StoredPathGoal = Tgt->Origin;               // 0x1028494f
			BaseScheduleHost.StoredPathType = GOALTYPE_TARGETENT;        // 0x10284967
			BaseScheduleHost.StoredPathFlags = 0;                        // 0x10284971
			BaseScheduleHost.StoredPathTarget = TargetEnt;               // 0x102849a4
			break;
		}
		case 2:                                                          // 0x10284849  the enemy LKP
		{
			FElysiumEntity* E = Enemy();
			if (E == nullptr)
			{
				Fail(0x76f, FAIL_NO_ENEMY);                              // 0x10284871
				return 0;
			}
			BaseScheduleHost.StoredPathGoal = StartTaskLastKnownPosition(Enemy()); // 0x102848a1
			BaseScheduleHost.StoredPathType = GOALTYPE_LOCATION;         // 0x10284ab3
			BaseScheduleHost.StoredPathFlags = 0;                        // 0x10284ab9
			BaseScheduleHost.StoredPathTarget = FElysiumEntityHandle::Invalid(); // 0x10284abf
			break;
		}
		case 3:                                                          // 0x102849bc  the TARGET, in the ENEMY memory
		{
			FElysiumEntity* Tgt = ResolveTargetEnt();
			if (Tgt == nullptr)
			{
				Fail(0x78f, FAIL_NO_TARGET);                             // 0x10284a0b
				return 0;
			}
			BaseScheduleHost.StoredPathGoal = StartTaskLastKnownPosition(Tgt); // 0x10284a5a
			BaseScheduleHost.StoredPathType = GOALTYPE_LOCATION;         // 0x10284a79
			BaseScheduleHost.StoredPathFlags = 0;                        // 0x10284a7f
			BaseScheduleHost.StoredPathTarget = FElysiumEntityHandle::Invalid(); // 0x10284a85
			break;
		}
		case 4:                                                          // 0x10284a8d  m_vSavePosition
			BaseScheduleHost.StoredPathGoal = SavePosition;              // 0x10284a93
			BaseScheduleHost.StoredPathType = GOALTYPE_LOCATION;         // 0x10284ab3
			BaseScheduleHost.StoredPathFlags = 0;                        // 0x10284ab9
			BaseScheduleHost.StoredPathTarget = FElysiumEntityHandle::Invalid(); // 0x10284abf
			break;
		default:
			TaskComplete(false);                                         // 0x10284ad6
			return 0;
		}
		StartTaskSetMovementActivity(ACT_RUN);                           // 0x10284acd
		TaskComplete(false);                                             // 0x10284ad6
		return 0;
	}

	case TASK_GET_PATH_TO_GOAL:                                          // arm 0x0a, 0x10284ae8
	{
		// The goal record is built from the STORED path words before the operand is read.
		FStartTaskNavGoal Goal = MakeGoal(BaseScheduleHost.StoredPathType, NavToleranceHull); // 0x10284aff
		Goal.Target = BaseScheduleHost.StoredPathTarget;                 // 0x10284b4c
		FElysiumEntity* Stored = World != nullptr ? World->Resolve(BaseScheduleHost.StoredPathTarget) : nullptr;
		// The route tail `0x10284e2c`: a true `SetGoal` completes, a false one fails with `0xc`.
		auto RouteTail = [this, &Fail](bool bRoute)
		{
			if (bRoute)
			{
				TaskComplete(false);                                     // 0x10284e38
				return;
			}
			Fail(0x816, ElysiumStartTask19Base::FAIL_NO_ROUTE);          // 0x10284c74
		};
		switch (static_cast<int32>(Data))                                // 0x10284b64
		{
		case 0:                                                          // 0x10284dfd  the stored point
			Goal.DestCm = BaseScheduleHost.StoredPathGoal;
			Goal.bDestSet = true;
			RouteTail(StartTaskSetGoal(Goal, 0));                        // 0x10284e27
			return 0;
		case 1:                                                          // 0x10284c87  a shoot position
		{
			float MinUnits = 0.f;
			float MaxUnits = 0.f;
			StartTaskWeaponRange(MinUnits, MaxUnits);                    // 0x10284c89..0x10284d53
			const FVector Aim = Stored != nullptr ? Stored->EyePosition() : BaseScheduleHost.StoredPathGoal; // 0x10284d53
			FVector Los = FVector::ZeroVector;
			if (!StartTaskFindLosPos(BaseScheduleHost.StoredPathGoal, Aim, MinUnits, MaxUnits, Los)) // 0x10284db9
			{
				Fail(0x7de, FAIL_NO_SHOOT);                              // 0x10284dea
				return 0;
			}
			Goal.DestCm = Los;                                           // 0x10284e0f
			Goal.bDestSet = true;
			RouteTail(StartTaskSetGoal(Goal, 0));
			return 0;
		}
		case 2:                                                          // 0x10284b7f  cover from the stored entity
		{
			const FElysiumEntity* Threat = Stored != nullptr ? Stored : this; // 0x10284b83
			if (StartTaskFindLateralCover(Threat->EyePosition(), Threat)) // 0x10284b9b
			{
				BaseScheduleHost.MoveWaitFinished = Now + Data;          // 0x10284bb4
				TaskComplete(false);                                     // 0x10284bba
				return 0;
			}
			FVector Cover = FVector::ZeroVector;
			if (StartTaskFindCoverPos(Threat->Origin, Threat->EyePosition(), 0.f, CoverRadius(), Cover)) // 0x10284c08
			{
				// RETAIL DEFECT, reproduced: the cover point is written to the stack at `+0x7c` and
				// never copied into the goal record, so `SetGoal` gets the default dest triple.
				const bool bRoute = StartTaskSetGoal(Goal, 0);           // 0x10284c1e
				BaseScheduleHost.MoveWaitFinished = Now + Data;          // 0x10284c2f
				RouteTail(bRoute);                                       // 0x10284c35 -> 0x10284e2c
				return 0;
			}
			// The miss fails with 8 AND then falls into the 0xc failure: the second overwrites the
			// first reason at `+0x5c50` (retail's, reproduced).
			Fail(0x807, FAIL_NO_COVER);                                  // 0x10284c54
			Fail(0x816, FAIL_NO_ROUTE);                                  // 0x10284c74
			return 0;
		}
		default:
			Fail(0x816, FAIL_NO_ROUTE);                                  // 0x10284c5a
			return 0;
		}
	}

	case TASK_GET_PATH_TO_ENEMY:                                         // arm 0x0b, 0x1028509b
	{
		// `IsUnreachable(GetEnemy())` FIRST, with a possibly-null enemy (slot 530).
		if (IsUnreachable(Enemy()))                                      // 0x102850a8
		{
			Fail(0x83a, FAIL_NO_ROUTE);                                  // 0x102850cc
			return 0;
		}
		if (Enemy() == nullptr)                                          // 0x102850e1
		{
			Fail(0x842, FAIL_NO_ENEMY);                                  // 0x10285105
			return 0;
		}
		const FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_ENEMY, NavToleranceKeep); // 0x102851a6
		if (StartTaskSetGoal(Goal, 0))                                   // 0x102851cd
		{
			TaskComplete(false);                                         // 0x102851d9
			return 0;
		}
		StartTaskDevMessage(TEXT("GetPathToEnemy failed!!\n"));          // 0x102851f2  DevWarning(2, ...)
		RememberUnreachable(Enemy());                                    // 0x10285208  0x10274080
		Fail(0x84f, FAIL_NO_ROUTE);                                      // 0x10285227
		return 0;
	}

	case TASK_GET_PATH_TO_ENEMY_LKP:                                     // arm 0x0c, 0x10284349
	{
		FElysiumEntity* E = Enemy();                                     // 0x1028434d
		if (IsUnreachable(E))                                            // 0x1028435a
		{
			Fail(0x712, FAIL_NO_ROUTE);                                  // 0x1028437e
			return 0;
		}
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceKeep);
		Goal.DestCm = StartTaskLastKnownPosition(E);                     // 0x102843b3
		Goal.bDestSet = true;
		float Scalar = NavPathScalar20;                                  // 0x10284424  0x102f2fc0: path+0x20
		// Slot 563 `TranslateEnemyChasePosition(enemy, &goal.dest, &goal.tolerance, &scalar)`.
		TranslateEnemyChasePosition(E, Goal.DestCm, &Goal.ToleranceUnits, &Scalar); // 0x10284441
		if (StartTaskSetGoal(Goal, 2))                                   // 0x10284454  SetGoal(.., 2)
		{
			NavPathScalar20 = Scalar;                                    // 0x10284468  0x102f2fe0
			TaskComplete(false);                                         // 0x10284470
			return 0;
		}
		StartTaskDevMessage(TEXT("GetPathToEnemyLKP failed!!\n"));       // 0x10284489
		RememberUnreachable(Enemy());                                    // 0x1028449f
		Fail(0x724, FAIL_NO_ROUTE);                                      // 0x102844be
		return 0;
	}

	case TASK_GET_PATH_TO_ENEMY_LKP_LOS:                                 // arm 0x0d, 0x102844d1
	{
		FElysiumEntity* E = Enemy();
		if (E == nullptr)
		{
			Fail(0x72d, FAIL_NO_ENEMY);                                  // 0x102844f9
			return 0;
		}
		const FVector Lkp = StartTaskLastKnownPosition(Enemy());         // 0x10284526
		float MinUnits = 0.f;
		float MaxUnits = 0.f;
		StartTaskWeaponRange(MinUnits, MaxUnits);                        // 0x1028452d..0x102845f7
		// The aim point is the LKP plus the ENEMY's `m_vecViewOffset` (`+0x184`).
		FElysiumEntity* Again = Enemy();                                 // 0x102845fb
		const FVector Aim = Lkp + (Again != nullptr ? Again->EyePosition() - Again->Origin : FVector::ZeroVector); // 0x10284601
		FVector Los = FVector::ZeroVector;
		if (!StartTaskFindLosPos(Lkp, Aim, MinUnits, MaxUnits, Los))     // 0x10284678
		{
			Fail(0x74e, FAIL_NO_SHOOT);                                  // 0x10284790
			return 0;
		}
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceHull);
		Goal.DestCm = Los;
		Goal.bDestSet = true;
		Goal.MovementActivity = ACT_RUN;                                 // 0x102846ec
		StartTaskSetGoal(Goal, 2);                                       // 0x1028470c  result not tested
		StartTaskSetArrivalDirection(Lkp - Los);    // 0x10284764  0x102ee530
		return 0;
	}

	case TASK_GET_PATH_TO_ENEMY_CORPSE:                                  // arm 0x0e, 0x1028523a
	{
		FVector Forward = FVector::ZeroVector;
		StartTaskAngleVectors(Angles, &Forward, nullptr);                // 0x10285251
		const FVector Lkp = StartTaskLastKnownPosition(Enemy());         // 0x10285275
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceKeep);
		Goal.DestCm = Lkp - Forward * (ElysiumNpcTunables::SixtyFour * U); // 0x1028527a  0x10451acc
		Goal.bDestSet = true;
		StartTaskSetGoal(Goal, 2);                                       // 0x10285375  result discarded
		return 0;
	}

	case TASK_GET_PATH_TO_PLAYER:                                        // arm 0x0f, 0x10285387
	{
		FElysiumPlayer* Player = World != nullptr ? World->FindPlayer() : nullptr; // 0x10285396
		if (Player == nullptr)
		{
			// Retail dereferences the lookup without a test (`0x1028541e`). Named crash guard.
			++StartTaskNav.CrashGuards;
			return 0;
		}
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceKeep);
		Goal.DestCm = Player->WorldSpaceCenter();                        // 0x1028541e  slot 192
		Goal.bDestSet = true;
		Goal.Target = Player->Handle;                                    // 0x10285444  [10]
		StartTaskSetGoal(Goal, 0);                                       // 0x10285448  result discarded
		return 0;
	}

	case TASK_GET_PATH_TO_ENEMY_LOS:                                     // arm 0x10, 0x1028545a
	{
		if (Enemy() == nullptr)
		{
			Fail(0x86f, FAIL_NO_ENEMY);                                  // 0x10285482
			return 0;
		}
		float MinUnits = 0.f;
		float MaxUnits = 0.f;
		StartTaskWeaponRange(MinUnits, MaxUnits);                        // 0x10285495..0x1028555f
		FElysiumEntity* E = Enemy();                                     // 0x10285563
		FVector Los = FVector::ZeroVector;
		if (!StartTaskFindLosPos(E->Origin, E->EyePosition(), MinUnits, MaxUnits, Los)) // 0x102855b3
		{
			Fail(0x891, FAIL_NO_SHOOT);                                  // 0x102856f8
			return 0;
		}
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceHull);
		Goal.DestCm = Los;
		Goal.bDestSet = true;
		Goal.MovementActivity = ACT_RUN;                                 // 0x10285625
		StartTaskSetGoal(Goal, 0);                                       // 0x10285641  result discarded
		if (BaseScheduleHost.HintNode != INDEX_NONE)                     // 0x10285646
		{
			StartTaskSetArrivalActivity(GetCoverActivity(&BaseScheduleHost.HintNode)); // 0x10285664
		}
		if (FElysiumEntity* Again = Enemy())                             // 0x1028566d
		{
			StartTaskSetArrivalDirection(Again->Origin - Los); // 0x102856cc
		}
		else
		{
			++StartTaskNav.CrashGuards;
		}
		return 0;
	}

	case TASK_GET_PATH_TO_TARGET:                                        // arm 0x11, 0x10285949
	{
		FElysiumEntity* Tgt = ResolveTargetEnt();
		if (Tgt == nullptr)
		{
			Fail(0x8bf, FAIL_NO_TARGET);                                 // 0x10285998
			return 0;
		}
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceKeep);
		Goal.DestCm = Tgt->Origin;                                       // 0x102859e4
		Goal.bDestSet = true;
		Goal.Target = TargetEnt;                                         // 0x10285a80  [10]
		StartTaskSetGoal(Goal, 0);                                       // 0x10285a8c  result discarded
		return 0;
	}

	case TASK_GET_PATH_TO_HINTNODE:                                      // arm 0x12, 0x10285a9e
	{
		if (BaseScheduleHost.HintNode == INDEX_NONE)
		{
			Fail(0x8cf, FAIL_NO_HINT_NODE);                              // 0x10285ac4
			return 0;
		}
		FVector Approach = FVector::ZeroVector;
		HintLosEndpoint(BaseScheduleHost.HintNode, Approach);            // 0x10285add  0x102d1180
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceKeep);
		Goal.DestCm = Approach;
		Goal.bDestSet = true;
		Goal.MovementActivity = ACT_RUN;                                 // 0x10285b5e
		StartTaskSetGoal(Goal, 0);                                       // 0x10285b9e  result discarded
		return 0;
	}

	case TASK_STORE_LASTPOSITION:                                        // arm 0x13, 0x10282afd
		LastPosition = Origin;                                           // 0x10282b09  +0x5db8 = GetOrigin()
		LastFacing = Angles;                                             // 0x10282b2f  +0x5dc4 = GetAngles()
		TaskComplete(false);                                             // 0x10282b49
		return 0;

	case TASK_CLEAR_LASTPOSITION:                                        // arm 0x14, 0x10282b5b
		LastPosition = FVector::ZeroVector;                              // 0x10282b63  vec3_origin 0x1070d1b0
		LastFacing = FVector::ZeroVector;                                // 0x10282b86  vec3_angle 0x1070d9d0
		TaskComplete(false);                                             // 0x10282ba5
		return 0;

	case TASK_STORE_POSITION_IN_SAVEPOSITION:                            // arm 0x15, 0x10282bb7
		SavePosition = Origin;                                           // 0x10282bc5  +0x5dd0
		TaskComplete(false);                                             // 0x10282bdf
		return 0;

	case TASK_STORE_BESTSOUND_IN_SAVEPOSITION:                           // arm 0x16, 0x10282bf1
	{
		const FElysiumGameSoundEvent* Sound = static_cast<const FElysiumGameSoundEvent*>(GetBestSound()); // 0x10282bf5
		if (Sound == nullptr)
		{
			FailText(0x492, TEXT("No Sound!"), FAIL_TEXT_NO_SOUND);      // 0x10282c1c
			return 0;
		}
		SavePosition = Sound->Position;                                  // 0x10282c33  sound+0x20
		// `0x100290c0(gEntList, sound)` resolves the sound's owner; when it does, TWICE its slot 199
		// `GetVelocity` vector is added, the Z addend first computed into a temporary.
		if (FElysiumEntity* Owner = World != nullptr ? World->Resolve(Sound->Source) : nullptr) // 0x10282c51
		{
			FVector OwnerVelocity = FVector::ZeroVector;
			Owner->GetVelocity(&OwnerVelocity, nullptr);                      // 0x10282c65  slot 199
			const float DoubledZ = OwnerVelocity.Z + OwnerVelocity.Z;              // 0x10282c7b
			SavePosition.X += OwnerVelocity.X + OwnerVelocity.X;                   // 0x10282c8c
			SavePosition.Y += OwnerVelocity.Y + OwnerVelocity.Y;                   // 0x10282c98
			SavePosition.Z += DoubledZ;                                  // 0x10282cab
		}
		TaskComplete(false);                                             // 0x10282cb5
		return 0;
	}

	case TASK_STORE_ENEMY_POSITION_IN_SAVEPOSITION:                      // arm 0x17, 0x10282cc7
	{
		if (Enemy() == nullptr)
		{
			Fail(0x4a7, FAIL_NO_ENEMY);                                  // 0x10282cef
			return 0;
		}
		SavePosition = Enemy()->Origin;                                  // 0x10282d0e  GetAbsOrigin
		TaskComplete(false);                                             // 0x10282d32
		return 0;
	}

	case TASK_GET_PATH_TO_LASTPOSITION:                                  // arm 0x18, 0x10285bb0
	{
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceKeep);
		Goal.DestCm = LastPosition;                                      // 0x10285bcc
		Goal.bDestSet = true;
		if (!StartTaskSetGoal(Goal, 0))                                  // 0x10285c64
		{
			Fail(0x8df, FAIL_NO_ROUTE);                                  // 0x10285c87
			return 0;
		}
		StartTaskSetArrivalDirectionAngles(LastFacing);    // 0x10285ca7  0x102ee550
		return 0;
	}

	case TASK_GET_PATH_TO_SAVEPOSITION:                                  // arm 0x19, 0x10285cb9
	{
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceKeep);
		Goal.DestCm = SavePosition;                                      // 0x10285cd5
		Goal.bDestSet = true;
		StartTaskSetGoal(Goal, 0);                                       // 0x10285d6d  result discarded
		return 0;
	}

	case TASK_GET_PATH_TO_SAVEPOSITION_LOS:                              // arm 0x1a, 0x1028570b
	{
		float MinUnits = 0.f;
		float MaxUnits = 0.f;
		StartTaskWeaponRange(MinUnits, MaxUnits);                        // 0x1028570d..0x102857d7
		// The aim point is the save position plus the NPC's OWN `m_vecViewOffset` (`+0x184`).
		const FVector Aim = SavePosition + (EyePosition() - Origin);     // 0x102857d7
		FVector Los = FVector::ZeroVector;
		if (!StartTaskFindLosPos(SavePosition, Aim, MinUnits, MaxUnits, Los)) // 0x1028584b
		{
			Fail(0x8b6, FAIL_NO_SHOOT);                                  // 0x10285936
			return 0;
		}
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceHull);
		Goal.DestCm = Los;
		Goal.bDestSet = true;
		Goal.MovementActivity = ACT_RUN;                                 // 0x102858ce
		StartTaskSetGoal(Goal, 0);                                       // 0x1028590a  result discarded
		return 0;
	}

	case TASK_GET_PATH_TO_RANDOM_NODE:                                   // arm 0x1b, 0x10285d7f
	{
		const FVector Direction = BodyDirection2D();                     // 0x10285d93  slot 368
		const float DistanceUnits = ResolveTaskDistance(Data);           // 0x10285da2  slot 418
		if (StartTaskSetRandomGoal(DistanceUnits, Direction))            // 0x10285dae  0x102ed940
		{
			TaskComplete(false);                                         // 0x10285dbb
			return 0;
		}
		Fail(0x8f7, FAIL_NO_REACHABLE_NODE);                             // 0x10285de5
		return 0;
	}

	case TASK_GET_PATH_TO_BESTSOUND:                                     // arm 0x1c, 0x10285df8
	case TASK_GET_PATH_TO_BESTSCENT:                                     // arm 0x1d, 0x10285ef8
	{
		const bool bScent = TaskId == TASK_GET_PATH_TO_BESTSCENT;
		const FElysiumGameSoundEvent* Sound = static_cast<const FElysiumGameSoundEvent*>(
			bScent ? GetBestScent() : GetBestSound());                   // 0x10285efc slot 475 / 0x10285dfc slot 474
		if (Sound == nullptr)
		{
			if (bScent)
			{
				Fail(0x915, FAIL_NO_SCENT);                              // 0x10285f22
			}
			else
			{
				Fail(0x905, FAIL_NO_SOUND);                              // 0x10285e22
			}
			return 0;
		}
		// Tolerance `-1.0` here OVERRIDES nothing new but is authored explicitly: `SetGoal` keeps
		// the path's tolerance unless it is zero.
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceKeep);
		Goal.DestCm = Sound->Position;                                   // sound+0x20
		Goal.bDestSet = true;
		StartTaskSetGoal(Goal, 0);                                       // 0x10285ee6 / 0x10285fe6  result discarded
		return 0;
	}

	case TASK_RUN_PATH:                                                  // arm 0x1e, 0x102863f1
	{
		const int32 Activity = SelectWeightedSequenceForActivity(ACT_RUN) != INDEX_NONE ? ACT_RUN : ACT_WALK; // 0x102863f9
		StartTaskSetMovementActivity(Activity);                          // 0x1028640e
		BaseScheduleHost.MemoryBits &= ~static_cast<uint32>(MemoryInCover); // 0x1028641b
		TaskComplete(false);                                             // 0x10286426
		return 0;
	}

	case TASK_WALK_PATH:                                                 // arm 0x1f, 0x10286438
	{
		int32 Activity = INDEX_NONE;
		const bool bFly = GetMoveType() == MoveTypeFly || GetMoveType() == MoveTypeFlyGravity; // 0x1028643c / 0x1028644e
		if (bFly && SelectWeightedSequenceForActivity(ACT_FLY) != INDEX_NONE) // 0x1028645e
		{
			Activity = ACT_FLY;
		}
		else
		{
			Activity = SelectWeightedSequenceForActivity(ACT_WALK) != INDEX_NONE ? ACT_WALK : ACT_RUN; // 0x10286470
		}
		StartTaskSetMovementActivity(Activity);                          // 0x10286485
		BaseScheduleHost.MemoryBits &= ~static_cast<uint32>(MemoryInCover); // 0x10286492
		TaskComplete(false);                                             // 0x1028649d
		return 0;
	}

	case TASK_WALK_PATH_TIMED:                                           // arm 0x20, 0x102864f1
		BaseScheduleHost.bShouldMove = true;                             // 0x102864f9  +0x1a40
		StartTaskSetMovementActivity(ACT_WALK);                          // 0x10286500
		BaseScheduleHost.WaitFinished = Now + Data;                      // 0x10286511  falls into arm 0x01
		return 0;

	case TASK_WALK_PATH_WITHIN_DIST:                                     // arm 0x21, 0x102864af
		BaseScheduleHost.bShouldMove = true;                             // 0x102864b7
		StartTaskSetMovementActivity(ACT_WALK);                          // 0x102864be
		return 0;

	case TASK_RUN_PATH_WITHIN_DIST:                                      // arm 0x22, 0x102864d0
		BaseScheduleHost.bShouldMove = true;                             // 0x102864d0
		StartTaskSetMovementActivity(ACT_RUN);                           // 0x102864df  falls into arm 0x61
		return 0;

	case TASK_RUN_PATH_TIMED:                                            // arm 0x23, 0x10286523
		BaseScheduleHost.bShouldMove = true;                             // 0x1028652b
		StartTaskSetMovementActivity(ACT_RUN);                           // 0x10286532
		BaseScheduleHost.WaitFinished = Now + Data;                      // 0x10286544  falls into arm 0x29
		return 0;

	case TASK_STRAFE_PATH:                                               // arm 0x24, 0x10286556
	{
		BaseScheduleHost.bShouldMove = true;                             // 0x10286563
		FVector Right = FVector::ZeroVector;
		StartTaskAngleVectors(Angles, nullptr, &Right);                  // 0x10286571
		const FVector Waypoint = StartTaskCurWaypointPos();              // 0x1028658d
		// Both vectors normalised in 2-D only; a zero length zeroes the components, otherwise each
		// is scaled by `1.0 / len` (`0x104454c0`).
		double DirX = Waypoint.X - Origin.X;                             // 0x10286592
		double DirY = Waypoint.Y - Origin.Y;
		const double DirLen = FMath::Sqrt(DirX * DirX + DirY * DirY);    // 0x102865ba
		if (DirLen == 0.0)                                               // 0x102865ce
		{
			DirX = 0.0;
			DirY = 0.0;
		}
		else
		{
			DirX *= 1.0 / DirLen;
			DirY *= 1.0 / DirLen;
		}
		double RightX = Right.X;
		double RightY = Right.Y;
		const double RightLen = FMath::Sqrt(RightX * RightX + RightY * RightY); // 0x10286624
		if (RightLen == 0.0)                                             // 0x10286638
		{
			RightX = 0.0;
			RightY = 0.0;
		}
		else
		{
			RightX *= 1.0 / RightLen;
			RightY *= 1.0 / RightLen;
		}
		const double Dot = RightX * DirX + RightY * DirY;                // 0x10286674
		StartTaskSetMovementActivity(Dot <= 0.0 ? ACT_STRAFE_LEFT : ACT_STRAFE_RIGHT); // 0x10286683 / 0x102866a4 / 0x10286687
		TaskComplete(false);                                             // 0x10286690 / 0x102866ad
		return 0;
	}

	case TASK_CLEAR_MOVE_WAIT:                                           // arm 0x25, 0x10284218
		BaseScheduleHost.MoveWaitFinished = Now;                         // 0x10284225  +0x5cf0
		TaskComplete(false);                                             // 0x1028422b
		return 0;

	case TASK_SMALL_FLINCH:                                              // arm 0x26, 0x102867e5
		SetIdealActivity(StartTaskFlinchActivity());                     // 0x102867e7 / 0x102867ef
		return 0;

	case TASK_FACE_IDEAL:                                                // arm 0x27, 0x10283cd5
		StartTaskMotorHoldYaw();                                         // 0x10283cdb
		SetTurnActivity();                                               // 0x10283ce4  slot 572
		return 0;

	case TASK_FACE_PATH:                                                 // arm 0x28, 0x10283cf7
	{
		if (!NavIsGoalActive())                                          // 0x10283cfd  0x102ee6a0
		{
			StartTaskDevMessage(TEXT("No route to face!\n"));            // 0x10283d0d  DevWarning(2, ...)
			Fail(0x63e, FAIL_NO_ROUTE);                                  // 0x10283d30
			return 0;
		}
		StartTaskMotorHoldYaw();                                         // 0x10283d49
		StartTaskMotorSetIdealYawToTarget(StartTaskCurWaypointPos());    // 0x10283d5c / 0x10283d64
		if (FMath::Abs(static_cast<double>(MotorDeltaIdealYaw())) > FacePathYawTolerance) // 0x10283d6f / 0x10283d76
		{
			SetTurnActivity();                                           // 0x10283d9b
			return 0;
		}
		TaskComplete(false);                                             // 0x10283d87
		return 0;
	}

	case TASK_FACE_PLAYER:                                               // arm 0x29, 0x10286537
		BaseScheduleHost.WaitFinished = Now + Data;                      // 0x10286544
		return 0;

	case TASK_FACE_ENEMY:                                                // arm 0x2a, 0x10283c66
	{
		const FVector Lkp = StartTaskLastKnownPosition(Enemy());         // 0x10283c85
		if (FInAimCone(Lkp))                                             // 0x10283c93  slot 364
		{
			TaskComplete(false);                                         // 0x10286cdd (arm 0x02's)
			return 0;
		}
		StartTaskMotorHoldYaw();                                         // 0x10283ca7
		StartTaskMotorSetIdealYawToTarget(Lkp);                          // 0x10283cb9
		SetTurnActivity();                                               // 0x10283cc2
		return 0;
	}

	case TASK_FACE_HINTNODE:                                             // arm 0x2b, 0x10283a36
	{
		StartTaskMotorHoldYaw();                                         // 0x10283a3c
		if (BaseScheduleHost.HintNode == INDEX_NONE)
		{
			// `0x102d12e0(m_pHintNode)` with no null test. Named crash guard.
			++StartTaskNav.CrashGuards;
			return 0;
		}
		float Yaw = 0.f;
		StartTaskHintYaw(BaseScheduleHost.HintNode, Yaw);                // 0x10283a4d  0x102d12e0
		StartTaskMotorSetIdealYaw(Yaw);                                  // 0x10283a52 shared store
		SetTurnActivity();                                               // 0x10283a9a / 0x102829aa
		return 0;
	}

	case TASK_PLAY_HINT_ACTIVITY:                                        // arm 0x2c, 0x10282dfb
	{
		FHintWords Hint;
		if (BaseScheduleHost.HintNode == INDEX_NONE || !HintWords(BaseScheduleHost.HintNode, Hint))
		{
			// `m_pHintNode->m_nHintType` (`+0x5dc`) read with no null test (`0x10282e05`). Named
			// crash guard.
			++StartTaskNav.CrashGuards;
			return 0;
		}
		SetIdealActivity(GetHintActivity(static_cast<int16>(Hint.HintType))); // 0x10282e0c slot 567 / 0x10282e15
		return 0;
	}

	case TASK_FACE_TARGET:                                               // arm 0x2d, 0x10283b9e
	{
		FElysiumEntity* Tgt = ResolveTargetEnt();
		if (Tgt == nullptr)
		{
			Fail(0x61c, FAIL_NO_TARGET);                                 // 0x10283c53
			return 0;
		}
		StartTaskMotorHoldYaw();                                         // 0x10283bd8
		StartTaskMotorSetIdealYawToTarget(Tgt->Origin);                  // 0x10283c1d  GetAbsOrigin
		SetTurnActivity();                                               // 0x10283c26
		return 0;
	}

	case TASK_FACE_LASTPOSITION:                                         // arm 0x2e, 0x10283aad
		StartTaskMotorHoldYaw();                                         // 0x10283ab3
		StartTaskMotorSetIdealYawToTarget(LastPosition);                 // 0x10283ac7
		SetTurnActivity();                                               // 0x10283ad0
		return 0;

	case TASK_SET_IDEAL_YAW_TO_CURRENT:                                  // arm 0x2f, 0x10283ae3
		StartTaskMotorHoldYaw();                                         // 0x10283ae9
		StartTaskMotorSetIdealYaw(StartTaskAngleMod(static_cast<float>(Angles.Y))); // 0x10283af8..0x10283b62
		TaskComplete(false);                                             // 0x10283b67 / 0x10283b8c
		return 0;

	case TASK_RANGE_ATTACK1:                                             // arm 0x30, 0x10284286
	case TASK_RANGE_ATTACK1_NOTURN:
		LastAttackTime = Now;                                            // 0x10284293  +0x5d9c
		RestartIdealActivityId(ACT_RANGE_ATTACK1);                       // 0x10284299  0x10289ee0
		return 0;

	case TASK_RANGE_ATTACK2:                                             // arm 0x31, 0x102842ab
	case TASK_RANGE_ATTACK2_NOTURN:
		LastAttackTime = Now;                                            // 0x102842b5
		RestartIdealActivityId(ACT_RANGE_ATTACK2);                       // 0x102842bd
		return 0;

	case TASK_MELEE_ATTACK1:                                             // arm 0x32, 0x1028423d
	case TASK_MELEE_ATTACK1_NOTURN:
		LastAttackTime = Now;                                            // 0x10284247
		RestartIdealActivityId(ACT_MELEE_ATTACK1);                       // 0x1028424f
		return 0;

	case TASK_MELEE_ATTACK2:                                             // arm 0x33, 0x10284261
	case TASK_MELEE_ATTACK2_NOTURN:
		LastAttackTime = Now;                                            // 0x1028426e
		RestartIdealActivityId(ACT_MELEE_ATTACK2);                       // 0x10284274
		return 0;

	case TASK_RELOAD:                                                    // arm 0x34, 0x102842cf
	case TASK_RELOAD_NOTURN:
		RestartIdealActivityId(ACT_RELOAD);                              // 0x102842d3  no attack stamp
		return 0;

	case TASK_SPECIAL_ATTACK1:                                           // arm 0x35, 0x102842e5
		RestartIdealActivityId(ACT_SPECIAL_ATTACK1);                     // 0x102842e9
		return 0;

	case TASK_SPECIAL_ATTACK2:                                           // arm 0x36, 0x102842fb
		RestartIdealActivityId(ACT_SPECIAL_ATTACK2);                     // 0x102842ff
		return 0;

	case TASK_FIND_HINTNODE:                                             // arm 0x37, 0x10282a17
	case TASK_FIND_LOCK_HINTNODE:
		if (BaseScheduleHost.HintNode != INDEX_NONE)                     // 0x10282a1f
		{
			TaskComplete(false);                                         // 0x10282a4c
		}
		else
		{
			// `0x102d1af0(this, 0, (int)data, 2000.0, NULL, NULL)` — the operand is the hint TYPE.
			BaseScheduleHost.HintNode = FindHintNear(static_cast<int32>(Data), 0, HintSearchRadiusUnits); // 0x10282a36 / 0x10282a3e
			if (BaseScheduleHost.HintNode != INDEX_NONE)
			{
				TaskComplete(false);                                     // 0x10282a4c
			}
			else
			{
				Fail(0x45a, FAIL_NO_HINT_NODE);                          // 0x10282a69
			}
		}
		if (TaskId == TASK_FIND_HINTNODE)                                // 0x10282a6f
		{
			return 0;
		}
		// `TASK_FIND_LOCK_HINTNODE` falls into arm 0x39's body.
		[[fallthrough]];
	case TASK_LOCK_HINTNODE:                                             // arm 0x39, 0x10282a79
	{
		if (BaseScheduleHost.HintNode == INDEX_NONE)
		{
			Fail(0x465, FAIL_NO_HINT_NODE);                              // 0x10282a99
			return 0;
		}
		// `0x102d1350(hint, this)` — the claim. It lives on the Troika's helper surface
		// (`FElysiumNpc::ClaimHintNode`), which a base-only NPC does not have.
		++StartTaskNav.HintLockAttempts;
		FElysiumNpc* Troika = AsNpc();
		const bool bLocked = Troika != nullptr && Troika->ClaimHintNode(BaseScheduleHost.HintNode); // 0x10282aad
		if (bLocked)
		{
			TaskComplete(false);                                         // 0x10282aba
			return 0;
		}
		Fail(0x46d, FAIL_ALREADY_LOCKED);                                // 0x10282ae0
		BaseScheduleHost.HintNode = INDEX_NONE;                          // 0x10282ae6  the failure drops the hint too
		return 0;
	}

	case TASK_CLEAR_HINTNODE:                                            // arm 0x38, 0x10282d44
		ReleaseHintNode(BaseScheduleHost.HintNode, ElysiumNpcTunables::Zero); // 0x10282d4c  0x102d1420(hint, 0.0)
		BaseScheduleHost.HintNode = INDEX_NONE;                          // 0x10282d55
		TaskComplete(false);                                             // 0x10282d5f
		return 0;

	case TASK_SOUND_ANGRY:                                               // arm 0x3a, 0x102868a3
		StartTaskDevMessage(TEXT("SOUND\n"));                            // 0x102868aa  DevMsg(2, ...) — no sound is played
		TaskComplete(false);                                             // 0x102868b7
		return 0;

	case TASK_SOUND_IDLE:                                                // arm 0x3b, 0x10286863
		IdleSound();                                                     // 0x10286867  slot 490
		TaskComplete(false);                                             // 0x10286871
		return 0;

	case TASK_SOUND_WAKE:                                                // arm 0x3c, 0x10286823
		AlertSound();                                                    // 0x10286827  slot 489
		TaskComplete(false);                                             // 0x10286831
		return 0;

	case TASK_SOUND_PAIN:                                                // arm 0x3d, 0x10286883
		PainSound();                                                     // 0x10286887  slot 491
		TaskComplete(false);                                             // 0x10286891
		return 0;

	case TASK_SOUND_DIE:                                                 // arm 0x3e, 0x10286843
		DeathSound();                                                    // 0x10286847  slot 488
		TaskComplete(false);                                             // 0x10286851
		return 0;

	case TASK_SPEAK_SENTENCE:                                            // arm 0x3f, 0x102868c9
		SpeakSentence(static_cast<int32>(Data));                         // 0x102868d6  slot 508
		TaskComplete(false);                                             // 0x102868e0
		return 0;

	case TASK_SET_ACTIVITY:                                              // arm 0x40, 0x10284311
	{
		const int32 Activity = StartTaskActivityOperand(*Step);          // 0x10284314
		if (Activity != 0)
		{
			SetIdealActivity(Activity);                                  // 0x10284320
		}
		else
		{
			ActivityNumber = 0;                                          // 0x10284332  m_Activity = 0
		}
		// Neither arm completes.
		return 0;
	}

	case TASK_SET_SCHEDULE:                                              // arm 0x41, 0x10282e27
	{
		// `0x102cc1f0`: slot 440 `TranslateSchedule`, slot 446 `GetScheduleOfType`, and on a miss the
		// DevMsg and schedule 1 — so the null test below only fails when schedule 1 is missing too.
		const int32 Translated = TranslateSchedule(static_cast<int32>(Data)); // 0x102cc1fd
		const FElysiumScheduleProgram* Program =
			static_cast<const FElysiumScheduleProgram*>(GetScheduleOfType(Translated)); // 0x102cc20a
		if (Program == nullptr)
		{
			StartTaskDevMessage(FString::Printf(
				TEXT("GetScheduleOfType(): No CASE for Schedule Type %d!\n"), Translated)); // 0x102cc21a
			Program = static_cast<const FElysiumScheduleProgram*>(GetScheduleOfType(1)); // 0x102cc229
		}
		if (Program == nullptr)                                          // 0x10282e39
		{
			Fail(0x507, FAIL_SCHEDULE_NOT_FOUND);                        // 0x10282e98
			return 0;
		}
		// `+0x1b2c = 1`, `+0x1b30 = file`, `+0x1b34 = 0x4fc` — the SetSchedule call record.
		RecordScheduleEvent(FString::Printf(TEXT("SetSchedule trace 1 %s:%d"), File, 0x4fc)); // 0x10282e3d
		BaseScheduleHost.IdealScheduleRetail = static_cast<int32>(Data); // 0x10282e66  the UNtranslated id
		ElysiumSchedule::Install(Schedule, Program->GlobalId, *this);    // 0x10282e6c  SetSchedule 0x10280e50
		// No `TaskComplete`: the schedule change ends the program.
		return 0;
	}

	case TASK_SET_FAIL_SCHEDULE:                                         // arm 0x42, 0x10286c45
		Schedule.FailScheduleOverride = static_cast<int32>(Data);        // 0x10286c51  m_failSchedule (+0x5c54)
		TaskComplete(false);                                             // 0x10286c57
		return 0;

	case TASK_SET_TOLERANCE_DISTANCE:                                    // arm 0x43, 0x10286c69
		SetGoalTolerance(ResolveTaskDistance(Data));                     // 0x10286c71 slot 418 / 0x10286c84 0x102ee1c0, untruncated
		TaskComplete(false);                                             // 0x10286c8d
		return 0;

	case TASK_SET_ROUTE_SEARCH_TIME:                                     // arm 0x44, 0x10286d1a
		NavRouteSearchTime = static_cast<float>(static_cast<int32>(Data)); // 0x10286d1d / 0x10286d3d  0x102886f0, truncated
		TaskComplete(false);                                             // 0x10286d46
		return 0;

	case TASK_CLEAR_FAIL_SCHEDULE:                                       // arm 0x45, 0x10286d58
		Schedule.FailScheduleOverride = 0;                               // 0x10286d5c  m_failSchedule = 0
		TaskComplete(false);                                             // 0x10286d66
		return 0;

	case TASK_PLAY_SEQUENCE:                                             // arm 0x46, 0x10282dde
	case TASK_PLAY_PRIVATE_SEQUENCE:
	case TASK_PLAY_PRIVATE_SEQUENCE_FACE_ENEMY:
	case TASK_PLAY_SEQUENCE_FACE_ENEMY:
	case TASK_PLAY_SEQUENCE_FACE_TARGET:
		SetIdealActivity(StartTaskActivityOperand(*Step));               // 0x10282de1 / 0x10282de9
		return 0;

	case TASK_FIND_COVER_FROM_BEST_SOUND:                                // arm 0x47, 0x102838e7
	{
		const FElysiumGameSoundEvent* Sound = static_cast<const FElysiumGameSoundEvent*>(GetBestSound()); // 0x102838eb
		if (Sound == nullptr)
		{
			FailText(0x5eb, TEXT("No sound in list"), FAIL_TEXT_NO_SOUND_IN_LIST); // 0x10283914
			return 0;
		}
		// The sound's origin is BOTH the threat and its eye, and its `m_iVolume` (`+0x8`, an int in
		// units) is the MINIMUM radius.
		const float VolumeUnits = static_cast<float>(static_cast<int32>(Sound->RadiusCm / U)); // 0x1028394d FILD
		FVector Cover = FVector::ZeroVector;
		if (!StartTaskFindCoverPos(Sound->Position, Sound->Position, VolumeUnits, CoverRadius(), Cover)) // 0x10283955
		{
			Fail(0x5fb, FAIL_NO_COVER);                                  // 0x10283a23
			return 0;
		}
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceHull);
		Goal.DestCm = Cover;
		Goal.bDestSet = true;
		Goal.MovementActivity = ACT_RUN;                                 // 0x102839d1
		StartTaskSetGoal(Goal, 0);                                       // 0x102839e5
		BaseScheduleHost.MoveWaitFinished = Now + Data;                  // 0x102839f7
		return 0;
	}

	case TASK_FIND_COVER_FROM_ENEMY:                                     // arm 0x48, 0x10283558
	{
		const FElysiumEntity* Threat = Enemy();                          // 0x1028355c
		if (Threat == nullptr)
		{
			Threat = this;                                               // 0x10283568
		}
		if (StartTaskFindLateralCover(Threat->EyePosition(), Threat))    // 0x10283580  0x102784a0
		{
			BaseScheduleHost.MoveWaitFinished = Now + Data;              // 0x10283599
			TaskComplete(false);                                         // 0x1028359f
			return 0;
		}
		FVector Cover = FVector::ZeroVector;
		if (!StartTaskFindCoverPos(Threat->Origin, Threat->EyePosition(), 0.f, CoverRadius(), Cover)) // 0x102835ed
		{
			Fail(0x5a6, FAIL_NO_COVER);                                  // 0x102836e7
			return 0;
		}
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION_NEAREST_NODE, NavToleranceHull);
		Goal.DestCm = Cover;
		Goal.bDestSet = true;
		Goal.MovementActivity = ACT_RUN;                                 // 0x1028365f
		StartTaskSetGoal(Goal, 0);                                       // 0x1028367b
		HintArrival();                                                   // 0x10283680..0x102836c3
		BaseScheduleHost.MoveWaitFinished = Now + Data;                  // 0x102839f7 (via 0x102836c8)
		return 0;
	}

	case TASK_FIND_LATERAL_COVER_FROM_ENEMY:                             // arm 0x49, 0x102836fa
	{
		// `m_hEnemy` (`+0x5ce0`) resolved raw (not slot 167), else this NPC.
		const FElysiumEntity* Threat = World != nullptr ? World->Resolve(BaseMemory.Enemy) : nullptr; // 0x102836fa
		if (Threat == nullptr)
		{
			Threat = this;                                               // 0x1028372c
		}
		if (!StartTaskFindLateralCover(Threat->EyePosition(), Threat))   // 0x1028376b
		{
			Fail(0x5c2, FAIL_NO_COVER);                                  // 0x102837b6
			return 0;
		}
		BaseScheduleHost.MoveWaitFinished = Now + Data;                  // 0x10283784
		TaskComplete(false);                                             // 0x1028378a
		return 0;
	}

	case TASK_FIND_BACKAWAY_FROM_SAVEPOSITION:                           // arm 0x4a, 0x10282eab
	{
		if (Enemy() == nullptr)
		{
			Fail(0x510, FAIL_NO_ENEMY);                                  // 0x10282ed3
			return 0;
		}
		// `0x102edae0(nav, &m_vSavePosition, 0.0, 30000.0, &out)` — the family-Senses10 seam.
		FVector Node = FVector::ZeroVector;
		if (!NearestNavigatorNode(SavePosition, 0.f, BackawaySearchUnits, Node)) // 0x10282f00
		{
			Fail(0x519, FAIL_NO_BACKAWAY_NODE);                          // 0x10282f23
			return 0;
		}
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceKeep);
		Goal.DestCm = Node;
		Goal.bDestSet = true;
		Goal.MovementActivity = ACT_RUN;                                 // 0x10282fb3
		if (StartTaskSetGoal(Goal, 0))                                   // 0x10282fec
		{
			TaskComplete(false);                                         // 0x10282ff8
			return 0;
		}
		Fail(0x524, FAIL_NO_ROUTE);                                      // 0x10283022
		return 0;
	}

	case TASK_FIND_NODE_COVER_FROM_ENEMY:                                // arm 0x4b, 0x102833ac
	case TASK_FIND_NEAR_NODE_COVER_FROM_ENEMY:                           // arm 0x4c, 0x10283035
	case TASK_FIND_FAR_NODE_COVER_FROM_ENEMY:                            // arm 0x4d, 0x102831ea
	{
		// One shape, three radius pairs and three line sets.
		int32 NoEnemyLine = 0x566;                                       // 0x5b: 0x102833ca
		int32 NoCoverLine = 0x57a;                                       // 0x5b: 0x1028353b
		float CoverTolerance = NavToleranceKeep;                              // 0x5b: 0x10283469
		if (TaskId == TASK_FIND_NEAR_NODE_COVER_FROM_ENEMY)
		{
			NoEnemyLine = 0x52d;                                         // 0x10283053
			NoCoverLine = 0x541;                                         // 0x102831cd
		}
		else if (TaskId == TASK_FIND_FAR_NODE_COVER_FROM_ENEMY)
		{
			NoEnemyLine = 0x549;                                         // 0x10283208
			NoCoverLine = 0x55e;                                         // 0x1028338f
			CoverTolerance = NavToleranceHull;                              // 0x102832bd
		}
		if (Enemy() == nullptr)
		{
			Fail(NoEnemyLine, FAIL_NO_ENEMY);                            // 0x102833d4 / 0x1028305d / 0x10283212
			return 0;
		}
		FElysiumEntity* E = Enemy();
		// The radius pair: 0x5b 0 .. CoverRadius; 0x5c 0 .. ResolveTaskDistance(data);
		// 0x5d ResolveTaskDistance(data) .. CoverRadius. Evaluation order is retail's: the
		// far arm reads CoverRadius before ResolveTaskDistance (`0x10283259`, `0x1028326b`).
		float MinUnits = 0.f;
		float MaxUnits = 0.f;
		if (TaskId == TASK_FIND_NODE_COVER_FROM_ENEMY)
		{
			MaxUnits = CoverRadius();                                    // 0x10283417
		}
		else if (TaskId == TASK_FIND_NEAR_NODE_COVER_FROM_ENEMY)
		{
			MaxUnits = ResolveTaskDistance(Data);                        // 0x102830a8
		}
		else
		{
			MaxUnits = CoverRadius();                                    // 0x10283259
			MinUnits = ResolveTaskDistance(Data);                        // 0x1028326b
		}
		FVector Cover = FVector::ZeroVector;
		if (!StartTaskFindCoverPos(E->Origin, E->EyePosition(), MinUnits, MaxUnits, Cover)) // 0x10283443 / 0x102830d7 / 0x10283297
		{
			Fail(NoCoverLine, FAIL_NO_COVER);                            // 0x10283545 / 0x102831d7 / 0x10283399
			return 0;
		}
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION_NEAREST_NODE, CoverTolerance);
		Goal.DestCm = Cover;
		Goal.bDestSet = true;
		Goal.MovementActivity = ACT_RUN;                                 // 0x102834b5 / 0x1028313f / 0x10283309
		StartTaskSetGoal(Goal, 0);                                       // 0x102834d1 / 0x10283163 / 0x10283325
		// With no hint held the arm breaks to the bare return; neither path completes.
		HintArrival();                                                   // 0x102834d6..0x10283519
		return 0;
	}

	case TASK_FIND_COVER_FROM_ORIGIN:                                    // arm 0x4e, 0x102837c9
	{
		FVector Cover = FVector::ZeroVector;
		if (!StartTaskFindCoverPos(Origin, EyePosition(), 0.f, CoverRadius(), Cover)) // 0x10283807
		{
			Fail(0x5d5, FAIL_NO_COVER);                                  // 0x102838d4
			return 0;
		}
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceHull);
		Goal.DestCm = Cover;
		Goal.bDestSet = true;
		Goal.MovementActivity = ACT_RUN;                                 // 0x1028387b
		StartTaskSetGoal(Goal, 0);                                       // 0x10283897
		BaseScheduleHost.MoveWaitFinished = Now + Data;                  // 0x102838a8
		return 0;
	}

	case TASK_DIE:                                                       // arm 0x4f, 0x10286801
	case TASK_DIE_IMMEDIATE:
		StartTaskClearGoal();                                            // 0x10286807  0x102ee270
		LifeStateRetail = LifeStateDying;                                // 0x1028680c  m_lifeState = 1
		return 0;

	case TASK_WAIT_FOR_SCRIPT:                                           // arm 0x50, 0x102868f2
	{
		FElysiumScriptedSequence* Cine = ResolveCine();                  // 0x102868f2  m_hCine
		if (Cine == nullptr)
		{
			// Retail reads `(NULL)+0x5f44` with no test (`0x10286938`). Named crash guard.
			++StartTaskNav.CrashGuards;
			return 0;
		}
		if (!Cine->PreIdle.IsEmpty())                                    // 0x1028693f  m_iszIdle != NULL_STRING
		{
			Cine->StartSequence(*this, Cine->PreIdle, false);            // 0x10286966  slot 584
			// `strcmp(STRING(m_iszPlay), STRING(m_iszIdle)) == 0` (`0x10288560`) -> arm 0x65's store.
			if (Cine->Play.Equals(Cine->PreIdle, ESearchCase::CaseSensitive)) // 0x10286996
			{
				if (FElysiumNpc* Troika = AsNpc())
				{
					Troika->SequencePlaybackRate = 0.f;                  // 0x102869a6  +0x6f4
				}
			}
			return 0;
		}
		if (FElysiumScriptedSequence::ScriptStateOf(*this) != ScriptStateCustomMove) // 0x102869bd  +0x5d70
		{
			SetIdealActivity(ACT_IDLE);                                  // 0x102869ce
		}
		return 0;
	}

	case TASK_PUSH_SCRIPT_ARRIVAL_ACTIVITY:                              // arm 0x51, 0x102869e0
	{
		FElysiumScriptedSequence* Cine = ResolveCine();
		if (Cine == nullptr)
		{
			// `m_hCine` resolved and read with no test (`0x102869f5`). Named crash guard.
			++StartTaskNav.CrashGuards;
			return 0;
		}
		// Prefer `m_iszPlay` (`+0x5f48`), then `m_iszPostIdle` (`+0x5f4c`), else NULL_STRING.
		FString Pick;
		if (!Cine->Play.IsEmpty())                                       // 0x10286a02
		{
			Pick = Cine->Play;
		}
		else if (!Cine->PostIdle.IsEmpty())                              // 0x10286a3a
		{
			Pick = Cine->PostIdle;
		}
		ScriptArrivalActivity = INDEX_NONE;                              // 0x10286a72  +0x5d7c = -1
		ScriptArrivalSequence.Empty();                                   // 0x10286a86  +0x5d80 = NULL_STRING
		if (!Pick.IsEmpty())                                             // 0x10286a96
		{
			ScriptArrivalActivity = ActivityIdForName(Pick);             // 0x10286aa9  ActivityList_IndexForName 0x1025d760
			if (ScriptArrivalActivity == INDEX_NONE)                     // 0x10286ab1
			{
				ScriptArrivalSequence = Pick;                            // 0x10286abf  the raw name is parked
			}
		}
		TaskComplete(false);                                             // 0x10286ac9
		return 0;
	}

	case TASK_PLAY_SCRIPT:                                               // arm 0x52, 0x10286adb
		// `HasMovement(GetSequence())` (`0x10288610` -> `0x10094eb0`), result discarded; then the
		// empty `0x1027f270(0)`; then `m_scriptState = 0`, which this runtime keeps on the director.
		if (FElysiumScriptedSequence* Cine = ResolveCine())
		{
			Cine->NpcScriptState = ScriptStatePlaying;                   // 0x10286af3  +0x5d70 = 0
		}
		return 0;

	case TASK_PLAY_SCRIPT_POST_IDLE:                                     // arm 0x53, 0x10286b0a
		if (FElysiumScriptedSequence* Cine = ResolveCine())              // 0x1027f270(2) is empty
		{
			Cine->NpcScriptState = ScriptStatePostIdle;                  // 0x10286b13  +0x5d70 = 2
		}
		return 0;

	case TASK_ENABLE_SCRIPT:                                             // arm 0x54, 0x10286b2a
		if (FElysiumScriptedSequence* Cine = ResolveCine())
		{
			Cine->DelayStart(false);                                     // 0x10286b39  0x101a8cf0(cine, 0)
		}
		else
		{
			// `DelayStart` on a null cine (`0x10286b39`). Named crash guard.
			++StartTaskNav.CrashGuards;
		}
		TaskComplete(false);                                             // 0x10286b42
		return 0;

	case TASK_PLANT_ON_SCRIPT:                                           // arm 0x55, 0x10286b54
		if (FElysiumEntity* Tgt = ResolveTargetEnt())                              // 0x10286b5e  0x1028ac10
		{
			SetOrigin(Tgt->Origin);                                      // 0x10286b7b  0x102885d0: slot 62 SetOrigin(target->GetAbsOrigin())
		}
		TaskComplete(false);                                             // 0x10286b84
		return 0;

	case TASK_FACE_SCRIPT:                                               // arm 0x56, 0x10286b96
		if (FElysiumEntity* Tgt = ResolveTargetEnt())                              // 0x10286ba0
		{
			StartTaskMotorHoldYaw();                                     // 0x10286bb2
			// `UTIL_AngleMod(target->GetAngles().y)` (`0x102885f0`, `0x10288590`) into the motor store
			// `0x10288670`.
			StartTaskMotorSetIdealYaw(StartTaskAngleMod(static_cast<float>(Tgt->Angles.Y))); // 0x10286bc0..0x10286bda
		}
		if (FElysiumScriptedSequence::ScriptStateOf(*this) != ScriptStateCustomMove) // 0x10286bdf
		{
			SetTurnActivity();                                           // 0x10286bec
		}
		StartTaskClearGoal();                                            // 0x10286bfb
		return 0;

	case TASK_WAIT_RANDOM:                                               // arm 0x57, 0x10283dae
		// `curtime + RandomFloat(0.1f, data)`, the literal first (`PUSH 0x3dcccccd`).
		BaseScheduleHost.WaitFinished = Now + RandomSeconds(Data);       // 0x10283dbf / 0x10283dcc
		return 0;

	case TASK_STOP_MOVING:                                               // arm 0x58, 0x10282d71
		if (!NavIsGoalActive())                                          // 0x10282d77
		{
			BaseScheduleHost.bShouldMove = false;                        // 0x10282dc5
			TaskComplete(false);                                         // 0x10282dcc
			return 0;
		}
		StartTaskClearGoal();                                            // 0x10282d86
		if (LookupPoseParameter(TEXT("move_yaw")) >= 0)                  // 0x10282d92
		{
			SetPoseParameter(TEXT("move_yaw"), 0.f, false);              // 0x10282dac  slot 345
			++StartTaskNav.PoseParameterZeroes;
		}
		// The active-goal path returns WITHOUT completing; RunTask carries it.
		return 0;

	case TASK_TURN_LEFT:                                                 // arm 0x59, 0x10282926
	case TASK_TURN_RIGHT:                                                // arm 0x5a, 0x10282848
	{
		StartTaskMotorHoldYaw();                                         // 0x1028292c / 0x1028284e
		const float Current = StartTaskAngleMod(static_cast<float>(Angles.Y)); // 0x10282935..0x10282958
		const float Turned = TaskId == TASK_TURN_LEFT ? Current + Data : Current - Data; // 0x10282962 FADD / 0x10282884 FSUB
		StartTaskMotorSetIdealYaw(StartTaskAngleMod(Turned));            // 0x1028296b / 0x1028288d, store 0x10283a52 / 0x1028289b
		SetTurnActivity();                                               // 0x102829aa / 0x102828ed
		return 0;
	}

	case TASK_REMEMBER:                                                  // arm 0x5b, 0x102829bd
		BaseScheduleHost.MemoryBits |= static_cast<uint32>(static_cast<int32>(Data)); // 0x102829cd
		TaskComplete(false);                                             // 0x102829d7
		return 0;

	case TASK_FORGET:                                                    // arm 0x5c, 0x102829e9
		BaseScheduleHost.MemoryBits &= ~static_cast<uint32>(static_cast<int32>(Data)); // 0x102829f9
		TaskComplete(false);                                             // 0x10282a05
		return 0;

	case TASK_WAIT_FOR_MOVEMENT:                                         // arm 0x5d, 0x10286749
		// `if (path+0x10) path+0x10 = 0` (`0x102ee2e0` / `0x102ee2c0`).
		if (NavIsGoalSet())                                              // 0x1028674f
		{
			++StartTaskNav.PathGoalFlagClears;                           // 0x1028675e
		}
		// `GetCurWaypoint()` (`0x102ee620` -> path+0x5c) non-null.
		if (!NavIsGoalActive())                                          // 0x10286769
		{
			BaseScheduleHost.bShouldMove = false;                        // 0x10286775
			TaskComplete(false);                                         // 0x1028677b
			StartTaskClearGoal();                                        // 0x10286786
			return 0;
		}
		if (NavIsGoalActive())                                           // 0x1028679e  0x102ee6a0
		{
			BaseScheduleHost.bShouldMove = true;                         // 0x102867cb
			ValidateNavGoal();                                           // 0x102867d2  slot 528
			return 0;
		}
		BaseScheduleHost.bShouldMove = false;                            // 0x102867a9
		SetIdealActivity(ResolveLinkActivity());                         // 0x102867af GetStoppedActivity 0x1027a6c0 / 0x102867b7
		return 0;

	case TASK_WAIT_FOR_MOVEMENT_STEP:                                    // arm 0x5e, 0x102866bf
		if (NavIsGoalSet())                                              // 0x102866c5
		{
			++StartTaskNav.PathGoalFlagClears;                           // 0x102866d4
		}
		if (!NavIsGoalActive())                                          // 0x102866df
		{
			BaseScheduleHost.bShouldMove = false;                        // 0x102866ec
			TaskComplete(false);                                         // 0x102866f2
			return 0;
		}
		if (IsActivityFinished())                                        // 0x10286706  slot 251
		{
			BaseScheduleHost.bShouldMove = false;                        // 0x10286714
			TaskComplete(false);                                         // 0x1028671b
			return 0;
		}
		BaseScheduleHost.bShouldMove = true;                             // 0x1028672f
		ValidateNavGoal();                                               // 0x10286736
		return 0;

	case TASK_WEAPON_FIND:                                               // arm 0x5f, 0x10286d78
	{
		const FVector Extents(WeaponFindExtentUnits, WeaponFindExtentUnits, WeaponFindExtentUnits); // 0x10286d78
		FElysiumEntity* Found = StartTaskWeaponFindUsable(Extents);      // 0x10286d9c  0x10333ad0
		SetTarget(Found != nullptr ? Found->Handle : FElysiumEntityHandle::Invalid()); // 0x10286da4  SetTarget
		if (ResolveTargetEnt() == nullptr)                                         // 0x10286dab
		{
			Fail(0xaca, FAIL_NO_WEAPON);                                 // 0x10286de2
			return 0;
		}
		TaskComplete(false);                                             // 0x10286db8
		return 0;
	}

	case TASK_WEAPON_PICKUP:                                             // arm 0x60, 0x10286df5
		SetIdealActivity(ACT_PICKUP_GROUND);                             // 0x10286df9
		return 0;

	case TASK_WEAPON_RUN_PATH:                                           // arm 0x61, 0x102864d7
		StartTaskSetMovementActivity(ACT_RUN);                           // 0x102864df
		return 0;

	case TASK_USE_SMALL_HULL:                                            // arm 0x62, 0x10286e0b
		SetHullSizeSmall(false);                                         // 0x10286e0f  0x10273180
		TaskComplete(false);                                             // 0x10286e18
		return 0;

	case TASK_GET_DROPSHIP_DEPLOY_PATH:                                  // arm 0x63, 0x10284e4a
	{
		// `GetVectors(&forward)`; `start = GetOrigin() + forward * 256`; `end = start - (0,0,500)`;
		// a ray `start -> end` under mask `0x46004003`, and the goal at the trace's end point.
		FVector Forward = FVector::ZeroVector;
		StartTaskAngleVectors(Angles, &Forward, nullptr);                // 0x10284e55  GetVectors 0x100ad320
		const FVector DropStartUnits = Origin / U + Forward * DropshipForwardUnits; // 0x10284e5a..0x10284ea5
		FVector DropEndUnits = DropStartUnits;
		DropEndUnits.Z -= DropshipDropUnits;                                 // 0x10284ece
		FKernelHullTrace Trace;
		Trace.EndPosUnits = DropEndUnits;
		KernelHullTrace(DropStartUnits, DropEndUnits, FVector::ZeroVector, FVector::ZeroVector, DropshipTraceMask, Trace); // 0x10284f35
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceKeep);
		Goal.DestCm = Trace.EndPosUnits * U;                             // 0x10284f77  tr.endpos
		Goal.bDestSet = true;
		if (StartTaskSetGoal(Goal, 2))                                   // 0x1028503f
		{
			TaskComplete(false);                                         // 0x1028504b
			TaskComplete(false);                                         // 0x10285053  TWICE
			return 0;
		}
		FailText(0x82f, TEXT("gah"), FAIL_TEXT_GAH);                     // 0x10285080
		TaskComplete(false);                                             // 0x10285089  and THEN complete (blocked by the failure)
		return 0;
	}

	case TASK_WANDER:                                                    // arm 0x64, 0x10286ec2
	{
		const int32 Packed = static_cast<int32>(Data);                   // 0x10286ec5
		const int32 MinPart = Packed / WanderSplit;                      // 0x10286ecc..0x10286edd  truncating
		const int32 MaxPart = Packed % WanderSplit;                      // 0x10286ee6..0x10286ef5
		if (StartTaskSetWanderGoal(static_cast<float>(MinPart), static_cast<float>(MaxPart))) // 0x10286f19
		{
			TaskComplete(false);                                         // 0x10286f26
			return 0;
		}
		Fail(0xb05, FAIL_NO_REACHABLE_NODE);                             // 0x10286f50
		return 0;
	}

	case TASK_FREEZE:                                                    // arm 0x65, 0x102869a6
		if (FElysiumNpc* Troika = AsNpc())
		{
			Troika->SequencePlaybackRate = 0.f;                          // 0x102869a6  m_flPlaybackRate = 0
		}
		return 0;

	case TASK_SET_MELEE_TOLERANCE_DISTANCE:                              // arm 0x66, 0x10286c9f
	{
		const FElysiumEntity* HeldWeapon = ActiveWeaponEntity();             // 0x10286ca1  GetActiveWeapon 0x1032e7b0
		if (HeldWeapon == nullptr)
		{
			Fail(0xab3, FAIL_NO_WEAPON);                                 // 0x10286d07
			return 0;
		}
		float Words[4] = { 0.f, 0.f, 0.f, 0.f };
		StartTaskWeaponRangeWords(*HeldWeapon, Words);
		// `(float)(int)(weapon->+0x8c0 * flTaskData)` — the FMUL at `0x10286cb7` the decompiler drops.
		const float MeleeTolerance = static_cast<float>(static_cast<int32>(Words[2] * Data)); // 0x10286cb1..0x10286cca
		SetGoalTolerance(MeleeTolerance);                                     // 0x10286cd4  0x102ee1c0
		TaskComplete(false);                                             // 0x10286cdd
		return 0;
	}

	case TASK_CHOOSE_BEST_MELEE_WEAPON:                                  // arm 0x67, 0x10286e2a
		if (StartTaskChooseBestMeleeWeapon())                            // 0x10286e2c  0x10337230
		{
			TaskComplete(false);                                         // 0x10286e39
			return 0;
		}
		Fail(0xae6, FAIL_NO_WEAPON_TO_CHOOSE);                           // 0x10286e63
		return 0;

	case TASK_CHOOSE_BEST_RANGED_WEAPON:                                 // arm 0x68, 0x10286e76
		if (StartTaskChooseBestRangedWeapon())                           // 0x10286e78  0x10337300
		{
			TaskComplete(false);                                         // 0x10286e85
			return 0;
		}
		Fail(0xaf1, FAIL_NO_WEAPON_TO_CHOOSE);                           // 0x10286eaf
		return 0;

	case TASK_GET_PATH_TO_PATHCORNER:                                    // arm 0x69, 0x10285ff8
	{
		if (Target.IsEmpty())                                            // 0x10285ff8  m_target (+0x20c)
		{
			// RETAIL DEFECT, reproduced: the scent arm's code and so its reason text.
			Fail(0x921, FAIL_NO_SCENT);                                  // 0x1028601c
			return 0;
		}
		const bool bFly = GetMoveType() == MoveTypeFly || GetMoveType() == MoveTypeFlyGravity; // 0x10286031 / 0x10286040
		// `m_pGoalEnt` (`+0x5de8`) — NOT the entity `m_target` names — read with no null test.
		FElysiumEntity* GoalEnt = World != nullptr ? World->Resolve(BaseScheduleHost.GoalEnt) : nullptr;
		if (GoalEnt == nullptr)
		{
			++StartTaskNav.CrashGuards;                                  // 0x10286059 named crash guard
			return 0;
		}
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_PATHCORNER, NavToleranceKeep);
		Goal.DestCm = GoalEnt->Origin;                                   // 0x10286059  slot 220 GetOrigin
		Goal.bDestSet = true;
		Goal.MovementActivity = bFly ? ACT_FLY : ACT_WALK;               // 0x10286092..0x102860c8  (-bFly & 0x19) + 9
		Goal.GoalFlags = 1;                                              // 0x102860d4
		if (!StartTaskSetGoal(Goal, 0))                                  // 0x102860f0
		{
			StartTaskDevMessage(TEXT("Can't Create Route!\n"));          // 0x10286104  DevWarning(2, ...)
		}
		// Neither path completes.
		return 0;
	}

	default:                                                             // arm 0x6a, 0x10286f63
		// `DevMsg("No StartTask entry for %s\n", TaskName(iTask))` (slot 449) and return WITHOUT
		// completing or failing: an unhandled task does not fail the schedule.
		StartTaskDevMessage(FString::Printf(TEXT("No StartTask entry for %s\n"), TaskName(TaskId))); // 0x10286f68 / 0x10286f74
		return 0;
	}
}

// =================================================================================================
// The navigator services `0x102827f0` drives
// =================================================================================================

bool FElysiumNpcBase::StartTaskSetGoal(const FStartTaskNavGoal& Goal, int32 SetGoalFlags)
{
	using namespace ElysiumStartTask19Base;

	// `CAI_Navigator::SetGoal` `0x102ecd20`, in its order.
	++StartTaskNav.SetGoalCalls;
	StartTaskNav.LastGoal = Goal;
	StartTaskNav.LastSetGoalFlags = SetGoalFlags;
	const float U = ElysiumMove::U;
	FElysiumNpc* Troika = AsNpc();

	// `this->vtable[0x1c]()` — navigator slot 7, the reset (`0x102eea70`).
	FUN_102eea70();
	// Flag 1 clears the path (`0x102f28a0`); flag 2 resets the path's target handle and dest words
	// (`path+0x30..+0x3c`), which this runtime's mover does not keep.
	if ((SetGoalFlags & 1) != 0 && Motor != nullptr)
	{
		Motor->ClearNavigationGoal();
	}
	// [5] -> `SetMovementActivity`.
	if (Goal.MovementActivity != INDEX_NONE)
	{
		StartTaskSetMovementActivity(Goal.MovementActivity);
	}

	// The tolerance (`path+0x28`): -2.0 the hull's, anything but -1.0 as given, -1.0 the path's own
	// unless that is 0.0, when the hull's (averaged with the goal entity's for entity goals) stands.
	FVector MinsUnits = FVector::ZeroVector;
	FVector MaxsUnits = FVector::ZeroVector;
	RetailHullExtents(HullKind, EElysiumHullExtents::Full, MinsUnits, MaxsUnits);
	const float HullWidthUnits = static_cast<float>(MaxsUnits.Y - MinsUnits.Y); // 0x102d61b0: row+0x18 - row+0xc
	FElysiumEntity* GoalEntity = nullptr;
	if (World != nullptr)
	{
		if (Goal.Type == GOALTYPE_TARGETENT)
		{
			GoalEntity = World->Resolve(TargetEnt);                       // owner +0x5ce4
		}
		else if (Goal.Type == GOALTYPE_ENEMY)
		{
			GoalEntity = static_cast<const FElysiumNpcBase*>(this)->GetEnemy(); // owner slot 167
		}
	}
	float ToleranceUnits = Goal.ToleranceUnits;
	if (Goal.ToleranceUnits == NavToleranceHull)
	{
		ToleranceUnits = HullWidthUnits;
	}
	else if (Goal.ToleranceUnits == NavToleranceKeep)
	{
		ToleranceUnits = Troika != nullptr ? Troika->ScheduleHost.GoalToleranceCm / U : 0.f;
		if (ToleranceUnits == 0.f)
		{
			ToleranceUnits = HullWidthUnits;
			const FElysiumNpcBase* GoalNpc = GoalEntity != nullptr ? GoalEntity->AsNpcBase() : nullptr;
			if (GoalNpc != nullptr)
			{
				FVector OtherMins = FVector::ZeroVector;
				FVector OtherMaxs = FVector::ZeroVector;
				RetailHullExtents(GoalNpc->HullKind, EElysiumHullExtents::Full, OtherMins, OtherMaxs);
				ToleranceUnits = (static_cast<float>(OtherMaxs.Y - OtherMins.Y) + HullWidthUnits)
					* ElysiumNpcTunables::Half;
			}
		}
	}
	SetGoalTolerance(ToleranceUnits);
	StartTaskNav.LastSetGoalToleranceUnits = ToleranceUnits;

	// The destination: an entity goal reads the entity (the goal's own dest overrides it when set);
	// a location goal reads its dest; the default triple with no node names nothing to go to.
	bool bHaveDest = false;
	FVector DestCm = FVector::ZeroVector;
	if (Goal.bDestSet)
	{
		DestCm = Goal.DestCm;
		bHaveDest = true;
	}
	else if (GoalEntity != nullptr)
	{
		DestCm = GoalEntity->Origin;
		bHaveDest = true;
	}
	StartTaskNav.LastSetGoalDestCm = DestCm;

	// The route (`0x102f1dc0`). The port's navigator is `IElysiumNpcMotor`; the Troika line's body
	// arbitration (`AcquireScheduleBody`) is the port's condition for commanding it.
	bool bRoute = false;
	if (bHaveDest && Motor != nullptr
		&& (Troika == nullptr || Troika->AcquireScheduleBody(TEXT("SetGoal 0x102ecd20"))))
	{
		const int32 MoveActivity = Troika != nullptr ? Troika->ScheduleHost.NavigationActivity : INDEX_NONE;
		const EElysiumNpcGaitKind Gait = MoveActivity == ACT_RUN ? EElysiumNpcGaitKind::Run : EElysiumNpcGaitKind::Walk;
		bRoute = Motor->MoveTo(DestCm, ToleranceUnits * U, ElysiumNpcGait::TravelSpeed(Motor, Gait),
			/*bAllowPartialPath=*/false, Gait);
	}
	bMoveIssued = bRoute;
	if (Troika != nullptr && bRoute)
	{
		Troika->MoveGoal = DestCm;
	}
	StartTaskNav.bLastSetGoalResult = bRoute;

	if (!bRoute)
	{
		// `0x102f1dc0` with `m_afMemory & 0x20` clear: a zero route search time fails the task at
		// once through `OnNavFailed(0xc)`; otherwise the bit is set and the search deferred (the
		// retry is the route builder's re-entry, unrecovered for this runtime's mover).
		if (NavRouteSearchTime == ElysiumNpcTunables::Zero)
		{
			NavOnNavFailed(FAIL_NO_ROUTE);
		}
		else
		{
			BaseScheduleHost.MemoryBits |= MemoryPathFailed;
		}
		// Flag 4: a refused goal clears the path (`0x102f28a0`).
		if ((SetGoalFlags & 4) != 0 && Motor != nullptr)
		{
			Motor->ClearNavigationGoal();
		}
		return false;
	}
	// A built route clears the deferred bit and, unless slot 529 answers a continuous move, the
	// navigator's own slot 2 completes the running task.
	BaseScheduleHost.MemoryBits &= ~MemoryPathFailed;
	if (!IsCurTaskContinuousMove())
	{
		MotorTaskComplete(false);
	}
	// Goal flag bit 0: face the path (`0x102e0b40`, `0x102e2020` toward the route).
	if ((Goal.GoalFlags & 1) != 0)
	{
		StartTaskMotorHoldYaw();
		StartTaskMotorSetIdealYawToTarget(DestCm);
	}
	// [6] / [7] / the default arrival activity 1.
	if (Goal.ArrivalActivity != INDEX_NONE)
	{
		StartTaskSetArrivalActivity(Goal.ArrivalActivity);
	}
	else if (Goal.ArrivalSequence == INDEX_NONE)
	{
		StartTaskSetArrivalActivity(ACT_IDLE);
	}
	return true;
}

void FElysiumNpcBase::StartTaskClearGoal()
{
	// `0x102ee270`: the path clear then the navigator's slot 7 reset. The port's mover clears its
	// route and keeps any special traversal (`ClearNavigationGoal`).
	++StartTaskNav.ClearGoalCalls;
	if (Motor != nullptr)
	{
		Motor->ClearNavigationGoal();
	}
	bMoveIssued = false;
	FUN_102eea70();
}

void FElysiumNpcBase::StartTaskSetMovementActivity(int32 Activity)
{
	// `0x102ee250`: `m_pPath->m_movementActivity (+0x2c) = activity`. The word lives on the Troika's
	// host record in this runtime (`FElysiumNpcScheduleHost::NavigationActivity`); the gait is
	// pushed on to the route in flight (`SetTravelGait`), never a rebuild.
	++StartTaskNav.MovementActivitySets;
	StartTaskNav.LastMovementActivity = Activity;
	if (FElysiumNpc* Troika = AsNpc())
	{
		Troika->ScheduleHost.NavigationActivity = Activity;
	}
	if (Motor != nullptr)
	{
		const EElysiumNpcGaitKind Gait = Activity == ElysiumStartTask19Base::ACT_RUN
			? EElysiumNpcGaitKind::Run : EElysiumNpcGaitKind::Walk;
		Motor->SetTravelGait(Gait, ElysiumNpcGait::TravelSpeed(Motor, Gait));
	}
}

void FElysiumNpcBase::StartTaskSetArrivalActivity(int32 Activity)
{
	++StartTaskNav.ArrivalActivitySets;
	StartTaskNav.LastArrivalActivity = Activity;
}

void FElysiumNpcBase::StartTaskSetArrivalDirection(const FVector& Direction)
{
	// `0x102ee530` — `path->SetArrivalDirection(vector)` (`0x1000745a`).
	++StartTaskNav.ArrivalDirectionSets;
	StartTaskNav.LastArrivalDirection = Direction;
	StartTaskNav.bLastArrivalDirectionIsAngles = false;
}

void FElysiumNpcBase::StartTaskSetArrivalDirectionAngles(const FVector& ArrivalAngles)
{
	// `0x102ee550` — `AngleVectors` the angles (`0x1000f493`) and hand the vector to the same path
	// setter. Recorded as the angles retail was given.
	++StartTaskNav.ArrivalDirectionSets;
	StartTaskNav.LastArrivalDirection = ArrivalAngles;
	StartTaskNav.bLastArrivalDirectionIsAngles = true;
}

FVector FElysiumNpcBase::StartTaskCurWaypointPos() const
{
	const FElysiumNpc* Troika = AsNpc();
	if (bMoveIssued && Troika != nullptr)
	{
		return Troika->MoveGoal;
	}
	return Origin;
}

void FElysiumNpcBase::StartTaskMotorHoldYaw()
{
	++StartTaskNav.MotorYawHolds;
}

void FElysiumNpcBase::StartTaskMotorSetIdealYaw(float Yaw)
{
	// `0x10288670`: the `+0x28` flip — `+180` below 180, `-180` at or above — then the `+0x1c`
	// sentinel store into `+0x34`.
	float Stored = Yaw;
	if (BaseScheduleHost.bMotorAnimationMovement)
	{
		if (Stored < ElysiumNpcTunables::OneEighty)
		{
			Stored += ElysiumNpcTunables::OneEighty;
		}
		else
		{
			Stored -= ElysiumNpcTunables::OneEighty;
		}
	}
	MotorIdealYaw = Stored;
}

void FElysiumNpcBase::StartTaskMotorSetIdealYawToTarget(const FVector& TargetCm)
{
	StartTaskMotorSetIdealYaw(CalcIdealYaw(TargetCm));
}

float FElysiumNpcBase::StartTaskAngleMod(float Yaw)
{
	const int32 Quantised = static_cast<int32>(Yaw * ElysiumNpcTunables::AngleQuantumInverse) & 0xffff;
	return static_cast<float>(Quantised) * ElysiumNpcTunables::AngleQuantum;
}

void FElysiumNpcBase::StartTaskAngleVectors(const FVector& SourceAngles, FVector* OutForward, FVector* OutRight)
{
	const double PitchRad = FMath::DegreesToRadians(SourceAngles.X);
	const double YawRad = FMath::DegreesToRadians(SourceAngles.Y);
	const double RollRad = FMath::DegreesToRadians(SourceAngles.Z);
	const double SP = FMath::Sin(PitchRad);
	const double CP = FMath::Cos(PitchRad);
	const double SY = FMath::Sin(YawRad);
	const double CY = FMath::Cos(YawRad);
	const double SR = FMath::Sin(RollRad);
	const double CR = FMath::Cos(RollRad);
	// Source's forward (cp*cy, cp*sy, -sp) and right (-sr*sp*cy + cr*sy, -sr*sp*sy - cr*cy, -sr*cp),
	// each with its Y reflected into this runtime's position space.
	if (OutForward != nullptr)
	{
		*OutForward = FVector(CP * CY, -(CP * SY), -SP);
	}
	if (OutRight != nullptr)
	{
		*OutRight = FVector(-SR * SP * CY + CR * SY, SR * SP * SY + CR * CY, -SR * CP);
	}
}

void FElysiumNpcBase::StartTaskWeaponRange(float& OutMinUnits, float& OutMaxUnits) const
{
	using namespace ElysiumStartTask19Base;
	// Retail calls `GetActiveWeapon()` once per field read — eight calls; one read here.
	OutMaxUnits = WeaponMaxRangeUnarmed;
	OutMinUnits = ElysiumNpcTunables::Zero;
	if (const FElysiumEntity* HeldWeapon = ActiveWeaponEntity())
	{
		float Words[4] = { 0.f, 0.f, 0.f, 0.f };   // +0x8b8, +0x8bc, +0x8c0, +0x8c4
		StartTaskWeaponRangeWords(*HeldWeapon, Words);
		OutMaxUnits = Words[3] >= Words[2] ? Words[3] : Words[2];   // TEST AH,5 / JP: ties to +0x8c4
		OutMinUnits = Words[1] <= Words[0] ? Words[1] : Words[0];   // AND 0x4100 / JNZ: ties to +0x8bc
	}
	// `max = min(max, m_flDistTooFar)`: keep when below or equal (`AND 0x4100 / JNZ`).
	if (!(OutMaxUnits <= DistTooFar))
	{
		OutMaxUnits = DistTooFar;
	}
}

bool FElysiumNpcBase::StartTaskWeaponRangeWords(const FElysiumEntity& Weapon, float OutWords[4]) const
{
	(void)Weapon;
	(void)OutWords;
	return false;
}

bool FElysiumNpcBase::StartTaskFindCoverPos(const FVector& ThreatCm, const FVector& ThreatEyeCm, float MinUnits,
	float MaxUnits, FVector& OutCm)
{
	++StartTaskNav.CoverSearches;
	StartTaskNav.LastSearchThreatCm = ThreatCm;
	StartTaskNav.LastSearchThreatEyeCm = ThreatEyeCm;
	StartTaskNav.LastSearchMinUnits = MinUnits;
	StartTaskNav.LastSearchMaxUnits = MaxUnits;
	if (Motor == nullptr)
	{
		return false;
	}
	return Motor->FindNodeCover(ThreatCm, ThreatEyeCm, MaxUnits * ElysiumMove::U, OutCm);
}

bool FElysiumNpcBase::StartTaskFindLosPos(const FVector& ThreatCm, const FVector& ThreatEyeCm, float MinUnits,
	float MaxUnits, FVector& OutCm)
{
	++StartTaskNav.LosSearches;
	StartTaskNav.LastSearchThreatCm = ThreatCm;
	StartTaskNav.LastSearchThreatEyeCm = ThreatEyeCm;
	StartTaskNav.LastSearchMinUnits = MinUnits;
	StartTaskNav.LastSearchMaxUnits = MaxUnits;
	(void)OutCm;
	return false;
}

bool FElysiumNpcBase::StartTaskSetRandomGoal(float DistanceUnits, const FVector& Direction)
{
	++StartTaskNav.RandomGoalRequests;
	StartTaskNav.LastSearchMaxUnits = DistanceUnits;
	(void)Direction;
	return false;
}

bool FElysiumNpcBase::StartTaskSetWanderGoal(float MinUnits, float MaxUnits)
{
	++StartTaskNav.WanderGoalRequests;
	StartTaskNav.LastSearchMinUnits = MinUnits;
	StartTaskNav.LastSearchMaxUnits = MaxUnits;
	return false;
}

bool FElysiumNpcBase::StartTaskFindLateralCover(const FVector& ThreatEyeCm, const FElysiumEntity* Ignore)
{
	using namespace ElysiumStartTask19Base;
	// `0x102784a0`: the origin first.
	if (StartTaskTestLateralCover(ThreatEyeCm, Origin, Ignore))
	{
		return true;
	}
	// `AngleVectors(GetAngles(), NULL, &right, NULL)`, 2-D, scaled by 48 units; left (origin -
	// step*i) is tested before right (origin + step*i), five times.
	FVector Right = FVector::ZeroVector;
	StartTaskAngleVectors(Angles, nullptr, &Right);
	const FVector Step(Right.X * LateralCoverStepUnits * ElysiumMove::U,
		Right.Y * LateralCoverStepUnits * ElysiumMove::U, 0.0);
	FVector Left = Origin;
	FVector RightPoint = Origin;
	for (int32 Index = 0; Index < LateralCoverSteps; ++Index)
	{
		Left -= Step;
		RightPoint += Step;
		if (StartTaskTestLateralCover(ThreatEyeCm, Left, Ignore)
			|| StartTaskTestLateralCover(ThreatEyeCm, RightPoint, Ignore))
		{
			return true;
		}
	}
	return false;
}

bool FElysiumNpcBase::StartTaskTestLateralCover(const FVector& ThreatEyeCm, const FVector& PointCm,
	const FElysiumEntity* Ignore)
{
	using namespace ElysiumStartTask19Base;
	(void)Ignore;   // the trace filter's ignore entity; the port's sight service takes none
	++StartTaskNav.LateralCoverTests;
	// The ray from the threat's eye to the point at THIS NPC's eye height, mask `0x2804091`: a clear
	// ray (`fraction == 1.0`) is not cover. No collision world means no cover.
	IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr || Embodiment->QueryLineOfSight(ThreatEyeCm, PointCm + (EyePosition() - Origin)))
	{
		return false;
	}
	if (!IsValidCover(PointCm, nullptr))                                 // slot 548
	{
		return false;
	}
	if (Motor == nullptr || !Motor->CanReachLateralCover(PointCm))       // MoveLimit, mask 0x202400b
	{
		return false;
	}
	FStartTaskNavGoal Goal;
	Goal.Type = GOALTYPE_LOCATION;
	Goal.DestCm = PointCm;
	Goal.bDestSet = true;
	Goal.MovementActivity = ACT_RUN;
	Goal.ToleranceUnits = NavToleranceKeep;                              // `_DAT_104994a0` = -1.0
	return StartTaskSetGoal(Goal, 1);
}

FVector FElysiumNpcBase::StartTaskLastKnownPosition(const FElysiumEntity* Subject)
{
	// `0x102dfed0` over the memory list: the entity's record; else the last position-only record
	// (`+0x34 == 1`) with its DevWarning; else `vec3_origin` with the other.
	if (Subject != nullptr)
	{
		if (const FElysiumNpcEnemyMemoryRecord* Found = EnemyMemory.Find(Subject->Handle))
		{
			return Found->LastPosition;
		}
	}
	const FElysiumNpcEnemyMemoryRecord* PositionOnly = nullptr;
	for (const FElysiumNpcEnemyMemoryRecord& MemoryRecord : EnemyMemory.Records())
	{
		if (MemoryRecord.bPositionOnly)
		{
			PositionOnly = &MemoryRecord;
		}
	}
	const FString SubjectName = Subject != nullptr ? Subject->DebugString() : FString(TEXT("(NULL)"));
	if (PositionOnly != nullptr)
	{
		StartTaskDevMessage(FString::Printf(
			TEXT("Asking LastKnownPosition for enemy that's not in my memory!! (%s) — position-only record\n"), *Name));
		return PositionOnly->LastPosition;
	}
	StartTaskDevMessage(FString::Printf(
		TEXT("Asking LastKnownPosition for enemy that's not in my memory!! (%s)\n"), *Name));
	return FVector::ZeroVector;
}

int32 FElysiumNpcBase::StartTaskFlinchActivity()
{
	using namespace ElysiumStartTask19Base;
	// `0x10265970`. It opens with `AngleVectors(GetAngles(), &forward)` whose result nothing reads.
	int32 Activity = ACT_SMALL_FLINCH;
	switch (LastHitGroup)                                                // +0x1594
	{
	case 1: Activity = 0x66; break;
	case 3: Activity = 0x68; break;
	case 4: Activity = 0x69; break;
	case 5: Activity = 0x6a; break;
	case 6: Activity = 0x6b; break;
	case 7: Activity = 0x6c; break;
	default: break;
	}
	return SelectWeightedSequenceForActivity(Activity) != INDEX_NONE ? Activity : ACT_SMALL_FLINCH;
}

int32 FElysiumNpcBase::StartTaskActivityOperand(const FElysiumScheduleStep& Step) const
{
	const FString* Name = FElysiumScheduleCorpus::Get().Activities().NameOf(static_cast<int32>(Step.Data));
	if (Name == nullptr)
	{
		return static_cast<int32>(Step.Data);
	}
	return ActivityIdForName(*Name);
}

bool FElysiumNpcBase::StartTaskHintYaw(int32 HintNode, float& OutYaw) const
{
	// `0x102d12e0`: a hint bound to a network node answers the node's yaw (`0x10010258` on the
	// network `DAT_1093407c`, which family Hints seams as `HintYaw`); an unbound one answers its own
	// `GetAngles().y` (slot 221).
	FHintWords Words;
	if (!HintWords(HintNode, Words))
	{
		return false;
	}
	if (Words.NodeId != INDEX_NONE)
	{
		return HintYaw(HintNode, OutYaw);
	}
	OutYaw = static_cast<float>(Words.Angles.Y);
	return true;
}

bool FElysiumNpcBase::StartTaskHintFacing(int32 HintNode, FVector& OutDirection) const
{
	// `0x102d11f0`: `AngleVectors` of the yaw above (`0x1000ecd2` over a (0, yaw, 0) angle).
	float Yaw = 0.f;
	const bool bKnown = StartTaskHintYaw(HintNode, Yaw);
	StartTaskAngleVectors(FVector(0.0, Yaw, 0.0), &OutDirection, nullptr);
	return bKnown;
}

FElysiumEntity* FElysiumNpcBase::StartTaskWeaponFindUsable(const FVector& ExtentsUnits)
{
	++StartTaskNav.WeaponSearches;
	(void)ExtentsUnits;
	return nullptr;
}

bool FElysiumNpcBase::StartTaskChooseBestMeleeWeapon()
{
	return false;
}

bool FElysiumNpcBase::StartTaskChooseBestRangedWeapon()
{
	return false;
}

void FElysiumNpcBase::StartTaskDevMessage(const FString& Message)
{
	++StartTaskNav.DevMessages;
	StartTaskNav.LastDevMessage = Message;
	RecordScheduleEvent(Message);
}
