#include "Substrate/ElysiumNpcAsianVampire.h"

#include "Substrate/ElysiumNpcKernelClassLookup.h"
#include "ElysiumEntity.h"
#include "ElysiumEntityDefs.h"
#include "ElysiumEntityWorld.h"
#include "ElysiumMoveSolve.h"
#include "ElysiumPlayer.h"
#include "ElysiumRng.h"
#include "ElysiumStub.h"
#include "ElysiumWorldServices.h"
#include "Substrate/ElysiumHint.h"
#include "Substrate/ElysiumInterestingPlace.h"
#include "Substrate/ElysiumItemClasses.h"
#include "Substrate/ElysiumLaw.h"
#include "Substrate/ElysiumMiscFlags.h"
#include "Substrate/ElysiumNpcConditions.h"
#include "Substrate/ElysiumNpcEnemy.h"
#include "ElysiumNpcFlags.h"
#include "Substrate/ElysiumNpcCombat10_2Shared.h"
#include "Substrate/ElysiumNpcConditions10Shared.h"
#include "Substrate/ElysiumNpcGeometryShared.h"
#include "Substrate/ElysiumNpcHintsShared.h"
#include "Substrate/ElysiumNpcLifecycle19_2Shared.h"
#include "Substrate/ElysiumNpcMotor2Shared.h"
#include "Substrate/ElysiumNpcMotorShared.h"
#include "Substrate/ElysiumNpcPositionsShared.h"
#include "Substrate/ElysiumNpcPrecache10Shared.h"
#include "Substrate/ElysiumNpcScheduleShared.h"
#include "Substrate/ElysiumNpcKernelShape.h"
#include "Substrate/ElysiumNpcLog.h"
#include "Substrate/ElysiumNpcMind.h"
#include "Substrate/ElysiumNpcMingXiaoTentacle.h"
#include "Substrate/ElysiumNpcScheduleHost.h"
#include "Substrate/ElysiumNpcSenses.h"
#include "Substrate/ElysiumNpcWitness.h"
#include "Substrate/ElysiumRelationships.h"
#include "Substrate/ElysiumRetailHullTable.h"
#include "Substrate/ElysiumSchedule.h"
#include "Substrate/ElysiumWeaponClasses.h"

// --- File-scope helpers moved with this class's bodies (story 5 step 4) ---

namespace
{
	const TCHAR* const GAsianVampireFile = TEXT("NPC_AVampire.cpp");
	constexpr float GMotorTailAsianJumpRise = 100.0f;        // _DAT_104a9310
	constexpr float GAsianJumpScheduleDrop = 40.0f; // _DAT_104a9314
	constexpr float GAsianJumpNear = 30.0f;         // _DAT_104a9308
	constexpr float GAsianStationaryTime = 3.0f;    // _DAT_104a9318
	constexpr float GAsianMovedEpsilon = 40.0f;     // _DAT_104a931c
	constexpr float GJumpbaseClearance = 150.0f;    // DAT_104a9320
	constexpr int32 GJumpbaseHintType = 18000;
	constexpr int32 GSchedJumpDown = 0x15b;
	constexpr int32 GSchedJumpAcross = 0x15a;
	// `CNPC_VAsianVampire::SelectLedgeNode` `0x103615c0`'s clearance.
	constexpr float AsianLedgeClearance = 150.0f;         // DAT_104a9320
}

const FElysiumNpcClass* FElysiumNpcAsianVampire::OwnRetailClass() const
{
	static const FElysiumNpcClass* const Row = ElysiumNpcKernelClass::Find(RetailClassName);
	return Row;
}

// Slot 420: `0x10360ce0`.
// `0x10360ce0`
void FElysiumNpcAsianVampire::NPCInit()
{
	VampireBossNPCInit();                                                // 10360ce0
	const FVector OriginUnits = Origin / ElysiumMove::U;
	LastJumpPosition[0] = OriginUnits;                                   // 10360d45 slot 217
	LastJumpPosition[1] = OriginUnits;
	LastJumpPositionIdx = 0;                                             // +0x66d0 CORRECTION
	BaseScheduleHost.HintNode = INDEX_NONE;                                  // +0x5ddc inherited
	bAsianVampirePathBlocked = false;                                    // +0x66d4
	MovedTimeStamp = NpcKernelLifecycle19_2Shared::Lifecycle19_2Now(*this);                            // +0x66d8
	MovedPosition = OriginUnits;                                         // +0x66dc
	bSuppressRanged = true;                                  // +0x66e8
	JumpGravity = AsianVampireJumpGravity;                               // +0x64b8
}

// Slot 104: `0x10360bc0`.
// 0x10360bc0
void FElysiumNpcAsianVampire::Precache()
{
	// `CNPC_VAsianVampire::Precache` `0x10360bc0` — a scope-trace frame naming
	// `"CNPC_VAsianVampire::Precache"` with two empty operands, the Troika body, exactly one
	// `UTIL_PrecacheOther`, and the frame popped. That single weapon is the whole species payload.
	//
	// The scope-trace frame (`g_ScopeTraceStack`) is retail's VPROF-style profiling stack. It has no
	// port and nothing the kernel reads depends on it; the four arms here that push one say so and
	// carry no code for it.
	TroikaPrecache();
	NpcKernelPrecache10Shared::Precache10Other(*this, TEXT("item_w_avamp_blade"));
}

// Slot 461: `0x10361060`, the selector tag 0x6 and then a direct call into the human line's `0x103851e0`.
int32 FElysiumNpcAsianVampire::SelectIdealStateRetail()
{
	SelectIdealStateSelector = 0x6;
	return HumanSelectIdealState();
}

// Slot 604: `0x10361be0`, which replaces the Troika body wholesale; its argument is read by no arm.
// `CNPC_VAsianVampire::SelectScheduleMeleeCombat` `0x10361be0`, its slot-604 override's body.
int32 FElysiumNpcAsianVampire::SelectScheduleMeleeCombat(int32 Unused)
{
	(void)Unused;
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	FElysiumEntity* Enemy = const_cast<FElysiumEntity*>(World != nullptr
		? ElysiumNpcCond::ResolveEnemyHandle(*World, BaseMemory.Enemy) : nullptr);
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	if (!bInMelee)
	{
		if (!Slot599(0))
		{
			return NpcKernelScheduleShared::HasUsableRangedWeaponPort(*this) ? 0x15c : 0xe4;
		}
	}
	else if (Slot602())   // vtable +0x968
	{
		Slot601(Enemy);
		if (NpcKernelScheduleShared::HasUsableRangedWeaponPort(*this))
		{
			return 0x15c;
		}
		// `if (range + range < dist != (range + range == dist))` — the decompiler's spelling of
		// the FPU compare; it is `dist > 2 * range`.
		return ScheduleHost.EnemyDistUnits > MeleeRangeUnits() * 2.0f ? 0xe7 : 0xe4;
	}
	if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
	{
		Slot601(Enemy);
		return GetJumpSchedule(Enemy);
	}
	if (Conds.Has(EElysiumNpcCond::CanMeleeAttack1))
	{
		return NpcKernelScheduleShared::HasUsableRangedWeaponPort(*this) ? 0xdc : 0xdd;
	}
	const bool bHeightArmed = NpcKernelScheduleShared::TickMeleeHeightDiffTimer(*this, Now);
	if (NpcKernelScheduleShared::HasUsableRangedWeaponPort(*this)
		&& (Conds.Has(EElysiumNpcCond::TooFarForMelee)
			|| Conds.Has(EElysiumNpcCond::InterruptTime) || bHeightArmed)
		&& !bSuppressRanged)
	{
		Slot601(Enemy);
		return 0x15c;
	}
	if (!Conds.Has(EElysiumNpcCond::TooFarForMelee)
		&& !Conds.Has(EElysiumNpcCond::TooFarToAttack)
		&& !Conds.Has(EElysiumNpcCond::EnemyOccluded))
	{
		return 199;
	}
	if (NpcKernelScheduleShared::HasUsableRangedWeaponPort(*this) && !bSuppressRanged)
	{
		return 0xca;
	}
	return 0xcb;
}

// Slot 448: `0x10362390`, its own arm and then a direct call into the Troika body `0x1029adb0`.
/** `CNPC_VAsianVampire::TaskFail` (`0x10362390`) — on failure codes 12..15 (`0xb < code && code <
 *  0x10`) it sets `m_bPathBlocked` (`+0x66d4`) and nothing else. */
void FElysiumNpcAsianVampire::TaskFail(int32 Reason)
{
	// `CNPC_VAsianVampire::TaskFail` (`0x10362390`), 117 bytes, of which the scope-trace push is
	// most. `103623c5`: `if (0xb < code && code < 0x10) m_bPathBlocked = 1;`.
	if (Reason >= NpcKernelConditions10Shared::GCond10PathFailFirst && Reason <= NpcKernelConditions10Shared::GCond10PathFailLast)
	{
		bAsianVampirePathBlocked = true;                                      // +0x66d4
	}
	FElysiumNpc::TaskFail(Reason);
}

// Slot 605: `0x103620d0`
/** `CNPC_VAsianVampire::SelectScheduleRangedCombat` (`0x103620d0`), 546 bytes. Answers `0xf0` where
 *  the Troika base answers `0xb8` for COND `0x3c`, and against the human arm it has no dodge helper,
 *  no slot-606 arm and no cover-hint arm. */
int32 FElysiumNpcAsianVampire::SelectScheduleRangedCombat(int32 Arg)
{
	const FElysiumNpcConditions& Conds = Cognition.Conditions;

	// `1036213a`: arm 1 — `m_bInMelee`.
	if (bInMelee)
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe3"), GAsianVampireFile, 0x26a));
		return 0xe3;
	}
	// `10362163`: arm 2 — the same COND `0x8` / slot 307 / slot 599 triple.
	if (Conds.Has(EElysiumNpcCond::TooCloseForRanged) && NpcKernelCombat10_2Shared::HasUsableMeleeWeaponPort(*this)
		&& NpcKernelCombat10_2Shared::EnemySlot599(*this))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xe3"), GAsianVampireFile, 0x284));
		return 0xe3;
	}
	// `103621c2`: arm 3 — `COND 0x3c` answers **0xf0**, where the Troika base answers 0xb8.
	if (Conds.Has(EElysiumNpcCond::WeaponThroughWall))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xf0"), GAsianVampireFile, 0x289));
		return 0xf0;
	}
	// `103621f4`: arm 4 — the weapon pre-pass. This body offers NEITHER the door helper NOR the
	// combat-reaction prologue, and it has no dodge helper, no slot-606 arm and no cover-hint arm.
	if (const int32 PrePass = RangedWeaponPrePass(); PrePass != 0)
	{
		return PrePass;
	}
	// `10362210`: COND `0x5f` OR COND `0x8`, and neither `0x2f` nor `0x63` → 0xf0.
	if (Conds.Has(EElysiumNpcCond::TooCloseToAttack) || Conds.Has(EElysiumNpcCond::TooCloseForRanged))
	{
		if (!Conds.Has(EElysiumNpcCond::WaitingAttackTime)
			&& !Conds.Has(EElysiumNpcCond::WeaponBlockedByFriend))
		{
			RecordScheduleEvent(FString::Printf(
				TEXT("SelectScheduleRangedCombat %s:%d -> 0xf0"), GAsianVampireFile, 0x2af));
			return 0xf0;
		}
	}
	// `10362253`: `COND_SEE_ENEMY (0x46)` and not `0x48` and not `0x60` → 0xf0.
	if (Conds.Has(EElysiumNpcCond::SeeEnemy) && !Conds.Has(EElysiumNpcCond::EnemyOccluded)
		&& !Conds.Has(EElysiumNpcCond::TooFarToAttack))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> 0xf0"), GAsianVampireFile, 0x2e5));
		return 0xf0;
	}
	// `103622b1`: `COND_ENEMY_UNREACHABLE (0x59)` → `GetJumpSchedule`, else 0xe8.
	if (Conds.Has(EElysiumNpcCond::EnemyUnreachable))
	{
		RecordScheduleEvent(FString::Printf(
			TEXT("SelectScheduleRangedCombat %s:%d -> jump"), GAsianVampireFile, 0x2ce));
		return GetJumpSchedule(NpcKernelCombat10_2Shared::RangedEnemy(*this));
	}
	RecordScheduleEvent(FString::Printf(
		TEXT("SelectScheduleRangedCombat %s:%d -> 0xe8"), GAsianVampireFile, 0x2d2));
	return 0xe8;
}

// Slot 440: `0x10362910`.
/** The species slot-440 bodies, each its class's `TranslateScheduleRetail` override's body. */
// `0x10362910`
// `0x10362910`, `CNPC_VAsianVampire::TranslateSchedule`, the body of `FElysiumNpcAsianVampire::TranslateScheduleRetail`
// (story 5 step 3). Every miss is a direct call into the Troika body `0x102b12f0`.
int32 FElysiumNpcAsianVampire::TranslateScheduleRetail(int32 ScheduleNumber)
{
	// `10362972`: the arm CALLS `CNPC_VAsianVampire::GetJumpSchedule` (`0x10362430`) and
	// returns its answer; Troika is never reached. The C renders the return value away.
	if (ScheduleNumber > 0xe4 && ScheduleNumber < 0xe7)
	{
		return GetJumpSchedule();
	}
	return TroikaTranslateScheduleRetail(ScheduleNumber);
}

// Slot 566: `0x10361470`, a replacement that does not chain.
bool FElysiumNpcAsianVampire::FValidateHintType(void* Hint)
{
	// The whole body is `return 1;`: the hint is never read.
	(void)Hint;
	return true;
}

// Slot 546: `0x10360610`, the class's own schedule id space.
const TCHAR* FElysiumNpcAsianVampire::SquadSlotName(int32 SlotEn)
{
	// The class's `CAI_ClassScheduleIdSpace` `0x1093a578`, left empty by `0x102ea090(isRoot = false)`:
	// `SlotEn` translates to -1 and names `<<null>>`.
	static constexpr FSquadSlotSpecies IdSpace = {
		TEXT("CNPC_VAsianVampire"), TEXT("0x10360610"), TEXT("0x1093a578") };
	return GlobalSquadSlotName(SquadSlotLocalToGlobal(&IdSpace, SlotEn));
}

// Slot 127: `0x10360e10`, whose body is the `CNPC_VVampireBoss` restore (`0x103c5910`, family
// SaveRestore10's `VampireBossRestore`) — the census's mechanism row for this class.
int32 FElysiumNpcAsianVampire::Restore(void* Archive)
{
	return VampireBossRestore(Archive);
}

// --- Moved from `ElysiumNpcCombat10_2.cpp` (story 5 step 4) ---

// --- `CNPC_VAsianVampire::SelectScheduleRangedCombat` `0x103620d0`, 546 bytes ---------------------

// --- Moved from `ElysiumNpcConditions10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcGeometry.cpp` (story 5 step 4) ---

bool FElysiumNpcAsianVampire::StandingOnPlayerOverlap(const FVector& MyOriginCm, const FVector& OtherOriginCm,
	const FVector& MyMinsCm, const FVector& MyMaxsCm, const FVector& OtherMinsCm,
	const FVector& OtherMaxsCm)
{
	// `10362730`'s tail. Retail builds three 2-D lengths with `sqrtf` and compares one against the
	// sum of the other two, each scaled by `_DAT_104454d0` (0.5):
	//
	//     sqrtf((o.x-m.x)^2 + (o.y-m.y)^2)
	//       <   sqrt((oMax.x-oMin.x)^2 + (oMax.y-oMin.y)^2) * 0.5
	//         + sqrt((mMax.x-mMin.x)^2 + (mMax.y-mMin.y)^2) * 0.5
	//
	// The two right-hand terms are HALF-DIAGONALS of the XY footprints, not radii: for a 32x32 hull
	// that is 22.6 units, not 16. So the test admits a diagonal overlap a circle of the box's
	// half-width would refuse, and the comparison is STRICTLY less — exactly touching is not
	// standing on.
	const double Separation = FVector2D(OtherOriginCm.X - MyOriginCm.X,
		OtherOriginCm.Y - MyOriginCm.Y).Size();
	const double OtherHalfDiagonal = FVector2D(OtherMaxsCm.X - OtherMinsCm.X,
		OtherMaxsCm.Y - OtherMinsCm.Y).Size() * NpcKernelGeometryShared::GRetailHalf;
	const double MyHalfDiagonal = FVector2D(MyMaxsCm.X - MyMinsCm.X,
		MyMaxsCm.Y - MyMinsCm.Y).Size() * NpcKernelGeometryShared::GRetailHalf;
	return Separation < OtherHalfDiagonal + MyHalfDiagonal;
}

bool FElysiumNpcAsianVampire::StandingOnPlayer() const
{
	// `0x10362730`, 383 bytes, retail-named. The scope-trace pair is the outer 100 of them.
	//
	// The subject is `m_hClosestPlayer` (`+0x628c`), the sense pass's cache, NOT `GetEnemy()` — an
	// asian vampire standing on a player it is not fighting still answers true.
	const FElysiumNpcMemory& Mem = Senses.Memory;
	FElysiumEntity* Player = (Mem.ClosestPlayer.IsSet() && World)
		? World->Resolve(Mem.ClosestPlayer) : nullptr;
	if (Player == nullptr)
	{
		// `thunk_FUN_100290c0` answered null — retail falls straight to the `return false` tail.
		return false;
	}

	FVector PlayerMinsUnits = FVector::ZeroVector;
	FVector PlayerMaxsUnits = FVector::ZeroVector;
	FVector MyMinsUnits = FVector::ZeroVector;
	FVector MyMaxsUnits = FVector::ZeroVector;
	// `piVar14[0x9c]` is the player's `m_Collision` and `this->m_Collision` is `+0x270`; slots 4 and
	// 8 on each are the OBB mins and maxs. Family Motor's extents seam is the same absent collision
	// property and answers both as zero, which collapses both half-diagonals to zero and makes the
	// test "are the two origins at exactly the same XY point" — the conservative refusal, stated.
	RetailCollisionExtents(*Player, PlayerMinsUnits, PlayerMaxsUnits);
	RetailCollisionExtents(*this, MyMinsUnits, MyMaxsUnits);

	return StandingOnPlayerOverlap(Origin, Player->Origin, MyMinsUnits * ElysiumMove::U,
		MyMaxsUnits * ElysiumMove::U, PlayerMinsUnits * ElysiumMove::U,
		PlayerMaxsUnits * ElysiumMove::U);
}

// --- Moved from `ElysiumNpcHints.cpp` (story 5 step 4) ---

void FElysiumNpcAsianVampire::AddHintToStoredJumpPositions(const FHintWords& Hint)
{
	// `CNPC_VAsianVampire::AddHintToStoredJumpPositions` (`0x10361990`). A two-slot ring: write the
	// hint's origin at the index, advance, and wrap when the advanced index is greater than 1. The
	// buffer size 2 is baked into the wrap test, not read from anywhere.
	if (!Hint.bValid)
	{
		return;
	}
	// The ring lives on family Motor's `LastJumpPosition` pair, in SOURCE UNITS.
	LastJumpPosition[LastJumpPositionIdx] = Hint.OriginCm / ElysiumMove::U;
	++LastJumpPositionIdx;
	if (LastJumpPositionIdx > 1)
	{
		LastJumpPositionIdx = 0;
	}
}

// --- Moved from `ElysiumNpcLifecycle19_2.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcMotor.cpp` (story 5 step 4) ---

bool FElysiumNpcAsianVampire::PositionClearForTeleport(const FVector& PositionUnits, float RadiusUnits) const
{
	// `CNPC_VAsianVampire::PositionClearForTeleport(pos, 150.0)`. **SEAM**: answers false, so
	// `SelectJumpbaseNode` finds no node rather than choosing one blind.
	(void)PositionUnits;
	(void)RadiusUnits;
	return false;
}

void FElysiumNpcAsianVampire::AddHintToStoredJumpPositions(int32 HintNode)
{
	// `CNPC_VAsianVampire::AddHintToStoredJumpPositions(hint)` — the ring write that pairs with
	// `IsPosNearStoredJumpPositions`: store the hint's origin at `m_iLastJumpPositionIdx` (+0x66d0)
	// and advance modulo the ring's two entries. The hint's ORIGIN is the seam.
	FVector HintOriginUnits = FVector::ZeroVector;
	if (!NavHintNodeOrigin(HintNode, HintOriginUnits))
	{
		return;
	}
	LastJumpPosition[LastJumpPositionIdx % 2] = HintOriginUnits;
	LastJumpPositionIdx = (LastJumpPositionIdx + 1) % 2;
}

// --- Moved from `ElysiumNpcKernelMotor2.cpp` (story 5 step 4) ---

void FElysiumNpcAsianVampire::AsianVampireSetupJump(float Enabled)
{
	// `CNPC_VAsianVampire::SetupJump` `0x10361a70`, rise `_DAT_104a9310` = 100.0.
	SetupJumpRise(Enabled, GMotorTailAsianJumpRise);
}

int32 FElysiumNpcAsianVampire::GetJumpSchedule() const
{
	// `CNPC_VAsianVampire::GetJumpSchedule` `0x10362430`:
	//     if (m_hClosestPlayer resolves
	//         && player->GetAbsOrigin().z - GetAbsOrigin().z < -40.0)   // -_DAT_104a9314
	//         return 0x15b;
	//     return 0x15a;
	// The threshold is the NEGATED constant: the player has to be more than 40 units BELOW this NPC.
	FElysiumPlayer* Player = Senses.Memory.ClosestPlayer.IsSet() && World != nullptr
		? World->FindPlayer()
		: nullptr;
	if (Player != nullptr && !Player->IsInert()
		&& Player->Handle == Senses.Memory.ClosestPlayer)
	{
		const float DeltaZ = static_cast<float>(NpcKernelMotor2Shared::MotorTailSourceOf(Player->Origin).Z - NpcKernelMotor2Shared::MotorTailSourceOf(Origin).Z);
		if (DeltaZ < -GAsianJumpScheduleDrop)
		{
			return GSchedJumpDown;
		}
	}
	return GSchedJumpAcross;
}

bool FElysiumNpcAsianVampire::IsPosNearStoredJumpPositions(const FVector& PositionUnits) const
{
	// `CNPC_VAsianVampire::IsPosNearStoredJumpPositions` `0x103618a0`: a fixed two-iteration loop
	// over `m_vLastJumpPosition` (+0x66b8), each entry tested by 2-D distance against
	// `_DAT_104a9308` = 30.0. The comparison is `dist < 30.0`, not `<=`.
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FVector Delta(LastJumpPosition[Index].X - PositionUnits.X,
			LastJumpPosition[Index].Y - PositionUnits.Y, 0.0);
		if (NpcKernelMotor2Shared::Length2D(Delta) < GAsianJumpNear)
		{
			return true;
		}
	}
	return false;
}

bool FElysiumNpcAsianVampire::StationaryForTooLong() const
{
	// `CNPC_VAsianVampire::StationaryForTooLong` `0x10362670`:
	//     return 3.0 <= curtime - m_fMovedTimeStamp;     // _DAT_104a9318, inclusive
	const double Now = World != nullptr ? World->NowSeconds() : 0.0;
	return GAsianStationaryTime <= Now - MovedTimeStamp;
}

void FElysiumNpcAsianVampire::UpdateMovedTimeStamp()
{
	// `CNPC_VAsianVampire::UpdateMovedTimeStamp` `0x10362540`:
	//     if (40.0 < Length(GetAbsOrigin() - m_vMovedPosition)) {    // _DAT_104a931c, exclusive
	//         m_fMovedTimeStamp = curtime;
	//         m_vMovedPosition  = GetAbsOrigin();
	//     }
	// The distance is 3-D, and the write order is stamp first, position second — which matters
	// because retail re-reads `GetAbsOrigin()` for the second write.
	const FVector SelfUnits = NpcKernelMotor2Shared::MotorTailSourceOf(Origin);
	const FVector Delta = SelfUnits - MovedPosition;
	if (GAsianMovedEpsilon < NpcKernelMotor2Shared::Length3D(Delta))
	{
		MovedTimeStamp = World != nullptr ? World->NowSeconds() : 0.0;
		MovedPosition = SelfUnits;
	}
}

int32 FElysiumNpcAsianVampire::SelectJumpbaseNode()
{
	// `CNPC_VAsianVampire::SelectJumpbaseNode` `0x10361730`:
	//     best = NULL;  bestDist = FLT_MAX;
	//     for (hint = g_pHintList; hint; hint = hint->+0x5d8)
	//         if (hint->m_nHintType == 18000
	//             && PositionClearForTeleport(hint->GetAbsOrigin(), 150.0))      // DAT_104a9320
	//         {
	//             float d = Length(GetAbsOrigin() - hint->GetAbsOrigin());        // 3-D
	//             if (d < bestDist) { bestDist = d; best = hint; }
	//         }
	//     if (best) AddHintToStoredJumpPositions(best);
	//     return best;
	//
	// Identical in shape to `SelectLedgeNode`, which filters hint type 0x4653 instead.
	// **SEAM**: the global hint list answers empty, so the search finds nothing — which is retail's
	// own answer for a map with no `jumpbase` hints.
	int32 Best = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	TArray<int32> Hints;
	NavAllHintNodes(Hints);
	const FVector SelfUnits = NpcKernelMotor2Shared::MotorTailSourceOf(Origin);
	for (int32 Hint : Hints)
	{
		int32 Type = 0;
		if (!NavHintNodeType(Hint, Type) || Type != GJumpbaseHintType)
		{
			continue;
		}
		FVector HintUnits = FVector::ZeroVector;
		if (!NavHintNodeOrigin(Hint, HintUnits))
		{
			continue;
		}
		if (!PositionClearForTeleport(HintUnits, GJumpbaseClearance))
		{
			continue;
		}
		const float Distance = NpcKernelMotor2Shared::Length3D(SelfUnits - HintUnits);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Hint;
		}
	}
	if (Best != INDEX_NONE)
	{
		AddHintToStoredJumpPositions(Best);
	}
	return Best;
}

// --- Moved from `ElysiumNpcPositions.cpp` (story 5 step 4) ---

int32 FElysiumNpcAsianVampire::SelectLedgeNodeAsianRule(TArrayView<const FHintWords> Nodes,
	const FVector& SelfCm, TFunctionRef<bool(const FVector&, float)> Clear)
{
	// The one selector of the eight that never asks for a player: type `0x4653`, the clearance gate
	// FIRST (at `DAT_104a9320 = 150.0`, and the clearance is checked before the distance is even
	// computed), then the 3-D distance to this NPC's OWN origin, nearest wins.
	int32 Best = INDEX_NONE;
	float BestDistance = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Nodes.Num(); ++Index)
	{
		if (Nodes[Index].HintType != NpcKernelPositionsShared::HintLedge)
		{
			continue;
		}
		const FVector& NodeCm = Nodes[Index].OriginCm;
		if (!Clear(NodeCm, AsianLedgeClearance * NpcKernelPositionsShared::U))
		{
			continue;
		}
		const float Distance = static_cast<float>(FVector::Dist(SelfCm, NodeCm));
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Index;
		}
	}
	return Best;
}

int32 FElysiumNpcAsianVampire::SelectLedgeNodeAsian()
{
	TArray<FHintWords> Nodes;
	TArray<int32> NodeIds;
	GatherHintNodes(Nodes, NodeIds);
	const int32 Pick = SelectLedgeNodeAsianRule(Nodes, Origin,
		[this](const FVector& PositionCm, float ClearanceCm)
		{
			return PositionClearForTeleportAsian(PositionCm, ClearanceCm);
		});
	if (Pick == INDEX_NONE)
	{
		return INDEX_NONE;   // retail's own `if (piVar5 != NULL)` guard before the store
	}
	// `AddHintToStoredJumpPositions(winner)` — family Hints' body (`0x10361990`), which pushes the
	// hint's origin into the two-slot ring and advances the index. Calling it is the whole of the
	// tail.
	AddHintToStoredJumpPositions(Nodes[Pick]);
	return NodeIds[Pick];
}

bool FElysiumNpcAsianVampire::PositionClearForTeleportAsian(const FVector& PositionCm, float ClearanceCm) const
{
	// `0x103629d0`, the whole body:
	//     if (IsPosNearStoredJumpPositions(pos))           return false;
	//     if (player resolves && |player.xy - pos.xy| <= clearance) return false;
	//     if (|GetAbsOrigin().xy - pos.xy| < clearance)    return false;
	//     return true;
	// Both distance terms are FLAT — the Asian vampire's ledges are stacked, so a candidate directly
	// above him is "clear" — and the player term's compare is `<=` where his own is `<`. The
	// listing's `(a < b) != (a == b)` is that `<=`; the asymmetry is retail's and is reproduced.
	if (IsPosNearStoredJumpPositions(PositionCm))
	{
		return false;
	}
	const FElysiumPlayer* Player =
		Senses.Memory.ClosestPlayer.IsSet() && World != nullptr ? World->FindPlayer() : nullptr;
	if (Player != nullptr && Player->Handle == Senses.Memory.ClosestPlayer)
	{
		if (NpcKernelPositionsShared::FlatDistance(Player->Origin, PositionCm) <= ClearanceCm)
		{
			return false;
		}
	}
	if (NpcKernelPositionsShared::FlatDistance(Origin, PositionCm) < ClearanceCm)
	{
		return false;
	}
	return true;
}

// --- Moved from `ElysiumNpcPrecache10.cpp` (story 5 step 4) ---

// --- Moved from `ElysiumNpcSchedule.cpp` (story 5 step 4) ---

int32 FElysiumNpcAsianVampire::GetJumpSchedule(FElysiumEntity* Enemy) const
{
	// SEAM for `GetJumpSchedule` (`CNPC_VAsianVampire`'s `COND_ENEMY_UNREACHABLE` arm). No jump
	// schedule family is registered here and the retail body is not one of this story's rows.
	(void)Enemy;
	return 0;
}

// --- Moved from `ElysiumNpcTranslate19.cpp` (story 5 step 4) ---

