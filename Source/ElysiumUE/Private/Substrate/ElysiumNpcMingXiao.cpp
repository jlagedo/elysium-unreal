#include "Substrate/ElysiumNpcMingXiao.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumAnimEvent.h"
#include "ElysiumEntityWorld.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "ElysiumDecalSubsystem.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumOverlayStack.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumSaveArchive.h"
#include "ElysiumSchedule.h"
#include "ElysiumSkeletalBasis.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Player/ElysiumCameraShots.h"
#include "Substrate/ElysiumDamage.h"
#include "Substrate/ElysiumGameSound.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemyMemory.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcAnim10_2Shared.h"
#include "Substrate/ElysiumNpcCombat10Shared.h"
#include "Substrate/ElysiumNpcCombat10_2Shared.h"
#include "Substrate/ElysiumNpcConditions10Shared.h"
#include "Substrate/ElysiumNpcDamage2Shared.h"
#include "Substrate/ElysiumNpcDamageShared.h"
#include "Substrate/ElysiumNpcDebug10_2Shared.h"
#include "Substrate/ElysiumNpcMotorShared.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumReactions.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumSceneData.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	constexpr TCHAR GAnim10_2FileMingXiao[] = TEXT("NPC_VMingXiao.cpp");  // 0x10647090
	constexpr float Bosses2Zero = ElysiumNpcTunables::Zero;
	constexpr float Bosses2One = ElysiumNpcTunables::One;
	// `0x10398030`'s distance bands against `m_flClosestPlayerDistance` (+0x6264), SOURCE units.
	constexpr float MingXiaoNearBand = ElysiumNpcTunables::Hundred;
	constexpr float MingXiaoFarBand300 = 300.0f;      // _DAT_10462b84
	constexpr float MingXiaoFarBand200 = 200.0f;      // _DAT_104492b8
	constexpr float MingXiaoReachBand = 150.0f;       // _DAT_10457f60
	// `0x10398030`'s tentacle schedule ids, slots 0..3.
	constexpr int32 MingXiaoTentacleSchedules[4] = { 0x112a, 0x112b, 0x112c, 0x112d };
	// `0x10396dc0`'s two conditions and the two schedules they answer.
	constexpr int32 MingXiaoCondGrabA = 0x7b;
	constexpr int32 MingXiaoCondGrabB = 0x7c;
	constexpr int32 MingXiaoSchedGrabA = 0x15c;
	constexpr int32 MingXiaoSchedGrabB = 0x15d;
	// `0x103983d0`'s out-of-range answer — the same shared 20.0 cell as the pickup cone's upper edge.
	constexpr float MingXiaoThrowDefault = 20.0f;     // _DAT_1044eb0c
	// `0x103989b0`'s abeam test.
	constexpr float PedestalHeightTolerance = static_cast<float>(ElysiumNpcTunables::SixtyFourDouble);
	constexpr float PedestalForwardLo = -0.17f;       // _DAT_104bde64
	constexpr float PedestalForwardHi = ElysiumNpcTunables::Half;
	// `0x10398b20`'s search radius, its stationary tolerance and its name prefix.
	constexpr float PedestalSearchRadius = 257.0f;
	constexpr float PedestalStationaryTolerance = 0.1f;   // _DAT_104491b4
	constexpr const TCHAR* PedestalNamePrefix = TEXT("Pedestal");
	constexpr int32 PedestalNamePrefixLength = 8;         // retail's `__strnicmp(..., 8)`
	// `VectorNormalize` `0x10137220`: `1.0 / (FLT_EPSILON + length)`, so a zero vector normalizes to
	// zero rather than to NaN and a unit vector comes back a hair short. Both are observable, and the
	// first is what makes `PedestalTaskForSide` answer for a candidate standing on top of the boss.
	constexpr float Bosses2NormalizeEpsilon = ElysiumNpcTunables::FloatEpsilon;
	FVector Bosses2Normalize(const FVector& V)
	{
		const float Scale = Bosses2One / (Bosses2NormalizeEpsilon + static_cast<float>(V.Size()));
		return V * Scale;
	}
	const TCHAR* const GMingXiaoFile = TEXT("NPC_VMingXiao.cpp");
	// `m_eThrowableObjectMode` values 3 and 4, the MingXiao arm's motor case (`103940a5`), and the
	// 180.0 the motor's steering is reset to (`0x43340000`).
	constexpr int32 GCond10ThrowModeMotorA = 3;
	constexpr int32 GCond10ThrowModeMotorB = 4;
	constexpr float GCond10MingXiaoSteeringYaw = 180.f;
	constexpr float ThrowLeadZScale = ElysiumNpcTunables::Half;
	constexpr float ThrowConeLo = -20.0f;           // _DAT_1049ae98
	constexpr float ThrowConeHi = 20.0f;            // _DAT_1044eb0c
	constexpr float ThrowSpeedFloor = 1000.0f;      // _DAT_10447ee0
	// `0x103937d0`'s TaskFail code when there is no active weapon.
	constexpr int32 MingXiaoNoWeaponFailure = 0x1f;
	// `0x10396bc0`'s schedule id and its selector-trace line.
	constexpr int32 MingXiaoGrabSchedule = 0x167;
	constexpr int32 MingXiaoGrabTraceLine = 0xbc3;
	// `CNPC_VMingXiao::TestHitboxes`'s hitbox-set requirement.
	constexpr int32 MingXiaoHitboxSetsRequired = 7;
	// `UTIL_AngleDiff` `0x1013d580`: `a - b` walked back into `[-180, 180]` by whole turns, wrapping
	// only on the side the `a <= b` test selects. Families Bosses, Facing and Positions each keep an
	// identical private copy for the same reason: none owns the other's file.
	float Damage2AngleDiff(float A, float B)
	{
		float Delta = A - B;
		if (A <= B)
		{
			while (Delta < -180.0f)
			{
				Delta += 360.0f;
			}
		}
		else
		{
			while (Delta > 180.0f)
			{
				Delta -= 360.0f;
			}
		}
		return Delta;
	}
	// `UTIL_VecToYaw` in SOURCE's frame, over a delta expressed in THIS world's axes (whose Y is the
	// negated Source one), exactly as families Bosses and Facing take it.
	float Damage2VecToYaw(const FVector& Delta)
	{
		if (FMath::IsNearlyZero(Delta.X) && FMath::IsNearlyZero(Delta.Y))
		{
			return 0.f;
		}
		return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(-Delta.Y, Delta.X)));
	}
	constexpr float GDebug10_2MingXiaoRingHeightUnits = 8.f;// 0x41000000
	// The four MingXiao ring radii, in SOURCE units, in the order `0x10399d40` draws them. These are
	// the reason the body is worth porting at all: they are the recovered range bands.
	constexpr float GDebug10_2MingXiaoRadiiUnits[] = { 100.f, 150.f, 200.f, 300.f };
	// `thunk_FUN_101e8da0(0x10739d08)` — `CNPC_VMingXiao`'s own playback-tuning record, read by
	// field offset. **SEAM**: this substrate holds no such table, so every field answers 0 and the
	// non-discipline arm's blend lands on its floor.
	float FacingMingXiaoTuningField(int32)
	{
		return 0.f;
	}
	// `_DAT_1046dcd0` = 128.0f SOURCE units — the range gate on `CoordinateTroops`' severed-tentacle
	// scatter. Compared against the LENGTH `VectorNormalize` answers, inclusively (`AND EAX,0x4100`
	// keeps both the below and the equal flags).
	constexpr float GScatterRangeUnits = 128.0f;
	// `_DAT_10449260`, a **DOUBLE** (`1039997d  FCOMP double ptr [0x10449260]`) = 0.25. The 2-D dot
	// floor on the same gate: a 75.5-degree half-angle in front of `m_vecForward`. Read as a float
	// the cell is 0.0 and the gate becomes the whole forward half-plane.
	constexpr double GScatterForwardDotFloor = 0.25;
	// The two forced-schedule ids `0x103998d0` refuses to scatter a tentacle out of. What each one
	// IS is not a fact of this family's rows; they are carried by number, as retail compares them.
	constexpr int32 GScatterRefusedScheduleA = 0x163;
	constexpr int32 GScatterRefusedScheduleB = 0x165;
	// `m_ePhase` (+0x6670) must read exactly this before a severed tentacle will scatter.
	constexpr int32 GScatterRequiredPhase = 2;
	// `m_rhSeveredTentacles` is a fixed SIX-entry array in retail and both scatter bodies walk all
	// six unconditionally (`iVar4 = 6; do { … } while (--iVar4)`).
	constexpr int32 GSeveredTentacleCount = 6;
	// `thunk_FUN_101e8da0(0x10739d08)` — `CNPC_VMingXiao`'s playback/turn tuning record, read by
	// field offset. **SEAM**: this substrate holds no such table, so every field answers 0. The
	// Facing family records the same gap for the same record; the two are deliberately separate
	// file-local helpers rather than one shared member, because neither family owns the other's file.
	float MingXiaoTuningField(int32)
	{
		return 0.f;
	}
	const TCHAR* const GMingXiaoEmitters[] = {
		TEXT("Ming_xiao_slimetrail_emitter"),
		TEXT("Ming_xiao_slimetrail_emitter2"),
		TEXT("Ming_xiao_tentacle_damage_emitter"),
		TEXT("Ming_xiao_tentacle_burst_emitter"),
		TEXT("Ming_xiao_death_emitter"),
		TEXT("Ming_xiao_death_emitter2"),
		TEXT("Ming_xiao_death_proxy_emitter"),
		TEXT("Ming_xiao_death_proxy_emitter2"),
		TEXT("Ming_xiao_vomit_emitter"),
		TEXT("Ming_xiao_transform_emitter"),
		TEXT("Ming_xiao_transform_emitter2"),
	};
	// `PTR_..._1064339c`, one entry: the directory name carries a SPACE, not an underscore.
	const TCHAR* const GMingXiaoMoveSound = TEXT("character/monster/ming xiao/movement.wav");
	const TCHAR* const GMingXiaoWeapons[] = {
		TEXT("item_w_mingxiao_melee"),
		TEXT("item_w_mingxiao_tentacle"),
		TEXT("item_w_mingxiao_spit"),
	};
	// `_DAT_1044eb0c` = **20.0** Source units, Ming Xiao's aim-point Z bonus.
	constexpr float GMingXiaoAimZBonusUnits = 20.0f;
}

const FElysiumNpcClass* FElysiumNpcMingXiao::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 482: `0x10396e90`, the same standalone copy as the human line's.
int32 FElysiumNpcMingXiao::CanPlaySequence(bool bDisregardState, int32 InterruptLevel)
{
	return CanPlaySequenceSpecies(bDisregardState, InterruptLevel);
}

// Slot 104: `0x10392660`.
// 0x10392660
void FElysiumNpcMingXiao::Precache()
{
	// `CNPC_VMingXiao::Precache` `0x10392660` — the Troika body, ELEVEN emitters all with preload
	// **0**, one move sound, three weapons.
	TroikaPrecache();
	for (const TCHAR* Emitter : GMingXiaoEmitters)
	{
		NpcKernelPrecache10Shared::Precache10Particle(*this, Emitter, /*Preload=*/0);
	}
	NpcKernelPrecache10Shared::Precache10Sound(*this, GMingXiaoMoveSound);
	for (const TCHAR* Weapon : GMingXiaoWeapons)
	{
		NpcKernelPrecache10Shared::Precache10Other(*this, Weapon);
	}
}

// Slot 126: `0x10395f80`.
/** `CNPC_VMingXiao::Save` (`0x10395f80`) — the six `m_rflRegrowTimers` (`+0x66f4`) encoded at mode
 *  **4** ascending, the Troika body, then the same six decoded ascending. */
int32 FElysiumNpcMingXiao::Save(void* Archive)
{
	// `CNPC_VMingXiao::Save` `0x10395f80`. `pfVar2 = m_rflRegrowTimers; iVar1 = 6; do { encode(p, 4);
	// ++p; } while (--iVar1);` — ascending, mode 4, then the Troika body, then the identical
	// descending-count/ascending-pointer decode loop over the SAME six.
	//
	// Mode 4 is the fact: an exactly-`FLT_MAX` regrow timer is a tentacle that will never regrow,
	// and without the sentinel retail's `FIELD_TIME` rebase on load would shift it.
	for (int32 Index = 0; Index < MingXiaoRegrowTimerCount; ++Index)
	{
		SaveStampEncode(MingXiaoRegrowTimers[Index], ESaveStampMode::FloatMax);
	}
	const int32 Result = TroikaSave(Archive);   // the Troika body, directly
	for (int32 Index = 0; Index < MingXiaoRegrowTimerCount; ++Index)
	{
		SaveStampDecode(MingXiaoRegrowTimers[Index], ESaveStampMode::FloatMax);
	}
	return Result;
}

// Slot 127: `0x10396000`.
/** `CNPC_VMingXiao::vfunc127` (`0x10396000`) — the Troika body, then the six `m_rflRegrowTimers`
 *  decoded at mode **4** ascending. */
int32 FElysiumNpcMingXiao::Restore(void* Archive)
{
	// `CNPC_VMingXiao::vfunc127` `0x10396000` — the base FIRST, then the six regrow timers decoded
	// ascending at mode 4. The decode twin of `0x10395f80`.
	const int32 Result = TroikaRestore(Archive);
	for (int32 Index = 0; Index < MingXiaoRegrowTimerCount; ++Index)
	{
		SaveStampDecode(MingXiaoRegrowTimers[Index], ESaveStampMode::FloatMax);
	}
	return Result;
}

// Slot 180: `0x10391230`, which ends in `TroikaUpdateOnRemove`.
/** `CNPC_VMingXiao::UpdateOnRemove` (`0x10391230`) — when `m_eThrowableObjectMode` (`+0x673c`) is
 *  non-zero, drop the carried throwable through family Damage's `MingXiaoThrowCleanup`
 *  (`0x10398fd0`), then the Troika body either way. Without the drop the thrown prop outlives the
 *  boss. */
void FElysiumNpcMingXiao::UpdateOnRemove()
{
	// `CNPC_VMingXiao::vfunc180` `0x10391230` — `if (m_eThrowableObjectMode +0x673c)
	// thunk_FUN_10398fd0(this);` then the Troika body ALWAYS. Without the drop the thrown prop
	// outlives the boss.
	if (MingXiaoThrowableObjectMode != 0)
	{
		MingXiaoThrowCleanup();   // 0x10398fd0, family Damage's
	}
	TroikaUpdateOnRemove();
}

// Slot 461: `0x103945a0`, a complete replacement: `GetEnemy() ? COMBAT : IDLE`.
int32 FElysiumNpcMingXiao::SelectIdealStateRetail()
{
	// `vfunc461` writes `m_IdealNPCState = 7` when `!(IsAlive() && m_NPCState != 7)` and then ALWAYS
	// overwrites it from the enemy test — the dead write is unreachable in the same call, which is
	// retail's own dead code and is reproduced by leaving the branch here with nothing it can keep.
	const bool bDeadWrite = !(!IsInert() && Mind.State() != EElysiumNpcState::Dead);
	(void)bDeadWrite;
	// `GetEnemy() ? 2 : 1`: COMBAT or IDLE.
	Mind.WriteIdealStateRetail(BaseMemory.Enemy.IsSet() ? 2 : 1);
	return IdealStateRetail();
}

// Slot 574: `0x10395d00`
/** `CNPC_VMingXiao::GetShootEnemyDir` (`0x10395d00`), slot 574's one species arm: the base body with
 *  `_DAT_1044eb0c` (**20.0** Source units) added to the aim point's Z before the subtraction. */
FVector FElysiumNpcMingXiao::GetShootEnemyDir(const FVector& ShootPositionCm, int32 A, int32 B)
{
	// `10395d1c`: identical to the base `0x10278900` except that the aim point's Z is raised by
	// `_DAT_1044eb0c` (**20.0** Source units) BEFORE the caller's shoot position is subtracted.
	FVector AimPointCm = ShootEnemyAimPoint(ShootPositionCm);
	AimPointCm.Z += static_cast<double>(GMingXiaoAimZBonusUnits) * ElysiumMove::U;
	// The delta is normalised in place through `0x1057966c` and the UNIT direction stored out, which
	// the decompiled C hides for the same stack-shift reason as the base body.
	return (AimPointCm - ShootPositionCm).GetSafeNormal();
}

// Slot 604: `0x10396050`, which replaces the Troika body wholesale; its argument is read by no arm.
/** `CNPC_VMingXiao::SelectScheduleMeleeCombat` (`0x10396050`), 1,522 bytes. The same skeleton as the
 *  human's with FOUR stated differences: the distance is tested BEFORE the roll; the common tail
 *  offers only `0x102b7370`; an extra `COND 0x48 ENEMY_OCCLUDED` arm opens it; and there is no
 *  `COND 0x0d SHOULD_BLOCK` arm at all. */
int32 FElysiumNpcMingXiao::SelectScheduleMeleeCombat(int32 Unused)
{
	(void)Unused;
	// `CNPC_VMingXiao::SelectScheduleMeleeCombat` `0x10396050`, 1,522 bytes. The same skeleton as
	// `0x10385e40` with FOUR stated differences, each marked below.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	const float Range = MeleeRangeUnits();
	const float Distance = ScheduleHost.EnemyDistUnits;
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	if (!bInMelee)
	{
		// **DIFFERENCE 1**: MingXiao re-fetches `GetEnemy()` at every use rather than caching it, and
		// its not-engaged arm does NOT offer the melee failure gate `0x102b6fe0` at all.
		if (!Slot599(0))
		{
			// **DIFFERENCE 2**: the distance is tested BEFORE the roll, where the human body offers
			// the gate first. `10396095` is the same `range + 200 < distance` strict compare.
			if (Range + NpcKernelAnim10_2Shared::GAnim10_2FarMargin < Distance)
			{
				RecordScheduleEvent(
					FString::Printf(TEXT("%s:%d -> 0xe7"), GAnim10_2FileMingXiao, 2644));
				return 0xe7;
			}
			if (NpcKernelAnim10_2Shared::Anim10_2Rng().RandRange(0, 99) > NpcKernelAnim10_2Shared::GAnim10_2RollFloor && Range <= Distance)
			{
				RecordScheduleEvent(
					FString::Printf(TEXT("%s:%d -> 0xe5"), GAnim10_2FileMingXiao, 2656));
				return 0xe5;
			}
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe4"), GAnim10_2FileMingXiao, 2652));
			return 0xe4;
		}
	}
	else if (Slot602())
	{
		Slot601(GetEnemy());
		if (NpcKernelAnim10_2Shared::Anim10_2HasRangedWeapon(*this))
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe9"), GAnim10_2FileMingXiao, 2623));
			return 0xe9;
		}
		if (Range + Range <= Distance)
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe7"), GAnim10_2FileMingXiao, 2629));
			return 0xe7;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe4"), GAnim10_2FileMingXiao, 2633));
		return 0xe4;
	}

	// **DIFFERENCE 3**: the common tail offers ONLY `0x102b7370`; there is no second
	// `0x102b6fe0` offer.
	// `thunk_FUN_102b7370(this)` — `SelectDoorObstructionSchedule`, which `FElysiumNpc` already
	// carries (`ElysiumNpc.cpp`) as an `int32`; converted back to retail's number
	// because retail's `if (answer != 0) return answer` is over the raw one.
	const int32 DoorOffer =
		SelectDoorObstructionSchedule();
	if (DoorOffer != 0)
	{
		return DoorOffer;
	}

	// **DIFFERENCE 4a**: an extra arm the human body lacks, and it OPENS the ladder — `COND 0x48
	// ENEMY_OCCLUDED`, read through `HasCondition` and not through the interrupt form.
	if (Conds.Has(EElysiumNpcCond::EnemyOccluded))
	{
		if (NpcKernelAnim10_2Shared::Anim10_2HasRangedWeapon(*this))
		{
			Slot601(GetEnemy());
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe9"), GAnim10_2FileMingXiao, 2675));
			return 0xe9;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xcd"), GAnim10_2FileMingXiao, 2685));
		return 0xcd;
	}
	if (ElysiumSchedule::HasInterruptCondition(Schedule, *this, Conds,
			EElysiumNpcCond::ShouldDodge))
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd5"), GAnim10_2FileMingXiao, 2692));
		return 0xd5;
	}
	// **DIFFERENCE 4b**: there is NO `COND 0x0d SHOULD_BLOCK` arm at all.
	const bool bKick = Conds.Has(EElysiumNpcCond::ShouldKick);
	const bool bStepback = Conds.Has(EElysiumNpcCond::ShouldStepback);
	if (bKick)
	{
		if (!bStepback || NpcKernelAnim10_2Shared::Anim10_2Rng().RandRange(0, 1) == 1)
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xdb"), GAnim10_2FileMingXiao, 2702));
			return 0xdb;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd3"), GAnim10_2FileMingXiao, 2706));
		return 0xd3;
	}
	if (bStepback)
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd3"), GAnim10_2FileMingXiao, 2706));
		return 0xd3;
	}
	if (Conds.Has(EElysiumNpcCond::CanMeleeAttack1))
	{
		if (NpcKernelAnim10_2Shared::Anim10_2HasRangedWeapon(*this))
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xdc"), GAnim10_2FileMingXiao, 2716));
			return 0xdc;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xdd"), GAnim10_2FileMingXiao, 2720));
		return 0xdd;
	}

	const bool bRetryExpired = NpcKernelAnim10_2Shared::Anim10_2TickHeightDiffTimer(*this, Now);
	if (NpcKernelAnim10_2Shared::Anim10_2HasRangedWeapon(*this)
		&& (Conds.Has(EElysiumNpcCond::TooFarForMelee) || Conds.Has(EElysiumNpcCond::InterruptTime)
			|| Conds.Has(EElysiumNpcCond::EnemyUnreachable) || bRetryExpired))
	{
		Slot601(GetEnemy());
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe9"), GAnim10_2FileMingXiao, 2754));
		return 0xe9;
	}
	if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
	{
		Slot601(GetEnemy());
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0x17"), GAnim10_2FileMingXiao, 2762));
		return 0x17;
	}
	if (!Conds.Has(EElysiumNpcCond::TooFarForMelee) && !Conds.Has(EElysiumNpcCond::TooFarToAttack))
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 199"), GAnim10_2FileMingXiao, 2820));
		return 199;
	}
	FElysiumEntity* Enemy = GetEnemy();
	if (Enemy != nullptr)
	{
		(void)ElysiumCameraShots::SurroundingBounds(*Enemy).GetCenter();   // slot 192
		if (ScheduleMeleeReachGate())
		{
			if (NpcKernelAnim10_2Shared::Anim10_2HasRangedWeapon(*this))
			{
				RecordScheduleEvent(
					FString::Printf(TEXT("%s:%d -> 0xe0"), GAnim10_2FileMingXiao, 2779));
				return 0xe0;
			}
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xe1"), GAnim10_2FileMingXiao, 2783));
			return 0xe1;
		}
	}
	FScheduleHintSearchRequest CoverRequest;
	CoverRequest.bRequest2 = true;
	CoverRequest.bRequest4 = true;   // `thunk_FUN_102b7690(this, 0, 1, 0, 1)`
	const int32 CoverOffer = SelectCoverOrKickSchedule(CoverRequest);
	if (CoverOffer != 0)
	{
		return CoverOffer;
	}
	if (Distance < Range && !bRetryExpired)
	{
		if (NpcKernelAnim10_2Shared::Anim10_2HasRangedWeapon(*this))
		{
			RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd1"), GAnim10_2FileMingXiao, 2810));
			return 0xd1;
		}
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xd2"), GAnim10_2FileMingXiao, 2814));
		return 0xd2;
	}
	if (NpcKernelAnim10_2Shared::Anim10_2HasRangedWeapon(*this))
	{
		RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xca"), GAnim10_2FileMingXiao, 2799));
		return 0xca;
	}
	RecordScheduleEvent(FString::Printf(TEXT("%s:%d -> 0xcb"), GAnim10_2FileMingXiao, 2803));
	return 0xcb;
}

// Slot 418: `0x10392a10`, a species sentinel ahead of a direct call into the Troika body.
/** The species bodies of slot 604 `SelectScheduleMeleeCombat` that live in this family, each the body
 *  of its class's override (story 5 step 3): the Chang brothers' `0x1036d800` and the Tzimisce
 *  runner's `0x103c4430` (one shape, two id sets), AsianVampire's `0x10361be0`, SheriffMan's
 *  `0x103af960` and SabbatLeader's `0x103aa060`. */
/** The two species bodies of slot 418 `ResolveTaskDistance`, each its class's override's body: a
 *  species sentinel, else a direct call into the Troika body. */
// `0x10392a10`
// `CNPC_VMingXiao::ResolveTaskDistance` `0x10392a10`: `if ((int)param != -1000004) return
// base(param); else return m_flIdealRange (+0x6748);`. The base is a direct call into the Troika
// body `0x102bf6e0`.
float FElysiumNpcMingXiao::ResolveTaskDistance(float Distance)
{
	if (static_cast<int32>(Distance) == -1000004)
	{
		// SEAM: `m_flIdealRange` is a SPECIES word above `+0x665c` with no port member and no
		// producer; family Squad declares the four species words its bodies read and this is not
		// one of them.
		ElysiumStub::Fired(TEXT("species"),
			TEXT("CNPC_VMingXiao::ResolveTaskDistance m_flIdealRange +0x6748"), DebugString(),
			TEXT("-1000004"), TEXT("0002/29c-1: no MingXiao ideal range"));
		return 0.0f;
	}
	return FElysiumNpc::ResolveTaskDistance(Distance);
}

// Slot 448: `0x10394090`, its own arm and then a direct call into the Troika body `0x1029adb0`.
void FElysiumNpcMingXiao::TaskFail(int32 Reason)
{
	// `CNPC_VMingXiao::TaskFail` (`0x10394090`), 88 bytes: a switch on `m_eThrowableObjectMode`
	// (`+0x673c`).
	if (MingXiaoThrowableObjectMode == GCond10ThrowModeMotorA
		|| MingXiaoThrowableObjectMode == GCond10ThrowModeMotorB)
	{
		// `103940a5`: `0x102e0a60(m_pMotor, 0x43340000)` — `m_pMotor->+0x1c = 180.0`, the same
		// steering reset the Troika `TaskFail` body itself makes, which is why the port's
		// `ResetSteering()` (already `0x102e0a60`'s body) carries the constant rather than taking it.
		static_assert(GCond10MingXiaoSteeringYaw == 180.f, "0x43340000 is 180.0f");
		if (Motor != nullptr)
		{
			Motor->ResetSteering();
		}
	}
	else
	{
		// **CORRECTION.** `0x10398d90` is `m_eThrowableObjectMode = arg` and nothing else; the
		// checklist's walk calls it "clear the throwable prop". The default arm therefore sets the
		// MODE to 0 (`103940c5`, through thunk `0x1000e45d`) and then releases the handle. The mode is
		// the one word `+0x673c` every other MingXiao body reads (story 5 step 4r: the port had a
		// second carrier here that nothing else wrote, so the motor arm was unreachable).
		ThrowableObjectMode(0);                                          // 0x10398d90(this, 0)
		MingXiaoThrowObject = FElysiumEntityHandle::Invalid();            // m_hThrowObject +0x6718
	}
	FElysiumNpc::TaskFail(Reason);
}

// Slot 348: `0x103970d0`
/** `CNPC_VMingXiao::vfunc348` (`0x103970d0`), slot 348's one species arm: the base formula with a
 *  loop `i = 0..5` over `0x10398000(this, i)` folding one extra contribution in per true result —
 *  the regrown-limb count. Declared here and dispatched from the slot body. */
int32 FElysiumNpcMingXiao::HealthToPercent()
{
	// `0x103970d0`, `CNPC_VMingXiao#348`. The decompiler lost the FPU arithmetic (it shows three bare
	// `__ftol()` calls with no operands); what the body DOES is the base formula with one extra
	// contribution folded in per attached limb, `i = 0..5` over `0x10398000(this, i)`. The two stat
	// reads, their order and the scope-trace string are the base's — retail even reuses
	// `CBaseCombatCharacter::HealthToPercent` as this override's trace name.
	const int32 Cap = TypedStatValue(NpcKernelCombat10Shared::GStatListTypeSheet, NpcKernelCombat10Shared::GStatMaxHealth);
	const int32 Wounds = TypedStatValue(NpcKernelCombat10Shared::GStatListTypeSheet, NpcKernelCombat10Shared::GStatWounds);
	int32 Limbs = 0;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		if (MingXiaoLimbPresent(Index))
		{
			++Limbs;
		}
	}
	if (Cap == 0)
	{
		return 0;   // the same crash guard as the base
	}
	// The limb term LOWERS the reported percent — the regrown-limb count is added to the wounds.
	// With every limb absent (this runtime's seam) the arm equals the base, which is retail's own
	// answer for an intact boss.
	return ((Cap - (Wounds + Limbs)) * MaxHealth) / Cap;
}

// Slot 605: `0x103967d0`
/** `CNPC_VMingXiao::SelectScheduleRangedCombat` (`0x103967d0`), 794 bytes — the human skeleton with
 *  the COND `0x3c` arm dropped, the discipline gate dropped, and the dodge decision INLINED at the
 *  same `0x4b` threshold `0x102b7f40` uses. */
int32 FElysiumNpcMingXiao::SelectScheduleRangedCombat(int32 Arg)
{
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	// The inlined dodge decision — the FIRST arm of `0x102b7f40` only, at the same `0x4b`. Ming Xiao
	// does NOT get the discipline arm or the `COND 0x3c` arm the shared helper carries.
	const auto InlineDodge = [this, &Conds]() -> bool
	{
		return SelectWeightedSequenceForActivity(NpcKernelCombat10_2Shared::GDodgeActivity) != 0
			&& !Conds.Has(EElysiumNpcCond::StopBackup)
			&& NpcKernelCombat10_2Shared::RangedRoll() < NpcKernelCombat10_2Shared::GDodgeRollThreshold;
	};

	// `103967d6`: arm 1.
	if (bInMelee)
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe3"), GMingXiaoFile, 0xb12));
		return 0xe3;
	}
	// `103967f8`: arm 2 — the cover hint, exactly as the human's.
	bool bHasCoverHint = ScheduleHost.ShootAtHintNode != 0;
	if (!bHasCoverHint)
	{
		Slot609(false);
		const int32 FoundHint = FindShootAtHintNode(false);
		ScheduleHost.ShootAtHintNode = FoundHint > 0 ? FoundHint : 0;   // as the human arm folds it
		bHasCoverHint = ScheduleHost.ShootAtHintNode != 0;
	}
	if (bHasCoverHint && !Conds.Has(EElysiumNpcCond::WaitingAttackTime))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xec"), GMingXiaoFile, 0xb27));
		return 0xec;
	}
	// `10396847`: arm 3.
	if (Conds.Has(EElysiumNpcCond::TooCloseForRanged) && NpcKernelCombat10_2Shared::HasUsableMeleeWeaponPort(*this)
		&& NpcKernelCombat10_2Shared::EnemySlot599(*this))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe3"), GMingXiaoFile, 0xb2c));
		return 0xe3;
	}
	// **There is NO `COND 0x3c` arm here** — the human's arm 4 is absent.
	// `1039688d`: the three helpers, same order as the human's.
	if (const int32 PrePass = RangedWeaponPrePass(); PrePass != 0)
	{
		return PrePass;
	}
	const int32 Door = SelectDoorObstructionSchedule();   // 0x102b7370
	if (Door != ElysiumScheduleId::None)
	{
		return Door;
	}
	if (const int32 Reaction = SelectCombatReactionSchedule(); Reaction != 0)
	{
		return Reaction;
	}
	// `103968c3`: the SPLIT — and **there is NO discipline gate**, so the slot-606 branch is entered
	// on the two conditions alone.
	if (!Conds.Has(EElysiumNpcCond::TooCloseToAttack)
		&& !Conds.Has(EElysiumNpcCond::TooCloseForRanged))
	{
		if (const int32 Slot606Answer = Slot606(Arg); Slot606Answer != 0)
		{
			return Slot606Answer;
		}
		if (Conds.Has(EElysiumNpcCond::ExtendedBlockedByFriend))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xbd"), GMingXiaoFile, 0xb6e));
			return 0xbd;
		}
		if (Conds.Has(EElysiumNpcCond::TooFarToAttack))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xb1"), GMingXiaoFile, 0xb73));
			return 0xb1;
		}
		return 0;
	}
	if (!Conds.Has(EElysiumNpcCond::WaitingAttackTime)
		&& !Conds.Has(EElysiumNpcCond::WeaponBlockedByFriend))
	{
		if (InlineDodge())
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xef"), GMingXiaoFile, 0xb49));
			return 0xef;
		}
		if (NpcKernelCombat10_2Shared::HasUsableMeleeWeaponPort(*this) && NpcKernelCombat10_2Shared::RangedRoll() < NpcKernelCombat10_2Shared::GMeleeSwitchRollThreshold
			&& NpcKernelCombat10_2Shared::EnemySlot599(*this))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xe8"), GMingXiaoFile, 0xb4d));
			return 0xe8;
		}
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xf0"), GMingXiaoFile, 0xb51));
		return 0xf0;
	}
	if (InlineDodge())
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xb8"), GMingXiaoFile, 0xb58));
		return 0xb8;
	}
	if (NpcKernelCombat10_2Shared::HasUsableMeleeWeaponPort(*this) && NpcKernelCombat10_2Shared::EnemySlot599(*this))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe8"), GMingXiaoFile, 0xb5c));
		return 0xe8;
	}
	RecordScheduleEvent(FString::Printf(
		TEXT("SelectScheduleRangedCombat %s:%d -> 0xb9"), GMingXiaoFile, 0xb60));
	return 0xb9;
}

// Slot 440: `0x10394570`.
// `0x10394570`
// `0x10394570`, `CNPC_VMingXiao::TranslateSchedule`, the body of `FElysiumNpcMingXiao::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpcMingXiao::TranslateScheduleRetail(int32 ScheduleNumber)
{
	return ScheduleNumber == 0x147 ? 0x16f : TroikaTranslateScheduleRetail(ScheduleNumber);
}

// Slot 516: `0x10394930`, which replaces the Troika ladder.
/** `CNPC_VMingXiao::MaxYawSpeed` `0x10394930` on this NPC's own words: the body of its override. */
float FElysiumNpcMingXiao::MaxYawSpeed()
{
	// `CNPC_VMingXiao::MaxYawSpeed` `0x10394930` on this NPC's own activity and tuning words — the
	// body of `FElysiumNpcMingXiao::MaxYawSpeed`.
	return MaxYawSpeedMingXiao(ActivityNumber, MingXiaoTuningField);
}

// Slot 69: `0x10396fd0` (byte-identical across Hengeyokai, MingXiao and Tzimisce): the `0x16` derived-type
// gate, then a direct call into the Troika body `0x1029b180`.
bool FElysiumNpcMingXiao::NavIgnoreCollision(FElysiumEntity* Other)
{
	if (Other != nullptr && (RetailDerivedType(*Other) & 0x16) != 0)
	{
		return true;
	}
	return FElysiumNpc::NavIgnoreCollision(Other);
}

// Slot 123: `0x10399d40`
/** `CNPC_VMingXiao::DrawDebugGeometryOverlays` (`0x10399d40`) — four range rings under
 *  `m_debugOverlays & 0x20000000` (NOT bit 0 like its siblings), then the Troika body. */
void FElysiumNpcMingXiao::DrawDebugGeometryOverlays()
{
	// `0x10399d40`, 328 bytes. Gated on `m_debugOverlays & 0x20000000` — the WEAPON-RING bit, not
	// bit 0 like its siblings — so MingXiao's bands and the Troika body's two weapon rings appear
	// together and are meant to be read against each other.
	//
	// Four rings about `(1, 0, 0)` at 100, 150, 200 and 300 source units, height 8.0, colour
	// (255, 32, 32) at alpha 128, no depth test, duration 0. The four radii are the recovered range
	// bands and are the reason to port the body at all.
	if ((DebugOverlays & NpcKernelDebug10_2Shared::GDebug10_2BitWeaponRings) != 0)
	{
		const FVector OriginUnits = Origin / ElysiumMove::U;
		for (const float RadiusUnits : GDebug10_2MingXiaoRadiiUnits)
		{
			EmitOverlayText(NpcKernelDebug10_2Shared::GDebug10_2Circle, OriginUnits,
				FString::Printf(TEXT("axis=(1.0 0.0 0.0) r=%.1f h=%.1f rgba=(255 32 32 128)"),
					RadiusUnits, GDebug10_2MingXiaoRingHeightUnits));
		}
	}
	TroikaDrawDebugGeometryOverlays();
}

// Slot 408: `0x103951d0`, whose miss calls `CAI_BaseNPC::GetShortConditionName` (`0x1027ede0`) directly.
const TCHAR* FElysiumNpcMingXiao::GetShortConditionName(int32 ConditionId)
{
	// The class's own block over ids 0x77..0x7e — the id straight above the base table's last —
	// read from `.rdata` `0x10647194` down to `0x10647178`.
	static const TCHAR* const Names[] = {
		TEXT("xfr"), TEXT("xfl"), TEXT("xmr"), TEXT("xml"),
		TEXT("xbr"), TEXT("xbl"), TEXT("xsp"), TEXT("xmh") };
	const int32 Offset = ConditionId - 0x77;
	if (Offset >= 0 && Offset < UE_ARRAY_COUNT(Names))
	{
		return Names[Offset];
	}
	// The `default:` arm: `CAI_BaseNPC::GetShortConditionName` (`0x1027ede0`) directly.
	return FElysiumNpcBase::GetShortConditionName(ConditionId);
}

// Slot 465: `0x103947b0`, ending in a direct call into `CAI_BaseNPCTroika::OnChangeActivity` (`0x10295a60`).
//
// `CAI_BaseNPCTroika::OnChangeActivity` `0x10295a60` is `return;` — 29c's verdict, and the body the
// generator emits for slot 465. `CNPC_VMingXiao`, `CNPC_VSabbatGunman` and `CNPC_VWerewolf` override
// it on their C++ classes (story 5 step 3) with these bodies, each ending in a direct call into the
// Troika body. `CNPC_Crow#465` (`0x10357b30`) carries no port body: no map stands that class.
// `0x103947b0`
void FElysiumNpcMingXiao::OnChangeActivity(int32 Activity)
{
	// `CNPC_VMingXiao::OnChangeActivity`, 303 bytes. **SEAM** on all three inputs: the gate
	// `0x10398870`, the tuning record `0x101e8da0(0x10739d08)` and the tentacle count (+0x670c)
	// have no port source, so the discipline arm is false, every field answers 0 and the blend
	// lands on its 0.1 floor. The two tails `0x1039ab30` and `0x1039aca0` are MingXiao's own and
	// are named here rather than invented.
	const FMingXiaoPlayback Pick = MingXiaoPlaybackScalar(Activity, /*bDisciplineArm*/ false,
		/*TentacleCount*/ 0, [](int32 Field) { return FacingMingXiaoTuningField(Field); });
	(void)Pick;   // SetPlaybackAndSpeedScalar has no kernel-tier seam in this substrate

	// The tail: `CAI_BaseNPCTroika::OnChangeActivity` (`0x10295a60`) directly.
	FElysiumNpc::OnChangeActivity(Activity);
}

// Slot 337: `0x10392a50`.
int32 FElysiumNpcMingXiao::GetUsedHullBits()
{
	// The Troika body `0x1029a050` called directly, its 1 ORed with this class's bit.
	return FElysiumNpc::GetUsedHullBits() | 0x38000;
}

// Slot 546: `0x10391390`, the class's own schedule id space.
const TCHAR* FElysiumNpcMingXiao::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093bacc`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VMingXiao"), TEXT("0x10391390"), TEXT("0x1093bacc") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// Slot 259: `0x10392a70`, the footstep body of `docs/vtmb/footsteps.md` §1.7; an id it does not
// claim is a direct call into the base body.
bool FElysiumNpcMingXiao::HandleAnimEvent(const FElysiumAnimEvent& Event)
{
	return SpeciesFootstepAnimEvent(TEXT("npc_VMingXiao"), Event);
}

// Slot 563: `0x10392c40`, the `GoalToleranceLead` shape; a replacement that does not chain.
void FElysiumNpcMingXiao::TranslateEnemyChasePosition(FElysiumEntity* Enemy, FVector& ChasePositionCm,
	void* Tolerance, void* SecondTolerance)
{
	TranslateEnemyChasePositionShaped(EChaseTranslateShape::GoalToleranceLead, Enemy, ChasePositionCm, Tolerance,
		SecondTolerance);
}

// --- Moved from `ElysiumNpcDamage2.cpp` (story 5 step 4) ---

void FElysiumNpcMingXiao::SpitAttackTimer(bool bFirstParamSet, bool bSecondParamSet)
{
	// Twenty-nine bytes: `if (p1 && p2) m_flSpitAttackTimer = 0;`. Neither parameter is read for
	// anything else, so the presence test is the whole of the condition.
	if (bFirstParamSet && bSecondParamSet)
	{
		MingXiaoSpitAttackTimer = 0.0;
	}
}

// --- From `FElysiumNpcMingXiaoTentacle` (the move manifest's corrected owner) ---

int32 FElysiumNpcMingXiao::ProxySlotIndexOf(const FElysiumEntity* Proxy) const
{
	// `proxy->+0x6660`: `CNPC_VMingXiaoTentacle::m_iTentacleID`, the tentacle's own index. Retail
	// reads the word off whatever it is handed; a non-tentacle has no such word here and answers
	// `INDEX_NONE`, the "the slot did not resolve" arm.
	FElysiumNpc* Npc = Proxy != nullptr ? const_cast<FElysiumEntity*>(Proxy)->AsNpc() : nullptr;
	const FElysiumNpcMingXiaoTentacle* Tentacle =
		Npc != nullptr ? Npc->AsSpecies<FElysiumNpcMingXiaoTentacle>() : nullptr;
	return Tentacle != nullptr ? Tentacle->TentacleId : INDEX_NONE;
}

bool FElysiumNpcMingXiao::ProxyReadyTimer(const FElysiumEntity* Proxy, double Now)
{
	// `FUN_10397b40`, arm by arm.
	//
	// 1. A null argument answers false.
	if (Proxy == nullptr)
	{
		return false;
	}
	// 2. `curtime < m_flProxyReadyTimer` (`+0x66a4`) answers false — the cooldown is not up. The
	//    word is family **Bosses**' `MingXiaoProxyReadyTimer`, read through its owner.
	if (Now < MingXiaoProxyReadyTimer)
	{
		return false;
	}
	// 3. The argument's own slot index (`proxy+0x6660`) must index back to the argument through
	//    `this+0x66a8 + slot*4`, `m_rhSeveredTentacles` (story 5 step 4 CORRECTION: the port resolved
	//    through `m_rhProxies` `+0x668c`, the array arm 5 counts). Anything else answers false.
	const int32 Slot = ProxySlotIndexOf(Proxy);
	if (Slot < 0 || Slot >= MingXiaoProxySlots)
	{
		return false;
	}
	const FElysiumEntity* Registered = (World && SeveredTentacles[Slot].IsSet())
		? World->Resolve(SeveredTentacles[Slot]) : nullptr;
	if (Registered != Proxy)
	{
		return false;
	}
	// 4. An already-registered slot answers TRUE at once, before the census below.
	if (bProxyRegistered[Slot])
	{
		return true;
	}
	// 5. Count the six slots that are either a live handle OR already registered. Only a count of
	//    ZERO registers this slot and answers true — one proxy at a time.
	int32 Taken = 0;
	for (int32 i = 0; i < MingXiaoProxySlots; ++i)
	{
		const bool bLive =
			World && Proxies[i].IsSet() && World->Resolve(Proxies[i]) != nullptr;
		if (bLive || bProxyRegistered[i])
		{
			++Taken;
		}
	}
	if (Taken < 1)
	{
		bProxyRegistered[Slot] = true;
		return true;
	}
	return false;
}

// --- Moved from `ElysiumNpcAnim.cpp` (story 5 step 4) ---

bool FElysiumNpcMingXiao::BodyGroupCvarIsCommand() const
{
	// `ConVar::IsCommand()` on `DAT_1093bb14` (`debug_tentacle_mask`): a ConVar object, never a
	// ConCommand.
	return false;
}

int32 FElysiumNpcMingXiao::BodyGroupCvarValue() const
{
	// `ConVar::m_nValue` (`+0x2c`) on the same object: `debug_tentacle_mask`, shipped "-1" — the
	// negative arm, which takes the severed-tentacle mask.
	return ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::DebugTentacleMask);
}

void FElysiumNpcMingXiao::BodyGroup()
{
	// `0x10398800`, three arms in retail's order. The cvar is read TWICE, and the second read is not
	// redundant: retail asks `IsCommand()` again rather than caching it.
	if (!BodyGroupCvarIsCommand() && BodyGroupCvarValue() < 0)
	{
		NpcBody = static_cast<int32>(MingXiaoSeveredTentacleMask);
		return;
	}
	if (BodyGroupCvarIsCommand())
	{
		NpcBody = 0;
		return;
	}
	NpcBody = BodyGroupCvarValue();
}

// --- Moved from `ElysiumNpcKernelAnim10_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcBosses.cpp` (story 5 step 4) ---

int32 FElysiumNpcMingXiao::MingXiaoPedestalCvar() const
{
	// `DAT_1093ba8c` `+0x2c`: `ming_xiao_pickup`, shipped "1", which opens the search.
	return ElysiumNpcTunables::ConVarInt(ElysiumNpcTunables::EConVar::MingXiaoPickup);
}

FVector FElysiumNpcMingXiao::EntityVelocityUnits(const FElysiumEntity& Entity) const
{
	// Slot 199 `GetVelocity(Vector*, AngularImpulse*)`, the linear half, in SOURCE units.
	return Entity.Velocity / ElysiumMove::U;
}

// --- Moved from `ElysiumNpcKernelBosses2.cpp` (story 5 step 4) ---

bool FElysiumNpcMingXiao::IsTentacleConnected(int32 TentacleId) const
{
	// `0x10398000`: `(m_iSeveredTentacleMask & (1 << (n & 0x1f))) == 0`.
	return (MingXiaoSeveredTentacleMask & (1u << (static_cast<uint32>(TentacleId) & 0x1fu))) == 0u;
}

bool FElysiumNpcMingXiao::IsMingXiaoProxy() const
{
	// `0x10398870`: `m_iTentacleID != -1`.
	return MingXiaoTentacleId != INDEX_NONE;
}

int32 FElysiumNpcMingXiao::FUN_10396dc0() const
{
	// `0x10396dc0`, the whole body:
	//     if (m_hThrowObject resolves to a live entity
	//         && 2 < m_eThrowableObjectMode && m_eThrowableObjectMode < 5) {
	//         if (HasCondition(0x7b)) { <selector trace>; return 0x15c; }
	//         if (HasCondition(0x7c)) { <selector trace>; return 0x15d; }
	//     }
	//     return 0;
	// The band is STRICT on both sides — modes 3 and 4 only. The selector trace writes retail's
	// `__FILE__`/`__LINE__` into `+0x1b30`/`+0x1b34`, which the shape map records as ABSENT here.
	if (World == nullptr || World->Resolve(MingXiaoThrowObject) == nullptr)
	{
		return 0;
	}
	if (!(2 < MingXiaoThrowableObjectMode && MingXiaoThrowableObjectMode < 5))
	{
		return 0;
	}
	if (Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(MingXiaoCondGrabA)))
	{
		return MingXiaoSchedGrabA;
	}
	if (Cognition.Conditions.Has(static_cast<EElysiumNpcCond>(MingXiaoCondGrabB)))
	{
		return MingXiaoSchedGrabB;
	}
	return 0;
}

void FElysiumNpcMingXiao::FUN_10397a50(const FElysiumEntity* Proxy,
	TFunctionRef<float(int32)> TuningField)
{
	// `0x10397a50`:
	//     if (param_1 == NULL) return;
	//     f = Tuning[100] + Tuning[0x68] * (6 - m_iConnectedTentacleCount);
	//     if (f <= 0.0) f = 0.0;                                       // _DAT_104454c4
	//     m_flProxyReadyTimer = f + curtime;                           // +0x66a4
	//     if (Resolve(m_rhProxies[param_1->m_iTentacleID]) == param_1)  // +0x668c indexed by +0x6674
	//         SeverTentacle(param_1->m_iTentacleID);                   // 0x10397930
	// The clamp is `<=`, so an exactly-zero blend also takes the floor — the same value either way,
	// and reproduced as written.
	if (Proxy == nullptr)
	{
		return;
	}
	float Blend = TuningField(100)
		+ TuningField(0x68) * static_cast<float>(6 - MingXiaoConnectedTentacleCount);
	if (Blend <= Bosses2Zero)
	{
		Blend = Bosses2Zero;
	}
	MingXiaoProxyReadyTimer =
		static_cast<double>(Blend) + (World != nullptr ? World->NowSeconds() : 0.0);

	// Retail reads `param_1 + 0x6674`, `CNPC_VMingXiao::m_iTentacleID`: the argument is one of this
	// head's `m_rhProxies`, a `CNPC_VMingXiao` of its own (the head's word is -1), so the id is read
	// off it through its class. Anything else has no such word and is left alone.
	const FElysiumNpc* ProxyNpc = const_cast<FElysiumEntity*>(Proxy)->AsNpc();
	const FElysiumNpcMingXiao* ProxyHead = ProxyNpc != nullptr ? ProxyNpc->AsSpecies<FElysiumNpcMingXiao>() : nullptr;
	if (ProxyHead == nullptr || World == nullptr)
	{
		return;
	}
	const int32 Id = ProxyHead->MingXiaoTentacleId;
	if (Id < 0 || Id >= 6)
	{
		return;
	}
	if (World->Resolve(Proxies[Id]) == Proxy)
	{
		SeverTentacle(Id);
	}
}

float FElysiumNpcMingXiao::FUN_10397f70(TFunctionRef<float(int32)> TuningField) const
{
	// `0x10397f70`:
	//     if (m_iTentacleID != -1) return Tuning[0x20];                 // 0x10398870
	//     f = Tuning[0x6c] + Tuning[0x70] * (6 - m_iConnectedTentacleCount);
	//     if (f <= 0.0) f = 0.0;
	//     return f;
	if (IsMingXiaoProxy())
	{
		return TuningField(0x20);
	}
	const float Blend = TuningField(0x6c)
		+ TuningField(0x70) * static_cast<float>(6 - MingXiaoConnectedTentacleCount);
	return Blend <= Bosses2Zero ? Bosses2Zero : Blend;
}

bool FElysiumNpcMingXiao::ChooseMeleeAttackSequenceSeam() const
{
	// SEAM for `0x10398030`'s `param_2` tail. Slot 331's Troika body (`0x10347180`) is story 29d's
	// and `m_hMeleeWeapon`'s owner chain has no counterpart here. False is retail's refusal.
	return false;
}

bool FElysiumNpcMingXiao::FUN_10398030(int32 Slot, bool bTestMelee, int32& OutSchedule)
{
	// `0x10398030`, arm for arm and in retail's order:
	//
	//     if (!IsTentacleConnected(param_1)) return false;              // 0x10398000
	//     if (curtime < m_rflAttackTimers[param_1]) return false;       // +0x66c4 + param_1*4
	//     sched = -1;
	//     switch (param_1) {
	//       case 0: case 1:
	//         if (m_bBlockedByFriend) return false;                     // 0x1039ab10
	//         if (dist < 100.0)  return false;                          // _DAT_10450564
	//         if (dist >= 300.0) return false;                          // _DAT_10462b84
	//         sched = 0x112a + param_1;  break;
	//       case 2: case 3:
	//         if (m_bBlockedByFriend) return false;
	//         if (dist < 100.0)  return false;
	//         if (dist >= 200.0) return false;                          // _DAT_104492b8
	//         sched = 0x112c + (param_1 - 2);  break;
	//       case 4: case 5:
	//         if (m_bBlockedByFriend) return false;
	//         if (dist < 150.0) return false;                           // _DAT_10457f60
	//         if (!m_hThrowObject resolves live) return false;          // +0x6718
	//         if (m_eThrowingTentacle != param_1) return false;         // +0x671c
	//         break;                                                    // sched STAYS -1
	//     }
	//     if (param_2 && sched != -1) { <the melee tail>; }
	//     return true;
	//
	// Two facts worth stating because they look like slips and are not. Slots 4 and 5 leave the
	// schedule at -1, so the `param_2` tail is SKIPPED for them however `param_2` was passed. And
	// `param_1` outside 0..5 falls through the switch with the schedule still -1 and answers TRUE,
	// having passed only the mask and timer gates.
	OutSchedule = INDEX_NONE;
	if (!IsTentacleConnected(Slot))
	{
		return false;
	}
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (Slot >= 0 && Slot < 6 && Now < MingXiaoAttackTimers[Slot])
	{
		return false;
	}
	const float DistUnits =
		static_cast<float>(Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U);
	switch (Slot)
	{
	case 0:
	case 1:
	case 2:
	case 3:
	{
		if (BlockedByFriend())
		{
			return false;
		}
		if (DistUnits < MingXiaoNearBand)
		{
			return false;
		}
		const float Ceiling = (Slot < 2) ? MingXiaoFarBand300 : MingXiaoFarBand200;
		if (DistUnits >= Ceiling)
		{
			return false;
		}
		OutSchedule = MingXiaoTentacleSchedules[Slot];
		break;
	}
	case 4:
	case 5:
		if (BlockedByFriend())
		{
			return false;
		}
		if (DistUnits < MingXiaoReachBand)
		{
			return false;
		}
		if (World == nullptr || World->Resolve(MingXiaoThrowObject) == nullptr)
		{
			return false;
		}
		if (MingXiaoThrowingTentacle != Slot)
		{
			return false;
		}
		break;
	default:
		break;
	}
	if (bTestMelee && OutSchedule != INDEX_NONE)
	{
		if (!ChooseMeleeAttackSequenceSeam())
		{
			return false;
		}
	}
	return true;
}

float FElysiumNpcMingXiao::FUN_103983d0(int32 Selector, TFunctionRef<float(int32)> TuningField) const
{
	// `0x103983d0`, recovered from the LISTING: the decompiled C lost the jump table at
	// `0x10398598` and the ST0 return, and read the selector as a return-storage pointer.
	//
	//     switch (selector) {
	//       case 0: case 1:
	//         if (dist > 200.0) <a dead Tuning fetch whose result is discarded>;
	//         scale = dist <= 150.0 ? Tuning[0x38] : Tuning[0x34];
	//         v = Tuning[0x74] + Tuning[0x78] * (6 - count);
	//         if (v <= 0) v = 0;
	//         return v * scale;
	//       case 2: case 3:
	//         scale = dist <= 150.0 ? Tuning[0x40] : Tuning[0x3c];
	//         v = Tuning[0x74] + Tuning[0x78] * (6 - count);
	//         if (v <= 0) v = 0;
	//         return v * scale;
	//       case 4: case 5:
	//         if (dist <= 200.0) { a = Tuning[0x84]; b = Tuning[0x88]; }
	//         else               { a = Tuning[0x7c]; b = Tuning[0x80]; }
	//         v = a + b * (6 - count);
	//         return v <= 0 ? 0 : v;                                   // NO second factor
	//       default: return 20.0;                                      // _DAT_1044eb0c
	//     }
	//
	// Slots 0/1 and 2/3 share the `0x74`/`0x78` blend and differ only in which pair of scale cells
	// the distance picks; slots 4/5 use a different pair entirely and skip the multiply. The dead
	// fetch on slots 0/1 is retail's and is not reproduced: its result is discarded before the next
	// instruction and nothing observes the call.
	const float DistUnits =
		static_cast<float>(Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U);
	const float Count = static_cast<float>(6 - MingXiaoConnectedTentacleCount);
	switch (Selector)
	{
	case 0:
	case 1:
	case 2:
	case 3:
	{
		const int32 NearCell = (Selector < 2) ? 0x38 : 0x40;
		const int32 FarCell = (Selector < 2) ? 0x34 : 0x3c;
		const float Scale = (DistUnits <= MingXiaoReachBand) ? TuningField(NearCell)
			: TuningField(FarCell);
		float Blend = TuningField(0x74) + TuningField(0x78) * Count;
		if (Blend <= Bosses2Zero)
		{
			Blend = Bosses2Zero;
		}
		return Blend * Scale;
	}
	case 4:
	case 5:
	{
		const int32 BaseCell = (DistUnits <= MingXiaoFarBand200) ? 0x84 : 0x7c;
		const int32 StepCell = (DistUnits <= MingXiaoFarBand200) ? 0x88 : 0x80;
		const float Blend = TuningField(BaseCell) + TuningField(StepCell) * Count;
		return Blend <= Bosses2Zero ? Bosses2Zero : Blend;
	}
	default:
		return MingXiaoThrowDefault;
	}
}

bool FElysiumNpcMingXiao::PedestalTaskForSide(const FVector& DeltaUnits, const FVector& Forward,
	const FVector& Right, bool bTentacle4Connected, bool bTentacle5Connected, int32& OutTask)
{
	// `0x103989b0`, read from the listing because the decompiler dropped the `VectorNormalize` call
	// and turned three `FCOM`s into status-word arithmetic:
	//
	//     d = param_1 - GetAbsOrigin();
	//     if (fabs(d.z) > 64.0) return false;                    // _DAT_1049ae28, a double
	//     d.z = 0;  VectorNormalize(&d);                         // 0x10137220
	//     f = d.x * m_vecForward.x + d.y * m_vecForward.y;
	//     if (f < -0.17) return false;                           // _DAT_104bde64
	//     if (f >  0.5)  return false;                           // _DAT_104454d0
	//     r = d.x * m_vecRight.x + d.y * m_vecRight.y;
	//     if (r <= 0.0) { if (TentacleConnected(5)) { *out = 5; return true; } }
	//     else          { if (TentacleConnected(4)) { *out = 4; return true; } }
	//     return false;
	//
	// The height gate is inclusive at 64 and the forward band inclusive at both edges; `r == 0`
	// takes the tentacle-5 arm. The dot is against a NORMALIZED 2-D direction, so the band is an
	// angle and not a distance — which is what makes `[-0.17, 0.5]` an abeam wedge.
	if (FMath::Abs(DeltaUnits.Z) > PedestalHeightTolerance)
	{
		return false;
	}
	FVector Flat(DeltaUnits.X, DeltaUnits.Y, 0.0);
	Flat = Bosses2Normalize(Flat);
	const float ForwardDot = static_cast<float>(Flat.X * Forward.X + Flat.Y * Forward.Y);
	if (ForwardDot < PedestalForwardLo)
	{
		return false;
	}
	if (ForwardDot > PedestalForwardHi)
	{
		return false;
	}
	const float RightDot = static_cast<float>(Flat.X * Right.X + Flat.Y * Right.Y);
	if (RightDot <= Bosses2Zero)
	{
		if (bTentacle5Connected)
		{
			OutTask = 5;
			return true;
		}
		return false;
	}
	if (bTentacle4Connected)
	{
		OutTask = 4;
		return true;
	}
	return false;
}

bool FElysiumNpcMingXiao::FUN_103989b0(const FVector& TargetOriginUnits, int32& OutTask) const
{
	// The member form. `m_vecForward` (+0x6290) and `m_vecRight` (+0x629c) are retail's cached
	// basis; NOTHING in this runtime writes them, so both are the zero vector, every dot is 0.0,
	// the forward band admits it (0 is inside `[-0.17, 0.5]`) and the right test takes its `<= 0`
	// arm — tentacle 5 for every candidate inside the height gate. That is the seam's answer, not
	// retail's, and it is stated rather than hidden.
	const FVector DeltaUnits = TargetOriginUnits - Origin / ElysiumMove::U;
	return PedestalTaskForSide(DeltaUnits, Forward, Right, IsTentacleConnected(4),
		IsTentacleConnected(5), OutTask);
}

void FElysiumNpcMingXiao::MingXiaoPedestalAimPoint(const FVector& PedestalOriginUnits, int32 Task,
	FVector& OutAimPointUnits, FVector& OutForward) const
{
	// SEAM for `0x10398890`:
	//     r  = m_vecRight   * _DAT_1049ae40;
	//     p  = pedestal     + m_vecForward * _DAT_10463584;
	//     out = (task == 4) ? p + r : p - r;
	//     outForward = m_vecForward;
	// Both cells live past `.data`'s raw size and are **unrecovered**; with them reading 0 the aim
	// point is the pedestal's own origin, which is the seam's answer. The SIGN split on task 4 and
	// the forward copy are the recovered half and are ported.
	OutAimPointUnits = PedestalOriginUnits;
	(void)Task;
	OutForward = Forward;
}

FElysiumEntity* FElysiumNpcMingXiao::FindNearestPedestal(float RadiusUnits, int32& OutTask)
{
	// `0x10398b20`'s search, behind the cvar gate:
	//     best = 257.0;  winner = NULL;  me = GetAbsOrigin();
	//     while ((e = FindEntityInSphere(prev, 6, me, best)) != NULL) {
	//         if (BossBlacklistHolds(e)) continue;                        // 0x10366400
	//         if (strnicmp(e->m_iName, "Pedestal", 8) != 0) continue;
	//         e->GetVelocity(&v, &av);                                     // slot 199
	//         if (fabs(v.x) > 0.1 || fabs(v.y) > 0.1 || fabs(v.z) >= 0.1) continue;
	//         if (!PedestalTask(e->GetAbsOrigin(), &task)) continue;       // 0x103989b0
	//         best = length(me - e->GetAbsOrigin());  winner = e;          // the radius SHRINKS
	//     }
	//
	// Three facts reproduced verbatim. The sphere radius is re-read each iteration, so every
	// accepted winner tightens the search. The Z tolerance is STRICT (`< 0.1`) while X and Y are
	// inclusive (`<= 0.1`) — retail spells the third compare differently from the first two. And the
	// name test is an eight-character case-insensitive PREFIX, so `Pedestal_03` matches.
	FElysiumEntity* Winner = nullptr;
	if (World == nullptr)
	{
		return nullptr;
	}
	float Best = RadiusUnits;
	const FVector MeUnits = Origin / ElysiumMove::U;
	for (const TUniquePtr<FElysiumEntity>& Owned : World->Entities())
	{
		FElysiumEntity* Candidate = Owned.Get();
		if (Candidate == nullptr || Candidate == this || Candidate->IsInert())
		{
			continue;
		}
		const FVector CandidateUnits = Candidate->Origin / ElysiumMove::U;
		if ((CandidateUnits - MeUnits).SizeSquared() >= static_cast<double>(Best) * Best)
		{
			continue;
		}
		if (BossBlacklistHolds(Candidate))
		{
			continue;
		}
		if (FCString::Strnicmp(*Candidate->TargetName, PedestalNamePrefix, PedestalNamePrefixLength)
			!= 0)
		{
			continue;
		}
		const FVector V = EntityVelocityUnits(*Candidate);
		if (FMath::Abs(V.X) > PedestalStationaryTolerance
			|| FMath::Abs(V.Y) > PedestalStationaryTolerance
			|| !(FMath::Abs(V.Z) < PedestalStationaryTolerance))
		{
			continue;
		}
		int32 Task = OutTask;
		if (!FUN_103989b0(CandidateUnits, Task))
		{
			continue;
		}
		OutTask = Task;
		Best = static_cast<float>((MeUnits - CandidateUnits).Size());
		Winner = Candidate;
	}
	return Winner;
}

FElysiumEntity* FElysiumNpcMingXiao::FUN_10398b20(int32& InOutTask, FVector& OutAimPointUnits,
	FVector& OutForward)
{
	// `0x10398b20`'s head, in retail's order:
	//     if (m_flClosestPlayerDistance < 150.0) return NULL;             // _DAT_10457f60
	//     if (!TentacleConnected(4) && !TentacleConnected(5)) return NULL;
	//     if (cvar(DAT_1093ba8c) == 0) return NULL;
	//     winner = <the search>;
	//     if (winner) { AimPoint(winner->GetAbsOrigin(), *param_1, &pos, &fwd); return winner; }
	//     return NULL;
	// Note the aim call re-reads `*param_1` — the task the search wrote through the same pointer.
	const float DistUnits =
		static_cast<float>(Senses.Memory.ClosestPlayerDistanceCm / ElysiumMove::U);
	if (DistUnits < MingXiaoReachBand)
	{
		return nullptr;
	}
	if (!IsTentacleConnected(4) && !IsTentacleConnected(5))
	{
		return nullptr;
	}
	if (MingXiaoPedestalCvar() == 0)
	{
		return nullptr;
	}
	FElysiumEntity* Winner = FindNearestPedestal(PedestalSearchRadius, InOutTask);
	if (Winner == nullptr)
	{
		return nullptr;
	}
	MingXiaoPedestalAimPoint(Winner->Origin / ElysiumMove::U, InOutTask, OutAimPointUnits,
		OutForward);
	return Winner;
}

// --- Moved from `ElysiumNpcCombat10.cpp` (story 5 step 4) ---

bool FElysiumNpcMingXiao::MingXiaoLimbPresent(int32 /*LimbIndex*/) const
{
	// SEAM for `0x10398000(this, i)`. No port system stands Ming Xiao's severable limbs.
	return false;
}

// --- Moved from `ElysiumNpcCombat10_2.cpp` (story 5 step 4) ---

// --- `CNPC_VMingXiao::SelectScheduleRangedCombat` `0x103967d0`, 794 bytes ------------------------

// --- Moved from `ElysiumNpcConditions10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcDamage.cpp` (story 5 step 4) ---

bool FElysiumNpcMingXiao::TestOneHitbox(int32 HitboxSetIndex, const FVector& RayStartUnits,
	const FVector& RayEndUnits, uint32 Mask) const
{
	// SEAM for `thunk_FUN_10399ef0(ray, mask, trace, studiohdr, hitboxset, bonecache)`.
	(void)HitboxSetIndex;
	(void)RayStartUnits;
	(void)RayEndUnits;
	(void)Mask;
	return false;
}

float FElysiumNpcMingXiao::MingXiaoThrowCvar(int32 Which) const
{
	// SEAM for `DAT_1093bbcc` (0, the quadratic term), `DAT_1093bc14` (1, the constant term) and
	// `DAT_1093b9fc` (2, the Z term) of `0x103990c0`'s throw speed. All three pointer cells live
	// past `.data`'s raw size and no corpus function constructs them: UNRECOVERED, all answer 0.0f.
	(void)Which;
	return NpcKernelDamageShared::DamageZero;
}

// --- Moved from `ElysiumNpcDamage2.cpp` (story 5 step 4) ---

float FElysiumNpcMingXiao::MingXiaoThrowSpeed(float DistanceSquared, float Quadratic, float Constant)
{
	// The listing's two arms, both computing the same sum and differing only in what they answer:
	//     if (quadratic * distSq + constant <= 1000.0)  speed = 1000.0;   // _DAT_10447ee0
	//     else                                          speed = quadratic * distSq + constant;
	// so 1000 is a FLOOR on the throw speed and not a cap.
	const float Sum = Quadratic * DistanceSquared + Constant;
	return (Sum <= ThrowSpeedFloor) ? ThrowSpeedFloor : Sum;
}

void FElysiumNpcMingXiao::MingXiaoThrowCleanup()
{
	// 0x10398fd0, and also the tail every path of `0x103990c0` runs.
	if (MingXiaoPhysicsAnimlink.IsSet())
	{
		RemoveNamedEntity(MingXiaoPhysicsAnimlink);
		MingXiaoPhysicsAnimlink = FElysiumEntityHandle();
	}
	// UNCONDITIONAL, outside the handle guard.
	MingXiaoThrowObject = FElysiumEntityHandle();
	ArmIgnoreCollisionExpiry(NpcKernelDamage2Shared::ThrowIgnoreCollisionSeconds);
	ThrowableObjectMode(0);
}

void FElysiumNpcMingXiao::LaunchRagdollTowardTarget()
{
	// 1. Remove and clear `m_hPhysicsAnimlink` (+0x6738) if it resolves — the link goes before the
	//    impulse, not after.
	if (MingXiaoPhysicsAnimlink.IsSet())
	{
		RemoveNamedEntity(MingXiaoPhysicsAnimlink);
		MingXiaoPhysicsAnimlink = FElysiumEntityHandle();
	}

	// 2. Everything else is inside `GetEnemy() != null` (slot 167, `+0x29c`).
	FElysiumEntity* Enemy = (World != nullptr && BaseMemory.Enemy.IsSet())
		? World->Resolve(BaseMemory.Enemy) : nullptr;
	if (Enemy != nullptr)
	{
		// 3. The held object's CENTRE (`+0x370`), not its origin, is the launch point.
		const FVector FromUnits = MingXiaoThrowObject.IsSet() && World != nullptr
			&& World->Resolve(MingXiaoThrowObject) != nullptr
			? World->Resolve(MingXiaoThrowObject)->Origin / ElysiumMove::U
			: Origin / ElysiumMove::U;

		// 4. The lead point. `thunk_FUN_102c36d0(this, from, enemy, gravity, null, &out)` solves it
		//    with the gravity cvar; that solver is family Bosses' `SolveThrowImpulse`'s sibling and
		//    has no body here, so the enemy's own position stands for the lead and the substitution
		//    is named. The enemy's per-frame position delta (`+0xa0..0xa2` minus `+0x9d..0x9f` — its
		//    current origin minus its previous one) scaled by 0.5 (`_DAT_104454d0`) is then added to
		//    the lead's Z, and ONLY to its Z.
		FVector Lead = Enemy->Origin / ElysiumMove::U;
		const FVector EnemyDelta = FVector::ZeroVector;   // SEAM: no previous-origin word here
		Lead.Z += EnemyDelta.Z * ThrowLeadZScale;

		// 5. The cone. `UTIL_AngleDiff(VecToYaw(lead - from), GetAngles().y)` outside `[-20, +20]`
		//    re-aims the XY at exactly `yaw - 20` (below the cone) or `yaw + 20` (above it),
		//    preserving Z. The listing's two `FCOMP`s are inclusive at both edges, exactly as family
		//    Bosses recorded for the pickup cone that reads the same two cells.
		FVector Delta = Lead - FromUnits;
		const float SelfYaw = static_cast<float>(Angles.Y);
		const float Diff = Damage2AngleDiff(Damage2VecToYaw(Delta), SelfYaw);
		if (Diff < ThrowConeLo || Diff >= ThrowConeHi)
		{
			const float Clamped = (Diff < ThrowConeLo) ? (SelfYaw - ThrowConeHi)
													   : (SelfYaw + ThrowConeHi);
			const float Rad = FMath::DegreesToRadians(Clamped);
			const float Len = static_cast<float>(FVector2D(Delta.X, Delta.Y).Size());
			Delta.X = FMath::Cos(Rad) * Len;
			Delta.Y = -FMath::Sin(Rad) * Len;
		}
		const float DistSq = static_cast<float>(Delta.SizeSquared());
		FVector Impulse = Delta.GetSafeNormal();

		// 6. The speed, then the Z term — which is added AFTER the XY scale and is a separate cvar.
		const float Speed = MingXiaoThrowSpeed(DistSq, MingXiaoThrowCvar(0), MingXiaoThrowCvar(1));
		Impulse *= Speed;
		Impulse.Z += MingXiaoThrowCvar(2) * DistSq;

		// 7. `CRagdollProp`'s `+0x428`, or the physics object's `+0xa0`/`+0x9c` fallback. Family
		//    Bosses stands both as one seam and this body reuses it rather than declaring a second.
		ApplyThrowImpulse(MingXiaoThrowObject, Impulse);
	}

	// 8. The tail runs on EVERY path — including the one with no enemy.
	MingXiaoThrowObject = FElysiumEntityHandle();
	ArmIgnoreCollisionExpiry(NpcKernelDamage2Shared::ThrowIgnoreCollisionSeconds);
	ThrowableObjectMode(0);
}

void FElysiumNpcMingXiao::MingXiaoThrowAttack(int32 TaskId, int32 Tentacle,
	TFunctionRef<float(int32)> TuningField)
{
	// 1. `thunk_FUN_102e0b40(m_pNavigator)` — clear the path. No navigator here; recorded by the
	//    absence, as family Motor states.
	// 2. `m_hMeleeWeapon`'s owner (+0xa0) is resolved and then `+0x610` runs, both before the
	//    active-weapon test.
	// 3. No active weapon fails the task with code 0x1f and returns.
	(void)TaskId;
	if (!Inventory.ActiveWeapon.IsSet())
	{
		TaskFail(MingXiaoNoWeaponFailure);
		return;
	}
	// 4. The enemy's `+0x9c` (its own owner/target word) is read, then the weapon's activity
	//    translation (`+0x5a4`) and `+0x5e0`, then slot 331 `ChooseMeleeAttackSequence`. A refusal
	//    OR a negative activity fails the task; a success sets the activity through `+0x4dc`.
	//    Slot 331 is family Bosses' `ChooseMeleeAttackSequenceSeam`, which answers FALSE.
	if (!ChooseMeleeAttackSequenceSeam())
	{
		TaskFail(MingXiaoNoWeaponFailure);
	}
	// 5. Either way the attack timer is stamped: `m_rflAttackTimers[t] = curtime + FUN_103983d0(..)`
	//    — family Bosses owns both the array and the curve, and this body reuses them.
	if (Tentacle >= 0 && Tentacle < 6)
	{
		const double Now = World != nullptr ? World->NowSeconds() : 0.0;
		MingXiaoAttackTimers[Tentacle] = Now + static_cast<double>(
			FUN_103983d0(MingXiaoThrowingTentacle, TuningField));
	}
}

int32 FElysiumNpcMingXiao::MingXiaoFindThrowObject(int32 PedestalCvarDraw, int32 PedestalCvarCeiling)
{
	// 1. A LIVE `m_hThrowObject` answers 0 — the search only runs with empty hands.
	if (MingXiaoThrowObject.IsSet() && World != nullptr
		&& World->Resolve(MingXiaoThrowObject) != nullptr)
	{
		return 0;
	}
	// 2. Two curtime-gated cooldowns, in THIS order: `+0x66d8` first, then `+0x66d4`.
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	if (Now < MingXiaoAttackTimers[5])
	{
		return 0;
	}
	if (Now < MingXiaoAttackTimers[4])
	{
		return 0;
	}
	// 3. With an enemy, `+0x848` runs — the per-species float family Bosses records as one of the
	//    four overrides of that virtual. Then `ClearCondition(9)`, unconditionally.
	Cognition.Conditions.Clear(EElysiumNpcCond::TooFarForMelee);   // retail's ClearCondition(9)

	// 4. `RandomInt(...)` against the tuning record's `+8` cell: the search runs ONLY on a draw
	//    strictly below it. The caller hands both in so the gate is measurable; the record itself
	//    lives past `.data`'s raw size, which families Bosses, Facing and Motor all record.
	if (PedestalCvarDraw < PedestalCvarCeiling)
	{
		int32 Task = MingXiaoThrowingTentacle;
		FVector AimPoint = FVector::ZeroVector;
		FVector SavedForward = FVector::ZeroVector;
		FElysiumEntity* Pedestal = FUN_10398b20(Task, AimPoint, SavedForward);
		MingXiaoThrowObject = Pedestal != nullptr ? Pedestal->Handle : FElysiumEntityHandle();
		MingXiaoThrowingTentacle = Task;
		MingXiaoPickupTargetPos = AimPoint;
		MingXiaoPickupSavedForward = SavedForward;
	}

	// 5. Only a LIVE object answers the schedule: ignore its collision, set the throwable mode to 1,
	//    stamp the selector trace with line 0xbc3 and answer schedule 0x167.
	if (MingXiaoThrowObject.IsSet() && World != nullptr
		&& World->Resolve(MingXiaoThrowObject) != nullptr)
	{
		StartIgnoringCollision(MingXiaoThrowObject);
		ThrowableObjectMode(1);
		(void)MingXiaoGrabTraceLine;
		return MingXiaoGrabSchedule;
	}
	return 0;
}

void FElysiumNpcMingXiao::ThrowableObjectMode(int32 Mode)
{
	// Thirteen bytes, one store. `m_eThrowableObjectMode` is family Bosses' member at +0x673c.
	MingXiaoThrowableObjectMode = Mode;
}

bool FElysiumNpcMingXiao::SeveredTentaclesCanStandOn(const FElysiumEntity* Candidate) const
{
	// The loop walks index 0..5 and reads TWO words per step: `puVar5[-7]` is `m_rhProxies[i]`
	// (+0x668c, seven dwords below `m_rhSeveredTentacles` at +0x66a8) and `*puVar5` the severed
	// tentacle. Both resolved pointers are compared against the candidate and either match answers
	// FALSE — `return uVar1 & 0xffffff00`, i.e. AL = 0.
	for (int32 i = 0; i < 6; ++i)
	{
		const FElysiumEntity* Proxy =
			(World != nullptr && Proxies[i].IsSet()) ? World->Resolve(Proxies[i]) : nullptr;
		if (Proxy == Candidate)
		{
			return false;
		}
		const FElysiumEntity* Severed = (World != nullptr && SeveredTentacles[i].IsSet())
			? World->Resolve(SeveredTentacles[i]) : nullptr;
		if (Severed == Candidate)
		{
			return false;
		}
	}
	// On a full miss a NON-NULL candidate is asked its own `IsStandable` (+0x290, slot 164) and a
	// false there answers false; a NULL candidate skips the call entirely and answers TRUE.
	if (Candidate != nullptr && !CandidateIsStandable(Candidate))
	{
		return false;
	}
	return true;
}

bool FElysiumNpcMingXiao::TestHitboxesMingXiao(const FVector& RayStartUnits, const FVector& RayEndUnits,
	uint32 Mask)
{
	// Three refusals, in retail's order: no model, then `!m_bHasTransformed`, then fewer than 7
	// hitbox sets (`*(int *)(studiohdr + 0x100) < 7`).
	if (!bMingXiaoHasTransformed)
	{
		return false;
	}
	if (HitboxSetCount() < MingXiaoHitboxSetsRequired)
	{
		return false;
	}
	// Hitbox set 0 — the master box — is tested FIRST and unguarded; a hit ends the body.
	if (TestOneHitbox(0, RayStartUnits, RayEndUnits, Mask))
	{
		return true;
	}
	// Then sets 1..6, each gated by `thunk_FUN_10398000(this, index)` — family Bosses'
	// `IsTentacleConnected`, which answers "tentacle n is NOT severed". The set offset walks
	// `0xc, 0x18, … 0x48` while the tentacle index walks `0, 1, … 6`, so the LOOP runs seven times
	// with the index one ahead of the set: index 0 pairs with set 1.
	for (int32 i = 0; i < 6; ++i)
	{
		if (!IsTentacleConnected(i))
		{
			continue;
		}
		if (TestOneHitbox(i + 1, RayStartUnits, RayEndUnits, Mask))
		{
			return true;
		}
	}
	// Retail's fallthrough answers TRUE — `return CONCAT31(…, 1)` — even when nothing was hit. That
	// is not a transcription slip: the body's answer is "I handled the hitbox test", not "I hit".
	return true;
}

// --- Moved from `ElysiumNpcDebug10_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcFacing.cpp` (story 5 step 4) ---

FElysiumNpc::FMingXiaoPlayback FElysiumNpcMingXiao::MingXiaoPlaybackScalar(int32 Activity,
	bool bDisciplineArm, int32 TentacleCount, TFunctionRef<float(int32)> TuningField)
{
	// `CNPC_VMingXiao::OnChangeActivity` `0x103947b0`. Two arms of three rows each, selected by
	// `0x10398870` — the gate `docs/vtmb/animation_and_movers.md` names "+0x6674". The activity
	// numbers are the listing's raw words read as floats: 9 (`ACT_WALK`), 0x13 (`ACT_RUN`), 0x4b and
	// 0x1132.
	FMingXiaoPlayback Out;
	Out.bWalkOrRun = (Activity == 9 || Activity == 0x13);
	if (bDisciplineArm)
	{
		// A flat scalar straight off the record: +0x1c for walk/run, +0x18 for 0x4b, +0x14 otherwise.
		if (Out.bWalkOrRun)
		{
			Out.Scalar = TuningField(0x1c);
			return Out;
		}
		if (Activity == 0x4b)
		{
			// This arm writes the scalar and leaves at once, taking the second tail below without a
			// second `SetPlaybackAndSpeedScalar`.
			Out.Scalar = TuningField(0x18);
			Out.bSecondWriteSkipped = true;
			return Out;
		}
		Out.Scalar = TuningField(0x14);
		return Out;
	}
	// The tentacle arm: a base plus a per-tentacle term over `6 - m_iConnectedTentacleCount`
	// (+0x670c), floored at `_DAT_104493d0 = 0.1`.
	float Base = 0.f;
	float Per = 0.f;
	if (Out.bWalkOrRun)
	{
		Base = TuningField(0x5c);
		Per = TuningField(0x60);
	}
	else if (Activity == 0x4b)
	{
		Base = TuningField(0x54);
		Per = TuningField(0x58);
	}
	else
	{
		Base = TuningField(0x4c);
		Per = TuningField(0x50);
	}
	Out.Scalar = Per * static_cast<float>(6 - TentacleCount) + Base;
	if (Out.Scalar <= 0.1f)
	{
		Out.Scalar = 0.1f;
	}
	return Out;
}

// --- Moved from `ElysiumNpcGeometry.cpp` (story 5 step 4) ---

void FElysiumNpcMingXiao::NotifyOwnedCopiesOfOwnerMove(FElysiumEntity* Moved)
{
	// `FUN_10397e00`, 100 bytes. `this` is the OWNER `CNPC_VMingXiao` and `Moved` is the tentacle
	// that moved — `0x1039ef60` above it resolves the pair that way round.
	if (Moved == nullptr)
	{
		// `if (param_1 != 0)` is the whole of retail's first test.
		return;
	}

	// The position handed out is `Moved`'s own (`(**(code **)(*param_1 + 0x364))()`), read ONCE per
	// surviving tentacle inside the loop rather than hoisted — 29c's walk reads it as "this
	// entity's own position", and the listing's receiver is `param_1`.
	const FVector MovedOriginCm = Moved->Origin;

	// `m_rhSeveredTentacles[6]` (`+0x66a8`, family Squad's member), walked all six unconditionally.
	for (int32 Index = 0; Index < GSeveredTentacleCount; ++Index)
	{
		if (!SeveredTentacles[Index].IsSet() || World == nullptr)
		{
			continue;
		}
		FElysiumEntity* Other = World->Resolve(SeveredTentacles[Index]);
		if (Other == nullptr || Other == Moved)
		{
			// Retail's two guards: the handle resolved to something, and it is not `param_1`
			// itself. A tentacle is never told to scatter away from where it already is.
			continue;
		}
		NotifyScatterCenter(Other, MovedOriginCm);
	}
}

bool FElysiumNpcMingXiao::ScatterTentacleGate(const FVector& DeltaCm, const FVector& Forward)
{
	// `10399919`..`10399988`. The delta is normalised IN PLACE and the length `VectorNormalize`
	// answers is the range test, so the dot that follows is against a UNIT direction.
	FVector Direction = DeltaCm;
	const double LengthCm = Direction.Size();
	Direction.Normalize();

	// `FCOMP [0x1046dcd0]` with `AND EAX,0x4100; JZ skip` — the mask keeps both the "below" and the
	// "equal" flags, so the range gate is inclusive at exactly 128 Source units.
	if (!(LengthCm <= GScatterRangeUnits * ElysiumMove::U))
	{
		return false;
	}

	// `1039996b`..`10399988`: `dir.y * m_vecForward[1] + dir.x * m_vecForward[0]`, a **2-D** dot —
	// Z is not multiplied by anything — compared against the DOUBLE `_DAT_10449260` = 0.25 with
	// `TEST AH,0x5; JNP skip`, which proceeds at or above. A 75.5-degree half-angle in front.
	const double Dot = Direction.X * Forward.X + Direction.Y * Forward.Y;
	return Dot >= GScatterForwardDotFloor;
}

void FElysiumNpcMingXiao::FUN_103998d0(FElysiumEntity* Tentacle)
{
	// `0x103998d0`, 212 bytes — `CNPC_VMingXiao::CoordinateTroops`'s severed-tentacle half.
	// `CoordinateTroops` (`0x10399610`) runs it on ONE tentacle per call, walking
	// `m_iCoordinateTentacleID` (`+0x6740`) 0..5 and wrapping, so the whole set is coordinated over
	// six calls rather than every call.
	// The walk hands in `m_rhSeveredTentacles[m_iCoordinateTentacleID]` (`+0x66a8`), the severed
	// tentacles; the tentacle's words are read through its class. (`m_rhProxies` holds
	// `CNPC_VMingXiao` proxies, not tentacles.)
	FElysiumNpc* AsNpc = Tentacle ? Tentacle->AsNpc() : nullptr;
	FElysiumNpcMingXiaoTentacle* TentacleNpc =
		AsNpc ? AsNpc->AsSpecies<FElysiumNpcMingXiaoTentacle>() : nullptr;
	if (TentacleNpc == nullptr)
	{
		return;
	}

	// `103998db`..`103998fe`: three gates on the tentacle's own words, all three before anything is
	// measured. `m_iForcedSchedule` (`+0x65c8`) is `FElysiumNpcScheduleHost::ForcedSchedule` in this
	// runtime, which carries a registered schedule id; the two refused numbers are retail's and are
	// compared as numbers.
	const int32 ForcedSchedule = static_cast<int32>(TentacleNpc->ScheduleHost.ForcedSchedule);
	if (ForcedSchedule == GScatterRefusedScheduleA || ForcedSchedule == GScatterRefusedScheduleB)
	{
		return;
	}
	if (TentacleNpc->TentaclePhase != GScatterRequiredPhase)
	{
		return;
	}

	// `10399904`..`10399931`: the delta is the TENTACLE's origin minus mine.
	const FVector DeltaCm = TentacleNpc->Origin - Origin;

	// `m_vecForward` (`+0x6290`) is `FElysiumNpc::Forward`, retail's cached facing basis. Nothing in
	// this runtime writes it yet (the shape map says so: "this runtime recomputes it per query"), so
	// the cone gate takes its refusal arm until a sense pass fills it.
	if (!ScatterTentacleGate(DeltaCm, Forward))
	{
		return;
	}

	// `1039998a`: the centre handed over is MY origin — the tentacle is told to scatter away from
	// the boss, which is the opposite receiver from `0x10397e00`'s.
	NotifyScatterCenter(TentacleNpc, Origin);
}

// --- Moved from `ElysiumNpcMotor.cpp` (story 5 step 4) ---

float FElysiumNpcMingXiao::MaxYawSpeedMingXiao(int32 Activity, TFunctionRef<float(int32)> TuningField)
{
	// `CNPC_VMingXiao::MaxYawSpeed` `0x10394930`: the tuning record `0x101e8da0(0x10739d08)`'s
	// +0x48 inside the half-open band `(0x1129, 0x112e)` and its +0x44 everywhere else.
	if (0x1129 < Activity && Activity < 0x112e)
	{
		return TuningField(0x48);
	}
	return TuningField(0x44);
}

void FElysiumNpcMingXiao::SetBlockedByFriend(bool bBlocked)
{
	// `FUN_1039aaf0` `0x1039aaf0`: `this->+0x6750 = param_1`.
	bBlockedByFriend = bBlocked;
}

bool FElysiumNpcMingXiao::BlockedByFriend() const
{
	// `FUN_1039ab10` `0x1039ab10`: `return this->+0x6750;`.
	return bBlockedByFriend;
}

// --- Moved from `ElysiumNpcPrecache10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSaveRestore10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSchedule.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSenses10_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSquad.cpp` (story 5 step 4) ---

void FElysiumNpcMingXiao::CoordinateTroops()
{
	// 0x10399610 `CNPC_VMingXiao::CoordinateTroops`, one index per call:
	//
	//   id = m_iCoordinateTentacleID
	//   if (m_rhSeveredTentacles[id] resolves) 0x103998d0(this, tentacle)
	//   if (m_rhProxies[id]          resolves) 0x103999f0(this, proxy)
	//   if (++m_iCoordinateTentacleID > 5) m_iCoordinateTentacleID = 0
	//
	// The index advance and the wrap at 5 are this body; the two per-troop arms are rows of their
	// own and are NOT ported here:
	//   * `0x103998d0` — the severed tentacle's re-aim: skip when its schedule is 0x163/0x165 or
	//     its state is not 2, then a distance and a 2-D dot against `+0x6290/+0x6294` before
	//     `0x1039ef90(tentacle, myOrigin)`;
	//   * `0x103999f0` — the proxy pair's swap: `0x1039aaf0(x, 0)` on both, then two
	//     distance/dot tests against the enemy that re-arm one of them with `0x1039aaf0(x, 1)`.
	// Both are asked here and answer nothing, as the handle seam does.
	const int32 Id = CoordinateTentacleId;
	if (Id >= 0 && Id < static_cast<int32>(UE_ARRAY_COUNT(SeveredTentacles)) && World != nullptr)
	{
		if (World->Resolve(SeveredTentacles[Id]) != nullptr)
		{
			// 0x103998d0, unported.
		}
		if (World->Resolve(Proxies[Id]) != nullptr)
		{
			// 0x103999f0, unported.
		}
	}
	CoordinateTentacleId = Id + 1;
	if (CoordinateTentacleId > 5)
	{
		CoordinateTentacleId = 0;
	}
}

// --- Moved from `ElysiumNpcTranslate19.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcKernelBosses2.cpp` (story 5 step 4) ---

// -------------------------------------------------------------------------------------------------
// `CNPC_VMingXiao` — the tentacle rules.
// -------------------------------------------------------------------------------------------------

void FElysiumNpcMingXiao::SeverTentacle(int32 TentacleId)
{
	// SEAM for `0x10397930`, which is no family's row. The two writes this substrate can make are
	// made (`m_rhProxies[id]` and `m_rhSeveredTentacles[id]`, family Squad's members); the hit
	// points, the attack and regrow timers, `m_rbProxyRegistered` and the bodygroup set are records.
	LastSeveredTentacle = TentacleId;
	if (TentacleId >= 0 && TentacleId < 6)
	{
		SeveredTentacles[TentacleId] = FElysiumEntityHandle::Invalid();
		Proxies[TentacleId] = FElysiumEntityHandle::Invalid();
		// `m_rflAttackTimers[id] = curtime + _DAT_1044e664` — that cell lives past `.data`'s raw
		// size and is **unrecovered**, so the stamp is left alone rather than guessed.
	}
}
