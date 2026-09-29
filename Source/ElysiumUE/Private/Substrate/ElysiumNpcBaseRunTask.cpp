// Story 0019/8 (29e under the strict verdict), family **RunTask19** -- `CAI_BaseNPC`'s bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcBaseRunTask.inl` (included inside `class FElysiumNpcBase`) or
// generated in `ElysiumNpcBaseSlots.inl` for a slot body.
//
// Owns (RunTask19's `rule` rows): 0x10288780 CAI_BaseNPC::RunTask.
//
// Lane L05 (pass I). The body is read off the listing (`vtmb_asm 0x10288780`) arm by arm; the
// packet's merged walk and its two judges' corrections decided every branch sense. Walked prose:
// `docs/vtmb/npc-ai/schedule-kernel.md` § "`CAI_BaseNPC::RunTask` `0x10288780`".

#include "Substrate/ElysiumNpcBase.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcFacingShared.h"
#include "Substrate/ElysiumNpcLifecycle2Shared.h"
#include "Substrate/ElysiumNpcMotor2Shared.h"
#include "Substrate/ElysiumScheduleText.h"
#include "Substrate/ElysiumScriptedSequence.h"
#include "Substrate/ElysiumWeaponClasses.h"

namespace RunTask19Base
{
	// The source file every fail trace of this body stamps (`0x105cde88`).
	const TCHAR* const GFile = TEXT("E:\\Vampire\\main\\dlls\\AI_BaseNPC_Schedule.cpp");

	// Task ids (the registrar `FUN_10316ff0`, `walk-19-29-pack-08.md` / `-13.md`). The jump table is
	// `0x10289724` indexed by the byte table `0x10289794` over `id - 2`, ids `2..0xb1`.
	constexpr int32 TaskWait = 0x02;                          // TASK_WAIT
	constexpr int32 TaskWaitFaceEnemy = 0x04;                 // TASK_WAIT_FACE_ENEMY
	constexpr int32 TaskWaitPvs = 0x05;                       // TASK_WAIT_PVS
	constexpr int32 TaskMoveToTargetRange = 0x0b;             // TASK_MOVE_TO_TARGET_RANGE
	constexpr int32 TaskGetPathToRandomNode = 0x1f;           // TASK_GET_PATH_TO_RANDOM_NODE
	constexpr int32 TaskWalkPathTimed = 0x24;                 // TASK_WALK_PATH_TIMED
	constexpr int32 TaskWalkPathWithinDist = 0x25;            // TASK_WALK_PATH_WITHIN_DIST
	constexpr int32 TaskRunPathWithinDist = 0x26;             // TASK_RUN_PATH_WITHIN_DIST
	constexpr int32 TaskRunPathTimed = 0x27;                  // TASK_RUN_PATH_TIMED
	constexpr int32 TaskSmallFlinch = 0x2a;                   // TASK_SMALL_FLINCH
	constexpr int32 TaskFaceIdeal = 0x2b;                     // TASK_FACE_IDEAL
	constexpr int32 TaskFacePath = 0x2c;                      // TASK_FACE_PATH
	constexpr int32 TaskFacePlayer = 0x2d;                    // TASK_FACE_PLAYER
	constexpr int32 TaskFaceEnemy = 0x2e;                     // TASK_FACE_ENEMY
	constexpr int32 TaskFaceHintNode = 0x2f;                  // TASK_FACE_HINTNODE
	constexpr int32 TaskPlayHintActivity = 0x30;              // TASK_PLAY_HINT_ACTIVITY
	constexpr int32 TaskFaceTarget = 0x31;                    // TASK_FACE_TARGET
	constexpr int32 TaskFaceLastPosition = 0x32;              // TASK_FACE_LASTPOSITION
	constexpr int32 TaskRangeAttack1 = 0x34;                  // TASK_RANGE_ATTACK1
	constexpr int32 TaskRangeAttack2 = 0x35;                  // TASK_RANGE_ATTACK2
	constexpr int32 TaskMeleeAttack1 = 0x36;                  // TASK_MELEE_ATTACK1
	constexpr int32 TaskMeleeAttack2 = 0x37;                  // TASK_MELEE_ATTACK2
	constexpr int32 TaskReload = 0x38;                        // TASK_RELOAD
	constexpr int32 TaskRangeAttack1NoTurn = 0x39;            // TASK_RANGE_ATTACK1_NOTURN
	constexpr int32 TaskRangeAttack2NoTurn = 0x3a;            // TASK_RANGE_ATTACK2_NOTURN
	constexpr int32 TaskMeleeAttack1NoTurn = 0x3b;            // TASK_MELEE_ATTACK1_NOTURN
	constexpr int32 TaskMeleeAttack2NoTurn = 0x3c;            // TASK_MELEE_ATTACK2_NOTURN
	constexpr int32 TaskReloadNoTurn = 0x3d;                  // TASK_RELOAD_NOTURN
	constexpr int32 TaskSpecialAttack1 = 0x3e;                // TASK_SPECIAL_ATTACK1
	constexpr int32 TaskSpecialAttack2 = 0x3f;                // TASK_SPECIAL_ATTACK2
	constexpr int32 TaskSetActivity = 0x4b;                   // TASK_SET_ACTIVITY
	constexpr int32 TaskPlaySequence = 0x52;                  // TASK_PLAY_SEQUENCE
	constexpr int32 TaskPlayPrivateSequence = 0x53;           // TASK_PLAY_PRIVATE_SEQUENCE
	constexpr int32 TaskPlayPrivateSequenceFaceEnemy = 0x54;  // TASK_PLAY_PRIVATE_SEQUENCE_FACE_ENEMY
	constexpr int32 TaskPlaySequenceFaceEnemy = 0x55;         // TASK_PLAY_SEQUENCE_FACE_ENEMY
	constexpr int32 TaskPlaySequenceFaceTarget = 0x56;        // TASK_PLAY_SEQUENCE_FACE_TARGET
	constexpr int32 TaskDie = 0x5f;                           // TASK_DIE
	constexpr int32 TaskWaitForScript = 0x60;                 // TASK_WAIT_FOR_SCRIPT
	constexpr int32 TaskPlayScript = 0x62;                    // TASK_PLAY_SCRIPT
	constexpr int32 TaskPlayScriptPostIdle = 0x63;            // TASK_PLAY_SCRIPT_POST_IDLE
	constexpr int32 TaskFaceScript = 0x66;                    // TASK_FACE_SCRIPT
	constexpr int32 TaskWaitRandom = 0x67;                    // TASK_WAIT_RANDOM
	constexpr int32 TaskWaitIndefinite = 0x68;                // TASK_WAIT_INDEFINITE
	constexpr int32 TaskStopMoving = 0x69;                    // TASK_STOP_MOVING
	constexpr int32 TaskTurnLeft = 0x6a;                      // TASK_TURN_LEFT
	constexpr int32 TaskTurnRight = 0x6b;                     // TASK_TURN_RIGHT
	constexpr int32 TaskWaitForMovement = 0x6e;               // TASK_WAIT_FOR_MOVEMENT
	constexpr int32 TaskWaitForMovementStep = 0x6f;           // TASK_WAIT_FOR_MOVEMENT_STEP
	constexpr int32 TaskWeaponPickup = 0x71;                  // TASK_WEAPON_PICKUP
	constexpr int32 TaskWeaponRunPath = 0x72;                 // TASK_WEAPON_RUN_PATH
	constexpr int32 TaskFallToGround = 0x74;                  // TASK_FALL_TO_GROUND
	constexpr int32 TaskWander = 0x76;                        // TASK_WANDER
	constexpr int32 TaskFreeze = 0x77;                        // TASK_FREEZE
	constexpr int32 TaskWaitAttackTime1 = 0xb0;               // TASK_WAIT_ATTACK_TIME1
	constexpr int32 TaskWaitAttackTime2 = 0xb1;               // TASK_WAIT_ATTACK_TIME2

	// `TaskFail` reasons (`0x106152b0`).
	constexpr int32 FailNoTarget = 1;
	constexpr int32 FailWeaponOwned = 2;
	constexpr int32 FailWeaponMissing = 3;
	constexpr int32 FailNoHintNode = 4;
	constexpr int32 FailNoPlayer = 0x17;
	constexpr int32 FailStuckOnTop = 0x1c;

	// The `+0x1b48` source lines.
	constexpr int32 LineNoHintNode = 0xb5c;
	constexpr int32 LineStuckOnTop = 0xb81;
	constexpr int32 LineNoPlayer = 0xbc7;
	constexpr int32 LineNoTarget = 0xc09;
	constexpr int32 LineWeaponOwnedRunPath = 0xc30;
	constexpr int32 LineWeaponMissing = 0xc3a;
	constexpr int32 LineWeaponOwnedPickup = 0xd35;

	// Motor yaw speeds (the pushed immediates `0xc0000000` / `0xbf800000`).
	constexpr float YawSpeedHold = -2.0f;
	constexpr float YawSpeedDefault = -1.0f;
	constexpr int32 UpdateYawDefault = -1;

	// `SF_NPC_ALWAYSTHINK`, spawnflag bit 10 (`SHR EAX,0xa; TEST AL,1`).
	constexpr int32 SpawnFlagAlwaysThink = 0x400;
	// Slot 513's bit the attack arm tests (`TEST EAX,0x20000000`, `0x1028923d`).
	constexpr int32 CapabilityAimHold = 0x20000000;
	// `COND_NO_PRIMARY_AMMO` / `COND_NO_SECONDARY_AMMO`, cleared by `TASK_RELOAD`.
	constexpr int32 CondNoSecondaryAmmo = 0x41;

	// `_DAT_1044e664` f32 = 10.0 -- `TASK_FACE_PLAYER`'s remaining-yaw bound. Not in the tunables
	// table (the table carries the pooled 5.0 and 30.0, not this cell).
	constexpr float FacePlayerYawBound = 10.0f;
	// `m_lifeState` `LIFE_DEAD`.
	constexpr int32 LifeDead = 2;
	// `TASK_DIE`'s hull (`0x1028901b..0x1028904c`): maxs `(4,4,1)`, mins `(-4,-4,0)`.
	constexpr float DeathHullHalf = 4.0f;
	// `CSoundEnt::InsertSound(SOUND_CARCASS 0x20, origin, 0x180, 30.0, 0)` (`0x102890cc..0x102890e1`).
	constexpr int32 SoundCarcass = 0x20;
	constexpr int32 SoundCarcassVolume = 0x180;
	// NAV types (`0x1027d990`): `NAV_GROUND` 0, `NAV_JUMP` 1, `NAV_CLIMB` 3.
	constexpr int32 NavGround = 0;
	constexpr int32 NavJump = 1;
	constexpr int32 NavClimb = 3;
}

// --- The motor and navigator primitives (`ElysiumNpcBaseRunTask.inl`) ------------------------------

void FElysiumNpcBase::MotorMoveStop()
{
	// `0x102e0b40`: `*(motor + 0x2c) = 0xbf800000`. The whole body.
	MotorYawClock = -1.0f;
}

void FElysiumNpcBase::MotorSetIdealYawAndUpdate(float YawDegrees, float YawSpeed)
{
	// `0x102e1c10`. The `+0x28` animation-movement latch turns the yaw half a turn.
	float Ideal = YawDegrees;
	if (BaseScheduleHost.bMotorAnimationMovement)
	{
		Ideal = Ideal < ElysiumNpcTunables::OneEighty ? Ideal + ElysiumNpcTunables::OneEighty
			: Ideal - ElysiumNpcTunables::OneEighty;
	}
	// `+0x1c == 180.0` stores it directly; the clamped arm `0x102e0a80` needs a max-yaw word the
	// port motor does not carry, so the direct store is the arm taken.
	MotorIdealYaw = Ideal;
	// The seam recorder family BaseHelpers already stands over this call.
	++TroikaMotor.MoveReissues;
	TroikaMotor.LastReissueYaw = Ideal;
	TroikaMotor.LastReissueSpeed = YawSpeed;
	if (YawSpeed != -1.0f)
	{
		if (YawSpeed != -2.0f)
		{
			MotorYawSpeedWord = YawSpeed;   // `+0x38` (`0x102e1ca8`)
		}
	}
	// With `-1.0` retail first runs `0x102e1cf0` (unrecovered; SEAM: nothing), then both arms end in
	// `UpdateYaw(-1)`.
	MotorUpdateYaw(-1);
}

void FElysiumNpcBase::MotorSetIdealYawToTargetAndUpdate(const FVector& TargetCm, float YawSpeed)
{
	// `0x102e20b0`: `0x102e2750` -- `JMP [outer vtbl+0x80c]`, slot 515 `CalcIdealYaw` (`0x10274b30`,
	// `VecToYaw` of the delta; 0 for a zero one) -- then `0x102e1c10`.
	const float Yaw = CalcIdealYaw(TargetCm);
	MotorSetIdealYawAndUpdate(Yaw, YawSpeed);
}

void FElysiumNpcBase::MotorUpdateYaw(int32 YawSpeed)
{
	// `0x102e1e20`. The clock restart below `0.0` and the stamp are retail's; the step is Unreal's
	// (named modernization, see the declaration). `Face` takes this world's (negated) yaw.
	(void)YawSpeed;
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	++MotorUpdateYawCalls;
	MotorYawClock = static_cast<float>(Now);
	if (Motor != nullptr)
	{
		Motor->Face(-MotorIdealYaw);
	}
}

void FElysiumNpcBase::NavUpdateGoalPos(const FVector& GoalCm)
{
	// `0x102ee220` -- SEAM (declaration).
	NavUpdatedGoalCm = GoalCm;
	++NavUpdateGoalCalls;
}

void FElysiumNpcBase::NavSetMovementActivity(int32 Activity)
{
	// `0x102ee250`: `m_pPath->m_movementActivity = act`.
	if (FElysiumNpc* Troika = AsNpc())
	{
		Troika->Navigator.MovementActivity = Activity;
	}
}

void FElysiumNpcBase::NavClearGoal()
{
	// `0x102ee270`, the landed idiom (`ElysiumNpcBaseLifecycle2.cpp` `NPCInit`).
	if (Motor != nullptr)
	{
		Motor->ClearNavigationGoal();
	}
	++NavigationGoalClears;
}

bool FElysiumNpcBase::DeathHullIsFlat() const
{
	// `0x10279420` -- SEAM (declaration).
	return false;
}

void FElysiumNpcBase::InsertAiSound(int32 Type, const FVector& OriginCm, int32 Volume, float Duration)
{
	// `0x101babc0` -- SEAM (declaration).
	(void)OriginCm;
	InsertedAiSoundType = Type;
	InsertedAiSoundVolume = Volume;
	InsertedAiSoundDuration = Duration;
}

void FElysiumNpcBase::StartFadeOut()
{
	// `0x102695d0` -- SEAM (declaration).
	++StartFadeOutCalls;
}

void FElysiumNpcBase::WeaponFinishReload(FElysiumEntity& Weapon)
{
	// `weapon+0x898 = 1` then weapon slot 322 -- SEAM (declaration).
	(void)Weapon;
	++WeaponFinishReloadCalls;
}

FElysiumEntity* FElysiumNpcBase::TargetWeaponOwner(FElysiumEntity* TargetEntity) const
{
	// `__RTDynamicCast(target, CBaseEntity -> CBaseCombatWeapon)` then `0x102521f0`: the weapon's
	// `m_hOwner` (`+0x88c`) resolved, answered as its combat character (`+0x9c`). The port's weapon
	// answers its owner as that character directly.
	FElysiumItem* Item = TargetEntity != nullptr ? TargetEntity->AsItem() : nullptr;
	FElysiumWeapon* Weapon = Item != nullptr ? Item->AsWeapon() : nullptr;
	return Weapon != nullptr ? Weapon->OwnerCharacter() : nullptr;
}

bool FElysiumNpcBase::IsOnGroundFlag() const
{
	if (Motor != nullptr)
	{
		return Motor->SampleNavigation().bGrounded;
	}
	return (static_cast<uint32>(Flags) & NpcKernelLifecycle19Shared::GFlOnGround) != 0;
}

// Slot 444: `CAI_BaseNPC::RunTask` `0x10288780`, 4,002 bytes. `Task` is the running step.
int32 FElysiumNpcBase::RunTaskSlot444(void* Task)
{
	using namespace RunTask19Base;
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	if (Step == nullptr)
	{
		// Crash guard: retail dereferences the task at `0x1028878a`.
		// same arm: 0x102887a6 JMP
		return 0;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	auto Fail = [this](int32 Line, int32 Reason)
	{
		// `+0x1b44 = "E:\Vampire\main\dlls\AI_BaseNPC_Schedule.cpp"`, `+0x1b48 = line`, then slot 448.
		RecordScheduleEvent(FString::Printf(TEXT("RunTask fail trace %s:%d"), GFile, Line));
		TaskFail(Reason);
	};
	// `this->GetEnemy()` through slot 167 (the const overload, `vtable+0x29c`).
	auto EnemySlot167 = [this]() -> FElysiumEntity*
	{
		return static_cast<const FElysiumNpcBase*>(this)->GetEnemy();
	};
	// `GetEnemies()->LastKnownPosition(enemy)` (`0x102dfed0`), called with a NULL enemy too: the
	// enemy's record, else the last position-only record ("danger pos"), else `vec3_origin`.
	// (L05 integration: was `EnemyLastKnownPosition`, whose no-enemy arm answered the zero vector
	// without the record walk.)
	auto EnemyLkp = [this]() -> FVector
	{
		return Conditions19LastKnownPosition(static_cast<const FElysiumNpcBase*>(this)->GetEnemy());
	};
	// `0x102ee140` (the goal point). The arms below compare it with `Here` / `TargetUnits`, which are
	// Source-frame (`MotorTailSourceOf`, Y reflected), while `NavGoalPosition` answers the port frame
	// (`Origin / U`, no reflection); the Y is reflected here so the two agree (`0x10288cc1`, `0x102895dc`).
	auto GoalUnits = [this]() -> FVector
	{
		FVector Goal = FVector::ZeroVector;
		NavGoalPosition(Goal);
		Goal.Y = -Goal.Y;
		return Goal;
	};
	// `[EDI]` is retail's `Task_t::iTask`, the CLASS-LOCAL id; the step carries the GLOBAL id (stated
	// divergence, `ElysiumScheduleText.h`), translated once here through slot 450 `GetLocalTaskId`
	// (`0x101a6640`) exactly as `StartTaskSlot442` does (the StartTask19 integration's fix).
	const int32 Id = GetLocalTaskId(Step->TaskId);                                 // 0x1028878a slot 450
	switch (Id)
	{
	case TaskWait:
	case TaskWaitRandom:
		// `0x10288bb6`: `10288c1d FCOMP [+0x5db4]` / `10288c25 AND EAX,0x100`: below or UNORDERED keeps
		// running, so only an ordered `curtime >= m_flWaitFinished` completes.
		if (Now >= BaseScheduleHost.WaitFinished)                                  // 0x10288c2a
		{
			TaskComplete(false);                                                   // 0x10288c34
		}
		return 0;

	case TaskWaitFaceEnemy:
	case TaskWaitAttackTime1:
	case TaskWaitAttackTime2:
	{
		MotorMoveStop();                                                           // 0x10288bc7
		// same arm: 0x10288bd0 CALL, 0x10288be0 CALL
		const FVector Lkp = EnemyLkp();                                            // 0x10288be8
		if (!FInAimCone(Lkp))                                                      // 0x10288bf6
		// same arm: 0x10288bfe JNZ
		{
			MotorSetIdealYawToTargetAndUpdate(Lkp, YawSpeedHold);                  // 0x10288c10
		}
		if (Now >= BaseScheduleHost.WaitFinished)                                  // 0x10288c1d / 0x10288c2a (unordered runs)
		{
			TaskComplete(false);                                                   // 0x10288c34
		}
		return 0;
	}

	case TaskWaitPvs:
		if ((SpawnFlags & SpawnFlagAlwaysThink) != 0)                               // 0x10288b86
		{
			TaskComplete(false);                                                   // 0x10289713
			return 0;
		}
		if (Conditions19ClientInPvs())                                             // 0x10288b93 / 0x10288b9d
		{
			TaskComplete(false);                                                   // 0x10288ba7
		}
		return 0;

	case TaskMoveToTargetRange:
	{
		FElysiumEntity* TargetEntity = World != nullptr ? World->Resolve(TargetEnt) : nullptr;
		if (TargetEntity == nullptr)                                                     // 0x10288c4c..0x10288c6f
		// same arm: 0x10288c69 JNZ
		{
			Fail(LineNoTarget, FailNoTarget);                                      // 0x10288c8b
			return 0;
		}
		const float Range = ResolveTaskDistance(Step->Data);                       // 0x10288ca3
		const FVector Here = NpcKernelMotor2Shared::MotorTailSourceOf(Origin);    // 0x10288cb7
		FVector Goal = GoalUnits();                                                // 0x10288cc1
		float Distance = NpcKernelMotor2Shared::Length2D(Goal - Here);             // 0x10288ce2
		bool bReaim = Distance < Range;                                            // 0x10288cf8 JNP
		// same arm: 0x10288d07 JZ, 0x10288d24 JNZ, 0x10288d34 CALL
		if (!bReaim)
		{
			const FVector TargetUnits = NpcKernelMotor2Shared::MotorTailSourceOf(TargetEntity->Origin);
			Goal = GoalUnits();                                                    // 0x10288d3e
			const float Drift = NpcKernelMotor2Shared::Length3D(Goal - TargetUnits); // 0x10288d6d
			bReaim = Range * ElysiumNpcTunables::HalfDouble < Drift;              // 0x10288d77 / 0x10288d87
		}
		if (bReaim)
		{
			// `0x10288d8d`: the distance becomes the 2-D distance to the target, and the goal is
			// same arm: 0x10288d96 JZ, 0x10288db3 JNZ, 0x10288dbf CALL, 0x10288dcb CALL
			// re-aimed at the target's `GetAbsOrigin`.
			const FVector TargetUnits = NpcKernelMotor2Shared::MotorTailSourceOf(TargetEntity->Origin);
			Distance = NpcKernelMotor2Shared::Length2D(TargetUnits - Here);        // 0x10288ded
			// same arm: 0x10288e03 JZ, 0x10288e20 JNZ, 0x10288e30 CALL
			NavUpdateGoalPos(TargetEntity->Origin);                                      // 0x10288e39
		}
		if (Distance < Range)                                                      // 0x10288e4d JNP
		{
			TaskComplete(false);                                                   // 0x10288eff
			NavClearGoal();                                                        // 0x10288f0a
			return 0;
		}
		const int32 Activity = Slot571(Distance);                                  // 0x10288e5a
		NavSetMovementActivity(Activity);                                          // 0x10288e69
		SetIdealActivity(Activity);                                                // 0x10288e71
		return 0;
	}

	case TaskGetPathToRandomNode:
	case TaskWaitIndefinite:
	case TaskWander:
	case TaskFreeze:
		// Table entry `0x10289718`: the epilogue. The task keeps running.
		return 0;

	case TaskWalkPathTimed:
	case TaskRunPathTimed:
		// `0x10289614`: `1028961d FCOMP` / `10289625 AND EAX,0x4100` / `1028962a JZ`: only an ordered
		// `curtime > m_flWaitFinished` goes straight to the stop; at or below it, or unordered, the goal
		// test decides. Otherwise stop, complete and clear the goal.
		if (!(Now > BaseScheduleHost.WaitFinished) && NavIsGoalActive())         // 0x1028962a / 0x10289639
		// same arm: 0x10289632 CALL
		{
			return 0;
		}
		BaseScheduleHost.bShouldMove = false;                                      // 0x1028963f
		TaskComplete(false);                                                       // 0x10288eff
		NavClearGoal();                                                            // 0x10288f0a
		return 0;

	case TaskWalkPathWithinDist:
	case TaskRunPathWithinDist:
	{
		const FVector Here = NpcKernelMotor2Shared::MotorTailSourceOf(Origin);    // 0x102895a3
		// same arm: 0x102895ad CALL
		const float Distance = NpcKernelMotor2Shared::Length3D(Here - GoalUnits()); // 0x102895dc
		const float Range = ResolveTaskDistance(Step->Data);                       // 0x102895f1
		if (!(Range >= Distance))                                                  // 0x102895f7 FCOMP / 0x102895fd AND 0x100 / 0x10289602 (unordered runs)
		{
			return 0;
		}
		BaseScheduleHost.bShouldMove = false;                                      // 0x10289608
		TaskComplete(false);                                                       // 0x10288eff
		NavClearGoal();                                                            // 0x10288f0a
		return 0;
	}

	case TaskSmallFlinch:
		if (IsActivityFinished())                                                  // 0x102891d3
		{
			TaskComplete(false);                                                   // 0x102891e5
		}
		return 0;

	case TaskFaceIdeal:
	case TaskFacePath:
	case TaskFaceHintNode:
	case TaskFaceTarget:
	case TaskFaceLastPosition:
	case TaskFaceScript:
		MotorUpdateYaw(UpdateYawDefault);                                          // 0x10288b54
		if (FacingIdeal())                                                         // 0x10288b5b
		// same arm: 0x10288b62 JZ
		{
			TaskComplete(false);                                                   // 0x10288b6c
		}
		return 0;

	case TaskFacePlayer:
	{
		// `engine->PEntityOfEntIndex(1)`, else (`0x10288a7c JNZ` not taken) `PEntityOfEntIndex(0)` --
		// the world edict -- then `CBaseEntity::Instance` (`0x10004d13`). With no player retail faces
		// WORLDSPAWN's origin, `(0,0,0)`; the port stands no world entity, so its origin is the
		// constant. The `TaskFail(0x17)` at `0x10288b22..0x10288b3c` needs the world edict to have no
		// entity, which a loaded map never has; it is carried unreachable. (L05 integration: the port
		// failed the task whenever the player was absent.)
		FElysiumPlayer* Player = World != nullptr ? World->FindPlayer() : nullptr; // 0x10288a22..0x10288a8e
		// same arm: 0x10288a7c JNZ, 0x10288a87 CALL
		const bool bWorldEntityMissing = World == nullptr;                         // 0x10288aa4
		if (bWorldEntityMissing)
		{
			Fail(LineNoPlayer, FailNoPlayer);                                      // 0x10288b3c
			return 0;
		}
		const FVector FacePoint = Player != nullptr ? Player->Origin : FVector::ZeroVector; // 0x10288ac0 slot 217
		MotorMoveStop();                                                           // 0x10288aac
		// same arm: 0x10288ac0 CALL
		MotorSetIdealYawToTargetAndUpdate(FacePoint, YawSpeedHold);                // 0x10288ac9
		SetTurnActivity();                                                         // 0x10288ad2
		if (Now > BaseScheduleHost.WaitFinished                                    // 0x10288aed
			&& MotorDeltaIdealYaw() < FacePlayerYawBound)                          // 0x10288af9 / 0x10288b09
		{
			TaskComplete(false);                                                   // 0x10288b13
		}
		return 0;
	}

	case TaskFaceEnemy:
	{
		MotorMoveStop();                                                           // 0x102889bb
		// same arm: 0x102889c4 CALL, 0x102889d4 CALL
		const FVector Lkp = EnemyLkp();                                            // 0x102889dc
		MotorSetIdealYawToTargetAndUpdate(Lkp, YawSpeedDefault);                   // 0x102889f1
		if (FacingIdeal())                                                         // 0x102889f8
		// same arm: 0x102889ff JZ
		{
			TaskComplete(false);                                                   // 0x10288a09
		}
		return 0;
	}

	case TaskPlayHintActivity:
	{
		if (BaseScheduleHost.HintNode == INDEX_NONE)                               // 0x10288877
		{
			Fail(LineNoHintNode, FailNoHintNode);                                  // 0x10288893
			// Crash guard: retail goes on to read `m_pHintNode->m_hHintOwner` through the null
			// pointer (`0x10288899..0x102888ac`) and faults.
			return 0;
		}
		FHintWords Words;
		if (HintWords(BaseScheduleHost.HintNode, Words))
		{
			const FElysiumEntity* Owner = World != nullptr ? World->Resolve(Words.HintOwner) : nullptr;
			if (Owner != this)                                                     // 0x102888b3
			// same arm: 0x102888bb CALL
			{
				const FString Name = Words.Name.IsEmpty() ? FString(TEXT("ai_hint")) : Words.Name;
				EmitDevMsg(TEXT("Hint node (%s) being used by non-owner!\n"),     // 0x102888c6
					FString::Printf(TEXT("Hint node (%s) being used by non-owner!\n"), *Name));
			}
		}
		if (IsActivityFinished())                                                  // 0x10289289
		{
			TaskComplete(false);                                                   // 0x1028929b
		}
		return 0;
	}

	case TaskRangeAttack1:
	case TaskRangeAttack2:
	case TaskMeleeAttack1:
	case TaskMeleeAttack2:
	case TaskSpecialAttack1:
	case TaskSpecialAttack2:
	{
		AutoMovement();                                                            // 0x102891f6
		MotorMoveStop();                                                           // 0x10289201
		// same arm: 0x1028920a CALL, 0x1028921a CALL
		const FVector Lkp = EnemyLkp();                                            // 0x10289222
		// Retail tests `0x34 || 0x38` here; `0x38` never reaches this arm (its own is `0x102890f3`).
		if ((Id == TaskRangeAttack1 || Id == TaskReload)                            // 0x1028922c / 0x10289231
			&& (CapabilitiesGet() & CapabilityAimHold) != 0                        // 0x10289237
			// same arm: 0x10289242 JZ
			&& FInAimCone(Lkp))                                                    // 0x1028924d
			// same arm: 0x10289255 JZ
		{
			MotorSetIdealYawAndUpdate(MotorIdealYaw, YawSpeedHold);               // 0x10289269
		}
		else
		{
			MotorSetIdealYawToTargetAndUpdate(Lkp, YawSpeedHold);                 // 0x10289280
		}
		if (IsActivityFinished())                                                  // 0x10289289
		{
			TaskComplete(false);                                                   // 0x1028929b
		}
		return 0;
	}

	case TaskReload:
	case TaskReloadNoTurn:
	{
		AutoMovement();                                                            // 0x102890f5
		if (Id != TaskReloadNoTurn)                                                // 0x102890fd
		{
			MotorMoveStop();                                                       // 0x10289105
			// same arm: 0x10289113 JZ, 0x1028912f JNZ, 0x10289141 CALL
			// `m_hEnemy` read raw (`+0x5ce0`) rather than through slot 167 -- the same answer.
			const FVector Lkp = EnemyLkp();                                        // 0x10289149
			MotorSetIdealYawToTargetAndUpdate(Lkp, YawSpeedHold);                 // 0x1028915e
		}
		if (!IsActivityFinished())                                                 // 0x10289167
		{
			return 0;
		}
		if (FElysiumEntity* Weapon = ActiveWeaponEntity())                         // 0x10289177
		// same arm: 0x1028917e JZ, 0x10289186 CALL
		{
			WeaponFinishReload(*Weapon);                                           // 0x1028918d / 0x1028919d
			// same arm: 0x10289194 CALL
			Cognition.Conditions.Clear(EElysiumNpcCond::NoPrimaryAmmo);            // 0x102891a7
			Cognition.Conditions.ClearOrdinal(CondNoSecondaryAmmo);                // 0x102891b0
		}
		TaskComplete(false);                                                       // 0x102891b9 / 0x10289713
		return 0;
	}

	case TaskRangeAttack1NoTurn:
	case TaskRangeAttack2NoTurn:
	case TaskMeleeAttack1NoTurn:
	case TaskMeleeAttack2NoTurn:
	case TaskPlaySequence:
	case TaskPlayPrivateSequence:
		AutoMovement();                                                            // 0x102891ca
		if (IsActivityFinished())                                                  // 0x102891d3
		// same arm: 0x102891db JZ
		{
			TaskComplete(false);                                                   // 0x102891e5
		}
		return 0;

	case TaskSetActivity:
		// `0x102889a2`: the sequence compare jumps straight to the shared `JNZ` at `0x10288c2a`.
		if (SequenceNumber == IdealSequence)
		{
			TaskComplete(false);                                                   // 0x10288c34
		}
		return 0;

	case TaskPlayPrivateSequenceFaceEnemy:
	case TaskPlaySequenceFaceEnemy:
	case TaskPlaySequenceFaceTarget:
	{
		FElysiumEntity* Face = Id == TaskPlaySequenceFaceTarget                    // 0x102887c9
			? (World != nullptr ? World->Resolve(TargetEnt) : nullptr)             // 0x102887d8
			: EnemySlot167();                                                      // 0x102887e3
		if (Face != nullptr)                                                       // 0x102887ed
		{
			MotorMoveStop();                                                       // 0x102887f5
			const FVector Delta = Face->Origin - Origin;                           // 0x102887fe / 0x1028880a
			// `UTIL_VecToYaw` (`0x101d2c70`) answers 0 for a zero vector.
			const float Yaw = NpcKernelFacingShared::RetailVecToYaw(Delta);        // 0x10288854
			MotorSetIdealYawAndUpdate(Yaw, YawSpeedHold);                          // 0x1028885e
		}
		AutoMovement();                                                            // 0x10288865
		if (IsActivityFinished())                                                  // 0x10289289
		{
			TaskComplete(false);                                                   // 0x1028929b
		}
		return 0;
	}

	case TaskDie:
	{
		if (!IsActivityFinished() || !(SequenceCycle >= ElysiumNpcTunables::One)) // 0x10288fc8 slot 251 / 0x10288fd0 / 0x10288fdc FCOMP / 0x10288fe4 AND 0x100 / 0x10288fe9 (unordered runs)
		{
			return 0;
		}
		LifeState = LifeDead;                                                // 0x10288ff7
		ThinkSet(nullptr, 0.0);                                                    // 0x10289001
		if (FElysiumNpc* Troika = AsNpc())
		{
			Troika->SequencePlaybackRate = 0.f;                                    // 0x10289008
		}
		FVector Mins(-DeathHullHalf, -DeathHullHalf, 0.f);
		FVector Maxs(DeathHullHalf, DeathHullHalf, ElysiumNpcTunables::One);
		if (DeathHullIsFlat())                                                     // 0x10289012
		// same arm: 0x10289019 JNZ
		{
			// `WorldAlignMins()` and `(maxs.x, maxs.y, mins.z + 1)` off `m_Collision` slots 1/2.
			FVector CollisionMins = FVector::ZeroVector;
			FVector CollisionMaxs = FVector::ZeroVector;
			RetailCollisionExtents(*this, CollisionMins, CollisionMaxs);           // 0x10289065..0x1028909e
			// same arm: 0x1028906e CALL, 0x10289077 CALL
			Mins = CollisionMins;
			Maxs = FVector(CollisionMaxs.X, CollisionMaxs.Y, CollisionMins.Z + ElysiumNpcTunables::One);
		}
		// `UTIL_SetSize(this, mins, maxs)` (`0x101cf390`), family Motor10's recorder.
		LastSetSizeMinsUnits = Mins;                                               // 0x102890a3
		LastSetSizeMaxsUnits = Maxs;
		++SetSizeCalls;
		if (ShouldFadeOnDeath())                                                   // 0x102890af
		// same arm: 0x102890b9 JZ
		{
			StartFadeOut();                                                        // 0x102890bb
			return 0;
		}
		// The origin argument is slot 220 (`0x102890d8 CALL [EDX+0x370]`); the seam records no origin.
		InsertAiSound(SoundCarcass, Origin, SoundCarcassVolume,                   // 0x102890e1
			ElysiumNpcTunables::Thirty);
		return 0;
	}

	case TaskWaitForScript:
	{
		FElysiumScriptedSequence* Cine = ResolveCine();                            // 0x102892aa..0x102892e0
		// same arm: 0x102892b3 JZ, 0x102892d7 JNZ, 0x102892ef JZ, 0x10289306 JNZ
		if (Cine != nullptr)
		{
			if (Cine->IsTimeToStart())                                             // 0x1028930e
			// same arm: 0x10289315 JZ
			{
				TaskComplete(false);                                               // 0x1028931f
				// same arm: 0x1028932d JZ, 0x1028934a JNZ
				Cine->StartScript();                                               // 0x10289352
				// The partner resolve (`0x10289357..0x102893ae`) re-reads `m_hCine` after
				// same arm: 0x10289366 JZ, 0x10289380 JNZ, 0x10289391 JZ, 0x102893a8 JNZ
				// `StartScript`. Crash guard: retail calls through a null director here.
				if (FElysiumScriptedSequence* Partner = ResolveCine())
				{
					Partner->StartSequence(*this, Cine->Play, true);               // 0x102893bc slot 584
				}
				if (bSequenceFinished)                                             // 0x102893ca
				{
					ClearSchedule();                                               // 0x102893ce
				}
				if (FElysiumNpc* Troika = AsNpc())
				{
					Troika->SequencePlaybackRate = ElysiumNpcTunables::One;        // 0x102893d3
				}
				return 0;
			}
			// `0x102893e7..0x10289412`: a live handle keeps the task running.
			// same arm: 0x102893f6 JZ, 0x1028940d JNZ
			return 0;
		}
		EmitDevMsg(TEXT("Cine died!\n"), TEXT("Cine died!\n"));                    // 0x1028941d
		TaskComplete(false);                                                       // 0x1028942a
		return 0;
	}

	case TaskPlayScript:
	{
		AutoMovement();                                                            // 0x1028943b
		if (!bSequenceFinished)                                                    // 0x10289448
		{
			return 0;
		}
		if (FElysiumScriptedSequence* Cine = ResolveCine())                        // 0x1028944e..0x10289484
		// same arm: 0x10289457 JZ, 0x1028947b JNZ, 0x10289493 JZ, 0x102894aa JNZ
		{
			Cine->SequenceDone(*this);                                             // 0x102894af
			TaskComplete(false);                                                   // 0x102894b8
			// same arm: 0x102894ca CALL, 0x102894d3 CALL
			return 0;
		}
		TaskComplete(false);                                                       // 0x1028970f
		return 0;
	}

	case TaskPlayScriptPostIdle:
	{
		FElysiumScriptedSequence* Cine = ResolveCine();
		if (!bSequenceFinished)                                                    // 0x102894f0
		// same arm: 0x102894fb JZ, 0x10289515 JNZ
		{
			if (Cine == nullptr)
			{
				// Crash guard: retail reads `+0x5f94` off the null director (`0x1028951d`).
				return 0;
			}
			const FElysiumEntity* Next = World != nullptr ? World->Resolve(Cine->NextCine) : nullptr;
			if (Next == nullptr)                                                   // 0x10289526..0x1028954d
			// same arm: 0x10289544 JNZ
			{
				return 0;
			}
		}
		if (Cine != nullptr)                                                       // 0x10289553..0x10289573
		// same arm: 0x1028955c JZ
		{
			Cine->Finish(*this);                                                   // 0x10289578
		}
		// Retail's `Finish(NULL, this)` arm (`0x1028958a`) has no receiver here; nothing runs.
		return 0;
	}

	case TaskStopMoving:
		if (NavGetType() == NavJump)                                               // 0x102888d6 / 0x102888de
		{
			if (IsOnGroundFlag())                                                  // 0x102888e6
			// same arm: 0x102888ef JZ
			{
				NavSetType(NavGround);                                             // 0x102888f3
			}
			else
			{
				const float Speed = static_cast<float>(Velocity.Size() / ElysiumMove::U); // 0x102888fc..0x10288924
				if (Speed > ElysiumNpcTunables::HundredthDouble)                  // 0x1028892a / 0x1028893a
				{
					return 0;
				}
				NavSetType(NavGround);                                             // 0x10288944
				Fail(LineStuckOnTop, FailStuckOnTop);                              // 0x10288963
			}
		}
		if (NavGetType() != NavClimb)                                              // 0x1028896b / 0x10288973
		{
			SetIdealActivity(ResolveLinkActivity());                               // 0x1028897b / 0x10288983
			BaseScheduleHost.bShouldMove = false;                                  // 0x1028898c
			TaskComplete(false);                                                   // 0x10288993
		}
		return 0;

	case TaskTurnLeft:
	case TaskTurnRight:
		MotorUpdateYaw(UpdateYawDefault);                                          // 0x102887b5
		if (FacingIdeal())                                                         // 0x102887bc / 0x1028928f
		// same arm: 0x10289291 JZ
		{
			TaskComplete(false);                                                   // 0x1028929b
		}
		return 0;

	case TaskWaitForMovement:
	case TaskWaitForMovementStep:
	{
		const float Limit = Step->Data;
		// `10288f4e TEST AH,0x44` / `10288f51 JNP`: only an ordered `data == 0` skips the timer; then
		// `10288f66 AND EAX,0x4100` / `10288f6b JZ`: only an ordered `elapsed > data` completes, an
		// unordered compare goes on to the goal test.
		if ((Limit == ElysiumNpcTunables::Zero                                     // 0x10288f46 / 0x10288f51
				|| !(static_cast<float>(Now - Schedule.TaskStartedAt) > Limit))    // 0x10288f5e / 0x10288f6b
			&& NavIsGoalActive())                                                  // 0x10288f77 / 0x10288f7e
		{
			if (NavigatorGoalIsActive())                                           // 0x10288f8a
			// same arm: 0x10288f93 JNZ
			{
				ValidateNavGoal();                                                 // 0x10288fb4 slot 528
				return 0;
			}
			BaseScheduleHost.bShouldMove = false;                                  // 0x10288f95
			SetIdealActivity(ResolveLinkActivity());                               // 0x10288f9b / 0x10288fa3
			return 0;
		}
		BaseScheduleHost.bShouldMove = false;                                      // 0x10289608
		TaskComplete(false);                                                       // 0x10288eff
		NavClearGoal();                                                            // 0x10288f0a
		return 0;
	}

	case TaskWeaponPickup:
	{
		if (!IsActivityFinished())                                                 // 0x1028964f
		// same arm: 0x10289657 JZ
		{
			return 0;
		}
		// `m_hTargetEnt` resolved: 0x10289666 JZ, 0x10289683 JNZ.
		FElysiumEntity* TargetEntity = World != nullptr ? World->Resolve(TargetEnt) : nullptr;
		// Crash guard: a target that is null or not a weapon casts to null and retail's
		// `0x102521f0` reads `+0x88c` through it; the port answers "unowned" and completes.
		if (TargetWeaponOwner(TargetEntity) != nullptr)                                  // 0x1028969a / 0x102896a4
		// same arm: 0x102896ad JZ
		{
			Fail(LineWeaponOwnedPickup, FailWeaponOwned);                          // 0x102896c7
			return 0;
		}
		TaskComplete(false);                                                       // 0x10289713
		return 0;
	}

	case TaskWeaponRunPath:
	{
		FElysiumEntity* TargetEntity = World != nullptr ? World->Resolve(TargetEnt) : nullptr;
		if (TargetEntity == nullptr)                                                     // 0x10288e89..0x10288eae
		// same arm: 0x10288ea8 JNZ
		{
			Fail(LineWeaponMissing, FailWeaponMissing);                            // 0x10288f33
			return 0;
		}
		// Slot 97 (`+0x184`, `GetOwnerEntity`) answers the RESOLVED owner, so a stale handle is no
		// owner (L05 integration: the port tested the raw handle).
		if (World->Resolve(TargetEntity->GetOwnerEntity()) != nullptr)            // 0x10288eb4 slot 97 / 0x10288eba
		// same arm: 0x10288ebc JZ
		{
			Fail(LineWeaponOwnedRunPath, FailWeaponOwned);                         // 0x10288ed8
			return 0;
		}
		if (NavIsGoalActive())                                                     // 0x10288eee / 0x10288ef5
		{
			return 0;
		}
		TaskComplete(false);                                                       // 0x10288eff
		NavClearGoal();                                                            // 0x10288f0a
		return 0;
	}

	case TaskFallToGround:
		if (IsOnGroundFlag())                                                      // 0x102896d9 / 0x102896e0
		{
			TaskComplete(false);                                                   // 0x102896e6
		}
		return 0;

	default:
		break;
	}

	// `0x102896f5`: every id outside `2..0xb1` and every in-range id the byte table sends here.
	const TCHAR* Name = TaskName(Id);                                              // 0x102896fa slot 449
	EmitDevMsg(TEXT("No RunTask entry for %s\n"),                                  // 0x10289706
		FString::Printf(TEXT("No RunTask entry for %s\n"), Name != nullptr ? Name : TEXT("")));
	TaskComplete(false);                                                           // 0x10289713
	return 0;
}
