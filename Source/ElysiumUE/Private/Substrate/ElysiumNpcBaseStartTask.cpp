// Story 0019/8 (29e under the strict verdict), family **StartTask19** -- `CAI_BaseNPC`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcBaseStartTask.inl` (included inside `class FElysiumNpcBase`) or
// generated in `ElysiumNpcBaseSlots.inl` for a slot body.
//
// Owns (StartTask19's `rule` rows): 0x102827f0 CAI_BaseNPC::StartTask.
//
// Lane L03 (pass I): the body is `CAI_BaseNPC::StartTask` `0x102827f0`, 18330 bytes, read off the
// listing (`uv run elysium --verbose research corpus asm 0x102827f0`) arm by arm in the order of its
// arm table `0x10286f8c` (107 entries, reached through the byte table `0x10287138` indexed `iTask-1`).
// The walked account is
// `docs/vtmb/npc-ai/schedule-kernel.md` § "`CAI_BaseNPC::StartTask` `0x102827f0`".
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
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumItemTable.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcEngineRandom.h"
#include "Substrate/ElysiumNpcGait.h"
#include "Substrate/ElysiumNpcSightTrace.h"
#include "Substrate/ElysiumPlaceSet.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumScheduleCorpus.h"
#include "Substrate/ElysiumScriptedSequence.h"
#include "Substrate/ElysiumWeaponClasses.h"   // ElysiumWeapons::ItemRangeWords — the four range words

namespace ElysiumStartTask19Base
{
	// The retail source file every trace in this body names (`0x105cde88`, pre-loaded into EDI at
	// `0x10282816`).
	const TCHAR* const File = TEXT("AI_BaseNPC_Schedule.cpp");

	// --- Task ids: the registrar `FUN_10316ff0`'s numbers, retail's class-local `iTask` (the step's global id translated) ---
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
	constexpr int32 GOALTYPE_BESTSEEUNKNOWN = 7;
	// 8 interesting place (pedestrian; sets the pedestrian byte `path+0x1`), 9 interesting place (animal).
	constexpr int32 GOALTYPE_PLACE_PEDESTRIAN = 8;
	// `0x1049a160` (-1.0, read 2026-09-27 off the image) — keep the path's tolerance.
	constexpr float NavToleranceKeep = ElysiumNpcTunables::StartTaskToleranceKeep;
	// `0x1049a164` (-2.0) — the hull's tolerance.
	constexpr float NavToleranceHull = ElysiumNpcTunables::StartTaskToleranceHull;

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
	constexpr float LateralCoverStepUnits = ElysiumNpcTunables::FortyEight;       // `0x10447ee8`
	constexpr int32 LateralCoverSteps = 5;               // `0x102784a0`'s `4 < i` exit
	constexpr int32 LateralCoverSightMask = 0x2804091;   // `0x10278220`'s line (R2 §5)
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
	// `[EDI]` is retail's `Task_t::iTask`, the CLASS-LOCAL id (the parser `0x1030d850` stores
	// `0x102ea280(space+0x18, global)`). This runtime's step carries the GLOBAL id (stated divergence,
	// `ElysiumScheduleText.h`), so it is translated here through this class's task space -- the body
	// of slot 450 `GetLocalTaskId` (`0x101a6640`), which no class overrides. For the root and Troika
	// spaces the local id is the registrar's number the arms compare.
	const int32 TaskId = GlobalToLocalId(IdSpace(EElysiumIdCategory::Task), Step->TaskId); // 0x10282803
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
	// The hint follow-up the cover arms share (`0x10283168..0x102831ab`): when `m_pHintNode` is 0x10283170 0x10283181 0x1028318a 0x102831a3
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

	// `0x10282806`: `(iTask - 1) > 0x11f` unsigned -> the default arm; otherwise the byte table. 0x1028280e 0x10282821
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
		WriteIdealStateRetail(static_cast<int32>(Data));                 // 0x10286c2d  m_IdealNPCState (+0x5cc4) 0x10286c24
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
		SetTarget(Player->Handle);                                       // 0x10283ef0  SetTarget 0x10279cc0 0x10283eed
		TaskComplete(false);                                             // 0x10283ef9
		return 0;
	}

	case TASK_WALK_TO_TARGET:                                            // arm 0x06, 0x10283f36 0x10283f41
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
			Fail(0x686, FAIL_NO_TARGET);                                 // 0x10283f85 0x10283f61 0x10283f69 0x10283f98 0x10283faf 0x10283fc1
			ArrivalTail();
			return 0;
		}
		// `|target->GetAbsOrigin() - GetOrigin()| < 1.0` (`0x104454c0`), Source units.
		if ((Tgt->Origin - Origin).Size() < ElysiumNpcTunables::One * U) // 0x10284005 0x10283fcf 0x10283fff
		{
			TaskComplete(false);                                         // 0x10284019 0x10284013
			ArrivalTail();
			return 0;
		}
		int32 Activity = ACT_WALK;                                       // 0x1028402b  task 8 0x10284029
		if (TaskId == TASK_RUN_TO_TARGET)
		{
			Activity = ACT_RUN;                                          // 0x10284037 0x10284035
		}
		else if (TaskId == TASK_SCRIPT_CUSTOM_MOVE_TO_TARGET)
		{
			Activity = GetScriptCustomMoveActivity();                    // 0x10284040  0x10289fe0
		}
		// `SelectWeightedSequence(act, -1)` (`0x1008dc40`), skipped only for the custom-move answer.
		if (Activity != ACT_SCRIPT_CUSTOM_MOVE && SelectWeightedSequenceForActivity(Activity) == INDEX_NONE) // 0x10284050 0x1028404a / 0x1008dc40 common stream
		{
			TaskComplete(false);                                         // 0x1028405d 0x10284057
			ArrivalTail();
			return 0;
		}
		if (ResolveTargetEnt() == nullptr)                                         // 0x10284067  re-tested 0x1028406f
		{
			Fail(0x6a7, FAIL_NO_TARGET);                                 // 0x102840b0 0x1028408c 0x10284094
			ArrivalTail();
			return 0;
		}
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_TARGETENT, NavToleranceKeep); // 0x10284109
		Goal.MovementActivity = Activity;                                // 0x10284115  [5]
		Goal.Target = TargetEnt;                                         // 0x10284121  [10] = the resolved target
		// Only in `NPC_STATE_SCRIPT` (slot 464 == 4): a pushed arrival activity goes to [6]; failing
		// that, a pushed sequence NAME goes through `LookupSequence` into [7].
		if (GetState() == EElysiumNpcState::Scripted)                    // 0x10284135 0x1028413e
		{
			if (ScriptArrivalActivity != INDEX_NONE)                     // 0x10284148
			{
				Goal.ArrivalActivity = ScriptArrivalActivity;            // 0x10284156 0x10284154
			}
			else if (!ScriptArrivalSequence.IsEmpty())                   // 0x10284150
			{
				Goal.ArrivalSequence = LookupSequenceByName(*ScriptArrivalSequence); // 0x1028416e 0x10284164
			}
		}
		if (!StartTaskSetGoal(Goal, 4))                                  // 0x10284184  SetGoal(.., 4) 0x1028418b
		{
			Fail(0x6be, FAIL_NO_ROUTE);                                  // 0x102841a7 0x102841b7
			ArrivalTail();
			return 0;
		}
		// `SetArrivalDirection(target->GetAbsAngles())` (`0x102ee550`). Retail re-resolves the handle
		// and dereferences it without a test (`0x102841da`); a target lost inside `SetGoal` is the 0x102841d4
		// named crash guard.
		if (FElysiumEntity* Again = ResolveTargetEnt())
		{
			StartTaskSetArrivalDirectionAngles(Again->Angles); // 0x102841ed 0x102841e4
		}
		else
		{
			++StartTaskNav.CrashGuards;
		}
		ArrivalTail();
		return 0;
	}

	case TASK_MOVE_TO_TARGET_RANGE:                                      // arm 0x07, 0x10283dde 0x10283de9
	{
		FElysiumEntity* Tgt = ResolveTargetEnt();
		if (Tgt == nullptr)
		{
			Fail(0x669, FAIL_NO_TARGET);                                 // 0x10283e2b 0x10283e09 0x10283e0f 0x10283e46 0x10283e5d 0x10283e69
			return 0;
		}
		if ((Tgt->Origin - Origin).Size() < ElysiumNpcTunables::One * U) // 0x10283eab 0x10283e75 0x10283ea5
		{
			TaskComplete(false);                                         // 0x10283ec3 0x10283eb9
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
		Goal.DestCm = Origin + Forward * (DistanceUnits * U);            // 0x102861a5 0x1028619f
		Goal.bDestSet = true;
		Goal.MovementActivity = ACT_WALK;                                // 0x102861ce
		if (StartTaskSetGoal(Goal, 0))                                   // 0x1028627e
		{
			TaskComplete(false);                                         // 0x1028628a 0x10286287 0x1028629e
			return 0;
		}
		// `FindCoverPos(GetOrigin(), EyePosition(), 0.0, CoverRadius(), &out)`.
		FVector Cover = FVector::ZeroVector;
		if (!StartTaskFindCoverPos(Origin, EyePosition(), 0.f, CoverRadius(), Cover)) // 0x102862dd 0x102862ba 0x102862d1 0x102862e4
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
		// jumps straight to the completion (`0x102847b2 JA 0x10284ad2`). 0x102847b8
		const int32 Mode = static_cast<int32>(Data);                     // 0x102847a6
		switch (Mode)
		{
		case 0:                                                          // 0x102847bf  the enemy 0x102847c3 0x102847cd
		{
			FElysiumEntity* E = Enemy();
			if (E == nullptr)
			{
				Fail(0x75f, FAIL_NO_ENEMY);                              // 0x102847e7
				return 0;
			}
			BaseScheduleHost.StoredPathGoal = E->Origin;                 // 0x1028480e  +0x5df8 0x102847fc 0x10284806
			BaseScheduleHost.StoredPathType = GOALTYPE_ENEMY;            // 0x1028482a  +0x5e04
			BaseScheduleHost.StoredPathFlags = 0;                        // 0x10284834  +0x5e08
			FElysiumEntity* Again = Enemy();                             // 0x1028483e
			BaseScheduleHost.StoredPathTarget = Again != nullptr         // 0x10284abf  +0x5df4
				? Again->Handle : FElysiumEntityHandle::Invalid();
			break;
		}
		case 1:                                                          // 0x102848bf  the target 0x102848ca
		{
			FElysiumEntity* Tgt = ResolveTargetEnt();
			if (Tgt == nullptr)
			{
				Fail(0x77f, FAIL_NO_TARGET);                             // 0x1028490b 0x102848ea 0x102848ef 0x10284926
				return 0;
			}
			BaseScheduleHost.StoredPathGoal = Tgt->Origin;               // 0x1028494f 0x1028493d 0x10284947
			BaseScheduleHost.StoredPathType = GOALTYPE_TARGETENT;        // 0x10284967
			BaseScheduleHost.StoredPathFlags = 0;                        // 0x10284971 0x10284983
			BaseScheduleHost.StoredPathTarget = TargetEnt;               // 0x102849a4 0x102849a0
			break;
		}
		case 2:                                                          // 0x10284849  the enemy LKP 0x1028484d 0x10284857
		{
			FElysiumEntity* E = Enemy();
			if (E == nullptr)
			{
				Fail(0x76f, FAIL_NO_ENEMY);                              // 0x10284871 0x10284886
				return 0;
			}
			BaseScheduleHost.StoredPathGoal = Conditions19LastKnownPosition(Enemy()); // 0x102848a1 0x10284899
			BaseScheduleHost.StoredPathType = GOALTYPE_LOCATION;         // 0x10284ab3
			BaseScheduleHost.StoredPathFlags = 0;                        // 0x10284ab9
			BaseScheduleHost.StoredPathTarget = FElysiumEntityHandle::Invalid(); // 0x10284abf
			break;
		}
		case 3:                                                          // 0x102849bc  the TARGET, in the ENEMY memory 0x102849c7
		{
			FElysiumEntity* Tgt = ResolveTargetEnt();
			if (Tgt == nullptr)
			{
				Fail(0x78f, FAIL_NO_TARGET);                             // 0x10284a0b 0x102849e7 0x102849ef 0x10284a26
				return 0;
			}
			BaseScheduleHost.StoredPathGoal = Conditions19LastKnownPosition(Tgt); // 0x10284a5a 0x10284a3d 0x10284a52
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
		// The route tail `0x10284e2c`: a true `SetGoal` completes, a false one fails with `0xc`. 0x10284e2e
		auto RouteTail = [this, &Fail](bool bRoute)
		{
			if (bRoute)
			{
				TaskComplete(false);                                     // 0x10284e38
				return;
			}
			Fail(0x816, ElysiumStartTask19Base::FAIL_NO_ROUTE);          // 0x10284c74
		};
		switch (static_cast<int32>(Data))                                // 0x10284b64 0x10284b6b
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
			StartTaskWeaponRange(MinUnits, MaxUnits);                    // 0x10284c89..0x10284d53 0x10284c99 0x10284ca0 0x10284ca8 0x10284cb9 0x10284ccf 0x10284cd1 0x10284cde 0x10284cef 0x10284d00 0x10284d18 0x10284d1a 0x10284d27 0x10284d47 0x10284d5b 0x10284d67
			const FVector Aim = Stored != nullptr ? Stored->EyePosition() : BaseScheduleHost.StoredPathGoal; // 0x10284d53
			FVector Los = FVector::ZeroVector;
			if (!StartTaskFindLosPos(BaseScheduleHost.StoredPathGoal, Aim, MinUnits, MaxUnits, Los)) // 0x10284db9 0x10284dc0
			{
				Fail(0x7de, FAIL_NO_SHOOT);                              // 0x10284dea
				return 0;
			}
			Goal.DestCm = Los;                                           // 0x10284e0f
			Goal.bDestSet = true;
			RouteTail(StartTaskSetGoal(Goal, 0));
			return 0;
		}
		case 2:                                                          // 0x10284b7f  cover from the stored entity 0x10284b72 0x10284b79 0x10284b81
		{
			const FElysiumEntity* Threat = Stored != nullptr ? Stored : this; // 0x10284b83
			if (StartTaskFindLateralCover(Threat->EyePosition(), Threat)) // 0x10284b9b 0x10284b92 0x10284ba2
			{
				BaseScheduleHost.MoveWaitFinished = Now + Data;          // 0x10284bb4
				TaskComplete(false);                                     // 0x10284bba 0x10284bdc
				return 0;
			}
			FVector Cover = FVector::ZeroVector;
			if (StartTaskFindCoverPos(Threat->Origin, Threat->EyePosition(), 0.f, CoverRadius(), Cover)) // 0x10284c08 0x10284bf4 0x10284bff 0x10284c0f
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

	case TASK_GET_PATH_TO_ENEMY:                                         // arm 0x0b, 0x1028509b 0x1028509f
	{
		// `IsUnreachable(GetEnemy())` FIRST, with a possibly-null enemy (slot 530).
		if (IsUnreachable(Enemy()))                                      // 0x102850a8 0x102850b2
		{
			Fail(0x83a, FAIL_NO_ROUTE);                                  // 0x102850cc
			return 0;
		}
		if (Enemy() == nullptr)                                          // 0x102850e1 0x102850e9
		{
			Fail(0x842, FAIL_NO_ENEMY);                                  // 0x10285105
			return 0;
		}
		const FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_ENEMY, NavToleranceKeep); // 0x102851a6
		if (StartTaskSetGoal(Goal, 0))                                   // 0x102851cd
		{
			TaskComplete(false);                                         // 0x102851d9 0x102851d4
			return 0;
		}
		StartTaskDevMessage(TEXT("GetPathToEnemy failed!!\n"));          // 0x102851f2  DevWarning(2, ...)
		RememberUnreachable(Enemy());                                    // 0x10285208  0x10274080 0x102851ff
		Fail(0x84f, FAIL_NO_ROUTE);                                      // 0x10285227
		return 0;
	}

	case TASK_GET_PATH_TO_ENEMY_LKP:                                     // arm 0x0c, 0x10284349
	{
		FElysiumEntity* E = Enemy();                                     // 0x1028434d
		if (IsUnreachable(E))                                            // 0x1028435a 0x10284364
		{
			Fail(0x712, FAIL_NO_ROUTE);                                  // 0x1028437e
			return 0;
		}
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceKeep);
		Goal.DestCm = Conditions19LastKnownPosition(E);                     // 0x102843b3 0x102843ab
		Goal.bDestSet = true;
		// Slot 563 `TranslateEnemyChasePosition(enemy, &goal.dest, &goal.tolerance, &scalar)`
		// (`0x1028443e..0x10284441`), the scalar seeded from `0x102f2fc0` (path+0x20, `0x10284424`).
		// The port's slot-563 bodies read and write both words in CENTIMETRES (`ElysiumNpcPositions2.cpp`:
		// the hull width `* U`, `GoalToleranceCm`); the goal's word is SOURCE units with the -1.0 "keep"
		// sentinel, which a body that writes nothing leaves standing.
		float ToleranceScratchCm = Goal.ToleranceUnits;
		float ScalarCm = NavPathScalar20 * U;
		TranslateEnemyChasePosition(E, Goal.DestCm, &ToleranceScratchCm, &ScalarCm); // 0x10284441
		if (ToleranceScratchCm != Goal.ToleranceUnits)
		{
			Goal.ToleranceUnits = ToleranceScratchCm / U;
		}
		if (StartTaskSetGoal(Goal, 2))                                   // 0x10284454  SetGoal(.., 2) 0x1028445b
		{
			NavPathScalar20 = ScalarCm / U;                              // 0x10284468  0x102f2fe0
			TaskComplete(false);                                         // 0x10284470
			return 0;
		}
		StartTaskDevMessage(TEXT("GetPathToEnemyLKP failed!!\n"));       // 0x10284489
		RememberUnreachable(Enemy());                                    // 0x1028449f 0x10284496
		Fail(0x724, FAIL_NO_ROUTE);                                      // 0x102844be
		return 0;
	}

	case TASK_GET_PATH_TO_ENEMY_LKP_LOS:                                 // arm 0x0d, 0x102844d1 0x102844d5 0x102844df
	{
		FElysiumEntity* E = Enemy();
		if (E == nullptr)
		{
			Fail(0x72d, FAIL_NO_ENEMY);                                  // 0x102844f9 0x1028450e
			return 0;
		}
		const FVector Lkp = Conditions19LastKnownPosition(Enemy());         // 0x10284526 0x1028451e
		float MinUnits = 0.f;
		float MaxUnits = 0.f;
		StartTaskWeaponRange(MinUnits, MaxUnits);                        // 0x1028452d..0x102845f7 0x1028453d 0x10284544 0x1028454c 0x1028455d 0x10284573 0x10284575 0x10284582 0x10284593 0x102845a4 0x102845bc 0x102845be 0x102845cb 0x102845eb
		// The aim point is the LKP plus the ENEMY's `m_vecViewOffset` (`+0x184`).
		FElysiumEntity* Again = Enemy();                                 // 0x102845fb
		const FVector Aim = Lkp + (Again != nullptr ? Again->EyePosition() - Again->Origin : FVector::ZeroVector); // 0x10284601
		FVector Los = FVector::ZeroVector;
		if (!StartTaskFindLosPos(Lkp, Aim, MinUnits, MaxUnits, Los))     // 0x10284678 0x1028467f
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
		StartTaskAngleVectors(Angles, &Forward, nullptr);                // 0x10285251 0x1028524a 0x1028525d
		const FVector Lkp = Conditions19LastKnownPosition(Enemy());         // 0x10285275 0x1028526d
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

	case TASK_GET_PATH_TO_ENEMY_LOS:                                     // arm 0x10, 0x1028545a 0x1028545e 0x10285468
	{
		if (Enemy() == nullptr)
		{
			Fail(0x86f, FAIL_NO_ENEMY);                                  // 0x10285482
			return 0;
		}
		float MinUnits = 0.f;
		float MaxUnits = 0.f;
		StartTaskWeaponRange(MinUnits, MaxUnits);                        // 0x10285495..0x1028555f 0x102854a5 0x102854ac 0x102854b4 0x102854c5 0x102854db 0x102854dd 0x102854ea 0x102854fb 0x1028550c 0x10285524 0x10285526 0x10285533 0x10285553
		FElysiumEntity* E = Enemy();                                     // 0x10285563 0x1028556f
		FVector Los = FVector::ZeroVector;
		if (!StartTaskFindLosPos(E->Origin, E->EyePosition(), MinUnits, MaxUnits, Los)) // 0x102855b3 0x1028559f 0x102855aa 0x102855ba
		{
			Fail(0x891, FAIL_NO_SHOOT);                                  // 0x102856f8
			return 0;
		}
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceHull);
		Goal.DestCm = Los;
		Goal.bDestSet = true;
		Goal.MovementActivity = ACT_RUN;                                 // 0x10285625
		StartTaskSetGoal(Goal, 0);                                       // 0x10285641  result discarded
		if (BaseScheduleHost.HintNode != INDEX_NONE)                     // 0x10285646 0x1028564e
		{
			StartTaskSetArrivalActivity(GetCoverActivity(&BaseScheduleHost.HintNode)); // 0x10285664 0x1028565b
		}
		if (FElysiumEntity* Again = Enemy())                             // 0x1028566d 0x10285677
		{
			StartTaskSetArrivalDirection(Again->Origin - Los); // 0x102856cc
		}
		else
		{
			++StartTaskNav.CrashGuards;
		}
		return 0;
	}

	case TASK_GET_PATH_TO_TARGET:                                        // arm 0x11, 0x10285949 0x10285954
	{
		FElysiumEntity* Tgt = ResolveTargetEnt();
		if (Tgt == nullptr)
		{
			Fail(0x8bf, FAIL_NO_TARGET);                                 // 0x10285998 0x10285974 0x1028597c 0x102859b3
			return 0;
		}
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceKeep);
		Goal.DestCm = Tgt->Origin;                                       // 0x102859e4 0x102859ca
		Goal.bDestSet = true;
		Goal.Target = TargetEnt;                                         // 0x10285a80  [10] 0x10285a57 0x10285a74
		StartTaskSetGoal(Goal, 0);                                       // 0x10285a8c  result discarded
		return 0;
	}

	case TASK_GET_PATH_TO_HINTNODE:                                      // arm 0x12, 0x10285a9e 0x10285aa8
	{
		if (BaseScheduleHost.HintNode == INDEX_NONE)
		{
			Fail(0x8cf, FAIL_NO_HINT_NODE);                              // 0x10285ac4
			return 0;
		}
		// `0x102d1180(hint, this, &out)` (`0x10285add`): the hint's `GetAbsOrigin`, or with a network
		// node (`+0x5e4 != -1`) the node's position at this NPC's pathing hull (`0x102f46d0`). Only a
		// hint index that names no live hint leaves no destination (the route is refused).
		FVector Approach = FVector::ZeroVector;
		const bool bApproach = HintPositionCm(BaseScheduleHost.HintNode, Approach); // 0x10285add  0x102d1180
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceKeep);
		Goal.DestCm = Approach;
		Goal.bDestSet = bApproach;
		Goal.MovementActivity = ACT_RUN;                                 // 0x10285b5e
		StartTaskSetGoal(Goal, 0);                                       // 0x10285b9e  result discarded
		return 0;
	}

	case TASK_STORE_LASTPOSITION:                                        // arm 0x13, 0x10282afd 0x10282b01
		LastPosition = Origin;                                           // 0x10282b09  +0x5db8 = GetOrigin()
		LastFacing = Angles;                                             // 0x10282b2f  +0x5dc4 = GetAngles() 0x10282b25
		TaskComplete(false);                                             // 0x10282b49
		return 0;

	case TASK_CLEAR_LASTPOSITION:                                        // arm 0x14, 0x10282b5b
		LastPosition = FVector::ZeroVector;                              // 0x10282b63  vec3_origin 0x1070d1b0
		LastFacing = FVector::ZeroVector;                                // 0x10282b86  vec3_angle 0x1070d9d0
		TaskComplete(false);                                             // 0x10282ba5
		return 0;

	case TASK_STORE_POSITION_IN_SAVEPOSITION:                            // arm 0x15, 0x10282bb7 0x10282bbb
		SavePosition = Origin;                                           // 0x10282bc5  +0x5dd0
		TaskComplete(false);                                             // 0x10282bdf
		return 0;

	case TASK_STORE_BESTSOUND_IN_SAVEPOSITION:                           // arm 0x16, 0x10282bf1
	{
		const FElysiumGameSoundEvent* Sound = static_cast<const FElysiumGameSoundEvent*>(GetBestSound()); // 0x10282bf5 0x10282bfd
		if (Sound == nullptr)
		{
			FailText(0x492, TEXT("No Sound!"), FAIL_TEXT_NO_SOUND);      // 0x10282c1c
			return 0;
		}
		SavePosition = Sound->Position;                                  // 0x10282c33  sound+0x20
		// `0x100290c0(gEntList, sound)` resolves the sound's owner; when it does, TWICE its slot 199
		// `GetVelocity` vector is added, the Z addend first computed into a temporary.
		if (FElysiumEntity* Owner = World != nullptr ? World->Resolve(Sound->Source) : nullptr) // 0x10282c51 0x10282c58
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

	case TASK_STORE_ENEMY_POSITION_IN_SAVEPOSITION:                      // arm 0x17, 0x10282cc7 0x10282ccb 0x10282cd5
	{
		if (Enemy() == nullptr)
		{
			Fail(0x4a7, FAIL_NO_ENEMY);                                  // 0x10282cef
			return 0;
		}
		SavePosition = Enemy()->Origin;                                  // 0x10282d0e  GetAbsOrigin 0x10282d04
		TaskComplete(false);                                             // 0x10282d32
		return 0;
	}

	case TASK_GET_PATH_TO_LASTPOSITION:                                  // arm 0x18, 0x10285bb0
	{
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceKeep);
		Goal.DestCm = LastPosition;                                      // 0x10285bcc
		Goal.bDestSet = true;
		if (!StartTaskSetGoal(Goal, 0))                                  // 0x10285c64 0x10285c6b
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
		StartTaskWeaponRange(MinUnits, MaxUnits);                        // 0x1028570d..0x102857d7 0x1028571d 0x10285724 0x1028572c 0x1028573d 0x10285753 0x10285755 0x10285762 0x10285773 0x10285784 0x1028579c 0x1028579e 0x102857ab 0x102857cb
		// The aim point is the save position plus the NPC's OWN `m_vecViewOffset` (`+0x184`).
		const FVector Aim = SavePosition + (EyePosition() - Origin);     // 0x102857d7
		FVector Los = FVector::ZeroVector;
		if (!StartTaskFindLosPos(SavePosition, Aim, MinUnits, MaxUnits, Los)) // 0x1028584b 0x10285852
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
			TaskComplete(false);                                         // 0x10285dbb 0x10285db7
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
			bScent ? GetBestScent() : GetBestSound());                   // 0x10285efc slot 475 / 0x10285dfc slot 474 0x10285e06 0x10285f06
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
		StartTaskSetMovementActivity(Activity);                          // 0x1028640e 0x10286406
		BaseScheduleHost.MemoryBits &= ~static_cast<uint32>(MemoryInCover); // 0x1028641b
		TaskComplete(false);                                             // 0x10286426
		return 0;
	}

	case TASK_WALK_PATH:                                                 // arm 0x1f, 0x10286438
	{
		int32 Activity = INDEX_NONE;
		const bool bFly = GetMoveType() == MoveTypeFly || GetMoveType() == MoveTypeFlyGravity; // 0x1028643c / 0x1028644e 0x10286448
		if (bFly && SelectWeightedSequenceForActivity(ACT_FLY) != INDEX_NONE) // 0x1028645e 0x10286457 0x10286465
		{
			Activity = ACT_FLY;
		}
		else
		{
			Activity = SelectWeightedSequenceForActivity(ACT_WALK) != INDEX_NONE ? ACT_WALK : ACT_RUN; // 0x10286470 0x10286477
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
		BaseScheduleHost.bShouldMove = true;                             // 0x10286563 0x1028656a
		FVector Right = FVector::ZeroVector;
		StartTaskAngleVectors(Angles, nullptr, &Right);                  // 0x10286571
		const FVector Waypoint = StartTaskCurWaypointPos();              // 0x1028658d 0x10286583
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
		if (!NavIsGoalActive())                                          // 0x10283cfd  0x102ee6a0 0x10283d04
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
		TaskComplete(false);                                             // 0x10283d87 0x10283d83
		return 0;
	}

	case TASK_FACE_PLAYER:                                               // arm 0x29, 0x10286537
		BaseScheduleHost.WaitFinished = Now + Data;                      // 0x10286544
		return 0;

	case TASK_FACE_ENEMY:                                                // arm 0x2a, 0x10283c66 0x10283c6a
	{
		const FVector Lkp = Conditions19LastKnownPosition(Enemy());         // 0x10283c85 0x10283c7d
		if (FInAimCone(Lkp))                                             // 0x10283c93  slot 364 0x10283c9b
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
		StartTaskMotorSetIdealYaw(Yaw);                                  // 0x10283a52 shared store 0x10283a5b 0x10283a76
		SetTurnActivity();                                               // 0x10283a9a / 0x102829aa 0x10282999 0x10283a8e
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

	case TASK_FACE_TARGET:                                               // arm 0x2d, 0x10283b9e 0x10283ba9
	{
		FElysiumEntity* Tgt = ResolveTargetEnt();
		if (Tgt == nullptr)
		{
			Fail(0x61c, FAIL_NO_TARGET);                                 // 0x10283c53
			return 0;
		}
		StartTaskMotorHoldYaw();                                         // 0x10283bd8 0x10283bca 0x10283bd0 0x10283be5
		StartTaskMotorSetIdealYawToTarget(Tgt->Origin);                  // 0x10283c1d  GetAbsOrigin 0x10283c02 0x10283c14
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
		StartTaskMotorSetIdealYaw(StartTaskAngleMod(static_cast<float>(Angles.Y))); // 0x10283af8..0x10283b62 0x10283af2 0x10283b01 0x10283b28 0x10283b37 0x10283b5a
		TaskComplete(false);                                             // 0x10283b67 / 0x10283b8c 0x10283b80
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
			TaskComplete(false);                                         // 0x10282a4c 0x10282a46
		}
		else
		{
			// `0x102d1af0(this, 0, (int)data, 2000.0, NULL, NULL)` — the operand is the hint TYPE.
			BaseScheduleHost.HintNode = FindHintNear(static_cast<int32>(Data), 0, HintSearchRadiusUnits); // 0x10282a36 / 0x10282a3e 0x10282a2d
			if (BaseScheduleHost.HintNode != INDEX_NONE)
			{
				TaskComplete(false);                                     // 0x10282a4c
			}
			else
			{
				Fail(0x45a, FAIL_NO_HINT_NODE);                          // 0x10282a69
			}
		}
		if (TaskId == TASK_FIND_HINTNODE)                                // 0x10282a6f 0x10282a73
		{
			return 0;
		}
		// `TASK_FIND_LOCK_HINTNODE` falls into arm 0x39's body.
		[[fallthrough]];
	case TASK_LOCK_HINTNODE:                                             // arm 0x39, 0x10282a79 0x10282a81
	{
		if (BaseScheduleHost.HintNode == INDEX_NONE)
		{
			Fail(0x465, FAIL_NO_HINT_NODE);                              // 0x10282a99
			return 0;
		}
		// `0x102d1350(hint, this)` — the claim, called directly by the base arm (0018/8: on the live
		// hint, `FElysiumNpcBase::ClaimHint`); no Troika surface is between them in retail.
		++StartTaskNav.HintLockAttempts;
		const bool bLocked = ClaimHint(BaseScheduleHost.HintNode);          // 0x10282aad
		if (bLocked)
		{
			TaskComplete(false);                                         // 0x10282aba 0x10282ab6
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

	case TASK_SPEAK_SENTENCE:                                            // arm 0x3f, 0x102868c9 0x102868ce
		SpeakSentence(static_cast<int32>(Data));                         // 0x102868d6  slot 508
		TaskComplete(false);                                             // 0x102868e0
		return 0;

	case TASK_SET_ACTIVITY:                                              // arm 0x40, 0x10284311
	{
		const int32 Activity = StartTaskActivityOperand(*Step);          // 0x10284314
		if (Activity != 0)
		{
			SetIdealActivity(Activity);                                  // 0x10284320 0x1028431b
		}
		else
		{
			ActivityNumber = 0;                                          // 0x10284332  m_Activity = 0
		}
		// Neither arm completes.
		return 0;
	}

	case TASK_SET_SCHEDULE:                                              // arm 0x41, 0x10282e27 0x10282e2a
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
		if (Program == nullptr)                                          // 0x10282e39 0x10282e32 0x10282e3b
		{
			Fail(0x507, FAIL_SCHEDULE_NOT_FOUND);                        // 0x10282e98
			return 0;
		}
		// `+0x1b2c = 1`, `+0x1b30 = file`, `+0x1b34 = 0x4fc` — the SetSchedule call record.
		RecordScheduleEvent(FString::Printf(TEXT("SetSchedule trace 1 %s:%d"), File, 0x4fc)); // 0x10282e3d
		BaseScheduleHost.IdealScheduleRetail = static_cast<int32>(Data); // 0x10282e66  the UNtranslated id 0x10282e5e
		ElysiumSchedule::Install(Schedule, Program->GlobalId, *this);    // 0x10282e6c  SetSchedule 0x10280e50
		// No `TaskComplete`: the schedule change ends the program.
		return 0;
	}

	case TASK_SET_FAIL_SCHEDULE:                                         // arm 0x42, 0x10286c45 0x10286c48
		Schedule.FailScheduleOverride = static_cast<int32>(Data);        // 0x10286c51  m_failSchedule (+0x5c54)
		TaskComplete(false);                                             // 0x10286c57
		return 0;

	case TASK_SET_TOLERANCE_DISTANCE:                                    // arm 0x43, 0x10286c69
		Navigator.GoalToleranceCm = ResolveTaskDistance(Data) * ElysiumMove::U; // 0x10286c71 slot 418 / 0x10286c84 0x102ee1c0 path+0x28, untruncated 0x10286c7d
		TaskComplete(false);                                             // 0x10286c8d
		return 0;

	case TASK_SET_ROUTE_SEARCH_TIME:                                     // arm 0x44, 0x10286d1a
		Navigator.RouteSearchTime = static_cast<float>(static_cast<int32>(Data)); // 0x10286d1d / 0x10286d3d  0x102886f0, truncated 0x10286d36
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
		const FElysiumGameSoundEvent* Sound = static_cast<const FElysiumGameSoundEvent*>(GetBestSound()); // 0x102838eb 0x102838f7
		if (Sound == nullptr)
		{
			FailText(0x5eb, TEXT("No sound in list"), FAIL_TEXT_NO_SOUND_IN_LIST); // 0x10283914
			return 0;
		}
		// The sound's origin is BOTH the threat and its eye, and its `m_iVolume` (`+0x8`, an int in
		// units) is the MINIMUM radius.
		const float VolumeUnits = static_cast<float>(static_cast<int32>(Sound->RadiusCm / U)); // 0x1028394d FILD 0x1028393c
		FVector Cover = FVector::ZeroVector;
		if (!StartTaskFindCoverPos(Sound->Position, Sound->Position, VolumeUnits, CoverRadius(), Cover)) // 0x10283955 0x1028395c
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
			Threat = this;                                               // 0x10283568 0x10283566
		}
		if (StartTaskFindLateralCover(Threat->EyePosition(), Threat))    // 0x10283580  0x102784a0 0x10283577 0x10283587
		{
			BaseScheduleHost.MoveWaitFinished = Now + Data;              // 0x10283599
			TaskComplete(false);                                         // 0x1028359f 0x102835c1
			return 0;
		}
		FVector Cover = FVector::ZeroVector;
		if (!StartTaskFindCoverPos(Threat->Origin, Threat->EyePosition(), 0.f, CoverRadius(), Cover)) // 0x102835ed 0x102835d9 0x102835e4 0x102835f4
		{
			Fail(0x5a6, FAIL_NO_COVER);                                  // 0x102836e7
			return 0;
		}
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION_NEAREST_NODE, NavToleranceHull);
		Goal.DestCm = Cover;
		Goal.bDestSet = true;
		Goal.MovementActivity = ACT_RUN;                                 // 0x1028365f
		StartTaskSetGoal(Goal, 0);                                       // 0x1028367b
		HintArrival();                                                   // 0x10283680..0x102836c3 0x10283688 0x10283699 0x102836a2 0x102836bb
		BaseScheduleHost.MoveWaitFinished = Now + Data;                  // 0x102839f7 (via 0x102836c8)
		return 0;
	}

	case TASK_FIND_LATERAL_COVER_FROM_ENEMY:                             // arm 0x49, 0x102836fa 0x10283705
	{
		// `m_hEnemy` (`+0x5ce0`) resolved raw (not slot 167), else this NPC.
		const FElysiumEntity* Threat = World != nullptr ? World->Resolve(BaseMemory.Enemy) : nullptr; // 0x102836fa
		if (Threat == nullptr)
		{
			Threat = this;                                               // 0x1028372c 0x10283725 0x1028372a 0x10283738
		}
		if (!StartTaskFindLateralCover(Threat->EyePosition(), Threat))   // 0x1028376b 0x1028374f 0x10283762 0x10283772
		{
			Fail(0x5c2, FAIL_NO_COVER);                                  // 0x102837b6
			return 0;
		}
		BaseScheduleHost.MoveWaitFinished = Now + Data;                  // 0x10283784
		TaskComplete(false);                                             // 0x1028378a
		return 0;
	}

	case TASK_FIND_BACKAWAY_FROM_SAVEPOSITION:                           // arm 0x4a, 0x10282eab 0x10282eaf 0x10282eb7
	{
		if (Enemy() == nullptr)
		{
			Fail(0x510, FAIL_NO_ENEMY);                                  // 0x10282ed3
			return 0;
		}
		// `0x102edae0(nav, &m_vSavePosition, 0.0, 30000.0, &out)` — the family-Senses10 seam.
		FVector Node = FVector::ZeroVector;
		if (!NearestNavigatorNode(SavePosition, 0.f, BackawaySearchUnits, Node)) // 0x10282f00 0x10282f07
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
			TaskComplete(false);                                         // 0x10282ff8 0x10282ff5
			return 0;
		}
		Fail(0x524, FAIL_NO_ROUTE);                                      // 0x10283022
		return 0;
	}

	case TASK_FIND_NODE_COVER_FROM_ENEMY:                                // arm 0x4b, 0x102833ac 0x102833b0 0x102833ba
	case TASK_FIND_NEAR_NODE_COVER_FROM_ENEMY:                           // arm 0x4c, 0x10283035 0x10283039 0x10283043
	case TASK_FIND_FAR_NODE_COVER_FROM_ENEMY:                            // arm 0x4d, 0x102831ea 0x102831ee 0x102831f8
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
			NoCoverLine = 0x55e;                                         // 0x1028338f 0x10283365 0x1028336d
			CoverTolerance = NavToleranceHull;                              // 0x102832bd
		}
		if (Enemy() == nullptr)
		{
			Fail(NoEnemyLine, FAIL_NO_ENEMY);                            // 0x102833d4 / 0x1028305d / 0x10283212 0x10283072 0x1028307e 0x10283227 0x10283233 0x102833e9 0x102833f5
			return 0;
		}
		FElysiumEntity* E = Enemy();
		// The radius pair: 0x5b 0 .. CoverRadius; 0x5c 0 .. ResolveTaskDistance(data);
		// 0x5d ResolveTaskDistance(data) .. CoverRadius. Evaluation order is retail's: the
		// far arm reads CoverRadius before ResolveTaskDistance (`0x10283259`, `0x1028326b`). 0x10283249 0x10283281
		float MinUnits = 0.f;
		float MaxUnits = 0.f;
		if (TaskId == TASK_FIND_NODE_COVER_FROM_ENEMY)
		{
			MaxUnits = CoverRadius();                                    // 0x10283417 0x10283407
		}
		else if (TaskId == TASK_FIND_NEAR_NODE_COVER_FROM_ENEMY)
		{
			MaxUnits = ResolveTaskDistance(Data);                        // 0x102830a8 0x10283094
		}
		else
		{
			MaxUnits = CoverRadius();                                    // 0x10283259
			MinUnits = ResolveTaskDistance(Data);                        // 0x1028326b
		}
		FVector Cover = FVector::ZeroVector;
		if (!StartTaskFindCoverPos(E->Origin, E->EyePosition(), MinUnits, MaxUnits, Cover)) // 0x10283443 / 0x102830d7 / 0x10283297 0x102830c1 0x102830cc 0x102830de 0x1028328c 0x1028329e 0x1028342f 0x1028343a 0x1028344a
		{
			Fail(NoCoverLine, FAIL_NO_COVER);                            // 0x10283545 / 0x102831d7 / 0x10283399
			return 0;
		}
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION_NEAREST_NODE, CoverTolerance);
		Goal.DestCm = Cover;
		Goal.bDestSet = true;
		Goal.MovementActivity = ACT_RUN;                                 // 0x102834b5 / 0x1028313f / 0x10283309
		StartTaskSetGoal(Goal, 0);                                       // 0x102834d1 / 0x10283163 / 0x10283325 0x10283332 0x10283343 0x1028334c
		// With no hint held the arm breaks to the bare return; neither path completes.
		HintArrival();                                                   // 0x102834d6..0x10283519 0x102834de 0x102834ef 0x102834f8 0x10283511
		return 0;
	}

	case TASK_FIND_COVER_FROM_ORIGIN:                                    // arm 0x4e, 0x102837c9 0x102837cd 0x102837e5
	{
		FVector Cover = FVector::ZeroVector;
		if (!StartTaskFindCoverPos(Origin, EyePosition(), 0.f, CoverRadius(), Cover)) // 0x10283807 0x102837fd 0x1028380e
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
		LifeState = LifeStateDying;                                // 0x1028680c  m_lifeState = 1
		return 0;

	case TASK_WAIT_FOR_SCRIPT:                                           // arm 0x50, 0x102868f2 0x1028690c
	{
		FElysiumScriptedSequence* Cine = ResolveCine();                  // 0x102868f2  m_hCine
		if (Cine == nullptr)
		{
			// Retail reads `(NULL)+0x5f44` with no test (`0x10286938`). Named crash guard. 0x10286929
			++StartTaskNav.CrashGuards;
			return 0;
		}
		if (!Cine->PreIdle.IsEmpty())                                    // 0x1028693f  m_iszIdle != NULL_STRING 0x10286946 0x1028694a
		{
			Cine->StartSequence(*this, Cine->PreIdle, false);            // 0x10286966  slot 584 0x10286957 0x1028696e 0x1028697b
			// `_strcmpi(STRING(m_iszPlay), STRING(m_iszIdle)) == 0` (`0x10288560`: `__strcmpi`, case-
			// insensitive) -> arm 0x65's store.
			if (Cine->Play.Equals(Cine->PreIdle, ESearchCase::IgnoreCase)) // 0x10286996 0x10286983 0x10286990 0x102869a0
			{
				if (FElysiumNpc* Troika = AsNpc())
				{
					Troika->SequencePlaybackRate = 0.f;                  // 0x102869a6  +0x6f4
				}
			}
			return 0;
		}
		if (GetScriptState() != ScriptStateCustomMove)                   // 0x102869bd  +0x5d70 0x102869c4
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
			// `m_hCine` resolved and read with no test (`0x102869f5`). Named crash guard. 0x102869ed
			++StartTaskNav.CrashGuards;
			return 0;
		}
		// Prefer `m_iszPlay` (`+0x5f48`), then `m_iszPostIdle` (`+0x5f4c`), else NULL_STRING.
		FString Pick;
		if (!Cine->Play.IsEmpty())                                       // 0x10286a02 0x10286a09 0x10286a0d
		{
			Pick = Cine->Play;
		}
		else if (!Cine->PostIdle.IsEmpty())                              // 0x10286a3a 0x10286a25 0x10286a2d 0x10286a41 0x10286a45
		{
			Pick = Cine->PostIdle;
		}
		ScriptArrivalActivity = INDEX_NONE;                              // 0x10286a72  +0x5d7c = -1 0x10286a5d 0x10286a78
		ScriptArrivalSequence.Empty();                                   // 0x10286a86  +0x5d80 = NULL_STRING 0x10286a8c
		if (!Pick.IsEmpty())                                             // 0x10286a96 0x10286a9d
		{
			ScriptArrivalActivity = ActivityIdForName(Pick);             // 0x10286aa9  ActivityList_IndexForName 0x1025d760 0x10286aa3
			if (ScriptArrivalActivity == INDEX_NONE)                     // 0x10286ab1
			{
				ScriptArrivalSequence = Pick;                            // 0x10286abf  the raw name is parked 0x10286ab9
			}
		}
		TaskComplete(false);                                             // 0x10286ac9
		return 0;
	}

	case TASK_PLAY_SCRIPT:                                               // arm 0x52, 0x10286adb 0x10286add 0x10286ae5
		// `HasMovement(GetSequence())` (`0x10288610` -> `0x10094eb0`), result discarded; then the
		// empty `0x1027f270(0)`; then the NPC's own `m_scriptState = 0` -- no cine is read.
		SetScriptState(ScriptStatePlaying);                              // 0x10286af3  +0x5d70 = 0 0x10286aee
		return 0;

	case TASK_PLAY_SCRIPT_POST_IDLE:                                     // arm 0x53, 0x10286b0a 0x10286b0e
		// The empty `0x1027f270(2)`, then the NPC's own `m_scriptState = 2` -- no cine is read.
		SetScriptState(ScriptStatePostIdle);                             // 0x10286b13  +0x5d70 = 2
		return 0;

	case TASK_ENABLE_SCRIPT:                                             // arm 0x54, 0x10286b2a
		if (FElysiumScriptedSequence* Cine = ResolveCine())
		{
			Cine->DelayStart(false);                                     // 0x10286b39  0x101a8cf0(cine, 0) 0x10286b32
		}
		else
		{
			// `DelayStart` on a null cine (`0x10286b39`). Named crash guard.
			++StartTaskNav.CrashGuards;
		}
		TaskComplete(false);                                             // 0x10286b42
		return 0;

	case TASK_PLANT_ON_SCRIPT:                                           // arm 0x55, 0x10286b54
		if (FElysiumEntity* Tgt = ResolveTargetEnt())                              // 0x10286b5e  0x1028ac10 0x10286b65 0x10286b69
		{
			SetOrigin(Tgt->Origin);                                      // 0x10286b7b  0x102885d0: slot 62 SetOrigin(target->GetAbsOrigin()) 0x10286b72
		}
		TaskComplete(false);                                             // 0x10286b84
		return 0;

	case TASK_FACE_SCRIPT:                                               // arm 0x56, 0x10286b96
		if (FElysiumEntity* Tgt = ResolveTargetEnt())                              // 0x10286ba0 0x10286ba7
		{
			StartTaskMotorHoldYaw();                                     // 0x10286bb2 0x10286bab 0x10286bb9
			// `UTIL_AngleMod(target->GetAngles().y)` (`0x102885f0`, `0x10288590`) into the motor store
			// `0x10288670`.
			StartTaskMotorSetIdealYaw(StartTaskAngleMod(static_cast<float>(Tgt->Angles.Y))); // 0x10286bc0..0x10286bda 0x10286bc9 0x10286bd3
		}
		if (GetScriptState() != ScriptStateCustomMove)                   // 0x10286bdf  +0x5d70
		{
			SetTurnActivity();                                           // 0x10286bec 0x10286be6
		}
		StartTaskClearGoal();                                            // 0x10286bfb 0x10286bf4
		return 0;

	case TASK_WAIT_RANDOM:                                               // arm 0x57, 0x10283dae
		// `curtime + RandomFloat(0.1f, data)`, the literal first (`PUSH 0x3dcccccd`).
		BaseScheduleHost.WaitFinished = Now + RandomSeconds(Data);       // 0x10283dbf / 0x10283dcc
		return 0;

	case TASK_STOP_MOVING:                                               // arm 0x58, 0x10282d71
		if (!NavIsGoalActive())                                          // 0x10282d77 0x10282d7e
		{
			BaseScheduleHost.bShouldMove = false;                        // 0x10282dc5
			TaskComplete(false);                                         // 0x10282dcc
			return 0;
		}
		StartTaskClearGoal();                                            // 0x10282d86
		if (LookupPoseParameter(TEXT("move_yaw")) >= 0)                  // 0x10282d92 0x10282d99
		{
			SetPoseParameter(TEXT("move_yaw"), 0.f, false);              // 0x10282dac  slot 345
			++StartTaskNav.PoseParameterZeroes;
		}
		// The active-goal path returns WITHOUT completing; RunTask carries it.
		return 0;

	case TASK_TURN_LEFT:                                                 // arm 0x59, 0x10282926
	case TASK_TURN_RIGHT:                                                // arm 0x5a, 0x10282848
	{
		StartTaskMotorHoldYaw();                                         // 0x1028292c / 0x1028284e 0x10282857 0x10282866
		const float Current = StartTaskAngleMod(static_cast<float>(Angles.Y)); // 0x10282935..0x10282958 0x10282944
		const float Turned = TaskId == TASK_TURN_LEFT ? Current + Data : Current - Data; // 0x10282962 FADD / 0x10282884 FSUB
		StartTaskMotorSetIdealYaw(StartTaskAngleMod(Turned));            // 0x1028296b / 0x1028288d, store 0x10283a52 / 0x1028289b 0x102828ae
		SetTurnActivity();                                               // 0x102829aa / 0x102828ed 0x102828c5 0x102828e0 0x10282907 0x10282913
		return 0;
	}

	case TASK_REMEMBER:                                                  // arm 0x5b, 0x102829bd 0x102829c0
		BaseScheduleHost.MemoryBits |= static_cast<uint32>(static_cast<int32>(Data)); // 0x102829cd
		TaskComplete(false);                                             // 0x102829d7
		return 0;

	case TASK_FORGET:                                                    // arm 0x5c, 0x102829e9 0x102829ec
		BaseScheduleHost.MemoryBits &= ~static_cast<uint32>(static_cast<int32>(Data)); // 0x102829f9
		TaskComplete(false);                                             // 0x10282a05
		return 0;

	case TASK_WAIT_FOR_MOVEMENT:                                         // arm 0x5d, 0x10286749
		// `if (path+0x10) path+0x10 = 0` (`0x102ee2e0` reads the PAUSED byte; `0x102ee2c0` clears it).
		if (NavigatorIsPaused())                                         // 0x1028674f 0x10286756
		{
			Navigator.bPaused = false;                                   // 0x102ee2c0 -> 0x1030bea0
			++StartTaskNav.PathGoalFlagClears;                           // 0x1028675e
		}
		// `0x102ee620` -> `path+0x5c`, the goal TYPE: no goal completes the task and clears it.
		if (!NavigatorIsGoalSet())                                       // 0x10286769
		{
			BaseScheduleHost.bShouldMove = false;                        // 0x10286775 0x10286770
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
		BaseScheduleHost.bShouldMove = false;                            // 0x102867a9 0x102867a7
		SetIdealActivity(ResolveLinkActivity());                         // 0x102867af GetStoppedActivity 0x1027a6c0 / 0x102867b7
		return 0;

	case TASK_WAIT_FOR_MOVEMENT_STEP:                                    // arm 0x5e, 0x102866bf
		if (NavigatorIsPaused())                                         // 0x102866c5 0x102ee2e0 path+0x10 / 0x102866cc
		{
			Navigator.bPaused = false;                                   // 0x102ee2c0 -> 0x1030bea0
			++StartTaskNav.PathGoalFlagClears;                           // 0x102866d4
		}
		if (!NavIsGoalActive())                                          // 0x102866df
		{
			BaseScheduleHost.bShouldMove = false;                        // 0x102866ec 0x102866e8
			TaskComplete(false);                                         // 0x102866f2
			return 0;
		}
		if (IsActivityFinished())                                        // 0x10286706  slot 251
		{
			BaseScheduleHost.bShouldMove = false;                        // 0x10286714 0x10286710
			TaskComplete(false);                                         // 0x1028671b
			return 0;
		}
		BaseScheduleHost.bShouldMove = true;                             // 0x1028672f
		ValidateNavGoal();                                               // 0x10286736
		return 0;

	case TASK_WEAPON_FIND:                                               // arm 0x5f, 0x10286d78
	{
		const FVector Extents(WeaponFindExtentUnits, WeaponFindExtentUnits, WeaponFindExtentUnits); // 0x10286d78
		FElysiumEntity* Found = StartTaskWeaponFindUsable(Extents);      // 0x10286d9c  0x10333ad0 0x10286d94
		SetTarget(Found != nullptr ? Found->Handle : FElysiumEntityHandle::Invalid()); // 0x10286da4  SetTarget
		if (ResolveTargetEnt() == nullptr)                                         // 0x10286dab
		{
			Fail(0xaca, FAIL_NO_WEAPON);                                 // 0x10286de2
			return 0;
		}
		TaskComplete(false);                                             // 0x10286db8 0x10286db4
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
		const FVector DropStartUnits = Origin / U + Forward * DropshipForwardUnits; // 0x10284e5a..0x10284ea5 0x10284e8e
		FVector DropEndUnits = DropStartUnits;
		DropEndUnits.Z -= DropshipDropUnits;                                 // 0x10284ece
		FKernelHullTrace Trace;
		Trace.EndPosUnits = DropEndUnits;
		KernelHullTrace(DropStartUnits, DropEndUnits, FVector::ZeroVector, FVector::ZeroVector, DropshipTraceMask, Trace); // 0x10284f35 0x10284f06 0x10284f11 0x10284f40 0x10284f45 0x10284f4f
		FStartTaskNavGoal Goal = MakeGoal(GOALTYPE_LOCATION, NavToleranceKeep);
		Goal.DestCm = Trace.EndPosUnits * U;                             // 0x10284f77  tr.endpos 0x10284f6f
		Goal.bDestSet = true;
		if (StartTaskSetGoal(Goal, 2))                                   // 0x1028503f
		{
			TaskComplete(false);                                         // 0x1028504b 0x10285048
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
		if (StartTaskSetWanderGoal(static_cast<float>(MinPart), static_cast<float>(MaxPart))) // 0x10286f19 0x10286f12
		{
			TaskComplete(false);                                         // 0x10286f26 0x10286f22
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
		// `(float)(int)(weapon->+0x8c0 * flTaskData)` — the FMUL at `0x10286cb7` the decompiler drops. 0x10286cba
		const float MeleeTolerance = static_cast<float>(static_cast<int32>(Words[2] * Data)); // 0x10286cb1..0x10286cca 0x10286caa 0x10286cac 0x10286ccd
		Navigator.GoalToleranceCm = MeleeTolerance * ElysiumMove::U;                 // 0x10286cd4  0x102ee1c0 path+0x28
		TaskComplete(false);                                             // 0x10286cdd
		return 0;
	}

	case TASK_CHOOSE_BEST_MELEE_WEAPON:                                  // arm 0x67, 0x10286e2a
		if (StartTaskChooseBestMeleeWeapon())                            // 0x10286e2c  0x10337230
		{
			TaskComplete(false);                                         // 0x10286e39 0x10286e35
			return 0;
		}
		Fail(0xae6, FAIL_NO_WEAPON_TO_CHOOSE);                           // 0x10286e63
		return 0;

	case TASK_CHOOSE_BEST_RANGED_WEAPON:                                 // arm 0x68, 0x10286e76
		if (StartTaskChooseBestRangedWeapon())                           // 0x10286e78  0x10337300
		{
			TaskComplete(false);                                         // 0x10286e85 0x10286e81
			return 0;
		}
		Fail(0xaf1, FAIL_NO_WEAPON_TO_CHOOSE);                           // 0x10286eaf
		return 0;

	case TASK_GET_PATH_TO_PATHCORNER:                                    // arm 0x69, 0x10285ff8 0x10286002
	{
		if (Target.IsEmpty())                                            // 0x10285ff8  m_target (+0x20c)
		{
			// RETAIL DEFECT, reproduced: the scent arm's code and so its reason text.
			Fail(0x921, FAIL_NO_SCENT);                                  // 0x1028601c
			return 0;
		}
		const bool bFly = GetMoveType() == MoveTypeFly || GetMoveType() == MoveTypeFlyGravity; // 0x10286031 / 0x10286040 0x1028603a 0x10286049
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
		if (!StartTaskSetGoal(Goal, 0))                                  // 0x102860f0 0x102860f7
		{
			StartTaskDevMessage(TEXT("Can't Create Route!\n"));          // 0x10286104  DevWarning(2, ...)
		}
		else if (FElysiumNpc* Troika = AsNpc())
		{
			// The type-3 route build is `DoFindPath`'s chain arm (`0x102f2330`): the corner's `speed`
			// copy, the chain laid from `m_pGoalEnt`, the head leg. `SetGoal` issued the leg to the
			// first corner; this lays the chain over it (`NavFindPathCorners`, Troika line).
			(void)Troika->NavFindPathCorners();
		}
		// Neither path completes.
		return 0;
	}

	default:                                                             // arm 0x6a, 0x10286f63
		// `DevMsg("No StartTask entry for %s\n", TaskName(iTask))` (slot 449) and return WITHOUT
		// completing or failing: an unhandled task does not fail the schedule.
		return 0;
	}
}

// =================================================================================================
// The navigator services `0x102827f0` drives
// =================================================================================================

namespace
{
	// The waypoint arrival radius retail's move applies to the goal waypoint (navigator slot 16):
	// the constant `0x10451f78`, 0.0625 units (0.25, `0x10449260`, under `npc_vphysics`, which the
	// port does not read). It is NOT the path's goal tolerance `path+0x28`, which retail uses only for
	// a BLOCKED step (`0x102ef760`). Source units.
	constexpr float GNavArrivalRadiusUnits = 0.0625f;
	// `FUN_102fe9f0`'s per-search `RandomInt(5, 10)`, the pedestrian cost multiplier (`local_20`).
	constexpr int32 GNavPedestrianCostMin = 5;
	constexpr int32 GNavPedestrianCostMax = 10;

	// One travel request, filled from the navigator's goal words: the path's movement activity picks
	// the gait, the pedestrian byte (`path+0x1`, set by a type-8 goal) the pedestrian pricing with a
	// multiplier drawn per request, and the arrival radius is retail's waypoint constant. A goal that
	// is not a pedestrian goal draws nothing.
	FElysiumNpcMoveRequest MakeNavigatorMoveRequest(const FElysiumNpcNavigator& Nav,
		const IElysiumNpcMotor* Motor, const FVector& DestCm)
	{
		const int32 Activity = Nav.GetMovementActivity();
		const EElysiumNpcGaitKind Gait = Activity == ElysiumStartTask19Base::ACT_RUN
			? EElysiumNpcGaitKind::Run : EElysiumNpcGaitKind::Walk;
		FElysiumNpcMoveRequest Request;
		Request.DestinationCm = DestCm;
		Request.AcceptanceToleranceCm = GNavArrivalRadiusUnits * ElysiumMove::U;
		Request.SpeedCmPerSecond = ElysiumNpcGait::TravelSpeed(Motor, Gait);
		Request.GaitKind = Gait;
		Request.PartialPath = EElysiumNpcPartialPath::Refuse;
		Request.PedestrianCostMultiplier = Nav.bPedestrian
			? ElysiumNpcEngineRandom::RandomInt(GNavPedestrianCostMin, GNavPedestrianCostMax) : 0;
		Request.MovementActivityName = FName(*FString::Printf(TEXT("ACT_0x%02x"), Activity));
		return Request;
	}
}

bool FElysiumNpcBase::StartTaskSetGoal(const FStartTaskNavGoal& Goal, int32 SetGoalFlags)
{
	using namespace ElysiumStartTask19Base;

	// `CAI_Navigator::SetGoal` `0x102ecd20`, in its order. The ONE port body: the Troika halves
	// (`StartTask19SetGoal`, `TaskTailNavSetGoal`) and family Script19 (`Script19SetGoal`) convert
	// their literals and call this.
	++StartTaskNav.SetGoalCalls;
	StartTaskNav.LastGoal = Goal;
	StartTaskNav.LastSetGoalFlags = SetGoalFlags;
	const float U = ElysiumMove::U;
	FElysiumNpc* Troika = AsNpc();

	// `0x102ecd20..0x102ecd62`: nav `+0x8` := the owner's hull, `+0xc` := curtime (twice, through
	// `0x102ecc00`) -- words the port's mover does not keep -- then `this->vtable[0x1c]()`,
	// navigator slot 7, the reset (`0x102eea70`).
	FUN_102eea70();                                                      // 0x102ecd66
	if ((SetGoalFlags & 1) != 0)                                         // 0x102ecd6e TEST AL,0x1
	{
		NavClearRoute();                                                 // 0x102ecd74 0x102f28a0
	}
	// Flag 2 (`0x102ecd7b`): the path's target handle (`path+0x30`, `0x100a0ae0(.., NULL)`) and its
	// dest words (`path+0x34..+0x3c` := `DAT_1070d1b0..b8`).
	if ((SetGoalFlags & 2) != 0)
	{
		Navigator.TargetEntity = FElysiumEntityHandle::Invalid();
		Navigator.TargetOffsetCm = FVector::ZeroVector;
	}

	// [5] -> `SetMovementActivity` (`0x102ecdaa` / `0x102ecdb2` `0x102ee250`).
	if (Goal.MovementActivity != INDEX_NONE)
	{
		StartTaskSetMovementActivity(Goal.MovementActivity);
	}

	// The goal record's words onto the path: the type through `0x1030ba50` (`path+0x5c`), the flags
	// `path+0x60 = goal[9]` (this store is their only writer), the destination node `[4]`
	// (`0x102ee9c0`) and the arrival words `[6]` / `[7]`. A type-8 goal (interesting place,
	// pedestrian -- NOT 9, the animal place) sets the pedestrian byte `path+0x1`, which only the
	// path reset clears.
	//
	// Goal flag 2 (`0x102ecf27`) returns BEFORE these stores (the `0x1030ba50(path, type)` write and
	// `path+0x60 = goal[9]`, which follow the flag-2 block in the listing), so a node-route goal writes
	// none of them: the path's type comes from the route's own install (4), `path+0x60` is left as it
	// stood and the arrival words are never applied. The arrival words `[6]` / `[7]` are written after a
	// successful build only (`0x102ed190..0x102ed1cc`), below.
	const bool bNodeRouteGoal = (Goal.GoalFlags & 2) != 0;
	if (!bNodeRouteGoal)
	{
		Navigator.GoalType = Goal.Type;
		Navigator.GoalFlags = Goal.GoalFlags;
		Navigator.GoalNode = Goal.DestNode;
		if (Goal.Type == GOALTYPE_PLACE_PEDESTRIAN)
		{
			Navigator.bPedestrian = true;
		}
	}

	// The goal entity by type (`0x102ecd20`'s two resolutions share it): 1 `m_hTargetEnt`
	// (`+0x5ce4`), 2 slot 167 `GetEnemy()` (vtable `+0x29c`), 7 `GetBestSeeUnknown()` through
	// `+0x98` (vtable `+0x928`, the Troika's slot 586).
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
		else if (Goal.Type == GOALTYPE_BESTSEEUNKNOWN && Troika != nullptr)
		{
			GoalEntity = World->Resolve(Troika->GetBestSeeUnknown());     // +0x98 vtable +0x928
		}
	}

	// The tolerance (`path+0x28`, `Navigator.GoalToleranceCm`): -2.0 (`_DAT_1049d980`) the hull's width,
	// anything but -1.0 (`_DAT_1049d97c`) as given, -1.0 keeps the path's own unless that is 0.0,
	// when the hull's is written -- and, for an entity goal whose entity is an NPC
	// (`+0x9c` non-null), the average of the two hulls (`* _DAT_10449270` 0.5).
	FVector MinsUnits = FVector::ZeroVector;
	FVector MaxsUnits = FVector::ZeroVector;
	// Nav `+0x8` is the owner's PATHING hull, `+0x156c` (`0x102ecd2c`), not `m_eHull` (`+0x1568`); the
	// goal NPC's side below reads its `m_eHull` (`[+0x9c]+0x1568`, `0x102ece99`).
	const int32 PathingHull = Troika != nullptr ? Troika->PathingHullKind : HullKind;
	RetailHullExtents(PathingHull, EElysiumHullExtents::Full, MinsUnits, MaxsUnits);
	const float HullWidthUnits = static_cast<float>(MaxsUnits.X - MinsUnits.X); // 0x102d61b0: row+0x18 - row+0xc
	if (Goal.ToleranceUnits == NavToleranceHull)
	{
		Navigator.GoalToleranceCm = HullWidthUnits * U;                         // 0x102ecdc9 -> 0x102ecec7
	}
	else if (Goal.ToleranceUnits != NavToleranceKeep)
	{
		Navigator.GoalToleranceCm = Goal.ToleranceUnits * U;                    // 0x102ecde4 -> 0x102ecec7
	}
	else if (Navigator.GoalToleranceCm == ElysiumNpcTunables::Zero)             // 0x102ecdf1 == _DAT_104454c4
	{
		Navigator.GoalToleranceCm = HullWidthUnits * U;                         // 0x102ece0e
		const FElysiumNpcBase* GoalNpc = GoalEntity != nullptr ? GoalEntity->AsNpcBase() : nullptr;
		if (GoalNpc != nullptr)                                          // 0x102ece90 +0x9c
		{
			FVector OtherMins = FVector::ZeroVector;
			FVector OtherMaxs = FVector::ZeroVector;
			RetailHullExtents(GoalNpc->HullKind, EElysiumHullExtents::Full, OtherMins, OtherMaxs);
			Navigator.GoalToleranceCm = (static_cast<float>(OtherMaxs.X - OtherMins.X) + HullWidthUnits)
				* ElysiumNpcTunables::Half * U;                          // 0x102ecebd FMUL 0.5
		}
	}
	const float ToleranceCm = Navigator.GoalToleranceCm;
	StartTaskNav.LastSetGoalToleranceUnits = ToleranceCm / U;
	// `0x102ececa`: path `+0x40` := the pathing hull * 0.5 (`FLD hull; FMUL half`; the hull's width is
	// the figure the tolerance arms above use).
	Navigator.WaypointToleranceCm = HullWidthUnits * ElysiumNpcTunables::Half * U;
	// `[11..13]` != `DAT_10934060..68` -> `0x1030be20` (arrival direction; every caller passes the
	// sentinel); path `+0x8` / `+0x4` := `[14]` / `[15]` (zero at every caller). None has a port word.
	//
	// Goal flag 2 (`0x102ecf27 TEST [goal+0x24],0x2`, taken at `0x102ecf2e`): an explicit NODE route,
	// returned WITHOUT the route build `0x102f1dc0`. One shipped issuer: Troika task `0xc7
	// TASK_GET_PATH_TO_ENEMY_CLOSEST`, reached by `SCHED_VTZIMISCE_ATTACK_CLOSEST`. In retail's order:
	//
	//     start = NearestNodeToNPC(net, npc, GetAbsOrigin);   -1 -> return false      0x102f3c10
	//     goal  = NearestNode(net, goal[1..3]);               -1 -> return false      0x102f41b0
	//     route = 0x102fd240(pathfinder, start, goal);        NULL -> return false
	//     path.type = 4; path.install(route); path.finalise();  return true           0x1030ba50 / 0x1030b4d0 / 0x1030b8e0
	//
	// No `OnNavFailed`, no `MotorTaskComplete`, no memory bit `0x20`, no face-the-path and no arrival
	// activity: every refusal is a plain `false` for the issuing arm to fail on. The install tail is the
	// one `SetRandomGoal` runs (`0x102ed430`), so it is the same body here (`InstallPathNoGoal`).
	//
	// NAMED DIVERGENCE: this runtime keeps the places (0018 story 4) but no links, so `0x102fd240`'s
	// route is Unreal's, asked of the follower (a refused request is the third refusal). The goal-side
	// nearest node (`NavNearestNodeTo`, no NPC and no hull, a clear line to one of the ten nearest nodes
	// within 2048 units) is retail's own and refuses exactly. The NPC-side one is gated by
	// `CanFitAtNode`'s stand check (`0x102f1900` -> `0x102e7270`), a seam answering false until 0018
	// story 6, so it cannot refuse yet: only an EMPTY network does (`0x102f3c3a`, `*network == 0`).
	if (bNodeRouteGoal)
	{
		// Flip when the move probe's stand check is real (0018 story 6): then a live network with no
		// node standable near the NPC is retail's refusal as well.
		constexpr bool bNodeStandCheckIsLive = false;
		const bool bNetworkEmpty = World == nullptr || World->Places().NumNodes() == 0;
		bool bRouted = false;
		StartTaskNav.LastSetGoalDestCm = Goal.DestCm;
		const int32 StartNode = NavNearestNodeToNpc(Origin);                        // 0x102f3c10
		if (!bNetworkEmpty && (StartNode != INDEX_NONE || !bNodeStandCheckIsLive))
		{
			const int32 GoalNode = Troika != nullptr ? Troika->NavNearestNodeTo(Goal.DestCm) : INDEX_NONE; // 0x102f41b0
			bRouted = GoalNode != INDEX_NONE && InstallPathNoGoal(Goal.DestCm);     // 0x102fd240 + install
		}
		StartTaskNav.bLastSetGoalResult = bRouted;
		return bRouted;
	}

	// The destination: an entity goal routes to the entity (`path+0x30`), its dest words written
	// only when the goal's own dest is not the default triple; a location goal takes `[1..3]`, or
	// with the default triple the node `[4]` (`0x102ee9c0`; no node graph, so nothing).
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
	// The goal position (`path+0x4c..+0x54`, `0x1030ba30`) and, for an entity goal, the target handle
	// (`path+0x30`, `[10]`, else the entity the type resolved).
	if (bHaveDest)
	{
		Navigator.GoalPosCm = DestCm;
	}
	// Types 1/2/7 overwrite `EBX` with the entity they resolved (`0x102ed05b..0x102ed0b9`), the goal's own
	// `[10]` included; the word is written only for a non-null `EBX` (`0x102ed0fd..0x102ed112`), so a type
	// that resolved nothing leaves `path+0x30` standing. Every other type carries `[10]` through.
	if (Goal.Type == GOALTYPE_TARGETENT || Goal.Type == GOALTYPE_ENEMY || Goal.Type == GOALTYPE_BESTSEEUNKNOWN)
	{
		if (GoalEntity != nullptr)
		{
			Navigator.TargetEntity = GoalEntity->Handle;                 // 0x102ed112
		}
	}
	else if (Goal.Target.IsSet())
	{
		Navigator.TargetEntity = Goal.Target;                            // 0x102ed112 [10]
	}

	// The AI trace's `move` goal: the destination this `SetGoal` hands the route build (debug output
	// only, behind its sink).
	if (bHaveDest && IsAiTraced())
	{
		EmitAiTrace(TEXT("move"), FString::Printf(TEXT("goal %.0f %.0f %.0f"), DestCm.X, DestCm.Y, DestCm.Z));
	}
	// `0x102ed11e` `0x102f1dc0(this, goal flags bit 3)`.
	const bool bRoute = NavBuildRoute(bHaveDest, DestCm);
	StartTaskNav.bLastSetGoalResult = bRoute;
	if (!bRoute)
	{
		// Flag 4: a refused goal clears the route (`0x102ed131` `0x102f28a0`).
		if ((SetGoalFlags & 4) != 0)
		{
			NavClearRoute();
		}
		return false;
	}
	// Goal flag bit 0: face the path (`0x102ed14b` `0x102e0b40`, `0x102ed15f` `0x102e2020` toward
	// the path's goal `0x1030ba30`).
	if ((Goal.GoalFlags & 1) != 0)
	{
		StartTaskMotorHoldYaw();
		StartTaskMotorSetIdealYawToTarget(DestCm);
	}
	// `0x102ed168` `0x102f13d0(this, 1)` -- unrecovered, no port word. Then [6] / [7] / the default
	// arrival activity 1 (`0x1030b550` / `0x1030b5b0`).
	// The arrival words, after a built route only: `[6]` (`0x102ed19b`), else `[7]` (`0x102ed1b7`), else
	// activity 1 (`0x102ed1cc`); a refused build leaves the words as they stood.
	if (Goal.ArrivalActivity != INDEX_NONE)
	{
		Navigator.ArrivalActivity = Goal.ArrivalActivity;
		StartTaskSetArrivalActivity(Goal.ArrivalActivity);
	}
	else if (Goal.ArrivalSequence != INDEX_NONE)
	{
		Navigator.ArrivalSequence = Goal.ArrivalSequence;
	}
	else
	{
		Navigator.ArrivalActivity = ACT_IDLE;
		StartTaskSetArrivalActivity(ACT_IDLE);
	}
	return true;
}

bool FElysiumNpcBase::NavBuildRoute(bool bHaveDest, const FVector& DestCm)
{
	using namespace ElysiumStartTask19Base;

	// `0x102f1dc0`. The build itself (`0x102f2330`) is the port's mover: `IElysiumNpcMotor::MoveTo`.
	// `SetGoal 0x102ecd20` fails only on the route: the running program owns the navigator.
	FElysiumNpc* Troika = AsNpc();
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	auto Build = [this]() -> bool
	{
		return DoFindSavedPath(); // 0x102f2330 complete semantic goal dispatch
	};
	// `0x102f1de4` `0x10319ee0(path+0x24)`, `path+0x44 := -1`: the path's waypoint list is emptied at
	// every entry, so a search that finds nothing leaves no head (`Navigator.bHasHeadWaypoint`).
	Navigator.bHasHeadWaypoint = false;
	Navigator.bHeadIsGoal = false;
	Navigator.LastNodePassed = INDEX_NONE;
	if (Troika != nullptr)
	{
		Troika->PedestrianLegs.Reset();                                         // the list's port half
		Troika->bNavBodyParked = false;                                         // a new route is issued fresh
	}
	if ((BaseScheduleHost.MemoryBits & MemoryPathFailed) == 0)                  // 0x102f1e0a TEST [+0x5d8c],0x20
	{
		if (Build())                                                            // 0x102f1e15 0x102f2330
		{
			BaseScheduleHost.MemoryBits &= ~MemoryPathFailed;                    // 0x102f1e22
			if (!IsCurTaskContinuousMove())                                     // 0x102f1e2c slot 529
			{
				MotorTaskComplete(false);                                       // 0x102f1e38 nav slot 2 -> 0x102623c0
			}
			return true;
		}
		if (Navigator.RouteSearchTime == ElysiumNpcTunables::Zero)                     // 0x102f1ee8 +0x40 == 0.0
		{
			NavOnNavFailed(FAIL_NO_ROUTE);                                      // 0x102f1f00 vtable+0x28 (0xc, 1)
			return false;
		}
		BaseScheduleHost.MemoryBits |= MemoryPathFailed;                        // 0x102f1f12
		Navigator.RouteRetryTime = Now + Navigator.RouteRetryInterval;                        // 0x102f1f27 +0x4c
		Navigator.RouteGiveUpTime = Now + Navigator.RouteSearchTime;                          // 0x102f1f36 +0x48
		return false;
	}
	if (Navigator.RouteGiveUpTime < Now)                                               // 0x102f1f4d +0x48 < curtime
	{
		NavOnNavFailed(FAIL_NO_ROUTE);                                          // 0x102f1f5a (0xc, 1)
		return false;
	}
	if (Navigator.RouteRetryTime < Now)                                                // 0x102f1f73 +0x4c < curtime
	{
		if (Build())                                                            // 0x102f1f80 0x102f2330
		{
			BaseScheduleHost.MemoryBits &= ~MemoryPathFailed;                    // 0x102f1f8d
			int32 TaskNumber = INDEX_NONE;
			if (!CurrentRetailTaskNumber(TaskNumber) || TaskNumber != TASK_WAIT_FOR_MOVEMENT) // 0x102f1f97 0x1028a150, != 0x6e
			{
				MotorTaskComplete(false);                                       // 0x102f1fa6 nav slot 2
			}
			return true;
		}
		Navigator.RouteRetryTime = Now + Navigator.RouteRetryInterval;                        // 0x102f1fc5 +0x4c
	}
	return false;
}

void FElysiumNpcBase::NavClearRoute()
{
	// `0x102f28a0`.
	Navigator.RouteSearchTime = 0.f;                                                   // +0x40
	Navigator.RouteGiveUpTime = 0.0;                                                   // +0x48
	Navigator.RouteRetryTime = 0.0;                                                    // +0x4c
	Navigator.RouteRetryInterval = 0.f;                                                // +0x44
	BaseScheduleHost.MemoryBits &= ~ElysiumStartTask19Base::MemoryPathFailed;   // +0x5d8c &= ~0x20
	// `0x1030bb30(path)`: the path reset -- goal type 0, goal position and target offset to the origin,
	// movement activity 1, tolerance `+0x28` 0, the pedestrian byte and the head waypoint cleared.
	Navigator.ResetPath();
	NavPathScalar20 = 0.f;                                                             // `0x1030bb82` path+0x20 := 0
	if (FElysiumNpc* Troika = AsNpc())
	{
		Troika->NavHeadCorner = FElysiumEntityHandle::Invalid();                       // the head waypoint's `wp+0x20`
		Troika->PedestrianLegs.Reset();                                                // the waypoint list's curb legs
		Troika->bNavBodyParked = false;                                                // no leg left to re-issue
	}
	if (Motor != nullptr)
	{
		Motor->ClearNavigationGoal();
	}
}

void FElysiumNpcBase::StartTaskClearGoal()
{
	// `0x102ee270`: the navigator reset `0x102f28a0` (route words zeroed, memory bit `0x20` cleared, the
	// path reset `0x1030bb30`), then the navigator's slot 7. The port's mover clears its route and keeps
	// any special traversal (`ClearNavigationGoal`, inside `NavClearRoute`).
	++StartTaskNav.ClearGoalCalls;
	NavClearRoute();
	bMoveIssued = false;
	FUN_102eea70();
}

void FElysiumNpcBase::StartTaskSetMovementActivity(int32 Activity)
{
	// `0x102ee250`: `m_pPath->m_movementActivity (+0x2c) = activity`. The word is the
	// navigator's (`FElysiumNpcNavigator::MovementActivity`) on every body; the gait is pushed on to the
	// route in flight (`SetTravelGait`), never a rebuild.
	++StartTaskNav.MovementActivitySets;
	StartTaskNav.LastMovementActivity = Activity;
	Navigator.MovementActivity = Activity;
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
	// `0x102ee5e0` -> `0x10012805(path)`: the head waypoint (`path+0x24`) when one stands -- the navigator
	// word `IsGoalActive 0x102ee6a0` reads -- else the static origin waypoint (`DAT_10936afc`).
	const FElysiumNpc* Troika = AsNpc();
	if (NavIsGoalActive() && Troika != nullptr)
	{
		return Troika->MoveGoal;
	}
	return Origin;
}

void FElysiumNpcBase::StartTaskMotorHoldYaw()
{
	// `0x102e0b40` is family RunTask19's `MotorMoveStop` (`motor+0x2c = -1.0`); counted for the tests.
	++StartTaskNav.MotorYawHolds;
	MotorMoveStop();
}

void FElysiumNpcBase::StartTaskMotorSetIdealYaw(float Yaw)
{
	// `0x10288670`: the `+0x28` flip then the `+0x1c == 180` direct store into `motor+0x34` -- the same
	// tail as `0x102e2020`'s, which family Hints' `SetMotorHintYaw` carries (a RETAIL-frame yaw). No
	// `UpdateYaw`: neither body calls it; the turn is the motor's own (`ReleaseMotorHintYaw`, L13).
	SetMotorHintYaw(Yaw);
}

void FElysiumNpcBase::StartTaskMotorSetIdealYawToTarget(const FVector& TargetCm)
{
	// `0x102e2020`: `0x102e2750` = slot 515 `CalcIdealYaw` (retail frame, `[0, 360)`), then the tail.
	SetMotorHintYaw(CalcIdealYaw(TargetCm));
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
	// `+0x8b8 +0x8bc +0x8c0 +0x8c4`, the weapon's class words (0018 story 8, findings R3).
	ElysiumWeapons::FRangeWords Words;
	if (!ElysiumWeapons::ItemRangeWords(Weapon, Words))
	{
		return false;
	}
	OutWords[0] = Words.MinRange1;
	OutWords[1] = Words.MinRange2;
	OutWords[2] = Words.MaxRange1;
	OutWords[3] = Words.MaxRange2;
	return true;
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

namespace ElysiumStartTaskWanderPick
{
	// PORT CONSTANTS -- no retail source. Retail's walk (`0x102ff3e0`) follows the AIN's links and
	// cannot pick a place the far side of a wall; the capped point pick (0018 story 4, "The pick,
	// entire") draws a place in a straight line and asks Unreal for the route, so it needs a rule for
	// a route that runs far past the order. A route longer than `kWanderDetourRatio` x the resolved
	// distance is a detour and the pick is dropped; at most `kWanderMaxDraws` draws are made in all.
	constexpr float kWanderDetourRatio = 2.0f;
	constexpr int32 kWanderMaxDraws = 5;

	// `+0x70`, the node type retail's walk never ENDS on (`0x102ff3e0`'s `!= 4` tests): a climb node.
	constexpr int32 NodeTypeClimb = 4;
	// `0x1030ba50(path, 4)`: the path type word `SetRandomGoal`'s install writes.
	constexpr int32 PathTypeRandom = 4;

	// One candidate place: its network index, where the NPC would stand there (`GetPosition` at the
	// pathing hull), and its straight-line distance in SOURCE units.
	struct FCandidate
	{
		int32 Node = INDEX_NONE;
		FVector PositionCm = FVector::ZeroVector;
		float DistanceUnits = 0.0f;
		bool bAhead = false;
	};
}

bool FElysiumNpcBase::InstallPathNoGoal(const FVector& DestCm)
{
	using namespace ElysiumStartTask19Base;
	using namespace ElysiumStartTaskWanderPick;

	// **The narrow seam 0018 story 5's navigator absorbs.** `0x102ed430`'s tail installs a route
	// straight into the navigator's PATH object and never calls `SetGoal` (`0x102ecd20`):
	//
	//     0x1030ba50(nav->m_pPath (+0x30), 4);     // path +0x5c, the type word
	//     0x1030b4d0(nav->m_pPath, route, 0);      // the waypoints
	//     0x1030b8e0(nav->m_pPath);                // (unrecovered: the path's own finalisation)
	//     nav->+0x14 = |from - path goal|^2;       // 0x1000f89e(path), SOURCE units squared
	//
	// So no `AI_NavGoal_t`, no goal type, no tolerance (`path+0x28` keeps the reset's value), no
	// movement or arrival activity. The port's route is the mover's: the move is issued here as the same
	// navigator request `NavBuildRoute` issues (the path's movement activity picks the gait, the waypoint
	// arrival radius is retail's constant), and nothing of `StartTaskSetGoal`'s runs.
	FElysiumNpc* Troika = AsNpc();
	if (Motor == nullptr)
	{
		return false;
	}
	// The AI trace's `move` goal for a goal-less install (debug output only, behind its sink).
	if (IsAiTraced())
	{
		EmitAiTrace(TEXT("move"), FString::Printf(TEXT("goal %.0f %.0f %.0f"), DestCm.X, DestCm.Y, DestCm.Z));
	}
	// `SetRandomGoal 0x102ed430` fails only on the route (no node, no pick): no body claim.
	if (Troika != nullptr)
	{
		Troika->MoveGoal = DestCm;
		// A new route in the path: a previous pedestrian route's curb legs are not its waypoints.
		Troika->PedestrianLegs.Reset();
		Troika->bNavBodyParked = false;
	}
	// Retail's A* prices the search from the path's own words: the pedestrian byte (`path+0x1`) is not
	// written here, so a byte a previous type-8 goal left set still prices this route, as it would.
	bMoveIssued = NavIssueLeg(MakeNavigatorMoveRequest(Navigator, Motor, DestCm));
	if (!bMoveIssued)
	{
		return false;
	}
	Navigator.GoalType = PathTypeRandom;                                    // 0x102ed4a6 0x1030ba50(path, 4)
	// `0x1030b4d0` / `0x1030b8e0` install the waypoints: the head stands, and the path's endpoint
	// (`0x1000f89e(path)`, what `nav+0x14` measures to) is the route's end. The head is not the GOAL
	// waypoint of a goal (no goal record), so the goal bit stays clear.
	Navigator.bHasHeadWaypoint = true;
	Navigator.bHeadIsGoal = false;
	Navigator.GoalPosCm = DestCm;
	Navigator.TargetOffsetCm = FVector::ZeroVector;
	Navigator.EndpointDistanceSqrUnits = static_cast<float>(
		FVector::DistSquared(Origin / ElysiumMove::U, DestCm / ElysiumMove::U)); // 0x102ed4e9 nav +0x14
	++Navigator.PathNoGoalInstalls;
	return true;
}

bool FElysiumNpcBase::StartTaskSetRandomGoal(float DistanceUnits, const FVector& Direction)
{
	using namespace ElysiumStartTaskWanderPick;

	// `CAI_Navigator::SetRandomGoal` (`0x102ed940`): nav `+0x8` := `+0x156c`, `+0xc` := the frame
	// counter (scratch words the port's mover does not keep), then `0x102ed430(this, GetOrigin()
	// (slot 220), distance, direction)`, which opens with the navigator's slot 7 reset and refuses a
	// network of no node.
	//
	// **NAMED MODERNIZATION (0018 story 4, decided by the owner 2026-09-21): the capped point pick.**
	// Retail's `0x102ff3e0` WALKS the AIN from the nearest node (`0x102f3c10`), hop by hop over
	// `0x102ff960`'s link test, until the hops add up to the distance or `0x14` steps. This draws the
	// walk's ENDPOINT instead, as a retail place chosen by retail's tests:
	//
	//   1. distance = min(order, the map's wander cap for the pathing hull) -- the cap (`20 x` the
	//      median link length, baked) stands in for the `0x14` iteration guard;
	//   2. candidates: the places within `distance` in a straight line, never a type-4 (climb) node,
	//      and slot 527's usability test (`0x1027db30`);
	//   3. two tiers by the node cooldown `+0x9c`: expired (`<= curtime`, `0x102ff3e0`'s own test)
	//      first, a cooling place only when no expired one stands;
	//   4. within the tier, the places AHEAD (positive 2-D dot against `Direction`) at `>= distance /
	//      2`; that empty, every place ahead; that empty, every candidate. A zero direction skips the
	//      ahead test;
	//   5. one `RandomInt(0, count - 1)` on the engine stream;
	//   6. Unreal's route to it on the body's own agent (`QueryRoute`): no route, or one longer
	//      than `kWanderDetourRatio x distance`, drops the pick from the pool and draws again over
	//      what remains (tier and band re-taken), `kWanderMaxDraws` in all;
	//   7. the route installed as a PATH, no goal (`InstallPathNoGoal`).
	//
	// Given up, knowingly: the dot-to-dot meander, the rotating neighbour cursor `node+0xa4`, the
	// per-step heading replacement, the visited set, and the accumulated path length (the straight
	// line checked against the route stands for it). Failure is the caller's `TaskFail(0x18)`; the
	// task completes synchronously in `StartTask`, as retail's does.
	++StartTaskNav.RandomGoalRequests;
	StartTaskNav.LastRandomGoalOrderUnits = DistanceUnits;
	StartTaskNav.LastRandomGoalNode = INDEX_NONE;
	StartTaskNav.LastRandomGoalDraws = 0;
	StartTaskNav.LastRandomGoalDistanceUnits = 0.0f;
	FUN_102eea70();                                                            // 0x102ed437 CALL [EAX+0x1c]
	const FElysiumPlaceSet* Places = World != nullptr ? &World->Places() : nullptr;
	if (Places == nullptr || Places->NumNodes() < 1)                           // 0x102ed43d CMP [ECX],0 / JG
	{
		return false;
	}
	// 1. The cap. A hull the map declares no cap for takes the human hull's figure (the bake's own
	// rule for a hull with no link); a map with no cap at all (an unbaked test world) caps nothing.
	const int32 Hull = RetailPathingHull();
	float CapUnits = Places->WanderCapUnits(Hull);
	if (CapUnits <= 0.0f)
	{
		CapUnits = Places->WanderCapUnits(ElysiumRetailHulls::DefaultHull);
	}
	const float Distance = CapUnits > 0.0f ? FMath::Min(DistanceUnits, CapUnits) : DistanceUnits;
	StartTaskNav.LastRandomGoalDistanceUnits = Distance;
	if (!(Distance > 0.0f))                                                    // 0x102ff3e0's `param_2 <= 0` refusal
	{
		return false;
	}

	// 2-3. The candidates, in two tiers.
	const float Curtime = static_cast<float>(World->NowSeconds());            // gpGlobals->curtime
	const FVector2D Ahead(Direction.X, Direction.Y);
	const bool bAheadTest = !Ahead.IsZero();
	TArray<FCandidate> Expired;
	TArray<FCandidate> Cooling;
	for (int32 Node = 0; Node < Places->NumNodes(); ++Node)
	{
		if (Places->Row(Node).Type == NodeTypeClimb)
		{
			continue;
		}
		FCandidate Candidate;
		Candidate.Node = Node;
		if (!Places->GetPositionCm(Node, Hull, Candidate.PositionCm))
		{
			continue;
		}
		const FVector Offset = Candidate.PositionCm - Origin;
		Candidate.DistanceUnits = static_cast<float>(Offset.Size() / ElysiumMove::U);
		if (Candidate.DistanceUnits > Distance || IsUnusableNodeIndex(Node))  // slot 527 via 0x1027db30
		{
			continue;
		}
		Candidate.bAhead = !bAheadTest || FVector2D::DotProduct(FVector2D(Offset.X, Offset.Y), Ahead) > 0.0;
		(Places->NodeCooldown(Node) <= Curtime ? Expired : Cooling).Add(Candidate);   // node +0x9c
	}
	// 4-7. The draws. A dropped pick leaves the candidate pool, and the tier and band are taken again
	// from what remains ("the next drawn"; "none left is TaskFail(0x18)"), so a band emptied by
	// unroutable picks falls to the next band, and the expired tier to the cooling one, exactly as an
	// empty one does -- within `kWanderMaxDraws` draws in all.
	const float LongestRouteCm = kWanderDetourRatio * Distance * ElysiumMove::U;
	for (int32 Draw = 0; Draw < kWanderMaxDraws; ++Draw)
	{
		TArray<FCandidate>& Tier = Expired.Num() > 0 ? Expired : Cooling;
		TArray<int32> Band;                                                    // indices into Tier
		for (int32 Index = 0; Index < Tier.Num(); ++Index)
		{
			if (Tier[Index].bAhead && Tier[Index].DistanceUnits >= Distance * 0.5f)
			{
				Band.Add(Index);
			}
		}
		for (int32 Index = 0; Band.Num() == 0 && Index < Tier.Num(); ++Index)
		{
			if (Tier[Index].bAhead)
			{
				Band.Add(Index);
			}
		}
		for (int32 Index = 0; Band.Num() == 0 && Index < Tier.Num(); ++Index)
		{
			Band.Add(Index);
		}
		if (Band.Num() == 0)
		{
			break;
		}
		const int32 Pick = Band[ElysiumNpcEngineRandom::RandomInt(0, Band.Num() - 1)];
		const FCandidate Candidate = Tier[Pick];
		++StartTaskNav.LastRandomGoalDraws;
		// The route is asked under the filter the walk will be searched under: the navigator's
		// pedestrian pricing when the current request carries one (a type-8 goal's `path+0x1` byte,
		// left standing: `InstallPathNoGoal` does not clear it), else the default filter (0). Retail
		// never sets the byte for this task -- `SetRandomGoal 0x102ed430` writes no goal record -- so a
		// pedestrian price here is only ever a previous goal's. Named gap: the walked leg then draws its
		// own `RandomInt(5, 10)` (`MakeNavigatorMoveRequest`), so a stale pedestrian byte can price the
		// asked route and the walked one differently; retail's single A* search has one draw.
		FElysiumNpcRouteQuery RouteAsked;
		RouteAsked.DestCm = Candidate.PositionCm;
		RouteAsked.PedestrianCostMultiplier = Navigator.bPedestrian && Navigator.bHeadLegRequestSet
			? Navigator.HeadLegRequest.PedestrianCostMultiplier : 0;
		// `QueryRoute` answers false only when it cannot ask (no mesh, no agent, no projection); a mesh
		// with no complete route answers true with `bReachable` false (and a length of 0), so the pick
		// is kept only when the route is reachable AND no longer than the detour cap.
		FElysiumNpcRouteAnswer RouteFound;
		if (Motor != nullptr && Motor->QueryRoute(RouteAsked, RouteFound) && RouteFound.bReachable
			&& RouteFound.LengthCm <= LongestRouteCm)
		{
			if (!InstallPathNoGoal(Candidate.PositionCm))
			{
				return false;
			}
			StartTaskNav.LastRandomGoalNode = Candidate.Node;
			return true;
		}
		Tier.RemoveAt(Pick);
	}
	return false;
}

bool FElysiumNpcBase::StartTaskSetWanderGoal(float MinUnits, float MaxUnits)
{
	// `CAI_Navigator::SetWanderGoal` (`0x102ed540`, `TASK_WANDER 0x76`, which no shipped schedule
	// issues -- it is only registered):
	//
	//     for (i = 0; i < 5; ++i) {
	//         dist = RandomFloat(min, max);  yaw = RandomFloat(0, 359.99);      // 0x43b3feb8
	//         dir = 0x101d2f40(yaw);                                            // the 2-D heading
	//         if (0x102ed610(this, dir, dist, ...)) return true;                 // the radial probe
	//     }
	//     return SetRandomGoal(1.0, vec3_origin);                               // 0x102ed940
	//
	// The ten draws are the engine stream's and are made. The radial probe `0x102ed610` is
	// `StartTaskWanderRadialProbe` (0018 story 6), handed the drawn distance and the task MIN as its
	// `minDist` (listing `102ed59a`; the decompiler shows min twice); the MIN reaches it through
	// `StartTaskNav.LastSearchMinUnits`, written above. When all five fail the fallback is the capped
	// point pick above, called as retail calls it. Under the pick an order of `1.0` unit finds no
	// place (retail's walk always takes one hop, because it stops only AFTER a step), so the fallback
	// fails `0x18` where retail would reach a neighbouring node: part of the named modernization, and
	// unreached by any shipped schedule.
	++StartTaskNav.WanderGoalRequests;
	StartTaskNav.LastSearchMinUnits = MinUnits;
	StartTaskNav.LastSearchMaxUnits = MaxUnits;
	constexpr int32 RadialTries = 5;
	constexpr float YawMax = 359.99f;                                          // 0x43b3feb8
	constexpr float FallbackDistanceUnits = 1.0f;                              // 0x102ed5bb PUSH 0x3f800000
	FRandomStream& Stream = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule);
	for (int32 Try = 0; Try < RadialTries; ++Try)
	{
		const float DistanceUnits = Stream.FRandRange(MinUnits, MaxUnits);    // 0x102ed566 DAT_1070b244 +4
		const float YawDegrees = Stream.FRandRange(0.0f, YawMax);              // 0x102ed57c
		if (StartTaskWanderRadialProbe(YawDegrees, DistanceUnits))             // 0x102ed59e -> 0x102ed610
		{
			return true;
		}
	}
	return StartTaskSetRandomGoal(FallbackDistanceUnits, FVector::ZeroVector); // 0x102ed5c2, vec3_origin 0x1070d1b0
}

bool FElysiumNpcBase::StartTaskWanderRadialProbe(float YawDegrees, float DistanceUnits)
{
	using namespace ElysiumStartTask19Base;
	// `0x102ed610(dir, dist, minDist, skip)` (R1 §6), the wander's radial probe along one heading:
	//
	//     end = GetOrigin() (slot 220) + dist * dir;
	//     MoveLimit(nav+0x18, origin, end, 0x2400b, NULL, 100.0, 0, &tr, skip, NULL);   // NPCs not solid
	//     if (blocked && dist - tr.flDistObstructed <= minDist) return false;          // 102ed6df
	//     SetGoal(AI_NavGoal_t(GOALTYPE_LOCATION, tr.vEndPosition), 0);                 // flags 0
	//
	// The walk is the named modernization `MotorMoveTraceSweep` states for the ground arm: the body's
	// NavMesh raycast (`IElysiumNpcMotor::NavRaycast`, default filter -- the ground test prices
	// nothing). A hit is where the walk left the mesh; `flDistObstructed` is the 2-D distance still to
	// go from there, so `dist - flDistObstructed` is how far the heading got. A motor with no NavMesh
	// answers nothing, and the probe refuses (this seam's answer before 0018 story 6), so the caller
	// reaches its fallback pick.
	const double U = ElysiumMove::U;
	const float MinDistUnits = StartTaskNav.LastSearchMinUnits;             // SetWanderGoal's task MIN
	// `0x101d2f40(yaw)`: the 2-D heading `(cos, sin, 0)` in retail's axes; the port's Y is negated.
	const double Radians = FMath::DegreesToRadians(static_cast<double>(YawDegrees));
	const FVector Heading(FMath::Cos(Radians), -FMath::Sin(Radians), 0.0);
	const FVector EndCm = Origin + Heading * (static_cast<double>(DistanceUnits) * U);
	FElysiumNpcNavRaycast Ray;
	Ray.FromCm = Origin;
	Ray.ToCm = EndCm;
	FElysiumNpcNavRaycastAnswer Answer;
	if (Motor == nullptr || !Motor->NavRaycast(Ray, Answer))
	{
		return false;
	}
	FVector ReachedCm = EndCm;
	if (Answer.bHit)
	{
		ReachedCm = Answer.HitCm;
		const double ObstructedUnits = FVector::Dist2D(ReachedCm, EndCm) / U;
		if (static_cast<double>(DistanceUnits) - ObstructedUnits <= static_cast<double>(MinDistUnits))
		{
			return false;
		}
	}
	FStartTaskNavGoal Goal;
	Goal.Type = GOALTYPE_LOCATION;
	Goal.DestCm = ReachedCm;
	Goal.bDestSet = true;
	Goal.GoalFlags = 0;
	return StartTaskSetGoal(Goal, 0);
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
	++StartTaskNav.LateralCoverTests;
	// The LINE from the threat's eye to the point at THIS NPC's standing eye height (`0x10278294`
	// Ray_t::Init with point + `m_vecViewOffset` +0x184..+0x18c; `0x102782b8` enginetrace slot 4
	// TraceRay), mask `0x2804091`, filter `CTraceFilterSimpleTwoEnt(this, ignore, 0)` (`0x101ccd70`):
	// the line must be BLOCKED -- `fraction == 1.0` rejects (`102782f8`). R2 §5: `TwoEnt`'s
	// `ShouldHitEntity 0x101ccda0` has no `StandardFilterRules`, no BCC/hidden gate and no
	// NPC-transparent gate, so third-party NPCs block this line (`bNpcsBlock`), unlike `FVisible`.
	// No collision world means no cover. `0x102782c0` ConVar 0x10738960 test / `0x102782c7` JZ /
	// `0x102782e9` the debug-overlay line of the trace: not ported (a developer overlay).
	//
	// `TwoEnt`'s two pass entities onto `ElysiumNpcSight::Visible`: THIS NPC (`Looker`) and
	// `ignore` (`SecondIgnore`, `0x10278239`). There is no target: the verdict is only clear or
	// blocked.
	IElysiumEmbodiment* Embodiment = World != nullptr ? World->Embodiment() : nullptr;
	if (Embodiment == nullptr)
	{
		return false;
	}
	ElysiumNpcSight::FVisibleQuery Line;
	Line.EyeCm = ThreatEyeCm;
	Line.TargetCm = PointCm + (EyePosition() - Origin);
	Line.Mask = LateralCoverSightMask;
	Line.Looker = Handle;
	Line.SecondIgnore = Ignore != nullptr ? Ignore->Handle : FElysiumEntityHandle::Invalid();
	Line.World = World;
	Line.bNpcsBlock = true;
	if (ElysiumNpcSight::Visible(*Embodiment, Line, nullptr))
	{
		return false;
	}
	if (!IsValidCover(PointCm, nullptr))                                 // slot 548 (`0x10278310`)
	{
		return false;
	}
	// `0x10278336` slot 220 GetAbsOrigin as the probe start; `0x10278359` MoveLimit(NAV_GROUND,
	// start, point, mask 0x202400b, no target, 100.0).
	if (Motor == nullptr || !Motor->CanReachLateralCover(PointCm))       // MoveLimit, mask 0x202400b
	{
		return false;
	}
	FStartTaskNavGoal Goal;
	Goal.Type = GOALTYPE_LOCATION;
	Goal.DestCm = PointCm;
	Goal.bDestSet = true;
	Goal.MovementActivity = ACT_RUN;
	Goal.ToleranceUnits = ElysiumNpcTunables::MinusOneAt94A0;                        // `_DAT_104994a0` = -1.0
	return StartTaskSetGoal(Goal, 1);
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
	// `0x102d12e0`: a hint bound to a network node answers the node's yaw (`0x102f47b0` on the
	// network `DAT_1093407c`); an unbound one answers its own `GetAngles().y` (slot 221). Both arms
	// are family Hints' `HintYaw`, the one body.
	return HintYaw(HintNode, OutYaw);
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
	// `CBaseCombatCharacter::ChooseBestMeleeWeapon 0x10337230`: `GetBestMeleeWeapon 0x10336f20`; a
	// weapon -> owner slot 388 (`+0x610`) `Weapon_Switch(weapon, 0)` and true; none -> false.
	//
	// `GetBestMeleeWeapon`: the type list `0x10619eb4` = `{1, 2, 3, 7, -1}`; for a type whose section
	// number `0x10619d28[type]` is `>= 0`, weapon slots `[0x10937cd0[type], + owner slot 298
	// (+0x4a8)(section))`, the first `GetWeapon(i)` whose slot 360 (`+0x5a0`) `& 0x18000` wins. The
	// two tables are the inventory sections of `vdata\system\items.txt` § `InventorySections`
	// (`CacheInventorySections 0x10340180`, packet S5 item 6): types 1, 2, 3, 7 are `Weapon_Melee`,
	// `Weapon_Ranged`, `Weapon_Thrown`, `Hidden`, 32 slots each from 0, 32, 64, 192.
	//
	// The port's inventory is ONE compact list (`FElysiumInventory::Slots`), not eight 32-slot
	// sections: an item's section is its record's type (`ElysiumSectionForItemType`), and its place
	// inside the section is its place in the list. The walk is therefore the four sections in
	// retail's type order, each over the list in position order. UNVERIFIED: that the list's order
	// inside one section equals retail's section-slot order (both are acquisition order as far as
	// the port's `Inventory_Add` goes).
	static const EElysiumInvSection GBestMeleeSections[] = {
		EElysiumInvSection::WeaponMelee,    // type 1, section 0, slots [0, n)
		EElysiumInvSection::WeaponRanged,   // type 2, section 1, slots [32, 32 + n)
		EElysiumInvSection::WeaponThrown,   // type 3, section 2, slots [64, 64 + n)
		EElysiumInvSection::Hidden,         // type 7, section 6, slots [192, 192 + n)
	};
	FElysiumItem* Best = nullptr;
	for (const EElysiumInvSection Section : GBestMeleeSections)
	{
		for (int32 Position = 0; Position < Inventory.Num() && Best == nullptr; ++Position)
		{
			FElysiumItem* const Item = Inventory.At(*this, Position);
			const FElysiumWeapon* const Weapon = Item != nullptr ? Item->AsWeapon() : nullptr;
			const FElysiumItemDef* const Record = Weapon != nullptr ? Weapon->Data() : nullptr;
			if (Record == nullptr || ElysiumSectionForItemType(Record->Type) != Section)
			{
				continue;
			}
			// Slot 360 `& 0x18000`: the port's melee capability is a controllable weapon whose
			// record is the melee family (`ElysiumNpcCond::WeaponCapability`'s own test).
			if (Record->IsControllableWeapon() && Record->Type == EElysiumItemType::WeaponMelee)
			{
				Best = Item;
			}
		}
		if (Best != nullptr)
		{
			break;
		}
	}
	if (Best == nullptr)
	{
		return false;   // 0x10337230: no weapon -> 0 -> FAIL_NO_WEAPON_TO_CHOOSE
	}
	// Slot 388 `Weapon_Switch(weapon, 0)` (`0x1032dde0`) is still a generated stub; the port's
	// active-weapon switch is the inventory's. Retail answers true whatever the switch answers.
	(void)Inventory.SetActiveWeapon(*this, *Best);
	return true;
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
