// `CAI_BaseNPC`'s bodies of the `Lifecycle19` family (story 5 step 5): the base layer's half of
// what the Troika family files held. Declarations are in `ElysiumNpcBaseLifecycle2.inl` (included inside
// `class FElysiumNpcBase`), or generated in `ElysiumNpcBaseSlots.inl` for a slot body.

#include "Substrate/ElysiumNpcBase.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumRng.h"
#include "ElysiumSessionSubsystem.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumNpc.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcLifecycle2Shared.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumRulebook.h"
#include "Substrate/ElysiumRulebookSubsystem.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- Moved from `ElysiumNpcLifecycle2.cpp` (story 5 step 5) ---

const TCHAR* FElysiumNpcBase::NpcInitThinkFunction()
{
	return TEXT("0x10273aa0");
}

const TCHAR* FElysiumNpcBase::StartNpcThinkFunction()
{
	return TEXT("LAB_1000f4e8");
}

void FElysiumNpcBase::ThinkSet(const TCHAR* Function, double Delay)
{
	++ThinkSetCalls;                                                     // ThinkSet
	ThinkFunctionName = Function != nullptr ? FString(Function) : FString();
	ThinkSetDelay = Delay;
	if (Function == nullptr || Function[0] == 0)
	{
		NextThink = ELYSIUM_NEVER_THINK;                                 // ThinkSet(NULL)
		return;
	}
	if (Delay != 0.0)
	{
		ArmThinkAt(NpcKernelLifecycle19Shared::Lifecycle19Now(*this) + Delay);
	}
}

bool FElysiumNpcBase::MoveProbeFloorDrop(FVector& InOutOriginUnits)
{
	// SEAM for `CAI_MoveProbe::TraceHull` `0x102e7880` on `m_pMoveProbe +0x5d40`, mask `0x202400b`,
	// swept from `0.0` down to `-256.0`. Retail answers 1 when the sweep FOUND floor (fraction != 1)
	// and writes `param_5 = trace.endpos`; it answers 0 — the "stuck in wall" warning — otherwise.
	// Three searches: the address has no port body; `+0x5d40` is the CHAIN row onto Motor; the two
	// probe bodies this runtime does carry are `MoveProbeCheckStandPosition` (`0x102e7270`) and
	// `MotorMoveTraceSweep` (`0x102e6d70`), neither of which is this hull sweep. Answers the
	// found-floor arm with a zero-length drop, so the origin is left where the caller put it.
	(void)InOutOriginUnits;
	return true;
}

void FElysiumNpcBase::RestoreGiveUp()
{
	// `0x1027be60`.
	if (Motor != nullptr)
	{
		Motor->ClearNavigationGoal();                                    // 1027be6x
	}
	++NavigationGoalClears;
	ClearSchedule();                                                     // 10280d30
	if (GetEnemy() == nullptr)
	{
		Cognition.Conditions.Reset();                                    // six-word block +0x5c5c
	}
	if (NpcStateRetail() == 4 && !ScriptOwnerIsLive())
	{
		SetState(1);                                                     // 1026e340 IDLE
		WriteIdealStateRetail(1);
		UE_LOG(LogElysiumNpcEnt, Log,
			TEXT("Scripted Sequence stripped on level transition"));     // 1027bexx
	}
}

bool FElysiumNpcBase::RefindPostRestorePath()
{
	// SEAM for `0x102ee1e0`. No navigator: answers failure.
	++PostRestorePathRefinds;
	return false;
}

void FElysiumNpcBase::InstallScheduleRetail(int32 RawId, bool bForce)
{
	++SetScheduleRetailCalls;
	LastSetScheduleRetail = RawId;
	bLastSetScheduleForce = bForce;
	const int32 Stamp = ResolveIdealScheduleStamp(RawId);                // 10280de0 first half
	LastIdealScheduleStamp = Stamp;
	const int32 Mapped = Stamp;
	if (Mapped != ElysiumScheduleId::None)
	{
		ChangeSchedule(Mapped);
		return;
	}
	// Story 25 miss arm: registry miss installs `IDLE_STAND` untranslated.
	ElysiumSchedule::Start(Schedule, ElysiumSched::IDLE_STAND, *this);
}

void FElysiumNpcBase::NPCInit()
{
	bNpcTransparent = true;                                              // 1027339x SetNPCTransparent(1)
	BaseMemory.LastDamageAttacker = FElysiumEntityHandle::Invalid();  // m_hLastDamageEnt = -1
	Cognition.bCondTookDamage = false;                                   // 102733xx
	++BaseInitAnimatingResets;                                           // ClearAllClientRagdolling
	Flags |= NpcInitAddFlags;                                            // AddFlag(0x12000)
	Gravity = 1.f;                                                       // m_flGravity
	TakeDamageMode = 2;                                                  // m_takedamage
	// `thunk_FUN_102e0b40(m_pMotor)` — `*(motor+0x2c) = -1.0`, the yaw-speed hold. SEAM: the same
	// one families Conditions and Hints already stand for this call; no motor word carries it.
	// Then yaw from `GetAbsAngles()[1]`, `±180` when the motor's animation-movement byte is set, and
	// `motor+0x1c == 180.0` writes `motor+0x34` (`m_IdealYaw`) directly while anything else goes
	// through `0x102e0a80`. The word is the MOTOR's ideal yaw, not the Troika's `+0x63ec`, which
	// this body leaves alone (story 5 step 5 correction). SEAM: no motor `+0x1c`; `MotorIdealYaw`
	// takes the direct write.
	MotorIdealYaw = static_cast<float>(Angles.Y);
	if (BaseScheduleHost.bMotorAnimationMovement)
	{
		if (MotorIdealYaw < MotorYawHalfTurn)
		{
			MotorIdealYaw += MotorYawHalfTurn;
		}
		else
		{
			MotorIdealYaw -= MotorYawHalfTurn;
		}
	}
	MaxHealth = 100;                                                     // 102733xx
	LifeState = 0;                                          // 0x10273459 m_lifeState = LIFE_ALIVE
	bDead = false;                                                       // (the port's removal flag, cleared with it)
	SetDeathReportedForRestore(false);
	// +0x1b3c/+0x1b40 provenance ABSENT (shape map). Line 0x1af1 is not stored.
	WriteIdealStateRetail(1);                                            // m_IdealNPCState = IDLE
	const int32 Sequence = SelectHeaviestSequence(1, INDEX_NONE);
	if (Sequence < 0)
	{
		const int32 Fallback = SelectHeaviestSequence(0xf1, INDEX_NONE);
		if (Fallback < 0)
		{
			// m_nSequence = 0. No sequence member on the kernel surface; ActivityNumber stays.
		}
		else
		{
			SetIdealActivity(0xf1);                                      // 10272650
		}
	}
	else
	{
		SetIdealActivity(1);
	}
	BaseScheduleHost.bShouldMove = false;                                    // +0x1a40
	CollisionMask = NpcInitCollisionMask;                                // +0x1a44
	// `102734cb`: `*(m_pNavigator + 0x2c) = DAT_1093407c` — the process-wide node network the map's
	// `.ain` loaded (`0x102f65b0` / `0x102f6690` are its only writers). SEAM: no navigator and no
	// node network stand here, so there is nothing to point at; `TroikaOnRestore`'s ped-link arm
	// states the same absence from the reading side.
	ClearSchedule();                                                     // 10280d30 — every class
	if (Motor != nullptr)
	{
		Motor->ClearNavigationGoal();                                    // 102ee270
	}
	++NavigationGoalClears;
	++BaseInitAnimatingResets;                                           // 10095be0 + 1008f540
	BaseScheduleHost.HintNode = INDEX_NONE;                                  // m_pHintNode = 0
	BaseScheduleHost.MemoryBits = 0;                                         // m_afMemory
	ElysiumNpcEnemy::SetEnemy(*this, FElysiumEntityHandle::Invalid());
	DistTooFar = BaseInitDistTooFar;
	SetDistLook(BaseInitDistLookUnits * ElysiumMove::U);                 // 1026a2a0(3072)
	bKeepSound = false;
	if ((static_cast<uint32>(SpawnFlags) & SpawnFlagFarSight) != 0)
	{
		DistTooFar = FarSightDistTooFar;
		SetDistLook(FarSightDistLookUnits * ElysiumMove::U);
	}
	Cognition.Conditions.Reset();                                        // six words +0x5c5c
	Cognition.DelayedConditions.Reset();                                 // 102cc7e0 +0x1a9c
	++DelayedConditionListClears;
	PendingSounds.Reset();                                               // 102cc7e0 +0x1ae0
	++DelayedConditionListClears;
	SetDefaultEyeOffset();                                               // 10274ca0
	++BaseInitTailCalls;
	// `1027359x`: `m_pfnUse = &LAB_10004da9`, a retail member-function pointer. This runtime
	// dispatches `Use` through the entity's own virtual, so there is no pointer word to write.
	const double Now = NpcKernelLifecycle19Shared::Lifecycle19Now(*this);
	if (Now <= MapFirstSecond)
	{
		ThinkSet(NpcInitThinkFunction(), 0.0);                           // 10273aa0
		ArmThinkAt(Now + NpcInitThinkDelay);
	}
	else
	{
		++NpcInitInlineThinkCalls;                                       // inline 10273aa0
	}
	Mind.ClearForceStateChange();                                        // +0x1b28
	Unknown5b58 = 0;                                                     // +0x5b58
	SetFrenziedWord(0);                                         // +0x5b84
	NpcInitTime = Now;                                                   // +0x5b5c
	WeaponBlockedByFriendTimer = 0.0;
	ExtendedBlockedByFriendTimer = static_cast<double>(NpcKernelLifecycle19Shared::GFltMax);
	Mind.StampLastStateChangeTime(0.0);                                  // +0x5cc8
	BaseMemory.EnemyOccludedCheck = 10;
	ShootTargetOverride = FElysiumEntityHandle::Invalid();
	// +0x5b90 m_pSurfaceData ABSENT.
	bCineScriptHidden = false;                                           // 102735xx +0x5d78
	bInChoreoScene = false;
	UpdateEnemyWentOccluded(nullptr, false);                             // 10270180(NULL, 0)
	++BaseInitChoreoClears;
	BaseMemory.RepeatedDamageWindowStart = 0.0;                          // 0x10273628 +0x5d98 m_flLastDamageTime
	LastAttackTime = 0.0;
	BaseMemory.SoundWaitTime = 0.0;
	NextEyeLookTime = 0.f;                                               // +0x5d6c
	NextWeaponSearchTime = 0.0;
	BaseScheduleHost.WaitFinished = 0.0;
}

void FElysiumNpcBase::StartNPC()
{
	Schedule.bDidMaintainSchedule = false;                               // +0x5bb8
	BaseScheduleHost.bRanAi = false;                                         // +0x1b4c
	const int32 MoveType = GetMoveType();
	const uint32 Caps = static_cast<uint32>(CapabilitiesGet());
	const bool bSkipDrop = MoveType == 5 || MoveType == 6
		|| (Caps & CapabilityNoFloorDrop) != 0
		|| (static_cast<uint32>(SpawnFlags) & SpawnFlagNoFloorDrop) != 0;
	if (!bSkipDrop)
	{
		FVector OriginUnits = Origin / ElysiumMove::U;
		const bool bHit = MoveProbeFloorDrop(OriginUnits);               // 102e7880
		if (!bHit)
		{
			++FloorDropWarnings;
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("NPC %s stuck in wall--level design error"),      // 10273b78 105cc558
				Def != nullptr ? *Def->Classname : TEXT(""));
		}
		Origin = OriginUnits * ElysiumMove::U;                           // 10273b8b slot 62
		++FloorDropPerformed;
	}
	else
	{
		Flags &= ~NpcKernelLifecycle19Shared::GFlOnGround;                                           // 10273b97 RemoveFlag(1)
		++FloorDropSkipped;
	}
	if (!Target.IsEmpty())
	{
		FElysiumEntity* Found = World != nullptr ? World->FindByName(Target) : nullptr;
		BaseScheduleHost.GoalEnt = Found != nullptr ? Found->Handle : FElysiumEntityHandle();
		if (Found == nullptr)
		{
			// `10273bd9`: THREE pushes — the classname AND the target name.
			UE_LOG(LogElysiumNpcEnt, Warning,
				TEXT("ReadyNPC()--%s couldn't find target %s"),        // 105cc528
				Def != nullptr ? *Def->Classname : TEXT(""), *Target);
		}
		else
		{
			SetState(1);                                                 // 1026e340 IDLE
			InstallScheduleRetail(StartNpcGoalEntityRetailId, false);    // 10280de0(3)
		}
	}
	InitSquad();                                                         // slot 545
	const double Now = NpcKernelLifecycle19Shared::Lifecycle19Now(*this);
	if (Now <= MapFirstSecond)
	{
		ThinkSet(StartNpcThinkFunction(), 0.0);
		const float Jitter = ElysiumRng::Stream(EElysiumRngStream::NpcSchedule)
			.FRandRange(StartNpcDelayMin, StartNpcDelayMax);
		ArmThinkAt(Now + static_cast<double>(Jitter));
	}
	else
	{
		ThinkSet(StartNpcThinkFunction(), 0.0);                          // BOTH arms ThinkSet
		ArmThinkAt(Now);
	}
	ScriptArrivalActivity = static_cast<int32>(0xffffffff);
	ScriptArrivalSequence.Reset();
	if ((static_cast<uint32>(SpawnFlags) & SpawnFlagPreAimed) != 0)
	{
		SetState(1);
		SetActivity(1);                                                  // slot 310
		InstallScheduleRetail(StartNpcAmbushRetailId, false);            // 0x2d
	}
}

void FElysiumNpcBase::OnRestore(bool /*bFromLoad*/)
{
	bool bGiveUp = true;
	if ((NpcStateRetail() != 4 || ScriptOwnerIsLive())
		&& !LastSavedExtendedHeader.ScheduleName.IsEmpty()
		&& LastSavedExtendedHeader.Version == 1)
	{
		bool bOk = true;
		if ((LastSavedExtendedHeader.Flags & 1u) != 0 && GetEnemy() == nullptr)
		{
			bOk = false;
		}
		if (bOk && (LastSavedExtendedHeader.Flags & 2u) != 0)
		{
			if (NpcKernelLifecycle19Shared::Lifecycle19ResolveHandle(*this, TargetEnt) == nullptr)
			{
				bOk = false;
			}
		}
		if (bOk)
		{
			bGiveUp = false;
		}
	}
	if (Schedule.TaskIndex > RestoreTaskIndexCeiling)
	{
		Schedule.TaskIndex = 1;
	}
	if (!bGiveUp)
	{
		// `CAI_ScheduleManager::FindByName` (`0x1030f350`), retail's own body, over the loaded
		// corpus rather than over a registry of programs typed here.
		const FElysiumScheduleProgram* Found = FElysiumScheduleCorpus::Get().Manager().FindByName(
			LastSavedExtendedHeader.ScheduleName);
		Schedule.Current = Found != nullptr ? Found->GlobalId    // 1027c020 m_pSchedule
			: ElysiumScheduleId::None;
		if (Schedule.Current != ElysiumScheduleId::None)                // 1027c026
		{
			// `1027c02d`/`1027c048`/`1027c052`: CRC32 over the resolved schedule's task array —
			// `schedule+0x20` for `schedule+0x24 << 3` bytes, eight per task — compared against the
			// checksum the save wrote at `+0x1a3c`. A schedule whose task list has CHANGED since
			// the save is dropped (`1027c068`), which then takes the give-up arm below. This is the
			// same checksum `SaveWriteFields` already computes through `ScheduleTaskBytes`.
			TArray<uint8> TaskBytes;
			ScheduleTaskBytes(TaskBytes);
			uint32 Crc = SaveCrc32Init();
			Crc = SaveCrc32Update(Crc, TaskBytes.GetData(), TaskBytes.Num());
			if (SaveCrc32Final(Crc) != LastSavedExtendedHeader.ScheduleCrc)   // 1027c064
			{
				Schedule.Current = ElysiumScheduleId::None;             // 1027c068
			}
		}
	}
	if (Schedule.Current == ElysiumScheduleId::None || bGiveUp)
	{
		BaseScheduleHost.bDoPostRestoreRefindPath = false;
		RestoreGiveUp();
	}
	else
	{
		BaseScheduleHost.bDoPostRestoreRefindPath =
			((LastSavedExtendedHeader.Flags >> 2) & 1u) != 0;
	}
	// CBaseCombatCharacter::OnRestore `0x10323b60` — SEAM, no port body.
	// +0x5b90 m_pSurfaceData ABSENT.
	if (!BaseScheduleHost.bDoPostRestoreRefindPath)
	{
		if (Motor != nullptr)
		{
			Motor->ClearNavigationGoal();
		}
		++NavigationGoalClears;
	}
	else if (!RefindPostRestorePath())
	{
		RestoreGiveUp();
	}
}
