// Story 0019/8 (29e under the strict verdict), family **RunTask19** -- `CAI_BaseNPCTroika`'s
// bodies.
//
// Created by the story-8 shape commit (`uv run elysium research kernel_story8_shape`, spec 0019
// story 8, `docs/specs/0019-npc-kernel-rework/story-8-execution-plan.md` R1/R2), before any body is
// ported, so that the family's lane owns this file alone.
//
// Declarations are in `ElysiumNpcRunTask.inl` (included inside `class FElysiumNpc`) or generated
// in `ElysiumNpcSlots.inl` for a slot body.
//
// Owns (RunTask19's `rule` rows): 0x102aacf0 CAI_BaseNPCTroika::RunTask.
//
// Lane L05 (pass I). The dispatch is `id - 2 <= 0x147` through the byte table `0x102ac844` into the
// 57-entry jump table `0x102ac760`; the arms below are in jump-table index order (the order the
// decompiler prints them), each with its arm address and case ids. Every branch sense was read off
// the listing (`vtmb_asm 0x102aacf0`); the packet's chunk walks (`bench/pair-read/passR/giant*`)
// name the ids per arm. Walked prose:
// `docs/vtmb/npc-ai/schedule-kernel.md` § "`CAI_BaseNPCTroika::RunTask` `0x102aacf0`".

#include "Substrate/ElysiumNpc.h"

#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumNpcFacingShared.h"
#include "Substrate/ElysiumNpcMotor2Shared.h"
#include "Substrate/ElysiumNpcThinkCadence.h"
#include "Substrate/ElysiumScheduleText.h"
#include "Substrate/ElysiumWeaponClasses.h"

namespace RunTask19Troika
{
	// The source file every fail trace of this body stamps (`0x105da024`).
	const TCHAR* const GFile = TEXT("E:\\Vampire\\main\\dlls\\AI_BaseNPCTroika.cpp");

	// Task ids (the registrar `FUN_10316ff0`; names from `walk-19-29-pack-08.md` / `-13.md`).
	constexpr int32 TaskWait = 0x02;
	constexpr int32 TaskWaitFaceEnemy = 0x04;
	constexpr int32 TaskWaitPvs = 0x05;
	constexpr int32 TaskFaceIdeal = 0x2b;
	constexpr int32 TaskFaceEnemy = 0x2e;
	constexpr int32 TaskFaceTarget = 0x31;
	constexpr int32 TaskRangeAttack1 = 0x34;
	constexpr int32 TaskRangeAttack2 = 0x35;
	constexpr int32 TaskMeleeAttack1 = 0x36;
	constexpr int32 TaskMeleeAttack2 = 0x37;
	constexpr int32 TaskSpecialAttack1 = 0x3e;
	constexpr int32 TaskSpecialAttack2 = 0x3f;
	constexpr int32 TaskSetActivity = 0x4b;
	constexpr int32 TaskPlayPrivateSequenceFaceEnemy = 0x54;
	constexpr int32 TaskPlaySequenceFaceEnemy = 0x55;
	constexpr int32 TaskPlaySequenceFaceTarget = 0x56;
	constexpr int32 TaskDie = 0x5f;
	constexpr int32 TaskWaitRandom = 0x67;
	constexpr int32 TaskWaitIndefinite = 0x68;
	constexpr int32 TaskWaitForMovement = 0x6e;
	constexpr int32 TaskGetPathToPatrolPoint = 0x7a;
	constexpr int32 TaskGetPathToPatrolPointHunt = 0x7b;
	constexpr int32 TaskMeleeDodgeAttack = 0x8c;
	constexpr int32 TaskMeleeKnockback = 0x92;
	constexpr int32 TaskMeleeFlyingKnockbackInto = 0x93;
	constexpr int32 TaskMeleeFlyingKnockbackIdle = 0x94;
	constexpr int32 TaskMeleeFlyingKnockbackLand = 0x95;
	constexpr int32 TaskMeleeFlyingKnockbackWall96 = 0x96;
	constexpr int32 TaskMeleeFlyingKnockbackWall97 = 0x97;
	constexpr int32 TaskMeleeFlyingKnockbackWall98 = 0x98;
	constexpr int32 TaskMeleeHitByFinishingMove = 0x99;
	constexpr int32 TaskMeleeKick = 0x9a;
	constexpr int32 TaskOnFireInto = 0x9c;
	constexpr int32 TaskOnFireLoop = 0x9d;
	constexpr int32 TaskOnFireOutof = 0x9e;
	constexpr int32 TaskTest1 = 0xa7;
	constexpr int32 TaskWaitAttackTime1 = 0xb0;
	constexpr int32 TaskWaitAttackTime2 = 0xb1;
	constexpr int32 TaskFaceInterest = 0xb2;
	constexpr int32 TaskFacePatrolInterest = 0xb3;
	constexpr int32 TaskDoInterestActivity = 0xb4;
	constexpr int32 TaskDoPatrolInterestActivity = 0xb5;
	constexpr int32 TaskDoLoiterActivity = 0xb6;
	constexpr int32 TaskDoInteractActivity = 0xb7;
	constexpr int32 TaskDoInterestDeathActivity = 0xb8;
	constexpr int32 TaskRunDialog = 0xb9;
	constexpr int32 TaskRunDisposition = 0xba;
	constexpr int32 TaskRunDispositionRandom = 0xbb;
	constexpr int32 TaskSpecialIdleActivity = 0xbc;
	constexpr int32 TaskSpecialIdleActivityRandom = 0xbd;
	constexpr int32 TaskKnockout = 0xdd;
	constexpr int32 TaskUnknockout = 0xde;
	constexpr int32 TaskDieImmediate = 0xdf;
	constexpr int32 TaskDoJumpActivity = 0xe0;
	constexpr int32 TaskDoLoopActivity = 0xe1;
	constexpr int32 TaskDoBlendActivity = 0xe2;
	constexpr int32 TaskDoBlendLoopActivity = 0xe3;
	constexpr int32 TaskPlayCower = 0xe6;
	constexpr int32 TaskSetCower = 0xe7;
	constexpr int32 TaskDieGib = 0xe9;                        // `TASK_DIE_GIB` (ElysiumTaskOps.h)
	constexpr int32 TaskDieExplodeGib = 0xea;
	constexpr int32 TaskDieDueToPlayer = 0xeb;
	constexpr int32 TaskPlayComfortInto = 0xec;
	constexpr int32 TaskDoComfortLoop = 0xed;
	constexpr int32 TaskPlayComfortOutof = 0xee;
	constexpr int32 TaskPlayPartialResistActivity = 0xef;
	constexpr int32 TaskPlayFullResistActivity = 0xf0;
	constexpr int32 TaskFaceNextNode = 0xf7;
	constexpr int32 TaskLookAtBestSound = 0xf8;
	constexpr int32 TaskAlertLookAtBestSound = 0xf9;
	constexpr int32 TaskAlertLookAtRandomLoc = 0xfa;
	constexpr int32 TaskLookAtPlayer = 0xfb;
	constexpr int32 TaskLookAtBestUnknown = 0xfc;
	constexpr int32 TaskAlertLookAtDetectedAttack = 0xfd;
	constexpr int32 TaskUnlookAt = 0xfe;
	constexpr int32 TaskUnlookAtFace = 0xff;
	constexpr int32 TaskAttemptDive = 0x107;
	constexpr int32 TaskAttemptDiveSide = 0x108;
	constexpr int32 TaskAttemptDiveForward = 0x109;
	constexpr int32 TaskPlayCoverInto = 0x10a;
	constexpr int32 TaskPlayCoverIdle = 0x10b;
	constexpr int32 TaskPlayCoverOutof = 0x10c;
	constexpr int32 TaskPlayCoverAim = 0x10d;
	constexpr int32 TaskKickHint = 0x10f;
	constexpr int32 TaskKickHintAt = 0x110;
	constexpr int32 TaskKickProp = 0x113;
	constexpr int32 TaskResolveBotch = 0x116;
	constexpr int32 TaskResolveBotchInCover = 0x117;
	constexpr int32 TaskResolveBotchOutOfCover = 0x118;
	constexpr int32 TaskStepBack = 0x11b;
	constexpr int32 TaskStepBackRun = 0x11c;
	constexpr int32 TaskFaceLastAngle = 0x11d;
	constexpr int32 TaskMeleeCircleEnemy = 0x122;
	constexpr int32 TaskCircleEnemy = 0x123;
	constexpr int32 TaskCircleEnemyFullCycle = 0x124;
	constexpr int32 TaskMeleeCheer = 0x128;
	constexpr int32 TaskFaceSavePosition = 0x12e;
	constexpr int32 TaskPlayCombatStartSequence = 0x137;
	constexpr int32 TaskPreJump = 0x139;
	constexpr int32 TaskJump = 0x13a;
	constexpr int32 TaskLand = 0x13b;
	constexpr int32 TaskLandHard = 0x13c;
	constexpr int32 TaskAlertLookAtUnknownAttacker = 0x148;
	constexpr int32 TaskPlayDeathSequence = 0x149;

	// `TaskFail` reasons (`0x106152b0`) and the `+0x1b48` lines.
	constexpr int32 FailNoEnemy = 6;
	constexpr int32 FailBadActivity = 0x15;
	constexpr int32 FailNoInterestingPlace = 0x22;
	constexpr int32 FailLostInterestingPlace = 0x23;
	constexpr int32 LineWaitNoEnemy = 0x407c;
	constexpr int32 LineFaceInterest = 0x4105;
	constexpr int32 LineDoInterest = 0x4141;
	constexpr int32 LineCircleNoActivity = 0x4255;
	constexpr int32 LineCheerNoEnemy = 0x4304;

	// Activities the arms name (the registered `Activity` numbers).
	constexpr int32 ActIdle = 1;
	constexpr int32 ActWalk = 9;
	constexpr int32 ActSmallFlinch = 0x19;
	constexpr int32 ActKnockbackLand = 0x90;
	constexpr int32 ActKnockbackFly = 0x91;
	constexpr int32 ActKnockbackWallHit = 0x92;
	constexpr int32 ActKnockbackWallFall = 0x93;
	constexpr int32 ActInterestIdle = 0x10f7;
	constexpr int32 ActKnockedOut = 0x1052;
	constexpr int32 ActUnknockout = 0x1068;
	constexpr int32 ActDive = 0xf1d;
	constexpr int32 ActCircle = 0x1121;

	// Schedules the knockback arms force (`0x102ae750`, raw registrar ids).
	constexpr int32 SchedKnockbackFly = 0x14e;
	constexpr int32 SchedKnockbackWall = 0x14f;

	// Word masks.
	constexpr uint32 MemoryTurning = 0x2000u;                 // `m_afMemory & 0x2000`
	constexpr uint32 AiFlagsLookClear = 0x08000000u;          // `AND 0xf7ffffff`
	constexpr uint32 AiFlagsDiveClear = 0x00004000u;          // `AND CH,0xbf`
	constexpr uint32 AiFlagsBotchClear = 0x00040000u;         // `AND 0xfffbffff`
	constexpr uint32 AiFlagsUnknockoutKeep = 0xbbf5ffffu;     // `AND 0xbbf5ffff`
	constexpr uint32 AiFlagsInterestDeathClear = 0x20000000u; // `AND 0xdfffffff`
	constexpr int32 SpawnFlagAlwaysThink = 0x400;
	constexpr int32 CondSeePlayerId = 0x5a;

	// Motor yaw speeds (the pushed immediates).
	constexpr float YawSpeedHold = -2.0f;                     // `0xc0000000`
	constexpr float YawSpeedDefault = -1.0f;                  // `0xbf800000`
	constexpr int32 UpdateYawDefault = -1;

	// `_DAT_1046dcd0` f32 = 128.0 -- the special-idle player distance (Source units).
	constexpr float SpecialIdlePlayerDistance = ElysiumNpcTunables::OneTwentyEight;
	// `_DAT_1049ae74` f32 = 50000.0 -- the finishing-move force scale.
	constexpr float FinishingMoveForceScale = ElysiumNpcTunables::FiftyThousand;
	// `CVStatList_t::SetBaseToStatValue(0xf, 0x11)` on stat list 0 (`0x102ac46b`).
	constexpr int32 FinishingMoveStatList = 0;
	constexpr int32 FinishingMoveStatTarget = 0xf;
	constexpr int32 FinishingMoveStatSource = 0x11;
	// NAV types.
	constexpr int32 NavGround = 0;
	constexpr int32 NavJump = 1;
	// `CBaseEntity::SetMoveType(MOVETYPE_STEP 5, MOVECOLLIDE_DEFAULT 0)` (`0x102c4e30`).
	constexpr int32 MoveTypeStep = 5;

	// `m_pInterestingPlace`-style resolve of an entity index to the place it names (the same test
	// `FElysiumNpc::CurrentAmbientSpot` applies).
	FElysiumInterestingPlace* PlaceAt(FElysiumEntityWorld* World, int32 Index)
	{
		if (World == nullptr || Index == INDEX_NONE || Index == 0)
		{
			return nullptr;
		}
		FElysiumEntity* Entity = World->Resolve(FElysiumEntityHandle(Index, World->GetEpoch()));
		return Entity != nullptr && Entity->Def != nullptr
			&& Entity->Def->Classname.Equals(TEXT("intersting_place"), ESearchCase::IgnoreCase)
			? static_cast<FElysiumInterestingPlace*>(Entity) : nullptr;
	}
}

// --- Helpers and seams (`ElysiumNpcRunTask.inl`) ---------------------------------------------------

void FElysiumNpc::FacePendingAimTarget()
{
	// `0x102aab70`. `param_1[0x52f]` is `+0x14bc`, `m_bfAINPCFlags2`.
	if (NpcFlags.HasRawWord2Bits(0x10u))
	{
		if (static_cast<const FElysiumNpcBase*>(this)->GetEnemy() != nullptr)    // 0x102aab70 slot 167
		{
			const FVector Lkp = Conditions19LastKnownPosition(                     // 0x102dfed0
				static_cast<const FElysiumNpcBase*>(this)->GetEnemy());
			if (!FInAimCone(Lkp))                                                  // slot 364
			{
				MotorSetIdealYawToTargetAndUpdate(Lkp, RunTask19Troika::YawSpeedHold); // 0x102e20b0
			}
		}
		return;
	}
	if (NpcFlags.HasRawWord2Bits(0x20u))
	{
		// `m_hTargetEnt` (`param_1[0x1739]`, `+0x5ce4`), live.
		if (FElysiumEntity* TargetEntity = World != nullptr ? World->Resolve(TargetEnt) : nullptr)
		{
			const FVector Aim = TargetEntity->Origin;                                    // slot 217
			if (!FInAimCone(Aim))
			{
				MotorSetIdealYawToTargetAndUpdate(Aim, RunTask19Troika::YawSpeedHold);
			}
		}
	}
}

void FElysiumNpc::JumpHaltMotion()
{
	// `0x102c4e30`: slot 93 `SetMoveType(5, 0)`, `+0x3ec = 0`, `SetLocalAngularVelocity(vec3_angle)`,
	// `SetAbsVelocity(vec3_origin)`.
	SetMoveType(RunTask19Troika::MoveTypeStep, 0);
	Gravity = 0.f;
	AngularVelocity = FVector::ZeroVector;
	Velocity = FVector::ZeroVector;
}

bool FElysiumNpc::NavArrivedWithinTolerance()
{
	// `0x102f2ea0` -- SEAM (declaration).
	return false;
}

void FElysiumNpc::KnockbackHitEntityReact()
{
	// `0x102a0870` -- SEAM (declaration).
	++KnockbackHitEntityReactions;
}

bool FElysiumNpc::KnockbackWallProbe(FVector& OutNormalUnits)
{
	// `0x102a0490` -- SEAM (declaration).
	(void)OutNormalUnits;
	return false;
}

int32 FElysiumNpc::SequenceLinkedNext() const
{
	// `0x103454c0` -- SEAM (declaration).
	return INDEX_NONE;
}

int32 FElysiumNpc::SequenceLinkedLand() const
{
	// `0x10345480` -- SEAM (declaration).
	return INDEX_NONE;
}

bool FElysiumNpc::KnockbackLanded()
{
	// `0x102c4eb0`: `GetFlags() & FL_ONGROUND` answers at once; the one-unit-down hull trace is the
	// SEAM half and answers "no ground".
	return IsOnGroundFlag();
}

void FElysiumNpc::JumpCommit()
{
	// `0x102c4e80` -- SEAM (declaration), counted on the motor ledger's one recorder for this call
	// (`MotorSeams.SetupJumpCommits`, family Motor; the L05 integration dropped this lane's second
	// counter and pointed `FElysiumNpcVampireBoss::CommitSetupJump` here).
	++MotorSeams.SetupJumpCommits;
}

int32 FElysiumNpc::RunDialogActivity()
{
	// `0x102c1400` -- SEAM (declaration). Retail answers -1 (after `0x102c0360`) only when
	// `IsInDialog()` (`0x102c1170`) is false; inside a dialogue it runs the upkeep (the `+0x6554`
	// handle release, `FinishTalking`, `0x102c0520`, the `+0x65c` disposition lookup through slot
	// 611 answering 0xf1 or 1, `CDialog::ShowPlayerChoices`) and answers `m_Activity` (`+0xfec`) or
	// that 0xf1 / 1. The dialogue family owns that body. Its FIRST arm is evaluated (StartTask19
	// integration: folded from L04's `TaxiDialogUpkeep`): not in a dialogue answers -1; inside one the
	// unported upkeep answers `m_Activity`, what the body returns when neither disposition arm fires.
	if (!IsInDialog())
	{
		return INDEX_NONE;
	}
	return ActivityNumber;
}

void FElysiumNpc::BloodExplode()
{
	// `0x1033d4b0` -- SEAM (declaration).
	++BloodExplodeCalls;
}

void FElysiumNpc::Die(const FElysiumEntity* Credit)
{
	// `CBaseCombatCharacter::Die(credit, 0, 0)` `0x103392c0`, the whole body: on a body whose
	// `m_lifeState` is not LIFE_DEAD (2), a `CVDmg_t` with `SetSrc(this)`, `m_iDiceAmt = 1`,
	// `m_iToHitSuccesses = 1`; the packet `0x101c26d0(info, 0, credit, 1.0, 0, 0, &dmg, -1)` (no
	// inflictor, the credit as attacker); its `+0x48` / `+0x49` bytes from the two flag arguments
	// (`0x101c2a90` / `0x101c2ad0`, both 0 here, and words the port's packet does not carry);
	// `SetBaseToStatValue(0xf, 0x11)`; then slot 144 `Event_Killed(info)` and slot 403 `Event_Dying`.
	++RunTaskDieCalls;
	LastDieCredit = Credit != nullptr ? Credit->Handle : FElysiumEntityHandle::Invalid();
	if (LifeState == 2)                                   // 0x10339330 m_lifeState != 2
	{
		return;
	}
	FElysiumDmg Dmg;
	Dmg.Source = Handle;                                               // SetSrc(this)
	Dmg.BaseDamage = 1;                                                // m_iDiceAmt
	Dmg.ExtraInput = 1;                                                // m_iToHitSuccesses
	FElysiumTakeDamageInfo Info;
	Info.Dmg = &Dmg;
	Info.Attacker = LastDieCredit;
	Info.Damage = 1.f;
	Info.DamageBits = 0;
	Info.AmmoType = INDEX_NONE;
	Sheet.SetBase(EElysiumTraitContainer::Attributes, ElysiumSlot::Health,
		TypedStatValue(0, ElysiumSlot::MaxHealth));                    // 0x103393ff SetBaseToStatValue(0xf, 0x11)
	RecomputeSheet();
	Event_Killed(&Info);                                               // 0x1033940d slot 144 (+0x240)
	Event_Dying();                                                     // 0x10339417 slot 403 (+0x64c)
}

bool FElysiumNpc::EnemyMeleeSwingOver(const FElysiumCombatCharacter& Enemy) const
{
	// `0x10345330` -- SEAM (declaration).
	(void)Enemy;
	return true;
}

int32 FElysiumNpc::MeleeRollBand(const FElysiumMeleeRoll& Roll) const
{
	// `0x103498b0`, the defender half of the `rules.txt` margin ladder (L05 integration: was a seam
	// answering 0; the port already classifies with it, `ElysiumWeapons::ClassifyDefender`).
	// `103498b1..103498bc`: margin = roll `+4` (lethality) - `+0xc` (soak) - `+8` (defense), which is
	// `FElysiumMeleeRoll::Margin()`; `__ftol` of its `FILD` is the same integer. Then four float
	// thresholds `_DAT_10739fa0..fac` (`SuccessesForDefenderDodgeAttack .. BlockStagger`), each
	// `FCOMP` / `TEST AH,0x41` / `JP`: an ordered `margin <= T` answers the band (0..3), else 4.
	// The thresholds are the rules table's; with no table (the port's `bValid` false) they read as
	// retail's zero-initialised floats, so the ladder collapses to `margin <= 0 ? 0 : 4`.
	const FElysiumMeleeMargins Margins = FElysiumWeaponContext::FromCharacter(*this).Margins;
	const float Margin = static_cast<float>(Roll.Margin());                        // 103498c2..103498d3
	const float T0 = Margins.bValid ? static_cast<float>(Margins.DefenderDodgeAttack) : 0.f;
	const float T1 = Margins.bValid ? static_cast<float>(Margins.DefenderDodge) : 0.f;
	const float T2 = Margins.bValid ? static_cast<float>(Margins.DefenderBlock) : 0.f;
	const float T3 = Margins.bValid ? static_cast<float>(Margins.DefenderBlockStagger) : 0.f;
	if (Margin <= T0) { return 0; }                                                // 103498d7 / 103498e2
	if (Margin <= T1) { return 1; }                                                // 103498ec / 103498f7
	if (Margin <= T2) { return 2; }                                                // 10349904 / 1034990f
	return Margin <= T3 ? 3 : 4;                                                   // 1034991c / 1034992c
}

bool FElysiumNpc::EnemySequenceEventsPending(const FElysiumCombatCharacter& Enemy,
	bool& bOutHasSequence) const
{
	// The `GetSeqDesc` / `GetSequenceCycleRate` event walk -- SEAM (declaration).
	(void)Enemy;
	bOutHasSequence = true;
	return false;
}

bool FElysiumNpc::PlaceHolstersWeapon(const FElysiumInterestingPlace& Place) const
{
	// `place+0x571 m_bHolsterWeapon` -- SEAM (declaration).
	(void)Place;
	return false;
}

// Slot 444: `CAI_BaseNPCTroika::RunTask` `0x102aacf0`, 6,767 bytes.
int32 FElysiumNpc::RunTaskSlot444(void* Task)
{
	using namespace RunTask19Troika;
	const FElysiumScheduleStep* Step = static_cast<const FElysiumScheduleStep*>(Task);
	if (Step == nullptr)
	{
		// Crash guard: retail dereferences the task at `0x102aad03`.
		// same arm: 0x102aad0e JA, 0x102aad18 JMP
		return 0;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	auto Fail = [this](int32 Line, int32 Reason)
	{
		// `+0x1b44 = "E:\Vampire\main\dlls\AI_BaseNPCTroika.cpp"`, `+0x1b48 = line`, slot 448.
		RecordScheduleEvent(FString::Printf(TEXT("RunTask fail trace %s:%d"), GFile, Line));
		TaskFail(Reason);
	};
	auto EnemySlot167 = [this]() -> FElysiumEntity*
	{
		return static_cast<const FElysiumNpc*>(this)->GetEnemy();
	};
	// `0x102dfed0` with the slot-167 enemy, NULL included: the record, else the last position-only
	// record, else `vec3_origin` (L05 integration: was `EnemyLastKnownPosition`, which skipped the
	// record walk with no enemy -- 0x10d's `0x102ab8e5 -> 0x102aaeef` reaches it with none).
	auto EnemyLkp = [this]() -> FVector
	{
		return Conditions19LastKnownPosition(static_cast<const FElysiumNpcBase*>(this)->GetEnemy());
	};
	auto Override = [this]() -> FElysiumEntity*
	{
		// `m_hShootTargetOverride` (`+0x5ba8`), live.
		return World != nullptr ? World->Resolve(ShootTargetOverride) : nullptr;
	};
	// `m_flWaitFinished` against curtime (`0x102aad3c FCOMP; AND 0x100; JNZ` -> running): below it or
	// UNORDERED keeps running, so only an ordered `curtime >= m_flWaitFinished` completes.
	auto WaitTest = [this, Now]()
	{
		if (Now >= BaseScheduleHost.WaitFinished)                                  // 0x102aad49
		{
			TaskComplete(false);                                                   // 0x102aad4f
		}
	};
	auto TurnUnlessMemory = [this]()
	{
		if ((BaseScheduleHost.MemoryBits & MemoryTurning) == 0)                    // `TEST AH,0x20`
		{
			SetTurnActivity();                                                     // slot 572
		}
	};
	auto CompleteWhenActivityFinished = [this]()
	{
		if (IsActivityFinished())                                                  // slot 251
		{
			TaskComplete(false);                                                   // 0x102ab931
		}
	};
	auto ClearAiFlags = [this](uint32 Mask)
	{
		NpcFlags.AssignAiFlagsWord(NpcFlags.RawWord1() & ~Mask);                   // `+0x14b8`
	};

	// Retail's `pTask->iTask` is CLASS-LOCAL; the step carries the GLOBAL id, translated once through
	// slot 450 `GetLocalTaskId` (`0x101a6640`), as `StartTaskSlot442` (`0x102a1923`) does.
	const int32 Id = GetLocalTaskId(Step->TaskId);                                 // 0x102aad03 slot 450
	switch (Id)
	{
	// --- index 0x00 `0x102aad61`: 0x2, 0x67, 0x68 ------------------------------------------------
	case TaskWait:
	case TaskWaitRandom:
	case TaskWaitIndefinite:
		FacePendingAimTarget();                                                    // 0x102aad64
		return FElysiumNpcBase::RunTaskSlot444(Task);                              // 0x102aad6c

	// --- index 0x01 `0x102ab659`: 0x4, 0xb0, 0xb1 ------------------------------------------------
	case TaskWaitFaceEnemy:
	case TaskWaitAttackTime1:
	case TaskWaitAttackTime2:
	{
		// Retail leaves the aim point uninitialised on the no-enemy arm and still aims at it; the
		// port aims at the zero vector there (named divergence).
		FVector Aim = FVector::ZeroVector;
		if (FElysiumEntity* TargetEntity = Override())                                   // 0x102ab659..0x102ab687
		// same arm: 0x102ab662 JZ, 0x102ab682 JNZ, 0x102ab692 JZ, 0x102ab6a9 JNZ
		{
			Aim = TargetEntity->Origin;                                                  // 0x102ab6b3 slot 217
		}
		else if (EnemySlot167() != nullptr)                                        // 0x102ab6d3
		// same arm: 0x102ab6dd JZ, 0x102ab6e1 CALL, 0x102ab6f4 CALL
		{
			Aim = EnemyLkp();                                                      // 0x102ab6fc
		}
		else
		{
			Fail(LineWaitNoEnemy, FailNoEnemy);                                    // 0x102ab72f
		}
		if (!FInAimCone(Aim))                                                      // 0x102ab73e
		// same arm: 0x102ab746 JNZ
		{
			MotorSetIdealYawToTargetAndUpdate(Aim, YawSpeedHold);                  // 0x102ab758
		}
		WaitTest();                                                                // 0x102ab765
		return 0;
	}

	// --- index 0x02 `0x102aad7e`: 0x5 `TASK_WAIT_PVS` -------------------------------------------
	case TaskWaitPvs:
	{
		if ((SpawnFlags & SpawnFlagAlwaysThink) != 0                                // 0x102aad89
			|| ElysiumNpcThink::ShouldThinkFrequently(*this))                      // 0x102aad91
			// same arm: 0x102aad98 JNZ, 0x102aada7 JZ, 0x102aadc4 JNZ, 0x102aadce CALL
		{
			TaskComplete(false);                                                   // 0x102ac11b
			return 0;
		}
		// `0x101d1a90(m_hClosestPlayer, this)`, the engine PVS test.
		const FElysiumPlayer* Player =
			Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
		bool bInPvs = false;
		if (Player != nullptr && !Player->IsInert() && Player->Handle == Senses.Memory.ClosestPlayer)
		{
			const IElysiumEmbodiment* Embodiment = World->Embodiment();
			bInPvs = Embodiment == nullptr || Embodiment->ArePointsInSamePvs(Player->Origin, Origin);
		}
		if (!bInPvs)                                                               // 0x102aadd8
		{
			return 0;
		}
		ResetThinkTimers(Now);                                                     // 0x102aadde slot 614
		EntityLastThink = Now;                                                     // 0x102aadef +0x178
		ScheduleHost.LastUpdate = Now;                                             // 0x102aadfd +0x6254
		ScheduleHost.LastNormal = Now;                                             // 0x102aae0c +0x6258
		ScheduleHost.LastMove = Now;                                               // 0x102aae1b +0x625c
		ScheduleHost.LastAI = Now;                                                 // 0x102aae29 +0x6260
		TaskComplete(false);                                                       // 0x102aae31
		return 0;
	}

	// --- index 0x03 `0x102aae43`: 0x2b, 0x31 -----------------------------------------------------
	case TaskFaceIdeal:
	case TaskFaceTarget:
		TurnUnlessMemory();                                                        // 0x102aae49
		// same arm: 0x102aae4c JNZ, 0x102aae56 CALL
		return FElysiumNpcBase::RunTaskSlot444(Task);

	// --- index 0x04 `0x102aae61`: 0x2e `TASK_FACE_ENEMY` ------------------------------------------
	// same arm: 0x102aae6a JNZ, 0x102aae70 CALL
	case TaskFaceEnemy:
	{
		TurnUnlessMemory();
		// A stale override handle is a null deref in retail (`0x102aaea6`); `Override()` resolves both
		// serial tests at once.
		const FVector Aim = Override() != nullptr ? Override()->Origin : EnemyLkp(); // 0x102aae76 / 0x102aaefb
		// same arm: 0x102aae7f JZ, 0x102aae9f JNZ, 0x102aaea4 JZ
		MotorSetIdealYawToTargetAndUpdate(Aim, YawSpeedDefault);                   // 0x102aaf24
		if (FacingIdeal())                                                         // 0x102ab922
		{
			TaskComplete(false);                                                   // 0x102ab931
		}
		return 0;
	}

	// --- index 0x05 `0x102ab0a9`: 0x34 `TASK_RANGE_ATTACK1`, the burst ---------------------------
	case TaskRangeAttack1:
	{
		AutoMovement();                                                            // 0x102ab0ab
		// same arm: 0x102ab0b9 JZ, 0x102ab0d9 JNZ, 0x102ab0de JZ, 0x102ab0e9 JZ, 0x102ab100 JNZ
		//   0x102ab106 CALL, 0x102ab112 CALL, 0x102ab11e CALL, 0x102ab131 CALL, 0x102ab139 CALL
		const FVector Aim = Override() != nullptr ? Override()->Origin : EnemyLkp();
		MotorSetIdealYawToTargetAndUpdate(Aim, YawSpeedHold);                      // 0x102ab162
		if (BurstFireCount > 0)                                                    // 0x102ab171
		{
			FElysiumEntity* WeaponEntity = ActiveWeaponEntity();                   // 0x102ab173
			if (WeaponEntity == nullptr)                                           // 0x102ab17c
			{
				TaskComplete(false);                                               // 0x102ab181
				// Crash guard: retail goes on to read the null weapon's next-attack time
				// (`0x102ab193`).
				return 0;
			}
			// `0x10252450(weapon)` -- the weapon's next primary attack time.
			FElysiumItem* Item = WeaponEntity->AsItem();
			const FElysiumWeapon* Weapon = Item != nullptr ? Item->AsWeapon() : nullptr;
			const double NextAttack = Weapon != nullptr ? Weapon->NextPrimaryAttackTime : 0.0;
			if (!(NextAttack <= Now))                                              // 0x102ab19f `TEST AH,0x41; JP` (greater or NaN runs)
			{
				return 0;
			}
			--BurstFireCount;                                                      // 0x102ab1af
			if (PlayHintIdleActivity(Now))                                         // 0x102ab1b5 `0x102aaa60`
			// same arm: 0x102ab1bc JNZ
			{
				return 0;
			}
			TaskComplete(false);                                                   // 0x102ab1c6
			return 0;
		}
		CompleteWhenActivityFinished();                                            // 0x102ab1da
		return 0;
	}

	// --- index 0x06 `0x102ab1e5`: 0x35, 0x3e, 0x3f ------------------------------------------------
	// same arm: 0x102ab1e7 CALL, 0x102ab1f5 JZ, 0x102ab215 JNZ, 0x102ab21a JZ, 0x102ab225 JZ
	//   0x102ab23c JNZ, 0x102ab242 CALL, 0x102ab24e CALL, 0x102ab25a CALL, 0x102ab26d CALL
	//   0x102ab275 CALL
	case TaskRangeAttack2:
	case TaskSpecialAttack1:
	case TaskSpecialAttack2:
	{
		AutoMovement();
		const FVector Aim = Override() != nullptr ? Override()->Origin : EnemyLkp();
		MotorSetIdealYawToTargetAndUpdate(Aim, YawSpeedHold);                      // 0x102ab297
		CompleteWhenActivityFinished();                                            // switchD 0xf
		return 0;
	}

	// --- index 0x07 `0x102ab2b2`: 0x36, 0x37, 0x8c, 0x9a ------------------------------------------
	case TaskMeleeAttack1:
	case TaskMeleeAttack2:
	case TaskMeleeDodgeAttack:
	case TaskMeleeKick:
		AutoMovement();                                                            // 0x102ab2b4
		if (FElysiumEntity* Enemy = EnemySlot167())                                // 0x102ab2bd
		// same arm: 0x102ab2c5 JZ
		{
			if (Slot591())                                                         // 0x102ab2cb slot 591
			// same arm: 0x102ab2d3 JZ, 0x102ab2d9 CALL, 0x102ab2e3 CALL
			{
				MotorSetIdealYawToTargetAndUpdate(Enemy->Origin, YawSpeedHold);    // 0x102ab297
			}
		}
		CompleteWhenActivityFinished();
		return 0;

	// --- index 0x08 `0x102aad1f`: 0x4b `TASK_SET_ACTIVITY` ----------------------------------------
	case TaskSetActivity:
		if (SequenceNumber == IdealSequence)                                       // 0x102aad2d
		{
			TaskComplete(false);                                                   // 0x102ab931
			return 0;
		}
		WaitTest();                                                                // 0x102aad3c
		return 0;

	// --- index 0x09 `0x102ab4c5`: 0x54, 0x55, 0x56 ------------------------------------------------
	case TaskPlayPrivateSequenceFaceEnemy:
	case TaskPlaySequenceFaceEnemy:
	case TaskPlaySequenceFaceTarget:
	{
		bool bFace = false;
		FVector Point = FVector::ZeroVector;
		if (Id == TaskPlaySequenceFaceTarget)                                      // 0x102ab4c8
		// same arm: 0x102ab4d3 JZ, 0x102ab4f0 JNZ
		{
			// Crash guard: a stale `m_hTargetEnt` is a null deref in retail (`0x102ab4f6`).
			// same arm: 0x102ab505 CALL
			if (FElysiumEntity* TargetEntity = World != nullptr ? World->Resolve(TargetEnt) : nullptr)
			{
				Point = TargetEntity->Origin;
				bFace = true;
			}
		}
		else if (FElysiumEntity* TargetEntity = Override())                              // 0x102ab510
		// same arm: 0x102ab519 JZ, 0x102ab539 JNZ, 0x102ab53e JZ, 0x102ab549 JZ, 0x102ab560 JNZ
		//   0x102ab566 CALL, 0x102ab572 CALL, 0x102ab57e CALL, 0x102ab586 JZ
		{
			Point = TargetEntity->Origin;
			bFace = true;
		}
		else if (EnemySlot167() != nullptr)                                        // 0x102ab58c
		// same arm: 0x102ab590 CALL, 0x102ab5a3 CALL
		{
			Point = EnemyLkp();                                                    // 0x102ab5ab
			bFace = true;
		}
		if (bFace)
		{
			// `0x102ab5b0`: `VecToYaw(point - GetAbsOrigin())`, then `0x102e1c10(yaw, -2.0)`.
			// same arm: 0x102ab5c8 CALL
			const float Yaw = NpcKernelFacingShared::RetailVecToYaw(Point - Origin); // 0x102ab616
			MotorSetIdealYawAndUpdate(Yaw, YawSpeedHold);                          // 0x102ab620
		}
		AutoMovement();                                                            // 0x102ab627
		CompleteWhenActivityFinished();                                            // 0x102ab62e
		// same arm: 0x102ab630 CALL
		return 0;
	}

	// --- index 0x0a `0x102abb90`: 0x5f, 0xe9, 0xeb -- and index 0x28 `0x102abb89`: 0xea --------
	case TaskDieExplodeGib:
		BloodExplode();                                                            // 0x102abb8b
		[[fallthrough]];
	case TaskDie:
	case TaskDieGib:
	case TaskDieDueToPlayer:
	{
		const bool bFinished = IsActivityFinished()                               // 0x102abb94
		// same arm: 0x102abba1 JZ
			&& SequenceCycle >= ElysiumNpcTunables::OneDouble;                     // 0x102abba9 / 0x102abbb1 AND 0x100 (NaN is not finished)
			// same arm: 0x102abbb6 JZ
		if (!bFinished && IdealActivityNumber != ActIdle)                          // 0x102abbb8
		{
			return 0;
		}
		// `m_hClosestPlayer` gets the credit, except 0xe9 and 0x5f which credit this body.
		const FElysiumEntity* Credit =
			World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr; // 0x102abbc4
			// same arm: 0x102abbcd JZ, 0x102abbea JNZ
		if (Id == TaskDieGib || Id == TaskDie)                                     // 0x102abbf7 / 0x102abbfe
		// same arm: 0x102abbfc JZ, 0x102abc01 JNZ
		{
			Credit = this;
		}
		if (LifeState == 1)                                                  // 0x102abc05
		// same arm: 0x102abc0b JNZ
		{
			LifeState = 0;                                                   // 0x102abc0d
		}
		Die(Credit);                                                               // 0x102abc1e
		if (Id == TaskDieGib || Id == TaskDieExplodeGib)                           // 0x102abc26 / 0x102abc2d
		// same arm: 0x102abc2b JZ, 0x102abc32 JNZ
		{
			Event_Gibbed();                                                        // 0x102abc38 slot 402
		}
		if (Id == TaskDieDueToPlayer && Step->Data != ElysiumNpcTunables::Zero)    // 0x102abc3e / 0x102abc59
		// same arm: 0x102abc45 JNZ
		{
			ScriptHide();                                                          // 0x102abc63 slot 77
		}
		return 0;
	}

	// --- index 0x0b `0x102aaf2e`: 0x6e `TASK_WAIT_FOR_MOVEMENT` -----------------------------------
	case TaskWaitForMovement:
	{
		const float Limit = Step->Data;
		if ((Limit != ElysiumNpcTunables::Zero                                     // 0x102aaf31
		// same arm: 0x102aaf3c JNP
				&& Limit < static_cast<float>(Now - Schedule.TaskStartedAt))       // 0x102aaf4d
				// same arm: 0x102aaf57 JZ
			|| !NavigatorIsGoalSet())                                              // 0x102aaf63 `0x102ee620` == 0: goal TYPE, not the head waypoint
			// same arm: 0x102aaf6a JZ
		{
			BaseScheduleHost.bShouldMove = false;                                  // 0x102aafeb
			TaskComplete(false);                                                   // 0x102aaff2
			NavClearGoal();                                                        // 0x102aaffd
			return 0;
		}
		if (!NavigatorGoalIsActive())                                              // 0x102aaf72 `0x102ee6a0`
		// same arm: 0x102aaf79 JNZ
		{
			BaseScheduleHost.bShouldMove = false;                                  // 0x102aaf7d
			SetIdealActivity(ResolveLinkActivity());                               // 0x102aaf83 / 0x102aaf8b
			TaskComplete(false);                                                   // 0x102aaf94
			return 0;
		}
		if (!NavArrivedWithinTolerance())                                          // 0x102aafac `0x102f2ea0`
		// same arm: 0x102aafb5 JZ
		{
			ValidateNavGoal();                                                     // 0x102aafd4 slot 528
			return 0;
		}
		BaseScheduleHost.bShouldMove = false;                                      // 0x102aafb9
		TaskComplete(false);                                                       // 0x102aafc0
		return 0;
	}

	// --- index 0x0c / 0x0d `0x102ab00f` / `0x102ab02a`: 0x7a, 0x7b -------------------------------
	case TaskGetPathToPatrolPoint:
		IssuePatrolMoveRun(&PatrolPathCell);                                       // 0x102ab00f LEA +0x658c / 0x102ab018 -> 0x102aa860 (L10's port)
		return 0;
	case TaskGetPathToPatrolPointHunt:
		IssuePatrolMoveRun(&PatrolPathHuntCell);                                   // 0x102ab02a LEA +0x6594 / 0x102ab033 -> 0x102aa860 (L10's port)
		return 0;

	// --- index 0x0e `0x102ab83c`: the plain sequence waits under AutoMovement ---------------------
	case TaskMeleeKnockback:
	case TaskMeleeFlyingKnockbackLand:
	case TaskMeleeFlyingKnockbackWall98:
	case TaskPlayCower:
	case TaskPlayComfortInto:
	case TaskPlayComfortOutof:
	case TaskPlayPartialResistActivity:
	case TaskPlayFullResistActivity:
	case TaskKickHint:
	case TaskKickHintAt:
	case TaskKickProp:
		AutoMovement();                                                            // 0x102ab83e
		CompleteWhenActivityFinished();
		return 0;

	// --- index 0x0f `0x102ab2a3`: 0x93, 0xe0 ------------------------------------------------------
	case TaskMeleeFlyingKnockbackInto:
	case TaskDoJumpActivity:
		CompleteWhenActivityFinished();                                            // 0x102ab2a7
		return 0;

	// --- index 0x10 `0x102ac4ef`: 0x94, the knockback flight --------------------------------------
	// same arm: 0x102ac4f1 CALL
	case TaskMeleeFlyingKnockbackIdle:
	{
		if (!(NavGetType() == NavJump && IsOnGroundFlag()))                        // 0x102ac4fd / 0x102ac508
		// same arm: 0x102ac501 CALL
		{
			KnockbackHitEntityReact();                                             // 0x102ac510
			FVector NormalUnits = FVector::ZeroVector;
			if (KnockbackWallProbe(NormalUnits))                                   // 0x102ac51c
			// same arm: 0x102ac523 JZ
			{
				// `VectorAngles(normal)` (`0x10139970`); the helper takes this world's axes.
				const FVector WallAngles = NpcKernelFacingShared::FacingRetailVectorAngles(
					FVector(NormalUnits.X, -NormalUnits.Y, NormalUnits.Z));        // 0x102ac533
				const int32 Sequence = SequenceLinkedNext();                       // 0x102ac53d
				// same arm: 0x102ac546 JL
				if (Sequence < 0)
				{
					RestartIdealActivityId(ActKnockbackFly);                       // 0x102ac562
				}
				else
				{
					ForcePreTranslatedSequenceAndActivity(ActKnockbackFly, ActKnockbackFly, Sequence); // 0x102ac555
				}
				FVector NewAngles = Angles;                                        // 0x102ac56b slot 219
				NewAngles.Y = WallAngles.Y;                                        // 0x102ac585
				JumpHaltMotion();                                                  // 0x102ac58f
				Angles = NewAngles;                                                // 0x102ac59d slot 218
				KnockbackVelocity = NormalUnits * ElysiumNpcTunables::Hundred;     // 0x102ac5a7..0x102ac5e0
				SetSchedule(SchedKnockbackFly, false);                             // 0x102ac5e6 `0x102ae750`
			}
			TroikaMotor.MoveInterval = 0.f;                                        // 0x102ac5f1 motor +0x30
			// same arm: 0x102ac603 JZ
			// `CalcAbsoluteVelocity` under `EFL_DIRTY_ABSVELOCITY` (`0x102ac607`): this runtime's
			// `Velocity` is always current.
			if (!(Velocity.Z < ElysiumNpcTunables::Zero))                         // 0x102ac61d
			{
				return 0;
			}
			if (!KnockbackLanded())                                                // 0x102ac625
			// same arm: 0x102ac62c JZ
			{
				return 0;
			}
		}
		const int32 Sequence = SequenceLinkedLand();                               // 0x102ac634 `0x10345480`
		FUN_102e1270();                                                            // 0x102ac643 motor slot 8
		NavSetType(NavGround);                                                     // 0x102ac64a
		bJumping = false;                                                          // 0x102ac653
		TaskComplete(false);                                                       // 0x102ac65a
		if (Sequence < 0)                                                          // 0x102ac663
		{
			RestartIdealActivityId(ActKnockbackLand);                              // 0x102ac68a
		}
		else
		{
			ForcePreTranslatedSequenceAndActivity(ActKnockbackLand, ActKnockbackLand, Sequence); // 0x102ac672
		}
		return 0;
	}

	// --- index 0x11 `0x102abea5`: 0x96 ------------------------------------------------------------
	case TaskMeleeFlyingKnockbackWall96:
		if (Now >= KnockbackWallHitFallTime)                                        // 0x102abead / 0x102abeb5 AND 0x100 (NaN runs)
		// same arm: 0x102abeba JNZ
		{
			SetSchedule(SchedKnockbackWall, false);                                // 0x102abec9
		}
		return 0;

	// --- index 0x12 `0x102abedb`: 0x97 ------------------------------------------------------------
	case TaskMeleeFlyingKnockbackWall97:
	{
		// The playing sequence's own activity (`GetSeqDesc` + `0x10412520`).
		FString Label;
		FString ActivityName;
		const int32 PlayingActivity = SequenceDescriptor(SequenceNumber, Label, ActivityName) // 0x102abee4
		// same arm: 0x102abeeb JZ
			? ActivityIdForName(ActivityName) : INDEX_NONE;                        // 0x102abef3
		if (PlayingActivity == ActKnockbackFly && IsActivityFinished())            // 0x102abefb / 0x102abf06
		// same arm: 0x102abf00 JNZ, 0x102abf0e JZ
		{
			const int32 Sequence = SequenceLinkedNext();                           // 0x102abf12
			// same arm: 0x102abf1b JL
			if (Sequence < 0)
			{
				RestartIdealActivityId(ActKnockbackWallHit);                       // 0x102abf37
				// same arm: 0x102abf3e CALL
			}
			else
			{
				ForcePreTranslatedSequenceAndActivity(ActKnockbackWallHit, ActKnockbackWallHit, Sequence); // 0x102abf2a
			}
		}
		if (!(NavGetType() == NavJump && IsOnGroundFlag()))                        // 0x102abf4a / 0x102abf55
		// same arm: 0x102abf4e CALL
		{
			TroikaMotor.MoveInterval = 0.f;                                        // 0x102abf5d
			// same arm: 0x102abf6f JZ, 0x102abf73 CALL
			if (!(Velocity.Z < ElysiumNpcTunables::Zero))                         // 0x102abf89
			{
				return 0;
			}
			if (!KnockbackLanded())                                                // 0x102abf91
			// same arm: 0x102abf98 JZ
			{
				return 0;
			}
		}
		const int32 Sequence = SequenceLinkedNext();                               // 0x102abfa0 `0x103454c0`
		FUN_102e1270();                                                            // 0x102abfaf
		NavSetType(NavGround);                                                     // 0x102abfb6
		bJumping = false;                                                          // 0x102abfbd
		// same arm: 0x102abfc6 JL
		if (Sequence < 0)
		{
			RestartIdealActivityId(ActKnockbackWallFall);                          // 0x102abfe5
		}
		else
		{
			ForcePreTranslatedSequenceAndActivity(ActKnockbackWallFall, ActKnockbackWallFall, Sequence); // 0x102abfd5
		}
		TaskComplete(false);                                                       // 0x102ab931
		return 0;
	}

	// --- index 0x13 `0x102ac2ed`: 0x99 `TASK_MELEE_HIT_BY_FINISHING_MOVE` --------------------------
	case TaskMeleeHitByFinishingMove:
	{
		AutoMovement();                                                            // 0x102ac2ef
		FVector BoneUnits = FVector::ZeroVector;
		FVector BoneAngles = FVector::ZeroVector;
		RetailBonePosition(TEXT("Bip01 Spine2"), BoneUnits, BoneAngles);           // 0x102ac305
		// same arm: 0x102ac30e CALL
		if (!IsActivityFinished())                                                 // 0x102ac316
		{
			FinishingMoveBoneTrackLastTime = Now;                                  // 0x102ac4c7
			FinishingMoveBoneTrackLastPos = BoneUnits;                             // 0x102ac4d1..0x102ac4dd
			return 0;
		}
		JumpCommit();                                                              // 0x102ac31e `0x102c4e80`
		FElysiumDmg Dmg;                                                           // 0x102ac327 `CVDmg_t`
		Dmg.Source = Handle;                                                       // 0x102ac331 `SetSrc(this)`
		Dmg.BaseDamage = 1;                                                        // 0x102ac354 `m_iDiceAmt`
		Dmg.ExtraInput = 1;                                                        // 0x102ac358 `m_iToHitSuccesses`
		FElysiumTakeDamageInfo Info;                                               // 0x102ac35c `0x101c26d0`
		Info.Dmg = &Dmg;
		Info.Attacker = Handle;
		Info.Damage = ElysiumNpcTunables::One;
		Info.DamageBits = 0;
		Info.AmmoType = INDEX_NONE;
		FVector Travel = FinishingMoveBoneTrackLastPos - BoneUnits;               // 0x102ac361..0x102ac39f
		Travel.Normalize();                                                        // 0x102ac3a7 `VectorNormalize`
		const float Elapsed = static_cast<float>(FinishingMoveBoneTrackLastTime - Now); // 0x102ac3b7
		FinishingMoveDamageForce = Travel * Elapsed * FinishingMoveForceScale;     // 0x102ac3e2..0x102ac41c
		// `SetBaseToStatValue(0xf, 0x11)` on the type-0 stat list (`0x102ac423..0x102ac471`).
		// same arm: 0x102ac433 JZ, 0x102ac43b JL, 0x102ac444 JNZ, 0x102ac454 CALL, 0x102ac45e CALL
		TypedStatSet(FinishingMoveStatList, FinishingMoveStatTarget,
			TypedStatValue(FinishingMoveStatList, FinishingMoveStatSource));
		Event_Killed(&Info);                                                       // 0x102ac482 slot 144
		Event_Dying();                                                             // 0x102ac48c slot 403
		TaskComplete(false);                                                       // 0x102ac496
		// same arm: 0x102ac49f CALL
		return 0;
	}

	// --- index 0x14 `0x102abfef` / 0x16 `0x102ac145`: 0x9c, 0x9e -------------------------------
	case TaskOnFireInto:
	case TaskOnFireOutof:
		if (IsActivityFinished())                                                  // 0x102abff3 / 0x102ac149
		{
			TaskComplete(false);                                                   // 0x102ac14f
			// same arm: 0x102ac153 JZ, 0x102ac157 CALL
			return 0;
		}
		AutoMovement();                                                            // 0x102ac133
		return 0;

	// --- index 0x15 `0x102abffe`: 0x9d `TASK_ON_FIRE_LOOP` ----------------------------------------
	case TaskOnFireLoop:
	{
		if (!IsActivityFinished())                                                 // 0x102ac002
		// same arm: 0x102ac00a JZ
		{
			AutoMovement();                                                        // 0x102ac133
			return 0;
		}
		// Drain the queued burn records into this body, one at a time: take record 0, then move the
		// last record into slot 0 and shrink (`0x10430fa0` memmove of 0x4c bytes).
		while (QueuedBurnDamage.Num() != 0)                                        // 0x102ac016
		// same arm: 0x102ac018 JZ
		{
			const FElysiumDmg Record = QueuedBurnDamage[0];
			FElysiumEntity* SourceEntity = World != nullptr ? World->Resolve(Record.Source) : nullptr;
			TakeDamage(Record, SourceEntity != nullptr ? SourceEntity->AsCombatCharacter() : nullptr); // 0x102ac023
			if (QueuedBurnDamage.Num() > 0)                                        // 0x102ac02e
			// same arm: 0x102ac030 JLE
			{
				QueuedBurnDamage.RemoveAtSwap(0);          // 0x102ac046 / 0x102ac055
				// same arm: 0x102ac063 JNZ
			}
		}
		if (IsAlive())                                                             // 0x102ac069 slot 158
		// same arm: 0x102ac071 JZ
		{
			for (FElysiumEntityHandle& Particle : BodyFireParticles)               // 0x102ac077..0x102ac10b
			// same arm: 0x102ac087 JZ, 0x102ac0a2 JNZ, 0x102ac0a7 JZ, 0x102ac0ac JNZ, 0x102ac0bc JZ
			//   0x102ac0c1 JNZ, 0x102ac0cd CALL, 0x102ac0d8 JZ, 0x102ac0f5 JNZ, 0x102ac102 CALL
			{
				FElysiumEntity* Emitter = World != nullptr ? World->Resolve(Particle) : nullptr;
				if (Emitter != nullptr)
				{
					// The emitter's `+0x484` gate, its slot 242 and the 2.0 s fade: SEAM, counted.
					++BodyFireParticleStops;
				}
			}
		}
		Slot616();                                                                 // 0x102ac115 slot 616
		TaskComplete(false);                                                       // 0x102ac11b
		return 0;
	}

	// --- index 0x17 `0x102ab900`: 0xa7, 0x11d, 0x12e ----------------------------------------------
	case TaskTest1:
	case TaskFaceLastAngle:
	case TaskFaceSavePosition:
		TurnUnlessMemory();                                                        // 0x102ab906 / 0x102ab90f
		// same arm: 0x102ab909 JNZ
		MotorUpdateYaw(UpdateYawDefault);                                          // 0x102ab915
		if (FacingIdeal())                                                         // 0x102ab922
		{
			TaskComplete(false);                                                   // 0x102ab931
		}
		return 0;

	// --- index 0x18 `0x102ab8f6`: 0xb2 `TASK_FACE_INTEREST` ---------------------------------------
	case TaskFaceInterest:
		if (CurrentAmbientSpot() == nullptr)                                       // 0x102ab8fe `+0x62ec`
		{
			Fail(LineFaceInterest, FailNoInterestingPlace);                        // 0x102ab961
			return 0;
		}
		TurnUnlessMemory();
		MotorUpdateYaw(UpdateYawDefault);
		if (FacingIdeal())
		{
			TaskComplete(false);
		}
		return 0;

	// --- index 0x19 `0x102ab974`: 0xb3 `TASK_FACE_PATROL_INTEREST` --------------------------------
	case TaskFacePatrolInterest:
		// `0x1029f780(this, &m_sppPatrolPath)`: the path's current node.
		if (ResolvePatrolInterestPlace(PatrolCurrentNode(PatrolPathCell)) == 0)    // 0x102ab97d / 0x102ab984
		{
			TaskComplete(false);                                                   // 0x102ac11b
			// same arm: 0x102ac11f CALL
			return 0;
		}
		TurnUnlessMemory();                                                        // 0x102ab990
		// same arm: 0x102ab993 JNZ, 0x102ab999 CALL
		MotorUpdateYaw(UpdateYawDefault);                                          // 0x102ab9a7
		if (FacingIdeal())                                                         // 0x102ab9ae
		// same arm: 0x102ab9b5 JZ
		{
			ScheduleHost.Unknown6300 = 0;                                          // 0x102ab9c0
			ScheduleHost.Unknown659c = 0;                                          // 0x102ab9c6
			TaskComplete(false);                                                   // 0x102ab9cc
		}
		return 0;

	// --- index 0x1a `0x102ab9de`: 0xb4 `TASK_DO_INTEREST_ACTIVITY` --------------------------------
	case TaskDoInterestActivity:
	{
		FElysiumInterestingPlace* Place = CurrentAmbientSpot();                    // 0x102ab9de
		// same arm: 0x102ab9e8 JZ
		if (Place == nullptr)
		{
			Fail(LineDoInterest, FailLostInterestingPlace);                        // 0x102aba59
			return 0;
		}
		if (!RunInterestingPlaceLoop(Place, Now))                                  // 0x102ab9eb `0x102aa210`
		// same arm: 0x102ab9f2 JZ
		{
			return 0;
		}
		if (PlaceHolstersWeapon(*Place) && ActiveWeaponEntity() != nullptr)       // 0x102ab9fe / 0x102aba0a
		// same arm: 0x102aba06 JZ, 0x102aba11 JZ
		{
			++WeaponHolsterCalls;                                                  // 0x102aba17 weapon slot 315
		}
		// `0x102b53d0(this, 1, "Leaving interesting place (RunTask-WaitFinished)")`: the release,
		// through the port's one release transaction (see `LeaveInterestingPlaceOnRemove`). Inside
		// it, with the argument 1 and `+0x62e8` arrived, `0x102b54f1..0x102b5500` fire this NPC's
		// `m_OnInterestingPlaceLeft` (`+0x5f8c`, activator the place `+0x62ec`) before
		// `0x102da600(place, this, 1, fired)` -- the port's release did not fire it (L05
		// integration). `0x102b53d0`'s other work (its two sounds, the `+0x14b8` / `+0x14bc` bit
		// clears, `0x102ae310`) is not this family's row and stays with the port's release.
		if (FElysiumInterestingPlace* Leaving = CurrentAmbientSpot())             // 0x102aba26
		{
			if (bAmbientArrived)
			{
				FireOutput(FName(TEXT("OnInterestingPlaceLeft")), Leaving->Handle);  // 0x102b54f1 `0x100cd660`
			}
			FinishAmbientUse(bAmbientArrived, /*bStopMovement=*/false);   // `0x102b53d0` stops no motor
		}
		bAmbientArrived = false;
		++InterestingPlaceReleases;
		RecordScheduleEvent(TEXT("Leaving interesting place (RunTask-WaitFinished)"));
		TaskComplete(false);                                                       // 0x102aba2f
		return 0;
	}

	// --- index 0x1b `0x102aba6c`: 0xb5 `TASK_DO_PATROL_INTEREST_ACTIVITY` -------------------------
	case TaskDoPatrolInterestActivity:
	{
		const int32 PlaceIndex = ResolvePatrolInterestPlace(PatrolCurrentNode(PatrolPathCell)); // 0x102aba75
		if (PlaceIndex == 0)                                                       // 0x102aba82
		{
			TaskComplete(false);                                                   // 0x102abb10
			return 0;
		}
		FElysiumInterestingPlace* Place = PlaceAt(World, PlaceIndex);
		if (!RunInterestingPlaceLoop(Place, Now))                                  // 0x102aba89
		// same arm: 0x102aba90 JZ
		{
			return 0;
		}
		if (Place != nullptr && PlaceHolstersWeapon(*Place) && ActiveWeaponEntity() != nullptr) // 0x102aba96
		// same arm: 0x102aba9e JZ, 0x102abaa2 CALL, 0x102abaa9 JZ
		{
			++WeaponHolsterCalls;                                                  // 0x102abaaf
		}
		if (bAmbientArrived)                                                       // 0x102abab5
		// same arm: 0x102ababd JZ
		{
			// `m_OnInterestingPlaceLeft.FireOutput(place, this, 0)` (`+0x5f8c`, `0x100cd660`).
			FElysiumEntityHandle PlaceHandle = Place != nullptr ? Place->Handle : FElysiumEntityHandle::Invalid();
			FireOutput(FName(TEXT("OnInterestingPlaceLeft")), PlaceHandle);        // 0x102abace
		}
		// `0x102da600(place, this, 0, arrived)` (`0x102abad3..0x102abae2`, EBX = 0): with the second
		// argument 0 it only fires the place's `OnNPCLeft` (`+0x468`) when arrived -- no claimant
		// removal, no `+0x564` decrement (L05 integration: the port also released the claim).
		if (Place != nullptr && bAmbientArrived)                                   // 0x102abae2 `0x102da600`
		{
			Place->Left(Handle);
		}
		ScheduleHost.Unknown6300 = 0;                                              // 0x102abaea
		ScheduleHost.Unknown659c = 0;                                              // 0x102abaf0
		bAmbientArrived = false;                                                   // 0x102abaf6
		TaskComplete(false);                                                       // 0x102abafd
		return 0;
	}

	// --- index 0x1c `0x102abb22`: 0xb6, 0xb7 ------------------------------------------------------
	case TaskDoLoiterActivity:
	case TaskDoInteractActivity:
		if (IsActivityFinished())                                                  // 0x102abb26
		// same arm: 0x102abb2e JZ
		{
			int32 WeaponActivity = 0;
			const int32 Walk = TranslateActivityNumber(ActWalk, WeaponActivity);   // 0x102abb3a `0x10271ff0(9,0)`
			NavSetMovementActivity(Walk);                                          // 0x102abb46
			TaskComplete(false);                                                   // 0x102ab931
		}
		return 0;

	// --- index 0x1d `0x102abb50`: 0xb8 ------------------------------------------------------------
	case TaskDoInterestDeathActivity:
		if (IsActivityFinished())                                                  // 0x102abb54
		// same arm: 0x102abb5c JZ
		{
			ClearAiFlags(AiFlagsInterestDeathClear);                               // 0x102abb6a
			MoveToBoneOriginAngles(TEXT("Bip01"), false, true);                    // 0x102abb7f
			TaskComplete(false);                                                   // 0x102ab931
		}
		return 0;

	// --- index 0x1e `0x102ab303`: 0xb9 `TASK_RUN_DIALOG` ------------------------------------------
	case TaskRunDialog:
	{
		const int32 Activity = RunDialogActivity();                                // 0x102ab305 `0x102c1400`
		if (Activity != INDEX_NONE)                                                // 0x102ab30d
		// same arm: 0x102ab30f JNZ
		{
			SetActivity(Activity);                                                 // 0x102ab331 slot 310
			MotorUpdateYaw(UpdateYawDefault);                                      // 0x102ab33f
			return 0;
		}
		TaskComplete(false);                                                       // 0x102ab313
		Cognition.Conditions.Clear(EElysiumNpcCond::HearPlayer);                   // 0x102ab31c `ClearCondition(0x6f)`
		return 0;
	}

	// --- index 0x1f `0x102ab351`: 0xba, 0xbb ------------------------------------------------------
	case TaskRunDisposition:
	case TaskRunDispositionRandom:
		Slot588();                                                                 // 0x102ab355 slot 588
		WaitTest();
		return 0;

	// --- index 0x20 `0x102ab369`: 0xbc, 0xbd ------------------------------------------------------
	case TaskSpecialIdleActivity:
	case TaskSpecialIdleActivityRandom:
	{
		const FElysiumEntity* Player =
			World != nullptr ? World->Resolve(Senses.Memory.ClosestPlayer) : nullptr; // 0x102ab369..0x102ab398
			// same arm: 0x102ab372 JZ, 0x102ab393 JNZ
		const float PlayerDistUnits = Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U;
		if (Player == nullptr
			|| !(PlayerDistUnits <= SpecialIdlePlayerDistance)                     // 0x102ab3ab `TEST AH,0x41; JP` (greater or NaN)
			|| !Cognition.Conditions.HasOrdinal(CondSeePlayerId))                  // 0x102ab3b1 `HasCondition(0x5a)`
			// same arm: 0x102ab3b8 JZ
		{
			Slot588();                                                             // 0x102ab40d
		}
		else if (IsActivityFinished())                                             // 0x102ab3be
		// same arm: 0x102ab3c6 JZ
		{
			if (SelectWeightedSequenceForActivity(ActInterestIdle) == INDEX_NONE)  // 0x102ab3d1
			// same arm: 0x102ab3db JNZ
			{
				RestartIdealActivityId(ActInterestIdle);                           // 0x102ab3e2
			}
			else
			{
				Slot588();                                                         // 0x102ab3f6
			}
		}
		WaitTest();                                                                // 0x102aad3c
		return 0;
	}

	// --- index 0x21 `0x102ab465`: 0xdd `TASK_KNOCKOUT` --------------------------------------------
	// same arm: 0x102ab469 CALL
	case TaskKnockout:
		CompleteWhenActivityFinished();
		return 0;

	// --- index 0x22 `0x102ab045`: 0xde `TASK_UNKNOCKOUT` ------------------------------------------
	case TaskUnknockout:
		if (!IsActivityFinished())                                                 // 0x102ab049
		// same arm: 0x102ab051 JZ
		{
			return 0;
		}
		if (IdealActivityNumber != ActKnockedOut && IdealActivityNumber == ActUnknockout) // 0x102ab05d / 0x102ab064
		// same arm: 0x102ab062 JZ, 0x102ab067 JNZ
		{
			TaskComplete(false);                                                   // 0x102ab06d
			NpcFlags.AssignAiFlagsWord(NpcFlags.RawWord1() & AiFlagsUnknockoutKeep); // 0x102ab079
			return 0;
		}
		RestartIdealActivityId(ActUnknockout);                                     // 0x102ab097
		return 0;

	// --- index 0x23 `0x102abc76`: 0xdf `TASK_DIE_IMMEDIATE` ---------------------------------------
	case TaskDieImmediate:
		Die(nullptr);                                                              // 0x102abc7e `Die(0,0,0)`
		return 0;

	// --- index 0x24 / 0x26 `0x102ab420` / `0x102ab474`: 0xe1, 0xe3 ------------------------------
	// same arm: 0x102ab477 CALL, 0x102ab486 JZ, 0x102ab48c CALL, 0x102ab494 JNZ, 0x102ab49f CALL
	case TaskDoLoopActivity:
	case TaskDoBlendLoopActivity:
	{
		const int32 Activity = static_cast<int32>(Step->Data);                     // 0x102ab420 `__ftol`
		// same arm: 0x102ab423 CALL
		if (!bLastDisciplineResist || !IsActivityFinished())                       // 0x102ab42a / 0x102ab438
		// same arm: 0x102ab432 JZ, 0x102ab440 JNZ
		{
			SetActivity(Activity);                                                 // 0x102ab44b slot 310
			return 0;
		}
		TaskComplete(false);                                                       // 0x102ab931
		return 0;
	}

	// --- index 0x25 `0x102ab45e`: 0xe2, 0x149 -----------------------------------------------------
	case TaskDoBlendActivity:
	case TaskPlayDeathSequence:
		AutoMovement();                                                            // 0x102ab460
		CompleteWhenActivityFinished();
		return 0;

	// --- index 0x27 `0x102ab4b2`: 0xe7 `TASK_SET_COWER` -------------------------------------------
	case TaskSetCower:
		if (SequenceNumber == IdealSequence)                                       // 0x102ab4be
		{
			TaskComplete(false);
		}
		return 0;

	// --- index 0x29 `0x102aad71`: 0xed -- the epilogue; the task keeps running --------------------
	case TaskDoComfortLoop:
		return 0;

	// --- index 0x2a `0x102ab63b`: 0xf7 `TASK_FACE_NEXT_NODE` --------------------------------------
	// same arm: 0x102ab644 JNZ, 0x102ab64e CALL
	case TaskFaceNextNode:
		TurnUnlessMemory();
		MotorUpdateYaw(UpdateYawDefault);                                          // 0x102ab915
		// same arm: 0x102ab91d CALL
		if (FacingIdeal())
		{
			TaskComplete(false);
		}
		return 0;

	// --- index 0x2b `0x102ab76a`: the look tasks 0xf8..0xff, 0x148 --------------------------------
	case TaskLookAtBestSound:
	case TaskAlertLookAtBestSound:
	case TaskAlertLookAtRandomLoc:
	case TaskLookAtPlayer:
	case TaskLookAtBestUnknown:
	case TaskAlertLookAtDetectedAttack:
	case TaskUnlookAt:
	case TaskUnlookAtFace:
	case TaskAlertLookAtUnknownAttacker:
		MotorUpdateYaw(UpdateYawDefault);                                          // 0x102ab770
		// same arm: 0x102ab772 CALL
		if ((BaseScheduleHost.WaitFinished <= Now || IsActivityFinished())         // 0x102ab780 / 0x102ab793
		// same arm: 0x102ab78d JZ, 0x102ab79b JZ
			&& FacingIdeal())                                                      // 0x102ab7a3
			// same arm: 0x102ab7aa JZ
		{
			ClearAiFlags(AiFlagsLookClear);                                        // 0x102ab7b8
			TaskComplete(false);                                                   // 0x102ab7b8
			// same arm: 0x102ab7c6 CALL
		}
		return 0;

	// --- index 0x2c `0x102ab7d8`: 0x107..0x109 `TASK_ATTEMPT_DIVE*` -------------------------------
	// same arm: 0x102ab7dc CALL
	case TaskAttemptDive:
	case TaskAttemptDiveSide:
	case TaskAttemptDiveForward:
		if (!IsActivityFinished())                                                 // 0x102ab7e4
		{
			return 0;
		}
		if (IdealActivityNumber == ActDive)                                        // 0x102ab7ea
		{
			SetIdealActivity(ActKnockedOut);                                       // 0x102ab7fd
			return 0;
		}
		ClearAiFlags(AiFlagsDiveClear);                                            // 0x102ab817 `AND CH,0xbf`
		TaskComplete(false);                                                       // 0x102ab822
		return 0;

	// --- index 0x2d `0x102ab834`: 0x10a..0x10c `TASK_PLAY_COVER_*` --------------------------------
	case TaskPlayCoverInto:
	case TaskPlayCoverIdle:
	case TaskPlayCoverOutof:
		FacePendingAimTarget();                                                    // 0x102ab837
		AutoMovement();                                                            // 0x102ab83e
		// same arm: 0x102ab847 CALL
		CompleteWhenActivityFinished();
		return 0;

	// --- index 0x2e `0x102ab852`: 0x10d `TASK_PLAY_COVER_AIM` -------------------------------------
	case TaskPlayCoverAim:
	{
		AutoMovement();                                                            // 0x102ab854
		// On the no-override, cover-object-is-not-the-enemy arm retail completes and then still aims
		// at an uninitialised point and may complete again (`0x102ab8ec` -> `0x102aaf14`); the port
		// aims at the zero vector there (named divergence) and keeps both completions.
		FVector Aim = FVector::ZeroVector;
		if (FElysiumEntity* TargetEntity = Override())                                   // 0x102ab859..0x102ab887
		// same arm: 0x102ab868 JZ, 0x102ab882 JNZ
		{
			Aim = TargetEntity->Origin;                                                  // 0x102aaea6
			// same arm: 0x102aaeaf JZ, 0x102aaeca JNZ, 0x102aaed4 CALL, 0x102aaee0 CALL
		}
		else
		{
			const FElysiumEntity* Cover =
				World != nullptr ? World->Resolve(ScheduleHost.HintCoverObject) : nullptr; // 0x102ab88d
				// same arm: 0x102ab896 JZ, 0x102ab8ad JNZ, 0x102ab8b7 CALL, 0x102ab8c8 CALL
			if (Cover == EnemySlot167())                                           // 0x102ab8ce
			// same arm: 0x102ab8d2 JNZ, 0x102ab8d6 CALL
			{
				Aim = EnemyLkp();                                                  // 0x102ab8e5 -> 0x102aaeef
				// same arm: 0x102aaef3 CALL
			}
			else
			{
				TaskComplete(false);                                               // 0x102ab8ec
			}
		}
		MotorSetIdealYawToTargetAndUpdate(Aim, YawSpeedDefault);                   // 0x102aaf24
		if (FacingIdeal())                                                         // 0x102ab922
		// same arm: 0x102ab924 CALL, 0x102ab92b JZ
		{
			TaskComplete(false);                                                   // 0x102ab931
		}
		return 0;
	}

	// --- index 0x2f / 0x30 `0x102abc90` / `0x102abcc9`: 0x116, 0x117 -----------------------------
	case TaskResolveBotch:
	case TaskResolveBotchInCover:
		if (IsActivityFinished())                                                  // 0x102abc94 / 0x102abccd
		// same arm: 0x102abc9c JZ
		{
			TaskComplete(false);                                                   // 0x102abca4
			ClearAiFlags(AiFlagsBotchClear);                                       // 0x102abcb2
		}
		return 0;

	// --- index 0x31 `0x102abcd5`: 0x118 `TASK_RESOLVE_BOTCH_OUT_OF_COVER` -------------------------
	case TaskResolveBotchOutOfCover:
	{
		if (!IsActivityFinished())                                                 // 0x102abcd9
		// same arm: 0x102abce1 JZ
		{
			return 0;
		}
		FHintWords Hint;
		const int32 HintType = HintWords(BaseScheduleHost.HintNode, Hint) ? Hint.HintType : INDEX_NONE;
		const int32 InCover = FElysiumNpcScheduleHost::HintNodeActivity(           // 0x102abcef `0x102a13d0`
			EElysiumHintActivityQuery::Query102a13d0, HintType, bLeaningLeft);
		if (IdealActivityNumber == InCover)                                        // 0x102abcf4
		// same arm: 0x102abcf6 JNZ
		{
			// `0x102a1620`: the `0x102a1510` activity restarted when there is one.
			const int32 Out = FElysiumNpcScheduleHost::HintNodeActivity(
				EElysiumHintActivityQuery::Query102a1510, HintType, bLeaningLeft);
			if (Out != INDEX_NONE)
			{
				RestartIdealActivityId(Out);                                       // 0x102abcf8
			}
			return 0;
		}
		const int32 Peek = FElysiumNpcScheduleHost::HintNodeActivity(              // 0x102abd0a `0x102a1510`
			EElysiumHintActivityQuery::Query102a1510, HintType, bLeaningLeft);
		if (IdealActivityNumber == Peek && Step->Data != ElysiumNpcTunables::Zero) // 0x102abd0f / 0x102abd23
		// same arm: 0x102abd13 JNZ
		{
			// `0x102a15c0`: the `0x102a1470` activity restarted when there is one.
			const int32 Back = FElysiumNpcScheduleHost::HintNodeActivity(
				EElysiumHintActivityQuery::Query102a1470, HintType, bLeaningLeft);
			if (Back != INDEX_NONE)
			{
				RestartIdealActivityId(Back);                                      // 0x102abd29
			}
			return 0;
		}
		TaskComplete(false);                                                       // 0x102abca4
		// same arm: 0x102abca6 CALL
		ClearAiFlags(AiFlagsBotchClear);
		return 0;
	}

	// --- index 0x32 `0x102abd3b`: 0x11b, 0x11c, 0x123 ---------------------------------------------
	case TaskStepBack:
	case TaskStepBackRun:
	case TaskCircleEnemy:
		AutoMovement();                                                            // 0x102abd3d
		if (FElysiumEntity* Enemy = EnemySlot167())                                // 0x102abd44
		// same arm: 0x102abd46 CALL, 0x102abd4e JZ, 0x102abd54 CALL, 0x102abd5e CALL
		{
			MotorSetIdealYawToTargetAndUpdate(Enemy->Origin, YawSpeedDefault);     // 0x102abd88
		}
		if (Now < BaseScheduleHost.WaitFinished)                                   // 0x102abd96
		// same arm: 0x102abda3 JNZ
		{
			return 0;
		}
		ScheduleHost.DesiredMoveYaw = 0.f;                                         // 0x102abda9
		TaskComplete(false);                                                       // 0x102ab931
		return 0;

	// --- index 0x33 `0x102abdb8`: 0x122, 0x124 ----------------------------------------------------
	case TaskMeleeCircleEnemy:
	case TaskCircleEnemyFullCycle:
	{
		AutoMovement();                                                            // 0x102abdba
		// same arm: 0x102abdc3 CALL, 0x102abdcb JZ, 0x102abdd1 CALL, 0x102abddb CALL
		if (FElysiumEntity* Enemy = EnemySlot167())
		{
			MotorSetIdealYawToTargetAndUpdate(Enemy->Origin, YawSpeedDefault);     // 0x102abe05
		}
		if (!IsActivityFinished())                                                 // 0x102abe0e
		// same arm: 0x102abe16 JZ
		{
			return 0;
		}
		if (Now < BaseScheduleHost.WaitFinished                                    // 0x102abe24
		// same arm: 0x102abe31 JZ
			&& !Cognition.Conditions.Has(EElysiumNpcCond::ShouldStepback))         // 0x102abe3b `HasCondition(0xe)`
			// same arm: 0x102abe42 JNZ
		{
			int32 Activity = ActCircle;
			if (SelectWeightedSequenceForActivity(ActCircle) == INDEX_NONE)        // 0x102abe52 `0x10295460`
			// same arm: 0x102abe5a JNZ
			{
				Activity = ActWalk;
				if (SelectWeightedSequenceForActivity(ActWalk) == INDEX_NONE)      // 0x102abe66
				// same arm: 0x102abe6e JNZ
				{
					Fail(LineCircleNoActivity, FailBadActivity);                   // 0x102abe8a
				}
			}
			RestartIdealActivityId(Activity);                                      // 0x102abe93
			return 0;
		}
		ScheduleHost.DesiredMoveYaw = 0.f;                                         // 0x102abda9
		TaskComplete(false);
		return 0;
	}

	// --- index 0x34 `0x102ac169`: 0x128 `TASK_MELEE_CHEER` ----------------------------------------
	case TaskMeleeCheer:
	{
		AutoMovement();                                                            // 0x102ac16b
		// same arm: 0x102ac179 JZ, 0x102ac199 JNZ, 0x102ac19e JZ, 0x102ac1a9 JZ, 0x102ac1c0 JNZ
		//   0x102ac1ca CALL, 0x102ac1ea CALL, 0x102ac1f4 JZ, 0x102ac1f8 CALL, 0x102ac20b CALL
		//   0x102ac213 CALL
		FVector Aim = FVector::ZeroVector;   // retail: uninitialised on the no-enemy arm (divergence)
		if (FElysiumEntity* TargetEntity = Override())
		{
			Aim = TargetEntity->Origin;
		}
		else if (EnemySlot167() != nullptr)
		{
			Aim = EnemyLkp();
		}
		else
		{
			Fail(LineCheerNoEnemy, FailNoEnemy);                                   // 0x102ac246
		}
		if (!FInAimCone(Aim))                                                      // 0x102ac255
		// same arm: 0x102ac25d JNZ
		{
			MotorSetIdealYawToTargetAndUpdate(Aim, YawSpeedHold);                  // 0x102ac26f
		}
		if (Now < BaseScheduleHost.WaitFinished)                                   // 0x102ac284
		// same arm: 0x102ac289 JNZ
		{
			return 0;
		}
		CompleteWhenActivityFinished();                                            // 0x102ac28f
		// same arm: 0x102ac293 CALL
		return 0;
	}

	// --- index 0x35 `0x102ac29e`: 0x137 `TASK_PLAY_COMBAT_START_SEQUENCE` -------------------------
	case TaskPlayCombatStartSequence:
		AutoMovement();                                                            // 0x102ac2a0
		// same arm: 0x102ac2a9 CALL, 0x102ac2b1 JZ, 0x102ac2bb CALL, 0x102ac2c5 CALL
		if (FElysiumEntity* Enemy = EnemySlot167())
		{
			MotorSetIdealYawToTargetAndUpdate(Enemy->Origin, YawSpeedDefault);     // 0x102ab297
			// same arm: 0x102ab29e CALL
		}
		CompleteWhenActivityFinished();
		return 0;

	// --- index 0x36 `0x102ac743`: 0x139, 0x13b, 0x13c ---------------------------------------------
	case TaskPreJump:
	case TaskLand:
	case TaskLandHard:
		MotorUpdateYaw(UpdateYawDefault);                                          // 0x102ac749
		// same arm: 0x102ac74b CALL
		CompleteWhenActivityFinished();                                            // 0x102ac750
		// same arm: 0x102ac754 CALL
		return 0;

	// --- index 0x37 `0x102ac69c`: 0x13a `TASK_JUMP` -----------------------------------------------
	// same arm: 0x102ac69e CALL
	case TaskJump:
		if (!(NavGetType() == NavJump && IsOnGroundFlag()))                        // 0x102ac6a3 / 0x102ac6ae
		// same arm: 0x102ac6aa JNZ, 0x102ac6b5 JNZ, 0x102ac6c1 CALL
		{
			const float Yaw = NpcKernelFacingShared::RetailVecToYaw(Velocity);      // 0x102ac6cd slot 198 / `0x101d2c70`
			MotorSetIdealYawAndUpdate(Yaw, YawSpeedDefault);                       // 0x102ac6c7
			TroikaMotor.MoveInterval = 0.f;                                        // 0x102ac6d2
			// same arm: 0x102ac6d7 CALL, 0x102ac6f4 JZ, 0x102ac6f8 CALL
			if (!(Velocity.Z < ElysiumNpcTunables::Zero))                         // 0x102ac70e
			{
				return 0;
			}
			if (!KnockbackLanded())                                                // 0x102ac716
			{
				return 0;
			}
		}
		FUN_102e1270();                                                            // 0x102ac716 motor slot 8
		NavSetType(NavGround);                                                     // 0x102ac71d
		bJumping = false;                                                          // 0x102ac72b
		// same arm: 0x102ac732 CALL
		TaskComplete(false);                                                       // 0x102ab931
		// same arm: 0x102ab935 CALL
		return 0;

	default:
		break;
	}

	// Index 0x38 `0x102aad69` and every id outside `2..0x149`: `CAI_BaseNPC::RunTask`.
	return FElysiumNpcBase::RunTaskSlot444(Task);                                  // 0x102aad6c
}
